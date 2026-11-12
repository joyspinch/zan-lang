/* rt_hw_sm.c: 国密 SM3、SHA-512 与 SM4 硬件加速指令集向量实现 */

/* 7 */
#if (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))

static const uint32_t zan_sm3_kc_arm[64] = {
    0x79cc4519u,    0xf3988a32u,    0xe7311465u,    0xce6228cbu,
    0x9cc45197u,    0x3988a32fu,    0x7311465eu,    0xe6228cbcu,
    0xcc451979u,    0x988a32f3u,    0x311465e7u,    0x6228cbceu,
    0xc451979cu,    0x88a32f39u,    0x11465e73u,    0x228cbce6u,
    0x9d8a7a87u,    0x3b14f50fu,    0x7629ea1eu,    0xec53d43cu,
    0xd8a7a879u,    0xb14f50f3u,    0x629ea1e7u,    0xc53d43ceu,
    0x8a7a879du,    0x14f50f3bu,    0x29ea1e76u,    0x53d43cecu,
    0xa7a879d8u,    0x4f50f3b1u,    0x9ea1e762u,    0x3d43cec5u,
    0x7a879d8au,    0xf50f3b14u,    0xea1e7629u,    0xd43cec53u,
    0xa879d8a7u,    0x50f3b14fu,    0xa1e7629eu,    0x43cec53du,
    0x879d8a7au,    0x0f3b14f5u,    0x1e7629eau,    0x3cec53d4u,
    0x79d8a7a8u,    0xf3b14f50u,    0xe7629ea1u,    0xcec53d43u,
    0x9d8a7a87u,    0x3b14f50fu,    0x7629ea1eu,    0xec53d43cu,
    0xd8a7a879u,    0xb14f50f3u,    0x629ea1e7u,    0xc53d43ceu,
    0x8a7a879du,    0x14f50f3bu,    0x29ea1e76u,    0x53d43cecu,
    0xa7a879d8u,    0x4f50f3b1u,    0x9ea1e762u,    0x3d43cec5u,
};

__attribute__((target("sm4")))
static void zan_sm3_transform_arm(uint32_t state[8], const uint8_t *data, size_t num_blocks) {
    uint32x4_t rev = vrev64q_u32(vld1q_u32(state));
    uint32x4_t avec = vextq_u32(rev, rev, 2);                       /* {D,C,B,A} */
    rev = vrev64q_u32(vld1q_u32(state + 4));
    uint32x4_t evec = vextq_u32(rev, rev, 2);                       /* {H,G,F,E} */
    uint32x4_t ss1;

    for (size_t blk = 0; blk < num_blocks; blk++, data += 64) {
        uint32x4_t a0 = avec, e0 = evec;
        uint32x4_t W[4];
        for (int i = 0; i < 4; i++) {
            W[i] = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(data + 16 * i)));
        }

        for (int g = 0; g < 16; g++) {
            uint32x4_t cur = W[g % 4];
            uint32x4_t wprime = veorq_u32(cur, W[(g + 1) % 4]);
            uint32x4_t kc = vld1q_u32(&zan_sm3_kc_arm[4 * g]);

            uint32x4_t n1 = vextq_u32(W[(g + 1) % 4], W[(g + 2) % 4], 3);
            uint32x4_t n2 = vextq_u32(W[(g + 2) % 4], W[(g + 3) % 4], 2);
            uint32x4_t m2 = vextq_u32(W[g % 4], W[(g + 1) % 4], 3);
            uint32x4_t ext = vsm3partw1q_u32(W[g % 4], n1, W[(g + 3) % 4]);
            W[g % 4] = vsm3partw2q_u32(ext, n2, m2);

#define ZAN_SM3_ROUND(l)                                                    \
            ss1 = vsm3ss1q_u32(avec, evec, vextq_u32(kc, kc, ((l) + 1) & 3)); \
            if (g < 4) {                                                    \
                avec = vsm3tt1aq_u32(avec, ss1, wprime, (l));               \
                evec = vsm3tt2aq_u32(evec, ss1, cur, (l));                  \
            } else {                                                        \
                avec = vsm3tt1bq_u32(avec, ss1, wprime, (l));               \
                evec = vsm3tt2bq_u32(evec, ss1, cur, (l));                  \
            }
            ZAN_SM3_ROUND(0)
            ZAN_SM3_ROUND(1)
            ZAN_SM3_ROUND(2)
            ZAN_SM3_ROUND(3)
#undef ZAN_SM3_ROUND
        }

        avec = veorq_u32(avec, a0);
        evec = veorq_u32(evec, e0);
        vst1q_u32(state, vextq_u32(vrev64q_u32(avec), vrev64q_u32(avec), 2));
        vst1q_u32(state + 4, vextq_u32(vrev64q_u32(evec), vrev64q_u32(evec), 2));
    }
}

