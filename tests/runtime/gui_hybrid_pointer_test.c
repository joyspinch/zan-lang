/* Hybrid pointer arbitration, TouchSlop, HitSlop, and Sticky Hover regression test. */
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

typedef enum {
    POINTER_MOUSE = 0,
    POINTER_TOUCH = 1,
    POINTER_PEN   = 2
} PointerDeviceType;

typedef struct {
    i32 bx, by, bw, bh;
    i32 hit_slop_t, hit_slop_r, hit_slop_b, hit_slop_l;
    i32 min_touch_size; // 默认 44px
    bool visible;
} MockControl;

static void ctl_init(MockControl *c, i32 x, i32 y, i32 w, i32 h) {
    c->bx = x; c->by = y; c->bw = w; c->bh = h;
    c->hit_slop_t = c->hit_slop_r = c->hit_slop_b = c->hit_slop_l = 0;
    c->min_touch_size = 44;
    c->visible = true;
}

static bool ctl_contains_point(const MockControl *c, i32 px, i32 py, PointerDeviceType ptype) {
    if (!c->visible || c->bw <= 0 || c->bh <= 0) return false;

    if (ptype == POINTER_TOUCH) {
        i32 slop_t = c->hit_slop_t;
        i32 slop_r = c->hit_slop_r;
        i32 slop_b = c->hit_slop_b;
        i32 slop_l = c->hit_slop_l;
        i32 min_sz = c->min_touch_size > 0 ? c->min_touch_size : 44;

        if (slop_t == 0 && slop_b == 0 && c->bh < min_sz) {
            i32 pad_h = (min_sz - c->bh) / 2;
            slop_t = pad_h;
            slop_b = pad_h;
        }
        if (slop_l == 0 && slop_r == 0 && c->bw < min_sz) {
            i32 pad_w = (min_sz - c->bw) / 2;
            slop_l = pad_w;
            slop_r = pad_w;
        }
        return px >= (c->bx - slop_l) && px < (c->bx + c->bw + slop_r) &&
               py >= (c->by - slop_t) && py < (c->by + c->bh + slop_b);
    }

    // 鼠标高精度 1px 像素级命中
    return px >= c->bx && px < (c->bx + c->bw) &&
           py >= c->by && py < (c->by + c->bh);
}

typedef struct {
    PointerDeviceType pointer_type;
    i32 touch_down_x, touch_down_y;
    bool touch_slop_exceeded;
    bool click_emitted;
    bool cancel_emitted;
    bool hover_active;
    i32 target_id;
} MockVisualTreePipeline;

static void pipeline_init(MockVisualTreePipeline *p, PointerDeviceType ptype) {
    p->pointer_type = ptype;
    p->touch_down_x = p->touch_down_y = 0;
    p->touch_slop_exceeded = false;
    p->click_emitted = false;
    p->cancel_emitted = false;
    p->hover_active = false;
    p->target_id = -1;
}

static void pipeline_down(MockVisualTreePipeline *p, i32 target_id, i32 x, i32 y) {
    p->target_id = target_id;
    p->hover_active = true;
    p->click_emitted = false;
    p->cancel_emitted = false;
    if (p->pointer_type == POINTER_TOUCH) {
        p->touch_down_x = x;
        p->touch_down_y = y;
        p->touch_slop_exceeded = false;
    }
}

static void pipeline_move(MockVisualTreePipeline *p, i32 x, i32 y) {
    if (p->pointer_type == POINTER_TOUCH) {
        if (p->target_id != -1 && !p->touch_slop_exceeded) {
            i32 dx = x - p->touch_down_x;
            i32 dy = y - p->touch_down_y;
            // 8px 防抖阈值 (8 * 8 = 64)
            if (dx * dx + dy * dy >= 64) {
                p->touch_slop_exceeded = true;
                p->cancel_emitted = true; // 发送 PointerCancel
            }
        }
    }
}

static void pipeline_up(MockVisualTreePipeline *p, i32 release_target_id) {
    bool allow_click = true;
    if (p->pointer_type == POINTER_TOUCH && p->touch_slop_exceeded) {
        allow_click = false; // 触控滑动突破防抖死区，禁止误触发点击
    }

    if (allow_click && p->target_id == release_target_id && p->target_id != -1) {
        p->click_emitted = true;
    }

    if (p->pointer_type == POINTER_TOUCH) {
        // 触控抬手：清除悬停态，根除 Sticky Hover
        p->hover_active = false;
    }
    p->target_id = -1;
}

