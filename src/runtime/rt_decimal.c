#include "rt_decimal.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
/* 除零上报：原生走 SIGFPE（与整数除零同一条崩溃/异常通道）；wasm 没有信号
 * （wasi-libc 的 signal.h 直接 #error），用 unreachable 陷阱同样终止执行。 */
#if defined(__wasm__)
#define ZAN_DECIMAL_FPE() __builtin_trap()
#else
#include <signal.h>
#define ZAN_DECIMAL_FPE() raise(SIGFPE)
#endif

#define DECIMAL_SIGN_MASK  0x80000000U
#define DECIMAL_SCALE_MASK 0x00FF0000U
#define DECIMAL_SCALE_SHIFT 16
#define MAX_SCALE 28

/* 96 位上限：2^96 - 1 */
#if defined(__SIZEOF_INT128__)
typedef unsigned __int128 uint128_t;
static const uint128_t MANTISSA_96_MAX = (((uint128_t)1) << 96) - 1;
static const uint128_t MANTISSA_96_DIV10 = ((((uint128_t)1) << 96) - 1) / 10;

static const uint64_t POW10_U64[20] = {
    1ULL,
    10ULL,
    100ULL,
    1000ULL,
    10000ULL,
    100000ULL,
    1000000ULL,
    10000000ULL,
    100000000ULL,
    1000000000ULL,
    10000000000ULL,
    100000000000ULL,
    1000000000000ULL,
    10000000000000ULL,
    100000000000000ULL,
    1000000000000000ULL,
    10000000000000000ULL,
    100000000000000000ULL,
    1000000000000000000ULL,
    10000000000000000000ULL
};

static const double POW10_DBL[29] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,
    1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19,
    1e20, 1e21, 1e22, 1e23, 1e24, 1e25, 1e26, 1e27, 1e28
};

static inline uint128_t pow10_128(int n) {
    if (n <= 19) return (uint128_t)POW10_U64[n];
    return (uint128_t)POW10_U64[19] * (uint128_t)POW10_U64[n - 19];
}

static inline int nlz32(uint32_t x) {
    if (x == 0) return 32;
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_clz(x);
#else
    int n = 0;
    while (!(x & 0x80000000U)) { x <<= 1; n++; }
    return n;
#endif
}

/* 128 位除以 32 位快速路径（覆盖 /10、/10^9 与小除数），免去 Knuth 算法 D 归一化开销 */
static inline uint128_t u128_divrem_u32(uint128_t num, uint32_t d, uint32_t *rem_out) {
    if (num <= UINT64_MAX) {
        uint64_t n = (uint64_t)num;
        uint64_t q = n / d;
        if (rem_out) *rem_out = (uint32_t)(n - q * d);
        return (uint128_t)q;
    }
    uint64_t hi = (uint64_t)(num >> 64);
    uint64_t lo = (uint64_t)num;
    uint64_t q_hi = hi / d;
    uint64_t r = hi - q_hi * d;
    uint64_t mid = (r << 32) | (lo >> 32);
    uint64_t q_mid = mid / d;
    r = mid - q_mid * d;
    uint64_t low = (r << 32) | (lo & 0xFFFFFFFFULL);
    uint64_t q_lo = low / d;
    if (rem_out) *rem_out = (uint32_t)(low - q_lo * d);
    return ((uint128_t)q_hi << 64) | (q_mid << 32) | q_lo;
}

