/* Standalone regression: clang task_handle_registry.c -o
 * task_handle_registry.exe */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include "../../src/runtime/timer/rt_timer_task.c"

static long long profile_clock;
long long zan_co_precise_us(void) { return profile_clock; }

typedef struct {
  int done;
  int payload;
  int cancel;
} test_frame;
static int released;
static void release_frame(void *frame) {
  /* Re-entering the registry proves that callbacks execute outside its lock. */
  (void)zan_task_live_count();
  __atomic_add_fetch(&released, 1, __ATOMIC_RELAXED);
  free(frame);
}
static test_frame *make_frame(int payload) {
  test_frame *frame = (test_frame *)calloc(1, sizeof(*frame));
  assert(frame);
  frame->payload = payload;
  return frame;
}
#if defined(_WIN32)
#include <windows.h>
typedef HANDLE test_thread;
#define THREAD_RESULT DWORD WINAPI
#define THREAD_RETURN return 0
#else
#include <pthread.h>
typedef pthread_t test_thread;
#define THREAD_RESULT void *
#define THREAD_RETURN return NULL
#endif
static uint64_t shared_token;
static int stop_queries;
static int successful_pins;
static THREAD_RESULT query_worker(void *unused) {
  (void)unused;
  while (!__atomic_load_n(&stop_queries, __ATOMIC_ACQUIRE)) {
    uint64_t token = __atomic_load_n(&shared_token, __ATOMIC_ACQUIRE);
    (void)zan_task_isdone(token);
    zan_task_cancel(token);
    test_frame *frame = (test_frame *)zan_task_acquire(token, 0);
    if (frame) {
      assert(frame->payload == 12345);
      __atomic_add_fetch(&successful_pins, 1, __ATOMIC_RELAXED);
      zan_task_unpin(frame);
    }
  }
  THREAD_RETURN;
}
static void concurrent_queries(void) {
  test_thread threads[4];
  for (int i = 0; i < 4; ++i) {
#if defined(_WIN32)
    threads[i] = CreateThread(NULL, 0, query_worker, NULL, 0, NULL);
    assert(threads[i]);
#else
    assert(pthread_create(&threads[i], NULL, query_worker, NULL) == 0);
#endif
  }
  for (int i = 0; i < 50000; ++i) {
    test_frame *frame = make_frame(12345);
    zan_task_frame_config(frame, &frame->done, &frame->cancel);
    test_frame *child = make_frame(54321);
    zan_task_frame_config(child, &child->done, &child->cancel);
    zan_task_child(frame, child);
    uint64_t token = zan_task_register(frame, &frame->done);
    __atomic_store_n(&shared_token, token, __ATOMIC_RELEASE);
    if (i == 0) {
      while (!__atomic_load_n(&successful_pins, __ATOMIC_ACQUIRE)) {
      }
    }
    zan_task_cancel(token);
    assert(__atomic_load_n(&child->cancel, __ATOMIC_ACQUIRE) == 1);
    zan_task_child(frame, NULL);
    zan_task_retire(child, release_frame);
    child = make_frame(54321);
    zan_task_frame_config(child, &child->done, &child->cancel);
    zan_task_child(frame, child);
    assert(__atomic_load_n(&child->cancel, __ATOMIC_ACQUIRE) == 1);
    /* Retire clears incoming edges while cancellation threads race. */
    zan_task_retire(child, release_frame);
    __atomic_store_n(&frame->done, 1, __ATOMIC_RELEASE);
    zan_task_retire(frame, release_frame);
    assert(zan_task_isdone(token));
    assert(zan_task_acquire(token, 0) == NULL);
  }
  __atomic_store_n(&stop_queries, 1, __ATOMIC_RELEASE);
  for (int i = 0; i < 4; ++i) {
#if defined(_WIN32)
    assert(WaitForSingleObject(threads[i], 60000) == WAIT_OBJECT_0);
    CloseHandle(threads[i]);
#else
    assert(pthread_join(threads[i], NULL) == 0);
#endif
  }
  assert(successful_pins > 0);
  assert(zan_task_live_count() == 0);
  assert(zan_task_slot_capacity() <= TASK_SHARDS * 16);
  assert(released == 250001);
}
typedef struct {
  uint64_t marker;
  test_frame frame;
} allocator_frame;
static int allocator_released;
static void allocator_release(void *raw) {
  allocator_frame *allocation =
      (allocator_frame *)((char *)raw - offsetof(allocator_frame, frame));
  assert(allocation->marker == UINT64_C(0x51a110c));
  assert(zan_task_live_count() == 0);
  ++allocator_released;
  free(allocation);
}
static void allocator_provenance(void) {
  allocator_frame *allocation =
      (allocator_frame *)calloc(1, sizeof(*allocation));
  assert(allocation);
  allocation->marker = UINT64_C(0x51a110c);
  test_frame *frame = &allocation->frame;
  zan_task_frame_allocator(frame, allocator_release);
  zan_task_frame_config(frame, &frame->done, &frame->cancel);
  uint64_t token = zan_task_register(frame, &frame->done);
  test_frame *pin = (test_frame *)zan_task_acquire(token, 1);
  assert(pin == frame);
  int before = released;
  zan_task_retire(frame, release_frame);
  assert(allocator_released == 0 && released == before);
  zan_task_unpin(pin);
  assert(allocator_released == 1 && released == before);
  assert(zan_task_live_count() == 0 && zan_task_isdone(token));
}
static int discarded;
static void discard_frame(void *raw) {
  test_frame *frame = (test_frame *)raw;
  assert(frame->payload == 91);
  assert(zan_task_live_count() == 1);
  frame->payload = 0;
  ++discarded;
  zan_task_retire(frame, release_frame);
}
static void typed_discard(void) {
  int before = released;
  test_frame *frame = make_frame(91);
  zan_task_frame_config(frame, &frame->done, &frame->cancel);
  zan_task_discard_config(frame, discard_frame);
  uint64_t token = zan_task_register(frame, &frame->done);
  test_frame *observer = (test_frame *)zan_task_acquire(token, 0);
  assert(observer == frame);
  __atomic_store_n(&frame->done, 1, __ATOMIC_RELEASE);
  test_frame *consumer = (test_frame *)zan_task_acquire(token, 1);
  assert(consumer == frame);
  assert(zan_task_acquire(token, 1) == NULL);
  zan_task_discard(consumer);
  assert(discarded == 1 && consumer->payload == 0);
  assert(released == before && zan_task_live_count() == 1);
  assert(zan_task_isdone(token));
  zan_task_cancel(token);
  zan_task_unpin(consumer);
  assert(released == before && observer->payload == 0);
  zan_task_unpin(observer);
  assert(released == before + 1 && zan_task_live_count() == 0);
  assert(zan_task_acquire(token, 0) == NULL);
  assert(discarded == 1 && zan_task_slot_capacity() <= TASK_SHARDS * 16);
}
static void keep_frame(void *frame) {
  test_frame *child = (test_frame *)frame;
  child->cancel = 0;
}
static void registry_growth(void) {
  enum { frame_count = 1024 };
  test_frame *frames[frame_count];
  uint64_t tokens[frame_count];
  test_frame *child = make_frame(-1);
  zan_task_frame_config(child, &child->done, &child->cancel);
  for (int i = 0; i < frame_count; ++i) {
    frames[i] = make_frame(i);
    zan_task_frame_config(frames[i], &frames[i]->done, &frames[i]->cancel);
    tokens[i] = zan_task_register(frames[i], &frames[i]->done);
    zan_task_child(frames[i], child);
  }
  assert(zan_task_live_count() == frame_count + 1);
  for (int i = 0; i < frame_count; ++i) {
    assert(zan_task_acquire(tokens[i], 0) == frames[i]);
    zan_task_unpin(frames[i]);
  }
  zan_task_cancel(tokens[0]);
  assert(child->cancel == 1);
  /* Reuse both the raw frame address and the registry slot. */
  zan_task_retire(child, keep_frame);
  zan_task_frame_config(child, &child->done, &child->cancel);
  for (int i = frame_count - 1; i >= 0; --i) {
    zan_task_cancel(tokens[i]);
    zan_task_retire(frames[i], release_frame);
    assert(zan_task_acquire(tokens[i], 0) == NULL);
  }
  assert(child->cancel == 0);
  zan_task_retire(child, release_frame);
  assert(zan_task_live_count() == 0);
}
static test_frame *profile_child;
static test_frame *profile_late;
static test_frame *profile_unrelated;

