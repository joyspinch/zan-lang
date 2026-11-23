/* Public handles are positive, never-reused generations. Raw scheduler frames
 * have separate entries and parent/child edges. Queries, cancellation and
 * retire share one lock; no callback or destructor runs while that lock is
 * held. Metadata is bounded by peak live frames plus query pins awaiting
 * retirement. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif
#include "../rt_co.h"
#include "../rt_timer.h"

#define ZAN_PROFILE_MAX_WAITS 32
#define ZAN_PROFILE_MAX_EXECUTION 64
#define ZAN_PROFILE_MAX_STAGES 48

typedef struct {
  int64_t cpu;
  int64_t bytes;
  int recorded;
  int cpu_missing;
} zan_task_stage_resources;

typedef struct {
  int64_t tid;
  int64_t start;
  int64_t elapsed;
  int64_t cpu;
  int64_t bytes;
} zan_task_execution;

typedef struct {
  const char *file;
  const char *kind;
  int32_t line;
  int32_t column;
  int64_t start;
  int64_t elapsed;
} zan_task_wait;

typedef struct {
  size_t refs;
  int64_t unfinished;
  int closed;
  int cpu_missing;
  void *root;
  int64_t cpu;
  int64_t bytes;
  int64_t slices;
  int64_t omitted_waits;
  int wait_count;
  zan_task_wait *waits;
  int record_execution;
  int execution_count;
  int64_t omitted_execution;
  zan_task_execution *execution;
  int64_t stage_id;
  zan_task_stage_resources *stages;
} zan_task_profile;

typedef struct {
  void *frame;
  zan_task_profile *profile;
  int64_t cpu_start;
  int64_t start;
  uint64_t bytes_start;
  int measuring;
  int inherited_stage;
  uint64_t open_stages;
} zan_task_profile_slice;

/* Slots are assigned in opening order. Only this frame can close its slots;
 * child frames carry the active slot they inherited when created. */
static int task_profile_stage(const zan_task_profile_slice *slice) {
  if (!slice->open_stages)
    return slice->inherited_stage;
  return 64 - __builtin_clzll(slice->open_stages);
}

#if defined(__wasm__) || defined(ZAN_BARE_METAL)
#define ZAN_PROFILE_TLS
#else
#define ZAN_PROFILE_TLS _Thread_local
#endif
static ZAN_PROFILE_TLS zan_task_profile_slice *g_profile_slice;
static ZAN_PROFILE_TLS uint64_t g_thread_alloc_bytes;
static size_t g_profile_count;
static int g_profile_enabled;
static int64_t g_profile_stage_id;

typedef struct zan_task_entry zan_task_entry;
struct zan_task_entry {
  uint64_t generation;
  uint64_t seq;
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
  zan_task_profile *profile;
  int pending_wait;
  int configured;
  int inherited_stage;
  uint64_t open_stages;
};
typedef struct {
  zan_task_entry *frames;
  zan_task_entry *tokens;
} zan_task_bucket;

#define ZAN_TASK_SHARDS 64
#define ZAN_TASK_SHARD_MASK (ZAN_TASK_SHARDS - 1)
#define ZAN_TASK_SHARD_FREE_CAP 16

typedef struct {
  volatile int lock;
  char pad[64 - sizeof(volatile int)];
} zan_task_shard_lock_t;

static zan_task_bucket *g_task_buckets;
static zan_task_entry *g_task_free;
static size_t g_task_free_count;
static zan_task_entry *g_shard_free[ZAN_TASK_SHARDS];
static size_t g_shard_free_count[ZAN_TASK_SHARDS];
static zan_task_shard_lock_t g_frame_locks[ZAN_TASK_SHARDS];
static zan_task_shard_lock_t g_token_locks[ZAN_TASK_SHARDS];
static zan_task_shard_lock_t g_reserve_lock;
static zan_task_shard_lock_t g_grow_lock;
static zan_task_shard_lock_t g_profile_lock;
static size_t g_task_capacity;
static size_t g_task_count;
static uint64_t g_task_generation;

static inline void spin_pause(void) {
#if defined(_WIN32)
  YieldProcessor();
#elif defined(__x86_64__) || defined(__i386__)
  __builtin_ia32_pause();
#elif defined(__aarch64__)
  __asm__ __volatile__("yield" ::: "memory");
#endif
}
static inline void spin_lock_at(volatile int *lk) {
  while (__atomic_exchange_n(lk, 1, __ATOMIC_ACQUIRE)) {
    while (__atomic_load_n(lk, __ATOMIC_RELAXED))
      spin_pause();
  }
}
static inline int spin_trylock_at(volatile int *lk) {
  return !__atomic_load_n(lk, __ATOMIC_RELAXED) &&
         !__atomic_exchange_n(lk, 1, __ATOMIC_ACQUIRE);
}
static inline void spin_unlock_at(volatile int *lk) {
  __atomic_store_n(lk, 0, __ATOMIC_RELEASE);
}
static inline void profile_lock(void) { spin_lock_at(&g_profile_lock.lock); }
static inline void profile_unlock(void) {
  spin_unlock_at(&g_profile_lock.lock);
}

static size_t task_bucket(uint64_t key, size_t capacity) {
  key ^= key >> 33;
  key *= UINT64_C(0xff51afd7ed558ccd);
  key ^= key >> 33;
  return (size_t)key & (capacity - 1);
}
static inline size_t frame_shard_for(void *frame, size_t cap) {
  return task_bucket((uintptr_t)frame, cap) & ZAN_TASK_SHARD_MASK;
}
static inline size_t token_shard_for(uint64_t token, size_t cap) {
  return task_bucket(token, cap) & ZAN_TASK_SHARD_MASK;
}

