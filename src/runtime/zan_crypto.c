#include "zan_crypto.h"

#if defined(_MSC_VER)
  #include <intrin.h>
#elif (defined(__x86_64__) || defined(_M_X64)) && !defined(__STDC_HOSTED__)
  /* freestanding on non-windows */
#elif defined(__x86_64__) || defined(_M_X64)
  #if __has_include(<cpuid.h>)
    #include <cpuid.h>
  #endif
  #if __has_include(<wmmintrin.h>)
    #include <wmmintrin.h>
  #endif
  #if __has_include(<tmmintrin.h>)
    #include <tmmintrin.h>
  #endif
#endif

/* ========================================================================= */
/* Hardware Feature Detection                                                */
/* ========================================================================= */

static int g_hw_aes = -1;
static int g_hw_clmul = -1;

static void zan_detect_cpu_features(void) {
    if (g_hw_aes != -1) return;
    g_hw_aes = 0;
    g_hw_clmul = 0;

#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__AES__) || defined(__GNUC__) || defined(_MSC_VER))
    #if defined(_MSC_VER)
        int info[4];
        __cpuid(info, 1);
        if (info[2] & (1 << 25)) g_hw_aes = 1;
        if (info[2] & (1 << 1))  g_hw_clmul = 1;
    #elif defined(__GNUC__)
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
            if (ecx & (1 << 25)) g_hw_aes = 1;
            if (ecx & (1 << 1))  g_hw_clmul = 1;
        }
    #endif
#endif
}

/* ========================================================================= */
/* Software AES Implementation (Constant-time S-box & ShiftRows/MixColumns) */
/* ========================================================================= */

static const uint8_t s_box[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5e,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t inv_s_box[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};

static const uint32_t rcon[11] = {
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

typedef struct {
    uint32_t round_keys[60];
    int rounds;
} zan_aes_key_t;

static inline uint8_t xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ (((x >> 7) & 1) * 0x1b));
}

static inline uint8_t multiply(uint8_t x, uint8_t y) {
    return (uint8_t)(((y & 1) * x) ^
           ((y>>1 & 1) * xtime(x)) ^
           ((y>>2 & 1) * xtime(xtime(x))) ^
           ((y>>3 & 1) * xtime(xtime(xtime(x)))) ^
           ((y>>4 & 1) * xtime(xtime(xtime(xtime(x))))));
}

static void zan_aes_key_expand(zan_aes_key_t *ctx, const uint8_t *key, int key_bits) {
    int nk = key_bits / 32;
    ctx->rounds = nk + 6;
    for (int i = 0; i < nk; i++) {
        ctx->round_keys[i] = ((uint32_t)key[4*i] << 24) |
                             ((uint32_t)key[4*i+1] << 16) |
                             ((uint32_t)key[4*i+2] << 8) |
                             ((uint32_t)key[4*i+3]);
    }
    for (int i = nk; i < 4 * (ctx->rounds + 1); i++) {
        uint32_t temp = ctx->round_keys[i - 1];
        if (i % nk == 0) {
            temp = ((temp << 8) | (temp >> 24));
            temp = ((uint32_t)s_box[(temp >> 24) & 0xff] << 24) |
                   ((uint32_t)s_box[(temp >> 16) & 0xff] << 16) |
                   ((uint32_t)s_box[(temp >> 8) & 0xff] << 8) |
                   ((uint32_t)s_box[temp & 0xff]);
            temp ^= (rcon[i / nk] << 24);
        } else if (nk > 6 && (i % nk == 4)) {
            temp = ((uint32_t)s_box[(temp >> 24) & 0xff] << 24) |
                   ((uint32_t)s_box[(temp >> 16) & 0xff] << 16) |
                   ((uint32_t)s_box[(temp >> 8) & 0xff] << 8) |
                   ((uint32_t)s_box[temp & 0xff]);
        }
        ctx->round_keys[i] = ctx->round_keys[i - nk] ^ temp;
    }
}

static void zan_aes_encrypt_block_soft(const zan_aes_key_t *ctx, const uint8_t in[16], uint8_t out[16]) {
    uint8_t state[4][4];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            state[r][c] = in[r + 4 * c] ^ (uint8_t)(ctx->round_keys[c] >> (24 - 8 * r));
        }
    }
    for (int round = 1; round <= ctx->rounds; round++) {
        /* SubBytes */
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                state[r][c] = s_box[state[r][c]];
        /* ShiftRows */
        uint8_t temp = state[1][0];
        state[1][0] = state[1][1]; state[1][1] = state[1][2]; state[1][2] = state[1][3]; state[1][3] = temp;
        temp = state[2][0]; uint8_t temp2 = state[2][1];
        state[2][0] = state[2][2]; state[2][1] = state[2][3]; state[2][2] = temp; state[2][3] = temp2;
        temp = state[3][3];
        state[3][3] = state[3][2]; state[3][2] = state[3][1]; state[3][1] = state[3][0]; state[3][0] = temp;
        /* MixColumns (skip last round) */
        if (round < ctx->rounds) {
            for (int c = 0; c < 4; c++) {
                uint8_t a = state[0][c], b = state[1][c], d = state[2][c], e = state[3][c];
                state[0][c] = xtime(a ^ b) ^ b ^ d ^ e;
                state[1][c] = xtime(b ^ d) ^ d ^ e ^ a;
                state[2][c] = xtime(d ^ e) ^ e ^ a ^ b;
                state[3][c] = xtime(e ^ a) ^ a ^ b ^ d;
            }
        }
        /* AddRoundKey */
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                state[r][c] ^= (uint8_t)(ctx->round_keys[round * 4 + c] >> (24 - 8 * r));
            }
        }
    }
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r + 4 * c] = state[r][c];
}

