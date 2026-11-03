
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

/* The real allocator, behind the linker wrap. */
void *__real_malloc(size_t n);
void  __real_free(void *p);
void *__real_calloc(size_t n, size_t m);
void *__real_realloc(void *p, size_t n);

#define ZAN_MEM_MAX_SMALL   2048u
#define ZAN_MEM_SLAB        (1u << 20)      /* 1 MiB */

typedef struct {
    uint32_t magic;
    uint32_t cls;
    struct zan_mem_cache *owner;
} zan_mem_hdr_t;

_Static_assert(sizeof(zan_mem_hdr_t) == ZAN_OBJ_HDR_SIZE,
               "allocator header must keep payloads 16-byte aligned "
               "for the compiler's object header (zan_abi.h)");

#define ZAN_MEM_MAGIC 0x5A414E4DU     /* "ZANM" */
#define ZAN_MEM_FREED 0x5A414E46U
#define ZAN_MEM_HDR   ((size_t)sizeof(zan_mem_hdr_t))

static const uint16_t k_class_size[] = {
      16,   32,   48,   64,   80,   96,  112,  128,
     160,  192,  224,  256,  320,  384,  448,  512,
     640,  768,  896, 1024, 1280, 1536, 1792, 2048
};
#define ZAN_MEM_NCLASS ((int)(sizeof(k_class_size) / sizeof(k_class_size[0])))

static unsigned char g_size_class[ZAN_MEM_MAX_SMALL + 1];
static int g_class_ready;

#define ZAN_MEM_SET_SIZE  (1u << 14)
#define ZAN_MEM_SET_MASK  (ZAN_MEM_SET_SIZE - 1u)
static uintptr_t g_slab_set[ZAN_MEM_SET_SIZE];
static size_t g_slab_count;
static size_t g_small_alloc_count;
static size_t g_trimmed_bytes;

size_t zan_mem_slabs(void) {
    return __atomic_load_n(&g_slab_count, __ATOMIC_RELAXED);
}

void zan_mem_get_stats(zan_mem_stats_t *out) {
    if (!out) return;
    size_t slabs = __atomic_load_n(&g_slab_count, __ATOMIC_RELAXED);
    out->slab_count = slabs;
    out->slab_bytes = slabs * (size_t)ZAN_MEM_SLAB;
    out->small_alloc_count = __atomic_load_n(&g_small_alloc_count, __ATOMIC_RELAXED);
    out->trimmed_bytes = __atomic_load_n(&g_trimmed_bytes, __ATOMIC_RELAXED);
}

#define ZAN_MEM_REMOTE_STRIPES 8

