#ifndef ZAN_GUI_TOUCH_GAME_H
#define ZAN_GUI_TOUCH_GAME_H

#include <math.h>
#include <string.h>

/* 编译器代码生成与运行时系统底层调用契约 */
enum {
    ZAN_TG_DOWN, ZAN_TG_MOVE, ZAN_TG_UP, ZAN_TG_CANCEL,
    ZAN_TG_POINTER_DOWN, ZAN_TG_POINTER_UP
};
enum { ZAN_TG_IDLE, ZAN_TG_SINGLE, ZAN_TG_PAIR, ZAN_TG_SUPPRESS };
#define ZAN_TG_SLOP2 64.0f
#define ZAN_TG_CTRL 1

typedef struct { int id; float x, y; } zan_tg_pointer;
typedef void (*zan_tg_emit)(void *ctx, const int event[8]);
typedef struct {
    int phase, first_id, second_id, dragging;
    float anchor_x, anchor_y, x, y, distance, wheel_remainder;
} zan_touch_game;

static inline void zan_tg_event(zan_tg_emit emit, void *ctx, int kind,
                                float x, float y, int code, int mods, int flag) {
    int event[8] = { kind, (int)x, (int)y, 0, code, mods, flag, 0 };
    emit(ctx, event);
}

/* 编译器代码生成与运行时系统底层调用契约 */
static inline int zan_tg_coalesce(int last[8], const int event[8]) {
    if (last[0] != event[0] || last[3] != event[3] ||
        last[5] != event[5] || last[6] != event[6] || last[7] != event[7]) return 0;
    if (event[0] != 1 && event[0] != 13) return 0;
    /* 编译器代码生成与运行时系统底层调用契约 */
    if (event[0] == 1 && event[6] == 2) return 0;
    last[1] = event[1]; last[2] = event[2];
    if (event[0] == 13) last[4] += event[4];
    return 1;
}

static inline const zan_tg_pointer *zan_tg_find(const zan_tg_pointer *p,
                                               int count, int id) {
    for (int i = 0; i < count; ++i) if (p[i].id == id) return &p[i];
    return NULL;
}

static inline void zan_tg_cancel(zan_touch_game *g, int phase,
                                 zan_tg_emit emit, void *ctx) {
    if (g->phase == ZAN_TG_SINGLE || g->phase == ZAN_TG_PAIR)
        zan_tg_event(emit, ctx, 3, g->x, g->y, 0, 0, 1);
    memset(g, 0, sizeof(*g));
    g->phase = phase;
}

static inline void zan_tg_single_move(zan_touch_game *g, const zan_tg_pointer *p,
                                      zan_tg_emit emit, void *ctx) {
    float dx = p->x - g->anchor_x, dy = p->y - g->anchor_y;
    g->x = p->x; g->y = p->y;
    if (dx * dx + dy * dy >= ZAN_TG_SLOP2) g->dragging = 1;
    if (g->dragging) zan_tg_event(emit, ctx, 1, g->x, g->y, 0, 0, 1);
}

static inline void zan_tg_pair_position(zan_touch_game *g,
                                        const zan_tg_pointer *a,
                                        const zan_tg_pointer *b) {
    g->x = (a->x + b->x) * 0.5f;
    g->y = (a->y + b->y) * 0.5f;
    g->distance = hypotf(b->x - a->x, b->y - a->y);
}

