/* Deterministic blocking dispatch faults with a synchronous fake worker. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#include <netinet/in.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#else
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/runtime/rt_io.h"

static int fail_alloc, fail_start, native_calls;
static int live_allocations;
static void *test_calloc(size_t size) {
  if (fail_alloc)
    return NULL;
  void *p = calloc(1, size);
  if (p)
    live_allocations++;
  return p;
}
static void test_free(void *p) {
  if (p)
    live_allocations--;
  free(p);
}

#ifdef _WIN32
static HANDLE g_iocp;
static volatile LONG g_blocking_wake_lost;
static BOOL test_queue_work(LPTHREAD_START_ROUTINE run, void *arg,
                            ULONG flags) {
  assert(flags == WT_EXECUTELONGFUNCTION);
  if (fail_start)
    return FALSE;
  run(arg);
  return TRUE;
}
#define QueueUserWorkItem test_queue_work
#else
typedef long LONG;
static LONG InterlockedIncrement(volatile LONG *count) { return ++*count; }
static LONG InterlockedDecrement(volatile LONG *count) { return --*count; }
static int g_dns_wake_fd = -1;
static int test_create_thread(pthread_t *id, const pthread_attr_t *attr,
                              void *(*run)(void *), void *arg) {
  (void)attr;
  if (fail_start)
    return 1;
  *id = pthread_self();
  run(arg);
  return 0;
}
static int test_detach(pthread_t id) {
  (void)id;
  return 0;
}
#define pthread_create test_create_thread
#define pthread_detach test_detach
#endif

static volatile LONG g_blocking_inflight;
static void dns_wake_notify(void);
static void io_wake(void *frame, zan_co_step_t step) { step(frame); }
void zan_io_init(void) {}
int32_t zan_io_resolve_ipv4(const char *name) {
  (void)name;
  return 123;
}
int32_t zan_io_resolve_sa(const char *name, int32_t port, void *buf,
                          int32_t cap) {
  (void)name;
  (void)port;
  memset(buf, 0xaa, (size_t)cap);
  return cap;
}
#define ZAN_MAYBE_UNUSED
#define ZAN_BLOCKING_JOB_CALLOC test_calloc
#define free test_free
#include "../../src/runtime/io/rt_io_dns.c"
#undef ZAN_BLOCKING_JOB_CALLOC
#undef free

static int64_t native_result(int64_t value) {
  native_calls++;
  return value;
}
static void native_void(void) { native_calls++; }
static void resume(void *wake_count) { (*(int *)wake_count)++; }

static void expect_native(enum zan_blocking_status expected, int is_void,
                          int64_t value) {
  int wake_count = 0;
  int32_t status = -1;
  int64_t result = INT64_MAX;
  int previous_calls = native_calls;
  void *fn = is_void ? (void *)native_void : (void *)native_result;
  zan_rt_blocking_checked_co(fn, is_void ? 0 : 1, value, 0, 0, 0, &wake_count,
                             resume, is_void ? NULL : &result, &status);
  dns_drain();
  assert(status == expected && wake_count == 1);
  assert(result ==
         (expected == ZAN_BLOCKING_OK && !is_void ? value : INT64_MAX));
  assert(native_calls == previous_calls + (expected == ZAN_BLOCKING_OK));
  assert(!g_blocking_inflight && !live_allocations);
}

static void expect_dns_failure(void) {
  int wake_count = 0;
  int32_t ipv4 = -1, length = -1;
  unsigned char addr[28] = {0};
  zan_io_resolve_co("example", &wake_count, resume, &ipv4);
  zan_io_resolve_sa_co("example", 80, addr, sizeof(addr), &wake_count, resume,
                       &length);
  dns_drain();
  assert(wake_count == 2 && ipv4 == 0 && length == 0);
  for (size_t i = 0; i < sizeof(addr); i++)
    assert(addr[i] == 0);
  assert(!g_blocking_inflight && !live_allocations);
}

int main(void) {
  fail_alloc = 1;
  expect_native(ZAN_BLOCKING_NO_MEMORY, 0, 0);
  expect_native(ZAN_BLOCKING_NO_MEMORY, 1, 0);
  expect_dns_failure();
  fail_alloc = 0;

  g_blocking_active = ZAN_BLOCKING_MAX_ACTIVE;
  g_blocking_queued_n = ZAN_BLOCKING_MAX_QUEUED;
  expect_native(ZAN_BLOCKING_QUEUE_FULL, 0, 0);
  expect_native(ZAN_BLOCKING_QUEUE_FULL, 1, 0);
  expect_dns_failure();
  g_blocking_active = g_blocking_queued_n = 0;

  fail_start = 1;
  expect_native(ZAN_BLOCKING_THREAD_FAILED, 0, 0);
  expect_native(ZAN_BLOCKING_THREAD_FAILED, 1, 0);
  expect_dns_failure();
  assert(!g_blocking_active);
  fail_start = 0;

  expect_native(ZAN_BLOCKING_OK, 0, 0);
  expect_native(ZAN_BLOCKING_OK, 0, -1);
  expect_native(ZAN_BLOCKING_OK, 0, INT64_MIN);
  expect_native(ZAN_BLOCKING_OK, 1, 0);
  puts("blocking dispatch passed");
  return 0;
}
