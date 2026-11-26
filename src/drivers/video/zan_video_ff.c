/* zan_video_ff.c -- FFmpeg (libav* 6/7/8) 零头文件运行时动态加载器
 * 被 zan_video.c 以 unity 方式包含。
 *
 * 结构体关键偏移均已在真实 FFmpeg 7 (avformat 61 / avcodec 61 / avutil 59)
 * 与 FFmpeg 8 (avformat 62 / avcodec 62 / avutil 60) DLL 上经探针逐字节核验：
 *   - AVFormatContext: nb_streams@44, streams@48, duration@104 (us), bit_rate@112
 *   - AVStream:        index@8, codecpar@16, time_base@32, duration@48, nb_frames@56, avg_frame_rate@88
 *   - AVCodecParameters:
 *       type@0, codec_id@4, format@28, bit_rate@32
 *       avcodec >= 61: width@72, height@76, ch_layout@128 (nb_channels@132), sample_rate@152
 *       avcodec == 60: width@56, height@60, ch_layout@112 (nb_channels@116), sample_rate@136
 *   - AVPacket:        pts@8, dts@16, data@24, size@32, stream_index@36, flags@40
 *   - AVFrame:         data[8]@0, linesize[8]@64, width@104, height@108, nb_samples@112, format@116, pts@136
 */

typedef struct zan_ff_rational {
    int32_t num;
    int32_t den;
} zan_ff_rational_t;

/* FFmpeg AVChannelLayout (24 bytes on 64-bit, FFmpeg 5.1/6/7/8 稳定 ABI) */
typedef struct zan_ff_ch_layout {
    int32_t order;
    int32_t nb_channels;
    uint64_t u_mask;
    void *opaque;
} zan_ff_ch_layout_t;

typedef struct zan_ff_api {
    int loaded;
    int tried;
    unsigned avformat_major;
    unsigned avcodec_major;
    unsigned avutil_major;
    char version_str[96];
    char custom_dir[512];

#ifdef _WIN32
    HMODULE h_avutil;
    HMODULE h_avcodec;
    HMODULE h_avformat;
    HMODULE h_swscale;
    HMODULE h_swresample;
#else
    void *h_avutil;
    void *h_avcodec;
    void *h_avformat;
    void *h_swscale;
    void *h_swresample;
#endif

    /* libavutil */
    unsigned (*avutil_version)(void);
    void *(*av_frame_alloc)(void);
    void (*av_frame_free)(void **frame);
    void (*av_frame_unref)(void *frame);
    void (*av_log_set_level)(int level);
    void (*av_channel_layout_default)(zan_ff_ch_layout_t *ch_layout, int nb_channels);
    int (*av_channel_layout_copy)(zan_ff_ch_layout_t *dst, const zan_ff_ch_layout_t *src);
    void (*av_channel_layout_uninit)(zan_ff_ch_layout_t *ch_layout);
    int64_t (*av_rescale_q)(int64_t a, zan_ff_rational_t bq, zan_ff_rational_t cq);

    /* libavcodec */
    unsigned (*avcodec_version)(void);
    void *(*avcodec_alloc_context3)(const void *codec);
    void (*avcodec_free_context)(void **avctx);
    int (*avcodec_parameters_to_context)(void *codec, const void *par);
    int (*avcodec_open2)(void *avctx, const void *codec, void **options);
    int (*avcodec_send_packet)(void *avctx, const void *avpkt);
    int (*avcodec_receive_frame)(void *avctx, void *frame);
    void (*avcodec_flush_buffers)(void *avctx);
    const char *(*avcodec_get_name)(int id);
    void *(*av_packet_alloc)(void);
    void (*av_packet_free)(void **pkt);
    void (*av_packet_unref)(void *pkt);

    /* libavformat */
    unsigned (*avformat_version)(void);
    int (*avformat_open_input)(void **ps, const char *url, const void *fmt, void **options);
    int (*avformat_find_stream_info)(void *ic, void **options);
    void (*avformat_close_input)(void **s);
    int (*av_find_best_stream)(void *ic, int type, int wanted, int related,
                               const void **decoder_ret, int flags);
    int (*av_read_frame)(void *s, void *pkt);
    int (*av_seek_frame)(void *s, int stream_index, int64_t timestamp, int flags);

    /* libswscale */
    void *(*sws_getCachedContext)(void *ctx, int srcW, int srcH, int srcFormat,
                                  int dstW, int dstH, int dstFormat,
                                  int flags, void *srcFilter, void *dstFilter,
                                  const double *param);
    int (*sws_scale)(void *c, const uint8_t *const srcSlice[],
                     const int srcStride[], int srcSliceY, int srcSliceH,
                     uint8_t *const dst[], const int dstStride[]);
    void (*sws_freeContext)(void *swsContext);

    /* libswresample */
    int (*swr_alloc_set_opts2)(void **ps,
                               const zan_ff_ch_layout_t *out_ch_layout, int out_sample_fmt, int out_sample_rate,
                               const zan_ff_ch_layout_t *in_ch_layout, int in_sample_fmt, int in_sample_rate,
                               int log_offset, void *log_ctx);
    int (*swr_init)(void *s);
    int (*swr_convert)(void *s, uint8_t **out, int out_count,
                       const uint8_t **in, int in_count);
    void (*swr_free)(void **s);
    int64_t (*swr_get_delay)(void *s, int64_t base);
} zan_ff_api_t;

