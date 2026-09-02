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
#include "gui_backend.h"
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

typedef struct zan_surface_s {
    u32 *pixels;
    int width;
    int height;
    int stride;
    /* Rasterizer for this surface, or NULL for the built-in software one (see
     * gui_backend.h). Set once when the surface is created; every drawing
     * export below hands its call over after validating arguments. */
    const zan_gui_backend *be;
    /* Own slot in g_surfaces: the caches keyed by surface id (blur, snapshot)
     * are reached from the backend entries, which are handed a surface. */
    int id;
    /* Active clip window (x1/y1 exclusive) enforced by every pixel write, so
     * scroll containers can bound their content to a viewport. clip_stack holds
     * saved windows for nested PushClip/PopClip (4 ints per level, 16 levels). */
    int clip_x0;
    int clip_y0;
    int clip_x1;
    int clip_y1;
    int clip_stack[64];
    int clip_depth;
    /* 0 until a whole-surface clear has run on it: a brand-new surface holds
     * whatever the allocator handed back (usually the *previous* surface of a
     * drag-resize, laid out at its old stride), so presenting it paints sheared
     * copies of old frames. The shell polls this and refuses to present. */
    int painted;
} zan_surface_t;

static zan_surface_t *g_surfaces[64];
static int g_surface_count = 0;

/* Backend every new surface is created with. NULL (the built-in software
 * rasterizer) until an application installs one, which is deliberately an
 * application-level choice rather than an environment variable: one switch
 * shared by every Zan program on the machine is not a setting. */
static const zan_gui_backend *g_backend = NULL;

/* The backend that implements `entry` for this surface: its own when it has
 * one, else the built-in software rasterizer. So an export reads as "validate
 * the arguments, then hand the primitive to whoever can draw it", and a backend
 * may implement part of the vtable and inherit the CPU path for the rest. */
static const zan_gui_backend *zan_impl_pick(zan_surface_t *s, int has_entry) {
    if (has_entry) {
        /* The backend is about to draw: if the CPU path wrote the last pixels
         * (a primitive it does not implement), they have to reach it first. */
        if (s->be->sync_from_cpu) s->be->sync_from_cpu(s);
        return s->be;
    }
    /* Falling back to the software rasterizer, which writes into s->pixels:
     * pull whatever the backend has composed into it first. */
    if (s->be && s->be->sync_to_cpu) s->be->sync_to_cpu(s);
    return &zan_cpu_backend;
}

#define ZAN_IMPL(s, entry)                                                \
    zan_impl_pick((s), (s)->be != NULL && (s)->be->entry != NULL)

/* Text still runs its platform rasterizer (GDI / FreeType / CoreText) straight
 * from the export when no backend claims it -- see gui_backend.h. */
#define ZAN_BE(s, entry, ...)                                             \
    do {                                                                  \
        if ((s)->be && (s)->be->entry) {                                  \
            (s)->be->entry(__VA_ARGS__);                                  \
            return;                                                       \
        }                                                                 \
    } while (0)

/* Internal (not part of the [DllImport] ABI): install the backend surfaces
 * created from now on will use, NULL to go back to the software rasterizer.
 * Called by a platform GPU backend once it has a working context -- if it
 * cannot get one it simply never calls this and everything keeps running on
 * the CPU. Surfaces that already exist keep the backend they were born with,
 * so a switch takes effect when the shell next recreates its surface. */
void zan_gui_internal_set_backend(const zan_gui_backend *be) {
    g_backend = be;
}

const char *zan_gui_internal_backend_name(void) {
    return (g_backend && g_backend->name) ? g_backend->name
                                          : zan_cpu_backend.name;
}
/* Declared here because destroy_surface / zan_gui_clear (above their definition
 * below) reference them; the resize re-blit path assigns them later. */
static i64 g_last_surface = -1;
static u32 g_bg_color = 0xFFFFFFFF;

/* Defined with the caches further down; destroy_surface (above them) has to
 * drop every slot the dying surface owns. */
static void zan_cache_drop_surface(int sid);

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

/* SSE2 is baseline on x86-64 (the Windows/Linux desktop targets); anything
 * else takes the scalar run below, which produces the same pixels. */
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define ZAN_HAS_SSE2 1
#else
#define ZAN_HAS_SSE2 0
#endif

/* Source-over of one constant colour across a run of pixels.
 *
 * The per-pixel blend_over above divides three channels by 255; a frame spends
 * most of its raster time in translucent fills (scrims, tints, frosted panels,
 * selection washes), so the run form keeps the identical arithmetic but pays
 * for it once per run instead of once per channel per pixel:
 *   - src * sa is hoisted out of the loop (the colour is constant),
 *   - red and blue ride in one word as two 16-bit lanes (their products are
 *     <= 255*255, so lanes never carry into each other),
 *   - v / 255 becomes (v + 1 + (v >> 8)) >> 8, exact for v <= 65025, which is
 *     the range of s*sa + d*(255-sa).
 * The output is bit-identical to blend_over. Destinations that are not opaque
 * (shaped/glass windows clear the surface to alpha 0) take the general path. */
/* Pixels of translucent fills taken by the 4-wide path vs. the one-at-a-time
 * path, so a profile can tell "blending is expensive" from "blending fell off
 * its fast path" (see zan_gui_stat_read idx 11/12). */
static long long g_blend_px_simd, g_blend_px_scalar;

/* Opaque fill of a run of pixels.
 *
 * A full-window frame writes about twice the surface in opaque fills (window
 * background, then each panel's own background over it), so these writes set
 * the frame's bandwidth floor. Non-temporal (cache-bypassing) stores were
 * measured here and came out slower -- 4.0 ms vs 3.7 ms per full frame on the
 * IDE at 1942x1286 -- because every filled pixel is read back within the same
 * frame by the layers blended on top and by the blit to the window, so keeping
 * the fill in cache is worth more than saving its read-for-ownership. Plain
 * stores it is; clang vectorises this loop. */
static void fill_run_const(u32 *row, int n, u32 c) {
    for (int i = 0; i < n; i++) row[i] = c;
}

static void blend_run_const(u32 *row, int n, u32 src) {
    u32 sa = (src >> 24) & 0xFF;
    if (sa == 0 || n <= 0) return;
    if (sa == 255) {
        for (int i = 0; i < n; i++) row[i] = src;
        return;
    }
    u32 inv = 255 - sa;
    u32 srb = (src & 0x00FF00FFu) * sa;
    u32 sg = ((src >> 8) & 0xFFu) * sa;
    int i = 0;
#if ZAN_HAS_SSE2
    /* Four pixels per step, same integer expression in 16-bit lanes. A group
     * that contains a non-opaque destination falls back to the scalar loop for
     * those four pixels. */
    if (n >= 4) {
        __m128i zero = _mm_setzero_si128();
        __m128i amask = _mm_set1_epi32((int)0xFF000000u);
        __m128i invv = _mm_set1_epi16((short)inv);
        __m128i one = _mm_set1_epi16(1);
        __m128i s4 = _mm_set1_epi32((int)src);
        __m128i sav = _mm_set1_epi16((short)sa);
        __m128i sLo = _mm_mullo_epi16(_mm_unpacklo_epi8(s4, zero), sav);
        __m128i sHi = _mm_mullo_epi16(_mm_unpackhi_epi8(s4, zero), sav);
        for (; i + 4 <= n; i += 4) {
            __m128i d = _mm_loadu_si128((const __m128i *)(row + i));
            __m128i da = _mm_and_si128(d, amask);
            if (_mm_movemask_epi8(_mm_cmpeq_epi8(da, amask)) != 0xFFFF) break;
            g_blend_px_simd += 4;
            __m128i lo = _mm_add_epi16(_mm_mullo_epi16(_mm_unpacklo_epi8(d, zero), invv), sLo);
            __m128i hi = _mm_add_epi16(_mm_mullo_epi16(_mm_unpackhi_epi8(d, zero), invv), sHi);
            lo = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(lo, one), _mm_srli_epi16(lo, 8)), 8);
            hi = _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(hi, one), _mm_srli_epi16(hi, 8)), 8);
            __m128i out = _mm_or_si128(_mm_packus_epi16(lo, hi), amask);
            _mm_storeu_si128((__m128i *)(row + i), out);
        }
    }
#endif
    g_blend_px_scalar += n - i;
    for (; i < n; i++) {
        u32 d = row[i];
        if ((d >> 24) != 0xFFu) { row[i] = blend_over(d, src); continue; }
        u32 vrb = (d & 0x00FF00FFu) * inv + srb;
        vrb = (vrb + 0x00010001u + ((vrb >> 8) & 0x00FF00FFu)) >> 8;
        u32 vg = ((d >> 8) & 0xFFu) * inv + sg;
        vg = (vg + 1 + (vg >> 8)) >> 8;
        row[i] = 0xFF000000u | (vrb & 0x00FF00FFu) | (vg << 8);
    }
}

/* Reset the clip window to the whole surface (frame start / new surface). */
/* Hand the clip window that was just installed to the backend: a GPU backend
 * keeps clipping in its own scissor state instead of testing it per pixel. */
static void clip_notify(zan_surface_t *s) {
    if (s->be && s->be->set_clip) {
        s->be->set_clip(s, s->clip_x0, s->clip_y0, s->clip_x1, s->clip_y1);
    }
}

static void clip_reset_full(zan_surface_t *s) {
    s->clip_x0 = 0;
    s->clip_y0 = 0;
    s->clip_x1 = s->width;
    s->clip_y1 = s->height;
    s->clip_depth = 0;
    clip_notify(s);
}

/* Rect (x1/y1 exclusive) versus the active clip window: 0 when the region
 * cannot write a single pixel this frame. Region operations whose cost is paid
 * up front (blur: downsample + 3 box passes, plus the source checksum) test
 * this first, instead of computing the result and then having the per-pixel
 * clip test throw all of it away -- which is what a damage-clipped frame did
 * to every frosted panel outside the damage strip. */
static int clip_hits(zan_surface_t *s, int x0, int y0, int x1, int y1) {
    if (x1 <= s->clip_x0 || x0 >= s->clip_x1) return 0;
    if (y1 <= s->clip_y0 || y0 >= s->clip_y1) return 0;
    return 1;
}

/* True when the clip window contains the whole rect, i.e. every pixel of the
 * region is writable this frame. */
