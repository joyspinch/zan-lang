/* 内部辅助实现 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(__ANDROID__) || defined(ZAN_GUI_OHOS)
#include <malloc.h> /* 底层系统交互与数据协议契约 */
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
/* 内部辅助实现 */
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "imm32.lib")
/* 内部辅助实现 */
#if defined(ZAN_GUI_STATIC)
#define EXPORT
#else
#define EXPORT __declspec(dllexport)
#endif
#elif defined(__linux__) || defined(__wasm__)
/* 底层系统交互与数据协议契约 */
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

/* 内部辅助逻辑 */
#include "../common/host_oom.h"
/* 模块核心语义抽象与接口调用契约 */
#if !defined(__wasm__)
#include "rt_crash.h"
#endif
#include "gui_backend.h"
#define ZAN_GUI_GRAPHICS_API EXPORT
#include "zan_gui_graphics.h"
typedef int32_t i32;
typedef int64_t i64;
/* 内部辅助实现 */
typedef intptr_t iptr;
typedef uint32_t u32;
typedef uint8_t  u8;

/* 核心系统底层抽象与内存语义契约 */

typedef struct zan_surface_s {
    u32 *pixels;
    int width;
    int height;
    int stride;
    /* 内部辅助逻辑 */
    const zan_gui_backend *be;
    /* 内部辅助实现 */
    int id;
    /* 内部辅助实现 */
    int clip_x0;
    int clip_y0;
    int clip_x1;
    int clip_y1;
    int clip_stack[256];
    int clip_depth;
    int clip_overflow;
    /* 内部辅助实现 */
    int painted;
} zan_surface_t;

static zan_surface_t *g_surfaces[64];
static int g_surface_count = 0;

/* 底层系统交互与数据协议契约 */
static const zan_gui_backend *g_backend = NULL;

/* 内部辅助逻辑 */
static const zan_gui_backend *zan_impl_pick(zan_surface_t *s, int has_entry) {
    if (has_entry) {
        /* 内部辅助实现 */
        if (s->be->sync_from_cpu) s->be->sync_from_cpu(s);
        return s->be;
    }
    /* 内部辅助实现 */
    if (s->be && s->be->sync_to_cpu) s->be->sync_to_cpu(s);
    return &zan_cpu_backend;
}

#define ZAN_IMPL(s, entry)                                                \
    zan_impl_pick((s), (s)->be != NULL && (s)->be->entry != NULL)

/* 内部辅助实现 */
#define ZAN_BE(s, entry, ...)                                             \
    do {                                                                  \
        if ((s)->be && (s)->be->entry) {                                  \
            (s)->be->entry(__VA_ARGS__);                                  \
            return;                                                       \
        }                                                                 \
    } while (0)

/* 内部辅助实现 */
void zan_gui_internal_set_backend(const zan_gui_backend *be) {
    g_backend = be;
}

const char *zan_gui_internal_backend_name(void) {
    return (g_backend && g_backend->name) ? g_backend->name
                                          : zan_cpu_backend.name;
}
/* 内部辅助实现 */
static i64 g_last_surface = -1;
static u32 g_bg_color = 0xFFFFFFFF;

/* 内部辅助逻辑 */
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
    /* 底层系统交互与数据协议契约 */
    u32 da2 = da * inv_sa / 255;
    u32 oa = sa + da2;
    if (oa == 0) return 0;
    u32 or_ = (sr * sa + dr * da2) / oa;
    u32 og = (sg * sa + dg * da2) / oa;
    u32 ob = (sb * sa + db * da2) / oa;
    return (oa << 24) | (or_ << 16) | (og << 8) | ob;
}

/* 内部辅助实现 */
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define ZAN_HAS_SSE2 1
#else
#define ZAN_HAS_SSE2 0
#endif

/* 内部辅助实现 */
#if defined(__aarch64__) || (defined(_M_ARM64) && !defined(_M_ARM64EC))
#include <arm_neon.h>
#define ZAN_HAS_NEON 1
#else
#define ZAN_HAS_NEON 0
#endif

/* 模块核心语义抽象与接口调用契约 */
/* 底层系统交互与数据协议契约 */
static long long g_blend_px_simd, g_blend_px_scalar;

/* 核心系统底层抽象与内存语义契约 */
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
    /* 模块核心语义抽象与接口调用契约 */
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
    /* 内部辅助实现 */
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

/* 模块核心语义抽象与接口调用契约 */
/* 内部辅助实现 */
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

/* 内部辅助逻辑 */
static int clip_hits(zan_surface_t *s, int x0, int y0, int x1, int y1) {
    if (x1 <= s->clip_x0 || x0 >= s->clip_x1) return 0;
    if (y1 <= s->clip_y0 || y0 >= s->clip_y1) return 0;
    return 1;
}

/* 模块核心语义抽象与接口调用契约 */
static int clip_covers(zan_surface_t *s, int x0, int y0, int x1, int y1) {
    return x0 >= s->clip_x0 && y0 >= s->clip_y0
        && x1 <= s->clip_x1 && y1 <= s->clip_y1;
}

/* 模块核心语义抽象与接口调用契约 */
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

/* 模块核心语义抽象与接口调用契约 */
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

/* 核心系统底层抽象与内存语义契约 */

/* 内部辅助实现 */
EXPORT i32 zan_gui_guard_call(iptr fn, iptr arg) {
    void (*body)(void *) = (void (*)(void *))fn;
    if (!body) return 1;
#if defined(_WIN32)
    return (i32)zan__guard_call(body, (void *)arg);
#else
    /* 内部辅助实现 */
    body((void *)arg);
    return 1;
#endif
}

