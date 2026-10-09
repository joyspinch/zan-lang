/* 内部辅助实现 */
#ifndef ZAN_RT_CRASH_H
#define ZAN_RT_CRASH_H

/* 底层系统交互与数据协议契约 */
#ifndef ZAN_MAYBE_UNUSED
#  if defined(__GNUC__) || defined(__clang__)
#    define ZAN_MAYBE_UNUSED __attribute__((unused))
#  else
#    define ZAN_MAYBE_UNUSED
#  endif
#endif

#if defined(_WIN32)
#include <windows.h>
/* 内部辅助实现 */
WINBASEAPI ULONGLONG WINAPI GetTickCount64(VOID);
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
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

/* 编译器代码生成与运行时系统底层调用契约 */
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
    unsigned char *opt = nt + 24; /* 核心系统底层抽象与内存语义契约 */
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

/* 编译器代码生成与运行时系统底层调用契约 */
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

/* 内部辅助实现 */
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
    return 0;
}

/* 内部辅助实现 */
static int zan__crash_line_is_noise(const char *line) {
    static const char *const noise[] = {
        "cygming-crtbegin.c", "__tmainCRTStartup", "WinMainCRTStartup",
        "mainCRTStartup", "__mingw_", "pre_c_init", "pre_cpp_init",
        "mingw-w64-crt", "crtexe.c", "crtdll.c", "crt_handler.c",
    };
    for (size_t i = 0; i < sizeof(noise) / sizeof(noise[0]); i++)
        if (strstr(line, noise[i])) return 1;
    /* 核心系统底层抽象与内存语义契约 */
    if (line[0] == '?' && line[1] == '?') return 1;
    return 0;
}

/* 过滤符号化输出噪音行并写入崩溃日志 */
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

/* 内部辅助实现 */
static void zan__crash_symbolize(const char *logpath, const char *exe,
                                 const char *exe_dir, uintptr_t exe_base,
                                 void *fault, void **frames, USHORT nframes,
                                 int keep_all) {
    uintptr_t image_base = zan__crash_image_base(exe);
    if (!image_base) return;
    char sym[MAX_PATH];
    int mode = zan__crash_find_symbolizer(exe_dir, sym, sizeof sym);
    if (!mode) return;

    /* 编译器代码生成与运行时系统底层调用契约 */
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
    /* 内部辅助实现 */
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 8 && h == INVALID_HANDLE_VALUE; attempt++) {
        unsigned long long salt =
            (unsigned long long)GetTickCount64()
            ^ ((unsigned long long)(uintptr_t)&attempt << 17)
            ^ ((unsigned long long)GetCurrentProcessId() << 33)
            ^ ((unsigned long long)attempt * 0x9E3779B97F4A7C15ull);
        /* 高精度时间戳 (QPC)：避免 ~15ms 粒度导致的日志覆盖 */
        LARGE_INTEGER qpc;
        if (QueryPerformanceCounter(&qpc))
            salt ^= (unsigned long long)qpc.QuadPart << 19;
        snprintf(tmppath, sizeof tmppath, "%szan_crash.sym.%lu.%016llx.tmp",
                 exe_dir, (unsigned long)GetCurrentProcessId(), salt);
        SECURITY_ATTRIBUTES sa;
        memset(&sa, 0, sizeof sa);
        sa.nLength = sizeof sa;
        sa.bInheritHandle = TRUE;
        h = CreateFileA(tmppath, GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_DELETE, &sa,
                        CREATE_NEW,
                        FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY,
                        NULL);
    }
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

    /* 崩溃回溯块头部标识 */
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

/* 解析单个隔离地址（ARC 保护检测） */
static void zan__crash_symbolize_one(const char *logpath, const char *exe,
                                    const char *exe_dir, uintptr_t exe_base,
                                    void *addr) {
    FILE *hf = fopen(logpath, "ab");
    if (hf) { fprintf(hf, "released (arc.freed_by) at:\n"); fclose(hf); }
    /* 保留所有符号化行，包括合成析构函数 */
    zan__crash_symbolize(logpath, exe, exe_dir, exe_base, addr, NULL, 0, 1);
}

