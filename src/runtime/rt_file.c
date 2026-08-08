/* rt_file.c -- file metadata + FILE*-stream IO helpers (System.IO.FileInfo,
 * System.IO.FileStream).
 *
 * Split out of rt_sync.c so that file IO does not drag the atomics/threads/
 * shared-table runtime into a program that only touches files: zanc links
 * this object on `uses_file_runtime` (zan_file_* externs) and rt_sync.o only
 * on `uses_sync_runtime` (zan_atomic_int_*, zan_shared_table_*, zan_thread_*,
 * ...). That separation is what lets a wasm32 (WASI) cross-build link a
 * file-IO program against plain libc -- wasm has no pthread/shm, so rt_sync.c
 * cannot be built for it, while this file is pure libc + a single mutex.
 */

#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
/* O_NOFOLLOW/O_CLOEXEC are GNU extensions; glibc gates them on
 * _DEFAULT_SOURCE. */
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
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
#include <sys/stat.h>
#include <unistd.h>
#endif

#if !defined(_WIN32) && !defined(O_NOFOLLOW)
#define O_NOFOLLOW 0
#endif
#if !defined(_WIN32) && !defined(O_CLOEXEC)
#define O_CLOEXEC 0
#endif

/* --- File metadata -------------------------------------------------------
 *
 * The standard library had whole-file text IO and nothing else, so nothing
 * could ask when a file last changed -- which is what incremental builds and
 * hot reload are made of. These report Unix seconds (0 when the file does not
 * exist) and the DOS/Unix attribute bits behind System.IO.FileInfo.
 */

/* 0 = last write, 1 = creation, 2 = last access. */
long long zan_file_time(const char *path, int which) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &d)) return 0;
    FILETIME ft = d.ftLastWriteTime;
    if (which == 1) ft = d.ftCreationTime;
    else if (which == 2) ft = d.ftLastAccessTime;
    unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32)
                         | (unsigned long long)ft.dwLowDateTime;
    if (t == 0) return 0;
    /* 100 ns ticks since 1601 -> seconds since 1970 */
    return (long long)(t / 10000000ULL) - 11644473600LL;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (which == 1) return (long long)st.st_ctime;
    if (which == 2) return (long long)st.st_atime;
    return (long long)st.st_mtime;
#endif
}

/* Size in bytes, or -1 when the path does not exist. */
long long zan_file_length(const char *path) {
    if (!path || !path[0]) return -1;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &d)) return -1;
    return (long long)(((unsigned long long)d.nFileSizeHigh << 32)
                       | (unsigned long long)d.nFileSizeLow);
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long long)st.st_size;
#endif
}

/* Bit 0 read-only, bit 1 hidden, bit 2 directory; -1 when missing. */
long long zan_file_attributes(const char *path) {
    if (!path || !path[0]) return -1;
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
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
/* POSIX: resolve `path` to a descriptor once, then operate on the fd. This
 * closes the TOCTOU window between stat()-ing a path and chmod/utimens-ing
 * it (a swapped path now acts on the file actually opened), and O_NOFOLLOW
 * refuses to reach a target through a symlink (a symlink chain is ELOOP).
 * O_RDONLY opens files and directories on every POSIX; O_PATH (Linux) is the
 * fallback for read-protected targets where O_RDONLY would fail EACCES.
 * Returns the fd, or -1 with errno set on failure. */
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

/* Marks the file read-only (`on`) or writable. Returns 1 on success. */
long long zan_file_set_readonly(const char *path, int on) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES) return 0;
    if (on) a |= FILE_ATTRIBUTE_READONLY;
    else    a &= ~(DWORD)FILE_ATTRIBUTE_READONLY;
    return SetFileAttributesA(path, a) ? 1 : 0;
#else
    int fd = zan_posix_open_nofollow(path);
    if (fd < 0) return 0;   /* missing, ELOOP symlink, EACCES, ... */
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return 0; }
    mode_t m = st.st_mode;
    if (on) m &= ~(mode_t)(S_IWUSR | S_IWGRP | S_IWOTH);
    else    m |= S_IWUSR;
    int ok = fchmod(fd, m) == 0 ? 1 : 0;
    close(fd);
    return ok;
