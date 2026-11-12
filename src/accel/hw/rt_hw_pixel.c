/* rt_hw_pixel.c: SIMD 像素 Alpha 混合、色彩通道交换与栅格化图形加速实现 */


#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))

__attribute__((target("sse2")))
static void pixel_blend_over_sse2(uint8_t *dst, const uint8_t *src, int64_t count) {
    int64_t i = 0;
    __m128i zero = _mm_setzero_si128();
    __m128i k255 = _mm_set1_epi16(255);
    __m128i k128 = _mm_set1_epi16(128);
    __m128i rgb_mask = _mm_setr_epi16(-1, -1, -1, 0, -1, -1, -1, 0);
    __m128i k_alpha_255 = _mm_setr_epi16(0, 0, 0, 255, 0, 0, 0, 255);

    for (; i + 4 <= count; i += 4) {
        __m128i s = _mm_loadu_si128((const __m128i*)(src + i * 4));
        __m128i d = _mm_loadu_si128((const __m128i*)(dst + i * 4));

        __m128i s_lo = _mm_unpacklo_epi8(s, zero);
        __m128i d_lo = _mm_unpacklo_epi8(d, zero);
        __m128i s_hi = _mm_unpackhi_epi8(s, zero);
        __m128i d_hi = _mm_unpackhi_epi8(d, zero);

        __m128i sa_0 = _mm_shufflelo_epi16(s_lo, _MM_SHUFFLE(3, 3, 3, 3));
        sa_0 = _mm_shufflehi_epi16(sa_0, _MM_SHUFFLE(3, 3, 3, 3));
        __m128i inv_sa_0 = _mm_sub_epi16(k255, sa_0);
        __m128i sw_0 = _mm_or_si128(_mm_and_si128(sa_0, rgb_mask), k_alpha_255);

        __m128i res_lo = _mm_add_epi16(_mm_mullo_epi16(s_lo, sw_0), _mm_mullo_epi16(d_lo, inv_sa_0));
        res_lo = _mm_add_epi16(res_lo, k128);
        res_lo = _mm_srli_epi16(_mm_add_epi16(res_lo, _mm_srli_epi16(res_lo, 8)), 8);

        __m128i sa_1 = _mm_shufflelo_epi16(s_hi, _MM_SHUFFLE(3, 3, 3, 3));
        sa_1 = _mm_shufflehi_epi16(sa_1, _MM_SHUFFLE(3, 3, 3, 3));
        __m128i inv_sa_1 = _mm_sub_epi16(k255, sa_1);
        __m128i sw_1 = _mm_or_si128(_mm_and_si128(sa_1, rgb_mask), k_alpha_255);

        __m128i res_hi = _mm_add_epi16(_mm_mullo_epi16(s_hi, sw_1), _mm_mullo_epi16(d_hi, inv_sa_1));
        res_hi = _mm_add_epi16(res_hi, k128);
        res_hi = _mm_srli_epi16(_mm_add_epi16(res_hi, _mm_srli_epi16(res_hi, 8)), 8);

        __m128i out = _mm_packus_epi16(res_lo, res_hi);
        _mm_storeu_si128((__m128i*)(dst + i * 4), out);
    }
    for (; i < count; i++) {
        uint32_t sp, dp;
        memcpy(&sp, src + i * 4, 4);
        memcpy(&dp, dst + i * 4, 4);
        uint32_t sa = (sp >> 24) & 0xFF;
        uint32_t da = (dp >> 24) & 0xFF;
        uint32_t inv_a = 255 - sa;
        uint32_t out_a = sa + (da * inv_a + 127) / 255;
        uint32_t sr = (sp >> 16) & 0xFF, dr = (dp >> 16) & 0xFF;
        uint32_t sg = (sp >> 8)  & 0xFF, dg = (dp >> 8)  & 0xFF;
        uint32_t sb = sp & 0xFF,         db = dp & 0xFF;
        uint32_t out_r = (sr * sa + dr * inv_a + 127) / 255;
        uint32_t out_g = (sg * sa + dg * inv_a + 127) / 255;
        uint32_t out_b = (sb * sa + db * inv_a + 127) / 255;
        uint32_t outp = (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
        memcpy(dst + i * 4, &outp, 4);
    }
}

__attribute__((target("ssse3")))
static void pixel_swap_rb_ssse3(uint8_t *dst, const uint8_t *src, int64_t count) {
    int64_t i = 0;
    __m128i mask = _mm_setr_epi8(2, 1, 0, 3,  6, 5, 4, 7,  10, 9, 8, 11,  14, 13, 12, 15);
    for (; i + 4 <= count; i += 4) {
        __m128i v = _mm_loadu_si128((const __m128i*)(src + i * 4));
        v = _mm_shuffle_epi8(v, mask);
        _mm_storeu_si128((__m128i*)(dst + i * 4), v);
    }
    for (; i < count; i++) {
        uint32_t p;
        memcpy(&p, src + i * 4, 4);
        uint32_t swapped = (p & 0xFF00FF00u) | ((p & 0x00FF0000u) >> 16) | ((p & 0x000000FFu) << 16);
        memcpy(dst + i * 4, &swapped, 4);
    }
}

#endif

void zan_hw_pixel_blend_over(uint8_t *dst, const uint8_t *src, int64_t count) {
    if (count <= 0 || !dst || !src) return;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    pixel_blend_over_sse2(dst, src, count);
#else
    for (int64_t i = 0; i < count; i++) {
        uint32_t sp, dp;
        memcpy(&sp, src + i * 4, 4);
        memcpy(&dp, dst + i * 4, 4);
        uint32_t sa = (sp >> 24) & 0xFF;
        uint32_t da = (dp >> 24) & 0xFF;
        uint32_t inv_a = 255 - sa;
        uint32_t out_a = sa + (da * inv_a + 127) / 255;
        uint32_t sr = (sp >> 16) & 0xFF, dr = (dp >> 16) & 0xFF;
        uint32_t sg = (sp >> 8)  & 0xFF, dg = (dp >> 8)  & 0xFF;
        uint32_t sb = sp & 0xFF,         db = dp & 0xFF;
        uint32_t out_r = (sr * sa + dr * inv_a + 127) / 255;
        uint32_t out_g = (sg * sa + dg * inv_a + 127) / 255;
        uint32_t out_b = (sb * sa + db * inv_a + 127) / 255;
        uint32_t outp = (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
        memcpy(dst + i * 4, &outp, 4);
    }
#endif
}

void zan_hw_pixel_swap_rb(uint8_t *dst, const uint8_t *src, int64_t count) {
    if (count <= 0 || !dst || !src) return;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_x86_can(ZAN_HW_REQ_PIXEL)) {
        pixel_swap_rb_ssse3(dst, src, count);
        return;
    }
#endif
    for (int64_t i = 0; i < count; i++) {
        uint32_t p;
        memcpy(&p, src + i * 4, 4);
        uint32_t swapped = (p & 0xFF00FF00u) | ((p & 0x00FF0000u) >> 16) | ((p & 0x000000FFu) << 16);
        memcpy(dst + i * 4, &swapped, 4);
    }
}

void zan_hw_pixel_fill_rect(uint8_t *dst, int64_t stride, int64_t x, int64_t y,
                            int64_t w, int64_t h, uint32_t color) {
    if (w <= 0 || h <= 0 || !dst) return;
    for (int64_t r = 0; r < h; r++) {
        uint32_t *row = (uint32_t*)(dst + (y + r) * stride + x * 4);
        for (int64_t c = 0; c < w; c++) {
            row[c] = color;
        }
    }
}

static inline uint32_t bilerp_ch(uint32_t c00, uint32_t c01, uint32_t c10, uint32_t c11,
                                 uint32_t wx, uint32_t inv_wx, uint32_t wy, uint32_t inv_wy) {
    uint32_t top = (c00 * inv_wx + c01 * wx + 128) >> 8;
    uint32_t bot = (c10 * inv_wx + c11 * wx + 128) >> 8;
    return (top * inv_wy + bot * wy + 128) >> 8;
}

void zan_hw_pixel_resample_bilinear_row(uint8_t *dst, const uint8_t *src0,
                                       const uint8_t *src1, const int32_t *x_idx,
                                       const int32_t *x_wt, int32_t wy, int64_t width) {
    if (width <= 0 || !dst || !src0 || !src1 || !x_idx || !x_wt) return;
    uint32_t *dst_px = (uint32_t*)dst;
    const uint32_t *s0 = (const uint32_t*)src0;
    const uint32_t *s1 = (const uint32_t*)src1;
    uint32_t inv_wy = 256 - (uint32_t)wy;

    for (int64_t c = 0; c < width; c++) {
        int64_t sx = (int64_t)x_idx[c];
        uint32_t wx = (uint32_t)x_wt[c];
        uint32_t inv_wx = 256 - wx;

        uint32_t p00 = s0[sx];
        uint32_t p01 = s0[sx + 1];
        uint32_t p10 = s1[sx];
        uint32_t p11 = s1[sx + 1];

        uint32_t a = bilerp_ch((p00 >> 24) & 0xFF, (p01 >> 24) & 0xFF, (p10 >> 24) & 0xFF, (p11 >> 24) & 0xFF, wx, inv_wx, wy, inv_wy);
        uint32_t r = bilerp_ch((p00 >> 16) & 0xFF, (p01 >> 16) & 0xFF, (p10 >> 16) & 0xFF, (p11 >> 16) & 0xFF, wx, inv_wx, wy, inv_wy);
        uint32_t g = bilerp_ch((p00 >> 8)  & 0xFF, (p01 >> 8)  & 0xFF, (p10 >> 8)  & 0xFF, (p11 >> 8)  & 0xFF, wx, inv_wx, wy, inv_wy);
        uint32_t b = bilerp_ch(p00 & 0xFF,         p01 & 0xFF,         p10 & 0xFF,         p11 & 0xFF,         wx, inv_wx, wy, inv_wy);

        dst_px[c] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}
