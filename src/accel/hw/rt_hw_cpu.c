/* rt_hw_cpu.c: CPU 特征指令集探针与硬件 KAT 校验门禁实现 */

static volatile int g_cpuid_state = 0; /* 0: uninit, 1: initing, 2: ready */
static int g_has_popcnt   = 0;
static int g_has_lzcnt    = 0;
static int g_has_sse42    = 0;
static int g_has_avx2     = 0;
static int g_has_aesni    = 0;
static int g_has_vaes     = 0;
static int g_has_neon     = 0;
static int g_has_shani    = 0;
static int g_has_pclmul   = 0;

static int g_a_hw_aes    = 0;
static int g_a_hw_sha1   = 0;
static int g_a_hw_sha2   = 0;
static int g_a_hw_pmull  = 0;
static int g_a_hw_sha512 = 0;
static int g_a_hw_sm3    = 0;
static int g_a_hw_sm4    = 0;
static int g_a_hw_crc32  = 0;

#if defined(__aarch64__) || defined(_M_ARM64)
#if !defined(_WIN32) && !defined(__APPLE__)
#define ZAN_HWCAP_FP      (1u << 0)
#define ZAN_HWCAP_ASIMD   (1u << 1)
#define ZAN_HWCAP_AES     (1u << 3)
#define ZAN_HWCAP_PMULL   (1u << 4)
#define ZAN_HWCAP_SHA1    (1u << 5)
#define ZAN_HWCAP_SHA2    (1u << 6)
#define ZAN_HWCAP_CRC32   (1u << 7)
#define ZAN_HWCAP_SHA3    (1u << 17)
#define ZAN_HWCAP_SM3     (1u << 18)
#define ZAN_HWCAP_SM4     (1u << 19)
#define ZAN_HWCAP_SHA512  (1u << 21)
#endif
static int zan_sysctl_i(const char *key) {
#if defined(__APPLE__)
    int v = 0;
    size_t n = sizeof(v);
    if (sysctlbyname(key, &v, &n, NULL, 0) != 0) return 0;
    return v != 0;
#else
    (void)key;
    return 0;
#endif
}
static void zan_arm_probe(void) {
#if defined(_WIN32)
    int crypto = IsProcessorFeaturePresent(75 /* PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE */);
    g_a_hw_aes = g_a_hw_sha1 = g_a_hw_sha2 = g_a_hw_pmull = crypto;
    g_a_hw_crc32 = IsProcessorFeaturePresent(76 /* PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE */);
#elif defined(__APPLE__)
    int crypto = zan_sysctl_i("hw.optional.armv8_crypto");
    g_a_hw_aes = g_a_hw_sha1 = g_a_hw_sha2 = g_a_hw_pmull = crypto;
    g_a_hw_crc32  = zan_sysctl_i("hw.optional.armv8_crc32");
    g_a_hw_sha512 = zan_sysctl_i("hw.optional.armv8_2_sha512");
    g_a_hw_sm3    = zan_sysctl_i("hw.optional.arm.FEAT_SM3");
    g_a_hw_sm4    = zan_sysctl_i("hw.optional.arm.FEAT_SM4");
#else
    unsigned long hw = getauxval(16 /* AT_HWCAP */);
    g_a_hw_aes    = (hw & ZAN_HWCAP_AES)    != 0;
    g_a_hw_sha1   = (hw & ZAN_HWCAP_SHA1)   != 0;
    g_a_hw_sha2   = (hw & ZAN_HWCAP_SHA2)   != 0;
    g_a_hw_pmull  = (hw & ZAN_HWCAP_PMULL)  != 0;
    g_a_hw_crc32  = (hw & ZAN_HWCAP_CRC32)  != 0;
    g_a_hw_sha512 = (hw & ZAN_HWCAP_SHA512) != 0;
    g_a_hw_sm3    = (hw & ZAN_HWCAP_SM3)    != 0;
    g_a_hw_sm4    = (hw & ZAN_HWCAP_SM4)    != 0;
#endif
}
#endif /* aarch64 */

static int zan_hw_soft_forced(void) {
    const char *e = getenv("ZAN_NO_HWACCEL");
    return e != NULL && e[0] != '\0' && e[0] != '0';
}

static uint64_t zan_xgetbv_safe(void) {
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86))
#if defined(__GNUC__) || defined(__clang__)
    uint32_t eax, edx;
    __asm__ volatile(".byte 0x0f, 0x01, 0xd0" : "=a"(eax), "=d"(edx) : "c"(0));
    return ((uint64_t)edx << 32) | eax;
