/* irgen_expr_core */

/* 底层系统交互与数据协议契约 */
static zan_type_t *subst_type_param(zan_type_t *t, zan_type_t *recv);
static zan_type_t *concretize(zan_irgen_t *g, zan_type_t *t);
static zan_type_t *subst_type_param_deep(zan_irgen_t *g, zan_type_t *t,
                                         zan_type_t *recv);
static bool type_is_concrete(zan_type_t *t);
static int is_rc_managed_type(zan_type_t *t);
static LLVMValueRef get_array_release_decl(zan_irgen_t *g, zan_type_t *elem_type, int rect);
static LLVMValueRef get_array_desc(zan_irgen_t *g, zan_type_t *elem_type);
static LLVMValueRef get_libc_fn(zan_irgen_t *g, const char *name, LLVMTypeRef ty);

static LLVMValueRef emit_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals);
static void emit_runtime_check(zan_irgen_t *g, LLVMValueRef is_error,
                               zan_loc_t loc, const char *msg);
static LLVMValueRef emit_soft_base_select(zan_irgen_t *g, LLVMValueRef base,
                                          LLVMValueRef isnull, zan_loc_t loc);
/* 发射 Lambda 表达式并推断参数与返回值类型 */
static LLVMValueRef emit_lambda_typed(zan_irgen_t *g, zan_ast_node_t *expr,
                                      zan_type_t *expected, local_scope_t *locals);
/* 模块核心语义抽象与接口调用契约 */
static zan_type_t *method_param_type(zan_irgen_t *g, zan_symbol_t *msym, int idx);
static zan_type_t *method_param_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                        int idx, zan_ast_node_t *call,
                                        zan_ast_node_t *recv_expr,
                                        local_scope_t *locals);
static zan_type_t *method_ret_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                      zan_ast_node_t *call,
                                      zan_ast_node_t *recv_expr,
                                      local_scope_t *locals);
static bool method_ret_is_bare_tp(zan_symbol_t *msym);
/* 发射a call argument, typing a bare lambda from the target delegate param */
static LLVMValueRef emit_arg_typed(zan_irgen_t *g, zan_ast_node_t *arg,
                                   zan_type_t *ptype, local_scope_t *locals);

/* 内部辅助逻辑 */
enum {
    ASYNC_FRAME_SCHED = 0,        /* 模块核心语义抽象与接口调用契约 */
    ASYNC_FRAME_SCHED_STEP = 1,   /* 内部辅助逻辑 */
    ASYNC_FRAME_STATE = 2,        /* i32: 0=start, k=resume-after-await-k, -1=done */
    ASYNC_FRAME_DONE = 3,         /* 核心系统底层抽象与内存语义契约 */
    ASYNC_FRAME_AWAITER = 4,      /* 核心系统底层抽象与内存语义契约 */
    ASYNC_FRAME_AWAITER_STEP = 5, /* void(i8*)*: awaiter's resume fn (or null) */
    ASYNC_FRAME_RESULT = 6,       /* 内部辅助实现 */
    ASYNC_FRAME_CLEANUP = 7,      /* 底层系统交互与数据协议契约 */
    ASYNC_FRAME_HCOUNT = 8,       /* 底层系统交互与数据协议契约 */
    ASYNC_FRAME_SELF_STEP = 9,    /* 底层系统交互与数据协议契约 */
    ASYNC_FRAME_EXC = 10,         /* 核心系统底层抽象与内存语义契约 */
    ASYNC_FRAME_EXC_TID = 11,     /* 核心系统底层抽象与内存语义契约 */
    ASYNC_FRAME_EXC_OWNED = 12,   /* 核心系统底层抽象与内存语义契约 */
    ASYNC_FRAME_CANCEL = 13,      /* 核心系统底层抽象与内存语义契约 */
    ASYNC_FRAME_CHILD = 14,       /* 内部辅助实现 */
    ASYNC_FRAME_LNEXT = 15,       /* 底层系统交互与数据协议契约 */
    ASYNC_FRAME_PENDING_COUNT = 16, /* 模块核心语义抽象与接口调用契约 */
    ASYNC_FRAME_HSTACK = 17,      /* 内部辅助实现 */
    ASYNC_FRAME_CEXC = 18,        /* 内部辅助逻辑 */
    ASYNC_FRAME_CEXC_OWNED = 19,  /* 底层系统交互与数据协议契约 */
    ASYNC_FRAME_CEXC_TID = 20,    /* 内部辅助实现 */
    ASYNC_FRAME_PENDING = 21,    /* 内部辅助逻辑 */
    ASYNC_FRAME_HPENDING = 22,   /* 核心系统底层抽象与内存语义契约 */
    ASYNC_FRAME_FIRST_PARAM = 23
};
static LLVMValueRef coerce_to_i64(zan_irgen_t *g, LLVMValueRef v);
static LLVMValueRef coerce_to_frame_result(zan_irgen_t *g, LLVMValueRef v,
                                           zan_type_t *ty);
static LLVMValueRef coerce_from_frame_result(zan_irgen_t *g, LLVMValueRef res,
                                             zan_type_t *ty);
static LLVMValueRef coerce_async_ret(zan_irgen_t *g, LLVMValueRef val);
static void emit_async_cancel_check(zan_irgen_t *g, local_scope_t *locals);
static LLVMBasicBlockRef get_async_requeue_bb(zan_irgen_t *g);
static LLVMValueRef get_async_state_ptr(zan_irgen_t *g);
static LLVMValueRef get_async_cancel_ptr(zan_irgen_t *g);
static LLVMValueRef get_async_self_i8(zan_irgen_t *g);
static LLVMValueRef get_async_self_int(zan_irgen_t *g, LLVMTypeRef ptr_int_ty);
static LLVMValueRef get_async_child_ptr(zan_irgen_t *g);
static LLVMValueRef get_async_sub_slot_ptr(zan_irgen_t *g);
static LLVMValueRef get_async_result_ptr(zan_irgen_t *g);
static void emit_co_cancel_delay(zan_irgen_t *g, LLVMValueRef frame);
static LLVMValueRef get_co_cancel_fn(zan_irgen_t *g);
static LLVMValueRef get_co_isdone_fn(zan_irgen_t *g);
static LLVMValueRef get_co_reap_fn(zan_irgen_t *g);
static LLVMValueRef get_co_track_fn(zan_irgen_t *g);
static LLVMValueRef get_co_untrack_fn(zan_irgen_t *g);
static bool anf_stmt_contains_await(zan_ast_node_t *s);
static void emit_async_eh_unarm(zan_irgen_t *g);
static LLVMBasicBlockRef get_async_suspend_ret_bb(zan_irgen_t *g);
static void emit_async_check_sub_exc(zan_irgen_t *g, LLVMValueRef sub, LLVMValueRef tmp_mark);

/* 底层系统交互与数据协议契约 */
static LLVMValueRef emit_task_member(zan_irgen_t *g, LLVMValueRef hp,
                                     zan_type_t *rt, int mode) {
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    if (mode == 2) {
        LLVMValueRef idf = get_co_isdone_fn(g);
        return zan_call2(g->builder, LLVMGlobalGetValueType(idf), idf,
                         &hp, 1, "task.done");
    }
    LLVMValueRef fn = g->current_fn;
    LLVMBasicBlockRef test_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "tk.test");
    LLVMBasicBlockRef pump_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "tk.pump");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "tk.done");
    LLVMBuildBr(g->builder, test_bb);
    LLVMPositionBuilderAtEnd(g->builder, test_bb);
    LLVMValueRef idf = get_co_isdone_fn(g);
    LLVMValueRef dn = zan_call2(g->builder, LLVMGlobalGetValueType(idf), idf,
                                &hp, 1, "tk.dn");
    LLVMValueRef dn1 = zan_icmp(g->builder, LLVMIntNE, dn,
                                LLVMConstInt(LLVMTypeOf(dn), 0, 0), "tk.dn1");
    LLVMBuildCondBr(g->builder, dn1, done_bb, pump_bb);
    LLVMPositionBuilderAtEnd(g->builder, pump_bb);
    /* 内部辅助实现 */
    zan_call2(g->builder, g->rt_co_sched_run_type, g->rt_co_sched_run, NULL, 0, "");
    LLVMBuildBr(g->builder, test_bb);
    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    if (mode == 1) {
        /* Task<T> */
        LLVMValueRef rp = LLVMBuildStructGEP2(g->builder, g->co_header_type,
            hp, ASYNC_FRAME_RESULT, "tk.resp");
        LLVMValueRef raw = LLVMBuildLoad2(g->builder, i64t, rp, "tk.raw");
        LLVMValueRef val = coerce_from_frame_result(g, raw, rt);
        LLVMValueRef reap = get_co_reap_fn(g);
        zan_call2(g->builder, LLVMGlobalGetValueType(reap), reap, &hp, 1, "");
        return val;
    }
    if (mode == 3) {
        /* 内部辅助逻辑 */
        LLVMValueRef reap = get_co_reap_fn(g);
        zan_call2(g->builder, LLVMGlobalGetValueType(reap), reap, &hp, 1, "");
    }
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

/* 底层系统交互与数据协议契约 */
static bool is_name_path(zan_ast_node_t *node) {
    if (!node) return false;
    if (node->kind == AST_IDENTIFIER) return true;
    if (node->kind == AST_MEMBER_ACCESS) return is_name_path(node->member.object);
    return false;
}

/* 模块核心语义抽象与接口调用契约 */
static zan_ast_node_t *name_path_head(zan_ast_node_t *node) {
    while (node && node->kind == AST_MEMBER_ACCESS) node = node->member.object;
    return (node && node->kind == AST_IDENTIFIER) ? node : NULL;
}

/* 内部辅助逻辑 */
static int conv_rank(zan_type_t *t) {
    if (!t) return 0;
    switch (t->kind) {
    case TYPE_SBYTE: case TYPE_BYTE: case TYPE_CHAR: return 1;
    case TYPE_SHORT: case TYPE_USHORT: return 2;
    case TYPE_INT: case TYPE_UINT: return 4;
    case TYPE_LONG: case TYPE_ULONG: return 8;
    case TYPE_NINT: return 8;
    default: return 0;
    }
}

static bool narrow_warn_enabled(void) {
    static int state = -1;
    if (state < 0) state = getenv("ZAN_WARN_NARROW") ? 1 : 0;
    return state == 1;
}

/* 内部辅助实现 */
/* 内部辅助逻辑 */
static bool const_int_expr(zan_ast_node_t *e, int64_t *out) {
    if (!e) return false;
    switch (e->kind) {
    case AST_INT_LITERAL:
        *out = e->int_val;
        return true;
    case AST_UNARY: {
        int64_t v;
        if (!const_int_expr(e->unary.operand, &v)) return false;
        uint64_t uv = (uint64_t)v;
        switch (e->unary.op) {
        case TK_MINUS: *out = (int64_t)(0ULL - uv); return true;
        case TK_PLUS:  *out = v;  return true;
        case TK_TILDE: *out = ~v; return true;
        default: return false;
        }
    }
    case AST_BINARY: {
        int64_t l, r;
        if (!const_int_expr(e->binary.left, &l)) return false;
        if (!const_int_expr(e->binary.right, &r)) return false;
        uint64_t ul = (uint64_t)l, ur = (uint64_t)r;
        switch (e->binary.op) {
        case TK_PLUS:  *out = (int64_t)(ul + ur); return true;
        case TK_MINUS: *out = (int64_t)(ul - ur); return true;
        case TK_STAR:  *out = (int64_t)(ul * ur); return true;
        case TK_SLASH:
            /* 底层系统交互与数据协议契约 */
            if (!r || (l == INT64_MIN && r == -1)) return false;
            *out = l / r; return true;
            /* 模块核心语义抽象与接口调用契约 */
        case TK_LESS_LESS: *out = (int64_t)((uint64_t)l << (r & 63)); return true;
        case TK_AMP:   *out = l & r; return true;
        case TK_PIPE:  *out = l | r; return true;
        default: return false;
        }
    }
    default:
        return false;
    }
}

static bool const_fits(zan_type_t *dst, int64_t v) {
    switch (dst->kind) {
    case TYPE_SBYTE:  return v >= -128 && v <= 127;
    case TYPE_BYTE:   return v >= 0 && v <= 255;
    case TYPE_SHORT:  return v >= -32768 && v <= 32767;
    case TYPE_USHORT: case TYPE_CHAR: return v >= 0 && v <= 65535;
    case TYPE_INT:    return v >= INT32_MIN && v <= INT32_MAX;
    case TYPE_UINT:   return v >= 0 && v <= 4294967295LL;
    case TYPE_LONG: case TYPE_NINT: return true;
    case TYPE_ULONG:  return v >= 0;
    default: return false;
    }
}

static void check_implicit_narrowing(zan_irgen_t *g, zan_type_t *dst,
                                      zan_type_t *src, zan_ast_node_t *at,
                                      const char *what) {
    if (!g || !g->diag || !at || !dst || !src) return;
    if (at->kind == AST_CAST_EXPR) return;
    int64_t cv;
    if (const_int_expr(at, &cv) && const_fits(dst, cv)) return;
    int rd = conv_rank(dst), rs = conv_rank(src);
    bool src_float = src->kind == TYPE_FLOAT || src->kind == TYPE_DOUBLE;
    if (!rd || (!rs && !src_float)) return;

    /* 编译器代码生成与运行时系统底层调用契约 */
    bool native_handle_loss =
        src->kind == TYPE_NINT && dst->kind != TYPE_NINT && rd < 8;
    /* 内部辅助逻辑 */
    bool float_to_int_loss = src_float && rd != 0;
    bool loses = src_float || rs > rd || native_handle_loss;
    if (!loses) return;
    if (!float_to_int_loss && !native_handle_loss && !narrow_warn_enabled())
        return;

    zan_diag_emit(g->diag,
                  (float_to_int_loss || native_handle_loss) ? DIAG_ERROR
                                                            : DIAG_WARNING,
                  at->loc,
                  "narrowing conversion from '%s' to '%s' in %s needs an "
                  "explicit cast (C# rules)",
                  src->name.str ? src->name.str : "?",
                  dst->name.str ? dst->name.str : "?", what);
}

