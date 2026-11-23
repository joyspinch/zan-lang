/* rt_timer_co.c: 协程生命周期存活跟踪与结构化汇聚（Join/WhenAll/WhenAny） */

#define ZAN_LIVE_DEAD ((void *)(uintptr_t)1)
#define ZAN_COLIVE_SHARDS 64u
#define ZAN_COLIVE_MASK (ZAN_COLIVE_SHARDS - 1u)

typedef struct {
  _Alignas(64) volatile int lock;
  void **slots;
  size_t cap;
  size_t live;
  size_t dead; /* tombstones */
} zan_colive_shard_t;

static zan_colive_shard_t g_colive[ZAN_COLIVE_SHARDS];
static volatile int g_join_lock;
static volatile size_t g_joinmap_active;

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

static void spin_lock_word(volatile int *lk) {
  for (int spins = 0;; spins++) {
    if (!__sync_lock_test_and_set(lk, 1))
      return;
    zan_lock_backoff(spins);
  }
}

static void spin_unlock_word(volatile int *lk) { __sync_lock_release(lk); }

static void join_lock(void) { spin_lock_word(&g_join_lock); }
static void join_unlock(void) { spin_unlock_word(&g_join_lock); }

static size_t live_hash(void *p) {
  uint64_t x = (uint64_t)(uintptr_t)p;
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return (size_t)x;
}

static inline zan_colive_shard_t *live_shard(void *frame, size_t *hash_out) {
  size_t h = live_hash(frame);
  if (hash_out)
    *hash_out = h;
  return &g_colive[(h >> 16) & ZAN_COLIVE_MASK];
}

static void live_rehash(zan_colive_shard_t *sh, size_t ncap) {
  void **old = sh->slots;
  size_t ocap = sh->cap;
  void **ns = (void **)calloc(ncap, sizeof(*ns));
  if (!ns)
    zan_rt_fatal("oom", "timer: coroutine-live table grow failed");
  sh->slots = ns;
  sh->cap = ncap;
  sh->dead = 0;
  sh->live = 0;
  for (size_t i = 0; i < ocap; i++) {
    void *f = old[i];
    if (!f || f == ZAN_LIVE_DEAD)
      continue;
    size_t j = live_hash(f) & (ncap - 1);
    while (ns[j])
      j = (j + 1) & (ncap - 1);
    ns[j] = f;
    sh->live++;
  }
  free(old);
}

void zan_co_live_add(void *frame) {
  if (!frame)
    return;
  size_t h = 0;
  zan_colive_shard_t *sh = live_shard(frame, &h);
  spin_lock_word(&sh->lock);
  if ((sh->live + sh->dead + 1) * 4 > sh->cap * 3)
    live_rehash(sh, sh->cap ? (sh->live * 4 > sh->cap ? sh->cap * 2 : sh->cap)
                            : 32);
  size_t mask = sh->cap - 1;
  size_t i = h & mask;
  size_t reuse = (size_t)-1;
  for (;;) {
    void *cur = sh->slots[i];
    if (!cur)
      break;
    if (cur == frame) {
      spin_unlock_word(&sh->lock);
      return;
    }
    if (cur == ZAN_LIVE_DEAD && reuse == (size_t)-1)
      reuse = i;
    i = (i + 1) & mask;
  }
  if (reuse != (size_t)-1) {
    i = reuse;
    sh->dead--;
  }
  sh->slots[i] = frame;
  sh->live++;
  spin_unlock_word(&sh->lock);
}

int zan_co_live_count(void) {
  size_t total = 0;
  for (size_t s = 0; s < ZAN_COLIVE_SHARDS; s++) {
    spin_lock_word(&g_colive[s].lock);
    total += g_colive[s].live;
    spin_unlock_word(&g_colive[s].lock);
  }
  return (int)total;
}

int zan_co_live_has(void *frame) {
  if (!frame)
    return 0;
  size_t h = 0;
  zan_colive_shard_t *sh = live_shard(frame, &h);
  spin_lock_word(&sh->lock);
  int found = 0;
  if (sh->cap) {
    size_t mask = sh->cap - 1;
    size_t i = h & mask;
    for (;;) {
      void *cur = sh->slots[i];
      if (!cur)
        break;
      if (cur == frame) {
        found = 1;
        break;
      }
      i = (i + 1) & mask;
    }
  }
  spin_unlock_word(&sh->lock);
  return found;
}

