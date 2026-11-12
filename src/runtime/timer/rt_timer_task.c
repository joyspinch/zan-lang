/* Public handles are positive, never-reused generations. Raw scheduler frames
 * have separate entries and parent/child edges. Queries, cancellation and retire
 * share one lock; no callback or destructor runs while that lock is held.
 * Metadata is bounded by peak live frames plus query pins awaiting retirement. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef struct {
    uint64_t generation;
    void *frame;
    const volatile int *done;
    volatile int *cancel;
    void *child;
    size_t pins;
    void (*retire)(void *);
    void (*allocator_release)(void *);
    void (*discard)(void *);
} zan_task_entry;
static zan_task_entry *g_task_entries;
static size_t g_task_capacity;
static uint64_t g_task_generation;
static volatile int g_task_lock;
static void task_lock(void) {
    while (__atomic_exchange_n(&g_task_lock, 1, __ATOMIC_ACQUIRE)) { }
}
static void task_unlock(void) { __atomic_store_n(&g_task_lock, 0, __ATOMIC_RELEASE); }
static zan_task_entry *task_find(uint64_t generation) {
    if (!generation) return NULL;
    for (size_t i = 0; i < g_task_capacity; ++i)
        if (g_task_entries[i].generation == generation && g_task_entries[i].frame)
            return &g_task_entries[i];
    return NULL;
}
static zan_task_entry *task_frame(void *frame) {
    if (!frame) return NULL;
    for (size_t i = 0; i < g_task_capacity; ++i)
        if (g_task_entries[i].frame == frame) return &g_task_entries[i];
    return NULL;
}
static zan_task_entry *task_slot(void *frame) {
    zan_task_entry *entry = task_frame(frame);
    if (entry) return entry;
    size_t i;
    for (i = 0; i < g_task_capacity; ++i)
        if (!g_task_entries[i].frame) break;
    if (i == g_task_capacity) {
        size_t next = g_task_capacity ? g_task_capacity * 2 : 16;
        if (next < g_task_capacity || next > SIZE_MAX / sizeof(zan_task_entry)) abort();
        zan_task_entry *entries = (zan_task_entry *)realloc(g_task_entries,
            next * sizeof(zan_task_entry));
        if (!entries) abort();
        memset(entries + g_task_capacity, 0,
            (next - g_task_capacity) * sizeof(zan_task_entry));
        g_task_entries = entries;
        g_task_capacity = next;
    }
    entry = &g_task_entries[i];
    entry->frame = frame;
    return entry;
}
/* Called before publication. Addresses are supplied by the compiler rather
 * than inferred from a driver-specific frame layout. NULL leaves a field alone. */
void zan_task_frame_config(void *frame, const volatile int *done, volatile int *cancel) {
    if (!frame) abort();
    task_lock();
    zan_task_entry *entry = task_slot(frame);
    if (entry->retire) { task_unlock(); abort(); }
    if (done) entry->done = done;
    if (cancel) entry->cancel = cancel;
    task_unlock();
}
/* Record allocation provenance before publishing the frame. Retirement never
 * probes bytes before an address to guess which allocator owns it. */
void zan_task_frame_allocator(void *frame, void (*release)(void *)) {
    if (!frame || !release) abort();
    task_lock();
    zan_task_entry *entry = task_slot(frame);
    if (entry->retire || entry->allocator_release) { task_unlock(); abort(); }
    entry->allocator_release = release;
    task_unlock();
}
void zan_task_discard_config(void *frame, void (*discard)(void *)) {
    if (!frame || !discard) abort();
    task_lock();
    zan_task_entry *entry = task_slot(frame);
    if (entry->retire) { task_unlock(); abort(); }
    entry->discard = discard;
    task_unlock();
}
/* The caller owns a consuming pin. Take the callback once, then run arbitrary
 * typed result and exception destructors outside the registry lock. */
