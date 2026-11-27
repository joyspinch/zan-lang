/* rt_hw_aes.c: AES-NI / ARM Crypto 基础对称分组密码实现 */
#include "rt_hw_internal.h"

static uint8_t zan_aes_sbox[256];

static void zan_aes_init_sbox(void) {
  static int inited = 0;
  if (inited)
    return;
  uint8_t inv[256];
  inv[0] = 0;
  inv[1] = 1;
  for (int i = 2; i < 256; i++) {
    for (int j = 2; j < 256; j++) {
      uint8_t p = 0, a = (uint8_t)i, b = (uint8_t)j;
      for (int k = 0; k < 8; k++) {
        if (b & 1)
          p ^= a;
        uint8_t hi = a & 0x80;
        a <<= 1;
        if (hi)
          a ^= 0x1B;
        b >>= 1;
      }
      if (p == 1) {
        inv[i] = (uint8_t)j;
        break;
      }
    }
  }
  for (int i = 0; i < 256; i++) {
    uint8_t x = inv[i], r = x;
    for (int k = 0; k < 4; k++) {
      x = (uint8_t)((x << 1) | (x >> 7));
      r ^= x;
    }
    zan_aes_sbox[i] = (uint8_t)(r ^ 0x63);
  }
  inited = 1;
}

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
     defined(_M_IX86)) &&                                                      \
    (defined(__GNUC__) || defined(__clang__))

__attribute__((target("aes,sse4.1"))) static inline __m128i
zan_aes128_kg_step(__m128i k, __m128i kg) {
  kg = _mm_shuffle_epi32(kg, 0xFF);
  k = _mm_xor_si128(k, _mm_slli_si128(k, 4));
  k = _mm_xor_si128(k, _mm_slli_si128(k, 4));
  k = _mm_xor_si128(k, _mm_slli_si128(k, 4));
  return _mm_xor_si128(k, kg);
}

__attribute__((target("aes,sse4.1"))) static inline void
zan_aes192_kg_step(__m128i *t1, __m128i *t3, __m128i t2) {
  t2 = _mm_shuffle_epi32(t2, 0x55);
  __m128i t4 = _mm_slli_si128(*t1, 4);
  *t1 = _mm_xor_si128(*t1, t4);
  t4 = _mm_slli_si128(t4, 4);
  *t1 = _mm_xor_si128(*t1, t4);
  t4 = _mm_slli_si128(t4, 4);
  *t1 = _mm_xor_si128(*t1, t4);
  *t1 = _mm_xor_si128(*t1, t2);
  t2 = _mm_shuffle_epi32(*t1, 0xFF);
  t4 = _mm_slli_si128(*t3, 4);
  *t3 = _mm_xor_si128(*t3, t4);
  *t3 = _mm_xor_si128(*t3, t2);
}

__attribute__((target("aes,sse4.1"))) static inline __m128i
zan_aes256_kg_step1(__m128i t1, __m128i t2) {
  t2 = _mm_shuffle_epi32(t2, 0xFF);
  t1 = _mm_xor_si128(t1, _mm_slli_si128(t1, 4));
  t1 = _mm_xor_si128(t1, _mm_slli_si128(t1, 4));
  t1 = _mm_xor_si128(t1, _mm_slli_si128(t1, 4));
  return _mm_xor_si128(t1, t2);
}

__attribute__((target("aes,sse4.1"))) static inline __m128i
zan_aes256_kg_step2(__m128i t1, __m128i t3) {
  __m128i t2 = _mm_aeskeygenassist_si128(t1, 0x00);
  t2 = _mm_shuffle_epi32(t2, 0xAA);
  t3 = _mm_xor_si128(t3, _mm_slli_si128(t3, 4));
  t3 = _mm_xor_si128(t3, _mm_slli_si128(t3, 4));
  t3 = _mm_xor_si128(t3, _mm_slli_si128(t3, 4));
  return _mm_xor_si128(t3, t2);
}

/* 常量时间 AES-NI 硬件密钥扩展（零 S-box 内存查表，消除缓存时序侧信道） */
__attribute__((target("aes,sse4.1"))) static void
zan_aes_expand_enc_ni(const uint8_t *key, int keybits, __m128i rk[15],
                      int *nr_out) {
  if (keybits == 128) {
    __m128i k = _mm_loadu_si128((const __m128i *)key);
    rk[0] = k;
    k = zan_aes128_kg_step(k, _mm_aeskeygenassist_si128(k, 0x01));
    rk[1] = k;
    k = zan_aes128_kg_step(k, _mm_aeskeygenassist_si128(k, 0x02));
    rk[2] = k;
    k = zan_aes128_kg_step(k, _mm_aeskeygenassist_si128(k, 0x04));
    rk[3] = k;
    k = zan_aes128_kg_step(k, _mm_aeskeygenassist_si128(k, 0x08));
    rk[4] = k;
    k = zan_aes128_kg_step(k, _mm_aeskeygenassist_si128(k, 0x10));
    rk[5] = k;
    k = zan_aes128_kg_step(k, _mm_aeskeygenassist_si128(k, 0x20));
    rk[6] = k;
    k = zan_aes128_kg_step(k, _mm_aeskeygenassist_si128(k, 0x40));
    rk[7] = k;
    k = zan_aes128_kg_step(k, _mm_aeskeygenassist_si128(k, 0x80));
    rk[8] = k;
    k = zan_aes128_kg_step(k, _mm_aeskeygenassist_si128(k, 0x1B));
    rk[9] = k;
    k = zan_aes128_kg_step(k, _mm_aeskeygenassist_si128(k, 0x36));
    rk[10] = k;
    *nr_out = 10;
  } else if (keybits == 192) {
    __m128i t1 = _mm_loadu_si128((const __m128i *)key);
    __m128i t3 = _mm_loadl_epi64((const __m128i *)(key + 16));
    __m128i t3_prev;
    rk[0] = t1;
    t3_prev = t3;
    zan_aes192_kg_step(&t1, &t3, _mm_aeskeygenassist_si128(t3, 0x01));
    rk[1] = _mm_unpacklo_epi64(t3_prev, t1);
    rk[2] = _mm_alignr_epi8(t3, t1, 8);
    zan_aes192_kg_step(&t1, &t3, _mm_aeskeygenassist_si128(t3, 0x02));
    rk[3] = t1;
    t3_prev = t3;
    zan_aes192_kg_step(&t1, &t3, _mm_aeskeygenassist_si128(t3, 0x04));
    rk[4] = _mm_unpacklo_epi64(t3_prev, t1);
    rk[5] = _mm_alignr_epi8(t3, t1, 8);
    zan_aes192_kg_step(&t1, &t3, _mm_aeskeygenassist_si128(t3, 0x08));
    rk[6] = t1;
    t3_prev = t3;
    zan_aes192_kg_step(&t1, &t3, _mm_aeskeygenassist_si128(t3, 0x10));
    rk[7] = _mm_unpacklo_epi64(t3_prev, t1);
    rk[8] = _mm_alignr_epi8(t3, t1, 8);
    zan_aes192_kg_step(&t1, &t3, _mm_aeskeygenassist_si128(t3, 0x20));
    rk[9] = t1;
    t3_prev = t3;
    zan_aes192_kg_step(&t1, &t3, _mm_aeskeygenassist_si128(t3, 0x40));
    rk[10] = _mm_unpacklo_epi64(t3_prev, t1);
    rk[11] = _mm_alignr_epi8(t3, t1, 8);
    zan_aes192_kg_step(&t1, &t3, _mm_aeskeygenassist_si128(t3, 0x80));
    rk[12] = t1;
    *nr_out = 12;
  } else {
    __m128i t1 = _mm_loadu_si128((const __m128i *)key);
    __m128i t3 = _mm_loadu_si128((const __m128i *)(key + 16));
    rk[0] = t1;
    rk[1] = t3;
    t1 = zan_aes256_kg_step1(t1, _mm_aeskeygenassist_si128(t3, 0x01));
    rk[2] = t1;
    t3 = zan_aes256_kg_step2(t1, t3);
    rk[3] = t3;
    t1 = zan_aes256_kg_step1(t1, _mm_aeskeygenassist_si128(t3, 0x02));
    rk[4] = t1;
    t3 = zan_aes256_kg_step2(t1, t3);
    rk[5] = t3;
    t1 = zan_aes256_kg_step1(t1, _mm_aeskeygenassist_si128(t3, 0x04));
    rk[6] = t1;
    t3 = zan_aes256_kg_step2(t1, t3);
    rk[7] = t3;
    t1 = zan_aes256_kg_step1(t1, _mm_aeskeygenassist_si128(t3, 0x08));
    rk[8] = t1;
    t3 = zan_aes256_kg_step2(t1, t3);
    rk[9] = t3;
    t1 = zan_aes256_kg_step1(t1, _mm_aeskeygenassist_si128(t3, 0x10));
    rk[10] = t1;
    t3 = zan_aes256_kg_step2(t1, t3);
    rk[11] = t3;
    t1 = zan_aes256_kg_step1(t1, _mm_aeskeygenassist_si128(t3, 0x20));
    rk[12] = t1;
    t3 = zan_aes256_kg_step2(t1, t3);
    rk[13] = t3;
    t1 = zan_aes256_kg_step1(t1, _mm_aeskeygenassist_si128(t3, 0x40));
    rk[14] = t1;
    *nr_out = 14;
  }
}