typedef struct zan_mem_cache {
    void  *free_list[ZAN_MEM_NCLASS];   /* owner-only */
    char  *bump;                        /* owner-only */
    char  *bump_end;                    /* owner-only */
    int    bump_decommitted;            /* unallocated bump pages decommitted to OS */
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
static DWORD g_fls = FLS_OUT_OF_INDEXES;
static int g_fls_state;
static void WINAPI zan_mem_fls_cb(void *p) {
    if (p) zan_mem_retire((zan_mem_cache *)p);
}
#else
#include <pthread.h>
#include <sched.h>
static __thread zan_mem_cache *t_cache;
static pthread_key_t g_exit_key;
static int g_exit_key_state;
static void zan_mem_thread_exit(void *p) {
    t_cache = NULL;
    if (p) zan_mem_retire((zan_mem_cache *)p);
}
#endif

static volatile int g_slab_lock;

static void zan_mem_backoff(int spins) {
    if (spins < 64) {
#if defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause();
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
        if (!__sync_lock_test_and_set(&g_slab_lock, 1)) return;
        zan_mem_backoff(spins);
    }
}

static void zan_mem_unlock(void) { __sync_lock_release(&g_slab_lock); }

static void zan_mem_build_classes(void) {
    int c = 0;
    for (unsigned s = 0; s <= ZAN_MEM_MAX_SMALL; s++) {
        while (c < ZAN_MEM_NCLASS && k_class_size[c] < s) c++;
        g_size_class[s] = (unsigned char)(c < ZAN_MEM_NCLASS ? c : ZAN_MEM_NCLASS - 1);
    }
    __atomic_store_n(&g_class_ready, 1, __ATOMIC_RELEASE);
}

static void zan_mem_ensure_classes(void) {
    if (__atomic_load_n(&g_class_ready, __ATOMIC_ACQUIRE)) return;
    zan_mem_lock();
    if (!g_class_ready) zan_mem_build_classes();
    zan_mem_unlock();
}

static unsigned zan_mem_hash(uintptr_t base) {
    uint64_t h = (uint64_t)(base >> 20) * 0x9E3779B97F4A7C15ull;
    return (unsigned)(h >> 40) & ZAN_MEM_SET_MASK;
}

static int zan_mem_owns(const void *p) {
    if (!__atomic_load_n(&g_slab_count, __ATOMIC_ACQUIRE)) return 0;
    uintptr_t base = (uintptr_t)p & ~(uintptr_t)(ZAN_MEM_SLAB - 1);
    unsigned i = zan_mem_hash(base);
    for (unsigned n = 0; n < 64; n++) {
        uintptr_t v = __atomic_load_n(&g_slab_set[(i + n) & ZAN_MEM_SET_MASK],
                                      __ATOMIC_ACQUIRE);
        if (v == base) return 1;
        if (v == 0) return 0;
    }
    return 0;
}

static zan_mem_cache *zan_mem_cache_get(void) {
#if defined(_WIN32)
    int fst = __atomic_load_n(&g_fls_state, __ATOMIC_ACQUIRE);
    if (fst == 0 && __atomic_compare_exchange_n(&g_fls_state, &fst, 1, 0,
                                                __ATOMIC_ACQ_REL,
                                                __ATOMIC_ACQUIRE)) {
        DWORD idx = FlsAlloc(zan_mem_fls_cb);
        __atomic_store_n(&g_fls, idx, __ATOMIC_RELEASE);
        __atomic_store_n(&g_fls_state, idx == FLS_OUT_OF_INDEXES ? 3 : 2,
                         __ATOMIC_RELEASE);
        fst = idx == FLS_OUT_OF_INDEXES ? 3 : 2;
    }
    if (fst != 2) return NULL;
    DWORD fls = __atomic_load_n(&g_fls, __ATOMIC_ACQUIRE);
    zan_mem_cache *c = (fls == FLS_OUT_OF_INDEXES)
                       ? NULL : (zan_mem_cache *)FlsGetValue(fls);
    if (c) return c;
#else
    zan_mem_cache *c = t_cache;
    if (c) return c;
    int st = __atomic_load_n(&g_exit_key_state, __ATOMIC_ACQUIRE);
    if (st == 0 && __atomic_compare_exchange_n(&g_exit_key_state, &st, 1, 0,
                                               __ATOMIC_ACQ_REL,
                                               __ATOMIC_ACQUIRE)) {
        int ok = pthread_key_create(&g_exit_key, zan_mem_thread_exit) == 0;
        __atomic_store_n(&g_exit_key_state, ok ? 2 : 3, __ATOMIC_RELEASE);
        st = ok ? 2 : 3;
    } else {
        while ((st = __atomic_load_n(&g_exit_key_state, __ATOMIC_ACQUIRE)) == 1) {
#if defined(__i386__) || defined(__x86_64__)
            __builtin_ia32_pause();
#endif
        }
    }
    if (st != 2) return NULL;
#endif
    zan_mem_lock();
    c = g_cache_pool;
    if (c) g_cache_pool = c->next_free;
    zan_mem_unlock();
    if (!c) {
        c = (zan_mem_cache *)__real_malloc(sizeof *c);
        if (!c) return NULL;
        memset(c, 0, sizeof *c);
    } else {
        c->next_free = NULL;
        zan_mem_drain_remote(c);
    }
#if defined(_WIN32)
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
    if (!g_cache_pool) return NULL;
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
    if (!g_cache_pool) return 0;
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
    while (n < 64 && g_slab_set[(i + n) & ZAN_MEM_SET_MASK] != 0) n++;
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
    if (!raw) return 0;
    uintptr_t base = ((uintptr_t)raw + (ZAN_MEM_SLAB - 1))
                     & ~(uintptr_t)(ZAN_MEM_SLAB - 1);
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
    if (raw == MAP_FAILED) return 0;
    uintptr_t base = ((uintptr_t)raw + (ZAN_MEM_SLAB - 1))
                     & ~(uintptr_t)(ZAN_MEM_SLAB - 1);
    size_t head = (size_t)(base - (uintptr_t)raw);
    if (head) munmap(raw, head);
    size_t tail = span - head - ZAN_MEM_SLAB;
    if (tail) munmap((char *)(base + ZAN_MEM_SLAB), tail);

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
    if (!c) return NULL;
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
            if (!zan_mem_new_slab(c)) return NULL;
        }
    }
