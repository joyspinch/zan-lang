static void canon_key(const char *in, char *out, size_t out_sz) {
#ifdef _WIN32
    char full[4096];
    if (!zan_utf8_full_path(in, full, sizeof(full)))
        snprintf(full, sizeof(full), "%s", in);
    size_t j = 0;
    for (size_t i = 0; full[i] && j + 1 < out_sz; i++) {
        char c = full[i];
        if (c == '/') c = '\\';
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[j++] = c;
    }
    out[j] = '\0';
#else
    char full[4096];
    if (realpath(in, full) == NULL) snprintf(full, sizeof(full), "%s", in);
    snprintf(out, out_sz, "%s", full);
#endif
}

typedef struct {
    char *key;
#ifndef _WIN32
    dev_t dev;
    ino_t ino;
    int have_stat;
#endif
} input_key_t;

static input_key_t *input_keys = NULL;
static int input_key_count = 0;
static int input_key_cap = 0;
static int *input_key_idx = NULL;
static int input_key_idx_cap = 0;
#ifndef _WIN32
static int *input_ino_idx = NULL;
static int input_ino_idx_cap = 0;
#endif

static uint64_t input_hash_str(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return h;
}

#ifndef _WIN32
static uint64_t input_hash_ino(dev_t dev, ino_t ino) {
    uint64_t h = (uint64_t)dev;
    h = h * 0x9E3779B97F4A7C15ULL + (uint64_t)ino;
    h ^= h >> 29;
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 32;
    return h;
}
#endif

static void input_key_idx_rehash(int ncap) {
    int *grown = (int *)malloc((size_t)ncap * sizeof(*grown));
    if (!grown) {
        fprintf(stderr, "error: out of memory tracking input files\n");
        exit(1);
    }
    for (int i = 0; i < ncap; i++) grown[i] = -1;
    for (int i = 0; i < input_key_count; i++) {
        size_t j = (size_t)input_hash_str(input_keys[i].key) & (size_t)(ncap - 1);
        while (grown[j] >= 0) j = (j + 1) & (size_t)(ncap - 1);
        grown[j] = i;
    }
    free(input_key_idx);
    input_key_idx = grown;
    input_key_idx_cap = ncap;
}

#ifndef _WIN32
static void input_ino_idx_rehash(int ncap) {
    int *grown = (int *)malloc((size_t)ncap * sizeof(*grown));
    if (!grown) {
        fprintf(stderr, "error: out of memory tracking input files\n");
        exit(1);
    }
    for (int i = 0; i < ncap; i++) grown[i] = -1;
    for (int i = 0; i < input_key_count; i++) {
        if (!input_keys[i].have_stat) continue;
        size_t j = (size_t)input_hash_ino(input_keys[i].dev,
                                          input_keys[i].ino) & (size_t)(ncap - 1);
        while (grown[j] >= 0) j = (j + 1) & (size_t)(ncap - 1);
        grown[j] = i;
    }
    free(input_ino_idx);
    input_ino_idx = grown;
    input_ino_idx_cap = ncap;
}
#endif

