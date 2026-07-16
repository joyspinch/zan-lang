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
typedef int64_t i64;
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

/* ---- Exported rendering functions ---- */

EXPORT i64 zan_gui_create_surface(i64 width, i64 height) {
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

EXPORT i64 zan_gui_destroy_surface(i64 id) {
    if (id < 0 || id >= g_surface_count || !g_surfaces[id]) return 1;
    free(g_surfaces[id]->pixels);
    free(g_surfaces[id]);
    g_surfaces[id] = NULL;
    /* Don't leave the resize re-blit pointing at a freed surface (it would read
     * a NULL slot at best, a recycled one at worst). */
    if (id == g_last_surface) g_last_surface = -1;
    return 0;
}

EXPORT i64 zan_gui_surface_width(i64 id) {
    if (id < 0 || id >= g_surface_count || !g_surfaces[id]) return 0;
    return g_surfaces[id]->width;
}

EXPORT i64 zan_gui_surface_height(i64 id) {
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

EXPORT void zan_gui_clear(i64 surface_id, i64 color) {
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
EXPORT void zan_gui_push_clip(i64 surface_id, i64 x, i64 y, i64 w, i64 h) {
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
EXPORT void zan_gui_pop_clip(i64 surface_id) {
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
EXPORT void zan_gui_reset_clip(i64 surface_id) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    clip_reset_full(s);
}

EXPORT void zan_gui_fill_rect(i64 surface_id, i64 x, i64 y, i64 w, i64 h, i64 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    u32 c = (u32)color;
    int x0 = clamp_i((int)x, 0, s->width);
    int y0 = clamp_i((int)y, 0, s->height);
    int x1 = clamp_i((int)(x + w), 0, s->width);
    int y1 = clamp_i((int)(y + h), 0, s->height);
    u32 sa = (c >> 24) & 0xFF;
    if (sa == 255) {
        for (int py = y0; py < y1; py++) {
            u32 *row = s->pixels + py * s->stride;
            for (int px = x0; px < x1; px++) row[px] = c;
        }
    } else {
        for (int py = y0; py < y1; py++)
            for (int px = x0; px < x1; px++)
                set_pixel(s, px, py, c);
    }
}

/* Vertical linear gradient fill: each row is a lerp between color_top (at y)
 * and color_bottom (at y+h-1). Opaque; used for modern gradient wallpapers
 * (AI / Aurora / Glass backdrops). Single pass, no allocation. */
EXPORT void zan_gui_fill_vgrad(i64 surface_id, i64 x, i64 y, i64 w, i64 h,
                               i64 color_top, i64 color_bottom) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    int x0 = clamp_i((int)x, 0, s->width);
    int y0 = clamp_i((int)y, 0, s->height);
    int x1 = clamp_i((int)(x + w), 0, s->width);
    int y1 = clamp_i((int)(y + h), 0, s->height);
    int rh = (int)h;
    if (rh < 1) rh = 1;
    int denom = rh > 1 ? rh - 1 : 1;
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

/* Backdrop blur: separable box blur (3 passes ~ Gaussian) over a rectangular
 * region, in place. Used to render frosted-glass panels — draw the backdrop,
 * blur the region behind a panel, then overlay a translucent tint. Alpha is
 * forced opaque (the backdrop under an overlay is opaque). Edge samples are
 * clamped to the region. Cost is O(passes * area), independent of radius. */
EXPORT void zan_gui_blur_rect(i64 surface_id, i64 x, i64 y, i64 w, i64 h, i64 radius) {
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

    /* Vibrancy (luma-preserving ~1.4x saturation) on the small copy so the
     * frost keeps the wallpaper's colour instead of washing to grey. */
    for (int i = 0; i < (int)dn; i++) {
        u32 px = a[i];
        int rc = (int)((px >> 16) & 0xFF);
        int gc = (int)((px >> 8) & 0xFF);
        int bc = (int)(px & 0xFF);
        int luma = (rc * 77 + gc * 150 + bc * 29) >> 8;
        rc = luma + (rc - luma) * 7 / 5;
        gc = luma + (gc - luma) * 7 / 5;
        bc = luma + (bc - luma) * 7 / 5;
        a[i] = 0xFF000000u | ((u32)clamp_i(rc, 0, 255) << 16)
             | ((u32)clamp_i(gc, 0, 255) << 8) | (u32)clamp_i(bc, 0, 255);
    }

    /* Upscale the small blurred copy back into the surface region. */
    if (ds == 1) {
        for (int j = 0; j < rh; j++) {
            u32 *row = s->pixels + (y0 + j) * s->stride + x0;
            u32 *src = a + j * dw;
            for (int i = 0; i < rw; i++) row[i] = src[i];
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
                row[i] = 0xFF000000u | ((u32)rC << 16) | ((u32)gC << 8) | (u32)bC;
            }
        }
    }
    free(a); free(b);
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
    int x0, y0, rw, rh, r;
    u32 *pixels;
    size_t cap;
} zan_blur_cache_t;
static zan_blur_cache_t g_blur_cache[ZAN_BLUR_CACHE_SLOTS];

EXPORT void zan_gui_blur_rect_cached(i64 surface_id, i64 x, i64 y, i64 w,
                                     i64 h, i64 radius, i64 slot, i64 dirty) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    if (slot < 0 || slot >= ZAN_BLUR_CACHE_SLOTS) {
        zan_gui_blur_rect(surface_id, x, y, w, h, radius);
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
    if (!dirty && c->valid && c->pixels
        && c->x0 == x0 && c->y0 == y0 && c->rw == rw && c->rh == rh
        && c->r == r) {
        for (int j = 0; j < rh; j++) {
            u32 *row = s->pixels + (y0 + j) * s->stride + x0;
            u32 *src = c->pixels + (size_t)j * rw;
            for (int i = 0; i < rw; i++) row[i] = src[i];
        }
        return;
    }

    /* Recompute in place (identical clamping), then snapshot into the slot. */
    zan_gui_blur_rect(surface_id, x, y, w, h, radius);
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
    c->valid = 1;
}

/* --- Static-backdrop snapshot cache ---------------------------------------
 * Even with the blur cached, an on-screen animation still re-paints the whole
 * page every frame -- and under glass the wallpaper (a gradient plus several
 * large alpha-blended blobs) is by far the most expensive part of that. These
 * slots snapshot a region's pixels after it is drawn; on animation-only frames
 * the caller restores the snapshot with a single copy instead of re-painting
 * the backdrop. Geometry must match (else restore reports failure and the
 * caller repaints normally, e.g. after a resize). */
#define ZAN_SNAP_CACHE_SLOTS 8
static zan_blur_cache_t g_snap_cache[ZAN_SNAP_CACHE_SLOTS];

EXPORT void zan_gui_snapshot_rect(i64 surface_id, i64 x, i64 y, i64 w,
                                  i64 h, i64 slot) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    if (slot < 0 || slot >= ZAN_SNAP_CACHE_SLOTS) return;
    int x0 = clamp_i((int)x, 0, s->width);
    int y0 = clamp_i((int)y, 0, s->height);
    int x1 = clamp_i((int)(x + w), 0, s->width);
    int y1 = clamp_i((int)(y + h), 0, s->height);
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return;
    zan_blur_cache_t *c = &g_snap_cache[slot];
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
    c->valid = 1;
}

EXPORT i64 zan_gui_restore_rect(i64 surface_id, i64 x, i64 y, i64 w,
                                i64 h, i64 slot) {
    if (surface_id < 0 || surface_id >= g_surface_count) return 0;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return 0;
    if (slot < 0 || slot >= ZAN_SNAP_CACHE_SLOTS) return 0;
    int x0 = clamp_i((int)x, 0, s->width);
    int y0 = clamp_i((int)y, 0, s->height);
    int x1 = clamp_i((int)(x + w), 0, s->width);
    int y1 = clamp_i((int)(y + h), 0, s->height);
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return 0;
    zan_blur_cache_t *c = &g_snap_cache[slot];
    if (!c->valid || !c->pixels
        || c->x0 != x0 || c->y0 != y0 || c->rw != rw || c->rh != rh) {
        return 0;
    }
    for (int j = 0; j < rh; j++) {
        u32 *row = s->pixels + (y0 + j) * s->stride + x0;
        u32 *src = c->pixels + (size_t)j * rw;
        for (int i = 0; i < rw; i++) row[i] = src[i];
    }
    return 1;
}

EXPORT void zan_gui_draw_rect(i64 surface_id, i64 x, i64 y, i64 w, i64 h, i64 color, i64 thickness) {
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

/* Anti-aliased rounded rectangle */
EXPORT void zan_gui_fill_rounded_rect(i64 surface_id, i64 x, i64 y, i64 w, i64 h, i64 radius, i64 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    u32 c = (u32)color;
    int r = (int)radius;
    int ix = (int)x, iy = (int)y, iw = (int)w, ih = (int)h;
    if (r <= 0) { zan_gui_fill_rect(surface_id, x, y, w, h, color); return; }
    if (r > iw/2) r = iw/2;
    if (r > ih/2) r = ih/2;

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

    int cc[4][2] = {
        {xl, yt},         /* TL, square px<xl, py<yt   */
        {xr - 1, yt},     /* TR, square px>=xr, py<yt  */
        {xl, yb - 1},     /* BL, square px<xl, py>=yb  */
        {xr - 1, yb - 1}  /* BR, square px>=xr, py>=yb */
    };
    int zx[4] = {ix, xr, ix, xr};
    int zy[4] = {iy, iy, yb, yb};
    for (int ci = 0; ci < 4; ci++) {
        int ccx = cc[ci][0], ccy = cc[ci][1];
        for (int py = zy[ci]; py < zy[ci] + r; py++) {
            for (int px = zx[ci]; px < zx[ci] + r; px++) {
                double ddx = px - ccx, ddy = py - ccy;
                double dist = sqrt(ddx*ddx + ddy*ddy);
                double cov = r + 0.5 - dist;
                if (cov <= 0.0) continue;
                if (cov > 1.0) cov = 1.0;
                set_pixel_aa(s, px, py, c, (int)(cov * 255.0));
            }
        }
    }
}

/* Anti-aliased filled circle */
EXPORT void zan_gui_fill_circle(i64 surface_id, i64 cx, i64 cy, i64 radius, i64 color) {
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

/* Anti-aliased filled ring sector (pie / donut slice). Angles in degrees with
 * 0 at 12 o'clock, increasing clockwise. r_inner=0 gives a solid pie slice. */
EXPORT void zan_gui_fill_sector(i64 surface_id, i64 cx, i64 cy, i64 r_inner, i64 r_outer,
                                i64 a0_deg, i64 a1_deg, i64 color) {
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
            /* test membership allowing the sweep to exceed 360 via wrap */
            int inside = 0;
            if (ang >= a0 && ang <= a1) inside = 1;
            else if ((ang + 360.0) >= a0 && (ang + 360.0) <= a1) inside = 1;
            if (!inside) continue;
            int cov = (int)(radCov * 255.0);
            if (cov >= 255) set_pixel(s, icx + dx, icy + dy, c);
            else set_pixel_aa(s, icx + dx, icy + dy, c, cov);
        }
    }
}

/* Anti-aliased line (Wu's algorithm) */
EXPORT void zan_gui_draw_line(i64 surface_id, i64 x0, i64 y0, i64 x1, i64 y1, i64 color, i64 thickness) {
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
                double cov = half - dist + 0.5;   /* ~1px anti-aliased edge */
                if (cov <= 0.0) continue;
                if (cov >= 1.0) set_pixel(s, px, py, c);
                else set_pixel_aa(s, px, py, c, (int)(cov * 255.0));
            }
        }
    }
}

EXPORT void *zan_gui_get_pixels(i64 surface_id) {
    if (surface_id < 0 || surface_id >= g_surface_count || !g_surfaces[surface_id]) return NULL;
    return (void *)g_surfaces[surface_id]->pixels;
}

/* ========================================================================
 * Text Rendering — Platform-specific (Win32: GDI, Linux: Xft/fallback)
 * ======================================================================== */

#ifdef _WIN32

static HDC g_text_dc = NULL;
static HFONT g_fonts[16]; /* cached fonts by size index */
static int g_font_count = 0;

static void ensure_text_dc(void) {
    if (!g_text_dc) {
        g_text_dc = CreateCompatibleDC(NULL);
        SetBkMode(g_text_dc, TRANSPARENT);
    }
}

static int g_font_sizes[16];

static HFONT get_or_create_font(int size) {
    /* Cache by exact size match */
    for (int i = 0; i < 16; i++) {
        if (g_font_sizes[i] == size && g_fonts[i]) return g_fonts[i];
    }
    /* Find empty slot */
    int slot = -1;
    for (int i = 0; i < 16; i++) {
        if (!g_fonts[i]) { slot = i; break; }
    }
    if (slot < 0) slot = 15; /* reuse last */

    if (g_fonts[slot]) DeleteObject(g_fonts[slot]);
    g_fonts[slot] = CreateFontW(
        -size, 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"Segoe UI"
    );
    g_font_sizes[slot] = size;
    return g_fonts[slot];
}

/* --- Rasterised text-run cache -------------------------------------------
 * zan_gui_draw_text is called many times per frame (one call per token/word)
 * and, while scrolling, with the *same* strings every frame -- only the Y
 * position moves. GDI-rendering each call (DIB alloc + TextOutW + GdiFlush)
 * then dominates CPU. Cache the white-on-black coverage bitmap keyed by
 * (text, size); the draw colour is applied at composite time, so it is not
 * part of the key and the same glyph run is reused for any colour. Entries
 * persist across frames, so scrolling only re-blits cached masks. */
typedef struct {
    char *text;   /* UTF-8 key; NULL marks an empty slot */
    int   size;
    int   tw, th;
    u32  *bits;   /* tw*th ARGB coverage straight from GDI */
    uint64_t used;   /* LRU tick */
} zan_text_cache_t;

#define ZAN_TEXT_CACHE_CAP 1024
#define ZAN_TEXT_CACHE_PROBE 8
static zan_text_cache_t g_tcache[ZAN_TEXT_CACHE_CAP];
static uint64_t g_tcache_clock = 0;

static uint64_t zan_text_hash(const char *s, int size) {
    uint64_t h = 1469598103934665603ULL;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        h ^= (uint64_t)(*p);
        h *= 1099511628211ULL;
    }
    h ^= (uint64_t)(unsigned)size;
    h *= 1099511628211ULL;
    return h;
}

/* Returns a cache entry whose bits hold the coverage mask, or NULL on error. */
static zan_text_cache_t *zan_text_cache_get(const char *text, int size) {
    uint64_t h = zan_text_hash(text, size);
    int base = (int)(h % ZAN_TEXT_CACHE_CAP);
    zan_text_cache_t *victim = NULL;
    for (int i = 0; i < ZAN_TEXT_CACHE_PROBE; i++) {
        zan_text_cache_t *e = &g_tcache[(base + i) % ZAN_TEXT_CACHE_CAP];
        if (e->text && e->size == size && strcmp(e->text, text) == 0) {
            e->used = ++g_tcache_clock;
            return e;
        }
        if (!e->text && !victim) victim = e;
    }
    if (!victim) {
        uint64_t best = ~0ULL;
        for (int i = 0; i < ZAN_TEXT_CACHE_PROBE; i++) {
            zan_text_cache_t *e = &g_tcache[(base + i) % ZAN_TEXT_CACHE_CAP];
            if (e->used <= best) { best = e->used; victim = e; }
        }
    }

    /* Miss: rasterise white text on black to capture the coverage mask. */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *wtext = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wtext) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, wlen);
    int text_len = wlen - 1;

    HFONT font = get_or_create_font(size);
    HFONT old_font = (HFONT)SelectObject(g_text_dc, font);
    SIZE ts;
    GetTextExtentPoint32W(g_text_dc, wtext, text_len, &ts);
    int tw = ts.cx + 2;
    int th = ts.cy + 2;
    if (tw <= 0 || th <= 0) {
        free(wtext); SelectObject(g_text_dc, old_font); return NULL;
    }

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = tw;
    bmi.bmiHeader.biHeight = -th;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void *dbits = NULL;
    HBITMAP hbmp = CreateDIBSection(g_text_dc, &bmi, DIB_RGB_COLORS, &dbits, NULL, 0);
    if (!hbmp) { free(wtext); SelectObject(g_text_dc, old_font); return NULL; }
    HBITMAP old_bmp = (HBITMAP)SelectObject(g_text_dc, hbmp);
    memset(dbits, 0, (size_t)(tw * th * 4));
    SetTextColor(g_text_dc, RGB(255, 255, 255));
    TextOutW(g_text_dc, 0, 0, wtext, text_len);
    GdiFlush();

    u32 *copy = (u32 *)malloc((size_t)(tw * th) * sizeof(u32));
    if (copy) memcpy(copy, dbits, (size_t)(tw * th) * sizeof(u32));

    SelectObject(g_text_dc, old_bmp);
    SelectObject(g_text_dc, old_font);
    DeleteObject(hbmp);
    free(wtext);
    if (!copy) return NULL;

    if (victim->text) { free(victim->text); victim->text = NULL; }
    if (victim->bits) { free(victim->bits); victim->bits = NULL; }
    size_t klen = strlen(text) + 1;
    victim->text = (char *)malloc(klen);
    if (!victim->text) { free(copy); return NULL; }
    memcpy(victim->text, text, klen);
    victim->size = size;
    victim->tw = tw;
    victim->th = th;
    victim->bits = copy;
    victim->used = ++g_tcache_clock;
    return victim;
}

EXPORT void zan_gui_draw_text(i64 surface_id, i64 x, i64 y,
                              const char *text, i64 color, i64 font_size) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !text || !*text) return;

    int size = (int)font_size;
    if (size < 8) size = 8;

    ensure_text_dc();

    zan_text_cache_t *e = zan_text_cache_get(text, size);
    if (!e || !e->bits) return;
    int tw = e->tw;
    int th = e->th;
    u32 *src = e->bits;

    /* Extract color components */
    u32 cr = ((u32)color >> 16) & 0xFF;
    u32 cg = ((u32)color >> 8) & 0xFF;
    u32 cb = (u32)color & 0xFF;
    u32 ca = ((u32)color >> 24) & 0xFF;
    if (ca == 0) ca = 255; /* treat 0 alpha as opaque for text */

    /* Composite cached coverage using per-channel ClearType AA. */
    for (int py = 0; py < th; py++) {
        int dst_y = (int)y + py;
        if (dst_y < s->clip_y0 || dst_y >= s->clip_y1) continue;
        for (int px = 0; px < tw; px++) {
            int dst_x = (int)x + px;
            if (dst_x < s->clip_x0 || dst_x >= s->clip_x1) continue;
            u32 sp = src[py * tw + px];
            u32 sr = (sp >> 16) & 0xFF;
            u32 sg = (sp >> 8) & 0xFF;
            u32 sb = sp & 0xFF;
            if (sr == 0 && sg == 0 && sb == 0) continue;
            /* Per-channel blend for subpixel AA */
            int idx = dst_y * s->stride + dst_x;
            u32 dp = s->pixels[idx];
            u32 dr = (dp >> 16) & 0xFF;
            u32 dg = (dp >> 8) & 0xFF;
            u32 db = dp & 0xFF;
            u32 ar = sr * ca / 255;
            u32 ag = sg * ca / 255;
            u32 ab = sb * ca / 255;
            u32 or_ = (cr * ar + dr * (255 - ar)) / 255;
            u32 og = (cg * ag + dg * (255 - ag)) / 255;
            u32 ob = (cb * ab + db * (255 - ab)) / 255;
            s->pixels[idx] = (255u << 24) | (or_ << 16) | (og << 8) | ob;
        }
    }
}

EXPORT i64 zan_gui_measure_text(const char *text, i64 font_size) {
    if (!text || !*text) return 0;
    int size = (int)font_size;
    if (size < 8) size = 8;

    ensure_text_dc();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *wtext = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, wlen);

    HFONT font = get_or_create_font(size);
    HFONT old_font = (HFONT)SelectObject(g_text_dc, font);
    SIZE text_size;
    GetTextExtentPoint32W(g_text_dc, wtext, wlen - 1, &text_size);
    SelectObject(g_text_dc, old_font);
    free(wtext);
    return (i64)text_size.cx;
}