static int clip_covers(zan_surface_t *s, int x0, int y0, int x1, int y1) {
    return x0 >= s->clip_x0 && y0 >= s->clip_y0
        && x1 <= s->clip_x1 && y1 <= s->clip_y1;
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

/* ---- Fault guard (see rt_crash.h) ---- */

/* Run `fn(arg)` -- a Zan `static void f(nint)` handed over as a function
 * pointer -- with a recovery point installed, and report whether it finished:
 * 1 normally, 0 when a hard fault inside it was logged and abandoned. The GUI
 * loop wraps one event dispatch and one frame in it, so a fault in event or
 * paint code costs that event or frame instead of the whole program. */
EXPORT i32 zan_gui_guard_call(iptr fn, iptr arg) {
    void (*body)(void *) = (void (*)(void *))fn;
    if (!body) return 1;
#if defined(_WIN32)
    return (i32)zan__guard_call(body, (void *)arg);
#else
    /* POSIX: a fatal signal is reported by rt_crash's handler and re-raised;
     * there is no resume point to jump back to, so the call is plain. */
    body((void *)arg);
    return 1;
#endif
}

#if defined(_WIN32)
/* Guarded window procedure. The class is registered with this instead of the
 * Zan procedure directly, and the fault taken by one message is recovered from
 * *inside* the callback: DispatchMessageW then returns normally, having had a
 * (default) reply to the message, and only that message is lost. Recovering
 * one level further out -- around the message loop, as the loop's own guard
 * does -- would instead jump over the middle of Win32's own dispatch, leaving
 * the thread's message state half-finished; a window that stops responding is
 * not a better outcome than one that loses a click. */
typedef iptr (*zan_gui_wndproc_fn)(iptr hwnd, i32 msg, iptr wp, iptr lp);
static zan_gui_wndproc_fn zan_gui_zan_wndproc;

typedef struct {
    HWND hwnd; UINT msg; WPARAM wp; LPARAM lp; iptr result;
} zan_gui_wndproc_args;

static void zan_gui_wndproc_body(void *p) {
    zan_gui_wndproc_args *a = (zan_gui_wndproc_args *)p;
    a->result = zan_gui_zan_wndproc((iptr)a->hwnd, (i32)a->msg,
                                    (iptr)a->wp, (iptr)a->lp);
}

static LRESULT CALLBACK zan_gui_wndproc(HWND hwnd, UINT msg,
                                        WPARAM wp, LPARAM lp) {
    if (!zan_gui_zan_wndproc) return DefWindowProcW(hwnd, msg, wp, lp);
    zan_gui_wndproc_args a;
    a.hwnd = hwnd; a.msg = msg; a.wp = wp; a.lp = lp; a.result = 0;
    if (!zan__guard_call(zan_gui_wndproc_body, &a))
        return DefWindowProcW(hwnd, msg, wp, lp);
    return (LRESULT)a.result;
}
#endif

/* Adopt `proc` (a Zan `static nint f(nint, int, nint, nint)`) as the window
 * procedure and return the address to register in WNDCLASSEX instead. */
EXPORT iptr zan_gui_guard_wndproc(iptr proc) {
#if defined(_WIN32)
    zan_gui_zan_wndproc = (zan_gui_wndproc_fn)proc;
    return (iptr)(void *)&zan_gui_wndproc;
#else
    return proc;
#endif
}

/* How many faults the guard has absorbed in this process. */
EXPORT i32 zan_gui_guard_recovered(void) {
#if defined(_WIN32)
    return (i32)zan__guard_recovered_count();
#else
    return 0;
#endif
}

/* ---- Exported rendering functions ---- */

EXPORT i32 zan_gui_create_surface(i32 width, i32 height) {
    zan__crash_install(); /* idempotent; ensures GUI processes log hard crashes */
    /* Reject non-positive or absurdly large dimensions before multiplying:
     * width*height in int overflows past ~46341^2, and no legitimate surface
     * exceeds 16384 on a side. 16384^2 pixels * 4 bytes = 1 GB, the ceiling a
     * fullscreen 16K panel would need. */
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return -1;
    int64_t px = (int64_t)width * (int64_t)height;
    if (px <= 0 || px > 16384LL * 16384LL) return -1;
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
    if (!s) return -1;
    s->width = (int)width;
    s->height = (int)height;
    s->stride = (int)width;
    s->be = g_backend;
    s->id = id;
    clip_reset_full(s);
    s->pixels = (u32 *)malloc((size_t)px * sizeof(u32));
    if (!s->pixels) { free(s); return -1; }
    /* Prefill with the last background colour instead of leaving the allocator's
     * leftovers: the shell must not present an unpainted surface (see `painted`),
     * but a WM_PAINT that slips through then shows a flat window colour rather
     * than shredded old frames. One memset per resize, not per frame. */
    fill_run_const(s->pixels, (int)px, g_bg_color);
    g_surfaces[id] = s;
    return (i64)id;
}

/* 1 once a whole-surface clear has covered this surface, i.e. it holds a frame
 * rather than the allocator's leftovers. The shell presents nothing else. */
EXPORT i32 zan_gui_surface_painted(i32 id) {
    if (id < 0 || id >= g_surface_count || !g_surfaces[id]) return 0;
    return g_surfaces[id]->painted;
}

EXPORT i32 zan_gui_destroy_surface(i32 id) {
    if (id < 0 || id >= g_surface_count || !g_surfaces[id]) return 1;
    if (g_surfaces[id]->be && g_surfaces[id]->be->drop_surface)
        g_surfaces[id]->be->drop_surface(g_surfaces[id]);
    free(g_surfaces[id]->pixels);
    free(g_surfaces[id]);
    g_surfaces[id] = NULL;
    /* Slot ids are recycled by create_surface, so a snapshot taken before a
     * resize would otherwise still match `sid` on the new surface and get
     * blitted back with the old geometry -- old panels reappearing at old
     * offsets, sheared where the width changed. */
    zan_cache_drop_surface(id);
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

/* Write a self-describing raw ARGB snapshot without passing the pixel buffer
 * through the managed string ABI. The 24-byte header is:
 *   "ZPX1", x, y, width, height, bytes_per_pixel
 * followed by tightly packed rows in native ARGB32 order. */
EXPORT i32 zan_gui_write_pixels(i32 surface_id, const char *path,
                                i32 x, i32 y, i32 width, i32 height) {
    if (surface_id < 0 || surface_id >= g_surface_count
        || !g_surfaces[surface_id] || !path || path[0] == '\0') return -1;
    zan_surface_t *s = g_surfaces[surface_id];
    if (x < 0 || y < 0 || width <= 0 || height <= 0
        || x > s->width - width || y > s->height - height) return -1;
    size_t pixel_bytes = (size_t)width * (size_t)height * sizeof(u32);
    size_t expected = 4 + 5 * sizeof(uint32_t) + pixel_bytes;
    FILE *fp = fopen(path, "wb");
    if (!fp) return -2;
    const char magic[4] = { 'Z', 'P', 'X', '1' };
    uint32_t meta[5];
    meta[0] = (uint32_t)x;
    meta[1] = (uint32_t)y;
    meta[2] = (uint32_t)width;
    meta[3] = (uint32_t)height;
    meta[4] = (uint32_t)sizeof(u32);
    size_t written = 0;
    written += fwrite(magic, 1, sizeof(magic), fp);
    written += fwrite(meta, sizeof(uint32_t), 5, fp) * sizeof(uint32_t);
    if (written != 4 + 5 * sizeof(uint32_t)) {
        fclose(fp);
        return -3;
    }
    for (int row = 0; row < height; row++) {
        written += fwrite(s->pixels + (size_t)(y + row) * (size_t)s->stride + x,
                          sizeof(u32), (size_t)width, fp) * sizeof(u32);
    }
    if (fclose(fp) != 0 || written != expected) return -4;
    return (i32)written;
}

/* Internal (not part of the [DllImport] ABI): expose a surface's raw ARGB
 * buffer to a native platform backend compiled as a separate TU (e.g. the
 * macOS Cocoa .m file), which cannot see the static g_surfaces table. */
u32 *zan_gui_internal_surface_data(i64 id, int *w, int *h, int *stride) {
    if (id < 0 || id >= g_surface_count || !g_surfaces[id]) return NULL;
    zan_surface_t *s = g_surfaces[id];
    /* With a GPU backend the finished frame lives in its own render target, so
     * settle the batches and mirror them back before anyone reads s->pixels
     * (layered-window presents, screenshots, pixel-compare tests). */
    if (s->be) {
        if (s->be->flush) s->be->flush(s);
        if (s->be->read_pixels) s->be->read_pixels(s);
    }
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
    if (s->be && s->be->clear_rect) {
        s->be->clear_rect(s, 0, 0, s->width, s->height, c);
    } else {
        int count = s->width * s->height;
        fill_run_const(s->pixels, count, c);
    }
    s->painted = 1;
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
    clip_notify(s);
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
        clip_notify(s);
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
/* Table 0 tracks translucent fills, table 1 opaque ones: an opaque fill that
 * covers pixels a later layer paints again is pure overdraw, and naming the
 * widest ones is the only way to tell which layer to stop painting. */
#define ZAN_TOP_T 2
static int g_top_a[ZAN_TOP_T][ZAN_TOP_N], g_top_x[ZAN_TOP_T][ZAN_TOP_N],
           g_top_y[ZAN_TOP_T][ZAN_TOP_N], g_top_w[ZAN_TOP_T][ZAN_TOP_N],
           g_top_h[ZAN_TOP_T][ZAN_TOP_N], g_top_c[ZAN_TOP_T][ZAN_TOP_N];

static void stat_top_rect(int t, int area, int x, int y, int w, int h,
                          u32 color) {
    /* The same layer repaints the same rect every frame, so without this an
     * entry over several frames fills every slot with one rect and hides the
     * other layers -- exactly what the table exists to show. */
    for (int i = 0; i < ZAN_TOP_N; i++) {
        if (g_top_a[t][i] == area && g_top_x[t][i] == x &&
            g_top_y[t][i] == y && g_top_w[t][i] == w &&
            g_top_h[t][i] == h && g_top_c[t][i] == (int)color) {
            return;
        }
    }
    int slot = -1;
    for (int i = 0; i < ZAN_TOP_N; i++) {
        if (area > g_top_a[t][i]) { slot = i; break; }
    }
    if (slot < 0) return;
    for (int i = ZAN_TOP_N - 1; i > slot; i--) {
        g_top_a[t][i] = g_top_a[t][i - 1]; g_top_x[t][i] = g_top_x[t][i - 1];
        g_top_y[t][i] = g_top_y[t][i - 1]; g_top_w[t][i] = g_top_w[t][i - 1];
        g_top_h[t][i] = g_top_h[t][i - 1]; g_top_c[t][i] = g_top_c[t][i - 1];
    }
    g_top_a[t][slot] = area; g_top_x[t][slot] = x; g_top_y[t][slot] = y;
    g_top_w[t][slot] = w; g_top_h[t][slot] = h; g_top_c[t][slot] = (int)color;
}

static i32 stat_top_field(int t, i32 rank, i32 field) {
    if (rank < 0 || rank >= ZAN_TOP_N) return 0;
    switch (field) {
        case 0: return g_top_a[t][rank] / 1000;
        case 1: return g_top_x[t][rank];
        case 2: return g_top_y[t][rank];
        case 3: return g_top_w[t][rank];
        case 5: return (g_top_c[t][rank] >> 24) & 0xFF;
        default: break;
    }
    i32 h = g_top_h[t][rank];
    g_top_a[t][rank] = 0;
    return h;
}

/* field: 0 area/1000, 1 x, 2 y, 3 w, 4 h; reading field 4 clears the entry. */
EXPORT i32 zan_gui_stat_top(i32 rank, i32 field) {
    return stat_top_field(0, rank, field);
}

/* Same fields, for the frame's widest opaque fills. */
EXPORT i32 zan_gui_stat_top_fill(i32 rank, i32 field) {
    return stat_top_field(1, rank, field);
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
        case 11:
        case 12: {
            long long *px = idx == 11 ? &g_blend_px_simd : &g_blend_px_scalar;
            if (kind == 0) { return 0; }
            i32 kpx = (i32)(*px / 1000);
            *px = 0;
            return kpx;
        }
        default: return 0;
    }
    if (kind == 0) { return (i32)t->calls; }
    /* kind 2 peeks the pixel counter without clearing it: a single frame's cost
     * is read as a difference between two peeks, which must not disturb the
     * per-batch totals the profiler prints from kind 1. */
    if (kind == 2) { return (i32)(t->px / 1000); }
    i32 kpx = (i32)(t->px / 1000);
    t->calls = 0;
    t->px = 0;
    return kpx;
}

/* zan_gui_clear restricted to a rect: stores the color, alpha included, instead
 * of blending it. A damage-clipped frame begins by re-establishing the window
 * background inside its damage rect only, and it must land on exactly the
 * pixels a whole-frame clear would have produced. Doing that with fill_rect
 * instead blends the background over the previous frame still sitting in the
 * surface, so a translucent (glass) or fully transparent (shaped-window)
 * background either tints the strip once more per partial frame or leaves the
 * stale pixels untouched. */
static void cpu_clear_rect(zan_surface_t *s, int x, int y, int w, int h,
                           u32 c) {
    int x0 = clamp_i(x, s->clip_x0, s->clip_x1);
    int y0 = clamp_i(y, s->clip_y0, s->clip_y1);
    int x1 = clamp_i(x + w, s->clip_x0, s->clip_x1);
    int y1 = clamp_i(y + h, s->clip_y0, s->clip_y1);
    long long area = (long long)(x1 - x0) * (long long)(y1 - y0);
    if (area <= 0) return;
    ZAN_STAT(g_st_fill_op, area);
    stat_top_rect(1, (int)area, x0, y0, x1 - x0, y1 - y0, c);
    for (int py = y0; py < y1; py++) {
        fill_run_const(s->pixels + py * s->stride + x0, x1 - x0, c);
    }
}

EXPORT void zan_gui_clear_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, clear_rect)->clear_rect(s, (int)x, (int)y, (int)w, (int)h,
                                        (u32)color);
}

static void cpu_fill_rect(zan_surface_t *s, int x, int y, int w, int h, u32 c) {
    int x0 = clamp_i(x, s->clip_x0, s->clip_x1);
    int y0 = clamp_i(y, s->clip_y0, s->clip_y1);
    int x1 = clamp_i(x + w, s->clip_x0, s->clip_x1);
    int y1 = clamp_i(y + h, s->clip_y0, s->clip_y1);
    u32 sa = (c >> 24) & 0xFF;
    long long area = (long long)(x1 - x0) * (long long)(y1 - y0);
    if (area < 0) area = 0;
    if (sa == 255) {
        ZAN_STAT(g_st_fill_op, area);
        stat_top_rect(1, (int)area, x0, y0, x1 - x0, y1 - y0, c);
        for (int py = y0; py < y1; py++) {
            fill_run_const(s->pixels + py * s->stride + x0, x1 - x0, c);
        }
    } else if (sa != 0) {
        ZAN_STAT(g_st_fill_blend, area);
        stat_top_rect(0, (int)area, x0, y0, x1 - x0, y1 - y0, c);
        /* Rect is already clamped to the clip window, so blend straight into
         * the row instead of re-clipping every pixel via set_pixel -- these
         * translucent fills (scrims, hover/selection tints) cover large areas. */
        for (int py = y0; py < y1; py++) {
            blend_run_const(s->pixels + py * s->stride + x0, x1 - x0, c);
        }
    }
}

EXPORT void zan_gui_fill_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, fill_rect)->fill_rect(s, (int)x, (int)y, (int)w, (int)h,
                                      (u32)color);
}

/* Vertical linear gradient fill: each row is a lerp between color_top (at y)
 * and color_bottom (at y+h-1). Opaque; used for modern gradient wallpapers
 * (AI / Aurora / Glass backdrops). Single pass, no allocation. */
static int zan_round_cov(int i, int j, int rw, int rh, int cr, int cmask);

/* Optionally clipped to a rounded-rect mask: the unmasked form overwrites every
 * pixel in the rect, so painting it over a FillRoundRect destroys the corner AA
 * (the arcs' blended edge pixels get replaced by opaque gradient) and the
 * corners read as jagged teeth. With a radius it cuts the same silhouette as
 * fill_round: interior pixels are solid, the 1px corner arc is blended with
 * fractional coverage. */
static void cpu_fill_vgrad(zan_surface_t *s, int x, int y, int w, int h,
                           int radius, int corners, u32 ct, u32 cb) {
    int x0 = clamp_i(x, s->clip_x0, s->clip_x1);
    int y0 = clamp_i(y, s->clip_y0, s->clip_y1);
    int x1 = clamp_i(x + w, s->clip_x0, s->clip_x1);
    int y1 = clamp_i(y + h, s->clip_y0, s->clip_y1);
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return;
    int r = radius;
    int m = corners;
    if (r < 0) r = 0;
    if (r > 0 && (m & 15) == 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int rh2 = h;
    if (rh2 < 1) rh2 = 1;
    int denom = rh2 > 1 ? rh2 - 1 : 1;
    ZAN_STAT(g_st_grad, (long long)rw * (long long)rh);
    int tr = (ct >> 16) & 0xFF, tg = (ct >> 8) & 0xFF, tb = ct & 0xFF;
    int mr = (cb >> 16) & 0xFF, mg = (cb >> 8) & 0xFF, mb = cb & 0xFF;
    for (int py = y0; py < y1; py++) {
        int num = py - y;
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
            int cov = zan_round_cov(px - x, py - y, w, h, r, m);
            if (cov >= 255) row[px] = c;
            else if (cov > 0) set_pixel_aa(s, px, py, c, cov);
        }
    }
}

EXPORT void zan_gui_fill_vgrad(
    i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 color_top,
    i32 color_bottom) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, fill_vgrad)->fill_vgrad(s, (int)x, (int)y, (int)w, (int)h,
                                       0, ZAN_CORNERS_ALL,
                                       (u32)color_top, (u32)color_bottom);
}

EXPORT void zan_gui_fill_vgrad_mask(
    i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 mask,
    i32 color_top, i32 color_bottom) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, fill_vgrad)->fill_vgrad(s, (int)x, (int)y, (int)w, (int)h,
                                       (int)radius, (int)mask,
                                       (u32)color_top, (u32)color_bottom);
}

/* Two- or three-stop linear gradient sample at position t (0..1000), lerping
 * alpha as well so a translucent gradient keeps its ends' alpha. */
static u32 grad_sample(u32 from, u32 via, u32 to, int t) {
    u32 a = from, b = to;
    if (via != 0) {
        if (t < 500) { b = via; t = t * 2; }
        else { a = via; t = (t - 500) * 2; }
    }
    int out = 0;
    for (int sh = 0; sh <= 24; sh += 8) {
        int ca = (int)((a >> sh) & 0xFF);
        int cb = (int)((b >> sh) & 0xFF);
        out |= (ca + (cb - ca) * t / 1000) << sh;
    }
    return (u32)out;
}

/* Linear gradient fill cut to a rounded-rect mask, in the direction `dir`
 * (0 = top-to-bottom, 1 = left-to-right, 2 = to bottom-right, 3 = to
 * bottom-left) with an optional middle stop. A diagonal samples the stop at
 * the pixel's distance along the diagonal axis, so both ends sit on the
 * corners the CSS keyword names.
 * Blended per pixel (so translucent stops composite over the backdrop) and
 * anti-aliased on the corner arcs: a gradient painted as bands of FillRect
 * inset row by row leaves the corners as a staircase that a 1px arc outline
 * drawn over it cannot hide. */
static void cpu_fill_grad(zan_surface_t *s, int x, int y, int w, int h,
                          int radius, int corners, int dir,
                          u32 cf, u32 cv, u32 ct) {
    int x0 = clamp_i(x, s->clip_x0, s->clip_x1);
    int y0 = clamp_i(y, s->clip_y0, s->clip_y1);
    int x1 = clamp_i(x + w, s->clip_x0, s->clip_x1);
    int y1 = clamp_i(y + h, s->clip_y0, s->clip_y1);
    if (x1 <= x0 || y1 <= y0) return;
    int r = radius;
    int m = corners;
    if (r < 0) r = 0;
    if (r > 0 && (m & 15) == 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int d = dir;
    int span = (d == 1) ? w : h;
    if (d == 2 || d == 3) span = w + h - 1;
    if (span < 1) span = 1;
    int denom = span > 1 ? span - 1 : 1;
    ZAN_STAT(g_st_grad, (long long)(x1 - x0) * (long long)(y1 - y0));
    for (int py = y0; py < y1; py++) {
        u32 *row = s->pixels + py * s->stride;
        u32 c = 0;
        if (d == 0) {
            c = grad_sample(cf, cv, ct,
                clamp_i(py - y, 0, denom) * 1000 / denom);
            /* Straight part of a vertical gradient: one colour for the whole
             * row, so blend it as a run instead of pixel by pixel. */
            int j = py - y;
            if (r <= 0 || (j >= r && j < h - r)) {
                blend_run_const(row + x0, x1 - x0, c);
                continue;
            }
        }
        for (int px = x0; px < x1; px++) {
            if (d != 0) {
                int i = px - x;
                int pos = i;
                if (d == 2) pos = i + (py - y);
                else if (d == 3) pos = (w - 1 - i) + (py - y);
                c = grad_sample(cf, cv, ct,
                    clamp_i(pos, 0, denom) * 1000 / denom);
            }
            int cov = r <= 0 ? 255
                : zan_round_cov(px - x, py - y, w, h, r, m);
            if (cov <= 0) continue;
            if (cov >= 255) row[px] = blend_over(row[px], c);
            else set_pixel_aa(s, px, py, c, cov);
        }
    }
}

EXPORT void zan_gui_fill_grad_mask(
    i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 mask, i32 dir,
    i32 color_from, i32 color_via, i32 color_to) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, fill_grad)->fill_grad(s, (int)x, (int)y, (int)w, (int)h,
                                     (int)radius, (int)mask, (int)dir,
                                     (u32)color_from, (u32)color_via,
                                     (u32)color_to);
}

/* Anti-aliased rounded-rect mask: 0..255 coverage of pixel (i,j) for a
 * rounded rect of size rw*rh and radius cr (corner bits per ZAN_CORNER_*).
 * 255 = fully inside, 0 = outside, in between = the 1px corner arc edge.
 * Shares zan_gui_fill_rounded_rect_mask's corner geometry, so every masked
 * operation (gradient fill, frosted blur) cuts the silhouette the plain
 * rounded fill draws and a glass panel's layers land on one arc. */
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
/* Write one blurred pixel through the rounded mask: full coverage overwrites,
 * the ~1px arc edge blends the blur over the sharp backdrop underneath, so the
 * silhouette is anti-aliased instead of a staircase of whole pixels. */
static void blur_put(zan_surface_t *s, int px, int py, u32 c, int cov) {
    if (cov <= 0 || clipped_out(s, px, py)) return;
    if (cov >= 255) { s->pixels[py * s->stride + px] = c; return; }
    set_pixel_aa(s, px, py, c, cov);
}

/* `capture`, when non-NULL, receives the rw*rh region as it ends up on the
 * surface (blur inside the mask, the anti-aliased blend on the arc, untouched
 * backdrop outside). The blur cache stores that, so a cached frame restores it
 * with a plain copy: re-blending the arc over an already-composited edge would
 * darken it a little more every frame. */