int main(void) {
    /* 1. 验证 16x16px 小图标控件在鼠标与触控模式下的命中对比 */
    MockControl icon_btn;
    ctl_init(&icon_btn, 100, 100, 16, 16);

    // 点 (105, 105) 位于物理矩形内部
    CHECK(ctl_contains_point(&icon_btn, 105, 105, POINTER_MOUSE), "mouse hits inside 16x16 box");
    CHECK(ctl_contains_point(&icon_btn, 105, 105, POINTER_TOUCH), "touch hits inside 16x16 box");

    // 点 (95, 95) 位于物理矩形外部 5px
    // 鼠标模式下严格为 false
    CHECK(!ctl_contains_point(&icon_btn, 95, 95, POINTER_MOUSE), "mouse must not hit outside 16x16 box");
    // 触控模式下自动外扩至 44x44px（四边外扩 14px，热区为 [86, 86, 44, 44]），必须命中！
    CHECK(ctl_contains_point(&icon_btn, 95, 95, POINTER_TOUCH), "touch must hit 95,95 via 44px min target expansion");

    // 点 (80, 80) 超出 44px 热区，必须未命中
    CHECK(!ctl_contains_point(&icon_btn, 80, 80, POINTER_TOUCH), "touch must not hit outside 44px target");

    /* 2. 验证触控防抖与微颤（Jitter within 8px slop）-> 正常触发 Click */
    MockVisualTreePipeline touch_pipeline;
    pipeline_init(&touch_pipeline, POINTER_TOUCH);

    pipeline_down(&touch_pipeline, 42, 200, 200);
    CHECK(touch_pipeline.hover_active, "touch down activates hover feedback");

    // 手指微颤移动 4px, 4px (dx^2 + dy^2 = 16 + 16 = 32 < 64)
    pipeline_move(&touch_pipeline, 204, 204);
    CHECK(!touch_pipeline.touch_slop_exceeded, "4px jitter must not break touch slop");
    CHECK(!touch_pipeline.cancel_emitted, "4px jitter must not emit PointerCancel");

    // 松手，正常触发点击
    pipeline_up(&touch_pipeline, 42);
    CHECK(touch_pipeline.click_emitted, "micro-jitter tap must emit Click");
    CHECK(!touch_pipeline.hover_active, "touch up must eliminate sticky hover");

    /* 3. 验证触控滑动（Swipe exceeding 8px slop）-> 升级为滚动手势并取消点击 */
    pipeline_init(&touch_pipeline, POINTER_TOUCH);
    pipeline_down(&touch_pipeline, 42, 200, 200);

    // 手指滑移 10px, 10px (dx^2 + dy^2 = 100 + 100 = 200 >= 64)
    pipeline_move(&touch_pipeline, 210, 210);
    CHECK(touch_pipeline.touch_slop_exceeded, "10px movement must break touch slop");
    CHECK(touch_pipeline.cancel_emitted, "touch slop break must emit PointerCancel");

    // 松手，严禁触发点击！
    pipeline_up(&touch_pipeline, 42);
    CHECK(!touch_pipeline.click_emitted, "touch swipe must never emit Click");
    CHECK(!touch_pipeline.hover_active, "touch up must eliminate sticky hover after swipe");

    /* 4. 验证桌面鼠标模式下的持续 Hover 与 1px 点击精准度 */
    MockVisualTreePipeline mouse_pipeline;
    pipeline_init(&mouse_pipeline, POINTER_MOUSE);

    pipeline_down(&mouse_pipeline, 99, 300, 300);
    pipeline_move(&mouse_pipeline, 350, 350); // 鼠标大幅移动
    pipeline_up(&mouse_pipeline, 99);
    CHECK(mouse_pipeline.click_emitted, "mouse click allows intentional mouse drag-release");
    CHECK(mouse_pipeline.hover_active, "mouse maintains hover state after release");

    printf("PASS: gui_hybrid_pointer_test passed all checks.\n");
    return 0;
}
