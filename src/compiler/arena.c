/* arena.c -- Bump allocator implementation. */

#include "arena.h"
#include <stdlib.h>
#include <string.h>

#include "../common/host_oom.h"
zan_arena_t *zan_arena_new(void) {
    zan_arena_t *a = (zan_arena_t *)malloc(sizeof(zan_arena_t));
    if (!a) return NULL;
    a->cap = ZAN_ARENA_BLOCK_SIZE;
    a->base = (char *)malloc(a->cap);
    if (!a->base) { free(a); return NULL; }
    a->used = 0;
    a->prev = NULL;
    return a;
}

void zan_arena_free(zan_arena_t *arena) {
    while (arena) {
        zan_arena_t *prev = arena->prev;
        free(arena->base);
        free(arena);
        arena = prev;
    }
}

void *zan_arena_alloc(zan_arena_t *arena, size_t size) {
    /* align to 8 bytes */
    size = (size + 7) & ~(size_t)7;

    if (arena->used + size > arena->cap) {
        /* allocate new block */
        size_t new_cap = ZAN_ARENA_BLOCK_SIZE;
        if (size > new_cap) new_cap = size * 2;
        zan_arena_t *block = (zan_arena_t *)malloc(sizeof(zan_arena_t));
        if (!block) return NULL;
        block->base = (char *)malloc(new_cap);
        if (!block->base) { free(block); return NULL; }
        block->cap = new_cap;
        block->used = 0;
        block->prev = arena->prev;
        /* swap: new block becomes current, old block goes to prev chain */
        char *old_base = arena->base;
        size_t old_used = arena->used;
        size_t old_cap = arena->cap;
        arena->base = block->base;
        arena->used = 0;
        arena->cap = new_cap;
        block->base = old_base;
        block->used = old_used;
        block->cap = old_cap;
        arena->prev = block;
    }

    void *ptr = arena->base + arena->used;
    arena->used += size;
    memset(ptr, 0, size);
    return ptr;
}

char *zan_arena_strdup(zan_arena_t *arena, const char *str, size_t len) {
    char *dup = (char *)zan_arena_alloc(arena, len + 1);
    if (!dup) return NULL;
    memcpy(dup, str, len);
    dup[len] = '\0';
    return dup;
}