static void task_ensure_init(void) {
  if (__atomic_load_n(&g_task_capacity, __ATOMIC_ACQUIRE))
    return;
  spin_lock_at(&g_grow_lock.lock);
  if (!g_task_capacity) {
    size_t cap = 16;
    zan_task_entry *entries = (zan_task_entry *)calloc(cap, sizeof(*entries));
    zan_task_bucket *buckets = (zan_task_bucket *)calloc(cap, sizeof(*buckets));
    if (!entries || !buckets)
      abort();
    g_task_buckets = buckets;
    for (size_t i = 0; i < cap; ++i) {
      entries[i].frame_next = g_task_free;
      g_task_free = &entries[i];
    }
    g_task_free_count = cap;
    __atomic_store_n(&g_task_capacity, cap, __ATOMIC_RELEASE);
  }
  spin_unlock_at(&g_grow_lock.lock);
}

static size_t lock_frame_shard(void *frame, size_t *out_cap) {
  for (;;) {
    size_t cap = __atomic_load_n(&g_task_capacity, __ATOMIC_ACQUIRE);
    if (!cap) {
      task_ensure_init();
      continue;
    }
    size_t shard = frame_shard_for(frame, cap);
    spin_lock_at(&g_frame_locks[shard].lock);
    if (__atomic_load_n(&g_task_capacity, __ATOMIC_ACQUIRE) == cap) {
      if (out_cap)
        *out_cap = cap;
      return shard;
    }
    spin_unlock_at(&g_frame_locks[shard].lock);
  }
}

static size_t lock_token_shard(uint64_t token, size_t *out_cap) {
  for (;;) {
    size_t cap = __atomic_load_n(&g_task_capacity, __ATOMIC_ACQUIRE);
    if (!cap)
      return ZAN_TASK_SHARDS;
    size_t shard = token_shard_for(token, cap);
    spin_lock_at(&g_token_locks[shard].lock);
    if (__atomic_load_n(&g_task_capacity, __ATOMIC_ACQUIRE) == cap) {
      if (out_cap)
        *out_cap = cap;
      return shard;
    }
    spin_unlock_at(&g_token_locks[shard].lock);
  }
}

static zan_task_entry *task_find_cap(uint64_t generation, size_t cap) {
  if (!generation || !cap)
    return NULL;
  for (zan_task_entry *entry =
           g_task_buckets[task_bucket(generation, cap)].tokens;
       entry; entry = entry->token_next)
    if (entry->generation == generation)
      return entry;
  return NULL;
}
static zan_task_entry *task_frame_cap(void *frame, size_t cap) {
  if (!frame || !cap)
    return NULL;
  for (zan_task_entry *entry =
           g_task_buckets[task_bucket((uintptr_t)frame, cap)].frames;
       entry; entry = entry->frame_next)
    if (entry->frame == frame)
      return entry;
  return NULL;
}
static void task_index_frame(zan_task_entry *entry, size_t cap) {
  zan_task_bucket *bucket =
      &g_task_buckets[task_bucket((uintptr_t)entry->frame, cap)];
  entry->frame_next = bucket->frames;
  bucket->frames = entry;
}
static void task_index_token(zan_task_entry *entry, size_t cap) {
  zan_task_bucket *bucket =
      &g_task_buckets[task_bucket(entry->generation, cap)];
  entry->token_next = bucket->tokens;
  bucket->tokens = entry;
}
static void task_forget_token_locked(zan_task_entry *entry, size_t cap) {
  uint64_t gen = entry->generation;
  if (!gen)
    return;
  size_t tshard = token_shard_for(gen, cap);
  spin_lock_at(&g_token_locks[tshard].lock);
  zan_task_entry **link = &g_task_buckets[task_bucket(gen, cap)].tokens;
  while (*link && *link != entry)
    link = &(*link)->token_next;
  if (*link == entry) {
    *link = entry->token_next;
    entry->token_next = NULL;
  }
  entry->generation = 0;
  spin_unlock_at(&g_token_locks[tshard].lock);
}
static void task_grow_all_locked(void) {
  size_t old_capacity = g_task_capacity;
  size_t capacity = old_capacity ? old_capacity * 2 : 16;
  if (capacity < old_capacity || capacity > SIZE_MAX / sizeof(zan_task_entry))
    abort();
  zan_task_entry *entries =
      (zan_task_entry *)calloc(capacity - old_capacity, sizeof(*entries));
  zan_task_bucket *buckets =
      (zan_task_bucket *)calloc(capacity, sizeof(*buckets));
  if (!entries || !buckets)
    abort();

  zan_task_bucket *old_buckets = g_task_buckets;
  g_task_buckets = buckets;
  for (size_t i = 0; i < old_capacity; ++i) {
    zan_task_entry *entry = old_buckets[i].frames;
    while (entry) {
      zan_task_entry *next = entry->frame_next;
      task_index_frame(entry, capacity);
      if (entry->generation)
        task_index_token(entry, capacity);
      entry = next;
    }
  }
  free(old_buckets);
  /* Blocks stay at stable addresses for await edges and the free list. */
  spin_lock_at(&g_reserve_lock.lock);
  for (size_t i = 0; i < capacity - old_capacity; ++i) {
    entries[i].frame_next = g_task_free;
    g_task_free = &entries[i];
    ++g_task_free_count;
  }
  spin_unlock_at(&g_reserve_lock.lock);
  __atomic_store_n(&g_task_capacity, capacity, __ATOMIC_RELEASE);
}
static zan_task_entry *task_pop_reserve(void) {
  spin_lock_at(&g_reserve_lock.lock);
  zan_task_entry *entry = g_task_free;
  if (entry) {
    g_task_free = entry->frame_next;
    entry->frame_next = NULL;
    --g_task_free_count;
  }
  spin_unlock_at(&g_reserve_lock.lock);
  return entry;
}
static zan_task_entry *task_slot_locked(void *frame, size_t *shard_io,
                                        size_t *cap_io) {
  for (;;) {
    size_t shard = *shard_io;
    size_t cap = *cap_io;
    zan_task_entry *entry = task_frame_cap(frame, cap);
    if (entry)
      return entry;

    entry = g_shard_free[shard];
    if (entry) {
      g_shard_free[shard] = entry->frame_next;
      entry->frame_next = NULL;
      --g_shard_free_count[shard];
    } else {
      entry = task_pop_reserve();
    }
    if (entry) {
      entry->frame = frame;
      task_index_frame(entry, cap);
      __atomic_add_fetch(&g_task_count, 1, __ATOMIC_RELAXED);
      return entry;
    }

    /* Reclaim idle entries from other shards or grow when truly full. */
    spin_unlock_at(&g_frame_locks[shard].lock);
    spin_lock_at(&g_grow_lock.lock);
    for (size_t s = 0; s < ZAN_TASK_SHARDS; ++s)
      spin_lock_at(&g_frame_locks[s].lock);
    for (size_t s = 0; s < ZAN_TASK_SHARDS; ++s)
      spin_lock_at(&g_token_locks[s].lock);

    int reclaimed = 0;
    if (!g_task_free) {
      spin_lock_at(&g_reserve_lock.lock);
      for (size_t s = 0; s < ZAN_TASK_SHARDS; ++s) {
        while (g_shard_free[s]) {
          zan_task_entry *idle = g_shard_free[s];
          g_shard_free[s] = idle->frame_next;
          --g_shard_free_count[s];
          idle->frame_next = g_task_free;
          g_task_free = idle;
          ++g_task_free_count;
          reclaimed = 1;
        }
      }
      spin_unlock_at(&g_reserve_lock.lock);
      if (!reclaimed)
        task_grow_all_locked();
    }

    for (size_t s = ZAN_TASK_SHARDS; s-- > 0;)
      spin_unlock_at(&g_token_locks[s].lock);
    for (size_t s = ZAN_TASK_SHARDS; s-- > 0;)
      spin_unlock_at(&g_frame_locks[s].lock);
    spin_unlock_at(&g_grow_lock.lock);

    *shard_io = lock_frame_shard(frame, cap_io);
  }
}
static void task_profile_release(zan_task_profile *profile) {
  if (profile && --profile->refs == 0) {
    free(profile->waits);
    free(profile->execution);
    free(profile->stages);
    free(profile);
  }
}