EXPORT i64 zan_gui_font_height(i64 font_size) {
    int size = (int)font_size;
    if (size < 8) size = 8;

    ensure_text_dc();
    HFONT font = get_or_create_font(size);
    HFONT old_font = (HFONT)SelectObject(g_text_dc, font);
    TEXTMETRICW tm;
    GetTextMetricsW(g_text_dc, &tm);
    SelectObject(g_text_dc, old_font);
    return (i64)tm.tmHeight;
}


/* ========================================================================
 * Win32 Window Shell
 * ======================================================================== */
/* Compiled out when the unified SDL backend owns windowing (see ZAN_GUI_SDL
 * shell below); the Win32 GDI *text* rendering above still builds. */
#if !defined(ZAN_GUI_SDL)

static HWND g_main_hwnd = NULL;
/* Source window of the most recently reported event, so an app that owns more
 * than one top-level window (e.g. the IDE with a floating dialog) can route the
 * event to the right window. Set at the top of the window procedure. */
static HWND g_event_hwnd = NULL;
static int g_pending_event[8]; /* kind, x, y, button, keycode, modifiers, 0, 0 */
static int g_has_event = 0;
static int g_window_width = 0, g_window_height = 0;
/* g_last_surface: most recently presented surface, re-blitted synchronously on
 * WM_SIZE/WM_PAINT so a maximize/resize fills the new client area immediately
 * instead of flashing black. g_bg_color: last frame's clear color, used to fill
 * the newly-exposed margin during a live resize. Both declared near the top. */
/* --- Windows 10/11 acrylic backdrop --------------------------------------
 * When enabled, the window becomes a layered window presented with per-pixel
 * alpha (UpdateLayeredWindow) and the DWM composites a real blurred+tinted
 * acrylic of whatever is behind it wherever the surface alpha is < 255. The
 * app clears the surface transparent under a glass theme, so the desktop shows
 * through the gaps while opaque widgets/text stay crisp.
 *
 * Robustness: the present hands UpdateLayeredWindow a NULL destination point
 * so it only updates the *content*, never the window position — the OS keeps
 * ownership of where the window is. That is what makes a title-bar drag stay
 * in sync (an earlier version repositioned the window on every present from a
 * stale GetWindowRect, which fought the modal move loop and left the content
 * torn/offset after a drag). */
enum {
    ZAN_ACCENT_DISABLED = 0,
    ZAN_ACCENT_ENABLE_BLURBEHIND = 3,
    ZAN_ACCENT_ENABLE_ACRYLICBLURBEHIND = 4
};
typedef struct {
    int AccentState;
    int AccentFlags;
    unsigned int GradientColor; /* AABBGGRR */
    int AnimationId;
} ZAN_ACCENT_POLICY;
typedef struct {
    int Attrib;      /* WCA_ACCENT_POLICY == 19 */
    void *pvData;
    size_t cbData;
} ZAN_WINCOMPATTRDATA;
typedef BOOL (WINAPI *ZAN_pSetWCA)(HWND, ZAN_WINCOMPATTRDATA *);

static int g_glass_on = 0;
static u32 g_glass_tint = 0; /* ARGB from Zan (0 -> a light default) */

static void acrylic_apply(HWND hwnd, int state, u32 tint_argb) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    ZAN_pSetWCA setWCA = (ZAN_pSetWCA)(void *)
        GetProcAddress(user32, "SetWindowCompositionAttribute");
    if (!setWCA) return;
    u32 a = (tint_argb >> 24) & 0xFF, r = (tint_argb >> 16) & 0xFF;
    u32 g = (tint_argb >> 8) & 0xFF, b = tint_argb & 0xFF;
    ZAN_ACCENT_POLICY accent;
    accent.AccentState = state;
    accent.AccentFlags = 0;
    accent.GradientColor = (a << 24) | (b << 16) | (g << 8) | r; /* -> ABGR */
    accent.AnimationId = 0;
    ZAN_WINCOMPATTRDATA data;
    data.Attrib = 19; /* WCA_ACCENT_POLICY */
    data.pvData = &accent;
    data.cbData = sizeof(accent);
    setWCA(hwnd, &data);
}

/* Present a layered window: build a premultiplied top-down BGRA DIB from the
 * straight-alpha surface and hand it to the DWM via UpdateLayeredWindow. The
 * memory DC + DIB section are cached across frames (rebuilt only when the
 * surface size changes) so a live drag/animation doesn't churn GDI objects. */
static HDC g_lw_mem = NULL;
static HBITMAP g_lw_dib = NULL;
static HGDIOBJ g_lw_old = NULL;
static u32 *g_lw_bits = NULL;
static int g_lw_w = 0, g_lw_h = 0;

static void present_layered(HWND hwnd, zan_surface_t *s) {
    if (g_lw_mem == NULL || g_lw_w != s->width || g_lw_h != s->height) {
        if (g_lw_mem) {
            SelectObject(g_lw_mem, g_lw_old);
            DeleteObject(g_lw_dib);
            DeleteDC(g_lw_mem);
        }
        HDC screen = GetDC(NULL);
        g_lw_mem = CreateCompatibleDC(screen);
        ReleaseDC(NULL, screen);
        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = s->width;
        bmi.bmiHeader.biHeight = -(int)s->height; /* top-down */
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void *bits = NULL;
        g_lw_dib = CreateDIBSection(g_lw_mem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (!g_lw_dib) { DeleteDC(g_lw_mem); g_lw_mem = NULL; return; }
        g_lw_old = SelectObject(g_lw_mem, g_lw_dib);
        g_lw_bits = (u32 *)bits;
        g_lw_w = s->width;
        g_lw_h = s->height;
    }
    u32 *d = g_lw_bits;
    int n = s->width * s->height;
    for (int i = 0; i < n; i++) {
        u32 p = s->pixels[i];
        u32 a = (p >> 24) & 0xFF;
        u32 r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
        /* premultiply for AC_SRC_ALPHA */
        r = r * a / 255; g = g * a / 255; b = b * a / 255;
        d[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    HDC screen = GetDC(NULL);
    SIZE sz = { s->width, s->height };
    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION bf; bf.BlendOp = AC_SRC_OVER; bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255; bf.AlphaFormat = AC_SRC_ALPHA;
    /* pptDst = NULL: keep the OS-managed window position (never reposition on a
     * content update), so a drag never tears/offsets the window. */
    UpdateLayeredWindow(hwnd, screen, NULL, &sz, g_lw_mem, &ptSrc, 0, &bf, ULW_ALPHA);
    ReleaseDC(NULL, screen);
}

/* Enable native glass on a window. tint_argb: ARGB tint (alpha ~0x40-0xA0
 * reads as frost strength). Idempotent; safe to call every theme change. */
EXPORT i64 zan_gui_enable_glass(i64 hwnd_val, i64 tint_argb) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    if (!hwnd) return 1;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (!(ex & WS_EX_LAYERED)) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    }
    g_glass_tint = (u32)tint_argb;
    acrylic_apply(hwnd, ZAN_ACCENT_ENABLE_ACRYLICBLURBEHIND, g_glass_tint);
    g_glass_on = 1;
    /* Present the current frame immediately so the layered window has valid
     * content the moment the style flips (otherwise it can flash empty). */
    if (g_last_surface >= 0 && g_last_surface < g_surface_count
        && g_surfaces[g_last_surface])
        present_layered(hwnd, g_surfaces[g_last_surface]);
    return 0;
}

/* Disable native glass and drop the layered style, back to opaque GDI present. */
EXPORT i64 zan_gui_disable_glass(i64 hwnd_val) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    if (!hwnd) return 1;
    acrylic_apply(hwnd, ZAN_ACCENT_DISABLED, 0);
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
    g_glass_on = 0;
    return 0;
}

static void blit_surface_to_hwnd(HWND hwnd, zan_surface_t *s) {
    if (!s) return;
    if (g_glass_on) { present_layered(hwnd, s); return; }
    HDC hdc = GetDC(hwnd);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = s->width;
    bmi.bmiHeader.biHeight = -(int)s->height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    /* Blit the last frame crisply at 1:1 (never stretched — stretching looks
     * rubbery/blurry during a drag-resize). Any client area beyond the frame is
     * filled with the canvas background so a grow-resize shows solid bg in the
     * new region until the app thread repaints it crisply. */
    SetDIBitsToDevice(hdc, 0, 0, (DWORD)s->width, (DWORD)s->height,
        0, 0, 0, (UINT)s->height, s->pixels, &bmi, DIB_RGB_COLORS);
    if (g_window_width > (int)s->width || g_window_height > (int)s->height) {
        HBRUSH br = CreateSolidBrush(RGB((g_bg_color >> 16) & 0xFF,
                                         (g_bg_color >> 8) & 0xFF,
                                          g_bg_color & 0xFF));
        if (g_window_width > (int)s->width) {
            RECT r = { (int)s->width, 0, g_window_width, g_window_height };
            FillRect(hdc, &r, br);
        }
        if (g_window_height > (int)s->height) {
            RECT r = { 0, (int)s->height, (int)s->width, g_window_height };
            FillRect(hdc, &r, br);
        }
        DeleteObject(br);
    }
    ReleaseDC(hwnd, hdc);
}
/* Borderless (client-covers-everything) window chrome metrics, in device px. */
static int g_dpi = 96;
static int g_titlebar_h = 32;
static int g_btn_w = 46;
/* Number of caption buttons drawn on the right of the title bar (theme, pin,
 * minimize, maximize, close). The rightmost N*btn_w px are treated as client
 * area (clickable) rather than draggable caption. */
static int g_caption_btn_count = 5;

/* Caret position (client-area px) reported by the focused text widget so the
 * IME composition + candidate windows follow the cursor instead of pinning to
 * the window origin. Updated from Zan via zan_gui_set_ime_pos each frame. */
static int g_ime_x = 0;
static int g_ime_y = 0;

/* Move the active input context's composition + candidate windows to the caret.
 * Safe to call whenever; no-op if the window has no IMM context. */
static void zan_ime_reposition(HWND hwnd) {
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) { return; }
    COMPOSITIONFORM cf;
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = g_ime_x;
    cf.ptCurrentPos.y = g_ime_y;
    ImmSetCompositionWindow(himc, &cf);
    CANDIDATEFORM cand;
    cand.dwIndex = 0;
    cand.dwStyle = CFS_CANDIDATEPOS;
    cand.ptCurrentPos.x = g_ime_x;
    cand.ptCurrentPos.y = g_ime_y;
    cand.rcArea.left = 0;
    cand.rcArea.top = 0;
    cand.rcArea.right = 0;
    cand.rcArea.bottom = 0;
    ImmSetCandidateWindow(himc, &cand);
    ImmReleaseContext(hwnd, himc);
}

static LRESULT CALLBACK ZanGuiWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    g_event_hwnd = hwnd;
    switch (msg) {
    case WM_IME_STARTCOMPOSITION:
        /* Position the composition window at the caret before the IME draws it,
         * then let DefWindowProc run the normal composition handling. */
        zan_ime_reposition(hwnd);
        break;
    case WM_IME_COMPOSITION:
        zan_ime_reposition(hwnd);
        break;
    case WM_IME_SETCONTEXT:
        zan_ime_reposition(hwnd);
        break;
    case WM_DESTROY:
        g_pending_event[0] = 8;
        g_has_event = 1;
        /* Only tear the whole app down when the primary window closes; a
         * secondary window (dialog) just reports kind 8 for its own hwnd. */
        if (hwnd == g_main_hwnd) { PostQuitMessage(0); }
        return 0;
    case WM_ENTERSIZEMOVE:
        /* Full acrylic re-blurs the desktop on every move, so dragging can
         * stutter. Downgrade to plain blur-behind for the modal move/size loop:
         * it is far cheaper (smooth drag) yet still frosts the backdrop so the
         * window never looks see-through, then restore full acrylic on release. */
        if (g_glass_on) {
            acrylic_apply(hwnd, ZAN_ACCENT_ENABLE_BLURBEHIND, g_glass_tint);
        }
        break;
    case WM_EXITSIZEMOVE:
        if (g_glass_on) {
            acrylic_apply(hwnd, ZAN_ACCENT_ENABLE_ACRYLICBLURBEHIND, g_glass_tint);
            /* Re-present the current frame at the settled position so the
             * layered content is fresh and correctly aligned after the drag. */
            if (g_last_surface >= 0 && g_last_surface < g_surface_count)
                present_layered(hwnd, g_surfaces[g_last_surface]);
        }
        break;
    case WM_SIZE:
        g_window_width = LOWORD(lp);
        g_window_height = HIWORD(lp);
        /* Fill the resized client area now (last frame at 1:1 + bg-colored
         * margin) so a maximize/drag doesn't flash black before the app thread
         * repaints crisply. Skipped under native glass: a layered present here
         * would fight the resize with the old surface size. */
        if (!g_glass_on && g_last_surface >= 0 && g_last_surface < g_surface_count)
            blit_surface_to_hwnd(hwnd, g_surfaces[g_last_surface]);
        g_pending_event[0] = 7;
        g_pending_event[1] = g_window_width;
        g_pending_event[2] = g_window_height;
        g_has_event = 1;
        return 0;
    case WM_MOUSEMOVE:
        g_pending_event[0] = 1;
        g_pending_event[1] = (short)LOWORD(lp);
        g_pending_event[2] = (short)HIWORD(lp);
        g_has_event = 1;
        return 0;
    case WM_LBUTTONDOWN:
        g_pending_event[0] = 2; g_pending_event[3] = 0;
        g_pending_event[1] = (short)LOWORD(lp);
        g_pending_event[2] = (short)HIWORD(lp);
        g_pending_event[5] = (GetKeyState(VK_CONTROL) & 0x8000 ? 1 : 0) |
                             (GetKeyState(VK_SHIFT) & 0x8000 ? 2 : 0) |
                             (GetKeyState(VK_MENU) & 0x8000 ? 4 : 0);
        g_has_event = 1;
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        g_pending_event[0] = 3; g_pending_event[3] = 0;
        g_pending_event[1] = (short)LOWORD(lp);
        g_pending_event[2] = (short)HIWORD(lp);
        g_pending_event[5] = (GetKeyState(VK_CONTROL) & 0x8000 ? 1 : 0) |
                             (GetKeyState(VK_SHIFT) & 0x8000 ? 2 : 0) |
                             (GetKeyState(VK_MENU) & 0x8000 ? 4 : 0);
        g_has_event = 1;
        ReleaseCapture();
        return 0;
    case WM_RBUTTONDOWN:
        g_pending_event[0] = 2; g_pending_event[3] = 1;
        g_pending_event[1] = (short)LOWORD(lp);
        g_pending_event[2] = (short)HIWORD(lp);
        g_pending_event[5] = (GetKeyState(VK_CONTROL) & 0x8000 ? 1 : 0) |
                             (GetKeyState(VK_SHIFT) & 0x8000 ? 2 : 0) |
                             (GetKeyState(VK_MENU) & 0x8000 ? 4 : 0);
        g_has_event = 1;
        return 0;
    case WM_RBUTTONUP:
        g_pending_event[0] = 3; g_pending_event[3] = 1;
        g_pending_event[1] = (short)LOWORD(lp);
        g_pending_event[2] = (short)HIWORD(lp);
        g_has_event = 1;
        return 0;
    case WM_KEYDOWN:
        g_pending_event[0] = 4;
        g_pending_event[4] = (int)wp;
        g_pending_event[5] = (GetKeyState(VK_CONTROL) & 0x8000 ? 1 : 0) |
                             (GetKeyState(VK_SHIFT) & 0x8000 ? 2 : 0) |
                             (GetKeyState(VK_MENU) & 0x8000 ? 4 : 0);
        g_has_event = 1;
        return 0;
    case WM_KEYUP:
        g_pending_event[0] = 5;
        g_pending_event[4] = (int)wp;
        g_has_event = 1;
        return 0;
    case WM_CHAR:
        g_pending_event[0] = 6;
        g_pending_event[4] = (int)wp;
        g_has_event = 1;
        return 0;
    case WM_MOUSEWHEEL: {
        POINT wheel_pt = {
            (int)(short)LOWORD(lp), (int)(short)HIWORD(lp)
        };
        ScreenToClient(hwnd, &wheel_pt);
        g_pending_event[1] = wheel_pt.x;
        g_pending_event[2] = wheel_pt.y;
        g_pending_event[0] = 13;
        g_pending_event[4] = GET_WHEEL_DELTA_WPARAM(wp);
        g_has_event = 1;
        return 0;
    }
    case WM_NCCALCSIZE:
        /* Remove the standard window frame so the client area covers the whole
         * window (custom, themeable title bar). Keep the frame inset when
         * maximized so content is not clipped off-screen. */
        if (wp) {
            WINDOWPLACEMENT wpl; wpl.length = sizeof(wpl);
            if (GetWindowPlacement(hwnd, &wpl) && wpl.showCmd == SW_SHOWMAXIMIZED) {
                NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)lp;
                int fx = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                int fy = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                p->rgrc[0].left += fx;
                p->rgrc[0].right -= fx;
                p->rgrc[0].top += fy;
                p->rgrc[0].bottom -= fy;
            }
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        POINT pt; pt.x = (short)LOWORD(lp); pt.y = (short)HIWORD(lp);
        RECT rc; GetWindowRect(hwnd, &rc);
        int x = (int)pt.x - rc.left;
        int y = (int)pt.y - rc.top;
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        int b = 8 * g_dpi / 96;
        WINDOWPLACEMENT wpl; wpl.length = sizeof(wpl);
        int maxed = (GetWindowPlacement(hwnd, &wpl) && wpl.showCmd == SW_SHOWMAXIMIZED);
        int inButtons = (y < g_titlebar_h && x >= w - g_caption_btn_count * g_btn_w);
        if (!maxed && !inButtons) {
            int left = x < b, right = x >= w - b, top = y < b, bottom = y >= h - b;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }
        if (inButtons) return HTCLIENT;
        if (y < g_titlebar_h) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        /* Repaint from the last frame so OS-driven invalidations (move, uncover,
         * DWM) never leave the client black between app repaints. A layered
         * (glass) window keeps its own composited bitmap, so WM_PAINT need not
         * re-present it. */
        if (!g_glass_on && g_last_surface >= 0 && g_last_surface < g_surface_count)
            blit_surface_to_hwnd(hwnd, g_surfaces[g_last_surface]);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            return TRUE; /* prevent default cursor reset */
        }
        break;
    case WM_ERASEBKGND:
        return 1; /* prevent flicker */
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* Enable DPI awareness without a static Shcore.dll dependency so the binary
 * still loads on Windows 7. Prefer per-monitor awareness (SetProcessDpiAwareness,
 * Shcore.dll, Windows 8.1+); fall back to system-DPI awareness
 * (SetProcessDPIAware, user32.dll, Vista+) when Shcore is unavailable. */
static void zan_enable_dpi_awareness(void) {
    HMODULE shcore = LoadLibraryW(L"Shcore.dll");
    if (shcore) {
        typedef HRESULT (WINAPI *SetProcessDpiAwarenessFn)(PROCESS_DPI_AWARENESS);
        SetProcessDpiAwarenessFn set_awareness =
            (SetProcessDpiAwarenessFn)GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (set_awareness) {
            set_awareness(PROCESS_PER_MONITOR_DPI_AWARE);
            FreeLibrary(shcore);
            return;
        }
        FreeLibrary(shcore);
    }
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef BOOL (WINAPI *SetProcessDPIAwareFn)(void);
        SetProcessDPIAwareFn set_aware =
            (SetProcessDPIAwareFn)GetProcAddress(user32, "SetProcessDPIAware");
        if (set_aware) set_aware();
    }
}

