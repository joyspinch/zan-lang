/* 独立图像解码与 ARGB32 内存缓存（无窗口/渲染器状态依赖） */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "../common/host_oom.h"

#if defined(ZAN_IMAGE_STATIC)
#define ZAN_IMAGE_EXPORT
#elif defined(_WIN32)
#define ZAN_IMAGE_EXPORT __declspec(dllexport)
#else
#define ZAN_IMAGE_EXPORT __attribute__((visibility("default")))
#endif
#include "zan_image.h"

/* 避免 MinGW 静态链接 TLS 故障，使用标准内存布局 */
#define STBI_NO_THREAD_LOCALS
/* 统一解码为 RGBA8/ARGB32 格式，剥离 HDR/浮点路径以降低体积 */
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

/* 集成内嵌 libwebp 解码子集与 NanoSVG 栅格化器 */
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

#define ZAN_IMG_CACHE_CAP 1024
/* 移动平台内存预算控制：按需缓存与淘汰 */
#if defined(__ANDROID__)
#define ZAN_IMG_CACHE_BYTES (12u * 1024u * 1024u)
#else
#define ZAN_IMG_CACHE_BYTES 0
#endif

static size_t g_img_bytes = 0;
typedef struct {
    char path[512];
    zan_bitmap bitmap;
} zan_img_t;
static zan_img_t g_imgs[ZAN_IMG_CACHE_CAP];
static int g_img_n = 0;
static uint64_t g_img_serial = 0;

static uint64_t zan_img_next_serial(void) {
    /* 单调递增的序列号生成：防止 GPU 缓存复用已淘汰的旧句柄 */
    if (g_img_serial == (UINT64_MAX >> 1)) abort();
    return (UINT64_C(1) << 63) | ++g_img_serial;
}

static void zan_img_set_pixels(zan_img_t *e, uint32_t *pix, int w, int h) {
    e->bitmap.pixels = pix;
    e->bitmap.width = w;
    e->bitmap.height = h;
    e->bitmap.stride = w;
    e->bitmap.serial = zan_img_next_serial();
}

static zan_img_t *zan_img_find(const char *path) {
    int i;
    for (i = 0; i < g_img_n; i++)
        if (strncmp(g_imgs[i].path, path, 511) == 0) return &g_imgs[i];
    return NULL;
}

/* 缓存加载失败路径，避免重复发生磁盘 IO 与解码开销 */
#define ZAN_IMG_BAD_CAP 256
static char g_img_bad[ZAN_IMG_BAD_CAP][512];
static int g_img_bad_n = 0;

static int zan_img_is_bad(const char *path) {
    int i;
    for (i = 0; i < g_img_bad_n; i++)
        if (strncmp(g_img_bad[i], path, 511) == 0) return 1;
    return 0;
}

static void zan_img_mark_bad(const char *path) {
    if (zan_img_is_bad(path)) return;
    if (g_img_bad_n >= ZAN_IMG_BAD_CAP) {
        memmove(&g_img_bad[0], &g_img_bad[1],
                sizeof(g_img_bad[0]) * (ZAN_IMG_BAD_CAP - 1));
        g_img_bad_n = ZAN_IMG_BAD_CAP - 1;
    }
    strncpy(g_img_bad[g_img_bad_n], path, 511);
    g_img_bad[g_img_bad_n][511] = '\0';
    g_img_bad_n++;
}

/* 内存二进制图像缓存键管理：调用方维护自身源数据生命周期 */
#define ZAN_IMG_MEM_CAP 1024
static zan_img_t g_mem_imgs[ZAN_IMG_MEM_CAP];
static int g_mem_img_n = 0;

static int zan_img_is_mem_key(const char *key) {
    return key && strncmp(key, "mem:", 4) == 0;
}

static zan_img_t *zan_img_mem_find(const char *key) {
    int i;
    for (i = 0; i < g_mem_img_n; i++)
        if (strncmp(g_mem_imgs[i].path, key, 511) == 0) return &g_mem_imgs[i];
    return NULL;
}