static zan_ff_api_t g_ff;
static char g_vid_err[256];

static void zan_vid_set_err(const char *msg) {
    if (!msg) { g_vid_err[0] = 0; return; }
    size_t n = strlen(msg);
    if (n >= sizeof(g_vid_err)) n = sizeof(g_vid_err) - 1;
    memcpy(g_vid_err, msg, n);
    g_vid_err[n] = 0;
}

#ifdef _WIN32
static CRITICAL_SECTION g_ff_cs;
static INIT_ONCE g_ff_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK zan_ff_cs_init(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once; (void)param; (void)ctx;
    InitializeCriticalSection(&g_ff_cs);
    return TRUE;
}
static void zan_ff_lock(void) {
    InitOnceExecuteOnce(&g_ff_once, zan_ff_cs_init, NULL, NULL);
    EnterCriticalSection(&g_ff_cs);
}
static void zan_ff_unlock(void) { LeaveCriticalSection(&g_ff_cs); }
static void *zan_dl_sym(HMODULE m, const char *name) { return (void *)GetProcAddress(m, name); }
static void zan_dl_close(HMODULE m) { if (m) FreeLibrary(m); }
#else
static pthread_mutex_t g_ff_mutex = PTHREAD_MUTEX_INITIALIZER;
static void zan_ff_lock(void) { pthread_mutex_lock(&g_ff_mutex); }
static void zan_ff_unlock(void) { pthread_mutex_unlock(&g_ff_mutex); }
static void *zan_dl_sym(void *m, const char *name) { return dlsym(m, name); }
static void zan_dl_close(void *m) { if (m) dlclose(m); }
#endif

static void zan_ff_unload_locked(void) {
    zan_dl_close(g_ff.h_swresample); g_ff.h_swresample = NULL;
    zan_dl_close(g_ff.h_swscale);    g_ff.h_swscale = NULL;
    zan_dl_close(g_ff.h_avformat);   g_ff.h_avformat = NULL;
    zan_dl_close(g_ff.h_avcodec);    g_ff.h_avcodec = NULL;
    zan_dl_close(g_ff.h_avutil);     g_ff.h_avutil = NULL;
    g_ff.loaded = 0;
}

typedef struct zan_ff_ver_tuple {
    const char *avutil;
    const char *avcodec;
    const char *avformat;
    const char *swscale;
    const char *swresample;
} zan_ff_ver_tuple_t;

/* FFmpeg 8 (62/62/60/9/6), FFmpeg 7 (61/61/59/8/5), FFmpeg 6 (60/60/58/7/4) */
static const zan_ff_ver_tuple_t g_ff_tuples[] = {
    { "60", "62", "62", "9", "6" },
    { "59", "61", "61", "8", "5" },
    { "58", "60", "60", "7", "4" },
};

