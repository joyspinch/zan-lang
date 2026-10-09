/* 模块核心语义抽象与接口调用契约 */
#include <stdio.h>
#include <stdlib.h>
#include "../../src/runtime/gui_touch_game.h"

typedef struct {
    zan_touch_game game;
    int events[256][8];
    int count;
} fixture;
static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #cond); \
        ++failures; \
    } \
} while (0)

static void record(void *ctx, const int event[8]) {
    fixture *f = ctx;
    if (f->count == 256) { fprintf(stderr, "test event overflow\n"); exit(2); }
    memcpy(f->events[f->count++], event, sizeof(f->events[0]));
}
static void feed(fixture *f, int action, int id, const zan_tg_pointer *p, int n) {
    zan_touch_game_feed(&f->game, action, id, p, n, record, f);
}
static void expect_event(fixture *f, int index, int kind, int x, int y,
                         int code, int mods, int flag) {
    int expected[8] = { kind, x, y, 0, code, mods, flag, 0 };
    CHECK(index < f->count);
    if (index < f->count) CHECK(memcmp(f->events[index], expected, sizeof(expected)) == 0);
}
static void start_pair(fixture *f) {
    zan_tg_pointer p[] = { { 7, 0, 0 }, { 23, 100, 0 } };
    feed(f, ZAN_TG_DOWN, 7, p, 1);
    feed(f, ZAN_TG_POINTER_DOWN, 23, p, 2);
}

static void test_tap_and_slop(void) {
    fixture f = {0};
    zan_tg_pointer p = { 19, 40, 60 };
    feed(&f, ZAN_TG_DOWN, 19, &p, 1);
    expect_event(&f, 0, 1, 40, 60, 0, 0, 0);
    expect_event(&f, 1, 2, 40, 60, 0, 0, 0);
    p.x += 7;
    feed(&f, ZAN_TG_MOVE, -1, &p, 1);
    CHECK(f.count == 2);
    feed(&f, ZAN_TG_UP, 19, &p, 1);
    CHECK(f.count == 3 && f.game.phase == ZAN_TG_IDLE);
    expect_event(&f, 2, 3, 40, 60, 0, 0, 0);

    f.count = 0;
    p = (zan_tg_pointer){ 19, 40, 60 };
    feed(&f, ZAN_TG_DOWN, 19, &p, 1);
    p.x += 8;
    feed(&f, ZAN_TG_MOVE, -1, &p, 1);
    expect_event(&f, 2, 1, 48, 60, 0, 0, 1);
    /* 模块核心语义抽象与接口调用契约 */
    p.x = 40;
    feed(&f, ZAN_TG_UP, 19, &p, 1);
    CHECK(f.count == 5 && f.game.phase == ZAN_TG_IDLE);
    expect_event(&f, 3, 1, 40, 60, 0, 0, 1);
    expect_event(&f, 4, 3, 40, 60, 0, 0, 1);
}

static void test_single_drag_no_scroll(void) {
    fixture f = {0};
    zan_tg_pointer p = { 3, 10, 20 };
    feed(&f, ZAN_TG_DOWN, 3, &p, 1);
    p.x = 90; p.y = 120;
    feed(&f, ZAN_TG_MOVE, -1, &p, 1);
    expect_event(&f, 2, 1, 90, 120, 0, 0, 1);
    p.x = 100; p.y = 140;
    feed(&f, ZAN_TG_UP, 3, &p, 1);
    expect_event(&f, 3, 1, 100, 140, 0, 0, 1);
    expect_event(&f, 4, 3, 100, 140, 0, 0, 1);
    CHECK(f.count == 5);
    for (int i = 0; i < f.count; ++i) CHECK(f.events[i][0] != 13);

    /* 模块核心语义抽象与接口调用契约 */
    f.count = 0;
    feed(&f, ZAN_TG_DOWN, 3, &p, 1);
    p.y += 20;
    feed(&f, ZAN_TG_UP, 3, &p, 1);
    CHECK(f.count == 4);
    expect_event(&f, 3, 3, 100, 160, 0, 0, 1);
}

static void test_pair_ids_pan_and_zoom(void) {
    fixture f = {0};
    start_pair(&f);
    CHECK(f.count == 4);
    expect_event(&f, 2, 3, 0, 0, 0, 0, 1);
    expect_event(&f, 3, 1, 50, 0, 0, 0, 2);
    /* 模块核心语义抽象与接口调用契约 */
    zan_tg_pointer p[] = { { 23, 120, 30 }, { 7, 20, 30 } };
    feed(&f, ZAN_TG_MOVE, -1, p, 2);
    CHECK(f.count == 5);
    expect_event(&f, 4, 1, 70, 30, 0, 0, 2);
    p[0].x = 170; p[1].x = -30;
    feed(&f, ZAN_TG_MOVE, -1, p, 2);
    expect_event(&f, 5, 1, 70, 30, 0, 0, 2);
    expect_event(&f, 6, 13, 70, 30, 120, 1, 0);
    p[0].x = 120; p[1].x = 20;
    feed(&f, ZAN_TG_MOVE, -1, p, 2);
    expect_event(&f, 8, 13, 70, 30, -120, 1, 0);
    CHECK(f.count == 9);
    for (int i = 2; i < f.count; ++i) {
        CHECK(f.events[i][0] != 2);
        CHECK(f.events[i][3] == 0);
    }
}

