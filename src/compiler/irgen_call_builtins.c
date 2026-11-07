#include "irgen_internal.h"
/* irgen_call_builtins.c - 语言内置原语、数学、字符串、IO 与系统调用求值拦截 */

/* 按**目标**平台选 TLS errno 取址函数并清零，返回 errno 槽位指针。
 * Windows 宿主交叉编 macOS 时 IR 名 `_errno` 会被 Mach-O 再加前导下划线，
 * 而 libSystem 导出的 C 符号是 `__error`——链接期才炸，且炸在离病灶最远的
 * 调用点上。分支次序对齐 Socket.zan 的 DllImport 守卫（ANDROID 也定义
 * LINUX，安卓分支必须在前）。strtoll 溢出的唯一可靠信号是 errno=ERANGE：
 * 返回值分不清「输入恰好是极值」与「饱和」。 */
static LLVMValueRef emit_errno_preset(zan_irgen_t *g, LLVMTypeRef i32t) {
    LLVMTypeRef errno_ty = LLVMFunctionType(LLVMPointerType(i32t, 0), NULL, 0, 0);
    const char *errno_sym = "__errno_location";
    if (strstr(g->target_triple, "android") || strstr(g->target_triple, "ohos")) {
        errno_sym = "__errno";
    } else if (g->target_is_macos || g->target_is_ios) {
        errno_sym = "__error";
    } else if (g->target_is_windows) {
        errno_sym = "_errno";
    }
    LLVMValueRef errno_fn = get_libc_fn(g, errno_sym, errno_ty);
    LLVMValueRef errnop = zan_call2(g->builder, errno_ty, errno_fn, NULL, 0,
                                    "parse.errnop");
    LLVMBuildStore(g->builder, LLVMConstInt(i32t, 0, 0), errnop);
    return errnop;
}

LLVMValueRef emit_builtin_call_intercept(zan_irgen_t *g, zan_ast_node_t *expr,
                                        local_scope_t *locals, bool *matched) {
    if (matched) *matched = true;

/* Composite Dispatcher Submodules */
#include "call_builtins/builtin_sys.c"
#include "call_builtins/builtin_math.c"
#include "call_builtins/builtin_str.c"
#include "call_builtins/builtin_io.c"

    if (matched) *matched = false;
    return NULL;
}
