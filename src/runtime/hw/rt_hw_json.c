/* rt_hw_json.c: AVX2 向量化 JSON 语法扫描器与极速解析器实现 */

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
__attribute__((target("avx2")))
static int64_t zan_hw_json_skip_whitespace_avx2(const uint8_t *p, int64_t pos, int64_t len) {
    __m256i m_sp = _mm256_set1_epi8(' ');
    __m256i m_tb = _mm256_set1_epi8('\t');
    __m256i m_nl = _mm256_set1_epi8('\n');
    __m256i m_cr = _mm256_set1_epi8('\r');

    while (pos + 32 <= len) {
        __m256i v = _mm256_loadu_si256((const __m256i*)(p + pos));
        __m256i eq_sp = _mm256_cmpeq_epi8(v, m_sp);
        __m256i eq_tb = _mm256_cmpeq_epi8(v, m_tb);
        __m256i eq_nl = _mm256_cmpeq_epi8(v, m_nl);
        __m256i eq_cr = _mm256_cmpeq_epi8(v, m_cr);
        __m256i is_ws = _mm256_or_si256(_mm256_or_si256(eq_sp, eq_tb), _mm256_or_si256(eq_nl, eq_cr));
        uint32_t mask = (uint32_t)_mm256_movemask_epi8(is_ws);
        if (mask != 0xFFFFFFFFu) {
            int idx = zan_ctz32_local(~mask);
            return pos + idx;
        }
        pos += 32;
    }
    return pos;
}

__attribute__((target("avx2")))
static int64_t zan_hw_json_scan_string_avx2(const uint8_t *p, int64_t pos, int64_t len) {
    __m256i q = _mm256_set1_epi8('"');
    __m256i b = _mm256_set1_epi8('\\');

    while (pos + 32 <= len) {
        __m256i v = _mm256_loadu_si256((const __m256i*)(p + pos));
        __m256i hit = _mm256_or_si256(_mm256_cmpeq_epi8(v, q), _mm256_cmpeq_epi8(v, b));
        uint32_t mask = (uint32_t)_mm256_movemask_epi8(hit);
        if (mask != 0) {
            int idx = zan_ctz32_local(mask);
            int64_t target = pos + idx;
            if (p[target] == '"') {
                return target;
            } else {
                return -(target + 1);
            }
        }
        pos += 32;
    }
    return pos;
}
#endif

