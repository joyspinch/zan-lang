/* rt_timer_co.c: 协程生命周期存活跟踪与结构化汇聚（Join/WhenAll/WhenAny） */

#define ZAN_LIVE_DEAD ((void *)(uintptr_t)1)

static void  **g_colive_slots;
static size_t   g_colive_cap;
static size_t   g_colive_live;
static size_t   g_colive_dead;   /* tombstones */

static volatile int g_colive_lock;

/* 带退避的 TTAS 自旋锁 */
static void zan_lock_backoff(int spins) {
    if (spins < 64) {
#if defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause();
#endif
        return;
    }
#if defined(_WIN32)
    SwitchToThread();
#elif defined(__wasm__) || defined(ZAN_BARE_METAL)
#else
    sched_yield();
#endif
}

static void live_lock(void) {
    for (int spins = 0;; spins++) {
        if (!__sync_lock_test_and_set(&g_colive_lock, 1)) return;
        zan_lock_backoff(spins);
    }
}

static void live_unlock(void) { __sync_lock_release(&g_colive_lock); }

static size_t live_hash(void *p) {
    uint64_t x = (uint64_t)(uintptr_t)p;
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (size_t)x;
}

static void live_rehash(size_t ncap) {
    void **old = g_colive_slots;
    size_t ocap = g_colive_cap;
    void **ns = (void **)calloc(ncap, sizeof(*ns));
    if (!ns) zan_rt_fatal("oom", "timer: coroutine-live table grow failed");
    g_colive_slots = ns;
    g_colive_cap = ncap;
    g_colive_dead = 0;
    g_colive_live = 0;
    for (size_t i = 0; i < ocap; i++) {
        void *f = old[i];
        if (!f || f == ZAN_LIVE_DEAD) continue;
        size_t j = live_hash(f) & (ncap - 1);
        while (ns[j]) j = (j + 1) & (ncap - 1);
        ns[j] = f;
        g_colive_live++;
    }
    free(old);
}

void zan_co_live_add(void *frame) {
    if (!frame) return;
    live_lock();
    if ((g_colive_live + g_colive_dead + 1) * 4 > g_colive_cap * 3)
        live_rehash(g_colive_cap ? (g_colive_live * 4 > g_colive_cap ? g_colive_cap * 2 : g_colive_cap) : 64);
    size_t mask = g_colive_cap - 1;
    size_t i = live_hash(frame) & mask;
    size_t reuse = (size_t)-1;
    for (;;) {
        void *cur = g_colive_slots[i];
        if (!cur) break;
        if (cur == frame) { live_unlock(); return; }
        if (cur == ZAN_LIVE_DEAD && reuse == (size_t)-1) reuse = i;
        i = (i + 1) & mask;
    }
    if (reuse != (size_t)-1) { i = reuse; g_colive_dead--; }
    g_colive_slots[i] = frame;
    g_colive_live++;
    live_unlock();
}

#define ZAN_MAX_JOIN_FIRES 16
static int join_on_untrack(void *frame, void **out_joiners,
                           zan_timer_step_t *out_steps, int max_fires);

void zan_co_live_del(void *frame) {
    if (!frame || !g_colive_cap) return;
    void *fire_joiners[ZAN_MAX_JOIN_FIRES];
    zan_timer_step_t fire_steps[ZAN_MAX_JOIN_FIRES];
    live_lock();
    int nfires = join_on_untrack(frame, fire_joiners, fire_steps, ZAN_MAX_JOIN_FIRES);
    size_t mask = g_colive_cap - 1;
    size_t i = live_hash(frame) & mask;
    for (;;) {
        void *cur = g_colive_slots[i];
        if (!cur) break;
        if (cur == frame) {
            g_colive_slots[i] = ZAN_LIVE_DEAD;
            g_colive_live--;
            g_colive_dead++;
            break;
        }
        i = (i + 1) & mask;
    }
    live_unlock();
    void (*_hook)(void *, zan_timer_step_t) = g_ready_hook;
    if (_hook) {
        for (int k = 0; k < nfires; k++) {
            _hook(fire_joiners[k], fire_steps[k]);
        }
    }
}

