static int embed_push(zan_embed_list_t *l, char *name,
                      unsigned char *data, long long len) {
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 32;
        zan_embed_file_t *v = (zan_embed_file_t *)realloc(l->v,
            (size_t)cap * sizeof(*v));
        if (!v) return 0;
        l->v = v;
        l->cap = cap;
    }
    l->v[l->n].name = name;
    l->v[l->n].data = data;
    l->v[l->n].len = len;
    l->n++;
    return 1;
}

static unsigned char *embed_read_file(const char *path, long long *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    rewind(f);
    unsigned char *buf = (unsigned char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = size > 0 ? fread(buf, 1, (size_t)size, f) : 0;
    fclose(f);
    buf[got] = 0;
    *out_len = (long long)got;
    return buf;
}

static char *embed_join(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    char *s = (char *)malloc(la + lb + 2);
    if (!s) return NULL;
    if (la) {
        memcpy(s, a, la);
        s[la] = '/';
        memcpy(s + la + 1, b, lb + 1);
    } else {
        memcpy(s, b, lb + 1);
    }
    return s;
}

static int embed_add_file(zan_embed_list_t *l, const char *path,
                          const char *name) {
    long long len = 0;
    unsigned char *data = embed_read_file(path, &len);
    if (!data) {
        fprintf(stderr, "warning: cannot embed '%s' (unreadable)\n", path);
        return 0;
    }
    char *dup = (char *)malloc(strlen(name) + 1);
    if (!dup) { free(data); return 0; }
    memcpy(dup, name, strlen(name) + 1);
    if (!embed_push(l, dup, data, len)) { free(dup); free(data); return 0; }
    return 1;
}

#define EMBED_WALK_MAX_DEPTH 128

static void embed_walk_impl(zan_embed_list_t *l, const char *dir, const char *name, int depth) {
    if (depth > EMBED_WALK_MAX_DEPTH) return;
#ifdef _WIN32
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    wchar_t *wide_pattern = zan_utf8_to_wide_alloc(pattern);
    if (!wide_pattern) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wide_pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        free(wide_pattern);
        return;
    }
    free(wide_pattern);
    do {
        char *file_name = zan_wide_to_utf8_alloc(fd.cFileName);
        if (!file_name) continue;
        if (strcmp(file_name, ".") == 0 || strcmp(file_name, "..") == 0) {
            free(file_name);
            continue;
        }
        char path[1024];
        snprintf(path, sizeof(path), "%s\\%s", dir, file_name);
        char *sub = embed_join(name, file_name);
        free(file_name);
        if (!sub) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                free(sub);
                continue;
            }
            embed_walk_impl(l, path, sub, depth + 1);
        } else {
            embed_add_file(l, path, sub);
        }
        free(sub);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (lstat(path, &st) != 0) continue;
        char *sub = embed_join(name, e->d_name);
        if (!sub) continue;
        if (S_ISDIR(st.st_mode)) embed_walk_impl(l, path, sub, depth + 1);
        else if (S_ISREG(st.st_mode)) embed_add_file(l, path, sub);
        free(sub);
    }
    closedir(d);
#endif
}

static void embed_walk(zan_embed_list_t *l, const char *dir, const char *name) {
    embed_walk_impl(l, dir, name, 1);
}

static void embed_walk_filtered_impl(zan_embed_list_t *l, const char *dir,
                                     const char *name, int depth,
                                     const char *const *filter,
                                     int filter_count) {
    if (depth > EMBED_WALK_MAX_DEPTH) return;
#ifdef _WIN32
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    wchar_t *wide_pattern = zan_utf8_to_wide_alloc(pattern);
    if (!wide_pattern) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wide_pattern, &fd);
    free(wide_pattern);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        char *file_name = zan_wide_to_utf8_alloc(fd.cFileName);
        if (!file_name) continue;
        if (strcmp(file_name, ".") == 0 || strcmp(file_name, "..") == 0) {
            free(file_name);
            continue;
        }
        char path[1024];
        snprintf(path, sizeof(path), "%s\\%s", dir, file_name);
        char *sub = embed_join(name, file_name);
        free(file_name);
        if (!sub) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { free(sub); continue; }
            int keep = depth == 1 ? 0 : 1;
            const char *leaf = strrchr(sub, '/');
            leaf = leaf ? leaf + 1 : sub;
            for (int f = 0; depth == 1 && f < filter_count; f++) {
                if (strcmp(filter[f], leaf) == 0) { keep = 1; break; }
            }
            if (keep) {
                if (depth == 1) embed_walk_impl(l, path, sub, depth + 1);
                else embed_walk_filtered_impl(l, path, sub, depth + 1,
                                              filter, filter_count);
            }
        } else {
            embed_add_file(l, path, sub);
        }
        free(sub);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (lstat(path, &st) != 0) continue;
        char *sub = embed_join(name, e->d_name);
        if (!sub) continue;
        if (S_ISDIR(st.st_mode)) {
            int keep = depth == 1 ? 0 : 1;
            const char *leaf = strrchr(sub, '/');
            leaf = leaf ? leaf + 1 : sub;
            for (int f = 0; depth == 1 && f < filter_count; f++) {
                if (strcmp(filter[f], leaf) == 0) { keep = 1; break; }
            }
            if (keep) {
                if (depth == 1) embed_walk_impl(l, path, sub, depth + 1);
                else embed_walk_filtered_impl(l, path, sub, depth + 1,
                                              filter, filter_count);
            }
        } else if (S_ISREG(st.st_mode)) embed_add_file(l, path, sub);
        free(sub);
    }
    closedir(d);
