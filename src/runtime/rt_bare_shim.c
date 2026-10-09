/* rt_bare_shim */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#if defined(__has_include)
#if __has_include(<stdio.h>)
#include <stdio.h>
#define ZAN_SHIM_HAVE_STDIO 1
#endif
#endif
#ifndef ZAN_SHIM_HAVE_STDIO
/* headerless rv32 toolchain (no sysroot): declare just what we call */
typedef __builtin_va_list zan_va_list;
int vsnprintf(char *s, unsigned long n, const char *fmt, zan_va_list ap);
#define va_list zan_va_list
#endif

/* Stub symbols are weak so a board bring-up (QEMU kit, Arduino core, */
#if defined(__ELF__)
#define ZAN_SHIM_WEAK __attribute__((weak))
#else
#define ZAN_SHIM_WEAK
#endif

/* 内部辅助逻辑 */

#ifndef ZAN_BARE_HEAP_BYTES
#define ZAN_BARE_HEAP_BYTES (64 * 1024)
#endif

union zan_shim_block {
    struct {
        size_t size;             /* payload bytes, free blocks only */
        size_t magic;            /* set at malloc, checked at free */
        union zan_shim_block *next;
    } hdr;
    /* guarantees the payload is 8-byte aligned and at least word sized */
    long long align_min;
    unsigned char align_bytes[16];
};
/* 内部辅助逻辑 */
#define ZAN_SHIM_MAGIC ((size_t)0x5A4E4245u)   /* "ZNBE" */

static unsigned char zan_shim_pool[ZAN_BARE_HEAP_BYTES]
    __attribute__((aligned(8)));
static union zan_shim_block *zan_shim_free = NULL;
static int zan_shim_pool_ready;
/* 内部辅助逻辑 */
static size_t zan_shim_live_bytes;
static size_t zan_shim_live_peak;
static unsigned zan_shim_oom;
static size_t zan_shim_oom_size;   /* request size of the latest OOM */
static unsigned zan_shim_frees;
static unsigned zan_shim_allocs;
static unsigned zan_shim_bad_frees;  /* wild/doubled pointers refused */

/* 内部辅助逻辑 */
static union zan_shim_block *zan_shim_checked_header(void *p) {
    if ((uintptr_t)p < (uintptr_t)zan_shim_pool + sizeof(union zan_shim_block) ||
        (uintptr_t)p >= (uintptr_t)zan_shim_pool + sizeof(zan_shim_pool))
        return NULL;
    union zan_shim_block *b =
        (union zan_shim_block *)((unsigned char *)p - sizeof(union zan_shim_block));
    if (b->hdr.magic != ZAN_SHIM_MAGIC) return NULL;
    return b;
}
/* 内部辅助逻辑 */
#define ZAN_SHIM_TRACE 16
static size_t zan_shim_alloc_trace[ZAN_SHIM_TRACE];
static size_t zan_shim_free_trace[ZAN_SHIM_TRACE];
static unsigned zan_shim_alloc_trace_n;
static unsigned zan_shim_free_trace_n;

void zan_shim_pool_stats(size_t *live, size_t *peak, unsigned *oom) {
    if (live) *live = zan_shim_live_bytes;
    if (peak) *peak = zan_shim_live_peak;
    if (oom) *oom = zan_shim_oom;
}

size_t zan_shim_oom_request(void) { return zan_shim_oom_size; }
unsigned zan_shim_free_count(void) { return zan_shim_frees; }
unsigned zan_shim_alloc_count(void) { return zan_shim_allocs; }
unsigned zan_shim_bad_free_count(void) { return zan_shim_bad_frees; }

void zan_shim_trace(const size_t **allocs, const size_t **frees,
    unsigned *an, unsigned *fn) {
    *allocs = zan_shim_alloc_trace;
    *frees = zan_shim_free_trace;
    *an = zan_shim_alloc_trace_n;
    *fn = zan_shim_free_trace_n;
}

