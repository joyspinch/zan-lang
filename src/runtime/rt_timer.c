/* clock_gettime is POSIX, not ISO C: a strict -std=c11 build (which is how the
 * timer object is compiled, natively and for every cross target) hides it
 * behind this feature macro, so ask for it before any header is pulled in.
 * Darwin exposes it unconditionally and narrows other APIs when asked for
 * strict POSIX, so leave it alone there. */
#if !defined(_WIN32) && !defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L
#endif

#include "rt_timer.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
typedef CRITICAL_SECTION zan_timer_mutex_t;
static INIT_ONCE g_lock_once = INIT_ONCE_STATIC_INIT;
static zan_timer_mutex_t g_lock;
static BOOL CALLBACK timer_lock_init(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once; (void)param; (void)ctx;
    InitializeCriticalSection(&g_lock);
    return TRUE;
}
static void timer_lock(void) {
    InitOnceExecuteOnce(&g_lock_once, timer_lock_init, NULL, NULL);
    EnterCriticalSection(&g_lock);
}
static void timer_unlock(void) { LeaveCriticalSection(&g_lock); }
#else
#include <pthread.h>
#include <time.h>
typedef pthread_mutex_t zan_timer_mutex_t;
static zan_timer_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static void timer_lock(void) { pthread_mutex_lock(&g_lock); }
static void timer_unlock(void) { pthread_mutex_unlock(&g_lock); }
#endif

typedef enum zan_timer_kind {
    ZAN_TIMER_DELAY = 0,
    ZAN_TIMER_PUBLIC = 1
} zan_timer_kind;

typedef struct zan_timer_entry {
    long long due_ms;
    long long id;
    long long interval;
    long long exec_msec;
    long long exec_count;
    long long round;
    unsigned long long sequence;
    zan_timer_kind kind;
    int removed;
    zan_timer_callback_t callback;
    void *frame;
    zan_timer_step_t step;
} zan_timer_entry;

static zan_timer_entry **g_heap;
static size_t g_heap_len;
static size_t g_heap_cap;
static long long g_next_id = 1;
static long long g_round;
static unsigned long long g_sequence;
static int g_initialized;
static void (*g_ready_hook)(void *frame, zan_timer_step_t step);

long long zan_timer_now_ms(void) {
#if defined(_WIN32)
    return (long long)GetTickCount64();
#elif defined(__APPLE__)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

static int timer_less(const zan_timer_entry *a, const zan_timer_entry *b) {
    return a->due_ms < b->due_ms ||
           (a->due_ms == b->due_ms && a->sequence < b->sequence);
}

static void heap_swap(size_t a, size_t b) {
    zan_timer_entry *entry = g_heap[a];
    g_heap[a] = g_heap[b];
    g_heap[b] = entry;
}

static void heap_push(zan_timer_entry *entry) {
    if (g_heap_len == g_heap_cap) {
        size_t cap = g_heap_cap ? g_heap_cap * 2 : 64;
        zan_timer_entry **heap = (zan_timer_entry **)realloc(g_heap, cap * sizeof(*heap));
        if (!heap) abort();
        g_heap = heap;
        g_heap_cap = cap;
    }
    size_t index = g_heap_len++;
    g_heap[index] = entry;
    while (index > 0) {
        size_t parent = (index - 1) / 2;
        if (!timer_less(entry, g_heap[parent])) break;
        heap_swap(index, parent);
        index = parent;
    }
}

static zan_timer_entry *heap_pop(void) {
    if (g_heap_len == 0) return NULL;
    zan_timer_entry *root = g_heap[0];
    zan_timer_entry *last = g_heap[--g_heap_len];
    if (g_heap_len == 0) return root;
    g_heap[0] = last;
    size_t index = 0;
    for (;;) {
        size_t left = index * 2 + 1;
        if (left >= g_heap_len) break;
        size_t right = left + 1;
        size_t smallest = right < g_heap_len && timer_less(g_heap[right], g_heap[left])
            ? right : left;
        if (!timer_less(g_heap[smallest], g_heap[index])) break;
        heap_swap(index, smallest);
        index = smallest;
    }
    return root;
}

static void heap_rebuild(void) {
    if (g_heap_len < 2) return;
    for (size_t i = g_heap_len / 2; i-- > 0;) {
        size_t index = i;
        for (;;) {
            size_t left = index * 2 + 1;
            if (left >= g_heap_len) break;
            size_t right = left + 1;
            size_t smallest = right < g_heap_len && timer_less(g_heap[right], g_heap[left])
                ? right : left;
            if (!timer_less(g_heap[smallest], g_heap[index])) break;
            heap_swap(index, smallest);
            index = smallest;
        }
    }
}

void zan_timer_set_ready_hook(void (*ready)(void *frame, zan_timer_step_t step)) {
    timer_lock();
    g_ready_hook = ready;
    timer_unlock();
}

void zan_timer_runtime_reset(void) {
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++) free(g_heap[i]);
    g_heap_len = 0;
    g_next_id = 1;
    g_round = 0;
    g_sequence = 0;
    g_initialized = 1;
    timer_unlock();
}

void zan_timer_delay(long long ms, void *frame, zan_timer_step_t step) {
    if (!step) return;
    zan_timer_entry *entry = (zan_timer_entry *)calloc(1, sizeof(*entry));
    if (!entry) abort();
    entry->due_ms = zan_timer_now_ms() + (ms > 0 ? ms : 0);
    entry->sequence = ++g_sequence;
    entry->kind = ZAN_TIMER_DELAY;
    entry->frame = frame;
    entry->step = step;
    timer_lock();
    g_initialized = 1;
    heap_push(entry);
    timer_unlock();
}

