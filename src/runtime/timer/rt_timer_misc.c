/* rt_timer_misc.c: 异步执行器参数配置、浮点精确格式化、内存度量与 SipHash 字典键散列 */

static volatile int g_cfg_workers   = 0;
static volatile int g_cfg_io_shards = 0;
static volatile int g_cfg_sync_fast = -1;   /* -1 = unset */

void zan_async_set_workers(int32_t n)    { __atomic_store_n(&g_cfg_workers, (n > 0) ? (int)n : 0, __ATOMIC_RELEASE); }
void zan_async_set_io_shards(int32_t n)  { __atomic_store_n(&g_cfg_io_shards, (n > 0) ? (int)n : 0, __ATOMIC_RELEASE); }
void zan_async_set_sync_fast(int32_t on) { __atomic_store_n(&g_cfg_sync_fast, on ? 1 : 0, __ATOMIC_RELEASE); }

int32_t zan_async_cfg_workers(void)   { return (int32_t)__atomic_load_n(&g_cfg_workers, __ATOMIC_ACQUIRE); }
int32_t zan_async_cfg_io_shards(void) { return (int32_t)__atomic_load_n(&g_cfg_io_shards, __ATOMIC_ACQUIRE); }
int32_t zan_async_cfg_sync_fast(void) { return (int32_t)__atomic_load_n(&g_cfg_sync_fast, __ATOMIC_ACQUIRE); }

void zan_rt_dbl_str(char *buf, unsigned long long cap, double v) {
    int is_flt = (cap & 0x8000000000000000ULL) != 0;
    size_t actual_cap = (size_t)(cap & ~0x8000000000000000ULL);
    if (v != v) { snprintf(buf, actual_cap, "NaN"); return; }
    if (v == 0.0) { snprintf(buf, actual_cap, signbit(v) ? "-0" : "0"); return; }
    const char *sign = signbit(v) ? "-" : "";
    double a = (v < 0.0) ? -v : v;
    if (is_flt ? (a > 3.402823466e+38) : (a > 1.7976931348623157e308)) {
        snprintf(buf, actual_cap, "%sInfinity", sign);
        return;
    }
    char m[40];
    int p;
    int max_p = is_flt ? 10 : 17;
    for (p = 1; p < max_p; p++) {
        snprintf(m, sizeof m, "%.*e", p - 1, v);
        if (is_flt) {
            if (strtof(m, NULL) == (float)v) break;
        } else {
            if (strtod(m, NULL) == v) break;
        }
    }
    if (p == max_p) snprintf(m, sizeof m, is_flt ? "%.9e" : "%.16e", v);

    /* split "-d */
    char *q = (*m == '-') ? m + 1 : m;
    char *es = strchr(q, 'e');
    int e10 = atoi(es + 1);
    char sig[24];
    int n = 0;
    for (char *c = q; c < es; c++)
        if (*c >= '0' && *c <= '9') sig[n++] = *c;
    while (n > 1 && sig[n - 1] == '0') n--;
    sig[n] = 0;

    char out[48];
    if (e10 >= -4 && e10 <= 14) {
        if (e10 >= n) {
            char zeros[24];
            int z = e10 - (n - 1);
            memset(zeros, '0', (size_t)z);
            zeros[z] = 0;
            snprintf(out, sizeof out, "%s%s%s", sign, sig, zeros);
        } else if (e10 >= 0) {
            int h = e10 + 1;
            if (sig[h])
                snprintf(out, sizeof out, "%s%.*s.%s", sign, h, sig, sig + h);
            else
                snprintf(out, sizeof out, "%s%.*s", sign, h, sig);
        } else {
            /* |v| < 1: 0. + (-e10-1) zeros + digits */
            char zeros[8];
            int z = -e10 - 1;
            memset(zeros, '0', (size_t)z);
            zeros[z] = 0;
            snprintf(out, sizeof out, "%s0.%s%s", sign, zeros, sig);
        }
    } else {
        /* scientific: d[ */
        if (sig[1])
            snprintf(out, sizeof out, "%s%c.%sE%+03d", sign, sig[0], sig + 1,
                     e10);
        else
            snprintf(out, sizeof out, "%s%sE%+03d", sign, sig, e10);
    }
    snprintf(buf, actual_cap, "%s", out);
}

void zan_rt_flt_str(char *buf, unsigned long long cap, float v) {
    zan_rt_dbl_str(buf, cap | 0x8000000000000000ULL, (double)v);
}

double zan_rt_dbl_parse(const char *s, char **endp) {
    if (endp) *endp = (char *)s;
    if (strcmp(s, "NaN") == 0) {
        if (endp) *endp = (char *)s + 3;
        return (double)NAN;
    }
    if (strcmp(s, "Infinity") == 0 || strcmp(s, "+Infinity") == 0) {
        if (endp) *endp = (char *)s + (s[0] == '+' ? 9 : 8);
        return (double)INFINITY;
    }
    if (strcmp(s, "-Infinity") == 0) {
        if (endp) *endp = (char *)s + 9;
        return -(double)INFINITY;
    }
    errno = 0;
    double r = strtod(s, endp);
    if (errno == ERANGE && (r == (double)INFINITY || r == -(double)INFINITY)) {
        /* strtod 上溢：把 endp 拨回起点，让调用方的"零消费"判坏接住
         * （C# double.Parse 抛 OverflowException、TryParse 返 false），
         * 与 "Infinity" 特殊拼写的合法 INF 回读区分开。 */
        if (endp) { *endp = (char *)s; }
    }
    return r;
}

