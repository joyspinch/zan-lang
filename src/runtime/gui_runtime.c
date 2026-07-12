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
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
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
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <poll.h>
#include <time.h>
#include <locale.h>
#ifdef ZAN_GUI_FREETYPE
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#endif
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT __attribute__((visibility("default")))
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

/* Alpha blend src over dst */
static u32 blend_over(u32 dst, u32 src) {
    u32 sa = (src >> 24) & 0xFF;
    if (sa == 255) return src;
    if (sa == 0)   return dst;
    u32 sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
    u32 dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    u32 inv_sa = 255 - sa;
    u32 or_ = (sr * sa + dr * inv_sa) / 255;
    u32 og = (sg * sa + dg * inv_sa) / 255;
    u32 ob = (sb * sa + db * inv_sa) / 255;
    return (255u << 24) | (or_ << 16) | (og << 8) | ob;
}

static void set_pixel(zan_surface_t *s, int x, int y, u32 color) {
    if (x < 0 || x >= s->width || y < 0 || y >= s->height) return;
    int idx = y * s->stride + x;
    s->pixels[idx] = blend_over(s->pixels[idx], color);
}

/* Set pixel with coverage alpha applied on top of color's own alpha */
static void set_pixel_aa(zan_surface_t *s, int x, int y, u32 color, int coverage) {
    if (x < 0 || x >= s->width || y < 0 || y >= s->height) return;
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
    u32 c = (u32)color;
    g_bg_color = c;
    int count = s->width * s->height;
    for (int i = 0; i < count; i++) s->pixels[i] = c;
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

EXPORT void zan_gui_draw_text(i64 surface_id, i64 x, i64 y,
                              const char *text, i64 color, i64 font_size) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !text || !*text) return;

    int size = (int)font_size;
    if (size < 8) size = 8;

    ensure_text_dc();

    /* Convert text to wide string */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *wtext = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, wlen);
    int text_len = wlen - 1;

    /* Measure text */
    HFONT font = get_or_create_font(size);
    HFONT old_font = (HFONT)SelectObject(g_text_dc, font);
    SIZE text_size;
    GetTextExtentPoint32W(g_text_dc, wtext, text_len, &text_size);

    int tw = text_size.cx + 2;
    int th = text_size.cy + 2;
    if (tw <= 0 || th <= 0) { free(wtext); SelectObject(g_text_dc, old_font); return; }

    /* Create a temporary bitmap for text rendering */
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = tw;
    bmi.bmiHeader.biHeight = -th;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    HBITMAP hbmp = CreateDIBSection(g_text_dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hbmp) { free(wtext); SelectObject(g_text_dc, old_font); return; }

    HBITMAP old_bmp = (HBITMAP)SelectObject(g_text_dc, hbmp);
    /* Clear to black */
    memset(bits, 0, (size_t)(tw * th * 4));

    /* Draw white text on black background to get alpha mask */
    SetTextColor(g_text_dc, RGB(255, 255, 255));
    TextOutW(g_text_dc, 0, 0, wtext, text_len);
    GdiFlush();

    /* Extract color components */
    u32 cr = ((u32)color >> 16) & 0xFF;
    u32 cg = ((u32)color >> 8) & 0xFF;
    u32 cb = (u32)color & 0xFF;

    /* Composite text using per-channel ClearType coverage for sharp rendering */
    u32 *src = (u32 *)bits;
    u32 ca = ((u32)color >> 24) & 0xFF;
    if (ca == 0) ca = 255; /* treat 0 alpha as opaque for text */
    for (int py = 0; py < th; py++) {
        int dst_y = (int)y + py;
        if (dst_y < 0 || dst_y >= s->height) continue;
        for (int px = 0; px < tw; px++) {
            int dst_x = (int)x + px;
            if (dst_x < 0 || dst_x >= s->width) continue;
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

    SelectObject(g_text_dc, old_bmp);
    SelectObject(g_text_dc, old_font);
    DeleteObject(hbmp);
    free(wtext);
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

/* ---- Icon font glyph rendering (crisp, aligned) ----
 * Uses "Segoe MDL2 Assets" (present on Win10/11) so icons are real font
 * glyphs instead of hand-drawn vectors: sharp edges + consistent metrics. */
static HFONT g_icon_font = NULL;
static int g_icon_font_size = 0;

static HFONT get_or_create_icon_font(int size) {
    if (g_icon_font && g_icon_font_size == size) return g_icon_font;
    if (g_icon_font) DeleteObject(g_icon_font);
    g_icon_font = CreateFontW(
        -size, 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe MDL2 Assets"
    );
    g_icon_font_size = size;
    return g_icon_font;
}

/* Draw a single icon-font glyph (codepoint) centered inside a `box` square at
 * (x,y), tinted `color`. */
EXPORT void zan_gui_draw_icon(i64 surface_id, i64 x, i64 y, i64 box, i64 color, i64 codepoint) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;

    int bx = (int)box;
    if (bx < 8) bx = 8;
    /* Glyph em slightly smaller than the box so it visually fits. */
    int fsize = bx * 8 / 10;
    if (fsize < 8) fsize = 8;

    ensure_text_dc();
    HFONT font = get_or_create_icon_font(fsize);
    HFONT old_font = (HFONT)SelectObject(g_text_dc, font);

    wchar_t wtext[2];
    wtext[0] = (wchar_t)codepoint;
    wtext[1] = 0;

    SIZE ts;
    GetTextExtentPoint32W(g_text_dc, wtext, 1, &ts);
    int tw = ts.cx + 2;
    int th = ts.cy + 2;
    if (tw <= 0 || th <= 0) { SelectObject(g_text_dc, old_font); return; }

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = tw;
    bmi.bmiHeader.biHeight = -th;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    HBITMAP hbmp = CreateDIBSection(g_text_dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hbmp) { SelectObject(g_text_dc, old_font); return; }
    HBITMAP old_bmp = (HBITMAP)SelectObject(g_text_dc, hbmp);
    memset(bits, 0, (size_t)(tw * th * 4));
    SetTextColor(g_text_dc, RGB(255, 255, 255));
    TextOutW(g_text_dc, 0, 0, wtext, 1);
    GdiFlush();

    /* Center the glyph bitmap within the box. */
    int ox = (int)x + (bx - tw) / 2;
    int oy = (int)y + (bx - th) / 2;

    u32 cr = ((u32)color >> 16) & 0xFF;
    u32 cg = ((u32)color >> 8) & 0xFF;
    u32 cb = (u32)color & 0xFF;
    u32 ca = ((u32)color >> 24) & 0xFF;
    if (ca == 0) ca = 255;

    u32 *src = (u32 *)bits;
    for (int py = 0; py < th; py++) {
        int dst_y = oy + py;
        if (dst_y < 0 || dst_y >= s->height) continue;
        for (int px = 0; px < tw; px++) {
            int dst_x = ox + px;
            if (dst_x < 0 || dst_x >= s->width) continue;
            u32 sp = src[py * tw + px];
            u32 sr = (sp >> 16) & 0xFF;
            u32 sg = (sp >> 8) & 0xFF;
            u32 sb = sp & 0xFF;
            if (sr == 0 && sg == 0 && sb == 0) continue;
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

    SelectObject(g_text_dc, old_bmp);
    SelectObject(g_text_dc, old_font);
    DeleteObject(hbmp);
}

/* ========================================================================
 * Win32 Window Shell
 * ======================================================================== */

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
static void blit_surface_to_hwnd(HWND hwnd, zan_surface_t *s) {
    if (!s) return;
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

static LRESULT CALLBACK ZanGuiWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    g_event_hwnd = hwnd;
    switch (msg) {
    case WM_DESTROY:
        g_pending_event[0] = 8;
        g_has_event = 1;
        /* Only tear the whole app down when the primary window closes; a
         * secondary window (dialog) just reports kind 8 for its own hwnd. */
        if (hwnd == g_main_hwnd) { PostQuitMessage(0); }
        return 0;
    case WM_SIZE:
        g_window_width = LOWORD(lp);
        g_window_height = HIWORD(lp);
        /* Fill the resized client area now (last frame at 1:1 + bg-colored
         * margin) so a maximize/drag doesn't flash black before the app thread
         * repaints crisply. */
        if (g_last_surface >= 0 && g_last_surface < g_surface_count)
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
    case WM_MOUSEWHEEL:
        g_pending_event[0] = 13;
        g_pending_event[4] = GET_WHEEL_DELTA_WPARAM(wp);
        g_has_event = 1;
        return 0;
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
         * DWM) never leave the client black between app repaints. */
        if (g_last_surface >= 0 && g_last_surface < g_surface_count)
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

EXPORT i64 zan_gui_create_window(const char *title, i64 width, i64 height) {
    static int registered = 0;
    static int dpi_set = 0;
    if (!dpi_set) {
        SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
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
    if (!g_main_hwnd) { g_main_hwnd = hwnd; }
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
EXPORT i64 zan_gui_set_caption_buttons(i64 count) {
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

#endif /* _WIN32 */

/* ========================================================================
 * Linux X11 Window Shell
 * ======================================================================== */

#ifdef __linux__

static Display *g_display = NULL;
static Window g_x11_window = 0;
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

    XEvent ev;
    for (;;) {
        XNextEvent(g_display, &ev);
        if (ev.type == SelectionRequest) {
            x11_serve_selection(&ev.xselectionrequest);
            continue;
        }
        if (XFilterEvent(&ev, None)) continue; /* consumed by the input method */
        x11_translate_event(&ev);
        if (evq_pop_linux()) return 0;
    }
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
#endif /* __linux__ */

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

#if !defined(_WIN32)
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
    case 0xE735: case 0xE734:
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
    default:
        break;
    }
}
#endif

#ifdef __linux__
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
EXPORT i64 zan_gui_set_caption_buttons(i64 count) {
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
#endif /* __linux__ */

/* ========================================================================
 * macOS (and other non-Windows, non-Linux) windowing.
 *
 * When ZAN_GUI_COCOA is defined the real Cocoa backend in gui_runtime_mac.m
 * provides these; otherwise they are no-op stubs so the library still links.
 * Text rendering is the shared software path above, so it is omitted here.
 * ======================================================================== */
#if !defined(_WIN32) && !defined(__linux__) && !defined(ZAN_GUI_COCOA)

EXPORT i64 zan_gui_create_window(const char *t, i64 w, i64 h) { (void)t;(void)w;(void)h; return 0; }
EXPORT i64 zan_gui_show_window(i64 h) { (void)h; return 0; }
EXPORT i64 zan_gui_wait_event(void) { return -1; }
EXPORT i64 zan_gui_poll_event(void) { return -1; }
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
 * Client-side-decoration metrics are a Win32 concept; on X11/macOS the window
 * manager owns the title bar, so these report 0. write_file is portable. */
#if !defined(_WIN32) && !defined(__linux__)
EXPORT i64 zan_gui_caption_button_width(void) { return 0; }
EXPORT i64 zan_gui_titlebar_height(void) { return 0; }
EXPORT i64 zan_gui_set_caption_buttons(i64 count) { (void)count; return 0; }
#endif

/* write_file is portable across every non-Win32 backend (X11, Cocoa and the
 * no-op fallback all need it), so it lives outside the CSD-metrics guard. */
#if !defined(_WIN32)
EXPORT i64 zan_gui_write_file(const char *path, const char *utf8) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    if (utf8) fwrite(utf8, 1, strlen(utf8), f);
    fclose(f);
    return 0;
}
#endif

/* Window management: X11 has real implementations in the __linux__ branch
 * above; the Cocoa backend (ZAN_GUI_COCOA) provides them on macOS. Any other
 * non-Windows target falls back to no-ops. */
#if !defined(_WIN32) && !defined(__linux__) && !defined(ZAN_GUI_COCOA)
EXPORT i64 zan_gui_get_dpi_scale(void) { return 100; }
EXPORT i64 zan_gui_close_window(i64 hwnd_val) { (void)hwnd_val; return 0; }
EXPORT i64 zan_gui_minimize(i64 hwnd_val) { (void)hwnd_val; return 0; }
EXPORT i64 zan_gui_toggle_maximize(i64 hwnd_val) { (void)hwnd_val; return 0; }
EXPORT i64 zan_gui_is_maximized(i64 hwnd_val) { (void)hwnd_val; return 0; }
EXPORT i64 zan_gui_set_topmost(i64 hwnd_val, i64 on) { (void)hwnd_val; (void)on; return 0; }
EXPORT i64 zan_gui_set_clipboard(const char *utf8) { (void)utf8; return 0; }
EXPORT const char *zan_gui_get_clipboard(void) { return ""; }
#endif
