/* Transitional Zan C-host runtime stubs (RT_OBJS): the full-stdlib pull
 * references the gen0 host entry points (NativeMemory.*, audio, Win codepage
 * APIs) that ngen_host maps to BLExtern. Implemented over libc where the
 * semantics are unambiguous; abort() where a call would mean an unported
 * path.
 *
 * Zan runtime handover: symbols already reimplemented in
 * runtime_core.zan (compiled by the native bootstrap compiler, localized
 * except for its zan_* exports) are compiled out with -DZAN_RT_CORE_ZAN so
 * the Zan object supplies them at link time. Batch 1: zan_monotonic_ns/us,
 * zan_sha256, zan_sha512. Batch 2: zan_alloc, zan_free, zan_crc32 (the
 * stdlib NativeMemory decls now carry matching EntryPoints).
 *
 * Legacy aliases (RETIRED 第十三批): artifacts produced from a pre-rename
 * stdlib snapshot (a stage1 compiled by an older seed beside its frozen
 * stdlib) still referenced the bare names. The bridge was to leave the bare
 * faces unguarded; the pinned seed now postdates the rename
 * (run.MrSWND snapshots the renamed stdlib), so the bare faces compile only
 * in the full C baseline (-UZAN_RT_CORE_ZAN) and zanstubs_rest.o no longer
 * defines them. Bootstrap from a pre-rename seed: stage a fresh stdlib
 * beside the seed binary instead of relying on this bridge. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>

long long zan_monotonic_ns(void);
void *zan_alloc(int size);
void zan_free(void *p);
long long zan_crc32(void *p, long long len);

#ifndef ZAN_RT_CORE_ZAN /* 第十三批起裸名仅存于 C 基线车道 */
void *Alloc(int size) { return zan_alloc(size); }
void Free(void *p) { zan_free(p); }
long long Crc32(void *p, long long len) { return zan_crc32(p, len); }
#endif /* ZAN_RT_CORE_ZAN */

#ifndef ZAN_RT_CORE_ZAN
void *zan_alloc(int size) { return calloc(1, (size_t)(size > 0 ? size : 1)); }
void zan_free(void *p) { free(p); }
#endif /* ZAN_RT_CORE_ZAN */

/* zan_open_creat(二参 open 包装,缺口一绕行)已退役:缺口一批次修好
 * 变参调用降级后,runtime_core.zan 直接以 [DllImport(Variadic=true)]
 * open(path, flags, 0644) 三参调用,双 ABI 车道金标验证
 * (native_varargs / native_varargs_elf)。 */
#ifndef ZAN_RT_CORE_ZAN /* 第十三批起裸名仅存于 C 基线车道 */
void Copy(void *dst, void *src, int n) { memmove(dst, src, (size_t)n); }
void Fill(void *p, int v, int n) { memset(p, v, (size_t)n); }
int Compare(void *a, void *b, int n) { return memcmp(a, b, (size_t)n); }
int Find(void *p, int off, int b, int n) {
    unsigned char *hit = memchr((unsigned char *)p + off, b, (size_t)n);
    return hit ? (int)(hit - (unsigned char *)p) : -1;
}
void PutString(void *p, int off, unsigned char *s, int len) {
    if (s && len > 0) memcpy((unsigned char *)p + off, s, (size_t)len);
}
/* string GetString(nint p, int off, int len): the oracle inlines this
 * (irgen_expr.c) into alloc+memcpy+NUL+len-stamp; we emit BLExtern, so
 * build the same object by hand: raw = [rc:8][header:8][data][NUL],
 * header = 0x5A414E53_00000000 | len, user pointer = raw + 16. */
void *GetString(void *p, int off, int len) {
    if (len < 0 || p == 0) { len = 0; }
    char *raw = (char *)malloc((size_t)len + 17);
    if (!raw) { return 0; }
    *(long long *)(void *)raw = 1;                       /* rc = 1 */
    *(long long *)(void *)(raw + 8) = (long long)0x5A414E5300000000LL | (long long)(unsigned int)len;
    unsigned char *data = (unsigned char *)raw + 16;
    if (len > 0) { memcpy(data, (unsigned char *)p + off, (size_t)len); }
    data[len] = 0;
    return data;
}

#ifndef ZAN_RT_CORE_ZAN
/* long long zan_crc32(nint p, long long len): IEEE 802.3 reflected CRC32
 * (poly 0xEDB88320), table-driven, matching the oracle's nm_crc32_fn. */
long long zan_crc32(void *p, long long len) {
    static unsigned int table[256];
    static int ready = 0;
    if (!ready) {
        for (unsigned int i = 0; i < 256; i++) {
            unsigned int c = i;
            for (int k = 0; k < 8; k++) { c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1); }
            table[i] = c;
        }
        ready = 1;
    }
    unsigned int c = 0xFFFFFFFFu;
    unsigned char *b = (unsigned char *)p;
    for (long long i = 0; p && i < len; i++) { c = table[(c ^ b[i]) & 0xFF] ^ (c >> 8); }
    /* zero-extend: the oracle widens zext i32->i64 (irgen.zan), so a signed
     * int return prints the crc negative from Zan */
    return (long long)(c ^ 0xFFFFFFFFu);
}
/* void zan_sha512(data, len, md): FIPS 180-4 SHA-512, self-contained — the
 * emitted runtime calls this instead of CC_SHA512, which misbehaved through
 * the chained-fixup stub path (a direct C call in the same binary produced
 * the correct digest while the stub call did not; the oracle also inlines
 * its digest implementations, so self-containment is the house style). */
static unsigned long long zsha_rot64(unsigned long long x, int n) {
    return (x >> n) | (x << (64 - n));
}
static void zsha_block(unsigned long long *h, const unsigned char *p) {
    static const unsigned long long K[80] = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
        0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
        0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
        0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
        0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
        0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
        0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
        0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
        0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
        0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
        0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
        0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
        0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
        0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
        0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
        0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
    };
    unsigned long long w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = 0;
        for (int j = 0; j < 8; j++) { w[i] = (w[i] << 8) | p[i * 8 + j]; }
    }
    for (int i = 16; i < 80; i++) {
        unsigned long long s0 = zsha_rot64(w[i-15], 1) ^ zsha_rot64(w[i-15], 8) ^ (w[i-15] >> 7);
        unsigned long long s1 = zsha_rot64(w[i-2], 19) ^ zsha_rot64(w[i-2], 61) ^ (w[i-2] >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    unsigned long long a = h[0], b = h[1], c = h[2], d = h[3];
    unsigned long long e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 80; i++) {
        unsigned long long S1 = zsha_rot64(e, 14) ^ zsha_rot64(e, 18) ^ zsha_rot64(e, 41);
        unsigned long long ch = (e & f) ^ (~e & g);
        unsigned long long t1 = hh + S1 + ch + K[i] + w[i];
        unsigned long long S0 = zsha_rot64(a, 28) ^ zsha_rot64(a, 34) ^ zsha_rot64(a, 39);
        unsigned long long maj = (a & b) ^ (a & c) ^ (b & c);
        unsigned long long t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
void zan_sha512(const void *data, long long len, unsigned char *md) {
    unsigned long long h[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
    };
    const unsigned char *msg = (const unsigned char *)data;
    long long total = len > 0 ? len : 0;
    long long off = 0;
    while (off + 128 <= total) { zsha_block(h, msg + off); off += 128; }
    unsigned char block[128];
    long long tail = total - off;                 /* 0..127 */
    memcpy(block, msg + off, (size_t)tail);
    block[tail] = 0x80;
    if (tail + 1 > 112) {
        /* no room for the 128-bit length in this block: flush it and use a
         * fresh all-zero final block */
        memset(block + tail + 1, 0, (size_t)(128 - tail - 1));
        zsha_block(h, block);
        memset(block, 0, 120);
        tail = -1;
    }
    memset(block + tail + 1, 0, (size_t)(120 - tail - 1));
    unsigned long long bits = (unsigned long long)total * 8;
    for (int i = 0; i < 8; i++) { block[120 + i] = (unsigned char)(bits >> (56 - 8 * i)); }
    zsha_block(h, block);
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) { md[i * 8 + j] = (unsigned char)(h[i] >> (56 - 8 * j)); }
    }
}

/* void zan_sha256(data, len, md): FIPS 180-4 SHA-256, self-contained like
 * zan_sha512 — the NativeMemory.Sha256 intrinsic's ngen lowering calls this
 * with (data, len>=0 clamped by the caller, out32). Same house style as the
 * oracle, which inlines its own digest code. */
