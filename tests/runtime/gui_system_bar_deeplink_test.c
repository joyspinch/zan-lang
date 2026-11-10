/* Mobile & Multi-platform SystemBar and DeepLink regression test. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define CHECK(cond, message) do { if (!(cond)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); exit(1); \
} } while (0)

typedef int32_t i32;
typedef uint32_t u32;

/* --- 模拟驱动：系统状态栏与导航栏控制 --- */
static int g_status_bar_visible = 1;
static int g_status_bar_style = 0; /* 0: Default, 1: LightIcons, 2: DarkIcons */
static uint32_t g_status_bar_color = 0x00000000;
static int g_nav_bar_visible = 1;

static void mock_system_bar_set_status(i32 visible, i32 style, u32 color) {
    g_status_bar_visible = visible ? 1 : 0;
    if (style >= 0 && style <= 2) g_status_bar_style = style;
    g_status_bar_color = color;
}

static i32 mock_system_bar_get_status_visible(void) {
    return g_status_bar_visible;
}

static i32 mock_system_bar_get_status_style(void) {
    return g_status_bar_style;
}

static i32 mock_system_bar_get_status_color(void) {
    return (i32)g_status_bar_color;
}

static void mock_system_bar_set_nav_visible(i32 visible) {
    g_nav_bar_visible = visible ? 1 : 0;
}

static i32 mock_system_bar_get_nav_visible(void) {
    return g_nav_bar_visible;
}

/* --- 模拟驱动：深度链接与外部唤醒 --- */
static char g_pending_open_url[512] = "";

static const char *mock_take_open_url(void) {
    static char s_take_buf[512];
    if (g_pending_open_url[0] == '\0') return "";
    strncpy(s_take_buf, g_pending_open_url, sizeof(s_take_buf)-1);
    s_take_buf[sizeof(s_take_buf)-1] = '\0';
    g_pending_open_url[0] = '\0';
    return s_take_buf;
}

static void mock_inject_open_url(const char *url) {
    if (!url) {
        g_pending_open_url[0] = '\0';
        return;
    }
    strncpy(g_pending_open_url, url, sizeof(g_pending_open_url)-1);
    g_pending_open_url[sizeof(g_pending_open_url)-1] = '\0';
}

int main(void) {
    /* 1. 验证系统状态栏与导航栏控制通道 */
    CHECK(mock_system_bar_get_status_visible() == 1, "status bar must be visible by default");
    CHECK(mock_system_bar_get_status_style() == 0, "default status bar style must be 0 (Default)");
    CHECK(mock_system_bar_get_nav_visible() == 1, "navigation bar must be visible by default");

    // 切换为浅色背景下的深色文字图标（DarkIcons = 2, 白色底色 0xFFFFFFFF）
    mock_system_bar_set_status(1, 2, 0xFFFFFFFF);
    CHECK(mock_system_bar_get_status_visible() == 1, "status bar must remain visible");
    CHECK(mock_system_bar_get_status_style() == 2, "status bar style must update to DarkIcons (2)");
    CHECK((u32)mock_system_bar_get_status_color() == 0xFFFFFFFF, "status bar color must match 0xFFFFFFFF");

    // 进入全屏沉浸（隐藏状态栏与导航条）
    mock_system_bar_set_status(0, 1, 0x00000000);
    mock_system_bar_set_nav_visible(0);
    CHECK(mock_system_bar_get_status_visible() == 0, "status bar must be hidden in immersive mode");
    CHECK(mock_system_bar_get_status_style() == 1, "status bar style must update to LightIcons (1)");
    CHECK(mock_system_bar_get_nav_visible() == 0, "navigation bar must be hidden in immersive mode");

    /* 2. 验证外部唤醒与深度链接队列的单次消费契约 */
    CHECK(strcmp(mock_take_open_url(), "") == 0, "initial pending URL must be empty");

    // 模拟从外部浏览器点击唤起应用
    mock_inject_open_url("zanapp://order/pay?id=8888");
    const char *taken = mock_take_open_url();
    CHECK(strcmp(taken, "zanapp://order/pay?id=8888") == 0, "take_open_url must return injected deep link");

    // 验证消费后清空
    CHECK(strcmp(mock_take_open_url(), "") == 0, "subsequent take_open_url must be empty (consumed)");

    // 再次注入并消费新链接
    mock_inject_open_url("https://app.zanlang.org/open?tab=profile");
    CHECK(strcmp(mock_take_open_url(), "https://app.zanlang.org/open?tab=profile") == 0, "second URL must be taken properly");
    CHECK(strcmp(mock_take_open_url(), "") == 0, "queue must remain empty after second take");

    printf("PASS: gui_system_bar_deeplink_test passed all checks.\n");
    return 0;
}
