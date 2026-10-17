/* RETIRED (第十四批). This file used to shim the macOS-only surface the
 * Zan runtime emitted (CommonCrypto digests, OSAtomic, os_unfair_lock,
 * mach task ports, musl-missing libc spellings) so the ELF lane could link
 * and behave identically in a Linux guest. Batch 14 made the runtime and
 * the compiler's emitters target-agnostic instead: digests/hmac/urandom/
 * vm_read/locks/exe_path now live in runtime_core.zan behind zan_* faces
 * with per-platform implementations inside (#if MACOS / musl direct), so
 * nothing here is referenced anymore and linux_vehicle.sh links the ELF
 * with runtime_core.elf.o + musl ONLY — zero project C. The musl-side
 * struct stat/errno/mutexattr/readlink knowledge this file embodied moved
 * into runtime_core.zan's #if !MACOS arms; digest vectors are pinned in
 * tests/selfhost/native_digests (RFC 1321/3174/4231, FIPS 180-4) and run
 * through the guest lane as a default fixture. Kept as a historical
 * record; do not extend.
 *
 * Also see compat/net/if_dl.h (shadow header for the reactor includes) —
 * likewise no longer referenced, since zanstubs.c no longer compiles for
 * the ELF lane. Nothing here was ever referenced on the macOS lane. */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/syscall.h>

/* ---- libc spellings musl lacks or spells differently ---- */

int *__error(void) { return &errno; }

int pthread_threadid_np(pthread_t t, uint64_t *tid) {
    if (tid == 0) { return EINVAL; }
    *tid = (uint64_t)syscall(SYS_gettid);
    return 0;
}

/* _NSGetExecutablePath: /proc/self/exe is the Linux equivalent; the
 * contract (0 = copied; -1 = buffer too small, *bufsize = needed length)
 * is kept so callers need no OS branch. */
int _NSGetExecutablePath(char *buf, unsigned int *bufsize) {
    ssize_t n = readlink("/proc/self/exe", buf, (size_t)*bufsize);
    if (n < 0) { return -1; }
    if ((unsigned long)n >= (unsigned long)*bufsize) { return -1; }
    buf[n] = 0;
    *bufsize = (unsigned int)n;
    return 0;
}

/* arc4random_buf: musl has no resolver; getrandom(2) with a fallback to
 * /dev/urandom for very large requests (getrandom caps at 32 MiB). */
void arc4random_buf(void *buf, unsigned long n) {
    unsigned char *p = buf;
    while (n > 0) {
        ssize_t r = syscall(SYS_getrandom, p, n, 0);
        if (r <= 0) {
            if (r < 0 && errno == EINTR) { continue; }
            int fd = open("/dev/urandom", O_RDONLY);
            if (fd < 0) { return; }
            r = read(fd, p, n);
            close(fd);
            if (r <= 0) { return; }
        }
        p += r;
        n -= (unsigned long)r;
    }
}

/* ---- atomics / locks ---- */

long long OSAtomicAdd64Barrier(long long v, long long *p) {
    return __atomic_add_fetch(p, v, __ATOMIC_SEQ_CST);
}

int OSAtomicCompareAndSwap64Barrier(long long o, long long n, long long *p) {
    return __atomic_compare_exchange_n(p, &o, n, 0,
        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) ? 1 : 0;
}

/* os_unfair_lock is a 4-byte address-keyed lock; a test-and-set spinlock
 * keeps the runtime's embedded 4-byte slot layout (a pthread_mutex_t
 * would overflow it). */
void os_unfair_lock_lock(void *p) {
    while (__atomic_test_and_set((unsigned char *)p, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n((unsigned char *)p, __ATOMIC_RELAXED)) { }
    }
}

void os_unfair_lock_unlock(void *p) {
    __atomic_clear((unsigned char *)p, __ATOMIC_RELEASE);
}

/* ---- mach ---- */

/* Task port: unused on Linux (self memory needs no port); defined as
 * data because macOS callers link it as a symbol, not a call. */
unsigned long mach_task_self_ = 0;

/* Self-memory read: mach_vm_read_overwrite(target, addr, size, outbuf,
 * outsize) -> 0 (KERN_SUCCESS) with the copied size written back. */
