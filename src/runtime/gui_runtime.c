/*
 * zan_gui runtime — software 2D renderer with anti-aliasing + system font rendering
 * Cross-platform: Win32 (full), X11 (Linux), Cocoa stubs (macOS)
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <rpc.h>
#include <shellscalingapi.h>
#include <dwmapi.h>
#include <imm.h>
/* Shcore.lib is intentionally NOT linked statically: Shcore.dll only exists on
 * Windows 8.1+, so a static import makes the executable fail to load on Windows
 * 7. SetProcessDpiAwareness is resolved dynamically instead (see
 * zan_enable_dpi_awareness). Dwmapi is Vista+ and present on Windows 7. */
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "imm32.lib")
/* When ZAN_GUI_STATIC is defined the runtime is compiled into a static archive
 * and linked directly into the executable (no zan_gui.dll dependency). The lib
 * pragmas above are honored by lld-link even in that mode, so the Win32 system
 * libraries are pulled in automatically without touching the compiler. */
#if defined(ZAN_GUI_STATIC)
#define EXPORT
#else
#define EXPORT __declspec(dllexport)
#endif
#elif defined(__linux__)
/* X11 headers back the native Linux window shell; the unified SDL backend
 * (ZAN_GUI_SDL) owns windowing instead, so they are not needed (and the build
 * need not depend on libX11-dev) in that configuration. */
#if !defined(ZAN_GUI_SDL)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#endif
#include <poll.h>
#include <time.h>
#include <locale.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#ifdef ZAN_GUI_FREETYPE
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#endif
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT __attribute__((visibility("default")))
#endif

/* Unified cross-platform windowing backend. When ZAN_GUI_SDL is defined the
 * per-platform window shells (Win32/X11/Cocoa) are compiled out and a single
 * SDL3-based shell drives windowing, input and present — the same SDL3 stack
 * the Game.* stdlib uses, so the IDE and games share one window/render path.
 * The software rasterizer and system-font text rendering are unchanged; only
 * the OS window, event pump and present go through SDL. */
#ifdef ZAN_GUI_SDL
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#endif

#include "../common/host_oom.h"
#include "rt_crash.h"
typedef int32_t i32;
typedef int64_t i64;
/* Native window handle (HWND / X11 Window / NSWindow*): pointer-width, so it
 * must not ride in a type that narrows when Zan `int` becomes 32-bit. */
typedef intptr_t iptr;
typedef uint32_t u32;
typedef uint8_t  u8;

/* ========================================================================
 * Pixel Buffer (Software Renderer)
 * ======================================================================== */

typedef struct {
    u32 *pixels;
    int width;
    int height;
    int stride;
    /* Active clip window (x1/y1 exclusive) enforced by every pixel write, so
     * scroll containers can bound their content to a viewport. clip_stack holds
     * saved windows for nested PushClip/PopClip (4 ints per level, 16 levels). */
    int clip_x0;
    int clip_y0;
    int clip_x1;
    int clip_y1;
    int clip_stack[64];
    int clip_depth;
} zan_surface_t;

static zan_surface_t *g_surfaces[64];
static int g_surface_count = 0;
/* Declared here because destroy_surface / zan_gui_clear (above their definition
 * below) reference them; the resize re-blit path assigns them later. */
static i64 g_last_surface = -1;
static u32 g_bg_color = 0xFFFFFFFF;

static int clamp_i(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static u32 pack_argb(int r, int g, int b, int a) {
    return ((u32)a << 24) | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

/* Alpha blend src over dst (straight-alpha, both stored non-premultiplied).
 * The dst==opaque fast path is bit-identical to the original opaque-only
 * compositor; the general path also accumulates the destination alpha so a
 * surface that was cleared transparent (for OS glass show-through) keeps a
 * meaningful alpha channel the layered-window present can hand to the DWM. */
static u32 blend_over(u32 dst, u32 src) {
    u32 sa = (src >> 24) & 0xFF;
    if (sa == 255) return src;
    if (sa == 0)   return dst;
    u32 sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
    u32 dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    u32 da = (dst >> 24) & 0xFF;
    u32 inv_sa = 255 - sa;
    if (da == 255) {
        u32 or_ = (sr * sa + dr * inv_sa) / 255;
        u32 og = (sg * sa + dg * inv_sa) / 255;
        u32 ob = (sb * sa + db * inv_sa) / 255;
        return (255u << 24) | (or_ << 16) | (og << 8) | ob;
    }
    /* General source-over on a possibly-transparent destination. */
    u32 da2 = da * inv_sa / 255;
    u32 oa = sa + da2;
    if (oa == 0) return 0;
    u32 or_ = (sr * sa + dr * da2) / oa;
    u32 og = (sg * sa + dg * da2) / oa;
    u32 ob = (sb * sa + db * da2) / oa;
    return (oa << 24) | (or_ << 16) | (og << 8) | ob;
}

/* Reset the clip window to the whole surface (frame start / new surface). */
static void clip_reset_full(zan_surface_t *s) {
    s->clip_x0 = 0;
    s->clip_y0 = 0;
    s->clip_x1 = s->width;
    s->clip_y1 = s->height;
    s->clip_depth = 0;
}

/* True when (x,y) lies outside the surface or the active clip window. */
static int clipped_out(zan_surface_t *s, int x, int y) {
    if (x < 0 || x >= s->width || y < 0 || y >= s->height) return 1;
    if (x < s->clip_x0 || x >= s->clip_x1) return 1;
    if (y < s->clip_y0 || y >= s->clip_y1) return 1;
    return 0;
}

static void set_pixel(zan_surface_t *s, int x, int y, u32 color) {
    if (clipped_out(s, x, y)) return;
    int idx = y * s->stride + x;
    s->pixels[idx] = blend_over(s->pixels[idx], color);
}

/* Set pixel with coverage alpha applied on top of color's own alpha */
static void set_pixel_aa(zan_surface_t *s, int x, int y, u32 color, int coverage) {
    if (clipped_out(s, x, y)) return;
    if (coverage <= 0) return;
    if (coverage > 255) coverage = 255;
    u32 a = ((color >> 24) & 0xFF) * (u32)coverage / 255;
    u32 c = (a << 24) | (color & 0x00FFFFFF);
    int idx = y * s->stride + x;
    s->pixels[idx] = blend_over(s->pixels[idx], c);
}

/* Convert a 0..1 coverage value to an alpha byte with a linear ramp. The
 * stroke AA transition is a fixed 1px band (cov = half + 0.5 - dist at the
 * call sites), the same distance-field falloff zan_gui_fill_circle uses —
 * a linear map matches it exactly, so circles, lines and polylines all get
 * the identical edge quality at every DPI. A smoothstep on a 1px band would
 * push mid-tones toward the ends and re-introduce the step it was meant to
 * remove. */
static inline int aa_coverage(double cov) {
    if (cov <= 0.0) return 0;
    if (cov >= 1.0) return 255;
    return (int)(cov * 255.0);
}

/* ---- Exported rendering functions ---- */

EXPORT i32 zan_gui_create_surface(i32 width, i32 height) {
    zan__crash_install(); /* idempotent; ensures GUI processes log hard crashes */
    /* Reuse a slot freed by destroy_surface so repeated resize/maximize cycles
     * (each destroy+create) don't leak the fixed 64-slot table. */
    int id = -1;
    for (int i = 0; i < g_surface_count; i++) {
        if (!g_surfaces[i]) { id = i; break; }
    }
    if (id < 0) {
        if (g_surface_count >= 64) return -1;
        id = g_surface_count++;
    }
    zan_surface_t *s = (zan_surface_t *)calloc(1, sizeof(zan_surface_t));
    s->width = (int)width;
    s->height = (int)height;
    s->stride = (int)width;
    clip_reset_full(s);
    /* malloc (not calloc): every frame starts with zan_gui_clear covering the
     * whole surface, so zero-init is wasted work — and on large windows the
     * per-resize zeroing of ~32MB was a noticeable hitch during drag-resize. */
    s->pixels = (u32 *)malloc((size_t)(width * height) * sizeof(u32));
    g_surfaces[id] = s;
    return (i64)id;
}

EXPORT i32 zan_gui_destroy_surface(i32 id) {
    if (id < 0 || id >= g_surface_count || !g_surfaces[id]) return 1;
    free(g_surfaces[id]->pixels);
    free(g_surfaces[id]);
    g_surfaces[id] = NULL;
    /* Don't leave the resize re-blit pointing at a freed surface (it would read
     * a NULL slot at best, a recycled one at worst). */
    if (id == g_last_surface) g_last_surface = -1;
    return 0;
}

EXPORT i32 zan_gui_surface_width(i32 id) {
    if (id < 0 || id >= g_surface_count || !g_surfaces[id]) return 0;
    return g_surfaces[id]->width;
}

EXPORT i32 zan_gui_surface_height(i32 id) {
    if (id < 0 || id >= g_surface_count || !g_surfaces[id]) return 0;
    return g_surfaces[id]->height;
}

/* Internal (not part of the [DllImport] ABI): expose a surface's raw ARGB
 * buffer to a native platform backend compiled as a separate TU (e.g. the
 * macOS Cocoa .m file), which cannot see the static g_surfaces table. */
u32 *zan_gui_internal_surface_data(i64 id, int *w, int *h, int *stride) {
    if (id < 0 || id >= g_surface_count || !g_surfaces[id]) return NULL;
    zan_surface_t *s = g_surfaces[id];
    if (w) *w = s->width;
    if (h) *h = s->height;
    if (stride) *stride = s->stride;
    return s->pixels;
}

/* Internal (not part of the [DllImport] ABI): expose the surface's active
 * clip window so platform backends that rasterize text/glyphs themselves
 * (e.g. the macOS CoreText path) honour PushClip like the shared renderer. */
void zan_gui_internal_surface_clip(i64 id, int *x0, int *y0, int *x1, int *y1) {
    if (id < 0 || id >= g_surface_count || !g_surfaces[id]) {
        if (x0) *x0 = 0;
        if (y0) *y0 = 0;
        if (x1) *x1 = 0;
        if (y1) *y1 = 0;
        return;
    }
    zan_surface_t *s = g_surfaces[id];
    if (x0) *x0 = s->clip_x0;
    if (y0) *y0 = s->clip_y0;
    if (x1) *x1 = s->clip_x1;
    if (y1) *y1 = s->clip_y1;
}

EXPORT void zan_gui_clear(i32 surface_id, i32 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    /* A frame begins with clear(), so drop any clip left set by the last one. */
    clip_reset_full(s);
    u32 c = (u32)color;
    g_bg_color = c;
    int count = s->width * s->height;
    for (int i = 0; i < count; i++) s->pixels[i] = c;
}

/* Intersect the clip window with (x,y,w,h) and save the previous window so a
 * matching zan_gui_pop_clip restores it. Nested pushes can only shrink. */
EXPORT void zan_gui_push_clip(i32 surface_id, i32 x, i32 y, i32 w, i32 h) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    int cap = (int)(sizeof(s->clip_stack) / sizeof(s->clip_stack[0]));
    if (s->clip_depth * 4 + 4 <= cap) {
        s->clip_stack[s->clip_depth * 4 + 0] = s->clip_x0;
        s->clip_stack[s->clip_depth * 4 + 1] = s->clip_y0;
        s->clip_stack[s->clip_depth * 4 + 2] = s->clip_x1;
        s->clip_stack[s->clip_depth * 4 + 3] = s->clip_y1;
        s->clip_depth++;
    }
    int nx0 = (int)x;
    int ny0 = (int)y;
    int nx1 = (int)x + (int)w;
    int ny1 = (int)y + (int)h;
    if (nx0 < s->clip_x0) nx0 = s->clip_x0;
    if (ny0 < s->clip_y0) ny0 = s->clip_y0;
    if (nx1 > s->clip_x1) nx1 = s->clip_x1;
    if (ny1 > s->clip_y1) ny1 = s->clip_y1;
    if (nx1 < nx0) nx1 = nx0;
    if (ny1 < ny0) ny1 = ny0;
    s->clip_x0 = nx0;
    s->clip_y0 = ny0;
    s->clip_x1 = nx1;
    s->clip_y1 = ny1;
}

/* Restore the clip window saved by the most recent zan_gui_push_clip. */
EXPORT void zan_gui_pop_clip(i32 surface_id) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    if (s->clip_depth > 0) {
        s->clip_depth--;
        s->clip_x0 = s->clip_stack[s->clip_depth * 4 + 0];
        s->clip_y0 = s->clip_stack[s->clip_depth * 4 + 1];
        s->clip_x1 = s->clip_stack[s->clip_depth * 4 + 2];
        s->clip_y1 = s->clip_stack[s->clip_depth * 4 + 3];
    } else {
        clip_reset_full(s);
    }
}