static int64_t task_profile_cpu(void) {
#if defined(_WIN32)
  ULONG64 cycles;
  return QueryThreadCycleTime(GetCurrentThread(), &cycles) ? (int64_t)cycles
                                                           : -1;
#elif defined(CLOCK_THREAD_CPUTIME_ID) && !defined(__wasm__) &&                \
    !defined(ZAN_BARE_METAL)
  struct timespec stamp;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &stamp) != 0)
    return -1;
  return (int64_t)stamp.tv_sec * 1000000000 + stamp.tv_nsec;
#else
  return -1;
#endif
}

int64_t zan_task_profile_process_start_unix_ms(void) {
#if defined(_WIN32)
  FILETIME created, exited, kernel, user;
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    return 0;
  uint64_t ticks =
      ((uint64_t)created.dwHighDateTime << 32) | created.dwLowDateTime;
  const uint64_t unix_epoch = UINT64_C(116444736000000000);
  return ticks >= unix_epoch ? (int64_t)((ticks - unix_epoch) / 10000) : 0;
#else
  return 0;
#endif
}

static void task_profile_resume(zan_task_profile_slice *slice) {
  if (!slice || !slice->profile)
    return;
  slice->measuring = 1;
  slice->bytes_start = g_thread_alloc_bytes;
  slice->cpu_start = task_profile_cpu();
  if (slice->profile->execution)
    slice->start = zan_co_precise_us();
}

static void task_profile_pause(zan_task_profile_slice *slice) {
  if (!slice || !slice->measuring)
    return;
  zan_task_profile *profile = slice->profile;
  int64_t end = profile->execution ? zan_co_precise_us() : 0;
  int64_t cpu_end = task_profile_cpu();
  int64_t cpu = slice->cpu_start < 0 || cpu_end < slice->cpu_start
                    ? -1
                    : cpu_end - slice->cpu_start;
  int64_t bytes = (int64_t)(g_thread_alloc_bytes - slice->bytes_start);
  profile_lock();
  if (!profile->closed) {
    profile->slices++;
    profile->bytes += bytes;
    if (cpu < 0)
      profile->cpu_missing = 1;
    else
      profile->cpu += cpu;
    if (profile->stages) {
      zan_task_stage_resources *stage =
          &profile->stages[task_profile_stage(slice)];
      stage->recorded = 1;
      stage->bytes += bytes;
      if (cpu < 0)
        stage->cpu_missing = 1;
      else
        stage->cpu += cpu;
    }
    if (profile->record_execution) {
#if defined(_WIN32)
      if (profile->execution &&
          profile->execution_count < ZAN_PROFILE_MAX_EXECUTION) {
        profile->execution[profile->execution_count++] = (zan_task_execution){
            GetCurrentThreadId(), slice->start, end - slice->start, cpu, bytes};
      } else
#endif
      {
        profile->omitted_execution++;
      }
    }
  }
  profile_unlock();
  slice->measuring = 0;
}

void zan_task_profile_enable(void) {
  __atomic_store_n(&g_profile_enabled, 1, __ATOMIC_RELAXED);
}

void zan_task_profile_alloc_add(int64_t bytes) {
  if (__atomic_load_n(&g_profile_enabled, __ATOMIC_RELAXED) &&
      g_profile_slice && g_profile_slice->measuring && bytes > 0) {
    g_thread_alloc_bytes += (uint64_t)bytes;
  }
}