/* 底层系统交互与数据协议契约 */
static zan_symbol_t *find_user_conversion(zan_irgen_t *g, zan_type_t *from_type,
                                          zan_type_t *to_type, const char *op_name);

/* 内部辅助逻辑 */
static void check_value_type_mismatch(zan_irgen_t *g, zan_type_t *dst, zan_type_t *src,
                                      zan_ast_node_t *at, const char *what) {
    if (!g || !g->diag || !at || !dst || !src) return;
    if (dst->kind == src->kind) return;
    bool dst_struct = dst->kind == TYPE_STRUCT, src_struct = src->kind == TYPE_STRUCT;
    if (dst_struct == src_struct) return;
    /* 内部辅助逻辑 */
    if (find_user_conversion(g, src, dst, "op_implicit")) return;
    bool other_prim = (dst_struct ? conv_rank(src) : conv_rank(dst)) != 0 ||
                      (dst_struct ? src->kind : dst->kind) == TYPE_FLOAT ||
                      (dst_struct ? src->kind : dst->kind) == TYPE_DOUBLE ||
                      (dst_struct ? src->kind : dst->kind) == TYPE_BOOL;
    if (!other_prim) return;
    zan_diag_emit(g->diag, DIAG_ERROR, at->loc,
                  "cannot convert '%s' to '%s' in %s: a value struct and a "
                  "primitive are unrelated types",
                  src->name.str ? src->name.str : "?",
                  dst->name.str ? dst->name.str : "?", what);
}

/* 内部辅助逻辑 */
static int render_type_full(zan_type_t *t, char *buf, int cap) {
    if (!t || cap <= 1) return 0;
    int n = 0;
    if (t->kind == TYPE_ARRAY && t->element_type) {
        n = render_type_full(t->element_type, buf, cap);
        if (n < cap - 1) {
            buf[n++] = '[';
            for (int r = 1; r < (t->array_rank > 1 ? t->array_rank : 1); r++)
                if (n < cap - 1) buf[n++] = ',';
            if (n < cap - 1) buf[n++] = ']';
        }
        if (n < cap) buf[n] = '\0';
        return n;
    }
    if (t->name.len) {
        int w = (int)t->name.len;
        if (w > cap - n - 1) w = cap - n - 1;
        memcpy(buf + n, t->name.str, (size_t)w);
        n += w;
    } else if (n < cap - 1) {
        buf[n++] = '?';
    }
    if (t->type_arg_count > 0 && n < cap - 2) {
        buf[n++] = '<';
        for (int i = 0; i < t->type_arg_count && n < cap - 2; i++) {
            if (i) buf[n++] = ',';
            n += render_type_full(t->type_args[i], buf + n, cap - n);
        }
        if (n < cap - 1) buf[n++] = '>';
    }
    if (n < cap) buf[n] = '\0';
    return n;
}

/* 模块核心语义抽象与接口调用契约 */
static bool type_full_equal(zan_type_t *a, zan_type_t *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    if (a->name.len != b->name.len ||
        (a->name.len && memcmp(a->name.str, b->name.str, (size_t)a->name.len) != 0))
        return false;
    if (a->type_arg_count != b->type_arg_count) return false;
    for (int i = 0; i < a->type_arg_count; i++)
        if (!type_full_equal(a->type_args[i], b->type_args[i])) return false;
    return true;
}

/* 内部辅助实现 */
static bool type_has_unresolved(zan_type_t *t) {
    if (!t) return true;
    if (t->kind == TYPE_ERROR || t->kind == TYPE_TYPE_PARAM) return true;
    for (int i = 0; i < t->type_arg_count; i++)
        if (type_has_unresolved(t->type_args[i])) return true;
    if (t->element_type && type_has_unresolved(t->element_type)) return true;
    return false;
}

/* 内部辅助逻辑 */
static void check_generic_invariance(zan_irgen_t *g, zan_type_t *dst, zan_type_t *src,
                                     zan_ast_node_t *at, const char *what) {
    if (!g || !g->diag || !at || !dst || !src) return;
    if (dst->kind != TYPE_CLASS && dst->kind != TYPE_STRUCT) return;
    if (src->sym != dst->sym) return;          /* 核心系统底层抽象与内存语义契约 */
    if (dst->type_arg_count == 0) return;
    if (type_has_unresolved(dst) || type_has_unresolved(src)) return;
    for (int i = 0; i < dst->type_arg_count; i++) {
        zan_type_t *a = dst->type_args[i];
        zan_type_t *b = src->type_arg_count > i ? src->type_args[i] : NULL;
        if (!a || !b) return;
        if (!type_full_equal(a, b)) {
            char dbuf[96], sbuf[96];
            render_type_full(dst, dbuf, sizeof(dbuf));
            render_type_full(src, sbuf, sizeof(sbuf));
            zan_diag_emit(g->diag, DIAG_ERROR, at->loc,
                          "cannot convert '%s' to '%s' in %s: generic type "
                          "arguments are invariant and must match exactly",
                          sbuf[0] ? sbuf : "?",
                          dbuf[0] ? dbuf : "?", what);
            return;
        }
    }
}

static zan_type_t *infer_expr_type(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);

/* 内部辅助逻辑 */
static zan_type_t *field_type_through(zan_irgen_t *g, zan_symbol_t *fsym,
                                     zan_type_t *owner) {
    if (!fsym) return NULL;
    zan_type_t *ft = fsym->type;
    if (!owner) return ft;
    ft = subst_type_param_deep(g, ft, owner);
    if (ft && ft->kind == TYPE_TYPE_PARAM) ft = concretize(g, ft);
    return ft;
}

static zan_type_t *member_access_field_type(zan_irgen_t *g, local_scope_t *locals, zan_ast_node_t *member) {
    if (!member || member->kind != AST_MEMBER_ACCESS) return NULL;
    zan_ast_node_t *obj = member->member.object;
    if (obj->kind == AST_IDENTIFIER) {
        local_var_t *l = local_find(locals, obj->ident.name);
        if (l && l->type && l->type->sym) {
            zan_symbol_t *fsym = get_field_sym(l->type->sym, member->member.name);
            if (fsym) return field_type_through(g, fsym, l->type);
        }
        /* ClassName */
        if (!l && g && g->binder) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder, obj->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *fsym = get_field_sym(cs, member->member.name);
                if (fsym) return fsym->type;
            }
        }
        /* EnumType */
        if (!l && g && g->binder) {
            zan_symbol_t *es = zan_binder_lookup(g->binder, obj->ident.name);
            if (es && es->kind == SYM_ENUM) {
                for (int ei = 0; ei < es->member_count; ei++) {
                    zan_symbol_t *em = es->members[ei];
                    if (em && em->kind == SYM_ENUM_MEMBER &&
                        em->name.len == member->member.name.len &&
                        memcmp(em->name.str, member->member.name.str,
                               (size_t)member->member.name.len) == 0) {
                        return es->type;
                    }
                }
            }
        }
        /* 编译器代码生成与运行时系统底层调用契约 */
        if (g && g->current_type_sym) {
            zan_symbol_t *ofsym = get_field_sym(g->current_type_sym, obj->ident.name);
            if (ofsym && ofsym->type && ofsym->type->sym) {
                zan_symbol_t *fsym = get_field_sym(ofsym->type->sym, member->member.name);
                if (fsym) return field_type_through(g, fsym, ofsym->type);
            }
        }
    }
    /* explicit `this */
    if ((obj->kind == AST_THIS_EXPR || obj->kind == AST_BASE_EXPR) &&
        g && g->current_type_sym) {
        zan_symbol_t *fsym = get_field_sym(g->current_type_sym, member->member.name);
        if (fsym) return fsym->type;
    }
    /* 内部辅助逻辑 */
    {
        zan_type_t *ot = infer_expr_type(g, obj, locals);
        if (ot && ot->sym) {
            zan_symbol_t *fsym = get_field_sym(ot->sym, member->member.name);
            if (fsym) return field_type_through(g, fsym, ot);
        }
    }
    return NULL;
}

static zan_type_t *infer_expr_type(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
static zan_type_t *container_elem_type(zan_type_t *t);
static zan_type_t *generic_method_ret(zan_irgen_t *g, zan_symbol_t *msym,
                                      zan_ast_node_t *call, local_scope_t *locals);

/* 模块核心语义抽象与接口调用契约 */
static int render_type_ref_name(const zan_ast_node_t *t, char *buf, int cap) {
    int n = 0;
    if (t && t->kind == AST_TYPE_REF && cap > 1) {
        int len = (int)t->type_ref.name.len;
        if (len > cap - n - 1) len = cap - n - 1;
        memcpy(buf + n, t->type_ref.name.str, (size_t)len);
        n += len;
        if (t->type_ref.type_args.count > 0) {
            if (n < cap - 1) buf[n++] = '<';
            for (int i = 0; i < t->type_ref.type_args.count; i++) {
                if (i > 0) {
                    if (n < cap - 1) buf[n++] = ',';
                    if (n < cap - 1) buf[n++] = ' ';
                }
                n += render_type_ref_name(t->type_ref.type_args.items[i],
                                          buf + n, cap - n);
            }
            if (n < cap - 1) buf[n++] = '>';
        }
        if (t->type_ref.is_array) {
            if (n < cap - 1) buf[n++] = '[';
            if (n < cap - 1) buf[n++] = ']';
        }
        if (t->type_ref.is_nullable && n < cap - 1) buf[n++] = '?';
    }
    if (cap > 0) buf[n] = 0;
    return n;
}

/* 编译器代码生成与运行时系统底层调用契约 */
static bool is_string_expr(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    if (!e || !locals) return false;
    switch (e->kind) {
    case AST_STRING_LITERAL:
    case AST_STRING_INTERP:
    case AST_TYPEOF_EXPR:
        return true;
    case AST_IDENTIFIER: {
        local_var_t *l = local_find(locals, e->ident.name);
        if (l) return l->type && l->type->kind == TYPE_STRING;
        if (g->current_type_sym) {
            zan_symbol_t *fs = get_field_sym(g->current_type_sym, e->ident.name);
            if (fs) return fs->type && fs->type->kind == TYPE_STRING;
        }
        return false;
    }
    case AST_MEMBER_ACCESS: {
        zan_type_t *ft = member_access_field_type(g, locals, e);
        return ft && ft->kind == TYPE_STRING;
    }
    case AST_INDEX: {
        /* 模块核心语义抽象与接口调用契约 */
        zan_type_t *ot = infer_expr_type(g, e->index.object, locals);
        zan_type_t *et;
        if (ot && type_named(ot, "Dict", 4) &&
            ot->type_arg_count == 2)
            et = ot->type_args[1];   /* 底层系统交互与数据协议契约 */
        else
            et = container_elem_type(ot);
        return et && et->kind == TYPE_STRING;
    }
    case AST_CONDITIONAL:
        /* 模块核心语义抽象与接口调用契约 */
        return is_string_expr(g, e->conditional.then_expr, locals) &&
               is_string_expr(g, e->conditional.else_expr, locals);
    case AST_BINARY:
        if (e->binary.op == TK_PLUS)
            return is_string_expr(g, e->binary.left, locals) ||
                   is_string_expr(g, e->binary.right, locals);
        return false;
    case AST_CALL: {
        zan_ast_node_t *callee = e->call.callee;
        if (callee && callee->kind == AST_MEMBER_ACCESS) {
            zan_istr_t m = callee->member.name;
            if ((m.len == 9 && memcmp(m.str, "Substring", 9) == 0) ||
                (m.len == 8 && memcmp(m.str, "ToString", 8) == 0) ||
                (m.len == 4 && memcmp(m.str, "Trim", 4) == 0) ||
                (m.len == 7 && memcmp(m.str, "Replace", 7) == 0) ||
                (m.len == 7 && memcmp(m.str, "ToUpper", 7) == 0) ||
                (m.len == 7 && memcmp(m.str, "ToLower", 7) == 0))
                return true;
        }
        /* 模块核心语义抽象与接口调用契约 */
        {
            zan_type_t *rt = infer_expr_type(g, e, locals);
            return rt && rt->kind == TYPE_STRING;
        }
    }
    default:
        return false;
    }
}

/* 底层系统交互与数据协议契约 */
static zan_type_t *container_elem_type(zan_type_t *t) {
    if (!t) return NULL;
    if (t->element_type) return t->element_type;
    if (t->type_args && t->type_arg_count > 0) return t->type_args[0];
    return NULL;
}

static int is_span_type(zan_type_t *t) {
    return t && t->kind == TYPE_STRUCT && t->name.len == 4 &&
           memcmp(t->name.str, "Span", 4) == 0;
}

static zan_type_t *dict_key_type(zan_irgen_t *g, zan_type_t *t) {
    if (!t || !t->type_args || t->type_arg_count < 1) return g ? g->binder->type_string : NULL;
    return t->type_args[0];
}

static zan_type_t *dict_value_type(zan_type_t *t) {
    if (!t || !t->type_args || t->type_arg_count < 2) return NULL;
    return t->type_args[1];
}

/* 核心系统底层抽象与内存语义契约 */
static bool type_mentions_tp(zan_type_t *t);
static zan_type_t *method_param_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                        int idx, zan_ast_node_t *call,
                                        zan_ast_node_t *recv_expr,
                                        local_scope_t *locals);

/* 模块核心语义抽象与接口调用契约 */
static bool types_concrete_equal(zan_type_t *a, zan_type_t *b) {
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    if ((a->kind == TYPE_CLASS || a->kind == TYPE_STRUCT ||
         a->kind == TYPE_INTERFACE || a->kind == TYPE_ENUM) &&
        a->sym != b->sym)
        return false;
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_NULLABLE) {
        if (a->kind == TYPE_ARRAY && a->array_rank != b->array_rank)
            return false; /* 核心系统底层抽象与内存语义契约 */
        return types_concrete_equal(a->element_type, b->element_type);
    }
    if (a->type_arg_count != b->type_arg_count) return false;
    for (int i = 0; i < a->type_arg_count; i++)
        if (!types_concrete_equal(a->type_args[i], b->type_args[i]))
            return false;
    return true;
}