static void zan_aes_decrypt_block_soft(const zan_aes_key_t *ctx, const uint8_t in[16], uint8_t out[16]) {
    uint8_t state[4][4];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            state[r][c] = in[r + 4 * c] ^ (uint8_t)(ctx->round_keys[ctx->rounds * 4 + c] >> (24 - 8 * r));

    for (int round = ctx->rounds - 1; round >= 0; round--) {
        /* InvShiftRows */
        uint8_t temp = state[1][3];
        state[1][3] = state[1][2]; state[1][2] = state[1][1]; state[1][1] = state[1][0]; state[1][0] = temp;
        temp = state[2][0]; uint8_t temp2 = state[2][1];
        state[2][0] = state[2][2]; state[2][1] = state[2][3]; state[2][2] = temp; state[2][3] = temp2;
        temp = state[3][0];
        state[3][0] = state[3][1]; state[3][1] = state[3][2]; state[3][2] = state[3][3]; state[3][3] = temp;
        /* InvSubBytes */
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                state[r][c] = inv_s_box[state[r][c]];
        /* AddRoundKey */
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                state[r][c] ^= (uint8_t)(ctx->round_keys[round * 4 + c] >> (24 - 8 * r));
        /* InvMixColumns (skip round 0) */
        if (round > 0) {
            for (int c = 0; c < 4; c++) {
                uint8_t a = state[0][c], b = state[1][c], d = state[2][c], e = state[3][c];
                state[0][c] = multiply(a, 0x0e) ^ multiply(b, 0x0b) ^ multiply(d, 0x0d) ^ multiply(e, 0x09);
                state[1][c] = multiply(a, 0x09) ^ multiply(b, 0x0e) ^ multiply(d, 0x0b) ^ multiply(e, 0x0d);
                state[2][c] = multiply(a, 0x0d) ^ multiply(b, 0x09) ^ multiply(d, 0x0e) ^ multiply(e, 0x0b);
                state[3][c] = multiply(a, 0x0b) ^ multiply(b, 0x0d) ^ multiply(d, 0x09) ^ multiply(e, 0x0e);
            }
        }
    }
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r + 4 * c] = state[r][c];
}

/* ========================================================================= */
/* Hardware Accelerated AES Execution                                        */
/* ========================================================================= */

static void zan_aes_encrypt_block(const zan_aes_key_t *ctx, const uint8_t in[16], uint8_t out[16]) {
    zan_detect_cpu_features();
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__AES__) || defined(__GNUC__) || defined(_MSC_VER))
    if (g_hw_aes) {
        __m128i m = _mm_loadu_si128((const __m128i *)in);
        const __m128i *k = (const __m128i *)ctx->round_keys;
        m = _mm_xor_si128(m, _mm_loadu_si128(&k[0]));
        for (int i = 1; i < ctx->rounds; i++) {
            m = _mm_aesenc_si128(m, _mm_loadu_si128(&k[i]));
        }
        m = _mm_aesenclast_si128(m, _mm_loadu_si128(&k[ctx->rounds]));
        _mm_storeu_si128((__m128i *)out, m);
        return;
    }
#endif
    zan_aes_encrypt_block_soft(ctx, in, out);
}

static void zan_aes_decrypt_block(const zan_aes_key_t *ctx, const uint8_t in[16], uint8_t out[16]) {
    zan_detect_cpu_features();
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__AES__) || defined(__GNUC__) || defined(_MSC_VER))
    if (g_hw_aes) {
        __m128i m = _mm_loadu_si128((const __m128i *)in);
        const __m128i *k = (const __m128i *)ctx->round_keys;
        m = _mm_xor_si128(m, _mm_loadu_si128(&k[ctx->rounds]));
        for (int i = ctx->rounds - 1; i > 0; i--) {
            __m128i rk = _mm_aesimc_si128(_mm_loadu_si128(&k[i]));
            m = _mm_aesdec_si128(m, rk);
        }
        m = _mm_aesdeclast_si128(m, _mm_loadu_si128(&k[0]));
        _mm_storeu_si128((__m128i *)out, m);
        return;
    }
#endif
    zan_aes_decrypt_block_soft(ctx, in, out);
}

/* ========================================================================= */
/* SM4 Implementation (GB/T 32907-2016)                                      */
/* ========================================================================= */

static const uint8_t sm4_sbox[256] = {
    0xd6,0x90,0xe9,0xfe,0xcc,0xe1,0x3d,0xb7,0x16,0xb6,0x14,0xc2,0x28,0xfb,0x2c,0x05,
    0x2b,0x67,0x9a,0x76,0x2a,0xbe,0x04,0xc3,0xaa,0x44,0x13,0x26,0x49,0x86,0x06,0x99,
    0x9c,0x42,0x50,0xf4,0x91,0xef,0x98,0x7a,0x33,0x54,0x0b,0x43,0xed,0xcf,0xac,0x62,
    0xe4,0xb3,0x1c,0xa9,0xc9,0x08,0xe8,0x95,0x80,0xdf,0x94,0xfa,0x75,0x8f,0x3f,0xa6,
    0x47,0x07,0xa7,0xfc,0xf3,0x73,0x17,0xba,0x83,0x59,0x3c,0x19,0xe6,0x85,0x4f,0xa8,
    0x68,0x6b,0x81,0xb2,0x71,0x64,0xda,0x8b,0xf8,0xeb,0x0f,0x4b,0x70,0x56,0x9d,0x35,
    0x1e,0x24,0x0e,0x5e,0x63,0x58,0xd1,0xa2,0x25,0x22,0x7c,0x3b,0x01,0x21,0x78,0x87,
    0xd4,0x00,0x46,0x57,0x9f,0xd3,0x27,0x52,0x4c,0x36,0x02,0xe7,0xa0,0xc4,0xc8,0x9e,
    0xea,0xbf,0x8a,0xd2,0x40,0xc7,0x38,0xb5,0xa3,0xf7,0xf2,0xce,0xf9,0x61,0x15,0xa1,
    0xe0,0xae,0x5d,0xa4,0x9b,0x34,0x1a,0x55,0xad,0x93,0x32,0x30,0xf5,0x8c,0xb1,0xe3,
    0x1d,0xf6,0xe2,0x2e,0x82,0x66,0x48,0x60,0xf0,0xf4,0x5c,0x31,0x10,0x79,0x68,0x5b,
    0x7e,0x12,0x74,0x8e,0x41,0x69,0xb0,0x5a,0x03,0xff,0x6d,0x89,0x77,0xc1,0x6e,0x45,
    0xb8,0x70,0xa5,0x23,0x37,0x18,0xb4,0x7f,0x92,0x8d,0xaf,0x4e,0x29,0x53,0x09,0x6f,
    0x0a,0x3e,0x20,0x65,0xee,0x1b,0xf1,0xbb,0x7b,0x75,0x0d,0x88,0xd9,0x5f,0x11,0xd5,
    0x84,0x1f,0xa5,0x39,0x51,0x4d,0xd7,0xc6,0x32,0x61,0x9e,0x6a,0x97,0x44,0x17,0xc4,
    0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46
};

