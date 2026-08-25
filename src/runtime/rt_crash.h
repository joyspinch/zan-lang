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
 * to a symbolizer found next to the exe only (addr2line.exe /
 * llvm-symbolizer.exe) -- deliberately not PATH/CWD, which are
 * attacker-influenceable; if none is present the raw module+offset lines still
 * stand and can be resolved offline (scripts\symbolize_crash.ps1).
 *
 * Included by the always-linked runtime objects (rt_io.c reactor and the
 * gui_runtime DLL) so both console/async and GUI (ZanIDE) processes leave a
 * trace. Every symbol is static so multiple includers never collide at link
 * time, and installation is idempotent (only the first caller wins).
 *
 * Beyond logging, the file provides the fault guard (zan__guard_call, exported
 * as zan_gui_guard_call): work run through it survives a hard fault -- the
 * record is written and the callback abandoned, instead of the process dying.
 * The GUI loop wraps one event dispatch and one frame in it, so a bad pointer
 * or a failed bounds check in UI code costs that event or frame. Recording is
 * done by a vectored handler, which runs before any frame-based handler and
 * therefore also survives another component replacing the filter (an embedded
 * browser does), the case where a crash used to leave nothing at all behind.
 */
#ifndef ZAN_RT_CRASH_H
#define ZAN_RT_CRASH_H

#if defined(_WIN32)
#include <windows.h>
/* GetTickCount64 (used below): some MinGW header sets (TDM-GCC with the
 * plain -DZAN_GUI_STATIC build) hide its declaration behind a WINVER guard,
 * and windows.h may already be included before this header, so bumping
 * _WIN32_WINNT here would be too late. The prototype is a stable Win32 ABI;
 * a matching redeclaration is harmless where the header did declare it. */
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

/* Locate a symbolizer beside the exe, so a Debug publish is self-contained
 * and the crash path never executes a binary whose location depends on CWD
 * or PATH (either is attacker-influenceable; the search was removed rather
 * than hardened). Returns 1/2 for addr2line/llvm-symbolizer and writes the
 * program path into out. */
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
    /* Unpredictable name, created exclusively: a pid-only temp beside the exe
     * is trivially pre-created by another local process, and CREATE_ALWAYS
     * would then truncate whatever that handle holds -- poisoning the block
     * this function later appends into the crash log. Tick count plus an ASLR-
     * derived stack address give bits an outside process cannot guess; the
     * CREATE_NEW loop closes the residual collision window. */
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 8 && h == INVALID_HANDLE_VALUE; attempt++) {
        unsigned long long salt =
            (unsigned long long)GetTickCount64()
            ^ ((unsigned long long)(uintptr_t)&attempt << 17)
            ^ ((unsigned long long)GetCurrentProcessId() << 33)
            ^ ((unsigned long long)attempt * 0x9E3779B97F4A7C15ull);
        /* QPC beats the ~15 ms tick granularity for same-process crashes
         * that land within one tick of each other. */
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

/* <exe_dir>\zan_crash.log, plus the directory itself. Both fall back to the
 * bare file name in the current directory when the exe path is unavailable. */
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

/* One line into the crash log. Used for the markers that make "the program
 * vanished and there is no log" a distinguishable state: no file at all means
 * the image has no handler installed (or writes elsewhere), a file with only
 * a `run` line means the process left without any exception -- an exit()/
 * ExitProcess/TerminateProcess path rather than a crash. */
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

/* One instance of the handler state per binary, not per translation unit.
 * This header is included by several runtime objects (the timer, the IO
 * reactor, the GUI runtime), and a produced program links more than one of
 * them: with `static` state each copy got its own guard stack, so the guard a
 * GUI event was entered through was invisible to the vectored handler in
 * another copy -- the fault was logged as unrecoverable and the process died
 * with a perfectly good recovery point one frame below. selectany puts the
 * definitions in COMDATs the linker folds into one. */
#if defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#define ZAN_CRASH_SHARED __attribute__((selectany))
#elif defined(_MSC_VER)
#define ZAN_CRASH_SHARED __declspec(selectany)
#else
#define ZAN_CRASH_SHARED
#endif

/* Address range of the loaded main image, from its own PE headers. */
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

/* Return addresses left on the faulting stack, most frequent first.
 *
 * Used when no real backtrace could be taken -- a stack overflow leaves no
 * room to walk the stack from the handler. The overflowing call chain is still
 * written all over the stack, so scanning it upwards for values inside the
 * program's own code and counting them names the recursion: the few addresses
 * that repeat thousands of times are the cycle. */
static void zan__crash_stack_scan(FILE *f, ULONG_PTR rsp, uintptr_t exe_base) {
    uintptr_t lo, hi;
    zan__crash_image_range(exe_base, &lo, &hi);
    if (hi <= lo) return;

    /* Start clear of the low end of the stack: by the time this runs the guard
     * page has been re-armed somewhere below the current frame, and reading it
     * would raise the overflow a second time. The recursion repeats all the
     * way up, so skipping its innermost frames costs nothing. */
    ULONG_PTR start = rsp + 128 * 1024;

    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void *)start, &mbi, sizeof mbi)) return;
    if (mbi.State != MEM_COMMIT) return;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return;
    ULONG_PTR region_end = (ULONG_PTR)mbi.BaseAddress + mbi.RegionSize;

    /* Enough to cover a deep recursion without walking the whole stack. */
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

