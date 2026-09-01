/* rt_sched.c -- Zan M1 coroutine runtime (cooperative, stackful fibers).
 *
 * Single OS thread for M1 (M:1). See rt_sched.h for the compiler-facing ABI.
 * Now integrates with rt_io for async IO multiplexing.
 */

/* macOS gates the ucontext routines behind _XOPEN_SOURCE; it must be defined
 * before any system header is pulled in (directly or transitively). */
#if defined(__APPLE__) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif

#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601   /* Windows 7+: GetTickCount64, modern Fibers API */
#endif

#include "rt_sched.h"
#include "rt_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>      /* coroutine stack pool: mmap + PROT_NONE guard */
#include <ucontext.h>
#include <time.h>
#include <unistd.h>
#endif
#include "../common/host_oom.h"

/* Per-coroutine stack size. */
#define ZAN_CO_STACK (128 * 1024)

/* ---- coroutine + task objects ---- */

typedef struct zan_co {
    void          *fiber;   /* platform fiber handle */
    zan_co_body_t  body;
    zan_task_t    *task;
    int            finished;
    struct zan_co *next;    /* ready-queue / free link */
} zan_co_t;

struct zan_task {
    int         completed;
    int64_t     result;
    void       *arg;
    zan_co_t   *co;
    zan_co_t   *waiters;   /* LIFO chain of parked awaiters, linked via co->next */
    zan_task_t *all_next;
};

/* ---- timers ---- */

typedef struct zan_timer {
    int64_t           due_ms;
    zan_task_t       *task;
    struct zan_timer *next;
} zan_timer_t;

/* ---- global scheduler state ---- */

static void        *g_sched_fiber;
static zan_co_t    *g_current;
static zan_co_t    *g_ready_head;
static zan_co_t    *g_ready_tail;
static zan_timer_t *g_timers;
static zan_task_t  *g_all_tasks;
static int          g_live;

/* ================= platform fiber layer ================= */

#ifdef _WIN32
static void WINAPI co_trampoline(void *p);

static void plat_sched_enter(void) { g_sched_fiber = ConvertThreadToFiber(NULL); }
static void plat_sched_leave(void) { ConvertFiberToThread(); }
static void *plat_fiber_new(zan_co_t *co) {
    return CreateFiber(ZAN_CO_STACK, co_trampoline, co);
}
static void plat_fiber_delete(void *f) { DeleteFiber(f); }
static void plat_switch(void *to)      { SwitchToFiber(to); }
static void plat_sleep(int64_t ms)     { Sleep((DWORD)(ms < 0 ? 0 : ms)); }
static int64_t plat_now_ms(void)       { return (int64_t)GetTickCount64(); }

#else
/* POSIX ucontext backend */
static ucontext_t g_sched_ctx;
static void co_trampoline_posix(unsigned hi, unsigned lo);

typedef struct { ucontext_t ctx; char *stack; } posix_fiber_t;

/* Coroutine stacks come from a pool instead of a malloc/free per spawn: a
 * server that spawns thousands of short coroutines otherwise pays a 128 KB
 * allocation per coroutine. Each stack has a PROT_NONE guard page at its low
 * end, so an overflow faults immediately instead of corrupting adjacent
 * memory. The scheduler is single-threaded (M:1), so the pool needs no lock. */
#define ZAN_CO_STACK_POOL_MAX 64

static void *g_stack_pool;
static int g_stack_pool_n;

