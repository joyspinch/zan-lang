#ifndef ZAN_COMPILER_WIN_UTF8_H
#define ZAN_COMPILER_WIN_UTF8_H


#ifdef _WIN32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <process.h>

static inline wchar_t *zan_utf8_to_wide_alloc(const char *text) {
    if (!text) return NULL;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
                                    NULL, 0);
    if (count <= 0) return NULL;
    wchar_t *wide = (wchar_t *)malloc((size_t)count * sizeof(wchar_t));
    if (!wide) return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
                            wide, count) <= 0) {
        free(wide);
        return NULL;
    }
    return wide;
}

static inline char *zan_wide_to_utf8_alloc(const wchar_t *wide) {
    if (!wide) return NULL;
    int count = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0,
                                    NULL, NULL);
    if (count <= 0) return NULL;
    char *text = (char *)malloc((size_t)count);
    if (!text) return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, count,
                            NULL, NULL) <= 0) {
        free(text);
        return NULL;
    }
    return text;
}

static inline FILE *zan_utf8_fopen(const char *path, const char *mode) {
    wchar_t *wide_path = zan_utf8_to_wide_alloc(path);
    wchar_t wide_mode[16];
    size_t i = 0;
    if (!wide_path) return NULL;
    while (mode[i] && i + 1 < sizeof(wide_mode) / sizeof(wide_mode[0])) {
        wide_mode[i] = (wchar_t)(unsigned char)mode[i];
        i++;
    }
    wide_mode[i] = L'\0';
    FILE *file = _wfopen(wide_path, wide_mode);
    free(wide_path);
    return file;
}

static inline int zan_utf8_remove(const char *path) {
    wchar_t *wide = zan_utf8_to_wide_alloc(path);
    if (!wide) return -1;
    int rc = _wremove(wide);
    free(wide);
    return rc;
}

static inline int zan_utf8_rename(const char *from, const char *to) {
    wchar_t *wide_from = zan_utf8_to_wide_alloc(from);
    wchar_t *wide_to = zan_utf8_to_wide_alloc(to);
    if (!wide_from || !wide_to) {
        free(wide_from);
        free(wide_to);
        return -1;
    }
    int rc = _wrename(wide_from, wide_to);
    free(wide_from);
    free(wide_to);
    return rc;
}

static inline DWORD zan_utf8_get_file_attributes(const char *path) {
    wchar_t *wide = zan_utf8_to_wide_alloc(path);
    if (!wide) return INVALID_FILE_ATTRIBUTES;
    DWORD attributes = GetFileAttributesW(wide);
    free(wide);
    return attributes;
}

