
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(__ANDROID__) || defined(ZAN_GUI_OHOS)
#include <malloc.h>
#endif
#include <math.h>
#include <stdio.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <rpc.h>
#include <shellscalingapi.h>
#include <dwmapi.h>
#include <imm.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "imm32.lib")
#if defined(ZAN_GUI_STATIC)
#define EXPORT
#else
#define EXPORT __declspec(dllexport)
#endif
#elif defined(__linux__) || defined(__wasm__)
#if !defined(ZAN_GUI_OHOS) && !defined(__ANDROID__) && !defined(__wasm__)
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
#include <dirent.h>
#ifdef ZAN_GUI_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/ftcolor.h>
#include <freetype/ftbitmap.h>
#if !defined(__ANDROID__) && !defined(ZAN_GUI_OHOS) && !defined(__wasm__)
#include <fontconfig/fontconfig.h>
#endif
#endif
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT __attribute__((visibility("default")))
#endif

#include "../../common/host_oom.h"
#if !defined(__wasm__)
#include "rt_crash.h"
#endif
#include "gui_backend.h"
#define ZAN_GUI_GRAPHICS_API EXPORT
#include "zan_gui_graphics.h"
typedef int32_t i32;
typedef int64_t i64;
typedef intptr_t iptr;
typedef uint32_t u32;
typedef uint8_t  u8;


typedef struct zan_surface_s {
    u32 *pixels;
    int width;
    int height;
    int stride;
    const zan_gui_backend *be;
    int id;
    int clip_x0;
    int clip_y0;
    int clip_x1;
    int clip_y1;
    int clip_stack[256];
    int clip_depth;
    int clip_overflow;
    int painted;
} zan_surface_t;

static zan_surface_t *g_surfaces[64];
static int g_surface_count = 0;

static const zan_gui_backend *g_backend = NULL;

static const zan_gui_backend *zan_impl_pick(zan_surface_t *s, int has_entry) {
    if (has_entry) {
        if (s->be->sync_from_cpu) s->be->sync_from_cpu(s);
        return s->be;
    }
    if (s->be && s->be->sync_to_cpu) s->be->sync_to_cpu(s);
    return &zan_cpu_backend;
}

#define ZAN_IMPL(s, entry)                                                \
    zan_impl_pick((s), (s)->be != NULL && (s)->be->entry != NULL)

#define ZAN_BE(s, entry, ...)                                             \
    do {                                                                  \
        if ((s)->be && (s)->be->entry) {                                  \
            (s)->be->entry(__VA_ARGS__);                                  \
            return;                                                       \
        }                                                                 \
    } while (0)

void zan_gui_internal_set_backend(const zan_gui_backend *be) {
    g_backend = be;
}

const char *zan_gui_internal_backend_name(void) {
    return (g_backend && g_backend->name) ? g_backend->name
                                          : zan_cpu_backend.name;
}
static i64 g_last_surface = -1;
static u32 g_bg_color = 0xFFFFFFFF;

static void zan_cache_drop_surface(int sid);

