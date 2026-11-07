/* rt_sched.c: M:N 混合型协程调度器与任务生命周期（跨平台分发入口） */

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#include "rt_sched.h"
#include "rt_io.h"
#include "rt_timer.h"
#include "rt_co.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
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

typedef struct zan_co zan_co_t;
typedef struct zan_task zan_task_t;
typedef struct zan_worker zan_worker_t;

/* 前向声明完成任务与纤程切出调度器 */
static void complete_task(zan_task_t *t, int64_t result);
static void *watchdog_thread_main(void *arg);

/* --------------------------------------------------------------------------
 * 引入平台纤程、换栈与底层同步原语
 * -------------------------------------------------------------------------- */

struct zan_co {
    void          *fiber;
    zan_co_body_t  body;
    zan_task_t    *task;
    int            finished;
    volatile int   running;
    int32_t        last_error;
    /* B-ID86: 时间片与协作抢占跟踪 */
    int64_t        slice_start_ms;
    uint32_t       slice_ticks;
    volatile int   preempt_requested;
    struct zan_co *next;
};

static ZAN_THREAD_LOCAL zan_co_t *g_current;
static ZAN_THREAD_LOCAL int       g_worker_id = -1;

#ifdef _WIN32
#include "sched/rt_sched_fiber_win.c"
#else
#include "sched/rt_sched_fiber_posix.c"
#endif

/* --------------------------------------------------------------------------
 * 任务控制块与定时器定义
 * -------------------------------------------------------------------------- */

struct zan_task {
    zan_mutex_t lock;
    int         completed;
    int64_t     result;
    void       *arg;
    zan_co_t   *co;
    zan_co_t   *waiters;
    zan_task_t *all_next;
};

typedef struct zan_timer {
    int64_t     due_ms;
    zan_task_t *task;
} zan_timer_t;

#define ZAN_MAX_WORKERS 32
#define ZAN_LOCAL_QUEUE_CAP 256

#if defined(_MSC_VER)
#define ZAN_CACHELINE_ALIGN __declspec(align(64))
#elif defined(__GNUC__) || defined(__clang__)
#define ZAN_CACHELINE_ALIGN __attribute__((aligned(64)))
#else
#define ZAN_CACHELINE_ALIGN _Alignas(64)
#endif

struct ZAN_CACHELINE_ALIGN zan_worker {
    int                 id;
    int                 active;
    volatile int        sleeping;
    zan_mutex_t         lock;
    zan_co_t           *local_head;
    zan_co_t           *local_tail;
    int                 local_count;
    /* B-ID86: 正在该 Worker 上执行的当前协程（供 Watchdog 采样检测超时） */
    zan_co_t * volatile running_co;
    plat_thread_t       thread;
    plat_worker_sync_t  sync;
    char                pad[64];
};

static zan_worker_t        g_workers[ZAN_MAX_WORKERS];
static int                 g_worker_count = 1;
static volatile int        g_sched_stop   = 0;
static volatile int        g_live         = 0;
static int64_t             g_sched_quantum_ms = 10;

static inline void co_switch_to(zan_worker_t *w, zan_co_t *co) {
    if (w) w->running_co = co;
    plat_switch_co(co);
    if (w) w->running_co = NULL;
}

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

/* --------------------------------------------------------------------------
 * Worker 唤醒与等待抽象
 * -------------------------------------------------------------------------- */

static void wake_one_worker(void) {
    if (g_worker_count <= 1) return;
    int start = (g_worker_id >= 0) ? (g_worker_id + 1) % g_worker_count : 0;
    for (int k = 0; k < g_worker_count; k++) {
        int i = (start + k) % g_worker_count;
        if (g_workers[i].active && g_workers[i].sleeping) {
            plat_worker_sync_wake_one(&g_workers[i].sync);
            break;
        }
    }
}

static void wake_all_workers(void) {
    for (int i = 0; i < g_worker_count; i++) {
        if (g_workers[i].active) {
            plat_worker_sync_wake_all(&g_workers[i].sync);
        }
    }
}

static void worker_wait(zan_worker_t *w, int timeout_ms) {
    if (!w) {
        plat_sleep(timeout_ms > 0 ? timeout_ms : 1);
        return;
    }
    w->sleeping = 1;
    plat_worker_sync_wait(&w->sync, timeout_ms);
    w->sleeping = 0;
}