static void zan_blur_rect_core(zan_surface_t *s, int x, int y, int w, int h,
                               int radius, int cr, int cmask, u32 *capture) {
    int x0 = clamp_i(x, 0, s->width);
    int y0 = clamp_i(y, 0, s->height);
    int x1 = clamp_i(x + w, 0, s->width);
    int y1 = clamp_i(y + h, 0, s->height);
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return;
    if (!clip_hits(s, x0, y0, x1, y1)) return;
    int r = radius;
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
    /* Downsample/upscale scratch, reused across calls: every glass panel on
     * an animated page re-blurs per frame, and two malloc/free of the small
     * copy each time was measurable allocator churn (frame-budget probes
     * flagged it). Grow-only, like the polyline coverage scratch. */
    static u32 *scratch_a = NULL, *scratch_b = NULL;
    static size_t scratch_n = 0;
    if (dn > scratch_n) {
        u32 *na = (u32 *)realloc(scratch_a, dn * sizeof(u32));
        if (!na) return;                    /* keep old block; skip this blur */
        scratch_a = na;
        u32 *nb = (u32 *)realloc(scratch_b, dn * sizeof(u32));
        if (!nb) return;
        scratch_b = nb;
        scratch_n = dn;
    }
    u32 *a = scratch_a;
    u32 *b = scratch_b;

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
            u32 *src = a + j * dw;
            for (int i = 0; i < rw; i++) {
                u32 px = src[i];
                int nz = ((((x0 + i) * 197 + (y0 + j) * 173) >> 3) % 7) - 3;
                int rC = clamp_i((int)((px >> 16) & 0xFF) + nz, 0, 255);
                int gC = clamp_i((int)((px >> 8) & 0xFF) + nz, 0, 255);
                int bC = clamp_i((int)(px & 0xFF) + nz, 0, 255);
                u32 out = 0xFF000000u | ((u32)rC << 16) | ((u32)gC << 8) | (u32)bC;
                blur_put(s, x0 + i, y0 + j, out,
                         cr <= 0 ? 255 : zan_round_cov(i, j, rw, rh, cr, cmask));
                if (capture)
                    capture[(size_t)j * rw + i] =
                        s->pixels[(y0 + j) * s->stride + x0 + i];
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
                u32 out = 0xFF000000u | ((u32)rC << 16) | ((u32)gC << 8) | (u32)bC;
                blur_put(s, x0 + i, y0 + j, out,
                         cr <= 0 ? 255 : zan_round_cov(i, j, rw, rh, cr, cmask));
                if (capture)
                    capture[(size_t)j * rw + i] =
                        s->pixels[(y0 + j) * s->stride + x0 + i];
            }
        }
    }
}

EXPORT void zan_gui_blur_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, blur)->blur(s, (int)x, (int)y, (int)w, (int)h, (int)radius,
                            -1, 1, 0, ZAN_CORNERS_ALL);
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
    int cr, cmask; /* corner mask baked into the snapshot's anti-aliased edge */
    u32 src_sum; /* sampled checksum of the pre-blur backdrop, see zan_src_sum */
    u32 *pixels;
    size_t cap;
} zan_blur_cache_t;

/* Sampled checksum of a region's current pixels: every 8th pixel of every 8th
 * row, which is 1/64th of the work of the blur it guards. A "the content behind
 * the glass may have changed" hint from the caller (any click, scroll or key)
 * is thus verified against the pixels themselves, so the frosted panels only
 * re-blur when their backdrop really did change. */
static u32 zan_src_sum(zan_surface_t *s, int x0, int y0, int rw, int rh) {
    u32 sum = 2166136261u;
    for (int j = 0; j < rh; j += 8) {
        u32 *row = s->pixels + (y0 + j) * s->stride + x0;
        for (int i = 0; i < rw; i += 8) {
            sum = (sum ^ row[i]) * 16777619u;
        }
    }
    return sum ^ (u32)(rw * 31 + rh);
}
static zan_blur_cache_t g_blur_cache[ZAN_BLUR_CACHE_SLOTS];

/* Damage-clipped frames whose glass had to be re-blurred from a source that is
 * only partly fresh: outside the damage strip the surface still holds the last
 * frame's *composited* pixels (panel tint, text), so the kernel mixes them into
 * the strip and the result drifts from what a whole frame would produce. The
 * caller polls this to promote the next frame to a whole one. */
static long long g_blur_partial_miss;

/* The `blur` entry: an out-of-range slot (including the -1 the uncached export
 * passes) means "no cache", so the software path is one entry, as the vtable
 * declares it. */
static void cpu_blur(zan_surface_t *s, int x, int y, int w, int h, int radius,
                     int slot, int dirty, int cr, int cmask) {
    if (slot < 0 || slot >= ZAN_BLUR_CACHE_SLOTS) {
        zan_blur_rect_core(s, x, y, w, h, radius, cr, cmask, NULL);
        return;
    }
    int x0 = clamp_i(x, 0, s->width);
    int y0 = clamp_i(y, 0, s->height);
    int x1 = clamp_i(x + w, 0, s->width);
    int y1 = clamp_i(y + h, 0, s->height);
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return;
    /* Damage-clipped frame, panel outside the strip: nothing of this region is
     * writable, so neither the cached restore nor a re-blur would change a
     * pixel. Leave the slot untouched (it stays valid for the next full frame)
     * and skip even the checksum. */
    if (!clip_hits(s, x0, y0, x1, y1)) return;
    int r = radius;
    if (r < 1) return;
    if (r > 40) r = 40;

    zan_blur_cache_t *c = &g_blur_cache[slot];
    size_t n = (size_t)rw * (size_t)rh;

    /* Reuse path: same geometry, not dirtied -> restore cached blurred pixels
     * over the (freshly redrawn, identical) backdrop without re-blurring. */
    int geom_ok = c->valid && c->pixels && c->sid == s->id
        && c->x0 == x0 && c->y0 == y0 && c->rw == rw && c->rh == rh
        && c->r == r && c->cr == cr && c->cmask == cmask;
    int covers = clip_covers(s, x0, y0, x1, y1);
    u32 sum = geom_ok ? zan_src_sum(s, x0, y0, rw, rh) : 0;
    /* Region straddles the damage strip: outside it the surface still holds
     * last frame's *composited* pixels, so the checksum cannot tell whether the
     * backdrop behind the glass changed. With the caller hinting that it may
     * have ("dirty"), the cache is no longer trustworthy: re-blur, count the
     * region as a partial miss and drop the slot, so the caller promotes the
     * next frame to a whole one instead of leaving a stale panel on screen.
     * Without the hint the cached pixels are still the faithful answer, and a
     * plain hover strip crossing a glass panel stays cheap. */
    if (geom_ok && (!dirty || (covers && sum == c->src_sum))) {
        ZAN_STAT(g_st_blur_hit, (long long)rw * (long long)rh);
        if (covers) {
            /* Whole region inside the clip: plain row copies. Per-pixel
             * blur_put (a clip test plus blend bookkeeping each) dominated
             * the hit path on animation-only frames; with the clip covering
             * everything at coverage 255 the copy is semantically identical. */
            for (int j = 0; j < rh; j++)
                memcpy(s->pixels + (size_t)(y0 + j) * s->stride + x0,
                       c->pixels + (size_t)j * rw, (size_t)rw * sizeof(u32));
        } else {
            /* Region straddles the damage strip: keep the per-pixel clip. */
            for (int j = 0; j < rh; j++) {
                u32 *src = c->pixels + (size_t)j * rw;
                for (int i = 0; i < rw; i++)
                    blur_put(s, x0 + i, y0 + j, src[i], 255);
            }
        }
        return;
    }

    /* Recompute in place (identical clamping), capturing the composited region
     * straight into the slot. */
    u32 fresh = geom_ok ? sum : zan_src_sum(s, x0, y0, rw, rh);
    if (c->cap < n) {
        u32 *np = (u32 *)realloc(c->pixels, n * sizeof(u32));
        if (np) { c->pixels = np; c->cap = n; }
    }
    zan_blur_rect_core(s, x, y, w, h, radius, cr, cmask,
                       c->cap >= n ? c->pixels : NULL);
    /* Only part of the region was writable (the panel straddles the damage
     * strip), so the surface now holds blurred pixels inside the strip and the
     * untouched backdrop outside it. Snapshotting that mixture would hand a
     * later frame a half-sharp "blur"; drop the slot and let the next full
     * frame refill it. */
    if (!covers) { g_blur_partial_miss++; }
    if (!covers || c->cap < n) { c->valid = 0; return; }
    c->x0 = x0; c->y0 = y0; c->rw = rw; c->rh = rh; c->r = r;
    c->cr = cr; c->cmask = cmask;
    c->src_sum = fresh;
    c->sid = s->id;
    c->valid = 1;
}

/* Number of glass regions blurred from a partly stale backdrop since the last
 * call (and resets the counter). */
EXPORT i32 zan_gui_blur_partial_miss_take(void) {
    i32 v = (i32)g_blur_partial_miss;
    g_blur_partial_miss = 0;
    return v;
}

EXPORT void zan_gui_blur_rect_cached(
    i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 slot,
    i32 dirty) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, blur)->blur(s, (int)x, (int)y, (int)w, (int)h, (int)radius,
                            (int)slot, (int)dirty, 0, ZAN_CORNERS_ALL);
}

/* Rounded frosted-glass blur: like zan_gui_blur_rect_cached but the blurred
 * output is clipped to a rounded-rect (corner radius cr, corner bits cmask) so
 * a panel's corners keep the sharp backdrop and line up with the rounded tint
 * drawn over them. */
EXPORT void zan_gui_blur_round_cached(
    i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 slot,
    i32 dirty, i32 corner_radius, i32 corner_mask) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, blur)->blur(s, (int)x, (int)y, (int)w, (int)h, (int)radius,
                            (int)slot, (int)dirty, (int)corner_radius,
                            (int)corner_mask);
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

static void zan_cache_drop_surface(int sid) {
    for (int i = 0; i < ZAN_BLUR_CACHE_SLOTS; i++) {
        if (g_blur_cache[i].valid && g_blur_cache[i].sid == sid) {
            g_blur_cache[i].valid = 0;
        }
    }
    for (int i = 0; i < g_snap_cache_count; i++) {
        if (g_snap_cache[i].valid && g_snap_cache[i].sid == sid) {
            g_snap_cache[i].valid = 0;
        }
    }
}

static void cpu_snapshot(zan_surface_t *s, int x, int y, int w, int h,
                         int slot) {
    zan_blur_cache_t *c = zan_snap_slot(slot);
    if (!c) return;
    int x0 = clamp_i(x, 0, s->width);
    int y0 = clamp_i(y, 0, s->height);
    int x1 = clamp_i(x + w, 0, s->width);
    int y1 = clamp_i(y + h, 0, s->height);
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
    c->sid = s->id;
    c->valid = 1;
}

EXPORT void zan_gui_snapshot_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 slot) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, snapshot)->snapshot(s, (int)x, (int)y, (int)w, (int)h,
                                    (int)slot);
}

EXPORT void zan_gui_snapshot_patch_rect(i32 surface_id, i32 x, i32 y, i32 w,
                                        i32 h, i32 slot) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, snapshot_patch)->snapshot_patch(s, (int)x, (int)y, (int)w,
                                                (int)h, (int)slot);
}

/* Refreshes [x,y,w,h] inside an existing snapshot without changing the
 * slot's geometry: a damage-strip frame re-renders only its strip, and the
 * caller folds the strip's fresh pixels back into the whole-window snapshot
 * so whole-window restores keep matching it. Refuses (no-op) when the slot
 * does not hold a valid snapshot of this surface containing the rect --
 * the caller then treats the snapshot as unusable, exactly like a failed
 * restore. */
static void cpu_snapshot_patch(zan_surface_t *s, int x, int y, int w, int h,
                               int slot) {
    zan_blur_cache_t *c = zan_snap_slot(slot);
    if (!c) return;
    if (!c->valid || !c->pixels || c->sid != s->id) return;
    int x0 = clamp_i(x, 0, s->width);
    int y0 = clamp_i(y, 0, s->height);
    int x1 = clamp_i(x + w, 0, s->width);
    int y1 = clamp_i(y + h, 0, s->height);
    if (x1 <= x0 || y1 <= y0) return;
    if (x0 < c->x0 || y0 < c->y0
        || x1 > c->x0 + c->rw || y1 > c->y0 + c->rh) {
        return;
    }
    for (int j = y0; j < y1; j++) {
        u32 *row = s->pixels + j * s->stride + x0;
        u32 *dst = c->pixels + (size_t)(j - c->y0) * c->rw + (x0 - c->x0);
        for (int i = 0; i < x1 - x0; i++) dst[i] = row[i];
    }
}

static int cpu_restore_rect(zan_surface_t *s, int x, int y, int w, int h,
                            int slot) {
    zan_blur_cache_t *c = zan_snap_slot(slot);
    if (!c) return 0;
    int x0 = clamp_i(x, 0, s->width);
    int y0 = clamp_i(y, 0, s->height);
    int x1 = clamp_i(x + w, 0, s->width);
    int y1 = clamp_i(y + h, 0, s->height);
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return 0;
    if (!c->valid || !c->pixels || c->sid != s->id
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
 * the slot holds a valid snapshot covering the rect, 0 otherwise -- callers
 * treat 0 as "repair impossible, repaint the whole frame".
 *
 * The rect must lie *entirely* inside the snapshot. Slots are one process-wide
 * table shared by everyone (the effects layer, wallpapers, host chart caches),
 * so a rect that only partly overlaps the snapshot means the slot is not the
 * one this caller took: silently repairing the overlap paints a foreign, stale
 * region over freshly composited pixels, and it survives until something forces
 * a whole frame. Refusing hands the caller back the only safe answer. */
static int cpu_restore_sub_rect(zan_surface_t *s, int x, int y, int w, int h,
                                int slot) {
    zan_blur_cache_t *c = zan_snap_slot(slot);
    if (!c) return 0;
    if (!c->valid || !c->pixels || c->sid != s->id) return 0;
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x1 <= x0 || y1 <= y0) return 1;
    if (x0 < c->x0 || y0 < c->y0
        || x1 > c->x0 + c->rw || y1 > c->y0 + c->rh) {
        return 0;
    }
    /* Like every other drawing op (and like zan_gui_restore_rect), a restore
     * writes only inside the clip window: outside it the surface still holds
     * the frame that is on screen, and nothing outside the clip gets uploaded,
     * so writing there only desynchronizes surface and window. */
    if (x0 < s->clip_x0) x0 = s->clip_x0;
    if (y0 < s->clip_y0) y0 = s->clip_y0;
    if (x1 > s->clip_x1) x1 = s->clip_x1;
    if (y1 > s->clip_y1) y1 = s->clip_y1;
    x0 = clamp_i(x0, 0, s->width);
    y0 = clamp_i(y0, 0, s->height);
    x1 = clamp_i(x1, 0, s->width);
    y1 = clamp_i(y1, 0, s->height);
    if (x1 <= x0 || y1 <= y0) return 1;
    ZAN_STAT(g_st_restore, (long long)(x1 - x0) * (long long)(y1 - y0));
    for (int j = y0; j < y1; j++) {
        u32 *row = s->pixels + j * s->stride + x0;
        u32 *src = c->pixels + (size_t)(j - c->y0) * c->rw + (x0 - c->x0);
        for (int i = 0; i < x1 - x0; i++) row[i] = src[i];
    }
    return 1;
}

static int cpu_restore(zan_surface_t *s, int x, int y, int w, int h, int slot,
                      int sub_rect) {
    return sub_rect ? cpu_restore_sub_rect(s, x, y, w, h, slot)
                    : cpu_restore_rect(s, x, y, w, h, slot);
}

EXPORT i32 zan_gui_restore_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 slot) {
    if (surface_id < 0 || surface_id >= g_surface_count) return 0;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return 0;
    return ZAN_IMPL(s, restore)->restore(s, (int)x, (int)y, (int)w, (int)h,
                                         (int)slot, 0);
}

EXPORT i32 zan_gui_restore_sub_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 slot) {
    if (surface_id < 0 || surface_id >= g_surface_count) return 0;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return 0;
    return ZAN_IMPL(s, restore)->restore(s, (int)x, (int)y, (int)w, (int)h,
                                         (int)slot, 1);
}

/* Debug helper: writes the surface's pixels as a 24bpp bottom-up BMP so a
 * headless agent can compare the canvas contents against what the window
 * actually shows. Returns 1 on success. */
EXPORT i32 zan_gui_surface_dump(i32 surface_id, const char *path) {
    if (surface_id < 0 || surface_id >= g_surface_count) return 0;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !s->pixels || !path) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int w = s->width, h = s->height;
    int stride_out = (w * 3 + 3) & ~3;
    u32 data_size = (u32)stride_out * (u32)h;
    u32 file_size = 54 + data_size;
    unsigned char hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(hdr + 2, &file_size, 4);
    u32 off = 54; memcpy(hdr + 10, &off, 4);
    u32 ihs = 40; memcpy(hdr + 14, &ihs, 4);
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    unsigned short planes = 1; memcpy(hdr + 26, &planes, 2);
    unsigned short bpp = 24; memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &data_size, 4);
    fwrite(hdr, 1, 54, f);
    unsigned char *row = (unsigned char *)malloc((size_t)stride_out);
    if (!row) { fclose(f); return 0; }
    memset(row, 0, (size_t)stride_out);
    for (int j = h - 1; j >= 0; j--) {
        u32 *src = s->pixels + (size_t)j * (size_t)s->stride;
        for (int i = 0; i < w; i++) {
            u32 p = src[i];
            /* BMP 24bpp is B,G,R byte order (the surface is 0xAARRGGBB);
             * writing it the other way swaps red and blue on readback. */
            row[i * 3 + 0] = (unsigned char)(p & 0xFF);
            row[i * 3 + 1] = (unsigned char)((p >> 8) & 0xFF);
            row[i * 3 + 2] = (unsigned char)((p >> 16) & 0xFF);
        }
        fwrite(row, 1, (size_t)stride_out, f);
    }
    free(row);
    fclose(f);
    return 1;
}

