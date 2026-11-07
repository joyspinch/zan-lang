/* rt_co.c: 无栈协程就绪队列与 M:1 协作式调度器驱动实现 */

#include "rt_co.h"
#include "rt_timer.h"

#include <stdlib.h>

#include "../common/host_oom.h"
#include "rt_oom.h"  /* B-ID98 站点化 OOM 注入 */

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
    zan_co_slot_t *nq = (zan_co_slot_t *)ZAN_OOM_MALLOC("co.queue_grow",
                                                        ncap * sizeof(*nq));
    if (!nq) {
        /* B-ID98：翻倍失败降档 +16，碎片化内存下最大化扩容成功率 */
        ncap = g_cap ? g_cap + 16 : 16;
        nq = (zan_co_slot_t *)ZAN_OOM_MALLOC("co.queue_grow",
                                             ncap * sizeof(*nq));
    }
    if (!nq) {
        /* 队列扩容遇 OOM 丢弃本次恢复，保护主进程与已连接会话不崩溃 */
        fprintf(stderr, "zan runtime: coroutine ready-queue grow failed "
                        "(OOM); dropping one resumption\n");
        /* 双档全败必须早退：曾继续线性化写 NULL 并 free 掉现存队列
         * （本批 OOM 注入钉子暴露的落空分支） */
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
    if (g_len == g_cap) return;
    size_t tail = (g_head + g_len) % g_cap;
    g_queue[tail].frame = frame;
    g_queue[tail].step  = step;
    g_len++;
}

void zan_co_ready_batch(const zan_co_task_t *tasks, size_t count) {
    if (!tasks || count == 0) return;
    for (size_t i = 0; i < count; i++) {
        zan_co_ready(tasks[i].frame, tasks[i].step);
    }
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

void *__zan_co_frame_alloc(size_t size) {
    return calloc(1, size ? size : 1);
}

void __zan_co_frame_free(void *frame) {
    free(frame);
}
