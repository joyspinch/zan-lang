
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
#if defined(__linux__)
#include <sys/syscall.h>
#endif
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
#define ZAN_MEM_COMMIT_CHUNK (4u * ZAN_MEM_PAGE)             /* 64 KiB */

typedef struct zan_mem_slab_meta {
  struct zan_mem_cache *owner[ZAN_MEM_PAGES];
  uint8_t cls1[ZAN_MEM_PAGES];       /* 尺寸类 + 1；0 = 页未分配/已回收 */
  uint16_t trim_cnt[ZAN_MEM_PAGES];  /* 仅在 g_trim_lock 下由 zan_mem_trim 使用 */
  uint64_t trim_touched;             /* 本轮 trim 触及的页位图 */
  uint64_t reclaimed_mask;           /* 仅在 zan_mem_lock 下：已反提交、可跨类复用的空页 */
  struct zan_mem_slab_meta *trim_next;
  struct zan_mem_slab_meta *reclaimed_next;
  uint8_t in_reclaimed_list;
} zan_mem_slab_meta;

_Static_assert(sizeof(zan_mem_slab_meta) <= ZAN_MEM_PAGE,
               "slab page table must fit in the reserved first page");

static uintptr_t g_mem_secret;

/* 前 24 类是通用类（16 对齐）；其后是字符串专用的"串页"类（2026-11-24）：
 * 8 字节粒度 24..128。Zan 串头 12 字节（[-12] u32 len、[-8] 头字），为让
 * [-8] 头字 8 字节对齐，串块排在页内 4 mod 8 处：第 i 块真起点
 * page + 4 + S*i，对外交出（也收回）的是"虚基址" V = page + S*i = 真起点 - 4
 * （= 用户指针 - 16，编译器的 free 一向传它）。V 处 4 字节属于前一块的
 * 尾巴，分配器自己的空闲链/ cookie 字放在 V+8 / V+16（都在本块内、8 对齐）。
 * "item1234567"：12 + 12 = 24 字节一块（通用类得 32）。 */
/* 通用类之后、串页类之前是"对象页"类（2026-11-24）：16 字节粒度之间的
 * 奇数倍 8（24..120），只经 zan_mem_obj_malloc 取用——编译器的 zan_rt_alloc
 * 给 Zan 对象/闭包/装箱分配时调它。块 8 字节对齐（页首 + 40*i），比 malloc
 * 契约的 16 弱，所以绝不进 g_size_class（通用 malloc 永远拿不到）。两指针
 * 字段的无多态类：8 字节头 + 2 字段 = 24 字节一块（docs/OBJECT_LAYOUT.md）。 */
#define ZAN_MEM_CLASS_SIZES(X)                                                 \
  X(16)                                                                        \
  X(32) X(48) X(64) X(80) X(96) X(112) X(128) X(160) X(192) X(224) X(256)     \
      X(320) X(384) X(448) X(512) X(640) X(768) X(896) X(1024) X(1280)        \
          X(1536) X(1792) X(2048) X(24) X(40) X(56) X(72) X(88) X(104) X(120) \
              X(24) X(32) X(40) X(48) X(56) X(64) X(72) X(80) X(88) X(96)     \
                  X(104) X(112) X(120) X(128)
#define ZAN_MEM_SZ_ELEM(n) n,
#define ZAN_MEM_DIVM_ELEM(n) (UINT64_MAX / (uint64_t)(n) + 1u),
static const uint16_t k_class_size[] = {ZAN_MEM_CLASS_SIZES(ZAN_MEM_SZ_ELEM)};
/* free 校验"页内偏移是块尺寸整数倍"用 Lemire 整除判定（off * M <= M - 1，
 * M = ceil(2^64 / sz)，对任意 32 位 off 精确）：曾是一条 udiv，每次释放
 * 都付 ~10 周期的除法延迟。 */
static const uint64_t k_class_divm[] = {ZAN_MEM_CLASS_SIZES(ZAN_MEM_DIVM_ELEM)};
#define ZAN_MEM_NCLASS ((int)(sizeof(k_class_size) / sizeof(k_class_size[0])))
#define ZAN_MEM_OBJ_CLS0 24 /* 第一个对象页类（24 字节）的下标 */
#define ZAN_MEM_OBJ_MAX 120u
#define ZAN_MEM_STR_CLS0 31 /* 第一个串页类（24 字节）的下标 */
#define ZAN_MEM_STR_MIN 24u
#define ZAN_MEM_STR_MAX 128u
#define ZAN_MEM_STR_SKEW 4u /* 串块真起点 = V + 4 */

_Static_assert(ZAN_MEM_STR_CLS0 + (ZAN_MEM_STR_MAX - ZAN_MEM_STR_MIN) / 8 + 1 ==
                   ZAN_MEM_NCLASS,
               "string page classes must end the class table");
_Static_assert(ZAN_MEM_OBJ_CLS0 + (ZAN_MEM_OBJ_MAX - 24u) / 16 + 1 ==
                   ZAN_MEM_STR_CLS0,
               "object page classes sit between the general and string classes");

static inline int zan_mem_is_str_cls(int cls) { return cls >= ZAN_MEM_STR_CLS0; }
/* 空闲块的链字与 cookie 字相对块地址 p（串页类为 V）的偏移 */
static inline size_t zan_mem_link_off(int cls) {
  return (size_t)zan_mem_is_str_cls(cls) << 3; /* 8 或 0，无分支 */
}
static inline void **zan_mem_link(void *p, int cls) {
  return (void **)((char *)p + zan_mem_link_off(cls));
}
static inline uintptr_t *zan_mem_cookie_word(void *p, int cls) {
  return (uintptr_t *)((char *)p + zan_mem_link_off(cls) + 8u);
}
/* 页内最后一个可切位置之后：串页类末尾要给真起点的 +4 让出空间 */
static inline size_t zan_mem_page_span(int cls) {
  return ZAN_MEM_PAGE - ((size_t)zan_mem_is_str_cls(cls) << 2); /* -4 或 -0 */
}