/* 内部辅助实现 */
static bool types_match_modulo_tp(zan_type_t *a, zan_type_t *b) {
    if (!a || !b) return false;
    if (a->kind == TYPE_TYPE_PARAM || b->kind == TYPE_TYPE_PARAM) return true;
    if (a->kind != b->kind) return false;
    if ((a->kind == TYPE_CLASS || a->kind == TYPE_STRUCT ||
         a->kind == TYPE_INTERFACE || a->kind == TYPE_ENUM) &&
        a->sym != b->sym)
        return false;
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_NULLABLE) {
        if (a->kind == TYPE_ARRAY && a->array_rank != b->array_rank)
            return false; /* 核心系统底层抽象与内存语义契约 */
        return types_match_modulo_tp(a->element_type, b->element_type);
    }
    if (a->type_arg_count != b->type_arg_count) return false;
    for (int i = 0; i < a->type_arg_count; i++)
        if (!types_match_modulo_tp(a->type_args[i], b->type_args[i]))
            return false;
    return true;
}

/* 内部辅助逻辑 */
enum { FAM_UNKNOWN = 0, FAM_BOOL, FAM_INT, FAM_FLOAT, FAM_STRING, FAM_REF };

static int type_family(zan_type_t *t) {
    if (!t) return FAM_UNKNOWN;
    switch (t->kind) {
    case TYPE_BOOL: return FAM_BOOL;
    case TYPE_BYTE: case TYPE_SHORT: case TYPE_INT: case TYPE_LONG:
    case TYPE_SBYTE: case TYPE_USHORT: case TYPE_UINT: case TYPE_ULONG:
    case TYPE_CHAR: case TYPE_ENUM:
        return FAM_INT;
    case TYPE_FLOAT: case TYPE_DOUBLE: return FAM_FLOAT;
    case TYPE_STRING: return FAM_STRING;
    case TYPE_CLASS: case TYPE_INTERFACE: case TYPE_ARRAY: case TYPE_OBJECT: return FAM_REF;
    default: return FAM_UNKNOWN;
    }
}

/* 模块核心语义抽象与接口调用契约 */
static bool str_and_byte_buffer(zan_type_t *s, zan_type_t *b) {
    if (!s || !b || s->kind != TYPE_STRING || b->kind != TYPE_ARRAY) return false;
    zan_type_t *e = b->element_type;
    return e && (e->kind == TYPE_BYTE || e->kind == TYPE_SBYTE ||
                 e->kind == TYPE_CHAR);
}

/* 内部辅助实现 */
static void stmt_collect_return_types(zan_irgen_t *g, zan_ast_node_t *stmt,
                                      local_scope_t *locals,
                                      zan_type_t **found, int *mixed) {
    if (!stmt || *mixed) return;
    switch (stmt->kind) {
    case AST_RETURN_STMT:
        if (!stmt->ret.value) { *mixed = 1; return; } /* `return;` is void */
        {
            zan_type_t *t = infer_expr_type(g, stmt->ret.value, locals);
            if (!t) { *mixed = 1; return; }
            if (!*found) { *found = t; return; }
            if (!types_concrete_equal(*found, t)) *mixed = 1;
        }
        return;
    case AST_BLOCK:
        for (int i = 0; i < stmt->block.stmts.count; i++)
            stmt_collect_return_types(g, stmt->block.stmts.items[i],
                                      locals, found, mixed);
        return;
    case AST_IF_STMT:
        stmt_collect_return_types(g, stmt->if_stmt.then_body, locals, found, mixed);
        if (stmt->if_stmt.else_body)
            stmt_collect_return_types(g, stmt->if_stmt.else_body, locals,
                                      found, mixed);
        return;
    case AST_VAR_DECL:
        /* 内部辅助逻辑 */
        if (stmt->var_decl.initializer) {
            zan_type_t *t = infer_expr_type(g, stmt->var_decl.initializer, locals);
            if (t) local_add(locals, stmt->var_decl.name, NULL, t);
        }
        return;
    case AST_LAMBDA:
        return;                    /* 底层系统交互与数据协议契约 */
    case AST_WHILE_STMT: case AST_DO_WHILE_STMT: case AST_FOR_STMT:
    case AST_FOREACH_STMT: case AST_SWITCH_STMT: case AST_TRY_STMT:
        *mixed = 1;                /* 底层系统交互与数据协议契约 */
        return;
    default:
        return;
    }
}

static zan_type_t *stmt_lambda_return_type(zan_irgen_t *g, zan_ast_node_t *body,
                                           local_scope_t *locals) {
    if (!body || body->kind != AST_BLOCK) return NULL;
    zan_type_t *found = NULL;
    int mixed = 0;
    stmt_collect_return_types(g, body, locals, &found, &mixed);
    return mixed ? NULL : found;
}

/* 内部辅助实现 */
static zan_type_t *lambda_body_type(zan_irgen_t *g, zan_ast_node_t *lam,
                                    zan_type_t *dt, local_scope_t *locals) {
    if (!lam || !dt || !locals) return NULL;
    zan_ast_node_t *body = lam->lambda.body;
    if (!body) return NULL;
    int mark = locals->count;
    for (int k = 0; k < lam->lambda.params.count; k++) {
        zan_ast_node_t *p = lam->lambda.params.items[k];
        zan_type_t *pt = k < dt->delegate_param_count
            ? dt->delegate_param_types[k] : NULL;
        local_add(locals, p->param.name, NULL, pt);
    }
    zan_type_t *bt = (body->kind == AST_BLOCK)
        ? stmt_lambda_return_type(g, body, locals)
        : infer_expr_type(g, body, locals);
    locals->count = mark;
    return bt;
}

/* 底层系统交互与数据协议契约 */
static zan_symbol_t *arg_method_group(zan_irgen_t *g, zan_ast_node_t *a,
                                      local_scope_t *locals, int arity) {
    if (!a || a->kind != AST_MEMBER_ACCESS) return NULL;
    zan_ast_node_t *obj = a->member.object;
    if (!obj || obj->kind != AST_IDENTIFIER) return NULL;
    if (locals && local_find(locals, obj->ident.name)) return NULL;
    zan_symbol_t *cs = zan_binder_lookup(g->binder, obj->ident.name);
    if (!cs || (cs->kind != SYM_CLASS && cs->kind != SYM_STRUCT)) return NULL;
    if (get_field_sym(cs, a->member.name)) return NULL;
    zan_symbol_t *m = arity >= 0 ? resolve_overload(cs, a->member.name, arity, 0)
                                 : NULL;
    if (!m) m = get_method_sym(cs, a->member.name);
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) return NULL;
    return m;
}

/* 内部辅助逻辑 */
static int method_group_score(zan_irgen_t *g, zan_symbol_t *mg, zan_type_t *pt) {
    if (!pt || pt->kind != TYPE_DELEGATE) return -1;
    if (mg->decl->method_decl.params.count != pt->delegate_param_count) return -1;
    int score = 2;
    zan_type_t *rt = mg->decl->method_decl.return_type
        ? zan_binder_resolve_type(g->binder, mg->decl->method_decl.return_type)
        : NULL;
    zan_type_t *dr = pt->delegate_ret_type;
    if (rt && dr && !type_mentions_tp(rt) && !type_mentions_tp(dr)) {
        /* 内部辅助实现 */
        if (types_concrete_equal(rt, dr)) score += 2;
        else if (type_family(rt) != type_family(dr) &&
                 type_family(rt) != FAM_UNKNOWN &&
                 type_family(dr) != FAM_UNKNOWN)
            return -1;
    }
    return score;
}

static struct zan_ctor_entry *find_ctor(zan_irgen_t *g, zan_symbol_t *type_sym,
                                        zan_ast_list_t *args, local_scope_t *locals,
                                        zan_ast_node_t *exclude);
static bool implicit_ctor_for_arg(zan_irgen_t *g, zan_type_t *target,
                                  zan_type_t *source, zan_ast_node_t *arg,
                                  local_scope_t *locals);
static bool class_implements_iface(zan_symbol_t *cls, zan_symbol_t *iface);

static bool class_derives_from(zan_symbol_t *derived, zan_symbol_t *base) {
    if (!derived || !base) return false;
    for (zan_symbol_t *cur = derived; cur && cur->type && cur->type->base_type;
         cur = cur->type->base_type->sym) {
        if (cur->type->base_type->sym == base) return true;
    }
    return false;
}

static struct zan_ctor_entry *find_ctor(zan_irgen_t *g, zan_symbol_t *type_sym,
                                        zan_ast_list_t *args, local_scope_t *locals,
                                        zan_ast_node_t *exclude) {
    struct zan_ctor_entry *best = NULL;
    int best_score = -1;
    int argc = args ? args->count : 0;
    for (int i = 0; i < g->ctor_count; i++) {
        struct zan_ctor_entry *entry = &g->ctors[i];
        if (entry->type_sym != type_sym || entry->param_count != argc ||
            entry->decl == exclude)
            continue;
        int score = 0;
        bool compatible = true;
        if (entry->decl && locals) {
            for (int j = 0; j < argc; j++) {
                /* 内部辅助实现 */
                zan_ast_node_t *arg = args->items[j];
                if (arg && arg->kind == AST_NAMED_ARG) {
                    int target = -1;
                    for (int q = 0; q < entry->decl->method_decl.params.count; q++) {
                        zan_ast_node_t *pp = entry->decl->method_decl.params.items[q];
                        if (pp && pp->kind == AST_PARAM &&
                            pp->param.name.len == arg->named_arg.name.len &&
                            memcmp(pp->param.name.str, arg->named_arg.name.str,
                                   (size_t)arg->named_arg.name.len) == 0) {
                            target = q;
                            break;
                        }
                    }
                    if (target < 0) {
                        compatible = false;
                        break;
                    }
                    if (target < argc && args->items[target] &&
                        args->items[target]->kind != AST_NAMED_ARG) {
                        /* 底层系统交互与数据协议契约 */
                        compatible = false;
                        break;
                    }
                    arg = arg->named_arg.expr;
                    zan_type_t *nt = zan_binder_resolve_type(g->binder,
                        entry->decl->method_decl.params.items[target]->param.type);
                    zan_type_t *nat = infer_expr_type(g, arg, locals);
                    if (nt && nat && types_concrete_equal(nt, nat)) score += 4;
                    continue;
                }
                zan_ast_node_t *param = entry->decl->method_decl.params.items[j];
                zan_type_t *pt = zan_binder_resolve_type(g->binder, param->param.type);
                /* 内部辅助实现 */
                if (pt && args->items[j] && args->items[j]->kind == AST_LAMBDA) {
                    zan_ast_node_t *lam = args->items[j];
                    if (pt->kind != TYPE_DELEGATE ||
                        pt->delegate_param_count != lam->lambda.params.count) {
                        compatible = false;
                        break;
                    }
                    score += 2;
                    /* 内部辅助实现 */
                    zan_type_t *bt = lambda_body_type(g, lam, pt, locals);
                    if (bt && pt->delegate_ret_type &&
                        types_concrete_equal(bt, pt->delegate_ret_type))
                        score += 2;
                    continue;
                }
                /* 内部辅助实现 */
                zan_symbol_t *mg = arg_method_group(
                    g, args->items[j], locals,
                    (pt && pt->kind == TYPE_DELEGATE)
                        ? pt->delegate_param_count : -1);
                if (mg) {
                    int ms = method_group_score(g, mg, pt);
                    if (ms < 0) { compatible = false; break; }
                    score += ms;
                    continue;
                }
                zan_type_t *at = infer_expr_type(g, args->items[j], locals);
                if (pt && at && type_mentions_tp(pt) && !type_mentions_tp(at)) {
                    /* 内部辅助实现 */
                    if (types_match_modulo_tp(pt, at)) score += 3;
                    continue;
                }
                if (!pt || !at || type_mentions_tp(pt) || type_mentions_tp(at))
                    continue;
                if (types_concrete_equal(pt, at)) {
                    score += 4;
                    continue;
                }
                int pf = type_family(pt), af = type_family(at);
                if (pt->kind == TYPE_OBJECT && (af == FAM_REF || af == FAM_STRING)) {
                    score += 1;
                    continue;
                }
                if (pf == af && pf != FAM_UNKNOWN) {
                    if (pf == FAM_REF) {
                        if (pt->kind == TYPE_OBJECT) {
                            score += 1;
                            continue;
                        }
                        if (pt->kind == TYPE_INTERFACE) {
                            if (at->kind == TYPE_CLASS && at->sym && pt->sym) {
                                if (!class_implements_iface(at->sym, pt->sym)) {
                                    compatible = false;
                                    break;
                                }
                            }
                        } else if (pt->kind == TYPE_CLASS && at->kind == TYPE_CLASS) {
                            if (pt->sym && at->sym && pt->sym != at->sym &&
                                !class_derives_from(at->sym, pt->sym)) {
                                compatible = false;
                                break;
                            }
                        }
                    }
                    score += 2;
                    continue;
                }
                if (implicit_ctor_for_arg(g, pt, at, args->items[j], locals)) {
                    score += 1;
                    continue;
                }
                if (pf == FAM_FLOAT && af == FAM_INT)
                    continue;
                if (str_and_byte_buffer(pt, at) || str_and_byte_buffer(at, pt))
                    continue;
                compatible = false;
                break;
            }
        }
        if (compatible && score > best_score) {
            best = entry;
            best_score = score;
        }
    }
    /* 内部辅助逻辑 */
    return best;
}

/* 内部辅助逻辑 */
static bool is_implicit_ctor_source(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BOOL:
    case TYPE_SBYTE: case TYPE_BYTE: case TYPE_SHORT: case TYPE_USHORT:
    case TYPE_INT: case TYPE_UINT: case TYPE_LONG: case TYPE_ULONG:
    case TYPE_NINT: case TYPE_CHAR: case TYPE_ENUM:
        return true;
    default:
        return false;
    }
}