static void input_key_add(const char *path) {
    if (input_key_count == input_key_cap) {
        int ncap = input_key_cap ? input_key_cap * 2 : 16;
        input_key_t *grown =
            (input_key_t *)realloc(input_keys, (size_t)ncap * sizeof(*grown));
        if (!grown) {
            fprintf(stderr, "error: out of memory tracking input files\n");
            exit(1);
        }
        input_keys = grown;
        input_key_cap = ncap;
    }
    char k[1024];
    canon_key(path, k, sizeof(k));
    input_key_t *e = &input_keys[input_key_count++];
    e->key = (char *)malloc(strlen(k) + 1);
    if (!e->key) {
        fprintf(stderr, "error: out of memory tracking input files\n");
        exit(1);
    }
    memcpy(e->key, k, strlen(k) + 1);
#ifndef _WIN32
    struct stat st;
    e->have_stat = stat(path, &st) == 0;
    if (e->have_stat) { e->dev = st.st_dev; e->ino = st.st_ino; }
#endif
    if ((input_key_count + 1) * 2 >= input_key_idx_cap)
        input_key_idx_rehash(input_key_idx_cap ? input_key_idx_cap * 2 : 64);
    size_t j = (size_t)input_hash_str(e->key) & (size_t)(input_key_idx_cap - 1);
    while (input_key_idx[j] >= 0) j = (j + 1) & (size_t)(input_key_idx_cap - 1);
    input_key_idx[j] = input_key_count - 1;
#ifndef _WIN32
    if (e->have_stat) {
        if ((input_key_count + 1) * 2 >= input_ino_idx_cap)
            input_ino_idx_rehash(input_ino_idx_cap ? input_ino_idx_cap * 2 : 64);
        size_t ij = (size_t)input_hash_ino(e->dev, e->ino)
                    & (size_t)(input_ino_idx_cap - 1);
        while (input_ino_idx[ij] >= 0) ij = (ij + 1) & (size_t)(input_ino_idx_cap - 1);
        input_ino_idx[ij] = input_key_count - 1;
    }
#endif
}

static int input_file_present(const char *cand) {
    char ck[1024];
    canon_key(cand, ck, sizeof(ck));
    if (input_key_idx_cap) {
        size_t j = (size_t)input_hash_str(ck) & (size_t)(input_key_idx_cap - 1);
        while (input_key_idx[j] >= 0) {
            if (strcmp(ck, input_keys[input_key_idx[j]].key) == 0) return 1;
            j = (j + 1) & (size_t)(input_key_idx_cap - 1);
        }
    }
#ifndef _WIN32
    struct stat cand_stat;
    int have_cand_stat = stat(cand, &cand_stat) == 0;
    if (have_cand_stat && input_ino_idx_cap) {
        size_t j = (size_t)input_hash_ino(cand_stat.st_dev, cand_stat.st_ino)
                   & (size_t)(input_ino_idx_cap - 1);
        while (input_ino_idx[j] >= 0) {
            input_key_t *e = &input_keys[input_ino_idx[j]];
            if (e->have_stat && e->dev == cand_stat.st_dev &&
                e->ino == cand_stat.st_ino) return 1;
            j = (j + 1) & (size_t)(input_ino_idx_cap - 1);
        }
    }
#endif
    return 0;
}

void input_files_push(const char ***files, int *count, int *cap,
                             const char *path) {
    if (*count == *cap) {
        int ncap = *cap ? *cap * 2 : 16;
        const char **grown =
            (const char **)realloc((void *)*files, (size_t)ncap * sizeof(*grown));
        if (!grown) {
            fprintf(stderr, "error: out of memory tracking input files\n");
            exit(1);
        }
        *files = grown;
        *cap = ncap;
    }
    (*files)[(*count)++] = path;
    input_key_add(path);
}

static void add_stdlib_input(const char ***files, int *count, int *cap,
                             const char *path) {
    FILE *check = fopen(path, "rb");
    if (!check) return;
    fclose(check);
    if (input_file_present(path)) return;
    char *dup = (char *)malloc(strlen(path) + 1);
    if (!dup) zan_host_oom();
    memcpy(dup, path, strlen(path) + 1);
    input_files_push(files, count, cap, dup);
}

