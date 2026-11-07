/* rt_hw_asym.c: X25519 标量乘法、有限域算术与 RSA 大数模幂/CRT 实现 */

#if defined(__SIZEOF_INT128__) || (defined(__clang__) || defined(__GNUC__))
typedef unsigned __int128 zan_fe_u128;
#else
typedef struct { uint64_t lo, hi; } zan_fe_u128;
#endif

typedef uint64_t zan_fe[5];

static inline uint64_t zan_fe_load64_le(const uint8_t *s) {
    return ((uint64_t)s[0]) | (((uint64_t)s[1]) << 8) |
           (((uint64_t)s[2]) << 16) | (((uint64_t)s[3]) << 24) |
           (((uint64_t)s[4]) << 32) | (((uint64_t)s[5]) << 40) |
           (((uint64_t)s[6]) << 48) | (((uint64_t)s[7]) << 56);
}

static inline void zan_fe_frombytes(zan_fe h, const uint8_t *s) {
    uint64_t mask51 = 0x7ffffffffffffULL;
    h[0] = zan_fe_load64_le(s) & mask51;
    h[1] = (zan_fe_load64_le(s + 6) >> 3) & mask51;
    h[2] = (zan_fe_load64_le(s + 12) >> 6) & mask51;
    h[3] = (zan_fe_load64_le(s + 19) >> 1) & mask51;
    h[4] = (zan_fe_load64_le(s + 24) >> 12) & mask51;
}

static inline void zan_fe_carry(zan_fe h) {
    uint64_t mask51 = (1ULL << 51) - 1;
    for (int iter = 0; iter < 2; iter++) {
        uint64_t c0 = h[0] >> 51; h[0] &= mask51; h[1] += c0;
        uint64_t c1 = h[1] >> 51; h[1] &= mask51; h[2] += c1;
        uint64_t c2 = h[2] >> 51; h[2] &= mask51; h[3] += c2;
        uint64_t c3 = h[3] >> 51; h[3] &= mask51; h[4] += c3;
        uint64_t c4 = h[4] >> 51; h[4] &= mask51; h[0] += c4 * 19;
    }
}

static inline void zan_fe_tobytes(uint8_t *s, const zan_fe h) {
    zan_fe t;
    memcpy(t, h, sizeof(t));
    zan_fe_carry(t);
    uint64_t r[5];
    r[0] = t[0] + 19;
    uint64_t c = r[0] >> 51; r[0] &= (1ULL << 51) - 1;
    r[1] = t[1] + c; c = r[1] >> 51; r[1] &= (1ULL << 51) - 1;
    r[2] = t[2] + c; c = r[2] >> 51; r[2] &= (1ULL << 51) - 1;
    r[3] = t[3] + c; c = r[3] >> 51; r[3] &= (1ULL << 51) - 1;
    r[4] = t[4] + c;
    uint64_t mask = 0 - (r[4] >> 51);
    r[4] &= (1ULL << 51) - 1;
    for (int i = 0; i < 5; i++) {
        t[i] ^= mask & (t[i] ^ r[i]);
    }
    uint64_t w0 = t[0] | (t[1] << 51);
    uint64_t w1 = (t[1] >> 13) | (t[2] << 38);
    uint64_t w2 = (t[2] >> 26) | (t[3] << 25);
    uint64_t w3 = (t[3] >> 39) | (t[4] << 12);
    for (int i = 0; i < 8; i++) s[i] = (uint8_t)(w0 >> (i * 8));
    for (int i = 0; i < 8; i++) s[8 + i] = (uint8_t)(w1 >> (i * 8));
    for (int i = 0; i < 8; i++) s[16 + i] = (uint8_t)(w2 >> (i * 8));
    for (int i = 0; i < 8; i++) s[24 + i] = (uint8_t)(w3 >> (i * 8));
}

static inline void zan_fe_add(zan_fe r, const zan_fe a, const zan_fe b) {
    for (int i = 0; i < 5; i++) r[i] = a[i] + b[i];
    zan_fe_carry(r);
}

static inline void zan_fe_sub(zan_fe r, const zan_fe a, const zan_fe b) {
    uint64_t two_p0 = 2 * ((1ULL << 51) - 19);
    uint64_t two_p14 = 2 * ((1ULL << 51) - 1);
    r[0] = (a[0] + two_p0) - b[0];
    r[1] = (a[1] + two_p14) - b[1];
    r[2] = (a[2] + two_p14) - b[2];
    r[3] = (a[3] + two_p14) - b[3];
    r[4] = (a[4] + two_p14) - b[4];
    zan_fe_carry(r);
}