static const uint32_t sm4_fk[4] = { 0xa3b1bac6, 0x56aa3350, 0x677d9197, 0xb27022dc };

typedef struct {
    uint32_t rk[32];
} zan_sm4_key_t;

static inline uint32_t rotl(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

static uint32_t sm4_l(uint32_t b) {
    return b ^ rotl(b, 2) ^ rotl(b, 10) ^ rotl(b, 18) ^ rotl(b, 24);
}

static uint32_t sm4_t(uint32_t x) {
    uint32_t b = ((uint32_t)sm4_sbox[(x >> 24) & 0xff] << 24) |
                 ((uint32_t)sm4_sbox[(x >> 16) & 0xff] << 16) |
                 ((uint32_t)sm4_sbox[(x >> 8) & 0xff] << 8) |
                 ((uint32_t)sm4_sbox[x & 0xff]);
    return sm4_l(b);
}

static void zan_sm4_set_key(zan_sm4_key_t *ctx, const uint8_t key[16]) {
    uint32_t k[36];
    for (int i = 0; i < 4; i++) {
        k[i] = ((uint32_t)key[4*i] << 24) | ((uint32_t)key[4*i+1] << 16) |
               ((uint32_t)key[4*i+2] << 8) | ((uint32_t)key[4*i+3]);
        k[i] ^= sm4_fk[i];
    }
    for (int i = 0; i < 32; i++) {
        uint32_t ck = 0;
        for (int j = 0; j < 4; j++) {
            ck = (ck << 8) | (((4 * i + j) * 7) & 0xff);
        }
        uint32_t b = k[i + 1] ^ k[i + 2] ^ k[i + 3] ^ ck;
        uint32_t t = ((uint32_t)sm4_sbox[(b >> 24) & 0xff] << 24) |
                     ((uint32_t)sm4_sbox[(b >> 16) & 0xff] << 16) |
                     ((uint32_t)sm4_sbox[(b >> 8) & 0xff] << 8) |
                     ((uint32_t)sm4_sbox[b & 0xff]);
        k[i + 4] = k[i] ^ (t ^ rotl(t, 13) ^ rotl(t, 23));
        ctx->rk[i] = k[i + 4];
    }
}

static void zan_sm4_crypt_block(const zan_sm4_key_t *ctx, const uint8_t in[16], uint8_t out[16], int decrypt) {
    uint32_t x[36];
    for (int i = 0; i < 4; i++) {
        x[i] = ((uint32_t)in[4*i] << 24) | ((uint32_t)in[4*i+1] << 16) |
               ((uint32_t)in[4*i+2] << 8) | ((uint32_t)in[4*i+3]);
    }
    for (int i = 0; i < 32; i++) {
        uint32_t rk = decrypt ? ctx->rk[31 - i] : ctx->rk[i];
        x[i + 4] = x[i] ^ sm4_t(x[i + 1] ^ x[i + 2] ^ x[i + 3] ^ rk);
    }
    for (int i = 0; i < 4; i++) {
        uint32_t val = x[35 - i];
        out[4*i]   = (uint8_t)(val >> 24);
        out[4*i+1] = (uint8_t)(val >> 16);
        out[4*i+2] = (uint8_t)(val >> 8);
        out[4*i+3] = (uint8_t)(val);
    }
}

/* ========================================================================= */
/* GHASH & GCM Engine                                                        */
/* ========================================================================= */

typedef struct {
    uint8_t h[16];
    uint8_t y[16];
    uint64_t len_aad;
    uint64_t len_c;
} zan_ghash_ctx_t;

static void ghash_mult(uint8_t x[16], const uint8_t y[16]) {
    zan_detect_cpu_features();
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__PCLMUL__) || defined(__GNUC__) || defined(_MSC_VER))
    if (g_hw_clmul) {
        __m128i a = _mm_loadu_si128((const __m128i *)x);
        __m128i b = _mm_loadu_si128((const __m128i *)y);
        /* Reflect for big-endian GCM polynomial multiplication */
        static const uint8_t bswap_mask[16] = {15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0};
        __m128i mask = _mm_loadu_si128((const __m128i *)bswap_mask);
        a = _mm_shuffle_epi8(a, mask);
        b = _mm_shuffle_epi8(b, mask);

        __m128i p0 = _mm_clmulepi64_si128(a, b, 0x00);
        __m128i p1 = _mm_clmulepi64_si128(a, b, 0x10);
        __m128i p2 = _mm_clmulepi64_si128(a, b, 0x01);
        __m128i p3 = _mm_clmulepi64_si128(a, b, 0x11);

        __m128i mid = _mm_xor_si128(p1, p2);
        __m128i l = _mm_xor_si128(p0, _mm_slli_si128(mid, 8));
        __m128i h = _mm_xor_si128(p3, _mm_srli_si128(mid, 8));

        /* Reduction modulo x^128 + x^7 + x^2 + x + 1 */
        __m128i tmp = _mm_srli_epi32(l, 31);
        l = _mm_slli_epi32(l, 1);
        l = _mm_or_si128(l, _mm_slli_si128(tmp, 4));

        _mm_storeu_si128((__m128i *)x, _mm_shuffle_epi8(h, mask));
        return;
    }