#ifndef _WIN32
static int resolve_dir_component(const char *parent, const char *component,
                                 char *resolved, size_t resolved_sz) {
    DIR *dir = opendir(parent);
    if (!dir) return 0;
    char match_name[256] = {0};
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcasecmp(ent->d_name, component) != 0) continue;
        if (!match_name[0] || strcmp(ent->d_name, component) == 0)
            snprintf(match_name, sizeof(match_name), "%s", ent->d_name);
        if (strcmp(ent->d_name, component) == 0) break;
    }
    closedir(dir);
    if (!match_name[0]) return 0;
    char candidate[1024];
    int n = snprintf(candidate, sizeof(candidate), "%s/%s", parent, match_name);
    if (n < 0 || (size_t)n >= sizeof(candidate)) return 0;
    DIR *match = opendir(candidate);
    if (!match) return 0;
    closedir(match);
    snprintf(resolved, resolved_sz, "%s", candidate);
    return 1;
}

static int resolve_stdlib_dir(const char *stdlib_root, const char *subdir,
                              char *resolved, size_t resolved_sz) {
    char current[1024];
    snprintf(current, sizeof(current), "%s", stdlib_root);
    const char *p = subdir;
    while (*p) {
        char component[256];
        size_t len = 0;
        while (p[len] && p[len] != '/') len++;
        if (len == 0 || len >= sizeof(component)) return 0;
        memcpy(component, p, len);
        component[len] = '\0';
        char next[1024];
        if (!resolve_dir_component(current, component, next, sizeof(next))) return 0;
        snprintf(current, sizeof(current), "%s", next);
        p += len;
        if (*p == '/') p++;
    }
    snprintf(resolved, resolved_sz, "%s", current);
    return 1;
}
#endif

static int stdlib_has_dir(const char *stdlib_root, const char *subdir) {
    if (!subdir) return 0;
    if (strncmp(subdir, "System", 6) == 0 &&
        (subdir[6] == '\0' || subdir[6] == '/' || subdir[6] == '\\' || subdir[6] == '.')) {
        return 1;
    }
#ifdef _WIN32
    char p[1024];
    if (snprintf(p, sizeof(p), "%s\\%s", stdlib_root, subdir) >= (int)sizeof(p)) return 0;
    for (char *q = p; *q; q++) if (*q == '/') *q = '\\';
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    char r[1024];
    return resolve_stdlib_dir(stdlib_root, subdir, r, sizeof(r));
#endif
}

static char **globbed_dirs = NULL;
static int globbed_dir_count = 0;
static int globbed_dir_cap = 0;
static int missing_namespace_count = 0;
char package_project_root[1024] = ".";
static char **project_namespaces = NULL;
static int project_namespace_count = 0;
static int project_namespace_cap = 0;

static int project_namespace_declared(const char *name) {
    for (int i = 0; i < project_namespace_count; i++) {
        const char *decl = project_namespaces[i];
        size_t dn = strlen(decl), nn = strlen(name);
        if (strcmp(decl, name) == 0) return 1;
        if (dn > nn && strncmp(decl, name, nn) == 0 && decl[nn] == '/') return 1;
        if (nn > dn && strncmp(name, decl, dn) == 0 && name[dn] == '/') return 1;
    }
    return 0;
}

static void project_namespace_add(const char *name) {
    if (!name[0] || project_namespace_declared(name)) return;
    if (project_namespace_count == project_namespace_cap) {
        int ncap = project_namespace_cap ? project_namespace_cap * 2 : 16;
        char **grown = (char **)realloc(project_namespaces,
                                        (size_t)ncap * sizeof(*grown));
        if (!grown) return;
        project_namespaces = grown;
        project_namespace_cap = ncap;
    }
    char *dup = (char *)malloc(strlen(name) + 1);
    if (!dup) return;
    memcpy(dup, name, strlen(name) + 1);
    project_namespaces[project_namespace_count++] = dup;
}

static int subdir_globbed(const char *subdir) {
    for (int i = 0; i < globbed_dir_count; i++)
        if (strcmp(globbed_dirs[i], subdir) == 0) return 1;
    if (globbed_dir_count == globbed_dir_cap) {
        int ncap = globbed_dir_cap ? globbed_dir_cap * 2 : 32;
        char **grown = (char **)realloc(globbed_dirs, (size_t)ncap * sizeof(*grown));
        if (!grown) return 0;
        globbed_dirs = grown;
        globbed_dir_cap = ncap;
    }
    char *dup = (char *)malloc(strlen(subdir) + 1);
    if (!dup) return 0;
    memcpy(dup, subdir, strlen(subdir) + 1);
    globbed_dirs[globbed_dir_count++] = dup;
    return 0;
}

