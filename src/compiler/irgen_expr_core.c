/* irgen_expr_core */

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
static LLVMValueRef emit_expr_identifier(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static LLVMValueRef emit_expr_unary(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static LLVMValueRef emit_expr_binary(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static LLVMValueRef emit_expr_assignment(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static LLVMValueRef emit_incdec_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals, int is_prefix);
static LLVMValueRef emit_expr_string_interp(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static LLVMValueRef emit_expr_member_access(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static LLVMValueRef emit_expr_index(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static LLVMValueRef emit_expr_query_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static LLVMValueRef emit_expr_tuple(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static LLVMValueRef emit_expr_new_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static LLVMValueRef emit_expr_conditional(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static LLVMValueRef emit_expr_switch_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static LLVMValueRef emit_expr_with_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static LLVMValueRef emit_expr_await_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static LLVMValueRef emit_expr_cast_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static LLVMValueRef emit_ref_arg(zan_irgen_t *g, zan_ast_node_t *arg, local_scope_t *locals);
static LLVMValueRef emit_expr_call(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);

/* 判断目标平台是否为 WebAssembly */
static inline bool target_is_wasm32(zan_irgen_t *g) {
    return g->target_triple[0] &&
           strncmp(g->target_triple, "wasm32", 6) == 0;
}
static void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals);
static void emit_runtime_check(zan_irgen_t *g, LLVMValueRef is_error,
                               zan_loc_t loc, const char *msg);
static LLVMValueRef emit_soft_base_select(zan_irgen_t *g, LLVMValueRef base,
                                          LLVMValueRef isnull, zan_loc_t loc);
/* 发射 Lambda 表达式并推断参数与返回值类型 */
static LLVMValueRef emit_lambda_typed(zan_irgen_t *g, zan_ast_node_t *expr,
                                      zan_type_t *expected, local_scope_t *locals);
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

enum {
    ASYNC_FRAME_SCHED = 0,
    ASYNC_FRAME_SCHED_STEP = 1,
    ASYNC_FRAME_STATE = 2,        /* i32: 0=start, k=resume-after-await-k, -1=done */
    ASYNC_FRAME_DONE = 3,
    ASYNC_FRAME_AWAITER = 4,
    ASYNC_FRAME_AWAITER_STEP = 5, /* void(i8*)*: awaiter's resume fn (or null) */
    ASYNC_FRAME_RESULT = 6,
    ASYNC_FRAME_CLEANUP = 7,
    ASYNC_FRAME_HCOUNT = 8,
    ASYNC_FRAME_SELF_STEP = 9,
    ASYNC_FRAME_EXC = 10,
    ASYNC_FRAME_EXC_TID = 11,
    ASYNC_FRAME_EXC_OWNED = 12,
    ASYNC_FRAME_CANCEL = 13,
    ASYNC_FRAME_CHILD = 14,
    ASYNC_FRAME_LNEXT = 15,
    ASYNC_FRAME_PENDING_COUNT = 16,
    ASYNC_FRAME_HSTACK = 17,
    ASYNC_FRAME_CEXC = 18,
    ASYNC_FRAME_CEXC_OWNED = 19,
    ASYNC_FRAME_CEXC_TID = 20,
    ASYNC_FRAME_PENDING = 21,
    ASYNC_FRAME_HPENDING = 22,
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
        LLVMValueRef reap = get_co_reap_fn(g);
        zan_call2(g->builder, LLVMGlobalGetValueType(reap), reap, &hp, 1, "");
    }
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

static bool is_name_path(zan_ast_node_t *node) {
    if (!node) return false;
    if (node->kind == AST_IDENTIFIER) return true;
    if (node->kind == AST_MEMBER_ACCESS) return is_name_path(node->member.object);
    return false;
}

static zan_ast_node_t *name_path_head(zan_ast_node_t *node) {
    while (node && node->kind == AST_MEMBER_ACCESS) node = node->member.object;
    return (node && node->kind == AST_IDENTIFIER) ? node : NULL;
}

/* 把成员访问链摊成点号路径："System.Text.Csv"。 */
static bool qpath_flatten(zan_ast_node_t *n, char *buf, int cap) {
    if (!n || cap <= 1) return false;
    if (n->kind == AST_IDENTIFIER) {
        int len = (int)n->ident.name.len;
        if (len + 1 > cap) return false;
        memcpy(buf, n->ident.name.str, (size_t)len);
        buf[len] = '\0';
        return true;
    }
    if (n->kind == AST_MEMBER_ACCESS) {
        if (!qpath_flatten(n->member.object, buf, cap)) return false;
        int len = (int)strlen(buf);
        int mlen = (int)n->member.name.len;
        if (len + 1 + mlen + 1 > cap) return false;
        buf[len] = '.';
        memcpy(buf + len + 1, n->member.name.str, (size_t)mlen);
        buf[len + 1 + mlen] = '\0';
        return true;
    }
    return false;
}

/* 命名空间段边界上的祖先判断：a 是 b 的祖先（"Sys" 是 "System.Text" 的祖先）。 */
static bool ns_is_ancestor(const char *a, size_t alen, const char *b, size_t blen) {
    if (alen == 0 || alen > blen) return false;
    if (memcmp(a, b, alen) != 0) return false;
    if (alen == blen) return true;
    return b[alen] == '.';
}

/* ns 以 suf 结尾且在段边界上（"Gui.Widget" 以 "Widget" 结尾）。 */
static bool ns_ends_with_seg(const char *ns, size_t nslen, const char *suf, size_t suflen) {
    if (suflen == 0 || nslen < suflen + 1) return false;
    if (memcmp(ns + nslen - suflen, suf, suflen) != 0) return false;
    return ns[nslen - suflen - 1] == '.';
}

/* 限定名路径必须和候选类型的命名空间对得上。
 *
 * 踩过的坑：`System.Text.Csv.Parse(...)`（Csv 其实住在 System.Csv 命名空间）
 * 以前会**按最后一段**绑到 using 引进的 Csv 类上——不报"未解析"，返回类型
 * 也不匹配（CsvTable 当成 List<List<string>> 用），运行期直接踩内存
 * (0xC0000005)。限定名写错就该报错，不能猜。
 *
 * 判据分两种：
 *   - 路径**首段是类**（Widget.Tooltip.Flush）：后面是嵌套成员，按最后一段
 *     找是合法的，放行；
 *   - 路径首段是**命名空间**（System.Text.Csv）：路径前缀（去掉最后一段）
 *     与类型所在命名空间必须互为祖先——
 *       Csv 在 System.Csv，路径 "System.Csv"          → 前缀是 ns 的祖先 ✔
 *       Json 在 System.Text.Json，路径 "System.Text.Json" → 前缀是 ns 的祖先 ✔
 *       Csv 在 System.Csv，路径 "System.Text.Csv"      → 互不为祖先 ✘（拒绝）
 */
static bool qpath_ns_ok(zan_irgen_t *g, zan_ast_node_t *path, zan_symbol_t *sym) {
    if (!sym) return true;
    if (!sym->decl) return true;  /* 查不到声明的符号（内置/外部）不拦 */
    zan_ast_node_t *head = name_path_head(path);
    if (!head) return true;
    zan_symbol_t *hs = zan_binder_lookup(g->binder, head->ident.name);
    if (hs && (hs->kind == SYM_CLASS || hs->kind == SYM_STRUCT))
        return true;              /* 类开头的路径：嵌套成员，放行 */
    char buf[512];
    if (!qpath_flatten(path, buf, (int)sizeof(buf))) return true;
    char *last = strrchr(buf, '.');
    if (!last) return true;       /* 单段名：using 引进的类，合法 */
    size_t plen = (size_t)(last - buf);
    zan_istr_t ns = zan_ast_ns_name(sym->decl);
    if (ns.len == 0) return false; /* 全局命名空间的类型不能带命名空间前缀 */
    if (ns.len == plen && memcmp(ns.str, buf, plen) == 0) return true;   /* 完全一致 */
    if (ns_is_ancestor(buf, plen, ns.str, ns.len)) return true;            /* 前缀是 ns 的祖先 */
    if (ns_is_ancestor(ns.str, ns.len, buf, plen)) return true;            /* ns 是前缀的祖先（嵌套类） */
    if (ns_ends_with_seg(ns.str, ns.len, buf, plen)) return true;          /* 相对外层命名空间写（Gui.Widget ← Widget） */
    return false;
}

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
            if (!r || (l == INT64_MIN && r == -1)) return false;
            *out = l / r; return true;
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

    bool native_handle_loss =
        src->kind == TYPE_NINT && dst->kind != TYPE_NINT && rd < 8;
    bool float_to_int_loss = src_float && rd != 0;
    bool loses = src_float || rs > rd || native_handle_loss;
    if (!loses) return;
    if (!float_to_int_loss && !native_handle_loss && !narrow_warn_enabled())
        return;

    zan_diag_emit_code(g->diag,
                  (float_to_int_loss || native_handle_loss) ? DIAG_ERROR
                                                            : DIAG_WARNING,
                  at->loc, 2022,
                  "narrowing conversion from '%s' to '%s' in %s needs an "
                  "explicit cast (C# rules)",
                  src->name.str ? src->name.str : "?",
                  dst->name.str ? dst->name.str : "?", what);
}

static zan_symbol_t *find_user_conversion(zan_irgen_t *g, zan_type_t *from_type,
                                          zan_type_t *to_type, const char *op_name);

static void check_value_type_mismatch(zan_irgen_t *g, zan_type_t *dst, zan_type_t *src,
                                      zan_ast_node_t *at, const char *what) {
    if (!g || !g->diag || !at || !dst || !src) return;
    if (dst->kind == src->kind) return;
    bool dst_struct = dst->kind == TYPE_STRUCT, src_struct = src->kind == TYPE_STRUCT;
    if (dst_struct == src_struct) return;
    if (find_user_conversion(g, src, dst, "op_implicit")) return;
    bool other_prim = (dst_struct ? conv_rank(src) : conv_rank(dst)) != 0 ||
                      (dst_struct ? src->kind : dst->kind) == TYPE_FLOAT ||
                      (dst_struct ? src->kind : dst->kind) == TYPE_DOUBLE ||
                      (dst_struct ? src->kind : dst->kind) == TYPE_BOOL;
    if (!other_prim) return;
    zan_diag_emit_code(g->diag, DIAG_ERROR, at->loc, 2105,
                  "cannot convert '%s' to '%s' in %s: a value struct and a "
                  "primitive are unrelated types",
                  src->name.str ? src->name.str : "?",
                  dst->name.str ? dst->name.str : "?", what);
}

/* 值类型 ↔ 引用类型：Zan 没有装箱设施，两侧不能隐式互转。
 *
 * 踩过的坑（A449）：`FuncT<Order, object> f = (Order o) => o.id;` 编译期一声
 * 不吭，运行期把 int 值当对象指针（IntToPtr）返回，调用方拿到非 null 的野
 * 指针，进程退出时 ARC 去回收这个假对象再崩一次（0xC0000005）。
 * 委托返回类型与 lambda 体类型对不上时，必须在编译期拦下来。 */
static bool irgen_type_is_value_kind(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BOOL: case TYPE_BYTE: case TYPE_SBYTE:
    case TYPE_SHORT: case TYPE_USHORT: case TYPE_INT: case TYPE_UINT:
    case TYPE_LONG: case TYPE_ULONG: case TYPE_NINT:
    case TYPE_FLOAT: case TYPE_DOUBLE: case TYPE_DECIMAL:
    case TYPE_CHAR: case TYPE_ENUM: case TYPE_STRUCT:
        return true;
    case TYPE_NULLABLE:
        return irgen_type_is_value_kind(t->element_type);
    default:
        return false;
    }
}

static bool irgen_type_is_ref_kind(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_OBJECT: case TYPE_INTERFACE: case TYPE_STRING:
    case TYPE_CLASS: case TYPE_ARRAY: case TYPE_DELEGATE: case TYPE_TASK:
        return true;
    case TYPE_NULLABLE:
        return irgen_type_is_ref_kind(t->element_type);
    default:
        return false;
    }
}

/* 目标类型是引用类型、来源是值类型（或反过来）时报错：没有装箱/拆箱路径，
 * 硬转只会造出野指针。类型参数（泛型未实例化）一律放过。 */
static void check_no_boxing_conversion(zan_irgen_t *g, zan_type_t *dst,
                                       zan_type_t *src, zan_ast_node_t *at,
                                       const char *what) {
    if (!g || !g->diag || !at || !dst || !src) return;
    if (dst->kind == src->kind) return;
    if (dst->kind == TYPE_TYPE_PARAM || src->kind == TYPE_TYPE_PARAM) return;
    if (dst->kind == TYPE_ERROR || src->kind == TYPE_ERROR) return;
    if (dst->kind == TYPE_VOID || src->kind == TYPE_VOID) return;
    if (find_user_conversion(g, src, dst, "op_implicit")) return;
    bool dst_ref = irgen_type_is_ref_kind(dst);
    bool src_ref = irgen_type_is_ref_kind(src);
    if (dst_ref == src_ref) return;
    /* 类型名按形状渲染（`T?` / `T[]` / `List<T>`）——与 checker 共用
     * binder 的 zan_type_display，两边不各说各话（A463）。 */
    char sbuf[128], dbuf[128];
    zan_type_display(src, sbuf, sizeof sbuf);
    zan_type_display(dst, dbuf, sizeof dbuf);
    const char *sname = sbuf[0] ? sbuf : "?";
    const char *dname = dbuf[0] ? dbuf : "?";
    if (dst_ref) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, at->loc, 2106,
                      "cannot convert '%s' to '%s' in %s: value types cannot be "
                      "boxed into a reference type",
                      sname, dname, what);
    } else {
        zan_diag_emit_code(g->diag, DIAG_ERROR, at->loc, 2107,
                      "cannot convert '%s' to '%s' in %s: a reference type cannot "
                      "be converted to a value type",
                      sname, dname, what);
    }
}

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

static bool type_has_unresolved(zan_type_t *t) {
    if (!t) return true;
    if (t->kind == TYPE_ERROR || t->kind == TYPE_TYPE_PARAM) return true;
    for (int i = 0; i < t->type_arg_count; i++)
        if (type_has_unresolved(t->type_args[i])) return true;
    if (t->element_type && type_has_unresolved(t->element_type)) return true;
    return false;
}

static void check_generic_invariance(zan_irgen_t *g, zan_type_t *dst, zan_type_t *src,
                                     zan_ast_node_t *at, const char *what) {
    if (!g || !g->diag || !at || !dst || !src) return;
    if (dst->kind != TYPE_CLASS && dst->kind != TYPE_STRUCT) return;
    if (src->sym != dst->sym) return;
    if (dst->type_arg_count == 0) return;
    if (type_has_unresolved(dst) || type_has_unresolved(src)) return;
    for (int i = 0; i < dst->type_arg_count; i++) {
        zan_type_t *a = dst->type_args[i];
        zan_type_t *b = src->type_arg_count > i ? src->type_args[i] : NULL;
        if (!a || !b) return;
        if (!type_full_equal(a, b)) {
            /* 与 checker 共用 zan_type_display：泛型实参的可空/数组形状
             * 也要说出来（`List<int?>`），不能只报个裸名字（A463）。 */
            char dbuf[128], sbuf[128];
            zan_type_display(dst, dbuf, sizeof(dbuf));
            zan_type_display(src, sbuf, sizeof(sbuf));
            zan_diag_emit_code(g->diag, DIAG_ERROR, at->loc, 2108,
                          "cannot convert '%s' to '%s' in %s: generic type "
                          "arguments are invariant and must match exactly",
                          sbuf[0] ? sbuf : "?",
                          dbuf[0] ? dbuf : "?", what);
            return;
        }
    }
}

static zan_type_t *infer_expr_type(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);

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
        /* 泛型类的 `this.field` 必须按当前实例替换类型实参：字段声明
         * 里的 List<T> 在 Box<Pos> 里是 List<Pos>。不替换的话元素类型
         * 停在类型参数上——按值语义退化成"存指针"，结构体元素写进去
         * 的是临时对象的地址，读回来就是野指针（实测 0xC0000005）。 */
        if (fsym) {
            zan_type_t *ft = fsym->type;
            if (ft && g->cur_inst) ft = subst_type_param_deep(g, ft, g->cur_inst);
            if (ft && ft->kind == TYPE_TYPE_PARAM) ft = concretize(g, ft);
            return ft;
        }
    }
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
        zan_type_t *ot = infer_expr_type(g, e->index.object, locals);
        zan_type_t *et;
        if (ot && type_named(ot, "Dict", 4) &&
            ot->type_arg_count == 2)
            et = ot->type_args[1];
        else
            et = container_elem_type(ot);
        return et && et->kind == TYPE_STRING;
    }
    case AST_CONDITIONAL:
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
        {
            zan_type_t *rt = infer_expr_type(g, e, locals);
            return rt && rt->kind == TYPE_STRING;
        }
    }
    default:
        return false;
    }
}