int mach_vm_read_overwrite(unsigned long target, unsigned long long addr,
    unsigned long long size, unsigned long long outbuf,
    unsigned long long *outsize) {
    if (target != mach_task_self_ || outsize == 0) { return 1; }
    memcpy((void *)(unsigned long)outbuf, (const void *)(unsigned long)addr,
        (size_t)size);
    *outsize = size;
    return 0;
}

/* ---- CommonCrypto digests (MD5 / SHA-1 / SHA-256 + HMAC) ----
 * Standard RFC 1321 / FIPS 180 constructions; vectors pinned in
 * zanlinuxshims_test.c against the published test suites. Only the four
 * symbols runtime_core references are provided (CC_SHA512 is not: the
 * Zan _zan_sha512 API is implemented in Zan, not via CommonCrypto). */

static unsigned int zan_rotl(unsigned int x, int n) {
    return (x << n) | (x >> (32 - n));
}
static unsigned int zan_rotr(unsigned int x, int n) {
    return (x >> n) | (x << (32 - n));
}

/* ---- MD5 (RFC 1321) ---- */

struct zan_md5 {
    unsigned int a, b, c, d;
    unsigned long long len;
    unsigned long bl;
    unsigned char buf[64];
};

static void zan_md5_block(struct zan_md5 *x, const unsigned char *p) {
    unsigned int m[16];
    for (int i = 0; i < 16; i++) {
        m[i] = (unsigned int)p[i * 4] | ((unsigned int)p[i * 4 + 1] << 8) |
               ((unsigned int)p[i * 4 + 2] << 16) |
               ((unsigned int)p[i * 4 + 3] << 24);
    }
    static const unsigned char r[64] = {
        0x00000007, 0x0000000c, 0x00000011, 0x00000016, 0x00000007, 0x0000000c, 0x00000011, 0x00000016, 0x00000007, 0x0000000c, 0x00000011, 0x00000016, 0x00000007, 0x0000000c, 0x00000011, 0x00000016,
        0x00000005, 0x00000009, 0x0000000e, 0x00000014, 0x00000005, 0x00000009, 0x0000000e, 0x00000014, 0x00000005, 0x00000009, 0x0000000e, 0x00000014, 0x00000005, 0x00000009, 0x0000000e, 0x00000014,
        0x00000004, 0x0000000b, 0x00000010, 0x00000017, 0x00000004, 0x0000000b, 0x00000010, 0x00000017, 0x00000004, 0x0000000b, 0x00000010, 0x00000017, 0x00000004, 0x0000000b, 0x00000010, 0x00000017,
        0x00000006, 0x0000000a, 0x0000000f, 0x00000015, 0x00000006, 0x0000000a, 0x0000000f, 0x00000015, 0x00000006, 0x0000000a, 0x0000000f, 0x00000015, 0x00000006, 0x0000000a, 0x0000000f, 0x00000015,
    };
    static const unsigned int k[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
        0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
        0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
        0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
        0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
        0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
        0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
    };
    unsigned int a = x->a, b = x->b, c = x->c, d = x->d;
    for (int i = 0; i < 64; i++) {
        unsigned int f;
        int g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
        else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15; }
        else { f = c ^ (b | ~d); g = (7 * i) & 15; }
        unsigned int tmp = d;
        d = c;
        c = b;
        b = b + zan_rotl(a + f + k[i] + m[g], r[i]);
        a = tmp;
    }
    x->a += a;
    x->b += b;
    x->c += c;
    x->d += d;
}

static void zan_md5_reset(struct zan_md5 *x) {
    x->a = 0x67452301;
    x->b = 0xefcdab89;
    x->c = 0x98badcfe;
    x->d = 0x10325476;
    x->len = 0;
    x->bl = 0;
}

static void zan_md5_feed(struct zan_md5 *x, const void *data,
    unsigned long n) {
    const unsigned char *p = data;
    x->len += n;
    while (n > 0) {
        if (x->bl == 0 && n >= 64) {
            zan_md5_block(x, p);
            p += 64;
            n -= 64;
            continue;
        }
        unsigned long take = 64 - x->bl;
        if (take > n) { take = n; }
        memcpy(x->buf + x->bl, p, take);
        x->bl += take;
        p += take;
        n -= take;
        if (x->bl == 64) {
            zan_md5_block(x, x->buf);
            x->bl = 0;
        }
    }
}