__attribute__((target("aes,sse4.1"))) static void
zan_aes_expand_key_ni_bytes(const uint8_t *key, int keybits, uint8_t rk[15][16],
                            int *nr_out) {
  __m128i vrk[15];
  int nr = 0;
  zan_aes_expand_enc_ni(key, keybits, vrk, &nr);
  for (int i = 0; i <= nr; i++)
    _mm_storeu_si128((__m128i *)rk[i], vrk[i]);
  *nr_out = nr;
}
#endif

void zan_aes_expand_key(const uint8_t *key, int keybits, uint8_t rk[15][16],
                        int *nr_out) {
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
     defined(_M_IX86)) &&                                                      \
    (defined(__GNUC__) || defined(__clang__))
  if (zan_hw_x86_can(ZAN_HW_REQ_AES)) {
    zan_aes_expand_key_ni_bytes(key, keybits, rk, nr_out);
    return;
  }
#endif
  zan_aes_init_sbox();
  int nk = keybits / 32;
  int nr = nk + 6;
  uint32_t w[60];
  for (int i = 0; i < nk; i++) {
    w[i] = ((uint32_t)key[4 * i] << 24) | ((uint32_t)key[4 * i + 1] << 16) |
           ((uint32_t)key[4 * i + 2] << 8) | key[4 * i + 3];
  }
  int rc = 1;
  for (int i = nk; i < 4 * (nr + 1); i++) {
    uint32_t t = w[i - 1];
    if (i % nk == 0) {
      t = (t << 8) | (t >> 24); /* RotWord */
      t = ((uint32_t)zan_aes_sbox[(t >> 24) & 255] << 24) |
          ((uint32_t)zan_aes_sbox[(t >> 16) & 255] << 16) |
          ((uint32_t)zan_aes_sbox[(t >> 8) & 255] << 8) | zan_aes_sbox[t & 255];
      t ^= (uint32_t)rc << 24;
      rc = ((rc << 1) ^ ((rc & 0x80) ? 0x11B : 0)) & 0xFF;
    } else if (nk > 6 && i % nk == 4) {
      t = ((uint32_t)zan_aes_sbox[(t >> 24) & 255] << 24) |
          ((uint32_t)zan_aes_sbox[(t >> 16) & 255] << 16) |
          ((uint32_t)zan_aes_sbox[(t >> 8) & 255] << 8) | zan_aes_sbox[t & 255];
    }
    w[i] = w[i - nk] ^ t;
  }
  for (int r = 0; r <= nr; r++) {
    for (int c = 0; c < 4; c++) {
      uint32_t v = w[r * 4 + c];
      rk[r][c * 4] = (uint8_t)(v >> 24);
      rk[r][c * 4 + 1] = (uint8_t)(v >> 16);
      rk[r][c * 4 + 2] = (uint8_t)(v >> 8);
      rk[r][c * 4 + 3] = (uint8_t)v;
    }
  }
  *nr_out = nr;
}

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
     defined(_M_IX86)) &&                                                      \
    (defined(__GNUC__) || defined(__clang__))

__attribute__((target("aes,sse4.1"))) static void
zan_aes_expand_dec_hw(const uint8_t *key, int keybits, __m128i *dec_rk,
                      int *nr_out) {
  __m128i rk[15];
  int nr = 0;
  zan_aes_expand_enc_ni(key, keybits, rk, &nr);
  dec_rk[0] = rk[nr];
  for (int i = 1; i < nr; i++) {
    dec_rk[i] = _mm_aesimc_si128(rk[nr - i]);
  }
  dec_rk[nr] = rk[0];
  *nr_out = nr;
}

__attribute__((target("aes,sse4.1"))) static __m128i
zan_aes_enc_block_hw(const uint8_t *key, int keybits, __m128i block) {
  __m128i rk[15];
  int nr = 0;
  zan_aes_expand_enc_ni(key, keybits, rk, &nr);
  block = _mm_xor_si128(block, rk[0]);
  for (int r = 1; r < nr; r++)
    block = _mm_aesenc_si128(block, rk[r]);
  return _mm_aesenclast_si128(block, rk[nr]);
}

__attribute__((target("aes,sse4.1"))) static int64_t
zan_aes_cbc_encrypt_ni(const uint8_t *in, int64_t len, const uint8_t *key,
                       int keybits, const uint8_t *iv, uint8_t *out) {
  __m128i rk[15];
  int nr = 0;
  zan_aes_expand_enc_ni(key, keybits, rk, &nr);

  int pad_val = 16 - (int)(len % 16);
  int64_t full_blocks = len / 16;
  __m128i feedback = _mm_loadu_si128((const __m128i *)iv);

  for (int64_t i = 0; i < full_blocks; i++) {
    __m128i block = _mm_loadu_si128((const __m128i *)(in + i * 16));
    block = _mm_xor_si128(block, feedback);
    block = _mm_xor_si128(block, rk[0]);
    for (int r = 1; r < nr; r++)
      block = _mm_aesenc_si128(block, rk[r]);
    block = _mm_aesenclast_si128(block, rk[nr]);
    _mm_storeu_si128((__m128i *)(out + i * 16), block);
    feedback = block;
  }

  uint8_t tail[16];
  int rem = (int)(len - full_blocks * 16);
  for (int j = 0; j < rem; j++)
    tail[j] = in[full_blocks * 16 + j];
  for (int j = rem; j < 16; j++)
    tail[j] = (uint8_t)pad_val;

  __m128i block = _mm_loadu_si128((const __m128i *)tail);
  block = _mm_xor_si128(block, feedback);
  block = _mm_xor_si128(block, rk[0]);
  for (int r = 1; r < nr; r++)
    block = _mm_aesenc_si128(block, rk[r]);
  block = _mm_aesenclast_si128(block, rk[nr]);
  _mm_storeu_si128((__m128i *)(out + full_blocks * 16), block);
  return (full_blocks + 1) * 16;
}

