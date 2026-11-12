/* Exercise the generated production release_dyn at an exact recursion depth.
 * Descriptors and headers use the public compiler/runtime ABI. No depth global
 * is patched: 100 live destructor calls establish the threshold naturally. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "zan_abi.h"
#ifdef _WIN32
__declspec(dllimport) void *__stdcall CreateThread(void *, uintptr_t,
    unsigned long (__stdcall *)(void *), void *, unsigned long, unsigned long *);
__declspec(dllimport) unsigned long __stdcall WaitForSingleObject(void *, unsigned long);
__declspec(dllimport) int __stdcall CloseHandle(void *);
__declspec(dllimport) void __stdcall Sleep(unsigned long);
#else
#include <pthread.h>
#include <sched.h>
#endif
#include <stdatomic.h>

extern void zan_rt_release_dyn(void *);
extern int zan_rt_defer_pending(void);
extern void zan_rt_defer_drain(void);
typedef struct Object { uint64_t rc, descriptor; int remaining; } Object;
static void (*descriptor)(void *);
static atomic_int phase;
static int mode, stack_released, heap_released, failures;
static Object heap_leaf;

static void require(int value, const char *message) {
    if (!value) {
        fprintf(stderr, "FAIL %s\n", message);
        failures++;
    }
}
static void yield_thread(void) {
#ifdef _WIN32
    Sleep(0);
#else
    sched_yield();
#endif
}
static void wait_phase(int value) {
    while (atomic_load_explicit(&phase, memory_order_acquire) < value)
        yield_thread();
}
static Object object(int remaining, int stack) {
    Object value = {1, (uintptr_t)&descriptor |
        (stack ? ZAN_OBJ_STACK_FLAG : 0), remaining};
    return value;
}
static void destructor(void *payload) {
    Object *self = (Object *)((char *)payload - ZAN_OBJ_HDR_SIZE);
    if (self->remaining > 0) {
        Object child = object(self->remaining - 1, 1);
        zan_rt_release_dyn(&child.remaining);
    } else if (self->remaining == -1) {
        stack_released++;
        /* Stack destructors may themselves release heap objects. Those must
         * retain the depth bound and drain only after the outermost return. */
        zan_rt_release_dyn(&heap_leaf.remaining);
    } else if (self->remaining == -2) {
        heap_released++;
    } else if (mode == 0) {
        Object local = object(-1, 1);
        zan_rt_release_dyn(&local.remaining);
        require(stack_released == 1, "stack destructor delayed past caller frame");
        require(heap_released == 0, "nested heap release lost depth bound");
        require(zan_rt_defer_pending(), "nested heap release was not deferred");
        /* On failure stop before local storage expires into the real queue. */
        if (failures) { fflush(NULL); _Exit(7); }
    } else {
        atomic_store_explicit(&phase, 1, memory_order_release);
        wait_phase(2);
    }
}
#ifdef _WIN32
static unsigned long __stdcall shallow_thread(void *unused) {
#else
static void *shallow_thread(void *unused) {
#endif
    (void)unused;
    wait_phase(1);
    Object leaf = object(-2, 0);
    zan_rt_release_dyn(&leaf.remaining);
    /* A second thread's shallow call must run synchronously even while the
     * first thread has 100 live destructor frames. It must not inherit 100. */
    require(heap_released == 1, "release depth leaked across threads");
    if (failures) { fflush(NULL); _Exit(9); }
    atomic_store_explicit(&phase, 2, memory_order_release);
    return 0;
}
int ArcDeferredStackTest(void) {
    descriptor = destructor;
    heap_leaf = object(-2, 0);
    Object root = object(99, 0);
#ifndef ARC_DEPTH_ONLY
    zan_rt_release_dyn(&root.remaining);
    require(heap_released == 1, "nested heap queue did not drain at root return");
    require(!zan_rt_defer_pending(), "root returned with deferred heap work");
    if (failures) return 7;
    puts("PASS stack synchronous and nested heap bounded");
#endif

    mode = 1;
    heap_released = 0;
    atomic_init(&phase, 0);
#ifdef _WIN32
    void *thread = CreateThread(NULL, 0, shallow_thread, NULL, 0, NULL);
    if (!thread) return 8;
#else
    pthread_t thread;
    if (pthread_create(&thread, NULL, shallow_thread, NULL)) return 8;
#endif
    zan_rt_release_dyn(&root.remaining);
#ifdef _WIN32
    require(WaitForSingleObject(thread, 10000) == 0, "thread join failed");
    CloseHandle(thread);
#else
    require(pthread_join(thread, NULL) == 0, "thread join failed");
#endif
    zan_rt_defer_drain();
    require(!zan_rt_defer_pending(), "threaded release left pending work");
    if (failures) return 9;
    puts("PASS release depth isolated between threads");
    return 0;
}
