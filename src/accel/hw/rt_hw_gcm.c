/* rt_hw_gcm.c: AES-GCM 认证加密与 GHASH 向量加速实现 */

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
#define ZAN_BSWAP128 _mm_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15)

static const uint64_t g_ghash_s_table[64] = {
    0x0000000000000000ULL, 0xe608000000000000ULL, 0x0e10000000000000ULL, 0xe818000000000000ULL,
    0x1c20000000000000ULL, 0xfa28000000000000ULL, 0x1230000000000000ULL, 0xf438000000000000ULL,
    0x3840000000000000ULL, 0xde48000000000000ULL, 0x3650000000000000ULL, 0xd058000000000000ULL,
    0x2460000000000000ULL, 0xc268000000000000ULL, 0x2a70000000000000ULL, 0xcc78000000000000ULL,
    0x7080000000000000ULL, 0x9688000000000000ULL, 0x7e90000000000000ULL, 0x9898000000000000ULL,
    0x6ca0000000000000ULL, 0x8aa8000000000000ULL, 0x62b0000000000000ULL, 0x84b8000000000000ULL,
    0x48c0000000000000ULL, 0xaec8000000000000ULL, 0x46d0000000000000ULL, 0xa0d8000000000000ULL,
    0x54e0000000000000ULL, 0xb2e8000000000000ULL, 0x5af0000000000000ULL, 0xbcf8000000000000ULL,
    0xe100000000000000ULL, 0x0708000000000000ULL, 0xef10000000000000ULL, 0x0918000000000000ULL,
    0xfd20000000000000ULL, 0x1b28000000000000ULL, 0xf330000000000000ULL, 0x1538000000000000ULL,
    0xd940000000000000ULL, 0x3f48000000000000ULL, 0xd750000000000000ULL, 0x3158000000000000ULL,
    0xc560000000000000ULL, 0x2368000000000000ULL, 0xcb70000000000000ULL, 0x2d78000000000000ULL,
    0x9180000000000000ULL, 0x7788000000000000ULL, 0x9f90000000000000ULL, 0x7998000000000000ULL,
    0x8da0000000000000ULL, 0x6ba8000000000000ULL, 0x83b0000000000000ULL, 0x65b8000000000000ULL,
    0xa9c0000000000000ULL, 0x4fc8000000000000ULL, 0xa7d0000000000000ULL, 0x41d8000000000000ULL,
    0xb5e0000000000000ULL, 0x53e8000000000000ULL, 0xbbf0000000000000ULL, 0x5df8000000000000ULL
};

__attribute__((target("pclmul,sse4.1")))
static inline __m128i zan_ghash_reduce(__m128i lo, __m128i hi) {
    __m128i hi_mask = _mm_set_epi64x((long long)0x8000000000000000ULL, 0);
    __m128i bit127_hi = _mm_and_si128(lo, hi_mask);
    __m128i bit127_lo = _mm_srli_si128(_mm_srli_epi64(bit127_hi, 63), 8);

    __m128i E = _mm_xor_si128(lo, bit127_hi);
    __m128i F = _mm_xor_si128(_mm_xor_si128(ZAN_XSHIFT_R(E, 6), ZAN_XSHIFT_R(E, 1)),
                              _mm_xor_si128(E, ZAN_XSHIFT_L(E, 1)));
    uint64_t low6 = (uint64_t)_mm_cvtsi128_si64(lo) & 0x3F;
    uint64_t s = g_ghash_s_table[low6];
    __m128i direct = _mm_xor_si128(ZAN_XSHIFT_L(hi, 1), bit127_lo);
    return _mm_xor_si128(F, _mm_xor_si128(direct, _mm_set_epi64x((long long)s, 0)));
}

__attribute__((target("pclmul,sse4.1")))
static inline __m128i zan_ghash_step_clmul(__m128i y, __m128i x, __m128i b) {
    __m128i a = _mm_xor_si128(y, x);
    __m128i m0 = _mm_clmulepi64_si128(a, b, 0x00);
    __m128i m3 = _mm_clmulepi64_si128(a, b, 0x11);
    __m128i mid = _mm_xor_si128(_mm_clmulepi64_si128(a, b, 0x10),
                                _mm_clmulepi64_si128(a, b, 0x01));
    __m128i lo = _mm_xor_si128(m0, _mm_slli_si128(mid, 8));
    __m128i hi = _mm_xor_si128(m3, _mm_srli_si128(mid, 8));
    return zan_ghash_reduce(lo, hi);
}

