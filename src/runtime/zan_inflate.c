#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "miniz_tinfl.c"

uint32_t zan_embed_rawlen(const void *payload, uint64_t len) {
    if (!payload || !(len & 0x4000000000000000ULL)) return 0;
    if ((len & ~0x4000000000000000ULL) < 4) return 0;
    uint32_t raw;
    memcpy(&raw, payload, 4);
    return raw;
}

void *zan_embed_decode(const void *payload, uint64_t len) {
    if (!payload || !(len & 0x4000000000000000ULL)) return NULL;
    uint64_t total = len & ~0x4000000000000000ULL;
    if (total < 8) return NULL;
    uint32_t raw_len, comp_len;
    memcpy(&raw_len, payload, 4);
    memcpy(&comp_len, (const char *)payload + 4, 4);
    const uint8_t *src = (const uint8_t *)payload + 8;
    if ((uint64_t)comp_len > total - 8) return NULL;
    if ((uint64_t)raw_len + 1 > (uint64_t)SIZE_MAX) return NULL;
    uint8_t *out = (uint8_t *)malloc((size_t)raw_len + 1);
    if (!out) return NULL;
    size_t out_len = raw_len;
    size_t in_len = comp_len;
    if (raw_len &&
        tinfl_decompress_mem_to_mem(out, out_len, src, in_len, 0) ==
            TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
        free(out);
        return NULL;
    }
    out[raw_len] = 0;
    return out;
}

/* ---- 内嵌资源解密（与 src/compiler/embedres.c 的 bake 侧互为镜像）----
 * 资源在 exe 里不是明文：bake 时对（压缩后的）整块字节做密钥流 XOR，
 * 密钥流 = splitmix64(seed ^ fnv1a64(资源名) 的步进序列)，seed 由
 * 编译器从本次内嵌全部内容派生（确定性构建保持可复现）。这里只做
 * 内存解密，绝不落临时文件。防 strings 静态提取，不对抗逆向专家。 */

static uint64_t zan_embed_mix64(uint64_t x) {
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27; x *= 0x94D049BB133111EBULL;
    x ^= x >> 31; return x;
}

static void zan_embed_crypt(const char *name, unsigned char *buf,
                            long long len, uint64_t seed) {
    uint64_t h = 1469598103934665603ULL;
    const char *p;
    long long i = 0;
    for (p = name ? name : ""; *p; p++) {
        h ^= (unsigned char)*p;
        h *= 1099511628211ULL;
    }
    uint64_t st = seed ^ (h * 0x9E3779B97F4A7C15ULL);
    while (i < len) {
        uint64_t z;
        int n = len - i < 8 ? (int)(len - i) : 8;
        int k;
        st += 0x9E3779B97F4A7C15ULL;
        z = zan_embed_mix64(st);
        for (k = 0; k < n; k++) buf[i + k] ^= (unsigned char)(z >> (8 * k));
        i += n;
    }
}

void *zan_embed_uncrypt(const char *name, const void *payload,
                        uint64_t stored_len, uint64_t seed) {
    uint8_t *out;
    if (!payload || stored_len > 0xFFFFFFFEULL) return NULL;
    out = (uint8_t *)malloc((size_t)stored_len + 1);
    if (!out) return NULL;
    memcpy(out, payload, (size_t)stored_len);
    zan_embed_crypt(name, out, (long long)stored_len, seed);
    out[stored_len] = 0;
    return out;
}
