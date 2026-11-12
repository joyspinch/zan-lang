/* rt_crash_win_sym.c -- Windows 符号化器（addr2line / llvm-symbolizer）与 PE 栈分析 */
#include <wchar.h>

/* Older MinGW headers omit this Vista+ UTF-8 validation flag. */
#ifndef WC_ERR_INVALID_CHARS
#define WC_ERR_INVALID_CHARS 0x00000080
#endif

static void zan__crash_modline(zan__cf_t f, void *addr, const char *tag) {
    HMODULE m = NULL;
    char mp[MAX_PATH];
    if (addr &&
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)addr, &m) &&
        m && GetModuleFileNameA(m, mp, (DWORD)sizeof mp)) {
        zan__cf_printf(f, "%s%p  %s+0x%llX\n", tag, addr, mp,
                (unsigned long long)((char *)addr - (char *)m));
    } else {
        zan__cf_printf(f, "%s%p  <unknown>\n", tag, addr);
    }
}

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
    unsigned char *opt = nt + 24;
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

static int zan__crash_find_symbolizer(const char *exe_dir, char *out, size_t outsz) {
    char cand[MAX_PATH];
    zan__crash_snprintf(cand, sizeof cand, "%saddr2line.exe", exe_dir);
    if (GetFileAttributesA(cand) != INVALID_FILE_ATTRIBUTES) {
        zan__crash_snprintf(out, outsz, "%s", cand); return 1;
    }
    zan__crash_snprintf(cand, sizeof cand, "%sllvm-symbolizer.exe", exe_dir);
    if (GetFileAttributesA(cand) != INVALID_FILE_ATTRIBUTES) {
        zan__crash_snprintf(out, outsz, "%s", cand); return 2;
    }
    return 0;
}

static int zan__crash_line_is_noise(const char *line) {
    static const char *const noise[] = {
        "cygming-crtbegin.c", "__tmainCRTStartup", "WinMainCRTStartup",
        "mainCRTStartup", "__mingw_", "pre_c_init", "pre_cpp_init",
        "mingw-w64-crt", "crtexe.c", "crtdll.c", "crt_handler.c",
    };
    for (size_t i = 0; i < sizeof(noise) / sizeof(noise[0]); i++)
        if (strstr(line, noise[i])) return 1;
    if (line[0] == '?' && line[1] == '?') return 1;
    return 0;
}