__attribute__((target("pclmul,sse4.1")))
static inline __m128i zan_ghash_4blocks_clmul(__m128i y,
                                              __m128i x0, __m128i x1,
                                              __m128i x2, __m128i x3,
                                              __m128i h1, __m128i h2,
                                              __m128i h3, __m128i h4) {
    __m128i a0 = _mm_xor_si128(y, x0);
    __m128i a1 = x1;
    __m128i a2 = x2;
    __m128i a3 = x3;

    __m128i m0_0 = _mm_clmulepi64_si128(a0, h4, 0x00);
    __m128i m3_0 = _mm_clmulepi64_si128(a0, h4, 0x11);
    __m128i mid_0 = _mm_xor_si128(_mm_clmulepi64_si128(a0, h4, 0x10),
                                  _mm_clmulepi64_si128(a0, h4, 0x01));

    __m128i m0_1 = _mm_clmulepi64_si128(a1, h3, 0x00);
    __m128i m3_1 = _mm_clmulepi64_si128(a1, h3, 0x11);
    __m128i mid_1 = _mm_xor_si128(_mm_clmulepi64_si128(a1, h3, 0x10),
                                  _mm_clmulepi64_si128(a1, h3, 0x01));

    __m128i m0_2 = _mm_clmulepi64_si128(a2, h2, 0x00);
    __m128i m3_2 = _mm_clmulepi64_si128(a2, h2, 0x11);
    __m128i mid_2 = _mm_xor_si128(_mm_clmulepi64_si128(a2, h2, 0x10),
                                  _mm_clmulepi64_si128(a2, h2, 0x01));

    __m128i m0_3 = _mm_clmulepi64_si128(a3, h1, 0x00);
    __m128i m3_3 = _mm_clmulepi64_si128(a3, h1, 0x11);
    __m128i mid_3 = _mm_xor_si128(_mm_clmulepi64_si128(a3, h1, 0x10),
                                  _mm_clmulepi64_si128(a3, h1, 0x01));

    __m128i m0 = _mm_xor_si128(_mm_xor_si128(m0_0, m0_1), _mm_xor_si128(m0_2, m0_3));
    __m128i m3 = _mm_xor_si128(_mm_xor_si128(m3_0, m3_1), _mm_xor_si128(m3_2, m3_3));
    __m128i mid = _mm_xor_si128(_mm_xor_si128(mid_0, mid_1), _mm_xor_si128(mid_2, mid_3));

    __m128i lo = _mm_xor_si128(m0, _mm_slli_si128(mid, 8));
    __m128i hi = _mm_xor_si128(m3, _mm_srli_si128(mid, 8));
    return zan_ghash_reduce(lo, hi);
}

__attribute__((target("pclmul,sse4.1")))
static int64_t zan_ghash_update_clmul(const uint8_t *h16, const uint8_t *data, int64_t len, uint8_t *y16) {
    if (len <= 0) return 0;
    const __m128i BSWAP = ZAN_BSWAP128;
    __m128i b = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)h16), BSWAP);
    __m128i y = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)y16), BSWAP);

    int64_t off = 0;
    while (off + 16 <= len) {
        __m128i x = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + off)), BSWAP);
        y = zan_ghash_step_clmul(y, x, b);
        off += 16;
    }
    if (off < len) {
        uint8_t pad[16] = {0};
        memcpy(pad, data + off, (size_t)(len - off));
        __m128i x = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)pad), BSWAP);
        y = zan_ghash_step_clmul(y, x, b);
    }

    _mm_storeu_si128((__m128i*)y16, _mm_shuffle_epi8(y, BSWAP));
    return 0;
}

__attribute__((target("pclmul,sse4.1")))
static inline int64_t zan_ghash_block_clmul(const uint8_t *h16, const uint8_t *x16, uint8_t *y16) {
    return zan_ghash_update_clmul(h16, x16, 16, y16);
}

#endif /* x86 ghash */

#if (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
typedef unsigned __int128 zan_u128;

static inline uint8x16_t zan_bswap128_arm(uint8x16_t v) {
    uint8x16_t r = vrev64q_u8(v);
    return vextq_u8(r, r, 8);
}