/* u128/u128 多精度除法：64 位硬件除法 + 32 位除数快路径 + Knuth 算法 D（TAOCP 4.3.1） */
static uint128_t u128_divrem(uint128_t num, uint128_t den, uint128_t *rem_out) {
    if (den == 0) {
        if (rem_out) *rem_out = 0;
        return 0;
    }
    if (num < den) {
        if (rem_out) *rem_out = num;
        return 0;
    }
    if (num == den) {
        if (rem_out) *rem_out = 0;
        return 1;
    }
    if (num <= UINT64_MAX) {
        uint64_t n = (uint64_t)num;
        uint64_t d = (uint64_t)den;
        uint64_t q = n / d;
        if (rem_out) *rem_out = (uint128_t)(n - q * d);
        return (uint128_t)q;
    }
    if (den <= 0xFFFFFFFFULL) {
        uint32_t r32 = 0;
        uint128_t q = u128_divrem_u32(num, (uint32_t)den, &r32);
        if (rem_out) *rem_out = (uint128_t)r32;
        return q;
    }

    const uint64_t B = 0x100000000ull; /* base 2^32 */
    uint64_t u[6] = {0}, v[4] = {0}, q[5] = {0};
    int vn = 4; /* 除数有效 digit 数 */
    while (vn > 1 && ((den >> ((vn - 1) * 32)) & 0xffffffffull) == 0) vn--;
    for (int i = 0; i < 4; i++)
        v[i] = (uint64_t)((den >> (i * 32)) & 0xffffffffull);

    /* D1：归一化，使除数最高 digit >= B/2。被除数固定补零到 5 个
     * digit（m+n）；num < 2^128 左移 <=31 位后 < 2^159，第 5 个
     * digit 承接全部移出位，第 6 个恒 0。 */
    int s = nlz32((uint32_t)v[vn - 1]);
    const int mn = 5;
    for (int i = 0; i < 4; i++)
        u[i] = (uint64_t)((num >> (i * 32)) & 0xffffffffull);
    if (s != 0) {
        uint64_t carry = 0;
        for (int i = 0; i < mn; i++) {
            uint64_t cur = u[i];
            u[i] = ((cur << s) | carry) & 0xffffffffull;
            carry = cur >> (32 - s);
        }
        uint64_t vc = 0;
        for (int i = 0; i < vn; i++) {
            uint64_t cur = v[i];
            v[i] = ((cur << s) | vc) & 0xffffffffull;
            vc = cur >> (32 - s);
        }
    }

    int m = mn - vn; /* 商 digit 下标上界（共 m+1 个） */
    for (int j = m; j >= 0; j--) {
        /* D3：商估 qhat */
        uint64_t num_hi = (u[j + vn] << 32) | u[j + vn - 1];
        uint64_t qhat = num_hi / v[vn - 1];
        uint64_t rhat = num_hi % v[vn - 1];
        while (qhat >= B ||
               (vn > 1 && qhat * v[vn - 2] > (rhat << 32) + u[j + vn - 2])) {
            qhat--;
            rhat += v[vn - 1];
            if (rhat >= B) break;
        }
        /* D4：乘减 */
        int64_t borrow = 0;
        for (int i = 0; i < vn; i++) {
            uint64_t p = qhat * v[i];
            int64_t t = (int64_t)u[i + j] - borrow - (int64_t)(p & 0xffffffffull);
            u[i + j] = (uint64_t)t & 0xffffffffull;
            borrow = (int64_t)(p >> 32) - (t >> 32);
        }
        int64_t t = (int64_t)u[j + vn] - borrow;
        u[j + vn] = (uint64_t)t & 0xffffffffull;
        if (t < 0) {
            /* D5/D6：商估过大，商减一、加回除数 */
            qhat--;
            uint64_t carry = 0;
            for (int i = 0; i < vn; i++) {
                uint64_t cur = u[i + j] + v[i] + carry;
                u[i + j] = cur & 0xffffffffull;
                carry = cur >> 32;
            }
            u[j + vn] = (u[j + vn] + carry) & 0xffffffffull;
        }
        q[j] = qhat;
    }

    /* 余数：u[0..vn-1] 右移 s 还原（归一化只补了零低位） */
    uint128_t rem = 0;
    for (int i = vn - 1; i >= 0; i--)
        rem = (rem << 32) | u[i];
    if (rem_out) *rem_out = rem >> s;

    uint128_t quo = 0;
    for (int i = m; i >= 0; i--)
        quo = (quo << 32) | q[i];
    return quo;
}

#if defined(_WIN32)
uint128_t __udivti3(uint128_t a, uint128_t b);
uint128_t __umodti3(uint128_t a, uint128_t b);

uint128_t __udivti3(uint128_t a, uint128_t b) {
    return u128_divrem(a, b, NULL);
}

uint128_t __umodti3(uint128_t a, uint128_t b) {
    uint128_t rem = 0;
    u128_divrem(a, b, &rem);
    return rem;
}
#endif
#endif

static inline bool dec_is_neg(zan_decimal_t d) {
    return (d.flags & DECIMAL_SIGN_MASK) != 0;
}

static inline uint8_t dec_scale(zan_decimal_t d) {
    return (uint8_t)((d.flags & DECIMAL_SCALE_MASK) >> DECIMAL_SCALE_SHIFT);
}