#endif
}

static void embed_walk_filtered(zan_embed_list_t *l, const char *dir,
                                const char *name,
                                const char *const *filter, int filter_count) {
    embed_walk_filtered_impl(l, dir, name, 1, filter, filter_count);
}

/* ---------- 发布态设计稿自动发现 ----------
 * Gui 程序的界面设计稿（*.html）在 --publish 时必须随 exe 走（发布态
 * 加密内嵌，不留在盘上）。这里只负责按工程根收集候选：
 *   - 只扫 <root>/src（设计稿与源码同源的工程布局约定）；
 *   - 跳表排除构建产物、依赖副本与文档测试等非设计稿 html；
 *   - spec 形如 "<绝对路径>=<工程根相对路径>"，相对段用 '/'，
 *     与 Form.Show 的 FindDoc 查找名一致。
 * 是否启用由 main.c 的门控决定（publish 模式 + 源码出现 " : Form"
 * 窗口类记号）——服务端/web 程序的 html 是运行期盘上资产，绝不自动内嵌。 */

static int embed_design_dir_skipped(const char *leaf) {
    static const char *const skip[] = {
        ".git", ".svn", ".hg", "build", "dist", "bin", "obj", "out",
        "node_modules", "packages", ".zan-packages", "_scratch",
        "docs", "doc", "tests", "test", ".idea", ".vs",
    };
    for (size_t i = 0; i < sizeof(skip) / sizeof(skip[0]); i++)
        if (strcmp(leaf, skip[i]) == 0) return 1;
    return 0;
}

static int embed_tail_eq_ci(const char *leaf, const char *tail) {
    size_t ln = strlen(leaf), lt = strlen(tail);
    if (ln < lt) return 0;
    for (size_t i = 0; i < lt; i++) {
        char a = leaf[ln - lt + i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != tail[i]) return 0;
    }
    return 1;
}

typedef struct {
    char (*specs)[1300];
    int max_specs;
    int count;
    int truncated;
} design_scan_t;

static void design_scan_impl(design_scan_t *s, const char *dir,
                             const char *rel, int depth) {
    if (s->truncated || depth > 24) return;
#ifdef _WIN32
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    wchar_t *wide_pattern = zan_utf8_to_wide_alloc(pattern);
    if (!wide_pattern) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wide_pattern, &fd);
    free(wide_pattern);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        char *file_name = zan_wide_to_utf8_alloc(fd.cFileName);
        if (!file_name) continue;
        if (strcmp(file_name, ".") == 0 || strcmp(file_name, "..") == 0) {
            free(file_name);
            continue;
        }
        char path[1024];
        snprintf(path, sizeof(path), "%s\\%s", dir, file_name);
        char *sub = embed_join(rel, file_name);
        free(file_name);
        if (!sub) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { free(sub); continue; }
            const char *leaf = strrchr(sub, '/');
            leaf = leaf ? leaf + 1 : sub;
            if (!embed_design_dir_skipped(leaf))
                design_scan_impl(s, path, sub, depth + 1);
        } else if (embed_tail_eq_ci(sub, ".html") ||
                   embed_tail_eq_ci(sub, ".htm")) {
            if (s->count >= s->max_specs) { s->truncated = 1; free(sub); break; }
            snprintf(s->specs[s->count], 1300, "%s=src/%s", path, sub);
            s->count++;
        }
        free(sub);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (lstat(path, &st) != 0) continue;
        char *sub = embed_join(rel, e->d_name);
        if (!sub) continue;
        if (S_ISDIR(st.st_mode)) {
            if (S_ISLNK(st.st_mode)) { free(sub); continue; }
            const char *leaf = strrchr(sub, '/');
            leaf = leaf ? leaf + 1 : sub;
            if (!embed_design_dir_skipped(leaf))
                design_scan_impl(s, path, sub, depth + 1);
        } else if (S_ISREG(st.st_mode) &&
                   (embed_tail_eq_ci(sub, ".html") ||
                    embed_tail_eq_ci(sub, ".htm"))) {
            if (s->count >= s->max_specs) { s->truncated = 1; free(sub); break; }
            snprintf(s->specs[s->count], 1300, "%s=src/%s", path, sub);
            s->count++;
        }
        free(sub);
    }
    closedir(d);
