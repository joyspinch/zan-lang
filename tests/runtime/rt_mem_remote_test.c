/* rt_mem_remote_test.c -- cross-thread frees go back to the owning cache.
 *
 * A server allocates on one thread and frees on another (the worker that
 * parses a request is rarely the one that finishes the response). When a
 * foreign free pushed the block onto the *freeing* thread's list instead of
 * the owner's, every block drifted one way: the allocating thread found its
 * class lists empty and carved fresh slab space forever while the freeing
 * thread hoarded blocks it would never reuse. zan_mem_slabs() makes that
 * visible -- with owner-directed frees the slab count settles, so the whole
 * run fits in a handful of slabs no matter how many blocks pass through.
 *
 * Build (POSIX):
 *   cc -O2 -pthread tests/runtime/rt_mem_remote_test.c src/runtime/rt_mem.c \
 *        -Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=calloc -Wl,--wrap=realloc \
 *        -o rt_mem_remote_test
 *
 * Exit code 0 = all checks passed, non-zero = at least one failure.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void  *__wrap_malloc(size_t n);
void   __wrap_free(void *p);
size_t zan_mem_slabs(void);

static int failures;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
} while (0)

/* Hand-off ring between the producer and the consumer thread. */
#define RING 256
#define ROUNDS 4000
#define PER_ROUND 64

static void *g_ring[RING];
static size_t g_put, g_take;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static int g_done;

static void ring_put(void *p) {
    pthread_mutex_lock(&g_mu);
    while (g_put - g_take == RING) pthread_cond_wait(&g_cv, &g_mu);
    g_ring[g_put++ % RING] = p;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
}

/* Returns NULL once the producer is finished and the ring has drained. */
static void *ring_take(void) {
    pthread_mutex_lock(&g_mu);
    while (g_put == g_take && !g_done) pthread_cond_wait(&g_cv, &g_mu);
    void *p = NULL;
    if (g_put != g_take) p = g_ring[g_take++ % RING];
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
    return p;
}

/* The consumer: frees blocks it never allocated. */
static void *consumer(void *arg) {
    (void)arg;
    size_t freed = 0;
    for (;;) {
        void *p = ring_take();
        if (!p) break;
        /* Touch the payload so a block handed out twice would corrupt the
         * other holder's data, not just this counter. */
        memset(p, 0xAB, 16);
        __wrap_free(p);
        freed++;
    }
    return (void *)freed;
}

/* A thread with a cache of its own: allocates, frees its own blocks, exits. */
static void *alloc_then_exit(void *arg) {
    (void)arg;
    void *keep[PER_ROUND];
    for (int i = 0; i < PER_ROUND; i++) keep[i] = __wrap_malloc(128);
    for (int i = 0; i < PER_ROUND; i++) __wrap_free(keep[i]);
    return NULL;
}

int main(void) {
    /* Warm up: the producer's first slab and the size-class table. */
    void *warm[PER_ROUND];
    for (int i = 0; i < PER_ROUND; i++) warm[i] = __wrap_malloc(64);
    for (int i = 0; i < PER_ROUND; i++) __wrap_free(warm[i]);
    CHECK(zan_mem_slabs() >= 1, "producer mapped a slab");
    size_t slabs_before = zan_mem_slabs();

    pthread_t th;
    CHECK(pthread_create(&th, NULL, consumer, NULL) == 0, "pthread_create");
    if (failures) return 1;

    size_t sent = 0;
    for (int r = 0; r < ROUNDS; r++) {
        for (int i = 0; i < PER_ROUND; i++) {
            /* A spread of classes, all small enough for the slab path. */
            size_t n = (size_t)(16 + ((r * PER_ROUND + i) % 12) * 64);
            void *p = __wrap_malloc(n);
            CHECK(p != NULL, "malloc returned a block");
            if (!p) { g_done = 1; return 1; }
            memset(p, 0x5A, 16);
            ring_put(p);
            sent++;
        }
    }
    pthread_mutex_lock(&g_mu);
    g_done = 1;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);

    void *ret = NULL;
    pthread_join(th, &ret);
    CHECK((size_t)ret == sent, "consumer freed every block it was handed");

    /* ROUNDS * PER_ROUND = 256k blocks crossed threads. Bounded reuse means a
     * few slabs; a drifting allocator needs one 1 MiB slab per ~8k blocks and
     * would be an order of magnitude past this. */
    size_t grew = zan_mem_slabs() - slabs_before;
    if (grew > 8) {
        fprintf(stderr, "FAIL: slab count grew by %zu after %zu cross-thread "
                        "frees (blocks are not returning to their owner)\n",
                grew, sent);
        failures++;
    }

    /* The returned blocks must be usable again by the owner: allocate the same
     * traffic once more and require no further slab growth at all. */
    size_t settled = zan_mem_slabs();
    for (int r = 0; r < 64; r++) {
        void *keep[PER_ROUND];
        for (int i = 0; i < PER_ROUND; i++) keep[i] = __wrap_malloc(64);
        for (int i = 0; i < PER_ROUND; i++) __wrap_free(keep[i]);
    }
    CHECK(zan_mem_slabs() == settled, "reused returned blocks without new slabs");

    /* Short-lived threads, each with its own cache. A cache retired at thread
     * exit must be adopted by the next thread instead of being abandoned with
     * its slab space, so 64 threads do not cost 64 slabs. */
    for (int t = 0; t < 64; t++) {
        pthread_t th2;
        CHECK(pthread_create(&th2, NULL, alloc_then_exit, NULL) == 0,
              "pthread_create");
        if (failures) return 1;
        pthread_join(th2, NULL);
    }
    CHECK(zan_mem_slabs() - settled <= 2, "short-lived threads reuse caches");

    if (failures == 0) printf("rt_mem_remote_test: all checks passed\n");
    return failures ? 1 : 0;
}