/* 8 路流水线 AES-CBC 解密（各块相互独立，充分填满 aesdec 流水线并支持原地解密） */
__attribute__((target("aes,sse4.1"))) static int64_t
zan_aes_cbc_decrypt_ni(const uint8_t *in, int64_t len, const uint8_t *key,
                       int keybits, const uint8_t *iv, uint8_t *out) {
  if (len <= 0 || (len % 16) != 0)
    return -1;
  __m128i dec_rk[15];
  int nr = 0;
  zan_aes_expand_dec_hw(key, keybits, dec_rk, &nr);

  int64_t blocks = len / 16;
  int64_t i = 0;
  __m128i prev = _mm_loadu_si128((const __m128i *)iv);

  for (; i + 8 <= blocks; i += 8) {
    const uint8_t *p = in + i * 16;
    uint8_t *q = out + i * 16;
    __m128i c0 = _mm_loadu_si128((const __m128i *)(p + 0));
    __m128i c1 = _mm_loadu_si128((const __m128i *)(p + 16));
    __m128i c2 = _mm_loadu_si128((const __m128i *)(p + 32));
    __m128i c3 = _mm_loadu_si128((const __m128i *)(p + 48));
    __m128i c4 = _mm_loadu_si128((const __m128i *)(p + 64));
    __m128i c5 = _mm_loadu_si128((const __m128i *)(p + 80));
    __m128i c6 = _mm_loadu_si128((const __m128i *)(p + 96));
    __m128i c7 = _mm_loadu_si128((const __m128i *)(p + 112));

    __m128i b0 = _mm_xor_si128(c0, dec_rk[0]);
    __m128i b1 = _mm_xor_si128(c1, dec_rk[0]);
    __m128i b2 = _mm_xor_si128(c2, dec_rk[0]);
    __m128i b3 = _mm_xor_si128(c3, dec_rk[0]);
    __m128i b4 = _mm_xor_si128(c4, dec_rk[0]);
    __m128i b5 = _mm_xor_si128(c5, dec_rk[0]);
    __m128i b6 = _mm_xor_si128(c6, dec_rk[0]);
    __m128i b7 = _mm_xor_si128(c7, dec_rk[0]);

    for (int r = 1; r < nr; r++) {
      __m128i rk = dec_rk[r];
      b0 = _mm_aesdec_si128(b0, rk);
      b1 = _mm_aesdec_si128(b1, rk);
      b2 = _mm_aesdec_si128(b2, rk);
      b3 = _mm_aesdec_si128(b3, rk);
      b4 = _mm_aesdec_si128(b4, rk);
      b5 = _mm_aesdec_si128(b5, rk);
      b6 = _mm_aesdec_si128(b6, rk);
      b7 = _mm_aesdec_si128(b7, rk);
    }
    __m128i rkl = dec_rk[nr];
    b0 = _mm_xor_si128(_mm_aesdeclast_si128(b0, rkl), prev);
    b1 = _mm_xor_si128(_mm_aesdeclast_si128(b1, rkl), c0);
    b2 = _mm_xor_si128(_mm_aesdeclast_si128(b2, rkl), c1);
    b3 = _mm_xor_si128(_mm_aesdeclast_si128(b3, rkl), c2);
    b4 = _mm_xor_si128(_mm_aesdeclast_si128(b4, rkl), c3);
    b5 = _mm_xor_si128(_mm_aesdeclast_si128(b5, rkl), c4);
    b6 = _mm_xor_si128(_mm_aesdeclast_si128(b6, rkl), c5);
    b7 = _mm_xor_si128(_mm_aesdeclast_si128(b7, rkl), c6);
    prev = c7;

    _mm_storeu_si128((__m128i *)(q + 0), b0);
    _mm_storeu_si128((__m128i *)(q + 16), b1);
    _mm_storeu_si128((__m128i *)(q + 32), b2);
    _mm_storeu_si128((__m128i *)(q + 48), b3);
    _mm_storeu_si128((__m128i *)(q + 64), b4);
    _mm_storeu_si128((__m128i *)(q + 80), b5);
    _mm_storeu_si128((__m128i *)(q + 96), b6);
    _mm_storeu_si128((__m128i *)(q + 112), b7);
  }

  if (i + 4 <= blocks) {
    const uint8_t *p = in + i * 16;
    uint8_t *q = out + i * 16;
    __m128i c0 = _mm_loadu_si128((const __m128i *)(p + 0));
    __m128i c1 = _mm_loadu_si128((const __m128i *)(p + 16));
    __m128i c2 = _mm_loadu_si128((const __m128i *)(p + 32));
    __m128i c3 = _mm_loadu_si128((const __m128i *)(p + 48));
    __m128i b0 = _mm_xor_si128(c0, dec_rk[0]);
    __m128i b1 = _mm_xor_si128(c1, dec_rk[0]);
    __m128i b2 = _mm_xor_si128(c2, dec_rk[0]);
    __m128i b3 = _mm_xor_si128(c3, dec_rk[0]);
    for (int r = 1; r < nr; r++) {
      __m128i rk = dec_rk[r];
      b0 = _mm_aesdec_si128(b0, rk);
      b1 = _mm_aesdec_si128(b1, rk);
      b2 = _mm_aesdec_si128(b2, rk);
      b3 = _mm_aesdec_si128(b3, rk);
    }
    __m128i rkl = dec_rk[nr];
    b0 = _mm_xor_si128(_mm_aesdeclast_si128(b0, rkl), prev);
    b1 = _mm_xor_si128(_mm_aesdeclast_si128(b1, rkl), c0);
    b2 = _mm_xor_si128(_mm_aesdeclast_si128(b2, rkl), c1);
    b3 = _mm_xor_si128(_mm_aesdeclast_si128(b3, rkl), c2);
    prev = c3;
    _mm_storeu_si128((__m128i *)(q + 0), b0);
    _mm_storeu_si128((__m128i *)(q + 16), b1);
    _mm_storeu_si128((__m128i *)(q + 32), b2);
    _mm_storeu_si128((__m128i *)(q + 48), b3);
    i += 4;
  }

  for (; i < blocks; i++) {
    __m128i cur = _mm_loadu_si128((const __m128i *)(in + i * 16));
    __m128i block = _mm_xor_si128(cur, dec_rk[0]);
    for (int r = 1; r < nr; r++)
      block = _mm_aesdec_si128(block, dec_rk[r]);
    block = _mm_aesdeclast_si128(block, dec_rk[nr]);
    block = _mm_xor_si128(block, prev);
    _mm_storeu_si128((__m128i *)(out + i * 16), block);
    prev = cur;
  }

  /* 无分支常量时间 PKCS#7 填充校验 */
  uint32_t pad_val = (uint32_t)out[len - 1];
  uint32_t bad = (uint32_t)((pad_val - 1u) >= 16u);
  for (uint32_t k = 0; k < 16u; k++) {
    uint32_t diff = (uint32_t)(out[len - 1 - k] ^ (uint8_t)pad_val);
    uint32_t in_pad = (uint32_t)(k - pad_val) >> 31;
    uint32_t nz = (diff | (0u - diff)) >> 31;
    bad |= in_pad & nz;
  }
  if (bad)
    return -2;
  return len - (int64_t)pad_val;
}

__attribute__((target("aes,sse4.1"))) static int64_t
zan_aes_ecb_block_ni(const uint8_t *key, int keybits, const uint8_t *in16,
                     uint8_t *out16) {
  __m128i block = _mm_loadu_si128((const __m128i *)in16);
  block = zan_aes_enc_block_hw(key, keybits, block);
  _mm_storeu_si128((__m128i *)out16, block);
  return 0;
}

/* 8 路流水线 AES-CTR 核心：寄存器常驻大端 128 位计数器，消除逐字节自增与栈回写停顿 */
__attribute__((target("aes,sse4.1"))) static void
zan_aes_ctr_ni_rk(const __m128i rk[15], int nr, const uint8_t *in, int64_t len,
                  uint8_t *counter16, uint8_t *out) {
  const __m128i BSWAP_LO =
      _mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 15, 14, 13, 12, 11, 10, 9, 8);
  const __m128i INC1 = _mm_set_epi64x(1, 0);
  const __m128i INC2 = _mm_set_epi64x(2, 0);
  const __m128i INC3 = _mm_set_epi64x(3, 0);
  const __m128i INC4 = _mm_set_epi64x(4, 0);
  const __m128i INC5 = _mm_set_epi64x(5, 0);
  const __m128i INC6 = _mm_set_epi64x(6, 0);
  const __m128i INC7 = _mm_set_epi64x(7, 0);
  const __m128i INC8 = _mm_set_epi64x(8, 0);

  uint64_t hi = 0, lo = 0;
  for (int i = 0; i < 8; i++)
    hi = (hi << 8) | counter16[i];
  for (int i = 8; i < 16; i++)
    lo = (lo << 8) | counter16[i];

  __m128i ctr_v =
      _mm_shuffle_epi8(_mm_loadu_si128((const __m128i *)counter16), BSWAP_LO);
  int64_t off = 0;

  while (off + 128 <= len && lo <= 0xFFFFFFFFFFFFFFF7ULL) {
    __m128i c0 = _mm_shuffle_epi8(ctr_v, BSWAP_LO);
    __m128i c1 = _mm_shuffle_epi8(_mm_add_epi64(ctr_v, INC1), BSWAP_LO);
    __m128i c2 = _mm_shuffle_epi8(_mm_add_epi64(ctr_v, INC2), BSWAP_LO);
    __m128i c3 = _mm_shuffle_epi8(_mm_add_epi64(ctr_v, INC3), BSWAP_LO);
    __m128i c4 = _mm_shuffle_epi8(_mm_add_epi64(ctr_v, INC4), BSWAP_LO);
    __m128i c5 = _mm_shuffle_epi8(_mm_add_epi64(ctr_v, INC5), BSWAP_LO);
    __m128i c6 = _mm_shuffle_epi8(_mm_add_epi64(ctr_v, INC6), BSWAP_LO);
    __m128i c7 = _mm_shuffle_epi8(_mm_add_epi64(ctr_v, INC7), BSWAP_LO);
    ctr_v = _mm_add_epi64(ctr_v, INC8);
    lo += 8;

    __m128i k0 = rk[0];
    c0 = _mm_xor_si128(c0, k0);
    c1 = _mm_xor_si128(c1, k0);
    c2 = _mm_xor_si128(c2, k0);
    c3 = _mm_xor_si128(c3, k0);
    c4 = _mm_xor_si128(c4, k0);
    c5 = _mm_xor_si128(c5, k0);
    c6 = _mm_xor_si128(c6, k0);
    c7 = _mm_xor_si128(c7, k0);

    for (int r = 1; r < nr; r++) {
      __m128i kr = rk[r];
      c0 = _mm_aesenc_si128(c0, kr);
      c1 = _mm_aesenc_si128(c1, kr);
      c2 = _mm_aesenc_si128(c2, kr);
      c3 = _mm_aesenc_si128(c3, kr);
      c4 = _mm_aesenc_si128(c4, kr);
      c5 = _mm_aesenc_si128(c5, kr);
      c6 = _mm_aesenc_si128(c6, kr);
      c7 = _mm_aesenc_si128(c7, kr);
    }
    __m128i kl = rk[nr];
    c0 = _mm_aesenclast_si128(c0, kl);
    c1 = _mm_aesenclast_si128(c1, kl);
    c2 = _mm_aesenclast_si128(c2, kl);
    c3 = _mm_aesenclast_si128(c3, kl);
    c4 = _mm_aesenclast_si128(c4, kl);
    c5 = _mm_aesenclast_si128(c5, kl);
    c6 = _mm_aesenclast_si128(c6, kl);
    c7 = _mm_aesenclast_si128(c7, kl);

    const uint8_t *p = in + off;
    uint8_t *q = out + off;
    _mm_storeu_si128((__m128i *)(q + 0),
                     _mm_xor_si128(_mm_loadu_si128((const __m128i *)(p + 0)), c0));
    _mm_storeu_si128((__m128i *)(q + 16),
                     _mm_xor_si128(_mm_loadu_si128((const __m128i *)(p + 16)), c1));
    _mm_storeu_si128((__m128i *)(q + 32),
                     _mm_xor_si128(_mm_loadu_si128((const __m128i *)(p + 32)), c2));
    _mm_storeu_si128((__m128i *)(q + 48),
                     _mm_xor_si128(_mm_loadu_si128((const __m128i *)(p + 48)), c3));
    _mm_storeu_si128((__m128i *)(q + 64),
                     _mm_xor_si128(_mm_loadu_si128((const __m128i *)(p + 64)), c4));
    _mm_storeu_si128((__m128i *)(q + 80),
                     _mm_xor_si128(_mm_loadu_si128((const __m128i *)(p + 80)), c5));
    _mm_storeu_si128((__m128i *)(q + 96),
                     _mm_xor_si128(_mm_loadu_si128((const __m128i *)(p + 96)), c6));
    _mm_storeu_si128((__m128i *)(q + 112),
                     _mm_xor_si128(_mm_loadu_si128((const __m128i *)(p + 112)), c7));
    off += 128;
  }

  if (off + 64 <= len && lo <= 0xFFFFFFFFFFFFFFFBULL) {
    __m128i c0 = _mm_shuffle_epi8(ctr_v, BSWAP_LO);
    __m128i c1 = _mm_shuffle_epi8(_mm_add_epi64(ctr_v, INC1), BSWAP_LO);
    __m128i c2 = _mm_shuffle_epi8(_mm_add_epi64(ctr_v, INC2), BSWAP_LO);
    __m128i c3 = _mm_shuffle_epi8(_mm_add_epi64(ctr_v, INC3), BSWAP_LO);
    ctr_v = _mm_add_epi64(ctr_v, INC4);
    lo += 4;
    __m128i k0 = rk[0];
    c0 = _mm_xor_si128(c0, k0);
    c1 = _mm_xor_si128(c1, k0);
    c2 = _mm_xor_si128(c2, k0);
    c3 = _mm_xor_si128(c3, k0);
    for (int r = 1; r < nr; r++) {
      __m128i kr = rk[r];
      c0 = _mm_aesenc_si128(c0, kr);
      c1 = _mm_aesenc_si128(c1, kr);
      c2 = _mm_aesenc_si128(c2, kr);
      c3 = _mm_aesenc_si128(c3, kr);
    }
    __m128i kl = rk[nr];
    c0 = _mm_aesenclast_si128(c0, kl);
    c1 = _mm_aesenclast_si128(c1, kl);
    c2 = _mm_aesenclast_si128(c2, kl);
    c3 = _mm_aesenclast_si128(c3, kl);
    const uint8_t *p = in + off;
    uint8_t *q = out + off;
    _mm_storeu_si128((__m128i *)(q + 0),
                     _mm_xor_si128(_mm_loadu_si128((const __m128i *)(p + 0)), c0));
    _mm_storeu_si128((__m128i *)(q + 16),
                     _mm_xor_si128(_mm_loadu_si128((const __m128i *)(p + 16)), c1));
    _mm_storeu_si128((__m128i *)(q + 32),
                     _mm_xor_si128(_mm_loadu_si128((const __m128i *)(p + 32)), c2));
    _mm_storeu_si128((__m128i *)(q + 48),
                     _mm_xor_si128(_mm_loadu_si128((const __m128i *)(p + 48)), c3));
    off += 64;
  }

  uint8_t ctr[16];
  for (int i = 0; i < 8; i++)
    ctr[i] = (uint8_t)(hi >> (56 - 8 * i));
  for (int i = 0; i < 8; i++)
    ctr[8 + i] = (uint8_t)(lo >> (56 - 8 * i));

  while (off < len) {
    __m128i ks = _mm_loadu_si128((const __m128i *)ctr);
    ks = _mm_xor_si128(ks, rk[0]);
    for (int r = 1; r < nr; r++)
      ks = _mm_aesenc_si128(ks, rk[r]);
    ks = _mm_aesenclast_si128(ks, rk[nr]);

    int avail = (int)(len - off);
    if (avail >= 16) {
      __m128i blk = _mm_loadu_si128((const __m128i *)(in + off));
      _mm_storeu_si128((__m128i *)(out + off), _mm_xor_si128(blk, ks));
    } else {
      uint8_t tmpIn[16] = {0};
      memcpy(tmpIn, in + off, (size_t)avail);
      __m128i o = _mm_xor_si128(_mm_loadu_si128((const __m128i *)tmpIn), ks);
      uint8_t tmpOut[16];
      _mm_storeu_si128((__m128i *)tmpOut, o);
      memcpy(out + off, tmpOut, (size_t)avail);
    }

    if (++lo == 0) {
      ++hi;
      for (int i = 0; i < 8; i++)
        ctr[i] = (uint8_t)(hi >> (56 - 8 * i));
    }
    for (int i = 0; i < 8; i++)
      ctr[8 + i] = (uint8_t)(lo >> (56 - 8 * i));
    off += 16;
  }
  memcpy(counter16, ctr, 16);
}

