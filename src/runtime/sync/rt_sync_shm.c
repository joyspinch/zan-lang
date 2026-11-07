/* rt_sync_shm.c: 跨平台具名共享内存与内存映射支持 */

#if defined(__ANDROID__) || (defined(__OHOS__) && !defined(ZAN_SYNC_NO_SHM_SHIM))
static const char *zan_android_shm_dir(void) {
    const char *d = getenv("ZAN_SHM_DIR");
    return (d && d[0]) ? d : "/data/local/tmp";
}
static int zan_android_shm_path(const char *name, char *path, size_t cap) {
    if (name[0] == '/') name++;
    int n = snprintf(path, cap, "%s/%s", zan_android_shm_dir(), name);
    if (n < 0 || (size_t)n >= cap) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}
static int zan_android_shm_open(const char *name, int flags, mode_t mode) {
    char path[1024];
    if (zan_android_shm_path(name, path, sizeof(path)) != 0) return -1;
    return open(path, flags | O_CLOEXEC, mode);
}
static int zan_android_shm_unlink(const char *name) {
    char path[1024];
    if (zan_android_shm_path(name, path, sizeof(path)) != 0) return -1;
    return unlink(path);
}
#define shm_open(n, f, m) zan_android_shm_open((n), (f), (m))
#define shm_unlink(n) zan_android_shm_unlink((n))
#endif

#ifdef _WIN32
typedef struct {
    HANDLE h;
} zan_mmap_handle;
#else
typedef struct {
    int fd;
    int owner;
    char name[256];
} zan_mmap_handle;
#endif

/* 创建指定字节大小的具名共享内存区 */
long long zan_mmap_create(const char *name, long long size) {
    if (!name || !name[0] || size <= 0) return 0;
#ifdef _WIN32
    DWORD hi = (DWORD)(((unsigned long long)size) >> 32);
    DWORD lo = (DWORD)((unsigned long long)size & 0xFFFFFFFFULL);
    int wlen = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
    if (wlen <= 0) return 0;
    wchar_t *wname = (wchar_t *)calloc((size_t)wlen, sizeof(wchar_t));
    if (!wname) return 0;
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, wlen);
    HANDLE named = CreateFileMappingW(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, hi, lo, wname);
    DWORD err = GetLastError();
    free(wname);
    if (!named) return 0;
    if (err == ERROR_ALREADY_EXISTS) {
        CloseHandle(named);
        return 0;
    }
    return (long long)(intptr_t)named;
#else
    char shm_name[256];
    snprintf(shm_name, sizeof(shm_name), "/%s", name);
    int fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) return 0;
    if (ftruncate(fd, (off_t)size) != 0) {
        shm_unlink(shm_name);
        close(fd);
        return 0;
    }
    zan_mmap_handle *h = (zan_mmap_handle *)calloc(1, sizeof(zan_mmap_handle));
    if (!h) { shm_unlink(shm_name); close(fd); return 0; }
    h->fd = fd;
    h->owner = 1;
    snprintf(h->name, sizeof(h->name), "%s", shm_name);
    return (long long)(intptr_t)h;
#endif
}

/* 打开已存在的具名共享内存区 */
long long zan_mmap_open(const char *name, long long size) {
    if (!name || !name[0]) return 0;
#ifdef _WIN32
    (void)size;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
    if (wlen <= 0) return 0;
    wchar_t *wname = (wchar_t *)calloc((size_t)wlen, sizeof(wchar_t));
    if (!wname) return 0;
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, wlen);
    HANDLE h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wname);
    free(wname);
    if (!h) return 0;
    return (long long)(intptr_t)h;
#else
    char shm_name[256];
    snprintf(shm_name, sizeof(shm_name), "/%s", name);
    int fd = shm_open(shm_name, O_RDWR, 0600);
    if (fd < 0) return 0;
    if (size > 0) {
        struct stat st;
        if (fstat(fd, &st) != 0 || st.st_size != size) {
            close(fd);
            return 0;
        }
    }
    zan_mmap_handle *h = (zan_mmap_handle *)calloc(1, sizeof(zan_mmap_handle));
    if (!h) { close(fd); return 0; }
    h->fd = fd;
    snprintf(h->name, sizeof(h->name), "%s", shm_name);
    return (long long)(intptr_t)h;
#endif
}