static unsigned char g_size_class[ZAN_MEM_MAX_SMALL + 1];
static int g_class_ready;

/* 512 槽内联初表仅占 4 KiB 静态 .bss（覆盖 256 MiB 小对象堆才需首次扩容），
 * 取代原 128 KiB 静态数组并消除 16384 slab 硬上限；扩容时锁外分配新表并以
 * release/acquire 原子指针切换，旧表永不释放以保持 zan_mem_owns() 完全无锁。 */
#define ZAN_MEM_INIT_SET_SIZE (1u << 9)
typedef struct zan_slab_table {
  uint32_t mask;
  uint32_t cap;
  uintptr_t *slots;
  struct zan_slab_table *prev;
} zan_slab_table;

static uintptr_t g_slab_init_slots[ZAN_MEM_INIT_SET_SIZE];
static zan_slab_table g_slab_init_tbl = {ZAN_MEM_INIT_SET_SIZE - 1u,
                                         ZAN_MEM_INIT_SET_SIZE,
                                         g_slab_init_slots, NULL};
static zan_slab_table *g_slab_tbl = &g_slab_init_tbl;
static size_t g_slab_count;
static size_t g_small_alloc_count;
static size_t g_trimmed_bytes;

size_t zan_mem_slabs(void) {
  return __atomic_load_n(&g_slab_count, __ATOMIC_RELAXED);
}

#define ZAN_MEM_REMOTE_STRIPES 8

typedef struct zan_mem_cache {
  void *free_list[ZAN_MEM_NCLASS]; /* owner-only */
  char *page_bump[ZAN_MEM_NCLASS]; /* owner-only: 当前类页内未切部分 */
  char *page_end[ZAN_MEM_NCLASS];
  char *bump;           /* owner-only: 当前 slab 未分出的页区间 */
  char *bump_end;       /* owner-only */
  char *bump_committed; /* owner-only: Windows 已提交上界（按 64 KiB 块按需提交） */
  uintptr_t last_slab;  /* owner-only: 最近命中的 slab 基址（免去高频释放的全局哈希探查） */
  int bump_decommitted; /* unallocated bump pages decommitted to OS */
  uint32_t alloc_batch; /* owner-only: 批次累计后刷入 g_small_alloc_count */
  uint32_t remote_mask; /* 非空远端释放条带位图，免去空条带 8 次 XCHG */
  struct {
    void *head;
    char pad[64 - sizeof(void *)];
  } remote[ZAN_MEM_REMOTE_STRIPES];
  struct zan_mem_cache *next_free;
} zan_mem_cache;

static zan_mem_cache *g_cache_pool;
static zan_mem_slab_meta *g_reclaimed_head;
static volatile int g_trim_lock;
static int g_cache_init_count;
static int g_multi_thread;

/* g_small_alloc_count 按线程攒批并入（每 64 次一次原子加）：曾每次分配都对
 * 这个全局计数做一次原子加，多线程分配时它是所有核争抢的同一缓存行。
 * 统计因此最多滞后"活线程数 × 63"次，线程退役时补齐。 */
#define ZAN_MEM_COUNT_BATCH 64u
static inline void zan_mem_count_alloc(zan_mem_cache *c) {
  uint32_t b = c->alloc_batch + 1u;
  if (__builtin_expect(b >= ZAN_MEM_COUNT_BATCH, 0)) {
    __atomic_fetch_add(&g_small_alloc_count, (size_t)b, __ATOMIC_RELAXED);
    c->alloc_batch = 0;
  } else {
    c->alloc_batch = b;
  }
}

static unsigned g_remote_rr;

static void zan_mem_retire(zan_mem_cache *c);
static void zan_mem_drain_remote(zan_mem_cache *c);
static void zan_mem_drain_remote_all(zan_mem_cache *c);

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

void zan_mem_get_stats(zan_mem_stats_t *out) {
  if (!out)
    return;
  size_t slabs = __atomic_load_n(&g_slab_count, __ATOMIC_RELAXED);
  out->slab_count = slabs;
  out->slab_bytes = slabs * (size_t)ZAN_MEM_SLAB;
  size_t cnt = __atomic_load_n(&g_small_alloc_count, __ATOMIC_RELAXED);
  if (t_cache)
    cnt += t_cache->alloc_batch;
  out->small_alloc_count = cnt;
  out->trimmed_bytes = __atomic_load_n(&g_trimmed_bytes, __ATOMIC_RELAXED);
}

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
    if (!__atomic_load_n(&g_slab_lock, __ATOMIC_RELAXED) &&
        !__sync_lock_test_and_set(&g_slab_lock, 1))
      return;
    zan_mem_backoff(spins);
  }
}

static void zan_mem_unlock(void) { __sync_lock_release(&g_slab_lock); }

static void zan_mem_trim_lock(void) {
  for (int spins = 0;; spins++) {
    if (!__atomic_load_n(&g_trim_lock, __ATOMIC_RELAXED) &&
        !__sync_lock_test_and_set(&g_trim_lock, 1))
      return;
    zan_mem_backoff(spins);
  }
}

static void zan_mem_trim_unlock(void) { __sync_lock_release(&g_trim_lock); }