/* Drop all clip windows and clip to the whole surface again. */
EXPORT void zan_gui_reset_clip(i32 surface_id) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    clip_reset_full(s);
}

/* ---- opt-in raster statistics ------------------------------------------- *
 * Counts calls and touched pixels per primitive so a frame's cost can be
 * attributed to fills / blends / blurs / images / text instead of guessed at.
 * zan_gui_perf_dump appends one line and resets. Zero cost when unused. */
typedef struct { long long calls, px; } zan_stat_t;
static zan_stat_t g_st_fill_op, g_st_fill_blend, g_st_grad, g_st_round,
                  g_st_radial, g_st_blur, g_st_blur_hit, g_st_img,
                  g_st_snap, g_st_restore, g_st_glyph;
#define ZAN_STAT(s, n) do { (s).calls++; (s).px += (long long)(n); } while (0)

EXPORT void zan_gui_stat_glyphs(i32 n) { ZAN_STAT(g_st_glyph, n); }

/* The widest translucent fills of the frame, so a profile can name the layers
 * that dominate blending instead of only totalling them. */
#define ZAN_TOP_N 6
static int g_top_a[ZAN_TOP_N], g_top_x[ZAN_TOP_N], g_top_y[ZAN_TOP_N],
           g_top_w[ZAN_TOP_N], g_top_h[ZAN_TOP_N], g_top_c[ZAN_TOP_N];

static void stat_top_blend(int area, int x, int y, int w, int h, u32 color) {
    int slot = -1;
    for (int i = 0; i < ZAN_TOP_N; i++) {
        if (area > g_top_a[i]) { slot = i; break; }
    }
    if (slot < 0) return;
    for (int i = ZAN_TOP_N - 1; i > slot; i--) {
        g_top_a[i] = g_top_a[i - 1]; g_top_x[i] = g_top_x[i - 1];
        g_top_y[i] = g_top_y[i - 1]; g_top_w[i] = g_top_w[i - 1];
        g_top_h[i] = g_top_h[i - 1]; g_top_c[i] = g_top_c[i - 1];
    }
    g_top_a[slot] = area; g_top_x[slot] = x; g_top_y[slot] = y;
    g_top_w[slot] = w; g_top_h[slot] = h; g_top_c[slot] = (int)color;
}

/* field: 0 area/1000, 1 x, 2 y, 3 w, 4 h; reading field 4 clears the entry. */
EXPORT i32 zan_gui_stat_top(i32 rank, i32 field) {
    if (rank < 0 || rank >= ZAN_TOP_N) return 0;
    switch (field) {
        case 0: return g_top_a[rank] / 1000;
        case 1: return g_top_x[rank];
        case 2: return g_top_y[rank];
        case 3: return g_top_w[rank];
        case 5: return (g_top_c[rank] >> 24) & 0xFF;
        default: break;
    }
    i32 h = g_top_h[rank];
    g_top_a[rank] = 0;
    return h;
}

/* Reads one counter and clears it: idx selects the primitive (see the switch),
 * kind 0 = call count, 1 = touched pixels / 1000. Cleared on read of kind 1 so
 * the caller reads both then moves to the next primitive. Formatting lives in
 * the Zan side (App's frame profiler). */
EXPORT i32 zan_gui_stat_read(i32 idx, i32 kind) {
    zan_stat_t *t = 0;
    switch (idx) {
        case 0: t = &g_st_fill_op; break;
        case 1: t = &g_st_fill_blend; break;
        case 2: t = &g_st_grad; break;
        case 3: t = &g_st_round; break;
        case 4: t = &g_st_radial; break;
        case 5: t = &g_st_blur; break;
        case 6: t = &g_st_blur_hit; break;
        case 7: t = &g_st_img; break;
        case 8: t = &g_st_snap; break;
        case 9: t = &g_st_restore; break;
        case 10: t = &g_st_glyph; break;
        default: return 0;
    }
    if (kind == 0) { return (i32)t->calls; }
    i32 kpx = (i32)(t->px / 1000);
    t->calls = 0;
    t->px = 0;
    return kpx;
}

EXPORT void zan_gui_fill_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    u32 c = (u32)color;
    int x0 = clamp_i((int)x, s->clip_x0, s->clip_x1);
    int y0 = clamp_i((int)y, s->clip_y0, s->clip_y1);
    int x1 = clamp_i((int)(x + w), s->clip_x0, s->clip_x1);
    int y1 = clamp_i((int)(y + h), s->clip_y0, s->clip_y1);
    u32 sa = (c >> 24) & 0xFF;
    long long area = (long long)(x1 - x0) * (long long)(y1 - y0);
    if (area < 0) area = 0;
    if (sa == 255) {
        ZAN_STAT(g_st_fill_op, area);
        for (int py = y0; py < y1; py++) {
            u32 *row = s->pixels + py * s->stride;
            for (int px = x0; px < x1; px++) row[px] = c;
        }
    } else if (sa != 0) {
        ZAN_STAT(g_st_fill_blend, area);
        stat_top_blend((int)area, x0, y0, x1 - x0, y1 - y0, c);
        /* Rect is already clamped to the clip window, so blend straight into
         * the row instead of re-clipping every pixel via set_pixel -- these
         * translucent fills (scrims, hover/selection tints) cover large areas. */
        for (int py = y0; py < y1; py++) {
            u32 *row = s->pixels + py * s->stride;
            for (int px = x0; px < x1; px++) row[px] = blend_over(row[px], c);
        }
    }
}

/* Vertical linear gradient fill: each row is a lerp between color_top (at y)
 * and color_bottom (at y+h-1). Opaque; used for modern gradient wallpapers
 * (AI / Aurora / Glass backdrops). Single pass, no allocation. */
static int zan_round_cov(int i, int j, int rw, int rh, int cr, int cmask);
EXPORT void zan_gui_fill_vgrad(
    i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 color_top,
    i32 color_bottom) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    int x0 = clamp_i((int)x, s->clip_x0, s->clip_x1);
    int y0 = clamp_i((int)y, s->clip_y0, s->clip_y1);
    int x1 = clamp_i((int)(x + w), s->clip_x0, s->clip_x1);
    int y1 = clamp_i((int)(y + h), s->clip_y0, s->clip_y1);
    int rh = (int)h;
    if (rh < 1) rh = 1;
    int denom = rh > 1 ? rh - 1 : 1;
    ZAN_STAT(g_st_grad, (long long)(x1 - x0) * (long long)(y1 - y0));
    u32 ct = (u32)color_top, cb = (u32)color_bottom;
    int tr = (ct >> 16) & 0xFF, tg = (ct >> 8) & 0xFF, tb = ct & 0xFF;
    int mr = (cb >> 16) & 0xFF, mg = (cb >> 8) & 0xFF, mb = cb & 0xFF;
    for (int py = y0; py < y1; py++) {
        int num = py - (int)y;
        if (num < 0) num = 0;
        if (num > rh - 1) num = rh - 1;
        int rr = tr + (mr - tr) * num / denom;
        int gg = tg + (mg - tg) * num / denom;
        int bb = tb + (mb - tb) * num / denom;
        u32 c = 0xFF000000u | ((u32)rr << 16) | ((u32)gg << 8) | (u32)bb;
        u32 *row = s->pixels + py * s->stride;
        for (int px = x0; px < x1; px++) row[px] = c;
    }
}

/* Vertical gradient fill clipped to a rounded-rect mask. The plain vgrad above
 * overwrites every pixel in the rect, so painting it over a FillRoundRect
 * destroys the corner AA (the arcs' blended edge pixels get replaced by opaque
 * gradient) and the corners read as jagged teeth. This variant cuts the same
 * silhouette as zan_gui_fill_rounded_rect_mask: interior pixels are solid, the
 * 1px corner arc is blended with fractional coverage. */
