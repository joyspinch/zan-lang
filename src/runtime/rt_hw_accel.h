#ifndef ZAN_RT_HW_ACCEL_H
#define ZAN_RT_HW_ACCEL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CPU feature detection (cached after first query) */
int zan_hw_has_popcnt(void);
int zan_hw_has_lzcnt(void);
int zan_hw_has_sse42(void);
int zan_hw_has_avx2(void);
int zan_hw_has_aesni(void);
int zan_hw_has_vaes(void);
int zan_hw_has_neon(void);
int zan_hw_has_shani(void);
int zan_hw_has_pclmul(void);
int zan_cpu_feature(int id);

/* Per-primitive known-answer-test gate state (conformance can assert that a
 * present feature actually passed its KAT instead of silently degrading):
 *   1 = KAT passed (hardware path live), 0 = not yet evaluated,
 *  -1 = feature absent or KAT failed (hardware path disabled).
 * Ids match zan_cpu_feature. */
int zan_hw_kat_state(int id);

/* ---------------------------------------------------------------------
 * Digest kernels. Each returns 0 and fills `out` on success, or -1 when
 * no hardware path is available (the caller falls back to the pure-Zan
 * implementation). No portable C implementation lives in the runtime.
 * --------------------------------------------------------------------- */
int64_t zan_hw_sha256(const uint8_t *data, int64_t len, uint8_t out[32]);
int64_t zan_hw_sha1(const uint8_t *data, int64_t len, uint8_t out[20]);
int64_t zan_hw_sha512(const uint8_t *data, int64_t len, uint8_t out[64]);
int64_t zan_hw_sm3(const uint8_t *data, int64_t len, uint8_t out[32]);

/* SM4 Block Cipher CBC mode with PKCS#7 padding (GB/T 32907-2016).
 * Returns produced/plaintext length or -1. On x86_64, uses AES-NI affine
 * decomposition and AVX2+VAES multi-block pipelined vector instructions;
 * on ARM with FEAT_SM4 uses sm4e instructions; falls back to the T-table
 * driver when hardware extensions are absent. */
int64_t zan_hw_sm4_cbc_encrypt(const uint8_t *in, int64_t len,
                               const uint8_t *key, const uint8_t *iv,
                               uint8_t *out);
int64_t zan_hw_sm4_cbc_decrypt(const uint8_t *in, int64_t len,
                               const uint8_t *key, const uint8_t *iv,
                               uint8_t *out);

/* AES (FIPS-197) with 128/192/256-bit keys. CBC applies PKCS#7 padding and
 * returns the produced/plaintext length (-1 on refusal). Every path is a
 * thin hardware kernel (AES-NI on x86, FEAT_AES on ARM); the pure-Zan
 * implementation in the stdlib is the software fallback. */
int64_t zan_hw_aes_cbc_encrypt(const uint8_t *in, int64_t len,
                               const uint8_t *key, int keybits,
                               const uint8_t *iv, uint8_t *out);
int64_t zan_hw_aes_cbc_decrypt(const uint8_t *in, int64_t len,
                               const uint8_t *key, int keybits,
                               const uint8_t *iv, uint8_t *out);
/* Single-block ECB: 16 bytes in -> 16 bytes out. 0 or -1. */
int64_t zan_hw_aes_ecb_block(const uint8_t *key, int keybits,
                             const uint8_t *in16, uint8_t *out16);
/* CTR keystream XOR over len bytes; counter16 is the 128-bit big-endian
 * counter, advanced in place past the consumed blocks. Returns len or -1. */
int64_t zan_hw_aes_ctr_crypt(const uint8_t *in, int64_t len,
                             const uint8_t *key, int keybits,
                             uint8_t *counter16, uint8_t *out);
/* GHASH universal hash step (GCM, NIST SP 800-38D): y = (y ^ x) * h in
 * GF(2^128). All three point at 16-byte blocks. 0 or -1. */
int64_t zan_hw_ghash_block(const uint8_t *h16, const uint8_t *x16, uint8_t *y16);
/* Streamed GHASH: updates y with len bytes of data, zero-padding final block if needed. */
int64_t zan_hw_ghash_update(const uint8_t *h16, const uint8_t *data, int64_t len, uint8_t *y16);

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;      /* 0x47434D31 ('GCM1') */
    int32_t keybits;
    int32_t nr;
    uint8_t h16[16];
    uint8_t rkb[15][16];
    uint8_t h_bswap[16];
    uint8_t h_powers[8][16]; /* H^1, H^2, H^3, H^4, H^5, H^6, H^7, H^8 */
} zan_gcm_ctx_t;
#pragma pack(pop)

int64_t zan_hw_aes_gcm_init(uint8_t *ctxBuf, int64_t ctxLen, const uint8_t *key, int keybits);
int64_t zan_hw_aes_gcm_encrypt_ctx(const uint8_t *ctxBuf,
                                   const uint8_t *iv12,
                                   const uint8_t *aad, int64_t aadLen,
                                   const uint8_t *in, int64_t inLen,
                                   uint8_t *out, uint8_t *tag16);
