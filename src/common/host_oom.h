#pragma once

#include <stdio.h>
#include <stdlib.h>

/* OOM policy. TUs that carry rt_timer.h define ZAN_OOM_TO_RUNTIME (and are
 * always linked with zan_rt_fatal's definition), so their allocation failures
 * join the zan_rt_fatal funnel: one diagnostic line on stderr, then the
 * embedder's zan_rt_set_fatal_handler takeover, then abort() -- the same path
 * as every other unrecoverable runtime fault. Tool binaries (zanc, zan-lsp,
 * zan-dap) and archives built without rt_timer.h keep the historical direct
 * abort; no link dependency either way. */
#if defined(ZAN_OOM_TO_RUNTIME)
static inline void zan_host_oom(void) {
    /* zan_rt_fatal never returns. rt_timer.h, which defines the macro, has
     * declared it by the time this header is included. */
    zan_rt_fatal("oom", "host allocation failed");
}
#else
static inline void zan_host_oom(void) {
    fprintf(stderr, "error: out of memory\n");
    abort();
}
#endif

/* ---- Test-only allocation-failure injection ----
 *
 * Production policy (below, in the #else) is to abort on OOM, so callers never
 * observe a NULL return and the OOM-recovery paths in json.c / rpc.c / etc. are
 * unreachable in real builds. To unit-test those paths, compile the TU under
 * test with -DZAN_ALLOC_INJECT: the abort policy is replaced by NULL-return,
 * and once zan_alloc_fail_at is reached the Nth non-zero-size allocation
 * (1-based) is forced to fail. The test driver owns the two counters and resets
 * them around each probe. ZAN_ALLOC_INJECT must never be defined for a real
 * (non-test) target. */
#ifdef ZAN_ALLOC_INJECT
extern int zan_alloc_fail_at;   /* 0 = disabled; >0 = fail at this count */
extern int zan_alloc_counter;   /* running; a test resets it to 0 per probe */
static inline int zan_alloc_tick_fail(void) {
    if (zan_alloc_fail_at > 0 && ++zan_alloc_counter >= zan_alloc_fail_at)
        return 1;
    return 0;
}
static inline void *zan_host_malloc(size_t size) {
    if (size != 0 && zan_alloc_tick_fail()) return NULL;
    return (malloc)(size);
}
static inline void *zan_host_calloc(size_t count, size_t size) {
    if (size != 0 && count != 0 && zan_alloc_tick_fail()) return NULL;
    if (size != 0 && count > (size_t)-1 / size) return NULL;
    return (calloc)(count, size);
}
static inline void *zan_host_realloc(void *ptr, size_t size) {
    if (size != 0 && zan_alloc_tick_fail()) return NULL;
    return (realloc)(ptr, size);
}
#else
static inline void *zan_host_malloc(size_t size) {
    void *p = (malloc)(size);
    if (!p && size != 0) zan_host_oom();
    return p;
}
static inline void *zan_host_calloc(size_t count, size_t size) {
    void *p;
    if (size != 0 && count > (size_t)-1 / size) zan_host_oom();
    p = (calloc)(count, size);
    if (!p && count != 0 && size != 0) zan_host_oom();
    return p;
}
static inline void *zan_host_realloc(void *ptr, size_t size) {
    void *p = (realloc)(ptr, size);
    if (!p && size != 0) zan_host_oom();
    return p;
}
#endif

#define malloc zan_host_malloc
#define calloc zan_host_calloc
#define realloc zan_host_realloc