__attribute__((target("aes,sse4.1"))) static int64_t
zan_aes_ctr_ni(const uint8_t *in, int64_t len, const uint8_t *key, int keybits,
               uint8_t *counter16, uint8_t *out) {
  __m128i rk[15];
  int nr = 0;
  zan_aes_expand_enc_ni(key, keybits, rk, &nr);
  zan_aes_ctr_ni_rk(rk, nr, in, len, counter16, out);
  return len;
}

#endif /* x86 aes */

#if (defined(__aarch64__) || defined(_M_ARM64)) &&                             \
    (defined(__GNUC__) || defined(__clang__))
__attribute__((target("aes"))) static uint8x16_t
zan_aes_enc_block_arm(const uint8_t *key, int keybits, uint8x16_t block) {
  uint8_t rkb[15][16];
  int nr;
  zan_aes_expand_key(key, keybits, rkb, &nr);
  for (int r = 0; r + 1 < nr; r++)
    block = vaesmcq_u8(vaeseq_u8(block, vld1q_u8(rkb[r])));
  return veorq_u8(vaeseq_u8(block, vld1q_u8(rkb[nr - 1])), vld1q_u8(rkb[nr]));
}

__attribute__((target("aes"))) static void
zan_aes_expand_dec_arm(const uint8_t *key, int keybits, uint8x16_t *dec_rk) {
  uint8_t rkb[15][16];
  int nr;
  zan_aes_expand_key(key, keybits, rkb, &nr);
  for (int i = 0; i <= nr; i++)
    dec_rk[i] = vld1q_u8(rkb[nr - i]);
}

__attribute__((target("aes"))) static int64_t
zan_aes_cbc_encrypt_arm(const uint8_t *in, int64_t len, const uint8_t *key,
                        int keybits, const uint8_t *iv, uint8_t *out) {
  uint8_t rkb[15][16];
  int nr;
  zan_aes_expand_key(key, keybits, rkb, &nr);
  uint8x16_t rk[15];
  for (int i = 0; i <= nr; i++)
    rk[i] = vld1q_u8(rkb[i]);

  int pad_val = 16 - (int)(len % 16);
  int64_t full_blocks = len / 16;
  uint8x16_t feedback = vld1q_u8(iv);

  for (int64_t i = 0; i < full_blocks; i++) {
    uint8x16_t block = veorq_u8(vld1q_u8(in + i * 16), feedback);
    for (int r = 0; r + 1 < nr; r++)
      block = vaesmcq_u8(vaeseq_u8(block, rk[r]));
    block = veorq_u8(vaeseq_u8(block, rk[nr - 1]), rk[nr]);
    vst1q_u8(out + i * 16, block);
    feedback = block;
  }

  uint8_t tail[16];
  int rem = (int)(len - full_blocks * 16);
  for (int j = 0; j < rem; j++)
    tail[j] = in[full_blocks * 16 + j];
  for (int j = rem; j < 16; j++)
    tail[j] = (uint8_t)pad_val;

  uint8x16_t block = veorq_u8(vld1q_u8(tail), feedback);
  for (int r = 0; r + 1 < nr; r++)
    block = vaesmcq_u8(vaeseq_u8(block, rk[r]));
  block = veorq_u8(vaeseq_u8(block, rk[nr - 1]), rk[nr]);
  vst1q_u8(out + full_blocks * 16, block);
  return (full_blocks + 1) * 16;
}

__attribute__((target("aes"))) static int64_t
zan_aes_cbc_decrypt_arm(const uint8_t *in, int64_t len, const uint8_t *key,
                        int keybits, const uint8_t *iv, uint8_t *out) {
  if (len <= 0 || (len % 16) != 0)
    return -1;
  uint8x16_t dec_rk[15];
  zan_aes_expand_dec_arm(key, keybits, dec_rk);
  int nr = keybits / 32 + 6;

  int64_t blocks = len / 16;
  uint8x16_t prev = vld1q_u8(iv);

  for (int64_t i = 0; i < blocks; i++) {
    uint8x16_t cur = vld1q_u8(in + i * 16);
    uint8x16_t block = cur;
    for (int r = 1; r < nr; r++) {
      uint8x16_t p = veorq_u8(vaesdq_u8(block, dec_rk[r - 1]), dec_rk[r]);
      block = veorq_u8(vaesimcq_u8(p), dec_rk[r]);
    }
    uint8x16_t plain = veorq_u8(vaesdq_u8(block, dec_rk[nr - 1]), dec_rk[nr]);
    plain = veorq_u8(plain, prev);
    vst1q_u8(out + i * 16, plain);
    prev = cur;
  }

  uint8_t pad_val = out[len - 1];
  int bad = (pad_val == 0 || pad_val > 16);
  for (int i = 0; i < 16; i++) {
    int diff = (out[len - 1 - i] ^ pad_val);
    int in_pad = (i < pad_val);
    if (in_pad && diff != 0)
      bad = 1;
  }
  if (bad)
    return -2;
  return len - pad_val;
}

__attribute__((target("aes"))) static int64_t
zan_aes_ecb_block_arm(const uint8_t *key, int keybits, const uint8_t *in16,
                      uint8_t *out16) {
  vst1q_u8(out16, zan_aes_enc_block_arm(key, keybits, vld1q_u8(in16)));
  return 0;
}

