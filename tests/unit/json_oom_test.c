/* json_oom_test.c -- reproduces A1: json_obj_set must not leave a dangling
 * keys pointer or leak the incoming value when a mid-grow realloc fails.
 *
 * json.c is compiled with -DZAN_ALLOC_INJECT (see src/common/host_oom.h), so
 * its abort-on-OOM policy is replaced by NULL-return and the Nth allocation is
 * forced to fail. We grow an object to capacity, then fail the *second* grow
 * realloc (the vals array) -- exactly the scenario where the original code
 * freed the freshly-realloc'd keys block but left obj->as.obj.keys pointing at
 * it, so a later json_free double-freed. */
#include "src/common/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Owned by this TU; referenced extern by host_oom.h under ZAN_ALLOC_INJECT
 * (pulled in transitively by json.c). */
int zan_alloc_fail_at = 0;
int zan_alloc_counter = 0;

static int failures = 0;

#define EXPECT(cond, ...) do {                                              \
    if (!(cond)) {                                                          \
        failures++;                                                         \
        fprintf(stderr, "FAIL: ");                                          \
        fprintf(stderr, __VA_ARGS__);                                       \
        fprintf(stderr, "\n");                                              \
    }                                                                       \
} while (0)

/* Fill the object so the next json_obj_set triggers a keys+vals grow. The
 * initial capacity is 8, so 8 entries fill it; the 9th grows to 16. */
static json_value *fill_to_capacity(void) {
    json_value *obj = json_new_obj();
    EXPECT(obj != NULL, "json_new_obj returned NULL");
    if (!obj) return NULL;
    for (int i = 0; i < 8; i++) {
        char k[16];
        snprintf(k, sizeof(k), "k%d", i);
        json_obj_set(obj, k, json_new_num(i));
    }
    return obj;
}

static void test_grow_vals_fail_leaves_object_consistent(void) {
    zan_alloc_fail_at = 0;
    zan_alloc_counter = 0;

    json_value *obj = fill_to_capacity();
    if (!obj) return;

    /* Create the incoming value first (with injection disabled) so the only
     * allocations counted inside json_obj_set are the two grows: keys (#1)
     * then vals (#2). Fail at #2 -> keys succeeds, vals fails. */
    json_value *val8 = json_new_num(8);
    zan_alloc_counter = 0;
    zan_alloc_fail_at = 2;
    json_obj_set(obj, "k8", val8);
    zan_alloc_fail_at = 0;

    /* The 8 prior entries must survive untouched. */
    EXPECT(json_obj_get(obj, "k0") != NULL, "entry k0 lost after grow failure");
    EXPECT(json_obj_get(obj, "k7") != NULL, "entry k7 lost after grow failure");
    /* k8 must NOT be present: the set failed, so val8 was freed, not stored. */
    EXPECT(json_obj_get(obj, "k8") == NULL,
           "k8 inserted despite vals grow failure (val leaked into freed slot)");

    /* Re-setting the same key (injection off) must still work: the object grew
     * keys but not vals, so this completes the grow and stores k8. With the
     * original dangling keys pointer this realloc/serialize crashes. */
    json_obj_set(obj, "k8", json_new_num(88));
    json_value *k8 = json_obj_get(obj, "k8");
    EXPECT(k8 != NULL && json_get_num(k8, -1) == 88.0,
           "k8 not stored after re-set following grow failure");

    /* Freeing must not double-free the keys array. The original code freed the
     * realloc'd keys block but left obj->as.obj.keys pointing at it. */
    json_free(obj);
    EXPECT(1, "json_free survived after vals grow failure");
}

static void test_grow_keys_fail_leaves_object_consistent(void) {
    zan_alloc_fail_at = 0;
    zan_alloc_counter = 0;

    json_value *obj = fill_to_capacity();
    if (!obj) return;

    json_value *val8 = json_new_num(8);
    /* Fail at #1 -> the keys grow realloc fails first. */
    zan_alloc_counter = 0;
    zan_alloc_fail_at = 1;
    json_obj_set(obj, "k8", val8);
    zan_alloc_fail_at = 0;

    EXPECT(json_obj_get(obj, "k0") != NULL, "entry k0 lost after keys grow failure");
    EXPECT(json_obj_get(obj, "k8") == NULL,
           "k8 inserted despite keys grow failure");
    json_free(obj);
    EXPECT(1, "json_free survived after keys grow failure");
}

static void test_normal_growth_still_works(void) {
    zan_alloc_fail_at = 0;
    zan_alloc_counter = 0;
    json_value *obj = json_new_obj();
    if (!obj) return;
    for (int i = 0; i < 64; i++) {
        char k[16];
        snprintf(k, sizeof(k), "k%d", i);
        json_obj_set(obj, k, json_new_num(i));
    }
    EXPECT(json_obj_get(obj, "k63") != NULL, "k63 missing after normal growth");
    /* Round-trip through serialize/parse to exercise the whole tree. */
    char *s = json_serialize(obj);
    EXPECT(s != NULL, "json_serialize returned NULL");
    json_value *reparsed = s ? json_parse(s) : NULL;
    EXPECT(reparsed != NULL, "re-parse of serialized object failed");
    json_free(reparsed);
    free(s);
    json_free(obj);
}

int main(void) {
    test_grow_vals_fail_leaves_object_consistent();
    test_grow_keys_fail_leaves_object_consistent();
    test_normal_growth_still_works();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
