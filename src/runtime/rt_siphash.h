#ifndef ZAN_RT_SIPHASH_H
#define ZAN_RT_SIPHASH_H

/* SipHash-2-4（B-ID80）：Dictionary/HashSet 键哈希的共享核心。
 * IR 侧 __zan_dict_hash 是外部符号（irgen_builtins.c 只留声明），
 * 三个运行时各自落一个定义：
 *   - rt_mem.c（hosted）：盐取 OS 熵源，进程生命周期内稳定；
 *   - rt_wasm.c / rt_bare_shim.c：确定性盐（wasm 沙箱无 OS 熵、bare 嵌入
 *     单租户，离线构造碰撞的威胁模型不同；算法保持同构，行为可复现）。 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SIPROT(x, b) ((uint64_t)((x) << (b)) | ((x) >> (64 - (b))))

static inline void zan_sipround(uint64_t *v0, uint64_t *v1, uint64_t *v2, uint64_t *v3) {
    *v0 += *v1; *v1 = SIPROT(*v1, 13); *v1 ^= *v0; *v0 = SIPROT(*v0, 32);
    *v2 += *v3; *v3 = SIPROT(*v3, 16); *v3 ^= *v2;
    *v0 += *v3; *v3 = SIPROT(*v3, 21); *v3 ^= *v0;
    *v2 += *v1; *v1 = SIPROT(*v1, 17); *v1 ^= *v2; *v2 = SIPROT(*v2, 32);
}

static inline uint64_t zan_siphash24(const void *data, size_t len,
                                     uint64_t k0, uint64_t k1) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t v0 = k0 ^ 0x736f6d6570736575ull; /* "somepseudorandomlygenerated" */
    uint64_t v1 = k1 ^ 0x646f72616e646f6dull; /*  le 常量，SipHash 规范  */
    uint64_t v2 = k0 ^ 0x6c7967656e657261ull;
    uint64_t v3 = k1 ^ 0x7465646279746573ull;
    size_t left = len;
    while (left >= 8) {
        uint64_t m;
        memcpy(&m, p, 8);
        v3 ^= m;
        zan_sipround(&v0, &v1, &v2, &v3);
        zan_sipround(&v0, &v1, &v2, &v3);
        v0 ^= m;
        p += 8; left -= 8;
    }
    uint64_t b = ((uint64_t)len) << 56;
    for (size_t i = 0; i < left; i++) b |= ((uint64_t)p[i]) << (8 * i);
    v3 ^= b;
    zan_sipround(&v0, &v1, &v2, &v3);
    zan_sipround(&v0, &v1, &v2, &v3);
    v0 ^= b;
    v2 ^= 0xff;
    zan_sipround(&v0, &v1, &v2, &v3);
    zan_sipround(&v0, &v1, &v2, &v3);
    zan_sipround(&v0, &v1, &v2, &v3);
    zan_sipround(&v0, &v1, &v2, &v3);
    return v0 ^ v1 ^ v2 ^ v3;
}

#endif /* ZAN_RT_SIPHASH_H */
