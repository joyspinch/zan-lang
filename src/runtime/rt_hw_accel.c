/* Zan Hardware Acceleration Engine & Cryptographic / SIMD Drivers
 * Implements AES-NI, SHA-NI, AVX2 / SSE SIMD kernels for maximum throughput.
 */

#include "rt_hw_accel.h"
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
__asm__(
  ".intel_syntax noprefix\n"
  ".text\n"
  ".globl zan_sha256_transform_ni\n"
  ".def zan_sha256_transform_ni; .scl 2; .type 32; .endef\n"
  "zan_sha256_transform_ni:\n"
  "    # Windows x64 calling convention:\n"
  "    # rcx: state pointer (uint32_t state[8])\n"
  "    # rdx: data pointer (const uint8_t *data)\n"
  "    # r8:  num_blocks (size_t)\n"
  "    push   rdi\n"
  "    push   rsi\n"
  "    push   rbx\n"
  "    push   rbp\n"
  "    mov    rdi, rcx\n"
  "    mov    rsi, rdx\n"
  "    mov    rdx, r8\n"
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
  ".section .rdata,\"dr\"\n"
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