EXPORT i64 zan_gui_create_window(const char *title, i64 width, i64 height) {
    static int registered = 0;
    static int dpi_set = 0;
    if (!dpi_set) {
        zan_enable_dpi_awareness();
        dpi_set = 1;
    }
    if (!registered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ZanGuiWndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorA(NULL, MAKEINTRESOURCEA(32512));
        wc.hbrBackground = NULL; /* no background brush - prevents flicker */
        /* App icon (resource id 1, embedded via zan.rc) for taskbar/alt-tab. */
        wc.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
        wc.hIconSm = wc.hIcon;
        wc.lpszClassName = L"ZanGuiApp";
        wc.style = CS_OWNDC | CS_DBLCLKS;
        RegisterClassExW(&wc);
        registered = 1;
    }

    int wtitle_len = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
    wchar_t *wtitle = (wchar_t *)malloc((size_t)wtitle_len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, wtitle_len);

    /* Scale requested size by system DPI */
    HDC screenDC = GetDC(NULL);
    int dpi = GetDeviceCaps(screenDC, LOGPIXELSX);
    ReleaseDC(NULL, screenDC);
    int scaledW = (int)width * dpi / 96;
    int scaledH = (int)height * dpi / 96;

    g_dpi = dpi;
    g_titlebar_h = 32 * dpi / 96;
    g_btn_w = 46 * dpi / 96;

    RECT r = {0, 0, (LONG)scaledW, (LONG)scaledH};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    g_window_width = scaledW;
    g_window_height = scaledH;

    HWND hwnd = CreateWindowExW(
        0, L"ZanGuiApp", wtitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        NULL, NULL, GetModuleHandleW(NULL), NULL
    );
    free(wtitle);
    if (!g_main_hwnd) {
        g_main_hwnd = hwnd;
    } else {
        RECT parent_rect;
        RECT child_rect;
        if (GetWindowRect(g_main_hwnd, &parent_rect) &&
            GetWindowRect(hwnd, &child_rect)) {
            int child_w = child_rect.right - child_rect.left;
            int child_h = child_rect.bottom - child_rect.top;
            int x = parent_rect.left +
                ((parent_rect.right - parent_rect.left) - child_w) / 2;
            int y = parent_rect.top +
                ((parent_rect.bottom - parent_rect.top) - child_h) / 2;
            MONITORINFO monitor = { sizeof(MONITORINFO) };
            HMONITOR hm = MonitorFromWindow(
                g_main_hwnd, MONITOR_DEFAULTTONEAREST);
            if (GetMonitorInfoW(hm, &monitor)) {
                if (x < monitor.rcWork.left) x = monitor.rcWork.left;
                if (y < monitor.rcWork.top) y = monitor.rcWork.top;
                if (x + child_w > monitor.rcWork.right)
                    x = monitor.rcWork.right - child_w;
                if (y + child_h > monitor.rcWork.bottom)
                    y = monitor.rcWork.bottom - child_h;
            }
            SetWindowPos(hwnd, NULL, x, y, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    /* Restore the system drop shadow on the borderless window. Extending the
     * DWM frame by a 1px sheet re-enables the shadow + rounded corners while
     * WM_NCCALCSIZE still hides the standard frame. */
    MARGINS shadow = {0, 0, 0, 1};
    DwmExtendFrameIntoClientArea(hwnd, &shadow);
    /* Apply the frame removal (WM_NCCALCSIZE) right away. */
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return (i64)(intptr_t)hwnd;
}

EXPORT i64 zan_gui_show_window(i64 hwnd_val) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    /* Ensure the window is focused so wheel/keyboard events route to it. */
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    return 0;
}

/* ---- Custom title bar window controls ---- */
EXPORT i64 zan_gui_minimize(i64 hwnd_val) {
    ShowWindow((HWND)(intptr_t)hwnd_val, SW_MINIMIZE);
    return 0;
}

EXPORT i64 zan_gui_toggle_maximize(i64 hwnd_val) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    WINDOWPLACEMENT wpl; wpl.length = sizeof(wpl);
    if (GetWindowPlacement(hwnd, &wpl) && wpl.showCmd == SW_SHOWMAXIMIZED) {
        ShowWindow(hwnd, SW_RESTORE);
    } else {
        ShowWindow(hwnd, SW_MAXIMIZE);
    }
    return 0;
}

EXPORT i64 zan_gui_close_window(i64 hwnd_val) {
    PostMessageW((HWND)(intptr_t)hwnd_val, WM_CLOSE, 0, 0);
    return 0;
}

EXPORT i64 zan_gui_is_maximized(i64 hwnd_val) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    WINDOWPLACEMENT wpl; wpl.length = sizeof(wpl);
    if (GetWindowPlacement(hwnd, &wpl) && wpl.showCmd == SW_SHOWMAXIMIZED) { return 1; }
    return 0;
}

EXPORT i64 zan_gui_titlebar_height(void) { return g_titlebar_h; }
EXPORT i64 zan_gui_caption_button_width(void) { return g_btn_w; }

/* Set the number of caption buttons so the draggable caption region excludes
 * exactly the button cluster on the right. */
EXPORT i64 zan_gui_set_caption_buttons(i64 hwnd_val, i64 count) {
    (void)hwnd_val;
    if (count >= 0 && count <= 8) { g_caption_btn_count = (int)count; }
    return 0;
}

/* Toggle always-on-top (topmost) state for the window. */
EXPORT i64 zan_gui_set_topmost(i64 hwnd_val, i64 on) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    HWND after = on ? HWND_TOPMOST : HWND_NOTOPMOST;
    SetWindowPos(hwnd, after, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return 0;
}

EXPORT i64 zan_gui_poll_event(void) {
    g_has_event = 0;
    memset(g_pending_event, 0, sizeof(g_pending_event));
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return -1;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (g_has_event) return 0;
    }
    return 1;
}

EXPORT i64 zan_gui_wait_event(void) {
    g_has_event = 0;
    memset(g_pending_event, 0, sizeof(g_pending_event));
    MSG msg;
    if (GetMessageW(&msg, NULL, 0, 0) <= 0) return -1;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
    return 0;
}

/* Wake a UI thread blocked in wait_event so it can drain the dispatch queue.
 * PostMessageW is documented thread-safe, so this is callable from any
 * thread. The posted WM_NULL carries no event; wait_event returns kind 0. */
EXPORT i64 zan_gui_wake(void) {
    if (g_main_hwnd) { PostMessageW(g_main_hwnd, WM_NULL, 0, 0); }
    return 0;
}

EXPORT i64 zan_gui_event_kind(void)    { return g_pending_event[0]; }
EXPORT i64 zan_gui_event_x(void)       { return g_pending_event[1]; }
EXPORT i64 zan_gui_event_y(void)       { return g_pending_event[2]; }
EXPORT i64 zan_gui_event_button(void)  { return g_pending_event[3]; }
EXPORT i64 zan_gui_event_keycode(void) { return g_pending_event[4]; }
EXPORT i64 zan_gui_event_mods(void)    { return g_pending_event[5]; }

EXPORT i64 zan_gui_window_width(void)  { return g_window_width; }
EXPORT i64 zan_gui_window_height(void) { return g_window_height; }

EXPORT i64 zan_gui_event_hwnd(void)    { return (i64)(intptr_t)g_event_hwnd; }

EXPORT i64 zan_gui_client_width(i64 hwnd_val) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    RECT rc;
    if (!hwnd || !GetClientRect(hwnd, &rc)) { return 0; }
    return rc.right - rc.left;
}

EXPORT i64 zan_gui_client_height(i64 hwnd_val) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    RECT rc;
    if (!hwnd || !GetClientRect(hwnd, &rc)) { return 0; }
    return rc.bottom - rc.top;
}

EXPORT i64 zan_gui_present(i64 hwnd_val, i64 surface_id) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    if (surface_id < 0 || surface_id >= g_surface_count || !g_surfaces[surface_id]) return 1;
    g_last_surface = surface_id;
    blit_surface_to_hwnd(hwnd, g_surfaces[surface_id]);
    return 0;
}

EXPORT i64 zan_gui_set_title(i64 hwnd_val, const char *title) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    int len = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
    wchar_t *wt = (wchar_t *)malloc((size_t)len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wt, len);
    SetWindowTextW(hwnd, wt);
    free(wt);
    return 0;
}

EXPORT i64 zan_gui_set_cursor(i64 cursor_type) {
    HCURSOR c;
    switch ((int)cursor_type) {
        case 0: c = LoadCursorA(NULL, MAKEINTRESOURCEA(32512)); break;
        case 1: c = LoadCursorA(NULL, MAKEINTRESOURCEA(32649)); break;
        case 2: c = LoadCursorA(NULL, MAKEINTRESOURCEA(32513)); break;
        case 3: c = LoadCursorA(NULL, MAKEINTRESOURCEA(32644)); break; /* SIZEWE */
        case 4: c = LoadCursorA(NULL, MAKEINTRESOURCEA(32645)); break; /* SIZENS */
        case 5: c = LoadCursorA(NULL, MAKEINTRESOURCEA(32515)); break; /* CROSS */
        default: c = LoadCursorA(NULL, MAKEINTRESOURCEA(32512)); break;
    }
    SetCursor(c);
    return 0;
}

EXPORT i64 zan_gui_get_dpi_scale(void) {
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    return (i64)(dpi * 100 / 96);
}

EXPORT i64 zan_gui_get_tick_ms(void) {
    /* GetTickCount64 only updates every ~10-16ms, so consecutive reads a frame
     * apart often return the same value -> per-frame dt jitters between 0 and
     * ~16ms and animations step unevenly (visible stutter). Use the
     * high-resolution performance counter for smooth, monotonic ms. */
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) { QueryPerformanceFrequency(&freq); }
    QueryPerformanceCounter(&now);
    return (i64)(now.QuadPart * 1000 / freq.QuadPart);
}

EXPORT void zan_gui_sleep_ms(i64 ms) {
    if (ms > 0) Sleep((DWORD)ms);
}

/* Copy a UTF-8 string to the Windows clipboard as CF_UNICODETEXT.
 * Returns 0 on success, -1 on failure. */
EXPORT i64 zan_gui_set_clipboard(const char *utf8) {
    if (!utf8) return -1;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wlen <= 0) return -1;
    HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, (size_t)wlen * sizeof(WCHAR));
    if (!hmem) return -1;
    WCHAR *dst = (WCHAR *)GlobalLock(hmem);
    if (!dst) { GlobalFree(hmem); return -1; }
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, dst, wlen);
    GlobalUnlock(hmem);
    if (!OpenClipboard(g_main_hwnd)) { GlobalFree(hmem); return -1; }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, hmem)) {
        CloseClipboard();
        GlobalFree(hmem);
        return -1;
    }
    CloseClipboard();
    /* Ownership of hmem transferred to the clipboard; do not free. */
    return 0;
}

/* Report the caret position (client-area px) so the IME composition/candidate
 * windows follow the cursor. Called each frame by the focused text widget. */
EXPORT void zan_gui_set_ime_pos(i64 x, i64 y) {
    g_ime_x = (int)x;
    g_ime_y = (int)y;
    if (g_event_hwnd) { zan_ime_reposition(g_event_hwnd); }
}

/* Read CF_UNICODETEXT from the Windows clipboard as a UTF-8 string. Returns a
 * pointer valid until the next call (the previous buffer is freed each time),
 * or "" when the clipboard holds no text. The Zan `string` ABI is a plain
 * NUL-terminated i8*, so a heap char* can be handed back directly. */
EXPORT const char *zan_gui_get_clipboard(void) {
    static char *g_clip_buf = NULL;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return "";
    if (!OpenClipboard(g_main_hwnd)) return "";
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) { CloseClipboard(); return ""; }
    WCHAR *w = (WCHAR *)GlobalLock(h);
    if (!w) { CloseClipboard(); return ""; }
    int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *nb = (char *)malloc((size_t)(need > 0 ? need : 1));
    if (nb) {
        if (need > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, nb, need, NULL, NULL);
        else nb[0] = '\0';
    }
    GlobalUnlock(h);
    CloseClipboard();
    if (!nb) return "";
    free(g_clip_buf);
    g_clip_buf = nb;
    return g_clip_buf;
}

/* Write a UTF-8 string to a file (overwrite), prefixed with a UTF-8 BOM so
 * spreadsheet apps detect the encoding (important for CJK CSV export).
 * Returns 0 on success, -1 on failure. */
EXPORT i64 zan_gui_write_file(const char *path, const char *utf8) {
    if (!path || !utf8) return -1;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wlen <= 0) return -1;
    WCHAR *wpath = (WCHAR *)malloc((size_t)wlen * sizeof(WCHAR));
    if (!wpath) return -1;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);
    HANDLE hf = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    free(wpath);
    if (hf == INVALID_HANDLE_VALUE) return -1;
    DWORD wr = 0;
    const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    WriteFile(hf, bom, 3, &wr, NULL);
    size_t len = strlen(utf8);
    BOOL ok = WriteFile(hf, utf8, (DWORD)len, &wr, NULL);
    CloseHandle(hf);
    if (!ok) return -1;
    return 0;
}

#endif /* !ZAN_GUI_SDL (Win32 window shell) */

#endif /* _WIN32 */

/* ========================================================================
 * Unified SDL3 Window Shell  (ZAN_GUI_SDL)
 * ------------------------------------------------------------------------
 * A single cross-platform windowing/event/present backend built on SDL3 —
 * the same stack the Game.* stdlib uses via zan_sdl3, so the IDE window is an
 * SDL window unified with games. The software rasterizer still paints into a
 * CPU surface; here that surface is uploaded to a per-window streaming texture
 * and presented through the SDL renderer (D3D11/Metal/Vulkan/GL, chosen by
 * SDL). The exported ABI is byte-for-byte the one Gui.Window imports, so no
 * Zan code changes. Borderless chrome + drag/resize is delivered through
 * SDL_SetWindowHitTest, mirroring the old WM_NCHITTEST regions. Event codes,
 * VK keycodes and modifier bits match the Win32 encoding Gui/App.zan expects.
 * ======================================================================== */

#ifdef ZAN_GUI_SDL

/* Borderless-window chrome metrics (device px), reported to Zan so its custom
 * title bar and caption-button hit regions line up with the drag/resize
 * regions the hit-test below carves out. */
static int g_dpi = 96;
static int g_titlebar_h = 32;
static int g_btn_w = 46;
static int g_caption_btn_count = 5;

/* Last reported event, in the shared int[8] protocol:
 * [0]=kind [1]=x [2]=y [3]=button [4]=keycode/char/delta [5]=modifiers. */
static int g_pending_event[8];
static SDL_Window *g_event_win = NULL; /* window that produced g_pending_event */
static SDL_Window *g_main_win = NULL;  /* first window: closing it quits */
static bool g_mouse_captured = false;  /* holding a client-widget drag */
static int g_window_width = 0, g_window_height = 0; /* last-resized size (px) */
static int g_sdl_ready = 0;
static int g_quit = 0;
static Uint32 g_wake_event = 0; /* user event type posted by zan_gui_wake */
static int g_ime_x = 0, g_ime_y = 0;

/* Per-window renderer + streaming texture registry. The handle handed back to
 * Zan is the SDL_Window* (cast to i64), exactly like the Win32 HWND was. */
typedef struct {
    SDL_Window   *win;
    SDL_Renderer *ren;
    SDL_Texture  *tex;
    int           tw, th;  /* current texture size */
    int           closed;  /* 1 once zan_gui_close_window hid it (pending free) */
    int           caption_btns; /* caption buttons this window draws (per-window
                                 * so a dialog with fewer buttons never corrupts
                                 * the main window's draggable/hit regions) */
} zan_sdl_win_t;
#define ZAN_SDL_MAX_WIN 32
static zan_sdl_win_t g_wins[ZAN_SDL_MAX_WIN];
static int g_win_count = 0;

static zan_sdl_win_t *sdl_find(SDL_Window *w) {
    for (int i = 0; i < g_win_count; i++)
        if (g_wins[i].win == w) return &g_wins[i];
    return NULL;
}

/* Destroy any window a prior zan_gui_close_window marked closed, freeing its
 * renderer/texture/window and compacting the registry so the 32-slot pool is
 * reclaimed. Win32 destroyed a window synchronously on WM_CLOSE; SDL defers to
 * here (called when the app is no longer inside that window's frame -- i.e. at
 * the next create/present) so we never free a window mid-use. */
static void sdl_reclaim_closed(void) {
    int i = 0;
    while (i < g_win_count) {
        if (g_wins[i].closed) {
            SDL_Window *w = g_wins[i].win;
            if (g_wins[i].tex) SDL_DestroyTexture(g_wins[i].tex);
            if (g_wins[i].ren) SDL_DestroyRenderer(g_wins[i].ren);
            if (w) SDL_DestroyWindow(w);
            if (g_event_win == w) g_event_win = NULL;
            for (int j = i + 1; j < g_win_count; j++) g_wins[j - 1] = g_wins[j];
            g_win_count--;
        } else {
            i++;
        }
    }
}

/* --- internal event queue: one SDL event may yield 0..N zan events (e.g. an
 * IME text commit expands to one kind-6 per codepoint). ------------------- */
typedef struct { int e[8]; SDL_Window *win; } zan_zev_t;
#define ZAN_ZQ_CAP 512
static zan_zev_t g_zq[ZAN_ZQ_CAP];
static int g_zq_head = 0, g_zq_tail = 0;

static int zq_empty(void) { return g_zq_head == g_zq_tail; }

static void zq_push(int kind, int x, int y, int button, int code, int mods,
                    SDL_Window *win) {
    /* Coalesce mouse-move floods: unlike Win32 (which collapses WM_MOUSEMOVE),
     * SDL emits one motion event per sample, so a fast drag can enqueue
     * hundreds. The app consumes one event per ~16ms frame, so an un-coalesced
     * backlog makes the UI (and hover highlights) lag seconds behind the
     * cursor. If the last unconsumed event is also a plain move on the same
     * window, overwrite it with the newer position instead of enqueuing. */
    if (kind == 1 && !zq_empty()) {
        int last = (g_zq_tail + ZAN_ZQ_CAP - 1) % ZAN_ZQ_CAP;
        zan_zev_t *p = &g_zq[last];
        if (p->e[0] == 1 && p->win == win) {
            p->e[1] = x; p->e[2] = y; p->e[5] = mods;
            return;
        }
    }
    int next = (g_zq_tail + 1) % ZAN_ZQ_CAP;
    if (next == g_zq_head) return; /* full: drop oldest-safe (never overwrite) */
    zan_zev_t *z = &g_zq[g_zq_tail];
    z->e[0] = kind; z->e[1] = x; z->e[2] = y; z->e[3] = button;
    z->e[4] = code; z->e[5] = mods; z->e[6] = 0; z->e[7] = 0;
    z->win = win;
    g_zq_tail = next;
}

/* Pop the front zan event into the reported slots. Caller checks !zq_empty. */
static void zq_pop(void) {
    zan_zev_t *z = &g_zq[g_zq_head];
    for (int i = 0; i < 8; i++) g_pending_event[i] = z->e[i];
    g_event_win = z->win;
    g_zq_head = (g_zq_head + 1) % ZAN_ZQ_CAP;
}

/* Map an SDL keycode to the Win32 virtual-key code Gui/Keys expects. Letters
 * and digits share their ASCII value with VK codes; the rest are switched. */
static int sdl_key_to_vk(SDL_Keycode k) {
    if (k >= 'a' && k <= 'z') return 65 + (int)(k - 'a'); /* VK 'A'..'Z' */
    if (k >= '0' && k <= '9') return (int)k;              /* VK '0'..'9' */
    switch (k) {
        case SDLK_ESCAPE:    return 27;
        case SDLK_RETURN:    return 13;
        case SDLK_KP_ENTER:  return 13;
        case SDLK_TAB:       return 9;
        case SDLK_BACKSPACE: return 8;
        case SDLK_DELETE:    return 46;
        case SDLK_INSERT:    return 45;
        case SDLK_LEFT:      return 37;
        case SDLK_UP:        return 38;
        case SDLK_RIGHT:     return 39;
        case SDLK_DOWN:      return 40;
        case SDLK_HOME:      return 36;
        case SDLK_END:       return 35;
        case SDLK_PAGEUP:    return 33;
        case SDLK_PAGEDOWN:  return 34;
        case SDLK_SPACE:     return 32;
        case SDLK_F1:  return 112; case SDLK_F2:  return 113;
        case SDLK_F3:  return 114; case SDLK_F4:  return 115;
        case SDLK_F5:  return 116; case SDLK_F6:  return 117;
        case SDLK_F7:  return 118; case SDLK_F8:  return 119;
        case SDLK_F9:  return 120; case SDLK_F10: return 121;
        case SDLK_F11: return 122; case SDLK_F12: return 123;
        default:             return (k < 128) ? (int)k : 0;
    }
}

static int sdl_mods_to_bits(SDL_Keymod m) {
    int r = 0;
    if (m & SDL_KMOD_CTRL)  r |= 1;
    if (m & SDL_KMOD_SHIFT) r |= 2;
    if (m & SDL_KMOD_ALT)   r |= 4;
    return r;
}

