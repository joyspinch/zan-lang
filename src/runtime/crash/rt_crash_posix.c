/* rt_crash_posix.c -- POSIX 信号安全捕获、栈回溯解析与日志写入 */

#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#if (defined(__GLIBC__) || defined(__APPLE__)) && !defined(ZAN_NO_EXECINFO)
#include <execinfo.h>
#define ZAN_CRASH_HAVE_BACKTRACE 1
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
/* dladdr 符号化解析（musl/BSD 无 execinfo 兜底） */
#include <dlfcn.h>

static void zan__crash_wr(int fd, const char *s) {
    size_t n = strlen(s);
    while (n) {
        ssize_t w = write(fd, s, n);
        /* 管道阻塞时安全丢弃剩余日志，防止死锁 */
        if (w <= 0) return;
        s += (size_t)w;
        n -= (size_t)w;
    }
}

static const char *zan__crash_dec(unsigned long long v, char *buf) {
    char *p = buf + 23;
    *p = '\0';
    do { *--p = (char)('0' + (v % 10)); v /= 10; } while (v);
    return p;
}

static const char *zan__crash_hex(unsigned long long v, char *buf) {
    static const char d[] = "0123456789abcdef";
    char *p = buf + 23;
    *p = '\0';
    do { *--p = d[v & 0xF]; v >>= 4; } while (v);
    *--p = 'x';
    *--p = '0';
    return p;
}

#if defined(_GNU_SOURCE)
#define ZAN_CRASH_HAVE_DLADDR 1
#endif

static void zan__crash_frame(int fd, void *pc) {
    char num[24];
    zan__crash_wr(fd, "  # ");
#if defined(ZAN_CRASH_HAVE_DLADDR)
    Dl_info di;
    if (dladdr(pc, &di) && di.dli_fname) {
        zan__crash_wr(fd, di.dli_fname);
        zan__crash_wr(fd, "+0x");
        zan__crash_wr(fd, zan__crash_hex(
            (unsigned long long)((char *)pc - (char *)di.dli_fbase), num));
        if (di.dli_sname) {
            zan__crash_wr(fd, " (");
            zan__crash_wr(fd, di.dli_sname);
            zan__crash_wr(fd, ")");
        }
    } else {
        zan__crash_wr(fd, zan__crash_hex((unsigned long long)(size_t)pc, num));
    }
#else
    zan__crash_wr(fd, zan__crash_hex((unsigned long long)(size_t)pc, num));
#endif
    zan__crash_wr(fd, "\n");
}

static int zan__crash_fp_walk(int fd, void **fp) {
    int n = 0;
    while (fp && n < 48) {
        void **next = (void **)fp[0];
        void *ret = fp[1];
        if (!ret) break;
        zan__crash_frame(fd, ret);
        n++;
        if ((size_t)next & (sizeof(void *) - 1)) break;
        if ((size_t)next <= (size_t)fp) break;
        if ((size_t)next - (size_t)fp > (1u << 20)) break;
        fp = next;
    }
    return n;
}

static const char *zan__crash_signame(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS:  return "SIGBUS";
    case SIGFPE:  return "SIGFPE";
    case SIGILL:  return "SIGILL";
    case SIGABRT: return "SIGABRT";
    default:      return "signal";
    }
}

static char zan__crash_exe[4096];
static char zan__crash_logdir[4096];
static char zan__crash_wid[32];

static long zan__crash_tz_off;
static int zan__crash_err_fd = -1;

static void zan__crash_cache_handler_state(void) {
    time_t now = time(NULL);
    struct tm g, l;
    if (gmtime_r(&now, &g) && localtime_r(&now, &l)) {
        long day_diff = (long)l.tm_yday - (long)g.tm_yday;
        if (l.tm_year > g.tm_year) day_diff = 1;
        else if (l.tm_year < g.tm_year) day_diff = -1;
        zan__crash_tz_off =
            day_diff * 86400L +
            ((long)(l.tm_hour) - (long)(g.tm_hour)) * 3600L +
            ((long)(l.tm_min) - (long)(g.tm_min)) * 60L +
            ((long)(l.tm_sec) - (long)(g.tm_sec));
    }
#if defined(F_DUPFD_CLOEXEC)
    int fd = fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 100);
