/* Regression for the `lock (obj)` runtime monitor (zan_monitor_enter/exit in
 * src/runtime/sync/rt_sync_thread.c): recursion, 2-8 threads contending on one
 * object, many distinct objects, address reuse after free, foreign exit, and
 * bounded block accounting. Also meant to run clean under -fsanitize=thread:
 *   clang -std=c11 -g -O1 -fsanitize=thread -I src/runtime tests/runtime/monitor_lock.c \
 *     src/runtime/rt_sync.c src/runtime/rt_file.c src/runtime/rt_timer.c -o monitor_lock */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "rt_sync.h"

#if defined(_WIN32)
#include <windows.h>
typedef HANDLE test_thread;
#define THREAD_RESULT DWORD WINAPI
#define THREAD_RETURN return 0
static void test_spawn(test_thread *t, LPTHREAD_START_ROUTINE fn, void *arg) {
    *t = CreateThread(NULL, 0, fn, arg, 0, NULL);
    assert(*t);
}
static void test_join(test_thread t) {
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}
static void test_sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <pthread.h>
#include <time.h>
#include <unistd.h>
typedef pthread_t test_thread;
#define THREAD_RESULT void *
#define THREAD_RETURN return NULL
static void test_spawn(test_thread *t, void *(*fn)(void *), void *arg) {
    assert(pthread_create(t, NULL, fn, arg) == 0);
}
static void test_join(test_thread t) { assert(pthread_join(t, NULL) == 0); }
static void test_sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#endif

/* Upper bound of recycled blocks: 256 buckets x per-bucket free-list cap. */
#define MAX_PARKED_BLOCKS (256 * 4)

static int64_t in_use_blocks(void) {
    int64_t used = -1, alloc = -1;
    zan_monitor_debug_stats(&used, &alloc);
    return used;
}

static void assert_quiescent(const char *phase) {
    int64_t used = -1, alloc = -1;
    zan_monitor_debug_stats(&used, &alloc);
    if (used != 0 || alloc < 0 || alloc > MAX_PARKED_BLOCKS) {
        fprintf(stderr, "[%s] leaked monitor blocks: in_use=%lld allocated=%lld\n",
                phase, (long long)used, (long long)alloc);
        abort();
    }
}

/* ---- recursion: re-entry by the owner never blocks, and the object stays
 * held until the outermost exit ---------------------------------------- */
static int g_rec_obj;
static int g_rec_acquired;

static THREAD_RESULT rec_contender(void *unused) {
    (void)unused;
    zan_monitor_enter(&g_rec_obj);
    __atomic_store_n(&g_rec_acquired, 1, __ATOMIC_SEQ_CST);
    zan_monitor_exit(&g_rec_obj);
    THREAD_RETURN;
}

static void test_recursion(void) {
    for (int i = 0; i < 5; ++i) zan_monitor_enter(&g_rec_obj);
    assert(in_use_blocks() == 1);

    test_thread t;
    test_spawn(&t, rec_contender, NULL);
    for (int depth = 5; depth > 1; --depth) {
        test_sleep_ms(20);
        assert(__atomic_load_n(&g_rec_acquired, __ATOMIC_SEQ_CST) == 0);
        zan_monitor_exit(&g_rec_obj);
    }
    test_sleep_ms(20);
    assert(__atomic_load_n(&g_rec_acquired, __ATOMIC_SEQ_CST) == 0);
    zan_monitor_exit(&g_rec_obj); /* outermost: contender may proceed */
    test_join(t);
    assert(g_rec_acquired == 1);
    assert_quiescent("recursion");
}

/* ---- exit by a non-owner (or with nothing held) is ignored ------------- */
static int g_foreign_obj;
static THREAD_RESULT foreign_exiter(void *unused) {
    (void)unused;
    zan_monitor_exit(&g_foreign_obj); /* must not release main's hold */
    THREAD_RETURN;
}

static void test_foreign_exit(void) {
    int stray;
    zan_monitor_exit(&stray); /* never entered */
    zan_monitor_exit(NULL);
    zan_monitor_enter(NULL);
    assert_quiescent("stray-exit");

    zan_monitor_enter(&g_foreign_obj);
    test_thread t;
    test_spawn(&t, foreign_exiter, NULL);
    test_join(t);
    assert(in_use_blocks() == 1); /* still held by this thread */
    zan_monitor_exit(&g_foreign_obj);
    assert_quiescent("foreign-exit");
}

/* ---- 2..8 threads contending on one object ----------------------------- */
typedef struct {
    void *obj;
    int iters;
    long long *counter; /* plain, protected only by the monitor */
    int *inside;
} contend_arg;

static THREAD_RESULT contend_worker(void *p) {
    contend_arg *a = (contend_arg *)p;
    for (int i = 0; i < a->iters; ++i) {
        zan_monitor_enter(a->obj);
        assert(__atomic_add_fetch(a->inside, 1, __ATOMIC_SEQ_CST) == 1);
        if (i & 1) { /* nested re-entry under contention */
            zan_monitor_enter(a->obj);
            (*a->counter)++;
            zan_monitor_exit(a->obj);
        } else {
            (*a->counter)++;
        }
        assert(__atomic_sub_fetch(a->inside, 1, __ATOMIC_SEQ_CST) == 0);
        zan_monitor_exit(a->obj);
    }
    THREAD_RETURN;
}