static int clamp_i(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static u32 pack_argb(int r, int g, int b, int a) {
    return ((u32)a << 24) | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

/* 像素 Alpha 混合：SrcOver 算法实现 */
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
    u32 da2 = da * inv_sa / 255;
    u32 oa = sa + da2;
    if (oa == 0) return 0;
    u32 or_ = (sr * sa + dr * da2) / oa;
    u32 og = (sg * sa + dg * da2) / oa;
    u32 ob = (sb * sa + db * da2) / oa;
    return (oa << 24) | (or_ << 16) | (og << 8) | ob;
}

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define ZAN_HAS_SSE2 1
#else
#define ZAN_HAS_SSE2 0
#endif

#if defined(__aarch64__) || (defined(_M_ARM64) && !defined(_M_ARM64EC))
#include <arm_neon.h>
#define ZAN_HAS_NEON 1
#else
#define ZAN_HAS_NEON 0
#endif

static long long g_blend_px_simd, g_blend_px_scalar;

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
#if ZAN_HAS_NEON
    if (n >= 4) {
        uint8x16_t ff8 = vdupq_n_u8(0xFF);
        uint32x4_t amask = vdupq_n_u32(0xFF000000u);
        uint16x8_t invv = vdupq_n_u16((uint16_t)inv);
        uint16x8_t one = vdupq_n_u16(1);
        uint16x8_t sav = vdupq_n_u16((uint16_t)sa);
        uint8x16_t sbytes = vreinterpretq_u8_u32(vdupq_n_u32(src));
        uint16x8_t sLo = vmulq_u16(vshll_n_u8(vget_low_u8(sbytes), 0), sav);
        uint16x8_t sHi = vmulq_u16(vshll_n_u8(vget_high_u8(sbytes), 0), sav);
        for (; i + 4 <= n; i += 4) {
            uint8x16_t d = vld1q_u8((const uint8_t *)(row + i));
            uint8x16_t da = vandq_u8(d, vreinterpretq_u8_u32(amask));
            if (vminvq_u32(vreinterpretq_u32_u8(vceqq_u8(da, vreinterpretq_u8_u32(amask))))
                    != 0xFFFFFFFFu) { break; }
            g_blend_px_simd += 4;
            uint16x8_t lo = vaddq_u16(vmulq_u16(vshll_n_u8(vget_low_u8(d), 0), invv), sLo);
            uint16x8_t hi = vaddq_u16(vmulq_u16(vshll_n_u8(vget_high_u8(d), 0), invv), sHi);
            lo = vshrq_n_u16(vaddq_u16(vaddq_u16(lo, one), vshrq_n_u16(lo, 8)), 8);
            hi = vshrq_n_u16(vaddq_u16(vaddq_u16(hi, one), vshrq_n_u16(hi, 8)), 8);
            uint8x16_t out = vorrq_u8(
                vcombine_u8(vqmovn_u16(lo), vqmovn_u16(hi)),
                vreinterpretq_u8_u32(amask));
            vst1q_u8((uint8_t *)(row + i), out);
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
    s->clip_overflow = 0;
    clip_notify(s);
}

static int clip_hits(zan_surface_t *s, int x0, int y0, int x1, int y1) {
    if (x1 <= s->clip_x0 || x0 >= s->clip_x1) return 0;
    if (y1 <= s->clip_y0 || y0 >= s->clip_y1) return 0;
    return 1;
}

static int clip_covers(zan_surface_t *s, int x0, int y0, int x1, int y1) {
    return x0 >= s->clip_x0 && y0 >= s->clip_y0
        && x1 <= s->clip_x1 && y1 <= s->clip_y1;
}

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

static void set_pixel_aa(zan_surface_t *s, int x, int y, u32 color, int coverage) {
    if (clipped_out(s, x, y)) return;
    if (coverage <= 0) return;
    if (coverage > 255) coverage = 255;
    u32 a = ((color >> 24) & 0xFF) * (u32)coverage / 255;
    u32 c = (a << 24) | (color & 0x00FFFFFF);
    int idx = y * s->stride + x;
    s->pixels[idx] = blend_over(s->pixels[idx], c);
}

/* Convert a 0 */
static inline int aa_coverage(double cov) {
    if (cov <= 0.0) return 0;
    if (cov >= 1.0) return 255;
    return (int)(cov * 255.0);
}


EXPORT i32 zan_gui_guard_call(iptr fn, iptr arg) {
    void (*body)(void *) = (void (*)(void *))fn;
    if (!body) return 1;
#if defined(_WIN32)
    return (i32)zan__guard_call(body, (void *)arg);
#else
    body((void *)arg);
    return 1;
#endif
}

#if defined(_WIN32)
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

EXPORT iptr zan_gui_guard_wndproc(iptr proc) {
#if defined(_WIN32)
    zan_gui_zan_wndproc = (zan_gui_wndproc_fn)proc;
    return (iptr)(void *)&zan_gui_wndproc;
#else
    return proc;
#endif
}

EXPORT i32 zan_gui_guard_recovered(void) {
#if defined(_WIN32)
    return (i32)zan__guard_recovered_count();
#else
    return 0;
#endif
}


EXPORT i32 zan_gui_create_surface(i32 width, i32 height) {
#if !defined(__wasm__)
    zan__crash_install();
#endif
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return -1;
    int64_t px = (int64_t)width * (int64_t)height;
    if (px <= 0 || px > 16384LL * 16384LL) return -1;
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
    fill_run_const(s->pixels, (int)px, g_bg_color);
    g_surfaces[id] = s;
    return (i64)id;
}

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
    zan_cache_drop_surface(id);
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

EXPORT i32 zan_gui_write_pixels(i32 surface_id, const char *path,
                                i32 x, i32 y, i32 width, i32 height) {
    if (surface_id < 0 || surface_id >= g_surface_count
        || !g_surfaces[surface_id] || !path || path[0] == '\0') return -1;
    zan_surface_t *s = g_surfaces[surface_id];
    if (s->be) {
        if (s->be->flush) s->be->flush(s);
        if (s->be->read_pixels) s->be->read_pixels(s);
    }
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

u32 *zan_gui_internal_surface_data(i64 id, int *w, int *h, int *stride) {
    if (id < 0 || id >= g_surface_count || !g_surfaces[id]) return NULL;
    zan_surface_t *s = g_surfaces[id];
    if (s->be) {
        if (s->be->flush) s->be->flush(s);
        if (s->be->read_pixels) s->be->read_pixels(s);
    }
    if (w) *w = s->width;
    if (h) *h = s->height;
    if (stride) *stride = s->stride;
    return s->pixels;
}

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

EXPORT void zan_gui_push_clip(i32 surface_id, i32 x, i32 y, i32 w, i32 h) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    int cap = (int)(sizeof(s->clip_stack) / sizeof(s->clip_stack[0]));
    if (s->clip_depth * 4 + 4 <= cap && s->clip_overflow == 0) {
        s->clip_stack[s->clip_depth * 4 + 0] = s->clip_x0;
        s->clip_stack[s->clip_depth * 4 + 1] = s->clip_y0;
        s->clip_stack[s->clip_depth * 4 + 2] = s->clip_x1;
        s->clip_stack[s->clip_depth * 4 + 3] = s->clip_y1;
        s->clip_depth++;
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
    } else {
        s->clip_overflow++;
    }
}

EXPORT void zan_gui_pop_clip(i32 surface_id) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    if (s->clip_overflow > 0) {
        s->clip_overflow--;
        return;
    }
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

EXPORT void zan_gui_reset_clip(i32 surface_id) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    clip_reset_full(s);
}

typedef struct { long long calls, px; } zan_stat_t;
static zan_stat_t g_st_fill_op, g_st_fill_blend, g_st_grad, g_st_round,
                  g_st_radial, g_st_blur, g_st_blur_hit, g_st_img,
                  g_st_snap, g_st_restore, g_st_glyph;
#define ZAN_STAT(s, n) do { (s).calls++; (s).px += (long long)(n); } while (0)

EXPORT void zan_gui_stat_glyphs(i32 n) { ZAN_STAT(g_st_glyph, n); }

#define ZAN_TOP_N 6
#define ZAN_TOP_T 2
static int g_top_a[ZAN_TOP_T][ZAN_TOP_N], g_top_x[ZAN_TOP_T][ZAN_TOP_N],
           g_top_y[ZAN_TOP_T][ZAN_TOP_N], g_top_w[ZAN_TOP_T][ZAN_TOP_N],
           g_top_h[ZAN_TOP_T][ZAN_TOP_N], g_top_c[ZAN_TOP_T][ZAN_TOP_N];

static void stat_top_rect(int t, int area, int x, int y, int w, int h,
                          u32 color) {
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

EXPORT i32 zan_gui_stat_top(i32 rank, i32 field) {
    return stat_top_field(0, rank, field);
}

EXPORT i32 zan_gui_stat_top_fill(i32 rank, i32 field) {
    return stat_top_field(1, rank, field);
}

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
    if (kind == 2) { return (i32)(t->px / 1000); }
    i32 kpx = (i32)(t->px / 1000);
    t->calls = 0;
    t->px = 0;
    return kpx;
}

#include "gui_runtime_raster.c"
#include "gui_runtime_present.c"
#include "gui_runtime_draw.c"

EXPORT void *zan_gui_get_pixels(i32 surface_id) {
    if (surface_id < 0 || surface_id >= g_surface_count || !g_surfaces[surface_id]) return NULL;
    zan_surface_t *s = g_surfaces[surface_id];
    if (s->be) {
        if (s->be->flush) s->be->flush(s);
        if (s->be->read_pixels) s->be->read_pixels(s);
    }
    return (void *)s->pixels;
}

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

EXPORT i32 zan_gui_present_window(i32 surface_id, void *native_window) {
    if (surface_id < 0 || surface_id >= g_surface_count ||
        !g_surfaces[surface_id]) return 0;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s->be || !s->be->present) return 0;
    return s->be->present(s, native_window) ? 1 : 0;
}

