static void sh_probe_mem(const char *tag) {
#ifdef _WIN32
    if (!getenv("ZAN_PROBE_MEM")) return;
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        fprintf(stderr, "    [probe %-22s Commit: %4zu MB, Peak: %4zu MB]\n",
                tag, pmc.PagefileUsage / (1024 * 1024),
                pmc.PeakPagefileUsage / (1024 * 1024));
    }
#endif
}

enum {
    SH_F_DECL = 0,
    SH_F_DEMOTE,
    SH_F_EXT,
    SH_F_BLOCK
};

enum {
    SH_G_DECL = 0,
    SH_G_TRAVEL,    /* 内部常量：定义直接复制到每个引用它的分片模块中 */
    SH_G_BLOCK      /* 无法迁移的全局状态（初始化器逃逸至内部函数/别名/TLS）：保留在协调器模块 */
};

typedef struct {
    void     **keys;
    int       *vals;
    int        mask;
    int        n;
} sh_map_t;

static size_t sh_hash_ptr(void *v) {
    size_t x = (size_t)(uintptr_t)v;
    x ^= x >> 17; x *= (size_t)0x9E3779B9U;
    return x;
}

static void sh_map_init(sh_map_t *m) {
    m->mask = 1023;
    m->n = 0;
    m->keys = (void **)malloc(1024 * sizeof(*m->keys));
    m->vals = (int *)malloc(1024 * sizeof(*m->vals));
    for (int i = 0; i < 1024; i++) m->vals[i] = -1;
}

static void sh_map_free(sh_map_t *m) { free(m->keys); free(m->vals); }

static int sh_map_get(sh_map_t *m, void *k) {
    size_t i = sh_hash_ptr(k) & (size_t)m->mask;
    while (m->vals[i] >= 0) {
        if (m->keys[i] == k) return m->vals[i];
        i = (i + 1) & (size_t)m->mask;
    }
    return -1;
}

static bool sh_map_put(sh_map_t *m, void *k, int v) {
    if (m->n * 2 >= m->mask + 1) {
        int ncap = (m->mask + 1) * 4, nmask = ncap - 1;
        void **nk = (void **)malloc((size_t)ncap *
                                                  sizeof(*nk));
        int *nv = (int *)malloc((size_t)ncap * sizeof(*nv));
        if (!nk || !nv) { free(nk); free(nv); return false; }
        for (int i = 0; i < ncap; i++) nv[i] = -1;
        for (int i = 0; i <= m->mask; i++) {
            if (m->vals[i] < 0) continue;
            size_t j = sh_hash_ptr(m->keys[i]) & (size_t)nmask;
            while (nv[j] >= 0) j = (j + 1) & (size_t)nmask;
            nk[j] = m->keys[i]; nv[j] = m->vals[i];
        }
        free(m->keys); free(m->vals);
        m->keys = nk; m->vals = nv; m->mask = nmask;
    }
    size_t i = sh_hash_ptr(k) & (size_t)m->mask;
    while (m->vals[i] >= 0) {
        if (m->keys[i] == k) { m->vals[i] = v; return true; }
        i = (i + 1) & (size_t)m->mask;
    }
    m->keys[i] = k; m->vals[i] = v; m->n++;
    return true;
}

typedef struct { char *p; size_t n, cap; bool oom; } sh_sbuf_t;

static void sh_sb_putn(sh_sbuf_t *b, const char *s, size_t n) {
    if (b->oom || !n) return;
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->n + n + 1) nc *= 2;
        char *np = (char *)realloc(b->p, nc);
        if (!np) { b->oom = true; return; }
        b->p = np; b->cap = nc;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = '\0';
}

static void sh_sb_puts(sh_sbuf_t *b, const char *s) { sh_sb_putn(b, s, strlen(s)); }

typedef struct { LLVMValueRef fn; LLVMValueRef glob; unsigned old_linkage; } sh_link_rec_t;
typedef struct { LLVMValueRef orig, decl; char *origname; } sh_move_rec_t;

