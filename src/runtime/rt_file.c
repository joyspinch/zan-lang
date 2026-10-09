/* 内部辅助实现 */

#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
/* 编译器代码生成与运行时系统底层调用契约 */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif

/* 内部辅助逻辑 */
#if !defined(_WIN32) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif
#if !defined(_WIN32) && !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef __wasm__
#include <sys/file.h>      /* flock */
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>   /* _NSGetExecutablePath */
#endif
#endif

#if !defined(_WIN32) && !defined(O_NOFOLLOW)
#define O_NOFOLLOW 0
#endif
#if !defined(_WIN32) && !defined(O_CLOEXEC)
#define O_CLOEXEC 0
#endif

/* 核心系统底层抽象与内存语义契约 */
#ifdef _WIN32
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
#endif

void *zan_file_fopen(const char *path, const char *mode) {
    if (!path || !mode) return NULL;
#ifdef _WIN32
    wchar_t *wide_path = zan_win_utf8_to_wide(path);
    wchar_t *wide_mode = zan_win_utf8_to_wide(mode);
    if (!wide_path || !wide_mode) { free(wide_path); free(wide_mode); return NULL; }
    FILE *f = _wfopen(wide_path, wide_mode);
    free(wide_path); free(wide_mode);
    return f;
#else
    return fopen(path, mode);
#endif
}

int zan_file_remove(const char *path) {
    if (!path || !path[0]) return -1;
#ifdef _WIN32
    wchar_t *wide_path = zan_win_utf8_to_wide(path);
    if (!wide_path) return -1;
    int ok = DeleteFileW(wide_path) ? 0 : -1;
    free(wide_path);
    return ok;
#else
    return remove(path);
#endif
}

int zan_file_rename(const char *source, const char *dest) {
    if (!source || !source[0] || !dest || !dest[0]) return -1;
#ifdef _WIN32
    wchar_t *wide_source = zan_win_utf8_to_wide(source);
    wchar_t *wide_dest = zan_win_utf8_to_wide(dest);
    if (!wide_source || !wide_dest) { free(wide_source); free(wide_dest); return -1; }
    int ok = MoveFileExW(wide_source, wide_dest, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
    free(wide_source); free(wide_dest);
    return ok;
#else
    return rename(source, dest);
#endif
}

/* 内部辅助实现 */

/* 核心系统底层抽象与内存语义契约 */
long long zan_file_time(const char *path, int which) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
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
    /* 核心系统底层抽象与内存语义契约 */
    return (long long)(t / 10000000ULL) - 11644473600LL;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (which == 1) return (long long)st.st_ctime;
    if (which == 2) return (long long)st.st_atime;
    return (long long)st.st_mtime;
#endif
}

/* 模块核心语义抽象与接口调用契约 */
long long zan_file_length(const char *path) {
    if (!path || !path[0]) return -1;
#ifdef _WIN32
    wchar_t *wide_path = zan_win_utf8_to_wide(path);
    if (!wide_path) return -1;
    WIN32_FILE_ATTRIBUTE_DATA d;
    int got = GetFileAttributesExW(wide_path, GetFileExInfoStandard, &d);
    free(wide_path);
    if (!got) return -1;
    return (long long)(((unsigned long long)d.nFileSizeHigh << 32)
                       | (unsigned long long)d.nFileSizeLow);
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long long)st.st_size;
#endif
}

/* 内部辅助实现 */

/* 内部辅助逻辑 */
#if defined(_WIN32)
static volatile LONG g_appdir_resolved;
#elif defined(__wasm__)
static int g_appdir_resolved;   /* 核心系统底层抽象与内存语义契约 */
#else
static volatile int g_appdir_resolved;
#endif