/* ========================================================================
 * Incremental GDI present (tear-free full-frame blits)
 * ======================================================================== */
/* SetDIBitsToDevice streams rows into the window's redirection surface, so a
 * full-window upload leaves the screen mid-tear for milliseconds -- visible as
 * doubled/ghosted text while a list scrolls (the frame is correct on the
 * canvas; the *screen* shows two frames blended at the tear line). Uploading
 * only the tiles that actually changed shrinks that window to near zero for
 * partial updates, and -- unlike damage-rect reporting -- the diff against the
 * previously presented bits is ground truth: a missed damage declaration can
 * never ghost the screen, it only costs one extra uploaded tile. */
#ifdef _WIN32

typedef struct zan_present_shadow_s {
    void *hwnd;
    int w;
    int h;
    u32 *pixels;
    struct zan_present_shadow_s *next;
} zan_present_shadow_t;

static zan_present_shadow_t *g_present_shadows = NULL;

#define ZAN_DIFF_TILE 64

/* Shadow for this window at this size, or NULL. `alloc` creates/resizes it
 * (zeroed: a fresh shadow diffs as fully changed, so the first present is a
 * correct full upload). The list is capped; the oldest entry is evicted. */
static zan_present_shadow_t *present_shadow(void *hwnd, int w, int h, int alloc) {
    zan_present_shadow_t *prev = NULL;
    zan_present_shadow_t *it = g_present_shadows;
    int n = 0;
    while (it) {
        if (it->hwnd == hwnd) {
            if (it->w != w || it->h != h) {
                if (!alloc) return NULL;
                u32 *np = (u32 *)realloc(it->pixels,
                                         (size_t)w * (size_t)h * 4);
                if (!np) return NULL;
                memset(np, 0, (size_t)w * (size_t)h * 4);
                it->pixels = np;
                it->w = w;
                it->h = h;
            }
            if (prev) {  /* move to front (LRU) */
                prev->next = it->next;
                it->next = g_present_shadows;
                g_present_shadows = it;
            }
            return it;
        }
        prev = it;
        it = it->next;
        n++;
    }
    if (!alloc) return NULL;
    zan_present_shadow_t *ns = (zan_present_shadow_t *)calloc(1, sizeof(*ns));
    if (!ns) return NULL;
    ns->pixels = (u32 *)calloc((size_t)w * (size_t)h, 4);
    if (!ns->pixels) { free(ns); return NULL; }
    ns->hwnd = hwnd;
    ns->w = w;
    ns->h = h;
    ns->next = g_present_shadows;
    g_present_shadows = ns;
    if (n >= 8) {  /* drop the now-tail entry */
        zan_present_shadow_t *tail = g_present_shadows;
        while (tail->next && tail->next->next) tail = tail->next;
        free(tail->next->pixels);
        free(tail->next);
        tail->next = NULL;
    }
    return ns;
}

static void present_shadow_fill_rect(zan_present_shadow_t *sh, const u32 *pix,
                                     int stride, int rx, int ry, int rw, int rh) {
    for (int j = 0; j < rh; j++) {
        memcpy(sh->pixels + (size_t)(ry + j) * (size_t)sh->w + rx,
               pix + (size_t)(ry + j) * (size_t)stride + rx,
               (size_t)rw * 4);
    }
}

/* One SetDIBitsToDevice streams its rows into the window's redirection
 * surface without any atomic-swap guarantee: DWM can composite mid-stream and
 * put a torn frame on screen (the classic 花屏). Splitting every upload into
 * short horizontal chunks bounds each exposure window to a sub-millisecond
 * and any visible tear to one chunk height; between chunks the surface is in
 * a consistent old/new mix, which for animation reads as motion, not garbage.
 * Returns the number of calls made. */
#define ZAN_CHUNK_H 128

static int gdi_upload_chunks(HDC dc, u32 *pix, BITMAPINFO *bmi,
                             int rx, int ry, int rw, int rh) {
    int calls = 0;
    int y = ry;
    int end = ry + rh;
    int w = bmi->bmiHeader.biWidth;
    /* Upload each chunk through a top-down memory DIB with BitBlt instead of
     * feeding SetDIBitsToDevice directly. SetDIBitsToDevice streams scans
     * through the DC's clip region, and on a WS_CLIPCHILDREN window hosting
     * native child HWNDs (WebView2/CEF) that region has a huge hole where the
     * children sit; the scan accounting against the holed DC corrupted the
     * upload -- content landed rolled by one chunk height, the chrome rows
     * swapped top/bottom (the "上下颠倒/错位" reports), while childless
     * windows with an unclipped DC stayed pixel-exact through this very same
     * code. BitBlt positions every chunk absolutely: the clip region can only
     * mask rows, never move them, so hosting children no longer matters.
     * Rows are copied un-reversed: the stage DIB is top-down (negative
     * biHeight) and shares the surface origin, chunk for chunk. */
    HDC sdc = GetDC(0);
    HDC mdc = CreateCompatibleDC(sdc);
    ReleaseDC(0, sdc);
    if (!mdc) return calls;
    BITMAPINFOHEADER cbh = bmi->bmiHeader;
    cbh.biWidth = rw;
    cbh.biHeight = -(ZAN_CHUNK_H > rh ? ZAN_CHUNK_H : rh);
    void *stage = NULL;
    HBITMAP bm = CreateDIBSection(mdc, (BITMAPINFO *)&cbh, DIB_RGB_COLORS,
                                  &stage, NULL, 0);
    if (!bm) {
        DeleteDC(mdc);
        return calls;
    }
    HGDIOBJ old = SelectObject(mdc, bm);
    while (y < end) {
        int ch = end - y;
        if (ch > ZAN_CHUNK_H) ch = ZAN_CHUNK_H;
        for (int j = 0; j < ch; j++) {
            memcpy((u32 *)stage + (size_t)j * rw,
                   pix + (size_t)(y + j) * (size_t)w + rx,
                   (size_t)rw * 4);
        }
        BitBlt(dc, rx, y, rw, ch, mdc, 0, 0, SRCCOPY);
        calls++;
        y += ch;
    }
    SelectObject(mdc, old);
    DeleteObject(bm);
    DeleteDC(mdc);
    return calls;
}

#endif /* _WIN32 */

/* GDI blit with a per-window shadow of the last presented frame.
 * rect_count > 0: upload exactly those [x,y,w,h] rects (declared damage).
 * rect_count < 0: forced full upload (WM_PAINT: the OS invalidated the
 *   redirection surface, the shadow can no longer be trusted).
 * rect_count == 0: diff against the shadow and upload only changed tiles.
 * Every upload -- diffed, declared or forced -- is split into horizontal
 * chunks at most ZAN_CHUNK_H tall so no single call streams a whole window
 * past DWM. Presents whose changed area exceeds ZAN_ATOMIC_AT percent of the
 * window go out through zan_gui_atomic_present instead -- but only when the
 * window is already layered (glass/shape); a plain window streams, because
 * going layered would cost it its DWM drop shadow.
 * Returns the number of SetDIBitsToDevice calls made, or -1 on failure (the
 * caller falls back to its own full blit). Non-Windows builds return -1 and
 * the shells keep their own present path. */
EXPORT i32 zan_gui_atomic_present(void *hwnd, i32 surface_id);

/* Changed area above this fraction of the window swaps atomically instead of
 * streaming: the copy cost is the same order as the upload, and the screen
 * never shows a mixed frame. */
#define ZAN_ATOMIC_AT 35
/* Return value of zan_gui_gdi_present when the frame went out through the
 * atomic swap instead of rect uploads; the shell only tests ret > 0, so any
 * distinctive positive works -- the value just makes present logs decodable. */
#define ZAN_ATOMIC_RET 1000000
EXPORT i32 zan_gui_gdi_present(void *hwnd, i32 surface_id, i32 *rects,
                               i32 rect_count) {
#ifdef _WIN32
    if (!hwnd || surface_id < 0 || surface_id >= g_surface_count) return -1;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !s->pixels) return -1;
    if (s->be) {
        if (s->be->flush) s->be->flush(s);
        if (s->be->read_pixels) s->be->read_pixels(s);
    }
    int w = s->width, h = s->height;
    u32 *pix = s->pixels;
    int stride = s->stride;

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  /* top-down: rows share the surface origin */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC dc = GetDC((HWND)hwnd);
    if (!dc) return -1;
    int calls = 0;

    zan_present_shadow_t *sh = present_shadow(hwnd, w, h, 1);
    int force_full = (rect_count < 0) || !sh;
    {
        /* The tile diff compares the surface against a shadow of what was
         * *uploaded*, not what the screen *shows*. On real Win10 boxes that
         * agreement silently breaks (content lost without a WM_PAINT to
         * invalidate the ledger -- repro: gui_gallery at 1702x1136, chrome
         * rows land at the bottom, permanent mixed-frame scramble) and the
         * diff then patches the wrong tiles forever. Full-window upload is
         * byte-exact no matter what the screen lost, so it is the default;
         * ZAN_TILE_DIFF=1 opts back into the diff for debugging it. */
        static int tilediff = -1;
        if (tilediff < 0) {
            const char *ev = getenv("ZAN_TILE_DIFF");
            tilediff = ev && *ev;
        }
        if (!tilediff && rect_count == 0) force_full = 1;
    }

    if (!force_full && rect_count == 0) {
        /* Tile diff against the last presented frame. */
        int tw = (w + ZAN_DIFF_TILE - 1) / ZAN_DIFF_TILE;
        int th = (h + ZAN_DIFF_TILE - 1) / ZAN_DIFF_TILE;
        unsigned char *changed = (unsigned char *)calloc((size_t)tw * th, 1);
        unsigned char *rowany = (unsigned char *)calloc((size_t)th, 1);
        int *out = (int *)malloc(sizeof(int) * 4 * (size_t)(tw * th + th));
        if (!changed || !out || !rowany) {
            free(changed);
            free(out);
            free(rowany);
            ReleaseDC((HWND)hwnd, dc);
            return -1;
        }
        int changed_tiles = 0;
        for (int ty = 0; ty < th; ty++) {
            int y0 = ty * ZAN_DIFF_TILE;
            int rows = (y0 + ZAN_DIFF_TILE <= h) ? ZAN_DIFF_TILE : (h - y0);
            for (int tx = 0; tx < tw; tx++) {
                int x0 = tx * ZAN_DIFF_TILE;
                int cols = (x0 + ZAN_DIFF_TILE <= w) ? ZAN_DIFF_TILE : (w - x0);
                int dirty = 0;
                for (int j = 0; j < rows && !dirty; j++) {
                    u32 *a = pix + (size_t)(y0 + j) * (size_t)stride + x0;
                    u32 *b = sh->pixels + (size_t)(y0 + j) * (size_t)w + x0;
                    if (memcmp(a, b, (size_t)cols * 4) != 0) dirty = 1;
                }
                if (dirty) {
                    changed[(size_t)ty * tw + tx] = 1;
                    changed_tiles++;
                }
            }
        }
        if (changed_tiles > 0) {
            /* A page switch, a scroll past the diff's sweet spot: streaming
             * this much would keep the surface in a mixed state for most of
             * the frame period, so swap atomically instead. */
            long long area = (long long)changed_tiles * ZAN_DIFF_TILE
                             * ZAN_DIFF_TILE;
            if (area * 100 > (long long)w * h * ZAN_ATOMIC_AT
                    && zan_gui_atomic_present(hwnd, surface_id)) {
                memcpy(sh->pixels, pix, (size_t)w * (size_t)h * 4);
                free(changed);
                free(out);
                free(rowany);
                ReleaseDC((HWND)hwnd, dc);
                return ZAN_ATOMIC_RET;
            }
            for (int ty = 0; ty < th; ty++) {
                for (int tx = 0; tx < tw; tx++) {
                    if (changed[(size_t)ty * tw + tx]) { rowany[ty] = 1; break; }
                }
            }
            /* Merge changed tiles into maximal rects: horizontal runs, then
             * extend each run downward while the same span stays changed. A
             * scroll touches one contiguous block, which collapses to a rect
             * or two -- the screen then updates as a single sweep of just the
             * changed area. Scattered small changes (particles, carets) stay
             * as small rects. */
            int n = 0;
            for (int ty = 0; ty < th; ty++) {
                int tx = 0;
                while (tx < tw) {
                    if (!changed[(size_t)ty * tw + tx]) { tx++; continue; }
                    int tx0 = tx;
                    while (tx < tw && changed[(size_t)ty * tw + tx]) tx++;
                    int ty1 = ty + 1;
                    while (ty1 < th) {
                        int ok = 1;
                        for (int k = tx0; k < tx; k++) {
                            if (!changed[(size_t)ty1 * tw + k]) { ok = 0; break; }
                        }
                        if (!ok) break;
                        ty1++;
                    }
                    out[n * 4 + 0] = tx0 * ZAN_DIFF_TILE;
                    out[n * 4 + 1] = ty * ZAN_DIFF_TILE;
                    out[n * 4 + 2] = (tx - tx0) * ZAN_DIFF_TILE;
                    out[n * 4 + 3] = (ty1 - ty) * ZAN_DIFF_TILE;
                    n++;
                    for (int yy = ty; yy < ty1; yy++) {
                        for (int k = tx0; k < tx; k++) {
                            changed[(size_t)yy * tw + k] = 0;
                        }
                    }
                }
            }
            /* Heavily fragmented frame (particles, noise): dozens of disjoint
             * rects cost more fixed GDI overhead than a few full-width bands,
             * so re-merge into one band per run of changed tile-rows and let
             * the chunk splitter below keep each call short. rowany was
             * snapshotted before collection cleared `changed`. */
            if (n > 48) {
                int ty = 0;
                n = 0;
                while (ty < th) {
                    if (!rowany[ty]) { ty++; continue; }
                    int ty0 = ty;
                    while (ty < th && rowany[ty]) ty++;
                    out[n * 4 + 0] = 0;
                    out[n * 4 + 1] = ty0 * ZAN_DIFF_TILE;
                    out[n * 4 + 2] = w;
                    int bh = (ty - ty0) * ZAN_DIFF_TILE;
                    if (out[n * 4 + 1] + bh > h) bh = h - out[n * 4 + 1];
                    out[n * 4 + 3] = bh;
                    n++;
                }
            }
            for (int i = 0; i < n; i++) {
                int rx = out[i * 4 + 0];
                int ry = out[i * 4 + 1];
                int rw = out[i * 4 + 2];
                int rh = out[i * 4 + 3];
                if (rx < 0) { rw += rx; rx = 0; }
                if (ry < 0) { rh += ry; ry = 0; }
                if (rx + rw > w) rw = w - rx;
                if (ry + rh > h) rh = h - ry;
                if (rw <= 0 || rh <= 0) continue;
                calls += gdi_upload_chunks(dc, pix, &bmi, rx, ry, rw, rh);
                present_shadow_fill_rect(sh, pix, stride, rx, ry, rw, rh);
            }
        }
        free(changed);
        free(out);
        free(rowany);
    } else if (force_full) {
        /* WM_PAINT / fresh shadow: the OS invalidated the redirection
         * surface, so everything changes -- swap atomically when the window
         * allows it, chunks only as fallback. */
        if (zan_gui_atomic_present(hwnd, surface_id)) {
            memcpy(sh->pixels, pix, (size_t)w * (size_t)h * 4);
            ReleaseDC((HWND)hwnd, dc);
            return ZAN_ATOMIC_RET;
        }
        calls = gdi_upload_chunks(dc, pix, &bmi, 0, 0, w, h);
        memcpy(sh->pixels, pix, (size_t)w * (size_t)h * 4);
    } else if (rect_count > 0) {
        long long area = 0;
        for (int i = 0; i < rect_count; i++) {
            int rx = rects[i * 4 + 0];
            int ry = rects[i * 4 + 1];
            int rw = rects[i * 4 + 2];
            int rh = rects[i * 4 + 3];
            if (rx < 0) { rw += rx; rx = 0; }
            if (ry < 0) { rh += ry; ry = 0; }
            if (rx + rw > w) rw = w - rx;
            if (ry + rh > h) rh = h - ry;
            if (rw <= 0 || rh <= 0) continue;
            area += (long long)rw * rh;
        }
        if (area * 100 > (long long)w * h * ZAN_ATOMIC_AT
                && zan_gui_atomic_present(hwnd, surface_id)) {
            memcpy(sh->pixels, pix, (size_t)w * (size_t)h * 4);
            ReleaseDC((HWND)hwnd, dc);
            return ZAN_ATOMIC_RET;
        }
        for (int i = 0; i < rect_count; i++) {
            int rx = rects[i * 4 + 0];
            int ry = rects[i * 4 + 1];
            int rw = rects[i * 4 + 2];
            int rh = rects[i * 4 + 3];
            if (rx < 0) { rw += rx; rx = 0; }
            if (ry < 0) { rh += ry; ry = 0; }
            if (rx + rw > w) rw = w - rx;
            if (ry + rh > h) rh = h - ry;
            if (rw <= 0 || rh <= 0) continue;
            calls += gdi_upload_chunks(dc, pix, &bmi, rx, ry, rw, rh);
            present_shadow_fill_rect(sh, pix, stride, rx, ry, rw, rh);
        }
    }
    ReleaseDC((HWND)hwnd, dc);
    return calls;
