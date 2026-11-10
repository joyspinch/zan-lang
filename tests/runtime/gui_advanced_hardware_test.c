/* Mobile & Multi-platform Advanced Hardware (Screen, Geolocation, DeviceInfo, Biometrics) regression test. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#define CHECK(cond, message) do { if (!(cond)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); exit(1); \
} } while (0)

typedef int32_t i32;

/* --- 模拟驱动：屏幕常亮与亮度控制 --- */
static int g_screen_keep_awake = 0;
static float g_screen_brightness = -1.0f;

static void mock_screen_keep_awake(i32 enable) {
    g_screen_keep_awake = enable ? 1 : 0;
}

static i32 mock_screen_is_keep_awake(void) {
    return g_screen_keep_awake;
}

static void mock_screen_set_brightness(float brightness) {
    g_screen_brightness = brightness;
}

static float mock_screen_get_brightness(void) {
    return g_screen_brightness;
}

/* --- 模拟驱动：GPS 地理定位 --- */
static double g_location_lat = 0.0;
static double g_location_lon = 0.0;
static double g_location_alt = 0.0;
static float  g_location_acc = 0.0f;
static float  g_location_speed = 0.0f;

static i32 mock_location_get(double *out_coords, float *out_metrics) {
    if (out_coords) {
        out_coords[0] = g_location_lat;
        out_coords[1] = g_location_lon;
        out_coords[2] = g_location_alt;
    }
    if (out_metrics) {
        out_metrics[0] = g_location_acc;
        out_metrics[1] = g_location_speed;
    }
    return 0;
}

static void mock_location_set(double lat, double lon, double alt, float acc, float speed) {
    g_location_lat = lat;
    g_location_lon = lon;
    g_location_alt = alt;
    g_location_acc = acc;
    g_location_speed = speed;
}

/* --- 模拟驱动：设备信息 --- */
static char g_dev_model[64] = "HUAWEI Mate XT";
static char g_dev_manufacturer[64] = "HUAWEI";
static char g_dev_platform[32] = "HarmonyOS";
static char g_dev_os_version[32] = "Next.0.0.36";
static int  g_dev_is_foldable = 1;

static const char *mock_device_info_get(i32 info_key) {
    if (info_key == 1) return g_dev_model;
    if (info_key == 2) return g_dev_manufacturer;
    if (info_key == 3) return g_dev_platform;
    if (info_key == 4) return g_dev_os_version;
    return "";
}

static i32 mock_device_is_foldable(void) {
    return g_dev_is_foldable;
}

static void mock_device_info_set(i32 info_key, const char *val) {
    if (!val) return;
    if (info_key == 1) { strncpy(g_dev_model, val, sizeof(g_dev_model)-1); g_dev_model[sizeof(g_dev_model)-1] = '\0'; }
    else if (info_key == 2) { strncpy(g_dev_manufacturer, val, sizeof(g_dev_manufacturer)-1); g_dev_manufacturer[sizeof(g_dev_manufacturer)-1] = '\0'; }
    else if (info_key == 3) { strncpy(g_dev_platform, val, sizeof(g_dev_platform)-1); g_dev_platform[sizeof(g_dev_platform)-1] = '\0'; }
    else if (info_key == 4) { strncpy(g_dev_os_version, val, sizeof(g_dev_os_version)-1); g_dev_os_version[sizeof(g_dev_os_version)-1] = '\0'; }
    else if (info_key == 5) { g_dev_is_foldable = (atoi(val) != 0) ? 1 : 0; }
}

/* --- 模拟驱动：生物识别 --- */
static int g_bio_can_auth = 1;
static int g_bio_type = 2; /* 2: Face */
static int g_bio_auth_result = 1;

static i32 mock_biometrics_can_authenticate(void) {
    return g_bio_can_auth;
}

static i32 mock_biometrics_type(void) {
    return g_bio_type;
}

static i32 mock_biometrics_authenticate(const char *title, const char *subtitle) {
    (void)title; (void)subtitle;
    return g_bio_auth_result;
}