static char *plat_stack_alloc(void) {
    if (g_stack_pool) {               /* reuse a retired stack */
        char *s = (char *)g_stack_pool;
        g_stack_pool = *(void **)s;
        g_stack_pool_n--;
        return s;
    }
    long page = sysconf(_SC_PAGESIZE);
    size_t total = ZAN_CO_STACK + (size_t)page;
    char *base = (char *)mmap(NULL, total, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return NULL;
    if (mprotect(base, (size_t)page, PROT_NONE) != 0) {
        munmap(base, total);
        return NULL;
    }
    return base + page;               /* usable region starts after the guard */
}

static void plat_stack_free(char *stack) {
    if (g_stack_pool_n < ZAN_CO_STACK_POOL_MAX) {
        *(void **)stack = g_stack_pool;
        g_stack_pool = stack;
        g_stack_pool_n++;
        return;
    }
    long page = sysconf(_SC_PAGESIZE);
    munmap((char *)stack - page, ZAN_CO_STACK + (size_t)page);
}

static void plat_sched_enter(void) { g_sched_fiber = &g_sched_ctx; }
static void plat_sched_leave(void) {}
static void *plat_fiber_new(zan_co_t *co) {
    posix_fiber_t *pf = (posix_fiber_t *)calloc(1, sizeof(*pf));
    if (!pf) abort();                 /* OOM: fail fast, like the rest of rt_* */
    pf->stack = plat_stack_alloc();
    if (!pf->stack) abort();          /* OOM: fail fast, like the rest of rt_* */
    getcontext(&pf->ctx);
    pf->ctx.uc_stack.ss_sp = pf->stack;
    pf->ctx.uc_stack.ss_size = ZAN_CO_STACK;
    pf->ctx.uc_link = &g_sched_ctx;
    /* makecontext passes int-sized args; split the co pointer across two.
     * The trampoline reconstructs it, so each fiber runs its own body. */
    uintptr_t p = (uintptr_t)co;
    makecontext(&pf->ctx, (void (*)(void))co_trampoline_posix, 2,
                (unsigned)(p >> 32), (unsigned)(p & 0xffffffffu));
    return pf;
}
static void plat_fiber_delete(void *f) {
    posix_fiber_t *pf = (posix_fiber_t *)f;
    plat_stack_free(pf->stack);
    free(pf);
}
static void plat_switch(void *to) {
    /* Only ever called by the scheduler to enter a coroutine, so the state
     * we save is the scheduler's; the coroutine returns here via
     * switch_to_sched (swap co <-> g_sched_ctx). */
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

/* switch back to the scheduler from a coroutine */
static void switch_to_sched(void) {
#ifdef _WIN32
    plat_switch(g_sched_fiber);
#else
    swapcontext(&((posix_fiber_t *)g_current->fiber)->ctx, &g_sched_ctx);
#endif
}

/* ================= ready queue ================= */

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
    if (!t) abort();                  /* OOM: fail fast, like the rest of rt_* */
    t->co = owner;
    t->all_next = g_all_tasks;
    g_all_tasks = t;
    return t;
}

static void complete_task(zan_task_t *t, int64_t result) {
    if (t->completed) return;
    t->result = result;
    t->completed = 1;
    /* Wake every awaiter, not just one: several coroutines may await the same
     * task, and a single waiter slot silently strands all but one -- they
     * never reach the ready queue again and g_live never drops. The waiters
     * list is LIFO, so reverse it first to resume in park order (FIFO). */
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
        rev->next = NULL;   /* ready_push takes over `next` */
        ready_push(rev);
        rev = n;
    }
}

/* Detach and free a task the caller no longer references. Tasks stay alive
 * after completion so zan_task_result remains readable for as long as the
 * caller needs it; release is the call that declares "done". Safe to call
 * from any coroutine after its await on the task returned, and idempotent:
 * releasing an already-released (or never-tracked) pointer is a no-op. */
void zan_task_release(zan_task_t *task) {
    if (!task) return;
    zan_task_t **pp = &g_all_tasks;
    while (*pp && *pp != task) pp = &(*pp)->all_next;
    if (!*pp) return;   /* already released: ignore */
    if (!task->completed) {
        /* Contract violation and a latent use-after-free: a delay timer still
         * points at this task (timers_process would complete_task into freed
         * memory), its coroutine may not have run out, or another coroutine
         * is parked in await reading its result. There is no safe early
         * release -- refuse loudly instead of corrupting the heap. */
        fprintf(stderr, "zan runtime: task released before completion\n");
        abort();
    }
    *pp = task->all_next;
    free(task);
}

/* Number of task objects still tracked (live or completed but not released).
 * Test/diagnostic hook: with every task released, this is 0 at shutdown. */
