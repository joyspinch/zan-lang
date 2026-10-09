/* gui_backend */

#ifndef ZAN_GUI_BACKEND_H
#define ZAN_GUI_BACKEND_H

#include <stdint.h>
#include "zan_bitmap.h"

struct zan_surface_s;

/* 底层系统交互与数据协议契约 */
#define ZAN_CORNERS_ALL 15

/* 底层系统交互与数据协议契约 */
#define ZAN_GRAD_VERTICAL      0  /* top -> bottom */
#define ZAN_GRAD_HORIZONTAL    1  /* left -> right */
#define ZAN_GRAD_TO_BOTTOM_RIGHT 2
#define ZAN_GRAD_TO_BOTTOM_LEFT  3

/* 内部辅助逻辑 */
typedef struct zan_glyph_tile_s {
    uint32_t id;        /* 核心系统底层抽象与内存语义契约 */
    uint32_t rev;       /* 底层系统交互与数据协议契约 */
    int w, h;
    /* 内部辅助逻辑 */
    int left, top;
    int advance;        /* 核心系统底层抽象与内存语义契约 */
    int bpp;            /* 核心系统底层抽象与内存语义契约 */
    int flags;          /* 内部辅助逻辑 */
    const void *cov;    /* 核心系统底层抽象与内存语义契约 */
} zan_glyph_tile;

#define ZAN_TILE_RGBA 1

/* 底层系统交互与数据协议契约 */
typedef struct {
    const zan_glyph_tile *tile;
    int x, y;
} zan_glyph_item;

typedef struct {
    uint32_t color;
    int count;
    const zan_glyph_item *items;
} zan_glyph_run;

typedef struct zan_gui_backend_s {
    const char *name;   /* "cpu", "gl", ... */

    /* 核心系统底层抽象与内存语义契约 */
    void (*clear_rect)(struct zan_surface_s *s, int x, int y, int w, int h,
                       uint32_t color);
    void (*fill_rect)(struct zan_surface_s *s, int x, int y, int w, int h,
                      uint32_t color);
    /* 底层系统交互与数据协议契约 */
    void (*fill_round)(struct zan_surface_s *s, int x, int y, int w, int h,
                       int radius, int corners, uint32_t color);
    /* 内部辅助逻辑 */
    void (*draw_round)(struct zan_surface_s *s, int x, int y, int w, int h,
                       int radius, int corners, uint32_t color, int thickness);
    /* 底层系统交互与数据协议契约 */
    void (*surface_round)(struct zan_surface_s *s, int x, int y, int w, int h,
                          int radius, int corners, uint32_t fill,
                          uint32_t border, int thickness);
    /* 内部辅助逻辑 */
    void (*fill_vgrad)(struct zan_surface_s *s, int x, int y, int w, int h,
                       int radius, int corners, uint32_t top, uint32_t bottom);
    /* 内部辅助逻辑 */
    void (*fill_grad)(struct zan_surface_s *s, int x, int y, int w, int h,
                      int radius, int corners, int dir,
                      uint32_t from, uint32_t via, uint32_t to);
    void (*shadow_round)(struct zan_surface_s *s, int x, int y, int w, int h,
                         int radius, int blur, uint32_t color);
    void (*fill_circle)(struct zan_surface_s *s, int cx, int cy, int radius,
                        uint32_t color);
    void (*draw_circle)(struct zan_surface_s *s, int cx, int cy, int radius,
                        uint32_t color, int thickness);
    /* 底层系统交互与数据协议契约 */
    void (*fill_radial)(struct zan_surface_s *s, int cx, int cy, int radius,
                        uint32_t color, int inner_alpha);
    /* 内部辅助逻辑 */
    void (*fill_sector)(struct zan_surface_s *s, int cx, int cy,
                        int r_inner, int r_outer, int a0_deg, int a1_deg,
                        uint32_t color);
    /* 内部辅助逻辑 */
    void (*draw_line)(struct zan_surface_s *s, int x0, int y0, int x1, int y1,
                      uint32_t color, int thickness);
    /* 底层系统交互与数据协议契约 */
    void (*polyline)(struct zan_surface_s *s, const int32_t *pts, int n,
                     uint32_t color, int thickness, int fx);
    /* 内部辅助逻辑 */
    void (*polybatch)(struct zan_surface_s *s, const int32_t *pts,
                      const int32_t *counts, int n_paths,
                      uint32_t color, int thickness, int fx);

    /* 核心系统底层抽象与内存语义契约 */
    /* 核心系统底层抽象与内存语义契约 */
    void (*blur)(struct zan_surface_s *s, int x, int y, int w, int h,
                 int radius, int slot, int dirty,
                 int corner_radius, int corner_mask);
    void (*snapshot)(struct zan_surface_s *s, int x, int y, int w, int h,
                     int slot);
    /* 内部辅助逻辑 */
    void (*snapshot_patch)(struct zan_surface_s *s, int x, int y, int w, int h,
                           int slot);
    /* 内部辅助逻辑 */
    int  (*restore)(struct zan_surface_s *s, int x, int y, int w, int h,
                    int slot, int sub_rect);

    /* 核心系统底层抽象与内存语义契约 */
    /* 内部辅助逻辑 */
    void (*draw_text)(struct zan_surface_s *s, int x, int y, const char *text,
                      uint32_t color, int font_size);
    /* 内部辅助逻辑 */
    void (*glyph_run)(struct zan_surface_s *s, const zan_glyph_run *run);
    /* 底层系统交互与数据协议契约 */
    void (*blit_pixels)(struct zan_surface_s *s, const zan_bitmap *bitmap,
                        int dx, int dy, int dw, int dh,
                        int sx, int sy, int sw, int sh);
    void (*blit_pixels_batch)(struct zan_surface_s *s, const zan_bitmap *bitmap,
                              const float *quads, int count);

    /* 核心系统底层抽象与内存语义契约 */
    void (*set_clip)(struct zan_surface_s *s, int x0, int y0, int x1, int y1);
    /* 底层系统交互与数据协议契约 */
    void (*flush)(struct zan_surface_s *s);
    /* 内部辅助逻辑 */
    void (*read_pixels)(struct zan_surface_s *s);
    /* 内部辅助逻辑 */
    int  (*present)(struct zan_surface_s *s, void *native_window);
    /* 内部辅助逻辑 */
    void (*drop_window)(void *native_window);
    /* 内部辅助逻辑 */
    void (*drop_surface)(struct zan_surface_s *s);

    /* 核心系统底层抽象与内存语义契约 */
    /* 内部辅助逻辑 */
    void (*sync_to_cpu)(struct zan_surface_s *s);
    void (*sync_from_cpu)(struct zan_surface_s *s);
} zan_gui_backend;

/* 内部辅助逻辑 */
extern const zan_gui_backend zan_cpu_backend;

#endif /* ZAN_GUI_BACKEND_H */
