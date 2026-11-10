/* Hardware sensors and system notification C-ABI regression test. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#define CHECK(cond, message) do { if (!(cond)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); exit(1); \
} } while (0)

typedef int32_t i32;

/* 模拟驱动中的全局传感器状态 */
static float g_sensor_accel[3] = { 0.0f, 9.8f, 0.0f }; // 默认静止重力向下
static float g_sensor_gyro[3]  = { 0.0f, 0.0f, 0.0f };
static float g_sensor_compass  = 0.0f; // 0~360 deg

static i32 mock_sensor_get(i32 sensor_type, float *out_x, float *out_y, float *out_z) {
    if (!out_x || !out_y || !out_z) return -1;
    if (sensor_type == 1) { // 1: Accelerometer (三轴加速度，含重力，m/s^2)
        *out_x = g_sensor_accel[0];
        *out_y = g_sensor_accel[1];
        *out_z = g_sensor_accel[2];
        return 0;
    } else if (sensor_type == 2) { // 2: Gyroscope (三轴角速度，rad/s)
        *out_x = g_sensor_gyro[0];
        *out_y = g_sensor_gyro[1];
        *out_z = g_sensor_gyro[2];
        return 0;
    } else if (sensor_type == 3) { // 3: Compass (地磁偏角，deg)
        *out_x = g_sensor_compass;
        *out_y = 0.0f;
        *out_z = 0.0f;
        return 0;
    }
    return -1;
}

static i32 mock_sensor_set(i32 sensor_type, float x, float y, float z) {
    if (sensor_type == 1) {
        g_sensor_accel[0] = x; g_sensor_accel[1] = y; g_sensor_accel[2] = z;
        return 0;
    } else if (sensor_type == 2) {
        g_sensor_gyro[0] = x; g_sensor_gyro[1] = y; g_sensor_gyro[2] = z;
        return 0;
    } else if (sensor_type == 3) {
        g_sensor_compass = x;
        return 0;
    }
    return -1;
}

static i32 mock_notification_send(const char *title, const char *body, const char *action, i32 badge, const char *sound) {
    if (!title || !body) return -1;
    (void)action; (void)badge; (void)sound;
    return 0;
}

int main(void) {
    /* 1. 验证加速度计默认物理重力向量 */
    float x = 0.0f, y = 0.0f, z = 0.0f;
    CHECK(mock_sensor_get(1, &x, &y, &z) == 0, "sensor_get accel must succeed");
    CHECK(fabsf(x - 0.0f) < 0.001f, "default accel x must be 0");
    CHECK(fabsf(y - 9.8f) < 0.001f, "default accel y must be 9.8 (gravity)");
    CHECK(fabsf(z - 0.0f) < 0.001f, "default accel z must be 0");

    /* 2. 验证陀螺仪静止状态 */
    CHECK(mock_sensor_get(2, &x, &y, &z) == 0, "sensor_get gyro must succeed");
    CHECK(fabsf(x) < 0.001f && fabsf(y) < 0.001f && fabsf(z) < 0.001f, "default gyro must be 0");

    /* 3. 验证指针与异常类型保护 */
    CHECK(mock_sensor_get(1, NULL, &y, &z) == -1, "null pointer x must return -1");
    CHECK(mock_sensor_get(1, &x, NULL, &z) == -1, "null pointer y must return -1");
    CHECK(mock_sensor_get(1, &x, &y, NULL) == -1, "null pointer z must return -1");
    CHECK(mock_sensor_get(999, &x, &y, &z) == -1, "unknown sensor type must return -1");

    /* 4. 验证动态模拟数据注入与姿态解算 */
    // 注入倾斜 45 度状态：水平 ax = 6.93, 垂直 ay = 6.93
    CHECK(mock_sensor_set(1, 6.93f, 6.93f, 0.0f) == 0, "sensor_set accel must succeed");
    CHECK(mock_sensor_get(1, &x, &y, &z) == 0, "read injected accel");
    CHECK(fabsf(x - 6.93f) < 0.001f, "injected accel x match");
    CHECK(fabsf(y - 6.93f) < 0.001f, "injected accel y match");

    // 注入陀螺仪绕 Z 轴高速旋转 (1.57 rad/s ~= 90 deg/s)
    CHECK(mock_sensor_set(2, 0.0f, 0.0f, 1.57f) == 0, "sensor_set gyro must succeed");
    CHECK(mock_sensor_get(2, &x, &y, &z) == 0, "read injected gyro");
    CHECK(fabsf(z - 1.57f) < 0.001f, "injected gyro z match");

    // 注入罗盘正东朝向 (90 deg)
    CHECK(mock_sensor_set(3, 90.0f, 0.0f, 0.0f) == 0, "sensor_set compass must succeed");
    CHECK(mock_sensor_get(3, &x, &y, &z) == 0, "read injected compass");
    CHECK(fabsf(x - 90.0f) < 0.001f, "compass heading must be 90 deg");

    /* 5. 验证通知调度参数契约 */
    CHECK(mock_notification_send("任务完成", "构建成功", "open_log", 1, "chime") == 0, "notification_send must succeed");
    CHECK(mock_notification_send(NULL, "内容", "", 0, "") == -1, "null title must fail");
    CHECK(mock_notification_send("标题", NULL, "", 0, "") == -1, "null body must fail");

    printf("PASS: gui_hardware_sensor_test passed all checks.\n");
    return 0;
}