static unsigned int zsha_rot32(unsigned int x, int n) {
    return (x >> n) | (x << (32 - n));
}
static void zsha256_block(unsigned int *h, const unsigned char *p) {
    static const unsigned int K[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    unsigned int w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((unsigned int)p[i * 4] << 24) | ((unsigned int)p[i * 4 + 1] << 16)
             | ((unsigned int)p[i * 4 + 2] << 8) | (unsigned int)p[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        unsigned int s0 = zsha_rot32(w[i-15], 7) ^ zsha_rot32(w[i-15], 18) ^ (w[i-15] >> 3);
        unsigned int s1 = zsha_rot32(w[i-2], 17) ^ zsha_rot32(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    unsigned int a = h[0], b = h[1], c = h[2], d = h[3];
    unsigned int e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        unsigned int S1 = zsha_rot32(e, 6) ^ zsha_rot32(e, 11) ^ zsha_rot32(e, 25);
        unsigned int ch = (e & f) ^ (~e & g);
        unsigned int t1 = hh + S1 + ch + K[i] + w[i];
        unsigned int S0 = zsha_rot32(a, 2) ^ zsha_rot32(a, 13) ^ zsha_rot32(a, 22);
        unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
        unsigned int t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
void zan_sha256(const void *data, long long len, unsigned char *md) {
    unsigned int h[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    const unsigned char *msg = (const unsigned char *)data;
    long long total = len > 0 ? len : 0;
    long long off = 0;
    while (off + 64 <= total) { zsha256_block(h, msg + off); off += 64; }
    unsigned char block[64];
    long long tail = total - off;                 /* 0..63 */
    memcpy(block, msg + off, (size_t)tail);
    block[tail] = 0x80;
    if (tail + 1 > 56) {
        memset(block + tail + 1, 0, (size_t)(64 - tail - 1));
        zsha256_block(h, block);
        memset(block, 0, 56);
        tail = -1;
    }
    memset(block + tail + 1, 0, (size_t)(56 - tail - 1));
    unsigned long long bits = (unsigned long long)total * 8;
    for (int i = 0; i < 8; i++) { block[56 + i] = (unsigned char)(bits >> (56 - 8 * i)); }
    zsha256_block(h, block);
    for (int i = 0; i < 8; i++) {
        md[i * 4] = (unsigned char)(h[i] >> 24);
        md[i * 4 + 1] = (unsigned char)(h[i] >> 16);
        md[i * 4 + 2] = (unsigned char)(h[i] >> 8);
        md[i * 4 + 3] = (unsigned char)(h[i]);
    }
}

long long zan_monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000ll + (long long)ts.tv_nsec;
}
#endif /* ZAN_RT_CORE_ZAN */
/* ---- Win32 宽文本对,CP_UTF8(65001)真实实现(第十三批:原 return-0
 * stub 是阉割件,Wide.Of/Read 在其上产出空串)。flags=0 语义:非法字节
 * 按 U+FFFD 替换(Win32 现行 CP_UTF8 行为);sl/wl<0 表 NUL 终止(计数
 * 含 NUL);缓冲 0 表量尺;缓冲不足返 0;他代码页返 0(不支持)。 ---- */
int MultiByteToWideChar(unsigned int cp, unsigned long fl, const char *s, int sl, unsigned short *w, int wl) {
    if (cp != 65001u || s == 0) { return 0; }
    int n = sl;
    if (n < 0) { n = (int)strlen(s); }
    if (n == 0) { return 0; }
    int units = 0, i = 0;
    while (i < n) {
        unsigned char b0 = (unsigned char)s[i];
        long cpw; int adv;
        if (b0 < 0x80) { cpw = b0; adv = 1; }
        else if ((b0 & 0xE0) == 0xC0) { cpw = b0 & 0x1F; adv = 2; }
        else if ((b0 & 0xF0) == 0xE0) { cpw = b0 & 0x0F; adv = 3; }
        else if ((b0 & 0xF8) == 0xF0) { cpw = b0 & 0x07; adv = 4; }
        else { cpw = -1; adv = 1; }
        if (cpw >= 0) {
            if (i + adv > n) { cpw = -1; }
            else {
                for (int k = 1; k < adv; k++) {
                    unsigned char bx = (unsigned char)s[i + k];
                    if ((bx & 0xC0) != 0x80) { cpw = -1; break; }
                    cpw = (cpw << 6) | (bx & 0x3F);
                }
            }
        }
        if (cpw >= 0 && ((adv == 2 && cpw < 0x80) || (adv == 3 && cpw < 0x800)
                || (adv == 4 && cpw < 0x10000) || cpw > 0x10FFFF)) { cpw = -1; }
        if (cpw < 0) { cpw = 0xFFFD; adv = 1; }
        int cu = cpw > 0xFFFF ? 2 : 1;
        if (w == 0 || wl == 0) { units += cu; }
        else {
            if (units + cu > wl) { return 0; }
            if (cu == 2) {
                cpw -= 0x10000;
                w[units] = (unsigned short)(0xD800 + (cpw >> 10));
                w[units + 1] = (unsigned short)(0xDC00 + (cpw & 0x3FF));
            } else {
                w[units] = (unsigned short)cpw;
            }
            units += cu;
        }
        i += adv;
    }
    if (sl < 0) {
        units += 1;
        if (w != 0 && wl != 0) {
            if (units > wl) { return 0; }
            w[units - 1] = 0;
        }
    }
    return units;
}
int WideCharToMultiByte(unsigned int cp, unsigned long fl, const unsigned short *w, int wl, char *s, int sl, char *dc, int *du) {
    if (cp != 65001u || w == 0) { return 0; }
    int n = wl;
    if (n < 0) { n = 0; while (w[n] != 0) { n++; } }
    if (n == 0) { return 0; }
    int bytes = 0, i = 0;
    while (i < n) {
        unsigned u = w[i];
        long cpw;
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < n
                && (unsigned)w[i + 1] >= 0xDC00 && (unsigned)w[i + 1] <= 0xDFFF) {
            cpw = 0x10000 + ((long)(u - 0xD800) << 10) + (w[i + 1] - 0xDC00);
            i += 2;
        } else {
            cpw = (u >= 0xD800 && u <= 0xDFFF) ? 0xFFFD : (long)u;
            i += 1;
        }
        int len = cpw < 0x80 ? 1 : cpw < 0x800 ? 2 : cpw < 0x10000 ? 3 : 4;
        if (s == 0) { bytes += len; continue; }
        if (bytes + len > sl) { return 0; }
        if (len == 1) { s[bytes] = (char)cpw; }
        else if (len == 2) {
            s[bytes] = (char)(0xC0 | (cpw >> 6));
            s[bytes + 1] = (char)(0x80 | (cpw & 0x3F));
        } else if (len == 3) {
            s[bytes] = (char)(0xE0 | (cpw >> 12));
            s[bytes + 1] = (char)(0x80 | ((cpw >> 6) & 0x3F));
            s[bytes + 2] = (char)(0x80 | (cpw & 0x3F));
        } else {
            s[bytes] = (char)(0xF0 | (cpw >> 18));
            s[bytes + 1] = (char)(0x80 | ((cpw >> 12) & 0x3F));
            s[bytes + 2] = (char)(0x80 | ((cpw >> 6) & 0x3F));
            s[bytes + 3] = (char)(0x80 | (cpw & 0x3F));
        }
        bytes += len;
    }
    if (wl < 0) {
        if (s == 0) { bytes += 1; }
        else {
            if (bytes + 1 > sl) { return 0; }
            s[bytes] = 0;
            bytes += 1;
        }
    }
    if (du != 0) { *du = 0; }
    return bytes;
}
#endif /* ZAN_RT_CORE_ZAN */
/* 第十三批 zan_ 别名:stdlib 的 EntryPoint 改名指向这里;Zan 车道由
 * runtime_core.zan 提供同名符号,本文件 -DZAN_RT_CORE_ZAN 编出。 */
#ifndef ZAN_RT_CORE_ZAN
void zan_copy(void *dst, void *src, int n) { Copy(dst, src, n); }
void zan_fill(void *p, int v, int n) { Fill(p, v, n); }
int zan_compare(void *a, void *b, int n) { return Compare(a, b, n); }
int zan_find(void *p, int off, int b, int n) { return Find(p, off, b, n); }
void *zan_get_string(void *p, int off, int len) { return GetString(p, off, len); }
void zan_put_string(void *p, int off, unsigned char *s, int len) { PutString(p, off, s, len); }
int zan_multi_byte_to_wide_char(unsigned int cp, unsigned long fl, const char *s, int sl, unsigned short *w, int wl) { return MultiByteToWideChar(cp, fl, s, sl, w, wl); }
int zan_wide_char_to_multi_byte(unsigned int cp, unsigned long fl, const unsigned short *w, int wl, char *s, int sl, char *dc, int *du) { return WideCharToMultiByte(cp, fl, w, wl, s, sl, dc, du); }
#endif

/* ---- audio honest stubs — darwin REST 车道由下面的 zan_audio.c 真身
 * 独占(b41 起);其余车道(full 基线、ELF C remainder)由这里的诚实桩
 * 供给:native_rt_core 车辆测试在 ELF 车道 DllImport 全家,旧守卫
 * (-D 一律编出)只靠陈旧 rt 缓存遮掩,缓存一清即断链。诚实桩无状态、
 * 与真身不重定义,按"非 darwin-rest"条件编译。 */
#ifndef ZAN_RT_CORE_ZAN

int zan_audio_open(void) { return 0; }
void zan_audio_close(void) { }
int zan_audio_is_open(void) { return 0; }
const char *zan_audio_driver_name(void) { return "stub"; }
const char *zan_audio_last_error(void) { return "stub"; }
double zan_audio_volume(void) { return 0.0; }
void zan_audio_set_volume(double v) { (void)v; }
int zan_audio_active_voices(void) { return 0; }
void zan_audio_stop_all(void) { }
long long zan_audio_load_wav(const void *d, long long n) { (void)d;(void)n; return 0; }
long long zan_audio_load_ogg(const void *d, long long n) { (void)d;(void)n; return 0; }
long long zan_audio_load_wav_mem(const void *d, long long n) { (void)d;(void)n; return 0; }
long long zan_audio_load_ogg_mem(const void *d, long long n) { (void)d;(void)n; return 0; }
void zan_audio_free_clip(long long c) { (void)c; }
double zan_audio_clip_duration_ms(long long c) { (void)c; return 0.0; }
int zan_audio_clip_channels(long long c) { (void)c; return 0; }
int zan_audio_clip_frequency(long long c) { (void)c; return 0; }
int zan_audio_play(long long c, int loop) { (void)c;(void)loop; return -1; }
int zan_audio_voice_playing(int v) { (void)v; return 0; }
void zan_audio_voice_stop(int v) { (void)v; }
void zan_audio_voice_set_gain(int v, double g) { (void)v;(void)g; }

#endif /* ZAN_RT_CORE_ZAN */

/* ---- lock-statement monitor (spec: oracle rt_sync.c zan_monitor_*) ----
 * `lock (obj)` 给每个对象一个监视器；进程级单把锁会让锁无关对象的线程
 * 互相等待，所以按对象地址条纹化：指针折叠哈希选 64 把递归锁之一。
 * 两个对象共享条纹只会过度串行，不丢互斥；递归保证重入合法。 */
#ifndef ZAN_RT_CORE_ZAN
#define ZT_MONITOR_STRIPES 64

static pthread_mutex_t zt_monitor_mx[ZT_MONITOR_STRIPES];
static pthread_once_t zt_monitor_once = PTHREAD_ONCE_INIT;

static unsigned zt_monitor_stripe(void *obj) {
    uintptr_t bits = (uintptr_t)obj;
    bits ^= bits >> 20;
    bits ^= bits >> 8;
    return (unsigned)((bits >> 4) & (ZT_MONITOR_STRIPES - 1));
}

static void zt_monitor_init(void) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    for (unsigned i = 0; i < ZT_MONITOR_STRIPES; i++)
        pthread_mutex_init(&zt_monitor_mx[i], &attr);
    pthread_mutexattr_destroy(&attr);
}

void zan_monitor_enter(void *obj) {
    pthread_once(&zt_monitor_once, zt_monitor_init);
    pthread_mutex_lock(&zt_monitor_mx[zt_monitor_stripe(obj)]);
}

void zan_monitor_exit(void *obj) {
    pthread_mutex_unlock(&zt_monitor_mx[zt_monitor_stripe(obj)]);
}

#endif /* ZAN_RT_CORE_ZAN */

/* ---- threads (spec: oracle src/runtime/rt_sync.c zan_thread_*) ----
 * 本过渡运行时只服务我们自己 ngen 产出的目标码：那边传入的 ThreadStart
 * 委托对象就是 {fn@0, env@8}，调用约定 fn(env)，且目标码不引用
 * _zan_delegate_retain/release（已 nm 核实），所以 trampoline 不做引用
 * 计数，也不碰异常栈状态。 */
#include <pthread.h>
#include <stdint.h>

/* 每线程 EH 状态块（oracle 用 C TLV __zan_eh_self）。块布局由编译器
 * 拥有（{i32 top, ptr exc, 256 x 1024 字节 setjmp 槽}），这里只负责把
 * 存储做成每线程一份：之前状态块指针缓存在进程级全局里，线程会把
 * longjmp 打进别人 armed 的槽（exception_threads 正是抓这个的）。 */
#ifndef ZAN_RT_CORE_ZAN
static __thread void *zan_eh_tls;
void *zan_eh_tls_state(void) {
    if (!zan_eh_tls) {
        zan_eh_tls = calloc(1, 16 + 256 * 1024);
        if (!zan_eh_tls) abort();
    }
    return zan_eh_tls;
}

/* 缺口五起 Zan 车道的 trampoline 是 runtime_core.zan 的
 * [ThreadEntry] thread_trampoline（x19-x28 序言/收尾替代这里的裸函数
 * 屏障），本段只在 C 基线车道编出。 */
/* Zan 方法体把 x19-x25 当作跨调用存活的工作寄存器（方法序言只保存
 * x29/x30，Zan 世界内部自洽），但 POSIX 线程入口是 C→Zan 边界：
 * libpthread 的 _pthread_start 在被调用者保存寄存器里放着线程自指针，
 * 被工作线程 clobber 后 _pthread_exit 拿到代码地址当 self，
 * 线程退出时在 self+0xa6 上 CAS 崩溃（dispatch_first_use 抓到）。
 * oracle 的方法体全程用栈槽不触碰这些寄存器，因此不需要此屏障；
 * 这层裸函数在任何 Zan 方法体外先存后取 x19-x28。 */
extern void *zan_thread_trampoline_body(void *arg);
#if defined(__aarch64__) || defined(__arm64__)
__attribute__((naked)) void *zan_thread_trampoline(void *arg) {
    __asm__ volatile(
        "stp x19, x20, [sp, #-0x10]!\n"
        "stp x21, x22, [sp, #-0x10]!\n"
        "stp x23, x24, [sp, #-0x10]!\n"
        "stp x25, x26, [sp, #-0x10]!\n"
        "stp x27, x28, [sp, #-0x10]!\n"
        "stp x29, x30, [sp, #-0x10]!\n"
        "bl _zan_thread_trampoline_body\n"
        "ldp x29, x30, [sp], #0x10\n"
        "ldp x27, x28, [sp], #0x10\n"
        "ldp x25, x26, [sp], #0x10\n"
        "ldp x23, x24, [sp], #0x10\n"
        "ldp x21, x22, [sp], #0x10\n"
        "ldp x19, x20, [sp], #0x10\n"
        "ret\n");
}
#else
void *zan_thread_trampoline(void *arg) {
    return zan_thread_trampoline_body(arg);
}
#endif

void *zan_thread_trampoline_body(void *arg) {
    void **closure = (void **)arg;
    void (*fn)(void *) = (void (*)(void *))closure[0];
    void *env = closure[1];
    if (fn) fn(env);
    return NULL;
}

int32_t zan_thread_start(void *body) {
    if (!body) return 0;
    pthread_t t;
    if (pthread_create(&t, NULL, zan_thread_trampoline, body) != 0) return 0;
    pthread_detach(t);
    return 1;
}

int64_t zan_thread_current_id(void) {
    /* pthread_self() 是指针、逐次漂移；pthread_threadid_np 给出稳定的
     * 每线程数值 id（与 oracle 的 macOS 分支一致）。 */
    uint64_t tid = 0;
    if (pthread_threadid_np(NULL, &tid) != 0) return 0;
    return (int64_t)tid;
}
#endif /* ZAN_RT_CORE_ZAN */

/* ---- 第十四批 zan_ 别名:编译器发射面(digests/guid/exedir/threadid/
 * string_header/guard)与 stdlib AppPath 已改指 runtime_core.zan 的
 * 目标无关符号;C 基线车道(-UZAN_RT_CORE_ZAN)用同语义平台实现补齐,
 * 保证 A/B 对拍在两条车道都可链。mach/CommonCrypto 部分按 __APPLE__ 收口。 */
#ifndef ZAN_RT_CORE_ZAN

#include <stdint.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonCrypto.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <os/lock.h>
#else
#include <unistd.h>
#endif

void zan_md5(const void *data, long long len, unsigned char *md) {
#ifdef __APPLE__
    CC_MD5(data, (CC_LONG)len, md);
#else
    (void)data; (void)len; (void)md;
#endif
}

void zan_sha1(const void *data, long long len, unsigned char *md) {
#ifdef __APPLE__
    CC_SHA1(data, (CC_LONG)len, md);
#else
    (void)data; (void)len; (void)md;
#endif
}

void zan_hmac_sha256(const void *key, long long klen,
                     const void *data, long long dlen, unsigned char *mac) {
#ifdef __APPLE__
    CCHmac(kCCHmacAlgSHA256, key, (size_t)klen, data, (size_t)dlen, mac);
#else
    (void)key; (void)klen; (void)data; (void)dlen; (void)mac;
#endif
}

void zan_urandom(void *buf, long long len) {
#ifdef __APPLE__
    arc4random_buf(buf, (size_t)len);
#else
    (void)buf; (void)len;
#endif
}

int zan_vm_read(void *dst, void *addr, long long len) {
#ifdef __APPLE__
    mach_vm_size_t outsize = 0;
    return mach_vm_read_overwrite(mach_task_self(), (mach_vm_address_t)(uintptr_t)addr,
                                  (mach_vm_size_t)len, (mach_vm_address_t)(uintptr_t)dst,
                                  &outsize);
#else
    (void)dst; (void)addr; (void)len;
    return -1;
#endif
}

/* 发射器 guard 串行化:地址即锁的 8 字节零初始化槽。os_unfair_lock 的
 * 文档初始化态就是全零,直接落在调用方槽位上,与 Zan 实现的槽布局一致。 */
int zan_lock_enter(void *p) {
#ifdef __APPLE__
    if (!p) return 22;
    os_unfair_lock_lock((os_unfair_lock_t)p);
    return 0;
#else
    (void)p; return 22;
#endif
}

int zan_lock_exit(void *p) {
#ifdef __APPLE__
    if (!p) return 22;
    os_unfair_lock_unlock((os_unfair_lock_t)p);
    return 0;
#else
    (void)p; return 22;
#endif
}

int zan_exe_path(void *buf, void *capp) {
#ifdef __APPLE__
    return _NSGetExecutablePath((char *)buf, (uint32_t *)capp);
#else
    if (!buf || !capp) return -1;
    long cap = *(long *)capp;
    if (cap <= 0) return -1;
    long n = readlink("/proc/self/exe", (char *)buf, (size_t)cap - 1);
    if (n < 0 || n >= cap - 1) {
        if (n >= cap - 1) *(long *)capp = n + 1;
        return -1;
    }
    ((char *)buf)[n] = 0;
    return 0;
#endif
}

#endif /* ZAN_RT_CORE_ZAN */

/* ---- UI-thread dispatch queue (spec: oracle rt_sync.c zan_dispatch_*) ----
 * The oracle ring retains a posted closure and releases on take/clear via the
 * record's own dtor. Selfhost delegate values are interned process-lifetime
 * blocks (_zan_dlg_intern) that never die, so entries move as plain pointers;
 * all correctness lives in the OS mutex, which workers hit concurrently with
 * the UI thread's drain. Post answers 1 on success -- callers branch on it.
 * 第七批起由 runtime_core.zan 提供(CAS 自旋锁版),这里 -D 编出。 */
#ifndef ZAN_RT_CORE_ZAN
#define ZAN_DISPATCH_CAP0 64
#define ZAN_DISPATCH_CAP_MAX (1u << 20)
static void *g_dispatch_static[ZAN_DISPATCH_CAP0];
static void **g_dispatch_ring = g_dispatch_static;
static int g_dispatch_cap = ZAN_DISPATCH_CAP0;
static int g_dispatch_head = 0;
static int g_dispatch_tail = 0;
static pthread_mutex_t g_dispatch_mx = PTHREAD_MUTEX_INITIALIZER;

void zan_dispatch_init(void) {
    pthread_mutex_lock(&g_dispatch_mx);
    g_dispatch_head = 0;
    g_dispatch_tail = 0;
    pthread_mutex_unlock(&g_dispatch_mx);
}

static int zan_dispatch_grow(void) {
    if ((unsigned)g_dispatch_cap >= ZAN_DISPATCH_CAP_MAX) return 0;
    int ncap = g_dispatch_cap * 2;
    void **nring = (void **)malloc((size_t)ncap * sizeof *nring);
    if (!nring) return 0;
    int count = 0;
    int i = g_dispatch_head;
    while (i != g_dispatch_tail) {
        nring[count++] = g_dispatch_ring[i];
        i = (i + 1) % g_dispatch_cap;
    }
    if (g_dispatch_ring != g_dispatch_static) free(g_dispatch_ring);
    g_dispatch_ring = nring;
    g_dispatch_cap = ncap;
    g_dispatch_head = 0;
    g_dispatch_tail = count;
    return 1;
}

int32_t zan_dispatch_post(void *fn) {
    if (!fn) return 0;
    int32_t ok = 0;
    pthread_mutex_lock(&g_dispatch_mx);
    int next = (g_dispatch_tail + 1) % g_dispatch_cap;
    if (next == g_dispatch_head && zan_dispatch_grow())
        next = (g_dispatch_tail + 1) % g_dispatch_cap;
    if (next != g_dispatch_head) {
        g_dispatch_ring[g_dispatch_tail] = fn;
        g_dispatch_tail = next;
        ok = 1;
    }
    pthread_mutex_unlock(&g_dispatch_mx);
    return ok;
}

void *zan_dispatch_take(void) {
    void *fn = NULL;
    pthread_mutex_lock(&g_dispatch_mx);
    if (g_dispatch_head != g_dispatch_tail) {
        fn = g_dispatch_ring[g_dispatch_head];
        g_dispatch_head = (g_dispatch_head + 1) % g_dispatch_cap;
    }
    pthread_mutex_unlock(&g_dispatch_mx);
    return fn;
}

void zan_dispatch_clear(void) {
    pthread_mutex_lock(&g_dispatch_mx);
    g_dispatch_head = 0;
    g_dispatch_tail = 0;
    pthread_mutex_unlock(&g_dispatch_mx);
}

#endif /* ZAN_RT_CORE_ZAN */

/* ---- atomic int (spec: oracle rt_sync.c zan_atomic_int_*，C11 __atomic) ----
 * 第六批起由 runtime_core.zan 提供,这里 -D 编出(RMW 走 OSAtomic,
 * 见该文件第六批追加约束)。 */
#ifndef ZAN_RT_CORE_ZAN

int64_t zan_atomic_int_create(int64_t initial_value) {
    int64_t *p = (int64_t *)malloc(sizeof(int64_t));
    if (!p) return 0;
    *p = initial_value;
    return (int64_t)(intptr_t)p;
}

void zan_atomic_int_destroy(int64_t handle) {
    free((void *)(intptr_t)handle);
}

int64_t zan_atomic_int_load(int64_t handle) {
    int64_t *p = (int64_t *)(intptr_t)handle;
    if (!p) return 0;
    return __atomic_load_n(p, __ATOMIC_SEQ_CST);
}

void zan_atomic_int_store(int64_t handle, int64_t new_value) {
    int64_t *p = (int64_t *)(intptr_t)handle;
    if (!p) return;
    __atomic_store_n(p, new_value, __ATOMIC_SEQ_CST);
}

int64_t zan_atomic_int_add(int64_t handle, int64_t delta) {
    int64_t *p = (int64_t *)(intptr_t)handle;
    if (!p) return 0;
    return __atomic_add_fetch(p, delta, __ATOMIC_SEQ_CST);
}

int64_t zan_atomic_int_exchange(int64_t handle, int64_t new_value) {
    int64_t *p = (int64_t *)(intptr_t)handle;
    if (!p) return 0;
    return __atomic_exchange_n(p, new_value, __ATOMIC_SEQ_CST);
}

int64_t zan_atomic_int_compare_exchange(
    int64_t handle, int64_t expected, int64_t desired) {
    int64_t *p = (int64_t *)(intptr_t)handle;
    if (!p) return 0;
    int64_t seen = expected;
    (void)__atomic_compare_exchange_n(
        p, &seen, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return seen; /* oracle 语义：返回比较时看到的旧值 */
}

#endif /* ZAN_RT_CORE_ZAN */

/* ---- shared table（单进程诚实移植，规格：oracle rt_sync.c）----
 * oracle 用跨进程 mmap + 哈希槽；回归测试全是单进程的，这里换成
 * 进程内注册表 + 堆上行式存储，但语义逐条对齐：
 *   - 列 schema "i:name;"/"f:name;"/"s:<n>:name;"，字符串列占 n+1 字节
 *     （含 NUL），int/float 8 字节且按 8 对齐，重名列/坏 schema 拒绝；
 *   - 键哈希 = oracle 同款 FNV-1a 64、强制非零，_at 族按哈希寻行；
 *   - 过期用 wall clock（CLOCK_REALTIME 毫秒），expire/purge/rate/lock
 *     算法从 rt_sync.c 原样移植（含 rate 的 count/window_start 双 int 列、
 *     lock 的 INT 列 "owner"、仅 absent 建行、min 时 0 视为未设）；
 *   - 容量到达 7/8（oracle 载荷因子）拒绝建行；
 *   - Destroy 只把名字从注册表摘除（POSIX unlink 语义：既有句柄仍可
 *     读写）；表永不 free，避免悬垂句柄——测试进程即退即收。
 * 跨进程面（OsHandle/Attach）在过渡运行时没有承载物，诚实失败：具名表
 * 按 Oracle 文档本就返回 0，匿名表与 Attach 返回 0（打开失败的表）。
 * 第八批起由 runtime_core.zan 提供(块布局自定、自旋锁互斥),这里
 * -D 编出。 */
#ifndef ZAN_RT_CORE_ZAN

#define ZT_MAX_COLUMNS 32
#define ZT_MAX_STRING 65536
#define ZT_MAX_CAPACITY (1 << 20)

typedef struct {
    char name[64];
    int type;   /* 0 int, 1 float, 2 string */
    int size;   /* int/float 8；字符串 = 解析值 + 1 */
    int offset;
} zt_col;

typedef struct zt_row {
    struct zt_row *next;
    uint64_t hash;
    char *key;          /* 建行时的键副本 */
    int alive;          /* 0 = tombstone，保留槽位 */
    int64_t expires_at; /* 0 = 无期限 */
    void *data;         /* stride 字节 */
} zt_row;

typedef struct zt_table {
    char *name;         /* NULL = 匿名 */
    int cap, keysize, ncol, stride;
    zt_col cols[ZT_MAX_COLUMNS];
    zt_row *rows;       /* 含 tombstone 的单链表 */
    int count;          /* alive 行数 */
    int linked;         /* 在注册表里（可被 Open） */
    pthread_mutex_t mu;
} zt_table;

static zt_table *zt_registry[64];
static int zt_reg_n;
static pthread_mutex_t zt_reg_mu = PTHREAD_MUTEX_INITIALIZER;

static int64_t zt_wall_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static uint64_t zt_hash(const char *value) {
    uint64_t h = UINT64_C(1469598103934665603);
    const unsigned char *p = (const unsigned char *)(value ? value : "");
    while (*p) {
        h ^= *p++;
        h *= UINT64_C(1099511628211);
    }
    return h ? h : 1;
}

static int zt_align8(int v) { return (v + 7) & ~7; }

static int zt_parse_schema(const char *schema, zt_table *t) {
    const char *p = schema;
    int offset = 0;
    if (!schema || !*schema) return 0;
    while (*p) {
        if (t->ncol >= ZT_MAX_COLUMNS) return 0;
        zt_col *c = &t->cols[t->ncol];
        if (*p == 'i') {
            c->type = 0; c->size = 8; p++;
            if (*p++ != ':') return 0;
        } else if (*p == 'f') {
            c->type = 1; c->size = 8; p++;
            if (*p++ != ':') return 0;
        } else if (*p == 's') {
            char *end = NULL;
            unsigned long parsed;
            c->type = 2; p++;
            if (*p++ != ':') return 0;
            parsed = strtoul(p, &end, 10);
            if (end == p || !end || *end != ':' ||
                parsed == 0 || parsed > ZT_MAX_STRING) return 0;
            c->size = (int)parsed + 1; /* 含 NUL，同 oracle */
            p = end + 1;
        } else {
            return 0;
        }
        const char *name = p;
        while (*p && *p != ';') p++;
        size_t name_len = (size_t)(p - name);
        if (*p != ';' || name_len == 0 || name_len >= sizeof(c->name)) return 0;
        for (int i = 0; i < t->ncol; i++) {
            if (strlen(t->cols[i].name) == name_len &&
                memcmp(t->cols[i].name, name, name_len) == 0) return 0;
        }
        memset(c->name, 0, sizeof(c->name));
        memcpy(c->name, name, name_len);
        if (c->type == 0 || c->type == 1) offset = zt_align8(offset);
        c->offset = offset;
        offset += c->size;
        t->ncol++;
        p++;
    }
    t->stride = offset;
    return 1;
}

static zt_table *zt_new_table(const char *name, int cap, int keysize) {
    zt_table *t = (zt_table *)calloc(1, sizeof(zt_table));
    if (!t) return NULL;
    if (name) {
        t->name = strdup(name);
        if (!t->name) { free(t); return NULL; }
    }
    t->cap = cap;
    t->keysize = keysize;
    pthread_mutex_init(&t->mu, NULL);
    return t;
}

int64_t zan_shared_table_create(
    const char *name, int32_t capacity, int32_t key_size, const char *schema) {
    if (!name || !*name || capacity <= 0 || capacity > ZT_MAX_CAPACITY ||
        key_size <= 0 || key_size > 4096)
        return 0;
    zt_table *t = zt_new_table(name, capacity, key_size);
    if (!t) return 0;
    if (!zt_parse_schema(schema, t)) {
        free(t->name);
        free(t);
        return 0;
    }
    int ok = 1;
    pthread_mutex_lock(&zt_reg_mu);
    for (int i = 0; i < zt_reg_n; i++) {
        if (zt_registry[i]->linked && strcmp(zt_registry[i]->name, name) == 0) {
            ok = 0; /* 已有同名具名表：拒绝（O_CREAT|O_EXCL 语义） */
            break;
        }
    }
    if (ok && zt_reg_n < (int)(sizeof(zt_registry) / sizeof(zt_registry[0]))) {
        t->linked = 1;
        zt_registry[zt_reg_n++] = t;
    } else {
        ok = 0;
    }
    pthread_mutex_unlock(&zt_reg_mu);
    if (!ok) { free(t->name); free(t); return 0; }
    return (int64_t)(intptr_t)t;
}

/* 匿名表登记：OsHandle/Attach 的同进程承载物。句柄=表指针，登记表
 * 只用来区分"本进程创建的匿名表"（可往返）与具名表/野指针（不可）。 */
static zt_table *zt_anon_reg[256];
static int zt_anon_n = 0;
static pthread_mutex_t zt_anon_mu = PTHREAD_MUTEX_INITIALIZER;

static zt_table *zt_anon_lookup(int64_t handle) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t) return NULL;
    pthread_mutex_lock(&zt_anon_mu);
    for (int i = 0; i < zt_anon_n; i++) {
        if (zt_anon_reg[i] == t) {
            pthread_mutex_unlock(&zt_anon_mu);
            return t;
        }
    }
    pthread_mutex_unlock(&zt_anon_mu);
    return NULL;
}

int64_t zan_shared_table_create_anon(
    int32_t capacity, int32_t key_size, const char *schema) {
    if (capacity <= 0 || capacity > ZT_MAX_CAPACITY ||
        key_size <= 0 || key_size > 4096)
        return 0;
    zt_table *t = zt_new_table(NULL, capacity, key_size);
    if (!t) return 0;
    if (!zt_parse_schema(schema, t)) { free(t); return 0; }
    /* 入匿名登记表：web_shared_scope 的 master 把自己的匿名表交给
     * 同进程的 "worker" 管理器（RateLimiter.Attach/LockManager.Attach），
     * OsHandle/Attach 必须在同一进程内往返同一张表。句柄值就是表指针
     * 本身（非零），attach 校验登记表成员后再放行。 */
    pthread_mutex_lock(&zt_anon_mu);
    if (zt_anon_n < (int)(sizeof(zt_anon_reg) / sizeof(zt_anon_reg[0]))) {
        zt_anon_reg[zt_anon_n++] = t;
    }
    pthread_mutex_unlock(&zt_anon_mu);
    return (int64_t)(intptr_t)t;
}

int64_t zan_shared_table_open(const char *name) {
    if (!name) return 0;
    pthread_mutex_lock(&zt_reg_mu);
    zt_table *found = NULL;
    for (int i = 0; i < zt_reg_n; i++) {
        if (zt_registry[i]->linked && strcmp(zt_registry[i]->name, name) == 0) {
            found = zt_registry[i];
            break;
        }
    }
    pthread_mutex_unlock(&zt_reg_mu);
    return found ? (int64_t)(intptr_t)found : 0;
}

long zan_shared_table_handle(int64_t handle) {
    /* 具名表按文档返回 0（本就按名字打开）；匿名表返回可交给
     * Attach 的句柄（同进程内即表指针本身）。 */
    if (!zt_anon_lookup(handle)) return 0;
    return handle;
}

int64_t zan_shared_table_attach(int64_t os_handle) {
    /* 同进程附着：句柄是本进程创建的匿名表时返回它，attach 出来的
     * SharedTable 与建表方共享同一份行数据和互斥量。 */
    if (!zt_anon_lookup(os_handle)) return 0;
    return os_handle;
}

void zan_shared_table_close(int64_t handle) {
    /* 表有意不 free（防悬垂句柄），Close 无需做事。 */
    (void)handle;
}

int32_t zan_shared_table_destroy(int64_t handle) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t) return 0;
    int was_linked = 0;
    pthread_mutex_lock(&zt_reg_mu);
    if (t->linked) {
        t->linked = 0;
        was_linked = 1;
    }
    pthread_mutex_unlock(&zt_reg_mu);
    return was_linked;
}