EXPORT void zan_gui_release_window(void *native_window) {
    if (g_backend && g_backend->drop_window)
        g_backend->drop_window(native_window);
}

static void cpu_blit_img(zan_surface_t *s, const zan_bitmap *img, int tint,
                         int dx, int dy, int dw, int dh,
                         int sx, int sy, int sw, int sh)
{
    int isx, isy, isw, ish, idx2, idy, idw, idh, x0, y0, x1, y1, py, px;
    if (!img) return;
    isx = sx; isy = sy; isw = sw; ish = sh;
    if (isw <= 0) { isx = 0; isw = img->width; }
    if (ish <= 0) { isy = 0; ish = img->height; }
    idx2 = dx; idy = dy; idw = dw; idh = dh;
    if (idw <= 0) idw = isw;
    if (idh <= 0) idh = ish;
    x0 = idx2 > s->clip_x0 ? idx2 : s->clip_x0;
    y0 = idy  > s->clip_y0 ? idy  : s->clip_y0;
    long long ex = (long long)idx2 + (long long)idw;
    long long ey = (long long)idy  + (long long)idh;
    x1 = ex < (long long)s->clip_x1 ? (int)ex : s->clip_x1;
    y1 = ey < (long long)s->clip_y1 ? (int)ey : s->clip_y1;
    ZAN_STAT(g_st_img, (long long)(x1 - x0) * (long long)(y1 - y0));
    if (idw <= 0 || idh <= 0 || x1 <= x0 || y1 <= y0) return;

    int boxX = idw < isw;
    int boxY = idh < ish;
    long long nDen = boxX ? idw : 2 * (long long)idw;
    long long t0 = boxX ? (long long)(x0 - idx2) * isw
                        : ((long long)(x0 - idx2) * 2 + 1) * isw;
    long long tStep = boxX ? isw : 2 * (long long)isw;
    int qx = (int)(t0 / nDen);
    int rx = (int)(t0 % nDen);
    int qxStep = (int)(tStep / nDen);
    int rxStep = (int)(tStep % nDen);
    int rDen = (int)nDen;
    int qx2 = (int)((((long long)(x0 - idx2) + 1) * isw + idw - 1) / idw);
    int rx2 = (int)((((long long)(x0 - idx2) + 1) * isw + idw - 1) % idw);
    for (py = y0; py < y1; py++) {
        int sy0, sy1;
        if (boxY) {
            sy0 = isy + (int)((long long)(py - idy) * ish / idh);
            sy1 = isy + (int)(((long long)(py - idy + 1) * ish + idh - 1) / idh);
            if (sy1 <= sy0) { sy1 = sy0 + 1; }
        } else {
            sy0 = isy + (int)(((long long)(py - idy) * 2 + 1) * ish / (2 * (long long)idh));
            sy1 = sy0 + 1;
        }
        if (sy0 < 0 || sy1 > img->height || sy0 >= sy1) continue;
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
            if (sx0 < 0 || sx1 > img->width || sx0 >= sx1) continue;
            u32 sp;
            if (sy1 - sy0 == 1 && sx1 - sx0 == 1) {
                sp = img->pixels[(size_t)sy0 * (size_t)img->stride + (size_t)sx0];
            } else {
                int sa = 0, sr = 0, sg = 0, sb = 0, n = 0;
                for (int yy = sy0; yy < sy1; yy++) {
                    const u32 *srow = img->pixels + (size_t)yy * (size_t)img->stride;
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
            if (tint != -1)
                sp = ((((sp >> 24) & 255) * ((tint >> 24) & 255) / 255) << 24)
                   | ((((sp >> 16) & 255) * ((tint >> 16) & 255) / 255) << 16)
                   | ((((sp >> 8) & 255) * ((tint >> 8) & 255) / 255) << 8)
                   | (((sp & 255) * (tint & 255)) / 255);
            u32 dp = drow[px];
            u32 da = sp >> 24;
            if (da == 0) continue;
            if (da == 0xFF) { drow[px] = sp; continue; }
            drow[px] = blend_over(dp, sp);
        }
    }
}

static void cpu_blit_pixels(zan_surface_t *s, const zan_bitmap *img,
                            int dx, int dy, int dw, int dh,
                            int sx, int sy, int sw, int sh) {
    cpu_blit_img(s, img, -1, dx, dy, dw, dh, sx, sy, sw, sh);
}

static void cpu_blit_pixels_batch(zan_surface_t *s, const zan_bitmap *img,
                                  const float *quads, int count) {
    for (int i = 0; i < count; i++) {
        const float *q = quads + (size_t)i * 10;
        u32 tint;
        memcpy(&tint, q + 8, sizeof(tint));
        cpu_blit_img(s, img, (int)tint, (int)q[0], (int)q[1], (int)q[2],
                     (int)q[3], (int)q[4], (int)q[5], (int)q[6], (int)q[7]);
    }
}

static int zan_bitmap_valid(const zan_bitmap *img) {
    return img && img->pixels && img->width > 0 && img->height > 0
        && img->width <= 32768 && img->height <= 32768
        && img->stride >= img->width;
}

EXPORT void zan_gui_blit_pixels(i32 surf_id, const zan_bitmap *img,
    i32 dx, i32 dy, i32 dw, i32 dh, i32 sx, i32 sy, i32 sw, i32 sh) {
    if (!zan_bitmap_valid(img) || surf_id < 0 || surf_id >= g_surface_count
        || !g_surfaces[surf_id]) return;
    zan_surface_t *s = g_surfaces[surf_id];
    ZAN_IMPL(s, blit_pixels)->blit_pixels(s, img, dx, dy, dw, dh, sx, sy, sw, sh);
}

EXPORT void zan_gui_blit_pixels_batch(i32 surf_id, const zan_bitmap *img,
    const float *quads, i32 count) {
    if (!zan_bitmap_valid(img) || !quads || count <= 0
        || surf_id < 0 || surf_id >= g_surface_count || !g_surfaces[surf_id]) return;
    zan_surface_t *s = g_surfaces[surf_id];
    ZAN_IMPL(s, blit_pixels_batch)->blit_pixels_batch(s, img, quads, count);
}

EXPORT const zan_bitmap *zan_gui_surface_bitmap(i32 surf_id) {
    static zan_bitmap views[64];
    if (surf_id < 0 || surf_id >= g_surface_count || !g_surfaces[surf_id]) return NULL;
    zan_surface_t *s = g_surfaces[surf_id];
    if (s->be && s->be->flush) s->be->flush(s);
    if (s->be && s->be->read_pixels) s->be->read_pixels(s);
    zan_bitmap *v = &views[surf_id];
    v->pixels = s->pixels;
    v->width = s->width;
    v->height = s->height;
    v->stride = s->stride;
    v->serial = 0;
    return v;
}

EXPORT void zan_gui_blit_surface_scaled(i32 dst_id, i32 src_id,
    i32 dx, i32 dy, i32 dw, i32 dh, i32 sx, i32 sy, i32 sw, i32 sh) {
    const zan_bitmap *src = zan_gui_surface_bitmap(src_id);
    if (!src) return;
    if (dst_id == src_id) {
        size_t bytes = (size_t)src->stride * (size_t)src->height * sizeof(u32);
        u32 *copy = (u32 *)malloc(bytes);
        if (!copy) return;
        memcpy(copy, src->pixels, bytes);
        zan_bitmap view = *src;
        view.pixels = copy;
        zan_gui_blit_pixels(dst_id, &view, dx, dy, dw, dh, sx, sy, sw, sh);
        free(copy);
    } else {
        zan_gui_blit_pixels(dst_id, src, dx, dy, dw, dh, sx, sy, sw, sh);
    }
}

#define ZAN_GUI_MAX_HIT_GUARDS 1024
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

static inline int zan_gui_in_hit_guard(iptr hwnd, int x, int y) {
    for (int i = 0; i < g_hit_guard_count; i++) {
        const int *g = g_hit_guards[i];
        if (g_hit_guard_win[i] != hwnd) continue;
        if (x >= g[0] && x < g[0] + g[2] && y >= g[1] && y < g[1] + g[3])
            return 1;
    }
    return 0;
}

#include "gui_runtime_glyph.c"
#include "gui_runtime_text.c"
#if defined(ZAN_GUI_OHOS)
#include "gui_runtime_ohos.c"
#elif defined(ZAN_GUI_ANDROID_NATIVE)
#include "gui_runtime_android_native.c"
#elif defined(ZAN_GUI_IOS)
#include "gui_runtime_ios.c"
#elif defined(__wasm__)
#include "gui_runtime_wasm.c"
#else
#include "gui_runtime_x11.c"
#endif
#include "gui_runtime_font.c"
#include "gui_runtime_tray.c"
#include "gui_runtime_shims.c"
#include "gui_runtime_android.c"

static void cpu_glyph_run(zan_surface_t *s, const zan_glyph_run *run) {
    u32 cr = (run->color >> 16) & 0xFF;
    u32 cg = (run->color >> 8) & 0xFF;
    u32 cb = run->color & 0xFF;
    u32 ca = (run->color >> 24) & 0xFF;
    if (ca == 0) ca = 255;
    for (int i = 0; i < run->count; i++) {
        const zan_glyph_tile *tile = run->items[i].tile;
        int ox = run->items[i].x, oy = run->items[i].y;
        int tw = tile->w, th = tile->h;
        int py0 = s->clip_y0 - oy; if (py0 < 0) py0 = 0;
        int py1 = s->clip_y1 - oy; if (py1 > th) py1 = th;
        int px0 = s->clip_x0 - ox; if (px0 < 0) px0 = 0;
        int px1 = s->clip_x1 - ox; if (px1 > tw) px1 = tw;
        if (tile->flags & ZAN_TILE_RGBA) {
            const u32 *cov = (const u32 *)tile->cov;
            for (int py = py0; py < py1; py++) {
                const u32 *srow = cov + (size_t)py * (size_t)tw;
                int dst_row = (oy + py) * s->stride + ox;
                for (int px = px0; px < px1; px++) {
                    u32 sp = srow[px];
                    u32 a = (sp >> 24) & 0xFF;
                    if (!a) continue;
                    u32 sb = sp & 0xFF;
                    u32 sg = (sp >> 8) & 0xFF;
                    u32 sr = (sp >> 16) & 0xFF;
                    int idx = dst_row + px;
                    u32 dp = s->pixels[idx];
                    u32 dr = (dp >> 16) & 0xFF;
                    u32 dg = (dp >> 8) & 0xFF;
                    u32 db = dp & 0xFF;
                    u32 or_ = (sr * a + dr * (255 - a)) / 255;
                    u32 og = (sg * a + dg * (255 - a)) / 255;
                    u32 ob = (sb * a + db * (255 - a)) / 255;
                    s->pixels[idx] = (255u << 24) | (or_ << 16)
                                   | (og << 8) | ob;
                }
            }
            continue;
        }
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
    .surface_round = cpu_surface_round,
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
    .draw_text    = NULL,
    .glyph_run    = cpu_glyph_run,
    .blit_pixels  = cpu_blit_pixels,
    .blit_pixels_batch = cpu_blit_pixels_batch,
    .set_clip     = NULL,
    .flush        = NULL,
    .read_pixels  = NULL,
};

#include "gui_gl_backend.c"
#include "gui_metal_backend.h"

EXPORT i32 zan_gui_set_render_backend(i32 mode) {
    const zan_gui_backend *be = NULL;
    if (mode == 1 || mode == 2) {
#if defined(__APPLE__) && !defined(ZAN_GUI_IOS)
        if (zan_gui_internal_metal_supported() && zan_gui_internal_metal_install()) {
            be = &zan_metal_backend;
        } else if (zan_gui_internal_gl_install()) {
            be = &zan_gl_backend;
        }
#else
        if (zan_gui_internal_gl_install()) be = &zan_gl_backend;
#endif
    } else {
        zan_gui_internal_set_backend(NULL);
    }
    for (int i = 0; i < g_surface_count; i++) {
        zan_surface_t *s = g_surfaces[i];
        if (!s || s->be == be) continue;
        if (s->be && s->be->flush) s->be->flush(s);
        if (s->be && s->be->read_pixels) s->be->read_pixels(s);
        s->be = be;
    }
    if (!be) {
        zan_gui_internal_gl_drop_present();
#if defined(__APPLE__) && !defined(ZAN_GUI_IOS)
        zan_gui_internal_metal_drop_present();
#endif
    }
    return be ? 1 : 0;
}

EXPORT const char *zan_gui_render_backend(void) {
    return zan_gui_internal_backend_name();
}

static inline size_t zan_gui_upload_last_bytes(void) { return 0; }
static inline int zan_gui_upload_last_full(void) { return 0; }

EXPORT const char *zan_gui_mem_report(void) {
    static char *rep = NULL;
    size_t surf_bytes = 0;
    size_t blur_bytes = 0, snap_bytes = 0;
    int i, blur_cnt = 0, snap_cnt = 0, atlas_live = 0;
    for (i = 0; i < g_surface_count; i++) {
        if (g_surfaces[i])
            surf_bytes += (size_t)g_surfaces[i]->stride
                          * (size_t)g_surfaces[i]->height * sizeof(u32);
    }
    for (i = 0; i < ZAN_ATLAS_CAP; i++)
        if (g_atlas[i].tile.cov) atlas_live++;
    for (i = 0; i < ZAN_BLUR_CACHE_SLOTS; i++) {
        if (g_blur_cache[i].pixels) {
            blur_bytes += g_blur_cache[i].cap * sizeof(u32);
            blur_cnt++;
        }
    }
    for (i = 0; i < g_snap_cache_count; i++) {
        if (g_snap_cache[i].pixels) {
            snap_bytes += g_snap_cache[i].cap * sizeof(u32);
            snap_cnt++;
        }
    }
    char buf[384];
#if defined(__ANDROID__) || defined(ZAN_GUI_OHOS)
    struct mallinfo mi = mallinfo();
#endif
#ifdef __ANDROID__
    snprintf(buf, sizeof(buf),
             "atl %.1fM/%d cov %.2fM "
             "surf %.1fM blur %d/%.1fM snap %d/%.1fM "
             "heap %.1f/%.1fM "
             "up %.1fM%s",
             (double)g_atlas_bytes / 1048576.0, atlas_live,
             (double)g_cov_pool_bytes / 1048576.0,
             (double)surf_bytes / 1048576.0,
             blur_cnt, (double)blur_bytes / 1048576.0,
             snap_cnt, (double)snap_bytes / 1048576.0,
             (double)mi.uordblks / 1048576.0,
             (double)mi.fordblks / 1048576.0,
             (double)zan_gui_upload_last_bytes() / 1048576.0,
             zan_gui_upload_last_full() ? " full" : " dirty");
#elif defined(ZAN_GUI_OHOS)
    snprintf(buf, sizeof(buf),
             "atl %.1fM/%d cov %.2fM "
             "surf %.1fM blur %d/%.1fM snap %d/%.1fM "
             "heap %.1f/%.1fM",
             (double)g_atlas_bytes / 1048576.0, atlas_live,
             (double)g_cov_pool_bytes / 1048576.0,
             (double)surf_bytes / 1048576.0,
             blur_cnt, (double)blur_bytes / 1048576.0,
             snap_cnt, (double)snap_bytes / 1048576.0,
             (double)mi.uordblks / 1048576.0,
             (double)mi.fordblks / 1048576.0);
#else
    snprintf(buf, sizeof(buf),
             "atl %.1fM/%d cov %.2fM "
             "surf %.1fM blur %d/%.1fM snap %d/%.1fM",
             (double)g_atlas_bytes / 1048576.0, atlas_live,
             (double)g_cov_pool_bytes / 1048576.0,
             (double)surf_bytes / 1048576.0,
             blur_cnt, (double)blur_bytes / 1048576.0,
             snap_cnt, (double)snap_bytes / 1048576.0);
#endif
    size_t n = strlen(buf);
    char *nb = (char *)malloc(n + 1);
    if (!nb) return "";
    memcpy(nb, buf, n + 1);
    free(rep);
    rep = nb;
    return rep;
}