__attribute__((target("aes,neon")))
static int64_t zan_ghash_update_pmull(const uint8_t *h16, const uint8_t *data, int64_t len, uint8_t *y16) {
    if (len <= 0) return 0;
    uint64x2_t h = vreinterpretq_u64_u8(zan_bswap128_arm(vld1q_u8(h16)));
    uint64x2_t y = vreinterpretq_u64_u8(zan_bswap128_arm(vld1q_u8(y16)));
    uint64_t b_lo = vgetq_lane_u64(h, 0);
    uint64_t b_hi = vgetq_lane_u64(h, 1);

    int64_t off = 0;
    while (off < len) {
        uint64x2_t x;
        int64_t rem = len - off;
        if (rem >= 16) {
            x = vreinterpretq_u64_u8(zan_bswap128_arm(vld1q_u8(data + off)));
        } else {
            uint8_t pad[16] = {0};
            memcpy(pad, data + off, (size_t)rem);
            x = vreinterpretq_u64_u8(zan_bswap128_arm(vld1q_u8(pad)));
        }
        uint64_t a_lo = vgetq_lane_u64(y, 0) ^ vgetq_lane_u64(x, 0);
        uint64_t a_hi = vgetq_lane_u64(y, 1) ^ vgetq_lane_u64(x, 1);

        zan_u128 m0  = (zan_u128)vmull_p64((poly64_t)a_lo, (poly64_t)b_lo);
        zan_u128 m3  = (zan_u128)vmull_p64((poly64_t)a_hi, (poly64_t)b_hi);
        zan_u128 mid = (zan_u128)vmull_p64((poly64_t)a_lo, (poly64_t)b_hi)
                     ^ (zan_u128)vmull_p64((poly64_t)a_hi, (poly64_t)b_lo);
        zan_u128 lo = m0 ^ (mid << 64);
        zan_u128 hi = m3 ^ (mid >> 64);

        uint64_t bit127 = (uint64_t)(lo >> 63) & 1;
        zan_u128 E = lo ^ ((zan_u128)bit127 << 127);
        zan_u128 F = (E >> 6) ^ (E >> 1) ^ E ^ (E << 1);
        uint64_t s = 0, low6 = (uint64_t)lo & 0x3F;
        for (int i = 0; i < 6; i++) if ((low6 >> i) & 1) {
            s ^= 0xE1ULL << (51 + i);          /* x^(133-i) = x^(5-i) * q */
            if (i == 0) s ^= 0xE1ULL << 56;    /* x^128 = q, only t = 0 */
        }
        zan_u128 direct = (hi << 1) ^ bit127;
        zan_u128 res = F ^ direct ^ ((zan_u128)s << 64);

        y = vcombine_u64(vmov_n_u64((uint64_t)res), vmov_n_u64((uint64_t)(res >> 64)));
        off += 16;
    }

    vst1q_u8(y16, zan_bswap128_arm(vreinterpretq_u8_u64(y)));
    return 0;
}

__attribute__((target("aes,neon")))
static inline int64_t zan_ghash_block_pmull(const uint8_t *h16, const uint8_t *x16, uint8_t *y16) {
    return zan_ghash_update_pmull(h16, x16, 16, y16);
}

#endif /* arm ghash */

static int zan_ghash_kat(void) {
    static const uint8_t H[16]  = { 0x03,0x14,0x25,0x36,0x47,0x58,0x69,0x7a,0x8b,0x9c,0xad,0xbe,0xcf,0xe0,0xf1,0x02 };
    static const uint8_t X1[16] = { 0x07,0x24,0x41,0x5e,0x7b,0x98,0xb5,0xd2,0xef,0x0c,0x29,0x46,0x63,0x80,0x9d,0xba };
    static const uint8_t X2[16] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x80 };
    static const uint8_t want[16] = { 0x57,0xe7,0xfc,0x2f,0x3a,0xe6,0xd8,0x6a,0x99,0x19,0x76,0x64,0x6a,0x70,0x7e,0xe2 };
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    uint8_t y[16] = {0};
    if (zan_ghash_block_clmul(H, X1, y) != 0) return 0;
    if (zan_ghash_block_clmul(H, X2, y) != 0) return 0;
    return memcmp(y, want, 16) == 0;
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    uint8_t y[16] = {0};
    if (zan_ghash_block_pmull(H, X1, y) != 0) return 0;
    if (zan_ghash_block_pmull(H, X2, y) != 0) return 0;
    return memcmp(y, want, 16) == 0;
#else
    (void)H; (void)X1; (void)X2; (void)want;
    return 1;
#endif
}


int64_t zan_hw_ghash_block(const uint8_t *h16, const uint8_t *x16, uint8_t *y16) {
    if (!h16 || !x16 || !y16) return -1;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_gate(&g_gate_ghash, zan_hw_has_pclmul(), zan_ghash_kat)) {
        return zan_ghash_block_clmul(h16, x16, y16);
    }
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_gate(&g_gate_ghash, zan_hw_arm_pmull(), zan_ghash_kat)) {
        return zan_ghash_block_pmull(h16, x16, y16);
    }
#endif
    return -1;
}

int64_t zan_hw_ghash_update(const uint8_t *h16, const uint8_t *data, int64_t len, uint8_t *y16) {
    if (!h16 || !y16) return -1;
    if (len <= 0) return 0;
    if (!data) return -1;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_gate(&g_gate_ghash, zan_hw_has_pclmul(), zan_ghash_kat)) {
        return zan_ghash_update_clmul(h16, data, len, y16);
    }
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_gate(&g_gate_ghash, zan_hw_arm_pmull(), zan_ghash_kat)) {
        return zan_ghash_update_pmull(h16, data, len, y16);
    }