/* 以下操作都要求已持有 t->mu。 */

static void zt_purge_locked(zt_table *t, int64_t now) {
    for (zt_row *r = t->rows; r; r = r->next) {
        if (r->alive && r->expires_at != 0 && r->expires_at <= now) {
            r->alive = 0;
            t->count--;
        }
    }
}

static zt_row *zt_find_row(zt_table *t, const char *key) {
    for (zt_row *r = t->rows; r; r = r->next)
        if (r->alive && strcmp(r->key, key) == 0) return r;
    return NULL;
}

static zt_row *zt_find_row_hash(zt_table *t, uint64_t hash) {
    for (zt_row *r = t->rows; r; r = r->next)
        if (r->alive && r->hash == hash) return r;
    return NULL;
}

/* oracle 载荷因子：7/8 满即拒绝建行；tombstone 槽位优先复用。 */
static zt_row *zt_row_for(zt_table *t, const char *key) {
    zt_row *r = zt_find_row(t, key);
    if (r) return r;
    if ((int64_t)t->count * 8 >= (int64_t)t->cap * 7) return NULL;
    for (r = t->rows; r; r = r->next)
        if (!r->alive) break;
    if (!r) {
        r = (zt_row *)calloc(1, sizeof(zt_row));
        if (!r) return NULL;
        r->key = strdup(key);
        r->data = calloc(1, (size_t)(t->stride > 0 ? t->stride : 1));
        if (!r->key || !r->data) {
            free(r->key);
            free(r->data);
            free(r);
            return NULL;
        }
        r->next = t->rows;
        t->rows = r;
    }
    r->hash = zt_hash(key);
    r->expires_at = 0;
    memset(r->data, 0, (size_t)t->stride);
    r->alive = 1;
    t->count++;
    return r;
}

