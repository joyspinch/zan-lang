
/* LINQ 查询表达式完整降解实现 */

typedef struct {
    zan_istr_t seq_name;
    LLVMValueRef seq_value;
    /* LINQ 查询中间结果列表生命周期管理标记 */
    bool seq_owned;
    zan_type_t *elem_type;
    bool row_mode;
    zan_type_t *row_type;
    int row_fields;
    zan_istr_t *field_names;
    zan_type_t **field_types;
} query_seq_t;

typedef struct {
    LLVMValueRef col;
    LLVMValueRef data;
    LLVMValueRef count;
    LLVMValueRef idx;
    LLVMBasicBlockRef cond, body, inc, end;
} query_loop_t;

static int q_synth_counter = 0;

static zan_istr_t query_fresh_name(zan_irgen_t *g, const char *prefix) {
    char buf[40];
    int n = (int)__atomic_fetch_add(&q_synth_counter, 1, __ATOMIC_SEQ_CST);
    snprintf(buf, sizeof buf, "__q%s%d", prefix, n);
    char *p = zan_arena_strdup(g->arena, buf, (size_t)strlen(buf));
    return (zan_istr_t){ p, (uint32_t)strlen(buf) };
}

static zan_ast_node_t *query_ident(zan_irgen_t *g, zan_istr_t name,
                                   zan_loc_t loc) {
    zan_ast_node_t *n = zan_ast_new(g->arena, AST_IDENTIFIER, loc);
    n->ident.name = name;
    return n;
}

static LLVMValueRef query_new_list(zan_irgen_t *g, zan_ast_node_t *at,
                                   zan_type_t *elem) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef raw = emit_alloc_rc_collection(g, at, 24, 1, elem);
    LLVMValueRef lp = LLVMBuildBitCast(g->builder, raw,
        LLVMPointerType(g->list_struct_type, 0), "qlp");
    LLVMValueRef cnt = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
        lp, 0, "qlc");
    zan_store_fit(g, LLVMConstInt(i64, 0, 0), cnt);
    LLVMValueRef cap = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
        lp, 1, "qlcap");
    zan_store_fit(g, LLVMConstInt(i64, 8, 0), cap);
    LLVMValueRef data = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
        lp, 2, "qld");
    LLVMValueRef dsz = LLVMConstInt(i64,
        8 * 8 * (long long)elem_slot_words(g, elem), 0);
    LLVMValueRef dp = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
        get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), dsz },
        2, "qdata");
    zan_irgen_emit_oom_check(g, g->current_fn, dp);
    zan_store_fit(g, LLVMBuildBitCast(g->builder, dp,
        LLVMPointerType(i64, 0), "qdp"), data);
    return LLVMBuildBitCast(g->builder, lp, i8ptr, "qlv");
}

/* 注册保存指定值的隐藏局部变量并返回其 AST 符号 */
/* 结构体值传递：采用指针指向其连续内存存储 */
static bool query_struct_slot(zan_irgen_t *g, LLVMValueRef value,
                              zan_type_t *value_type) {
    if (!value_type) return false;
    LLVMTypeRef want = map_type(g, value_type);
    return want && LLVMGetTypeKind(want) == LLVMStructTypeKind &&
           LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMPointerTypeKind;
}

static zan_istr_t query_hold(zan_irgen_t *g, local_scope_t *locals,
                             const char *prefix, LLVMValueRef value,
                             zan_type_t *value_type) {
    zan_istr_t name = query_fresh_name(g, prefix);
    if (query_struct_slot(g, value, value_type)) {
        local_add(locals, name, value, value_type);
        return name;
    }
    LLVMValueRef alloc = emit_entry_alloca(g, LLVMTypeOf(value), "qh");
    zan_store_fit(g, value, alloc);
    local_add(locals, name, alloc, value_type);
    return name;
}

static void query_declare(zan_irgen_t *g, local_scope_t *locals,
                          zan_istr_t name, LLVMValueRef value,
                          zan_type_t *value_type) {
    if (query_struct_slot(g, value, value_type)) {
        local_add(locals, name, value, value_type);
        return;
    }
    LLVMValueRef alloc = emit_entry_alloca(g, LLVMTypeOf(value), "qdecl");
    zan_store_fit(g, value, alloc);
    local_add(locals, name, alloc, value_type);
}

static void query_emit_add(zan_irgen_t *g, zan_istr_t list_name,
                           zan_ast_node_t *item, zan_loc_t loc,
                           local_scope_t *locals) {
    zan_ast_node_t *madd = zan_ast_new(g->arena, AST_MEMBER_ACCESS, loc);
    madd->member.object = query_ident(g, list_name, loc);
    madd->member.name = (zan_istr_t){ "Add", 3 };
    madd->member.null_cond = 0;
    zan_ast_node_t *addcall = zan_ast_new(g->arena, AST_CALL, loc);
    addcall->call.callee = madd;
    zan_ast_list_init(&addcall->call.args);
    zan_ast_list_init(&addcall->call.type_args);
    zan_ast_list_push(&addcall->call.args, item, g->arena);
    emit_expr(g, addcall, locals);
}

static LLVMValueRef query_emit_enumerable(zan_irgen_t *g, const char *fn,
                                          int fn_len, zan_ast_node_t **args,
                                          int nargs, zan_loc_t loc,
                                          local_scope_t *locals) {
    zan_ast_node_t *m = zan_ast_new(g->arena, AST_MEMBER_ACCESS, loc);
    m->member.object = query_ident(g, (zan_istr_t){ "Enumerable", 10 }, loc);
    m->member.name = (zan_istr_t){ (char *)fn, (uint32_t)fn_len };
    m->member.null_cond = 0;
    zan_ast_node_t *call = zan_ast_new(g->arena, AST_CALL, loc);
    call->call.callee = m;
    zan_ast_list_init(&call->call.args);
    zan_ast_list_init(&call->call.type_args);
    for (int i = 0; i < nargs; i++)
        zan_ast_list_push(&call->call.args, args[i], g->arena);
    return emit_expr(g, call, locals);
}

static LLVMValueRef query_emit_eq(zan_irgen_t *g, zan_istr_t lname,
                                  zan_istr_t rname, zan_loc_t loc,
                                  local_scope_t *locals) {
    zan_ast_node_t *b = zan_ast_new(g->arena, AST_BINARY, loc);
    b->binary.op = TK_EQ_EQ;
    b->binary.left = query_ident(g, lname, loc);
    b->binary.right = query_ident(g, rname, loc);
    return emit_expr(g, b, locals);
}

static void query_loop_open(zan_irgen_t *g, query_loop_t *l,
                            LLVMValueRef list_val) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    l->col = LLVMBuildBitCast(g->builder, list_val,
        LLVMPointerType(g->list_struct_type, 0), "qlc");
    LLVMValueRef cp = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
        l->col, 0, "qlcp");
    l->count = LLVMBuildLoad2(g->builder, i64, cp, "qlcnt");
    LLVMValueRef dp = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
        l->col, 2, "qlcdp");
    l->data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), dp, "qldata");
    l->idx = emit_entry_alloca(g, i64, "qlidx");
    zan_store_fit(g, LLVMConstInt(i64, 0, 0), l->idx);
    l->cond = LLVMAppendBasicBlockInContext(g->ctx, fn, "ql.cond");
    l->body = LLVMAppendBasicBlockInContext(g->ctx, fn, "ql.body");
    l->inc = LLVMAppendBasicBlockInContext(g->ctx, fn, "ql.inc");
    l->end = LLVMAppendBasicBlockInContext(g->ctx, fn, "ql.end");
    LLVMBuildBr(g->builder, l->cond);
    LLVMPositionBuilderAtEnd(g->builder, l->cond);
    LLVMValueRef iv = LLVMBuildLoad2(g->builder, i64, l->idx, "qliv");
    LLVMValueRef cmp = zan_icmp(g->builder, LLVMIntSLT, iv, l->count, "qlcmp");
    LLVMBuildCondBr(g->builder, cmp, l->body, l->end);
    LLVMPositionBuilderAtEnd(g->builder, l->body);
}

static LLVMValueRef query_loop_load(zan_irgen_t *g, query_loop_t *l,
                                    zan_type_t *elem, LLVMValueRef iv) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef elm = map_type(g, elem);
    LLVMValueRef qwidx = slot_word_index(g, iv, elem_slot_words(g, elem));
    LLVMValueRef ep = LLVMBuildGEP2(g->builder, i64, l->data, &qwidx, 1, "qlep");
    /* 集合元素加载梯级类型匹配转换 */
    LLVMValueRef ev = load_collection_slot_value(g, elem, ep);
    LLVMValueRef slot = emit_entry_alloca(g, elm, "qels");
    zan_store_fit(g, ev, slot);
    return slot;
}

static void query_loop_close(zan_irgen_t *g, query_loop_t *l) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    /* 循环体尾部跳转至步进块 */
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
        LLVMBuildBr(g->builder, l->inc);
    LLVMPositionBuilderAtEnd(g->builder, l->inc);
    LLVMValueRef next = zan_add(g->builder,
        LLVMBuildLoad2(g->builder, i64, l->idx, "qli2"),
        LLVMConstInt(i64, 1, 0), "qlnext");
    zan_store_fit(g, next, l->idx);
    LLVMBuildBr(g->builder, l->cond);
    LLVMPositionBuilderAtEnd(g->builder, l->end);
}

/* 注册 LINQ 迭代范围变量 */
static void query_register_iter(zan_irgen_t *g, zan_ast_node_t *expr,
                                query_seq_t *q, LLVMValueRef slot,
                                local_scope_t *locals) {
    if (!q->row_mode) {
        local_add(locals, expr->query.var, slot, q->elem_type);
        return;
    }
    LLVMTypeRef row_llvm = map_type(g, q->row_type);
    for (int i = 0; i < q->row_fields; i++) {
        LLVMValueRef fp = emit_field_ptr(g, q->row_type->sym, row_llvm,
            slot, i, "qrf");
        LLVMValueRef fv = LLVMBuildLoad2(g->builder,
            map_type(g, q->field_types[i]), fp, "qrfv");
        LLVMValueRef fs = emit_entry_alloca(g, map_type(g, q->field_types[i]),
            "qrfs");
        zan_store_fit(g, fv, fs);
        local_add(locals, q->field_names[i], fs, q->field_types[i]);
    }
}

static void query_scope_vars_for_infer(zan_irgen_t *g, zan_ast_node_t *expr,
                                       query_seq_t *q, local_scope_t *locals) {
    if (q->row_mode) {
        for (int i = 0; i < q->row_fields; i++)
            local_add(locals, q->field_names[i], NULL, q->field_types[i]);
    } else {
        local_add(locals, expr->query.var, NULL, q->elem_type);
    }
}

static void query_scope_lets(zan_irgen_t *g, zan_ast_list_t *clauses,
                             int until, local_scope_t *locals) {
    for (int ci = 0; ci < until; ci++) {
        zan_ast_node_t *cl = clauses->items[ci];
        if (cl->kind == AST_QUERY_LET) {
            zan_type_t *lt = infer_expr_type(g, cl->query_clause.expr, locals);
            if (!lt) lt = g->binder->type_int;
            local_add(locals, cl->query_clause.name, NULL, lt);
        }
    }
}

static int query_key_kind(zan_irgen_t *g, zan_type_t *kt, bool group) {
    if (!kt) return 0;
    switch (kt->kind) {
    case TYPE_INT: case TYPE_UINT: case TYPE_SHORT: case TYPE_USHORT:
    case TYPE_BYTE: case TYPE_SBYTE: case TYPE_CHAR: case TYPE_BOOL:
    case TYPE_NINT:
        return 0;
    case TYPE_LONG: case TYPE_ULONG:
        return group ? -1 : 1;
    case TYPE_FLOAT: case TYPE_DOUBLE:
        return group ? -1 : 2;
    case TYPE_STRING:
        return 3;
    default:
        return -1;
    }
}

static const char *query_sort_fn(int kind, int desc) {
    switch (kind) {
    case 0: return desc ? "OrderByKeysIntDescending" : "OrderByKeysInt";
    case 1: return desc ? "OrderByKeysLongDescending" : "OrderByKeysLong";
    case 2: return desc ? "OrderByKeysNumDescending" : "OrderByKeysNum";
    default: return desc ? "OrderByKeysStrDescending" : "OrderByKeysStr";
    }
}

static void query_sort_by_key(zan_irgen_t *g, zan_ast_node_t *expr,
                              query_seq_t *q, zan_ast_node_t *key_expr,
                              int desc, int let_until, zan_ast_list_t *clauses,
                              local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int imark = locals->count;
    query_scope_vars_for_infer(g, expr, q, locals);
    query_scope_lets(g, clauses, let_until, locals);
    zan_type_t *kt = infer_expr_type(g, key_expr, locals);
    locals->count = imark;
    if (!kt) kt = g->binder->type_int;
    int kind = query_key_kind(g, kt, false);
    if (kind < 0) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, key_expr->loc, 3016,
                      "orderby key must be int, long, double or string");
        kind = 0;
    }
    zan_type_t *K = kind == 0 ? g->binder->type_int
                  : kind == 1 ? g->binder->type_long
                  : kind == 2 ? g->binder->type_double
                              : g->binder->type_string;

    LLVMValueRef keys = query_new_list(g, expr, K);
    zan_istr_t keys_name = query_hold(g, locals, "k", keys,
        zan_binder_make_list_type(g->binder, K));

    query_loop_t l;
    int lmark = locals->count;
    query_loop_open(g, &l, q->seq_value);
    LLVMValueRef iv = LLVMBuildLoad2(g->builder, i64, l.idx, "qkiv");
    LLVMValueRef slot = query_loop_load(g, &l,
        q->row_mode ? q->row_type : q->elem_type, iv);
    query_register_iter(g, expr, q, slot, locals);
    for (int ci = 0; ci < let_until; ci++) {
        zan_ast_node_t *cl = clauses->items[ci];
        if (cl->kind == AST_QUERY_LET) {
            zan_type_t *lt = infer_expr_type(g, cl->query_clause.expr, locals);
            if (!lt) lt = g->binder->type_int;
            query_declare(g, locals, cl->query_clause.name,
                          emit_expr(g, cl->query_clause.expr, locals), lt);
        }
    }
    LLVMValueRef kv = emit_expr(g, key_expr, locals);
    kv = emit_boundary_coerce(g, kv, map_type(g, K));
    zan_istr_t kv_name = query_hold(g, locals, "kv", kv, K);
    query_emit_add(g, keys_name, query_ident(g, kv_name, expr->loc),
                   expr->loc, locals);
    locals->count = lmark;
    query_loop_close(g, &l);

    zan_ast_node_t *args[2] = { query_ident(g, q->seq_name, expr->loc),
                                query_ident(g, keys_name, expr->loc) };
    LLVMValueRef sorted = query_emit_enumerable(g, query_sort_fn(kind, desc),
        (int)strlen(query_sort_fn(kind, desc)), args, 2, expr->loc, locals);
    LLVMValueRef salloc = local_find(locals, q->seq_name)->alloca;
    zan_store_fit(g, sorted, salloc);
    if (q->seq_owned)
        emit_release_owned_call_temp(g, expr, q->seq_value, locals);
    emit_release_owned_call_temp(g, expr, keys, locals);
    q->seq_value = sorted;
    q->seq_owned = true;
}

static LLVMValueRef query_build_row(zan_irgen_t *g, zan_type_t *row_type,
                                    int n, LLVMValueRef *values,
                                    zan_ast_node_t *at) {
    LLVMTypeRef st = get_struct_llvm_type(g, row_type->sym);
    if (!st) st = map_type(g, row_type);
    LLVMValueRef alloca = emit_entry_alloca(g, st, "qrow");
    zan_store_fit(g, LLVMConstNull(st), alloca);
    for (int i = 0; i < n; i++) {
        LLVMValueRef fp = emit_field_ptr(g, row_type->sym, st, alloca, i,
            "qrowf");
        zan_store_fit(g, values[i], fp);
    }
    return alloca;
}

static void query_materialize_join(zan_irgen_t *g, zan_ast_node_t *expr,
                                   query_seq_t *q, zan_ast_node_t *jc,
                                   int jc_idx, zan_ast_list_t *clauses,
                                   local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int mark = locals->count;
    zan_loc_t loc = jc->loc;

    LLVMValueRef s_val = emit_expr(g, jc->query_clause.source, locals);
    zan_type_t *s_ty = infer_expr_type(g, jc->query_clause.source, locals);
    zan_type_t *y_ty = container_elem_type(s_ty);
    if (!y_ty) y_ty = g->binder->type_int;

    if (!q->row_mode) {
        zan_type_t *row1 = zan_binder_make_tuple_type(g->binder,
            &q->elem_type, 1);
        LLVMValueRef rows1 = query_new_list(g, expr, row1);
        zan_istr_t rows1_name = query_hold(g, locals, "r", rows1,
            zan_binder_make_list_type(g->binder, row1));
        query_loop_t ol;
        int olmark = locals->count;
        query_loop_open(g, &ol, q->seq_value);
        LLVMValueRef oiv = LLVMBuildLoad2(g->builder, i64, ol.idx, "qmiv");
        LLVMValueRef xslot = query_loop_load(g, &ol, q->elem_type, oiv);
        local_add(locals, expr->query.var, xslot, q->elem_type);
        LLVMValueRef xv = LLVMBuildLoad2(g->builder, map_type(g, q->elem_type),
            xslot, "qmx");
        LLVMValueRef rv = query_build_row(g, row1, 1, &xv, expr);
        zan_istr_t rv_name = query_hold(g, locals, "rv", rv, row1);
        query_emit_add(g, rows1_name, query_ident(g, rv_name, loc), loc,
                       locals);
        locals->count = olmark;
        query_loop_close(g, &ol);
        local_var_t *slv = local_find(locals, q->seq_name);
        zan_store_fit(g, rows1, slv->alloca);
        /* 查询中间隐藏序列变量类型更新 */
        slv->type = zan_binder_make_list_type(g->binder, row1);
        if (q->seq_owned)
            emit_release_owned_call_temp(g, expr, q->seq_value, locals);
        q->seq_value = rows1;
        q->seq_owned = true;
        q->row_mode = true;
        q->row_type = row1;
        q->row_fields = 1;
        q->field_names = (zan_istr_t *)zan_arena_alloc(g->arena,
            sizeof(zan_istr_t));
        q->field_names[0] = expr->query.var;
        q->field_types = (zan_type_t **)zan_arena_alloc(g->arena,
            sizeof(zan_type_t *));
        q->field_types[0] = q->elem_type;
    }

    zan_istr_t jf_name = jc->query_clause.into.len > 0
        ? jc->query_clause.into : jc->query_clause.name;
    zan_type_t *jf_ty = jc->query_clause.into.len > 0
        ? zan_binder_make_list_type(g->binder, y_ty) : y_ty;
    int nf = q->row_fields + 1;
    zan_type_t **ft = (zan_type_t **)zan_arena_alloc(g->arena,
        sizeof(zan_type_t *) * (size_t)nf);
    zan_istr_t *fn = (zan_istr_t *)zan_arena_alloc(g->arena,
        sizeof(zan_istr_t) * (size_t)nf);
    for (int i = 0; i < q->row_fields; i++) {
        fn[i] = q->field_names[i];
        ft[i] = q->field_types[i];
    }
    fn[q->row_fields] = jf_name;
    ft[q->row_fields] = jf_ty;
    zan_type_t *new_row = zan_binder_make_tuple_type(g->binder, ft, nf);
    LLVMValueRef rows2 = query_new_list(g, expr, new_row);
    zan_istr_t rows2_name = query_hold(g, locals, "r", rows2,
        zan_binder_make_list_type(g->binder, new_row));

    query_loop_t ol;
    int olmark = locals->count;
    query_loop_open(g, &ol, q->seq_value);
    LLVMValueRef oiv = LLVMBuildLoad2(g->builder, i64, ol.idx, "qmiv2");
    LLVMValueRef row_alloc = query_loop_load(g, &ol, q->row_type, oiv);
    query_register_iter(g, expr, q, row_alloc, locals);
    for (int ci = 0; ci < jc_idx; ci++) {
        zan_ast_node_t *cl = clauses->items[ci];
        if (cl->kind == AST_QUERY_LET) {
            zan_type_t *lt = infer_expr_type(g, cl->query_clause.expr, locals);
            if (!lt) lt = g->binder->type_int;
            query_declare(g, locals, cl->query_clause.name,
                          emit_expr(g, cl->query_clause.expr, locals), lt);
        }
    }

    /* 连接查询 (join)：外层循环行左键提取计算 */
    LLVMValueRef lkv = emit_expr(g, jc->query_clause.left_key, locals);
    zan_type_t *lkt = infer_expr_type(g, jc->query_clause.left_key, locals);
    if (!lkt) lkt = g->binder->type_int;
    zan_istr_t lk_name = query_hold(g, locals, "lk", lkv, lkt);

    LLVMValueRef ml_val = NULL;
    zan_istr_t ml_name = { NULL, 0 };
    LLVMValueRef ml_alloc = NULL;
    if (jc->query_clause.into.len > 0) {
        LLVMValueRef ml = query_new_list(g, expr, y_ty);
        ml_name = query_hold(g, locals, "m", ml,
            zan_binder_make_list_type(g->binder, y_ty));
        ml_alloc = local_find(locals, ml_name)->alloca;
        ml_val = ml;
        query_loop_t il;
        int imark = locals->count;
        query_loop_open(g, &il, s_val);
        LLVMValueRef iiv = LLVMBuildLoad2(g->builder, i64, il.idx, "qmiv3");
        LLVMValueRef yslot = query_loop_load(g, &il, y_ty, iiv);
        local_add(locals, jc->query_clause.name, yslot, y_ty);
        LLVMValueRef rkv = emit_expr(g, jc->query_clause.right_key, locals);
        zan_type_t *rkt = infer_expr_type(g, jc->query_clause.right_key,
                                          locals);
        if (!rkt) rkt = y_ty;
        if (query_key_kind(g, lkt, false) != query_key_kind(g, rkt, false))
            zan_diag_emit_code(g->diag, DIAG_ERROR, loc, 3017,
                          "join key types must match (int, long, double or "
                          "string)");
        zan_istr_t rk_name = query_hold(g, locals, "rk", rkv, rkt);
        LLVMValueRef eq = query_emit_eq(g, lk_name, rk_name, loc, locals);
        LLVMBasicBlockRef keep_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "qm.keep");
        LLVMBasicBlockRef skip_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "qm.skip");
        LLVMBuildCondBr(g->builder, eq, keep_bb, skip_bb);
        LLVMPositionBuilderAtEnd(g->builder, keep_bb);
        query_emit_add(g, ml_name, query_ident(g, jc->query_clause.name, loc),
                       loc, locals);
        LLVMBuildBr(g->builder, il.inc);
        LLVMPositionBuilderAtEnd(g->builder, skip_bb);
        LLVMBuildBr(g->builder, il.inc);
        locals->count = imark;
        query_loop_close(g, &il);
    } else {
        query_loop_t il;
        int imark = locals->count;
        query_loop_open(g, &il, s_val);
        LLVMValueRef iiv = LLVMBuildLoad2(g->builder, i64, il.idx, "qmiv4");
        LLVMValueRef yslot = query_loop_load(g, &il, y_ty, iiv);
        local_add(locals, jc->query_clause.name, yslot, y_ty);
        LLVMValueRef rkv = emit_expr(g, jc->query_clause.right_key, locals);
        zan_type_t *rkt = infer_expr_type(g, jc->query_clause.right_key,
                                          locals);
        if (!rkt) rkt = y_ty;
        if (query_key_kind(g, lkt, false) != query_key_kind(g, rkt, false))
            zan_diag_emit_code(g->diag, DIAG_ERROR, loc, 3017,
                          "join key types must match (int, long, double or "
                          "string)");
        zan_istr_t rk_name = query_hold(g, locals, "rk", rkv, rkt);
        LLVMValueRef eq = query_emit_eq(g, lk_name, rk_name, loc, locals);
        LLVMBasicBlockRef keep_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "qm.keep");
        LLVMBasicBlockRef skip_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "qm.skip");
        LLVMBuildCondBr(g->builder, eq, keep_bb, skip_bb);
        LLVMPositionBuilderAtEnd(g->builder, keep_bb);
        LLVMValueRef *vals = (LLVMValueRef *)zan_arena_alloc(g->arena,
            sizeof(LLVMValueRef) * (size_t)nf);
        for (int i = 0; i < q->row_fields; i++) {
            LLVMValueRef fp = emit_field_ptr(g, q->row_type->sym,
                map_type(g, q->row_type), row_alloc, i, "qmfp");
            vals[i] = LLVMBuildLoad2(g->builder, map_type(g, q->field_types[i]),
                fp, "qmfl");
        }
        LLVMValueRef yv = LLVMBuildLoad2(g->builder, map_type(g, y_ty), yslot,
            "qmy");
        vals[q->row_fields] = yv;
        LLVMValueRef rv2 = query_build_row(g, new_row, nf, vals, expr);
        zan_istr_t rv2_name = query_hold(g, locals, "rv", rv2, new_row);
        query_emit_add(g, rows2_name, query_ident(g, rv2_name, loc), loc,
                       locals);
        LLVMBuildBr(g->builder, il.inc);
        LLVMPositionBuilderAtEnd(g->builder, skip_bb);
        LLVMBuildBr(g->builder, il.inc);
        locals->count = imark;
        query_loop_close(g, &il);
    }

    if (jc->query_clause.into.len > 0) {
        LLVMValueRef *vals = (LLVMValueRef *)zan_arena_alloc(g->arena,
            sizeof(LLVMValueRef) * (size_t)nf);
        for (int i = 0; i < q->row_fields; i++) {
            LLVMValueRef fp = emit_field_ptr(g, q->row_type->sym,
                map_type(g, q->row_type), row_alloc, i, "qmfp");
            vals[i] = LLVMBuildLoad2(g->builder, map_type(g, q->field_types[i]),
                fp, "qmfl");
        }
        LLVMValueRef mlv = LLVMBuildLoad2(g->builder,
            LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0), ml_alloc,
            "qmml");
        vals[q->row_fields] = mlv;
        LLVMValueRef rv2 = query_build_row(g, new_row, nf, vals, expr);
        zan_istr_t rv2_name = query_hold(g, locals, "rv", rv2, new_row);
        query_emit_add(g, rows2_name, query_ident(g, rv2_name, loc), loc,
                       locals);
        emit_release_owned_call_temp(g, expr, ml_val, locals);
    }

    locals->count = olmark;
    query_loop_close(g, &ol);
    emit_release_owned_call_temp(g, jc->query_clause.source, s_val, locals);

    LLVMValueRef salloc = local_find(locals, q->seq_name)->alloca;
    zan_store_fit(g, rows2, salloc);
    /* 序列局部变量类型同步更新 */
    local_find(locals, q->seq_name)->type =
        zan_binder_make_list_type(g->binder, new_row);
    if (q->seq_owned)
        emit_release_owned_call_temp(g, expr, q->seq_value, locals);
    q->seq_value = rows2;
    q->seq_owned = true;
    q->row_type = new_row;
    q->row_fields = nf;
    q->field_names = fn;
    q->field_types = ft;
    locals->count = mark;
    (void)lkt;
}