#endif
    return -1;
}


#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
__attribute__((target("aes,pclmul,sse4.1")))
static void zan_aes_gcm_encrypt_ni_fused(const uint8_t rkb[15][16], int nr,
                                         const uint8_t h_powers[8][16],
                                         const uint8_t *in, int64_t inLen,
                                         uint8_t counter16[16],
                                         uint8_t *out, uint8_t y16[16]) {
    const __m128i BSWAP = ZAN_BSWAP128;
    __m128i b = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)h_powers[0]), BSWAP);
    __m128i h1 = b;
    __m128i h2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)h_powers[1]), BSWAP);
    __m128i h3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)h_powers[2]), BSWAP);
    __m128i h4 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)h_powers[3]), BSWAP);
    __m128i y = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)y16), BSWAP);

    __m128i rk[15];
    for (int i = 0; i <= nr; i++) {
        rk[i] = _mm_loadu_si128((const __m128i*)rkb[i]);
    }

    uint8_t ctr0[16];
    memcpy(ctr0, counter16, 16);
    uint32_t c_lo = ((uint32_t)ctr0[12] << 24) | ((uint32_t)ctr0[13] << 16) |
                    ((uint32_t)ctr0[14] << 8)  | (uint32_t)ctr0[15];

    uint8_t cblk[4][16];
    memcpy(cblk[0], ctr0, 12);
    memcpy(cblk[1], ctr0, 12);
    memcpy(cblk[2], ctr0, 12);
    memcpy(cblk[3], ctr0, 12);

    int64_t off = 0;
    while (off + 64 <= inLen) {
        uint32_t c0 = c_lo;
        uint32_t c1 = c_lo + 1;
        uint32_t c2 = c_lo + 2;
        uint32_t c3 = c_lo + 3;
        c_lo += 4;

        uint32_t bc0 = __builtin_bswap32(c0);
        uint32_t bc1 = __builtin_bswap32(c1);
        uint32_t bc2 = __builtin_bswap32(c2);
        uint32_t bc3 = __builtin_bswap32(c3);
        memcpy(&cblk[0][12], &bc0, 4);
        memcpy(&cblk[1][12], &bc1, 4);
        memcpy(&cblk[2][12], &bc2, 4);
        memcpy(&cblk[3][12], &bc3, 4);

        __m128i t0 = _mm_xor_si128(_mm_loadu_si128((const __m128i*)cblk[0]), rk[0]);
        __m128i t1 = _mm_xor_si128(_mm_loadu_si128((const __m128i*)cblk[1]), rk[0]);
        __m128i t2 = _mm_xor_si128(_mm_loadu_si128((const __m128i*)cblk[2]), rk[0]);
        __m128i t3 = _mm_xor_si128(_mm_loadu_si128((const __m128i*)cblk[3]), rk[0]);

        for (int r = 1; r < nr; r++) {
            t0 = _mm_aesenc_si128(t0, rk[r]);
            t1 = _mm_aesenc_si128(t1, rk[r]);
            t2 = _mm_aesenc_si128(t2, rk[r]);
            t3 = _mm_aesenc_si128(t3, rk[r]);
        }
        t0 = _mm_aesenclast_si128(t0, rk[nr]);
        t1 = _mm_aesenclast_si128(t1, rk[nr]);
        t2 = _mm_aesenclast_si128(t2, rk[nr]);
        t3 = _mm_aesenclast_si128(t3, rk[nr]);

        __m128i ct0 = _mm_xor_si128(t0, _mm_loadu_si128((const __m128i*)(in + off)));
        __m128i ct1 = _mm_xor_si128(t1, _mm_loadu_si128((const __m128i*)(in + off + 16)));
        __m128i ct2 = _mm_xor_si128(t2, _mm_loadu_si128((const __m128i*)(in + off + 32)));
        __m128i ct3 = _mm_xor_si128(t3, _mm_loadu_si128((const __m128i*)(in + off + 48)));

        _mm_storeu_si128((__m128i*)(out + off), ct0);
        _mm_storeu_si128((__m128i*)(out + off + 16), ct1);
        _mm_storeu_si128((__m128i*)(out + off + 32), ct2);
        _mm_storeu_si128((__m128i*)(out + off + 48), ct3);

        y = zan_ghash_4blocks_clmul(y,
                                    _mm_shuffle_epi8(ct0, BSWAP),
                                    _mm_shuffle_epi8(ct1, BSWAP),
                                    _mm_shuffle_epi8(ct2, BSWAP),
                                    _mm_shuffle_epi8(ct3, BSWAP),
                                    h1, h2, h3, h4);

        off += 64;
    }

    ctr0[12] = (uint8_t)(c_lo >> 24); ctr0[13] = (uint8_t)(c_lo >> 16);
    ctr0[14] = (uint8_t)(c_lo >> 8);  ctr0[15] = (uint8_t)c_lo;

    while (off < inLen) {
        __m128i ks = _mm_xor_si128(_mm_loadu_si128((const __m128i*)ctr0), rk[0]);
        for (int r = 1; r < nr; r++) ks = _mm_aesenc_si128(ks, rk[r]);
        ks = _mm_aesenclast_si128(ks, rk[nr]);

        int avail = (int)(inLen - off);
        __m128i blk;
        if (avail >= 16) {
            blk = _mm_loadu_si128((const __m128i*)(in + off));
            __m128i o = _mm_xor_si128(blk, ks);
            _mm_storeu_si128((__m128i*)(out + off), o);
            y = zan_ghash_step_clmul(y, _mm_shuffle_epi8(o, BSWAP), b);
        } else {
            uint8_t tmp[16] = {0};
            memcpy(tmp, in + off, (size_t)avail);
            blk = _mm_loadu_si128((const __m128i*)tmp);
            __m128i o = _mm_xor_si128(blk, ks);
            _mm_storeu_si128((__m128i*)tmp, o);
            memcpy(out + off, tmp, (size_t)avail);
            memset(tmp + avail, 0, (size_t)(16 - avail));
            y = zan_ghash_step_clmul(y, _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)tmp), BSWAP), b);
        }

        for (int j = 15; j >= 0; j--) {
            if (++ctr0[j] != 0) break;
        }
        off += 16;
    }

    memcpy(counter16, ctr0, 16);
    _mm_storeu_si128((__m128i*)y16, _mm_shuffle_epi8(y, BSWAP));
}

