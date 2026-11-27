/* rt_hw_b64.c: Base64 快速查表与编解码向量加速实现 */
#include "rt_hw_internal.h"
#include "../../common/zan_abi.h"

/* 7 */

static const char ZAN_B64_ENC_TABLE[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static uint8_t ZAN_B64_DEC_TABLE[256];
static int zan_b64_dec_inited = 0;

static void zan_b64_init_dec_table(void) {
  if (zan_b64_dec_inited)
    return;
  memset(ZAN_B64_DEC_TABLE, 0x80, 256);
  for (int i = 0; i < 64; i++) {
    ZAN_B64_DEC_TABLE[(uint8_t)ZAN_B64_ENC_TABLE[i]] = (uint8_t)i;
  }
  ZAN_B64_DEC_TABLE['='] = 0;
  /* URL-safe RFC 4648 §5: '-' -> 62, '_' -> 63 */
  ZAN_B64_DEC_TABLE[(uint8_t)'-'] = 62;
  ZAN_B64_DEC_TABLE[(uint8_t)'_'] = 63;
  zan_b64_dec_inited = 1;
}

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
     defined(_M_IX86)) &&                                                      \
    (defined(__GNUC__) || defined(__clang__))

/* Muła-Lemire AVX2 (256-bit) Base64 编码：每轮 24B -> 32B，双 128-bit 装载零越界。 */
__attribute__((target("avx2"))) static void
zan_b64_encode_avx2(const uint8_t **sp, char **dp, int64_t *remp) {
  const uint8_t *s = *sp;
  char *d = *dp;
  int64_t rem = *remp;

  const __m256i shuf = _mm256_setr_epi8(
      1, 0, 2, 1, 4, 3, 5, 4, 7, 6, 8, 7, 10, 9, 11, 10,
      1, 0, 2, 1, 4, 3, 5, 4, 7, 6, 8, 7, 10, 9, 11, 10);
  const __m256i mask0 = _mm256_set1_epi32(0x0FC0FC00);
  const __m256i mul0 = _mm256_set1_epi32(0x04000040);
  const __m256i mask2 = _mm256_set1_epi32(0x003F03F0);
  const __m256i mul2 = _mm256_set1_epi32(0x01000010);
  const __m256i k51 = _mm256_set1_epi8(51);
  const __m256i k26 = _mm256_set1_epi8(26);
  const __m256i k13 = _mm256_set1_epi8(13);
  const __m256i lut = _mm256_setr_epi8(
      71, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -19, -16, 65, 0, 0,
      71, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -19, -16, 65, 0, 0);

  while (rem >= 28) {
    __m128i lo = _mm_loadu_si128((const __m128i *)s);
    __m128i hi = _mm_loadu_si128((const __m128i *)(s + 12));
    __m256i in = _mm256_inserti128_si256(_mm256_castsi128_si256(lo), hi, 1);
    in = _mm256_shuffle_epi8(in, shuf);
    __m256i t1 = _mm256_mulhi_epu16(_mm256_and_si256(in, mask0), mul0);
    __m256i t3 = _mm256_mullo_epi16(_mm256_and_si256(in, mask2), mul2);
    __m256i idx = _mm256_or_si256(t1, t3);

    __m256i sub = _mm256_subs_epu8(idx, k51);
    __m256i less = _mm256_cmpgt_epi8(k26, idx);
    __m256i lut_i = _mm256_or_si256(sub, _mm256_and_si256(less, k13));
    __m256i out = _mm256_add_epi8(idx, _mm256_shuffle_epi8(lut, lut_i));
    _mm256_storeu_si256((__m256i *)d, out);

    s += 24;
    d += 32;
    rem -= 24;
  }

  *sp = s;
  *dp = d;
  *remp = rem;
}