static LLVMValueRef query_do_group(zan_irgen_t *g, zan_ast_node_t *expr,
                                   query_seq_t *q, zan_ast_list_t *clauses,
                                   local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int imark = locals->count;
    query_scope_vars_for_infer(g, expr, q, locals);
    query_scope_lets(g, clauses, clauses->count, locals);
    zan_type_t *kt = infer_expr_type(g, expr->query.group_key, locals);
    zan_type_t *ge = infer_expr_type(g, expr->query.group_expr, locals);
    locals->count = imark;
    if (!kt) kt = g->binder->type_int;
    if (!ge) ge = q->elem_type;
    int kind = query_key_kind(g, kt, true);
    if (kind < 0) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->query.group_key->loc, 3018,
                      "group key must be int or string");
        kind = 0;
    }
    zan_type_t *K = kind == 0 ? g->binder->type_int : g->binder->type_string;

    LLVMValueRef keys = query_new_list(g, expr, K);
    zan_istr_t keys_name = query_hold(g, locals, "gk", keys,
        zan_binder_make_list_type(g->binder, K));
    LLVMValueRef items = query_new_list(g, expr, ge);
    zan_istr_t items_name = query_hold(g, locals, "gi", items,
        zan_binder_make_list_type(g->binder, ge));

    query_loop_t l;
    int lmark = locals->count;
    query_loop_open(g, &l, q->seq_value);
    LLVMValueRef iv = LLVMBuildLoad2(g->builder, i64, l.idx, "qgiv");
    LLVMValueRef slot = query_loop_load(g, &l,
        q->row_mode ? q->row_type : q->elem_type, iv);
    query_register_iter(g, expr, q, slot, locals);
    for (int ci = 0; ci < clauses->count; ci++) {
        zan_ast_node_t *cl = clauses->items[ci];
        if (cl->kind == AST_QUERY_LET) {
            zan_type_t *lt = infer_expr_type(g, cl->query_clause.expr, locals);
            if (!lt) lt = g->binder->type_int;
            query_declare(g, locals, cl->query_clause.name,
                          emit_expr(g, cl->query_clause.expr, locals), lt);
        }
    }
    LLVMValueRef kkv = emit_expr(g, expr->query.group_key, locals);
    kkv = emit_boundary_coerce(g, kkv, map_type(g, K));
    zan_istr_t kkv_name = query_hold(g, locals, "gkv", kkv, K);
    query_emit_add(g, keys_name, query_ident(g, kkv_name, expr->loc),
                   expr->loc, locals);
    LLVMValueRef eev = emit_expr(g, expr->query.group_expr, locals);
    zan_istr_t eev_name = query_hold(g, locals, "gev", eev, ge);
    query_emit_add(g, items_name, query_ident(g, eev_name, expr->loc),
                   expr->loc, locals);
    locals->count = lmark;
    query_loop_close(g, &l);

    const char *fn = kind == 0 ? "GroupByKeysInt" : "GroupByKeysStr";
    zan_ast_node_t *args[3] = { query_ident(g, q->seq_name, expr->loc),
                                query_ident(g, keys_name, expr->loc),
                                query_ident(g, items_name, expr->loc) };
    LLVMValueRef groups = query_emit_enumerable(g, fn, (int)strlen(fn),
        args, 3, expr->loc, locals);
    emit_release_owned_call_temp(g, expr, keys, locals);
    emit_release_owned_call_temp(g, expr, items, locals);
    return groups;
}

/* LINQ 最终迭代阶段：按源码顺序处理 where/let，嵌套循环处理延迟查询 */
static void query_final_pass(zan_irgen_t *g, zan_ast_node_t *expr,
                             zan_ast_list_t *clauses, int *join_in_rows,
                             int from_idx, zan_istr_t result_name,
                             zan_ast_node_t *select,
                             LLVMBasicBlockRef *skip_stack, int *skip_depth,
                             local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    for (int ci = from_idx; ci < clauses->count; ci++) {
        zan_ast_node_t *cl = clauses->items[ci];
        if (cl->kind == AST_QUERY_WHERE) {
            LLVMValueRef c = emit_expr(g, cl->query_clause.expr, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(c)) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(LLVMTypeOf(c)) != 1)
                c = zan_icmp(g->builder, LLVMIntNE, c,
                             LLVMConstNull(LLVMTypeOf(c)), "qw");
            LLVMBasicBlockRef pass_bb = LLVMAppendBasicBlockInContext(g->ctx,
                LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)),
                "q.pass");
            {
                int rd = *skip_depth - 1;
                if (rd >= 16) rd = 15;
                LLVMBuildCondBr(g->builder, c, pass_bb, skip_stack[rd]);
            }
            LLVMPositionBuilderAtEnd(g->builder, pass_bb);
        } else if (cl->kind == AST_QUERY_LET) {
            zan_type_t *lt = infer_expr_type(g, cl->query_clause.expr, locals);
            if (!lt) lt = g->binder->type_int;
            query_declare(g, locals, cl->query_clause.name,
                          emit_expr(g, cl->query_clause.expr, locals), lt);
        } else if (cl->kind == AST_QUERY_ORDERBY) {
            continue;
        } else if (join_in_rows[ci]) {
            continue;
        } else {
            LLVMValueRef s_val = emit_expr(g, cl->query_clause.source, locals);
            zan_type_t *s_ty = infer_expr_type(g, cl->query_clause.source,
                                               locals);
            zan_type_t *y_ty = container_elem_type(s_ty);
            if (!y_ty) y_ty = g->binder->type_int;
            LLVMValueRef lkv = emit_expr(g, cl->query_clause.left_key, locals);
            zan_type_t *lkt = infer_expr_type(g, cl->query_clause.left_key,
                                              locals);
            if (!lkt) lkt = g->binder->type_int;
            zan_istr_t lk_name = query_hold(g, locals, "lk", lkv, lkt);
            /* LINQ into 语法处理 */
            if (cl->query_clause.into.len > 0) {
                LLVMValueRef ml = query_new_list(g, expr, y_ty);
                zan_istr_t ml_name = query_hold(g, locals, "m", ml,
                    zan_binder_make_list_type(g->binder, y_ty));
                LLVMValueRef ml_alloc = local_find(locals, ml_name)->alloca;
                int imark = locals->count;
                query_loop_t il2;
                query_loop_open(g, &il2, s_val);
                LLVMValueRef iiv2 = LLVMBuildLoad2(g->builder, i64, il2.idx,
                    "qjiv2");
                LLVMValueRef yslot2 = query_loop_load(g, &il2, y_ty, iiv2);
                local_add(locals, cl->query_clause.name, yslot2, y_ty);
                LLVMValueRef rkv2 = emit_expr(g, cl->query_clause.right_key,
                                              locals);
                zan_type_t *rkt2 = infer_expr_type(g,
                    cl->query_clause.right_key, locals);
                if (!rkt2) rkt2 = y_ty;
                zan_istr_t rk2_name = query_hold(g, locals, "rk", rkv2, rkt2);
                LLVMValueRef eq2 = query_emit_eq(g, lk_name, rk2_name,
                    cl->loc, locals);
                LLVMBasicBlockRef k2 = LLVMAppendBasicBlockInContext(g->ctx,
                    LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)),
                    "qj.k2");
                LLVMBasicBlockRef s2 = LLVMAppendBasicBlockInContext(g->ctx,
                    LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)),
                    "qj.s2");
                LLVMBuildCondBr(g->builder, eq2, k2, s2);
                LLVMPositionBuilderAtEnd(g->builder, k2);
                query_emit_add(g, ml_name,
                    query_ident(g, cl->query_clause.name, cl->loc), cl->loc,
                    locals);
                LLVMBuildBr(g->builder, il2.inc);
                LLVMPositionBuilderAtEnd(g->builder, s2);
                LLVMBuildBr(g->builder, il2.inc);
                locals->count = imark;
                query_loop_close(g, &il2);
                emit_release_owned_call_temp(g, cl->query_clause.source,
                                             s_val, locals);
                LLVMValueRef mlv = LLVMBuildLoad2(g->builder,
                    LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                    ml_alloc, "qgv");
                query_declare(g, locals, cl->query_clause.into, mlv,
                    zan_binder_make_list_type(g->binder, y_ty));
                query_final_pass(g, expr, clauses, join_in_rows, ci + 1,
                                 result_name, select, skip_stack, skip_depth,
                                 locals);
                emit_release_owned_call_temp(g, expr, ml, locals);
                return;
            }
            query_loop_t il;
            int lmark = locals->count;
            query_loop_open(g, &il, s_val);
            LLVMValueRef iiv = LLVMBuildLoad2(g->builder, i64, il.idx, "qjiv");
            LLVMValueRef yslot = query_loop_load(g, &il, y_ty, iiv);
            local_add(locals, cl->query_clause.name, yslot, y_ty);
            LLVMValueRef rkv = emit_expr(g, cl->query_clause.right_key,
                                         locals);
            zan_type_t *rkt = infer_expr_type(g, cl->query_clause.right_key,
                                              locals);
            if (!rkt) rkt = y_ty;
            if (query_key_kind(g, lkt, false) != query_key_kind(g, rkt, false))
                zan_diag_emit_code(g->diag, DIAG_ERROR, cl->loc, 3017,
                              "join key types must match (int, long, double "
                              "or string)");
            zan_istr_t rk_name = query_hold(g, locals, "rk", rkv, rkt);
            LLVMValueRef eq = query_emit_eq(g, lk_name, rk_name, cl->loc,
                                            locals);
            LLVMBasicBlockRef keep_bb = LLVMAppendBasicBlockInContext(g->ctx,
                LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)),
                "qj.keep");
            LLVMBasicBlockRef skip_bb = LLVMAppendBasicBlockInContext(g->ctx,
                LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)),
                "qj.skip");
            LLVMBuildCondBr(g->builder, eq, keep_bb, skip_bb);
            LLVMPositionBuilderAtEnd(g->builder, keep_bb);
            *skip_depth = *skip_depth + 1;
            {
                int sd = *skip_depth - 1;
                if (sd >= 16) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, cl->loc, 3019,
                                  "query expression has too many join "
                                  "clauses (max 15)");
                    sd = 15;
                }
                skip_stack[sd] = il.inc;
            }
            query_final_pass(g, expr, clauses, join_in_rows, ci + 1,
                             result_name, select, skip_stack, skip_depth,
                             locals);
            *skip_depth = *skip_depth - 1;
            LLVMBuildBr(g->builder, il.inc);
            LLVMPositionBuilderAtEnd(g->builder, skip_bb);
            LLVMBuildBr(g->builder, il.inc);
            locals->count = lmark;
            query_loop_close(g, &il);
            emit_release_owned_call_temp(g, cl->query_clause.source, s_val,
                                         locals);
            return;
        }
    }
    query_emit_add(g, result_name, select, expr->loc, locals);
}

static void query_group_into_pass(zan_irgen_t *g, zan_ast_node_t *expr,
                                  LLVMValueRef groups, zan_type_t *grp_ty,
                                  zan_istr_t gvar, zan_ast_node_t *select,
                                  zan_istr_t result_name,
                                  local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    query_loop_t l;
    int lmark = locals->count;
    query_loop_open(g, &l, groups);
    LLVMValueRef iv = LLVMBuildLoad2(g->builder, i64, l.idx, "qgiv2");
    LLVMValueRef gslot = query_loop_load(g, &l, grp_ty, iv);
    local_add(locals, gvar, gslot, grp_ty);
    query_emit_add(g, result_name, select, expr->loc, locals);
    locals->count = lmark;
    query_loop_close(g, &l);
}

static LLVMValueRef emit_expr_query_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    int mark = locals->count;

    LLVMValueRef collection = emit_expr(g, expr->query.source, locals);
    zan_type_t *src_ty = infer_expr_type(g, expr->query.source, locals);
    zan_type_t *elem = container_elem_type(src_ty);
    if (!elem) elem = g->binder->type_int;

    query_seq_t q;
    memset(&q, 0, sizeof q);
    q.seq_value = collection;
    q.elem_type = elem;
    q.seq_name = query_hold(g, locals, "s", collection,
        zan_binder_make_list_type(g->binder, elem));

    zan_ast_list_t *clauses = &expr->query.clauses;
    int ncl = clauses->count;
    int *join_in_rows = ncl > 0
        ? (int *)zan_arena_alloc(g->arena, sizeof(int) * (size_t)ncl) : NULL;
    if (join_in_rows) memset(join_in_rows, 0, sizeof(int) * (size_t)ncl);

    int smark = locals->count;
    local_add(locals, expr->query.var, NULL, elem);
    for (int ci = 0; ci < ncl; ci++) {
        zan_ast_node_t *cl = clauses->items[ci];
        if (cl->kind == AST_QUERY_LET) {
            zan_type_t *lt = infer_expr_type(g, cl->query_clause.expr, locals);
            if (!lt) lt = g->binder->type_int;
            local_add(locals, cl->query_clause.name, NULL, lt);
        } else if (cl->kind == AST_QUERY_JOIN) {
            zan_type_t *jty = infer_expr_type(g, cl->query_clause.source,
                                              locals);
            zan_type_t *je = container_elem_type(jty);
            if (!je) je = g->binder->type_int;
            if (cl->query_clause.into.len > 0)
                local_add(locals, cl->query_clause.into, NULL,
                          zan_binder_make_list_type(g->binder, je));
            else
                local_add(locals, cl->query_clause.name, NULL, je);
        }
    }
    zan_type_t *sel = NULL;
    zan_type_t *grp_ty = NULL;
    if (expr->query.group_expr) {
        zan_type_t *ge = infer_expr_type(g, expr->query.group_expr, locals);
        if (!ge) ge = elem;
        grp_ty = zan_binder_make_grouping_type(g->binder, ge);
        if (expr->query.group_into.len > 0) {
            local_add(locals, expr->query.group_into, NULL, grp_ty);
            sel = infer_expr_type(g, expr->query.select, locals);
            if (!sel) sel = ge;
        } else {
            sel = grp_ty;
        }
    } else {
        sel = infer_expr_type(g, expr->query.select, locals);
        if (!sel) sel = elem;
    }
    locals->count = smark;

    LLVMValueRef result = query_new_list(g, expr, sel);
    zan_istr_t result_name = query_hold(g, locals, "q", result,
        zan_binder_make_list_type(g->binder, sel));

    int first_pending_join = -1;
    for (int ci = 0; ci < ncl; ci++) {
        zan_ast_node_t *cl = clauses->items[ci];
        if (cl->kind == AST_QUERY_JOIN) {
            if (first_pending_join < 0) first_pending_join = ci;
            continue;
        }
        if (cl->kind != AST_QUERY_ORDERBY) continue;
        if (first_pending_join >= 0) {
            for (int ji = first_pending_join; ji < ci; ji++) {
                zan_ast_node_t *jc = clauses->items[ji];
                if (jc->kind == AST_QUERY_JOIN) {
                    query_materialize_join(g, expr, &q, jc, ji, clauses,
                                           locals);
                    join_in_rows[ji] = 1;
                }
            }
            first_pending_join = -1;
        }
        int run_end = ci;
        while (run_end < ncl &&
               clauses->items[run_end]->kind == AST_QUERY_ORDERBY)
            run_end++;
        for (int ki = run_end - 1; ki >= ci; ki--) {
            zan_ast_node_t *ob = clauses->items[ki];
            query_sort_by_key(g, expr, &q, ob->query_clause.expr,
                              ob->query_clause.descending, ci, clauses,
                              locals);
        }
        ci = run_end - 1;
    }

    if (expr->query.group_expr) {
        if (first_pending_join >= 0) {
            for (int ji = first_pending_join; ji < ncl; ji++) {
                zan_ast_node_t *jc = clauses->items[ji];
                if (jc->kind == AST_QUERY_JOIN) {
                    query_materialize_join(g, expr, &q, jc, ji, clauses,
                                           locals);
                    join_in_rows[ji] = 1;
                }
            }
        }
        LLVMValueRef groups = query_do_group(g, expr, &q, clauses, locals);
        if (expr->query.group_into.len == 0) {
            emit_release_owned_call_temp(g, expr, result, locals);
            if (q.seq_value == collection)
                emit_release_owned_call_temp(g, expr->query.source,
                                             collection, locals);
            else
                emit_release_owned_call_temp(g, expr, q.seq_value, locals);
            locals->count = mark;
            return groups;
        }
        /* `group e by k into g select p` */
        query_group_into_pass(g, expr, groups, grp_ty,
                              expr->query.group_into, expr->query.select,
                              result_name, locals);
        emit_release_owned_call_temp(g, expr, groups, locals);
        if (q.seq_value == collection)
            emit_release_owned_call_temp(g, expr->query.source, collection,
                                         locals);
        else
            emit_release_owned_call_temp(g, expr, q.seq_value, locals);
        locals->count = mark;
        return result;
    }

    query_loop_t ol;
    int lmark = locals->count;
    query_loop_open(g, &ol, q.seq_value);
    LLVMValueRef oiv = LLVMBuildLoad2(g->builder, i64, ol.idx, "qoiv");
    LLVMValueRef islot = query_loop_load(g, &ol,
        q.row_mode ? q.row_type : q.elem_type, oiv);
    query_register_iter(g, expr, &q, islot, locals);
    LLVMBasicBlockRef skip_stack[16];
    int skip_depth = 0;
    skip_stack[skip_depth++] = ol.inc;
    query_final_pass(g, expr, clauses, join_in_rows, 0, result_name,
                     expr->query.select, skip_stack, &skip_depth, locals);
    locals->count = lmark;
    query_loop_close(g, &ol);

    if (q.seq_value == collection)
        emit_release_owned_call_temp(g, expr->query.source, collection,
                                     locals);
    else
        emit_release_owned_call_temp(g, expr, q.seq_value, locals);
    locals->count = mark;
    (void)fn;
    return result;
}

/* 元组字面量 (a, b, ...)：分配并填充合成的匿名元组结构体值 */
static LLVMValueRef emit_expr_tuple(zan_irgen_t *g, zan_ast_node_t *expr,
                                    local_scope_t *locals) {
    zan_type_t *ttype = infer_expr_type(g, expr, locals);
    if (!ttype || ttype->kind != TYPE_STRUCT || !ttype->sym) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3020,
                      "cannot form a tuple here");
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
    }
    LLVMTypeRef st = get_struct_llvm_type(g, ttype->sym);
    if (!st) st = map_type(g, ttype);
    LLVMValueRef alloca = emit_entry_alloca(g, st, "tup");
    zan_store_fit(g, LLVMConstNull(st), alloca);
    int n = expr->tuple_expr.items.count;
    for (int i = 0; i < n; i++) {
        zan_ast_node_t *item = expr->tuple_expr.items.items[i];
        char fname[16];
        snprintf(fname, sizeof fname, "Item%d", i + 1);
        zan_istr_t f_istr = { fname, (uint32_t)strlen(fname) };
        zan_symbol_t *fsym = ttype->sym ? get_field_sym(ttype->sym, f_istr) : NULL;
        if (!fsym) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, item->loc, 3021,
                          "tuple element %d has no field in its struct type",
                          i + 1);
            continue;
        }
        int fi = get_field_index(ttype->sym, f_istr);
        if (fi < 0) fi = i;
        LLVMValueRef fptr = emit_field_ptr(g, ttype->sym, st, alloca, fi, "tf");
        LLVMValueRef fval = emit_arg_typed(g, item, fsym->type, locals);
        /* 规范化结构体所有权契约，维护内部字段引用计数 */
        if (fsym->type && is_rc_managed_type(fsym->type) &&
            LLVMGetTypeKind(LLVMTypeOf(fval)) == LLVMPointerTypeKind &&
            !expr_yields_owned_rc_value(g, item, locals)) {
            emit_rc_retain_for_type(g, fsym->type, fval);
        } else if (fsym->type && fsym->type->kind == TYPE_STRUCT &&
                   type_contains_collection_rc(g, fsym->type, 0) &&
                   !expr_yields_owned_rc_value(g, item, locals)) {
            emit_collection_value_retain(g, fsym->type, fval, 0);
        }
        zan_store_fit(g, fval, fptr);
    }
    return alloca;
}