static int zan_ff_try_load_set(const char *dir, const zan_ff_ver_tuple_t *t) {
    char p_u[640], p_c[640], p_f[640], p_s[640], p_r[640];
#ifdef _WIN32
    if (dir && dir[0]) {
        SetDllDirectoryA(dir);
        snprintf(p_u, sizeof(p_u), "%s\\avutil-%s.dll", dir, t->avutil);
        snprintf(p_c, sizeof(p_c), "%s\\avcodec-%s.dll", dir, t->avcodec);
        snprintf(p_f, sizeof(p_f), "%s\\avformat-%s.dll", dir, t->avformat);
        snprintf(p_s, sizeof(p_s), "%s\\swscale-%s.dll", dir, t->swscale);
        snprintf(p_r, sizeof(p_r), "%s\\swresample-%s.dll", dir, t->swresample);
    } else {
        snprintf(p_u, sizeof(p_u), "avutil-%s.dll", t->avutil);
        snprintf(p_c, sizeof(p_c), "avcodec-%s.dll", t->avcodec);
        snprintf(p_f, sizeof(p_f), "avformat-%s.dll", t->avformat);
        snprintf(p_s, sizeof(p_s), "swscale-%s.dll", t->swscale);
        snprintf(p_r, sizeof(p_r), "swresample-%s.dll", t->swresample);
    }
    HMODULE hu = LoadLibraryA(p_u);
    HMODULE hc = hu ? LoadLibraryA(p_c) : NULL;
    HMODULE hf = hc ? LoadLibraryA(p_f) : NULL;
    HMODULE hs = hf ? LoadLibraryA(p_s) : NULL;
    HMODULE hr = hs ? LoadLibraryA(p_r) : NULL;
    if (dir && dir[0]) SetDllDirectoryA(NULL);
    if (!hu || !hc || !hf || !hs || !hr) {
        zan_dl_close(hr); zan_dl_close(hs); zan_dl_close(hf); zan_dl_close(hc); zan_dl_close(hu);
        return 0;
    }
#elif defined(__APPLE__)
    const char *pfx = (dir && dir[0]) ? dir : "";
    const char *sep = (dir && dir[0]) ? "/" : "";
    snprintf(p_u, sizeof(p_u), "%s%slibavutil.%s.dylib", pfx, sep, t->avutil);
    snprintf(p_c, sizeof(p_c), "%s%slibavcodec.%s.dylib", pfx, sep, t->avcodec);
    snprintf(p_f, sizeof(p_f), "%s%slibavformat.%s.dylib", pfx, sep, t->avformat);
    snprintf(p_s, sizeof(p_s), "%s%slibswscale.%s.dylib", pfx, sep, t->swscale);
    snprintf(p_r, sizeof(p_r), "%s%slibswresample.%s.dylib", pfx, sep, t->swresample);
    void *hu = dlopen(p_u, RTLD_LAZY | RTLD_LOCAL);
    void *hc = hu ? dlopen(p_c, RTLD_LAZY | RTLD_LOCAL) : NULL;
    void *hf = hc ? dlopen(p_f, RTLD_LAZY | RTLD_LOCAL) : NULL;
    void *hs = hf ? dlopen(p_s, RTLD_LAZY | RTLD_LOCAL) : NULL;
    void *hr = hs ? dlopen(p_r, RTLD_LAZY | RTLD_LOCAL) : NULL;
    if (!hu || !hc || !hf || !hs || !hr) {
        zan_dl_close(hr); zan_dl_close(hs); zan_dl_close(hf); zan_dl_close(hc); zan_dl_close(hu);
        return 0;
    }
#else
    const char *pfx = (dir && dir[0]) ? dir : "";
    const char *sep = (dir && dir[0]) ? "/" : "";
    snprintf(p_u, sizeof(p_u), "%s%slibavutil.so.%s", pfx, sep, t->avutil);
    snprintf(p_c, sizeof(p_c), "%s%slibavcodec.so.%s", pfx, sep, t->avcodec);
    snprintf(p_f, sizeof(p_f), "%s%slibavformat.so.%s", pfx, sep, t->avformat);
    snprintf(p_s, sizeof(p_s), "%s%slibswscale.so.%s", pfx, sep, t->swscale);
    snprintf(p_r, sizeof(p_r), "%s%slibswresample.so.%s", pfx, sep, t->swresample);
    void *hu = dlopen(p_u, RTLD_LAZY | RTLD_LOCAL);
    void *hc = hu ? dlopen(p_c, RTLD_LAZY | RTLD_LOCAL) : NULL;
    void *hf = hc ? dlopen(p_f, RTLD_LAZY | RTLD_LOCAL) : NULL;
    void *hs = hf ? dlopen(p_s, RTLD_LAZY | RTLD_LOCAL) : NULL;
    void *hr = hs ? dlopen(p_r, RTLD_LAZY | RTLD_LOCAL) : NULL;
    if (!hu || !hc || !hf || !hs || !hr) {
        zan_dl_close(hr); zan_dl_close(hs); zan_dl_close(hf); zan_dl_close(hc); zan_dl_close(hu);
        return 0;
    }
#endif

    g_ff.h_avutil = hu;
    g_ff.h_avcodec = hc;
    g_ff.h_avformat = hf;
    g_ff.h_swscale = hs;
    g_ff.h_swresample = hr;

#define ZAN_LOAD_SYM(mod, field) \
    do { \
        *(void **)&g_ff.field = zan_dl_sym(mod, #field); \
        if (!g_ff.field) { zan_ff_unload_locked(); return 0; } \
    } while (0)

    ZAN_LOAD_SYM(hu, avutil_version);
    ZAN_LOAD_SYM(hu, av_frame_alloc);
    ZAN_LOAD_SYM(hu, av_frame_free);
    ZAN_LOAD_SYM(hu, av_frame_unref);
    ZAN_LOAD_SYM(hu, av_log_set_level);
    ZAN_LOAD_SYM(hu, av_channel_layout_default);
    ZAN_LOAD_SYM(hu, av_channel_layout_copy);
    ZAN_LOAD_SYM(hu, av_channel_layout_uninit);
    ZAN_LOAD_SYM(hu, av_rescale_q);

    ZAN_LOAD_SYM(hc, avcodec_version);
    ZAN_LOAD_SYM(hc, avcodec_alloc_context3);
    ZAN_LOAD_SYM(hc, avcodec_free_context);
    ZAN_LOAD_SYM(hc, avcodec_parameters_to_context);
    ZAN_LOAD_SYM(hc, avcodec_open2);
    ZAN_LOAD_SYM(hc, avcodec_send_packet);
    ZAN_LOAD_SYM(hc, avcodec_receive_frame);
    ZAN_LOAD_SYM(hc, avcodec_flush_buffers);
    ZAN_LOAD_SYM(hc, avcodec_get_name);
    ZAN_LOAD_SYM(hc, av_packet_alloc);
    ZAN_LOAD_SYM(hc, av_packet_free);
    ZAN_LOAD_SYM(hc, av_packet_unref);

    ZAN_LOAD_SYM(hf, avformat_version);
    ZAN_LOAD_SYM(hf, avformat_open_input);
    ZAN_LOAD_SYM(hf, avformat_find_stream_info);
    ZAN_LOAD_SYM(hf, avformat_close_input);
    ZAN_LOAD_SYM(hf, av_find_best_stream);
    ZAN_LOAD_SYM(hf, av_read_frame);
    ZAN_LOAD_SYM(hf, av_seek_frame);

    ZAN_LOAD_SYM(hs, sws_getCachedContext);
    ZAN_LOAD_SYM(hs, sws_scale);
    ZAN_LOAD_SYM(hs, sws_freeContext);

    ZAN_LOAD_SYM(hr, swr_alloc_set_opts2);
    ZAN_LOAD_SYM(hr, swr_init);
    ZAN_LOAD_SYM(hr, swr_convert);
    ZAN_LOAD_SYM(hr, swr_free);
    ZAN_LOAD_SYM(hr, swr_get_delay);
#undef ZAN_LOAD_SYM

    unsigned vf = g_ff.avformat_version();
    unsigned vc = g_ff.avcodec_version();
    unsigned vu = g_ff.avutil_version();
    g_ff.avformat_major = vf >> 16;
    g_ff.avcodec_major = vc >> 16;
    g_ff.avutil_major = vu >> 16;
    snprintf(g_ff.version_str, sizeof(g_ff.version_str),
             "libavformat %u.%u.%u / libavcodec %u.%u.%u / libavutil %u.%u.%u",
             vf >> 16, (vf >> 8) & 255, vf & 255,
             vc >> 16, (vc >> 8) & 255, vc & 255,
             vu >> 16, (vu >> 8) & 255, vu & 255);
    /* 静音 FFmpeg stderr 刷屏，仅保留致命错误 */
    g_ff.av_log_set_level(16);
    g_ff.loaded = 1;
    return 1;
}

static int zan_ff_try_dir(const char *dir) {
    size_t n = sizeof(g_ff_tuples) / sizeof(g_ff_tuples[0]);
    for (size_t i = 0; i < n; i++) {
        if (zan_ff_try_load_set(dir, &g_ff_tuples[i])) return 1;
    }
    return 0;
}

static int zan_ff_ensure_loaded(void) {
    zan_ff_lock();
    if (g_ff.loaded) { zan_ff_unlock(); return 1; }
    if (g_ff.tried) { zan_ff_unlock(); return 0; }
    g_ff.tried = 1;

    /* 1. 显式配置的目录 */
    if (g_ff.custom_dir[0] && zan_ff_try_dir(g_ff.custom_dir)) {
        zan_ff_unlock(); return 1;
    }

    /* 2. 环境变量 ZAN_FFMPEG_DIR */
    const char *env_dir = getenv("ZAN_FFMPEG_DIR");
    if (env_dir && env_dir[0] && zan_ff_try_dir(env_dir)) {
        zan_ff_unlock(); return 1;
    }

    /* 3. 可执行文件同目录及其 ffmpeg/ 子目录 */
#ifdef _WIN32
    {
        char exe_path[512];
        DWORD len = GetModuleFileNameA(NULL, exe_path, (DWORD)sizeof(exe_path) - 1);
        if (len > 0 && len < sizeof(exe_path)) {
            exe_path[len] = 0;
            char *slash = strrchr(exe_path, '\\');
            if (!slash) slash = strrchr(exe_path, '/');
            if (slash) {
                *slash = 0;
                if (zan_ff_try_dir(exe_path)) { zan_ff_unlock(); return 1; }
                char sub[640];
                snprintf(sub, sizeof(sub), "%s\\ffmpeg", exe_path);
                if (zan_ff_try_dir(sub)) { zan_ff_unlock(); return 1; }
            }
        }
    }
#endif

    /* 4. 系统默认库搜索路径 (PATH / LD_LIBRARY_PATH / DYLD) */
    if (zan_ff_try_dir(NULL)) { zan_ff_unlock(); return 1; }

    /* 5. 常见系统安装位置兜底 */
#ifdef _WIN32
    static const char *win_dirs[] = {
        "C:\\ffmpeg\\bin",
        "C:\\Program Files\\ffmpeg\\bin",
        "C:\\Program Files\\FFmpeg\\bin",
        "C:\\Program Files\\ReArk",
        NULL
    };
    for (int i = 0; win_dirs[i]; i++) {
        if (zan_ff_try_dir(win_dirs[i])) { zan_ff_unlock(); return 1; }
    }
#elif defined(__APPLE__)
    static const char *mac_dirs[] = {
        "/opt/homebrew/lib",
        "/usr/local/lib",
        NULL
    };
    for (int i = 0; mac_dirs[i]; i++) {
        if (zan_ff_try_dir(mac_dirs[i])) { zan_ff_unlock(); return 1; }
    }
#else
    static const char *lin_dirs[] = {
        "/usr/lib/x86_64-linux-gnu",
        "/usr/lib/aarch64-linux-gnu",
        "/usr/lib64",
        "/usr/local/lib",
        NULL
    };
    for (int i = 0; lin_dirs[i]; i++) {
        if (zan_ff_try_dir(lin_dirs[i])) { zan_ff_unlock(); return 1; }
    }
#endif

    zan_vid_set_err("FFmpeg shared libraries (libavformat/libavcodec/libavutil/libswscale/libswresample 6/7/8) not found; set ZAN_FFMPEG_DIR or call zan_video_set_ffmpeg_dir");
    zan_ff_unlock();
    return 0;
}

/* 版本感知的结构体字段访问辅助函数 */
static inline uint32_t zan_ff_fmt_nb_streams(const void *fmt) {
    return *(const uint32_t *)((const uint8_t *)fmt + 44);
}
static inline void **zan_ff_fmt_streams(const void *fmt) {
    return *(void **const *)((const uint8_t *)fmt + 48);
}
static inline int64_t zan_ff_fmt_duration_us(const void *fmt) {
    return *(const int64_t *)((const uint8_t *)fmt + 104);
}
static inline int64_t zan_ff_fmt_bitrate(const void *fmt) {
    return *(const int64_t *)((const uint8_t *)fmt + 112);
}

static inline void *zan_ff_stream_codecpar(const void *st) {
    return *(void *const *)((const uint8_t *)st + 16);
}
static inline zan_ff_rational_t zan_ff_stream_time_base(const void *st) {
    zan_ff_rational_t r;
    r.num = *(const int32_t *)((const uint8_t *)st + 32);
    r.den = *(const int32_t *)((const uint8_t *)st + 36);
    return r;
}
static inline int64_t zan_ff_stream_nb_frames(const void *st) {
    return *(const int64_t *)((const uint8_t *)st + 56);
}
static inline zan_ff_rational_t zan_ff_stream_avg_fps(const void *st) {
    zan_ff_rational_t r;
    r.num = *(const int32_t *)((const uint8_t *)st + 88);
    r.den = *(const int32_t *)((const uint8_t *)st + 92);
    return r;
}

static inline int32_t zan_ff_cp_codec_id(const void *cp) {
    return *(const int32_t *)((const uint8_t *)cp + 4);
}
static inline int32_t zan_ff_cp_format(const void *cp) {
    return *(const int32_t *)((const uint8_t *)cp + 28);
}
static inline int32_t zan_ff_cp_width(const void *cp) {
    size_t off = (g_ff.avcodec_major >= 61) ? 72 : 56;
    return *(const int32_t *)((const uint8_t *)cp + off);
}
static inline int32_t zan_ff_cp_height(const void *cp) {
    size_t off = (g_ff.avcodec_major >= 61) ? 76 : 60;
    return *(const int32_t *)((const uint8_t *)cp + off);
}
static inline const zan_ff_ch_layout_t *zan_ff_cp_ch_layout(const void *cp) {
    size_t off = (g_ff.avcodec_major >= 61) ? 128 : 112;
    return (const zan_ff_ch_layout_t *)((const uint8_t *)cp + off);
}
static inline int32_t zan_ff_cp_sample_rate(const void *cp) {
    size_t off = (g_ff.avcodec_major >= 61) ? 152 : 136;
    return *(const int32_t *)((const uint8_t *)cp + off);
}

static inline int32_t zan_ff_pkt_stream_index(const void *pkt) {
    return *(const int32_t *)((const uint8_t *)pkt + 36);
}

static inline uint8_t *const *zan_ff_frame_data(const void *f) {
    return (uint8_t *const *)((const uint8_t *)f + 0);
}
static inline const int32_t *zan_ff_frame_linesize(const void *f) {
    return (const int32_t *)((const uint8_t *)f + 64);
}
static inline int32_t zan_ff_frame_width(const void *f) {
    return *(const int32_t *)((const uint8_t *)f + 104);
}
static inline int32_t zan_ff_frame_height(const void *f) {
    return *(const int32_t *)((const uint8_t *)f + 108);
}
static inline int32_t zan_ff_frame_nb_samples(const void *f) {
    return *(const int32_t *)((const uint8_t *)f + 112);
}
static inline int32_t zan_ff_frame_format(const void *f) {
    return *(const int32_t *)((const uint8_t *)f + 116);
}
static inline int64_t zan_ff_frame_pts(const void *f) {
    return *(const int64_t *)((const uint8_t *)f + 136);
}
