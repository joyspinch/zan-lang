#pragma once

#include <stdio.h>
#include <stdlib.h>

/* 核心系统底层抽象与内存语义契约 */
#if defined(ZAN_OOM_TO_RUNTIME)
static inline void zan_host_oom(void) {
    /* 核心系统底层抽象与内存语义契约 */
    zan_rt_fatal("oom", "host allocation failed");
}
#else
static inline void zan_host_oom(void) {
    fprintf(stderr, "error: out of memory\n");
    abort();
}
#endif

/* 内部辅助逻辑 */
#ifdef ZAN_ALLOC_INJECT
extern int zan_alloc_fail_at;   /* 核心系统底层抽象与内存语义契约 */
extern int zan_alloc_counter;   /* 核心系统底层抽象与内存语义契约 */
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