/* GB/T 32905-2012 appendix A sample: SM3("abc"). */
static int zan_sm3_kat_arm(void) {
    static const uint8_t expect[32] = {
        0x66, 0xc7, 0xf0, 0xf4, 0x62, 0xee, 0xed, 0xd9,
        0xd1, 0xf2, 0xd4, 0x6b, 0xdc, 0x10, 0xe4, 0xe2,
        0x41, 0x67, 0xc4, 0x87, 0x5c, 0xf2, 0xf7, 0xa2,
        0x29, 0x7d, 0xa0, 0x2b, 0x8f, 0x4b, 0xa8, 0xe0
    };
    uint32_t st[8] = {
        0x7380166fu, 0x4914b2b9u, 0x172442d7u, 0xda8a0600u,
        0xa96f30bcu, 0x163138aau, 0xe38dee4du, 0xb0fb0e4eu
    };
    uint8_t tail[64];
    tail[0] = 'a'; tail[1] = 'b'; tail[2] = 'c';
    tail[3] = 0x80;
    memset(tail + 4, 0, 52);
    uint64_t bits = 24;
    for (int i = 0; i < 8; i++) {
        tail[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    }
    zan_sm3_transform_arm(st, tail, 1);
    uint8_t out[32];
    for (int i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(st[i] >> 24);
        out[i*4+1] = (uint8_t)(st[i] >> 16);
        out[i*4+2] = (uint8_t)(st[i] >> 8);
        out[i*4+3] = (uint8_t)(st[i]);
    }
    return memcmp(out, expect, 32) == 0 ? 1 : -1;
}
#endif /* aarch64 SM3 */

/* 7 */
int64_t zan_hw_sha512(const uint8_t *data, int64_t len, uint8_t out[64]) {
    (void)data; (void)len; (void)out;
    return -1;
}

int64_t zan_hw_sm3(const uint8_t *data, int64_t len, uint8_t out[32]) {
    if (len < 0) len = 0;
    uint32_t state[8] = {
        0x7380166f, 0x4914b2b9, 0x172442d7, 0xda8a0600,
        0xa96f30bc, 0x163138aa, 0xe38dee4d, 0xb0fb0e4e
    };

    size_t full_blocks = (size_t)len / 64;
    int use_ni = 0;
#if (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    use_ni = zan_hw_gate(&g_gate_sm3, zan_hw_arm_sm3(), zan_sm3_kat_arm);
#endif
    if (!use_ni) return -1;

    if (full_blocks > 0 && data) {
#if (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
        zan_sm3_transform_arm(state, data, full_blocks);
#endif
    }

    uint8_t tail[128];
    size_t rem = (size_t)len % 64;
    if (rem > 0 && data) {
        memcpy(tail, data + full_blocks * 64, rem);
    }
    tail[rem] = 0x80;
    size_t pad_blocks = (rem >= 56) ? 2 : 1;
    size_t total_tail = pad_blocks * 64;
    memset(tail + rem + 1, 0, total_tail - rem - 1);
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) {
        tail[total_tail - 8 + i] = (uint8_t)(bits >> (56 - 8 * i));
    }
#if (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    zan_sm3_transform_arm(state, tail, pad_blocks);
#endif

    for (int i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(state[i] >> 24);
        out[i*4+1] = (uint8_t)(state[i] >> 16);
        out[i*4+2] = (uint8_t)(state[i] >> 8);
        out[i*4+3] = (uint8_t)(state[i]);
    }
    return 0;
}

static inline uint32_t zan_rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

/* —— SM4 恒定时间核心（B-ID85）——
 * 曾用静态 S-box/T-table 直查（rt_hw_accel_sm4_tables.h，已删）：缓存时序
 * 侧信道可还原轮密钥与明文。现把 AESNI 路径（zan_sm4_kat_x86 运行期 KAT
 * 自证）的同构分解 S(x) = M2( AES_S( M1(x) ) ⊕ 0x0f ) 改写为纯标量位运算：
 *   - M1/M2：4→8 位仿射（pshufb 表等价的位选择实现，无查表无分支）；
 *   - AES_S：GF(2^8) mod x^8+x^4+x^3+x+1 的恒定时间逆（Itoh 链 15 乘）
 *     + 仿射 0x63；⊕0x0f 是 aesenclast 的 round key 常量。
 * 探针验证：256 项全表对拍旧 S-box 表逐位一致；GB/T 32907-2016 附录 A
 * KAT（key/pt=0123456789abcdeffedcba9876543210 → 681edf34…4246）通过。 */

#define ZAN_SM4_FK0 0xa3b1bac6u
#define ZAN_SM4_FK1 0x56aa3350u
#define ZAN_SM4_FK2 0x677d9197u
#define ZAN_SM4_FK3 0xb27022dcu

static const uint32_t zan_sm4_ck[32] = {
 0x00070e15u, 0x1c232a31u, 0x383f464du, 0x545b6269u,
 0x70777e85u, 0x8c939aa1u, 0xa8afb6bdu, 0xc4cbd2d9u,
 0xe0e7eef5u, 0xfc030a11u, 0x181f262du, 0x343b4249u,
 0x50575e65u, 0x6c737a81u, 0x888f969du, 0xa4abb2b9u,
 0xc0c7ced5u, 0xdce3eaf1u, 0xf8ff060du, 0x141b2229u,
 0x30373e45u, 0x4c535a61u, 0x686f767du, 0x848b9299u,
 0xa0a7aeb5u, 0xbcc3cad1u, 0xd8dfe6edu, 0xf4fb0209u,
 0x10171e25u, 0x2c333a41u, 0x484f565du, 0x646b7279u,
};

/* 位选择仿射（M1/M2 的 pshufb 表等价式）：r = c ^ XOR d[i]·bit_i(v)。
 * 常量取自 m1l/m1h/m2l/m2h（_mm_set_epi64x 解码后按仿射性提取）。 */
static inline uint8_t zan_sm4_sel4(uint8_t c, uint8_t d0, uint8_t d1,
                                   uint8_t d2, uint8_t d3, uint8_t v) {
    uint8_t r = c;
    r ^= (uint8_t)(d0 & (uint8_t)(0u - (v & 1)));
    r ^= (uint8_t)(d1 & (uint8_t)(0u - ((v >> 1) & 1)));
    r ^= (uint8_t)(d2 & (uint8_t)(0u - ((v >> 2) & 1)));
    r ^= (uint8_t)(d3 & (uint8_t)(0u - ((v >> 3) & 1)));
    return r;
}

/* GF(2^8) mod x^8+x^4+x^3+x+1 恒定时间乘（固定 8 轮移位-规约） */
static inline uint8_t zan_sm4_gf_mul(uint8_t a, uint8_t b) {
    uint8_t r = 0;
    for (int i = 0; i < 8; i++) {
        r ^= (uint8_t)(a & (uint8_t)(0u - (b & 1)));
        uint8_t hi = (uint8_t)(a >> 7);
        a = (uint8_t)((uint8_t)(a << 1) ^ (uint8_t)(0x1b & (uint8_t)(0u - hi)));
        b >>= 1;
    }
    return r;
}

/* 域内逆：x^254 = x^(250+4)，Itoh 链（固定 15 次域乘），x=0 归 0 */
static inline uint8_t zan_sm4_gf_inv(uint8_t x) {
    uint8_t x2   = zan_sm4_gf_mul(x, x);
    uint8_t x4   = zan_sm4_gf_mul(x2, x2);
    uint8_t x3   = zan_sm4_gf_mul(x2, x);
    uint8_t x6   = zan_sm4_gf_mul(x3, x3);
    uint8_t x7   = zan_sm4_gf_mul(x6, x);
    uint8_t x14  = zan_sm4_gf_mul(x7, x7);
    uint8_t x15  = zan_sm4_gf_mul(x14, x);
    uint8_t x30  = zan_sm4_gf_mul(x15, x15);
    uint8_t x31  = zan_sm4_gf_mul(x30, x);
    uint8_t x62  = zan_sm4_gf_mul(x31, x31);
    uint8_t x124 = zan_sm4_gf_mul(x62, x62);
    uint8_t x125 = zan_sm4_gf_mul(x124, x);
    uint8_t x250 = zan_sm4_gf_mul(x125, x125);
    uint8_t x252 = zan_sm4_gf_mul(x250, x2);
    uint8_t x254 = zan_sm4_gf_mul(x252, x2);
    return (uint8_t)(x254 & (uint8_t)(0u - (x != 0)));
}

