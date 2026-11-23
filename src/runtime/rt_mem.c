
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif

#if (defined(__linux__) || defined(__unix__)) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/zan_abi.h"
#include "rt_timer.h"

#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
#include <sys/mman.h>
#include <unistd.h>
#define ZAN_MEM_HAVE_MMAP 1
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif
#if defined(_WIN32)
#include <windows.h>
#endif

/* PE import thunks can themselves be rewritten by LLD --wrap. Resolve the
 * native CRT entry points directly so internal allocator metadata never
 * re-enters __wrap_malloc through a rewritten __real_malloc import thunk. */
#if defined(_WIN32)
static INIT_ONCE g_native_alloc_once = INIT_ONCE_STATIC_INIT;
static void *(__cdecl *g_native_malloc)(size_t);
static void(__cdecl *g_native_free)(void *);
static void *(__cdecl *g_native_calloc)(size_t, size_t);
static void *(__cdecl *g_native_realloc)(void *, size_t);
static BOOL CALLBACK zan_mem_init_native_alloc(PINIT_ONCE once, PVOID arg,
                                               PVOID *context) {
  (void)once;
  (void)arg;
  (void)context;
  /* MinGW 链接的宿主挂 msvcrt.dll；clang/MSVC-CRT（libcmt/ucrt）宿主
   * 没有 msvcrt——UCRT 的 malloc/free/calloc/realloc 同样是进程真实
   * 分配器，必须回退，否则非 MinGW 宿主启动即 ExitProcess(127)
   * （探针：zanrt_mem_recommit_test 在 MSVC-CRT 构建下无输出即退）。
   * 静态 CRT 宿主连 ucrtbase 都没挂：LoadLibrary 显式载入，native
   * 堆自洽成对（分配与释放都走同一组函数），不与宿主 CRT 混用。 */
  HMODULE crt = GetModuleHandleA("msvcrt.dll");
  if (!crt)
    crt = GetModuleHandleA("ucrtbase.dll");
  if (!crt)
    crt = LoadLibraryA("ucrtbase.dll");
  if (!crt)
    return FALSE;
  g_native_malloc = (void *(__cdecl *)(size_t))GetProcAddress(crt, "malloc");
  g_native_free = (void(__cdecl *)(void *))GetProcAddress(crt, "free");
  g_native_calloc =
      (void *(__cdecl *)(size_t, size_t))GetProcAddress(crt, "calloc");
  g_native_realloc =
      (void *(__cdecl *)(void *, size_t))GetProcAddress(crt, "realloc");
  return g_native_malloc && g_native_free && g_native_calloc &&
         g_native_realloc;
}
static void zan_mem_require_native_alloc(void) {
  if (!InitOnceExecuteOnce(&g_native_alloc_once, zan_mem_init_native_alloc,
                           NULL, NULL))
    ExitProcess(127);
}
static void *zan_mem_crt_malloc(size_t n) {
  zan_mem_require_native_alloc();
  return g_native_malloc(n);
}
static void zan_mem_crt_free(void *p) {
  zan_mem_require_native_alloc();
  g_native_free(p);
}
static void *zan_mem_crt_calloc(size_t n, size_t m) {
  zan_mem_require_native_alloc();
  return g_native_calloc(n, m);
}
static void *zan_mem_crt_realloc(void *p, size_t n) {
  zan_mem_require_native_alloc();
  return g_native_realloc(p, n);
}
#define __real_malloc zan_mem_crt_malloc
#define __real_free zan_mem_crt_free
#define __real_calloc zan_mem_crt_calloc
#define __real_realloc zan_mem_crt_realloc
#else
/* ELF's real allocator symbols are supplied by the linker wrap. */
void *__real_malloc(size_t n);
void __real_free(void *p);
void *__real_calloc(size_t n, size_t m);
void *__real_realloc(void *p, size_t n);
#endif

/* Internal metadata is released with __real_free. Static musl's calloc can
 * call wrapped malloc, so __real_calloc does not guarantee native provenance.
 */
static void *zan_mem_native_calloc(size_t n, size_t size) {
  if (size && n > SIZE_MAX / size)
    return NULL;
  size_t bytes = n * size;
  void *p = __real_malloc(bytes ? bytes : 1);
  if (p)
    memset(p, 0, bytes);
  return p;
}

#define ZAN_MEM_MAX_SMALL 2048u
#define ZAN_MEM_SLAB (1u << 20) /* 1 MiB */

typedef struct {
  uint32_t magic;
  uint32_t cls;
  struct zan_mem_cache *owner;
} zan_mem_hdr_t;

_Static_assert(sizeof(zan_mem_hdr_t) == ZAN_OBJ_HDR_SIZE,
               "allocator header must keep payloads 16-byte aligned "
               "for the compiler's object header (zan_abi.h)");

#define ZAN_MEM_MAGIC 0x5A414E4DU /* "ZANM" */
#define ZAN_MEM_FREED 0x5A414E46U
#define ZAN_MEM_HDR ((size_t)sizeof(zan_mem_hdr_t))

static const uint16_t k_class_size[] = {
    16,  32,  48,  64,  80,  96,  112, 128,  160,  192,  224,  256,
    320, 384, 448, 512, 640, 768, 896, 1024, 1280, 1536, 1792, 2048};
#define ZAN_MEM_NCLASS ((int)(sizeof(k_class_size) / sizeof(k_class_size[0])))

static unsigned char g_size_class[ZAN_MEM_MAX_SMALL + 1];
static int g_class_ready;

#define ZAN_MEM_SET_SIZE (1u << 14)
#define ZAN_MEM_SET_MASK (ZAN_MEM_SET_SIZE - 1u)
static uintptr_t g_slab_set[ZAN_MEM_SET_SIZE];
static size_t g_slab_count;
static size_t g_small_alloc_count;
static size_t g_trimmed_bytes;

size_t zan_mem_slabs(void) {
  return __atomic_load_n(&g_slab_count, __ATOMIC_RELAXED);
}

void zan_mem_get_stats(zan_mem_stats_t *out) {
  if (!out)
    return;
  size_t slabs = __atomic_load_n(&g_slab_count, __ATOMIC_RELAXED);
  out->slab_count = slabs;
  out->slab_bytes = slabs * (size_t)ZAN_MEM_SLAB;
  out->small_alloc_count =
      __atomic_load_n(&g_small_alloc_count, __ATOMIC_RELAXED);
  out->trimmed_bytes = __atomic_load_n(&g_trimmed_bytes, __ATOMIC_RELAXED);
}

#define ZAN_MEM_REMOTE_STRIPES 8

typedef struct zan_mem_cache {
  void *free_list[ZAN_MEM_NCLASS]; /* owner-only */
  char *bump;                      /* owner-only */
  char *bump_end;                  /* owner-only */
  int bump_decommitted; /* unallocated bump pages decommitted to OS */
  struct {
    void *head;
    char pad[64 - sizeof(void *)];
  } remote[ZAN_MEM_REMOTE_STRIPES];
  struct zan_mem_cache *next_free;
} zan_mem_cache;

static zan_mem_cache *g_cache_pool;

static unsigned g_remote_rr;

static void zan_mem_retire(zan_mem_cache *c);
static void zan_mem_drain_remote(zan_mem_cache *c);

#if defined(_WIN32)
static _Thread_local zan_mem_cache *t_cache;
static DWORD g_fls = FLS_OUT_OF_INDEXES;
static int g_fls_state;
static void WINAPI zan_mem_fls_cb(void *p) {
  t_cache = NULL;
  if (p)
    zan_mem_retire((zan_mem_cache *)p);
}
#else
#include <pthread.h>
#include <sched.h>
static __thread zan_mem_cache *t_cache;
static pthread_key_t g_exit_key;
static int g_exit_key_state;
static void zan_mem_thread_exit(void *p) {
  t_cache = NULL;
  if (p)
    zan_mem_retire((zan_mem_cache *)p);
}
#endif

