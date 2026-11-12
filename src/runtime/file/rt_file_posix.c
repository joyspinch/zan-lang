/* rt_file_posix.c: POSIX / Linux / Darwin / BSD / WebAssembly 原生文件系统与文件流底层实现 */

#ifdef _WIN32
#error "rt_file_posix.c is not intended for Windows targets"
#endif

#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#if !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif
#if !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif

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
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#if !defined(O_NOFOLLOW)
#define O_NOFOLLOW 0
#endif
#if !defined(O_CLOEXEC)
#define O_CLOEXEC 0
#endif

void *zan_file_fopen(const char *path, const char *mode) {
    if (!path || !mode) return NULL;
    return fopen(path, mode);
}

int zan_file_remove(const char *path) {
    if (!path || !path[0]) return -1;
    return remove(path);
}

int zan_file_rename(const char *source, const char *dest) {
    if (!source || !source[0] || !dest || !dest[0]) return -1;
    return rename(source, dest);
}

static int zan_sync_fd(int fd) {
    int rc;
    do { rc = fsync(fd); } while (rc != 0 && errno == EINTR);
    return rc;
}

int zan_file_flush_to_disk(const char *path) {
#if defined(__wasm__)
    (void)path;
    return -2;
#else
    if (!path || !path[0]) return -1;
    int fd;
    // Nonblocking open lets fstat reject a FIFO without waiting for a writer.
    do { fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW); }
    while (fd < 0 && errno == EINTR);
    if (fd < 0) return -1;
    struct stat st;
    int ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && zan_sync_fd(fd) == 0;
    int close_rc = close(fd);
    return ok && close_rc == 0 ? 0 : -1;
#endif
}

int zan_file_flush_directory_to_disk(const char *path) {
#if defined(__wasm__) || !defined(O_DIRECTORY)
    (void)path;
    return -2;
#else
    if (!path || !path[0]) return -1;
    int fd;
    do { fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW); }
    while (fd < 0 && errno == EINTR);
    if (fd < 0) return -1;
    struct stat st;
    int ok = fstat(fd, &st) == 0 && S_ISDIR(st.st_mode) && zan_sync_fd(fd) == 0;
    int close_rc = close(fd);
    return ok && close_rc == 0 ? 0 : -1;
#endif
}

/* 文件元数据错误码口径规范（B-ID106 收敛）：
 * 1. zan_file_time: 成功返回 Unix 秒时间戳；路径不存在或读取失败返回 0
 * 2. zan_file_length: 成功返回文件字节数；路径不存在或读取失败返回 -1。
 * 3. zan_file_attributes: 成功返回属性位掩码；路径不存在或读取失败返回 -1。
 * 4. zan_file_set_readonly: 成功返回 0；失败返回 -1。
 */
long long zan_file_time(const char *path, int which) {
    if (!path || !path[0]) return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    if (which == 1) return (long long)st.st_birthtime;
#else
    if (which == 1) return (long long)st.st_ctime;
#endif
    if (which == 2) return (long long)st.st_atime;
    return (long long)st.st_mtime;
}

long long zan_file_length(const char *path) {
    if (!path || !path[0]) return -1;
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long long)st.st_size;
}

static long long zan_file_attributes_at(const char *path) {
    if (!path || !path[0]) return -1;
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    long long r = 0;
    if (!(st.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH))) r |= 1;
    { const char *base = strrchr(path, '/');
      base = base ? base + 1 : path;
      if (base[0] == '.') r |= 2; }
    if (S_ISDIR(st.st_mode)) r |= 4;
    return r;
}

static int zan_posix_open_nofollow(const char *path) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
#if defined(O_PATH)
    if (fd < 0 && errno == EACCES) {
        fd = open(path, O_PATH | O_NOFOLLOW | O_CLOEXEC);
    }
#endif
    return fd;
}

