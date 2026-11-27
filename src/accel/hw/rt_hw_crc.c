/* rt_hw_crc.c: CRC32 与 CRC32C 硬件切片与指令加速实现 */
#include "rt_hw_internal.h"

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
     defined(_M_IX86)) &&                                                      \
    (defined(__GNUC__) || defined(__clang__))

/* 3 路并行 SSE4.2 CRC-32C + PCLMULQDQ 零查表 GF(2) 移位合并：
 * _mm_crc32_u64 延迟 3 周期、吞吐 1 周期；3 路独立流跑满硬件峰值（~2.5~3x 加速）。 */
__attribute__((target("sse4.2,crc32,pclmul"))) static uint32_t
zan_crc32c_sse42_3way(uint32_t crc, const uint8_t *p, int64_t n) {
  uint64_t c = crc;
  while (n >= 8 && ((uintptr_t)p & 7)) {
    c = _mm_crc32_u8((uint32_t)c, *p++);
    n--;
  }
  const __m128i k1024 = _mm_cvtsi32_si128((int)0x170076fau);
  const __m128i k2048 = _mm_cvtsi32_si128((int)0xa51b6135u);
  while (n >= 3072) {
    uint64_t c0 = c, c1 = 0, c2 = 0;
    const uint8_t *p0 = p;
    const uint8_t *p1 = p + 1024;
    const uint8_t *p2 = p + 2048;
    for (int i = 0; i < 128; i++) {
      uint64_t v0, v1, v2;
      memcpy(&v0, p0, 8);
      memcpy(&v1, p1, 8);
      memcpy(&v2, p2, 8);
      c0 = _mm_crc32_u64(c0, v0);
      c1 = _mm_crc32_u64(c1, v1);
      c2 = _mm_crc32_u64(c2, v2);
      p0 += 8;
      p1 += 8;
      p2 += 8;
    }
    uint64_t m0 = (uint64_t)_mm_cvtsi128_si64(
        _mm_clmulepi64_si128(_mm_cvtsi32_si128((int)(uint32_t)c0), k2048, 0x00));
    uint64_t m1 = (uint64_t)_mm_cvtsi128_si64(
        _mm_clmulepi64_si128(_mm_cvtsi32_si128((int)(uint32_t)c1), k1024, 0x00));
    c = _mm_crc32_u64(0, m0) ^ _mm_crc32_u64(0, m1) ^ c2;
    p += 3072;
    n -= 3072;
  }
  const __m128i k256 = _mm_cvtsi32_si128((int)0xb9e02b86u);
  const __m128i k512 = _mm_cvtsi32_si128((int)0xdd7e3b0cu);
  while (n >= 768) {
    uint64_t c0 = c, c1 = 0, c2 = 0;
    const uint8_t *p0 = p;
    const uint8_t *p1 = p + 256;
    const uint8_t *p2 = p + 512;
    for (int i = 0; i < 32; i++) {
      uint64_t v0, v1, v2;
      memcpy(&v0, p0, 8);
      memcpy(&v1, p1, 8);
      memcpy(&v2, p2, 8);
      c0 = _mm_crc32_u64(c0, v0);
      c1 = _mm_crc32_u64(c1, v1);
      c2 = _mm_crc32_u64(c2, v2);
      p0 += 8;
      p1 += 8;
      p2 += 8;
    }
    uint64_t m0 = (uint64_t)_mm_cvtsi128_si64(
        _mm_clmulepi64_si128(_mm_cvtsi32_si128((int)(uint32_t)c0), k512, 0x00));
    uint64_t m1 = (uint64_t)_mm_cvtsi128_si64(
        _mm_clmulepi64_si128(_mm_cvtsi32_si128((int)(uint32_t)c1), k256, 0x00));
    c = _mm_crc32_u64(0, m0) ^ _mm_crc32_u64(0, m1) ^ c2;
    p += 768;
    n -= 768;
  }
  while (n >= 8) {
    uint64_t v;
    memcpy(&v, p, 8);
    c = _mm_crc32_u64(c, v);
    p += 8;
    n -= 8;
  }
  if (n >= 4) {
    uint32_t v;
    memcpy(&v, p, 4);
    c = _mm_crc32_u32((uint32_t)c, v);
    p += 4;
    n -= 4;
  }
  if (n >= 2) {
    uint16_t v;
    memcpy(&v, p, 2);
    c = _mm_crc32_u16((uint32_t)c, v);
    p += 2;
    n -= 2;
  }
  if (n >= 1) {
    c = _mm_crc32_u8((uint32_t)c, *p);
  }
  return (uint32_t)c;
}