static zt_col *zt_find_col(zt_table *t, const char *name, int type) {
    for (int i = 0; i < t->ncol; i++)
        if (t->cols[i].type == type && strcmp(t->cols[i].name, name) == 0)
            return &t->cols[i];
    return NULL;
}

int32_t zan_shared_table_set_int(
    int64_t handle, const char *key, const char *column, int64_t value) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t || !key) return 0;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_col *c = zt_find_col(t, column, 0);
    zt_row *r = c ? zt_row_for(t, key) : NULL;
    if (r) *(int64_t *)((char *)r->data + c->offset) = value;
    pthread_mutex_unlock(&t->mu);
    return r != NULL;
}

int64_t zan_shared_table_get_int(
    int64_t handle, const char *key, const char *column) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t || !key) return 0;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_col *c = zt_find_col(t, column, 0);
    zt_row *r = c ? zt_find_row(t, key) : NULL;
    int64_t v = r ? *(int64_t *)((char *)r->data + c->offset) : 0;
    pthread_mutex_unlock(&t->mu);
    return v;
}

int32_t zan_shared_table_set_float(
    int64_t handle, const char *key, const char *column, double value) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t || !key) return 0;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_col *c = zt_find_col(t, column, 1);
    zt_row *r = c ? zt_row_for(t, key) : NULL;
    if (r) *(double *)((char *)r->data + c->offset) = value;
    pthread_mutex_unlock(&t->mu);
    return r != NULL;
}

double zan_shared_table_get_float(
    int64_t handle, const char *key, const char *column) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t || !key) return 0.0;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_col *c = zt_find_col(t, column, 1);
    zt_row *r = c ? zt_find_row(t, key) : NULL;
    double v = r ? *(double *)((char *)r->data + c->offset) : 0.0;
    pthread_mutex_unlock(&t->mu);
    return v;
}

int32_t zan_shared_table_set_string(
    int64_t handle, const char *key, const char *column, const char *value) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t || !key || !value) return 0;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_col *c = zt_find_col(t, column, 2);
    zt_row *r = NULL;
    if (c && strlen(value) < (size_t)c->size) r = zt_row_for(t, key);
    if (r) {
        char *dst = (char *)r->data + c->offset;
        memcpy(dst, value, strlen(value));
        dst[strlen(value)] = '\0';
    }
    pthread_mutex_unlock(&t->mu);
    return r != NULL;
}

const char *zan_shared_table_get_string(
    int64_t handle, const char *key, const char *column) {
    char *result = (char *)malloc(1);
    if (result) result[0] = '\0'; /* 缺失 → 空串，同 oracle */
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!result || !t || !key) return result;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_col *c = zt_find_col(t, column, 2);
    zt_row *r = c ? zt_find_row(t, key) : NULL;
    if (r) {
        const char *src = (const char *)r->data + c->offset;
        size_t len = strnlen(src, (size_t)(c->size - 1));
        char *copy = (char *)malloc(len + 1);
        if (copy) {
            memcpy(copy, src, len);
            copy[len] = '\0';
            free(result);
            result = copy;
        }
    }
    pthread_mutex_unlock(&t->mu);
    return result;
}

