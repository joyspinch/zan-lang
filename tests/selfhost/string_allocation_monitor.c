/* Transparent allocation oracle for selfhost generated modules. The harness
 * redirects only that module's allocator symbols; CRT/runtime allocations are
 * untouched. No failure injection, ownership anchors or payload inspection. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { void *pointer; size_t size; } Allocation;
static Allocation allocations[65536];
static uint64_t allocated, freed, live_bytes, live_blocks, bad_frees;
static uint64_t start_allocated, start_freed, start_bytes, start_blocks, start_bad;

static void remember(void *pointer, size_t size) {
    if (!pointer) abort();
    for (size_t i = 0; i < 65536; ++i) {
        if (!allocations[i].pointer) {
            allocations[i].pointer = pointer;
            allocations[i].size = size;
            allocated++; live_blocks++; live_bytes += size;
            return;
        }
    }
    abort();
}
void *string_probe_malloc(size_t size) {
    void *pointer = malloc(size);
    if (pointer) remember(pointer, size);
    return pointer;
}
void *string_probe_calloc(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) {
        errno = ENOMEM;
        return NULL;
    }
    void *pointer = calloc(count, size);
    if (pointer) remember(pointer, count * size);
    return pointer;
}
void string_probe_free(void *pointer) {
    if (!pointer) return;
    for (size_t i = 0; i < 65536; ++i) {
        if (allocations[i].pointer == pointer) {
            live_bytes -= allocations[i].size;
            allocations[i].pointer = NULL;
            freed++; live_blocks--;
            free(pointer);
            return;
        }
    }
    /* Reject invalid/double frees instead of hiding them behind the CRT. */
    bad_frees++;
}
void *string_probe_realloc(void *pointer, size_t size) {
    if (!pointer) {
        void *result = realloc(NULL, size);
        if (result) remember(result, size);
        return result;
    }
    for (size_t i = 0; i < 65536; ++i) {
        if (allocations[i].pointer == pointer) {
            size_t old_size = allocations[i].size;
            void *result = realloc(pointer, size);
            if (!result) {
                if (!size) {
                    /* The Windows CRT frees a nonnull block on realloc(p, 0). */
                    live_bytes -= old_size;
                    allocations[i].pointer = NULL;
                    allocations[i].size = 0;
                    freed++; live_blocks--;
                }
                /* Nonzero-size failure preserves the original live record. */
                return NULL;
            }
            /* Count a successful resize as replacing one allocation, including
             * an in-place resize. The number of live blocks does not change. */
            live_bytes -= old_size;
            live_bytes += size;
            allocations[i].pointer = result;
            allocations[i].size = size;
            allocated++; freed++;
            return result;
        }
    }
    bad_frees++;
    abort();
}
void string_probe_begin(void) {
    start_allocated = allocated; start_freed = freed;
    start_bytes = live_bytes; start_blocks = live_blocks; start_bad = bad_frees;
}
int64_t string_probe_end(void) {
    int64_t blocks = (int64_t)live_blocks - (int64_t)start_blocks;
    int64_t bytes = (int64_t)live_bytes - (int64_t)start_bytes;
    printf("balance alloc=%llu free=%llu blocks=%lld bytes=%lld bad=%llu\n",
        (unsigned long long)(allocated - start_allocated),
        (unsigned long long)(freed - start_freed),
        (long long)blocks, (long long)bytes,
        (unsigned long long)(bad_frees - start_bad));
    return blocks == 0 && bytes == 0 && bad_frees == start_bad;
}
