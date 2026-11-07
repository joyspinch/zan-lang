/* rt_timer_diag.c: 跨平台参数转换、软失败日志记录与致命错误处理 */

int zan_utf8_argv(int *argc, char ***argv) {
#if defined(_WIN32)
    if (!argc || !argv) return 0;

    int wide_argc = 0;
    LPWSTR *wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
    if (!wide_argv || wide_argc < 1) return 0;

    char **utf8_argv = (char **)calloc((size_t)wide_argc + 1, sizeof(*utf8_argv));
    if (!utf8_argv) {
        LocalFree(wide_argv);
        return 0;
    }

    for (int i = 0; i < wide_argc; i++) {
        int bytes = WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1,
                                        NULL, 0, NULL, NULL);
        if (bytes <= 0) {
            for (int j = 0; j < i; j++) free(utf8_argv[j]);
            free(utf8_argv);
            LocalFree(wide_argv);
            return 0;
        }
        utf8_argv[i] = (char *)malloc((size_t)bytes);
        if (!utf8_argv[i]) {
            for (int j = 0; j < i; j++) free(utf8_argv[j]);
            free(utf8_argv);
            LocalFree(wide_argv);
            return 0;
        }
        if (WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1,
                                utf8_argv[i], bytes, NULL, NULL) != bytes) {
            free(utf8_argv[i]);
            for (int j = 0; j < i; j++) free(utf8_argv[j]);
            free(utf8_argv);
            LocalFree(wide_argv);
            return 0;
        }
    }

    LocalFree(wide_argv);
    *argc = wide_argc;
    *argv = utf8_argv;
    return 1;
#else
    (void)argc;
    (void)argv;
    return 0;
#endif
}


#define ZAN_SOFT_MAX_SITES 1024

static char *g_soft_seen[ZAN_SOFT_MAX_SITES];
static int g_soft_seen_count;
static int g_soft_strict;

static int zan_soft_is_hard(void) {
    timer_lock();
    static int hard = -1;
    if (hard < 0) {
        const char *env = getenv("ZAN_RT_HARD");
        hard = (env && *env && *env != '0') ? 1 : 0;
        if (!hard && !env) hard = g_soft_strict;
    }
    int v = hard;
    timer_unlock();
    return v;
}

int zan_rt_soft_is_hard(void) { return zan_soft_is_hard(); }

void zan_rt_set_strict(void) { g_soft_strict = 1; }

#define ZAN_SOFT_SCRATCH_RC  (UINT64_C(1) << 62)
#define ZAN_SOFT_SCRATCH_PAYLOAD 4096
static union {
    unsigned char bytes[40 + ZAN_SOFT_SCRATCH_PAYLOAD];
    long double align_it;
} g_soft_scratch_store;
static unsigned char *g_soft_scratch;

static void store_u64_le(unsigned char *p, uint64_t v) {
    for (int i = 0; i < 8; i++) { p[i] = (unsigned char)(v >> (8 * i)); }
}

unsigned char *zan_rt_soft_scratch(void) {
    timer_lock();
    if (!g_soft_scratch) {
        unsigned char *hdr = g_soft_scratch_store.bytes;
        memset(hdr, 0, sizeof(g_soft_scratch_store.bytes));
        store_u64_le(hdr + 0,  ZAN_SOFT_SCRATCH_RC);    /* P-32: array rc */
        store_u64_le(hdr + 8,  ZAN_ARRAY_RC_MAGIC);
        store_u64_le(hdr + 16, ZAN_SOFT_SCRATCH_RC);    /* P-16: object rc */
        store_u64_le(hdr + 24, ZAN_ARRAY_MAGIC);        /* P-8:  discriminator */
        g_soft_scratch = hdr + 32;
    }
    unsigned char *scratch = g_soft_scratch;
    timer_unlock();
    return scratch;
}

static int zan_soft_seen(const char *text) {
    for (int i = 0; i < g_soft_seen_count; i++)
        if (g_soft_seen[i] == text) return 1;
    if (g_soft_seen_count < ZAN_SOFT_MAX_SITES)
        g_soft_seen[g_soft_seen_count++] = (char *)text;
    return 0;
}

static struct {
    const char *file;
    unsigned line;
    unsigned col;
} g_soft_seen3[ZAN_SOFT_MAX_SITES];
static int g_soft_seen3_count;