/* Per-thread state for the handlers below, in a plain table keyed by thread id
 * rather than in thread-local variables.
 *
 * A vectored handler runs on whatever thread faulted, and __thread there is a
 * trap: the access compiles to TEB->ThreadLocalStoragePointer[_tls_index],
 * which is null on threads whose static TLS block is not in place (a thread
 * still starting up, one already torn down, threads a foreign component brings
 * with it). Reading it then faults *inside the handler*, and that fault is
 * what kills the process -- with no record written, since writing one is
 * exactly what never got started. Two words of table lookup instead. */
typedef struct zan__guard zan__guard_t;
typedef struct zan__fault zan__fault_t;

typedef struct zan__thread_slot {
    volatile LONG tid;      /* owning thread id, 0 when free */
    zan__guard_t *top;      /* innermost guard on that thread */
    int ready;              /* one-time per-thread setup done */
    int busy;               /* a handler is writing a record right now */
} zan__thread_slot;

#define ZAN_GUARD_SLOTS 64

/* All the state the handlers share, in one COMDAT.
 *
 * It is deliberately one aggregate with a non-zero initialiser: a zeroed
 * selectany definition goes to .bss$name with gcc and to .data$name with
 * clang, and a program that links objects from both toolchains (the mingw-ABI
 * GUI runtime next to the clang-built runtime objects) then has two sections
 * the linker will not fold -- "multiple definition of zan__slots". `magic`
 * keeps the aggregate in initialised data for either compiler. */
typedef struct zan__shared {
    LONG magic;
    volatile LONG busy;         /* record being written on a thread with no slot */
    volatile LONG recovered;    /* faults recovered from, all threads */
    volatile LONG logged;       /* records written for them */
    volatile LONG firstchance;  /* hard faults seen that nothing recovered */
    volatile LONG installed;    /* handlers registered (once per binary) */
    zan__thread_slot slots[ZAN_GUARD_SLOTS];
} zan__shared_t;

ZAN_CRASH_SHARED zan__shared_t zan__shared = { 0x5A414353, 0, 0, 0, 0, 0, { { 0, 0, 0, 0 } } };

/* Spelled as the plain names the code below reads best with. */
#define zan__slots             zan__shared.slots
#define zan__crash_busy_shared zan__shared.busy
#define zan__guard_recovered   zan__shared.recovered
#define zan__guard_logged      zan__shared.logged
#define zan__guard_firstchance zan__shared.firstchance
#define zan__crash_installed   zan__shared.installed

/* Find this thread's slot, claiming a free one when `create`. Returns NULL if
 * the thread has none and none may be claimed -- the handlers treat that as
 * "nothing of ours is running here" and keep their hands off. */
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