static inline zan_decimal_t make_dec(uint32_t hi, uint64_t lo, bool neg, uint8_t scale) {
    zan_decimal_t d;
    d.flags = ((uint32_t)scale << DECIMAL_SCALE_SHIFT) | (neg ? DECIMAL_SIGN_MASK : 0);
    d.hi32 = hi;
    d.lo64 = lo;
    return d;
}

zan_decimal_t zan_decimal_zero(void) {
    return make_dec(0, 0, false, 0);
}

zan_decimal_t zan_decimal_one(void) {
    return make_dec(0, 1, false, 0);
}

zan_decimal_t zan_decimal_from_int64(int64_t val) {
    if (val < 0) {
        uint64_t u = (uint64_t)(-(val + 1)) + 1;
        return make_dec(0, u, true, 0);
    }
    return make_dec(0, (uint64_t)val, false, 0);
}

zan_decimal_t zan_decimal_from_uint64(uint64_t val) {
    return make_dec(0, val, false, 0);
}

zan_decimal_t zan_decimal_from_parts(uint32_t lo, uint32_t mid, uint32_t hi, bool is_negative, uint8_t scale) {
    uint64_t lo64 = ((uint64_t)mid << 32) | (uint64_t)lo;
    if (scale > MAX_SCALE) scale = MAX_SCALE;
    return make_dec(hi, lo64, is_negative, scale);
}

zan_decimal_t zan_decimal_from_double(double val) {
    if (isnan(val) || isinf(val)) return zan_decimal_zero();
    bool neg = val < 0;
    if (neg) val = -val;

    /* 将浮点数转换为十进制表示，保留最多 28 位精度 */
    char buf[64];
    snprintf(buf, sizeof(buf), "%.16g", val);
    zan_decimal_t res;
    if (zan_decimal_parse(buf, strlen(buf), &res)) {
        if (neg) res = zan_decimal_neg(res);
        return res;
    }
    return zan_decimal_zero();
}

int64_t zan_decimal_to_int64(zan_decimal_t d) {
    uint8_t scale = dec_scale(d);
    if (scale > MAX_SCALE) scale = MAX_SCALE;
#if defined(__SIZEOF_INT128__)
    uint128_t m = ((uint128_t)d.hi32 << 64) | (uint128_t)d.lo64;
    if (scale == 0)
        return dec_is_neg(d) ? -(int64_t)m : (int64_t)m;
    uint128_t div = pow10_128(scale);
    uint128_t r = 0;
    uint128_t q = u128_divrem(m, div, &r);
    if (r * 2 > div || (r * 2 == div && ((uint64_t)q & 1ULL))) q += 1;
    return dec_is_neg(d) ? -(int64_t)q : (int64_t)q;
#else
    uint64_t res = d.lo64;
    return dec_is_neg(d) ? -(int64_t)res : (int64_t)res;
#endif
}

double zan_decimal_to_double(zan_decimal_t d) {
    double res = (double)d.hi32 * 18446744073709551616.0 + (double)d.lo64;
    uint8_t scale = dec_scale(d);
    if (scale > 0) {
        if (scale <= MAX_SCALE) {
            res /= POW10_DBL[scale];
        } else {
            while (scale > 0) { res /= 10.0; scale--; }
        }
    }
    return dec_is_neg(d) ? -res : res;
}

zan_decimal_t zan_decimal_neg(zan_decimal_t a) {
    a.flags ^= DECIMAL_SIGN_MASK;
    return a;
}

zan_decimal_t zan_decimal_abs(zan_decimal_t a) {
    a.flags &= ~DECIMAL_SIGN_MASK;
    return a;
}

#if defined(__SIZEOF_INT128__)

static inline uint128_t get_m128(zan_decimal_t d) {
    return ((uint128_t)d.hi32 << 64) | (uint128_t)d.lo64;
}

static inline zan_decimal_t from_m128(uint128_t m, bool neg, uint8_t scale) {
    if (m > MANTISSA_96_MAX && scale > 0) {
        while (m > MANTISSA_96_MAX && scale >= 9) {
            uint128_t q = u128_divrem_u32(m, 1000000000U, NULL);
            if (q <= MANTISSA_96_MAX) break;
            m = q;
            scale -= 9;
        }
        while (m > MANTISSA_96_MAX && scale > 0) {
            m = u128_divrem_u32(m, 10U, NULL);
            scale--;
        }
    }
    uint32_t hi = (uint32_t)(m >> 64);
    uint64_t lo = (uint64_t)(m & 0xFFFFFFFFFFFFFFFFULL);
    return make_dec(hi, lo, neg, scale);
}