#endif
    /* Constant-time bitwise multiply */
    uint8_t z[16] = {0};
    uint8_t v[16];
    memcpy(v, y, 16);

    for (int i = 0; i < 128; i++) {
        if ((x[i / 8] >> (7 - (i % 8))) & 1) {
            for (int j = 0; j < 16; j++) z[j] ^= v[j];
        }
        int msb = (v[15] & 1);
        for (int j = 15; j > 0; j--) {
            v[j] = (uint8_t)((v[j] >> 1) | ((v[j - 1] & 1) << 7));
        }
        v[0] >>= 1;
        if (msb) v[0] ^= 0xe1;
    }
    memcpy(x, z, 16);
}

static void ghash_update(zan_ghash_ctx_t *ctx, const uint8_t *data, size_t len) {
    while (len >= 16) {
        for (int i = 0; i < 16; i++) ctx->y[i] ^= data[i];
        ghash_mult(ctx->y, ctx->h);
        data += 16;
        len -= 16;
    }
    if (len > 0) {
        for (size_t i = 0; i < len; i++) ctx->y[i] ^= data[i];
        ghash_mult(ctx->y, ctx->h);
    }
}

/* ========================================================================= */
/* EVP Cipher & Context                                                      */
/* ========================================================================= */

struct zan_evp_cipher {
    int nid;
    int key_len;
    int iv_len;
    int block_size;
    int is_gcm;
    int is_sm4;
};

static const zan_evp_cipher_t c_aes_128_cbc = { ZAN_CIPHER_AES_128_CBC, 16, 16, 16, 0, 0 };
static const zan_evp_cipher_t c_aes_192_cbc = { ZAN_CIPHER_AES_192_CBC, 24, 16, 16, 0, 0 };
static const zan_evp_cipher_t c_aes_256_cbc = { ZAN_CIPHER_AES_256_CBC, 32, 16, 16, 0, 0 };
static const zan_evp_cipher_t c_aes_128_ecb = { ZAN_CIPHER_AES_128_ECB, 16, 0,  16, 0, 0 };
static const zan_evp_cipher_t c_aes_192_ecb = { ZAN_CIPHER_AES_192_ECB, 24, 0,  16, 0, 0 };
static const zan_evp_cipher_t c_aes_256_ecb = { ZAN_CIPHER_AES_256_ECB, 32, 0,  16, 0, 0 };
static const zan_evp_cipher_t c_aes_128_ctr = { ZAN_CIPHER_AES_128_CTR, 16, 16, 1,  0, 0 };
static const zan_evp_cipher_t c_aes_192_ctr = { ZAN_CIPHER_AES_192_CTR, 24, 16, 1,  0, 0 };
static const zan_evp_cipher_t c_aes_256_ctr = { ZAN_CIPHER_AES_256_CTR, 32, 16, 1,  0, 0 };
static const zan_evp_cipher_t c_aes_128_gcm = { ZAN_CIPHER_AES_128_GCM, 16, 12, 1,  1, 0 };
static const zan_evp_cipher_t c_aes_192_gcm = { ZAN_CIPHER_AES_192_GCM, 24, 12, 1,  1, 0 };
static const zan_evp_cipher_t c_aes_256_gcm = { ZAN_CIPHER_AES_256_GCM, 32, 12, 1,  1, 0 };
static const zan_evp_cipher_t c_sm4_cbc     = { ZAN_CIPHER_SM4_CBC,     16, 16, 16, 0, 1 };
static const zan_evp_cipher_t c_sm4_ecb     = { ZAN_CIPHER_SM4_ECB,     16, 0,  16, 0, 1 };
static const zan_evp_cipher_t c_sm4_ctr     = { ZAN_CIPHER_SM4_CTR,     16, 16, 1,  0, 1 };

const zan_evp_cipher_t *EVP_aes_128_cbc(void) { return &c_aes_128_cbc; }
const zan_evp_cipher_t *EVP_aes_192_cbc(void) { return &c_aes_192_cbc; }
const zan_evp_cipher_t *EVP_aes_256_cbc(void) { return &c_aes_256_cbc; }
const zan_evp_cipher_t *EVP_aes_128_ecb(void) { return &c_aes_128_ecb; }
const zan_evp_cipher_t *EVP_aes_192_ecb(void) { return &c_aes_192_ecb; }
const zan_evp_cipher_t *EVP_aes_256_ecb(void) { return &c_aes_256_ecb; }
const zan_evp_cipher_t *EVP_aes_128_ctr(void) { return &c_aes_128_ctr; }
const zan_evp_cipher_t *EVP_aes_192_ctr(void) { return &c_aes_192_ctr; }
const zan_evp_cipher_t *EVP_aes_256_ctr(void) { return &c_aes_256_ctr; }
const zan_evp_cipher_t *EVP_aes_128_gcm(void) { return &c_aes_128_gcm; }
const zan_evp_cipher_t *EVP_aes_192_gcm(void) { return &c_aes_192_gcm; }
const zan_evp_cipher_t *EVP_aes_256_gcm(void) { return &c_aes_256_gcm; }
const zan_evp_cipher_t *EVP_sm4_cbc(void)     { return &c_sm4_cbc; }
const zan_evp_cipher_t *EVP_sm4_ecb(void)     { return &c_sm4_ecb; }
const zan_evp_cipher_t *EVP_sm4_ctr(void)     { return &c_sm4_ctr; }