/* Set while a handler is writing a record. A fault raised from inside the
 * handling of another one must not be handled again: on a stack overflow the
 * record writer itself (a few kilobytes of buffers) touches the guard page and
 * faults, and without this the vectored handler is re-entered for that fault,
 * faults again, and the process spins in KiUserExceptionDispatch until the
 * stack is gone -- a hang with an empty log instead of a crash. Threads
 * without a slot share one flag; they only ever reach the filter. */
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
        /* Generated code raised this only to get the record written; it exits
         * with its own status right after the raise, so resume there rather
         * than killing the process with a foreign exit code. */
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_EXECUTE_HANDLER; /* log, then terminate (skip WER dialog) */
}

/* Append one diagnostic record for `ep`. `reason` says how the exception was
 * seen: "unhandled" (the filter, process is about to die), "recovered" (the
 * fault guard below is about to abandon the faulting work and continue), or
 * "first-chance" (the vectored handler saw a hard fault that something else
 * may still handle -- logged because a hijacked filter would otherwise leave
 * no trace at all). */
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
            case 0xE0A2C007: arc = "retain of an already-freed array"; break;
            case 0xE0A2C008: arc = "release of an already-freed array"; break;
            case 0xE0A2C009: arc = "use of a freed array through a stale "
                                   "reference (missing retain when stored)";
                             break;
            default: break;
            }
            /* A failed runtime check (bounds, division by zero, a failed I/O
             * guard) raised by generated code. It prints the message too, but
             * a `--subsystem windows` program has no console, so the text
             * would otherwise be lost and the process would just vanish. The
             * pointer is a string literal in this same process. */
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
        }
#endif

        if (bt) {
            /* Captured where the fault happened; this call site may already be
             * the recovered frame, whose own stack says nothing. */
            fn = (USHORT)(btn < 62 ? btn : 62);
            for (USHORT i = 0; i < fn; i++) frames[i] = bt[i];
        } else {
            fn = RtlCaptureStackBackTrace(0, 62, frames, NULL);
        }
        if (fn == 0 && ep->ContextRecord)
            zan__crash_stack_scan(f, ep->ContextRecord->Rsp, exe_base);
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
         * us the addresses -- and skipped entirely after an overflow, where
         * this still runs on the stack that has just run out and spawning a
         * symbolizer is more than it has left. Also skipped when the heap or
         * fail-fast machinery reported itself broken: fopen/fprintf/spawn run
         * on CRT state those codes declare untrustworthy, and hanging there
         * costs more than the resolution is worth -- the module+offset lines
         * above still resolve offline via scripts\symbolize_crash.ps1. */
        DWORD crash_code = er->ExceptionCode;
        if (crash_code != EXCEPTION_STACK_OVERFLOW &&
            crash_code != 0xC0000374 /* heap corruption */ &&
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

/* ------------------------------------------------------------------ *
 * Fault guard
 *
 * A hard fault (or a failed runtime check) inside GUI work used to end the
 * process: no window, no message, and -- if some other component had replaced
 * the unhandled-exception filter meanwhile -- no log either. The guard makes
 * that survivable. `zan_rt_guard_call` runs a callback with a recovery point
 * recorded for the calling thread; the vectored handler below turns a fault
 * raised inside it into a jump back to that point, so the work in progress
 * (one frame, one event) is abandoned with a logged record and the loop keeps
 * running instead of the program disappearing.
 *
 * Not everything may be resumed this way: heap corruption and fail-fast
 * reports say the process state itself is no longer trustworthy, so those keep
 * falling through to the filter and terminate as before.
 *
 * The handler itself does as little as possible on the faulting stack: it
 * copies the exception out and jumps back, and the record is written from the
 * recovered frame. Writing it in place is what a stack that has just run out
 * cannot afford -- the writer's own buffers fault again, and the handler is
 * re-entered for that fault, which is how an ordinary crash turns into a
 * process that spins in the exception dispatcher and cannot even be killed.
 * ------------------------------------------------------------------ */

/* The exception, copied out of the faulting frame for the record the recovered
 * frame writes. Lives in the guard frame, which the fault left intact. */
struct zan__fault {
    EXCEPTION_RECORD er;
    CONTEXT ctx;
    void *bt[62];
    unsigned btn;
    int have;
};

struct zan__guard {
    void *jb[5];             /* __builtin_setjmp buffer (fp, sp, pc, ...) */
    struct zan__guard *prev; /* enclosing guard on this thread */
    ULONG_PTR sp;            /* stack pointer inside the guard frame */
    volatile LONG armed;
    zan__fault_t f;
};

/* Records written so far, and the ones after which the log thins out: a fault
 * that repeats every frame must not turn a 100 MB log file into the next
 * problem, while the beginning of the run -- where the first occurrence and
 * its backtrace are -- is always kept in full. */
#define ZAN_GUARD_LOG_FULL 20
#define ZAN_GUARD_LOG_EVERY 100

/* Codes it makes sense to abandon the current callback for. Deliberately
 * excludes EXCEPTION_STACK_OVERFLOW (there is no stack left to run on),
 * 0xC0000374 (heap corruption) and the fail-fast codes: continuing after those
 * buys a corrupted process rather than a working one. */
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
    /* Raised by generated code: the ARC integrity checks, and 0xE0A2C010 for
     * a failed runtime check (bounds, division by zero, an I/O guard), which
     * would otherwise exit() right after the raise. Inside a guard that means
     * losing one frame or one event instead of the program. */
    case 0xE0A2C001: case 0xE0A2C002: case 0xE0A2C003: case 0xE0A2C004:
    case 0xE0A2C005: case 0xE0A2C006: case 0xE0A2C007: case 0xE0A2C008:
    case 0xE0A2C009: case 0xE0A2C010:
        return 1;
    default:
        return 0;
    }
}