static long long timer_add(long long ms, zan_timer_callback_t callback, int repeat) {
    if (ms < 1 || !callback) return 0;
    zan_timer_entry *entry = (zan_timer_entry *)calloc(1, sizeof(*entry));
    if (!entry) abort();
    timer_lock();
    g_initialized = 1;
    entry->id = g_next_id++;
    if (entry->id <= 0) entry->id = g_next_id = 1;
    entry->interval = repeat ? ms : 0;
    entry->due_ms = zan_timer_now_ms() + ms;
    entry->sequence = ++g_sequence;
    entry->kind = ZAN_TIMER_PUBLIC;
    heap_push(entry);
    timer_unlock();
    entry->callback = callback;
    return entry->id;
}

long long zan_timer_tick(long long interval, zan_timer_callback_t callback) {
    return timer_add(interval, callback, 1);
}

long long zan_timer_after(long long delay, zan_timer_callback_t callback) {
    return timer_add(delay, callback, 0);
}

long long zan_timer_next_timeout(void) {
    long long timeout = -1;
    timer_lock();
    while (g_heap_len > 0 && g_heap[0]->removed) free(heap_pop());
    if (g_heap_len > 0) {
        timeout = g_heap[0]->due_ms - zan_timer_now_ms();
        if (timeout < 0) timeout = 0;
    }
    timer_unlock();
    return timeout;
}

size_t zan_timer_dispatch_due(void) {
    size_t dispatched = 0;
    for (;;) {
        long long now = zan_timer_now_ms();
        timer_lock();
        while (g_heap_len > 0 && g_heap[0]->removed) free(heap_pop());
        if (g_heap_len == 0 || g_heap[0]->due_ms > now) {
            timer_unlock();
            return dispatched;
        }
        zan_timer_entry *entry = heap_pop();
        if (entry->kind == ZAN_TIMER_PUBLIC) {
            entry->exec_count++;
            entry->round = ++g_round;
        }
        timer_unlock();

        long long started = zan_timer_now_ms();
        if (entry->kind == ZAN_TIMER_DELAY) {
            void (*ready)(void *, zan_timer_step_t) = g_ready_hook;
            if (ready) ready(entry->frame, entry->step);
            else entry->step(entry->frame);
        } else entry->callback();
        long long elapsed = zan_timer_now_ms() - started;
        dispatched++;

        timer_lock();
        if (entry->kind == ZAN_TIMER_PUBLIC) entry->exec_msec = elapsed;
        if (entry->kind == ZAN_TIMER_PUBLIC && entry->interval > 0 && !entry->removed) {
            entry->due_ms = zan_timer_now_ms() + entry->interval;
            entry->sequence = ++g_sequence;
            heap_push(entry);
            timer_unlock();
        } else {
            timer_unlock();
            free(entry);
        }
    }
}

size_t zan_timer_pending(void) {
    size_t count = 0;
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++)
        if (!g_heap[i]->removed) count++;
    timer_unlock();
    return count;
}

int zan_timer_clear(long long id) {
    int found = 0;
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++) {
        zan_timer_entry *entry = g_heap[i];
        if (entry->kind == ZAN_TIMER_PUBLIC && entry->id == id && !entry->removed) {
            entry->removed = 1;
            found = 1;
            break;
        }
    }
    timer_unlock();
    return found;
}

long long zan_timer_clear_all(void) {
    long long count = 0;
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++) {
        zan_timer_entry *entry = g_heap[i];
        if (entry->kind == ZAN_TIMER_PUBLIC && !entry->removed) {
            entry->removed = 1;
            count++;
        }
    }
    timer_unlock();
    return count;
}

int zan_timer_info(long long id, long long *exec_msec, long long *exec_count,
                   long long *interval, long long *round, int *removed) {
    int found = 0;
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++) {
        zan_timer_entry *entry = g_heap[i];
        if (entry->kind == ZAN_TIMER_PUBLIC && entry->id == id) {
            if (exec_msec) *exec_msec = entry->exec_msec;
            if (exec_count) *exec_count = entry->exec_count;
            if (interval) *interval = entry->interval;
            if (round) *round = entry->round;
            if (removed) *removed = entry->removed;
            found = 1;
            break;
        }
    }
    timer_unlock();
    return found;
}

long long zan_timer_list_count(void) {
    long long count = 0;
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++)
        if (g_heap[i]->kind == ZAN_TIMER_PUBLIC && !g_heap[i]->removed) count++;
    timer_unlock();
    return count;
}

long long zan_timer_list_at(long long index) {
    long long current = 0;
    long long id = 0;
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++) {
        zan_timer_entry *entry = g_heap[i];
        if (entry->kind != ZAN_TIMER_PUBLIC || entry->removed) continue;
        if (current++ == index) { id = entry->id; break; }
    }
    timer_unlock();
    return id;
}

void zan_timer_stats(long long *initialized, long long *num, long long *round) {
    if (initialized) *initialized = g_initialized;
    if (num) *num = zan_timer_list_count();
    if (round) *round = g_round;
}

long long swoole_timer_tick(long long interval, zan_timer_callback_t callback) { return zan_timer_tick(interval, callback); }
long long swoole_timer_after(long long delay, zan_timer_callback_t callback) { return zan_timer_after(delay, callback); }
int swoole_timer_clear(long long id) { return zan_timer_clear(id); }
long long swoole_timer_clear_all(void) { return zan_timer_clear_all(); }
int swoole_timer_info(long long id, long long *a, long long *b, long long *c, long long *d, int *e) { return zan_timer_info(id, a, b, c, d, e); }
long long swoole_timer_list_count(void) { return zan_timer_list_count(); }
long long swoole_timer_list_at(long long index) { return zan_timer_list_at(index); }
void swoole_timer_stats(long long *a, long long *b, long long *c) { zan_timer_stats(a, b, c); }