static inline void zan_fe_mul(zan_fe r, const zan_fe a, const zan_fe b) {
    uint64_t mask51 = (1ULL << 51) - 1;
    zan_fe_u128 r0 = (zan_fe_u128)a[0] * b[0] +
                    (zan_fe_u128)a[1] * ((zan_fe_u128)b[4] * 19) +
                    (zan_fe_u128)a[2] * ((zan_fe_u128)b[3] * 19) +
                    (zan_fe_u128)a[3] * ((zan_fe_u128)b[2] * 19) +
                    (zan_fe_u128)a[4] * ((zan_fe_u128)b[1] * 19);

    zan_fe_u128 r1 = (zan_fe_u128)a[0] * b[1] +
                    (zan_fe_u128)a[1] * b[0] +
                    (zan_fe_u128)a[2] * ((zan_fe_u128)b[4] * 19) +
                    (zan_fe_u128)a[3] * ((zan_fe_u128)b[3] * 19) +
                    (zan_fe_u128)a[4] * ((zan_fe_u128)b[2] * 19);

    zan_fe_u128 r2 = (zan_fe_u128)a[0] * b[2] +
                    (zan_fe_u128)a[1] * b[1] +
                    (zan_fe_u128)a[2] * b[0] +
                    (zan_fe_u128)a[3] * ((zan_fe_u128)b[4] * 19) +
                    (zan_fe_u128)a[4] * ((zan_fe_u128)b[3] * 19);

    zan_fe_u128 r3 = (zan_fe_u128)a[0] * b[3] +
                    (zan_fe_u128)a[1] * b[2] +
                    (zan_fe_u128)a[2] * b[1] +
                    (zan_fe_u128)a[3] * b[0] +
                    (zan_fe_u128)a[4] * ((zan_fe_u128)b[4] * 19);

    zan_fe_u128 r4 = (zan_fe_u128)a[0] * b[4] +
                    (zan_fe_u128)a[1] * b[3] +
                    (zan_fe_u128)a[2] * b[2] +
                    (zan_fe_u128)a[3] * b[1] +
                    (zan_fe_u128)a[4] * b[0];

    uint64_t c0 = (uint64_t)(r0 >> 51); r0 &= mask51; r1 += c0;
    uint64_t c1 = (uint64_t)(r1 >> 51); r1 &= mask51; r2 += c1;
    uint64_t c2 = (uint64_t)(r2 >> 51); r2 &= mask51; r3 += c2;
    uint64_t c3 = (uint64_t)(r3 >> 51); r3 &= mask51; r4 += c3;
    uint64_t c4 = (uint64_t)(r4 >> 51); r4 &= mask51;
    r0 += (zan_fe_u128)c4 * 19;
    c0 = (uint64_t)(r0 >> 51); r0 &= mask51; r1 += c0;

    r[0] = (uint64_t)r0;
    r[1] = (uint64_t)r1;
    r[2] = (uint64_t)r2;
    r[3] = (uint64_t)r3;
    r[4] = (uint64_t)r4;
    zan_fe_carry(r);
}

static inline void zan_fe_sqr(zan_fe r, const zan_fe a) {
    zan_fe_mul(r, a, a);
}

static inline void zan_fe_mul121665(zan_fe r, const zan_fe a) {
    uint64_t mask51 = (1ULL << 51) - 1;
    zan_fe_u128 r0 = (zan_fe_u128)a[0] * 121665;
    zan_fe_u128 r1 = (zan_fe_u128)a[1] * 121665;
    zan_fe_u128 r2 = (zan_fe_u128)a[2] * 121665;
    zan_fe_u128 r3 = (zan_fe_u128)a[3] * 121665;
    zan_fe_u128 r4 = (zan_fe_u128)a[4] * 121665;

    uint64_t c0 = (uint64_t)(r0 >> 51); r0 &= mask51; r1 += c0;
    uint64_t c1 = (uint64_t)(r1 >> 51); r1 &= mask51; r2 += c1;
    uint64_t c2 = (uint64_t)(r2 >> 51); r2 &= mask51; r3 += c2;
    uint64_t c3 = (uint64_t)(r3 >> 51); r3 &= mask51; r4 += c3;
    uint64_t c4 = (uint64_t)(r4 >> 51); r4 &= mask51;
    r0 += (zan_fe_u128)c4 * 19;
    c0 = (uint64_t)(r0 >> 51); r0 &= mask51; r1 += c0;

    r[0] = (uint64_t)r0;
    r[1] = (uint64_t)r1;
    r[2] = (uint64_t)r2;
    r[3] = (uint64_t)r3;
    r[4] = (uint64_t)r4;
    zan_fe_carry(r);
}

