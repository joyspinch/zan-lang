/* arena.h -- Bump allocator for compiler data structures.
 *
 * All AST nodes, types, and symbols are allocated from arenas.
 * At the end of compilation the entire arena is freed in one call.
 */

#ifndef ZAN_ARENA_H
#define ZAN_ARENA_H

#include "zan.h"

#define ZAN_ARENA_BLOCK_SIZE (1024 * 1024) /* 1 MB blocks */

struct zan_arena {
    char *base;     /* start of current block */
    size_t used;    /* bytes used in current block */
    size_t cap;     /* capacity of current block */
    struct zan_arena *prev; /* linked list of previous blocks */
};

zan_arena_t *zan_arena_new(void);
void zan_arena_free(zan_arena_t *arena);
void *zan_arena_alloc(zan_arena_t *arena, size_t size);
char *zan_arena_strdup(zan_arena_t *arena, const char *str, size_t len);

#endif /* ZAN_ARENA_H */
