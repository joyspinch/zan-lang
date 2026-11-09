/* Mobile keyboard insets and haptic feedback regression test. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define CHECK(cond, message) do { if (!(cond)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); exit(1); \
} } while (0)

typedef int32_t i32;

/* 模拟多端 C 驱动软键盘高度状态机 */
typedef struct {
    i32 keyboard_height;
} MockDriverState;

static void driver_set_keyboard_height(MockDriverState *d, i32 h) {
    if (h >= 0) {
        d->keyboard_height = h;
    }
}

static i32 driver_get_keyboard_height(const MockDriverState *d) {
    return d->keyboard_height;
}

static i32 driver_haptic_feedback(i32 type) {
    if (type < 0 || type > 6) {
        return -1; // 越界拒绝
    }
    return 0; // 成功分派马达震动
}

/* 模拟 KeyboardAvoidingView 避让内边距计算逻辑 */
typedef struct {
    i32 mode; // 0 = Padding, 1 = Position
    i32 extra_offset;
    i32 base_padding_bottom;
    i32 applied_padding_bottom;
} MockKeyboardAvoidingView;

static void kav_init(MockKeyboardAvoidingView *k, i32 base_pad, i32 extra) {
    k->mode = 0;
    k->base_padding_bottom = base_pad;
    k->extra_offset = extra;
    k->applied_padding_bottom = base_pad;
}

static void kav_sync_keyboard(MockKeyboardAvoidingView *k, i32 keyboard_h) {
    if (k->mode == 0) {
        if (keyboard_h > 0) {
            k->applied_padding_bottom = keyboard_h + k->extra_offset + k->base_padding_bottom;
        } else {
            k->applied_padding_bottom = k->base_padding_bottom;
        }
    }
}

int main(void) {
    /* 1. 验证 C 驱动软键盘状态机与边界防御 */
    MockDriverState drv = { .keyboard_height = 0 };
    CHECK(driver_get_keyboard_height(&drv) == 0, "initial keyboard height must be 0");

    driver_set_keyboard_height(&drv, 336);
    CHECK(driver_get_keyboard_height(&drv) == 336, "keyboard height update failed");

    /* 负值防篡改守卫：异常输入不得破坏当前状态 */
    driver_set_keyboard_height(&drv, -50);
    CHECK(driver_get_keyboard_height(&drv) == 336, "negative keyboard height must be rejected");

    driver_set_keyboard_height(&drv, 0);
    CHECK(driver_get_keyboard_height(&drv) == 0, "keyboard dismiss reset failed");

    /* 2. 验证触觉反馈枚举分派 */
    for (i32 t = 0; t <= 6; t++) {
        CHECK(driver_haptic_feedback(t) == 0, "haptic feedback valid enum must succeed");
    }
    CHECK(driver_haptic_feedback(-1) == -1, "negative haptic type must be rejected");
    CHECK(driver_haptic_feedback(7) == -1, "out of bounds haptic type must be rejected");

    /* 3. 验证 KeyboardAvoidingView 避让几何推导 */
    MockKeyboardAvoidingView kav;
    kav_init(&kav, 16, 8); // base_pad = 16px, extra = 8px
    CHECK(kav.applied_padding_bottom == 16, "initial padding mismatch");

    // 虚拟键盘弹起 (高度 320px)
    kav_sync_keyboard(&kav, 320);
    CHECK(kav.applied_padding_bottom == (320 + 8 + 16), "avoiding padding mismatch (320 + 8 + 16 = 344)");

    // 虚拟键盘收起 (高度 0px)
    kav_sync_keyboard(&kav, 0);
    CHECK(kav.applied_padding_bottom == 16, "dismiss reset padding mismatch");

    printf("PASS: gui_keyboard_haptic_test passed all checks.\n");
    return 0;
}
