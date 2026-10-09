#ifndef ZAN_RT_HW_ACCEL_H
#define ZAN_RT_HW_ACCEL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 底层系统交互与数据协议契约 */
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

/* 硬件加速原语标准答案测试 (KAT) 门控状态 (1:通过 0:未评估 -1:不支持或失败) */
int zan_hw_kat_state(int id);

/* 硬件加速哈希摘要内核：成功返回 0 并填充 out，无硬件路径时返回 -1 由纯 Zan 兜底 */
int64_t zan_hw_sha256(const uint8_t *data, int64_t len, uint8_t out[32]);
int64_t zan_hw_sha1(const uint8_t *data, int64_t len, uint8_t out[20]);
int64_t zan_hw_sha512(const uint8_t *data, int64_t len, uint8_t out[64]);
int64_t zan_hw_sm3(const uint8_t *data, int64_t len, uint8_t out[32]);

/* SM4 分组密码 CBC 模式 (PKCS#7 填充)：支持 AES-NI/AVX2/ARM-SM4 硬件指令与 T-table 回退 */
int64_t zan_hw_sm4_cbc_encrypt(const uint8_t *in, int64_t len,
                               const uint8_t *key, const uint8_t *iv,
                               uint8_t *out);
int64_t zan_hw_sm4_cbc_decrypt(const uint8_t *in, int64_t len,
                               const uint8_t *key, const uint8_t *iv,
                               uint8_t *out);

/* AES 加密/解密内核 (CBC 模式带 PKCS#7 填充)：采用 AES-NI 或 ARMv8 硬件加速 */
int64_t zan_hw_aes_cbc_encrypt(const uint8_t *in, int64_t len,
                               const uint8_t *key, int keybits,
                               const uint8_t *iv, uint8_t *out);
int64_t zan_hw_aes_cbc_decrypt(const uint8_t *in, int64_t len,
                               const uint8_t *key, int keybits,
                               const uint8_t *iv, uint8_t *out);
/* 底层系统交互与数据协议契约 */
int64_t zan_hw_aes_ecb_block(const uint8_t *key, int keybits,
                             const uint8_t *in16, uint8_t *out16);
/* 底层系统交互与数据协议契约 */
int64_t zan_hw_aes_ctr_crypt(const uint8_t *in, int64_t len,
                             const uint8_t *key, int keybits,
                             uint8_t *counter16, uint8_t *out);
/* 底层系统交互与数据协议契约 */
int64_t zan_hw_ghash_block(const uint8_t *h16, const uint8_t *x16, uint8_t *y16);
/* 底层系统交互与数据协议契约 */
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

/* 底层系统交互与数据协议契约 */
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

/* 底层系统交互与数据协议契约 */
int64_t zan_hw_rsa_mod_pow(const uint8_t *base, int64_t bLen,
                           const uint8_t *exp, int64_t eLen,
                           const uint8_t *mod, int64_t mLen,
                           uint8_t *out);

/* RSA 中国剩余定理 (CRT) 模幂计算：基于 Garner 算法重组结果 */
int64_t zan_hw_rsa_crt(const uint8_t *msg, int64_t mLen,
                       const uint8_t *p, int64_t pLen,
                       const uint8_t *q, int64_t qLen,
                       const uint8_t *dp, int64_t dpLen,
                       const uint8_t *dq, int64_t dqLen,
                       const uint8_t *qinv, int64_t qinvLen,
                       uint8_t *out, int64_t outLen);

/* RFC 7748 X25519 常数时间 Diffie-Hellman 标量乘法内核 */
int64_t zan_hw_x25519(const uint8_t *scalar, const uint8_t *point, uint8_t *out);

/* CRC-32C 硬件加速计算：基于 SSE4.2 crc32 / ARMv8 CRC 指令 */
int64_t zan_hw_crc32c_update(uint32_t crc, const uint8_t *p, int64_t len);

/* 底层系统交互与数据协议契约 */
void zan_hw_aes_encrypt(const void *val, const void *key, void *out);
void zan_hw_aes_encrypt_last(const void *val, const void *key, void *out);
void zan_hw_aes_decrypt(const void *val, const void *key, void *out);
void zan_hw_aes_decrypt_last(const void *val, const void *key, void *out);
void zan_hw_aes_keygenassist(const void *val, uint8_t rcon, void *out);
void zan_hw_aes_imc(const void *val, void *out);
void zan_hw_vec128_xor(const void *a, const void *b, void *out);
void zan_hw_vec128_load(const void *addr, void *out);
void zan_hw_vec128_store(void *addr, const void *val);

/* 底层系统交互与数据协议契约 */
int64_t zan_hw_base64_encode(const uint8_t *src, int64_t len, char *dst);
int64_t zan_hw_base64_decode(const char *src, int64_t len, uint8_t *dst);

/* 底层系统交互与数据协议契约 */
int64_t zan_hw_json_skip_whitespace(const uint8_t *buf, int64_t pos, int64_t len);
int64_t zan_hw_json_scan_string(const uint8_t *buf, int64_t pos, int64_t len);

/* 核心系统底层抽象与内存语义契约 */
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