#if defined(_WIN32)
/* 核心系统底层抽象与内存语义契约 */
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

/* 内部辅助实现 */
EXPORT iptr zan_gui_guard_wndproc(iptr proc) {
#if defined(_WIN32)
    zan_gui_zan_wndproc = (zan_gui_wndproc_fn)proc;
    return (iptr)(void *)&zan_gui_wndproc;
#else
    return proc;
#endif
}

/* 模块核心语义抽象与接口调用契约 */
EXPORT i32 zan_gui_guard_recovered(void) {
#if defined(_WIN32)
    return (i32)zan__guard_recovered_count();
#else
    return 0;
#endif
}

/* 核心系统底层抽象与内存语义契约 */

EXPORT i32 zan_gui_create_surface(i32 width, i32 height) {
#if !defined(__wasm__)
    zan__crash_install(); /* 底层系统交互与数据协议契约 */
#endif
    /* 内部辅助实现 */
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return -1;
    int64_t px = (int64_t)width * (int64_t)height;
    if (px <= 0 || px > 16384LL * 16384LL) return -1;
    /* 内部辅助实现 */
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
    /* 内部辅助实现 */
    fill_run_const(s->pixels, (int)px, g_bg_color);
    g_surfaces[id] = s;
    return (i64)id;
}

/* 模块核心语义抽象与接口调用契约 */
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
    /* 内部辅助实现 */
    zan_cache_drop_surface(id);
    /* 内部辅助逻辑 */
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

/* 内部辅助逻辑 */
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

/* 内部辅助实现 */
u32 *zan_gui_internal_surface_data(i64 id, int *w, int *h, int *stride) {
    if (id < 0 || id >= g_surface_count || !g_surfaces[id]) return NULL;
    zan_surface_t *s = g_surfaces[id];
    /* 内部辅助实现 */
    if (s->be) {
        if (s->be->flush) s->be->flush(s);
        if (s->be->read_pixels) s->be->read_pixels(s);
    }
    if (w) *w = s->width;
    if (h) *h = s->height;
    if (stride) *stride = s->stride;
    return s->pixels;
}

/* 内部辅助实现 */
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
    /* 内部实现与并发/内存约束规范 */
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

/* 内部辅助逻辑 */
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

/* 内部实现与并发/内存约束规范 */
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

/* 模块核心语义抽象与接口调用契约 */
EXPORT void zan_gui_reset_clip(i32 surface_id) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s) return;
    clip_reset_full(s);
}

/* 内部辅助实现 */
typedef struct { long long calls, px; } zan_stat_t;
static zan_stat_t g_st_fill_op, g_st_fill_blend, g_st_grad, g_st_round,
                  g_st_radial, g_st_blur, g_st_blur_hit, g_st_img,
                  g_st_snap, g_st_restore, g_st_glyph;
#define ZAN_STAT(s, n) do { (s).calls++; (s).px += (long long)(n); } while (0)

EXPORT void zan_gui_stat_glyphs(i32 n) { ZAN_STAT(g_st_glyph, n); }

/* 内部辅助实现 */
#define ZAN_TOP_N 6
/* 内部辅助实现 */
#define ZAN_TOP_T 2
static int g_top_a[ZAN_TOP_T][ZAN_TOP_N], g_top_x[ZAN_TOP_T][ZAN_TOP_N],
           g_top_y[ZAN_TOP_T][ZAN_TOP_N], g_top_w[ZAN_TOP_T][ZAN_TOP_N],
           g_top_h[ZAN_TOP_T][ZAN_TOP_N], g_top_c[ZAN_TOP_T][ZAN_TOP_N];

static void stat_top_rect(int t, int area, int x, int y, int w, int h,
                          u32 color) {
    /* 内部辅助实现 */
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

/* 底层系统交互与数据协议契约 */
EXPORT i32 zan_gui_stat_top(i32 rank, i32 field) {
    return stat_top_field(0, rank, field);
}

/* 模块核心语义抽象与接口调用契约 */
EXPORT i32 zan_gui_stat_top_fill(i32 rank, i32 field) {
    return stat_top_field(1, rank, field);
}

/* 内部辅助实现 */
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
    /* 内部辅助实现 */
    if (kind == 2) { return (i32)(t->px / 1000); }
    i32 kpx = (i32)(t->px / 1000);
    t->calls = 0;
    t->px = 0;
    return kpx;
}

/* 内部辅助逻辑 */
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
        /* 内部辅助实现 */
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

/* 内部辅助逻辑 */
static int zan_round_cov(int i, int j, int rw, int rh, int cr, int cmask);

/* 内部辅助实现 */
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

/* 底层系统交互与数据协议契约 */
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

/* 内部辅助实现 */
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
            /* 内部辅助逻辑 */
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

/* 抗锯齿圆角矩形掩模采样与混合 */
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
    double cov = (double)cr - dist + 0.5;           /* 核心系统底层抽象与内存语义契约 */
    if (cov >= 1.0) return 255;
    if (cov <= 0.0) return 0;
    return (int)(cov * 255.0);
}

/* 内部辅助逻辑 */
/* 内部辅助实现 */
static void blur_put(zan_surface_t *s, int px, int py, u32 c, int cov) {
    if (cov <= 0 || clipped_out(s, px, py)) return;
    if (cov >= 255) { s->pixels[py * s->stride + px] = c; return; }
    set_pixel_aa(s, px, py, c, cov);
}

