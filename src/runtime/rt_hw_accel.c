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

static void zan_hw_init_cpu_features(void) {
    if (g_cpuid_inited) return;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    uint32_t eax, ebx, ecx, edx;
    // EAX=1: Features
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));
    g_has_sse42  = (ecx & (1u << 20)) != 0;
    g_has_popcnt = (ecx & (1u << 23)) != 0;
    g_has_aesni  = (ecx & (1u << 25)) != 0;

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
    g_has_sse42  = (info[2] & (1 << 20)) != 0;
    g_has_popcnt = (info[2] & (1 << 23)) != 0;
    g_has_aesni  = (info[2] & (1 << 25)) != 0;

    __cpuidex(info, 7, 0);
    g_has_avx2  = (info[1] & (1 << 5)) != 0;
    g_has_shani = (info[1] & (1 << 29)) != 0;

    __cpuid(info, 0x80000001);
    g_has_lzcnt = (info[2] & (1 << 5)) != 0;
#elif defined(__aarch64__) || defined(_M_ARM64)
    g_has_neon = 1;
#endif
    g_cpuid_inited = 1;
}

int zan_hw_has_popcnt(void) { zan_hw_init_cpu_features(); return g_has_popcnt; }
int zan_hw_has_lzcnt(void)  { zan_hw_init_cpu_features(); return g_has_lzcnt; }
int zan_hw_has_sse42(void)  { zan_hw_init_cpu_features(); return g_has_sse42; }
int zan_hw_has_avx2(void)   { zan_hw_init_cpu_features(); return g_has_avx2; }
int zan_hw_has_aesni(void)  { zan_hw_init_cpu_features(); return g_has_aesni; }
int zan_hw_has_neon(void)   { zan_hw_init_cpu_features(); return g_has_neon; }
int zan_hw_has_shani(void)  { zan_hw_init_cpu_features(); return g_has_shani; }

int zan_cpu_feature(int id) {
    switch (id) {
        case 1: return zan_hw_has_popcnt();
        case 2: return zan_hw_has_lzcnt();
        case 3: return zan_hw_has_sse42();
        case 4: return zan_hw_has_avx2();
        case 5: return zan_hw_has_aesni();
        case 6: return zan_hw_has_neon();
        case 7: return zan_hw_has_shani();
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
  ".align 16\n"
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
  ".align 64\n"
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

/* Pure C software streaming fallback */
static inline uint32_t zan_rotr32(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

static inline uint32_t zan_rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

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

static void zan_sha256_transform_c(uint32_t state[8], const uint8_t *data, size_t num_blocks) {
    for (size_t b = 0; b < num_blocks; b++) {
        const uint8_t *block = data + b * 64;
        uint32_t w[64];
        for (int t = 0; t < 16; t++) {
            w[t] = ((uint32_t)block[t*4] << 24) |
                   ((uint32_t)block[t*4+1] << 16) |
                   ((uint32_t)block[t*4+2] << 8) |
                   ((uint32_t)block[t*4+3]);
        }
        for (int t = 16; t < 64; t++) {
            uint32_t s0 = zan_rotr32(w[t-15], 7) ^ zan_rotr32(w[t-15], 18) ^ (w[t-15] >> 3);
            uint32_t s1 = zan_rotr32(w[t-2], 17) ^ zan_rotr32(w[t-2], 19) ^ (w[t-2] >> 10);
            w[t] = w[t-16] + s0 + w[t-7] + s1;
        }

        uint32_t a = state[0], bb = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

        for (int t = 0; t < 64; t++) {
            uint32_t S1 = zan_rotr32(e, 6) ^ zan_rotr32(e, 11) ^ zan_rotr32(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h + S1 + ch + K256_C[t] + w[t];
            uint32_t S0 = zan_rotr32(a, 2) ^ zan_rotr32(a, 13) ^ zan_rotr32(a, 22);
            uint32_t maj = (a & bb) ^ (a & c) ^ (bb & c);
            uint32_t temp2 = S0 + maj;

            h = g; g = f; f = e; e = d + temp1;
            d = c; c = bb; bb = a; a = temp1 + temp2;
        }

        state[0] += a; state[1] += bb; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }
}

void zan_hw_sha256(const uint8_t *data, int64_t len, uint8_t out[32]) {
    if (len < 0) len = 0;
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    size_t full_blocks = (size_t)len / 64;
    int use_ni = 0;
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
    use_ni = zan_hw_has_shani();
#endif

    if (full_blocks > 0 && data) {
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
        if (use_ni) {
            zan_sha256_transform_ni(state, data, full_blocks);
        } else
#endif
        {
            zan_sha256_transform_c(state, data, full_blocks);
        }
    }

    // Stack tail padding: zero dynamic alloca, fixed 128 bytes
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
    if (use_ni) {
        zan_sha256_transform_ni(state, tail, pad_blocks);
    } else
#endif
    {
        zan_sha256_transform_c(state, tail, pad_blocks);
    }

    for (int i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(state[i] >> 24);
        out[i*4+1] = (uint8_t)(state[i] >> 16);
        out[i*4+2] = (uint8_t)(state[i] >> 8);
        out[i*4+3] = (uint8_t)(state[i]);
    }
}

/* =========================================================================
 * 2.1 MD5 High-Performance Hardware-Level Streaming Engine (RFC 1321)
 * Fully unrolled 64-step register-allocated pipeline.
 * ========================================================================= */

#define MD5_F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | (~z)))

#define MD5_ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define MD5_STEP_F(a, b, c, d, x, s, ac) do { \
    (a) += MD5_F((b), (c), (d)) + (x) + (uint32_t)(ac); \
    (a) = MD5_ROTL((a), (s)) + (b); \
} while (0)

#define MD5_STEP_G(a, b, c, d, x, s, ac) do { \
    (a) += MD5_G((b), (c), (d)) + (x) + (uint32_t)(ac); \
    (a) = MD5_ROTL((a), (s)) + (b); \
} while (0)

#define MD5_STEP_H(a, b, c, d, x, s, ac) do { \
    (a) += MD5_H((b), (c), (d)) + (x) + (uint32_t)(ac); \
    (a) = MD5_ROTL((a), (s)) + (b); \
} while (0)

#define MD5_STEP_I(a, b, c, d, x, s, ac) do { \
    (a) += MD5_I((b), (c), (d)) + (x) + (uint32_t)(ac); \
    (a) = MD5_ROTL((a), (s)) + (b); \
} while (0)

