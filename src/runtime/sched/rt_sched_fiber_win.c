/* rt_sched_fiber_win.c: Windows (Win32) 原生纤程、换栈与 Worker 线程底座实现 */

#ifndef _WIN32
#error "rt_sched_fiber_win.c is only intended for Windows targets"
#endif

#if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0601
#undef _WIN32_WINNT
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <winsock2.h>
#include <windows.h>

/* ---- 互斥锁与原子操作 ---- */
typedef CRITICAL_SECTION zan_mutex_t;
static inline void zan_mutex_init(zan_mutex_t *m) { InitializeCriticalSection(m); }
static inline void zan_mutex_lock(zan_mutex_t *m) { EnterCriticalSection(m); }
static inline void zan_mutex_unlock(zan_mutex_t *m) { LeaveCriticalSection(m); }
static inline void zan_mutex_destroy(zan_mutex_t *m) { DeleteCriticalSection(m); }

static inline int zan_atomic_inc(volatile int *p) { return (int)InterlockedIncrement((volatile LONG *)p); }
static inline int zan_atomic_dec(volatile int *p) { return (int)InterlockedDecrement((volatile LONG *)p); }
static inline int zan_atomic_read(volatile int *p) { return (int)InterlockedCompareExchange((volatile LONG *)p, 0, 0); }

static inline void co_acquire(zan_co_t *co) {
    while (InterlockedCompareExchange((volatile LONG *)&co->running, 1, 0) != 0) {
        YieldProcessor();
    }
}

static inline void co_release(zan_co_t *co) {
    InterlockedExchange((volatile LONG *)&co->running, 0);
}

/* ---- 线程与唤醒同步 ---- */
typedef HANDLE plat_thread_t;

typedef struct {
    HANDLE wake_event;
} plat_worker_sync_t;

static inline void plat_worker_sync_init(plat_worker_sync_t *ws) {
    ws->wake_event = CreateEvent(NULL, FALSE, FALSE, NULL);
}

static inline void plat_worker_sync_destroy(plat_worker_sync_t *ws) {
    if (ws->wake_event) {
        CloseHandle(ws->wake_event);
        ws->wake_event = NULL;
    }
}

static inline void plat_worker_sync_wake_one(plat_worker_sync_t *ws) {
    SetEvent(ws->wake_event);
}

static inline void plat_worker_sync_wake_all(plat_worker_sync_t *ws) {
    SetEvent(ws->wake_event);
}

static inline void plat_worker_sync_wait(plat_worker_sync_t *ws, int timeout_ms) {
    WaitForSingleObject(ws->wake_event, (DWORD)(timeout_ms > 0 ? timeout_ms : 1));
}

static inline int plat_thread_create(plat_thread_t *th, void *(*fn)(void *), void *arg) {
    HANDLE h = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)fn, arg, 0, NULL);
    if (!h) {
        *th = NULL;
        return -1;
    }
    *th = h;
    return 0;
}

static inline void plat_thread_join(plat_thread_t th) {
    if (th) {
        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
    }
}

/* ---- 纤程与换栈上下文 ---- */
static ZAN_THREAD_LOCAL void *g_sched_fiber;

static void WINAPI co_trampoline(void *p);

static void plat_sched_enter(void) { g_sched_fiber = ConvertThreadToFiber(NULL); }
static void plat_sched_leave(void) { ConvertFiberToThread(); }
static void *plat_fiber_new(zan_co_t *co) {
    return CreateFiber((SIZE_T)co_stack_size(), co_trampoline, co);
}
static void plat_fiber_delete(void *f) { DeleteFiber(f); }
static void plat_switch(void *to)      { SwitchToFiber(to); }

#define ZAN_SLEEP_MS_MAX ((int64_t)0x7FFFFFFF)
static void plat_sleep(int64_t ms) {
    if (ms < 0) ms = 0;
    if (ms > ZAN_SLEEP_MS_MAX) ms = ZAN_SLEEP_MS_MAX;
    Sleep((DWORD)ms);
}
static int64_t plat_now_ms(void)       { return (int64_t)GetTickCount64(); }

static void plat_sched_drain_stacks(void) {
    /* Windows 纤程由系统 API 分配与回收，无私有 mmap 栈池 */
}

static void plat_switch_to_sched(void) {
    if (g_current) {
        g_current->last_error = (int32_t)GetLastError();
    }
    plat_switch(g_sched_fiber);
    if (g_current) {
        SetLastError((DWORD)g_current->last_error);
    }
}

static inline void plat_switch_co(zan_co_t *co) {
    co_acquire(co);
    g_current = co;
    co->slice_start_ms = plat_now_ms();
    co->slice_ticks = 0;
    co->preempt_requested = 0;
    SetLastError((DWORD)co->last_error);
    plat_switch(co->fiber);
    g_current = NULL;
}

static void WINAPI co_trampoline(void *p) {
    zan_co_t *co = (zan_co_t *)p;
    SetLastError(0);
    co->body(co->task);
    co->finished = 1;
    complete_task(co->task, 0);
    plat_switch_to_sched();
}

/* ---- Watchdog 抢占巡检后台线程 ---- */
static plat_thread_t g_watchdog_thread = NULL;
static void *watchdog_thread_main(void *arg);

static void plat_watchdog_start(void) {
    plat_thread_create(&g_watchdog_thread, watchdog_thread_main, NULL);
}

static void plat_watchdog_stop(void) {
    plat_thread_join(g_watchdog_thread);
    g_watchdog_thread = NULL;
}