void zan_co_live_reset(void) {
  for (size_t s = 0; s < ZAN_COLIVE_SHARDS; s++) {
    spin_lock_word(&g_colive[s].lock);
    free(g_colive[s].slots);
    g_colive[s].slots = NULL;
    g_colive[s].cap = g_colive[s].live = g_colive[s].dead = 0;
    spin_unlock_word(&g_colive[s].lock);
  }
}

/* 任务协同汇聚与事件驱动 Join 原语 */

#define JOIN_OFF_DONE (12 + (int)sizeof(void *))
typedef struct zan_co_header_probe {
  long long sched;            /* ASYNC_FRAME_SCHED (i64) */
  void (*sched_step)(void *); /* ASYNC_FRAME_SCHED_STEP (ptr) */
  int state;                  /* ASYNC_FRAME_STATE (i32) */
  int done;                   /* ASYNC_FRAME_DONE (i32) */
  void *awaiter;              /* ASYNC_FRAME_AWAITER (ptr) */
  void *awaiter_step;         /* ASYNC_FRAME_AWAITER_STEP (ptr) */
  long long result;           /* ASYNC_FRAME_RESULT (i64) */
  void (*cleanup)(void *);    /* ASYNC_FRAME_CLEANUP (ptr) */
  int hcount;                 /* ASYNC_FRAME_HCOUNT (i32) */
  void (*self_step)(void *);  /* ASYNC_FRAME_SELF_STEP (ptr) */
  void *exc;                  /* ASYNC_FRAME_EXC (ptr) */
  void *exc_tid;              /* ASYNC_FRAME_EXC_TID (ptr) */
  int exc_owned;              /* ASYNC_FRAME_EXC_OWNED (i32) */
} zan_co_header_probe;
_Static_assert(offsetof(zan_co_header_probe, done) == JOIN_OFF_DONE,
               "JOIN_OFF_DONE drifts from the emitter's ASYNC_FRAME_DONE "
               "(see irgen_expr_core.c)");
_Static_assert(offsetof(zan_co_header_probe, state) ==
                   JOIN_OFF_DONE - (int)sizeof(int),
               "co header state/done pair out of order vs JOIN_OFF_DONE");
#define JOIN_OFF_EXC (int)offsetof(zan_co_header_probe, exc)
#define JOIN_OFF_EXC_TID (int)offsetof(zan_co_header_probe, exc_tid)
#define JOIN_OFF_EXC_OWN (int)offsetof(zan_co_header_probe, exc_owned)

typedef struct zan_join_pair {
  void *frame;
  struct zan_join *owner;
  int idx;
  int done;
  struct zan_join_pair *next_in_frame;
} zan_join_pair_t;

typedef struct zan_join {
  int any;
  int fired;
  void *joiner;
  zan_timer_step_t joiner_step;
  int npairs;
  int winner;
  int remaining;
  /* M2：首个失败子任务的异常快照（任务结束锁内从子帧转移所有权），
   * joiner 唤醒后经 zan_join_exc 取走并沿 await 链重抛。 */
  void *exc;
  void *exc_tid;
  int exc_owned;
  int exc_taken;
  int capacity;
  zan_join_pair_t pairs[];
} zan_join_t;

static zan_join_pair_t **g_joinmap_slots;
static size_t g_joinmap_cap;
static size_t g_joinmap_live;
static size_t g_joinmap_dead;
#define JOINMAP_TOMB ((zan_join_pair_t *)(uintptr_t)1)

static void joinmap_rehash(size_t ncap) {
  zan_join_pair_t **nslots = calloc(ncap, sizeof(*nslots));
  if (!nslots) {
    zan_rt_fatal("oom", "joinmap: rehash failed");
    return;
  }
  size_t nmask = ncap - 1;
  for (size_t i = 0; i < g_joinmap_cap; i++) {
    zan_join_pair_t *cur = g_joinmap_slots[i];
    if (!cur || cur == JOINMAP_TOMB)
      continue;
    size_t j = live_hash(cur->frame) & nmask;
    while (nslots[j])
      j = (j + 1) & nmask;
    nslots[j] = cur;
  }
  free(g_joinmap_slots);
  g_joinmap_slots = nslots;
  g_joinmap_cap = ncap;
  g_joinmap_dead = 0;
}

