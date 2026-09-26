/* Zan Hardware Acceleration Engine & Cryptographic / SIMD Drivers
 * Implements AES-NI, SHA-NI, AVX2 / SSE SIMD kernels for maximum throughput.
 */

#include "rt_hw_accel.h"
#include "rt_hw_accel_sm4_tables.h"
#include <string.h>
#include <stdlib.h>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  #include <immintrin.h>
  #include <wmmintrin.h>
  #include <emmintrin.h>
  #include <tmmintrin.h>
  #include <smmintrin.h>
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
  #if defined(_WIN32)
    /* windows.h normally arrives from the including TU (rt_timer.c); include
     * it here too so the unit compiles standalone. */
    #include <windows.h>
  #elif defined(__APPLE__)
    #include <sys/sysctl.h>
  #else
    /* Linux / Android (bionic, API 21+) / OpenHarmony (musl) all ship
     * getauxval; iOS goes through the __APPLE__ branch above. */
    #include <sys/auxv.h>
  #endif
#endif

/* ===== 1. CPU Feature Detection ===== */
static int g_cpuid_inited = 0;
static int g_has_popcnt   = 0;
static int g_has_lzcnt    = 0;
static int g_has_sse42    = 0;
static int g_has_avx2     = 0;
static int g_has_aesni    = 0;
static int g_has_neon     = 0;
static int g_has_shani    = 0;
static int g_has_pclmul   = 0;

/* ARMv8 crypto extension availability (aarch64 only). Split per algorithm:
 * FEAT_AES/SHA1/SHA2/PMULL are baseline "crypto"; FEAT_SHA512/SM3/SM4 and
 * FEAT_CRC32 are later optional extensions that must each be probed. */
static int g_a_hw_aes    = 0;
static int g_a_hw_sha1   = 0;
static int g_a_hw_sha2   = 0;
static int g_a_hw_pmull  = 0;
static int g_a_hw_sha512 = 0;
static int g_a_hw_sm3    = 0;
static int g_a_hw_sm4    = 0;
static int g_a_hw_crc32  = 0;

#if defined(__aarch64__) || defined(_M_ARM64)
#if !defined(_WIN32) && !defined(__APPLE__)
/* Linux HWCAP bits (uapi/asm/hwcap.h). */
#define ZAN_HWCAP_FP      (1u << 0)
#define ZAN_HWCAP_ASIMD   (1u << 1)
#define ZAN_HWCAP_AES     (1u << 3)
#define ZAN_HWCAP_PMULL   (1u << 4)
#define ZAN_HWCAP_SHA1    (1u << 5)
#define ZAN_HWCAP_SHA2    (1u << 6)
#define ZAN_HWCAP_CRC32   (1u << 7)
#define ZAN_HWCAP_SHA3    (1u << 17)
#define ZAN_HWCAP_SM3     (1u << 18)
#define ZAN_HWCAP_SM4     (1u << 19)
#define ZAN_HWCAP_SHA512  (1u << 21)
#endif
static int zan_sysctl_i(const char *key) {
#if defined(__APPLE__)
    int v = 0;
    size_t n = sizeof(v);
    if (sysctlbyname(key, &v, &n, NULL, 0) != 0) return 0;
    return v != 0;
#else
    (void)key;
    return 0;
#endif
}
static void zan_arm_probe(void) {
#if defined(_WIN32)
    /* IsProcessorFeaturePresent bundles FEAT_AES+SHA1+SHA2+PMULL as the
     * "v8 crypto" set; Windows exposes no per-extension flag for
     * FEAT_SHA512/SM3/SM4, so those fall back to portable C there. */
    int crypto = IsProcessorFeaturePresent(75 /* PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE */);
    g_a_hw_aes = g_a_hw_sha1 = g_a_hw_sha2 = g_a_hw_pmull = crypto;
    g_a_hw_crc32 = IsProcessorFeaturePresent(76 /* PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE */);
#elif defined(__APPLE__)
    int crypto = zan_sysctl_i("hw.optional.armv8_crypto");
    g_a_hw_aes = g_a_hw_sha1 = g_a_hw_sha2 = g_a_hw_pmull = crypto;
    g_a_hw_crc32  = zan_sysctl_i("hw.optional.armv8_crc32");
    g_a_hw_sha512 = zan_sysctl_i("hw.optional.armv8_2_sha512");
    g_a_hw_sm3    = zan_sysctl_i("hw.optional.arm.FEAT_SM3");
    g_a_hw_sm4    = zan_sysctl_i("hw.optional.arm.FEAT_SM4");
#else
    unsigned long hw = getauxval(16 /* AT_HWCAP */);
    g_a_hw_aes    = (hw & ZAN_HWCAP_AES)    != 0;
    g_a_hw_sha1   = (hw & ZAN_HWCAP_SHA1)   != 0;
    g_a_hw_sha2   = (hw & ZAN_HWCAP_SHA2)   != 0;
    g_a_hw_pmull  = (hw & ZAN_HWCAP_PMULL)  != 0;
    g_a_hw_crc32  = (hw & ZAN_HWCAP_CRC32)  != 0;
    g_a_hw_sha512 = (hw & ZAN_HWCAP_SHA512) != 0;
    g_a_hw_sm3    = (hw & ZAN_HWCAP_SM3)    != 0;
    g_a_hw_sm4    = (hw & ZAN_HWCAP_SM4)    != 0;
#endif
}
#endif /* aarch64 */

/* ZAN_NO_HWACCEL=1 forces every dispatch onto the portable C path. Conformance
 * tests run each case twice (with and without the variable) to prove the
 * hardware path and the reference path agree byte for byte. */
static int zan_hw_soft_forced(void) {
    const char *e = getenv("ZAN_NO_HWACCEL");
    return e != NULL && e[0] != '\0' && e[0] != '0';
}

static void zan_hw_init_cpu_features(void) {
    if (g_cpuid_inited) return;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    uint32_t eax, ebx, ecx, edx;
    // EAX=1: Features
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));
    g_has_sse42   = (ecx & (1u << 20)) != 0;
    g_has_popcnt  = (ecx & (1u << 23)) != 0;
    g_has_aesni   = (ecx & (1u << 25)) != 0;
    g_has_pclmul  = (ecx & (1u << 1)) != 0;

    // EAX=7, ECX=0: Extended Features
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
    g_has_avx2  = (ebx & (1u << 5)) != 0;
    g_has_shani = (ebx & (1u << 29)) != 0;

    // EAX=0x80000001: LZCNT
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000001u), "c"(0));
    g_has_lzcnt = (ecx & (1u << 5)) != 0;
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int info[4];
    __cpuid(info, 1);
    g_has_sse42   = (info[2] & (1 << 20)) != 0;
    g_has_popcnt  = (info[2] & (1 << 23)) != 0;
    g_has_aesni   = (info[2] & (1 << 25)) != 0;
    g_has_pclmul  = (info[2] & (1 << 1)) != 0;

    __cpuidex(info, 7, 0);
    g_has_avx2  = (info[1] & (1 << 5)) != 0;
    g_has_shani = (info[1] & (1 << 29)) != 0;

    __cpuid(info, 0x80000001);
    g_has_lzcnt = (info[2] & (1 << 5)) != 0;
#elif defined(__aarch64__) || defined(_M_ARM64)
    g_has_neon = 1;
    zan_arm_probe();
#endif
    if (zan_hw_soft_forced()) {
        g_has_sse42 = g_has_popcnt = g_has_lzcnt = 0;
        g_has_avx2 = g_has_aesni = g_has_shani = g_has_pclmul = 0;
#if defined(__aarch64__) || defined(_M_ARM64)
        g_a_hw_aes = g_a_hw_sha1 = g_a_hw_sha2 = g_a_hw_pmull = 0;
        g_a_hw_sha512 = g_a_hw_sm3 = g_a_hw_sm4 = g_a_hw_crc32 = 0;
#endif
    }
    g_cpuid_inited = 1;
}

/* KAT gate for hardware paths: state is tri-state per primitive
 * (0 = untested, 1 = known-answer test passed, -1 = feature missing or KAT
 * failed -> permanently software). Every hardware kernel is checked once,
 * before its first real use, against published test vectors (FIPS-197,
 * SP 800-38A/38D, FIPS 180-4, GB/T 32905/32907, RFC 4960), so a defective
 * instruction path can never ship wrong bytes: it degrades to the pure-Zan
 * implementation in the stdlib. The registry below is what conformance
 * asserts against: a present feature must have KAT state 1, never -1. */
static int g_gate_sha256 = 0;
static int g_gate_sha1   = 0;
static int g_gate_sha512 = 0;
static int g_gate_sm3    = 0;
static int g_gate_aes    = 0;
static int g_gate_ghash  = 0;
static int g_gate_crc32c = 0;
static int g_gate_sm4    = 0;

static int zan_hw_gate(int *state, int feature, int (*kat)(void)) {
    if (*state == 0) {
        if (!feature) { *state = -1; return 0; }
        *state = kat() ? 1 : -1;
    }
    return *state == 1;
}

int zan_hw_kat_state(int id) {
    zan_hw_init_cpu_features();
    switch (id) {
        case 5: case 9:  return g_gate_aes;
        case 7: case 10: return g_gate_sha1;
        case 11:         return g_gate_sha256;
        case 12:         return g_gate_ghash;
        case 13:         return g_gate_sha512;
        case 14:         return g_gate_sm3;
        case 16:         return g_gate_crc32c;
        case 15:         return g_gate_sm4;
        default:         return -1;
    }
}

int zan_hw_has_popcnt(void) { zan_hw_init_cpu_features(); return g_has_popcnt; }
int zan_hw_has_lzcnt(void)  { zan_hw_init_cpu_features(); return g_has_lzcnt; }
int zan_hw_has_sse42(void)  { zan_hw_init_cpu_features(); return g_has_sse42; }
int zan_hw_has_avx2(void)   { zan_hw_init_cpu_features(); return g_has_avx2; }
int zan_hw_has_aesni(void)  { zan_hw_init_cpu_features(); return g_has_aesni; }
int zan_hw_has_neon(void)   { zan_hw_init_cpu_features(); return g_has_neon; }
int zan_hw_has_shani(void)  { zan_hw_init_cpu_features(); return g_has_shani; }
int zan_hw_has_pclmul(void) { zan_hw_init_cpu_features(); return g_has_pclmul; }

int zan_hw_arm_aes(void)    { zan_hw_init_cpu_features(); return g_a_hw_aes; }
int zan_hw_arm_sha1(void)   { zan_hw_init_cpu_features(); return g_a_hw_sha1; }
int zan_hw_arm_sha2(void)   { zan_hw_init_cpu_features(); return g_a_hw_sha2; }
int zan_hw_arm_pmull(void)  { zan_hw_init_cpu_features(); return g_a_hw_pmull; }
int zan_hw_arm_sha512(void) { zan_hw_init_cpu_features(); return g_a_hw_sha512; }
int zan_hw_arm_sm3(void)    { zan_hw_init_cpu_features(); return g_a_hw_sm3; }
int zan_hw_arm_sm4(void)    { zan_hw_init_cpu_features(); return g_a_hw_sm4; }
int zan_hw_arm_crc32(void)  { zan_hw_init_cpu_features(); return g_a_hw_crc32; }

int zan_cpu_feature(int id) {
    switch (id) {
        case 1: return zan_hw_has_popcnt();
        case 2: return zan_hw_has_lzcnt();
        case 3: return zan_hw_has_sse42();
        case 4: return zan_hw_has_avx2();
        case 5: return zan_hw_has_aesni();
        case 6: return zan_hw_has_neon();
        case 7: return zan_hw_has_shani();
        case 8: return zan_hw_has_pclmul();
        case 9: return zan_hw_arm_aes();
        case 10: return zan_hw_arm_sha1();
        case 11: return zan_hw_arm_sha2();
        case 12: return zan_hw_arm_pmull();
        case 13: return zan_hw_arm_sha512();
        case 14: return zan_hw_arm_sm3();
        case 15: return zan_hw_arm_sm4();
        case 16: return zan_hw_arm_crc32();
        default: return 0;
    }
}

/* ===== 2. SHA-256 Hardware Kernel & Streaming Driver ===== */

#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))

#if defined(_WIN32)
#define ZAN_SHA256_DIRECTIVE ".def zan_sha256_transform_ni; .scl 2; .type 32; .endef\n"
#define ZAN_SHA256_PROLOGUE \
  "    mov    rdi, rcx\n" \
  "    mov    rsi, rdx\n" \
  "    mov    rdx, r8\n"
#define ZAN_SHA256_RODATA ".section .rdata,\"dr\"\n"
#elif defined(__APPLE__)
#define ZAN_SHA256_DIRECTIVE ""
#define ZAN_SHA256_PROLOGUE ""
#define ZAN_SHA256_RODATA ".section __TEXT,__const\n"
#else
#define ZAN_SHA256_DIRECTIVE ".type zan_sha256_transform_ni, @function\n"
#define ZAN_SHA256_PROLOGUE ""
#define ZAN_SHA256_RODATA ".section .rodata\n"
#endif