/* 在已有文件句柄上建立内存映射 */
long long zan_mmap_from_file(const char *path, long long size) {
    if (!path || !path[0]) return 0;
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wlen <= 0) return 0;
    wchar_t *wpath = (wchar_t *)calloc((size_t)wlen, sizeof(wchar_t));
    if (!wpath) return 0;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);
    HANDLE fh = CreateFileW(wpath, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wpath);
    if (fh == INVALID_HANDLE_VALUE) return 0;
    DWORD hi = 0, lo = 0;
    if (size > 0) {
        hi = (DWORD)(((unsigned long long)size) >> 32);
        lo = (DWORD)((unsigned long long)size & 0xFFFFFFFFULL);
    }
    HANDLE h = CreateFileMappingW(fh, NULL, PAGE_READWRITE, hi, lo, NULL);
    CloseHandle(fh);
    if (!h) return 0;
    return (long long)(intptr_t)h;
#else
    int fd = open(path, O_RDWR);
    if (fd < 0) return 0;
    zan_mmap_handle *h = (zan_mmap_handle *)calloc(1, sizeof(zan_mmap_handle));
    if (!h) { close(fd); return 0; }
    h->fd = fd;
    h->name[0] = '\0';
    return (long long)(intptr_t)h;
#endif
}

long long zan_mmap_map(long long handle, long long size) {
    if (!handle || size <= 0) return 0;
#ifdef _WIN32
    HANDLE h = (HANDLE)(intptr_t)handle;
    void *p = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, (size_t)size);
    return p ? (long long)(intptr_t)p : 0;
#else
    zan_mmap_handle *h = (zan_mmap_handle *)(intptr_t)handle;
    void *p = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   h->fd, 0);
    if (p == MAP_FAILED) return 0;
    return (long long)(intptr_t)p;
#endif
}

long long zan_mmap_unmap(long long ptr, long long size) {
    if (!ptr) return 0;
#ifdef _WIN32
    (void)size;
    return UnmapViewOfFile((void *)(intptr_t)ptr) ? 1 : 0;
#else
    return munmap((void *)(intptr_t)ptr, (size_t)size) == 0 ? 1 : 0;
#endif
}

long long zan_mmap_flush(long long ptr, long long size) {
    if (!ptr) return 1;
#ifdef _WIN32
    return FlushViewOfFile((void *)(intptr_t)ptr, (size_t)size) ? 1 : 0;
#else
    return msync((void *)(intptr_t)ptr, (size_t)size, MS_SYNC) == 0 ? 1 : 0;
#endif
}

long long zan_mmap_close(long long handle) {
    if (!handle) return 0;
#ifdef _WIN32
    HANDLE h = (HANDLE)(intptr_t)handle;
    return CloseHandle(h) ? 1 : 0;
#else
    zan_mmap_handle *h = (zan_mmap_handle *)(intptr_t)handle;
    close(h->fd);
    if (h->owner && h->name[0]) shm_unlink(h->name);
    free(h);
    return 1;
#endif
}

long long zan_mmap_unlink(const char *name) {
    if (!name || !name[0]) return 0;
#ifdef _WIN32
    (void)name;
    return 1;
#else
    char shm_name[256];
    snprintf(shm_name, sizeof(shm_name), "/%s", name);
    return shm_unlink(shm_name) == 0 ? 1 : 0;
#endif
}

long long zan_exe_dir_into(char *out, long long cap) {
    if (!out || cap <= 0) return 0;
    out[0] = '\0';
#ifdef _WIN32
    char buf[1024];
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof(buf));
    while (n > 0 && buf[n - 1] != '\\') n--;
    if (n > 0) n--;
    if ((long long)n >= cap) n = (DWORD)(cap - 1);
    memcpy(out, buf, n);
    out[n] = '\0';
    return (long long)n;
#else
    char buf[1024];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n < 0) n = 0;
    while (n > 0 && buf[n - 1] != '/') n--;
    if (n > 0) n--;
    if ((long long)n >= cap) n = (ssize_t)(cap - 1);
    memcpy(out, buf, (size_t)n);
    out[n] = '\0';
    return (long long)n;
#endif
}

long long zan_dir_list_into(const char *pattern, char *out, long long cap) {
    if (!out || cap <= 0) return 0;
    out[0] = '\0';
    if (!pattern) return 0;
    long long len = 0;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        long long nl = (long long)strlen(fd.cFileName);
        if (len + nl + 2 > cap) break;
        memcpy(out + len, fd.cFileName, (size_t)nl);
        len += nl;
        out[len++] = '\n';
        out[len] = '\0';
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    glob_t g;
    if (glob(pattern, 0, NULL, &g) != 0) return 0;
    for (size_t i = 0; i < g.gl_pathc; i++) {
        const char *base = strrchr(g.gl_pathv[i], '/');
        base = base ? base + 1 : g.gl_pathv[i];
        long long nl = (long long)strlen(base);
        if (len + nl + 2 > cap) break;
        memcpy(out + len, base, (size_t)nl);
        len += nl;
        out[len++] = '\n';
        out[len] = '\0';
    }
    globfree(&g);
#endif
    return len;
}