#endif
}

int zan_embed_collect_design_html(const char *project_root,
                                  char (*specs)[1300], int max_specs) {
    char src_dir[1024];
    snprintf(src_dir, sizeof(src_dir), "%s/src", project_root);
    design_scan_t s;
    memset(&s, 0, sizeof(s));
    s.specs = specs;
    s.max_specs = max_specs;
    design_scan_impl(&s, src_dir, "", 1);
    return s.truncated ? -1 : s.count;
}

static int handler_attr_is_event(const char *s, size_t n) {
    if (n > 1 && s[0] == '@') return 1;
    if (n > 5 && (memcmp(s, "v-on:", 5) == 0 || memcmp(s, "z-on:", 5) == 0)) return 1;
    if (n > 8 && memcmp(s, "data-on-", 8) == 0) return 1;
    if (n > 2 && s[0] == 'o' && s[1] == 'n') {
        for (size_t i = 2; i < n; i++)
            if (s[i] < 'a' || s[i] > 'z') return 0;
        return 1;
    }
    return 0;
}

static int handler_is_attr_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ':' || c == '@';
}

static int handler_is_ident_char(char c, int first) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           (!first && c >= '0' && c <= '9');
}

static void handler_add(char ***names, int *count, int *cap,
                        const char *s, size_t n) {
    for (int i = 0; i < *count; i++)
        if (strlen((*names)[i]) == n && memcmp((*names)[i], s, n) == 0) return;
    if (*count == *cap) {
        int ncap = *cap ? *cap * 2 : 64;
        char **nn = (char **)realloc(*names, (size_t)ncap * sizeof(char *));
        if (!nn) return;
        *names = nn;
        *cap = ncap;
    }
    char *d = (char *)malloc(n + 1);
    if (!d) return;
    memcpy(d, s, n);
    d[n] = 0;
    (*names)[(*count)++] = d;
}

static void handler_scan_text(const char *t, size_t len, char ***names,
                              int *count, int *cap) {
    for (size_t i = 1; i + 1 < len; i++) {
        if (t[i] != '=' || (t[i + 1] != '"' && t[i + 1] != 39)) continue;
        size_t a = i;
        while (a > 0 && handler_is_attr_char(t[a - 1])) a--;
        if (!handler_attr_is_event(t + a, i - a)) continue;
        size_t v = i + 2, e = v;
        while (e < len && handler_is_ident_char(t[e], e == v)) e++;
        if (e > v && e < len && (t[e] == t[i + 1] || t[e] == '('))
            handler_add(names, count, cap, t + v, e - v);
    }
}

int zan_embed_collect_html_handlers(const char *project_root, char ***out_names) {
    static char specs[256][1300];
    int nd = zan_embed_collect_design_html(project_root, specs, 256);
    if (nd < 0) nd = 256;
    char **names = NULL;
    int count = 0, cap = 0;
    for (int i = 0; i < nd; i++) {
        char *eq = strrchr(specs[i], '=');
        if (!eq) continue;
        *eq = 0;
        FILE *f = fopen(specs[i], "rb");
        *eq = '=';
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = sz > 0 ? (char *)malloc((size_t)sz) : NULL;
        if (buf && fread(buf, 1, (size_t)sz, f) == (size_t)sz)
            handler_scan_text(buf, (size_t)sz, &names, &count, &cap);
        free(buf);
        fclose(f);
    }
    *out_names = names;
    return count;
}

static int embed_is_dir(const char *path) {