/* 崩溃日志路径：<exe_dir>/zan_crash.log 或当前目录回退 */
static void zan__crash_logpath(char *logpath, size_t cap, char *exe_dir) {
    exe_dir[0] = '\0';
    DWORD n = GetModuleFileNameA(NULL, logpath, (DWORD)cap);
    if (n == 0 || n >= cap) {
        snprintf(logpath, cap, "zan_crash.log");
        return;
    }
    char *slash = strrchr(logpath, '\\');
    if (!slash) {
        snprintf(logpath, cap, "zan_crash.log");
        return;
    }
    size_t dl = (size_t)(slash + 1 - logpath);
    memcpy(exe_dir, logpath, dl);
    exe_dir[dl] = '\0';
    slash[1] = '\0';
    strncat(logpath, "zan_crash.log", cap - strlen(logpath) - 1);
}

/* 底层系统交互与数据协议契约 */
static void zan__crash_note(const char *what, const char *detail) {
    char logpath[MAX_PATH];
    char exe_dir[MAX_PATH];
    zan__crash_logpath(logpath, sizeof logpath, exe_dir);
    FILE *f = fopen(logpath, "ab");
    if (!f) f = fopen("zan_crash.log", "ab");
    if (!f) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "---- %s %04d-%02d-%02d %02d:%02d:%02d.%03d pid=%lu tid=%lu %s\n",
            what, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
            st.wSecond, st.wMilliseconds,
            (unsigned long)GetCurrentProcessId(),
            (unsigned long)GetCurrentThreadId(), detail ? detail : "");
    fclose(f);
}

static void zan__crash_write_record(EXCEPTION_POINTERS *ep, const char *reason,
                                   void **bt, unsigned btn);

#if defined(_MSC_VER)
#define ZAN_CRASH_TLS __declspec(thread)
#else
#define ZAN_CRASH_TLS __thread
#endif

/* 编译器代码生成与运行时系统底层调用契约 */
#if defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#define ZAN_CRASH_SHARED __attribute__((selectany))
#elif defined(_MSC_VER)
#define ZAN_CRASH_SHARED __declspec(selectany)
#else
#define ZAN_CRASH_SHARED
#endif

/* 跨架构 CPU 寄存器上下文解析 */
#if defined(_M_ARM64) || defined(__aarch64__)
#define ZAN_CTX_SP(c) ((c)->Sp)
#define ZAN_CTX_PC(c) ((c)->Pc)
#elif defined(_M_X64) || defined(__x86_64__)
#define ZAN_CTX_SP(c) ((c)->Rsp)
#define ZAN_CTX_PC(c) ((c)->Rip)
#else
#define ZAN_CTX_SP(c) (0)
#define ZAN_CTX_PC(c) (0)
#endif

/* 编译器代码生成与运行时系统底层调用契约 */
static void zan__crash_image_range(uintptr_t base, uintptr_t *lo,
                                  uintptr_t *hi) {
    *lo = base;
    *hi = base;
    if (!base) return;
    const IMAGE_DOS_HEADER *dh = (const IMAGE_DOS_HEADER *)base;
    if (dh->e_magic != IMAGE_DOS_SIGNATURE) return;
    const IMAGE_NT_HEADERS *nh =
        (const IMAGE_NT_HEADERS *)(base + (uintptr_t)dh->e_lfanew);
    if (nh->Signature != IMAGE_NT_SIGNATURE) return;
    *hi = base + nh->OptionalHeader.SizeOfImage;
}

