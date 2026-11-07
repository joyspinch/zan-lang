#include "irgen_internal.h"
/* irgen_call.c - 表达式调用求值、虚表与实例分发、方法重载解析与未解析诊断 */
LLVMValueRef emit_expr_call(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
    bool matched = false;
    /* B-ID62: 符号级 Intrinsic 分发门控。
     * 若已解析绑定为用户自定义符号且无 [Intrinsic] 标注，
     * 则完全绕过字符串横截，根除符号劫持与意外冲突；
     * 仅当为明确的 Intrinsic 原语或无用户符号绑定的内置调用时才进入横截。 */
    if (expr->call.resolved_sym != NULL && !expr->call.is_intrinsic) {
        /* 用户普通符号与重载：直接跳过内置横截，进入常规方法分发 */
    } else {
        LLVMValueRef builtin_res = emit_builtin_call_intercept(g, expr, locals, &matched);
        if (matched) return builtin_res;
    }

/* Composite Dispatcher Submodules */
#include "call/call_collections.c"
#include "call/call_dispatch.c"
#include "call/call_ident.c"
}