static void profile_unrelated_step(void *raw) {
  assert(raw == profile_unrelated);
  zan_task_profile_alloc_add(4096);
}

static void profile_child_suspend(void *raw) {
  assert(raw == profile_child);
  zan_task_profile_alloc_add(64);
  zan_task_profile_wait(raw, "fixture.zan", 17, 9, "timer");
}

static void profile_child_complete(void *raw) {
  assert(raw == profile_child);
  zan_task_profile_alloc_add(128);
  zan_task_profile_checkpoint(raw);
  zan_task_profile_alloc_add(4096);
}

static THREAD_RESULT profile_resume_worker(void *frame) {
  zan_task_run_profiled(frame, profile_child_complete);
  THREAD_RETURN;
}

static void profile_request(void *raw) {
  assert(zan_task_profile_begin(3) == 1);
  int64_t id = zan_task_profile_stage_id();
  assert(id > 0);
  zan_task_profile_alloc_add(16);
  zan_task_profile_stage_set(id, 0, 1);
  profile_child = make_frame(0);
  profile_late = make_frame(0);
  zan_task_frame_config(profile_child, &profile_child->done,
                        &profile_child->cancel);
  zan_task_frame_config(profile_late, &profile_late->done,
                        &profile_late->cancel);
  zan_task_run_profiled(profile_unrelated, profile_unrelated_step);
  profile_clock = 100;
  zan_task_run_profiled(profile_child, profile_child_suspend);
  profile_clock = 125;
#if defined(_WIN32)
  test_thread thread =
      CreateThread(NULL, 0, profile_resume_worker, profile_child, 0, NULL);
  assert(thread && WaitForSingleObject(thread, 60000) == WAIT_OBJECT_0);
  CloseHandle(thread);
#else
  test_thread thread;
  assert(pthread_create(&thread, NULL, profile_resume_worker, profile_child) ==
         0);
  assert(pthread_join(thread, NULL) == 0);
#endif
  zan_task_profile_stage_set(id, 1, 1);
  zan_task_profile_stage_set(id + 1, 1, 0);
  zan_task_profile_alloc_add(8);
  zan_task_profile_stage_set(id, 1, 0);
  zan_task_profile_alloc_add(24);
  zan_task_profile_stage_set(id, 0, 0);
  int64_t cpu, bytes, slices, unfinished;
  void *report = zan_task_profile_end(&cpu, &bytes, &slices, &unfinished);
  assert(report && bytes == 240 && slices == 9 && unfinished == 1);
  int64_t stage_cpu, stage_bytes;
  int64_t accounted_cpu = 0;
  const int64_t expected_stage_bytes[] = {16, 216, 8};
  for (int slot = -1; slot < 2; slot++) {
    assert(zan_task_profile_read_stage(report, slot, &stage_cpu, &stage_bytes));
    assert(stage_bytes == expected_stage_bytes[slot + 1]);
    if (cpu >= 0) {
      assert(stage_cpu >= 0);
      accounted_cpu += stage_cpu;
    }
  }
  assert(cpu < 0 || accounted_cpu == cpu);
  assert(!zan_task_profile_read_stage(report, 2, &stage_cpu, &stage_bytes));
  assert(zan_task_profile_end(&cpu, &bytes, &slices, &unfinished) == NULL);
  const char *file, *kind;
  int32_t line, column;
  int64_t start, elapsed;
  assert(zan_task_profile_read_wait(report, 0, &file, &line, &column, &kind,
                                    &start, &elapsed));
  assert(strcmp(file, "fixture.zan") == 0 && strcmp(kind, "timer") == 0);
  assert(line == 17 && column == 9 && start == 100 && elapsed == 25);
  assert(!zan_task_profile_read_wait(report, 1, &file, &line, &column, &kind,
                                     &start, &elapsed));
  assert(zan_task_profile_omitted_waits(report) == 0);
  int64_t tid, slice_cpu, slice_bytes;
#if defined(_WIN32)
  static const int64_t expected_bytes[] = {16, 0, 0, 64, 128, 0, 8, 24, 0};
  DWORD root_tid = GetCurrentThreadId();
  for (int i = 0; i < 9; i++) {
    assert(zan_task_profile_read_execution(report, i, &tid, &start, &elapsed,
                                           &slice_cpu, &slice_bytes));
    assert(tid > 0 && (i == 4 ? tid != root_tid : tid == root_tid));
    assert(elapsed >= 0 && slice_bytes == expected_bytes[i]);
  }
#endif
  assert(!zan_task_profile_read_execution(report, (int)slices, &tid, &start,
                                          &elapsed, &slice_cpu, &slice_bytes));
  assert(zan_task_profile_omitted_execution(report) == 0);
  zan_task_profile_release_report(report);
  zan_task_profile_checkpoint(raw);
}