void zan_task_run_profiled(void *frame, zan_co_step_t step) {
  if (!__atomic_load_n(&g_profile_enabled, __ATOMIC_RELAXED)) {
    step(frame);
    return;
  }
  /* Track the current frame even before the first request opens a scope. */
  zan_task_profile_slice slice = {.frame = frame};
  zan_task_profile_slice *previous = g_profile_slice;
  task_profile_pause(previous);
  if (__atomic_load_n(&g_profile_count, __ATOMIC_ACQUIRE)) {
    size_t cap = 0;
    size_t shard = lock_frame_shard(frame, &cap);
    zan_task_entry *entry = task_frame_cap(frame, cap);
    if (entry && entry->profile) {
      profile_lock();
      if (!entry->profile->closed) {
        slice.profile = entry->profile;
        slice.inherited_stage = entry->inherited_stage;
        slice.open_stages = entry->open_stages;
        slice.profile->refs++;
        if (entry->pending_wait) {
          zan_task_wait *wait = &slice.profile->waits[entry->pending_wait - 1];
          wait->elapsed = zan_co_precise_us() - wait->start;
          entry->pending_wait = 0;
        }
      }
      profile_unlock();
    }
    spin_unlock_at(&g_frame_locks[shard].lock);
  }
  g_profile_slice = &slice;
  task_profile_resume(&slice);
  step(frame);
  task_profile_pause(&slice);
  g_profile_slice = previous;
  if (slice.profile) {
    profile_lock();
    task_profile_release(slice.profile);
    profile_unlock();
  }
  task_profile_resume(previous);
}

/* Registry references represent work whose completion has not been published.
 */
static void task_profile_detach_locked(zan_task_entry *entry) {
  zan_task_profile *profile = entry->profile;
  if (!profile)
    return;
  entry->profile = NULL;
  entry->pending_wait = 0;
  entry->inherited_stage = 0;
  entry->open_stages = 0;
  if (profile->root == entry->frame) {
    profile->closed = 1;
    __atomic_sub_fetch(&g_profile_count, 1, __ATOMIC_RELEASE);
  } else {
    profile->unfinished--;
  }
  task_profile_release(profile);
}

/* Settle the current resume before publishing DONE or waking its waiter. */
void zan_task_profile_checkpoint(void *frame) {
  zan_task_profile_slice *slice = g_profile_slice;
  if (!slice || slice->frame != frame || !slice->profile)
    return;
  task_profile_pause(slice);
  size_t cap = 0;
  size_t shard = lock_frame_shard(frame, &cap);
  zan_task_entry *entry = task_frame_cap(frame, cap);
  profile_lock();
  if (entry && entry->profile == slice->profile)
    task_profile_detach_locked(entry);
  task_profile_release(slice->profile);
  slice->profile = NULL;
  profile_unlock();
  spin_unlock_at(&g_frame_locks[shard].lock);
}

/* Bit 0 retains the execution-slice option. Bit 1 enables phase accounting. */
int zan_task_profile_begin(int flags) {
  zan_task_profile_slice *slice = g_profile_slice;
  if (!slice || !slice->frame || slice->profile)
    return 0;
  zan_task_profile *profile = calloc(1, sizeof(*profile));
  if (!profile)
    return 0;
  profile->refs = 1;
  if (flags & 2) {
    profile->stages =
        calloc(ZAN_PROFILE_MAX_STAGES + 1, sizeof(*profile->stages));
  }
#if defined(_WIN32)
  profile->record_execution = flags & 1;
  if (profile->record_execution) {
    profile->execution =
        malloc(ZAN_PROFILE_MAX_EXECUTION * sizeof(*profile->execution));
  }
#endif
  size_t cap = 0;
  size_t shard = lock_frame_shard(slice->frame, &cap);
  zan_task_entry *entry = task_frame_cap(slice->frame, cap);
  if (!entry || entry->retire || entry->profile) {
    spin_unlock_at(&g_frame_locks[shard].lock);
    task_profile_release(profile);
    return 0;
  }
  profile_lock();
  profile->refs++; /* Frame registration plus this active resume. */
  profile->root = slice->frame;
  if (profile->stages)
    profile->stage_id = ++g_profile_stage_id;
  entry->profile = profile;
  slice->profile = profile;
  slice->inherited_stage = 0;
  slice->open_stages = 0;
  __atomic_add_fetch(&g_profile_count, 1, __ATOMIC_RELEASE);
  profile_unlock();
  spin_unlock_at(&g_frame_locks[shard].lock);
  task_profile_resume(slice);
  return 1;
}

int64_t zan_task_profile_stage_id(void) {
  zan_task_profile_slice *slice = g_profile_slice;
  return slice && slice->measuring ? slice->profile->stage_id : 0;
}

void zan_task_profile_stage_set(int64_t id, int slot, int open) {
  zan_task_profile_slice *slice = g_profile_slice;
  if (!id || !slice || !slice->measuring || slice->profile->stage_id != id ||
      slot < 0 || slot >= ZAN_PROFILE_MAX_STAGES)
    return;
  uint64_t bit = UINT64_C(1) << slot;
  uint64_t next = open ? slice->open_stages | bit : slice->open_stages & ~bit;
  if (next == slice->open_stages)
    return;
  task_profile_pause(slice);
  slice->open_stages = next;
  size_t cap = 0;
  size_t shard = lock_frame_shard(slice->frame, &cap);
  zan_task_entry *entry = task_frame_cap(slice->frame, cap);
  if (entry && entry->profile == slice->profile)
    entry->open_stages = next;
  spin_unlock_at(&g_frame_locks[shard].lock);
  task_profile_resume(slice);
}

void zan_task_profile_wait(void *frame, const char *file, int32_t line,
                           int32_t column, const char *kind) {
  if (!__atomic_load_n(&g_profile_enabled, __ATOMIC_RELAXED))
    return;
  zan_task_profile_slice *slice = g_profile_slice;
  if (!slice || slice->frame != frame || !slice->measuring)
    return;
  zan_task_profile *profile = slice->profile;
  int64_t start = zan_co_precise_us();
  size_t cap = 0;
  size_t shard = lock_frame_shard(frame, &cap);
  zan_task_entry *entry = task_frame_cap(frame, cap);
  profile_lock();
  if (!profile->closed && entry && entry->profile == profile &&
      !entry->pending_wait) {
    if (!profile->waits)
      profile->waits = malloc(ZAN_PROFILE_MAX_WAITS * sizeof(*profile->waits));
    if (profile->waits && profile->wait_count < ZAN_PROFILE_MAX_WAITS) {
      profile->waits[profile->wait_count++] =
          (zan_task_wait){file, kind, line, column, start, -1};
      entry->pending_wait = profile->wait_count;
    } else {
      profile->omitted_waits++;
    }
  }
  profile_unlock();
  spin_unlock_at(&g_frame_locks[shard].lock);
}