__attribute__((target("aes,pclmul,sse4.1")))
static void zan_aes_gcm_decrypt_ni_fused(const uint8_t rkb[15][16], int nr,
                                         const uint8_t h_powers[8][16],
                                         const uint8_t *in, int64_t inLen,
                                         uint8_t counter16[16],
                                         uint8_t *out, uint8_t y16[16]) {
    const __m128i BSWAP = ZAN_BSWAP128;
    __m128i b = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)h_powers[0]), BSWAP);
    __m128i h1 = b;
    __m128i h2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)h_powers[1]), BSWAP);
    __m128i h3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)h_powers[2]), BSWAP);
    __m128i h4 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)h_powers[3]), BSWAP);
    __m128i y = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)y16), BSWAP);

    __m128i rk[15];
    for (int i = 0; i <= nr; i++) {
        rk[i] = _mm_loadu_si128((const __m128i*)rkb[i]);
    }

    uint8_t ctr0[16];
    memcpy(ctr0, counter16, 16);
    uint32_t c_lo = ((uint32_t)ctr0[12] << 24) | ((uint32_t)ctr0[13] << 16) |
                    ((uint32_t)ctr0[14] << 8)  | (uint32_t)ctr0[15];

    uint8_t cblk[4][16];
    memcpy(cblk[0], ctr0, 12);
    memcpy(cblk[1], ctr0, 12);
    memcpy(cblk[2], ctr0, 12);
    memcpy(cblk[3], ctr0, 12);

    int64_t off = 0;
    while (off + 64 <= inLen) {
        uint32_t c0 = c_lo;
        uint32_t c1 = c_lo + 1;
        uint32_t c2 = c_lo + 2;
        uint32_t c3 = c_lo + 3;
        c_lo += 4;

        uint32_t bc0 = __builtin_bswap32(c0);
        uint32_t bc1 = __builtin_bswap32(c1);
        uint32_t bc2 = __builtin_bswap32(c2);
        uint32_t bc3 = __builtin_bswap32(c3);
        memcpy(&cblk[0][12], &bc0, 4);
        memcpy(&cblk[1][12], &bc1, 4);
        memcpy(&cblk[2][12], &bc2, 4);
        memcpy(&cblk[3][12], &bc3, 4);

        __m128i t0 = _mm_xor_si128(_mm_loadu_si128((const __m128i*)cblk[0]), rk[0]);
        __m128i t1 = _mm_xor_si128(_mm_loadu_si128((const __m128i*)cblk[1]), rk[0]);
        __m128i t2 = _mm_xor_si128(_mm_loadu_si128((const __m128i*)cblk[2]), rk[0]);
        __m128i t3 = _mm_xor_si128(_mm_loadu_si128((const __m128i*)cblk[3]), rk[0]);

        for (int r = 1; r < nr; r++) {
            t0 = _mm_aesenc_si128(t0, rk[r]);
            t1 = _mm_aesenc_si128(t1, rk[r]);
            t2 = _mm_aesenc_si128(t2, rk[r]);
            t3 = _mm_aesenc_si128(t3, rk[r]);
        }
        t0 = _mm_aesenclast_si128(t0, rk[nr]);
        t1 = _mm_aesenclast_si128(t1, rk[nr]);
        t2 = _mm_aesenclast_si128(t2, rk[nr]);
        t3 = _mm_aesenclast_si128(t3, rk[nr]);

        __m128i in0 = _mm_loadu_si128((const __m128i*)(in + off));
        __m128i in1 = _mm_loadu_si128((const __m128i*)(in + off + 16));
        __m128i in2 = _mm_loadu_si128((const __m128i*)(in + off + 32));
        __m128i in3 = _mm_loadu_si128((const __m128i*)(in + off + 48));

        y = zan_ghash_4blocks_clmul(y,
                                    _mm_shuffle_epi8(in0, BSWAP),
                                    _mm_shuffle_epi8(in1, BSWAP),
                                    _mm_shuffle_epi8(in2, BSWAP),
                                    _mm_shuffle_epi8(in3, BSWAP),
                                    h1, h2, h3, h4);

        _mm_storeu_si128((__m128i*)(out + off), _mm_xor_si128(in0, t0));
        _mm_storeu_si128((__m128i*)(out + off + 16), _mm_xor_si128(in1, t1));
        _mm_storeu_si128((__m128i*)(out + off + 32), _mm_xor_si128(in2, t2));
        _mm_storeu_si128((__m128i*)(out + off + 48), _mm_xor_si128(in3, t3));

        off += 64;
    }

    ctr0[12] = (uint8_t)(c_lo >> 24); ctr0[13] = (uint8_t)(c_lo >> 16);
    ctr0[14] = (uint8_t)(c_lo >> 8);  ctr0[15] = (uint8_t)c_lo;

    while (off < inLen) {
        __m128i ks = _mm_xor_si128(_mm_loadu_si128((const __m128i*)ctr0), rk[0]);
        for (int r = 1; r < nr; r++) ks = _mm_aesenc_si128(ks, rk[r]);
        ks = _mm_aesenclast_si128(ks, rk[nr]);

        int avail = (int)(inLen - off);
        if (avail >= 16) {
            __m128i inb = _mm_loadu_si128((const __m128i*)(in + off));
            y = zan_ghash_step_clmul(y, _mm_shuffle_epi8(inb, BSWAP), b);
            _mm_storeu_si128((__m128i*)(out + off), _mm_xor_si128(inb, ks));
        } else {
            uint8_t tmp[16] = {0};
            memcpy(tmp, in + off, (size_t)avail);
            __m128i inb = _mm_loadu_si128((const __m128i*)tmp);
            y = zan_ghash_step_clmul(y, _mm_shuffle_epi8(inb, BSWAP), b);
            __m128i o = _mm_xor_si128(inb, ks);
            _mm_storeu_si128((__m128i*)tmp, o);
            memcpy(out + off, tmp, (size_t)avail);
        }

        for (int j = 15; j >= 0; j--) {
            if (++ctr0[j] != 0) break;
        }
        off += 16;
    }

    memcpy(counter16, ctr0, 16);
    _mm_storeu_si128((__m128i*)y16, _mm_shuffle_epi8(y, BSWAP));
}
#endif