__asm__(
  ".intel_syntax noprefix\n"
  ".text\n"
  ".globl zan_sha256_transform_ni\n"
  ZAN_SHA256_DIRECTIVE
  "zan_sha256_transform_ni:\n"
  "    push   rdi\n"
  "    push   rsi\n"
  "    push   rbx\n"
  "    push   rbp\n"
  ZAN_SHA256_PROLOGUE
  "\n"
  "    sub    rsp, 0x68\n"
  "    movups [rsp+0x00], xmm6\n"
  "    movups [rsp+0x10], xmm7\n"
  "    movups [rsp+0x20], xmm8\n"
  "    movups [rsp+0x30], xmm9\n"
  "    movups [rsp+0x40], xmm10\n"
  "\n"
  "    lea    rcx, [rip + K256 + 0x80]\n"
  "    movdqu xmm1, [rdi]\n"
  "    movdqu xmm2, [rdi+0x10]\n"
  "    movdqu xmm7, [rcx + 0x180]\n"
  "\n"
  "    pshufd xmm0, xmm1, 0x1b\n"
  "    pshufd xmm1, xmm1, 0xb1\n"
  "    pshufd xmm2, xmm2, 0x1b\n"
  "    movdqa xmm8, xmm7\n"
  "    palignr xmm1, xmm2, 0x8\n"
  "    punpcklqdq xmm2, xmm0\n"
  "    jmp    .Lloop\n"
  "\n"
  /* .p2align, not .align: the operand is always a power-of-two exponent
   * for every assembler, while plain .align switches meaning -- ELF/x86
   * takes bytes, Mach-O takes an exponent (.align 64 = align 2^64). */
  ".p2align 4\n"
  ".Lloop:\n"
  "    movdqu xmm3, [rsi]\n"
  "    movdqu xmm4, [rsi+0x10]\n"
  "    movdqu xmm5, [rsi+0x20]\n"
  "    pshufb xmm3, xmm7\n"
  "    movdqu xmm6, [rsi+0x30]\n"
  "    movdqa xmm0, [rcx-0x80]\n"
  "    paddd  xmm0, xmm3\n"
  "    pshufb xmm4, xmm7\n"
  "    movdqa xmm10, xmm2\n"
  "    sha256rnds2 xmm2, xmm1, xmm0\n"
  "    pshufd xmm0, xmm0, 0xe\n"
  "    movdqa xmm9, xmm1\n"
  "    sha256rnds2 xmm1, xmm2, xmm0\n"
  "\n"
  "    movdqa xmm0, [rcx-0x60]\n"
  "    paddd  xmm0, xmm4\n"
  "    pshufb xmm5, xmm7\n"
  "    sha256rnds2 xmm2, xmm1, xmm0\n"
  "    pshufd xmm0, xmm0, 0xe\n"
  "    lea    rsi, [rsi+0x40]\n"
  "    sha256msg1 xmm3, xmm4\n"
  "    sha256rnds2 xmm1, xmm2, xmm0\n"
  "\n"
  "    movdqa xmm0, [rcx-0x40]\n"
  "    paddd  xmm0, xmm5\n"
  "    pshufb xmm6, xmm7\n"
  "    sha256rnds2 xmm2, xmm1, xmm0\n"
  "    pshufd xmm0, xmm0, 0xe\n"
  "    movdqa xmm7, xmm6\n"
  "    palignr xmm7, xmm5, 0x4\n"
  "    paddd  xmm3, xmm7\n"
  "    sha256msg1 xmm4, xmm5\n"
  "    sha256rnds2 xmm1, xmm2, xmm0\n"
  "\n"
  "    movdqa xmm0, [rcx-0x20]\n"
  "    paddd  xmm0, xmm6\n"
  "    sha256msg2 xmm3, xmm6\n"
  "    sha256rnds2 xmm2, xmm1, xmm0\n"
  "    pshufd xmm0, xmm0, 0xe\n"
  "    movdqa xmm7, xmm3\n"
  "    palignr xmm7, xmm6, 0x4\n"
  "    paddd  xmm4, xmm7\n"
  "    sha256msg1 xmm5, xmm6\n"
  "    sha256rnds2 xmm1, xmm2, xmm0\n"
  "\n"
  "    movdqa xmm0, [rcx]\n"
  "    paddd  xmm0, xmm3\n"
  "    sha256msg2 xmm4, xmm3\n"
  "    sha256rnds2 xmm2, xmm1, xmm0\n"
  "    pshufd xmm0, xmm0, 0xe\n"
  "    movdqa xmm7, xmm4\n"
  "    palignr xmm7, xmm3, 0x4\n"
  "    paddd  xmm5, xmm7\n"
  "    sha256msg1 xmm6, xmm3\n"
  "    sha256rnds2 xmm1, xmm2, xmm0\n"
  "\n"
  "    movdqa xmm0, [rcx+0x20]\n"
  "    paddd  xmm0, xmm4\n"
  "    sha256msg2 xmm5, xmm4\n"
  "    sha256rnds2 xmm2, xmm1, xmm0\n"
  "    pshufd xmm0, xmm0, 0xe\n"
  "    movdqa xmm7, xmm5\n"
  "    palignr xmm7, xmm4, 0x4\n"
  "    paddd  xmm6, xmm7\n"
  "    sha256msg1 xmm3, xmm4\n"
  "    sha256rnds2 xmm1, xmm2, xmm0\n"
  "\n"
  "    movdqa xmm0, [rcx+0x40]\n"
  "    paddd  xmm0, xmm5\n"
  "    sha256msg2 xmm6, xmm5\n"
  "    sha256rnds2 xmm2, xmm1, xmm0\n"
  "    pshufd xmm0, xmm0, 0xe\n"
  "    movdqa xmm7, xmm6\n"
  "    palignr xmm7, xmm5, 0x4\n"
  "    paddd  xmm3, xmm7\n"
  "    sha256msg1 xmm4, xmm5\n"
  "    sha256rnds2 xmm1, xmm2, xmm0\n"
  "\n"
  "    movdqa xmm0, [rcx+0x60]\n"
  "    paddd  xmm0, xmm6\n"
  "    sha256msg2 xmm3, xmm6\n"
  "    sha256rnds2 xmm2, xmm1, xmm0\n"
  "    pshufd xmm0, xmm0, 0xe\n"
  "    movdqa xmm7, xmm3\n"
  "    palignr xmm7, xmm6, 0x4\n"
  "    paddd  xmm4, xmm7\n"
  "    sha256msg1 xmm5, xmm6\n"
  "    sha256rnds2 xmm1, xmm2, xmm0\n"
  "\n"
  "    movdqa xmm0, [rcx+0x80]\n"
  "    paddd  xmm0, xmm3\n"
  "    sha256msg2 xmm4, xmm3\n"
  "    sha256rnds2 xmm2, xmm1, xmm0\n"
  "    pshufd xmm0, xmm0, 0xe\n"
  "    movdqa xmm7, xmm4\n"
  "    palignr xmm7, xmm3, 0x4\n"
  "    paddd  xmm5, xmm7\n"
  "    sha256msg1 xmm6, xmm3\n"
  "    sha256rnds2 xmm1, xmm2, xmm0\n"
  "\n"
  "    movdqa xmm0, [rcx+0xa0]\n"
  "    paddd  xmm0, xmm4\n"
  "    sha256msg2 xmm5, xmm4\n"
  "    sha256rnds2 xmm2, xmm1, xmm0\n"
  "    pshufd xmm0, xmm0, 0xe\n"
  "    movdqa xmm7, xmm5\n"
  "    palignr xmm7, xmm4, 0x4\n"
  "    paddd  xmm6, xmm7\n"
  "    sha256msg1 xmm3, xmm4\n"
  "    sha256rnds2 xmm1, xmm2, xmm0\n"
  "\n"
  "    movdqa xmm0, [rcx+0xc0]\n"
  "    paddd  xmm0, xmm5\n"
  "    sha256msg2 xmm6, xmm5\n"
  "    sha256rnds2 xmm2, xmm1, xmm0\n"
  "    pshufd xmm0, xmm0, 0xe\n"
  "    movdqa xmm7, xmm6\n"
  "    palignr xmm7, xmm5, 0x4\n"
  "    paddd  xmm3, xmm7\n"
  "    sha256msg1 xmm4, xmm5\n"
  "    sha256rnds2 xmm1, xmm2, xmm0\n"
  "\n"
  "    movdqa xmm0, [rcx+0xe0]\n"
  "    paddd  xmm0, xmm6\n"
  "    sha256msg2 xmm3, xmm6\n"
  "    sha256rnds2 xmm2, xmm1, xmm0\n"
  "    pshufd xmm0, xmm0, 0xe\n"
  "    movdqa xmm7, xmm3\n"
  "    palignr xmm7, xmm6, 0x4\n"
  "    paddd  xmm4, xmm7\n"
  "    sha256msg1 xmm5, xmm6\n"
  "    sha256rnds2 xmm1, xmm2, xmm0\n"
  "\n"
  "    movdqa xmm0, [rcx+0x100]\n"
  "    paddd  xmm0, xmm3\n"
  "    sha256msg2 xmm4, xmm3\n"
  "    sha256rnds2 xmm2, xmm1, xmm0\n"
  "    pshufd xmm0, xmm0, 0xe\n"
  "    movdqa xmm7, xmm4\n"
  "    palignr xmm7, xmm3, 0x4\n"
  "    paddd  xmm5, xmm7\n"
  "    sha256msg1 xmm6, xmm3\n"
  "    sha256rnds2 xmm1, xmm2, xmm0\n"
  "\n"
  "    movdqa xmm0, [rcx+0x120]\n"
  "    paddd  xmm0, xmm4\n"
  "    sha256msg2 xmm5, xmm4\n"
  "    sha256rnds2 xmm2, xmm1, xmm0\n"
  "    pshufd xmm0, xmm0, 0xe\n"
  "    movdqa xmm7, xmm5\n"
  "    palignr xmm7, xmm4, 0x4\n"
  "    sha256rnds2 xmm1, xmm2, xmm0\n"
  "    paddd  xmm6, xmm7\n"
  "\n"
  "    movdqa xmm0, [rcx+0x140]\n"
  "    paddd  xmm0, xmm5\n"
  "    sha256rnds2 xmm2, xmm1, xmm0\n"
  "    pshufd xmm0, xmm0, 0xe\n"
  "    sha256msg2 xmm6, xmm5\n"
  "    movdqa xmm7, xmm8\n"
  "    sha256rnds2 xmm1, xmm2, xmm0\n"
  "\n"
  "    movdqa xmm0, [rcx+0x160]\n"
  "    paddd  xmm0, xmm6\n"
  "    sha256rnds2 xmm2, xmm1, xmm0\n"
  "    pshufd xmm0, xmm0, 0xe\n"
  "    dec    rdx\n"
  "    sha256rnds2 xmm1, xmm2, xmm0\n"
  "\n"
  "    paddd  xmm2, xmm10\n"
  "    paddd  xmm1, xmm9\n"
  "    jne    .Lloop\n"
  "\n"
  "    pshufd xmm2, xmm2, 0xb1\n"
  "    pshufd xmm7, xmm1, 0x1b\n"
  "    pshufd xmm1, xmm1, 0xb1\n"
  "    punpckhqdq xmm1, xmm2\n"
  "    palignr xmm2, xmm7, 0x8\n"
  "    movdqu [rdi], xmm1\n"
  "    movdqu [rdi+0x10], xmm2\n"
  "\n"
  "    movups xmm6, [rsp+0x00]\n"
  "    movups xmm7, [rsp+0x10]\n"
  "    movups xmm8, [rsp+0x20]\n"
  "    movups xmm9, [rsp+0x30]\n"
  "    movups xmm10, [rsp+0x40]\n"
  "    add    rsp, 0x68\n"
  "    pop    rbp\n"
  "    pop    rbx\n"
  "    pop    rsi\n"
  "    pop    rdi\n"
  "    ret\n"
  "\n"
  ZAN_SHA256_RODATA
  ".p2align 6\n"
  "K256:\n"
  "    .long 0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5\n"
  "    .long 0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5\n"
  "    .long 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5\n"
  "    .long 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5\n"
  "    .long 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3\n"
  "    .long 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3\n"
  "    .long 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174\n"
  "    .long 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174\n"
  "    .long 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc\n"
  "    .long 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc\n"
  "    .long 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da\n"
  "    .long 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da\n"
  "    .long 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7\n"
  "    .long 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7\n"
  "    .long 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967\n"
  "    .long 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967\n"
  "    .long 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13\n"
  "    .long 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13\n"
  "    .long 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85\n"
  "    .long 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85\n"
  "    .long 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3\n"
  "    .long 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3\n"
  "    .long 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070\n"
  "    .long 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070\n"
  "    .long 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5\n"
  "    .long 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5\n"
  "    .long 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3\n"
  "    .long 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3\n"
  "    .long 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208\n"
  "    .long 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208\n"
  "    .long 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2\n"
  "    .long 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2\n"
  "    # Byte swap mask for pshufb:\n"
  "    .byte 0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04\n"
  "    .byte 0x0b, 0x0a, 0x09, 0x08, 0x0f, 0x0e, 0x0d, 0x0c\n"
  "    .byte 0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04\n"
  "    .byte 0x0b, 0x0a, 0x09, 0x08, 0x0f, 0x0e, 0x0d, 0x0c\n"
  ".text\n"
  ".att_syntax prefix\n"
);
extern void zan_sha256_transform_ni(uint32_t state[8], const uint8_t *data, size_t num_blocks);
#endif

static const uint32_t K256_C[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,
    0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
    0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,
    0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,
    0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
    0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,
    0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,
    0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
    0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

/* ARMv8 Cryptographic Extension SHA-2 (FEAT_SHA2: sha256h/h2/su0/su1). */
#if (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
#include <arm_neon.h>

__attribute__((target("sha2")))
static void zan_sha256_transform_arm(uint32_t state[8], const uint8_t *data, size_t num_blocks) {
    uint32x4_t s0 = vld1q_u32(&state[0]);
    uint32x4_t s1 = vld1q_u32(&state[4]);
    for (size_t b = 0; b < num_blocks; b++) {
        const uint8_t *p = data + b * 64;
        uint32x4_t state0 = s0, state1 = s1;
        uint32x4_t msg0 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p)));
        uint32x4_t msg1 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 16)));
        uint32x4_t msg2 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 32)));
        uint32x4_t msg3 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 48)));
        uint32x4_t tmp, save;

        /* ACLE SHA-256 pair, verified against the FIPS loop: vsha256hq
         * (abcd, efgh, wk) returns the new ABCD, vsha256h2q (efgh, abcd,
         * wk) returns the new EFGH and reads the PRE-group ABCD. */
        /* Rounds 0-3 */
        save = state0;
        tmp = vaddq_u32(msg0, vld1q_u32(&K256_C[0]));
        state0 = vsha256hq_u32(state0, state1, tmp);
        state1 = vsha256h2q_u32(state1, save, tmp);
        msg0 = vsha256su0q_u32(msg0, msg1);
        /* Rounds 4-7 */
        save = state0;
        tmp = vaddq_u32(msg1, vld1q_u32(&K256_C[4]));
        state0 = vsha256hq_u32(state0, state1, tmp);
        state1 = vsha256h2q_u32(state1, save, tmp);
        msg1 = vsha256su0q_u32(msg1, msg2);
        msg0 = vsha256su1q_u32(msg0, msg2, msg3);
        /* Rounds 8-11 */
        save = state0;
        tmp = vaddq_u32(msg2, vld1q_u32(&K256_C[8]));
        state0 = vsha256hq_u32(state0, state1, tmp);
        state1 = vsha256h2q_u32(state1, save, tmp);
        msg2 = vsha256su0q_u32(msg2, msg3);
        msg1 = vsha256su1q_u32(msg1, msg3, msg0);
        /* Rounds 12-15 */
        save = state0;
        tmp = vaddq_u32(msg3, vld1q_u32(&K256_C[12]));
        state0 = vsha256hq_u32(state0, state1, tmp);
        state1 = vsha256h2q_u32(state1, save, tmp);
        msg3 = vsha256su0q_u32(msg3, msg0);
        msg2 = vsha256su1q_u32(msg2, msg0, msg1);

        for (int i = 16; i < 64; i += 16) {
            save = state0;
            tmp = vaddq_u32(msg0, vld1q_u32(&K256_C[i]));
            state0 = vsha256hq_u32(state0, state1, tmp);
            state1 = vsha256h2q_u32(state1, save, tmp);
            msg0 = vsha256su0q_u32(msg0, msg1);
            msg3 = vsha256su1q_u32(msg3, msg1, msg2);

            save = state0;
            tmp = vaddq_u32(msg1, vld1q_u32(&K256_C[i + 4]));
            state0 = vsha256hq_u32(state0, state1, tmp);
            state1 = vsha256h2q_u32(state1, save, tmp);
            msg1 = vsha256su0q_u32(msg1, msg2);
            msg0 = vsha256su1q_u32(msg0, msg2, msg3);

            save = state0;
            tmp = vaddq_u32(msg2, vld1q_u32(&K256_C[i + 8]));
            state0 = vsha256hq_u32(state0, state1, tmp);
            state1 = vsha256h2q_u32(state1, save, tmp);
            msg2 = vsha256su0q_u32(msg2, msg3);
            msg1 = vsha256su1q_u32(msg1, msg3, msg0);

            save = state0;
            tmp = vaddq_u32(msg3, vld1q_u32(&K256_C[i + 12]));
            state0 = vsha256hq_u32(state0, state1, tmp);
            state1 = vsha256h2q_u32(state1, save, tmp);
            msg3 = vsha256su0q_u32(msg3, msg0);
            msg2 = vsha256su1q_u32(msg2, msg0, msg1);
        }

        s0 = vaddq_u32(s0, state0);
        s1 = vaddq_u32(s1, state1);
    }
    vst1q_u32(&state[0], s0);
    vst1q_u32(&state[4], s1);
}

