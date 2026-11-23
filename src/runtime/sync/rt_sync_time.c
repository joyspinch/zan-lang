/* rt_sync_time.c: 高精度单调时钟与计时器秒表底层原语实现 */

#ifdef _WIN32
static inline int64_t zan_qpc_freq(void) {
    static int64_t freq;
    int64_t f = __atomic_load_n(&freq, __ATOMIC_RELAXED);
    if (!f) {
        LARGE_INTEGER lf;
        QueryPerformanceFrequency(&lf);
        f = lf.QuadPart;
        __atomic_store_n(&freq, f, __ATOMIC_RELAXED);
    }
    return f;
}
#endif

int64_t zan_monotonic_us(void) {
#ifdef _WIN32
    int64_t f = zan_qpc_freq();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (f == 10000000LL) return (int64_t)(now.QuadPart / 10LL);
    return (int64_t)((now.QuadPart / f) * 1000000
        + ((now.QuadPart % f) * 1000000) / f);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + (int64_t)ts.tv_nsec / 1000;
#endif
}

int64_t zan_monotonic_ns(void) {
#ifdef _WIN32
    int64_t f = zan_qpc_freq();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (f == 10000000LL) return (int64_t)(now.QuadPart * 100LL);
    return (int64_t)((now.QuadPart / f) * 1000000000
        + ((now.QuadPart % f) * 1000000000) / f);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + (int64_t)ts.tv_nsec;
#endif
}

int64_t zan_stopwatch_ticks(void) {
#ifdef _WIN32
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
#else
    return zan_monotonic_ns();
#endif
}

int64_t zan_stopwatch_frequency(void) {
#ifdef _WIN32
    return zan_qpc_freq();
#else
    return 1000000000;
#endif
}

int64_t zan_monotonic_ticks(void) {
    return zan_stopwatch_ticks();
}

int64_t zan_monotonic_frequency(void) {
    return zan_stopwatch_frequency();
}