static void zan_md5_done(struct zan_md5 *x, unsigned char *out) {
    unsigned long long bits = x->len * 8;
    unsigned char pad = 0x80;
    zan_md5_feed(x, &pad, 1);
    unsigned char z = 0;
    while (x->bl != 56) { zan_md5_feed(x, &z, 1); }
    unsigned char tail[8];
    for (int i = 0; i < 8; i++) {
        tail[i] = (unsigned char)(bits >> (8 * i));
    }
    zan_md5_feed(x, tail, 8);
    for (int i = 0; i < 4; i++) { out[i] = (unsigned char)(x->a >> (8 * i)); }
    for (int i = 0; i < 4; i++) { out[4 + i] = (unsigned char)(x->b >> (8 * i)); }
    for (int i = 0; i < 4; i++) { out[8 + i] = (unsigned char)(x->c >> (8 * i)); }
    for (int i = 0; i < 4; i++) { out[12 + i] = (unsigned char)(x->d >> (8 * i)); }
}

/* ---- SHA-1 / SHA-256 (FIPS 180-4) ---- */

struct zan_sha32 {
    unsigned int h[8];
    unsigned long long len;
    unsigned long bl;
    unsigned char buf[64];
    int words; /* 5 = SHA-1, 8 = SHA-256 */
};

