/* zan_image_encode.c -- 零外部依赖 ARGB32 图像编码器（JPEG Baseline / BMP / TGA）
 * 被 zan_image.c 以 unity 方式包含。 */

typedef struct zan_enc_buf {
    uint8_t *data;
    size_t len;
    size_t cap;
    int oom;
} zan_enc_buf_t;

static void zan_enc_reserve(zan_enc_buf_t *b, size_t extra) {
    if (b->oom) return;
    if (extra > SIZE_MAX - b->len) { b->oom = 1; return; }
    size_t need = b->len + extra;
    if (need <= b->cap) return;
    size_t ncap = b->cap ? b->cap : 4096;
    while (ncap < need) {
        if (ncap > SIZE_MAX / 2) { ncap = need; break; }
        ncap *= 2;
    }
    uint8_t *p = (uint8_t *)realloc(b->data, ncap);
    if (!p) { b->oom = 1; return; }
    b->data = p;
    b->cap = ncap;
}

static void zan_enc_put1(zan_enc_buf_t *b, uint8_t v) {
    if (b->len >= b->cap) zan_enc_reserve(b, 1);
    if (!b->oom) b->data[b->len++] = v;
}

static void zan_enc_put_bytes(zan_enc_buf_t *b, const void *src, size_t n) {
    zan_enc_reserve(b, n);
    if (!b->oom && n > 0) {
        memcpy(b->data + b->len, src, n);
        b->len += n;
    }
}

static void zan_enc_put_be16(zan_enc_buf_t *b, uint16_t v) {
    zan_enc_put1(b, (uint8_t)(v >> 8));
    zan_enc_put1(b, (uint8_t)(v & 0xFF));
}

static void zan_enc_put_le16(zan_enc_buf_t *b, uint16_t v) {
    zan_enc_put1(b, (uint8_t)(v & 0xFF));
    zan_enc_put1(b, (uint8_t)(v >> 8));
}

static void zan_enc_put_le32(zan_enc_buf_t *b, uint32_t v) {
    zan_enc_put1(b, (uint8_t)(v & 0xFF));
    zan_enc_put1(b, (uint8_t)((v >> 8) & 0xFF));
    zan_enc_put1(b, (uint8_t)((v >> 16) & 0xFF));
    zan_enc_put1(b, (uint8_t)((v >> 24) & 0xFF));
}

/* ---- BMP (32-bit BGRA / 24-bit BGR) ---- */
static uint8_t *zan_encode_bmp(const uint32_t *pixels, int32_t w, int32_t h,
                               int32_t stride, int with_alpha, int32_t *out_len) {
    int bpp = with_alpha ? 4 : 3;
    int64_t row_bytes = ((int64_t)w * bpp + 3) & ~3LL;
    int64_t img_bytes = row_bytes * (int64_t)h;
    int64_t total = 54 + img_bytes;
    if (total <= 54 || total > 0x7FFFFFFFLL) return NULL;

    zan_enc_buf_t b = {0};
    zan_enc_reserve(&b, (size_t)total);
    if (b.oom) { free(b.data); return NULL; }

    /* BITMAPFILEHEADER (14 bytes) */
    zan_enc_put1(&b, 'B'); zan_enc_put1(&b, 'M');
    zan_enc_put_le32(&b, (uint32_t)total);
    zan_enc_put_le16(&b, 0);
    zan_enc_put_le16(&b, 0);
    zan_enc_put_le32(&b, 54);

    /* BITMAPINFOHEADER (40 bytes), top-down negative height */
    zan_enc_put_le32(&b, 40);
    zan_enc_put_le32(&b, (uint32_t)w);
    zan_enc_put_le32(&b, (uint32_t)(-h));
    zan_enc_put_le16(&b, 1);
    zan_enc_put_le16(&b, (uint16_t)(bpp * 8));
    zan_enc_put_le32(&b, 0);
    zan_enc_put_le32(&b, (uint32_t)img_bytes);
    zan_enc_put_le32(&b, 2835);
    zan_enc_put_le32(&b, 2835);
    zan_enc_put_le32(&b, 0);
    zan_enc_put_le32(&b, 0);

    int pad = (int)(row_bytes - (int64_t)w * bpp);
    static const uint8_t zeros[4] = {0, 0, 0, 0};
    for (int32_t y = 0; y < h; y++) {
        const uint32_t *row = pixels + (size_t)y * (size_t)stride;
        if (with_alpha) {
            for (int32_t x = 0; x < w; x++) {
                uint32_t p = row[x];
                zan_enc_put1(&b, (uint8_t)(p & 0xFF));
                zan_enc_put1(&b, (uint8_t)((p >> 8) & 0xFF));
                zan_enc_put1(&b, (uint8_t)((p >> 16) & 0xFF));
                zan_enc_put1(&b, (uint8_t)((p >> 24) & 0xFF));
            }
        } else {
            for (int32_t x = 0; x < w; x++) {
                uint32_t p = row[x];
                zan_enc_put1(&b, (uint8_t)(p & 0xFF));
                zan_enc_put1(&b, (uint8_t)((p >> 8) & 0xFF));
                zan_enc_put1(&b, (uint8_t)((p >> 16) & 0xFF));
            }
        }
        if (pad > 0) zan_enc_put_bytes(&b, zeros, (size_t)pad);
    }
    if (b.oom) { free(b.data); return NULL; }
    *out_len = (int32_t)b.len;
    return b.data;
}