static int zan_sha256_kat_arm(void) {
    /* FIPS 180-4: SHA-256("abc") */
    static const uint8_t blk[64] = {
        0x61,0x62,0x63,0x80, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0x18
    };
    static const uint32_t want[8] = {
        0xba7816bf, 0x8f01cfea, 0x414140de, 0x5dae2223,
        0xb00361a3, 0x96177a9c, 0xb410ff61, 0xf20015ad
    };
    uint32_t a[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    zan_sha256_transform_arm(a, blk, 1);
    for (int i = 0; i < 8; i++) {
        if (a[i] != want[i]) return 0;
    }
    return 1;
}

/* FEAT_SHA1 (sha1c/sha1p/sha1m rounds, sha1h rotate, su0/su1 schedule).
 * Group invariant: after a 4-round group the new E equals (a_before_group)
 * <<< 30, which is exactly what SHA1H extracts from the pre-group lane 0. */
__attribute__((target("sha2")))
static void zan_sha1_transform_arm(uint32_t state[5], const uint8_t *data, size_t num_blocks) {
    uint32x4_t abcd = vld1q_u32(&state[0]);
    uint32_t e = state[4];
    for (size_t b = 0; b < num_blocks; b++) {
        const uint8_t *p = data + b * 64;
        uint32x4_t abcd0 = abcd;
        uint32_t e0 = e;
        uint32x4_t msg[4] = {
            vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p))),
            vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 16))),
            vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 32))),
            vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(p + 48)))
        };
        for (int g = 0; g < 20; g++) {
            uint32_t e_next = vsha1h_u32(vgetq_lane_u32(abcd, 0));
            if (g >= 4) {
                uint32x4_t t = vsha1su0q_u32(msg[g % 4], msg[(g + 1) % 4], msg[(g + 2) % 4]);
                msg[g % 4] = vsha1su1q_u32(t, msg[(g + 3) % 4]);
            }
            /* The SHA-1 instructions carry no K constant: K rides in the
             * message word (SHA1C/P/M fold m ^ k internally). The schedule
             * updates above stay on the raw words. */
            uint32_t k = (g < 5)   ? 0x5a827999u
                       : (g < 10)  ? 0x6ed9eba1u
                       : (g < 15)  ? 0x8f1bbcdcu
                       :             0xca62c1d6u;
            uint32x4_t wm = vaddq_u32(msg[g % 4], vdupq_n_u32(k));
            if (g < 5)       abcd = vsha1cq_u32(abcd, e, wm);
            else if (g < 10) abcd = vsha1pq_u32(abcd, e, wm);
            else if (g < 15) abcd = vsha1mq_u32(abcd, e, wm);
            else             abcd = vsha1pq_u32(abcd, e, wm);
            e = e_next;
        }
        abcd = vaddq_u32(abcd, abcd0);
        e += e0;
    }
    vst1q_u32(&state[0], abcd);
    state[4] = e;
}
#endif /* aarch64 sha1+sha2 */

#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
static int zan_sha256_kat_x86(void) {
    /* FIPS 180-4: SHA-256("abc") */
    static const uint8_t blk[64] = {
        0x61,0x62,0x63,0x80, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0x18
    };
    static const uint32_t want[8] = {
        0xba7816bf, 0x8f01cfea, 0x414140de, 0x5dae2223,
        0xb00361a3, 0x96177a9c, 0xb410ff61, 0xf20015ad
    };
    uint32_t a[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    extern void zan_sha256_transform_ni(uint32_t state[8], const uint8_t *data, size_t num_blocks);
    zan_sha256_transform_ni(a, blk, 1);
    for (int i = 0; i < 8; i++) {
        if (a[i] != want[i]) return 0;
    }
    return 1;
}
#endif

int64_t zan_hw_sha256(const uint8_t *data, int64_t len, uint8_t out[32]) {
    if (len < 0) len = 0;
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    size_t full_blocks = (size_t)len / 64;
    int use_ni = 0;
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
    use_ni = zan_hw_gate(&g_gate_sha256, zan_hw_has_shani(), zan_sha256_kat_x86);
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    use_ni = zan_hw_gate(&g_gate_sha256, zan_hw_arm_sha2(), zan_sha256_kat_arm);
#endif
    if (!use_ni) return -1;

    if (full_blocks > 0 && data) {
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
        zan_sha256_transform_ni(state, data, full_blocks);
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
        zan_sha256_transform_arm(state, data, full_blocks);
#endif
    }

    // Stack tail padding: fixed 128 bytes
    uint8_t tail[128];
    size_t rem = (size_t)len % 64;
    if (rem > 0 && data) {
        memcpy(tail, data + full_blocks * 64, rem);
    }
    tail[rem] = 0x80;
    size_t pad_blocks = (rem >= 56) ? 2 : 1;
    size_t total_tail = pad_blocks * 64;
    memset(tail + rem + 1, 0, total_tail - rem - 1);
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) {
        tail[total_tail - 8 + i] = (uint8_t)(bits >> (56 - 8 * i));
    }

#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
    zan_sha256_transform_ni(state, tail, pad_blocks);
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    zan_sha256_transform_arm(state, tail, pad_blocks);
#endif

    for (int i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(state[i] >> 24);
        out[i*4+1] = (uint8_t)(state[i] >> 16);
        out[i*4+2] = (uint8_t)(state[i] >> 8);
        out[i*4+3] = (uint8_t)(state[i]);
    }
    return 0;
}

/* ===== 2.1 SHA-1 Hardware Kernel (FIPS 180-4) ============================
 * x86: Intel SHA Extensions (sha1rnds4/nexte/msg1/msg2). ARM: FEAT_SHA1
 * (sha1c/sha1p/sha1m). The transform consumes whole 64-byte blocks only;
 * padding and the length word are driver plumbing below.
 * ======================================================================== */
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))

/* Canonical Intel SHA Extensions pipeline (public domain, noloader/SHA-Intrinsics).
 * `length` is in BYTES and must be a multiple of 64. */
__attribute__((target("sha,sse4.1")))
static void zan_sha1_transform_ni(uint32_t state[5], const uint8_t *data, size_t num_blocks) {
    __m128i ABCD, ABCD_SAVE, E0, E0_SAVE, E1;
    __m128i MSG0, MSG1, MSG2, MSG3;
    const __m128i MASK = _mm_set_epi64x(0x0001020304050607ULL, 0x08090a0b0c0d0e0fULL);

    size_t length = num_blocks * 64;
    ABCD = _mm_loadu_si128((const __m128i*) state);
    E0 = _mm_set_epi32((int)state[4], 0, 0, 0);
    ABCD = _mm_shuffle_epi32(ABCD, 0x1B);

    while (length >= 64) {
        ABCD_SAVE = ABCD;
        E0_SAVE = E0;

        MSG0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 0)), MASK);
        E0 = _mm_add_epi32(E0, MSG0);
        E1 = ABCD;
        ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 0);

        MSG1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 16)), MASK);
        E1 = _mm_sha1nexte_epu32(E1, MSG1);
        E0 = ABCD;
        ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 0);
        MSG0 = _mm_sha1msg1_epu32(MSG0, MSG1);

        MSG2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 32)), MASK);
        E0 = _mm_sha1nexte_epu32(E0, MSG2);
        E1 = ABCD;
        ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 0);
        MSG1 = _mm_sha1msg1_epu32(MSG1, MSG2);
        MSG0 = _mm_xor_si128(MSG0, MSG2);

        MSG3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(data + 48)), MASK);
        E1 = _mm_sha1nexte_epu32(E1, MSG3);
        E0 = ABCD;
        MSG0 = _mm_sha1msg2_epu32(MSG0, MSG3);
        ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 0);
        MSG2 = _mm_sha1msg1_epu32(MSG2, MSG3);
        MSG1 = _mm_xor_si128(MSG1, MSG3);

        E0 = _mm_sha1nexte_epu32(E0, MSG0);
        E1 = ABCD;
        MSG1 = _mm_sha1msg2_epu32(MSG1, MSG0);
        ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 0);
        MSG3 = _mm_sha1msg1_epu32(MSG3, MSG0);
        MSG2 = _mm_xor_si128(MSG2, MSG0);

        E1 = _mm_sha1nexte_epu32(E1, MSG1);
        E0 = ABCD;
        MSG2 = _mm_sha1msg2_epu32(MSG2, MSG1);
        ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 1);
        MSG0 = _mm_sha1msg1_epu32(MSG0, MSG1);
        MSG3 = _mm_xor_si128(MSG3, MSG1);

        E0 = _mm_sha1nexte_epu32(E0, MSG2);
        E1 = ABCD;
        MSG3 = _mm_sha1msg2_epu32(MSG3, MSG2);
        ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 1);
        MSG1 = _mm_sha1msg1_epu32(MSG1, MSG2);
        MSG0 = _mm_xor_si128(MSG0, MSG2);

        E1 = _mm_sha1nexte_epu32(E1, MSG3);
        E0 = ABCD;
        MSG0 = _mm_sha1msg2_epu32(MSG0, MSG3);
        ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 1);
        MSG2 = _mm_sha1msg1_epu32(MSG2, MSG3);
        MSG1 = _mm_xor_si128(MSG1, MSG3);

        E0 = _mm_sha1nexte_epu32(E0, MSG0);
        E1 = ABCD;
        MSG1 = _mm_sha1msg2_epu32(MSG1, MSG0);
        ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 1);
        MSG3 = _mm_sha1msg1_epu32(MSG3, MSG0);
        MSG2 = _mm_xor_si128(MSG2, MSG0);

        E1 = _mm_sha1nexte_epu32(E1, MSG1);
        E0 = ABCD;
        MSG2 = _mm_sha1msg2_epu32(MSG2, MSG1);
        ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 1);
        MSG0 = _mm_sha1msg1_epu32(MSG0, MSG1);
        MSG3 = _mm_xor_si128(MSG3, MSG1);

        E0 = _mm_sha1nexte_epu32(E0, MSG2);
        E1 = ABCD;
        MSG3 = _mm_sha1msg2_epu32(MSG3, MSG2);
        ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 2);
        MSG1 = _mm_sha1msg1_epu32(MSG1, MSG2);
        MSG0 = _mm_xor_si128(MSG0, MSG2);

        E1 = _mm_sha1nexte_epu32(E1, MSG3);
        E0 = ABCD;
        MSG0 = _mm_sha1msg2_epu32(MSG0, MSG3);
        ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 2);
        MSG2 = _mm_sha1msg1_epu32(MSG2, MSG3);
        MSG1 = _mm_xor_si128(MSG1, MSG3);

        E0 = _mm_sha1nexte_epu32(E0, MSG0);
        E1 = ABCD;
        MSG1 = _mm_sha1msg2_epu32(MSG1, MSG0);
        ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 2);
        MSG3 = _mm_sha1msg1_epu32(MSG3, MSG0);
        MSG2 = _mm_xor_si128(MSG2, MSG0);

        E1 = _mm_sha1nexte_epu32(E1, MSG1);
        E0 = ABCD;
        MSG2 = _mm_sha1msg2_epu32(MSG2, MSG1);
        ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 2);
        MSG0 = _mm_sha1msg1_epu32(MSG0, MSG1);
        MSG3 = _mm_xor_si128(MSG3, MSG1);

        E0 = _mm_sha1nexte_epu32(E0, MSG2);
        E1 = ABCD;
        MSG3 = _mm_sha1msg2_epu32(MSG3, MSG2);
        ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 2);
        MSG1 = _mm_sha1msg1_epu32(MSG1, MSG2);
        MSG0 = _mm_xor_si128(MSG0, MSG2);

        E1 = _mm_sha1nexte_epu32(E1, MSG3);
        E0 = ABCD;
        MSG0 = _mm_sha1msg2_epu32(MSG0, MSG3);
        ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 3);
        MSG2 = _mm_sha1msg1_epu32(MSG2, MSG3);
        MSG1 = _mm_xor_si128(MSG1, MSG3);

        E0 = _mm_sha1nexte_epu32(E0, MSG0);
        E1 = ABCD;
        MSG1 = _mm_sha1msg2_epu32(MSG1, MSG0);
        ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 3);
        MSG3 = _mm_sha1msg1_epu32(MSG3, MSG0);
        MSG2 = _mm_xor_si128(MSG2, MSG0);

        E1 = _mm_sha1nexte_epu32(E1, MSG1);
        E0 = ABCD;
        MSG2 = _mm_sha1msg2_epu32(MSG2, MSG1);
        ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 3);
        MSG3 = _mm_xor_si128(MSG3, MSG1);

        E0 = _mm_sha1nexte_epu32(E0, MSG2);
        E1 = ABCD;
        MSG3 = _mm_sha1msg2_epu32(MSG3, MSG2);
        ABCD = _mm_sha1rnds4_epu32(ABCD, E0, 3);

        E1 = _mm_sha1nexte_epu32(E1, MSG3);
        E0 = ABCD;
        ABCD = _mm_sha1rnds4_epu32(ABCD, E1, 3);

        E0 = _mm_sha1nexte_epu32(E0, E0_SAVE);
        ABCD = _mm_add_epi32(ABCD, ABCD_SAVE);

        data += 64;
        length -= 64;
    }

    ABCD = _mm_shuffle_epi32(ABCD, 0x1B);
    _mm_storeu_si128((__m128i*) state, ABCD);
    state[4] = (uint32_t)_mm_extract_epi32(E0, 3);
}

#endif /* x86 sha1 */

/* FEAT_SHA1 rides LLVM's "sha2" target feature (SHA-1 and SHA-256 ship as
 * one crypto unit), so the kernel below carries the same attribute as the
 * SHA-2 one. */
#if (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
#define ZAN_SHA1_ARM_KERNEL 1
#else
#define ZAN_SHA1_ARM_KERNEL 0
#endif

static int zan_sha1_kat(void) {
    /* FIPS 180-4: SHA-1("abc") = a9993e36 4706816a b3e25717 850c26c9 cd0d89d */
    static const uint8_t blk[64] = {
        0x61,0x62,0x63,0x80, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0x18
    };
    uint32_t a[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
    zan_sha1_transform_ni(a, blk, 1);
#elif ZAN_SHA1_ARM_KERNEL
    zan_sha1_transform_arm(a, blk, 1);
#endif
    static const uint8_t wantb[20] = {
        0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,
        0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d
    };
    uint8_t got[20];
    for (int i = 0; i < 5; i++) {
        got[i*4]   = (uint8_t)(a[i] >> 24);
        got[i*4+1] = (uint8_t)(a[i] >> 16);
        got[i*4+2] = (uint8_t)(a[i] >> 8);
        got[i*4+3] = (uint8_t)(a[i]);
    }
    return memcmp(got, wantb, 20) == 0;
}

int64_t zan_hw_sha1(const uint8_t *data, int64_t len, uint8_t out[20]) {
    if (len < 0) len = 0;
    int use_ni = 0;
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
    use_ni = zan_hw_gate(&g_gate_sha1, zan_hw_has_shani(), zan_sha1_kat);
#elif ZAN_SHA1_ARM_KERNEL
    use_ni = zan_hw_gate(&g_gate_sha1, zan_hw_arm_sha1(), zan_sha1_kat);
#endif
    if (!use_ni) return -1;

    uint32_t state[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };
    size_t full_blocks = (size_t)len / 64;
    if (full_blocks > 0 && data) {
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
        zan_sha1_transform_ni(state, data, full_blocks);
#elif ZAN_SHA1_ARM_KERNEL
        zan_sha1_transform_arm(state, data, full_blocks);
#endif
    }

    uint8_t tail[128];
    size_t rem = (size_t)len % 64;
    if (rem > 0 && data) {
        memcpy(tail, data + full_blocks * 64, rem);
    }
    tail[rem] = 0x80;
    size_t pad_blocks = (rem >= 56) ? 2 : 1;
    size_t total_tail = pad_blocks * 64;
    memset(tail + rem + 1, 0, total_tail - rem - 1);
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) {
        tail[total_tail - 8 + i] = (uint8_t)(bits >> (56 - 8 * i));
    }

#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
    zan_sha1_transform_ni(state, tail, pad_blocks);
#elif ZAN_SHA1_ARM_KERNEL
    zan_sha1_transform_arm(state, tail, pad_blocks);
#endif

    for (int i = 0; i < 5; i++) {
        out[i*4]   = (uint8_t)(state[i] >> 24);
        out[i*4+1] = (uint8_t)(state[i] >> 16);
        out[i*4+2] = (uint8_t)(state[i] >> 8);
        out[i*4+3] = (uint8_t)(state[i]);
    }
    return 0;
}

/* ===== 3. AES Hardware Kernels (FIPS-197, SP 800-38A) & GHASH (SP 800-38D)
 * Thin instruction-facing kernels only: AES-NI/PCLMULQDQ on x86, FEAT_AES /
 * FEAT_PMULL on ARM. Key schedule is the FIPS-197 scalar expansion feeding
 * the round instructions (shared by both ISAs); modes live in the Zan
 * stdlib, which falls back to its pure-Zan implementation when the kernel
 * reports -1 (no hardware, or the known-answer test below failed once).
 * ======================================================================== */
static uint8_t zan_aes_sbox[256];