static inline void zan_fe_cswap(zan_fe a, zan_fe b, uint64_t swap) {
    uint64_t mask = 0 - swap;
    for (int i = 0; i < 5; i++) {
        uint64_t x = mask & (a[i] ^ b[i]);
        a[i] ^= x;
        b[i] ^= x;
    }
}

static void zan_fe_invert(zan_fe out, const zan_fe z) {
    zan_fe t0, t1, t2, t3;
    /* z^2 */
    zan_fe_sqr(t0, z);
    /* z^4 */
    zan_fe_sqr(t1, t0);
    /* z^8 */
    zan_fe_sqr(t1, t1);
    /* z^9 */
    zan_fe_mul(t1, t1, z);
    /* z^11 */
    zan_fe_mul(t0, t0, t1);
    /* z^22 */
    zan_fe_sqr(t2, t0);
    /* z^31 = z^(2^5 - 1) */
    zan_fe_mul(t1, t2, t1);
    /* z^(2^10 - 2^5) */
    zan_fe_sqr(t2, t1);
    for (int i = 1; i < 5; i++) zan_fe_sqr(t2, t2);
    /* z^(2^10 - 1) */
    zan_fe_mul(t1, t2, t1);
    /* z^(2^20 - 2^10) */
    zan_fe_sqr(t2, t1);
    for (int i = 1; i < 10; i++) zan_fe_sqr(t2, t2);
    /* z^(2^20 - 1) */
    zan_fe_mul(t2, t2, t1);
    /* z^(2^40 - 2^20) */
    zan_fe_sqr(t3, t2);
    for (int i = 1; i < 20; i++) zan_fe_sqr(t3, t3);
    /* z^(2^40 - 1) */
    zan_fe_mul(t2, t3, t2);
    /* z^(2^50 - 2^10) */
    zan_fe_sqr(t2, t2);
    for (int i = 1; i < 10; i++) zan_fe_sqr(t2, t2);
    /* z^(2^50 - 1) */
    zan_fe_mul(t1, t2, t1);
    /* z^(2^100 - 2^50) */
    zan_fe_sqr(t2, t1);
    for (int i = 1; i < 50; i++) zan_fe_sqr(t2, t2);
    /* z^(2^100 - 1) */
    zan_fe_mul(t2, t2, t1);
    /* z^(2^200 - 2^100) */
    zan_fe_sqr(t3, t2);
    for (int i = 1; i < 100; i++) zan_fe_sqr(t3, t3);
    /* z^(2^200 - 1) */
    zan_fe_mul(t2, t3, t2);
    /* z^(2^250 - 2^50) */
    zan_fe_sqr(t2, t2);
    for (int i = 1; i < 50; i++) zan_fe_sqr(t2, t2);
    /* z^(2^250 - 1) */
    zan_fe_mul(t1, t2, t1);
    /* z^(2^255 - 2^5) */
    zan_fe_sqr(t1, t1);
    for (int i = 1; i < 5; i++) zan_fe_sqr(t1, t1);
    /* z^(2^255 - 21) */
    zan_fe_mul(out, t1, t0);
}