static void zan_mem_build_classes(void) {
  int c = 0;
  for (unsigned s = 0; s <= ZAN_MEM_MAX_SMALL; s++) {
    while (c < ZAN_MEM_OBJ_CLS0 && k_class_size[c] < s)
      c++;
    g_size_class[s] =
        (unsigned char)(c < ZAN_MEM_OBJ_CLS0 ? c : ZAN_MEM_OBJ_CLS0 - 1);
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

static unsigned zan_mem_hash(uintptr_t base, uint32_t mask) {
  uint64_t h = (uint64_t)(base >> 20) * 0x9E3779B97F4A7C15ull;
  return (unsigned)(h >> 40) & mask;
}

static zan_slab_table *zan_mem_alloc_slab_table(uint32_t cap) {
  size_t bytes = sizeof(zan_slab_table) + (size_t)cap * sizeof(uintptr_t);
  zan_slab_table *t = (zan_slab_table *)__real_calloc(1, bytes);
  if (!t)
    return NULL;
  t->mask = cap - 1u;
  t->cap = cap;
  t->slots = (uintptr_t *)(t + 1);
  t->prev = NULL;
  return t;
}

static int zan_mem_owns(const void *p) {
  if (!__atomic_load_n(&g_slab_count, __ATOMIC_ACQUIRE))
    return 0;
  uintptr_t base = (uintptr_t)p & ~(uintptr_t)(ZAN_MEM_SLAB - 1);
  zan_slab_table *tbl = __atomic_load_n(&g_slab_tbl, __ATOMIC_ACQUIRE);
  uint32_t mask = tbl->mask;
  uintptr_t *slots = tbl->slots;
  unsigned i = zan_mem_hash(base, mask);
  for (unsigned n = 0; n < 64; n++) {
    uintptr_t v = __atomic_load_n(&slots[(i + n) & mask], __ATOMIC_ACQUIRE);
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
static inline int zan_mem_block_class(const void *p) {
  unsigned pi;
  zan_mem_slab_meta *m = zan_mem_meta_of(p, &pi);
  if (__builtin_expect(pi == 0, 0))
    return -1;
  unsigned c1 = __atomic_load_n(&m->cls1[pi], __ATOMIC_ACQUIRE);
  if (__builtin_expect(c1 == 0 || c1 > (unsigned)ZAN_MEM_NCLASS, 0))
    return -1;
  int cls = (int)c1 - 1;
  uintptr_t off = (uintptr_t)p & (uintptr_t)(ZAN_MEM_PAGE - 1);
  size_t sz = k_class_size[cls];
  uint64_t dm = k_class_divm[cls];
  if (__builtin_expect((uint64_t)off * dm > dm - 1u ||
                           off + sz > zan_mem_page_span(cls),
                       0))
    return -1;
  return cls;
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

static __attribute__((noinline)) zan_mem_cache *zan_mem_cache_get_slow(void) {
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
  if (++g_cache_init_count > 1)
    __atomic_store_n(&g_multi_thread, 1, __ATOMIC_RELEASE);
  c = __atomic_load_n(&g_cache_pool, __ATOMIC_RELAXED);
  if (c)
    __atomic_store_n(&g_cache_pool, c->next_free, __ATOMIC_RELEASE);
  zan_mem_unlock();
  if (!c) {
    c = (zan_mem_cache *)__real_malloc(sizeof *c);
    if (!c)
      return NULL;
    memset(c, 0, sizeof *c);
  } else {
    c->next_free = NULL;
    zan_mem_drain_remote_all(c);
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

static void zan_mem_drain_stripe(zan_mem_cache *c, int s) {
  void *p = __atomic_exchange_n(&c->remote[s].head, NULL, __ATOMIC_ACQUIRE);
  while (p) {
    int cls = zan_mem_block_class(p);
    if (cls < 0)
      break; /* 链已损坏：丢弃余下的块 */
    void **link = zan_mem_link(p, cls);
    void *next = *link;
    *link = c->free_list[cls];
    c->free_list[cls] = p;
    p = next;
  }
}

/* 快路径：线程已有缓存时只是一次 TLS 读（曾整个函数不内联，每次分配
 * 多付一次调用） */
static inline __attribute__((always_inline)) zan_mem_cache *
zan_mem_cache_get(void) {
  zan_mem_cache *c = t_cache;
  if (__builtin_expect(c != NULL, 1))
    return c;
  return zan_mem_cache_get_slow();
}

static void zan_mem_drain_remote(zan_mem_cache *c) {
  if (!__atomic_load_n(&c->remote_mask, __ATOMIC_ACQUIRE))
    return;
  uint32_t mask = __atomic_exchange_n(&c->remote_mask, 0, __ATOMIC_ACQ_REL);
  while (mask) {
    int s = __builtin_ctz(mask);
    mask &= mask - 1u;
    zan_mem_drain_stripe(c, s);
  }
}

static void zan_mem_drain_remote_all(zan_mem_cache *c) {
  __atomic_store_n(&c->remote_mask, 0, __ATOMIC_RELEASE);
  for (int s = 0; s < ZAN_MEM_REMOTE_STRIPES; s++) {
    if (!__atomic_load_n(&c->remote[s].head, __ATOMIC_RELAXED))
      continue;
    zan_mem_drain_stripe(c, s);
  }
}

static void zan_mem_retire(zan_mem_cache *c) {
  if (c->alloc_batch) {
    __atomic_fetch_add(&g_small_alloc_count, (size_t)c->alloc_batch,
                       __ATOMIC_RELAXED);
    c->alloc_batch = 0;
  }
  zan_mem_drain_remote_all(c);
  if (c->bump && c->bump_end && !c->bump_decommitted) {
    uintptr_t start = ((uintptr_t)c->bump + 4095) & ~(uintptr_t)4095;
#if defined(_WIN32)
    uintptr_t end = (uintptr_t)c->bump_committed & ~(uintptr_t)4095;
#else
    uintptr_t end = (uintptr_t)c->bump_end & ~(uintptr_t)4095;
#endif
    if (end > start && (end - start) >= 65536) {
      size_t bytes = end - start;
#if defined(_WIN32)
      VirtualFree((void *)start, bytes, MEM_DECOMMIT);
      c->bump_committed = c->bump;
#elif defined(ZAN_MEM_HAVE_MMAP)
      madvise((void *)start, bytes, MADV_DONTNEED);
#endif
      c->bump_decommitted = 1;
      __atomic_fetch_add(&g_trimmed_bytes, bytes, __ATOMIC_RELAXED);
    }
  }
  zan_mem_lock();
  c->next_free = __atomic_load_n(&g_cache_pool, __ATOMIC_RELAXED);
  __atomic_store_n(&g_cache_pool, c, __ATOMIC_RELEASE);
  zan_mem_unlock();
}

static void *zan_mem_harvest_free_block(zan_mem_cache *c, int cls) {
  if (!__atomic_load_n(&g_cache_pool, __ATOMIC_ACQUIRE))
    return NULL;
  zan_mem_lock();
  for (zan_mem_cache *rc = g_cache_pool; rc; rc = rc->next_free) {
    zan_mem_drain_remote_all(rc);
    void *p = rc->free_list[cls];
    if (p) {
      rc->free_list[cls] = NULL;
      c->free_list[cls] = *zan_mem_link(p, cls);
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
  if (!__atomic_load_n(&g_cache_pool, __ATOMIC_ACQUIRE))
    return 0;
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
      c->last_slab = (uintptr_t)m;
      __atomic_store_n(&m->owner[pi], c, __ATOMIC_RELEASE);
      zan_mem_unlock();
      return 1;
    }
  }
  zan_mem_unlock();
  return 0;
}

static int zan_mem_adopt_retired_bump(zan_mem_cache *c, size_t need) {
  if (!__atomic_load_n(&g_cache_pool, __ATOMIC_ACQUIRE))
    return 0;
  zan_mem_lock();
  for (zan_mem_cache *rc = g_cache_pool; rc; rc = rc->next_free) {
    if ((size_t)(rc->bump_end - rc->bump) >= need) {
      c->bump = rc->bump;
      c->bump_end = rc->bump_end;
      c->bump_committed = rc->bump_committed;
      c->bump_decommitted = rc->bump_decommitted;
      rc->bump = NULL;
      rc->bump_end = NULL;
      rc->bump_committed = NULL;
      rc->bump_decommitted = 0;
      zan_mem_unlock();
      return 1;
    }
  }
  zan_mem_unlock();
  return 0;
}

static uintptr_t zan_mem_os_entropy(void) {
  uintptr_t ent = 0;
#if defined(_WIN32)
  /* Do not call LoadLibrary inside malloc lock; query already-loaded DLLs only. */
  HMODULE adv = GetModuleHandleA("advapi32.dll");
  if (adv) {
    typedef BOOLEAN(WINAPI * rtl_gen_random_fn)(PVOID, ULONG);
    rtl_gen_random_fn fn =
        (rtl_gen_random_fn)(void *)GetProcAddress(adv, "SystemFunction036");
    if (fn)
      (void)fn(&ent, (ULONG)sizeof(ent));
  }
  LARGE_INTEGER qpc;
  QueryPerformanceCounter(&qpc);
  ent ^= (uintptr_t)qpc.QuadPart ^
         ((uintptr_t)GetCurrentProcessId() << 32) ^
         (uintptr_t)GetCurrentThreadId();
#elif defined(__APPLE__)
  arc4random_buf(&ent, sizeof(ent));
#elif defined(__linux__) && defined(SYS_getrandom)
  (void)syscall(SYS_getrandom, &ent, sizeof(ent), 1u /* GRND_NONBLOCK */);
  ent ^= ((uintptr_t)getpid() << 16);
#elif defined(ZAN_MEM_HAVE_MMAP)
  ent ^= ((uintptr_t)getpid() << 16);
#endif
  return ent;
}

static int zan_mem_publish_slab(zan_mem_cache *c, uintptr_t base) {
  uintptr_t ent = 0;
  if (!__atomic_load_n(&g_mem_secret, __ATOMIC_RELAXED))
    ent = zan_mem_os_entropy();
  zan_mem_lock();
  if (!g_mem_secret) {
    uint64_t x = (uint64_t)ent ^ (uint64_t)(uintptr_t)&g_mem_secret ^
                 ((uint64_t)base * UINT64_C(0x9E3779B97F4A7C15)) ^
                 (uint64_t)(uintptr_t)c;
    x = (x ^ (x >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94D049BB133111EB);
    x = x ^ (x >> 31);
    __atomic_store_n(&g_mem_secret, (uintptr_t)x | (uintptr_t)1,
                     __ATOMIC_RELEASE);
  }
  uint32_t min_cap = 0;
  for (;;) {
    zan_slab_table *tbl = g_slab_tbl;
    uint32_t want_cap = 0;
    if (tbl->cap < min_cap) {
      want_cap = min_cap;
    } else if ((g_slab_count + 1) * 2 > tbl->cap) {
      want_cap = tbl->cap * 2u;
    } else {
      unsigned i = zan_mem_hash(base, tbl->mask);
      unsigned n = 0;
      while (n < 64 && tbl->slots[(i + n) & tbl->mask] != 0)
        n++;
      if (n < 64) {
        __atomic_store_n(&tbl->slots[(i + n) & tbl->mask], base,
                         __ATOMIC_RELEASE);
        __atomic_store_n(&g_slab_count, g_slab_count + 1, __ATOMIC_RELEASE);
        break;
      }
      want_cap = tbl->cap * 2u;
    }
    if (want_cap == 0 || want_cap > (1u << 24)) {
      zan_mem_unlock();
      return 0;
    }
    /* 在锁外分配新表，避免持有全局自旋锁时重入 CRT allocator */
    zan_mem_unlock();
    zan_slab_table *nt = zan_mem_alloc_slab_table(want_cap);
    if (!nt)
      return 0;
    zan_mem_lock();
    if (g_slab_tbl->cap >= want_cap) {
      zan_mem_unlock();
      __real_free(nt);
      zan_mem_lock();
      continue;
    }
    zan_slab_table *old = g_slab_tbl;
    int ok = 1;
    for (uint32_t k = 0; k < old->cap; k++) {
      uintptr_t v = old->slots[k];
      if (!v)
        continue;
      unsigned j = zan_mem_hash(v, nt->mask);
      unsigned m = 0;
      while (m < 64 && nt->slots[(j + m) & nt->mask] != 0)
        m++;
      if (m == 64) {
        ok = 0;
        break;
      }
      nt->slots[(j + m) & nt->mask] = v;
    }
    if (ok) {
      unsigned i = zan_mem_hash(base, nt->mask);
      unsigned n = 0;
      while (n < 64 && nt->slots[(i + n) & nt->mask] != 0)
        n++;
      if (n < 64) {
        nt->slots[(i + n) & nt->mask] = base;
        nt->prev = old;
        __atomic_store_n(&g_slab_tbl, nt, __ATOMIC_RELEASE);
        __atomic_store_n(&g_slab_count, g_slab_count + 1, __ATOMIC_RELEASE);
        break;
      }
    }
    min_cap = want_cap * 2u;
    zan_mem_unlock();
    __real_free(nt);
    zan_mem_lock();
  }
  zan_mem_unlock();

  /* 第 0 页是页表（mmap/VirtualAlloc 交来即为零：全部页未分配） */
  c->bump = (char *)base + ZAN_MEM_PAGE;
  c->bump_end = (char *)base + ZAN_MEM_SLAB;
#if defined(_WIN32)
  c->bump_committed = c->bump;
#else
  c->bump_committed = c->bump_end;
#endif
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
  /* 仅先提交第 0 页（slab 页表），数据页在切页时按 64 KiB 块按需提交，
   * 避免多 worker 线程少量小分配就预占整块 1 MiB 提交配额。 */
  if (!VirtualAlloc((void *)base, ZAN_MEM_PAGE, MEM_COMMIT, PAGE_READWRITE)) {
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

static char *zan_mem_pop_reclaimed_page(void) {
  if (!__atomic_load_n(&g_reclaimed_head, __ATOMIC_ACQUIRE))
    return NULL;
  zan_mem_lock();
  zan_mem_slab_meta *m = g_reclaimed_head;
  while (m && m->reclaimed_mask == 0) {
    m->in_reclaimed_list = 0;
    m = m->reclaimed_next;
    __atomic_store_n(&g_reclaimed_head, m, __ATOMIC_RELEASE);
  }
  if (!m) {
    zan_mem_unlock();
    return NULL;
  }
  unsigned pi = (unsigned)__builtin_ctzll(m->reclaimed_mask);
  m->reclaimed_mask &= ~(1ull << pi);
  if (m->reclaimed_mask == 0) {
    m->in_reclaimed_list = 0;
    __atomic_store_n(&g_reclaimed_head, m->reclaimed_next, __ATOMIC_RELEASE);
    m->reclaimed_next = NULL;
  }
  zan_mem_unlock();

  char *page = (char *)m + (size_t)pi * ZAN_MEM_PAGE;
#if defined(_WIN32)
  if (!VirtualAlloc((void *)page, ZAN_MEM_PAGE, MEM_COMMIT, PAGE_READWRITE)) {
    zan_mem_lock();
    m->reclaimed_mask |= (1ull << pi);
    if (!m->in_reclaimed_list) {
      m->reclaimed_next = g_reclaimed_head;
      m->in_reclaimed_list = 1;
      __atomic_store_n(&g_reclaimed_head, m, __ATOMIC_RELEASE);
    }
    zan_mem_unlock();
    return NULL;
  }
#endif
  return page;
}

/* 慢路径：本线程空闲链为空且当前页已切完——收远端释放、收割退役缓存、复用空页、切新页 */
static __attribute__((noinline)) void *zan_mem_small_slow(zan_mem_cache *c,
                                                          int cls) {
  void *p = c->free_list[cls];
  if (!p) {
    zan_mem_drain_remote(c);
    p = c->free_list[cls];
  }
  if (p) {
    c->free_list[cls] = *zan_mem_link(p, cls);
    *zan_mem_cookie_word(p, cls) = 0; /* 清掉空闲 cookie */
    zan_mem_count_alloc(c);
    return p;
  }
  p = zan_mem_harvest_free_block(c, cls);
  if (p) {
    *zan_mem_cookie_word(p, cls) = 0;
    zan_mem_count_alloc(c);
    return p;
  }
  size_t sz = k_class_size[cls];
  if (!c->page_bump[cls] || (size_t)(c->page_end[cls] - c->page_bump[cls]) < sz) {
    if (!zan_mem_adopt_retired_page(c, cls)) {
      char *page = zan_mem_pop_reclaimed_page();
      if (!page) {
        /* 从当前 slab 的未分出页区间切一整页给这个类 */
        if ((size_t)(c->bump_end - c->bump) < ZAN_MEM_PAGE) {
          if (!zan_mem_adopt_retired_bump(c, ZAN_MEM_PAGE)) {
            if (!zan_mem_new_slab(c))
              return NULL;
          }
        }
#if defined(_WIN32)
        if (c->bump_decommitted || c->bump_committed <= c->bump ||
            (size_t)(c->bump_committed - c->bump) < ZAN_MEM_PAGE) {
          size_t avail = (size_t)(c->bump_end - c->bump);
          size_t chunk =
              avail < ZAN_MEM_COMMIT_CHUNK ? avail : ZAN_MEM_COMMIT_CHUNK;
          if (!VirtualAlloc((void *)c->bump, chunk, MEM_COMMIT, PAGE_READWRITE))
            return NULL;
          c->bump_committed = c->bump + chunk;
          c->bump_decommitted = 0;
        }
#else
        c->bump_decommitted = 0;
#endif
        page = c->bump;
        c->bump += ZAN_MEM_PAGE;
      }
      unsigned pi;
      zan_mem_slab_meta *m = zan_mem_meta_of(page, &pi);
      c->last_slab = (uintptr_t)m;
      __atomic_store_n(&m->owner[pi], c, __ATOMIC_RELAXED);
      __atomic_store_n(&m->cls1[pi], (uint8_t)(cls + 1), __ATOMIC_RELEASE);
      c->page_bump[cls] = page;
      c->page_end[cls] = page + zan_mem_page_span(cls);
    }
  }
  p = c->page_bump[cls];
  c->page_bump[cls] += sz;
  *zan_mem_cookie_word(p, cls) = 0;
  zan_mem_count_alloc(c);
  return p;
}

/* 在 g_trim_lock 保护下扫描缓存的各尺寸类空闲链：若某个 16 KiB 页的全部
 * cap 个块都在空闲链中（零活对象），将其摘链、清页表并反提交给 OS，挂入
 * slab 的 reclaimed_mask 供后续任意尺寸类跨类复用（热路径零开销）。 */
static size_t zan_mem_trim_cache_pages(zan_mem_cache *c, int lock_held) {
  size_t trimmed = 0;
  zan_mem_drain_remote_all(c);
  for (int cls = 0; cls < ZAN_MEM_NCLASS; cls++) {
    void *head = c->free_list[cls];
    if (!head)
      continue;
    size_t sz = k_class_size[cls];
    uint16_t cap = (uint16_t)(zan_mem_page_span(cls) / sz);
    uintptr_t partial_page = 0;
    if (c->page_bump[cls] &&
        (size_t)(c->page_end[cls] - c->page_bump[cls]) >= sz) {
      partial_page =
          (uintptr_t)c->page_bump[cls] & ~(uintptr_t)(ZAN_MEM_PAGE - 1);
    }

    zan_mem_slab_meta *touched = NULL;
    int found_empty = 0;
    for (void *p = head; p; p = *zan_mem_link(p, cls)) {
      unsigned pi;
      zan_mem_slab_meta *m = zan_mem_meta_of(p, &pi);
      if (m->trim_touched == 0) {
        m->trim_next = touched;
        touched = m;
      }
      m->trim_touched |= (1ull << pi);
      uint16_t cnt = ++m->trim_cnt[pi];
      if (cnt == cap) {
        uintptr_t paddr = (uintptr_t)m + (size_t)pi * ZAN_MEM_PAGE;
        if (paddr != partial_page)
          found_empty = 1;
      }
    }

    if (found_empty) {
      void **pp = &c->free_list[cls];
      while (*pp) {
        void *cur = *pp;
        unsigned pi;
        zan_mem_slab_meta *m = zan_mem_meta_of(cur, &pi);
        uintptr_t paddr = (uintptr_t)m + (size_t)pi * ZAN_MEM_PAGE;
        if (m->trim_cnt[pi] == cap && paddr != partial_page) {
          *pp = *zan_mem_link(cur, cls);
        } else {
          pp = zan_mem_link(cur, cls);
        }
      }
      if (c->page_bump[cls] && !partial_page) {
        unsigned pi;
        zan_mem_slab_meta *m = zan_mem_meta_of(c->page_end[cls] - 1, &pi);
        if (m->trim_cnt[pi] == cap) {
          c->page_bump[cls] = NULL;
          c->page_end[cls] = NULL;
        }
      }
      for (zan_mem_slab_meta *m = touched; m;) {
        zan_mem_slab_meta *next = m->trim_next;
        uint64_t mask = m->trim_touched;
        uint64_t reclaimed = 0;
        while (mask) {
          unsigned pi = (unsigned)__builtin_ctzll(mask);
          mask &= mask - 1ull;
          uintptr_t paddr = (uintptr_t)m + (size_t)pi * ZAN_MEM_PAGE;
          if (m->trim_cnt[pi] == cap && paddr != partial_page) {
            __atomic_store_n(&m->cls1[pi], 0, __ATOMIC_RELEASE);
            __atomic_store_n(&m->owner[pi], NULL, __ATOMIC_RELEASE);
#if defined(_WIN32)
            VirtualFree((void *)paddr, ZAN_MEM_PAGE, MEM_DECOMMIT);
#elif defined(ZAN_MEM_HAVE_MMAP)
            madvise((void *)paddr, ZAN_MEM_PAGE, MADV_DONTNEED);
#endif
            reclaimed |= (1ull << pi);
            trimmed += ZAN_MEM_PAGE;
          }
          m->trim_cnt[pi] = 0;
        }
        m->trim_touched = 0;
        m->trim_next = NULL;
        if (reclaimed) {
          if (!lock_held)
            zan_mem_lock();
          m->reclaimed_mask |= reclaimed;
          if (!m->in_reclaimed_list) {
            m->reclaimed_next = g_reclaimed_head;
            m->in_reclaimed_list = 1;
            __atomic_store_n(&g_reclaimed_head, m, __ATOMIC_RELEASE);
          }
          if (!lock_held)
            zan_mem_unlock();
        }
        m = next;
      }
    } else {
      for (zan_mem_slab_meta *m = touched; m;) {
        zan_mem_slab_meta *next = m->trim_next;
        uint64_t mask = m->trim_touched;
        while (mask) {
          unsigned pi = (unsigned)__builtin_ctzll(mask);
          mask &= mask - 1ull;
          m->trim_cnt[pi] = 0;
        }
        m->trim_touched = 0;
        m->trim_next = NULL;
        m = next;
      }
    }
  }
  return trimmed;
}

/* 快路径（内联进各分配入口）：弹本线程空闲链 */
static inline __attribute__((always_inline)) void *zan_mem_small_cls(int cls) {
  zan_mem_cache *c = zan_mem_cache_get();
  if (__builtin_expect(!c, 0))
    return NULL;
  void *p = c->free_list[cls];
  if (__builtin_expect(p != NULL, 1)) {
    c->free_list[cls] = *zan_mem_link(p, cls);
    *zan_mem_cookie_word(p, cls) = 0; /* 清掉空闲 cookie */
    zan_mem_count_alloc(c);
    return p;
  }
  if (__builtin_expect(
          __atomic_load_n(&c->remote_mask, __ATOMIC_RELAXED) != 0, 0)) {
    zan_mem_drain_remote(c);
    p = c->free_list[cls];
    if (p) {
      c->free_list[cls] = *zan_mem_link(p, cls);
      *zan_mem_cookie_word(p, cls) = 0;
      zan_mem_count_alloc(c);
      return p;
    }
  }
  /* 空闲链空且无远端待收、本类当前页还能切：直接切。 */
  char *pb = c->page_bump[cls];
  size_t sz = k_class_size[cls];
  if (__builtin_expect(pb != NULL && (size_t)(c->page_end[cls] - pb) >= sz, 1)) {
    c->page_bump[cls] = pb + sz;
    *zan_mem_cookie_word(pb, cls) = 0;
    zan_mem_count_alloc(c);
    return pb;
  }
  return zan_mem_small_slow(c, cls);
}

static void *zan_mem_small(size_t n) {
  if (__builtin_expect(!__atomic_load_n(&g_class_ready, __ATOMIC_ACQUIRE), 0))
    zan_mem_ensure_classes();
  return zan_mem_small_cls(g_size_class[n]);
}

size_t zan_mem_trim(void) {
  size_t total_trimmed = 0;
  /* 锁顺序固定：g_trim_lock -> zan_mem_lock，杜绝死锁。 */
  zan_mem_trim_lock();
  zan_mem_lock();
  for (zan_mem_cache *rc = g_cache_pool; rc; rc = rc->next_free) {
    total_trimmed += zan_mem_trim_cache_pages(rc, 1);
    if (!rc->bump || !rc->bump_end || rc->bump_decommitted)
      continue;
    uintptr_t start = ((uintptr_t)rc->bump + 4095) & ~(uintptr_t)4095;
#if defined(_WIN32)
    uintptr_t end = (uintptr_t)rc->bump_committed & ~(uintptr_t)4095;
#else
    uintptr_t end = (uintptr_t)rc->bump_end & ~(uintptr_t)4095;
#endif
    if (end > start) {
      size_t bytes = end - start;
#if defined(_WIN32)
      VirtualFree((void *)start, bytes, MEM_DECOMMIT);
      rc->bump_committed = rc->bump;
#elif defined(ZAN_MEM_HAVE_MMAP)
      madvise((void *)start, bytes, MADV_DONTNEED);
#endif
      rc->bump_decommitted = 1;
      total_trimmed += bytes;
    }
  }
  zan_mem_unlock();

  zan_mem_cache *self = t_cache;
  if (self) {
    total_trimmed += zan_mem_trim_cache_pages(self, 0);
    if (self->bump && self->bump_end && !self->bump_decommitted) {
      uintptr_t start = ((uintptr_t)self->bump + 4095) & ~(uintptr_t)4095;
#if defined(_WIN32)
      uintptr_t end = (uintptr_t)self->bump_committed & ~(uintptr_t)4095;
#else
      uintptr_t end = (uintptr_t)self->bump_end & ~(uintptr_t)4095;
#endif
      if (end > start && (end - start) >= 65536) {
        size_t bytes = end - start;
#if defined(_WIN32)
        VirtualFree((void *)start, bytes, MEM_DECOMMIT);
        self->bump_committed = self->bump;
#elif defined(ZAN_MEM_HAVE_MMAP)
        madvise((void *)start, bytes, MADV_DONTNEED);
#endif
        self->bump_decommitted = 1;
        total_trimmed += bytes;
      }
    }
  }
  zan_mem_trim_unlock();

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

/* Zan 字符串分配（编译器的 zan_rt_str_alloc 在链了本对象时调它）：要 n 字节
 * 真实存储（12 字节串头 + 负载），返回虚基址 V——串块从 V+4 起、用户指针
 * 是 V+16，释放传 V（free/zan_mem_free/realloc/libc free 都认）。
 * 超出串页类的大串退回 malloc(n + 4)：V 即块起点，V[0..3] 是串的空位。 */
void *zan_mem_str_malloc(size_t n) {
  if (n <= ZAN_MEM_STR_MAX) {
    size_t k = n <= ZAN_MEM_STR_MIN ? ZAN_MEM_STR_MIN : (n + 7u) & ~(size_t)7u;
    void *p =
        zan_mem_small_cls(ZAN_MEM_STR_CLS0 + (int)((k - ZAN_MEM_STR_MIN) / 8u));
    if (p)
      return p;
  }
  return __wrap_malloc(n + ZAN_MEM_STR_SKEW);
}

/* Zan 对象分配（编译器的 zan_rt_alloc 在链了本对象时调它）：n = 8 字节对象
 * 头 + 载荷（对齐 16 的类再前垫 8）。n 是 16 的奇数倍 8 时进 8 字节粒度的对象页类，否则同 malloc。
 * 8 字节对齐只在 n % 16 == 8 时出现，而 LLVM 里对齐要求 16 的类型尺寸必是
 * 16 的倍数——含这类字段的对象 n % 16 == 0，照旧拿 16 对齐的通用类块。 */
void *zan_mem_obj_malloc(size_t n) {
  if (n <= ZAN_MEM_OBJ_MAX && (n & 15u) == 8u) {
    void *p = zan_mem_small_cls(ZAN_MEM_OBJ_CLS0 + (int)(n >> 4) - 1);
    if (p)
      return p;
  }
  return __wrap_malloc(n);
}

/* 冷路径单独成函数：内联进 free 会把 snprintf 缓冲带进热路径（栈保护 +
 * 栈帧开销每次释放都付）。 */
static __attribute__((noinline, cold)) void zan_mem_fatal_block(const char *what,
                                                                const void *p) {
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
  zan_mem_cache *self = t_cache;
  uintptr_t base = (uintptr_t)p & ~(uintptr_t)(ZAN_MEM_SLAB - 1);
  if (__builtin_expect(!(self != NULL && base == self->last_slab), 0)) {
    if (!zan_mem_owns(p)) {
      __real_free(p);
      return;
    }
  }
  int cls = zan_mem_block_class(p);
  if (__builtin_expect(cls < 0, 0)) {
    zan_mem_fatal_block("invalid free (not a block start)", p);
    return; /* 致命处理器返回时 */
  }
  unsigned pi = (unsigned)(((uintptr_t)p - base) >> ZAN_MEM_PAGE_SHIFT);
  zan_mem_slab_meta *m = (zan_mem_slab_meta *)base;
  uintptr_t ck = zan_mem_cookie(p);
  uintptr_t *w = zan_mem_cookie_word(p, cls);
  void **link = zan_mem_link(p, cls);
  zan_mem_cache *owner = __atomic_load_n(&m->owner[pi], __ATOMIC_RELAXED);
  if (__builtin_expect(owner == self && self != NULL, 1)) {
    if (__builtin_expect(!__atomic_load_n(&g_multi_thread, __ATOMIC_RELAXED),
                         1)) {
      if (__builtin_expect(*w == ck, 0)) {
        zan_mem_fatal_block("double free", p);
        return;
      }
      *w = ck;
    } else if (__builtin_expect(
                   __atomic_exchange_n(w, ck, __ATOMIC_ACQ_REL) == ck, 0)) {
      zan_mem_fatal_block("double free", p);
      return;
    }
    self->last_slab = base;
    *link = self->free_list[cls];
    self->free_list[cls] = p;
    return;
  }
  __atomic_store_n(&g_multi_thread, 1, __ATOMIC_RELEASE);
  if (__builtin_expect(
          __atomic_exchange_n(w, ck, __ATOMIC_ACQ_REL) == ck, 0)) {
    zan_mem_fatal_block("double free", p);
    return;
  }
  owner = __atomic_load_n(&m->owner[pi], __ATOMIC_ACQUIRE);
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
    *link = head;
  } while (!__atomic_compare_exchange_n(slot, &head, p, 1, __ATOMIC_RELEASE,
                                        __ATOMIC_RELAXED));
  if (!head) {
    __atomic_fetch_or(&owner->remote_mask, (uint32_t)(1u << s),
                      __ATOMIC_RELEASE);
  }

  if (self && self != owner &&
      __atomic_load_n(&self->remote_mask, __ATOMIC_RELAXED) != 0) {
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
  if (__atomic_load_n(zan_mem_cookie_word(p, cls), __ATOMIC_ACQUIRE) ==
      zan_mem_cookie(p)) {
    zan_mem_fatal_block("realloc of freed block", p);
    return NULL;
  }
  size_t old = k_class_size[cls];
  if (zan_mem_is_str_cls(cls)) {
    /* 串页块从 V 看是 old + 4 字节，但 V[0..3] 是邻块的：绝不原地交回
     * （调用方会写那 4 字节），一律搬到通用块，布局不变（新块 [0..3]
     * 成了自己的空位）。 */
    old += ZAN_MEM_STR_SKEW;
    void *np = __wrap_malloc(n);
    if (!np)
      return NULL;
    memcpy(np, p, n < old ? n : old);
    __wrap_free(p);
    return np;
  }
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
  if (__atomic_load_n(zan_mem_cookie_word((void *)p, cls), __ATOMIC_ACQUIRE) ==
      zan_mem_cookie(p))
    return 0; /* 已释放 */
  return k_class_size[cls] + (zan_mem_is_str_cls(cls) ? ZAN_MEM_STR_SKEW : 0u);
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
