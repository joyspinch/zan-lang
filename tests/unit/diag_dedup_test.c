#include "diag.h"
#include "arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define EXPECT(cond, ...) do {                                              \
    if (!(cond)) {                                                          \
        failures++;                                                         \
        fprintf(stderr, "FAIL: ");                                          \
        fprintf(stderr, __VA_ARGS__);                                       \
        fprintf(stderr, "\n");                                              \
    }                                                                       \
} while (0)

static void test_diag_dedup_basic(void) {
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_set_max_errors(diag, 100);
    zan_diag_add_file(diag, "test.zan", "int a = 1;\nint b = 2;\n");

    zan_loc_t loc1 = { .file_id = 0, .line = 1, .col = 5 };

    /* 1. Emit 3 identical errors: should be deduplicated to 1 */
    zan_diag_emit(diag, DIAG_ERROR, loc1, "duplicate type error '%s'", "Foo");
    zan_diag_emit(diag, DIAG_ERROR, loc1, "duplicate type error '%s'", "Foo");
    zan_diag_emit(diag, DIAG_ERROR, loc1, "duplicate type error '%s'", "Foo");
    EXPECT(diag->error_count == 1, "expected error_count == 1, got %d", diag->error_count);

    /* 2. Emit error at different column: should NOT be deduplicated */
    zan_loc_t loc2 = { .file_id = 0, .line = 1, .col = 6 };
    zan_diag_emit(diag, DIAG_ERROR, loc2, "duplicate type error '%s'", "Foo");
    EXPECT(diag->error_count == 2, "expected error_count == 2, got %d", diag->error_count);

    /* 3. Emit error at different line: should NOT be deduplicated */
    zan_loc_t loc3 = { .file_id = 0, .line = 2, .col = 5 };
    zan_diag_emit(diag, DIAG_ERROR, loc3, "duplicate type error '%s'", "Foo");
    EXPECT(diag->error_count == 3, "expected error_count == 3, got %d", diag->error_count);

    /* 4. Emit error with different message: should NOT be deduplicated */
    zan_diag_emit(diag, DIAG_ERROR, loc1, "different error '%s'", "Bar");
    EXPECT(diag->error_count == 4, "expected error_count == 4, got %d", diag->error_count);

    /* 5. Warnings deduplication */
    zan_diag_emit(diag, DIAG_WARNING, loc1, "unused variable '%s'", "x");
    zan_diag_emit(diag, DIAG_WARNING, loc1, "unused variable '%s'", "x");
    zan_diag_emit(diag, DIAG_WARNING, loc1, "unused variable '%s'", "x");
    EXPECT(diag->warning_count == 1, "expected warning_count == 1, got %d", diag->warning_count);

    zan_diag_free_buffers(diag);
    zan_arena_free(arena);
}

static void test_diag_capture_dedup(void) {
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_set_capture(diag, true);
    zan_diag_add_file(diag, "test.zan", "int a = 1;\n");

    zan_loc_t loc = { .file_id = 0, .line = 1, .col = 1 };

    /* In capture mode (LSP), duplicate entries must not bloat capture table */
    zan_diag_emit(diag, DIAG_ERROR, loc, "syntax error");
    zan_diag_emit(diag, DIAG_ERROR, loc, "syntax error");
    zan_diag_emit(diag, DIAG_ERROR, loc, "syntax error");

    EXPECT(zan_diag_entry_count(diag) == 1, "capture entry_count expected 1, got %d",
           zan_diag_entry_count(diag));

    zan_diag_free_buffers(diag);
    zan_arena_free(arena);
}