int64_t zan_shared_table_increment(
    int64_t handle, const char *key, const char *column, int64_t delta) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t || !key) return 0;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_col *c = zt_find_col(t, column, 0);
    zt_row *r = c ? zt_row_for(t, key) : NULL;
    int64_t v = 0;
    if (r) {
        v = *(int64_t *)((char *)r->data + c->offset) + delta;
        *(int64_t *)((char *)r->data + c->offset) = v;
    }
    pthread_mutex_unlock(&t->mu);
    return v;
}

int32_t zan_shared_table_expire_at(
    int64_t handle, const char *key, int64_t expires_at_ms);

int32_t zan_shared_table_expire(
    int64_t handle, const char *key, int64_t ttl_ms) {
    if (ttl_ms < 0) return 0;
    int64_t now = zt_wall_now();
    if (ttl_ms > INT64_MAX - now) return 0;
    return zan_shared_table_expire_at(handle, key, now + ttl_ms);
}

int32_t zan_shared_table_expire_at(
    int64_t handle, const char *key, int64_t expires_at_ms) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t || !key || expires_at_ms < 0) return 0;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_row *r = zt_find_row(t, key);
    if (r) r->expires_at = expires_at_ms;
    pthread_mutex_unlock(&t->mu);
    return r != NULL;
}

int64_t zan_shared_table_expires_at(int64_t handle, const char *key) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t || !key) return -1;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_row *r = zt_find_row(t, key);
    int64_t at = r ? r->expires_at : -1;
    pthread_mutex_unlock(&t->mu);
    return at;
}

int64_t zan_shared_table_purge_expired(int64_t handle, int64_t now_ms) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t) return 0;
    if (now_ms < 0) now_ms = zt_wall_now();
    pthread_mutex_lock(&t->mu);
    int64_t before = t->count;
    zt_purge_locked(t, now_ms);
    int64_t removed = before - t->count;
    pthread_mutex_unlock(&t->mu);
    return removed;
}

int32_t zan_shared_table_rate_allow(
    int64_t handle, const char *key, int64_t now_ms,
    int64_t window_ms, int64_t limit) {
    if (limit <= 0) return 1;
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t || !key || window_ms <= 0) return 0;
    if (now_ms < 0) now_ms = zt_wall_now();
    if (window_ms > INT64_MAX - now_ms) return 0;
    pthread_mutex_lock(&t->mu);
    zt_col *count_col = zt_find_col(t, "count", 0);
    zt_col *start_col = zt_find_col(t, "window_start", 0);
    zt_row *r = NULL;
    int allowed = 0;
    if (count_col && start_col) {
        zt_purge_locked(t, now_ms);
        r = zt_find_row(t, key);
        if (!r) r = zt_row_for(t, key);
        if (r) {
            int64_t *count = (int64_t *)((char *)r->data + count_col->offset);
            int64_t *start = (int64_t *)((char *)r->data + start_col->offset);
            if (*start <= 0 || now_ms - *start >= window_ms) {
                *count = 1;
                *start = now_ms;
                r->expires_at = now_ms + window_ms;
                allowed = 1;
            } else if (*count < limit) {
                (*count)++;
                allowed = 1;
            }
        }
    }
    pthread_mutex_unlock(&t->mu);
    return allowed;
}

int32_t zan_shared_table_lock_acquire(
    int64_t handle, const char *key, int64_t owner,
    int64_t now_ms, int64_t lease_ms) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t || !key || owner == 0 || lease_ms <= 0) return 0;
    if (now_ms < 0) now_ms = zt_wall_now();
    if (lease_ms > INT64_MAX - now_ms) return 0;
    pthread_mutex_lock(&t->mu);
    zt_col *owner_col = zt_find_col(t, "owner", 0);
    int acquired = 0;
    if (owner_col) {
        zt_purge_locked(t, now_ms);
        zt_row *r = zt_find_row(t, key);
        if (!r) {
            r = zt_row_for(t, key); /* 仅 absent 建行，同 oracle */
            if (r) {
                *(int64_t *)((char *)r->data + owner_col->offset) = owner;
                r->expires_at = now_ms + lease_ms;
                acquired = 1;
            }
        }
    }
    pthread_mutex_unlock(&t->mu);
    return acquired;
}

int32_t zan_shared_table_lock_release(
    int64_t handle, const char *key, int64_t owner) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t || !key || owner == 0) return 0;
    pthread_mutex_lock(&t->mu);
    zt_col *owner_col = zt_find_col(t, "owner", 0);
    int released = 0;
    if (owner_col) {
        zt_purge_locked(t, zt_wall_now());
        zt_row *r = zt_find_row(t, key);
        if (r && *(int64_t *)((char *)r->data + owner_col->offset) == owner) {
            r->alive = 0;
            t->count--;
            released = 1;
        }
    }
    pthread_mutex_unlock(&t->mu);
    return released;
}

int32_t zan_shared_table_delete(int64_t handle, const char *key) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t || !key) return 0;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_row *r = zt_find_row(t, key);
    if (r) {
        r->alive = 0;
        t->count--;
    }
    pthread_mutex_unlock(&t->mu);
    return r != NULL;
}

int32_t zan_shared_table_exists(int64_t handle, const char *key) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t || !key) return 0;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_row *r = zt_find_row(t, key);
    pthread_mutex_unlock(&t->mu);
    return r != NULL;
}

int64_t zan_shared_table_count(int64_t handle) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t) return 0;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    int64_t n = t->count;
    pthread_mutex_unlock(&t->mu);
    return n;
}

void zan_shared_table_clear(int64_t handle) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t) return;
    pthread_mutex_lock(&t->mu);
    for (zt_row *r = t->rows; r; r = r->next) r->alive = 0;
    t->count = 0;
    pthread_mutex_unlock(&t->mu);
}

int64_t zan_shared_table_hash(const char *value) {
    return (int64_t)zt_hash(value);
}

long zan_shared_table_stat(int64_t handle, int32_t what) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t) return 0;
    switch (what) {
        case 0: /* RESERVED：oracle 返回映射字节数 */
        case 1: /* RESIDENT：全堆存储，全部驻留 */
            return (int64_t)t->stride * t->cap;
        case 2: return t->cap;      /* CAPACITY */
        case 3: return t->count;    /* COUNT */
        case 4: return t->stride;   /* ROW_STRIDE */
        case 5: return t->keysize;  /* KEY_SIZE */
        case 6: return t->ncol;     /* COLUMNS */
        default: return 0;
    }
}

int32_t zan_shared_table_set_int_at(
    int64_t handle, int64_t key_hash, const char *column, int64_t value) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t) return 0;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_col *c = zt_find_col(t, column, 0);
    zt_row *r = NULL;
    if (c) {
        r = zt_find_row_hash(t, (uint64_t)key_hash);
        if (!r) {
            /* 哈希寻径建行：键取哈希的十进制形式占位（该 API 本就无键） */
            char keybuf[32];
            snprintf(keybuf, sizeof(keybuf), "%lld", (long long)key_hash);
            r = zt_row_for(t, keybuf);
            if (r) r->hash = (uint64_t)key_hash;
        }
        if (r) *(int64_t *)((char *)r->data + c->offset) = value;
    }
    pthread_mutex_unlock(&t->mu);
    return r != NULL;
}

int64_t zan_shared_table_get_int_at(
    int64_t handle, int64_t key_hash, const char *column) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t) return 0;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_col *c = zt_find_col(t, column, 0);
    zt_row *r = c ? zt_find_row_hash(t, (uint64_t)key_hash) : NULL;
    int64_t v = r ? *(int64_t *)((char *)r->data + c->offset) : 0;
    pthread_mutex_unlock(&t->mu);
    return v;
}

int64_t zan_shared_table_increment_at(
    int64_t handle, int64_t key_hash, const char *column, int64_t delta) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t) return 0;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_col *c = zt_find_col(t, column, 0);
    zt_row *r = NULL;
    int64_t v = 0;
    if (c) {
        r = zt_find_row_hash(t, (uint64_t)key_hash);
        if (!r) {
            char keybuf[32];
            snprintf(keybuf, sizeof(keybuf), "%lld", (long long)key_hash);
            r = zt_row_for(t, keybuf);
            if (r) r->hash = (uint64_t)key_hash;
        }
        if (r) {
            v = *(int64_t *)((char *)r->data + c->offset) + delta;
            *(int64_t *)((char *)r->data + c->offset) = v;
        }
    }
    pthread_mutex_unlock(&t->mu);
    return v;
}

int64_t zan_shared_table_extreme_at(
    int64_t handle, int64_t key_hash, const char *column, int64_t value,
    int64_t keep_larger) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t) return 0;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_col *c = zt_find_col(t, column, 0);
    zt_row *r = NULL;
    int64_t stored = 0;
    if (c) {
        r = zt_find_row_hash(t, (uint64_t)key_hash);
        if (!r) {
            char keybuf[32];
            snprintf(keybuf, sizeof(keybuf), "%lld", (long long)key_hash);
            r = zt_row_for(t, keybuf);
            if (r) r->hash = (uint64_t)key_hash;
        }
        if (r) {
            int64_t *slot = (int64_t *)((char *)r->data + c->offset);
            stored = *slot;
            int replace = keep_larger ? (value > stored)
                                      : (stored == 0 || value < stored);
            if (replace) {
                *slot = value;
                stored = value;
            }
        }
    }
    pthread_mutex_unlock(&t->mu);
    return stored;
}

int32_t zan_shared_table_set_string_at(
    int64_t handle, int64_t key_hash, const char *column, const char *value) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t || !value) return 0;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_col *c = zt_find_col(t, column, 2);
    zt_row *r = NULL;
    if (c && strlen(value) < (size_t)c->size) {
        r = zt_find_row_hash(t, (uint64_t)key_hash);
        if (!r) {
            char keybuf[32];
            snprintf(keybuf, sizeof(keybuf), "%lld", (long long)key_hash);
            r = zt_row_for(t, keybuf);
            if (r) r->hash = (uint64_t)key_hash;
        }
        if (r) {
            char *dst = (char *)r->data + c->offset;
            memcpy(dst, value, strlen(value));
            dst[strlen(value)] = '\0';
        }
    }
    pthread_mutex_unlock(&t->mu);
    return r != NULL;
}

const char *zan_shared_table_get_string_at(
    int64_t handle, int64_t key_hash, const char *column) {
    char *result = (char *)malloc(1);
    if (result) result[0] = '\0';
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!result || !t) return result;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_col *c = zt_find_col(t, column, 2);
    zt_row *r = c ? zt_find_row_hash(t, (uint64_t)key_hash) : NULL;
    if (r) {
        const char *src = (const char *)r->data + c->offset;
        size_t len = strnlen(src, (size_t)(c->size - 1));
        char *copy = (char *)malloc(len + 1);
        if (copy) {
            memcpy(copy, src, len);
            copy[len] = '\0';
            free(result);
            result = copy;
        }
    }
    pthread_mutex_unlock(&t->mu);
    return result;
}

int32_t zan_shared_table_match_at(
    int64_t handle, int64_t key_hash, const char *column, const char *text) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t || !text) return 0;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_col *c = zt_find_col(t, column, 2);
    zt_row *r = c ? zt_find_row_hash(t, (uint64_t)key_hash) : NULL;
    int equal = 0;
    if (r)
        equal = strncmp(
            (const char *)r->data + c->offset, text, (size_t)c->size) == 0;
    pthread_mutex_unlock(&t->mu);
    return equal;
}

int32_t zan_shared_table_exists_at(int64_t handle, int64_t key_hash) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t) return 0;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_row *r = zt_find_row_hash(t, (uint64_t)key_hash);
    pthread_mutex_unlock(&t->mu);
    return r != NULL;
}