/* ---- TGA (32-bit uncompressed BGRA, top-left origin) ---- */
static uint8_t *zan_encode_tga(const uint32_t *pixels, int32_t w, int32_t h,
                               int32_t stride, int32_t *out_len) {
    if (w > 65535 || h > 65535) return NULL;
    int64_t total = 18 + (int64_t)w * (int64_t)h * 4;
    if (total > 0x7FFFFFFFLL) return NULL;

    zan_enc_buf_t b = {0};
    zan_enc_reserve(&b, (size_t)total);
    if (b.oom) { free(b.data); return NULL; }

    uint8_t hdr[18] = {0};
    hdr[2] = 2; /* uncompressed true-color */
    hdr[12] = (uint8_t)(w & 0xFF);
    hdr[13] = (uint8_t)((w >> 8) & 0xFF);
    hdr[14] = (uint8_t)(h & 0xFF);
    hdr[15] = (uint8_t)((h >> 8) & 0xFF);
    hdr[16] = 32;
    hdr[17] = 0x28; /* top-left origin (0x20) + 8-bit alpha (0x08) */
    zan_enc_put_bytes(&b, hdr, 18);

    for (int32_t y = 0; y < h; y++) {
        const uint32_t *row = pixels + (size_t)y * (size_t)stride;
        for (int32_t x = 0; x < w; x++) {
            uint32_t p = row[x];
            zan_enc_put1(&b, (uint8_t)(p & 0xFF));
            zan_enc_put1(&b, (uint8_t)((p >> 8) & 0xFF));
            zan_enc_put1(&b, (uint8_t)((p >> 16) & 0xFF));
            zan_enc_put1(&b, (uint8_t)((p >> 24) & 0xFF));
        }
    }
    if (b.oom) { free(b.data); return NULL; }
    *out_len = (int32_t)b.len;
    return b.data;
}

/* ---- JPEG Baseline (4:4:4 YCbCr, ITU-T T.81 Annex K tables) ---- */
static const uint8_t zan_jpg_zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

static const uint8_t zan_jpg_std_lum_q[64] = {
    16, 11, 10, 16, 24, 40, 51, 61,
    12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68,109,103, 77,
    24, 35, 55, 64, 81,104,113, 92,
    49, 64, 78, 87,103,121,120,101,
    72, 92, 95, 98,112,100,103, 99
};

static const uint8_t zan_jpg_std_chr_q[64] = {
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99
};

