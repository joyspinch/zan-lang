#include "irgen_internal.h"
#include "irgen_internal.h"
/* irgen_expr_core */
zan_type_t *subst_type_param(zan_type_t *t, zan_type_t *recv);
zan_type_t *concretize(zan_irgen_t *g, zan_type_t *t);
zan_type_t *subst_type_param_deep(zan_irgen_t *g, zan_type_t *t,
                                         zan_type_t *recv);
bool type_is_concrete(zan_type_t *t);
int is_rc_managed_type(zan_type_t *t);
LLVMValueRef get_array_release_decl(zan_irgen_t *g, zan_type_t *elem_type, int rect);
LLVMValueRef get_array_desc(zan_irgen_t *g, zan_type_t *elem_type);
LLVMValueRef get_libc_fn(zan_irgen_t *g, const char *name, LLVMTypeRef ty);
LLVMValueRef emit_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_identifier(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_unary(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_binary(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_assignment(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_incdec_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals, int is_prefix);
LLVMValueRef emit_expr_string_interp(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_member_access(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_index(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_query_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_tuple(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_new_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_conditional(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_switch_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_with_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_await_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_expr_cast_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
LLVMValueRef emit_ref_arg(zan_irgen_t *g, zan_ast_node_t *arg, local_scope_t *locals);
LLVMValueRef emit_expr_call(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);

/* 判断目标平台是否为 WebAssembly */
void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals);
void emit_runtime_check(zan_irgen_t *g, LLVMValueRef is_error,
                               zan_loc_t loc, const char *msg);
LLVMValueRef emit_soft_base_select(zan_irgen_t *g, LLVMValueRef base,
                                          LLVMValueRef isnull, zan_loc_t loc);
/* 发射 Lambda 表达式并推断参数与返回值类型 */
LLVMValueRef emit_lambda_typed(zan_irgen_t *g, zan_ast_node_t *expr,
                                      zan_type_t *expected, local_scope_t *locals);
zan_type_t *method_param_type(zan_irgen_t *g, zan_symbol_t *msym, int idx);
zan_type_t *method_param_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                        int idx, zan_ast_node_t *call,
                                        zan_ast_node_t *recv_expr,
                                        local_scope_t *locals);
zan_type_t *method_ret_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                      zan_ast_node_t *call,
                                      zan_ast_node_t *recv_expr,
                                      local_scope_t *locals);
bool method_ret_is_bare_tp(zan_symbol_t *msym);
/* 发射a call argument, typing a bare lambda from the target delegate param */
LLVMValueRef emit_arg_typed(zan_irgen_t *g, zan_ast_node_t *arg,
                                   zan_type_t *ptype, local_scope_t *locals);
LLVMValueRef coerce_to_i64(zan_irgen_t *g, LLVMValueRef v);
LLVMValueRef coerce_to_frame_result(zan_irgen_t *g, LLVMValueRef v,
                                           zan_type_t *ty);
LLVMValueRef coerce_from_frame_result(zan_irgen_t *g, LLVMValueRef res,
                                             zan_type_t *ty);
LLVMValueRef coerce_async_ret(zan_irgen_t *g, LLVMValueRef val);
void emit_async_cancel_check(zan_irgen_t *g, local_scope_t *locals);
LLVMBasicBlockRef get_async_requeue_bb(zan_irgen_t *g);
LLVMValueRef get_async_state_ptr(zan_irgen_t *g);
LLVMValueRef get_async_cancel_ptr(zan_irgen_t *g);
LLVMValueRef get_async_self_i8(zan_irgen_t *g);
LLVMValueRef get_async_self_int(zan_irgen_t *g, LLVMTypeRef ptr_int_ty);
LLVMValueRef get_async_child_ptr(zan_irgen_t *g);
LLVMValueRef get_async_sub_slot_ptr(zan_irgen_t *g);
LLVMValueRef get_async_result_ptr(zan_irgen_t *g);
void emit_co_cancel_delay(zan_irgen_t *g, LLVMValueRef frame);
LLVMValueRef get_co_cancel_fn(zan_irgen_t *g);
LLVMValueRef get_co_isdone_fn(zan_irgen_t *g);
LLVMValueRef get_co_reap_fn(zan_irgen_t *g);
LLVMValueRef get_co_track_fn(zan_irgen_t *g);
LLVMValueRef get_co_untrack_fn(zan_irgen_t *g);
bool anf_stmt_contains_await(zan_ast_node_t *s);
void emit_async_eh_unarm(zan_irgen_t *g);
LLVMBasicBlockRef get_async_suspend_ret_bb(zan_irgen_t *g);
void emit_async_check_sub_exc(zan_irgen_t *g, LLVMValueRef sub, LLVMValueRef tmp_mark);
LLVMValueRef emit_task_member(zan_irgen_t *g, LLVMValueRef hp,
                                     zan_type_t *rt, int mode) {
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef token = LLVMBuildPtrToInt(g->builder, hp, i64t, "task.token");
    if (mode == 2)
        return emit_task_runtime_call(g, "zan_task_isdone", i32, &token, 1);
    LLVMValueRef fn = g->current_fn;
    LLVMBasicBlockRef test_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "tk.test");
    LLVMBasicBlockRef pump_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "tk.pump");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "tk.done");
    LLVMBuildBr(g->builder, test_bb);
    LLVMPositionBuilderAtEnd(g->builder, test_bb);
    LLVMValueRef dn = emit_task_runtime_call(g, "zan_task_isdone", i32, &token, 1);
    LLVMValueRef dn1 = zan_icmp(g->builder, LLVMIntNE, dn,
                                LLVMConstInt(LLVMTypeOf(dn), 0, 0), "tk.dn1");
    LLVMBuildCondBr(g->builder, dn1, done_bb, pump_bb);
    LLVMPositionBuilderAtEnd(g->builder, pump_bb);
    zan_call2(g->builder, g->rt_co_sched_run_type, g->rt_co_sched_run, NULL, 0, "");
    LLVMBuildBr(g->builder, test_bb);
    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    if (mode == 1 || mode == 4) {
        LLVMValueRef args[] = { token, LLVMConstInt(i32, 1, 0) };
        LLVMValueRef frame = emit_task_runtime_call(g, "zan_task_acquire", i8ptr, args, 2);
        LLVMValueRef missing = zan_icmp(g->builder, LLVMIntEQ, frame,
            LLVMConstNull(i8ptr), "task.consumed");
        LLVMBasicBlockRef release_done = NULL;
        if (mode == 4) {
            LLVMBasicBlockRef release_body = LLVMAppendBasicBlockInContext(g->ctx, fn, "task.release");
            release_done = LLVMAppendBasicBlockInContext(g->ctx, fn, "task.release.done");
            LLVMBuildCondBr(g->builder, missing, release_done, release_body);
            LLVMPositionBuilderAtEnd(g->builder, release_body);
        } else {
            LLVMBasicBlockRef invalid = LLVMAppendBasicBlockInContext(g->ctx, fn, "task.invalid");
            LLVMBasicBlockRef valid = LLVMAppendBasicBlockInContext(g->ctx, fn, "task.valid");
            LLVMBuildCondBr(g->builder, missing, invalid, valid);
            LLVMPositionBuilderAtEnd(g->builder, invalid);
            emit_task_runtime_call(g, "abort", LLVMVoidTypeInContext(g->ctx), NULL, 0);
            LLVMBuildUnreachable(g->builder);
            LLVMPositionBuilderAtEnd(g->builder, valid);
        }
        if (mode == 4) {
            emit_task_runtime_call(g, "zan_task_discard", LLVMVoidTypeInContext(g->ctx), &frame, 1);
            emit_task_runtime_call(g, "zan_task_unpin", LLVMVoidTypeInContext(g->ctx), &frame, 1);
            LLVMBuildBr(g->builder, release_done);
            LLVMPositionBuilderAtEnd(g->builder, release_done);
            return LLVMConstInt(i32, 0, 0);
        }
        /* Consumption owns the frame until its one reap. Release the query pin
         * before exception transfer, whose existing helper may unwind immediately. */
        emit_task_runtime_call(g, "zan_task_unpin", LLVMVoidTypeInContext(g->ctx), &frame, 1);
        if (mode == 1) emit_async_check_sub_exc(g, frame, NULL);
        LLVMValueRef rp = LLVMBuildStructGEP2(g->builder, g->co_header_type,
            frame, ASYNC_FRAME_RESULT, "tk.resp");
        LLVMValueRef raw = LLVMBuildLoad2(g->builder, i64t, rp, "tk.raw");
        LLVMValueRef val = rt ? coerce_from_frame_result(g, raw, rt) : NULL;
        if (mode == 4 && rt) emit_rc_release_for_type(g, rt, val);
        LLVMBuildStore(g->builder, LLVMConstInt(i64t, 0, 0), rp);
        LLVMValueRef reap = get_co_reap_fn(g);
        zan_call2(g->builder, LLVMGlobalGetValueType(reap), reap, &frame, 1, "");
        if (mode == 1) return val;
        LLVMBuildBr(g->builder, release_done);
        LLVMPositionBuilderAtEnd(g->builder, release_done);
    }
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}
bool is_name_path(zan_ast_node_t *node) {
    if (!node) return false;
    if (node->kind == AST_IDENTIFIER) return true;
    if (node->kind == AST_MEMBER_ACCESS) return is_name_path(node->member.object);
    return false;
}
zan_ast_node_t *name_path_head(zan_ast_node_t *node) {
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
bool qpath_ns_ok(zan_irgen_t *g, zan_ast_node_t *path, zan_symbol_t *sym) {
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
void check_implicit_narrowing(zan_irgen_t *g, zan_type_t *dst,
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
zan_symbol_t *find_user_conversion(zan_irgen_t *g, zan_type_t *from_type,
                                          zan_type_t *to_type, const char *op_name);
void check_value_type_mismatch(zan_irgen_t *g, zan_type_t *dst, zan_type_t *src,
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
 * 踩过的坑（A449）：`Func<Order, object> f = (Order o) => o.id;` 编译期一声
 * 不吭，运行期把 int 值当对象指针（IntToPtr）返回，调用方拿到非 null 的野
 * 指针，进程退出时 ARC 去回收这个假对象再崩一次（0xC0000005）。
 * 委托返回类型与 lambda 体类型对不上时，必须在编译期拦下来。 */
bool irgen_type_is_value_kind(zan_type_t *t) {
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
void check_no_boxing_conversion(zan_irgen_t *g, zan_type_t *dst,
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
int render_type_full(zan_type_t *t, char *buf, int cap) {
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
    if (t->kind == TYPE_NULLABLE && t->element_type) {
        n = render_type_full(t->element_type, buf, cap);
        if (n < cap - 1) buf[n++] = '?';
        if (n < cap) buf[n] = '\0';
        return n;
    }
    if (t->name.len) {
        zan_istr_t ns = (zan_istr_t){NULL, 0};
        zan_symbol_t *s = t->sym;
        while (s) {
            if (s->decl && zan_ast_ns_name(s->decl).len) {
                ns = zan_ast_ns_name(s->decl);
                break;
            }
            s = s->parent;
        }
        if (ns.len && !memchr(t->name.str, '.', (size_t)t->name.len)) {
            int nw = (int)ns.len;
            if (nw > cap - n - 1) nw = cap - n - 1;
            memcpy(buf + n, ns.str, (size_t)nw);
            n += nw;
            if (n < cap - 1) buf[n++] = '.';
        }
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
void check_generic_invariance(zan_irgen_t *g, zan_type_t *dst, zan_type_t *src,
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
zan_type_t *infer_expr_type(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);

static zan_type_t *field_type_through(zan_irgen_t *g, zan_symbol_t *fsym,
                                     zan_type_t *owner) {
    if (!fsym) return NULL;
    zan_type_t *ft = fsym->type;
    if (!owner) return ft;
    ft = subst_type_param_deep(g, ft, owner);
    if (ft && ft->kind == TYPE_TYPE_PARAM) ft = concretize(g, ft);
    return ft;
}
zan_type_t *member_access_field_type(zan_irgen_t *g, local_scope_t *locals, zan_ast_node_t *member) {
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
zan_type_t *infer_expr_type(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
zan_type_t *container_elem_type(zan_type_t *t);
zan_type_t *generic_method_ret(zan_irgen_t *g, zan_symbol_t *msym,
                                      zan_ast_node_t *call, local_scope_t *locals);
int render_type_ref_name(const zan_ast_node_t *t, char *buf, int cap) {
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
bool is_string_expr(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
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
    case AST_AWAIT_EXPR: {
        /* Lazy async arms retain await nodes instead of ANF locals. String
         * operators must recognize their result type to consume owned results. */
        zan_type_t *rt = infer_expr_type(g, e, locals);
        return rt && rt->kind == TYPE_STRING;
    }
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
zan_type_t *container_elem_type(zan_type_t *t) {
    if (!t) return NULL;
    if (t->element_type) return t->element_type;
    if (t->type_args && t->type_arg_count > 0) return t->type_args[0];
    return NULL;
}
int is_span_type(zan_type_t *t) {
    if (!t || t->kind != TYPE_STRUCT) return 0;
    if (t->name.len == 4 && memcmp(t->name.str, "Span", 4) == 0) return 1;
    if (t->name.len == 12 && memcmp(t->name.str, "ReadOnlySpan", 12) == 0) return 1;
    return 0;
}
zan_type_t *dict_key_type(zan_irgen_t *g, zan_type_t *t) {
    if (!t || !t->type_args || t->type_arg_count < 1) return g ? g->binder->type_string : NULL;
    return t->type_args[0];
}
zan_type_t *dict_value_type(zan_type_t *t) {
    if (!t || !t->type_args || t->type_arg_count < 2) return NULL;
    return t->type_args[1];
}
bool type_mentions_tp(zan_type_t *t);
zan_type_t *method_param_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                        int idx, zan_ast_node_t *call,
                                        zan_ast_node_t *recv_expr,
                                        local_scope_t *locals);
bool types_concrete_equal(zan_type_t *a, zan_type_t *b) {
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