static zan_type_t *container_elem_type(zan_type_t *t) {
    if (!t) return NULL;
    if (t->element_type) return t->element_type;
    if (t->type_args && t->type_arg_count > 0) return t->type_args[0];
    return NULL;
}

static int is_span_type(zan_type_t *t) {
    if (!t || t->kind != TYPE_STRUCT) return 0;
    if (t->name.len == 4 && memcmp(t->name.str, "Span", 4) == 0) return 1;
    if (t->name.len == 12 && memcmp(t->name.str, "ReadOnlySpan", 12) == 0) return 1;
    return 0;
}

static zan_type_t *dict_key_type(zan_irgen_t *g, zan_type_t *t) {
    if (!t || !t->type_args || t->type_arg_count < 1) return g ? g->binder->type_string : NULL;
    return t->type_args[0];
}

static zan_type_t *dict_value_type(zan_type_t *t) {
    if (!t || !t->type_args || t->type_arg_count < 2) return NULL;
    return t->type_args[1];
}

static bool type_mentions_tp(zan_type_t *t);
static zan_type_t *method_param_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                        int idx, zan_ast_node_t *call,
                                        zan_ast_node_t *recv_expr,
                                        local_scope_t *locals);

static bool types_concrete_equal(zan_type_t *a, zan_type_t *b) {
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    if ((a->kind == TYPE_CLASS || a->kind == TYPE_STRUCT ||
         a->kind == TYPE_INTERFACE || a->kind == TYPE_ENUM) &&
        a->sym != b->sym)
        return false;
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_NULLABLE) {
        if (a->kind == TYPE_ARRAY && a->array_rank != b->array_rank)
            return false;
        return types_concrete_equal(a->element_type, b->element_type);
    }
    if (a->type_arg_count != b->type_arg_count) return false;
    for (int i = 0; i < a->type_arg_count; i++)
        if (!types_concrete_equal(a->type_args[i], b->type_args[i]))
            return false;
    return true;
}

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
            return false;
        return types_match_modulo_tp(a->element_type, b->element_type);
    }
    if (a->type_arg_count != b->type_arg_count) return false;
    for (int i = 0; i < a->type_arg_count; i++)
        if (!types_match_modulo_tp(a->type_args[i], b->type_args[i]))
            return false;
    return true;
}

