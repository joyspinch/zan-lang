/* Linux/POSIX multiworker execution gate. Frame storage is retained until
 * zan_co_sched_run joins its owned workers. No volatile-based synchronization. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "../../src/runtime/rt_co.h"

void zan_co_delay(long long ms, void *frame, zan_co_step_t step);

typedef struct {
    /* Scheduler ABI prefix: accessed by runtime __atomic operations only. */
    long long sched;
    zan_co_step_t pending;
    atomic_int visits;
    atomic_int active;
    unsigned id;
} frame;

static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed;
static int started, finished;
static unsigned phase_participants, worker_count;
static atomic_uint completed, failures, timer_resumes, self_resumes;
static frame *items;
static frame root;
static unsigned burst, producers, rounds, deadline_seconds;
static struct timespec stop_at;

static void fail(const char *why) {
    fprintf(stderr, "FAIL: %s\n", why);
    atomic_fetch_add(&failures, 1);
}
static unsigned option(const char *name, unsigned fallback, unsigned max) {
    const char *s = getenv(name);
    if (!s || !*s) return fallback;
    char *end;
    errno = 0;
    unsigned long n = strtoul(s, &end, 10);
    if (errno || *end || !n || n > max) {
        fprintf(stderr, "invalid %s (range 1..%u)\n", name, max);
        exit(2);
    }
    return (unsigned)n;
}
static void *watchdog(void *unused) {
    (void)unused;
    pthread_mutex_lock(&gate);
    while (!finished) {
        int e = pthread_cond_timedwait(&changed, &gate, &stop_at);
        if (e == ETIMEDOUT && !finished) {
            fprintf(stderr, "FAIL deadline %us exceeded; completed=%u\n",
                    deadline_seconds, atomic_load(&completed));
            fflush(stderr);
            _Exit(124);
        }
        if (e && e != ETIMEDOUT) _Exit(125);
    }
    pthread_mutex_unlock(&gate);
    return NULL;
}
static void item_step(void *p) {
    frame *f = p;
    if (atomic_exchange(&f->active, 1)) fail("concurrent execution of one frame");
    int visit = atomic_fetch_add(&f->visits, 1);
    if (visit == 0) {
        /* First tasks rendezvous on distinct running workers. This proves
         * actual parallel execution rather than a configured worker count.
         * The watchdog bounds a scheduler that cannot fill the rendezvous. */
        pthread_mutex_lock(&gate);
        if (phase_participants < worker_count) {
            phase_participants++;
            pthread_cond_broadcast(&changed);
            while (phase_participants < worker_count)
                pthread_cond_wait(&changed, &gate);
        }
        pthread_mutex_unlock(&gate);
        /* Alternate self-ready and timer delivery; both can race the
         * scheduler's RUNNING -> idle transition. */
        if (f->id & 1) {
            atomic_fetch_add(&self_resumes, 1);
            zan_co_ready(f, item_step);
        } else {
            atomic_fetch_add(&timer_resumes, 1);
            zan_co_delay(1 + f->id % 3, f, item_step);
        }
    } else if (visit == 1) {
        atomic_fetch_add(&completed, 1);
    } else {
        fail("duplicate task delivery");
    }
    atomic_store(&f->active, 0);
}
static void root_step(void *p) {
    (void)p;
    pthread_mutex_lock(&gate);
    if (!started) {
        /* Worker-originated burst plus simultaneous foreign-thread bursts. */
        for (unsigned i = producers; i < burst; i += producers + 1)
            zan_co_ready(&items[i], item_step);
        started = 1;
        pthread_cond_broadcast(&changed);
    }
    pthread_mutex_unlock(&gate);
    if (atomic_load(&completed) < burst)
        zan_co_delay(1, &root, root_step);
}
static void *producer(void *p) {
    unsigned id = (unsigned)(uintptr_t)p;
    pthread_mutex_lock(&gate);
    while (!started) pthread_cond_wait(&changed, &gate);
    pthread_mutex_unlock(&gate);
    for (unsigned i = id; i < burst; i += producers + 1)
        zan_co_ready(&items[i], item_step);
    return NULL;
}
static void init_frame(frame *f, unsigned id) {
    memset(f, 0, sizeof(*f));
    atomic_init(&f->visits, 0);
    atomic_init(&f->active, 0);
    f->id = id;
}
int main(void) {
    rounds = option("ZAN_SOAK_ROUNDS", 8, 10000);
    burst = option("ZAN_SOAK_BURST", 128, 2048);
    producers = option("ZAN_SOAK_PRODUCERS", 3, 4);
    deadline_seconds = option("ZAN_SOAK_DEADLINE", 45, 1800);
    unsigned workers = option("ZAN_CO_WORKERS", 4, 8);
    if (workers < 2 || burst < producers + 1 || burst < workers) return 2;
    worker_count = workers;
    char worker_text[16];
    snprintf(worker_text, sizeof(worker_text), "%u", workers);
    if (setenv("ZAN_CO_WORKERS", worker_text, 1)) return 2;
    pthread_condattr_t clock_attr;
    if (pthread_condattr_init(&clock_attr)) return 2;
    if (pthread_condattr_setclock(&clock_attr, CLOCK_MONOTONIC)) return 2;
    if (pthread_cond_init(&changed, &clock_attr)) return 2;
    pthread_condattr_destroy(&clock_attr);
    if (clock_gettime(CLOCK_MONOTONIC, &stop_at)) return 2;
    stop_at.tv_sec += deadline_seconds;
    pthread_t guard;
    if (pthread_create(&guard, NULL, watchdog, NULL)) return 2;
    items = calloc(burst, sizeof(*items));
    if (!items) return 2;
    zan_co_sched_init();
    for (unsigned round = 0; round < rounds; round++) {
        pthread_t threads[4];
        pthread_mutex_lock(&gate);
        started = 0;
        phase_participants = 0;
        pthread_mutex_unlock(&gate);
        atomic_store(&completed, 0);
        init_frame(&root, 0);
        for (unsigned i = 0; i < burst; i++) init_frame(&items[i], i);
        for (unsigned i = 0; i < producers; i++)
            if (pthread_create(&threads[i], NULL, producer, (void *)(uintptr_t)i))
                _Exit(2);
        /* Register a timer before run: producers are gated until a foreground
         * worker executes root, avoiding detached background-pool ambiguity. */
        zan_co_delay(1, &root, root_step);
        zan_co_sched_run();
        for (unsigned i = 0; i < producers; i++) pthread_join(threads[i], NULL);
        if (atomic_load(&completed) != burst) fail("lost tasks at pool stop");
        for (unsigned i = 0; i < burst; i++)
            if (atomic_load(&items[i].visits) != 2 || atomic_load(&items[i].active))
                fail("frame not quiescent after pool join");
        if (atomic_load(&failures)) break;
    }
    pthread_mutex_lock(&gate);
    finished = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&gate);
    pthread_join(guard, NULL);
    free(items);
    if (atomic_load(&failures)) return 1;
    if (!atomic_load(&timer_resumes) || !atomic_load(&self_resumes)) return 1;
    printf("PASS workers=%u rounds=%u burst=%u producers=%u timer=%u self=%u\n",
           workers, rounds, burst, producers,
           atomic_load(&timer_resumes), atomic_load(&self_resumes));
    return 0;
}
