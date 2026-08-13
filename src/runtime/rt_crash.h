/* rt_crash.h -- persistent native crash logging.
 *
 * Installs a Windows unhandled-exception filter that appends a diagnostic
 * record to <exe_dir>\zan_crash.log on any hard crash (access violation, heap
 * corruption 0xC0000374, illegal instruction, etc.). Each record carries the
 * timestamp, exe path, process/thread ids, exception code/flags, the faulting
 * read/write/execute address, key registers, the faulting module+offset, and a
 * return-address backtrace as module+offset lines.
 *
 * When the process image carries DWARF debug info (built with `zanc -g`, e.g.
 * a Debug publish), the handler additionally resolves every in-module frame to
 * `function  <file>.zan:line` right there in the log, so a crash points at the
 * exact source location with no manual addr2line step. Resolution shells out
 * to a symbolizer found next to the exe (addr2line.exe / llvm-symbolizer.exe)
 * or on PATH; if none is present the raw module+offset lines still stand and
 * can be resolved offline (scripts\symbolize_crash.ps1).
 *
 * Included by the always-linked runtime objects (rt_io.c reactor and the
 * gui_runtime DLL) so both console/async and GUI (ZanIDE) processes leave a
 * trace. Every symbol is static so multiple includers never collide at link
 * time, and installation is idempotent (only the first caller wins).
 */
#ifndef ZAN_RT_CRASH_H
#define ZAN_RT_CRASH_H

#if defined(_WIN32)
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static void zan__crash_modline(FILE *f, void *addr, const char *tag) {
    HMODULE m = NULL;
    char mp[MAX_PATH];
    if (addr &&
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)addr, &m) &&
        m && GetModuleFileNameA(m, mp, (DWORD)sizeof mp)) {
        fprintf(f, "%s%p  %s+0x%llX\n", tag, addr, mp,
                (unsigned long long)((char *)addr - (char *)m));
    } else {
        fprintf(f, "%s%p  <unknown>\n", tag, addr);
    }
}

/* Preferred (link-time) ImageBase, read from the exe FILE on disk. The
 * symbolizer wants a section VMA (link ImageBase + RVA); the in-memory PE
 * header is unreliable here because on a relocated GNU-ld image the loader
 * rewrites OptionalHeader.ImageBase to the ASLR runtime base, which would make
 * every VMA collapse to a bare RVA and resolve to nothing. The on-disk header
 * still carries the true link base (0x140000000 for these builds). */
static uintptr_t zan__crash_image_base(const char *exe) {
    HANDLE h = CreateFileA(exe, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    unsigned char buf[0x400];
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, (DWORD)sizeof buf, &got, NULL);
    CloseHandle(h);
    if (!ok || got < 0x120) return 0;
    if (buf[0] != 'M' || buf[1] != 'Z') return 0;
    uint32_t e_lfanew;
    memcpy(&e_lfanew, buf + 0x3C, 4);
    if ((size_t)e_lfanew + 0x40 > (size_t)got) return 0;
    unsigned char *nt = buf + e_lfanew;
    if (!(nt[0] == 'P' && nt[1] == 'E' && nt[2] == 0 && nt[3] == 0)) return 0;
    unsigned char *opt = nt + 24; /* 4 (sig) + 20 (file header) */
    uint16_t magic;
    memcpy(&magic, opt, 2);
    if (magic == 0x20B) { /* PE32+ : ImageBase is a ULONGLONG at opt+24 */
        unsigned long long ib;
        memcpy(&ib, opt + 24, 8);
        return (uintptr_t)ib;
    }
    if (magic == 0x10B) { /* PE32 : ImageBase is a DWORD at opt+28 */
        uint32_t ib;
        memcpy(&ib, opt + 28, 4);
        return (uintptr_t)ib;
    }
    return 0;
}

/* True when addr belongs to the main exe module (skip ntdll/kernel frames). */
static int zan__crash_in_main(void *addr, uintptr_t exe_base) {
    HMODULE m = NULL;
    if (addr &&
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)addr, &m)) {
        return (uintptr_t)m == exe_base;
    }
    return 0;
}

/* Locate a symbolizer: prefer one shipped beside the exe (so a Debug publish
 * is self-contained), else fall back to PATH. Returns 1/2 for
 * addr2line/llvm-symbolizer and writes the program name/path into out. */