static void joinmap_put(zan_join_pair_t *pr) {
  if (!pr || !pr->frame)
    return;
  pr->next_in_frame = NULL;
  if ((g_joinmap_live + g_joinmap_dead + 1) * 4 >= g_joinmap_cap * 3)
    joinmap_rehash(g_joinmap_cap
                       ? (g_joinmap_live * 4 > g_joinmap_cap ? g_joinmap_cap * 2
                                                             : g_joinmap_cap)
                       : 64);
  size_t mask = g_joinmap_cap - 1;
  size_t i = live_hash(pr->frame) & mask;
  size_t reuse = SIZE_MAX;
  for (;;) {
    zan_join_pair_t *cur = g_joinmap_slots[i];
    if (!cur)
      break;
    if (cur == JOINMAP_TOMB) {
      if (reuse == SIZE_MAX)
        reuse = i;
    } else if (cur->frame == pr->frame) {
      /* Keep every subscriber for a frame on the same chain. */
      pr->next_in_frame = cur;
      g_joinmap_slots[i] = pr;
      return;
    }
    i = (i + 1) & mask;
  }
  if (reuse != SIZE_MAX) {
    i = reuse;
    g_joinmap_dead--;
  }
  g_joinmap_slots[i] = pr;
  g_joinmap_live++;
}

static zan_join_pair_t *joinmap_take(void *frame) {
  if (!g_joinmap_cap)
    return NULL;
  size_t mask = g_joinmap_cap - 1;
  size_t i = live_hash(frame) & mask;
  for (;;) {
    zan_join_pair_t *cur = g_joinmap_slots[i];
    if (!cur)
      return NULL;
    if (cur != JOINMAP_TOMB && cur->frame == frame) {
      g_joinmap_slots[i] = JOINMAP_TOMB;
      g_joinmap_live--;
      g_joinmap_dead++;
      return cur;
    }
    i = (i + 1) & mask;
  }
}

static int joinmap_remove(void *frame, zan_join_pair_t *pr) {
  if (!g_joinmap_cap || !pr)
    return 0;
  size_t mask = g_joinmap_cap - 1;
  size_t i = live_hash(frame) & mask;
  for (;;) {
    zan_join_pair_t *cur = g_joinmap_slots[i];
    if (!cur)
      return 0;
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
          return 1;
        }
        pp = &(*pp)->next_in_frame;
      }
      return 0;
    }
    i = (i + 1) & mask;
  }
}

static int join_pair_done(const zan_join_pair_t *pr) {
  if (pr->done)
    return 1;
  if (!zan_co_live_has(pr->frame))
    return 1; /* untracked: completed */
  int32_t done = __atomic_load_n(
      (const volatile int32_t *)((const unsigned char *)pr->frame +
                                 JOIN_OFF_DONE),
      __ATOMIC_ACQUIRE);
  return done != 0;
}

static int join_any_done(zan_join_t *j) {
  if (j->npairs == 0)
    return 1;
  for (int i = 0; i < j->npairs; i++)
    if (join_pair_done(&j->pairs[i])) {
      j->winner = j->pairs[i].idx;
      return 1;
    }
  return 0;
}

typedef void (*zan_join_ready_t)(void *, zan_timer_step_t);

typedef struct {
  void *frame;
  zan_timer_step_t step;
} zan_join_wake_t;

#define JOIN_INLINE_WAKES 16

typedef struct {
  size_t count;
  size_t overflow_cap;
  zan_join_wake_t *overflow;
  zan_join_wake_t local[JOIN_INLINE_WAKES];
} zan_join_wakes_t;

static zan_join_ready_t join_ready_hook(void) {
  timer_lock();
  zan_join_ready_t hook = g_ready_hook;
  timer_unlock();
  return hook;
}

