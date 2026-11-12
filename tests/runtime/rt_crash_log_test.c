/* 崩溃兜底日志的体积上限回归。
 *
 * zan_crash.log 随每次进程启动/退出各追加一行，一轮全量门禁能写进几 MB，
 * 真崩的那条记录会淹没在噪声里。zan__crash_open_log() 在打开时查文件体积，
 * 超过 ZAN_CRASH_LOG_MAX_BYTES 就把旧文件轮转成 <name>.1，只保留最近一代：
 * 崩溃现场永远在当前文件里，历史不丢但有界。
 *
 * 轮转逻辑在 rt_crash.c 里仍是 static 函数（B-ID106 拆出后头文件只留
 * 导出原型），单测直接 include 该 .c 来测。
 * 该头文件的文件日志路径只在 Windows 分支实现，POSIX 分支走 signal+管道，
 * 所以本用例只编 Windows。
 */
#if defined(_WIN32)

#include "rt_crash.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL(msg)                                                          \
    do {                                                                   \
        fprintf(stderr, "FAIL: %s\n", (msg));                              \
        return 1;                                                          \
    } while (0)

static const char *k_log = "rt_crash_log_test.log";

static long file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

static int write_blob(const char *path, long n) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    for (long i = 0; i < n; i++) fputc('x', f);
    fclose(f);
    return 1;
}

/* All fixtures are under a fresh owned temp directory, including Unicode and
 * >MAX_PATH paths. A resolver failure must never select exe/cwd as a fallback. */
static void remove_tree(wchar_t *path) {
    size_t n = wcslen(path);
    wcscpy(path + n, L"\\*");
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(path, &data);
    path[n] = 0;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
            wcscpy(path + n, L"\\");
            wcscpy(path + n + 1, data.cFileName);
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree(path);
            else DeleteFileW(path);
            path[n] = 0;
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    RemoveDirectoryW(path);
}