static inline uint8_t zan_rotl8(uint8_t v, int n) {
    return (uint8_t)((uint8_t)(v << n) | (uint8_t)(v >> (8 - n)));
}

/* AES S-box（恒定时间）：仿射 = v ^ rotl1 ^ rotl2 ^ rotl3 ^ rotl4 ^ 0x63 */
static inline uint8_t zan_sm4_aes_sbox(uint8_t u) {
    uint8_t v = zan_sm4_gf_inv(u);
    return (uint8_t)(v ^ zan_rotl8(v, 1) ^ zan_rotl8(v, 2) ^
                     zan_rotl8(v, 3) ^ zan_rotl8(v, 4) ^ 0x63);
}

/* SM4 S-box 恒定时间合成（M1 → AES_S ⊕0x0f → M2，M2 低半字节取反） */
static inline uint8_t zan_sm4_sbox(uint8_t x) {
    uint8_t u = (uint8_t)(
        zan_sm4_sel4(0x00, 0xa2, 0x49, 0x09, 0x12, (uint8_t)(x >> 4)) ^
        zan_sm4_sel4(0x01, 0x06, 0x73, 0xe5, 0x56, (uint8_t)(x & 0x0f)));
    uint8_t v = (uint8_t)(zan_sm4_aes_sbox(u) ^ 0x0f);
    return (uint8_t)(
        zan_sm4_sel4(0x00, 0xdc, 0xaf, 0xdd, 0xbf, (uint8_t)(v >> 4)) ^
        zan_sm4_sel4(0x34, 0x3c, 0xa9, 0xfa, 0xb6, (uint8_t)(~v & 0x0f)));
}

/* 轮函数 τ：4 字节 S-box 并置（恒定时间） */
static inline uint32_t zan_sm4_tau(uint32_t x) {
    return ((uint32_t)zan_sm4_sbox((uint8_t)(x >> 24)) << 24) |
           ((uint32_t)zan_sm4_sbox((uint8_t)(x >> 16)) << 16) |
           ((uint32_t)zan_sm4_sbox((uint8_t)(x >> 8)) << 8) |
            (uint32_t)zan_sm4_sbox((uint8_t)x);
}

/* 轮函数 L（加解密）：B ^ <<<2 ^ <<<10 ^ <<<18 ^ <<<24 */
static inline uint32_t zan_sm4_l(uint32_t b) {
    return b ^ zan_rotl32(b, 2) ^ zan_rotl32(b, 10) ^
               zan_rotl32(b, 18) ^ zan_rotl32(b, 24);
}

/* 8 */
static inline uint32_t zan_sm4_lp(uint32_t b) {
    return b ^ zan_rotl32(b, 13) ^ zan_rotl32(b, 23);
}

static void zan_sm4_set_key(uint32_t rk[32], const uint8_t key[16]) {
    uint32_t K[36];
    for (int i = 0; i < 4; i++) {
        uint32_t w = ((uint32_t)key[i*4 + 0] << 24) |
                     ((uint32_t)key[i*4 + 1] << 16) |
                     ((uint32_t)key[i*4 + 2] << 8)  |
                     ((uint32_t)key[i*4 + 3]);
        K[i] = w ^ ((const uint32_t[]){ZAN_SM4_FK0, ZAN_SM4_FK1, ZAN_SM4_FK2, ZAN_SM4_FK3})[i];
    }
    for (int i = 0; i < 32; i++) {
        uint32_t x = K[i+1] ^ K[i+2] ^ K[i+3] ^ zan_sm4_ck[i];
        K[i+4] = K[i] ^ zan_sm4_lp(zan_sm4_tau(x));
        rk[i] = K[i+4];
    }
}

static inline void zan_sm4_crypt_block(const uint32_t rk[32], const uint8_t in[16], uint8_t out[16]) {
    uint32_t x0 = ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) | in[3];
    uint32_t x1 = ((uint32_t)in[4] << 24) | ((uint32_t)in[5] << 16) | ((uint32_t)in[6] << 8) | in[7];
    uint32_t x2 = ((uint32_t)in[8] << 24) | ((uint32_t)in[9] << 16) | ((uint32_t)in[10] << 8) | in[11];
    uint32_t x3 = ((uint32_t)in[12] << 24) | ((uint32_t)in[13] << 16) | ((uint32_t)in[14] << 8) | in[15];

    for (int i = 0; i < 32; i++) {
        uint32_t nx = x0 ^ zan_sm4_l(zan_sm4_tau(x1 ^ x2 ^ x3 ^ rk[i]));
        x0 = x1; x1 = x2; x2 = x3; x3 = nx;
    }

    out[0] = (uint8_t)(x3 >> 24); out[1] = (uint8_t)(x3 >> 16); out[2] = (uint8_t)(x3 >> 8); out[3] = (uint8_t)x3;
    out[4] = (uint8_t)(x2 >> 24); out[5] = (uint8_t)(x2 >> 16); out[6] = (uint8_t)(x2 >> 8); out[7] = (uint8_t)x2;
    out[8] = (uint8_t)(x1 >> 24); out[9] = (uint8_t)(x1 >> 16); out[10] = (uint8_t)(x1 >> 8); out[11] = (uint8_t)x1;
    out[12] = (uint8_t)(x0 >> 24); out[13] = (uint8_t)(x0 >> 16); out[14] = (uint8_t)(x0 >> 8); out[15] = (uint8_t)x0;
}

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))

