/* arena.h: 编译器专用块分配器（单向增长，编译结束一次性释放） */

#ifndef ZAN_ARENA_H
#define ZAN_ARENA_H

#include "zan.h"

#define ZAN_ARENA_BLOCK_SIZE (1024 * 1024) /* 1 MB blocks */

struct zan_arena {
    char *base;     /* 核心系统底层抽象与内存语义契约 */
    size_t used;    /* 核心系统底层抽象与内存语义契约 */
    size_t cap;     /* 核心系统底层抽象与内存语义契约 */
    struct zan_arena *prev; /* 核心系统底层抽象与内存语义契约 */
};

zan_arena_t *zan_arena_new(void);
void zan_arena_free(zan_arena_t *arena);
void *zan_arena_alloc(zan_arena_t *arena, size_t size);
char *zan_arena_strdup(zan_arena_t *arena, const char *str, size_t len);
size_t zan_arena_total_bytes(const zan_arena_t *arena);
void zan_arena_dump_stats(void);

#endif /* ZAN_ARENA_H */
