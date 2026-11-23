/* Gate ticket interleavings and native-thread contention without ZAN_CO_DRIVER.
 */
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

typedef void (*zan_co_step_t)(void *);
static void zan_co_ready(void *frame, zan_co_step_t step) { step(frame); }
static atomic_int live_allocations;
static void *test_calloc(size_t n, size_t size) {
  void *p = calloc(n, size);
  if (p)
    atomic_fetch_add_explicit(&live_allocations, 1, memory_order_relaxed);
  return p;
}
static void test_free(void *p) {
  if (p)
    atomic_fetch_sub_explicit(&live_allocations, 1, memory_order_relaxed);
  free(p);
}
#define calloc test_calloc
#define free test_free
#include "../../src/runtime/io/rt_io_gate.c"
#undef calloc
#undef free

static int failures;
static void step(void *frame) { (*(int *)frame)++; }
static void check(int condition, const char *name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition)
    failures++;
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
  check(a == 1 && b == 0,
        "signal before park wakes once without ghost surplus");
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
  check(a == 1 && b == 1,
        "expired wait leaves subsequent signal for next waiter");
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
  check(live_allocations > 0,
        "early timeout registration pins closed gate until park");
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
  check(a == 1 && b == 1 && c == 0,
        "two banked signals consumed once by distinct waits");
  zan_gate_free(h);
  check(c == 1, "Close wakes original one-argument Park");
  h = zan_gate_new();
  ta = zan_gate_prepare(h);
  zan_gate_free(h);
  a = 0;
  park_timed(h, ta, &a);
  check(a == 1 && live_allocations == 0,
        "untimed registration also pins Close-before-park");
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
  check(a == 1 && b == 1 && c == 0,
        "SignalAll includes pending waits without extra surplus");
  zan_gate_free(h);
}
enum { NATIVE_THREADS = 4, NATIVE_WAITS = 2048 };

typedef struct {
  long long gate;
  atomic_int arrived;
  atomic_int start;
} native_gate_run;

typedef struct {
  native_gate_run *run;
  atomic_int wakes[NATIVE_WAITS];
} native_gate_worker;

static void count_native_wake(void *frame) {
  atomic_fetch_add_explicit((atomic_int *)frame, 1, memory_order_relaxed);
}

static void yield_thread(void) {
#ifdef _WIN32
  Sleep(0);
#else
  sched_yield();
#endif
}

#ifdef _WIN32
static DWORD WINAPI native_worker(void *arg) {
#else
static void *native_worker(void *arg) {
#endif
  native_gate_worker *worker = arg;
  native_gate_run *run = worker->run;
  atomic_fetch_add_explicit(&run->arrived, 1, memory_order_relaxed);
  while (!atomic_load_explicit(&run->start, memory_order_acquire))
    yield_thread();
  for (int i = 0; i < NATIVE_WAITS; i++) {
    long long ticket = zan_gate_prepare(run->gate);
    if (!ticket)
      abort();
    zan_gate_park_ticket(run->gate, ticket, &worker->wakes[i],
                         count_native_wake);
    zan_gate_signal(run->gate);
  }
  return 0;
}

static void native_thread_contention(void) {
  native_gate_run run;
  run.gate = zan_gate_new();
  if (!run.gate)
    abort();
  atomic_init(&run.arrived, 0);
  atomic_init(&run.start, 0);
  native_gate_worker workers[NATIVE_THREADS];
#ifdef _WIN32
  HANDLE threads[NATIVE_THREADS];
#else
  pthread_t threads[NATIVE_THREADS];
#endif
  for (int i = 0; i < NATIVE_THREADS; i++) {
    workers[i].run = &run;
    for (int j = 0; j < NATIVE_WAITS; j++)
      atomic_init(&workers[i].wakes[j], 0);
#ifdef _WIN32
    threads[i] = CreateThread(NULL, 0, native_worker, &workers[i], 0, NULL);
    if (!threads[i])
      abort();
#else
    if (pthread_create(&threads[i], NULL, native_worker, &workers[i]) != 0)
      abort();
#endif
  }
  while (atomic_load_explicit(&run.arrived, memory_order_relaxed) !=
         NATIVE_THREADS)
    yield_thread();
  atomic_store_explicit(&run.start, 1, memory_order_release);

  int exact_wakes = 0;
  for (int i = 0; i < NATIVE_THREADS; i++) {
#ifdef _WIN32
    if (WaitForSingleObject(threads[i], INFINITE) != WAIT_OBJECT_0)
      abort();
    CloseHandle(threads[i]);
#else
    if (pthread_join(threads[i], NULL) != 0)
      abort();
#endif
  }
  for (int i = 0; i < NATIVE_THREADS; i++) {
    for (int j = 0; j < NATIVE_WAITS; j++) {
      if (atomic_load_explicit(&workers[i].wakes[j], memory_order_relaxed) == 1)
        exact_wakes++;
    }
  }
  check(exact_wakes == NATIVE_THREADS * NATIVE_WAITS,
        "native threads wake every prepared ticket exactly once before close");
  zan_gate_free(run.gate);
}

int main(void) {
  interleaved_tickets();
  timeout_before_park();
  signal_before_park();
  timeout_does_not_consume_signal();
  close_pending();
  untimed_and_counting();
  signal_all_pending();
  native_thread_contention();
  check(live_allocations == 0, "all native gate allocations released");
  printf("failures: %d\n", failures);
  return failures ? 1 : 0;
}