#define SM4_AESNI_STEP4(idx, a0, a1, a2, a3) do { \
    uint32_t k = rk[idx]; \
    __m128i rk_vec = _mm_set1_epi32(k); \
    __m128i x = _mm_xor_si128(_mm_xor_si128(_mm_xor_si128(a1, a2), a3), rk_vec); \
    __m128i y = _mm_shuffle_epi8(m1l, _mm_and_si128(x, c0f)); \
    x = _mm_xor_si128(_mm_shuffle_epi8(m1h, _mm_and_si128(_mm_srli_epi64(x, 4), c0f)), y); \
    x = _mm_aesenclast_si128(_mm_shuffle_epi8(x, shr), c0f); \
    y = _mm_shuffle_epi8(m2l, _mm_andnot_si128(x, c0f)); \
    x = _mm_xor_si128(_mm_shuffle_epi8(m2h, _mm_and_si128(_mm_srli_epi64(x, 4), c0f)), y); \
    y = _mm_xor_si128(_mm_xor_si128(x, _mm_shuffle_epi8(x, r08)), _mm_shuffle_epi8(x, r16)); \
    y = _mm_xor_si128(_mm_slli_epi32(y, 2), _mm_srli_epi32(y, 30)); \
    x = _mm_xor_si128(_mm_xor_si128(x, y), _mm_shuffle_epi8(x, r24)); \
    a0 = _mm_xor_si128(a0, x); \
} while(0)

#define SM4_AESNI_STEP8(idx, a0, a1, a2, a3, b0, b1, b2, b3) do { \
    uint32_t k = rk[idx]; \
    __m128i rk_vec = _mm_set1_epi32(k); \
    __m128i xa = _mm_xor_si128(_mm_xor_si128(_mm_xor_si128(a1, a2), a3), rk_vec); \
    __m128i xb = _mm_xor_si128(_mm_xor_si128(_mm_xor_si128(b1, b2), b3), rk_vec); \
    __m128i ya = _mm_shuffle_epi8(m1l, _mm_and_si128(xa, c0f)); \
    __m128i yb = _mm_shuffle_epi8(m1l, _mm_and_si128(xb, c0f)); \
    xa = _mm_xor_si128(_mm_shuffle_epi8(m1h, _mm_and_si128(_mm_srli_epi64(xa, 4), c0f)), ya); \
    xb = _mm_xor_si128(_mm_shuffle_epi8(m1h, _mm_and_si128(_mm_srli_epi64(xb, 4), c0f)), yb); \
    xa = _mm_aesenclast_si128(_mm_shuffle_epi8(xa, shr), c0f); \
    xb = _mm_aesenclast_si128(_mm_shuffle_epi8(xb, shr), c0f); \
    ya = _mm_shuffle_epi8(m2l, _mm_andnot_si128(xa, c0f)); \
    yb = _mm_shuffle_epi8(m2l, _mm_andnot_si128(xb, c0f)); \
    xa = _mm_xor_si128(_mm_shuffle_epi8(m2h, _mm_and_si128(_mm_srli_epi64(xa, 4), c0f)), ya); \
    xb = _mm_xor_si128(_mm_shuffle_epi8(m2h, _mm_and_si128(_mm_srli_epi64(xb, 4), c0f)), yb); \
    ya = _mm_xor_si128(_mm_xor_si128(xa, _mm_shuffle_epi8(xa, r08)), _mm_shuffle_epi8(xa, r16)); \
    yb = _mm_xor_si128(_mm_xor_si128(xb, _mm_shuffle_epi8(xb, r08)), _mm_shuffle_epi8(xb, r16)); \
    ya = _mm_xor_si128(_mm_slli_epi32(ya, 2), _mm_srli_epi32(ya, 30)); \
    yb = _mm_xor_si128(_mm_slli_epi32(yb, 2), _mm_srli_epi32(yb, 30)); \
    xa = _mm_xor_si128(_mm_xor_si128(xa, ya), _mm_shuffle_epi8(xa, r24)); \
    xb = _mm_xor_si128(_mm_xor_si128(xb, yb), _mm_shuffle_epi8(xb, r24)); \
    a0 = _mm_xor_si128(a0, xa); \
    b0 = _mm_xor_si128(b0, xb); \
} while(0)

#define SM4_VAES_STEP16(idx, a0, a1, a2, a3, b0, b1, b2, b3) do { \
    uint32_t k = rk[idx]; \
    __m256i rk_vec = _mm256_set1_epi32(k); \
    __m256i xa = _mm256_xor_si256(_mm256_xor_si256(_mm256_xor_si256(a1, a2), a3), rk_vec); \
    __m256i xb = _mm256_xor_si256(_mm256_xor_si256(_mm256_xor_si256(b1, b2), b3), rk_vec); \
    __m256i ya = _mm256_shuffle_epi8(m1l, _mm256_and_si256(xa, c0f)); \
    __m256i yb = _mm256_shuffle_epi8(m1l, _mm256_and_si256(xb, c0f)); \
    xa = _mm256_xor_si256(_mm256_shuffle_epi8(m1h, _mm256_and_si256(_mm256_srli_epi64(xa, 4), c0f)), ya); \
    xb = _mm256_xor_si256(_mm256_shuffle_epi8(m1h, _mm256_and_si256(_mm256_srli_epi64(xb, 4), c0f)), yb); \
    xa = _mm256_aesenclast_epi128(_mm256_shuffle_epi8(xa, shr), c0f); \
    xb = _mm256_aesenclast_epi128(_mm256_shuffle_epi8(xb, shr), c0f); \
    ya = _mm256_shuffle_epi8(m2l, _mm256_andnot_si256(xa, c0f)); \
    yb = _mm256_shuffle_epi8(m2l, _mm256_andnot_si256(xb, c0f)); \
    xa = _mm256_xor_si256(_mm256_shuffle_epi8(m2h, _mm256_and_si256(_mm256_srli_epi64(xa, 4), c0f)), ya); \
    xb = _mm256_xor_si256(_mm256_shuffle_epi8(m2h, _mm256_and_si256(_mm256_srli_epi64(xb, 4), c0f)), yb); \
    ya = _mm256_xor_si256(_mm256_xor_si256(xa, _mm256_shuffle_epi8(xa, r08)), _mm256_shuffle_epi8(xa, r16)); \
    yb = _mm256_xor_si256(_mm256_xor_si256(xb, _mm256_shuffle_epi8(xb, r08)), _mm256_shuffle_epi8(xb, r16)); \
    ya = _mm256_xor_si256(_mm256_slli_epi32(ya, 2), _mm256_srli_epi32(ya, 30)); \
    yb = _mm256_xor_si256(_mm256_slli_epi32(yb, 2), _mm256_srli_epi32(yb, 30)); \
    xa = _mm256_xor_si256(_mm256_xor_si256(xa, ya), _mm256_shuffle_epi8(xa, r24)); \
    xb = _mm256_xor_si256(_mm256_xor_si256(xb, yb), _mm256_shuffle_epi8(xb, r24)); \
    a0 = _mm256_xor_si256(a0, xa); \
    b0 = _mm256_xor_si256(b0, xb); \
} while(0)