typedef struct {
    zan_irgen_t      *g;
    sh_map_t          fn_v;        /* fn      -> SH_F_* */
    sh_map_t          glob_v;      /* global  -> SH_G_* */
    sh_map_t          members_all;
    sh_map_t          needs_decl;
    sh_map_t          type_done;
    sh_link_rec_t    *link_recs;   int link_n, link_cap;
    sh_map_t          ext_globs;
    sh_map_t          ext_fns;
    bool              failed;
    bool              optimizer_failed; /* cannot recover by emitting the coordinator */
    char              reason[256];
} sh_state_t;

static void sh_fail(sh_state_t *st, const char *fmt, ...) {
    if (st->failed) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(st->reason, sizeof(st->reason), fmt, ap);
    va_end(ap);
    st->failed = true;
}

static bool sh_name_is(const LLVMValueRef v, const char *pfx) {
    const char *n = LLVMGetValueName(v);
    return n && strncmp(n, pfx, strlen(pfx)) == 0;
}

typedef struct {
    long long   offset;
    size_t      len;
} sh_body_slice_t;

typedef struct {
    char            **keys;
    sh_body_slice_t  *vals;
    int               mask;
    int               count;
} sh_body_index_t;

static size_t sh_hash_str(const char *s, size_t len) {
    size_t h = 2166136261U;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619U;
    }
    return h;
}