__attribute__((target("sse4.2,crc32"))) static uint32_t
zan_crc32c_sse42(uint32_t crc, const uint8_t *p, int64_t n) {
  uint64_t c = crc;
  while (n >= 8 && ((uintptr_t)p & 7)) {
    c = _mm_crc32_u8((uint32_t)c, *p++);
    n--;
  }
  while (n >= 8) {
    c = _mm_crc32_u64(c, *(const uint64_t *)p);
    p += 8;
    n -= 8;
  }
  if (n >= 4) {
    c = _mm_crc32_u32((uint32_t)c, *(const uint32_t *)p);
    p += 4;
    n -= 4;
  }
  if (n >= 2) {
    c = _mm_crc32_u16((uint32_t)c, *(const uint16_t *)p);
    p += 2;
    n -= 2;
  }
  if (n >= 1) {
    c = _mm_crc32_u8((uint32_t)c, *p);
  }
  return (uint32_t)c;
}

/* Intel 4 路 64B PCLMULQDQ 折叠 + Barrett 规约 IEEE 802.3 CRC-32（多项式 0xEDB88320）。 */
__attribute__((target("pclmul,sse4.1"))) static inline __m128i
zan_crc32_fold128(__m128i v, __m128i k, __m128i nxt) {
  __m128i a = _mm_clmulepi64_si128(v, k, 0x00);
  __m128i b = _mm_clmulepi64_si128(v, k, 0x11);
  return _mm_xor_si128(_mm_xor_si128(a, b), nxt);
}

__attribute__((target("pclmul,sse4.1"))) static uint32_t
zan_crc32_pclmul(uint32_t crc, const uint8_t *p, int64_t n,
                 const uint32_t table[8][256]) {
  uint32_t c = crc ^ UINT32_MAX;
  if (n < 64) {
    while (n > 0) {
      c = table[0][(c ^ *p++) & 0xFF] ^ (c >> 8);
      n--;
    }
    return c ^ UINT32_MAX;
  }
  const __m128i k12 =
      _mm_set_epi64x((long long)0x1c6e41596ULL, (long long)0x154442bd4ULL);
  const __m128i k34 =
      _mm_set_epi64x((long long)0x0ccaa009eULL, (long long)0x1751997d0ULL);
  const __m128i k6 = _mm_set_epi64x(0, (long long)0x163cd6124ULL);
  const __m128i mu_poly =
      _mm_set_epi64x((long long)0x1db710641ULL, (long long)0x1f7011641ULL);
  const __m128i mask32 = _mm_set_epi64x(0, 0xFFFFFFFFLL);

  __m128i x0 = _mm_xor_si128(_mm_loadu_si128((const __m128i *)p),
                             _mm_cvtsi32_si128((int)c));
  __m128i x1 = _mm_loadu_si128((const __m128i *)(p + 16));
  __m128i x2 = _mm_loadu_si128((const __m128i *)(p + 32));
  __m128i x3 = _mm_loadu_si128((const __m128i *)(p + 48));
  p += 64;
  n -= 64;

  while (n >= 64) {
    x0 = zan_crc32_fold128(x0, k12, _mm_loadu_si128((const __m128i *)p));
    x1 = zan_crc32_fold128(x1, k12, _mm_loadu_si128((const __m128i *)(p + 16)));
    x2 = zan_crc32_fold128(x2, k12, _mm_loadu_si128((const __m128i *)(p + 32)));
    x3 = zan_crc32_fold128(x3, k12, _mm_loadu_si128((const __m128i *)(p + 48)));
    p += 64;
    n -= 64;
  }

  x1 = zan_crc32_fold128(x0, k34, x1);
  x2 = zan_crc32_fold128(x1, k34, x2);
  x3 = zan_crc32_fold128(x2, k34, x3);

  while (n >= 16) {
    x3 = zan_crc32_fold128(x3, k34, _mm_loadu_si128((const __m128i *)p));
    p += 16;
    n -= 16;
  }

  /* 128 -> 96（内含 * x^32）-> 64 -> Barrett 32 */
  __m128i tmp =
      _mm_xor_si128(_mm_clmulepi64_si128(x3, k34, 0x10), _mm_srli_si128(x3, 8));
  __m128i r64 = _mm_xor_si128(
      _mm_srli_si128(tmp, 4),
      _mm_clmulepi64_si128(_mm_and_si128(tmp, mask32), k6, 0x00));
  __m128i q = _mm_and_si128(
      _mm_clmulepi64_si128(_mm_and_si128(r64, mask32), mu_poly, 0x00), mask32);
  __m128i res = _mm_xor_si128(r64, _mm_clmulepi64_si128(q, mu_poly, 0x10));
  c = (uint32_t)_mm_extract_epi32(res, 1);

  while (n > 0) {
    c = table[0][(c ^ *p++) & 0xFF] ^ (c >> 8);
    n--;
  }
  return c ^ UINT32_MAX;
}
#endif /* x86 crc */

