#pragma once

#include <stdio.h>
#include <stdlib.h>

static inline void zan_host_oom(void) {
    fprintf(stderr, "error: out of memory\n");
    abort();
}

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

#define malloc zan_host_malloc
#define calloc zan_host_calloc
#define realloc zan_host_realloc