#endif
}

/* Sets one file timestamp to a Unix timestamp. `which` matches zan_file_time:
 * 0 = last write, 1 = creation (best-effort), 2 = last access. Returns 1 on
 * success. Creation time is only settable on Windows; elsewhere it is a no-op
 * that returns 0 so callers can degrade gracefully. */
long long zan_file_set_time(const char *path, int which, long long unix_sec) {
    if (!path || !path[0]) return 0;
    if (which != 0 && which != 1 && which != 2) return 0;
#ifdef _WIN32
    HANDLE h = CreateFileA(path, FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    /* Unix seconds since 1970 -> 100ns ticks since 1601. */
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
    if (which == 1) return 0;   /* creation time: not settable on POSIX */
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

/* ---- file handles (System.IO.FileStream) ---------------------------------
 * FILE*-based stream IO with 64-bit offsets, exposed as an opaque 64-bit
 * handle so zan code never spells FILE* (its size and the width of fseek's
 * offset both vary by platform). Handles are unforgeable: a handle is
 * (gen << 32) | index into a fixed table of { FILE*, gen, open } slots, and
 * every operation first checks the index range, the generation and the open
 * flag under a mutex. A fabricated integer, a stale handle from a closed
 * slot, or a double close no longer reaches fread/fclose on a bogus FILE*.
 * A handle is 0 when the open failed; every other call treats 0 (and every
 * other invalid handle) as a no-op so a failed open cannot corrupt memory.
 * Table access is serialized; the captured FILE* is used outside the lock so
 * blocking I/O never holds it.
 */

#define ZAN_FH_CAP 1024
typedef struct {
    FILE *fp;
    uint32_t gen;   /* bumped on every close; stale handles stop matching */
    int open;
} zan_fh_slot;
static zan_fh_slot g_fh_table[ZAN_FH_CAP];
static int g_fh_ready = 0;

static void zan_fh_ensure(void) {
    if (g_fh_ready) return;
    int i = 0;
    while (i < ZAN_FH_CAP) {
        g_fh_table[i].fp = NULL;
        g_fh_table[i].gen = 1;   /* gen 0 would make slot 0's handle read 0 */
        g_fh_table[i].open = 0;
        i = i + 1;
    }
    g_fh_ready = 1;
}

#ifdef _WIN32
/* Same lazy-init discipline as the runtime's other tables: INIT_ONCE runs the
 * critical-section constructor exactly once, so two threads opening files for
 * the first time cannot race the table's first use. */
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
/* Single-threaded wasm (WASI): the sysroot has no pthreads, and none are
 * needed -- one thread cannot race the handle table. */
static void zan_fh_lock(void) {}
static void zan_fh_unlock(void) {}
#else
static pthread_mutex_t g_fh_mx = PTHREAD_MUTEX_INITIALIZER;
static void zan_fh_lock(void) { pthread_mutex_lock(&g_fh_mx); }
static void zan_fh_unlock(void) { pthread_mutex_unlock(&g_fh_mx); }
#endif

/* Resolve `handle` to the table slot of a live open file, or -1 when the
 * handle is forged: index out of range, wrong generation, or a closed slot.
 * Call under the table lock. */
static long zan_fh_index(long long handle) {
    unsigned long long h = (unsigned long long)handle;
    uint32_t idx = (uint32_t)(h & 0xFFFFFFFFULL);
    uint32_t gen = (uint32_t)(h >> 32);
    if (idx >= ZAN_FH_CAP) return -1;
    zan_fh_slot *s = &g_fh_table[idx];
    if (!s->open || s->fp == NULL || s->gen != gen) return -1;
    return (long)idx;
}

/* `mode` is a stdio mode string ("rb", "wb", "r+b", "ab", ...). */
long long zan_file_open(const char *path, const char *mode) {
    if (!path || !path[0] || !mode || !mode[0]) return 0;
    FILE *f = fopen(path, mode);
    if (!f) return 0;
    zan_fh_lock();
    zan_fh_ensure();
    long long handle = 0;
    int i = 0;
    while (i < ZAN_FH_CAP) {
        zan_fh_slot *s = &g_fh_table[i];
        if (!s->open) {
            if (s->gen == 0) { s->gen = 1; }   /* keep slot 0's handle nonzero */
            s->fp = f;
            s->open = 1;
            /* build in unsigned to avoid signed-shift UB when gen's high bit
             * is set (a long long is bit-preserving on the Zan side) */
            handle = (long long)(((unsigned long long)s->gen << 32)
                                 | (unsigned long long)(uint32_t)i);
            break;
        }
        i = i + 1;
    }
    zan_fh_unlock();
    if (handle == 0) { fclose(f); return 0; }  /* table full: fail gracefully */
    return handle;
}

long long zan_file_read(long long handle, long long buf, long long count) {
    if (!buf || count <= 0) return 0;
    zan_fh_lock();
    zan_fh_ensure();
    long idx = zan_fh_index(handle);
    FILE *f = idx >= 0 ? g_fh_table[idx].fp : NULL;
    zan_fh_unlock();
    if (!f) return 0;
    return (long long)fread((void *)(intptr_t)buf, 1, (size_t)count, f);
}

long long zan_file_write(long long handle, long long buf, long long count) {
    if (!buf || count <= 0) return 0;
    zan_fh_lock();
    zan_fh_ensure();
    long idx = zan_fh_index(handle);
    FILE *f = idx >= 0 ? g_fh_table[idx].fp : NULL;
    zan_fh_unlock();
    if (!f) return 0;
    return (long long)fwrite((const void *)(intptr_t)buf, 1, (size_t)count, f);
}

/* `origin`: 0 = begin, 1 = current, 2 = end. Returns the new absolute
 * position, or -1 on failure. */
long long zan_file_seek(long long handle, long long offset, int origin) {
    zan_fh_lock();
    zan_fh_ensure();
    long idx = zan_fh_index(handle);
    FILE *f = idx >= 0 ? g_fh_table[idx].fp : NULL;
    zan_fh_unlock();
    if (!f) return -1;
    int whence = origin == 1 ? SEEK_CUR : (origin == 2 ? SEEK_END : SEEK_SET);
#ifdef _WIN32
    if (_fseeki64(f, (__int64)offset, whence) != 0) return -1;
    return (long long)_ftelli64(f);
#else
    if (fseeko(f, (off_t)offset, whence) != 0) return -1;
    return (long long)ftello(f);
#endif
}

long long zan_file_tell(long long handle) {
    zan_fh_lock();
    zan_fh_ensure();
    long idx = zan_fh_index(handle);
    FILE *f = idx >= 0 ? g_fh_table[idx].fp : NULL;
    zan_fh_unlock();
    if (!f) return -1;
#ifdef _WIN32
    return (long long)_ftelli64(f);
#else
    return (long long)ftello(f);
#endif
}

long long zan_file_flush(long long handle) {
    zan_fh_lock();
    zan_fh_ensure();
    long idx = zan_fh_index(handle);
    FILE *f = idx >= 0 ? g_fh_table[idx].fp : NULL;
    zan_fh_unlock();
    if (!f) return -1;
    return fflush(f) == 0 ? 1 : 0;
}

long long zan_file_close(long long handle) {
    zan_fh_lock();
    zan_fh_ensure();
    long idx = zan_fh_index(handle);
    FILE *f = NULL;
    if (idx >= 0) {
        zan_fh_slot *s = &g_fh_table[idx];
        s->open = 0;
        f = s->fp;
        s->fp = NULL;
        s->gen = s->gen + 1;   /* invalidate every outstanding copy of the handle */
        if (s->gen == 0) { s->gen = 1; }
    }
    zan_fh_unlock();
    if (!f) return 0;   /* unknown or already-closed handle */
    return fclose(f) == 0 ? 1 : 0;
}

/* 1 once a read hit end-of-file on this handle. */
long long zan_file_eof(long long handle) {
    zan_fh_lock();
    zan_fh_ensure();
    long idx = zan_fh_index(handle);
    FILE *f = idx >= 0 ? g_fh_table[idx].fp : NULL;
    zan_fh_unlock();
    if (!f) return 1;
    return feof(f) ? 1 : 0;
}