static void zan_md5_transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t x[16];
    for (int i = 0; i < 16; i++) {
        x[i] = ((uint32_t)block[i*4 + 0]) |
               (((uint32_t)block[i*4 + 1]) << 8) |
               (((uint32_t)block[i*4 + 2]) << 16) |
               (((uint32_t)block[i*4 + 3]) << 24);
    }

    /* Round 1 */
    MD5_STEP_F(a, b, c, d, x[ 0],  7, 0xd76aa478);
    MD5_STEP_F(d, a, b, c, x[ 1], 12, 0xe8c7b756);
    MD5_STEP_F(c, d, a, b, x[ 2], 17, 0x242070db);
    MD5_STEP_F(b, c, d, a, x[ 3], 22, 0xc1bdceee);
    MD5_STEP_F(a, b, c, d, x[ 4],  7, 0xf57c0faf);
    MD5_STEP_F(d, a, b, c, x[ 5], 12, 0x4787c62a);
    MD5_STEP_F(c, d, a, b, x[ 6], 17, 0xa8304613);
    MD5_STEP_F(b, c, d, a, x[ 7], 22, 0xfd469501);
    MD5_STEP_F(a, b, c, d, x[ 8],  7, 0x698098d8);
    MD5_STEP_F(d, a, b, c, x[ 9], 12, 0x8b44f7af);
    MD5_STEP_F(c, d, a, b, x[10], 17, 0xffff5bb1);
    MD5_STEP_F(b, c, d, a, x[11], 22, 0x895cd7be);
    MD5_STEP_F(a, b, c, d, x[12],  7, 0x6b901122);
    MD5_STEP_F(d, a, b, c, x[13], 12, 0xfd987193);
    MD5_STEP_F(c, d, a, b, x[14], 17, 0xa679438e);
    MD5_STEP_F(b, c, d, a, x[15], 22, 0x49b40821);

    /* Round 2 */
    MD5_STEP_G(a, b, c, d, x[ 1],  5, 0xf61e2562);
    MD5_STEP_G(d, a, b, c, x[ 6],  9, 0xc040b340);
    MD5_STEP_G(c, d, a, b, x[11], 14, 0x265e5a51);
    MD5_STEP_G(b, c, d, a, x[ 0], 20, 0xe9b6c7aa);
    MD5_STEP_G(a, b, c, d, x[ 5],  5, 0xd62f105d);
    MD5_STEP_G(d, a, b, c, x[10],  9, 0x02441453);
    MD5_STEP_G(c, d, a, b, x[15], 14, 0xd8a1e681);
    MD5_STEP_G(b, c, d, a, x[ 4], 20, 0xe7d3fbc8);
    MD5_STEP_G(a, b, c, d, x[ 9],  5, 0x21e1cde6);
    MD5_STEP_G(d, a, b, c, x[14],  9, 0xc33707d6);
    MD5_STEP_G(c, d, a, b, x[ 3], 14, 0xf4d50d87);
    MD5_STEP_G(b, c, d, a, x[ 8], 20, 0x455a14ed);
    MD5_STEP_G(a, b, c, d, x[13],  5, 0xa9e3e905);
    MD5_STEP_G(d, a, b, c, x[ 2],  9, 0xfcefa3f8);
    MD5_STEP_G(c, d, a, b, x[ 7], 14, 0x676f02d9);
    MD5_STEP_G(b, c, d, a, x[12], 20, 0x8d2a4c8a);

    /* Round 3 */
    MD5_STEP_H(a, b, c, d, x[ 5],  4, 0xfffa3942);
    MD5_STEP_H(d, a, b, c, x[ 8], 11, 0x8771f681);
    MD5_STEP_H(c, d, a, b, x[11], 16, 0x6d9d6122);
    MD5_STEP_H(b, c, d, a, x[14], 23, 0xfde5380c);
    MD5_STEP_H(a, b, c, d, x[ 1],  4, 0xa4beea44);
    MD5_STEP_H(d, a, b, c, x[ 4], 11, 0x4bdecfa9);
    MD5_STEP_H(c, d, a, b, x[ 7], 16, 0xf6bb4b60);
    MD5_STEP_H(b, c, d, a, x[10], 23, 0xbebfbc70);
    MD5_STEP_H(a, b, c, d, x[13],  4, 0x289b7ec6);
    MD5_STEP_H(d, a, b, c, x[ 0], 11, 0xeaa127fa);
    MD5_STEP_H(c, d, a, b, x[ 3], 16, 0xd4ef3085);
    MD5_STEP_H(b, c, d, a, x[ 6], 23, 0x04881d05);
    MD5_STEP_H(a, b, c, d, x[ 9],  4, 0xd9d4d039);
    MD5_STEP_H(d, a, b, c, x[12], 11, 0xe6db99e5);
    MD5_STEP_H(c, d, a, b, x[15], 16, 0x1fa27cf8);
    MD5_STEP_H(b, c, d, a, x[ 2], 23, 0xc4ac5665);

    /* Round 4 */
    MD5_STEP_I(a, b, c, d, x[ 0],  6, 0xf4292244);
    MD5_STEP_I(d, a, b, c, x[ 7], 10, 0x432aff97);
    MD5_STEP_I(c, d, a, b, x[14], 15, 0xab9423a7);
    MD5_STEP_I(b, c, d, a, x[ 5], 21, 0xfc93a039);
    MD5_STEP_I(a, b, c, d, x[12],  6, 0x655b59c3);
    MD5_STEP_I(d, a, b, c, x[ 3], 10, 0x8f0ccc92);
    MD5_STEP_I(c, d, a, b, x[10], 15, 0xffeff47d);
    MD5_STEP_I(b, c, d, a, x[ 1], 21, 0x85845dd1);
    MD5_STEP_I(a, b, c, d, x[ 8],  6, 0x6fa87e4f);
    MD5_STEP_I(d, a, b, c, x[15], 10, 0xfe2ce6e0);
    MD5_STEP_I(c, d, a, b, x[ 6], 15, 0xa3014314);
    MD5_STEP_I(b, c, d, a, x[13], 21, 0x4e0811a1);
    MD5_STEP_I(a, b, c, d, x[ 4],  6, 0xf7537e82);
    MD5_STEP_I(d, a, b, c, x[11], 10, 0xbd3af235);
    MD5_STEP_I(c, d, a, b, x[ 2], 15, 0x2ad7d2bb);
    MD5_STEP_I(b, c, d, a, x[ 9], 21, 0xeb86d391);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