int32_t zan_shared_table_delete_at(int64_t handle, int64_t key_hash) {
    zt_table *t = (zt_table *)(intptr_t)handle;
    if (!t) return 0;
    pthread_mutex_lock(&t->mu);
    zt_purge_locked(t, zt_wall_now());
    zt_row *r = zt_find_row_hash(t, (uint64_t)key_hash);
    if (r) {
        r->alive = 0;
        t->count--;
    }
    pthread_mutex_unlock(&t->mu);
    return r != NULL;
}

#endif /* ZAN_RT_CORE_ZAN */

/* ---- sockets / resolver (spec: oracle src/runtime/rt_io.c zan_io_*) ----
 * stdlib System/Net pulls this family through DllImport EntryPoints; the
 * awaited forms (zan_io_resolve_all_async / zan_io_connect_sa) run inline on
 * the coroutine thread on this lane (no blocking-worker pool), which is the
 * documented recv/accept deviation. POSIX/darwin branches only.
 * 第五批起 zan_io_* 纯函数族由 runtime_core.zan 提供,这里 -D 编出;
 * 下面的 native io reactor 段(watch 表 + close_notify + poll)仍在 C,
 * 仍需这些头。 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if.h>
#if defined(__APPLE__)
#include <net/if_dl.h>
#endif
#include <ifaddrs.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#ifndef ZAN_RT_CORE_ZAN
#define ZAN_IO_SA_STRIDE 32

/* The oracle's zan_io_init installs this once: a peer hanging up mid-send
 * must be a return value, not a process killer (rt_io.c:68-86). */
static void zt_io_ignore_sigpipe(void) {
    struct sigaction sa;
    struct sigaction cur;
    if (sigaction(SIGPIPE, NULL, &cur) == 0 && cur.sa_handler != SIG_DFL) {
        return;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPIPE, &sa, NULL);
}

void zan_io_socket_cleanup(void) {}

/* rt_io.c:444: >=0 bytes accepted; -1 would block; -2 fatal. */
int64_t zan_io_socket_send(intptr_t fd, const void *buf, int64_t len,
                           int32_t flags) {
    zt_io_ignore_sigpipe();
    if (len <= 0) return len == 0 ? 0 : -2;
    if (len > 0x7FFFFFFF) len = 0x7FFFFFFF;
    for (;;) {
        ssize_t r = send((int)fd, buf, (size_t)len, (int)flags);
        if (r >= 0) return (int64_t)r;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
        if (errno == EINTR) continue;
        return -2;
    }
}

int64_t zan_io_socket_recv(intptr_t fd, void *buf, int64_t len,
                           int32_t flags) {
    if (len < 0) return -2;
    if (len > 0x7FFFFFFF) len = 0x7FFFFFFF;
    return (int64_t)recv((int)fd, buf, (size_t)len, (int)flags);
}

/* rt_io.c:509: thread-local so an adopted text survives the next call. */
const char *zan_io_sockaddr_ip_str(const void *sa) {
    static __thread char address[128];
    const struct sockaddr *a = (const struct sockaddr *)sa;
    if (a->sa_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)a;
        if (!inet_ntop(AF_INET, &in->sin_addr, address, sizeof(address)))
            return "";
        return address;
    }
    if (a->sa_family == AF_INET6) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)a;
        if (!inet_ntop(AF_INET6, &in6->sin6_addr, address, sizeof(address)))
            return "";
        return address;
    }
    return "";
}

const char *zan_io_socket_peer_ip(intptr_t fd) {
    struct sockaddr_storage peer;
    socklen_t length = (socklen_t)sizeof(peer);
    if (getpeername((int)fd, (struct sockaddr *)&peer, &length) != 0) return "";
    return zan_io_sockaddr_ip_str(&peer);
}

/* rt_io.c:552: copy-out variants; length excluding NUL, or -1. */
static int32_t zan_io_ip_copy_out(const char *text, char *buf, int32_t cap) {
    if (!buf || cap <= 0) return -1;
    size_t n = strlen(text);
    if ((size_t)cap <= n) return -1;
    memcpy(buf, text, n + 1);
    return (int32_t)n;
}

int32_t zan_io_socket_peer_ip_into(intptr_t fd, char *buf, int32_t cap) {
    struct sockaddr_storage peer;
    socklen_t length = (socklen_t)sizeof(peer);
    if (getpeername((int)fd, (struct sockaddr *)&peer, &length) != 0)
        return -1;
    return zan_io_ip_copy_out(zan_io_sockaddr_ip_str(&peer), buf, cap);
}

int32_t zan_io_sockaddr_ip_str_into(const void *sa, char *buf, int32_t cap) {
    return zan_io_ip_copy_out(zan_io_sockaddr_ip_str(sa), buf, cap);
}

/* rt_io.c:573: literal ':' names are v6-or-nothing, everything else prefers
 * v4 with an AF_UNSPEC fallback for v6-only hostnames. Length 16/28 or 0. */
int32_t zan_io_resolve_sa(const char *name, int32_t port, void *buf,
                          int32_t cap) {
    if (!name || !*name) return 0;
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = strchr(name, ':') ? AF_UNSPEC : AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    if (getaddrinfo(name, portstr, &hints, &res) != 0 || !res) {
        if (hints.ai_family == AF_INET) {
            hints.ai_family = AF_UNSPEC;
            if (getaddrinfo(name, portstr, &hints, &res) != 0 || !res)
                return 0;
        } else {
            return 0;
        }
    }
    int len = (res->ai_family == AF_INET6) ? (int)sizeof(struct sockaddr_in6)
             : (res->ai_family == AF_INET) ? (int)sizeof(struct sockaddr_in)
             : 0;
    int32_t r = 0;
    if (len && len <= cap && res->ai_addrlen == (size_t)len) {
        memcpy(buf, res->ai_addr, (size_t)len);
        r = len;
    }
    freeaddrinfo(res);
    return r;
}

/* rt_io.c:607: IPv4 address in inet_addr byte order, 0 on failure. */
int32_t zan_io_resolve_ipv4(const char *hostname) {
    if (!hostname || !*hostname) return 0;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    if (getaddrinfo(hostname, NULL, &hints, &res) != 0 || !res) return 0;
    struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
    int32_t addr = 0;
    if (res->ai_family == AF_INET && sin->sin_family == AF_INET)
        addr = (int32_t)sin->sin_addr.s_addr;
    freeaddrinfo(res);
    return addr;
}

int32_t zan_io_sockaddr_family(const void *sa, int32_t len) {
    if (!sa || len < 2) return 0;
    const struct sockaddr *a = (const struct sockaddr *)sa;
    if (a->sa_family == AF_INET && len >= (int32_t)sizeof(struct sockaddr_in))
        return AF_INET;
    if (a->sa_family == AF_INET6 && len >= (int32_t)sizeof(struct sockaddr_in6))
        return AF_INET6;
    return 0;
}

/* rt_io.c:638: 1 only for a usable public address (or an explicit loopback
 * exception); embedded IPv4 forms are judged by the v4 rules. */
int32_t zan_io_sockaddr_is_safe(const void *sa, int32_t len,
                               int32_t allow_loopback) {
    int family = zan_io_sockaddr_family(sa, len);
    if (family == AF_INET) {
        const struct sockaddr_in *v4 = (const struct sockaddr_in *)sa;
        uint32_t a = ntohl(v4->sin_addr.s_addr);
        uint32_t first = a >> 24;
        uint32_t second = (a >> 16) & 255u;
        if (first == 127u) return allow_loopback ? 1 : 0;
        if (first == 0u || first == 10u || first >= 224u || first >= 240u)
            return 0;
        if (first == 169u && second == 254u) return 0;
        if (first == 172u && second >= 16u && second <= 31u) return 0;
        if (first == 192u && second == 168u) return 0;
        if (first == 192u && second == 0u) return 0;
        uint32_t third = (a >> 8) & 255u;
        if (first == 192u && second == 2u) return 0;
        if (first == 192u && second == 88u && third == 99u) return 0;
        if (first == 198u && second >= 18u && second <= 19u) return 0;
        if (first == 198u && second == 51u && third == 100u) return 0;
        if (first == 203u && second == 0u && third == 113u) return 0;
        if (first == 100u && second >= 64u && second <= 127u) return 0;
        if (a == 0xffffffffu) return 0;
        return 1;
    }
    if (family == AF_INET6) {
        const struct sockaddr_in6 *v6 = (const struct sockaddr_in6 *)sa;
        const unsigned char *b = (const unsigned char *)&v6->sin6_addr;
        int all_zero = 1;
        for (int i = 0; i < 16; i++) if (b[i] != 0) all_zero = 0;
        if (all_zero) return 0;
        int loop = 1;
        for (int i = 0; i < 15; i++) if (b[i] != 0) loop = 0;
        if (loop && b[15] == 1) return allow_loopback ? 1 : 0;
        if ((b[0] & 0xfeu) == 0xfcu) return 0;
        if ((b[0] & 0xfeu) == 0xfeu && (b[1] & 0xc0u) == 0x80u) return 0;
        if (b[0] == 0xffu) return 0;
        if (b[0] == 0x20u && b[1] == 0x01u && b[2] == 0x0du && b[3] == 0xb8u)
            return 0;
        int mapped = 1;
        for (int i = 0; i < 10; i++) if (b[i] != 0) mapped = 0;
        if (mapped && b[10] == 0xffu && b[11] == 0xffu) {
            struct sockaddr_in mapped4;
            memset(&mapped4, 0, sizeof(mapped4));
            mapped4.sin_family = AF_INET;
            memcpy(&mapped4.sin_addr, b + 12, 4);
            return zan_io_sockaddr_is_safe(&mapped4,
                (int32_t)sizeof(mapped4), allow_loopback);
        }
        if (b[0] == 0x20u && b[1] == 0x01u && b[2] == 0x00u && b[3] == 0x00u) {
            struct sockaddr_in t4;
            unsigned char cli[4];
            memset(&t4, 0, sizeof(t4));
            t4.sin_family = AF_INET;
            memcpy(&t4.sin_addr, b + 4, 4);
            if (!zan_io_sockaddr_is_safe(&t4, (int32_t)sizeof(t4), allow_loopback))
                return 0;
            for (int i = 0; i < 4; i++) cli[i] = (unsigned char)(b[12 + i] ^ 0xffu);
            memcpy(&t4.sin_addr, cli, 4);
            return zan_io_sockaddr_is_safe(&t4, (int32_t)sizeof(t4), allow_loopback);
        }
        int v4_off = -1;
        if (b[0] == 0x20u && b[1] == 0x02u)
            v4_off = 2;
        else if (b[0] == 0x00u && b[1] == 0x64u && b[2] == 0xffu && b[3] == 0x9bu)
            v4_off = 12;
        else if (mapped)
            v4_off = 12;
        if (v4_off >= 0) {
            struct sockaddr_in e4;
            memset(&e4, 0, sizeof(e4));
            e4.sin_family = AF_INET;
            memcpy(&e4.sin_addr, b + v4_off, 4);
            return zan_io_sockaddr_is_safe(&e4, (int32_t)sizeof(e4), allow_loopback);
        }
        return 1;
    }
    return 0;
}

/* rt_io.c:727: stable de-dup into caller slots; never a partial set --
 * overflow clears the buffer and returns 0. */
int32_t zan_io_resolve_all(const char *name, int32_t port, void *buf,
                           int32_t cap) {
    if (!buf || cap <= 0) return 0;
    if (!name || !*name || cap < ZAN_IO_SA_STRIDE) {
        memset(buf, 0, (size_t)cap);
        return 0;
    }
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = NULL;
    if (getaddrinfo(name, portstr, &hints, &res) != 0 || !res) {
        memset(buf, 0, (size_t)cap);
        return 0;
    }
    int32_t capacity = cap / ZAN_IO_SA_STRIDE;
    int32_t count = 0;
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        int32_t family = zan_io_sockaddr_family(p->ai_addr,
            (int32_t)p->ai_addrlen);
        int32_t len = family == AF_INET6 ? (int32_t)sizeof(struct sockaddr_in6)
                     : family == AF_INET ? (int32_t)sizeof(struct sockaddr_in) : 0;
        if (!len || p->ai_addrlen < (size_t)len) continue;
        int duplicate = 0;
        for (int32_t i = 0; i < count; i++) {
            unsigned char *old = (unsigned char *)buf + i * ZAN_IO_SA_STRIDE;
            int oldfam = zan_io_sockaddr_family(old, len);
            if (oldfam == family && memcmp(old, p->ai_addr, (size_t)len) == 0) {
                duplicate = 1; break;
            }
        }
        if (duplicate) continue;
        if (count >= capacity) {
            memset(buf, 0, (size_t)cap);
            freeaddrinfo(res);
            return 0;
        }
        unsigned char *dst = (unsigned char *)buf + count * ZAN_IO_SA_STRIDE;
        memset(dst, 0, ZAN_IO_SA_STRIDE);
        memcpy(dst, p->ai_addr, (size_t)len);
        count++;
    }
    freeaddrinfo(res);
    return count;
}