__attribute__((target("aes"))) static int64_t
zan_aes_ctr_arm(const uint8_t *in, int64_t len, const uint8_t *key, int keybits,
                uint8_t *counter16, uint8_t *out) {
  uint8_t rkb[15][16];
  int nr;
  zan_aes_expand_key(key, keybits, rkb, &nr);
  uint8x16_t rk[15];
  for (int i = 0; i <= nr; i++)
    rk[i] = vld1q_u8(rkb[i]);

  uint8_t ctr[16];
  memcpy(ctr, counter16, 16);
  int64_t off = 0;
  while (off < len) {
    uint8x16_t ks = vld1q_u8(ctr);
    for (int r = 0; r + 1 < nr; r++)
      ks = vaesmcq_u8(vaeseq_u8(ks, rk[r]));
    ks = veorq_u8(vaeseq_u8(ks, rk[nr - 1]), rk[nr]);

    uint8x16_t blk = vld1q_u8(in + off);
    uint8x16_t o = veorq_u8(blk, ks);
    int avail = (int)(len - off);
    if (avail >= 16) {
      vst1q_u8(out + off, o);
    } else {
      uint8_t tmp[16];
      vst1q_u8(tmp, o);
      memcpy(out + off, tmp, (size_t)avail);
    }

    for (int j = 15; j >= 0; j--) {
      if (++ctr[j] != 0)
        break;
    }
    off += 16;
  }
  memcpy(counter16, ctr, 16);
  return len;
}

#endif /* arm aes */

int zan_aes_kat(void) {
  static const uint8_t pt[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
                                 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
                                 0xcc, 0xdd, 0xee, 0xff};
  static const uint8_t key128[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                     0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
                                     0x0c, 0x0d, 0x0e, 0x0f};
  static const uint8_t key192[24] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
      0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
  static const uint8_t key256[32] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
      0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
      0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
  static const uint8_t ct128[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b,
                                    0x04, 0x30, 0xd8, 0xcd, 0xb7, 0x80,
                                    0x70, 0xb4, 0xc5, 0x5a};
  static const uint8_t ct192[16] = {0xdd, 0xa9, 0x7c, 0xa4, 0x86, 0x4c,
                                    0xdf, 0xe0, 0x6e, 0xaf, 0x70, 0xa0,
                                    0xec, 0x0d, 0x71, 0x91};
  static const uint8_t ct256[16] = {0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67,
                                    0x45, 0xbf, 0xea, 0xfc, 0x49, 0x90,
                                    0x4b, 0x49, 0x60, 0x89};

  /* SP 800-38A F.2.1 (AES-128.CBC.Encrypt) & F.2.5 (AES-256) */
  static const uint8_t cbc_key[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae,
                                      0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88,
                                      0x09, 0xcf, 0x4f, 0x3c};
  static const uint8_t cbc_iv[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                     0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
                                     0x0c, 0x0d, 0x0e, 0x0f};
  static const uint8_t cbc_pt[64] = {
      0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e,
      0x11, 0x73, 0x93, 0x17, 0x2a, 0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03,
      0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51, 0x30,
      0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11, 0xe5, 0xfb, 0xc1, 0x19,
      0x1a, 0x0a, 0x52, 0xef, 0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b,
      0x17, 0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10};
  static const uint8_t cbc_ct128[64] = {
      0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46, 0xce, 0xe9, 0x8e,
      0x9b, 0x12, 0xe9, 0x19, 0x7d, 0x50, 0x86, 0xcb, 0x9b, 0x50, 0x72,
      0x19, 0xee, 0x95, 0xdb, 0x11, 0x3a, 0x91, 0x76, 0x78, 0xb2, 0x73,
      0xbe, 0xd6, 0xb8, 0xe3, 0xc1, 0x74, 0x3b, 0x71, 0x16, 0xe6, 0x9e,
      0x22, 0x22, 0x95, 0x16, 0x3f, 0xf1, 0xca, 0xa1, 0x68, 0x1f, 0xac,
      0x09, 0x12, 0x0e, 0xca, 0x30, 0x75, 0x86, 0xe1, 0xa7};
  static const uint8_t cbc_key256[32] = {
      0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe, 0x2b, 0x73, 0xae,
      0xf0, 0x85, 0x7d, 0x77, 0x81, 0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61,
      0x08, 0xd7, 0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4};
  static const uint8_t cbc_ct256_1[16] = {0xf5, 0x8c, 0x4c, 0x04, 0xd6, 0xe5,
                                          0xf1, 0xba, 0x77, 0x9e, 0xab, 0xfb,
                                          0x5f, 0x7b, 0xfb, 0xd6};

  /* SP 800-38A F.5.1 (AES-128.CTR.Encrypt) */
  static const uint8_t ctr_iv[16] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5,
                                     0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb,
                                     0xfc, 0xfd, 0xfe, 0xff};
  static const uint8_t ctr_ct[32] = {
      0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26, 0x1b, 0xef, 0x68,
      0x64, 0x99, 0x0d, 0xb6, 0xce, 0x98, 0x06, 0xf6, 0x6b, 0x79, 0x70,
      0xfd, 0xff, 0x86, 0x17, 0x18, 0x7b, 0xb9, 0xff, 0xfd, 0xff};

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
     defined(_M_IX86)) &&                                                      \
    (defined(__GNUC__) || defined(__clang__))
  uint8_t out[80];
  uint8_t blk[16];

  if (zan_aes_ecb_block_ni(key128, 128, pt, blk) != 0)
    return 0;
  if (memcmp(blk, ct128, 16) != 0)
    return 0;
  if (zan_aes_ecb_block_ni(key192, 192, pt, blk) != 0)
    return 0;
  if (memcmp(blk, ct192, 16) != 0)
    return 0;
  if (zan_aes_ecb_block_ni(key256, 256, pt, blk) != 0)
    return 0;
  if (memcmp(blk, ct256, 16) != 0)
    return 0;

  int64_t n = zan_aes_cbc_encrypt_ni(cbc_pt, 64, cbc_key, 128, cbc_iv, out);
  if (n != 80 || memcmp(out, cbc_ct128, 64) != 0)
    return 0;
  uint8_t back[80];
  n = zan_aes_cbc_decrypt_ni(out, 80, cbc_key, 128, cbc_iv, back);
  if (n != 64 || memcmp(back, cbc_pt, 64) != 0)
    return 0;

  n = zan_aes_cbc_encrypt_ni(cbc_pt, 64, cbc_key256, 256, cbc_iv, out);
  if (n != 80 || memcmp(out, cbc_ct256_1, 16) != 0)
    return 0;
  n = zan_aes_cbc_decrypt_ni(out, 80, cbc_key256, 256, cbc_iv, back);
  if (n != 64 || memcmp(back, cbc_pt, 64) != 0)
    return 0;

  uint8_t ctrb[16];
  memcpy(ctrb, ctr_iv, 16);
  n = zan_aes_ctr_ni(cbc_pt, 32, cbc_key, 128, ctrb, out);
  if (n != 32 || memcmp(out, ctr_ct, 32) != 0)
    return 0;
  return 1;
#elif (defined(__aarch64__) || defined(_M_ARM64)) &&                           \
    (defined(__GNUC__) || defined(__clang__))
  uint8_t out[80];
  uint8_t blk[16];

  if (zan_aes_ecb_block_arm(key128, 128, pt, blk) != 0)
    return 0;
  if (memcmp(blk, ct128, 16) != 0)
    return 0;
  if (zan_aes_ecb_block_arm(key192, 192, pt, blk) != 0)
    return 0;
  if (memcmp(blk, ct192, 16) != 0)
    return 0;
  if (zan_aes_ecb_block_arm(key256, 256, pt, blk) != 0)
    return 0;
  if (memcmp(blk, ct256, 16) != 0)
    return 0;

  int64_t n = zan_aes_cbc_encrypt_arm(cbc_pt, 64, cbc_key, 128, cbc_iv, out);
  if (n != 80 || memcmp(out, cbc_ct128, 64) != 0)
    return 0;
  uint8_t back[80];
  n = zan_aes_cbc_decrypt_arm(out, 80, cbc_key, 128, cbc_iv, back);
  if (n != 64 || memcmp(back, cbc_pt, 64) != 0)
    return 0;

  n = zan_aes_cbc_encrypt_arm(cbc_pt, 64, cbc_key256, 256, cbc_iv, out);
  if (n != 80 || memcmp(out, cbc_ct256_1, 16) != 0)
    return 0;
  n = zan_aes_cbc_decrypt_arm(out, 80, cbc_key256, 256, cbc_iv, back);
  if (n != 64 || memcmp(back, cbc_pt, 64) != 0)
    return 0;

  uint8_t ctrb[16];
  memcpy(ctrb, ctr_iv, 16);
  n = zan_aes_ctr_arm(cbc_pt, 32, cbc_key, 128, ctrb, out);
  if (n != 32 || memcmp(out, ctr_ct, 32) != 0)
    return 0;
  return 1;
#else
  (void)pt;
  (void)key128;
  (void)key192;
  (void)key256;
  (void)ct128;
  (void)ct192;
  (void)ct256;
  (void)cbc_key;
  (void)cbc_iv;
  (void)cbc_pt;
  (void)cbc_ct128;
  (void)cbc_key256;
  (void)cbc_ct256_1;
  (void)ctr_iv;
  (void)ctr_ct;
  return 1;