static int zan__crash_find_symbolizer(const char *exe_dir, char *out, size_t outsz) {
    char cand[MAX_PATH];
    snprintf(cand, sizeof cand, "%saddr2line.exe", exe_dir);
    if (GetFileAttributesA(cand) != INVALID_FILE_ATTRIBUTES) {
        snprintf(out, outsz, "%s", cand); return 1;
    }
    snprintf(cand, sizeof cand, "%sllvm-symbolizer.exe", exe_dir);
    if (GetFileAttributesA(cand) != INVALID_FILE_ATTRIBUTES) {
        snprintf(out, outsz, "%s", cand); return 2;
    }
    if (SearchPathA(NULL, "addr2line", ".exe", 0, NULL, NULL)) {
        snprintf(out, outsz, "addr2line"); return 1;
    }
    if (SearchPathA(NULL, "llvm-symbolizer", ".exe", 0, NULL, NULL)) {
        snprintf(out, outsz, "llvm-symbolizer"); return 2;
    }
    return 0;
}

/* True for a symbolizer line that carries no information about the crashing
 * program: an unresolved frame (`??`), the compiler support frames DWARF
 * attributes to cygming-crtbegin.c, and the mingw CRT entry thunks. Dropping
 * them keeps the resolved block to application frames; the raw
 * module+offset backtrace above it still lists every address. */
static int zan__crash_line_is_noise(const char *line) {
    static const char *const noise[] = {
        "cygming-crtbegin.c", "__tmainCRTStartup", "WinMainCRTStartup",
        "mainCRTStartup", "__mingw_", "pre_c_init", "pre_cpp_init",
        "mingw-w64-crt", "crtexe.c", "crtdll.c", "crt_handler.c",
    };
    for (size_t i = 0; i < sizeof(noise) / sizeof(noise[0]); i++)
        if (strstr(line, noise[i])) return 1;
    /* `?? at ??:0` / `?? ??:?`: no function and no file. */
    if (line[0] == '?' && line[1] == '?') return 1;
    return 0;
}

/* Copy the symbolizer's output into the log, dropping noise lines. Returns the
 * number of lines kept. */
static int zan__crash_append_filtered(const char *logpath, const char *tmppath,
                                      int keep_all) {
    FILE *in = fopen(tmppath, "rb");
    if (!in) return 0;
    FILE *out = fopen(logpath, "ab");
    if (!out) { fclose(in); return 0; }
    char line[1024];
    int kept = 0;
    while (fgets(line, (int)sizeof line, in)) {
        if (!keep_all && zan__crash_line_is_noise(line)) continue;
        fputs(line, out);
        kept++;
    }
    fclose(out);
    fclose(in);
    return kept;
}

/* Append `function  file:line` for every in-module frame to the (closed) log
 * by spawning the symbolizer with stdout redirected to a temp file, which is
 * then filtered into the log. Best-effort: any failure leaves the raw log
 * intact. */