/* SSSE3 (128-bit) Base64 编码：每轮 12B -> 16B。 */
__attribute__((target("ssse3"))) static void
zan_b64_encode_ssse3(const uint8_t **sp, char **dp, int64_t *remp) {
  const uint8_t *s = *sp;
  char *d = *dp;
  int64_t rem = *remp;

  const __m128i shuf = _mm_setr_epi8(1, 0, 2, 1, 4, 3, 5, 4, 7, 6, 8, 7, 10, 9,
                                     11, 10);
  const __m128i mask0 = _mm_set1_epi32(0x0FC0FC00);
  const __m128i mul0 = _mm_set1_epi32(0x04000040);
  const __m128i mask2 = _mm_set1_epi32(0x003F03F0);
  const __m128i mul2 = _mm_set1_epi32(0x01000010);
  const __m128i k51 = _mm_set1_epi8(51);
  const __m128i k26 = _mm_set1_epi8(26);
  const __m128i k13 = _mm_set1_epi8(13);
  const __m128i lut = _mm_setr_epi8(71, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4,
                                    -19, -16, 65, 0, 0);

  while (rem >= 16) {
    __m128i in = _mm_loadu_si128((const __m128i *)s);
    in = _mm_shuffle_epi8(in, shuf);
    __m128i t1 = _mm_mulhi_epu16(_mm_and_si128(in, mask0), mul0);
    __m128i t3 = _mm_mullo_epi16(_mm_and_si128(in, mask2), mul2);
    __m128i idx = _mm_or_si128(t1, t3);

    __m128i sub = _mm_subs_epu8(idx, k51);
    __m128i less = _mm_cmpgt_epi8(k26, idx);
    __m128i lut_i = _mm_or_si128(sub, _mm_and_si128(less, k13));
    __m128i out = _mm_add_epi8(idx, _mm_shuffle_epi8(lut, lut_i));
    _mm_storeu_si128((__m128i *)d, out);

    s += 12;
    d += 16;
    rem -= 12;
  }

  *sp = s;
  *dp = d;
  *remp = rem;
}

/* Muła-Lemire AVX2 (256-bit) Base64 解码：每轮 32B -> 精确 24B（零越界写），
 * 遇 URL-safe(-/_)、中间 '=' 或非法字符立即回落标量路径保持 100% 兼容语义。 */
__attribute__((target("avx2"))) static void
zan_b64_decode_avx2(const uint8_t **sp, uint8_t **dp, int64_t *remp) {
  const uint8_t *s = *sp;
  uint8_t *d = *dp;
  int64_t rem = *remp;

  const __m256i mask0f = _mm256_set1_epi8(0x0F);
  const __m256i hi_lut = _mm256_setr_epi8(
      0, 0, 3, 4, 8, 16, 8, 16, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 3, 4, 8, 16, 8, 16, 0, 0, 0, 0, 0, 0, 0, 0);
  const __m256i lo_lut = _mm256_setr_epi8(
      20, 28, 28, 28, 28, 28, 28, 28, 28, 28, 24, 10, 8, 8, 8, 9,
      20, 28, 28, 28, 28, 28, 28, 28, 28, 28, 24, 10, 8, 8, 8, 9);
  const __m256i off_lut = _mm256_setr_epi8(
      0, 0, 19, 4, -65, -65, -71, -71, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 19, 4, -65, -65, -71, -71, 0, 0, 0, 0, 0, 0, 0, 0);
  const __m256i slash = _mm256_set1_epi8(0x2F);
  const __m256i k3 = _mm256_set1_epi8(3);
  const __m256i zero = _mm256_setzero_si256();
  const __m256i mul_ub = _mm256_set1_epi32(0x01400140);
  const __m256i mul_16 = _mm256_set1_epi32(0x00011000);
  const __m256i pack = _mm256_setr_epi8(
      2, 1, 0, 6, 5, 4, 10, 9, 8, 14, 13, 12, -1, -1, -1, -1,
      2, 1, 0, 6, 5, 4, 10, 9, 8, 14, 13, 12, -1, -1, -1, -1);

  while (rem >= 32) {
    __m256i in = _mm256_loadu_si256((const __m256i *)s);
    __m256i lo_nib = _mm256_and_si256(in, mask0f);
    __m256i hi_nib = _mm256_and_si256(_mm256_srli_epi32(in, 4), mask0f);
    __m256i valid = _mm256_and_si256(_mm256_shuffle_epi8(lo_lut, lo_nib),
                                     _mm256_shuffle_epi8(hi_lut, hi_nib));
    if (!_mm256_testz_si256(_mm256_cmpeq_epi8(valid, zero),
                            _mm256_set1_epi8(-1))) {
      break;
    }
    __m256i off = _mm256_shuffle_epi8(off_lut, hi_nib);
    __m256i is_slash = _mm256_cmpeq_epi8(in, slash);
    off = _mm256_sub_epi8(off, _mm256_and_si256(is_slash, k3));
    __m256i vals = _mm256_add_epi8(in, off);

    __m256i merged = _mm256_maddubs_epi16(vals, mul_ub);
    __m256i packed = _mm256_madd_epi16(merged, mul_16);
    __m256i res = _mm256_shuffle_epi8(packed, pack);

    __m128i lo12 = _mm256_castsi256_si128(res);
    __m128i hi12 = _mm256_extracti128_si256(res, 1);
    uint64_t w0 = (uint64_t)_mm_cvtsi128_si64(lo12);
    uint32_t w1 = (uint32_t)_mm_extract_epi32(lo12, 2);
    uint64_t w2 = (uint64_t)_mm_cvtsi128_si64(hi12);
    uint32_t w3 = (uint32_t)_mm_extract_epi32(hi12, 2);
    memcpy(d, &w0, 8);
    memcpy(d + 8, &w1, 4);
    memcpy(d + 12, &w2, 8);
    memcpy(d + 20, &w3, 4);

    s += 32;
    d += 24;
    rem -= 32;
  }

  *sp = s;
  *dp = d;
  *remp = rem;
}