const char *zan_file_app_dir(void) {
    static char dir[4096];
#if defined(_WIN32)
    for (;;) {
        if (InterlockedCompareExchangeAcquire(&g_appdir_resolved, 0, 0) == 1)
            return dir;
        if (InterlockedCompareExchangeAcquire(&g_appdir_resolved, 2, 0) == 0)
            break;                      /* 核心系统底层抽象与内存语义契约 */
        SwitchToThread();               /* 核心系统底层抽象与内存语义契约 */
    }
#elif defined(__wasm__)
    if (g_appdir_resolved) return dir;
#else
    for (;;) {
        if (__atomic_load_n(&g_appdir_resolved, __ATOMIC_ACQUIRE) == 1)
            return dir;
        int expected = 0;
        if (__atomic_compare_exchange_n(&g_appdir_resolved, &expected, 2, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            break;                      /* 核心系统底层抽象与内存语义契约 */
        sched_yield();                  /* 核心系统底层抽象与内存语义契约 */
    }
#endif
    const char *env = getenv("ZAN_APP_DIR");
    char local[4096];
    local[0] = '\0';
    if (env && env[0]) {
        size_t n = strlen(env);
        if (n < sizeof(local)) {
            memcpy(local, env, n + 1);
        } else {
            /* 内部辅助逻辑 */
            fprintf(stderr,
                    "zan: ZAN_APP_DIR too long (%zu bytes, limit %zu) -- "
                    "falling back to the executable directory\n",
                    n, sizeof(local) - 1);
            fflush(stderr);
        }
    } else {
        char exe[4096];
        exe[0] = 0;
#ifdef _WIN32
        DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe));
        if (n == 0 || n >= sizeof(exe)) exe[0] = 0;
#elif defined(__APPLE__)
        uint32_t cap = (uint32_t)sizeof(exe);
        if (_NSGetExecutablePath(exe, &cap) != 0) exe[0] = 0;
#elif defined(__linux__)
        ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n <= 0) exe[0] = 0;
        else exe[n] = 0;
#endif
        if (exe[0]) {
            char *fwd = strrchr(exe, '/');
            char *back = strrchr(exe, '\\');
            char *sep = fwd > back ? fwd : back;
            if (sep && sep != exe) {
                *sep = 0;
                if (strlen(exe) < sizeof(local))
                    memcpy(local, exe, strlen(exe) + 1);
            }
        }
    }
    /* 内部辅助逻辑 */
    memcpy(dir, local, strlen(local) + 1);
#if defined(_WIN32)
    InterlockedCompareExchangeRelease(&g_appdir_resolved, 1, 2);
#elif defined(__wasm__)
    g_appdir_resolved = 1;
#else
    __atomic_store_n(&g_appdir_resolved, 1, __ATOMIC_RELEASE);
#endif
    return dir;
}

/* 内部辅助逻辑 */
static const char *zan_alt_path(const char *path, int which, char *out,
                                size_t cap) {
    if (!path || !path[0]) return NULL;
    if (path[0] == '/' || path[0] == '\\') return NULL;
    if (path[1] == ':') return NULL;
    const char *base = which == 0 ? getenv("ZAN_PKG_DIR") : zan_file_app_dir();
    if (!base || !base[0]) return NULL;
    size_t bl = strlen(base), pl = strlen(path);
    if (bl + pl + 2 > cap) return NULL;
    memcpy(out, base, bl);
    out[bl] = '/';
    memcpy(out + bl + 1, path, pl + 1);
    return out;
}

#define ZAN_ALT_BASES 2

/* 模块核心语义抽象与接口调用契约 */
static long long zan_file_attributes_at(const char *path);

/* 内部辅助实现 */
const char *zan_file_read_path(const char *path) {
    if (zan_file_attributes_at(path) >= 0) return "";
    /* 内部辅助逻辑 */
    static _Thread_local char alt[4096];
    for (int which = 0; which < ZAN_ALT_BASES; which++) {
        const char *p = zan_alt_path(path, which, alt, sizeof(alt));
        if (p && zan_file_attributes_at(p) >= 0) return p;
    }
    return "";
}

/* 编译器代码生成与运行时系统底层调用契约 */
void *zan_pkg_fopen(const char *path, const char *mode) {
    FILE *f = (FILE *)zan_file_fopen(path, mode);
    if (f) return f;
    if (!mode || mode[0] != 'r') return NULL;
    if (strchr(mode, '+')) return NULL;
    char alt[4096];
    for (int which = 0; which < ZAN_ALT_BASES; which++) {
        const char *p = zan_alt_path(path, which, alt, sizeof(alt));
        if (!p) continue;
        f = (FILE *)zan_file_fopen(p, mode);
        if (f) return f;
    }
    return NULL;
}