void *zan_task_profile_end(int64_t *cpu, int64_t *bytes, int64_t *slices,
                           int64_t *unfinished) {
  zan_task_profile_slice *slice = g_profile_slice;
  if (!slice || !slice->profile || slice->profile->root != slice->frame)
    return NULL;
  task_profile_pause(slice);
  size_t cap = 0;
  size_t shard = lock_frame_shard(slice->frame, &cap);
  zan_task_entry *entry = task_frame_cap(slice->frame, cap);
  profile_lock();
  zan_task_profile *profile = slice->profile;
  profile->closed = 1;
  *cpu = profile->cpu_missing ? -1 : profile->cpu;
  *bytes = profile->bytes;
  *slices = profile->slices;
  *unfinished = profile->unfinished;
  if (entry && entry->profile == profile)
    task_profile_detach_locked(entry);
  slice->profile = NULL; /* Transfer the resume's reference to the report. */
  profile_unlock();
  spin_unlock_at(&g_frame_locks[shard].lock);
  return profile;
}

int zan_task_profile_read_stage(const void *report, int slot, int64_t *cpu,
                                int64_t *bytes) {
  const zan_task_profile *profile = report;
  if (!profile || !profile->stages || slot < -1 ||
      slot >= ZAN_PROFILE_MAX_STAGES)
    return 0;
  const zan_task_stage_resources *stage = &profile->stages[slot + 1];
  if (!stage->recorded)
    return 0;
  *cpu = stage->cpu_missing ? -1 : stage->cpu;
  *bytes = stage->bytes;
  return 1;
}

int zan_task_profile_read_execution(const void *report, int index, int64_t *tid,
                                    int64_t *start, int64_t *elapsed,
                                    int64_t *cpu, int64_t *bytes) {
  const zan_task_profile *profile = report;
  if (!profile || index < 0 || index >= profile->execution_count)
    return 0;
  const zan_task_execution *slice = &profile->execution[index];
  *tid = slice->tid;
  *start = slice->start;
  *elapsed = slice->elapsed;
  *cpu = slice->cpu;
  *bytes = slice->bytes;
  return 1;
}

int64_t zan_task_profile_omitted_execution(const void *report) {
  const zan_task_profile *profile = report;
  return profile ? profile->omitted_execution : 0;
}

int zan_task_profile_read_wait(const void *report, int index, const char **file,
                               int32_t *line, int32_t *column,
                               const char **kind, int64_t *start,
                               int64_t *elapsed) {
  const zan_task_profile *profile = report;
  if (!profile || index < 0 || index >= profile->wait_count)
    return 0;
  const zan_task_wait *wait = &profile->waits[index];
  *file = wait->file;
  *line = wait->line;
  *column = wait->column;
  *kind = wait->kind;
  *start = wait->start;
  *elapsed = wait->elapsed;
  return 1;
}

int64_t zan_task_profile_omitted_waits(const void *report) {
  const zan_task_profile *profile = report;
  return profile ? profile->omitted_waits : 0;
}

void zan_task_profile_release_report(void *report) {
  profile_lock();
  task_profile_release(report);
  profile_unlock();
}

static void task_recycle(zan_task_entry *entry, size_t shard, size_t cap) {
  zan_task_entry **link =
      &g_task_buckets[task_bucket((uintptr_t)entry->frame, cap)].frames;
  while (*link != entry)
    link = &(*link)->frame_next;
  *link = entry->frame_next;
  if (entry->profile) {
    profile_lock();
    task_profile_detach_locked(entry);
    profile_unlock();
  }
  uint64_t next_seq = entry->seq + 1;
  memset(entry, 0, sizeof(*entry));
  entry->seq = next_seq;
  if (g_shard_free_count[shard] < ZAN_TASK_SHARD_FREE_CAP) {
    entry->frame_next = g_shard_free[shard];
    g_shard_free[shard] = entry;
    ++g_shard_free_count[shard];
  } else {
    spin_lock_at(&g_reserve_lock.lock);
    entry->frame_next = g_task_free;
    g_task_free = entry;
    ++g_task_free_count;
    spin_unlock_at(&g_reserve_lock.lock);
  }
  __atomic_sub_fetch(&g_task_count, 1, __ATOMIC_RELAXED);
}
static void task_unlink_child(zan_task_entry *parent) {
  if (!parent->child)
    return;
  *parent->parent_link = parent->parent_next;
  if (parent->parent_next)
    parent->parent_next->parent_link = parent->parent_link;
  parent->child = NULL;
  parent->parent_next = NULL;
  parent->parent_link = NULL;
}
static void lock_two_frame_shards(void *f1, void *f2, size_t *s1_out,
                                  size_t *s2_out, size_t *cap_out) {
  for (;;) {
    size_t cap = __atomic_load_n(&g_task_capacity, __ATOMIC_ACQUIRE);
    if (!cap) {
      task_ensure_init();
      continue;
    }
    size_t s1 = frame_shard_for(f1, cap);
    size_t s2 = frame_shard_for(f2, cap);
    if (s1 == s2) {
      spin_lock_at(&g_frame_locks[s1].lock);
    } else if (s1 < s2) {
      spin_lock_at(&g_frame_locks[s1].lock);
      spin_lock_at(&g_frame_locks[s2].lock);
    } else {
      spin_lock_at(&g_frame_locks[s2].lock);
      spin_lock_at(&g_frame_locks[s1].lock);
    }
    if (__atomic_load_n(&g_task_capacity, __ATOMIC_ACQUIRE) == cap) {
      *s1_out = s1;
      *s2_out = s2;
      *cap_out = cap;
      return;
    }
    spin_unlock_at(&g_frame_locks[s1].lock);
    if (s2 != s1)
      spin_unlock_at(&g_frame_locks[s2].lock);
  }
}
static void lock_sorted_shards(size_t *shards, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      if (shards[j] < shards[i]) {
        size_t tmp = shards[i];
        shards[i] = shards[j];
        shards[j] = tmp;
      }
    }
  }
  for (size_t i = 0; i < n; ++i) {
    if (i == 0 || shards[i] != shards[i - 1])
      spin_lock_at(&g_frame_locks[shards[i]].lock);
  }
}
static void unlock_sorted_shards(const size_t *shards, size_t n) {
  for (size_t i = n; i-- > 0;) {
    if (i == 0 || shards[i] != shards[i - 1])
      spin_unlock_at(&g_frame_locks[shards[i]].lock);
  }
}
/* Called before publication. Addresses are supplied by the compiler rather
 * than inferred from a driver-specific frame layout. NULL leaves a field alone.
 */
