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
#include <time.h>
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

#define TASK_SHARD_BITS 6
#define TASK_SHARDS (1u << TASK_SHARD_BITS)

typedef struct {
  uint64_t generation;
  uint64_t incarnation; /* never 0 for a live entry */
  void *frame;
  const volatile int *done;
  volatile int *cancel;
  void *child;
  uint64_t child_incarnation;
  size_t pins;
  void (*retire)(void *);
  void (*allocator_release)(void *);
  void (*discard)(void *);
  zan_task_profile *profile;
  int pending_wait;
  int configured;
  int inherited_stage;
  uint64_t open_stages;
} zan_task_entry;

typedef struct {
  _Alignas(64) volatile int lock;
  zan_task_entry *entries;
  size_t capacity;
  /* Open-addressing index (slot+1, 0 = empty; linear probing, backward-shift
   * deletion) and a free-slot stack. Indices stay valid across realloc. */
  uint32_t *index;
  size_t index_cap; /* power of two, >= 2 * capacity */
  uint32_t *free_slots;
  size_t free_n;
  uint64_t next_incarnation;
} task_shard;
static task_shard g_task_shards[TASK_SHARDS];
static uint64_t g_task_generation;
static _Alignas(64) volatile int g_profile_lock;

static void task_lock(task_shard *s) {
  for (unsigned spin = 0;; spin++) {
    if (!__atomic_load_n(&s->lock, __ATOMIC_RELAXED) &&
        !__atomic_exchange_n(&s->lock, 1, __ATOMIC_ACQUIRE))
      return;
    if (spin < 128)
      task_cpu_relax();
    else
      task_os_yield();
  }
}
static void task_unlock(task_shard *s) {
  __atomic_store_n(&s->lock, 0, __ATOMIC_RELEASE);
}
static inline void profile_lock(void) {
  for (unsigned spin = 0;; spin++) {
    if (!__atomic_load_n(&g_profile_lock, __ATOMIC_RELAXED) &&
        !__atomic_exchange_n(&g_profile_lock, 1, __ATOMIC_ACQUIRE))
      return;
    if (spin < 128)
      task_cpu_relax();
    else
      task_os_yield();
  }
}
static inline void profile_unlock(void) {
  __atomic_store_n(&g_profile_lock, 0, __ATOMIC_RELEASE);
}

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
  while (s->index[h])
    h = (h + 1) & mask;
  s->index[h] = (uint32_t)(slot + 1);
}
static void task_index_rebuild(task_shard *s, size_t cap) {
  uint32_t *idx = (uint32_t *)calloc(cap, sizeof(uint32_t));
  if (!idx)
    abort();
  free(s->index);
  s->index = idx;
  s->index_cap = cap;
  for (size_t i = 0; i < s->capacity; ++i)
    if (s->entries[i].frame)
      task_index_insert(s, i);
}
static size_t task_index_pos(task_shard *s, const void *frame) {
  if (!s->index_cap)
    return SIZE_MAX;
  size_t mask = s->index_cap - 1;
  size_t h = (size_t)task_mix(frame) & mask;
  for (;;) {
    uint32_t v = s->index[h];
    if (!v)
      return SIZE_MAX;
    if (s->entries[v - 1].frame == frame)
      return h;
    h = (h + 1) & mask;
  }
}
static void task_index_remove(task_shard *s, const void *frame) {
  size_t pos = task_index_pos(s, frame);
  if (pos == SIZE_MAX)
    return;
  size_t mask = s->index_cap - 1;
  s->index[pos] = 0;
  /* Backward-shift: re-seat the rest of the probe run. */
  size_t j = (pos + 1) & mask;
  while (s->index[j]) {
    uint32_t v = s->index[j];
    s->index[j] = 0;
    size_t h = (size_t)task_mix(s->entries[v - 1].frame) & mask;
    while (s->index[h])
      h = (h + 1) & mask;
    s->index[h] = v;
    j = (j + 1) & mask;
  }
}
static zan_task_entry *task_find(task_shard *s, uint64_t generation) {
  if (!generation)
    return NULL;
  for (size_t i = 0; i < s->capacity; ++i)
    if (s->entries[i].generation == generation && s->entries[i].frame)
      return &s->entries[i];
  return NULL;
}
static zan_task_entry *task_frame(task_shard *s, void *frame) {
  if (!frame)
    return NULL;
  size_t pos = task_index_pos(s, frame);
  if (pos == SIZE_MAX)
    return NULL;
  return &s->entries[s->index[pos] - 1];
}
static zan_task_entry *task_slot(task_shard *s, void *frame) {
  zan_task_entry *entry = task_frame(s, frame);
  if (entry)
    return entry;
  if (!s->free_n) {
    size_t next = s->capacity ? s->capacity * 2 : 16;
    if (next < s->capacity || next > UINT32_MAX - 1 ||
        next > SIZE_MAX / sizeof(zan_task_entry))
      abort();
    zan_task_entry *entries =
        (zan_task_entry *)realloc(s->entries, next * sizeof(zan_task_entry));
    if (!entries)
      abort();
    memset(entries + s->capacity, 0,
           (next - s->capacity) * sizeof(zan_task_entry));
    uint32_t *fl = (uint32_t *)realloc(s->free_slots, next * sizeof(uint32_t));
    if (!fl)
      abort();
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
  entry->incarnation =
      ++s->next_incarnation * TASK_SHARDS + (uint64_t)(s - g_task_shards);
  task_index_insert(s, i);
  return entry;
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
    task_shard *s = task_shard_of(frame);
    task_lock(s);
    zan_task_entry *entry = task_frame(s, frame);
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
    task_unlock(s);
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
  task_shard *s = task_shard_of(frame);
  task_lock(s);
  zan_task_entry *entry = task_frame(s, frame);
  profile_lock();
  if (entry && entry->profile == slice->profile)
    task_profile_detach_locked(entry);
  task_profile_release(slice->profile);
  slice->profile = NULL;
  profile_unlock();
  task_unlock(s);
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
  task_shard *s = task_shard_of(slice->frame);
  task_lock(s);
  zan_task_entry *entry = task_frame(s, slice->frame);
  if (!entry || entry->retire || entry->profile) {
    task_unlock(s);
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
  task_unlock(s);
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
  task_shard *s = task_shard_of(slice->frame);
  task_lock(s);
  zan_task_entry *entry = task_frame(s, slice->frame);
  if (entry && entry->profile == slice->profile)
    entry->open_stages = next;
  task_unlock(s);
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
  task_shard *s = task_shard_of(frame);
  task_lock(s);
  zan_task_entry *entry = task_frame(s, frame);
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
  task_unlock(s);
}

void *zan_task_profile_end(int64_t *cpu, int64_t *bytes, int64_t *slices,
                           int64_t *unfinished) {
  zan_task_profile_slice *slice = g_profile_slice;
  if (!slice || !slice->profile || slice->profile->root != slice->frame)
    return NULL;
  task_profile_pause(slice);
  task_shard *s = task_shard_of(slice->frame);
  task_lock(s);
  zan_task_entry *entry = task_frame(s, slice->frame);
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
  task_unlock(s);
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

/* Release a slot: unindex, detach profiler if attached, zero, free-list.
 * Edges naming it go stale. */
static void task_drop(task_shard *s, zan_task_entry *entry) {
  if (entry->profile) {
    profile_lock();
    task_profile_detach_locked(entry);
    profile_unlock();
  }
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
 * than inferred from a driver-specific frame layout. NULL leaves a field alone.
 */
void zan_task_frame_config(void *frame, const volatile int *done,
                           volatile int *cancel) {
  if (!frame)
    abort();
  task_shard *s = task_shard_of(frame);
  task_lock(s);
  zan_task_entry *entry = task_slot(s, frame);
  if (entry->retire) {
    task_unlock(s);
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
  task_unlock(s);
}
/* Record allocation provenance before publishing the frame. Retirement never
 * probes bytes before an address to guess which allocator owns it. */
void zan_task_frame_allocator(void *frame, void (*release)(void *)) {
  if (!frame || !release)
    abort();
  task_shard *s = task_shard_of(frame);
  task_lock(s);
  zan_task_entry *entry = task_slot(s, frame);
  if (entry->retire || entry->allocator_release) {
    task_unlock(s);
    abort();
  }
  entry->allocator_release = release;
  task_unlock(s);
}
void zan_task_discard_config(void *frame, void (*discard)(void *)) {
  if (!frame || !discard)
    abort();
  task_shard *s = task_shard_of(frame);
  task_lock(s);
  zan_task_entry *entry = task_slot(s, frame);
  if (entry->retire) {
    task_unlock(s);
    abort();
  }
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
    task_unlock(s);
    abort();
  }
  void (*discard)(void *) = entry->discard;
  entry->discard = NULL;
  task_unlock(s);
  discard(frame);
}
uint64_t zan_task_register(void *frame, const volatile int *done) {
  if (!frame || !done)
    abort();
  task_shard *s = task_shard_of(frame);
  /* Exhaustion is fatal, never wrap to a generation a saved long can name. */
  uint64_t seq = __atomic_add_fetch(&g_task_generation, 1, __ATOMIC_RELAXED);
  if (seq >= (uint64_t)INT64_MAX / TASK_SHARDS)
    abort();
  uint64_t token = seq * TASK_SHARDS + (uint64_t)(s - g_task_shards);
  task_lock(s);
  zan_task_entry *entry = task_slot(s, frame);
  if (entry->generation || entry->retire) {
    task_unlock(s);
    abort();
  }
  entry->generation = token;
  entry->done = done;
  task_unlock(s);
  return token;
}
int zan_task_isdone(uint64_t token) {
  if (!token)
    return 1;
  task_shard *s = task_shard_of_token(token);
  task_lock(s);
  zan_task_entry *entry = task_find(s, token);
  int done = !entry || entry->retire ||
             __atomic_load_n(entry->done, __ATOMIC_ACQUIRE) != 0;
  task_unlock(s);
  return done;
}
void *zan_task_acquire(uint64_t token, int consume) {
  if (!token)
    return NULL;
  task_shard *s = task_shard_of_token(token);
  task_lock(s);
  zan_task_entry *entry = task_find(s, token);
  void *frame = NULL;
  if (entry && !entry->retire) {
    ++entry->pins;
    frame = entry->frame;
    if (consume)
      entry->generation = 0;
  }
  task_unlock(s);
  return frame;
}
void zan_task_unpin(void *frame) {
  if (!frame)
    return;
  void (*retire)(void *) = NULL;
  task_shard *s = task_shard_of(frame);
  task_lock(s);
  zan_task_entry *entry = task_frame(s, frame);
  if (!entry || !entry->pins) {
    task_unlock(s);
    abort();
  }
  --entry->pins;
  if (!entry->pins && entry->retire) {
    retire = entry->retire;
    task_drop(s, entry);
  }
  task_unlock(s);
  if (retire)
    retire(frame);
}
/* Mark `entry` (held under `s`, which this releases) cancelled, then follow
 * live await edges one shard at a time. A malformed cycle must not hang
 * cancellation: valid await edges form a tree, so the walk is bounded by the
 * slot count. */
static void task_cancel_chain_locked(task_shard *s, zan_task_entry *entry) {
  size_t remaining = task_capacity_bound();
  for (;;) {
    if (!entry || entry->retire) {
      task_unlock(s);
      return;
    }
    if (entry->cancel)
      __atomic_store_n(entry->cancel, 1, __ATOMIC_RELEASE);
    void *child = entry->child;
    uint64_t incarnation = entry->child_incarnation;
    task_unlock(s);
    if (!child || !remaining--)
      return;
    s = task_shard_of(child);
    task_lock(s);
    entry = task_frame(s, child);
    if (entry && entry->incarnation != incarnation)
      entry = NULL;
  }
}
void zan_task_cancel(uint64_t token) {
  if (!token)
    return;
  task_shard *s = task_shard_of_token(token);
  task_lock(s);
  task_cancel_chain_locked(s, task_find(s, token));
}
void zan_task_frame_cancel(void *frame) {
  if (!frame)
    return;
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
    if (!c || c->retire) {
      task_unlock(cs);
      abort();
    }
    incarnation = c->incarnation;
    task_unlock(cs);
  }
  task_shard *ps = task_shard_of(parent);
  task_lock(ps);
  zan_task_entry *p = task_frame(ps, parent);
  if (!p || p->retire) {
    task_unlock(ps);
    abort();
  }
  p->child = child;
  p->child_incarnation = incarnation;
  int cancelled = p->cancel && __atomic_load_n(p->cancel, __ATOMIC_ACQUIRE);
  task_unlock(ps);
  if (cancelled && child) {
    task_lock(cs);
    zan_task_entry *c = task_frame(cs, child);
    if (c && c->incarnation != incarnation)
      c = NULL;
    task_cancel_chain_locked(cs, c);
  }
}
/* Only after the scheduler gives up its final QUEUED/RUNNING owner. Edges that
 * still name this frame go stale with its incarnation, so no reuse of the
 * address can be reached through them. */
void zan_task_retire(void *frame, void (*release)(void *)) {
  if (!frame)
    return;
  int deferred = 0;
  task_shard *s = task_shard_of(frame);
  task_lock(s);
  zan_task_entry *entry = task_frame(s, frame);
  if (entry) {
    entry->generation = 0;
    entry->child = NULL;
    entry->child_incarnation = 0;
    if (entry->retire) {
      task_unlock(s);
      return;
    }
    if (entry->allocator_release)
      release = entry->allocator_release;
    if (entry->pins) {
      entry->retire = release;
      deferred = 1;
    } else {
      task_drop(s, entry);
    }
  }
  task_unlock(s);
  if (!deferred && release)
    release(frame);
}
size_t zan_task_live_count(void) {
  size_t count = 0;
  for (unsigned k = 0; k < TASK_SHARDS; ++k) {
    task_shard *s = &g_task_shards[k];
    task_lock(s);
    for (size_t i = 0; i < s->capacity; ++i)
      if (s->entries[i].frame)
        ++count;
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