static void test_drag_to_pair_and_partial_stream(void) {
    fixture f = {0};
    zan_tg_pointer p[] = { { 17, 100, 80 }, { 9, 200, 120 } };
    feed(&f, ZAN_TG_DOWN, 17, p, 1);
    p[0].x = 120; p[0].y = 100;
    feed(&f, ZAN_TG_MOVE, -1, p, 1);
    feed(&f, ZAN_TG_POINTER_DOWN, 9, p, 2);
    expect_event(&f, 3, 3, 120, 100, 0, 0, 1);
    expect_event(&f, 4, 1, 160, 110, 0, 0, 2);
    /* 模块核心语义抽象与接口调用契约 */
    feed(&f, ZAN_TG_POINTER_UP, 9, p, 2);
    expect_event(&f, 5, 3, 160, 110, 0, 0, 1);
    feed(&f, ZAN_TG_MOVE, -1, p, 1);
    feed(&f, ZAN_TG_UP, 17, p, 1);
    CHECK(f.count == 6 && f.game.phase == ZAN_TG_IDLE);

    /* 模块核心语义抽象与接口调用契约 */
    f.count = 0;
    feed(&f, ZAN_TG_MOVE, -1, p, 1);
    feed(&f, ZAN_TG_POINTER_DOWN, 9, p, 2);
    CHECK(f.count == 0);
    feed(&f, ZAN_TG_DOWN, 17, p, 1);
    feed(&f, ZAN_TG_MOVE, -1, p, 2); /* 核心系统底层抽象与内存语义契约 */
    expect_event(&f, 2, 3, 120, 100, 0, 0, 1);
    CHECK(f.count == 3 && f.game.phase == ZAN_TG_SUPPRESS);
    feed(&f, ZAN_TG_CANCEL, -1, NULL, 0);
    CHECK(f.count == 3 && f.game.phase == ZAN_TG_IDLE);
}

static void test_extra_finger_and_residual_suppression(void) {
    /* 模块核心语义抽象与接口调用契约 */
    for (int lifted = 0; lifted < 2; ++lifted) {
        fixture f = {0};
        start_pair(&f);
        zan_tg_pointer p[] = { { 31, 900, 800 }, { 23, 100, 0 }, { 7, 0, 0 } };
        feed(&f, ZAN_TG_POINTER_DOWN, 31, p, 3);
        feed(&f, ZAN_TG_POINTER_UP, 31, p, 3);
        CHECK(f.count == 4);
        feed(&f, ZAN_TG_POINTER_DOWN, 31, p, 3);
        feed(&f, ZAN_TG_MOVE, -1, p, 3);
        expect_event(&f, 4, 1, 50, 0, 0, 0, 2);
        int lift_id = lifted ? 23 : 7;
        feed(&f, ZAN_TG_POINTER_UP, lift_id, p, 3);
        expect_event(&f, 5, 3, 50, 0, 0, 0, 1);
        CHECK(f.game.phase == ZAN_TG_SUPPRESS);
        int survivor = lifted ? 7 : 23;
        zan_tg_pointer rest[] = { { survivor, 200, 100 }, { 31, 600, 700 } };
        feed(&f, ZAN_TG_MOVE, -1, rest, 2);
        feed(&f, ZAN_TG_POINTER_DOWN, 31, rest, 2);
        feed(&f, ZAN_TG_POINTER_UP, 31, rest, 2);
        feed(&f, ZAN_TG_MOVE, -1, rest, 1);
        feed(&f, ZAN_TG_UP, survivor, rest, 1);
        CHECK(f.count == 6 && f.game.phase == ZAN_TG_IDLE);
        feed(&f, ZAN_TG_DOWN, survivor, rest, 1);
        feed(&f, ZAN_TG_UP, survivor, rest, 1);
        CHECK(f.count == 9);
        expect_event(&f, 8, 3, 200, 100, 0, 0, 0);
    }
}