static zan_ast_node_t *owned_rhs_marker(zan_irgen_t *g, zan_loc_t loc);

static LLVMValueRef emit_expr_new_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* 工厂调用接对象初始化式 */
        if (!expr->new_expr.type && expr->new_expr.call_init) {
            LLVMValueRef obj = emit_expr(g, expr->new_expr.call_init, locals);
            zan_type_t *otype = infer_expr_type(g, expr->new_expr.call_init,
                                                locals);
            if (!otype || !otype->sym ||
                (otype->sym->kind != SYM_CLASS &&
                 otype->sym->kind != SYM_STRUCT)) {
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3022,
                    "object initializer can only continue a call that "
                    "returns a class instance");
                return obj;
            }
            zan_symbol_t *sym = otype->sym;
            LLVMTypeRef st = get_struct_llvm_type(g, sym);
            if (!st) return obj;
            for (int i = 0; i < expr->new_expr.arg_inits.count; i++) {
                zan_ast_node_t *arg = expr->new_expr.arg_inits.items[i];
                if (arg->kind != AST_COLL_INIT && arg->kind != AST_ASSIGNMENT)
                    continue;
                zan_istr_t mname = (arg->kind == AST_COLL_INIT)
                    ? arg->coll_init.name
                    : arg->binary.left->ident.name;
                zan_symbol_t *msym = get_field_sym(sym, mname);
                if (!msym) continue;
                if (arg->kind == AST_COLL_INIT) {
                    int getter_owned = 0;
                    zan_ast_node_t *recv = zan_ast_new(g->arena,
                        AST_IDENTIFIER, arg->loc);
                    recv->ident.name = mname;
                    LLVMTypeRef coll_lt = map_type(g, msym->type);
                    LLVMValueRef cslot = emit_entry_alloca(g, coll_lt,
                                                           "cinit.slot");
                    zan_store_fit(g, LLVMConstNull(coll_lt), cslot);
                    zan_symbol_t *getter = property_getter_sym(g, msym);
                    if (getter) {
                        LLVMValueRef v = emit_property_getter_call(g, getter,
                            otype, obj, arg, locals);
                        zan_store_fit(g, v, cslot);
                        getter_owned = 1;
                    } else {
                        int cfi = get_field_index(sym, mname);
                        if (cfi < 0) continue;
                        LLVMValueRef cptr = emit_field_ptr(g, sym, st, obj,
                                                           cfi, "cinit.ptr");
                        zan_store_fit(g, LLVMBuildLoad2(g->builder, coll_lt,
                            cptr, "cinit.load"), cslot);
                    }
                    local_add(locals, mname, cslot, msym->type);
                    for (int k = 0; k < arg->coll_init.items.count; k++) {
                        zan_ast_node_t *item = arg->coll_init.items.items[k];
                        zan_ast_node_t *madd = zan_ast_new(g->arena,
                            AST_MEMBER_ACCESS, item->loc);
                        madd->member.object = recv;
                        madd->member.name = (zan_istr_t){"Add", 3};
                        madd->member.null_cond = 0;
                        zan_ast_node_t *addcall = zan_ast_new(g->arena,
                            AST_CALL, item->loc);
                        addcall->call.callee = madd;
                        zan_ast_list_init(&addcall->call.args);
                        zan_ast_list_init(&addcall->call.type_args);
                        zan_ast_list_push(&addcall->call.args, item, g->arena);
                        if (msym->type && type_named(msym->type, "Dict", 4) &&
                            k + 1 < arg->coll_init.items.count) {
                            zan_ast_list_push(&addcall->call.args,
                                arg->coll_init.items.items[++k], g->arena);
                        }
                        /* 支持流式 API 的 Add 方法处理 */
                        LLVMValueRef addres = emit_expr(g, addcall, locals);
                        if (addres && expr_yields_owned_rc_value(g, addcall,
                                                      locals)) {
                            zan_type_t *rt = infer_expr_type(g, addcall,
                                                             locals);
                            if (rt && is_rc_managed_type(rt) &&
                                LLVMGetTypeKind(LLVMTypeOf(addres))
                                    == LLVMPointerTypeKind) {
                                emit_rc_release_for_type(g, rt, addres);
                            }
                        }
                    }
                    for (int lv = locals->count - 1; lv >= 0; lv--) {
                        if (locals->vars[lv].name.len == mname.len &&
                            memcmp(locals->vars[lv].name.str, mname.str,
                                   (size_t)mname.len) == 0) {
                            for (int sh = lv; sh < locals->count - 1; sh++)
                                locals->vars[sh] = locals->vars[sh + 1];
                            locals->count--;
                            break;
                        }
                    }
                    if (getter_owned) {
                        LLVMValueRef cv = LLVMBuildLoad2(g->builder, coll_lt,
                            cslot, "cinit.done");
                        emit_rc_release_for_type(g, msym->type, cv);
                    }
                    continue;
                }
                /* 普通字段/属性写入：通过合成访问器 */
                zan_ast_node_t *mref = zan_ast_new(g->arena,
                    AST_MEMBER_ACCESS, arg->loc);
                mref->member.object = expr->new_expr.call_init;
                mref->member.name = mname;
                mref->member.null_cond = 0;
                zan_ast_node_t *setn = zan_ast_new(g->arena,
                    AST_ASSIGNMENT, arg->loc);
                setn->binary.op = TK_EQ;
                setn->binary.left = mref;
                setn->binary.right = arg->binary.right;
                emit_expr(g, setn, locals);
            }
            return obj;
        }
        /* 匿名对象字面量 new { ... } 校验与降解 */
        if (!expr->new_expr.type) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3023,
                "anonymous object `new { ... }` is only valid inside a typed "
                "ORM query (GroupBy/ToList/ToAggregate)");
            return LLVMConstNull(LLVMInt8TypeInContext(g->ctx));
        }
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF &&
            !expr->new_expr.is_array) {
            zan_istr_t spn = expr->new_expr.type->type_ref.name;
            if (spn.len == 4 && memcmp(spn.str, "Span", 4) == 0 &&
                expr->new_expr.args.count == 2) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef base = emit_expr(g, expr->new_expr.args.items[0], locals);
                if (LLVMGetTypeKind(LLVMTypeOf(base)) == LLVMIntegerTypeKind)
                    base = LLVMBuildIntToPtr(g->builder, base, i8ptr, "span.b2p");
                else
                    base = LLVMBuildBitCast(g->builder, base, i8ptr, "span.bbc");
                LLVMValueRef len = coerce_int_to(g,
                    emit_expr(g, expr->new_expr.args.items[1], locals), i64t);
                LLVMValueRef negative_len = zan_icmp(g->builder, LLVMIntSLT, len,
                    LLVMConstInt(i64t, 0, 0), "span.len.neg");
                emit_runtime_check(g, negative_len, expr->new_expr.args.items[1]->loc,
                                   "negative span length");
                LLVMValueRef v = LLVMGetUndef(g->span_struct_type);
                v = LLVMBuildInsertValue(g->builder, v, base, 0, "span.n0");
                v = LLVMBuildInsertValue(g->builder, v, len, 1, "span.n1");
                return v;
            }
        }
        /* new List<T>() 内建动态列表实例分配 */
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF &&
            !expr->new_expr.is_array) {
            zan_istr_t tname = expr->new_expr.type->type_ref.name;
            if (tname.len == 4 && memcmp(tname.str, "List", 4) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                zan_type_t *lelem = NULL;
                {
                    zan_type_t *lt = resolve_type_ctx(g, expr->new_expr.type);
                    if (lt) lelem = container_elem_type(lt);
                }
                LLVMValueRef list_ptr = emit_alloc_rc_collection(g, expr, 24, 1, lelem);
                /* cast to List* */
                LLVMValueRef typed_ptr = LLVMBuildBitCast(g->builder, list_ptr,
                    LLVMPointerType(g->list_struct_type, 0), "lptr");
                /* new List<T>(src) 拷贝构造函数降解 */
                if (expr->new_expr.list_copy && expr->new_expr.args.count == 1) {
                    LLVMValueRef src_raw =
                        emit_expr(g, expr->new_expr.args.items[0], locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(src_raw)) != LLVMPointerTypeKind) {
                        src_raw = LLVMBuildIntToPtr(g->builder, src_raw, i8ptr,
                                                    "cpy.src.ip");
                    }
                    LLVMValueRef src_ptr = LLVMBuildBitCast(g->builder, src_raw,
                        LLVMPointerType(g->list_struct_type, 0), "cpy.src");
                    LLVMValueRef src_cnt = LLVMBuildLoad2(g->builder, i64,
                        LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                            src_ptr, 0, "src.cnt"), "src.n");
                    LLVMValueRef src_data = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(i64, 0),
                        LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                            src_ptr, 2, "src.df"), "src.d");

                    LLVMValueRef eight = LLVMConstInt(i64, 8, 0);
                    LLVMValueRef cap = LLVMBuildSelect(g->builder,
                        zan_icmp(g->builder, LLVMIntSGT, src_cnt, eight, "cpy.gt8"),
                        src_cnt, eight, "cpy.cap");
                    zan_store_fit(g, src_cnt, LLVMBuildStructGEP2(g->builder,
                        g->list_struct_type, typed_ptr, 0, "cnt"));
                    zan_store_fit(g, cap, LLVMBuildStructGEP2(g->builder,
                        g->list_struct_type, typed_ptr, 1, "cap"));
                    unsigned lwords = elem_slot_words(g, lelem);
                    LLVMValueRef bytes = LLVMBuildMul(g->builder, cap,
                        LLVMConstInt(i64, (unsigned long long)(8 * lwords), 0),
                        "cpy.bytes");
                    LLVMValueRef data_ptr = zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                        get_calloc_fn(g),
                        (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), bytes }, 2,
                        "cpy.data");
                    zan_irgen_emit_oom_check(g, g->current_fn, data_ptr);
                    LLVMValueRef dst_data = LLVMBuildBitCast(g->builder, data_ptr,
                        LLVMPointerType(i64, 0), "cpy.dp");
                    zan_store_fit(g, dst_data, LLVMBuildStructGEP2(g->builder,
                        g->list_struct_type, typed_ptr, 2, "df"));

                    LLVMValueRef cfn = LLVMGetBasicBlockParent(
                        LLVMGetInsertBlock(g->builder));
                    LLVMBasicBlockRef saved_bb =
                        LLVMGetInsertBlock(g->builder);
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, cfn, "cpy.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, cfn, "cpy.body");
                    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, cfn, "cpy.end");
                    LLVMValueRef ip = LLVMBuildAlloca(g->builder, i64, "cpy.i");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), ip);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef iv = LLVMBuildLoad2(g->builder, i64, ip, "cpy.iv");
                    LLVMBuildCondBr(g->builder,
                        zan_icmp(g->builder, LLVMIntSLT, iv, src_cnt, "cpy.more"),
                        body_bb, end_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    iv = LLVMBuildLoad2(g->builder, i64, ip, "cpy.iv2");
                    LLVMValueRef off = LLVMBuildMul(g->builder, iv,
                        LLVMConstInt(i64, lwords, 0), "cpy.off");
                    LLVMValueRef s_slot = LLVMBuildGEP2(g->builder, i64,
                        src_data, &off, 1, "cpy.ss");
                    LLVMValueRef d_slot = LLVMBuildGEP2(g->builder, i64,
                        dst_data, &off, 1, "cpy.ds");
                    LLVMTypeRef elem_llvm = lelem ? map_type(g, lelem) : i64;
                    LLVMValueRef v;
                    if (LLVMGetTypeKind(elem_llvm) == LLVMStructTypeKind) {
                        v = load_struct_from_slot(g, s_slot, elem_llvm);
                    } else {
                        v = LLVMBuildLoad2(g->builder, i64, s_slot, "cpy.sv");
                        if (LLVMGetTypeKind(elem_llvm) == LLVMPointerTypeKind) {
                            v = LLVMBuildIntToPtr(g->builder, v, elem_llvm, "cpy.pv");
                        } else if (LLVMGetTypeKind(elem_llvm) == LLVMDoubleTypeKind) {
                            v = LLVMBuildBitCast(g->builder, v, elem_llvm, "cpy.dv");
                        }
                    }
                    emit_collection_slot_store(g, lelem, i64, d_slot, v,
                                               NULL, locals, 0);
                    LLVMValueRef nxt = LLVMBuildAdd(g->builder, iv,
                        LLVMConstInt(i64, 1, 0), "cpy.nx");
                    LLVMBuildStore(g->builder, nxt, ip);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, end_bb);
                    return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr, "listv");
                }
                /* new List<T>(array) 数组拷贝构造：数组不是 List，落进下面
                 * 的初始化式路径会把整个数组对象当成 1 个元素 Add 进去
                 * （count=1、读元素得到假扮字符串的数组头、下标 1 越界，
                 * 全程静默——Process 参数列表全是垃圾，实测踩过）。这里
                 * 按数组布局（长度在 ptr-16，元素从 ptr+0 紧凑排布）逐
                 * 元素拷进 List 的字槽。 */
                if (!expr->new_expr.list_copy && expr->new_expr.args.count == 1
                        && expr->new_expr.arg_inits.count == 0) {
                    zan_ast_node_t *asrc = expr->new_expr.args.items[0];
                    zan_type_t *at = asrc
                        ? infer_expr_type(g, asrc, locals) : NULL;
                    if (at && at->kind == TYPE_ARRAY && at->array_rank == 1
                            && at->element_type) {
                        LLVMValueRef arr = emit_expr(g, asrc, locals);
                        LLVMTypeRef aetl = map_type(g, at->element_type);
                        LLVMTypeKind aek = LLVMGetTypeKind(aetl);
                        unsigned alwords = elem_slot_words(g, lelem);
                        LLVMValueRef alen = zan_array_len(g, arr);
                        LLVMValueRef aeight = LLVMConstInt(i64, 8, 0);
                        LLVMValueRef acap = LLVMBuildSelect(g->builder,
                            zan_icmp(g->builder, LLVMIntSGT, alen, aeight,
                                     "acpy.gt8"),
                            alen, aeight, "acpy.cap");
                        zan_store_fit(g, alen, LLVMBuildStructGEP2(g->builder,
                            g->list_struct_type, typed_ptr, 0, "acnt"));
                        zan_store_fit(g, acap, LLVMBuildStructGEP2(g->builder,
                            g->list_struct_type, typed_ptr, 1, "acap"));
                        LLVMValueRef abytes = LLVMBuildMul(g->builder, acap,
                            LLVMConstInt(i64,
                                (unsigned long long)(8 * alwords), 0),
                            "acpy.bytes");
                        LLVMValueRef adata = zan_call2(g->builder,
                            LLVMFunctionType(i8ptr,
                                (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                            get_calloc_fn(g),
                            (LLVMValueRef[]){
                                LLVMConstInt(i64, 1, 0), abytes }, 2,
                            "acpy.data");
                        zan_irgen_emit_oom_check(g, g->current_fn, adata);
                        LLVMValueRef adp = LLVMBuildBitCast(g->builder, adata,
                            LLVMPointerType(i64, 0), "acpy.dp");
                        zan_store_fit(g, adp, LLVMBuildStructGEP2(g->builder,
                            g->list_struct_type, typed_ptr, 2, "adf"));
                        LLVMValueRef acfn = LLVMGetBasicBlockParent(
                            LLVMGetInsertBlock(g->builder));
                        LLVMBasicBlockRef acond = LLVMAppendBasicBlockInContext(
                            g->ctx, acfn, "acpy.cond");
                        LLVMBasicBlockRef abody = LLVMAppendBasicBlockInContext(
                            g->ctx, acfn, "acpy.body");
                        LLVMBasicBlockRef aend = LLVMAppendBasicBlockInContext(
                            g->ctx, acfn, "acpy.end");
                        LLVMValueRef aip = LLVMBuildAlloca(g->builder, i64,
                                                           "acpy.i");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0),
                                       aip);
                        LLVMBuildBr(g->builder, acond);
                        LLVMPositionBuilderAtEnd(g->builder, acond);
                        LLVMValueRef aiv = LLVMBuildLoad2(g->builder, i64,
                                                          aip, "acpy.iv");
                        LLVMBuildCondBr(g->builder,
                            zan_icmp(g->builder, LLVMIntSLT, aiv, alen,
                                     "acpy.more"),
                            abody, aend);
                        LLVMPositionBuilderAtEnd(g->builder, abody);
                        aiv = LLVMBuildLoad2(g->builder, i64, aip, "acpy.iv2");
                        LLVMValueRef aslot;
                        if (aek == LLVMStructTypeKind) {
                            aslot = LLVMBuildGEP2(g->builder, aetl, arr,
                                                  &aiv, 1, "acpy.ss");
                        } else {
                            LLVMValueRef aptr = LLVMBuildBitCast(g->builder,
                                arr, LLVMPointerType(aetl, 0), "acpy.ab");
                            aslot = LLVMBuildGEP2(g->builder, aetl, aptr,
                                                  &aiv, 1, "acpy.ss");
                        }
                        LLVMValueRef av;
                        if (aek == LLVMStructTypeKind) {
                            av = load_struct_from_slot(g, aslot, aetl);
                        } else {
                            av = LLVMBuildLoad2(g->builder, aetl, aslot,
                                                "acpy.sv");
                            if (aek == LLVMIntegerTypeKind) {
                                unsigned aw = LLVMGetIntTypeWidth(aetl);
                                if (aw < 64) {
                                    av = LLVMBuildSExt(g->builder, av, i64,
                                                       "acpy.sw");
                                } else if (aw > 64) {
                                    av = LLVMBuildTrunc(g->builder, av, i64,
                                                        "acpy.tw");
                                }
                            }
                        }
                        LLVMValueRef awpos = slot_word_index(g, aiv,
                                                             alwords);
                        LLVMValueRef adst = LLVMBuildGEP2(g->builder, i64,
                            adp, &awpos, 1, "acpy.ds");
                        emit_collection_slot_store(g, lelem, i64, adst, av,
                                                   NULL, locals, 0);
                        LLVMValueRef anx = LLVMBuildAdd(g->builder, aiv,
                            LLVMConstInt(i64, 1, 0), "acpy.nx");
                        LLVMBuildStore(g->builder, anx, aip);
                        LLVMBuildBr(g->builder, acond);
                        LLVMPositionBuilderAtEnd(g->builder, aend);
                        return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr,
                                                "listv");
                    }
                }
                /* 集合初始化式项处理 (new List<T>{ a, b, c })：发射逐项 Add 调用 */
                int ninit = expr->new_expr.args.count +
                            expr->new_expr.arg_inits.count;
                if (ninit == 0) {
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 0, "cnt");
                    zan_store_fit(g, LLVMConstInt(i64, 0, 0), count_ptr);
                    LLVMValueRef cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 1, "cap");
                    zan_store_fit(g, LLVMConstInt(i64, 0, 0), cap_ptr);
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 2, "df");
                    zan_store_fit(g, LLVMConstNull(LLVMPointerType(i64, 0)), data_field);
                    return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr, "listv");
                }
                long long initcap = ninit > 8 ? (long long)ninit : 8;
                /* count = ninit */
                LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 0, "cnt");
                zan_store_fit(g, LLVMConstInt(i64, (unsigned long long)ninit, 0), count_ptr);
                LLVMValueRef cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 1, "cap");
                zan_store_fit(g, LLVMConstInt(i64, (unsigned long long)initcap, 0), cap_ptr);
                unsigned lwords = elem_slot_words(g, lelem);
                LLVMValueRef data_size = LLVMConstInt(i64,
                    (unsigned long long)(initcap * 8 * lwords), 0);
                LLVMValueRef data_ptr = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), data_size }, 2, "data");
                zan_irgen_emit_oom_check(g, g->current_fn, data_ptr);
                LLVMValueRef data_typed = LLVMBuildBitCast(g->builder, data_ptr,
                    LLVMPointerType(i64, 0), "dptr");
                LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 2, "df");
                zan_store_fit(g, data_typed, data_field);
                for (int ii = 0; ii < ninit; ii++) {
                    zan_ast_node_t *item = ii < expr->new_expr.args.count
                        ? expr->new_expr.args.items[ii]
                        : expr->new_expr.arg_inits.items[
                              ii - expr->new_expr.args.count];
                    LLVMValueRef idxk = LLVMConstInt(i64,
                        (unsigned long long)ii * lwords, 0);
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data_typed, &idxk, 1, "iis");
                    LLVMValueRef ival = emit_expr(g, item, locals);
                    emit_collection_slot_store(g, lelem, i64, slot, ival, item, locals, 0);
                }
                return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr, "listv");
            }
        }

        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF &&
            !expr->new_expr.is_array) {
            zan_istr_t sbname = expr->new_expr.type->type_ref.name;
            if (sbname.len == 13 && memcmp(sbname.str, "StringBuilder", 13) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef sb_raw = emit_alloc_rc_collection(g, expr, 24, 2, NULL);
                LLVMValueRef sb_ptr = LLVMBuildBitCast(g->builder, sb_raw,
                    LLVMPointerType(g->sb_struct_type, 0), "sbp");
                LLVMValueRef cnt_p = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sb_ptr, 0, "sbc");
                zan_store_fit(g, LLVMConstInt(i64, 0, 0), cnt_p);
                LLVMValueRef cap_p = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sb_ptr, 1, "sbcap");
                zan_store_fit(g, LLVMConstInt(i64, 0, 0), cap_p);
                LLVMValueRef data_p = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sb_ptr, 2, "sbd");
                zan_store_fit(g, LLVMConstNull(i8ptr), data_p);
                return LLVMBuildBitCast(g->builder, sb_ptr, i8ptr, "sbv");
            }
        }

        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF &&
            !expr->new_expr.is_array) {
            zan_istr_t tname2 = expr->new_expr.type->type_ref.name;
            if ((tname2.len == 4 && memcmp(tname2.str, "Dict", 4) == 0) ||
                (tname2.len == 10 && memcmp(tname2.str, "Dictionary", 10) == 0)) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef dict_raw = emit_alloc_rc_collection(g, expr, 64, 3,
                    resolve_type_ctx(g, expr->new_expr.type));
                LLVMValueRef typed_ptr = LLVMBuildBitCast(g->builder, dict_raw,
                    LLVMPointerType(g->dict_struct_type, 0), "dptr");
                for (unsigned zf = 4; zf < 7; zf++) {
                    LLVMValueRef zp = LLVMBuildStructGEP2(g->builder,
                        g->dict_struct_type, typed_ptr, zf, "dz");
                    zan_store_fit(g,
                        (zf == 4) ? (LLVMValueRef)LLVMConstNull(LLVMPointerType(i64, 0))
                                  : (LLVMValueRef)LLVMConstInt(i64, 0, 0), zp);
                }
                zan_type_t *dict_type = resolve_type_ctx(g, expr->new_expr.type);
                zan_type_t *value_type = dict_value_type(dict_type);
                unsigned value_words = elem_slot_words(g, value_type);
                LLVMValueRef value_words_const = LLVMConstInt(i64, value_words, 0);
                zan_store_fit(g, value_words_const,
                    LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 7, "vwp"));
                /* 字典初始化式条目处理 (new Dict<K,V>{ {k, v}, ... })：发射逐项索引器写入 */
                zan_ast_list_t dict_inits;
                zan_ast_list_init(&dict_inits);
                for (int di = 0; di < expr->new_expr.args.count; di++)
                    zan_ast_list_push(&dict_inits,
                        expr->new_expr.args.items[di], g->arena);
                for (int di = 0; di < expr->new_expr.arg_inits.count; di++)
                    zan_ast_list_push(&dict_inits,
                        expr->new_expr.arg_inits.items[di], g->arena);
                int ninit = dict_inits.count;
                if (ninit == 0) {
                    /* count = 0 */
                    LLVMValueRef cnt_p = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 0, "cnt");
                    zan_store_fit(g, LLVMConstInt(i64, 0, 0), cnt_p);
                    /* capacity = 0 (惰性分配，避免空字典产生键值数组微碎片) */
                    LLVMValueRef cap_p = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 1, "cap");
                    zan_store_fit(g, LLVMConstInt(i64, 0, 0), cap_p);
                    /* keys = NULL */
                    LLVMValueRef kf = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 2, "kf");
                    zan_store_fit(g, LLVMConstNull(LLVMPointerType(i8ptr, 0)), kf);
                    /* values = NULL */
                    LLVMValueRef vf = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 3, "vf");
                    zan_store_fit(g, LLVMConstNull(LLVMPointerType(i64, 0)), vf);
                    return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr, "dictv");
                }
                /* count = 0 */
                LLVMValueRef cnt_p = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 0, "cnt");
                zan_store_fit(g, LLVMConstInt(i64, 0, 0), cnt_p);
                /* capacity = 16 */
                LLVMValueRef cap_p = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 1, "cap");
                zan_store_fit(g, LLVMConstInt(i64, 16, 0), cap_p);
                /* keys = malloc(16 * 8) */
                LLVMValueRef keys_sz = LLVMConstInt(i64, 128, 0);
                LLVMValueRef keys_raw = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), keys_sz }, 2, "keys");
                zan_irgen_emit_oom_check(g, g->current_fn, keys_raw);
                LLVMValueRef keys_typed = LLVMBuildBitCast(g->builder, keys_raw,
                    LLVMPointerType(i8ptr, 0), "kptr");
                LLVMValueRef kf = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 2, "kf");
                zan_store_fit(g, keys_typed, kf);
                /* values = malloc(16 * 8) */
                LLVMValueRef vals_sz = LLVMConstInt(i64, 128ULL * value_words, 0);
                LLVMValueRef vals_raw = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), vals_sz }, 2, "vals");
                zan_irgen_emit_oom_check(g, g->current_fn, vals_raw);
                LLVMValueRef vals_typed = LLVMBuildBitCast(g->builder, vals_raw,
                    LLVMPointerType(i64, 0), "vptr");
                LLVMValueRef vf = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 3, "vf");
                zan_store_fit(g, vals_typed, vf);
                if (ninit >= 2) {
                    zan_type_t *kt = dict_key_type(g, dict_type);
                    for (int ii = 0; ii + 1 < ninit; ii += 2) {
                        zan_ast_node_t *kexpr = dict_inits.items[ii];
                        zan_ast_node_t *vexpr = dict_inits.items[ii + 1];
                        LLVMValueRef key = emit_expr(g, kexpr, locals);
                        if (LLVMGetTypeKind(LLVMTypeOf(key)) == LLVMIntegerTypeKind) {
                            if (LLVMGetIntTypeWidth(LLVMTypeOf(key)) < 64)
                                key = LLVMBuildSExt(g->builder, key, i64, "k.sx");
                            key = LLVMBuildIntToPtr(g->builder, key, i8ptr, "k.ip");
                        } else if (LLVMTypeOf(key) != i8ptr) {
                            key = LLVMBuildBitCast(g->builder, key, i8ptr, "k.bc");
                        }
                        LLVMValueRef val = emit_expr(g, vexpr, locals);
                        emit_dict_value_set(g, dict_type, dict_raw, key, val,
                            kexpr, vexpr, locals);
                    }
                }
                return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr, "dictv");
            }
        }

        /* 数组初始化式 (new Type[] { a, b, c })：分配连续内存并填充初始元素 */
        if (expr->new_expr.is_array && expr->new_expr.array_init) {
            zan_type_t *elem_type = resolve_type_ctx(g, expr->new_expr.type);
            if (elem_type && elem_type->kind == TYPE_ARRAY && elem_type->element_type)
                elem_type = elem_type->element_type;
            if (!elem_type) elem_type = g->binder->type_int;
            LLVMTypeRef elem_llvm = map_type(g, elem_type);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            int n = expr->new_expr.args.count;
            LLVMValueRef total = zan_mul(g->builder,
                LLVMConstInt(i64t, (unsigned long long)n, 0),
                LLVMSizeOf(elem_llvm), "total");
            LLVMValueRef arr = zan_array_alloc_typed(g, total,
                LLVMConstInt(i64t, (unsigned long long)n, 0), elem_type);
            for (int k = 0; k < n; k++) {
                LLVMValueRef v = emit_arg_typed(g, expr->new_expr.args.items[k],
                                                elem_type, locals);
                /* 数组缓冲区持有元素所有权：ARC 引用类型元素保留 +1 计数 */
                if (elem_type && is_rc_managed_type(elem_type) &&
                    !expr_yields_owned_rc_value(g, expr->new_expr.args.items[k],
                                                locals)) {
                    emit_rc_retain_for_type(g, elem_type, v);
                }
                LLVMValueRef idx = LLVMConstInt(i64t, (unsigned long long)k, 0);
                LLVMValueRef slot = LLVMBuildGEP2(g->builder, elem_llvm, arr,
                                                  &idx, 1, "aep");
                LLVMBuildStore(g->builder, emit_boundary_coerce(g, v, elem_llvm), slot);
            }
            return arr;
        }

        /* 数组分配 (new Type[d1, d2, ...])：多维数组分配形状描述符与平铺缓冲区 */
        if (expr->new_expr.is_array && expr->new_expr.args.count > 0) {
            zan_type_t *alloc_type = resolve_type_ctx(g, expr->new_expr.type);
            if (!alloc_type) alloc_type = g->binder->type_int;
            int rank = expr->new_expr.array_rank > 0
                ? expr->new_expr.array_rank : 1;
            zan_type_t *elem_type = alloc_type;
            if (elem_type->kind == TYPE_ARRAY && elem_type->element_type)
                elem_type = elem_type->element_type;
            LLVMTypeRef elem_llvm = map_type(g, elem_type);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            int ndims = rank > 16 ? 16 : rank;
            LLVMValueRef dims[16];
            for (int d = 0; d < ndims; d++) {
                LLVMValueRef dv = emit_expr(g, expr->new_expr.args.items[d], locals);
                dv = emit_index_i64(g, dv, "arr.dim");
                LLVMValueRef negative = zan_icmp(g->builder, LLVMIntSLT, dv,
                    LLVMConstInt(i64t, 0, 0), "arr.negative");
                emit_runtime_check(g, negative,
                    expr->new_expr.args.items[d]->loc, "negative array length");
                /* 数组维度负数检查与越界防护 */
                dims[d] = LLVMBuildSelect(g->builder, negative,
                    LLVMConstInt(i64t, 0, 0), dv, "arr.dim.safe");
            }
            LLVMValueRef arr;
            if (rank <= 1) {
                LLVMValueRef total = zan_mul(g->builder, dims[0],
                    LLVMSizeOf(elem_llvm), "total");
                arr = zan_array_alloc_typed(g, total, dims[0], elem_type);
            } else {
                arr = zan_mdarray_alloc(g, dims, rank, elem_llvm);
            }
            int ninit = expr->new_expr.args.count - ndims;
            if (ninit > 0) {
                LLVMValueRef data = arr;
                if (rank > 1) {
                    LLVMValueRef data_off = LLVMConstInt(i64t,
                        (unsigned long long)rank * 8, 0);
                    data = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                        arr, &data_off, 1, "md.data");
                }
                LLVMValueRef typed = LLVMBuildBitCast(g->builder, data,
                    LLVMPointerType(elem_llvm, 0), "aip");
                for (int k = 0; k < ninit; k++) {
                    LLVMValueRef v = emit_arg_typed(g,
                        expr->new_expr.args.items[ndims + k], elem_type, locals);
                    if (elem_type && is_rc_managed_type(elem_type) &&
                        !expr_yields_owned_rc_value(g,
                            expr->new_expr.args.items[ndims + k], locals)) {
                        emit_rc_retain_for_type(g, elem_type, v);
                    }
                    LLVMValueRef idx = LLVMConstInt(i64t, (unsigned long long)k, 0);
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, elem_llvm,
                        typed, &idx, 1, "aep");
                    LLVMBuildStore(g->builder,
                        emit_boundary_coerce(g, v, elem_llvm), slot);
                }
            }
            return arr;
        }

        zan_istr_t type_name = {NULL, 0};
        if (expr->new_expr.type) {
            if (expr->new_expr.type->kind == AST_IDENTIFIER) {
                type_name = expr->new_expr.type->ident.name;
            } else if (expr->new_expr.type->kind == AST_TYPE_REF) {
                type_name = expr->new_expr.type->type_ref.name;
            }
        }
        if (type_name.str && type_name.len == 6 && memcmp(type_name.str, "object", 6) == 0) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef sz = LLVMConstInt(i64, 8, 0);
            LLVMValueRef site_name = LLVMConstNull(i8ptr);
            int site_idx = reserve_arc_site(g, NULL, NULL, 0, NULL);
            LLVMValueRef site_val = arc_site_arg(g, site_idx);
            if (g->check_leaks) {
                char site_buf[600];
                const char *sfile = loc_site_file(g, expr->loc);
                snprintf(site_buf, sizeof(site_buf), "%s:%u:%u",
                         sfile, expr->loc.line, expr->loc.col);
                site_name = zan_irgen_intern_string(g, site_buf);
            }
            LLVMTypeRef alloc_fn_type = LLVMFunctionType(i8ptr,
                (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0);
            LLVMValueRef alloc_args3[] = { sz, site_val, site_name };
            LLVMValueRef raw = zan_call2(g->builder, alloc_fn_type, g->rt_alloc, alloc_args3, 3, "newobj");
            LLVMValueRef zero64 = LLVMConstInt(i64, 0, 0);
            LLVMValueRef p64 = LLVMBuildBitCast(g->builder, raw, LLVMPointerType(i64, 0), "objp");
            zan_store_fit(g, zero64, p64);
            return raw;
        }
        zan_type_t *actual_t = resolve_type_ctx(g, expr->new_expr.type);
        if (actual_t && (actual_t->kind == TYPE_INT || actual_t->kind == TYPE_UINT ||
                         actual_t->kind == TYPE_LONG || actual_t->kind == TYPE_ULONG ||
                         actual_t->kind == TYPE_SHORT || actual_t->kind == TYPE_USHORT ||
                         actual_t->kind == TYPE_BYTE || actual_t->kind == TYPE_SBYTE ||
                         actual_t->kind == TYPE_BOOL || actual_t->kind == TYPE_FLOAT ||
                         actual_t->kind == TYPE_DOUBLE || actual_t->kind == TYPE_CHAR ||
                         actual_t->kind == TYPE_NINT)) {
            LLVMTypeRef lt = map_type(g, actual_t);
            return LLVMConstNull(lt);
        }
        zan_symbol_t *sym = NULL;
        if (actual_t && actual_t->sym &&
            (actual_t->sym->type->kind == TYPE_STRUCT || actual_t->sym->type->kind == TYPE_CLASS)) {
            sym = actual_t->sym;
        } else if (type_name.str) {
            sym = zan_binder_lookup(g->binder, type_name);
        }
        if (sym) {
            if (sym->type && (sym->type->kind == TYPE_STRUCT || sym->type->kind == TYPE_CLASS)) {
                LLVMTypeRef st = get_struct_llvm_type(g, sym);
                if (st) {
                    LLVMValueRef alloca;
                    if (sym->type->kind == TYPE_CLASS) {
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        LLVMValueRef sz = LLVMSizeOf(st);
                        LLVMValueRef site_name = LLVMConstNull(i8ptr);
                        LLVMValueRef site_val;
                        {
                            /* 分配固定分配点索引用于内存剖析与诊断 */
                            zan_type_t *site_inst =
                                resolve_type_ctx(g, expr->new_expr.type);
                            int site_idx = reserve_arc_site(
                                g, sym, site_inst, 0, NULL);
                            site_val = arc_site_arg(g, site_idx);
                            if (g->check_leaks) {
                                char site_buf[600];
                                const char *sfile = loc_site_file(g, expr->loc);
                                snprintf(site_buf, sizeof(site_buf), "%s:%u:%u",
                                         sfile, expr->loc.line, expr->loc.col);
                                site_name = zan_irgen_intern_string(g, site_buf);
                            }
                        }
                        LLVMTypeRef alloc_fn_type = LLVMFunctionType(i8ptr,
                            (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0);
                        LLVMValueRef alloc_args3[] = { sz, site_val, site_name };
                        LLVMValueRef raw = zan_call2(g->builder, alloc_fn_type, g->rt_alloc, alloc_args3, 3, "newobj");
                        alloca = LLVMBuildBitCast(g->builder, raw, LLVMPointerType(st, 0), "objp");
                    } else {
                        alloca = emit_entry_alloca(g, st, "new");
                    }
                    zan_store_fit(g, LLVMConstNull(st), alloca);

                    /* 查找并解析类的实例构造函数 */
                    zan_type_t *new_inst = resolve_type_ctx(g, expr->new_expr.type);

                    if (sym->type->kind == TYPE_CLASS && class_has_virtual_methods(sym)) {
                        LLVMTypeRef i8ptr_vt = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        /* 泛型类**每个实例化一张自己的 vtable**：整类共用一张表时，
                         * 表里的槽位指向擦除版方法体，而擦除版拿不到类型实参，
                         * 方法体里的 `m is T` 恒为 false——经基类引用做虚调用时
                         * 判定恒错（静默）。实例化自己的表指向特化版方法体。 */
                        LLVMValueRef vtg = get_vtable_global_inst(g, sym, new_inst);
                        LLVMValueRef vpf0 = LLVMBuildStructGEP2(g->builder, st, alloca, 0, "vpf.init");
                        zan_store_fit(g,
                            LLVMBuildBitCast(g->builder, vtg, i8ptr_vt, "vt.i8"), vpf0);
                    }

                    /* 对象初始化式字段赋值发射 */
                    int init_start = expr->new_expr.args.count;
                    while (init_start > 0) {
                        zan_ast_node_t *a =
                            expr->new_expr.args.items[init_start - 1];
                        if (a->kind == AST_COLL_INIT) {
                            if (get_field_index(sym, a->coll_init.name) < 0)
                                break;
                            init_start--;
                            continue;
                        }
                        if (a->kind != AST_ASSIGNMENT ||
                            a->binary.left->kind != AST_IDENTIFIER ||
                            get_field_index(sym, a->binary.left->ident.name) < 0)
                            break;
                        init_start--;
                    }
                    zan_ast_list_t ctor_arg_list = expr->new_expr.args;
                    ctor_arg_list.count = init_start;
                    struct zan_ctor_entry *ctor = find_ctor(
                        g, sym, &ctor_arg_list, locals, NULL);
                    if (!ctor) {
                        zan_ast_list_t filled;
                        if (fill_ctor_default_args(g, sym, &ctor_arg_list,
                                                   &filled)) {
                            struct zan_ctor_entry *dc = find_ctor(
                                g, sym, &filled, locals, NULL);
                            if (dc) {
                                ctor = dc;
                                ctor_arg_list = filled;
                            }
                        }
                    }
                    LLVMValueRef ctor_fn = NULL;
                    LLVMTypeRef ctor_ft = NULL;
                    if (ctor && new_inst && new_inst->type_arg_count > 0)
                        ctor_fn = find_generic_ctor(g, sym, ctor->decl,
                                                    new_inst->type_args,
                                                    new_inst->type_arg_count,
                                                    &ctor_ft);
                    if (!ctor_fn && ctor) {
                        ctor_fn = ctor->fn;
                        ctor_ft = ctor->fn_type;
                    }
                    if (ctor && ctor->decl) {
                        /* 构造函数命名参数对齐与重排序 */
                        reorder_named_args_impl(g, &ctor_arg_list,
                                                expr->loc, ctor->decl);
                    }
                    if (ctor_fn) {
                        int argc = ctor_arg_list.count + 1;
                        LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                        call_args[0] = alloca;
                        int obj_eh_pushed = 0;
                        if (sym->type->kind == TYPE_CLASS) {
                            emit_eh_tmp_push(g, alloca);
                            obj_eh_pushed = 1;
                        }
                        /* 实参槽：构造函数抛异常时由展开器释放实参里的拥有型
                         * 临时量（与普通方法调用路径同源判定）。 */
                        int *arg_eh_pushed = (int *)calloc((size_t)argc, sizeof(int));
                        for (int k = 0; k < ctor_arg_list.count; k++) {
                            zan_ast_node_t *param =
                                ctor->decl->method_decl.params.items[k];
                            zan_type_t *pt = zan_binder_resolve_type(
                                g->binder, param->param.type);
                            /* 泛型类构造函数签名特化与参数映射 */
                            if (new_inst && new_inst->type_arg_count > 0)
                                pt = subst_delegate_sig(g, pt, new_inst);
                            call_args[k + 1] = emit_arg_typed(
                                g, ctor_arg_list.items[k], pt, locals);
                            /* 实参里的拥有型临时量（new 出来的对象/集合）归
                             * 调用方所有：正常返回后由下面的
                             * emit_release_owned_call_temp 释放；构造函数
                             * 抛异常时必须先进 EH 临时栈交给展开器，否则
                             * 这些临时量没人释放（泄漏）。普通方法调用路径
                             * 一直这么做，构造路径此前漏了。 */
                            arg_eh_pushed[k + 1] = emit_call_arg_eh_push(
                                g, ctor_arg_list.items[k], call_args[k + 1],
                                locals);
                        }
                        coerce_args_to_params(g, ctor_ft, call_args, argc);
                        zan_call2(g->builder, ctor_ft, ctor_fn, call_args, (unsigned)argc, "");
                        /* 逆序弹出：实参槽先进后进（后进先出），对象槽最后弹 */
                        for (int k = argc - 1; k >= 0; k--)
                            if (arg_eh_pushed[k]) emit_eh_tmp_pop(g);
                        if (obj_eh_pushed) emit_eh_tmp_pop(g);
                        for (int k = 0; k < ctor_arg_list.count; k++) {
                            emit_release_owned_call_temp(g, ctor_arg_list.items[k],
                                call_args[k + 1], locals);
                        }
                        free(arg_eh_pushed);
                        free(call_args);
                    } else {
                        if (sym->type->kind == TYPE_CLASS &&
                            ctor_arg_list.count > 0)
                            zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3024,
                                "no constructor of '%.*s' accepts %d argument%s",
                                (int)sym->name.len, sym->name.str,
                                ctor_arg_list.count,
                                ctor_arg_list.count == 1 ? "" : "s");
                        int fields_eh_pushed = 0;
                        if (sym->type->kind == TYPE_CLASS) {
                            emit_eh_tmp_push(g, alloca);
                            fields_eh_pushed = 1;
                        }
                        emit_implicit_field_initializers(
                            g, sym, new_inst ? new_inst : sym->type,
                            alloca, locals);
                        if (fields_eh_pushed) emit_eh_tmp_pop(g);
                    }

                    {
                        /* 构造函数参数后置的常规对象初始化式 */
                        zan_ast_list_t object_inits;
                        zan_ast_list_init(&object_inits);
                        for (int oi = init_start;
                             oi < expr->new_expr.args.count; oi++) {
                            zan_ast_list_push(&object_inits,
                                expr->new_expr.args.items[oi], g->arena);
                        }
                        for (int oi = 0;
                             oi < expr->new_expr.arg_inits.count; oi++) {
                            zan_ast_list_push(&object_inits,
                                expr->new_expr.arg_inits.items[oi], g->arena);
                        }
                        for (int i = 0; i < object_inits.count; i++) {
                            zan_ast_node_t *arg = object_inits.items[i];
                            /* 嵌套集合初始化式 Members = { a, b } 降解 */
                            if (arg->kind == AST_COLL_INIT) {
                                zan_istr_t cname = arg->coll_init.name;
                                zan_symbol_t *msym = get_field_sym(sym, cname);
                                if (!msym || !msym->type) continue;
                                int getter_owned = 0;
                                zan_ast_node_t *recv = zan_ast_new(g->arena,
                                    AST_IDENTIFIER, arg->loc);
                                recv->ident.name = cname;
                                /* 对象初始化式中隐式读取新分配实例的 this 指针 */
                                LLVMTypeRef coll_lt = map_type(g, msym->type);
                                LLVMValueRef cslot = emit_entry_alloca(g,
                                    coll_lt, "cinit.slot");
                                zan_store_fit(g, LLVMConstNull(coll_lt), cslot);
                                int cfi = get_field_index(sym, cname);
                                zan_symbol_t *getter =
                                    property_getter_sym(g, msym);
                                if (getter) {
                                    LLVMValueRef v = emit_property_getter_call(
                                        g, getter,
                                        new_inst ? new_inst : sym->type,
                                        alloca, arg, locals);
                                    zan_store_fit(g, v, cslot);
                                    /* getter 返回值持有 +1 引用处理 */
                                    getter_owned = 1;
                                } else if (cfi >= 0) {
                                    LLVMValueRef cptr = emit_field_ptr(g, sym,
                                        st, alloca, cfi, "cinit.ptr");
                                    LLVMValueRef v = LLVMBuildLoad2(g->builder,
                                        coll_lt, cptr, "cinit.load");
                                    zan_store_fit(g, v, cslot);
                                } else {
                                    continue;
                                }
                                local_add(locals, cname, cslot, msym->type);
                                /* 类类型快照借用加载语义 */
                                for (int k = 0;
                                     k < arg->coll_init.items.count; k++) {
                                    zan_ast_node_t *item =
                                        arg->coll_init.items.items[k];
                                    zan_ast_node_t *madd = zan_ast_new(g->arena,
                                        AST_MEMBER_ACCESS, item->loc);
                                    madd->member.object = recv;
                                    madd->member.name = (zan_istr_t){"Add", 3};
                                    madd->member.null_cond = 0;
                                    zan_ast_node_t *addcall = zan_ast_new(
                                        g->arena, AST_CALL, item->loc);
                                    addcall->call.callee = madd;
                                    zan_ast_list_init(&addcall->call.args);
                                    zan_ast_list_init(&addcall->call.type_args);
                                    zan_ast_list_push(&addcall->call.args, item,
                                                      g->arena);
                                    if (msym->type &&
                                        type_named(msym->type, "Dict", 4) &&
                                        k + 1 < arg->coll_init.items.count) {
                                        zan_ast_list_push(&addcall->call.args,
                                            arg->coll_init.items.items[++k],
                                            g->arena);
                                    }
                                    emit_expr(g, addcall, locals);
                                }
                                for (int lv = locals->count - 1; lv >= 0; lv--) {
                                    if (locals->vars[lv].name.len == cname.len &&
                                        memcmp(locals->vars[lv].name.str,
                                               cname.str, (size_t)cname.len) == 0) {
                                        for (int sh = lv; sh < locals->count - 1; sh++)
                                            locals->vars[sh] = locals->vars[sh + 1];
                                        locals->count--;
                                        break;
                                    }
                                }
                                if (getter_owned) {
                                    LLVMValueRef cv = LLVMBuildLoad2(g->builder,
                                        coll_lt, cslot, "cinit.done");
                                    emit_rc_release_for_type(g, msym->type, cv);
                                }
                                continue;
                            }
                            if (arg->kind == AST_ASSIGNMENT && arg->binary.left->kind == AST_IDENTIFIER) {
                                zan_symbol_t *fsym = get_field_sym(sym, arg->binary.left->ident.name);
                                zan_symbol_t *setter = property_setter_sym(g, fsym);
                                if (setter) {
                                    emit_property_setter_call(g, setter,
                                        new_inst ? new_inst : sym->type,
                                        alloca, emit_expr(g, arg->binary.right, locals),
                                        arg->binary.left, arg->binary.right, locals);
                                    continue;
                                }
                                int fi = get_field_index(sym, arg->binary.left->ident.name);
                                if (fi >= 0) {
                                    LLVMValueRef fptr = emit_field_ptr(g, sym, st, alloca, fi, "finit");
                                    zan_symbol_t *fsym = get_field_sym(sym, arg->binary.left->ident.name);
                                    int fval_owned = 0;
                                    LLVMValueRef fval = NULL;
                                    if (fsym && fsym->type && type_is_binding(fsym->type) &&
                                        !type_is_binding(infer_expr_type(g, arg->binary.right, locals))) {
                                        fval = emit_binding_value(g, fsym->type,
                                                                  arg->binary.right, locals);
                                        if (fval) fval_owned = 1;
                                    }
                                    if (!fval)
                                        fval =
                                        (fsym && fsym->type && fsym->type->kind == TYPE_DELEGATE &&
                                         arg->binary.right->kind == AST_LAMBDA)
                                            ? emit_lambda_typed(g, arg->binary.right, fsym->type, locals)
                                            : emit_expr(g, arg->binary.right, locals);
                                    if (fsym && fsym->type) {
                                        LLVMTypeRef target_t = map_type(g, fsym->type);
                                        LLVMTypeRef val_t = LLVMTypeOf(fval);
                                        if (LLVMGetTypeKind(target_t) == LLVMFloatTypeKind &&
                                            LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                                            fval = LLVMBuildFPTrunc(g->builder, fval, target_t, "trunc");
                                        }
                                    }
                                    /* ARC 字段存储契约：旧值释放与新值保留 */
                                    if (fsym && fsym->type && fval) {
                                        zan_type_t *fsty = field_store_type(
                                            g, fsym, new_inst ? new_inst : sym->type);
                                        if (fsty && (is_rc_managed_type(fsty) || fsty->kind == TYPE_OBJECT) &&
                                            !(fsym->modifiers & MOD_WEAK)) {
                                            /* 绑定值发射结果处理 */
                                            emit_rc_store_field(g, fsty, fptr, fval,
                                                fval_owned
                                                    ? owned_rhs_marker(g, arg->binary.right->loc)
                                                    : arg->binary.right,
                                                locals, 0);
                                        } else if (fsty && fsty->kind == TYPE_STRUCT &&
                                                   type_contains_collection_rc(g, fsty, 0)) {
                                            emit_struct_field_capture(g, fsty, fptr, fval,
                                                fval_owned
                                                    ? owned_rhs_marker(g, arg->binary.right->loc)
                                                    : arg->binary.right, locals);
                                        } else {
                                            zan_store_fit(g, fval, fptr);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    return alloca;
                }
            }
        }
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

/* 统一三元表达式分支值类型至 PHI 节点类型，适配整型位宽与指针类型 */
static LLVMValueRef coerce_ternary_value(zan_irgen_t *g, LLVMValueRef v,
                                         LLVMTypeRef target) {
    if (!v || !target) return v;
    int vk = LLVMGetTypeKind(LLVMTypeOf(v));
    int tk = LLVMGetTypeKind(target);
    if (vk == LLVMIntegerTypeKind &&
        (tk == LLVMFloatTypeKind || tk == LLVMDoubleTypeKind))
        return LLVMBuildSIToFP(g->builder, v, target, "tern.sitofp");
    if ((vk == LLVMFloatTypeKind || vk == LLVMDoubleTypeKind) &&
        tk == LLVMIntegerTypeKind)
        return LLVMBuildFPToSI(g->builder, v, target, "tern.fptosi");
    return coerce_int_to(g, v, target);
}

static LLVMValueRef emit_expr_conditional(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        LLVMValueRef cond = emit_expr(g, expr->conditional.cond, locals);
        /* normalize to i1 */
        cond = zan_tobool(g->builder, cond, "cond");
        LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "tern.then");
        LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "tern.else");
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "tern.merge");
        LLVMBuildCondBr(g->builder, cond, then_bb, else_bb);

        LLVMPositionBuilderAtEnd(g->builder, then_bb);
        LLVMValueRef then_val = emit_expr(g, expr->conditional.then_expr, locals);
        LLVMBasicBlockRef then_end = LLVMGetInsertBlock(g->builder);

        LLVMPositionBuilderAtEnd(g->builder, else_bb);
        LLVMValueRef else_val = emit_expr(g, expr->conditional.else_expr, locals);
        LLVMBasicBlockRef else_end = LLVMGetInsertBlock(g->builder);

        /* A482：两臂都是 void 调用（语句位三元 `flag ? A() : B();`，最常见
         * 的三元写法之一）时不建 phi——phi void 是非法 IR（宿主与自举同病，
         * "Instruction has a name, but provides a void value!"），直接分支
         * 汇合返回哑值。 */
        if (LLVMGetTypeKind(LLVMTypeOf(then_val)) == LLVMVoidTypeKind &&
            LLVMGetTypeKind(LLVMTypeOf(else_val)) == LLVMVoidTypeKind) {
            LLVMPositionBuilderAtEnd(g->builder, then_end);
            LLVMBuildBr(g->builder, merge_bb);
            LLVMPositionBuilderAtEnd(g->builder, else_end);
            LLVMBuildBr(g->builder, merge_bb);
            LLVMPositionBuilderAtEnd(g->builder, merge_bb);
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* 分支字面量位宽对齐转换 */
        LLVMTypeRef tty = LLVMTypeOf(then_val);
        LLVMTypeRef ety = LLVMTypeOf(else_val);
        LLVMTypeRef phi_ty = tty;
        zan_type_t *conditional_type = infer_expr_type(g, expr, locals);
        if (conditional_type && is_rc_managed_type(conditional_type)) {
            int then_owned = expr_yields_owned_rc_value(
                g, expr->conditional.then_expr, locals);
            int else_owned = expr_yields_owned_rc_value(
                g, expr->conditional.else_expr, locals);
            /* 产生持有引用的条件表达式需规范化各分支所有权状态 */
            if (then_owned != else_owned) {
                if (!then_owned) {
                    LLVMPositionBuilderAtEnd(g->builder, then_end);
                    emit_rc_retain_for_type(g, conditional_type, then_val);
                } else {
                    LLVMPositionBuilderAtEnd(g->builder, else_end);
                    emit_rc_retain_for_type(g, conditional_type, else_val);
                }
            }
        }
        if (tty != ety) {
            zan_type_t *st = conditional_type;
            if (st) {
                LLVMTypeRef want = map_type(g, st);
                if (want) phi_ty = want;
            }
            if (phi_ty == tty) {
                int tk = LLVMGetTypeKind(tty), ek = LLVMGetTypeKind(ety);
                if (tk == LLVMIntegerTypeKind && ek == LLVMIntegerTypeKind) {
                    if (LLVMGetIntTypeWidth(ety) > LLVMGetIntTypeWidth(tty))
                        phi_ty = ety;
                } else if ((tk == LLVMFloatTypeKind || tk == LLVMDoubleTypeKind) &&
                           (ek == LLVMFloatTypeKind || ek == LLVMDoubleTypeKind)) {
                    if ((ek == LLVMDoubleTypeKind) &&
                        !(tk == LLVMDoubleTypeKind)) phi_ty = ety;
                } else if (ek == LLVMFloatTypeKind || ek == LLVMDoubleTypeKind) {
                    phi_ty = ety;
                }
            }
            LLVMPositionBuilderAtEnd(g->builder, then_end);
            then_val = coerce_ternary_value(g, then_val, phi_ty);
            LLVMPositionBuilderAtEnd(g->builder, else_end);
            else_val = coerce_ternary_value(g, else_val, phi_ty);
        }

        LLVMPositionBuilderAtEnd(g->builder, then_end);
        LLVMBuildBr(g->builder, merge_bb);
        LLVMPositionBuilderAtEnd(g->builder, else_end);
        LLVMBuildBr(g->builder, merge_bb);

        LLVMPositionBuilderAtEnd(g->builder, merge_bb);
        LLVMValueRef phi = LLVMBuildPhi(g->builder, phi_ty, "tern");
        LLVMValueRef incoming_vals[] = { then_val, else_val };
        LLVMBasicBlockRef incoming_bbs[] = { then_end, else_end };
        LLVMAddIncoming(phi, incoming_vals, incoming_bbs, 2);
        return phi;
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

/* switch 表达式 (expr switch { pattern => result, ... }) 模式匹配降解 */
static LLVMValueRef emit_expr_switch_expr(zan_irgen_t *g, zan_ast_node_t *expr,
                                          local_scope_t *locals) {
    zan_type_t *rty = infer_expr_type(g, expr, locals);
    if (!rty || rty->kind == TYPE_ERROR || rty->kind == TYPE_VOID)
        rty = g->binder->type_int;
    LLVMTypeRef rtll = map_type(g, rty);
    LLVMValueRef rslot = emit_entry_alloca(g, rtll, "swx.result");
    zan_store_fit(g, LLVMConstNull(rtll), rslot);

    static const char kSwx[] = { 1, 's', 'w', 'x' };
    zan_istr_t hname = { (char *)kSwx, 4 };
    int mark = locals->count;
    local_add(locals, hname, rslot, rty);

    zan_ast_node_t *syn = zan_ast_new(g->arena, AST_SWITCH_STMT, expr->loc);
    syn->switch_stmt.expr = expr->switch_expr.expr;
    zan_ast_list_init(&syn->switch_stmt.cases);

    int narms = expr->switch_expr.arms.count;
    bool has_default = false;
    for (int i = 0; i < narms; i++) {
        zan_ast_node_t *arm = expr->switch_expr.arms.items[i];
        if (arm->switch_arm.is_default) has_default = true;

        zan_ast_node_t *sc = zan_ast_new(g->arena, AST_SWITCH_CASE, arm->loc);
        sc->switch_case.pattern = arm->switch_arm.pattern;
        sc->switch_case.type_pattern = arm->switch_arm.type_pattern;
        sc->switch_case.when_cond = arm->switch_arm.when_cond;
        sc->switch_case.var_name = arm->switch_arm.var_name;

        /* body: { __swx = result; break; } */
        zan_ast_node_t *block = zan_ast_new(g->arena, AST_BLOCK, arm->loc);
        zan_ast_list_init(&block->block.stmts);
        zan_ast_node_t *assign = zan_ast_new(g->arena, AST_ASSIGNMENT, arm->loc);
        zan_ast_node_t *lhs = zan_ast_new(g->arena, AST_IDENTIFIER, arm->loc);
        lhs->ident.name = hname;
        assign->binary.op = TK_EQ;
        assign->binary.left = lhs;
        assign->binary.right = arm->switch_arm.result;
        zan_ast_node_t *es = zan_ast_new(g->arena, AST_EXPR_STMT, arm->loc);
        es->expr_stmt.expr = assign;
        zan_ast_list_push(&block->block.stmts, es, g->arena);
        zan_ast_node_t *brk = zan_ast_new(g->arena, AST_BREAK_STMT, arm->loc);
        zan_ast_list_push(&block->block.stmts, brk, g->arena);
        sc->switch_case.body = block;
        zan_ast_list_push(&syn->switch_stmt.cases, sc, g->arena);
    }
    if (!has_default) {
        zan_ast_node_t *sc = zan_ast_new(g->arena, AST_SWITCH_CASE, expr->loc);
        zan_ast_node_t *block = zan_ast_new(g->arena, AST_BLOCK, expr->loc);
        zan_ast_list_init(&block->block.stmts);
        zan_ast_node_t *brk = zan_ast_new(g->arena, AST_BREAK_STMT, expr->loc);
        zan_ast_list_push(&block->block.stmts, brk, g->arena);
        sc->switch_case.body = block;
        zan_ast_list_push(&syn->switch_stmt.cases, sc, g->arena);
    }

    emit_stmt(g, syn, locals);
    locals->count = mark;
    return LLVMBuildLoad2(g->builder, rtll, rslot, "swx.load");
}

/* record with 表达式：非破坏性浅拷贝并覆盖指定字段 */
static LLVMValueRef emit_expr_with_expr(zan_irgen_t *g, zan_ast_node_t *expr,
                                        local_scope_t *locals) {
    zan_istr_t cm = { (char *)"__CloneWith", 11 };
    zan_type_t *rty = concretize(g, infer_expr_type(g, expr->with_expr.expr,
                                                    locals));
    if (!rty || !rty->sym ||
        !get_method_sym(rty->sym, cm)) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3025,
            "'with' requires a record receiver with a synthesized clone");
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
    }
    static const char kWr[] = { 1, 'w', 'r' };
    zan_istr_t hname = { (char *)kWr, 3 };
    LLVMTypeRef rtll = map_type(g, rty);
    LLVMValueRef recv = emit_expr(g, expr->with_expr.expr, locals);
    LLVMValueRef rslot = emit_entry_alloca(g, rtll, "with.recv");
    zan_store_fit(g, recv, rslot);
    int mark = locals->count;
    local_add(locals, hname, rslot, rty);

    zan_ast_node_t *recv_id = zan_ast_new(g->arena, AST_IDENTIFIER, expr->loc);
    recv_id->ident.name = hname;
    zan_ast_node_t *callee = zan_ast_new(g->arena, AST_MEMBER_ACCESS, expr->loc);
    callee->member.object = recv_id;
    callee->member.name = cm;
    zan_ast_node_t *call = zan_ast_new(g->arena, AST_CALL, expr->loc);
    call->call.callee = callee;
    zan_ast_list_init(&call->call.args);
    zan_ast_list_init(&call->call.type_args);
    for (int fi = 0; fi < rty->sym->member_count; fi++) {
        zan_symbol_t *m = rty->sym->members[fi];
        if (!m || m->kind != SYM_FIELD) continue;
        zan_ast_node_t *arg = NULL;
        for (int ai = 0; ai < expr->with_expr.assigns.count; ai++) {
            zan_ast_node_t *asg = expr->with_expr.assigns.items[ai];
            if (asg && asg->kind == AST_ASSIGNMENT &&
                asg->binary.left->ident.name.len == m->name.len &&
                memcmp(asg->binary.left->ident.name.str, m->name.str,
                       (size_t)m->name.len) == 0) {
                arg = asg->binary.right;
                break;
            }
        }
        if (!arg) {
            arg = zan_ast_new(g->arena, AST_MEMBER_ACCESS, expr->loc);
            zan_ast_node_t *obj =
                zan_ast_new(g->arena, AST_IDENTIFIER, expr->loc);
            obj->ident.name = hname;
            arg->member.object = obj;
            arg->member.name = m->name;
        }
        zan_ast_list_push(&call->call.args, arg, g->arena);
    }

    LLVMValueRef result = emit_expr(g, call, locals);
    /* 接收者所有权转移与生命周期管理 */
    LLVMValueRef recv_now = LLVMBuildLoad2(g->builder, rtll, rslot,
                                           "with.recv.load");
    emit_release_owned_call_temp(g, expr->with_expr.expr, recv_now, locals);
    locals->count = mark;
    return result;
}

/* 协程帧结果槽为 64 位宽：await 统一加载 64 位槽值并进行反向类型转换 */
static LLVMValueRef coerce_await_result(zan_irgen_t *g, zan_ast_node_t *expr,
        LLVMValueRef res, local_scope_t *locals) {
    zan_type_t *rt = concretize(g, infer_expr_type(g, expr->await_expr.expr, locals));
    if (rt && rt->type_arg_count > 0) {
        if (rt->kind == TYPE_TASK ||
            (rt->name.str && ((rt->name.len >= 4 && memcmp(rt->name.str, "Task", 4) == 0) ||
                             (rt->name.len >= 9 && memcmp(rt->name.str, "ValueTask", 9) == 0))) ||
            (rt->sym &&
             ((rt->sym->name.len == 4 && memcmp(rt->sym->name.str, "Task", 4) == 0) ||
              (rt->sym->name.len == 9 && memcmp(rt->sym->name.str, "ValueTask", 9) == 0)))) {
            rt = concretize(g, rt->type_args[0]);
        }
    }
    return coerce_from_frame_result(g, res, rt);
}

static zan_symbol_t *direct_extern_method(zan_irgen_t *g, zan_ast_node_t *expr) {
    if (!expr || expr->kind != AST_CALL || !expr->call.callee) return NULL;
    zan_symbol_t *sym = NULL;
    if (expr->call.callee->kind == AST_IDENTIFIER) {
        if (g->current_type_sym)
            sym = get_method_sym(g->current_type_sym,
                                 expr->call.callee->ident.name);
        if (!sym)
            sym = zan_binder_lookup(g->binder, expr->call.callee->ident.name);
    } else if (expr->call.callee->kind == AST_MEMBER_ACCESS &&
               expr->call.callee->member.object->kind == AST_IDENTIFIER) {
        zan_symbol_t *cls = zan_binder_lookup(g->binder,
            expr->call.callee->member.object->ident.name);
        if (cls) sym = get_method_sym(cls, expr->call.callee->member.name);
    }
    if (!sym || !sym->decl || sym->decl->kind != AST_METHOD_DECL) return NULL;
    if (!zan_ast_method_extern_lib(sym->decl).str &&
        !(sym->decl->method_decl.modifiers & MOD_EXTERN))
        return NULL;
    return sym;
}

static bool blocking_integer_type(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BOOL:
    case TYPE_BYTE:
    case TYPE_SHORT:
    case TYPE_INT:
    case TYPE_LONG:
    case TYPE_SBYTE:
    case TYPE_USHORT:
    case TYPE_UINT:
    case TYPE_ULONG:
    case TYPE_CHAR:
    case TYPE_NINT:
    case TYPE_ENUM:
        return true;
    default:
        return false;
    }
}

static LLVMValueRef blocking_arg_i64(zan_irgen_t *g, LLVMValueRef v,
                                     zan_type_t *type) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMIntegerTypeKind)
        return LLVMBuildPtrToInt(g->builder, v, i64, "blocking.arg");
    if (LLVMGetIntTypeWidth(LLVMTypeOf(v)) == 64) return v;
    if (type && (type->kind == TYPE_BOOL || type->kind == TYPE_BYTE ||
                 type->kind == TYPE_USHORT || type->kind == TYPE_UINT ||
                 type->kind == TYPE_ULONG || type->kind == TYPE_CHAR))
        return LLVMBuildZExt(g->builder, v, i64, "blocking.arg");
    return LLVMBuildSExt(g->builder, v, i64, "blocking.arg");
}

