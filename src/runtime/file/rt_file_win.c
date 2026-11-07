/* rt_file_win.c: Windows (Win32) 原生文件系统与文件流底层实现 */

#ifndef _WIN32
#error "rt_file_win.c is only intended for Windows targets"
#endif

#if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0601
#undef _WIN32_WINNT
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>

static wchar_t *zan_win_utf8_to_wide(const char *text) {
    if (!text) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
                                NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *wide = (wchar_t *)malloc((size_t)n * sizeof(*wide));
    if (!wide) return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
                            wide, n) != n) { free(wide); return NULL; }
    return wide;
}

void *zan_file_fopen(const char *path, const char *mode) {
    if (!path || !mode) return NULL;
    wchar_t *wide_path = zan_win_utf8_to_wide(path);
    wchar_t *wide_mode = zan_win_utf8_to_wide(mode);
    if (!wide_path || !wide_mode) { free(wide_path); free(wide_mode); return NULL; }
    FILE *f = _wfopen(wide_path, wide_mode);
    free(wide_path); free(wide_mode);
    return f;
}

int zan_file_remove(const char *path) {
    if (!path || !path[0]) return -1;
    wchar_t *wide_path = zan_win_utf8_to_wide(path);
    if (!wide_path) return -1;
    int ok = DeleteFileW(wide_path) ? 0 : -1;
    free(wide_path);
    return ok;
}

int zan_file_rename(const char *source, const char *dest) {
    if (!source || !source[0] || !dest || !dest[0]) return -1;
    wchar_t *wide_source = zan_win_utf8_to_wide(source);
    wchar_t *wide_dest = zan_win_utf8_to_wide(dest);
    if (!wide_source || !wide_dest) { free(wide_source); free(wide_dest); return -1; }
    int ok = MoveFileExW(wide_source, wide_dest, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
    free(wide_source); free(wide_dest);
    return ok;
}

/* 文件元数据错误码口径规范（B-ID106 收敛）：
 * 1. zan_file_time: 成功返回 Unix 秒时间戳；路径不存在或读取失败返回 0
 * 2. zan_file_length: 成功返回文件字节数；路径不存在或读取失败返回 -1。
 * 3. zan_file_attributes: 成功返回属性位掩码；路径不存在或读取失败返回 -1。
 * 4. zan_file_set_readonly: 成功返回 0；失败返回 -1。
 */
long long zan_file_time(const char *path, int which) {
    if (!path || !path[0]) return 0;
    wchar_t *wide_path = zan_win_utf8_to_wide(path);
    if (!wide_path) return 0;
    WIN32_FILE_ATTRIBUTE_DATA d;
    int got = GetFileAttributesExW(wide_path, GetFileExInfoStandard, &d);
    free(wide_path);
    if (!got) return 0;
    FILETIME ft = d.ftLastWriteTime;
    if (which == 1) ft = d.ftCreationTime;
    else if (which == 2) ft = d.ftLastAccessTime;
    unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32)
                         | (unsigned long long)ft.dwLowDateTime;
    if (t == 0) return 0;
    return (long long)(t / 10000000ULL) - 11644473600LL;
}

long long zan_file_length(const char *path) {
    if (!path || !path[0]) return -1;
    wchar_t *wide_path = zan_win_utf8_to_wide(path);
    if (!wide_path) return -1;
    WIN32_FILE_ATTRIBUTE_DATA d;
    int got = GetFileAttributesExW(wide_path, GetFileExInfoStandard, &d);
    free(wide_path);
    if (!got) return -1;
    return (long long)(((unsigned long long)d.nFileSizeHigh << 32)
                       | (unsigned long long)d.nFileSizeLow);
}

static long long zan_file_attributes_at(const char *path) {
    if (!path || !path[0]) return -1;
    wchar_t *wide_path = zan_win_utf8_to_wide(path);
    if (!wide_path) return -1;
    DWORD a = GetFileAttributesW(wide_path);
    free(wide_path);
    if (a == INVALID_FILE_ATTRIBUTES) return -1;
    long long r = 0;
    if (a & FILE_ATTRIBUTE_READONLY)  r |= 1;
    if (a & FILE_ATTRIBUTE_HIDDEN)    r |= 2;
    if (a & FILE_ATTRIBUTE_DIRECTORY) r |= 4;
    return r;
}