/* 返回addresses left on the faulting stack, most frequent first */
static void zan__crash_stack_scan(FILE *f, ULONG_PTR rsp, uintptr_t exe_base) {
    uintptr_t lo, hi;
    zan__crash_image_range(exe_base, &lo, &hi);
    if (hi <= lo) return;

    /* 内部辅助实现 */
    ULONG_PTR start = rsp + 128 * 1024;

    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void *)start, &mbi, sizeof mbi)) return;
    if (mbi.State != MEM_COMMIT) return;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return;
    ULONG_PTR region_end = (ULONG_PTR)mbi.BaseAddress + mbi.RegionSize;

    /* 模块核心语义抽象与接口调用契约 */
    ULONG_PTR end = start + 512 * 1024;
    if (end > region_end) end = region_end;

    enum { SLOTS = 24 };
    uintptr_t addr[SLOTS];
    unsigned long hits[SLOTS];
    unsigned used = 0;
    unsigned long total = 0;

    for (ULONG_PTR p = (start + 7) & ~(ULONG_PTR)7; p + 8 <= end; p += 8) {
        uintptr_t v = *(const uintptr_t *)p;
        if (v < lo || v >= hi) continue;
        total++;
        unsigned i = 0;
        for (; i < used; i++) {
            if (addr[i] == v) { hits[i]++; break; }
        }
        if (i == used && used < SLOTS) {
            addr[used] = v;
            hits[used] = 1;
            used++;
        }
    }
    if (!used) return;

    fprintf(f, "stack-scan (%lu in-image addresses, most frequent first --"
               " a repeated address is the recursion):\n", total);
    for (unsigned n = 0; n < used && n < 12; n++) {
        unsigned best = n;
        for (unsigned i = n + 1; i < used; i++)
            if (hits[i] > hits[best]) best = i;
        uintptr_t ta = addr[best]; unsigned long th = hits[best];
        addr[best] = addr[n]; hits[best] = hits[n];
        addr[n] = ta; hits[n] = th;
        char tag[32];
        snprintf(tag, sizeof tag, "  x%-6lu ", th);
        zan__crash_modline(f, (void *)ta, tag);
    }
}

/* 线程级崩溃恢复状态槽（以线程 ID 为键） */
typedef struct zan__guard zan__guard_t;
typedef struct zan__fault zan__fault_t;

typedef struct zan__thread_slot {
    volatile LONG tid;      /* 核心系统底层抽象与内存语义契约 */
    zan__guard_t *top;      /* 核心系统底层抽象与内存语义契约 */
    int ready;              /* 底层系统交互与数据协议契约 */
    int busy;               /* 核心系统底层抽象与内存语义契约 */
} zan__thread_slot;

#define ZAN_GUARD_SLOTS 64

/* 模块核心语义抽象与接口调用契约 */
typedef struct zan__shared {
    LONG magic;
    volatile LONG busy;         /* 底层系统交互与数据协议契约 */
    volatile LONG recovered;    /* 核心系统底层抽象与内存语义契约 */
    volatile LONG logged;       /* 核心系统底层抽象与内存语义契约 */
    volatile LONG firstchance;  /* 底层系统交互与数据协议契约 */
    volatile LONG installed;    /* 核心系统底层抽象与内存语义契约 */
    zan__thread_slot slots[ZAN_GUARD_SLOTS];
} zan__shared_t;

ZAN_CRASH_SHARED zan__shared_t zan__shared = { 0x5A414353, 0, 0, 0, 0, 0, { { 0, 0, 0, 0 } } };

/* 编译器代码生成与运行时系统底层调用契约 */
#define zan__slots             zan__shared.slots
#define zan__crash_busy_shared zan__shared.busy
#define zan__guard_recovered   zan__shared.recovered
#define zan__guard_logged      zan__shared.logged
#define zan__guard_firstchance zan__shared.firstchance
#define zan__crash_installed   zan__shared.installed

/* 查找或分配当前线程的崩溃恢复状态槽 */
static zan__thread_slot *zan__slot(int create) {
    LONG tid = (LONG)GetCurrentThreadId();
    unsigned start = ((unsigned)tid * 2654435761u) % ZAN_GUARD_SLOTS;
    for (unsigned i = 0; i < ZAN_GUARD_SLOTS; i++) {
        zan__thread_slot *s = &zan__slots[(start + i) % ZAN_GUARD_SLOTS];
        if (s->tid == tid) return s;
    }
    if (!create) return NULL;
    for (unsigned i = 0; i < ZAN_GUARD_SLOTS; i++) {
        zan__thread_slot *s = &zan__slots[(start + i) % ZAN_GUARD_SLOTS];
        if (InterlockedCompareExchange(&s->tid, tid, 0) == 0) return s;
    }
    return NULL;
}

