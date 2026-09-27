/* arena.c -- Bump allocator implementation. */

#include "arena.h"
#include <windows.h>
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

#include <stdio.h>

static size_t g_arena_total_requested = 0;
static size_t g_arena_alloc_count = 0;
static size_t g_bucket_counts[16] = {0};
static size_t g_bucket_bytes[16] = {0};

typedef struct { size_t sz; size_t count; size_t bytes; } size_stat_t;
static size_stat_t g_top_sizes[128];
static int g_top_size_count = 0;

void *zan_arena_alloc(zan_arena_t *arena, size_t size) {
    /* align to 8 bytes */
    size = (size + 7) & ~(size_t)7;

    g_arena_total_requested += size;
    g_arena_alloc_count++;
    int b = 0;
    size_t s = size;
    while (s > 16 && b < 15) { s >>= 1; b++; }
    g_bucket_counts[b]++;
    g_bucket_bytes[b] += size;

    int found = 0;
    for (int i = 0; i < g_top_size_count; i++) {
        if (g_top_sizes[i].sz == size) {
            g_top_sizes[i].count++;
            g_top_sizes[i].bytes += size;
            found = 1;
            break;
        }
    }
    if (!found && g_top_size_count < 128) {
        g_top_sizes[g_top_size_count].sz = size;
        g_top_sizes[g_top_size_count].count = 1;
        g_top_sizes[g_top_size_count].bytes = size;
        g_top_size_count++;
    }

    if (arena->used + size > arena->cap) {
        /* allocate new block. An oversized request (bigger than the 1 MB
         * standard block) gets a block of EXACTLY its own size: sizing it
         * 2x left half of the block permanently unused whenever the next
         * allocation did not fit the slack, so a stream of similarly sized
         * large objects wasted ~50% of arena memory (and `size * 2` could
         * overflow for absurd sizes). The exact-size block retires to the
         * prev chain fully used; the next allocation opens a fresh standard
         * block. `size` is already 8-aligned here. */
        size_t new_cap = ZAN_ARENA_BLOCK_SIZE;
        if (size > new_cap) new_cap = size;
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

size_t zan_arena_total_bytes(const zan_arena_t *arena) {
    size_t total = 0;
    while (arena) {
        total += arena->cap;
        arena = arena->prev;
    }
    return total;
}

void zan_arena_dump_stats(void) {
    fprintf(stderr, "=== Arena Stats: %zu allocs, %zu MB requested ===\n",
            g_arena_alloc_count, g_arena_total_requested / (1024 * 1024));
    /* Sort top sizes by total bytes descending */
    for (int i = 0; i < g_top_size_count - 1; i++) {
        for (int j = i + 1; j < g_top_size_count; j++) {
            if (g_top_sizes[j].bytes > g_top_sizes[i].bytes) {
                size_stat_t tmp = g_top_sizes[i];
                g_top_sizes[i] = g_top_sizes[j];
                g_top_sizes[j] = tmp;
            }
        }
    }
    fprintf(stderr, "--- Top Alloc Sizes by Total Bytes ---\n");
    for (int i = 0; i < g_top_size_count && i < 15; i++) {
        fprintf(stderr, "  size %6zu B: %8zu allocs (%6zu MB)\n",
                g_top_sizes[i].sz, g_top_sizes[i].count, g_top_sizes[i].bytes / (1024 * 1024));
    }
}