long long zan_file_attributes(const char *path) {
    long long r = zan_file_attributes_at(path);
    if (r >= 0) return r;
    char alt[4096];
    for (int which = 0; which < ZAN_ALT_BASES; which++) {
        const char *p = zan_alt_path(path, which, alt, sizeof(alt));
        if (!p) continue;
        r = zan_file_attributes_at(p);
        if (r >= 0) return r;
    }
    return -1;
}

static long long zan_file_attributes_at(const char *path) {
    if (!path || !path[0]) return -1;
#ifdef _WIN32
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
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    long long r = 0;
    if (access(path, W_OK) != 0) r |= 1;
    { const char *base = strrchr(path, '/');
      base = base ? base + 1 : path;
      if (base[0] == '.') r |= 2; }
    if (S_ISDIR(st.st_mode)) r |= 4;
    return r;
#endif
}

#ifndef _WIN32
/* 模块核心语义抽象与接口调用契约 */
static int zan_posix_open_nofollow(const char *path) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
#if defined(O_PATH)
    if (fd < 0 && errno == EACCES) {
        fd = open(path, O_PATH | O_NOFOLLOW | O_CLOEXEC);
    }
#endif
    return fd;
}
#endif

/* 底层系统交互与数据协议契约 */
long long zan_file_set_readonly(const char *path, int on) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    wchar_t *wide_path = zan_win_utf8_to_wide(path);
    if (!wide_path) return 0;
    DWORD a = GetFileAttributesW(wide_path);
    if (a == INVALID_FILE_ATTRIBUTES) { free(wide_path); return 0; }
    if (on) a |= FILE_ATTRIBUTE_READONLY;
    else    a &= ~(DWORD)FILE_ATTRIBUTE_READONLY;
    int ok = SetFileAttributesW(wide_path, a) ? 1 : 0;
    free(wide_path);
    return ok;
#else
    int fd = zan_posix_open_nofollow(path);
    if (fd < 0) return 0;   /* 核心系统底层抽象与内存语义契约 */
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return 0; }
    mode_t m = st.st_mode;
    if (on) m &= ~(mode_t)(S_IWUSR | S_IWGRP | S_IWOTH);
    else    m |= S_IWUSR;
#if defined(__wasi__)
    /* 底层系统交互与数据协议契约 */
    int ok = 0;
#else
    int ok = fchmod(fd, m) == 0 ? 1 : 0;
#endif
    close(fd);
    return ok;
#endif
}

/* 底层系统交互与数据协议契约 */
long long zan_file_set_time(const char *path, int which, long long unix_sec) {
    if (!path || !path[0]) return 0;
    if (which != 0 && which != 1 && which != 2) return 0;
#ifdef _WIN32
    wchar_t *wide_path = zan_win_utf8_to_wide(path);
    if (!wide_path) return 0;
    HANDLE h = CreateFileW(wide_path, FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(wide_path);
    if (h == INVALID_HANDLE_VALUE) return 0;
    /* 核心系统底层抽象与内存语义契约 */
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
#else
    if (which == 1) return 0;   /* 核心系统底层抽象与内存语义契约 */
    int fd = zan_posix_open_nofollow(path);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return 0; }
    struct timespec times[2];
    /* atime, mtime */
    times[0].tv_sec = (which == 2) ? unix_sec : st.st_atime;
    times[0].tv_nsec = 0;
    times[1].tv_sec = (which == 0) ? unix_sec : st.st_mtime;
    times[1].tv_nsec = 0;
    int ok = futimens(fd, times) == 0 ? 1 : 0;
    close(fd);
    return ok;
#endif
}

/* 核心系统底层抽象与内存语义契约 */

