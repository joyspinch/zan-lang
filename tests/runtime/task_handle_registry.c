/* Standalone regression: clang task_handle_registry.c -o task_handle_registry.exe */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include "../../src/runtime/timer/rt_timer_task.c"

typedef struct { int done; int payload; int cancel; } test_frame;
static int released;
static void release_frame(void *frame) {
    /* Re-entering the registry proves that callbacks execute outside its lock. */
    (void)zan_task_live_count();
    __atomic_add_fetch(&released, 1, __ATOMIC_RELAXED);
    free(frame);
}
static test_frame *make_frame(int payload) {
    test_frame *frame = (test_frame *)calloc(1, sizeof(*frame));
    assert(frame); frame->payload = payload; return frame;
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
            while (!__atomic_load_n(&successful_pins, __ATOMIC_ACQUIRE)) { }
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
    assert(zan_task_slot_capacity() == 16);
    assert(released == 250001);
}
typedef struct { uint64_t marker; test_frame frame; } allocator_frame;
static int allocator_released;
static void allocator_release(void *raw) {
    allocator_frame *allocation = (allocator_frame *)((char *)raw - offsetof(allocator_frame, frame));
    assert(allocation->marker == UINT64_C(0x51a110c));
    assert(zan_task_live_count() == 0);
    ++allocator_released;
    free(allocation);
}
static void allocator_provenance(void) {
    allocator_frame *allocation = (allocator_frame *)calloc(1, sizeof(*allocation));
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
    assert(discarded == 1 && zan_task_slot_capacity() == 16);
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
    assert(zan_task_slot_capacity() == 16);
    assert(released == 100001);
    concurrent_queries();
    typed_discard();
    allocator_provenance();
    puts("task registry: lifetime, consume, stale generations, bounded slots, four query threads, typed discard, allocator provenance passed");
    return 0;
}