#else
    int fd = fcntl(STDERR_FILENO, F_DUPFD, 100);
#endif
    if (fd >= 0) {
        int fl = fcntl(fd, F_GETFL);
        if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK); /* best-effort */
        zan__crash_err_fd = fd;
    }
#if defined(ZAN_CRASH_HAVE_BACKTRACE)
    {
        void *warm[2];
        backtrace(warm, 2);
    }
#endif
}

/* 纯整数公历日期计算（信号安全） */
static void zan__crash_civil(long long z, int *y, unsigned *m, unsigned *d) {
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long yy = (long long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    *m = (unsigned)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yy + (*m <= 2));
}

static char *zan__crash_put2(char *p, unsigned v) {
    p[0] = (char)('0' + (v / 10) % 10);
    p[1] = (char)('0' + v % 10);
    return p + 2;
}

/* Startup only: create the base before any signal can need it. */
static int zan__crash_prepare_dir(char *dir) {
    if (!*dir) return 0;
    for (size_t i = 1; dir[i]; ++i) {
        if (dir[i] != '/') continue;
        dir[i] = 0;
        int made = mkdir(dir, 0700), err = errno;
        dir[i] = '/';
        if (made && err != EEXIST) return 0;
    }
    if (mkdir(dir, 0700) && errno != EEXIST) return 0;
    struct stat st;
    return stat(dir, &st) == 0 && S_ISDIR(st.st_mode) && access(dir, W_OK) == 0;
}

static void zan__crash_resolve_paths(void) {
    ssize_t n = -1;
#if defined(__linux__)
    n = readlink("/proc/self/exe", zan__crash_exe, sizeof(zan__crash_exe) - 1);
#elif defined(__APPLE__)
    uint32_t cap = (uint32_t)(sizeof(zan__crash_exe) - 1);
    if (_NSGetExecutablePath(zan__crash_exe, &cap) == 0) {
        n = (ssize_t)strlen(zan__crash_exe);
    }
#endif
    if (n <= 0) {
        memcpy(zan__crash_exe, "<unknown>", 10);
        n = 0;
    } else {
        zan__crash_exe[n] = '\0';
    }

    const char *wid = getenv("ZAN_WORKER_ID");
    if (wid && *wid && strlen(wid) < sizeof(zan__crash_wid)) {
        memcpy(zan__crash_wid, wid, strlen(wid) + 1);
    }

    const char *env = getenv("ZAN_LOG_DIR");
    const char *home = getenv("HOME");
    int len = 0;
    zan__crash_logdir[0] = 0;
    if (env && *env) {
        len = snprintf(zan__crash_logdir, sizeof zan__crash_logdir, "%s", env);
    } else {
#if defined(__APPLE__)
        if (home && home[0] == '/')
            len = snprintf(zan__crash_logdir, sizeof zan__crash_logdir,
                           "%s/Library/Caches/Zan/logs", home);
#else
        const char *cache = getenv("XDG_CACHE_HOME");
        if (cache && cache[0] == '/')
            len = snprintf(zan__crash_logdir, sizeof zan__crash_logdir, "%s/zan/logs", cache);
        else if (home && home[0] == '/')
            len = snprintf(zan__crash_logdir, sizeof zan__crash_logdir, "%s/.cache/zan/logs", home);
#endif
    }
    if (len > 0 && (size_t)len < sizeof zan__crash_logdir &&
        zan__crash_prepare_dir(zan__crash_logdir)) return;
    const char *tmp = getenv("TMPDIR");
    if (!tmp || tmp[0] != '/') tmp = "/tmp";
    len = snprintf(zan__crash_logdir, sizeof zan__crash_logdir,
                   "%s/zan-%lu/logs", tmp, (unsigned long)getuid());
    if (len <= 0 || (size_t)len >= sizeof zan__crash_logdir ||
        !zan__crash_prepare_dir(zan__crash_logdir)) zan__crash_logdir[0] = 0;
}