int64_t zan_hw_x25519(const uint8_t *scalar, const uint8_t *point, uint8_t *out) {
    if (!scalar || !point || !out) return -1;

    uint8_t k[32];
    memcpy(k, scalar, 32);
    k[0] &= 248;
    k[31] &= 127;
    k[31] |= 64;

    zan_fe x1;
    zan_fe_frombytes(x1, point);

    zan_fe x2 = {1, 0, 0, 0, 0};
    zan_fe z2 = {0, 0, 0, 0, 0};
    zan_fe x3;
    memcpy(x3, x1, sizeof(zan_fe));
    zan_fe z3 = {1, 0, 0, 0, 0};

    uint64_t swap = 0;
    for (int t = 254; t >= 0; t--) {
        uint64_t kt = (k[t / 8] >> (t % 8)) & 1;
        swap ^= kt;
        zan_fe_cswap(x2, x3, swap);
        zan_fe_cswap(z2, z3, swap);
        swap = kt;

        zan_fe A, B, AA, BB, E, C, D, DA, CB;
        zan_fe_add(A, x2, z2);
        zan_fe_sqr(AA, A);
        zan_fe_sub(B, x2, z2);
        zan_fe_sqr(BB, B);
        zan_fe_sub(E, AA, BB);
        zan_fe_add(C, x3, z3);
        zan_fe_sub(D, x3, z3);
        zan_fe_mul(DA, D, A);
        zan_fe_mul(CB, C, B);

        zan_fe t0, t1;
        zan_fe_add(t0, DA, CB);
        zan_fe_sqr(x3, t0);
        zan_fe_sub(t1, DA, CB);
        zan_fe_sqr(t1, t1);
        zan_fe_mul(z3, x1, t1);

        zan_fe_mul(x2, AA, BB);
        zan_fe_mul121665(t0, E);
        zan_fe_add(t0, AA, t0);
        zan_fe_mul(z2, E, t0);
    }
    zan_fe_cswap(x2, x3, swap);
    zan_fe_cswap(z2, z3, swap);

    zan_fe z2_inv;
    zan_fe_invert(z2_inv, z2);
    zan_fe res;
    zan_fe_mul(res, x2, z2_inv);
    zan_fe_tobytes(out, res);
    return 0;
}

typedef unsigned __int128 zan_u128_t;

static void zan_load_be64_limbs(const uint8_t *src, int64_t slen, uint64_t *dst, int k) {
    for (int i = 0; i < k; i++) dst[i] = 0;
    for (int64_t i = 0; i < slen; i++) {
        int64_t rev = slen - 1 - i;
        int limb_idx = (int)(rev / 8);
        int byte_idx = (int)(rev % 8);
        if (limb_idx < k) {
            dst[limb_idx] |= ((uint64_t)src[i]) << (byte_idx * 8);
        }
    }
}

static void zan_store_be64_limbs(const uint64_t *src, int k, uint8_t *dst, int64_t dlen) {
    for (int64_t i = 0; i < dlen; i++) {
        int64_t rev = dlen - 1 - i;
        int limb_idx = (int)(rev / 8);
        int byte_idx = (int)(rev % 8);
        if (limb_idx < k) {
            dst[i] = (uint8_t)((src[limb_idx] >> (byte_idx * 8)) & 0xFF);
        } else {
            dst[i] = 0;
        }
    }
}

static void zan_mont_mul_core(const uint64_t *a, const uint64_t *b, const uint64_t *n,
                              uint64_t n0_inv, int k, uint64_t *res) {
    uint64_t t[132] = {0};
    for (int i = 0; i < k; i++) {
        uint64_t carry = 0;
        uint64_t bi = b[i];
        for (int j = 0; j < k; j++) {
            zan_u128_t cur = (zan_u128_t)t[j] + (zan_u128_t)a[j] * bi + carry;
            t[j] = (uint64_t)cur;
            carry = (uint64_t)(cur >> 64);
        }
        zan_u128_t cur2 = (zan_u128_t)t[k] + carry;
        t[k] = (uint64_t)cur2;
        t[k + 1] = (uint64_t)(cur2 >> 64);

        uint64_t m = t[0] * n0_inv;
        zan_u128_t c2 = (zan_u128_t)t[0] + (zan_u128_t)m * n[0];
        carry = (uint64_t)(c2 >> 64);
        for (int j = 1; j < k; j++) {
            zan_u128_t cur = (zan_u128_t)t[j] + (zan_u128_t)m * n[j] + carry;
            t[j - 1] = (uint64_t)cur;
            carry = (uint64_t)(cur >> 64);
        }
        zan_u128_t c3 = (zan_u128_t)t[k] + carry;
        t[k - 1] = (uint64_t)c3;
        t[k] = t[k + 1] + (uint64_t)(c3 >> 64);
        t[k + 1] = 0;
    }

    uint64_t borrow = 0;
    uint64_t sub[66];
    for (int i = 0; i < k; i++) {
        zan_u128_t ni_b = (zan_u128_t)n[i] + borrow;
        borrow = (t[i] < ni_b) ? 1 : 0;
        sub[i] = (uint64_t)(t[i] - ni_b);
    }
    if (t[k] > 0 || borrow == 0) {
        for (int i = 0; i < k; i++) res[i] = sub[i];
    } else {
        for (int i = 0; i < k; i++) res[i] = t[i];
    }
}