static const unsigned int zan_sha256_k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static void zan_sha32_block(struct zan_sha32 *x, const unsigned char *p) {
    unsigned int w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((unsigned int)p[i * 4] << 24) |
               ((unsigned int)p[i * 4 + 1] << 16) |
               ((unsigned int)p[i * 4 + 2] << 8) |
               ((unsigned int)p[i * 4 + 3]);
    }
    if (x->words == 5) {
        for (int i = 16; i < 80; i++) {
            w[i] = zan_rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
    } else {
        for (int i = 16; i < 64; i++) {
            unsigned int s0 = zan_rotr(w[i - 15], 7) ^ zan_rotr(w[i - 15], 18) ^
                (w[i - 15] >> 3);
            unsigned int s1 = zan_rotr(w[i - 2], 17) ^ zan_rotr(w[i - 2], 19) ^
                (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
    }
    unsigned int a = x->h[0], b = x->h[1], c = x->h[2], d = x->h[3];
    unsigned int e = x->h[4], f = 0, g = 0, h = 0;
    if (x->words == 8) {
        f = x->h[5];
        g = x->h[6];
        h = x->h[7];
    }
    for (int t = 0; t < 80; t++) {
        if (t == 64 && x->words == 8) { break; } /* SHA-256 = 64 rounds; SHA-1 = 80 */
        unsigned int t1, t2;
        if (x->words == 5) {
            unsigned int f, k;
            if (t < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
            else if (t < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
            else if (t < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
            else { f = b ^ c ^ d; k = 0xca62c1d6; }
            t1 = e + zan_rotl(a, 5) + f + k + w[t];
            t2 = zan_rotl(a, 30);
            e = d;
            d = c;
            c = zan_rotl(b, 30);
            b = a;
            a = t1;
            continue;
        }
        unsigned int s1 = zan_rotr(e, 6) ^ zan_rotr(e, 11) ^ zan_rotr(e, 25);
        unsigned int ch = (e & f) ^ (~e & g);
        t1 = h + s1 + ch + zan_sha256_k[t] + w[t];
        unsigned int s0 = zan_rotr(a, 2) ^ zan_rotr(a, 13) ^ zan_rotr(a, 22);
        unsigned int mj = (a & b) ^ (a & c) ^ (b & c);
        t2 = s0 + mj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    x->h[0] += a;
    x->h[1] += b;
    x->h[2] += c;
    x->h[3] += d;
    x->h[4] += e;
    if (x->words == 8) {
        x->h[5] += f;
        x->h[6] += g;
        x->h[7] += h;
    }
}

static void zan_sha32_reset(struct zan_sha32 *x, int words) {
    x->words = words;
    x->len = 0;
    x->bl = 0;
    if (words == 5) {
        x->h[0] = 0x67452301;
        x->h[1] = 0xefcdab89;
        x->h[2] = 0x98badcfe;
        x->h[3] = 0x10325476;
        x->h[4] = 0xc3d2e1f0;
    } else {
/* IV = first 32 bits of the fractional square roots of the first
 * 8 primes (FIPS 180-4 5.3.3), pinned here: */
        static const unsigned int iv[8] = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
        };
        memcpy(x->h, iv, sizeof(iv));
    }
}

static void zan_sha32_feed(struct zan_sha32 *x, const void *data,
    unsigned long n) {
    const unsigned char *p = data;
    x->len += n;
    while (n > 0) {
        if (x->bl == 0 && n >= 64) {
            zan_sha32_block(x, p);
            p += 64;
            n -= 64;
            continue;
        }
        unsigned long take = 64 - x->bl;
        if (take > n) { take = n; }
        memcpy(x->buf + x->bl, p, take);
        x->bl += take;
        p += take;
        n -= take;
        if (x->bl == 64) {
            zan_sha32_block(x, x->buf);
            x->bl = 0;
        }
    }
}

static void zan_sha32_done(struct zan_sha32 *x, unsigned char *out) {
    unsigned long long bits = x->len * 8;
    unsigned char pad = 0x80;
    zan_sha32_feed(x, &pad, 1);
    unsigned char z = 0;
    while (x->bl != 56) { zan_sha32_feed(x, &z, 1); }
    unsigned char tail[8];
    for (int i = 0; i < 8; i++) {
        tail[i] = (unsigned char)(bits >> (56 - 8 * i));
    }
    zan_sha32_feed(x, tail, 8);
    for (int i = 0; i < x->words; i++) {
        out[i * 4] = (unsigned char)(x->h[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(x->h[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(x->h[i] >> 8);
        out[i * 4 + 3] = (unsigned char)(x->h[i]);
    }
}

/* ---- CommonCrypto entry points ---- */

unsigned char *CC_MD5(const void *data, unsigned long len, unsigned char *md) {
    struct zan_md5 x;
    zan_md5_reset(&x);
    zan_md5_feed(&x, data, len);
    zan_md5_done(&x, md);
    return md;
}

unsigned char *CC_SHA1(const void *data, unsigned long len, unsigned char *md) {
    struct zan_sha32 x;
    zan_sha32_reset(&x, 5);
    zan_sha32_feed(&x, data, len);
    zan_sha32_done(&x, md);
    return md;
}

unsigned char *CC_SHA256(const void *data, unsigned long len,
    unsigned char *md) {
    struct zan_sha32 x;
    zan_sha32_reset(&x, 8);
    zan_sha32_feed(&x, data, len);
    zan_sha32_done(&x, md);
    return md;
}

/* CCHmac: alg 0 = MD5, 1 = SHA1, 2 = SHA256 (Apple kCCHmacAlg* values).
 * HMAC via the standard ipad/opad construction over the 64-byte block
 * algorithms above. */

void CCHmac(int alg, const void *key, unsigned long keyLen,
    const void *data, unsigned long dataLen, void *macOut) {
    if (alg != 0 && alg != 1 && alg != 2) { return; }
    unsigned char k0[64], inner[32];
    unsigned char *ip, *op;
    memset(k0, 0, 64);
    if (keyLen > 64) {
        if (alg == 0) { CC_MD5(key, keyLen, k0); }
        else if (alg == 1) { CC_SHA1(key, keyLen, k0); }
        else { CC_SHA256(key, keyLen, k0); }
    } else {
        memcpy(k0, key, keyLen);
    }
    ip = k0; /* reuse: build padded copies below */
    unsigned char ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = k0[i] ^ 0x36;
        opad[i] = k0[i] ^ 0x5c;
    }
    if (alg == 0) {
        struct zan_md5 x;
        zan_md5_reset(&x);
        zan_md5_feed(&x, ipad, 64);
        zan_md5_feed(&x, data, dataLen);
        zan_md5_done(&x, inner);
        zan_md5_reset(&x);
        zan_md5_feed(&x, opad, 64);
        zan_md5_feed(&x, inner, 16);
        zan_md5_done(&x, macOut);
        return;
    }
    struct zan_sha32 x;
    int words = (alg == 1) ? 5 : 8;
    int macLen = words * 4;
    zan_sha32_reset(&x, words);
    zan_sha32_feed(&x, ipad, 64);
    zan_sha32_feed(&x, data, dataLen);
    zan_sha32_done(&x, inner);
    zan_sha32_reset(&x, words);
    zan_sha32_feed(&x, opad, 64);
    zan_sha32_feed(&x, inner, (unsigned long)macLen);
    zan_sha32_done(&x, macOut);
    (void)ip;
    (void)op;
}

/* ---- GCD semaphore surface (v18o-22 车道衔接) ----
 * stdlib System/Threading 的 #elif MACOS 臂经 DllImport 直呼
 * dispatch_semaphore_*(libSystem);ELF 车道在 macOS 主机上编译,同一臂
 * 被编进(条件定义目前 per-HOST,ZAN_TARGET 条件化是 known_open 工程),
 * 而 musl 没有 libdispatch。GCD 信号量语义用 musl 未命名 POSIX 信号量
 * 逐位给全(macOS 的 sem_init 是 ENOSYS 桩,Linux 是真实现)。时间约定
 * 与 stdlib 调用面一致:dispatch_time(0,delta) 返回单调 ns 绝对期限,
 * -1 是 DISPATCH_TIME_FOREVER;wait 0=成功,超时 49(macOS
 * KERN_OPERATION_TIMED_OUT),其余 errno 原样。 */
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <semaphore.h>

static long long zl_mono_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void *dispatch_semaphore_create(long value) {
    sem_t *s = (sem_t *)calloc(1, 64);
    if (!s) return NULL;
    if (sem_init(s, 0, value < 0 ? 0u : (unsigned)value) != 0) {
        free(s);
        return NULL;
    }
    return s;
}

long dispatch_time(long base, long deltaNs) {
    if (base == -1LL) return -1LL;         /* FOREVER 吸收一切增量 */
    return zl_mono_ns() + deltaNs;
}

int dispatch_semaphore_wait(void *sem, long timeout) {
    sem_t *s = (sem_t *)sem;
    if (timeout == -1LL) {
        while (sem_wait(s) != 0) {
            if (errno != EINTR) return errno;
        }
        return 0;
    }
    long long delta = (long long)timeout - zl_mono_ns();
    if (delta <= 0) {
        return sem_trywait(s) == 0 ? 0 : 49;
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)(delta / 1000000000LL);
    ts.tv_nsec += (long)(delta % 1000000000LL);
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_nsec -= 1000000000L;
        ts.tv_sec += 1;
    }
    int r;
    while ((r = sem_timedwait(s, &ts)) != 0 && errno == EINTR) {}
    return r == 0 ? 0 : (errno == ETIMEDOUT ? 49 : errno);
}

int dispatch_semaphore_signal(void *sem) {
    return sem_post((sem_t *)sem) == 0 ? 0 : errno;
}

void dispatch_release(void *sem) {
    sem_destroy((sem_t *)sem);
    free(sem);
}

/* monitor 的递归锁类型常数:v11 运行时按 Darwin 实测硬编码
 * PTHREAD_MUTEX_RECURSIVE=2,musl 的 2 是 ERRORCHECK(重入直接
 * EDEADLK,lock 语句的重入契约在 guest 车道被打破)。本文件属主车道
 * 垫片:静态链接时本定义胜 musl 档案副本,2→1 翻译;其它类型值原样
 * 透传。真正的 per-TARGET 常数条件化仍是 known_open 工程。 */
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type) {
    /* 静态链接下同名定义遮挡 musl 档案副本,不能回调真身;musl 1.2.x
     * 的实现就是把类型存 pthread_mutexattr_t 首字的低 4 位
     * (*a = (*a & ~15) | type,>2 返回 EINVAL),attr 在我们的调用面里
     * 恒为 calloc 全零(mon_ensure 只走 init+settype+destroy),低位写
     * 与真身逐位等价;native_sync 的重入/跨条纹断言在 guest 逐位仲裁。 */
    if ((unsigned)type > 2u) return 22;    /* EINVAL */
    if (type == 2) type = 1;               /* Darwin RECURSIVE -> musl RECURSIVE */
    unsigned *a = (unsigned *)attr;
    *a = (*a & ~15u) | (unsigned)type;
    return 0;
}
