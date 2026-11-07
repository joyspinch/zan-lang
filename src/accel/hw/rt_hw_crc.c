/* rt_hw_crc.c: CRC32 与 CRC32C 硬件切片与指令加速实现 */

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
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
#endif /* x86 crc */

#if (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
#include <arm_acle.h>
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

__attribute__((target("crc")))
static uint32_t zan_crc32_arm(uint32_t crc, const uint8_t *p, int64_t n) {
    uint64_t c = (uint64_t)(crc ^ 0xFFFFFFFFu);
    while (n >= 8 && ((uintptr_t)p & 7)) { c = __crc32b((uint32_t)c, *p++); n--; }
    while (n >= 8) { c = __crc32d((uint32_t)c, *(const uint64_t*)p); p += 8; n -= 8; }
    if (n >= 4) { c = __crc32w((uint32_t)c, *(const uint32_t*)p); p += 4; n -= 4; }
    if (n >= 2) { c = __crc32h((uint32_t)c, *(const uint16_t*)p); p += 2; n -= 2; }
    if (n >= 1) { c = __crc32b((uint32_t)c, *p); }
    return (uint32_t)c ^ 0xFFFFFFFFu;
}
#endif /* arm crc */

static int zan_crc32c_kat(void) {
    /* RFC 4960 B.8: CRC-32C("123456789") = 0xE3069283 */
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    return zan_crc32c_sse42(0xFFFFFFFFu, (const uint8_t*)"123456789", 9) == 0x1CF96D7Cu;
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    return zan_crc32c_pmull_arm(0xFFFFFFFFu, (const uint8_t*)"123456789", 9) == 0x1CF96D7Cu;
#else
    return 1;
#endif
}

static int zan_crc32_kat(void) {
    /* IEEE 802.3: CRC-32("123456789") = 0xCBF43926 */
#if (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    return zan_crc32_arm(0, (const uint8_t*)"123456789", 9) == 0xCBF43926u;
#else
    return 1;
#endif
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

static uint32_t s_zan_crc32_table[8][256];
static int s_zan_crc32_inited = 0;

static void zan_init_crc32_slice8(void) {
    if (s_zan_crc32_inited) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        s_zan_crc32_table[0][i] = c;
    }
    for (int k = 1; k < 8; k++) {
        for (uint32_t i = 0; i < 256; i++) {
            s_zan_crc32_table[k][i] = (s_zan_crc32_table[k - 1][i] >> 8) ^
                                      s_zan_crc32_table[0][s_zan_crc32_table[k - 1][i] & 0xFF];
        }
    }
    s_zan_crc32_inited = 1;
}

static uint32_t zan_crc32_slice8(uint32_t crc, const uint8_t *buf, int64_t len) {
    zan_init_crc32_slice8();
    uint32_t c = crc ^ 0xFFFFFFFFu;
    const uint8_t *p = buf;
    while (len > 0 && ((uintptr_t)p & 7)) {
        c = s_zan_crc32_table[0][(c ^ *p++) & 0xFF] ^ (c >> 8);
        len--;
    }
    while (len >= 8) {
        uint32_t one = *(const uint32_t*)p ^ c;
        uint32_t two = *(const uint32_t*)(p + 4);
        c = s_zan_crc32_table[7][one & 0xFF] ^
            s_zan_crc32_table[6][(one >> 8) & 0xFF] ^
            s_zan_crc32_table[5][(one >> 16) & 0xFF] ^
            s_zan_crc32_table[4][(one >> 24) & 0xFF] ^
            s_zan_crc32_table[3][two & 0xFF] ^
            s_zan_crc32_table[2][(two >> 8) & 0xFF] ^
            s_zan_crc32_table[1][(two >> 16) & 0xFF] ^
            s_zan_crc32_table[0][(two >> 24) & 0xFF];
        p += 8;
        len -= 8;
    }
    while (len > 0) {
        c = s_zan_crc32_table[0][(c ^ *p++) & 0xFF] ^ (c >> 8);
        len--;
    }
    return c ^ 0xFFFFFFFFu;
}

int64_t zan_hw_crc32_update(uint32_t crc, const uint8_t *p, int64_t len) {
    if (len < 0 || !p) return -1;
#if (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    if (zan_hw_gate(&g_gate_crc32, zan_hw_arm_crc32(), zan_crc32_kat)) {
        return (int64_t)zan_crc32_arm(crc, p, len);
    }
#endif
    return (int64_t)zan_crc32_slice8(crc, p, len);
}