static void align_scale(uint128_t *ma, uint8_t *sa, uint128_t *mb, uint8_t *sb) {
    if (*sa == *sb) return;
    if (*sa < *sb) {
        uint8_t diff = *sb - *sa;
        if (diff <= MAX_SCALE) {
            uint128_t p = pow10_128(diff);
            if (*ma <= u128_divrem(MANTISSA_96_MAX, p, NULL)) {
                *ma *= p;
                *sa = *sb;
                return;
            }
        }
        while (*sa < *sb && *ma <= MANTISSA_96_DIV10) {
            *ma *= 10;
            (*sa)++;
        }
        if (*sa < *sb) {
            uint8_t rem_d = *sb - *sa;
            if (rem_d <= MAX_SCALE) {
                *mb = u128_divrem(*mb, pow10_128(rem_d), NULL);
                *sb = *sa;
            } else {
                while (*sa < *sb) {
                    *mb = u128_divrem_u32(*mb, 10U, NULL);
                    (*sb)--;
                }
            }
        }
    } else {
        uint8_t diff = *sa - *sb;
        if (diff <= MAX_SCALE) {
            uint128_t p = pow10_128(diff);
            if (*mb <= u128_divrem(MANTISSA_96_MAX, p, NULL)) {
                *mb *= p;
                *sb = *sa;
                return;
            }
        }
        while (*sb < *sa && *mb <= MANTISSA_96_DIV10) {
            *mb *= 10;
            (*sb)++;
        }
        if (*sb < *sa) {
            uint8_t rem_d = *sa - *sb;
            if (rem_d <= MAX_SCALE) {
                *ma = u128_divrem(*ma, pow10_128(rem_d), NULL);
                *sa = *sb;
            } else {
                while (*sb < *sa) {
                    *ma = u128_divrem_u32(*ma, 10U, NULL);
                    (*sa)--;
                }
            }
        }
    }
}

int32_t zan_decimal_cmp(zan_decimal_t a, zan_decimal_t b) {
    bool neg_a = dec_is_neg(a);
    bool neg_b = dec_is_neg(b);
    uint128_t ma = get_m128(a);
    uint128_t mb = get_m128(b);

    if (ma == 0 && mb == 0) return 0;
    if (neg_a && !neg_b) return -1;
    if (!neg_a && neg_b) return 1;

    uint8_t sa = dec_scale(a);
    uint8_t sb = dec_scale(b);
    if (sa != sb) align_scale(&ma, &sa, &mb, &sb);

    int cmp = (ma > mb) - (ma < mb);
    return neg_a ? -cmp : cmp;
}

zan_decimal_t zan_decimal_add(zan_decimal_t a, zan_decimal_t b) {
    bool neg_a = dec_is_neg(a);
    bool neg_b = dec_is_neg(b);
    uint128_t ma = get_m128(a);
    uint128_t mb = get_m128(b);
    uint8_t sa = dec_scale(a);
    uint8_t sb = dec_scale(b);

    if (sa != sb) align_scale(&ma, &sa, &mb, &sb);

    if (neg_a == neg_b) {
        uint128_t sum = ma + mb;
        return from_m128(sum, neg_a, sa);
    } else {
        if (ma >= mb) {
            return from_m128(ma - mb, neg_a, sa);
        } else {
            return from_m128(mb - ma, neg_b, sa);
        }
    }
}

zan_decimal_t zan_decimal_sub(zan_decimal_t a, zan_decimal_t b) {
    return zan_decimal_add(a, zan_decimal_neg(b));
}

zan_decimal_t zan_decimal_mul(zan_decimal_t a, zan_decimal_t b) {
    bool neg = dec_is_neg(a) ^ dec_is_neg(b);
    uint128_t ma = get_m128(a);
    uint128_t mb = get_m128(b);
    uint8_t scale = dec_scale(a) + dec_scale(b);

    /* 简单的 128 位乘法 */
    uint128_t prod = ma * mb;
    if (scale > MAX_SCALE) {
        int drop = (int)scale - MAX_SCALE;
        if (drop <= MAX_SCALE) {
            prod = u128_divrem(prod, pow10_128(drop), NULL);
            scale = MAX_SCALE;
        } else {
            while (scale > MAX_SCALE) {
                prod = u128_divrem_u32(prod, 10U, NULL);
                scale--;
            }
        }
    }
    return from_m128(prod, neg, scale);
}