/* 内部辅助逻辑 */
#define ZAN_SHIM_LIVE_N 64
static struct { void *p; size_t n; } zan_shim_live[ZAN_SHIM_LIVE_N];
static unsigned zan_shim_live_over;   /* live table overflows stop recording */

static void zan_shim_live_add(void *p, size_t n) {
    if (zan_shim_live_over) return;
    for (unsigned i = 0; i < ZAN_SHIM_LIVE_N; i++) {
        if (!zan_shim_live[i].p) { zan_shim_live[i].p = p; zan_shim_live[i].n = n; return; }
    }
    zan_shim_live_over = 1;
}
static void zan_shim_live_del(void *p) {
    for (unsigned i = 0; i < ZAN_SHIM_LIVE_N; i++) {
        if (zan_shim_live[i].p == p) { zan_shim_live[i].p = 0; return; }
    }
}
/* top-8 live sizes, descending */
unsigned zan_shim_live_top(size_t *out, unsigned max) {
    unsigned found = 0;
    for (unsigned i = 0; i < ZAN_SHIM_LIVE_N && found < max; i++) {
        if (!zan_shim_live[i].p) continue;
        size_t n = zan_shim_live[i].n;
        unsigned j = found;
        while (j > 0 && out[j - 1] < n) { out[j] = out[j - 1]; j--; }
        out[j] = n;
        found++;
    }
    return found;
}

static void zan_shim_pool_init(void) {
    union zan_shim_block *b = (union zan_shim_block *)zan_shim_pool;
    size_t bytes = sizeof(zan_shim_pool);
    if (bytes < sizeof(*b)) return;
    b->hdr.size = bytes - sizeof(*b);
    b->hdr.next = NULL;
    zan_shim_free = b;
    zan_shim_pool_ready = 1;
}

void *malloc(size_t n) {
    if (!zan_shim_pool_ready) zan_shim_pool_init();
    if (n == 0) n = 1;
    /* 内部辅助逻辑 */
    if (n > sizeof(zan_shim_pool)) {
        zan_shim_oom++;
        zan_shim_oom_size = n;
        return NULL;
    }
    n = (n + 7u) & ~(size_t)7u;
    union zan_shim_block **prev = &zan_shim_free;
    for (union zan_shim_block *b = zan_shim_free; b; b = b->hdr.next) {
        if (b->hdr.size < n) { prev = &b->hdr.next; continue; }
        size_t rest = b->hdr.size - n;
        if (rest >= sizeof(*b)) {
            /* split: hand out the front, keep the tail on the free list */
            union zan_shim_block *tail =
                (union zan_shim_block *)((unsigned char *)b + sizeof(*b) + n);
            tail->hdr.size = rest - sizeof(*b);
            tail->hdr.next = b->hdr.next;
            *prev = tail;
        } else {
            *prev = b->hdr.next;
            /* 内部辅助逻辑 */
            n = b->hdr.size;
        }
        b->hdr.size = n;         /* free() reads this back */
        b->hdr.magic = ZAN_SHIM_MAGIC;
        zan_shim_allocs++;
        zan_shim_alloc_trace[zan_shim_alloc_trace_n++ % ZAN_SHIM_TRACE] = n;
        zan_shim_live_bytes += n;
        if (zan_shim_live_bytes > zan_shim_live_peak)
            zan_shim_live_peak = zan_shim_live_bytes;
        zan_shim_live_add((unsigned char *)b + sizeof(*b), n);
        return (unsigned char *)b + sizeof(*b);
    }
    zan_shim_oom++;
    zan_shim_oom_size = n;
    return NULL;
}

/* 内部辅助逻辑 */
void zan_shim_free_walk(size_t *total, size_t *maxhole, unsigned *holes) {
    size_t sum = 0, best = 0;
    unsigned count = 0;
    for (union zan_shim_block *b = zan_shim_free; b; b = b->hdr.next) {
        sum += b->hdr.size;
        if (b->hdr.size > best) best = b->hdr.size;
        count++;
    }
    if (total) *total = sum;
    if (maxhole) *maxhole = best;
    if (holes) *holes = count;
}

