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
            if (depth <= 1 && (prev == TK_EOF || prev == TK_SEMICOLON ||
                               prev == TK_RBRACE || prev == TK_LBRACE ||
                               prev == TK_RBRACKET)) {
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
                        (next.str_val.len == 9 && memcmp(next.str_val.str, "HttpPatch", 9) == 0) ||
                        (next.str_val.len == 4 && memcmp(next.str_val.str, "Test", 4) == 0)) {
                        f->has_controller = 1;
                    }
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

