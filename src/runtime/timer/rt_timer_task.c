/* Public handles are positive, never-reused generations. Raw scheduler frames
 * have separate entries and await edges. No callback or destructor runs while a
 * registry lock is held. Metadata is bounded by peak live frames plus query pins
 * awaiting retirement.
 *
 * ---- Sharding ----
 * Every coroutine creation configures its frame and every await sets and clears
 * an edge, so a web request makes ~100 registry calls. One process-wide lock
 * serialized all scheduler threads on it: under load half the acquisitions were
 * contended and the spinning was ~40% of a web server's busy CPU. Entries now
 * live in TASK_SHARDS shards chosen by frame address, each with its own lock,
 * slots, open-addressing index and free list, and every operation holds at most
 * one shard lock at a time.
 *
 * That works because edges no longer need back pointers: an await edge records
 * the child frame together with the child's incarnation (a stamp unique to one
 * registration of an address). Retirement only drops its own entry; an edge to
 * a retired child, or to a new frame that reused its address, no longer matches
 * and is ignored. Cancellation walks the chain one shard at a time; a racing
 * await is still caught because both the walk and zan_task_child work under the
 * parent's shard lock (the walk sets cancel then reads the edge, the edge
 * setter writes the edge then reads cancel).
 *
 * Generation tokens carry their shard in the low bits so token lookups visit a
 * single shard. Tokens stay strictly increasing across calls. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#if defined(_WIN32)
#include <windows.h>
#define task_cpu_relax() YieldProcessor()
static void task_os_yield(void) { SwitchToThread(); }
#elif defined(__wasm__) || defined(ZAN_BARE_METAL)
#define task_cpu_relax() ((void)0)
static void task_os_yield(void) { }
#else
#include <sched.h>
#if defined(__aarch64__) || defined(__arm__)
#define task_cpu_relax() __asm__ __volatile__("yield" ::: "memory")
#elif defined(__x86_64__) || defined(__i386__)
#define task_cpu_relax() __builtin_ia32_pause()
#else
#define task_cpu_relax() __asm__ __volatile__("" ::: "memory")
#endif
static void task_os_yield(void) { sched_yield(); }
#endif

#define TASK_SHARD_BITS 6
#define TASK_SHARDS (1u << TASK_SHARD_BITS)

typedef struct {
    uint64_t generation;
    uint64_t incarnation;     /* never 0 for a live entry */
    void *frame;
    const volatile int *done;
    volatile int *cancel;
    void *child;
    uint64_t child_incarnation;
    size_t pins;
    void (*retire)(void *);
    void (*allocator_release)(void *);
    void (*discard)(void *);
} zan_task_entry;

typedef struct {
    _Alignas(64) volatile int lock;
    zan_task_entry *entries;
    size_t capacity;
    /* Open-addressing index (slot+1, 0 = empty; linear probing, backward-shift
     * deletion) and a free-slot stack. Indices stay valid across realloc. */
    uint32_t *index;
    size_t index_cap;          /* power of two, >= 2 * capacity */
    uint32_t *free_slots;
    size_t free_n;
    uint64_t next_incarnation;
} task_shard;
static task_shard g_task_shards[TASK_SHARDS];
static uint64_t g_task_generation;

static void task_lock(task_shard *s) {
    for (unsigned spin = 0;; spin++) {
        if (!__atomic_load_n(&s->lock, __ATOMIC_RELAXED)
            && !__atomic_exchange_n(&s->lock, 1, __ATOMIC_ACQUIRE)) return;
        if (spin < 128) task_cpu_relax();
        else task_os_yield();
    }
}
static void task_unlock(task_shard *s) { __atomic_store_n(&s->lock, 0, __ATOMIC_RELEASE); }

static uint64_t task_mix(const void *frame) {
    uint64_t x = (uint64_t)(uintptr_t)frame;
    x ^= x >> 33;
    x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 33;
    return x;
}
/* Shard from the high bits, index position from the low bits. */
static task_shard *task_shard_of(const void *frame) {
    return &g_task_shards[task_mix(frame) >> (64 - TASK_SHARD_BITS)];
}
static task_shard *task_shard_of_token(uint64_t token) {
    return &g_task_shards[token & (TASK_SHARDS - 1)];
}