/* --------------------------------------------------------------------------
 * 任务入队、出队与工作窃取（Work Stealing）
 * -------------------------------------------------------------------------- */

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
            int count = w->local_count;
            zan_mutex_unlock(&w->lock);
            if (count > 1) {
                wake_one_worker();
            }
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

/* --------------------------------------------------------------------------
 * 任务控制块与生命周期
 * -------------------------------------------------------------------------- */

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
        ready_push(rev);
        rev = n;
    }
}

/* --------------------------------------------------------------------------
 * 最小二叉堆定时器管理
 * -------------------------------------------------------------------------- */

static void timer_add(zan_task_t *task, int64_t delay_ms) {
    if (delay_ms < 0) delay_ms = 0;
    int64_t due = plat_now_ms() + delay_ms;
    zan_mutex_lock(&g_timer_lock);
    if (g_timer_n >= g_timer_cap) {
        size_t ncap = g_timer_cap ? g_timer_cap * 2 : 16;
        zan_timer_t *nt = (zan_timer_t *)realloc(g_timers, ncap * sizeof(*nt));
        if (!nt) zan_rt_fatal("oom", "scheduler: timer realloc failed");
        g_timers = nt;
        g_timer_cap = ncap;
    }
    size_t i = g_timer_n++;
    while (i > 0) {
        size_t p = (i - 1) / 2;
        if (g_timers[p].due_ms <= due) break;
        g_timers[i] = g_timers[p];
        i = p;
    }
    g_timers[i].due_ms = due;
    g_timers[i].task = task;
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

/* --------------------------------------------------------------------------
 * Worker 线程与 Watchdog 抢占主循环
 * -------------------------------------------------------------------------- */

static void *worker_thread_main(void *arg) {
    zan_worker_t *w = (zan_worker_t *)arg;
    g_worker_id = w->id;
    plat_sched_enter();

    while (!g_sched_stop) {
        zan_co_t *co = ready_pop_worker(w->id);
        if (co) {
            co_switch_to(w, co);
            if (co->finished) {
                plat_fiber_delete(co->fiber);
                free(co);
                zan_atomic_dec(&g_live);
            } else {
                co_release(co);
            }
        } else {
            /* 空闲 Worker 协助轮询就绪 IO，杜绝 IO 假死 */
            if (zan_io_has_pending()) {
                zan_io_poll(0);
                co = ready_pop_worker(w->id);
                if (co) {
                    co_switch_to(w, co);
                    if (co->finished) {
                        plat_fiber_delete(co->fiber);
                        free(co);
                        zan_atomic_dec(&g_live);
                    } else {
                        co_release(co);
                    }
                    continue;
                }
            }
            worker_wait(w, 2);
        }
    }

    plat_sched_leave();
    return NULL;
}

static void *watchdog_thread_main(void *arg) {
    (void)arg;
    while (!g_sched_stop) {
        plat_sleep(2);
        int64_t now = plat_now_ms();
        for (int i = 0; i < g_worker_count; i++) {
            zan_co_t *co = g_workers[i].running_co;
            if (co && !co->preempt_requested) {
                if (now - co->slice_start_ms >= g_sched_quantum_ms) {
                    co->preempt_requested = 1;
                }
            }
        }
    }
    return NULL;
}

/* --------------------------------------------------------------------------
 * 协程调度公共 API
 * -------------------------------------------------------------------------- */

zan_task_t *zan_spawn(zan_co_body_t body, void *arg) {
    zan_co_t *co = (zan_co_t *)calloc(1, sizeof(*co));
    if (!co) zan_rt_fatal("oom", "scheduler: coroutine alloc failed");
    co->body = body;
    zan_task_t *task = task_new(co);
    co->task = task;
    co->task->arg = arg;
    co->fiber = plat_fiber_new(co);
    if (!co->fiber) zan_rt_fatal("oom", "scheduler: fiber alloc failed");
    zan_atomic_inc(&g_live);
    ready_push(co);
    return task;
}

void *zan_task_arg(zan_task_t *task) { return task ? task->arg : NULL; }

void zan_task_return(zan_task_t *task, int64_t result) {
    if (g_current) g_current->finished = 1;
    complete_task(task, result);
    plat_switch_to_sched();
}

int64_t zan_task_await(zan_task_t *task) {
    if (!task) return 0;
    zan_mutex_lock(&task->lock);
    if (!task->completed) {
        g_current->next = task->waiters;
        task->waiters = g_current;
        zan_mutex_unlock(&task->lock);
        plat_switch_to_sched();
        zan_mutex_lock(&task->lock);
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
        plat_switch_to_sched();
    }
}

int64_t zan_task_result(zan_task_t *task) { return task ? task->result : 0; }

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
        fprintf(stderr, "zan runtime: task released before completion\n");
        abort();
    }
    *pp = task->all_next;
    zan_mutex_unlock(&g_all_tasks_lock);
    free(task);
}

