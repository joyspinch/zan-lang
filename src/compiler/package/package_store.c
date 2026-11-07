static bool pkg_safe_component(const char *s) {
    if (!s || !*s || strcmp(s, ".") == 0 || strcmp(s, "..") == 0) return false;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (!isalnum(c) && c != '_' && c != '-' && c != '.' && c != '@') return false;
    }
    return true;
}

static bool pkg_safe_namespace_path(const char *s) {
    if (!s || !*s || s[0] == '/' || s[0] == '\\') return false;
    char part[128]; size_t n = 0;
    for (;;) {
        char c = *s++;
        if (c == '/' || c == '\\' || c == 0) {
            if (n == 0 || n >= sizeof(part)) return false;
            part[n] = 0;
            if (!pkg_safe_component(part)) return false;
            n = 0;
            if (!c) return true;
        } else if (n + 1 < sizeof(part)) {
            part[n++] = c;
        } else return false;
    }
}

static bool pkg_is_dir(const char *path) {
#ifdef _WIN32
    char norm[1024];
    snprintf(norm, sizeof(norm), "%s", path);
    for (char *p = norm; *p; p++)
        if (*p == '/') *p = '\\';
    DWORD a = GetFileAttributesA(norm);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool zan_pkg_global_store(char *out, size_t out_size) {
    const char *base;
#ifdef _WIN32
    base = getenv("LOCALAPPDATA");
    if (!base || !*base) return false;
    return snprintf(out, out_size, "%s\\Zan\\packages", base) > 0 &&
           strlen(out) < out_size;
#else
    base = getenv("XDG_DATA_HOME");
    if (base && *base)
        return snprintf(out, out_size, "%s/zan/packages", base) > 0 &&
               strlen(out) < out_size;
    base = getenv("HOME");
    if (!base || !*base) return false;
    return snprintf(out, out_size, "%s/.local/share/zan/packages", base) > 0 &&
           strlen(out) < out_size;
#endif
}

static bool pkg_dir_in_list(const char (*out_dirs)[1024], int count, const char *dir) {
    for (int i = 0; i < count; i++) {
        if (strcmp(out_dirs[i], dir) == 0) return true;
    }
    return false;
}


static int pkg_names_cmp(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static char **pkg_dir_names_sorted(const char *dir, int *out_count) {
    *out_count = 0;
    int cap = 64, count = 0;
    char **names = (char **)malloc((size_t)cap * sizeof(char *));
    if (!names) return NULL;
#ifdef _WIN32
    char pattern[1024]; WIN32_FIND_DATAA fd;
    if (snprintf(pattern, sizeof(pattern), "%s\\*", dir) < (int)sizeof(pattern)) {
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(fd.cFileName, ".") == 0 ||
                    strcmp(fd.cFileName, "..") == 0) continue;
                if (count == cap) {
                    int ncap = cap * 2;
                    char **grown = (char **)realloc(names,
                        (size_t)ncap * sizeof(char *));
                    if (!grown) break;
                    names = grown;
                    cap = ncap;
                }
                size_t len = strlen(fd.cFileName);
                names[count] = (char *)malloc(len + 1);
                if (!names[count]) break;
                memcpy(names[count], fd.cFileName, len + 1);
                count++;
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
#else
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") == 0 ||
                strcmp(e->d_name, "..") == 0) continue;
            if (count == cap) {
                int ncap = cap * 2;
                char **grown = (char **)realloc(names,
                    (size_t)ncap * sizeof(char *));
                if (!grown) break;
                names = grown;
                cap = ncap;
            }
            size_t len = strlen(e->d_name);
            names[count] = (char *)malloc(len + 1);
            if (!names[count]) break;
            memcpy(names[count], e->d_name, len + 1);
            count++;
        }
        closedir(d);
    }
#endif
    if (count > 1) qsort(names, (size_t)count, sizeof(char *), pkg_names_cmp);
    *out_count = count;
    return names;
}

static void pkg_dir_names_free(char **names, int count) {
    if (!names) return;
    for (int i = 0; i < count; i++) free(names[i]);
    free(names);
}

