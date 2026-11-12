#if !defined(_WIN32) && !defined(__APPLE__)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
/* timegm is a GNU/BSD extension used for UTC/local offset conversion. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#endif

#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif

/* rand_s 声明开关——须先于 <stdlib.h>（Dictionary 键哈希盐的熵源，
 * 见 rt_timer_misc.c B-ID80 段） */
#if defined(_WIN32) && !defined(_CRT_RAND_S)
#define _CRT_RAND_S 1
#endif

#include "rt_timer.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include "../common/host_oom.h"
#include "../common/zan_abi.h"
#include "rt_oom.h"  /* B-ID98 站点化 OOM 注入 */

#if !defined(__wasm__) && !defined(ZAN_BARE_METAL)
#include "rt_crash.h"
#endif

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
typedef CRITICAL_SECTION zan_timer_mutex_t;
static INIT_ONCE g_lock_once = INIT_ONCE_STATIC_INIT;
static zan_timer_mutex_t g_lock;
static BOOL CALLBACK timer_lock_init(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once; (void)param; (void)ctx;
    InitializeCriticalSection(&g_lock);
    return TRUE;
}
static void timer_lock(void) {
    InitOnceExecuteOnce(&g_lock_once, timer_lock_init, NULL, NULL);
    EnterCriticalSection(&g_lock);
}
static void timer_unlock(void) { LeaveCriticalSection(&g_lock); }
#elif defined(__wasm__)
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
/* cloudlibc's time */
int clock_gettime(clockid_t, struct timespec *);
static void timer_lock(void) {}
static void timer_unlock(void) {}
#elif defined(ZAN_BARE_METAL)
#include <time.h>
static void timer_lock(void) {}
static void timer_unlock(void) {}
#else
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
typedef pthread_mutex_t zan_timer_mutex_t;
static zan_timer_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static void timer_lock(void) { pthread_mutex_lock(&g_lock); }
static void timer_unlock(void) { pthread_mutex_unlock(&g_lock); }
#endif

#if defined(_WIN32)
static unsigned long timer_self_tid(void) { return GetCurrentThreadId(); }
static void timer_yield(void) { Sleep(1); }
#elif defined(__wasm__) || defined(ZAN_BARE_METAL)
static unsigned long timer_self_tid(void) { return 1; }
static void timer_yield(void) { (void)0; }
#else
#include <sched.h>
static unsigned long timer_self_tid(void) {
    return (unsigned long)(uintptr_t)pthread_self();
}
static void timer_yield(void) { sched_yield(); }
#endif

/* 组合分发器（Composite Dispatcher）子模块按职责拓扑引入：
 * 1. rt_timer_diag: 跨平台参数转换、软失败日志记录与致命错误处理
 * 2. rt_timer_core: 核心定时器最小堆、超时调度、微秒单调时钟与纪元时间支持
 * 3. rt_timer_co:   协程生命周期存活跟踪与结构化汇聚（Join/WhenAll/WhenAny）
 * 4. rt_timer_misc: 异步执行器参数配置、浮点精确格式化、内存度量与 SipHash 字典键散列
 */
#include "timer/rt_timer_diag.c"
#include "timer/rt_timer_core.c"
#include "timer/rt_timer_co.c"
#include "timer/rt_timer_task.c"
#include "timer/rt_timer_misc.c"