static void test_diag_ir_handoff_no_double_free(void) {
    zan_arena_t *front_arena = zan_arena_new();
    zan_arena_t *ir_arena = zan_arena_new();

    zan_diag_t *diag = zan_diag_new(front_arena);
    zan_diag_add_file(diag, "sample.zan", "class C {}");
    zan_diag_suppress_code(diag, 1005);

    zan_loc_t loc = { .file_id = 0, .line = 1, .col = 1 };
    zan_diag_emit(diag, DIAG_WARNING, loc, "test warning");

    /* Simulate main.c:3480 ir_diag handoff */
    zan_diag_t *ir_diag = (zan_diag_t *)zan_arena_alloc(ir_arena, sizeof(*ir_diag));
    *ir_diag = *diag;
    ir_diag->file_sources = NULL;
    const char **ir_file_names = (const char **)malloc(
        (size_t)ir_diag->file_count * sizeof(*ir_file_names));
    for (int i = 0; i < ir_diag->file_count; i++)
        ir_file_names[i] = zan_arena_strdup(ir_arena,
            ir_diag->file_names[i], strlen(ir_diag->file_names[i]));
    ir_diag->file_names = ir_file_names;
    ir_diag->entries = NULL;
    ir_diag->entry_count = ir_diag->entry_cap = 0;
    ir_diag->dedup_sigs = NULL;
    ir_diag->dedup_count = ir_diag->dedup_cap = 0;
    ir_diag->suppress_codes = NULL;
    ir_diag->suppress_count = ir_diag->suppress_cap = 0;
    ir_diag->pragma_ranges = NULL;
    ir_diag->pragma_count = ir_diag->pragma_cap = 0;

    /* Free front-end diag buffers and front_arena */
    zan_diag_free_buffers(diag);
    zan_arena_free(front_arena);

    /* ir_diag should still be able to emit and free without crash */
    zan_diag_emit(ir_diag, DIAG_WARNING, loc, "codegen warning");
    zan_diag_emit(ir_diag, DIAG_WARNING, loc, "codegen warning"); /* duplicate */
    EXPECT(ir_diag->warning_count == 2, "ir_diag should have 2 warnings total, got %d",
           ir_diag->warning_count);

    zan_diag_free_buffers(ir_diag);
    zan_arena_free(ir_arena);
}

static void test_diag_dedup_full_string(void) {
    /* B-ID206③：去重签名必须比完整格式化串，不是截断后的 msgbuf——
     * 两个超长诊断（>1024 字节）前 1023 字节相同、尾部不同时，截断串
     * 比对会把两条不同的错误判成同一条吞掉。 */
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_set_max_errors(diag, 100);
    zan_diag_add_file(diag, "test.zan", "int a = 1;\n");

    zan_loc_t loc = { .file_id = 0, .line = 1, .col = 1 };
    char long_a[1400], long_b[1400];
    memset(long_a, 'A', 1200);
    memset(long_b, 'A', 1200);
    snprintf(long_a + 1200, 200, "tail-alpha");
    snprintf(long_b + 1200, 200, "tail-beta");

    /* 尾部不同的两条超长诊断：完整串不同，都必须存活。
     * 修复前这里 error_count == 1（前 1023 字节相同被截断串误判去重）。 */
    zan_diag_emit(diag, DIAG_ERROR, loc, "%s", long_a);
    zan_diag_emit(diag, DIAG_ERROR, loc, "%s", long_b);
    EXPECT(diag->error_count == 2,
           "long msgs differing past the truncation point must both count, "
           "got %d", diag->error_count);

    /* 完整串相同的超长诊断仍然去重。 */
    zan_diag_emit(diag, DIAG_ERROR, loc, "%s", long_a);
    EXPECT(diag->error_count == 2,
           "identical long msgs must still dedup, got %d", diag->error_count);

    zan_diag_free_buffers(diag);
    zan_arena_free(arena);
}

int main(void) {
    test_diag_dedup_basic();
    test_diag_capture_dedup();
    test_diag_ir_handoff_no_double_free();
    test_diag_dedup_full_string();

    if (failures == 0) {
        printf("ALL DIAG DEDUP TESTS PASSED\n");
        return 0;
    } else {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
}