#endif
}

int64_t zan_hw_aes_cbc_encrypt(const uint8_t *in, int64_t len,
                               const uint8_t *key, int keybits,
                               const uint8_t *iv, uint8_t *out) {
  if (len < 0 || !in || !key || !iv || !out)
    return -1;
  if (keybits != 128 && keybits != 192 && keybits != 256)
    return -1;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
     defined(_M_IX86)) &&                                                      \
    (defined(__GNUC__) || defined(__clang__))
  if (zan_hw_gate(&g_gate_aes, zan_hw_x86_can(ZAN_HW_REQ_AES), zan_aes_kat)) {
    return zan_aes_cbc_encrypt_ni(in, len, key, keybits, iv, out);
  }
#elif (defined(__aarch64__) || defined(_M_ARM64)) &&                           \
    (defined(__GNUC__) || defined(__clang__))
  if (zan_hw_gate(&g_gate_aes, zan_hw_arm_aes(), zan_aes_kat)) {
    return zan_aes_cbc_encrypt_arm(in, len, key, keybits, iv, out);
  }
#endif
  return -1;
}

int64_t zan_hw_aes_cbc_decrypt(const uint8_t *in, int64_t len,
                               const uint8_t *key, int keybits,
                               const uint8_t *iv, uint8_t *out) {
  if (len <= 0 || !in || !key || !iv || !out)
    return -1;
  if (keybits != 128 && keybits != 192 && keybits != 256)
    return -1;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
     defined(_M_IX86)) &&                                                      \
    (defined(__GNUC__) || defined(__clang__))
  if (zan_hw_gate(&g_gate_aes, zan_hw_x86_can(ZAN_HW_REQ_AES), zan_aes_kat)) {
    return zan_aes_cbc_decrypt_ni(in, len, key, keybits, iv, out);
  }
#elif (defined(__aarch64__) || defined(_M_ARM64)) &&                           \
    (defined(__GNUC__) || defined(__clang__))
  if (zan_hw_gate(&g_gate_aes, zan_hw_arm_aes(), zan_aes_kat)) {
    return zan_aes_cbc_decrypt_arm(in, len, key, keybits, iv, out);
  }
#endif
  return -1;
}

int64_t zan_hw_aes_ecb_block(const uint8_t *key, int keybits,
                             const uint8_t *in16, uint8_t *out16) {
  if (!key || !in16 || !out16)
    return -1;
  if (keybits != 128 && keybits != 192 && keybits != 256)
    return -1;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
     defined(_M_IX86)) &&                                                      \
    (defined(__GNUC__) || defined(__clang__))
  if (zan_hw_gate(&g_gate_aes, zan_hw_x86_can(ZAN_HW_REQ_AES), zan_aes_kat)) {
    return zan_aes_ecb_block_ni(key, keybits, in16, out16);
  }
#elif (defined(__aarch64__) || defined(_M_ARM64)) &&                           \
    (defined(__GNUC__) || defined(__clang__))
  if (zan_hw_gate(&g_gate_aes, zan_hw_arm_aes(), zan_aes_kat)) {
    return zan_aes_ecb_block_arm(key, keybits, in16, out16);
  }
#endif
  return -1;
}

int64_t zan_hw_aes_ctr_crypt(const uint8_t *in, int64_t len, const uint8_t *key,
                             int keybits, uint8_t *counter16, uint8_t *out) {
  if (len < 0 || !in || !key || !counter16 || !out)
    return -1;
  if (keybits != 128 && keybits != 192 && keybits != 256)
    return -1;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
     defined(_M_IX86)) &&                                                      \
    (defined(__GNUC__) || defined(__clang__))
  if (zan_hw_gate(&g_gate_aes, zan_hw_x86_can(ZAN_HW_REQ_AES), zan_aes_kat)) {
    return zan_aes_ctr_ni(in, len, key, keybits, counter16, out);
  }
#elif (defined(__aarch64__) || defined(_M_ARM64)) &&                           \
    (defined(__GNUC__) || defined(__clang__))
  if (zan_hw_gate(&g_gate_aes, zan_hw_arm_aes(), zan_aes_kat)) {
    return zan_aes_ctr_arm(in, len, key, keybits, counter16, out);
  }
#endif
  return -1;
}

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
     defined(_M_IX86)) &&                                                      \
    (defined(__GNUC__) || defined(__clang__))
__attribute__((target("aes,sse4.1"))) void
zan_aes_ecb_block_rk(const uint8_t rkb[15][16], int nr, const uint8_t *in16,
                     uint8_t *out16) {
  __m128i b = _mm_loadu_si128((const __m128i *)in16);
  b = _mm_xor_si128(b, _mm_loadu_si128((const __m128i *)rkb[0]));
  for (int r = 1; r < nr; r++) {
    b = _mm_aesenc_si128(b, _mm_loadu_si128((const __m128i *)rkb[r]));
  }
  b = _mm_aesenclast_si128(b, _mm_loadu_si128((const __m128i *)rkb[nr]));
  _mm_storeu_si128((__m128i *)out16, b);
}

__attribute__((target("aes,sse4.1"))) void
zan_aes_ctr_ni_4way(const uint8_t rkb[15][16], int nr, const uint8_t *in,
                    int64_t len, uint8_t *counter16, uint8_t *out) {
  __m128i rk[15];
  for (int i = 0; i <= nr; i++)
    rk[i] = _mm_loadu_si128((const __m128i *)rkb[i]);
  zan_aes_ctr_ni_rk(rk, nr, in, len, counter16, out);
}

#endif /* x86 ctr 4-way */

static uint8_t zan_aes_inv_sbox[256];
static int zan_aes_tables_inited = 0;

static inline uint8_t zan_aes_xtime(uint8_t a) {
  return (uint8_t)((a << 1) ^ ((a & 0x80) ? 0x1B : 0));
}

static inline uint8_t zan_aes_gmul(uint8_t a, uint8_t b) {
  uint8_t p = 0;
  for (int i = 0; i < 8; i++) {
    if (b & 1)
      p ^= a;
    uint8_t hi = a & 0x80;
    a <<= 1;
    if (hi)
      a ^= 0x1B;
    b >>= 1;
  }
  return p;
}

static void zan_aes_init_tables(void) {
  if (zan_aes_tables_inited)
    return;
  zan_aes_init_sbox();
  for (int i = 0; i < 256; i++) {
    zan_aes_inv_sbox[zan_aes_sbox[i]] = (uint8_t)i;
  }
  zan_aes_tables_inited = 1;
}

static void zan_aes_encrypt_round_soft(const void *val, const void *key,
                                       void *out, int mix_columns) {
  zan_aes_init_tables();
  const uint8_t *s = (const uint8_t *)val;
  const uint8_t *k = (const uint8_t *)key;
  uint8_t state[16];

  /* 1. SubBytes & ShiftRows */
  state[0] = zan_aes_sbox[s[0]];
  state[4] = zan_aes_sbox[s[4]];
  state[8] = zan_aes_sbox[s[8]];
  state[12] = zan_aes_sbox[s[12]];

  state[1] = zan_aes_sbox[s[5]];
  state[5] = zan_aes_sbox[s[9]];
  state[9] = zan_aes_sbox[s[13]];
  state[13] = zan_aes_sbox[s[1]];

  state[2] = zan_aes_sbox[s[10]];
  state[6] = zan_aes_sbox[s[14]];
  state[10] = zan_aes_sbox[s[2]];
  state[14] = zan_aes_sbox[s[6]];

  state[3] = zan_aes_sbox[s[15]];
  state[7] = zan_aes_sbox[s[3]];
  state[11] = zan_aes_sbox[s[7]];
  state[15] = zan_aes_sbox[s[11]];

  /* 2. MixColumns (if mix_columns != 0) */
  uint8_t mc[16];
  if (mix_columns) {
    for (int c = 0; c < 4; c++) {
      int i = c * 4;
      uint8_t s0 = state[i], s1 = state[i + 1], s2 = state[i + 2],
              s3 = state[i + 3];
      mc[i] = zan_aes_xtime(s0 ^ s1) ^ s1 ^ s2 ^ s3;
      mc[i + 1] = zan_aes_xtime(s1 ^ s2) ^ s2 ^ s3 ^ s0;
      mc[i + 2] = zan_aes_xtime(s2 ^ s3) ^ s3 ^ s0 ^ s1;
      mc[i + 3] = zan_aes_xtime(s3 ^ s0) ^ s0 ^ s1 ^ s2;
    }
  } else {
    memcpy(mc, state, 16);
  }

  /* 3. AddRoundKey */
  uint8_t *res = (uint8_t *)out;
  for (int i = 0; i < 16; i++) {
    res[i] = mc[i] ^ k[i];
  }
}