static void task_index_insert(task_shard *s, size_t slot) {
    size_t mask = s->index_cap - 1;
    size_t h = (size_t)task_mix(s->entries[slot].frame) & mask;
    while (s->index[h]) h = (h + 1) & mask;
    s->index[h] = (uint32_t)(slot + 1);
}
static void task_index_rebuild(task_shard *s, size_t cap) {
    uint32_t *idx = (uint32_t *)calloc(cap, sizeof(uint32_t));
    if (!idx) abort();
    free(s->index);
    s->index = idx;
    s->index_cap = cap;
    for (size_t i = 0; i < s->capacity; ++i)
        if (s->entries[i].frame) task_index_insert(s, i);
}
static size_t task_index_pos(task_shard *s, const void *frame) {
    if (!s->index_cap) return SIZE_MAX;
    size_t mask = s->index_cap - 1;
    size_t h = (size_t)task_mix(frame) & mask;
    for (;;) {
        uint32_t v = s->index[h];
        if (!v) return SIZE_MAX;
        if (s->entries[v - 1].frame == frame) return h;
        h = (h + 1) & mask;
    }
}
static void task_index_remove(task_shard *s, const void *frame) {
    size_t pos = task_index_pos(s, frame);
    if (pos == SIZE_MAX) return;
    size_t mask = s->index_cap - 1;
    s->index[pos] = 0;
    /* Backward-shift: re-seat the rest of the probe run. */
    size_t j = (pos + 1) & mask;
    while (s->index[j]) {
        uint32_t v = s->index[j];
        s->index[j] = 0;
        size_t h = (size_t)task_mix(s->entries[v - 1].frame) & mask;
        while (s->index[h]) h = (h + 1) & mask;
        s->index[h] = v;
        j = (j + 1) & mask;
    }
}

static zan_task_entry *task_find(task_shard *s, uint64_t generation) {
    if (!generation) return NULL;
    for (size_t i = 0; i < s->capacity; ++i)
        if (s->entries[i].generation == generation && s->entries[i].frame)
            return &s->entries[i];
    return NULL;
}
static zan_task_entry *task_frame(task_shard *s, void *frame) {
    if (!frame) return NULL;
    size_t pos = task_index_pos(s, frame);
    if (pos == SIZE_MAX) return NULL;
    return &s->entries[s->index[pos] - 1];
}
static zan_task_entry *task_slot(task_shard *s, void *frame) {
    zan_task_entry *entry = task_frame(s, frame);
    if (entry) return entry;
    if (!s->free_n) {
        size_t next = s->capacity ? s->capacity * 2 : 16;
        if (next < s->capacity || next > UINT32_MAX - 1
            || next > SIZE_MAX / sizeof(zan_task_entry)) abort();
        zan_task_entry *entries = (zan_task_entry *)realloc(s->entries,
            next * sizeof(zan_task_entry));
        if (!entries) abort();
        memset(entries + s->capacity, 0,
            (next - s->capacity) * sizeof(zan_task_entry));
        uint32_t *fl = (uint32_t *)realloc(s->free_slots, next * sizeof(uint32_t));
        if (!fl) abort();
        s->free_slots = fl;
        /* Push new slots so the lowest number pops first. */
        for (size_t k = next; k > s->capacity; --k)
            s->free_slots[s->free_n++] = (uint32_t)(k - 1);
        s->entries = entries;
        __atomic_store_n(&s->capacity, next, __ATOMIC_RELAXED);
        task_index_rebuild(s, next * 2);
    }
    size_t i = s->free_slots[--s->free_n];
    entry = &s->entries[i];
    entry->frame = frame;
    entry->incarnation = ++s->next_incarnation * TASK_SHARDS
        + (uint64_t)(s - g_task_shards);
    task_index_insert(s, i);
    return entry;
}
/* Release a slot: unindex, zero, free-list. Edges naming it go stale. */
static void task_drop(task_shard *s, zan_task_entry *entry) {
    task_index_remove(s, entry->frame);
    size_t slot = (size_t)(entry - s->entries);
    memset(entry, 0, sizeof(*entry));
    s->free_slots[s->free_n++] = (uint32_t)slot;
}
static size_t task_capacity_bound(void) {
    size_t total = 0;
    for (unsigned i = 0; i < TASK_SHARDS; ++i)
        total += __atomic_load_n(&g_task_shards[i].capacity, __ATOMIC_RELAXED);
    return total + 1;
}
/* Called before publication. Addresses are supplied by the compiler rather
 * than inferred from a driver-specific frame layout. NULL leaves a field alone. */
