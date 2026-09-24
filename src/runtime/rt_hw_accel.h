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
int zan_hw_has_neon(void);
int zan_hw_has_shani(void);
int zan_cpu_feature(int id);

/* SHA-256 hardware accelerated / zero-stack-overflow streaming */
void zan_hw_sha256(const uint8_t *data, int64_t len, uint8_t out[32]);

/* AES-128 CBC hardware accelerated */
int64_t zan_hw_aes128_cbc_encrypt(const uint8_t *in, int64_t len,
                                  const uint8_t *key, const uint8_t *iv,
                                  uint8_t *out);
int64_t zan_hw_aes128_cbc_decrypt(const uint8_t *in, int64_t len,
                                  const uint8_t *key, const uint8_t *iv,
                                  uint8_t *out);

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