/* 原生异步挂起原语发射 */
static LLVMValueRef emit_await_blocking_extern(zan_irgen_t *g,
        zan_ast_node_t *expr, local_scope_t *locals, zan_symbol_t *sym) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    zan_ast_node_t *call = expr->await_expr.expr;
    zan_ast_node_t *decl = sym->decl;
    int argc = call->call.args.count;
    zan_type_t *ret = decl->method_decl.return_type
        ? resolve_type_ctx(g, decl->method_decl.return_type)
        : g->binder->type_void;

    if (strstr(g->target_triple, "wasm") != NULL) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3026,
            "await native extern calls are not supported on wasm32: "
            "the blocking runtime requires native worker threads");
        return LLVMConstInt(i64, 0, 0);
    }

    if (!g->current_async_frame || !g->current_async_switch) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3027,
            "await native extern calls are only supported inside an async "
            "method");
        return LLVMConstInt(i64, 0, 0);
    }
    if (argc > 4) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3028,
            "await native extern call has %d arguments; at most four scalar "
            "arguments are supported (prepare data before suspension and "
            "await a handle-only operation)", argc);
        return LLVMConstInt(i64, 0, 0);
    }
    if (ret->kind != TYPE_VOID && ret->kind != TYPE_INT &&
        ret->kind != TYPE_LONG && ret->kind != TYPE_NINT) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3029,
            "await native extern call returns '%s', but only int, long, nint, "
            "or void can cross a suspension safely; return a scalar handle "
            "instead", ret->name.str ? ret->name.str : "unsupported");
        return LLVMConstInt(i64, 0, 0);
    }

    for (int k = 0; k < argc; k++) {
        zan_ast_node_t *p = decl->method_decl.params.items[k];
        zan_type_t *pt = (p && p->kind == AST_PARAM)
            ? resolve_type_ctx(g, p->param.type) : NULL;
        if (!blocking_integer_type(pt)) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, call->call.args.items[k]->loc, 3030,
                "await native extern argument %d has type '%s'; only integer, "
                "nint, bool, and enum scalars are safe across suspension "
                "(prepare/bind managed data first, then await a handle-only "
                "operation)", k + 1, pt && pt->name.str ? pt->name.str : "unsupported");
            return LLVMConstInt(i64, 0, 0);
        }
    }

    int fi = irgen_find_function(g, sym);
    LLVMValueRef native = fi >= 0 ? g->functions[fi].fn : NULL;
    if (!native) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3031,
            "cannot lower await native extern call: external function is not "
            "available in the generated module");
        return LLVMConstInt(i64, 0, 0);
    }

    LLVMValueRef args[4] = {
        LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0),
        LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0)
    };
    for (int k = 0; k < argc; k++) {
        zan_ast_node_t *p = decl->method_decl.params.items[k];
        zan_type_t *pt = resolve_type_ctx(g, p->param.type);
        args[k] = blocking_arg_i64(g,
            emit_arg_typed(g, call->call.args.items[k], pt, locals), pt);
    }

    g->uses_socket_async = true;
    int state = g->current_async_next_state++;
    LLVMValueRef frame = g->current_async_frame;
    LLVMTypeRef frame_type = g->current_async_frame_type;
    LLVMValueRef frame_i8 = LLVMBuildBitCast(g->builder, frame, i8ptr,
                                             "blocking.frame");
    LLVMValueRef out = NULL;
    if (ret->kind != TYPE_VOID) {
        out = get_async_result_ptr(g);
    } else {
        out = LLVMConstNull(LLVMPointerType(i64, 0));
    }
    LLVMValueRef fnptr = LLVMBuildBitCast(g->builder, native, i8ptr,
                                          "blocking.fn");
    zan_store_fit(g, LLVMConstInt(i32, (unsigned)state, 0), get_async_state_ptr(g));
    LLVMValueRef rt_args[] = {
        fnptr, LLVMConstInt(i32, (unsigned)argc, 0),
        args[0], args[1], args[2], args[3],
        frame_i8, g->current_async_resume_fn, out
    };
    zan_call2(g->builder, g->rt_blocking_co_type, g->rt_blocking_co,
              rt_args, 9, "");
    LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

    LLVMBasicBlockRef resume = LLVMAppendBasicBlockInContext(g->ctx,
        g->current_async_resume_fn, "co.blocking.resume");
    LLVMAddCase(g->current_async_switch,
        LLVMConstInt(i32, (unsigned)state, 0), resume);
    LLVMPositionBuilderAtEnd(g->builder, resume);
    if (ret->kind == TYPE_VOID) return LLVMConstInt(i64, 0, 0);
    LLVMValueRef raw = LLVMBuildLoad2(g->builder, i64, get_async_result_ptr(g), "blocking.raw");
    return coerce_await_result(g, expr, raw, locals);
}