static int sh_hex_digit(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* 解码 LLVM 打印器输出的 @name 标识符 */
static char *sh_body_header_name(const char *header) {
    bool quoted = false;
    const char *p = header + 7;
    for (; *p; p++) {
        if (*p == '"') quoted = !quoted;
        else if (*p == '@' && !quoted) break;
    }
    if (*p != '@') return NULL;
    p++;
    const char *start = p;
    if (*p == '"') {
        start = ++p;
        while (*p && *p != '"') p++;
        if (*p != '"') return NULL;
        size_t len = (size_t)(p - start);
        char *name = (char *)malloc(len + 1);
        if (!name) return NULL;
        size_t n = 0;
        for (size_t i = 0; i < len; i++) {
            unsigned char c = (unsigned char)start[i];
            if (c == '\\') {
                if (i + 2 >= len) { free(name); return NULL; }
                int hi = sh_hex_digit((unsigned char)start[i + 1]);
                int lo = sh_hex_digit((unsigned char)start[i + 2]);
                if (hi < 0 || lo < 0) { free(name); return NULL; }
                c = (unsigned char)((hi << 4) | lo);
                i += 2;
            }
            if (!c) { free(name); return NULL; }
            name[n++] = (char)c;
        }
        name[n] = '\0';
        if (!n) { free(name); return NULL; }
        return name;
    }
    while (*p && *p != '(' && *p != ' ' && *p != '\t' &&
           *p != '\r' && *p != '\n') p++;
    size_t len = (size_t)(p - start);
    if (!len) return NULL;
    char *name = (char *)malloc(len + 1);
    if (name) { memcpy(name, start, len); name[len] = '\0'; }
    return name;
}

static bool sh_body_index_add(sh_body_index_t *idx, char *name,
                              long long offset, size_t len) {
    /* 内存不足拒绝不完整索引构建 */
    if ((size_t)(idx->count + 1) * 4 >= (size_t)(idx->mask + 1) * 3) {
        int old_cap = idx->mask + 1;
        if (old_cap > INT_MAX / 2) return false;
        int new_cap = old_cap * 2, new_mask = new_cap - 1;
        char **keys = (char **)calloc((size_t)new_cap, sizeof(*keys));
        sh_body_slice_t *vals = (sh_body_slice_t *)calloc((size_t)new_cap,
                                                         sizeof(*vals));
        if (!keys || !vals) { free(keys); free(vals); return false; }
        for (int i = 0; i < old_cap; i++) {
            if (!idx->keys[i]) continue;
            size_t h = sh_hash_str(idx->keys[i], strlen(idx->keys[i])) &
                       (size_t)new_mask;
            while (keys[h]) h = (h + 1) & (size_t)new_mask;
            keys[h] = idx->keys[i];
            vals[h] = idx->vals[i];
        }
        free(idx->keys); free(idx->vals);
        idx->keys = keys; idx->vals = vals; idx->mask = new_mask;
    }
    size_t h = sh_hash_str(name, strlen(name)) & (size_t)idx->mask;
    while (idx->keys[h]) {
        if (!strcmp(idx->keys[h], name)) return false;
        h = (h + 1) & (size_t)idx->mask;
    }
    idx->keys[h] = name;
    idx->vals[h].offset = offset;
    idx->vals[h].len = len;
    idx->count++;
    return true;
}

static bool sh_build_body_index(sh_body_index_t *idx, FILE *file) {
    int cap = 1024;
    idx->mask = cap - 1;
    idx->keys = (char **)calloc((size_t)cap, sizeof(*idx->keys));
    idx->vals = (sh_body_slice_t *)calloc((size_t)cap, sizeof(*idx->vals));
    if (!idx->keys || !idx->vals) return false;

    /* 逐块读取长行：仅保留 define 函数头信息 */
    char chunk[16384];
    sh_sbuf_t header = {0};
    char *name = NULL;
    long long pos = 0, body_start = 0;
    bool line_start = true, in_body = false, in_header = false;
    bool ok = true;
    while (fgets(chunk, sizeof(chunk), file)) {
        size_t n = strlen(chunk);
        if (!n || (unsigned long long)n > (unsigned long long)(LLONG_MAX - pos)) {
            ok = false; break;
        }
        bool line_end = chunk[n - 1] == '\n';
        if (line_start && !in_body && !strncmp(chunk, "define ", 7)) {
            in_body = in_header = true;
            body_start = pos;
            header.n = 0;
        }
        if (in_header) {
            sh_sb_putn(&header, chunk, n);
            if (header.oom) { ok = false; break; }
            if (line_end) {
                name = sh_body_header_name(header.p);
                if (!name) { ok = false; break; }
                in_header = false;
            }
        } else if (in_body && line_start && chunk[0] == '}') {
            unsigned long long span = (unsigned long long)(pos - body_start) + n;
            if (!line_end || span > (unsigned long long)SIZE_MAX ||
                !sh_body_index_add(idx, name, body_start, (size_t)span)) {
                ok = false; break;
            }
            name = NULL;
            in_body = false;
        }
        pos += (long long)n;
        line_start = line_end;
    }
    if (ferror(file) || in_body) ok = false;
    free(name);
    free(header.p);
    return ok;
}

static bool sh_append_body_slice(FILE *file, const sh_body_slice_t *slice,
                                 sh_sbuf_t *bodies) {
#ifdef _WIN32
    if (_fseeki64(file, slice->offset, SEEK_SET)) return false;
#else
    off_t offset = (off_t)slice->offset;
    if ((long long)offset != slice->offset || fseeko(file, offset, SEEK_SET))
        return false;
#endif
    char chunk[16384];
    size_t left = slice->len;
    while (left) {
        size_t n = left < sizeof(chunk) ? left : sizeof(chunk);
        if (fread(chunk, 1, n, file) != n) return false;
        sh_sb_putn(bodies, chunk, n);
        if (bodies->oom) return false;
        left -= n;
    }
    return true;
}

static const sh_body_slice_t *sh_find_body_slice(const sh_body_index_t *idx, const char *name) {
    if (!idx || !idx->keys || !name) return NULL;
    size_t len = strlen(name);
    size_t h = sh_hash_str(name, len) & (size_t)idx->mask;
    while (idx->keys[h]) {
        if (strcmp(idx->keys[h], name) == 0) return &idx->vals[h];
        h = (h + 1) & (size_t)idx->mask;
    }
    return NULL;
}

static void sh_free_body_index(sh_body_index_t *idx) {
    if (!idx) return;
    if (idx->keys) {
        int cap = idx->mask + 1;
        for (int i = 0; i < cap; i++) free(idx->keys[i]);
    }
    free(idx->keys);
    free(idx->vals);
    memset(idx, 0, sizeof(*idx));
}

/* 分片引用的协调器函数必须通过外部导出符号可达 */
static int sh_fn_verdict(sh_state_t *st, LLVMValueRef f) {
    int hit = sh_map_get(&st->fn_v, f);
    if (hit >= 0) return hit;
    int v = SH_F_DECL;
    if (!LLVMIsDeclaration(f)) {
        unsigned lk = LLVMGetLinkage(f);
        if (lk == LLVMInternalLinkage || lk == LLVMPrivateLinkage)
            v = sh_name_is(f, "__zan_") ? SH_F_DEMOTE : SH_F_EXT;
    }
    sh_map_put(&st->fn_v, f, v);
    return v;
}

static bool sh_has_attached_metadata(LLVMValueRef f) {
    if (LLVMHasMetadata(f)) return true;
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(f); bb;
         bb = LLVMGetNextBasicBlock(bb))
        for (LLVMValueRef in = LLVMGetFirstInstruction(bb); in;
             in = LLVMGetNextInstruction(in))
            if (LLVMHasMetadata(in)) return true;
    return false;
}

