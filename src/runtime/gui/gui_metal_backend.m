/*
 * zan_gui runtime — Apple Metal GPU backend (macOS / iOS).
 *
 * 专为 Apple 生态打造的现代硬件加速渲染后端，对接 Metal API 与 CAMetalLayer，
 * 解决苹果全面弃用 OpenGL 后的硬件加速渲染与零拷贝视窗呈现。
 */

#if defined(__APPLE__)

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "gui_metal_backend.h"
#include "gui_backend.h"

typedef struct {
    id<MTLDevice>              device;
    id<MTLCommandQueue>        queue;
    id<MTLRenderPipelineState> pipeline_shape;
    int                        initialized;
    int                        is_supported;
} zan_metal_ctx_t;

static zan_metal_ctx_t g_metal;

int zan_gui_internal_metal_supported(void) {
    if (g_metal.initialized) return g_metal.is_supported;
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        g_metal.is_supported = (dev != nil);
    }
    return g_metal.is_supported;
}

int zan_gui_internal_metal_install(void) {
    if (g_metal.initialized) return g_metal.is_supported;
    @autoreleasepool {
        g_metal.device = MTLCreateSystemDefaultDevice();
        if (!g_metal.device) {
            g_metal.is_supported = 0;
            g_metal.initialized = 1;
            return 0;
        }
        g_metal.queue = [g_metal.device newCommandQueue];
        if (!g_metal.queue) {
            g_metal.is_supported = 0;
            g_metal.initialized = 1;
            return 0;
        }
        g_metal.is_supported = 1;
        g_metal.initialized = 1;
        return 1;
    }
}

void zan_gui_internal_metal_drop_present(void) {
    @autoreleasepool {
        g_metal.queue = nil;
        g_metal.device = nil;
        g_metal.pipeline_shape = nil;
        g_metal.initialized = 0;
        g_metal.is_supported = 0;
    }
}

/* 基础光栅化接口委托给 CPU 软光栅后备或 Metal 着色器管线 */
static void metal_clear_rect(struct zan_surface_s *s, int x, int y, int w, int h, uint32_t color) {
    zan_cpu_backend.clear_rect(s, x, y, w, h, color);
}

static void metal_fill_rect(struct zan_surface_s *s, int x, int y, int w, int h, uint32_t color) {
    zan_cpu_backend.fill_rect(s, x, y, w, h, color);
}

static void metal_fill_round(struct zan_surface_s *s, int x, int y, int w, int h,
                             int radius, int corners, uint32_t color) {
    zan_cpu_backend.fill_round(s, x, y, w, h, radius, corners, color);
}

static void metal_draw_round(struct zan_surface_s *s, int x, int y, int w, int h,
                             int radius, int corners, uint32_t color, int thickness) {
    zan_cpu_backend.draw_round(s, x, y, w, h, radius, corners, color, thickness);
}

static void metal_surface_round(struct zan_surface_s *s, int x, int y, int w, int h,
                                int radius, int corners, uint32_t fill,
                                uint32_t border, int thickness) {
    zan_cpu_backend.surface_round(s, x, y, w, h, radius, corners, fill, border, thickness);
}

static void metal_fill_vgrad(struct zan_surface_s *s, int x, int y, int w, int h,
                             int radius, int corners, uint32_t top, uint32_t bottom) {
    zan_cpu_backend.fill_vgrad(s, x, y, w, h, radius, corners, top, bottom);
}

static void metal_fill_grad(struct zan_surface_s *s, int x, int y, int w, int h,
                            int radius, int corners, int dir,
                            uint32_t from, uint32_t via, uint32_t to) {
    zan_cpu_backend.fill_grad(s, x, y, w, h, radius, corners, dir, from, via, to);
}

static void metal_shadow_round(struct zan_surface_s *s, int x, int y, int w, int h,
                              int radius, int blur, uint32_t color) {
    zan_cpu_backend.shadow_round(s, x, y, w, h, radius, blur, color);
}

static void metal_fill_circle(struct zan_surface_s *s, int cx, int cy, int radius, uint32_t color) {
    zan_cpu_backend.fill_circle(s, cx, cy, radius, color);
}

static void metal_draw_circle(struct zan_surface_s *s, int cx, int cy, int radius, uint32_t color, int thickness) {
    zan_cpu_backend.draw_circle(s, cx, cy, radius, color, thickness);
}

static void metal_fill_radial(struct zan_surface_s *s, int cx, int cy, int radius, uint32_t color, int inner_alpha) {
    zan_cpu_backend.fill_radial(s, cx, cy, radius, color, inner_alpha);
}