/* <logdir>/{yyyy}{MM}/{dd} */
static int zan__crash_logpath_for(const struct tm *tmv, char *out, size_t cap) {
    char month[16];
    char day[16];
    month[0] = '\0';
    day[0] = '\0';
    if (tmv) {
        int y = tmv->tm_year + 1900;
        unsigned mo = (unsigned)(tmv->tm_mon + 1);
        unsigned da = (unsigned)tmv->tm_mday;
        if (y < 0 || y > 9999) y = 1970;
        if (mo > 12) mo = 1;
        if (da > 31) da = 1;
        char *p = month;
        p[0] = (char)('0' + (y / 1000) % 10);
        p[1] = (char)('0' + (y / 100) % 10);
        p = zan__crash_put2(p + 2, (unsigned)(y % 100));
        p = zan__crash_put2(p, mo);
        *p = '\0';
        p = day;
        p = zan__crash_put2(p, da);
        *p = '\0';
    }
    if (!month[0] || !day[0] || !zan__crash_logdir[0]) return 0;
    char dir[4096];
    if ((size_t)snprintf(dir, sizeof dir, "%s/%s", zan__crash_logdir, month)
            >= sizeof dir) return 0;
    mkdir(zan__crash_logdir, 0755);
    mkdir(dir, 0755);
    return (size_t)snprintf(out, cap, "%s/%s.log", dir, day) < cap;
}

static void zan__crash_record(int fd, int sig, void *addr, const char *stamp,
                              void *uctx) {
    char num[24];
    zan__crash_wr(fd, "==== ZAN CRASH ");
    zan__crash_wr(fd, stamp);
    zan__crash_wr(fd, " ====\n");
    zan__crash_wr(fd, "exe=");
    zan__crash_wr(fd, zan__crash_exe);
    zan__crash_wr(fd, " pid=");
    zan__crash_wr(fd, zan__crash_dec((unsigned long long)getpid(), num));
    if (zan__crash_wid[0]) {
        zan__crash_wr(fd, " worker=");
        zan__crash_wr(fd, zan__crash_wid);
    }
    zan__crash_wr(fd, "\nsignal=");
    zan__crash_wr(fd, zan__crash_signame(sig));
    zan__crash_wr(fd, "(");
    zan__crash_wr(fd, zan__crash_dec((unsigned long long)sig, num));
    zan__crash_wr(fd, ") addr=");
    zan__crash_wr(fd, zan__crash_hex((unsigned long long)(size_t)addr, num));
    zan__crash_wr(fd, "\n");
#if defined(ZAN_CRASH_HAVE_BACKTRACE)
    {
        void *frames[64];
        int n = backtrace(frames, 64);
        zan__crash_wr(fd, "backtrace (");
        zan__crash_wr(fd, zan__crash_dec((unsigned long long)n, num));
        zan__crash_wr(fd, " frames):\n");
        backtrace_symbols_fd(frames, n, fd);
    }
#else
    /* No execinfo (musl/BSD) */
#  if defined(__linux__) && defined(__x86_64__)
    /* glibc/musl mcontext_t 寄存器布局解析 */
    ucontext_t *uc = (ucontext_t *)uctx;
    if (uc) {
        unsigned long *mc = (unsigned long *)&uc->uc_mcontext;
        void *rip = (void *)mc[16];   /* REG_RIP */
        void **rbp = (void **)mc[10]; /* REG_RBP */
        zan__crash_wr(fd, "faulting ip:\n");
        zan__crash_frame(fd, rip);
        zan__crash_wr(fd, "backtrace (frame-pointer walk):\n");
        if (!((size_t)rbp & (sizeof(void *) - 1)))
            zan__crash_fp_walk(fd, rbp);
        else
            zan__crash_wr(fd, "  # <frame pointer not preserved>\n");
    }
#  else
    zan__crash_wr(fd, "backtrace (frame-pointer walk from handler):\n");
    zan__crash_fp_walk(fd, (void **)__builtin_frame_address(0));
#  endif
#endif
    zan__crash_wr(fd, "\n");
}