static void zan_aes_decrypt_round_soft(const void *val, const void *key,
                                       void *out, int inv_mix_columns) {
  zan_aes_init_tables();
  const uint8_t *s = (const uint8_t *)val;
  const uint8_t *k = (const uint8_t *)key;
  uint8_t state[16];

  /* 1. InvShiftRows & InvSubBytes */
  state[0] = zan_aes_inv_sbox[s[0]];
  state[4] = zan_aes_inv_sbox[s[4]];
  state[8] = zan_aes_inv_sbox[s[8]];
  state[12] = zan_aes_inv_sbox[s[12]];

  state[1] = zan_aes_inv_sbox[s[13]];
  state[5] = zan_aes_inv_sbox[s[1]];
  state[9] = zan_aes_inv_sbox[s[5]];
  state[13] = zan_aes_inv_sbox[s[9]];

  state[2] = zan_aes_inv_sbox[s[10]];
  state[6] = zan_aes_inv_sbox[s[14]];
  state[10] = zan_aes_inv_sbox[s[2]];
  state[14] = zan_aes_inv_sbox[s[6]];

  state[3] = zan_aes_inv_sbox[s[7]];
  state[7] = zan_aes_inv_sbox[s[11]];
  state[11] = zan_aes_inv_sbox[s[15]];
  state[15] = zan_aes_inv_sbox[s[3]];

  /* 2. InvMixColumns (if inv_mix_columns != 0) */
  uint8_t imc[16];
  if (inv_mix_columns) {
    for (int c = 0; c < 4; c++) {
      int i = c * 4;
      uint8_t s0 = state[i], s1 = state[i + 1], s2 = state[i + 2],
              s3 = state[i + 3];
      imc[i] = zan_aes_gmul(s0, 0x0e) ^ zan_aes_gmul(s1, 0x0b) ^
               zan_aes_gmul(s2, 0x0d) ^ zan_aes_gmul(s3, 0x09);
      imc[i + 1] = zan_aes_gmul(s0, 0x09) ^ zan_aes_gmul(s1, 0x0e) ^
                   zan_aes_gmul(s2, 0x0b) ^ zan_aes_gmul(s3, 0x0d);
      imc[i + 2] = zan_aes_gmul(s0, 0x0d) ^ zan_aes_gmul(s1, 0x09) ^
                   zan_aes_gmul(s2, 0x0e) ^ zan_aes_gmul(s3, 0x0b);
      imc[i + 3] = zan_aes_gmul(s0, 0x0b) ^ zan_aes_gmul(s1, 0x0d) ^
                   zan_aes_gmul(s2, 0x09) ^ zan_aes_gmul(s3, 0x0e);
    }
  } else {
    memcpy(imc, state, 16);
  }

  /* 3. AddRoundKey */
  uint8_t *res = (uint8_t *)out;
  for (int i = 0; i < 16; i++) {
    res[i] = imc[i] ^ k[i];
  }
}

static void zan_aes_imc_soft(const void *val, void *out) {
  const uint8_t *s = (const uint8_t *)val;
  uint8_t *res = (uint8_t *)out;
  for (int c = 0; c < 4; c++) {
    int i = c * 4;
    uint8_t s0 = s[i], s1 = s[i + 1], s2 = s[i + 2], s3 = s[i + 3];
    res[i] = zan_aes_gmul(s0, 0x0e) ^ zan_aes_gmul(s1, 0x0b) ^
             zan_aes_gmul(s2, 0x0d) ^ zan_aes_gmul(s3, 0x09);
    res[i + 1] = zan_aes_gmul(s0, 0x09) ^ zan_aes_gmul(s1, 0x0e) ^
                 zan_aes_gmul(s2, 0x0b) ^ zan_aes_gmul(s3, 0x0d);
    res[i + 2] = zan_aes_gmul(s0, 0x0d) ^ zan_aes_gmul(s1, 0x09) ^
                 zan_aes_gmul(s2, 0x0e) ^ zan_aes_gmul(s3, 0x0b);
    res[i + 3] = zan_aes_gmul(s0, 0x0b) ^ zan_aes_gmul(s1, 0x0d) ^
                 zan_aes_gmul(s2, 0x09) ^ zan_aes_gmul(s3, 0x0e);
  }
}

static void zan_aes_keygenassist_soft(const void *val, uint8_t rcon,
                                      void *out) {
  zan_aes_init_tables();
  const uint8_t *s = (const uint8_t *)val;
  uint8_t *res = (uint8_t *)out;

  /* Word 0 (bytes 0..3): SubWord(SRC[63:32] = bytes 4..7) */
  res[0] = zan_aes_sbox[s[4]];
  res[1] = zan_aes_sbox[s[5]];
  res[2] = zan_aes_sbox[s[6]];
  res[3] = zan_aes_sbox[s[7]];

  res[4] = zan_aes_sbox[s[5]] ^ rcon;
  res[5] = zan_aes_sbox[s[6]];
  res[6] = zan_aes_sbox[s[7]];
  res[7] = zan_aes_sbox[s[4]];

  /* Word 2 (bytes 8..11): SubWord(SRC[127:96] = bytes 12..15) */
  res[8] = zan_aes_sbox[s[12]];
  res[9] = zan_aes_sbox[s[13]];
  res[10] = zan_aes_sbox[s[14]];
  res[11] = zan_aes_sbox[s[15]];

  res[12] = zan_aes_sbox[s[13]] ^ rcon;
  res[13] = zan_aes_sbox[s[14]];
  res[14] = zan_aes_sbox[s[15]];
  res[15] = zan_aes_sbox[s[12]];
}

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||            \
     defined(_M_IX86)) &&                                                      \
    (defined(__GNUC__) || defined(__clang__))

__attribute__((target("aes,sse4.1"))) static inline void
zan_hw_aes_encrypt_ni(const void *val, const void *key, void *out) {
  __m128i v = _mm_loadu_si128((const __m128i *)val);
  __m128i k = _mm_loadu_si128((const __m128i *)key);
  _mm_storeu_si128((__m128i *)out, _mm_aesenc_si128(v, k));
}

__attribute__((target("aes,sse4.1"))) static inline void
zan_hw_aes_encrypt_last_ni(const void *val, const void *key, void *out) {
  __m128i v = _mm_loadu_si128((const __m128i *)val);
  __m128i k = _mm_loadu_si128((const __m128i *)key);
  _mm_storeu_si128((__m128i *)out, _mm_aesenclast_si128(v, k));
}

__attribute__((target("aes,sse4.1"))) static inline void
zan_hw_aes_decrypt_ni(const void *val, const void *key, void *out) {
  __m128i v = _mm_loadu_si128((const __m128i *)val);
  __m128i k = _mm_loadu_si128((const __m128i *)key);
  _mm_storeu_si128((__m128i *)out, _mm_aesdec_si128(v, k));
}

__attribute__((target("aes,sse4.1"))) static inline void
zan_hw_aes_decrypt_last_ni(const void *val, const void *key, void *out) {
  __m128i v = _mm_loadu_si128((const __m128i *)val);
  __m128i k = _mm_loadu_si128((const __m128i *)key);
  _mm_storeu_si128((__m128i *)out, _mm_aesdeclast_si128(v, k));
}

__attribute__((target("aes,sse4.1"))) static inline void
zan_hw_aes_keygenassist_ni(const void *val, uint8_t rcon, void *out) {
  __m128i v = _mm_loadu_si128((const __m128i *)val);
  __m128i res;
  switch (rcon) {
  case 0x01:
    res = _mm_aeskeygenassist_si128(v, 0x01);
    break;
  case 0x02:
    res = _mm_aeskeygenassist_si128(v, 0x02);
    break;
  case 0x04:
    res = _mm_aeskeygenassist_si128(v, 0x04);
    break;
  case 0x08:
    res = _mm_aeskeygenassist_si128(v, 0x08);
    break;
  case 0x10:
    res = _mm_aeskeygenassist_si128(v, 0x10);
    break;
  case 0x20:
    res = _mm_aeskeygenassist_si128(v, 0x20);
    break;
  case 0x40:
    res = _mm_aeskeygenassist_si128(v, 0x40);
    break;
  case 0x80:
    res = _mm_aeskeygenassist_si128(v, 0x80);
    break;
  case 0x1b:
    res = _mm_aeskeygenassist_si128(v, 0x1b);
    break;
  case 0x36:
    res = _mm_aeskeygenassist_si128(v, 0x36);
    break;
  default:
    res = _mm_aeskeygenassist_si128(v, 0x00);
    break;
  }
  _mm_storeu_si128((__m128i *)out, res);
}

__attribute__((target("aes,sse4.1"))) static inline void
zan_hw_aes_imc_ni(const void *val, void *out) {
  __m128i v = _mm_loadu_si128((const __m128i *)val);
  _mm_storeu_si128((__m128i *)out, _mm_aesimc_si128(v));
}

void zan_hw_aes_encrypt(const void *val, const void *key, void *out) {
  if (zan_hw_x86_can(ZAN_HW_REQ_AES)) {
    zan_hw_aes_encrypt_ni(val, key, out);
  } else {
    zan_aes_encrypt_round_soft(val, key, out, 1);
  }
}

void zan_hw_aes_encrypt_last(const void *val, const void *key, void *out) {
  if (zan_hw_x86_can(ZAN_HW_REQ_AES)) {
    zan_hw_aes_encrypt_last_ni(val, key, out);
  } else {
    zan_aes_encrypt_round_soft(val, key, out, 0);
  }
}

void zan_hw_aes_decrypt(const void *val, const void *key, void *out) {
  if (zan_hw_x86_can(ZAN_HW_REQ_AES)) {
    zan_hw_aes_decrypt_ni(val, key, out);
  } else {
    zan_aes_decrypt_round_soft(val, key, out, 1);
  }
}

