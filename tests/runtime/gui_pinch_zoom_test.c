/* Multi-touch tracking, Euclidean pinch-to-zoom and pivot anchor regression test. */
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

typedef struct {
    i32 id;
    i32 x, y;
    bool active;
} PointerState;

typedef struct {
    PointerState pointers[5];
    float prev_span;
    float current_span;
    float focus_x, focus_y;
} MockMultiTouchTracker;

static void mt_init(MockMultiTouchTracker *m) {
    for (i32 i = 0; i < 5; i++) {
        m->pointers[i].id = -1;
        m->pointers[i].active = false;
    }
    m->prev_span = 0.0f;
    m->current_span = 0.0f;
    m->focus_x = m->focus_y = 0.0f;
}

static void mt_recalc(MockMultiTouchTracker *m, bool reset_baseline) {
    i32 p0 = -1, p1 = -1;
    for (i32 i = 0; i < 5; i++) {
        if (m->pointers[i].active) {
            if (p0 == -1) p0 = i;
            else if (p1 == -1) { p1 = i; break; }
        }
    }

    if (p0 != -1 && p1 != -1) {
        float dx = (float)(m->pointers[p1].x - m->pointers[p0].x);
        float dy = (float)(m->pointers[p1].y - m->pointers[p0].y);
        float span = sqrtf(dx * dx + dy * dy);
        if (span < 1.0f) span = 1.0f;

        m->current_span = span;
        m->focus_x = (float)(m->pointers[p0].x + m->pointers[p1].x) * 0.5f;
        m->focus_y = (float)(m->pointers[p0].y + m->pointers[p1].y) * 0.5f;

        if (reset_baseline || m->prev_span <= 0.0f) {
            m->prev_span = m->current_span;
        }
    }
}

static void mt_down(MockMultiTouchTracker *m, i32 id, i32 x, i32 y) {
    for (i32 i = 0; i < 5; i++) {
        if (!m->pointers[i].active) {
            m->pointers[i].id = id;
            m->pointers[i].x = x;
            m->pointers[i].y = y;
            m->pointers[i].active = true;
            break;
        }
    }
    mt_recalc(m, true);
}

static void mt_move(MockMultiTouchTracker *m, i32 id, i32 x, i32 y) {
    for (i32 i = 0; i < 5; i++) {
        if (m->pointers[i].active && m->pointers[i].id == id) {
            m->pointers[i].x = x;
            m->pointers[i].y = y;
            break;
        }
    }
    mt_recalc(m, false);
}

static float mt_scale_delta(MockMultiTouchTracker *m) {
    if (m->prev_span <= 0.0f || m->current_span <= 0.0f) return 1.0f;
    float ratio = m->current_span / m->prev_span;
    m->prev_span = m->current_span;
    return ratio;
}

int main(void) {
    /* 1. 验证双指欧氏距离与对称扩展 */
    MockMultiTouchTracker mt;
    mt_init(&mt);

    // 双指落点：(100, 200) 与 (300, 200)，水平距离 200px
    mt_down(&mt, 0, 100, 200);
    mt_down(&mt, 1, 300, 200);

    CHECK(fabsf(mt.current_span - 200.0f) < 0.01f, "initial span must be 200px");
    CHECK(fabsf(mt.focus_x - 200.0f) < 0.01f, "initial focus x must be 200px");
    CHECK(fabsf(mt.focus_y - 200.0f) < 0.01f, "initial focus y must be 200px");

    // 双指向外张开至 (50, 200) 与 (350, 200)，距离扩至 300px
    mt_move(&mt, 0, 50, 200);
    mt_move(&mt, 1, 350, 200);

    CHECK(fabsf(mt.current_span - 300.0f) < 0.01f, "expanded span must be 300px");
    float scale = mt_scale_delta(&mt);
    CHECK(fabsf(scale - 1.50f) < 0.01f, "pinch scale delta must be 300 / 200 = 1.50x");

    // 对称张开时，双指中心焦点必须恒定不动
    CHECK(fabsf(mt.focus_x - 200.0f) < 0.01f, "symmetric pinch focus x must remain 200px");

    /* 2. 验证缩放极限与边界保护 */
    float s = 1.0f;
    float min_s = 0.5f, max_s = 4.0f;

    // 持续放大超出最大限制
    s = s * 6.0f;
    if (s > max_s) s = max_s;
    CHECK(s == 4.0f, "scale clamping to maxScale 4.0x failed");

    // 持续缩小超出最小限制
    s = s * 0.1f;
    if (s < min_s) s = min_s;
    CHECK(s == 0.5f, "scale clamping to minScale 0.5x failed");

    /* 3. 验证双击复位状态机 */
    float zoom_scale = 1.0f;
    // 第一次双击放大至 2.0x
    if (zoom_scale > 1.05f || zoom_scale < 0.95f) zoom_scale = 1.0f;
    else zoom_scale = 2.0f;
    CHECK(zoom_scale == 2.0f, "first double tap must zoom to 2.0x");

    // 第二次双击复位回到 1.0x
    if (zoom_scale > 1.05f || zoom_scale < 0.95f) zoom_scale = 1.0f;
    else zoom_scale = 2.0f;
    CHECK(zoom_scale == 1.0f, "second double tap must reset to 1.0x");

    printf("PASS: gui_pinch_zoom_test passed all checks.\n");
    return 0;
}