static void zan_aes_init_sbox(void) {
    /* FIPS-197 S-box generated from GF(2^8) inverse + affine map, so the
     * table stays generated plumbing rather than a second implementation. */
    static int inited = 0;
    if (inited) return;
    uint8_t inv[256];
    inv[0] = 0; inv[1] = 1;
    for (int i = 2; i < 256; i++) {
        for (int j = 2; j < 256; j++) {
            uint8_t p = 0, a = (uint8_t)i, b = (uint8_t)j;
            for (int k = 0; k < 8; k++) {
                if (b & 1) p ^= a;
                uint8_t hi = a & 0x80;
                a <<= 1;
                if (hi) a ^= 0x1B;
                b >>= 1;
            }
            if (p == 1) { inv[i] = (uint8_t)j; break; }
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

/* FIPS-197 key expansion into nr+1 16-byte round keys (big-endian words). */
static void zan_aes_expand_key(const uint8_t *key, int keybits,
                               uint8_t rk[15][16], int *nr_out) {
    zan_aes_init_sbox();
    int nk = keybits / 32;
    int nr = nk + 6;
    uint32_t w[60];
    for (int i = 0; i < nk; i++) {
        w[i] = ((uint32_t)key[4*i] << 24) | ((uint32_t)key[4*i+1] << 16) |
               ((uint32_t)key[4*i+2] << 8) | key[4*i+3];
    }
    int rc = 1;
    for (int i = nk; i < 4 * (nr + 1); i++) {
        uint32_t t = w[i-1];
        if (i % nk == 0) {
            t = (t << 8) | (t >> 24);  /* RotWord */
            t = ((uint32_t)zan_aes_sbox[(t >> 24) & 255] << 24) |
                ((uint32_t)zan_aes_sbox[(t >> 16) & 255] << 16) |
                ((uint32_t)zan_aes_sbox[(t >> 8) & 255] << 8) |
                zan_aes_sbox[t & 255];
            t ^= (uint32_t)rc << 24;
            rc = ((rc << 1) ^ ((rc & 0x80) ? 0x11B : 0)) & 0xFF;
        } else if (nk > 6 && i % nk == 4) {
            t = ((uint32_t)zan_aes_sbox[(t >> 24) & 255] << 24) |
                ((uint32_t)zan_aes_sbox[(t >> 16) & 255] << 16) |
                ((uint32_t)zan_aes_sbox[(t >> 8) & 255] << 8) |
                zan_aes_sbox[t & 255];
        }
        w[i] = w[i - nk] ^ t;
    }
    for (int r = 0; r <= nr; r++) {
        for (int c = 0; c < 4; c++) {
            uint32_t v = w[r*4 + c];
            rk[r][c*4]   = (uint8_t)(v >> 24);
            rk[r][c*4+1] = (uint8_t)(v >> 16);
            rk[r][c*4+2] = (uint8_t)(v >> 8);
            rk[r][c*4+3] = (uint8_t)v;
        }
    }
    *nr_out = nr;
}

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))

__attribute__((target("aes,sse4.1")))
static inline __m128i zan_aes_load_rk(const uint8_t *p) {
    return _mm_loadu_si128((const __m128i*)p);
}

__attribute__((target("aes,sse4.1")))
static void zan_aes_expand_dec_hw(const uint8_t *key, int keybits, __m128i *dec_rk) {
    uint8_t rk[15][16];
    int nr;
    zan_aes_expand_key(key, keybits, rk, &nr);
    dec_rk[0] = zan_aes_load_rk(rk[nr]);
    for (int i = 1; i < nr; i++) {
        dec_rk[i] = _mm_aesimc_si128(zan_aes_load_rk(rk[nr - i]));
    }
    dec_rk[nr] = zan_aes_load_rk(rk[0]);
}

__attribute__((target("aes,sse4.1")))
static __m128i zan_aes_enc_block_hw(const uint8_t *key, int keybits, __m128i block) {
    uint8_t rkb[15][16];
    int nr;
    zan_aes_expand_key(key, keybits, rkb, &nr);
    __m128i rk[15];
    for (int i = 0; i <= nr; i++) rk[i] = zan_aes_load_rk(rkb[i]);
    block = _mm_xor_si128(block, rk[0]);
    for (int r = 1; r < nr; r++) block = _mm_aesenc_si128(block, rk[r]);
    return _mm_aesenclast_si128(block, rk[nr]);
}

__attribute__((target("aes,sse4.1")))
static int64_t zan_aes_cbc_encrypt_ni(const uint8_t *in, int64_t len,
                                      const uint8_t *key, int keybits,
                                      const uint8_t *iv, uint8_t *out) {
    uint8_t rkb[15][16];
    int nr;
    zan_aes_expand_key(key, keybits, rkb, &nr);
    __m128i rk[15];
    for (int i = 0; i <= nr; i++) rk[i] = zan_aes_load_rk(rkb[i]);

    int pad_val = 16 - (int)(len % 16);
    int64_t full_blocks = len / 16;
    __m128i feedback = _mm_loadu_si128((const __m128i*)iv);

    for (int64_t i = 0; i < full_blocks; i++) {
        __m128i block = _mm_loadu_si128((const __m128i*)(in + i * 16));
        block = _mm_xor_si128(block, feedback);
        block = _mm_xor_si128(block, rk[0]);
        for (int r = 1; r < nr; r++) block = _mm_aesenc_si128(block, rk[r]);
        block = _mm_aesenclast_si128(block, rk[nr]);
        _mm_storeu_si128((__m128i*)(out + i * 16), block);
        feedback = block;
    }

    uint8_t tail[16];
    int rem = (int)(len - full_blocks * 16);
    for (int j = 0; j < rem; j++) tail[j] = in[full_blocks * 16 + j];
    for (int j = rem; j < 16; j++) tail[j] = (uint8_t)pad_val;

    __m128i block = _mm_loadu_si128((const __m128i*)tail);
    block = _mm_xor_si128(block, feedback);
    block = _mm_xor_si128(block, rk[0]);
    for (int r = 1; r < nr; r++) block = _mm_aesenc_si128(block, rk[r]);
    block = _mm_aesenclast_si128(block, rk[nr]);
    _mm_storeu_si128((__m128i*)(out + full_blocks * 16), block);
    return (full_blocks + 1) * 16;
}

__attribute__((target("aes,sse4.1")))
static int64_t zan_aes_cbc_decrypt_ni(const uint8_t *in, int64_t len,
                                      const uint8_t *key, int keybits,
                                      const uint8_t *iv, uint8_t *out) {
    if (len <= 0 || (len % 16) != 0) return -1;
    __m128i dec_rk[15];
    zan_aes_expand_dec_hw(key, keybits, dec_rk);
    int nr = keybits / 32 + 6;

    int64_t blocks = len / 16;
    __m128i prev = _mm_loadu_si128((const __m128i*)iv);

    for (int64_t i = 0; i < blocks; i++) {
        __m128i cur = _mm_loadu_si128((const __m128i*)(in + i * 16));
        __m128i block = _mm_xor_si128(cur, dec_rk[0]);
        for (int r = 1; r < nr; r++) block = _mm_aesdec_si128(block, dec_rk[r]);
        block = _mm_aesdeclast_si128(block, dec_rk[nr]);
        block = _mm_xor_si128(block, prev);
        _mm_storeu_si128((__m128i*)(out + i * 16), block);
        prev = cur;
    }

    uint8_t pad_val = out[len - 1];
    if (pad_val == 0 || pad_val > 16) return -1;
    int bad = 0;
    for (int i = 0; i < pad_val; i++) {
        if (out[len - 1 - i] != pad_val) bad = 1;
    }
    if (bad) return -1;
    return len - pad_val;
}

__attribute__((target("aes,sse4.1")))
static int64_t zan_aes_ecb_block_ni(const uint8_t *key, int keybits,
                                    const uint8_t *in16, uint8_t *out16) {
    __m128i block = _mm_loadu_si128((const __m128i*)in16);
    block = zan_aes_enc_block_hw(key, keybits, block);
    _mm_storeu_si128((__m128i*)out16, block);
    return 0;
}

__attribute__((target("aes,sse4.1")))
static int64_t zan_aes_ctr_ni(const uint8_t *in, int64_t len,
                              const uint8_t *key, int keybits,
                              uint8_t *counter16, uint8_t *out) {
    uint8_t rkb[15][16];
    int nr;
    zan_aes_expand_key(key, keybits, rkb, &nr);
    __m128i rk[15];
    for (int i = 0; i <= nr; i++) rk[i] = zan_aes_load_rk(rkb[i]);

    uint8_t ctr[16];
    memcpy(ctr, counter16, 16);
    int64_t off = 0;
    while (off < len) {
        __m128i ks = _mm_loadu_si128((const __m128i*)ctr);
        ks = _mm_xor_si128(ks, rk[0]);
        for (int r = 1; r < nr; r++) ks = _mm_aesenc_si128(ks, rk[r]);
        ks = _mm_aesenclast_si128(ks, rk[nr]);

        __m128i blk = _mm_loadu_si128((const __m128i*)(in + off));
        __m128i o = _mm_xor_si128(blk, ks);
        int avail = (int)(len - off);
        if (avail >= 16) {
            _mm_storeu_si128((__m128i*)(out + off), o);
        } else {
            uint8_t tmp[16];
            _mm_storeu_si128((__m128i*)tmp, o);
            memcpy(out + off, tmp, (size_t)avail);
        }

        /* 128-bit big-endian counter increment */
        for (int j = 15; j >= 0; j--) {
            if (++ctr[j] != 0) break;
        }
        off += 16;
    }
    memcpy(counter16, ctr, 16);
    return len;
}

/* GHASH over PCLMULQDQ. Domain: blocks byte-reversed into the register so
 * register bit r <-> GCM coefficient x^(127-r). Product bit t <-> x^(254-t);
 * reduction of bit t (t <= 126) lands at result bits {t-6, t-1, t, t+1}; the
 * six lowest product bits additionally spill through the second-level fold
 * (0xE1 at the top byte = q = x^7+x^2+x+1). Verified against the GCM spec
 * bit loop and KAT'd below. */
#define ZAN_XSHIFT_R(x, n) _mm_xor_si128(_mm_srli_epi64(x, n), _mm_srli_si128(_mm_slli_epi64(x, 64-(n)), 8))
#define ZAN_XSHIFT_L(x, n) _mm_xor_si128(_mm_slli_epi64(x, n), _mm_slli_si128(_mm_srli_epi64(x, 64-(n)), 8))
#define ZAN_BSWAP128 _mm_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15)

__attribute__((target("pclmul,sse4.1")))
static int64_t zan_ghash_block_clmul(const uint8_t *h16, const uint8_t *x16, uint8_t *y16) {
    const __m128i BSWAP = ZAN_BSWAP128;
    __m128i b = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)h16), BSWAP);
    __m128i x = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)x16), BSWAP);
    __m128i y = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)y16), BSWAP);
    __m128i a = _mm_xor_si128(y, x);

    __m128i m0 = _mm_clmulepi64_si128(a, b, 0x00);
    __m128i m3 = _mm_clmulepi64_si128(a, b, 0x11);
    __m128i mid = _mm_xor_si128(_mm_clmulepi64_si128(a, b, 0x10),
                                _mm_clmulepi64_si128(a, b, 0x01));
    __m128i lo = _mm_xor_si128(m0, _mm_slli_si128(mid, 8));
    __m128i hi = _mm_xor_si128(m3, _mm_srli_si128(mid, 8));

    uint64_t bit127 = (uint64_t)_mm_extract_epi64(lo, 1) >> 63;
    __m128i E = _mm_xor_si128(lo, _mm_set_epi64x((long long)(bit127 << 63), 0));
    __m128i F = _mm_xor_si128(_mm_xor_si128(ZAN_XSHIFT_R(E, 6), ZAN_XSHIFT_R(E, 1)),
                              _mm_xor_si128(E, ZAN_XSHIFT_L(E, 1)));
    uint64_t s = 0, low6 = (uint64_t)_mm_extract_epi64(lo, 0) & 0x3F;
    for (int i = 0; i < 6; i++) if ((low6 >> i) & 1) {
        s ^= 0xE1ULL << (51 + i);          /* x^(133-i) = x^(5-i) * q */
        if (i == 0) s ^= 0xE1ULL << 56;    /* x^128 = q, only t = 0 */
    }
    __m128i direct = _mm_xor_si128(ZAN_XSHIFT_L(hi, 1), _mm_set_epi64x(0, (long long)bit127));
    __m128i res = _mm_xor_si128(F, _mm_xor_si128(direct, _mm_set_epi64x((long long)s, 0)));
    _mm_storeu_si128((__m128i*)y16, _mm_shuffle_epi8(res, BSWAP));
    return 0;
}

/* CRC-32C (Castagnoli), reflected poly 0x82F63B78, SSE4.2 single-cycle.
 * clang's feature model keeps the CRC32 instructions behind their own
 * "crc32" feature -- target("sse4.2") alone covers the SIMD half only
 * (-msse4.2 on the command line implies it, a function attribute does
 * not), so the cross builds hard-error without it. */
__attribute__((target("sse4.2,crc32")))
static uint32_t zan_crc32c_sse42(uint32_t crc, const uint8_t *p, int64_t n) {
    uint64_t c = crc;
    while (n >= 8 && ((uintptr_t)p & 7)) { c = _mm_crc32_u8((uint32_t)c, *p++); n--; }
    while (n >= 8) { c = _mm_crc32_u64(c, *(const uint64_t*)p); p += 8; n -= 8; }
    if (n >= 4) { c = _mm_crc32_u32((uint32_t)c, *(const uint32_t*)p); p += 4; n -= 4; }
    if (n >= 2) { c = _mm_crc32_u16((uint32_t)c, *(const uint16_t*)p); p += 2; n -= 2; }
    if (n >= 1) { c = _mm_crc32_u8((uint32_t)c, *p); }
    return (uint32_t)c;
}

#endif /* x86 */

/* ---- ARM64 FEAT_AES / FEAT_PMULL / FEAT_CRC32 kernels ----
 * Mirrors of the x86 kernels above: same shared FIPS-197 scalar key
 * schedule (consumed round by round in the same order), same PKCS#7 CBC /
 * CTR / single-block-ECB semantics, same GHASH register-domain reflection.
 * On ARM the round key is XORed BEFORE the S-box (AESE/AESD), so the
 * pipeline is vaesmc(vaeseq(...)) over keys 0..nr-1 with the final round
 * XORing the last key twice (the pre-XOR inside vaeseq and the explicit
 * veor cancel, leaving the true post-round whitening). */
#if (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
#include <arm_acle.h>

typedef unsigned __int128 zan_u128;

static inline uint8x16_t zan_bswap128_arm(uint8x16_t v) {
    uint8x16_t r = vrev64q_u8(v);
    return vextq_u8(r, r, 8);
}

__attribute__((target("aes")))
static uint8x16_t zan_aes_enc_block_arm(const uint8_t *key, int keybits, uint8x16_t block) {
    uint8_t rkb[15][16];
    int nr;
    zan_aes_expand_key(key, keybits, rkb, &nr);
    /* ARM fusion: AESE folds the round key in before the S-box, so the raw
     * schedule streams straight through vaeseq; vaesmc after every round
     * but the last (FIPS-197 round Nr has no MixColumns) and the state
     * stays "pre-whitened": b = state_{r+1} ^ rk[r+1]. The final vaeseq
     * consumes the pending rk[nr-1] to finish round Nr-1, then a plain
     * veor applies the true last key. */
    for (int r = 0; r + 1 < nr; r++) block = vaesmcq_u8(vaeseq_u8(block, vld1q_u8(rkb[r])));
    return veorq_u8(vaeseq_u8(block, vld1q_u8(rkb[nr - 1])), vld1q_u8(rkb[nr]));
}

__attribute__((target("aes")))
static void zan_aes_expand_dec_arm(const uint8_t *key, int keybits, uint8x16_t *dec_rk) {
    /* Mirror of the encryption fusion: plain reversed schedule — vaesimc is
     * applied to the state inside the round loop, not to the keys (the x86
     * aesdec flow pre-transforms its keys; ARM must not). */
    uint8_t rkb[15][16];
    int nr;
    zan_aes_expand_key(key, keybits, rkb, &nr);
    for (int i = 0; i <= nr; i++) dec_rk[i] = vld1q_u8(rkb[nr - i]);
}