static void test_contention(int threads, int iters) {
    static int obj;
    long long counter = 0;
    int inside = 0;
    contend_arg arg = { &obj, iters, &counter, &inside };
    test_thread t[8];
    for (int i = 0; i < threads; ++i) test_spawn(&t[i], contend_worker, &arg);
    for (int i = 0; i < threads; ++i) test_join(t[i]);
    assert(counter == (long long)threads * iters);
    assert_quiescent("contention");
}

/* ---- many distinct objects: pairs locked in address order --------------- */
#define DISTINCT_OBJS 4096
typedef struct { long long count; char pad[56]; } slot_t;
static slot_t *g_slots;

typedef struct { unsigned seed; int iters; } distinct_arg;

static THREAD_RESULT distinct_worker(void *p) {
    distinct_arg *a = (distinct_arg *)p;
    unsigned s = a->seed;
    for (int i = 0; i < a->iters; ++i) {
        s = s * 1103515245u + 12345u;
        unsigned x = (s >> 8) % DISTINCT_OBJS;
        s = s * 1103515245u + 12345u;
        unsigned y = (s >> 8) % DISTINCT_OBJS;
        slot_t *lo = &g_slots[x < y ? x : y];
        slot_t *hi = &g_slots[x < y ? y : x];
        zan_monitor_enter(lo);
        zan_monitor_enter(hi); /* same object twice when x == y: recursion */
        lo->count++;
        hi->count++;
        zan_monitor_exit(hi);
        zan_monitor_exit(lo);
    }
    THREAD_RETURN;
}

static void test_distinct(void) {
    g_slots = (slot_t *)calloc(DISTINCT_OBJS, sizeof(slot_t));
    assert(g_slots);
    enum { T = 8, ITERS = 40000 };
    distinct_arg args[T];
    test_thread t[T];
    for (int i = 0; i < T; ++i) {
        args[i].seed = 0x9e3779b9u * (unsigned)(i + 1);
        args[i].iters = ITERS;
        test_spawn(&t[i], distinct_worker, &args[i]);
    }
    for (int i = 0; i < T; ++i) test_join(t[i]);
    long long total = 0;
    for (int i = 0; i < DISTINCT_OBJS; ++i) total += g_slots[i].count;
    assert(total == 2LL * T * ITERS);
    free(g_slots);
    assert_quiescent("distinct");
}

/* ---- short-lived objects and address reuse after free ------------------- */
static THREAD_RESULT churn_worker(void *unused) {
    (void)unused;
    for (int i = 0; i < 50000; ++i) {
        int *o = (int *)malloc(sizeof(int) * (1 + (i & 7)));
        assert(o);
        zan_monitor_enter(o);
        zan_monitor_enter(o);
        *o = i;
        zan_monitor_exit(o);
        zan_monitor_exit(o);
        free(o); /* address is likely handed straight back to the next malloc */
    }
    THREAD_RETURN;
}

static void *g_reuse_obj;
static int g_reuse_ok;
static THREAD_RESULT reuse_locker(void *unused) {
    (void)unused;
    zan_monitor_enter(g_reuse_obj); /* must not block on the dead object's monitor */
    __atomic_store_n(&g_reuse_ok, 1, __ATOMIC_SEQ_CST);
    zan_monitor_exit(g_reuse_obj);
    THREAD_RETURN;
}

static void test_reuse(void) {
    int reused = 0;
    for (int round = 0; round < 64; ++round) {
        void *a = malloc(48);
        assert(a);
        zan_monitor_enter(a);
        zan_monitor_exit(a);
        free(a);
        void *b = malloc(48);
        assert(b);
        if (b == a) reused++;
        g_reuse_obj = b;
        g_reuse_ok = 0;
        test_thread t;
        test_spawn(&t, reuse_locker, NULL);
        test_join(t);
        assert(g_reuse_ok == 1);
        free(b);
    }
    test_thread t[4];
    for (int i = 0; i < 4; ++i) test_spawn(&t[i], churn_worker, NULL);
    for (int i = 0; i < 4; ++i) test_join(t[i]);
    assert_quiescent("reuse");
    printf("monitor_lock: address reuse observed %d/64\n", reused);
}

/* ---- memory stays bounded across 200k distinct short-lived objects ------ */
static void test_bounded(void) {
    char *arena = (char *)malloc(200000 * 16);
    assert(arena);
    for (int i = 0; i < 200000; ++i) {
        zan_monitor_enter(arena + (size_t)i * 16);
        zan_monitor_exit(arena + (size_t)i * 16);
    }
    /* Holding many at once grows the table; releasing them must shrink it back. */
    for (int i = 0; i < 20000; ++i) zan_monitor_enter(arena + (size_t)i * 16);
    assert(in_use_blocks() == 20000);
    for (int i = 0; i < 20000; ++i) zan_monitor_exit(arena + (size_t)i * 16);
    free(arena);
    assert_quiescent("bounded");
}

int main(void) {
#if !defined(_WIN32)
    alarm(120); /* a lost wakeup must fail the case, not hang ctest */
#endif
    test_recursion();
    test_foreign_exit();
    test_contention(2, 200000);
    test_contention(4, 100000);
    test_contention(8, 50000);
    test_distinct();
    test_reuse();
    test_bounded();
    int64_t used = 0, alloc = 0;
    zan_monitor_debug_stats(&used, &alloc);
    printf("monitor_lock: ok (in_use=%lld, recycled=%lld)\n", (long long)used, (long long)alloc);
    return 0;
}