/* 核心系统底层抽象与内存语义契约 */
static int zan__crash_busy_get(zan__thread_slot *s) {
    return s ? s->busy : (int)zan__crash_busy_shared;
}
static void zan__crash_busy_set(zan__thread_slot *s, int v) {
    if (s) s->busy = v; else zan__crash_busy_shared = v;
}

static LONG WINAPI zan__crash_filter(EXCEPTION_POINTERS *ep) {
    if (!ep || !ep->ExceptionRecord) return EXCEPTION_EXECUTE_HANDLER;
    zan__thread_slot *s = zan__slot(0);
    if (zan__crash_busy_get(s)) return EXCEPTION_EXECUTE_HANDLER;
    zan__crash_busy_set(s, 1);
    zan__crash_write_record(ep, "unhandled", NULL, 0);
    zan__crash_busy_set(s, 0);
    if (ep->ExceptionRecord->ExceptionCode == 0xE0A2C010) {
        /* 编译器内部生成的非致命崩溃记录触发信号 */
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_EXECUTE_HANDLER; /* 底层系统交互与数据协议契约 */
}

/* 核心系统底层抽象与内存语义契约 */
static void zan__crash_write_record(EXCEPTION_POINTERS *ep,
                                   const char *reason,
                                   void **bt, unsigned btn) {
    if (!ep || !ep->ExceptionRecord) return;

    char logpath[MAX_PATH];
    char exe_dir[MAX_PATH];
    zan__crash_logpath(logpath, sizeof logpath, exe_dir);

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

        fprintf(f, "==== ZAN CRASH %04d-%02d-%02d %02d:%02d:%02d.%03d (%s) ====\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                st.wSecond, st.wMilliseconds, reason ? reason : "unhandled");
        fprintf(f, "exe=%s pid=%lu tid=%lu\n", exe,
                (unsigned long)GetCurrentProcessId(),
                (unsigned long)GetCurrentThreadId());
        fprintf(f, "code=0x%08lX flags=0x%08lX\n",
                (unsigned long)er->ExceptionCode,
                (unsigned long)er->ExceptionFlags);
        {   /* ARC 完整性断言崩溃代码判定 */
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
            case 0xE0A2C007: arc = "retain of an already-freed array"; break;
            case 0xE0A2C008: arc = "release of an already-freed array"; break;
            case 0xE0A2C009: arc = "use of a freed array through a stale "
                                   "reference (missing retain when stored)";
                             break;
            default: break;
            }
            /* 运行时断言失败崩溃（越界、除零或 IO 门控断言） */
            if (er->ExceptionCode == 0xE0A2C010 &&
                er->NumberParameters >= 1 && er->ExceptionInformation[0]) {
                fprintf(f, "zan=%s", (const char *)er->ExceptionInformation[0]);
                if (er->NumberParameters >= 2)
                    fprintf(f, "exit=%lld\n",
                            (long long)er->ExceptionInformation[1]);
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
            /* 内部辅助实现 */
            {
                ULONG_PTR raw_rsp = ZAN_CTX_SP(c);
                MEMORY_BASIC_INFORMATION raw_mbi;
                if (raw_rsp && VirtualQuery((void *)raw_rsp, &raw_mbi,
                                            sizeof raw_mbi)
                    && raw_mbi.State == MEM_COMMIT
                    && !(raw_mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
                    fprintf(f, "raw stack (@rsp):\n");
                    for (unsigned w = 0; w < 16; w++) {
                        ULONG_PTR p = raw_rsp + w * sizeof(ULONG_PTR);
                        char tag[16];
                        snprintf(tag, sizeof tag, "  [%02u] ", w);
                        zan__crash_modline(f, (void *)(*(ULONG_PTR *)p), tag);
                    }
                }
            }
        }
#elif defined(_M_ARM64) || defined(__aarch64__)
        if (ep->ContextRecord) {
            CONTEXT *c = ep->ContextRecord;
            fprintf(f, "pc=%p sp=%p lr=%p fp=%p\n",
                    (void *)c->Pc, (void *)c->Sp, (void *)c->Lr,
                    (void *)c->Fp);
            fprintf(f, "x0=%p x1=%p x2=%p x3=%p x4=%p x5=%p\n",
                    (void *)c->X0, (void *)c->X1, (void *)c->X2,
                    (void *)c->X3, (void *)c->X4, (void *)c->X5);
        }
#endif

        if (bt) {
            /* 捕获故障发生点调用栈帧 */
            fn = (USHORT)(btn < 62 ? btn : 62);
            for (USHORT i = 0; i < fn; i++) frames[i] = bt[i];
        } else {
            fn = RtlCaptureStackBackTrace(0, 62, frames, NULL);
        }
        if (fn == 0 && ep->ContextRecord)
            zan__crash_stack_scan(f, ZAN_CTX_SP(ep->ContextRecord), exe_base);
        fprintf(f, "backtrace (%u frames):\n", (unsigned)fn);
        for (USHORT i = 0; i < fn; i++) {
            char tag[16];
            snprintf(tag, sizeof tag, "  #%02u ", (unsigned)i);
            zan__crash_modline(f, frames[i], tag);
        }
        fprintf(f, "\n");
        fclose(f);

        /* 模块核心语义抽象与接口调用契约 */
        DWORD crash_code = er->ExceptionCode;
        if (crash_code != EXCEPTION_STACK_OVERFLOW &&
            crash_code != 0xC0000374 /* 核心系统底层抽象与内存语义契约 */ &&
            crash_code != 0xC0000409 /* fail-fast */) {
            zan__crash_symbolize(logpath, exe, exe_dir[0] ? exe_dir : ".\\",
                                 exe_base, ep->ExceptionRecord->ExceptionAddress,
                                 frames, fn, 0);
        }
        if (arc_freed_by)
            zan__crash_symbolize_one(logpath, exe, exe_dir[0] ? exe_dir : ".\\",
                                     exe_base, arc_freed_by);
    }
}

/* 内部辅助实现 */

/* 拷贝故障帧异常信息以供恢复帧写入日志 */
struct zan__fault {
    EXCEPTION_RECORD er;
    CONTEXT ctx;
    void *bt[62];
    unsigned btn;
    int have;
};

struct zan__guard {
    void *jb[5];             /* 核心系统底层抽象与内存语义契约 */
    struct zan__guard *prev; /* 核心系统底层抽象与内存语义契约 */
    ULONG_PTR sp;            /* 底层系统交互与数据协议契约 */
    volatile LONG armed;
    zan__fault_t f;
};

/* 内部辅助实现 */
#define ZAN_GUARD_LOG_FULL 20
#define ZAN_GUARD_LOG_EVERY 100

/* 内部辅助实现 */
#if defined(_M_X64) || defined(__x86_64__)
/* 模块核心语义抽象与接口调用契约 */
static int zan__guard_recoverable(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INVALID_OPERATION:
    /* 内部辅助实现 */
    case 0xE0A2C001: case 0xE0A2C002: case 0xE0A2C003: case 0xE0A2C004:
    case 0xE0A2C005: case 0xE0A2C006: case 0xE0A2C007: case 0xE0A2C008:
    case 0xE0A2C009: case 0xE0A2C010:
        return 1;
    default:
        return 0;
    }
}

/* 崩溃恢复放弃故障帧调用链，不触发析构展开 */
static int zan__guard_rip_recoverable(EXCEPTION_POINTERS *ep) {
    ULONG_PTR rip = ep->ContextRecord ? ZAN_CTX_PC(ep->ContextRecord) : 0;
    HMODULE m = NULL;
    if (!rip ||
        !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(ULONG_PTR)rip, &m))
        return 0;
    return (uintptr_t)m == (uintptr_t)GetModuleHandleA(NULL);
}
#endif /* 核心系统底层抽象与内存语义契约 */