void zan_task_frame_config(void *frame, const volatile int *done, volatile int *cancel) {
    if (!frame) abort();
    task_shard *s = task_shard_of(frame);
    task_lock(s);
    zan_task_entry *entry = task_slot(s, frame);
    if (entry->retire) { task_unlock(s); abort(); }
    if (done) entry->done = done;
    if (cancel) entry->cancel = cancel;
    task_unlock(s);
}
/* Record allocation provenance before publishing the frame. Retirement never
 * probes bytes before an address to guess which allocator owns it. */
void zan_task_frame_allocator(void *frame, void (*release)(void *)) {
    if (!frame || !release) abort();
    task_shard *s = task_shard_of(frame);
    task_lock(s);
    zan_task_entry *entry = task_slot(s, frame);
    if (entry->retire || entry->allocator_release) { task_unlock(s); abort(); }
    entry->allocator_release = release;
    task_unlock(s);
}
void zan_task_discard_config(void *frame, void (*discard)(void *)) {
    if (!frame || !discard) abort();
    task_shard *s = task_shard_of(frame);
    task_lock(s);
    zan_task_entry *entry = task_slot(s, frame);
    if (entry->retire) { task_unlock(s); abort(); }
    entry->discard = discard;
    task_unlock(s);
}
/* The caller owns a consuming pin. Take the callback once, then run arbitrary
 * typed result and exception destructors outside the registry lock. */
void zan_task_discard(void *frame) {
    task_shard *s = task_shard_of(frame);
    task_lock(s);
    zan_task_entry *entry = task_frame(s, frame);
    if (!entry || entry->generation || !entry->pins || !entry->discard) {
        task_unlock(s); abort();
    }
    void (*discard)(void *) = entry->discard;
    entry->discard = NULL;
    task_unlock(s);
    discard(frame);
}
uint64_t zan_task_register(void *frame, const volatile int *done) {
    if (!frame || !done) abort();
    task_shard *s = task_shard_of(frame);
    /* Exhaustion is fatal, never wrap to a generation a saved long can name. */
    uint64_t seq = __atomic_add_fetch(&g_task_generation, 1, __ATOMIC_RELAXED);
    if (seq >= (uint64_t)INT64_MAX / TASK_SHARDS) abort();
    uint64_t token = seq * TASK_SHARDS + (uint64_t)(s - g_task_shards);
    task_lock(s);
    zan_task_entry *entry = task_slot(s, frame);
    if (entry->generation || entry->retire) { task_unlock(s); abort(); }
    entry->generation = token;
    entry->done = done;
    task_unlock(s);
    return token;
}
int zan_task_isdone(uint64_t token) {
    task_shard *s = task_shard_of_token(token);
    task_lock(s);
    zan_task_entry *entry = task_find(s, token);
    int done = !entry || entry->retire ||
        __atomic_load_n(entry->done, __ATOMIC_ACQUIRE) != 0;
    task_unlock(s);
    return done;
}
void *zan_task_acquire(uint64_t token, int consume) {
    task_shard *s = task_shard_of_token(token);
    task_lock(s);
    zan_task_entry *entry = task_find(s, token);
    void *frame = NULL;
    if (entry && !entry->retire) {
        ++entry->pins;
        frame = entry->frame;
        if (consume) entry->generation = 0;
    }
    task_unlock(s);
    return frame;
}
void zan_task_unpin(void *frame) {
    if (!frame) return;
    void (*retire)(void *) = NULL;
    task_shard *s = task_shard_of(frame);
    task_lock(s);
    zan_task_entry *entry = task_frame(s, frame);
    if (!entry || !entry->pins) { task_unlock(s); abort(); }
    --entry->pins;
    if (!entry->pins && entry->retire) {
        retire = entry->retire;
        task_drop(s, entry);
    }
    task_unlock(s);
    if (retire) retire(frame);
}
/* Mark `entry` (held under `s`, which this releases) cancelled, then follow
 * live await edges one shard at a time. A malformed cycle must not hang
 * cancellation: valid await edges form a tree, so the walk is bounded by the
 * slot count. */