int zan_co_live_count(void) {
    if (!g_colive_cap) return 0;
    live_lock();
    int n = (int)g_colive_live;
    live_unlock();
    return n;
}

static int live_has_nolock(void *frame) {
    size_t mask = g_colive_cap - 1;
    size_t i = live_hash(frame) & mask;
    for (;;) {
        void *cur = g_colive_slots[i];
        if (!cur) return 0;
        if (cur == frame) return 1;
        i = (i + 1) & mask;
    }
}

int zan_co_live_has(void *frame) {
    if (!frame || !g_colive_cap) return 0;
    live_lock();
    int found = live_has_nolock(frame);
    live_unlock();
    return found;
}

void zan_co_live_reset(void) {
    live_lock();
    free(g_colive_slots);
    g_colive_slots = NULL;
    g_colive_cap = g_colive_live = g_colive_dead = 0;
    live_unlock();
}

/* 任务协同汇聚与事件驱动 Join 原语 */

#define JOIN_OFF_DONE (12 + (int)sizeof(void *))
typedef struct zan_co_header_probe {
    long long sched;                /* ASYNC_FRAME_SCHED (i64) */
    void (*sched_step)(void *);     /* ASYNC_FRAME_SCHED_STEP (ptr) */
    int state;                      /* ASYNC_FRAME_STATE (i32) */
    int done;                       /* ASYNC_FRAME_DONE (i32) */
} zan_co_header_probe;
_Static_assert(offsetof(zan_co_header_probe, done) == JOIN_OFF_DONE,
               "JOIN_OFF_DONE drifts from the emitter's ASYNC_FRAME_DONE "
               "(see irgen_expr_core.c)");
_Static_assert(offsetof(zan_co_header_probe, state) == JOIN_OFF_DONE - (int)sizeof(int),
               "co header state/done pair out of order vs JOIN_OFF_DONE");

typedef struct zan_join_pair {
    void            *frame;
    struct zan_join *owner;
    int              idx;
    int              done;
    struct zan_join_pair *next_in_frame;
} zan_join_pair_t;

typedef struct zan_join {
    int              any;
    int              fired;
    void            *joiner;
    zan_timer_step_t joiner_step;
    int              npairs;
    int              winner;
    int              remaining;
    int              capacity;
    zan_join_pair_t  pairs[];
} zan_join_t;

static zan_join_pair_t **g_joinmap_slots;
static size_t   g_joinmap_cap;
static size_t   g_joinmap_live;
static size_t   g_joinmap_dead;
#define JOINMAP_TOMB ((zan_join_pair_t *)(uintptr_t)1)

static void joinmap_rehash(size_t ncap) {
    zan_join_pair_t **nslots = calloc(ncap, sizeof(*nslots));
    if (!nslots) { zan_rt_fatal("oom", "joinmap: rehash failed"); return; }
    size_t nmask = ncap - 1;
    for (size_t i = 0; i < g_joinmap_cap; i++) {
        zan_join_pair_t *cur = g_joinmap_slots[i];
        if (!cur || cur == JOINMAP_TOMB) continue;
        size_t j = live_hash(cur->frame) & nmask;
        while (nslots[j]) j = (j + 1) & nmask;
        nslots[j] = cur;
    }
    free(g_joinmap_slots);
    g_joinmap_slots = nslots;
    g_joinmap_cap = ncap;
    g_joinmap_dead = 0;
}

