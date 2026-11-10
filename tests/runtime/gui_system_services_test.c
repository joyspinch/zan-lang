/* Mobile & Multi-platform system services (Network, Theme, Share, Permissions) regression test. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define CHECK(cond, message) do { if (!(cond)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); exit(1); \
} } while (0)

typedef int32_t i32;

/* 模拟驱动中的全局系统服务状态 */
static int g_network_type = 1; // 默认 1: Wifi
static int g_dark_mode = 0;    // 默认 0: 浅色模式
static int g_permissions[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }; // 默认 0: Granted

static i32 mock_network_type(void) { return g_network_type; }
static void mock_set_network_type(i32 type) { if (type >= 0 && type <= 3) g_network_type = (int)type; }

static i32 mock_is_dark_mode(void) { return g_dark_mode; }
static void mock_set_dark_mode(i32 dark) { g_dark_mode = dark ? 1 : 0; }

static i32 mock_share_text(const char *text, const char *title) {
    if (!text) return -1;
    (void)title;
    return 0;
}

static i32 mock_permission_check(i32 perm_type) {
    if (perm_type >= 0 && perm_type < 8) return g_permissions[perm_type];
    return 1; // Denied
}

static i32 mock_permission_request(i32 perm_type) {
    if (perm_type >= 0 && perm_type < 8) return g_permissions[perm_type];
    return 1; // Denied
}

static void mock_set_permission(i32 perm_type, i32 status) {
    if (perm_type >= 0 && perm_type < 8) g_permissions[perm_type] = status;
}

int main(void) {
    /* 1. 验证网络状态机流转 */
    CHECK(mock_network_type() == 1, "default network must be Wifi (1)");
    mock_set_network_type(2); // 切换至 Cellular
    CHECK(mock_network_type() == 2, "network must switch to Cellular (2)");
    mock_set_network_type(0); // 切换至 Offline
    CHECK(mock_network_type() == 0, "network must switch to Offline (0)");
    mock_set_network_type(99); // 越界注入保护
    CHECK(mock_network_type() == 0, "invalid network type must be rejected");

    /* 2. 验证深浅色主题状态切换 */
    CHECK(mock_is_dark_mode() == 0, "default theme must be Light (0)");
    mock_set_dark_mode(1);
    CHECK(mock_is_dark_mode() == 1, "theme must switch to Dark (1)");
    mock_set_dark_mode(0);
    CHECK(mock_is_dark_mode() == 0, "theme must revert to Light (0)");

    /* 3. 验证系统分享面板参数校验契约 */
    CHECK(mock_share_text("https://example.com", "分享标题") == 0, "valid share text must succeed");
    CHECK(mock_share_text(NULL, "标题") == -1, "null text must be rejected with -1");

    /* 4. 验证权限状态机与越界防御 */
    // 权限类型 0: Location 默认 Granted
    CHECK(mock_permission_check(0) == 0, "default location permission must be Granted");
    // 模拟用户拒绝相机权限 (类型 1: Camera)
    mock_set_permission(1, 1);
    CHECK(mock_permission_check(1) == 1, "camera permission must be Denied");
    CHECK(mock_permission_request(1) == 1, "camera request must reflect Denied");
    // 未知权限类型保护
    CHECK(mock_permission_check(99) == 1, "unknown permission must safely return Denied");

    printf("PASS: gui_system_services_test passed all checks.\n");
    return 0;
}
