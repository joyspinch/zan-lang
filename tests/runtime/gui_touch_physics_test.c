/* Touch physics, fling momentum, and rubber-band spring regression test. */
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

/* 1. 模拟 VelocityTracker */
typedef struct {
    i32 time_ms, x, y;
} TouchSample;

typedef struct {
    TouchSample samples[16];
    i32 count;
    i32 head;
    float vx, vy;
} MockVelocityTracker;

static void vt_init(MockVelocityTracker *v) {
    v->count = 0;
    v->head = 0;
    v->vx = 0.0f;
    v->vy = 0.0f;
}

static void vt_add(MockVelocityTracker *v, i32 t, i32 x, i32 y) {
    v->samples[v->head].time_ms = t;
    v->samples[v->head].x = x;
    v->samples[v->head].y = y;
    v->head = (v->head + 1) % 16;
    if (v->count < 16) v->count++;
}

static void vt_compute(MockVelocityTracker *v, i32 units, float max_v) {
    if (v->count < 2) { v->vx = v->vy = 0.0f; return; }
    i32 last_idx = (v->head - 1 + 16) % 16;
    TouchSample latest = v->samples[last_idx];
    TouchSample earliest = latest;

    for (i32 i = 1; i < v->count; i++) {
        i32 idx = (v->head - 1 - i + 32) % 16;
        TouchSample s = v->samples[idx];
        if (latest.time_ms - s.time_ms > 150) break;
        earliest = s;
    }

    i32 dt = latest.time_ms - earliest.time_ms;
    if (dt <= 10) { v->vx = v->vy = 0.0f; return; }

    float scale = (float)units / (float)dt;
    float raw_vx = (float)(latest.x - earliest.x) * scale;
    float raw_vy = (float)(latest.y - earliest.y) * scale;

    if (raw_vx > max_v) raw_vx = max_v;
    else if (raw_vx < -max_v) raw_vx = -max_v;

    if (raw_vy > max_v) raw_vy = max_v;
    else if (raw_vy < -max_v) raw_vy = -max_v;

    v->vx = raw_vx;
    v->vy = raw_vy;
}

/* 2. 模拟 ScrollPhysics */
static float rubber_band_clamp(float over_raw, float dimension) {
    if (dimension <= 0.0f) dimension = 300.0f;
    bool neg = over_raw < 0.0f;
    float abs_over = neg ? -over_raw : over_raw;
    float factor = (abs_over * 0.55f) / dimension;
    float rubber = (1.0f - (1.0f / (factor + 1.0f))) * dimension;
    return neg ? -rubber : rubber;
}

static float step_fling(float *velocity, i32 dt_ms) {
    if (dt_ms <= 0) return 0.0f;
    float dt_sec = (float)dt_ms / 1000.0f;
    float delta = (*velocity) * dt_sec;

    float decay = 1.0f - (0.0025f * (float)dt_ms);
    if (decay < 0.0f) decay = 0.0f;
    *velocity = (*velocity) * decay;

    float abs_v = fabsf(*velocity);
    if (abs_v < 15.0f) {
        *velocity = 0.0f;
    }
    return delta;
}

static bool step_spring(float *disp, float *velocity, i32 dt_ms) {
    if (dt_ms <= 0) return false;
    float dt = (float)dt_ms / 1000.0f;
    if (dt > 0.05f) dt = 0.05f;

    float force = (-180.0f) * (*disp) - (22.0f * (*velocity));
    *velocity = (*velocity) + force * dt;
    *disp = (*disp) + (*velocity) * dt;

    if (fabsf(*disp) < 0.5f && fabsf(*velocity) < 5.0f) {
        *disp = 0.0f;
        *velocity = 0.0f;
        return true;
    }
    return false;
}

int main(void) {
    /* 1. 验证速度追踪器在滑动释放时的动量输出 */
    MockVelocityTracker vt;
    vt_init(&vt);

    /* 模拟 80ms 内自 y=100 滑动至 y=420 (位移 320px) */
    vt_add(&vt, 1000, 200, 100);
    vt_add(&vt, 1020, 200, 180);
    vt_add(&vt, 1040, 200, 260);
    vt_add(&vt, 1060, 200, 340);
    vt_add(&vt, 1080, 200, 420);

    vt_compute(&vt, 1000, 8000.0f);
    CHECK(vt.vx == 0.0f, "horizontal velocity should be 0");
    /* 320px / 80ms * 1000 = 4000 px/s */
    CHECK(fabsf(vt.vy - 4000.0f) < 1.0f, "vertical velocity must equal 4000 px/s");

    /* 2. 验证边缘橡皮筋非线性阻尼拉伸曲线 */
    CHECK(rubber_band_clamp(0.0f, 600.0f) == 0.0f, "zero displacement must be 0");

    float r1 = rubber_band_clamp(100.0f, 600.0f);
    /* (1 - 1 / (100 * 0.55 / 600 + 1)) * 600 ≈ 50.38px */
    CHECK(r1 > 48.0f && r1 < 53.0f, "rubber band dampening curve mismatch");

    float r_neg = rubber_band_clamp(-100.0f, 600.0f);
    CHECK(fabsf(r1 + r_neg) < 0.001f, "rubber band curve must be symmetric for negative pulls");

    /* 3. 验证惯性动量衰减 (Fling) 单调收敛 */
    float v = 4000.0f;
    float total_distance = 0.0f;
    for (i32 frame = 0; frame < 200; frame++) {
        float step = step_fling(&v, 16);
        total_distance += step;
        if (v == 0.0f) break;
    }
    CHECK(v == 0.0f, "fling momentum must decelerate to 0");
    CHECK(total_distance > 800.0f && total_distance < 3000.0f, "fling total scroll distance out of physical bound");

    /* 4. 验证弹簧振子回弹 (Spring Back) 阻尼归零 */
    float disp = 60.0f; // 橡皮筋拉伸 60px
    float spring_v = 0.0f;
    bool settled = false;
    for (i32 frame = 0; frame < 60; frame++) {
        settled = step_spring(&disp, &spring_v, 16); // 16ms 逐帧仿真
        if (settled) break;
    }
    CHECK(settled == true, "spring back must settle within 60 frames (1 second)");
    CHECK(disp == 0.0f, "final spring displacement must rest at 0");

    printf("PASS: gui_touch_physics_test passed all checks.\n");
    return 0;
}