void zan_hw_aes_decrypt_last(const void *val, const void *key, void *out) {
  if (zan_hw_x86_can(ZAN_HW_REQ_AES)) {
    zan_hw_aes_decrypt_last_ni(val, key, out);
  } else {
    zan_aes_decrypt_round_soft(val, key, out, 0);
  }
}

void zan_hw_aes_keygenassist(const void *val, uint8_t rcon, void *out) {
  if (zan_hw_x86_can(ZAN_HW_REQ_AES)) {
    zan_hw_aes_keygenassist_ni(val, rcon, out);
  } else {
    zan_aes_keygenassist_soft(val, rcon, out);
  }
}

void zan_hw_aes_imc(const void *val, void *out) {
  if (zan_hw_x86_can(ZAN_HW_REQ_AES)) {
    zan_hw_aes_imc_ni(val, out);
  } else {
    zan_aes_imc_soft(val, out);
  }
}

__attribute__((target("sse2"))) void
zan_hw_vec128_xor(const void *a, const void *b, void *out) {
  __m128i va = _mm_loadu_si128((const __m128i *)a);
  __m128i vb = _mm_loadu_si128((const __m128i *)b);
  _mm_storeu_si128((__m128i *)out, _mm_xor_si128(va, vb));
}

__attribute__((target("sse2"))) void zan_hw_vec128_load(const void *addr,
                                                        void *out) {
  __m128i v = _mm_loadu_si128((const __m128i *)addr);
  _mm_storeu_si128((__m128i *)out, v);
}

__attribute__((target("sse2"))) void zan_hw_vec128_store(void *addr,
                                                         const void *val) {
  __m128i v = _mm_loadu_si128((const __m128i *)val);
  _mm_storeu_si128((__m128i *)addr, v);
}

#elif (defined(__aarch64__) || defined(_M_ARM64)) &&                           \
    (defined(__GNUC__) || defined(__clang__))

__attribute__((target("aes"))) static inline void
zan_hw_aes_encrypt_arm_hw(const void *val, const void *key, void *out) {
  uint8x16_t v = vld1q_u8((const uint8_t *)val);
  uint8x16_t k = vld1q_u8((const uint8_t *)key);
  uint8x16_t z = vdupq_n_u8(0);
  uint8x16_t res = veorq_u8(vaesmcq_u8(vaeseq_u8(v, z)), k);
  vst1q_u8((uint8_t *)out, res);
}

__attribute__((target("aes"))) static inline void
zan_hw_aes_encrypt_last_arm_hw(const void *val, const void *key, void *out) {
  uint8x16_t v = vld1q_u8((const uint8_t *)val);
  uint8x16_t k = vld1q_u8((const uint8_t *)key);
  uint8x16_t z = vdupq_n_u8(0);
  uint8x16_t res = veorq_u8(vaeseq_u8(v, z), k);
  vst1q_u8((uint8_t *)out, res);
}

__attribute__((target("aes"))) static inline void
zan_hw_aes_decrypt_arm_hw(const void *val, const void *key, void *out) {
  uint8x16_t v = vld1q_u8((const uint8_t *)val);
  uint8x16_t k = vld1q_u8((const uint8_t *)key);
  uint8x16_t z = vdupq_n_u8(0);
  uint8x16_t res = veorq_u8(vaesimcq_u8(vaesdq_u8(v, z)), k);
  vst1q_u8((uint8_t *)out, res);
}

__attribute__((target("aes"))) static inline void
zan_hw_aes_decrypt_last_arm_hw(const void *val, const void *key, void *out) {
  uint8x16_t v = vld1q_u8((const uint8_t *)val);
  uint8x16_t k = vld1q_u8((const uint8_t *)key);
  uint8x16_t z = vdupq_n_u8(0);
  uint8x16_t res = veorq_u8(vaesdq_u8(v, z), k);
  vst1q_u8((uint8_t *)out, res);
}

__attribute__((target("aes"))) static inline void
zan_hw_aes_imc_arm_hw(const void *val, void *out) {
  uint8x16_t v = vld1q_u8((const uint8_t *)val);
  vst1q_u8((uint8_t *)out, vaesimcq_u8(v));
}

void zan_hw_aes_encrypt(const void *val, const void *key, void *out) {
  if (zan_hw_arm_aes()) {
    zan_hw_aes_encrypt_arm_hw(val, key, out);
  } else {
    zan_aes_encrypt_round_soft(val, key, out, 1);
  }
}

void zan_hw_aes_encrypt_last(const void *val, const void *key, void *out) {
  if (zan_hw_arm_aes()) {
    zan_hw_aes_encrypt_last_arm_hw(val, key, out);
  } else {
    zan_aes_encrypt_round_soft(val, key, out, 0);
  }
}

void zan_hw_aes_decrypt(const void *val, const void *key, void *out) {
  if (zan_hw_arm_aes()) {
    zan_hw_aes_decrypt_arm_hw(val, key, out);
  } else {
    zan_aes_decrypt_round_soft(val, key, out, 1);
  }
}

void zan_hw_aes_decrypt_last(const void *val, const void *key, void *out) {
  if (zan_hw_arm_aes()) {
    zan_hw_aes_decrypt_last_arm_hw(val, key, out);
  } else {
    zan_aes_decrypt_round_soft(val, key, out, 0);
  }
}

void zan_hw_aes_keygenassist(const void *val, uint8_t rcon, void *out) {
  zan_aes_keygenassist_soft(val, rcon, out);
}

void zan_hw_aes_imc(const void *val, void *out) {
  if (zan_hw_arm_aes()) {
    zan_hw_aes_imc_arm_hw(val, out);
  } else {
    zan_aes_imc_soft(val, out);
  }
}

void zan_hw_vec128_xor(const void *a, const void *b, void *out) {
  uint8x16_t va = vld1q_u8((const uint8_t *)a);
  uint8x16_t vb = vld1q_u8((const uint8_t *)b);
  vst1q_u8((uint8_t *)out, veorq_u8(va, vb));
}

void zan_hw_vec128_load(const void *addr, void *out) {
  uint8x16_t v = vld1q_u8((const uint8_t *)addr);
  vst1q_u8((uint8_t *)out, v);
}

void zan_hw_vec128_store(void *addr, const void *val) {
  uint8x16_t v = vld1q_u8((const uint8_t *)val);
  vst1q_u8((uint8_t *)addr, v);
}

#else

void zan_hw_aes_encrypt(const void *val, const void *key, void *out) {
  zan_aes_encrypt_round_soft(val, key, out, 1);
}

void zan_hw_aes_encrypt_last(const void *val, const void *key, void *out) {
  zan_aes_encrypt_round_soft(val, key, out, 0);
}

void zan_hw_aes_decrypt(const void *val, const void *key, void *out) {
  zan_aes_decrypt_round_soft(val, key, out, 1);
}

void zan_hw_aes_decrypt_last(const void *val, const void *key, void *out) {
  zan_aes_decrypt_round_soft(val, key, out, 0);
}

void zan_hw_aes_keygenassist(const void *val, uint8_t rcon, void *out) {
  zan_aes_keygenassist_soft(val, rcon, out);
}

void zan_hw_aes_imc(const void *val, void *out) { zan_aes_imc_soft(val, out); }

void zan_hw_vec128_xor(const void *a, const void *b, void *out) {
  const uint8_t *pa = (const uint8_t *)a;
  const uint8_t *pb = (const uint8_t *)b;
  uint8_t *po = (uint8_t *)out;
  for (int i = 0; i < 16; i++)
    po[i] = pa[i] ^ pb[i];
}

void zan_hw_vec128_load(const void *addr, void *out) { memcpy(out, addr, 16); }

void zan_hw_vec128_store(void *addr, const void *val) { memcpy(addr, val, 16); }

#endif

int64_t zan_hw_ffi_aes_cbc_encrypt(uint64_t dst, uint64_t src, int64_t size,
                                   uint64_t key, int64_t keybits, uint64_t iv) {
  return zan_hw_aes_cbc_encrypt(
      (const uint8_t *)(uintptr_t)src, size, (const uint8_t *)(uintptr_t)key,
      (int)keybits, (const uint8_t *)(uintptr_t)iv, (uint8_t *)(uintptr_t)dst);
}

int64_t zan_hw_ffi_aes_cbc_decrypt(uint64_t dst, uint64_t src, int64_t size,
                                   uint64_t key, int64_t keybits, uint64_t iv) {
  return zan_hw_aes_cbc_decrypt(
      (const uint8_t *)(uintptr_t)src, size, (const uint8_t *)(uintptr_t)key,
      (int)keybits, (const uint8_t *)(uintptr_t)iv, (uint8_t *)(uintptr_t)dst);
}

int64_t zan_hw_ffi_aes_ecb_block(uint64_t key, int64_t keybits, uint64_t in16,
                                 uint64_t out16) {
  return zan_hw_aes_ecb_block((const uint8_t *)(uintptr_t)key, (int)keybits,
                              (const uint8_t *)(uintptr_t)in16,
                              (uint8_t *)(uintptr_t)out16);
}

int64_t zan_hw_ffi_aes_ctr_crypt(uint64_t dst, uint64_t src, int64_t size,
                                 uint64_t key, int64_t keybits,
                                 uint64_t counter) {
  return zan_hw_aes_ctr_crypt(
      (const uint8_t *)(uintptr_t)src, size, (const uint8_t *)(uintptr_t)key,
      (int)keybits, (uint8_t *)(uintptr_t)counter, (uint8_t *)(uintptr_t)dst);
}