size_t zan_task_live(void) {
    size_t n = 0;
    for (zan_task_t *t = g_all_tasks; t; t = t->all_next) n++;
    return n;
}

/* ================= timers ================= */

static void timer_add(zan_task_t *t, int64_t delay_ms) {
    zan_timer_t *tm = (zan_timer_t *)calloc(1, sizeof(*tm));
    if (!tm) abort();                 /* OOM: fail fast, like the rest of rt_* */
    tm->due_ms = plat_now_ms() + (delay_ms < 0 ? 0 : delay_ms);
    tm->task = t;
    tm->next = g_timers;
    g_timers = tm;
}

static int64_t timers_process(void) {
    int64_t now = plat_now_ms();
    zan_timer_t **pp = &g_timers;
    int64_t nearest = -1;
    while (*pp) {
        zan_timer_t *tm = *pp;
        if (tm->due_ms <= now) {
            complete_task(tm->task, 0);
            *pp = tm->next;
            free(tm);
        } else {
            int64_t d = tm->due_ms - now;
            if (nearest < 0 || d < nearest) nearest = d;
            pp = &tm->next;
        }
    }
    return nearest;
}

/* ================= coroutine trampoline ================= */

#ifdef _WIN32
static void WINAPI co_trampoline(void *p) {
    zan_co_t *co = (zan_co_t *)p;
    co->body(co->task);
    co->finished = 1;
    complete_task(co->task, 0);
    switch_to_sched();
}
#else
static void co_trampoline_posix(unsigned hi, unsigned lo) {
    zan_co_t *co = (zan_co_t *)(((uintptr_t)hi << 32) | (uintptr_t)lo);
    co->body(co->task);
    co->finished = 1;
    complete_task(co->task, 0);
}
#endif

/* ================= public ABI ================= */

zan_task_t *zan_spawn(zan_co_body_t body, void *arg) {
    zan_co_t *co = (zan_co_t *)calloc(1, sizeof(*co));
    if (!co) abort();                 /* OOM: fail fast, like the rest of rt_* */
    co->body = body;
    co->task = task_new(co);
    co->task->arg = arg;
    co->fiber = plat_fiber_new(co);
    if (!co->fiber) abort();          /* OOM / fiber limits: fail fast */
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
        /* Park on the task. A parked coroutine is off the ready queue, so its
         * `next` link is free to chain the waiter list -- no extra allocation
         * and no growth of zan_task for the (overwhelmingly common) single
         * awaiter case. */
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
    /* The current coroutine is NOT pushed to the ready queue.
     * It will be resumed by zan_io_resume() when its fd is ready. */
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
    g_all_tasks = NULL;
    g_live = 0;
    plat_sched_enter();
    zan_io_init();
}

void zan_sched_run(void) {
    while (g_live > 0) {
        /* Process timers */
        int64_t next_timer = timers_process();

        /* Poll IO events (non-blocking if we have ready coroutines) */
        zan_co_t *co = ready_pop();
        if (!co) {
            /* Nothing ready -- block until an IO event or the nearest timer
             * instead of polling at a fixed 1 ms. -1 waits forever, which is
             * safe: zan_io_poll sweeps watchers stranded on dead fds before
             * committing to the wait, and returns immediately when no
             * watchers are pending. */
            int64_t poll_timeout = -1;
            if (next_timer >= 0) poll_timeout = next_timer;
            if (zan_io_has_pending()) {
                zan_io_poll(poll_timeout);
                co = ready_pop();
            } else if (next_timer >= 0) {
                plat_sleep(next_timer);
                continue;
            } else {
                /* No ready coroutine, no timer and no pending IO: nothing can
                 * ever wake whatever is still parked, so leaving silently
                 * turns a lost wakeup into a clean-looking exit with no hint
                 * of the coroutine that never ran. Say so, and name the count
                 * so the stranded work is at least diagnosable. */
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
            /* We have a ready coroutine; still do a non-blocking IO poll */
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
    while (g_timers) { zan_timer_t *n = g_timers->next; free(g_timers); g_timers = n; }
    plat_sched_leave();
}