/* 全局判定（含初始化器闭包依赖扫描） */
typedef struct { LLVMValueRef v; int depth; } sh_cw_item_t;

static int sh_global_verdict(sh_state_t *st, LLVMValueRef gv);

static bool sh_glob_closure_ok(sh_state_t *st, LLVMValueRef init,
                               sh_map_t *done, int depth) {
    if (!init || depth > 32) return true;
    if (sh_map_get(done, init) >= 0) return true;
    if (!sh_map_put(done, init, 1)) { sh_fail(st, "out of memory"); return false; }
    if (LLVMIsAFunction(init))
        return sh_fn_verdict(st, init) != SH_F_BLOCK;
    if (LLVMIsAGlobalAlias(init)) return false;
    if (LLVMIsAGlobalVariable(init)) {
        int v = sh_global_verdict(st, init);
        if (v == SH_G_BLOCK) return false;
        if (v == SH_G_TRAVEL && !LLVMIsDeclaration(init))
            return sh_glob_closure_ok(st, LLVMGetInitializer(init), done,
                                      depth + 1);
        return true;
    }
    if (LLVMIsAConstantExpr(init)) {
        unsigned nop = LLVMGetNumOperands(init);
        for (unsigned i = 0; i < nop; i++)
            if (!sh_glob_closure_ok(st, LLVMGetOperand(init, (int)i), done,
                                    depth + 1))
                return false;
    }
    return true;
}

static int sh_global_verdict(sh_state_t *st, LLVMValueRef gv) {
    int hit = sh_map_get(&st->glob_v, gv);
    if (hit >= 0) return hit;
    if (hit == -2) return SH_G_BLOCK;

    int verdict;
    if (!LLVMIsAGlobalVariable(gv)) {
        verdict = SH_G_BLOCK;
    } else if (LLVMHasMetadata(gv)) {
        /* 剥离无定义的 !N 元数据节点引用 */
        verdict = SH_G_BLOCK;
    } else if (LLVMIsDeclaration(gv)) {
        verdict = SH_G_DECL;
    } else if (LLVMIsThreadLocal(gv)) {
        /* 线程局部 (TLS) 全局变量声明：保持线程局部属性 */
        verdict = SH_G_BLOCK;
    } else {
        unsigned lk = LLVMGetLinkage(gv);
        bool intern = lk == LLVMInternalLinkage || lk == LLVMPrivateLinkage;
        if (LLVMIsGlobalConstant(gv) && intern) {
            sh_map_put(&st->glob_v, gv, -2);   /* in progress (cycles) */
            sh_map_t done; sh_map_init(&done);
            bool ok = sh_glob_closure_ok(st, LLVMGetInitializer(gv), &done, 0);
            sh_map_free(&done);
            verdict = ok ? SH_G_TRAVEL : SH_G_BLOCK;
            if (st->failed) return SH_G_DECL;
            sh_map_put(&st->glob_v, gv, verdict);   /* overwrites -2 */
            return verdict;
        }
        verdict = SH_G_DECL;
    }
    if (st->failed) return SH_G_DECL;
    sh_map_put(&st->glob_v, gv, verdict);
    return verdict;
}

typedef struct {
    sh_state_t *st;
    sh_map_t    decl_fns;
    sh_map_t    decl_globs;
    sh_map_t    travel;
    sh_map_t    body_done;
    sh_map_t   *closure_seen;
} sh_refs_t;