/* await 的目标是不是**异步**的。
 *
 * await 的目标分两类：
 *  - **异步目标**——它的值是"子协程帧"，结果要从帧里抽出来；
 *  - **普通目标**——它的值就是"普通值"，await 只是把它原样带过去。
 *
 * 所以要判断"这调用是不是 async"。判定有几种：
 *  - **Task / ValueTask 类型**——能直接接住；
 *  - **async 方法**——调用目标原型是"双反"；
 *  - **async 委托**——调用的对象（或被调者本身）是 async 委托。
 *
 * 判错的后果是读野指针：普通值往往也是 i8 指针（字符串就是），与子
 * 协程帧的指针同型，被当成帧去读就会读出面元指针——静默的垃圾值，
 * 不报错。
 */
static bool await_target_is_async(zan_irgen_t *g, zan_ast_node_t *expr,
                                  local_scope_t *locals) {
    if (!expr) return false;
    zan_type_t *ty = concretize(g, infer_expr_type(g, expr, locals));
    if (ty) {
        if (ty->kind == TYPE_TASK) return true;
        if (ty->name.str && ty->name.len >= 4 &&
            memcmp(ty->name.str, "Task", 4) == 0) return true;
        if (ty->name.str && ty->name.len >= 9 &&
            memcmp(ty->name.str, "ValueTask", 9) == 0) return true;
    }
    if (expr->kind != AST_CALL || !expr->call.callee) return false;
    zan_ast_node_t *callee = expr->call.callee;
    /* 被调者本身就是委托（局部变量、字段、下标元素都算）——
     * `await shutdownHooks[i]()`。委托类型自带 async 标记。 */
    if (callee->kind != AST_MEMBER_ACCESS) {
        zan_type_t *callee_ty = infer_expr_type(g, callee, locals);
        if (callee_ty && callee_ty->kind == TYPE_DELEGATE)
            return callee_ty->delegate_is_async;
    }
    if (callee->kind == AST_MEMBER_ACCESS) {
        zan_type_t *obj_ty = infer_expr_type(g, callee->member.object, locals);
        /* async 委托：调用的对象是 async 委托。 */
        if (obj_ty && obj_ty->kind == TYPE_DELEGATE && obj_ty->delegate_is_async)
            return true;
        /* 字段本身是 async 委托：`await this.recv(buf, n)`。 */
        zan_type_t *field_ty = member_access_field_type(g, locals, callee);
        if (field_ty && field_ty->kind == TYPE_DELEGATE)
            return field_ty->delegate_is_async;
        if (obj_ty) {
            zan_symbol_t *ts = obj_ty->sym ? obj_ty->sym
                : expr_class_sym(g, callee->member.object, locals);
            if (ts) {
                /* 接口方法：签名在接口上声明，async 标记也在那儿。 */
                if (ts->kind == SYM_INTERFACE) {
                    zan_symbol_t *im = resolve_iface_overload(ts,
                        callee->member.name, expr->call.args.count,
                        expr->call.type_args.count);
                    if (im && im->decl && im->decl->kind == AST_METHOD_DECL)
                        return (im->decl->method_decl.modifiers & MOD_ASYNC) != 0;
                }
                zan_symbol_t *msym = resolve_overload_typed(g, ts,
                    callee->member.name, expr, locals);
                if (msym && msym->decl && msym->decl->kind == AST_METHOD_DECL)
                    return (msym->decl->method_decl.modifiers & MOD_ASYNC) != 0;
            }
        } else {
            /* 静态接收者写的是**类名**（`Socket.SendStringAsync(...)`）——
             * 类名不是值，infer 给不出类型，要按名字查符号。 */
            zan_symbol_t *cs = callee->member.object->kind == AST_IDENTIFIER
                ? zan_binder_lookup(g->binder, callee->member.object->ident.name)
                : NULL;
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *msym = resolve_overload_typed(g, cs,
                    callee->member.name, expr, locals);
                if (msym && msym->decl && msym->decl->kind == AST_METHOD_DECL)
                    return (msym->decl->method_decl.modifiers & MOD_ASYNC) != 0;
            }
        }
    } else if (callee->kind == AST_IDENTIFIER) {
        /* 局部变量装的委托：`await driver(this, method, path, body)`。 */
        local_var_t *lv = local_find(locals, callee->ident.name);
        if (lv && lv->type && lv->type->kind == TYPE_DELEGATE)
            return lv->type->delegate_is_async;
        if (g->current_type_sym) {
            zan_symbol_t *msym = resolve_overload_typed(g, g->current_type_sym,
                callee->ident.name, expr, locals);
            if (msym && msym->decl && msym->decl->kind == AST_METHOD_DECL)
                return (msym->decl->method_decl.modifiers & MOD_ASYNC) != 0;
        }
    }
    return false;
}