/* Probe a */
static int probe_file_namespace(const char *path, char *out_ns, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char buf[1024];
    size_t nr = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (nr == 0) return 0;
    buf[nr] = '\0';

    const char *p = buf;
    while (*p) {
        if (p[0] == '/' && p[1] == '/') {
            p += 2;
            while (*p && *p != '\n' && *p != '\r') p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
            continue;
        }
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p += 2;
                else p++;
            }
            if (*p) p++;
            continue;
        }
        if (strncmp(p, "namespace", 9) == 0 &&
            (p == buf || (unsigned char)p[-1] <= 32 || p[-1] == ';') &&
            ((unsigned char)p[9] <= 32)) {
            p += 9;
            while (*p && (unsigned char)*p <= 32) p++;
            size_t idx = 0;
            while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '.')) {
                if (idx + 1 < cap) out_ns[idx++] = *p;
                p++;
            }
            out_ns[idx] = '\0';
            return idx > 0;
        }
        p++;
    }
    return 0;
}

static void subdir_to_namespace(const char *subdir, char *out_ns, size_t cap) {
    size_t i = 0;
    for (; subdir[i] && i + 1 < cap; i++) {
        out_ns[i] = (subdir[i] == '/' || subdir[i] == '\\') ? '.' : subdir[i];
    }
    out_ns[i] = '\0';
}

