/* High-resolution dirty rect merging and sub-frame performance regression test. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define CHECK(cond, message) do { if (!(cond)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); exit(1); \
} } while (0)

typedef int32_t i32;
typedef int64_t i64;

typedef struct {
    i32 view_w, view_h;
    i32 dmg_x, dmg_y, dmg_w, dmg_h;
    bool partial_pending;
} DamageState;

static void damage_init(DamageState *s, i32 vw, i32 vh) {
    s->view_w = vw;
    s->view_h = vh;
    s->dmg_x = 0; s->dmg_y = 0;
    s->dmg_w = 0; s->dmg_h = 0;
    s->partial_pending = false;
}

static void note_damage(DamageState *s, i32 x, i32 y, i32 w, i32 h) {
    if (w <= 0 || h <= 0) return;
    i32 x0 = x, y0 = y;
    i32 x1 = x + w, y1 = y + h;

    if (s->partial_pending) {
        if (s->dmg_x < x0) x0 = s->dmg_x;
        if (s->dmg_y < y0) y0 = s->dmg_y;
        if (s->dmg_x + s->dmg_w > x1) x1 = s->dmg_x + s->dmg_w;
        if (s->dmg_y + s->dmg_h > y1) y1 = s->dmg_y + s->dmg_h;
    }

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->view_w) x1 = s->view_w;
    if (y1 > s->view_h) y1 = s->view_h;
    if (x1 - x0 <= 0 || y1 - y0 <= 0) {
        s->partial_pending = false;
        return;
    }

    i32 dw = x1 - x0;
    i32 dh = y1 - y0;

    /* 面积防溢出保护：超过视口 60% 平滑退回全量重绘 */
    i64 total_area = (i64)s->view_w * (i64)s->view_h;
    if (total_area > 0 && (i64)dw * (i64)dh * 10 > total_area * 6) {
        s->partial_pending = false;
        return;
    }

    s->dmg_x = x0;
    s->dmg_y = y0;
    s->dmg_w = dw;
    s->dmg_h = dh;
    s->partial_pending = true;
}

static void control_invalidate(DamageState *s, i32 bx, i32 by, i32 bw, i32 bh, i32 shadow) {
    i32 outset = shadow > 0 ? 8 : 2;
    note_damage(s, bx - outset, by - outset, bw + outset * 2, bh + outset * 2);
}

int main(void) {
    /* 1. 模拟三折叠 3K 视口 (2880 x 2232) */
    DamageState ds;
    damage_init(&ds, 2880, 2232);

    /* 2. 单个 200x60 按钮触发局部 Invalidate (outset=2) */
    control_invalidate(&ds, 500, 800, 200, 60, 0);
    CHECK(ds.partial_pending == true, "partial frame should be pending");
    CHECK(ds.dmg_x == 498, "damage x should be 500 - 2 = 498");
    CHECK(ds.dmg_y == 798, "damage y should be 800 - 2 = 798");
    CHECK(ds.dmg_w == 204, "damage width should be 200 + 4 = 204");
    CHECK(ds.dmg_h == 64, "damage height should be 60 + 4 = 64");

    i64 dmg_pixels = (i64)ds.dmg_w * (i64)ds.dmg_h;
    i64 full_pixels = (i64)ds.view_w * (i64)ds.view_h;
    /* 断言重绘工作量低于全屏的 1% */
    CHECK(dmg_pixels * 100 < full_pixels, "dirty rect must save >99% fillrate on 3K screen");

    /* 3. 临近控件追加局部标脏：包围盒无缝吸收 */
    control_invalidate(&ds, 650, 820, 100, 40, 0);
    CHECK(ds.partial_pending == true, "merged damage should remain partial");
    CHECK(ds.dmg_x == 498, "merged min x mismatch");
    CHECK(ds.dmg_w == (650 + 100 + 2) - 498, "merged max width mismatch");

    /* 4. 极端超大区域标脏（如整屏翻页，占屏幕 80%）：自动回退全量帧，取消局部裁剪 */
    damage_init(&ds, 2880, 2232);
    note_damage(&ds, 0, 0, 2600, 2000);
    CHECK(ds.partial_pending == false, "damage >60% must fallback to full frame");

    printf("PASS: gui_dirty_rect_engine_test passed all checks.\n");
    return 0;
}
