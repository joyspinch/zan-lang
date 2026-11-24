
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
#elif defined(__APPLE__)
/* ld64 has no --wrap. The allocator instead exports zan_mem_malloc/free/
 * calloc/realloc and the compiler points every allocation call it emits at
 * them (zan_irgen_route_fast_alloc), so system libraries and C runtime
 * objects keep libc malloc. Ownership still crosses that line both ways:
 *   - zan_mem_free/realloc of a libc block: zan_mem_owns() is false, so it
 *     is forwarded to libc exactly like the ELF __wrap_free fallback;
 *   - libc free/realloc/malloc_size of a Zan block (a C driver or runtime
 *     object releasing a buffer Zan code handed it): the block is claimed
 *     by a registered malloc zone (zan_mem_zone_register below), whose
 *     size() answers for our slabs, so libmalloc dispatches it back here. */
#include <malloc/malloc.h>
#include <pthread.h>
#define __real_malloc malloc
#define __real_free free
#define __real_calloc calloc
#define __real_realloc realloc
#define __wrap_malloc zan_mem_malloc
#define __wrap_free zan_mem_free
#define __wrap_calloc zan_mem_calloc
#define __wrap_realloc zan_mem_realloc
void *zan_mem_malloc(size_t n);
void zan_mem_free(void *p);
void *zan_mem_calloc(size_t n, size_t m);
void *zan_mem_realloc(void *p, size_t n);
static void zan_mem_zone_register(void);
#else
/* ELF's real allocator symbols are supplied by the linker wrap. */
void *__real_malloc(size_t n);
void __real_free(void *p);
void *__real_calloc(size_t n, size_t m);
void *__real_realloc(void *p, size_t n);
#endif

#define ZAN_MEM_MAX_SMALL 2048u
#define ZAN_MEM_SLAB (1u << 20) /* 1 MiB */

/* 无块头布局（2026-11-23）。曾在每块前放 16 字节头（magic/类/属主），一个
 * 16 字节字段的 Zan 对象实占 64 字节（C++ 16）。现按 mimalloc 思路分页：
 * 1 MiB slab 切成 64 个 16 KiB 页，一页只放一个尺寸类、只属一个线程缓存；
 * 类与属主记在 slab 首页的页表里，块地址 → (p - slab) >> 14 即得。
 * 第 0 页整页留给页表（1.6%）。块尺寸皆为 16 的倍数，页首 16 对齐，载荷
 * 天然满足编译器对象头的 16 字节对齐。
 *
 * 双重释放检测保持原强度：空闲块第 0 字是空闲链指针，第 1 字写入按地址
 * 派生的随机 cookie；释放时对第 1 字做 CAS（已是 cookie 即双重释放），
 * 并发的两次释放仍只有一个能赢。分配时清零第 1 字。活块数据恰等于 cookie
 * 的概率为 2^-64 量级。 */
#define ZAN_MEM_PAGE_SHIFT  14
#define ZAN_MEM_PAGE        (1u << ZAN_MEM_PAGE_SHIFT)       /* 16 KiB */
#define ZAN_MEM_PAGES       (ZAN_MEM_SLAB / ZAN_MEM_PAGE)    /* 64 */

typedef struct {
  struct zan_mem_cache *owner[ZAN_MEM_PAGES];
  uint8_t cls1[ZAN_MEM_PAGES]; /* 尺寸类 + 1；0 = 页未分配 */
} zan_mem_slab_meta;

_Static_assert(sizeof(zan_mem_slab_meta) <= ZAN_MEM_PAGE,
               "slab page table must fit in the reserved first page");

static uintptr_t g_mem_secret;

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
  char *page_bump[ZAN_MEM_NCLASS]; /* owner-only: 当前类页内未切部分 */
  char *page_end[ZAN_MEM_NCLASS];
  char *bump;             /* owner-only: 当前 slab 未分出的页区间 */
  char *bump_end;         /* owner-only */
  int bump_decommitted;   /* unallocated bump pages decommitted to OS */
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

static inline zan_mem_slab_meta *zan_mem_meta_of(const void *p, unsigned *page) {
    uintptr_t base = (uintptr_t)p & ~(uintptr_t)(ZAN_MEM_SLAB - 1);
    *page = (unsigned)(((uintptr_t)p - base) >> ZAN_MEM_PAGE_SHIFT);
    return (zan_mem_slab_meta *)base;
}