static int path_cases(void) {
    wchar_t tmp[ZAN_CRASH_PATH_CAP], root[ZAN_CRASH_PATH_CAP], dir[ZAN_CRASH_PATH_CAP];
    DWORD n = GetTempPathW(ZAN_CRASH_PATH_CAP, tmp);
    if (!n || n >= ZAN_CRASH_PATH_CAP) FAIL("temp unavailable");
    swprintf(root, ZAN_CRASH_PATH_CAP, L"\\\\?\\%lszan-crash-path-test-%lu", tmp,
             (unsigned long)GetCurrentProcessId());
    if (!CreateDirectoryW(root, NULL)) FAIL("owned path fixture exists or cannot be created");
    SetEnvironmentVariableW(L"LOCALAPPDATA", root);
    SetEnvironmentVariableW(L"ZAN_LOG_DIR", NULL);
    zan__crash_resolve_paths();
    if (!strstr(zan__crash_cached_log, "\\Zan\\logs\\zan_crash.log")) FAIL("local cache default");
    zan__cf_t f = zan__crash_open_log(zan__crash_cached_log);
    if (!f.h) FAIL("default cache log unavailable");
    zan__cf_close(f);

    wcscpy(dir, root);
    wcscat(dir, L"\\custom-\x6d4b\x8bd5");
    SetEnvironmentVariableW(L"ZAN_LOG_DIR", dir);
    zan__crash_resolve_paths();
    if (!strstr(zan__crash_cached_log, "custom-")) FAIL("explicit log directory ignored");
    f = zan__crash_open_log(zan__crash_cached_log);
    if (!f.h) FAIL("Unicode log open failed");
    zan__cf_puts(f, "unicode-path\n");
    zan__cf_close(f);
    wcscat(dir, L"\\zan_crash.log");
    if (GetFileAttributesW(dir) == INVALID_FILE_ATTRIBUTES) FAIL("Unicode destination missing");

    wcscpy(dir, root);
    for (int i = 0; i < 12; ++i) wcscat(dir, L"\\long-directory-component-1234567890");
    SetEnvironmentVariableW(L"ZAN_LOG_DIR", dir);
    zan__crash_resolve_paths();
    if (strlen(zan__crash_cached_log) <= MAX_PATH) FAIL("long destination not selected");
    f = zan__crash_open_log(zan__crash_cached_log);
    if (!f.h) FAIL("long path log open failed");
    zan__cf_close(f);
    /* Environment changes cannot move a fault handler's cached destination. */
    SetEnvironmentVariableW(L"ZAN_LOG_DIR", L"ignored-after-install");
    if (strstr(zan__crash_cached_log, "ignored-after-install")) FAIL("path was not frozen");

    wcscpy(dir, root);
    wcscat(dir, L"\\blocker");
    HANDLE blocker = CreateFileW(dir, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    if (blocker == INVALID_HANDLE_VALUE) FAIL("blocker fixture failed");
    CloseHandle(blocker);
    wcscat(dir, L"\\cannot-create");
    SetEnvironmentVariableW(L"ZAN_LOG_DIR", dir);
    zan__crash_resolve_paths();
    if (zan__crash_cached_log[0]) FAIL("failed directory retained a primary path");
    if (!zan__crash_cached_temp[0] || !strstr(zan__crash_cached_temp, "\\Zan\\logs\\zan_crash.log"))
        FAIL("failed directory lacks user temp fallback");
    if (!zan__crash_prepare_log(root, zan__crash_cached_log)) FAIL("fixture log resolution failed");
    zan__crash_note("path-test", "");
    wchar_t log[ZAN_CRASH_PATH_CAP];
    if (!zan__crash_wpath(zan__crash_cached_log, log, ZAN_CRASH_PATH_CAP)) FAIL("invalid cached path");
    WIN32_FILE_ATTRIBUTE_DATA before, after;
    if (!GetFileAttributesExW(log, GetFileExInfoStandard, &before)) FAIL("note log missing");
    /* A real record must append bytes to the cached note destination. */
    EXCEPTION_RECORD record = {0};
    CONTEXT context = {0};
    EXCEPTION_POINTERS exception = { &record, &context };
    record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    zan__crash_write_record(&exception, "path-test", NULL, 0);
    if (!GetFileAttributesExW(log, GetFileExInfoStandard, &after)) FAIL("record log missing");
    ULONGLONG note_size = ((ULONGLONG)before.nFileSizeHigh << 32) | before.nFileSizeLow;
    ULONGLONG record_size = ((ULONGLONG)after.nFileSizeHigh << 32) | after.nFileSizeLow;
    if (record_size <= note_size) FAIL("record must append bytes after the note");
    zan__crash_cached_log[0] = zan__crash_cached_temp[0] = 0;
    remove_tree(root);
    printf("ok: crash log user cache, override, Unicode, long path, failure fallback, record\n");
    return 0;
}

int main(void) {
    if (path_cases()) return 1;
    char rotated[256];
    snprintf(rotated, sizeof rotated, "%s.1", k_log);
    remove(rotated);
    remove(k_log);

    /* 1) 未超限：不动，追加内容留在原文件里。 */
    if (!write_blob(k_log, 1024)) FAIL("cannot write small log");
    zan__cf_t f = zan__crash_open_log(k_log);
    if (!f.h) FAIL("open failed on small log");
    zan__cf_puts(f, "small\n");
    zan__cf_close(f);
    if (file_size(rotated) >= 0) FAIL("small log must not rotate");
    if (file_size(k_log) != 1024 + 6) FAIL("small log content changed");

    /* 2) 超限：旧内容整体搬进 .1，当前文件清空重开。 */
    if (!write_blob(k_log, ZAN_CRASH_LOG_MAX_BYTES + 4096))
        FAIL("cannot write big log");
    long big = file_size(k_log);
    f = zan__crash_open_log(k_log);
    if (!f.h) FAIL("open failed on big log");
    zan__cf_puts(f, "after-rotation\n");
    zan__cf_close(f);
    if (file_size(rotated) != big) FAIL("rotated file must keep the old content");
    if (file_size(k_log) != 15) FAIL("current log must start empty after rotation");

    /* 3) 只保留一代：再超限时上一代 .1 被丢弃，换成新的。 */
    if (!write_blob(k_log, ZAN_CRASH_LOG_MAX_BYTES + 8192))
        FAIL("cannot rewrite big log");
    long bigger = file_size(k_log);
    f = zan__crash_open_log(k_log);
    if (!f.h) FAIL("open failed on second big log");
    zan__cf_close(f);
    if (file_size(rotated) != bigger)
        FAIL("only one generation is kept: stale .1 was not replaced");
    if (file_size(rotated) == big) FAIL("rotated file was not refreshed");

    remove(rotated);
    remove(k_log);
    printf("ok: crash log rotation\n");
    return 0;
}

#else /* !_WIN32 */

#include "rt_crash.c"
#include <dirent.h>

static void remove_tree(char *path) {
    size_t n = strlen(path);
    DIR *dir = opendir(path);
    struct dirent *entry;
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            snprintf(path + n, 4096 - n, "/%s", entry->d_name);
            struct stat st;
            if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) remove_tree(path);
            else unlink(path);
            path[n] = 0;
        }
        closedir(dir);
    }
    rmdir(path);
}

