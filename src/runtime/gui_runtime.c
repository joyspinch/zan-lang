/*
 * zan_gui runtime — software 2D renderer with anti-aliasing + system font rendering
 * Cross-platform: Win32 (full), X11 (Linux), Cocoa stubs (macOS)
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
#include <X11/keysym.h>
#include <time.h>
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT __attribute__((visibility("default")))
#endif

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
    if (g_surface_count >= 64) return -1;
    zan_surface_t *s = (zan_surface_t *)calloc(1, sizeof(zan_surface_t));
    s->width = (int)width;
    s->height = (int)height;
    s->stride = (int)width;
    s->pixels = (u32 *)calloc((size_t)(width * height), sizeof(u32));
    int id = g_surface_count++;
    g_surfaces[id] = s;
    return (i64)id;
}

EXPORT i64 zan_gui_destroy_surface(i64 id) {
    if (id < 0 || id >= g_surface_count || !g_surfaces[id]) return 1;
    free(g_surfaces[id]->pixels);
    free(g_surfaces[id]);
    g_surfaces[id] = NULL;
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

EXPORT void zan_gui_clear(i64 surface_id, i64 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    u32 c = (u32)color;
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

    /* Center areas (no rounding) */
    zan_gui_fill_rect(surface_id, ix + r, iy, iw - 2*r, ih, color);
    zan_gui_fill_rect(surface_id, ix, iy + r, r, ih - 2*r, color);
    zan_gui_fill_rect(surface_id, ix + iw - r, iy + r, r, ih - 2*r, color);

    /* Four rounded corners with AA */
    int corners[4][2] = {
        {ix + r, iy + r},               /* top-left */
        {ix + iw - r - 1, iy + r},      /* top-right */
        {ix + r, iy + ih - r - 1},       /* bottom-left */
        {ix + iw - r - 1, iy + ih - r - 1} /* bottom-right */
    };

    for (int ci = 0; ci < 4; ci++) {
        int ccx = corners[ci][0];
        int ccy = corners[ci][1];
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                double dist = sqrt((double)(dx*dx + dy*dy));
                if (dist <= r - 0.5) {
                    set_pixel(s, ccx + dx, ccy + dy, c);
                } else if (dist <= r + 0.5) {
                    int cov = (int)((r + 0.5 - dist) * 255.0);
                    set_pixel_aa(s, ccx + dx, ccy + dy, c, cov);
                }
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
        /* Thick line: fill along perpendicular */
        double dx = (double)(x1 - x0), dy = (double)(y1 - y0);
        double len = sqrt(dx*dx + dy*dy);
        if (len < 0.001) return;
        double nx = -dy/len * t * 0.5, ny = dx/len * t * 0.5;
        int steps = (int)(len + 0.5);
        if (steps < 1) steps = 1;
        for (int i = 0; i <= steps; i++) {
            double fx = x0 + dx * i / steps;
            double fy = y0 + dy * i / steps;
            for (int j = -t/2; j <= t/2; j++) {
                int px = (int)(fx + (-dy/len) * j + 0.5);
                int py = (int)(fy + (dx/len) * j + 0.5);
                set_pixel(s, px, py, c);
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

EXPORT void zan_gui_draw_text(i64 surface_id, i64 x, i64 y, const char *text, i64 color, i64 font_size) {
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
static int g_pending_event[8]; /* kind, x, y, button, keycode, modifiers, 0, 0 */
static int g_has_event = 0;
static int g_window_width = 0, g_window_height = 0;
/* Borderless (client-covers-everything) window chrome metrics, in device px. */
static int g_dpi = 96;
static int g_titlebar_h = 32;
static int g_btn_w = 46;
/* Number of caption buttons drawn on the right of the title bar (theme, pin,
 * minimize, maximize, close). The rightmost N*btn_w px are treated as client
 * area (clickable) rather than draggable caption. */
static int g_caption_btn_count = 5;

static LRESULT CALLBACK ZanGuiWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DESTROY:
        g_pending_event[0] = 8;
        g_has_event = 1;
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        g_window_width = LOWORD(lp);
        g_window_height = HIWORD(lp);
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
        g_has_event = 1;
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        g_pending_event[0] = 3; g_pending_event[3] = 0;
        g_pending_event[1] = (short)LOWORD(lp);
        g_pending_event[2] = (short)HIWORD(lp);
        g_has_event = 1;
        ReleaseCapture();
        return 0;
    case WM_RBUTTONDOWN:
        g_pending_event[0] = 2; g_pending_event[3] = 1;
        g_pending_event[1] = (short)LOWORD(lp);
        g_pending_event[2] = (short)HIWORD(lp);
        g_has_event = 1;
        return 0;
    case WM_RBUTTONUP:
        g_pending_event[0] = 3; g_pending_event[3] = 1;
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
    g_main_hwnd = hwnd;
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

EXPORT i64 zan_gui_present(i64 hwnd_val, i64 surface_id) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    if (surface_id < 0 || surface_id >= g_surface_count || !g_surfaces[surface_id]) return 1;
    zan_surface_t *s = g_surfaces[surface_id];

    HDC hdc = GetDC(hwnd);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = s->width;
    bmi.bmiHeader.biHeight = -(int)s->height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    if (s->width == g_window_width && s->height == g_window_height) {
        SetDIBitsToDevice(hdc,
            0, 0, (DWORD)s->width, (DWORD)s->height,
            0, 0, 0, (UINT)s->height,
            s->pixels, &bmi, DIB_RGB_COLORS);
    } else {
        SetStretchBltMode(hdc, HALFTONE);
        StretchDIBits(hdc,
            0, 0, g_window_width, g_window_height,
            0, 0, s->width, s->height,
            s->pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
    }

    ReleaseDC(hwnd, hdc);
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
    return (i64)GetTickCount64();
}

#endif /* _WIN32 */

/* ========================================================================
 * Linux X11 Window Shell
 * ======================================================================== */

#ifdef __linux__

static Display *g_display = NULL;
static Window g_x11_window = 0;
static GC g_gc = 0;
static int g_pending_event_linux[8];
static int g_has_event_linux = 0;
static int g_win_w = 0, g_win_h = 0;

EXPORT i64 zan_gui_create_window(const char *title, i64 width, i64 height) {
    g_display = XOpenDisplay(NULL);
    if (!g_display) return 0;

    int screen = DefaultScreen(g_display);
    g_x11_window = XCreateSimpleWindow(g_display, RootWindow(g_display, screen),
        0, 0, (unsigned)width, (unsigned)height, 0,
        BlackPixel(g_display, screen), WhitePixel(g_display, screen));

    XStoreName(g_display, g_x11_window, title);
    XSelectInput(g_display, g_x11_window,
        ExposureMask | KeyPressMask | KeyReleaseMask |
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
        StructureNotifyMask);

    Atom wm_delete = XInternAtom(g_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_display, g_x11_window, &wm_delete, 1);

    g_gc = XCreateGC(g_display, g_x11_window, 0, NULL);
    g_win_w = (int)width;
    g_win_h = (int)height;

    return (i64)g_x11_window;
}

EXPORT i64 zan_gui_show_window(i64 win) {
    (void)win;
    if (g_display && g_x11_window) {
        XMapWindow(g_display, g_x11_window);
        XFlush(g_display);
    }
    return 0;
}

EXPORT i64 zan_gui_wait_event(void) {
    if (!g_display) return -1;
    g_has_event_linux = 0;
    memset(g_pending_event_linux, 0, sizeof(g_pending_event_linux));

    XEvent ev;
    XNextEvent(g_display, &ev);

    switch (ev.type) {
    case MotionNotify:
        g_pending_event_linux[0] = 1;
        g_pending_event_linux[1] = ev.xmotion.x;
        g_pending_event_linux[2] = ev.xmotion.y;
        break;
    case ButtonPress:
        g_pending_event_linux[0] = 2;
        g_pending_event_linux[1] = ev.xbutton.x;
        g_pending_event_linux[2] = ev.xbutton.y;
        g_pending_event_linux[3] = ev.xbutton.button - 1;
        break;
    case ButtonRelease:
        g_pending_event_linux[0] = 3;
        g_pending_event_linux[1] = ev.xbutton.x;
        g_pending_event_linux[2] = ev.xbutton.y;
        g_pending_event_linux[3] = ev.xbutton.button - 1;
        break;
    case KeyPress:
        g_pending_event_linux[0] = 4;
        g_pending_event_linux[4] = (int)XLookupKeysym(&ev.xkey, 0);
        break;
    case KeyRelease:
        g_pending_event_linux[0] = 5;
        g_pending_event_linux[4] = (int)XLookupKeysym(&ev.xkey, 0);
        break;
    case ConfigureNotify:
        g_pending_event_linux[0] = 7;
        g_win_w = ev.xconfigure.width;
        g_win_h = ev.xconfigure.height;
        g_pending_event_linux[1] = g_win_w;
        g_pending_event_linux[2] = g_win_h;
        break;
    case ClientMessage:
        g_pending_event_linux[0] = 8;
        break;
    }
    g_has_event_linux = 1;
    return 0;
}

EXPORT i64 zan_gui_poll_event(void) {
    if (!g_display) return -1;
    g_has_event_linux = 0;
    memset(g_pending_event_linux, 0, sizeof(g_pending_event_linux));
    if (XPending(g_display) <= 0) return 1;
    return zan_gui_wait_event();
}

EXPORT i64 zan_gui_event_kind(void)    { return g_pending_event_linux[0]; }
EXPORT i64 zan_gui_event_x(void)       { return g_pending_event_linux[1]; }
EXPORT i64 zan_gui_event_y(void)       { return g_pending_event_linux[2]; }
EXPORT i64 zan_gui_event_button(void)  { return g_pending_event_linux[3]; }
EXPORT i64 zan_gui_event_keycode(void) { return g_pending_event_linux[4]; }
EXPORT i64 zan_gui_event_mods(void)    { return g_pending_event_linux[5]; }

EXPORT i64 zan_gui_window_width(void)  { return g_win_w; }
EXPORT i64 zan_gui_window_height(void) { return g_win_h; }

EXPORT i64 zan_gui_present(i64 hwnd_val, i64 surface_id) {
    if (!g_display) return 1;
    if (surface_id < 0 || surface_id >= g_surface_count || !g_surfaces[surface_id]) return 1;
    zan_surface_t *s = g_surfaces[surface_id];

    XImage *img = XCreateImage(g_display, DefaultVisual(g_display, DefaultScreen(g_display)),
        24, ZPixmap, 0, (char *)s->pixels, (unsigned)s->width, (unsigned)s->height, 32, 0);
    if (img) {
        img->byte_order = LSBFirst;
        XPutImage(g_display, g_x11_window, g_gc, img, 0, 0, 0, 0,
                  (unsigned)s->width, (unsigned)s->height);
        img->data = NULL;
        XDestroyImage(img);
    }
    XFlush(g_display);
    return 0;
}

EXPORT i64 zan_gui_set_title(i64 hwnd_val, const char *title) {
    (void)hwnd_val;
    if (g_display && g_x11_window) {
        XStoreName(g_display, g_x11_window, title);
        XFlush(g_display);
    }
    return 0;
}

EXPORT i64 zan_gui_set_cursor(i64 cursor_type) {
    /* TODO: X11 cursor support */
    (void)cursor_type;
    return 0;
}

EXPORT i64 zan_gui_get_tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (i64)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* Fallback bitmap font for Linux text rendering */
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

EXPORT void zan_gui_draw_text(i64 surface_id, i64 x, i64 y, const char *text, i64 color, i64 font_size) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !text) return;
    u32 c = (u32)color;
    int scale = (int)font_size / 10;
    if (scale < 1) scale = 1;
    int cx = (int)x;
    int cy_base = (int)y;
    while (*text) {
        unsigned char ch = (unsigned char)*text;
        if (ch < 32 || ch > 127) { cx += 6 * scale; text++; continue; }
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
        text++;
    }
}

EXPORT i64 zan_gui_measure_text(const char *text, i64 font_size) {
    if (!text) return 0;
    int scale = (int)font_size / 10;
    if (scale < 1) scale = 1;
    int len = 0;
    while (*text) { len++; text++; }
    return (i64)(len * 6 * scale);
}

EXPORT i64 zan_gui_font_height(i64 font_size) {
    int scale = (int)font_size / 10;
    if (scale < 1) scale = 1;
    return (i64)(10 * scale);
}

EXPORT void zan_gui_draw_icon(i64 s, i64 x, i64 y, i64 b, i64 c, i64 cp) { (void)s;(void)x;(void)y;(void)b;(void)c;(void)cp; }
EXPORT i64 zan_gui_set_cursor(i64 cursor_type) { (void)cursor_type; return 0; }
EXPORT i64 zan_gui_set_title(i64 h, const char *t) { (void)h; (void)t; return 0; }
EXPORT i64 zan_gui_get_tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (i64)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

#endif /* __linux__ */

/* ========================================================================
 * macOS Cocoa stubs (Objective-C required for full implementation)
 * ======================================================================== */
#if !defined(_WIN32) && !defined(__linux__)

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
EXPORT i64 zan_gui_present(i64 h, i64 s) { (void)h;(void)s; return 1; }
EXPORT i64 zan_gui_set_title(i64 h, const char *t) { (void)h;(void)t; return 0; }
EXPORT i64 zan_gui_set_cursor(i64 c) { (void)c; return 0; }
EXPORT i64 zan_gui_get_tick_ms(void) { return 0; }

EXPORT void zan_gui_draw_text(i64 s, i64 x, i64 y, const char *t, i64 c, i64 f) {
    (void)s;(void)x;(void)y;(void)t;(void)c;(void)f;
}
EXPORT i64 zan_gui_measure_text(const char *t, i64 f) { (void)t;(void)f; return 0; }
EXPORT i64 zan_gui_font_height(i64 f) { (void)f; return (i64)f; }
EXPORT void zan_gui_draw_icon(i64 s, i64 x, i64 y, i64 b, i64 c, i64 cp) { (void)s;(void)x;(void)y;(void)b;(void)c;(void)cp; }

#endif