static void request_profile(void) {
  test_frame *root = make_frame(0);
  profile_unrelated = make_frame(0);
  zan_task_frame_config(root, &root->done, &root->cancel);
  zan_task_frame_config(profile_unrelated, &profile_unrelated->done,
                        &profile_unrelated->cancel);
  zan_task_run_profiled(profile_unrelated, profile_unrelated_step);
  assert(g_thread_alloc_bytes == 0);
  zan_task_profile_enable();
  zan_task_run_profiled(root, profile_request);
  assert(g_profile_count == 0);
  zan_task_retire(profile_late, release_frame);
  zan_task_retire(profile_child, release_frame);
  zan_task_retire(profile_unrelated, release_frame);
  zan_task_retire(root, release_frame);
  assert(zan_task_live_count() == 0);
}

/* Indexed lookups (open addressing + free list) and incarnation-checked edges:
 * growth across several doublings, deletion inside probe runs, edges to
 * retired children going stale, two parents naming one child, and slot reuse
 * with no further growth. */
static void noop_release(void *frame) { (void)frame; }
static void reset_cancels(test_frame *frames, int n) {
  for (int i = 0; i < n; ++i)
    frames[i].cancel = 0;
}
static void indexed_edges_and_reuse(void) {
  enum { N = 600 };
  static test_frame frames[N];
  for (int i = 0; i < N; ++i)
    zan_task_frame_config(&frames[i], &frames[i].done, &frames[i].cancel);
  assert(zan_task_live_count() == N);
  for (int i = 0; i + 1 < N; ++i)
    zan_task_child(&frames[i], &frames[i + 1]);
  zan_task_frame_cancel(&frames[0]);
  assert(frames[N - 1].cancel == 1);
  for (int i = 1; i < N; i += 2)
    zan_task_retire(&frames[i], noop_release);
  assert(zan_task_live_count() == N / 2);
  for (int i = 0; i < N; i += 2)
    zan_task_frame_config(&frames[i], NULL, NULL);
  assert(zan_task_live_count() == N / 2);
  reset_cancels(frames, N);
  zan_task_frame_cancel(&frames[0]);
  assert(frames[0].cancel == 1 && frames[2].cancel == 0);
  zan_task_child(&frames[2], &frames[4]);
  zan_task_child(&frames[6], &frames[4]);
  zan_task_retire(&frames[4], noop_release);
  reset_cancels(frames, N);
  zan_task_frame_cancel(&frames[2]);
  zan_task_frame_cancel(&frames[6]);
  assert(frames[2].cancel == 1 && frames[6].cancel == 1 &&
         frames[4].cancel == 0);
  for (int i = 0; i < N; i += 2)
    if (i != 4)
      zan_task_retire(&frames[i], noop_release);
  assert(zan_task_live_count() == 0);
  size_t cap = zan_task_slot_capacity();
  for (int i = 0; i < N; ++i)
    zan_task_frame_config(&frames[i], &frames[i].done, &frames[i].cancel);
  assert(zan_task_live_count() == N && zan_task_slot_capacity() == cap);
  for (int i = 0; i < N; ++i)
    zan_task_retire(&frames[i], noop_release);
  assert(zan_task_live_count() == 0);
}
/* Scheduler threads hit different shards at once: each thread builds
 * parent -> child chains whose frames hash to arbitrary shards, cancels some,
 * re-awaits, and retires, while every check stays per-thread exact. */
