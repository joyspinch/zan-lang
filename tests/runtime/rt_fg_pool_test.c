/* 底层系统交互与数据协议契约 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "../../src/runtime/rt_co.h"

/* 底层系统交互与数据协议契约 */
void zan_co_delay(long long ms, void *frame, void (*step)(void *));

/* 底层系统交互与数据协议契约 */
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
    /* 底层系统交互与数据协议契约 */
    zan_co_ready(&w2, w2_step);
    f->done = 1;
}

static void t_step(void *fp) { pframe_t *f = fp; sample_thr(); f->done = 1; }

static void *helper(void *arg) {
    (void)arg;
    usleep(50 * 1000);
    /* 底层系统交互与数据协议契约 */
    zan_co_ready(&w1, w1_step);
    sample_thr();
    /* 底层系统交互与数据协议契约 */
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
    zan_co_sched_run();            /* 核心系统底层抽象与内存语义契约 */
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
