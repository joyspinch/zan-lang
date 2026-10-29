/* main_prune.c - Program & Package Include (PI) 依赖可达性分析与模块裁剪 */

static zan_arena_t *pi_arena = NULL;
static int pi_filter_active = 0;
static int pi_seed_stdlib_input = 0;
static int pi_seeding_stdlib = 0;
static int pi_repair_scanning = 0;

typedef struct pi_name {
    const char *str;
    unsigned len;
    int flagged;
    int flagged_stdlib;
    int user_decl;
    int ns_root;
    struct pi_name *next;
} pi_name_t;

#define PI_BUCKETS 8192u
static pi_name_t *pi_table[PI_BUCKETS];

typedef struct pi_file {
    char *path;
    pi_name_t **top; int top_count, top_cap;
    pi_name_t **idents; int ident_count, ident_cap;
    char **usings; int using_count, using_cap;
    int has_ext;
    int has_controller;         /* 控制器/路由/表等生成器实体 */
    int pkg_src;
    int gate_live;
    int included;
    int parsed;
    int seeded;
    struct pi_file *dnext;
} pi_file_t;

typedef struct pi_dir {
    char *subdir;
    pi_file_t *files;
    int file_count, file_cap;
    int reached;                /* globbed + metadata-scanned */
    struct pi_dir *next;
} pi_dir_t;

static pi_dir_t *pi_dirs_head = NULL;
static pi_dir_t *pi_dirs_tail = NULL;

static zan_target_t pi_target;
static const char *const *pi_pp_defines = NULL;
static int pi_pp_define_count = 0;
static bool pi_publish_mode = false;
static void zan_apply_lex_defines(zan_lexer_t *lex, zan_target_t target,
                                  const char *const *pp_defines,
                                  int pp_define_count,
                                  bool is_publish);

static unsigned pi_hash(const char *s, size_t len) {
    unsigned h = 2166136261u;
    for (size_t i = 0; i < len; i++) h = (h ^ (unsigned char)s[i]) * 16777619u;
    return h;
}

static pi_name_t *pi_intern(const char *s, size_t len) {
    unsigned b = pi_hash(s, len) & (PI_BUCKETS - 1);
    for (pi_name_t *p = pi_table[b]; p; p = p->next)
        if (p->len == len && memcmp(p->str, s, len) == 0) return p;
    pi_name_t *p = (pi_name_t *)zan_arena_alloc(pi_arena, sizeof(*p));
    if (!p) return NULL;
    char *dup = (char *)zan_arena_alloc(pi_arena, len + 1);
    if (!dup) return NULL;
    memcpy(dup, s, len);
    dup[len] = 0;
    p->str = dup;
    p->len = (unsigned)len;
    p->flagged = 0;
    p->flagged_stdlib = 0;
    p->next = pi_table[b];
    pi_table[b] = p;
    return p;
}

static int pi_reserve(void *arr_p, int count, int *cap, size_t elem_sz) {
    if (count < *cap) return 1;
    int ncap = *cap ? *cap * 2 : 8;
    void *grown = zan_arena_alloc(pi_arena, (size_t)ncap * elem_sz);
    if (!grown) return 0;
    memcpy(grown, *(void **)arr_p, (size_t)count * elem_sz);
    *(void **)arr_p = grown;
    *cap = ncap;
    return 1;
}

static void pi_reach(const char *subdir) {
    for (pi_dir_t *d = pi_dirs_head; d; d = d->next)
        if (strcmp(d->subdir, subdir) == 0) return;
    pi_dir_t *d = (pi_dir_t *)zan_arena_alloc(pi_arena, sizeof(*d));
    if (!d) return;
    size_t len = strlen(subdir);
    char *dup = (char *)zan_arena_alloc(pi_arena, len + 1);
    if (!dup) return;
    memcpy(dup, subdir, len + 1);
    d->subdir = dup;
    d->files = NULL;
    d->file_count = 0;
    d->reached = 0;
    d->next = NULL;
    if (pi_dirs_tail) pi_dirs_tail->next = d;
    else pi_dirs_head = d;
    pi_dirs_tail = d;
}

static const char *pi_stdlib_root_buf;

static int pi_reach_input_dir(const char *file) {
    char comps[64][256];
    int n = 0;
    size_t len = strlen(file);
    size_t i = 0;
    while (i < len && n < 64) {
        size_t start = i;
        while (i < len && file[i] != '/' && file[i] != 92) i++;
        size_t cl = i - start;
        if (cl > 0 && cl < 256) {
            memcpy(comps[n], file + start, cl);
            comps[n][cl] = 0;
            n++;
        }
        i++;
    }
    if (n < 2 || !pi_stdlib_root_buf) return 0;
    const char *root = pi_stdlib_root_buf;
    size_t rl = strlen(root);
    size_t rs = rl;
    while (rs > 0 && root[rs - 1] != '/' && root[rs - 1] != 92) rs--;
    const char *leaf = root + rs;
    size_t ll = strlen(leaf);
    int base = -1;
    for (int k = n - 2; k >= 0; k--) {
        if (strlen(comps[k]) == ll) {
            int eq = 1;
            for (size_t c = 0; c < ll; c++) {
                char a = comps[k][c], b = leaf[c];
                if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
                if (a != b) { eq = 0; break; }
            }
            if (eq) { base = k; break; }
        }
    }
    if (base < 0 || base == n - 1) return 0;
    char sub[1024];
    size_t used = 0;
    for (int k = base + 1; k < n - 1; k++) {
        size_t cl = strlen(comps[k]);
        if (used && used + 1 < sizeof(sub)) sub[used++] = '/';
        if (used + cl + 1 > sizeof(sub)) return 1;
        memcpy(sub + used, comps[k], cl);
        used += cl;
    }
    sub[used] = 0;
    pi_reach(sub);
    char declared_ns[256] = {0};
    if (probe_file_namespace(file, declared_ns, sizeof(declared_ns))) {
        char canon_sub[256];
        size_t c = 0;
        for (; declared_ns[c] && c + 1 < sizeof(canon_sub); c++) {
            canon_sub[c] = declared_ns[c] == '.' ? '/' : declared_ns[c];
        }
        canon_sub[c] = '\0';
        if (canon_sub[0] && strcmp(canon_sub, sub) != 0) {
            pi_reach(canon_sub);
        }
    }
    return 1;
}

static void pi_add_file(pi_dir_t *d, const char *path) {
    if (!pi_reserve((void *)&d->files, d->file_count, &d->file_cap,
                    sizeof(pi_file_t)))
        return;
    pi_file_t *f = &d->files[d->file_count++];
    size_t len = strlen(path);
    f->path = (char *)zan_arena_alloc(pi_arena, len + 1);
    if (f->path) memcpy(f->path, path, len + 1);
    f->top = NULL; f->top_count = 0; f->top_cap = 0;
    f->idents = NULL; f->ident_count = 0; f->ident_cap = 0;
    f->usings = NULL; f->using_count = 0; f->using_cap = 0;
    f->has_ext = 0;
    f->pkg_src = 0;
    f->included = 0;
    f->parsed = 0;
    f->seeded = 0;
    f->dnext = NULL;
}