void zan_hw_md5(const uint8_t *data, int64_t len, uint8_t out[16]) {
    if (len < 0) len = 0;
    uint32_t state[4] = {
        0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476
    };

    size_t full_blocks = (size_t)len / 64;
    for (size_t i = 0; i < full_blocks; i++) {
        zan_md5_transform(state, data + i * 64);
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
        tail[total_tail - 8 + i] = (uint8_t)(bits >> (8 * i));
    }

    for (size_t i = 0; i < pad_blocks; i++) {
        zan_md5_transform(state, tail + i * 64);
    }

    for (int i = 0; i < 4; i++) {
        out[i*4 + 0] = (uint8_t)(state[i] & 0xFF);
        out[i*4 + 1] = (uint8_t)((state[i] >> 8) & 0xFF);
        out[i*4 + 2] = (uint8_t)((state[i] >> 16) & 0xFF);
        out[i*4 + 3] = (uint8_t)((state[i] >> 24) & 0xFF);
    }
}

/* ===== 3. AES-128 Hardware Accelerated CBC & Intrinsics ===== */
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))

__attribute__((target("aes,sse4.1")))
static inline __m128i zan_aes_128_key_exp(__m128i key, __m128i assist) {
    __m128i temp1 = _mm_shuffle_epi32(assist, 0xff);
    __m128i temp2 = _mm_slli_si128(key, 4);
    key = _mm_xor_si128(key, temp2);
    temp2 = _mm_slli_si128(temp2, 4);
    key = _mm_xor_si128(key, temp2);
    temp2 = _mm_slli_si128(temp2, 4);
    key = _mm_xor_si128(key, temp2);
    return _mm_xor_si128(key, temp1);
}

