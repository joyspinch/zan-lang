/* 底层系统交互与数据协议契约 */
#include "src/common/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 底层系统交互与数据协议契约 */
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

/* 底层系统交互与数据协议契约 */
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

    /* 底层系统交互与数据协议契约 */
    json_value *val8 = json_new_num(8);
    zan_alloc_counter = 0;
    zan_alloc_fail_at = 2;
    json_obj_set(obj, "k8", val8);
    zan_alloc_fail_at = 0;

    /* 底层系统交互与数据协议契约 */
    EXPECT(json_obj_get(obj, "k0") != NULL, "entry k0 lost after grow failure");
    EXPECT(json_obj_get(obj, "k7") != NULL, "entry k7 lost after grow failure");
    /* 底层系统交互与数据协议契约 */
    EXPECT(json_obj_get(obj, "k8") == NULL,
           "k8 inserted despite vals grow failure (val leaked into freed slot)");

    /* 底层系统交互与数据协议契约 */
    json_obj_set(obj, "k8", json_new_num(88));
    json_value *k8 = json_obj_get(obj, "k8");
    EXPECT(k8 != NULL && json_get_num(k8, -1) == 88.0,
           "k8 not stored after re-set following grow failure");

    /* 底层系统交互与数据协议契约 */
    json_free(obj);
    EXPECT(1, "json_free survived after vals grow failure");
}

static void test_grow_keys_fail_leaves_object_consistent(void) {
    zan_alloc_fail_at = 0;
    zan_alloc_counter = 0;

    json_value *obj = fill_to_capacity();
    if (!obj) return;

    json_value *val8 = json_new_num(8);
    /* 底层系统交互与数据协议契约 */
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
    /* 底层系统交互与数据协议契约 */
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
