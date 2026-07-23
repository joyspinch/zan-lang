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
    int x0 = clamp_i((int)x, s->clip_x0, s->clip_x1);
    int y0 = clamp_i((int)y, s->clip_y0, s->clip_y1);
    int x1 = clamp_i((int)(x + w), s->clip_x0, s->clip_x1);
    int y1 = clamp_i((int)(y + h), s->clip_y0, s->clip_y1);
    u32 sa = (c >> 24) & 0xFF;
    if (sa == 255) {
        for (int py = y0; py < y1; py++) {
            u32 *row = s->pixels + py * s->stride;
            for (int px = x0; px < x1; px++) row[px] = c;
        }
    } else if (sa != 0) {
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
EXPORT void zan_gui_fill_vgrad(i64 surface_id, i64 x, i64 y, i64 w, i64 h,
                               i64 color_top, i64 color_bottom) {
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
    int sid; /* owning surface: slots are shared across windows, so a slot
              * must never be restored into a different window's surface
              * (that painted one window's pixels into another during e.g. a
              * child-window open animation). */
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
    if (!dirty && c->valid && c->pixels && c->sid == (int)surface_id
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
    c->sid = (int)surface_id;
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
    c->sid = (int)surface_id;
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
    if (!c->valid || !c->pixels || c->sid != (int)surface_id
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

/* Restores just the part of slot `slot` that intersects [x,y,w,h]. Unlike
 * zan_gui_restore_rect the rect need not match the snapshot's geometry, so a
 * caller can repair many small damaged regions (e.g. the previous animation
 * frame's particles) without copying the whole snapshot back. Returns 1 while
 * the slot holds a valid snapshot for this surface size, 0 otherwise. */
EXPORT i64 zan_gui_restore_sub_rect(i64 surface_id, i64 x, i64 y, i64 w,
                                    i64 h, i64 slot) {
    if (surface_id < 0 || surface_id >= g_surface_count) return 0;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return 0;
    if (slot < 0 || slot >= ZAN_SNAP_CACHE_SLOTS) return 0;
    zan_blur_cache_t *c = &g_snap_cache[slot];
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
    for (int ci = 0; ci < 4; ci++) {
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

/* Anti-aliased stroked rounded rectangle. Straight edges are crisp (solid
 * fill_rect between the corners); the four corner arcs are anti-aliased rings
 * that share the fill's corner centres/radius so the border hugs a matching
 * fill_rounded_rect exactly instead of the old square DrawRect outline. */
EXPORT void zan_gui_draw_rounded_rect(i64 surface_id, i64 x, i64 y, i64 w, i64 h, i64 radius, i64 color, i64 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    u32 c = (u32)color;
    int ix = (int)x, iy = (int)y, iw = (int)w, ih = (int)h;
    int r = (int)radius, th = (int)thickness;
    if (th < 1) th = 1;
    if (r <= 0) { zan_gui_draw_rect(surface_id, x, y, w, h, color, thickness); return; }
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
    double rin = (double)r - (double)th;   /* inner arc radius */
    for (int ci = 0; ci < 4; ci++) {
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

/* Anti-aliased circle outline: a ring of the given thickness centered on
 * the radius, with smooth coverage falloff at both edges. */
EXPORT void zan_gui_draw_circle(i64 surface_id, i64 cx, i64 cy, i64 radius,
                                i64 color, i64 thickness) {
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
EXPORT void zan_gui_fill_radial(i64 surface_id, i64 cx, i64 cy, i64 radius,
                                i64 color, i64 inner_a) {
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