/* 目录枚举（排序）+ 就地属性：FindFirstFileA/readdir 本身就带类型，
 * 不必再对每个条目补一次 GetFileAttributesA/lstat——包源索引全树遍历
 * 一趟要碰 ~10^4 个条目，逐条目补 stat 的系统调用在小过滤驱动的机器
 * 上每次都是一次被拦截的打开。flags bit0=is_dir，bit1=reparse/symlink。 */
typedef struct {
    char **names;
    unsigned char *flags;
    int count;
} pkg_dir_entries_t;

typedef struct {
    char *name;
    unsigned char flag;
} pkg_dir_pair_t;

static int pkg_dir_pair_cmp(const void *a, const void *b) {
    const pkg_dir_pair_t *pa = (const pkg_dir_pair_t *)a;
    const pkg_dir_pair_t *pb = (const pkg_dir_pair_t *)b;
    return strcmp(pa->name, pb->name);
}

static pkg_dir_entries_t *pkg_dir_entries_sorted(const char *dir) {
    pkg_dir_entries_t *e = (pkg_dir_entries_t *)calloc(1, sizeof(*e));
    if (!e) return NULL;
    int cap = 64, count = 0;
    e->names = (char **)malloc((size_t)cap * sizeof(char *));
    e->flags = (unsigned char *)malloc((size_t)cap * sizeof(unsigned char));
    if (!e->names || !e->flags) {
        free(e->names);
        free(e->flags);
        free(e);
        return NULL;
    }
#ifdef _WIN32
    char pattern[1024];
    WIN32_FIND_DATAA fd;
    if (snprintf(pattern, sizeof(pattern), "%s\\*", dir) < (int)sizeof(pattern)) {
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(fd.cFileName, ".") == 0 ||
                    strcmp(fd.cFileName, "..") == 0) continue;
                if (count == cap) {
                    int ncap = cap * 2;
                    char **gn = (char **)realloc(e->names, (size_t)ncap * sizeof(char *));
                    unsigned char *gf = (unsigned char *)realloc(e->flags, (size_t)ncap);
                    if (!gn || !gf) break;
                    e->names = gn;
                    e->flags = gf;
                    cap = ncap;
                }
                size_t len = strlen(fd.cFileName);
                e->names[count] = (char *)malloc(len + 1);
                if (!e->names[count]) break;
                memcpy(e->names[count], fd.cFileName, len + 1);
                e->flags[count] = (unsigned char)(
                    ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0) |
                    ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? 2 : 0));
                count++;
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
#else
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 ||
                strcmp(de->d_name, "..") == 0) continue;
            if (count == cap) {
                int ncap = cap * 2;
                char **gn = (char **)realloc(e->names, (size_t)ncap * sizeof(char *));
                unsigned char *gf = (unsigned char *)realloc(e->flags, (size_t)ncap);
                if (!gn || !gf) break;
                e->names = gn;
                e->flags = gf;
                cap = ncap;
            }
            size_t len = strlen(de->d_name);
            e->names[count] = (char *)malloc(len + 1);
            if (!e->names[count]) break;
            memcpy(e->names[count], de->d_name, len + 1);
            unsigned char fl = 0;
            if (de->d_type == DT_DIR) fl = 1;
            else if (de->d_type == DT_LNK) fl = 2;
            else if (de->d_type == DT_UNKNOWN) {
                char ep[1024];
                struct stat st;
                if (snprintf(ep, sizeof(ep), "%s" PATH_SEP "%s", dir, de->d_name)
                    < (int)sizeof(ep) && lstat(ep, &st) == 0)
                    fl = (unsigned char)(S_ISDIR(st.st_mode) ? 1
                                         : (S_ISLNK(st.st_mode) ? 2 : 0));
            }
            e->flags[count] = fl;
            count++;
        }
        closedir(d);
    }
#endif
    if (count > 1) {
        pkg_dir_pair_t *pairs = (pkg_dir_pair_t *)malloc((size_t)count * sizeof(pkg_dir_pair_t));
        if (pairs) {
            for (int i = 0; i < count; i++) {
                pairs[i].name = e->names[i];
                pairs[i].flag = e->flags[i];
            }
            qsort(pairs, (size_t)count, sizeof(pkg_dir_pair_t), pkg_dir_pair_cmp);
            for (int i = 0; i < count; i++) {
                e->names[i] = pairs[i].name;
                e->flags[i] = pairs[i].flag;
            }
            free(pairs);
        }
    }
    e->count = count;
    return e;
}

