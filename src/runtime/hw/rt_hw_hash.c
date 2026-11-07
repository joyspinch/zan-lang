/* rt_hw_hash.c: SHA-256 与 SHA-1 硬件加速指令集内核与自检实现 */


#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))

#if defined(_WIN32)
#define ZAN_SHA256_DIRECTIVE ".def zan_sha256_transform_ni; .scl 2; .type 32; .endef\n"
#define ZAN_SHA256_PROLOGUE \
  "    mov    rdi, rcx\n" \
  "    mov    rsi, rdx\n" \
  "    mov    rdx, r8\n"
#define ZAN_SHA256_RODATA ".section .rdata,\"dr\"\n"
#define ZAN_SHA256_NAME "zan_sha256_transform_ni"
#elif defined(__APPLE__)
#define ZAN_SHA256_DIRECTIVE ""
#define ZAN_SHA256_PROLOGUE ""
#define ZAN_SHA256_RODATA ".section __TEXT,__const\n"
#define ZAN_SHA256_NAME "_zan_sha256_transform_ni"
#else
#define ZAN_SHA256_DIRECTIVE ".type zan_sha256_transform_ni, @function\n"
#define ZAN_SHA256_PROLOGUE ""
#define ZAN_SHA256_RODATA ".section .rodata\n"
#define ZAN_SHA256_NAME "zan_sha256_transform_ni"
#endif

__asm__(
  ".intel_syntax noprefix\n"
  ".text\n"
  ".globl " ZAN_SHA256_NAME "\n"
  ZAN_SHA256_DIRECTIVE
  ZAN_SHA256_NAME ":\n"
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
  /*  */
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

/* 2 */
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))

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