/* 块 p（已知在本分配器的 slab 内）的尺寸类；不是块起点（页表页、未分配页、
 * 页内非块边界）返回 -1。 */
static int zan_mem_block_class(const void *p) {
    unsigned pi;
    zan_mem_slab_meta *m = zan_mem_meta_of(p, &pi);
    if (pi == 0) return -1;
    unsigned c1 = __atomic_load_n(&m->cls1[pi], __ATOMIC_ACQUIRE);
    if (c1 == 0 || c1 > (unsigned)ZAN_MEM_NCLASS) return -1;
    uintptr_t off = (uintptr_t)p & (uintptr_t)(ZAN_MEM_PAGE - 1);
    if (off % k_class_size[c1 - 1] != 0) return -1;
    return (int)c1 - 1;
}

static inline uintptr_t zan_mem_cookie(const void *p) {
    return ((uintptr_t)p >> 4) ^ g_mem_secret;
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
      int cls = zan_mem_block_class(p);
      if (cls >= 0) {
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

/* 退役缓存切了一半的同类页：接过来续切，属主改为本缓存（退役缓存不再有
 * 线程使用；并发远端释放读到旧属主只会推进它的远端队列，之后被收割）。
 * 否则线程池频繁起落时每个退役线程都留下若干半空页。 */
static int zan_mem_adopt_retired_page(zan_mem_cache *c, int cls) {
    if (!g_cache_pool) return 0;
    size_t sz = k_class_size[cls];
    zan_mem_lock();
    for (zan_mem_cache *rc = g_cache_pool; rc; rc = rc->next_free) {
        char *pb = rc->page_bump[cls];
        if (pb && (size_t)(rc->page_end[cls] - pb) >= sz) {
            c->page_bump[cls] = pb;
            c->page_end[cls] = rc->page_end[cls];
            rc->page_bump[cls] = NULL;
            rc->page_end[cls] = NULL;
            unsigned pi;
            zan_mem_slab_meta *m = zan_mem_meta_of(pb, &pi);
            __atomic_store_n(&m->owner[pi], c, __ATOMIC_RELEASE);
            zan_mem_unlock();
            return 1;
        }
    }
    zan_mem_unlock();
    return 0;
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
  if (!g_mem_secret) {
    uintptr_t x =
        (uintptr_t)&g_mem_secret ^ (base * 0x9E3779B97F4A7C15ull) ^ (uintptr_t)c;
    g_mem_secret = x | 1;
  }
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

  /* 第 0 页是页表（mmap/VirtualAlloc 交来即为零：全部页未分配） */
  c->bump = (char *)base + ZAN_MEM_PAGE;
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
#if defined(__APPLE__)
  /* Before the first Zan block exists, and with no allocator lock held
   * (libmalloc's fork handlers take its lock, then our force_lock). */
  zan_mem_zone_register();
#endif
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
    ((uintptr_t *)p)[1] = 0; /* 清掉空闲 cookie */
    __atomic_fetch_add(&g_small_alloc_count, 1, __ATOMIC_RELAXED);
    return p;
  }
  size_t sz = k_class_size[cls];
  if ((size_t)(c->page_end[cls] - c->page_bump[cls]) < sz ||
      !c->page_bump[cls]) {
    if (!zan_mem_adopt_retired_page(c, cls)) {
      /* 从当前 slab 的未分出页区间切一整页给这个类 */
      if ((size_t)(c->bump_end - c->bump) < ZAN_MEM_PAGE) {
        if (!zan_mem_adopt_retired_bump(c, ZAN_MEM_PAGE)) {
          if (!zan_mem_new_slab(c))
            return NULL;
        }
      }
#if defined(_WIN32)
      if (c->bump_decommitted) {
        uintptr_t start = ((uintptr_t)c->bump + 4095) & ~(uintptr_t)4095;
        uintptr_t end = (uintptr_t)c->bump_end & ~(uintptr_t)4095;
        if (end > start) {
          if (!VirtualAlloc((void *)start, end - start, MEM_COMMIT,
                            PAGE_READWRITE))
            return NULL;
        }
        c->bump_decommitted = 0;
      }
#else
      c->bump_decommitted = 0;
#endif
      char *page = c->bump;
      c->bump += ZAN_MEM_PAGE;
      unsigned pi;
      zan_mem_slab_meta *m = zan_mem_meta_of(page, &pi);
      __atomic_store_n(&m->owner[pi], c, __ATOMIC_RELAXED);
      __atomic_store_n(&m->cls1[pi], (uint8_t)(cls + 1), __ATOMIC_RELEASE);
      c->page_bump[cls] = page;
      c->page_end[cls] = page + ZAN_MEM_PAGE;
    }
  }
  p = c->page_bump[cls];
  c->page_bump[cls] += sz;
  ((uintptr_t *)p)[1] = 0;
  __atomic_fetch_add(&g_small_alloc_count, 1, __ATOMIC_RELAXED);
  return p;
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

static void zan_mem_fatal_block(const char *what, const void *p) {
  char msg[80];
  snprintf(msg, sizeof msg, "%s of block %p", what, p);
  zan_rt_fatal("mem", msg);
}

/* 本分配器的块：返回尺寸类；指向 slab 内却不是块起点 → 致命（曾按坏
 * magic 静默忽略，内部指针 free 是确定的程序错误）。 */
static int zan_mem_class_or_die(const void *p) {
  int cls = zan_mem_block_class(p);
  if (cls < 0)
    zan_mem_fatal_block("invalid free (not a block start)", p);
  return cls;
}

void __wrap_free(void *p) {
  if (!p)
    return;
  if (!zan_mem_owns(p)) {
    __real_free(p);
    return;
  }
  int cls = zan_mem_class_or_die(p);
  if (cls < 0)
    return; /* 致命处理器返回时 */
  uintptr_t ck = zan_mem_cookie(p);
  uintptr_t *w = (uintptr_t *)p + 1;
  uintptr_t cur = __atomic_load_n(w, __ATOMIC_ACQUIRE);
  for (;;) {
    if (cur == ck) {
      zan_mem_fatal_block("double free", p);
      return;
    }
    if (__atomic_compare_exchange_n(w, &cur, ck, 0, __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE))
      break;
  }
  unsigned pi;
  zan_mem_slab_meta *m = zan_mem_meta_of(p, &pi);
  zan_mem_cache *owner = __atomic_load_n(&m->owner[pi], __ATOMIC_ACQUIRE);
#if defined(_WIN32)
  DWORD fls = zan_mem_fls_slot();
  zan_mem_cache *self =
      (fls == FLS_OUT_OF_INDEXES) ? NULL : (zan_mem_cache *)FlsGetValue(fls);
#else
  zan_mem_cache *self = t_cache;
#endif
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
  int cls = zan_mem_class_or_die(p);
  if (cls < 0)
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

#if defined(__APPLE__)
/* Size of a live Zan block, 0 for anything else. libmalloc asks every
 * registered zone this about pointers it does not recognise (free, realloc,
 * malloc_size, malloc_zone_from_ptr), so it must be cheap and never fault:
 * only block starts inside our slabs' carved pages are inspected. */
static size_t zan_mem_block_size(const void *p) {
  if (!p || !zan_mem_owns(p))
    return 0;
  int cls = zan_mem_block_class(p);
  if (cls < 0)
    return 0;
  if (__atomic_load_n((const uintptr_t *)p + 1, __ATOMIC_ACQUIRE) ==
      zan_mem_cookie(p))
    return 0; /* 已释放 */
  return k_class_size[cls];
}

static size_t zz_size(malloc_zone_t *z, const void *p) {
  (void)z;
  return zan_mem_block_size(p);
}
static void *zz_malloc(malloc_zone_t *z, size_t n) {
  (void)z;
  return zan_mem_malloc(n);
}
static void *zz_calloc(malloc_zone_t *z, size_t n, size_t m) {
  (void)z;
  return zan_mem_calloc(n, m);
}
static void *zz_valloc(malloc_zone_t *z, size_t n) {
  (void)z;
  return valloc(n);
}
static void zz_free(malloc_zone_t *z, void *p) {
  (void)z;
  zan_mem_free(p);
}
static void zz_free_definite(malloc_zone_t *z, void *p, size_t n) {
  (void)z;
  (void)n;
  zan_mem_free(p);
}
static void *zz_realloc(malloc_zone_t *z, void *p, size_t n) {
  (void)z;
  return zan_mem_realloc(p, n);
}
static void *zz_memalign(malloc_zone_t *z, size_t align, size_t n) {
  (void)z;
  void *p = NULL;
  return posix_memalign(&p, align, n) == 0 ? p : NULL;
}
static void zz_destroy(malloc_zone_t *z) { (void)z; }
static kern_return_t zz_enumerator(task_t task, void *ctx, unsigned type,
                                   vm_address_t zone, memory_reader_t reader,
                                   vm_range_recorder_t recorder) {
  (void)task;
  (void)ctx;
  (void)type;
  (void)zone;
  (void)reader;
  (void)recorder;
  return KERN_SUCCESS;
}
static size_t zz_good_size(malloc_zone_t *z, size_t n) {
  (void)z;
  if (n == 0)
    n = 1;
  if (n > ZAN_MEM_MAX_SMALL)
    return n;
  zan_mem_ensure_classes();
  return k_class_size[g_size_class[n]];
}
static boolean_t zz_check(malloc_zone_t *z) {
  (void)z;
  return 1;
}
static void zz_print(malloc_zone_t *z, boolean_t verbose) {
  (void)z;
  (void)verbose;
}
static void zz_log(malloc_zone_t *z, void *addr) {
  (void)z;
  (void)addr;
}
/* libmalloc brackets fork() with these for every registered zone, so a fork
 * never snapshots g_slab_lock held by a thread that will not exist in the
 * child. */
static void zz_force_lock(malloc_zone_t *z) {
  (void)z;
  zan_mem_lock();
}
static void zz_force_unlock(malloc_zone_t *z) {
  (void)z;
  zan_mem_unlock();
}
static void zz_reinit_lock(malloc_zone_t *z) {
  (void)z;
  __sync_lock_release(&g_slab_lock);
}
static void zz_statistics(malloc_zone_t *z, malloc_statistics_t *st) {
  (void)z;
  memset(st, 0, sizeof *st);
  size_t slabs = __atomic_load_n(&g_slab_count, __ATOMIC_RELAXED);
  st->size_allocated = slabs * (size_t)ZAN_MEM_SLAB;
}
static boolean_t zz_locked(malloc_zone_t *z) {
  (void)z;
  return __atomic_load_n(&g_slab_lock, __ATOMIC_RELAXED) != 0;
}

static malloc_introspection_t g_zan_zone_introspect = {
    .enumerator = zz_enumerator,
    .good_size = zz_good_size,
    .check = zz_check,
    .print = zz_print,
    .log = zz_log,
    .force_lock = zz_force_lock,
    .force_unlock = zz_force_unlock,
    .statistics = zz_statistics,
    .zone_locked = zz_locked,
    .reinit_lock = zz_reinit_lock,
};

static malloc_zone_t g_zan_zone = {
    .size = zz_size,
    .malloc = zz_malloc,
    .calloc = zz_calloc,
    .valloc = zz_valloc,
    .free = zz_free,
    .realloc = zz_realloc,
    .destroy = zz_destroy,
    .zone_name = "zan_mem",
    .introspect = &g_zan_zone_introspect,
    .version = 9,
    .memalign = zz_memalign,
    .free_definite_size = zz_free_definite,
};

static pthread_once_t g_zan_zone_once = PTHREAD_ONCE_INIT;
static void zan_mem_zone_register_once(void) {
  malloc_zone_register(&g_zan_zone);
}
static void zan_mem_zone_register(void) {
  pthread_once(&g_zan_zone_once, zan_mem_zone_register_once);
}
#endif

/* B-ID78 延迟释放队列已移至 timer/rt_timer_defer.c（随 rt_timer.o 全 hosted
 * 链接；曾只在本对象里，Apple 拿到同步弱兜底，深树释放照样爆栈）。 */

/* B-ID89 活对象注册表、孤儿环探测/回收器与堆快照导出已移至
 * timer/rt_timer_gc.c（随 rt_timer.o 进入所有 hosted 链接路径；曾只在
 * --fast-alloc 且非 Apple 的链接里存在，macOS 上 GC.DetectCycles 恒为 0）。 */

/* Keep native bridge aliases local to this implementation when included by
 * allocator unit tests that supply their own linker-ABI shims afterwards. */
#if defined(_WIN32) || defined(__APPLE__)
#undef __real_malloc
#undef __real_free
#undef __real_calloc
#undef __real_realloc
#endif
#if defined(__APPLE__)
#undef __wrap_malloc
#undef __wrap_free
#undef __wrap_calloc
#undef __wrap_realloc
#endif