static int cur_mod_bits(void) { return sdl_mods_to_bits(SDL_GetModState()); }

/* Defined below; used by the event translator to decide whether a press should
 * capture the mouse (client widget) or hand off to the OS move/resize loop. */
static SDL_HitTestResult SDLCALL zan_sdl_hittest(SDL_Window *win,
                                                 const SDL_Point *area,
                                                 void *data);

/* Decode one UTF-8 codepoint from s (advancing *i); returns -1 at end. */
static int sdl_utf8_next(const char *s, int *i) {
    unsigned char c = (unsigned char)s[*i];
    if (c == 0) return -1;
    int cp, n;
    if (c < 0x80)      { cp = c;        n = 1; }
    else if (c < 0xE0) { cp = c & 0x1F; n = 2; }
    else if (c < 0xF0) { cp = c & 0x0F; n = 3; }
    else               { cp = c & 0x07; n = 4; }
    for (int k = 1; k < n; k++) {
        unsigned char cc = (unsigned char)s[*i + k];
        if ((cc & 0xC0) != 0x80) { n = k; break; }
        cp = (cp << 6) | (cc & 0x3F);
    }
    *i += n;
    return cp;
}

/* Translate one SDL event into 0..N queued zan events. */
static void sdl_translate(const SDL_Event *e) {
    switch (e->type) {
        case SDL_EVENT_QUIT:
            zq_push(8, 0, 0, 0, 0, 0, g_main_win);
            g_quit = 1;
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
            /* OS-initiated close (e.g. Alt+F4 on a borderless window). Same
             * handling as an app-driven Close(): quit on the main window, hide
             * + reclaim a secondary one so it disappears instead of lingering. */
            SDL_Window *w = SDL_GetWindowFromID(e->window.windowID);
            zq_push(8, 0, 0, 0, 0, 0, w);
            if (w == g_main_win) {
                g_quit = 1;
            } else {
                zan_sdl_win_t *rec = sdl_find(w);
                if (rec && !rec->closed) { SDL_HideWindow(w); rec->closed = 1; }
            }
            break;
        }
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
            SDL_Window *w = SDL_GetWindowFromID(e->window.windowID);
            int pw = 0, ph = 0;
            if (w) SDL_GetWindowSizeInPixels(w, &pw, &ph);
            if (pw > 0 && ph > 0) { g_window_width = pw; g_window_height = ph; }
            zq_push(7, pw, ph, 0, 0, 0, w);
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            SDL_Window *w = SDL_GetWindowFromID(e->motion.windowID);
            /* Safety net: if capture is somehow still held with no button down
             * (e.g. an OS move/resize loop swallowed the button-up), drop it so
             * the pointer can't get stuck captured. */
            if (g_mouse_captured && (e->motion.state & SDL_BUTTON_LMASK) == 0) {
                SDL_CaptureMouse(false);
                g_mouse_captured = false;
            }
            zq_push(1, (int)e->motion.x, (int)e->motion.y, 0, 0,
                    cur_mod_bits(), w);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            SDL_Window *w = SDL_GetWindowFromID(e->button.windowID);
            int btn = (e->button.button == SDL_BUTTON_RIGHT) ? 1 : 0;
            int down = (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            int kind = down ? 2 : 3;
            /* Mirror Win32 SetCapture/ReleaseCapture so a held widget drag
             * (slider/scrollbar/selection) keeps getting motion + the button-up
             * even after the pointer leaves the window. Only capture when the
             * press lands on a client widget (SDL_HITTEST_NORMAL): a press on
             * the draggable caption or a resize border starts an OS modal
             * move/resize loop that swallows the button-up, which would leave
             * the mouse captured forever and break all later input routing. */
            if (e->button.button == SDL_BUTTON_LEFT) {
                if (down) {
                    SDL_Point p = { (int)e->button.x, (int)e->button.y };
                    if (w && zan_sdl_hittest(w, &p, NULL) == SDL_HITTEST_NORMAL) {
                        SDL_CaptureMouse(true);
                        g_mouse_captured = true;
                    }
                } else if (g_mouse_captured) {
                    SDL_CaptureMouse(false);
                    g_mouse_captured = false;
                }
            }
            zq_push(kind, (int)e->button.x, (int)e->button.y, btn, 0,
                    cur_mod_bits(), w);
            break;
        }
        case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
            /* Clear widget hover when the pointer leaves the window. Skipped
             * while a button is held: that drag still owns the pointer via the
             * capture above and must keep tracking. */
            if (SDL_GetMouseState(NULL, NULL) == 0) {
                SDL_Window *w = SDL_GetWindowFromID(e->window.windowID);
                zq_push(1, -1, -1, 0, 0, cur_mod_bits(), w);
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            SDL_Window *w = SDL_GetWindowFromID(e->wheel.windowID);
            /* Report a Win32-style ±120 delta so Gui/App's /120 math holds. */
            int delta = (int)(e->wheel.y * 120.0f);
            zq_push(13, (int)e->wheel.mouse_x, (int)e->wheel.mouse_y, 0,
                    delta, cur_mod_bits(), w);
            break;
        }
        case SDL_EVENT_KEY_DOWN: {
            SDL_Window *w = SDL_GetWindowFromID(e->key.windowID);
            zq_push(4, 0, 0, 0, sdl_key_to_vk(e->key.key),
                    sdl_mods_to_bits(e->key.mod), w);
            break;
        }
        case SDL_EVENT_KEY_UP: {
            SDL_Window *w = SDL_GetWindowFromID(e->key.windowID);
            zq_push(5, 0, 0, 0, sdl_key_to_vk(e->key.key),
                    sdl_mods_to_bits(e->key.mod), w);
            break;
        }
        case SDL_EVENT_TEXT_INPUT: {
            SDL_Window *w = SDL_GetWindowFromID(e->text.windowID);
            const char *txt = e->text.text;
            int i = 0, cp;
            /* One kind-6 (WM_CHAR-equivalent) per codepoint of the commit. */
            while ((cp = sdl_utf8_next(txt, &i)) > 0)
                zq_push(6, 0, 0, 0, cp, 0, w);
            break;
        }
        default:
            /* g_wake_event and everything else: no zan event. */
            break;
    }
}

/* Borderless drag/resize regions — the SDL analogue of WM_NCHITTEST: an 8px
 * (DPI-scaled) resize border, a draggable caption strip minus the caption
 * button cluster on the right, everything else client. */
static SDL_HitTestResult SDLCALL zan_sdl_hittest(SDL_Window *win,
                                                 const SDL_Point *area,
                                                 void *data) {
    (void)data;
    int W = 0, H = 0;
    SDL_GetWindowSize(win, &W, &H);
    int x = area->x, y = area->y;
    int b = 8 * g_dpi / 96;
    int maxed = (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) ? 1 : 0;
    zan_sdl_win_t *rec = sdl_find(win);
    int nbtn = rec ? rec->caption_btns : g_caption_btn_count;
    int inButtons = (y < g_titlebar_h &&
                     x >= W - nbtn * g_btn_w);
    if (!maxed && !inButtons) {
        int left = x < b, right = x >= W - b, top = y < b, bottom = y >= H - b;
        if (top && left)     return SDL_HITTEST_RESIZE_TOPLEFT;
        if (top && right)    return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (bottom && left)  return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (left)   return SDL_HITTEST_RESIZE_LEFT;
        if (right)  return SDL_HITTEST_RESIZE_RIGHT;
        if (top)    return SDL_HITTEST_RESIZE_TOP;
        if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
    }
    if (inButtons) return SDL_HITTEST_NORMAL;
    if (y < g_titlebar_h) return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
}

static void zan_sdl_ensure_init(void) {
    if (g_sdl_ready) return;
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) return;
    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0.0f) scale = 1.0f;
    g_dpi = (int)(scale * 96.0f + 0.5f);
    g_titlebar_h = 32 * g_dpi / 96;
    g_btn_w = 46 * g_dpi / 96;
    g_wake_event = SDL_RegisterEvents(1);
    g_sdl_ready = 1;
}

EXPORT i64 zan_gui_create_window(const char *title, i64 width, i64 height) {
    zan_sdl_ensure_init();
    sdl_reclaim_closed(); /* free windows a previous Close() hid, reclaim slots */
    if (!g_sdl_ready || g_win_count >= ZAN_SDL_MAX_WIN) return 0;

    int w = (int)width * g_dpi / 96;
    int h = (int)height * g_dpi / 96;
    SDL_WindowFlags flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE |
                            SDL_WINDOW_HIDDEN;
    SDL_Window *win = SDL_CreateWindow(title ? title : "", w, h, flags);
    if (!win) return 0;
    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    if (!ren) { SDL_DestroyWindow(win); return 0; }
    /* App paces its own frames (needsRedraw + 16ms sleep); leave VSync off so a
     * present never blocks the UI thread up to a refresh interval. */
    SDL_SetRenderVSync(ren, 0);
    SDL_SetWindowHitTest(win, zan_sdl_hittest, NULL);

#ifdef _WIN32
    /* A borderless SDL window loses the OS drop shadow. SDL keeps WS_THICKFRAME
     * on a resizable window and hides the frame via WM_NCCALCSIZE, so extending
     * the DWM frame by a 1px sheet on the native HWND re-enables the system
     * shadow (and Win11 rounded corners) -- the same trick the old Win32 shell
     * used. The 1px is fully covered by the presented texture. */
    {
        HWND hwnd = (HWND)SDL_GetPointerProperty(
            SDL_GetWindowProperties(win),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        if (hwnd) {
            MARGINS m = {0, 0, 0, 1};
            DwmExtendFrameIntoClientArea(hwnd, &m);
        }
    }
#endif

    zan_sdl_win_t *rec = &g_wins[g_win_count++];
    rec->win = win; rec->ren = ren; rec->tex = NULL; rec->tw = rec->th = 0;
    rec->closed = 0;
    rec->caption_btns = g_caption_btn_count;

    if (!g_main_win) {
        g_main_win = win;
        g_window_width = w; g_window_height = h;
    } else {
        /* Center a secondary window (dialog) over the main window, clamped to
         * the display's usable area — mirrors the Win32 dialog placement. */
        int mx = 0, my = 0, mw = 0, mh = 0;
        SDL_GetWindowPosition(g_main_win, &mx, &my);
        SDL_GetWindowSize(g_main_win, &mw, &mh);
        int x = mx + (mw - w) / 2, y = my + (mh - h) / 2;
        SDL_Rect ub;
        if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(win), &ub)) {
            if (x < ub.x) x = ub.x;
            if (y < ub.y) y = ub.y;
            if (x + w > ub.x + ub.w) x = ub.x + ub.w - w;
            if (y + h > ub.y + ub.h) y = ub.y + ub.h - h;
        }
        SDL_SetWindowPosition(win, x, y);
    }
    return (i64)(intptr_t)win;
}

EXPORT i64 zan_gui_show_window(i64 hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    if (!win) return 1;
    SDL_ShowWindow(win);
    SDL_RaiseWindow(win);
    /* Route typed text (incl. IME commits) to this window. */
    SDL_StartTextInput(win);
    return 0;
}

EXPORT i64 zan_gui_minimize(i64 hwnd_val) {
    SDL_MinimizeWindow((SDL_Window *)(intptr_t)hwnd_val);
    return 0;
}

EXPORT i64 zan_gui_toggle_maximize(i64 hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    if (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) SDL_RestoreWindow(win);
    else SDL_MaximizeWindow(win);
    return 0;
}

EXPORT i64 zan_gui_close_window(i64 hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    /* Deliver the close (kind 8) so the owning App drops the window, mirroring
     * the async Win32 WM_CLOSE. Closing the main window quits the app; a
     * secondary window (dialog) must actually disappear -- SDL won't destroy it
     * for us the way Win32/X11 did -- so hide it now and mark it for reclaim on
     * the next create (the app is done with it once it has processed kind 8). */
    zq_push(8, 0, 0, 0, 0, 0, win);
    if (win == g_main_win) {
        g_quit = 1;
    } else {
        zan_sdl_win_t *rec = sdl_find(win);
        if (rec && !rec->closed) {
            SDL_HideWindow(win);
            rec->closed = 1;
        }
    }
    return 0;
}

EXPORT i64 zan_gui_is_maximized(i64 hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    return (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) ? 1 : 0;
}

EXPORT i64 zan_gui_titlebar_height(void) { return g_titlebar_h; }
EXPORT i64 zan_gui_caption_button_width(void) { return g_btn_w; }

EXPORT i64 zan_gui_set_caption_buttons(i64 hwnd_val, i64 count) {
    if (count < 0 || count > 8) return 0;
    /* Record per-window so each window's hit-test excludes exactly its own
     * caption cluster; a dialog with fewer buttons no longer leaves the main
     * window's drag/close regions wrong. Keep the global as a create default. */
    g_caption_btn_count = (int)count;
    zan_sdl_win_t *rec = sdl_find((SDL_Window *)(intptr_t)hwnd_val);
    if (rec) rec->caption_btns = (int)count;
    return 0;
}

EXPORT i64 zan_gui_set_topmost(i64 hwnd_val, i64 on) {
    SDL_SetWindowAlwaysOnTop((SDL_Window *)(intptr_t)hwnd_val, on ? true : false);
    return 0;
}

/* Drain every SDL event currently available into the zan queue. Motion events
 * coalesce in zq_push, so this collapses a burst of samples to the single
 * latest position and, crucially, never leaves motion piling up in SDL's own
 * queue between frames (which is what starved the one-event-per-frame app). */
static void sdl_drain(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) sdl_translate(&e);
}

EXPORT i64 zan_gui_poll_event(void) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    if (!g_sdl_ready) return 1;
    sdl_drain();
    if (zq_empty()) return (g_quit) ? -1 : 1;
    zq_pop();
    return 0;
}

EXPORT i64 zan_gui_wait_event(void) {
    memset(g_pending_event, 0, sizeof(g_pending_event));
    if (!g_sdl_ready) return -1;
    if (zq_empty()) {
        SDL_Event e;
        if (!SDL_WaitEvent(&e)) return -1;
        sdl_translate(&e);
        sdl_drain(); /* fold in anything already waiting behind it */
    }
    if (!zq_empty()) zq_pop(); /* else leaves kind 0 (e.g. a wake) */
    return 0;
}

EXPORT i64 zan_gui_wake(void) {
    if (!g_sdl_ready) return 0;
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = g_wake_event;
    SDL_PushEvent(&ev);
    return 0;
}

EXPORT i64 zan_gui_event_kind(void)    { return g_pending_event[0]; }
EXPORT i64 zan_gui_event_x(void)       { return g_pending_event[1]; }
EXPORT i64 zan_gui_event_y(void)       { return g_pending_event[2]; }
EXPORT i64 zan_gui_event_button(void)  { return g_pending_event[3]; }
EXPORT i64 zan_gui_event_keycode(void) { return g_pending_event[4]; }
EXPORT i64 zan_gui_event_mods(void)    { return g_pending_event[5]; }

EXPORT i64 zan_gui_window_width(void)  { return g_window_width; }
EXPORT i64 zan_gui_window_height(void) { return g_window_height; }

EXPORT i64 zan_gui_event_hwnd(void) { return (i64)(intptr_t)g_event_win; }

EXPORT i64 zan_gui_client_width(i64 hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    int w = 0, h = 0;
    if (!win || !SDL_GetWindowSizeInPixels(win, &w, &h)) return 0;
    return w;
}

EXPORT i64 zan_gui_client_height(i64 hwnd_val) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    int w = 0, h = 0;
    if (!win || !SDL_GetWindowSizeInPixels(win, &w, &h)) return 0;
    return h;
}

EXPORT i64 zan_gui_present(i64 hwnd_val, i64 surface_id) {
    SDL_Window *win = (SDL_Window *)(intptr_t)hwnd_val;
    if (surface_id < 0 || surface_id >= g_surface_count ||
        !g_surfaces[surface_id]) return 1;
    zan_sdl_win_t *rec = sdl_find(win);
    if (!rec) return 1;
    zan_surface_t *s = g_surfaces[surface_id];
    g_last_surface = surface_id;

    if (!rec->tex || rec->tw != s->width || rec->th != s->height) {
        if (rec->tex) SDL_DestroyTexture(rec->tex);
        rec->tex = SDL_CreateTexture(rec->ren, SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     s->width, s->height);
        if (!rec->tex) return 1;
        SDL_SetTextureScaleMode(rec->tex, SDL_SCALEMODE_NEAREST);
        rec->tw = s->width; rec->th = s->height;
    }
    SDL_UpdateTexture(rec->tex, NULL, s->pixels, s->width * 4);

    /* Clear the (possibly larger) window to the canvas background so a live
     * grow-resize shows solid bg rather than stretched/garbage pixels, then
     * blit the frame 1:1 — never stretched. */
    SDL_SetRenderDrawColor(rec->ren, (g_bg_color >> 16) & 0xFF,
                           (g_bg_color >> 8) & 0xFF, g_bg_color & 0xFF, 255);
    SDL_RenderClear(rec->ren);
    SDL_FRect dst = { 0.0f, 0.0f, (float)s->width, (float)s->height };
    SDL_RenderTexture(rec->ren, rec->tex, NULL, &dst);
    SDL_RenderPresent(rec->ren);
    return 0;
}

EXPORT i64 zan_gui_set_title(i64 hwnd_val, const char *title) {
    SDL_SetWindowTitle((SDL_Window *)(intptr_t)hwnd_val, title ? title : "");
    return 0;
}

EXPORT i64 zan_gui_set_cursor(i64 cursor_type) {
    static SDL_Cursor *cache[6];
    static int made = 0;
    if (!made) {
        cache[0] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        cache[1] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
        cache[2] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
        cache[3] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
        cache[4] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
        cache[5] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
        made = 1;
    }
    int t = (int)cursor_type;
    if (t < 0 || t > 5) t = 0;
    if (cache[t]) SDL_SetCursor(cache[t]);
    return 0;
}

EXPORT i64 zan_gui_get_dpi_scale(void) { return (i64)(g_dpi * 100 / 96); }

EXPORT i64 zan_gui_get_tick_ms(void) { return (i64)SDL_GetTicks(); }

EXPORT void zan_gui_sleep_ms(i64 ms) { if (ms > 0) SDL_Delay((Uint32)ms); }

EXPORT i64 zan_gui_set_clipboard(const char *utf8) {
    if (!utf8) return -1;
    return SDL_SetClipboardText(utf8) ? 0 : -1;
}

EXPORT const char *zan_gui_get_clipboard(void) {
    static char *g_clip_buf = NULL;
    if (!SDL_HasClipboardText()) return "";
    char *t = SDL_GetClipboardText(); /* SDL-allocated, must SDL_free */
    if (!t) return "";
    size_t n = strlen(t);
    char *nb = (char *)malloc(n + 1);
    if (!nb) { SDL_free(t); return ""; }
    memcpy(nb, t, n + 1);
    SDL_free(t);
    free(g_clip_buf);
    g_clip_buf = nb;
    return g_clip_buf;
}

EXPORT void zan_gui_set_ime_pos(i64 x, i64 y) {
    g_ime_x = (int)x; g_ime_y = (int)y;
    SDL_Window *win = g_event_win ? g_event_win : g_main_win;
    if (win) {
        SDL_Rect r = { (int)x, (int)y, 1, g_titlebar_h };
        SDL_SetTextInputArea(win, &r, 0);
    }
}

/* Native OS glass has no portable SDL equivalent; the software frosted-glass
 * baked into the surface still renders. These stay as safe no-ops so themes
 * that toggle glass keep working under the unified backend. */
EXPORT i64 zan_gui_enable_glass(i64 hwnd_val, i64 tint_argb) {
    (void)hwnd_val; (void)tint_argb; return 0;
}
EXPORT i64 zan_gui_disable_glass(i64 hwnd_val) { (void)hwnd_val; return 0; }

EXPORT i64 zan_gui_write_file(const char *path, const char *utf8) {
    if (!path || !utf8) return -1;
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) return -1;
    static const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    SDL_WriteIO(io, bom, 3);
    size_t len = strlen(utf8);
    size_t wr = (len > 0) ? SDL_WriteIO(io, utf8, len) : 0;
    SDL_CloseIO(io);
    return (wr == len) ? 0 : -1;
}

#endif /* ZAN_GUI_SDL */

/* ========================================================================
 * Linux X11 Window Shell
 * ======================================================================== */

