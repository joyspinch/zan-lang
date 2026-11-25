/* rt_sync_time.c: 高精度单调时钟与计时器秒表底层原语实现 */

int64_t zan_monotonic_us(void) {
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (int64_t)((now.QuadPart / frequency.QuadPart) * 1000000
        + ((now.QuadPart % frequency.QuadPart) * 1000000) / frequency.QuadPart);
#elif defined(__APPLE__)
    return (int64_t)(clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) / 1000);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + (int64_t)ts.tv_nsec / 1000;
#endif
}

int64_t zan_monotonic_ns(void) {
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (int64_t)((now.QuadPart / frequency.QuadPart) * 1000000000
        + ((now.QuadPart % frequency.QuadPart) * 1000000000) / frequency.QuadPart);
#elif defined(__APPLE__)
    /* Darwin's CLOCK_MONOTONIC only advances in whole microseconds, while
     * Stopwatch reports a 1 GHz frequency: short samples read as 0. The raw
     * clock is mach_continuous_time (counts sleep, like MONOTONIC) at the
     * timebase resolution (41.7 ns on Apple Silicon). */
    return (int64_t)clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
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
    static LARGE_INTEGER frequency;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart;
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