EXPORT void zan_gui_fill_vgrad_mask(
    i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 mask,
    i32 color_top, i32 color_bottom) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    int x0 = clamp_i((int)x, s->clip_x0, s->clip_x1);
    int y0 = clamp_i((int)y, s->clip_y0, s->clip_y1);
    int x1 = clamp_i((int)(x + w), s->clip_x0, s->clip_x1);
    int y1 = clamp_i((int)(y + h), s->clip_y0, s->clip_y1);
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return;
    int r = (int)radius;
    int m = (int)mask;
    if (r < 0) r = 0;
    if (r > 0 && (m & 15) == 0) r = 0;
    if (r > (int)w / 2) r = (int)w / 2;
    if (r > (int)h / 2) r = (int)h / 2;
    int rh2 = (int)h;
    if (rh2 < 1) rh2 = 1;
    int denom = rh2 > 1 ? rh2 - 1 : 1;
    ZAN_STAT(g_st_grad, (long long)rw * (long long)rh);
    u32 ct = (u32)color_top, cb = (u32)color_bottom;
    int tr = (ct >> 16) & 0xFF, tg = (ct >> 8) & 0xFF, tb = ct & 0xFF;
    int mr = (cb >> 16) & 0xFF, mg = (cb >> 8) & 0xFF, mb = cb & 0xFF;
    for (int py = y0; py < y1; py++) {
        int num = py - (int)y;
        if (num < 0) num = 0;
        if (num > rh2 - 1) num = rh2 - 1;
        int rr = tr + (mr - tr) * num / denom;
        int gg = tg + (mg - tg) * num / denom;
        int bb = tb + (mb - tb) * num / denom;
        u32 c = 0xFF000000u | ((u32)rr << 16) | ((u32)gg << 8) | (u32)bb;
        u32 *row = s->pixels + py * s->stride;
        if (r <= 0) {
            for (int px = x0; px < x1; px++) row[px] = c;
            continue;
        }
        for (int px = x0; px < x1; px++) {
            int cov = zan_round_cov(px - (int)x, py - (int)y, (int)w, (int)h, r, m);
            if (cov >= 255) row[px] = c;
            else if (cov > 0) set_pixel_aa(s, px, py, c, cov);
        }
    }
}

/* Is local pixel (i,j) inside a rounded-rect mask of size rw*rh with corner
 * radius cr (corner bits per ZAN_CORNER_*)? Corner geometry matches
 * zan_gui_fill_rounded_rect_mask so a rounded blur lines up exactly with the
 * translucent rounded tint drawn over it. cr<=0 means the whole rect. */
static int zan_round_in(int i, int j, int rw, int rh, int cr, int cmask) {
    if (cr <= 0) return 1;
    if (cr > rw / 2) cr = rw / 2;
    if (cr > rh / 2) cr = rh / 2;
    if (cr <= 0) return 1;
    if ((i >= cr && i < rw - cr) || (j >= cr && j < rh - cr)) return 1;
    int cx, cy, bit;
    if (i < cr && j < cr)              { cx = cr;      cy = cr;      bit = 1; }
    else if (i >= rw - cr && j < cr)   { cx = rw - cr; cy = cr;      bit = 2; }
    else if (i < cr && j >= rh - cr)   { cx = cr;      cy = rh - cr; bit = 8; }
    else                               { cx = rw - cr; cy = rh - cr; bit = 4; }
    if (!(cmask & bit)) return 1;                 /* square (welded) corner */
    double dx = (double)i + 0.5 - (double)cx;
    double dy = (double)j + 0.5 - (double)cy;
    return (dx * dx + dy * dy) <= (double)cr * (double)cr ? 1 : 0;
}

/* Anti-aliased rounded-rect mask: 0..255 coverage of pixel (i,j) for a
 * rounded rect of size rw*rh and radius cr (corner bits per ZAN_CORNER_*).
 * 255 = fully inside, 0 = outside, in between = the 1px corner arc edge.
 * Shares the corner geometry of zan_round_in so translucent fills that blend
 * per-pixel (gradients) cut the same silhouette FillRoundRect draws. */
static int zan_round_cov(int i, int j, int rw, int rh, int cr, int cmask) {
    if (cr <= 0) return 255;
    if (cr > rw / 2) cr = rw / 2;
    if (cr > rh / 2) cr = rh / 2;
    if (cr <= 0) return 255;
    if ((i >= cr && i < rw - cr) || (j >= cr && j < rh - cr)) return 255;
    int cx, cy, bit;
    if (i < cr && j < cr)              { cx = cr;      cy = cr;      bit = 1; }
    else if (i >= rw - cr && j < cr)   { cx = rw - cr; cy = cr;      bit = 2; }
    else if (i < cr && j >= rh - cr)   { cx = cr;      cy = rh - cr; bit = 8; }
    else                               { cx = rw - cr; cy = rh - cr; bit = 4; }
    if (!(cmask & bit)) return 255;                 /* square (welded) corner */
    double dx = (double)i + 0.5 - (double)cx;
    double dy = (double)j + 0.5 - (double)cy;
    double dist = sqrt(dx * dx + dy * dy);
    double cov = (double)cr - dist + 0.5;           /* ~1px AA arc edge */
    if (cov >= 1.0) return 255;
    if (cov <= 0.0) return 0;
    return (int)(cov * 255.0);
}

/* Backdrop blur: separable box blur (3 passes ~ Gaussian) over a rectangular
 * region, in place. Used to render frosted-glass panels — draw the backdrop,
 * blur the region behind a panel, then overlay a translucent tint. Alpha is
 * forced opaque (the backdrop under an overlay is opaque). Edge samples are
 * clamped to the region. Cost is O(passes * area), independent of radius.
 * When cr>0 the blurred output is written only inside a rounded-rect mask, so
 * the panel's corners keep the sharp backdrop instead of a blurred square that
 * a translucent rounded tint can never paint back over. */
static void zan_blur_rect_core(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, int cr, int cmask) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    int x0 = clamp_i((int)x, 0, s->width);
    int y0 = clamp_i((int)y, 0, s->height);
    int x1 = clamp_i((int)(x + w), 0, s->width);
    int y1 = clamp_i((int)(y + h), 0, s->height);
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return;
    int r = (int)radius;
    if (r < 1) return;
    if (r > 40) r = 40;

    /* Downsampled frosted blur. Box-blurring at full resolution is the
     * dominant glass cost; for the large radii glass uses we downscale the
     * region, blur the small copy, then bilinearly upscale. Visually identical
     * for frost, but the blur runs on ~1/(ds*ds) of the pixels. */
    ZAN_STAT(g_st_blur, (long long)rw * (long long)rh);
    int ds = 1;
    if (r >= 12) { ds = 4; } else if (r >= 6) { ds = 2; }
    int dw = (rw + ds - 1) / ds;
    int dh = (rh + ds - 1) / ds;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    size_t dn = (size_t)dw * (size_t)dh;
    u32 *a = (u32 *)malloc(dn * sizeof(u32));
    u32 *b = (u32 *)malloc(dn * sizeof(u32));
    if (!a || !b) { free(a); free(b); return; }

    /* Downsample: average each ds x ds source block into one small pixel. */
    for (int dj = 0; dj < dh; dj++) {
        int sy0 = dj * ds;
        int sy1 = sy0 + ds; if (sy1 > rh) sy1 = rh;
        for (int di = 0; di < dw; di++) {
            int sx0 = di * ds;
            int sx1 = sx0 + ds; if (sx1 > rw) sx1 = rw;
            int sr = 0, sg = 0, sb = 0, cnt = 0;
            for (int yy = sy0; yy < sy1; yy++) {
                u32 *row = s->pixels + (y0 + yy) * s->stride + x0;
                for (int xx = sx0; xx < sx1; xx++) {
                    u32 px = row[xx];
                    sr += (px >> 16) & 0xFF; sg += (px >> 8) & 0xFF; sb += px & 0xFF;
                    cnt++;
                }
            }
            if (cnt < 1) cnt = 1;
            a[dj * dw + di] = 0xFF000000u | ((u32)(sr / cnt) << 16)
                            | ((u32)(sg / cnt) << 8) | (u32)(sb / cnt);
        }
    }

    /* Box blur the small copy (2 passes at reduced radius approximate the
     * former 3 full-res passes). */
    int rr = r / ds; if (rr < 1) rr = 1;
    int win = 2 * rr + 1;
    for (int p = 0; p < 2; p++) {
        /* horizontal: a -> b */
        for (int j = 0; j < dh; j++) {
            u32 *src = a + j * dw;
            u32 *dst = b + j * dw;
            int sr = 0, sg = 0, sb = 0;
            for (int k = -rr; k <= rr; k++) {
                int ii = clamp_i(k, 0, dw - 1);
                u32 px = src[ii];
                sr += (px >> 16) & 0xFF; sg += (px >> 8) & 0xFF; sb += px & 0xFF;
            }
            for (int i = 0; i < dw; i++) {
                dst[i] = 0xFF000000u | ((u32)(sr / win) << 16)
                       | ((u32)(sg / win) << 8) | (u32)(sb / win);
                u32 pa = src[clamp_i(i + rr + 1, 0, dw - 1)];
                u32 ps = src[clamp_i(i - rr, 0, dw - 1)];
                sr += (int)((pa >> 16) & 0xFF) - (int)((ps >> 16) & 0xFF);
                sg += (int)((pa >> 8) & 0xFF)  - (int)((ps >> 8) & 0xFF);
                sb += (int)(pa & 0xFF)         - (int)(ps & 0xFF);
            }
        }
        /* vertical: b -> a */
        for (int i = 0; i < dw; i++) {
            int sr = 0, sg = 0, sb = 0;
            for (int k = -rr; k <= rr; k++) {
                int jj = clamp_i(k, 0, dh - 1);
                u32 px = b[jj * dw + i];
                sr += (px >> 16) & 0xFF; sg += (px >> 8) & 0xFF; sb += px & 0xFF;
            }
            for (int j = 0; j < dh; j++) {
                a[j * dw + i] = 0xFF000000u | ((u32)(sr / win) << 16)
                              | ((u32)(sg / win) << 8) | (u32)(sb / win);
                u32 pa = b[clamp_i(j + rr + 1, 0, dh - 1) * dw + i];
                u32 ps = b[clamp_i(j - rr, 0, dh - 1) * dw + i];
                sr += (int)((pa >> 16) & 0xFF) - (int)((ps >> 16) & 0xFF);
                sg += (int)((pa >> 8) & 0xFF)  - (int)((ps >> 8) & 0xFF);
                sb += (int)(pa & 0xFF)         - (int)(ps & 0xFF);
            }
        }
    }

    /* Vibrancy (luma-preserving ~1.6x saturation) on the small copy so the
     * frost keeps the wallpaper's colour instead of washing to grey. */
    for (int i = 0; i < (int)dn; i++) {
        u32 px = a[i];
        int rc = (int)((px >> 16) & 0xFF);
        int gc = (int)((px >> 8) & 0xFF);
        int bc = (int)(px & 0xFF);
        int luma = (rc * 77 + gc * 150 + bc * 29) >> 8;
        rc = luma + (rc - luma) * 8 / 5;
        gc = luma + (gc - luma) * 8 / 5;
        bc = luma + (bc - luma) * 8 / 5;
        a[i] = 0xFF000000u | ((u32)clamp_i(rc, 0, 255) << 16)
             | ((u32)clamp_i(gc, 0, 255) << 8) | (u32)clamp_i(bc, 0, 255);
    }

    /* Upscale the small blurred copy back into the surface region, adding a
     * static fine grain (position-hashed +/-3 luma) so the frost reads as
     * acrylic texture instead of a perfectly smooth plastic sheet. */
    if (ds == 1) {
        for (int j = 0; j < rh; j++) {
            u32 *row = s->pixels + (y0 + j) * s->stride + x0;
            u32 *src = a + j * dw;
            for (int i = 0; i < rw; i++) {
                u32 px = src[i];
                int nz = ((((x0 + i) * 197 + (y0 + j) * 173) >> 3) % 7) - 3;
                int rC = clamp_i((int)((px >> 16) & 0xFF) + nz, 0, 255);
                int gC = clamp_i((int)((px >> 8) & 0xFF) + nz, 0, 255);
                int bC = clamp_i((int)(px & 0xFF) + nz, 0, 255);
                if (cr <= 0 || zan_round_in(i, j, rw, rh, cr, cmask))
                    row[i] = 0xFF000000u | ((u32)rC << 16) | ((u32)gC << 8) | (u32)bC;
            }
        }
    } else {
        /* Bilinear: sample the small copy at each full-res pixel centre
         * (fixed-point, 8 fractional bits). */
        for (int j = 0; j < rh; j++) {
            int fy = ((j * 2 + 1) * 256) / (ds * 2) - 128;
            if (fy < 0) fy = 0;
            int gy = fy >> 8; int wy = fy & 255;
            if (gy > dh - 1) { gy = dh - 1; wy = 0; }
            int gy1 = gy + 1; if (gy1 > dh - 1) gy1 = dh - 1;
            u32 *row = s->pixels + (y0 + j) * s->stride + x0;
            for (int i = 0; i < rw; i++) {
                int fx = ((i * 2 + 1) * 256) / (ds * 2) - 128;
                if (fx < 0) fx = 0;
                int gx = fx >> 8; int wx = fx & 255;
                if (gx > dw - 1) { gx = dw - 1; wx = 0; }
                int gx1 = gx + 1; if (gx1 > dw - 1) gx1 = dw - 1;
                u32 p00 = a[gy * dw + gx];
                u32 p01 = a[gy * dw + gx1];
                u32 p10 = a[gy1 * dw + gx];
                u32 p11 = a[gy1 * dw + gx1];
                int w00 = (256 - wx) * (256 - wy);
                int w01 = wx * (256 - wy);
                int w10 = (256 - wx) * wy;
                int w11 = wx * wy;
                int rC = ((int)((p00 >> 16) & 0xFF) * w00 + (int)((p01 >> 16) & 0xFF) * w01
                        + (int)((p10 >> 16) & 0xFF) * w10 + (int)((p11 >> 16) & 0xFF) * w11) >> 16;
                int gC = ((int)((p00 >> 8) & 0xFF) * w00 + (int)((p01 >> 8) & 0xFF) * w01
                        + (int)((p10 >> 8) & 0xFF) * w10 + (int)((p11 >> 8) & 0xFF) * w11) >> 16;
                int bC = ((int)(p00 & 0xFF) * w00 + (int)(p01 & 0xFF) * w01
                        + (int)(p10 & 0xFF) * w10 + (int)(p11 & 0xFF) * w11) >> 16;
                int nz = ((((x0 + i) * 197 + (y0 + j) * 173) >> 3) % 7) - 3;
                rC = clamp_i(rC + nz, 0, 255);
                gC = clamp_i(gC + nz, 0, 255);
                bC = clamp_i(bC + nz, 0, 255);
                if (cr <= 0 || zan_round_in(i, j, rw, rh, cr, cmask))
                    row[i] = 0xFF000000u | ((u32)rC << 16) | ((u32)gC << 8) | (u32)bC;
            }
        }
    }
    free(a); free(b);
}

