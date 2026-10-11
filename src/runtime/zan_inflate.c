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
