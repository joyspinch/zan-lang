/* rt_mem.c -- small-object allocator for produced Zan programs.
 *
 * ARC programs allocate constantly: every string, list node, object and
 * coroutine frame is a separate heap block with a very short life. musl's
 * mallocng (the allocator behind the bundled static sysroot) spends a large
 * share of a request in get_meta / alloc_slot / nontrivial_free for exactly
 * that pattern -- an HTTP benchmark showed ~23% of user CPU inside the
 * allocator.
 *
 * This object front-ends malloc/free/calloc/realloc with a size-class cache:
 *   - blocks up to ZAN_MEM_MAX_SMALL come from per-thread free lists carved
 *     out of 1 MiB slabs, so an allocation pops a pointer and a free pushes
 *     one -- no metadata search, no coalescing, no locks;
 *   - anything larger falls through to the libc allocator.
 *
 * It is linked with `ld --wrap=malloc,free,calloc,realloc`, so it also sees
 * allocations made inside libc: a pointer that did not come from one of our
 * slabs is handed straight back to the real allocator, decided by an
 * address-range test (never by reading memory we do not own).
 *
 * Slabs are never unmapped: freed blocks stay in their class list and are
 * reused, which is what a long-running server wants.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/zan_abi.h"

#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
#include <sys/mman.h>
#include <unistd.h>
#define ZAN_MEM_HAVE_MMAP 1
#endif

/* The real allocator, behind the linker wrap. */
void *__real_malloc(size_t n);
void  __real_free(void *p);
void *__real_calloc(size_t n, size_t m);
void *__real_realloc(void *p, size_t n);

#define ZAN_MEM_MAX_SMALL   2048u
#define ZAN_MEM_SLAB        (1u << 20)      /* 1 MiB */

/* 16-byte header keeps payloads 16-byte aligned; a freed block stores its
 * free-list link in the payload itself (every class is >= 16 bytes). The
 * compiler emits the same-size object header (refcount/site at obj-16/-8,
 * see ZAN_OBJ_HDR_SIZE in ../common/zan_abi.h) in front of every class
 * instance, so the allocator header must stay 16 bytes for payloads to keep
 * that object header 16-byte aligned. */
typedef struct {
    uint32_t magic;
    uint32_t cls;
    uint64_t pad;
} zan_mem_hdr_t;

_Static_assert(sizeof(zan_mem_hdr_t) == ZAN_OBJ_HDR_SIZE,
               "allocator header must keep payloads 16-byte aligned "
               "for the compiler's object header (zan_abi.h)");

#define ZAN_MEM_MAGIC 0x5A414E4DU     /* "ZANM" */
#define ZAN_MEM_FREED 0x5A414E46U     /* "ZANF": block currently on a free list */
#define ZAN_MEM_HDR   ((size_t)sizeof(zan_mem_hdr_t))

/* Payload sizes, all multiples of 16. */
static const uint16_t k_class_size[] = {
      16,   32,   48,   64,   80,   96,  112,  128,
     160,  192,  224,  256,  320,  384,  448,  512,
     640,  768,  896, 1024, 1280, 1536, 1792, 2048
};
#define ZAN_MEM_NCLASS ((int)(sizeof(k_class_size) / sizeof(k_class_size[0])))

/* size -> class table, filled on first use. */
static unsigned char g_size_class[ZAN_MEM_MAX_SMALL + 1];
static int g_class_ready;

/* Slab bases live in a lock-free open-addressing set: slabs are 1 MiB
 * aligned, so free() masks the pointer down to its slab base and looks it up.
 * Entries are written once (under the slab lock) and read atomically, so a
 * concurrent lookup never sees a half-published table. */
#define ZAN_MEM_SET_SIZE  (1u << 14)
#define ZAN_MEM_SET_MASK  (ZAN_MEM_SET_SIZE - 1u)
static uintptr_t g_slab_set[ZAN_MEM_SET_SIZE];

/* Per-thread caches: no locking on the hot path. A block freed by a thread
 * that did not allocate it migrates to the freeing thread's list -- slabs are
 * process-wide and never returned to the OS, so that is safe.
 *
 * On MinGW (Windows) __thread is emulated by libgcc's emutls, whose first
 * access runs pthread_once(emutls_init) -- and emutls_init calls malloc. A
 * malloc reached from inside __wrap_malloc re-enters __emutls_get_address on
 * the same not-yet-initialized variable and spins forever on the once lock
 * (a real deadlock, observed with TDM-GCC 10.3 at CRT startup). The allocator
 * is only ever linked on POSIX today (mmap slabs; Windows has no mmap path),
 * so guard the hazard: on Windows use plain statics, which are safe because
 * the slab path -- the only user of these caches -- is disabled there. */
#if defined(_WIN32)
static void *t_free_list[ZAN_MEM_NCLASS];
static char *t_bump;
static char *t_bump_end;
#else
static __thread void *t_free_list[ZAN_MEM_NCLASS];
static __thread char *t_bump;
static __thread char *t_bump_end;
#endif

static volatile int g_slab_lock;

