#ifndef ZAN_RT_DECIMAL_H
#define ZAN_RT_DECIMAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 128 位定点数（对齐 .NET System.Decimal 标准布局）：
 * 16 字节，自然对齐。
 * flags:
 *   bit 31: 符号位 (0=正, 1=负, 掩码 0x80000000)
 *   bits 16-23: scale (0..28, 掩码 0x00FF0000)
 *   其余 bits 为 0
 * hi32: 96 位尾数的高 32 位
 * lo64: 96 位尾数的低 64 位 (low 32 + mid 32)
 */
typedef struct {
    uint32_t flags;
    uint32_t hi32;
    uint64_t lo64;
} zan_decimal_t;

/* 构造与常量 */
zan_decimal_t zan_decimal_zero(void);
zan_decimal_t zan_decimal_one(void);
zan_decimal_t zan_decimal_from_int64(int64_t val);
zan_decimal_t zan_decimal_from_uint64(uint64_t val);
zan_decimal_t zan_decimal_from_double(double val);
zan_decimal_t zan_decimal_from_parts(uint32_t lo, uint32_t mid, uint32_t hi, bool is_negative, uint8_t scale);

/* 提取与转换 */
int64_t       zan_decimal_to_int64(zan_decimal_t d);
double        zan_decimal_to_double(zan_decimal_t d);
int           zan_decimal_to_string(zan_decimal_t d, char *buf, size_t buf_size);
bool          zan_decimal_parse(const char *str, size_t len, zan_decimal_t *out);

/* 算术操作 */
zan_decimal_t zan_decimal_add(zan_decimal_t a, zan_decimal_t b);
zan_decimal_t zan_decimal_sub(zan_decimal_t a, zan_decimal_t b);
zan_decimal_t zan_decimal_mul(zan_decimal_t a, zan_decimal_t b);
zan_decimal_t zan_decimal_div(zan_decimal_t a, zan_decimal_t b);
zan_decimal_t zan_decimal_rem(zan_decimal_t a, zan_decimal_t b);
zan_decimal_t zan_decimal_neg(zan_decimal_t a);
zan_decimal_t zan_decimal_abs(zan_decimal_t a);

/* 比较：a < b 返回 -1, a == b 返回 0, a > b 返回 1 */
int32_t       zan_decimal_cmp(zan_decimal_t a, zan_decimal_t b);
bool          zan_decimal_eq(zan_decimal_t a, zan_decimal_t b);
bool          zan_decimal_lt(zan_decimal_t a, zan_decimal_t b);
bool          zan_decimal_lte(zan_decimal_t a, zan_decimal_t b);
bool          zan_decimal_gt(zan_decimal_t a, zan_decimal_t b);
bool          zan_decimal_gte(zan_decimal_t a, zan_decimal_t b);

/* 导出给编译器后端生成的运行时外部符号（指针形参规避全平台 16 字节结构体传参 ABI 歧义）。
   长度/容量一律用 int64_t：编译器后端按 Zan 的 long（恒 64 位）发出调用，而 size_t
   在 wasm32 上是 32 位，会造成签名不匹配（wasm-ld: function signature mismatch）。 */
void          __zan_decimal_add(const zan_decimal_t *a, const zan_decimal_t *b, zan_decimal_t *out);
void          __zan_decimal_sub(const zan_decimal_t *a, const zan_decimal_t *b, zan_decimal_t *out);
void          __zan_decimal_mul(const zan_decimal_t *a, const zan_decimal_t *b, zan_decimal_t *out);
void          __zan_decimal_div(const zan_decimal_t *a, const zan_decimal_t *b, zan_decimal_t *out);
void          __zan_decimal_rem(const zan_decimal_t *a, const zan_decimal_t *b, zan_decimal_t *out);
void          __zan_decimal_neg(const zan_decimal_t *a, zan_decimal_t *out);
int32_t       __zan_decimal_cmp(const zan_decimal_t *a, const zan_decimal_t *b);
void          __zan_decimal_from_int64(int64_t val, zan_decimal_t *out);
void          __zan_decimal_from_uint64(uint64_t val, zan_decimal_t *out);
void          __zan_decimal_from_double(double val, zan_decimal_t *out);
int64_t       __zan_decimal_to_int64(const zan_decimal_t *a);
double        __zan_decimal_to_double(const zan_decimal_t *a);
int32_t       __zan_decimal_to_string(const zan_decimal_t *a, char *buf, int64_t buf_size);
bool          __zan_decimal_parse(const char *str, int64_t len, zan_decimal_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_RT_DECIMAL_H */
