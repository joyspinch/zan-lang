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

/* Append `function  file:line` for every in-module frame to the (closed) log
 * by spawning the symbolizer with stdout redirected onto the file. Best-effort:
 * any failure leaves the raw log intact. */
static void zan__crash_symbolize(const char *logpath, const char *exe,
                                 const char *exe_dir, uintptr_t exe_base,
                                 void *fault, void **frames, USHORT nframes) {
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

    /* Header line so the resolved block is easy to find; the child appends its
     * output after it, in the same order as the frames above. */
    FILE *hf = fopen(logpath, "ab");
    if (hf) {
        fprintf(hf, "resolved source locations (%d in-module frame(s), top first):\n",
                count);
        fclose(hf);
    }

    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    HANDLE h = CreateFileA(logpath, FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
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
    FILE *tf = fopen(logpath, "ab");
    if (tf) { fprintf(tf, "\n"); fclose(tf); }
}

static LONG WINAPI zan__crash_filter(EXCEPTION_POINTERS *ep) {
    if (!ep || !ep->ExceptionRecord) return EXCEPTION_EXECUTE_HANDLER;

    char logpath[MAX_PATH];
    char exe_dir[MAX_PATH];
    exe_dir[0] = '\0';
    DWORD n = GetModuleFileNameA(NULL, logpath, (DWORD)sizeof logpath);
    if (n == 0 || n >= sizeof logpath) {
        strcpy(logpath, "zan_crash.log");
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
            strcpy(logpath, "zan_crash.log");
        }
    }

    char exe[MAX_PATH];
    if (!GetModuleFileNameA(NULL, exe, (DWORD)sizeof exe)) exe[0] = '\0';
    uintptr_t exe_base = (uintptr_t)GetModuleHandleW(NULL);
    void *frames[62];
    USHORT fn = 0;

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
                             frames, fn);
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
static void zan__crash_install(void) {}
#endif

#endif /* ZAN_RT_CRASH_H */