int main(void) {
    char root[4096] = "/tmp/zan-crash-path-test-XXXXXX";
    if (!mkdtemp(root)) return 1;
    char home[4096], cache[4096], explicit_dir[4096], expected[4096], blocker[4096];
    snprintf(home, sizeof home, "%s/home", root);
    snprintf(cache, sizeof cache, "%s/xdg", root);
    snprintf(explicit_dir, sizeof explicit_dir, "%s/explicit", root);
    setenv("HOME", home, 1);
    setenv("XDG_CACHE_HOME", cache, 1);
    unsetenv("ZAN_LOG_DIR");
    zan__crash_resolve_paths();
#if defined(__APPLE__)
    snprintf(expected, sizeof expected, "%s/Library/Caches/Zan/logs", home);
#else
    snprintf(expected, sizeof expected, "%s/zan/logs", cache);
#endif
    if (strcmp(expected, zan__crash_logdir)) return 2;
    unsetenv("XDG_CACHE_HOME");
    zan__crash_resolve_paths();
#if !defined(__APPLE__)
    snprintf(expected, sizeof expected, "%s/.cache/zan/logs", home);
#endif
    if (strcmp(expected, zan__crash_logdir)) return 3;
    setenv("ZAN_LOG_DIR", explicit_dir, 1);
    zan__crash_resolve_paths();
    if (strcmp(explicit_dir, zan__crash_logdir)) return 4;
    struct tm day = {0};
    day.tm_year = 126; day.tm_mon = 10; day.tm_mday = 12;
    char path[4096];
    if (!zan__crash_logpath_for(&day, path, sizeof path) ||
        !strstr(path, "/202611/12.log")) return 5;
    int fd = open(path, O_WRONLY | O_CREAT, 0600);
    if (fd < 0) return 6;
    close(fd);
    snprintf(blocker, sizeof blocker, "%s/blocker", root);
    fd = open(blocker, O_WRONLY | O_CREAT, 0600);
    if (fd < 0) return 7;
    close(fd);
    snprintf(expected, sizeof expected, "%s/blocker/child", root);
    setenv("ZAN_LOG_DIR", expected, 1);
    setenv("TMPDIR", root, 1);
    zan__crash_resolve_paths();
    snprintf(expected, sizeof expected, "%s/zan-%lu/logs", root, (unsigned long)getuid());
    if (strcmp(expected, zan__crash_logdir)) return 8;
    remove_tree(root);
    puts("ok: crash log XDG/HOME defaults, override, dated layout, failure fallback");
    return 0;
}

#endif