static volatile int g_slab_lock;

static void zan_mem_backoff(int spins) {
  if (spins < 64) {
#if defined(_WIN32)
    YieldProcessor();
#elif defined(__i386__) || defined(__x86_64__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield" ::: "memory");
#elif defined(__riscv)
    __asm__ __volatile__(".insn i 0x0F, 0, x0, x0, 0x010" ::: "memory");
#endif
    return;
  }
#if defined(_WIN32)
  SwitchToThread();
#else
  sched_yield();
#endif
}

static void zan_mem_lock(void) {
  for (int spins = 0;; spins++) {
    if (!__sync_lock_test_and_set(&g_slab_lock, 1))
      return;
    zan_mem_backoff(spins);
  }
}

static void zan_mem_unlock(void) { __sync_lock_release(&g_slab_lock); }

static void zan_mem_build_classes(void) {
  int c = 0;
  for (unsigned s = 0; s <= ZAN_MEM_MAX_SMALL; s++) {
    while (c < ZAN_MEM_NCLASS && k_class_size[c] < s)
      c++;
    g_size_class[s] =
        (unsigned char)(c < ZAN_MEM_NCLASS ? c : ZAN_MEM_NCLASS - 1);
  }
  __atomic_store_n(&g_class_ready, 1, __ATOMIC_RELEASE);
}

static void zan_mem_ensure_classes(void) {
  if (__atomic_load_n(&g_class_ready, __ATOMIC_ACQUIRE))
    return;
  zan_mem_lock();
  if (!g_class_ready)
    zan_mem_build_classes();
  zan_mem_unlock();
}

static unsigned zan_mem_hash(uintptr_t base) {
  uint64_t h = (uint64_t)(base >> 20) * 0x9E3779B97F4A7C15ull;
  return (unsigned)(h >> 40) & ZAN_MEM_SET_MASK;
}

static int zan_mem_owns(const void *p) {
  if (!__atomic_load_n(&g_slab_count, __ATOMIC_ACQUIRE))
    return 0;
  uintptr_t base = (uintptr_t)p & ~(uintptr_t)(ZAN_MEM_SLAB - 1);
  unsigned i = zan_mem_hash(base);
  for (unsigned n = 0; n < 64; n++) {
    uintptr_t v = __atomic_load_n(&g_slab_set[(i + n) & ZAN_MEM_SET_MASK],
                                  __ATOMIC_ACQUIRE);
    if (v == base)
      return 1;
    if (v == 0)
      return 0;
  }
  return 0;
}

#if defined(_WIN32)
static inline DWORD zan_mem_fls_slot(void) {
  static DWORD s_fls = FLS_OUT_OF_INDEXES;
  if (__builtin_expect(s_fls != FLS_OUT_OF_INDEXES, 1))
    return s_fls;

  int fst = __atomic_load_n(&g_fls_state, __ATOMIC_ACQUIRE);
  if (fst == 0 &&
      __atomic_compare_exchange_n(&g_fls_state, &fst, 1, 0, __ATOMIC_ACQ_REL,
                                  __ATOMIC_ACQUIRE)) {
    DWORD idx = FlsAlloc(zan_mem_fls_cb);
    __atomic_store_n(&g_fls, idx, __ATOMIC_RELEASE);
    __atomic_store_n(&g_fls_state, idx == FLS_OUT_OF_INDEXES ? 3 : 2,
                     __ATOMIC_RELEASE);
    s_fls = idx;
    return s_fls;
  }
  while (__atomic_load_n(&g_fls_state, __ATOMIC_ACQUIRE) == 1) {
    YieldProcessor();
  }
  DWORD fls = __atomic_load_n(&g_fls, __ATOMIC_ACQUIRE);
  s_fls = fls;
  return fls;
}
#endif