/* 过滤并记录致命硬件异常 (AV/非法指令等) */
static int zan__guard_hard_fault(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
    case 0xC0000374:
    /* 底层系统交互与数据协议契约 */
    case 0xE0A2C001: case 0xE0A2C002: case 0xE0A2C003: case 0xE0A2C004:
    case 0xE0A2C005: case 0xE0A2C006: case 0xE0A2C007: case 0xE0A2C008:
    case 0xE0A2C009:
        return 1;
    default:
        return 0;
    }
}

#if defined(_M_X64) || defined(__x86_64__)
/* 内部辅助实现 */
static void zan__guard_resume(void) {
    zan__thread_slot *s = zan__slot(0);
    zan__guard_t *g = s ? s->top : NULL;
    if (!g) ExitProcess(0xE0A2C0FFu);
    __builtin_longjmp(g->jb, 1);
}
#endif

static void zan__crash_install(void);

#if defined(_M_X64) || defined(__x86_64__)
/* 恢复栈上写入崩溃记录日志 */
static void zan__guard_log_deferred(zan__thread_slot *s, zan__fault_t *flt) {
    if (!flt->have) return;
    flt->have = 0;
    if (zan__crash_busy_get(s)) return;

    LONG seen = InterlockedIncrement((LONG volatile *)&zan__guard_recovered);
    /* 线程崩溃恢复保护机制 */
    if (seen > ZAN_GUARD_LOG_FULL && seen % ZAN_GUARD_LOG_EVERY != 0) return;

    EXCEPTION_POINTERS p;
    p.ExceptionRecord = &flt->er;
    p.ContextRecord = &flt->ctx;
    zan__crash_busy_set(s, 1);
    zan__crash_write_record(&p, "recovered", flt->bt, flt->btn);
    zan__crash_busy_set(s, 0);
    InterlockedIncrement((LONG volatile *)&zan__guard_logged);
}
#endif /* x64 recovery */