static int glob_stdlib_dir(const char *stdlib_root, const char *subdir,
                           const char ***files, int *count, int *cap) {
    int before = *count;
    char target_ns[256];
    subdir_to_namespace(subdir, target_ns, sizeof(target_ns));

#ifdef _WIN32
    char glob_path[1024];
    snprintf(glob_path, sizeof(glob_path), "%s\\%s\\*.zan", stdlib_root, subdir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(glob_path, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            char mod_path[1024];
            snprintf(mod_path, sizeof(mod_path), "%s\\%s\\%s",
                     stdlib_root, subdir, fd.cFileName);
            add_stdlib_input(files, count, cap, mod_path);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    char sub_pattern[1024];
    snprintf(sub_pattern, sizeof(sub_pattern), "%s\\%s\\*", stdlib_root, subdir);
    h = FindFirstFileA(sub_pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == '.') continue;
            char candidate_sub[1024];
            snprintf(candidate_sub, sizeof(candidate_sub), "%s\\%s\\%s\\*.zan",
                     stdlib_root, subdir, fd.cFileName);
            WIN32_FIND_DATAA sub_fd;
            HANDLE sub_h = FindFirstFileA(candidate_sub, &sub_fd);
            if (sub_h != INVALID_HANDLE_VALUE) {
                char first_path[1024];
                snprintf(first_path, sizeof(first_path), "%s\\%s\\%s\\%s",
                         stdlib_root, subdir, fd.cFileName, sub_fd.cFileName);
                char declared_ns[256] = {0};
                if (probe_file_namespace(first_path, declared_ns, sizeof(declared_ns)) &&
                    strcmp(declared_ns, target_ns) == 0) {
                    do {
                        if (sub_fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                        char mod_path[1024];
                        snprintf(mod_path, sizeof(mod_path), "%s\\%s\\%s\\%s",
                                 stdlib_root, subdir, fd.cFileName, sub_fd.cFileName);
                        add_stdlib_input(files, count, cap, mod_path);
                    } while (FindNextFileA(sub_h, &sub_fd));
                }
                FindClose(sub_h);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    char dir_path[1024];
    if (!resolve_stdlib_dir(stdlib_root, subdir, dir_path, sizeof(dir_path)))
        return 0;
    DIR *d = opendir(dir_path);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            char entry_path[1024];
            snprintf(entry_path, sizeof(entry_path), "%s/%s", dir_path, ent->d_name);
            struct stat st;
            if (stat(entry_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                DIR *subd = opendir(entry_path);
                if (subd) {
                    struct dirent *sub_ent;
                    int sub_matched = 0;
                    while ((sub_ent = readdir(subd)) != NULL) {
                        size_t slen = strlen(sub_ent->d_name);
                        if (slen < 5 || strcmp(sub_ent->d_name + slen - 4, ".zan") != 0) continue;
                        char sub_file_path[1024];
                        snprintf(sub_file_path, sizeof(sub_file_path), "%s/%s", entry_path, sub_ent->d_name);
                        if (!sub_matched) {
                            char declared_ns[256] = {0};
                            if (probe_file_namespace(sub_file_path, declared_ns, sizeof(declared_ns)) &&
                                strcmp(declared_ns, target_ns) == 0) {
                                sub_matched = 1;
                            } else {
                                break;
                            }
                        }
                        if (sub_matched) {
                            add_stdlib_input(files, count, cap, sub_file_path);
                        }
                    }
                    closedir(subd);
                }
                continue;
            }
            size_t nlen = strlen(ent->d_name);
            if (nlen < 5 || strcmp(ent->d_name + nlen - 4, ".zan") != 0) continue;
            char mod_path[1024];
            snprintf(mod_path, sizeof(mod_path), "%s/%s", dir_path, ent->d_name);
            add_stdlib_input(files, count, cap, mod_path);
        }
        closedir(d);
    }
#endif
    return *count > before;
}

static zan_pkg_source_index_t *package_source_index;

static void package_sources_destroy(void) {
    zan_pkg_source_index_destroy(package_source_index);
    package_source_index = NULL;
}

static int package_visit_namespace(const char *subdir,
                                    zan_pkg_source_visitor_t visitor,
                                    void *context, int hierarchical) {
    if (!package_project_root[0]) return 0;
    if (!package_source_index) {
        package_source_index = zan_pkg_source_index_create(
            package_project_root, probe_file_namespace);
        atexit(package_sources_destroy);
    }
    return zan_pkg_source_index_visit(package_source_index, subdir, visitor,
                                       context, hierarchical);
}

typedef struct {
    const char ***files;
    int *count;
    int *cap;
} pkg_auto_include_t;

static void package_add_input(const char *path, void *context) {
    pkg_auto_include_t *a = (pkg_auto_include_t *)context;
    add_stdlib_input(a->files, a->count, a->cap, path);
}

static int auto_include_namespace(const char *stdlib_root, const char *subdir,
                                  const char ***files, int *count, int *cap) {
    int found = glob_stdlib_dir(stdlib_root, subdir, files, count, cap);
    int hierarchical = stdlib_has_dir(stdlib_root, subdir) == 0;
    pkg_auto_include_t args = { files, count, cap };
    int package_count = package_visit_namespace(subdir, package_add_input,
                                                  &args, hierarchical);
    found = found || package_count > 0;
    if (!found && package_project_root[0] != '\0' &&
        !stdlib_has_dir(stdlib_root, subdir) &&
        !project_namespace_declared(subdir)) {
        fprintf(stderr, "ZANPKG_MISSING namespace=%s\n", subdir);
        missing_namespace_count++;
    }
    return found;
}

static void scan_namespace_tokens(const char *source, size_t len) {
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_set_capture(diag, true);
    zan_lexer_t lex;
    zan_lexer_init(&lex, source, len, 0, arena, diag);
    for (;;) {
        zan_token_t tok = zan_lexer_next(&lex);
        if (tok.kind == TK_EOF) break;
        if (tok.kind != TK_NAMESPACE) continue;
        char name[512]; size_t used = 0;
        tok = zan_lexer_next(&lex);
        if (tok.kind != TK_IDENT) continue;
        for (;;) {
            if (used && used + 1 < sizeof(name)) name[used++] = '/';
            if (used + tok.str_val.len >= sizeof(name)) break;
            memcpy(name + used, tok.str_val.str, tok.str_val.len);
            used += tok.str_val.len;
            tok = zan_lexer_next(&lex);
            if (tok.kind != TK_DOT) break;
            tok = zan_lexer_next(&lex);
            if (tok.kind != TK_IDENT) break;
        }
        name[used] = 0;
        if (tok.kind == TK_SEMICOLON && used > 0) project_namespace_add(name);
    }
    zan_diag_free_buffers(diag);
    zan_arena_free(arena);
}

static int skip_project_dir(const char *name) {
    static const char *skip[] = {
        ".zan-packages", ".git", ".svn", ".hg", "build", "publish",
        "bin", "obj", "node_modules", "_scratch", "dist", "out", "vendor"
    };
    for (size_t i = 0; i < sizeof(skip) / sizeof(skip[0]); i++)
        if (strcmp(name, skip[i]) == 0) return 1;
    return 0;
}

/* ---- 目录输入：递归展开 .zan 源文件 ------------------------------------
 *
 * 动机：Windows 命令行长度上限约 32K 字符。一个 5618 文件的包把路径摊在
 * 命令行上，CreateProcess 直接起不来（"文件名或扩展名太长"）——而调用方
 * 往往只看到输出里没有 error，就当成编译过了，于是一个**从没被编译过**的
 * 包看起来是健康的（scripts/pkg_sweep.ps1 曾长期这样误报）。
 *
 * 把"编译一个目录"做成编译器的一等公民之后：
 *  - 命令行长度与项目规模解耦（大包一个路径就够，不再需要 @响应文件）；
 *  - 枚举规则只此一份（跳过哪些目录、要不要递归、什么顺序），脚本 / IDE /
 *    CI 不会各自实现一遍而各自漂移。
 *
 * 展开结果按路径字典序排序：目录遍历的原始顺序在 Windows 上不确定，而
 * 输入顺序会影响合并单元里 decl 的先后——可复现构建与差分门都要求稳定。
 */
int zan_path_cmp(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

bool zan_path_is_directory(const char *path) {
#ifdef _WIN32
    return zan_utf8_is_directory(path) != 0;
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

void zan_collect_dir_sources(const char *dir, const char ***files,
                                    int *count, int *cap) {
#ifdef _WIN32
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            if (fd.cFileName[0] == '.' || skip_project_dir(fd.cFileName)) continue;
            zan_collect_dir_sources(path, files, count, cap);
        } else {
            size_t nlen = strlen(fd.cFileName);
            if (nlen < 5 || strcmp(fd.cFileName + nlen - 4, ".zan") != 0) continue;
            add_stdlib_input(files, count, cap, path);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (lstat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (S_ISLNK(st.st_mode)) continue;
            if (e->d_name[0] == '.' || skip_project_dir(e->d_name)) continue;
            zan_collect_dir_sources(path, files, count, cap);
        } else if (S_ISREG(st.st_mode)) {
            size_t nlen = strlen(e->d_name);
            if (nlen < 5 || strcmp(e->d_name + nlen - 4, ".zan") != 0) continue;
            add_stdlib_input(files, count, cap, path);
        }
    }
    closedir(d);
#endif
}

static void scan_project_namespaces(const char *root) {
#ifdef _WIN32
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", root);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s\\%s", root, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            if (!skip_project_dir(fd.cFileName)) scan_project_namespaces(path);
        } else {
            size_t nlen = strlen(fd.cFileName);
            if (nlen < 5 || strcmp(fd.cFileName + nlen - 4, ".zan") != 0) continue;
            size_t n = 0;
            char *src = read_file(path, &n);
            if (src) { scan_namespace_tokens(src, n); free(src); }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(root);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", root, e->d_name);
        struct stat st;
        if (lstat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (!S_ISLNK(st.st_mode) && !skip_project_dir(e->d_name))
                scan_project_namespaces(path);
        } else if (S_ISREG(st.st_mode)) {
            size_t nlen = strlen(e->d_name);
            if (nlen < 5 || strcmp(e->d_name + nlen - 4, ".zan") != 0) continue;
            size_t n = 0;
            char *src = read_file(path, &n);
            if (src) { scan_namespace_tokens(src, n); free(src); }
        }
    }
    closedir(d);
#endif
}

static bool project_root_has_manifest(void) {
    char manifest[1200];
    snprintf(manifest, sizeof(manifest), "%s/zan.proj", package_project_root);
    FILE *f = fopen(manifest, "rb");
    if (f) { fclose(f); return true; }
    return false;
}

static void resolve_package_project_root(const char *input) {
    snprintf(package_project_root, sizeof(package_project_root), "%s", input);
    char *sep = strrchr(package_project_root, '/');
    char *back = strrchr(package_project_root, '\\');
    if (!sep || (back && back > sep)) sep = back;
    if (sep) *sep = 0;
    else snprintf(package_project_root, sizeof(package_project_root), ".");
    for (;;) {
        char manifest[1200];
        snprintf(manifest, sizeof(manifest), "%s/zan.proj", package_project_root);
        FILE *f = fopen(manifest, "rb");
        if (f) { fclose(f); return; }

        char pkg_dir[1200];
        snprintf(pkg_dir, sizeof(pkg_dir), "%s/packages", package_project_root);
#ifdef _WIN32
        DWORD a_attr = zan_utf8_get_file_attributes(pkg_dir);
        if (a_attr != INVALID_FILE_ATTRIBUTES && (a_attr & FILE_ATTRIBUTE_DIRECTORY)) return;
#else
        struct stat st_pkg;
        if (stat(pkg_dir, &st_pkg) == 0 && S_ISDIR(st_pkg.st_mode)) return;
#endif

        char *a = strrchr(package_project_root, '/');
        char *b = strrchr(package_project_root, '\\');
        char *last = (!a || (b && b > a)) ? b : a;
        if (!last) break;
#ifdef _WIN32
        if (last == package_project_root + 2 && package_project_root[1] == ':') break;
#endif
        *last = 0;
    }
    snprintf(package_project_root, sizeof(package_project_root), ".");
}

static char proj_android_package[256];
static char proj_android_label[256];
static bool proj_android_keys_ok = true;
static char proj_android_perms[128][128];
static int proj_android_perm_count = 0;
static char proj_skin_names[128][64];
static int proj_skin_name_count = 0;
static bool proj_skins_enabled = false;
/* zan.proj 的 type 键（"gui" 等）：DocApp 入口 shim 的工程级门控——
 * type=gui 且用户没写 Main 时合成 __ZanDocMain（B-ID147）。 */
static char proj_app_type[16] = "";
static char proj_entry_file[1400] = "";   /* zan.proj entry：窗口入口合成优先于 CLI 基名 */
static bool load_proj_android_keys_done = false;
static const char **skin_filter = NULL;
static int skin_filter_count = 0;

static char *proj_trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'
                     || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
    return s;
}

static void load_proj_android_keys(void) {
    char path[1400];
    snprintf(path, sizeof(path), "%s/zan.proj", package_project_root);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = proj_trim(line);
        char *val = proj_trim(eq + 1);
        if (strcmp(key, "skins") == 0) {
            proj_skins_enabled = strcmp(val, "1") == 0;
        } else if (strcmp(key, "type") == 0) {
            snprintf(proj_app_type, sizeof(proj_app_type), "%s", val);
        } else if (strcmp(key, "entry") == 0) {
            snprintf(proj_entry_file, sizeof(proj_entry_file), "%s", val);
        } else if (strcmp(key, "androidPackage") == 0) {
            if (strlen(val) >= sizeof(proj_android_package)) {
                fprintf(stderr, "error: zan.proj androidPackage exceeds "
                        "%zu bytes\n", sizeof(proj_android_package) - 1);
                proj_android_keys_ok = false;
            } else {
                snprintf(proj_android_package,
                         sizeof(proj_android_package), "%s", val);
            }
        } else if (strcmp(key, "androidLabel") == 0) {
            if (strlen(val) >= sizeof(proj_android_label)) {
                fprintf(stderr, "error: zan.proj androidLabel exceeds "
                        "%zu bytes\n", sizeof(proj_android_label) - 1);
                proj_android_keys_ok = false;
            } else {
                snprintf(proj_android_label,
                         sizeof(proj_android_label), "%s", val);
            }
        } else if (strcmp(key, "androidPermissions") == 0) {
            char *save = NULL;
            char *tok = strtok_r(val, ",", &save);
            while (tok) {
                char *p = proj_trim(tok);
                if (*p) {
                    if (proj_android_perm_count >= 128) {
                        fprintf(stderr, "error: zan.proj androidPermissions: "
                                "more than 128 entries\n");
                        proj_android_keys_ok = false;
                        break;
                    }
                    char *dst = proj_android_perms[proj_android_perm_count];
                    int wn = strncmp(p, "android.permission.", 19) == 0
                           ? snprintf(dst, 128, "%s", p)
                           : snprintf(dst, 128, "android.permission.%s", p);
                    if (wn >= 128) {
                        fprintf(stderr, "error: zan.proj androidPermissions: "
                                "'%s' exceeds 127 bytes\n", p);
                        proj_android_keys_ok = false;
                    } else {
                        proj_android_perm_count++;
                    }
                }
                tok = strtok_r(NULL, ",", &save);
            }
        } else if (strcmp(key, "skinlist") == 0) {
            char *save = NULL;
            char *tok = strtok_r(val, ",", &save);
            while (tok && proj_skin_name_count < 128) {
                char *p = proj_trim(tok);
                if (*p && strcmp(p, "-") != 0) {
                    snprintf(proj_skin_names[proj_skin_name_count],
                             sizeof(proj_skin_names[0]), "%s", p);
                    proj_skin_name_count++;
                }
                tok = strtok_r(NULL, ",", &save);
            }
        }
    }
    fclose(f);
}

static void scan_using_tokens(const char *source, size_t len,
                              const char *stdlib_root,
                              const char ***files, int *count, int *cap) {
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_set_capture(diag, true);
    zan_lexer_t lex;
    zan_lexer_init(&lex, source, len, 0, arena, diag);
    for (;;) {
        zan_token_t tok = zan_lexer_next(&lex);
        if (tok.kind == TK_EOF) break;
        if (tok.kind != TK_USING) continue;
        char subdir[512]; size_t used = 0;
        tok = zan_lexer_next(&lex);
        if (tok.kind != TK_IDENT) continue;
        for (;;) {
            if (used && used + 1 < sizeof(subdir)) subdir[used++] = '/';
            if (used + tok.str_val.len >= sizeof(subdir)) break;
            memcpy(subdir + used, tok.str_val.str, tok.str_val.len);
            used += tok.str_val.len;
            tok = zan_lexer_next(&lex);
            if (tok.kind != TK_DOT) break;
            tok = zan_lexer_next(&lex);
            if (tok.kind != TK_IDENT) break;
        }
        subdir[used] = 0;
        if (tok.kind == TK_SEMICOLON && used > 0 && !subdir_globbed(subdir))
            auto_include_namespace(stdlib_root, subdir, files, count, cap);
    }
    zan_diag_free_buffers(diag);
    zan_arena_free(arena);
}