static inline void zan_sm4_store_unpacked4(uint32_t *d, __m128i a0, __m128i a1, __m128i a2, __m128i a3) {
    uint32_t t0[4], t1[4], t2[4], t3[4];
    _mm_storeu_si128((__m128i*)t0, a0);
    _mm_storeu_si128((__m128i*)t1, a1);
    _mm_storeu_si128((__m128i*)t2, a2);
    _mm_storeu_si128((__m128i*)t3, a3);
    d[0] = t3[0]; d[1] = t2[0]; d[2] = t1[0]; d[3] = t0[0];
    d[4] = t3[1]; d[5] = t2[1]; d[6] = t1[1]; d[7] = t0[1];
    d[8] = t3[2]; d[9] = t2[2]; d[10] = t1[2]; d[11] = t0[2];
    d[12] = t3[3]; d[13] = t2[3]; d[14] = t1[3]; d[15] = t0[3];
}

__attribute__((target("aes,ssse3")))
static void zan_sm4_crypt4_aesni(const uint32_t rk[32], const void *src, void *dst) {
    const __m128i c0f = _mm_set1_epi8(0x0f);
    const __m128i flp = _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12);
    const __m128i shr = _mm_setr_epi8(0,13,10,7, 4,1,14,11, 8,5,2,15, 12,9,6,3);
    const __m128i m1l = _mm_set_epi64x(0xC7C1B4B222245157ULL, 0x9197E2E474720701ULL);
    const __m128i m1h = _mm_set_epi64x(0xF052B91BF95BB012ULL, 0xE240AB09EB49A200ULL);
    const __m128i m2l = _mm_set_epi64x(0xEDD14478172BBE82ULL, 0x5B67F2CEA19D0834ULL);
    const __m128i m2h = _mm_set_epi64x(0x11CDBE62CC1063BFULL, 0xAE7201DD73AFDC00ULL);
    const __m128i r08 = _mm_setr_epi8(3,0,1,2, 7,4,5,6, 11,8,9,10, 15,12,13,14);
    const __m128i r16 = _mm_setr_epi8(2,3,0,1, 6,7,4,5, 10,11,8,9, 14,15,12,13);
    const __m128i r24 = _mm_setr_epi8(1,2,3,0, 5,6,7,4, 9,10,11,8, 13,14,15,12);

    const uint32_t *p = (const uint32_t *)src;
    __m128i a0 = _mm_shuffle_epi8(_mm_set_epi32(p[12], p[8], p[4], p[0]), flp);
    __m128i a1 = _mm_shuffle_epi8(_mm_set_epi32(p[13], p[9], p[5], p[1]), flp);
    __m128i a2 = _mm_shuffle_epi8(_mm_set_epi32(p[14], p[10], p[6], p[2]), flp);
    __m128i a3 = _mm_shuffle_epi8(_mm_set_epi32(p[15], p[11], p[7], p[3]), flp);

    for (int i = 0; i < 32; i += 4) {
        SM4_AESNI_STEP4(i+0, a0, a1, a2, a3);
        SM4_AESNI_STEP4(i+1, a1, a2, a3, a0);
        SM4_AESNI_STEP4(i+2, a2, a3, a0, a1);
        SM4_AESNI_STEP4(i+3, a3, a0, a1, a2);
    }

    a0 = _mm_shuffle_epi8(a0, flp);
    a1 = _mm_shuffle_epi8(a1, flp);
    a2 = _mm_shuffle_epi8(a2, flp);
    a3 = _mm_shuffle_epi8(a3, flp);

    uint32_t *d = (uint32_t *)dst;
    zan_sm4_store_unpacked4(d, a0, a1, a2, a3);
}

__attribute__((target("aes,ssse3")))
static void zan_sm4_crypt8_aesni(const uint32_t rk[32], const void *src, void *dst) {
    const __m128i c0f = _mm_set1_epi8(0x0f);
    const __m128i flp = _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12);
    const __m128i shr = _mm_setr_epi8(0,13,10,7, 4,1,14,11, 8,5,2,15, 12,9,6,3);
    const __m128i m1l = _mm_set_epi64x(0xC7C1B4B222245157ULL, 0x9197E2E474720701ULL);
    const __m128i m1h = _mm_set_epi64x(0xF052B91BF95BB012ULL, 0xE240AB09EB49A200ULL);
    const __m128i m2l = _mm_set_epi64x(0xEDD14478172BBE82ULL, 0x5B67F2CEA19D0834ULL);
    const __m128i m2h = _mm_set_epi64x(0x11CDBE62CC1063BFULL, 0xAE7201DD73AFDC00ULL);
    const __m128i r08 = _mm_setr_epi8(3,0,1,2, 7,4,5,6, 11,8,9,10, 15,12,13,14);
    const __m128i r16 = _mm_setr_epi8(2,3,0,1, 6,7,4,5, 10,11,8,9, 14,15,12,13);
    const __m128i r24 = _mm_setr_epi8(1,2,3,0, 5,6,7,4, 9,10,11,8, 13,14,15,12);

    const uint32_t *p0 = (const uint32_t *)src;
    const uint32_t *p1 = (const uint32_t *)((const uint8_t *)src + 64);

    __m128i a0 = _mm_shuffle_epi8(_mm_set_epi32(p0[12], p0[8], p0[4], p0[0]), flp);
    __m128i a1 = _mm_shuffle_epi8(_mm_set_epi32(p0[13], p0[9], p0[5], p0[1]), flp);
    __m128i a2 = _mm_shuffle_epi8(_mm_set_epi32(p0[14], p0[10], p0[6], p0[2]), flp);
    __m128i a3 = _mm_shuffle_epi8(_mm_set_epi32(p0[15], p0[11], p0[7], p0[3]), flp);

    __m128i b0 = _mm_shuffle_epi8(_mm_set_epi32(p1[12], p1[8], p1[4], p1[0]), flp);
    __m128i b1 = _mm_shuffle_epi8(_mm_set_epi32(p1[13], p1[9], p1[5], p1[1]), flp);
    __m128i b2 = _mm_shuffle_epi8(_mm_set_epi32(p1[14], p1[10], p1[6], p1[2]), flp);
    __m128i b3 = _mm_shuffle_epi8(_mm_set_epi32(p1[15], p1[11], p1[7], p1[3]), flp);

    for (int i = 0; i < 32; i += 4) {
        SM4_AESNI_STEP8(i+0, a0, a1, a2, a3, b0, b1, b2, b3);
        SM4_AESNI_STEP8(i+1, a1, a2, a3, a0, b1, b2, b3, b0);
        SM4_AESNI_STEP8(i+2, a2, a3, a0, a1, b2, b3, b0, b1);
        SM4_AESNI_STEP8(i+3, a3, a0, a1, a2, b3, b0, b1, b2);
    }

    a0 = _mm_shuffle_epi8(a0, flp);
    a1 = _mm_shuffle_epi8(a1, flp);
    a2 = _mm_shuffle_epi8(a2, flp);
    a3 = _mm_shuffle_epi8(a3, flp);

    b0 = _mm_shuffle_epi8(b0, flp);
    b1 = _mm_shuffle_epi8(b1, flp);
    b2 = _mm_shuffle_epi8(b2, flp);
    b3 = _mm_shuffle_epi8(b3, flp);

    uint32_t *d0 = (uint32_t *)dst;
    uint32_t *d1 = (uint32_t *)((uint8_t *)dst + 64);
    zan_sm4_store_unpacked4(d0, a0, a1, a2, a3);
    zan_sm4_store_unpacked4(d1, b0, b1, b2, b3);
}