static void joinmap_put(zan_join_pair_t *pr) {
    if (!pr || !pr->frame) return;
    pr->next_in_frame = NULL;
    if ((g_joinmap_live + g_joinmap_dead + 1) * 4 >= g_joinmap_cap * 3)
        joinmap_rehash(g_joinmap_cap
                           ? (g_joinmap_live * 4 > g_joinmap_cap ? g_joinmap_cap * 2
                                                                 : g_joinmap_cap)
                           : 64);
    size_t mask = g_joinmap_cap - 1;
    size_t i = live_hash(pr->frame) & mask;
    for (;;) {
        zan_join_pair_t *cur = g_joinmap_slots[i];
        if (!cur || cur == JOINMAP_TOMB) {
            if (cur == JOINMAP_TOMB) g_joinmap_dead--;
            g_joinmap_slots[i] = pr;
            g_joinmap_live++;
            return;
        }
        if (cur != JOINMAP_TOMB && cur->frame == pr->frame) {
            /* 同一任务被多个 join 监听：头插法链入一对多链表 */
            pr->next_in_frame = cur;
            g_joinmap_slots[i] = pr;
            return;
        }
        i = (i + 1) & mask;
    }
}

static zan_join_pair_t *joinmap_get(void *frame) {
    if (!g_joinmap_cap) return NULL;
    size_t mask = g_joinmap_cap - 1;
    size_t i = live_hash(frame) & mask;
    for (;;) {
        zan_join_pair_t *cur = g_joinmap_slots[i];
        if (!cur) return NULL;
        if (cur != JOINMAP_TOMB && cur->frame == frame) return cur;
        i = (i + 1) & mask;
    }
}

static void joinmap_remove(void *frame, zan_join_pair_t *pr) {
    if (!g_joinmap_cap || !pr) return;
    size_t mask = g_joinmap_cap - 1;
    size_t i = live_hash(frame) & mask;
    for (;;) {
        zan_join_pair_t *cur = g_joinmap_slots[i];
        if (!cur) return;
        if (cur != JOINMAP_TOMB && cur->frame == frame) {
            zan_join_pair_t **pp = &g_joinmap_slots[i];
            while (*pp) {
                if (*pp == pr) {
                    *pp = pr->next_in_frame;
                    pr->next_in_frame = NULL;
                    if (!g_joinmap_slots[i]) {
                        g_joinmap_slots[i] = JOINMAP_TOMB;
                        g_joinmap_live--;
                        g_joinmap_dead++;
                    }
                    return;
                }
                pp = &(*pp)->next_in_frame;
            }
            return;
        }
        i = (i + 1) & mask;
    }
}

static int join_pair_done(const zan_join_pair_t *pr) {
    if (pr->done) return 1;
    if (!live_has_nolock(pr->frame)) return 1;   /* untracked: completed */
    int32_t done = __atomic_load_n(
        (const volatile int32_t *)((const unsigned char *)pr->frame + JOIN_OFF_DONE),
        __ATOMIC_ACQUIRE);
    return done != 0;
}

static int join_any_done(zan_join_t *j) {
    if (j->npairs == 0) return 1;
    for (int i = 0; i < j->npairs; i++)
        if (join_pair_done(&j->pairs[i])) { j->winner = j->pairs[i].idx; return 1; }
    return 0;
}

static void join_fire_locked(zan_join_t *j, void **out_joiner, zan_timer_step_t *out_step) {
    *out_joiner = NULL;
    if (j->fired) return;
    int trig = j->any ? join_any_done(j) : (j->remaining == 0);
    if (!trig) return;
    j->fired = 1;
    *out_joiner = j->joiner;
    *out_step = j->joiner_step;
}

static int join_on_untrack(void *frame, void **out_joiners,
                           zan_timer_step_t *out_steps, int max_fires) {
    int nfires = 0;
    zan_join_pair_t *pr = joinmap_get(frame);
    while (pr) {
        zan_join_pair_t *next = pr->next_in_frame;
        if (!pr->done) {
            pr->done = 1;
            pr->owner->remaining--;
            void *joiner = NULL;
            zan_timer_step_t step = NULL;
            join_fire_locked(pr->owner, &joiner, &step);
            if (joiner && step && nfires < max_fires) {
                out_joiners[nfires] = joiner;
                out_steps[nfires] = step;
                nfires++;
            }
        }
        joinmap_remove(frame, pr);
        pr = next;
    }
    return nfires;
}