int64_t zan_io_resolve_all_async(intptr_t name_ptr, int32_t port,
                                 intptr_t buf_ptr, int32_t cap) {
    return (int64_t)zan_io_resolve_all((const char *)name_ptr, port,
                                       (void *)buf_ptr, cap);
}

static int32_t zan_io_set_nonblocking(intptr_t fd) {
    int flags = fcntl((int)fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl((int)fd, F_SETFL, flags | O_NONBLOCK);
}

/* rt_io.c:881: nonblocking connect; 0 established, -2 in progress, else a
 * positive errno code or -1. */
static int32_t zan_io_connect_sa_start(intptr_t fd, const void *sa,
                                       int32_t salen) {
    if (!sa || zan_io_sockaddr_family(sa, salen) == 0) return -1;
    if (zan_io_set_nonblocking(fd) != 0) return -1;
    int r = connect((int)fd, (const struct sockaddr *)sa, (socklen_t)salen);
    if (r == 0) return 0;
    if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EALREADY)
        return -2;
    return errno > 0 ? errno : -1;
}

/* rt_io.c:903: the blocking-worker exact connect; on this lane it runs
 * inline. 0 established, positive SO_ERROR, -3 timed out, -1 failed. */
int64_t zan_io_connect_sa(intptr_t fd, const void *sa, int32_t salen,
                           int32_t timeout_ms) {
    zt_io_ignore_sigpipe();
    int32_t r = zan_io_connect_sa_start(fd, sa, salen);
    if (r == 0 || r != -2) return r;
    fd_set wfds, efds;
    FD_ZERO(&wfds); FD_ZERO(&efds);
    if (fd < 0 || fd >= FD_SETSIZE) return -1;
    FD_SET((int)fd, &wfds); FD_SET((int)fd, &efds);
    struct timeval tv, *ptv = NULL;
    if (timeout_ms > 0) { tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000; ptv = &tv; }
    int sr;
    do { sr = select((int)fd + 1, NULL, &wfds, &efds, ptv); }
    while (sr < 0 && errno == EINTR);
    if (sr <= 0) return sr == 0 ? -3 : -1;
    int err = 0;
    socklen_t len = (socklen_t)sizeof(err);
    if (getsockopt((int)fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0)
        return -1;
    return err == 0 ? 0 : err;
}

/* rt_io.c:853: 0 established, positive SO_ERROR (or -1), -2 in progress. */
int32_t zan_io_connect_status(intptr_t fd) {
    fd_set wfds, efds;
    FD_ZERO(&wfds);
    FD_ZERO(&efds);
    struct timeval timeout = {0, 0};
    int err = 0;
    socklen_t elen = (socklen_t)sizeof(err);
    if (fd < 0 || fd >= FD_SETSIZE) return -1;
    int sock = (int)fd;
    FD_SET(sock, &wfds);
    FD_SET(sock, &efds);
    if (select(sock + 1, NULL, &wfds, &efds, &timeout) < 0) return -1;
    if (FD_ISSET(sock, &efds)) {
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&err, &elen) != 0)
            return -1;
        return err != 0 ? (int32_t)err : -1;
    }
    if (FD_ISSET(sock, &wfds)) {
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&err, &elen) != 0)
            return -1;
        return (int32_t)err;
    }
    return -2;
}

/* rt_io.c:833: immediate readiness probe via select. */
int32_t zan_io_socket_ready(intptr_t fd, int32_t write_ready) {
    fd_set fds;
    FD_ZERO(&fds);
    struct timeval timeout = {0, 0};
    if (fd < 0 || fd >= FD_SETSIZE) return -1;
    int sock = (int)fd;
    FD_SET(sock, &fds);
    return (int32_t)select(sock + 1, write_ready ? NULL : &fds,
                           write_ready ? &fds : NULL, NULL, &timeout);
}

/* rt_io.c:869: SO_TYPE getsockopt (Windows) / F_GETFD (POSIX). */
int32_t zan_io_socket_alive(intptr_t fd) {
    if (fd < 0) return 0;
    return fcntl((int)fd, F_GETFD) != -1;
}
#endif /* ZAN_RT_CORE_ZAN */

/* ---- native io reactor (spec: oracle rt_co.c/rt_io.c reactor loop) ----
 * The five awaited io helpers (wait_co/recv_co/recv_to_co/accept_co/
 * resolve_sa_co) used to re-ready their frame immediately around a one-shot
 * syscall: readiness races lost data (RecvToOv's in-flight one-shot),
 * accepts parked under a closed listener hung, and hostname resolution was
 * a documented gap. This is the honest shape: watchers park in a table the
 * emitted scheduler's pump polls (zan_io_poll), close_notify wakes and
 * retires dead fds, and hostnames resolve on a detached worker signalled
 * through a self-pipe. Single-threaded coroutine engine: every watch-table
 * mutation happens on the main thread; the DNS worker only fills its own
 * job and hands it over under a mutex via the pipe. zan_co_ready is
 * emitted by the compiler into async programs; non-async links leave it
 * unresolved (weak), where these functions are unreachable anyway.
 * Batch 9: the reactor API (watch helpers, the five co entry points,
 * io_poll, close_notify, both resolve_co forms) is ported to Zan
 * (runtime_core.zan). Batch 10 (缺口五): language-level indirect calls
 * retire the remaining thunks -- the Zan reactor resumes through its own
 * hook static (zan.set_ready_hook/io_resume), spawns DNS workers through
 * the [ThreadEntry] trampoline, and drains under its own mutex; what
 * stays here (C baseline lane only) is the hook global + setter shim for
 * the C reactor and the C DNS worker/pipe. */
/* -DZAN_RT_CORE_ZAN 时下面三个定义已编出(runtime_core.zan 提供),
 * reactor 段/DNS worker 仍引用它们——前向声明。 */
extern int32_t zan_io_socket_alive(intptr_t fd);
extern int32_t zan_io_resolve_ipv4(const char *hostname);
extern int32_t zan_io_resolve_sa(const char *name, int32_t port, void *buf,
                                 int32_t cap);

#include <poll.h>

/* The emitted async runtime registers its _zan_co_ready through this shim
 * (the first co_ready call passes its own address). C-baseline lane only:
 * the Zan lane's runtime_core.zan owns the hook static and exports
 * _zan_set_ready_hook itself. The plain data global stays exported for
 * compilers emitted before the setter contract (they store the address
 * directly); current emission only calls the shim. */
#ifndef ZAN_RT_CORE_ZAN
void (*zan_co_ready_hook)(long long frame, long long step);

void zan_set_ready_hook(void *fn) {
    zan_co_ready_hook = (void (*)(long long, long long))fn;
}
#endif /* ZAN_RT_CORE_ZAN */

#ifndef ZAN_RT_CORE_ZAN
/* watcher kinds: 1/2 are ReadReady/WriteReady's interest codes, 3-5 the
 * value-yielding forms, 6 the persistent self-pipe watch */
#define ZT_IO_W 1
#define ZT_IO_WOUT 2
#define ZT_IO_RECV 3
#define ZT_IO_RECV_TO 4
#define ZT_IO_ACCEPT 5
#define ZT_IO_DNS 6

typedef struct ZtIOWatch {
    int fd;
    int kind;
    int active;
    char *buf;               /* recv forms */
    long long len;
    long long deadline_ns;   /* recv_to: 0 = none */
    long long frame, step;
    long long *outn;         /* recv/accept result slot (frame+32) */
} ZtIOWatch;

static ZtIOWatch *zt_io_ws;
static int zt_io_n, zt_io_cap;

static void zt_io_push(int fd, int kind, char *buf, long long len,
                       long long deadline_ns, long long frame, long long step,
                       long long *outn) {
    if (zt_io_n == zt_io_cap) {
        zt_io_cap = zt_io_cap ? zt_io_cap * 2 : 8;
        zt_io_ws = realloc(zt_io_ws, (size_t)zt_io_cap * sizeof(*zt_io_ws));
        if (!zt_io_ws) abort();
    }
    ZtIOWatch *w = &zt_io_ws[zt_io_n++];
    w->fd = fd; w->kind = kind; w->active = 1;
    w->buf = buf; w->len = len; w->deadline_ns = deadline_ns;
    w->frame = frame; w->step = step; w->outn = outn;
}

/* retire a watch and resume its frame; the result lands in the slot the
 * resume-k reload reads (frame+32, handed to us as outn) */
static void zt_io_deliver(ZtIOWatch *w, long long result) {
    w->active = 0;
    if (w->outn) *w->outn = result;
    if (zan_co_ready_hook) zan_co_ready_hook(w->frame, w->step);
}

/* one nonblocking recv: >=0 data/EOF, -2 would-block (park again),
 * -1 any other error (deliver; callers map negative to the closed shape) */
static long long zt_recv_try(int fd, char *buf, long long len) {
    if (len <= 0) return 0;
    for (;;) {
        ssize_t n = recv(fd, buf, (size_t)len, 0);
        if (n >= 0) return (long long)n;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return -2;
        return -1;
    }
}

/* zero-timeout readiness probe. EVERY co helper checks this before its
 * syscall: accepted sockets are often left in blocking mode by callers,
 * and a blocking recv/accept issued with nothing queued would stall the
 * whole single-threaded engine instead of parking this coroutine only. */
static int zt_fd_ready(int fd, short want) {
    struct pollfd p;
    p.fd = fd;
    p.events = want;
    p.revents = 0;
    return poll(&p, 1, 0) > 0;
}

void zan_io_wait_co(intptr_t fd, long long interest, long long frame,
                    long long step) {
    /* dead fd: fail fast (oracle io_reject_dead_fd). poll ignores negative
     * descriptors and a closed fd never fires, so parking would strand the
     * coroutine forever; readiness probes deliver 0 and resume at once. */
    if (zan_io_socket_alive(fd) == 0) {
        if (zan_co_ready_hook) zan_co_ready_hook(frame, step);
        return;
    }
    zt_io_push((int)fd, interest == 2 ? ZT_IO_WOUT : ZT_IO_W,
               NULL, 0, 0, frame, step, NULL);
}

void zan_io_recv_co(intptr_t fd, char *buf, long long len, long long frame,
                    long long step, long long *outn) {
    long long n = -2;
    if (zan_io_socket_alive(fd) == 0)
        n = 0;                           /* dead fd: EOF-shaped delivery */
    else if (zt_fd_ready((int)fd, POLLRDNORM))
        n = zt_recv_try((int)fd, buf, len);
    if (n == -2) {
        zt_io_push((int)fd, ZT_IO_RECV, buf, len, 0, frame, step, outn);
        return;
    }
    *outn = n;
    if (zan_co_ready_hook) zan_co_ready_hook(frame, step);
}

void zan_io_recv_to_co(intptr_t fd, char *buf, long long len, long long tmo,
                       long long frame, long long step, long long *outn) {
    long long n = -2;
    if (zan_io_socket_alive(fd) == 0)
        n = 0;                           /* dead fd: 0, not the timeout -1 */
    else if (zt_fd_ready((int)fd, POLLRDNORM))
        n = zt_recv_try((int)fd, buf, len);
    if (n == -2) {
        long long dl = tmo > 0 ? zan_monotonic_ns() + tmo * 1000000LL : 0;
        zt_io_push((int)fd, ZT_IO_RECV_TO, buf, len, dl, frame, step, outn);
        return;
    }
    *outn = n;
    if (zan_co_ready_hook) zan_co_ready_hook(frame, step);
}