/* 仅管理本模块内分配的内存缓存生命周期 */
static zan_img_t *zan_img_mem_put(const char *key, uint32_t *pix, int w, int h) {
    zan_img_t *e = zan_img_mem_find(key);
    if (e) { free(pix); return e; }
    if (g_mem_img_n >= ZAN_IMG_MEM_CAP) {
        free((void *)g_mem_imgs[0].bitmap.pixels);
        memmove(&g_mem_imgs[0], &g_mem_imgs[1],
                sizeof(zan_img_t) * (ZAN_IMG_MEM_CAP - 1));
        g_mem_img_n = ZAN_IMG_MEM_CAP - 1;
    }
    e = &g_mem_imgs[g_mem_img_n++];
    strncpy(e->path, key, 511); e->path[511] = '\0';
    zan_img_set_pixels(e, pix, w, h);
    return e;
}

static int zan_img_dimensions_ok(int w, int h) {
    return w > 0 && h > 0 && w <= 32768 && h <= 32768
        && (size_t)w <= SIZE_MAX / sizeof(uint32_t) / (size_t)h;
}

/* 底层系统交互与数据协议契约 */
static uint32_t *zan_rgba_to_argb(const unsigned char *data, int w, int h) {
    uint32_t *pix;
    size_t i, n;
    if (!zan_img_dimensions_ok(w, h)) return NULL;
    n = (size_t)w * (size_t)h;
    pix = (uint32_t *)malloc(n * sizeof(uint32_t));
    if (!pix) return NULL;
    for (i = 0; i < n; i++) {
        unsigned char r = data[i*4], g = data[i*4+1],
                      b = data[i*4+2], a = data[i*4+3];
        pix[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16)
               | ((uint32_t)g << 8) | b;
    }
    return pix;
}

static uint32_t zan_img_le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ICO 格式解码：选取最大尺寸条目 */
static const unsigned char *zan_img_ico_png(const unsigned char *data, int len,
                                          int *png_len) {
    int count, i, best_area = 0;
    uint32_t best_off = 0, best_len = 0;
    if (len < 6 || zan_img_le32(data) != UINT32_C(0x00010000)) return NULL;
    count = data[4] | (data[5] << 8);
    if (count <= 0 || count > (len - 6) / 16) return NULL;
    for (i = 0; i < count; i++) {
        const unsigned char *entry = data + 6 + i * 16;
        int ew = entry[0] ? entry[0] : 256;
        int eh = entry[1] ? entry[1] : 256;
        uint32_t size = zan_img_le32(entry + 8);
        uint32_t off = zan_img_le32(entry + 12);
        if (!off || !size || off > (uint32_t)len
            || size > (uint32_t)len - off)
            continue;
        if (ew * eh > best_area) {
            best_area = ew * eh;
            best_off = off;
            best_len = size;
        }
    }
    if (!best_area || best_len < 8
        || memcmp(data + best_off, "\x89PNG\r\n\x1a\n", 8) != 0)
        return NULL;
    *png_len = (int)best_len;
    return data + best_off;
}

static uint32_t *zan_img_decode(const unsigned char *data, int len,
                               int *out_w, int *out_h) {
    uint32_t *pix = NULL;
    int w = 0, h = 0, n;
    *out_w = *out_h = 0;
    if (!data || len <= 0) return NULL;
    if (len >= 6 && zan_img_le32(data) == UINT32_C(0x00010000)) {
        data = zan_img_ico_png(data, len, &len);
        if (!data) return NULL;
    }
    if (len >= 12 && memcmp(data, "RIFF", 4) == 0
        && memcmp(data + 8, "WEBP", 4) == 0) {
        if (WebPGetInfo(data, (size_t)len, &w, &h)
            && zan_img_dimensions_ok(w, h)) {
            uint8_t *rgba = WebPDecodeRGBA(data, (size_t)len, &w, &h);
            if (rgba) {
                pix = zan_rgba_to_argb(rgba, w, h);
                WebPFree(rgba);
            }
        }
    } else {
        unsigned char *rgba = stbi_load_from_memory(data, len, &w, &h, &n, 4);
        if (rgba) {
            pix = zan_rgba_to_argb(rgba, w, h);
            stbi_image_free(rgba);
        }
    }
    if (pix) { *out_w = w; *out_h = h; }
    return pix;
}