static void join_wakes_add(zan_join_wakes_t *wakes, const zan_join_t *j) {
  if (!j->joiner || !j->joiner_step)
    return;
  size_t i = wakes->count++;
  zan_join_wake_t wake = {j->joiner, j->joiner_step};
  if (i < JOIN_INLINE_WAKES) {
    wakes->local[i] = wake;
    return;
  }
  i -= JOIN_INLINE_WAKES;
  if (i == wakes->overflow_cap) {
    size_t cap =
        wakes->overflow_cap ? wakes->overflow_cap * 2 : JOIN_INLINE_WAKES;
    if (cap < wakes->overflow_cap || cap > SIZE_MAX / sizeof(wake))
      zan_rt_fatal("oom", "join: wake list too large");
    zan_join_wake_t *items = realloc(wakes->overflow, cap * sizeof(wake));
    if (!items)
      zan_rt_fatal("oom", "join: wake list allocation failed");
    wakes->overflow = items;
    wakes->overflow_cap = cap;
  }
  wakes->overflow[i] = wake;
}

static void join_wakes_dispatch(zan_join_wakes_t *wakes) {
  if (!wakes->count)
    return;
  zan_join_ready_t hook = join_ready_hook();
  if (hook) {
    for (size_t i = 0; i < wakes->count; i++) {
      zan_join_wake_t wake = i < JOIN_INLINE_WAKES
                                 ? wakes->local[i]
                                 : wakes->overflow[i - JOIN_INLINE_WAKES];
      hook(wake.frame, wake.step);
    }
  }
  free(wakes->overflow);
}

static void join_on_task_end_locked(void *frame, zan_join_wakes_t *wakes) {
  zan_join_pair_t *pr = joinmap_take(frame);
  while (pr) {
    zan_join_pair_t *next = pr->next_in_frame;
    pr->next_in_frame = NULL;
    __atomic_fetch_sub(&g_joinmap_active, 1, __ATOMIC_RELEASE);
    if (!pr->done) {
      pr->done = 1;
      zan_join_t *j = pr->owner;
      j->remaining--;
      /* M2：子任务带异常结束时，此刻子帧尚未拆除——把异常对象
       * 所有权从子帧转移到 join 快照（清子帧槽位防二次释放），
       * joiner 唤醒后经 zan_join_exc 取走重抛。仅记首个。 */
      if (!j->exc_taken && !j->exc) {
        void *exc = *(void **)((unsigned char *)frame + JOIN_OFF_EXC);
        if (exc) {
          j->exc = exc;
          j->exc_tid = *(void **)((unsigned char *)frame + JOIN_OFF_EXC_TID);
          j->exc_owned =
              *(volatile int *)((unsigned char *)frame + JOIN_OFF_EXC_OWN);
          *(void **)((unsigned char *)frame + JOIN_OFF_EXC) = NULL;
          *(volatile int *)((unsigned char *)frame + JOIN_OFF_EXC_OWN) = 0;
        }
      }
      if (!j->fired && (j->any ? join_any_done(j) : j->remaining == 0)) {
        j->fired = 1;
        /* A ready waiter may immediately cancel and free its join. */
        join_wakes_add(wakes, j);
      }
    }
    pr = next;
  }
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
  if (!frame)
    return;
  zan_join_wakes_t wakes = {0};
  join_lock();
  join_on_task_end_locked(frame, &wakes);
  join_unlock();
  join_wakes_dispatch(&wakes);
}

void zan_co_live_del(void *frame) {
  if (!frame)
    return;
  size_t h = 0;
  zan_colive_shard_t *sh = live_shard(frame, &h);
  spin_lock_word(&sh->lock);
  if (sh->cap) {
    size_t mask = sh->cap - 1;
    size_t i = h & mask;
    for (;;) {
      void *cur = sh->slots[i];
      if (!cur)
        break;
      if (cur == frame) {
        sh->slots[i] = ZAN_LIVE_DEAD;
        sh->live--;
        sh->dead++;
        break;
      }
      i = (i + 1) & mask;
    }
  }
  spin_unlock_word(&sh->lock);
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  if (__atomic_load_n(&g_joinmap_active, __ATOMIC_SEQ_CST) > 0) {
    zan_join_wakes_t wakes = {0};
    join_lock();
    join_on_task_end_locked(frame, &wakes);
    join_unlock();
    join_wakes_dispatch(&wakes);
  }
}