static zan_mem_cache *zan_mem_cache_get(void) {
#if defined(_WIN32)
  zan_mem_cache *c = t_cache;
  if (c)
    return c;
  DWORD fls = zan_mem_fls_slot();
  if (fls == FLS_OUT_OF_INDEXES)
    return NULL;
#else
  zan_mem_cache *c = t_cache;
  if (c)
    return c;
  int st = __atomic_load_n(&g_exit_key_state, __ATOMIC_ACQUIRE);
  if (st == 0 &&
      __atomic_compare_exchange_n(&g_exit_key_state, &st, 1, 0,
                                  __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
    int ok = pthread_key_create(&g_exit_key, zan_mem_thread_exit) == 0;
    __atomic_store_n(&g_exit_key_state, ok ? 2 : 3, __ATOMIC_RELEASE);
    st = ok ? 2 : 3;
  } else {
    while ((st = __atomic_load_n(&g_exit_key_state, __ATOMIC_ACQUIRE)) == 1) {
#if defined(__i386__) || defined(__x86_64__)
      __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
      __asm__ __volatile__("yield" ::: "memory");
#elif defined(__riscv)
      __asm__ __volatile__(".insn i 0x0F, 0, x0, x0, 0x010" ::: "memory");
#endif
    }
  }
  if (st != 2)
    return NULL;
#endif
  zan_mem_lock();
  c = g_cache_pool;
  if (c)
    g_cache_pool = c->next_free;
  zan_mem_unlock();
  if (!c) {
    c = (zan_mem_cache *)__real_malloc(sizeof *c);
    if (!c)
      return NULL;
    memset(c, 0, sizeof *c);
  } else {
    c->next_free = NULL;
    zan_mem_drain_remote(c);
  }
#if defined(_WIN32)
  t_cache = c;
  FlsSetValue(fls, c);
#else
  t_cache = c;
  if (__atomic_load_n(&g_exit_key_state, __ATOMIC_ACQUIRE) == 2)
    pthread_setspecific(g_exit_key, c);
#endif
  return c;
}

static void zan_mem_drain_remote(zan_mem_cache *c) {
  for (int s = 0; s < ZAN_MEM_REMOTE_STRIPES; s++) {
    void *p = __atomic_exchange_n(&c->remote[s].head, NULL, __ATOMIC_ACQUIRE);
    while (p) {
      void *next = *(void **)p;
      zan_mem_hdr_t *h = (zan_mem_hdr_t *)((char *)p - ZAN_MEM_HDR);
      uint32_t cls = __atomic_load_n(&h->cls, __ATOMIC_RELAXED);
      if (cls < (uint32_t)ZAN_MEM_NCLASS) {
        *(void **)p = c->free_list[cls];
        c->free_list[cls] = p;
      }
      p = next;
    }
  }
}

static void zan_mem_retire(zan_mem_cache *c) {
  zan_mem_drain_remote(c);
  if (c->bump && c->bump_end && !c->bump_decommitted) {
    uintptr_t start = ((uintptr_t)c->bump + 4095) & ~(uintptr_t)4095;
    uintptr_t end = (uintptr_t)c->bump_end & ~(uintptr_t)4095;
    if (end > start && (end - start) >= 65536) {
      size_t bytes = end - start;
#if defined(_WIN32)
      VirtualFree((void *)start, bytes, MEM_DECOMMIT);
#elif defined(ZAN_MEM_HAVE_MMAP)
      madvise((void *)start, bytes, MADV_DONTNEED);
#endif
      c->bump_decommitted = 1;
      __atomic_fetch_add(&g_trimmed_bytes, bytes, __ATOMIC_RELAXED);
    }
  }
  zan_mem_lock();
  c->next_free = g_cache_pool;
  g_cache_pool = c;
  zan_mem_unlock();
}

static void *zan_mem_harvest_free_block(zan_mem_cache *c, int cls) {
  if (!g_cache_pool)
    return NULL;
  zan_mem_lock();
  for (zan_mem_cache *rc = g_cache_pool; rc; rc = rc->next_free) {
    zan_mem_drain_remote(rc);
    void *p = rc->free_list[cls];
    if (p) {
      rc->free_list[cls] = NULL;
      c->free_list[cls] = *(void **)p;
      zan_mem_unlock();
      return p;
    }
  }
  zan_mem_unlock();
  return NULL;
}

static int zan_mem_adopt_retired_bump(zan_mem_cache *c, size_t need) {
  if (!g_cache_pool)
    return 0;
  zan_mem_lock();
  for (zan_mem_cache *rc = g_cache_pool; rc; rc = rc->next_free) {
    if ((size_t)(rc->bump_end - rc->bump) >= need) {
      c->bump = rc->bump;
      c->bump_end = rc->bump_end;
      c->bump_decommitted = rc->bump_decommitted;
      rc->bump = NULL;
      rc->bump_end = NULL;
      rc->bump_decommitted = 0;
      zan_mem_unlock();
      return 1;
    }
  }
  zan_mem_unlock();
  return 0;
}

static int zan_mem_publish_slab(zan_mem_cache *c, uintptr_t base) {
  zan_mem_lock();
  unsigned i = zan_mem_hash(base);
  unsigned n = 0;
  while (n < 64 && g_slab_set[(i + n) & ZAN_MEM_SET_MASK] != 0)
    n++;
  if (n == 64) {
    zan_mem_unlock();
    return 0;
  }
  __atomic_store_n(&g_slab_set[(i + n) & ZAN_MEM_SET_MASK], base,
                   __ATOMIC_RELEASE);
  __atomic_store_n(&g_slab_count, g_slab_count + 1, __ATOMIC_RELEASE);
  zan_mem_unlock();

  c->bump = (char *)base;
  c->bump_end = (char *)base + ZAN_MEM_SLAB;
  c->bump_decommitted = 0;
  return 1;
}

static int zan_mem_new_slab(zan_mem_cache *c) {
#if defined(_WIN32)
  size_t span = (size_t)ZAN_MEM_SLAB * 2;
  char *raw = (char *)VirtualAlloc(NULL, span, MEM_RESERVE, PAGE_NOACCESS);
  if (!raw)
    return 0;
  uintptr_t base =
      ((uintptr_t)raw + (ZAN_MEM_SLAB - 1)) & ~(uintptr_t)(ZAN_MEM_SLAB - 1);
  if (!VirtualAlloc((void *)base, ZAN_MEM_SLAB, MEM_COMMIT, PAGE_READWRITE)) {
    VirtualFree(raw, 0, MEM_RELEASE);
    return 0;
  }
  if (!zan_mem_publish_slab(c, base)) {
    VirtualFree(raw, 0, MEM_RELEASE);
    return 0;
  }
  return 1;
#elif !defined(ZAN_MEM_HAVE_MMAP)
  (void)c;
  return 0;
#else
  size_t span = (size_t)ZAN_MEM_SLAB * 2;
  char *raw = (char *)mmap(NULL, span, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (raw == MAP_FAILED)
    return 0;
  uintptr_t base =
      ((uintptr_t)raw + (ZAN_MEM_SLAB - 1)) & ~(uintptr_t)(ZAN_MEM_SLAB - 1);
  size_t head = (size_t)(base - (uintptr_t)raw);
  if (head)
    munmap(raw, head);
  size_t tail = span - head - ZAN_MEM_SLAB;
  if (tail)
    munmap((char *)(base + ZAN_MEM_SLAB), tail);

  if (!zan_mem_publish_slab(c, base)) {
    munmap((void *)base, ZAN_MEM_SLAB);
    return 0;
  }
  return 1;
#endif
}

static void *zan_mem_small(size_t n) {
  zan_mem_ensure_classes();
  zan_mem_cache *c = zan_mem_cache_get();
  if (!c)
    return NULL;
  int cls = g_size_class[n];
  void *p = c->free_list[cls];
  if (!p) {
    zan_mem_drain_remote(c);
    p = c->free_list[cls];
  }
  if (!p) {
    p = zan_mem_harvest_free_block(c, cls);
  }
  if (p) {
    if (p == c->free_list[cls]) {
      c->free_list[cls] = *(void **)p;
    }
    zan_mem_hdr_t *h = (zan_mem_hdr_t *)((char *)p - ZAN_MEM_HDR);
    __atomic_store_n(&h->cls, (uint32_t)cls, __ATOMIC_RELAXED);
    __atomic_store_n(&h->owner, c, __ATOMIC_RELAXED);
    __atomic_store_n(&h->magic, ZAN_MEM_MAGIC, __ATOMIC_RELEASE);
    __atomic_fetch_add(&g_small_alloc_count, 1, __ATOMIC_RELAXED);
    return p;
  }
  size_t need = (size_t)k_class_size[cls] + ZAN_MEM_HDR;
  if ((size_t)(c->bump_end - c->bump) < need) {
    if (!zan_mem_adopt_retired_bump(c, need)) {
      if (!zan_mem_new_slab(c))
        return NULL;
    }
  }
#if defined(_WIN32)
  if (c->bump_decommitted) {
    uintptr_t start = ((uintptr_t)c->bump + 4095) & ~(uintptr_t)4095;
    uintptr_t end = (uintptr_t)c->bump_end & ~(uintptr_t)4095;
    if (end > start) {
      if (!VirtualAlloc((void *)start, end - start, MEM_COMMIT, PAGE_READWRITE))
        return NULL;
    }
    c->bump_decommitted = 0;
  }
#else
  c->bump_decommitted = 0;
#endif
  zan_mem_hdr_t *h = (zan_mem_hdr_t *)c->bump;
  c->bump += need;
  __atomic_store_n(&h->cls, (uint32_t)cls, __ATOMIC_RELAXED);
  __atomic_store_n(&h->owner, c, __ATOMIC_RELAXED);
  __atomic_store_n(&h->magic, ZAN_MEM_MAGIC, __ATOMIC_RELEASE);
  __atomic_fetch_add(&g_small_alloc_count, 1, __ATOMIC_RELAXED);
  return (char *)h + ZAN_MEM_HDR;
}

size_t zan_mem_trim(void) {
  size_t total_trimmed = 0;
  zan_mem_lock();
  for (zan_mem_cache *rc = g_cache_pool; rc; rc = rc->next_free) {
    if (!rc->bump || !rc->bump_end || rc->bump_decommitted)
      continue;
    uintptr_t start = ((uintptr_t)rc->bump + 4095) & ~(uintptr_t)4095;
    uintptr_t end = (uintptr_t)rc->bump_end & ~(uintptr_t)4095;
    if (end > start) {
      size_t bytes = end - start;
#if defined(_WIN32)
      VirtualFree((void *)start, bytes, MEM_DECOMMIT);
#elif defined(ZAN_MEM_HAVE_MMAP)
      madvise((void *)start, bytes, MADV_DONTNEED);
#endif
      rc->bump_decommitted = 1;
      total_trimmed += bytes;
    }
  }
  zan_mem_unlock();

  zan_mem_cache *self = t_cache;
  if (self && self->bump && self->bump_end && !self->bump_decommitted) {
    uintptr_t start = ((uintptr_t)self->bump + 4095) & ~(uintptr_t)4095;
    uintptr_t end = (uintptr_t)self->bump_end & ~(uintptr_t)4095;
    if (end > start && (end - start) >= 65536) {
      size_t bytes = end - start;
#if defined(_WIN32)
      VirtualFree((void *)start, bytes, MEM_DECOMMIT);
#elif defined(ZAN_MEM_HAVE_MMAP)
      madvise((void *)start, bytes, MADV_DONTNEED);
#endif
      self->bump_decommitted = 1;
      total_trimmed += bytes;
    }
  }

  if (total_trimmed > 0) {
    __atomic_fetch_add(&g_trimmed_bytes, total_trimmed, __ATOMIC_RELAXED);
  }
  return total_trimmed;
}

void *__wrap_malloc(size_t n) {
  if (n == 0)
    n = 1;
  if (n <= ZAN_MEM_MAX_SMALL) {
    void *p = zan_mem_small(n);
    if (p)
      return p;
  }
  return __real_malloc(n);
}

static int zan_mem_hdr_check(const void *p, uint32_t *cls) {
  zan_mem_hdr_t *h = (zan_mem_hdr_t *)((const char *)p - ZAN_MEM_HDR);
  uint32_t magic = __atomic_load_n(&h->magic, __ATOMIC_ACQUIRE);
  if (magic == ZAN_MEM_FREED) {
    char msg[64];
    snprintf(msg, sizeof msg, "double free of block %p", p);
    zan_rt_fatal("mem", msg);
  }
  if (magic != ZAN_MEM_MAGIC)
    return -1;
  uint32_t c = __atomic_load_n(&h->cls, __ATOMIC_ACQUIRE);
  if (c >= (uint32_t)ZAN_MEM_NCLASS) {
    char msg[80];
    snprintf(msg, sizeof msg, "corrupt block header at %p (class %u)", p,
             (unsigned)c);
    zan_rt_fatal("mem", msg);
  }
  *cls = c;
  return 0;
}

void __wrap_free(void *p) {
  if (!p)
    return;
  if (!zan_mem_owns(p)) {
    __real_free(p);
    return;
  }
  uint32_t cls;
  if (zan_mem_hdr_check(p, &cls) != 0)
    return;
  zan_mem_hdr_t *h = (zan_mem_hdr_t *)((char *)p - ZAN_MEM_HDR);
  uint32_t expect = ZAN_MEM_MAGIC;
  if (!__atomic_compare_exchange_n(&h->magic, &expect, ZAN_MEM_FREED, 0,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
    if (expect == ZAN_MEM_FREED) {
      char msg[64];
      snprintf(msg, sizeof msg, "double free of block %p", p);
      zan_rt_fatal("mem", msg);
    }
    return;
  }
  zan_mem_cache *owner = __atomic_load_n(&h->owner, __ATOMIC_RELAXED);
  zan_mem_cache *self = t_cache;
  if (owner == self && self) {
    *(void **)p = self->free_list[cls];
    self->free_list[cls] = p;
    return;
  }
  if (!owner)
    return;
  unsigned s;
  if (self)
    s = (unsigned)(((uintptr_t)self >> 4) & (ZAN_MEM_REMOTE_STRIPES - 1));
  else
    s = (unsigned)(__atomic_fetch_add(&g_remote_rr, 1, __ATOMIC_RELAXED) &
                   (ZAN_MEM_REMOTE_STRIPES - 1));
  void **slot = &owner->remote[s].head;
  void *head = __atomic_load_n(slot, __ATOMIC_RELAXED);
  do {
    *(void **)p = head;
  } while (!__atomic_compare_exchange_n(slot, &head, p, 1, __ATOMIC_RELEASE,
                                        __ATOMIC_RELAXED));

  if (self && self != owner) {
    zan_mem_drain_remote(self);
  }
}

#if defined(__linux__)
/* Static --wrap=malloc bypasses musl's dynamic replacement detection. Its
 * calloc must not inspect mallocng metadata on a Zan-owned small block.
 * Returning false requests normal zeroing; native blocks retain the libc
 * fast path. The weak reference keeps this object usable with other libcs. */
extern int __real___malloc_allzerop(void *p) __attribute__((weak));
int __wrap___malloc_allzerop(void *p) {
  if (zan_mem_owns(p))
    return 0;
  return __real___malloc_allzerop ? __real___malloc_allzerop(p) : 0;
}
#endif

void *__wrap_calloc(size_t n, size_t m) {
  size_t total = n * m;
  if (n != 0 && total / n != m)
    return NULL; /* overflow */
  if (total <= ZAN_MEM_MAX_SMALL) {
    void *p = zan_mem_small(total ? total : 1);
    if (p) {
      memset(p, 0, total);
      return p;
    }
  }
  return __real_calloc(n, m);
}

void *__wrap_realloc(void *p, size_t n) {
  if (!p)
    return __wrap_malloc(n);
  if (n == 0) {
    __wrap_free(p);
    return NULL;
  }
  if (!zan_mem_owns(p))
    return __real_realloc(p, n);
  uint32_t cls;
  if (zan_mem_hdr_check(p, &cls) != 0)
    return NULL;
  size_t old = k_class_size[cls];
  if (n <= old)
    return p;
  void *np = __wrap_malloc(n);
  if (!np)
    return NULL;
  memcpy(np, p, old);
  __wrap_free(p);
  return np;
}

/* ---------- B-ID78 深树级联析构防护：延迟释放队列 ----------
 * 对象树深链（数千层链表/树）经 release 函数递归下钻会耗尽线程栈。
 * 编译器在 zan_rt_release_dyn 里对类释放深度计数，超过阈值把
 * (释放函数, 对象) 交到本队列，最外层出口或进程退出时迭代排空。
 * 对象此刻 rc 已为 0（死对象无人再持），仅延后析构体执行；drain
 * 中再触深的分支会重新入队，循环天然迭代化。队列全局共享 + 锁
 * 保护，释放函数在锁外执行（防 dtor 内再 push 自锁死）。多线程
 * 对 IR 侧深度计数的竞写最坏导致 drain 晚触发——atexit 安全网
 * 兜底排空，不丢释放。 */

typedef struct zan_defer_node {
  struct zan_defer_node *next;
  void (*fn)(void *);
  void *obj;
} zan_defer_node;

#if defined(_WIN32)
static SRWLOCK g_defer_lock = SRWLOCK_INIT;
#define ZAN_DEFER_LOCK_W() AcquireSRWLockExclusive(&g_defer_lock)
#define ZAN_DEFER_UNLOCK() ReleaseSRWLockExclusive(&g_defer_lock)
#else
#include <pthread.h>
static pthread_mutex_t g_defer_lock = PTHREAD_MUTEX_INITIALIZER;
#define ZAN_DEFER_LOCK_W() pthread_mutex_lock(&g_defer_lock)
#define ZAN_DEFER_UNLOCK() pthread_mutex_unlock(&g_defer_lock)
#endif

static zan_defer_node *g_defer_head;
static int g_defer_count;
static int g_defer_exit_hooked;

void zan_rt_defer_drain(void);

static void zan_rt_defer_at_exit(void) { zan_rt_defer_drain(); }

void zan_rt_defer_push(void *fn, void *obj) {
  zan_defer_node *n = (zan_defer_node *)__real_malloc(sizeof(*n));
  if (!n) {
    /* 队列节点分配失败：退回同步释放（与旧行为一致，深树极端
     * 场景叠加 OOM 时宁可冒栈深风险也不丢释放） */
    ((void (*)(void *))fn)(obj);
    return;
  }
  n->next = NULL;
  n->fn = (void (*)(void *))fn;
  n->obj = obj;
  ZAN_DEFER_LOCK_W();
  if (!g_defer_exit_hooked) {
    g_defer_exit_hooked = 1;
    atexit(zan_rt_defer_at_exit);
  }
  n->next = g_defer_head;
  g_defer_head = n;
  g_defer_count++;
  ZAN_DEFER_UNLOCK();
}

int zan_rt_defer_pending(void) {
  ZAN_DEFER_LOCK_W();
  int pend = g_defer_count > 0;
  ZAN_DEFER_UNLOCK();
  return pend;
}

void zan_rt_defer_drain(void) {
  for (;;) {
    void (*fn)(void *);
    void *obj;
    ZAN_DEFER_LOCK_W();
    zan_defer_node *n = g_defer_head;
    if (!n) {
      ZAN_DEFER_UNLOCK();
      return;
    }
    g_defer_head = n->next;
    g_defer_count--;
    ZAN_DEFER_UNLOCK();
    fn = n->fn;
    obj = n->obj;
    __real_free(n);
    /* 锁外执行：dtor 内再 push（深树再触阈值）不自锁死 */
    fn(obj);
  }
}

/* =========================================================================
 * B-ID89: 运行时活对象注册表、孤儿环探测器与堆快照导出
 * ========================================================================= */

/* A host may enable cycle scans after establishing control of all Zan heap
 * access. Every thread must enter before it can change the heap or its
 * reference counts and leave after cleanup, including any foreign callback or
 * destructor. Nested entries on a thread share one active slot. A host may also
 * scan after all mutators have left. Call at a boundary between heap
 * operations.
 *
 * The scan keeps the gate held to block new mutators. If another is active,
 * the scan returns -1 immediately rather than waiting for that thread, which
 * might itself be waiting for the caller. No scan runs without host opt-in. */
#if defined(_WIN32)
static SRWLOCK g_heap_gate = SRWLOCK_INIT;
#define ZAN_HEAP_GATE_LOCK() AcquireSRWLockExclusive(&g_heap_gate)
#define ZAN_HEAP_GATE_TRY_LOCK() TryAcquireSRWLockExclusive(&g_heap_gate)
#define ZAN_HEAP_GATE_UNLOCK() ReleaseSRWLockExclusive(&g_heap_gate)
#else
static pthread_mutex_t g_heap_gate = PTHREAD_MUTEX_INITIALIZER;
#define ZAN_HEAP_GATE_LOCK() pthread_mutex_lock(&g_heap_gate)
#define ZAN_HEAP_GATE_TRY_LOCK() (pthread_mutex_trylock(&g_heap_gate) == 0)
#define ZAN_HEAP_GATE_UNLOCK() pthread_mutex_unlock(&g_heap_gate)
#endif

static int g_heap_diagnostics_enabled;
static size_t g_heap_active_mutators;
static __thread size_t t_heap_mutator_depth;

void zan_rt_heap_diagnostics_enable(void) {
  ZAN_HEAP_GATE_LOCK();
  g_heap_diagnostics_enabled = 1;
  ZAN_HEAP_GATE_UNLOCK();
}

void zan_rt_heap_mutator_enter(void) {
  if (t_heap_mutator_depth++ != 0)
    return;
  ZAN_HEAP_GATE_LOCK();
  g_heap_active_mutators++;
  ZAN_HEAP_GATE_UNLOCK();
}

void zan_rt_heap_mutator_leave(void) {
  if (!t_heap_mutator_depth) {
    zan_rt_fatal("mem", "heap mutator leave without enter");
    return;
  }
  if (--t_heap_mutator_depth != 0)
    return;
  ZAN_HEAP_GATE_LOCK();
  g_heap_active_mutators--;
  ZAN_HEAP_GATE_UNLOCK();
}

static int zan_heap_scan_try_begin(void) {
  if (!ZAN_HEAP_GATE_TRY_LOCK())
    return 0;
  size_t own_slot = t_heap_mutator_depth != 0;
  if (g_heap_diagnostics_enabled && g_heap_active_mutators == own_slot)
    return 1;
  ZAN_HEAP_GATE_UNLOCK();
  return 0;
}

#define ZAN_LIVE_STRIPES 32

typedef enum {
  ZAN_LIVE_OBJECT,
  ZAN_LIVE_ARRAY,
  ZAN_LIVE_REF_ARRAY,
} zan_live_kind_t;

typedef struct zan_live_entry {
  void *obj;
  size_t size;
  uint64_t site;
  const char *source;
  size_t data_offset;
  zan_live_kind_t kind;
  struct zan_live_entry *next;
} zan_live_entry_t;

typedef struct {
#if defined(_WIN32)
  SRWLOCK lock;
#else
  pthread_mutex_t lock;
#endif
  zan_live_entry_t **buckets;
  size_t bucket_count;
  size_t count;
  zan_live_entry_t *free_list;
} zan_live_stripe_t;

static zan_live_stripe_t g_live_stripes[ZAN_LIVE_STRIPES];
static int g_live_stripes_inited = 0;

static inline void zan_live_lock_init(zan_live_stripe_t *s) {
#if defined(_WIN32)
  (void)s;
#else
  pthread_mutex_init(&s->lock, NULL);
#endif
}

static inline void zan_live_lock(zan_live_stripe_t *s) {
#if defined(_WIN32)
  AcquireSRWLockExclusive(&s->lock);
#else
  pthread_mutex_lock(&s->lock);
#endif
}

static inline void zan_live_unlock(zan_live_stripe_t *s) {
#if defined(_WIN32)
  ReleaseSRWLockExclusive(&s->lock);
#else
  pthread_mutex_unlock(&s->lock);
#endif
}

static void zan_live_lock_all(void) {
  for (int i = 0; i < ZAN_LIVE_STRIPES; i++)
    zan_live_lock(&g_live_stripes[i]);
}

static void zan_live_unlock_all(void) {
  for (int i = ZAN_LIVE_STRIPES - 1; i >= 0; i--)
    zan_live_unlock(&g_live_stripes[i]);
}

static inline unsigned zan_live_hash(uintptr_t ptr) {
  uintptr_t x = ptr >> 4;
  x ^= (x >> 16);
  x ^= (x >> 8);
  return (unsigned)x;
}

static void zan_live_atexit_detector(void) { zan_gc_detect_cycles(); }

static void zan_live_ensure_init(void) {
  if (__atomic_load_n(&g_live_stripes_inited, __ATOMIC_ACQUIRE))
    return;
  static volatile long init_lock = 0;
  while (__sync_lock_test_and_set(&init_lock, 1)) {
    /* spin */
  }
  if (!g_live_stripes_inited) {
    for (int i = 0; i < ZAN_LIVE_STRIPES; i++) {
      zan_live_lock_init(&g_live_stripes[i]);
      g_live_stripes[i].buckets = (zan_live_entry_t **)zan_mem_native_calloc(
          64, sizeof(zan_live_entry_t *));
      if (g_live_stripes[i].buckets)
        g_live_stripes[i].bucket_count = 64;
      g_live_stripes[i].count = 0;
      g_live_stripes[i].free_list = NULL;
    }
    if (getenv("ZAN_DETECT_LEAKS")) {
      atexit(zan_live_atexit_detector);
    }
    __atomic_store_n(&g_live_stripes_inited, 1, __ATOMIC_RELEASE);
  }
  __sync_lock_release(&init_lock);
}

static void zan_live_register(void *obj, size_t size, zan_live_kind_t kind,
                              size_t data_offset, const char *source) {
  if (!obj || size < ZAN_OBJ_HDR_SIZE)
    return;
  /* The compiler initializes the header before publishing the allocation.
   * Store its type discriminator so snapshots need not read live object data.
   */
  uint64_t site = *(const uint64_t *)((const char *)obj + ZAN_OBJ_SITE_OFF);
  /* Older callers use the object hook for rank-one primitive arrays. The
   * reserved site value identifies that layout without probing before -16. */
  if (kind == ZAN_LIVE_OBJECT && site == ZAN_ARRAY_MAGIC)
    kind = ZAN_LIVE_ARRAY;
  size_t header_size =
      kind == ZAN_LIVE_OBJECT ? ZAN_OBJ_HDR_SIZE : ZAN_ARR_HDR_SIZE;
  if (size < header_size || data_offset > size - header_size)
    return;
  zan_live_ensure_init();
  unsigned h = zan_live_hash((uintptr_t)obj);
  unsigned stripe_idx = h & (ZAN_LIVE_STRIPES - 1);
  zan_live_stripe_t *s = &g_live_stripes[stripe_idx];

  zan_live_lock(s);
  if (!s->buckets) {
    s->buckets = (zan_live_entry_t **)zan_mem_native_calloc(
        64, sizeof(zan_live_entry_t *));
    if (!s->buckets) {
      zan_live_unlock(s);
      return;
    }
    s->bucket_count = 64;
  }

  /* Grow buckets if load factor > 2 */
  if (s->count > s->bucket_count * 2 && s->bucket_count < (1u << 24)) {
    size_t new_cap = s->bucket_count * 2;
    zan_live_entry_t **new_buckets = (zan_live_entry_t **)zan_mem_native_calloc(
        new_cap, sizeof(zan_live_entry_t *));
    if (new_buckets) {
      for (size_t b = 0; b < s->bucket_count; b++) {
        zan_live_entry_t *curr = s->buckets[b];
        while (curr) {
          zan_live_entry_t *nxt = curr->next;
          unsigned b_idx =
              (zan_live_hash((uintptr_t)curr->obj) / ZAN_LIVE_STRIPES) &
              (new_cap - 1);
          curr->next = new_buckets[b_idx];
          new_buckets[b_idx] = curr;
          curr = nxt;
        }
      }
      __real_free(s->buckets);
      s->buckets = new_buckets;
      s->bucket_count = new_cap;
    }
  }

  unsigned b_idx = (h / ZAN_LIVE_STRIPES) & (s->bucket_count - 1);
  zan_live_entry_t *entry = s->free_list;
  if (entry) {
    s->free_list = entry->next;
  } else {
    entry = (zan_live_entry_t *)__real_malloc(sizeof(zan_live_entry_t));
    if (!entry) {
      zan_live_unlock(s);
      return;
    }
  }
  entry->obj = obj;
  entry->size = size;
  entry->site = site;
  entry->source = source;
  entry->data_offset = data_offset;
  entry->kind = kind;
  entry->next = s->buckets[b_idx];
  s->buckets[b_idx] = entry;
  s->count++;
  zan_live_unlock(s);
}

void zan_rt_live_register(void *obj, size_t size) {
  zan_live_register(obj, size, ZAN_LIVE_OBJECT, 0, NULL);
}

void zan_rt_live_register_named(void *obj, uint64_t size, const char *source) {
  if (size > SIZE_MAX)
    return;
  zan_live_register(obj, (size_t)size, ZAN_LIVE_OBJECT, 0, source);
}

void zan_rt_live_register_array(void *obj, uint64_t size, uint64_t data_offset,
                                int32_t elements_are_refs) {
  if (size > SIZE_MAX || data_offset > SIZE_MAX)
    return;
  zan_live_register(obj, (size_t)size,
                    elements_are_refs ? ZAN_LIVE_REF_ARRAY : ZAN_LIVE_ARRAY,
                    (size_t)data_offset, NULL);
}

void zan_rt_live_unregister(void *obj) {
  if (!obj)
    return;
  if (!__atomic_load_n(&g_live_stripes_inited, __ATOMIC_ACQUIRE))
    return;
  unsigned h = zan_live_hash((uintptr_t)obj);
  unsigned stripe_idx = h & (ZAN_LIVE_STRIPES - 1);
  zan_live_stripe_t *s = &g_live_stripes[stripe_idx];

  zan_live_lock(s);
  if (!s->buckets || s->count == 0) {
    zan_live_unlock(s);
    return;
  }

  unsigned b_idx = (h / ZAN_LIVE_STRIPES) & (s->bucket_count - 1);
  zan_live_entry_t **prev = &s->buckets[b_idx];
  zan_live_entry_t *curr = *prev;
  while (curr) {
    if (curr->obj == obj) {
      *prev = curr->next;
      curr->next = s->free_list;
      s->free_list = curr;
      s->count--;
      break;
    }
    prev = &curr->next;
    curr = curr->next;
  }
  zan_live_unlock(s);
}

static void **g_site_field_graphs_reg = NULL;
static void **g_site_tynames_reg = NULL;
static int64_t g_site_reg_count = 0;

void zan_rt_register_site_field_graphs(void **graphs, void **tynames,
                                       int64_t count) {
  g_site_field_graphs_reg = graphs;
  g_site_tynames_reg = tynames;
  g_site_reg_count = count;
}

static const char *zan_get_object_tyname(const zan_live_entry_t *entry) {
  uint64_t site = entry->site & ~ZAN_OBJ_STACK_FLAG;
  const char *const *names = NULL;
  if (entry->kind != ZAN_LIVE_OBJECT) {
    /* The rectangular layout uses its site word for the rank. */
    if (entry->kind == ZAN_LIVE_REF_ARRAY && entry->data_offset == 0 &&
        site != ZAN_ARRAY_MAGIC && site >= 4096) {
      const zan_arc_desc_t *desc = (const zan_arc_desc_t *)(uintptr_t)site;
      names = desc->tynames_arr;
    }
    return names && names[0] ? names[0] : "Array";
  }
  if ((site >> 32) == ZAN_STRING_TAG)
    return "string";
  if (g_site_tynames_reg && site > 0 && site < (uint64_t)g_site_reg_count) {
    names = g_site_tynames_reg[site];
  } else if (site >= 4096) {
    const zan_arc_desc_t *desc = (const zan_arc_desc_t *)(uintptr_t)site;
    names = desc->tynames_arr;
  }
  return names && names[0] ? names[0] : "object";
}

typedef void (*zan_edge_cb_t)(void *to, void *ctx);

static void zan_walk_pointer_edges(void *const *data, size_t count,
                                   zan_edge_cb_t cb, void *ctx) {
  if (!data)
    return;
  for (size_t i = 0; i < count; i++) {
    if (data[i])
      cb(data[i], ctx);
  }
}

static void zan_walk_object_edges(const zan_live_entry_t *entry,
                                  zan_edge_cb_t cb, void *ctx) {
  const char *obj = entry->obj;
  if (entry->kind != ZAN_LIVE_OBJECT) {
    if (entry->kind == ZAN_LIVE_REF_ARRAY) {
      int64_t count = *(const int64_t *)(obj + ZAN_OBJ_RC_OFF);
      size_t bytes = entry->size - ZAN_ARR_HDR_SIZE - entry->data_offset;
      if (count > 0 && (uint64_t)count <= bytes / sizeof(void *)) {
        zan_walk_pointer_edges((void *const *)(obj + entry->data_offset),
                               (size_t)count, cb, ctx);
      }
    }
    return;
  }

  uint64_t site = entry->site & ~ZAN_OBJ_STACK_FLAG;
  if ((site >> 32) == ZAN_STRING_TAG)
    return;
  const zan_field_graph_t *fg = NULL;
  if (g_site_field_graphs_reg && site > 0 &&
      site < (uint64_t)g_site_reg_count) {
    fg = g_site_field_graphs_reg[site];
  } else if (site >= 4096) {
    const zan_arc_desc_t *desc = (const zan_arc_desc_t *)(uintptr_t)site;
    fg = desc->field_graph;
  }
  if (!fg)
    return;

  size_t bytes = entry->size - ZAN_OBJ_HDR_SIZE;
  if (fg->kind == 0) {
    for (uint32_t k = 0; k < fg->ref_count; k++) {
      uint32_t off = fg->refs[k].offset;
      if (!fg->refs[k].is_rc || off > bytes || bytes - off < sizeof(void *))
        continue;
      void *child;
      memcpy(&child, obj + off, sizeof(child));
      if (child)
        cb(child, ctx);
    }
  } else if (fg->kind == 1 || fg->kind == 2) { /* List<T> or Dict<K,V> */
    size_t required = 16 + sizeof(void *) * fg->kind;
    if (bytes < required)
      return;
    int64_t count = *(const int64_t *)obj;
    int64_t capacity = *(const int64_t *)(obj + 8);
    if (count <= 0 || count > capacity)
      return;
    if (fg->kind == 1 || (fg->ref_count & 1)) {
      void *const *data = *(void *const *const *)(obj + 16);
      zan_walk_pointer_edges(data, (size_t)count, cb, ctx);
    }
    if (fg->kind == 2 && (fg->ref_count & 2)) {
      void *const *values = *(void *const *const *)(obj + 24);
      zan_walk_pointer_edges(values, (size_t)count, cb, ctx);
    }
  }
}

typedef struct {
  void *key;
  int val;
} zan_map_entry_t;

typedef struct {
  zan_map_entry_t *entries;
  size_t capacity;
  size_t mask;
} zan_obj_map_t;

static inline void zan_obj_map_init(zan_obj_map_t *map, size_t min_cap) {
  size_t cap = 64;
  while (cap < min_cap * 2)
    cap *= 2;
  map->capacity = cap;
  map->mask = cap - 1;
  map->entries =
      (zan_map_entry_t *)zan_mem_native_calloc(cap, sizeof(zan_map_entry_t));
}

static inline void zan_obj_map_free(zan_obj_map_t *map) {
  if (map->entries)
    __real_free(map->entries);
  map->entries = NULL;
}

static inline void zan_obj_map_put(zan_obj_map_t *map, void *key, int val) {
  if (!key || !map->entries)
    return;
  size_t idx = zan_live_hash((uintptr_t)key) & map->mask;
  while (map->entries[idx].key && map->entries[idx].key != key) {
    idx = (idx + 1) & map->mask;
  }
  map->entries[idx].key = key;
  map->entries[idx].val = val;
}

static inline int zan_obj_map_get(const zan_obj_map_t *map, void *key) {
  if (!key || !map->entries)
    return -1;
  size_t idx = zan_live_hash((uintptr_t)key) & map->mask;
  while (map->entries[idx].key) {
    if (map->entries[idx].key == key)
      return map->entries[idx].val;
    idx = (idx + 1) & map->mask;
  }
  return -1;
}

typedef struct {
  const zan_live_entry_t *entry;
  int64_t rc;
  int64_t internal_rc;
  int marked;
} zan_cycle_node_t;

typedef struct {
  zan_cycle_node_t *nodes;
  zan_obj_map_t *map;
} zan_cycle_ctx_t;

static void zan_cycle_edge_count_cb(void *to, void *ctx) {
  zan_cycle_ctx_t *c = (zan_cycle_ctx_t *)ctx;
  int j = zan_obj_map_get(c->map, to);
  if (j >= 0) {
    c->nodes[j].internal_rc++;
  }
}

typedef struct {
  zan_cycle_node_t *nodes;
  zan_obj_map_t *map;
  int *queue;
  size_t *qtail;
} zan_bfs_ctx_t;

static void zan_cycle_bfs_cb(void *to, void *ctx) {
  zan_bfs_ctx_t *b = (zan_bfs_ctx_t *)ctx;
  int v = zan_obj_map_get(b->map, to);
  if (v >= 0 && !b->nodes[v].marked) {
    b->nodes[v].marked = 1;
    b->queue[(*b->qtail)++] = v;
  }
}

int64_t zan_gc_detect_cycles(void) {
  if (!zan_heap_scan_try_begin())
    return -1;
  if (!__atomic_load_n(&g_live_stripes_inited, __ATOMIC_ACQUIRE)) {
    ZAN_HEAP_GATE_UNLOCK();
    return 0;
  }

  zan_live_lock_all();
  zan_cycle_node_t *nodes = NULL;
  int *queue = NULL;
  zan_obj_map_t map = {0};
  int64_t orphan_count = -1;

  size_t total_count = 0;
  for (int s = 0; s < ZAN_LIVE_STRIPES; s++) {
    total_count += g_live_stripes[s].count;
  }
  if (total_count == 0) {
    orphan_count = 0;
    goto done;
  }
  if (total_count > INT32_MAX || total_count > SIZE_MAX / sizeof(*nodes))
    goto done;

  nodes = __real_malloc(total_count * sizeof(*nodes));
  queue = __real_malloc(total_count * sizeof(*queue));
  zan_obj_map_init(&map, total_count);
  if (!nodes || !queue || !map.entries)
    goto done;

  size_t idx = 0;
  for (int s = 0; s < ZAN_LIVE_STRIPES; s++) {
    for (size_t b = 0; b < g_live_stripes[s].bucket_count; b++) {
      zan_live_entry_t *entry = g_live_stripes[s].buckets[b];
      while (entry) {
        int rc_offset =
            entry->kind == ZAN_LIVE_OBJECT ? ZAN_OBJ_RC_OFF : ZAN_ARR_RC_OFF;
        const int64_t *rc_ptr =
            (const int64_t *)((const char *)entry->obj + rc_offset);
        int64_t rc = __atomic_load_n(rc_ptr, __ATOMIC_RELAXED);
        /* A destructor can call the diagnostic while its own object
         * is still registered. The outgoing edges are no longer live. */
        if (rc != 0) {
          nodes[idx] = (zan_cycle_node_t){.entry = entry, .rc = rc};
          zan_obj_map_put(&map, entry->obj, (int)idx++);
        }
        entry = entry->next;
      }
    }
  }
  size_t actual_count = idx;

  zan_cycle_ctx_t ctx = {nodes, &map};
  for (size_t i = 0; i < actual_count; i++) {
    zan_walk_object_edges(nodes[i].entry, zan_cycle_edge_count_cb, &ctx);
  }

  size_t qhead = 0, qtail = 0;
  for (size_t i = 0; i < actual_count; i++) {
    if (nodes[i].rc > nodes[i].internal_rc) {
      nodes[i].marked = 1;
      queue[qtail++] = (int)i;
    }
  }
  zan_bfs_ctx_t bfs_ctx = {nodes, &map, queue, &qtail};
  while (qhead < qtail) {
    int u = queue[qhead++];
    zan_walk_object_edges(nodes[u].entry, zan_cycle_bfs_cb, &bfs_ctx);
  }

  orphan_count = 0;
  size_t orphan_bytes = 0;
  for (size_t i = 0; i < actual_count; i++) {
    if (!nodes[i].marked) {
      orphan_count++;
      orphan_bytes += nodes[i].entry->size;
    }
  }

  if (orphan_count > 0) {
    fprintf(stderr,
            "[zan-gc] cycle detection: %lld unreachable object(s) in orphan "
            "cycles (%zu bytes):\n",
            (long long)orphan_count, orphan_bytes);
    int printed = 0;
    for (size_t i = 0; i < actual_count && printed < 20; i++) {
      if (!nodes[i].marked) {
        const zan_live_entry_t *entry = nodes[i].entry;
        fprintf(
            stderr,
            "  - [%s] at %p (rc=%lld, internal_rc=%lld, size=%zu, site=%llu)\n",
            zan_get_object_tyname(entry), entry->obj, (long long)nodes[i].rc,
            (long long)nodes[i].internal_rc, entry->size,
            (unsigned long long)entry->site);
        printed++;
      }
    }
    if (orphan_count > 20) {
      fprintf(stderr, "  ... and %lld more orphan object(s)\n",
              (long long)(orphan_count - 20));
    }
  }

done:
  __real_free(queue);
  zan_obj_map_free(&map);
  __real_free(nodes);
  zan_live_unlock_all();
  ZAN_HEAP_GATE_UNLOCK();
  return orphan_count;
}

void zan_rt_dump_leaks(void) { zan_gc_detect_cycles(); }

typedef struct {
  const char *tyname;
  size_t count;
  size_t bytes;
} zan_heap_type_stat_t;

typedef struct {
  zan_heap_type_stat_t type;
  const char *source;
} zan_heap_source_stat_t;

typedef struct {
  zan_heap_source_stat_t *rows;
  size_t capacity;
  size_t count;
} zan_heap_sources_t;

static zan_heap_source_stat_t *zan_heap_source_slot(zan_heap_sources_t *sources,
                                                    const char *type,
                                                    const char *source) {
  size_t hash = 5381;
  for (const char *p = type; *p; p++)
    hash = hash * 33 + (unsigned char)*p;
  hash *= 33;
  for (const char *p = source; *p; p++)
    hash = hash * 33 + (unsigned char)*p;
  size_t mask = sources->capacity - 1;
  size_t i = hash & mask;
  while (sources->rows[i].source &&
         (strcmp(sources->rows[i].type.tyname, type) ||
          strcmp(sources->rows[i].source, source))) {
    i = (i + 1) & mask;
  }
  return &sources->rows[i];
}

static int zan_heap_source_add(zan_heap_sources_t *sources, const char *type,
                               const char *source, size_t bytes) {
  if (!source || !source[0])
    source = "<unknown>";
  zan_heap_source_stat_t *row =
      sources->capacity ? zan_heap_source_slot(sources, type, source) : NULL;
  if ((!row || !row->source) && sources->count >= sources->capacity / 2) {
    if (sources->capacity > SIZE_MAX / sizeof(*sources->rows) / 2)
      return 0;
    size_t capacity = sources->capacity ? sources->capacity * 2 : 256;
    zan_heap_source_stat_t *rows =
        zan_mem_native_calloc(capacity, sizeof(*rows));
    if (!rows)
      return 0;
    zan_heap_sources_t grown = {rows, capacity, sources->count};
    for (size_t i = 0; i < sources->capacity; i++) {
      zan_heap_source_stat_t *row = &sources->rows[i];
      if (row->source)
        *zan_heap_source_slot(&grown, row->type.tyname, row->source) = *row;
    }
    __real_free(sources->rows);
    *sources = grown;
    row = zan_heap_source_slot(sources, type, source);
  }
  if (!row->source) {
    row->type.tyname = type;
    row->source = source;
    sources->count++;
  }
  row->type.count++;
  row->type.bytes += bytes;
  return 1;
}

static int zan_heap_source_compare(const void *lhs, const void *rhs) {
  const zan_heap_source_stat_t *a = lhs;
  const zan_heap_source_stat_t *b = rhs;
  if (a->type.bytes != b->type.bytes)
    return a->type.bytes > b->type.bytes ? -1 : 1;
  int types = strcmp(a->type.tyname, b->type.tyname);
  return types ? types : strcmp(a->source, b->source);
}

int64_t zan_gc_write_heap_snapshot(const char *path) {
  if (!path || !path[0])
    return -1;
  if (!__atomic_load_n(&g_live_stripes_inited, __ATOMIC_ACQUIRE))
    return 0;

  zan_live_lock_all();

  size_t total_count = 0;
  size_t total_bytes = 0;
  for (int s = 0; s < ZAN_LIVE_STRIPES; s++) {
    total_count += g_live_stripes[s].count;
  }

  size_t stat_cap = 0;
  size_t stat_count = 0;
  zan_heap_type_stat_t *stats = NULL;
  zan_heap_sources_t sources = {0};

  for (int s = 0; s < ZAN_LIVE_STRIPES; s++) {
    for (size_t b = 0; b < g_live_stripes[s].bucket_count; b++) {
      zan_live_entry_t *entry = g_live_stripes[s].buckets[b];
      while (entry) {
        total_bytes += entry->size;
        const char *tn = zan_get_object_tyname(entry);
        size_t k = 0;
        while (k < stat_count && strcmp(stats[k].tyname, tn) != 0)
          k++;
        if (k == stat_count) {
          if (stat_count == stat_cap) {
            if (stat_cap > SIZE_MAX / sizeof(*stats) / 2)
              goto stats_error;
            size_t new_cap = stat_cap ? stat_cap * 2 : 256;
            zan_heap_type_stat_t *new_stats =
                __real_realloc(stats, new_cap * sizeof(*stats));
            if (!new_stats)
              goto stats_error;
            stats = new_stats;
            stat_cap = new_cap;
          }
          stats[stat_count++] = (zan_heap_type_stat_t){.tyname = tn};
        }
        stats[k].count++;
        stats[k].bytes += entry->size;
        if (!zan_heap_source_add(&sources, tn, entry->source, entry->size))
          goto stats_error;
        entry = entry->next;
      }
    }
  }

  zan_live_unlock_all();

  for (size_t i = 0; i < stat_count; i++) {
    for (size_t j = i + 1; j < stat_count; j++) {
      if (stats[j].bytes > stats[i].bytes) {
        zan_heap_type_stat_t tmp = stats[i];
        stats[i] = stats[j];
        stats[j] = tmp;
      }
    }
  }

  size_t occupied = 0;
  for (size_t i = 0; i < sources.capacity; i++) {
    if (sources.rows[i].source)
      sources.rows[occupied++] = sources.rows[i];
  }
  if (sources.count > 1) {
    qsort(sources.rows, sources.count, sizeof(*sources.rows),
          zan_heap_source_compare);
  }

  FILE *f = fopen(path, "w");
  if (!f)
    goto write_error;

  fprintf(f, "# Zan Heap Snapshot\n");
  fprintf(f, "# Total Live Objects: %zu\n", total_count);
  fprintf(f, "# Total Allocated Bytes: %zu\n\n", total_bytes);
  fprintf(f, "%-40s %15s %15s\n", "Type", "Count", "Bytes");
  fprintf(f, "-----------------------------------------------------------------"
             "---------------\n");
  for (size_t k = 0; k < stat_count; k++) {
    fprintf(f, "%-40s %15zu %15zu\n", stats[k].tyname, stats[k].count,
            stats[k].bytes);
  }
  fprintf(f, "-----------------------------------------------------------------"
             "---------------\n");
  fprintf(f,
          "\n# Live Objects by Type and Source\nType\tSource\tCount\tBytes\n");
  for (size_t k = 0; k < sources.count; k++) {
    const zan_heap_source_stat_t *row = &sources.rows[k];
    fprintf(f, "%s\t%s\t%zu\t%zu\n", row->type.tyname, row->source,
            row->type.count, row->type.bytes);
  }
  int write_failed = ferror(f);
  if (fclose(f) != 0)
    write_failed = 1;

  __real_free(sources.rows);
  __real_free(stats);
  return write_failed ? -1 : (int64_t)total_count;

stats_error:
  zan_live_unlock_all();
write_error:
  __real_free(sources.rows);
  __real_free(stats);
  return -1;
}

/* Keep native bridge aliases local to this implementation when included by
 * allocator unit tests that supply their own linker-ABI shims afterwards. */
#if defined(_WIN32)
#undef __real_malloc
#undef __real_free
#undef __real_calloc
#undef __real_realloc
#endif