static int zan_soft_seen3(const char *file, unsigned line, unsigned col) {
    for (int i = 0; i < g_soft_seen3_count; i++)
        if (g_soft_seen3[i].file == file && g_soft_seen3[i].line == line &&
            g_soft_seen3[i].col == col) return 1;
    if (g_soft_seen3_count < ZAN_SOFT_MAX_SITES) {
        g_soft_seen3[g_soft_seen3_count].file = file;
        g_soft_seen3[g_soft_seen3_count].line = line;
        g_soft_seen3[g_soft_seen3_count].col = col;
        g_soft_seen3_count++;
    }
    return 0;
}

#if defined(_WIN32)
static void zan_soft_log_path(char *path, size_t cap) {
    DWORD n = GetModuleFileNameA(NULL, path, (DWORD)cap);
    if (n == 0 || n >= cap) { snprintf(path, cap, "zan_soft.log"); return; }
    char *slash = strrchr(path, '\\');
    if (!slash) { snprintf(path, cap, "zan_soft.log"); return; }
    slash[1] = '\0';
    strncat(path, "zan_crash.log", cap - strlen(path) - 1);
}
#else
static char g_soft_logdir[4096];

static void zan_soft_init_logdir(void) {
    const char *env = getenv("ZAN_LOG_DIR");
    if (env && *env && strlen(env) < sizeof(g_soft_logdir)) {
        memcpy(g_soft_logdir, env, strlen(env) + 1);
        return;
    }
    memcpy(g_soft_logdir, "logs", 5);
    ssize_t n = readlink("/proc/self/exe", g_soft_logdir,
                         sizeof(g_soft_logdir) - 6);
    if (n <= 0) return;
    g_soft_logdir[n] = '\0';
    char *slash = strrchr(g_soft_logdir, '/');
    if (!slash) { memcpy(g_soft_logdir, "logs", 5); return; }
    slash[1] = '\0';
    strncat(g_soft_logdir, "logs", sizeof(g_soft_logdir) - strlen(g_soft_logdir) - 1);
}

/* <logdir>/<YYYYMM>/<DD> */
static void zan_soft_log_path(char *path, size_t cap) {
    if (!g_soft_logdir[0]) zan_soft_init_logdir();
    time_t now = time(NULL);
    struct tm lt;
    if (!localtime_r(&now, &lt)) { snprintf(path, cap, "zan_soft.log"); return; }
    char month[16], day[16];
    strftime(month, sizeof month, "%Y%m", &lt);
    strftime(day, sizeof day, "%d", &lt);
    char dir[4096];
    if ((size_t)snprintf(dir, sizeof dir, "%s/%s", g_soft_logdir, month)
            >= sizeof dir) { snprintf(path, cap, "zan_soft.log"); return; }
    mkdir(g_soft_logdir, 0755);
    mkdir(dir, 0755);
    snprintf(path, cap, "%s/%s.log", dir, day);
}
#endif

static void zan_soft_append(const char *text) {
    char path[4096];
    zan_soft_log_path(path, sizeof path);
    FILE *f = fopen(path, "ab");
    if (!f) {
#if defined(_WIN32)
        /* exe 目录只读（Program Files / 部署盘）时的最后一档：用户临时
         * 目录。软诊断宁可落进 %TEMP% 也不要静默丢（审计 P1-2）。 */
        char tmp[MAX_PATH];
        DWORD tn = GetTempPathA((DWORD)sizeof tmp, tmp);
        if (tn > 0 && tn < sizeof tmp) {
            snprintf(path, sizeof path, "%szan_crash.log", tmp);
            f = fopen(path, "ab");
        }
#endif
    }
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size == 0) {
#if defined(_WIN32)
        char exe[MAX_PATH];
        DWORD en = GetModuleFileNameA(NULL, exe, sizeof exe);
        if (en == 0 || en >= sizeof exe) snprintf(exe, sizeof exe, "<unknown>");
        fprintf(f, "==== ZAN RUNTIME (soft) ====\nexe=%s\n", exe);
#else
        char exe[4096];
        ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n > 0) exe[n] = '\0'; else snprintf(exe, sizeof exe, "<unknown>");
        fprintf(f, "==== ZAN RUNTIME (soft) ====\nexe=%s\n", exe);
#endif
    }
#if defined(_WIN32)
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d.%03d soft runtime error: %s",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds, text);
#else
    char stamp[32];
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &lt);
    fprintf(f, "%s soft runtime error: %s", stamp, text);