/* 在崩溃恢复点保护下执行函数 (正常返回 1，捕获故障返回 0) */
static ZAN_MAYBE_UNUSED int zan__guard_call(void (*fn)(void *), void *arg) {
    if (!fn) return 1;
    zan__thread_slot *s = zan__slot(1);
    if (!s) { fn(arg); return 1; } /* out of slots: unguarded, as before */
    /* 模块核心语义抽象与接口调用契约 */
    if (!s->ready) {
        s->ready = 1;
        /* 重新确立全局未处理异常过滤器所有权 */
        SetUnhandledExceptionFilter(zan__crash_filter);
        zan__crash_install();
        /* 保护页后备栈空间，保证栈溢出时有足够栈深度写入日志 */
        ULONG guarantee = 64 * 1024;
        SetThreadStackGuarantee(&guarantee);
    }

#if defined(_M_X64) || defined(__x86_64__)
    zan__guard_t g;
    volatile char frame_probe = 0;
    g.prev = s->top;
    g.sp = (ULONG_PTR)&frame_probe;
    g.armed = 1;
    g.f.have = 0;
    int ok = 1;
    if (__builtin_setjmp(g.jb) == 0) {
        s->top = &g;
        fn(arg);
    } else {
        ok = 0;
        s->top = g.prev;
        zan__guard_log_deferred(s, &g.f);
    }
    s->top = g.prev;
    (void)frame_probe;
    return ok;
#else
    /* 内部辅助实现 */
    fn(arg);
    return 1;
#endif
}