size_t zan_task_live(void) {
    zan_mutex_lock(&g_all_tasks_lock);
    size_t n = 0;
    for (zan_task_t *t = g_all_tasks; t; t = t->all_next) n++;
    zan_mutex_unlock(&g_all_tasks_lock);
    return n;
}

/* --------------------------------------------------------------------------
 * 时间片抢占与检测（Time-slice Preemption B-ID86）
 * -------------------------------------------------------------------------- */

void zan_sched_set_quantum_ms(int64_t ms) {
    if (ms <= 0) ms = 1;
    g_sched_quantum_ms = ms;
}

int64_t zan_sched_get_quantum_ms(void) {
    return g_sched_quantum_ms;
}

int zan_task_should_preempt(void) {
    zan_co_t *co = g_current;
    if (!co) return 0;
    if (co->preempt_requested) return 1;

    /* 周期性采样时钟（每 64 次调用检测一次系统时钟，兼顾微秒响应与零开销） */
    if ((++co->slice_ticks & 63) == 0) {
        int64_t now = plat_now_ms();
        if (now - co->slice_start_ms >= g_sched_quantum_ms) {
            co->preempt_requested = 1;
            return 1;
        }
    }
    return 0;
}

void zan_task_check_preempt(void) {
    if (zan_task_should_preempt()) {
        zan_task_yield();
    }
}

/* --------------------------------------------------------------------------
 * IO 事件集成桥接
 * -------------------------------------------------------------------------- */

void zan_io_suspend_current(void) {
    if (g_current) {
        plat_switch_to_sched();
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

/* --------------------------------------------------------------------------
 * 调度器初始化、启动与优雅停机
 * -------------------------------------------------------------------------- */

void zan_sched_init(void) {
    zan_mutex_init(&g_global_lock);
    zan_mutex_init(&g_timer_lock);
    zan_mutex_init(&g_all_tasks_lock);
    g_current = NULL;
    g_ready_head = g_ready_tail = NULL;
    g_timers = NULL;
    g_timer_n = 0;
    g_timer_cap = 0;
    g_all_tasks = NULL;
    g_live = 0;
    g_sched_stop = 0;
    g_sched_quantum_ms = 10;
    const char *qenv = getenv("ZAN_SCHED_QUANTUM");
    if (qenv && qenv[0]) {
        int q = atoi(qenv);
        if (q > 0) g_sched_quantum_ms = q;
    }

    for (int i = 0; i < ZAN_MAX_WORKERS; i++) {
        g_workers[i].id = i;
        g_workers[i].active = 0;
        g_workers[i].sleeping = 0;
        g_workers[i].running_co = NULL;
        zan_mutex_init(&g_workers[i].lock);
        g_workers[i].local_head = NULL;
        g_workers[i].local_tail = NULL;
        g_workers[i].local_count = 0;
        plat_worker_sync_init(&g_workers[i].sync);
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

    /* 启动 Watchdog 抢占巡检后台线程 */
    plat_watchdog_start();

    /* 启动多 Worker 辅助线程 */
    for (int i = 1; i < g_worker_count; i++) {
        g_workers[i].id = i;
        g_workers[i].active = 1;
        plat_thread_create(&g_workers[i].thread, worker_thread_main, &g_workers[i]);
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
            co_switch_to(&g_workers[0], co);
            if (co->finished) {
                plat_fiber_delete(co->fiber);
                free(co);
                zan_atomic_dec(&g_live);
            } else {
                co_release(co);
            }
        }
    }

    /* 优雅通知所有 Worker 退出 */
    g_sched_stop = 1;
    wake_all_workers();

    /* 等待并停止 Watchdog 线程 */
    plat_watchdog_stop();

    /* 回收所有辅助 Worker 线程句柄 */
    for (int i = 1; i < g_worker_count; i++) {
        if (g_workers[i].active) {
            plat_thread_join(g_workers[i].thread);
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
        plat_worker_sync_destroy(&g_workers[i].sync);
    }

    zan_mutex_destroy(&g_global_lock);
    zan_mutex_destroy(&g_timer_lock);
    zan_mutex_destroy(&g_all_tasks_lock);
    plat_sched_drain_stacks();
    plat_sched_leave();
}