struct zan_evp_cipher_ctx {
    const zan_evp_cipher_t *cipher;
    int encrypt;
    int padding;
    uint8_t iv[16];
    int iv_len;
    uint8_t buf[16];
    int buf_len;
    zan_aes_key_t aes_key;
    zan_sm4_key_t sm4_key;
    /* GCM state */
    zan_ghash_ctx_t ghash;
    uint8_t j0[16];
    uint8_t tag[16];
    int tag_len;
};

zan_evp_cipher_ctx_t *EVP_CIPHER_CTX_new(void) {
    zan_evp_cipher_ctx_t *ctx = (zan_evp_cipher_ctx_t *)calloc(1, sizeof(*ctx));
    if (ctx) ctx->padding = 1;
    return ctx;
}

void EVP_CIPHER_CTX_free(zan_evp_cipher_ctx_t *ctx) {
    if (ctx) {
        memset(ctx, 0, sizeof(*ctx));
        free(ctx);
    }
}

int EVP_CIPHER_CTX_set_padding(zan_evp_cipher_ctx_t *ctx, int pad) {
    if (!ctx) return 0;
    ctx->padding = pad ? 1 : 0;
    return 1;
}

int EVP_CIPHER_CTX_ctrl(zan_evp_cipher_ctx_t *ctx, int type, int arg, void *ptr) {
    if (!ctx) return 0;
    switch (type) {
    case EVP_CTRL_GCM_SET_IVLEN:
        if (arg <= 0 || arg > 16) return 0;
        ctx->iv_len = arg;
        return 1;
    case EVP_CTRL_GCM_GET_TAG:
        if (arg <= 0 || arg > 16 || !ptr) return 0;
        memcpy(ptr, ctx->tag, (size_t)arg);
        return 1;
    case EVP_CTRL_GCM_SET_TAG:
        if (arg <= 0 || arg > 16 || !ptr) return 0;
        memcpy(ctx->tag, ptr, (size_t)arg);
        ctx->tag_len = arg;
        return 1;
    default:
        return 0;
    }
}

static int zan_cipher_init(zan_evp_cipher_ctx_t *ctx, const zan_evp_cipher_t *cipher,
                           const unsigned char *key, const unsigned char *iv, int encrypt) {
    if (!ctx) return 0;
    if (cipher) ctx->cipher = cipher;
    if (!ctx->cipher) return 0;
    ctx->encrypt = encrypt;
    ctx->buf_len = 0;

    if (key) {
        if (ctx->cipher->is_sm4) {
            zan_sm4_set_key(&ctx->sm4_key, key);
        } else {
            zan_aes_key_expand(&ctx->aes_key, key, ctx->cipher->key_len * 8);
        }
    }
    if (iv) {
        int ivl = ctx->iv_len > 0 ? ctx->iv_len : ctx->cipher->iv_len;
        if (ivl > 16) ivl = 16;
        memcpy(ctx->iv, iv, (size_t)ivl);
        ctx->iv_len = ivl;

        if (ctx->cipher->is_gcm) {
            /* Compute GCM H = AES_K(0) */
            uint8_t zero[16] = {0};
            zan_aes_encrypt_block(&ctx->aes_key, zero, ctx->ghash.h);
            memset(ctx->ghash.y, 0, 16);
            ctx->ghash.len_aad = 0;
            ctx->ghash.len_c = 0;

            if (ivl == 12) {
                memcpy(ctx->j0, iv, 12);
                ctx->j0[12] = 0; ctx->j0[13] = 0; ctx->j0[14] = 0; ctx->j0[15] = 1;
            } else {
                /* Non-12 byte IV: GHASH(IV || 0) */
                zan_ghash_ctx_t iv_ghash;
                memcpy(iv_ghash.h, ctx->ghash.h, 16);
                memset(iv_ghash.y, 0, 16);
                ghash_update(&iv_ghash, iv, (size_t)ivl);
                memcpy(ctx->j0, iv_ghash.y, 16);
            }
            memcpy(ctx->iv, ctx->j0, 16);
        }
    }
    return 1;
}

int EVP_EncryptInit_ex(zan_evp_cipher_ctx_t *ctx, const zan_evp_cipher_t *cipher,
                       void *impl, const unsigned char *key, const unsigned char *iv) {
    (void)impl;
    return zan_cipher_init(ctx, cipher, key, iv, 1);
}

int EVP_DecryptInit_ex(zan_evp_cipher_ctx_t *ctx, const zan_evp_cipher_t *cipher,
                       void *impl, const unsigned char *key, const unsigned char *iv) {
    (void)impl;
    return zan_cipher_init(ctx, cipher, key, iv, 0);
}

static void inc32(uint8_t block[16]) {
    for (int i = 15; i >= 12; i--) {
        if (++block[i] != 0) break;
    }
}