EXPORT void zan_gui_blur_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius) {
    zan_blur_rect_core(surface_id, x, y, w, h, radius, 0, 15);
}

/* --- Frosted-glass blur cache ---------------------------------------------
 * Live Gaussian blur is the dominant per-frame cost under the glass theme: an
 * on-screen animation (spinner, toast) repaints the whole page, and every glass
 * panel re-blurs its otherwise-static backdrop. Each slot snapshots the blurred
 * output so animation-only frames (dirty==0, geometry unchanged) restore the
 * cached pixels with a plain copy instead of recomputing the blur. The caller
 * passes dirty==1 whenever the content behind the glass may have changed
 * (input, scroll, resize, theme) and a stable slot id per glass surface. */
#define ZAN_BLUR_CACHE_SLOTS 64
typedef struct {
    int valid;
    int sid; /* owning surface: slots are shared across windows, so a slot
              * must never be restored into a different window's surface
              * (that painted one window's pixels into another during e.g. a
              * child-window open animation). */
    int x0, y0, rw, rh, r;
    u32 *pixels;
    size_t cap;
} zan_blur_cache_t;
static zan_blur_cache_t g_blur_cache[ZAN_BLUR_CACHE_SLOTS];

static void zan_blur_cached_core(
    i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 slot,
    i32 dirty, int cr, int cmask) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    if (slot < 0 || slot >= ZAN_BLUR_CACHE_SLOTS) {
        zan_blur_rect_core(surface_id, x, y, w, h, radius, cr, cmask);
        return;
    }
    int x0 = clamp_i((int)x, 0, s->width);
    int y0 = clamp_i((int)y, 0, s->height);
    int x1 = clamp_i((int)(x + w), 0, s->width);
    int y1 = clamp_i((int)(y + h), 0, s->height);
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return;
    int r = (int)radius;
    if (r < 1) return;
    if (r > 40) r = 40;

    zan_blur_cache_t *c = &g_blur_cache[slot];
    size_t n = (size_t)rw * (size_t)rh;

    /* Reuse path: same geometry, not dirtied -> restore cached blurred pixels
     * over the (freshly redrawn, identical) backdrop without re-blurring. */
    if (!dirty && c->valid && c->pixels && c->sid == (int)surface_id
        && c->x0 == x0 && c->y0 == y0 && c->rw == rw && c->rh == rh
        && c->r == r) {
        ZAN_STAT(g_st_blur_hit, (long long)rw * (long long)rh);
        for (int j = 0; j < rh; j++) {
            u32 *row = s->pixels + (y0 + j) * s->stride + x0;
            u32 *src = c->pixels + (size_t)j * rw;
            for (int i = 0; i < rw; i++)
                if (cr <= 0 || zan_round_in(i, j, rw, rh, cr, cmask))
                    row[i] = src[i];
        }
        return;
    }

    /* Recompute in place (identical clamping), then snapshot into the slot. */
    zan_blur_rect_core(surface_id, x, y, w, h, radius, cr, cmask);
    if (c->cap < n) {
        u32 *np = (u32 *)realloc(c->pixels, n * sizeof(u32));
        if (!np) { c->valid = 0; return; }
        c->pixels = np;
        c->cap = n;
    }
    for (int j = 0; j < rh; j++) {
        u32 *row = s->pixels + (y0 + j) * s->stride + x0;
        u32 *dst = c->pixels + (size_t)j * rw;
        for (int i = 0; i < rw; i++) dst[i] = row[i];
    }
    c->x0 = x0; c->y0 = y0; c->rw = rw; c->rh = rh; c->r = r;
    c->sid = (int)surface_id;
    c->valid = 1;
}

EXPORT void zan_gui_blur_rect_cached(
    i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 slot,
    i32 dirty) {
    zan_blur_cached_core(surface_id, x, y, w, h, radius, slot, dirty, 0, 15);
}

/* Rounded frosted-glass blur: like zan_gui_blur_rect_cached but the blurred
 * output is clipped to a rounded-rect (corner radius cr, corner bits cmask) so
 * a panel's corners keep the sharp backdrop and line up with the rounded tint
 * drawn over them. */
EXPORT void zan_gui_blur_round_cached(
    i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 slot,
    i32 dirty, i32 corner_radius, i32 corner_mask) {
    zan_blur_cached_core(surface_id, x, y, w, h, radius, slot, dirty,
                         (int)corner_radius, (int)corner_mask);
}

/* --- Static-backdrop snapshot cache ---------------------------------------
 * Even with the blur cached, an on-screen animation still re-paints the whole
 * page every frame -- and under glass the wallpaper (a gradient plus several
 * large alpha-blended blobs) is by far the most expensive part of that. These
 * slots snapshot a region's pixels after it is drawn; on animation-only frames
 * the caller restores the snapshot with a single copy instead of re-painting
 * the backdrop. Geometry must match (else restore reports failure and the
 * caller repaints normally, e.g. after a resize).
 *
 * The pool is dynamic: ZAN_SNAP_CACHE_SLOTS is just the compile-time floor.
 * The runtime grows the array (realloc) on demand up to
 * ZAN_SNAP_CACHE_MAX_SLOTS, and zan_gui_surface_release frees a slot's pixels
 * so a caller (e.g. a grid cell that embeds a chart and then scrolls away) can
 * hand the memory back instead of holding it for the session. */
#define ZAN_SNAP_CACHE_SLOTS 8
#define ZAN_SNAP_CACHE_MAX_SLOTS 512
static zan_blur_cache_t *g_snap_cache;
static int g_snap_cache_count;
static void zan_snap_ensure(int slot) {
    if (slot < 0 || slot >= ZAN_SNAP_CACHE_MAX_SLOTS) return;
    if (slot < g_snap_cache_count) return;
    int grow = slot + 1;
    if (grow < ZAN_SNAP_CACHE_SLOTS) grow = ZAN_SNAP_CACHE_SLOTS;
    zan_blur_cache_t *np = (zan_blur_cache_t *)realloc(
        g_snap_cache, (size_t)grow * sizeof(zan_blur_cache_t));
    if (!np) return;
    for (int i = g_snap_cache_count; i < grow; i++) {
        np[i].valid = 0; np[i].pixels = NULL; np[i].cap = 0;
    }
    g_snap_cache = np;
    g_snap_cache_count = grow;
}
static zan_blur_cache_t *zan_snap_slot(int slot) {
    if (slot < 0 || slot >= ZAN_SNAP_CACHE_MAX_SLOTS) return NULL;
    zan_snap_ensure(slot);
    if (slot >= g_snap_cache_count) return NULL;
    return &g_snap_cache[slot];
}