void zan_task_frame_config(void *frame, const volatile int *done,
                           volatile int *cancel) {
  if (!frame)
    abort();
  size_t cap = 0;
  size_t shard = lock_frame_shard(frame, &cap);
  zan_task_entry *entry = task_slot_locked(frame, &shard, &cap);
  if (entry->retire) {
    spin_unlock_at(&g_frame_locks[shard].lock);
    abort();
  }
  if (done)
    entry->done = done;
  if (cancel)
    entry->cancel = cancel;
  if (!entry->configured &&
      __atomic_load_n(&g_profile_enabled, __ATOMIC_RELAXED) &&
      g_profile_slice && g_profile_slice->measuring && !entry->profile) {
    profile_lock();
    if (!g_profile_slice->profile->closed) {
      entry->profile = g_profile_slice->profile;
      entry->inherited_stage = task_profile_stage(g_profile_slice);
      entry->profile->refs++;
      entry->profile->unfinished++;
    }
    profile_unlock();
  }
  entry->configured = 1;
  spin_unlock_at(&g_frame_locks[shard].lock);
}
/* Record allocation provenance before publishing the frame. Retirement never
 * probes bytes before an address to guess which allocator owns it. */
void zan_task_frame_allocator(void *frame, void (*release)(void *)) {
  if (!frame || !release)
    abort();
  size_t cap = 0;
  size_t shard = lock_frame_shard(frame, &cap);
  zan_task_entry *entry = task_slot_locked(frame, &shard, &cap);
  if (entry->retire || entry->allocator_release) {
    spin_unlock_at(&g_frame_locks[shard].lock);
    abort();
  }
  entry->allocator_release = release;
  spin_unlock_at(&g_frame_locks[shard].lock);
}
void zan_task_discard_config(void *frame, void (*discard)(void *)) {
  if (!frame || !discard)
    abort();
  size_t cap = 0;
  size_t shard = lock_frame_shard(frame, &cap);
  zan_task_entry *entry = task_slot_locked(frame, &shard, &cap);
  if (entry->retire) {
    spin_unlock_at(&g_frame_locks[shard].lock);
    abort();
  }
  entry->discard = discard;
  spin_unlock_at(&g_frame_locks[shard].lock);
}
/* The caller owns a consuming pin. Take the callback once, then run arbitrary
 * typed result and exception destructors outside the registry lock. */