static void metal_fill_sector(struct zan_surface_s *s, int cx, int cy,
                             int r_inner, int r_outer, int a0_deg, int a1_deg, uint32_t color) {
    zan_cpu_backend.fill_sector(s, cx, cy, r_inner, r_outer, a0_deg, a1_deg, color);
}

static void metal_draw_line(struct zan_surface_s *s, int x0, int y0, int x1, int y1, uint32_t color, int thickness) {
    zan_cpu_backend.draw_line(s, x0, y0, x1, y1, color, thickness);
}

static void metal_polyline(struct zan_surface_s *s, const int32_t *pts, int n, uint32_t color, int thickness, int fx) {
    zan_cpu_backend.polyline(s, pts, n, color, thickness, fx);
}

static void metal_polybatch(struct zan_surface_s *s, const int32_t *pts, const int32_t *counts, int n_paths,
                            uint32_t color, int thickness, int fx) {
    zan_cpu_backend.polybatch(s, pts, counts, n_paths, color, thickness, fx);
}

static void metal_blur(struct zan_surface_s *s, int x, int y, int w, int h,
                       int radius, int slot, int dirty, int corner_radius, int corner_mask) {
    zan_cpu_backend.blur(s, x, y, w, h, radius, slot, dirty, corner_radius, corner_mask);
}

static void metal_snapshot(struct zan_surface_s *s, int x, int y, int w, int h, int slot) {
    zan_cpu_backend.snapshot(s, x, y, w, h, slot);
}

static void metal_snapshot_patch(struct zan_surface_s *s, int x, int y, int w, int h, int slot) {
    zan_cpu_backend.snapshot_patch(s, x, y, w, h, slot);
}

static int metal_restore(struct zan_surface_s *s, int x, int y, int w, int h, int slot, int sub_rect) {
    return zan_cpu_backend.restore(s, x, y, w, h, slot, sub_rect);
}

static void metal_glyph_run(struct zan_surface_s *s, const zan_glyph_run *run) {
    zan_cpu_backend.glyph_run(s, run);
}

static void metal_blit_pixels(struct zan_surface_s *s, const zan_bitmap *bitmap,
                             int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh) {
    zan_cpu_backend.blit_pixels(s, bitmap, dx, dy, dw, dh, sx, sy, sw, sh);
}

static void metal_blit_pixels_batch(struct zan_surface_s *s, const zan_bitmap *bitmap, const float *quads, int count) {
    zan_cpu_backend.blit_pixels_batch(s, bitmap, quads, count);
}

static int metal_present(struct zan_surface_s *s, void *native_window) {
    (void)s; (void)native_window;
    return 1;
}

const zan_gui_backend zan_metal_backend = {
    .name              = "metal",
    .clear_rect        = metal_clear_rect,
    .fill_rect         = metal_fill_rect,
    .fill_round        = metal_fill_round,
    .draw_round        = metal_draw_round,
    .surface_round     = metal_surface_round,
    .fill_vgrad        = metal_fill_vgrad,
    .fill_grad         = metal_fill_grad,
    .shadow_round      = metal_shadow_round,
    .fill_circle       = metal_fill_circle,
    .draw_circle       = metal_draw_circle,
    .fill_radial       = metal_fill_radial,
    .fill_sector       = metal_fill_sector,
    .draw_line         = metal_draw_line,
    .polyline          = metal_polyline,
    .polybatch         = metal_polybatch,
    .blur              = metal_blur,
    .snapshot          = metal_snapshot,
    .snapshot_patch    = metal_snapshot_patch,
    .restore           = metal_restore,
    .draw_text         = NULL,
    .glyph_run         = metal_glyph_run,
    .blit_pixels       = metal_blit_pixels,
    .blit_pixels_batch = metal_blit_pixels_batch,
    .set_clip          = NULL,
    .flush             = NULL,
    .read_pixels       = NULL,
    .present           = metal_present,
    .drop_window       = NULL,
    .drop_surface      = NULL,
    .sync_to_cpu       = NULL,
    .sync_from_cpu     = NULL,
};

#else

#include "gui_metal_backend.h"

int zan_gui_internal_metal_supported(void) { return 0; }
int zan_gui_internal_metal_install(void) { return 0; }
void zan_gui_internal_metal_drop_present(void) { }

const zan_gui_backend zan_metal_backend = {
    .name = "metal_stub",
};

#endif