#endif /* x86 Base64 SIMD */

int64_t zan_hw_base64_encode(const uint8_t *src, int64_t len, char *dst) {
  if (!src || len <= 0 || !dst)
    return 0;
  const uint8_t *s = src;
  char *d = dst;
  int64_t rem = len;

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
     defined(_M_IX86)) &&                                                      \
    (defined(__GNUC__) || defined(__clang__))
  if (rem >= 28 && zan_hw_x86_can(ZAN_HW_AVX2)) {
    zan_b64_encode_avx2(&s, &d, &rem);
  } else if (rem >= 16 && zan_hw_x86_can(ZAN_HW_SSSE3)) {
    zan_b64_encode_ssse3(&s, &d, &rem);
  }
#endif

  while (rem >= 12) {
    uint32_t w0 = ((uint32_t)s[0] << 16) | ((uint32_t)s[1] << 8) | s[2];
    uint32_t w1 = ((uint32_t)s[3] << 16) | ((uint32_t)s[4] << 8) | s[5];
    uint32_t w2 = ((uint32_t)s[6] << 16) | ((uint32_t)s[7] << 8) | s[8];
    uint32_t w3 = ((uint32_t)s[9] << 16) | ((uint32_t)s[10] << 8) | s[11];

    d[0] = ZAN_B64_ENC_TABLE[(w0 >> 18) & 0x3F];
    d[1] = ZAN_B64_ENC_TABLE[(w0 >> 12) & 0x3F];
    d[2] = ZAN_B64_ENC_TABLE[(w0 >> 6) & 0x3F];
    d[3] = ZAN_B64_ENC_TABLE[w0 & 0x3F];

    d[4] = ZAN_B64_ENC_TABLE[(w1 >> 18) & 0x3F];
    d[5] = ZAN_B64_ENC_TABLE[(w1 >> 12) & 0x3F];
    d[6] = ZAN_B64_ENC_TABLE[(w1 >> 6) & 0x3F];
    d[7] = ZAN_B64_ENC_TABLE[w1 & 0x3F];

    d[8] = ZAN_B64_ENC_TABLE[(w2 >> 18) & 0x3F];
    d[9] = ZAN_B64_ENC_TABLE[(w2 >> 12) & 0x3F];
    d[10] = ZAN_B64_ENC_TABLE[(w2 >> 6) & 0x3F];
    d[11] = ZAN_B64_ENC_TABLE[w2 & 0x3F];

    d[12] = ZAN_B64_ENC_TABLE[(w3 >> 18) & 0x3F];
    d[13] = ZAN_B64_ENC_TABLE[(w3 >> 12) & 0x3F];
    d[14] = ZAN_B64_ENC_TABLE[(w3 >> 6) & 0x3F];
    d[15] = ZAN_B64_ENC_TABLE[w3 & 0x3F];

    s += 12;
    d += 16;
    rem -= 12;
  }

  while (rem >= 3) {
    uint32_t w = ((uint32_t)s[0] << 16) | ((uint32_t)s[1] << 8) | s[2];
    d[0] = ZAN_B64_ENC_TABLE[(w >> 18) & 0x3F];
    d[1] = ZAN_B64_ENC_TABLE[(w >> 12) & 0x3F];
    d[2] = ZAN_B64_ENC_TABLE[(w >> 6) & 0x3F];
    d[3] = ZAN_B64_ENC_TABLE[w & 0x3F];
    s += 3;
    d += 4;
    rem -= 3;
  }

  if (rem == 1) {
    uint32_t w = (uint32_t)s[0] << 16;
    d[0] = ZAN_B64_ENC_TABLE[(w >> 18) & 0x3F];
    d[1] = ZAN_B64_ENC_TABLE[(w >> 12) & 0x3F];
    d[2] = '=';
    d[3] = '=';
    d += 4;
  } else if (rem == 2) {
    uint32_t w = ((uint32_t)s[0] << 16) | ((uint32_t)s[1] << 8);
    d[0] = ZAN_B64_ENC_TABLE[(w >> 18) & 0x3F];
    d[1] = ZAN_B64_ENC_TABLE[(w >> 12) & 0x3F];
    d[2] = ZAN_B64_ENC_TABLE[(w >> 6) & 0x3F];
    d[3] = '=';
    d += 4;
  }

  *d = '\0';
  return (int64_t)(d - dst);
}