static bool sh_refs_note_fn(sh_refs_t *r, LLVMValueRef f);
static void sh_link_rec(sh_state_t *st, LLVMValueRef v, bool is_glob) {
    if (st->link_n == st->link_cap) {
        st->link_cap = st->link_cap ? st->link_cap * 2 : 64;
        st->link_recs = (sh_link_rec_t *)realloc(
            st->link_recs, (size_t)st->link_cap * sizeof(*st->link_recs));
    }
    st->link_recs[st->link_n].fn = is_glob ? NULL : v;
    st->link_recs[st->link_n].glob = is_glob ? v : NULL;
    st->link_recs[st->link_n].old_linkage = LLVMGetLinkage(v);
    st->link_n++;
}
static bool sh_link_glob(sh_state_t *st, LLVMValueRef gv) {
    if (sh_map_get(&st->ext_globs, gv) >= 0) return true;
    if (!sh_map_put(&st->ext_globs, gv, 1)) return false;
    sh_link_rec(st, gv, true);
    LLVMSetLinkage(gv, LLVMExternalLinkage);
    return true;
}
static bool sh_refs_note_global(sh_refs_t *r, LLVMValueRef gv);

static void sh_mark_closure_fns(sh_state_t *st, sh_refs_t *r, LLVMValueRef gv) {
    /* 跨分片函数引用记录：分片间独立记录声明集 */
    sh_map_t walk_done; sh_map_init(&walk_done);
    sh_cw_item_t *stack = (sh_cw_item_t *)malloc(64 * sizeof(*stack));
    if (!stack) { sh_fail(st, "out of memory"); return; }
    int sn = 1, scap = 64;
    stack[0].v = LLVMGetInitializer(gv); stack[0].depth = 0;
    while (sn > 0) {
        sh_cw_item_t it = stack[--sn];
        LLVMValueRef v = it.v;
        if (!v || it.depth > 32) continue;
        if (sh_map_get(&walk_done, v) >= 0) continue;
        sh_map_put(&walk_done, v, 1);
        if (LLVMIsAFunction(v)) {
            if (sh_map_get(&st->members_all, v) >= 0) {
                sh_map_put(&st->needs_decl, v, 1);
                if (sh_map_get(&r->decl_fns, v) < 0)
                    sh_map_put(&r->decl_fns, v, 1);
            } else
                sh_refs_note_fn(r, v);
            continue;
        }
        if (LLVMIsAGlobalVariable(v)) {
            if (LLVMIsDeclaration(v)) continue;
            int gvv = sh_map_get(&st->glob_v, v) >= 0
                          ? sh_global_verdict(st, v) : SH_G_DECL;
            if (sn + 2 > scap) {
                scap *= 2;
                sh_cw_item_t *ns = (sh_cw_item_t *)realloc(
                    stack, (size_t)scap * sizeof(*ns));
                if (!ns) { sh_fail(st, "out of memory"); break; }
                stack = ns;
            }
            if (gvv == SH_G_TRAVEL) {
                /* 嵌套可迁移全局变量：完整定义打印至当前分片。
                 * 依赖闭包走查必须与 note_global 的 TRAVEL 路径同构
                 * （memo 防重入、每层重置深度预算）：反射元数据链
                 * （fields→ctors→mtab→type→targs→str）深于本函数的
                 * 32 层栈深上限，裸栈推进会在深层被静默截断，分片
                 * 打出 type 却缺其引用的 str/targs 定义（B-ID49）。 */
                if (sh_map_get(&r->travel, v) < 0) {
                    sh_map_put(&r->travel, v, 1);
                    if (!r->closure_seen ||
                        sh_map_get(r->closure_seen, v) < 0) {
                        if (r->closure_seen)
                            sh_map_put(r->closure_seen, v, 1);
                        sh_mark_closure_fns(st, r, v);
                    }
                }
                continue;
            }
            if (!sh_refs_note_global(r, v)) { break; }
            continue;
        }
        /* 复合常量（结构体/数组字面量）递归遍历其包含的指针 */
        if (LLVMIsAConstant(v)) {
            unsigned nop = LLVMGetNumOperands(v);
            for (unsigned i = 0; i < nop; i++) {
                if (sn + 1 > scap) {
                    scap *= 2;
                    sh_cw_item_t *ns = (sh_cw_item_t *)realloc(
                        stack, (size_t)scap * sizeof(*ns));
                    if (!ns) { sh_fail(st, "out of memory"); break; }
                    stack = ns;
                }
                stack[sn].v = LLVMGetOperand(v, (int)i);
                stack[sn].depth = it.depth + 1; sn++;
            }
        }
    }
    free(stack);
    sh_map_free(&walk_done);
}