static zan_img_t *zan_img_load(const char *path) {
    zan_img_t *e;
    int w = 0, h = 0;
    uint32_t *pix = NULL;
    if (!path || !path[0]) return NULL;
    e = zan_img_find(path);
    if (e) return e;
    if (zan_img_is_mem_key(path)) return zan_img_mem_find(path);
    if (zan_img_is_bad(path)) return NULL;
#ifdef _WIN32
    /* Win32 宽字符 UTF-8 路径读取 */
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
        pix = zan_img_decode(bytes, (int)got, &w, &h);
        free(bytes);
    }
#else
    /* 跨平台统一图像解码（含 ICO 内嵌 PNG 负载） */
    {
        FILE *f = fopen(path, "rb");
        long len;
        unsigned char *bytes;
        if (!f) { zan_img_mark_bad(path); return NULL; }
        if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) <= 0
            || len > INT32_MAX || fseek(f, 0, SEEK_SET) != 0) {
            fclose(f); zan_img_mark_bad(path); return NULL;
        }
        bytes = (unsigned char *)malloc((size_t)len);
        if (!bytes) { fclose(f); return NULL; }
        if (fread(bytes, 1, (size_t)len, f) != (size_t)len) {
            free(bytes); fclose(f); return NULL;
        }
        fclose(f);
        pix = zan_img_decode(bytes, (int)len, &w, &h);
        free(bytes);
    }
#endif
    if (!pix) { zan_img_mark_bad(path); return NULL; }
    g_img_bytes += (size_t)w * (size_t)h * sizeof(uint32_t);
    while (g_img_n >= ZAN_IMG_CACHE_CAP
           || (ZAN_IMG_CACHE_BYTES && g_img_bytes > ZAN_IMG_CACHE_BYTES
               && g_img_n > 1)) {
        g_img_bytes -= (size_t)g_imgs[0].bitmap.width
                     * (size_t)g_imgs[0].bitmap.height * sizeof(uint32_t);
        free((void *)g_imgs[0].bitmap.pixels);
        memmove(&g_imgs[0], &g_imgs[1],
                sizeof(zan_img_t) * (size_t)(g_img_n - 1));
        g_img_n--;
    }
    e = &g_imgs[g_img_n++];
    strncpy(e->path, path, 511); e->path[511] = '\0';
    zan_img_set_pixels(e, pix, w, h);
    return e;
}

ZAN_IMAGE_EXPORT const zan_bitmap *zan_image_get(const char *key) {
    zan_img_t *e = zan_img_load(key);
    return e ? &e->bitmap : NULL;
}

ZAN_IMAGE_EXPORT int32_t zan_image_width(const char *key) {
    const zan_bitmap *bitmap = zan_image_get(key);
    return bitmap ? bitmap->width : 0;
}

ZAN_IMAGE_EXPORT int32_t zan_image_height(const char *key) {
    const zan_bitmap *bitmap = zan_image_get(key);
    return bitmap ? bitmap->height : 0;
}

ZAN_IMAGE_EXPORT const char *zan_image_mem_report(void) {
    static char report[96];
    size_t mem_bytes = 0;
    int i;
    for (i = 0; i < g_mem_img_n; i++) {
        mem_bytes += (size_t)g_mem_imgs[i].bitmap.stride
                   * (size_t)g_mem_imgs[i].bitmap.height * sizeof(uint32_t);
    }
    snprintf(report, sizeof(report), "img %d/%.1fM mimg %d/%.1fM",
             g_img_n, (double)g_img_bytes / 1048576.0,
             g_mem_img_n, (double)mem_bytes / 1048576.0);
    return report;
}