EXPORT void zan_gui_snapshot_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 slot) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    zan_blur_cache_t *c = zan_snap_slot(slot);
    if (!c) return;
    int x0 = clamp_i((int)x, 0, s->width);
    int y0 = clamp_i((int)y, 0, s->height);
    int x1 = clamp_i((int)(x + w), 0, s->width);
    int y1 = clamp_i((int)(y + h), 0, s->height);
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return;
    ZAN_STAT(g_st_snap, (long long)rw * (long long)rh);
    size_t n = (size_t)rw * (size_t)rh;
    if (c->cap < n) {
        u32 *np = (u32 *)realloc(c->pixels, n * sizeof(u32));
        if (!np) { c->valid = 0; return; }
        c->pixels = np;
        c->cap = n;
    }
    for (int j = 0; j < rh; j++) {
        u32 *row = s->pixels + (y0 + j) * s->stride + x0;
        u32 *dst = c->pixels + (size_t)j * rw;
        for (int i = 0; i < rw; i++) dst[i] = row[i];
    }
    c->x0 = x0; c->y0 = y0; c->rw = rw; c->rh = rh; c->r = 0;
    c->sid = (int)surface_id;
    c->valid = 1;
}

EXPORT i32 zan_gui_restore_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 slot) {
    if (surface_id < 0 || surface_id >= g_surface_count) return 0;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return 0;
    zan_blur_cache_t *c = zan_snap_slot(slot);
    if (!c) return 0;
    int x0 = clamp_i((int)x, 0, s->width);
    int y0 = clamp_i((int)y, 0, s->height);
    int x1 = clamp_i((int)(x + w), 0, s->width);
    int y1 = clamp_i((int)(y + h), 0, s->height);
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return 0;
    if (!c->valid || !c->pixels || c->sid != (int)surface_id
        || c->x0 != x0 || c->y0 != y0 || c->rw != rw || c->rh != rh) {
        return 0;
    }
    /* Like every other drawing op, a restore writes only inside the clip
     * window: a partial (damage-clipped) frame restores just the background it
     * is allowed to touch and leaves the rest of the last frame standing. */
    int cx0 = x0 > s->clip_x0 ? x0 : s->clip_x0;
    int cy0 = y0 > s->clip_y0 ? y0 : s->clip_y0;
    int cx1 = x1 < s->clip_x1 ? x1 : s->clip_x1;
    int cy1 = y1 < s->clip_y1 ? y1 : s->clip_y1;
    if (cx1 <= cx0 || cy1 <= cy0) return 1;
    ZAN_STAT(g_st_restore, (long long)(cx1 - cx0) * (long long)(cy1 - cy0));
    for (int j = cy0; j < cy1; j++) {
        u32 *row = s->pixels + j * s->stride + cx0;
        u32 *src = c->pixels + (size_t)(j - y0) * rw + (cx0 - x0);
        for (int i = 0; i < cx1 - cx0; i++) row[i] = src[i];
    }
    return 1;
}

/* Restores just the part of slot `slot` that intersects [x,y,w,h]. Unlike
 * zan_gui_restore_rect the rect need not match the snapshot's geometry, so a
 * caller can repair many small damaged regions (e.g. the previous animation
 * frame's particles) without copying the whole snapshot back. Returns 1 while
 * the slot holds a valid snapshot for this surface size, 0 otherwise. */
EXPORT i32 zan_gui_restore_sub_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 slot) {
    if (surface_id < 0 || surface_id >= g_surface_count) return 0;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return 0;
    zan_blur_cache_t *c = zan_snap_slot(slot);
    if (!c) return 0;
    if (!c->valid || !c->pixels || c->sid != (int)surface_id) return 0;
    int x0 = clamp_i((int)x, c->x0, c->x0 + c->rw);
    int y0 = clamp_i((int)y, c->y0, c->y0 + c->rh);
    int x1 = clamp_i((int)(x + w), c->x0, c->x0 + c->rw);
    int y1 = clamp_i((int)(y + h), c->y0, c->y0 + c->rh);
    x0 = clamp_i(x0, 0, s->width);
    y0 = clamp_i(y0, 0, s->height);
    x1 = clamp_i(x1, 0, s->width);
    y1 = clamp_i(y1, 0, s->height);
    if (x1 <= x0 || y1 <= y0) return 1;
    for (int j = y0; j < y1; j++) {
        u32 *row = s->pixels + j * s->stride + x0;
        u32 *src = c->pixels + (size_t)(j - c->y0) * c->rw + (x0 - c->x0);
        for (int i = 0; i < x1 - x0; i++) row[i] = src[i];
    }
    return 1;
}

/* Frees the pixel buffer owned by a snapshot slot and marks it invalid, so the
 * memory a cached region holds (e.g. a chart embedded in a grid cell that just
 * scrolled out of view) is returned to the allocator instead of being retained
 * for the session. No-op on a free / out-of-range slot. */
EXPORT void zan_gui_surface_release(i32 surface_id, i32 slot) {
    (void)surface_id;
    zan_blur_cache_t *c = zan_snap_slot(slot);
    if (!c) return;
    free(c->pixels);
    c->pixels = NULL;
    c->cap = 0;
    c->valid = 0;
}

EXPORT void zan_gui_draw_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    u32 c = (u32)color;
    int t = (int)thickness;
    int ix = (int)x, iy = (int)y, iw = (int)w, ih = (int)h;
    /* top */ zan_gui_fill_rect(surface_id, ix, iy, iw, t, color);
    /* bottom */ zan_gui_fill_rect(surface_id, ix, iy + ih - t, iw, t, color);
    /* left */ zan_gui_fill_rect(surface_id, ix, iy + t, t, ih - 2*t, color);
    /* right */ zan_gui_fill_rect(surface_id, ix + iw - t, iy + t, t, ih - 2*t, color);
}

/* Anti-aliased rounded rectangle whose corners are rounded selectively:
 * `mask` is a bitset of ZAN_CORNER_TL/TR/BR/BL, so welded neighbours (the
 * buttons of a group, a tab against its panel) share a square seam while the
 * outer corners stay round. zan_gui_fill_rounded_rect is this with all four. */
#define ZAN_CORNER_TL 1
#define ZAN_CORNER_TR 2
#define ZAN_CORNER_BR 4
#define ZAN_CORNER_BL 8

EXPORT void zan_gui_fill_rounded_rect_mask(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 mask, i32 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    u32 c = (u32)color;
    int r = (int)radius;
    int ix = (int)x, iy = (int)y, iw = (int)w, ih = (int)h;
    int m = (int)mask;
    if (r <= 0 || (m & 15) == 0) { zan_gui_fill_rect(surface_id, x, y, w, h, color); return; }
    if (r > iw/2) r = iw/2;
    if (r > ih/2) r = ih/2;
    ZAN_STAT(g_st_round, (long long)iw * (long long)ih);

    /* Fill the interior as three rects that EXCLUDE the four r*r corner squares,
     * then draw each corner as a quarter circle over just its square. Every
     * pixel is thus written exactly once — filling the interior and full corner
     * circles would double-blend their overlap and darken corners for
     * translucent fills. */
    int xl = ix + r, xr = ix + iw - r;      /* inner corner-zone boundaries */
    int yt = iy + r, yb = iy + ih - r;
    zan_gui_fill_rect(surface_id, xl, iy, iw - 2*r, r, color);        /* top band  */
    zan_gui_fill_rect(surface_id, ix, yt, iw, ih - 2*r, color);      /* middle    */
    zan_gui_fill_rect(surface_id, xl, yb, iw - 2*r, r, color);        /* bottom band */

    /* Corner arcs: sample each pixel centre against the continuous corner
     * centre so all four corners share the same geometry (integer centres
     * biased TL/BR corners by half a pixel and read as lumpy edges). */
    double cc[4][2] = {
        {xl, yt},     /* TL, square px<xl, py<yt   */
        {xr, yt},     /* TR, square px>=xr, py<yt  */
        {xl, yb},     /* BL, square px<xl, py>=yb  */
        {xr, yb}      /* BR, square px>=xr, py>=yb */
    };
    int zx[4] = {ix, xr, ix, xr};
    int zy[4] = {iy, iy, yb, yb};
    int bit[4] = {ZAN_CORNER_TL, ZAN_CORNER_TR, ZAN_CORNER_BL, ZAN_CORNER_BR};
    for (int ci = 0; ci < 4; ci++) {
        if (!(m & bit[ci])) {
            /* Square corner: its r*r zone is plain fill. */
            zan_gui_fill_rect(surface_id, zx[ci], zy[ci], r, r, color);
            continue;
        }
        double ccx = cc[ci][0], ccy = cc[ci][1];
        for (int py = zy[ci]; py < zy[ci] + r; py++) {
            for (int px = zx[ci]; px < zx[ci] + r; px++) {
                double ddx = (double)px + 0.5 - ccx;
                double ddy = (double)py + 0.5 - ccy;
                double dist = sqrt(ddx*ddx + ddy*ddy);
                double cov = (double)r - dist + 0.5;
                if (cov >= 1.0) set_pixel(s, px, py, c);
                else if (cov > 0.0) set_pixel_aa(s, px, py, c, (int)(cov * 255.0));
            }
        }
    }
}

EXPORT void zan_gui_fill_rounded_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 color) {
    zan_gui_fill_rounded_rect_mask(surface_id, x, y, w, h, radius, 15, color);
}

/* Anti-aliased stroked rounded rectangle. Straight edges are crisp (solid
 * fill_rect between the corners); the four corner arcs are anti-aliased rings
 * that share the fill's corner centres/radius so the border hugs a matching
 * fill_rounded_rect exactly instead of the old square DrawRect outline. */