static THREAD_RESULT churn_worker(void *unused) {
  (void)unused;
  for (int i = 0; i < 40000; ++i) {
    test_frame *parent = make_frame(1);
    test_frame *child = make_frame(2);
    test_frame *grand = make_frame(3);
    zan_task_frame_config(parent, &parent->done, &parent->cancel);
    zan_task_frame_config(child, &child->done, &child->cancel);
    zan_task_frame_config(grand, &grand->done, &grand->cancel);
    zan_task_child(parent, child);
    zan_task_child(child, grand);
    if (i & 1) {
      zan_task_frame_cancel(parent);
      assert(child->cancel == 1 && grand->cancel == 1);
    } else {
      assert(child->cancel == 0 && grand->cancel == 0);
    }
    zan_task_child(child, NULL);
    zan_task_retire(grand, release_frame);
    zan_task_child(parent, NULL);
    zan_task_retire(child, release_frame);
    zan_task_retire(parent, release_frame);
  }
  THREAD_RETURN;
}
static void parallel_churn(void) {
  int before = __atomic_load_n(&released, __ATOMIC_ACQUIRE);
  test_thread threads[6];
  for (int i = 0; i < 6; ++i) {
#if defined(_WIN32)
    threads[i] = CreateThread(NULL, 0, churn_worker, NULL, 0, NULL);
    assert(threads[i]);
#else
    assert(pthread_create(&threads[i], NULL, churn_worker, NULL) == 0);
#endif
  }
  for (int i = 0; i < 6; ++i) {
#if defined(_WIN32)
    assert(WaitForSingleObject(threads[i], 60000) == WAIT_OBJECT_0);
    CloseHandle(threads[i]);
#else
    assert(pthread_join(threads[i], NULL) == 0);
#endif
  }
  assert(zan_task_live_count() == 0);
  assert(released == before + 6 * 40000 * 3);
}
int main(void) {
  test_frame *frame = make_frame(7);
  uint64_t first = zan_task_register(frame, &frame->done);
  assert(first > 0 && first <= INT64_MAX);
  assert(!zan_task_isdone(first));
  test_frame *pin = (test_frame *)zan_task_acquire(first, 0);
  assert(pin == frame);
  zan_task_retire(frame, release_frame);
  assert(zan_task_isdone(first));
  assert(!zan_task_acquire(first, 0));
  assert(released == 0 && pin->payload == 7);
  zan_task_unpin(pin);
  assert(released == 1 && zan_task_live_count() == 0);
  for (int i = 0; i < 100000; ++i) {
    frame = make_frame(i);
    uint64_t token = zan_task_register(frame, &frame->done);
    assert(token > first && !zan_task_isdone(token));
    assert(zan_task_isdone(first) && !zan_task_acquire(first, 0));
    frame->done = 1;
    assert(zan_task_isdone(token));
    pin = (test_frame *)zan_task_acquire(token, 1);
    assert(pin == frame && pin->payload == i);
    assert(!zan_task_acquire(token, 1));
    zan_task_retire(frame, release_frame);
    assert(zan_task_live_count() == 1);
    zan_task_unpin(pin);
    assert(zan_task_live_count() == 0);
  }
  assert(zan_task_slot_capacity() <= TASK_SHARDS * 16);
  assert(released == 100001);
  concurrent_queries();
  typed_discard();
  allocator_provenance();
  registry_growth();
  request_profile();
  indexed_edges_and_reuse();
  parallel_churn();
  puts("task registry: lifetime, consume, stale generations, bounded slots, "
       "four query threads, typed discard, allocator provenance, index growth, "
       "request profile, indexed edges and reuse, parallel sharded churn passed");
  return 0;
}