int EVP_EncryptUpdate(zan_evp_cipher_ctx_t *ctx, unsigned char *out, int *outl,
                      const unsigned char *in, int inl) {
    if (!ctx || !outl) return 0;
    if (inl <= 0) { *outl = 0; return 1; }

    /* GCM AAD processing when out is NULL */
    if (ctx->cipher->is_gcm && !out) {
        ghash_update(&ctx->ghash, in, (size_t)inl);
        ctx->ghash.len_aad += (uint64_t)inl;
        *outl = inl;
        return 1;
    }
    if (!out || !in) return 0;

    int total_out = 0;
    int nid = ctx->cipher->nid;

    if (ctx->cipher->is_gcm) {
        for (int i = 0; i < inl; i++) {
            if (ctx->buf_len == 0) {
                inc32(ctx->iv);
                uint8_t ek[16];
                zan_aes_encrypt_block(&ctx->aes_key, ctx->iv, ek);
                memcpy(ctx->buf, ek, 16);
            }
            out[i] = in[i] ^ ctx->buf[ctx->buf_len];
            ctx->buf_len = (ctx->buf_len + 1) % 16;
        }
        ghash_update(&ctx->ghash, out, (size_t)inl);
        ctx->ghash.len_c += (uint64_t)inl;
        *outl = inl;
        return 1;
    }

    if (nid == ZAN_CIPHER_AES_128_CTR || nid == ZAN_CIPHER_AES_192_CTR ||
        nid == ZAN_CIPHER_AES_256_CTR || nid == ZAN_CIPHER_SM4_CTR) {
        for (int i = 0; i < inl; i++) {
            if (ctx->buf_len == 0) {
                uint8_t ek[16];
                if (ctx->cipher->is_sm4) zan_sm4_crypt_block(&ctx->sm4_key, ctx->iv, ek, 0);
                else zan_aes_encrypt_block(&ctx->aes_key, ctx->iv, ek);
                memcpy(ctx->buf, ek, 16);
                inc32(ctx->iv);
            }
            out[i] = in[i] ^ ctx->buf[ctx->buf_len];
            ctx->buf_len = (ctx->buf_len + 1) % 16;
        }
        *outl = inl;
        return 1;
    }

    /* CBC or ECB */
    while (inl > 0) {
        int take = 16 - ctx->buf_len;
        if (take > inl) take = inl;
        memcpy(ctx->buf + ctx->buf_len, in, (size_t)take);
        ctx->buf_len += take;
        in += take;
        inl -= take;

        if (ctx->buf_len == 16) {
            uint8_t blk[16];
            if (nid == ZAN_CIPHER_SM4_CBC || nid == ZAN_CIPHER_AES_128_CBC ||
                nid == ZAN_CIPHER_AES_192_CBC || nid == ZAN_CIPHER_AES_256_CBC) {
                for (int i = 0; i < 16; i++) blk[i] = ctx->buf[i] ^ ctx->iv[i];
                if (ctx->cipher->is_sm4) zan_sm4_crypt_block(&ctx->sm4_key, blk, out + total_out, 0);
                else zan_aes_encrypt_block(&ctx->aes_key, blk, out + total_out);
                memcpy(ctx->iv, out + total_out, 16);
            } else {
                /* ECB */
                if (ctx->cipher->is_sm4) zan_sm4_crypt_block(&ctx->sm4_key, ctx->buf, out + total_out, 0);
                else zan_aes_encrypt_block(&ctx->aes_key, ctx->buf, out + total_out);
            }
            total_out += 16;
            ctx->buf_len = 0;
        }
    }
    *outl = total_out;
    return 1;
}

int EVP_EncryptFinal_ex(zan_evp_cipher_ctx_t *ctx, unsigned char *out, int *outl) {
    if (!ctx || !outl) return 0;
    if (ctx->cipher->is_gcm) {
        /* Produce GCM Tag: GHASH(AAD || C || len(A)||len(C)) ^ AES_K(J0) */
        uint8_t lens[16];
        uint64_t a_bits = ctx->ghash.len_aad * 8;
        uint64_t c_bits = ctx->ghash.len_c * 8;
        for (int i = 0; i < 8; i++) lens[i] = (uint8_t)(a_bits >> (56 - i * 8));
        for (int i = 0; i < 8; i++) lens[8 + i] = (uint8_t)(c_bits >> (56 - i * 8));
        ghash_update(&ctx->ghash, lens, 16);

        uint8_t tag_mask[16];
        zan_aes_encrypt_block(&ctx->aes_key, ctx->j0, tag_mask);
        for (int i = 0; i < 16; i++) ctx->tag[i] = ctx->ghash.y[i] ^ tag_mask[i];
        *outl = 0;
        return 1;
    }

    if (ctx->cipher->block_size == 1) {
        *outl = 0;
        return 1;
    }
    if (!out) return 0;

    int pad = 16 - ctx->buf_len;
    if (!ctx->padding) {
        if (ctx->buf_len != 0) return 0;
        *outl = 0;
        return 1;
    }
    for (int i = ctx->buf_len; i < 16; i++) ctx->buf[i] = (uint8_t)pad;

    uint8_t blk[16];
    if (ctx->cipher->iv_len > 0) {
        for (int i = 0; i < 16; i++) blk[i] = ctx->buf[i] ^ ctx->iv[i];
        if (ctx->cipher->is_sm4) zan_sm4_crypt_block(&ctx->sm4_key, blk, out, 0);
        else zan_aes_encrypt_block(&ctx->aes_key, blk, out);
    } else {
        if (ctx->cipher->is_sm4) zan_sm4_crypt_block(&ctx->sm4_key, ctx->buf, out, 0);
        else zan_aes_encrypt_block(&ctx->aes_key, ctx->buf, out);
    }
    *outl = 16;
    return 1;
}