__attribute__((target("aes")))
static int64_t zan_aes_cbc_encrypt_arm(const uint8_t *in, int64_t len,
                                       const uint8_t *key, int keybits,
                                       const uint8_t *iv, uint8_t *out) {
    uint8_t rkb[15][16];
    int nr;
    zan_aes_expand_key(key, keybits, rkb, &nr);
    uint8x16_t rk[15];
    for (int i = 0; i <= nr; i++) rk[i] = vld1q_u8(rkb[i]);

    int pad_val = 16 - (int)(len % 16);
    int64_t full_blocks = len / 16;
    uint8x16_t feedback = vld1q_u8(iv);

    for (int64_t i = 0; i < full_blocks; i++) {
        uint8x16_t block = veorq_u8(vld1q_u8(in + i * 16), feedback);
        for (int r = 0; r + 1 < nr; r++) block = vaesmcq_u8(vaeseq_u8(block, rk[r]));
        block = veorq_u8(vaeseq_u8(block, rk[nr - 1]), rk[nr]);
        vst1q_u8(out + i * 16, block);
        feedback = block;
    }

    uint8_t tail[16];
    int rem = (int)(len - full_blocks * 16);
    for (int j = 0; j < rem; j++) tail[j] = in[full_blocks * 16 + j];
    for (int j = rem; j < 16; j++) tail[j] = (uint8_t)pad_val;

    uint8x16_t block = veorq_u8(vld1q_u8(tail), feedback);
    for (int r = 0; r + 1 < nr; r++) block = vaesmcq_u8(vaeseq_u8(block, rk[r]));
    block = veorq_u8(vaeseq_u8(block, rk[nr - 1]), rk[nr]);
    vst1q_u8(out + full_blocks * 16, block);
    return (full_blocks + 1) * 16;
}

__attribute__((target("aes")))
static int64_t zan_aes_cbc_decrypt_arm(const uint8_t *in, int64_t len,
                                       const uint8_t *key, int keybits,
                                       const uint8_t *iv, uint8_t *out) {
    if (len <= 0 || (len % 16) != 0) return -1;
    uint8x16_t dec_rk[15];
    zan_aes_expand_dec_arm(key, keybits, dec_rk);
    int nr = keybits / 32 + 6;

    int64_t blocks = len / 16;
    uint8x16_t prev = vld1q_u8(iv);

    for (int64_t i = 0; i < blocks; i++) {
        uint8x16_t cur = vld1q_u8(in + i * 16);
        /* AESD (like AESE) XORs its key BEFORE the S-box, but the inverse
         * round needs the round key AFTER InvSubBytes — so the state
         * register carries the pending key: ct = state0 ^ rk[Nr] already,
         * each aesd cancels the pending key to feed the pure state through
         * InvSubBytes, veor adds this round's key, vaesimc applies
         * InvMixColumns, and that same key stays pending for the next
         * round. The final aesd cancels rk[1] and its output is the
         * plaintext once rk[0] is veor'd on. */
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
    if (pad_val == 0 || pad_val > 16) return -1;
    int bad = 0;
    for (int i = 0; i < pad_val; i++) {
        if (out[len - 1 - i] != pad_val) bad = 1;
    }
    if (bad) return -1;
    return len - pad_val;
}

__attribute__((target("aes")))
static int64_t zan_aes_ecb_block_arm(const uint8_t *key, int keybits,
                                     const uint8_t *in16, uint8_t *out16) {
    vst1q_u8(out16, zan_aes_enc_block_arm(key, keybits, vld1q_u8(in16)));
    return 0;
}

__attribute__((target("aes")))
static int64_t zan_aes_ctr_arm(const uint8_t *in, int64_t len,
                               const uint8_t *key, int keybits,
                               uint8_t *counter16, uint8_t *out) {
    uint8_t rkb[15][16];
    int nr;
    zan_aes_expand_key(key, keybits, rkb, &nr);
    uint8x16_t rk[15];
    for (int i = 0; i <= nr; i++) rk[i] = vld1q_u8(rkb[i]);

    uint8_t ctr[16];
    memcpy(ctr, counter16, 16);
    int64_t off = 0;
    while (off < len) {
        uint8x16_t ks = vld1q_u8(ctr);
        for (int r = 0; r + 1 < nr; r++) ks = vaesmcq_u8(vaeseq_u8(ks, rk[r]));
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

        /* 128-bit big-endian counter increment */
        for (int j = 15; j >= 0; j--) {
            if (++ctr[j] != 0) break;
        }
        off += 16;
    }
    memcpy(counter16, ctr, 16);
    return len;
}

/* GHASH over PMULL (vmull_p64 = the 64x64->128 carry-less multiply, the
 * ARM counterpart of PCLMULQDQ). Same register-domain reflection fold as
 * the x86 kernel: byte-reverse blocks into the register so register bit
 * r <-> GCM coefficient x^(127-r); product bit t <-> x^(254-t); reduction
 * of bit t (t <= 126) lands at result bits {t-6, t-1, t, t+1}; the six
 * lowest product bits spill through the second-level fold (0xE1 = q). */
__attribute__((target("aes,neon")))
static int64_t zan_ghash_block_pmull(const uint8_t *h16, const uint8_t *x16, uint8_t *y16) {
    uint64x2_t h = vreinterpretq_u64_u8(zan_bswap128_arm(vld1q_u8(h16)));
    uint64x2_t x = vreinterpretq_u64_u8(zan_bswap128_arm(vld1q_u8(x16)));
    uint64x2_t y = vreinterpretq_u64_u8(zan_bswap128_arm(vld1q_u8(y16)));
    uint64_t a_lo = vgetq_lane_u64(y, 0) ^ vgetq_lane_u64(x, 0);
    uint64_t a_hi = vgetq_lane_u64(y, 1) ^ vgetq_lane_u64(x, 1);
    uint64_t b_lo = vgetq_lane_u64(h, 0);
    uint64_t b_hi = vgetq_lane_u64(h, 1);

    zan_u128 m0  = (zan_u128)vmull_p64((poly64_t)a_lo, (poly64_t)b_lo);
    zan_u128 m3  = (zan_u128)vmull_p64((poly64_t)a_hi, (poly64_t)b_hi);
    zan_u128 mid = (zan_u128)vmull_p64((poly64_t)a_lo, (poly64_t)b_hi)
                 ^ (zan_u128)vmull_p64((poly64_t)a_hi, (poly64_t)b_lo);
    zan_u128 lo = m0 ^ (mid << 64);
    zan_u128 hi = m3 ^ (mid >> 64);

    uint64_t bit127 = (uint64_t)(lo >> 63) & 1;
    zan_u128 E = lo ^ ((zan_u128)bit127 << 127);
    zan_u128 F = (E >> 6) ^ (E >> 1) ^ E ^ (E << 1);
    uint64_t s = 0, low6 = (uint64_t)lo & 0x3F;
    for (int i = 0; i < 6; i++) if ((low6 >> i) & 1) {
        s ^= 0xE1ULL << (51 + i);          /* x^(133-i) = x^(5-i) * q */
        if (i == 0) s ^= 0xE1ULL << 56;    /* x^128 = q, only t = 0 */
    }
    zan_u128 direct = (hi << 1) ^ bit127;
    zan_u128 res = F ^ direct ^ ((zan_u128)s << 64);

    uint64x2_t rv = vcombine_u64(vmov_n_u64((uint64_t)res), vmov_n_u64((uint64_t)(res >> 64)));
    vst1q_u8(y16, zan_bswap128_arm(vreinterpretq_u8_u64(rv)));
    return 0;
}

/* CRC-32C (Castagnoli), reflected poly 0x82F63B78, FEAT_CRC32. The acle
 * helpers carry their own target("crc") attribute in arm_acle.h. */
__attribute__((target("crc")))
static uint32_t zan_crc32c_pmull_arm(uint32_t crc, const uint8_t *p, int64_t n) {
    uint64_t c = crc;
    while (n >= 8 && ((uintptr_t)p & 7)) { c = __crc32cb((uint32_t)c, *p++); n--; }
    while (n >= 8) { c = __crc32cd((uint32_t)c, *(const uint64_t*)p); p += 8; n -= 8; }
    if (n >= 4) { c = __crc32cw((uint32_t)c, *(const uint32_t*)p); p += 4; n -= 4; }
    if (n >= 2) { c = __crc32ch((uint32_t)c, *(const uint16_t*)p); p += 2; n -= 2; }
    if (n >= 1) { c = __crc32cb((uint32_t)c, *p); }
    return (uint32_t)c;
}

#endif /* aarch64 aes/ghash/crc32c */

/* ---- KAT gates: published vectors only ---- */
static int zan_aes_kat(void) {
    /* FIPS-197 appendix C: single ECB blocks for all three key sizes */
    static const uint8_t pt[16] = {
        0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff
    };
    static const uint8_t key128[16] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f
    };
    static const uint8_t key192[24] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17
    };
    static const uint8_t key256[32] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
    };
    static const uint8_t ct128[16] = { 0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a };
    static const uint8_t ct192[16] = { 0xdd,0xa9,0x7c,0xa4,0x86,0x4c,0xdf,0xe0,0x6e,0xaf,0x70,0xa0,0xec,0x0d,0x71,0x91 };
    static const uint8_t ct256[16] = { 0x8e,0xa2,0xb7,0xca,0x51,0x67,0x45,0xbf,0xea,0xfc,0x49,0x90,0x4b,0x49,0x60,0x89 };

    /* SP 800-38A F.2.1 (AES-128.CBC.Encrypt) & F.2.5 (AES-256) */
    static const uint8_t cbc_key[16] = {
        0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c
    };
    static const uint8_t cbc_iv[16] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f
    };
    static const uint8_t cbc_pt[64] = {
        0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a,
        0xae,0x2d,0x8a,0x57,0x1e,0x03,0xac,0x9c,0x9e,0xb7,0x6f,0xac,0x45,0xaf,0x8e,0x51,
        0x30,0xc8,0x1c,0x46,0xa3,0x5c,0xe4,0x11,0xe5,0xfb,0xc1,0x19,0x1a,0x0a,0x52,0xef,
        0xf6,0x9f,0x24,0x45,0xdf,0x4f,0x9b,0x17,0xad,0x2b,0x41,0x7b,0xe6,0x6c,0x37,0x10
    };
    static const uint8_t cbc_ct128[64] = {
        0x76,0x49,0xab,0xac,0x81,0x19,0xb2,0x46,0xce,0xe9,0x8e,0x9b,0x12,0xe9,0x19,0x7d,
        0x50,0x86,0xcb,0x9b,0x50,0x72,0x19,0xee,0x95,0xdb,0x11,0x3a,0x91,0x76,0x78,0xb2,
        0x73,0xbe,0xd6,0xb8,0xe3,0xc1,0x74,0x3b,0x71,0x16,0xe6,0x9e,0x22,0x22,0x95,0x16,
        0x3f,0xf1,0xca,0xa1,0x68,0x1f,0xac,0x09,0x12,0x0e,0xca,0x30,0x75,0x86,0xe1,0xa7
    };
    static const uint8_t cbc_key256[32] = {
        0x60,0x3d,0xeb,0x10,0x15,0xca,0x71,0xbe,0x2b,0x73,0xae,0xf0,0x85,0x7d,0x77,0x81,
        0x1f,0x35,0x2c,0x07,0x3b,0x61,0x08,0xd7,0x2d,0x98,0x10,0xa3,0x09,0x14,0xdf,0xf4
    };
    static const uint8_t cbc_ct256_1[16] = { 0xf5,0x8c,0x4c,0x04,0xd6,0xe5,0xf1,0xba,0x77,0x9e,0xab,0xfb,0x5f,0x7b,0xfb,0xd6 };

    /* SP 800-38A F.5.1 (AES-128.CTR.Encrypt) */
    static const uint8_t ctr_iv[16] = {
        0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff
    };
    static const uint8_t ctr_ct[32] = {
        0x87,0x4d,0x61,0x91,0xb6,0x20,0xe3,0x26,0x1b,0xef,0x68,0x64,0x99,0x0d,0xb6,0xce,
        0x98,0x06,0xf6,0x6b,0x79,0x70,0xfd,0xff,0x86,0x17,0x18,0x7b,0xb9,0xff,0xfd,0xff
    };

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    uint8_t out[80];
    uint8_t blk[16];

    if (zan_aes_ecb_block_ni(key128, 128, pt, blk) != 0) return 0;
    if (memcmp(blk, ct128, 16) != 0) return 0;
    if (zan_aes_ecb_block_ni(key192, 192, pt, blk) != 0) return 0;
    if (memcmp(blk, ct192, 16) != 0) return 0;
    if (zan_aes_ecb_block_ni(key256, 256, pt, blk) != 0) return 0;
    if (memcmp(blk, ct256, 16) != 0) return 0;

    int64_t n = zan_aes_cbc_encrypt_ni(cbc_pt, 64, cbc_key, 128, cbc_iv, out);
    if (n != 80 || memcmp(out, cbc_ct128, 64) != 0) return 0;
    uint8_t back[80];
    n = zan_aes_cbc_decrypt_ni(out, 80, cbc_key, 128, cbc_iv, back);
    if (n != 64 || memcmp(back, cbc_pt, 64) != 0) return 0;

    n = zan_aes_cbc_encrypt_ni(cbc_pt, 64, cbc_key256, 256, cbc_iv, out);
    if (n != 80 || memcmp(out, cbc_ct256_1, 16) != 0) return 0;
    n = zan_aes_cbc_decrypt_ni(out, 80, cbc_key256, 256, cbc_iv, back);
    if (n != 64 || memcmp(back, cbc_pt, 64) != 0) return 0;

    uint8_t ctrb[16];
    memcpy(ctrb, ctr_iv, 16);
    n = zan_aes_ctr_ni(cbc_pt, 32, cbc_key, 128, ctrb, out);
    if (n != 32 || memcmp(out, ctr_ct, 32) != 0) return 0;
    return 1;
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    uint8_t out[80];
    uint8_t blk[16];

    if (zan_aes_ecb_block_arm(key128, 128, pt, blk) != 0) return 0;
    if (memcmp(blk, ct128, 16) != 0) return 0;
    if (zan_aes_ecb_block_arm(key192, 192, pt, blk) != 0) return 0;
    if (memcmp(blk, ct192, 16) != 0) return 0;
    if (zan_aes_ecb_block_arm(key256, 256, pt, blk) != 0) return 0;
    if (memcmp(blk, ct256, 16) != 0) return 0;

    int64_t n = zan_aes_cbc_encrypt_arm(cbc_pt, 64, cbc_key, 128, cbc_iv, out);
    if (n != 80 || memcmp(out, cbc_ct128, 64) != 0) return 0;
    uint8_t back[80];
    n = zan_aes_cbc_decrypt_arm(out, 80, cbc_key, 128, cbc_iv, back);
    if (n != 64 || memcmp(back, cbc_pt, 64) != 0) return 0;

    n = zan_aes_cbc_encrypt_arm(cbc_pt, 64, cbc_key256, 256, cbc_iv, out);
    if (n != 80 || memcmp(out, cbc_ct256_1, 16) != 0) return 0;
    n = zan_aes_cbc_decrypt_arm(out, 80, cbc_key256, 256, cbc_iv, back);
    if (n != 64 || memcmp(back, cbc_pt, 64) != 0) return 0;

    uint8_t ctrb[16];
    memcpy(ctrb, ctr_iv, 16);
    n = zan_aes_ctr_arm(cbc_pt, 32, cbc_key, 128, ctrb, out);
    if (n != 32 || memcmp(out, ctr_ct, 32) != 0) return 0;
    return 1;
#else
    (void)pt; (void)key128; (void)key192; (void)key256;
    (void)ct128; (void)ct192; (void)ct256;
    (void)cbc_key; (void)cbc_iv; (void)cbc_pt; (void)cbc_ct128;
    (void)cbc_key256; (void)cbc_ct256_1; (void)ctr_iv; (void)ctr_ct;
    return 1;