/* 堆内存分配度量 (A464) */
#if defined(__wasm__) || defined(ZAN_BARE_METAL)
#define ZAN_ALLOC_TLS
#else
#define ZAN_ALLOC_TLS __thread
#endif

static ZAN_ALLOC_TLS int64_t g_thread_alloc_bytes;
static volatile int64_t g_total_alloc_bytes;

void zan_rt_alloc_add(int64_t bytes) {
    if (bytes <= 0) return;
    g_thread_alloc_bytes += bytes;
    __atomic_fetch_add(&g_total_alloc_bytes, bytes, __ATOMIC_RELAXED);
}

int64_t zan_rt_alloc_bytes_thread(void) {
    return g_thread_alloc_bytes;
}

int64_t zan_rt_alloc_bytes_total(void) {
    return __atomic_load_n(&g_total_alloc_bytes, __ATOMIC_RELAXED);
}

__attribute__((weak))
size_t zan_mem_slabs(void) {
    return 0;
}

__attribute__((weak))
size_t zan_mem_trim(void) {
    return 0;
}


/* —— Dictionary/HashSet 键哈希（B-ID80）——
 * 曾在 IR 内联定种 FNV-1a（基值写死，irgen_builtins.c）：攻击者可离线
 * 构造碰撞键族，Web 服务端收到恶意键集即把哈希桶打满 O(N) 链。现改
 * SipHash-2-4 + 进程级随机盐：盐由 OS 熵源一次性取（构造器初始化 +
 * 惰性兜底），不出进程——离线碰撞构造失效，同进程内哈希稳定。
 * 落点选 rt_timer.o：它是唯一被所有链接路径（hosted/交叉 linux/macos/
 * ios/wasm32/android）无条件拉入的运行时对象；bare 单独走
 * bare 编译（ZAN_BARE_METAL）走本文件确定性盐分支。SipHash 本体共享于 rt_siphash.h
 * （性质探针：雪崩 0.4999、10^6 键 2^16 桶 max/mean=2.23、跨盐同桶率
 * ≈1/256）。 */
#if defined(ZAN_BARE_METAL)

/* bare：无 OS 熵源，确定性盐（行为可复现，威胁模型同 wasm 说明） */
int64_t __zan_dict_hash(const void *key, int64_t is_str) {
    if (key == NULL) return 0;
    const uint64_t k0 = 0x5a4e483153344801ull; /* "ZNH1S4H" */
    const uint64_t k1 = 0x9e3779b97f4a7c15ull;
    if (is_str) {
        size_t n = 0;
        const char *c = (const char *)key;
        while (c[n]) n++;
        return (int64_t)zan_siphash24(key, n, k0, k1);
    }
    return (int64_t)zan_siphash24(&key, sizeof(key), k0, k1);
}

#else /* !ZAN_BARE_METAL：hosted 熵盐版 */

#include "rt_siphash.h"
#if defined(_WIN32)
#include <time.h>
#endif

static uint64_t zan_hash_salt_k0 = 0, zan_hash_salt_k1 = 0;
static volatile uint64_t zan_hash_salt_ready = 0;

static void zan_hash_salt_fill(uint64_t *k0, uint64_t *k1) {
#if defined(_WIN32)
    unsigned int r0 = 0, r1 = 0;
    if (rand_s(&r0) == 0 && rand_s(&r1) == 0) {
        *k0 = ((uint64_t)r0 << 32) | r1;
        if (rand_s(&r0) == 0 && rand_s(&r1) == 0)
            *k1 = ((uint64_t)r0 << 32) | r1;
        else
            *k1 = (uint64_t)(uintptr_t)zan_hash_salt_fill;
        return;
    }
    LARGE_INTEGER pc;
    QueryPerformanceCounter(&pc);
    *k0 = pc.QuadPart ^ ((uint64_t)(uintptr_t)&pc << 17);
    *k1 = (uint64_t)(uintptr_t)zan_hash_salt_fill ^ ((uint64_t)GetCurrentProcessId() << 32);
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t got = fread(k0, 1, 16, f);
        fclose(f);
        if (got == 16) return;
    }
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    *k0 = ((uint64_t)ts.tv_nsec << 32) ^ (uint64_t)(uintptr_t)k0;
    *k1 = (uint64_t)getpid() << 47 ^ (uint64_t)(uintptr_t)zan_hash_salt_fill;
#endif
}

__attribute__((constructor)) static void zan_hash_salt_ctor(void) {
    zan_hash_salt_fill(&zan_hash_salt_k0, &zan_hash_salt_k1);
    zan_hash_salt_ready = 1;
}

int64_t __zan_dict_hash(const void *key, int64_t is_str) {
    if (key == NULL) return 0;
    if (!zan_hash_salt_ready) {
        zan_hash_salt_fill(&zan_hash_salt_k0, &zan_hash_salt_k1);
        zan_hash_salt_ready = 1;
    }
    if (is_str) {
        return (int64_t)zan_siphash24(key, strlen((const char *)key),
                                      zan_hash_salt_k0, zan_hash_salt_k1);
    }
    /* 非字符串键走引用同一性：哈希指针 8 字节（同进程内稳定） */
    return (int64_t)zan_siphash24(&key, sizeof(key),
                                  zan_hash_salt_k0, zan_hash_salt_k1);
}

#endif /* !ZAN_BARE_METAL */