static void zan_mem_lock(void) {
    while (__sync_lock_test_and_set(&g_slab_lock, 1)) {
        while (g_slab_lock) {
#if defined(__i386__) || defined(__x86_64__)
            __builtin_ia32_pause();
#endif
        }
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

/* Build the size-class table once. g_class_ready is a plain int in the
 * original code, and zan_mem_small can be reached from any thread the
 * scheduler runs (coroutine workers and Thread.Start bodies alike), so two
 * threads could race on it. The table itself is deterministic, but the
 * unsynchronized read/write is a C data race; guard it with the slab lock and
 * an atomic flag. */
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

/* True when `p` points into one of our slabs. */
static int zan_mem_owns(const void *p) {
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

/* Grab a fresh 1 MiB-aligned slab for this thread's bump region. Returns 0
 * when it cannot, and callers then fall back to the libc allocator. */
static int zan_mem_new_slab(void) {
#ifndef ZAN_MEM_HAVE_MMAP
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

    zan_mem_lock();
    unsigned i = zan_mem_hash(base);
    unsigned n = 0;
    while (n < 64 && g_slab_set[(i + n) & ZAN_MEM_SET_MASK] != 0) n++;
    if (n == 64) {                       /* set full: stay out of our world */
        zan_mem_unlock();
        munmap((void *)base, ZAN_MEM_SLAB);
        return 0;
    }
    __atomic_store_n(&g_slab_set[(i + n) & ZAN_MEM_SET_MASK], base,
                     __ATOMIC_RELEASE);
    zan_mem_unlock();

    t_bump = (char *)base;
    t_bump_end = (char *)base + ZAN_MEM_SLAB;
    return 1;
#endif
}

static void *zan_mem_small(size_t n) {
    zan_mem_ensure_classes();
    int cls = g_size_class[n];
    void *p = t_free_list[cls];
    if (p) {
        t_free_list[cls] = *(void **)p;
        /* A popped block is still marked FREED from its last free: reset the
         * header so the next free of this live block is not mistaken for a
         * double free (the guard keys on the FREED marker). */
        zan_mem_hdr_t *h = (zan_mem_hdr_t *)((char *)p - ZAN_MEM_HDR);
        h->magic = ZAN_MEM_MAGIC;
        h->cls = (uint32_t)cls;
        return p;
    }
    size_t need = (size_t)k_class_size[cls] + ZAN_MEM_HDR;
    if ((size_t)(t_bump_end - t_bump) < need) {
        if (!zan_mem_new_slab()) return NULL;
    }
    zan_mem_hdr_t *h = (zan_mem_hdr_t *)t_bump;
    t_bump += need;
    h->magic = ZAN_MEM_MAGIC;
    h->cls = (uint32_t)cls;
    return (char *)h + ZAN_MEM_HDR;
}

void *__wrap_malloc(size_t n) {
    if (n == 0) n = 1;
    if (n <= ZAN_MEM_MAX_SMALL) {
        void *p = zan_mem_small(n);
        if (p) return p;
    }
    return __real_malloc(n);
}

/* Validate the allocator header in front of a slab block. Returns 0 with
 * *cls set when the block is a live block start; -1 for anything the
 * allocator does not own (not a block start: callers leave it alone); and
 * aborts on a block that was already freed or whose header is garbage --
 * both are aliasing bugs under ARC, and the abort is the detection.
 * __wrap_free and __wrap_realloc share this so their behavior can never
 * drift apart. */
static int zan_mem_hdr_check(const void *p, uint32_t *cls) {
    zan_mem_hdr_t *h = (zan_mem_hdr_t *)((const char *)p - ZAN_MEM_HDR);
    /* The allocator header at p-16 is only ever written by this allocator:
     * the compiler's object header (refcount/site) lives at p..p+15, so these
     * bytes survive for the block's whole lifetime. A freed block is marked
     * FREED until it is re-allocated, which turns a second free of the same
     * block -- a memory-aliasing bug under ARC -- into an immediate abort
     * instead of two allocations returning the same address. */
    if (h->magic == ZAN_MEM_FREED) {
        fprintf(stderr, "zan runtime: double free of block %p\n", (void *)p);
        abort();
    }
    if (h->magic != ZAN_MEM_MAGIC) return -1;   /* not a block start: ignore */
    uint32_t c = h->cls;
    if (c >= (uint32_t)ZAN_MEM_NCLASS) {   /* header garbage: refuse to trust it */
        fprintf(stderr, "zan runtime: corrupt block header at %p (class %u)\n",
                (void *)p, (unsigned)c);
        abort();
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
    h->magic = ZAN_MEM_FREED;
    *(void **)p = t_free_list[cls];
    t_free_list[cls] = p;
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
    /* realloc(p, 0) must free p; the standard leaves only the return value
     * implementation-defined (NULL or a unique 0-size block). Free and
     * report NULL rather than returning p unchanged, which would both leak
     * nothing but also pretend the block still exists. */
    if (n == 0) { __wrap_free(p); return NULL; }
    if (!zan_mem_owns(p)) return __real_realloc(p, n);
    /* Same header checks as free: reallocating a block that is on a free
     * list (use-after-free) or whose header is garbage must abort, not hand
     * the caller a block that some other owner may already hold. */
    uint32_t cls;
    if (zan_mem_hdr_check(p, &cls) != 0) return p;   /* not a block start: leave it alone */
    size_t old = k_class_size[cls];
    if (n <= old) return p;
    void *np = __wrap_malloc(n);
    if (!np) return NULL;
    memcpy(np, p, old);
    __wrap_free(p);
    return np;
}