typedef struct {
    int k;
    uint64_t n[66];
    uint64_t n0_inv;
    uint64_t r_mod_n[66];
    uint64_t r2_mod_n[66];
} zan_mont_ctx_t;

static int zan_mont_ctx_init(zan_mont_ctx_t *ctx, const uint8_t *mod, int64_t mLen) {
    if (!ctx || !mod || mLen <= 0 || mLen > 512) return -1;
    if ((mod[mLen - 1] & 1) == 0) return -1;
    int k = (int)((mLen + 7) / 8);
    if (k <= 0 || k > 64) return -1;
    ctx->k = k;
    memset(ctx->n, 0, sizeof(ctx->n));
    memset(ctx->r_mod_n, 0, sizeof(ctx->r_mod_n));
    memset(ctx->r2_mod_n, 0, sizeof(ctx->r2_mod_n));
    zan_load_be64_limbs(mod, mLen, ctx->n, k);

    uint64_t inv = 1;
    uint64_t n0 = ctx->n[0];
    for (int i = 0; i < 6; i++) {
        inv = inv * (2 - n0 * inv);
    }
    ctx->n0_inv = (uint64_t)(-(int64_t)inv);

    uint64_t x[66] = {0};
    x[0] = 1;
    int total_doubles = 64 * k * 2;
    for (int d = 1; d <= total_doubles; d++) {
        uint64_t carry = 0;
        for (int i = 0; i < k; i++) {
            uint64_t next_carry = x[i] >> 63;
            x[i] = (x[i] << 1) | carry;
            carry = next_carry;
        }
        uint64_t borrow = 0;
        uint64_t sub[66];
        for (int i = 0; i < k; i++) {
            zan_u128_t ni_b = (zan_u128_t)ctx->n[i] + borrow;
            borrow = (x[i] < ni_b) ? 1 : 0;
            sub[i] = (uint64_t)(x[i] - ni_b);
        }
        if (carry > 0 || borrow == 0) {
            for (int i = 0; i < k; i++) x[i] = sub[i];
        }
        if (d == 64 * k) {
            for (int i = 0; i < k; i++) ctx->r_mod_n[i] = x[i];
        }
    }
    for (int i = 0; i < k; i++) ctx->r2_mod_n[i] = x[i];
    return 0;
}

static void zan_mont_exp_ctx(const zan_mont_ctx_t *ctx, const uint64_t *base_limbs,
                             const uint64_t *exp_limbs, int e_k, uint64_t *out_limbs) {
    int k = ctx->k;
    uint64_t table[16][66];
    for (int i = 0; i < k; i++) table[0][i] = ctx->r_mod_n[i];

    zan_mont_mul_core(base_limbs, ctx->r2_mod_n, ctx->n, ctx->n0_inv, k, table[1]);
    for (int w = 2; w < 16; w++) {
        zan_mont_mul_core(table[w - 1], table[1], ctx->n, ctx->n0_inv, k, table[w]);
    }

    int bitlen = 0;
    for (int i = e_k - 1; i >= 0; i--) {
        if (exp_limbs[i] != 0) {
            bitlen = i * 64 + (64 - __builtin_clzll(exp_limbs[i]));
            break;
        }
    }

    uint64_t result[66] = {0};
    int started = 0;
    int top_window = (bitlen + 3) / 4 - 1;
    for (int w_idx = top_window; w_idx >= 0; w_idx--) {
        int bit_pos = w_idx * 4;
        int limb_idx = bit_pos / 64;
        int bit_offset = bit_pos % 64;
        uint32_t val = (uint32_t)((exp_limbs[limb_idx] >> bit_offset) & 0xF);
        if (bit_offset > 60 && limb_idx + 1 < e_k) {
            val |= (uint32_t)((exp_limbs[limb_idx + 1] << (64 - bit_offset)) & 0xF);
        }

        if (started) {
            zan_mont_mul_core(result, result, ctx->n, ctx->n0_inv, k, result);
            zan_mont_mul_core(result, result, ctx->n, ctx->n0_inv, k, result);
            zan_mont_mul_core(result, result, ctx->n, ctx->n0_inv, k, result);
            zan_mont_mul_core(result, result, ctx->n, ctx->n0_inv, k, result);
            if (val > 0) {
                zan_mont_mul_core(result, table[val], ctx->n, ctx->n0_inv, k, result);
            }
        } else {
            if (val > 0) {
                for (int i = 0; i < k; i++) result[i] = table[val][i];
                started = 1;
            }
        }
    }

    if (!started) {
        for (int i = 0; i < k; i++) result[i] = ctx->r_mod_n[i];
    }

    uint64_t one[66] = {0};
    one[0] = 1;
    zan_mont_mul_core(result, one, ctx->n, ctx->n0_inv, k, out_limbs);
}

