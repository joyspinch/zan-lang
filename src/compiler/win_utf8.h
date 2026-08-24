#ifndef ZAN_COMPILER_WIN_UTF8_H
#define ZAN_COMPILER_WIN_UTF8_H

/* Windows' narrow CRT APIs use the active ANSI code page, while Zan project
 * files and IDE response files are UTF-8. Keep compiler paths UTF-8 all the
 * way through LLVM and convert only at the Windows API boundary. */

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

static inline intptr_t zan_utf8_spawnv(int mode, const char *path,
                                       const char *const *argv) {
    int count = 0;
    while (argv[count]) count++;
    wchar_t *wide_path = zan_utf8_to_wide_alloc(path);
    wchar_t **wide_argv = (wchar_t **)calloc((size_t)count + 1,
                                             sizeof(wchar_t *));
    if (!wide_path || !wide_argv) {
        free(wide_path);
        free(wide_argv);
        return -1;
    }
    for (int i = 0; i < count; i++) {
        wide_argv[i] = zan_utf8_to_wide_alloc(argv[i]);
        if (!wide_argv[i]) {
            for (int j = 0; j < i; j++) free(wide_argv[j]);
            free(wide_argv);
            free(wide_path);
            return -1;
        }
    }
    intptr_t rc = _wspawnv(mode, wide_path,
                            (const wchar_t *const *)wide_argv);
    for (int i = 0; i < count; i++) free(wide_argv[i]);
    free(wide_argv);
    free(wide_path);
    return rc;
}

static inline int zan_utf8_system(const char *command) {
    wchar_t *wide = zan_utf8_to_wide_alloc(command);
    if (!wide) return -1;
    int rc = _wsystem(wide);
    free(wide);
    return rc;
}

/* The CRT's narrow argv is lossy outside the current ANSI code page. Recover
 * the original Windows command line and expose a UTF-8 argv to the compiler. */
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