static const uint8_t zan_jpg_dc_lum_bits[16] = {0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
static const uint8_t zan_jpg_dc_lum_vals[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
static const uint8_t zan_jpg_dc_chr_bits[16] = {0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
static const uint8_t zan_jpg_dc_chr_vals[12] = {0,1,2,3,4,5,6,7,8,9,10,11};

static const uint8_t zan_jpg_ac_lum_bits[16] = {0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,125};
static const uint8_t zan_jpg_ac_lum_vals[162] = {
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,
    0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
    0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
    0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
    0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
    0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
    0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
    0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
    0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
    0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
    0xf9,0xfa
};

static const uint8_t zan_jpg_ac_chr_bits[16] = {0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,119};
static const uint8_t zan_jpg_ac_chr_vals[162] = {
    0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
    0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
    0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
    0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
    0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
    0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
    0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
    0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
    0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
    0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
    0xf9,0xfa
};

typedef struct zan_jpg_huff {
    uint16_t code[256];
    uint8_t size[256];
} zan_jpg_huff_t;

typedef struct zan_jpg_bits {
    zan_enc_buf_t *out;
    uint32_t acc;
    int count;
} zan_jpg_bits_t;

static void zan_jpg_build_huff(zan_jpg_huff_t *h, const uint8_t bits[16], const uint8_t *vals) {
    memset(h, 0, sizeof(*h));
    uint32_t code = 0;
    int k = 0;
    for (int len = 1; len <= 16; len++) {
        for (int i = 0; i < bits[len - 1]; i++) {
            uint8_t sym = vals[k++];
            h->code[sym] = (uint16_t)code;
            h->size[sym] = (uint8_t)len;
            code++;
        }
        code <<= 1;
    }
}

static void zan_jpg_put_bits(zan_jpg_bits_t *bw, uint32_t bits, int n) {
    if (n <= 0) return;
    bw->acc = (bw->acc << n) | (bits & ((1u << n) - 1u));
    bw->count += n;
    while (bw->count >= 8) {
        bw->count -= 8;
        uint8_t byte = (uint8_t)((bw->acc >> bw->count) & 0xFFu);
        zan_enc_put1(bw->out, byte);
        if (byte == 0xFF) zan_enc_put1(bw->out, 0x00);
    }
}

static void zan_jpg_flush_bits(zan_jpg_bits_t *bw) {
    if (bw->count > 0) {
        int pad = 8 - bw->count;
        zan_jpg_put_bits(bw, (1u << pad) - 1u, pad);
    }
}

static void zan_jpg_write_dht(zan_enc_buf_t *b, uint8_t tc_th,
                              const uint8_t bits[16], const uint8_t *vals, int nvals) {
    zan_enc_put1(b, 0xFF); zan_enc_put1(b, 0xC4);
    zan_enc_put_be16(b, (uint16_t)(2 + 1 + 16 + nvals));
    zan_enc_put1(b, tc_th);
    zan_enc_put_bytes(b, bits, 16);
    zan_enc_put_bytes(b, vals, (size_t)nvals);
}

static int zan_jpg_bit_len(int v) {
    int n = 0;
    while (v > 0) { v >>= 1; n++; }
    return n;
}

/* 高速 1D 分离式 AAN (Arai-Agui-Nakajima) 8x8 FDCT：
 * 每 8 点仅 5 次乘法 + 29 次加减，配合预融合的量化缩放表，较朴素 2D 矩阵乘提速 3~4x。 */
static void zan_jpg_fdct8x8(const float in[64], float out[64]) {
    float tmp[64];
    for (int i = 0; i < 8; i++) {
        const float *r = in + i * 8;
        float t0 = r[0] + r[7], t7 = r[0] - r[7];
        float t1 = r[1] + r[6], t6 = r[1] - r[6];
        float t2 = r[2] + r[5], t5 = r[2] - r[5];
        float t3 = r[3] + r[4], t4 = r[3] - r[4];

        float t10 = t0 + t3, t13 = t0 - t3;
        float t11 = t1 + t2, t12 = t1 - t2;

        float *d = tmp + i * 8;
        d[0] = t10 + t11;
        d[4] = t10 - t11;

        float z1 = (t12 + t13) * 0.707106781f;
        d[2] = t13 + z1;
        d[6] = t13 - z1;

        t10 = t4 + t5;
        t11 = t5 + t6;
        t12 = t6 + t7;

        float z5 = (t10 - t12) * 0.382683433f;
        float z2 = 0.541196100f * t10 + z5;
        float z4 = 1.306562965f * t12 + z5;
        float z3 = t11 * 0.707106781f;

        float z11 = t7 + z3, z13 = t7 - z3;
        d[5] = z13 + z2;
        d[3] = z13 - z2;
        d[1] = z11 + z4;
        d[7] = z11 - z4;
    }

    for (int i = 0; i < 8; i++) {
        float s0 = tmp[0 * 8 + i], s1 = tmp[1 * 8 + i];
        float s2 = tmp[2 * 8 + i], s3 = tmp[3 * 8 + i];
        float s4 = tmp[4 * 8 + i], s5 = tmp[5 * 8 + i];
        float s6 = tmp[6 * 8 + i], s7 = tmp[7 * 8 + i];

        float t0 = s0 + s7, t7 = s0 - s7;
        float t1 = s1 + s6, t6 = s1 - s6;
        float t2 = s2 + s5, t5 = s2 - s5;
        float t3 = s3 + s4, t4 = s3 - s4;

        float t10 = t0 + t3, t13 = t0 - t3;
        float t11 = t1 + t2, t12 = t1 - t2;

        out[0 * 8 + i] = t10 + t11;
        out[4 * 8 + i] = t10 - t11;

        float z1 = (t12 + t13) * 0.707106781f;
        out[2 * 8 + i] = t13 + z1;
        out[6 * 8 + i] = t13 - z1;

        t10 = t4 + t5;
        t11 = t5 + t6;
        t12 = t6 + t7;

        float z5 = (t10 - t12) * 0.382683433f;
        float z2 = 0.541196100f * t10 + z5;
        float z4 = 1.306562965f * t12 + z5;
        float z3 = t11 * 0.707106781f;

        float z11 = t7 + z3, z13 = t7 - z3;
        out[5 * 8 + i] = z13 + z2;
        out[3 * 8 + i] = z13 - z2;
        out[1 * 8 + i] = z11 + z4;
        out[7 * 8 + i] = z11 - z4;
    }
}

static void zan_jpg_encode_block(zan_jpg_bits_t *bw, const float block[64],
                                 const float inv_q[64],
                                 int *prev_dc, const zan_jpg_huff_t *dc_h,
                                 const zan_jpg_huff_t *ac_h) {
    float dct[64];
    int qcoef[64];
    zan_jpg_fdct8x8(block, dct);
    for (int i = 0; i < 64; i++) {
        float v = dct[zan_jpg_zigzag[i]] * inv_q[i];
        qcoef[i] = (int)(v >= 0.0f ? (v + 0.5f) : (v - 0.5f));
    }

    int diff = qcoef[0] - *prev_dc;
    *prev_dc = qcoef[0];
    if (diff == 0) {
        zan_jpg_put_bits(bw, dc_h->code[0], dc_h->size[0]);
    } else {
        int abs_d = diff < 0 ? -diff : diff;
        int s = zan_jpg_bit_len(abs_d);
        int bits = diff < 0 ? (diff + (1 << s) - 1) : diff;
        zan_jpg_put_bits(bw, dc_h->code[s], dc_h->size[s]);
        zan_jpg_put_bits(bw, (uint32_t)bits, s);
    }

    int last_nz = 63;
    while (last_nz > 0 && qcoef[last_nz] == 0) last_nz--;
    int i = 1;
    while (i <= last_nz) {
        int zeros = 0;
        while (i <= last_nz && qcoef[i] == 0) { zeros++; i++; }
        while (zeros >= 16) {
            zan_jpg_put_bits(bw, ac_h->code[0xF0], ac_h->size[0xF0]);
            zeros -= 16;
        }
        int val = qcoef[i++];
        int abs_v = val < 0 ? -val : val;
        int s = zan_jpg_bit_len(abs_v);
        int sym = (zeros << 4) | s;
        int bits = val < 0 ? (val + (1 << s) - 1) : val;
        zan_jpg_put_bits(bw, ac_h->code[sym], ac_h->size[sym]);
        zan_jpg_put_bits(bw, (uint32_t)bits, s);
    }
    if (last_nz < 63) {
        zan_jpg_put_bits(bw, ac_h->code[0x00], ac_h->size[0x00]);
    }
}

static uint8_t *zan_encode_jpeg(const uint32_t *pixels, int32_t w, int32_t h,
                                int32_t stride, int32_t quality, int32_t *out_len) {
    if (w <= 0 || h <= 0 || w > 65535 || h > 65535) return NULL;
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;

    static const float aan_scale[8] = {
        1.000000000f, 1.387039845f, 1.306562965f, 1.175875602f,
        1.000000000f, 0.785694958f, 0.541196100f, 0.275899379f
    };
    int scale = quality < 50 ? (5000 / quality) : (200 - quality * 2);
    uint8_t q_lum[64], q_chr[64];
    float inv_q_lum[64], inv_q_chr[64];
    for (int i = 0; i < 64; i++) {
        int zz = zan_jpg_zigzag[i];
        int row = zz >> 3;
        int col = zz & 7;
        float aan = 8.0f * aan_scale[row] * aan_scale[col];
        int ql = (zan_jpg_std_lum_q[zz] * scale + 50) / 100;
        int qc = (zan_jpg_std_chr_q[zz] * scale + 50) / 100;
        if (ql < 1) ql = 1; else if (ql > 255) ql = 255;
        if (qc < 1) qc = 1; else if (qc > 255) qc = 255;
        q_lum[i] = (uint8_t)ql;
        q_chr[i] = (uint8_t)qc;
        inv_q_lum[i] = 1.0f / ((float)ql * aan);
        inv_q_chr[i] = 1.0f / ((float)qc * aan);
    }

    zan_jpg_huff_t dc_lum, dc_chr, ac_lum, ac_chr;
    zan_jpg_build_huff(&dc_lum, zan_jpg_dc_lum_bits, zan_jpg_dc_lum_vals);
    zan_jpg_build_huff(&dc_chr, zan_jpg_dc_chr_bits, zan_jpg_dc_chr_vals);
    zan_jpg_build_huff(&ac_lum, zan_jpg_ac_lum_bits, zan_jpg_ac_lum_vals);
    zan_jpg_build_huff(&ac_chr, zan_jpg_ac_chr_bits, zan_jpg_ac_chr_vals);

    zan_enc_buf_t b = {0};
    zan_enc_reserve(&b, 4096);

    /* SOI + APP0 (JFIF 1.01) */
    zan_enc_put1(&b, 0xFF); zan_enc_put1(&b, 0xD8);
    static const uint8_t app0[18] = {
        0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00,
        0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00
    };
    zan_enc_put_bytes(&b, app0, sizeof(app0));

    /* DQT (table 0: lum, table 1: chr) */
    zan_enc_put1(&b, 0xFF); zan_enc_put1(&b, 0xDB);
    zan_enc_put_be16(&b, 67);
    zan_enc_put1(&b, 0);
    zan_enc_put_bytes(&b, q_lum, 64);

    zan_enc_put1(&b, 0xFF); zan_enc_put1(&b, 0xDB);
    zan_enc_put_be16(&b, 67);
    zan_enc_put1(&b, 1);
    zan_enc_put_bytes(&b, q_chr, 64);

    /* SOF0 (8-bit, 3 components, 4:4:4) */
    zan_enc_put1(&b, 0xFF); zan_enc_put1(&b, 0xC0);
    zan_enc_put_be16(&b, 17);
    zan_enc_put1(&b, 8);
    zan_enc_put_be16(&b, (uint16_t)h);
    zan_enc_put_be16(&b, (uint16_t)w);
    zan_enc_put1(&b, 3);
    zan_enc_put1(&b, 1); zan_enc_put1(&b, 0x11); zan_enc_put1(&b, 0);
    zan_enc_put1(&b, 2); zan_enc_put1(&b, 0x11); zan_enc_put1(&b, 1);
    zan_enc_put1(&b, 3); zan_enc_put1(&b, 0x11); zan_enc_put1(&b, 1);

    /* DHT */
    zan_jpg_write_dht(&b, 0x00, zan_jpg_dc_lum_bits, zan_jpg_dc_lum_vals, 12);
    zan_jpg_write_dht(&b, 0x10, zan_jpg_ac_lum_bits, zan_jpg_ac_lum_vals, 162);
    zan_jpg_write_dht(&b, 0x01, zan_jpg_dc_chr_bits, zan_jpg_dc_chr_vals, 12);
    zan_jpg_write_dht(&b, 0x11, zan_jpg_ac_chr_bits, zan_jpg_ac_chr_vals, 162);

    /* SOS */
    static const uint8_t sos[14] = {
        0xFF, 0xDA, 0x00, 0x0C, 0x03,
        0x01, 0x00, 0x02, 0x11, 0x03, 0x11,
        0x00, 0x3F, 0x00
    };
    zan_enc_put_bytes(&b, sos, sizeof(sos));

    zan_jpg_bits_t bw = { &b, 0, 0 };
    int dc_y = 0, dc_cb = 0, dc_cr = 0;
    float yb[64], cbb[64], crb[64];

    for (int32_t by = 0; by < h; by += 8) {
        for (int32_t bx = 0; bx < w; bx += 8) {
            for (int dy = 0; dy < 8; dy++) {
                int32_t py = by + dy;
                if (py >= h) py = h - 1;
                const uint32_t *row = pixels + (size_t)py * (size_t)stride;
                for (int dx = 0; dx < 8; dx++) {
                    int32_t px = bx + dx;
                    if (px >= w) px = w - 1;
                    uint32_t p = row[px];
                    float r = (float)((p >> 16) & 0xFF);
                    float g = (float)((p >> 8) & 0xFF);
                    float bl = (float)(p & 0xFF);
                    int idx = dy * 8 + dx;
                    yb[idx]  =  0.299000f * r + 0.587000f * g + 0.114000f * bl - 128.0f;
                    cbb[idx] = -0.168736f * r - 0.331264f * g + 0.500000f * bl;
                    crb[idx] =  0.500000f * r - 0.418688f * g - 0.081312f * bl;
                }
            }
            zan_jpg_encode_block(&bw, yb,  inv_q_lum, &dc_y,  &dc_lum, &ac_lum);
            zan_jpg_encode_block(&bw, cbb, inv_q_chr, &dc_cb, &dc_chr, &ac_chr);
            zan_jpg_encode_block(&bw, crb, inv_q_chr, &dc_cr, &dc_chr, &ac_chr);
            if (b.oom) { free(b.data); return NULL; }
        }
    }

    zan_jpg_flush_bits(&bw);
    zan_enc_put1(&b, 0xFF); zan_enc_put1(&b, 0xD9);
    if (b.oom || b.len > 0x7FFFFFFF) { free(b.data); return NULL; }
    *out_len = (int32_t)b.len;
    return b.data;
}

/* ---- 高速原生 C11 PNG 编码器（Sub 滤波 + 4096 哈希 LZ77 + Fixed Huffman Deflate） ---- */
static uint32_t zan_png_crc32(const uint8_t *buf, size_t len) {
    static uint32_t tbl[256];
    static int ready = 0;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            tbl[i] = c;
        }
        ready = 1;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) c = tbl[(c ^ buf[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static uint32_t zan_png_adler32(const uint8_t *buf, size_t len) {
    uint32_t s1 = 1, s2 = 0;
    size_t i = 0;
    while (i < len) {
        size_t chunk = len - i;
        if (chunk > 5552) chunk = 5552;
        size_t end = i + chunk;
        while (i < end) {
            s1 += buf[i++];
            s2 += s1;
        }
        s1 %= 65521u;
        s2 %= 65521u;
    }
    return (s2 << 16) | s1;
}

static void zan_enc_put_be32(zan_enc_buf_t *b, uint32_t v) {
    zan_enc_put1(b, (uint8_t)((v >> 24) & 0xFFu));
    zan_enc_put1(b, (uint8_t)((v >> 16) & 0xFFu));
    zan_enc_put1(b, (uint8_t)((v >> 8) & 0xFFu));
    zan_enc_put1(b, (uint8_t)(v & 0xFFu));
}

typedef struct zan_def_bits {
    zan_enc_buf_t *out;
    uint32_t acc;
    int count;
} zan_def_bits_t;

static inline void zan_def_put(zan_def_bits_t *bw, uint32_t bits, int n) {
    bw->acc |= (bits << bw->count);
    bw->count += n;
    while (bw->count >= 8) {
        zan_enc_put1(bw->out, (uint8_t)(bw->acc & 0xFFu));
        bw->acc >>= 8;
        bw->count -= 8;
    }
}

static inline uint32_t zan_bitrev(uint32_t v, int bits) {
    uint32_t r = 0;
    for (int i = 0; i < bits; i++) {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

static void zan_def_emit_lit(zan_def_bits_t *bw, int sym) {
    if (sym <= 143) {
        zan_def_put(bw, zan_bitrev((uint32_t)(0x30 + sym), 8), 8);
    } else if (sym <= 255) {
        zan_def_put(bw, zan_bitrev((uint32_t)(0x190 + (sym - 144)), 9), 9);
    } else if (sym <= 279) {
        zan_def_put(bw, zan_bitrev((uint32_t)(sym - 256), 7), 7);
    } else {
        zan_def_put(bw, zan_bitrev((uint32_t)(0xC0 + (sym - 280)), 8), 8);
    }
}

static void zan_def_emit_match(zan_def_bits_t *bw, int len, int dist) {
    static const int len_base[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
        35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    static const int len_eb[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
        3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static const int dist_base[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
        257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static const int dist_eb[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
        7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };
    int li = 0;
    while (li < 28 && len >= len_base[li + 1]) li++;
    zan_def_emit_lit(bw, 257 + li);
    if (len_eb[li] > 0) zan_def_put(bw, (uint32_t)(len - len_base[li]), len_eb[li]);

    int di = 0;
    while (di < 29 && dist >= dist_base[di + 1]) di++;
    zan_def_put(bw, zan_bitrev((uint32_t)di, 5), 5);
    if (dist_eb[di] > 0) zan_def_put(bw, (uint32_t)(dist - dist_base[di]), dist_eb[di]);
}

static void zan_png_write_chunk(zan_enc_buf_t *b, const char tag[4],
                                const uint8_t *payload, size_t len) {
    zan_enc_put_be32(b, (uint32_t)len);
    size_t start = b->len;
    zan_enc_put_bytes(b, tag, 4);
    if (len > 0) zan_enc_put_bytes(b, payload, len);
    if (!b->oom) {
        uint32_t crc = zan_png_crc32(b->data + start, 4 + len);
        zan_enc_put_be32(b, crc);
    }
}

static uint8_t *zan_encode_png(const uint32_t *pixels, int32_t w, int32_t h,
                               int32_t stride, int32_t *out_len) {
    int64_t row_raw = 1 + (int64_t)w * 4;
    int64_t raw_size = row_raw * (int64_t)h;
    if (raw_size <= 0 || raw_size > 0x3FFFFFFFLL) return NULL;

    uint8_t *raw = (uint8_t *)malloc((size_t)raw_size);
    if (!raw) return NULL;

    /* 单遍 ARGB32 -> RGBA + PNG Sub 滤波（高压缩比且零额外整图缓冲） */
    for (int32_t y = 0; y < h; y++) {
        const uint32_t *srow = pixels + (size_t)y * (size_t)stride;
        uint8_t *drow = raw + (size_t)y * (size_t)row_raw;
        drow[0] = 1; /* Filter type 1 = Sub */
        uint8_t pr = 0, pg = 0, pb = 0, pa = 0;
        uint8_t *p = drow + 1;
        for (int32_t x = 0; x < w; x++) {
            uint32_t px = srow[x];
            uint8_t b = (uint8_t)(px & 0xFFu);
            uint8_t g = (uint8_t)((px >> 8) & 0xFFu);
            uint8_t r = (uint8_t)((px >> 16) & 0xFFu);
            uint8_t a = (uint8_t)((px >> 24) & 0xFFu);
            p[0] = (uint8_t)(r - pr);
            p[1] = (uint8_t)(g - pg);
            p[2] = (uint8_t)(b - pb);
            p[3] = (uint8_t)(a - pa);
            pr = r; pg = g; pb = b; pa = a;
            p += 4;
        }
    }

    zan_enc_buf_t zbuf = {0};
    zan_enc_reserve(&zbuf, (size_t)(raw_size / 2 + 1024));
    /* zlib header: CMF=0x78 (deflate 32K), FLG=0x01 */
    zan_enc_put1(&zbuf, 0x78);
    zan_enc_put1(&zbuf, 0x01);

    /* 单块 Fixed Huffman + 快速 4096 哈希 LZ77 压缩 */
    int32_t *head = (int32_t *)malloc(4096 * sizeof(int32_t));
    if (!head) { free(raw); free(zbuf.data); return NULL; }
    for (int i = 0; i < 4096; i++) head[i] = -1;

    zan_def_bits_t bw = { &zbuf, 0, 0 };
    zan_def_put(&bw, 1, 1); /* BFINAL = 1 */
    zan_def_put(&bw, 1, 2); /* BTYPE = 01 (fixed Huffman) */

    size_t n = (size_t)raw_size;
    size_t pos = 0;
    while (pos < n) {
        int best_len = 0;
        int best_dist = 0;
        if (pos + 3 <= n) {
            uint32_t h3 = ((uint32_t)raw[pos] << 16) | ((uint32_t)raw[pos + 1] << 8) | (uint32_t)raw[pos + 2];
            uint32_t hv = (h3 * 2654435761u) >> 20;
            int32_t prev = head[hv];
            head[hv] = (int32_t)pos;
            if (prev >= 0) {
                size_t dist = pos - (size_t)prev;
                if (dist > 0 && dist <= 32768) {
                    size_t max_l = n - pos;
                    if (max_l > 258) max_l = 258;
                    size_t l = 0;
                    const uint8_t *a = raw + (size_t)prev;
                    const uint8_t *b2 = raw + pos;
                    while (l < max_l && a[l] == b2[l]) l++;
                    if (l >= 3) {
                        best_len = (int)l;
                        best_dist = (int)dist;
                    }
                }
            }
        }
        if (best_len >= 3) {
            zan_def_emit_match(&bw, best_len, best_dist);
            size_t end_m = pos + (size_t)best_len;
            pos++;
            while (pos < end_m && pos + 3 <= n) {
                uint32_t h3 = ((uint32_t)raw[pos] << 16) | ((uint32_t)raw[pos + 1] << 8) | (uint32_t)raw[pos + 2];
                head[(h3 * 2654435761u) >> 20] = (int32_t)pos;
                pos++;
            }
            pos = end_m;
        } else {
            zan_def_emit_lit(&bw, raw[pos++]);
        }
        if (zbuf.oom) break;
    }
    zan_def_emit_lit(&bw, 256); /* EOB */
    if (bw.count > 0) zan_enc_put1(&zbuf, (uint8_t)(bw.acc & 0xFFu));

    free(head);
    uint32_t adler = zan_png_adler32(raw, n);
    free(raw);
    zan_enc_put_be32(&zbuf, adler);
    if (zbuf.oom) { free(zbuf.data); return NULL; }

    /* 组装完整 PNG 容器 */
    zan_enc_buf_t out = {0};
    zan_enc_reserve(&out, zbuf.len + 128);
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    zan_enc_put_bytes(&out, sig, 8);

    uint8_t ihdr[13];
    ihdr[0] = (uint8_t)((w >> 24) & 0xFF); ihdr[1] = (uint8_t)((w >> 16) & 0xFF);
    ihdr[2] = (uint8_t)((w >> 8) & 0xFF);  ihdr[3] = (uint8_t)(w & 0xFF);
    ihdr[4] = (uint8_t)((h >> 24) & 0xFF); ihdr[5] = (uint8_t)((h >> 16) & 0xFF);
    ihdr[6] = (uint8_t)((h >> 8) & 0xFF);  ihdr[7] = (uint8_t)(h & 0xFF);
    ihdr[8] = 8; /* bit depth */
    ihdr[9] = 6; /* RGBA */
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    zan_png_write_chunk(&out, "IHDR", ihdr, 13);
    zan_png_write_chunk(&out, "IDAT", zbuf.data, zbuf.len);
    zan_png_write_chunk(&out, "IEND", NULL, 0);
    free(zbuf.data);

    if (out.oom || out.len > 0x7FFFFFFF) { free(out.data); return NULL; }
    *out_len = (int32_t)out.len;
    return out.data;
}

ZAN_IMAGE_EXPORT uint8_t *zan_image_encode_argb(const uint32_t *pixels,
                                                int32_t w, int32_t h, int32_t stride,
                                                int32_t format, int32_t quality,
                                                int32_t *out_len) {
    if (out_len) *out_len = 0;
    if (!pixels || !out_len || !zan_img_dimensions_ok(w, h) || stride < w) return NULL;
    switch (format) {
        case 1: return zan_encode_png(pixels, w, h, stride, out_len);
        case 2: return zan_encode_jpeg(pixels, w, h, stride, quality, out_len);
        case 3: return zan_encode_bmp(pixels, w, h, stride, 1, out_len);
        case 4: return zan_encode_tga(pixels, w, h, stride, out_len);
        case 5: return zan_encode_bmp(pixels, w, h, stride, 0, out_len);
        default: return NULL;
    }
}

ZAN_IMAGE_EXPORT void zan_image_free_bytes(void *ptr) {
    free(ptr);
}