int64_t zan_hw_base64_decode(const char *src, int64_t len, uint8_t *dst) {
  if (!src || len <= 0 || !dst)
    return 0;
  zan_b64_init_dec_table();

  while (len > 0 && src[len - 1] == '=')
    len--;
  int rem_mod = (int)(len & 3);
  if (rem_mod == 1)
    return -1;

  int64_t full_quads = len / 4;
  int64_t plain_len =
      full_quads * 3 + (rem_mod == 2 ? 1 : (rem_mod == 3 ? 2 : 0));
  if (plain_len < 0)
    return -1;

  const uint8_t *s = (const uint8_t *)src;
  uint8_t *d = dst;
  int64_t rem = full_quads * 4;

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
     defined(_M_IX86)) &&                                                      \
    (defined(__GNUC__) || defined(__clang__))
  if (rem >= 32 && zan_hw_x86_can(ZAN_HW_AVX2)) {
    zan_b64_decode_avx2(&s, &d, &rem);
  }
#endif

  while (rem >= 16) {
    uint8_t a0 = ZAN_B64_DEC_TABLE[s[0]], b0 = ZAN_B64_DEC_TABLE[s[1]],
            c0 = ZAN_B64_DEC_TABLE[s[2]], d0 = ZAN_B64_DEC_TABLE[s[3]];
    uint8_t a1 = ZAN_B64_DEC_TABLE[s[4]], b1 = ZAN_B64_DEC_TABLE[s[5]],
            c1 = ZAN_B64_DEC_TABLE[s[6]], d1 = ZAN_B64_DEC_TABLE[s[7]];
    uint8_t a2 = ZAN_B64_DEC_TABLE[s[8]], b2 = ZAN_B64_DEC_TABLE[s[9]],
            c2 = ZAN_B64_DEC_TABLE[s[10]], d2 = ZAN_B64_DEC_TABLE[s[11]];
    uint8_t a3 = ZAN_B64_DEC_TABLE[s[12]], b3 = ZAN_B64_DEC_TABLE[s[13]],
            c3 = ZAN_B64_DEC_TABLE[s[14]], d3 = ZAN_B64_DEC_TABLE[s[15]];

    if ((a0 | b0 | c0 | d0 | a1 | b1 | c1 | d1 | a2 | b2 | c2 | d2 | a3 | b3 |
         c3 | d3) &
        0x80)
      return -1;

    uint32_t n0 =
        ((uint32_t)a0 << 18) | ((uint32_t)b0 << 12) | ((uint32_t)c0 << 6) | d0;
    uint32_t n1 =
        ((uint32_t)a1 << 18) | ((uint32_t)b1 << 12) | ((uint32_t)c1 << 6) | d1;
    uint32_t n2 =
        ((uint32_t)a2 << 18) | ((uint32_t)b2 << 12) | ((uint32_t)c2 << 6) | d2;
    uint32_t n3 =
        ((uint32_t)a3 << 18) | ((uint32_t)b3 << 12) | ((uint32_t)c3 << 6) | d3;

    d[0] = (uint8_t)(n0 >> 16);
    d[1] = (uint8_t)(n0 >> 8);
    d[2] = (uint8_t)n0;
    d[3] = (uint8_t)(n1 >> 16);
    d[4] = (uint8_t)(n1 >> 8);
    d[5] = (uint8_t)n1;
    d[6] = (uint8_t)(n2 >> 16);
    d[7] = (uint8_t)(n2 >> 8);
    d[8] = (uint8_t)n2;
    d[9] = (uint8_t)(n3 >> 16);
    d[10] = (uint8_t)(n3 >> 8);
    d[11] = (uint8_t)n3;

    s += 16;
    d += 12;
    rem -= 16;
  }

  while (rem >= 4) {
    uint8_t a = ZAN_B64_DEC_TABLE[s[0]];
    uint8_t b = ZAN_B64_DEC_TABLE[s[1]];
    uint8_t c = ZAN_B64_DEC_TABLE[s[2]];
    uint8_t dd = ZAN_B64_DEC_TABLE[s[3]];
    if ((a | b | c | dd) & 0x80)
      return -1;
    uint32_t n =
        ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | dd;
    d[0] = (uint8_t)(n >> 16);
    d[1] = (uint8_t)(n >> 8);
    d[2] = (uint8_t)n;
    s += 4;
    d += 3;
    rem -= 4;
  }

  if (rem_mod == 2) {
    uint8_t a = ZAN_B64_DEC_TABLE[s[0]];
    uint8_t b = ZAN_B64_DEC_TABLE[s[1]];
    if ((a | b) & 0x80)
      return -1;
    if ((b & 0x0F) != 0)
      return -1; /* RFC 4648 unused bits */
    d[0] = (uint8_t)((a << 2) | (b >> 4));
    d += 1;
  } else if (rem_mod == 3) {
    uint8_t a = ZAN_B64_DEC_TABLE[s[0]];
    uint8_t b = ZAN_B64_DEC_TABLE[s[1]];
    uint8_t c = ZAN_B64_DEC_TABLE[s[2]];
    if ((a | b | c) & 0x80)
      return -1;
    if ((c & 0x03) != 0)
      return -1; /* RFC 4648 unused bits */
    uint32_t n = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6);
    d[0] = (uint8_t)(n >> 16);
    d[1] = (uint8_t)(n >> 8);
    d += 2;
  }

  return (int64_t)(d - dst);
}

int64_t zan_hw_ffi_base64_capacity(int64_t len) {
  if (len <= 0)
    return 1;
  const int64_t groups = len / 3 + (len % 3 != 0);
  /* Leave room for the terminator and allocation header. The encoder
   * reports its output length as a pointer difference. */
  if (groups > (PTRDIFF_MAX - ZAN_ARR_HDR_SIZE - 1) / 4)
    return -1;
  return groups * 4 + 1;
}

int64_t zan_hw_ffi_base64_encode(uint64_t src, int64_t len, uint64_t dst) {
  return zan_hw_base64_encode((const uint8_t *)(uintptr_t)src, len,
                              (char *)(uintptr_t)dst);
}

int64_t zan_hw_ffi_base64_decode(uint64_t dst, uint64_t src, int64_t len) {
  return zan_hw_base64_decode((const char *)(uintptr_t)src, len,
                              (uint8_t *)(uintptr_t)dst);
}