static bool implicit_ctor_for_arg(zan_irgen_t *g, zan_type_t *target,
                                  zan_type_t *source, zan_ast_node_t *arg,
                                  local_scope_t *locals) {
    if (!target || target->kind != TYPE_CLASS || !target->sym ||
        !is_implicit_ctor_source(source) || !arg || !locals)
        return false;
    zan_ast_node_t *items[1] = { arg };
    zan_ast_list_t one;
    one.items = items;
    one.count = 1;
    one.capacity = 1;
    return find_ctor(g, target->sym, &one, locals, NULL) != NULL;
}

/* 内部辅助实现 */
static bool fill_ctor_default_args(zan_irgen_t *g, zan_symbol_t *type_sym,
                                   const zan_ast_list_t *args,
                                   zan_ast_list_t *out) {
    int argc = args ? args->count : 0;
    for (int i = 0; i < g->ctor_count; i++) {
        struct zan_ctor_entry *entry = &g->ctors[i];
        if (entry->type_sym != type_sym || entry->param_count <= argc ||
            !entry->decl) continue;
        zan_ast_list_t *ps = &entry->decl->method_decl.params;
        bool defaulted = true;
        for (int k = argc; k < ps->count; k++) {
            zan_ast_node_t *p = ps->items[k];
            if (!p || p->kind != AST_PARAM || !p->param.default_val) {
                defaulted = false;
                break;
            }
        }
        if (!defaulted) continue;
        zan_ast_list_init(out);
        for (int k = 0; k < argc; k++)
            zan_ast_list_push(out, args->items[k], g->arena);
        for (int k = argc; k < ps->count; k++)
            zan_ast_list_push(out, ps->items[k]->param.default_val, g->arena);
        return true;
    }
    return false;
}

/* 内部辅助逻辑 */
static int expr_family(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    if (!e) return FAM_UNKNOWN;
    switch (e->kind) {
    case AST_INT_LITERAL: case AST_CHAR_LITERAL: return FAM_INT;
    case AST_FLOAT_LITERAL: return FAM_FLOAT;
    case AST_STRING_LITERAL: case AST_STRING_INTERP: return FAM_STRING;
    case AST_BOOL_LITERAL: return FAM_BOOL;
    case AST_UNARY:
        if (e->unary.op == TK_BANG) return FAM_BOOL;
        return expr_family(g, e->unary.operand, locals);
    case AST_POSTFIX_UNARY:
        return expr_family(g, e->unary.operand, locals);
    case AST_CONDITIONAL: {
        int tf = expr_family(g, e->conditional.then_expr, locals);
        if (tf != FAM_UNKNOWN) return tf;
        return expr_family(g, e->conditional.else_expr, locals);
    }
    case AST_BINARY:
        switch (e->binary.op) {
        case TK_EQ_EQ: case TK_BANG_EQ: case TK_LESS: case TK_GREATER:
        case TK_LESS_EQ: case TK_GREATER_EQ: case TK_AMP_AMP: case TK_PIPE_PIPE:
            return FAM_BOOL;
        case TK_PLUS: case TK_MINUS: case TK_STAR: case TK_SLASH:
        case TK_PERCENT: {
            int lf = expr_family(g, e->binary.left, locals);
            int rf = expr_family(g, e->binary.right, locals);
            if (lf == FAM_STRING || rf == FAM_STRING) return FAM_STRING;
            if (lf == FAM_FLOAT || rf == FAM_FLOAT) return FAM_FLOAT;
            if (lf != FAM_UNKNOWN) return lf;
            return rf;
        }
        default:
            return FAM_UNKNOWN;
        }
    default:
        return type_family(infer_expr_type(g, e, locals));
    }
}

/* 内部辅助逻辑 */
static int concrete_arg_score(zan_irgen_t *g, zan_type_t *pt,
                              zan_ast_node_t *a, local_scope_t *locals) {
    zan_type_t *at = infer_expr_type(g, a, locals);
    if (at && !type_mentions_tp(at)) {
        if (types_concrete_equal(pt, at)) return 4;
        int pf = type_family(pt), af = type_family(at);
        if (pt->kind == TYPE_OBJECT && (af == FAM_REF || af == FAM_STRING)) return 1;
        if (pf == FAM_UNKNOWN || af == FAM_UNKNOWN) return 0;
        if (pf == af) return 2;
        if (implicit_ctor_for_arg(g, pt, at, a, locals)) return 1;
        /* 核心系统底层抽象与内存语义契约 */
        if (pf == FAM_FLOAT && af == FAM_INT) return 0;
        /* 内部辅助实现 */
        if (str_and_byte_buffer(pt, at) || str_and_byte_buffer(at, pt)) return 0;
        return -1;
    }
    int af = expr_family(g, a, locals);
    int pf = type_family(pt);
    if (af == FAM_UNKNOWN || pf == FAM_UNKNOWN) return 0;
    if (af == pf) return 2;
    if (pf == FAM_FLOAT && af == FAM_INT) return 0;
    return -1;
}

/* 内部辅助实现 */
static int method_args_score(zan_irgen_t *g, zan_symbol_t *m,
                             zan_ast_node_t *call, zan_ast_node_t *recv_expr,
                             local_scope_t *locals, int p0) {
    zan_ast_list_t *ps = &m->decl->method_decl.params;
    int score = 0;
    if (!call || call->kind != AST_CALL || !locals) return score;
    for (int j = p0; j < ps->count; j++) {
        int ai = j - p0;
        if (ai >= call->call.args.count) break;
        /* 内部辅助实现 */
        if (ps->items[j]->kind == AST_PARAM && ps->items[j]->param.is_params) {
            zan_type_t *bundle = method_param_type(g, m, j);
            if (!bundle || bundle->kind != TYPE_ARRAY ||
                !bundle->element_type || type_mentions_tp(bundle))
                break;
            for (; ai < call->call.args.count; ai++) {
                zan_ast_node_t *ta = call->call.args.items[ai];
                if (!ta || ta->kind == AST_NAMED_ARG || ta->kind == AST_LAMBDA)
                    continue;
                /* 底层系统交互与数据协议契约 */
                zan_type_t *ta_ty = infer_expr_type(g, ta, locals);
                if (ta_ty && types_concrete_equal(bundle, ta_ty)) {
                    score += 4;
                    continue;
                }
                int es = concrete_arg_score(g, bundle->element_type, ta, locals);
                if (es < 0) return -1;
                score += es;
            }
            break;
        }
        zan_ast_node_t *a = call->call.args.items[ai];
        if (!a) continue;
        if (a->kind == AST_NAMED_ARG) {
            /* 内部辅助实现 */
            continue;
        }
        if (a->kind == AST_LAMBDA) {
            zan_type_t *dp = method_param_type_at(g, m, j, call, recv_expr, locals);
            if (!dp || dp->kind != TYPE_DELEGATE) continue;
            if (a->lambda.params.count != dp->delegate_param_count) return -1;
            zan_ast_node_t *body = a->lambda.body;
            if (!body) continue;
            int mark = locals->count;
            for (int k = 0; k < a->lambda.params.count; k++) {
                zan_ast_node_t *lp = a->lambda.params.items[k];
                zan_type_t *lpt = lp->param.type
                    ? zan_binder_resolve_type(g->binder, lp->param.type)
                    : dp->delegate_param_types[k];
                local_add(locals, lp->param.name, NULL, lpt);
            }
            /* 内部辅助逻辑 */
            int bf = FAM_UNKNOWN;
            if (body->kind == AST_BLOCK) {
                zan_type_t *bt = stmt_lambda_return_type(g, body, locals);
                if (bt) bf = type_family(bt);
            } else {
                bf = expr_family(g, body, locals);
            }
            locals->count = mark;
            int df = type_family(dp->delegate_ret_type);
            if (bf != FAM_UNKNOWN && df != FAM_UNKNOWN) {
                if (bf == df) score += 2;
                else return -1;
            }
            continue;
        }
        zan_type_t *dp0 = method_param_type_at(g, m, j, call, recv_expr, locals);
        zan_symbol_t *mg = arg_method_group(
            g, a, locals,
            (dp0 && dp0->kind == TYPE_DELEGATE) ? dp0->delegate_param_count : -1);
        if (mg) {
            int ms = method_group_score(g, mg, dp0);
            if (ms < 0) return -1;
            score += ms;
            continue;
        }
        /* 内部辅助逻辑 */
        zan_type_t *pt = method_param_type(g, m, j);
        if (!pt) continue;
        if (type_mentions_tp(pt)) {
            /* 内部辅助实现 */
            if (pt->kind != TYPE_TYPE_PARAM) {
                zan_type_t *gat = infer_expr_type(g, a, locals);
                if (gat && types_match_modulo_tp(pt, gat)) score += 3;
            }
            continue;
        }
        /* 内部辅助逻辑 */
        if (pt->kind == TYPE_INTERFACE) continue;
        int s = concrete_arg_score(g, pt, a, locals);
        if (s < 0) return -1;
        score += s;
    }
    return score;
}

/* 内部辅助逻辑 */
static bool ext_receiver_interface_match(zan_type_t *pt, zan_type_t *recv_ty) {
    return pt && recv_ty && pt->kind == TYPE_INTERFACE &&
           pt->type_arg_count == 0 &&
           (recv_ty->kind == TYPE_CLASS || recv_ty->kind == TYPE_INTERFACE) &&
           class_implements_iface(recv_ty->sym, pt->sym);
}

/* 内部辅助实现 */
static int ext_method_score(zan_irgen_t *g, zan_symbol_t *m,
                            zan_type_t *recv_ty, zan_ast_node_t *call,
                            zan_ast_node_t *recv_expr, local_scope_t *locals) {
    zan_ast_list_t *ps = &m->decl->method_decl.params;
    zan_type_t *pt = zan_binder_resolve_type(g->binder, ps->items[0]->param.type);
    int score = 0;
    if (pt && !type_mentions_tp(pt)) {
        if (types_concrete_equal(pt, recv_ty)) score += 4;
        else if (ext_receiver_interface_match(pt, recv_ty)) score += 2;
        else if (!type_mentions_tp(recv_ty)) return -1;
    }
    int as = method_args_score(g, m, call, recv_expr, locals, 1);
    if (as < 0) return -1;
    return score + as;
}

/* 内部辅助逻辑 */
static zan_symbol_t *find_extension_method(zan_irgen_t *g, zan_type_t *recv_ty,
                                           zan_istr_t name, int argc,
                                           zan_ast_node_t *call,
                                           zan_ast_node_t *recv_expr,
                                           local_scope_t *locals) {
    if (!recv_ty) return NULL;
    zan_symbol_t *best = NULL;
    int best_score = -1;
    for (int fi = 0; fi < g->function_count; fi++) {
        zan_symbol_t *m = g->functions[fi].sym;
        if (!m || m->kind != SYM_METHOD || !m->decl ||
            m->decl->kind != AST_METHOD_DECL)
            continue;
        if (m->name.len != name.len ||
            memcmp(m->name.str, name.str, (size_t)name.len) != 0)
            continue;
        zan_ast_list_t *ps = &m->decl->method_decl.params;
        if (ps->count != argc + 1) continue;
        zan_ast_node_t *p0 = ps->items[0];
        if (!p0 || p0->kind != AST_PARAM || !p0->param.is_this) continue;
        zan_type_t *pt = zan_binder_resolve_type(g->binder, p0->param.type);
        if (!pt) continue;
        /* 内部辅助实现 */
        if (pt->kind != TYPE_TYPE_PARAM &&
            !ext_receiver_interface_match(pt, recv_ty)) {
            if (pt->kind != recv_ty->kind) continue;
            if ((pt->kind == TYPE_CLASS || pt->kind == TYPE_STRUCT ||
                 pt->kind == TYPE_INTERFACE || pt->kind == TYPE_ENUM) &&
                pt->sym != recv_ty->sym)
                continue;
        }
        int score = ext_method_score(g, m, recv_ty, call, recv_expr, locals);
        if (score > best_score) {
            best_score = score;
            best = m;
        }
    }
    return (best && best_score >= 0) ? best : NULL;
}

/* 内部辅助实现 */
#define IRGEN_SHARED_MAX_STRING 1048576
#define IRGEN_SHARED_MAX_KEY 1024

static void check_shared_table_width(zan_irgen_t *g, zan_symbol_t *type_sym,
                                     zan_istr_t name, zan_ast_node_t *call) {
    if (!type_sym || !call || call->kind != AST_CALL) return;
    if (type_sym->name.len != 11 ||
        memcmp(type_sym->name.str, "SharedTable", 11) != 0) return;

    long long limit;
    int idx;
    const char *what;
    if (name.len == 12 && memcmp(name.str, "ColumnString", 12) == 0) {
        limit = IRGEN_SHARED_MAX_STRING; idx = 1; what = "string column";
    } else if (name.len == 7 && memcmp(name.str, "KeySize", 7) == 0) {
        limit = IRGEN_SHARED_MAX_KEY; idx = 0; what = "key";
    } else {
        return;
    }
    if (call->call.args.count <= idx) return;
    zan_ast_node_t *arg = call->call.args.items[idx];
    if (!arg || arg->kind != AST_INT_LITERAL) return;
    if (arg->int_val > 0 && arg->int_val <= limit) return;

    zan_diag_emit(g->diag, DIAG_ERROR, arg->loc,
                  "shared table %s width %lld is out of range: a width must be "
                  "between 1 and %lld bytes",
                  what, (long long)arg->int_val, limit);
}

