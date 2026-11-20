/* Compile as one TU to inspect private predicates and force missing features
 * in the test only. No CPU override API is exported by production code. */
#include "../src/accel/zan_hw_accel.c"
#include <stdio.h>

static int test_requirements(void) {
    const unsigned requirements[] = {
        ZAN_HW_REQ_PIXEL, ZAN_HW_REQ_AES, ZAN_HW_REQ_SHA,
        ZAN_HW_REQ_GHASH, ZAN_HW_REQ_SM4, ZAN_HW_REQ_SM4_VAES
    };
    const unsigned all = ZAN_HW_SSSE3 | ZAN_HW_SSE41 | ZAN_HW_AES |
        ZAN_HW_SHA | ZAN_HW_PCLMUL | ZAN_HW_AVX2 | ZAN_HW_VAES;
    for (unsigned i = 0; i < sizeof(requirements) / sizeof(requirements[0]); ++i) {
        unsigned required = requirements[i];
        if (!zan_hw_features_cover(all, required)) return 1;
        for (unsigned bit = 1; bit <= ZAN_HW_VAES; bit <<= 1) {
            int expected = (required & bit) == 0;
            if (zan_hw_features_cover(all & ~bit, required) != expected) return 2;
        }
        if (zan_hw_features_cover(0, required)) return 3;
    }
    return 0;
}

static int test_pixel_fallback(void) {
    uint32_t src[11], dst[11], expected[11];
    for (unsigned i = 0; i < 11; ++i) {
        src[i] = 0x80402010u + i * 0x01020304u;
        expected[i] = (src[i] & 0xff00ff00u) |
            ((src[i] & 0x00ff0000u) >> 16) | ((src[i] & 0xffu) << 16);
        dst[i] = 0xdeadbeefu;
    }
    /* Initialize normally first, then mask private cached features. This test
     * must run safely even on hosts that have none of these extensions. */
    zan_hw_init_cpu_features();
    g_has_ssse3 = 0;
    if (zan_hw_x86_can(ZAN_HW_REQ_PIXEL)) return 4;
    zan_hw_pixel_swap_rb((uint8_t *)dst, (const uint8_t *)src, 11);
    if (memcmp(dst, expected, sizeof(dst)) != 0) return 5;
    zan_hw_pixel_swap_rb((uint8_t *)src, (const uint8_t *)src, 11);
    if (memcmp(src, expected, sizeof(src)) != 0) return 6;
    zan_hw_pixel_swap_rb(NULL, NULL, 0);
    return 0;
}

static int test_crypto_dispatch(void) {
    uint8_t key[32] = {0}, iv[16] = {0}, input[16] = {0}, output[32] = {0};
    /* Model a VM exposing crypto bits while masking its helper SIMD bits.
     * These public entry points must return/fall back before running a KAT. */
    g_has_aesni = g_has_shani = g_has_pclmul = 1;
    g_has_sse41 = g_has_ssse3 = 0;
    g_gate_aes = g_gate_sha1 = g_gate_sha256 = g_gate_ghash = g_gate_sm4 = 0;
    if (zan_hw_x86_can(ZAN_HW_REQ_AES) || zan_hw_x86_can(ZAN_HW_REQ_SHA) ||
        zan_hw_x86_can(ZAN_HW_REQ_GHASH) || zan_hw_x86_can(ZAN_HW_REQ_SM4)) return 7;
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    if (zan_hw_aes_cbc_encrypt(input, 16, key, 128, iv, output) != -1) return 8;
    if (g_gate_aes == 1) return 9;
    {
        uint8_t hash[32], y[16] = {0};
        /* The accelerator returns -1 so its Zan caller can run software;
         * a hardware KAT must not run just because the crypto bit is set. */
        if (zan_hw_sha256(input, 0, hash) != -1) return 10;
        if (zan_hw_ghash_block(key, input, y) != -1) return 11;
        if (g_gate_sha256 == 1 || g_gate_ghash == 1) return 12;
    }
#endif
    return 0;
}

int main(void) {
    int rc = test_requirements();
    if (!rc) rc = test_pixel_fallback();
    if (!rc) rc = test_crypto_dispatch();
    if (rc) fprintf(stderr, "native CPU gate regression failed: %d\n", rc);
    return rc;
}
