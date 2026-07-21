/* rt_crash.h -- persistent native crash logging.
 *
 * Installs a Windows unhandled-exception filter that appends a diagnostic
 * record to <exe_dir>\zan_crash.log on any hard crash (access violation, heap
 * corruption 0xC0000374, illegal instruction, etc.). Each record carries the
 * timestamp, exe path, process/thread ids, exception code/flags, the faulting
 * read/write/execute address, key registers, the faulting module+offset, and a
 * return-address backtrace as module+offset lines (feed those to addr2line /
 * llvm-symbolizer against the matching build to resolve function+line).
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

static LONG WINAPI zan__crash_filter(EXCEPTION_POINTERS *ep) {
    if (!ep || !ep->ExceptionRecord) return EXCEPTION_EXECUTE_HANDLER;

    char logpath[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, logpath, (DWORD)sizeof logpath);
    if (n == 0 || n >= sizeof logpath) {
        strcpy(logpath, "zan_crash.log");
    } else {
        char *slash = strrchr(logpath, '\\');
        if (slash) {
            slash[1] = '\0';
            strncat(logpath, "zan_crash.log",
                    sizeof(logpath) - strlen(logpath) - 1);
        } else {
            strcpy(logpath, "zan_crash.log");
        }
    }

    FILE *f = fopen(logpath, "ab");
    if (!f) f = fopen("zan_crash.log", "ab");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        EXCEPTION_RECORD *er = ep->ExceptionRecord;
        char exe[MAX_PATH];
        if (!GetModuleFileNameA(NULL, exe, (DWORD)sizeof exe)) exe[0] = '\0';

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

        void *frames[62];
        USHORT fn = RtlCaptureStackBackTrace(0, 62, frames, NULL);
        fprintf(f, "backtrace (%u frames):\n", (unsigned)fn);
        for (USHORT i = 0; i < fn; i++) {
            char tag[16];
            snprintf(tag, sizeof tag, "  #%02u ", (unsigned)i);
            zan__crash_modline(f, frames[i], tag);
        }
        fprintf(f, "\n");
        fclose(f);
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