long long zan_file_set_readonly(const char *path, int on) {
    if (!path || !path[0]) return 0;
    wchar_t *wide_path = zan_win_utf8_to_wide(path);
    if (!wide_path) return 0;
    DWORD a = GetFileAttributesW(wide_path);
    if (a == INVALID_FILE_ATTRIBUTES) { free(wide_path); return 0; }
    if (on) a |= FILE_ATTRIBUTE_READONLY;
    else    a &= ~(DWORD)FILE_ATTRIBUTE_READONLY;
    int ok = SetFileAttributesW(wide_path, a) ? 1 : 0;
    free(wide_path);
    return ok;
}

long long zan_file_set_time(const char *path, int which, long long unix_sec) {
    if (!path || !path[0]) return 0;
    if (which != 0 && which != 1 && which != 2) return 0;
    wchar_t *wide_path = zan_win_utf8_to_wide(path);
    if (!wide_path) return 0;
    HANDLE h = CreateFileW(wide_path, FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(wide_path);
    if (h == INVALID_HANDLE_VALUE) return 0;
    unsigned long long ticks = (unsigned long long)(unix_sec + 11644473600LL)
                             * 10000000ULL;
    FILETIME ft;
    ft.dwLowDateTime = (DWORD)(ticks & 0xFFFFFFFFULL);
    ft.dwHighDateTime = (DWORD)(ticks >> 32);
    int ok = 0;
    if (which == 0)      ok = SetFileTime(h, NULL, NULL, &ft);
    else if (which == 1) ok = SetFileTime(h, &ft, NULL, NULL);
    else                 ok = SetFileTime(h, NULL, &ft, NULL);
    CloseHandle(h);
    return ok ? 1 : 0;
}

/* 平台可执行文件路径获取 */
static void plat_get_executable_path(char *exe, size_t cap) {
    DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)cap);
    if (n == 0 || n >= cap) exe[0] = 0;
}

/* 平台 app_dir 首次探测同步原语 */
static volatile LONG g_appdir_resolved;
static void plat_appdir_acquire(void) {
    for (;;) {
        if (InterlockedCompareExchangeAcquire(&g_appdir_resolved, 0, 0) == 1)
            return;
        if (InterlockedCompareExchangeAcquire(&g_appdir_resolved, 2, 0) == 0)
            break;
        SwitchToThread();
    }
}
static int plat_appdir_is_resolved(void) {
    return InterlockedCompareExchangeAcquire(&g_appdir_resolved, 0, 0) == 1;
}
static void plat_appdir_release(void) {
    InterlockedCompareExchangeRelease(&g_appdir_resolved, 1, 2);
}

/* 平台 64 位文件定位与探针 */
static long long plat_file_seek_raw(FILE *f, long long offset, int whence) {
    return _fseeki64(f, (__int64)offset, whence) != 0 ? -1 : (long long)_ftelli64(f);
}

static long long plat_file_tell_raw(FILE *f) {
    return (long long)_ftelli64(f);
}

/* 平台句柄表互斥锁 */
static CRITICAL_SECTION g_fh_cs;
static INIT_ONCE g_fh_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK zan_fh_cs_init(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once; (void)param; (void)ctx;
    InitializeCriticalSection(&g_fh_cs);
    return TRUE;
}

static void zan_fh_lock(void) {
    InitOnceExecuteOnce(&g_fh_once, zan_fh_cs_init, NULL, NULL);
    EnterCriticalSection(&g_fh_cs);
}
static void zan_fh_unlock(void) { LeaveCriticalSection(&g_fh_cs); }

/* 平台底层排他锁持有句柄定义与操作 */
typedef HANDLE plat_file_lock_t;
#define PLAT_FILE_LOCK_INVALID NULL

static plat_file_lock_t plat_file_lock_open(const char *path) {
    wchar_t *wide_path = zan_win_utf8_to_wide(path);
    if (!wide_path) return PLAT_FILE_LOCK_INVALID;
    HANDLE h = CreateFileW(wide_path, GENERIC_WRITE, 0 /* no sharing */, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wide_path);
    if (h == INVALID_HANDLE_VALUE) return PLAT_FILE_LOCK_INVALID;
    return h;
}

static void plat_file_lock_close(plat_file_lock_t h) {
    if (h && h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
}