/* 过滤符号化输出噪音行并写入崩溃日志 */
static int zan__crash_append_filtered(const char *logpath, const char *tmppath,
                                      int keep_all) {
    HANDLE in = CreateFileA(tmppath, GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (in == INVALID_HANDLE_VALUE) return 0;
    zan__cf_t out = zan__cf_open_append(logpath);
    if (!out.h) { CloseHandle(in); return 0; }
    /* ReadFile 分块读 + 行重组，替代 fgets（stdio 流锁） */
    char chunk[8192];
    char line[2048];
    size_t lp = 0;
    int kept = 0, eof = 0;
    while (!eof) {
        DWORD got = 0;
        if (!ReadFile(in, chunk, (DWORD)sizeof chunk, &got, NULL) || got == 0)
            eof = 1;
        for (DWORD i = 0; i < got; i++) {
            char c = chunk[i];
            if (lp + 1 < sizeof line) line[lp++] = c;
            if (c == '\n') {
                line[lp] = '\0';
                if (keep_all || !zan__crash_line_is_noise(line)) {
                    zan__cf_write(out, line, lp);
                    kept++;
                }
                lp = 0;
            }
        }
    }
    if (lp) { /* 末行无换行符 */
        line[lp] = '\0';
        if (keep_all || !zan__crash_line_is_noise(line)) {
            zan__cf_write(out, line, lp);
            kept++;
        }
    }
    zan__cf_close(out);
    CloseHandle(in);
    return kept;
}

static void zan__crash_symbolize(const char *logpath, const char *exe,
                                 const char *exe_dir, uintptr_t exe_base,
                                 void *fault, void **frames, USHORT nframes,
                                 int keep_all) {
    uintptr_t image_base = zan__crash_image_base(exe);
    if (!image_base) return;
    char sym[MAX_PATH];
    int mode = zan__crash_find_symbolizer(exe_dir, sym, sizeof sym);
    if (!mode) return;

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
        int w = zan__crash_snprintf(addrs + ap, sizeof(addrs) - ap, " 0x%llX",
                         (unsigned long long)vma);
        if (w <= 0 || (size_t)w >= sizeof(addrs) - ap) break;
        ap += (size_t)w;
        count++;
    }
    if (!count) return;

    char cmd[4096];
    if (mode == 1) /* addr2line */
        zan__crash_snprintf(cmd, sizeof cmd, "\"%s\" -e \"%s\" -f -C -i -p%s", sym, exe, addrs);
    else /* llvm-symbolizer */
        zan__crash_snprintf(cmd, sizeof cmd,
                 "\"%s\" --obj=\"%s\" --demangle --functions=linkage --inlines%s",
                 sym, exe, addrs);

    char tmppath[MAX_PATH];
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
        zan__crash_snprintf(tmppath, sizeof tmppath, "%szan_crash.sym.%lu.%016llx.tmp",
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
    zan__cf_t hf = zan__cf_open_append(logpath);
    if (hf.h) {
        zan__cf_printf(hf, "resolved source locations (%d in-module frame(s), top first%s):\n",
                count, keep_all ? "" : ", CRT frames omitted");
        zan__cf_close(hf);
    }
    if (zan__crash_append_filtered(logpath, tmppath, keep_all) == 0) {
        zan__cf_t nf = zan__cf_open_append(logpath);
        if (nf.h) {
            zan__cf_printf(nf, "  <no source locations: build with `zanc -g` to get "
                        "file:line; the module+offset backtrace above still "
                        "resolves offline via scripts\\symbolize_crash.ps1>\n");
            zan__cf_close(nf);
        }
    }
    DeleteFileA(tmppath);
    zan__cf_t tf = zan__cf_open_append(logpath);
    if (tf.h) { zan__cf_printf(tf, "\n"); zan__cf_close(tf); }
}

/* 解析单个隔离地址（ARC 保护检测） */
static void zan__crash_symbolize_one(const char *logpath, const char *exe,
                                    const char *exe_dir, uintptr_t exe_base,
                                    void *addr) {
    zan__cf_t hf = zan__cf_open_append(logpath);
    if (hf.h) { zan__cf_printf(hf, "released (arc.freed_by) at:\n"); zan__cf_close(hf); }
    /* 保留所有符号化行，包括合成析构函数 */
    zan__crash_symbolize(logpath, exe, exe_dir, exe_base, addr, NULL, 0, 1);
}

/* Resolve and create paths before installing handlers: no getenv, allocation or
 * recursive directory creation on the faulting thread. Never fall back to the
 * executable or working directory (SDKs and installed applications are payloads). */
static char zan__crash_cached_log[ZAN_CRASH_PATH_CAP];
static char zan__crash_cached_temp[ZAN_CRASH_PATH_CAP];

static int zan__crash_env_path(const wchar_t *name, wchar_t *out) {
    DWORD n = GetEnvironmentVariableW(name, out, ZAN_CRASH_PATH_CAP);
    if (!n || n >= ZAN_CRASH_PATH_CAP) { out[0] = 0; return 0; }
    return 1;
}

static int zan__crash_absolute(const wchar_t *path) {
    return (path[0] == L'\\' && path[1] == L'\\') ||
           (path[0] && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'));
}

static int zan__crash_path_suffix(wchar_t *out, const wchar_t *suffix) {
    size_t a = wcslen(out), b = wcslen(suffix);
    if (a + b >= ZAN_CRASH_PATH_CAP) return 0;
    memcpy(out + a, suffix, (b + 1) * sizeof(wchar_t));
    return 1;
}

static int zan__crash_prepare_log(const wchar_t *dir, char *out) {
    wchar_t full[ZAN_CRASH_PATH_CAP], wide[ZAN_CRASH_PATH_CAP];
    out[0] = 0;
    DWORD n = GetFullPathNameW(dir, ZAN_CRASH_PATH_CAP, full, NULL);
    if (!n || n >= ZAN_CRASH_PATH_CAP) return 0;
    for (DWORD i = 0; i < n; ++i) if (full[i] == L'/') full[i] = L'\\';
    if (wcsncmp(full, L"\\\\?\\", 4) == 0) {
        memcpy(wide, full, (n + 1) * sizeof(wchar_t));
    } else {
        const wchar_t *prefix = full[0] == L'\\' && full[1] == L'\\'
                                    ? L"\\\\?\\UNC\\" : L"\\\\?\\";
        const wchar_t *tail = full + (wcslen(prefix) == 8 ? 2 : 0);
        wide[0] = 0;
        if (!zan__crash_path_suffix(wide, prefix) ||
            !zan__crash_path_suffix(wide, tail)) return 0;
    }
    /* Skip the drive root, or the server/share root for UNC paths. */
    size_t start = 7;
    if (wcsncmp(wide, L"\\\\?\\UNC\\", 8) == 0) {
        start = 8;
        for (int part = 0; part < 2; ++part) {
            while (wide[start] && wide[start] != L'\\') ++start;
            if (wide[start]) ++start;
        }
    }
    for (size_t i = start; wide[i]; ++i) {
        if (wide[i] != L'\\') continue;
        wide[i] = 0;
        BOOL made = CreateDirectoryW(wide, NULL);
        DWORD err = GetLastError();
        wide[i] = L'\\';
        if (!made && err != ERROR_ALREADY_EXISTS) return 0;
    }
    if (!CreateDirectoryW(wide, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return 0;
    DWORD attr = GetFileAttributesW(wide);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return 0;
    if (!zan__crash_path_suffix(wide, L"\\zan_crash.log")) return 0;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, out,
                              ZAN_CRASH_PATH_CAP, NULL, NULL)) { out[0] = 0; return 0; }
    return 1;
}

static void zan__crash_resolve_paths(void) {
    wchar_t dir[ZAN_CRASH_PATH_CAP];
    zan__crash_cached_log[0] = zan__crash_cached_temp[0] = 0;
    DWORD n = GetTempPathW(ZAN_CRASH_PATH_CAP, dir);
    if (n && n < ZAN_CRASH_PATH_CAP && zan__crash_path_suffix(dir, L"Zan\\logs"))
        zan__crash_prepare_log(dir, zan__crash_cached_temp);
    if (zan__crash_env_path(L"ZAN_LOG_DIR", dir)) {
        zan__crash_prepare_log(dir, zan__crash_cached_log);
    } else if (zan__crash_env_path(L"LOCALAPPDATA", dir) && zan__crash_absolute(dir)) {
        if (zan__crash_path_suffix(dir, L"\\Zan\\logs"))
            zan__crash_prepare_log(dir, zan__crash_cached_log);
    } else if (zan__crash_env_path(L"USERPROFILE", dir) && zan__crash_absolute(dir)) {
        if (zan__crash_path_suffix(dir, L"\\AppData\\Local\\Zan\\logs"))
            zan__crash_prepare_log(dir, zan__crash_cached_log);
    }
}

static void zan__crash_logpath(char *logpath, size_t cap, char *exe_dir) {
    zan__crash_snprintf(logpath, cap, "%s", zan__crash_cached_log);
    exe_dir[0] = 0;
    DWORD n = GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    if (!n || n >= MAX_PATH) { exe_dir[0] = 0; return; }
    char *slash = strrchr(exe_dir, '\\');
    if (slash) slash[1] = 0;
    else exe_dir[0] = 0;
}

static void zan__crash_temp_logpath(char *logpath, size_t cap) {
    zan__crash_snprintf(logpath, cap, "%s", zan__crash_cached_temp);
}

/* 崩溃兜底日志的大小上限。
 * 日志随每次进程启动/退出各追加一行（run/exit），一次全量门禁能写进几千行、
 * 几 MB，真崩的那条记录会淹没在噪声里。超过上限就把旧文件轮转成 <name>.1，
 * 只保留最近一代：崩溃现场永远在当前文件里，历史不丢但有界。 */
#define ZAN_CRASH_LOG_MAX_BYTES (1024 * 1024)

/* 打开崩溃日志（追加模式），顺手做一次体积轮转。 */
static zan__cf_t zan__crash_open_log(const char *logpath) {
    zan__cf_t f = zan__cf_open_append(logpath);
    if (!f.h) return f;
    LARGE_INTEGER sz;
    if (GetFileSizeEx(f.h, &sz) && sz.QuadPart >= ZAN_CRASH_LOG_MAX_BYTES) {
        zan__cf_close(f);
        char old[ZAN_CRASH_PATH_CAP];
        wchar_t wide[ZAN_CRASH_PATH_CAP], wide_old[ZAN_CRASH_PATH_CAP];
        if (strlen(logpath) + 3 > sizeof old) return (zan__cf_t){ 0 };
        zan__crash_snprintf(old, sizeof old, "%s.1", logpath);
        if (!zan__crash_wpath(logpath, wide, ZAN_CRASH_PATH_CAP) ||
            !zan__crash_wpath(old, wide_old, ZAN_CRASH_PATH_CAP)) return (zan__cf_t){ 0 };
        DeleteFileW(wide_old);
        MoveFileW(wide, wide_old);
        f = zan__cf_open_append(logpath);
    }
    return f;
}

static void zan__crash_note(const char *what, const char *detail) {
    char logpath[ZAN_CRASH_PATH_CAP];
    char exe_dir[MAX_PATH];
    zan__crash_logpath(logpath, sizeof logpath, exe_dir);
    zan__cf_t f = zan__crash_open_log(logpath);
    if (!f.h) {
        zan__crash_temp_logpath(logpath, sizeof logpath);
        if (logpath[0]) f = zan__crash_open_log(logpath);
    }
    if (!f.h) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    zan__cf_printf(f, "---- %s %04d-%02d-%02d %02d:%02d:%02d.%03d pid=%lu tid=%lu %s\n",
            what, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
            st.wSecond, st.wMilliseconds,
            (unsigned long)GetCurrentProcessId(),
            (unsigned long)GetCurrentThreadId(), detail ? detail : "");
    zan__cf_close(f);
}

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

/* 返回 addresses left on the faulting stack, most frequent first */
static void zan__crash_stack_scan(zan__cf_t f, ULONG_PTR rsp, uintptr_t exe_base) {
    uintptr_t lo, hi;
    zan__crash_image_range(exe_base, &lo, &hi);
    if (hi <= lo) return;

    ULONG_PTR start = rsp + 128 * 1024;

    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void *)start, &mbi, sizeof mbi)) return;
    if (mbi.State != MEM_COMMIT) return;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return;
    ULONG_PTR region_end = (ULONG_PTR)mbi.BaseAddress + mbi.RegionSize;

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

    zan__cf_printf(f, "stack-scan (%lu in-image addresses, most frequent first --"
               " a repeated address is the recursion):\n", total);
    for (unsigned n = 0; n < used && n < 12; n++) {
        unsigned best = n;
        for (unsigned i = n + 1; i < used; i++)
            if (hits[i] > hits[best]) best = i;
        uintptr_t ta = addr[best]; unsigned long th = hits[best];
        addr[best] = addr[n]; hits[best] = hits[n];
        addr[n] = ta; hits[n] = th;
        char tag[32];
        zan__crash_snprintf(tag, sizeof tag, "  x%-6lu ", th);
        zan__crash_modline(f, (void *)ta, tag);
    }
}
