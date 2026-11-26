/* zan_image_ops.c -- 商用高性能原生 C11 图像像素算子
 * 被 zan_image.c 以 unity 方式包含，天然覆盖全部 12 跨平台目标。
 * 核心优化策略：
 * - 行主序连续访存（彻底消除列遍历导致的 L1/L2 Cache 抖动）
 * - 14-bit 定点数可分离 Lanczos-3 缩放（预计算权重表，内层零浮点）
 * - 32x32 Cache-Tiled 旋转转置
 * - 256-entry 查表调色 + O(1) 行主序滑窗模糊 + 定点数透明混合 */

#include <math.h>

static inline uint8_t zan_clamp_u8(int v) {
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

/* ---- 高质量可分离 14-bit 定点数 Lanczos-3 权重表 ---- */
typedef struct zan_resamp_tbl {
    int32_t *starts;
    int32_t *counts;
    int32_t *offsets;
    int16_t *weights; /* 14-bit fixed point, sum == 16384 */
} zan_resamp_tbl_t;

static double zan_lanczos3_kernel(double x) {
    if (x < 0.0) x = -x;
    if (x < 1e-9) return 1.0;
    if (x >= 3.0) return 0.0;
    const double pi = 3.14159265358979323846;
    double px = pi * x;
    double px3 = px / 3.0;
    return (sin(px) / px) * (sin(px3) / px3);
}

static double zan_box_kernel(double x) {
    if (x < 0.0) x = -x;
    return x < 0.5 ? 1.0 : 0.0;
}

static void zan_resamp_free(zan_resamp_tbl_t *t) {
    free(t->starts);
    free(t->counts);
    free(t->offsets);
    free(t->weights);
    memset(t, 0, sizeof(*t));
}

static int zan_resamp_build(zan_resamp_tbl_t *t, int32_t src_len, int32_t dst_len,
                            double radius, int use_lanczos) {
    memset(t, 0, sizeof(*t));
    double ratio = (double)src_len / (double)dst_len;
    double scale = ratio > 1.0 ? ratio : 1.0;
    double support = radius * scale;
    int32_t max_taps = (int32_t)ceil(support * 2.0) + 4;
    if (max_taps > src_len) max_taps = src_len;
    if (max_taps < 1) max_taps = 1;

    t->starts  = (int32_t *)malloc((size_t)dst_len * sizeof(int32_t));
    t->counts  = (int32_t *)malloc((size_t)dst_len * sizeof(int32_t));
    t->offsets = (int32_t *)malloc((size_t)dst_len * sizeof(int32_t));
    t->weights = (int16_t *)malloc((size_t)dst_len * (size_t)max_taps * sizeof(int16_t));
    double *tmpw = (double *)malloc((size_t)max_taps * sizeof(double));
    if (!t->starts || !t->counts || !t->offsets || !t->weights || !tmpw) {
        free(tmpw);
        zan_resamp_free(t);
        return 0;
    }

    int32_t total_w = 0;
    for (int32_t d = 0; d < dst_len; d++) {
        double center = ((double)d + 0.5) * ratio - 0.5;
        int32_t left = (int32_t)floor(center - support);
        int32_t right = (int32_t)ceil(center + support);
        if (left < 0) left = 0;
        if (right >= src_len) right = src_len - 1;
        if (right < left) right = left;

        int32_t cnt = right - left + 1;
        if (cnt > max_taps) cnt = max_taps;
        double sum = 0.0;
        for (int32_t k = 0; k < cnt; k++) {
            double dist = ((double)(left + k) - center) / scale;
            double w = use_lanczos ? zan_lanczos3_kernel(dist) : zan_box_kernel(dist);
            tmpw[k] = w;
            sum += w;
        }
        if (sum == 0.0) {
            cnt = 1;
            int32_t cidx = (int32_t)floor(center + 0.5);
            if (cidx < 0) cidx = 0;
            if (cidx >= src_len) cidx = src_len - 1;
            left = cidx;
            tmpw[0] = 1.0;
            sum = 1.0;
        }
        /* 修剪首尾零权重 */
        while (cnt > 1 && fabs(tmpw[0]) < 1e-6) {
            left++;
            cnt--;
            memmove(tmpw, tmpw + 1, (size_t)cnt * sizeof(double));
        }
        while (cnt > 1 && fabs(tmpw[cnt - 1]) < 1e-6) {
            cnt--;
        }

        t->starts[d] = left;
        t->counts[d] = cnt;
        t->offsets[d] = total_w;

        double inv = 16384.0 / sum;
        int32_t qsum = 0;
        int32_t peak = 0;
        int32_t peak_abs = -1;
        for (int32_t k = 0; k < cnt; k++) {
            double v = tmpw[k] * inv;
            int32_t iv = (int32_t)(v >= 0.0 ? (v + 0.5) : (v - 0.5));
            t->weights[total_w + k] = (int16_t)iv;
            qsum += iv;
            int32_t av = iv < 0 ? -iv : iv;
            if (av > peak_abs) { peak_abs = av; peak = k; }
        }
        /* 消除定点舍入残差，保证稳态纯色图精确不变色 */
        t->weights[total_w + peak] = (int16_t)(t->weights[total_w + peak] + (16384 - qsum));
        total_w += cnt;
    }

    free(tmpw);
    return 1;
}

static int zan_resize_separable(const uint32_t *src, int32_t sw, int32_t sh,
                                uint32_t *dst, int32_t dw, int32_t dh,
                                double radius, int use_lanczos) {
    zan_resamp_tbl_t hx, vy;
    if (!zan_resamp_build(&hx, sw, dw, radius, use_lanczos)) return 0;
    if (!zan_resamp_build(&vy, sh, dh, radius, use_lanczos)) {
        zan_resamp_free(&hx);
        return 0;
    }

    uint32_t *mid = (uint32_t *)malloc((size_t)dw * (size_t)sh * sizeof(uint32_t));
    if (!mid) {
        zan_resamp_free(&hx);
        zan_resamp_free(&vy);
        return 0;
    }

    /* 水平阶段：行主序连续读写，完全命中 L1 Cache */
    for (int32_t y = 0; y < sh; y++) {
        const uint32_t *srow = src + (size_t)y * (size_t)sw;
        uint32_t *mrow = mid + (size_t)y * (size_t)dw;
        for (int32_t x = 0; x < dw; x++) {
            int32_t st = hx.starts[x];
            int32_t cnt = hx.counts[x];
            const int16_t *w = hx.weights + hx.offsets[x];
            int32_t sb = 8192, sg = 8192, sr = 8192, sa = 8192;
            const uint32_t *sp = srow + st;
            for (int32_t k = 0; k < cnt; k++) {
                uint32_t p = sp[k];
                int32_t wk = (int32_t)w[k];
                sb += (int32_t)(p & 0xFFu) * wk;
                sg += (int32_t)((p >> 8) & 0xFFu) * wk;
                sr += (int32_t)((p >> 16) & 0xFFu) * wk;
                sa += (int32_t)((p >> 24) & 0xFFu) * wk;
            }
            uint32_t b = zan_clamp_u8(sb >> 14);
            uint32_t g = zan_clamp_u8(sg >> 14);
            uint32_t r = zan_clamp_u8(sr >> 14);
            uint32_t a = zan_clamp_u8(sa >> 14);
            mrow[x] = b | (g << 8) | (r << 16) | (a << 24);
        }
    }

    /* 垂直阶段：外层按输出行、内层按 x 顺序推进，保持连续访存 */
    int32_t *acc = (int32_t *)malloc((size_t)dw * 4 * sizeof(int32_t));
    if (!acc) {
        free(mid);
        zan_resamp_free(&hx);
        zan_resamp_free(&vy);
        return 0;
    }

    for (int32_t y = 0; y < dh; y++) {
        int32_t st = vy.starts[y];
        int32_t cnt = vy.counts[y];
        const int16_t *w = vy.weights + vy.offsets[y];
        uint32_t *drow = dst + (size_t)y * (size_t)dw;

        /* 第一个 tap 直接初始化累加行 */
        {
            const uint32_t *mrow = mid + (size_t)st * (size_t)dw;
            int32_t w0 = (int32_t)w[0];
            for (int32_t x = 0; x < dw; x++) {
                uint32_t p = mrow[x];
                int32_t *a4 = acc + (size_t)x * 4;
                a4[0] = 8192 + (int32_t)(p & 0xFFu) * w0;
                a4[1] = 8192 + (int32_t)((p >> 8) & 0xFFu) * w0;
                a4[2] = 8192 + (int32_t)((p >> 16) & 0xFFu) * w0;
                a4[3] = 8192 + (int32_t)((p >> 24) & 0xFFu) * w0;
            }
        }
        for (int32_t k = 1; k < cnt; k++) {
            const uint32_t *mrow = mid + (size_t)(st + k) * (size_t)dw;
            int32_t wk = (int32_t)w[k];
            for (int32_t x = 0; x < dw; x++) {
                uint32_t p = mrow[x];
                int32_t *a4 = acc + (size_t)x * 4;
                a4[0] += (int32_t)(p & 0xFFu) * wk;
                a4[1] += (int32_t)((p >> 8) & 0xFFu) * wk;
                a4[2] += (int32_t)((p >> 16) & 0xFFu) * wk;
                a4[3] += (int32_t)((p >> 24) & 0xFFu) * wk;
            }
        }
        for (int32_t x = 0; x < dw; x++) {
            const int32_t *a4 = acc + (size_t)x * 4;
            uint32_t b = zan_clamp_u8(a4[0] >> 14);
            uint32_t g = zan_clamp_u8(a4[1] >> 14);
            uint32_t r = zan_clamp_u8(a4[2] >> 14);
            uint32_t a = zan_clamp_u8(a4[3] >> 14);
            drow[x] = b | (g << 8) | (r << 16) | (a << 24);
        }
    }

    free(acc);
    free(mid);
    zan_resamp_free(&hx);
    zan_resamp_free(&vy);
    return 1;
}

static int zan_resize_bilinear(const uint32_t *src, int32_t sw, int32_t sh,
                               uint32_t *dst, int32_t dw, int32_t dh) {
    int32_t *x0 = (int32_t *)malloc((size_t)dw * sizeof(int32_t));
    int32_t *x1 = (int32_t *)malloc((size_t)dw * sizeof(int32_t));
    int32_t *wx = (int32_t *)malloc((size_t)dw * sizeof(int32_t));
    if (!x0 || !x1 || !wx) { free(x0); free(x1); free(wx); return 0; }

    for (int32_t dx = 0; dx < dw; dx++) {
        int64_t fp = (dw > 1) ? (((int64_t)dx * (int64_t)(sw - 1) << 16) / (int64_t)(dw - 1)) : 0;
        int32_t sx = (int32_t)(fp >> 16);
        int32_t fx = (int32_t)((fp & 0xFFFF) >> 8);
        if (sx >= sw - 1) { sx = sw - 1; x0[dx] = sx; x1[dx] = sx; wx[dx] = 0; }
        else { x0[dx] = sx; x1[dx] = sx + 1; wx[dx] = fx; }
    }

    for (int32_t dy = 0; dy < dh; dy++) {
        int64_t fp = (dh > 1) ? (((int64_t)dy * (int64_t)(sh - 1) << 16) / (int64_t)(dh - 1)) : 0;
        int32_t sy = (int32_t)(fp >> 16);
        int32_t fy = (int32_t)((fp & 0xFFFF) >> 8);
        int32_t sy1 = (sy < sh - 1) ? (sy + 1) : (sh - 1);
        if (sy >= sh - 1) { sy = sh - 1; sy1 = sh - 1; fy = 0; }
        int32_t ify = 256 - fy;

        const uint32_t *r0 = src + (size_t)sy * (size_t)sw;
        const uint32_t *r1 = src + (size_t)sy1 * (size_t)sw;
        uint32_t *drow = dst + (size_t)dy * (size_t)dw;

        for (int32_t dx = 0; dx < dw; dx++) {
            int32_t i0 = x0[dx], i1 = x1[dx];
            int32_t fx = wx[dx], ifx = 256 - fx;
            uint32_t p00 = r0[i0], p10 = r0[i1];
            uint32_t p01 = r1[i0], p11 = r1[i1];

            uint32_t rb0 = ((p00 & 0x00FF00FFu) * (uint32_t)ifx + (p10 & 0x00FF00FFu) * (uint32_t)fx) >> 8;
            uint32_t ga0 = (((p00 >> 8) & 0x00FF00FFu) * (uint32_t)ifx + ((p10 >> 8) & 0x00FF00FFu) * (uint32_t)fx) >> 8;
            uint32_t rb1 = ((p01 & 0x00FF00FFu) * (uint32_t)ifx + (p11 & 0x00FF00FFu) * (uint32_t)fx) >> 8;
            uint32_t ga1 = (((p01 >> 8) & 0x00FF00FFu) * (uint32_t)ifx + ((p11 >> 8) & 0x00FF00FFu) * (uint32_t)fx) >> 8;

            uint32_t rb = (((rb0 & 0x00FF00FFu) * (uint32_t)ify + (rb1 & 0x00FF00FFu) * (uint32_t)fy + 0x00800080u) >> 8) & 0x00FF00FFu;
            uint32_t ga = (((ga0 & 0x00FF00FFu) * (uint32_t)ify + (ga1 & 0x00FF00FFu) * (uint32_t)fy + 0x00800080u) >> 8) & 0x00FF00FFu;
            drow[dx] = rb | (ga << 8);
        }
    }

    free(x0); free(x1); free(wx);
    return 1;
}

/* filter: 0=Nearest, 1=Bilinear, 2=Area/Box, 3=Lanczos-3 */
ZAN_IMAGE_EXPORT int32_t zan_image_resize_argb(const uint32_t *src, int32_t sw, int32_t sh,
                                               uint32_t *dst, int32_t dw, int32_t dh,
                                               int32_t filter) {
    if (!src || !dst || !zan_img_dimensions_ok(sw, sh) || !zan_img_dimensions_ok(dw, dh))
        return 0;
    if (sw == dw && sh == dh) {
        memcpy(dst, src, (size_t)sw * (size_t)sh * sizeof(uint32_t));
        return 1;
    }
    if (filter == 0) {
        int32_t *xmap = (int32_t *)malloc((size_t)dw * sizeof(int32_t));
        if (!xmap) return 0;
        for (int32_t dx = 0; dx < dw; dx++) {
            int32_t sx = (int32_t)((int64_t)dx * (int64_t)sw / (int64_t)dw);
            if (sx >= sw) sx = sw - 1;
            xmap[dx] = sx;
        }
        for (int32_t dy = 0; dy < dh; dy++) {
            int32_t sy = (int32_t)((int64_t)dy * (int64_t)sh / (int64_t)dh);
            if (sy >= sh) sy = sh - 1;
            const uint32_t *srow = src + (size_t)sy * (size_t)sw;
            uint32_t *drow = dst + (size_t)dy * (size_t)dw;
            for (int32_t dx = 0; dx < dw; dx++) drow[dx] = srow[xmap[dx]];
        }
        free(xmap);
        return 1;
    }
    if (filter == 1) return zan_resize_bilinear(src, sw, sh, dst, dw, dh);
    if (filter == 2) return zan_resize_separable(src, sw, sh, dst, dw, dh, 0.5, 0);
    return zan_resize_separable(src, sw, sh, dst, dw, dh, 3.0, 1);
}

/* 32x32 Cache-Tiled 旋转（mode: 90, 180, 270） */
ZAN_IMAGE_EXPORT int32_t zan_image_rotate_argb(const uint32_t *src, int32_t sw, int32_t sh,
                                               uint32_t *dst, int32_t mode) {
    if (!src || !dst || !zan_img_dimensions_ok(sw, sh)) return 0;
    if (mode == 180) {
        size_t n = (size_t)sw * (size_t)sh;
        for (size_t i = 0; i < n; i++) dst[n - 1 - i] = src[i];
        return 1;
    }
    const int32_t TILE = 32;
    if (mode == 90) {
        for (int32_t by = 0; by < sh; by += TILE) {
            int32_t yend = by + TILE < sh ? by + TILE : sh;
            for (int32_t bx = 0; bx < sw; bx += TILE) {
                int32_t xend = bx + TILE < sw ? bx + TILE : sw;
                for (int32_t y = by; y < yend; y++) {
                    int32_t dx = sh - 1 - y;
                    const uint32_t *srow = src + (size_t)y * (size_t)sw;
                    for (int32_t x = bx; x < xend; x++) {
                        dst[(size_t)x * (size_t)sh + (size_t)dx] = srow[x];
                    }
                }
            }
        }
        return 1;
    }
    if (mode == 270) {
        for (int32_t by = 0; by < sh; by += TILE) {
            int32_t yend = by + TILE < sh ? by + TILE : sh;
            for (int32_t bx = 0; bx < sw; bx += TILE) {
                int32_t xend = bx + TILE < sw ? bx + TILE : sw;
                for (int32_t y = by; y < yend; y++) {
                    const uint32_t *srow = src + (size_t)y * (size_t)sw;
                    for (int32_t x = bx; x < xend; x++) {
                        dst[(size_t)(sw - 1 - x) * (size_t)sh + (size_t)y] = srow[x];
                    }
                }
            }
        }
        return 1;
    }
    return 0;
}

/* LUT 对比度 + 定点数饱和度 + 灰度（单遍融合，保留 Alpha） */
ZAN_IMAGE_EXPORT void zan_image_color_adjust_argb(uint32_t *pixels, int32_t count,
                                                  int32_t contrast_pct, int32_t sat_pct,
                                                  int32_t grayscale) {
    if (!pixels || count <= 0) return;
    if (contrast_pct != 0) {
        if (contrast_pct < -100) contrast_pct = -100;
        if (contrast_pct > 100) contrast_pct = 100;
        double f = (100.0 + (double)contrast_pct) / 100.0;
        f = f * f;
        int32_t f256 = (int32_t)(f * 256.0 + 0.5);
        uint8_t lut[256];
        for (int i = 0; i < 256; i++) {
            lut[i] = zan_clamp_u8((((i - 128) * f256) >> 8) + 128);
        }
        for (int32_t i = 0; i < count; i++) {
            uint32_t p = pixels[i];
            uint32_t b = lut[p & 0xFFu];
            uint32_t g = lut[(p >> 8) & 0xFFu];
            uint32_t r = lut[(p >> 16) & 0xFFu];
            pixels[i] = b | (g << 8) | (r << 16) | (p & 0xFF000000u);
        }
    }
    if (grayscale) {
        for (int32_t i = 0; i < count; i++) {
            uint32_t p = pixels[i];
            uint32_t b = p & 0xFFu;
            uint32_t g = (p >> 8) & 0xFFu;
            uint32_t r = (p >> 16) & 0xFFu;
            uint32_t y = (b * 29u + g * 150u + r * 77u) >> 8;
            pixels[i] = y | (y << 8) | (y << 16) | (p & 0xFF000000u);
        }
    } else if (sat_pct != 0) {
        if (sat_pct < -100) sat_pct = -100;
        if (sat_pct > 200) sat_pct = 200;
        int32_t s256 = (100 + sat_pct) * 256 / 100;
        for (int32_t i = 0; i < count; i++) {
            uint32_t p = pixels[i];
            int32_t b = (int32_t)(p & 0xFFu);
            int32_t g = (int32_t)((p >> 8) & 0xFFu);
            int32_t r = (int32_t)((p >> 16) & 0xFFu);
            int32_t lum = (b * 29 + g * 150 + r * 77) >> 8;
            uint32_t ob = zan_clamp_u8(lum + (((b - lum) * s256) >> 8));
            uint32_t og = zan_clamp_u8(lum + (((g - lum) * s256) >> 8));
            uint32_t or_ = zan_clamp_u8(lum + (((r - lum) * s256) >> 8));
            pixels[i] = ob | (og << 8) | (or_ << 16) | (p & 0xFF000000u);
        }
    }
}

/* 行主序 O(1) 可分离滑窗模糊（passes=1 盒式模糊，passes=3 近似高斯模糊） */
ZAN_IMAGE_EXPORT int32_t zan_image_box_blur_argb(uint32_t *pixels, int32_t w, int32_t h,
                                                 int32_t radius, int32_t passes) {
    if (!pixels || !zan_img_dimensions_ok(w, h) || w <= 1 || h <= 1 || radius <= 0)
        return 0;
    if (passes < 1) passes = 1;
    if (passes > 5) passes = 5;
    int32_t rx = radius < w - 1 ? radius : w - 1;
    int32_t ry = radius < h - 1 ? radius : h - 1;

    uint32_t *tmp = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    int32_t *col_sum = (int32_t *)malloc((size_t)w * 4 * sizeof(int32_t));
    if (!tmp || !col_sum) { free(tmp); free(col_sum); return 0; }

    int32_t win_x = 2 * rx + 1;
    int32_t win_y = 2 * ry + 1;
    uint64_t inv_x = ((1ULL << 32) + (uint64_t)(win_x / 2)) / (uint64_t)win_x;
    uint64_t inv_y = ((1ULL << 32) + (uint64_t)(win_y / 2)) / (uint64_t)win_y;

    for (int32_t pass = 0; pass < passes; pass++) {
        /* 水平通行：pixels -> tmp */
        for (int32_t y = 0; y < h; y++) {
            const uint32_t *srow = pixels + (size_t)y * (size_t)w;
            uint32_t *trow = tmp + (size_t)y * (size_t)w;
            int32_t sb = 0, sg = 0, sr = 0, sa = 0;
            uint32_t p0 = srow[0];
            sb = (int32_t)(p0 & 0xFFu) * (rx + 1);
            sg = (int32_t)((p0 >> 8) & 0xFFu) * (rx + 1);
            sr = (int32_t)((p0 >> 16) & 0xFFu) * (rx + 1);
            sa = (int32_t)((p0 >> 24) & 0xFFu) * (rx + 1);
            for (int32_t k = 1; k <= rx; k++) {
                uint32_t pk = srow[k < w ? k : w - 1];
                sb += (int32_t)(pk & 0xFFu);
                sg += (int32_t)((pk >> 8) & 0xFFu);
                sr += (int32_t)((pk >> 16) & 0xFFu);
                sa += (int32_t)((pk >> 24) & 0xFFu);
            }
            for (int32_t x = 0; x < w; x++) {
                uint32_t ob = (uint32_t)(((uint64_t)(uint32_t)sb * inv_x + (1ULL << 31)) >> 32);
                uint32_t og = (uint32_t)(((uint64_t)(uint32_t)sg * inv_x + (1ULL << 31)) >> 32);
                uint32_t or_ = (uint32_t)(((uint64_t)(uint32_t)sr * inv_x + (1ULL << 31)) >> 32);
                uint32_t oa = (uint32_t)(((uint64_t)(uint32_t)sa * inv_x + (1ULL << 31)) >> 32);
                trow[x] = (ob > 255u ? 255u : ob)
                        | ((og > 255u ? 255u : og) << 8)
                        | ((or_ > 255u ? 255u : or_) << 16)
                        | ((oa > 255u ? 255u : oa) << 24);
                int32_t ex = x + rx + 1; if (ex >= w) ex = w - 1;
                int32_t lx = x - rx;     if (lx < 0) lx = 0;
                uint32_t pe = srow[ex], pl = srow[lx];
                sb += (int32_t)(pe & 0xFFu) - (int32_t)(pl & 0xFFu);
                sg += (int32_t)((pe >> 8) & 0xFFu) - (int32_t)((pl >> 8) & 0xFFu);
                sr += (int32_t)((pe >> 16) & 0xFFu) - (int32_t)((pl >> 16) & 0xFFu);
                sa += (int32_t)((pe >> 24) & 0xFFu) - (int32_t)((pl >> 24) & 0xFFu);
            }
        }

        /* 垂直通行（行主序扫描，保持 L1 连续访存）：tmp -> pixels */
        const uint32_t *top0 = tmp;
        for (int32_t x = 0; x < w; x++) {
            uint32_t p = top0[x];
            int32_t *cs = col_sum + (size_t)x * 4;
            cs[0] = (int32_t)(p & 0xFFu) * (ry + 1);
            cs[1] = (int32_t)((p >> 8) & 0xFFu) * (ry + 1);
            cs[2] = (int32_t)((p >> 16) & 0xFFu) * (ry + 1);
            cs[3] = (int32_t)((p >> 24) & 0xFFu) * (ry + 1);
        }
        for (int32_t k = 1; k <= ry; k++) {
            const uint32_t *rowk = tmp + (size_t)(k < h ? k : h - 1) * (size_t)w;
            for (int32_t x = 0; x < w; x++) {
                uint32_t p = rowk[x];
                int32_t *cs = col_sum + (size_t)x * 4;
                cs[0] += (int32_t)(p & 0xFFu);
                cs[1] += (int32_t)((p >> 8) & 0xFFu);
                cs[2] += (int32_t)((p >> 16) & 0xFFu);
                cs[3] += (int32_t)((p >> 24) & 0xFFu);
            }
        }
        for (int32_t y = 0; y < h; y++) {
            uint32_t *drow = pixels + (size_t)y * (size_t)w;
            int32_t ey = y + ry + 1; if (ey >= h) ey = h - 1;
            int32_t ly = y - ry;     if (ly < 0) ly = 0;
            const uint32_t *erow = tmp + (size_t)ey * (size_t)w;
            const uint32_t *lrow = tmp + (size_t)ly * (size_t)w;
            for (int32_t x = 0; x < w; x++) {
                int32_t *cs = col_sum + (size_t)x * 4;
                uint32_t ob = (uint32_t)(((uint64_t)(uint32_t)cs[0] * inv_y + (1ULL << 31)) >> 32);
                uint32_t og = (uint32_t)(((uint64_t)(uint32_t)cs[1] * inv_y + (1ULL << 31)) >> 32);
                uint32_t or_ = (uint32_t)(((uint64_t)(uint32_t)cs[2] * inv_y + (1ULL << 31)) >> 32);
                uint32_t oa = (uint32_t)(((uint64_t)(uint32_t)cs[3] * inv_y + (1ULL << 31)) >> 32);
                drow[x] = (ob > 255u ? 255u : ob)
                        | ((og > 255u ? 255u : og) << 8)
                        | ((or_ > 255u ? 255u : or_) << 16)
                        | ((oa > 255u ? 255u : oa) << 24);
                uint32_t pe = erow[x], pl = lrow[x];
                cs[0] += (int32_t)(pe & 0xFFu) - (int32_t)(pl & 0xFFu);
                cs[1] += (int32_t)((pe >> 8) & 0xFFu) - (int32_t)((pl >> 8) & 0xFFu);
                cs[2] += (int32_t)((pe >> 16) & 0xFFu) - (int32_t)((pl >> 16) & 0xFFu);
                cs[3] += (int32_t)((pe >> 24) & 0xFFu) - (int32_t)((pl >> 24) & 0xFFu);
            }
        }
    }

    free(col_sum);
    free(tmp);
    return 1;
}

/* 3x3 卷积锐化（amount_pct: 1..300，保留 Alpha 与图像边缘） */
ZAN_IMAGE_EXPORT int32_t zan_image_sharpen_argb(uint32_t *pixels, int32_t w, int32_t h,
                                                int32_t amount_pct) {
    if (!pixels || !zan_img_dimensions_ok(w, h) || w < 3 || h < 3 || amount_pct <= 0)
        return 0;
    if (amount_pct > 300) amount_pct = 300;
    int32_t k256 = (amount_pct * 256) / 100;

    uint32_t *copy = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!copy) return 0;
    memcpy(copy, pixels, (size_t)w * (size_t)h * sizeof(uint32_t));

    for (int32_t y = 1; y < h - 1; y++) {
        const uint32_t *r0 = copy + (size_t)(y - 1) * (size_t)w;
        const uint32_t *r1 = copy + (size_t)y * (size_t)w;
        const uint32_t *r2 = copy + (size_t)(y + 1) * (size_t)w;
        uint32_t *drow = pixels + (size_t)y * (size_t)w;
        for (int32_t x = 1; x < w - 1; x++) {
            uint32_t c = r1[x], u = r0[x], d = r2[x], l = r1[x - 1], r = r1[x + 1];
            int32_t cb = (int32_t)(c & 0xFFu);
            int32_t cg = (int32_t)((c >> 8) & 0xFFu);
            int32_t cr = (int32_t)((c >> 16) & 0xFFu);

            int32_t lb = 4 * cb - (int32_t)(u & 0xFFu) - (int32_t)(d & 0xFFu)
                       - (int32_t)(l & 0xFFu) - (int32_t)(r & 0xFFu);
            int32_t lg = 4 * cg - (int32_t)((u >> 8) & 0xFFu) - (int32_t)((d >> 8) & 0xFFu)
                       - (int32_t)((l >> 8) & 0xFFu) - (int32_t)((r >> 8) & 0xFFu);
            int32_t lr = 4 * cr - (int32_t)((u >> 16) & 0xFFu) - (int32_t)((d >> 16) & 0xFFu)
                       - (int32_t)((l >> 16) & 0xFFu) - (int32_t)((r >> 16) & 0xFFu);

            uint32_t ob = zan_clamp_u8(cb + ((lb * k256) >> 8));
            uint32_t og = zan_clamp_u8(cg + ((lg * k256) >> 8));
            uint32_t or_ = zan_clamp_u8(cr + ((lr * k256) >> 8));
            drow[x] = ob | (og << 8) | (or_ << 16) | (c & 0xFF000000u);
        }
    }

    free(copy);
    return 1;
}