/* 编译器代码生成与运行时系统底层调用契约 */
static zan_symbol_t *resolve_overload_typed(zan_irgen_t *g,
                                            zan_symbol_t *type_sym,
                                            zan_istr_t name,
                                            zan_ast_node_t *call,
                                            local_scope_t *locals) {
    int argc = (call && call->kind == AST_CALL) ? call->call.args.count : 0;
    int type_arg_count = (call && call->kind == AST_CALL)
        ? call->call.type_args.count : 0;
    check_shared_table_width(g, type_sym, name, call);
    zan_symbol_t *best = NULL;
    int best_score = -1;
    int arity_matches = 0;
    /* 内部辅助实现 */
    zan_symbol_t *cls = type_sym;
    while (cls) {
        for (int i = 0; i < cls->member_count; i++) {
            zan_symbol_t *m = cls->members[i];
            if (m->kind != SYM_METHOD || !m->decl ||
                m->decl->kind != AST_METHOD_DECL) continue;
            if (m->name.len != name.len ||
                memcmp(m->name.str, name.str, name.len) != 0) continue;
            /* 内部辅助实现 */
            if (type_arg_count > 0 &&
                m->decl->method_decl.type_params.count != type_arg_count)
                continue;
            if (!method_accepts_arity(m, argc)) continue;
            arity_matches++;
            int score = method_args_score(g, m, call, NULL, locals, 0);
            if (score > best_score) {
                best_score = score;
                best = m;
            }
        }
        zan_symbol_t *base = (cls->type && cls->type->base_type)
            ? cls->type->base_type->sym : NULL;
        cls = (base && base != cls) ? base : NULL;
    }
    if (best && best_score >= 0) return best;
    if (arity_matches > 0) {
        /* 模块核心语义抽象与接口调用契约 */
        zan_diag_emit(g->diag, DIAG_ERROR,
                      call ? call->loc : (zan_loc_t){0},
                      "no overload of '%.*s.%.*s' matches argument type(s)",
                      (int)type_sym->name.len, type_sym->name.str,
                      (int)name.len, name.str);
        return NULL;
    }
    return resolve_overload(type_sym, name, argc, type_arg_count);
}

/* 内部辅助实现 */
static zan_symbol_t *resolve_op_overload(zan_irgen_t *g,
                                        zan_symbol_t *type_sym,
                                        zan_istr_t name,
                                        zan_ast_node_t *call,
                                        local_scope_t *locals) {
    int argc = (call && call->kind == AST_CALL) ? call->call.args.count : 0;
    zan_symbol_t *best = NULL;
    zan_symbol_t *first_variadic = NULL;
    int best_score = -1;
    for (int i = 0; i < type_sym->member_count; i++) {
        zan_symbol_t *m = type_sym->members[i];
        if (m->kind != SYM_METHOD || !m->decl ||
            m->decl->kind != AST_METHOD_DECL) continue;
        if (m->name.len != name.len ||
            memcmp(m->name.str, name.str, (size_t)name.len) != 0) continue;
        zan_ast_list_t *ps = &m->decl->method_decl.params;
        if (ps->count < 1) continue;
        /* 内部辅助实现 */
        int is_static = (m->modifiers & MOD_STATIC) != 0;
        int p0 = is_static ? 1 : 0; /* 底层系统交互与数据协议契约 */
        int variadic = method_is_params_variadic(m);
        int score = 0;
        if (variadic) {
            /* 内部辅助实现 */
            int fixed = ps->count - 1 - p0;
            if (argc < fixed) continue;
            if (!first_variadic) first_variadic = m;
            score = method_args_score(g, m, call, NULL, locals, p0);
            if (score < 0 && fixed > 0) continue;
            /* 模块核心语义抽象与接口调用契约 */
            score = 0;
            zan_type_t *pt = zan_binder_resolve_type(g->binder,
                ps->items[ps->count - 1]->param.type);
            zan_type_t *et = pt ? pt->element_type : NULL;
            for (int ai = fixed; ai < argc; ai++) {
                zan_type_t *at = infer_expr_type(g, call->call.args.items[ai], locals);
                if (!et || !at) continue;
                if (types_concrete_equal(et, at)) {
                    score += 4;
                    continue;
                }
                int ef = type_family(et), af = type_family(at);
                if (ef == FAM_UNKNOWN || af == FAM_UNKNOWN) continue;
                if (ef == FAM_FLOAT && af == FAM_INT) continue;
                score = -1;
                break;
            }
        } else {
            if (ps->count != argc + p0) continue;
            score = method_args_score(g, m, call, NULL, locals, p0);
        }
        if (score > best_score) {
            best_score = score;
            best = m;
        }
    }
    if (best && best_score >= 0) return best;
    return first_variadic;
}

/* 内部辅助实现 */
static int op_index_param_offset(zan_symbol_t *m) {
    return (m->modifiers & MOD_STATIC) ? 1 : 0;
}

static zan_type_t *infer_expr_type_raw(zan_irgen_t *g, zan_ast_node_t *e,
                                       local_scope_t *locals);

/* 内部辅助实现 */
/* 内部辅助实现 */
#define INFER_CACHE_SLOTS 65536   /* power of two */

typedef struct {
    zan_ast_node_t *node;
    zan_type_t *type;
} infer_cache_slot_t;

static infer_cache_slot_t g_infer_cache[INFER_CACHE_SLOTS];
static struct {
    void *locals;
    int lcount;
    unsigned gen;
    void *inst;
    void *mtps;
    void *mbind;
    void *tsym;
    bool live;
} g_infer_ctx;

static infer_cache_slot_t *infer_cache_slot(zan_irgen_t *g, zan_ast_node_t *e,
                                            local_scope_t *locals) {
    if (!e || !locals) return NULL;
    if (!g_infer_ctx.live || g_infer_ctx.locals != (void *)locals ||
        g_infer_ctx.lcount != locals->count || g_infer_ctx.gen != g_local_gen ||
        g_infer_ctx.inst != (void *)g->cur_inst ||
        g_infer_ctx.mtps != (void *)g->cur_mtps ||
        g_infer_ctx.mbind != (void *)g->cur_mbind ||
        g_infer_ctx.tsym != (void *)g->current_type_sym) {
        memset(g_infer_cache, 0, sizeof(g_infer_cache));
        g_infer_ctx.locals = (void *)locals;
        g_infer_ctx.lcount = locals->count;
        g_infer_ctx.gen = g_local_gen;
        g_infer_ctx.inst = (void *)g->cur_inst;
        g_infer_ctx.mtps = (void *)g->cur_mtps;
        g_infer_ctx.mbind = (void *)g->cur_mbind;
        g_infer_ctx.tsym = (void *)g->current_type_sym;
        g_infer_ctx.live = true;
    }
    size_t h = ((size_t)(uintptr_t)e >> 4) * 2654435761u;
    return &g_infer_cache[h & (INFER_CACHE_SLOTS - 1)];
}

/* 核心系统底层抽象与内存语义契约 */
static void infer_cache_invalidate(void) { g_infer_ctx.live = false; }

/* 编译器代码生成与运行时系统底层调用契约 */
static zan_type_t *null_cond_result_type(zan_irgen_t *g, zan_ast_node_t *e,
                                         zan_type_t *t) {
    zan_ast_node_t *m = NULL;
    if (e->kind == AST_MEMBER_ACCESS) m = e;
    else if (e->kind == AST_CALL && e->call.callee) m = e->call.callee;
    if (!m || m->kind != AST_MEMBER_ACCESS || !m->member.null_cond) return t;
    return zan_binder_make_nullable_type(g->binder, t);
}

static zan_type_t *infer_expr_type_uncached(zan_irgen_t *g, zan_ast_node_t *e,
                                            local_scope_t *locals) {
    zan_type_t *t = null_cond_result_type(g, e, infer_expr_type_raw(g, e, locals));
    if (!t || !g->cur_inst || type_is_concrete(t)) return t;
    /* 内部辅助逻辑 */
    if (t->kind == TYPE_DELEGATE) return t;
    /* 内部辅助实现 */
    if (t->kind == TYPE_TYPE_PARAM) return t;
    return subst_type_param_deep(g, t, g->cur_inst);
}

/* 内部辅助逻辑 */
static int g_infer_depth;
static bool g_infer_bailed;

static zan_type_t *infer_expr_type(zan_irgen_t *g, zan_ast_node_t *e,
                                   local_scope_t *locals) {
    infer_cache_slot_t *slot = infer_cache_slot(g, e, locals);
    if (slot && slot->node == e) return slot->type;
    if (g_infer_depth >= ZAN_MAX_INFER_DEPTH) {
        if (!g_infer_bailed) {
            g_infer_bailed = true;
            zan_diag_emit(g->diag, DIAG_ERROR, e->loc,
                          "expression is nested too deeply to type "
                          "(inference recursed %d levels); split it into "
                          "statements", ZAN_MAX_INFER_DEPTH);
        }
        return NULL;
    }
    g_infer_depth++;
    zan_type_t *t = infer_expr_type_uncached(g, e, locals);
    g_infer_depth--;
    /* 内部辅助逻辑 */
    slot = infer_cache_slot(g, e, locals);
    if (slot) {
        slot->node = e;
        slot->type = t;
    }
    return t;
}