static inline void strip_trailing_zeros(uint128_t *quo, int *scale) {
    uint128_t q = *quo;
    int s = *scale;
    if (q == 0) { *scale = 0; return; }
    while (s >= 9 && q > UINT64_MAX) {
        uint32_t rem = 0;
        uint128_t next = u128_divrem_u32(q, 1000000000U, &rem);
        if (rem != 0) break;
        q = next;
        s -= 9;
    }
    while (s > 0 && q > UINT64_MAX) {
        uint32_t rem = 0;
        uint128_t next = u128_divrem_u32(q, 10U, &rem);
        if (rem != 0) break;
        q = next;
        s--;
    }
    if (q <= UINT64_MAX) {
        uint64_t q64 = (uint64_t)q;
        while (s > 0 && (q64 % 10ULL) == 0ULL) {
            q64 /= 10ULL;
            s--;
        }
        q = (uint128_t)q64;
    }
    *quo = q;
    *scale = s;
}

zan_decimal_t zan_decimal_div(zan_decimal_t a, zan_decimal_t b) {
    uint128_t mb = get_m128(b);
    if (mb == 0) {
        ZAN_DECIMAL_FPE();
        return zan_decimal_zero();
    }

    bool neg = dec_is_neg(a) ^ dec_is_neg(b);
    uint128_t ma = get_m128(a);
    if (ma == 0) return make_dec(0, 0, neg, 0);

    int scale = (int)dec_scale(a) - (int)dec_scale(b);

    /* 快路径：除数尾数为 1（如 / 1m、/ 100m）：免去先扩 28 位再退 28 位 */
    if (mb == 1) {
        if (scale < 0) {
            int grow = -scale;
            if (grow <= MAX_SCALE) {
                ma *= pow10_128(grow);
                scale = 0;
            } else {
                while (scale < 0) { ma *= 10; scale++; }
            }
        } else if (scale > 0) {
            strip_trailing_zeros(&ma, &scale);
        }
        return from_m128(ma, neg, (uint8_t)scale);
    }

    /* 精度目标提到 28 位 scale（.NET 的目标精度）：分步快乘与原逐位循环停止点严格一致 */
    static const uint128_t MANTISSA_96_DIV1E9 = ((((uint128_t)1) << 96) - 1) / 1000000000ULL;
    static const uint128_t MANTISSA_96_DIV1E4 = ((((uint128_t)1) << 96) - 1) / 10000ULL;
    while (scale + 9 <= MAX_SCALE && ma <= MANTISSA_96_DIV1E9) {
        ma *= 1000000000ULL;
        scale += 9;
    }
    while (scale + 4 <= MAX_SCALE && ma <= MANTISSA_96_DIV1E4) {
        ma *= 10000ULL;
        scale += 4;
    }
    while (scale < MAX_SCALE && ma <= MANTISSA_96_DIV10) {
        ma *= 10;
        scale++;
    }
    while (scale < 0) {
        ma *= 10;
        scale++;
    }

    uint128_t rem = 0;
    uint128_t quo = u128_divrem(ma, mb, &rem);
    bool rounded = false;
    if (rem != 0 && rem * 2 >= mb && quo < MANTISSA_96_MAX) {
        /* 末位四舍五入（远离零）——精度已用尽，2m/3m 尾位 …6667 不是截断 …666 */
        quo++;
        rounded = true;
    }

    /* 精确商去尾零：10m/4m 应得 2.5（scale 1），不是 2.5000…。
     * 舍入出来的不精确商不去零——尾位是补出来的。 */
    if (!rounded && scale > 0) {
        strip_trailing_zeros(&quo, &scale);
    }
    return from_m128(quo, neg, (uint8_t)scale);
}

zan_decimal_t zan_decimal_rem(zan_decimal_t a, zan_decimal_t b) {
    uint128_t mb = get_m128(b);
    if (mb == 0) {
        ZAN_DECIMAL_FPE();
        return zan_decimal_zero();
    }
    uint128_t ma = get_m128(a);
    uint8_t sa = dec_scale(a);
    uint8_t sb = dec_scale(b);
    if (sa != sb) align_scale(&ma, &sa, &mb, &sb);
    uint128_t rem = 0;
    u128_divrem(ma, mb, &rem);
    return from_m128(rem, dec_is_neg(a), sa);
}