static void zan__crash_symbolize(const char *logpath, const char *exe,
                                 const char *exe_dir, uintptr_t exe_base,
                                 void *fault, void **frames, USHORT nframes,
                                 int keep_all) {
    uintptr_t image_base = zan__crash_image_base(exe);
    if (!image_base) return;
    char sym[MAX_PATH];
    int mode = zan__crash_find_symbolizer(exe_dir, sym, sizeof sym);
    if (!mode) return;

    /* Collect the section VMAs of the fault site and each in-module frame. */
    char addrs[3200];
    size_t ap = 0;
    int count = 0;
    void *seq[64];
    int nseq = 0;
    if (zan__crash_in_main(fault, exe_base)) seq[nseq++] = fault;
    for (USHORT i = 0; i < nframes && nseq < 64; i++)
        if (zan__crash_in_main(frames[i], exe_base)) seq[nseq++] = frames[i];
    for (int i = 0; i < nseq; i++) {
        uintptr_t vma = image_base + ((uintptr_t)seq[i] - exe_base);
        int w = snprintf(addrs + ap, sizeof(addrs) - ap, " 0x%llX",
                         (unsigned long long)vma);
        if (w <= 0 || (size_t)w >= sizeof(addrs) - ap) break;
        ap += (size_t)w;
        count++;
    }
    if (!count) return;

    char cmd[4096];
    if (mode == 1) /* addr2line */
        snprintf(cmd, sizeof cmd, "\"%s\" -e \"%s\" -f -C -i -p%s", sym, exe, addrs);
    else /* llvm-symbolizer */
        snprintf(cmd, sizeof cmd,
                 "\"%s\" --obj=\"%s\" --demangle --functions=linkage --inlines%s",
                 sym, exe, addrs);

    char tmppath[MAX_PATH];
    snprintf(tmppath, sizeof tmppath, "%szan_crash.sym.%lu.tmp", exe_dir,
             (unsigned long)GetCurrentProcessId());

    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    HANDLE h = CreateFileA(tmppath, GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    STARTUPINFOA si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = NULL;
    si.hStdOutput = h;
    si.hStdError = h;
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof pi);
    if (CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                       NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 8000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    CloseHandle(h);

    /* Header line so the resolved block is easy to find; the filtered child
     * output follows it, in the same order as the frames above. */
    FILE *hf = fopen(logpath, "ab");
    if (hf) {
        fprintf(hf, "resolved source locations (%d in-module frame(s), top first%s):\n",
                count, keep_all ? "" : ", CRT frames omitted");
        fclose(hf);
    }
    if (zan__crash_append_filtered(logpath, tmppath, keep_all) == 0) {
        FILE *nf = fopen(logpath, "ab");
        if (nf) {
            fprintf(nf, "  <no source locations: build with `zanc -g` to get "
                        "file:line; the module+offset backtrace above still "
                        "resolves offline via scripts\\symbolize_crash.ps1>\n");
            fclose(nf);
        }
    }
    DeleteFileA(tmppath);
    FILE *tf = fopen(logpath, "ab");
    if (tf) { fprintf(tf, "\n"); fclose(tf); }
}

/* Resolve a single address (the release that quarantined a block under
 * --arc-guard) under its own header, so the log shows both the stale use and
 * the site that freed it. */
static void zan__crash_symbolize_one(const char *logpath, const char *exe,
                                    const char *exe_dir, uintptr_t exe_base,
                                    void *addr) {
    FILE *hf = fopen(logpath, "ab");
    if (hf) { fprintf(hf, "released (arc.freed_by) at:\n"); fclose(hf); }
    /* Keep every line here: a release inside a synthesized destructor has no
     * line table, and "?? at ??:?" from one address is still the answer that
     * the freeing code is compiler-generated rather than user code. */
    zan__crash_symbolize(logpath, exe, exe_dir, exe_base, addr, NULL, 0, 1);
}

static LONG WINAPI zan__crash_filter(EXCEPTION_POINTERS *ep) {
    if (!ep || !ep->ExceptionRecord) return EXCEPTION_EXECUTE_HANDLER;

    char logpath[MAX_PATH];
    char exe_dir[MAX_PATH];
    exe_dir[0] = '\0';
    DWORD n = GetModuleFileNameA(NULL, logpath, (DWORD)sizeof logpath);
    if (n == 0 || n >= sizeof logpath) {
        snprintf(logpath, sizeof(logpath), "zan_crash.log");
    } else {
        char *slash = strrchr(logpath, '\\');
        if (slash) {
            size_t dl = (size_t)(slash + 1 - logpath);
            memcpy(exe_dir, logpath, dl);
            exe_dir[dl] = '\0';
            slash[1] = '\0';
            strncat(logpath, "zan_crash.log",
                    sizeof(logpath) - strlen(logpath) - 1);
        } else {
            snprintf(logpath, sizeof(logpath), "zan_crash.log");
        }
    }

    char exe[MAX_PATH];
    if (!GetModuleFileNameA(NULL, exe, (DWORD)sizeof exe)) exe[0] = '\0';
    uintptr_t exe_base = (uintptr_t)GetModuleHandleW(NULL);
    void *frames[62];
    USHORT fn = 0;
    void *arc_freed_by = NULL;

    FILE *f = fopen(logpath, "ab");
    if (!f) f = fopen("zan_crash.log", "ab");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        EXCEPTION_RECORD *er = ep->ExceptionRecord;

        fprintf(f, "==== ZAN CRASH %04d-%02d-%02d %02d:%02d:%02d.%03d ====\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                st.wSecond, st.wMilliseconds);
        fprintf(f, "exe=%s pid=%lu tid=%lu\n", exe,
                (unsigned long)GetCurrentProcessId(),
                (unsigned long)GetCurrentThreadId());
        fprintf(f, "code=0x%08lX flags=0x%08lX\n",
                (unsigned long)er->ExceptionCode,
                (unsigned long)er->ExceptionFlags);
        {   /* ARC integrity failures raised by generated code (the codes are
             * defined in src/compiler/irgen.c): the reported frames are the
             * retain/release that used a dangling reference, i.e. the cause of
             * a use-after-free rather than its symptom. */
            const char *arc = NULL;
            switch (er->ExceptionCode) {
            case 0xE0A2C001: arc = "retain of an already-freed object"; break;
            case 0xE0A2C002: arc = "release of an already-freed object"; break;
            case 0xE0A2C003: arc = "retain of an already-freed string"; break;
            case 0xE0A2C004: arc = "release of an already-freed string"; break;
            case 0xE0A2C005: arc = "use of a freed object through a stale "
                                   "reference (missing retain when stored)";
                             break;
            case 0xE0A2C006: arc = "use of a freed string through a stale "
                                   "reference (missing retain when stored)";
                             break;
            default: break;
            }
            if (arc) {
                fprintf(f, "arc=%s\n", arc);
                if (er->NumberParameters >= 3) {
                    fprintf(f, "arc.object=0x%p arc.refcount=%lld\n",
                            (void *)er->ExceptionInformation[0],
                            (long long)er->ExceptionInformation[1]);
                    arc_freed_by = (void *)er->ExceptionInformation[2];
                    zan__crash_modline(f, arc_freed_by, "arc.freed_by=");
                }
            }
        }
        if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
            er->NumberParameters >= 2) {
            const char *kind = er->ExceptionInformation[0] == 1 ? "write"
                             : er->ExceptionInformation[0] == 8 ? "execute"
                                                                : "read";
            fprintf(f, "access=%s addr=0x%p\n", kind,
                    (void *)er->ExceptionInformation[1]);
        }
        zan__crash_modline(f, er->ExceptionAddress, "fault=");

#if defined(_M_X64) || defined(__x86_64__)
        if (ep->ContextRecord) {
            CONTEXT *c = ep->ContextRecord;
            fprintf(f, "rip=%p rsp=%p rbp=%p\n",
                    (void *)c->Rip, (void *)c->Rsp, (void *)c->Rbp);
            fprintf(f, "rax=%p rbx=%p rcx=%p rdx=%p rsi=%p rdi=%p\n",
                    (void *)c->Rax, (void *)c->Rbx, (void *)c->Rcx,
                    (void *)c->Rdx, (void *)c->Rsi, (void *)c->Rdi);
        }
#endif

        fn = RtlCaptureStackBackTrace(0, 62, frames, NULL);
        fprintf(f, "backtrace (%u frames):\n", (unsigned)fn);
        for (USHORT i = 0; i < fn; i++) {
            char tag[16];
            snprintf(tag, sizeof tag, "  #%02u ", (unsigned)i);
            zan__crash_modline(f, frames[i], tag);
        }
        fprintf(f, "\n");
        fclose(f);

        /* Resolve to source when the image carries DWARF (Debug builds). Done
         * after the raw record is flushed so a symbolizer failure never costs
         * us the addresses. */
        zan__crash_symbolize(logpath, exe, exe_dir[0] ? exe_dir : ".\\",
                             exe_base, ep->ExceptionRecord->ExceptionAddress,
                             frames, fn, 0);
        if (arc_freed_by)
            zan__crash_symbolize_one(logpath, exe, exe_dir[0] ? exe_dir : ".\\",
                                     exe_base, arc_freed_by);
    }
    return EXCEPTION_EXECUTE_HANDLER; /* log, then terminate (skip WER dialog) */
}