/* Compiled out when the unified SDL backend owns windowing (ZAN_GUI_SDL); the
 * shared software rasterizer and FreeType/software text above still build. */
#if defined(__linux__) && !defined(ZAN_GUI_SDL)

static Display *g_display = NULL;
static Window g_x11_window = 0;
/* Self-pipe used to wake a UI thread blocked in wait_event from another
 * thread (Xlib is not thread-safe, so we can't post an X event off-thread;
 * writing a byte to a pipe that wait_event also polls is safe). */
static int g_wake_pipe[2] = { -1, -1 };
static int g_pending_event_linux[8];
static int g_win_w = 0, g_win_h = 0;
static char *g_clip_text_linux = NULL;
static char *g_clip_read_linux = NULL;
static Cursor g_cursors_linux[8];
static XIM g_xim = NULL;
static XIC g_xic = NULL;

/* Client-side decoration metrics for the borderless window with an app-drawn
 * title bar, mirroring the Win32 backend. */
static int g_scale_linux = 100;
static int g_titlebar_h_l = 32;
static int g_btn_w_l = 46;
static int g_caption_btn_count_l = 5;

/* Per-window state so one process can drive several top-level windows. The
 * globals above still track the primary window for process-wide operations
 * (clipboard, cursor, input method) and single-window size queries. */
#define ZAN_MAX_WINDOWS 16
typedef struct {
    Window xid;
    GC gc;
    XIC xic;
    Pixmap backbuf;
    int backbuf_w, backbuf_h;
    int w, h;
} zan_lwin_t;
static zan_lwin_t g_lwins[ZAN_MAX_WINDOWS];
static int g_lwin_count = 0;
static Window g_primary_win = 0;
/* xid of the window the event currently being decoded originated from. */
static Window g_evwin_linux = 0;

static zan_lwin_t *lwin_find(Window xid) {
    for (int i = 0; i < g_lwin_count; i++)
        if (g_lwins[i].xid == xid) return &g_lwins[i];
    return NULL;
}

i64 zan_gui_get_dpi_scale(void);
i64 zan_gui_is_maximized(i64 hwnd_val);

/* Decoded-event queue: a single XEvent can yield several ABI events (a key
 * press -> keyDown + textInput; a wheel button -> scroll), so decoded events
 * are buffered and drained one per poll/wait call, mirroring the Win32 message
 * queue. Slots: [kind, x, y, button, keycode, mods, 0, 0]. */
#define ZAN_EVQ_CAP 64
static int g_evq_linux[ZAN_EVQ_CAP][8];
static int g_evq_head_linux = 0, g_evq_tail_linux = 0;

static void evq_push_linux(int kind, int x, int y, int button, int keycode, int mods) {
    int next = (g_evq_tail_linux + 1) % ZAN_EVQ_CAP;
    if (next == g_evq_head_linux) return; /* queue full: drop */
    int *e = g_evq_linux[g_evq_tail_linux];
    e[0] = kind; e[1] = x; e[2] = y; e[3] = button;
    e[4] = keycode; e[5] = mods; e[6] = (int)g_evwin_linux; e[7] = 0;
    g_evq_tail_linux = next;
}

static int evq_pop_linux(void) {
    if (g_evq_head_linux == g_evq_tail_linux) return 0;
    memcpy(g_pending_event_linux, g_evq_linux[g_evq_head_linux],
           sizeof(g_pending_event_linux));
    g_evq_head_linux = (g_evq_head_linux + 1) % ZAN_EVQ_CAP;
    return 1;
}

/* Decode one UTF-8 scalar, advancing *p past it. */
static unsigned x11_utf8_next(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    unsigned cp = *s;
    if (cp < 0x80) { *p += 1; return cp; }
    else if ((cp >> 5) == 0x6) { cp = ((cp & 0x1F) << 6) | (s[1] & 0x3F); *p += 2; }
    else if ((cp >> 4) == 0xE) { cp = ((cp & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); *p += 3; }
    else if ((cp >> 3) == 0x1E) { cp = ((cp & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); *p += 4; }
    else { *p += 1; }
    return cp;
}

/* X11 modifier state -> Win32 encoding (bit0=Ctrl, bit1=Shift, bit2=Alt). */
static int x11_mods(unsigned int state) {
    int m = 0;
    if (state & ControlMask) m |= 1;
    if (state & ShiftMask)   m |= 2;
    if (state & Mod1Mask)    m |= 4;
    return m;
}

/* X keysym -> Windows VK code the Zan `Keys` constants use. */
static int x11_vk_from_keysym(KeySym ks) {
    switch (ks) {
    case XK_Escape:    return 27;
    case XK_Return:
    case XK_KP_Enter:  return 13;
    case XK_Tab:       return 9;
    case XK_BackSpace: return 8;
    case XK_Delete:    return 46;
    case XK_Left:      return 37;
    case XK_Up:        return 38;
    case XK_Right:     return 39;
    case XK_Down:      return 40;
    case XK_Home:      return 36;
    case XK_End:       return 35;
    case XK_Prior:     return 33; /* PageUp */
    case XK_Next:      return 34; /* PageDown */
    case XK_space:     return 32;
    case XK_F1:        return 112;
    case XK_F2:        return 113;
    case XK_F5:        return 116;
    case XK_F11:       return 122;
    case XK_Shift_L:
    case XK_Shift_R:   return 16;
    case XK_Control_L:
    case XK_Control_R: return 17;
    case XK_Alt_L:
    case XK_Alt_R:     return 18;
    default: break;
    }
    if (ks >= XK_a && ks <= XK_z) return (int)(ks - XK_a + 'A');
    if ((ks >= XK_A && ks <= XK_Z) || (ks >= XK_0 && ks <= XK_9)) return (int)ks;
    return (int)ks;
}

static Atom x11_atom(const char *name) { return XInternAtom(g_display, name, False); }

/* Toggle/set an EWMH _NET_WM_STATE property via the window manager.
 * action: 0 = remove, 1 = add, 2 = toggle. state2 may be 0. */
static void x11_wm_state(Window win, Atom state1, Atom state2, long action) {
    if (!g_display || !win) return;
    XEvent xev;
    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.xclient.window = win;
    xev.xclient.message_type = x11_atom("_NET_WM_STATE");
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = action;
    xev.xclient.data.l[1] = (long)state1;
    xev.xclient.data.l[2] = (long)state2;
    xev.xclient.data.l[3] = 1;
    XSendEvent(g_display, DefaultRootWindow(g_display), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &xev);
    XFlush(g_display);
}

/* _NET_WM_MOVERESIZE directions. */
#define ZAN_NWMR_TOPLEFT     0
#define ZAN_NWMR_TOP         1
#define ZAN_NWMR_TOPRIGHT    2
#define ZAN_NWMR_RIGHT       3
#define ZAN_NWMR_BOTTOMRIGHT 4
#define ZAN_NWMR_BOTTOM      5
#define ZAN_NWMR_BOTTOMLEFT  6
#define ZAN_NWMR_LEFT        7
#define ZAN_NWMR_MOVE        8

/* Remove window-manager decorations via the Motif hint so the app can draw its
 * own title bar (matching the Win32 borderless window). */
static void x11_set_borderless(Window win) {
    if (!g_display || !win) return;
    struct {
        unsigned long flags, functions, decorations;
        long input_mode;
        unsigned long status;
    } hints;
    memset(&hints, 0, sizeof(hints));
    hints.flags = (1L << 1); /* MWM_HINTS_DECORATIONS */
    hints.decorations = 0;
    Atom a = x11_atom("_MOTIF_WM_HINTS");
    XChangeProperty(g_display, win, a, a, 32, PropModeReplace,
                    (unsigned char *)&hints, 5);
}

/* Ask the window manager to start an interactive move/resize, so the app-drawn
 * caption and resize borders behave like real ones. */
static void x11_start_moveresize(Window win, int x_root, int y_root, int direction) {
    if (!g_display || !win) return;
    XUngrabPointer(g_display, CurrentTime);
    XEvent xev;
    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.xclient.window = win;
    xev.xclient.message_type = x11_atom("_NET_WM_MOVERESIZE");
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = x_root;
    xev.xclient.data.l[1] = y_root;
    xev.xclient.data.l[2] = direction;
    xev.xclient.data.l[3] = 1; /* button 1 */
    xev.xclient.data.l[4] = 1; /* source indication: application */
    XSendEvent(g_display, DefaultRootWindow(g_display), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &xev);
    XFlush(g_display);
}

/* Map a left press to a move/resize direction, mirroring the Win32
 * WM_NCHITTEST logic: 8px resize borders plus a draggable caption that excludes
 * the caption-button cluster. Returns -1 for an ordinary client click. */
static int x11_caption_hit(zan_lwin_t *lw, int x, int y) {
    int w = lw->w, h = lw->h;
    int capW = g_caption_btn_count_l * g_btn_w_l;
    int inCaptionDrag = (y < g_titlebar_h_l && x < w - capW);
    if (zan_gui_is_maximized((i64)lw->xid)) {
        return inCaptionDrag ? ZAN_NWMR_MOVE : -1;
    }
    int b = 8 * g_scale_linux / 100;
    if (b < 4) b = 4;
    int left = x < b, right = x >= w - b, top = y < b, bottom = y >= h - b;
    if (top && left) return ZAN_NWMR_TOPLEFT;
    if (top && right) return ZAN_NWMR_TOPRIGHT;
    if (bottom && left) return ZAN_NWMR_BOTTOMLEFT;
    if (bottom && right) return ZAN_NWMR_BOTTOMRIGHT;
    if (left) return ZAN_NWMR_LEFT;
    if (right) return ZAN_NWMR_RIGHT;
    if (top) return ZAN_NWMR_TOP;
    if (bottom) return ZAN_NWMR_BOTTOM;
    if (inCaptionDrag) return ZAN_NWMR_MOVE;
    return -1;
}

/* Serve a clipboard paste request from another client (we own CLIPBOARD). */
static void x11_serve_selection(XSelectionRequestEvent *req) {
    XSelectionEvent resp;
    memset(&resp, 0, sizeof(resp));
    resp.type = SelectionNotify;
    resp.display = req->display;
    resp.requestor = req->requestor;
    resp.selection = req->selection;
    resp.target = req->target;
    resp.time = req->time;
    resp.property = req->property ? req->property : req->target;

    Atom a_utf8 = x11_atom("UTF8_STRING");
    Atom a_targets = x11_atom("TARGETS");
    const char *txt = g_clip_text_linux ? g_clip_text_linux : "";

    if (req->target == a_targets) {
        Atom offered[3] = { a_targets, a_utf8, XA_STRING };
        XChangeProperty(g_display, req->requestor, resp.property, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)offered, 3);
    } else if (req->target == a_utf8 || req->target == XA_STRING) {
        XChangeProperty(g_display, req->requestor, resp.property, req->target, 8,
                        PropModeReplace, (const unsigned char *)txt, (int)strlen(txt));
    } else {
        resp.property = None;
    }
    XSendEvent(g_display, req->requestor, False, 0, (XEvent *)&resp);
    XFlush(g_display);
}

/* Decode a raw XEvent into zero or more queued ABI events. */
static void x11_translate_event(XEvent *ev) {
    g_evwin_linux = ev->xany.window;
    switch (ev->type) {
    case MotionNotify:
        evq_push_linux(1, ev->xmotion.x, ev->xmotion.y, 0, 0,
                       x11_mods(ev->xmotion.state));
        break;
    case ButtonPress:
        /* Buttons 4/5 are the vertical wheel; report them as scroll (kind 13)
         * with a Win32-style +/-120 delta. Buttons 6/7 (horizontal) have no
         * ABI and are ignored. */
        if (ev->xbutton.button == 4 || ev->xbutton.button == 5) {
            int delta = ev->xbutton.button == 4 ? 120 : -120;
            evq_push_linux(13, ev->xbutton.x, ev->xbutton.y, 0, delta,
                           x11_mods(ev->xbutton.state));
        } else if (ev->xbutton.button == 6 || ev->xbutton.button == 7) {
            /* horizontal wheel: ignored */
        } else if (ev->xbutton.button == 1) {
            /* Honor the app-drawn caption and resize borders by delegating to
             * the WM, mirroring Win32 WM_NCHITTEST. Presses over content or the
             * caption buttons fall through as ordinary clicks. */
            zan_lwin_t *bw = lwin_find(ev->xbutton.window);
            int dir = bw ? x11_caption_hit(bw, ev->xbutton.x, ev->xbutton.y) : -1;
            if (dir >= 0) {
                x11_start_moveresize(ev->xbutton.window,
                                     ev->xbutton.x_root, ev->xbutton.y_root, dir);
            } else {
                evq_push_linux(2, ev->xbutton.x, ev->xbutton.y, 0, 0,
                               x11_mods(ev->xbutton.state));
            }
        } else {
            evq_push_linux(2, ev->xbutton.x, ev->xbutton.y,
                           (int)ev->xbutton.button - 1, 0,
                           x11_mods(ev->xbutton.state));
        }
        break;
    case ButtonRelease:
        if (ev->xbutton.button >= 4 && ev->xbutton.button <= 7) break;
        evq_push_linux(3, ev->xbutton.x, ev->xbutton.y,
                       (int)ev->xbutton.button - 1, 0,
                       x11_mods(ev->xbutton.state));
        break;
    case KeyPress: {
        int mods = x11_mods(ev->xkey.state);
        KeySym ks = 0;
        char buf[32];
        int n;
        zan_lwin_t *kw = lwin_find(ev->xkey.window);
        XIC xic = (kw && kw->xic) ? kw->xic : g_xic;
        if (xic) {
            Status st = 0;
            n = Xutf8LookupString(xic, &ev->xkey, buf, sizeof(buf) - 1, &ks, &st);
        } else {
            n = XLookupString(&ev->xkey, buf, sizeof(buf) - 1, &ks, NULL);
        }
        evq_push_linux(4, 0, 0, 0, x11_vk_from_keysym(ks), mods);
        /* Emit a WM_CHAR-style text event for each decoded character. X already
         * folds Ctrl combos to control codes (e.g. Ctrl+C -> 0x03) and delivers
         * Backspace/Tab/Enter as 8/9/13, exactly like Win32 WM_CHAR; the widgets
         * rely on those integer codes for both typing and shortcuts. */
        if (n > 0) {
            buf[n] = '\0';
            const char *p = buf;
            while (*p) {
                unsigned cp = x11_utf8_next(&p);
                if (cp != 0 && cp != 127)
                    evq_push_linux(6, 0, 0, 0, (int)cp, mods);
            }
        }
        break;
    }
    case KeyRelease: {
        KeySym ks = XLookupKeysym(&ev->xkey, 0);
        evq_push_linux(5, 0, 0, 0, x11_vk_from_keysym(ks),
                       x11_mods(ev->xkey.state));
        break;
    }
    case ConfigureNotify: {
        zan_lwin_t *cw = lwin_find(ev->xconfigure.window);
        if (cw && (ev->xconfigure.width != cw->w ||
                   ev->xconfigure.height != cw->h)) {
            cw->w = ev->xconfigure.width;
            cw->h = ev->xconfigure.height;
            if (ev->xconfigure.window == g_primary_win) {
                g_win_w = cw->w;
                g_win_h = cw->h;
            }
            evq_push_linux(7, cw->w, cw->h, 0, 0, 0);
        }
        break;
    }
    case Expose: {
        /* Re-blit the last frame from the window's back buffer so uncover/move
         * never leaves stale or blank content. */
        zan_lwin_t *ew = lwin_find(ev->xexpose.window);
        if (ew && ew->backbuf)
            XCopyArea(g_display, ew->backbuf, ew->xid, ew->gc, 0, 0,
                      (unsigned)ew->backbuf_w, (unsigned)ew->backbuf_h, 0, 0);
        break;
    }
    case ClientMessage:
        evq_push_linux(8, 0, 0, 0, 0, 0);
        break;
    }
}

EXPORT i64 zan_gui_create_window(const char *title, i64 width, i64 height) {
    if (!g_display) {
        g_display = XOpenDisplay(NULL);
        if (!g_display) return 0;
        /* Input method for UTF-8 text input (also enables IME preedit); opened
         * once per display and shared by every window's input context. */
        setlocale(LC_ALL, "");
        XSetLocaleModifiers("");
        g_xim = XOpenIM(g_display, NULL, NULL, NULL);
        if (g_wake_pipe[0] < 0 && pipe(g_wake_pipe) == 0) {
            fcntl(g_wake_pipe[0], F_SETFL, O_NONBLOCK);
            fcntl(g_wake_pipe[1], F_SETFL, O_NONBLOCK);
            fcntl(g_wake_pipe[0], F_SETFD, FD_CLOEXEC);
            fcntl(g_wake_pipe[1], F_SETFD, FD_CLOEXEC);
        }
    }
    if (g_lwin_count >= ZAN_MAX_WINDOWS) return 0;

    int screen = DefaultScreen(g_display);
    Window xid = XCreateSimpleWindow(g_display, RootWindow(g_display, screen),
        0, 0, (unsigned)width, (unsigned)height, 0,
        BlackPixel(g_display, screen), WhitePixel(g_display, screen));

    XStoreName(g_display, xid, title);
    XSelectInput(g_display, xid,
        ExposureMask | KeyPressMask | KeyReleaseMask |
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
        StructureNotifyMask);

    Atom wm_delete = XInternAtom(g_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_display, xid, &wm_delete, 1);

    XIC xic = NULL;
    if (g_xim) {
        xic = XCreateIC(g_xim,
            XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
            XNClientWindow, xid,
            XNFocusWindow, xid,
            NULL);
        if (xic) XSetICFocus(xic);
    }
    GC gc = XCreateGC(g_display, xid, 0, NULL);

    zan_lwin_t *w = &g_lwins[g_lwin_count++];
    w->xid = xid;
    w->gc = gc;
    w->xic = xic;
    w->backbuf = 0;
    w->backbuf_w = 0;
    w->backbuf_h = 0;
    w->w = (int)width;
    w->h = (int)height;

    if (!g_primary_win) {
        /* First window drives process-wide operations (clipboard, cursor) and
         * the client-side title-bar metrics computed once from its DPI. */
        g_primary_win = xid;
        g_x11_window = xid;
        g_xic = xic;
        g_win_w = (int)width;
        g_win_h = (int)height;
        g_scale_linux = (int)zan_gui_get_dpi_scale();
        g_titlebar_h_l = 32 * g_scale_linux / 100;
        g_btn_w_l = 46 * g_scale_linux / 100;
    } else {
        XWindowAttributes parent_attr;
        Window child = 0;
        int parent_x = 0;
        int parent_y = 0;
        Window root = RootWindow(g_display, screen);
        if (XGetWindowAttributes(g_display, g_primary_win, &parent_attr) &&
            XTranslateCoordinates(g_display, g_primary_win, root, 0, 0,
                                  &parent_x, &parent_y, &child)) {
            int x = parent_x + (parent_attr.width - (int)width) / 2;
            int y = parent_y + (parent_attr.height - (int)height) / 2;
            int screen_w = DisplayWidth(g_display, screen);
            int screen_h = DisplayHeight(g_display, screen);
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x + (int)width > screen_w) x = screen_w - (int)width;
            if (y + (int)height > screen_h) y = screen_h - (int)height;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            XMoveWindow(g_display, xid, x, y);
        }
    }
    /* Borderless window with app-drawn title bar (matches the Win32 backend). */
    x11_set_borderless(xid);

    return (i64)xid;
}

EXPORT i64 zan_gui_show_window(i64 win) {
    Window xid = win ? (Window)(intptr_t)win : g_x11_window;
    if (g_display && xid) {
        XMapWindow(g_display, xid);
        XFlush(g_display);
    }
    return 0;
}