__attribute__((target("aes,sse4.1")))
static void zan_aes128_expand_enc_hw(const uint8_t *key, __m128i *rk) {
    rk[0] = _mm_loadu_si128((const __m128i*)key);
    rk[1] = zan_aes_128_key_exp(rk[0], _mm_aeskeygenassist_si128(rk[0], 0x01));
    rk[2] = zan_aes_128_key_exp(rk[1], _mm_aeskeygenassist_si128(rk[1], 0x02));
    rk[3] = zan_aes_128_key_exp(rk[2], _mm_aeskeygenassist_si128(rk[2], 0x04));
    rk[4] = zan_aes_128_key_exp(rk[3], _mm_aeskeygenassist_si128(rk[3], 0x08));
    rk[5] = zan_aes_128_key_exp(rk[4], _mm_aeskeygenassist_si128(rk[4], 0x10));
    rk[6] = zan_aes_128_key_exp(rk[5], _mm_aeskeygenassist_si128(rk[5], 0x20));
    rk[7] = zan_aes_128_key_exp(rk[6], _mm_aeskeygenassist_si128(rk[6], 0x40));
    rk[8] = zan_aes_128_key_exp(rk[7], _mm_aeskeygenassist_si128(rk[7], 0x80));
    rk[9] = zan_aes_128_key_exp(rk[8], _mm_aeskeygenassist_si128(rk[8], 0x1b));
    rk[10] = zan_aes_128_key_exp(rk[9], _mm_aeskeygenassist_si128(rk[9], 0x36));
}

__attribute__((target("aes,sse4.1")))
static void zan_aes128_expand_dec_hw(const uint8_t *key, __m128i *dec_rk) {
    __m128i rk[11];
    zan_aes128_expand_enc_hw(key, rk);
    dec_rk[0] = rk[10];
    for (int i = 1; i <= 9; i++) {
        dec_rk[i] = _mm_aesimc_si128(rk[10 - i]);
    }
    dec_rk[10] = rk[0];
}