static void pkg_dir_entries_free(pkg_dir_entries_t *e) {
    if (!e) return;
    if (e->names) {
        for (int i = 0; i < e->count; i++) free(e->names[i]);
        free(e->names);
    }
    free(e->flags);
    free(e);
}

static long g_pkg_probe_calls = 0;
static long g_pkg_dirs_uniform = 0;
static long g_pkg_dirs_total = 0;

static double zan_pkg_now_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#endif
}

static int pkg_cpu_count(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#endif
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) void zan_compile_trace(const char *fmt, ...) {
    (void)fmt;
}
#endif

/* 包源索引两阶段构建：阶段一走树只「收集」（uniform 目录内联一个首文
 * 件 probe 定案），阶段二对待 probe 文件并行 probe（纯只读、无共享写，
 * 单线程 ~6000 次 fopen 在小过滤驱动的机器上要 2-5s——每次编译的固定
 * 税），最后按走树原次序单线程注册，保证与旧实现注册次序逐项一致。 */
typedef struct {
    char *path;
    char *src_root;
    struct pkg_index_package *package;
    int project_owned;
    int uniform;        /* 1: declared 已知 = dir_ns */
    char dir_ns[256];   /* 目录推导命名空间（回退用） */
    char declared[256]; /* probe 结果（仅 !uniform） */
    int has_ns;
} pkg_probe_item_t;

typedef struct {
    pkg_probe_item_t *items;
    int count;
    int cap;
} pkg_probe_list_t;

static void pkg_probe_list_push(pkg_probe_list_t *list, const pkg_probe_item_t *it) {
    if (list->count == list->cap) {
        int ncap = list->cap ? list->cap * 2 : 256;
        pkg_probe_item_t *grown = (pkg_probe_item_t *)realloc(
            list->items, (size_t)ncap * sizeof(*grown));
        if (!grown) {
            zan_host_oom();
        }
        list->items = grown;
        list->cap = ncap;
    }
    list->items[list->count++] = *it;
}

static void pkg_probe_list_free(pkg_probe_list_t *list) {
    for (int i = 0; i < list->count; i++) {
        free(list->items[i].path);
        free(list->items[i].src_root);
    }
    free(list->items);
    list->items = NULL;
    list->count = list->cap = 0;
}

/* ---- B-ID90: 源码树确定性哈希 ----
 * 只对 manifest（zan.pkg）算 SHA-256 挡不住源码被篡改。这里对整个包目录
 * 做确定性遍历：相对路径排序后，逐文件哈希「相对路径 + NUL + 8 字节大端
 * 长度 + 内容」。.git 目录不入账（clone 元数据非包内容）。 */
static bool pkg_tree_hash_dir(const char *dir, const char *rel,
                              zan_sha256_ctx_t *ctx, int depth) {
    if (depth > 32) return false;
    int count = 0;
    char **names = pkg_dir_names_sorted(dir, &count);
    if (!names && count == 0) return false;
    bool ok = true;
    for (int i = 0; i < count && ok; i++) {
        const char *name = names[i];
        if (strcmp(name, ".git") == 0) continue;
        char child[1024], child_rel[1024];
        if (snprintf(child, sizeof(child), "%s" PATH_SEP "%s", dir, name)
            >= (int)sizeof(child)) { ok = false; break; }
        if (rel[0])
            snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, name);
        else
            snprintf(child_rel, sizeof(child_rel), "%s", name);
        if (pkg_is_dir(child)) {
            ok = pkg_tree_hash_dir(child, child_rel, ctx, depth + 1);
            continue;
        }
        FILE *f = fopen(child, "rb");
        if (!f) { ok = false; break; }
        /* 路径与长度先入账，再回卷读内容——改一个字节必变哈希 */
        zan_sha256_update(ctx, (const uint8_t *)child_rel, strlen(child_rel));
        uint8_t nul = 0;
        zan_sha256_update(ctx, &nul, 1);
        long sz = -1;
        if (fseek(f, 0, SEEK_END) == 0) {
            sz = ftell(f);
            fseek(f, 0, SEEK_SET);
        }
        uint8_t szb[8];
        for (int k = 0; k < 8; k++) szb[k] = (uint8_t)((sz >> (56 - k * 8)) & 0xff);
        zan_sha256_update(ctx, szb, 8);
        uint8_t buf[8192];
        size_t n, total = 0;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            zan_sha256_update(ctx, buf, n);
            total += n;
        }
        fclose(f);
        if (sz >= 0 && total != (size_t)sz) ok = false;
    }
    pkg_dir_names_free(names, count);
    return ok;
}

