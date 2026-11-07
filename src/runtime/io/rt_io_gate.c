/* rt_io_gate.c: 异步协程门禁同步原语与精准看门狗实现 */

/* 协程计数信号门原语 */
#if defined(ZAN_CO_DRIVER) && !defined(_WIN32)
#include <pthread.h>
#endif
typedef struct zan_gate_waiter {
    struct zan_gate_waiter *next;
    void                   *frame;
    zan_co_step_t           step;
    long long               ticket;
} zan_gate_waiter;

typedef struct zan_gate {
    zan_gate_waiter *head;
    zan_gate_waiter *tail;
    long long        surplus;
    long long        next_ticket;
    long long        arming_ticket;
    int              ref_count;
    int              closed;
#if defined(ZAN_CO_DRIVER) && defined(_WIN32)
    CRITICAL_SECTION lock;
#elif defined(ZAN_CO_DRIVER)
    pthread_mutex_t  lock;
#endif
} zan_gate;

#if defined(ZAN_CO_DRIVER) && defined(_WIN32)
static void zan_gate_lock(zan_gate *g)   { EnterCriticalSection(&g->lock); }
static void zan_gate_unlock(zan_gate *g) { LeaveCriticalSection(&g->lock); }
static void zan_gate_lock_init(zan_gate *g) { InitializeCriticalSection(&g->lock); }
static void zan_gate_lock_free(zan_gate *g) { DeleteCriticalSection(&g->lock); }
#elif defined(ZAN_CO_DRIVER)
static void zan_gate_lock(zan_gate *g)   { pthread_mutex_lock(&g->lock); }
static void zan_gate_unlock(zan_gate *g) { pthread_mutex_unlock(&g->lock); }
static void zan_gate_lock_init(zan_gate *g) { pthread_mutex_init(&g->lock, NULL); }
static void zan_gate_lock_free(zan_gate *g) { pthread_mutex_destroy(&g->lock); }
#else
static void zan_gate_lock(zan_gate *g)   { (void)g; }
static void zan_gate_unlock(zan_gate *g) { (void)g; }
static void zan_gate_lock_init(zan_gate *g) { (void)g; }
static void zan_gate_lock_free(zan_gate *g) { (void)g; }
#endif

/* 分配a gate; returns an opaque handle (its address as an integer) */
long long zan_gate_new(void) {
    zan_gate *g = (zan_gate *)calloc(1, sizeof(*g));
    if (!g) return 0;
    g->ref_count = 1;
    zan_gate_lock_init(g);
    return (long long)(intptr_t)g;
}

/* 登记一次超时等待票据并增加门引用计数，返回 > 0 的 ticket；已 Close 返回 0 */
long long zan_gate_arm(long long handle) {
    zan_gate *g = (zan_gate *)(intptr_t)handle;
    if (!g) return 0;
    zan_gate_lock(g);
    if (g->closed) {
        zan_gate_unlock(g);
        return 0;
    }
    long long ticket = ++g->next_ticket;
    if (ticket <= 0) { ticket = g->next_ticket = 1; }
    g->arming_ticket = ticket;
    g->ref_count++;
    zan_gate_unlock(g);
    return ticket;
}

/* 将当前协程挂起在信号门等待队列 */
void zan_gate_park(long long handle, void *frame, zan_co_step_t step) {
    zan_gate *g = (zan_gate *)(intptr_t)handle;
    if (!g) { if (step) zan_co_ready(frame, step); return; }
    if (!step) return;
    zan_gate_lock(g);
    if (g->closed) {
        g->arming_ticket = 0;
        zan_gate_unlock(g);
        zan_co_ready(frame, step);
        return;
    }
    if (g->surplus > 0) {
        g->surplus--;
        g->arming_ticket = 0;
        zan_gate_unlock(g);
        zan_co_ready(frame, step);
        return;
    }
    zan_gate_waiter *w = (zan_gate_waiter *)malloc(sizeof(*w));
    if (!w) {
        g->arming_ticket = 0;
        zan_gate_unlock(g);
        zan_co_ready(frame, step);
        return;
    }
    w->next = NULL;
    w->frame = frame;
    w->step = step;
    w->ticket = g->arming_ticket;
    g->arming_ticket = 0;
    if (g->tail) g->tail->next = w; else g->head = w;
    g->tail = w;
    zan_gate_unlock(g);
}

void zan_gate_signal(long long handle) {
    zan_gate *g = (zan_gate *)(intptr_t)handle;
    if (!g) return;
    zan_gate_lock(g);
    if (g->closed) {
        zan_gate_unlock(g);
        return;
    }
    zan_gate_waiter *w = g->head;
    if (w) {
        g->head = w->next;
        if (!g->head) g->tail = NULL;
        zan_gate_unlock(g);
        zan_co_ready(w->frame, w->step);
        free(w);
        return;
    }
    g->surplus++;
    zan_gate_unlock(g);
}

