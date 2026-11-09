/* Multitarget driver regression test: safe area and clipboard parity. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, message) do { if (!(cond)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); exit(1); \
} } while (0)

typedef int32_t i32;
typedef int64_t i64;
typedef intptr_t iptr;

/* 模拟多端 Native 驱动环境下的 SafeArea 与 Clipboard 状态机 */
static int g_safe_area_top = 0;
static int g_safe_area_bottom = 0;
static int g_safe_area_left = 0;
static int g_safe_area_right = 0;

static void set_safe_area(i32 top, i32 bottom, i32 left, i32 right) {
    if (top >= 0) g_safe_area_top = (int)top;
    if (bottom >= 0) g_safe_area_bottom = (int)bottom;
    if (left >= 0) g_safe_area_left = (int)left;
    if (right >= 0) g_safe_area_right = (int)right;
}

static i32 get_safe_area_top(void)    { return g_safe_area_top; }
static i32 get_safe_area_bottom(void) { return g_safe_area_bottom; }
static i32 get_safe_area_left(void)   { return g_safe_area_left; }
static i32 get_safe_area_right(void)  { return g_safe_area_right; }

static char g_clip_buf[2048] = {0};
static i32 set_clipboard(const char *utf8) {
    if (!utf8) return 1;
    strncpy(g_clip_buf, utf8, sizeof(g_clip_buf) - 1);
    g_clip_buf[sizeof(g_clip_buf) - 1] = '\0';
    return 0;
}
static const char *get_clipboard(void) {
    return g_clip_buf;
}

int main(void) {
    /* 1. 验证默认初始状态 */
    CHECK(get_safe_area_top() == 0, "default safe_area_top must be 0");
    CHECK(get_safe_area_bottom() == 0, "default safe_area_bottom must be 0");
    CHECK(get_safe_area_left() == 0, "default safe_area_left must be 0");
    CHECK(get_safe_area_right() == 0, "default safe_area_right must be 0");

    /* 2. 验证设置真实刘海与手势横条 */
    set_safe_area(44, 34, 12, 12);
    CHECK(get_safe_area_top() == 44, "safe_area_top should be 44");
    CHECK(get_safe_area_bottom() == 34, "safe_area_bottom should be 34");
    CHECK(get_safe_area_left() == 12, "safe_area_left should be 12");
    CHECK(get_safe_area_right() == 12, "safe_area_right should be 12");

    /* 3. 验证负数防御 */
    set_safe_area(-1, -5, -10, -20);
    CHECK(get_safe_area_top() == 44, "negative values must not overwrite safe_area_top");
    CHECK(get_safe_area_bottom() == 34, "negative values must not overwrite safe_area_bottom");

    /* 4. 验证剪贴板读写契约（0 为成功） */
    CHECK(set_clipboard(NULL) == 1, "null clipboard write must fail with 1");
    CHECK(set_clipboard("Zan Native NDK Parity") == 0, "valid write must succeed with 0");
    CHECK(strcmp(get_clipboard(), "Zan Native NDK Parity") == 0, "clipboard read mismatch");

    printf("PASS: gui_multitarget_driver_test passed all checks.\n");
    return 0;
}