static void mock_biometrics_set_mock(i32 can_auth, i32 bio_type, i32 auth_result) {
    g_bio_can_auth = can_auth ? 1 : 0;
    g_bio_type = bio_type;
    g_bio_auth_result = auth_result ? 1 : 0;
}

int main(void) {
    /* 1. 验证屏幕防休眠与亮度控制 */
    CHECK(mock_screen_is_keep_awake() == 0, "default keep-awake must be disabled (0)");
    mock_screen_keep_awake(1);
    CHECK(mock_screen_is_keep_awake() == 1, "keep-awake must be enabled (1)");
    mock_screen_keep_awake(0);
    CHECK(mock_screen_is_keep_awake() == 0, "keep-awake must be restored to disabled (0)");

    CHECK(mock_screen_get_brightness() < 0.0f, "default brightness must be negative (follow system)");
    mock_screen_set_brightness(1.0f);
    CHECK(fabsf(mock_screen_get_brightness() - 1.0f) < 1e-4f, "brightness must be set to 1.0f");
    mock_screen_set_brightness(-1.0f);
    CHECK(mock_screen_get_brightness() < 0.0f, "brightness must be restored to follow system");

    /* 2. 验证 GPS 地理定位数据管道 */
    double coords[3] = { 0.0, 0.0, 0.0 };
    float  metrics[2] = { 0.0f, 0.0f };
    mock_location_set(31.2304, 121.4737, 12.5, 4.5f, 22.0f);
    mock_location_get(coords, metrics);
    CHECK(fabs(coords[0] - 31.2304) < 1e-5, "latitude must match injected value (31.2304)");
    CHECK(fabs(coords[1] - 121.4737) < 1e-5, "longitude must match injected value (121.4737)");
    CHECK(fabs(coords[2] - 12.5) < 1e-5, "altitude must match injected value (12.5)");
    CHECK(fabsf(metrics[0] - 4.5f) < 1e-4f, "accuracy must match injected value (4.5m)");
    CHECK(fabsf(metrics[1] - 22.0f) < 1e-4f, "speed must match injected value (22.0m/s)");

    /* 3. 验证设备信息与折叠屏形态识别 */
    CHECK(strcmp(mock_device_info_get(1), "HUAWEI Mate XT") == 0, "model must be HUAWEI Mate XT");
    CHECK(strcmp(mock_device_info_get(2), "HUAWEI") == 0, "manufacturer must be HUAWEI");
    CHECK(strcmp(mock_device_info_get(3), "HarmonyOS") == 0, "platform must be HarmonyOS");
    CHECK(strcmp(mock_device_info_get(4), "Next.0.0.36") == 0, "OS version must be Next.0.0.36");
    CHECK(mock_device_is_foldable() == 1, "HUAWEI Mate XT must be recognized as foldable (1)");

    // 动态注入模拟新机型
    mock_device_info_set(1, "iPhone15,2");
    mock_device_info_set(2, "Apple");
    mock_device_info_set(3, "iOS");
    mock_device_info_set(4, "17.4");
    mock_device_info_set(5, "0");
    CHECK(strcmp(mock_device_info_get(1), "iPhone15,2") == 0, "model must update to iPhone15,2");
    CHECK(mock_device_is_foldable() == 0, "iPhone must be non-foldable (0)");

    /* 4. 验证生物识别安全鉴权流程 */
    CHECK(mock_biometrics_can_authenticate() == 1, "default biometrics should be supported");
    CHECK(mock_biometrics_type() == 2, "default biometric type should be Face (2)");
    CHECK(mock_biometrics_authenticate("支付验证", "请刷脸完成支付") == 1, "authentication must succeed");

    // 模拟无生物凭据或用户取消
    mock_biometrics_set_mock(0, 0, 0);
    CHECK(mock_biometrics_can_authenticate() == 0, "mocked biometrics should report unavailable");
    CHECK(mock_biometrics_authenticate("支付验证", "") == 0, "cancelled authentication must return 0");

    printf("PASS: gui_advanced_hardware_test passed all checks.\n");
    return 0;
}