/* 唤醒全部等待者（Monitor.PulseAll 口径）；无等待者时暂存一次
 * 信号——与 zan_gate_signal 的计数语义一致，先 SignalAll 后 Wait
 * 立即通过。 */
void zan_gate_signal_all(long long handle) {
    zan_gate *g = (zan_gate *)(intptr_t)handle;
    if (!g) return;
    zan_gate_lock(g);
    if (g->closed) {
        zan_gate_unlock(g);
        return;
    }
    zan_gate_waiter *w = g->head;
    if (!w) {
        g->surplus++;
        zan_gate_unlock(g);
        return;
    }
    g->head = g->tail = NULL;
    zan_gate_unlock(g);
    while (w) {
        zan_gate_waiter *n = w->next;
        zan_co_ready(w->frame, w->step);
        free(w);
        w = n;
    }
}

/* 释放门句柄；置 closed 标志并就绪全部等待者，递减基准引用计数 */
void zan_gate_free(long long handle) {
    zan_gate *g = (zan_gate *)(intptr_t)handle;
    if (!g) return;
    zan_gate_lock(g);
    if (g->closed) {
        zan_gate_unlock(g);
        return;
    }
    g->closed = 1;
    zan_gate_waiter *w = g->head;
    g->head = g->tail = NULL;
    g->surplus = 0;
    zan_gate_unlock(g);
    while (w) {
        zan_gate_waiter *n = w->next;
        zan_co_ready(w->frame, w->step);
        free(w);
        w = n;
    }
    zan_gate_lock(g);
    g->ref_count--;
    int destroy = (g->ref_count <= 0);
    zan_gate_unlock(g);
    if (destroy) {
        zan_gate_lock_free(g);
        free(g);
    }
}

/* 看门狗超时唤醒回调：若指定 ticket 仍挂在等待队列中则精准唤醒它并摘除，
 * 绝不向 surplus 写入幽灵信号；随后递减引用计数并在零引用时彻底释放 */
void zan_gate_ticket_signal_and_release(long long handle, long long ticket) {
    zan_gate *g = (zan_gate *)(intptr_t)handle;
    if (!g) return;
    zan_gate_lock(g);
    if (!g->closed && ticket > 0) {
        zan_gate_waiter *prev = NULL;
        zan_gate_waiter *curr = g->head;
        while (curr) {
            if (curr->ticket == ticket) {
                if (prev) {
                    prev->next = curr->next;
                } else {
                    g->head = curr->next;
                }
                if (curr == g->tail) {
                    g->tail = prev;
                }
                zan_co_ready(curr->frame, curr->step);
                free(curr);
                break;
            }
            prev = curr;
            curr = curr->next;
        }
    }
    g->ref_count--;
    int destroy = (g->ref_count <= 0);
    zan_gate_unlock(g);
    if (destroy) {
        zan_gate_lock_free(g);
        free(g);
    }
}

/* Gate 协程同步原语运行时看门狗控制器（直通最小堆定时器，零协程分配、零幽灵信号盈余） */
typedef struct zan_gate_watchdog {
    long long handle;
    long long ticket;
    long long timer_id;
    volatile int done;
    volatile int ref;
} zan_gate_watchdog;

static void gate_watchdog_cb(void *arg) {
    zan_gate_watchdog *w = (zan_gate_watchdog *)arg;
    if (!w) return;
    if (__sync_bool_compare_and_swap(&w->done, 0, 1)) {
        zan_gate_ticket_signal_and_release(w->handle, w->ticket);
    }
    if (__sync_sub_and_fetch(&w->ref, 1) == 0) {
        free(w);
    }
}

long long zan_gate_watchdog_arm(long long handle, long long timeout_ms) {
    if (handle == 0 || timeout_ms <= 0) return 0;
    long long ticket = zan_gate_arm(handle);
    if (ticket == 0) return 0;
    zan_gate_watchdog *w = (zan_gate_watchdog *)calloc(1, sizeof(*w));
    if (!w) {
        zan_gate_ticket_signal_and_release(handle, 0);
        return 0;
    }
    w->handle = handle;
    w->ticket = ticket;
    w->ref = 2; /* 定时器持有 1，调用方持有 1 */
    w->timer_id = zan_timer_after_ctx(timeout_ms, gate_watchdog_cb, w);
    if (w->timer_id == 0) {
        zan_gate_ticket_signal_and_release(handle, 0);
        free(w);
        return 0;
    }
    return (long long)(intptr_t)w;
}

void zan_gate_watchdog_cancel(long long watchdog_handle) {
    zan_gate_watchdog *w = (zan_gate_watchdog *)(intptr_t)watchdog_handle;
    if (!w) return;
    if (w->timer_id > 0) {
        zan_timer_clear(w->timer_id);
    }
    if (__sync_bool_compare_and_swap(&w->done, 0, 1)) {
        /* 若定时器尚未触发，提前唤醒/取消负责释放 gate 上的引用计数 */
        zan_gate_ticket_signal_and_release(w->handle, 0);
    }
    if (__sync_sub_and_fetch(&w->ref, 1) == 0) {
        free(w);
    }
}