int64_t zan_hw_aes_gcm_init(uint8_t *ctxBuf, int64_t ctxLen, const uint8_t *key, int keybits) {
    if (!ctxBuf || ctxLen < (int64_t)sizeof(zan_gcm_ctx_t) || !key) return -1;
    if (keybits != 128 && keybits != 192 && keybits != 256) return -1;

    zan_gcm_ctx_t *ctx = (zan_gcm_ctx_t*)ctxBuf;
    memset(ctx, 0, sizeof(*ctx));
    ctx->magic = 0x47434D31;
    ctx->version = ZAN_GCM_CTX_VERSION;
    ctx->keybits = keybits;

    zan_aes_expand_key(key, keybits, ctx->rkb, &ctx->nr);

    /* Compute H = E_K(0) */
    uint8_t zero16[16] = {0};
    if (zan_hw_aes_ecb_block(key, keybits, zero16, ctx->h16) != 0) return -1;

    for (int i = 0; i < 16; i++) {
        ctx->h_bswap[i] = ctx->h16[15 - i];
    }

    memcpy(ctx->h_powers[0], ctx->h16, 16);
    for (int p = 1; p < 8; p++) {
        uint8_t zero16_t[16] = {0};
        uint8_t next[16];
        memcpy(next, ctx->h_powers[p - 1], 16);
        zan_hw_ghash_block(ctx->h16, zero16_t, next);
        memcpy(ctx->h_powers[p], next, 16);
    }
    return 0;
}