#if defined(_WIN32)
    if (c->bump_decommitted) {
        uintptr_t start = ((uintptr_t)c->bump + 4095) & ~(uintptr_t)4095;
        uintptr_t end = (uintptr_t)c->bump_end & ~(uintptr_t)4095;
        if (end > start) {
            VirtualAlloc((void *)start, end - start, MEM_COMMIT, PAGE_READWRITE);
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
        if (!rc->bump || !rc->bump_end || rc->bump_decommitted) continue;
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

#if defined(_WIN32)
    DWORD fls = __atomic_load_n(&g_fls, __ATOMIC_ACQUIRE);
    zan_mem_cache *self = (fls == FLS_OUT_OF_INDEXES)
                          ? NULL : (zan_mem_cache *)FlsGetValue(fls);
#else
    zan_mem_cache *self = t_cache;
#endif
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
    if (n == 0) n = 1;
    if (n <= ZAN_MEM_MAX_SMALL) {
        void *p = zan_mem_small(n);
        if (p) return p;
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
    if (magic != ZAN_MEM_MAGIC) return -1;
    uint32_t c = __atomic_load_n(&h->cls, __ATOMIC_ACQUIRE);
    if (c >= (uint32_t)ZAN_MEM_NCLASS) {
        char msg[80];
        snprintf(msg, sizeof msg, "corrupt block header at %p (class %u)",
                 p, (unsigned)c);
        zan_rt_fatal("mem", msg);
    }
    *cls = c;
    return 0;
}

void __wrap_free(void *p) {
    if (!p) return;
    if (!zan_mem_owns(p)) { __real_free(p); return; }
    uint32_t cls;
    if (zan_mem_hdr_check(p, &cls) != 0) return;
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
#if defined(_WIN32)
    DWORD fls = __atomic_load_n(&g_fls, __ATOMIC_ACQUIRE);
    zan_mem_cache *self = (fls == FLS_OUT_OF_INDEXES)
                          ? NULL : (zan_mem_cache *)FlsGetValue(fls);
#else
    zan_mem_cache *self = t_cache;
#endif
    if (owner == self && self) {
        *(void **)p = self->free_list[cls];
        self->free_list[cls] = p;
        return;
    }
    if (!owner) return;
    unsigned s;
    if (self)
        s = (unsigned)(((uintptr_t)self >> 4) & (ZAN_MEM_REMOTE_STRIPES - 1));
    else
        s = (unsigned)(__atomic_fetch_add(&g_remote_rr, 1, __ATOMIC_RELAXED)
                       & (ZAN_MEM_REMOTE_STRIPES - 1));
    void **slot = &owner->remote[s].head;
    void *head = __atomic_load_n(slot, __ATOMIC_RELAXED);
    do {
        *(void **)p = head;
    } while (!__atomic_compare_exchange_n(slot, &head, p, 1,
                                          __ATOMIC_RELEASE, __ATOMIC_RELAXED));

    if (self && self != owner) {
        zan_mem_drain_remote(self);
    }
}

void *__wrap_calloc(size_t n, size_t m) {
    size_t total = n * m;
    if (n != 0 && total / n != m) return NULL;      /* overflow */
    if (total <= ZAN_MEM_MAX_SMALL) {
        void *p = zan_mem_small(total ? total : 1);
        if (p) { memset(p, 0, total); return p; }
    }
    return __real_calloc(n, m);
}

void *__wrap_realloc(void *p, size_t n) {
    if (!p) return __wrap_malloc(n);
    if (n == 0) { __wrap_free(p); return NULL; }
    if (!zan_mem_owns(p)) return __real_realloc(p, n);
    uint32_t cls;
    if (zan_mem_hdr_check(p, &cls) != 0) return NULL;
    size_t old = k_class_size[cls];
    if (n <= old) return p;
    void *np = __wrap_malloc(n);
    if (!np) return NULL;
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
#define ZAN_DEFER_LOCK_W()   AcquireSRWLockExclusive(&g_defer_lock)
#define ZAN_DEFER_UNLOCK()   ReleaseSRWLockExclusive(&g_defer_lock)
#else
#include <pthread.h>
static pthread_mutex_t g_defer_lock = PTHREAD_MUTEX_INITIALIZER;
#define ZAN_DEFER_LOCK_W()   pthread_mutex_lock(&g_defer_lock)
#define ZAN_DEFER_UNLOCK()   pthread_mutex_unlock(&g_defer_lock)
#endif

static zan_defer_node *g_defer_head;
static int g_defer_count;
static int g_defer_exit_hooked;

void zan_rt_defer_drain(void);

static void zan_rt_defer_at_exit(void) {
    zan_rt_defer_drain();
}

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
