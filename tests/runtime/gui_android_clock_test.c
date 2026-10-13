/* Exercise the Android driver's actual clock with injected device uptime. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
/* Windows hosts run the same clock body using the OS's once/thread primitives.
   Android/POSIX hosts use pthread_once itself. */
typedef INIT_ONCE pthread_once_t;
#define PTHREAD_ONCE_INIT INIT_ONCE_STATIC_INIT
#define CLOCK_MONOTONIC 1
struct once_call { void (*fn)(void); };
static BOOL CALLBACK test_once(PINIT_ONCE once, PVOID arg, PVOID *context) {
    (void)once; (void)context;
    ((struct once_call *)arg)->fn();
    return TRUE;
}
static int pthread_once(pthread_once_t *once, void (*fn)(void)) {
    struct once_call call = { fn };
    return InitOnceExecuteOnce(once, test_once, &call, NULL) ? 0 : -1;
}
static SRWLOCK lock = SRWLOCK_INIT;
static CONDITION_VARIABLE changed = CONDITION_VARIABLE_INIT;
static void test_lock(void) { AcquireSRWLockExclusive(&lock); }
static void test_unlock(void) { ReleaseSRWLockExclusive(&lock); }
static void test_wait(void) { SleepConditionVariableSRW(&changed, &lock, INFINITE, 0); }
static void test_broadcast(void) { WakeAllConditionVariable(&changed); }
#define WORKER_RESULT DWORD WINAPI
#define WORKER_RETURN 0
#else
#include <pthread.h>
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static void test_lock(void) { pthread_mutex_lock(&lock); }
static void test_unlock(void) { pthread_mutex_unlock(&lock); }
static void test_wait(void) { pthread_cond_wait(&changed, &lock); }
static void test_broadcast(void) { pthread_cond_broadcast(&changed); }
#define WORKER_RESULT void *
#define WORKER_RETURN NULL
#endif

#define CHECK(cond, message) do { if (!(cond)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); exit(1); \
} } while (0)
#define WORKERS 16
static const int64_t device_ms = INT64_C(3000000000);
/* A fractional millisecond also checks subtraction across second boundaries. */
static int64_t device_ns = INT64_C(3000000000) * 1000000 + 999500000;
static unsigned clock_reads;
static int entered, started, first_sample, release_sample;

static int injected_clock_gettime(int clock_id, struct timespec *ts) {
    CHECK(clock_id == CLOCK_MONOTONIC, "driver must use the monotonic clock");
    test_lock();
    int64_t sample = device_ns;
    device_ns += 1000000;
    unsigned read = ++clock_reads;
    if (read == 1) {
        first_sample = 1;
        test_broadcast();
        /* Keep initialization in flight until every first reader has entered. */
        while (!release_sample) test_wait();
    }
    test_unlock();
    ts->tv_sec = (time_t)(sample / 1000000000);
    ts->tv_nsec = (long)(sample % 1000000000);
    return 0;
}

typedef int64_t i64;
#define ZAN_GUI_ANDROID_CLOCK_TEST
#define clock_gettime injected_clock_gettime
#include "../../src/runtime/gui_runtime_android_native.c"
#undef clock_gettime

static WORKER_RESULT first_reader(void *arg) {
    i64 *tick = (i64 *)arg;
    test_lock();
    while (!started) test_wait();
    ++entered;
    test_broadcast();
    test_unlock();
    *tick = ant_tick_ms();
    return WORKER_RETURN;
}

static void set_elapsed_ns(int64_t elapsed_ns) {
    test_lock();
    device_ns = device_ms * 1000000 + 999500000 + elapsed_ns;
    test_unlock();
}

int main(void) {
    i64 ticks[WORKERS];
#ifdef _WIN32
    HANDLE threads[WORKERS];
#else
    pthread_t threads[WORKERS];
#endif
    for (int i = 0; i < WORKERS; ++i) {
#ifdef _WIN32
        threads[i] = CreateThread(NULL, 0, first_reader, &ticks[i], 0, NULL);
        CHECK(threads[i] != NULL, "create first reader");
#else
        CHECK(pthread_create(&threads[i], NULL, first_reader, &ticks[i]) == 0,
              "create first reader");
#endif
    }
    test_lock();
    started = 1;
    test_broadcast();
    while (entered != WORKERS || !first_sample) test_wait();
    release_sample = 1;
    test_broadcast();
    test_unlock();
    unsigned seen = 0;
    for (int i = 0; i < WORKERS; ++i) {
#ifdef _WIN32
        CHECK(WaitForSingleObject(threads[i], INFINITE) == WAIT_OBJECT_0,
              "join first reader");
        CloseHandle(threads[i]);
#else
        CHECK(pthread_join(threads[i], NULL) == 0, "join first reader");
#endif
        CHECK(ticks[i] >= 1 && ticks[i] <= WORKERS,
              "concurrent first ticks must be small and nonnegative at high uptime");
        unsigned bit = 1u << (unsigned)(ticks[i] - 1);
        CHECK(!(seen & bit), "first samples must have unique elapsed timestamps");
        seen |= bit;
    }
    CHECK(clock_reads == WORKERS + 1, "concurrent initialization must sample one origin");
    printf("concurrent initialization: %d first ticks in [1,%d], one origin at >=%lld ms\n",
           WORKERS, WORKERS, (long long)device_ms);

    /* Window.GetTickMs is an int; App only considers positive deadlines. */
    set_elapsed_ns(INT64_C(100) * 1000000);
    int32_t now = (int32_t)ant_tick_ms();
    int32_t deadline = now + 16;
    CHECK(now == 100 && deadline > 0, "high device uptime must not make deadlines negative");
    set_elapsed_ns(INT64_C(115) * 1000000);
    CHECK((int32_t)ant_tick_ms() < deadline, "frame deadline must not fire early");
    set_elapsed_ns(INT64_C(116) * 1000000);
    CHECK((int32_t)ant_tick_ms() >= deadline, "first frame deadline must become due");
    puts("frame deadline: positive at 116 ms, due on first frame");

    set_elapsed_ns(INT64_C(2000) * 1000000);
    i64 gesture_down = ant_tick_ms();
    set_elapsed_ns(INT64_C(2080) * 1000000);
    i64 gesture_up = ant_tick_ms();
    CHECK(gesture_up - gesture_down == 80, "gesture velocity history must retain elapsed deltas");
    set_elapsed_ns(INT64_C(2096) * 1000000);
    i64 fling_next = ant_tick_ms();
    CHECK(fling_next - gesture_up == 16, "fling frame must retain elapsed deltas");
    puts("gesture/fling deltas: 80 ms / 16 ms");

    /* No reinitialization on idle/resume: a deadline keeps its original epoch. */
    int32_t pending = (int32_t)fling_next + 16;
    set_elapsed_ns(INT64_C(7202096) * 1000000);
    i64 resumed = ant_tick_ms();
    CHECK(resumed - fling_next == 7200000, "long idle must not reset the clock origin");
    CHECK(pending > 0 && (int32_t)resumed >= pending,
          "a pre-idle deadline must remain due after resume");
    set_elapsed_ns((INT64_C(2147483647) + 64000) * 1000000);
    CHECK(ant_tick_ms() == INT64_C(2147483647) + 64000,
          "native clock must preserve its existing i64 ABI");
    CHECK(clock_reads == WORKERS + 9, "origin must stay initialized across all later reads");
    puts("continuity: two-hour idle keeps origin; native result remains i64");
    return 0;
}