int64_t zan_hw_aes_gcm_encrypt_ctx(const uint8_t *ctxBuf,
                                   const uint8_t *iv12,
                                   const uint8_t *aad, int64_t aadLen,
                                   const uint8_t *in, int64_t inLen,
                                   uint8_t *out, uint8_t *tag16) {
    if (!ctxBuf || !iv12 || !tag16) return -1;
    const zan_gcm_ctx_t *ctx = (const zan_gcm_ctx_t*)ctxBuf;
    if (ctx->magic != 0x47434D31) return -1;
    if (ctx->version != ZAN_GCM_CTX_VERSION) return -1;
    if (aadLen < 0 || inLen < 0) return -1;
    if (aadLen > 0 && !aad) return -1;
    if (inLen > 0 && (!in || !out)) return -1;

    uint8_t j0[16] = {0};
    memcpy(j0, iv12, 12);
    j0[15] = 1;

    uint8_t ej0[16];
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    if (g_gate_aes == 1 || zan_hw_gate(&g_gate_aes, zan_hw_has_aesni(), zan_aes_kat)) {
        zan_aes_ecb_block_rk(ctx->rkb, ctx->nr, j0, ej0);
    } else {
        if (zan_hw_aes_ecb_block(ctx->rkb[0], ctx->keybits, j0, ej0) != 0) return -1;
    }
#else
    if (zan_hw_aes_ecb_block(ctx->rkb[0], ctx->keybits, j0, ej0) != 0) return -1;
#endif

    uint8_t ctr[16];
    memcpy(ctr, j0, 16);
    ctr[15] = 2;

    uint8_t y16[16] = {0};
    if (aadLen > 0) {
        if (zan_hw_ghash_update(ctx->h16, aad, aadLen, y16) != 0) return -1;
    }

    if (inLen > 0) {
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
        if ((g_gate_aes == 1 || zan_hw_gate(&g_gate_aes, zan_hw_has_aesni(), zan_aes_kat)) &&
            (g_gate_ghash == 1 || zan_hw_gate(&g_gate_ghash, zan_hw_has_pclmul(), zan_ghash_kat))) {
            zan_aes_gcm_encrypt_ni_fused(ctx->rkb, ctx->nr, ctx->h_powers, in, inLen, ctr, out, y16);
        } else if (g_gate_aes == 1 || zan_hw_gate(&g_gate_aes, zan_hw_has_aesni(), zan_aes_kat)) {
            zan_aes_ctr_ni_4way(ctx->rkb, ctx->nr, in, inLen, ctr, out);
            if (zan_hw_ghash_update(ctx->h16, out, inLen, y16) != 0) return -1;
        } else {
            if (zan_hw_aes_ctr_crypt(in, inLen, ctx->rkb[0], ctx->keybits, ctr, out) != inLen) return -1;
            if (zan_hw_ghash_update(ctx->h16, out, inLen, y16) != 0) return -1;
        }
#else
        if (zan_hw_aes_ctr_crypt(in, inLen, ctx->rkb[0], ctx->keybits, ctr, out) != inLen) return -1;
        if (zan_hw_ghash_update(ctx->h16, out, inLen, y16) != 0) return -1;
#endif
    }

    uint8_t lenBlock[16];
    uint64_t aadBits = (uint64_t)aadLen * 8;
    uint64_t inBits = (uint64_t)inLen * 8;
    for (int i = 0; i < 8; i++) {
        lenBlock[i] = (uint8_t)((aadBits >> (8 * (7 - i))) & 0xFF);
        lenBlock[8 + i] = (uint8_t)((inBits >> (8 * (7 - i))) & 0xFF);
    }
    if (zan_hw_ghash_update(ctx->h16, lenBlock, 16, y16) != 0) return -1;

    for (int i = 0; i < 16; i++) {
        tag16[i] = y16[i] ^ ej0[i];
    }
    return inLen;
}