__attribute__((target("vaes,avx2")))
static void zan_sm4_crypt16_vaes(const uint32_t rk[32], const void *src, void *dst) {
    const __m256i c0f = _mm256_set1_epi8(0x0f);
    const __m256i flp = _mm256_broadcastsi128_si256(_mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12));
    const __m256i shr = _mm256_broadcastsi128_si256(_mm_setr_epi8(0,13,10,7, 4,1,14,11, 8,5,2,15, 12,9,6,3));
    const __m256i m1l = _mm256_broadcastsi128_si256(_mm_set_epi64x(0xC7C1B4B222245157ULL, 0x9197E2E474720701ULL));
    const __m256i m1h = _mm256_broadcastsi128_si256(_mm_set_epi64x(0xF052B91BF95BB012ULL, 0xE240AB09EB49A200ULL));
    const __m256i m2l = _mm256_broadcastsi128_si256(_mm_set_epi64x(0xEDD14478172BBE82ULL, 0x5B67F2CEA19D0834ULL));
    const __m256i m2h = _mm256_broadcastsi128_si256(_mm_set_epi64x(0x11CDBE62CC1063BFULL, 0xAE7201DD73AFDC00ULL));
    const __m256i r08 = _mm256_broadcastsi128_si256(_mm_setr_epi8(3,0,1,2, 7,4,5,6, 11,8,9,10, 15,12,13,14));
    const __m256i r16 = _mm256_broadcastsi128_si256(_mm_setr_epi8(2,3,0,1, 6,7,4,5, 10,11,8,9, 14,15,12,13));
    const __m256i r24 = _mm256_broadcastsi128_si256(_mm_setr_epi8(1,2,3,0, 5,6,7,4, 9,10,11,8, 13,14,15,12));

    const uint32_t *p0 = (const uint32_t *)src;
    const uint32_t *p1 = (const uint32_t *)((const uint8_t *)src + 64);
    const uint32_t *p2 = (const uint32_t *)((const uint8_t *)src + 128);
    const uint32_t *p3 = (const uint32_t *)((const uint8_t *)src + 192);

    __m128i a0_lo = _mm_set_epi32(p0[12], p0[8], p0[4], p0[0]);
    __m128i a0_hi = _mm_set_epi32(p1[12], p1[8], p1[4], p1[0]);
    __m256i a0 = _mm256_shuffle_epi8(_mm256_set_m128i(a0_hi, a0_lo), flp);

    __m128i a1_lo = _mm_set_epi32(p0[13], p0[9], p0[5], p0[1]);
    __m128i a1_hi = _mm_set_epi32(p1[13], p1[9], p1[5], p1[1]);
    __m256i a1 = _mm256_shuffle_epi8(_mm256_set_m128i(a1_hi, a1_lo), flp);

    __m128i a2_lo = _mm_set_epi32(p0[14], p0[10], p0[6], p0[2]);
    __m128i a2_hi = _mm_set_epi32(p1[14], p1[10], p1[6], p1[2]);
    __m256i a2 = _mm256_shuffle_epi8(_mm256_set_m128i(a2_hi, a2_lo), flp);

    __m128i a3_lo = _mm_set_epi32(p0[15], p0[11], p0[7], p0[3]);
    __m128i a3_hi = _mm_set_epi32(p1[15], p1[11], p1[7], p1[3]);
    __m256i a3 = _mm256_shuffle_epi8(_mm256_set_m128i(a3_hi, a3_lo), flp);

    __m128i b0_lo = _mm_set_epi32(p2[12], p2[8], p2[4], p2[0]);
    __m128i b0_hi = _mm_set_epi32(p3[12], p3[8], p3[4], p3[0]);
    __m256i b0 = _mm256_shuffle_epi8(_mm256_set_m128i(b0_hi, b0_lo), flp);

    __m128i b1_lo = _mm_set_epi32(p2[13], p2[9], p2[5], p2[1]);
    __m128i b1_hi = _mm_set_epi32(p3[13], p3[9], p3[5], p3[1]);
    __m256i b1 = _mm256_shuffle_epi8(_mm256_set_m128i(b1_hi, b1_lo), flp);

    __m128i b2_lo = _mm_set_epi32(p2[14], p2[10], p2[6], p2[2]);
    __m128i b2_hi = _mm_set_epi32(p3[14], p3[10], p3[6], p3[2]);
    __m256i b2 = _mm256_shuffle_epi8(_mm256_set_m128i(b2_hi, b2_lo), flp);

    __m128i b3_lo = _mm_set_epi32(p2[15], p2[11], p2[7], p2[3]);
    __m128i b3_hi = _mm_set_epi32(p3[15], p3[11], p3[7], p3[3]);
    __m256i b3 = _mm256_shuffle_epi8(_mm256_set_m128i(b3_hi, b3_lo), flp);

    for (int i = 0; i < 32; i += 4) {
        SM4_VAES_STEP16(i+0, a0, a1, a2, a3, b0, b1, b2, b3);
        SM4_VAES_STEP16(i+1, a1, a2, a3, a0, b1, b2, b3, b0);
        SM4_VAES_STEP16(i+2, a2, a3, a0, a1, b2, b3, b0, b1);
        SM4_VAES_STEP16(i+3, a3, a0, a1, a2, b3, b0, b1, b2);
    }

    a0 = _mm256_shuffle_epi8(a0, flp);
    a1 = _mm256_shuffle_epi8(a1, flp);
    a2 = _mm256_shuffle_epi8(a2, flp);
    a3 = _mm256_shuffle_epi8(a3, flp);

    b0 = _mm256_shuffle_epi8(b0, flp);
    b1 = _mm256_shuffle_epi8(b1, flp);
    b2 = _mm256_shuffle_epi8(b2, flp);
    b3 = _mm256_shuffle_epi8(b3, flp);

    __m128i out_a0_lo = _mm256_castsi256_si128(a0);
    __m128i out_a0_hi = _mm256_extracti128_si256(a0, 1);
    __m128i out_a1_lo = _mm256_castsi256_si128(a1);
    __m128i out_a1_hi = _mm256_extracti128_si256(a1, 1);
    __m128i out_a2_lo = _mm256_castsi256_si128(a2);
    __m128i out_a2_hi = _mm256_extracti128_si256(a2, 1);
    __m128i out_a3_lo = _mm256_castsi256_si128(a3);
    __m128i out_a3_hi = _mm256_extracti128_si256(a3, 1);

    __m128i out_b0_lo = _mm256_castsi256_si128(b0);
    __m128i out_b0_hi = _mm256_extracti128_si256(b0, 1);
    __m128i out_b1_lo = _mm256_castsi256_si128(b1);
    __m128i out_b1_hi = _mm256_extracti128_si256(b1, 1);
    __m128i out_b2_lo = _mm256_castsi256_si128(b2);
    __m128i out_b2_hi = _mm256_extracti128_si256(b2, 1);
    __m128i out_b3_lo = _mm256_castsi256_si128(b3);
    __m128i out_b3_hi = _mm256_extracti128_si256(b3, 1);

    uint32_t *d0 = (uint32_t *)dst;
    uint32_t *d1 = (uint32_t *)((uint8_t *)dst + 64);
    uint32_t *d2 = (uint32_t *)((uint8_t *)dst + 128);
    uint32_t *d3 = (uint32_t *)((uint8_t *)dst + 192);

    zan_sm4_store_unpacked4(d0, out_a0_lo, out_a1_lo, out_a2_lo, out_a3_lo);
    zan_sm4_store_unpacked4(d1, out_a0_hi, out_a1_hi, out_a2_hi, out_a3_hi);
    zan_sm4_store_unpacked4(d2, out_b0_lo, out_b1_lo, out_b2_lo, out_b3_lo);
    zan_sm4_store_unpacked4(d3, out_b0_hi, out_b1_hi, out_b2_hi, out_b3_hi);
}