bool zan_pkg_compute_tree_checksum(const char *dir, char out_hex[65]) {
    if (!dir || !out_hex) return false;
    if (!pkg_is_dir(dir)) return false;
    zan_sha256_ctx_t ctx;
    zan_sha256_init(&ctx);
    if (!pkg_tree_hash_dir(dir, "", &ctx, 0)) return false;
    uint8_t digest[32];
    zan_sha256_final(&ctx, digest);
    for (int i = 0; i < 32; i++)
        sprintf(out_hex + (i * 2), "%02x", digest[i]);
    out_hex[64] = 0;
    return true;
}

static int pkg_scan_store(const char *store, const char *namespace_path,
                          char (*out_dirs)[1024], int count, int max_dirs) {
    if (!pkg_is_dir(store)) return count;
    int n = 0;
    char **names = pkg_dir_names_sorted(store, &n);
    if (!names) return count;
    for (int i = 0; i < n; i++) {
        const char *name = names[i];
        if (!pkg_safe_component(name)) continue;
        char root[1024], cand[1024];
        if (snprintf(root, sizeof(root), "%s" PATH_SEP "%s", store, name)
            >= (int)sizeof(root)) continue;
#ifdef _WIN32
        DWORD attr = GetFileAttributesA(root);
        if (attr == INVALID_FILE_ATTRIBUTES ||
            (attr & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
#else
        struct stat st;
        if (lstat(root, &st) != 0 || !S_ISDIR(st.st_mode) ||
            S_ISLNK(st.st_mode)) continue;
#endif
        /* 1 */
        snprintf(cand, sizeof(cand), "%s" PATH_SEP "src" PATH_SEP "%s",
                 root, namespace_path);
        if (!pkg_is_dir(cand)) {
            snprintf(cand, sizeof(cand), "%s" PATH_SEP "stdlib" PATH_SEP "%s",
                     root, namespace_path);
        }
        if (!pkg_is_dir(cand)) {
            snprintf(cand, sizeof(cand), "%s" PATH_SEP "%s",
                     root, namespace_path);
        }
        if (pkg_is_dir(cand) && count < max_dirs &&
            !pkg_dir_in_list(out_dirs, count, cand)) {
            snprintf(out_dirs[count++], 1024, "%s", cand);
            zan_pkg_note_usage(store, name);
        }
    }
    pkg_dir_names_free(names, n);
    return count;
}


#define PKG_NAMESPACE_BUCKETS 1024

typedef struct pkg_index_package {
    char *store;
    char *name;
    struct pkg_index_package *next;
} pkg_index_package_t;

typedef struct pkg_index_source {
    char *path;
    pkg_index_package_t *package;
    struct pkg_index_source *next;
} pkg_index_source_t;

typedef struct {
    pkg_index_source_t *source;
    int descendant;
} pkg_namespace_source_t;

typedef struct pkg_index_namespace {
    char name[256];
    pkg_namespace_source_t *sources;
    int count, capacity;
    struct pkg_index_namespace *next;
} pkg_index_namespace_t;

struct zan_pkg_source_index {
    pkg_index_namespace_t *namespaces[PKG_NAMESPACE_BUCKETS];
    pkg_index_package_t *packages;
    pkg_index_source_t *sources;
};

static char *pkg_index_copy(const char *s) {
    size_t n = strlen(s) + 1;
    char *copy = (char *)malloc(n);
    memcpy(copy, s, n);
    return copy;
}

