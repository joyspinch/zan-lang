#include "rt_decimal.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define DECIMAL_SIGN_MASK  0x80000000U
#define DECIMAL_SCALE_MASK 0x00FF0000U
#define DECIMAL_SCALE_SHIFT 16
#define MAX_SCALE 28

/* 96 位上限：2^96 - 1 */
#if defined(__SIZEOF_INT128__)
typedef unsigned __int128 uint128_t;
static const uint128_t MANTISSA_96_MAX = (((uint128_t)1) << 96) - 1;

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
    uint128_t q = 0;
    uint128_t r = 0;
    for (int i = 127; i >= 0; i--) {
        r = (r << 1) | ((num >> i) & 1);
        if (r >= den) {
            r -= den;
            q |= ((uint128_t)1 << i);
        }
    }
    if (rem_out) *rem_out = r;
    return q;
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
    /* C# 的 decimal→整数显式转换是舍入到最近、半数到偶（银行家）——
     * 2.9m→3、2.5m→2。曾按逐位截断：ToInt64(2.9m)=2（TASKS.md A513）。
     * 一次取商余再判定，不逐位舍入（逐位会把 2.49 滚成 3）。 */
    if (scale == 0)
        return dec_is_neg(d) ? -(int64_t)m : (int64_t)m;
    uint128_t div = 1;
    for (uint8_t i = 0; i < scale; i++) div *= 10;
    uint128_t q = m / div, r = m % div;
    if (r * 2 > div || (r * 2 == div && (q % 2 == 1))) q += 1;
    return dec_is_neg(d) ? -(int64_t)q : (int64_t)q;
#else
    uint64_t res = d.lo64;
    return dec_is_neg(d) ? -(int64_t)res : (int64_t)res;
#endif
}

double zan_decimal_to_double(zan_decimal_t d) {
    double res = (double)d.hi32 * 18446744073709551616.0 + (double)d.lo64;
    uint8_t scale = dec_scale(d);
    while (scale > 0) {
        res /= 10.0;
        scale--;
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
    while (m > MANTISSA_96_MAX && scale > 0) {
        m /= 10;
        scale--;
    }
    uint32_t hi = (uint32_t)(m >> 64);
    uint64_t lo = (uint64_t)(m & 0xFFFFFFFFFFFFFFFFULL);
    return make_dec(hi, lo, neg, scale);
}

static void align_scale(uint128_t *ma, uint8_t *sa, uint128_t *mb, uint8_t *sb) {
    while (*sa < *sb && *ma <= (MANTISSA_96_MAX / 10)) {
        *ma *= 10;
        (*sa)++;
    }
    while (*sb < *sa && *mb <= (MANTISSA_96_MAX / 10)) {
        *mb *= 10;
        (*sb)++;
    }
    while (*sa < *sb) {
        *mb /= 10;
        (*sb)--;
    }
    while (*sb < *sa) {
        *ma /= 10;
        (*sa)--;
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
    align_scale(&ma, &sa, &mb, &sb);

    int cmp = 0;
    if (ma < mb) cmp = -1;
    else if (ma > mb) cmp = 1;

    return neg_a ? -cmp : cmp;
}

zan_decimal_t zan_decimal_add(zan_decimal_t a, zan_decimal_t b) {
    bool neg_a = dec_is_neg(a);
    bool neg_b = dec_is_neg(b);
    uint128_t ma = get_m128(a);
    uint128_t mb = get_m128(b);
    uint8_t sa = dec_scale(a);
    uint8_t sb = dec_scale(b);

    align_scale(&ma, &sa, &mb, &sb);

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
    while (scale > MAX_SCALE && scale > 0) {
        prod /= 10;
        scale--;
    }
    return from_m128(prod, neg, scale);
}

zan_decimal_t zan_decimal_div(zan_decimal_t a, zan_decimal_t b) {
    uint128_t mb = get_m128(b);
    if (mb == 0) return zan_decimal_zero(); /* 除零安全退化 */

    bool neg = dec_is_neg(a) ^ dec_is_neg(b);
    uint128_t ma = get_m128(a);
    int scale = (int)dec_scale(a) - (int)dec_scale(b);

    /* 精度目标提到 28 位 scale（.NET 的目标精度）——曾停在 18，
     * 1m/3m 只有 18 位（C# 28 位，TASKS.md A511①）。 */
    while (scale < MAX_SCALE && ma <= (MANTISSA_96_MAX / 10)) {
        ma *= 10;
        scale++;
    }
    while (scale < 0) {
        ma *= 10;
        scale++;
    }

    uint128_t quo = ma / mb;
    uint128_t rem = ma % mb;
    bool rounded = false;
    if (rem != 0 && rem * 2 >= mb && quo < MANTISSA_96_MAX) {
        /* 末位四舍五入（远离零）——精度已用尽，2m/3m 尾位 …6667 不是截断 …666 */
        quo++;
        rounded = true;
    }

    /* 精确商去尾零：10m/4m 应得 2.5（scale 1），不是 2.5000…。
     * 舍入出来的不精确商不去零——尾位是补出来的。 */
    if (!rounded) {
        while (scale > 0 && quo % 10 == 0) {
            quo /= 10;
            scale--;
        }
    }
    return from_m128(quo, neg, (uint8_t)scale);
}

zan_decimal_t zan_decimal_rem(zan_decimal_t a, zan_decimal_t b) {
    uint128_t mb = get_m128(b);
    if (mb == 0) return zan_decimal_zero();
    uint128_t ma = get_m128(a);
    uint8_t sa = dec_scale(a);
    uint8_t sb = dec_scale(b);
    align_scale(&ma, &sa, &mb, &sb);
    return from_m128(ma % mb, dec_is_neg(a), sa);
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
    if (db == 0.0) return zan_decimal_zero();
    return zan_decimal_from_double(zan_decimal_to_double(a) / db);
}
zan_decimal_t zan_decimal_rem(zan_decimal_t a, zan_decimal_t b) {
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
    uint128_t m = 0;
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
            if (m <= (MANTISSA_96_MAX / 10)) {
                m = m * 10 + (uint32_t)(c - '0');
                if (has_dot) scale++;
            }
        } else if (c == 'm' || c == 'M') {
            break;
        } else {
            break;
        }
    }

    if (!has_digit) return false;
    if (scale > MAX_SCALE) scale = MAX_SCALE;
    *out = from_m128(m, neg, scale);
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
        while (m > 0) {
            digits[dpos++] = (char)('0' + (int)(m % 10));
            m /= 10;
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