static void pi_glob_into(pi_dir_t *d, const char *root, const char *subdir) {
    char target_ns[256];
    subdir_to_namespace(subdir, target_ns, sizeof(target_ns));

#ifdef _WIN32
    char glob_path[1024];
    if (subdir[0])
        snprintf(glob_path, sizeof(glob_path), "%s\\%s\\*.zan", root, subdir);
    else
        snprintf(glob_path, sizeof(glob_path), "%s\\*.zan", root);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(glob_path, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            char mod_path[1024];
            if (subdir[0])
                snprintf(mod_path, sizeof(mod_path), "%s\\%s\\%s",
                         root, subdir, fd.cFileName);
            else
                snprintf(mod_path, sizeof(mod_path), "%s\\%s", root, fd.cFileName);
            pi_add_file(d, mod_path);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    if (subdir[0]) {
        char sub_pattern[1024];
        snprintf(sub_pattern, sizeof(sub_pattern), "%s\\%s\\*", root, subdir);
        h = FindFirstFileA(sub_pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (fd.cFileName[0] == '.') continue;
                char candidate_sub[1024];
                snprintf(candidate_sub, sizeof(candidate_sub), "%s\\%s\\%s\\*.zan",
                         root, subdir, fd.cFileName);
                WIN32_FIND_DATAA sub_fd;
                HANDLE sub_h = FindFirstFileA(candidate_sub, &sub_fd);
                if (sub_h != INVALID_HANDLE_VALUE) {
                    char first_path[1024];
                    snprintf(first_path, sizeof(first_path), "%s\\%s\\%s\\%s",
                             root, subdir, fd.cFileName, sub_fd.cFileName);
                    char declared_ns[256] = {0};
                    if (probe_file_namespace(first_path, declared_ns, sizeof(declared_ns)) &&
                        strcmp(declared_ns, target_ns) == 0) {
                        do {
                            if (sub_fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                            char mod_path[1024];
                            snprintf(mod_path, sizeof(mod_path), "%s\\%s\\%s\\%s",
                                     root, subdir, fd.cFileName, sub_fd.cFileName);
                            pi_add_file(d, mod_path);
                        } while (FindNextFileA(sub_h, &sub_fd));
                    }
                    FindClose(sub_h);
                }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
#else
    char dir_path[1024];
    if (!resolve_stdlib_dir(root, subdir, dir_path, sizeof(dir_path)))
        return;
    DIR *dr = opendir(dir_path);
    if (dr) {
        struct dirent *ent;
        while ((ent = readdir(dr)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            char entry_path[1024];
            snprintf(entry_path, sizeof(entry_path), "%s/%s", dir_path, ent->d_name);
            struct stat st;
            if (stat(entry_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                DIR *subdr = opendir(entry_path);
                if (subdr) {
                    struct dirent *sub_ent;
                    int sub_matched = 0;
                    while ((sub_ent = readdir(subdr)) != NULL) {
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
                            pi_add_file(d, sub_file_path);
                        }
                    }
                    closedir(subdr);
                }
                continue;
            }
            size_t nlen = strlen(ent->d_name);
            if (nlen < 5 || strcmp(ent->d_name + nlen - 4, ".zan") != 0) continue;
            char mod_path[1024];
            snprintf(mod_path, sizeof(mod_path), "%s/%s", dir_path, ent->d_name);
            pi_add_file(d, mod_path);
        }
        closedir(dr);
    }
#endif
}

/* Record a dotted `using A */
static void pi_note_using(pi_file_t *f, const char *subdir) {
    if (!subdir[0]) return;
    if (pi_repair_scanning) return;
    if (!pi_reserve((void *)&f->usings, f->using_count, &f->using_cap,
                    sizeof(char *)))
        return;
    size_t len = strlen(subdir);
    char *dup = (char *)zan_arena_alloc(pi_arena, len + 1);
    if (!dup) return;
    memcpy(dup, subdir, len + 1);
    f->usings[f->using_count++] = dup;
    pi_reach(dup);
}

static void pi_flag_ident(pi_file_t *f, const char *s, size_t len) {
    pi_name_t *name = pi_intern(s, len);
    if (!name) return;
    if (!f) {
        name->flagged = 1;
        if (pi_seeding_stdlib) name->flagged_stdlib = 1;
        return;
    }
    if (!pi_reserve((void *)&f->idents, f->ident_count, &f->ident_cap,
                    sizeof(pi_name_t *)))
        return;
    f->idents[f->ident_count++] = name;
}

static uint64_t pi_meta_hash_bytes(uint64_t h, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= UINT64_C(1099511628211);
    }
    return h;
}

static uint64_t pi_meta_hash_u32(uint64_t h, uint32_t value) {
    return pi_meta_hash_bytes(h, &value, sizeof(value));
}

static uint64_t pi_meta_hash_text(uint64_t h, const char *text) {
    size_t len = text ? strlen(text) : 0;
    h = pi_meta_hash_u32(h, (uint32_t)len);
    return pi_meta_hash_bytes(h, text, len);
}

static uint64_t pi_meta_compiler_hash(void) {
    /* 默认不落盘元数据缓存（零磁盘垃圾模式）；仅当显式指定 ZAN_META_CACHE_DIR 时启用 */
    const char *override = getenv("ZAN_META_CACHE_DIR");
    if (!override || !*override) return 0;

    static uint64_t identity;
    static int checked;
    if (checked) return identity;
    checked = 1;
    char exe[4096];
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
    if (!n || n >= sizeof(exe)) return 0;
#elif defined(__APPLE__)
    uint32_t sz = sizeof(exe);
    if (_NSGetExecutablePath(exe, &sz) != 0) return 0;
#else
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) return 0;
    exe[n] = 0;
#endif
    FILE *in = fopen(exe, "rb");
    if (!in) return 0;
    uint64_t h = UINT64_C(1469598103934665603);
    unsigned char buf[65536];
    size_t nread;
    while ((nread = fread(buf, 1, sizeof(buf), in)) != 0)
        h = pi_meta_hash_bytes(h, buf, nread);
    int ok = !ferror(in);
    fclose(in);
    if (ok) identity = h ? h : 1;
    return identity;
}

#if defined(__GNUC__) || defined(__clang__)
#define PI_UNUSED __attribute__((unused))
#else
#define PI_UNUSED
#endif

static PI_UNUSED uint64_t pi_meta_file_hash(const char *path, const char *src, size_t len) {
    uint64_t compiler = pi_meta_compiler_hash();
    if (!compiler) return 0;
    uint64_t h = UINT64_C(1469598103934665603);
    h = pi_meta_hash_text(h, "zan-pullin-meta-v2");
    h = pi_meta_hash_text(h, ZAN_VERSION);
    h = pi_meta_hash_bytes(h, &compiler, sizeof(compiler));
    h = pi_meta_hash_text(h, path);
    h = pi_meta_hash_u32(h, (uint32_t)pi_target.arch);
    h = pi_meta_hash_u32(h, (uint32_t)pi_target.os);
    h = pi_meta_hash_u32(h, (uint32_t)pi_target.abi);
    h = pi_meta_hash_text(h, pi_target.triple);
    h = pi_meta_hash_text(h, pi_target.cpu);
    h = pi_meta_hash_text(h, pi_target.features);
    h = pi_meta_hash_u32(h, (uint32_t)pi_target.pointer_size);
    h = pi_meta_hash_u32(h, pi_target.pic ? 1u : 0u);
    h = pi_meta_hash_u32(h, pi_publish_mode ? 1u : 0u);
    h = pi_meta_hash_u32(h, (uint32_t)pi_pp_define_count);
    for (int i = 0; i < pi_pp_define_count; i++)
        h = pi_meta_hash_text(h, pi_pp_defines[i]);
    h = pi_meta_hash_u32(h, (uint32_t)len);
    return pi_meta_hash_bytes(h, src, len);
}

static PI_UNUSED void pi_meta_cache_path(uint64_t key, char *out, size_t out_sz) {
    (void)key;
    if (out && out_sz > 0) out[0] = 0;
}

#define PI_META_MAGIC UINT32_C(0x5a504d32)
#define PI_META_VERSION UINT32_C(3)
#define PI_META_MAX_NAMES 100000u

static int pi_meta_cache_load(pi_file_t *f, const char *src, size_t len) {
    (void)f; (void)src; (void)len;
    return 0;
}

static void pi_meta_cache_write(const pi_file_t *f, const char *src, size_t len) {
    (void)f; (void)src; (void)len;
}

static void pi_scan_file(pi_file_t *f) {
    g_scale_stats.metadata_scans++;
    size_t len = 0;
    char *src = read_file(f->path, &len);
    if (!src) {
        f->included = 1;
        return;
    }
    if (pi_meta_cache_load(f, src, len)) {
        free(src);
        return;
    }
    g_scale_stats.metadata_cache_misses++;
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_set_capture(diag, true);
    zan_lexer_t lex;
    zan_lexer_init(&lex, src, len, 0, arena, diag);
    zan_apply_lex_defines(&lex, pi_target, pi_pp_defines, pi_pp_define_count,
                          pi_publish_mode);
    int depth = 0;
    zan_token_kind_t prev = TK_EOF;
    pi_name_t *last_id = NULL;
    for (;;) {
        zan_token_t tok = zan_lexer_next(&lex);
        if (tok.kind == TK_EOF) break;
        switch (tok.kind) {
        case TK_LBRACE:
            depth++;
            break;
        case TK_RBRACE:
            if (depth > 0) depth--;
            break;
        case TK_LBRACKET: {
            zan_token_t next = zan_lexer_peek(&lex);
            if (next.kind == TK_IDENT) {
                if ((next.str_val.len == 5 && memcmp(next.str_val.str, "Route", 5) == 0) ||
                    (next.str_val.len == 13 && memcmp(next.str_val.str, "ApiController", 13) == 0) ||
                    (next.str_val.len == 5 && memcmp(next.str_val.str, "Table", 5) == 0) ||
                    (next.str_val.len == 6 && memcmp(next.str_val.str, "Custom", 6) == 0) ||
                    (next.str_val.len == 7 && memcmp(next.str_val.str, "HttpGet", 7) == 0) ||
                    (next.str_val.len == 8 && memcmp(next.str_val.str, "HttpPost", 8) == 0) ||
                    (next.str_val.len == 7 && memcmp(next.str_val.str, "HttpPut", 7) == 0) ||
                    (next.str_val.len == 10 && memcmp(next.str_val.str, "HttpDelete", 10) == 0) ||
                    (next.str_val.len == 9 && memcmp(next.str_val.str, "HttpPatch", 9) == 0)) {
                    f->has_controller = 1;
                }
            }
            break;
        }
        case TK_USING: {
            char subdir[512];
            size_t used = 0;
            tok = zan_lexer_next(&lex);
            if (tok.kind != TK_IDENT) { prev = TK_IDENT; continue; }
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
            if (tok.kind == TK_SEMICOLON) pi_note_using(f, subdir);
            prev = tok.kind;
            continue;
        }
        case TK_CLASS:
        case TK_STRUCT:
        case TK_ENUM:
        case TK_INTERFACE:
            if (depth <= 1 && prev != TK_COLON && prev != TK_COMMA) {
                zan_token_t next = zan_lexer_peek(&lex);
                if (next.kind == TK_IDENT) {
                    tok = zan_lexer_next(&lex);
                    if (tok.str_val.len >= 10 &&
                        memcmp(tok.str_val.str + tok.str_val.len - 10, "Controller", 10) == 0) {
                        f->has_controller = 1;
                    }
                    if (tok.str_val.len >= 3 &&
                        memcmp(tok.str_val.str + tok.str_val.len - 3, "Dao", 3) == 0) {
                        f->has_controller = 1;
                    }
                    pi_name_t *name = pi_intern(tok.str_val.str,
                                                tok.str_val.len);
                    if (name &&
                        pi_reserve((void *)&f->top, f->top_count, &f->top_cap,
                                   sizeof(pi_name_t *)))
                        f->top[f->top_count++] = name;
                    prev = TK_IDENT;
                    continue;
                }
            }
            break;
        case TK_DELEGATE:
            if (depth <= 1 && prev != TK_COLON && prev != TK_COMMA) {
                pi_name_t *name = NULL;
                int angle = 0;
                for (;;) {
                    tok = zan_lexer_next(&lex);
                    if (tok.kind == TK_EOF || tok.kind == TK_SEMICOLON)
                        break;
                    if (tok.kind == TK_LPAREN && angle == 0) break;
                    if (tok.kind == TK_LESS) angle++;
                    else if (tok.kind == TK_GREATER) { if (angle > 0) angle--; }
                    else if (tok.kind == TK_IDENT && angle == 0)
                        name = pi_intern(tok.str_val.str, tok.str_val.len);
                }
                if (name && tok.kind == TK_LPAREN &&
                    pi_reserve((void *)&f->top, f->top_count, &f->top_cap,
                               sizeof(pi_name_t *)))
                    f->top[f->top_count++] = name;
                prev = tok.kind;
                continue;
            }
            break;
        case TK_THIS: {
            zan_token_t next = zan_lexer_peek(&lex);
            if (prev == TK_LPAREN && (next.kind == TK_IDENT ||
                                      next.kind == TK_STRING ||
                                      next.kind == TK_BOOL ||
                                      next.kind == TK_CHAR ||
                                      next.kind == TK_INT ||
                                      next.kind == TK_LONG ||
                                      next.kind == TK_SHORT ||
                                      next.kind == TK_BYTE ||
                                      next.kind == TK_DOUBLE ||
                                      next.kind == TK_FLOAT ||
                                      next.kind == TK_UINT ||
                                      next.kind == TK_ULONG ||
                                      next.kind == TK_NINT ||
                                      next.kind == TK_OBJECT))
                f->has_ext = 1;
            break;
        }
        case TK_IDENT:
            if (depth <= 1 && tok.str_val.len == 6 &&
                memcmp(tok.str_val.str, "record", 6) == 0 &&
                zan_lexer_peek(&lex).kind == TK_IDENT) {
                tok = zan_lexer_next(&lex);
                pi_name_t *name = pi_intern(tok.str_val.str, tok.str_val.len);
                if (name &&
                    pi_reserve((void *)&f->top, f->top_count, &f->top_cap,
                               sizeof(pi_name_t *)))
                    f->top[f->top_count++] = name;
                prev = TK_IDENT;
                continue;
            }
            pi_name_t *cur_id = pi_intern(tok.str_val.str,
                                          tok.str_val.len);
            if (prev == TK_COLON) {
                if ((tok.str_val.len >= 10 &&
                     memcmp(tok.str_val.str + tok.str_val.len - 10, "Controller", 10) == 0) ||
                    (tok.str_val.len == 14 && memcmp(tok.str_val.str, "BaseRepository", 14) == 0) ||
                    (tok.str_val.len == 7 && memcmp(tok.str_val.str, "BaseDao", 7) == 0)) {
                    f->has_controller = 1;
                }
            }
            if (prev != TK_DOT) last_id = cur_id;
            pi_flag_ident(f, tok.str_val.str, tok.str_val.len);
            if (prev == TK_DOT && last_id && last_id->len == 4 &&
                memcmp(last_id->str, "Task", 4) == 0 && tok.str_val.len == 7 &&
                (memcmp(tok.str_val.str, "WhenAll", 7) == 0 ||
                 memcmp(tok.str_val.str, "WhenAny", 7) == 0))
                pi_flag_ident(f, "TaskJoin", 8);
            break;
        default:
            break;
        }
        prev = tok.kind;
    }
    zan_diag_free_buffers(diag);
    zan_arena_free(arena);
    pi_meta_cache_write(f, src, len);
    free(src);
}

static int pi_typeish(zan_token_kind_t k) {
    return k == TK_IDENT || k == TK_STRING || k == TK_BOOL || k == TK_CHAR ||
           k == TK_INT || k == TK_LONG || k == TK_SHORT || k == TK_BYTE ||
           k == TK_DOUBLE || k == TK_FLOAT || k == TK_UINT ||
           k == TK_ULONG || k == TK_NINT || k == TK_OBJECT ||
           k == TK_VOID;
}

static void pi_seed_source(const char *source, size_t len) {
    g_scale_stats.seed_sources++;
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_lexer_t lex;

    for (int pass = 0; pass < 2; pass++) {
        g_scale_stats.seed_lex_passes++;
        zan_lexer_init(&lex, source, len, 0, arena, diag);
        zan_apply_lex_defines(&lex, pi_target, pi_pp_defines,
                              pi_pp_define_count, pi_publish_mode);
        int depth = 0;
        int ns_file_scoped = 0;
        zan_token_kind_t prev = TK_EOF;
        pi_name_t *chain = NULL;
        for (;;) {
            zan_token_t tok = zan_lexer_next(&lex);
            if (tok.kind == TK_EOF) break;
            switch (tok.kind) {
            case TK_LBRACE:
                depth++;
                break;
            case TK_RBRACE:
                if (depth > 0) depth--;
                break;
            case TK_USING: {
                char subdir[512];
                size_t used = 0;
                tok = zan_lexer_next(&lex);
                if (tok.kind != TK_IDENT) { prev = TK_IDENT; continue; }
                for (;;) {
                    if (used && used + 1 < sizeof(subdir))
                        subdir[used++] = '/';
                    if (used + tok.str_val.len >= sizeof(subdir)) break;
                    memcpy(subdir + used, tok.str_val.str, tok.str_val.len);
                    used += tok.str_val.len;
                    tok = zan_lexer_next(&lex);
                    if (tok.kind != TK_DOT) break;
                    tok = zan_lexer_next(&lex);
                    if (tok.kind != TK_IDENT) break;
                }
                subdir[used] = 0;
                if (tok.kind == TK_SEMICOLON && used > 0) {
                    pi_reach(subdir);
                    for (char *seg = subdir; *seg; ) {
                        char *dot = strchr(seg, '/');
                        if (dot) *dot = 0;
                        pi_name_t *s = pi_intern(seg, strlen(seg));
                        if (s) s->ns_root = 1;
                        if (!dot) break;
                        seg = dot + 1;
                    }
                }
                prev = tok.kind;
                chain = NULL;
                continue;
            }
            case TK_NAMESPACE: {
                char nsname[512];
                size_t used = 0;
                tok = zan_lexer_next(&lex);
                if (tok.kind != TK_IDENT) { prev = TK_IDENT; continue; }
                for (;;) {
                    if (used && used + 1 < sizeof(nsname))
                        nsname[used++] = '/';
                    if (used + tok.str_val.len >= sizeof(nsname)) break;
                    memcpy(nsname + used, tok.str_val.str, tok.str_val.len);
                    used += tok.str_val.len;
                    tok = zan_lexer_next(&lex);
                    if (tok.kind != TK_DOT) break;
                    tok = zan_lexer_next(&lex);
                    if (tok.kind != TK_IDENT) break;
                }
                nsname[used] = 0;
                if (tok.kind == TK_SEMICOLON)
                    ns_file_scoped = 1;
                if (tok.kind == TK_SEMICOLON || tok.kind == TK_LBRACE) {
                    for (char *seg = nsname; *seg; ) {
                        char *dot = strchr(seg, '/');
                        if (dot) *dot = 0;
                        pi_name_t *s = pi_intern(seg, strlen(seg));
                        if (s) s->ns_root = 1;
                        if (!dot) break;
                        seg = dot + 1;
                    }
                }
                prev = tok.kind;
                chain = NULL;
                continue;
            }
            case TK_CLASS:
            case TK_STRUCT:
            case TK_ENUM:
            case TK_INTERFACE:
                if (depth == 0 && !ns_file_scoped &&
                    prev != TK_COLON && prev != TK_COMMA &&
                    pass == 1) {
                    zan_token_t next = zan_lexer_peek(&lex);
                    if (next.kind == TK_IDENT) {
                        tok = zan_lexer_next(&lex);
                        pi_name_t *name = pi_intern(tok.str_val.str,
                                                    tok.str_val.len);
                        if (name) name->user_decl = 1;
                        prev = TK_IDENT;
                        chain = NULL;
                        continue;
                    }
                }
                break;
            case TK_DELEGATE:
                if (depth == 0 && !ns_file_scoped &&
                    prev != TK_COLON && prev != TK_COMMA &&
                    pass == 1) {
                    pi_name_t *name = NULL;
                    int angle = 0;
                    for (;;) {
                        tok = zan_lexer_next(&lex);
                        if (tok.kind == TK_EOF || tok.kind == TK_SEMICOLON)
                            break;
                        if (tok.kind == TK_LPAREN && angle == 0) break;
                        if (tok.kind == TK_LESS) angle++;
                        else if (tok.kind == TK_GREATER) {
                            if (angle > 0) angle--;
                        } else if (tok.kind == TK_IDENT && angle == 0)
                            name = pi_intern(tok.str_val.str, tok.str_val.len);
                    }
                    if (name && tok.kind == TK_LPAREN) name->user_decl = 1;
                    prev = tok.kind;
                    chain = NULL;
                    continue;
                }
                break;
            case TK_IDENT:
                if (depth == 0 && !ns_file_scoped &&
                    tok.str_val.len == 6 &&
                    memcmp(tok.str_val.str, "record", 6) == 0 &&
                    zan_lexer_peek(&lex).kind == TK_IDENT && pass == 1) {
                    tok = zan_lexer_next(&lex);
                    pi_name_t *name = pi_intern(tok.str_val.str,
                                                tok.str_val.len);
                    if (name) name->user_decl = 1;
                    prev = TK_IDENT;
                    chain = NULL;
                    continue;
                }
                {
                    pi_name_t *name = pi_intern(tok.str_val.str,
                                                tok.str_val.len);
                    if (pass == 1 && name) {
                        if (prev != TK_DOT) {
                            chain = name;
                            zan_token_t next = zan_lexer_peek(&lex);
                            if (name->len == 4 &&
                                memcmp(name->str, "Task", 4) == 0 &&
                                next.kind == TK_DOT) {
                                zan_token_t meth =
                                    zan_lexer_peek2(&lex);
                                if (meth.kind == TK_IDENT &&
                                    ((meth.str_val.len == 7 &&
                                      memcmp(meth.str_val.str,
                                             "WhenAll", 7) == 0) ||
                                     (meth.str_val.len == 7 &&
                                      memcmp(meth.str_val.str,
                                             "WhenAny", 7) == 0))) {
                                    pi_name_t *tj =
                                        pi_intern("TaskJoin", 8);
                                    if (tj) { tj->flagged = 1; if (pi_seeding_stdlib) tj->flagged_stdlib = 1; }
                                }
                            }
                            if (depth >= 1 && pi_typeish(prev) &&
                                (next.kind == TK_LPAREN ||
                                 next.kind == TK_LBRACE)) {
                            } else if (!name->user_decl) {
                                name->flagged = 1;
                                if (pi_seeding_stdlib)
                                    name->flagged_stdlib = 1;
                            }
                        } else if (chain && chain->ns_root) {
                            name->flagged = 1;
                            if (pi_seeding_stdlib)
                                name->flagged_stdlib = 1;
                        } else if (chain && chain->len == 4 &&
                                   memcmp(chain->str, "Task", 4) == 0 &&
                                   name->len == 7 &&
                                   (memcmp(name->str, "WhenAll", 7) == 0 ||
                                    memcmp(name->str, "WhenAny", 7) == 0)) {
                            pi_name_t *tj = pi_intern("TaskJoin", 8);
                            if (tj) { tj->flagged = 1; if (pi_seeding_stdlib) tj->flagged_stdlib = 1; }
                        }
                    }
                }
                break;
            case TK_DOT:
                break;
            default:
                chain = NULL;
                break;
            }
            if (tok.kind != TK_DOT && tok.kind != TK_IDENT) chain = NULL;
            prev = tok.kind;
        }
    }
    zan_arena_free(arena);
}


static void pi_seed_chain(const zan_ast_node_t *n, int in_chain);

static void pi_seed_ast(const zan_ast_node_t *n) { pi_seed_chain(n, 0); }

static void pi_seed_list(const zan_ast_list_t *list) {
    for (int i = 0; i < list->count; i++) pi_seed_ast(list->items[i]);
}

static void pi_flag_istr(zan_istr_t name) {
    if (!name.str || name.len <= 0) return;
    pi_name_t *p = pi_intern(name.str, (size_t)name.len);
    if (p && !p->user_decl) {
        p->flagged = 1;
        if (pi_seeding_stdlib) p->flagged_stdlib = 1;
    }
}

static void pi_flag_qualified(zan_istr_t name) {
    if (!name.str || name.len <= 0) return;
    pi_name_t *p = pi_intern(name.str, (size_t)name.len);
    if (p) {
        p->flagged = 1;
        if (pi_seeding_stdlib) p->flagged_stdlib = 1;
    }
}

static int pi_is_ns_root(zan_istr_t name) {
    if (!name.str || name.len <= 0) return 0;
    pi_name_t *p = pi_intern(name.str, (size_t)name.len);
    return p && p->ns_root;
}

static void pi_seed_chain(const zan_ast_node_t *n, int in_chain) {
    if (!n) return;
    if (n->meta && n->meta->attributes.count > 0)
        pi_seed_list(&n->meta->attributes);
    switch (n->kind) {
    case AST_TYPE_REF:
        if (n->type_ref.name.str) {
            const char *s = n->type_ref.name.str;
            unsigned start = 0;
            for (unsigned ci = 0; ci <= n->type_ref.name.len; ci++) {
                if (ci == n->type_ref.name.len || s[ci] == '.') {
                    if (ci > start)
                        pi_flag_ident(NULL, s + start, (size_t)(ci - start));
                    start = ci + 1;
                }
            }
        }
        pi_seed_list(&n->type_ref.type_args);
        pi_seed_ast(n->type_ref.array_element);
        return;
    case AST_QUALIFIED_NAME:
        /* a */
        for (int qi = 0; qi < n->qualified_name.parts.count; qi++)
            pi_flag_istr(n->qualified_name.parts.items[qi]->ident.name);
        return;

    case AST_METHOD_DECL:
    case AST_CONSTRUCTOR_DECL:
    case AST_DESTRUCTOR_DECL:
    case AST_DELEGATE_DECL:
        pi_seed_ast(n->method_decl.return_type);
        pi_seed_list(&n->method_decl.params);
        pi_seed_list(&n->method_decl.type_params);
        if (n->method_decl.ext) {
            pi_seed_list(&n->method_decl.ext->where_clauses);
            pi_seed_list(&n->method_decl.ext->base_args);
        }
        pi_seed_ast(n->method_decl.body);
        return;
    case AST_CLASS_DECL:
    case AST_STRUCT_DECL:
    case AST_INTERFACE_DECL:
        pi_seed_list(&n->type_decl.bases);
        pi_seed_list(&n->type_decl.members);
        if (n->type_decl.where_clauses)
            pi_seed_list(n->type_decl.where_clauses);
        return;
    case AST_ENUM_DECL:
        pi_seed_list(&n->type_decl.bases);
        pi_seed_list(&n->type_decl.members);
        return;
    case AST_FIELD_DECL:
    case AST_PROPERTY_DECL:
        pi_seed_ast(n->field_decl.type);
        pi_seed_ast(n->field_decl.initializer);
        pi_seed_ast(n->field_decl.getter_body);
        pi_seed_ast(n->field_decl.setter_body);
        if (n->field_decl.indexer_params)
            pi_seed_list(n->field_decl.indexer_params);
        return;
    case AST_ENUM_MEMBER:
        pi_seed_ast(n->enum_member.value);
        return;
    case AST_PARAM:
        pi_seed_ast(n->param.type);
        pi_seed_ast(n->param.default_val);
        return;
    case AST_VAR_DECL:
        pi_seed_ast(n->var_decl.type);
        pi_seed_ast(n->var_decl.initializer);
        return;
    case AST_TUPLE_DECON:
        pi_seed_list(&n->tuple_decon.types);
        pi_seed_ast(n->tuple_decon.initializer);
        return;
    case AST_WHERE_CLAUSE:
        pi_seed_list(&n->where_clause.constraints);
        return;
    case AST_ATTRIBUTE:
        if (n->attribute.name) {
            if (n->attribute.name->kind == AST_IDENTIFIER)
                pi_flag_istr(n->attribute.name->ident.name);
            else
                pi_seed_ast(n->attribute.name);
        }
        pi_seed_list(&n->attribute.args);
        return;
    case AST_USING_DECL:
        return;
    case AST_NAMESPACE_DECL:
        pi_seed_list(&n->namespace_decl.members);
        return;

    /* statements */
    case AST_COMPILATION_UNIT:
        pi_seed_ast(n->comp_unit.ns);
        pi_seed_list(&n->comp_unit.decls);
        return;
    case AST_BLOCK:
        pi_seed_list(&n->block.stmts);
        return;
    case AST_EXPR_STMT:
        pi_seed_ast(n->expr_stmt.expr);
        return;
    case AST_RETURN_STMT:
        pi_seed_ast(n->ret.value);
        return;
    case AST_IF_STMT:
        pi_seed_ast(n->if_stmt.cond);
        pi_seed_ast(n->if_stmt.then_body);
        pi_seed_ast(n->if_stmt.else_body);
        return;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        pi_seed_ast(n->while_stmt.cond);
        pi_seed_ast(n->while_stmt.body);
        return;
    case AST_FOR_STMT:
        pi_seed_ast(n->for_stmt.init);
        pi_seed_ast(n->for_stmt.cond);
        pi_seed_ast(n->for_stmt.step);
        pi_seed_ast(n->for_stmt.body);
        return;
    case AST_FOREACH_STMT:
        pi_seed_ast(n->foreach_stmt.var_type);
        pi_seed_ast(n->foreach_stmt.collection);
        pi_seed_ast(n->foreach_stmt.body);
        return;
    case AST_THROW_STMT:
        pi_seed_ast(n->throw_stmt.value);
        return;
    case AST_TRY_STMT:
        pi_seed_ast(n->try_stmt.try_body);
        pi_seed_list(&n->try_stmt.catches);
        pi_seed_ast(n->try_stmt.finally_body);
        return;
    case AST_CATCH_CLAUSE:
        pi_seed_ast(n->catch_clause.type);
        pi_seed_ast(n->catch_clause.body);
        return;
    case AST_SWITCH_STMT:
        pi_seed_ast(n->switch_stmt.expr);
        pi_seed_list(&n->switch_stmt.cases);
        return;
    case AST_SWITCH_CASE:
        pi_seed_ast(n->switch_case.pattern);
        pi_seed_ast(n->switch_case.type_pattern);
        pi_seed_ast(n->switch_case.when_cond);
        pi_seed_ast(n->switch_case.body);
        return;
    case AST_SWITCH_EXPR:
        pi_seed_ast(n->switch_expr.expr);
        pi_seed_list(&n->switch_expr.arms);
        return;
    case AST_SWITCH_ARM:
        pi_seed_ast(n->switch_arm.pattern);
        pi_seed_ast(n->switch_arm.type_pattern);
        pi_seed_ast(n->switch_arm.when_cond);
        pi_seed_ast(n->switch_arm.result);
        return;
    case AST_YIELD_STMT:
        pi_seed_ast(n->yield_stmt.value);
        return;
    case AST_LOCK_STMT:
        pi_seed_ast(n->lock_stmt.expr);
        pi_seed_ast(n->lock_stmt.body);
        return;
    case AST_CHECKED_STMT:
        pi_seed_ast(n->checked_stmt.body);
        return;
    case AST_QUERY_EXPR:
        pi_seed_ast(n->query.source);
        pi_seed_list(&n->query.clauses);
        pi_seed_ast(n->query.group_expr);
        pi_seed_ast(n->query.group_key);
        pi_seed_ast(n->query.select);
        return;
    case AST_QUERY_WHERE:
    case AST_QUERY_LET:
    case AST_QUERY_ORDERBY:
        pi_seed_ast(n->query_clause.expr);
        return;
    case AST_QUERY_JOIN:
        pi_seed_ast(n->query_clause.source);
        pi_seed_ast(n->query_clause.left_key);
        pi_seed_ast(n->query_clause.right_key);
        return;
    case AST_WITH_EXPR:
        pi_seed_ast(n->with_expr.expr);
        pi_seed_list(&n->with_expr.assigns);
        return;

    /* expressions */
    case AST_IDENTIFIER:
        pi_seed_ast(n->ident.inst_type_ref);
        return;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        pi_seed_ast(n->binary.left);
        pi_seed_ast(n->binary.right);
        return;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        pi_seed_ast(n->unary.operand);
        return;
    case AST_CALL:
        if (n->call.callee && n->call.callee->kind != AST_IDENTIFIER)
            pi_seed_chain(n->call.callee, 0);
        pi_seed_list(&n->call.args);
        pi_seed_list(&n->call.type_args);
        return;
    case AST_MEMBER_ACCESS: {
        zan_ast_node_t *obj = n->member.object;
        /* `Ns */
        if (in_chain) {
            const zan_ast_node_t *rt = n;
            while (rt->kind == AST_MEMBER_ACCESS)
                rt = rt->member.object;
            if (rt->kind == AST_IDENTIFIER &&
                pi_is_ns_root(rt->ident.name))
                pi_flag_qualified(n->member.name);
            else
                pi_flag_istr(n->member.name);
        }
        if (obj && (obj->kind == AST_IDENTIFIER || obj->kind == AST_THIS_EXPR)) {
            if (obj->kind == AST_IDENTIFIER) {
                pi_flag_istr(obj->ident.name);
                if (obj->ident.name.len == 4 &&
                    memcmp(obj->ident.name.str, "Task", 4) == 0) {
                    zan_istr_t m = n->member.name;
                    if (m.str && m.len == 7 &&
                        (memcmp(m.str, "WhenAll", 7) == 0 ||
                         memcmp(m.str, "WhenAny", 7) == 0)) {
                        pi_name_t *tj = pi_intern("TaskJoin", 8);
                        if (tj) { tj->flagged = 1; if (pi_seeding_stdlib) tj->flagged_stdlib = 1; }
                    }
                }
            }
            pi_flag_istr(n->member.name);
            /* Also flag <Name>Dao for ORM entity member access (e.g. this.SysDepartment -> SysDepartmentDao) */
            if (n->member.name.len > 0 && n->member.name.len < 100 &&
                n->member.name.str[0] >= 'A' && n->member.name.str[0] <= 'Z') {
                char dao_buf[128];
                memcpy(dao_buf, n->member.name.str, (size_t)n->member.name.len);
                memcpy(dao_buf + n->member.name.len, "Dao", 4);
                pi_name_t *dao = pi_intern(dao_buf, (size_t)n->member.name.len + 3);
                if (dao && !dao->user_decl) {
                    dao->flagged = 1;
                    if (pi_seeding_stdlib) dao->flagged_stdlib = 1;
                }
            }
        }
        pi_seed_chain(obj, 1);
        return;
    }
    case AST_INDEX:
        pi_seed_ast(n->index.object);
        pi_seed_ast(n->index.index);
        pi_seed_list(&n->index.extra);
        return;
    case AST_NEW_EXPR:
        pi_seed_ast(n->new_expr.type);
        pi_seed_ast(n->new_expr.call_init);
        pi_seed_list(&n->new_expr.args);
        pi_seed_list(&n->new_expr.arg_inits);
        return;
    case AST_CAST_EXPR:
    case AST_TYPEOF_EXPR:
    case AST_SIZEOF_EXPR:
        pi_seed_ast(n->cast.type);
        pi_seed_ast(n->cast.expr);
        return;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        pi_seed_ast(n->type_test.expr);
        pi_seed_ast(n->type_test.type);
        return;
    case AST_CONDITIONAL:
        pi_seed_ast(n->conditional.cond);
        pi_seed_ast(n->conditional.then_expr);
        pi_seed_ast(n->conditional.else_expr);
        return;
    case AST_LAMBDA:
        pi_seed_list(&n->lambda.params);
        pi_seed_ast(n->lambda.body);
        return;
    case AST_AWAIT_EXPR:
        pi_seed_ast(n->await_expr.expr);
        return;
    case AST_STRING_INTERP:
        pi_seed_list(&n->string_interp.parts);
        pi_seed_list(&n->string_interp.formats);
        return;
    case AST_TUPLE_EXPR:
        pi_seed_list(&n->tuple_expr.items);
        return;
    case AST_TUPLE_TYPE:
        pi_seed_list(&n->tuple_type.elems);
        return;
    case AST_REF_ARG:
        pi_seed_ast(n->ref_arg.expr);
        pi_seed_ast(n->ref_arg.decl_type);
        return;
    case AST_NAMED_ARG:
        pi_seed_ast(n->named_arg.expr);
        return;
    case AST_COLL_INIT:
        pi_seed_list(&n->coll_init.items);
        return;

    default:
        return;
    }
}

static void pi_add_package_source(const char *path, void *context) {
    pi_dir_t *d = (pi_dir_t *)context;
    pi_add_file(d, path);
    if (d->file_count > 0) { d->files[d->file_count - 1].pkg_src = 1; }
}

static void pi_process_dir(pi_dir_t *d, const char *stdlib_root) {
    if (d->reached) return;
    d->reached = 1;
    int before = d->file_count;
    pi_glob_into(d, stdlib_root, d->subdir);
    int hierarchical = stdlib_has_dir(stdlib_root, d->subdir) == 0;
    int found = d->file_count != before;
    int pre_visit = d->file_count;
    int package_count = package_visit_namespace(d->subdir, pi_add_package_source,
                                                  d, hierarchical);
    found = found || package_count > 0;
    if (!hierarchical) {
        for (int i = pre_visit; i < d->file_count; i++)
            d->files[i].gate_live = 1;
    }
    if (!found && package_project_root[0] != '\0' &&
        strcmp(d->subdir, "System") != 0 &&
        !project_namespace_declared(d->subdir)) {
        fprintf(stderr, "ZANPKG_MISSING namespace=%s\n", d->subdir);
        missing_namespace_count++;
    }
    for (int i = 0; i < d->file_count; i++)
        pi_scan_file(&d->files[i]);
}

static int pi_close_once(const char *stdlib_root) {
    int changed = 0;
    for (pi_dir_t *d = pi_dirs_head; d; d = d->next) {
        if (!d->reached) pi_process_dir(d, stdlib_root);
        for (int i = 0; i < d->file_count; i++) {
            pi_file_t *f = &d->files[i];
            if (f->included) continue;
            int hit = f->has_ext || f->has_controller;
            const char *why = f->has_ext ? "ext"
                              : (f->has_controller ? "controller" : NULL);
            for (int k = 0; k < f->top_count && !hit; k++)
                if (f->top[k]->flagged) { hit = 1; why = f->top[k]->str; }
            if (!hit) continue;
            f->included = 1;
            if (getenv("ZAN_PULLIN_DEBUG") != NULL)
                fprintf(stderr, "[pullin] incl %s because %s\n",
                        f->path ? f->path : "?", why ? why : "?");
            changed = 1;
            for (int k = 0; k < f->using_count; k++)
                pi_reach(f->usings[k]);
        }
    }
    return changed;
}


static int pi_repair_done = 0;

static pi_name_t *pi_unsatisfied_set[256];
static int pi_unsatisfied_count = 0;

static int pi_name_declared_by_scanned(pi_name_t *n) {
    for (pi_dir_t *d = pi_dirs_head; d; d = d->next)
        for (int i = 0; i < d->file_count; i++)
            for (int k = 0; k < d->files[i].top_count; k++)
                if (d->files[i].top[k] == n) return 1;
    return 0;
}

static int pi_unsatisfied_live_name(void) {
    pi_unsatisfied_count = 0;
    for (unsigned b = 0; b < PI_BUCKETS; b++)
        for (pi_name_t *p = pi_table[b]; p; p = p->next) {
            if (!p->flagged_stdlib || pi_name_declared_by_scanned(p)) continue;
            if (pi_unsatisfied_count >= (int)(
                    sizeof(pi_unsatisfied_set) / sizeof(pi_unsatisfied_set[0])))
                return 1;
            pi_unsatisfied_set[pi_unsatisfied_count++] = p;
        }
    return pi_unsatisfied_count > 0;
}

static void pi_unsatisfied_mark_declared(pi_name_t *n) {
    for (int i = 0; i < pi_unsatisfied_count; i++) {
        if (pi_unsatisfied_set[i] != n) continue;
        pi_unsatisfied_set[i] = pi_unsatisfied_set[--pi_unsatisfied_count];
        return;
    }
}

static void pi_repair_scan_file(const char *path) {
    pi_file_t f;
    memset(&f, 0, sizeof(f));
    f.path = (char *)path;
    pi_repair_scanning = 1;
    pi_scan_file(&f);
    pi_repair_scanning = 0;
    for (int k = 0; k < f.top_count; k++)
        pi_unsatisfied_mark_declared(f.top[k]);
}

static int pi_repair_walk(const char *root, const char *rel) {
    char dir_path[1024];
    if (rel[0]) snprintf(dir_path, sizeof(dir_path), "%s/%s", root, rel);
    else snprintf(dir_path, sizeof(dir_path), "%s", root);
    int reached_any = 0;
    int dir_declares = 0;
#ifdef _WIN32
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir_path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.cFileName[0] == '.') continue;
        char child_path[1024];
        snprintf(child_path, sizeof(child_path), "%s\\%s", dir_path,
                 fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            char child[512];
            snprintf(child, sizeof(child), "%s%s%s", rel, rel[0] ? "/" : "",
                     fd.cFileName);
            if (pi_repair_walk(root, child)) reached_any = 1;
            continue;
        }
        size_t nlen = strlen(fd.cFileName);
        if (nlen < 5 || strcmp(fd.cFileName + nlen - 4, ".zan") != 0) continue;
        int before = pi_unsatisfied_count;
        pi_repair_scan_file(child_path);
        if (pi_unsatisfied_count < before) dir_declares = 1;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir_path);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char entry_path[1024];
        snprintf(entry_path, sizeof(entry_path), "%s/%s", dir_path,
                 e->d_name);
        struct stat st;
        if (lstat(entry_path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
            char child[512];
            snprintf(child, sizeof(child), "%s%s%s", rel, rel[0] ? "/" : "",
                     e->d_name);
            if (pi_repair_walk(root, child)) reached_any = 1;
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;
        size_t nlen = strlen(e->d_name);
        if (nlen < 5 || strcmp(e->d_name + nlen - 4, ".zan") != 0) continue;
        int before = pi_unsatisfied_count;
        pi_repair_scan_file(entry_path);
        if (pi_unsatisfied_count < before) dir_declares = 1;
    }
    closedir(d);
#endif
    if (dir_declares) {
        if (getenv("ZAN_PULLIN_DEBUG") != NULL)
            fprintf(stderr, "[pullin] repair reach %s\n",
                    rel[0] ? rel : "(root)");
        pi_reach(rel);
        reached_any = 1;
    }
    return reached_any;
}

static void pi_seed_parsed_unit(zan_ast_node_t *unit, int is_entry) {
    {
        for (int i = 0; i < unit->comp_unit.usings.count; i++) {
            zan_ast_node_t *u = unit->comp_unit.usings.items[i];
            zan_ast_node_t *qn = u ? u->using_decl.name : NULL;
            if (!qn || qn->kind != AST_QUALIFIED_NAME) continue;
            for (int k = 0; k < qn->qualified_name.parts.count; k++) {
                zan_istr_t seg =
                    qn->qualified_name.parts.items[k]->ident.name;
                if (!seg.str) continue;
                pi_name_t *nr = pi_intern(seg.str, (size_t)seg.len);
                if (nr) nr->ns_root = 1;
            }
        }
        if (unit->comp_unit.ns &&
            unit->comp_unit.ns->kind == AST_NAMESPACE_DECL) {
            zan_ast_node_t *qn = unit->comp_unit.ns->namespace_decl.name;
            if (qn && qn->kind == AST_QUALIFIED_NAME)
                for (int k = 0; k < qn->qualified_name.parts.count; k++) {
                    zan_istr_t seg =
                        qn->qualified_name.parts.items[k]->ident.name;
                    if (!seg.str) continue;
                    pi_name_t *nr = pi_intern(seg.str, (size_t)seg.len);
                    if (nr) nr->ns_root = 1;
                }
        }
        if (is_entry) {
            for (int i = 0; i < unit->comp_unit.usings.count; i++) {
                zan_ast_node_t *u = unit->comp_unit.usings.items[i];
                zan_ast_node_t *qn = u ? u->using_decl.name : NULL;
                if (!qn || qn->kind != AST_QUALIFIED_NAME) continue;
                char subdir[1024];
                size_t used = 0;
                for (int k = 0; k < qn->qualified_name.parts.count; k++) {
                    zan_istr_t seg =
                        qn->qualified_name.parts.items[k]->ident.name;
                    if (!seg.str) continue;
                    if (used && used + 1 < sizeof(subdir))
                        subdir[used++] = '/';
                    for (unsigned c = 0;
                         c < seg.len && used + 1 < sizeof(subdir); c++)
                        subdir[used++] = seg.str[c];
                }
                subdir[used] = 0;
                pi_reach(subdir);
                if (u->using_decl.is_static &&
                    qn->qualified_name.parts.count > 0)
                    pi_flag_istr(qn->qualified_name.parts.items[
                        qn->qualified_name.parts.count - 1]->ident.name);
            }
        }
        if (is_entry && !pi_seed_stdlib_input) {
            for (int i = 0; i < unit->comp_unit.decls.count; i++) {
                zan_ast_node_t *d = unit->comp_unit.decls.items[i];
                if (!d || (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL &&
                           d->kind != AST_INTERFACE_DECL &&
                           d->kind != AST_DELEGATE_DECL))
                    continue;
                if (zan_ast_ns_name(d).len > 0) continue;
                zan_istr_t name = d->type_decl.name;
                if (!name.str || name.len <= 0) continue;
                pi_name_t *nm = pi_intern(name.str,
                                          (size_t)name.len);
                if (nm) nm->user_decl = 1;
            }
        }
        pi_seed_ast(unit);
    }
}

static void pi_debug_dump(void) {
    if (getenv("ZAN_PULLIN_DEBUG") == NULL) return;
    for (pi_dir_t *d = pi_dirs_head; d; d = d->next)
        for (int i = 0; i < d->file_count; i++)
            fprintf(stderr, "[pullin] %s %s (top=%d, idents=%d)\n",
                    d->files[i].included ? "INCL" : "skip",
                    d->files[i].path ? d->files[i].path : "(null)",
                    d->files[i].top_count, d->files[i].ident_count);
}

static int pi_append_included(const char ***files, int *count, int *cap) {
    int before = *count;
    for (pi_dir_t *d = pi_dirs_head; d; d = d->next)
        for (int i = 0; i < d->file_count; i++) {
            pi_file_t *f = &d->files[i];
            if (f->included && !f->parsed && f->path) {
                add_stdlib_input(files, count, cap, f->path);
                f->parsed = 1;
            }
        }
    return *count - before;
}