/* 内部辅助逻辑 */
#define ZAN_FH_CAP0 64
#define ZAN_FH_CAP_MAX (1024 * 1024)
typedef struct {
    FILE *fp;
    uint32_t gen;   /* 底层系统交互与数据协议契约 */
    int open;
    int inuse;      /* 模块核心语义抽象与接口调用契约 */
    FILE *dying;    /* 模块核心语义抽象与接口调用契约 */
} zan_fh_slot;
static zan_fh_slot *g_fh_table = NULL;
static uint32_t g_fh_cap = 0;

static void zan_fh_ensure(void) {
    if (g_fh_table) return;
    g_fh_table = (zan_fh_slot *)calloc(ZAN_FH_CAP0, sizeof(*g_fh_table));
    if (!g_fh_table) return;  /* 底层系统交互与数据协议契约 */
    g_fh_cap = ZAN_FH_CAP0;
}

/* 编译器代码生成与运行时系统底层调用契约 */
static int zan_fh_grow(void) {
    uint32_t ncap;
    zan_fh_slot *ntab;
    if (g_fh_cap == 0) return 0;
    ncap = g_fh_cap * 2;
    if (ncap > ZAN_FH_CAP_MAX || ncap < g_fh_cap) return 0;
    ntab = (zan_fh_slot *)calloc((size_t)ncap, sizeof(*ntab));
    if (!ntab) return 0;
    memcpy(ntab, g_fh_table, (size_t)g_fh_cap * sizeof(*ntab));
    free(g_fh_table);
    g_fh_table = ntab;
    g_fh_cap = ncap;
    return 1;
}

#ifdef _WIN32
/* 内部辅助逻辑 */
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
#elif defined(__wasm__)
/* 内部辅助逻辑 */
static void zan_fh_lock(void) {}
static void zan_fh_unlock(void) {}
#else
static pthread_mutex_t g_fh_mx = PTHREAD_MUTEX_INITIALIZER;
static void zan_fh_lock(void) { pthread_mutex_lock(&g_fh_mx); }
static void zan_fh_unlock(void) { pthread_mutex_unlock(&g_fh_mx); }
#endif

/* 内部辅助逻辑 */
static long zan_fh_index(long long handle) {
    unsigned long long h = (unsigned long long)handle;
    uint32_t idx = (uint32_t)(h & 0xFFFFFFFFULL);
    uint32_t gen = (uint32_t)(h >> 32);
    if (!g_fh_table || idx >= g_fh_cap) return -1;
    zan_fh_slot *s = &g_fh_table[idx];
    if (!s->open || s->fp == NULL || s->gen != gen) return -1;
    return (long)idx;
}

/* 内部辅助逻辑 */
static FILE *zan_fh_pin(long long handle, long *idx_out) {
    zan_fh_lock();
    zan_fh_ensure();
    long idx = zan_fh_index(handle);
    FILE *f = NULL;
    if (idx >= 0) {
        f = g_fh_table[idx].fp;
        g_fh_table[idx].inuse++;
    }
    *idx_out = idx;
    zan_fh_unlock();
    return f;
}

/* Drop a pin */
static void zan_fh_unpin(long idx) {
    FILE *dying = NULL;
    if (idx < 0) return;
    zan_fh_lock();
    if (g_fh_table && (uint32_t)idx < g_fh_cap) {
        zan_fh_slot *s = &g_fh_table[idx];
        if (s->inuse > 0 && --s->inuse == 0 && s->dying) {
            dying = s->dying;
            s->dying = NULL;
        }
    }
    zan_fh_unlock();
    if (dying) fclose(dying);
}

