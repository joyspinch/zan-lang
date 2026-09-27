/* zan_inflate.c -- compressed-resource decoder for the inline embed API.
 *
 * zanc bakes large --embed payloads deflate-compressed: the resource bytes
 * are [u32 raw_len][u32 comp_len][raw deflate stream] and the read API the
 * compiler emits calls zan_embed_decode/rawlen here (main.c links this
 * object whenever a program embeds resources). Decompress-only: archive/
 * zip/stdio miniz APIs are compiled out (see CMakeLists zan_inflate).
 *
 * The decoder is for the *runtime* side of published programs, so it keeps
 * no ARC contract: it returns a malloc'd NUL-terminated buffer the caller
 * copies (File.ReadAllBytes) or reads as text and frees. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Unity-include the tinfl implementation: the object must stay a single
 * self-contained TU (it is staged as one file next to zanc and linked
 * straight into user programs). miniz_tinfl.c pulls in miniz.h. */
#include "miniz_tinfl.c"

/* Raw length: first 4 bytes of the baked payload. `len` is the runtime's
 * ARC-tagged byte count; the tag bit only says the pointer is a managed
 * array header, which the compressor side guarantees for embed payloads. */
uint32_t zan_embed_rawlen(const void *payload, uint64_t len) {
    if (!payload || !(len & 0x4000000000000000ULL)) return 0;
    uint32_t raw;
    memcpy(&raw, payload, 4);
    return raw;
}

/* Decode one payload: returns malloc'd raw_len+1 bytes, NUL-terminated,
 * NULL when the payload is malformed or the stream does not fill exactly
 * raw_len bytes (corrupt embed = loud failure, not a truncated resource). */
void *zan_embed_decode(const void *payload, uint64_t len) {
    if (!payload || !(len & 0x4000000000000000ULL)) return NULL;
    /* The tag bit marks a managed-array header; the payload byte count is what
     * remains. The bound below used the tagged `len` itself: comp_len is u32
     * and the tag puts len at >= 2^62, so the comparison could never be true
     * (dead check) and the deflate stream was read without any bound (A291).
     * Payloads are compiler-baked today, so this is hardening, not a live
     * hole -- but the check has to actually read the length. */
    uint64_t total = len & ~0x4000000000000000ULL;
    if (total < 8) return NULL;
    uint32_t raw_len, comp_len;
    memcpy(&raw_len, payload, 4);
    memcpy(&comp_len, (const char *)payload + 4, 4);
    const uint8_t *src = (const uint8_t *)payload + 8;
    if ((uint64_t)comp_len > total - 8) return NULL;
    uint8_t *out = (uint8_t *)malloc((size_t)raw_len + 1);
    if (!out) return NULL;
    size_t out_len = raw_len;
    size_t in_len = comp_len;
    /* a 0 raw_len payload is a legit empty resource: skip tinfl */
    if (raw_len &&
        tinfl_decompress_mem_to_mem(out, out_len, src, in_len, 0) ==
            TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
        free(out);
        return NULL;
    }
    out[raw_len] = 0;
    return out;
}