/* Recovery abandons the faulting frame chain without any unwind, so every
 * lock and invariant it held stays held. That trade is worth it for generated
 * Zan frames (they own no OS state across a message callback), but never for
 * foreign code: a fault inside another module's allocator or loader leaves,
 * say, its heap critical section owned, and the resumed event loop then
 * blocks forever on the next allocation -- a frozen-but-alive process with a
 * "recovered" line in the log. Restrict recovery to the main image, where
 * generated code and its runtime live; DLL faults die through the normal
 * unhandled path with a full record instead.
 *
 * Known limit: a statically linked CRT puts its heap INSIDE the main image,
 * so this check cannot see it. Guard recovery remains what it always was for
 * that case -- a deliberate bet that the abandoned frames held no OS lock. */
static int zan__guard_rip_recoverable(EXCEPTION_POINTERS *ep) {
    ULONG_PTR rip = ep->ContextRecord ? ep->ContextRecord->Rip : 0;
    HMODULE m = NULL;
    if (!rip ||
        !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(ULONG_PTR)rip, &m))
        return 0;
    return (uintptr_t)m == (uintptr_t)GetModuleHandleA(NULL);
}

/* Hard faults worth a record even when something else might still handle them:
 * the filter is the reliable place to log, but any component in the process
 * (an embedded browser, a plugin) can replace it after us. */
static int zan__guard_hard_fault(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
    case 0xC0000374:
    /* The ARC integrity reports from generated code. They are raised on
     * purpose and the raising code prints the facts, but the record is what
     * carries the backtrace -- and with a Debug build's line table, the source
     * line holding the stale reference. Worth writing here because nothing
     * else may: the raise is immediately followed by exit(), and a frame-based
     * handler anywhere below (the CRT installs one) can swallow it before the
     * unhandled-exception filter is ever consulted. */
    case 0xE0A2C001: case 0xE0A2C002: case 0xE0A2C003: case 0xE0A2C004:
    case 0xE0A2C005: case 0xE0A2C006: case 0xE0A2C007: case 0xE0A2C008:
    case 0xE0A2C009:
        return 1;
    default:
        return 0;
    }
}

/* Entered with a synthesized context (see the handler): a private stack and
 * this address in Rip, so the jump back runs on a frame the fault cannot have
 * damaged. __builtin_longjmp restores frame/stack/pc directly, without the
 * table-driven unwind msvcrt's longjmp would attempt from that frame. */