#else

/* 非 128 位编译器兜底 */
int32_t zan_decimal_cmp(zan_decimal_t a, zan_decimal_t b) {
    double da = zan_decimal_to_double(a);
    double db = zan_decimal_to_double(b);
    return (da < db) ? -1 : (da > db) ? 1 : 0;
}
zan_decimal_t zan_decimal_add(zan_decimal_t a, zan_decimal_t b) {
    return zan_decimal_from_double(zan_decimal_to_double(a) + zan_decimal_to_double(b));
}
zan_decimal_t zan_decimal_sub(zan_decimal_t a, zan_decimal_t b) {
    return zan_decimal_from_double(zan_decimal_to_double(a) - zan_decimal_to_double(b));
}
zan_decimal_t zan_decimal_mul(zan_decimal_t a, zan_decimal_t b) {
    return zan_decimal_from_double(zan_decimal_to_double(a) * zan_decimal_to_double(b));
}
zan_decimal_t zan_decimal_div(zan_decimal_t a, zan_decimal_t b) {
    double db = zan_decimal_to_double(b);
    if (db == 0.0) {
        ZAN_DECIMAL_FPE();
        return zan_decimal_zero();
    }
    return zan_decimal_from_double(zan_decimal_to_double(a) / db);
}
zan_decimal_t zan_decimal_rem(zan_decimal_t a, zan_decimal_t b) {
    ZAN_DECIMAL_FPE();
    return zan_decimal_zero();
}
#endif

bool zan_decimal_eq(zan_decimal_t a, zan_decimal_t b) {
    return zan_decimal_cmp(a, b) == 0;
}

bool zan_decimal_lt(zan_decimal_t a, zan_decimal_t b) {
    return zan_decimal_cmp(a, b) < 0;
}

bool zan_decimal_lte(zan_decimal_t a, zan_decimal_t b) {
    return zan_decimal_cmp(a, b) <= 0;
}

bool zan_decimal_gt(zan_decimal_t a, zan_decimal_t b) {
    return zan_decimal_cmp(a, b) > 0;
}

bool zan_decimal_gte(zan_decimal_t a, zan_decimal_t b) {
    return zan_decimal_cmp(a, b) >= 0;
}

bool zan_decimal_parse(const char *str, size_t len, zan_decimal_t *out) {
    if (!str || len == 0 || !out) return false;
    size_t i = 0;
    while (i < len && isspace((unsigned char)str[i])) i++;
    if (i >= len) return false;

    bool neg = false;
    if (str[i] == '-') { neg = true; i++; }
    else if (str[i] == '+') { i++; }

#if defined(__SIZEOF_INT128__)
    uint64_t m64 = 0;
    uint128_t m = 0;
    bool wide = false;
    uint8_t scale = 0;
    bool has_dot = false;
    bool has_digit = false;

    for (; i < len; i++) {
        char c = str[i];
        if (c == '.') {
            if (has_dot) break;
            has_dot = true;
        } else if (c >= '0' && c <= '9') {
            has_digit = true;
            uint32_t d = (uint32_t)(c - '0');
            if (!wide) {
                if (m64 <= 1844674407370955160ULL) {
                    m64 = m64 * 10ULL + d;
                    if (has_dot) scale++;
                } else {
                    wide = true;
                    m = (uint128_t)m64 * 10 + d;
                    if (has_dot) scale++;
                }
            } else if (m <= MANTISSA_96_DIV10) {
                m = m * 10 + d;
                if (has_dot) scale++;
            }
        } else {
            break;
        }
    }

    if (!has_digit) return false;
    if (scale > MAX_SCALE) scale = MAX_SCALE;
    if (!wide) {
        *out = make_dec(0, m64, neg, scale);
    } else {
        *out = from_m128(m, neg, scale);
    }
    return true;
#else
    double d = strtod(str, NULL);
    *out = zan_decimal_from_double(d);
    return true;
#endif
}

