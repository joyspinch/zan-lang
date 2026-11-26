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

/* 8x8 可分离浮点 FDCT（预计算 cos 表，稳定高精度） */
static void zan_jpg_fdct8x8(const float in[64], float out[64], const float cos_tbl[8][8]) {
    float tmp[64];
    for (int y = 0; y < 8; y++) {
        const float *row = in + y * 8;
        for (int u = 0; u < 8; u++) {
            float sum = 0.0f;
            for (int x = 0; x < 8; x++) sum += row[x] * cos_tbl[u][x];
            tmp[y * 8 + u] = sum;
        }
    }
    for (int u = 0; u < 8; u++) {
        for (int v = 0; v < 8; v++) {
            float sum = 0.0f;
            for (int y = 0; y < 8; y++) sum += tmp[y * 8 + u] * cos_tbl[v][y];
            out[v * 8 + u] = sum;
        }
    }
}

static void zan_jpg_encode_block(zan_jpg_bits_t *bw, const float block[64],
                                 const float inv_q[64], const float cos_tbl[8][8],
                                 int *prev_dc, const zan_jpg_huff_t *dc_h,
                                 const zan_jpg_huff_t *ac_h) {
    float dct[64];
    int qcoef[64];
    zan_jpg_fdct8x8(block, dct, cos_tbl);
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

    int scale = quality < 50 ? (5000 / quality) : (200 - quality * 2);
    uint8_t q_lum[64], q_chr[64];
    float inv_q_lum[64], inv_q_chr[64];
    for (int i = 0; i < 64; i++) {
        int zz = zan_jpg_zigzag[i];
        int ql = (zan_jpg_std_lum_q[zz] * scale + 50) / 100;
        int qc = (zan_jpg_std_chr_q[zz] * scale + 50) / 100;
        if (ql < 1) ql = 1; else if (ql > 255) ql = 255;
        if (qc < 1) qc = 1; else if (qc > 255) qc = 255;
        q_lum[i] = (uint8_t)ql;
        q_chr[i] = (uint8_t)qc;
        inv_q_lum[i] = 1.0f / (float)ql;
        inv_q_chr[i] = 1.0f / (float)qc;
    }

    float cos_tbl[8][8];
    const float pi = 3.14159265358979323846f;
    for (int u = 0; u < 8; u++) {
        float cu = (u == 0) ? 0.35355339059327376f : 0.5f;
        for (int x = 0; x < 8; x++) {
            cos_tbl[u][x] = cu * cosf(((2.0f * (float)x + 1.0f) * (float)u * pi) / 16.0f);
        }
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
            zan_jpg_encode_block(&bw, yb,  inv_q_lum, cos_tbl, &dc_y,  &dc_lum, &ac_lum);
            zan_jpg_encode_block(&bw, cbb, inv_q_chr, cos_tbl, &dc_cb, &dc_chr, &ac_chr);
            zan_jpg_encode_block(&bw, crb, inv_q_chr, cos_tbl, &dc_cr, &dc_chr, &ac_chr);
            if (b.oom) { free(b.data); return NULL; }
        }
    }

    zan_jpg_flush_bits(&bw);
    zan_enc_put1(&b, 0xFF); zan_enc_put1(&b, 0xD9);
    if (b.oom || b.len > 0x7FFFFFFF) { free(b.data); return NULL; }
    *out_len = (int32_t)b.len;
    return b.data;
}

ZAN_IMAGE_EXPORT uint8_t *zan_image_encode_argb(const uint32_t *pixels,
                                                int32_t w, int32_t h, int32_t stride,
                                                int32_t format, int32_t quality,
                                                int32_t *out_len) {
    if (out_len) *out_len = 0;
    if (!pixels || !out_len || !zan_img_dimensions_ok(w, h) || stride < w) return NULL;
    switch (format) {
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
