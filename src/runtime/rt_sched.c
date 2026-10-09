/* 内部辅助实现 */

/* 内部辅助实现 */
#if defined(__APPLE__) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif

#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601   /* 核心系统底层抽象与内存语义契约 */
#endif

#include "rt_sched.h"
#include "rt_io.h"
#include "rt_timer.h"       /* 核心系统底层抽象与内存语义契约 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>      /* 底层系统交互与数据协议契约 */
#include <ucontext.h>
#include <time.h>
#include <unistd.h>
#endif
#include "../common/host_oom.h"

/* 核心系统底层抽象与内存语义契约 */
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

/* 核心系统底层抽象与内存语义契约 */

typedef struct zan_co {
    void          *fiber;   /* 核心系统底层抽象与内存语义契约 */
    zan_co_body_t  body;
    zan_task_t    *task;
    int            finished;
    struct zan_co *next;    /* 核心系统底层抽象与内存语义契约 */
} zan_co_t;

struct zan_task {
    int         completed;
    int64_t     result;
    void       *arg;
    zan_co_t   *co;
    zan_co_t   *waiters;   /* 底层系统交互与数据协议契约 */
    zan_task_t *all_next;
};

/* ---- timers ---- */

/* 内部辅助逻辑 */

typedef struct zan_timer {
    int64_t     due_ms;
    zan_task_t *task;
} zan_timer_t;

/* 核心系统底层抽象与内存语义契约 */

static void        *g_sched_fiber;
static zan_co_t    *g_current;
static zan_co_t    *g_ready_head;
static zan_co_t    *g_ready_tail;
static zan_timer_t *g_timers;      /* 底层系统交互与数据协议契约 */
static size_t       g_timer_n;
static size_t       g_timer_cap;
static zan_task_t  *g_all_tasks;
static int          g_live;

/* 核心系统底层抽象与内存语义契约 */

#ifdef _WIN32
static void WINAPI co_trampoline(void *p);

static void plat_sched_enter(void) { g_sched_fiber = ConvertThreadToFiber(NULL); }
static void plat_sched_leave(void) { ConvertFiberToThread(); }
static void *plat_fiber_new(zan_co_t *co) {
    return CreateFiber((SIZE_T)co_stack_size(), co_trampoline, co);
}
static void plat_fiber_delete(void *f) { DeleteFiber(f); }
static void plat_switch(void *to)      { SwitchToFiber(to); }
/* 内部辅助实现 */
#define ZAN_SLEEP_MS_MAX ((int64_t)0x7FFFFFFF)
static void plat_sleep(int64_t ms) {
    if (ms < 0) ms = 0;
    if (ms > ZAN_SLEEP_MS_MAX) ms = ZAN_SLEEP_MS_MAX;
    Sleep((DWORD)ms);
}
static int64_t plat_now_ms(void)       { return (int64_t)GetTickCount64(); }

#else
/* 核心系统底层抽象与内存语义契约 */
static ucontext_t g_sched_ctx;
#if UINTPTR_MAX > 0xFFFFFFFFu
static void co_trampoline_posix(unsigned hi, unsigned lo);
#else
static void co_trampoline_posix(unsigned ptr);
#endif

typedef struct { ucontext_t ctx; char *stack; } posix_fiber_t;

/* 内部辅助实现 */
#define ZAN_CO_STACK_POOL_MAX 256

static void *g_stack_pool;
static int g_stack_pool_n;