int64_t zan_hw_rsa_mod_pow(const uint8_t *base, int64_t bLen,
                           const uint8_t *exp, int64_t eLen,
                           const uint8_t *mod, int64_t mLen,
                           uint8_t *out) {
    if (!base || !exp || !mod || !out) return -1;
    zan_mont_ctx_t ctx;
    if (zan_mont_ctx_init(&ctx, mod, mLen) != 0) return -1;
    int k = ctx.k;

    uint64_t g[66] = {0};
    zan_load_be64_limbs(base, bLen, g, k);

    int e_k = (int)((eLen + 7) / 8);
    if (e_k > 64) e_k = 64;
    uint64_t e[66] = {0};
    zan_load_be64_limbs(exp, eLen, e, e_k);

    uint64_t result[66] = {0};
    zan_mont_exp_ctx(&ctx, g, e, e_k, result);

    zan_store_be64_limbs(result, k, out, mLen);
    return 0;
}

static int zan_limbs_cmp(const uint64_t *a, const uint64_t *b, int k) {
    for (int i = k - 1; i >= 0; i--) {
        if (a[i] > b[i]) return 1;
        if (a[i] < b[i]) return -1;
    }
    return 0;
}

static uint64_t zan_limbs_sub(uint64_t *res, const uint64_t *a, const uint64_t *b, int k) {
    uint64_t borrow = 0;
    for (int i = 0; i < k; i++) {
        zan_u128_t bi = (zan_u128_t)b[i] + borrow;
        borrow = (a[i] < bi) ? 1 : 0;
        res[i] = (uint64_t)(a[i] - bi);
    }
    return borrow;
}

static uint64_t zan_limbs_add(uint64_t *res, const uint64_t *a, const uint64_t *b, int k) {
    uint64_t carry = 0;
    for (int i = 0; i < k; i++) {
        zan_u128_t sum = (zan_u128_t)a[i] + b[i] + carry;
        res[i] = (uint64_t)sum;
        carry = (uint64_t)(sum >> 64);
    }
    return carry;
}