static void test_cancel_missing_id_and_restart(void) {
    fixture f = {0};
    zan_tg_pointer p = { 5, 30, 40 };
    feed(&f, ZAN_TG_DOWN, 5, &p, 1);
    feed(&f, ZAN_TG_CANCEL, -1, NULL, 0);
    expect_event(&f, 2, 3, 30, 40, 0, 0, 1);
    feed(&f, ZAN_TG_MOVE, -1, &p, 1);
    feed(&f, ZAN_TG_UP, 5, &p, 1);
    feed(&f, ZAN_TG_CANCEL, -1, NULL, 0);
    CHECK(f.count == 3 && f.game.phase == ZAN_TG_IDLE);

    f.count = 0;
    start_pair(&f);
    feed(&f, ZAN_TG_CANCEL, -1, NULL, 0);
    expect_event(&f, 4, 3, 50, 0, 0, 0, 1);
    CHECK(f.game.phase == ZAN_TG_IDLE);
    f.count = 0;
    start_pair(&f);
    feed(&f, ZAN_TG_MOVE, -1, &p, 1); /* 核心系统底层抽象与内存语义契约 */
    expect_event(&f, 4, 3, 50, 0, 0, 0, 1);
    CHECK(f.game.phase == ZAN_TG_SUPPRESS);
    feed(&f, ZAN_TG_UP, 5, &p, 1);
    CHECK(f.count == 5 && f.game.phase == ZAN_TG_IDLE);

    /* 模块核心语义抽象与接口调用契约 */
    f.count = 0;
    feed(&f, ZAN_TG_DOWN, 5, &p, 1);
    p.id = 29; p.x = 80;
    feed(&f, ZAN_TG_DOWN, 29, &p, 1);
    expect_event(&f, 2, 3, 30, 40, 0, 0, 1);
    expect_event(&f, 4, 2, 80, 40, 0, 0, 0);
    CHECK(f.count == 5);
}

static void test_fractional_and_zero_distance(void) {
    fixture f = {0};
    start_pair(&f);
    f.count = 0;
    zan_tg_pointer p[] = { { 7, 0, 0 }, { 23, 100, 0 } };
    /* 模块核心语义抽象与接口调用契约 */
    for (int i = 1; i <= 100; ++i) {
        p[1].x = 100.0f + (float)i / 10.0f;
        feed(&f, ZAN_TG_MOVE, -1, p, 2);
    }
    int sum = 0;
    for (int i = 0; i < f.count; ++i) if (f.events[i][0] == 13) sum += f.events[i][4];
    CHECK(sum == 16);
    CHECK(f.game.wheel_remainder > 0 && f.game.wheel_remainder < 1);
    p[1].x = 100;
    feed(&f, ZAN_TG_MOVE, -1, p, 2);
    sum = 0;
    for (int i = 0; i < f.count; ++i) if (f.events[i][0] == 13) sum += f.events[i][4];
    CHECK(abs(sum) <= 1);

    f.count = 0;
    p[1].x = 0;
    feed(&f, ZAN_TG_MOVE, -1, p, 2);
    p[1].x = 20;
    feed(&f, ZAN_TG_MOVE, -1, p, 2);
    CHECK(f.count == 2 && f.game.wheel_remainder == 0);
    p[1].x = 40;
    feed(&f, ZAN_TG_MOVE, -1, p, 2);
    CHECK(f.count == 4);
    expect_event(&f, 3, 13, 20, 0, 120, 1, 0);
}

static void test_event_coalescing(void) {
    int last[8] = { 13, 10, 20, 0, 30, 0, 0, 0 };
    int wheel[8] = { 13, 40, 50, 0, -10, 0, 0, 0 };
    CHECK(zan_tg_coalesce(last, wheel));
    CHECK(last[1] == 40 && last[2] == 50 && last[4] == 20);
    wheel[5] = 1;
    CHECK(!zan_tg_coalesce(last, wheel));
    CHECK(last[4] == 20 && last[5] == 0);
    last[5] = 1;
    CHECK(zan_tg_coalesce(last, wheel));
    CHECK(last[4] == 10 && last[5] == 1);

    int move[8] = { 1, 30, 40, 0, 0, 0, 0, 0 };
    memcpy(last, move, sizeof(last));
    move[1] = 99;
    CHECK(zan_tg_coalesce(last, move));
    CHECK(last[1] == 99);
    move[6] = 1;
    CHECK(!zan_tg_coalesce(last, move));
    last[6] = 1;
    CHECK(zan_tg_coalesce(last, move));
    move[3] = 2;
    CHECK(!zan_tg_coalesce(last, move));
    move[3] = 0; move[6] = 2;
    CHECK(!zan_tg_coalesce(last, move));
    last[6] = 2;
    CHECK(!zan_tg_coalesce(last, move)); /* 核心系统底层抽象与内存语义契约 */
    last[0] = move[0] = 3;
    CHECK(!zan_tg_coalesce(last, move));
}

int main(void) {
    test_tap_and_slop();
    test_single_drag_no_scroll();
    test_pair_ids_pan_and_zoom();
    test_drag_to_pair_and_partial_stream();
    test_extra_finger_and_residual_suppression();
    test_cancel_missing_id_and_restart();
    test_fractional_and_zero_distance();
    test_event_coalescing();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    puts("gui_touch_game: 8 test groups passed");
    return 0;
}