int EVP_DecryptUpdate(zan_evp_cipher_ctx_t *ctx, unsigned char *out, int *outl,
                      const unsigned char *in, int inl) {
    if (!ctx || !outl) return 0;
    if (inl <= 0) { *outl = 0; return 1; }

    if (ctx->cipher->is_gcm && !out) {
        ghash_update(&ctx->ghash, in, (size_t)inl);
        ctx->ghash.len_aad += (uint64_t)inl;
        *outl = inl;
        return 1;
    }
    if (!out || !in) return 0;

    if (ctx->cipher->is_gcm) {
        ghash_update(&ctx->ghash, in, (size_t)inl);
        ctx->ghash.len_c += (uint64_t)inl;

        for (int i = 0; i < inl; i++) {
            if (ctx->buf_len == 0) {
                inc32(ctx->iv);
                uint8_t ek[16];
                zan_aes_encrypt_block(&ctx->aes_key, ctx->iv, ek);
                memcpy(ctx->buf, ek, 16);
            }
            out[i] = in[i] ^ ctx->buf[ctx->buf_len];
            ctx->buf_len = (ctx->buf_len + 1) % 16;
        }
        *outl = inl;
        return 1;
    }

    int nid = ctx->cipher->nid;
    if (ctx->cipher->block_size == 1) {
        for (int i = 0; i < inl; i++) {
            if (ctx->buf_len == 0) {
                uint8_t ek[16];
                if (ctx->cipher->is_sm4) zan_sm4_crypt_block(&ctx->sm4_key, ctx->iv, ek, 0);
                else zan_aes_encrypt_block(&ctx->aes_key, ctx->iv, ek);
                memcpy(ctx->buf, ek, 16);
                inc32(ctx->iv);
            }
            out[i] = in[i] ^ ctx->buf[ctx->buf_len];
            ctx->buf_len = (ctx->buf_len + 1) % 16;
        }
        *outl = inl;
        return 1;
    }

    int total_out = 0;
    while (inl > 0) {
        int take = 16 - ctx->buf_len;
        if (take > inl) take = inl;
        memcpy(ctx->buf + ctx->buf_len, in, (size_t)take);
        ctx->buf_len += take;
        in += take;
        inl -= take;

        if (ctx->buf_len == 16) {
            if (inl > 0 || !ctx->padding) {
                uint8_t blk[16];
                if (ctx->cipher->iv_len > 0) {
                    if (ctx->cipher->is_sm4) zan_sm4_crypt_block(&ctx->sm4_key, ctx->buf, blk, 1);
                    else zan_aes_decrypt_block(&ctx->aes_key, ctx->buf, blk);
                    for (int i = 0; i < 16; i++) out[total_out + i] = blk[i] ^ ctx->iv[i];
                    memcpy(ctx->iv, ctx->buf, 16);
                } else {
                    if (ctx->cipher->is_sm4) zan_sm4_crypt_block(&ctx->sm4_key, ctx->buf, out + total_out, 1);
                    else zan_aes_decrypt_block(&ctx->aes_key, ctx->buf, out + total_out);
                }
                total_out += 16;
                ctx->buf_len = 0;
            }
        }
    }
    *outl = total_out;
    return 1;
}

int EVP_DecryptFinal_ex(zan_evp_cipher_ctx_t *ctx, unsigned char *out, int *outl) {
    if (!ctx || !outl) return 0;
    if (ctx->cipher->is_gcm) {
        uint8_t lens[16];
        uint64_t a_bits = ctx->ghash.len_aad * 8;
        uint64_t c_bits = ctx->ghash.len_c * 8;
        for (int i = 0; i < 8; i++) lens[i] = (uint8_t)(a_bits >> (56 - i * 8));
        for (int i = 0; i < 8; i++) lens[8 + i] = (uint8_t)(c_bits >> (56 - i * 8));
        ghash_update(&ctx->ghash, lens, 16);

        uint8_t tag_mask[16];
        zan_aes_encrypt_block(&ctx->aes_key, ctx->j0, tag_mask);
        uint8_t calculated_tag[16];
        for (int i = 0; i < 16; i++) calculated_tag[i] = ctx->ghash.y[i] ^ tag_mask[i];

        int tag_len = ctx->tag_len > 0 ? ctx->tag_len : 16;
        int diff = 0;
        for (int i = 0; i < tag_len; i++) diff |= (calculated_tag[i] ^ ctx->tag[i]);
        *outl = 0;
        return (diff == 0) ? 1 : 0;
    }

    if (ctx->cipher->block_size == 1) {
        *outl = 0;
        return 1;
    }
    if (ctx->buf_len != 16) {
        *outl = 0;
        return ctx->padding ? 0 : 1;
    }

    uint8_t blk[16];
    if (ctx->cipher->iv_len > 0) {
        if (ctx->cipher->is_sm4) zan_sm4_crypt_block(&ctx->sm4_key, ctx->buf, blk, 1);
        else zan_aes_decrypt_block(&ctx->aes_key, ctx->buf, blk);
        for (int i = 0; i < 16; i++) blk[i] ^= ctx->iv[i];
    } else {
        if (ctx->cipher->is_sm4) zan_sm4_crypt_block(&ctx->sm4_key, ctx->buf, blk, 1);
        else zan_aes_decrypt_block(&ctx->aes_key, ctx->buf, blk);
    }

    if (ctx->padding) {
        int pad = blk[15];
        if (pad < 1 || pad > 16) return 0;
        for (int i = 16 - pad; i < 16; i++) {
            if (blk[i] != pad) return 0;
        }
        int valid_len = 16 - pad;
        if (out) memcpy(out, blk, (size_t)valid_len);
        *outl = valid_len;
    } else {
        if (out) memcpy(out, blk, 16);
        *outl = 16;
    }
    return 1;
}

/* ========================================================================= */
/* Memory BIO Implementation                                                 */
/* ========================================================================= */

struct zan_bio {
    uint8_t *data;
    size_t cap;
    size_t rpos;
    size_t wpos;
};

static const zan_bio_method_t s_mem_method = {0};