__attribute__((target("aes,ssse3")))
static void zan_sm4_crypt1_aesni(const uint32_t rk[32], const uint8_t in[16], uint8_t out[16]) {
    uint8_t in4[64] = {0};
    uint8_t out4[64];
    memcpy(in4, in, 16);
    zan_sm4_crypt4_aesni(rk, in4, out4);
    memcpy(out, out4, 16);
}

__attribute__((target("aes,ssse3")))
static int zan_sm4_kat_x86(void) {
    static const uint8_t key[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10
    };
    static const uint8_t pt[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10
    };
    static const uint8_t expected_ct[16] = {
        0x68, 0x1e, 0xdf, 0x34, 0xd2, 0x06, 0x96, 0x5e,
        0x86, 0xb3, 0xe9, 0x4f, 0x53, 0x6e, 0x42, 0x46
    };
    uint32_t rk[32];
    zan_sm4_set_key(rk, key);

    uint8_t ct[16];
    zan_sm4_crypt1_aesni(rk, pt, ct);
    if (memcmp(ct, expected_ct, 16) != 0) return 0;

    uint32_t rk_dec[32];
    for (int i = 0; i < 32; i++) rk_dec[i] = rk[31 - i];
    uint8_t recovered[16];
    zan_sm4_crypt1_aesni(rk_dec, ct, recovered);
    if (memcmp(recovered, pt, 16) != 0) return 0;

    return 1;
}

#endif /* x86 AES-NI / VAES */

#if (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))

__attribute__((target("+sm4")))
static void zan_sm4_crypt_block_arm(const uint32_t rk[32], const uint8_t in[16], uint8_t out[16]) {
    uint32x4_t s = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(in)));
    for (int i = 0; i < 32; i += 4) {
        uint32x4_t rk_vec = vld1q_u32(&rk[i]);
        s = vsm4eq_u32(s, rk_vec);
    }
    s = vrev64q_u32(s);
    s = vextq_u32(s, s, 2);
    vst1q_u8(out, vrev32q_u8(vreinterpretq_u8_u32(s)));
}

__attribute__((target("+sm4")))
static int zan_sm4_kat_arm(void) {
    static const uint8_t key[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10
    };
    static const uint8_t pt[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10
    };
    static const uint8_t expected_ct[16] = {
        0x68, 0x1e, 0xdf, 0x34, 0xd2, 0x06, 0x96, 0x5e,
        0x86, 0xb3, 0xe9, 0x4f, 0x53, 0x6e, 0x42, 0x46
    };
    uint32_t rk[32];
    zan_sm4_set_key(rk, key);

    uint8_t ct[16];
    zan_sm4_crypt_block_arm(rk, pt, ct);
    if (memcmp(ct, expected_ct, 16) != 0) return 0;

    uint32_t rk_dec[32];
    for (int i = 0; i < 32; i++) rk_dec[i] = rk[31 - i];
    uint8_t recovered[16];
    zan_sm4_crypt_block_arm(rk_dec, ct, recovered);
    if (memcmp(recovered, pt, 16) != 0) return 0;

    return 1;
}

#endif /* ARM64 FEAT_SM4 */

