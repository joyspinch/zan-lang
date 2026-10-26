
#if defined(__APPLE__) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif
/* _XOPEN_SOURCE 在 Darwin 上等效关掉 BSD 层，MAP_ANON 藏在 _DARWIN_C_SOURCE
 * 之后（Linux 的 _GNU_SOURCE 管不着这里）——栈池的 mmap 需要它。 */
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif

#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif

#include "rt_sched.h"
#include "rt_io.h"
#include "rt_timer.h"
#include "rt_co.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
/* Darwin 的 mmap 只有 MAP_ANON（即使 _XOPEN_SOURCE/_GNU_SOURCE 也不给
 * MAP_ANONYMOUS 拼写）；栈池的 mmap 两侧统一走这个别名。 */
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif
#include <ucontext.h>
#include <time.h>
#include <unistd.h>
#endif
#include "../common/host_oom.h"

#define ZAN_CO_STACK_DEFAULT (128 * 1024)

static size_t co_stack_size(void) {
    static size_t cached;
    if (!cached) {
        size_t v = ZAN_CO_STACK_DEFAULT;
        const char *env = getenv("ZAN_CO_STACK");
        if (env && env[0]) {
            char *end = NULL;
            long long parsed = strtoll(env, &end, 10);
            if (end && end != env && parsed > 0) {
                if (parsed < 64 * 1024) parsed = 64 * 1024;
                if (parsed > 16LL * 1024 * 1024) parsed = 16LL * 1024 * 1024;
                v = (size_t)parsed;
            }
        }
        cached = v;
    }
    return cached;
}


#if defined(_MSC_VER)
#define ZAN_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#define ZAN_THREAD_LOCAL __thread
#else
#define ZAN_THREAD_LOCAL _Thread_local
#endif

#ifdef _WIN32
typedef CRITICAL_SECTION zan_mutex_t;
static inline void zan_mutex_init(zan_mutex_t *m) { InitializeCriticalSection(m); }
static inline void zan_mutex_lock(zan_mutex_t *m) { EnterCriticalSection(m); }
static inline void zan_mutex_unlock(zan_mutex_t *m) { LeaveCriticalSection(m); }
static inline void zan_mutex_destroy(zan_mutex_t *m) { DeleteCriticalSection(m); }

static inline int zan_atomic_inc(volatile int *p) { return (int)InterlockedIncrement((volatile LONG *)p); }
static inline int zan_atomic_dec(volatile int *p) { return (int)InterlockedDecrement((volatile LONG *)p); }
static inline int zan_atomic_read(volatile int *p) { return (int)InterlockedCompareExchange((volatile LONG *)p, 0, 0); }
#else
#include <pthread.h>
typedef pthread_mutex_t zan_mutex_t;
static inline void zan_mutex_init(zan_mutex_t *m) { pthread_mutex_init(m, NULL); }
static inline void zan_mutex_lock(zan_mutex_t *m) { pthread_mutex_lock(m); }
static inline void zan_mutex_unlock(zan_mutex_t *m) { pthread_mutex_unlock(m); }
static inline void zan_mutex_destroy(zan_mutex_t *m) { pthread_mutex_destroy(m); }