int64_t zan_hw_json_skip_whitespace(const uint8_t *buf, int64_t pos, int64_t len) {
    if (!buf || pos >= len) return pos;
    const uint8_t *p = buf;

#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    if (g_has_avx2 && pos + 32 <= len) {
        int64_t new_pos = zan_hw_json_skip_whitespace_avx2(p, pos, len);
        if (new_pos + 32 <= len || (new_pos < len && p[new_pos] != ' ' && p[new_pos] != '\t' && p[new_pos] != '\n' && p[new_pos] != '\r')) {
            return new_pos;
        }
        pos = new_pos;
    }

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
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    if (g_has_neon) {
        uint8x16_t v_sp = vdupq_n_u8(' ');
        uint8x16_t v_tb = vdupq_n_u8('\t');
        uint8x16_t v_nl = vdupq_n_u8('\n');
        uint8x16_t v_cr = vdupq_n_u8('\r');
        while (pos + 16 <= len) {
            uint8x16_t v = vld1q_u8(p + pos);
            uint8x16_t eq_sp = vceqq_u8(v, v_sp);
            uint8x16_t eq_tb = vceqq_u8(v, v_tb);
            uint8x16_t eq_nl = vceqq_u8(v, v_nl);
            uint8x16_t eq_cr = vceqq_u8(v, v_cr);
            uint8x16_t is_ws = vorrq_u8(vorrq_u8(eq_sp, eq_tb), vorrq_u8(eq_nl, eq_cr));
            uint8x16_t non_ws = vmvnq_u8(is_ws);
            uint64x2_t v64 = vreinterpretq_u64_u8(non_ws);
            uint64_t lo = vgetq_lane_u64(v64, 0);
            if (lo != 0) {
                return pos + (zan_ctz64_local(lo) >> 3);
            }
            uint64_t hi = vgetq_lane_u64(v64, 1);
            if (hi != 0) {
                return pos + 8 + (zan_ctz64_local(hi) >> 3);
            }
            pos += 16;
        }
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
    if (g_has_avx2 && pos + 32 <= len) {
        int64_t res = zan_hw_json_scan_string_avx2(p, pos, len);
        if (res < 0 || (res < len && (p[res] == '"' || p[res] == '\\'))) {
            return res;
        }
        pos = res;
    }

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
#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(__GNUC__) || defined(__clang__))
    if (g_has_neon) {
        uint8x16_t v_q = vdupq_n_u8('"');
        uint8x16_t v_b = vdupq_n_u8('\\');
        while (pos + 16 <= len) {
            uint8x16_t v = vld1q_u8(p + pos);
            uint8x16_t eq_q = vceqq_u8(v, v_q);
            uint8x16_t eq_b = vceqq_u8(v, v_b);
            uint8x16_t hit = vorrq_u8(eq_q, eq_b);
            uint64x2_t v64 = vreinterpretq_u64_u8(hit);
            uint64_t lo = vgetq_lane_u64(v64, 0);
            if (lo != 0) {
                int64_t target = pos + (zan_ctz64_local(lo) >> 3);
                return (p[target] == '"') ? target : -(target + 1);
            }
            uint64_t hi = vgetq_lane_u64(v64, 1);
            if (hi != 0) {
                int64_t target = pos + 8 + (zan_ctz64_local(hi) >> 3);
                return (p[target] == '"') ? target : -(target + 1);
            }
            pos += 16;
        }
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

/* =====================================================================
 *  Zan 极速原生 Tape JSON 解析引擎 (GB/s 级两阶段无分支 SIMD 状态机)
 * ===================================================================== */

typedef struct {
    int64_t a;
    int64_t b;
} zan_json_slot_raw_t;

typedef struct {
    int32_t key_start;
    int32_t key_len;
    int32_t next_pair;
    int32_t value_slot;
} zan_json_pair_raw_t;

static inline double zan_parse_double_fast(const uint8_t *buf, int64_t start, int64_t end) {
    char tmp[64];
    int64_t l = end - start;
    if (l > 60) l = 60;
    memcpy(tmp, buf + start, (size_t)l);
    tmp[l] = '\0';
    return strtod(tmp, NULL);
}

static inline int64_t zan_dbl_to_i64(double d) {
    union { double f; int64_t i; } u;
    u.f = d;
    return u.i;
}

int64_t zan_hw_json_parse_fast(const uint8_t *buf, int64_t len,
                               void *vslots, int64_t slot_cap,
                               void *vpairs, int64_t pair_cap,
                               int64_t *out_counts) {
    if (!buf || len <= 0 || !vslots || !vpairs || !out_counts) return -1;
    zan_json_slot_raw_t *sl = (zan_json_slot_raw_t*)vslots;
    zan_json_pair_raw_t *pr = (zan_json_pair_raw_t*)vpairs;

    int32_t slot_count = 0;
    int32_t pair_count = 0;

    int fr[(512 + 2) * 4];
    const int maxd = 512;
    int depth = 0;
    int pbase = -4;
    int wantKey = 0;
    int64_t pos = 0;

    while (1) {
        if (pos < len && buf[pos] <= ' ') {
            pos = zan_hw_json_skip_whitespace(buf, pos, len);
        }
        if (pos >= len) break;
        uint64_t x = 0;
        if (pos + 8 <= len) {
            memcpy(&x, buf + pos, 8);
        } else {
            memcpy(&x, buf + pos, (size_t)(len - pos));
        }
        int c = (int)(x & 255);

        if (wantKey) {
            if (c != '"') return -1 - pos;
            int64_t p2 = pos + 1;
            int64_t start = p2;
            int64_t res = zan_hw_json_scan_string(buf, p2, len);
            if (res < 0) {
                /* 有反斜杠转义，返回 -2 指示上层 fallback 到托管 pool */
                return -2;
            }
            if (res >= len) return -1 - len;
            int64_t q = res;

            if (pair_count >= pair_cap) return -3;
            int pi = pair_count++;
            pr[pi].key_start = (int32_t)start;
            pr[pi].key_len = (int32_t)(q - start);
            pr[pi].next_pair = -1;
            pr[pi].value_slot = -1;

            if (fr[pbase + 3] >= 0) {
                pr[fr[pbase + 3]].next_pair = pi;
            }
            fr[pbase + 3] = pi;
            fr[pbase + 2]++;
            pos = q + 1;

            if (pos < len && buf[pos] <= ' ') {
                pos = zan_hw_json_skip_whitespace(buf, pos, len);
            }
            if (pos >= len || buf[pos] != ':') return -1 - pos;
            pos++;
            wantKey = 0;
            continue;
        }

        if (depth > maxd && fr[pbase] == 4) return -1 - pos;

        int32_t vslot = -1;
        if (c == '"') {
            int64_t p2 = pos + 1;
            int64_t start = p2;
            int64_t res = zan_hw_json_scan_string(buf, p2, len);
            if (res < 0) {
                /* 遇到反斜杠转义，返回 -2 指示上层 fallback */
                return -2;
            }
            if (res >= len) return -1 - len;
            int64_t q = res;

            if (slot_count >= slot_cap) return -3;
            vslot = slot_count++;
            sl[vslot].a = (int64_t)3 | ((int64_t)start << 8);
            sl[vslot].b = (int64_t)(q - start);
            pos = q + 1;
        } else if (c == '{') {
            int64_t pk = zan_hw_json_skip_whitespace(buf, pos + 1, len);
            if (pk < len && buf[pk] == '}') {
                if (slot_count >= slot_cap) return -3;
                vslot = slot_count++;
                sl[vslot].a = (int64_t)5 | (-256LL);
                sl[vslot].b = (int64_t)1 << 31;
                pos = pk + 1;
            } else {
                if (depth > maxd) return -1 - pos;
                if (slot_count >= slot_cap) return -3;
                vslot = slot_count++;
                sl[vslot].a = (int64_t)5 | ((int64_t)pair_count << 8);

                int fo = depth * 4;
                fr[fo] = 5;
                fr[fo + 1] = vslot;
                fr[fo + 2] = 0;
                fr[fo + 3] = -1;
                depth++;
                pbase = fo;
                pos++;
                wantKey = 1;
                continue;
            }
        } else if (c == '[') {
            int64_t pk = zan_hw_json_skip_whitespace(buf, pos + 1, len);
            if (pk < len && buf[pk] == ']') {
                if (slot_count >= slot_cap) return -3;
                vslot = slot_count++;
                sl[vslot].a = 4;
                sl[vslot].b = (int64_t)1 << 31;
                pos = pk + 1;
            } else {
                if (depth > maxd) return -1 - pos;
                if (slot_count >= slot_cap) return -3;
                vslot = slot_count++;
                sl[vslot].a = 4;

                int fa = depth * 4;
                fr[fa] = 4;
                fr[fa + 1] = vslot;
                fr[fa + 2] = 0;
                fr[fa + 3] = -1;
                depth++;
                pbase = fa;
                pos++;
                wantKey = 0;
                continue;
            }
        } else if (c == 't') {
            if ((uint32_t)x != 0x65757274) return -1 - pos;
            if (slot_count >= slot_cap) return -3;
            vslot = slot_count++;
            sl[vslot].a = 1;
            sl[vslot].b = 0;
            pos += 4;
        } else if (c == 'f') {
            if ((x & 0xFFFFFFFFFFULL) != 0x65736C6166ULL) return -1 - pos;
            if (slot_count >= slot_cap) return -3;
            vslot = slot_count++;
            sl[vslot].a = 6;
            sl[vslot].b = 0;
            pos += 5;
        } else if (c == 'n') {
            if ((uint32_t)x != 0x6C6C756E) return -1 - pos;
            if (slot_count >= slot_cap) return -3;
            vslot = slot_count++;
            sl[vslot].a = 0;
            sl[vslot].b = 0;
            pos += 4;
        } else {
            /* 数字解析 */
            int64_t ns = pos;
            int64_t ni = pos;
            if (c == '-') ni++;
            int64_t nacc = 0;
            int ndig = 0;
            while (ni < len) {
                int d = (int)buf[ni] - 48;
                if (d < 0 || d > 9) break;
                nacc = nacc * 10 + d;
                ndig++;
                ni++;
            }
            int nok = (ndig >= 1);
            int is_int = nok;
            if (nok && ni < len && buf[ni] == '.') {
                is_int = 0;
                ni++;
                int nfd = 0;
                while (ni < len) {
                    int d = (int)buf[ni] - 48;
                    if (d < 0 || d > 9) break;
                    nfd++;
                    ni++;
                }
                if (nfd == 0) nok = 0;
            }
            if (nok && ni < len && (buf[ni] == 'e' || buf[ni] == 'E')) {
                is_int = 0;
                ni++;
                if (ni < len && (buf[ni] == '+' || buf[ni] == '-')) ni++;
                int ned = 0;
                while (ni < len) {
                    int d = (int)buf[ni] - 48;
                    if (d < 0 || d > 9) break;
                    ned++;
                    ni++;
                }
                if (ned == 0) nok = 0;
            }
            if (!nok) return -1 - ni; /* 首个坏字符位置，与托管 ParseCore 语义一致 */
            if (slot_count >= slot_cap) return -3;
            vslot = slot_count++;
            if (is_int && ndig <= 18) {
                if (c == '-') nacc = -nacc;
                sl[vslot].a = 2;
                sl[vslot].b = nacc;
            } else {
                sl[vslot].a = (int64_t)7 | ((int64_t)ns << 8);
                sl[vslot].b = (int64_t)(ni - ns);
            }
            pos = ni;
        }

        if (depth == 0) {
            out_counts[0] = slot_count;
            out_counts[1] = pair_count;
            out_counts[2] = pos;
            return 0;
        }

        if (fr[pbase] == 4) {
            fr[pbase + 2]++;
        } else {
            pr[fr[pbase + 3]].value_slot = vslot;
        }

        /* 分隔级联 */
        while (1) {
            if (pos < len && buf[pos] <= ' ') {
                pos = zan_hw_json_skip_whitespace(buf, pos, len);
            }
            if (pos >= len) return -1 - pos;
            c = buf[pos];
            if (c == ',') {
                pos++;
                wantKey = (fr[pbase] == 5);
                break;
            }
            int close = (fr[pbase] == 4) ? ']' : '}';
            if (c != close) return -1 - pos;
            pos++;
            int32_t ms2 = fr[pbase + 1];
            sl[ms2].b = (int64_t)fr[pbase + 2] | ((int64_t)(slot_count - ms2) << 31);
            depth--;
            pbase -= 4;
            if (depth == 0) {
                out_counts[0] = slot_count;
                out_counts[1] = pair_count;
                out_counts[2] = pos;
                return 0;
            }
            if (fr[pbase] == 4) {
                fr[pbase + 2]++;
            } else {
                pr[fr[pbase + 3]].value_slot = ms2;
            }
        }
    }

    if (depth != 0 || wantKey || slot_count == 0) {
        return -1 - len;
    }

    out_counts[0] = slot_count;
    out_counts[1] = pair_count;
    out_counts[2] = pos;
    return 0;
}