ZAN_IMAGE_EXPORT void zan_image_evict(const char *path) {
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
            free((void *)g_mem_imgs[i].bitmap.pixels);
            memmove(&g_mem_imgs[i], &g_mem_imgs[i+1],
                    sizeof(zan_img_t) * (g_mem_img_n - i - 1));
            g_mem_img_n--;
            return;
        }
    }
    for (i = 0; i < g_img_n; i++) {
        if (strncmp(g_imgs[i].path, path, 511) == 0) {
            g_img_bytes -= (size_t)g_imgs[i].bitmap.width
                         * (size_t)g_imgs[i].bitmap.height * sizeof(uint32_t);
            free((void *)g_imgs[i].bitmap.pixels);
            memmove(&g_imgs[i], &g_imgs[i+1],
                    sizeof(zan_img_t) * (g_img_n - i - 1));
            g_img_n--;
            return;
        }
    }
}

/* 图像魔数识别：通过 RIFF 头探测 WebP，其他格式派发至 stb_image */
ZAN_IMAGE_EXPORT int32_t zan_image_load_mem(const char *key, const char *data,
                                           int32_t len) {
    int w = 0, h = 0;
    uint32_t *pix = NULL;
    zan_img_t *e;
    if (!key || !key[0] || !data || len <= 0) return 0;
    e = zan_img_mem_find(key);
    if (e) return e->bitmap.width;
    pix = zan_img_decode((const unsigned char *)data, (int)len, &w, &h);
    if (!pix) return 0;
    zan_img_mem_put(key, pix, w, h);
    return w;
}

ZAN_IMAGE_EXPORT int32_t zan_image_load_mem_bytes(const char *key,
                                                 const char *data, int32_t len) {
    return zan_image_load_mem(key, data, len);
}

/* SVG 解析：len < 0 支持 \0 结尾文本 */
ZAN_IMAGE_EXPORT int32_t zan_image_load_svg(const char *key, const char *text,
                                           int32_t len, int32_t rasterW,
                                           int32_t rasterH) {
    uint32_t *pix = NULL;
    int w = 0, h = 0;
    zan_img_t *e;
    if (!key || !key[0] || !text) return 0;
    e = zan_img_mem_find(key);
    if (e) return e->bitmap.width;
    if (len < 0) len = (int32_t)strlen(text);
    if (len <= 0) return 0;
    if (!zan_svg_raster(text, (int)len, &pix, &w, &h, (int)rasterW,
                        (int)rasterH))
        return 0;
    zan_img_mem_put(key, pix, w, h);
    return w;
}

ZAN_IMAGE_EXPORT int32_t zan_image_register_argb(const char *key,
                                                const uint32_t *pixels,
                                                int w, int h, int stride) {
    uint32_t *copy;
    zan_img_t *e;
    int row;
    if (!zan_img_is_mem_key(key) || !pixels || !zan_img_dimensions_ok(w, h)
        || stride < w)
        return 0;
    /* 源跨度与紧凑目标像素缓冲区边界校验 */
    if ((size_t)(h - 1) > (SIZE_MAX / sizeof(uint32_t) - (size_t)w)
                          / (size_t)stride)
        return 0;
    copy = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!copy) return 0;
    for (row = 0; row < h; row++) {
        memcpy(copy + (size_t)row * (size_t)w,
               pixels + (size_t)row * (size_t)stride,
               (size_t)w * sizeof(uint32_t));
    }
    /* 先拷贝后替换：防止借用的源图像在替换写入时发生并发破坏 */
    e = zan_img_mem_find(key);
    if (e) {
        free((void *)e->bitmap.pixels);
        zan_img_set_pixels(e, copy, w, h);
    } else {
        zan_img_mem_put(key, copy, w, h);
    }
    return w;
}
