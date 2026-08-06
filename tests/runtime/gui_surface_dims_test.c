/* gui_surface_dims_test.c -- reproduces A4: zan_gui_create_surface must reject
 * non-positive or absurdly large dimensions instead of computing a
 * width*height that overflows int (and then malloc'ing the wrong size, or
 * aborting under the OOM policy), and must cope with a NULL pixel allocation.
 *
 * The existing gui_runtime_test covers the happy path; this one covers the
 * rejection path. */
#include <stdint.h>
#include <stdio.h>

typedef int64_t i64;
typedef int32_t i32;

extern i32 zan_gui_create_surface(i64 width, i64 height);
extern i32 zan_gui_destroy_surface(i64 id);

static int failures = 0;

#define EXPECT(cond, ...) do {                                              \
    if (!(cond)) {                                                          \
        failures++;                                                         \
        fprintf(stderr, "FAIL: ");                                          \
        fprintf(stderr, __VA_ARGS__);                                       \
        fprintf(stderr, "\n");                                              \
    }                                                                       \
} while (0)

static void test_zero_dims_rejected(void) {
    i64 s = zan_gui_create_surface(0, 0);
    EXPECT(s < 0, "create_surface(0,0) should fail (got %lld)", (long long)s);
    if (s >= 0) zan_gui_destroy_surface(s);
}

static void test_negative_dims_rejected(void) {
    i64 s = zan_gui_create_surface(-1, 10);
    EXPECT(s < 0, "create_surface(-1,10) should fail (got %lld)", (long long)s);
    if (s >= 0) zan_gui_destroy_surface(s);

    s = zan_gui_create_surface(10, -1);
    EXPECT(s < 0, "create_surface(10,-1) should fail (got %lld)", (long long)s);
    if (s >= 0) zan_gui_destroy_surface(s);
}

static void test_oversize_dims_rejected(void) {
    /* 100000 x 100000 = 10^10 pixels: overflows 32-bit int in width*height,
     * and 40 GB of pixels is not a legitimate surface. Must be rejected. */
    i64 s = zan_gui_create_surface(100000, 100000);
    EXPECT(s < 0, "create_surface(100000,100000) should fail (got %lld)", (long long)s);
    if (s >= 0) zan_gui_destroy_surface(s);

    /* just over the 16384 cap on one axis */
    s = zan_gui_create_surface(16385, 16);
    EXPECT(s < 0, "create_surface(16385,16) should fail (got %lld)", (long long)s);
    if (s >= 0) zan_gui_destroy_surface(s);
}

static void test_normal_create_still_works(void) {
    i64 s = zan_gui_create_surface(64, 48);
    EXPECT(s >= 0, "create_surface(64,48) should succeed (got %lld)", (long long)s);
    if (s >= 0) {
        EXPECT(zan_gui_destroy_surface(s) == 0, "destroy failed");
    }

    /* boundary: exactly 16384 is allowed */
    s = zan_gui_create_surface(16384, 1);
    EXPECT(s >= 0, "create_surface(16384,1) should succeed (got %lld)", (long long)s);
    if (s >= 0) zan_gui_destroy_surface(s);
}

int main(void) {
    test_zero_dims_rejected();
    test_negative_dims_rejected();
    test_oversize_dims_rejected();
    test_normal_create_still_works();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