static LLVMValueRef emit_expr_await_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* await 落在 async 方法的 lambda 体内：lambda 不是协程，没有自己的帧可挂
         * ——硬降级会把外层协程的块当自己的用（跨函数的 br，LLVM 校验直接失败：
         * "Referring to a basic block in another function"）。这里报诊断，不落那条
         * 非法 IR。同步方法里的 lambda 没有外层协程，根驱动等得到，不算错。 */
        if (g->lambda_in_async_method) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3032,
                          "'await' inside a lambda is not supported: "
                          "a lambda is not a coroutine");
            return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
        }

        /* await Task.Yield(): 协作式让出控制权并将当前协程帧重新入队 */
        if (is_call_to(expr->await_expr.expr, "Task", "Yield") &&
            expr->await_expr.expr->call.args.count == 0) {
            if (g->current_async_frame && g->current_async_switch) {
                int k = g->current_async_next_state++;
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                LLVMBuildBr(g->builder, get_async_requeue_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                return LLVMConstInt(di64, 0, 0);
            }
            return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
        }

        /* await Task.Delay(ms): 基于定时器的挂起（复用当前帧，不分配子帧） */
        if (is_call_to(expr->await_expr.expr, "Task", "Delay") &&
            expr->await_expr.expr->call.args.count == 1) {
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef ms = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
            if (LLVMTypeOf(ms) != di64) {
                ms = LLVMBuildIntCast2(g->builder, ms, di64, 1, "ms64");
            }
            if (g->current_async_frame && g->current_async_switch) {
                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_co_delay_type, g->rt_co_delay,
                    (LLVMValueRef[]){ ms, self_i8, g->current_async_resume_fn }, 3, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                return LLVMConstInt(di64, 0, 0);
            }
            /* 顶层非异步上下文：同步休眠等待。
             *
             * 这里原来只**查**模块里的 "Sleep"（Windows）/ "poll"
             * （POSIX）：LLVMGetNamedFunction 找不到就返回 NULL，而这两个
             * 符号本来就不在当前模块里（Sleep 是 Win32 API、poll 是 libc
             * 符号，链接期才进来）——查不到就**整段等待被静默跳过**：
             * `await Task.Delay(500)` 编译成一条 ret，方法立刻返回，
             * 调用方以为等了 500ms，其实等了 0ms。
             *
             * 改法与 irgen.c 里 zan_io_pump_timeout 一致：**声明**符号
             * （LLVMAddFunction）而不是只查——声明进模块后链接期由系统库
             * 解析，符号一定找得到。
             *
             * 代价：同步休眠**占住调用线程**。这段代码在 async 协程内部被
             * 调用时，该 worker 线程上的其它协程要等休眠结束才能派发。
             *
             * 修法（A470）：不要用裸的 Sleep / poll——那会把整段时间
             * 变成"谁都不派发"。改成调运行时的 zan_co_sync_sleep：它把
             * 一次长睡拆成**按下一个定时器截止时间切片**的等待，每片醒来
             * 派发一次到期定时器并把被唤醒的协程跑起来。于是别的协程的
             * 定时器照样按时到期，而这次等待本身也照样等满。
             *
             * 实测（单 worker）：一个协程在 async 内部同步睡 500ms，另一
             * 个协程 200ms 的定时器从 503ms（改前）回到 204ms（改后）。
             * 注意必须**在 async 协程内部**才复现——放在 Main 里占的是
             * 主线程（不是 worker），worker 池照常派发，看不出差别。
             */
            LLVMValueRef ms32 = LLVMBuildTrunc(g->builder, ms, di32, "ms32");
            {
                LLVMTypeRef ss_args[] = { di64 };
                LLVMTypeRef ss_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                    ss_args, 1, 0);
                LLVMValueRef fn_ss = LLVMGetNamedFunction(g->mod, "zan_co_sync_sleep");
                if (!fn_ss) fn_ss = LLVMAddFunction(g->mod, "zan_co_sync_sleep", ss_ty);
                zan_call2(g->builder, ss_ty, fn_ss,
                    (LLVMValueRef[]){ zan_iwiden(g->builder, ms32, di64) }, 1, "");
            }
            return LLVMConstInt(di64, 0, 0);
        }

        /* await Gate.Park(handle): 事件驱动协程等待原语 */
        if (is_call_to(expr->await_expr.expr, "Gate", "Park") &&
            expr->await_expr.expr->call.args.count == 1) {
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            if (!(g->current_async_frame && g->current_async_switch)) {
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3033,
                    "await Gate.Park is only supported inside an async method");
                return LLVMConstInt(di64, 0, 0);
            }
            LLVMValueRef handle = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
            if (LLVMTypeOf(handle) != di64) {
                handle = LLVMBuildIntCast2(g->builder, handle, di64, 1, "gate64");
            }
            g->uses_socket_async = true;
            LLVMTypeRef gate_park_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                (LLVMTypeRef[]){ di64, di8ptr, g->co_step_ptr }, 3, 0);
            LLVMValueRef gate_park = LLVMGetNamedFunction(g->mod, "zan_gate_park");
            if (!gate_park) {
                gate_park = LLVMAddFunction(g->mod, "zan_gate_park", gate_park_type);
            }
            int k = g->current_async_next_state++;
            LLVMValueRef self_i8 = get_async_self_i8(g);
            zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
            zan_call2(g->builder, gate_park_type, gate_park,
                (LLVMValueRef[]){ handle, self_i8, g->current_async_resume_fn }, 3, "");
            LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

            LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                g->current_async_resume_fn, "co.resume");
            LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
            LLVMPositionBuilderAtEnd(g->builder, rk);
            return LLVMConstInt(di64, 0, 0);
        }

        /* await Task.JoinWait(entry): 事件驱动汇合挂起 */
        if (is_call_to(expr->await_expr.expr, "Task", "JoinWait") &&
            expr->await_expr.expr->call.args.count == 1) {
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef entry = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
            if (LLVMTypeOf(entry) != di64)
                entry = LLVMBuildIntCast2(g->builder, entry, di64, 1, "join64");
            LLVMTypeRef jw_type = LLVMFunctionType(di32,
                (LLVMTypeRef[]){ di64, di8ptr, g->co_step_ptr }, 3, 0);
            LLVMValueRef jw = LLVMGetNamedFunction(g->mod, "zan_join_wait2");
            if (!jw) jw = LLVMAddFunction(g->mod, "zan_join_wait2", jw_type);
            if (g->current_async_frame && g->current_async_switch) {
                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                LLVMValueRef r = zan_call2(g->builder, jw_type, jw,
                    (LLVMValueRef[]){ entry, self_i8, g->current_async_resume_fn },
                    3, "join.wait");
                LLVMBuildStore(g->builder,
                    LLVMBuildZExt(g->builder, r, di64, "join.r"),
                    get_async_result_ptr(g));
                LLVMValueRef must_suspend = zan_icmp(g->builder, LLVMIntEQ, r,
                    LLVMConstInt(di32, 0, 0), "join.susp");
                LLVMValueRef fn = g->current_fn;
                LLVMBasicBlockRef susp_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "join.park");
                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMBuildCondBr(g->builder, must_suspend, susp_bb, rk);
                LLVMPositionBuilderAtEnd(g->builder, susp_bb);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                return LLVMBuildLoad2(g->builder, di64, get_async_result_ptr(g), "join.r2");
            }
            LLVMValueRef r2 = zan_call2(g->builder, jw_type, jw,
                (LLVMValueRef[]){ entry, LLVMConstNull(di8ptr),
                                  LLVMConstNull(g->co_step_ptr) }, 3, "join.wait.root");
            return LLVMBuildZExt(g->builder, r2, di64, "join.r");
        }

        /* 套接字就绪态异步等待（Reactor 反应堆通知） */
        {
            bool is_read  = is_call_to(expr->await_expr.expr, "Socket", "ReadReady");
            bool is_write = is_call_to(expr->await_expr.expr, "Socket", "WriteReady");
            if ((is_read || is_write) && expr->await_expr.expr->call.args.count == 1) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3034,
                        "await Socket.%s is only supported inside an async method",
                        is_read ? "ReadReady" : "WriteReady");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef fd = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(fd) != di64) {
                    fd = LLVMBuildIntCast2(g->builder, fd, di64, 1, "fd64");
                }
                /* ZAN_IO_READ = 1, ZAN_IO_WRITE = 2 (see rt_io.h) */
                LLVMValueRef interest = LLVMConstInt(di32, is_read ? 1 : 2, 0);
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_io_wait_co_type, g->rt_io_wait_co,
                    (LLVMValueRef[]){ fd, interest, self_i8, g->current_async_resume_fn }, 4, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                return LLVMConstInt(di64, 0, 0);
            }
        }

        /* Windows IOCP 重叠接收异步等待 */
        {
            if (is_call_to(expr->await_expr.expr, "Socket", "RecvOv") &&
                expr->await_expr.expr->call.args.count == 3) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3035,
                        "await Socket.RecvOv is only supported inside an async method");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef fd = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(fd) != di64)
                    fd = LLVMBuildIntCast2(g->builder, fd, di64, 1, "fd64");
                LLVMValueRef buf = emit_expr(g, expr->await_expr.expr->call.args.items[1], locals);
                if (LLVMTypeOf(buf) != di8ptr) {
                    if (LLVMGetTypeKind(LLVMTypeOf(buf)) == LLVMIntegerTypeKind)
                        buf = LLVMBuildIntToPtr(g->builder, buf, di8ptr, "recvbuf");
                    else
                        buf = LLVMBuildBitCast(g->builder, buf, di8ptr, "recvbuf");
                }
                LLVMValueRef len = emit_expr(g, expr->await_expr.expr->call.args.items[2], locals);
                if (LLVMTypeOf(len) != di32)
                    len = LLVMBuildIntCast2(g->builder, len, di32, 1, "len32");
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                LLVMValueRef out_n = get_async_result_ptr(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_io_recv_co_type, g->rt_io_recv_co,
                    (LLVMValueRef[]){ fd, buf, len, self_i8,
                        g->current_async_resume_fn, out_n }, 6, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                return LLVMBuildLoad2(g->builder, di64, get_async_result_ptr(g), "recvn");
            }
        }

        /* 带超时的 Windows 套接字重叠接收挂起 */
        {
            if (is_call_to(expr->await_expr.expr, "Socket", "RecvToOv") &&
                expr->await_expr.expr->call.args.count == 4) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3036,
                        "await Socket.RecvToOv is only supported inside an async method");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef fd = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(fd) != di64)
                    fd = LLVMBuildIntCast2(g->builder, fd, di64, 1, "fd64");
                LLVMValueRef buf = emit_expr(g, expr->await_expr.expr->call.args.items[1], locals);
                if (LLVMTypeOf(buf) != di8ptr) {
                    if (LLVMGetTypeKind(LLVMTypeOf(buf)) == LLVMIntegerTypeKind)
                        buf = LLVMBuildIntToPtr(g->builder, buf, di8ptr, "recvbuf");
                    else
                        buf = LLVMBuildBitCast(g->builder, buf, di8ptr, "recvbuf");
                }
                LLVMValueRef len = emit_expr(g, expr->await_expr.expr->call.args.items[2], locals);
                if (LLVMTypeOf(len) != di32)
                    len = LLVMBuildIntCast2(g->builder, len, di32, 1, "len32");
                LLVMValueRef tmo = emit_expr(g, expr->await_expr.expr->call.args.items[3], locals);
                if (LLVMTypeOf(tmo) != di64)
                    tmo = LLVMBuildIntCast2(g->builder, tmo, di64, 1, "tmo64");
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                LLVMValueRef out_n = get_async_result_ptr(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_io_recv_to_co_type, g->rt_io_recv_to_co,
                    (LLVMValueRef[]){ fd, buf, len, tmo, self_i8,
                        g->current_async_resume_fn, out_n }, 7, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                return LLVMBuildLoad2(g->builder, di64, get_async_result_ptr(g), "recvton");
            }
        }

        /* Windows AcceptEx 异步接收连接完成等待 */
        {
            if (is_call_to(expr->await_expr.expr, "Socket", "AcceptOv") &&
                expr->await_expr.expr->call.args.count == 1) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3037,
                        "await Socket.AcceptOv is only supported inside an async method");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef fd = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(fd) != di64)
                    fd = LLVMBuildIntCast2(g->builder, fd, di64, 1, "fd64");
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                LLVMValueRef out_fd = get_async_result_ptr(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_io_accept_co_type,
                    g->rt_io_accept_co, (LLVMValueRef[]){ fd, self_i8,
                        g->current_async_resume_fn, out_fd }, 4, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch,
                    LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                return LLVMBuildLoad2(g->builder, di64, get_async_result_ptr(g), "acceptfd");
            }
        }

        /* 异步 DNS 解析等待 */
        {
            if (is_call_to(expr->await_expr.expr, "Socket", "ResolveAsync") &&
                expr->await_expr.expr->call.args.count == 1) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3038,
                        "await Socket.ResolveAsync is only supported inside an async method");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef host = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(host) != di8ptr)
                    host = LLVMBuildBitCast(g->builder, host, di8ptr, "dns.host");
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                /* 套接字结果槽位指针绑定至反应堆事件 */
                LLVMValueRef res_gep = get_async_result_ptr(g);
                LLVMValueRef out32 = LLVMBuildBitCast(g->builder, res_gep,
                    LLVMPointerType(di32, 0), "self.dnsout");
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_io_resolve_co_type,
                    g->rt_io_resolve_co, (LLVMValueRef[]){ host, self_i8,
                        g->current_async_resume_fn, out32 }, 4, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch,
                    LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                LLVMValueRef res_slot = LLVMBuildBitCast(g->builder,
                    get_async_result_ptr(g),
                    LLVMPointerType(di32, 0), "self.dnsout2");
                return LLVMBuildLoad2(g->builder, di32, res_slot, "dnsaddr");
            }
        }

        /* 异步套接字地址解析挂起 */
        {
            if (is_call_to(expr->await_expr.expr, "Socket", "ResolveSockAddr") &&
                expr->await_expr.expr->call.args.count == 4) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3039,
                        "await Socket.ResolveSockAddr is only supported inside an async method");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef name = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(name) != di8ptr)
                    name = LLVMBuildBitCast(g->builder, name, di8ptr, "sa.name");
                LLVMValueRef port = emit_expr(g, expr->await_expr.expr->call.args.items[1], locals);
                if (LLVMTypeOf(port) != di32)
                    port = LLVMBuildIntCast2(g->builder, port, di32, 1, "sa.port");
                LLVMValueRef buf = emit_expr(g, expr->await_expr.expr->call.args.items[2], locals);
                if (LLVMTypeOf(buf) != di8ptr)
                    buf = LLVMBuildBitCast(g->builder, buf, di8ptr, "sa.buf");
                LLVMValueRef cap = emit_expr(g, expr->await_expr.expr->call.args.items[3], locals);
                if (LLVMTypeOf(cap) != di32)
                    cap = LLVMBuildIntCast2(g->builder, cap, di32, 1, "sa.cap");
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                LLVMValueRef res_gep = get_async_result_ptr(g);
                LLVMValueRef out32 = LLVMBuildBitCast(g->builder, res_gep,
                    LLVMPointerType(di32, 0), "self.saout");
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_io_resolve_sa_co_type,
                    g->rt_io_resolve_sa_co, (LLVMValueRef[]){ name, port, buf,
                        cap, self_i8, g->current_async_resume_fn, out32 }, 7, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch,
                    LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                LLVMValueRef res_slot = LLVMBuildBitCast(g->builder,
                    get_async_result_ptr(g),
                    LLVMPointerType(di32, 0), "self.saout2");
                return LLVMBuildLoad2(g->builder, di32, res_slot, "salen");
            }
        }

        {
            zan_symbol_t *native = direct_extern_method(g, expr->await_expr.expr);
            if (native)
                return emit_await_blocking_extern(g, expr, locals, native);
        }

        /* await <call>: 等待异步方法子协程执行完成 */
        /* await 挂起点临时栈深度记录，供异常展开清理 */
        LLVMValueRef aw_tmp_mark = LLVMBuildLoad2(g->builder,
            LLVMInt32TypeInContext(g->ctx), get_eh_tmp_top_global(g),
            "aw.tmpmark");
        /* await 一个**非异步**的值：原样返回，不进协程帧那套。
         *
         * 例如 `await someSyncDelegate()`——同步委托的返回值是**非异步**的，
         * 它不是子协程帧，所以应认作普通值。判错就是读野指针：普通值
         * 往往也是 i8 指针（字符串就是），与子协程帧的指针同型，被当成
         * 帧去读就会读出面元指针——静默的垃圾值，不报错。 */
        if (!await_target_is_async(g, expr->await_expr.expr, locals)) {
            return emit_expr(g, expr->await_expr.expr, locals);
        }
        LLVMValueRef sub = emit_expr(g, expr->await_expr.expr, locals);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef hdr = g->co_header_type;

        LLVMValueRef sub_resume = NULL;
        if (LLVMIsACallInst(sub)) {
            /* 直接异步调用：已知被调用函数入口，直连 ramp 与 resume 符号 */
            LLVMValueRef callee = LLVMGetCalledValue(sub);
            LLVMValueRef callee_fn = callee ? LLVMIsAFunction(callee) : NULL;
            if (callee_fn) {
                size_t nl = 0;
                const char *cn = LLVMGetValueName2(callee_fn, &nl);
                if (cn && nl > 0 && nl < 240) {
                    char rn[256];
                    memcpy(rn, cn, nl);
                    memcpy(rn + nl, "$resume", 8);
                    sub_resume = LLVMGetNamedFunction(g->mod, rn);
                }
            }
        }

        /* 间接异步调用：通过虚方法表或委托分发 resume 入口 */
        if (!sub_resume &&
            LLVMGetTypeKind(LLVMTypeOf(sub)) == LLVMPointerTypeKind) {
            LLVMValueRef sub_hdr = (LLVMTypeOf(sub) == i8ptr) ? sub :
                LLVMBuildBitCast(g->builder, sub, i8ptr, "sub.hdr");
            LLVMValueRef ssp = LLVMBuildStructGEP2(g->builder, hdr, sub_hdr,
                ASYNC_FRAME_SELF_STEP, "sub.selfstep.p");
            sub_resume = LLVMBuildLoad2(g->builder, g->co_step_ptr, ssp, "sub.selfstep");
        }

        if (sub_resume && LLVMGetTypeKind(LLVMTypeOf(sub)) == LLVMPointerTypeKind) {
            LLVMValueRef sub_i8 = (LLVMTypeOf(sub) == i8ptr) ? sub :
                LLVMBuildBitCast(g->builder, sub, i8ptr, "sub");

            if (g->current_async_frame && g->current_async_switch) {
                int k = g->current_async_next_state++;
                LLVMTypeRef ptr_int_ty = g->target_is_wasm ? i32 : i64;
                LLVMValueRef fn = g->current_async_resume_fn;

                LLVMBasicBlockRef prep_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "await.prep");
                LLVMBasicBlockRef suspend_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "await.suspend");
                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.resume");
                LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "await.cont");

                LLVMAddCase(g->current_async_switch, LLVMConstInt(i32, (unsigned)k, 0), rk);

                /* 快路径探测：子协程若已同步完成则直接内联提取结果，避免调度开销 */
                LLVMValueRef done_p = LLVMBuildStructGEP2(g->builder, hdr, sub_i8,
                    ASYNC_FRAME_DONE, "sub.done.p");
                LLVMValueRef is_done = LLVMBuildLoad2(g->builder, i32, done_p, "sub.is_done");
                LLVMSetOrdering(is_done, LLVMAtomicOrderingAcquire);
                LLVMValueRef fast_cond = zan_icmp(g->builder, LLVMIntNE, is_done,
                    LLVMConstInt(i32, 0, 0), "sub.already_done");
                LLVMBasicBlockRef probe_bb = LLVMGetInsertBlock(g->builder);
                LLVMBuildCondBr(g->builder, fast_cond, cont_bb, prep_bb);

                LLVMPositionBuilderAtEnd(g->builder, prep_bb);
                zan_store_fit(g, g->current_async_resume_fn,
                    LLVMBuildStructGEP2(g->builder, hdr, sub_i8, ASYNC_FRAME_AWAITER_STEP, "sub.aws"));
                LLVMValueRef aw_ptr = LLVMBuildStructGEP2(g->builder, hdr, sub_i8,
                    ASYNC_FRAME_AWAITER, "sub.aw");
                LLVMTypeRef aw_target_pty = LLVMPointerType(ptr_int_ty, 0);
                LLVMValueRef aw_iptr = (LLVMTypeOf(aw_ptr) == aw_target_pty) ? aw_ptr :
                    LLVMBuildBitCast(g->builder, aw_ptr, aw_target_pty, "sub.aw.iptr");
                LLVMValueRef self_int = get_async_self_int(g, ptr_int_ty);
                LLVMValueRef cas_res = LLVMBuildAtomicCmpXchg(g->builder, aw_iptr,
                    LLVMConstInt(ptr_int_ty, 0, 0), self_int,
                    LLVMAtomicOrderingSequentiallyConsistent,
                    LLVMAtomicOrderingSequentiallyConsistent, 0);
                LLVMValueRef won = LLVMBuildExtractValue(g->builder, cas_res, 1, "cas.won");
                LLVMBuildCondBr(g->builder, won, suspend_bb, cont_bb);

                LLVMPositionBuilderAtEnd(g->builder, suspend_bb);
                LLVMValueRef sub_slot = get_async_sub_slot_ptr(g);
                zan_store_fit(g, sub_i8, sub_slot);
                zan_store_fit(g, sub_i8, get_async_child_ptr(g));
                zan_store_fit(g, LLVMConstInt(i32, (unsigned)k, 0), get_async_state_ptr(g));
                LLVMValueRef sched_args[] = { sub_i8, sub_resume };
                zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMPositionBuilderAtEnd(g->builder, rk);
                LLVMValueRef sub_rl = LLVMBuildLoad2(g->builder, i8ptr, sub_slot, "sub.rl");
                LLVMBuildBr(g->builder, cont_bb);

                LLVMPositionBuilderAtEnd(g->builder, cont_bb);
                LLVMValueRef completed_sub = LLVMBuildPhi(g->builder, i8ptr, "sub.completed");
                LLVMValueRef sub_vals[] = { sub_i8, sub_i8, sub_rl };
                LLVMBasicBlockRef sub_bbs[] = { probe_bb, prep_bb, rk };
                LLVMAddIncoming(completed_sub, sub_vals, sub_bbs, 3);
                emit_async_check_sub_exc(g, completed_sub, NULL);
                LLVMValueRef rptr = LLVMBuildStructGEP2(g->builder, hdr, completed_sub,
                    ASYNC_FRAME_RESULT, "sub.result");
                LLVMValueRef awres = LLVMBuildLoad2(g->builder, i64, rptr, "awres");
                LLVMValueRef val = coerce_await_result(g, expr, awres, locals);
                zan_emit_frame_free(g, completed_sub);
                if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMVoidTypeKind)
                    return LLVMConstInt(i64, 0, 0);
                return val;
            }

            /* 根驱动调度循环（非异步调用方）：驱动调度器推进协程执行至完成 */
            LLVMValueRef done_p = LLVMBuildStructGEP2(g->builder, hdr, sub_i8,
                ASYNC_FRAME_DONE, "sub.done.p");
            LLVMValueRef is_done = LLVMBuildLoad2(g->builder, i32, done_p, "sub.is_done");
            LLVMSetOrdering(is_done, LLVMAtomicOrderingAcquire);
            LLVMValueRef need_pump = zan_icmp(g->builder, LLVMIntEQ, is_done,
                LLVMConstInt(i32, 0, 0), "sub.need_pump");
            LLVMValueRef fn = g->current_fn;
            LLVMBasicBlockRef pump_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "await.pump");
            LLVMBasicBlockRef fin_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "await.fin");
            LLVMBuildCondBr(g->builder, need_pump, pump_bb, fin_bb);

            LLVMPositionBuilderAtEnd(g->builder, pump_bb);
            LLVMValueRef sched_args[] = { sub_i8, sub_resume };
            zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
            /* 单协程调度循环：轮询驱动直至当前协程执行完成 */
            zan_call2(g->builder, g->rt_co_sched_run_until_type,
                g->rt_co_sched_run_until, (LLVMValueRef[]){ done_p }, 1, "");
            LLVMBuildBr(g->builder, fin_bb);

            LLVMPositionBuilderAtEnd(g->builder, fin_bb);
            emit_async_check_sub_exc(g, sub_i8, aw_tmp_mark);
            LLVMValueRef rptr = LLVMBuildStructGEP2(g->builder, hdr, sub_i8,
                ASYNC_FRAME_RESULT, "sub.result");
            LLVMValueRef awres = LLVMBuildLoad2(g->builder, i64, rptr, "awres");
            LLVMValueRef val = coerce_await_result(g, expr, awres, locals);
            zan_emit_frame_free(g, sub_i8);
            return val;
        }

        if (LLVMGetTypeKind(LLVMTypeOf(sub)) == LLVMPointerTypeKind) {
            LLVMValueRef task_ptr = LLVMBuildBitCast(g->builder, sub,
                LLVMPointerType(g->task_struct_type, 0), "taskp");
            LLVMBasicBlockRef poll_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "aw.poll");
            LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "aw.done");
            LLVMBuildBr(g->builder, poll_bb);
            LLVMPositionBuilderAtEnd(g->builder, poll_bb);
            LLVMValueRef comp_ptr = LLVMBuildStructGEP2(g->builder, g->task_struct_type, task_ptr, 0, "comp");
            LLVMValueRef comp = LLVMBuildLoad2(g->builder, i64, comp_ptr, "cv");
            LLVMValueRef is_done = zan_icmp(g->builder, LLVMIntNE, comp, LLVMConstInt(i64, 0, 0), "done");
            LLVMBuildCondBr(g->builder, is_done, done_bb, poll_bb);
            LLVMPositionBuilderAtEnd(g->builder, done_bb);
            LLVMValueRef res_ptr = LLVMBuildStructGEP2(g->builder, g->task_struct_type, task_ptr, 1, "resp");
            return coerce_await_result(g, expr,
                LLVMBuildLoad2(g->builder, i64, res_ptr, "awres"), locals);
        }
        return sub;
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