static inline int zan_utf8_is_directory(const char *path) {
    DWORD attributes = zan_utf8_get_file_attributes(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static inline int zan_utf8_full_path(const char *path, char *out, size_t out_size) {
    wchar_t *wide = zan_utf8_to_wide_alloc(path);
    if (!wide) return 0;
    DWORD needed = GetFullPathNameW(wide, 0, NULL, NULL);
    if (needed == 0) { free(wide); return 0; }
    wchar_t *full = (wchar_t *)malloc((size_t)needed * sizeof(wchar_t));
    if (!full) { free(wide); return 0; }
    DWORD written = GetFullPathNameW(wide, needed, full, NULL);
    free(wide);
    if (written == 0 || written >= needed) { free(full); return 0; }
    char *utf8 = zan_wide_to_utf8_alloc(full);
    free(full);
    if (!utf8) return 0;
    size_t len = strlen(utf8);
    if (len + 1 > out_size) { free(utf8); return 0; }
    memcpy(out, utf8, len + 1);
    free(utf8);
    return 1;
}

/* _wspawnv 的命令行拼接由 msvcrt 完成，含空格的实参不会被可靠地加引号
 * ——实测 `--libpath "D:\dir with space"` 到 ld.exe 被按空格劈成三个
 * 实参（"cannot find with/space"），链接静默换语义。这里按
 * CommandLineToArgvW 的拆分规则自行拼接命令行（含空格/tab/引号/空串
 * 的实参加引号，实参内引号翻倍、引号前反斜杠翻倍、结尾反斜杠翻倍）
 * 并直接 CreateProcessW：与子进程 CRT 的拆分规则严格互逆，任何实参
 * 都原样到达。调用点全部是 _P_WAIT，返回退出码（_P_NOWAIT 兼容路径
 * 返回进程句柄，当前无人使用）。 */
static inline intptr_t zan_utf8_spawnv(int mode, const char *path,
                                       const char *const *argv) {
    (void)path; /* 程序名以 argv[0] 进命令行首 token，见下方 CreateProcessW */
    int count = 0;
    while (argv[count]) count++;
    /* 最坏情形：每个字节翻倍（引号/反斜杠）+ 每实参一对引号 + 分隔符 */
    size_t cap = 8;
    for (int i = 0; i < count; i++) {
        cap += strlen(argv[i]) * 2 + 4;
    }
    char *cl = (char *)malloc(cap);
    if (!cl) return -1;
    size_t pos = 0;
    for (int i = 0; i < count; i++) {
        const char *a = argv[i];
        int needs_quote = (a[0] == '\0') ||
                          strpbrk(a, " \t\"") != NULL;
        if (needs_quote) cl[pos++] = '"';
        size_t backslashes = 0;
        for (const char *p = a; *p; p++) {
            if (*p == '\\') {
                backslashes++;
                continue;
            }
            if (*p == '"') {
                /* 引号前的反斜杠翻倍，引号本身翻倍 */
                for (size_t k = 0; k < backslashes * 2; k++) cl[pos++] = '\\';
                backslashes = 0;
                cl[pos++] = '"';
                cl[pos++] = '"';
                continue;
            }
            for (size_t k = 0; k < backslashes; k++) cl[pos++] = '\\';
            backslashes = 0;
            cl[pos++] = *p;
        }
        if (needs_quote) {
            /* 引号包裹的实参，结尾反斜杠也要翻倍（防吃掉收尾引号） */
            for (size_t k = 0; k < backslashes * 2; k++) cl[pos++] = '\\';
            cl[pos++] = '"';
        } else {
            for (size_t k = 0; k < backslashes; k++) cl[pos++] = '\\';
        }
        if (i + 1 < count) cl[pos++] = ' ';
    }
    cl[pos] = '\0';
    wchar_t *wide_cl = zan_utf8_to_wide_alloc(cl);
    free(cl);
    if (!wide_cl) {
        free(wide_cl);
        return -1;
    }
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    /* lpApplicationName 必须留 NULL：程序名以命令行首 token 传递
     * （argv[0]，调用点已设置）——裸名（如 "ld.lld"）才会走 PATH
     * 搜索与 .exe 补全，与 _wspawnv 的语义一致；给 lpApplicationName
     * 传裸名会直接 CreateProcess 失败。 */
    if (!CreateProcessW(NULL, wide_cl, NULL, NULL, FALSE, 0,
                        NULL, NULL, &si, &pi)) {
        free(wide_cl);
        return -1;
    }
    free(wide_cl);
    CloseHandle(pi.hThread);
    if (mode == _P_WAIT) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        return (intptr_t)code;
    }
    return (intptr_t)pi.hProcess;
}

static inline int zan_utf8_system(const char *command) {
    wchar_t *wide = zan_utf8_to_wide_alloc(command);
    if (!wide) return -1;
    int rc = _wsystem(wide);
    free(wide);
    return rc;
}

static inline char **zan_utf8_command_line_argv(int *out_argc) {
    typedef LPWSTR *(WINAPI *command_line_to_argv_w_fn)(LPCWSTR, int *);
    HMODULE shell32 = LoadLibraryW(L"shell32.dll");
    if (!shell32) return NULL;
    command_line_to_argv_w_fn parse = (command_line_to_argv_w_fn)(void *)
        GetProcAddress(shell32, "CommandLineToArgvW");
    if (!parse) { FreeLibrary(shell32); return NULL; }
    int argc = 0;
    LPWSTR *wide_argv = parse(GetCommandLineW(), &argc);
    if (!wide_argv || argc <= 0) {
        if (wide_argv) LocalFree(wide_argv);
        FreeLibrary(shell32);
        return NULL;
    }
    char **argv = (char **)calloc((size_t)argc + 1, sizeof(char *));
    if (!argv) {
        LocalFree(wide_argv);
        FreeLibrary(shell32);
        return NULL;
    }
    for (int i = 0; i < argc; i++) {
        argv[i] = zan_wide_to_utf8_alloc(wide_argv[i]);
        if (!argv[i]) {
            for (int j = 0; j < i; j++) free(argv[j]);
            free(argv);
            LocalFree(wide_argv);
            FreeLibrary(shell32);
            return NULL;
        }
    }
    LocalFree(wide_argv);
    FreeLibrary(shell32);
    *out_argc = argc;
    return argv;
}

#endif /* _WIN32 */

#endif /* ZAN_COMPILER_WIN_UTF8_H */