static inline void zan_touch_game_feed(zan_touch_game *g, int action,
                                       int changed_id, const zan_tg_pointer *p,
                                       int count, zan_tg_emit emit, void *ctx) {
    if (action == ZAN_TG_CANCEL) {
        zan_tg_cancel(g, ZAN_TG_IDLE, emit, ctx);
        return;
    }
    if (action == ZAN_TG_DOWN) {
        /* 模块核心语义抽象与接口调用契约 */
        zan_tg_cancel(g, ZAN_TG_IDLE, emit, ctx);
        const zan_tg_pointer *a = zan_tg_find(p, count, changed_id);
        if (!a || count != 1) { g->phase = ZAN_TG_SUPPRESS; return; }
        g->phase = ZAN_TG_SINGLE; g->first_id = a->id;
        g->x = g->anchor_x = a->x; g->y = g->anchor_y = a->y;
        zan_tg_event(emit, ctx, 1, g->x, g->y, 0, 0, 0);
        zan_tg_event(emit, ctx, 2, g->x, g->y, 0, 0, 0);
        return;
    }
    if (g->phase == ZAN_TG_IDLE) return;
    if (g->phase == ZAN_TG_SUPPRESS) {
        if (action == ZAN_TG_UP || count == 0 ||
            (action == ZAN_TG_POINTER_UP && count == 1)) g->phase = ZAN_TG_IDLE;
        return;
    }

    const zan_tg_pointer *a = zan_tg_find(p, count, g->first_id);
    if (!a) {
        zan_tg_cancel(g, count ? ZAN_TG_SUPPRESS : ZAN_TG_IDLE, emit, ctx);
        return;
    }
    if (g->phase == ZAN_TG_SINGLE) {
        if (action == ZAN_TG_POINTER_DOWN) {
            const zan_tg_pointer *b = zan_tg_find(p, count, changed_id);
            if (!b || b->id == a->id) {
                zan_tg_cancel(g, ZAN_TG_SUPPRESS, emit, ctx);
                return;
            }
            zan_tg_event(emit, ctx, 3, a->x, a->y, 0, 0, 1);
            g->phase = ZAN_TG_PAIR; g->second_id = b->id;
            g->wheel_remainder = 0;
            zan_tg_pair_position(g, a, b);
            zan_tg_event(emit, ctx, 1, g->x, g->y, 0, 0, 2);
        } else if (action == ZAN_TG_UP && changed_id == g->first_id) {
            /* 模块核心语义抽象与接口调用契约 */
            zan_tg_single_move(g, a, emit, ctx);
            zan_tg_event(emit, ctx, 3,
                         g->dragging ? g->x : g->anchor_x,
                         g->dragging ? g->y : g->anchor_y, 0, 0, g->dragging);
            memset(g, 0, sizeof(*g));
        } else if (action == ZAN_TG_MOVE && count == 1) {
            zan_tg_single_move(g, a, emit, ctx);
        } else {
            /* 底层系统交互与数据协议契约 */
            zan_tg_cancel(g, ZAN_TG_SUPPRESS, emit, ctx);
        }
        return;
    }

    const zan_tg_pointer *b = zan_tg_find(p, count, g->second_id);
    if (!b) {
        zan_tg_cancel(g, count ? ZAN_TG_SUPPRESS : ZAN_TG_IDLE, emit, ctx);
        return;
    }
    if (action == ZAN_TG_UP ||
        (action == ZAN_TG_POINTER_UP &&
         (changed_id == g->first_id || changed_id == g->second_id))) {
        zan_tg_pair_position(g, a, b);
        zan_tg_cancel(g, action == ZAN_TG_UP ? ZAN_TG_IDLE : ZAN_TG_SUPPRESS,
                      emit, ctx);
    } else if (action == ZAN_TG_MOVE) {
        float previous = g->distance;
        zan_tg_pair_position(g, a, b);
        zan_tg_event(emit, ctx, 1, g->x, g->y, 0, 0, 2);
        /* 编译器代码生成与运行时系统底层调用契约 */
        if (previous > 0 && g->distance > 0) {
            g->wheel_remainder += 120.0f * log2f(g->distance / previous);
            int delta = (int)g->wheel_remainder;
            if (delta) {
                g->wheel_remainder -= (float)delta;
                zan_tg_event(emit, ctx, 13, g->x, g->y, delta, ZAN_TG_CTRL, 0);
            }
        } else {
            g->wheel_remainder = 0;
        }
    }
    /* 编译器代码生成与运行时系统底层调用契约 */
}

#endif