/* 用户自定义隐式/显式类型转换运算符降解 */
static zan_symbol_t *find_conversion_method(zan_irgen_t *g, zan_symbol_t *sym,
                                            zan_type_t *from_type,
                                            zan_type_t *to_type,
                                            const char *op_name) {
    if (!sym) return NULL;
    size_t olen = strlen(op_name);
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (!m || m->kind != SYM_METHOD || !(m->modifiers & MOD_STATIC)) continue;
        if (!m->decl || m->decl->kind != AST_METHOD_DECL) continue;
        if ((size_t)m->name.len != olen ||
            memcmp(m->name.str, op_name, olen) != 0) continue;
        zan_type_t *rt = zan_binder_resolve_type(g->binder,
            m->decl->method_decl.return_type);
        if (!rt || !types_concrete_equal(rt, to_type)) continue;
        zan_ast_list_t *ps = &m->decl->method_decl.params;
        if (ps->count != 1 || !ps->items[0] || ps->items[0]->kind != AST_PARAM)
            continue;
        zan_type_t *pt = zan_binder_resolve_type(g->binder,
            ps->items[0]->param.type);
        if (pt && types_concrete_equal(pt, from_type)) return m;
    }
    return NULL;
}

/* 合成 new 节点作为所有权标记 */
static zan_ast_node_t *owned_rhs_marker(zan_irgen_t *g, zan_loc_t loc) {
    return zan_ast_new(g->arena, AST_NEW_EXPR, loc);
}

static zan_symbol_t *find_user_conversion(zan_irgen_t *g, zan_type_t *from_type,
                                          zan_type_t *to_type,
                                          const char *op_name) {
    if (!from_type || !to_type) return NULL;
    if (from_type->kind == TYPE_CLASS || from_type->kind == TYPE_STRUCT) {
        zan_symbol_t *m = find_conversion_method(g, from_type->sym, from_type,
                                                 to_type, op_name);
        if (m) return m;
    }
    if (to_type->kind == TYPE_CLASS || to_type->kind == TYPE_STRUCT) {
        zan_symbol_t *m = find_conversion_method(g, to_type->sym, from_type,
                                                 to_type, op_name);
        if (m) return m;
    }
    return NULL;
}

static LLVMValueRef emit_user_conversion(zan_irgen_t *g, zan_type_t *from_type,
                                         zan_type_t *to_type, const char *op_name,
                                         LLVMValueRef val, zan_ast_node_t *val_expr,
                                         local_scope_t *locals) {
    zan_symbol_t *op = find_user_conversion(g, from_type, to_type, op_name);
    if (!op) return val;
    int fi = irgen_find_function(g, op);
    if (fi < 0) return val;
    LLVMTypeRef mft = g->functions[fi].fn_type;
    LLVMValueRef mfn = route_generic_method(g, from_type, op,
        g->functions[fi].fn, mft, &mft);
    LLVMValueRef arg = val;
    zan_type_t *p0 = method_param_type_at(g, op, 0, val_expr, val_expr, locals);
    if (p0) {
        LLVMTypeRef p0t = map_type(g, p0);
        if (LLVMTypeOf(arg) != p0t) arg = coerce_int_to(g, arg, p0t);
    }
    return zan_call2(g->builder, mft, mfn, &arg, 1, "uc");
}

/* (nint)SomeMethod 原生回调函数指针转换 */
static LLVMValueRef emit_raw_fn_for_cb_cast(zan_irgen_t *g, zan_ast_node_t *e,
                                            local_scope_t *locals) {
    zan_symbol_t *msym = NULL;
    if (e->kind == AST_IDENTIFIER && g->current_type_sym) {
        msym = get_method_sym(g->current_type_sym, e->ident.name);
    } else if (e->kind == AST_MEMBER_ACCESS &&
               e->member.object->kind == AST_IDENTIFIER &&
               !local_find(locals, e->member.object->ident.name)) {
        zan_symbol_t *cs = zan_binder_lookup(g->binder, e->member.object->ident.name);
        if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT))
            msym = get_method_sym(cs, e->member.name);
    }
    if (msym && msym->decl && msym->decl->kind == AST_METHOD_DECL &&
        (msym->decl->method_decl.modifiers & MOD_STATIC) != 0) {
        for (int fi = irgen_find_function(g, msym); fi >= 0; fi = -1)
            if (g->functions[fi].sym == msym)
                return g->functions[fi].fn;
    }
    if (e->kind == AST_IDENTIFIER && !local_find(locals, e->ident.name)) {
        char nbuf[256];
        size_t nl = e->ident.name.len < 255 ? e->ident.name.len : 255;
        memcpy(nbuf, e->ident.name.str, nl);
        nbuf[nl] = '\0';
        LLVMValueRef gfn = LLVMGetNamedFunction(g->mod, nbuf);
        if (gfn && LLVMIsAFunction(gfn)) return gfn;
    }
    return NULL;
}

/* (nint)Method 回调函数指针跨语言传递给宿主 C 代码 */
static LLVMValueRef emit_wasm_cb_thunk(zan_irgen_t *g, LLVMValueRef fn) {
    char name[300];
    snprintf(name, sizeof(name), "__zan_cb_thunk.%s", LLVMGetValueName(fn));
    LLVMValueRef existing = LLVMGetNamedFunction(g->mod, name);
    if (existing && LLVMIsAFunction(existing)) return existing;
    LLVMTypeRef i32T = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64T = LLVMInt64TypeInContext(g->ctx);
    unsigned nparams = LLVMCountParams(fn);
    LLVMTypeRef *param_types = (LLVMTypeRef *)malloc((nparams > 0 ? nparams : 1) * sizeof(LLVMTypeRef));
    for (unsigned i = 0; i < nparams; i++) {
        param_types[i] = i32T;
    }
    LLVMTypeRef ret_ty = LLVMGetReturnType(LLVMGlobalGetValueType(fn));
    LLVMTypeRef thunk_ty = LLVMFunctionType(ret_ty, param_types, nparams, 0);
    free(param_types);

    LLVMValueRef thunk = LLVMAddFunction(g->mod, name, thunk_ty);
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, thunk, "entry");
    LLVMPositionBuilderAtEnd(g->builder, bb);

    LLVMTypeRef fn_ty = LLVMGlobalGetValueType(fn);
    LLVMValueRef *call_args = NULL;
    if (nparams > 0) {
        call_args = (LLVMValueRef *)malloc(nparams * sizeof(LLVMValueRef));
        for (unsigned i = 0; i < nparams; i++) {
            LLVMValueRef p = LLVMGetParam(thunk, i);
            LLVMTypeRef target_param_ty = LLVMTypeOf(LLVMGetParam(fn, i));
            if (LLVMGetTypeKind(target_param_ty) == LLVMIntegerTypeKind) {
                unsigned w = LLVMGetIntTypeWidth(target_param_ty);
                if (w == 64) {
                    call_args[i] = LLVMBuildZExt(g->builder, p, i64T, "arg64");
                } else if (w == 32) {
                    call_args[i] = p;
                } else if (w < 32) {
                    call_args[i] = LLVMBuildTrunc(g->builder, p, target_param_ty, "argtrunc");
                } else {
                    call_args[i] = LLVMBuildZExt(g->builder, p, target_param_ty, "argext");
                }
            } else if (LLVMGetTypeKind(target_param_ty) == LLVMPointerTypeKind) {
                call_args[i] = LLVMBuildIntToPtr(g->builder, p, target_param_ty, "argptr");
            } else {
                call_args[i] = p;
            }
        }
    }
    LLVMValueRef ret_val = LLVMBuildCall2(g->builder, fn_ty, fn, call_args, nparams, "");
    if (call_args) free(call_args);

    if (LLVMGetTypeKind(ret_ty) == LLVMVoidTypeKind) {
        LLVMBuildRetVoid(g->builder);
    } else {
        LLVMBuildRet(g->builder, ret_val);
    }
    LLVMPositionBuilderAtEnd(g->builder, saved_bb);
    return thunk;
}

static int type_is_or_derives(zan_type_t *s, zan_type_t *t);
static LLVMValueRef emit_runtime_is_check_name(zan_irgen_t *g, LLVMValueRef x,
                                               zan_type_t *tt);

static LLVMValueRef emit_runtime_is_check_name(zan_irgen_t *g, LLVMValueRef x,
                                               zan_type_t *tt);

/* A454 装箱三助手（定义在 emit_runtime_is_check_name 之后） */
static bool irgen_boxed_eligible_struct(zan_irgen_t *g, zan_type_t *t);
static LLVMValueRef emit_box_struct(zan_irgen_t *g, zan_type_t *st,
                                    zan_ast_node_t *src_expr,
                                    LLVMValueRef val, local_scope_t *locals);
static LLVMValueRef emit_unbox_struct(zan_irgen_t *g, zan_type_t *tt,
                                      LLVMValueRef val, zan_ast_node_t *expr);

