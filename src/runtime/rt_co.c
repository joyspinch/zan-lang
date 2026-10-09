/* rt_co.c: 无栈协程就绪队列与 M:1 协作式调度器驱动实现 */

#include "rt_co.h"
#include "rt_timer.h"

#include <stdlib.h>

#include "../common/host_oom.h"

/* 就绪协程槽位：step(frame) 重新进入状态机执行 */
typedef struct {
    void         *frame;
    zan_co_step_t step;
} zan_co_slot_t;

/* 基于环形缓冲区的 FIFO 就绪队列（单线程协作式无锁模型） */
static zan_co_slot_t *g_queue;
static size_t         g_cap;    /* 队列容量 */
static size_t         g_len;    /* 就绪项数量 */
static size_t         g_head;   /* 出队游标 */

/* IO 反应堆空闲等待桥接回调，无回调时为 NULL */
static zan_co_idle_fn g_idle;

void zan_co_sched_init(void) {
    free(g_queue);
    g_queue = NULL;
    g_cap = g_len = g_head = 0;
    g_idle = NULL;
}

void zan_co_set_idle(zan_co_idle_fn fn) {
    g_idle = fn;
}

static void queue_grow(void) {
    size_t ncap = g_cap ? g_cap * 2 : 16;
    zan_co_slot_t *nq = (zan_co_slot_t *)realloc(g_queue, ncap * sizeof(*nq));
    if (!nq) {
        /* 队列扩容遇 OOM 丢弃本次恢复，保护主进程与已连接会话不崩溃 */
        fprintf(stderr, "zan runtime: coroutine ready-queue grow failed "
                        "(OOM); dropping one resumption\n");
        return;
    }
    /* 将环形缓冲区重新线性化到新存储区 */
    for (size_t i = 0; i < g_len; i++) {
        nq[i] = g_queue[(g_head + i) % g_cap];
    }
    free(g_queue);
    g_queue = nq;
    g_cap = ncap;
    g_head = 0;
}

void zan_co_ready(void *frame, zan_co_step_t step) {
    if (!step) return;
    if (g_len == g_cap) queue_grow();
    if (g_len == g_cap) return;   /* 核心系统底层抽象与内存语义契约 */
    size_t tail = (g_head + g_len) % g_cap;
    g_queue[tail].frame = frame;
    g_queue[tail].step  = step;
    g_len++;
}

void zan_co_sched_run_until(const volatile int *done) {
    for (;;) {
        while (g_len > 0) {
            if (done && *done) return;
            zan_co_slot_t slot = g_queue[g_head];
            g_head = (g_head + 1) % g_cap;
            g_len--;
            slot.step(slot.frame);
        }
        if (done && *done) return;
        /* 就绪队列已清空。若接入 IO 反应堆，则阻塞等待外部事件派发新任务 */
        if (!g_idle) return;
        if (g_idle() <= 0) return;
    }
}

void zan_co_sched_run(void) {
    zan_co_sched_run_until(NULL);
}

size_t zan_co_pending(void) {
    return g_len;
}