enum { FAM_UNKNOWN = 0, FAM_BOOL, FAM_INT, FAM_FLOAT, FAM_STRING, FAM_REF, FAM_DECIMAL };

static int type_family(zan_type_t *t) {
    if (!t) return FAM_UNKNOWN;
    switch (t->kind) {
    case TYPE_BOOL: return FAM_BOOL;
    case TYPE_BYTE: case TYPE_SHORT: case TYPE_INT: case TYPE_LONG:
    case TYPE_SBYTE: case TYPE_USHORT: case TYPE_UINT: case TYPE_ULONG:
    case TYPE_CHAR: case TYPE_ENUM:
        return FAM_INT;
    case TYPE_FLOAT: case TYPE_DOUBLE: return FAM_FLOAT;
    case TYPE_DECIMAL: return FAM_DECIMAL;
    case TYPE_STRING: return FAM_STRING;
    case TYPE_CLASS: case TYPE_INTERFACE: case TYPE_ARRAY: case TYPE_OBJECT: return FAM_REF;
    default: return FAM_UNKNOWN;
    }
}

static bool str_and_byte_buffer(zan_type_t *s, zan_type_t *b) {
    if (!s || !b || s->kind != TYPE_STRING || b->kind != TYPE_ARRAY) return false;
    zan_type_t *e = b->element_type;
    return e && (e->kind == TYPE_BYTE || e->kind == TYPE_SBYTE ||
                 e->kind == TYPE_CHAR);
}

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
        if (stmt->var_decl.initializer) {
            zan_type_t *t = infer_expr_type(g, stmt->var_decl.initializer, locals);
            if (t) local_add(locals, stmt->var_decl.name, NULL, t);
        }
        return;
    case AST_LAMBDA:
        return;
    case AST_WHILE_STMT: case AST_DO_WHILE_STMT: case AST_FOR_STMT:
    case AST_FOREACH_STMT: case AST_SWITCH_STMT: case AST_TRY_STMT:
        *mixed = 1;
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