static void zan__crash_install(void) {
    static LONG once = 0;
    if (InterlockedCompareExchange(&once, 1, 0) != 0) return;
    SetUnhandledExceptionFilter(zan__crash_filter);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void zan__crash_ctor(void) {
    zan__crash_install();
}
#endif

#else /* !_WIN32 */
/* POSIX counterpart: a fatal signal (SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT)
 * otherwise kills the process without a word, which is exactly the case that
 * matters for a supervised worker -- the master only sees its control socket
 * close. The handler writes one record to stderr (so a worker whose output is
 * redirected to a log file leaves it there) and appends the same record to
 * <exe_dir>/zan_crash.log, then re-raises with the default disposition so the
 * wait status still reports the signal.
 *
 * Everything here is async-signal-safe: write(2) plus hand-rolled integer
 * formatting, no malloc/stdio. The backtrace uses backtrace_symbols_fd, which
 * writes with the raw fd for that reason (glibc only; musl has no execinfo). */
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

#if defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>
#define ZAN_CRASH_HAVE_BACKTRACE 1
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

static void zan__crash_wr(int fd, const char *s) {
    size_t n = strlen(s);
    while (n) {
        ssize_t w = write(fd, s, n);
        if (w <= 0) return;
        s += (size_t)w;
        n -= (size_t)w;
    }
}

/* Decimal, into a caller-owned buffer (>= 24 bytes). */
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

/* Own program path, and the directory the dated crash log goes in: $ZAN_LOG_DIR
 * when the process was started with one (the server master exports it for its
 * workers, so a worker's crash record lands in the pool's shared daily log),
 * else <exe_dir>/logs. Both are resolved at install time -- reading /proc or
 * the environment from inside a signal handler would be one more thing that
 * can fault. */
static char zan__crash_exe[4096];
static char zan__crash_logdir[4096];
/* $ZAN_WORKER_ID, so a record tells which worker of the pool died. */
static char zan__crash_wid[32];

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
    if (env && *env && strlen(env) < sizeof(zan__crash_logdir)) {
        memcpy(zan__crash_logdir, env, strlen(env) + 1);
        return;
    }
    /* <exe_dir>/logs */
    memcpy(zan__crash_logdir, "logs", 5);
    if (n <= 0) return;
    char *slash = strrchr(zan__crash_exe, '/');
    if (!slash) return;
    size_t dl = (size_t)(slash + 1 - zan__crash_exe);
    if (dl + 5 >= sizeof(zan__crash_logdir)) return;
    memcpy(zan__crash_logdir, zan__crash_exe, dl);
    memcpy(zan__crash_logdir + dl, "logs", 5);
}