long long zan_join_new(int npairs, int any) {
  size_t sz = sizeof(zan_join_t) + (size_t)npairs * sizeof(zan_join_pair_t);
  zan_join_t *j = malloc(sz);
  if (!j)
    zan_rt_fatal("oom", "join: entry alloc failed");
  memset(j, 0, sz);
  j->any = (any != 0);
  j->capacity = npairs;
  return (long long)(intptr_t)j;
}

int zan_join_bind(long long entry, void *frame, int idx) {
  zan_join_t *j = (zan_join_t *)(intptr_t)entry;
  if (!j || !frame)
    return 0;
  int bound = 0;
  join_lock();
  __atomic_fetch_add(&g_joinmap_active, 1, __ATOMIC_SEQ_CST);
  if (zan_co_live_has(frame)) {
    int32_t done = __atomic_load_n(
        (const volatile int32_t *)((const unsigned char *)frame +
                                   JOIN_OFF_DONE),
        __ATOMIC_ACQUIRE);
    if (done && !j->exc_taken && !j->exc) {
      /* M2：已完成（甚至已 throw）的子任务在 bind 前就绪时，异常
       * 快照仍要收——否则 join 语义吞掉先于 bind 的失败。帧未 reap
       * 时 EXC 槽位有效（所有权随快照转移）；已 reap 的帧在
       * live 检查处就已跳过。 */
      void *exc = *(void **)((unsigned char *)frame + JOIN_OFF_EXC);
      if (exc) {
        j->exc = exc;
        j->exc_tid = *(void **)((unsigned char *)frame + JOIN_OFF_EXC_TID);
        j->exc_owned =
            *(volatile int *)((unsigned char *)frame + JOIN_OFF_EXC_OWN);
        *(void **)((unsigned char *)frame + JOIN_OFF_EXC) = NULL;
        *(volatile int *)((unsigned char *)frame + JOIN_OFF_EXC_OWN) = 0;
      }
    }
    if (!done && j->npairs < j->capacity) {
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
  if (!bound)
    __atomic_fetch_sub(&g_joinmap_active, 1, __ATOMIC_RELEASE);
  join_unlock();
  return bound;
}

int zan_join_wait2(long long entry, void *frame, zan_timer_step_t step) {
  zan_join_t *j = (zan_join_t *)(intptr_t)entry;
  if (!j || !frame || !step || !join_ready_hook())
    return 2;
  int satisfied;
  join_lock();
  satisfied = j->fired || (j->any ? join_any_done(j) : j->remaining == 0);
  if (!satisfied) {
    j->joiner = frame;
    j->joiner_step = step;
  }
  join_unlock();
  return satisfied ? 1 : 0;
}

void *zan_join_exc(long long entry, void **tid_out, int *owned_out) {
  zan_join_t *j = (zan_join_t *)(intptr_t)entry;
  if (!j) {
    if (tid_out)
      *tid_out = NULL;
    if (owned_out)
      *owned_out = 0;
    return NULL;
  }
  join_lock();
  void *exc = j->exc;
  void *tid = j->exc_tid;
  int owned = j->exc_owned;
  j->exc = NULL;
  j->exc_tid = NULL;
  j->exc_owned = 0;
  /* An await observes this snapshot once. A later completion leaves its
   * exception on its own frame while the join is being detached. */
  j->exc_taken = 1;
  join_unlock();
  if (tid_out)
    *tid_out = tid;
  if (owned_out)
    *owned_out = owned;
  return exc;
}

void zan_join_cancel(long long entry) {
  zan_join_t *j = (zan_join_t *)(intptr_t)entry;
  if (!j)
    return;
  join_lock();
  for (int i = 0; i < j->npairs; i++) {
    zan_join_pair_t *pr = &j->pairs[i];
    if (!pr->done && joinmap_remove(pr->frame, pr))
      __atomic_fetch_sub(&g_joinmap_active, 1, __ATOMIC_RELEASE);
  }
  join_unlock();
  free(j);
}