static int method_group_score(zan_irgen_t *g, zan_symbol_t *mg, zan_type_t *pt) {
    if (!pt || pt->kind != TYPE_DELEGATE) return -1;
    if (mg->decl->method_decl.params.count != pt->delegate_param_count) return -1;
    int score = 2;
    zan_type_t *rt = mg->decl->method_decl.return_type
        ? zan_binder_resolve_type(g->binder, mg->decl->method_decl.return_type)
        : NULL;
    zan_type_t *dr = pt->delegate_ret_type;
    if (rt && dr && !type_mentions_tp(rt) && !type_mentions_tp(dr)) {
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
                if (pt && args->items[j] && args->items[j]->kind == AST_LAMBDA) {
                    zan_ast_node_t *lam = args->items[j];
                    if (pt->kind != TYPE_DELEGATE ||
                        pt->delegate_param_count != lam->lambda.params.count) {
                        compatible = false;
                        break;
                    }
                    score += 2;
                    zan_type_t *bt = lambda_body_type(g, lam, pt, locals);
                    if (bt && pt->delegate_ret_type &&
                        types_concrete_equal(bt, pt->delegate_ret_type))
                        score += 2;
                    continue;
                }
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
    return best;
}

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

static int expr_family(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    if (!e) return FAM_UNKNOWN;
    switch (e->kind) {
    case AST_INT_LITERAL: case AST_CHAR_LITERAL: return FAM_INT;
    case AST_FLOAT_LITERAL: return FAM_FLOAT;
    case AST_DECIMAL_LITERAL: return FAM_DECIMAL;
    case AST_STRING_LITERAL: case AST_STRING_INTERP: return FAM_STRING;
    case AST_BOOL_LITERAL: return FAM_BOOL;
    case AST_UNARY:
        if (e->unary.op == TK_BANG) return FAM_BOOL;
        return expr_family(g, e->unary.operand, locals);
    case AST_POSTFIX_UNARY:
        return expr_family(g, e->unary.operand, locals);
    case AST_CONDITIONAL: {
        /* 两支家族合并：一边未知取另一边；数值家族按提升阶梯统一
         * （float/double、decimal 赢整数）——与 checker 的
         * merge_conditional_types 同口径。曾按 then 臂定型：
         * (t?1:2.0)/2 因此做整数除法得 0，C# 是 0.5（TASKS.md A514）。 */
        int tf = expr_family(g, e->conditional.then_expr, locals);
        int ef = expr_family(g, e->conditional.else_expr, locals);
        if (tf == FAM_UNKNOWN) return ef;
        if (ef == FAM_UNKNOWN) return tf;
        if (tf == ef) return tf;
        if ((tf == FAM_INT && ef == FAM_FLOAT) ||
            (tf == FAM_FLOAT && ef == FAM_INT))
            return FAM_FLOAT;
        if ((tf == FAM_INT && ef == FAM_DECIMAL) ||
            (tf == FAM_DECIMAL && ef == FAM_INT) ||
            (tf == FAM_FLOAT && ef == FAM_DECIMAL) ||
            (tf == FAM_DECIMAL && ef == FAM_FLOAT))
            return FAM_DECIMAL;
        /* 其余混合（STRING/REF/BOOL 之间）维持 then 臂——与旧行为一致 */
        return tf;
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
            if (lf == FAM_DECIMAL || rf == FAM_DECIMAL) return FAM_DECIMAL;
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

static int concrete_arg_score(zan_irgen_t *g, zan_type_t *pt,
                              zan_ast_node_t *a, local_scope_t *locals) {
    /* null 字面量实参：推断不出静态类型，只能按"接不接得住 null"判——
     * 与 checker 共用同一条规则（zan_null_arg_score）。
     *
     * 踩过的坑：`Arg.Text(null)` 以前走到 `Text(int)`，null 被当成 0 传
     * 下去，调用点毫无异常，断言却永远对不上——因为这里对 null 一律给
     * 0 分，等于所有重载同分，第一个（值类型）就赢了。 */
    if (a && a->kind == AST_NULL_LITERAL) {
        int ns = zan_null_arg_score(pt);
        return ns < 0 ? -1 : ns;
    }
    zan_type_t *at = infer_expr_type(g, a, locals);
    if (at && !type_mentions_tp(at)) {
        if (types_concrete_equal(pt, at)) return 4;
        int pf = type_family(pt), af = type_family(at);
        if (pt->kind == TYPE_OBJECT && (af == FAM_REF || af == FAM_STRING)) return 1;
        if (pf == FAM_UNKNOWN || af == FAM_UNKNOWN) return 0;
        if (pf == af) return 2;
        if (implicit_ctor_for_arg(g, pt, at, a, locals)) return 1;
        if (pf == FAM_FLOAT && af == FAM_INT) return 0;
        if (pf == FAM_DECIMAL && af == FAM_INT) return 1;
        if (str_and_byte_buffer(pt, at) || str_and_byte_buffer(at, pt)) return 0;
        return -1;
    }
    int af = expr_family(g, a, locals);
    int pf = type_family(pt);
    if (af == FAM_UNKNOWN || pf == FAM_UNKNOWN) return 0;
    if (af == pf) return 2;
    if (pf == FAM_FLOAT && af == FAM_INT) return 0;
    if (pf == FAM_DECIMAL && af == FAM_INT) return 1;
    return -1;
}

static int method_args_score(zan_irgen_t *g, zan_symbol_t *m,
                             zan_ast_node_t *call, zan_ast_node_t *recv_expr,
                             local_scope_t *locals, int p0) {
    zan_ast_list_t *ps = &m->decl->method_decl.params;
    int score = 0;
    if (!call || call->kind != AST_CALL || !locals) return score;
    for (int j = p0; j < ps->count; j++) {
        int ai = j - p0;
        if (ai >= call->call.args.count) break;
        if (ps->items[j]->kind == AST_PARAM && ps->items[j]->param.is_params) {
            zan_type_t *bundle = method_param_type(g, m, j);
            if (!bundle || bundle->kind != TYPE_ARRAY ||
                !bundle->element_type || type_mentions_tp(bundle))
                break;
            for (; ai < call->call.args.count; ai++) {
                zan_ast_node_t *ta = call->call.args.items[ai];
                if (!ta || ta->kind == AST_NAMED_ARG || ta->kind == AST_LAMBDA)
                    continue;
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
            continue;
        }
        /* lambda 实参（也包括三元表达式里的一支是 lambda 的形状）：lambda
         * 没有自己的类型——类型由目标委托形参决定。所以打分只能按形参的
         * 委托类型来，不能按实参自身的类型（推不出来）。
         *
         * 形参不是委托 → 判死（-1）。不判死的话这个候选与正确候选同分，
         * 重载决议退化成「取成员表里第一个同名方法」：IDE 里
         * `LspSession.StartAsync(app != null ? (Action a) => ... : null)`
         * 就绑到了 App 那个重载上，无限自我递归，一开工程就 exit(1)。 */
        zan_ast_node_t *lam = zan_ast_lambda_source(a);
        if (lam) {
            zan_type_t *dp = method_param_type_at(g, m, j, call, recv_expr, locals);
            if (!dp) continue;                        /* 判不出来：宽容放过 */
            if (dp->kind == TYPE_TYPE_PARAM || type_mentions_tp(dp)) continue;
            if (dp->kind != TYPE_DELEGATE) return -1; /* lambda 只能转委托 */
            if (lam->lambda.params.count != dp->delegate_param_count) return -1;
            score += 2;
            /* lambda 显式声明的形参类型与委托形参类型一致 → 更贴 */
            for (int k = 0; k < lam->lambda.params.count; k++) {
                zan_ast_node_t *lp = lam->lambda.params.items[k];
                if (!lp || lp->kind != AST_PARAM || !lp->param.type) continue;
                zan_type_t *lpt = zan_binder_resolve_type(g->binder, lp->param.type);
                zan_type_t *dpt = dp->delegate_param_types[k];
                if (lpt && dpt && types_concrete_equal(lpt, dpt)) score += 2;
            }
            zan_ast_node_t *body = lam->lambda.body;
            if (!body) continue;
            int mark = locals->count;
            for (int k = 0; k < lam->lambda.params.count; k++) {
                zan_ast_node_t *lp = lam->lambda.params.items[k];
                zan_type_t *lpt = lp->param.type
                    ? zan_binder_resolve_type(g->binder, lp->param.type)
                    : dp->delegate_param_types[k];
                local_add(locals, lp->param.name, NULL, lpt);
            }
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
        zan_type_t *pt = method_param_type(g, m, j);
        if (!pt) continue;
        if (type_mentions_tp(pt)) {
            if (pt->kind != TYPE_TYPE_PARAM) {
                zan_type_t *gat = infer_expr_type(g, a, locals);
                if (gat && types_match_modulo_tp(pt, gat)) score += 3;
            }
            continue;
        }
        if (pt->kind == TYPE_INTERFACE) {
            /* 接口形参的匹配判定：
             *   - null 字面量：按"接不接得住 null"判（与 checker、concrete_arg_score
             *     同一条规则 zan_null_arg_score）。
             *   - 实参静态类型**也是接口** → 精确比对：同一个接口 = 精确匹配（+4）；
             *     实参接口派生自形参接口 = 可赋值（+1）；毫无关系 = 不加分。
             *   - 实参是**类** → 实现了该接口 = 可赋值（+1）；没实现就不加分，
             *     但不判死（实现关系可能经由泛型实例化等路径看不出来，误杀代价大）。
             *
             * 踩过的坑：这里原来一律 0 分（continue），理由是"打分看不出实现
             * 列表"。但"只靠接口类型区分"的一组重载于是**全部同分**，第一个永远
             * 赢：`TransactionScope.Create(fsql)`（fsql 是 IZanSql）被发到
             * `Create(IUnitOfWork)` 上——字段里装的是错类型，后面调用保存点方法
             * 时才炸，错误消息完全指不到病根。 */
            if (a && a->kind == AST_NULL_LITERAL) {
                int ns = zan_null_arg_score(pt);
                if (ns < 0) return -1;
                score += ns;
                continue;
            }
            zan_type_t *at = infer_expr_type(g, a, locals);
            if (at && at->sym && pt->sym && pt->type_arg_count == 0 &&
                at->type_arg_count == 0) {
                if (at->kind == TYPE_INTERFACE) {
                    if (at->sym == pt->sym) { score += 4; continue; }
                    if (zan_binder_class_implements_iface(at->sym, pt->sym))
                        score += 1;
                    continue;
                }
                if (at->kind == TYPE_CLASS &&
                    zan_binder_class_implements_iface(at->sym, pt->sym))
                    score += 1;
            }
            continue;
        }
        int s = concrete_arg_score(g, pt, a, locals);
        if (s < 0) return -1;
        score += s;
    }
    return score;
}

static bool ext_receiver_interface_match(zan_type_t *pt, zan_type_t *recv_ty) {
    return pt && recv_ty && pt->kind == TYPE_INTERFACE &&
           pt->type_arg_count == 0 &&
           (recv_ty->kind == TYPE_CLASS || recv_ty->kind == TYPE_INTERFACE) &&
           class_implements_iface(recv_ty->sym, pt->sym);
}

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

static zan_symbol_t *find_extension_method(zan_irgen_t *g, zan_type_t *recv_ty,
                                           zan_istr_t name, int argc,
                                           zan_ast_node_t *call,
                                           zan_ast_node_t *recv_expr,
                                           local_scope_t *locals) {
    if (!recv_ty) return NULL;
    zan_symbol_t *best = NULL;
    int best_score = -1;

    /* 优先从 binder 的全量扩展方法登记表中查找：包含泛型方法模板（其在发射阶段
     * 因带有类型参数未特化而被 method_is_tp_template 略过，未进 g->functions）。 */
    int ext_count = (g->binder && g->binder->ext_methods) ? g->binder->ext_method_count : 0;
    for (int i = 0; i < ext_count; i++) {
        zan_symbol_t *m = g->binder->ext_methods[i].method;
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
        zan_type_t *pt = g->binder->ext_methods[i].recv_type;
        if (!pt) pt = zan_binder_resolve_type(g->binder, p0->param.type);
        if (!pt) continue;
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
    if (best && best_score >= 0) return best;

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
        zan_ast_node_t *p0 = ps->count > 0 ? ps->items[0] : NULL;
        if (!p0 || p0->kind != AST_PARAM || !p0->param.is_this) continue;
        zan_type_t *pt = zan_binder_resolve_type(g->binder, p0->param.type);
        if (!pt) continue;
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

    zan_diag_emit_code(g->diag, DIAG_ERROR, arg->loc, 4001,
                  "shared table %s width %lld is out of range: a width must be "
                  "between 1 and %lld bytes",
                  what, (long long)arg->int_val, limit);
}

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
    zan_symbol_t *cls = type_sym;
    while (cls) {
        for (int i = 0; i < cls->member_count; i++) {
            zan_symbol_t *m = cls->members[i];
            if (m->kind != SYM_METHOD || !m->decl ||
                m->decl->kind != AST_METHOD_DECL) continue;
            if (m->name.len != name.len ||
                memcmp(m->name.str, name.str, name.len) != 0) continue;
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
    if (best && best_score >= 0) {
        /* A443：Convert.ToString 的真重载进了 stdlib，但内建特判覆盖的形状
         * 面更宽。宽容分曾把不该进的实参送进真方法：可空→标量 0 分
         * （type_family(可空)=UNKNOWN 走"宽容放过"），box 指针当 i64 传，
         * LLVM verify 当场炸；枚举/数值协变 2 分虽然能传，但内建对它们的
         * 渲染才是被 reject/golden 锁住的既有行为。收敛口径：只有精确
         * 类型匹配（分=4）才解析到真方法，其余形状静默交还调用方——
         * 与「候选一个都没有」的旧路径同一落点（object 指针拷贝、可空
         * emit_to_cstr_u、class/struct/array/delegate 的 A463 拒绝措辞
         * 全部原样保留）。 */
        if (best_score < 4 && name.len == 8 &&
            memcmp(name.str, "ToString", 8) == 0 &&
            type_sym->name.len == 7 &&
            memcmp(type_sym->name.str, "Convert", 7) == 0) {
            return NULL;
        }
        return best;
    }
    if (arity_matches > 0) {
        /* 无类型匹配的形状（int[]/object/class/struct/delegate）同理回落
         * 内建——曾在这里报死 "no overload matches"，顶掉 A463 措辞，
         * 四个 convert_to_string_* reject 用例齐红。 */
        if (name.len == 8 && memcmp(name.str, "ToString", 8) == 0 &&
            type_sym->name.len == 7 &&
            memcmp(type_sym->name.str, "Convert", 7) == 0) {
            return NULL;
        }
        zan_diag_emit_code(g->diag, DIAG_ERROR,
                      call ? call->loc : (zan_loc_t){0}, 2103,
                      "no overload of '%.*s.%.*s' matches argument type(s)",
                      (int)type_sym->name.len, type_sym->name.str,
                      (int)name.len, name.str);
        return NULL;
    }
    return resolve_overload(type_sym, name, argc, type_arg_count);
}

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
        int is_static = (m->modifiers & MOD_STATIC) != 0;
        int p0 = is_static ? 1 : 0;
        int variadic = method_is_params_variadic(m);
        int score = 0;
        if (variadic) {
            int fixed = ps->count - 1 - p0;
            if (argc < fixed) continue;
            if (!first_variadic) first_variadic = m;
            score = method_args_score(g, m, call, NULL, locals, p0);
            if (score < 0 && fixed > 0) continue;
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

static int op_index_param_offset(zan_symbol_t *m) {
    return (m->modifiers & MOD_STATIC) ? 1 : 0;
}

/* The result type of a member of a built-in type (List<T>, string,
 * Dictionary, StringBuilder).
 *
 * A built-in type has no binder symbol, so the symbol table cannot answer
 * "what does this member return" -- the built-in table can.  Without it the
 * type of `list.Contains(x)` was unknown, and the renderer that picks the
 * text for a bool fell back to the integer path: the same program printed
 * "true" for `Console.WriteLine(true)` but "1" for
 * `Console.WriteLine(list.Contains(x))`. */
static zan_type_t *builtin_member_result_type(zan_irgen_t *g, zan_type_t *rt,
                                               const char *name, int name_len) {
    if (!g || !g->binder || !rt || rt->sym || !name || name_len <= 0) return NULL;
    char tname[64];
    int n = rt->name.len;
    if (n <= 0 || n >= (int)sizeof(tname)) return NULL;
    memcpy(tname, rt->name.str, (size_t)n);
    tname[n] = 0;
    const char *res = zan_builtin_member_result(tname, name, name_len);
    if (!res) return NULL;
    if (strcmp(res, "string") == 0) return g->binder->type_string;
    if (strcmp(res, "int") == 0) return g->binder->type_int;
    if (strcmp(res, "long") == 0) return g->binder->type_long;
    if (strcmp(res, "double") == 0) return g->binder->type_double;
    if (strcmp(res, "bool") == 0) return g->binder->type_bool;
    /* the element type comes from the receiver: List<T>.ToArray -> T[],
     * Dictionary<K,V>.Keys -> List<K> */
    if (strcmp(res, "T[]") == 0 && rt->type_arg_count == 1)
        return zan_binder_make_array_type(g->binder, rt->type_args[0]);
    if (strcmp(res, "List<K>") == 0 && rt->type_arg_count == 2)
        return zan_binder_make_list_type(g->binder, rt->type_args[0]);
    if (strcmp(res, "List<V>") == 0 && rt->type_arg_count == 2)
        return zan_binder_make_list_type(g->binder, rt->type_args[1]);
    if (strcmp(res, "List<string>") == 0)
        return zan_binder_make_list_type(g->binder, g->binder->type_string);
    return NULL;
}

/* The static built-in class behind a primitive type name: `string.Format` is
 * a member of the static String class, not of the instance string type. */
static const char *builtin_static_class_for(const char *name) {
    if (strcmp(name, "string") == 0) return "String";
    if (strcmp(name, "char") == 0) return "Char";
    return NULL;
}

static zan_type_t *infer_expr_type_raw(zan_irgen_t *g, zan_ast_node_t *e,
                                       local_scope_t *locals);

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

static void infer_cache_invalidate(void) { g_infer_ctx.live = false; }

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
    if (t->kind == TYPE_DELEGATE) return t;
    if (t->kind == TYPE_TYPE_PARAM) return t;
    return subst_type_param_deep(g, t, g->cur_inst);
}

static int g_infer_depth;
static bool g_infer_bailed;

static zan_type_t *infer_expr_type(zan_irgen_t *g, zan_ast_node_t *e,
                                   local_scope_t *locals) {
    infer_cache_slot_t *slot = infer_cache_slot(g, e, locals);
    if (slot && slot->node == e) return slot->type;
    if (g_infer_depth >= ZAN_MAX_INFER_DEPTH) {
        if (!g_infer_bailed) {
            g_infer_bailed = true;
            zan_diag_emit_code(g->diag, DIAG_ERROR, e->loc, 2104,
                          "expression is nested too deeply to type "
                          "(inference recursed %d levels); split it into "
                          "statements", ZAN_MAX_INFER_DEPTH);
        }
        return NULL;
    }
    g_infer_depth++;
    zan_type_t *t = infer_expr_type_uncached(g, e, locals);
    g_infer_depth--;
    slot = infer_cache_slot(g, e, locals);
    if (slot) {
        slot->node = e;
        slot->type = t;
    }
    return t;
}

/* 两支数值提升（infer 侧）：与 checker 的 promote_numeric 同口径——
 * double > float > ulong > long > int，decimal 只与整数合。曾按 then 臂
 * 返回类型，(t?1:2.0)/2 被推成 int 除法得 0（C# 0.5，TASKS.md A514）。 */
static zan_type_t *irgen_promote_pair(zan_irgen_t *g, zan_type_t *a,
                                      zan_type_t *b) {
    if (!a) return b;
    if (!b) return a;
    if (a->kind == TYPE_DECIMAL) return (b->kind == TYPE_FLOAT ||
        b->kind == TYPE_DOUBLE) ? NULL : g->binder->type_decimal;
    if (b->kind == TYPE_DECIMAL) return (a->kind == TYPE_FLOAT ||
        a->kind == TYPE_DOUBLE) ? NULL : g->binder->type_decimal;
    if (a->kind == TYPE_DOUBLE || b->kind == TYPE_DOUBLE) return g->binder->type_double;
    if (a->kind == TYPE_FLOAT  || b->kind == TYPE_FLOAT)  return g->binder->type_float;
    if (a->kind == TYPE_ULONG  || b->kind == TYPE_ULONG)  return g->binder->type_ulong;
    if (a->kind == TYPE_LONG   || b->kind == TYPE_LONG)   return g->binder->type_long;
    if (a->kind == TYPE_UINT   || b->kind == TYPE_UINT)   return g->binder->type_uint;
    int fa = type_family(a);
    int fb = type_family(b);
    if ((fa == FAM_INT || fa == FAM_FLOAT || fa == FAM_DECIMAL) &&
        (fb == FAM_INT || fb == FAM_FLOAT || fb == FAM_DECIMAL)) {
        return g->binder ? g->binder->type_int : a;
    }
    return a;
}

static zan_type_t *infer_expr_type_raw(zan_irgen_t *g, zan_ast_node_t *e,
                                       local_scope_t *locals) {
    if (!e) return NULL;
    switch (e->kind) {
    case AST_STRING_LITERAL:
        return g->binder ? g->binder->type_string : NULL;
    case AST_INT_LITERAL:
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
        /* 与 checker 一致:f 后缀字面量推断为 float */
        if (g->binder && e->lit_suffix) { return g->binder->type_float; }
        return g->binder ? g->binder->type_double : NULL;
    case AST_DECIMAL_LITERAL:
        return g->binder ? g->binder->type_decimal : NULL;
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
        return infer_expr_type(g, e->await_expr.expr, locals);
    case AST_QUERY_EXPR: {
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
                break;
            }
        }
        if (e->query.group_expr) {
            zan_type_t *ge = infer_expr_type(g, e->query.group_expr, locals);
            if (!ge) ge = elem;
            zan_type_t *grp = zan_binder_make_grouping_type(g->binder, ge);
            if (e->query.group_into.len > 0) {
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
        {
            zan_type_t *rt = infer_expr_type(g, e->member.object, locals);
            if (zan_refl_is_typeinfo(rt)) {
                zan_type_t *mt = zan_refl_member_type(g->binder, rt,
                                                      e->member.name);
                if (mt) return mt;
            }
        }
        if (e->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, e->member.object->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder,
                                                 e->member.object->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *fs = get_field_sym(cs, e->member.name);
                if (fs) return fs->type;
            }
        }
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
                zan_type_t *ft = subst_type_param_deep(g, fs->type, ot);
                if (ft && ft->kind == TYPE_TYPE_PARAM) ft = concretize(g, ft);
                return ft;
            }
        }
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
            if (ot->kind == TYPE_ARRAY && e->member.name.len == 5 &&
                memcmp(e->member.name.str, "Count", 5) == 0)
                return g->binder->type_int;
        }
        return NULL;
    }
    case AST_INDEX: {
        zan_type_t *ot = infer_expr_type(g, e->index.object, locals);
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
        if (ot && type_named(ot, "Dict", 4) &&
            ot->type_arg_count == 2)
            return ot->type_args[1];
        if (ot && ot->kind == TYPE_STRING) return g->binder->type_char;
        return container_elem_type(ot);
    }
    case AST_CALL: {
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
        {
            zan_type_t *ct = infer_expr_type_raw(g, callee, locals);
            if (ct && (ct->kind == TYPE_CLASS || ct->kind == TYPE_STRUCT) && ct->sym) {
                zan_istr_t op_istr = {(char *)"op_call", 7};
                zan_symbol_t *op = resolve_op_overload(g, ct->sym,
                                                       op_istr, e, locals);
                if (op) return op->type;
            }
        }
        if (callee->kind == AST_IDENTIFIER || callee->kind == AST_MEMBER_ACCESS) {
            zan_type_t *dt = infer_expr_type_raw(g, callee, locals);
            if (dt && dt->kind == TYPE_DELEGATE)
                return dt->delegate_ret_type;
        }
        if (callee->kind == AST_MEMBER_ACCESS) {
            zan_istr_t mm = callee->member.name;
            if (mm.len == 6 && memcmp(mm.str, "AsSpan", 6) == 0) {
                zan_type_t *aot = infer_expr_type(g, callee->member.object, locals);
                if (aot && aot->kind == TYPE_ARRAY && aot->element_type)
                    return zan_binder_make_span_type(g->binder, aot->element_type);
                if (aot && aot->kind == TYPE_STRING)
                    return zan_binder_make_readonly_span_type(g->binder, g->binder->type_char);
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
            /* A558：Object 方法族推断——接收者是 object/接口/类且该类型
             * 自身（含基类链/接口）未声明同名成员时：Equals(1)→bool、
             * GetHashCode(0)→int。ToString(0) 由下方通用启发式覆盖
             * （返回 string）。缺 Equals 覆写的声明时也不劫持：让普通
             * 推断路径先走。 */
            if (mm.len == 6 && memcmp(mm.str, "Equals", 6) == 0 &&
                e->call.args.count == 1) {
                zan_type_t *eot = infer_expr_type(g, callee->member.object, locals);
                if (eot && (eot->kind == TYPE_OBJECT || eot->kind == TYPE_INTERFACE ||
                            eot->kind == TYPE_CLASS)) {
                    zan_symbol_t *osym = eot->sym;
                    bool declared = osym && resolve_overload(osym, mm, 1, 0) != NULL;
                    if (eot->kind == TYPE_INTERFACE && osym)
                        declared = resolve_iface_overload(osym, mm, 1, 0) != NULL;
                    if (!declared) return g->binder->type_bool;
                }
            }
            if (mm.len == 11 && memcmp(mm.str, "GetHashCode", 11) == 0 &&
                e->call.args.count == 0) {
                zan_type_t *eot = infer_expr_type(g, callee->member.object, locals);
                if (eot && (eot->kind == TYPE_OBJECT || eot->kind == TYPE_INTERFACE ||
                            eot->kind == TYPE_CLASS)) {
                    zan_symbol_t *osym = eot->sym;
                    bool declared = osym && resolve_overload(osym, mm, 0, 0) != NULL;
                    if (eot->kind == TYPE_INTERFACE && osym)
                        declared = resolve_iface_overload(osym, mm, 0, 0) != NULL;
                    if (!declared) return g->binder->type_int;
                }
            }
            /* string 实例方法启发式——char/Char 的静态类（char.ToUpper 等，
             * A525）成员名与之相撞（ToUpper/ToLower），先跳过让它落到
             * 下面的静态内建分支按 Char 表解析。 */
            int recv_is_char_cls = callee->member.object->kind == AST_IDENTIFIER &&
                callee->member.object->ident.name.len == 4 &&
                (memcmp(callee->member.object->ident.name.str, "char", 4) == 0 ||
                 memcmp(callee->member.object->ident.name.str, "Char", 4) == 0);
            if (!recv_is_char_cls &&
                ((mm.len == 9 && memcmp(mm.str, "Substring", 9) == 0) ||
                (mm.len == 8 && memcmp(mm.str, "ToString", 8) == 0) ||
                (mm.len == 4 && memcmp(mm.str, "Trim", 4) == 0) ||
                (mm.len == 7 && memcmp(mm.str, "Replace", 7) == 0) ||
                (mm.len == 7 && memcmp(mm.str, "ToUpper", 7) == 0) ||
                (mm.len == 7 && memcmp(mm.str, "ToLower", 7) == 0)))
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
            if (g->current_type_sym) {
            zan_symbol_t *m = resolve_overload_typed(g, g->current_type_sym,
                                                     callee->ident.name, e, locals);
            if (!m) m = get_method_sym(g->current_type_sym, callee->ident.name);
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
            if (obj->kind == AST_IDENTIFIER && !local_find(locals, obj->ident.name)) {
                char cls[64];
                int cn = (int)obj->ident.name.len;
                if (cn > 0 && cn < (int)sizeof(cls)) {
                    memcpy(cls, obj->ident.name.str, (size_t)cn);
                    cls[cn] = 0;
                    const zan_builtin_type_t *bt = zan_builtin_find(cls);
                    const char *bcls = cls;
                    if (bt && !bt->is_static) {
                        /* `string.IsNullOrEmpty(x)` is a member of the static
                         * String class, not of the instance string type */
                        const char *alias = builtin_static_class_for(cls);
                        if (alias) {
                            const zan_builtin_type_t *abt = zan_builtin_find(alias);
                            if (abt && abt->is_static) { bt = abt; bcls = alias; }
                        }
                    }
                    const char *res = bt && bt->is_static
                        ? zan_builtin_member_result(bcls, callee->member.name.str,
                                                    (int)callee->member.name.len)
                        : NULL;
                    if (res) {
                        if (strcmp(res, "string") == 0) return g->binder->type_string;
                        if (strcmp(res, "int") == 0) return g->binder->type_int;
                        if (strcmp(res, "long") == 0) return g->binder->type_long;
                        if (strcmp(res, "double") == 0) return g->binder->type_double;
                        if (strcmp(res, "bool") == 0) return g->binder->type_bool;
                        if (strcmp(res, "char") == 0) return g->binder->type_char;
                        if (strcmp(res, "List<string>") == 0)
                            return zan_binder_make_list_type(g->binder,
                                                             g->binder->type_string);
                    }
                    /* A529：标量实例类型上的 native 静态成员（int.Parse 等）
                     * 曾推断为 NULL——`int.Parse("9").ToString()` 断在
                     * "could not be resolved"（var x = int.Parse(...) 与
                     * 比较运算不吃 infer 才侥幸能用）。返回类型就是标量
                     * 本身，与 irgen_call.c 的原生分派点同口径。 */
                    if (bt && !bt->is_static &&
                        zan_builtin_has_native_member(cls, cn,
                            callee->member.name.str,
                            (int)callee->member.name.len)) {
                        if (strcmp(cls, "int") == 0) return g->binder->type_int;
                        if (strcmp(cls, "long") == 0) return g->binder->type_long;
                        if (strcmp(cls, "double") == 0) return g->binder->type_double;
                        if (strcmp(cls, "float") == 0) return g->binder->type_float;
                    }
                }
            }
            /* static: Namespace */
            if (obj->kind == AST_MEMBER_ACCESS && is_name_path(obj)) {
                zan_ast_node_t *head = name_path_head(obj);
                if (head && !local_find(locals, head->ident.name)) {
                    zan_symbol_t *ts = zan_binder_lookup(g->binder, obj->member.name);
                    if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT) &&
                        qpath_ns_ok(g, obj, ts)) {
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
                zan_symbol_t *m = resolve_overload_typed(g, rt->sym,
                    callee->member.name, e, locals);
                if (!m) m = get_method_sym(rt->sym, callee->member.name);
                if (m) {
                    zan_type_t *mt = method_ret_type_at(g, m, e, obj, locals);
                    if (!mt) mt = m->type;
                    mt = subst_type_param_deep(g, mt, rt);
                    if (mt && mt->kind == TYPE_TYPE_PARAM) mt = concretize(g, mt);
                    return mt;
                }
            }
            /* a built-in type (List<T>, string, Dictionary) has no symbol:
             * its members' result types come from the built-in table */
            if (rt) {
                zan_type_t *bt = builtin_member_result_type(g, rt,
                    callee->member.name.str, (int)callee->member.name.len);
                if (bt) return bt;
            }
            zan_symbol_t *xm = find_extension_method(g, rt, callee->member.name,
                                                     e->call.args.count,
                                                     e, obj, locals);
            if (xm) return method_ret_type_at(g, xm, e, obj, locals);
        }
        return NULL;
    }
    case AST_NEW_EXPR:
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
        return NULL;
    case AST_UNARY:
        if (e->unary.op == TK_BANG)
            return g->binder ? g->binder->type_bool : NULL;
        return infer_expr_type(g, e->unary.operand, locals);
    case AST_POSTFIX_UNARY:
        return infer_expr_type(g, e->unary.operand, locals);
    case AST_BINARY:
        if (e->binary.op == TK_PLUS && is_string_expr(g, e, locals))
            return g->binder->type_string;
        /* 运算符重载：先解析重载方法并返回它的返回类型。
         *
         * 不做这一步的话，`a * s + b`（重载结果再参与下一次运算）这种
         * 表达式的类型推不出来——左操作数是"结构体 × 标量"，左右类型
         * 不同 kinds，下面的数值分支会返回 NULL；上层拿不到类型就跳过
         * 重载分派，代码生成退化成整数加法，LLVM 校验直接失败
         * （"Integer arithmetic operators only work with integral types"）。 */
        {
            const char *op_name = NULL;
            switch (e->binary.op) {
            case TK_PLUS:       op_name = "op_add"; break;
            case TK_MINUS:      op_name = "op_sub"; break;
            case TK_STAR:       op_name = "op_mul"; break;
            case TK_SLASH:      op_name = "op_div"; break;
            case TK_PERCENT:    op_name = "op_mod"; break;
            case TK_EQ_EQ:      op_name = "op_eq"; break;
            case TK_BANG_EQ:    op_name = "op_neq"; break;
            case TK_LESS:       op_name = "op_lt"; break;
            case TK_GREATER:    op_name = "op_gt"; break;
            case TK_LESS_EQ:    op_name = "op_le"; break;
            case TK_GREATER_EQ: op_name = "op_ge"; break;
            default: break;
            }
            if (op_name) {
                zan_type_t *lt = infer_expr_type(g, e->binary.left, locals);
                if (lt && lt->sym &&
                    (lt->kind == TYPE_CLASS || lt->kind == TYPE_STRUCT)) {
                    zan_istr_t op_istr = { (char *)op_name, (int)strlen(op_name) };
                    zan_ast_node_t *op_probe = zan_ast_new(g->arena, AST_CALL,
                                                           e->loc);
                    op_probe->call.callee = NULL;
                    zan_ast_list_init(&op_probe->call.args);
                    zan_ast_list_init(&op_probe->call.type_args);
                    zan_ast_list_push(&op_probe->call.args, e->binary.right,
                                      g->arena);
                    zan_symbol_t *op = resolve_op_overload(g, lt->sym, op_istr,
                                                           op_probe, locals);
                    if (op) return op->type;
                }
            }
        }
        switch (e->binary.op) {
        case TK_PLUS: case TK_MINUS: case TK_STAR: case TK_SLASH:
        case TK_PERCENT: case TK_AMP: case TK_PIPE: case TK_CARET:
        case TK_LESS_LESS: case TK_GREATER_GREATER:
        case TK_GREATER_GREATER_GREATER: {
            zan_type_t *lt = infer_expr_type(g, e->binary.left, locals);
            zan_type_t *rt = infer_expr_type(g, e->binary.right, locals);
            if (lt && lt->kind == TYPE_NULLABLE) return lt;
            if (rt && rt->kind == TYPE_NULLABLE) return rt;
            if ((lt && lt->kind == TYPE_ULONG) || (rt && rt->kind == TYPE_ULONG))
                return g->binder->type_ulong;
            if (lt && rt && lt->kind == rt->kind) return lt;
            if (lt && rt) {
                /* A510 收窄的前置：混合位宽数值要按 C# 提升定型
                 * （long+int → long）。曾返回 NULL——发射端 coerce_int_pair
                 * 会把臂对齐到 64 位所以数值碰巧没错，但定型为 NULL 后外层
                 * 表达式（如 `(long子式)/int`）会取另一臂的 int 当整个表达
                 * 式的类型；A510 的 32 位收窄按这个错误定型截断 long 臂，
                 * 2147483648 被截回 -2147483648（numdig/Encoding.zan 一线）。
                 * 用 A514 同一条 irgen_promote_pair。 */
                zan_type_t *m = irgen_promote_pair(g, lt, rt);
                if (m) return m;
            }
            if (lt && !rt) return lt;
            if (rt && !lt) return rt;
            return NULL;
        }
        case TK_EQ_EQ: case TK_BANG_EQ: case TK_LESS:
        case TK_LESS_EQ: case TK_GREATER: case TK_GREATER_EQ:
        case TK_AMP_AMP: case TK_PIPE_PIPE:
            return g->binder ? g->binder->type_bool : NULL;
        case TK_QUESTION_QUESTION: {
            zan_type_t *lt = infer_expr_type(g, e->binary.left, locals);
            zan_type_t *rt = infer_expr_type(g, e->binary.right, locals);
            if (lt && lt->kind == TYPE_NULLABLE && lt->element_type) {
                if (rt && rt->kind == TYPE_NULLABLE) return rt;
                return lt->element_type;
            }
            if (!lt) return rt;
            if (!rt) return lt;
            if (lt == rt) return lt;
            zan_type_t *m = irgen_promote_pair(g, lt, rt);
            if (m) return m;
            return lt;
        }
        default:
            return NULL;
        }
    case AST_TYPEOF_EXPR:
        return g->binder->type_typeinfo;
    case AST_STRING_INTERP:
        return g->binder->type_string;
    case AST_CAST_EXPR:
        return resolve_type_ctx(g, e->cast.type);
    case AST_CONDITIONAL: {
        /* 两支提升合一定型（C# ?: 取公共提升型），不是 then 臂——
         * infer 与家族合并（expr_family）同口径，TASKS.md A514。 */
        zan_type_t *lt = infer_expr_type(g, e->conditional.then_expr, locals);
        zan_type_t *rt = infer_expr_type(g, e->conditional.else_expr, locals);
        if (!lt) return rt;
        if (!rt) return lt;
        if (lt == rt) return lt;
        zan_type_t *m = irgen_promote_pair(g, lt, rt);
        if (m) return m;
        return lt; /* 非数值混合维持旧行为（then 臂） */
    }
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
        return infer_expr_type(g, e->with_expr.expr, locals);
    default:
        return NULL;
    }
}

static bool expr_is_ulong(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && t->kind == TYPE_ULONG;
}

static bool expr_is_uint(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && t->kind == TYPE_UINT;
}

static bool expr_is_longish(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && (t->kind == TYPE_LONG || t->kind == TYPE_ULONG);
}

static bool expr_is_int32(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && t->kind == TYPE_INT;
}

static bool expr_is_bool(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && t->kind == TYPE_BOOL;
}

static bool expr_is_unsigned_int(zan_irgen_t *g, zan_ast_node_t *e,
                                 local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    if (!t) return false;
    if (t->kind == TYPE_ULONG || t->kind == TYPE_UINT ||
        t->kind == TYPE_USHORT || t->kind == TYPE_BYTE)
        return true;
    return false;
}

static bool expr_is_char(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && t->kind == TYPE_CHAR;
}

static zan_symbol_t *expr_class_sym(zan_irgen_t *g, zan_ast_node_t *e,
                                    local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
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

static void emit_weak_read_guard(zan_irgen_t *g, zan_ast_node_t *read_expr,
                                 LLVMValueRef value, zan_loc_t loc,
                                 local_scope_t *locals) {
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

static LLVMValueRef emit_guarded_member_object_ex(zan_irgen_t *g,
                                                  zan_ast_node_t *member,
                                                  local_scope_t *locals,
                                                  bool async_gate) {
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
            /* A550：async 被调者 + null 接收者不能走软基址——scratch 会被
             * 当协程帧调度，协程序言的虚表派发/retain 读哨兵字节。走到这里
             * = 不在 try（在 try 的 emit_weak_read_guard 已 longjmp 转
             * NRE），async 调用没有有效续延，响亮中止；同步调用维持软基址
             * （成员访问触发软诊断，报告后继续）。 */
            if (async_gate) {
                LLVMValueRef gfn = LLVMGetBasicBlockParent(
                    LLVMGetInsertBlock(g->builder));
                LLVMBasicBlockRef live_bb = LLVMAppendBasicBlockInContext(
                    g->ctx, gfn, "recv.live");
                LLVMBasicBlockRef dead_bb = LLVMAppendBasicBlockInContext(
                    g->ctx, gfn, "recv.dead");
                LLVMBuildCondBr(g->builder, isnull, dead_bb, live_bb);
                LLVMPositionBuilderAtEnd(g->builder, dead_bb);
                LLVMTypeRef abty = LLVMFunctionType(
                    LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
                zan_call2(g->builder, abty,
                    get_libc_fn(g, "abort", abty), NULL, 0, "");
                LLVMBuildUnreachable(g->builder);
                LLVMPositionBuilderAtEnd(g->builder, live_bb);
                return value;
            }
            value = emit_soft_base_select(g, value, isnull,
                                          member->member.object->loc);
        }
    }
    return value;
}

/* 读路径（字段/下标/属性等）维持原口径：软基址替换，报告后继续。 */
static LLVMValueRef emit_guarded_member_object(zan_irgen_t *g,
                                               zan_ast_node_t *member,
                                               local_scope_t *locals) {
    return emit_guarded_member_object_ex(g, member, locals, false);
}

static bool class_implements_iface(zan_symbol_t *cls, zan_symbol_t *iface) {
    /* 与 checker 共用 binder 里的同一份判定：两边对"接口方法算不算这个
     * 类型的成员"必须口径一致，否则 checker 报"没有这个方法"而 irgen
     * 又能把同一个调用发出去。 */
    return zan_binder_class_implements_iface(cls, iface);
}

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

/* A443：Convert.ToString 的真重载进 stdlib 后，内建特判只该在「实参与
 * 某个 ToString 重载的形参**精确同型**」时让路——精确匹配的实参由静态
 * 解析（irgen_call.c 的 resolve_overload_typed 路径）发真方法；其余形状
 * （可空/object/枚举/数组/协变数值）继续走内建：内建的形状覆盖面
 * （可空 emit_to_cstr_u、object 指针拷贝、class/struct/array/delegate 的
 * A463 拒绝措辞）是 reject/golden 锁住的既有行为，真方法的参数表表达
 * 不了，宽容打分更会把可空的 box 指针当 i64 送进真方法（LLVM verify 炸）。 */
static bool zan_convert_tostring_exact_decl(zan_irgen_t *g,
                                            zan_ast_node_t *arg,
                                            local_scope_t *locals) {
    if (!g || !g->binder || !arg) return false;
    zan_istr_t tn = { "Convert", 7 };
    zan_symbol_t *sym = zan_binder_lookup(g->binder, tn);
    if (!sym || !sym->type ||
        (sym->type->kind != TYPE_CLASS && sym->type->kind != TYPE_STRUCT))
        return false;
    zan_type_t *at = infer_expr_type(g, arg, locals);
    if (!at || type_mentions_tp(at)) return false;
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (m->kind != SYM_METHOD || !m->decl ||
            m->decl->kind != AST_METHOD_DECL) continue;
        if (m->name.len != 8 || memcmp(m->name.str, "ToString", 8) != 0)
            continue;
        if (m->decl->method_decl.params.count != 1) continue;
        zan_type_t *pt = method_param_type(g, m, 0);
        if (pt && types_concrete_equal(pt, at)) return true;
    }
    return false;
}


static void emit_runtime_check(zan_irgen_t *g, LLVMValueRef is_error,
                               zan_loc_t loc, const char *msg);

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
    emit_rt_alloc_add(g, bytes);
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

static LLVMValueRef zan_mdarray_alloc(zan_irgen_t *g, LLVMValueRef *dims,
                                      int rank, LLVMTypeRef elem_llvm) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMValueRef total = LLVMConstInt(i64, 1, 0);
    for (int d = 0; d < rank; d++)
        total = zan_mul(g->builder, total, dims[d], "md.total");
    LLVMValueRef neg = zan_icmp(g->builder, LLVMIntSLT, total,
        LLVMConstInt(i64, 0, 0), "md.neg");
    emit_runtime_check(g, neg, (zan_loc_t){0}, "array allocation overflow");
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
    emit_rt_alloc_add(g, bytes);
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
    LLVMTypeRef tmp_ty = LLVMArrayType(i8, 24);
    LLVMValueRef tmp = LLVMBuildAlloca(g->builder, tmp_ty, "itoa.tmp");
    LLVMValueRef zero64 = LLVMConstInt(i64t, 0, 0);
    LLVMValueRef ten = LLVMConstInt(i64t, 10, 0);
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