void zan_io_accept_co(intptr_t fd, long long frame, long long step,
                      long long *outfd) {
    int c = -2;
    if (zan_io_socket_alive(fd) == 0)
        c = -1;                          /* dead fd: accept failure shape */
    else if (zt_fd_ready((int)fd, POLLRDNORM)) {
        for (;;) {
            c = accept((int)fd, NULL, NULL);
            if (c >= 0 || errno != EINTR) break;
        }
        if (c < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) c = -2;
    }
    if (c == -2) {
        zt_io_push((int)fd, ZT_IO_ACCEPT, NULL, 0, 0, frame, step, outfd);
        return;
    }
    *outfd = c;
    if (zan_co_ready_hook) zan_co_ready_hook(frame, step);
}

#endif /* ZAN_RT_CORE_ZAN */

/* hostname resolution: zan_io_resolve_sa is synchronous (getaddrinfo), so
 * it runs on a detached worker that hands the finished job over under a
 * mutex and signals the self-pipe; the pipe watch drains on the main
 * thread, so the ready queue is only ever touched by the main thread.
 * C-baseline lane only: the Zan lane's runtime_core.zan owns its own
 * pipe/mutex/worker (dns_pipe_ensure/dns_worker_job/thread_trampoline). */
#ifndef ZAN_RT_CORE_ZAN
typedef struct ZtDnsJob {
    struct ZtDnsJob *next;
    char *name;
    int32_t port;
    char *buf;
    int32_t cap;
    long long *outn;
    long long frame, step;
    int32_t result;
    int32_t v4;                          /* plain ipv4 lookup, no sockaddr */
} ZtDnsJob;

static int zt_dns_pipe[2] = {-1, -1};
static pthread_mutex_t zt_dns_mu = PTHREAD_MUTEX_INITIALIZER;
static ZtDnsJob *zt_dns_done;

/* create the self-pipe once. The read end must be non-blocking: the poll
 * side drains "until it would block", and a blocking read there would
 * freeze the reactor right after the last byte instead of returning. */
static int zt_dns_pipe_ensure(void) {
    if (zt_dns_pipe[0] >= 0) return 1;
    if (pipe(zt_dns_pipe) != 0) return 0;
    int fl = fcntl(zt_dns_pipe[0], F_GETFL, 0);
    if (fl >= 0) fcntl(zt_dns_pipe[0], F_SETFL, fl | O_NONBLOCK);
    return 1;
}

static void *zt_dns_worker(void *arg) {
    ZtDnsJob *j = (ZtDnsJob *)arg;
    if (j->v4)
        j->result = zan_io_resolve_ipv4(j->name);
    else
        j->result = zan_io_resolve_sa(j->name, j->port, j->buf, j->cap);
    pthread_mutex_lock(&zt_dns_mu);
    j->next = zt_dns_done;
    zt_dns_done = j;
    pthread_mutex_unlock(&zt_dns_mu);
    if (write(zt_dns_pipe[1], "x", 1) < 0) { /* main thread gone: nothing to wake */ }
    return NULL;
}
#endif /* ZAN_RT_CORE_ZAN */

#ifndef ZAN_RT_CORE_ZAN
void zan_resolve_sa_co(const char *name, long long port, char *buf,
                       long long cap, long long frame, long long step,
                       long long *outn) {
    if (!name || !*name) {
        *outn = 0;                       /* no worker for the empty name */
        if (zan_co_ready_hook) zan_co_ready_hook(frame, step);
        return;
    }
    struct in_addr a4;
    struct in6_addr a6;
    if (inet_pton(AF_INET, name, &a4) == 1 || inet_pton(AF_INET6, name, &a6) == 1) {
        /* literal: no DNS, no worker */
        *outn = zan_io_resolve_sa(name, (int32_t)port, buf, (int32_t)cap);
        if (zan_co_ready_hook) zan_co_ready_hook(frame, step);
        return;
    }
    if (!zt_dns_pipe_ensure()) {
        *outn = 0;
        if (zan_co_ready_hook) zan_co_ready_hook(frame, step);
        return;
    }
    ZtDnsJob *j = calloc(1, sizeof(*j));
    if (j) j->name = strdup(name);
    if (!j || !j->name) {
        free(j);
        *outn = 0;
        if (zan_co_ready_hook) zan_co_ready_hook(frame, step);
        return;
    }
    j->port = (int32_t)port; j->buf = buf; j->cap = (int32_t)cap;
    j->outn = outn; j->frame = frame; j->step = step;
    int havepipe = 0;
    for (int i = 0; i < zt_io_n; i++)
        if (zt_io_ws[i].active && zt_io_ws[i].kind == ZT_IO_DNS) { havepipe = 1; break; }
    if (!havepipe)
        zt_io_push(zt_dns_pipe[0], ZT_IO_DNS, NULL, 0, 0, 0, 0, NULL);
    pthread_t t;
    if (pthread_create(&t, NULL, zt_dns_worker, j) != 0) {
        /* no thread: degrade to an inline (blocking) lookup */
        *outn = zan_io_resolve_sa(j->name, j->port, j->buf, j->cap);
        free(j->name);
        free(j);
        if (zan_co_ready_hook) zan_co_ready_hook(frame, step);
        return;
    }
    pthread_detach(t);
}

/* plain ipv4 lookup (Socket.ResolveAsync): identical shape to the sockaddr
 * variant, but the worker delivers zan_io_resolve_ipv4's int, and getaddrinfo
 * resolves literal dotted quads itself, so no literal fast path is needed. */
void zan_resolve_ipv4_co(const char *name, long long frame, long long step,
                         long long *outn) {
    if (!name || !*name) {
        *outn = 0;                       /* no worker for the empty name */
        if (zan_co_ready_hook) zan_co_ready_hook(frame, step);
        return;
    }
    if (!zt_dns_pipe_ensure()) {
        *outn = 0;
        if (zan_co_ready_hook) zan_co_ready_hook(frame, step);
        return;
    }
    ZtDnsJob *j = calloc(1, sizeof(*j));
    if (j) j->name = strdup(name);
    if (!j || !j->name) {
        free(j);
        *outn = 0;
        if (zan_co_ready_hook) zan_co_ready_hook(frame, step);
        return;
    }
    j->v4 = 1;
    j->outn = outn; j->frame = frame; j->step = step;
    int havepipe = 0;
    for (int i = 0; i < zt_io_n; i++)
        if (zt_io_ws[i].active && zt_io_ws[i].kind == ZT_IO_DNS) { havepipe = 1; break; }
    if (!havepipe)
        zt_io_push(zt_dns_pipe[0], ZT_IO_DNS, NULL, 0, 0, 0, 0, NULL);
    pthread_t t;
    if (pthread_create(&t, NULL, zt_dns_worker, j) != 0) {
        /* no thread: degrade to an inline (blocking) lookup */
        *outn = zan_io_resolve_ipv4(j->name);
        free(j->name);
        free(j);
        if (zan_co_ready_hook) zan_co_ready_hook(frame, step);
        return;
    }
    pthread_detach(t);
}

/* the emitted scheduler's pump calls this whenever its ready queue drains:
 * poll the parked watchers (bounded by the earliest timer deadline), wake
 * fd-ready ones, expire recv-to deadlines. Returns 1 when watchers exist
 * (the loop continues), 0 when none (caller falls back to timer sleep). */
int64_t zan_io_poll(int64_t timer_wait_ns) {
    int i, k = 0;
    for (i = 0; i < zt_io_n; i++)
        if (zt_io_ws[i].active) zt_io_ws[k++] = zt_io_ws[i];
    zt_io_n = k;
    if (k == 0) return 0;
    struct pollfd *fds = calloc((size_t)k, sizeof(*fds));
    if (!fds) abort();
    int timeout = -1;                     /* watchers with no timer: block */
    if (timer_wait_ns >= 0) {
        long long ms = (timer_wait_ns + 999999) / 1000000;
        if (ms > 0x7fffffffLL) ms = 0x7fffffffLL;
        timeout = (int)ms;
    }
    for (i = 0; i < k; i++) {
        fds[i].fd = zt_io_ws[i].fd;
        fds[i].events = (zt_io_ws[i].kind == ZT_IO_WOUT) ? POLLOUT : POLLIN;
        fds[i].revents = 0;
    }
    int rc;
    do { rc = poll(fds, (nfds_t)k, timeout); } while (rc < 0 && errno == EINTR);
    long long now = zan_monotonic_ns();
    for (i = 0; i < k; i++) {
        ZtIOWatch *w = &zt_io_ws[i];
        if (!w->active) continue;
        short rev = fds[i].revents;
        if (rev & POLLNVAL) {            /* fd died without a close_notify */
            zt_io_deliver(w, w->kind == ZT_IO_ACCEPT ? -1 : 0);
            continue;
        }
        if (rev & (POLLIN | POLLOUT | POLLERR | POLLHUP)) {
            switch (w->kind) {
            case ZT_IO_W:
            case ZT_IO_WOUT:
                zt_io_deliver(w, 0);
                break;
            case ZT_IO_RECV: {
                long long n = zt_recv_try(w->fd, w->buf, w->len);
                if (n != -2) zt_io_deliver(w, n);  /* spurious: keep watching */
                break;
            }
            case ZT_IO_RECV_TO: {
                long long n = zt_recv_try(w->fd, w->buf, w->len);
                if (n == -2) {
                    if (w->deadline_ns && now >= w->deadline_ns)
                        zt_io_deliver(w, -1);
                } else {
                    zt_io_deliver(w, n);   /* data beats a passing deadline */
                }
                break;
            }
            case ZT_IO_ACCEPT: {
                int c;
                for (;;) {
                    c = accept(w->fd, NULL, NULL);
                    if (c >= 0 || errno != EINTR) break;
                }
                if (!(c < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)))
                    zt_io_deliver(w, c);
                break;
            }
            case ZT_IO_DNS: {
                char drain[64];
                while (read(zt_dns_pipe[0], drain, sizeof(drain)) > 0) {}
                pthread_mutex_lock(&zt_dns_mu);
                ZtDnsJob *list = zt_dns_done;
                zt_dns_done = NULL;
                pthread_mutex_unlock(&zt_dns_mu);
                while (list) {
                    ZtDnsJob *nx = list->next;
                    *list->outn = list->result;
                    if (zan_co_ready_hook) zan_co_ready_hook(list->frame, list->step);
                    free(list->name);
                    free(list);
                    list = nx;
                }
                break;
            }
            }
            continue;
        }
        if (w->kind == ZT_IO_RECV_TO && w->deadline_ns && now >= w->deadline_ns)
            zt_io_deliver(w, -1);
    }
    free(fds);
    return 1;
}

void zan_io_close_notify(intptr_t fd) {
    /* called BEFORE the actual close (stdlib's Socket.Close): wake every
     * watcher on this fd so a parked accept/recv fails instead of hanging.
     * recv/recv_to wake with the closed shape (0 bytes), accept with -1. */
    int i;
    for (i = 0; i < zt_io_n; i++) {
        ZtIOWatch *w = &zt_io_ws[i];
        if (w->active && w->fd == (int)fd)
            zt_io_deliver(w, w->kind == ZT_IO_ACCEPT ? -1 : 0);
    }
}
#endif /* ZAN_RT_CORE_ZAN */

#ifndef ZAN_RT_CORE_ZAN
int64_t zan_monotonic_us(void) {
    return zan_monotonic_ns() / 1000;
}
#endif /* ZAN_RT_CORE_ZAN */


/* ---- b35 (v18o-63): 音频面已完全由 runtime_core.zan 纯 Zan 实现 ---- */
/* ---- b37 (v18o-67): 压缩资源解码已由 runtime_core.zan 纯 Zan 实现 (Deflate/inflate) ---- */
/* ---- b38 (v18o-69): 定时器 + 协程门/单线程驱动已完全由 runtime_core.zan 纯 Zan 实现 ---- */
/* ---- b39 (v18o-71): 目录列表 + 线程解绑 + 非阻塞套接字导出面已完全由 runtime_core.zan 纯 Zan 实现 ---- */
/* zanstubs.c 在 -DZAN_RT_CORE_ZAN 下已完全退役，导出 0 全局符号 (Zero C Remainder) */