EXPORT void zan_gui_draw_rounded_rect_mask(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 mask, i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    u32 c = (u32)color;
    int ix = (int)x, iy = (int)y, iw = (int)w, ih = (int)h;
    int r = (int)radius, th = (int)thickness;
    int m = (int)mask;
    if (th < 1) th = 1;
    if (r <= 0 || (m & 15) == 0) { zan_gui_draw_rect(surface_id, x, y, w, h, color, thickness); return; }
    if (r > iw/2) r = iw/2;
    if (r > ih/2) r = ih/2;
    if (th > r) th = r;

    int xl = ix + r, xr = ix + iw - r;
    int yt = iy + r, yb = iy + ih - r;
    /* straight edges (exclude the r-wide corner zones) */
    zan_gui_fill_rect(surface_id, xl, iy, iw - 2*r, th, color);              /* top    */
    zan_gui_fill_rect(surface_id, xl, iy + ih - th, iw - 2*r, th, color);    /* bottom */
    zan_gui_fill_rect(surface_id, ix, yt, th, ih - 2*r, color);             /* left   */
    zan_gui_fill_rect(surface_id, ix + iw - th, yt, th, ih - 2*r, color);   /* right  */

    double cc[4][2] = {
        {xl, yt}, {xr, yt}, {xl, yb}, {xr, yb}
    };
    int zx[4] = {ix, xr, ix, xr};
    int zy[4] = {iy, iy, yb, yb};
    int bit[4] = {ZAN_CORNER_TL, ZAN_CORNER_TR, ZAN_CORNER_BL, ZAN_CORNER_BR};
    int hx[4] = {ix, xr, ix, xr};          /* square-corner horizontal arm */
    double rin = (double)r - (double)th;   /* inner arc radius */
    for (int ci = 0; ci < 4; ci++) {
        if (!(m & bit[ci])) {
            /* Square corner: the two straight edges meet as an L. */
            int cy = (ci < 2) ? zy[ci] : zy[ci] + r - th;
            int cx = (ci % 2 == 0) ? hx[ci] : hx[ci] + r - th;
            zan_gui_fill_rect(surface_id, zx[ci], cy, r, th, color);
            zan_gui_fill_rect(surface_id, cx, zy[ci], th, r, color);
            continue;
        }
        double ccx = cc[ci][0], ccy = cc[ci][1];
        for (int py = zy[ci]; py < zy[ci] + r; py++) {
            for (int px = zx[ci]; px < zx[ci] + r; px++) {
                double ddx = (double)px + 0.5 - ccx;
                double ddy = (double)py + 0.5 - ccy;
                double dist = sqrt(ddx*ddx + ddy*ddy);
                double outerCov = (double)r + 0.5 - dist;        /* 1 inside outer edge */
                double innerCov = dist - (rin - 0.5);            /* 1 outside inner edge */
                double cov = outerCov < innerCov ? outerCov : innerCov;
                if (cov <= 0.0) continue;
                if (cov >= 1.0) set_pixel(s, px, py, c);
                else set_pixel_aa(s, px, py, c, (int)(cov * 255.0));
            }
        }
    }
}

EXPORT void zan_gui_draw_rounded_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 color, i32 thickness) {
    zan_gui_draw_rounded_rect_mask(surface_id, x, y, w, h, radius, 15, color, thickness);
}

/* Anti-aliased filled circle */
EXPORT void zan_gui_fill_circle(i32 surface_id, i32 cx, i32 cy, i32 radius, i32 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    u32 c = (u32)color;
    int r = (int)radius;
    int icx = (int)cx, icy = (int)cy;

    for (int dy = -r - 1; dy <= r + 1; dy++) {
        for (int dx = -r - 1; dx <= r + 1; dx++) {
            double dist = sqrt((double)(dx*dx + dy*dy));
            if (dist <= r - 0.5) {
                set_pixel(s, icx + dx, icy + dy, c);
            } else if (dist <= r + 0.5) {
                int cov = (int)((r + 0.5 - dist) * 255.0);
                set_pixel_aa(s, icx + dx, icy + dy, c, cov);
            }
        }
    }
}

/* Anti-aliased circle outline: a ring of the given thickness centered on
 * the radius, with smooth coverage falloff at both edges. */
EXPORT void zan_gui_draw_circle(
    i32 surface_id, i32 cx, i32 cy, i32 radius, i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    u32 c = (u32)color;
    int r = (int)radius;
    if (r <= 0) return;
    double half = (double)(thickness > 0 ? thickness : 1) / 2.0;
    int ext = r + (int)half + 2;
    int icx = (int)cx, icy = (int)cy;

    for (int dy = -ext; dy <= ext; dy++) {
        for (int dx = -ext; dx <= ext; dx++) {
            double d = fabs(sqrt((double)(dx*dx + dy*dy)) - (double)r);
            if (d <= half - 0.5) {
                set_pixel(s, icx + dx, icy + dy, c);
            } else if (d <= half + 0.5) {
                int cov = (int)((half + 0.5 - d) * 255.0);
                set_pixel_aa(s, icx + dx, icy + dy, c, cov);
            }
        }
    }
}

/* Soft radial glow: a filled disc whose alpha fades smoothly from `inner_a`
 * (0..255) at the centre to 0 at `radius`, so it reads as a luminous bloom
 * rather than a hard-edged disc. Only the low 24 bits of `color` (RGB) are
 * used; `inner_a` drives the peak alpha. The falloff is a smoothstep raised to
 * a higher power to keep a bright, tight core with a long soft tail -- this is
 * the building block for specular highlights, spotlights and border glow. */
EXPORT void zan_gui_fill_radial(i32 surface_id, i32 cx, i32 cy, i32 radius, i32 color, i32 inner_a) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    int r = (int)radius;
    if (r <= 0) return;
    int icx = (int)cx, icy = (int)cy;
    u32 rgb = (u32)color & 0x00FFFFFFu;
    int peak = (int)inner_a;
    if (peak > 255) peak = 255;
    if (peak <= 0) return;
    int r2 = r * r;
    ZAN_STAT(g_st_radial, (long long)(2 * r + 1) * (long long)(2 * r + 1));
    /* Integer squared-distance falloff (no per-pixel sqrt/double): t is 256 at
     * the centre and 0 at the edge, squared for a bright tight core and a soft
     * tail. This is called hundreds of times per animated frame, so the hot
     * loop stays entirely in integer math. */
    for (int dy = -r; dy <= r; dy++) {
        int py = icy + dy;
        int dy2 = dy * dy;
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy2;
            if (d2 >= r2) continue;
            int t = ((r2 - d2) << 8) / r2;
            int a = (peak * t * t) >> 16;
            if (a <= 0) continue;
            set_pixel(s, icx + dx, py, ((u32)a << 24) | rgb);
        }
    }
}

/* Anti-aliased filled ring sector (pie / donut slice). Angles in degrees with
 * 0 at 12 o'clock, increasing clockwise. r_inner=0 gives a solid pie slice. */
EXPORT void zan_gui_fill_sector(
    i32 surface_id, i32 cx, i32 cy, i32 r_inner, i32 r_outer, i32 a0_deg,
    i32 a1_deg, i32 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    u32 c = (u32)color;
    int icx = (int)cx, icy = (int)cy;
    double ri = (double)r_inner, ro = (double)r_outer;
    double a0 = (double)a0_deg, a1 = (double)a1_deg;
    if (a1 < a0) { double tmp = a0; a0 = a1; a1 = tmp; }
    int R = (int)r_outer;
    const double PI = 3.14159265358979323846;

    for (int dy = -R - 1; dy <= R + 1; dy++) {
        for (int dx = -R - 1; dx <= R + 1; dx++) {
            double dist = sqrt((double)(dx*dx + dy*dy));
            /* radial coverage (AA on inner & outer edge) */
            double radCov = 1.0;
            if (dist > ro + 0.5) continue;
            if (ri > 0.0 && dist < ri - 0.5) continue;
            if (dist > ro - 0.5) radCov = ro + 0.5 - dist;
            else if (ri > 0.0 && dist < ri + 0.5) radCov = dist - (ri - 0.5);
            if (radCov <= 0.0) continue;
            if (radCov > 1.0) radCov = 1.0;
            /* angle of this pixel: 0 at top, clockwise, in [0,360) */
            double ang = atan2((double)dx, (double)(-dy)) * 180.0 / PI;
            if (ang < 0.0) ang += 360.0;
            /* Angular coverage: anti-alias the two radial (start/end) edges of
             * the sweep so slice sides are smooth, not stair-stepped. One pixel
             * of arc length subtends ~ (0.5/dist) radians; convert to degrees
             * and ramp coverage across that half-pixel band on each edge. Skip
             * for full turns so the closing seam does not double-darken. */
            double angCov = 1.0;
            double sweep = a1 - a0;
            if (sweep < 360.0) {
                double aa = ang;
                double dpix = 45.0;
                if (dist > 0.5) dpix = (0.5 / dist) * 180.0 / PI;
                if (aa < a0 - dpix) aa += 360.0;
                double dLo = aa - a0;   /* >0 inside from the start edge */
                double dHi = a1 - aa;   /* >0 inside from the end edge   */
                if (dLo <= -dpix || dHi <= -dpix) continue; /* fully outside */
                double covLo = (dLo + dpix) / (2.0 * dpix);
                double covHi = (dHi + dpix) / (2.0 * dpix);
                if (covLo > 1.0) covLo = 1.0; if (covLo < 0.0) covLo = 0.0;
                if (covHi > 1.0) covHi = 1.0; if (covHi < 0.0) covHi = 0.0;
                angCov = covLo * covHi;
                if (angCov <= 0.0) continue;
            }
            int cov = (int)(radCov * angCov * 255.0);
            if (cov >= 255) set_pixel(s, icx + dx, icy + dy, c);
            else set_pixel_aa(s, icx + dx, icy + dy, c, cov);
        }
    }
}