static zan_type_t *infer_expr_type_raw(zan_irgen_t *g, zan_ast_node_t *e,
                                       local_scope_t *locals) {
    if (!e) return NULL;
    switch (e->kind) {
    /* 内部辅助逻辑 */
    case AST_STRING_LITERAL:
        return g->binder ? g->binder->type_string : NULL;
    /* 内部辅助逻辑 */
    case AST_INT_LITERAL:
        /* 内部辅助实现 */
        if (!g->binder) return NULL;
        switch (e->lit_suffix) {
        case 1:
            return g->binder->type_long;
        case 2:
            if (e->int_val < 0 || e->int_val > 4294967295LL)
                return g->binder->type_ulong;
            return g->binder->type_uint;
        case 3:
            return g->binder->type_ulong;
        default:
            if ((e->lit_radix == 2 || e->lit_radix == 8
                 || e->lit_radix == 16)
                && e->int_val >= 0 && e->int_val <= 4294967295LL) {
                return g->binder->type_int;
            }
            if (e->int_val < -2147483648LL || e->int_val > 2147483647LL)
                return g->binder->type_long;
            return g->binder->type_int;
        }
    case AST_FLOAT_LITERAL:
        return g->binder ? g->binder->type_double : NULL;
    case AST_CHAR_LITERAL:
        return g->binder ? g->binder->type_char : NULL;
    case AST_BOOL_LITERAL:
        return g->binder ? g->binder->type_bool : NULL;
    case AST_IDENTIFIER: {
        local_var_t *l = local_find(locals, e->ident.name);
        if (l) return l->type;
        if (g->current_type_sym) {
            zan_symbol_t *fs = get_field_sym(g->current_type_sym, e->ident.name);
            /* `item` (implicit `this */
            if (fs) return concretize(g, fs->type);
        }
        return NULL;
    }
    case AST_THIS_EXPR:
        return g->current_type_sym ? g->current_type_sym->type : NULL;
    case AST_BASE_EXPR:
        /* `base */
        if (g->current_type_sym && g->current_type_sym->type
            && g->current_type_sym->type->base_type) {
            return g->current_type_sym->type->base_type;
        }
        return g->current_type_sym ? g->current_type_sym->type : NULL;
    case AST_AWAIT_EXPR:
        /* 编译器代码生成与运行时系统底层调用契约 */
        return infer_expr_type(g, e->await_expr.expr, locals);
    case AST_QUERY_EXPR: {
        /* 内部辅助实现 */
        zan_type_t *src_ty = infer_expr_type(g, e->query.source, locals);
        zan_type_t *elem = container_elem_type(src_ty);
        if (!elem) elem = g->binder->type_int;
        int mark = locals->count;
        local_add(locals, e->query.var, NULL, elem);
        for (int ci = 0; ci < e->query.clauses.count; ci++) {
            zan_ast_node_t *cl = e->query.clauses.items[ci];
            switch (cl->kind) {
            case AST_QUERY_LET: {
                zan_type_t *lt =
                    infer_expr_type(g, cl->query_clause.expr, locals);
                if (!lt) lt = g->binder->type_int;
                local_add(locals, cl->query_clause.name, NULL, lt);
                break;
            }
            case AST_QUERY_JOIN: {
                zan_type_t *jty =
                    infer_expr_type(g, cl->query_clause.source, locals);
                zan_type_t *je = container_elem_type(jty);
                if (!je) je = g->binder->type_int;
                if (cl->query_clause.into.len > 0)
                    local_add(locals, cl->query_clause.into, NULL,
                              zan_binder_make_list_type(g->binder, je));
                else
                    local_add(locals, cl->query_clause.name, NULL, je);
                break;
            }
            default:
                break; /* 核心系统底层抽象与内存语义契约 */
            }
        }
        if (e->query.group_expr) {
            zan_type_t *ge = infer_expr_type(g, e->query.group_expr, locals);
            if (!ge) ge = elem;
            zan_type_t *grp = zan_binder_make_grouping_type(g->binder, ge);
            if (e->query.group_into.len > 0) {
                /* 核心系统底层抽象与内存语义契约 */
                int mark2 = locals->count;
                local_add(locals, e->query.group_into, NULL, grp);
                zan_type_t *sel = infer_expr_type(g, e->query.select, locals);
                locals->count = mark2;
                locals->count = mark;
                if (!sel) sel = ge;
                return zan_binder_make_list_type(g->binder, sel);
            }
            locals->count = mark;
            return zan_binder_make_list_type(g->binder, grp);
        }
        zan_type_t *sel = infer_expr_type(g, e->query.select, locals);
        locals->count = mark;
        if (!sel) sel = elem;
        return zan_binder_make_list_type(g->binder, sel);
    }
    case AST_MEMBER_ACCESS: {
        /* 核心系统底层抽象与内存语义契约 */
        {
            zan_type_t *rt = infer_expr_type(g, e->member.object, locals);
            if (zan_refl_is_typeinfo(rt)) {
                zan_type_t *mt = zan_refl_member_type(g->binder, rt,
                                                      e->member.name);
                if (mt) return mt;
            }
        }
        /* 核心系统底层抽象与内存语义契约 */
        if (e->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, e->member.object->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder,
                                                 e->member.object->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *fs = get_field_sym(cs, e->member.name);
                if (fs) return fs->type;
            }
        }
        /* 内部辅助实现 */
        if (e->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, e->member.object->ident.name)) {
            zan_symbol_t *es = zan_binder_lookup(g->binder,
                                                 e->member.object->ident.name);
            if (es && es->kind == SYM_ENUM) {
                for (int ei = 0; ei < es->member_count; ei++) {
                    zan_symbol_t *em = es->members[ei];
                    if (em && em->kind == SYM_ENUM_MEMBER &&
                        em->name.len == e->member.name.len &&
                        memcmp(em->name.str, e->member.name.str,
                               (size_t)e->member.name.len) == 0)
                        return es->type;
                }
            }
        }
        /* 核心系统底层抽象与内存语义契约 */
        if (e->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, e->member.object->ident.name)) {
            zan_istr_t on = e->member.object->ident.name;
            zan_istr_t mn = e->member.name;
            bool is_scalar = (on.len == 3 && memcmp(on.str, "int", 3) == 0) ||
                (on.len == 4 && (memcmp(on.str, "uint", 4) == 0 ||
                                 memcmp(on.str, "long", 4) == 0 ||
                                 memcmp(on.str, "byte", 4) == 0 ||
                                 memcmp(on.str, "char", 4) == 0)) ||
                (on.len == 5 && (memcmp(on.str, "ulong", 5) == 0 ||
                                 memcmp(on.str, "short", 5) == 0 ||
                                 memcmp(on.str, "sbyte", 5) == 0 ||
                                 memcmp(on.str, "float", 5) == 0)) ||
                (on.len == 6 && (memcmp(on.str, "ushort", 6) == 0 ||
                                 memcmp(on.str, "double", 6) == 0));
            if (is_scalar) {
                bool is_const = (mn.len == 8 &&
                                 (memcmp(mn.str, "MaxValue", 8) == 0 ||
                                  memcmp(mn.str, "MinValue", 8) == 0)) ||
                    (mn.len == 3 && memcmp(mn.str, "NaN", 3) == 0) ||
                    (mn.len == 16 &&
                     (memcmp(mn.str, "PositiveInfinity", 16) == 0 ||
                      memcmp(mn.str, "NegativeInfinity", 16) == 0)) ||
                    (mn.len == 7 && memcmp(mn.str, "Epsilon", 7) == 0);
                if (is_const) {
                    if (on.len == 3 && memcmp(on.str, "int", 3) == 0)
                        return g->binder->type_int;
                    if (on.len == 4 && memcmp(on.str, "uint", 4) == 0)
                        return g->binder->type_uint;
                    if (on.len == 4 && memcmp(on.str, "long", 4) == 0)
                        return g->binder->type_long;
                    if (on.len == 5 && memcmp(on.str, "ulong", 5) == 0)
                        return g->binder->type_ulong;
                    if (on.len == 5 && memcmp(on.str, "short", 5) == 0)
                        return g->binder->type_short;
                    if (on.len == 6 && memcmp(on.str, "ushort", 6) == 0)
                        return g->binder->type_ushort;
                    if (on.len == 4 && memcmp(on.str, "byte", 4) == 0)
                        return g->binder->type_byte;
                    if (on.len == 5 && memcmp(on.str, "sbyte", 5) == 0)
                        return g->binder->type_sbyte;
                    if (on.len == 4 && memcmp(on.str, "char", 4) == 0)
                        return g->binder->type_char;
                    if (on.len == 5 && memcmp(on.str, "float", 5) == 0)
                        return g->binder->type_float;
                    if (on.len == 6 && memcmp(on.str, "double", 6) == 0)
                        return g->binder->type_double;
                }
            }
        }
        zan_type_t *ot = infer_expr_type(g, e->member.object, locals);
        /* Dict */
        if (ot && type_named(ot, "Dict", 4)) {
            if (e->member.name.len == 4 &&
                memcmp(e->member.name.str, "Keys", 4) == 0)
                return zan_binder_make_list_type(g->binder, dict_key_type(g, ot));
            if (e->member.name.len == 6 &&
                memcmp(e->member.name.str, "Values", 6) == 0) {
                zan_type_t *vt = dict_value_type(ot);
                if (vt) return zan_binder_make_list_type(g->binder, vt);
            }
        }
        if (is_span_type(ot) && e->member.name.len == 6 &&
            memcmp(e->member.name.str, "Length", 6) == 0)
            return g->binder->type_int;
        /* 底层系统交互与数据协议契约 */
        if (ot && ot->kind == TYPE_NULLABLE) {
            if (e->member.name.len == 8 &&
                memcmp(e->member.name.str, "HasValue", 8) == 0)
                return g->binder->type_bool;
            if (e->member.name.len == 5 &&
                memcmp(e->member.name.str, "Value", 5) == 0)
                return ot->element_type;
        }
        if (ot && ot->sym) {
            zan_symbol_t *fs = get_field_sym(ot->sym, e->member.name);
            if (fs) {
                /* 内部辅助逻辑 */
                zan_type_t *ft = subst_type_param_deep(g, fs->type, ot);
                if (ft && ft->kind == TYPE_TYPE_PARAM) ft = concretize(g, ft);
                return ft;
            }
        }
        /* 底层系统交互与数据协议契约 */
        if (ot) {
            const char *bt = NULL;
            if (ot->kind == TYPE_STRING) bt = "string";
            else if (type_named(ot, "List", 4)) bt = "List";
            else if (type_named(ot, "Dict", 4)) bt = "Dict";
            else if (type_named(ot, "StringBuilder", 13)) bt = "StringBuilder";
            if (bt && zan_builtin_member_kind(bt, e->member.name.str,
                                              (int)e->member.name.len) == 'P') {
                const char *res = zan_builtin_member_result(
                    bt, e->member.name.str, (int)e->member.name.len);
                if (res) {
                    if (strcmp(res, "int") == 0) return g->binder->type_int;
                    if (strcmp(res, "long") == 0) return g->binder->type_long;
                    if (strcmp(res, "bool") == 0) return g->binder->type_bool;
                    if (strcmp(res, "double") == 0) return g->binder->type_double;
                    if (strcmp(res, "string") == 0) return g->binder->type_string;
                }
            }
            if (ot->kind == TYPE_ARRAY && e->member.name.len == 6 &&
                memcmp(e->member.name.str, "Length", 6) == 0)
                return g->binder->type_int;
            /* 底层系统交互与数据协议契约 */
            if (ot->kind == TYPE_ARRAY && e->member.name.len == 5 &&
                memcmp(e->member.name.str, "Count", 5) == 0)
                return g->binder->type_int;
        }
        return NULL;
    }
    case AST_INDEX: {
        zan_type_t *ot = infer_expr_type(g, e->index.object, locals);
        /* 内部辅助逻辑 */
        if (ot && (ot->kind == TYPE_CLASS || ot->kind == TYPE_STRUCT) && ot->sym) {
            zan_istr_t op_istr = {(char *)"op_index", 8};
            zan_ast_node_t *op_call = zan_ast_new(g->arena, AST_CALL, e->loc);
            op_call->call.callee = NULL;
            zan_ast_list_init(&op_call->call.args);
            zan_ast_list_init(&op_call->call.type_args);
            zan_ast_list_push(&op_call->call.args, e->index.index, g->arena);
            zan_symbol_t *op = resolve_op_overload(g, ot->sym,
                                                   op_istr, op_call, locals);
            if (op) return op->type;
        }
        /* 编译器代码生成与运行时系统底层调用契约 */
        if (ot && type_named(ot, "Dict", 4) &&
            ot->type_arg_count == 2)
            return ot->type_args[1];
        /* 模块核心语义抽象与接口调用契约 */
        if (ot && ot->kind == TYPE_STRING) return g->binder->type_char;
        return container_elem_type(ot);
    }
    case AST_CALL: {
        /* 内部辅助逻辑 */
        zan_ast_node_t *callee = e->call.callee;
        if (!callee) return NULL;
        /* EnumType */
        if (callee->kind == AST_MEMBER_ACCESS &&
            callee->member.object->kind == AST_IDENTIFIER &&
            callee->member.name.len == 8 &&
            memcmp(callee->member.name.str, "TryParse", 8) == 0) {
            zan_symbol_t *es = zan_binder_lookup(g->binder,
                callee->member.object->ident.name);
            if (es && es->kind == SYM_ENUM) {
                return g->binder->type_bool;
            }
        }
        /* 核心系统底层抽象与内存语义契约 */
        if (callee->kind == AST_MEMBER_ACCESS) {
            zan_type_t *rt = infer_expr_type(g, callee->member.object, locals);
            if (zan_refl_is_typeinfo(rt) ||
                (zan_refl_is_reflectable(rt) &&
                 (!rt->sym || !get_method_sym(rt->sym, callee->member.name)))) {
                zan_type_t *mt = zan_refl_member_type(g->binder, rt,
                                                      callee->member.name);
                if (mt) return mt;
            }
        }
        /* 内部辅助逻辑 */
        {
            zan_type_t *ct = infer_expr_type_raw(g, callee, locals);
            if (ct && (ct->kind == TYPE_CLASS || ct->kind == TYPE_STRUCT) && ct->sym) {
                zan_istr_t op_istr = {(char *)"op_call", 7};
                zan_symbol_t *op = resolve_op_overload(g, ct->sym,
                                                       op_istr, e, locals);
                if (op) return op->type;
            }
        }
        /* 内部辅助逻辑 */
        if (callee->kind == AST_IDENTIFIER || callee->kind == AST_MEMBER_ACCESS) {
            zan_type_t *dt = infer_expr_type_raw(g, callee, locals);
            if (dt && dt->kind == TYPE_DELEGATE)
                return dt->delegate_ret_type;
        }
        /* 内部辅助逻辑 */
        if (callee->kind == AST_MEMBER_ACCESS) {
            zan_istr_t mm = callee->member.name;
            if (mm.len == 6 && memcmp(mm.str, "AsSpan", 6) == 0) {
                zan_type_t *aot = infer_expr_type(g, callee->member.object, locals);
                if (aot && aot->kind == TYPE_ARRAY && aot->element_type)
                    return zan_binder_make_span_type(g->binder, aot->element_type);
                if (is_span_type(aot)) return aot;
            }
            if (mm.len == 5 && memcmp(mm.str, "Slice", 5) == 0) {
                zan_type_t *sot = infer_expr_type(g, callee->member.object, locals);
                if (is_span_type(sot)) return sot;
            }
            if (mm.len == 7 && memcmp(mm.str, "ToBytes", 7) == 0) {
                zan_type_t *bot = infer_expr_type(g, callee->member.object, locals);
                if (bot && bot->kind == TYPE_STRING)
                    return zan_binder_make_array_type(g->binder, g->binder->type_byte);
            }
            if (mm.len == 17 && memcmp(mm.str, "GetValueOrDefault", 17) == 0) {
                zan_type_t *nvt = infer_expr_type(g, callee->member.object, locals);
                if (nvt && nvt->kind == TYPE_NULLABLE) return nvt->element_type;
            }
            if (mm.len == 5 && memcmp(mm.str, "ToStr", 5) == 0) {
                zan_type_t *bot = infer_expr_type(g, callee->member.object, locals);
                if (bot && bot->kind == TYPE_ARRAY) return g->binder->type_string;
            }
            if ((mm.len == 9 && memcmp(mm.str, "Substring", 9) == 0) ||
                (mm.len == 8 && memcmp(mm.str, "ToString", 8) == 0) ||
                (mm.len == 4 && memcmp(mm.str, "Trim", 4) == 0) ||
                (mm.len == 7 && memcmp(mm.str, "Replace", 7) == 0) ||
                (mm.len == 7 && memcmp(mm.str, "ToUpper", 7) == 0) ||
                (mm.len == 7 && memcmp(mm.str, "ToLower", 7) == 0))
                return g->binder->type_string;
            if (mm.len == 5 && memcmp(mm.str, "Split", 5) == 0) {
                zan_type_t *ot = infer_expr_type(g, callee->member.object, locals);
                if (ot && ot->kind == TYPE_STRING)
                    return zan_binder_make_list_type(g->binder, g->binder->type_string);
            }
            /* string */
            if (callee->member.object->kind == AST_IDENTIFIER &&
                !local_find(locals, callee->member.object->ident.name)) {
                zan_istr_t on = callee->member.object->ident.name;
                int sr = on.len == 6 && (memcmp(on.str, "string", 6) == 0 ||
                                         memcmp(on.str, "String", 6) == 0);
                if (sr && ((mm.len == 4 && memcmp(mm.str, "Join", 4) == 0) ||
                           (mm.len == 6 && memcmp(mm.str, "Format", 6) == 0)))
                    return g->binder->type_string;
            }
        }
        if (callee->kind == AST_IDENTIFIER) {
            /* 模块核心语义抽象与接口调用契约 */
            if (g->current_type_sym) {
                /* 内部辅助实现 */
            zan_symbol_t *m = resolve_overload_typed(g, g->current_type_sym,
                                                     callee->ident.name, e, locals);
            if (!m) m = get_method_sym(g->current_type_sym, callee->ident.name);
                /* 内部辅助逻辑 */
                if (m) return method_ret_type_at(g, m, e, NULL, locals);
            }
            zan_symbol_t *gf = zan_binder_lookup(g->binder, callee->ident.name);
            if (gf && gf->kind == SYM_METHOD)
                return method_ret_type_at(g, gf, e, NULL, locals);
            return NULL;
        }
        if (callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *obj = callee->member.object;
            /* static: ClassName */
            if (obj->kind == AST_IDENTIFIER && !local_find(locals, obj->ident.name)) {
                zan_symbol_t *ts = zan_binder_lookup(g->binder, obj->ident.name);
                if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT)) {
                    zan_symbol_t *m = resolve_overload_typed(g, ts, callee->member.name, e, locals);
                    if (!m) m = get_method_sym(ts, callee->member.name);
                    if (m) {
                        zan_type_t *gr = generic_method_ret(g, m, e, locals);
                        return gr ? gr : m->type;
                    }
                }
            }
            /* 核心系统底层抽象与内存语义契约 */
            if (obj->kind == AST_IDENTIFIER && !local_find(locals, obj->ident.name)) {
                char cls[64];
                int cn = (int)obj->ident.name.len;
                if (cn > 0 && cn < (int)sizeof(cls)) {
                    memcpy(cls, obj->ident.name.str, (size_t)cn);
                    cls[cn] = 0;
                    const zan_builtin_type_t *bt = zan_builtin_find(cls);
                    const char *res = bt && bt->is_static
                        ? zan_builtin_member_result(cls, callee->member.name.str,
                                                    (int)callee->member.name.len)
                        : NULL;
                    if (res) {
                        if (strcmp(res, "string") == 0) return g->binder->type_string;
                        if (strcmp(res, "int") == 0) return g->binder->type_int;
                        if (strcmp(res, "long") == 0) return g->binder->type_long;
                        if (strcmp(res, "double") == 0) return g->binder->type_double;
                        if (strcmp(res, "bool") == 0) return g->binder->type_bool;
                        if (strcmp(res, "List<string>") == 0)
                            return zan_binder_make_list_type(g->binder,
                                                             g->binder->type_string);
                    }
                }
            }
            /* static: Namespace */
            if (obj->kind == AST_MEMBER_ACCESS && is_name_path(obj)) {
                zan_ast_node_t *head = name_path_head(obj);
                if (head && !local_find(locals, head->ident.name)) {
                    zan_symbol_t *ts = zan_binder_lookup(g->binder, obj->member.name);
                    if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT)) {
                        zan_symbol_t *m = get_method_sym(ts, callee->member.name);
                        if (m) {
                            zan_type_t *gr = generic_method_ret(g, m, e, locals);
                            return gr ? gr : m->type;
                        }
                    }
                }
            }
            /* instance: <expr> */
            zan_type_t *rt = infer_expr_type(g, obj, locals);
            if (rt && rt->sym) {
                /* 底层系统交互与数据协议契约 */
                zan_symbol_t *m = resolve_overload_typed(g, rt->sym,
                    callee->member.name, e, locals);
                if (!m) m = get_method_sym(rt->sym, callee->member.name);
                if (m) {
                    /* 内部辅助逻辑 */
                    /* 编译器代码生成与运行时系统底层调用契约 */
                    zan_type_t *mt = method_ret_type_at(g, m, e, obj, locals);
                    if (!mt) mt = m->type;
                    mt = subst_type_param_deep(g, mt, rt);
                    if (mt && mt->kind == TYPE_TYPE_PARAM) mt = concretize(g, mt);
                    return mt;
                }
            }
            /* 核心系统底层抽象与内存语义契约 */
            zan_symbol_t *xm = find_extension_method(g, rt, callee->member.name,
                                                     e->call.args.count,
                                                     e, obj, locals);
            if (xm) return method_ret_type_at(g, xm, e, obj, locals);
        }
        return NULL;
    }
    case AST_NEW_EXPR:
        /* 内部辅助实现 */
        if (e->new_expr.is_array) {
            if (!e->new_expr.type) return NULL;
            zan_type_t *at = resolve_type_ctx(g, e->new_expr.type);
            if (at && at->kind == TYPE_ARRAY) return at;
            return NULL;
        }
        /* `FactoryCall( */
        if (!e->new_expr.type && e->new_expr.call_init)
            return infer_expr_type(g, e->new_expr.call_init, locals);
        return resolve_type_ctx(g, e->new_expr.type);
    case AST_COLL_INIT:
        /* 内部辅助逻辑 */
        return NULL;
    case AST_UNARY:
        /* 内部辅助逻辑 */
        if (e->unary.op == TK_BANG)
            return g->binder ? g->binder->type_bool : NULL;
        return infer_expr_type(g, e->unary.operand, locals);
    case AST_POSTFIX_UNARY:
        /* 模块核心语义抽象与接口调用契约 */
        return infer_expr_type(g, e->unary.operand, locals);
    case AST_BINARY:
        /* 内部辅助逻辑 */
        if (e->binary.op == TK_PLUS && is_string_expr(g, e, locals))
            return g->binder->type_string;
        switch (e->binary.op) {
        case TK_PLUS: case TK_MINUS: case TK_STAR: case TK_SLASH:
        case TK_PERCENT: case TK_AMP: case TK_PIPE: case TK_CARET:
        case TK_LESS_LESS: case TK_GREATER_GREATER:
        case TK_GREATER_GREATER_GREATER: {
            zan_type_t *lt = infer_expr_type(g, e->binary.left, locals);
            zan_type_t *rt = infer_expr_type(g, e->binary.right, locals);
            /* 内部辅助逻辑 */
            if (lt && lt->kind == TYPE_NULLABLE) return lt;
            if (rt && rt->kind == TYPE_NULLABLE) return rt;
            if ((lt && lt->kind == TYPE_ULONG) || (rt && rt->kind == TYPE_ULONG))
                return g->binder->type_ulong;
            /* 内部辅助逻辑 */
            if (lt && rt && lt->kind == rt->kind) return lt;
            if (lt && !rt) return lt;
            if (rt && !lt) return rt;
            return NULL;
        }
        case TK_EQ_EQ: case TK_BANG_EQ: case TK_LESS:
        case TK_LESS_EQ: case TK_GREATER: case TK_GREATER_EQ:
        case TK_AMP_AMP: case TK_PIPE_PIPE:
            return g->binder ? g->binder->type_bool : NULL;
        default:
            return NULL;
        }
    /* 内部辅助逻辑 */
    case AST_TYPEOF_EXPR:
        return g->binder->type_typeinfo;
    case AST_STRING_INTERP:
        return g->binder->type_string;
    case AST_CAST_EXPR:
        return resolve_type_ctx(g, e->cast.type);
    case AST_CONDITIONAL:
        /* 内部辅助实现 */
        return infer_expr_type(g, e->conditional.then_expr, locals);
    case AST_TUPLE_EXPR: {
        /* (a, b, */
        if (!g->binder) return NULL;
        int n = e->tuple_expr.items.count;
        zan_type_t **elems = (zan_type_t **)zan_arena_alloc(
            g->arena, sizeof(zan_type_t *) * (size_t)(n > 0 ? n : 1));
        for (int i = 0; i < n; i++) {
            elems[i] = infer_expr_type(g, e->tuple_expr.items.items[i], locals);
            if (!elems[i]) return NULL;
        }
        return zan_binder_make_tuple_type(g->binder, elems, n);
    }
    case AST_SWITCH_EXPR:
        /* `x switch { */
        if (e->switch_expr.arms.count == 0) return NULL;
        return infer_expr_type(g, e->switch_expr.arms.items[0]->switch_arm.result,
                               locals);
    case AST_WITH_EXPR:
        /* 底层系统交互与数据协议契约 */
        return infer_expr_type(g, e->with_expr.expr, locals);
    default:
        return NULL;
    }
}