/* <logdir>/{yyyy}{MM}/{dd}.log -- the very file the master and the workers log
 * to, so the crash record sits right where the operator is already reading,
 * between the request that triggered it and the master's respawn line. Returns
 * 0 when the directory could not be made. */
static int zan__crash_logpath_for(const struct tm *tmv, char *out, size_t cap) {
    char month[16];
    char day[16];
    month[0] = '\0';
    day[0] = '\0';
    if (tmv) {
        strftime(month, sizeof month, "%Y%m", tmv);
        strftime(day, sizeof day, "%d", tmv);
    }
    if (!month[0] || !day[0]) return 0;
    char dir[4096];
    if ((size_t)snprintf(dir, sizeof dir, "%s/%s", zan__crash_logdir, month)
            >= sizeof dir) return 0;
    mkdir(zan__crash_logdir, 0755); /* best effort; already-exists is fine */
    mkdir(dir, 0755);
    return (size_t)snprintf(out, cap, "%s/%s.log", dir, day) < cap;
}

static void zan__crash_record(int fd, int sig, void *addr, const char *stamp) {
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
    zan__crash_wr(fd, "backtrace unavailable (no execinfo)\n");
#endif
    zan__crash_wr(fd, "\n");
}

static void zan__crash_handler(int sig, siginfo_t *info, void *ctx) {
    (void)ctx;
    void *addr = info ? info->si_addr : NULL;
    time_t now = time(NULL);
    struct tm tmv;
    struct tm *lt = localtime_r(&now, &tmv);
    char stamp[32];
    stamp[0] = '\0';
    if (lt) strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", lt);

    /* stderr first: a supervised worker has it redirected to its dated output
     * file, which is where the operator looks anyway. */
    zan__crash_record(STDERR_FILENO, sig, addr, stamp);

    char path[4096];
    if (zan__crash_logpath_for(lt, path, sizeof path)) {
        int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            /* A supervised worker already has stderr pointing at this same
             * file; writing the record a second time would only double it. */
            struct stat a, b;
            int same = fstat(STDERR_FILENO, &a) == 0 && fstat(fd, &b) == 0
                       && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
            if (!same) zan__crash_record(fd, sig, addr, stamp);
            close(fd);
        }
    }
    /* Die of the original signal so waitpid() reports WIFSIGNALED with it. */
    signal(sig, SIG_DFL);
    raise(sig);
}

static void zan__crash_install(void) {
    static volatile int once = 0;
    if (once) return;
    once = 1;
    zan__crash_resolve_paths();
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = zan__crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    static const int sigs[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT };
    for (unsigned i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        struct sigaction cur;
        /* Leave a disposition the program chose for itself alone. */
        if (sigaction(sigs[i], NULL, &cur) == 0 && cur.sa_handler != SIG_DFL) {
            continue;
        }
        sigaction(sigs[i], &sa, NULL);
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void zan__crash_ctor(void) {
    zan__crash_install();
}
#endif
#endif

#endif /* ZAN_RT_CRASH_H */