EXPORT i64 zan_gui_wait_event(void) {
    if (!g_display) return -1;
    memset(g_pending_event_linux, 0, sizeof(g_pending_event_linux));
    if (evq_pop_linux()) return 0;

    int xfd = ConnectionNumber(g_display);
    XEvent ev;
    for (;;) {
        /* Drain any X events already buffered in the client before blocking. */
        while (XPending(g_display) > 0) {
            XNextEvent(g_display, &ev);
            if (ev.type == SelectionRequest) {
                x11_serve_selection(&ev.xselectionrequest);
                continue;
            }
            if (XFilterEvent(&ev, None)) continue;
            x11_translate_event(&ev);
            if (evq_pop_linux()) return 0;
        }
        /* Block until the X connection or the wake pipe becomes readable. */
        struct pollfd fds[2];
        fds[0].fd = xfd; fds[0].events = POLLIN; fds[0].revents = 0;
        int nfds = 1;
        if (g_wake_pipe[0] >= 0) {
            fds[1].fd = g_wake_pipe[0]; fds[1].events = POLLIN; fds[1].revents = 0;
            nfds = 2;
        }
        int pr = poll(fds, (nfds_t)nfds, -1);
        if (pr < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (nfds == 2 && (fds[1].revents & POLLIN)) {
            char buf[64];
            while (read(g_wake_pipe[0], buf, sizeof(buf)) > 0) { }
            /* Return a benign empty frame (kind 0) so the caller drains its
             * dispatch queue. Any pending X events are handled next loop. */
            return 0;
        }
        /* X connection readable: loop back to XPending/XNextEvent. */
    }
}

/* Wake a UI thread blocked in wait_event so it can drain the dispatch queue.
 * write() is async-signal-safe and thread-safe, so this is callable from any
 * thread even though Xlib is not. */
EXPORT i64 zan_gui_wake(void) {
    if (g_wake_pipe[1] >= 0) {
        char b = 1;
        ssize_t n = write(g_wake_pipe[1], &b, 1);
        (void)n;
    }
    return 0;
}

EXPORT i64 zan_gui_poll_event(void) {
    if (!g_display) return -1;
    memset(g_pending_event_linux, 0, sizeof(g_pending_event_linux));
    if (evq_pop_linux()) return 0;
    for (;;) {
        if (XPending(g_display) <= 0) return 1;
        XEvent ev;
        XNextEvent(g_display, &ev);
        if (ev.type == SelectionRequest) {
            x11_serve_selection(&ev.xselectionrequest);
            continue;
        }
        if (XFilterEvent(&ev, None)) continue;
        x11_translate_event(&ev);
        if (evq_pop_linux()) return 0;
    }
}

EXPORT i64 zan_gui_event_kind(void)    { return g_pending_event_linux[0]; }
EXPORT i64 zan_gui_event_x(void)       { return g_pending_event_linux[1]; }
EXPORT i64 zan_gui_event_y(void)       { return g_pending_event_linux[2]; }
EXPORT i64 zan_gui_event_button(void)  { return g_pending_event_linux[3]; }
EXPORT i64 zan_gui_event_keycode(void) { return g_pending_event_linux[4]; }
EXPORT i64 zan_gui_event_mods(void)    { return g_pending_event_linux[5]; }

EXPORT i64 zan_gui_window_width(void)  { return g_win_w; }
EXPORT i64 zan_gui_window_height(void) { return g_win_h; }

EXPORT i64 zan_gui_event_hwnd(void)    { return (i64)(unsigned)g_pending_event_linux[6]; }
EXPORT i64 zan_gui_client_width(i64 hwnd_val) {
    zan_lwin_t *w = lwin_find((Window)(intptr_t)hwnd_val);
    return w ? w->w : g_win_w;
}
EXPORT i64 zan_gui_client_height(i64 hwnd_val) {
    zan_lwin_t *w = lwin_find((Window)(intptr_t)hwnd_val);
    return w ? w->h : g_win_h;
}

EXPORT i64 zan_gui_present(i64 hwnd_val, i64 surface_id) {
    if (!g_display) return 1;
    zan_lwin_t *w = lwin_find((Window)(intptr_t)hwnd_val);
    if (!w && g_lwin_count > 0) w = &g_lwins[0];
    if (!w) return 1;
    if (surface_id < 0 || surface_id >= g_surface_count || !g_surfaces[surface_id]) return 1;
    zan_surface_t *s = g_surfaces[surface_id];

    int screen = DefaultScreen(g_display);
    unsigned depth = (unsigned)DefaultDepth(g_display, screen);

    /* Blit through the window's own off-screen Pixmap (double buffering) so
     * resizes and expose events never show a half-drawn or torn frame. */
    if (!w->backbuf || w->backbuf_w != s->width || w->backbuf_h != s->height) {
        if (w->backbuf) XFreePixmap(g_display, w->backbuf);
        w->backbuf = XCreatePixmap(g_display, w->xid,
                                   (unsigned)s->width, (unsigned)s->height, depth);
        w->backbuf_w = s->width;
        w->backbuf_h = s->height;
    }

    XImage *img = XCreateImage(g_display, DefaultVisual(g_display, screen),
        depth, ZPixmap, 0, (char *)s->pixels, (unsigned)s->width, (unsigned)s->height, 32, 0);
    if (img) {
        img->byte_order = LSBFirst;
        XPutImage(g_display, w->backbuf, w->gc, img, 0, 0, 0, 0,
                  (unsigned)s->width, (unsigned)s->height);
        img->data = NULL;
        XDestroyImage(img);
        XCopyArea(g_display, w->backbuf, w->xid, w->gc, 0, 0,
                  (unsigned)s->width, (unsigned)s->height, 0, 0);
    }
    XFlush(g_display);
    return 0;
}

EXPORT i64 zan_gui_set_title(i64 hwnd_val, const char *title) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    if (g_display && xid) {
        XStoreName(g_display, xid, title);
        XFlush(g_display);
    }
    return 0;
}

EXPORT i64 zan_gui_set_cursor(i64 cursor_type) {
    if (!g_display || g_lwin_count == 0) return 1;
    int slot = (int)cursor_type;
    if (slot < 0 || slot >= 8) slot = 0;
    if (!g_cursors_linux[slot]) {
        unsigned int shape = XC_left_ptr;
        if (slot == 1) shape = XC_hand2;
        else if (slot == 2) shape = XC_xterm;
        else if (slot == 3) shape = XC_sb_h_double_arrow;
        else if (slot == 4) shape = XC_sb_v_double_arrow;
        else if (slot == 5) shape = XC_crosshair;
        g_cursors_linux[slot] = XCreateFontCursor(g_display, shape);
    }
    for (int i = 0; i < g_lwin_count; i++)
        XDefineCursor(g_display, g_lwins[i].xid, g_cursors_linux[slot]);
    XFlush(g_display);
    return 0;
}

EXPORT i64 zan_gui_get_tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (i64)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

EXPORT void zan_gui_sleep_ms(i64 ms) {
    if (ms <= 0) return;
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000);
    req.tv_nsec = (long)((ms % 1000) * 1000000L);
    nanosleep(&req, NULL);
}
#endif /* __linux__ && !ZAN_GUI_SDL (X11 window shell) */

/* ========================================================================
 * Software bitmap-font fallback for non-Windows/non-Cocoa backends.
 * ======================================================================== */
#if !defined(_WIN32) && !defined(ZAN_GUI_COCOA)
/* Fallback bitmap font for software text rendering */
static const unsigned char zan_font_6x10[96][10] = {
    /* space (32) */ {0},
    /* ! */ {0x04,0x04,0x04,0x04,0x04,0x00,0x04,0x00,0x00,0x00},
    /* " */ {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* # */ {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A,0x00,0x00,0x00},
    /* $ */ {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04,0x00,0x00,0x00},
    /* % */ {0x18,0x19,0x02,0x04,0x08,0x13,0x03,0x00,0x00,0x00},
    /* & */ {0x08,0x14,0x14,0x08,0x15,0x12,0x0D,0x00,0x00,0x00},
    /* ' */ {0x04,0x04,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* ( */ {0x02,0x04,0x08,0x08,0x08,0x04,0x02,0x00,0x00,0x00},
    /* ) */ {0x08,0x04,0x02,0x02,0x02,0x04,0x08,0x00,0x00,0x00},
    /* * */ {0x00,0x04,0x15,0x0E,0x15,0x04,0x00,0x00,0x00,0x00},
    /* + */ {0x00,0x04,0x04,0x1F,0x04,0x04,0x00,0x00,0x00,0x00},
    /* , */ {0x00,0x00,0x00,0x00,0x00,0x04,0x04,0x08,0x00,0x00},
    /* - */ {0x00,0x00,0x00,0x1F,0x00,0x00,0x00,0x00,0x00,0x00},
    /* . */ {0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00},
    /* / */ {0x01,0x02,0x02,0x04,0x08,0x08,0x10,0x00,0x00,0x00},
    /* 0 */ {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E,0x00,0x00,0x00},
    /* 1 */ {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E,0x00,0x00,0x00},
    /* 2 */ {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F,0x00,0x00,0x00},
    /* 3 */ {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E,0x00,0x00,0x00},
    /* 4 */ {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02,0x00,0x00,0x00},
    /* 5 */ {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E,0x00,0x00,0x00},
    /* 6 */ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E,0x00,0x00,0x00},
    /* 7 */ {0x1F,0x01,0x02,0x04,0x08,0x08,0x08,0x00,0x00,0x00},
    /* 8 */ {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E,0x00,0x00,0x00},
    /* 9 */ {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C,0x00,0x00,0x00},
    /* : */ {0x00,0x00,0x04,0x00,0x00,0x04,0x00,0x00,0x00,0x00},
    /* ; */ {0x00,0x00,0x04,0x00,0x00,0x04,0x04,0x08,0x00,0x00},
    /* < */ {0x02,0x04,0x08,0x10,0x08,0x04,0x02,0x00,0x00,0x00},
    /* = */ {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00,0x00,0x00,0x00},
    /* > */ {0x08,0x04,0x02,0x01,0x02,0x04,0x08,0x00,0x00,0x00},
    /* ? */ {0x0E,0x11,0x01,0x02,0x04,0x00,0x04,0x00,0x00,0x00},
    /* @ */ {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E,0x00,0x00,0x00},
    /* A-Z */
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11,0x00,0x00,0x00},
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E,0x00,0x00,0x00},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E,0x00,0x00,0x00},
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C,0x00,0x00,0x00},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F,0x00,0x00,0x00},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10,0x00,0x00,0x00},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F,0x00,0x00,0x00},
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11,0x00,0x00,0x00},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E,0x00,0x00,0x00},
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C,0x00,0x00,0x00},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11,0x00,0x00,0x00},
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F,0x00,0x00,0x00},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11,0x00,0x00,0x00},
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11,0x00,0x00,0x00},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E,0x00,0x00,0x00},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10,0x00,0x00,0x00},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D,0x00,0x00,0x00},
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11,0x00,0x00,0x00},
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E,0x00,0x00,0x00},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04,0x00,0x00,0x00},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E,0x00,0x00,0x00},
    {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04,0x00,0x00,0x00},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A,0x00,0x00,0x00},
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11,0x00,0x00,0x00},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04,0x00,0x00,0x00},
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F,0x00,0x00,0x00},
    /* [, \, ], ^, _, ` */
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E,0x00,0x00,0x00},
    {0x10,0x08,0x08,0x04,0x02,0x02,0x01,0x00,0x00,0x00},
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E,0x00,0x00,0x00},
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    {0x08,0x04,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* a-z */
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F,0x00,0x00,0x00},
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E,0x00,0x00,0x00},
    {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E,0x00,0x00,0x00},
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F,0x00,0x00,0x00},
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E,0x00,0x00,0x00},
    {0x06,0x08,0x1E,0x08,0x08,0x08,0x08,0x00,0x00,0x00},
    {0x00,0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E,0x00,0x00},
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x11,0x00,0x00,0x00},
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E,0x00,0x00,0x00},
    {0x02,0x00,0x06,0x02,0x02,0x02,0x12,0x0C,0x00,0x00},
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12,0x00,0x00,0x00},
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E,0x00,0x00,0x00},
    {0x00,0x00,0x1A,0x15,0x15,0x15,0x15,0x00,0x00,0x00},
    {0x00,0x00,0x1E,0x11,0x11,0x11,0x11,0x00,0x00,0x00},
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E,0x00,0x00,0x00},
    {0x00,0x00,0x1E,0x11,0x11,0x1E,0x10,0x10,0x00,0x00},
    {0x00,0x00,0x0F,0x11,0x11,0x0F,0x01,0x01,0x00,0x00},
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10,0x00,0x00,0x00},
    {0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E,0x00,0x00,0x00},
    {0x08,0x08,0x1E,0x08,0x08,0x08,0x06,0x00,0x00,0x00},
    {0x00,0x00,0x11,0x11,0x11,0x11,0x0F,0x00,0x00,0x00},
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04,0x00,0x00,0x00},
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A,0x00,0x00,0x00},
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11,0x00,0x00,0x00},
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E,0x00,0x00,0x00},
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F,0x00,0x00,0x00},
    /* {, |, }, ~ */
    {0x02,0x04,0x04,0x08,0x04,0x04,0x02,0x00,0x00,0x00},
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00,0x00,0x00},
    {0x08,0x04,0x04,0x02,0x04,0x04,0x08,0x00,0x00,0x00},
    {0x00,0x00,0x08,0x15,0x02,0x00,0x00,0x00,0x00,0x00},
};

static u32 utf8_next(const char **text) {
    const unsigned char *p = (const unsigned char *)*text;
    if (!*p) return 0;
    if (*p < 0x80) {
        *text += 1;
        return *p;
    }
    if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        u32 cp = ((u32)(p[0] & 0x1F) << 6) | (u32)(p[1] & 0x3F);
        *text += 2;
        return cp >= 0x80 ? cp : 0xFFFD;
    }
    if ((*p & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 &&
        (p[2] & 0xC0) == 0x80) {
        u32 cp = ((u32)(p[0] & 0x0F) << 12) |
                 ((u32)(p[1] & 0x3F) << 6) | (u32)(p[2] & 0x3F);
        *text += 3;
        return cp >= 0x800 ? cp : 0xFFFD;
    }
    if ((*p & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 &&
        (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        u32 cp = ((u32)(p[0] & 0x07) << 18) |
                 ((u32)(p[1] & 0x3F) << 12) |
                 ((u32)(p[2] & 0x3F) << 6) | (u32)(p[3] & 0x3F);
        *text += 4;
        return cp >= 0x10000 && cp <= 0x10FFFF ? cp : 0xFFFD;
    }
    *text += 1;
    return 0xFFFD;
}

static void bitmap_draw_text(i64 surface_id, i64 x, i64 y,
                             const char *text, i64 color, i64 font_size) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !text) return;
    u32 c = (u32)color;
    int scale = (int)font_size / 10;
    if (scale < 1) scale = 1;
    int cx = (int)x;
    int cy_base = (int)y;
    while (*text) {
        u32 cp = utf8_next(&text);
        unsigned char ch = (cp >= 32 && cp <= 127) ? (unsigned char)cp : '?';
        int idx = ch - 32;
        for (int row = 0; row < 10; row++) {
            unsigned char bits = zan_font_6x10[idx][row];
            for (int col = 0; col < 6; col++) {
                if (bits & (0x10 >> col)) {
                    for (int sy = 0; sy < scale; sy++)
                        for (int sx = 0; sx < scale; sx++)
                            set_pixel(s, cx + col*scale + sx, cy_base + row*scale + sy, c);
                }
            }
        }
        cx += 6 * scale;
    }
}

static i64 bitmap_measure_text(const char *text, i64 font_size) {
    if (!text) return 0;
    int scale = (int)font_size / 10;
    if (scale < 1) scale = 1;
    int len = 0;
    while (*text) { utf8_next(&text); len++; }
    return (i64)(len * 6 * scale);
}

static i64 bitmap_font_height(i64 font_size) {
    int scale = (int)font_size / 10;
    if (scale < 1) scale = 1;
    return (i64)(10 * scale);
}

#ifdef ZAN_GUI_FREETYPE
static FT_Library g_ft_library;
static FT_Face g_ft_face;
static int g_ft_state;

static int ft_prepare(int font_size) {
    if (g_ft_state == 0) {
        g_ft_state = -1;
        if (!FcInit() || FT_Init_FreeType(&g_ft_library) != 0) return 0;
        FcPattern *pattern = FcNameParse((const FcChar8 *)"sans");
        if (!pattern) return 0;
        FcConfigSubstitute(NULL, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);
        FcResult result;
        FcPattern *match = FcFontMatch(NULL, pattern, &result);
        FcPatternDestroy(pattern);
        if (!match) return 0;
        FcChar8 *path = NULL;
        int found = FcPatternGetString(match, FC_FILE, 0, &path) == FcResultMatch;
        if (!found || FT_New_Face(g_ft_library, (const char *)path, 0,
                                  &g_ft_face) != 0) {
            FcPatternDestroy(match);
            return 0;
        }
        FcPatternDestroy(match);
        g_ft_state = 1;
    }
    if (g_ft_state != 1) return 0;
    if (font_size < 8) font_size = 8;
    return FT_Set_Pixel_Sizes(g_ft_face, 0, (FT_UInt)font_size) == 0;
}

#define ZAN_FT_FB_MAX 8
static FT_Face g_ft_fb[ZAN_FT_FB_MAX];
static int g_ft_fb_count = 0;

/* Return a face that can render `cp` at `font_size`, discovering a fallback
 * font via fontconfig when the primary "sans" face lacks the glyph (e.g. CJK).
 * Discovered faces are cached; falls back to the primary face when no better
 * match exists. */
static FT_Face ft_face_for_cp(u32 cp, int font_size) {
    if (FT_Get_Char_Index(g_ft_face, cp)) return g_ft_face;
    for (int i = 0; i < g_ft_fb_count; i++) {
        if (g_ft_fb[i] && FT_Get_Char_Index(g_ft_fb[i], cp)) {
            FT_Set_Pixel_Sizes(g_ft_fb[i], 0, (FT_UInt)font_size);
            return g_ft_fb[i];
        }
    }
    if (g_ft_fb_count < ZAN_FT_FB_MAX) {
        FcCharSet *charset = FcCharSetCreate();
        FcCharSetAddChar(charset, cp);
        FcPattern *pat = FcPatternCreate();
        FcPatternAddCharSet(pat, FC_CHARSET, charset);
        FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
        FcConfigSubstitute(NULL, pat, FcMatchPattern);
        FcDefaultSubstitute(pat);
        FcResult res;
        FcPattern *match = FcFontMatch(NULL, pat, &res);
        FcPatternDestroy(pat);
        FcCharSetDestroy(charset);
        if (match) {
            FcChar8 *path = NULL;
            FT_Face face = NULL;
            if (FcPatternGetString(match, FC_FILE, 0, &path) == FcResultMatch &&
                FT_New_Face(g_ft_library, (const char *)path, 0, &face) == 0) {
                g_ft_fb[g_ft_fb_count++] = face;
                FcPatternDestroy(match);
                FT_Set_Pixel_Sizes(face, 0, (FT_UInt)font_size);
                return face;
            }
            FcPatternDestroy(match);
        }
    }
    return g_ft_face;
}

static void ft_draw_text(i64 surface_id, i64 x, i64 y,
                         const char *text, i64 color, int font_size) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !text) return;
    int pen_x = (int)x;
    int baseline = (int)y +
                   (int)(g_ft_face->size->metrics.ascender >> 6);
    while (*text) {
        u32 cp = utf8_next(&text);
        FT_Face face = ft_face_for_cp(cp, font_size);
        FT_UInt glyph = FT_Get_Char_Index(face, cp);
        if (!glyph) glyph = FT_Get_Char_Index(face, '?');
        if (glyph && FT_Load_Glyph(face, glyph, FT_LOAD_RENDER) == 0) {
            FT_GlyphSlot slot = face->glyph;
            FT_Bitmap *bitmap = &slot->bitmap;
            int ox = pen_x + slot->bitmap_left;
            int oy = baseline - slot->bitmap_top;
            int pitch = bitmap->pitch;
            for (int py = 0; py < (int)bitmap->rows; py++) {
                const unsigned char *row =
                    pitch >= 0
                        ? bitmap->buffer + py * pitch
                        : bitmap->buffer +
                              ((int)bitmap->rows - 1 - py) * (-pitch);
                for (int px = 0; px < (int)bitmap->width; px++) {
                    int coverage = 0;
                    if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY)
                        coverage = row[px];
                    else if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO)
                        coverage = (row[px >> 3] & (0x80 >> (px & 7)))
                                       ? 255
                                       : 0;
                    else if (bitmap->pixel_mode == FT_PIXEL_MODE_BGRA)
                        coverage = row[px * 4 + 3];
                    if (coverage)
                        set_pixel_aa(s, ox + px, oy + py,
                                     (u32)color, coverage);
                }
            }
            pen_x += (int)(slot->advance.x >> 6);
        }
    }
}