#endif
}

static int zan_ghash_kat(void) {
    /* Two GHASH steps checked against the SP 800-38D bit loop, frozen as
     * constants (H, X1, X2 and the expected accumulator). */
    static const uint8_t H[16]  = { 0x03,0x14,0x25,0x36,0x47,0x58,0x69,0x7a,0x8b,0x9c,0xad,0xbe,0xcf,0xe0,0xf1,0x02 };
    static const uint8_t X1[16] = { 0x07,0x24,0x41,0x5e,0x7b,0x98,0xb5,0xd2,0xef,0x0c,0x29,0x46,0x63,0x80,0x9d,0xba };
    static const uint8_t X2[16] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x80 };
    static const uint8_t want[16] = { 0x57,0xe7,0xfc,0x2f,0x3a,0xe6,0xd8,0x6a,0x99,0x19,0x76,0x64,0x6a,0x70,0x7e,0xe2 };
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    uint8_t y[16] = {0};
    if (zan_ghash_block_clmul(H, X1, y) != 0) return 0;
    if (zan_ghash_block_clmul(H, X2, y) != 0) return 0;
    return memcmp(y, want, 16) == 0;
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    uint8_t y[16] = {0};
    if (zan_ghash_block_pmull(H, X1, y) != 0) return 0;
    if (zan_ghash_block_pmull(H, X2, y) != 0) return 0;
    return memcmp(y, want, 16) == 0;
#else
    (void)H; (void)X1; (void)X2; (void)want;
    return 1;
#endif
}

static int zan_crc32c_kat(void) {
    /* RFC 4960 B.8: CRC-32C("123456789") = 0xE3069283 */
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    /* the kernel is a continuation (no final complement): raw state = ~E3069283 */
    return zan_crc32c_sse42(0xFFFFFFFFu, (const uint8_t*)"123456789", 9) == 0x1CF96D7Cu;
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    return zan_crc32c_pmull_arm(0xFFFFFFFFu, (const uint8_t*)"123456789", 9) == 0x1CF96D7Cu;
#else
    return 1;
#endif
}

int64_t zan_hw_aes_cbc_encrypt(const uint8_t *in, int64_t len,
                               const uint8_t *key, int keybits,
                               const uint8_t *iv, uint8_t *out) {
    if (len < 0 || !in || !key || !iv || !out) return -1;
    if (keybits != 128 && keybits != 192 && keybits != 256) return -1;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_gate(&g_gate_aes, zan_hw_has_aesni(), zan_aes_kat)) {
        return zan_aes_cbc_encrypt_ni(in, len, key, keybits, iv, out);
    }
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_gate(&g_gate_aes, zan_hw_arm_aes(), zan_aes_kat)) {
        return zan_aes_cbc_encrypt_arm(in, len, key, keybits, iv, out);
    }
#endif
    return -1;
}

int64_t zan_hw_aes_cbc_decrypt(const uint8_t *in, int64_t len,
                               const uint8_t *key, int keybits,
                               const uint8_t *iv, uint8_t *out) {
    if (len <= 0 || !in || !key || !iv || !out) return -1;
    if (keybits != 128 && keybits != 192 && keybits != 256) return -1;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_gate(&g_gate_aes, zan_hw_has_aesni(), zan_aes_kat)) {
        return zan_aes_cbc_decrypt_ni(in, len, key, keybits, iv, out);
    }
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_gate(&g_gate_aes, zan_hw_arm_aes(), zan_aes_kat)) {
        return zan_aes_cbc_decrypt_arm(in, len, key, keybits, iv, out);
    }
#endif
    return -1;
}

int64_t zan_hw_aes_ecb_block(const uint8_t *key, int keybits,
                             const uint8_t *in16, uint8_t *out16) {
    if (!key || !in16 || !out16) return -1;
    if (keybits != 128 && keybits != 192 && keybits != 256) return -1;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_gate(&g_gate_aes, zan_hw_has_aesni(), zan_aes_kat)) {
        return zan_aes_ecb_block_ni(key, keybits, in16, out16);
    }
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_gate(&g_gate_aes, zan_hw_arm_aes(), zan_aes_kat)) {
        return zan_aes_ecb_block_arm(key, keybits, in16, out16);
    }
#endif
    return -1;
}

int64_t zan_hw_aes_ctr_crypt(const uint8_t *in, int64_t len,
                             const uint8_t *key, int keybits,
                             uint8_t *counter16, uint8_t *out) {
    if (len < 0 || !in || !key || !counter16 || !out) return -1;
    if (keybits != 128 && keybits != 192 && keybits != 256) return -1;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_gate(&g_gate_aes, zan_hw_has_aesni(), zan_aes_kat)) {
        return zan_aes_ctr_ni(in, len, key, keybits, counter16, out);
    }
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_gate(&g_gate_aes, zan_hw_arm_aes(), zan_aes_kat)) {
        return zan_aes_ctr_arm(in, len, key, keybits, counter16, out);
    }
#endif
    return -1;
}

int64_t zan_hw_ghash_block(const uint8_t *h16, const uint8_t *x16, uint8_t *y16) {
    if (!h16 || !x16 || !y16) return -1;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_gate(&g_gate_ghash, zan_hw_has_pclmul(), zan_ghash_kat)) {
        return zan_ghash_block_clmul(h16, x16, y16);
    }
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_gate(&g_gate_ghash, zan_hw_arm_pmull(), zan_ghash_kat)) {
        return zan_ghash_block_pmull(h16, x16, y16);
    }
#endif
    return -1;
}

int64_t zan_hw_crc32c_update(uint32_t crc, const uint8_t *p, int64_t len) {
    if (len < 0 || !p) return -1;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_gate(&g_gate_crc32c, zan_hw_has_sse42(), zan_crc32c_kat)) {
        return (int64_t)zan_crc32c_sse42(crc, p, len);
    }
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_gate(&g_gate_crc32c, zan_hw_arm_crc32(), zan_crc32c_kat)) {
        return (int64_t)zan_crc32c_pmull_arm(crc, p, len);
    }
#endif
    return -1;
}

/* ===== 4. Single-Cycle Intrinsics Implementation ===== */
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))

__attribute__((target("aes,sse4.1")))
void zan_hw_aes_encrypt(const void *val, const void *key, void *out) {
    __m128i v = _mm_loadu_si128((const __m128i*)val);
    __m128i k = _mm_loadu_si128((const __m128i*)key);
    _mm_storeu_si128((__m128i*)out, _mm_aesenc_si128(v, k));
}

__attribute__((target("aes,sse4.1")))
void zan_hw_aes_encrypt_last(const void *val, const void *key, void *out) {
    __m128i v = _mm_loadu_si128((const __m128i*)val);
    __m128i k = _mm_loadu_si128((const __m128i*)key);
    _mm_storeu_si128((__m128i*)out, _mm_aesenclast_si128(v, k));
}

__attribute__((target("aes,sse4.1")))
void zan_hw_aes_decrypt(const void *val, const void *key, void *out) {
    __m128i v = _mm_loadu_si128((const __m128i*)val);
    __m128i k = _mm_loadu_si128((const __m128i*)key);
    _mm_storeu_si128((__m128i*)out, _mm_aesdec_si128(v, k));
}

__attribute__((target("aes,sse4.1")))
void zan_hw_aes_decrypt_last(const void *val, const void *key, void *out) {
    __m128i v = _mm_loadu_si128((const __m128i*)val);
    __m128i k = _mm_loadu_si128((const __m128i*)key);
    _mm_storeu_si128((__m128i*)out, _mm_aesdeclast_si128(v, k));
}

__attribute__((target("aes,sse4.1")))
void zan_hw_aes_keygenassist(const void *val, uint8_t rcon, void *out) {
    __m128i v = _mm_loadu_si128((const __m128i*)val);
    __m128i res;
    switch (rcon) {
        case 0x01: res = _mm_aeskeygenassist_si128(v, 0x01); break;
        case 0x02: res = _mm_aeskeygenassist_si128(v, 0x02); break;
        case 0x04: res = _mm_aeskeygenassist_si128(v, 0x04); break;
        case 0x08: res = _mm_aeskeygenassist_si128(v, 0x08); break;
        case 0x10: res = _mm_aeskeygenassist_si128(v, 0x10); break;
        case 0x20: res = _mm_aeskeygenassist_si128(v, 0x20); break;
        case 0x40: res = _mm_aeskeygenassist_si128(v, 0x40); break;
        case 0x80: res = _mm_aeskeygenassist_si128(v, 0x80); break;
        case 0x1b: res = _mm_aeskeygenassist_si128(v, 0x1b); break;
        case 0x36: res = _mm_aeskeygenassist_si128(v, 0x36); break;
        default:   res = _mm_aeskeygenassist_si128(v, 0x00); break;
    }
    _mm_storeu_si128((__m128i*)out, res);
}

__attribute__((target("aes,sse4.1")))
void zan_hw_aes_imc(const void *val, void *out) {
    __m128i v = _mm_loadu_si128((const __m128i*)val);
    _mm_storeu_si128((__m128i*)out, _mm_aesimc_si128(v));
}

__attribute__((target("sse2")))
void zan_hw_vec128_xor(const void *a, const void *b, void *out) {
    __m128i va = _mm_loadu_si128((const __m128i*)a);
    __m128i vb = _mm_loadu_si128((const __m128i*)b);
    _mm_storeu_si128((__m128i*)out, _mm_xor_si128(va, vb));
}

__attribute__((target("sse2")))
void zan_hw_vec128_load(const void *addr, void *out) {
    __m128i v = _mm_loadu_si128((const __m128i*)addr);
    _mm_storeu_si128((__m128i*)out, v);
}

__attribute__((target("sse2")))
void zan_hw_vec128_store(void *addr, const void *val) {
    __m128i v = _mm_loadu_si128((const __m128i*)val);
    _mm_storeu_si128((__m128i*)addr, v);
}

#else

void zan_hw_aes_encrypt(const void *val, const void *key, void *out) { (void)val; (void)key; (void)out; }
void zan_hw_aes_encrypt_last(const void *val, const void *key, void *out) { (void)val; (void)key; (void)out; }
void zan_hw_aes_decrypt(const void *val, const void *key, void *out) { (void)val; (void)key; (void)out; }
void zan_hw_aes_decrypt_last(const void *val, const void *key, void *out) { (void)val; (void)key; (void)out; }
void zan_hw_aes_keygenassist(const void *val, uint8_t rcon, void *out) { (void)val; (void)rcon; (void)out; }
void zan_hw_aes_imc(const void *val, void *out) { (void)val; (void)out; }
void zan_hw_vec128_xor(const void *a, const void *b, void *out) { (void)a; (void)b; (void)out; }
void zan_hw_vec128_load(const void *addr, void *out) { (void)addr; (void)out; }
void zan_hw_vec128_store(void *addr, const void *val) { (void)addr; (void)val; }

#endif

/* ===== 5. SIMD-Accelerated PixelOps ===== */
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))

