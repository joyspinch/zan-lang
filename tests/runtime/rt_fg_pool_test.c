/* rt_fg_pool_test.c -- a ready() during a FOREGROUND multi-worker run must
 * not start a second worker pool.
 *
 * The foreground branch of zan_co_sched_run starts workers 1..N-1 and runs
 * worker 0 on the calling thread WITHOUT raising g_co_pool_live, so every
 * wake (an IO completion, a timer pop, a foreign spawn) reached
 * co_pool_ensure, saw "no pool", and started a detached background
 * generation behind the foreground pool's back: 2x workers over-subscribed
 * on the same queues, two pool lifecycles interleaved.
 *
 * Shape: main arms a 300ms timer (pending work for co_all_idle, but arming
 * does NOT call zan_co_ready -- the ready fires only at dispatch), so the
 * foreground branch is taken with no pool live. At t=50ms a helper pthread
 * readies w1 from outside the pool; w1_step (on a foreground worker) readies
 * w2 -- both are the exact co_pool_ensure trigger. The helper then exercises
 * the root-await protocol (zan_co_sched_run_until) against the running
 * foreground pool, which must wait for the named frame, never start a pool
 * on its own thread. Thread count is sampled from /proc/self/status at
 * every step (right after the foreign ready, since ensure runs synchronously
 * there).
 *
 * Expected: peak = 5 (main + 3 foreground workers + helper); the pre-fix
 * code peaked at 9 (a detached pool of 4 on top). Outside Linux the thread
 * assertion is vacuous (no /proc) and the test still proves the frames run
 * to completion under the single-pool discipline.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "../../src/runtime/rt_co.h"

/* zan_co_delay lives in the driver half of rt_io.c; not in rt_co.h. */
void zan_co_delay(long long ms, void *frame, void (*step)(void *));

/* Driver frame prefix -- must mirror zan_co_fhdr in rt_io.c: the first two
 * slots of every compiler-emitted async frame (ASYNC_FRAME_SCHED /
 * ASYNC_FRAME_SCHED_STEP). */
typedef struct {
    volatile long long sched;
    void (*pending)(void *);
    int done;
    int id;
} pframe_t;

static pframe_t w1, w2, tf;
static int fails = 0;

static void check(int cond, const char *what) {
    if (!cond) { printf("FAIL: %s\n", what); fails++; }
    else       { printf("ok: %s\n", what); }
}

static int thr_now(void) {
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    int n = -1;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "Threads: %d", &n) == 1) break;
    fclose(f);
    return n;
}

static int peak_thr = 0;
static void sample_thr(void) {
    int n = thr_now();
    if (n > peak_thr) peak_thr = n;
}

static void w2_step(void *fp) { pframe_t *f = fp; sample_thr(); f->done = 1; }

static void w1_step(void *fp) {
    pframe_t *f = fp;
    sample_thr();
    /* Worker-to-worker wake while the foreground pool runs. */
    zan_co_ready(&w2, w2_step);
    f->done = 1;
}

static void t_step(void *fp) { pframe_t *f = fp; sample_thr(); f->done = 1; }

static void *helper(void *arg) {
    (void)arg;
    usleep(50 * 1000);
    /* Foreign-thread ready DURING the foreground run. co_pool_ensure runs
     * synchronously inside zan_co_ready, so the count right after it returns
     * already shows any second pool it started. */
    zan_co_ready(&w1, w1_step);
    sample_thr();
    /* Root-await protocol against the serving foreground pool: must wait for
     * the ONE named frame -- never start a second pool on this thread. */
    zan_co_sched_run_until(&w2.done);
    if (!w2.done) { printf("FAIL: run_until returned before w2 done\n"); fails++; }
    return NULL;
}

int main(void) {
    setenv("ZAN_CO_WORKERS", "4", 1);
    zan_co_sched_init();
    memset(&w1, 0, sizeof w1);
    memset(&w2, 0, sizeof w2);
    memset(&tf, 0, sizeof tf);

    zan_co_delay(300, &tf, t_step);
    pthread_t h;
    pthread_create(&h, NULL, helper, NULL);
    zan_co_sched_run();            /* foreground: this thread becomes worker 0 */
    pthread_join(h, NULL);

    check(w1.done, "foreign-readied frame ran on the foreground pool");
    check(w2.done, "worker-readied frame ran on the foreground pool");
    check(tf.done, "timer frame dispatched on the foreground pool");
    if (peak_thr >= 0) {
        printf("threads peak=%d (single pool expects 5)\n", peak_thr);
        check(peak_thr <= 6, "no second worker pool started");
    }
    if (!fails) printf("PASS\n");
    return fails;
}