#endif
    fclose(f);
}

void zan_rt_soft_note(const char *text) {
    if (!text) return;
    timer_lock();
    int seen = zan_soft_seen(text);
    timer_unlock();
    if (seen) return;
    fprintf(stderr, "%s", text);
    fflush(stderr);
    zan_soft_append(text);
}

void zan_rt_soft_note2(const char *prefix, const char *msg) {
    if (!prefix) return;
    char buf[1400];
    timer_lock();
    int seen = zan_soft_seen(prefix);
    if (!seen) {
        size_t n = strlen(prefix);
        if (n >= sizeof buf) n = sizeof buf - 1;
        memcpy(buf, prefix, n);
        if (msg) {
            size_t m = strlen(msg);
            if (n + m >= sizeof buf) m = sizeof buf - 1 - n;
            memcpy(buf + n, msg, m);
            n += m;
        }
        buf[n] = '\0';
    }
    timer_unlock();
    if (seen) return;
    fprintf(stderr, "%s", buf);
    fflush(stderr);
    zan_soft_append(buf);
}

void zan_rt_soft_note3(const char *file, unsigned line, unsigned col,
                       const char *msg) {
    char buf[1400];
    int n = snprintf(buf, sizeof buf, "%s:%u:%u: runtime error: %s",
                     file ? file : "<unknown>", line, col,
                     msg ? msg : "");
    if (n <= 0) return;
    if ((size_t)n >= sizeof buf - 1) n = (int)sizeof buf - 2;
    buf[n] = '\n';
    buf[n + 1] = '\0';
    timer_lock();
    int seen = zan_soft_seen3(file, line, col);
    timer_unlock();
    if (seen) return;
    fprintf(stderr, "%s", buf);
    fflush(stderr);
    zan_soft_append(buf);
}

void zan_rt_guard_fail2(const char *prefix, const char *msg) {
    if (zan_soft_is_hard()) {
        char buf[1400];
        size_t n = prefix ? strlen(prefix) : 0;
        if (n >= sizeof buf) n = sizeof buf - 1;
        memcpy(buf, prefix ? prefix : "", n);
        if (msg) {
            size_t m = strlen(msg);
            if (n + m >= sizeof buf) m = sizeof buf - 1 - n;
            memcpy(buf + n, msg, m);
            n += m;
        }
        buf[n] = '\0';
        fprintf(stderr, "%s", buf);
        fflush(stderr);
#if defined(_WIN32)
        void (WINAPI *raise)(DWORD, DWORD, DWORD, const ULONG_PTR *) =
            RaiseException;
        unsigned long code = 0xE0A2C010u; 
        ULONG_PTR args[2] = { (ULONG_PTR)buf, 70 };
        raise(code, 0, 2, args);
#endif
        exit(70);
    }
    zan_rt_soft_note2(prefix, msg);
}

void zan_rt_guard_fail3(const char *file, unsigned line, unsigned col,
                        const char *msg) {
    if (zan_soft_is_hard()) {
#if defined(_WIN32)
        char buf[1400];
        snprintf(buf, sizeof buf, "%s:%u:%u: runtime error: %s",
                 file ? file : "<unknown>", line, col, msg ? msg : "");
        size_t n = strlen(buf);
        if (n && buf[n - 1] != '\n' && n + 1 < sizeof buf) {
            buf[n] = '\n';
            buf[n + 1] = '\0';
        }
        fprintf(stderr, "%s", buf);
        fflush(stderr);
        void (WINAPI *raise)(DWORD, DWORD, DWORD, const ULONG_PTR *) =
            RaiseException;
        unsigned long code = 0xE0A2C010u; 
        ULONG_PTR args[2] = { (ULONG_PTR)buf, 70 };
        raise(code, 0, 2, args);
#endif
        exit(70);
    }
    zan_rt_soft_note3(file, line, col, msg);
}

static zan_fatal_fn g_fatal_handler;

void zan_rt_set_fatal_handler(zan_fatal_fn fn) { g_fatal_handler = fn; }

void zan_rt_fatal(const char *category, const char *message) {
    fprintf(stderr, "zan runtime: fatal (%s): %s\n",
            category ? category : "runtime", message ? message : "");
    fflush(stderr);
    zan_fatal_fn fn = g_fatal_handler;
    if (fn) fn(category, message);
    abort();
}