long long zan_file_set_readonly(const char *path, int on) {
    if (!path || !path[0]) return 0;
    int fd = zan_posix_open_nofollow(path);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return 0; }
    mode_t m = st.st_mode;
    if (on) m &= ~(mode_t)(S_IWUSR | S_IWGRP | S_IWOTH);
    else    m |= S_IWUSR;
#if defined(__wasi__)
    int ok = 0;
#else
    int ok = fchmod(fd, m) == 0 ? 1 : 0;
#endif
    close(fd);
    return ok;
}

long long zan_file_set_time(const char *path, int which, long long unix_sec) {
    if (!path || !path[0]) return 0;
    if (which == 1) return 0;
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
}

/* 平台可执行文件路径获取 */
static void plat_get_executable_path(char *exe, size_t cap) {
#if defined(__APPLE__)
    uint32_t c = (uint32_t)cap;
    if (_NSGetExecutablePath(exe, &c) != 0) exe[0] = 0;
#elif defined(__linux__)
    ssize_t n = readlink("/proc/self/exe", exe, cap - 1);
    if (n <= 0) exe[0] = 0;
    else exe[n] = 0;
#elif defined(__FreeBSD__) || defined(__DragonFly__)
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
    size_t cb = cap;
    if (sysctl(mib, 4, exe, &cb, NULL, 0) != 0) exe[0] = 0;
#elif defined(__sun)
    const char *p = getexecname();
    if (p && strlen(p) < cap) strcpy(exe, p);
    else exe[0] = 0;
#else
    (void)cap;
    exe[0] = 0;
#endif
}

/* 平台 app_dir 首次探测同步原语 */
#if defined(__wasm__)
static int g_appdir_resolved;
static void plat_appdir_acquire(void) {}
static int plat_appdir_is_resolved(void) { return g_appdir_resolved; }
static void plat_appdir_release(void) { g_appdir_resolved = 1; }
#else
static volatile int g_appdir_resolved;
static void plat_appdir_acquire(void) {
    for (;;) {
        if (__atomic_load_n(&g_appdir_resolved, __ATOMIC_ACQUIRE) == 1)
            return;
        int expected = 0;
        if (__atomic_compare_exchange_n(&g_appdir_resolved, &expected, 2, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            break;
        sched_yield();
    }
}
static int plat_appdir_is_resolved(void) {
    return __atomic_load_n(&g_appdir_resolved, __ATOMIC_ACQUIRE) == 1;
}
static void plat_appdir_release(void) {
    __atomic_store_n(&g_appdir_resolved, 1, __ATOMIC_RELEASE);
}
#endif

/* 平台 64 位文件定位与探针 */
static long long plat_file_seek_raw(FILE *f, long long offset, int whence) {
    return fseeko(f, (off_t)offset, whence) != 0 ? -1 : (long long)ftello(f);
}

static long long plat_file_tell_raw(FILE *f) {
    return (long long)ftello(f);
}

/* 平台句柄表互斥锁 */
#if defined(__wasm__)
static void zan_fh_lock(void) {}
static void zan_fh_unlock(void) {}
#else
static pthread_mutex_t g_fh_mx = PTHREAD_MUTEX_INITIALIZER;
static void zan_fh_lock(void) { pthread_mutex_lock(&g_fh_mx); }
static void zan_fh_unlock(void) { pthread_mutex_unlock(&g_fh_mx); }
#endif

/* 平台底层排他锁持有句柄定义与操作 */
typedef int plat_file_lock_t;
#define PLAT_FILE_LOCK_INVALID (-1)

static plat_file_lock_t plat_file_lock_open(const char *path) {
#if defined(__wasm__)
    (void)path;
    return PLAT_FILE_LOCK_INVALID;
#else
    int fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0644);
    if (fd < 0) return PLAT_FILE_LOCK_INVALID;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return PLAT_FILE_LOCK_INVALID;
    }
    return fd;
#endif
}

static void plat_file_lock_close(plat_file_lock_t fd) {
#if !defined(__wasm__)
    if (fd >= 0) {
        close(fd);
    }
#else
    (void)fd;
#endif
}