static void zan__guard_resume(void) {
    zan__thread_slot *s = zan__slot(0);
    zan__guard_t *g = s ? s->top : NULL;
    if (!g) ExitProcess(0xE0A2C0FFu);
    __builtin_longjmp(g->jb, 1);
}

static void zan__crash_install(void);

/* Write the record for the fault this thread just recovered from. Runs on the
 * restored stack, where a few kilobytes of buffers and a symbolizer are
 * affordable again. */
static void zan__guard_log_deferred(zan__thread_slot *s, zan__fault_t *flt) {
    if (!flt->have) return;
    flt->have = 0;
    if (zan__crash_busy_get(s)) return;

    LONG seen = InterlockedIncrement((LONG volatile *)&zan__guard_recovered);
    /* Always recover -- there is no fault count at which killing the program
     * is the better outcome -- but stop writing a full record for every one
     * once the pattern is established. */
    if (seen > ZAN_GUARD_LOG_FULL && seen % ZAN_GUARD_LOG_EVERY != 0) return;

    EXCEPTION_POINTERS p;
    p.ExceptionRecord = &flt->er;
    p.ContextRecord = &flt->ctx;
    zan__crash_busy_set(s, 1);
    zan__crash_write_record(&p, "recovered", flt->bt, flt->btn);
    zan__crash_busy_set(s, 0);
    InterlockedIncrement((LONG volatile *)&zan__guard_logged);
}

/* Run fn(arg) with a recovery point for this thread. Returns 1 when it
 * returned normally, 0 when a fault inside it was recovered from. */
static int zan__guard_call(void (*fn)(void *), void *arg) {
    if (!fn) return 1;
    zan__thread_slot *s = zan__slot(1);
    if (!s) { fn(arg); return 1; } /* out of slots: unguarded, as before */
    /* Once per thread: this is on the path of every window message. */
    if (!s->ready) {
        s->ready = 1;
        /* Re-assert ownership of the filter: a component loaded after us (CEF
         * and friends install their own) would otherwise take every unhandled
         * fault with it and leave no record behind. */
        SetUnhandledExceptionFilter(zan__crash_filter);
        zan__crash_install();
        /* Headroom below the guard page, so the handler still has a stack to
         * write its record on when the fault is the stack running out. */
        ULONG guarantee = 64 * 1024;
        SetThreadStackGuarantee(&guarantee);
    }

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
}

