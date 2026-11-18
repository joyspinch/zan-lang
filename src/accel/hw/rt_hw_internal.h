#ifndef ZAN_HW_INTERNAL_H
#define ZAN_HW_INTERNAL_H

/* Shared declarations only: each algorithm is compiled into its own archive
 * member. Keep CPU/KAT state independent of the algorithm implementations. */
#include "../zan_hw_accel.h"
#include <string.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <windows.h>
#endif
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#include <wmmintrin.h>
#include <emmintrin.h>
#include <tmmintrin.h>
#include <smmintrin.h>
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif !defined(_WIN32)
#include <sys/auxv.h>
#endif
#endif

#if (defined(__GNUC__) || defined(__clang__)) && !defined(_WIN32)
#define ZAN_HW_INTERNAL __attribute__((visibility("hidden")))
#else
#define ZAN_HW_INTERNAL
#endif

/* Requirements describe the instructions used by each implementation. */
enum {
    ZAN_HW_SSSE3 = 1u << 0,
    ZAN_HW_SSE41 = 1u << 1,
    ZAN_HW_AES = 1u << 2,
    ZAN_HW_SHA = 1u << 3,
    ZAN_HW_PCLMUL = 1u << 4,
    ZAN_HW_AVX2 = 1u << 5,
    ZAN_HW_VAES = 1u << 6,
    ZAN_HW_REQ_PIXEL = ZAN_HW_SSSE3,
    ZAN_HW_REQ_AES = ZAN_HW_AES | ZAN_HW_SSSE3 | ZAN_HW_SSE41,
    ZAN_HW_REQ_SHA = ZAN_HW_SHA | ZAN_HW_SSSE3 | ZAN_HW_SSE41,
    ZAN_HW_REQ_GHASH = ZAN_HW_PCLMUL | ZAN_HW_SSSE3 | ZAN_HW_SSE41,
    ZAN_HW_REQ_SM4 = ZAN_HW_AES | ZAN_HW_SSSE3,
    ZAN_HW_REQ_SM4_VAES = ZAN_HW_AES | ZAN_HW_SSSE3 | ZAN_HW_AVX2 | ZAN_HW_VAES
};

/* KAT state lives in the lightweight CPU member. Querying it must not create
 * references to any algorithm/KAT implementation and pull those members in. */
enum {
    ZAN_HW_GATE_SHA256, ZAN_HW_GATE_SHA1, ZAN_HW_GATE_SHA512,
    ZAN_HW_GATE_SM3, ZAN_HW_GATE_AES, ZAN_HW_GATE_GHASH,
    ZAN_HW_GATE_CRC32C, ZAN_HW_GATE_CRC32, ZAN_HW_GATE_SM4,
    ZAN_HW_GATE_COUNT
};
extern ZAN_HW_INTERNAL int zan_hw_gate_states[ZAN_HW_GATE_COUNT];
#define g_gate_sha256 zan_hw_gate_states[ZAN_HW_GATE_SHA256]
#define g_gate_sha1   zan_hw_gate_states[ZAN_HW_GATE_SHA1]
#define g_gate_sha512 zan_hw_gate_states[ZAN_HW_GATE_SHA512]
#define g_gate_sm3    zan_hw_gate_states[ZAN_HW_GATE_SM3]
#define g_gate_aes    zan_hw_gate_states[ZAN_HW_GATE_AES]
#define g_gate_ghash  zan_hw_gate_states[ZAN_HW_GATE_GHASH]
#define g_gate_crc32c zan_hw_gate_states[ZAN_HW_GATE_CRC32C]
#define g_gate_crc32  zan_hw_gate_states[ZAN_HW_GATE_CRC32]
#define g_gate_sm4    zan_hw_gate_states[ZAN_HW_GATE_SM4]

ZAN_HW_INTERNAL void zan_hw_init_cpu_features(void);
ZAN_HW_INTERNAL int zan_hw_x86_can(unsigned required);
ZAN_HW_INTERNAL int zan_hw_gate(int *state, int feature, int (*kat)(void));

/* These existing external CPU query symbols remain externally visible. */
int zan_hw_arm_aes(void);
int zan_hw_arm_sha1(void);
int zan_hw_arm_sha2(void);
int zan_hw_arm_pmull(void);
int zan_hw_arm_sha512(void);
int zan_hw_arm_sm3(void);
int zan_hw_arm_sm4(void);
int zan_hw_arm_crc32(void);

static inline int zan_hw_features_cover(unsigned available, unsigned required) {
    return (available & required) == required;
}

/* AES owns the key schedule, KAT and batched CTR kernels reused by GCM. */
ZAN_HW_INTERNAL void zan_aes_expand_key(const uint8_t *key, int keybits,
                                       uint8_t rk[15][16], int *nr_out);
ZAN_HW_INTERNAL int zan_aes_kat(void);
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
__attribute__((target("aes,sse4.1")))
ZAN_HW_INTERNAL void zan_aes_ecb_block_rk(const uint8_t rkb[15][16], int nr,
                                         const uint8_t *in16, uint8_t *out16);
__attribute__((target("aes,sse4.1")))
ZAN_HW_INTERNAL void zan_aes_ctr_ni_4way(const uint8_t rkb[15][16], int nr,
                                       const uint8_t *in, int64_t len,
                                       uint8_t *counter16, uint8_t *out);
#endif

#endif /* ZAN_HW_INTERNAL_H */