int64_t zan_hw_sm4_cbc_encrypt(const uint8_t *in, int64_t len,
                               const uint8_t *key, const uint8_t *iv,
                               uint8_t *out) {
    if (len < 0 || !in || !key || !iv || !out) return -1;
    uint32_t rk[32];
    zan_sm4_set_key(rk, key);

    size_t blocks = (size_t)len / 16;
    uint8_t pad_val = (uint8_t)(16 - ((size_t)len % 16));
    size_t total_len = (blocks + 1) * 16;

    int use_hw = 0;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    use_hw = zan_hw_gate(&g_gate_sm4, zan_hw_x86_can(ZAN_HW_REQ_SM4), zan_sm4_kat_x86);
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    use_hw = zan_hw_gate(&g_gate_sm4, zan_hw_arm_sm4(), zan_sm4_kat_arm);
#endif

    uint8_t prev[16];
    memcpy(prev, iv, 16);

    uint8_t block[16];
    for (size_t b = 0; b < blocks; b++) {
        for (int i = 0; i < 16; i++) {
            block[i] = in[b * 16 + i] ^ prev[i];
        }
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
        if (use_hw) {
            zan_sm4_crypt1_aesni(rk, block, out + b * 16);
        } else {
            zan_sm4_crypt_block(rk, block, out + b * 16);
        }
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
        if (use_hw) {
            zan_sm4_crypt_block_arm(rk, block, out + b * 16);
        } else {
            zan_sm4_crypt_block(rk, block, out + b * 16);
        }
#else
        zan_sm4_crypt_block(rk, block, out + b * 16);
#endif
        memcpy(prev, out + b * 16, 16);
    }

    size_t rem = (size_t)len % 16;
    for (size_t i = 0; i < rem; i++) {
        block[i] = in[blocks * 16 + i] ^ prev[i];
    }
    for (size_t i = rem; i < 16; i++) {
        block[i] = pad_val ^ prev[i];
    }
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    if (use_hw) {
        zan_sm4_crypt1_aesni(rk, block, out + blocks * 16);
    } else {
        zan_sm4_crypt_block(rk, block, out + blocks * 16);
    }
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    if (use_hw) {
        zan_sm4_crypt_block_arm(rk, block, out + blocks * 16);
    } else {
        zan_sm4_crypt_block(rk, block, out + blocks * 16);
    }
#else
    zan_sm4_crypt_block(rk, block, out + blocks * 16);
#endif
    return (int64_t)total_len;
}

int64_t zan_hw_sm4_cbc_decrypt(const uint8_t *in, int64_t len,
                               const uint8_t *key, const uint8_t *iv,
                               uint8_t *out) {
    if (len <= 0 || (len % 16) != 0 || !in || !key || !iv || !out) return -1;
    uint32_t rk[32];
    zan_sm4_set_key(rk, key);

    uint32_t rk_dec[32];
    for (int i = 0; i < 32; i++) {
        rk_dec[i] = rk[31 - i];
    }

    size_t blocks = (size_t)len / 16;
    size_t b = 0;

    int use_hw = 0;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    use_hw = zan_hw_gate(&g_gate_sm4, zan_hw_x86_can(ZAN_HW_REQ_SM4), zan_sm4_kat_x86);
    if (use_hw) {
        int use_vaes = zan_hw_x86_can(ZAN_HW_REQ_SM4_VAES);
        if (use_vaes) {
            while (blocks - b >= 16) {
                uint8_t dec16[256];
                zan_sm4_crypt16_vaes(rk_dec, in + b * 16, dec16);
                for (int blk = 0; blk < 16; blk++) {
                    const uint8_t *prev = (b + blk == 0) ? iv : in + (b + blk - 1) * 16;
                    __m128i mdec = _mm_loadu_si128((const __m128i *)(dec16 + blk * 16));
                    __m128i mprv = _mm_loadu_si128((const __m128i *)prev);
                    _mm_storeu_si128((__m128i *)(out + (b + blk) * 16), _mm_xor_si128(mdec, mprv));
                }
                b += 16;
            }
        }

        while (blocks - b >= 8) {
            uint8_t dec8[128];
            zan_sm4_crypt8_aesni(rk_dec, in + b * 16, dec8);
            for (int blk = 0; blk < 8; blk++) {
                const uint8_t *prev = (b + blk == 0) ? iv : in + (b + blk - 1) * 16;
                __m128i mdec = _mm_loadu_si128((const __m128i *)(dec8 + blk * 16));
                __m128i mprv = _mm_loadu_si128((const __m128i *)prev);
                _mm_storeu_si128((__m128i *)(out + (b + blk) * 16), _mm_xor_si128(mdec, mprv));
            }
            b += 8;
        }

        while (blocks - b >= 4) {
            uint8_t dec4[64];
            zan_sm4_crypt4_aesni(rk_dec, in + b * 16, dec4);
            for (int blk = 0; blk < 4; blk++) {
                const uint8_t *prev = (b + blk == 0) ? iv : in + (b + blk - 1) * 16;
                __m128i mdec = _mm_loadu_si128((const __m128i *)(dec4 + blk * 16));
                __m128i mprv = _mm_loadu_si128((const __m128i *)prev);
                _mm_storeu_si128((__m128i *)(out + (b + blk) * 16), _mm_xor_si128(mdec, mprv));
            }
            b += 4;
        }

        while (b < blocks) {
            const uint8_t *prev = (b == 0) ? iv : in + (b - 1) * 16;
            uint8_t blk[16];
            zan_sm4_crypt1_aesni(rk_dec, in + b * 16, blk);
            for (int i = 0; i < 16; i++) {
                out[b * 16 + i] = blk[i] ^ prev[i];
            }
            b++;
        }
    }
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    use_hw = zan_hw_gate(&g_gate_sm4, zan_hw_arm_sm4(), zan_sm4_kat_arm);
    if (use_hw) {
        uint8_t prev[16];
        memcpy(prev, iv, 16);
        uint8_t block[16];
        for (size_t b_idx = 0; b_idx < blocks; b_idx++) {
            uint8_t cur[16];
            memcpy(cur, in + b_idx * 16, 16);
            zan_sm4_crypt_block_arm(rk_dec, cur, block);
            for (int i = 0; i < 16; i++) {
                out[b_idx * 16 + i] = block[i] ^ prev[i];
            }
            memcpy(prev, cur, 16);
        }
        b = blocks;
    }
#endif

    if (!use_hw) {
        uint8_t prev[16];
        memcpy(prev, iv, 16);
        uint8_t block[16];
        for (size_t b_idx = 0; b_idx < blocks; b_idx++) {
            uint8_t cur[16];
            memcpy(cur, in + b_idx * 16, 16);
            zan_sm4_crypt_block(rk_dec, cur, block);
            for (int i = 0; i < 16; i++) {
                out[b_idx * 16 + i] = block[i] ^ prev[i];
            }
            memcpy(prev, cur, 16);
        }
    }

    uint8_t pad_val = out[len - 1];
    int bad = (pad_val == 0 || pad_val > 16);
    for (int i = 0; i < 16; i++) {
        int diff = (out[len - 1 - i] ^ pad_val);
        int in_pad = (i < pad_val);
        if (in_pad && diff != 0) bad = 1;
    }
    if (bad) return -1;
    return len - pad_val;
}