void zan_task_discard(void *frame) {
  size_t cap = 0;
  size_t shard = lock_frame_shard(frame, &cap);
  zan_task_entry *entry = task_frame_cap(frame, cap);
  if (!entry || entry->generation || !entry->pins || !entry->discard) {
    spin_unlock_at(&g_frame_locks[shard].lock);
    abort();
  }
  void (*discard)(void *) = entry->discard;
  entry->discard = NULL;
  spin_unlock_at(&g_frame_locks[shard].lock);
  discard(frame);
}
uint64_t zan_task_register(void *frame, const volatile int *done) {
  if (!frame || !done)
    abort();
  size_t cap = 0;
  size_t shard = lock_frame_shard(frame, &cap);
  zan_task_entry *entry = task_slot_locked(frame, &shard, &cap);
  if (entry->generation || entry->retire) {
    spin_unlock_at(&g_frame_locks[shard].lock);
    abort();
  }
  /* Exhaustion is fatal, never wrap to a generation a saved long can name. */
  uint64_t token = __atomic_add_fetch(&g_task_generation, 1, __ATOMIC_RELAXED);
  if (token > (uint64_t)INT64_MAX) {
    spin_unlock_at(&g_frame_locks[shard].lock);
    abort();
  }
  size_t tshard = token_shard_for(token, cap);
  spin_lock_at(&g_token_locks[tshard].lock);
  entry->done = done;
  entry->generation = token;
  task_index_token(entry, cap);
  spin_unlock_at(&g_token_locks[tshard].lock);
  spin_unlock_at(&g_frame_locks[shard].lock);
  return token;
}
int zan_task_isdone(uint64_t token) {
  if (!token)
    return 1;
  size_t cap = 0;
  size_t tshard = lock_token_shard(token, &cap);
  if (tshard == ZAN_TASK_SHARDS)
    return 1;
  zan_task_entry *entry = task_find_cap(token, cap);
  int done = !entry || entry->retire ||
             __atomic_load_n(entry->done, __ATOMIC_ACQUIRE) != 0;
  spin_unlock_at(&g_token_locks[tshard].lock);
  return done;
}
void *zan_task_acquire(uint64_t token, int consume) {
  if (!token)
    return NULL;
  size_t cap = 0;
  size_t tshard = lock_token_shard(token, &cap);
  if (tshard == ZAN_TASK_SHARDS)
    return NULL;
  zan_task_entry *entry = task_find_cap(token, cap);
  if (!entry) {
    spin_unlock_at(&g_token_locks[tshard].lock);
    return NULL;
  }
  void *frame = entry->frame;
  uint64_t seq = entry->seq;
  spin_unlock_at(&g_token_locks[tshard].lock);

  size_t fcap = 0;
  size_t fshard = lock_frame_shard(frame, &fcap);
  void *ret = NULL;
  if (entry->frame == frame && entry->seq == seq &&
      entry->generation == token && !entry->retire) {
    ++entry->pins;
    ret = frame;
    if (consume)
      task_forget_token_locked(entry, fcap);
  }
  spin_unlock_at(&g_frame_locks[fshard].lock);
  return ret;
}
void zan_task_unpin(void *frame) {
  if (!frame)
    return;
  void (*retire)(void *) = NULL;
  size_t cap = 0;
  size_t shard = lock_frame_shard(frame, &cap);
  zan_task_entry *entry = task_frame_cap(frame, cap);
  if (!entry || !entry->pins) {
    spin_unlock_at(&g_frame_locks[shard].lock);
    abort();
  }
  --entry->pins;
  if (!entry->pins && entry->retire) {
    retire = entry->retire;
    task_recycle(entry, shard, cap);
  }
  spin_unlock_at(&g_frame_locks[shard].lock);
  if (retire)
    retire(frame);
}
static void task_cancel_chain_locked(zan_task_entry *entry, size_t *shard_io,
                                     size_t *cap_io) {
  /* Bound traversal even if a malformed await chain contains a cycle.
   * Hand-over-hand trylock preserves deadlock freedom; sequence validation
   * prevents ABA when falling back to re-acquire out-of-order shards. */
  size_t remaining = __atomic_load_n(&g_task_count, __ATOMIC_RELAXED) + 64;
  while (entry && !entry->retire && remaining--) {
    if (entry->cancel)
      __atomic_store_n(entry->cancel, 1, __ATOMIC_RELEASE);
    zan_task_entry *next = entry->child;
    if (!next)
      break;
    void *next_frame = next->frame;
    uint64_t next_seq = next->seq;
    size_t next_shard = frame_shard_for(next_frame, *cap_io);
    if (next_shard == *shard_io) {
      entry = next;
      continue;
    }
    if (spin_trylock_at(&g_frame_locks[next_shard].lock)) {
      spin_unlock_at(&g_frame_locks[*shard_io].lock);
      *shard_io = next_shard;
      entry = next;
      continue;
    }
    spin_unlock_at(&g_frame_locks[*shard_io].lock);
    *shard_io = lock_frame_shard(next_frame, cap_io);
    zan_task_entry *found = task_frame_cap(next_frame, *cap_io);
    if (!found || found != next || found->seq != next_seq || found->retire)
      break;
    entry = found;
  }
}
void zan_task_cancel(uint64_t token) {
  if (!token)
    return;
  size_t cap = 0;
  size_t tshard = lock_token_shard(token, &cap);
  if (tshard == ZAN_TASK_SHARDS)
    return;
  zan_task_entry *entry = task_find_cap(token, cap);
  if (!entry) {
    spin_unlock_at(&g_token_locks[tshard].lock);
    return;
  }
  void *frame = entry->frame;
  uint64_t seq = entry->seq;
  spin_unlock_at(&g_token_locks[tshard].lock);

  size_t fcap = 0;
  size_t fshard = lock_frame_shard(frame, &fcap);
  if (entry->frame == frame && entry->seq == seq &&
      entry->generation == token && !entry->retire)
    task_cancel_chain_locked(entry, &fshard, &fcap);
  spin_unlock_at(&g_frame_locks[fshard].lock);
}
void zan_task_frame_cancel(void *frame) {
  if (!frame)
    return;
  size_t cap = 0;
  size_t shard = lock_frame_shard(frame, &cap);
  zan_task_entry *entry = task_frame_cap(frame, cap);
  if (entry && !entry->retire)
    task_cancel_chain_locked(entry, &shard, &cap);
  spin_unlock_at(&g_frame_locks[shard].lock);
}
void zan_task_child(void *parent, void *child) {
  size_t cap = 0;
  size_t s_p = lock_frame_shard(parent, &cap);
  zan_task_entry *p = task_frame_cap(parent, cap);
  if (!p || p->retire) {
    spin_unlock_at(&g_frame_locks[s_p].lock);
    abort();
  }
  /* Fast path: clearing child on await resume (child == NULL). */
  if (!child) {
    zan_task_entry *old_c = p->child;
    if (!old_c) {
      spin_unlock_at(&g_frame_locks[s_p].lock);
      return;
    }
    size_t s_old = frame_shard_for(old_c->frame, cap);
    if (s_old == s_p) {
      task_unlink_child(p);
      spin_unlock_at(&g_frame_locks[s_p].lock);
      return;
    }
    if (s_old > s_p) {
      spin_lock_at(&g_frame_locks[s_old].lock);
      task_unlink_child(p);
      spin_unlock_at(&g_frame_locks[s_old].lock);
      spin_unlock_at(&g_frame_locks[s_p].lock);
      return;
    }
  }

  /* Fast path: setting initial child when parent has no prior child. */
  if (!p->child) {
    size_t s_c = frame_shard_for(child, cap);
    if (s_c == s_p || s_c > s_p) {
      if (s_c > s_p)
        spin_lock_at(&g_frame_locks[s_c].lock);
      if (__atomic_load_n(&g_task_capacity, __ATOMIC_ACQUIRE) == cap) {
        zan_task_entry *c = task_frame_cap(child, cap);
        if (!c || c->retire) {
          if (s_c > s_p)
            spin_unlock_at(&g_frame_locks[s_c].lock);
          spin_unlock_at(&g_frame_locks[s_p].lock);
          abort();
        }
        p->child = c;
        p->parent_next = c->parents;
        p->parent_link = &c->parents;
        if (c->parents)
          c->parents->parent_link = &p->parent_next;
        c->parents = p;
        int should_cancel =
            p->cancel && __atomic_load_n(p->cancel, __ATOMIC_ACQUIRE);
        if (s_c > s_p)
          spin_unlock_at(&g_frame_locks[s_c].lock);
        spin_unlock_at(&g_frame_locks[s_p].lock);
        if (should_cancel)
          zan_task_frame_cancel(child);
        return;
      }
      if (s_c > s_p)
        spin_unlock_at(&g_frame_locks[s_c].lock);
    }
  }

  void *old_frame = p->child ? p->child->frame : NULL;
  spin_unlock_at(&g_frame_locks[s_p].lock);

  for (;;) {
    size_t cur_cap = __atomic_load_n(&g_task_capacity, __ATOMIC_ACQUIRE);
    size_t shards[3];
    size_t n = 0;
    shards[n++] = frame_shard_for(parent, cur_cap);
    if (old_frame)
      shards[n++] = frame_shard_for(old_frame, cur_cap);
    if (child)
      shards[n++] = frame_shard_for(child, cur_cap);
    lock_sorted_shards(shards, n);

    if (__atomic_load_n(&g_task_capacity, __ATOMIC_ACQUIRE) != cur_cap) {
      unlock_sorted_shards(shards, n);
      continue;
    }
    p = task_frame_cap(parent, cur_cap);
    if (!p || p->retire) {
      unlock_sorted_shards(shards, n);
      abort();
    }
    void *now_old = p->child ? p->child->frame : NULL;
    if (now_old != old_frame) {
      old_frame = now_old;
      unlock_sorted_shards(shards, n);
      continue;
    }
    zan_task_entry *c = child ? task_frame_cap(child, cur_cap) : NULL;
    if (child && (!c || c->retire)) {
      unlock_sorted_shards(shards, n);
      abort();
    }
    task_unlink_child(p);
    if (c) {
      p->child = c;
      p->parent_next = c->parents;
      p->parent_link = &c->parents;
      if (c->parents)
        c->parents->parent_link = &p->parent_next;
      c->parents = p;
    }
    int should_cancel =
        c && p->cancel && __atomic_load_n(p->cancel, __ATOMIC_ACQUIRE);
    unlock_sorted_shards(shards, n);
    if (should_cancel)
      zan_task_frame_cancel(child);
    return;
  }
}
/* Only after the scheduler gives up its final QUEUED/RUNNING owner. Retiring
 * a raw child clears all incoming edges before any address can be reused. */