#else
    (void)hwnd; (void)surface_id; (void)rects; (void)rect_count;
    return -1;
#endif
}

/* ========================================================================
 * Atomic full-frame present (UpdateLayeredWindow)
 * ======================================================================== */
/* GDI SetDIBitsToDevice streams rows into the window's redirection surface:
 * however the upload is split, DWM can composite a half-updated frame and the
 * user sees bands of old and new content (花屏). The other backends never have
 * this problem because their present is an atomic swap (SDL_Renderer flip on
 * Linux/macOS, the GL backend's SwapBuffers here). Windows has an atomic swap
 * too: a layered window hands DWM a whole new surface via UpdateLayeredWindow,
 * which either shows entirely or not at all. This path costs a full-frame
 * premultiplied copy, so it is reserved for presents whose changed area is
 * large (page switches, scrolls, WM_PAINT restores); small diffs stay on the
 * cheap rect uploads above, where the exposure window is far below one frame.
 * For an opaque window "premultiplied ARGB" is just the canvas pixel with the
 * alpha byte forced to 0xFF -- the same pixels GDI would have shown, since
 * SetDIBitsToDevice ignores alpha anyway. */
#ifdef _WIN32
typedef struct zan_atomic_layer_s {
    void *hwnd;
    int w, h;
    HBITMAP bm;
    HDC dc;
    void *bits;
    struct zan_atomic_layer_s *next;
} zan_atomic_layer_t;

static zan_atomic_layer_t *g_atomic_layers = NULL;

static zan_atomic_layer_t *atomic_layer(void *hwnd, int w, int h) {
    zan_atomic_layer_t *prev = NULL;
    zan_atomic_layer_t *it = g_atomic_layers;
    int n = 0;
    while (it) {
        if (it->hwnd == hwnd) {
            if (it->w != w || it->h != h) {
                /* deleting the DC deselects the DIB, so the bitmap can go */
                DeleteDC(it->dc);
                DeleteObject(it->bm);
                BITMAPINFO bmi;
                memset(&bmi, 0, sizeof(bmi));
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = w;
                bmi.bmiHeader.biHeight = -h;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                HDC sdc = GetDC(0);
                it->dc = CreateCompatibleDC(sdc);
                ReleaseDC(0, sdc);
                it->bm = CreateDIBSection(it->dc, &bmi, DIB_RGB_COLORS,
                                          &it->bits, NULL, 0);
                if (!it->bm) {
                    DeleteDC(it->dc);
                    it->dc = 0;
                    it->bits = NULL;
                    return NULL;
                }
                SelectObject(it->dc, it->bm);
                it->w = w;
                it->h = h;
            }
            if (prev) {
                prev->next = it->next;
                it->next = g_atomic_layers;
                g_atomic_layers = it;
            }
            return it;
        }
        prev = it;
        it = it->next;
        n++;
    }
    zan_atomic_layer_t *nl = (zan_atomic_layer_t *)calloc(1, sizeof(*nl));
    if (!nl) return NULL;
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    HDC sdc = GetDC(0);
    nl->dc = CreateCompatibleDC(sdc);
    ReleaseDC(0, sdc);
    if (!nl->dc) { free(nl); return NULL; }
    nl->bm = CreateDIBSection(nl->dc, &bmi, DIB_RGB_COLORS, &nl->bits, NULL, 0);
    if (!nl->bm) { DeleteDC(nl->dc); nl->dc = 0; free(nl); return NULL; }
    SelectObject(nl->dc, nl->bm);
    nl->hwnd = hwnd;
    nl->w = w;
    nl->h = h;
    nl->next = g_atomic_layers;
    g_atomic_layers = nl;
    if (n >= 8) {
        zan_atomic_layer_t *tail = g_atomic_layers;
        while (tail->next && tail->next->next) tail = tail->next;
        DeleteDC(tail->next->dc);
        DeleteObject(tail->next->bm);
        free(tail->next);
        tail->next = NULL;
    }
    return nl;
}

static void zan_atomic_layer_drop(void *hwnd) {
    zan_atomic_layer_t **p = &g_atomic_layers;
    while (*p) {
        if ((*p)->hwnd == hwnd) {
            zan_atomic_layer_t *dead = *p;
            *p = dead->next;
            DeleteDC(dead->dc);
            DeleteObject(dead->bm);
            free(dead);
            return;
        }
        p = &(*p)->next;
    }
}
#endif /* _WIN32 (atomic layered-present internals are GDI-only) */

/* Swaps the surface onto the screen atomically. Only for windows that are
 * already layered (native glass, outline shape): a plain opaque window would
 * lose its non-client area and with it the DWM drop shadow. Layered content
 * covers the whole window -- a layered window has no non-client area -- and
 * UpdateLayeredWindow's psize *sets* the window size, so the DIB must be sized
 * to the window rect with the client pixels placed at the client offset:
 * sizing it to the client rect would shrink a maximized window by the frame
 * inset on every present (the WM_NCCALCSIZE maximized compensation is the one
 * state where client < window), and the shell's relayout would loop the
 * shrink. On a normal window the offset is zero and this is the client-sized
 * geometry. Rows beyond the surface (a resize frame where the surface lags)
 * and the maximized frame ring go up transparent, exactly like the glass
 * present. Returns 1 when the frame is on screen. */
EXPORT i32 zan_gui_atomic_present(void *hwnd, i32 surface_id) {
#ifdef _WIN32
    if (!hwnd || surface_id < 0 || surface_id >= g_surface_count) return 0;
    /* Only a window that already opted into layering -- native glass or an
     * outline shape, which set WS_EX_LAYERED before their first present --
     * may swap atomically. Flipping a plain opaque window layered here would
     * strip its non-client area (DWM NC rendering off): the drop shadow
     * vanishes, later GDI uploads land in a discarded redirection surface and
     * mouse tracking degrades -- the very breakage that reverted fa7ed72c. */
    if (!(GetWindowLongPtrW((HWND)hwnd, GWL_EXSTYLE) & WS_EX_LAYERED)) return 0;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !s->pixels) return 0;
    if (s->be) {
        if (s->be->flush) s->be->flush(s);
        if (s->be->read_pixels) s->be->read_pixels(s);
    }
    RECT rc;
    if (!GetClientRect((HWND)hwnd, &rc)) return 0;
    int pw = rc.right - rc.left;
    int ph = rc.bottom - rc.top;
    if (pw <= 0 || ph <= 0) return 0;
    RECT wr;
    POINT co;
    co.x = 0;
    co.y = 0;
    int ww = pw;
    int wh = ph;
    int ox = 0;
    int oy = 0;
    if (GetWindowRect((HWND)hwnd, &wr) && ClientToScreen((HWND)hwnd, &co)) {
        int tw = wr.right - wr.left;
        int th = wr.bottom - wr.top;
        int tx = co.x - wr.left;
        int ty = co.y - wr.top;
        if (tw > 0 && th > 0 && tx >= 0 && ty >= 0 && tx + pw <= tw
                && ty + ph <= th) {
            ww = tw;
            wh = th;
            ox = tx;
            oy = ty;
        }
    }
    {
        static int alog = -1;
        if (alog < 0) alog = getenv("ZAN_ATOMIC_LOG") != NULL;
        if (alog) {
            FILE *af = fopen("atomic_trace.log", "a");
            if (af) {
                fprintf(af, "atomic hwnd=%p cli=%dx%d win=%dx%d -> psize=%dx%d off=%d,%d surf=%dx%d\n",
                        hwnd, pw, ph, wr.right - wr.left, wr.bottom - wr.top,
                        ww, wh, ox, oy, s->width, s->height);
                fclose(af);
            }
        }
    }
    zan_atomic_layer_t *l = atomic_layer(hwnd, ww, wh);
    if (!l || !l->bits) return 0;
    int w = s->width, h = s->height;
    int stride = s->stride;
    u32 *pix = s->pixels;
    int copyW = w < ww ? w : ww;
    int copyH = h < wh ? h : wh;
    u32 *dst = (u32 *)l->bits;
    for (int y = 0; y < wh; y++) {
        u32 *drow = dst + (size_t)y * (size_t)ww;
        int sy = y - oy;
        if (sy < 0 || sy >= copyH) {
            memset(drow, 0, (size_t)ww * 4);
        } else {
            const u32 *srow = pix + (size_t)sy * (size_t)stride;
            int x = 0;
            for (; x < ox; x++) { drow[x] = 0; }
            for (; x < ox + copyW; x++) {
                drow[x] = srow[x - ox] | 0xFF000000u;
            }
            for (; x < ww; x++) { drow[x] = 0; }
        }
    }
    POINT src = { 0, 0 };
    SIZE size = { ww, wh };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    HDC sdc = GetDC(0);
    BOOL ok = UpdateLayeredWindow((HWND)hwnd, sdc, NULL, &size, l->dc, &src,
                                  0, &bf, 2);
    ReleaseDC(0, sdc);
    return ok ? 1 : 0;
#else
    (void)hwnd; (void)surface_id;
    return 0;
#endif
}

/* A window is gone: drop its presented-frame shadow so the allocation does not
 * linger for the session. No-op when the handle has none. */
EXPORT void zan_gui_gdi_present_drop(void *hwnd) {
#ifdef _WIN32
    zan_present_shadow_t **p = &g_present_shadows;
    while (*p) {
        if ((*p)->hwnd == hwnd) {
            zan_present_shadow_t *dead = *p;
            *p = dead->next;
            free(dead->pixels);
            free(dead);
            return;
        }
        p = &(*p)->next;
    }
    zan_atomic_layer_drop(hwnd);
#else
    (void)hwnd;
#endif
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

/* Square-cornered stroke: the four edges as plain fills. */
static void cpu_draw_square_rect(zan_surface_t *s, int x, int y, int w, int h,
                                 u32 c, int t) {
    /* top */ cpu_fill_rect(s, x, y, w, t, c);
    /* bottom */ cpu_fill_rect(s, x, y + h - t, w, t, c);
    /* left */ cpu_fill_rect(s, x, y + t, t, h - 2*t, c);
    /* right */ cpu_fill_rect(s, x + w - t, y + t, t, h - 2*t, c);
}

EXPORT void zan_gui_draw_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, draw_round)->draw_round(s, (int)x, (int)y, (int)w, (int)h,
                                        0, ZAN_CORNERS_ALL, (u32)color,
                                        (int)thickness);
}

/* Anti-aliased rounded rectangle whose corners are rounded selectively:
 * `mask` is a bitset of ZAN_CORNER_TL/TR/BR/BL, so welded neighbours (the
 * buttons of a group, a tab against its panel) share a square seam while the
 * outer corners stay round. zan_gui_fill_rounded_rect is this with all four. */
#define ZAN_CORNER_TL 1
#define ZAN_CORNER_TR 2
#define ZAN_CORNER_BR 4
#define ZAN_CORNER_BL 8

static void cpu_fill_round(zan_surface_t *s, int x, int y, int w, int h,
                           int radius, int mask, u32 c) {
    int r = radius;
    int ix = x, iy = y, iw = w, ih = h;
    int m = mask;
    if (r <= 0 || (m & 15) == 0) { cpu_fill_rect(s, x, y, w, h, c); return; }
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
    cpu_fill_rect(s, xl, iy, iw - 2*r, r, c);        /* top band  */
    cpu_fill_rect(s, ix, yt, iw, ih - 2*r, c);      /* middle    */
    cpu_fill_rect(s, xl, yb, iw - 2*r, r, c);        /* bottom band */

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
            cpu_fill_rect(s, zx[ci], zy[ci], r, r, c);
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

EXPORT void zan_gui_fill_rounded_rect_mask(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 mask, i32 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, fill_round)->fill_round(s, (int)x, (int)y, (int)w, (int)h,
                                        (int)radius, (int)mask, (u32)color);
}

EXPORT void zan_gui_fill_rounded_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 color) {
    zan_gui_fill_rounded_rect_mask(surface_id, x, y, w, h, radius, 15, color);
}

/* Anti-aliased stroked rounded rectangle. Straight edges are crisp (solid
 * fill_rect between the corners); the four corner arcs are anti-aliased rings
 * that share the fill's corner centres/radius so the border hugs a matching
 * fill_rounded_rect exactly instead of the old square DrawRect outline. */
static void cpu_draw_round(zan_surface_t *s, int x, int y, int w, int h,
                           int radius, int mask, u32 c, int thickness) {
    int ix = x, iy = y, iw = w, ih = h;
    int r = radius, th = thickness;
    int m = mask;
    if (r <= 0 || (m & 15) == 0) {
        cpu_draw_square_rect(s, x, y, w, h, c, thickness);
        return;
    }
    if (th < 1) th = 1;
    if (r > iw/2) r = iw/2;
    if (r > ih/2) r = ih/2;
    if (th > r) th = r;

    int xl = ix + r, xr = ix + iw - r;
    int yt = iy + r, yb = iy + ih - r;
    /* straight edges (exclude the r-wide corner zones) */
    cpu_fill_rect(s, xl, iy, iw - 2*r, th, c);              /* top    */
    cpu_fill_rect(s, xl, iy + ih - th, iw - 2*r, th, c);    /* bottom */
    cpu_fill_rect(s, ix, yt, th, ih - 2*r, c);             /* left   */
    cpu_fill_rect(s, ix + iw - th, yt, th, ih - 2*r, c);   /* right  */

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
            cpu_fill_rect(s, zx[ci], cy, r, th, c);
            cpu_fill_rect(s, cx, zy[ci], th, r, c);
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

EXPORT void zan_gui_draw_rounded_rect_mask(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 mask, i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, draw_round)->draw_round(s, (int)x, (int)y, (int)w, (int)h,
                                        (int)radius, (int)mask, (u32)color,
                                        (int)thickness);
}

EXPORT void zan_gui_draw_rounded_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 color, i32 thickness) {
    zan_gui_draw_rounded_rect_mask(surface_id, x, y, w, h, radius, 15, color, thickness);
}

/* Drop shadow: the rounded rect (x,y,w,h,radius) softened over a `blur`-wide
 * band. Coverage comes from the exact signed distance to the shape and fades
 * with a smoothstep across the band, which straddles the edge the way a
 * gaussian box-shadow does (half coverage on the outline itself). The falloff
 * is therefore continuous in every direction — including across the corner
 * diagonals, where stacking expanded rounded rects leaves wedge-shaped gaps
 * that read as concentric arcs.
 *
 * Each pixel is written exactly once, so a translucent shadow colour composites
 * to its nominal alpha instead of darkening wherever layers overlapped. */
static void cpu_shadow_round(zan_surface_t *s, int x, int y, int w, int h,
                             int radius, int blur, u32 c) {
    int iw = w, ih = h;
    if (iw <= 0 || ih <= 0) return;
    int bl = blur;
    if (bl <= 0) {
        cpu_fill_round(s, x, y, w, h, radius, ZAN_CORNERS_ALL, c);
        return;
    }
    int r = radius;
    if (r > iw/2) r = iw/2;
    if (r > ih/2) r = ih/2;
    if (r < 0) r = 0;

    /* Half extents measured from the shape centre, shrunk by the corner radius:
     * the classic rounded-box distance field is the distance to that inner
     * rectangle minus the radius. */
    double cx = (double)x + (double)iw / 2.0;
    double cy = (double)y + (double)ih / 2.0;
    double ex = (double)iw / 2.0 - (double)r;
    double ey = (double)ih / 2.0 - (double)r;

    int x0 = x - bl, x1 = x + iw + bl;
    int y0 = y - bl, y1 = y + ih + bl;
    if (x0 < s->clip_x0) x0 = s->clip_x0;
    if (y0 < s->clip_y0) y0 = s->clip_y0;
    if (x1 > s->clip_x1) x1 = s->clip_x1;
    if (y1 > s->clip_y1) y1 = s->clip_y1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->width) x1 = s->width;
    if (y1 > s->height) y1 = s->height;

    /* Everything further inside than the fade band is fully covered; fill that
     * rectangle in one go and let the distance field handle only the band. */
    int in = r + (bl + 1) / 2 + 1;
    int xi0 = x + in, xi1 = x + iw - in;
    int yi0 = y + in, yi1 = y + ih - in;
    if (xi1 > xi0 && yi1 > yi0) {
        cpu_fill_rect(s, xi0, yi0, xi1 - xi0, yi1 - yi0, c);
    } else {
        xi0 = 0; xi1 = 0; yi0 = 0; yi1 = 0;
    }

    double half = (double)bl / 2.0;
    double inv = 1.0 / (double)bl;
    for (int py = y0; py < y1; py++) {
        double qy = fabs((double)py + 0.5 - cy) - ey;
        int interiorRow = (py >= yi0 && py < yi1);
        for (int px = x0; px < x1; px++) {
            if (interiorRow && px >= xi0 && px < xi1) { px = xi1 - 1; continue; }
            double qx = fabs((double)px + 0.5 - cx) - ex;
            double mx = qx > 0.0 ? qx : 0.0;
            double my = qy > 0.0 ? qy : 0.0;
            double outside = sqrt(mx*mx + my*my);
            double inside = (qx > qy ? qx : qy);
            if (inside > 0.0) inside = 0.0;
            double d = outside + inside - (double)r;   /* <0 inside the shape */
            double t = (d + half) * inv;
            if (t <= 0.0) { set_pixel(s, px, py, c); continue; }
            if (t >= 1.0) continue;
            double cov = 1.0 - t * t * (3.0 - 2.0 * t);   /* smoothstep falloff */
            set_pixel_aa(s, px, py, c, (int)(cov * 255.0));
        }
    }
}