void free(void *p) {
    if (!p) return;
    union zan_shim_block *b = zan_shim_checked_header(p);
    if (!b) {
        /* wild or already-freed pointer: refuse rather than corrupt */
        zan_shim_bad_frees++;
        return;
    }
    b->hdr.magic = 0;        /* a second free of the same block fails above */
    zan_shim_frees++;
    zan_shim_free_trace[zan_shim_free_trace_n++ % ZAN_SHIM_TRACE] =
        b->hdr.size;
    zan_shim_live_del(p);
    if (b->hdr.size <= zan_shim_live_bytes)
        zan_shim_live_bytes -= b->hdr.size;
    /* Address-ordered insert + immediate coalescing */
    union zan_shim_block **link = &zan_shim_free;
    union zan_shim_block *pv = NULL;
    while (*link && (uintptr_t)*link < (uintptr_t)b) {
        pv = *link;
        link = &(*link)->hdr.next;
    }
    b->hdr.next = *link;
    *link = b;
    union zan_shim_block *nx = b->hdr.next;
    if (nx && (unsigned char *)b + sizeof(*b) + b->hdr.size ==
              (unsigned char *)nx) {
        b->hdr.size += sizeof(*b) + nx->hdr.size;
        b->hdr.next = nx->hdr.next;
    }
    if (pv && (unsigned char *)pv + sizeof(*b) + pv->hdr.size ==
              (unsigned char *)b) {
        pv->hdr.size += sizeof(*b) + b->hdr.size;
        pv->hdr.next = b->hdr.next;
    }
}

void *calloc(size_t count, size_t size) {
    if (count && size > (size_t)-1 / count) return NULL;   /* would wrap */
    size_t total = count * size;
    void *p = malloc(total);
    if (p) {
        unsigned char *q = (unsigned char *)p;
        for (size_t i = 0; i < total; i++) q[i] = 0;
    }
    return p;
}

void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    if (!n) { free(p); return NULL; }
    union zan_shim_block *b = zan_shim_checked_header(p);
    if (!b) {
        /* wild pointer: serve a fresh block, never read the garbage header */
        zan_shim_bad_frees++;
        return malloc(n);
    }
    size_t old = b->hdr.size;
    void *np = malloc(n);
    if (!np) return NULL;
    unsigned char *src = (unsigned char *)p;
    unsigned char *dst = (unsigned char *)np;
    for (size_t i = 0; i < (old < n ? old : n); i++) dst[i] = src[i];
    free(p);
    return np;
}

/* single-thread stubs */

ZAN_SHIM_WEAK int poll(void *fds, unsigned long nfds, int timeout) {
    (void)fds; (void)nfds; (void)timeout;
    return 0;                    /* nothing ready: pump falls to timers */
}

ZAN_SHIM_WEAK int pthread_mutex_lock(void *m) { (void)m; return 0; }
ZAN_SHIM_WEAK int pthread_mutex_unlock(void *m) { (void)m; return 0; }
ZAN_SHIM_WEAK unsigned long pthread_self(void) { return 1; }

ZAN_SHIM_WEAK char *getenv(const char *name) { (void)name; return NULL; }

/* snprintf ABI wrapper */

#ifdef ZAN_SHIM_HAVE_STDIO
int zan_w32_snprintf(char *s, long long n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(s, (size_t)n, fmt, ap);
    va_end(ap);
    return r;
}
#else
/* rv32ilp32: long long is a register pair either way; forward by ABI */
int zan_w32_snprintf(char *s, long long n, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int r = vsnprintf(s, (unsigned long)n, fmt, ap);
    __builtin_va_end(ap);
    return r;
}
#endif