int64_t zan_hw_aes_gcm_decrypt_ctx(const uint8_t *ctxBuf,
                                   const uint8_t *iv12,
                                   const uint8_t *aad, int64_t aadLen,
                                   const uint8_t *in, int64_t inLen,
                                   const uint8_t *tag16, uint8_t *out);

/* High-throughput integrated AES-GCM (NIST SP 800-38D, RFC 5288/8446) with hardware AES-NI & PCLMUL.
 * Returns inLen on success, -1 on unsupported/invalid args, or -2 on tag mismatch (decrypt). */
int64_t zan_hw_aes_gcm_encrypt(const uint8_t *key, int keybits,
                               const uint8_t *iv12,
                               const uint8_t *aad, int64_t aadLen,
                               const uint8_t *in, int64_t inLen,
                               uint8_t *out, uint8_t *tag16);
int64_t zan_hw_aes_gcm_decrypt(const uint8_t *key, int keybits,
                               const uint8_t *iv12,
                               const uint8_t *aad, int64_t aadLen,
                               const uint8_t *in, int64_t inLen,
                               const uint8_t *tag16, uint8_t *out);

/* Modular exponentiation: base^exp mod n for RSA/DH (up to 4096-bit odd modulus).
 * All inputs and outputs are big-endian byte buffers. Returns 0 on success, -1 otherwise. */
int64_t zan_hw_rsa_mod_pow(const uint8_t *base, int64_t bLen,
                           const uint8_t *exp, int64_t eLen,
                           const uint8_t *mod, int64_t mLen,
                           uint8_t *out);

/* RSA Chinese Remainder Theorem (CRT) modular exponentiation:
 * evaluates s1 = m^dp mod p, s2 = m^dq mod q, and recombines via Garner's algorithm:
 * h = (s1 - s2) * qinv mod p, s = s2 + h * q.
 * Returns 0 on success, -1 otherwise. */
int64_t zan_hw_rsa_crt(const uint8_t *msg, int64_t mLen,
                       const uint8_t *p, int64_t pLen,
                       const uint8_t *q, int64_t qLen,
                       const uint8_t *dp, int64_t dpLen,
                       const uint8_t *dq, int64_t dqLen,
                       const uint8_t *qinv, int64_t qinvLen,
                       uint8_t *out, int64_t outLen);

/* RFC 7748 X25519 constant-time Diffie-Hellman scalar multiplication:
 * computes scalar * point -> out (all 32 bytes little-endian).
 * Clamping of scalar is performed internally. Returns 0 on success. */
int64_t zan_hw_x25519(const uint8_t *scalar, const uint8_t *point, uint8_t *out);

/* CRC-32C (Castagnoli, poly 0x82F63B78) continuation: returns the updated
 * CRC of `crc` extended with len bytes at p, or -1 when no hardware path
 * exists (SSE4.2 crc32 / ARMv8 CRC instructions). */
int64_t zan_hw_crc32c_update(uint32_t crc, const uint8_t *p, int64_t len);

/* Vector128 / AES-NI single-cycle primitive helpers */
void zan_hw_aes_encrypt(const void *val, const void *key, void *out);
void zan_hw_aes_encrypt_last(const void *val, const void *key, void *out);
void zan_hw_aes_decrypt(const void *val, const void *key, void *out);
void zan_hw_aes_decrypt_last(const void *val, const void *key, void *out);
void zan_hw_aes_keygenassist(const void *val, uint8_t rcon, void *out);
void zan_hw_aes_imc(const void *val, void *out);
void zan_hw_vec128_xor(const void *a, const void *b, void *out);
void zan_hw_vec128_load(const void *addr, void *out);
void zan_hw_vec128_store(void *addr, const void *val);

/* Base64 High-Throughput SIMD / Pipelined Encoders (RFC 4648) */
int64_t zan_hw_base64_encode(const uint8_t *src, int64_t len, char *dst);
int64_t zan_hw_base64_decode(const char *src, int64_t len, uint8_t *dst);

/* JSON High-Throughput SIMD Structural Scanners */
int64_t zan_hw_json_skip_whitespace(const uint8_t *buf, int64_t pos, int64_t len);
int64_t zan_hw_json_scan_string(const uint8_t *buf, int64_t pos, int64_t len);

/* PixelOps SIMD acceleration */
void zan_hw_pixel_blend_over(uint8_t *dst, const uint8_t *src, int64_t count);
void zan_hw_pixel_swap_rb(uint8_t *dst, const uint8_t *src, int64_t count);
void zan_hw_pixel_fill_rect(uint8_t *dst, int64_t stride, int64_t x, int64_t y,
                            int64_t w, int64_t h, uint32_t color);
void zan_hw_pixel_resample_bilinear_row(uint8_t *dst, const uint8_t *src0,
                                       const uint8_t *src1, const int32_t *x_idx,
                                       const int32_t *x_wt, int32_t wy, int64_t width);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_RT_HW_ACCEL_H */