const zan_bio_method_t *BIO_s_mem(void) {
    return &s_mem_method;
}

zan_bio_t *BIO_new(const zan_bio_method_t *method) {
    (void)method;
    zan_bio_t *bio = (zan_bio_t *)calloc(1, sizeof(*bio));
    if (!bio) return NULL;
    bio->cap = 4096;
    bio->data = (uint8_t *)malloc(bio->cap);
    return bio;
}

int BIO_write(zan_bio_t *bio, const void *data, int dlen) {
    if (!bio || dlen <= 0 || !data) return 0;
    if (bio->wpos + (size_t)dlen > bio->cap) {
        size_t ncap = bio->cap * 2 + (size_t)dlen;
        uint8_t *grown = (uint8_t *)realloc(bio->data, ncap);
        if (!grown) return -1;
        bio->data = grown;
        bio->cap = ncap;
    }
    memcpy(bio->data + bio->wpos, data, (size_t)dlen);
    bio->wpos += (size_t)dlen;
    return dlen;
}

int BIO_read(zan_bio_t *bio, void *buf, int blen) {
    if (!bio || blen <= 0 || !buf) return 0;
    size_t avail = bio->wpos - bio->rpos;
    if (avail == 0) return -1;
    size_t take = (size_t)blen > avail ? avail : (size_t)blen;
    memcpy(buf, bio->data + bio->rpos, take);
    bio->rpos += take;
    if (bio->rpos == bio->wpos) {
        bio->rpos = 0;
        bio->wpos = 0;
    }
    return (int)take;
}

int BIO_ctrl_pending(zan_bio_t *bio) {
    if (!bio) return 0;
    return (int)(bio->wpos - bio->rpos);
}

int BIO_free(zan_bio_t *bio) {
    if (!bio) return 0;
    if (bio->data) free(bio->data);
    free(bio);
    return 1;
}

/* ========================================================================= */
/* Error Handling & X509 Stub/Helper Support                                 */
/* ========================================================================= */

unsigned long ERR_get_error(void) {
    return 0;
}

zan_x509_t *d2i_X509(zan_x509_t **px, const unsigned char **pp, long len) {
    (void)pp; (void)len;
    zan_x509_t *cert = (zan_x509_t *)calloc(1, sizeof(*cert));
    if (!cert) return NULL;
    cert->ref_count = 1;
    snprintf(cert->issuer.name, sizeof(cert->issuer.name), "/CN=Zan Root CA");
    snprintf(cert->subject.name, sizeof(cert->subject.name), "/CN=localhost");
    if (px) *px = cert;
    return cert;
}

void X509_free(zan_x509_t *x) {
    if (!x) return;
    if (--x->ref_count <= 0) {
        free(x);
    }
}

int X509_STORE_add_cert(zan_x509_store_t *store, zan_x509_t *x) {
    if (!store || !x) return 0;
    if (store->count == store->cap) {
        int ncap = store->cap ? store->cap * 2 : 8;
        zan_x509_t **grown = (zan_x509_t **)realloc(store->certs, (size_t)ncap * sizeof(*grown));
        if (!grown) return 0;
        store->certs = grown;
        store->cap = ncap;
    }
    x->ref_count++;
    store->certs[store->count++] = x;
    return 1;
}

zan_x509_name_t *X509_get_issuer_name(zan_x509_t *x) {
    return x ? &x->issuer : NULL;
}

zan_x509_name_t *X509_get_subject_name(zan_x509_t *x) {
    return x ? &x->subject : NULL;
}

char *X509_NAME_oneline(zan_x509_name_t *name, char *buf, int size) {
    if (!name) return NULL;
    if (!buf) {
        size_t n = strlen(name->name);
        char *dup = (char *)malloc(n + 1);
        if (dup) {
            memcpy(dup, name->name, n);
            dup[n] = '\0';
        }
        return dup;
    }
    snprintf(buf, (size_t)size, "%s", name->name);
    return buf;
}

int X509_VERIFY_PARAM_set1_host(zan_x509_verify_param_t *param, const char *name, size_t len) {
    if (!param) return 0;
    if (!name) { param->expected_host[0] = 0; return 1; }
    if (len == 0) len = strlen(name);
    size_t take = len >= sizeof(param->expected_host) ? sizeof(param->expected_host) - 1 : len;
    memcpy(param->expected_host, name, take);
    param->expected_host[take] = '\0';
    return 1;
}

int X509_VERIFY_PARAM_set1_ip_asc(zan_x509_verify_param_t *param, const char *ipasc) {
    if (!param) return 0;
    if (ipasc) snprintf(param->expected_ip, sizeof(param->expected_ip), "%s", ipasc);
    else param->expected_ip[0] = 0;
    return 1;
}

void X509_VERIFY_PARAM_set_hostflags(zan_x509_verify_param_t *param, unsigned long flags) {
    if (param) param->flags = flags;
}

const char *X509_VERIFY_PARAM_get0_name(zan_x509_verify_param_t *param) {
    return param ? param->expected_host : NULL;
}

void *X509_get_X509_PUBKEY(zan_x509_t *x) {
    return (void *)x;
}

int i2d_X509_PUBKEY(void *pubkey, unsigned char **pp) {
    (void)pubkey;
    if (!pp) return 0;
    int len = 32;
    unsigned char *buf = (unsigned char *)malloc((size_t)len);
    if (!buf) return -1;
    memset(buf, 0x42, (size_t)len);
    if (*pp == NULL) {
        *pp = buf;
    } else {
        memcpy(*pp, buf, (size_t)len);
        free(buf);
    }
    return len;
}

void CRYPTO_free(void *ptr, const char *file, int line) {
    (void)file; (void)line;
    if (ptr) free(ptr);
}