static LLVMValueRef emit_expr_cast_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        zan_type_t *tt0 = resolve_type_ctx(g, expr->cast.type);
        /* 传递裸函数指针供外部原生代码调用 */
        if (tt0 && expr->cast.expr &&
            (expr->cast.expr->kind == AST_IDENTIFIER ||
             expr->cast.expr->kind == AST_MEMBER_ACCESS) &&
            tt0->kind != TYPE_DELEGATE) {
            LLVMTypeRef tgt0 = map_type(g, tt0);
            if (LLVMGetTypeKind(tgt0) == LLVMIntegerTypeKind) {
                LLVMValueRef raw = emit_raw_fn_for_cb_cast(g, expr->cast.expr, locals);
                if (raw) {
                    if (target_is_wasm32(g))
                        raw = emit_wasm_cb_thunk(g, raw);
                    return LLVMBuildPtrToInt(g->builder, raw, tgt0, "cast.fn");
                }
            }
        }
        LLVMValueRef val = emit_expr(g, expr->cast.expr, locals);
        zan_type_t *tt = resolve_type_ctx(g, expr->cast.type);
        LLVMTypeRef target = tt ? map_type(g, tt) : LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef src = LLVMTypeOf(val);
        {
            zan_type_t *st = infer_expr_type(g, expr->cast.expr, locals);
            if (st && find_user_conversion(g, st, tt, "op_explicit"))
                return emit_user_conversion(g, st, tt, "op_explicit", val,
                                            expr->cast.expr, locals);
            /* `(object)` 对值形状的分流（A454）：无托管字段的 struct 走真
             * 装箱（分配带头部堆对象、写载荷、site 注册三件事都在
             * emit_box_struct）；其余值形状仍报错兜住泛型特化（类型参数
             * 在 checker 阶段还看不出来）——硬转只会把整数值当成对象指针
             * （下面走 IntToPtr），运行期拿野指针、退出时 ARC 回收再崩。 */
            if (tt && tt->kind == TYPE_OBJECT && st) {
                if (irgen_boxed_eligible_struct(g, st))
                    return emit_box_struct(g, st, expr, val, locals);
                if (irgen_type_is_value_kind(st))
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3040,
                              "cannot convert '%s' to 'object': value types "
                              "cannot be boxed (Zan has no boxing facility)",
                              st->name.str ? st->name.str : "?");
            }
            /* 拆箱（A454）：object → 装箱过的结构体。来源不限于 OBJECT
             * 形状（string / 接口 / 引用类都可能是箱的实际内容）；tag
             * 检查走 `is` 的描述符名表通路（对 string magic 亦正确），
             * 失败按 A509 软失败（stderr + 零值继续）。 */
            if (tt && tt->kind == TYPE_STRUCT && st &&
                st->kind != TYPE_STRUCT && st->kind != TYPE_NULLABLE &&
                LLVMGetTypeKind(src) == LLVMPointerTypeKind &&
                irgen_boxed_eligible_struct(g, tt))
                return emit_unbox_struct(g, tt, val, expr);
        }
        if (llvm_is_nullable(src) && !llvm_is_nullable(target)) {
            emit_runtime_check(g,
                LLVMBuildNot(g->builder, nullable_has_value(g, val), "nv.novalue"),
                expr->loc, "Nullable object must have a value");
            val = nullable_get_payload(g, val);
            src = LLVMTypeOf(val);
        } else if (llvm_is_nullable(target) && src != target) {
            return coerce_int_to(g, val, target);
        }
        /* 无符号整型目标转换保持 64 位寄存器表示 */
        if (tt && LLVMGetTypeKind(src) == LLVMIntegerTypeKind &&
            LLVMGetIntTypeWidth(src) < 64 &&
            (tt->kind == TYPE_UINT || tt->kind == TYPE_USHORT ||
             tt->kind == TYPE_SBYTE || tt->kind == TYPE_ULONG)) {
            val = zan_iwiden(g->builder, val, LLVMInt64TypeInContext(g->ctx));
            src = LLVMTypeOf(val);
        }
        if (tt && LLVMGetTypeKind(src) == LLVMIntegerTypeKind &&
            LLVMGetIntTypeWidth(src) == 64) {
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            switch (tt->kind) {
            case TYPE_UINT:
                return zan_and(g->builder, val,
                    LLVMConstInt(i64t, 0xFFFFFFFFull, 0), "cast.u32");
            case TYPE_USHORT:
                return zan_and(g->builder, val,
                    LLVMConstInt(i64t, 0xFFFFull, 0), "cast.u16");
            case TYPE_SBYTE: {
                LLVMValueRef sh = LLVMConstInt(i64t, 56, 0);
                LLVMValueRef up = zan_shl(g->builder, val, sh, "cast.i8.l");
                return zan_ashr(g->builder, up, sh, "cast.i8");
            }
            case TYPE_ULONG:
                return val;
            default:
                break;
            }
        }
        /* 引用下转的运行时类型检查（TASKS.md A509）：引用→引用曾直接返回
         * 原值——失败的下转把 string 指针当 User 用（静默类型混淆）。
         * 必须放在 `src == target` 早退之前：LLVM 不透明指针下所有指针
         * 同型（ptr），那道等值检查对引用恒真。
         * C# 抛 InvalidCastException（可 catch）；catchable 通道落地前
         * （A507 族）用软诊断 + 安全值 null：stderr 一次、值变 null、继续，
         * --strict-runtime 下 exit 70。静态可证安全的上转/同型/object 不查。 */
        if (LLVMGetTypeKind(src) == LLVMPointerTypeKind &&
            LLVMGetTypeKind(target) == LLVMPointerTypeKind && tt) {
            bool ref_target = tt->kind == TYPE_CLASS ||
                tt->kind == TYPE_INTERFACE || tt->kind == TYPE_STRING ||
                tt->kind == TYPE_ARRAY || tt->kind == TYPE_OBJECT;
            if (ref_target) {
                zan_type_t *st2 = infer_expr_type(g, expr->cast.expr, locals);
                bool static_ok = !st2 || st2->kind == TYPE_ERROR ||
                    types_equal(st2, tt) || type_is_or_derives(st2, tt) ||
                    tt->kind == TYPE_OBJECT;
                if (!static_ok) {
                    LLVMValueRef cfn = LLVMGetBasicBlockParent(
                        LLVMGetInsertBlock(g->builder));
                    LLVMValueRef ok = emit_runtime_is_check_name(g, val, tt);
                    LLVMBasicBlockRef cast_ok = LLVMGetInsertBlock(g->builder);
                    LLVMBasicBlockRef cast_bad = LLVMAppendBasicBlockInContext(
                        g->ctx, cfn, "cast.bad");
                    LLVMBasicBlockRef cast_m = LLVMAppendBasicBlockInContext(
                        g->ctx, cfn, "cast.m");
                    LLVMBuildCondBr(g->builder, ok, cast_m, cast_bad);
                    LLVMPositionBuilderAtEnd(g->builder, cast_bad);
                    emit_runtime_check(g,
                        LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0),
                        expr->loc,
                        "InvalidCastException: object is not an instance of the target type");
                    LLVMBasicBlockRef cast_bad_end =
                        LLVMGetInsertBlock(g->builder);
                    LLVMBuildBr(g->builder, cast_m);
                    LLVMPositionBuilderAtEnd(g->builder, cast_m);
                    LLVMValueRef rphi = LLVMBuildPhi(g->builder, target,
                                                     "cast.r");
                    LLVMValueRef rvals[] = { val, LLVMConstNull(target) };
                    LLVMBasicBlockRef rbbs[] = { cast_ok, cast_bad_end };
                    LLVMAddIncoming(rphi, rvals, rbbs, 2);
                    return rphi;
                }
            }
        }

        if (src == target) return val;

        /* decimal 显式强转支持（整型与浮点型互转） */
        if (target == get_decimal_type(g)) {
            if (LLVMGetTypeKind(src) == LLVMIntegerTypeKind) {
                return coerce_to_decimal(g, val);
            }
            if (LLVMGetTypeKind(src) == LLVMDoubleTypeKind ||
                LLVMGetTypeKind(src) == LLVMFloatTypeKind) {
                if (LLVMGetTypeKind(src) == LLVMFloatTypeKind) {
                    val = LLVMBuildFPExt(g->builder, val, LLVMDoubleTypeInContext(g->ctx), "dec.fpext");
                }
                LLVMValueRef slot = emit_entry_alloca(g, target, "dec.fslot");
                LLVMTypeRef ptr_ty = LLVMPointerType(target, 0);
                LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                    (LLVMTypeRef[]){ LLVMDoubleTypeInContext(g->ctx), ptr_ty }, 2, 0);
                LLVMValueRef fn = get_libc_fn(g, "__zan_decimal_from_double", fn_ty);
                zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){ val, slot }, 2, "");
                return LLVMBuildLoad2(g->builder, target, slot, "dec.fval");
            }
        }
        if (src == get_decimal_type(g)) {
            if (LLVMGetTypeKind(target) == LLVMIntegerTypeKind) {
                LLVMValueRef slot = emit_entry_alloca(g, src, "dec.tslot");
                LLVMBuildStore(g->builder, val, slot);
                LLVMTypeRef ptr_ty = LLVMPointerType(src, 0);
                LLVMTypeRef fn_ty = LLVMFunctionType(LLVMInt64TypeInContext(g->ctx),
                    (LLVMTypeRef[]){ ptr_ty }, 1, 0);
                LLVMValueRef fn = get_libc_fn(g, "__zan_decimal_to_int64", fn_ty);
                LLVMValueRef i64_val = zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){ slot }, 1, "dec.i64");
                if (LLVMGetIntTypeWidth(target) < 64) {
                    return LLVMBuildTrunc(g->builder, i64_val, target, "dec.trunc");
                }
                return i64_val;
            }
            if (LLVMGetTypeKind(target) == LLVMDoubleTypeKind ||
                LLVMGetTypeKind(target) == LLVMFloatTypeKind) {
                LLVMValueRef slot = emit_entry_alloca(g, src, "dec.tslot");
                LLVMBuildStore(g->builder, val, slot);
                LLVMTypeRef ptr_ty = LLVMPointerType(src, 0);
                LLVMTypeRef fn_ty = LLVMFunctionType(LLVMDoubleTypeInContext(g->ctx),
                    (LLVMTypeRef[]){ ptr_ty }, 1, 0);
                LLVMValueRef fn = get_libc_fn(g, "__zan_decimal_to_double", fn_ty);
                LLVMValueRef dbl = zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){ slot }, 1, "dec.dbl");
                if (LLVMGetTypeKind(target) == LLVMFloatTypeKind) {
                    return LLVMBuildFPTrunc(g->builder, dbl, target, "dec.flt");
                }
                return dbl;
            }
        }

        LLVMTypeKind sk = LLVMGetTypeKind(src);
        LLVMTypeKind tk = LLVMGetTypeKind(target);
        /* 地址与委托互转：(WndProc)addr 动态转换运行时回调 */
        if (sk == LLVMIntegerTypeKind && tk == LLVMPointerTypeKind)
            return LLVMBuildIntToPtr(g->builder, val, target, "cast.p");
        if (sk == LLVMPointerTypeKind && tk == LLVMIntegerTypeKind)
            return LLVMBuildPtrToInt(g->builder, val, target, "cast.a");
        bool src_fp = (sk == LLVMDoubleTypeKind || sk == LLVMFloatTypeKind);
        bool tgt_fp = (tk == LLVMDoubleTypeKind || tk == LLVMFloatTypeKind);
        if (sk == LLVMIntegerTypeKind && tk == LLVMIntegerTypeKind) {
            unsigned sw = LLVMGetIntTypeWidth(src);
            unsigned tw = LLVMGetIntTypeWidth(target);
            if (tw < sw) return LLVMBuildTrunc(g->builder, val, target, "cast");
            if (tw > sw) return zan_iwiden(g->builder, val, target);
            return val;
        }
        if (src_fp && tk == LLVMIntegerTypeKind)
            return LLVMBuildFPToSI(g->builder, val, target, "cast");
        if (sk == LLVMIntegerTypeKind && tgt_fp)
            return LLVMBuildSIToFP(g->builder, val, target, "cast");
        if (src_fp && tgt_fp) {
            if (sk == LLVMDoubleTypeKind && tk == LLVMFloatTypeKind)
                return LLVMBuildFPTrunc(g->builder, val, target, "cast");
            if (sk == LLVMFloatTypeKind && tk == LLVMDoubleTypeKind)
                return LLVMBuildFPExt(g->builder, val, target, "cast");
        }
        return val;
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

static int type_is_or_derives(zan_type_t *s, zan_type_t *t) {
    if (!s || !t) return 0;
    if (types_equal(s, t)) return 1;
    if (s->kind != TYPE_CLASS || t->kind != TYPE_CLASS) return 0;
    zan_symbol_t *cur = s->sym;
    while (cur && cur->type && cur->type->base_type &&
           cur->type->base_type->sym) {
        cur = cur->type->base_type->sym;
        if (cur == t->sym) return 1;
    }
    return 0;
}

/* 运行时 x is T 严格祖先类型继承检查 */
static LLVMValueRef emit_runtime_is_check(zan_irgen_t *g, LLVMValueRef x,
                                          const char *tname);

/* 运行时 x is T 类型检查辅助封装 */
static LLVMValueRef emit_runtime_is_check_name(zan_irgen_t *g, LLVMValueRef x,
                                               zan_type_t *tt) {
    char tbuf[320];
    const char *ns = NULL;
    int nlen = 0;
    if (tt && tt->name.str) {
        ns = tt->name.str;
        nlen = (int)tt->name.len;
    } else if (tt && tt->sym && tt->sym->name.str) {
        ns = tt->sym->name.str;
        nlen = (int)tt->sym->name.len;
    }
    if (nlen > 319) nlen = 319;
    if (nlen > 0) memcpy(tbuf, ns, (size_t)nlen);
    tbuf[nlen] = 0;
    return emit_runtime_is_check(g, x, tbuf);
}

static LLVMValueRef emit_runtime_is_check(zan_irgen_t *g, LLVMValueRef x,
                                          const char *tname) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
    if (!g->desc_hdr && !g->g_site_tynames) return LLVMConstInt(i1, 0, 0);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef false_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.f");
    LLVMBasicBlockRef true_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.t");
    LLVMBasicBlockRef merge = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.m");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.h");
    LLVMBasicBlockRef str_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.s");
    LLVMBasicBlockRef oob_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.o");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.l");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.b");
    LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.n");
    (void)cur;

    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, x,
        LLVMConstNull(LLVMTypeOf(x)), "is.nul");
    LLVMBuildCondBr(g->builder, isnull, false_bb, head);

    /* 对象头部 -8 处分配点标识字（支持字符串/对象运行时类型识别） */
    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef tstr = zan_irgen_intern_string(g, tname);
    LLVMValueRef neg8 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
    LLVMValueRef sptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
        x, &neg8, 1, "is.sptr");
    LLVMValueRef site = LLVMBuildLoad2(g->builder, i64,
        LLVMBuildBitCast(g->builder, sptr, LLVMPointerType(i64, 0), "is.sp"),
        "is.site");
    LLVMValueRef isstr = zan_hdr_is_string(g, site, "is.str");
    LLVMBuildCondBr(g->builder, isstr, str_bb, oob_bb);

    LLVMPositionBuilderAtEnd(g->builder, str_bb);
    LLVMValueRef ststr = zan_irgen_intern_string(g, "string");
    LLVMTypeRef sstrcmp_ty = LLVMFunctionType(i32,
        (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMValueRef sstrcmp_fn = get_libc_fn(g, "strcmp", sstrcmp_ty);
    LLVMValueRef scmp = zan_call2(g->builder, sstrcmp_ty, sstrcmp_fn,
        (LLVMValueRef[]){ ststr, tstr }, 2, "is.scmp");
    LLVMValueRef seq = zan_icmp(g->builder, LLVMIntEQ, scmp,
        LLVMConstInt(i32, 0, 0), "is.seq");
    LLVMBuildCondBr(g->builder, seq, true_bb, false_bb);

    /* 越界分配点索引判定为非目标类型 */
    LLVMPositionBuilderAtEnd(g->builder, oob_bb);
    LLVMValueRef oob;
    if (!g->desc_hdr) {
        LLVMValueRef bound = LLVMBuildLoad2(g->builder, i64,
            g->g_site_count, "is.bound");
        oob = zan_or(g->builder,
            zan_icmp(g->builder, LLVMIntSLT, site, LLVMConstInt(i64, 0, 0), "is.neg"),
            zan_icmp(g->builder, LLVMIntSGE, site, bound, "is.oob"),
            "is.oob2");
    } else {
        /* 描述符模式：头部字作为元数据记录指针 */
        oob = zan_or(g->builder,
            zan_or(g->builder,
                zan_icmp(g->builder, LLVMIntEQ, site, LLVMConstInt(i64, 0, 0),
                         "is.nodsc"),
                zan_icmp(g->builder, LLVMIntEQ, site,
                         LLVMConstInt(i64, ZAN_ARRAY_MAGIC, 0), "is.arrdsc"),
                "is.notdsc"),
            zan_icmp(g->builder, LLVMIntULT, site, LLVMConstInt(i64, 4096, 0),
                     "is.tinydsc"),
            "is.oob2");
    }
    LLVMBuildCondBr(g->builder, oob, false_bb, loop);

    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef idx = LLVMBuildPhi(g->builder, i64, "is.i");
    LLVMValueRef list;
    if (!g->desc_hdr) {
        LLVMValueRef tbl = LLVMBuildLoad2(g->builder,
            LLVMPointerType(i8ptr, 0), g->g_site_tynames, "is.tbl");
        list = LLVMBuildLoad2(g->builder, i8ptr,
            LLVMBuildGEP2(g->builder, i8ptr, tbl, &site, 1, "is.lp"),
            "is.list");
    } else {
        LLVMValueRef dp = LLVMBuildIntToPtr(g->builder, site,
            LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0), "is.dsc");
        LLVMValueRef tn_off = LLVMConstInt(i64, 8, 0);
        LLVMValueRef tn_p = LLVMBuildGEP2(g->builder,
            LLVMInt8TypeInContext(g->ctx), dp, &tn_off, 1, "is.tnp");
        list = LLVMBuildLoad2(g->builder, i8ptr, tn_p, "is.list");
    }
    LLVMValueRef lnull = zan_icmp(g->builder, LLVMIntEQ, list,
        LLVMConstNull(i8ptr), "is.ln");
    LLVMBuildCondBr(g->builder, lnull, false_bb, body);

    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef name_p = LLVMBuildGEP2(g->builder, i8ptr, list, &idx, 1, "is.np");
    LLVMValueRef name = LLVMBuildLoad2(g->builder, i8ptr, name_p, "is.name");
    LLVMValueRef nnull = zan_icmp(g->builder, LLVMIntEQ, name,
        LLVMConstNull(i8ptr), "is.nn");
    LLVMBuildCondBr(g->builder, nnull, false_bb, next);

    LLVMPositionBuilderAtEnd(g->builder, next);
    LLVMTypeRef strcmp_ty = LLVMFunctionType(i32,
        (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMValueRef strcmp_fn = get_libc_fn(g, "strcmp", strcmp_ty);
    LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty, strcmp_fn,
        (LLVMValueRef[]){ name, tstr }, 2, "is.cmp");
    LLVMValueRef eq = zan_icmp(g->builder, LLVMIntEQ, cmp,
        LLVMConstInt(i32, 0, 0), "is.eq");
    LLVMValueRef idx2 = zan_add(g->builder, idx, LLVMConstInt(i64, 1, 0), "is.i2");
    LLVMBuildCondBr(g->builder, eq, true_bb, loop);
    LLVMAddIncoming(idx, (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), idx2 },
                    (LLVMBasicBlockRef[]){ oob_bb, next }, 2);

    LLVMPositionBuilderAtEnd(g->builder, true_bb);
    LLVMBuildBr(g->builder, merge);
    LLVMPositionBuilderAtEnd(g->builder, false_bb);
    LLVMBuildBr(g->builder, merge);
    LLVMPositionBuilderAtEnd(g->builder, merge);
    LLVMValueRef res = LLVMBuildPhi(g->builder, i1, "is.res");
    LLVMAddIncoming(res, (LLVMValueRef[]){ LLVMConstInt(i1, 1, 0),
                                           LLVMConstInt(i1, 0, 0) },
                    (LLVMBasicBlockRef[]){ true_bb, false_bb }, 2);
    return res;
}

/* A454 装箱资格：用户声明的 struct、无显式布局、字段（含嵌套）不含
 * 托管引用。判定与 checker 侧 checker_struct_boxable 同口径——irgen 侧
 * 复用现成的 type_contains_collection_rc（含嵌套展开与 weak 跳过）。 */
static bool irgen_boxed_eligible_struct(zan_irgen_t *g, zan_type_t *t) {
    if (!t || t->kind != TYPE_STRUCT || !t->sym) return false;
    zan_symbol_t *sym = t->sym;
    if (!sym->decl || sym->decl->kind != AST_STRUCT_DECL) return false;
    if (decl_is_explicit_layout(sym)) return false;
    return true;
}

/* 装箱：值结构体 → 堆对象。布局契约（A556-2 基座）：首字 = 结构体
 * vtable 全局地址——接口分派按接收者首字做身份 tag 比对（类对象是
 * vptr，箱此前没有这个字，tag 是载荷前 8 字节当指针，命中不了任何
 * 候选）；载荷随之后移到 +8，拆箱与箱释放函数按 +8 取载荷
 * （emit_unbox_struct / build_class_release_body 的 SYM_STRUCT 分支）。
 * `is`/拆箱的 tag 走描述符名表（装箱 site 的 tynames 首项就是结构体名，
 * 见 zan_irgen_emit_arc_desc_init 的继承链遍历），基于头部不受载荷
 * 位移影响。头部由 zan_rt_alloc 统一布置：raw[0..7]=引用计数、
 * raw[8..15]=site 字。托管字段结构体（A556-3）同一协议：来源非 owned
 * 时对托管字段 retain，箱的释放函数按字段 walk 释放（含嵌套结构体
 * 下钻）。 */
static LLVMValueRef emit_box_struct(zan_irgen_t *g, zan_type_t *st,
                                    zan_ast_node_t *src_expr,
                                    LLVMValueRef val, local_scope_t *locals) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef lt = map_type(g, st);
    LLVMValueRef sval = coerce_struct_value(g, val, st);
    if (!lt || LLVMGetTypeKind(lt) != LLVMStructTypeKind ||
        LLVMGetTypeKind(LLVMTypeOf(sval)) != LLVMStructTypeKind) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, src_expr->loc, 3040,
                      "cannot convert '%s' to 'object': value types "
                      "cannot be boxed (Zan has no boxing facility)",
                      st->name.str ? st->name.str : "?");
        return LLVMConstNull(i8ptr);
    }
    int site = reserve_arc_site(g, st->sym, st, 0, NULL);
    char nbuf[320];
    snprintf(nbuf, sizeof(nbuf), "box:%.*s",
             (int)st->name.len, st->name.str ? st->name.str : "?");
    LLVMTypeRef alloc_ty = LLVMGlobalGetValueType(g->rt_alloc);
    LLVMValueRef sz8 = LLVMConstAdd(LLVMSizeOf(lt), LLVMConstInt(i64, 8, 0));
    LLVMValueRef raw = zan_call2(g->builder, alloc_ty, g->rt_alloc,
        (LLVMValueRef[]){ sz8, arc_site_arg(g, site),
                          zan_irgen_intern_string(g, nbuf) }, 3, "box.alloc");
    LLVMValueRef tag_slot = LLVMBuildBitCast(g->builder, raw,
        LLVMPointerType(i8ptr, 0), "box.vps");
    LLVMBuildStore(g->builder, get_vtable_global(g, st->sym), tag_slot);
    LLVMValueRef eight = LLVMConstInt(i64, 8, 0);
    /* 空载荷结构体（{}）跳过 store：零大小 store 非法 */
    if (LLVMCountStructElementTypes(lt) > 0) {
        LLVMValueRef p8 = LLVMBuildGEP2(g->builder, i8, raw, &eight, 1, "box.pl");
        LLVMValueRef slot = LLVMBuildBitCast(g->builder, p8,
            LLVMPointerType(lt, 0), "box.slot");
        LLVMBuildStore(g->builder, sval, slot);
        /* 载荷里的引用归堆对象所有；来源是既有变量（非构造临时）时
         * 补 +1——与 emit_struct_local_capture 同一捕获协议。注意这里
         * 判定的是**操作数**的 owned 性（装箱 cast 本身在 helper 里
         * 已定义为 owned，不能用它判字段来源）。 */
        zan_ast_node_t *operand = (src_expr && src_expr->kind == AST_CAST_EXPR)
                                      ? src_expr->cast.expr : src_expr;
        if (!expr_yields_owned_rc_value(g, operand, locals))
            emit_collection_value_retain(g, st, sval, 0);
    }
    return raw;
}

/* 拆箱：object → 装箱过的结构体。tag 检查复用 `is` 的描述符名表
 * 通路；失败按 A509 同款软失败（stderr 一次 + 零值继续，
 * --strict-runtime 下 exit 70）。C# 同款拷贝语义：载荷按值拷出，
 * 目的地（局部捕获/赋值协议）对托管字段 retain，箱保有自己的份。 */
static LLVMValueRef emit_unbox_struct(zan_irgen_t *g, zan_type_t *tt,
                                      LLVMValueRef val, zan_ast_node_t *expr) {
    LLVMTypeRef lt = map_type(g, tt);
    if (!lt || LLVMGetTypeKind(lt) != LLVMStructTypeKind) return val;
    LLVMValueRef ok = emit_runtime_is_check_name(g, val, tt);
    LLVMValueRef cfn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(g->ctx, cfn, "unbox.ok");
    LLVMBasicBlockRef bad_bb = LLVMAppendBasicBlockInContext(g->ctx, cfn, "unbox.bad");
    LLVMBasicBlockRef m_bb = LLVMAppendBasicBlockInContext(g->ctx, cfn, "unbox.m");
    LLVMBuildCondBr(g->builder, ok, ok_bb, bad_bb);
    LLVMPositionBuilderAtEnd(g->builder, ok_bb);
    /* A556-2：箱首字是 vtable tag，载荷在 +8（emit_box_struct 布局契约） */
    LLVMValueRef p8 = LLVMBuildGEP2(g->builder,
        LLVMInt8TypeInContext(g->ctx), val,
        (LLVMValueRef[]){ LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 8, 0) },
        1, "unbox.p8");
    LLVMValueRef slot = LLVMBuildBitCast(g->builder, p8,
        LLVMPointerType(lt, 0), "unbox.p");
    LLVMValueRef good = LLVMBuildLoad2(g->builder, lt, slot, "unbox.v");
    LLVMBuildBr(g->builder, m_bb);
    LLVMPositionBuilderAtEnd(g->builder, bad_bb);
    emit_runtime_check(g,
        LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0),
        expr->loc,
        "InvalidCastException: object is not an instance of the target struct type");
    LLVMBasicBlockRef bad_end = LLVMGetInsertBlock(g->builder);
    LLVMBuildBr(g->builder, m_bb);
    LLVMPositionBuilderAtEnd(g->builder, m_bb);
    LLVMValueRef phi = LLVMBuildPhi(g->builder, lt, "unbox.r");
    LLVMValueRef vals[] = { good, LLVMConstNull(lt) };
    LLVMBasicBlockRef bbs[] = { ok_bb, bad_end };
    LLVMAddIncoming(phi, vals, bbs, 2);
    return phi;
}