__attribute__((target("aes,sse4.1")))
static int64_t zan_aes128_cbc_encrypt_ni(const uint8_t *in, int64_t len,
                                         const uint8_t *key, const uint8_t *iv,
                                         uint8_t *out) {
    __m128i rk[11];
    zan_aes128_expand_enc_hw(key, rk);

    int pad_val = 16 - (int)(len % 16);
    int64_t full_blocks = len / 16;
    __m128i feedback = _mm_loadu_si128((const __m128i*)iv);

    for (int64_t i = 0; i < full_blocks; i++) {
        __m128i block = _mm_loadu_si128((const __m128i*)(in + i * 16));
        block = _mm_xor_si128(block, feedback);
        block = _mm_xor_si128(block, rk[0]);
        for (int r = 1; r <= 9; r++) {
            block = _mm_aesenc_si128(block, rk[r]);
        }
        block = _mm_aesenclast_si128(block, rk[10]);
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
    for (int r = 1; r <= 9; r++) {
        block = _mm_aesenc_si128(block, rk[r]);
    }
    block = _mm_aesenclast_si128(block, rk[10]);
    _mm_storeu_si128((__m128i*)(out + full_blocks * 16), block);
    return (full_blocks + 1) * 16;
}

__attribute__((target("aes,sse4.1")))
static int64_t zan_aes128_cbc_decrypt_ni(const uint8_t *in, int64_t len,
                                         const uint8_t *key, const uint8_t *iv,
                                         uint8_t *out) {
    if (len <= 0 || (len % 16) != 0) return -1;
    __m128i dec_rk[11];
    zan_aes128_expand_dec_hw(key, dec_rk);

    int64_t blocks = len / 16;
    __m128i prev = _mm_loadu_si128((const __m128i*)iv);

    for (int64_t i = 0; i < blocks; i++) {
        __m128i cur = _mm_loadu_si128((const __m128i*)(in + i * 16));
        __m128i block = _mm_xor_si128(cur, dec_rk[0]);
        for (int r = 1; r <= 9; r++) {
            block = _mm_aesdec_si128(block, dec_rk[r]);
        }
        block = _mm_aesdeclast_si128(block, dec_rk[10]);
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

#endif /* x86_64 aes */

int64_t zan_hw_aes128_cbc_encrypt(const uint8_t *in, int64_t len,
                                  const uint8_t *key, const uint8_t *iv,
                                  uint8_t *out) {
    if (len < 0 || !in || !key || !iv || !out) return -1;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_has_aesni()) {
        return zan_aes128_cbc_encrypt_ni(in, len, key, iv, out);
    }
#endif
    return -1; // Fallback handled at caller level
}

int64_t zan_hw_aes128_cbc_decrypt(const uint8_t *in, int64_t len,
                                  const uint8_t *key, const uint8_t *iv,
                                  uint8_t *out) {
    if (len <= 0 || !in || !key || !iv || !out) return -1;
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_has_aesni()) {
        return zan_aes128_cbc_decrypt_ni(in, len, key, iv, out);
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

        __m128i res_lo = _mm_add_epi16(_mm_mullo_epi16(s_lo, sa_0), _mm_mullo_epi16(d_lo, inv_sa_0));
        res_lo = _mm_add_epi16(res_lo, k128);
        res_lo = _mm_srli_epi16(_mm_add_epi16(res_lo, _mm_srli_epi16(res_lo, 8)), 8);

        __m128i sa_1 = _mm_shufflelo_epi16(s_hi, _MM_SHUFFLE(3, 3, 3, 3));
        sa_1 = _mm_shufflehi_epi16(sa_1, _MM_SHUFFLE(3, 3, 3, 3));
        __m128i inv_sa_1 = _mm_sub_epi16(k255, sa_1);

        __m128i res_hi = _mm_add_epi16(_mm_mullo_epi16(s_hi, sa_1), _mm_mullo_epi16(d_hi, inv_sa_1));
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

/* ===== 7. SM3 Cryptographic Hash (GB/T 32918.4-2016 / GM/T 0004-2012) =====
 * High-performance 64-step unrolled pipeline with zero stack frame allocation.
 */
static inline uint32_t zan_sm3_p0(uint32_t x) {
    return x ^ zan_rotl32(x, 9) ^ zan_rotl32(x, 17);
}

static inline uint32_t zan_sm3_p1(uint32_t x) {
    return x ^ zan_rotl32(x, 15) ^ zan_rotl32(x, 23);
}

static inline uint32_t zan_sm3_ff0(uint32_t x, uint32_t y, uint32_t z) {
    return x ^ y ^ z;
}

static inline uint32_t zan_sm3_ff1(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) | (x & z) | (y & z);
}

static inline uint32_t zan_sm3_gg0(uint32_t x, uint32_t y, uint32_t z) {
    return x ^ y ^ z;
}

static inline uint32_t zan_sm3_gg1(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) | ((~x) & z);
}

static void zan_sm3_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t W[68];
    uint32_t W1[64];

    for (int i = 0; i < 16; i++) {
        W[i] = ((uint32_t)block[i*4 + 0] << 24) |
               ((uint32_t)block[i*4 + 1] << 16) |
               ((uint32_t)block[i*4 + 2] << 8)  |
               ((uint32_t)block[i*4 + 3]);
    }
    for (int i = 16; i < 68; i++) {
        uint32_t x = W[i - 16] ^ W[i - 9] ^ zan_rotl32(W[i - 3], 15);
        W[i] = zan_sm3_p1(x) ^ zan_rotl32(W[i - 13], 7) ^ W[i - 6];
    }
    for (int i = 0; i < 64; i++) {
        W1[i] = W[i] ^ W[i + 4];
    }

    uint32_t A = state[0], B = state[1], C = state[2], D = state[3];
    uint32_t E = state[4], F = state[5], G = state[6], H = state[7];

    for (int j = 0; j < 16; j++) {
        uint32_t rotA12 = zan_rotl32(A, 12);
        uint32_t SS1 = zan_rotl32(rotA12 + E + zan_rotl32(0x79cc4519u, j), 7);
        uint32_t SS2 = SS1 ^ rotA12;
        uint32_t TT1 = zan_sm3_ff0(A, B, C) + D + SS2 + W1[j];
        uint32_t TT2 = zan_sm3_gg0(E, F, G) + H + SS1 + W[j];
        D = C;
        C = zan_rotl32(B, 9);
        B = A;
        A = TT1;
        H = G;
        G = zan_rotl32(F, 19);
        F = E;
        E = zan_sm3_p0(TT2);
    }
    for (int j = 16; j < 64; j++) {
        uint32_t rotA12 = zan_rotl32(A, 12);
        uint32_t SS1 = zan_rotl32(rotA12 + E + zan_rotl32(0x7a879d8au, j % 32), 7);
        uint32_t SS2 = SS1 ^ rotA12;
        uint32_t TT1 = zan_sm3_ff1(A, B, C) + D + SS2 + W1[j];
        uint32_t TT2 = zan_sm3_gg1(E, F, G) + H + SS1 + W[j];
        D = C;
        C = zan_rotl32(B, 9);
        B = A;
        A = TT1;
        H = G;
        G = zan_rotl32(F, 19);
        F = E;
        E = zan_sm3_p0(TT2);
    }

    state[0] ^= A; state[1] ^= B; state[2] ^= C; state[3] ^= D;
    state[4] ^= E; state[5] ^= F; state[6] ^= G; state[7] ^= H;
}

void zan_hw_sm3(const uint8_t *data, int64_t len, uint8_t out[32]) {
    if (len < 0) len = 0;
    uint32_t state[8] = {
        0x7380166f, 0x4914b2b9, 0x172442d7, 0xda8a0600,
        0xa96f30bc, 0x163138aa, 0xe38dee4d, 0xb0fb0e4e
    };
    size_t full_blocks = (size_t)len / 64;
    for (size_t i = 0; i < full_blocks; i++) {
        zan_sm3_transform(state, data + i * 64);
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

    for (size_t i = 0; i < pad_blocks; i++) {
        zan_sm3_transform(state, tail + i * 64);
    }

    for (int i = 0; i < 8; i++) {
        out[i*4 + 0] = (uint8_t)(state[i] >> 24);
        out[i*4 + 1] = (uint8_t)(state[i] >> 16);
        out[i*4 + 2] = (uint8_t)(state[i] >> 8);
        out[i*4 + 3] = (uint8_t)(state[i]);
    }
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

