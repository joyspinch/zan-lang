/* Windows-only deterministic MEM_COMMIT failure injection. Build this TU
 * alone (it includes rt_mem.c); no allocator --wrap flags are required. */
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int fail_commit;
static int failed_commits;
static LPVOID WINAPI test_virtual_alloc(LPVOID addr, SIZE_T size,
                                        DWORD type, DWORD protect) {
    if (fail_commit && type == MEM_COMMIT) {
        failed_commits++;
        SetLastError(ERROR_COMMITMENT_LIMIT);
        return NULL;
    }
    return VirtualAlloc(addr, size, type, protect);
}
#define VirtualAlloc test_virtual_alloc
#include "../../src/runtime/rt_mem.c"
#undef VirtualAlloc

/* Allocator corruption is outside the injected commit-failure path. Keep
 * this standalone harness fail-closed if any such invariant is violated. */
void zan_rt_fatal(const char *category, const char *message) {
    fprintf(stderr, "FAIL unexpected fatal [%s]: %s\n", category, message);
    abort();
}

void *__real_malloc(size_t n) { return malloc(n); }
void __real_free(void *p) { free(p); }
void *__real_calloc(size_t n, size_t m) { return calloc(n, m); }
void *__real_realloc(void *p, size_t n) { return realloc(p, n); }

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; \
} } while (0)

int main(void) {
    void *first = zan_mem_small(32);
    CHECK(first != NULL);
    zan_mem_cache *c = zan_mem_cache_get();
    int cls = g_size_class[32];
    /* 无块头分页布局：类页用完才从 slab 未分出区间切新页，提交失败只可能
     * 发生在切页时。把当前类页标成用尽，逼下一次分配去切页。 */
    c->page_bump[cls] = c->page_end[cls];
    uintptr_t start = ((uintptr_t)c->bump + 4095) & ~(uintptr_t)4095;
    uintptr_t end = (uintptr_t)c->bump_end & ~(uintptr_t)4095;
    CHECK(end > start);
    CHECK(VirtualFree((void *)start, end - start, MEM_DECOMMIT));
    c->bump_decommitted = 1;
    char *before = c->bump;
    uint64_t allocs = g_small_alloc_count;

    fail_commit = 1;
    CHECK(zan_mem_small(32) == NULL);
    CHECK(failed_commits == 1);
    CHECK(c->bump == before);
    CHECK(c->bump_decommitted == 1);
    CHECK(g_small_alloc_count == allocs);
    /* Retrying the same cache must still attempt to commit, not write into
     * the decommitted pages or return their address. */
    CHECK(zan_mem_small(32) == NULL);
    CHECK(failed_commits == 2);
    CHECK(c->bump == before && c->bump_decommitted == 1);
    fail_commit = 0;
    void *retry = zan_mem_small(32);
    CHECK(retry == before);              /* 新页页首，无块头 */
    CHECK(c->bump == before + ZAN_MEM_PAGE);
    CHECK(c->bump_decommitted == 0);
    memset(retry, 0x5a, 32);
    puts("PASS recommit failure preserves allocator state and retry succeeds");
    return 0;
}