int64_t zan_hw_rsa_crt(const uint8_t *msg, int64_t mLen,
                       const uint8_t *p, int64_t pLen,
                       const uint8_t *q, int64_t qLen,
                       const uint8_t *dp, int64_t dpLen,
                       const uint8_t *dq, int64_t dqLen,
                       const uint8_t *qinv, int64_t qinvLen,
                       uint8_t *out, int64_t outLen) {
    if (!msg || !p || !q || !dp || !dq || !qinv || !out) return -1;
    if (mLen <= 0 || pLen <= 0 || qLen <= 0) return -1;
    if (pLen > 256 || qLen > 256) return -1;

    zan_mont_ctx_t ctx_p, ctx_q;
    if (zan_mont_ctx_init(&ctx_p, p, pLen) != 0) return -1;
    if (zan_mont_ctx_init(&ctx_q, q, qLen) != 0) return -1;

    int k = ctx_p.k;
    if (ctx_q.k != k) return -1;

    // Load 2k limbs of msg
    uint64_t m_limbs[66] = {0};
    zan_load_be64_limbs(msg, mLen, m_limbs, 2 * k);

    const uint64_t *m_low = m_limbs;
    const uint64_t *m_high = m_limbs + k;

    // m_p = (m_high * R + m_low) mod p
    uint64_t t1[66] = {0};
    zan_mont_mul_core(m_high, ctx_p.r2_mod_n, ctx_p.n, ctx_p.n0_inv, k, t1);
    uint64_t m_p[66] = {0};
    uint64_t carry_p = zan_limbs_add(m_p, t1, m_low, k);
    if (carry_p > 0) {
        zan_limbs_add(m_p, m_p, ctx_p.r_mod_n, k);
    }
    while (zan_limbs_cmp(m_p, ctx_p.n, k) >= 0) {
        zan_limbs_sub(m_p, m_p, ctx_p.n, k);
    }

    // m_q = (m_high * R + m_low) mod q
    zan_mont_mul_core(m_high, ctx_q.r2_mod_n, ctx_q.n, ctx_q.n0_inv, k, t1);
    uint64_t m_q[66] = {0};
    uint64_t carry_q = zan_limbs_add(m_q, t1, m_low, k);
    if (carry_q > 0) {
        zan_limbs_add(m_q, m_q, ctx_q.r_mod_n, k);
    }
    while (zan_limbs_cmp(m_q, ctx_q.n, k) >= 0) {
        zan_limbs_sub(m_q, m_q, ctx_q.n, k);
    }

    // s1 = m_p^dp mod p
    int dp_k = (int)((dpLen + 7) / 8);
    if (dp_k > 64) dp_k = 64;
    uint64_t dp_limbs[66] = {0};
    zan_load_be64_limbs(dp, dpLen, dp_limbs, dp_k);
    uint64_t s1[66] = {0};
    zan_mont_exp_ctx(&ctx_p, m_p, dp_limbs, dp_k, s1);

    // s2 = m_q^dq mod q
    int dq_k = (int)((dqLen + 7) / 8);
    if (dq_k > 64) dq_k = 64;
    uint64_t dq_limbs[66] = {0};
    zan_load_be64_limbs(dq, dqLen, dq_limbs, dq_k);
    uint64_t s2[66] = {0};
    zan_mont_exp_ctx(&ctx_q, m_q, dq_limbs, dq_k, s2);

    // s2_p = s2 mod p
    uint64_t s2_p[66];
    for (int i = 0; i < k; i++) s2_p[i] = s2[i];
    if (zan_limbs_cmp(s2_p, ctx_p.n, k) >= 0) {
        zan_limbs_sub(s2_p, s2_p, ctx_p.n, k);
    }

    // diff = (s1 - s2_p) mod p
    uint64_t diff[66];
    if (zan_limbs_cmp(s1, s2_p, k) >= 0) {
        zan_limbs_sub(diff, s1, s2_p, k);
    } else {
        zan_limbs_sub(diff, s2_p, s1, k);
        zan_limbs_sub(diff, ctx_p.n, diff, k);
    }

    // h = (diff * qinv) mod p
    uint64_t qinv_limbs[66] = {0};
    zan_load_be64_limbs(qinv, qinvLen, qinv_limbs, k);

    uint64_t tmp[66] = {0};
    zan_mont_mul_core(diff, qinv_limbs, ctx_p.n, ctx_p.n0_inv, k, tmp);
    uint64_t h[66] = {0};
    zan_mont_mul_core(tmp, ctx_p.r2_mod_n, ctx_p.n, ctx_p.n0_inv, k, h);

    // s = s2 + h * q
    uint64_t s[66] = {0};
    for (int i = 0; i < k; i++) {
        uint64_t hi = h[i];
        uint64_t carry = 0;
        for (int j = 0; j < k; j++) {
            zan_u128_t prod = (zan_u128_t)s[i + j] + (zan_u128_t)hi * ctx_q.n[j] + carry;
            s[i + j] = (uint64_t)prod;
            carry = (uint64_t)(prod >> 64);
        }
        s[i + k] += carry;
    }
    uint64_t carry = 0;
    for (int i = 0; i < k; i++) {
        zan_u128_t sum = (zan_u128_t)s[i] + s2[i] + carry;
        s[i] = (uint64_t)sum;
        carry = (uint64_t)(sum >> 64);
    }
    for (int i = k; carry > 0 && i < 2 * k; i++) {
        zan_u128_t sum = (zan_u128_t)s[i] + carry;
        s[i] = (uint64_t)sum;
        carry = (uint64_t)(sum >> 64);
    }

    zan_store_be64_limbs(s, 2 * k, out, outLen);
    return 0;
}