/* Anti-aliased line (Wu's algorithm) */
EXPORT void zan_gui_draw_line(i32 surface_id, i32 x0, i32 y0, i32 x1, i32 y1, i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    u32 c = (u32)color;
    int t = (int)thickness;

    if (t <= 1) {
        /* Wu's anti-aliased line */
        int steep = abs((int)(y1 - y0)) > abs((int)(x1 - x0));
        double fx0 = (double)x0, fy0 = (double)y0, fx1 = (double)x1, fy1 = (double)y1;
        if (steep) { double tmp; tmp = fx0; fx0 = fy0; fy0 = tmp; tmp = fx1; fx1 = fy1; fy1 = tmp; }
        if (fx0 > fx1) { double tmp; tmp = fx0; fx0 = fx1; fx1 = tmp; tmp = fy0; fy0 = fy1; fy1 = tmp; }
        double dx = fx1 - fx0, dy = fy1 - fy0;
        double gradient = (dx == 0) ? 1.0 : dy / dx;
        double intery = fy0 + gradient;

        int xpxl1 = (int)fx0, xpxl2 = (int)fx1;
        if (steep) {
            set_pixel(s, (int)fy0, xpxl1, c);
            set_pixel(s, (int)fy1, xpxl2, c);
        } else {
            set_pixel(s, xpxl1, (int)fy0, c);
            set_pixel(s, xpxl2, (int)fy1, c);
        }

        for (int xi = xpxl1 + 1; xi < xpxl2; xi++) {
            int iy = (int)intery;
            double fpart = intery - (double)iy;
            int c1 = (int)((1.0 - fpart) * 255);
            int c2 = (int)(fpart * 255);
            if (steep) {
                set_pixel_aa(s, iy, xi, c, c1);
                set_pixel_aa(s, iy + 1, xi, c, c2);
            } else {
                set_pixel_aa(s, xi, iy, c, c1);
                set_pixel_aa(s, xi, iy + 1, c, c2);
            }
            intery += gradient;
        }
    } else {
        /* Thick line: solid, round-capped, anti-aliased via distance-to-segment
         * coverage. Filling every pixel within half-thickness of the segment
         * (instead of sampling perpendicular offsets) avoids the gaps/stipple
         * that made diagonal strokes look blurry. */
        double dx = (double)(x1 - x0), dy = (double)(y1 - y0);
        double len2 = dx*dx + dy*dy;
        double half = t * 0.5;
        /* AA transition: fixed 1px band, identical to zan_gui_fill_circle
         * (cov = half + 0.5 - dist, solid inside half-0.5, 1px falloff).
         * The circle renderer is the reference that looks smooth at every
         * DPI precisely because its 1px ramp is in physical pixels and never
         * scales with the object size — the polyline must match it exactly,
         * not widen the ramp with the (already DPI-scaled) stroke width,
         * which would stretch the edge and read as a soft, jagged halo. */
        int lox = (x0 < x1 ? (int)x0 : (int)x1);
        int hix = (x0 > x1 ? (int)x0 : (int)x1);
        int loy = (y0 < y1 ? (int)y0 : (int)y1);
        int hiy = (y0 > y1 ? (int)y0 : (int)y1);
        int minx = lox - t - 1, maxx = hix + t + 1;
        int miny = loy - t - 1, maxy = hiy + t + 1;
        for (int py = miny; py <= maxy; py++) {
            for (int px = minx; px <= maxx; px++) {
                double proj = 0.0;
                if (len2 > 0.0001) {
                    proj = ((px - (double)x0) * dx + (py - (double)y0) * dy) / len2;
                    if (proj < 0.0) proj = 0.0;
                    if (proj > 1.0) proj = 1.0;
                }
                double cxp = (double)x0 + proj * dx;
                double cyp = (double)y0 + proj * dy;
                double ddx = px - cxp, ddy = py - cyp;
                double dist = sqrt(ddx*ddx + ddy*ddy);
                double cov = half + 0.5 - dist;   /* 1px band, like circle */
                if (cov <= 0.0) continue;
                int icov = aa_coverage(cov);
                if (icov >= 255) set_pixel(s, px, py, c);
                else set_pixel_aa(s, px, py, c, icov);
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * Whole-polyline anti-aliased stroke.
 *
 * The per-segment draw_line above leaves two aliasing artifacts on curves:
 *  1. each segment is anti-aliased independently, so pixels on the far side of
 *     a joint get only one segment's coverage -- corners look notched/jagged;
 *  2. a joint's two round caps overlap, and the overlap is blended TWICE
 *     (source-over is not idempotent), so corners look brighter/beadier.
 *
 * This entry point rasterises the whole path in ONE coverage pass: for every
 * pixel we take the MAX coverage over all segments it lies near (round join,
 * like the union of the segment strokes), then blend each touched pixel
 * exactly once. Max is the correct anti-aliased stroke semantics: coverage is
 * a monotone function of distance to the path, so max coverage == the closest
 * segment == the true distance to the polyline; accumulating instead would
 * fatten dense curves (a pixel near several short segments saturates) and
 * brighten joints. Segments share one scratch coverage buffer (reused across
 * calls, no per-frame allocation); only the pixels a pass actually touches
 * are stored in a dirty list and re-zeroed after blending, so consecutive
 * calls never see stale coverage.
 *
 * pts holds n*2 int32s: x0,y0,x1,y1,... in device pixels.
 * ------------------------------------------------------------------------ */
static u8  *g_poly_cov = NULL;    /* surface-sized coverage scratch (reused) */
static i32  g_poly_cov_w = 0;
static i32  g_poly_cov_h = 0;
static i32 *g_poly_dirty = NULL;  /* packed (y<<16)|x of pixels this pass touched */
static i32  g_poly_dirty_n = 0;
static i32  g_poly_dirty_cap = 0;

/* Shared polyline rasterizer. shift=0: pts are integer coords; shift=8: pts
 * are 16.8 fixed-point (1/256 px), so a Catmull-Rom curve can be handed over
 * with sub-pixel vertex precision. Sampling the distance field with
 * sub-pixel endpoints is what makes a shallow curve read as a smooth ramp
 * instead of the staircase of integer-rounded vertices (the filled-circle
 * rasterizer is smooth for the same reason: every pixel measures distance to
 * the ideal geometry, not to a quantized outline). */
static void zan_polyline_core(zan_surface_t *s, const i32 *pts, i32 n,
                              u32 c, int t, int shift) {
    int W = s->width, H = s->height;
    if (W <= 0 || H <= 0) return;
    double scale = shift == 0 ? 1.0 : (1.0 / 256.0);

    if (g_poly_cov_w < W || g_poly_cov_h < H) {
        free(g_poly_cov);
        g_poly_cov = (u8*)malloc((size_t)W * (size_t)H);
        g_poly_cov_w = W;
        g_poly_cov_h = H;
    }
    g_poly_dirty_n = 0;
    /* The first-touch marker below relies on a zero buffer (g_poly_cov[idx]==0
     * means "not seen this pass"). malloc leaves garbage behind, so a dirty
     * slot can be skipped and its pixel silently dropped -- clear every pass. */
    memset(g_poly_cov, 0, (size_t)W * (size_t)H);

    double half = t * 0.5;
    for (i32 i = 0; i + 1 < n; i++) {
        double x0 = pts[i * 2] * scale, y0 = pts[i * 2 + 1] * scale;
        double x1 = pts[i * 2 + 2] * scale, y1 = pts[i * 2 + 3] * scale;
        if (x0 == x1 && y0 == y1) continue;
        double dx = x1 - x0, dy = y1 - y0;
        double len2 = dx * dx + dy * dy;
        if (len2 < 0.0001) continue;
        int minx = (int)floor(x0 < x1 ? x0 : x1) - t - 2;
        int maxx = (int)ceil(x0 > x1 ? x0 : x1) + t + 2;
        int miny = (int)floor(y0 < y1 ? y0 : y1) - t - 2;
        int maxy = (int)ceil(y0 > y1 ? y0 : y1) + t + 2;
        if (minx < 0) minx = 0;
        if (miny < 0) miny = 0;
        if (maxx >= W) maxx = W - 1;
        if (maxy >= H) maxy = H - 1;
        for (int py = miny; py <= maxy; py++) {
            for (int px = minx; px <= maxx; px++) {
                double proj = ((px - x0) * dx + (py - y0) * dy) / len2;
                if (proj < 0.0) proj = 0.0;
                if (proj > 1.0) proj = 1.0;
                double cxp = x0 + proj * dx;
                double cyp = y0 + proj * dy;
                double ddx = px - cxp, ddy = py - cyp;
                double dist = sqrt(ddx * ddx + ddy * ddy);
                double cov = half + 0.5 - dist;   /* 1px band, like circle */
                if (cov <= 0.0) continue;
                int icov = aa_coverage(cov);
                int idx = py * W + px;
                if (g_poly_cov[idx] == 0) {       /* first touch this pass */
                    if (g_poly_dirty_n >= g_poly_dirty_cap) {
                        g_poly_dirty_cap = g_poly_dirty_cap ? g_poly_dirty_cap * 2 : 1024;
                        g_poly_dirty = (i32*)realloc(g_poly_dirty,
                            (size_t)g_poly_dirty_cap * sizeof(i32));
                    }
                    g_poly_dirty[g_poly_dirty_n++] = (py << 16) | (px & 0xFFFF);
                }
                /* Max coverage across segments: coverage is a monotone
                 * function of distance to the path, so the closest segment
                 * gives the true distance to the polyline. Accumulating
                 * saturates dense paths (smooth curves subdivide into many
                 * short segments; a pixel near several of them would hit 255
                 * and lose its anti-aliased edge). */
                if (icov > g_poly_cov[idx]) g_poly_cov[idx] = (u8)icov;
            }
        }
    }

    /* Single blend pass: every touched pixel exactly once. */
    for (i32 d = 0; d < g_poly_dirty_n; d++) {
        int px = g_poly_dirty[d] & 0xFFFF;
        int py = (g_poly_dirty[d] >> 16) & 0xFFFF;
        int idx = py * W + px;
        int cov = g_poly_cov[idx];
        g_poly_cov[idx] = 0;
        if (cov >= 255) set_pixel(s, px, py, c);
        else set_pixel_aa(s, px, py, c, cov);
    }
}

EXPORT void zan_gui_draw_polyline(i32 surface_id, const i32 *pts, i32 n,
                                  i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !pts || n < 2) return;
    int t = (int)thickness;
    if (t < 1) t = 1;
    zan_polyline_core(s, pts, n, (u32)color, t, 0);
}

/* Fixed-point polyline: vertex coordinates are 16.8 (1/256 px), so smooth
 * curve paths keep sub-pixel shape instead of snapping to the integer grid. */
EXPORT void zan_gui_draw_polyline_fx(i32 surface_id, const i32 *pts, i32 n,
                                     i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !pts || n < 2) return;
    int t = (int)thickness;
    if (t < 1) t = 1;
    zan_polyline_core(s, pts, n, (u32)color, t, 8);
}

EXPORT void *zan_gui_get_pixels(i32 surface_id) {
    if (surface_id < 0 || surface_id >= g_surface_count || !g_surfaces[surface_id]) return NULL;
    return (void *)g_surfaces[surface_id]->pixels;
}

/* ========================================================================
 * Sprite-sheet image blitter (stb_image, path-keyed FIFO cache)
 * ======================================================================== */
/* stb's thread-local globals (STBI_THREAD_LOCAL) fault on the IDE's statically
 * linked mingw build: reading stbi__vertically_flip_on_load segfaults before a
 * single pixel is decoded, so every wallpaper crashed the process. The image
 * cache below is only ever touched from the UI thread, so plain globals are
 * what we want anyway. */
#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_IMPLEMENTATION
#include "../../stdlib/SDL3/native/stb_image.h"

#define ZAN_IMG_CACHE_CAP 16
typedef struct {
    char path[512];
    u32 *pix;   /* ARGB32: (a<<24)|(r<<16)|(g<<8)|b */
    int  w, h;
} zan_img_t;
static zan_img_t g_imgs[ZAN_IMG_CACHE_CAP];
static int       g_img_n = 0;

static zan_img_t *zan_img_find(const char *path) {
    int i;
    for (i = 0; i < g_img_n; i++)
        if (strncmp(g_imgs[i].path, path, 511) == 0) return &g_imgs[i];
    return NULL;
}
/* Paths that failed to decode. Without this a broken or unsupported image is
 * re-decoded on every frame that paints the wallpaper. */
#define ZAN_IMG_BAD_CAP 16
static char g_img_bad[ZAN_IMG_BAD_CAP][512];
static int  g_img_bad_n = 0;
static int zan_img_is_bad(const char *path) {
    int i;
    for (i = 0; i < g_img_bad_n; i++)
        if (strncmp(g_img_bad[i], path, 511) == 0) return 1;
    return 0;
}
static void zan_img_mark_bad(const char *path) {
    if (zan_img_is_bad(path)) return;
    if (g_img_bad_n >= ZAN_IMG_BAD_CAP) {
        memmove(&g_img_bad[0], &g_img_bad[1], sizeof(g_img_bad[0]) * (ZAN_IMG_BAD_CAP - 1));
        g_img_bad_n = ZAN_IMG_BAD_CAP - 1;
    }
    strncpy(g_img_bad[g_img_bad_n], path, 511);
    g_img_bad[g_img_bad_n][511] = '\0';
    g_img_bad_n++;
}

static zan_img_t *zan_img_load(const char *path) {
    zan_img_t *e;
    int i, w, h, n;
    unsigned char *data;
    u32 *pix;
    if (!path || !path[0]) return NULL;
    e = zan_img_find(path);
    if (e) return e;
    if (zan_img_is_bad(path)) return NULL;
#ifdef _WIN32
    /* stbi_load goes through fopen, which on Windows interprets the bytes in
     * the ANSI code page -- so a UTF-8 path with non-ASCII characters (a
     * Chinese picture name, say) never opens. Read the bytes through the Win32
     * wide API and decode from memory: passing a FILE* across CRTs is not safe
     * here, and stb's file reader would fault on a foreign stream. */
    {
        int wn = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
        wchar_t *wp = wn > 0 ? (wchar_t *)malloc((size_t)wn * sizeof(wchar_t)) : NULL;
        HANDLE fh = INVALID_HANDLE_VALUE;
        LARGE_INTEGER fsz;
        unsigned char *bytes = NULL;
        DWORD got = 0;
        if (!wp) return NULL;
        MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, wn);
        fh = CreateFileW(wp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
        free(wp);
        if (fh == INVALID_HANDLE_VALUE) { zan_img_mark_bad(path); return NULL; }
        if (!GetFileSizeEx(fh, &fsz) || fsz.QuadPart <= 0
            || fsz.QuadPart > 268435456) {
            CloseHandle(fh); zan_img_mark_bad(path); return NULL;
        }
        bytes = (unsigned char *)malloc((size_t)fsz.QuadPart);
        if (!bytes) { CloseHandle(fh); return NULL; }
        if (!ReadFile(fh, bytes, (DWORD)fsz.QuadPart, &got, NULL)
            || got != (DWORD)fsz.QuadPart) {
            free(bytes); CloseHandle(fh); return NULL;
        }
        CloseHandle(fh);
        data = stbi_load_from_memory(bytes, (int)got, &w, &h, &n, 4);
        free(bytes);
    }
#else
    data = stbi_load(path, &w, &h, &n, 4);
#endif
    if (!data) { zan_img_mark_bad(path); return NULL; }
    /* Reject implausible dimensions before sizing the buffer: w*h*4 must not
     * overflow the allocation size. */
    if (w <= 0 || h <= 0 || w > 32768 || h > 32768) {
        stbi_image_free(data);
        zan_img_mark_bad(path);
        return NULL;
    }
    pix = (u32 *)malloc((size_t)w * (size_t)h * sizeof(u32));
    if (!pix) { stbi_image_free(data); return NULL; }
    for (i = 0; i < w * h; i++) {
        unsigned char r = data[i*4], g = data[i*4+1],
                      b = data[i*4+2], a = data[i*4+3];
        pix[i] = ((u32)a << 24) | ((u32)r << 16) | ((u32)g << 8) | b;
    }
    stbi_image_free(data);
    if (g_img_n >= ZAN_IMG_CACHE_CAP) {
        free(g_imgs[0].pix);
        memmove(&g_imgs[0], &g_imgs[1], sizeof(zan_img_t) * (ZAN_IMG_CACHE_CAP - 1));
        g_img_n = ZAN_IMG_CACHE_CAP - 1;
    }
    e = &g_imgs[g_img_n++];
    strncpy(e->path, path, 511); e->path[511] = '\0';
    e->pix = pix; e->w = w; e->h = h;
    return e;
}

EXPORT i32 zan_gui_image_width(const char *path) {
    zan_img_t *e = zan_img_load(path);
    return e ? (i64)e->w : 0;
}
EXPORT i32 zan_gui_image_height(const char *path) {
    zan_img_t *e = zan_img_load(path);
    return e ? (i64)e->h : 0;
}

/* Evict cached pixels for a path (call after the source file changes). */
EXPORT void zan_gui_image_evict(const char *path) {
    int i;
    if (!path) return;
    for (i = 0; i < g_img_bad_n; i++) {
        if (strncmp(g_img_bad[i], path, 511) == 0) {
            memmove(&g_img_bad[i], &g_img_bad[i+1],
                    sizeof(g_img_bad[0]) * (g_img_bad_n - i - 1));
            g_img_bad_n--;
            break;
        }
    }
    for (i = 0; i < g_img_n; i++) {
        if (strncmp(g_imgs[i].path, path, 511) == 0) {
            free(g_imgs[i].pix);
            memmove(&g_imgs[i], &g_imgs[i+1], sizeof(zan_img_t) * (g_img_n - i - 1));
            g_img_n--;
            return;
        }
    }
}

/* Blit a region of an image file onto a GUI surface (nearest-neighbour scale).
 * sx/sy/sw/sh = source rect in the image (all zero = use the full image).
 * dx/dy/dw/dh = destination rect on the surface. */
EXPORT void zan_gui_blit_image(
    i32 surf_id, const char *path, i32 dx, i32 dy, i32 dw, i32 dh, i32 sx,
    i32 sy, i32 sw, i32 sh)
{
    zan_img_t *img;
    zan_surface_t *s;
    int isx, isy, isw, ish, idx2, idy, idw, idh, x0, y0, x1, y1, py, px;
    if (surf_id < 0 || surf_id >= g_surface_count || !g_surfaces[surf_id]) return;
    img = zan_img_load(path);
    if (!img) return;
    s = g_surfaces[surf_id];
    isx = (int)sx; isy = (int)sy; isw = (int)sw; ish = (int)sh;
    if (isw <= 0) { isx = 0; isw = img->w; }
    if (ish <= 0) { isy = 0; ish = img->h; }
    idx2 = (int)dx; idy = (int)dy; idw = (int)dw; idh = (int)dh;
    if (idw <= 0) idw = isw;
    if (idh <= 0) idh = ish;
    x0 = idx2 > s->clip_x0 ? idx2 : s->clip_x0;
    y0 = idy  > s->clip_y0 ? idy  : s->clip_y0;
    x1 = idx2 + idw < s->clip_x1 ? idx2 + idw : s->clip_x1;
    y1 = idy  + idh < s->clip_y1 ? idy  + idh : s->clip_y1;
    ZAN_STAT(g_st_img, (long long)(x1 - x0) * (long long)(y1 - y0));
    for (py = y0; py < y1; py++) {
        int sry = isy + (py - idy) * ish / idh;
        if (sry < 0 || sry >= img->h) continue;
        for (px = x0; px < x1; px++) {
            int srx = isx + (px - idx2) * isw / idw;
            if (srx < 0 || srx >= img->w) continue;
            s->pixels[py * s->stride + px] =
                blend_over(s->pixels[py * s->stride + px], img->pix[sry * img->w + srx]);
        }
    }
}

/* ---- client-priority hit guards ----------------------------------------
 * The app draws controls flush with the window edge -- a scrollbar sits in
 * the last few pixels -- and the borderless frame's resize border on top of
 * one swallows every press meant for it, leaving the wheel as the only way to
 * scroll. A rect registered here makes the edge border yield to the client;
 * corner grips keep priority so a diagonal resize stays reachable. Rects are
 * client pixels of one window, re-registered by the app every frame; they are
 * kept per window because sibling windows register overlapping offsets.
 */
#define ZAN_GUI_MAX_HIT_GUARDS 64
static int g_hit_guards[ZAN_GUI_MAX_HIT_GUARDS][4];
static iptr g_hit_guard_win[ZAN_GUI_MAX_HIT_GUARDS];
static int g_hit_guard_count;

EXPORT i32 zan_gui_clear_hit_guards(iptr hwnd) {
    int n = 0;
    for (int i = 0; i < g_hit_guard_count; i++) {
        if (g_hit_guard_win[i] == hwnd) continue;
        if (n != i) {
            memcpy(g_hit_guards[n], g_hit_guards[i], sizeof(g_hit_guards[0]));
            g_hit_guard_win[n] = g_hit_guard_win[i];
        }
        n++;
    }
    g_hit_guard_count = n;
    return 0;
}

EXPORT i32 zan_gui_add_hit_guard(iptr hwnd, i32 x, i32 y, i32 w, i32 h) {
    if (w <= 0 || h <= 0) return 0;
    if (g_hit_guard_count >= ZAN_GUI_MAX_HIT_GUARDS) return 0;
    int *g = g_hit_guards[g_hit_guard_count];
    g_hit_guard_win[g_hit_guard_count++] = hwnd;
    g[0] = (int)x; g[1] = (int)y; g[2] = (int)w; g[3] = (int)h;
    return 0;
}

/* static inline: backends that draw no client-side frame never call it. */
static inline int zan_gui_in_hit_guard(iptr hwnd, int x, int y) {
    for (int i = 0; i < g_hit_guard_count; i++) {
        const int *g = g_hit_guards[i];
        if (g_hit_guard_win[i] != hwnd) continue;
        if (x >= g[0] && x < g[0] + g[2] && y >= g[1] && y < g[1] + g[3])
            return 1;
    }
    return 0;
}

/* ---- gui_runtime translation-unit parts (order matters) ----------------
 * The GUI runtime is split by backend/concern into the files below; they
 * are plain #include'd here so shared statics and preprocessor context
 * stay inside this single translation unit. Do not add them to CMake.
 */
#include "gui_runtime_text.c"
#include "gui_runtime_sdl.c"
#include "gui_runtime_x11.c"
#include "gui_runtime_font.c"
#include "gui_runtime_shims.c"