/* 内部辅助逻辑 */
static bool expr_is_ulong(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && t->kind == TYPE_ULONG;
}

/* 内部辅助逻辑 */
static bool expr_is_uint(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && t->kind == TYPE_UINT;
}

/* 内部辅助逻辑 */
static bool expr_is_longish(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && (t->kind == TYPE_LONG || t->kind == TYPE_ULONG);
}

/* 模块核心语义抽象与接口调用契约 */
static bool expr_is_int32(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && t->kind == TYPE_INT;
}

/* 底层系统交互与数据协议契约 */
static bool expr_is_bool(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && t->kind == TYPE_BOOL;
}

/* 内部辅助实现 */
static bool expr_is_unsigned_int(zan_irgen_t *g, zan_ast_node_t *e,
                                 local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    if (!t) return false;
    if (t->kind == TYPE_ULONG || t->kind == TYPE_UINT ||
        t->kind == TYPE_USHORT || t->kind == TYPE_BYTE)
        return true;
    return false;
}

/* 内部辅助逻辑 */
static bool expr_is_char(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && t->kind == TYPE_CHAR;
}

/* 底层系统交互与数据协议契约 */
static zan_symbol_t *expr_class_sym(zan_irgen_t *g, zan_ast_node_t *e,
                                    local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    /* 内部辅助逻辑 */
    if (t && t->kind == TYPE_TYPE_PARAM) t = concretize(g, t);
    if (t && (t->kind == TYPE_CLASS || t->kind == TYPE_STRUCT)) return t->sym;
    return NULL;
}

/* 返回the field symbol when `e` is a read of a weak instance field */
static zan_symbol_t *weak_field_read_sym(zan_irgen_t *g, zan_ast_node_t *e,
                                         local_scope_t *locals) {
    zan_symbol_t *field = NULL;
    if (!e) return NULL;
    if (e->kind == AST_IDENTIFIER) {
        if (local_find(locals, e->ident.name) || !g->current_type_sym)
            return NULL;
        if (get_field_index(g->current_type_sym, e->ident.name) >= 0)
            field = get_field_sym(g->current_type_sym, e->ident.name);
        if (field && field_member_is_static(field)) return NULL;
    } else if (e->kind == AST_MEMBER_ACCESS && !e->member.null_cond) {
        zan_symbol_t *owner = expr_class_sym(g, e->member.object, locals);
        if (!owner) return NULL;
        if (get_field_index(owner, e->member.name) >= 0)
            field = get_field_sym(owner, e->member.name);
        if (field && field_member_is_static(field)) return NULL;
    }
    return field && (field->modifiers & MOD_WEAK) ? field : NULL;
}

/* 底层系统交互与数据协议契约 */
static void emit_weak_read_guard(zan_irgen_t *g, zan_ast_node_t *read_expr,
                                 LLVMValueRef value, zan_loc_t loc,
                                 local_scope_t *locals) {
    /* 内部辅助逻辑 */
    zan_type_t *rt = infer_expr_type(g, read_expr, locals);
    bool reflike = rt && (rt->kind == TYPE_OBJECT || rt->kind == TYPE_CLASS ||
                          rt->kind == TYPE_INTERFACE ||
                          rt->kind == TYPE_TYPE_PARAM ||
                          rt->kind == TYPE_ERROR);
    if (reflike && value &&
        LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMPointerTypeKind) {
        LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, value,
                                        LLVMConstNull(LLVMTypeOf(value)),
                                        "recv.null");
        char msg[256];
        if (read_expr && read_expr->kind == AST_IDENTIFIER)
            snprintf(msg, sizeof(msg),
                     "null reference: receiver '%.*s' is null",
                     (int)read_expr->ident.name.len, read_expr->ident.name.str);
        else
            snprintf(msg, sizeof(msg),
                     "null reference where an object is required (member access)");
        emit_runtime_check(g, is_null, loc, msg);
    }
    zan_symbol_t *field = weak_field_read_sym(g, read_expr, locals);
    if (!field || !value ||
        LLVMGetTypeKind(LLVMTypeOf(value)) != LLVMPointerTypeKind)
        return;
    LLVMTypeRef ptr_type = LLVMTypeOf(value);
    LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, value,
                                    LLVMConstNull(ptr_type), "weak.null");
    char msg[256];
    snprintf(msg, sizeof(msg),
             "null reference: weak field '%.*s' was cleared",
             (int)field->name.len, field->name.str);
    emit_runtime_check(g, is_null, loc, msg);
}

/* 内部辅助逻辑 */
static LLVMValueRef emit_guarded_member_object(zan_irgen_t *g,
                                               zan_ast_node_t *member,
                                               local_scope_t *locals) {
    LLVMValueRef value = emit_expr(g, member->member.object, locals);
    if (member && !member->member.null_cond)
        emit_weak_read_guard(g, member->member.object, value,
                             member->member.object->loc, locals);
    if (member && value &&
        LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMPointerTypeKind) {
        zan_type_t *rt = infer_expr_type(g, member->member.object, locals);
        bool reflike = rt && (rt->kind == TYPE_OBJECT || rt->kind == TYPE_CLASS ||
                              rt->kind == TYPE_INTERFACE ||
                              rt->kind == TYPE_TYPE_PARAM ||
                              rt->kind == TYPE_ERROR);
        if (reflike && !member->member.null_cond) {
            LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, value,
                                           LLVMConstNull(LLVMTypeOf(value)),
                                           "recv.null");
            value = emit_soft_base_select(g, value, isnull,
                                          member->member.object->loc);
        }
    }
    return value;
}

/* 内部辅助逻辑 */
static bool class_implements_iface_depth(zan_symbol_t *cls, zan_symbol_t *iface,
                                          int depth) {
    if (!cls || !cls->type || !iface || depth > 512) return false;
    for (int i = 0; i < cls->type->interface_count; i++) {
        zan_type_t *it = cls->type->interfaces[i];
        if (!it || !it->sym) continue;
        if (it->sym == iface) return true;
        if (it->sym != cls &&
            class_implements_iface_depth(it->sym, iface, depth + 1))
            return true;
    }
    if (cls->type->base_type && cls->type->base_type->sym &&
        cls->type->base_type->sym != cls)
        return class_implements_iface_depth(cls->type->base_type->sym,
                                            iface, depth + 1);
    return false;
}

static bool class_implements_iface(zan_symbol_t *cls, zan_symbol_t *iface) {
    return class_implements_iface_depth(cls, iface, 0);
}

/* 内部辅助实现 */
static zan_symbol_t *get_method_sym(zan_symbol_t *cls, zan_istr_t name);

static bool zan_type_defines(zan_irgen_t *g, const char *type_name,
                             const char *method) {
    if (!g || !g->binder) return false;
    zan_istr_t tn = { (char *)type_name, (uint32_t)strlen(type_name) };
    zan_symbol_t *sym = zan_binder_lookup(g->binder, tn);
    if (!sym || !sym->type) return false;
    if (sym->type->kind != TYPE_CLASS && sym->type->kind != TYPE_STRUCT)
        return false;
    zan_istr_t mn = { (char *)method, (uint32_t)strlen(method) };
    return get_method_sym(sym, mn) != NULL;
}

/* 内部辅助实现 */

static void emit_runtime_check(zan_irgen_t *g, LLVMValueRef is_error,
                               zan_loc_t loc, const char *msg);

/* 模块核心语义抽象与接口调用契约 */
static void zan_arr_hdr_store(zan_irgen_t *g, LLVMValueRef raw, int off,
                              LLVMValueRef val, const char *name) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMValueRef o = LLVMConstInt(i64, (unsigned long long)off, 0);
    LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, raw, &o, 1, name);
    LLVMBuildStore(g->builder, val,
        LLVMBuildBitCast(g->builder, p, LLVMPointerType(i64, 0), name));
}

