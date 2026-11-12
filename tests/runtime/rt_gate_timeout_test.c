/* Deterministic Gate timer/park interleavings; no real clock or scheduler. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#endif
#define ZAN_CO_DRIVER 1
typedef void (*zan_co_step_t)(void *);
static void zan_co_ready(void *frame, zan_co_step_t step) {
    (void)step;
    (*(int *)frame)++;
}
static long long zan_timer_after_ctx(long long ms, void (*cb)(void *), void *ctx) {
    (void)ms; (void)cb; (void)ctx; return 1;
}
static int zan_timer_clear(long long id) { (void)id; return 1; }
static int live_allocations;
static void *test_calloc(size_t n, size_t size) {
    void *p = calloc(n, size);
    if (p) live_allocations++;
    return p;
}
static void test_free(void *p) {
    if (p) live_allocations--;
    free(p);
}
#define calloc test_calloc
#define free test_free
#include "../../src/runtime/io/rt_io_gate.c"
#undef calloc
#undef free

static int failures;
static void step(void *frame) { (void)frame; }
static void check(int condition, const char *name) {
    printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) failures++;
}
static void park_timed(long long h, long long ticket, int *wake) {
    zan_gate_park_ticket(h, ticket, wake, step);
}
static void interleaved_tickets(void) {
    int a = 0, b = 0;
    long long h = zan_gate_new();
    long long ta = zan_gate_arm(h), tb = zan_gate_arm(h);
    park_timed(h, ta, &a);
    park_timed(h, tb, &b);
    zan_gate_ticket_signal_and_release(h, ta);
    check(a == 1 && b == 0, "first ticket wakes only first waiter");
    zan_gate_ticket_signal_and_release(h, tb);
    check(a == 1 && b == 1, "both distinct tickets time out exactly once");
    zan_gate_free(h);
}
static void timeout_before_park(void) {
    int wake = 0;
    long long h = zan_gate_new(), t = zan_gate_arm(h);
    zan_gate_ticket_signal_and_release(h, t);
    park_timed(h, t, &wake);
    check(wake == 1, "timeout before park remains observable");
    zan_gate_free(h);
}
static void signal_before_park(void) {
    int a = 0, b = 0;
    long long h = zan_gate_new(), t = zan_gate_arm(h);
    zan_gate_signal(h);
    park_timed(h, t, &a);
    zan_gate_ticket_signal_and_release(h, t);
    zan_gate_park(h, &b, step);
    check(a == 1 && b == 0, "signal before park wakes once without ghost surplus");
    zan_gate_signal(h);
    check(b == 1, "real next signal remains countable");
    zan_gate_free(h);
}
static void timeout_does_not_consume_signal(void) {
    int a = 0, b = 0;
    long long h = zan_gate_new(), t = zan_gate_arm(h);
    zan_gate_ticket_signal_and_release(h, t);
    zan_gate_signal(h);
    park_timed(h, t, &a);
    zan_gate_park(h, &b, step);
    check(a == 1 && b == 1, "expired wait leaves subsequent signal for next waiter");
    zan_gate_free(h);
}
static void close_pending(void) {
    int a = 0, b = 0;
    long long h = zan_gate_new(), ta = zan_gate_arm(h), tb = zan_gate_arm(h);
    zan_gate_ticket_signal_and_release(h, ta);
    park_timed(h, tb, &b);
    zan_gate_free(h);
    check(a == 0 && b == 1, "Close wakes parked waiter with pending timer");
    zan_gate_ticket_signal_and_release(h, tb);
    check(live_allocations > 0, "early timeout registration pins closed gate until park");
    park_timed(h, ta, &a);
    check(a == 1 && b == 1 && live_allocations == 0,
          "late park after Close drains final registration and gate");
}
static void untimed_and_counting(void) {
    int a = 0, b = 0, c = 0;
    long long h = zan_gate_new();
    zan_gate_signal(h);
    zan_gate_signal_all(h);
    long long ta = zan_gate_prepare(h), tb = zan_gate_arm(h);
    park_timed(h, tb, &b);
    park_timed(h, ta, &a);
    zan_gate_ticket_signal_and_release(h, tb);
    zan_gate_park(h, &c, step);
    check(a == 1 && b == 1 && c == 0, "two banked signals consumed once by distinct waits");
    zan_gate_free(h);
    check(c == 1, "Close wakes original one-argument Park");
    h = zan_gate_new();
    ta = zan_gate_prepare(h);
    zan_gate_free(h);
    a = 0;
    park_timed(h, ta, &a);
    check(a == 1 && live_allocations == 0, "untimed registration also pins Close-before-park");
}
static void signal_all_pending(void) {
    int a = 0, b = 0, c = 0;
    long long h = zan_gate_new(), ta = zan_gate_arm(h), tb = zan_gate_arm(h);
    park_timed(h, tb, &b);
    zan_gate_signal_all(h);
    zan_gate_ticket_signal_and_release(h, ta);
    park_timed(h, ta, &a);
    zan_gate_ticket_signal_and_release(h, tb);
    zan_gate_park(h, &c, step);
    check(a == 1 && b == 1 && c == 0, "SignalAll includes pending waits without extra surplus");
    zan_gate_free(h);
}
int main(void) {
    interleaved_tickets();
    timeout_before_park();
    signal_before_park();
    timeout_does_not_consume_signal();
    close_pending();
    untimed_and_counting();
    signal_all_pending();
    check(live_allocations == 0, "all native gate allocations released");
    printf("failures: %d\n", failures);
    return failures ? 1 : 0;
}