#elif defined(_MSC_VER)
    return _xgetbv(0);
#else
    return 0;
#endif
#else
    return 0;
#endif
}

static void zan_hw_init_cpu_features_impl(void) {
#if (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)) && (defined(__GNUC__) || defined(__clang__))
    uint32_t eax, ebx, ecx, edx;
    // EAX=1: Features
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));
    g_has_sse42   = (ecx & (1u << 20)) != 0;
    g_has_popcnt  = (ecx & (1u << 23)) != 0;
    g_has_aesni   = (ecx & (1u << 25)) != 0;
    g_has_pclmul  = (ecx & (1u << 1)) != 0;
    int osxsave   = (ecx & (1u << 27)) != 0;

    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
    int avx2_hw   = (ebx & (1u << 5)) != 0;
    int os_avx_ok = osxsave && ((zan_xgetbv_safe() & 6) == 6);
    g_has_avx2  = avx2_hw && os_avx_ok;
    g_has_shani = (ebx & (1u << 29)) != 0;
    g_has_vaes  = ((ecx & (1u << 9)) != 0) && g_has_avx2 && g_has_aesni;

    // EAX=0x80000001: LZCNT
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000001u), "c"(0));
    g_has_lzcnt = (ecx & (1u << 5)) != 0;
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int info[4];
    __cpuid(info, 1);
    g_has_sse42   = (info[2] & (1 << 20)) != 0;
    g_has_popcnt  = (info[2] & (1 << 23)) != 0;
    g_has_aesni   = (info[2] & (1 << 25)) != 0;
    g_has_pclmul  = (info[2] & (1 << 1)) != 0;
    int osxsave   = (info[2] & (1 << 27)) != 0;

    __cpuidex(info, 7, 0);
    int avx2_hw   = (info[1] & (1 << 5)) != 0;
    int os_avx_ok = osxsave && ((zan_xgetbv_safe() & 6) == 6);
    g_has_avx2  = avx2_hw && os_avx_ok;
    g_has_shani = (info[1] & (1 << 29)) != 0;
    g_has_vaes  = ((info[2] & (1 << 9)) != 0) && g_has_avx2 && g_has_aesni;

    __cpuid(info, 0x80000001);
    g_has_lzcnt = (info[2] & (1 << 5)) != 0;
#elif defined(__aarch64__) || defined(_M_ARM64)
    g_has_neon = 1;
    zan_arm_probe();
#endif
    if (zan_hw_soft_forced()) {
        g_has_sse42 = g_has_popcnt = g_has_lzcnt = 0;
        g_has_avx2 = g_has_aesni = g_has_vaes = g_has_shani = g_has_pclmul = 0;
#if defined(__aarch64__) || defined(_M_ARM64)
        g_a_hw_aes = g_a_hw_sha1 = g_a_hw_sha2 = g_a_hw_pmull = 0;
        g_a_hw_sha512 = g_a_hw_sm3 = g_a_hw_sm4 = g_a_hw_crc32 = 0;
#endif
    }
}

