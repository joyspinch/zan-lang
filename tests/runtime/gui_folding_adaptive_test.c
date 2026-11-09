/* Multitarget folding and hinge avoidance geometry regression test. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define CHECK(cond, message) do { if (!(cond)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); exit(1); \
} } while (0)

typedef int32_t i32;

typedef struct {
    i32 x, y, width, height;
    bool isVertical;
} HingeArea;

/* 模拟 HingeSplitPanel 物理折痕避让算法 */
static void calc_hinge_split(i32 px, i32 pw, HingeArea h, i32 pad,
                             i32 *left_w, i32 *right_x, i32 *right_w) {
    if (h.isVertical && h.width > 0 && h.x > px && h.x < px + pw) {
        *left_w = h.x - px - pad;
        if (*left_w < 0) *left_w = 0;
        *right_x = h.x + h.width + pad;
        *right_w = px + pw - *right_x;
        if (*right_w < 0) *right_w = 0;
    } else {
        *left_w = pw / 2;
        *right_x = px + *left_w;
        *right_w = pw - *left_w;
    }
}

/* 模拟 FoldMasterDetail 拓扑排布算法 */
typedef struct {
    i32 nav_w, master_w, detail_w;
    i32 nav_x, master_x, detail_x;
} FoldLayoutResult;

static void calc_fold_master_detail(i32 pw, i32 nav_cfg, i32 master_cfg, i32 active_page,
                                    FoldLayoutResult *res) {
    if (pw >= 1000) {
        /* 三折叠全展模式 */
        res->nav_w = nav_cfg;
        res->nav_x = 0;
        res->master_w = master_cfg;
        res->master_x = res->nav_w;
        res->detail_w = pw - res->nav_w - res->master_w;
        res->detail_x = res->master_x + res->master_w;
    } else if (pw >= 600) {
        /* 双屏阔折模式 */
        res->nav_w = 0;
        res->nav_x = 0;
        res->master_w = master_cfg;
        res->master_x = 0;
        res->detail_w = pw - res->master_w;
        res->detail_x = res->master_w;
    } else {
        /* 单屏合拢模式 */
        res->nav_w = 0;
        res->nav_x = 0;
        if (active_page == 1) {
            res->master_w = 0;
            res->master_x = 0;
            res->detail_w = pw;
            res->detail_x = 0;
        } else {
            res->master_w = pw;
            res->master_x = 0;
            res->detail_w = 0;
            res->detail_x = 0;
        }
    }
}

int main(void) {
    /* 1. 测试三折叠展开模式 (1140px) */
    FoldLayoutResult res3;
    calc_fold_master_detail(1140, 220, 340, 0, &res3);
    CHECK(res3.nav_w == 220, "tri-fold nav width mismatch");
    CHECK(res3.master_w == 340, "tri-fold master width mismatch");
    CHECK(res3.detail_w == 580, "tri-fold detail width mismatch (1140 - 220 - 340)");
    CHECK(res3.nav_x == 0 && res3.master_x == 220 && res3.detail_x == 560, "tri-fold columns not contiguous");

    /* 2. 测试双屏展开模式 (760px) */
    FoldLayoutResult res2;
    calc_fold_master_detail(760, 220, 340, 0, &res2);
    CHECK(res2.nav_w == 0, "dual-fold nav must be hidden");
    CHECK(res2.master_w == 340, "dual-fold master width mismatch");
    CHECK(res2.detail_w == 420, "dual-fold detail width mismatch (760 - 340)");
    CHECK(res2.master_x == 0 && res2.detail_x == 340, "dual-fold columns not contiguous");

    /* 3. 测试单屏合拢模式 (380px) 状态切换 */
    FoldLayoutResult res1_master, res1_detail;
    calc_fold_master_detail(380, 220, 340, 0, &res1_master);
    CHECK(res1_master.master_w == 380, "single-fold master must fill screen");
    CHECK(res1_master.detail_w == 0, "single-fold detail must be hidden in page 0");

    calc_fold_master_detail(380, 220, 340, 1, &res1_detail);
    CHECK(res1_detail.master_w == 0, "single-fold master must be hidden in page 1");
    CHECK(res1_detail.detail_w == 380, "single-fold detail must fill screen");

    /* 4. 测试物理折痕避让 (Hinge Avoidance) 算法 */
    HingeArea h = { .x = 390, .y = 0, .width = 20, .height = 1200, .isVertical = true };
    i32 left_w = 0, right_x = 0, right_w = 0;
    calc_hinge_split(0, 800, h, 8, &left_w, &right_x, &right_w);
    CHECK(left_w == 382, "left safe pane width should be 390 - 8 = 382");
    CHECK(right_x == 418, "right safe pane start should be 390 + 20 + 8 = 418");
    CHECK(right_w == 382, "right safe pane width should be 800 - 418 = 382");
    CHECK(right_x > left_w, "hinge gap must be isolated");

    printf("PASS: gui_folding_adaptive_test passed all checks.\n");
    return 0;
}