/* 带透明度缩放 (alpha_scale: 0..255) 的高速 Porter-Duff Over 合成（供水印/图层叠加使用） */
ZAN_IMAGE_EXPORT int32_t zan_image_blit_argb(uint32_t *dst, int32_t dw, int32_t dh,
                                             const uint32_t *src, int32_t sw, int32_t sh,
                                             int32_t dx, int32_t dy, int32_t alpha_scale) {
    if (!dst || !src || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0 || alpha_scale <= 0)
        return 0;
    if (alpha_scale > 255) alpha_scale = 255;

    int32_t sx = 0, sy = 0, cw = sw, ch = sh;
    if (dx < 0) { sx -= dx; cw += dx; dx = 0; }
    if (dy < 0) { sy -= dy; ch += dy; dy = 0; }
    if (dx + cw > dw) cw = dw - dx;
    if (dy + ch > dh) ch = dh - dy;
    if (cw <= 0 || ch <= 0) return 1;

    for (int32_t y = 0; y < ch; y++) {
        uint32_t *drow = dst + (size_t)(dy + y) * (size_t)dw + (size_t)dx;
        const uint32_t *srow = src + (size_t)(sy + y) * (size_t)sw + (size_t)sx;
        for (int32_t x = 0; x < cw; x++) {
            uint32_t sp = srow[x];
            uint32_t sa = (sp >> 24) & 0xFFu;
            if (alpha_scale < 255) sa = (sa * (uint32_t)alpha_scale + 127u) / 255u;
            if (sa == 0) continue;
            if (sa == 255) {
                drow[x] = (sp & 0x00FFFFFFu) | 0xFF000000u;
                continue;
            }
            uint32_t dp = drow[x];
            uint32_t inv = 255u - sa;
            uint32_t da = (dp >> 24) & 0xFFu;
            uint32_t out_a = sa + ((da * inv + 127u) * 257u >> 16);
            uint32_t b = (((sp & 0xFFu) * sa + (dp & 0xFFu) * inv + 127u) * 257u) >> 16;
            uint32_t g = ((((sp >> 8) & 0xFFu) * sa + ((dp >> 8) & 0xFFu) * inv + 127u) * 257u) >> 16;
            uint32_t r = ((((sp >> 16) & 0xFFu) * sa + ((dp >> 16) & 0xFFu) * inv + 127u) * 257u) >> 16;
            drow[x] = (b & 0xFFu) | ((g & 0xFFu) << 8) | ((r & 0xFFu) << 16) | ((out_a & 0xFFu) << 24);
        }
    }
    return 1;
}