static void zan_hw_init_cpu_features(void) {
    if (__atomic_load_n(&g_cpuid_state, __ATOMIC_ACQUIRE) == 2) return;
    int expected = 0;
    if (__atomic_compare_exchange_n(&g_cpuid_state, &expected, 1, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        zan_hw_init_cpu_features_impl();
        __atomic_store_n(&g_cpuid_state, 2, __ATOMIC_RELEASE);
    } else {
        while (__atomic_load_n(&g_cpuid_state, __ATOMIC_ACQUIRE) != 2) {
#if defined(_WIN32)
            YieldProcessor();
#elif defined(__i386__) || defined(__x86_64__)
            __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
            __asm__ __volatile__("yield" ::: "memory");
#elif defined(__riscv)
            __asm__ __volatile__(".insn i 0x0F, 0, x0, x0, 0x010" ::: "memory");
#endif
        }
    }
}

static int g_gate_sha256 = 0;
static int g_gate_sha1   = 0;
static int g_gate_sha512 = 0;
static int g_gate_sm3    = 0;
static int g_gate_aes    = 0;
static int g_gate_ghash  = 0;
static int g_gate_crc32c = 0;
static int g_gate_crc32  = 0;
static int g_gate_sm4    = 0;

static int zan_hw_gate(int *state, int feature, int (*kat)(void)) {
    int s = __atomic_load_n(state, __ATOMIC_ACQUIRE);
    if (s != 0 && s != 2) return s == 1;
    if (!feature) {
        __atomic_store_n(state, -1, __ATOMIC_RELEASE);
        return 0;
    }
    int expected = 0;
    if (__atomic_compare_exchange_n(state, &expected, 2, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        int res = kat() ? 1 : -1;
        __atomic_store_n(state, res, __ATOMIC_RELEASE);
        return res == 1;
    }
    while ((s = __atomic_load_n(state, __ATOMIC_ACQUIRE)) == 2) {
#if defined(_WIN32)
        YieldProcessor();
#elif defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
        __asm__ __volatile__("yield" ::: "memory");
#elif defined(__riscv)
        __asm__ __volatile__(".insn i 0x0F, 0, x0, x0, 0x010" ::: "memory");
#endif
    }
    return s == 1;
}

int zan_hw_kat_state(int id) {
    zan_hw_init_cpu_features();
    switch (id) {
        case 5: case 9:  return g_gate_aes;
        case 7: case 10: return g_gate_sha1;
        case 11:         return g_gate_sha256;
        case 12:         return g_gate_ghash;
        case 13:         return g_gate_sha512;
        case 14:         return g_gate_sm3;
        case 16:         return g_gate_crc32c;
        case 17:         return g_gate_crc32;
        case 15:         return g_gate_sm4;
        default:         return -1;
    }
}

int zan_hw_has_popcnt(void) { zan_hw_init_cpu_features(); return g_has_popcnt; }
int zan_hw_has_lzcnt(void)  { zan_hw_init_cpu_features(); return g_has_lzcnt; }
int zan_hw_has_sse42(void)  { zan_hw_init_cpu_features(); return g_has_sse42; }
int zan_hw_has_avx2(void)   { zan_hw_init_cpu_features(); return g_has_avx2; }
int zan_hw_has_aesni(void)  { zan_hw_init_cpu_features(); return g_has_aesni; }
int zan_hw_has_vaes(void)   { zan_hw_init_cpu_features(); return g_has_vaes; }
int zan_hw_has_neon(void)   { zan_hw_init_cpu_features(); return g_has_neon; }
int zan_hw_has_shani(void)  { zan_hw_init_cpu_features(); return g_has_shani; }
int zan_hw_has_pclmul(void) { zan_hw_init_cpu_features(); return g_has_pclmul; }

int zan_hw_arm_aes(void)    { zan_hw_init_cpu_features(); return g_a_hw_aes; }
int zan_hw_arm_sha1(void)   { zan_hw_init_cpu_features(); return g_a_hw_sha1; }
int zan_hw_arm_sha2(void)   { zan_hw_init_cpu_features(); return g_a_hw_sha2; }
int zan_hw_arm_pmull(void)  { zan_hw_init_cpu_features(); return g_a_hw_pmull; }
int zan_hw_arm_sha512(void) { zan_hw_init_cpu_features(); return g_a_hw_sha512; }
int zan_hw_arm_sm3(void)    { zan_hw_init_cpu_features(); return g_a_hw_sm3; }
int zan_hw_arm_sm4(void)    { zan_hw_init_cpu_features(); return g_a_hw_sm4; }
int zan_hw_arm_crc32(void)  { zan_hw_init_cpu_features(); return g_a_hw_crc32; }

int zan_cpu_feature(int id) {
    switch (id) {
        case 1: return zan_hw_has_popcnt();
        case 2: return zan_hw_has_lzcnt();
        case 3: return zan_hw_has_sse42();
        case 4: return zan_hw_has_avx2();
        case 5: return zan_hw_has_aesni();
        case 6: return zan_hw_has_neon();
        case 7: return zan_hw_has_shani();
        case 8: return zan_hw_has_pclmul();
        case 9: return zan_hw_arm_aes();
        case 10: return zan_hw_arm_sha1();
        case 11: return zan_hw_arm_sha2();
        case 12: return zan_hw_arm_pmull();
        case 13: return zan_hw_arm_sha512();
        case 14: return zan_hw_arm_sm3();
        case 15: return zan_hw_arm_sm4();
        case 16: return zan_hw_arm_crc32();
        case 17: return zan_hw_has_vaes();
        default: return 0;
    }
}