static i64 ft_measure_text(const char *text, int font_size) {
    int width = 0;
    while (text && *text) {
        u32 cp = utf8_next(&text);
        FT_Face face = ft_face_for_cp(cp, font_size);
        FT_UInt glyph = FT_Get_Char_Index(face, cp);
        if (!glyph) glyph = FT_Get_Char_Index(face, '?');
        if (glyph && FT_Load_Glyph(face, glyph, FT_LOAD_DEFAULT) == 0)
            width += (int)(face->glyph->advance.x >> 6);
    }
    return width;
}
#endif

EXPORT void zan_gui_draw_text(i64 surface_id, i64 x, i64 y,
                              const char *text, i64 color, i64 font_size) {
#ifdef ZAN_GUI_FREETYPE
    if (ft_prepare((int)font_size)) {
        ft_draw_text(surface_id, x, y, text, color, (int)font_size);
        return;
    }
#endif
    bitmap_draw_text(surface_id, x, y, text, color, font_size);
}

EXPORT i64 zan_gui_measure_text(const char *text, i64 font_size) {
#ifdef ZAN_GUI_FREETYPE
    if (ft_prepare((int)font_size)) return ft_measure_text(text, (int)font_size);
#endif
    return bitmap_measure_text(text, font_size);
}

EXPORT i64 zan_gui_font_height(i64 font_size) {
#ifdef ZAN_GUI_FREETYPE
    if (ft_prepare((int)font_size))
        return (i64)(g_ft_face->size->metrics.height >> 6);
#endif
    return bitmap_font_height(font_size);
}

#endif /* software bitmap text */

/* Icon glyphs are rendered as scalable vector primitives on every platform so
 * they look identical across Windows/macOS/Linux and need no icon font (Segoe
 * MDL2 Assets only exists on Windows 10+, so a font path would break Win7). */
static void icon_line(i64 s, int x0, int y0, int x1, int y1,
                      u32 color, int thickness) {
    zan_gui_draw_line(s, x0, y0, x1, y1, color, thickness);
}

static void icon_circle(i64 s, int cx, int cy, int radius,
                        u32 color, int thickness) {
    const double pi = 3.14159265358979323846;
    int segments = radius > 12 ? 24 : 16;
    int px = cx + radius, py = cy;
    for (int i = 1; i <= segments; i++) {
        double a = 2.0 * pi * (double)i / (double)segments;
        int nx = cx + (int)(cos(a) * radius);
        int ny = cy + (int)(sin(a) * radius);
        icon_line(s, px, py, nx, ny, color, thickness);
        px = nx;
        py = ny;
    }
}

static void icon_arrow(i64 s, int x0, int y0, int x1, int y1,
                       u32 color, int thickness) {
    icon_line(s, x0, y0, x1, y1, color, thickness);
    int dx = x1 - x0, dy = y1 - y0;
    int wing = (abs(dx) + abs(dy)) / 4;
    if (wing < 3) wing = 3;
    if (abs(dx) >= abs(dy)) {
        int sign = dx >= 0 ? 1 : -1;
        icon_line(s, x1, y1, x1 - sign * wing, y1 - wing,
                  color, thickness);
        icon_line(s, x1, y1, x1 - sign * wing, y1 + wing,
                  color, thickness);
    } else {
        int sign = dy >= 0 ? 1 : -1;
        icon_line(s, x1, y1, x1 - wing, y1 - sign * wing,
                  color, thickness);
        icon_line(s, x1, y1, x1 + wing, y1 - sign * wing,
                  color, thickness);
    }
}

static void icon_star(i64 s, int cx, int cy, int radius,
                      u32 color, int thickness) {
    const double pi = 3.14159265358979323846;
    int px = 0, py = 0, first_x = 0, first_y = 0;
    for (int i = 0; i < 10; i++) {
        double a = -pi / 2.0 + (double)i * pi / 5.0;
        int rr = (i & 1) ? radius * 2 / 5 : radius;
        int nx = cx + (int)(cos(a) * rr);
        int ny = cy + (int)(sin(a) * rr);
        if (i == 0) {
            first_x = nx;
            first_y = ny;
        } else {
            icon_line(s, px, py, nx, ny, color, thickness);
        }
        px = nx;
        py = ny;
    }
    icon_line(s, px, py, first_x, first_y, color, thickness);
}

/* Even-odd point-in-polygon test (ray cast). */
static int icon_pt_in_poly(int n, const int *px, const int *py, int x, int y) {
    int inside = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        if (((py[i] > y) != (py[j] > y)) &&
            (x < (px[j] - px[i]) * (y - py[i]) /
                     (py[j] - py[i]) + px[i])) {
            inside = !inside;
        }
    }
    return inside;
}

/* Solid five-point star (for the "filled" star icon). */
static void icon_fill_star(i64 s, int cx, int cy, int radius, u32 color) {
    const double pi = 3.14159265358979323846;
    int px[10], py[10];
    for (int i = 0; i < 10; i++) {
        double a = -pi / 2.0 + (double)i * pi / 5.0;
        int rr = (i & 1) ? radius * 2 / 5 : radius;
        px[i] = cx + (int)(cos(a) * rr);
        py[i] = cy + (int)(sin(a) * rr);
    }
    for (int yy = cy - radius; yy <= cy + radius; yy++) {
        int run = 0, startx = 0;
        for (int xx = cx - radius; xx <= cx + radius; xx++) {
            if (icon_pt_in_poly(10, px, py, xx, yy)) {
                if (!run) { run = 1; startx = xx; }
            } else if (run) {
                zan_gui_fill_rect(s, startx, yy, xx - startx, 1, color);
                run = 0;
            }
        }
        if (run) {
            zan_gui_fill_rect(s, startx, yy, cx + radius - startx + 1, 1, color);
        }
    }
}

EXPORT void zan_gui_draw_icon(i64 surface_id, i64 x, i64 y, i64 box,
                              i64 color, i64 codepoint) {
    int size = (int)box;
    if (size < 8) size = 8;
    int pad = size / 6;
    int x0 = (int)x + pad, y0 = (int)y + pad;
    int x1 = (int)x + size - pad, y1 = (int)y + size - pad;
    int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    int w = x1 - x0, h = y1 - y0;
    int thick = size / 10;
    if (thick < 1) thick = 1;
    u32 c = (u32)color;

    switch ((int)codepoint) {
    case 0xE8BB: case 0xEA39:
        icon_line(surface_id, x0, y0, x1, y1, c, thick);
        icon_line(surface_id, x1, y0, x0, y1, c, thick);
        break;
    case 0xE921: case 0xE738:
        icon_line(surface_id, x0, cy, x1, cy, c, thick);
        break;
    case 0xE922:
        zan_gui_draw_rect(surface_id, x0, y0, w, h, c, thick);
        break;
    case 0xE923: case 0xE8C8:
        zan_gui_draw_rect(surface_id, x0 + pad / 2, y0, w - pad / 2,
                          h - pad / 2, c, thick);
        zan_gui_draw_rect(surface_id, x0, y0 + pad / 2, w - pad / 2,
                          h - pad / 2, c, thick);
        break;
    case 0xE73E: case 0xE930:
        icon_line(surface_id, x0, cy, cx - pad / 3, y1, c, thick);
        icon_line(surface_id, cx - pad / 3, y1, x1, y0, c, thick);
        break;
    case 0xE710:
        icon_line(surface_id, x0, cy, x1, cy, c, thick);
        icon_line(surface_id, cx, y0, cx, y1, c, thick);
        break;
    case 0xE76B:
        icon_arrow(surface_id, x1, cy, x0, cy, c, thick);
        break;
    case 0xE76C:
        icon_arrow(surface_id, x0, cy, x1, cy, c, thick);
        break;
    case 0xE70E: case 0xE898:
        icon_arrow(surface_id, cx, y1, cx, y0, c, thick);
        break;
    case 0xE70D: case 0xE896:
        icon_arrow(surface_id, cx, y0, cx, y1, c, thick);
        break;
    case 0xE721:
        icon_circle(surface_id, cx - pad / 2, cy - pad / 2,
                    w / 3, c, thick);
        icon_line(surface_id, cx + pad / 2, cy + pad / 2,
                  x1, y1, c, thick);
        break;
    case 0xE713:
        icon_circle(surface_id, cx, cy, w / 4, c, thick);
        for (int i = 0; i < 8; i++) {
            double a = 3.14159265358979323846 * (double)i / 4.0;
            int ax = cx + (int)(cos(a) * w / 3);
            int ay = cy + (int)(sin(a) * h / 3);
            int bx = cx + (int)(cos(a) * w / 2);
            int by = cy + (int)(sin(a) * h / 2);
            icon_line(surface_id, ax, ay, bx, by, c, thick);
        }
        break;
    case 0xE77B:
        zan_gui_fill_circle(surface_id, cx, y0 + h / 4, w / 6, c);
        zan_gui_fill_sector(surface_id, cx, y1, 0, w / 2,
                            270, 450, c);
        break;
    case 0xEA8F:
        icon_circle(surface_id, cx, cy, w / 3, c, thick);
        zan_gui_fill_rect(surface_id, x0, cy, w, h / 3, c);
        zan_gui_fill_circle(surface_id, cx, y1, thick, c);
        break;
    case 0xE7C3:
        zan_gui_draw_rect(surface_id, x0, y0, w, h, c, thick);
        icon_line(surface_id, x1 - w / 3, y0,
                  x1, y0 + h / 3, c, thick);
        break;
    case 0xE8B7:
        zan_gui_draw_rect(surface_id, x0, y0 + h / 4, w,
                          h * 3 / 4, c, thick);
        icon_line(surface_id, x0, y0 + h / 4, cx, y0 + h / 4, c, thick);
        icon_line(surface_id, x0, y0 + h / 4, x0 + w / 4, y0, c, thick);
        icon_line(surface_id, x0 + w / 4, y0, cx, y0, c, thick);
        break;
    case 0xE80F:
        icon_line(surface_id, x0, cy, cx, y0, c, thick);
        icon_line(surface_id, cx, y0, x1, cy, c, thick);
        icon_line(surface_id, x0 + pad / 2, cy - pad / 2,
                  x0 + pad / 2, y1, c, thick);
        icon_line(surface_id, x1 - pad / 2, cy - pad / 2,
                  x1 - pad / 2, y1, c, thick);
        icon_line(surface_id, x0 + pad / 2, y1,
                  x1 - pad / 2, y1, c, thick);
        break;
    case 0xE74D:
        zan_gui_draw_rect(surface_id, x0 + pad / 2, y0 + pad,
                          w - pad, h - pad, c, thick);
        icon_line(surface_id, x0, y0 + pad / 2, x1, y0 + pad / 2, c, thick);
        icon_line(surface_id, cx - pad, y0, cx + pad, y0, c, thick);
        break;
    case 0xE70F:
        icon_line(surface_id, x0, y1, x1, y0, c, thick + 1);
        icon_line(surface_id, x0, y1, x0 + pad, y1, c, thick);
        break;
    case 0xE946:
        icon_circle(surface_id, cx, cy, w / 2, c, thick);
        zan_gui_fill_circle(surface_id, cx, y0 + h / 4, thick, c);
        icon_line(surface_id, cx, cy - pad / 3, cx, y1 - pad / 3, c, thick);
        break;
    case 0xE7BA:
        icon_line(surface_id, cx, y0, x0, y1, c, thick);
        icon_line(surface_id, x0, y1, x1, y1, c, thick);
        icon_line(surface_id, x1, y1, cx, y0, c, thick);
        icon_line(surface_id, cx, cy - pad / 2, cx, cy + pad / 2, c, thick);
        zan_gui_fill_circle(surface_id, cx, y1 - pad / 2, thick, c);
        break;
    case 0xE735: /* filled star */
        icon_fill_star(surface_id, cx, cy, w / 2, c);
        break;
    case 0xE734: /* empty star */
        icon_star(surface_id, cx, cy, w / 2, c, thick);
        break;
    case 0xEB51:
        icon_line(surface_id, x0, cy - pad, cx, y1, c, thick);
        icon_line(surface_id, cx, y1, x1, cy - pad, c, thick);
        icon_circle(surface_id, x0 + w / 4, cy - pad, w / 4, c, thick);
        icon_circle(surface_id, x1 - w / 4, cy - pad, w / 4, c, thick);
        break;
    case 0xE8E1:
        zan_gui_draw_rect(surface_id, x0 + w / 4, cy - pad,
                          w * 3 / 4, h / 2 + pad, c, thick);
        icon_line(surface_id, x0 + w / 4, cy, x0, cy, c, thick + 1);
        icon_line(surface_id, x0, cy, x0, y1, c, thick + 1);
        break;
    case 0xE787:
        zan_gui_draw_rect(surface_id, x0, y0 + pad / 2, w,
                          h - pad / 2, c, thick);
        icon_line(surface_id, x0, y0 + h / 3, x1, y0 + h / 3, c, thick);
        icon_line(surface_id, x0 + pad, y0, x0 + pad, y0 + pad, c, thick);
        icon_line(surface_id, x1 - pad, y0, x1 - pad, y0 + pad, c, thick);
        break;
    case 0xE823:
        icon_circle(surface_id, cx, cy, w / 2, c, thick);
        icon_line(surface_id, cx, cy, cx, y0 + pad, c, thick);
        icon_line(surface_id, cx, cy, x1 - pad, cy, c, thick);
        break;
    case 0xEB9F:
        zan_gui_draw_rect(surface_id, x0, y0, w, h, c, thick);
        zan_gui_fill_circle(surface_id, x1 - pad, y0 + pad, thick + 1, c);
        icon_line(surface_id, x0 + pad, y1 - pad,
                  cx, cy, c, thick);
        icon_line(surface_id, cx, cy, x1 - pad, y1 - pad, c, thick);
        break;
    case 0xE72C: case 0xE7A7: case 0xE7A6:
        icon_circle(surface_id, cx, cy, w / 2, c, thick);
        icon_arrow(surface_id, x1, cy, x1 - pad, y0, c, thick);
        break;
    case 0xE712:
        zan_gui_fill_circle(surface_id, x0 + pad, cy, thick + 1, c);
        zan_gui_fill_circle(surface_id, cx, cy, thick + 1, c);
        zan_gui_fill_circle(surface_id, x1 - pad, cy, thick + 1, c);
        break;
    case 0xE700:
        icon_line(surface_id, x0, y0 + pad, x1, y0 + pad, c, thick);
        icon_line(surface_id, x0, cy, x1, cy, c, thick);
        icon_line(surface_id, x0, y1 - pad, x1, y1 - pad, c, thick);
        break;
    case 0xE7B3:
        icon_line(surface_id, x0, cy, cx, y0 + pad, c, thick);
        icon_line(surface_id, cx, y0 + pad, x1, cy, c, thick);
        icon_line(surface_id, x1, cy, cx, y1 - pad, c, thick);
        icon_line(surface_id, cx, y1 - pad, x0, cy, c, thick);
        zan_gui_fill_circle(surface_id, cx, cy, w / 8, c);
        break;
    case 0xE72E:
        zan_gui_draw_rect(surface_id, x0, cy - pad / 2,
                          w, h / 2 + pad / 2, c, thick);
        icon_circle(surface_id, cx, cy - pad / 2, w / 4, c, thick);
        break;
    case 0xE715:
        zan_gui_draw_rect(surface_id, x0, y0, w, h, c, thick);
        icon_line(surface_id, x0, y0, cx, cy, c, thick);
        icon_line(surface_id, cx, cy, x1, y0, c, thick);
        break;
    case 0xE706:
        icon_circle(surface_id, cx, cy, w / 4, c, thick);
        for (int i = 0; i < 8; i++) {
            double a = 3.14159265358979323846 * (double)i / 4.0;
            icon_line(surface_id,
                      cx + (int)(cos(a) * w / 3),
                      cy + (int)(sin(a) * h / 3),
                      cx + (int)(cos(a) * w / 2),
                      cy + (int)(sin(a) * h / 2), c, thick);
        }
        break;
    case 0xE708:
        icon_circle(surface_id, cx, cy, w / 2, c, thick);
        icon_circle(surface_id, cx + pad, cy - pad, w / 2, c, thick);
        break;
    case 0xE718: case 0xE77A:
        zan_gui_fill_circle(surface_id, cx, y0 + h / 3, w / 4, c);
        icon_line(surface_id, cx, cy, cx, y1, c, thick);
        break;
    case 0xE71C:
        icon_line(surface_id, x0, y0, x1, y0, c, thick);
        icon_line(surface_id, x1, y0, cx + pad / 2, cy, c, thick);
        icon_line(surface_id, cx + pad / 2, cy,
                  cx + pad / 2, y1, c, thick);
        break;
    case 0xE8CB:
        icon_arrow(surface_id, x0 + pad, y1, x0 + pad, y0, c, thick);
        icon_arrow(surface_id, x1 - pad, y0, x1 - pad, y1, c, thick);
        break;
    case 0xE80A: case 0xF0E2: case 0xE8A1:
        zan_gui_draw_rect(surface_id, x0, y0, w, h, c, thick);
        icon_line(surface_id, cx, y0, cx, y1, c, thick);
        icon_line(surface_id, x0, cy, x1, cy, c, thick);
        break;
    case 0xE768:
        icon_line(surface_id, x0 + pad, y0, x0 + pad, y1, c, thick);
        icon_line(surface_id, x0 + pad, y0, x1, cy, c, thick);
        icon_line(surface_id, x1, cy, x0 + pad, y1, c, thick);
        break;
    case 0xE769:
        zan_gui_fill_rect(surface_id, x0 + pad, y0, thick * 2, h, c);
        zan_gui_fill_rect(surface_id, x1 - pad - thick * 2, y0,
                          thick * 2, h, c);
        break;
    case 0xE71A:
        zan_gui_fill_rect(surface_id, x0, y0, w, h, c);
        break;
    case 0xEBE8:
        icon_circle(surface_id, cx, cy, w / 3, c, thick);
        icon_line(surface_id, x0, y0, x0 + pad, cy, c, thick);
        icon_line(surface_id, x1, y0, x1 - pad, cy, c, thick);
        icon_line(surface_id, x0, y1, x0 + pad, cy, c, thick);
        icon_line(surface_id, x1, y1, x1 - pad, cy, c, thick);
        break;
    case 0xE943:
        icon_line(surface_id, cx - pad, y0, x0, cy, c, thick);
        icon_line(surface_id, x0, cy, cx - pad, y1, c, thick);
        icon_line(surface_id, cx + pad, y0, x1, cy, c, thick);
        icon_line(surface_id, x1, cy, cx + pad, y1, c, thick);
        break;
    case 0xE74E:
        zan_gui_draw_rect(surface_id, x0, y0, w, h, c, thick);
        zan_gui_draw_rect(surface_id, x0 + pad, y0, w - 2 * pad,
                          h / 3, c, thick);
        zan_gui_draw_rect(surface_id, x0 + pad, cy, w - 2 * pad,
                          h / 3, c, thick);
        break;
    case 0xE8C6:
        icon_line(surface_id, x0, y0, x1, y1, c, thick);
        icon_line(surface_id, x1, y0, x0, y1, c, thick);
        icon_circle(surface_id, x0 + pad, y1 - pad, pad, c, thick);
        icon_circle(surface_id, x1 - pad, y1 - pad, pad, c, thick);
        break;
    case 0xE77F:
        zan_gui_draw_rect(surface_id, x0, y0 + pad, w, h - pad, c, thick);
        zan_gui_draw_rect(surface_id, cx - pad, y0, pad * 2,
                          pad * 2, c, thick);
        break;
    case 0xE8B3:
        zan_gui_draw_rect(surface_id, x0, y0, w, h, c, thick);
        zan_gui_draw_rect(surface_id, x0 + pad, y0 + pad,
                          w - 2 * pad, h - 2 * pad, c, thick);
        break;
    case 0xE8D2:
        icon_line(surface_id, x0, y1, cx, y0, c, thick);
        icon_line(surface_id, cx, y0, x1, y1, c, thick);
        icon_line(surface_id, x0 + pad, cy + pad,
                  x1 - pad, cy + pad, c, thick);
        break;
    case 0xE790:
        icon_circle(surface_id, cx, cy, w / 2, c, thick);
        zan_gui_fill_circle(surface_id, cx - pad, cy - pad, thick + 1, c);
        zan_gui_fill_circle(surface_id, cx + pad, cy - pad, thick + 1, c);
        zan_gui_fill_circle(surface_id, cx, cy + pad, thick + 1, c);
        break;
    case 0xF101: /* chevron-down */
        icon_line(surface_id, x0, cy - h / 6, cx, cy + h / 6, c, thick);
        icon_line(surface_id, cx, cy + h / 6, x1, cy - h / 6, c, thick);
        break;
    case 0xF103: /* chevron-up */
        icon_line(surface_id, x0, cy + h / 6, cx, cy - h / 6, c, thick);
        icon_line(surface_id, cx, cy - h / 6, x1, cy + h / 6, c, thick);
        break;
    case 0xF102: /* chevron-right */
        icon_line(surface_id, cx - w / 6, y0, cx + w / 6, cy, c, thick);
        icon_line(surface_id, cx + w / 6, cy, cx - w / 6, y1, c, thick);
        break;
    case 0xF104: /* chevron-left */
        icon_line(surface_id, cx + w / 6, y0, cx - w / 6, cy, c, thick);
        icon_line(surface_id, cx - w / 6, cy, cx + w / 6, y1, c, thick);
        break;
    case 0xE774: /* globe: circle + equator + meridian + latitude lines */
        icon_circle(surface_id, cx, cy, w / 2, c, thick);
        icon_line(surface_id, x0, cy, x1, cy, c, thick);
        icon_line(surface_id, cx, y0, cx, y1, c, thick);
        icon_line(surface_id, cx - w / 3, cy - h / 4, cx + w / 3, cy - h / 4, c, thick);
        icon_line(surface_id, cx - w / 3, cy + h / 4, cx + w / 3, cy + h / 4, c, thick);
        break;
    default:
        break;
    }
}