static LONG CALLBACK zan__crash_veh(EXCEPTION_POINTERS *ep) {
    if (!ep || !ep->ExceptionRecord || !ep->ContextRecord)
        return EXCEPTION_CONTINUE_SEARCH;
    /* 未受保护线程故障处理 */
    zan__thread_slot *s = zan__slot(0);
    /* 递归二次故障防御，防止死循环 */
    if (zan__crash_busy_get(s)) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    zan__guard_t *g = s ? s->top : NULL;

#if defined(_M_X64) || defined(__x86_64__)
    if (g && g->armed && zan__guard_recoverable(code) &&
        zan__guard_rip_recoverable(ep)) {
        g->armed = 0; /* 模块核心语义抽象与接口调用契约 */
        /* 内部辅助实现 */
        g->f.er = *ep->ExceptionRecord;
        g->f.ctx = *ep->ContextRecord;
        g->f.btn = (unsigned)RtlCaptureStackBackTrace(0, 62, g->f.bt, NULL);
        g->f.have = 1;
        /* 内部辅助实现 */
        ULONG_PTR sp = ((g->sp - 256) & ~(ULONG_PTR)15) - 8;
        ep->ContextRecord->Rsp = (DWORD64)sp;
        ep->ContextRecord->Rip = (DWORD64)(ULONG_PTR)&zan__guard_resume;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
#endif

    /* 内部辅助实现 */
    if (zan__guard_hard_fault(code)) {
        LONG seen = InterlockedIncrement((LONG volatile *)&zan__guard_firstchance);
        if (seen <= ZAN_GUARD_LOG_FULL || seen % ZAN_GUARD_LOG_EVERY == 0) {
            zan__crash_busy_set(s, 1);
            zan__crash_write_record(ep, "first-chance", NULL, 0);
            zan__crash_busy_set(s, 0);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* 模块核心语义抽象与接口调用契约 */
static ZAN_MAYBE_UNUSED LONG zan__guard_recovered_count(void) {
    return zan__guard_recovered;
}

static void zan__crash_atexit(void) {
    /* 内部辅助实现 */
    char detail[96];
    snprintf(detail, sizeof detail, "recovered=%ld logged=%ld first-chance=%ld",
             (long)zan__guard_recovered, (long)zan__guard_logged,
             (long)zan__guard_firstchance);
    zan__crash_note("exit", detail);
}

/* 主程序实例标记（区分 DLL 宿主副本） */
static int zan__crash_in_main_image(void) {
    HMODULE m = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(ULONG_PTR)&zan__crash_in_main_image, &m))
        return 1; /* 核心系统底层抽象与内存语义契约 */
    return (uintptr_t)m == (uintptr_t)GetModuleHandleA(NULL);
}

static void zan__crash_install(void) {
#if defined(__SANITIZE_ADDRESS__)
    return;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
    return;
#endif
#endif
    /* 单进程内共享的向量化异常处理程序 */
    if (InterlockedCompareExchange((LONG volatile *)&zan__crash_installed,
                                   1, 0) != 0) return;
    SetUnhandledExceptionFilter(zan__crash_filter);
    /* 向量化异常处理优先于帧异常拦截 */
    AddVectoredExceptionHandler(1, zan__crash_veh);
    zan__crash_note("run", "");
    /* 底层系统交互与数据协议契约 */
    if (zan__crash_in_main_image()) atexit(zan__crash_atexit);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void zan__crash_ctor(void) {
    zan__crash_install();
}
#endif

#else /* !_WIN32 */
/* 内部辅助实现 */
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

#if (defined(__GLIBC__) || defined(__APPLE__)) && !defined(ZAN_NO_EXECINFO)
/* 内部辅助实现 */
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

/* 底层系统交互与数据协议契约 */
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

/* 内部辅助实现 */
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

/* 模块核心语义抽象与接口调用契约 */
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

/* 内部辅助实现 */
static char zan__crash_exe[4096];
static char zan__crash_logdir[4096];
/* 模块核心语义抽象与接口调用契约 */
static char zan__crash_wid[32];

/* 内部辅助实现 */
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

/* <logdir>/{yyyy}{MM}/{dd} */
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
    mkdir(zan__crash_logdir, 0755); /* 核心系统底层抽象与内存语义契约 */
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
    /* 编译器代码生成与运行时系统底层调用契约 */
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
    /* 模块核心语义抽象与接口调用契约 */
    signal(sig, SIG_DFL);
    raise(sig);
}

static void zan__crash_install(void) {
    static volatile int once = 0;
    if (once) return;
    once = 1;
    zan__crash_resolve_paths();
    zan__crash_cache_handler_state();
#if defined(__linux__) && !defined(SA_ONSTACK)
    /* 内部辅助实现 */
#define SA_ONSTACK 0x08000000
#endif
#if defined(__OHOS__) && !defined(SS_DISABLE)
    /* 模块核心语义抽象与接口调用契约 */
#define SS_DISABLE 2
int sigaltstack(const stack_t *__restrict, stack_t *__restrict);
#endif
    /* 内部辅助实现 */
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
        /* 模块核心语义抽象与接口调用契约 */
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