int64_t zan_hw_aes_gcm_decrypt_ctx(const uint8_t *ctxBuf,
                                   const uint8_t *iv12,
                                   const uint8_t *aad, int64_t aadLen,
                                   const uint8_t *in, int64_t inLen,
                                   const uint8_t *tag16, uint8_t *out) {
    if (!ctxBuf || !iv12 || !tag16) return -1;
    const zan_gcm_ctx_t *ctx = (const zan_gcm_ctx_t*)ctxBuf;
    if (ctx->magic != 0x47434D31) return -1;
    if (ctx->version != ZAN_GCM_CTX_VERSION) return -1;
    if (aadLen < 0 || inLen < 0) return -1;
    if (aadLen > 0 && !aad) return -1;
    if (inLen > 0 && (!in || !out)) return -1;

    uint8_t j0[16] = {0};
    memcpy(j0, iv12, 12);
    j0[15] = 1;

    uint8_t ej0[16];
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    if (g_gate_aes == 1 || zan_hw_gate(&g_gate_aes, zan_hw_has_aesni(), zan_aes_kat)) {
        zan_aes_ecb_block_rk(ctx->rkb, ctx->nr, j0, ej0);
    } else {
        if (zan_hw_aes_ecb_block(ctx->rkb[0], ctx->keybits, j0, ej0) != 0) return -1;
    }
#else
    if (zan_hw_aes_ecb_block(ctx->rkb[0], ctx->keybits, j0, ej0) != 0) return -1;
#endif

    uint8_t y16[16] = {0};
    if (aadLen > 0) {
        if (zan_hw_ghash_update(ctx->h16, aad, aadLen, y16) != 0) return -1;
    }

    uint8_t ctr[16];
    memcpy(ctr, j0, 16);
    ctr[15] = 2;

    int did_fused = 0;
    if (inLen > 0) {
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
        if ((g_gate_aes == 1 || zan_hw_gate(&g_gate_aes, zan_hw_has_aesni(), zan_aes_kat)) &&
            (g_gate_ghash == 1 || zan_hw_gate(&g_gate_ghash, zan_hw_has_pclmul(), zan_ghash_kat))) {
            zan_aes_gcm_decrypt_ni_fused(ctx->rkb, ctx->nr, ctx->h_powers, in, inLen, ctr, out, y16);
            did_fused = 1;
        } else {
            if (zan_hw_ghash_update(ctx->h16, in, inLen, y16) != 0) return -1;
        }
#else
        if (zan_hw_ghash_update(ctx->h16, in, inLen, y16) != 0) return -1;
#endif
    }

    uint8_t lenBlock[16];
    uint64_t aadBits = (uint64_t)aadLen * 8;
    uint64_t inBits = (uint64_t)inLen * 8;
    for (int i = 0; i < 8; i++) {
        lenBlock[i] = (uint8_t)((aadBits >> (8 * (7 - i))) & 0xFF);
        lenBlock[8 + i] = (uint8_t)((inBits >> (8 * (7 - i))) & 0xFF);
    }
    if (zan_hw_ghash_update(ctx->h16, lenBlock, 16, y16) != 0) return -1;

    int diff = 0;
    for (int i = 0; i < 16; i++) {
        diff |= (y16[i] ^ ej0[i]) ^ tag16[i];
    }
    if (diff != 0) {
        if (did_fused && inLen > 0 && out) memset(out, 0, (size_t)inLen);
        return -2;
    }

    if (!did_fused && inLen > 0) {
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
        if (g_gate_aes == 1 || zan_hw_gate(&g_gate_aes, zan_hw_has_aesni(), zan_aes_kat)) {
            zan_aes_ctr_ni_4way(ctx->rkb, ctx->nr, in, inLen, ctr, out);
        } else {
            if (zan_hw_aes_ctr_crypt(in, inLen, ctx->rkb[0], ctx->keybits, ctr, out) != inLen) return -1;
        }
#else
        if (zan_hw_aes_ctr_crypt(in, inLen, ctx->rkb[0], ctx->keybits, ctr, out) != inLen) return -1;
#endif
    }
    return inLen;
}

int64_t zan_hw_aes_gcm_encrypt(const uint8_t *key, int keybits,
                               const uint8_t *iv12,
                               const uint8_t *aad, int64_t aadLen,
                               const uint8_t *in, int64_t inLen,
                               uint8_t *out, uint8_t *tag16) {
    zan_gcm_ctx_t ctx;
    if (zan_hw_aes_gcm_init((uint8_t*)&ctx, sizeof(ctx), key, keybits) != 0) return -1;
    return zan_hw_aes_gcm_encrypt_ctx((const uint8_t*)&ctx, iv12, aad, aadLen, in, inLen, out, tag16);
}

int64_t zan_hw_aes_gcm_decrypt(const uint8_t *key, int keybits,
                               const uint8_t *iv12,
                               const uint8_t *aad, int64_t aadLen,
                               const uint8_t *in, int64_t inLen,
                               const uint8_t *tag16, uint8_t *out) {
    zan_gcm_ctx_t ctx;
    if (zan_hw_aes_gcm_init((uint8_t*)&ctx, sizeof(ctx), key, keybits) != 0) return -1;
    return zan_hw_aes_gcm_decrypt_ctx((const uint8_t*)&ctx, iv12, aad, aadLen, in, inLen, tag16, out);
}