static int zan_sched_trace(void) {
    static volatile int t = -1;
    int cur = __atomic_load_n(&t, __ATOMIC_RELAXED);
    if (cur < 0) {
        const char *e = getenv("ZAN_SCHED_TRACE");
        cur = (e && *e && *e != '0') ? 1 : 0;
        __atomic_store_n(&t, cur, __ATOMIC_RELAXED);
    }
    return cur;
}

void zan_join_complete(void *frame) {
    if (!frame || !g_joinmap_cap) return;
    void *fire_joiners[ZAN_MAX_JOIN_FIRES];
    zan_timer_step_t fire_steps[ZAN_MAX_JOIN_FIRES];
    int nfires = 0;
    live_lock();
    zan_join_pair_t *pr = joinmap_get(frame);
    while (pr) {
        zan_join_pair_t *next = pr->next_in_frame;
        if (!pr->done) {
            pr->done = 1;
            pr->owner->remaining--;
            void *joiner = NULL;
            zan_timer_step_t step = NULL;
            join_fire_locked(pr->owner, &joiner, &step);
            if (joiner && step && nfires < ZAN_MAX_JOIN_FIRES) {
                fire_joiners[nfires] = joiner;
                fire_steps[nfires] = step;
                nfires++;
            }
        }
        joinmap_remove(frame, pr);
        pr = next;
    }
    live_unlock();

    void (*_hook)(void *, zan_timer_step_t) = g_ready_hook;
    if (_hook) {
        for (int i = 0; i < nfires; i++) {
            _hook(fire_joiners[i], fire_steps[i]);
        }
    }
}

long long zan_join_new(int npairs, int any) {
    size_t sz = sizeof(zan_join_t) + (size_t)npairs * sizeof(zan_join_pair_t);
    zan_join_t *j = malloc(sz);
    if (!j) zan_rt_fatal("oom", "join: entry alloc failed");
    memset(j, 0, sz);
    j->any = (any != 0);
    j->capacity = npairs;
    return (long long)(intptr_t)j;
}

int zan_join_bind(long long entry, void *frame, int idx) {
    zan_join_t *j = (zan_join_t *)(intptr_t)entry;
    if (!j || !frame) return 0;
    int bound = 0;
    live_lock();
    if (live_has_nolock(frame)) {
        int32_t done = __atomic_load_n(
            (const volatile int32_t *)((const unsigned char *)frame + JOIN_OFF_DONE),
            __ATOMIC_ACQUIRE);
        if (!done) {
            if (j->npairs >= j->capacity) { live_unlock(); return 0; }
            zan_join_pair_t *pr = &j->pairs[j->npairs];
            pr->frame = frame;
            pr->owner = j;
            pr->idx = idx;
            pr->done = 0;
            pr->next_in_frame = NULL;
            j->npairs++;
            j->remaining++;
            joinmap_put(pr);
            bound = 1;
        }
    }
    live_unlock();
    return bound;
}

int zan_join_wait2(long long entry, void *frame, zan_timer_step_t step) {
    zan_join_t *j = (zan_join_t *)(intptr_t)entry;
    if (!j || !g_ready_hook || !frame || !step) return 2;
    int satisfied;
    live_lock();
    satisfied = j->fired || (j->any ? join_any_done(j) : j->remaining == 0);
    if (!satisfied) { j->joiner = frame; j->joiner_step = step; }
    live_unlock();
    return satisfied ? 1 : 0;
}

void zan_join_cancel(long long entry) {
    zan_join_t *j = (zan_join_t *)(intptr_t)entry;
    if (!j) return;
    live_lock();
    for (int i = 0; i < j->npairs; i++)
        joinmap_remove(j->pairs[i].frame, &j->pairs[i]);
    live_unlock();
    free(j);
}