EXPORT void zan_gui_shadow_rounded_rect(i32 surface_id, i32 x, i32 y, i32 w, i32 h, i32 radius, i32 blur, i32 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, shadow_round)->shadow_round(s, (int)x, (int)y, (int)w, (int)h,
                                            (int)radius, (int)blur,
                                            (u32)color);
}

/* Anti-aliased filled circle.
 *
 * Scanline form: per row the interior is the run |dx| <= dxIn (dx^2 + dy^2 <=
 * (r-0.5)^2) and only the two 1px edge runs out to dxOut need a distance. The
 * interior therefore costs one blended run instead of a sqrt, a compare and a
 * clip test per pixel -- which is what a scene full of round sprites, slots,
 * bullets and particle glows spends its frame on. Pixels are identical to the
 * per-pixel form: the two radii squared are never integers plus 0.25, so no
 * sample can sit exactly on a boundary and change side. */
static void cpu_fill_circle(zan_surface_t *s, int cx, int cy, int radius,
                            u32 c) {
    int r = radius;
    double rr = (double)r;
    double rin = rr - 0.5, rout = rr + 0.5;
    double rin2 = rin * rin, rout2 = rout * rout;

    /* Every sample is taken at the PIXEL CENTRE (px+0.5, py+0.5) relative to
     * the true circle centre. The old code measured distances on the integer
     * lattice (dx,dy around the centre), i.e. half a pixel off everywhere,
     * which threw the 1px coverage ramp out of phase with the actual pixel
     * footprint: arcs came out stepped, some rows nearly binary. Same
     * convention as cpu_fill_round's corner arcs, which always looked right.
     * Interior stays a solid span run; only the 1px outer band pays a sqrt. */
    ZAN_STAT(g_st_round, (long long)(2 * r + 3) * (long long)(2 * r + 3));
    int ext = r + 2;
    int ylo = cy - ext, yhi = cy + ext;
    if (ylo < s->clip_y0) ylo = s->clip_y0;
    if (yhi >= s->clip_y1) yhi = s->clip_y1 - 1;
    for (int py = ylo; py <= yhi; py++) {
        double wy = (double)py + 0.5 - (double)cy;
        double wy2 = wy * wy;
        if (wy2 > rout2) continue;
        u32 *row = s->pixels + (size_t)py * (size_t)s->stride;
        /* Solid span: pixel centres strictly within rin. Pixel px has centre
         * cx + k + 0.5 with k = px - cx, so the span is k ∈ [-hs-0.5, hs-0.5]. */
        double hs = (rin2 > wy2) ? sqrt(rin2 - wy2) : -1.0;
        int L = cx + (int)ceil(-hs - 0.5);
        int Rr = cx + (int)floor(hs - 0.5);
        if (L < cx - ext) L = cx - ext;
        if (Rr > cx + ext) Rr = cx + ext;
        int xlo = cx - ext, xhi = cx + ext;
        if (xlo < s->clip_x0) xlo = s->clip_x0;
        if (xhi >= s->clip_x1) xhi = s->clip_x1 - 1;
        for (int px = xlo; px <= xhi; px++) {
            if (px >= L && px <= Rr) {
                row[px] = blend_over(row[px], c);
                continue;
            }
            double wx = (double)px + 0.5 - (double)cx;
            double dist = sqrt(wx * wx + wy2);
            double cov = rout - dist;
            if (cov <= 0.0) continue;
            set_pixel_aa(s, px, py, c, (int)(cov * 255.0));
        }
    }
}

EXPORT void zan_gui_fill_circle(i32 surface_id, i32 cx, i32 cy, i32 radius, i32 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, fill_circle)->fill_circle(s, (int)cx, (int)cy, (int)radius,
                                         (u32)color);
}

/* Anti-aliased circle outline: a ring of the given thickness centered on
 * the radius, with smooth coverage falloff at both edges. */
static void cpu_draw_circle(zan_surface_t *s, int cx, int cy, int radius,
                            u32 c, int thickness) {
    int r = radius;
    if (r <= 0) return;
    double half = (double)(thickness > 0 ? thickness : 1) / 2.0;
    int ext = r + (int)half + 2;

    /* Pixel-centre sampling: see cpu_fill_circle. The lattice version drew
     * rings whose stroke width visibly wobbled around the compass. */
    for (int py = cy - ext; py <= cy + ext; py++) {
        if (py < s->clip_y0 || py >= s->clip_y1) continue;
        double wy = (double)py + 0.5 - (double)cy;
        for (int px = cx - ext; px <= cx + ext; px++) {
            if (px < s->clip_x0 || px >= s->clip_x1) continue;
            double wx = (double)px + 0.5 - (double)cx;
            double d = fabs(sqrt(wx * wx + wy * wy) - (double)r);
            if (d <= half - 0.5) {
                set_pixel(s, px, py, c);
            } else if (d <= half + 0.5) {
                int cov = (int)((half + 0.5 - d) * 255.0);
                set_pixel_aa(s, px, py, c, cov);
            }
        }
    }
}

EXPORT void zan_gui_draw_circle(
    i32 surface_id, i32 cx, i32 cy, i32 radius, i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, draw_circle)->draw_circle(s, (int)cx, (int)cy, (int)radius,
                                         (u32)color, (int)thickness);
}

/* Soft radial glow: a filled disc whose alpha fades smoothly from `inner_a`
 * (0..255) at the centre to 0 at `radius`, so it reads as a luminous bloom
 * rather than a hard-edged disc. Only the low 24 bits of `color` (RGB) are
 * used; `inner_a` drives the peak alpha. The falloff is a smoothstep raised to
 * a higher power to keep a bright, tight core with a long soft tail -- this is
 * the building block for specular highlights, spotlights and border glow. */
static void cpu_fill_radial(zan_surface_t *s, int cx, int cy, int radius,
                            u32 color, int inner_a) {
    int r = radius;
    if (r <= 0) return;
    int icx = cx, icy = cy;
    u32 rgb = color & 0x00FFFFFFu;
    int peak = inner_a;
    if (peak > 255) peak = 255;
    if (peak <= 0) return;
    int r2 = r * r;
    ZAN_STAT(g_st_radial, (long long)(2 * r + 1) * (long long)(2 * r + 1));
    /* Integer squared-distance falloff (no per-pixel sqrt/double): t is 256 at
     * the centre and 0 at the edge, squared for a bright tight core and a soft
     * tail. This is called hundreds of times per animated frame, so the hot
     * loop stays entirely in integer math. */
    /* Row bounds instead of a bounding-square walk with a reject test: glows
     * are drawn by the hundred per animated frame, and the corners of the
     * square (21% of it) were visited only to be discarded. Rows and columns
     * outside the clip window are dropped up front too, so a glow clipped to a
     * control costs only the pixels it can actually write. */
    int y_lo = icy - r, y_hi = icy + r;
    if (y_lo < s->clip_y0) y_lo = s->clip_y0;
    if (y_hi > s->clip_y1 - 1) y_hi = s->clip_y1 - 1;
    for (int py = y_lo; py <= y_hi; py++) {
        int dy = py - icy;
        int dy2 = dy * dy;
        int span = r2 - dy2;
        if (span <= 0) continue;
        int dxMax = (int)sqrt((double)span);
        while (dxMax > 0 && dxMax * dxMax >= span) dxMax--;
        while ((dxMax + 1) * (dxMax + 1) < span) dxMax++;
        int x_lo = icx - dxMax, x_hi = icx + dxMax;
        if (x_lo < s->clip_x0) x_lo = s->clip_x0;
        if (x_hi > s->clip_x1 - 1) x_hi = s->clip_x1 - 1;
        u32 *row = s->pixels + (size_t)py * (size_t)s->stride;
        for (int px = x_lo; px <= x_hi; px++) {
            int dx = px - icx;
            int d2 = dx * dx + dy2;
            int t = ((r2 - d2) << 8) / r2;
            int a = (peak * t * t) >> 16;
            if (a <= 0) continue;
            row[px] = blend_over(row[px], ((u32)a << 24) | rgb);
        }
    }
}

EXPORT void zan_gui_fill_radial(i32 surface_id, i32 cx, i32 cy, i32 radius, i32 color, i32 inner_a) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, fill_radial)->fill_radial(s, (int)cx, (int)cy, (int)radius,
                                         (u32)color, (int)inner_a);
}

/* Anti-aliased filled ring sector (pie / donut slice). Angles in degrees with
 * 0 at 12 o'clock, increasing clockwise. r_inner=0 gives a solid pie slice.
 *
 * Pixel-centre sampling throughout: integer-lattice distances would put the
 * radial AA band half a pixel off the true rim, and pie rims / gauge arcs then
 * read as stepped rings.
 *
 * The scan is derived from the shape, not from a bounding square. Walking the
 * whole (2R+3)^2 square of the outer radius and paying a sqrt -- plus, for
 * everything inside the annulus, an atan2 -- on every pixel of it is nearly all
 * waste for the two shapes this actually draws: a 270-degree gauge ring of
 * radius 170 covers ~35k pixels inside a 118k square, and a 90-degree pie slice
 * discards three quarters of what it touched. Three dial gauges cost ~6ms of
 * pure libm per frame that way, which is what dominated the HMI pages once
 * their damage clipping was fixed. So:
 *   - each row's x span comes from the circle, and the inner disc is skipped as
 *     a hole rather than tested pixel by pixel;
 *   - the sweep's own bounding box clamps the scan for partial sectors;
 *   - the radial test runs on squared distances, so sqrt only runs for the two
 *     half-pixel AA bands;
 *   - the angular test is a pair of half-plane distances against the edge
 *     directions instead of an atan2 per pixel. Since the edge vectors are unit
 *     length, the cross product *is* the perpendicular distance in pixels, so
 *     the half-pixel coverage ramp is the same band the arc-length form
 *     computed -- only without the transcendental.
 */
static void cpu_fill_sector(zan_surface_t *s, int cx, int cy, int r_inner,
                            int r_outer, int a0_deg, int a1_deg, u32 c) {
    if (r_outer <= 0) return;
    double a0 = (double)a0_deg, a1 = (double)a1_deg;
    if (a1 < a0) { double tmp = a0; a0 = a1; a1 = tmp; }
    double sweep = a1 - a0;
    if (sweep <= 0.0) return;
    if (sweep > 360.0) sweep = 360.0;
    /* One representation of the start angle for both the bbox scan below and
     * the edge vectors: fold it into [0,360). */
    a0 = a0 - floor(a0 / 360.0) * 360.0;
    a1 = a0 + sweep;

    const double PI = 3.14159265358979323846;
    double ri = (double)r_inner;
    if (ri < 0.0) ri = 0.0;
    double ro = (double)r_outer;
    int hasHole = ri > 0.0;
    double roOut = ro + 0.5, roIn = ro - 0.5;
    double riOut = ri - 0.5, riIn = ri + 0.5;
    if (riOut < 0.0) riOut = 0.0;
    double roOut2 = roOut * roOut, roIn2 = roIn * roIn;
    double riOut2 = riOut * riOut, riIn2 = riIn * riIn;
    if (roIn2 < 0.0) roIn2 = 0.0;
    int full = sweep >= 360.0;

    /* Edge directions (unit): angle a points at (sin a, -cos a) on screen. */
    double e0x = sin(a0 * PI / 180.0), e0y = -cos(a0 * PI / 180.0);
    double e1x = sin(a1 * PI / 180.0), e1y = -cos(a1 * PI / 180.0);
    /* cross(v,w) = v.x*w.y - v.y*w.x is > 0 when w is clockwise of v. */
    int wide = sweep > 180.0;   /* not an intersection of two half-planes */

    int bx0 = cx - r_outer - 1, bx1 = cx + r_outer + 1;
    int by0 = cy - r_outer - 1, by1 = cy + r_outer + 1;
    if (!full) {
        /* Extremes of the sweep: both edges at both radii, the apex when the
         * sector is solid, and the outer rim at every axis crossing inside. */
        double xmn = e0x * ri, xmx = xmn, ymn = e0y * ri, ymx = ymn;
        double cand[10][2];
        int n = 0;
        cand[n][0] = e0x * ro; cand[n][1] = e0y * ro; n++;
        cand[n][0] = e1x * ri; cand[n][1] = e1y * ri; n++;
        cand[n][0] = e1x * ro; cand[n][1] = e1y * ro; n++;
        if (!hasHole) { cand[n][0] = 0.0; cand[n][1] = 0.0; n++; }
        for (int k = 0; k <= 8; k++) {
            double ca = (double)(k * 90);
            if (ca < a0 || ca > a1) continue;
            cand[n][0] = sin(ca * PI / 180.0) * ro;
            cand[n][1] = -cos(ca * PI / 180.0) * ro;
            n++;
        }
        for (int i = 0; i < n; i++) {
            if (cand[i][0] < xmn) xmn = cand[i][0];
            if (cand[i][0] > xmx) xmx = cand[i][0];
            if (cand[i][1] < ymn) ymn = cand[i][1];
            if (cand[i][1] > ymx) ymx = cand[i][1];
        }
        int nx0 = cx + (int)floor(xmn) - 1, nx1 = cx + (int)ceil(xmx) + 1;
        int ny0 = cy + (int)floor(ymn) - 1, ny1 = cy + (int)ceil(ymx) + 1;
        if (nx0 > bx0) bx0 = nx0;
        if (nx1 < bx1) bx1 = nx1;
        if (ny0 > by0) by0 = ny0;
        if (ny1 < by1) by1 = ny1;
    }
    if (bx0 < s->clip_x0) bx0 = s->clip_x0;
    if (by0 < s->clip_y0) by0 = s->clip_y0;
    if (bx1 > s->clip_x1 - 1) bx1 = s->clip_x1 - 1;
    if (by1 > s->clip_y1 - 1) by1 = s->clip_y1 - 1;

    for (int py = by0; py <= by1; py++) {
        double wy = (double)py + 0.5 - (double)cy;
        double wy2 = wy * wy;
        if (wy2 > roOut2) continue;
        double half = sqrt(roOut2 - wy2);
        int rx0 = (int)ceil((double)cx - half - 0.5);
        int rx1 = (int)floor((double)cx + half - 0.5);
        if (rx0 < bx0) rx0 = bx0;
        if (rx1 > bx1) rx1 = bx1;
        /* Two spans when this row crosses the hole; one otherwise. The span
         * bounds are rounded outwards by a pixel, so the per-pixel radial test
         * still owns the exact edge. */
        int spans[2][2];
        int nspan = 0;
        if (hasHole && wy2 < riOut2) {
            double hi = sqrt(riOut2 - wy2);
            int hx0 = (int)floor((double)cx - hi - 0.5);
            int hx1 = (int)ceil((double)cx + hi - 0.5);
            spans[nspan][0] = rx0; spans[nspan][1] = hx0 < rx1 ? hx0 : rx1;
            nspan++;
            spans[nspan][0] = hx1 > rx0 ? hx1 : rx0; spans[nspan][1] = rx1;
            nspan++;
        } else {
            spans[nspan][0] = rx0; spans[nspan][1] = rx1;
            nspan++;
        }
        for (int sp = 0; sp < nspan; sp++) {
            int x0 = spans[sp][0], x1 = spans[sp][1];
            if (x0 < bx0) x0 = bx0;
            if (x1 > bx1) x1 = bx1;
            for (int px = x0; px <= x1; px++) {
                double wx = (double)px + 0.5 - (double)cx;
                double d2 = wx * wx + wy2;
                if (d2 > roOut2) continue;
                if (hasHole && d2 < riOut2) continue;
                double radCov = 1.0;
                if (d2 > roIn2) radCov = roOut - sqrt(d2);
                else if (hasHole && d2 < riIn2) radCov = sqrt(d2) - riOut;
                if (radCov <= 0.0) continue;
                if (radCov > 1.0) radCov = 1.0;
                double angCov = 1.0;
                if (!full) {
                    /* Perpendicular distance to each edge, positive inside. */
                    if (wide) {
                        /* Sweeps past a half turn are not an intersection of
                         * half-planes; measure the gap instead and invert. */
                        double gLo = e1x * wy - e1y * wx;
                        double gHi = wx * e0y - wy * e0x;
                        double covLo = gLo + 0.5, covHi = gHi + 0.5;
                        if (covLo > 1.0) covLo = 1.0;
                        if (covLo < 0.0) covLo = 0.0;
                        if (covHi > 1.0) covHi = 1.0;
                        if (covHi < 0.0) covHi = 0.0;
                        angCov = 1.0 - covLo * covHi;
                    } else {
                        double covLo = (e0x * wy - e0y * wx) + 0.5;
                        double covHi = (wx * e1y - wy * e1x) + 0.5;
                        if (covLo <= 0.0 || covHi <= 0.0) continue;
                        if (covLo > 1.0) covLo = 1.0;
                        if (covHi > 1.0) covHi = 1.0;
                        angCov = covLo * covHi;
                    }
                    if (angCov <= 0.0) continue;
                }
                int cov = (int)(radCov * angCov * 255.0);
                if (cov >= 255) set_pixel(s, px, py, c);
                else set_pixel_aa(s, px, py, c, cov);
            }
        }
    }
}