/* 内部辅助实现 */
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

    /* 核心系统底层抽象与内存语义契约 */
    ZAN_STAT(g_st_blur, (long long)rw * (long long)rh);
    int ds = 1;
    if (r >= 12) { ds = 4; } else if (r >= 6) { ds = 2; }
    int dw = (rw + ds - 1) / ds;
    int dh = (rh + ds - 1) / ds;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    size_t dn = (size_t)dw * (size_t)dh;
    /* 内部辅助实现 */
    static u32 *scratch_a = NULL, *scratch_b = NULL;
    static size_t scratch_n = 0;
    if (dn > scratch_n) {
        u32 *na = (u32 *)realloc(scratch_a, dn * sizeof(u32));
        if (!na) return;                    /* 底层系统交互与数据协议契约 */
        scratch_a = na;
        u32 *nb = (u32 *)realloc(scratch_b, dn * sizeof(u32));
        if (!nb) return;
        scratch_b = nb;
        scratch_n = dn;
    }
    u32 *a = scratch_a;
    u32 *b = scratch_b;

    /* 双线性/块均值下采样缩放 */
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

    /* 内部辅助逻辑 */
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

    /* Vibrancy (luma-preserving ~1 */
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

    /* 内部辅助实现 */
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
        /* 内部辅助逻辑 */
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

/* 内部辅助实现 */
#define ZAN_BLUR_CACHE_SLOTS 256
typedef struct {
    int valid;
    int sid; 
/* 内部辅助实现 */
    int x0, y0, rw, rh, r;
    int cr, cmask; /* 模块核心语义抽象与接口调用契约 */
    u32 src_sum; /* 模块核心语义抽象与接口调用契约 */
    u32 *pixels;
    size_t cap;
} zan_blur_cache_t;

/* 内部辅助实现 */
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

/* 内部辅助实现 */
static long long g_blur_partial_miss;

/* 内部辅助实现 */
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
    /* 内部辅助实现 */
    if (!clip_hits(s, x0, y0, x1, y1)) return;
    int r = radius;
    if (r < 1) return;
    if (r > 40) r = 40;

    zan_blur_cache_t *c = &g_blur_cache[slot];
    size_t n = (size_t)rw * (size_t)rh;

    /* 内部辅助实现 */
    int geom_ok = c->valid && c->pixels && c->sid == s->id
        && c->x0 == x0 && c->y0 == y0 && c->rw == rw && c->rh == rh
        && c->r == r && c->cr == cr && c->cmask == cmask;
    int covers = clip_covers(s, x0, y0, x1, y1);
    u32 sum = geom_ok ? zan_src_sum(s, x0, y0, rw, rh) : 0;
    /* 内部辅助实现 */
    if (geom_ok && (!dirty || (covers && sum == c->src_sum))) {
        ZAN_STAT(g_st_blur_hit, (long long)rw * (long long)rh);
        if (covers) {
            /* 模块核心语义抽象与接口调用契约 */
            for (int j = 0; j < rh; j++)
                memcpy(s->pixels + (size_t)(y0 + j) * s->stride + x0,
                       c->pixels + (size_t)j * rw, (size_t)rw * sizeof(u32));
        } else {
            /* 模块核心语义抽象与接口调用契约 */
            for (int j = 0; j < rh; j++) {
                u32 *src = c->pixels + (size_t)j * rw;
                for (int i = 0; i < rw; i++)
                    blur_put(s, x0 + i, y0 + j, src[i], 255);
            }
        }
        return;
    }

    /* 内部辅助逻辑 */
    u32 fresh = geom_ok ? sum : zan_src_sum(s, x0, y0, rw, rh);
    if (c->cap < n) {
        u32 *np = (u32 *)realloc(c->pixels, n * sizeof(u32));
        if (np) { c->pixels = np; c->cap = n; }
    }
    zan_blur_rect_core(s, x, y, w, h, radius, cr, cmask,
                       c->cap >= n ? c->pixels : NULL);
    /* 内部辅助实现 */
    if (!covers) { g_blur_partial_miss++; }
    if (!covers || c->cap < n) { c->valid = 0; return; }
    c->x0 = x0; c->y0 = y0; c->rw = rw; c->rh = rh; c->r = r;
    c->cr = cr; c->cmask = cmask;
    c->src_sum = fresh;
    c->sid = s->id;
    c->valid = 1;
}

/* 内部辅助逻辑 */
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

/* 内部辅助实现 */
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

/* 内部辅助实现 */
#define ZAN_SNAP_CACHE_SLOTS 8
#define ZAN_SNAP_CACHE_MAX_SLOTS 512
static zan_blur_cache_t *g_snap_cache;
static int g_snap_cache_count;
/* 模块核心语义抽象与接口调用契约 */
#if defined(__ANDROID__)
#define ZAN_SNAP_CACHE_BYTES (16u * 1024u * 1024u)
#else
#define ZAN_SNAP_CACHE_BYTES 0
#endif
static size_t zan_snap_total_bytes(void) {
    size_t total = 0;
    for (int i = 0; i < g_snap_cache_count; i++) {
        if (g_snap_cache[i].pixels)
            total += g_snap_cache[i].cap * sizeof(u32);
    }
    return total;
}
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
#if defined(__ANDROID__)
        /* 内部辅助实现 */
        size_t keep = c->pixels ? c->cap * sizeof(u32) : 0;
        if (ZAN_SNAP_CACHE_BYTES
            && zan_snap_total_bytes() - keep + n * sizeof(u32)
               > (size_t)ZAN_SNAP_CACHE_BYTES) {
            c->valid = 0;
            return;
        }