__attribute__((target("sse2")))
static void pixel_blend_over_sse2(uint8_t *dst, const uint8_t *src, int64_t count) {
    int64_t i = 0;
    __m128i zero = _mm_setzero_si128();
    __m128i k255 = _mm_set1_epi16(255);
    __m128i k128 = _mm_set1_epi16(128);
    __m128i rgb_mask = _mm_setr_epi16(-1, -1, -1, 0, -1, -1, -1, 0);
    __m128i k_alpha_255 = _mm_setr_epi16(0, 0, 0, 255, 0, 0, 0, 255);

    for (; i + 4 <= count; i += 4) {
        __m128i s = _mm_loadu_si128((const __m128i*)(src + i * 4));
        __m128i d = _mm_loadu_si128((const __m128i*)(dst + i * 4));

        __m128i s_lo = _mm_unpacklo_epi8(s, zero);
        __m128i d_lo = _mm_unpacklo_epi8(d, zero);
        __m128i s_hi = _mm_unpackhi_epi8(s, zero);
        __m128i d_hi = _mm_unpackhi_epi8(d, zero);

        __m128i sa_0 = _mm_shufflelo_epi16(s_lo, _MM_SHUFFLE(3, 3, 3, 3));
        sa_0 = _mm_shufflehi_epi16(sa_0, _MM_SHUFFLE(3, 3, 3, 3));
        __m128i inv_sa_0 = _mm_sub_epi16(k255, sa_0);
        __m128i sw_0 = _mm_or_si128(_mm_and_si128(sa_0, rgb_mask), k_alpha_255);

        __m128i res_lo = _mm_add_epi16(_mm_mullo_epi16(s_lo, sw_0), _mm_mullo_epi16(d_lo, inv_sa_0));
        res_lo = _mm_add_epi16(res_lo, k128);
        res_lo = _mm_srli_epi16(_mm_add_epi16(res_lo, _mm_srli_epi16(res_lo, 8)), 8);

        __m128i sa_1 = _mm_shufflelo_epi16(s_hi, _MM_SHUFFLE(3, 3, 3, 3));
        sa_1 = _mm_shufflehi_epi16(sa_1, _MM_SHUFFLE(3, 3, 3, 3));
        __m128i inv_sa_1 = _mm_sub_epi16(k255, sa_1);
        __m128i sw_1 = _mm_or_si128(_mm_and_si128(sa_1, rgb_mask), k_alpha_255);

        __m128i res_hi = _mm_add_epi16(_mm_mullo_epi16(s_hi, sw_1), _mm_mullo_epi16(d_hi, inv_sa_1));
        res_hi = _mm_add_epi16(res_hi, k128);
        res_hi = _mm_srli_epi16(_mm_add_epi16(res_hi, _mm_srli_epi16(res_hi, 8)), 8);

        __m128i out = _mm_packus_epi16(res_lo, res_hi);
        _mm_storeu_si128((__m128i*)(dst + i * 4), out);
    }
    for (; i < count; i++) {
        uint32_t sp, dp;
        memcpy(&sp, src + i * 4, 4);
        memcpy(&dp, dst + i * 4, 4);
        uint32_t sa = (sp >> 24) & 0xFF;
        uint32_t da = (dp >> 24) & 0xFF;
        uint32_t inv_a = 255 - sa;
        uint32_t out_a = sa + (da * inv_a + 127) / 255;
        uint32_t sr = (sp >> 16) & 0xFF, dr = (dp >> 16) & 0xFF;
        uint32_t sg = (sp >> 8)  & 0xFF, dg = (dp >> 8)  & 0xFF;
        uint32_t sb = sp & 0xFF,         db = dp & 0xFF;
        uint32_t out_r = (sr * sa + dr * inv_a + 127) / 255;
        uint32_t out_g = (sg * sa + dg * inv_a + 127) / 255;
        uint32_t out_b = (sb * sa + db * inv_a + 127) / 255;
        uint32_t outp = (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
        memcpy(dst + i * 4, &outp, 4);
    }
}

__attribute__((target("ssse3")))
static void pixel_swap_rb_ssse3(uint8_t *dst, const uint8_t *src, int64_t count) {
    int64_t i = 0;
    __m128i mask = _mm_setr_epi8(2, 1, 0, 3,  6, 5, 4, 7,  10, 9, 8, 11,  14, 13, 12, 15);
    for (; i + 4 <= count; i += 4) {
        __m128i v = _mm_loadu_si128((const __m128i*)(src + i * 4));
        v = _mm_shuffle_epi8(v, mask);
        _mm_storeu_si128((__m128i*)(dst + i * 4), v);
    }
    for (; i < count; i++) {
        uint32_t p;
        memcpy(&p, src + i * 4, 4);
        uint32_t swapped = (p & 0xFF00FF00u) | ((p & 0x00FF0000u) >> 16) | ((p & 0x000000FFu) << 16);
        memcpy(dst + i * 4, &swapped, 4);
    }
}

#endif

void zan_hw_pixel_blend_over(uint8_t *dst, const uint8_t *src, int64_t count) {
    if (count <= 0 || !dst || !src) return;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    pixel_blend_over_sse2(dst, src, count);
#else
    for (int64_t i = 0; i < count; i++) {
        uint32_t sp, dp;
        memcpy(&sp, src + i * 4, 4);
        memcpy(&dp, dst + i * 4, 4);
        uint32_t sa = (sp >> 24) & 0xFF;
        uint32_t da = (dp >> 24) & 0xFF;
        uint32_t inv_a = 255 - sa;
        uint32_t out_a = sa + (da * inv_a + 127) / 255;
        uint32_t sr = (sp >> 16) & 0xFF, dr = (dp >> 16) & 0xFF;
        uint32_t sg = (sp >> 8)  & 0xFF, dg = (dp >> 8)  & 0xFF;
        uint32_t sb = sp & 0xFF,         db = dp & 0xFF;
        uint32_t out_r = (sr * sa + dr * inv_a + 127) / 255;
        uint32_t out_g = (sg * sa + dg * inv_a + 127) / 255;
        uint32_t out_b = (sb * sa + db * inv_a + 127) / 255;
        uint32_t outp = (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
        memcpy(dst + i * 4, &outp, 4);
    }
#endif
}

void zan_hw_pixel_swap_rb(uint8_t *dst, const uint8_t *src, int64_t count) {
    if (count <= 0 || !dst || !src) return;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    pixel_swap_rb_ssse3(dst, src, count);
#else
    for (int64_t i = 0; i < count; i++) {
        uint32_t p;
        memcpy(&p, src + i * 4, 4);
        uint32_t swapped = (p & 0xFF00FF00u) | ((p & 0x00FF0000u) >> 16) | ((p & 0x000000FFu) << 16);
        memcpy(dst + i * 4, &swapped, 4);
    }
#endif
}

void zan_hw_pixel_fill_rect(uint8_t *dst, int64_t stride, int64_t x, int64_t y,
                            int64_t w, int64_t h, uint32_t color) {
    if (w <= 0 || h <= 0 || !dst) return;
    for (int64_t r = 0; r < h; r++) {
        uint32_t *row = (uint32_t*)(dst + (y + r) * stride + x * 4);
        for (int64_t c = 0; c < w; c++) {
            row[c] = color;
        }
    }
}

static inline uint32_t bilerp_ch(uint32_t c00, uint32_t c01, uint32_t c10, uint32_t c11,
                                 uint32_t wx, uint32_t inv_wx, uint32_t wy, uint32_t inv_wy) {
    uint32_t top = (c00 * inv_wx + c01 * wx + 128) >> 8;
    uint32_t bot = (c10 * inv_wx + c11 * wx + 128) >> 8;
    return (top * inv_wy + bot * wy + 128) >> 8;
}

void zan_hw_pixel_resample_bilinear_row(uint8_t *dst, const uint8_t *src0,
                                       const uint8_t *src1, const int32_t *x_idx,
                                       const int32_t *x_wt, int32_t wy, int64_t width) {
    if (width <= 0 || !dst || !src0 || !src1 || !x_idx || !x_wt) return;
    uint32_t *dst_px = (uint32_t*)dst;
    const uint32_t *s0 = (const uint32_t*)src0;
    const uint32_t *s1 = (const uint32_t*)src1;
    uint32_t inv_wy = 256 - (uint32_t)wy;

    for (int64_t c = 0; c < width; c++) {
        int64_t sx = (int64_t)x_idx[c];
        uint32_t wx = (uint32_t)x_wt[c];
        uint32_t inv_wx = 256 - wx;

        uint32_t p00 = s0[sx];
        uint32_t p01 = s0[sx + 1];
        uint32_t p10 = s1[sx];
        uint32_t p11 = s1[sx + 1];

        uint32_t a = bilerp_ch((p00 >> 24) & 0xFF, (p01 >> 24) & 0xFF, (p10 >> 24) & 0xFF, (p11 >> 24) & 0xFF, wx, inv_wx, wy, inv_wy);
        uint32_t r = bilerp_ch((p00 >> 16) & 0xFF, (p01 >> 16) & 0xFF, (p10 >> 16) & 0xFF, (p11 >> 16) & 0xFF, wx, inv_wx, wy, inv_wy);
        uint32_t g = bilerp_ch((p00 >> 8)  & 0xFF, (p01 >> 8)  & 0xFF, (p10 >> 8)  & 0xFF, (p11 >> 8)  & 0xFF, wx, inv_wx, wy, inv_wy);
        uint32_t b = bilerp_ch(p00 & 0xFF,         p01 & 0xFF,         p10 & 0xFF,         p11 & 0xFF,         wx, inv_wx, wy, inv_wy);

        dst_px[c] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}

#if (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))

/* ===== 7. SM3 Cryptographic Hash (GB/T 32905-2012), FEAT_SM3 =============
 * Per-round hardware:
 *   sm3ss1              -> SS1 = rotl(rotl(A,12) + E + W[j] + rotl(T_j,j), 7)
 *   sm3tt1a/tt1b (imm2) -> TT1 with the A/B/C/D rotation folded in
 *                          (FF = X^Y^Z for rounds 0-15, majority after);
 *   sm3tt2a/tt2b (imm2) -> TT2 with the P0 compression folded in
 *                          (GG = X^Y^Z for rounds 0-15, choice after);
 *   sm3partw1/partw2    -> W[j] = P1(W[j-16]^W[j-9]^rotl(W[j-3],15))
 *                          ^ rotl(W[j-13],7) ^ W[j-6].
 * The TT instructions keep the working variables REVERSED — the A-side
 * vector is {D,C,B,A}, the E-side {H,G,F,E} — and read A/E/SS1 at lane 3
 * while imm2 selects the W/W' lane, so the per-round K+W[j] sum rides a
 * vext rotation of the group's constant vector. */
static const uint32_t zan_sm3_kc_arm[64] = {
    0x79cc4519u,    0xf3988a32u,    0xe7311465u,    0xce6228cbu,
    0x9cc45197u,    0x3988a32fu,    0x7311465eu,    0xe6228cbcu,
    0xcc451979u,    0x988a32f3u,    0x311465e7u,    0x6228cbceu,
    0xc451979cu,    0x88a32f39u,    0x11465e73u,    0x228cbce6u,
    0x9d8a7a87u,    0x3b14f50fu,    0x7629ea1eu,    0xec53d43cu,
    0xd8a7a879u,    0xb14f50f3u,    0x629ea1e7u,    0xc53d43ceu,
    0x8a7a879du,    0x14f50f3bu,    0x29ea1e76u,    0x53d43cecu,
    0xa7a879d8u,    0x4f50f3b1u,    0x9ea1e762u,    0x3d43cec5u,
    0x7a879d8au,    0xf50f3b14u,    0xea1e7629u,    0xd43cec53u,
    0xa879d8a7u,    0x50f3b14fu,    0xa1e7629eu,    0x43cec53du,
    0x879d8a7au,    0x0f3b14f5u,    0x1e7629eau,    0x3cec53d4u,
    0x79d8a7a8u,    0xf3b14f50u,    0xe7629ea1u,    0xcec53d43u,
    0x9d8a7a87u,    0x3b14f50fu,    0x7629ea1eu,    0xec53d43cu,
    0xd8a7a879u,    0xb14f50f3u,    0x629ea1e7u,    0xc53d43ceu,
    0x8a7a879du,    0x14f50f3bu,    0x29ea1e76u,    0x53d43cecu,
    0xa7a879d8u,    0x4f50f3b1u,    0x9ea1e762u,    0x3d43cec5u,
};

__attribute__((target("sm4")))
static void zan_sm3_transform_arm(uint32_t state[8], const uint8_t *data, size_t num_blocks) {
    uint32x4_t rev = vrev64q_u32(vld1q_u32(state));
    uint32x4_t avec = vextq_u32(rev, rev, 2);                       /* {D,C,B,A} */
    rev = vrev64q_u32(vld1q_u32(state + 4));
    uint32x4_t evec = vextq_u32(rev, rev, 2);                       /* {H,G,F,E} */
    uint32x4_t ss1;

    for (size_t blk = 0; blk < num_blocks; blk++, data += 64) {
        uint32x4_t a0 = avec, e0 = evec;
        uint32x4_t W[4];
        for (int i = 0; i < 4; i++) {
            W[i] = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(data + 16 * i)));
        }

        for (int g = 0; g < 16; g++) {
            uint32x4_t cur = W[g % 4];
            uint32x4_t wprime = veorq_u32(cur, W[(g + 1) % 4]);
            uint32x4_t kc = vld1q_u32(&zan_sm3_kc_arm[4 * g]);

            /* Extend the schedule by four words into the rolling slot.
             * Group 15 produces W[64..67], which only W'[60..63] reads. */
            uint32x4_t n1 = vextq_u32(W[(g + 1) % 4], W[(g + 2) % 4], 3);
            uint32x4_t n2 = vextq_u32(W[(g + 2) % 4], W[(g + 3) % 4], 2);
            uint32x4_t m2 = vextq_u32(W[g % 4], W[(g + 1) % 4], 3);
            uint32x4_t ext = vsm3partw1q_u32(W[g % 4], n1, W[(g + 3) % 4]);
            W[g % 4] = vsm3partw2q_u32(ext, n2, m2);

            /* Rounds 0-15 run the XOR boolean form (tt1a/tt2a), rounds 16-63
             * the majority/choice form (tt1b/tt2b); imm2 stays per-lane. */
#define ZAN_SM3_ROUND(l)                                                    \
            ss1 = vsm3ss1q_u32(avec, evec, vextq_u32(kc, kc, ((l) + 1) & 3)); \
            if (g < 4) {                                                    \
                avec = vsm3tt1aq_u32(avec, ss1, wprime, (l));               \
                evec = vsm3tt2aq_u32(evec, ss1, cur, (l));                  \
            } else {                                                        \
                avec = vsm3tt1bq_u32(avec, ss1, wprime, (l));               \
                evec = vsm3tt2bq_u32(evec, ss1, cur, (l));                  \
            }
            ZAN_SM3_ROUND(0)
            ZAN_SM3_ROUND(1)
            ZAN_SM3_ROUND(2)
            ZAN_SM3_ROUND(3)
#undef ZAN_SM3_ROUND
        }

        avec = veorq_u32(avec, a0);
        evec = veorq_u32(evec, e0);
        vst1q_u32(state, vextq_u32(vrev64q_u32(avec), vrev64q_u32(avec), 2));
        vst1q_u32(state + 4, vextq_u32(vrev64q_u32(evec), vrev64q_u32(evec), 2));
    }
}

/* GB/T 32905-2012 appendix A sample: SM3("abc"). */
static int zan_sm3_kat_arm(void) {
    static const uint8_t expect[32] = {
        0x66, 0xc7, 0xf0, 0xf4, 0x62, 0xee, 0xed, 0xd9,
        0xd1, 0xf2, 0xd4, 0x6b, 0xdc, 0x10, 0xe4, 0xe2,
        0x41, 0x67, 0xc4, 0x87, 0x5c, 0xf2, 0xf7, 0xa2,
        0x29, 0x7d, 0xa0, 0x2b, 0x8f, 0x4b, 0xa8, 0xe0
    };
    uint32_t st[8] = {
        0x7380166fu, 0x4914b2b9u, 0x172442d7u, 0xda8a0600u,
        0xa96f30bcu, 0x163138aau, 0xe38dee4du, 0xb0fb0e4eu
    };
    uint8_t tail[64];
    tail[0] = 'a'; tail[1] = 'b'; tail[2] = 'c';
    tail[3] = 0x80;
    memset(tail + 4, 0, 52);
    uint64_t bits = 24;
    for (int i = 0; i < 8; i++) {
        tail[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    }
    zan_sm3_transform_arm(st, tail, 1);
    uint8_t out[32];
    for (int i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(st[i] >> 24);
        out[i*4+1] = (uint8_t)(st[i] >> 16);
        out[i*4+2] = (uint8_t)(st[i] >> 8);
        out[i*4+3] = (uint8_t)(st[i]);
    }
    return memcmp(out, expect, 32) == 0 ? 1 : -1;
}
#endif /* aarch64 SM3 */

/* ===== 7. SM3 driver (GB/T 32905-2012) ===================================
 * x86 has no SM3 instructions; FEAT_SM3 covers ARM64. Everywhere else the
 * pure-Zan Sm3 class is the implementation.
 * ======================================================================== */
/* SHA-512 has no x86 hardware engine (SHA extensions cover SHA-1/256 only);
 * the FEAT_SHA512 kernel lands with the ARM64 pass. Pure-Zan covers x86. */
int64_t zan_hw_sha512(const uint8_t *data, int64_t len, uint8_t out[64]) {
    (void)data; (void)len; (void)out;
    return -1;
}

int64_t zan_hw_sm3(const uint8_t *data, int64_t len, uint8_t out[32]) {
    if (len < 0) len = 0;
    uint32_t state[8] = {
        0x7380166f, 0x4914b2b9, 0x172442d7, 0xda8a0600,
        0xa96f30bc, 0x163138aa, 0xe38dee4d, 0xb0fb0e4e
    };

    size_t full_blocks = (size_t)len / 64;
    int use_ni = 0;
#if (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    use_ni = zan_hw_gate(&g_gate_sm3, zan_hw_arm_sm3(), zan_sm3_kat_arm);
#endif
    if (!use_ni) return -1;

    if (full_blocks > 0 && data) {
#if (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
        zan_sm3_transform_arm(state, data, full_blocks);
#endif
    }

    // Stack tail padding: fixed 128 bytes
    uint8_t tail[128];
    size_t rem = (size_t)len % 64;
    if (rem > 0 && data) {
        memcpy(tail, data + full_blocks * 64, rem);
    }
    tail[rem] = 0x80;
    size_t pad_blocks = (rem >= 56) ? 2 : 1;
    size_t total_tail = pad_blocks * 64;
    memset(tail + rem + 1, 0, total_tail - rem - 1);
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) {
        tail[total_tail - 8 + i] = (uint8_t)(bits >> (56 - 8 * i));
    }
#if (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    zan_sm3_transform_arm(state, tail, pad_blocks);
#endif

    for (int i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(state[i] >> 24);
        out[i*4+1] = (uint8_t)(state[i] >> 16);
        out[i*4+2] = (uint8_t)(state[i] >> 8);
        out[i*4+3] = (uint8_t)(state[i]);
    }
    return 0;
}

static inline uint32_t zan_rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

/* ===== 8. SM4 Block Cipher CBC Acceleration (GB/T 32907-2016) =====
 * Accelerated by 4x 32-bit precomputed T-Tables, folding S-Box and linear diffusion L
 * into 4 lookups + XORs per round. Zero stack overhead, full register pipeline.
 */
static inline uint32_t zan_sm4_lp(uint32_t b) {
    return b ^ zan_rotl32(b, 13) ^ zan_rotl32(b, 23);
}

static void zan_sm4_set_key(uint32_t rk[32], const uint8_t key[16]) {
    uint32_t K[36];
    for (int i = 0; i < 4; i++) {
        uint32_t w = ((uint32_t)key[i*4 + 0] << 24) |
                     ((uint32_t)key[i*4 + 1] << 16) |
                     ((uint32_t)key[i*4 + 2] << 8)  |
                     ((uint32_t)key[i*4 + 3]);
        K[i] = w ^ SM4_FK[i];
    }
    for (int i = 0; i < 32; i++) {
        uint32_t x = K[i+1] ^ K[i+2] ^ K[i+3] ^ SM4_CK[i];
        uint32_t tau = ((uint32_t)SM4_SBOX[(x >> 24) & 0xff] << 24) |
                       ((uint32_t)SM4_SBOX[(x >> 16) & 0xff] << 16) |
                       ((uint32_t)SM4_SBOX[(x >> 8) & 0xff] << 8)   |
                       ((uint32_t)SM4_SBOX[x & 0xff]);
        K[i+4] = K[i] ^ zan_sm4_lp(tau);
        rk[i] = K[i+4];
    }
}

static inline void zan_sm4_crypt_block(const uint32_t rk[32], const uint8_t in[16], uint8_t out[16]) {
    uint32_t x0 = ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) | in[3];
    uint32_t x1 = ((uint32_t)in[4] << 24) | ((uint32_t)in[5] << 16) | ((uint32_t)in[6] << 8) | in[7];
    uint32_t x2 = ((uint32_t)in[8] << 24) | ((uint32_t)in[9] << 16) | ((uint32_t)in[10] << 8) | in[11];
    uint32_t x3 = ((uint32_t)in[12] << 24) | ((uint32_t)in[13] << 16) | ((uint32_t)in[14] << 8) | in[15];

    for (int i = 0; i < 32; i += 4) {
        uint32_t b = x1 ^ x2 ^ x3 ^ rk[i+0];
        x0 ^= SM4_T0[(b >> 24) & 0xff] ^ SM4_T1[(b >> 16) & 0xff] ^ SM4_T2[(b >> 8) & 0xff] ^ SM4_T3[b & 0xff];

        b = x2 ^ x3 ^ x0 ^ rk[i+1];
        x1 ^= SM4_T0[(b >> 24) & 0xff] ^ SM4_T1[(b >> 16) & 0xff] ^ SM4_T2[(b >> 8) & 0xff] ^ SM4_T3[b & 0xff];

        b = x3 ^ x0 ^ x1 ^ rk[i+2];
        x2 ^= SM4_T0[(b >> 24) & 0xff] ^ SM4_T1[(b >> 16) & 0xff] ^ SM4_T2[(b >> 8) & 0xff] ^ SM4_T3[b & 0xff];

        b = x0 ^ x1 ^ x2 ^ rk[i+3];
        x3 ^= SM4_T0[(b >> 24) & 0xff] ^ SM4_T1[(b >> 16) & 0xff] ^ SM4_T2[(b >> 8) & 0xff] ^ SM4_T3[b & 0xff];
    }

    out[0] = (uint8_t)(x3 >> 24); out[1] = (uint8_t)(x3 >> 16); out[2] = (uint8_t)(x3 >> 8); out[3] = (uint8_t)x3;
    out[4] = (uint8_t)(x2 >> 24); out[5] = (uint8_t)(x2 >> 16); out[6] = (uint8_t)(x2 >> 8); out[7] = (uint8_t)x2;
    out[8] = (uint8_t)(x1 >> 24); out[9] = (uint8_t)(x1 >> 16); out[10] = (uint8_t)(x1 >> 8); out[11] = (uint8_t)x1;
    out[12] = (uint8_t)(x0 >> 24); out[13] = (uint8_t)(x0 >> 16); out[14] = (uint8_t)(x0 >> 8); out[15] = (uint8_t)x0;
}

