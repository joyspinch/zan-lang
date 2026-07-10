/* rt_co.c -- Zan stackless-coroutine driver (see rt_co.h). */

#include "rt_co.h"

#include <stdlib.h>

#include "../common/host_oom.h"
/* A queued resumption: step(frame) will re-enter the state machine. */
typedef struct {
    void         *frame;
    zan_co_step_t step;
} zan_co_slot_t;

/* FIFO ready queue implemented as a growable circular buffer. Single-threaded,
 * cooperative: a step() runs to its next suspension point (or completion)
 * before control returns here, so no locking is needed in the M:1 model. */
static zan_co_slot_t *g_queue;
static size_t         g_cap;    /* allocated slots */
static size_t         g_len;    /* live entries */
static size_t         g_head;   /* index of next to pop */

/* Optional bridge to a blocking event source (the IO reactor); NULL = none. */
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
    zan_co_slot_t *nq = (zan_co_slot_t *)malloc(ncap * sizeof(*nq));
    if (!nq) abort();
    /* Re-linearise the circular buffer into the new storage. */
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
    size_t tail = (g_head + g_len) % g_cap;
    g_queue[tail].frame = frame;
    g_queue[tail].step  = step;
    g_len++;
}

void zan_co_sched_run(void) {
    for (;;) {
        while (g_len > 0) {
            zan_co_slot_t slot = g_queue[g_head];
            g_head = (g_head + 1) % g_cap;
            g_len--;
            slot.step(slot.frame);
        }
        /* Ready queue drained. If an idle bridge (IO reactor) is wired, block
         * for external events that may enqueue more work; stop when it reports
         * nothing left to wait for. */
        if (!g_idle) return;
        if (g_idle() <= 0) return;
    }
}

size_t zan_co_pending(void) {
    return g_len;
}