static void zan__crash_handler(int sig, siginfo_t *info, void *ctx) {
    void *addr = info ? info->si_addr : NULL;
    time_t now = time(NULL) + (time_t)zan__crash_tz_off;
    char stamp[32];
    /* 信号安全格式化时间字符串 */
    struct tm lt;
    memset(&lt, 0, sizeof lt);
    {
        long long days = (long long)(now / 86400);
        long long secs = (long long)(now % 86400);
        if (secs < 0) { secs += 86400; days -= 1; }
        int y = 1970;
        unsigned mo = 1, da = 1;
        zan__crash_civil(days, &y, &mo, &da);
        unsigned hh = (unsigned)(secs / 3600);
        unsigned mi = (unsigned)((secs / 60) % 60);
        unsigned ss = (unsigned)(secs % 60);
        lt.tm_year = y - 1900;
        lt.tm_mon = (int)mo - 1;
        lt.tm_mday = (int)da;
        lt.tm_hour = (int)hh;
        lt.tm_min = (int)mi;
        lt.tm_sec = (int)ss;
        char *p = stamp;
        if (y < 0 || y > 9999) y = 1970;
        p[0] = (char)('0' + (y / 1000) % 10);
        p[1] = (char)('0' + (y / 100) % 10);
        p = zan__crash_put2(p + 2, (unsigned)(y % 100));
        *p++ = '-';
        p = zan__crash_put2(p, mo);
        *p++ = '-';
        p = zan__crash_put2(p, da);
        *p++ = ' ';
        p = zan__crash_put2(p, hh);
        *p++ = ':';
        p = zan__crash_put2(p, mi);
        *p++ = ':';
        p = zan__crash_put2(p, ss);
        *p = '\0';
    }

    /* 优先输出至 stderr，供外部进程守护者重定向捕获 */
    zan__crash_record(zan__crash_err_fd >= 0 ? zan__crash_err_fd
                                             : STDERR_FILENO,
                      sig, addr, stamp, ctx);

    char path[4096];
    if (zan__crash_logpath_for(&lt, path, sizeof path)) {
        int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            /* 避免向同一日志文件重复写入崩溃记录 */
            struct stat a, b;
            int same = fstat(STDERR_FILENO, &a) == 0 && fstat(fd, &b) == 0
                       && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
            if (!same) zan__crash_record(fd, sig, addr, stamp, ctx);
            close(fd);
        }
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

void zan__crash_install(void) {
    static volatile int once = 0;
    if (once) return;
    once = 1;
    zan__crash_resolve_paths();
    zan__crash_cache_handler_state();
#if defined(__linux__) && !defined(SA_ONSTACK)
#define SA_ONSTACK 0x08000000
#endif
#if defined(__OHOS__) || (defined(__linux__) && !defined(__GLIBC__))
/* musl hides sigaltstack() behind _BSD_SOURCE/_XOPEN_SOURCE/_GNU_SOURCE, and
   the cross recipes compile with a plain -std=c11 (no feature macros), so on
   musl targets the declaration -- and SS_DISABLE -- are simply missing.
   Declare them here. musl's prototype is exactly the one below; glibc's own
   declaration puts the const on the other argument, so re-declaring it there
   would be a conflicting-type error -- hence the __GLIBC__ guard. */
#if !defined(SS_DISABLE)
#define SS_DISABLE 2
#endif
int sigaltstack(const stack_t *__restrict, stack_t *__restrict);
#endif
    {
        stack_t cur;
        if (sigaltstack(NULL, &cur) == 0 &&
            (cur.ss_flags & SS_DISABLE || cur.ss_size == 0)) {
            static char zan__crash_altstack[64 * 1024];
            stack_t ss;
            memset(&ss, 0, sizeof ss);
            ss.ss_sp = zan__crash_altstack;
            ss.ss_size = sizeof zan__crash_altstack;
            ss.ss_flags = 0;
            sigaltstack(&ss, NULL);
        }
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = zan__crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    static const int sigs[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
    for (unsigned i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        struct sigaction cur;
        if (sigaction(sigs[i], NULL, &cur) == 0 && cur.sa_handler != SIG_DFL) {
            continue;
        }
        sigaction(sigs[i], &sa, NULL);
    }
}

/* 导出面跨平台兜底：POSIX 无 SEH 恢复语义，guard_call 直通、恢复计数恒 0 */
int zan__guard_call(void (*fn)(void *), void *arg) { if (fn) fn(arg); return 1; }
long zan__guard_recovered_count(void) { return 0; }

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void zan__crash_ctor(void) {
    zan__crash_install();
}
#endif