static void task_cancel_chain_locked(task_shard *s, zan_task_entry *entry) {
    size_t remaining = task_capacity_bound();
    for (;;) {
        if (!entry || entry->retire) { task_unlock(s); return; }
        if (entry->cancel) __atomic_store_n(entry->cancel, 1, __ATOMIC_RELEASE);
        void *child = entry->child;
        uint64_t incarnation = entry->child_incarnation;
        task_unlock(s);
        if (!child || !remaining--) return;
        s = task_shard_of(child);
        task_lock(s);
        entry = task_frame(s, child);
        if (entry && entry->incarnation != incarnation) entry = NULL;
    }
}
void zan_task_cancel(uint64_t token) {
    task_shard *s = task_shard_of_token(token);
    task_lock(s);
    task_cancel_chain_locked(s, task_find(s, token));
}
void zan_task_frame_cancel(void *frame) {
    if (!frame) return;
    task_shard *s = task_shard_of(frame);
    task_lock(s);
    task_cancel_chain_locked(s, task_frame(s, frame));
}
void zan_task_child(void *parent, void *child) {
    uint64_t incarnation = 0;
    task_shard *cs = NULL;
    if (child) {
        cs = task_shard_of(child);
        task_lock(cs);
        zan_task_entry *c = task_frame(cs, child);
        if (!c || c->retire) { task_unlock(cs); abort(); }
        incarnation = c->incarnation;
        task_unlock(cs);
    }
    task_shard *ps = task_shard_of(parent);
    task_lock(ps);
    zan_task_entry *p = task_frame(ps, parent);
    if (!p || p->retire) { task_unlock(ps); abort(); }
    p->child = child;
    p->child_incarnation = incarnation;
    int cancelled = p->cancel && __atomic_load_n(p->cancel, __ATOMIC_ACQUIRE);
    task_unlock(ps);
    if (cancelled && child) {
        task_lock(cs);
        zan_task_entry *c = task_frame(cs, child);
        if (c && c->incarnation != incarnation) c = NULL;
        task_cancel_chain_locked(cs, c);
    }
}
/* Only after the scheduler gives up its final QUEUED/RUNNING owner. Edges that
 * still name this frame go stale with its incarnation, so no reuse of the
 * address can be reached through them. */
void zan_task_retire(void *frame, void (*release)(void *)) {
    if (!frame) return;
    int deferred = 0;
    task_shard *s = task_shard_of(frame);
    task_lock(s);
    zan_task_entry *entry = task_frame(s, frame);
    if (entry) {
        entry->generation = 0;
        entry->child = NULL;
        entry->child_incarnation = 0;
        if (entry->retire) { task_unlock(s); return; }
        if (entry->allocator_release) release = entry->allocator_release;
        if (entry->pins) { entry->retire = release; deferred = 1; }
        else task_drop(s, entry);
    }
    task_unlock(s);
    if (!deferred) release(frame);
}
size_t zan_task_live_count(void) {
    size_t count = 0;
    for (unsigned k = 0; k < TASK_SHARDS; ++k) {
        task_shard *s = &g_task_shards[k];
        task_lock(s);
        for (size_t i = 0; i < s->capacity; ++i)
            if (s->entries[i].frame) ++count;
        task_unlock(s);
    }
    return count;
}
size_t zan_task_slot_capacity(void) {
    size_t capacity = 0;
    for (unsigned k = 0; k < TASK_SHARDS; ++k) {
        task_shard *s = &g_task_shards[k];
        task_lock(s);
        capacity += s->capacity;
        task_unlock(s);
    }
    return capacity;
}