void zan_task_discard(void *frame) {
    task_lock();
    zan_task_entry *entry = task_frame(frame);
    if (!entry || entry->generation || !entry->pins || !entry->discard) {
        task_unlock(); abort();
    }
    void (*discard)(void *) = entry->discard;
    entry->discard = NULL;
    task_unlock();
    discard(frame);
}
uint64_t zan_task_register(void *frame, const volatile int *done) {
    if (!frame || !done) abort();
    task_lock();
    /* Exhaustion is fatal, never wrap to a generation a saved long can name. */
    if (g_task_generation == INT64_MAX) { task_unlock(); abort(); }
    zan_task_entry *entry = task_slot(frame);
    if (entry->generation || entry->retire) { task_unlock(); abort(); }
    entry->generation = ++g_task_generation;
    entry->done = done;
    uint64_t token = entry->generation;
    task_unlock();
    return token;
}
int zan_task_isdone(uint64_t token) {
    task_lock();
    zan_task_entry *entry = task_find(token);
    int done = !entry || entry->retire ||
        __atomic_load_n(entry->done, __ATOMIC_ACQUIRE) != 0;
    task_unlock();
    return done;
}
void *zan_task_acquire(uint64_t token, int consume) {
    task_lock();
    zan_task_entry *entry = task_find(token);
    void *frame = NULL;
    if (entry && !entry->retire) {
        ++entry->pins;
        frame = entry->frame;
        if (consume) entry->generation = 0;
    }
    task_unlock();
    return frame;
}
void zan_task_unpin(void *frame) {
    if (!frame) return;
    void (*retire)(void *) = NULL;
    task_lock();
    zan_task_entry *entry = task_frame(frame);
    if (!entry || !entry->pins) { task_unlock(); abort(); }
    --entry->pins;
    if (!entry->pins && entry->retire) {
        retire = entry->retire;
        memset(entry, 0, sizeof(*entry));
    }
    task_unlock();
    if (retire) retire(frame);
}
static void task_cancel_chain(zan_task_entry *entry) {
    /* A malformed cycle must not hang cancellation. Valid await edges form
     * a tree, and every node visited is live under the same retirement lock. */
    size_t remaining = g_task_capacity;
    while (entry && !entry->retire && remaining--) {
        if (entry->cancel) __atomic_store_n(entry->cancel, 1, __ATOMIC_RELEASE);
        entry = task_frame(entry->child);
    }
}
void zan_task_cancel(uint64_t token) {
    task_lock();
    task_cancel_chain(task_find(token));
    task_unlock();
}
void zan_task_frame_cancel(void *frame) {
    task_lock();
    task_cancel_chain(task_frame(frame));
    task_unlock();
}
void zan_task_child(void *parent, void *child) {
    task_lock();
    zan_task_entry *p = task_frame(parent);
    zan_task_entry *c = task_frame(child);
    if (!p || p->retire || (child && (!c || c->retire))) {
        task_unlock(); abort();
    }
    p->child = child;
    if (p->cancel && __atomic_load_n(p->cancel, __ATOMIC_ACQUIRE))
        task_cancel_chain(c);
    task_unlock();
}
/* Only after the scheduler gives up its final QUEUED/RUNNING owner. Retiring
 * a raw child clears all incoming edges before any address can be reused. */
void zan_task_retire(void *frame, void (*release)(void *)) {
    if (!frame) return;
    int deferred = 0;
    task_lock();
    for (size_t i = 0; i < g_task_capacity; ++i)
        if (g_task_entries[i].child == frame) g_task_entries[i].child = NULL;
    zan_task_entry *entry = task_frame(frame);
    if (entry) {
        entry->generation = 0;
        entry->child = NULL;
        if (entry->retire) { task_unlock(); return; }
        if (entry->allocator_release) release = entry->allocator_release;
        if (entry->pins) { entry->retire = release; deferred = 1; }
        else memset(entry, 0, sizeof(*entry));
    }
    task_unlock();
    if (!deferred) release(frame);
}
size_t zan_task_live_count(void) {
    size_t count = 0;
    task_lock();
    for (size_t i = 0; i < g_task_capacity; ++i)
        if (g_task_entries[i].frame) ++count;
    task_unlock();
    return count;
}
size_t zan_task_slot_capacity(void) {
    task_lock();
    size_t capacity = g_task_capacity;
    task_unlock();
    return capacity;
}