#if defined(__linux__) && !defined(ZAN_GUI_SDL)
/* ---- window management (EWMH / Xlib) ---- */

EXPORT i64 zan_gui_minimize(i64 hwnd_val) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    if (g_display && xid) {
        XIconifyWindow(g_display, xid, DefaultScreen(g_display));
        XFlush(g_display);
    }
    return 0;
}

EXPORT i64 zan_gui_toggle_maximize(i64 hwnd_val) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    x11_wm_state(xid, x11_atom("_NET_WM_STATE_MAXIMIZED_VERT"),
                 x11_atom("_NET_WM_STATE_MAXIMIZED_HORZ"), 2 /* toggle */);
    return 0;
}

EXPORT i64 zan_gui_is_maximized(i64 hwnd_val) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    if (!g_display || !xid) return 0;
    Atom vert = x11_atom("_NET_WM_STATE_MAXIMIZED_VERT");
    Atom horz = x11_atom("_NET_WM_STATE_MAXIMIZED_HORZ");
    Atom actual_type;
    int actual_format;
    unsigned long nitems = 0, bytes_after = 0;
    unsigned char *prop = NULL;
    int found_v = 0, found_h = 0;
    if (XGetWindowProperty(g_display, xid, x11_atom("_NET_WM_STATE"),
                           0, 64, False, XA_ATOM, &actual_type, &actual_format,
                           &nitems, &bytes_after, &prop) == Success && prop) {
        Atom *states = (Atom *)prop;
        for (unsigned long i = 0; i < nitems; i++) {
            if (states[i] == vert) found_v = 1;
            if (states[i] == horz) found_h = 1;
        }
        XFree(prop);
    }
    return (found_v && found_h) ? 1 : 0;
}

EXPORT i64 zan_gui_set_topmost(i64 hwnd_val, i64 on) {
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_x11_window;
    x11_wm_state(xid, x11_atom("_NET_WM_STATE_ABOVE"), 0, on ? 1 : 0);
    return 0;
}

/* ---- client-side title-bar metrics (borderless window) ---- */
EXPORT i64 zan_gui_titlebar_height(void) { return g_titlebar_h_l; }
EXPORT i64 zan_gui_caption_button_width(void) { return g_btn_w_l; }
EXPORT i64 zan_gui_set_caption_buttons(i64 hwnd_val, i64 count) {
    (void)hwnd_val;
    if (count >= 0 && count <= 8) { g_caption_btn_count_l = (int)count; }
    return 0;
}

EXPORT i64 zan_gui_close_window(i64 hwnd_val) {
    if (!g_display) return 0;
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_primary_win;
    zan_lwin_t *w = lwin_find(xid);
    if (w) {
        if (w->backbuf) XFreePixmap(g_display, w->backbuf);
        if (w->gc) XFreeGC(g_display, w->gc);
        if (w->xic) XDestroyIC(w->xic);
        XDestroyWindow(g_display, w->xid);
        int idx = (int)(w - g_lwins);
        g_lwins[idx] = g_lwins[--g_lwin_count];
    }
    if (xid == g_primary_win) {
        /* Promote another window to primary so process-wide ops keep working. */
        g_primary_win = g_lwin_count ? g_lwins[0].xid : 0;
        g_x11_window = g_primary_win;
        if (g_lwin_count) {
            g_xic = g_lwins[0].xic;
            g_win_w = g_lwins[0].w;
            g_win_h = g_lwins[0].h;
        } else {
            g_xic = NULL;
        }
    }
    if (g_lwin_count == 0) {
        /* Last window gone: tear down shared resources. */
        for (int i = 0; i < 8; i++) {
            if (g_cursors_linux[i]) {
                XFreeCursor(g_display, g_cursors_linux[i]);
                g_cursors_linux[i] = 0;
            }
        }
        if (g_xim) { XCloseIM(g_xim); g_xim = NULL; }
        free(g_clip_text_linux);
        g_clip_text_linux = NULL;
        free(g_clip_read_linux);
        g_clip_read_linux = NULL;
    }
    XFlush(g_display);
    return 0;
}

/* Native glass on Linux: ask a compositing WM (KWin, or picom via rules) to
 * blur whatever is behind the window by setting the de-facto standard
 * _KDE_NET_WM_BLUR_BEHIND_REGION property. An empty region means "blur the
 * whole window". The blur is only *visible* where the window is translucent,
 * which requires a 32-bit ARGB visual plus a running compositor; without those
 * the hint is a harmless no-op. tint is unused (the compositor owns the tint).
 * This is the Linux side of the same Gui.Native.Window.EnableGlass API. */
EXPORT i64 zan_gui_enable_glass(i64 hwnd_val, i64 tint_argb) {
    (void)tint_argb;
    if (!g_display) return 1;
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_primary_win;
    if (!xid) return 1;
    Atom blur = x11_atom("_KDE_NET_WM_BLUR_BEHIND_REGION");
    long empty = 0;
    XChangeProperty(g_display, xid, blur, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&empty, 0);
    XFlush(g_display);
    return 0;
}

EXPORT i64 zan_gui_disable_glass(i64 hwnd_val) {
    if (!g_display) return 1;
    Window xid = hwnd_val ? (Window)(intptr_t)hwnd_val : g_primary_win;
    if (!xid) return 1;
    Atom blur = x11_atom("_KDE_NET_WM_BLUR_BEHIND_REGION");
    XDeleteProperty(g_display, xid, blur);
    XFlush(g_display);
    return 0;
}

EXPORT i64 zan_gui_get_dpi_scale(void) {
    if (!g_display) return 100;
    int screen = DefaultScreen(g_display);
    int wpx = DisplayWidth(g_display, screen);
    int wmm = DisplayWidthMM(g_display, screen);
    if (wmm <= 0) return 100;
    double dpi = (double)wpx * 25.4 / (double)wmm;
    long scale = (long)(dpi * 100.0 / 96.0 + 0.5);
    if (scale < 100) scale = 100; /* never report sub-1x scaling */
    return (i64)scale;
}

EXPORT i64 zan_gui_set_clipboard(const char *utf8) {
    if (!g_display || !g_x11_window) return 1;
    free(g_clip_text_linux);
    g_clip_text_linux = utf8 ? strdup(utf8) : strdup("");
    XSetSelectionOwner(g_display, x11_atom("CLIPBOARD"), g_x11_window, CurrentTime);
    XSetSelectionOwner(g_display, XA_PRIMARY, g_x11_window, CurrentTime);
    XFlush(g_display);
    return 0;
}

typedef struct {
    Atom selection;
    Window requestor;
} x11_selection_match_t;

static Bool x11_selection_notify(Display *display, XEvent *event, XPointer arg) {
    (void)display;
    x11_selection_match_t *match = (x11_selection_match_t *)arg;
    return event->type == SelectionNotify &&
           event->xselection.selection == match->selection &&
           event->xselection.requestor == match->requestor;
}

static int x11_read_selection(Atom selection, Atom target, char **out) {
    Atom property = x11_atom("ZAN_GUI_CLIPBOARD");
    XDeleteProperty(g_display, g_x11_window, property);
    XConvertSelection(g_display, selection, target, property,
                      g_x11_window, CurrentTime);
    XFlush(g_display);

    i64 deadline = zan_gui_get_tick_ms() + 1000;
    x11_selection_match_t match = { selection, g_x11_window };
    for (;;) {
        XEvent ev;
        while (XCheckTypedEvent(g_display, SelectionRequest, &ev)) {
            x11_serve_selection(&ev.xselectionrequest);
        }
        if (XCheckIfEvent(g_display, &ev, x11_selection_notify,
                          (XPointer)&match)) {
            if (ev.xselection.property == None) return 0;
            Atom actual_type = None;
            int actual_format = 0;
            unsigned long nitems = 0, bytes_after = 0;
            unsigned char *data = NULL;
            int rc = XGetWindowProperty(
                g_display, g_x11_window, property, 0, 4 * 1024 * 1024,
                True, AnyPropertyType, &actual_type, &actual_format,
                &nitems, &bytes_after, &data);
            if (rc != Success || !data || actual_format != 8 ||
                actual_type == x11_atom("INCR") || bytes_after != 0) {
                if (data) XFree(data);
                return 0;
            }
            char *copy = (char *)malloc((size_t)nitems + 1);
            if (!copy) {
                XFree(data);
                return 0;
            }
            memcpy(copy, data, (size_t)nitems);
            copy[nitems] = '\0';
            XFree(data);
            *out = copy;
            return 1;
        }

        i64 remain = deadline - zan_gui_get_tick_ms();
        if (remain <= 0) return 0;
        struct pollfd pfd;
        pfd.fd = ConnectionNumber(g_display);
        pfd.events = POLLIN;
        pfd.revents = 0;
        int wait_ms = remain > 1000 ? 1000 : (int)remain;
        if (poll(&pfd, 1, wait_ms) < 0) return 0;
    }
}

EXPORT void zan_gui_set_ime_pos(i64 x, i64 y) {
    /* X11 IME (XIM over-the-spot) not wired yet; accept + ignore. */
    (void)x; (void)y;
}

EXPORT const char *zan_gui_get_clipboard(void) {
    if (!g_display || !g_x11_window) return "";
    Atom clipboard = x11_atom("CLIPBOARD");
    Window owner = XGetSelectionOwner(g_display, clipboard);
    if (owner == g_x11_window)
        return g_clip_text_linux ? g_clip_text_linux : "";
    if (owner == None) return "";

    char *text = NULL;
    if (!x11_read_selection(clipboard, x11_atom("UTF8_STRING"), &text) &&
        !x11_read_selection(clipboard, XA_STRING, &text))
        return "";
    free(g_clip_read_linux);
    g_clip_read_linux = text;
    return g_clip_read_linux;
}
#endif /* __linux__ && !ZAN_GUI_SDL (X11 window management) */

/* ========================================================================
 * macOS (and other non-Windows, non-Linux) windowing.
 *
 * When ZAN_GUI_COCOA is defined the real Cocoa backend in gui_runtime_mac.m
 * provides these; otherwise they are no-op stubs so the library still links.
 * Text rendering is the shared software path above, so it is omitted here.
 * The unified SDL backend (ZAN_GUI_SDL) supplies real implementations, so the
 * stubs are compiled out there too. */
#if !defined(_WIN32) && !defined(__linux__) && !defined(ZAN_GUI_COCOA) && !defined(ZAN_GUI_SDL)

EXPORT i64 zan_gui_create_window(const char *t, i64 w, i64 h) { (void)t;(void)w;(void)h; return 0; }
EXPORT i64 zan_gui_show_window(i64 h) { (void)h; return 0; }
EXPORT i64 zan_gui_wait_event(void) { return -1; }
EXPORT i64 zan_gui_poll_event(void) { return -1; }
EXPORT i64 zan_gui_wake(void) { return 0; }
EXPORT i64 zan_gui_event_kind(void) { return 0; }
EXPORT i64 zan_gui_event_x(void) { return 0; }
EXPORT i64 zan_gui_event_y(void) { return 0; }
EXPORT i64 zan_gui_event_button(void) { return 0; }
EXPORT i64 zan_gui_event_keycode(void) { return 0; }
EXPORT i64 zan_gui_event_mods(void) { return 0; }
EXPORT i64 zan_gui_window_width(void) { return 0; }
EXPORT i64 zan_gui_window_height(void) { return 0; }
EXPORT i64 zan_gui_event_hwnd(void) { return 0; }
EXPORT i64 zan_gui_client_width(i64 h) { (void)h; return 0; }
EXPORT i64 zan_gui_client_height(i64 h) { (void)h; return 0; }
EXPORT i64 zan_gui_present(i64 h, i64 s) { (void)h;(void)s; return 1; }
EXPORT i64 zan_gui_set_title(i64 h, const char *t) { (void)h;(void)t; return 0; }
EXPORT i64 zan_gui_set_cursor(i64 c) { (void)c; return 0; }
EXPORT i64 zan_gui_get_tick_ms(void) { return 0; }
EXPORT void zan_gui_sleep_ms(i64 ms) { (void)ms; }

#endif

/* ========================================================================
 * Portable / fallback shims for functions native only to Win32.
 * ========================================================================
 * The Win32, X11 and Cocoa backends all draw their own client-side title bar
 * and supply these; only the headless/no-op fallback reports 0. */
#if !defined(_WIN32) && !defined(__linux__) && !defined(ZAN_GUI_COCOA) && !defined(ZAN_GUI_SDL)
EXPORT i64 zan_gui_caption_button_width(void) { return 0; }
EXPORT i64 zan_gui_titlebar_height(void) { return 0; }
EXPORT i64 zan_gui_set_caption_buttons(i64 hwnd_val, i64 count) { (void)hwnd_val; (void)count; return 0; }
#endif

/* write_file is portable across every non-Win32 backend (X11, Cocoa and the
 * no-op fallback all need it), so it lives outside the CSD-metrics guard. The
 * SDL backend supplies its own (SDL_IOStream) write_file. */
#if !defined(_WIN32) && !defined(ZAN_GUI_SDL)
EXPORT i64 zan_gui_write_file(const char *path, const char *utf8) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    if (utf8) fwrite(utf8, 1, strlen(utf8), f);
    fclose(f);
    return 0;
}
#endif

/* Native translucent glass. Windows has the DWM-acrylic implementation above.
 * macOS (NSVisualEffectView) and Linux (compositor blur) get real backends in
 * their own sections; any other target links a no-op so the cross-platform
 * Gui.Native.Window.EnableGlass entry points resolve everywhere. */
#if !defined(_WIN32) && !defined(__linux__) && !defined(ZAN_GUI_COCOA) && !defined(ZAN_GUI_SDL)
EXPORT i64 zan_gui_enable_glass(i64 hwnd_val, i64 tint_argb) {
    (void)hwnd_val; (void)tint_argb; return 1;
}
EXPORT i64 zan_gui_disable_glass(i64 hwnd_val) { (void)hwnd_val; return 1; }
#endif

/* Window management: X11 has real implementations in the __linux__ branch
 * above; the Cocoa backend (ZAN_GUI_COCOA) provides them on macOS. Any other
 * non-Windows target falls back to no-ops. */
#if !defined(_WIN32) && !defined(__linux__) && !defined(ZAN_GUI_COCOA) && !defined(ZAN_GUI_SDL)
EXPORT i64 zan_gui_get_dpi_scale(void) { return 100; }
EXPORT i64 zan_gui_close_window(i64 hwnd_val) { (void)hwnd_val; return 0; }
EXPORT i64 zan_gui_minimize(i64 hwnd_val) { (void)hwnd_val; return 0; }
EXPORT i64 zan_gui_toggle_maximize(i64 hwnd_val) { (void)hwnd_val; return 0; }
EXPORT i64 zan_gui_is_maximized(i64 hwnd_val) { (void)hwnd_val; return 0; }
EXPORT i64 zan_gui_set_topmost(i64 hwnd_val, i64 on) { (void)hwnd_val; (void)on; return 0; }
EXPORT i64 zan_gui_set_clipboard(const char *utf8) { (void)utf8; return 0; }
EXPORT const char *zan_gui_get_clipboard(void) { return ""; }
EXPORT void zan_gui_set_ime_pos(i64 x, i64 y) { (void)x; (void)y; }
#endif

/* ========================================================================
 * Embedded WebView (native browser control).
 *
 * A real, navigable web view is a heavyweight, per-platform native control
 * (WKWebView on macOS, WebView2 on Windows, WebKitGTK on Linux). Only the
 * macOS backend (gui_runtime_mac.m, ZAN_GUI_COCOA) currently implements it, as
 * a WKWebView subview of the window's content view with cookie access, a
 * navigation delegate (URL/title/status monitoring) and JavaScript eval.
 * Everywhere else these are no-op stubs: zan_gui_webview_create returns 0 so
 * the Zan WebView widget can detect the lack of native support and paint an
 * in-canvas placeholder instead of embedding a live browser.
 * ======================================================================== */
#if !defined(ZAN_GUI_COCOA)
/* profile_id selects a per-account isolation profile (see WebView.zan). When a
 * real backend is added here it should map profile_id to that engine's data
 * partitioning: on Windows a WebView2 per-profile user-data folder (distinct
 * CreateCoreWebView2EnvironmentWithOptions userDataFolder, or ICoreWebView2Profile
 * on newer runtimes); on Linux a WebKitGTK WebKitWebsiteDataManager with a
 * per-profile base data/cache directory. Until then it is a no-op stub. */
EXPORT i64 zan_gui_webview_create(i64 hwnd, const char *profile_id) {
    (void)hwnd; (void)profile_id; return 0;
}
EXPORT void zan_gui_webview_destroy(i64 h) { (void)h; }
EXPORT void zan_gui_webview_set_frame(i64 h, i64 x, i64 y, i64 w, i64 hh) {
    (void)h; (void)x; (void)y; (void)w; (void)hh;
}
EXPORT void zan_gui_webview_set_visible(i64 h, i64 visible) {
    (void)h; (void)visible;
}
EXPORT void zan_gui_webview_navigate(i64 h, const char *url) {
    (void)h; (void)url;
}
EXPORT void zan_gui_webview_load_html(i64 h, const char *html, const char *base_url) {
    (void)h; (void)html; (void)base_url;
}
EXPORT void zan_gui_webview_back(i64 h) { (void)h; }
EXPORT void zan_gui_webview_forward(i64 h) { (void)h; }
EXPORT void zan_gui_webview_reload(i64 h) { (void)h; }
EXPORT void zan_gui_webview_stop(i64 h) { (void)h; }
EXPORT i64 zan_gui_webview_can_go_back(i64 h) { (void)h; return 0; }
EXPORT i64 zan_gui_webview_can_go_forward(i64 h) { (void)h; return 0; }
EXPORT i64 zan_gui_webview_is_loading(i64 h) { (void)h; return 0; }
EXPORT i64 zan_gui_webview_nav_seq(i64 h) { (void)h; return 0; }
EXPORT i64 zan_gui_webview_last_status(i64 h) { (void)h; return 0; }
EXPORT const char *zan_gui_webview_get_url(i64 h) { (void)h; return ""; }
EXPORT const char *zan_gui_webview_get_title(i64 h) { (void)h; return ""; }
EXPORT const char *zan_gui_webview_last_request(i64 h) { (void)h; return ""; }
EXPORT const char *zan_gui_webview_eval(i64 h, const char *js) {
    (void)h; (void)js; return "";
}
EXPORT const char *zan_gui_webview_get_cookies(i64 h, const char *url) {
    (void)h; (void)url; return "";
}
EXPORT void zan_gui_webview_set_cookie(i64 h, const char *url,
                                       const char *name, const char *value) {
    (void)h; (void)url; (void)name; (void)value;
}
EXPORT void zan_gui_webview_clear_cookies(i64 h) { (void)h; }
#endif
