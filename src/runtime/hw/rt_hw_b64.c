/* rt_hw_b64.c: Base64 快速查表与编解码向量加速实现 */

/* 7 */

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
    /* URL-safe RFC 4648 §5: '-' -> 62, '_' -> 63 */
    ZAN_B64_DEC_TABLE[(uint8_t)'-'] = 62;
    ZAN_B64_DEC_TABLE[(uint8_t)'_'] = 63;
    zan_b64_dec_inited = 1;
}

int64_t zan_hw_base64_encode(const uint8_t *src, int64_t len, char *dst) {
    if (!src || len <= 0 || !dst) return 0;
    const uint8_t *s = src;
    char *d = dst;
    int64_t rem = len;

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

    while (len > 0 && src[len - 1] == '=') len--;
    int rem_mod = (int)(len & 3);
    if (rem_mod == 1) return -1;

    int64_t full_quads = len / 4;
    int64_t plain_len = full_quads * 3 + (rem_mod == 2 ? 1 : (rem_mod == 3 ? 2 : 0));
    if (plain_len < 0) return -1;

    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = dst;
    int64_t rem = full_quads * 4;


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

    if (rem_mod == 2) {
        uint8_t a = ZAN_B64_DEC_TABLE[s[0]];
        uint8_t b = ZAN_B64_DEC_TABLE[s[1]];
        if ((a | b) & 0x80) return -1;
        if ((b & 0x0F) != 0) return -1; /* RFC 4648 unused bits */
        d[0] = (uint8_t)((a << 2) | (b >> 4));
        d += 1;
    } else if (rem_mod == 3) {
        uint8_t a = ZAN_B64_DEC_TABLE[s[0]];
        uint8_t b = ZAN_B64_DEC_TABLE[s[1]];
        uint8_t c = ZAN_B64_DEC_TABLE[s[2]];
        if ((a | b | c) & 0x80) return -1;
        if ((c & 0x03) != 0) return -1; /* RFC 4648 unused bits */
        uint32_t n = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6);
        d[0] = (uint8_t)(n >> 16);
        d[1] = (uint8_t)(n >> 8);
        d += 2;
    }

    return (int64_t)(d - dst);
}

static inline int zan_ctz64_local(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(x);
#elif defined(_MSC_VER)
    unsigned long idx;
    _BitScanForward64(&idx, x);
    return (int)idx;
#else
    int n = 0;
    while ((x & 1) == 0) { x >>= 1; n++; }
    return n;
#endif
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