#if (defined(__aarch64__) || defined(_M_ARM64)) &&                             \
    (defined(__GNUC__) || defined(__clang__))
#include <arm_acle.h>
__attribute__((target("crc"))) static uint32_t
zan_crc32c_pmull_arm(uint32_t crc, const uint8_t *p, int64_t n) {
  uint64_t c = crc;
  while (n >= 8 && ((uintptr_t)p & 7)) {
    c = __crc32cb((uint32_t)c, *p++);
    n--;
  }
  while (n >= 8) {
    c = __crc32cd((uint32_t)c, *(const uint64_t *)p);
    p += 8;
    n -= 8;
  }
  if (n >= 4) {
    c = __crc32cw((uint32_t)c, *(const uint32_t *)p);
    p += 4;
    n -= 4;
  }
  if (n >= 2) {
    c = __crc32ch((uint32_t)c, *(const uint16_t *)p);
    p += 2;
    n -= 2;
  }
  if (n >= 1) {
    c = __crc32cb((uint32_t)c, *p);
  }
  return (uint32_t)c;
}

__attribute__((target("crc"))) static uint32_t
zan_crc32_arm(uint32_t crc, const uint8_t *p, int64_t n) {
  uint64_t c = (uint64_t)(crc ^ 0xFFFFFFFFu);
  while (n >= 8 && ((uintptr_t)p & 7)) {
    c = __crc32b((uint32_t)c, *p++);
    n--;
  }
  while (n >= 8) {
    c = __crc32d((uint32_t)c, *(const uint64_t *)p);
    p += 8;
    n -= 8;
  }
  if (n >= 4) {
    c = __crc32w((uint32_t)c, *(const uint32_t *)p);
    p += 4;
    n -= 4;
  }
  if (n >= 2) {
    c = __crc32h((uint32_t)c, *(const uint16_t *)p);
    p += 2;
    n -= 2;
  }
  if (n >= 1) {
    c = __crc32b((uint32_t)c, *p);
  }
  return (uint32_t)c ^ 0xFFFFFFFFu;
}
#endif /* arm crc */

static uint32_t s_zan_crc32_table[8][256];
static uint32_t s_zan_crc32c_table[8][256];
static int s_zan_crc32_table_state;
static int s_zan_crc32c_table_state;

static void zan_init_crc_table(uint32_t table[8][256], int *state,
                               uint32_t polynomial);

static int zan_crc32c_kat(void) {
  /* RFC 4960 B.8: CRC-32C("123456789") = 0xE3069283 */
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
     defined(_M_IX86)) &&                                                      \
    (defined(__GNUC__) || defined(__clang__))
  return zan_crc32c_sse42(0xFFFFFFFFu, (const uint8_t *)"123456789", 9) ==
         0x1CF96D7Cu;
#elif (defined(__aarch64__) || defined(_M_ARM64)) &&                           \
    (defined(__GNUC__) || defined(__clang__))
  return zan_crc32c_pmull_arm(0xFFFFFFFFu, (const uint8_t *)"123456789", 9) ==
         0x1CF96D7Cu;
#else
  return 1;
#endif
}

static int zan_crc32_kat(void) {
  /* IEEE 802.3: CRC-32("123456789") = 0xCBF43926；同时自检 64B PCLMUL 折叠路径 */
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
     defined(_M_IX86)) &&                                                      \
    (defined(__GNUC__) || defined(__clang__))
  zan_init_crc_table(s_zan_crc32_table, &s_zan_crc32_table_state, 0xEDB88320u);
  static const uint8_t k64[64] = {
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
      16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
      32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
      48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63};
  /* CRC-32(0..63) = 0x100ECE8C（IEEE 802.3） */
  return zan_crc32_pclmul(0, k64, 64, s_zan_crc32_table) == 0x100ECE8Cu;
#elif (defined(__aarch64__) || defined(_M_ARM64)) &&                           \
    (defined(__GNUC__) || defined(__clang__))
  return zan_crc32_arm(0, (const uint8_t *)"123456789", 9) == 0xCBF43926u;
#else
  return 1;
#endif
}

int64_t zan_hw_crc32c_update(uint32_t crc, const uint8_t *p, int64_t len) {
  if (len < 0 || !p)
    return -1;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
     defined(_M_IX86)) &&                                                      \
    (defined(__GNUC__) || defined(__clang__))
  if (zan_hw_gate(&g_gate_crc32c, zan_hw_has_sse42(), zan_crc32c_kat)) {
    if (len >= 768 && zan_hw_x86_can(ZAN_HW_PCLMUL)) {
      return (int64_t)zan_crc32c_sse42_3way(crc, p, len);
    }
    return (int64_t)zan_crc32c_sse42(crc, p, len);
  }
