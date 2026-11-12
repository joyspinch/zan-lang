/* Link with the real rt_mem object and target libc, wrapping malloc, free,
 * calloc and __malloc_allzerop. Calling libc calloc directly reproduces musl's
 * static-link path: wrapped malloc followed by its internal all-zero probe. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__linux__) && !defined(__GLIBC__)
extern void *__real_calloc(size_t count, size_t size);
extern void *__wrap_malloc(size_t size);
extern void __wrap_free(void *ptr);

/* Keep the libc call opaque even under optimization; do not substitute Zan's
 * __wrap_calloc or let a malloc/memset transformation hide the libc path. */
static void *(*volatile libc_calloc)(size_t, size_t) = __real_calloc;

static int check_calloc(size_t count, size_t size, int round) {
    size_t bytes = count * size;
    unsigned char *dirty = __wrap_malloc(bytes);
    if (!dirty) {
        fprintf(stderr, "FAIL: dirty malloc round %d size %zu\n", round, bytes);
        return 1;
    }
    memset(dirty, 0xa5, bytes);
    uintptr_t recycled = (uintptr_t)dirty;
    __wrap_free(dirty);

    unsigned char *ptr = libc_calloc(count, size);
    if (!ptr) {
        fprintf(stderr, "FAIL: libc calloc round %d size %zu\n", round, bytes);
        return 1;
    }
    /* Small blocks must return through Zan's freelist. Native large blocks
     * need not reuse the same address, but must retain libc zeroing semantics. */
    if (bytes <= 2048 && (uintptr_t)ptr != recycled) {
        fprintf(stderr, "FAIL: libc calloc bypassed recycled Zan block "
                        "round %d size %zu\n", round, bytes);
        __wrap_free(ptr);
        return 1;
    }
    for (size_t i = 0; i < bytes; i++) {
        if (ptr[i] != 0) {
            fprintf(stderr, "FAIL: libc calloc round %d size %zu byte %zu "
                            "is 0x%02x\n", round, bytes, i, (unsigned)ptr[i]);
            __wrap_free(ptr);
            return 1;
        }
    }
    /* Dirty the calloc result too: subsequent rounds exercise ownership and
     * zeroing after freeing both malloc and calloc results via the wrapper. */
    memset(ptr, 0x5a, bytes);
    __wrap_free(ptr);
    return 0;
}

int main(void) {
    static const size_t cases[][2] = {
        {1, 16}, {3, 11}, {4, 32}, {7, 73}, {32, 32}, {8, 256},
        {128, 512} /* 65536 bytes: native allocator, above Zan's small limit. */
    };
    for (int round = 0; round < 32; round++) {
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            if (check_calloc(cases[i][0], cases[i][1], round)) return 1;
        }
        void *overflow = libc_calloc(SIZE_MAX, 2);
        if (overflow) {
            fprintf(stderr, "FAIL: libc calloc accepted SIZE_MAX * 2 "
                            "round %d\n", round);
            __wrap_free(overflow);
            return 1;
        }
    }
    puts("PASS rt_mem_musl_calloc_test");
    return 0;
}
#else
int main(void) {
    fprintf(stderr, "SKIP: rt_mem_musl_calloc_test requires Linux target libc\n");
    return 77;
}
#endif