/* 核心系统底层抽象与内存语义契约 */
long long zan_file_open(const char *path, const char *mode) {
    if (!path || !path[0] || !mode || !mode[0]) return 0;
    FILE *f = (FILE *)zan_file_fopen(path, mode);
    if (!f) return 0;
    zan_fh_lock();
    zan_fh_ensure();
    long long handle = 0;
    if (g_fh_table) {
        for (;;) {
            uint32_t i = 0;
            while (i < g_fh_cap) {
                zan_fh_slot *s = &g_fh_table[i];
                /* 内部辅助实现 */
                if (!s->open && !s->dying && s->gen != UINT32_MAX) {
                    if (s->gen == 0) { s->gen = 1; }   /* 核心系统底层抽象与内存语义契约 */
                    s->fp = f;
                    s->open = 1;
                    /* 内部辅助逻辑 */
                    handle = (long long)(((unsigned long long)s->gen << 32)
                                         | (unsigned long long)i);
                    break;
                }
                i = i + 1;
            }
            if (i < g_fh_cap) break;    /* claimed a slot */
            if (!zan_fh_grow()) break;  /* 核心系统底层抽象与内存语义契约 */
        }
    }
    zan_fh_unlock();
    if (handle == 0) { fclose(f); return 0; }  /* 核心系统底层抽象与内存语义契约 */
    return handle;
}

long long zan_file_read(long long handle, long long buf, long long count) {
    if (!buf || count <= 0) return 0;
    long idx;
    FILE *f = zan_fh_pin(handle, &idx);
    if (!f) return 0;
    long long n = (long long)fread((void *)(intptr_t)buf, 1, (size_t)count, f);
    zan_fh_unpin(idx);
    return n;
}

long long zan_file_write(long long handle, long long buf, long long count) {
    if (!buf || count <= 0) return 0;
    long idx;
    FILE *f = zan_fh_pin(handle, &idx);
    if (!f) return 0;
    long long n = (long long)fwrite((const void *)(intptr_t)buf, 1, (size_t)count, f);
    zan_fh_unpin(idx);
    return n;
}

/* `origin`: 0 = begin, 1 = current, 2 = end */
long long zan_file_seek(long long handle, long long offset, int origin) {
    long idx;
    FILE *f = zan_fh_pin(handle, &idx);
    if (!f) return -1;
    int whence = origin == 1 ? SEEK_CUR : (origin == 2 ? SEEK_END : SEEK_SET);
    long long r;
#ifdef _WIN32
    r = _fseeki64(f, (__int64)offset, whence) != 0 ? -1 : (long long)_ftelli64(f);
#else
    r = fseeko(f, (off_t)offset, whence) != 0 ? -1 : (long long)ftello(f);
#endif
    zan_fh_unpin(idx);
    return r;
}

long long zan_file_tell(long long handle) {
    long idx;
    FILE *f = zan_fh_pin(handle, &idx);
    if (!f) return -1;
#ifdef _WIN32
    long long r = (long long)_ftelli64(f);
#else
    long long r = (long long)ftello(f);
#endif
    zan_fh_unpin(idx);
    return r;
}

long long zan_file_flush(long long handle) {
    long idx;
    FILE *f = zan_fh_pin(handle, &idx);
    if (!f) return -1;
    int rc = fflush(f);
    zan_fh_unpin(idx);
    return rc == 0 ? 1 : 0;
}

long long zan_file_close(long long handle) {
    zan_fh_lock();
    zan_fh_ensure();
    long idx = zan_fh_index(handle);
    FILE *f = NULL;
    int deferred = 0;
    if (idx >= 0) {
        zan_fh_slot *s = &g_fh_table[idx];
        s->open = 0;
        if (s->inuse > 0) {
            /* 内部辅助逻辑 */
            s->dying = s->fp;
            s->fp = NULL;
            deferred = 1;
        } else {
            f = s->fp;
            s->fp = NULL;
        }
        /* 内部辅助实现 */
        uint32_t next_gen = (uint32_t)(s->gen + 1);
        if (next_gen != 0) s->gen = next_gen;
    }
    zan_fh_unlock();
    if (deferred) return 1;   /* 模块核心语义抽象与接口调用契约 */
    if (!f) return 0;         /* 核心系统底层抽象与内存语义契约 */
    return fclose(f) == 0 ? 1 : 0;
}

/* 底层系统交互与数据协议契约 */
long long zan_file_eof(long long handle) {
    long idx;
    FILE *f = zan_fh_pin(handle, &idx);
    if (!f) return 1;
    int e = feof(f) ? 1 : 0;
    zan_fh_unpin(idx);
    return e;
}

/* 核心系统底层抽象与内存语义契约 */