static inline int zan_atomic_inc(volatile int *p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
static inline int zan_atomic_dec(volatile int *p) { return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST); }
static inline int zan_atomic_read(volatile int *p) { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
#endif

typedef struct zan_co {
    void          *fiber;
    zan_co_body_t  body;
    zan_task_t    *task;
    int            finished;
    volatile int   running;
    struct zan_co *next;
} zan_co_t;

static inline void co_acquire(zan_co_t *co) {
#ifdef _WIN32
    while (InterlockedCompareExchange((volatile LONG *)&co->running, 1, 0) != 0) {
        YieldProcessor();
    }
#else
    while (__atomic_test_and_set(&co->running, __ATOMIC_ACQUIRE)) {
        #if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        __builtin_ia32_pause();
        #else
        __asm__ __volatile__("yield" ::: "memory");
        #endif
    }
#endif
}

static inline void co_release(zan_co_t *co) {
#ifdef _WIN32
    InterlockedExchange((volatile LONG *)&co->running, 0);
#else
    __atomic_clear(&co->running, __ATOMIC_RELEASE);
#endif
}

struct zan_task {
    zan_mutex_t lock;
    int         completed;
    int64_t     result;
    void       *arg;
    zan_co_t   *co;
    zan_co_t   *waiters;
    zan_task_t *all_next;
};

/* ---- timers ---- */

typedef struct zan_timer {
    int64_t     due_ms;
    zan_task_t *task;
} zan_timer_t;

#define ZAN_MAX_WORKERS 32
#define ZAN_LOCAL_QUEUE_CAP 256

typedef struct zan_worker {
    int          id;
    int          active;
    volatile int sleeping;
    zan_mutex_t  lock;
    zan_co_t    *local_head;
    zan_co_t    *local_tail;
    int          local_count;
#ifdef _WIN32
    HANDLE       thread;
    HANDLE       wake_event;
#else
    pthread_t       thread;
    pthread_cond_t  wake_cond;
    pthread_mutex_t wake_mutex;
#endif
} zan_worker_t;

static zan_worker_t        g_workers[ZAN_MAX_WORKERS];
static int                 g_worker_count = 1;
static volatile int        g_sched_stop   = 0;
static volatile int        g_live         = 0;

static ZAN_THREAD_LOCAL void     *g_sched_fiber;
static ZAN_THREAD_LOCAL zan_co_t *g_current;
static ZAN_THREAD_LOCAL int       g_worker_id = -1;

/* 全局就绪队列（溢出或跨线程任务） */
static zan_mutex_t         g_global_lock;
static zan_co_t           *g_ready_head;
static zan_co_t           *g_ready_tail;

/* 定时器与全部任务追踪锁 */
static zan_mutex_t         g_timer_lock;
static zan_timer_t        *g_timers;
static size_t              g_timer_n;
static size_t              g_timer_cap;

static zan_mutex_t         g_all_tasks_lock;
static zan_task_t         *g_all_tasks;


#ifdef _WIN32
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

#else
static ZAN_THREAD_LOCAL ucontext_t g_sched_ctx;
#if UINTPTR_MAX > 0xFFFFFFFFu
static void co_trampoline_posix(unsigned hi, unsigned lo);
#else
static void co_trampoline_posix(unsigned ptr);
#endif

typedef struct { ucontext_t ctx; char *stack; } posix_fiber_t;

#define ZAN_CO_STACK_POOL_MAX 256

static zan_mutex_t g_stack_pool_lock;
static volatile int g_stack_pool_lock_inited = 0;
static void *g_stack_pool;
static int g_stack_pool_n;

static void ensure_stack_pool_lock(void) {
    if (!g_stack_pool_lock_inited) {
        zan_mutex_init(&g_stack_pool_lock);
        g_stack_pool_lock_inited = 1;
    }
}

static char *plat_stack_alloc(void) {
    ensure_stack_pool_lock();
    zan_mutex_lock(&g_stack_pool_lock);
    if (g_stack_pool) {
        char *s = (char *)g_stack_pool;
        g_stack_pool = *(void **)s;
        g_stack_pool_n--;
        zan_mutex_unlock(&g_stack_pool_lock);
        return s;
    }
    zan_mutex_unlock(&g_stack_pool_lock);
    long page = sysconf(_SC_PAGESIZE);
    size_t total = co_stack_size() + (size_t)page;
    char *base = (char *)mmap(NULL, total, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return NULL;
    if (mprotect(base, (size_t)page, PROT_NONE) != 0) {
        munmap(base, total);
        return NULL;
    }
    return base + page;
}

static void plat_stack_free(char *stack) {
    ensure_stack_pool_lock();
    zan_mutex_lock(&g_stack_pool_lock);
    if (g_stack_pool_n < ZAN_CO_STACK_POOL_MAX) {
        *(void **)stack = g_stack_pool;
        g_stack_pool = stack;
        g_stack_pool_n++;
        zan_mutex_unlock(&g_stack_pool_lock);
        return;
    }
    zan_mutex_unlock(&g_stack_pool_lock);
    long page = sysconf(_SC_PAGESIZE);
    munmap((char *)stack - page, co_stack_size() + (size_t)page);
}

static void plat_sched_enter(void) { g_sched_fiber = &g_sched_ctx; }
static void plat_sched_leave(void) {}
static void *plat_fiber_new(zan_co_t *co) {
    posix_fiber_t *pf = (posix_fiber_t *)calloc(1, sizeof(*pf));
    if (!pf) zan_rt_fatal("oom", "scheduler: fiber control block alloc failed");
    pf->stack = plat_stack_alloc();
    if (!pf->stack) zan_rt_fatal("oom", "scheduler: fiber stack alloc failed");
    getcontext(&pf->ctx);
    pf->ctx.uc_stack.ss_sp = pf->stack;
    pf->ctx.uc_stack.ss_size = co_stack_size();
    pf->ctx.uc_link = &g_sched_ctx;
#if UINTPTR_MAX > 0xFFFFFFFFu
    uintptr_t p = (uintptr_t)co;
    makecontext(&pf->ctx, (void (*)(void))co_trampoline_posix, 2,
                (unsigned)(p >> 32), (unsigned)(p & 0xffffffffu));
#else
    makecontext(&pf->ctx, (void (*)(void))co_trampoline_posix, 1,
                (unsigned)(uintptr_t)co);
#endif
    return pf;
}
static void plat_fiber_delete(void *f) {
    posix_fiber_t *pf = (posix_fiber_t *)f;
    plat_stack_free(pf->stack);
    free(pf);
}
static void plat_switch(void *to) {
#ifndef _WIN32
    ((posix_fiber_t *)to)->ctx.uc_link = &g_sched_ctx;
#endif
    swapcontext(&g_sched_ctx, &((posix_fiber_t *)to)->ctx);
}
static void plat_sleep(int64_t ms) {
    if (ms < 0) ms = 0;
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
static int64_t plat_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
#endif

static void switch_to_sched(void) {
#ifdef _WIN32
    plat_switch(g_sched_fiber);
#else
    swapcontext(&((posix_fiber_t *)g_current->fiber)->ctx, &g_sched_ctx);
#endif
}


static void wake_one_worker(void) {
    if (g_worker_count <= 1) return;
    int start = (g_worker_id >= 0) ? (g_worker_id + 1) % g_worker_count : 0;
    for (int k = 0; k < g_worker_count; k++) {
        int i = (start + k) % g_worker_count;
        if (g_workers[i].active && g_workers[i].sleeping) {
#ifdef _WIN32
            SetEvent(g_workers[i].wake_event);
#else
            pthread_mutex_lock(&g_workers[i].wake_mutex);
            pthread_cond_signal(&g_workers[i].wake_cond);
            pthread_mutex_unlock(&g_workers[i].wake_mutex);
#endif
            break;
        }
    }
}

static void wake_all_workers(void) {
    for (int i = 0; i < g_worker_count; i++) {
        if (g_workers[i].active) {
#ifdef _WIN32
            SetEvent(g_workers[i].wake_event);
#else
            pthread_mutex_lock(&g_workers[i].wake_mutex);
            pthread_cond_broadcast(&g_workers[i].wake_cond);
            pthread_mutex_unlock(&g_workers[i].wake_mutex);
#endif
        }
    }
}

static void ready_push(zan_co_t *co) {
    co->next = NULL;
    int wid = g_worker_id;
    if (wid >= 0 && wid < g_worker_count) {
        zan_worker_t *w = &g_workers[wid];
        zan_mutex_lock(&w->lock);
        if (w->local_count < ZAN_LOCAL_QUEUE_CAP) {
            if (w->local_tail) w->local_tail->next = co;
            else               w->local_head = co;
            w->local_tail = co;
            w->local_count++;
            zan_mutex_unlock(&w->lock);
            wake_one_worker();
            return;
        }
        zan_mutex_unlock(&w->lock);
    }

    zan_mutex_lock(&g_global_lock);
    if (g_ready_tail) g_ready_tail->next = co;
    else              g_ready_head = co;
    g_ready_tail = co;
    zan_mutex_unlock(&g_global_lock);
    wake_one_worker();
}

static zan_co_t *ready_pop_worker(int wid) {
    zan_worker_t *w = (wid >= 0 && wid < g_worker_count) ? &g_workers[wid] : NULL;

    // 1. 本地私有就绪队列优先弹出（无跨核锁争用）
    if (w) {
        zan_mutex_lock(&w->lock);
        zan_co_t *co = w->local_head;
        if (co) {
            w->local_head = co->next;
            if (!w->local_head) w->local_tail = NULL;
            w->local_count--;
            co->next = NULL;
            zan_mutex_unlock(&w->lock);
            return co;
        }
        zan_mutex_unlock(&w->lock);
    }

    // 2. 全局就绪队列弹出
    zan_mutex_lock(&g_global_lock);
    zan_co_t *co = g_ready_head;
    if (co) {
        g_ready_head = co->next;
        if (!g_ready_head) g_ready_tail = NULL;
        co->next = NULL;
        zan_mutex_unlock(&g_global_lock);
        return co;
    }
    zan_mutex_unlock(&g_global_lock);

    // 3. 工作窃取：从其他 Worker 本地队列窃取任务
    if (w && g_worker_count > 1) {
        for (int k = 1; k < g_worker_count; k++) {
            int victim_id = (wid + k) % g_worker_count;
            zan_worker_t *victim = &g_workers[victim_id];
            if (victim->local_count > 0) {
                zan_mutex_lock(&victim->lock);
                if (victim->local_count > 0 && victim->local_head) {
                    co = victim->local_head;
                    victim->local_head = co->next;
                    if (!victim->local_head) victim->local_tail = NULL;
                    victim->local_count--;
                    co->next = NULL;
                    zan_mutex_unlock(&victim->lock);
                    return co;
                }
                zan_mutex_unlock(&victim->lock);
            }
        }
    }

    return NULL;
}

static void worker_wait(zan_worker_t *w, int timeout_ms) {
    if (!w) {
        plat_sleep(timeout_ms > 0 ? timeout_ms : 1);
        return;
    }
    w->sleeping = 1;
#ifdef _WIN32
    WaitForSingleObject(w->wake_event, (DWORD)(timeout_ms > 0 ? timeout_ms : 1));
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long nsec = ts.tv_nsec + (long)(timeout_ms > 0 ? timeout_ms : 1) * 1000000L;
    ts.tv_sec += nsec / 1000000000L;
    ts.tv_nsec = nsec % 1000000000L;
    pthread_mutex_lock(&w->wake_mutex);
    pthread_cond_timedwait(&w->wake_cond, &w->wake_mutex, &ts);
    pthread_mutex_unlock(&w->wake_mutex);
#endif
    w->sleeping = 0;
}

/* ================= tasks ================= */

static zan_task_t *task_new(zan_co_t *owner) {
    zan_task_t *t = (zan_task_t *)calloc(1, sizeof(*t));
    if (!t) zan_rt_fatal("oom", "scheduler: task alloc failed");
    zan_mutex_init(&t->lock);
    t->co = owner;
    zan_mutex_lock(&g_all_tasks_lock);
    t->all_next = g_all_tasks;
    g_all_tasks = t;
    zan_mutex_unlock(&g_all_tasks_lock);
    return t;
}

static void complete_task(zan_task_t *t, int64_t result) {
    zan_mutex_lock(&t->lock);
    if (t->completed) {
        zan_mutex_unlock(&t->lock);
        return;
    }
    t->result = result;
    t->completed = 1;
    zan_co_t *w = t->waiters;
    t->waiters = NULL;
    zan_mutex_unlock(&t->lock);

    zan_co_t *rev = NULL;
    while (w) {
        zan_co_t *n = w->next;
        w->next = rev;
        rev = w;
        w = n;
    }
    while (rev) {
        zan_co_t *n = rev->next;
        rev->next = NULL;
        ready_push(rev);
        rev = n;
    }
}

void zan_task_release(zan_task_t *task) {
    if (!task) return;
    zan_mutex_lock(&g_all_tasks_lock);
    zan_task_t **pp = &g_all_tasks;
    while (*pp && *pp != task) pp = &(*pp)->all_next;
    if (!*pp) {
        zan_mutex_unlock(&g_all_tasks_lock);
        return;
    }
    if (!task->completed) {
        zan_mutex_unlock(&g_all_tasks_lock);
        zan_rt_fatal("sched", "task released before completion");
    }
    *pp = task->all_next;
    zan_mutex_unlock(&g_all_tasks_lock);
    zan_mutex_destroy(&task->lock);
    free(task);
}

size_t zan_task_live(void) {
    size_t n = 0;
    zan_mutex_lock(&g_all_tasks_lock);
    for (zan_task_t *t = g_all_tasks; t; t = t->all_next) n++;
    zan_mutex_unlock(&g_all_tasks_lock);
    return n;
}

/* ================= timers ================= */

static void timer_add(zan_task_t *t, int64_t delay_ms) {
    zan_mutex_lock(&g_timer_lock);
    if (g_timer_n == g_timer_cap) {
        size_t nc = g_timer_cap ? g_timer_cap * 2 : 64;
        zan_timer_t *nt = (zan_timer_t *)realloc(g_timers, nc * sizeof(*nt));
        if (!nt) {
            zan_mutex_unlock(&g_timer_lock);
            zan_rt_fatal("oom", "scheduler: timer table grow failed");
        }
        g_timers = nt;
        g_timer_cap = nc;
    }
    size_t i = g_timer_n++;
    int64_t due;
    if (delay_ms < 0) {
        due = plat_now_ms();
    } else if (delay_ms > INT64_MAX - plat_now_ms()) {
        due = INT64_MAX;
    } else {
        due = plat_now_ms() + delay_ms;
    }
    g_timers[i].due_ms = due;
    g_timers[i].task = t;
    while (i > 0) {
        size_t p = (i - 1) / 2;
        if (g_timers[p].due_ms <= g_timers[i].due_ms) break;
        zan_timer_t tmp = g_timers[p];
        g_timers[p] = g_timers[i];
        g_timers[i] = tmp;
        i = p;
    }
    zan_mutex_unlock(&g_timer_lock);
}

static void timer_pop_root(void) {
    g_timers[0] = g_timers[--g_timer_n];
    size_t i = 0;
    for (;;) {
        size_t l = 2 * i + 1, r = l + 1, m = i;
        if (l < g_timer_n && g_timers[l].due_ms < g_timers[m].due_ms) m = l;
        if (r < g_timer_n && g_timers[r].due_ms < g_timers[m].due_ms) m = r;
        if (m == i) break;
        zan_timer_t tmp = g_timers[m];
        g_timers[m] = g_timers[i];
        g_timers[i] = tmp;
        i = m;
    }
}

static int64_t timers_process(void) {
    zan_mutex_lock(&g_timer_lock);
    int64_t now = plat_now_ms();
    while (g_timer_n > 0 && g_timers[0].due_ms <= now) {
        zan_task_t *task = g_timers[0].task;
        timer_pop_root();
        zan_mutex_unlock(&g_timer_lock);
        complete_task(task, 0);
        zan_mutex_lock(&g_timer_lock);
    }
    int64_t res = g_timer_n > 0 ? g_timers[0].due_ms - now : -1;
    zan_mutex_unlock(&g_timer_lock);
    return res;
}


#ifdef _WIN32
static void WINAPI co_trampoline(void *p) {
    zan_co_t *co = (zan_co_t *)p;
    co->body(co->task);
    co->finished = 1;
    complete_task(co->task, 0);
    switch_to_sched();
}

static DWORD WINAPI worker_thread_main(LPVOID arg) {
    zan_worker_t *w = (zan_worker_t *)arg;
    g_worker_id = w->id;
    plat_sched_enter();

    while (!g_sched_stop) {
        zan_co_t *co = ready_pop_worker(w->id);
        if (co) {
            co_acquire(co);
            g_current = co;
            plat_switch(co->fiber);
            g_current = NULL;
            if (co->finished) {
                plat_fiber_delete(co->fiber);
                free(co);
                zan_atomic_dec(&g_live);
            } else {
                co_release(co);
            }
        } else {
            worker_wait(w, 2);
        }
    }

    plat_sched_leave();
    return 0;
}
#else
#if UINTPTR_MAX > 0xFFFFFFFFu
static void co_trampoline_posix(unsigned hi, unsigned lo) {
    zan_co_t *co = (zan_co_t *)(((uintptr_t)hi << 32) | (uintptr_t)lo);
#else
static void co_trampoline_posix(unsigned ptr) {
    zan_co_t *co = (zan_co_t *)(uintptr_t)ptr;
#endif
    co->body(co->task);
    co->finished = 1;
    complete_task(co->task, 0);
    switch_to_sched();
}

static void *worker_thread_main(void *arg) {
    zan_worker_t *w = (zan_worker_t *)arg;
    g_worker_id = w->id;
    plat_sched_enter();

    while (!g_sched_stop) {
        zan_co_t *co = ready_pop_worker(w->id);
        if (co) {
            co_acquire(co);
            g_current = co;
            plat_switch(co->fiber);
            g_current = NULL;
            if (co->finished) {
                plat_fiber_delete(co->fiber);
                free(co);
                zan_atomic_dec(&g_live);
            } else {
                co_release(co);
            }
        } else {
            worker_wait(w, 2);
        }
    }

    plat_sched_leave();
    return NULL;
}
#endif


zan_task_t *zan_spawn(zan_co_body_t body, void *arg) {
    zan_co_t *co = (zan_co_t *)calloc(1, sizeof(*co));
    if (!co) zan_rt_fatal("oom", "scheduler: coroutine alloc failed");
    co->body = body;
    co->task = task_new(co);
    co->task->arg = arg;
    co->fiber = plat_fiber_new(co);
    if (!co->fiber) zan_rt_fatal("oom", "scheduler: fiber alloc failed");
    zan_atomic_inc(&g_live);
    ready_push(co);
    return co->task;
}

void *zan_task_arg(zan_task_t *task) { return task ? task->arg : NULL; }

void zan_task_return(zan_task_t *task, int64_t result) {
    if (g_current) g_current->finished = 1;
    complete_task(task, result);
    switch_to_sched();
}

int64_t zan_task_await(zan_task_t *task) {
    if (!task) return 0;
    zan_mutex_lock(&task->lock);
    if (!task->completed) {
        g_current->next = task->waiters;
        task->waiters = g_current;
        zan_mutex_unlock(&task->lock);
        switch_to_sched();
        return task->result;
    }
    int64_t res = task->result;
    zan_mutex_unlock(&task->lock);
    return res;
}

zan_task_t *zan_task_delay(int64_t ms) {
    zan_task_t *t = task_new(NULL);
    timer_add(t, ms);
    return t;
}

void zan_task_yield(void) {
    if (g_current) {
        ready_push(g_current);
        switch_to_sched();
    }
}

int64_t zan_task_result(zan_task_t *task) { return task ? task->result : 0; }

/* ================= IO integration ================= */

void zan_io_suspend_current(void) {
    if (g_current) {
        switch_to_sched();
    }
}

void zan_io_resume(void *co) {
    if (co) {
        ready_push((zan_co_t *)co);
    }
}

void *zan_io_get_current_co(void) {
    return (void *)g_current;
}

/* ================= scheduler ================= */

void zan_sched_init(void) {
    zan_mutex_init(&g_global_lock);
    zan_mutex_init(&g_timer_lock);
    zan_mutex_init(&g_all_tasks_lock);
    g_sched_fiber = NULL;
    g_current = NULL;
    g_ready_head = g_ready_tail = NULL;
    g_timers = NULL;
    g_timer_n = 0;
    g_timer_cap = 0;
    g_all_tasks = NULL;
    g_live = 0;
    g_sched_stop = 0;

    for (int i = 0; i < ZAN_MAX_WORKERS; i++) {
        g_workers[i].id = i;
        g_workers[i].active = 0;
        g_workers[i].sleeping = 0;
        zan_mutex_init(&g_workers[i].lock);
        g_workers[i].local_head = NULL;
        g_workers[i].local_tail = NULL;
        g_workers[i].local_count = 0;
#ifdef _WIN32
        g_workers[i].wake_event = CreateEvent(NULL, FALSE, FALSE, NULL);
        g_workers[i].thread = NULL;
#else
        pthread_cond_init(&g_workers[i].wake_cond, NULL);
        pthread_mutex_init(&g_workers[i].wake_mutex, NULL);
#endif
    }

    g_worker_count = 1;
    int cfg_w = zan_async_cfg_workers();
    if (cfg_w > 1) {
        if (cfg_w > ZAN_MAX_WORKERS) cfg_w = ZAN_MAX_WORKERS;
        g_worker_count = cfg_w;
    }
    const char *env = getenv("ZAN_SCHED_WORKERS");
    if (env && env[0]) {
        int w = atoi(env);
        if (w > 1) {
            if (w > ZAN_MAX_WORKERS) w = ZAN_MAX_WORKERS;
            g_worker_count = w;
        }
    }
    g_workers[0].active = 1;
    g_worker_id = 0;

    plat_sched_enter();
    zan_io_init();
}

void zan_sched_run(void) {
    g_sched_stop = 0;

    int cfg_w = zan_async_cfg_workers();
    if (cfg_w > 1) {
        if (cfg_w > ZAN_MAX_WORKERS) cfg_w = ZAN_MAX_WORKERS;
        g_worker_count = cfg_w;
    }

    /* 启动多 Worker 辅助线程 */
    for (int i = 1; i < g_worker_count; i++) {
        g_workers[i].id = i;
        g_workers[i].active = 1;
#ifdef _WIN32
        g_workers[i].thread = CreateThread(NULL, 0, worker_thread_main, &g_workers[i], 0, NULL);
#else
        pthread_create(&g_workers[i].thread, NULL, worker_thread_main, &g_workers[i]);
#endif
    }

    /* Worker 0 调度主循环 */
    while (zan_atomic_read(&g_live) > 0) {
        int64_t next_timer = timers_process();

        zan_co_t *co = ready_pop_worker(0);
        if (!co) {
            int64_t poll_timeout = -1;
            if (next_timer >= 0) poll_timeout = next_timer;
            if (zan_io_has_pending()) {
                zan_io_poll(poll_timeout);
                co = ready_pop_worker(0);
            } else if (next_timer >= 0) {
                int64_t slp = next_timer > 5 ? 5 : next_timer;
                worker_wait(&g_workers[0], (int)slp);
                continue;
            } else {
                if (zan_atomic_read(&g_live) > 0) {
                    worker_wait(&g_workers[0], 2);
                    continue;
                }
                break;
            }
        } else {
            if (zan_io_has_pending()) {
                zan_io_poll(0);
            }
        }

        if (co) {
            co_acquire(co);
            g_current = co;
            plat_switch(co->fiber);
            g_current = NULL;
            if (co->finished) {
                plat_fiber_delete(co->fiber);
                free(co);
                zan_atomic_dec(&g_live);
            } else {
                co_release(co);
            }
        }
    }

    /* 通知并等待所有辅助 Worker 退出 */
    g_sched_stop = 1;
    wake_all_workers();
    for (int i = 1; i < g_worker_count; i++) {
        if (g_workers[i].active) {
#ifdef _WIN32
            WaitForSingleObject(g_workers[i].thread, INFINITE);
            CloseHandle(g_workers[i].thread);
#else
            pthread_join(g_workers[i].thread, NULL);
#endif
            g_workers[i].active = 0;
        }
    }
}

void zan_sched_shutdown(void) {
    zan_io_shutdown();
    zan_mutex_lock(&g_all_tasks_lock);
    zan_task_t *t = g_all_tasks;
    while (t) {
        zan_task_t *n = t->all_next;
        zan_mutex_destroy(&t->lock);
        free(t);
        t = n;
    }
    g_all_tasks = NULL;
    zan_mutex_unlock(&g_all_tasks_lock);

    zan_mutex_lock(&g_timer_lock);
    free(g_timers);
    g_timers = NULL;
    g_timer_n = g_timer_cap = 0;
    zan_mutex_unlock(&g_timer_lock);

    for (int i = 0; i < ZAN_MAX_WORKERS; i++) {
        zan_mutex_destroy(&g_workers[i].lock);
#ifdef _WIN32
        if (g_workers[i].wake_event) {
            CloseHandle(g_workers[i].wake_event);
            g_workers[i].wake_event = NULL;
        }
#else
        pthread_cond_destroy(&g_workers[i].wake_cond);
        pthread_mutex_destroy(&g_workers[i].wake_mutex);
#endif
    }

    zan_mutex_destroy(&g_global_lock);
    zan_mutex_destroy(&g_timer_lock);
    zan_mutex_destroy(&g_all_tasks_lock);
    plat_sched_leave();
}
