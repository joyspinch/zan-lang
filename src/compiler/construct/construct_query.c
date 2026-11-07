#include "irgen_internal.h"

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
LLVMValueRef emit_expr_query_expr(zan_irgen_t *g, zan_ast_node_t *expr,
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
LLVMValueRef emit_expr_tuple(zan_irgen_t *g, zan_ast_node_t *expr,
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