#define ZAN_LK_CAP 32
typedef struct {
#ifdef _WIN32
    HANDLE h;
#else
    int fd;
#endif
    int used;
    uint32_t gen;   /* 模块核心语义抽象与接口调用契约 */
} zan_lk_slot;
static zan_lk_slot g_lk_table[ZAN_LK_CAP];

/* 核心系统底层抽象与内存语义契约 */
static long zan_lk_index(long long handle) {
    if (handle <= 0) return -1;
    unsigned long long h = (unsigned long long)handle;
    uint32_t gen = (uint32_t)(h >> 32);
    unsigned long long slot = h & 0xFFFFFFFFULL;
    if (slot >= (unsigned long long)ZAN_LK_CAP) return -1;
    if (g_lk_table[slot].used != 1 || g_lk_table[slot].gen != gen) return -1;
    return (long)slot;
}

/* 底层系统交互与数据协议契约 */
long long zan_file_try_lock(const char *path) {
    if (!path || !path[0]) return 0;
#if defined(__wasm__)
    (void)path;
    return 0;               /* 核心系统底层抽象与内存语义契约 */
#else
    zan_fh_lock();          /* 模块核心语义抽象与接口调用契约 */
    long slot = -1;
    for (long i = 0; i < ZAN_LK_CAP; i++) {
        if (!g_lk_table[i].used) {
            slot = i;
            /* 底层系统交互与数据协议契约 */
            g_lk_table[slot].used = 1;
            break;
        }
    }
    zan_fh_unlock();
    if (slot < 0) return 0;
#ifdef _WIN32
    wchar_t *wide_path = zan_win_utf8_to_wide(path);
    if (!wide_path) goto fail_reserve;
    HANDLE h = CreateFileW(wide_path, GENERIC_WRITE, 0 /* no sharing */, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wide_path);
    if (h == INVALID_HANDLE_VALUE) goto fail_reserve;
#else
    int fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0644);
    if (fd < 0) goto fail_reserve;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) { close(fd); goto fail_reserve; }
#endif
    zan_fh_lock();
#ifdef _WIN32
    g_lk_table[slot].h = h;
#else
    g_lk_table[slot].fd = fd;
#endif
    /* 内部辅助实现 */
    uint32_t next_gen = (uint32_t)(g_lk_table[slot].gen + 1);
    if (next_gen == 0) {
#ifdef _WIN32
        g_lk_table[slot].h = NULL;
#else
        g_lk_table[slot].fd = -1;
#endif
        g_lk_table[slot].used = 2;          /* 核心系统底层抽象与内存语义契约 */
        zan_fh_unlock();
#ifdef _WIN32
        CloseHandle(h);
#else
        close(fd);              /* 底层系统交互与数据协议契约 */
#endif
        return 0;
    }
    g_lk_table[slot].gen = next_gen;
    unsigned long long lk_handle =
        ((unsigned long long)g_lk_table[slot].gen << 32)
        | (unsigned long long)slot;
    zan_fh_unlock();
    return (long long)lk_handle;

fail_reserve:
    zan_fh_lock();
    g_lk_table[slot].used = 0;   /* 核心系统底层抽象与内存语义契约 */
    zan_fh_unlock();
    return 0;
#endif /* !__wasm__ */
}

/* 底层系统交互与数据协议契约 */
long long zan_file_unlock(long long handle) {
    zan_fh_lock();
    long slot = zan_lk_index(handle);
    if (slot < 0) {
        zan_fh_unlock();
        return 0;
    }
    zan_lk_slot *s = &g_lk_table[slot];
#ifdef _WIN32
    HANDLE h = s->h;
    s->h = NULL;
#else
    int fd = s->fd;
    s->fd = -1;
#endif
    s->used = 0;
    s->gen = s->gen + 1;   /* 底层系统交互与数据协议契约 */
    if (s->gen == 0) {
        /* 内部辅助逻辑 */
        s->used = 2;
    }
    zan_fh_unlock();
#ifdef _WIN32
    CloseHandle(h);
#else
    close(fd);              /* 核心系统底层抽象与内存语义契约 */
#endif
    return 1;
}