EXPORT void zan_gui_fill_sector(
    i32 surface_id, i32 cx, i32 cy, i32 r_inner, i32 r_outer, i32 a0_deg,
    i32 a1_deg, i32 color) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, fill_sector)->fill_sector(s, (int)cx, (int)cy, (int)r_inner,
                                         (int)r_outer, (int)a0_deg,
                                         (int)a1_deg, (u32)color);
}

/* Anti-aliased line (Wu's algorithm) */
static void cpu_draw_line(zan_surface_t *s, int x0, int y0, int x1, int y1,
                          u32 c, int thickness) {
    int t = thickness;

    if (t <= 1) {
        /* Wu's anti-aliased line */
        int steep = abs(y1 - y0) > abs(x1 - x0);
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
        int lox = (x0 < x1 ? x0 : x1);
        int hix = (x0 > x1 ? x0 : x1);
        int loy = (y0 < y1 ? y0 : y1);
        int hiy = (y0 > y1 ? y0 : y1);
        int minx = lox - t - 1, maxx = hix + t + 1;
        int miny = loy - t - 1, maxy = hiy + t + 1;
        /* Walk only the pixels a row can actually cover. The covered set is a
         * capsule (segment + round caps), so each row meets it in one interval
         * whose ends are where the row crosses the two offset edges or the two
         * end circles. A steep 150px stroke used to test its whole 150x150
         * bounding box -- ~50x the pixels it paints -- which is what made
         * charts, spark lines and animated vector art expensive. */
        double ux = 0.0, uy = 0.0, seglen = sqrt(len2);
        if (seglen > 0.0001) { ux = dx / seglen; uy = dy / seglen; }
        double rr = half + 0.5;
        if (miny < s->clip_y0) miny = s->clip_y0;
        if (maxy > s->clip_y1 - 1) maxy = s->clip_y1 - 1;
        if (minx < s->clip_x0) minx = s->clip_x0;
        if (maxx > s->clip_x1 - 1) maxx = s->clip_x1 - 1;
        for (int py = miny; py <= maxy; py++) {
            double yc = (double)py;
            double lo = 1e30, hi = -1e30;
            for (int e = 0; e < 2; e++) {
                double ecx = e ? (double)x1 : (double)x0;
                double ecy = e ? (double)y1 : (double)y0;
                double dyc = yc - ecy;
                if (dyc * dyc > rr * rr) continue;
                double half_w = sqrt(rr * rr - dyc * dyc);
                if (ecx - half_w < lo) lo = ecx - half_w;
                if (ecx + half_w > hi) hi = ecx + half_w;
            }
            if (seglen > 0.0001) {
                double nx = -uy * rr, ny = ux * rr;   /* perpendicular offset */
                for (int e = 0; e < 2; e++) {
                    double sgn = e ? -1.0 : 1.0;
                    double ax = (double)x0 + sgn * nx, ay = (double)y0 + sgn * ny;
                    double bx = (double)x1 + sgn * nx, by = (double)y1 + sgn * ny;
                    double span = by - ay;
                    if (span > -0.0001 && span < 0.0001) continue;
                    double tt = (yc - ay) / span;
                    if (tt < 0.0 || tt > 1.0) continue;
                    double xs = ax + (bx - ax) * tt;
                    if (xs < lo) lo = xs;
                    if (xs > hi) hi = xs;
                }
            }
            if (lo > hi) continue;
            int rowLo = (int)floor(lo) - 1, rowHi = (int)ceil(hi) + 1;
            if (rowLo < minx) rowLo = minx;
            if (rowHi > maxx) rowHi = maxx;
            for (int px = rowLo; px <= rowHi; px++) {
                double pxc = (double)px + 0.5, pyc = (double)py + 0.5;
                double proj = 0.0;
                if (len2 > 0.0001) {
                    proj = ((pxc - (double)x0) * dx + (pyc - (double)y0) * dy) / len2;
                    if (proj < 0.0) proj = 0.0;
                    if (proj > 1.0) proj = 1.0;
                }
                double cxp = (double)x0 + proj * dx;
                double cyp = (double)y0 + proj * dy;
                double ddx = pxc - cxp, ddy = pyc - cyp;
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

EXPORT void zan_gui_draw_line(i32 surface_id, i32 x0, i32 y0, i32 x1, i32 y1, i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    ZAN_IMPL(s, draw_line)->draw_line(s, (int)x0, (int)y0, (int)x1, (int)y1,
                                     (u32)color, (int)thickness);
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
        /* One clear on (re)allocation only -- malloc leaves garbage that
         * would break the first-touch marker below. Every pass afterwards
         * restores all-zero itself: the blend pass resets each pixel it
         * touched, so a fresh O(surface) sweep per draw call is not needed.
         * Width changes do not matter either: slots are only ever written
         * through the tracked path, which always zeroes them again. */
        if (g_poly_cov) memset(g_poly_cov, 0, (size_t)W * (size_t)H);
    }
    if (!g_poly_cov) { g_poly_cov_w = 0; g_poly_cov_h = 0; return; }
    g_poly_dirty_n = 0;
    /* First-touch marker relies on the buffer being zero here; see the
     * allocation-time clear above and the blend-pass reset below. */

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
            /* Pixel-centre sampling (px+0.5, py+0.5): measuring from the bare
             * lattice point put every coverage ramp half a pixel out of phase
             * with the pixel footprint, which is exactly the "curves look
             * stepped" artifact. Chart smooth lines all come through here. */
            double pyc = (double)py + 0.5;
            for (int px = minx; px <= maxx; px++) {
                double pxc = (double)px + 0.5;
                double proj = ((pxc - x0) * dx + (pyc - y0) * dy) / len2;
                if (proj < 0.0) proj = 0.0;
                if (proj > 1.0) proj = 1.0;
                double cxp = x0 + proj * dx;
                double cyp = y0 + proj * dy;
                double ddx = pxc - cxp, ddy = pyc - cyp;
                double dist = sqrt(ddx * ddx + ddy * ddy);
                double cov = half + 0.5 - dist;   /* 1px band, like circle */
                if (cov <= 0.0) continue;
                int icov = aa_coverage(cov);
                int idx = py * W + px;
                if (g_poly_cov[idx] == 0) {       /* first touch this pass */
                    if (g_poly_dirty_n >= g_poly_dirty_cap) {
                        i32 cap2 = g_poly_dirty_cap ? g_poly_dirty_cap * 2 : 1024;
                        i32 *nd = (i32*)realloc(g_poly_dirty,
                            (size_t)cap2 * sizeof(i32));
                        /* Growth failed (test-injected OOM only): blending
                         * nothing and resetting coverage keeps the buffer's
                         * all-zero invariant intact -- pixels written after
                         * the aborted tracking would otherwise poison the
                         * next pass's first-touch marker. This call simply
                         * draws without anti-aliasing bookkeeping. */
                        if (!nd) {
                            memset(g_poly_cov, 0, (size_t)W * (size_t)H);
                            g_poly_dirty_n = 0;
                            return;
                        }
                        g_poly_dirty = nd;
                        g_poly_dirty_cap = cap2;
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
    ZAN_IMPL(s, polyline)->polyline(s, pts, (int)n, (u32)color, t, 0);
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
    ZAN_IMPL(s, polyline)->polyline(s, pts, (int)n, (u32)color, t, 8);
}

EXPORT void *zan_gui_get_pixels(i32 surface_id) {
    if (surface_id < 0 || surface_id >= g_surface_count || !g_surfaces[surface_id]) return NULL;
    zan_surface_t *s = g_surfaces[surface_id];
    /* This is the shells' present seam (GDI SetDIBitsToDevice /
     * UpdateLayeredWindow blit straight out of the returned buffer), so a GPU
     * backend has to settle its batches and mirror the frame back here. */
    if (s->be) {
        if (s->be->flush) s->be->flush(s);
        if (s->be->read_pixels) s->be->read_pixels(s);
    }
    return (void *)s->pixels;
}

/* One frame pixel as 0xAARRGGBB, or -1 outside the surface. Windowless
 * assertions (tests) read sampled blits back through this. */
EXPORT i32 zan_gui_read_pixel(i32 surface_id, i32 x, i32 y) {
    if (surface_id < 0 || surface_id >= g_surface_count || !g_surfaces[surface_id])
        return -1;
    zan_surface_t *s = g_surfaces[surface_id];
    if (x < 0 || y < 0 || x >= s->width || y >= s->height) return -1;
    if (s->be) {
        if (s->be->flush) s->be->flush(s);
        if (s->be->read_pixels) s->be->read_pixels(s);
    }
    return (i32)(s->pixels[(size_t)y * (size_t)s->stride + (size_t)x]);
}

/* The other present seam: hand the frame to the screen without a CPU copy.
 * `native_window` is the shell's window handle (HWND on Win32, X11 Window as an
 * integer). 1 means the frame is on screen and the caller must not blit its
 * bitmap; 0 means it has to present the usual way -- which is always the answer
 * on the CPU backend, for a layered (glass) window the backend cannot swap, and
 * on any machine without GL. */
EXPORT i32 zan_gui_present_window(i32 surface_id, void *native_window) {
    if (surface_id < 0 || surface_id >= g_surface_count ||
        !g_surfaces[surface_id]) return 0;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s->be || !s->be->present) return 0;
    return s->be->present(s, native_window) ? 1 : 0;
}

/* A shell window is closing: let the backend release whatever it attached to
 * that handle (its GL child window) before the handle dies. */
EXPORT void zan_gui_release_window(void *native_window) {
    if (g_backend && g_backend->drop_window)
        g_backend->drop_window(native_window);
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
/* Only 8-bit-per-channel decoding is used (zan_gui_load_image, the tray icon,
 * the SDL texture loader). Dropping the float/HDR paths also drops stb's only
 * call to pow(), so the driver archives link without libm on targets whose
 * link line does not carry it. */
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STB_IMAGE_IMPLEMENTATION
#include "../../stdlib/SDL3/native/stb_image.h"
/* WebP and SVG: stb_image does not decode either, so the vendored libwebp
 * decode-only subset (src/runtime/libwebp, scalar + baseline SSE2, no
 * threads) and the nanosvg raster TU are compiled in below, unity-build
 * style -- the same pattern as the gui_runtime_* siblings, which keeps the
 * one-file runtime compiles (build_ide.ps1, build_gallery.ps1, the CMake
 * zan_gui target) working unchanged. HAVE_CONFIG_H switches libwebp to the
 * vendored src/webp/config.h stub: SSE2 on, SSE41/threads off (see the
 * stub). See src/runtime/libwebp/README.md for the subset. */
#define HAVE_CONFIG_H
#include "libwebp/src/dec/alpha_dec.c"
#include "libwebp/src/dec/buffer_dec.c"
#include "libwebp/src/dec/frame_dec.c"
#include "libwebp/src/dec/idec_dec.c"
#include "libwebp/src/dec/io_dec.c"
#include "libwebp/src/dec/quant_dec.c"
#include "libwebp/src/dec/tree_dec.c"
#include "libwebp/src/dec/vp8_dec.c"
#include "libwebp/src/dec/vp8l_dec.c"
#include "libwebp/src/dec/webp_dec.c"
#include "libwebp/src/dsp/alpha_processing.c"
#include "libwebp/src/dsp/alpha_processing_sse2.c"
#include "libwebp/src/dsp/cpu.c"
#include "libwebp/src/dsp/dec.c"
#include "libwebp/src/dsp/dec_sse2.c"
#include "libwebp/src/dsp/dec_clip_tables.c"
#include "libwebp/src/dsp/filters.c"
#include "libwebp/src/dsp/filters_sse2.c"
#include "libwebp/src/dsp/lossless.c"
#include "libwebp/src/dsp/lossless_sse2.c"
#include "libwebp/src/dsp/rescaler.c"
#include "libwebp/src/dsp/rescaler_sse2.c"
#include "libwebp/src/dsp/upsampling.c"
#include "libwebp/src/dsp/upsampling_sse2.c"
#include "libwebp/src/dsp/yuv.c"
#include "libwebp/src/dsp/yuv_sse2.c"
#include "libwebp/src/utils/bit_reader_utils.c"
#include "libwebp/src/utils/color_cache_utils.c"
#include "libwebp/src/utils/filters_utils.c"
#include "libwebp/src/utils/huffman_utils.c"
#include "libwebp/src/utils/palette.c"
#include "libwebp/src/utils/quant_levels_dec_utils.c"
#include "libwebp/src/utils/random_utils.c"
#include "libwebp/src/utils/rescaler_utils.c"
#include "libwebp/src/utils/thread_utils.c"
#include "libwebp/src/utils/utils.c"
#include "gui_image_svg.c"

#define ZAN_IMG_CACHE_CAP 64
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

/* ---- memory-keyed images --------------------------------------------- */
/* Decoded pixels registered under a caller-chosen key instead of a file
 * path: base64 data URIs, HTTP downloads and SVG rasters land here so the
 * path-based width/height/blit/evict exports work on them unchanged. Keys
 * use a "mem:" prefix; zan_img_load looks them up here and never falls
 * through to the file path (and never marks them bad), so a key that is
 * not registered is simply absent and the Zan side re-registers on
 * demand. Only ever touched from the UI thread, like g_imgs. */
#define ZAN_IMG_MEM_CAP 64
static zan_img_t g_mem_imgs[ZAN_IMG_MEM_CAP];
static int       g_mem_img_n = 0;

static int zan_img_is_mem_key(const char *key) {
    return key && strncmp(key, "mem:", 4) == 0;
}

static zan_img_t *zan_img_mem_find(const char *key) {
    int i;
    for (i = 0; i < g_mem_img_n; i++)
        if (strncmp(g_mem_imgs[i].path, key, 511) == 0) return &g_mem_imgs[i];
    return NULL;
}

/* Takes ownership of pix. FIFO-evicts the oldest entry when full; the Zan
 * side re-registers an evicted key from its retained source bytes. */
static zan_img_t *zan_img_mem_put(const char *key, u32 *pix, int w, int h) {
    zan_img_t *e = zan_img_mem_find(key);
    if (e) { free(pix); return e; }   /* already registered: keep the first */
    if (g_mem_img_n >= ZAN_IMG_MEM_CAP) {
        free(g_mem_imgs[0].pix);
        memmove(&g_mem_imgs[0], &g_mem_imgs[1],
                sizeof(zan_img_t) * (ZAN_IMG_MEM_CAP - 1));
        g_mem_img_n = ZAN_IMG_MEM_CAP - 1;
    }
    e = &g_mem_imgs[g_mem_img_n++];
    strncpy(e->path, key, 511); e->path[511] = '\0';
    e->pix = pix; e->w = w; e->h = h;
    return e;
}

/* RGBA8 (stb / libwebp / nanosvg layout) -> ARGB32 pixel buffer. */
static u32 *zan_rgba_to_argb(const unsigned char *data, int w, int h) {
    u32 *pix = (u32 *)malloc((size_t)w * (size_t)h * sizeof(u32));
    int i, n = w * h;
    if (!pix) return NULL;
    for (i = 0; i < n; i++) {
        unsigned char r = data[i*4], g = data[i*4+1],
                      b = data[i*4+2], a = data[i*4+3];
        pix[i] = ((u32)a << 24) | ((u32)r << 16) | ((u32)g << 8) | b;
    }
    return pix;
}

static zan_img_t *zan_img_load(const char *path) {
    zan_img_t *e;
    int w, h, n;
    unsigned char *data;
    u32 *pix;
    if (!path || !path[0]) return NULL;
    e = zan_img_find(path);
    if (e) return e;
    /* Memory-keyed images never fall through to the file path: an
     * unregistered key is absent until the Zan side re-registers it. */
    if (zan_img_is_mem_key(path)) return zan_img_mem_find(path);
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
    pix = zan_rgba_to_argb(data, w, h);
    stbi_image_free(data);
    if (!pix) return NULL;
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

/* Evict cached pixels for a path or a "mem:" key (call after the source
 * file changes, or to release a memory image). */
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
    for (i = 0; i < g_mem_img_n; i++) {
        if (strncmp(g_mem_imgs[i].path, path, 511) == 0) {
            free(g_mem_imgs[i].pix);
            memmove(&g_mem_imgs[i], &g_mem_imgs[i+1],
                    sizeof(zan_img_t) * (g_mem_img_n - i - 1));
            g_mem_img_n--;
            return;
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

/* ---- in-memory image registration ------------------------------------- */

/* Register a decoded image under a caller-chosen "mem:..." key. data/len is
 * a raw image byte blob: WebP is sniffed from the RIFF header and decoded by
 * the vendored libwebp; everything else (PNG/JPEG/BMP/GIF/TGA/PNM/PSD) goes
 * through stb_image. Re-registering a live key is a no-op returning the
 * cached width. Returns the image width, 0 on decode failure -- the caller
 * keeps the source bytes and may retry with the same key. */
EXPORT i32 zan_gui_image_load_mem(const char *key, const char *data, i32 len) {
    int w, h;
    u32 *pix = NULL;
    if (!key || !key[0] || !data || len <= 0) return 0;
    zan_img_t *e = zan_img_mem_find(key);
    if (e) return e->w;
    if (len >= 12 && memcmp(data, "RIFF", 4) == 0
        && memcmp(data + 8, "WEBP", 4) == 0) {
        if (WebPGetInfo((const uint8_t *)data, (size_t)len, &w, &h)) {
            uint8_t *rgba = WebPDecodeRGBA((const uint8_t *)data, (size_t)len,
                                           &w, &h);
            if (rgba) {
                pix = zan_rgba_to_argb(rgba, w, h);
                WebPFree(rgba);
            }
        }
    } else {
        int n;
        unsigned char *rgba = stbi_load_from_memory((const unsigned char *)data,
                                                    (int)len, &w, &h, &n, 4);
        if (rgba) {
            if (w > 0 && h > 0 && w <= 32768 && h <= 32768)
                pix = zan_rgba_to_argb(rgba, w, h);
            else
                w = h = 0;
            stbi_image_free(rgba);
        }
    }
    if (!pix || w <= 0 || h <= 0) { free(pix); return 0; }
    zan_img_mem_put(key, pix, w, h);
    return w;
}

/* byte[] flavor of load_mem: the Zan array marshals as the same raw
 * pointer+len pair a string body would, so it forwards verbatim. */
EXPORT i32 zan_gui_image_load_mem_bytes(const char *key, const char *data,
                                        i32 len) {
    return zan_gui_image_load_mem(key, data, len);
}

/* Rasterize an SVG document (gui_image_svg.c, vendored nanosvg) into a
 * "mem:" key. rasterW/rasterH are the target box: the document keeps its
 * aspect ratio and is fitted inside (contain); <= 0 uses the document's
 * intrinsic size. Returns the raster width, 0 on failure. */
EXPORT i32 zan_gui_image_load_svg(const char *key, const char *text, i32 len,
                                  i32 rasterW, i32 rasterH) {
    u32 *pix = NULL;
    int w = 0, h = 0;
    if (!key || !key[0] || !text) return 0;
    zan_img_t *e = zan_img_mem_find(key);
    if (e) return e->w;
    if (len < 0) len = (i32)strlen(text);
    if (len <= 0) return 0;
    if (!zan_svg_raster(text, (int)len, &pix, &w, &h, (int)rasterW,
                        (int)rasterH))
        return 0;
    zan_img_mem_put(key, pix, w, h);
    return w;
}

/* Blit a region of an image file onto a GUI surface.
 * sx/sy/sw/sh = source rect in the image (all zero = use the full image).
 * dx/dy/dw/dh = destination rect on the surface.
 *
 * Sampling: shrinking an axis averages every source pixel the destination
 * touches (box filter) -- plain nearest drops whole columns/rows, which made
 * thin lines shimmer away on photo thumbnails. Growing or 1:1 samples the
 * centre of each destination pixel, (2i+1)*src/(2*dst): the old floor mapping
 * i*src/dst pinned destination column 0 to source column 0 and duplicated the
 * left/top edge, so upscaled sprites hugged one corner. 1:1 is exact under
 * both formulas, so unscaled blits are bit-identical to before. */
static void cpu_blit_image(zan_surface_t *s, const char *path,
                           int dx, int dy, int dw, int dh,
                           int sx, int sy, int sw, int sh)
{
    zan_img_t *img;
    int isx, isy, isw, ish, idx2, idy, idw, idh, x0, y0, x1, y1, py, px;
    img = zan_img_load(path);
    if (!img) return;
    isx = sx; isy = sy; isw = sw; ish = sh;
    if (isw <= 0) { isx = 0; isw = img->w; }
    if (ish <= 0) { isy = 0; ish = img->h; }
    idx2 = dx; idy = dy; idw = dw; idh = dh;
    if (idw <= 0) idw = isw;
    if (idh <= 0) idh = ish;
    x0 = idx2 > s->clip_x0 ? idx2 : s->clip_x0;
    y0 = idy  > s->clip_y0 ? idy  : s->clip_y0;
    x1 = idx2 + idw < s->clip_x1 ? idx2 + idw : s->clip_x1;
    y1 = idy  + idh < s->clip_y1 ? idy  + idh : s->clip_y1;
    ZAN_STAT(g_st_img, (long long)(x1 - x0) * (long long)(y1 - y0));
    if (idw <= 0 || idh <= 0 || x1 <= x0 || y1 <= y0) return;

    int boxX = idw < isw;
    int boxY = idh < ish;
    /* Fixed-point column walk with a running (quotient, remainder) pair: the
     * source column used to cost an integer divide per pixel, and every pixel
     * went through the general blend even where the sprite is fully
     * transparent (most of a tile sheet's cell) or fully opaque. Sprites and
     * tiles are the bulk of a game frame's pixels, so both are worth avoiding.
     * i >= 0 always: the loop starts at the clipped destination edge, never
     * left of it. */
    long long nDen = boxX ? idw : 2 * (long long)idw;
    long long t0 = boxX ? (long long)(x0 - idx2) * isw
                        : ((long long)(x0 - idx2) * 2 + 1) * isw;
    long long tStep = boxX ? isw : 2 * (long long)isw;
    int qx = (int)(t0 / nDen);
    int rx = (int)(t0 % nDen);
    int qxStep = (int)(tStep / nDen);
    int rxStep = (int)(tStep % nDen);
    int rDen = (int)nDen;
    /* Second stepper only when shrinking X: the exclusive end of the source
     * range, ceil((i+1)*isw/idw). */
    int qx2 = (int)((((long long)(x0 - idx2) + 1) * isw + idw - 1) / idw);
    int rx2 = (int)((((long long)(x0 - idx2) + 1) * isw + idw - 1) % idw);
    for (py = y0; py < y1; py++) {
        int sy0, sy1;
        if (boxY) {
            sy0 = isy + (int)((long long)(py - idy) * ish / idh);
            sy1 = isy + (int)(((long long)(py - idy + 1) * ish + idh - 1) / idh);
            if (sy1 <= sy0) { sy1 = sy0 + 1; }
        } else {
            /* Dest-pixel centre row: (2i+1)*ish / (2*idh), one divide/row. */
            sy0 = isy + (int)(((long long)(py - idy) * 2 + 1) * ish / (2 * (long long)idh));
            sy1 = sy0 + 1;
        }
        if (sy0 < 0 || sy1 > img->h || sy0 >= sy1) continue;
        u32 *drow = s->pixels + (size_t)py * (size_t)s->stride;
        int qxR = qx, rxR = rx, qx2R = qx2, rx2R = rx2;
        for (px = x0; px < x1; px++) {
            int sx0 = isx + qxR;
            int sx1 = boxX ? isx + qx2R : sx0 + 1;
            if (boxX && sx1 <= sx0) { sx1 = sx0 + 1; }
            qxR += qxStep; rxR += rxStep;
            if (rxR >= rDen) { rxR -= rDen; qxR++; }
            if (boxX) {
                qx2R += qxStep; rx2R += rxStep;
                if (rx2R >= idw) { rx2R -= idw; qx2R++; }
            }
            if (sx0 < 0 || sx1 > img->w || sx0 >= sx1) continue;
            u32 sp;
            if (sy1 - sy0 == 1 && sx1 - sx0 == 1) {
                sp = img->pix[(size_t)sy0 * (size_t)img->w + (size_t)sx0];
            } else {
                /* Box average, straight alpha: weight every covered source
                 * pixel equally and round half up. For sprites the alpha
                 * average is an approximation (exact only over uniform
                 * alpha); photos and UI chrome are fully opaque. */
                int sa = 0, sr = 0, sg = 0, sb = 0, n = 0;
                for (int yy = sy0; yy < sy1; yy++) {
                    const u32 *srow = img->pix + (size_t)yy * (size_t)img->w;
                    for (int xx = sx0; xx < sx1; xx++) {
                        u32 p = srow[xx];
                        sa += (int)(p >> 24);
                        sr += (int)((p >> 16) & 255);
                        sg += (int)((p >> 8) & 255);
                        sb += (int)(p & 255);
                        n++;
                    }
                }
                int ha = n / 2;
                sp = (u32)((sa + ha) / n) << 24
                   | (u32)((sr + ha) / n) << 16
                   | (u32)((sg + ha) / n) << 8
                   | (u32)((sb + ha) / n);
            }
            u32 dp = drow[px];
            u32 da = sp >> 24;
            if (da == 0) continue;
            if (da == 0xFF) { drow[px] = sp; continue; }
            drow[px] = blend_over(dp, sp);
        }
    }
}

EXPORT void zan_gui_blit_image(
    i32 surf_id, const char *path, i32 dx, i32 dy, i32 dw, i32 dh, i32 sx,
    i32 sy, i32 sw, i32 sh)
{
    if (surf_id < 0 || surf_id >= g_surface_count || !g_surfaces[surf_id]) return;
    zan_surface_t *s = g_surfaces[surf_id];
    ZAN_IMPL(s, blit_image)->blit_image(s, path, (int)dx, (int)dy, (int)dw,
                                        (int)dh, (int)sx, (int)sy, (int)sw,
                                        (int)sh);
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
#include "gui_runtime_glyph.c"
#include "gui_runtime_text.c"
#include "gui_runtime_sdl.c"
#include "gui_runtime_x11.c"
#include "gui_runtime_font.c"
#include "gui_runtime_tray.c"
#include "gui_runtime_shims.c"

/* ---- the software rasterizer as a backend ------------------------------
 * Every entry above is the code that always drew these primitives, unchanged;
 * gathering them here is what makes "CPU" one backend among others rather than
 * an implicit fallback path. Defined last because it names entries from the
 * translation-unit parts included above.
 *
 * draw_text is deliberately absent: glyph rasterization is platform code (GDI /
 * FreeType / CoreText) the export still calls itself. What it produces does
 * arrive here, as the coverage tiles of glyph_run. */
static void cpu_polyline(zan_surface_t *s, const int32_t *pts, int n, u32 c,
                         int t, int shift) {
    zan_polyline_core(s, pts, (i32)n, c, t, shift);
}

/* Composite one text run's cached coverage tiles (gui_runtime_glyph.c).
 *
 * Single-channel tiles blend as any other anti-aliased primitive. Per-channel
 * tiles come from GDI's ClearType, where each of R/G/B carries its own
 * coverage: they are blended channel by channel and land opaque, which is the
 * only way subpixel-antialiased text keeps looking like the system's. The clip
 * is resolved into pixel ranges once per tile rather than per pixel, since this
 * loop runs over every glyph pixel of every repaint. */
static void cpu_glyph_run(zan_surface_t *s, const zan_glyph_run *run) {
    u32 cr = (run->color >> 16) & 0xFF;
    u32 cg = (run->color >> 8) & 0xFF;
    u32 cb = run->color & 0xFF;
    u32 ca = (run->color >> 24) & 0xFF;
    if (ca == 0) ca = 255;   /* 0 alpha means opaque for text */
    for (int i = 0; i < run->count; i++) {
        const zan_glyph_tile *tile = run->items[i].tile;
        int ox = run->items[i].x, oy = run->items[i].y;
        int tw = tile->w, th = tile->h;
        int py0 = s->clip_y0 - oy; if (py0 < 0) py0 = 0;
        int py1 = s->clip_y1 - oy; if (py1 > th) py1 = th;
        int px0 = s->clip_x0 - ox; if (px0 < 0) px0 = 0;
        int px1 = s->clip_x1 - ox; if (px1 > tw) px1 = tw;
        if (tile->bpp == 1) {
            const unsigned char *cov = (const unsigned char *)tile->cov;
            for (int py = py0; py < py1; py++) {
                const unsigned char *srow = cov + (size_t)py * (size_t)tw;
                for (int px = px0; px < px1; px++) {
                    if (srow[px])
                        set_pixel_aa(s, ox + px, oy + py, run->color, srow[px]);
                }
            }
            continue;
        }
        const u32 *cov = (const u32 *)tile->cov;
        for (int py = py0; py < py1; py++) {
            const u32 *srow = cov + (size_t)py * (size_t)tw;
            int dst_row = (oy + py) * s->stride + ox;
            for (int px = px0; px < px1; px++) {
                u32 sp = srow[px];
                if ((sp & 0x00FFFFFFu) == 0) continue;
                u32 ar = ((sp >> 16) & 0xFF) * ca / 255;
                u32 ag = ((sp >> 8) & 0xFF) * ca / 255;
                u32 ab = (sp & 0xFF) * ca / 255;
                int idx = dst_row + px;
                u32 dp = s->pixels[idx];
                u32 dr = (dp >> 16) & 0xFF;
                u32 dg = (dp >> 8) & 0xFF;
                u32 db = dp & 0xFF;
                u32 or_ = (cr * ar + dr * (255 - ar)) / 255;
                u32 og = (cg * ag + dg * (255 - ag)) / 255;
                u32 ob = (cb * ab + db * (255 - ab)) / 255;
                s->pixels[idx] = (255u << 24) | (or_ << 16) | (og << 8) | ob;
            }
        }
    }
}

const zan_gui_backend zan_cpu_backend = {
    .name         = "cpu",
    .clear_rect   = cpu_clear_rect,
    .fill_rect    = cpu_fill_rect,
    .fill_round   = cpu_fill_round,
    .draw_round   = cpu_draw_round,
    .fill_vgrad   = cpu_fill_vgrad,
    .fill_grad    = cpu_fill_grad,
    .shadow_round = cpu_shadow_round,
    .fill_circle  = cpu_fill_circle,
    .draw_circle  = cpu_draw_circle,
    .fill_radial  = cpu_fill_radial,
    .fill_sector  = cpu_fill_sector,
    .draw_line    = cpu_draw_line,
    .polyline     = cpu_polyline,
    .blur         = cpu_blur,
    .snapshot     = cpu_snapshot,
    .snapshot_patch = cpu_snapshot_patch,
    .restore      = cpu_restore,
    .draw_text    = NULL,   /* the font engine is platform code, not a backend */
    .glyph_run    = cpu_glyph_run,
    .blit_image   = cpu_blit_image,
    .set_clip     = NULL,   /* the CPU path clips per pixel from the surface */
    .flush        = NULL,   /* nothing is queued: writes land in s->pixels */
    .read_pixels  = NULL,   /* the frame already is s->pixels */
};

/* ---- the GPU rasterizer as a backend -----------------------------------
 * Last part of the translation unit on purpose: it uses the CPU entries above
 * as its own per-primitive fallback (a primitive it has not implemented yet
 * runs on the CPU with the frame synced across), so it has to see them. */
#include "gui_gl_backend.c"

/* Which rasterizer this *application* draws with: 0 = software, 1 = GPU,
 * 2 = GPU when this machine can provide it (Auto). Returns what is actually
 * installed (0 or 1), so a caller that asked for the GPU and got 0 knows the
 * frame is being rasterized on the CPU -- there is deliberately no environment
 * variable for this: the choice belongs to the program (ZanIDE persists it in
 * its own profile), not to the machine every Zan program shares.
 *
 * Existing surfaces are moved over too: a GPU target starts from what the
 * surface already holds, so switching mid-session does not blank the window. */
EXPORT i32 zan_gui_set_render_backend(i32 mode) {
    const zan_gui_backend *be = NULL;
    if (mode == 1 || mode == 2) {
        if (zan_gui_internal_gl_install()) be = &zan_gl_backend;
    } else {
        zan_gui_internal_set_backend(NULL);
        /* Going back to the CPU: the GL child windows have to go, or they would
         * sit on top of the shell's client area showing the last GPU frame
         * forever while the shell blits its bitmap underneath them. */
        zan_gui_internal_gl_drop_present();
    }
    for (int i = 0; i < g_surface_count; i++) {
        zan_surface_t *s = g_surfaces[i];
        if (!s || s->be == be) continue;
        /* Leaving a backend: settle its frame into the surface first. */
        if (s->be && s->be->flush) s->be->flush(s);
        if (s->be && s->be->read_pixels) s->be->read_pixels(s);
        s->be = be;
    }
    return be ? 1 : 0;
}

/* Name of the rasterizer in use ("cpu" / "gl"), for diagnostics and for a
 * settings UI to show what it actually got. */
EXPORT const char *zan_gui_render_backend(void) {
    return zan_gui_internal_backend_name();
}
