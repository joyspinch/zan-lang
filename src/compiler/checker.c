
#include "checker.h"
#include "reflect_api.h"
#include "definite.h"
#include "diag.h"
#include "arena.h"
#include "builtin_api.h"
#include <string.h>
#include <stdio.h>

static const char *type_name(zan_type_t *t);

/* 线程局部存储。LSP 进程里诊断线程与工作线程各跑各的 checker
 * （src/lsp/lsp_main.c），类型名缓冲池不能共用。写法抄 runtime 的
 * rt_sched.c。 */
#if defined(_MSC_VER)
#define ZAN_TLS __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#define ZAN_TLS __thread
#else
#define ZAN_TLS _Thread_local
#endif

/* 类型名缓冲池。
 *
 * type_name() 的返回值只活到本次 zan_diag_emit 返回——diag 立刻
 * vsnprintf 固化进 512 字节的 msgbuf，之后指针就没用了。但一条诊断里
 * 最多并列三个类型名（'cannot convert X to Y in Z'），所以要几个槽位
 * 轮转，不能只用一个。 */
#define TYPE_NAME_SLOTS 8
#define TYPE_NAME_SLOT_SIZE 192

static ZAN_TLS char g_type_name_pool[TYPE_NAME_SLOTS][TYPE_NAME_SLOT_SIZE];
static ZAN_TLS int g_type_name_slot;


/* Composite Dispatcher Submodules */
#include "checker/checker_util.c"
#include "checker/checker_type.c"
#include "checker/checker_call.c"
#include "checker/checker_ctor.c"
#include "checker/checker_expr_core.c"
#include "checker/checker_expr_adv.c"
#include "checker/checker_stmt.c"
#include "checker/checker_decl.c"
#include "checker/checker_unit.c"