#elif (defined(__aarch64__) || defined(_M_ARM64)) &&                           \
    (defined(__GNUC__) || defined(__clang__))
  if (zan_hw_gate(&g_gate_crc32c, zan_hw_arm_crc32(), zan_crc32c_kat)) {
    return (int64_t)zan_crc32c_pmull_arm(crc, p, len);
  }
#endif
  return -1;
}

static void zan_init_crc_table(uint32_t table[8][256], int *state,
                               uint32_t polynomial) {
  if (__atomic_load_n(state, __ATOMIC_ACQUIRE) == 2)
    return;
  int expected = 0;
  if (!__atomic_compare_exchange_n(state, &expected, 1, 0, __ATOMIC_ACQ_REL,
                                   __ATOMIC_ACQUIRE)) {
    while (__atomic_load_n(state, __ATOMIC_ACQUIRE) != 2) {
    }
    return;
  }
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++) {
      c = (c & 1) ? (polynomial ^ (c >> 1)) : (c >> 1);
    }
    table[0][i] = c;
  }
  for (int k = 1; k < 8; k++) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = table[k - 1][i];
      table[k][i] = (c >> 8) ^ table[0][c & 0xFF];
    }
  }
  __atomic_store_n(state, 2, __ATOMIC_RELEASE);
}

static uint32_t zan_crc_slice8(const uint32_t table[8][256], uint32_t crc,
                               const uint8_t *buf, int64_t len) {
  uint32_t c = crc;
  const uint8_t *p = buf;
  while (len > 0 && ((uintptr_t)p & 7)) {
    c = table[0][(c ^ *p++) & 0xFF] ^ (c >> 8);
    len--;
  }
  while (len >= 8) {
    uint32_t one, two;
    memcpy(&one, p, sizeof(one));
    memcpy(&two, p + sizeof(one), sizeof(two));
    one ^= c;
    c = table[7][one & 0xFF] ^ table[6][(one >> 8) & 0xFF] ^
        table[5][(one >> 16) & 0xFF] ^ table[4][(one >> 24) & 0xFF] ^
        table[3][two & 0xFF] ^ table[2][(two >> 8) & 0xFF] ^
        table[1][(two >> 16) & 0xFF] ^ table[0][(two >> 24) & 0xFF];
    p += 8;
    len -= 8;
  }
  while (len > 0) {
    c = table[0][(c ^ *p++) & 0xFF] ^ (c >> 8);
    len--;
  }
  return c;
}

int64_t zan_hw_crc32_update(uint32_t crc, const uint8_t *p, int64_t len) {
  if (len < 0 || !p)
    return -1;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
     defined(_M_IX86)) &&                                                      \
    (defined(__GNUC__) || defined(__clang__))
  if (len >= 64 &&
      zan_hw_gate(&g_gate_crc32,
                  zan_hw_x86_can(ZAN_HW_PCLMUL | ZAN_HW_SSE41),
                  zan_crc32_kat)) {
    zan_init_crc_table(s_zan_crc32_table, &s_zan_crc32_table_state,
                       0xEDB88320u);
    return (int64_t)zan_crc32_pclmul(crc, p, len, s_zan_crc32_table);
  }
#elif (defined(__aarch64__) || defined(_M_ARM64)) &&                           \
    (defined(__GNUC__) || defined(__clang__))
  if (zan_hw_gate(&g_gate_crc32, zan_hw_arm_crc32(), zan_crc32_kat)) {
    return (int64_t)zan_crc32_arm(crc, p, len);
  }
#endif
  zan_init_crc_table(s_zan_crc32_table, &s_zan_crc32_table_state, 0xEDB88320u);
  return zan_crc_slice8(s_zan_crc32_table, crc ^ UINT32_MAX, p, len) ^
         UINT32_MAX;
}

int64_t zan_hw_ffi_crc32_update(int64_t crc, uint64_t p, int64_t len) {
  return zan_hw_crc32_update((uint32_t)crc, (const uint8_t *)(uintptr_t)p, len);
}

int64_t zan_hw_ffi_crc32c_update(int64_t crc, uint64_t p, int64_t len) {
  return zan_hw_crc32c_update((uint32_t)crc, (const uint8_t *)(uintptr_t)p,
                              len);
}

int64_t zan_hw_ffi_crc32c(uint64_t p, int64_t len) {
  if (len <= 0)
    return 0;
  if (!p)
    return -1;
  const uint8_t *data = (const uint8_t *)(uintptr_t)p;
  int64_t hw = zan_hw_crc32c_update(UINT32_MAX, data, len);
  if (hw >= 0)
    return (uint32_t)hw ^ UINT32_MAX;
  zan_init_crc_table(s_zan_crc32c_table, &s_zan_crc32c_table_state,
                     0x82F63B78u);
  return zan_crc_slice8(s_zan_crc32c_table, UINT32_MAX, data, len) ^ UINT32_MAX;
}
