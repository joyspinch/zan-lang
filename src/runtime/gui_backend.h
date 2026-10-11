/* gui_backend */

#ifndef ZAN_GUI_BACKEND_H
#define ZAN_GUI_BACKEND_H

#include <stdint.h>
#include "zan_bitmap.h"

struct zan_surface_s;

#define ZAN_CORNERS_ALL 15

#define ZAN_GRAD_VERTICAL      0  /* top -> bottom */
#define ZAN_GRAD_HORIZONTAL    1  /* left -> right */
#define ZAN_GRAD_TO_BOTTOM_RIGHT 2
#define ZAN_GRAD_TO_BOTTOM_LEFT  3

typedef struct zan_glyph_tile_s {
    uint32_t id;
    uint32_t rev;
    int w, h;
    int left, top;
    int advance;
    int bpp;
    int flags;
    const void *cov;
} zan_glyph_tile;

#define ZAN_TILE_RGBA 1

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

    void (*clear_rect)(struct zan_surface_s *s, int x, int y, int w, int h,
                       uint32_t color);
    void (*fill_rect)(struct zan_surface_s *s, int x, int y, int w, int h,
                      uint32_t color);
    void (*fill_round)(struct zan_surface_s *s, int x, int y, int w, int h,
                       int radius, int corners, uint32_t color);
    void (*draw_round)(struct zan_surface_s *s, int x, int y, int w, int h,
                       int radius, int corners, uint32_t color, int thickness);
    void (*surface_round)(struct zan_surface_s *s, int x, int y, int w, int h,
                          int radius, int corners, uint32_t fill,
                          uint32_t border, int thickness);
    void (*fill_vgrad)(struct zan_surface_s *s, int x, int y, int w, int h,
                       int radius, int corners, uint32_t top, uint32_t bottom);
    void (*fill_grad)(struct zan_surface_s *s, int x, int y, int w, int h,
                      int radius, int corners, int dir,
                      uint32_t from, uint32_t via, uint32_t to);
    void (*shadow_round)(struct zan_surface_s *s, int x, int y, int w, int h,
                         int radius, int blur, uint32_t color);
    void (*fill_circle)(struct zan_surface_s *s, int cx, int cy, int radius,
                        uint32_t color);
    void (*draw_circle)(struct zan_surface_s *s, int cx, int cy, int radius,
                        uint32_t color, int thickness);
    void (*fill_radial)(struct zan_surface_s *s, int cx, int cy, int radius,
                        uint32_t color, int inner_alpha);
    void (*fill_sector)(struct zan_surface_s *s, int cx, int cy,
                        int r_inner, int r_outer, int a0_deg, int a1_deg,
                        uint32_t color);
    void (*draw_line)(struct zan_surface_s *s, int x0, int y0, int x1, int y1,
                      uint32_t color, int thickness);
    void (*polyline)(struct zan_surface_s *s, const int32_t *pts, int n,
                     uint32_t color, int thickness, int fx);
    void (*polybatch)(struct zan_surface_s *s, const int32_t *pts,
                      const int32_t *counts, int n_paths,
                      uint32_t color, int thickness, int fx);

    void (*blur)(struct zan_surface_s *s, int x, int y, int w, int h,
                 int radius, int slot, int dirty,
                 int corner_radius, int corner_mask);
    void (*snapshot)(struct zan_surface_s *s, int x, int y, int w, int h,
                     int slot);
    void (*snapshot_patch)(struct zan_surface_s *s, int x, int y, int w, int h,
                           int slot);
    int  (*restore)(struct zan_surface_s *s, int x, int y, int w, int h,
                    int slot, int sub_rect);

    void (*draw_text)(struct zan_surface_s *s, int x, int y, const char *text,
                      uint32_t color, int font_size);
    void (*glyph_run)(struct zan_surface_s *s, const zan_glyph_run *run);
    void (*blit_pixels)(struct zan_surface_s *s, const zan_bitmap *bitmap,
                        int dx, int dy, int dw, int dh,
                        int sx, int sy, int sw, int sh);
    void (*blit_pixels_batch)(struct zan_surface_s *s, const zan_bitmap *bitmap,
                              const float *quads, int count);

    void (*set_clip)(struct zan_surface_s *s, int x0, int y0, int x1, int y1);
    void (*flush)(struct zan_surface_s *s);
    void (*read_pixels)(struct zan_surface_s *s);
    int  (*present)(struct zan_surface_s *s, void *native_window);
    void (*drop_window)(void *native_window);
    void (*drop_surface)(struct zan_surface_s *s);

    void (*sync_to_cpu)(struct zan_surface_s *s);
    void (*sync_from_cpu)(struct zan_surface_s *s);
} zan_gui_backend;

extern const zan_gui_backend zan_cpu_backend;

#endif /* ZAN_GUI_BACKEND_H */
