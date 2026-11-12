/* Linux scheduler liveness at the exact reactor-to-ready handoff. */
#define _GNU_SOURCE
#define ZAN_CO_DRIVER
#define ZAN_IO_STACKLESS_ONLY
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../src/runtime/io/rt_io_posix_compat.c"

#include "rt_io_owner_checks.h"
static void owner_count_release(volatile LONG *counter);
static void observe_counter(volatile LONG *counter);
static LONG handoff_dec(volatile LONG *counter) {
    LONG result = __sync_sub_and_fetch(counter, 1);
    observe_counter(counter);
    owner_count_release(counter);
    return result;
}
static LONG handoff_add(volatile LONG *counter, LONG delta) {
    LONG result = __sync_fetch_and_add(counter, delta);
    if (delta < 0) observe_counter(counter);
    return result;
}
#undef InterlockedDecrement
#undef InterlockedExchangeAdd
#define InterlockedDecrement handoff_dec
#define InterlockedExchangeAdd handoff_add
#include "../../src/runtime/rt_io.c"

static int failures, active, observations, resumed;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static void *observe_idle(void *arg) {
    *(int *)arg = co_all_idle();
    return NULL;
}
static void observe_counter(volatile LONG *counter) {
    if (!active || (counter != &g_io_count && counter != &g_io_dead_count)) return;
    int idle = -1;
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, observe_idle, &idle) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(!idle);
    observations++;
}
#define ZAN_OWNER_CHECKS_IMPLEMENTATION
#include "rt_io_owner_checks.h"
static void resume(void *frame) { (void)frame; resumed++; }
static void drain(int count) {
    CHECK(co_cnt_outstanding() == count);
    for (int i = 0; i < count; i++) {
        zan_co_task task;
        int found = co_next_task(&g_wk[0], &task);
        CHECK(found && task.step == resume);
        if (found) co_run(&g_wk[0], &task);
    }
    CHECK(co_cnt_outstanding() == 0 && co_all_idle());
}
static void run_case(int count, int close_waiter) {
    int pairs[4][2];
    char buffers[4][8];
    int64_t output[4] = {0};
    zan_co_fhdr frames[4] = {0};
    for (int i = 0; i < count; i++) {
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pairs[i]) == 0);
        zan_io_set_nonblocking(pairs[i][0]);
        zan_io_recv_co(pairs[i][0], buffers[i], sizeof(buffers[i]), &frames[i], resume, &output[i]);
        if (!close_waiter) CHECK(write(pairs[i][1], "x", 1) == 1);
    }
    CHECK(g_io_count == count);
    active = 1;
    if (close_waiter) {
        for (int i = 0; i < count; i++) zan_io_close_notify(pairs[i][0]);
    } else {
        CHECK(zan_io_poll(1000) == count);
    }
    active = 0;
    CHECK(g_io_count == 0);
    drain(count);
    for (int i = 0; i < count; i++) { close(pairs[i][0]); close(pairs[i][1]); }
}
static void run_failed_registration(void) {
    zan_co_fhdr frame = {0};
    int fd = open("/dev/null", O_RDONLY);
    CHECK(fd >= 0);
    active = 1;
    /* epoll rejects regular files with EPERM: this must transfer pending IO
     * ownership to the dead queue before releasing the original owner. */
    io_register(fd, ZAN_IO_READ, &frame, resume);
    CHECK(g_io_count == 0 && g_io_dead_count == 1 && !co_all_idle());
    CHECK(zan_io_poll(0) == 1);
    active = 0;
    CHECK(g_io_dead_count == 0);
    drain(1);
    close(fd);
}
int main(void) {
    setenv("ZAN_CO_WORKERS", "1", 1);
    zan_co_sched_init();
    g_co_pool_fg = 1; /* Explicitly drain stack-backed frames in this test. */
    zan_io_init();
    run_case(1, 0);
    run_case(4, 0);
    run_case(4, 1);
    run_failed_registration();
    CHECK(observations >= 8 && resumed == 10);
    run_owner_checks();
    zan_io_shutdown();
    if (!failures) puts("PASS Linux reactor handoff, batch accounting and close notification");
    return failures != 0;
}