static char *plat_stack_alloc(void) {
    if (g_stack_pool) {               /* 核心系统底层抽象与内存语义契约 */
        char *s = (char *)g_stack_pool;
        g_stack_pool = *(void **)s;
        g_stack_pool_n--;
        return s;
    }
    long page = sysconf(_SC_PAGESIZE);
    size_t total = co_stack_size() + (size_t)page;
    char *base = (char *)mmap(NULL, total, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return NULL;
    if (mprotect(base, (size_t)page, PROT_NONE) != 0) {
        munmap(base, total);
        return NULL;
    }
    return base + page;               /* 底层系统交互与数据协议契约 */
}

static void plat_stack_free(char *stack) {
    if (g_stack_pool_n < ZAN_CO_STACK_POOL_MAX) {
        *(void **)stack = g_stack_pool;
        g_stack_pool = stack;
        g_stack_pool_n++;
        return;
    }
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
    /* 模块核心语义抽象与接口调用契约 */
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
    /* 内部辅助实现 */
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

/* 底层系统交互与数据协议契约 */
static void switch_to_sched(void) {
#ifdef _WIN32
    plat_switch(g_sched_fiber);
#else
    swapcontext(&((posix_fiber_t *)g_current->fiber)->ctx, &g_sched_ctx);
#endif
}

/* 核心系统底层抽象与内存语义契约 */

static void ready_push(zan_co_t *co) {
    co->next = NULL;
    if (g_ready_tail) g_ready_tail->next = co;
    else              g_ready_head = co;
    g_ready_tail = co;
}

static zan_co_t *ready_pop(void) {
    zan_co_t *co = g_ready_head;
    if (co) {
        g_ready_head = co->next;
        if (!g_ready_head) g_ready_tail = NULL;
        co->next = NULL;
    }
    return co;
}

/* ================= tasks ================= */

static zan_task_t *task_new(zan_co_t *owner) {
    zan_task_t *t = (zan_task_t *)calloc(1, sizeof(*t));
    if (!t) zan_rt_fatal("oom", "scheduler: task alloc failed");
    t->co = owner;
    t->all_next = g_all_tasks;
    g_all_tasks = t;
    return t;
}

static void complete_task(zan_task_t *t, int64_t result) {
    if (t->completed) return;
    t->result = result;
    t->completed = 1;
    /* 内部辅助实现 */
    zan_co_t *w = t->waiters;
    t->waiters = NULL;
    zan_co_t *rev = NULL;
    while (w) {
        zan_co_t *n = w->next;
        w->next = rev;
        rev = w;
        w = n;
    }
    while (rev) {
        zan_co_t *n = rev->next;
        rev->next = NULL;   /* 核心系统底层抽象与内存语义契约 */
        ready_push(rev);
        rev = n;
    }
}

/* 模块核心语义抽象与接口调用契约 */
void zan_task_release(zan_task_t *task) {
    if (!task) return;
    zan_task_t **pp = &g_all_tasks;
    while (*pp && *pp != task) pp = &(*pp)->all_next;
    if (!*pp) return;   /* 核心系统底层抽象与内存语义契约 */
    if (!task->completed) {
        /* 内部辅助实现 */
        zan_rt_fatal("sched", "task released before completion");
    }
    *pp = task->all_next;
    free(task);
}

/* 模块核心语义抽象与接口调用契约 */
size_t zan_task_live(void) {
    size_t n = 0;
    for (zan_task_t *t = g_all_tasks; t; t = t->all_next) n++;
    return n;
}

/* ================= timers ================= */

static void timer_add(zan_task_t *t, int64_t delay_ms) {
    if (g_timer_n == g_timer_cap) {
        size_t nc = g_timer_cap ? g_timer_cap * 2 : 64;
        zan_timer_t *nt = (zan_timer_t *)realloc(g_timers, nc * sizeof(*nt));
        if (!nt) zan_rt_fatal("oom", "scheduler: timer table grow failed");
        g_timers = nt;
        g_timer_cap = nc;
    }
    /* 模块核心语义抽象与接口调用契约 */
    size_t i = g_timer_n++;
    /* 内部辅助实现 */
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
}

static void timer_pop_root(void) {
    /* 模块核心语义抽象与接口调用契约 */
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
    int64_t now = plat_now_ms();
    /* 核心系统底层抽象与内存语义契约 */
    while (g_timer_n > 0 && g_timers[0].due_ms <= now) {
        zan_task_t *task = g_timers[0].task;
        timer_pop_root();
        complete_task(task, 0);
    }
    /* 内部辅助实现 */
    return g_timer_n > 0 ? g_timers[0].due_ms - now : -1;
}

/* 核心系统底层抽象与内存语义契约 */

#ifdef _WIN32
static void WINAPI co_trampoline(void *p) {
    zan_co_t *co = (zan_co_t *)p;
    co->body(co->task);
    co->finished = 1;
    complete_task(co->task, 0);
    switch_to_sched();
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
}
#endif

/* 核心系统底层抽象与内存语义契约 */

zan_task_t *zan_spawn(zan_co_body_t body, void *arg) {
    zan_co_t *co = (zan_co_t *)calloc(1, sizeof(*co));
    if (!co) zan_rt_fatal("oom", "scheduler: coroutine alloc failed");
    co->body = body;
    co->task = task_new(co);
    co->task->arg = arg;
    co->fiber = plat_fiber_new(co);
    if (!co->fiber) zan_rt_fatal("oom", "scheduler: fiber alloc failed");
    g_live++;
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
    if (!task->completed) {
        /* 核心系统底层抽象与内存语义契约 */
        g_current->next = task->waiters;
        task->waiters = g_current;
        switch_to_sched();
    }
    return task->result;
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
    /* 模块核心语义抽象与接口调用契约 */
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
    g_sched_fiber = NULL;
    g_current = NULL;
    g_ready_head = g_ready_tail = NULL;
    g_timers = NULL;
    g_timer_n = 0;
    g_timer_cap = 0;
    g_all_tasks = NULL;
    g_live = 0;
    plat_sched_enter();
    zan_io_init();
}

void zan_sched_run(void) {
    while (g_live > 0) {
        /* 核心系统底层抽象与内存语义契约 */
        int64_t next_timer = timers_process();

        /* 底层系统交互与数据协议契约 */
        zan_co_t *co = ready_pop();
        if (!co) {
            /* 内部辅助逻辑 */
            int64_t poll_timeout = -1;
            if (next_timer >= 0) poll_timeout = next_timer;
            if (zan_io_has_pending()) {
                zan_io_poll(poll_timeout);
                co = ready_pop();
            } else if (next_timer >= 0) {
                plat_sleep(next_timer);
                continue;
            } else {
                /* 内部辅助实现 */
                if (g_live > 0) {
                    fprintf(stderr,
                            "zan runtime: %d coroutine(s) parked with no "
                            "wakeup source (no ready work, no timers, no IO "
                            "pending) -- scheduler stopping\n",
                            g_live);
                }
                break;
            }
        } else {
            /* 底层系统交互与数据协议契约 */
            if (zan_io_has_pending()) {
                zan_io_poll(0);
            }
        }

        if (co) {
            g_current = co;
            plat_switch(co->fiber);
            g_current = NULL;
            if (co->finished) {
                plat_fiber_delete(co->fiber);
                free(co);
                g_live--;
            }
        }
    }
}

void zan_sched_shutdown(void) {
    zan_io_shutdown();
    zan_task_t *t = g_all_tasks;
    while (t) { zan_task_t *n = t->all_next; free(t); t = n; }
    g_all_tasks = NULL;
    /* 模块核心语义抽象与接口调用契约 */
    free(g_timers);
    g_timers = NULL;
    g_timer_n = g_timer_cap = 0;
    plat_sched_leave();
}