#endif
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

/* 内部辅助实现 */
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
    /* 内部辅助实现 */
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

/* 局部视口快照区域还原恢复 */
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
    /* 内部辅助实现 */
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

/* 内部辅助实现 */
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
            /* 内部辅助逻辑 */
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

/* 模块核心语义抽象与接口调用契约 */
/* 内部辅助实现 */
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

/* 底层系统交互与数据协议契约 */
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
    if (n >= 8) {  /* 核心系统底层抽象与内存语义契约 */
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

/* 内部辅助实现 */
#define ZAN_CHUNK_H 128

static int gdi_upload_chunks(HDC dc, u32 *pix, BITMAPINFO *bmi,
                             int rx, int ry, int rw, int rh) {
    int calls = 0;
    int y = ry;
    int end = ry + rh;
    int w = bmi->bmiHeader.biWidth;
    /* 内部辅助逻辑 */
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

/* 模块核心语义抽象与接口调用契约 */
EXPORT i32 zan_gui_atomic_present(void *hwnd, i32 surface_id);

/* 内部辅助实现 */
#define ZAN_ATOMIC_AT 35
/* 内部辅助实现 */
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
    bmi.bmiHeader.biHeight = -h;  /* 底层系统交互与数据协议契约 */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC dc = GetDC((HWND)hwnd);
    if (!dc) return -1;
    int calls = 0;

    zan_present_shadow_t *sh = present_shadow(hwnd, w, h, 1);
    int force_full = (rect_count < 0) || !sh;
    {
        /* 内部辅助逻辑 */
        static int tilediff = -1;
        if (tilediff < 0) {
            const char *ev = getenv("ZAN_TILE_DIFF");
            tilediff = ev && *ev;
        }
        if (!tilediff && rect_count == 0) force_full = 1;
    }

    if (!force_full && rect_count == 0) {
        /* 底层系统交互与数据协议契约 */
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
            /* 内部辅助实现 */
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
            /* 内部辅助实现 */
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
            /* 内部辅助实现 */
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
        /* 内部辅助实现 */
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

/* 核心系统底层抽象与内存语义契约 */
/* 内部辅助实现 */
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
                /* 模块核心语义抽象与接口调用契约 */
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
#endif /* 模块核心语义抽象与接口调用契约 */

/* 帧缓冲区向物理屏幕原子呈现提交 */
EXPORT i32 zan_gui_atomic_present(void *hwnd, i32 surface_id) {
#ifdef _WIN32
    if (!hwnd || surface_id < 0 || surface_id >= g_surface_count) return 0;
    /* 内部辅助实现 */
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

/* 内部辅助逻辑 */
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

/* 内部辅助逻辑 */
EXPORT void zan_gui_surface_release(i32 surface_id, i32 slot) {
    (void)surface_id;
    zan_blur_cache_t *c = zan_snap_slot(slot);
    if (!c) return;
    free(c->pixels);
    c->pixels = NULL;
    c->cap = 0;
    c->valid = 0;
}

/* 模块核心语义抽象与接口调用契约 */
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

/* 内部辅助实现 */
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

    /* 内部辅助实现 */
    int xl = ix + r, xr = ix + iw - r;      /* 核心系统底层抽象与内存语义契约 */
    int yt = iy + r, yb = iy + ih - r;
    cpu_fill_rect(s, xl, iy, iw - 2*r, r, c);        /* 核心系统底层抽象与内存语义契约 */
    cpu_fill_rect(s, ix, yt, iw, ih - 2*r, c);      /* middle */
    cpu_fill_rect(s, xl, yb, iw - 2*r, r, c);        /* 核心系统底层抽象与内存语义契约 */

    /* 内部辅助实现 */
    double cc[4][2] = {
        {xl, yt},     /* TL, square px<xl, py<yt */
        {xr, yt},     /* TR, square px>=xr, py<yt */
        {xl, yb},     /* BL, square px<xl, py>=yb */
        {xr, yb}      /* BR, square px>=xr, py>=yb */
    };
    int zx[4] = {ix, xr, ix, xr};
    int zy[4] = {iy, iy, yb, yb};
    int bit[4] = {ZAN_CORNER_TL, ZAN_CORNER_TR, ZAN_CORNER_BL, ZAN_CORNER_BR};
    for (int ci = 0; ci < 4; ci++) {
        if (!(m & bit[ci])) {
            /* 底层系统交互与数据协议契约 */
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

/* 内部辅助逻辑 */
static u32 surface_round_color(u32 fill, u32 border, int outer, int inner) {
    uint64_t af = fill >> 24, ab = border >> 24;
    uint64_t bw = ab * (uint64_t)(outer - inner);
    uint64_t fw = af * ((uint64_t)outer * 255u - bw);
    bw *= 255u;
    uint64_t weight = fw + bw;
    if (!weight) return 0;
    u32 alpha = (u32)(weight / (255u * 255u));
    u32 rgb = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        uint64_t c = ((fill >> shift) & 255u) * fw
                   + ((border >> shift) & 255u) * bw;
        rgb |= (u32)(c / weight) << shift;
    }
    return (alpha << 24) | rgb;
}

static void cpu_surface_round(zan_surface_t *s, int x, int y, int w, int h,
                              int radius, int mask, u32 fill, u32 border,
                              int thickness) {
    if (w <= 0 || h <= 0) return;
    if (thickness <= 0 || (border >> 24) == 0) {
        cpu_fill_round(s, x, y, w, h, radius, mask, fill);
        return;
    }
    int r = clamp_i(radius, 0, (w < h ? w : h) / 2);
    /* 底层系统交互与数据协议契约 */
    int t = thickness < (w < h ? w : h) ? thickness : (w < h ? w : h);
    int iw = w - t - t, ih = h - t - t, ir = r > t ? r - t : 0;
    int x0 = clamp_i(x, s->clip_x0, s->clip_x1);
    int y0 = clamp_i(y, s->clip_y0, s->clip_y1);
    int x1 = (int)((int64_t)x + w < s->clip_x1 ? (int64_t)x + w : s->clip_x1);
    int y1 = (int)((int64_t)y + h < s->clip_y1 ? (int64_t)y + h : s->clip_y1);
    if (x1 <= x0 || y1 <= y0) return;
    ZAN_STAT(g_st_round, (long long)(x1-x0) * (y1-y0));
    for (int py = y0; py < y1; py++) {
        u32 *row = s->pixels + py * s->stride;
        for (int px = x0; px < x1; px++) {
            int i = px-x, j = py-y;
            int outer = zan_round_cov(i, j, w, h, r, mask);
            if (!outer) continue;
            int inner = 0;
            if (iw > 0 && ih > 0 && i >= t && j >= t && i-t < iw && j-t < ih)
                inner = zan_round_cov(i-t, j-t, iw, ih, ir, mask);
            if (inner > outer) inner = outer;
            u32 c = inner == outer ? ((fill & 0xFFFFFFu) |
                    ((u32)((fill >> 24) * outer / 255) << 24))
                    : surface_round_color(fill, border, outer, inner);
            row[px] = blend_over(row[px], c);
        }
    }
}

EXPORT void zan_gui_surface_rounded_rect_mask(i32 surface_id, i32 x, i32 y,
        i32 w, i32 h, i32 radius, i32 mask, i32 fill, i32 border, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || w <= 0 || h <= 0) return;
    ZAN_IMPL(s, surface_round)->surface_round(s, x, y, w, h, radius, mask,
                                             (u32)fill, (u32)border, thickness);
}

EXPORT void zan_gui_surface_rounded_rect(i32 surface_id, i32 x, i32 y,
        i32 w, i32 h, i32 radius, i32 fill, i32 border, i32 thickness) {
    zan_gui_surface_rounded_rect_mask(surface_id, x, y, w, h, radius, 15,
                                    fill, border, thickness);
}

/* 抗锯齿圆角矩形描边光栅化 */
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
    /* 底层系统交互与数据协议契约 */
    cpu_fill_rect(s, xl, iy, iw - 2*r, th, c);              /* top */
    cpu_fill_rect(s, xl, iy + ih - th, iw - 2*r, th, c);    /* bottom */
    cpu_fill_rect(s, ix, yt, th, ih - 2*r, c);             /* left */
    cpu_fill_rect(s, ix + iw - th, yt, th, ih - 2*r, c);   /* right */

    double cc[4][2] = {
        {xl, yt}, {xr, yt}, {xl, yb}, {xr, yb}
    };
    int zx[4] = {ix, xr, ix, xr};
    int zy[4] = {iy, iy, yb, yb};
    int bit[4] = {ZAN_CORNER_TL, ZAN_CORNER_TR, ZAN_CORNER_BL, ZAN_CORNER_BR};
    int hx[4] = {ix, xr, ix, xr};          /* 核心系统底层抽象与内存语义契约 */
    double rin = (double)r - (double)th;   /* 核心系统底层抽象与内存语义契约 */
    for (int ci = 0; ci < 4; ci++) {
        if (!(m & bit[ci])) {
            /* 底层系统交互与数据协议契约 */
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
                double outerCov = (double)r + 0.5 - dist;        /* 核心系统底层抽象与内存语义契约 */
                double innerCov = dist - (rin - 0.5);            /* 核心系统底层抽象与内存语义契约 */
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

/* 模块核心语义抽象与接口调用契约 */
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

    /* 内部辅助实现 */
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

    /* 内部辅助实现 */
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
            double d = outside + inside - (double)r;   /* 核心系统底层抽象与内存语义契约 */
            double t = (d + half) * inv;
            if (t <= 0.0) { set_pixel(s, px, py, c); continue; }
            if (t >= 1.0) continue;
            double cov = 1.0 - t * t * (3.0 - 2.0 * t);   /* 核心系统底层抽象与内存语义契约 */
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

/* 抗锯齿填充圆光栅化（亚像素采样） */
static void cpu_fill_circle(zan_surface_t *s, int cx, int cy, int radius,
                            u32 c) {
    int r = radius;
    double rr = (double)r;
    double rin = rr - 0.5, rout = rr + 0.5;
    double rin2 = rin * rin, rout2 = rout * rout;

    /* 底层系统交互与数据协议契约 */
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
        /* 底层系统交互与数据协议契约 */
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

/* 内部辅助实现 */
static void cpu_draw_circle(zan_surface_t *s, int cx, int cy, int radius,
                            u32 c, int thickness) {
    int r = radius;
    if (r <= 0) return;
    double half = (double)(thickness > 0 ? thickness : 1) / 2.0;
    int ext = r + (int)half + 2;

    /* 底层系统交互与数据协议契约 */
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

/* 模块核心语义抽象与接口调用契约 */
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
    /* 内部辅助实现 */
    /* 内部辅助实现 */
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

/* 内部辅助逻辑 */
EXPORT void zan_gui_fill_rects(i32 surface_id, const i32 *data, i32 count) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !data || count < 1) return;
    for (i32 i = 0; i < count; i++) {
        const i32 *d = data + i * 5;
        ZAN_IMPL(s, fill_rect)->fill_rect(s, (int)d[0], (int)d[1], (int)d[2],
                                          (int)d[3], (u32)d[4]);
    }
}

EXPORT void zan_gui_fill_circles(i32 surface_id, const i32 *data, i32 count) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !data || count < 1) return;
    for (i32 i = 0; i < count; i++) {
        const i32 *d = data + i * 4;
        ZAN_IMPL(s, fill_circle)->fill_circle(s, (int)d[0], (int)d[1],
                                              (int)d[2], (u32)d[3]);
    }
}

EXPORT void zan_gui_fill_radials(i32 surface_id, const i32 *data, i32 count) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !data || count < 1) return;
    for (i32 i = 0; i < count; i++) {
        const i32 *d = data + i * 5;
        ZAN_IMPL(s, fill_radial)->fill_radial(s, (int)d[0], (int)d[1],
                                              (int)d[2], (u32)d[3], (int)d[4]);
    }
}

/* 模块核心语义抽象与接口调用契约 */
EXPORT void zan_gui_blit_surface(i32 dst_id, i32 src_id, i32 dst_x, i32 dst_y,
                                 i32 src_x, i32 src_y, i32 w, i32 h) {
    if (dst_id < 0 || dst_id >= g_surface_count) return;
    if (src_id < 0 || src_id >= g_surface_count) return;
    zan_surface_t *d = g_surfaces[dst_id];
    zan_surface_t *s = g_surfaces[src_id];
    if (!d || !s || !d->pixels || !s->pixels) return;
    if (w <= 0 || h <= 0) return;
    if (s->be && s->be->sync_to_cpu) s->be->sync_to_cpu(s);
    if (d->be && d->be->sync_to_cpu) d->be->sync_to_cpu(d);
    int sx = (int)src_x, sy = (int)src_y, dx = (int)dst_x, dy = (int)dst_y;
    int cw = (int)w, ch = (int)h;
    if (sx < 0) { dx -= sx; cw += sx; sx = 0; }
    if (sy < 0) { dy -= sy; ch += sy; sy = 0; }
    if (sx + cw > s->width) cw = s->width - sx;
    if (sy + ch > s->height) ch = s->height - sy;
    if (dx < d->clip_x0) { int cut = d->clip_x0 - dx; dx += cut; sx += cut; cw -= cut; }
    if (dy < d->clip_y0) { int cut = d->clip_y0 - dy; dy += cut; sy += cut; ch -= cut; }
    if (dx + cw > d->clip_x1) cw = d->clip_x1 - dx;
    if (dy + ch > d->clip_y1) ch = d->clip_y1 - dy;
    if (cw <= 0 || ch <= 0) return;
    for (int row = 0; row < ch; row++) {
        u32 *from = s->pixels + (size_t)(sy + row) * (size_t)s->stride + (size_t)sx;
        u32 *to = d->pixels + (size_t)(dy + row) * (size_t)d->stride + (size_t)dx;
        memcpy(to, from, (size_t)cw * sizeof(u32));
    }
    if (d->be && d->be->sync_from_cpu) d->be->sync_from_cpu(d);
}

/* 模块核心语义抽象与接口调用契约 */
static void cpu_fill_sector(zan_surface_t *s, int cx, int cy, int r_inner,
                            int r_outer, int a0_deg, int a1_deg, u32 c) {
    if (r_outer <= 0) return;
    double a0 = (double)a0_deg, a1 = (double)a1_deg;
    if (a1 < a0) { double tmp = a0; a0 = a1; a1 = tmp; }
    double sweep = a1 - a0;
    if (sweep <= 0.0) return;
    if (sweep > 360.0) sweep = 360.0;
    /* 内部辅助逻辑 */
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

    /* 模块核心语义抽象与接口调用契约 */
    double e0x = sin(a0 * PI / 180.0), e0y = -cos(a0 * PI / 180.0);
    double e1x = sin(a1 * PI / 180.0), e1y = -cos(a1 * PI / 180.0);
    /* cross(v,w) = v */
    int wide = sweep > 180.0;   /* 核心系统底层抽象与内存语义契约 */

    int bx0 = cx - r_outer - 1, bx1 = cx + r_outer + 1;
    int by0 = cy - r_outer - 1, by1 = cy + r_outer + 1;
    if (!full) {
        /* 内部辅助实现 */
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
        /* 模块核心语义抽象与接口调用契约 */
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
                    /* 底层系统交互与数据协议契约 */
                    if (wide) {
                        /* 内部辅助逻辑 */
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

/* 吴小林抗锯齿直线光栅化算法 */
static void cpu_draw_line(zan_surface_t *s, int x0, int y0, int x1, int y1,
                          u32 c, int thickness) {
    int t = thickness;

    if (t <= 1) {
        /* 核心系统底层抽象与内存语义契约 */
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
        /* 宽线圆头抗锯齿几何光栅化 */
        double dx = (double)(x1 - x0), dy = (double)(y1 - y0);
        double len2 = dx*dx + dy*dy;
        double half = t * 0.5;
        /* 模块核心语义抽象与接口调用契约 */
        int lox = (x0 < x1 ? x0 : x1);
        int hix = (x0 > x1 ? x0 : x1);
        int loy = (y0 < y1 ? y0 : y1);
        int hiy = (y0 > y1 ? y0 : y1);
        int minx = lox - t - 1, maxx = hix + t + 1;
        int miny = loy - t - 1, maxy = hiy + t + 1;
        /* 模块核心语义抽象与接口调用契约 */
        double ux = 0.0, uy = 0.0, seglen = sqrt(len2);
        if (seglen > 0.0001) { ux = dx / seglen; uy = dy / seglen; }
        double rr = half + 0.5;
        if (miny < s->clip_y0) miny = s->clip_y0;
        if (maxy > s->clip_y1 - 1) maxy = s->clip_y1 - 1;
        if (minx < s->clip_x0) minx = s->clip_x0;
        if (maxx > s->clip_x1 - 1) maxx = s->clip_x1 - 1;
        for (int py = miny; py <= maxy; py++) {
            /* 内部实现与并发/内存约束规范 */
            double yc = (double)py + 0.5;
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
                double nx = -uy * rr, ny = ux * rr;   /* 核心系统底层抽象与内存语义契约 */
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
                double cov = half + 0.5 - dist;   /* 核心系统底层抽象与内存语义契约 */
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

/* 核心系统底层抽象与内存语义契约 */
static u8  *g_poly_cov = NULL;    /* 核心系统底层抽象与内存语义契约 */
static i32  g_poly_cov_w = 0;
static i32  g_poly_cov_h = 0;
static i32 *g_poly_dirty = NULL;  /* 核心系统底层抽象与内存语义契约 */
static i32  g_poly_dirty_n = 0;
static i32  g_poly_dirty_cap = 0;

/* 连续折线批处理光栅化 */
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
        /* 内部辅助逻辑 */
        if (g_poly_cov) memset(g_poly_cov, 0, (size_t)W * (size_t)H);
    }
    if (!g_poly_cov) { g_poly_cov_w = 0; g_poly_cov_h = 0; return; }
    g_poly_dirty_n = 0;
    /* 内部辅助实现 */

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
            /* 核心系统底层抽象与内存语义契约 */
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
                double cov = half + 0.5 - dist;   /* 核心系统底层抽象与内存语义契约 */
                if (cov <= 0.0) continue;
                int icov = aa_coverage(cov);
                int idx = py * W + px;
                if (g_poly_cov[idx] == 0) {       /* 核心系统底层抽象与内存语义契约 */
                    if (g_poly_dirty_n >= g_poly_dirty_cap) {
                        i32 cap2 = g_poly_dirty_cap ? g_poly_dirty_cap * 2 : 1024;
                        i32 *nd = (i32*)realloc(g_poly_dirty,
                            (size_t)cap2 * sizeof(i32));
                        /* 内部辅助实现 */
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
                /* 内部辅助实现 */
                if (icov > g_poly_cov[idx]) g_poly_cov[idx] = (u8)icov;
            }
        }
    }

    /* 模块核心语义抽象与接口调用契约 */
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

/* 底层系统交互与数据协议契约 */
EXPORT void zan_gui_draw_polyline_fx(i32 surface_id, const i32 *pts, i32 n,
                                     i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !pts || n < 2) return;
    int t = (int)thickness;
    if (t < 1) t = 1;
    ZAN_IMPL(s, polyline)->polyline(s, pts, (int)n, (u32)color, t, 8);
}

/* 内部辅助实现 */
static void cpu_polyline(zan_surface_t *s, const int32_t *pts, int n, u32 c,
                         int t, int shift);

EXPORT void zan_gui_draw_polybatch(i32 surface_id, const i32 *pts,
                                   const i32 *counts, i32 n_paths,
                                   i32 color, i32 thickness) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !pts || !counts || n_paths < 1) return;
    int t = (int)thickness;
    if (t < 1) t = 1;
    if (s->be && s->be->polybatch) {
        s->be->polybatch(s, pts, counts, (int)n_paths, (u32)color, t, 0);
        return;
    }
    i32 off = 0;
    if (s->be && s->be->polyline) {
        for (i32 p = 0; p < n_paths; p++) {
            i32 n = counts[p];
            if (n >= 2) s->be->polyline(s, pts + off, (int)n, (u32)color, t, 0);
            off += n * 2;
        }
        return;
    }
    for (i32 p = 0; p < n_paths; p++) {
        i32 n = counts[p];
        if (n >= 2) cpu_polyline(s, pts + off, (int)n, (u32)color, t, 0);
        off += n * 2;
    }
}

EXPORT void *zan_gui_get_pixels(i32 surface_id) {
    if (surface_id < 0 || surface_id >= g_surface_count || !g_surfaces[surface_id]) return NULL;
    zan_surface_t *s = g_surfaces[surface_id];
    /* 内部辅助实现 */
    if (s->be) {
        if (s->be->flush) s->be->flush(s);
        if (s->be->read_pixels) s->be->read_pixels(s);
    }
    return (void *)s->pixels;
}

/* 底层系统交互与数据协议契约 */
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

/* 内部实现与并发/内存约束规范 */
EXPORT i32 zan_gui_present_window(i32 surface_id, void *native_window) {
    if (surface_id < 0 || surface_id >= g_surface_count ||
        !g_surfaces[surface_id]) return 0;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s->be || !s->be->present) return 0;
    return s->be->present(s, native_window) ? 1 : 0;
}

/* 内部辅助实现 */
EXPORT void zan_gui_release_window(void *native_window) {
    if (g_backend && g_backend->drop_window)
        g_backend->drop_window(native_window);
}

/* 底层系统交互与数据协议契约 */
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
    /* 内部辅助实现 */
    long long nDen = boxX ? idw : 2 * (long long)idw;
    long long t0 = boxX ? (long long)(x0 - idx2) * isw
                        : ((long long)(x0 - idx2) * 2 + 1) * isw;
    long long tStep = boxX ? isw : 2 * (long long)isw;
    int qx = (int)(t0 / nDen);
    int rx = (int)(t0 % nDen);
    int qxStep = (int)(tStep / nDen);
    int rxStep = (int)(tStep % nDen);
    int rDen = (int)nDen;
    /* 内部辅助逻辑 */
    int qx2 = (int)((((long long)(x0 - idx2) + 1) * isw + idw - 1) / idw);
    int rx2 = (int)((((long long)(x0 - idx2) + 1) * isw + idw - 1) % idw);
    for (py = y0; py < y1; py++) {
        int sy0, sy1;
        if (boxY) {
            sy0 = isy + (int)((long long)(py - idy) * ish / idh);
            sy1 = isy + (int)(((long long)(py - idy + 1) * ish + idh - 1) / idh);
            if (sy1 <= sy0) { sy1 = sy0 + 1; }
        } else {
            /* 模块核心语义抽象与接口调用契约 */
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
                /* 内部辅助逻辑 */
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
    /* 模块核心语义抽象与接口调用契约 */
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

/* 内部辅助实现 */
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

/* 模块核心语义抽象与接口调用契约 */
static inline int zan_gui_in_hit_guard(iptr hwnd, int x, int y) {
    for (int i = 0; i < g_hit_guard_count; i++) {
        const int *g = g_hit_guards[i];
        if (g_hit_guard_win[i] != hwnd) continue;
        if (x >= g[0] && x < g[0] + g[2] && y >= g[1] && y < g[1] + g[3])
            return 1;
    }
    return 0;
}

/* 内部辅助实现 */
#include "gui_runtime_glyph.c"
#include "gui_runtime_text.c"
#if defined(ZAN_GUI_OHOS)
#include "gui_runtime_ohos.c"
#elif defined(ZAN_GUI_ANDROID_NATIVE)
#include "gui_runtime_android_native.c"
#elif defined(__wasm__)
#include "gui_runtime_wasm.c"
#else
#include "gui_runtime_x11.c"
#endif
#include "gui_runtime_font.c"
#include "gui_runtime_tray.c"
#include "gui_runtime_shims.c"
#include "gui_runtime_android.c"

/* 内部辅助实现 */
static void cpu_polyline(zan_surface_t *s, const int32_t *pts, int n, u32 c,
                         int t, int shift) {
    zan_polyline_core(s, pts, (i32)n, c, t, shift);
}

/* 模块核心语义抽象与接口调用契约 */
static void cpu_glyph_run(zan_surface_t *s, const zan_glyph_run *run) {
    u32 cr = (run->color >> 16) & 0xFF;
    u32 cg = (run->color >> 8) & 0xFF;
    u32 cb = run->color & 0xFF;
    u32 ca = (run->color >> 24) & 0xFF;
    if (ca == 0) ca = 255;   /* 核心系统底层抽象与内存语义契约 */
    for (int i = 0; i < run->count; i++) {
        const zan_glyph_tile *tile = run->items[i].tile;
        int ox = run->items[i].x, oy = run->items[i].y;
        int tw = tile->w, th = tile->h;
        int py0 = s->clip_y0 - oy; if (py0 < 0) py0 = 0;
        int py1 = s->clip_y1 - oy; if (py1 > th) py1 = th;
        int px0 = s->clip_x0 - ox; if (px0 < 0) px0 = 0;
        int px1 = s->clip_x1 - ox; if (px1 > tw) px1 = tw;
        if (tile->flags & ZAN_TILE_RGBA) {
            /* 内部辅助逻辑 */
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
    .draw_text    = NULL,   /* 底层系统交互与数据协议契约 */
    .glyph_run    = cpu_glyph_run,
    .blit_pixels  = cpu_blit_pixels,
    .blit_pixels_batch = cpu_blit_pixels_batch,
    .set_clip     = NULL,   /* 模块核心语义抽象与接口调用契约 */
    .flush        = NULL,   /* 核心系统底层抽象与内存语义契约 */
    .read_pixels  = NULL,   /* 核心系统底层抽象与内存语义契约 */
};

/* 内部辅助实现 */
#include "gui_gl_backend.c"

/* 内部辅助逻辑 */
EXPORT i32 zan_gui_set_render_backend(i32 mode) {
    const zan_gui_backend *be = NULL;
    if (mode == 1 || mode == 2) {
        if (zan_gui_internal_gl_install()) be = &zan_gl_backend;
    } else {
        zan_gui_internal_set_backend(NULL);
    }
    for (int i = 0; i < g_surface_count; i++) {
        zan_surface_t *s = g_surfaces[i];
        if (!s || s->be == be) continue;
        /* 模块核心语义抽象与接口调用契约 */
        if (s->be && s->be->flush) s->be->flush(s);
        if (s->be && s->be->read_pixels) s->be->read_pixels(s);
        s->be = be;
    }
    /* 模块核心语义抽象与接口调用契约 */
    if (!be) zan_gui_internal_gl_drop_present();
    return be ? 1 : 0;
}

/* 内部辅助逻辑 */
EXPORT const char *zan_gui_render_backend(void) {
    return zan_gui_internal_backend_name();
}

/* 内部辅助实现 */
/* 内部辅助实现 */
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
    /* 模块核心语义抽象与接口调用契约 */
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