int zan_decimal_to_string(zan_decimal_t d, char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return 0;
    uint8_t scale = dec_scale(d);
    bool neg = dec_is_neg(d);

#if defined(__SIZEOF_INT128__)
    uint128_t m = get_m128(d);
    char digits[64];
    int dpos = 0;
    if (m == 0) {
        digits[dpos++] = '0';
    } else {
        while (m > UINT64_MAX) {
            uint32_t chunk = 0;
            m = u128_divrem_u32(m, 1000000000U, &chunk);
            for (int k = 0; k < 9; k++) {
                uint32_t q = chunk / 10U;
                digits[dpos++] = (char)('0' + (int)(chunk - q * 10U));
                chunk = q;
            }
        }
        uint64_t m64 = (uint64_t)m;
        while (m64 > 0) {
            uint64_t q = m64 / 10ULL;
            digits[dpos++] = (char)('0' + (int)(m64 - q * 10ULL));
            m64 = q;
        }
    }

    size_t out_pos = 0;
    if (neg && (out_pos + 1 < buf_size)) {
        buf[out_pos++] = '-';
    }

    int total_digits = dpos;
    if (scale >= total_digits) {
        if (out_pos + 2 < buf_size) {
            buf[out_pos++] = '0';
            buf[out_pos++] = '.';
        }
        for (int z = 0; z < scale - total_digits && out_pos + 1 < buf_size; z++) {
            buf[out_pos++] = '0';
        }
        for (int k = total_digits - 1; k >= 0 && out_pos + 1 < buf_size; k--) {
            buf[out_pos++] = digits[k];
        }
    } else {
        int dot_after = total_digits - scale;
        for (int k = total_digits - 1; k >= 0 && out_pos + 1 < buf_size; k--) {
            buf[out_pos++] = digits[k];
            if (scale > 0 && (total_digits - k) == dot_after && out_pos + 1 < buf_size) {
                buf[out_pos++] = '.';
            }
        }
    }
    buf[out_pos] = '\0';
    return (int)out_pos;
#else
    return snprintf(buf, buf_size, "%.16g", zan_decimal_to_double(d));
#endif
}

/* 导出运行时外部符号实现 */
void __zan_decimal_add(const zan_decimal_t *a, const zan_decimal_t *b, zan_decimal_t *out) {
    if (out) *out = zan_decimal_add(*a, *b);
}

void __zan_decimal_sub(const zan_decimal_t *a, const zan_decimal_t *b, zan_decimal_t *out) {
    if (out) *out = zan_decimal_sub(*a, *b);
}

void __zan_decimal_mul(const zan_decimal_t *a, const zan_decimal_t *b, zan_decimal_t *out) {
    if (out) *out = zan_decimal_mul(*a, *b);
}

void __zan_decimal_div(const zan_decimal_t *a, const zan_decimal_t *b, zan_decimal_t *out) {
    if (out) *out = zan_decimal_div(*a, *b);
}

void __zan_decimal_rem(const zan_decimal_t *a, const zan_decimal_t *b, zan_decimal_t *out) {
    if (out) *out = zan_decimal_rem(*a, *b);
}

void __zan_decimal_neg(const zan_decimal_t *a, zan_decimal_t *out) {
    if (out) *out = zan_decimal_neg(*a);
}

int32_t __zan_decimal_cmp(const zan_decimal_t *a, const zan_decimal_t *b) {
    return zan_decimal_cmp(*a, *b);
}

void __zan_decimal_from_int64(int64_t val, zan_decimal_t *out) {
    if (out) *out = zan_decimal_from_int64(val);
}

void __zan_decimal_from_uint64(uint64_t val, zan_decimal_t *out) {
    if (out) *out = zan_decimal_from_uint64(val);
}

void __zan_decimal_from_double(double val, zan_decimal_t *out) {
    if (out) *out = zan_decimal_from_double(val);
}

int64_t __zan_decimal_to_int64(const zan_decimal_t *a) {
    return a ? zan_decimal_to_int64(*a) : 0;
}

double __zan_decimal_to_double(const zan_decimal_t *a) {
    return a ? zan_decimal_to_double(*a) : 0.0;
}

/* The two entry points below take their length/size as int64_t, NOT size_t:
   the compiler backend emits the matching call with Zan's `long` (always
   64-bit), while size_t is 32-bit on wasm32 -- a mismatch wasm-ld reports as
   "function signature mismatch" and that would break the call at run time.
   Every other __zan_decimal_* entry point is fixed-width for the same reason. */
int32_t __zan_decimal_to_string(const zan_decimal_t *a, char *buf, int64_t buf_size) {
    return a ? zan_decimal_to_string(*a, buf, (size_t)buf_size) : 0;
}

bool __zan_decimal_parse(const char *str, int64_t len, zan_decimal_t *out) {
    return zan_decimal_parse(str, (size_t)len, out);
}