void zan_task_retire(void *frame, void (*release)(void *)) {
  if (!frame)
    return;
  int deferred = 0;
  size_t cap = 0;
  size_t s_f = lock_frame_shard(frame, &cap);
  for (;;) {
    zan_task_entry *entry = task_frame_cap(frame, cap);
    if (!entry) {
      spin_unlock_at(&g_frame_locks[s_f].lock);
      if (release)
        release(frame);
      return;
    }
    if (entry->retire) {
      spin_unlock_at(&g_frame_locks[s_f].lock);
      return;
    }
    if (entry->parents) {
      zan_task_entry *p = entry->parents;
      size_t s_p = frame_shard_for(p->frame, cap);
      if (s_p == s_f) {
        task_unlink_child(p);
        continue;
      }
      if (s_p > s_f) {
        spin_lock_at(&g_frame_locks[s_p].lock);
        task_unlink_child(p);
        spin_unlock_at(&g_frame_locks[s_p].lock);
        continue;
      }
      void *pf = p->frame;
      spin_unlock_at(&g_frame_locks[s_f].lock);
      lock_two_frame_shards(pf, frame, &s_p, &s_f, &cap);
      entry = task_frame_cap(frame, cap);
      if (!entry || entry->retire) {
        spin_unlock_at(&g_frame_locks[s_f].lock);
        if (s_p != s_f)
          spin_unlock_at(&g_frame_locks[s_p].lock);
        return;
      }
      zan_task_entry *p2 = task_frame_cap(pf, cap);
      if (p2 && p2->child == entry)
        task_unlink_child(p2);
      if (s_p != s_f)
        spin_unlock_at(&g_frame_locks[s_p].lock);
      continue;
    }
    if (entry->child) {
      zan_task_entry *c = entry->child;
      size_t s_c = frame_shard_for(c->frame, cap);
      if (s_c == s_f) {
        task_unlink_child(entry);
        continue;
      }
      if (s_c > s_f) {
        spin_lock_at(&g_frame_locks[s_c].lock);
        task_unlink_child(entry);
        spin_unlock_at(&g_frame_locks[s_c].lock);
        continue;
      }
      void *cf = c->frame;
      spin_unlock_at(&g_frame_locks[s_f].lock);
      lock_two_frame_shards(cf, frame, &s_c, &s_f, &cap);
      entry = task_frame_cap(frame, cap);
      if (!entry || entry->retire) {
        spin_unlock_at(&g_frame_locks[s_f].lock);
        if (s_c != s_f)
          spin_unlock_at(&g_frame_locks[s_c].lock);
        return;
      }
      if (entry->child && entry->child->frame == cf)
        task_unlink_child(entry);
      if (s_c != s_f)
        spin_unlock_at(&g_frame_locks[s_c].lock);
      continue;
    }
    task_forget_token_locked(entry, cap);
    if (entry->allocator_release)
      release = entry->allocator_release;
    if (entry->pins) {
      entry->retire = release;
      deferred = 1;
    } else {
      task_recycle(entry, s_f, cap);
    }
    break;
  }
  spin_unlock_at(&g_frame_locks[s_f].lock);
  if (!deferred)
    release(frame);
}
size_t zan_task_live_count(void) {
  return __atomic_load_n(&g_task_count, __ATOMIC_ACQUIRE);
}
size_t zan_task_slot_capacity(void) {
  return __atomic_load_n(&g_task_capacity, __ATOMIC_ACQUIRE);
}