int64_t zan_hw_sm4_cbc_encrypt(const uint8_t *in, int64_t len,
                               const uint8_t *key, const uint8_t *iv,
                               uint8_t *out) {
    if (len < 0 || !in || !key || !iv || !out) return -1;
    uint32_t rk[32];
    zan_sm4_set_key(rk, key);

    size_t blocks = (size_t)len / 16;
    uint8_t pad_val = (uint8_t)(16 - ((size_t)len % 16));
    size_t total_len = (blocks + 1) * 16;

    uint8_t prev[16];
    memcpy(prev, iv, 16);

    uint8_t block[16];
    for (size_t b = 0; b < blocks; b++) {
        for (int i = 0; i < 16; i++) {
            block[i] = in[b * 16 + i] ^ prev[i];
        }
        zan_sm4_crypt_block(rk, block, out + b * 16);
        memcpy(prev, out + b * 16, 16);
    }

    size_t rem = (size_t)len % 16;
    for (size_t i = 0; i < rem; i++) {
        block[i] = in[blocks * 16 + i] ^ prev[i];
    }
    for (size_t i = rem; i < 16; i++) {
        block[i] = pad_val ^ prev[i];
    }
    zan_sm4_crypt_block(rk, block, out + blocks * 16);
    return (int64_t)total_len;
}

int64_t zan_hw_sm4_cbc_decrypt(const uint8_t *in, int64_t len,
                               const uint8_t *key, const uint8_t *iv,
                               uint8_t *out) {
    if (len <= 0 || (len % 16) != 0 || !in || !key || !iv || !out) return -1;
    uint32_t rk[32];
    zan_sm4_set_key(rk, key);

    uint32_t rk_dec[32];
    for (int i = 0; i < 32; i++) {
        rk_dec[i] = rk[31 - i];
    }

    size_t blocks = (size_t)len / 16;
    uint8_t prev[16];
    memcpy(prev, iv, 16);

    uint8_t block[16];
    for (size_t b = 0; b < blocks; b++) {
        uint8_t cur[16];
        memcpy(cur, in + b * 16, 16);
        zan_sm4_crypt_block(rk_dec, cur, block);
        for (int i = 0; i < 16; i++) {
            out[b * 16 + i] = block[i] ^ prev[i];
        }
        memcpy(prev, cur, 16);
    }

    uint8_t pad_val = out[len - 1];
    if (pad_val == 0 || pad_val > 16) return -1;
    for (int i = 0; i < pad_val; i++) {
        if (out[len - 1 - i] != pad_val) return -1;
    }
    return len - pad_val;
}

/* ===== 7. Base64 High-Throughput SIMD / Pipelined Encoders (RFC 4648) ===== */

static const char ZAN_B64_ENC_TABLE[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static uint8_t ZAN_B64_DEC_TABLE[256];
static int zan_b64_dec_inited = 0;

static void zan_b64_init_dec_table(void) {
    if (zan_b64_dec_inited) return;
    memset(ZAN_B64_DEC_TABLE, 0x80, 256);
    for (int i = 0; i < 64; i++) {
        ZAN_B64_DEC_TABLE[(uint8_t)ZAN_B64_ENC_TABLE[i]] = (uint8_t)i;
    }
    ZAN_B64_DEC_TABLE['='] = 0;
    zan_b64_dec_inited = 1;
}

int64_t zan_hw_base64_encode(const uint8_t *src, int64_t len, char *dst) {
    if (!src || len <= 0 || !dst) return 0;
    const uint8_t *s = src;
    char *d = dst;
    int64_t rem = len;

    /* 4-way unrolled pipeline: 12 input bytes -> 16 output characters */
    while (rem >= 12) {
        uint32_t w0 = ((uint32_t)s[0] << 16) | ((uint32_t)s[1] << 8) | s[2];
        uint32_t w1 = ((uint32_t)s[3] << 16) | ((uint32_t)s[4] << 8) | s[5];
        uint32_t w2 = ((uint32_t)s[6] << 16) | ((uint32_t)s[7] << 8) | s[8];
        uint32_t w3 = ((uint32_t)s[9] << 16) | ((uint32_t)s[10] << 8) | s[11];

        d[0]  = ZAN_B64_ENC_TABLE[(w0 >> 18) & 0x3F];
        d[1]  = ZAN_B64_ENC_TABLE[(w0 >> 12) & 0x3F];
        d[2]  = ZAN_B64_ENC_TABLE[(w0 >> 6)  & 0x3F];
        d[3]  = ZAN_B64_ENC_TABLE[w0 & 0x3F];

        d[4]  = ZAN_B64_ENC_TABLE[(w1 >> 18) & 0x3F];
        d[5]  = ZAN_B64_ENC_TABLE[(w1 >> 12) & 0x3F];
        d[6]  = ZAN_B64_ENC_TABLE[(w1 >> 6)  & 0x3F];
        d[7]  = ZAN_B64_ENC_TABLE[w1 & 0x3F];

        d[8]  = ZAN_B64_ENC_TABLE[(w2 >> 18) & 0x3F];
        d[9]  = ZAN_B64_ENC_TABLE[(w2 >> 12) & 0x3F];
        d[10] = ZAN_B64_ENC_TABLE[(w2 >> 6)  & 0x3F];
        d[11] = ZAN_B64_ENC_TABLE[w2 & 0x3F];

        d[12] = ZAN_B64_ENC_TABLE[(w3 >> 18) & 0x3F];
        d[13] = ZAN_B64_ENC_TABLE[(w3 >> 12) & 0x3F];
        d[14] = ZAN_B64_ENC_TABLE[(w3 >> 6)  & 0x3F];
        d[15] = ZAN_B64_ENC_TABLE[w3 & 0x3F];

        s += 12;
        d += 16;
        rem -= 12;
    }

    /* Scalar 3-byte loop */
    while (rem >= 3) {
        uint32_t w = ((uint32_t)s[0] << 16) | ((uint32_t)s[1] << 8) | s[2];
        d[0] = ZAN_B64_ENC_TABLE[(w >> 18) & 0x3F];
        d[1] = ZAN_B64_ENC_TABLE[(w >> 12) & 0x3F];
        d[2] = ZAN_B64_ENC_TABLE[(w >> 6)  & 0x3F];
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
        d[2] = ZAN_B64_ENC_TABLE[(w >> 6)  & 0x3F];
        d[3] = '=';
        d += 4;
    }

    *d = '\0';
    return (int64_t)(d - dst);
}

int64_t zan_hw_base64_decode(const char *src, int64_t len, uint8_t *dst) {
    if (!src || len <= 0 || !dst) return 0;
    zan_b64_init_dec_table();

    if ((len & 3) != 0) return -1;

    int pad = 0;
    if (len > 0 && src[len - 1] == '=') pad++;
    if (len > 1 && src[len - 2] == '=') pad++;

    int64_t plain_len = (len / 4) * 3 - pad;
    if (plain_len < 0) return -1;

    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = dst;
    int64_t rem = len - (pad ? 4 : 0);

    /* 4-way unrolled pipeline: 16 characters -> 12 bytes */
    while (rem >= 16) {
        uint8_t a0 = ZAN_B64_DEC_TABLE[s[0]],  b0 = ZAN_B64_DEC_TABLE[s[1]],  c0 = ZAN_B64_DEC_TABLE[s[2]],  d0 = ZAN_B64_DEC_TABLE[s[3]];
        uint8_t a1 = ZAN_B64_DEC_TABLE[s[4]],  b1 = ZAN_B64_DEC_TABLE[s[5]],  c1 = ZAN_B64_DEC_TABLE[s[6]],  d1 = ZAN_B64_DEC_TABLE[s[7]];
        uint8_t a2 = ZAN_B64_DEC_TABLE[s[8]],  b2 = ZAN_B64_DEC_TABLE[s[9]],  c2 = ZAN_B64_DEC_TABLE[s[10]], d2 = ZAN_B64_DEC_TABLE[s[11]];
        uint8_t a3 = ZAN_B64_DEC_TABLE[s[12]], b3 = ZAN_B64_DEC_TABLE[s[13]], c3 = ZAN_B64_DEC_TABLE[s[14]], d3 = ZAN_B64_DEC_TABLE[s[15]];

        if ((a0 | b0 | c0 | d0 | a1 | b1 | c1 | d1 | a2 | b2 | c2 | d2 | a3 | b3 | c3 | d3) & 0x80)
            return -1;

        uint32_t n0 = ((uint32_t)a0 << 18) | ((uint32_t)b0 << 12) | ((uint32_t)c0 << 6) | d0;
        uint32_t n1 = ((uint32_t)a1 << 18) | ((uint32_t)b1 << 12) | ((uint32_t)c1 << 6) | d1;
        uint32_t n2 = ((uint32_t)a2 << 18) | ((uint32_t)b2 << 12) | ((uint32_t)c2 << 6) | d2;
        uint32_t n3 = ((uint32_t)a3 << 18) | ((uint32_t)b3 << 12) | ((uint32_t)c3 << 6) | d3;

        d[0]  = (uint8_t)(n0 >> 16); d[1]  = (uint8_t)(n0 >> 8); d[2]  = (uint8_t)n0;
        d[3]  = (uint8_t)(n1 >> 16); d[4]  = (uint8_t)(n1 >> 8); d[5]  = (uint8_t)n1;
        d[6]  = (uint8_t)(n2 >> 16); d[7]  = (uint8_t)(n2 >> 8); d[8]  = (uint8_t)n2;
        d[9]  = (uint8_t)(n3 >> 16); d[10] = (uint8_t)(n3 >> 8); d[11] = (uint8_t)n3;

        s += 16;
        d += 12;
        rem -= 16;
    }

    while (rem >= 4) {
        uint8_t a = ZAN_B64_DEC_TABLE[s[0]];
        uint8_t b = ZAN_B64_DEC_TABLE[s[1]];
        uint8_t c = ZAN_B64_DEC_TABLE[s[2]];
        uint8_t dd = ZAN_B64_DEC_TABLE[s[3]];
        if ((a | b | c | dd) & 0x80) return -1;
        uint32_t n = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | dd;
        d[0] = (uint8_t)(n >> 16);
        d[1] = (uint8_t)(n >> 8);
        d[2] = (uint8_t)n;
        s += 4;
        d += 3;
        rem -= 4;
    }

    if (pad > 0) {
        uint8_t a = ZAN_B64_DEC_TABLE[s[0]];
        uint8_t b = ZAN_B64_DEC_TABLE[s[1]];
        uint8_t c = (pad == 2) ? 0 : ZAN_B64_DEC_TABLE[s[2]];
        uint8_t dd = 0;
        if ((a | b | c) & 0x80) return -1;
        uint32_t n = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | dd;
        if (pad == 1) {
            d[0] = (uint8_t)(n >> 16);
            d[1] = (uint8_t)(n >> 8);
            d += 2;
        } else if (pad == 2) {
            d[0] = (uint8_t)(n >> 16);
            d += 1;
        }
    }

    return (int64_t)(d - dst);
}

static inline int zan_ctz32_local(uint32_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctz(x);
#elif defined(_MSC_VER)
    unsigned long idx;
    _BitScanForward(&idx, x);
    return (int)idx;
#else
    int n = 0;
    while ((x & 1) == 0) { x >>= 1; n++; }
    return n;
#endif
}

/* ===== 9. SIMD JSON Structural Scanners ===== */
int64_t zan_hw_json_skip_whitespace(const uint8_t *buf, int64_t pos, int64_t len) {
    if (!buf || pos >= len) return pos;
    const uint8_t *p = buf;

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    __m128i m_sp = _mm_set1_epi8(' ');
    __m128i m_tb = _mm_set1_epi8('\t');
    __m128i m_nl = _mm_set1_epi8('\n');
    __m128i m_cr = _mm_set1_epi8('\r');

    while (pos + 16 <= len) {
        __m128i v = _mm_loadu_si128((const __m128i*)(p + pos));
        __m128i eq_sp = _mm_cmpeq_epi8(v, m_sp);
        __m128i eq_tb = _mm_cmpeq_epi8(v, m_tb);
        __m128i eq_nl = _mm_cmpeq_epi8(v, m_nl);
        __m128i eq_cr = _mm_cmpeq_epi8(v, m_cr);
        __m128i is_ws = _mm_or_si128(_mm_or_si128(eq_sp, eq_tb), _mm_or_si128(eq_nl, eq_cr));
        int mask = _mm_movemask_epi8(is_ws);
        if (mask != 0xFFFF) {
            int idx = zan_ctz32_local((uint32_t)(~mask & 0xFFFF));
            return pos + idx;
        }
        pos += 16;
    }
#endif

    while (pos < len) {
        uint8_t c = p[pos];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            break;
        pos++;
    }
    return pos;
}

int64_t zan_hw_json_scan_string(const uint8_t *buf, int64_t pos, int64_t len) {
    if (!buf || pos >= len) return len;
    const uint8_t *p = buf;

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    __m128i q = _mm_set1_epi8('"');
    __m128i b = _mm_set1_epi8('\\');

    while (pos + 16 <= len) {
        __m128i v = _mm_loadu_si128((const __m128i*)(p + pos));
        __m128i hit = _mm_or_si128(_mm_cmpeq_epi8(v, q), _mm_cmpeq_epi8(v, b));
        int mask = _mm_movemask_epi8(hit);
        if (mask != 0) {
            int idx = zan_ctz32_local((uint32_t)mask);
            int64_t target = pos + idx;
            if (p[target] == '"') {
                return target;
            } else {
                return -(target + 1);
            }
        }
        pos += 16;
    }
#endif

    while (pos < len) {
        uint8_t c = p[pos];
        if (c == '"') {
            return pos;
        } else if (c == '\\') {
            return -(pos + 1);
        }
        pos++;
    }
    return len;
}