static LONG CALLBACK zan__crash_veh(EXCEPTION_POINTERS *ep) {
    if (!ep || !ep->ExceptionRecord || !ep->ContextRecord)
        return EXCEPTION_CONTINUE_SEARCH;
    /* No slot means no guard was ever entered on this thread: there is nothing
     * to recover into, but a hard fault there is still worth a record (the
     * faults before the first window message live here). */
    zan__thread_slot *s = zan__slot(0);
    /* A fault taken while handling one: handling it again is how the process
     * ends up spinning in the dispatcher instead of crashing. */
    if (zan__crash_busy_get(s)) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    zan__guard_t *g = s ? s->top : NULL;

#if defined(_M_X64) || defined(__x86_64__)
    if (g && g->armed && zan__guard_recoverable(code) &&
        zan__guard_rip_recoverable(ep)) {
        g->armed = 0; /* one recovery per guard entry, no re-entry from here */
        /* Everything expensive is deferred to the recovered frame; all that
         * happens here is copying the exception out (the backtrace too, which
         * only exists while the faulting frames are still on the stack) and
         * redirecting execution. On a stack overflow even the walk is out of
         * reach -- the record falls back to scanning the stack instead. */
        g->f.er = *ep->ExceptionRecord;
        g->f.ctx = *ep->ContextRecord;
        g->f.btn = (unsigned)RtlCaptureStackBackTrace(0, 62, g->f.bt, NULL);
        g->f.have = 1;
        /* Resume just below the guard's own frame: that memory is committed
         * (the abandoned callees were using it) and cannot overlap the frame
         * being jumped back into. x64 wants (rsp % 16) == 8 at a function's
         * first instruction. */
        ULONG_PTR sp = ((g->sp - 256) & ~(ULONG_PTR)15) - 8;
        ep->ContextRecord->Rsp = (DWORD64)sp;
        ep->ContextRecord->Rip = (DWORD64)(ULONG_PTR)&zan__guard_resume;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
#endif

    /* Nothing to recover into: leave the disposition alone and only leave a
     * trace, since the filter may no longer be ours by the time this dies.
     * Throttled like the recoveries -- a hard fault another component handles
     * on purpose (some do, while probing) would otherwise repeat forever. */
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

/* Number of faults the guard has absorbed so far (a UI can surface it). */
static LONG zan__guard_recovered_count(void) {
    return zan__guard_recovered;
}

static void zan__crash_atexit(void) {
    /* Reached on every ordinary shutdown too; the value is in the contrast --
     * a log whose last line is `run` and nothing else means the process was
     * killed outright (TerminateProcess, a fail-fast, a hard exit in native
     * code) rather than crashing where we could see it. */
    char detail[96];
    snprintf(detail, sizeof detail, "recovered=%ld logged=%ld first-chance=%ld",
             (long)zan__guard_recovered, (long)zan__guard_logged,
             (long)zan__guard_firstchance);
    zan__crash_note("exit", detail);
}

static void zan__crash_install(void) {
    /* Shared across the copies of this header in one binary: three vectored
     * handlers doing the same work only multiply the records. */
    if (InterlockedCompareExchange((LONG volatile *)&zan__crash_installed,
                                   1, 0) != 0) return;
    SetUnhandledExceptionFilter(zan__crash_filter);
    /* First in line: a vectored handler runs before any frame-based one, so a
     * fault is recorded (and recovered from) whatever else is installed. */
    AddVectoredExceptionHandler(1, zan__crash_veh);
    zan__crash_note("run", "");
    atexit(zan__crash_atexit);
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
        /* w < 0 with EAGAIN (non-blocking dup of stderr whose pipe is full)
         * drops the remainder on purpose: blocking here inside a signal
         * handler turns one wedged reader into a hung worker pool. */
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

/* State cached at install time because the signal handler must not touch
 * libc machinery that takes locks:
 *  - the timezone offset: localtime_r/strftime grab the tz lock and may read
 *    /etc/localtime; a crash on a thread holding that lock (the master's
 *    timestamp path runs continuously) would self-deadlock the handler.
 *  - a non-blocking dup of stderr: writing a full pipe from the handler
 *    blocks in-kernel forever if the reader stalled, hanging this worker
 *    instead of letting the supervisor respawn it. Partial records are
 *    dropped instead.
 *  - one warm-up call of backtrace(): glibc can dlopen libgcc_s on first
 *    use, which means malloc and the loader lock inside the handler. */
static long zan__crash_tz_off;
static int zan__crash_err_fd = -1;

static void zan__crash_cache_handler_state(void) {
    time_t now = time(NULL);
    struct tm g, l;
    if (gmtime_r(&now, &g) && localtime_r(&now, &l)) {
        zan__crash_tz_off =
            ((long)(l.tm_yday) - (long)(g.tm_yday)) * 86400L +
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

/* days-since-epoch -> civil date, proleptic Gregorian. Integer-only
 * (Hinnant's civil_from_days), safe to run inside a signal handler. */
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
    /* Local time from the install-time timezone offset: localtime_r and
     * strftime take the tz lock (and read /etc/localtime) and are not
     * async-signal-safe. */
    time_t now = time(NULL) + (time_t)zan__crash_tz_off;
    char stamp[32];
    /* Same broken-down time, kept for zan__crash_logpath_for()'s strftime
     * below -- rebuilt by hand because localtime_r is not async-signal-safe. */
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

    /* stderr first: a supervised worker has it redirected to its dated output
     * file, which is where the operator looks anyway. The write goes to the
     * non-blocking dup when there is one, so a full pipe cannot hang the
     * handler (the record still reaches the crash log below). */
    zan__crash_record(zan__crash_err_fd >= 0 ? zan__crash_err_fd
                                             : STDERR_FILENO,
                      sig, addr, stamp);

    char path[4096];
    if (zan__crash_logpath_for(&lt, path, sizeof path)) {
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
    zan__crash_cache_handler_state();
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