static LLVMValueRef zan_array_alloc_impl(zan_irgen_t *g, LLVMValueRef total,
                                         LLVMValueRef count, zan_type_t *elem_type) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMValueRef max_i64 = LLVMConstInt(i64, UINT64_MAX, 0);
    LLVMValueRef header = LLVMConstInt(i64, ZAN_ARR_HDR_SIZE, 0);
    LLVMValueRef max_total = LLVMBuildSub(g->builder, max_i64, header, "arr.max");
    LLVMValueRef overflow = zan_icmp(g->builder, LLVMIntUGT, total,
        max_total, "arr.overflow");
    emit_runtime_check(g, overflow, (zan_loc_t){0}, "array allocation overflow");
    /* 内部辅助实现 */
    total = LLVMBuildSelect(g->builder, overflow,
        LLVMConstInt(i64, 0, 0), total, "arr.total.safe");
    count = LLVMBuildSelect(g->builder, overflow,
        LLVMConstInt(i64, 0, 0), count, "arr.count.safe");
    LLVMValueRef bytes = LLVMBuildAdd(g->builder, total, header, "arr.bytes");
    LLVMValueRef raw = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
        get_calloc_fn(g), (LLVMValueRef[]){ bytes, LLVMConstInt(i64, 1, 0) }, 2, "arr.raw");
    LLVMValueRef null_raw = LLVMBuildIsNull(g->builder, raw, "arr.null");
    emit_runtime_check(g, null_raw, (zan_loc_t){0}, "array allocation failed");
    /* 内部辅助逻辑 */
    zan_arr_hdr_store(g, raw, ZAN_ARR_HDR_SIZE + ZAN_ARR_RC_OFF,
                      LLVMConstInt(i64, 1, 0), "arr.rc");
    zan_arr_hdr_store(g, raw, ZAN_ARR_HDR_SIZE + ZAN_ARR_RC_MAGIC_OFF,
                      LLVMConstInt(i64, ZAN_ARRAY_RC_MAGIC, 0), "arr.rcmagic");
    zan_arr_hdr_store(g, raw, ZAN_ARR_HDR_SIZE + ZAN_OBJ_RC_OFF, count, "arr.count");
    if (elem_type && is_rc_managed_type(elem_type)) {
        LLVMValueRef desc = get_array_desc(g, elem_type);
        LLVMValueRef desc_i64 = LLVMBuildPtrToInt(g->builder, desc, i64, "arr.desci");
        zan_arr_hdr_store(g, raw, ZAN_ARR_HDR_SIZE + ZAN_OBJ_SITE_OFF,
                          desc_i64, "arr.desc");
    } else {
        zan_arr_hdr_store(g, raw, ZAN_ARR_HDR_SIZE + ZAN_OBJ_SITE_OFF,
                          LLVMConstInt(i64, ZAN_ARRAY_MAGIC, 0), "arr.magic");
    }
    LLVMValueRef off = LLVMConstInt(i64, ZAN_ARR_HDR_SIZE, 0);
    return LLVMBuildGEP2(g->builder, i8, raw, &off, 1, "arr");
}

static LLVMValueRef zan_array_alloc_typed(zan_irgen_t *g, LLVMValueRef total,
                                          LLVMValueRef count, zan_type_t *elem_type) {
    return zan_array_alloc_impl(g, total, count, elem_type);
}

static LLVMValueRef zan_array_alloc(zan_irgen_t *g, LLVMValueRef total,
                                    LLVMValueRef count) {
    return zan_array_alloc_impl(g, total, count, NULL);
}

static LLVMValueRef zan_array_len(zan_irgen_t *g, LLVMValueRef arr) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMValueRef off = LLVMConstInt(i64, (unsigned long long)ZAN_OBJ_RC_OFF, 1);
    LLVMValueRef hdr = LLVMBuildGEP2(g->builder, i8, arr, &off, 1, "arr.hdrp");
    return LLVMBuildLoad2(g->builder, i64,
        LLVMBuildBitCast(g->builder, hdr, LLVMPointerType(i64, 0), "arr.hdrc"),
        "arr.len");
}

/* 内部辅助实现 */
static LLVMValueRef zan_mdarray_alloc(zan_irgen_t *g, LLVMValueRef *dims,
                                      int rank, LLVMTypeRef elem_llvm) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMValueRef total = LLVMConstInt(i64, 1, 0);
    for (int d = 0; d < rank; d++)
        total = zan_mul(g->builder, total, dims[d], "md.total");
    /* 内部辅助逻辑 */
    LLVMValueRef neg = zan_icmp(g->builder, LLVMIntSLT, total,
        LLVMConstInt(i64, 0, 0), "md.neg");
    emit_runtime_check(g, neg, (zan_loc_t){0}, "array allocation overflow");
    /* 内部辅助实现 */
    total = LLVMBuildSelect(g->builder, neg,
        LLVMConstInt(i64, 0, 0), total, "md.total.safe");
    LLVMValueRef hdr = LLVMConstInt(i64,
        (unsigned long long)(ZAN_ARR_HDR_SIZE + 8 * rank), 0);
    LLVMValueRef bytes = zan_add(g->builder, hdr,
        zan_mul(g->builder, total, LLVMSizeOf(elem_llvm), "md.bytes"), "md.totalb");
    LLVMValueRef nbytes = zan_icmp(g->builder, LLVMIntSLT, bytes,
        LLVMConstInt(i64, 0, 0), "md.nb");
    emit_runtime_check(g, nbytes, (zan_loc_t){0}, "array allocation overflow");
    bytes = LLVMBuildSelect(g->builder, nbytes,
        LLVMConstInt(i64, ZAN_ARR_HDR_SIZE + 8 * rank, 0), bytes, "md.bytes.safe");
    LLVMValueRef raw = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
        get_calloc_fn(g), (LLVMValueRef[]){ bytes, LLVMConstInt(i64, 1, 0) }, 2, "md.raw");
    LLVMValueRef null_raw = LLVMBuildIsNull(g->builder, raw, "md.null");
    emit_runtime_check(g, null_raw, (zan_loc_t){0}, "array allocation failed");
    LLVMValueRef hdri64 = LLVMBuildBitCast(g->builder, raw,
        LLVMPointerType(i64, 0), "md.hdr");
    zan_arr_hdr_store(g, raw, ZAN_ARR_HDR_SIZE + ZAN_ARR_RC_OFF,
                      LLVMConstInt(i64, 1, 0), "md.rc");
    zan_arr_hdr_store(g, raw, ZAN_ARR_HDR_SIZE + ZAN_ARR_RC_MAGIC_OFF,
                      LLVMConstInt(i64, ZAN_ARRAY_RC_MAGIC, 0), "md.rcmagic");
    zan_arr_hdr_store(g, raw, ZAN_ARR_HDR_SIZE + ZAN_OBJ_RC_OFF, total, "md.count");
    zan_arr_hdr_store(g, raw, ZAN_ARR_HDR_SIZE + ZAN_OBJ_SITE_OFF,
                      LLVMConstInt(i64, rank, 0), "md.rank");
    for (int d = 0; d < rank; d++) {
        LLVMValueRef off = LLVMConstInt(i64,
            ZAN_ARR_HDR_SIZE / 8 + (unsigned long long)d, 0);
        LLVMBuildStore(g->builder, dims[d],
            LLVMBuildGEP2(g->builder, i64, hdri64, &off, 1, "md.dim"));
    }
    LLVMValueRef hdr_off = LLVMConstInt(i64, ZAN_ARR_HDR_SIZE, 0);
    return LLVMBuildGEP2(g->builder, i8, raw, &hdr_off, 1, "md.arr");
}

/* 内部辅助实现 */
static LLVMValueRef zan_itoa_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_itoa");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i64t,
        (LLVMTypeRef[]){ i8ptr, i64t, i32t }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_itoa", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "loop");
    LLVMBasicBlockRef sign = LLVMAppendBasicBlockInContext(g->ctx, fn, "sign");
    LLVMBasicBlockRef neg_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "neg");
    LLVMBasicBlockRef copy = LLVMAppendBasicBlockInContext(g->ctx, fn, "copy");

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef buf = LLVMGetParam(fn, 0);
    LLVMValueRef val = LLVMGetParam(fn, 1);
    LLVMValueRef uns = LLVMGetParam(fn, 2);
    /* 模块核心语义抽象与接口调用契约 */
    LLVMTypeRef tmp_ty = LLVMArrayType(i8, 24);
    LLVMValueRef tmp = LLVMBuildAlloca(g->builder, tmp_ty, "itoa.tmp");
    LLVMValueRef zero64 = LLVMConstInt(i64t, 0, 0);
    LLVMValueRef ten = LLVMConstInt(i64t, 10, 0);
    /* 内部辅助逻辑 */
    LLVMValueRef is_signed = zan_icmp(g->builder, LLVMIntEQ, uns,
        LLVMConstInt(i32t, 0, 0), "itoa.signed");
    LLVMValueRef is_lt0 = zan_icmp(g->builder, LLVMIntSLT, val, zero64, "itoa.lt0");
    LLVMValueRef is_neg = LLVMBuildAnd(g->builder, is_signed, is_lt0, "itoa.neg");
    LLVMValueRef mag = LLVMBuildSelect(g->builder, is_neg,
        LLVMBuildNeg(g->builder, val, "itoa.abs"), val, "itoa.mag");
    LLVMBuildBr(g->builder, loop);

    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef v = LLVMBuildPhi(g->builder, i64t, "itoa.v");
    LLVMValueRef pos = LLVMBuildPhi(g->builder, i64t, "itoa.pos");
    LLVMValueRef q = LLVMBuildUDiv(g->builder, v, ten, "itoa.q");
    LLVMValueRef r = LLVMBuildURem(g->builder, v, ten, "itoa.r");
    LLVMValueRef next_pos = LLVMBuildSub(g->builder, pos,
        LLVMConstInt(i64t, 1, 0), "itoa.pos1");
    LLVMValueRef digit = LLVMBuildTrunc(g->builder,
        LLVMBuildAdd(g->builder, r, LLVMConstInt(i64t, '0', 0), "itoa.ch"),
        i8, "itoa.ch8");
    LLVMValueRef gep_idx[] = { zero64, next_pos };
    LLVMBuildStore(g->builder, digit,
        LLVMBuildGEP2(g->builder, tmp_ty, tmp, gep_idx, 2, "itoa.dp"));
    LLVMValueRef more = zan_icmp(g->builder, LLVMIntNE, q, zero64, "itoa.more");
    LLVMBuildCondBr(g->builder, more, loop, sign);
    LLVMAddIncoming(v, (LLVMValueRef[]){ mag, q },
        (LLVMBasicBlockRef[]){ entry, loop }, 2);
    LLVMAddIncoming(pos, (LLVMValueRef[]){ LLVMConstInt(i64t, 24, 0), next_pos },
        (LLVMBasicBlockRef[]){ entry, loop }, 2);

    LLVMPositionBuilderAtEnd(g->builder, sign);
    LLVMBuildCondBr(g->builder, is_neg, neg_bb, copy);

    LLVMPositionBuilderAtEnd(g->builder, neg_bb);
    LLVMValueRef minus_pos = LLVMBuildSub(g->builder, next_pos,
        LLVMConstInt(i64t, 1, 0), "itoa.mpos");
    LLVMValueRef minus_idx[] = { zero64, minus_pos };
    LLVMBuildStore(g->builder, LLVMConstInt(i8, '-', 0),
        LLVMBuildGEP2(g->builder, tmp_ty, tmp, minus_idx, 2, "itoa.mp"));
    LLVMBuildBr(g->builder, copy);

    LLVMPositionBuilderAtEnd(g->builder, copy);
    LLVMValueRef start = LLVMBuildPhi(g->builder, i64t, "itoa.start");
    LLVMAddIncoming(start, (LLVMValueRef[]){ next_pos, minus_pos },
        (LLVMBasicBlockRef[]){ sign, neg_bb }, 2);
    LLVMValueRef len = LLVMBuildSub(g->builder, LLVMConstInt(i64t, 24, 0),
        start, "itoa.len");
    LLVMValueRef src_idx[] = { zero64, start };
    LLVMValueRef src = LLVMBuildGEP2(g->builder, tmp_ty, tmp, src_idx, 2, "itoa.src");
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    zan_call2(g->builder, memcpy_ty, get_libc_fn(g, "memcpy", memcpy_ty),
        (LLVMValueRef[]){ buf, src, len }, 3, "");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0),
        LLVMBuildGEP2(g->builder, i8, buf, &len, 1, "itoa.end"));
    LLVMBuildRet(g->builder, len);

    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* 发射`__zan_itoa(buf, val, is_unsigned)`; `val` must already be i64 */
static LLVMValueRef zan_emit_itoa(zan_irgen_t *g, LLVMValueRef buf,
                                 LLVMValueRef val, bool is_unsigned) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i64t,
        (LLVMTypeRef[]){ i8ptr, i64t, i32t }, 3, 0);
    return zan_call2(g->builder, fnty, zan_itoa_fn(g),
        (LLVMValueRef[]){ buf, val, LLVMConstInt(i32t, is_unsigned ? 1 : 0, 0) },
        3, "itoa.n");
}

static bool is_call_to(zan_ast_node_t *expr, const char *obj, const char *method) {
    if (expr->kind != AST_CALL) return false;
    zan_ast_node_t *callee = expr->call.callee;
    if (callee->kind != AST_MEMBER_ACCESS) return false;
    if (callee->member.object->kind != AST_IDENTIFIER) return false;

    zan_istr_t obj_name = callee->member.object->ident.name;
    zan_istr_t method_name = callee->member.name;

    return ((int)obj_name.len == (int)strlen(obj) &&
            memcmp(obj_name.str, obj, (size_t)obj_name.len) == 0 &&
            (int)method_name.len == (int)strlen(method) &&
            memcmp(method_name.str, method, (size_t)method_name.len) == 0);
}
