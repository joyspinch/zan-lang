/* Public handles are positive, never-reused generations. Raw scheduler frames
 * have separate entries and parent/child edges. Queries, cancellation and retire
 * share one lock; no callback or destructor runs while that lock is held.
 * Metadata is bounded by peak live frames plus query pins awaiting retirement. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef struct zan_task_entry zan_task_entry;
struct zan_task_entry {
    uint64_t generation;
    void *frame;
    const volatile int *done;
    volatile int *cancel;
    zan_task_entry *child;
    zan_task_entry *parents;
    zan_task_entry *parent_next;
    zan_task_entry **parent_link;
    zan_task_entry *frame_next;
    zan_task_entry *token_next;
    size_t pins;
    void (*retire)(void *);
    void (*allocator_release)(void *);
    void (*discard)(void *);
};
typedef struct {
    zan_task_entry *frames;
    zan_task_entry *tokens;
} zan_task_bucket;
static zan_task_bucket *g_task_buckets;
static zan_task_entry *g_task_free;
static size_t g_task_capacity;
static size_t g_task_count;
static uint64_t g_task_generation;
static volatile int g_task_lock;
static void task_lock(void) {
    while (__atomic_exchange_n(&g_task_lock, 1, __ATOMIC_ACQUIRE)) { }
}
static void task_unlock(void) { __atomic_store_n(&g_task_lock, 0, __ATOMIC_RELEASE); }
static size_t task_bucket(uint64_t key, size_t capacity) {
    key ^= key >> 33;
    key *= UINT64_C(0xff51afd7ed558ccd);
    key ^= key >> 33;
    return (size_t)key & (capacity - 1);
}
static zan_task_entry *task_find(uint64_t generation) {
    if (!generation || !g_task_capacity) return NULL;
    for (zan_task_entry *entry = g_task_buckets[task_bucket(generation, g_task_capacity)].tokens;
         entry; entry = entry->token_next)
        if (entry->generation == generation) return entry;
    return NULL;
}
static zan_task_entry *task_frame(void *frame) {
    if (!frame || !g_task_capacity) return NULL;
    for (zan_task_entry *entry = g_task_buckets[task_bucket((uintptr_t)frame, g_task_capacity)].frames;
         entry; entry = entry->frame_next)
        if (entry->frame == frame) return entry;
    return NULL;
}
static void task_index_frame(zan_task_entry *entry) {
    zan_task_bucket *bucket = &g_task_buckets[task_bucket((uintptr_t)entry->frame, g_task_capacity)];
    entry->frame_next = bucket->frames;
    bucket->frames = entry;
}
static void task_index_token(zan_task_entry *entry) {
    zan_task_bucket *bucket = &g_task_buckets[task_bucket(entry->generation, g_task_capacity)];
    entry->token_next = bucket->tokens;
    bucket->tokens = entry;
}
static void task_forget_token(zan_task_entry *entry) {
    if (!entry->generation) return;
    zan_task_entry **link = &g_task_buckets[task_bucket(entry->generation, g_task_capacity)].tokens;
    while (*link != entry) link = &(*link)->token_next;
    *link = entry->token_next;
    entry->token_next = NULL;
    entry->generation = 0;
}
static void task_grow(void) {
    size_t old_capacity = g_task_capacity;
    size_t capacity = old_capacity ? old_capacity * 2 : 16;
    if (capacity < old_capacity || capacity > SIZE_MAX / sizeof(zan_task_entry)) abort();
    zan_task_entry *entries = (zan_task_entry *)calloc(capacity - old_capacity, sizeof(*entries));
    zan_task_bucket *buckets = (zan_task_bucket *)calloc(capacity, sizeof(*buckets));
    if (!entries || !buckets) abort();

    zan_task_bucket *old_buckets = g_task_buckets;
    g_task_buckets = buckets;
    g_task_capacity = capacity;
    for (size_t i = 0; i < old_capacity; ++i) {
        zan_task_entry *entry = old_buckets[i].frames;
        while (entry) {
            zan_task_entry *next = entry->frame_next;
            task_index_frame(entry);
            if (entry->generation) task_index_token(entry);
            entry = next;
        }
    }
    free(old_buckets);
    /* Blocks stay at stable addresses for await edges and the free list. */
    for (size_t i = 0; i < capacity - old_capacity; ++i) {
        entries[i].frame_next = g_task_free;
        g_task_free = &entries[i];
    }
}
static zan_task_entry *task_slot(void *frame) {
    zan_task_entry *entry = task_frame(frame);
    if (entry) return entry;
    if (!g_task_free) task_grow();
    entry = g_task_free;
    g_task_free = entry->frame_next;
    entry->frame = frame;
    task_index_frame(entry);
    ++g_task_count;
    return entry;
}
static void task_recycle(zan_task_entry *entry) {
    zan_task_entry **link = &g_task_buckets[task_bucket((uintptr_t)entry->frame, g_task_capacity)].frames;
    while (*link != entry) link = &(*link)->frame_next;
    *link = entry->frame_next;
    memset(entry, 0, sizeof(*entry));
    entry->frame_next = g_task_free;
    g_task_free = entry;
    --g_task_count;
}
static void task_unlink_child(zan_task_entry *parent) {
    if (!parent->child) return;
    *parent->parent_link = parent->parent_next;
    if (parent->parent_next) parent->parent_next->parent_link = parent->parent_link;
    parent->child = NULL;
    parent->parent_next = NULL;
    parent->parent_link = NULL;
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
    task_index_token(entry);
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
        if (consume) task_forget_token(entry);
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
        task_recycle(entry);
    }
    task_unlock();
    if (retire) retire(frame);
}
static void task_cancel_chain(zan_task_entry *entry) {
    /* Bound traversal even if a malformed await chain contains a cycle.
     * Every entry remains live under the retirement lock. */
    size_t remaining = g_task_count;
    while (entry && !entry->retire && remaining--) {
        if (entry->cancel) __atomic_store_n(entry->cancel, 1, __ATOMIC_RELEASE);
        entry = entry->child;
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
    task_unlink_child(p);
    if (c) {
        p->child = c;
        p->parent_next = c->parents;
        p->parent_link = &c->parents;
        if (c->parents) c->parents->parent_link = &p->parent_next;
        c->parents = p;
    }
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
    zan_task_entry *entry = task_frame(frame);
    if (entry) {
        if (entry->retire) { task_unlock(); return; }
        while (entry->parents) task_unlink_child(entry->parents);
        task_unlink_child(entry);
        task_forget_token(entry);
        if (entry->allocator_release) release = entry->allocator_release;
        if (entry->pins) { entry->retire = release; deferred = 1; }
        else task_recycle(entry);
    }
    task_unlock();
    if (!deferred) release(frame);
}
size_t zan_task_live_count(void) {
    task_lock();
    size_t count = g_task_count;
    task_unlock();
    return count;
}
size_t zan_task_slot_capacity(void) {
    task_lock();
    size_t capacity = g_task_capacity;
    task_unlock();
    return capacity;
}
