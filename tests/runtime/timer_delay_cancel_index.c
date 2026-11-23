/* Standalone regression: clang timer_delay_cancel_index.c -o timer_delay_cancel_index
 *
 * zan_timer_cancel_delay runs on every async frame completion. It used to scan
 * and rebuild the whole timer heap under the global timer lock, so a server
 * with ~1000 pending watchdog delays serialized every completion on that scan.
 * It now goes through a frame -> entry index with lazy deletion. This checks
 * the index against a brute-force model: fired set, firing order, cancel
 * counts, index churn (growth, backward-shift erase), and dead-entry
 * compaction keeping the heap bounded. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <time.h>
#include "../../src/runtime/rt_timer.h"
#include "../../src/runtime/rt_oom.h"

#if defined(_WIN32)
#include <windows.h>
static CRITICAL_SECTION g_lock;
static int g_lock_ready;
static void timer_lock(void) {
    if (!g_lock_ready) { InitializeCriticalSection(&g_lock); g_lock_ready = 1; }
    EnterCriticalSection(&g_lock);
}
static void timer_unlock(void) { LeaveCriticalSection(&g_lock); }
static unsigned long timer_self_tid(void) { return GetCurrentThreadId(); }
static void timer_yield(void) { Sleep(1); }
#else
#include <pthread.h>
#include <sched.h>
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static void timer_lock(void) { pthread_mutex_lock(&g_lock); }
static void timer_unlock(void) { pthread_mutex_unlock(&g_lock); }
static unsigned long timer_self_tid(void) { return (unsigned long)(uintptr_t)pthread_self(); }
static void timer_yield(void) { sched_yield(); }
#endif

#include "../../src/runtime/timer/rt_timer_core.c"

#define FRAMES 4096
static char frames[FRAMES];      /* distinct addresses used as frames */
static int fired[FRAMES];
static int fire_order[FRAMES * 4];
static int fire_count;

static void on_step(void *frame) {
    int id = (int)((char *)frame - frames);
    fired[id]++;
    fire_order[fire_count++] = id;
}

static void fire_all(void) {
    /* Every test delay is 0 ms: due immediately, ordered by sequence. */
    while (zan_timer_dispatch_due() > 0) {}
}

static void reset(void) {
    zan_timer_runtime_reset();
    memset(fired, 0, sizeof(fired));
    fire_count = 0;
}

static unsigned rng = 12345u;
static unsigned next_rand(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static void randomized_against_model(void) {
    reset();
    static int expect[FRAMES];
    memset(expect, 0, sizeof(expect));
    for (int round = 0; round < 40; round++) {
        for (int k = 0; k < 600; k++) {
            int id = (int)(next_rand() % FRAMES);
            zan_timer_delay(0, &frames[id], on_step);
            expect[id]++;
        }
        for (int k = 0; k < 500; k++) {
            int id = (int)(next_rand() % FRAMES);
            int got = zan_timer_cancel_delay(&frames[id]);
            assert(got == expect[id]);
            expect[id] = 0;
            /* A second cancel finds nothing. */
            assert(zan_timer_cancel_delay(&frames[id]) == 0);
        }
        long long live = 0;
        for (int i = 0; i < FRAMES; i++) live += expect[i];
        assert(zan_timer_pending() == live);
        /* Lazy deletion must stay bounded: dead entries never exceed half
         * the heap once there are enough of them to compact. */
        assert(g_heap_dead < 64 || g_heap_dead * 2 < g_heap_len + 2);
    }
    fire_all();
    for (int i = 0; i < FRAMES; i++) assert(fired[i] == expect[i]);
    assert(zan_timer_pending() == 0);
    assert(g_dix_len == 0);
}

static void order_preserved_after_compaction(void) {
    reset();
    /* 200 frames, cancel every other one -> 100 dead entries trigger the
     * compaction path; survivors must still fire in insertion order. */
    for (int i = 0; i < 200; i++) zan_timer_delay(0, &frames[i], on_step);
    for (int i = 0; i < 200; i += 2) assert(zan_timer_cancel_delay(&frames[i]) == 1);
    assert(g_heap_len == 100);  /* compacted, not just marked */
    fire_all();
    assert(fire_count == 100);
    for (int k = 0; k < 100; k++) assert(fire_order[k] == 2 * k + 1);
}

static void multiple_entries_one_frame(void) {
    reset();
    for (int k = 0; k < 5; k++) zan_timer_delay(0, &frames[7], on_step);
    zan_timer_delay(0, &frames[8], on_step);
    assert(zan_timer_cancel_delay(&frames[7]) == 5);
    fire_all();
    assert(fired[7] == 0 && fired[8] == 1);
}

static void long_delay_cancel_does_not_grow_heap(void) {
    reset();
    /* Long delays cancelled right away (the pool-watchdog pattern) must not
     * pile up in the heap until they fall due. */
    for (int k = 0; k < 100000; k++) {
        int id = k % FRAMES;
        zan_timer_delay(60000, &frames[id], on_step);
        assert(zan_timer_cancel_delay(&frames[id]) == 1);
    }
    assert(g_heap_len < 256);
    assert(zan_timer_pending() == 0);
}

int main(void) {
    randomized_against_model();
    order_preserved_after_compaction();
    multiple_entries_one_frame();
    long_delay_cancel_does_not_grow_heap();
    printf("timer_delay_cancel_index: ok\n");
    return 0;
}
