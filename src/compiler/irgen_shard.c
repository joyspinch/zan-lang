/* 内部辅助实现 */

#include <llvm-c/IRReader.h>
#include <limits.h>
#include <time.h>
#ifndef _WIN32
#include <sys/types.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif
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
    SH_F_DECL = 0,  /* 核心系统底层抽象与内存语义契约 */
    SH_F_DEMOTE,    /* 底层系统交互与数据协议契约 */
    SH_F_EXT,       /* 底层系统交互与数据协议契约 */
    SH_F_BLOCK      /* 核心系统底层抽象与内存语义契约 */
};

enum {
    SH_G_DECL = 0,  /* 核心系统底层抽象与内存语义契约 */
    SH_G_TRAVEL,    /* 内部常量：定义直接复制到每个引用它的分片模块中 */
    SH_G_BLOCK      /* 无法迁移的全局状态（初始化器逃逸至内部函数/别名/TLS）：保留在协调器模块 */
};

/* 核心系统底层抽象与内存语义契约 */
typedef struct {
    void     **keys;
    int       *vals;      /* 核心系统底层抽象与内存语义契约 */
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

/* 核心系统底层抽象与内存语义契约 */
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

/* 核心系统底层抽象与内存语义契约 */
typedef struct { LLVMValueRef fn; LLVMValueRef glob; unsigned old_linkage; } sh_link_rec_t;
typedef struct { LLVMValueRef orig, decl; char *origname; } sh_move_rec_t;

typedef struct {
    zan_irgen_t      *g;
    sh_map_t          fn_v;        /* fn      -> SH_F_* */
    sh_map_t          glob_v;      /* global  -> SH_G_* */
    sh_map_t          members_all; /* 核心系统底层抽象与内存语义契约 */
    sh_map_t          needs_decl;  /* 核心系统底层抽象与内存语义契约 */
    sh_map_t          type_done;   /* 核心系统底层抽象与内存语义契约 */
    sh_link_rec_t    *link_recs;   int link_n, link_cap;
    sh_map_t          ext_globs;   /* 核心系统底层抽象与内存语义契约 */
    sh_map_t          ext_fns;     /* 核心系统底层抽象与内存语义契约 */
    bool              failed;
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

/* 模块核心语义抽象与接口调用契约 */
typedef struct {
    long long   offset;       /* 核心系统底层抽象与内存语义契约 */
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
            /* 模块核心语义抽象与接口调用契约 */
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
            name = NULL; /* 核心系统底层抽象与内存语义契约 */
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

/* 核心系统底层抽象与内存语义契约 */
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

/* 模块核心语义抽象与接口调用契约 */
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
/* 内部辅助实现 */
typedef struct { LLVMValueRef v; int depth; } sh_cw_item_t;

static int sh_global_verdict(sh_state_t *st, LLVMValueRef gv);

/* 模块核心语义抽象与接口调用契约 */
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
    if (hit == -2) return SH_G_BLOCK;   /* 核心系统底层抽象与内存语义契约 */

    int verdict;
    if (!LLVMIsAGlobalVariable(gv)) {
        verdict = SH_G_BLOCK;           /* 核心系统底层抽象与内存语义契约 */
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
    sh_map_t    decl_fns;    /* 核心系统底层抽象与内存语义契约 */
    sh_map_t    decl_globs;  /* 核心系统底层抽象与内存语义契约 */
    sh_map_t    travel;      /* 核心系统底层抽象与内存语义契约 */
    sh_map_t    body_done;   /* 核心系统底层抽象与内存语义契约 */
    sh_map_t   *closure_seen;/* 底层系统交互与数据协议契约 */
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

/* 内部辅助实现 */
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
                /* 模块核心语义抽象与接口调用契约 */
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
                /* 嵌套可迁移全局变量：完整定义打印至当前分片 */
                if (sh_map_get(&r->travel, v) < 0)
                    sh_map_put(&r->travel, v, 1);
            } else {
                /* 底层系统交互与数据协议契约 */
                if (!sh_refs_note_global(r, v)) { break; }
                continue;
            }
            stack[sn].v = v; stack[sn].depth = it.depth + 1; sn++;
            stack[sn].v = LLVMGetInitializer(v);
            stack[sn].depth = it.depth + 1; sn++;
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

/* 核心系统底层抽象与内存语义契约 */
static time_t sh_trace_t0(void) {
    static time_t t0 = 0;
    if (!t0) t0 = time(NULL);
    return t0;
}
static int sh_trace_on(void) {
    static int on = -1;
    if (on < 0) { const char *e = getenv("ZAN_SHARD_TRACE"); on = e && *e == '1'; }
    return on;
}
#define SH_TRACE(...)     do { if (sh_trace_on()) { \
        fprintf(stderr, "shard-trace [%lus]: ", \
                (unsigned long)(time(NULL) - sh_trace_t0())); \
        fprintf(stderr, __VA_ARGS__); \
        fflush(stderr); } } while (0)

/* 核心系统底层抽象与内存语义契约 */
/* 内部辅助实现 */
static bool sh_refs_note_fn(sh_refs_t *r, LLVMValueRef f) {
    sh_state_t *st = r->st;
    if (sh_map_get(&st->members_all, f) >= 0) {
        /* 跨分片成员：目标分片以 external 链接发射，本分片仅声明 */
        sh_map_put(&st->needs_decl, f, 1);
        if (sh_map_get(&r->decl_fns, f) < 0)
            if (!sh_map_put(&r->decl_fns, f, 1)) { sh_fail(st, "out of memory"); return false; }
        return true;
    }
    int v = sh_fn_verdict(st, f);
    if (v == SH_F_EXT) {
        /* 导出协调器端函数体以供分片解析，记录回滚状态 */
        if (sh_map_get(&st->ext_fns, f) < 0) {
            if (!sh_map_put(&st->ext_fns, f, 1)) { sh_fail(st, "out of memory"); return false; }
            sh_link_rec(st, f, false);
            LLVMSetLinkage(f, LLVMExternalLinkage);
        }
    } else if (v == SH_F_BLOCK) {
        sh_fail(st, "body references unshardable fn '%s'",
                LLVMGetValueName(f));
        return false;
    }
    /* 模块核心语义抽象与接口调用契约 */
    if (sh_map_get(&r->decl_fns, f) < 0)
        if (!sh_map_put(&r->decl_fns, f, 1)) { sh_fail(st, "out of memory"); return false; }
    return true;
}

static bool sh_refs_note_global(sh_refs_t *r, LLVMValueRef gv) {
    sh_state_t *st = r->st;
    int v = sh_global_verdict(st, gv);
    if (v == SH_G_BLOCK) {
        sh_fail(st, "fn references unshardable global '%s'",
                LLVMGetValueName(gv));
        return false;
    }
    if (v == SH_G_TRAVEL) {
        if (sh_map_get(&r->travel, gv) < 0)
            if (!sh_map_put(&r->travel, gv, 1)) { sh_fail(st, "out of memory"); return false; }
        /* 每分片独立计算闭包，结果计入本分片声明集合 */
        if (!r->closure_seen || sh_map_get(r->closure_seen, gv) < 0) {
            if (r->closure_seen) sh_map_put(r->closure_seen, gv, 1);
            sh_mark_closure_fns(st, r, gv);
        }
    } else {
        /* 外部声明全局变量：协调器端导出对应符号 */
        if (sh_map_get(&r->decl_globs, gv) < 0)
            if (!sh_map_put(&r->decl_globs, gv, 1)) { sh_fail(st, "out of memory"); return false; }
        if (LLVMIsDeclaration(gv)) return true;
        if (!sh_link_glob(st, gv)) { sh_fail(st, "out of memory"); return false; }
    }
    return true;
}

static bool sh_refs_walk_value(sh_refs_t *r, LLVMValueRef v, int depth) {
    if (!v || depth > 32) return true;
    sh_state_t *st = r->st;
    if (LLVMIsAFunction(v)) return sh_refs_note_fn(r, v);
    if (LLVMIsAGlobalVariable(v)) return sh_refs_note_global(r, v);
    if (LLVMIsAGlobalAlias(v)) {
        sh_fail(st, "reference to alias '%s'", LLVMGetValueName(v));
        return false;
    }
    /* 复合常量内嵌符号引用遍历 */
    if (LLVMIsAConstant(v)) {
        unsigned nop = LLVMGetNumOperands(v);
        for (unsigned i = 0; i < nop; i++)
            if (!sh_refs_walk_value(r, LLVMGetOperand(v, (int)i), depth + 1))
                return false;
    }
    return true;
}

static bool sh_refs_scan_fn(sh_refs_t *r, LLVMValueRef fn) {
    sh_state_t *st = r->st;
    /* 规避当前 LLVM 版本的 LLVMGetPersonalityFn 空指针缺陷 */
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn); bb;
         bb = LLVMGetNextBasicBlock(bb)) {
        for (LLVMValueRef in = LLVMGetFirstInstruction(bb); in;
             in = LLVMGetNextInstruction(in)) {
            unsigned nop = LLVMGetNumOperands(in);
            for (unsigned i = 0; i < nop; i++) {
                /* 直接调用的被调函数位于操作数末位 */
                if (!sh_refs_walk_value(r, LLVMGetOperand(in, (int)i), 0))
                    return false;
            }
        }
    }
    (void)st;
    return true;
}

/* ---- planning -------------------------------------------------------------- */
typedef struct {
    int         *idx;        /* 核心系统底层抽象与内存语义契约 */
    int          n, cap;
    long long    insns;
    const char  *minname;
} sh_comp_t;

typedef struct {
    int mf_i;                /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef fn;
    int  comp;
    bool needs_decl;
} sh_member_t;

static int sh_cmp_comp(const void *a, const void *b) {
    const sh_comp_t *x = (const sh_comp_t *)a, *y = (const sh_comp_t *)b;
    if (x->insns != y->insns) return y->insns > x->insns ? 1 : -1;
    return strcmp(x->minname, y->minname);
}

static int sh_cmp_name_ref(const void *a, const void *b) {
    LLVMValueRef x = *(LLVMValueRef *)a, y = *(LLVMValueRef *)b;
    return strcmp(LLVMGetValueName(x), LLVMGetValueName(y));
}

/* 核心系统底层抽象与内存语义契约 */
/* 内部辅助实现 */
/* 核心系统底层抽象与内存语义契约 */
/* 内部辅助实现 */
static bool sh_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '$' || c == '.' ||
           c == '-';
}

static void sh_harvest_types(sh_state_t *st, sh_sbuf_t *types, const char *text) {
    for (const char *p = text; *p; p++) {
        if (*p != '%') continue;
        bool quoted = false;
        const char *s = p + 1;
        if (*s == '"') {
            quoted = true;
            s++;
        } else if (!sh_ident_char(*s) || (*s >= '0' && *s <= '9')) {
            continue;
        }
        char name[256];
        size_t n = 0;
        if (quoted) {
            while (*s && *s != '"' && n < sizeof(name) - 1) name[n++] = *s++;
            if (*s == '"') s++;
        } else {
            while (sh_ident_char(*s) && n < sizeof(name) - 1) name[n++] = *s++;
        }
        name[n] = '\0';
        LLVMTypeRef ty = LLVMGetTypeByName2(st->g->ctx, name);
        if (!ty || sh_map_get(&st->type_done, ty) >= 0) continue;
        if (LLVMGetTypeKind(ty) != LLVMStructTypeKind) continue;
        sh_map_put(&st->type_done, ty, 1);
        if (LLVMIsOpaqueStruct(ty)) {
            char line[512];
            if (quoted) {
                snprintf(line, sizeof(line), "%%\"%s\" = type opaque\n", name);
            } else {
                snprintf(line, sizeof(line), "%%%s = type opaque\n", name);
            }
            sh_sb_puts(types, line);
            continue;
        }
        /* 递归打印依赖的子结构体类型 */
        unsigned ne = LLVMCountStructElementTypes(ty);
        LLVMTypeRef *elems = NULL;
        if (ne) {
            elems = (LLVMTypeRef *)malloc((size_t)ne * sizeof(*elems));
            if (!elems) { sh_fail(st, "out of memory"); return; }
            LLVMGetStructElementTypes(ty, elems);
            for (unsigned i = 0; i < ne; i++) {
                char *es = LLVMPrintTypeToString(elems[i]);
                sh_harvest_types(st, types, es);
                LLVMDisposeMessage(es);
            }
        }
        char line[512];
        if (quoted) {
            snprintf(line, sizeof(line), "%%\"%s\" = type %s{ ", name,
                     LLVMIsPackedStruct(ty) ? "<" : "");
        } else {
            snprintf(line, sizeof(line), "%%%s = type %s{ ", name,
                     LLVMIsPackedStruct(ty) ? "<" : "");
        }
        sh_sb_puts(types, line);
        if (ne) {
            for (unsigned i = 0; i < ne; i++) {
                char *es = LLVMPrintTypeToString(elems[i]);
                sh_sb_puts(types, es);
                LLVMDisposeMessage(es);
                if (i + 1 < ne) sh_sb_puts(types, ", ");
            }
            free(elems);
        }
        sh_sb_puts(types, LLVMIsPackedStruct(ty) ? " }>\n" : " }\n");
    }
}

static char *sh_type_name_clean(LLVMTypeRef ty) {
    if (LLVMGetTypeKind(ty) == LLVMStructTypeKind) {
        const char *name = LLVMGetStructName(ty);
        if (name && *name) {
            char buf[512];
            bool needs_quote = false;
            for (const char *p = name; *p; p++) {
                if (!sh_ident_char(*p)) { needs_quote = true; break; }
            }
            if (needs_quote) {
                snprintf(buf, sizeof(buf), "%%\"%s\"", name);
            } else {
                snprintf(buf, sizeof(buf), "%%%s", name);
            }
            return strdup(buf);
        }
    }
    char *s = LLVMPrintTypeToString(ty);
    if (!s) return strdup("void");
    char *eq = strstr(s, " = type ");
    if (eq) {
        *eq = '\0';
    }
    char *res = strdup(s);
    LLVMDisposeMessage(s);
    return res;
}

/* 核心系统底层抽象与内存语义契约 */
static void sh_emit_fn_decl(sh_sbuf_t *b, LLVMValueRef f) {
    LLVMTypeRef ft = LLVMGlobalGetValueType(f);
    LLVMTypeRef rt = LLVMGetReturnType(ft);
    char *rts = sh_type_name_clean(rt);
    sh_sb_puts(b, "declare ");
    sh_sb_puts(b, rts);
    free(rts);
    sh_sb_puts(b, " @");
    sh_sb_puts(b, LLVMGetValueName(f));
    sh_sb_puts(b, "(");
    unsigned np = LLVMCountParamTypes(ft);
    LLVMTypeRef *pts = NULL;
    if (np) {
        pts = (LLVMTypeRef *)malloc((size_t)np * sizeof(*pts));
        if (pts) LLVMGetParamTypes(ft, pts);
    }
    for (unsigned i = 0; i < np && pts; i++) {
        char *ps = sh_type_name_clean(pts[i]);
        sh_sb_puts(b, ps);
        free(ps);
        if (i + 1 < np || LLVMIsFunctionVarArg(ft)) sh_sb_puts(b, ", ");
    }
    free(pts);
    if (LLVMIsFunctionVarArg(ft)) sh_sb_puts(b, "...");
    sh_sb_puts(b, ")\n");
}

static void sh_emit_global_decl(sh_state_t *st, sh_sbuf_t *b, LLVMValueRef gv) {
    if (!LLVMIsDeclaration(gv)) {
        /* 模块核心语义抽象与接口调用契约 */
        sh_sb_puts(b, "@");
        sh_sb_puts(b, LLVMGetValueName(gv));
        sh_sb_puts(b, " = external ");
        sh_sb_puts(b, LLVMIsGlobalConstant(gv) ? "constant " : "global ");
        char *ts = sh_type_name_clean(LLVMGlobalGetValueType(gv));
        sh_sb_puts(b, ts);
        free(ts);
        sh_sb_puts(b, "\n");
        return;
    }
    /* 协调器外部声明：原样打印属性修饰符 */
    char *txt = LLVMPrintValueToString(gv);
    if (txt) { sh_sb_puts(b, txt); sh_sb_puts(b, "\n"); LLVMDisposeMessage(txt); }
    (void)st;
}

/* 核心系统底层抽象与内存语义契约 */
static LLVMTargetMachineRef sh_make_tm(sh_state_t *st) {
    LLVMTargetMachineRef tm = NULL;
    if (zan_bind_target_layout(st->g, &tm) != ZAN_OK) return NULL;
    return tm;
}

static void sh_ensure_parent_dir(const char *path) {
    if (!path) return;
    char dir[1024];
    size_t len = strlen(path);
    if (len >= sizeof(dir)) return;
    memcpy(dir, path, len + 1);
    char *p = dir;
    while (*p) {
        if ((*p == '/' || *p == '\\') && p > dir) {
            char sep = *p;
            *p = '\0';
#ifdef _WIN32
            CreateDirectoryA(dir, NULL);
#else
            mkdir(dir, 0755);
#endif
            *p = sep;
        }
        p++;
    }
}

static bool sh_emit_one_file(sh_state_t *st, const char *frag_path,
                             LLVMTargetMachineRef tm, const char *path,
                             char *errbuf, size_t errsz) {
    zan_irgen_t *g = st->g;
    sh_ensure_parent_dir(path);
    const char *dump = getenv("ZAN_SHARD_DUMP");
    if (dump && *dump) {
        FILE *df = fopen(dump, "wb");
        FILE *sf = fopen(frag_path, "rb");
        if (df && sf) {
            char cbuf[8192];
            size_t n;
            while ((n = fread(cbuf, 1, sizeof(cbuf), sf)) > 0)
                fwrite(cbuf, 1, n, df);
        }
        if (sf) fclose(sf);
        if (df) fclose(df);
    }
    LLVMMemoryBufferRef mb = NULL;
    char *perr = NULL;
    if (LLVMCreateMemoryBufferWithContentsOfFile(frag_path, &mb, &perr)) {
        snprintf(errbuf, errsz, "cannot read shard fragment: %.160s",
                 perr ? perr : "?");
        if (perr) LLVMDisposeMessage(perr);
        return false;
    }
    LLVMContextRef ctx = LLVMContextCreate();
    if (!ctx) {
        LLVMDisposeMemoryBuffer(mb);
        snprintf(errbuf, errsz, "context create failed");
        return false;
    }
    SH_TRACE("shard emit: frag %zu bytes -> parse\n", LLVMGetBufferSize(mb));
    LLVMModuleRef mod = NULL;
    /* 模块核心语义抽象与接口调用契约 */
    if (LLVMParseIRInContext(ctx, mb, &mod, &perr)) {
        snprintf(errbuf, errsz, "parse: %.160s", perr ? perr : "?");
        if (perr) LLVMDisposeMessage(perr);
        LLVMContextDispose(ctx);
        return false;
    }
    /* 模块核心语义抽象与接口调用契约 */
    LLVMSetModuleIdentifier(mod, "zan-shard", strlen("zan-shard"));
    LLVMSetSourceFileName(mod, "zan-shard", strlen("zan-shard"));
    const char *triple = LLVMGetTarget(g->mod);
    if (triple && *triple) LLVMSetTarget(mod, triple);
    LLVMSetDataLayout(mod, LLVMGetDataLayoutStr(g->mod));
    char *vmsg = NULL;
    SH_TRACE("shard emit: parse ok -> verify\n");
    if (LLVMVerifyModule(mod, LLVMReturnStatusAction, &vmsg)) {
        snprintf(errbuf, errsz, "verify: %.160s", vmsg ? vmsg : "?");
        if (vmsg) LLVMDisposeMessage(vmsg);
        LLVMDisposeModule(mod);
        LLVMContextDispose(ctx);
        return false;
    }
    if (vmsg) LLVMDisposeMessage(vmsg);
    if (g->obfuscate_strings || getenv("ZAN_SHARD_OPT")) {
        zan_opt_run_passes_on_module(mod, tm, g->obfuscate_strings ? ZAN_OPT_SIZE : ZAN_OPT_FULL);
    }
    char *eerr = NULL;
    SH_TRACE("shard emit: verify ok -> codegen\n");
    if (LLVMTargetMachineEmitToFile(tm, mod, path, LLVMObjectFile, &eerr)) {
        snprintf(errbuf, errsz, "emit: %.160s", eerr ? eerr : "?");
        if (eerr) LLVMDisposeMessage(eerr);
        LLVMDisposeModule(mod);
        LLVMContextDispose(ctx);
        return false;
    }
    LLVMDisposeModule(mod);
    LLVMContextDispose(ctx);
    return true;
}

/* 符号迁移（重命名 + 外部声明 + 全局替换 RAUW），支持完全回滚 */
static bool sh_unique_fn_name(zan_irgen_t *g, const char *base, const char *sfx,
                              int *counter, char *out, size_t outsz) {
    for (int i = 0; i < 10000; i++) {
        snprintf(out, outsz, "%s%s#%d", base, sfx, (*counter)++);
        if (!LLVMGetNamedFunction(g->mod, out) &&
            !LLVMGetNamedGlobal(g->mod, out))
            return true;
    }
    return false;
}

/* 核心系统底层抽象与内存语义契约 */
/* ---- entry ---------------------------------------------------------------- */
/* 内部辅助实现 */
static void sh_planner_mark(sh_state_t *st, LLVMValueRef v, int depth) {
    if (!v || depth > 8) return;
    if (LLVMIsAFunction(v)) {
        if (sh_map_get(&st->members_all, v) >= 0)
            sh_map_put(&st->needs_decl, v, 1);
        return;
    }
    if (LLVMIsAConstant(v)) {
        unsigned nop = LLVMGetNumOperands(v);
        for (unsigned k = 0; k < nop; k++)
            sh_planner_mark(st, LLVMGetOperand(v, (int)k), depth + 1);
    }
}

/* 追溯全局初始化常量中已重命名的函数引用 */
static void sh_trace_scan_const(LLVMValueRef owner, LLVMValueRef v, int depth) {
    if (!v || depth > 8) return;
    if (LLVMIsAFunction(v)) {
        const char *on = LLVMGetValueName(v);
        if (strstr(on, ".zsb$") || strstr(on, ".zsh$") || strstr(on, ".zshx$"))
            SH_TRACE("post-delete ref (global '%s') -> %s [decl=%d lk=%d]\n",
                     LLVMGetValueName(owner), on, LLVMIsDeclaration(v),
                     (int)LLVMGetLinkage(v));
        return;
    }
    if (LLVMIsAConstant(v)) {
        unsigned nop = LLVMGetNumOperands(v);
        for (unsigned k = 0; k < nop; k++)
            sh_trace_scan_const(owner, LLVMGetOperand(v, (int)k), depth + 1);
    }
}

int zan_irgen_shard_run(zan_irgen_t *g, const zan_cg_manifest_t *m,
                        const char *obj_base, char ***out_objs) {
    *out_objs = NULL;
    if (!m || m->fn_count == 0) return 0;

    long long max_fn = 400, max_insn = 80000;
    const char *e;
    if ((e = getenv("ZAN_SHARD_MAX_FN")) && *e) max_fn = atoll(e);
    if ((e = getenv("ZAN_SHARD_MAX_INSN")) && *e) max_insn = atoll(e);

    sh_state_t st;
    memset(&st, 0, sizeof(st));
    st.g = g;
    sh_map_init(&st.fn_v); sh_map_init(&st.glob_v);
    sh_map_init(&st.members_all);
    sh_map_init(&st.needs_decl);
    sh_map_init(&st.ext_globs);
    sh_map_init(&st.ext_fns);

    /* 模块核心语义抽象与接口调用契约 */
    sh_member_t *mem = (sh_member_t *)calloc((size_t)(m->fn_count ? m->fn_count : 1),
                                             sizeof(*mem));
    int *uf = (int *)calloc((size_t)(m->fn_count ? m->fn_count : 1), sizeof(*uf));
    int nm = 0;
    long long mov_insns = 0;
    int meta_skip = 0;
    for (int i = 0; i < m->fn_count; i++) {
        const zan_mf_fn *F = &m->fns[i];
        if (!F->defined || !F->eligible) continue;
        if (!strcmp(F->name, "Main") || !strcmp(F->name, "main")) continue;
        LLVMValueRef f = LLVMGetNamedFunction(g->mod, F->name);
        if (!f) continue;
        if (sh_has_attached_metadata(f)) {
            meta_skip++;
            continue;
        }
        mem[nm].mf_i = i;
        mem[nm].fn = f;
        mem[nm].comp = nm;
        uf[nm] = nm;
        nm++;
    }
    if (!nm) {
        fprintf(stderr, "shard: no eligible bodies — single module\n");
        free(mem); free(uf);
        sh_map_free(&st.fn_v); sh_map_free(&st.glob_v);
        sh_map_free(&st.members_all);
        sh_map_free(&st.needs_decl);
        return 0;
    }

    /* 底层系统交互与数据协议契约 */
    sh_map_t mf2mem; sh_map_init(&mf2mem);
    for (int k = 0; k < nm; k++) sh_map_put(&mf2mem, mem[k].fn, k);

    /* 脏不动点迭代：剔除无法通过外部链接解析的跨分片成员 */
    bool changed = true;
    int iter_guard = 0;
    while (changed && iter_guard++ < 64) {
        changed = false;
        for (int k = 0; k < nm; k++) {
            sh_member_t *M = &mem[k];
            if (M->comp < 0) continue;   /* 核心系统底层抽象与内存语义契约 */
            const zan_mf_fn *F = &m->fns[M->mf_i];
            bool dirty = false;
            for (int c = 0; c < F->call_cnt && !dirty; c++) {
                int ci = F->calls[c];
                LLVMValueRef cf = LLVMGetNamedFunction(g->mod, m->fns[ci].name);
                if (!cf) { dirty = true; break; }
                int slot = sh_map_get(&mf2mem, cf);
                if (slot >= 0) {
                    if (mem[slot].comp < 0) dirty = true;
                } else if (sh_fn_verdict(&st, cf) == SH_F_BLOCK) {
                    dirty = true;
                }
            }
            for (int x = 0; x < F->ext_cnt && !dirty; x++) {
                LLVMValueRef xf = LLVMGetNamedFunction(g->mod, F->exts[x]);
                if (xf && sh_fn_verdict(&st, xf) == SH_F_BLOCK) dirty = true;
            }
            for (int x = 0; x < F->glob_cnt && !dirty; x++) {
                LLVMValueRef gv2 = LLVMGetNamedGlobal(g->mod, F->globs[x]);
                if (!gv2 || sh_global_verdict(&st, gv2) == SH_G_BLOCK) dirty = true;
            }
            if (dirty) {
                M->comp = -1;
                changed = true;
            }
        }
    }
    int dropped = 0;
    for (int k = 0; k < nm; k++) {
        if (mem[k].comp < 0) {
            sh_map_put(&mf2mem, mem[k].fn, -1);
            dropped++;
        } else {
            sh_map_put(&st.members_all, mem[k].fn, 1);
            mov_insns += m->fns[mem[k].mf_i].insns;
        }
    }

    /* 装箱启发式分片：防止调用图合并为超大连通块导致分片倾斜 */
    for (int k = 0; k < nm; k++) {
        if (mem[k].comp < 0) continue;
        mem[k].comp = k;
    }

    /* ---- components ---- */
    sh_map_t root2comp; sh_map_init(&root2comp);
    sh_comp_t *comps = (sh_comp_t *)calloc((size_t)nm, sizeof(*comps));
    int ncomp = 0;
    bool comp_ok = comps != NULL;
    for (int k = 0; k < nm && comp_ok; k++) {
        if (mem[k].comp < 0) continue;
        int root = mem[k].comp;
        int ci = sh_map_get(&root2comp, mem[root].fn);
        if (ci < 0) {
            ci = ncomp++;
            if (!sh_map_put(&root2comp, mem[root].fn, ci)) {
                comp_ok = false; break;
            }
            comps[ci].minname = LLVMGetValueName(mem[k].fn);
        }
        /* 单节点连通块容量优化 */
        if (comps[ci].n == comps[ci].cap) {
            if (comps[ci].cap > INT_MAX / 2) { comp_ok = false; break; }
            int cap = comps[ci].cap ? comps[ci].cap * 2 : 1;
            int *idx = (int *)realloc(comps[ci].idx, (size_t)cap * sizeof(*idx));
            if (!idx) { comp_ok = false; break; }
            comps[ci].idx = idx;
            comps[ci].cap = cap;
        }
        comps[ci].idx[comps[ci].n++] = k;
        comps[ci].insns += m->fns[mem[k].mf_i].insns;
        const char *nm2 = LLVMGetValueName(mem[k].fn);
        if (strcmp(nm2, comps[ci].minname) < 0) comps[ci].minname = nm2;
    }
    if (!comp_ok) {
        for (int c = 0; c < ncomp; c++) free(comps[c].idx);
        free(comps); free(uf); free(mem);
        sh_map_free(&root2comp); sh_map_free(&mf2mem);
        sh_map_free(&st.fn_v); sh_map_free(&st.glob_v);
        sh_map_free(&st.members_all); sh_map_free(&st.needs_decl);
        sh_map_free(&st.ext_globs); sh_map_free(&st.ext_fns);
        free(st.link_recs);
        fprintf(stderr, "shard: out of memory planning components — single module\n");
        return 0;
    }
    qsort(comps, (size_t)ncomp, sizeof(*comps), sh_cmp_comp);

    /* 核心系统底层抽象与内存语义契约 */
    int nshard = 0;
    long long cur_fn = 0, cur_insn = 0;
    int *shard_of_comp = (int *)malloc((size_t)(ncomp ? ncomp : 1) * sizeof(int));
    for (int c = 0; c < ncomp; c++) {
        if (nshard == 0 || cur_fn + comps[c].n > max_fn ||
            cur_insn + comps[c].insns > max_insn) {
            nshard++;
            cur_fn = 0; cur_insn = 0;
        }
        shard_of_comp[c] = nshard - 1;
        cur_fn += comps[c].n;
        cur_insn += comps[c].insns;
    }
    sh_map_free(&root2comp);
    free(uf);

    fprintf(stderr,
            "shard: planning %d eligible -> %d fns / %lld insns movable "
            "(%d dropped as unshardable, %d metadata-attached), %d objects\n",
            nm + dropped + meta_skip, nm - dropped, mov_insns, dropped,
            meta_skip, nshard);
    if (nshard <= 1) {
        /* 模块核心语义抽象与接口调用契约 */
        for (int c = 0; c < ncomp; c++) free(comps[c].idx);
        free(comps); free(shard_of_comp); free(mem);
        sh_map_free(&mf2mem);
        sh_map_free(&st.fn_v); sh_map_free(&st.glob_v);
        sh_map_free(&st.members_all);
        sh_map_free(&st.needs_decl);
        return 0;
    }

    /* 核心系统底层抽象与内存语义契约 */
    for (int c = 0; c < ncomp; c++)
        for (int j = 0; j < comps[c].n; j++)
            mem[comps[c].idx[j]].comp = shard_of_comp[c];
    /* 核心系统底层抽象与内存语义契约 */

    /* 内部辅助实现 */
    for (int i = 0; i < m->fn_count; i++) {
        const zan_mf_fn *F = &m->fns[i];
        if (!F->defined) continue;
        LLVMValueRef caller = LLVMGetNamedFunction(g->mod, F->name);
        if (!caller) continue;
        int cslot = sh_map_get(&mf2mem, caller);
        if (cslot >= 0 && mem[cslot].comp >= 0) continue;
        for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(caller); bb;
             bb = LLVMGetNextBasicBlock(bb)) {
            for (LLVMValueRef in = LLVMGetFirstInstruction(bb); in;
                 in = LLVMGetNextInstruction(in)) {
                if (LLVMGetInstructionOpcode(in) == LLVMLandingPad)
                    continue; /* 核心系统底层抽象与内存语义契约 */
                unsigned nop = LLVMGetNumOperands(in);
                for (unsigned k = 0; k < nop; k++)
                    sh_planner_mark(&st, LLVMGetOperand(in, (int)k), 0);
            }
        }
    }

    /* 内部辅助实现 */

    /* 内部辅助实现 */
    {
        sh_refs_t scratch;
        memset(&scratch, 0, sizeof(scratch));
        scratch.st = &st;
        sh_map_init(&scratch.decl_fns); sh_map_init(&scratch.decl_globs);
        sh_map_init(&scratch.travel); sh_map_init(&scratch.body_done);
        scratch.closure_seen = &scratch.body_done;
        for (LLVMValueRef gv = LLVMGetFirstGlobal(g->mod); gv;
             gv = LLVMGetNextGlobal(gv)) {
            if (!LLVMGetInitializer(gv)) continue;
            sh_mark_closure_fns(&st, &scratch, gv);
            if (st.failed) break;
        }
        sh_map_free(&scratch.decl_fns); sh_map_free(&scratch.decl_globs);
        sh_map_free(&scratch.travel); sh_map_free(&scratch.body_done);
    }

    /* 第 1 遍：分片引用遍历并填充 needs_decl 需求表 */
    for (int s = 0; s < nshard && !st.failed; s++) {
        sh_refs_t refs;
        memset(&refs, 0, sizeof(refs));
        refs.st = &st;
        sh_map_init(&refs.decl_fns); sh_map_init(&refs.decl_globs);
        sh_map_init(&refs.travel); sh_map_init(&refs.body_done);
        refs.closure_seen = &refs.body_done;
        for (int c = 0; c < ncomp; c++) {
            if (shard_of_comp[c] != s) continue;
            for (int j = 0; j < comps[c].n; j++) {
                LLVMValueRef fn = mem[comps[c].idx[j]].fn;
                if (!sh_refs_scan_fn(&refs, fn)) goto pass1_done;
            }
        }
    pass1_done:
        SH_TRACE("pass1 done\n");
        sh_map_free(&refs.decl_fns); sh_map_free(&refs.decl_globs);
        sh_map_free(&refs.travel); sh_map_free(&refs.body_done);
    }

    int rc = 0;
    char **objs = NULL;
    LLVMTargetMachineRef tm = NULL;
    if (st.failed) {
        /* 回滚第 1 遍已外化修改的全局变量属性 */
        for (int i = 0; i < st.link_n; i++)
            LLVMSetLinkage(st.link_recs[i].glob ? st.link_recs[i].glob
                                               : st.link_recs[i].fn,
                           st.link_recs[i].old_linkage);
        fprintf(stderr, "shard: %s — falling back to single module\n",
                st.reason);
        goto cleanup_planning;
    }

    /* 调整外部引用函数的符号链接为 external */
    for (int k = 0; k < nm; k++) {
        if (mem[k].comp < 0) continue;
        if (sh_map_get(&st.needs_decl, mem[k].fn) >= 0) {
            sh_link_rec(&st, mem[k].fn, false);
            LLVMSetLinkage(mem[k].fn, LLVMExternalLinkage);
        }
    }
    /* 模块核心语义抽象与接口调用契约 */
    for (int i = 0; i <= st.fn_v.mask; i++) {
        if (st.fn_v.vals[i] != SH_F_DEMOTE) continue;
        LLVMValueRef f = st.fn_v.keys[i];
        sh_link_rec(&st, f, false);
        LLVMSetLinkage(f, LLVMExternalLinkage);
    }

    SH_TRACE("linkage done (%d recs)\n", st.link_n);
    /* ---- pass 2: assemble, parse, verify, emit ---- */
    tm = sh_make_tm(&st);
    if (!tm) {
        snprintf(st.reason, sizeof(st.reason), "cannot create target machine");
        st.failed = true;
    }
    objs = (char **)calloc((size_t)nshard, sizeof(*objs));
    if (!objs) { snprintf(st.reason, sizeof(st.reason), "out of memory"); st.failed = true; }

    char tmp_ll[1200];
    tmp_ll[0] = '\0';
    FILE *body_file = NULL;
    sh_body_index_t body_idx = {0};
    if (!st.failed) {
        if (!obj_base || !*obj_base) {
            sh_fail(&st, "missing shard output path");
        } else if (snprintf(tmp_ll, sizeof(tmp_ll), "%s.shard.tmp.ll", obj_base) >=
                   (int)sizeof(tmp_ll)) {
            tmp_ll[0] = '\0';
            sh_fail(&st, "shard module text path too long");
        } else {
            char *err_msg = NULL;
            if (LLVMPrintModuleToFile(g->mod, tmp_ll, &err_msg)) {
                sh_fail(&st, "cannot write shard module text: %.160s",
                        err_msg ? err_msg : "?");
            } else {
                body_file = fopen(tmp_ll, "rb");
                if (!body_file) {
                    sh_fail(&st, "cannot open shard module text (errno=%d)", errno);
                } else if (!sh_build_body_index(&body_idx, body_file)) {
                    sh_fail(&st, "cannot build complete shard body index");
                }
            }
            if (err_msg) LLVMDisposeMessage(err_msg);
        }
    }

    for (int s = 0; s < nshard && !st.failed; s++) {
        SH_TRACE("shard %d: assembling\n", s);
        sh_sbuf_t bodies = {0}, gdecls = {0}, types = {0};
        sh_map_t decl_fns, decl_globs, travel;
        sh_map_init(&decl_fns); sh_map_init(&decl_globs); sh_map_init(&travel);
        sh_map_init(&st.type_done);

        bool ok = true;
        /* 核心系统底层抽象与内存语义契约 */
        sh_map_t local; sh_map_init(&local);
        sh_map_t closure_memo; sh_map_init(&closure_memo);
        for (int c = 0; c < ncomp && ok; c++) {
            if (shard_of_comp[c] != s) continue;
            for (int j = 0; j < comps[c].n; j++)
                if (!sh_map_put(&local, mem[comps[c].idx[j]].fn, 1)) ok = false;
        }

        /* 核心系统底层抽象与内存语义契约 */
        for (int c = 0; c < ncomp && ok; c++) {
            if (shard_of_comp[c] != s) continue;
            LLVMValueRef *mbrs = (LLVMValueRef *)malloc(
                (size_t)comps[c].n * sizeof(*mbrs));
            if (!mbrs) { sh_fail(&st, "out of memory"); ok = false; break; }
            for (int j = 0; j < comps[c].n; j++)
                mbrs[j] = mem[comps[c].idx[j]].fn;
            qsort(mbrs, (size_t)comps[c].n, sizeof(*mbrs), sh_cmp_name_ref);
            for (int j = 0; j < comps[c].n && ok; j++) {
                LLVMValueRef fn = mbrs[j];
                /* 模块核心语义抽象与接口调用契约 */
                {
                    sh_refs_t refs;
                    memset(&refs, 0, sizeof(refs));
                    refs.st = &st;
                    refs.closure_seen = &closure_memo;
                    sh_map_init(&refs.decl_fns);
                    sh_map_init(&refs.decl_globs);
                    sh_map_init(&refs.travel);
                    sh_map_init(&refs.body_done);
                    ok = sh_refs_scan_fn(&refs, fn);
                    for (int i = 0; i <= refs.decl_fns.mask; i++)
                        if (refs.decl_fns.vals[i] > 0)
                            sh_map_put(&decl_fns, refs.decl_fns.keys[i], 1);
                    for (int i = 0; i <= refs.decl_globs.mask; i++)
                        if (refs.decl_globs.vals[i] > 0)
                            sh_map_put(&decl_globs, refs.decl_globs.keys[i], 1);
                    for (int i = 0; i <= refs.travel.mask; i++)
                        if (refs.travel.vals[i] > 0)
                            sh_map_put(&travel, refs.travel.keys[i], 1);
                    sh_map_free(&refs.decl_fns);
                    sh_map_free(&refs.decl_globs);
                    sh_map_free(&refs.travel);
                    sh_map_free(&refs.body_done);
                    if (!ok) break;
                }
                const char *fn_name = LLVMGetValueName(fn);
                const sh_body_slice_t *sl = sh_find_body_slice(&body_idx, fn_name);
                if (!sl) {
                    sh_fail(&st, "body '%s' missing from shard module text", fn_name);
                    ok = false;
                    break;
                }
                size_t body_start = bodies.n;
                if (!sh_append_body_slice(body_file, sl, &bodies)) {
                    sh_fail(&st, "cannot read body '%s' from shard module text", fn_name);
                    ok = false;
                    break;
                }
                /* 检查完整 define 头，无需克隆函数体 */
                char *header = bodies.p + body_start;
                char *hnl = (char *)memchr(header, '\n', sl->len);
                if (hnl) *hnl = '\0';
                bool has_comdat = strstr(header, " comdat") != NULL;
                if (hnl) *hnl = '\n';
                if (has_comdat) {
                    sh_fail(&st, "body '%s' carries comdat", fn_name);
                    ok = false;
                    break;
                }
                sh_sb_puts(&bodies, "\n");
            }
            free(mbrs);
        }

        SH_TRACE("shard %d: bodies done (%d bytes)\n", s, (int)bodies.n);
        /* 核心系统底层抽象与内存语义契约 */
        int ntrav = 0;
        for (int i = 0; i <= travel.mask; i++)
            if (travel.vals[i] > 0) ntrav++;
        if (ok && ntrav) {
            LLVMValueRef *tv = (LLVMValueRef *)malloc((size_t)ntrav * sizeof(*tv));
            if (!tv) { sh_fail(&st, "out of memory"); ok = false; }
            else {
                int t = 0;
                for (int i = 0; i <= travel.mask; i++)
                    if (travel.vals[i] > 0) tv[t++] = travel.keys[i];
                qsort(tv, (size_t)ntrav, sizeof(*tv), sh_cmp_name_ref);
                for (int t2 = 0; ok && t2 < ntrav; t2++) {
                    char *txt = LLVMPrintValueToString(tv[t2]);
                    if (!txt) { sh_fail(&st, "print global failed"); ok = false; break; }
                    if (strstr(txt, " comdat($") || strstr(txt, " = alias ")) {
                        sh_fail(&st, "global '%s' carries comdat/alias",
                                LLVMGetValueName(tv[t2]));
                        LLVMDisposeMessage(txt);
                        ok = false;
                        break;
                    }
                    sh_sb_puts(&gdecls, txt);
                    sh_sb_puts(&gdecls, "\n");
                    LLVMDisposeMessage(txt);
                }
                free(tv);
            }
        }
        int ngd = 0;
        for (int i = 0; i <= decl_globs.mask; i++)
            if (decl_globs.vals[i] > 0) ngd++;
        if (ok && ngd) {
            LLVMValueRef *gd = (LLVMValueRef *)malloc((size_t)ngd * sizeof(*gd));
            if (!gd) { sh_fail(&st, "out of memory"); ok = false; }
            else {
                int t = 0;
                for (int i = 0; i <= decl_globs.mask; i++)
                    if (decl_globs.vals[i] > 0) gd[t++] = decl_globs.keys[i];
                qsort(gd, (size_t)ngd, sizeof(*gd), sh_cmp_name_ref);
                for (int t2 = 0; t2 < ngd; t2++)
                    sh_emit_global_decl(&st, &gdecls, gd[t2]);
                free(gd);
            }
        }

        SH_TRACE("shard %d: globals done (%d bytes)\n", s, (int)gdecls.n);
        /* 底层系统交互与数据协议契约 */
        if (ok) {
            /* 合并跨分片引用的外部声明标记集合 */
            int nfd = 0;
            for (int i = 0; i <= decl_fns.mask; i++)
                if (decl_fns.vals[i] > 0 &&
                    sh_map_get(&local, decl_fns.keys[i]) < 0)
                    nfd++;
            if (nfd) {
                LLVMValueRef *fd = (LLVMValueRef *)malloc((size_t)nfd * sizeof(*fd));
                if (!fd) { sh_fail(&st, "out of memory"); ok = false; }
                else {
                    int t = 0;
                    for (int i = 0; i <= decl_fns.mask; i++)
                        if (decl_fns.vals[i] > 0 &&
                            sh_map_get(&local, decl_fns.keys[i]) < 0)
                            fd[t++] = decl_fns.keys[i];
                    qsort(fd, (size_t)nfd, sizeof(*fd), sh_cmp_name_ref);
                    for (int t2 = 0; t2 < nfd; t2++)
                        sh_emit_fn_decl(&gdecls, fd[t2]);
                    free(fd);
                }
            }
        }

        SH_TRACE("shard %d: declares done\n", s);
        /* 核心系统底层抽象与内存语义契约 */
        if (ok) {
            sh_harvest_types(&st, &types, bodies.p ? bodies.p : "");
            sh_harvest_types(&st, &types, gdecls.p ? gdecls.p : "");
        }

        /* 直接流式输出三段内容以降低内存驻留 */
        if (bodies.oom || gdecls.oom || types.oom) {
            sh_fail(&st, "out of memory assembling fragment");
            ok = false;
        }
        if (st.failed) ok = false;
        if (ok) {
            char frag_path[1200];
            snprintf(frag_path, sizeof(frag_path), "%s.shard%d.frag.ll", obj_base, s);
            FILE *ff = fopen(frag_path, "wb");
            if (ff) {
                bool wrote = (!types.n || fwrite(types.p, 1, types.n, ff) == types.n) &&
                             (!gdecls.n || fwrite(gdecls.p, 1, gdecls.n, ff) == gdecls.n) &&
                             (!bodies.n || fwrite(bodies.p, 1, bodies.n, ff) == bodies.n);
                if (fclose(ff)) wrote = false;
                if (!wrote) {
                    sh_fail(&st, "cannot write complete shard fragment '%s'", frag_path);
                    ok = false;
                }
            } else {
                sh_fail(&st, "cannot open shard fragment file '%s' for write (errno=%d)", frag_path, errno);
                ok = false;
            }
        } else if (!st.failed) {
            sh_fail(&st, "shard %d: assembling failed before write", s);
        }

        sh_map_free(&local);
        sh_map_free(&closure_memo);
        sh_map_free(&decl_fns); sh_map_free(&decl_globs); sh_map_free(&travel);
        sh_map_free(&st.type_done);
        st.type_done.keys = NULL; st.type_done.vals = NULL;
        free(bodies.p); free(gdecls.p); free(types.p);
    }

    sh_free_body_index(&body_idx);
    if (body_file) fclose(body_file);
    if (tmp_ll[0]) {
        remove(tmp_ll);
        tmp_ll[0] = '\0';
    }

    /* 第 2b 遍：从片段文件流式发射分片，限制峰值内存占用 */
    for (int s = 0; s < nshard && !st.failed; s++) {
        char frag_path[1200];
        snprintf(frag_path, sizeof(frag_path), "%s.shard%d.frag.ll", obj_base, s);
        char path[1200];
        snprintf(path, sizeof(path), "%s.shard%d.o", obj_base, s);
        char errbuf[256];
        char ptag[64];
        snprintf(ptag, sizeof(ptag), "shard %d: before emit", s);
        sh_probe_mem(ptag);
        if (sh_emit_one_file(&st, frag_path, tm, path, errbuf, sizeof(errbuf))) {
            objs[s] = (char *)malloc(strlen(path) + 1);
            if (objs[s]) strcpy(objs[s], path);
            else sh_fail(&st, "out of memory");
        } else {
            sh_fail(&st, "shard %d: %s", s, errbuf);
            remove(path);
        }
        remove(frag_path);
        snprintf(ptag, sizeof(ptag), "shard %d: after emit", s);
        sh_probe_mem(ptag);
    }

    if (st.failed) {
        for (int s = 0; s < nshard; s++) {
            char frag_path[1200];
            snprintf(frag_path, sizeof(frag_path), "%s.shard%d.frag.ll", obj_base, s);
            remove(frag_path);
        }
        /* 模块核心语义抽象与接口调用契约 */
        for (int i = 0; i < st.link_n; i++)
            LLVMSetLinkage(st.link_recs[i].glob ? st.link_recs[i].glob
                                               : st.link_recs[i].fn,
                           st.link_recs[i].old_linkage);
        if (objs)
            for (int s = 0; s < nshard; s++) {
                if (objs[s]) { remove(objs[s]); free(objs[s]); }
            }
        free(objs);
        fprintf(stderr, "shard: %s — falling back to single module\n",
                st.reason);
        goto cleanup_planning;
    }

    /* 符号迁移：重命名、外部声明并执行 RAUW 批量替换 */
    {
        bool moved_ok = true;
        int rename_counter = 0;
        /* 核心系统底层抽象与内存语义契约 */
        sh_move_rec_t *moves = (sh_move_rec_t *)calloc((size_t)nm,
                                                        sizeof(*moves));
        int nmoves = 0;
        for (int k = 0; k < nm && moved_ok; k++) {
            if (mem[k].comp < 0) continue;
            if (sh_map_get(&st.needs_decl, mem[k].fn) < 0) continue;
            LLVMValueRef orig = mem[k].fn;
            /* 复制 LLVMGetValueName 内部字符串以防重命名后内存失效 */
            char oname[512];
            {
                const char *n = LLVMGetValueName(orig);
                size_t nl = n ? strlen(n) : 0;
                if (!n || nl == 0 || nl >= sizeof(oname)) {
                    sh_fail(&st, "member name too long or empty");
                    moved_ok = false; break;
                }
                memcpy(oname, n, nl + 1);
            }
            char decltmp[512], origtmp[512];
            if (!sh_unique_fn_name(g, oname, ".zsa$", &rename_counter,
                                   decltmp, sizeof(decltmp)) ||
                !sh_unique_fn_name(g, oname, ".zsb$", &rename_counter,
                                   origtmp, sizeof(origtmp))) {
                moved_ok = false; break;
            }
            /* 内部辅助实现 */
            LLVMValueRef decl = LLVMAddFunction(
                g->mod, decltmp, LLVMGlobalGetValueType(orig));
            LLVMSetFunctionCallConv(decl, LLVMGetFunctionCallConv(orig));
            LLVMReplaceAllUsesWith(orig, decl);
            LLVMSetValueName2(orig, origtmp, strlen(origtmp));
            if (LLVMGetNamedFunction(g->mod, oname)) {
                sh_fail(&st, "clean name '%s' still occupied", oname);
                moved_ok = false; break;
            }
            LLVMSetValueName2(decl, oname, strlen(oname));
            moves[nmoves].orig = orig;
            moves[nmoves].decl = decl;
            moves[nmoves].origname = (char *)malloc(strlen(oname) + 1);
            if (moves[nmoves].origname) {
                strcpy(moves[nmoves].origname, oname);
                nmoves++;
            } else {
                moved_ok = false;
            }
        }
        if (moved_ok) {
            char *vmsg = NULL;
            if (LLVMVerifyModule(g->mod, LLVMReturnStatusAction, &vmsg)) {
                fprintf(stderr,
                        "shard: coordinator verify failed after move: %.160s\n",
                        vmsg ? vmsg : "?");
                moved_ok = false;
            }
            if (vmsg) LLVMDisposeMessage(vmsg);
        }
        if (!moved_ok) {
            /* 回滚：恢复函数使用点、原始符号名称与链接属性并删除声明 */
            for (int i = 0; i < nmoves; i++) {
                LLVMReplaceAllUsesWith(moves[i].decl, moves[i].orig);
                LLVMDeleteFunction(moves[i].decl); /* 核心系统底层抽象与内存语义契约 */
                LLVMSetValueName2(moves[i].orig, moves[i].origname,
                                  strlen(moves[i].origname));
                free(moves[i].origname);
            }
            for (int i = 0; i < st.link_n; i++)
                LLVMSetLinkage(st.link_recs[i].glob ? st.link_recs[i].glob
                                                   : st.link_recs[i].fn,
                               st.link_recs[i].old_linkage);
            free(moves);
            fprintf(stderr, "shard: move rollback (%s) — single module\n",
                    st.failed ? st.reason : "verify failed");
            rc = 0;
            for (int s = 0; s < nshard; s++) {
                if (objs[s]) { remove(objs[s]); free(objs[s]); }
            }
            free(objs);
            goto cleanup_planning;
        }
        /* 核心系统底层抽象与内存语义契约 */
        for (int k = 0; k < nm; k++) {
            if (mem[k].comp < 0) continue;
            LLVMValueRef orig = mem[k].fn;
            if (LLVMGetFirstUse(orig)) {
                if (sh_map_get(&st.needs_decl, orig) >= 0) {
                    /* 阶段 A 生成干净名称的声明供分片导出 */
                    const char *on = LLVMGetValueName(orig);
                    char clean[512];
                    const char *cut = strstr(on, ".zsb$");
                    if (!cut) cut = strstr(on, ".zsh$");
                    if (!cut) cut = strstr(on, ".zshx$");
                    if (!cut) cut = on + strlen(on);
                    size_t len = (size_t)(cut - on);
                    if (len >= sizeof(clean)) len = sizeof(clean) - 1;
                    memcpy(clean, on, len);
                    clean[len] = '\0';
                    LLVMValueRef decl = LLVMGetNamedFunction(g->mod, clean);
                    if (decl && decl != orig && LLVMIsDeclaration(decl)) {
                        LLVMReplaceAllUsesWith(orig, decl);
                        if (!LLVMGetFirstUse(orig)) {
                            LLVMDeleteFunction(orig);
                            continue;
                        }
                    }
                }
                SH_TRACE("keeping '%s' in coordinator (surviving refs)\n",
                         LLVMGetValueName(orig));
                continue;
            }
            LLVMDeleteFunction(orig);
        }
        free(moves);
        sh_probe_mem("after delete all bodies");

        char *vmsg = NULL;
            if (LLVMVerifyModule(g->mod, LLVMReturnStatusAction, &vmsg)) {
                /* 模块核心语义抽象与接口调用契约 */
                fprintf(stderr,
                        "error: shard: coordinator verify failed after delete: "
                        "%.160s\n", vmsg ? vmsg : "?");
                if (vmsg) LLVMDisposeMessage(vmsg);
                rc = -1;
            } else {
                if (sh_trace_on()) {
                    /* 校验所有迁移函数的引用点，防止链接期未定义符号 */
                    for (LLVMValueRef f = LLVMGetFirstFunction(g->mod); f;
                         f = LLVMGetNextFunction(f)) {
                        if (LLVMIsDeclaration(f)) continue;
                        for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(f);
                             bb; bb = LLVMGetNextBasicBlock(bb)) {
                            for (LLVMValueRef in = LLVMGetFirstInstruction(bb);
                                 in; in = LLVMGetNextInstruction(in)) {
                                unsigned nop = LLVMGetNumOperands(in);
                                for (unsigned k = 0; k < nop; k++) {
                                    LLVMValueRef op = LLVMGetOperand(in, (int)k);
                                    if (!op || !LLVMIsAFunction(op)) continue;
                                    const char *on = LLVMGetValueName(op);
                                    if (strstr(on, ".zsb$") || strstr(on, ".zsh$") ||
                                        strstr(on, ".zshx$"))
                                        SH_TRACE("post-delete ref: %s -> %s"
                                                 " [decl=%d lk=%d]\n",
                                                 LLVMGetValueName(f), on,
                                                 LLVMIsDeclaration(op),
                                                 (int)LLVMGetLinkage(op));
                                }
                            }
                        }
                    }
                    for (LLVMValueRef gv = LLVMGetFirstGlobal(g->mod); gv;
                         gv = LLVMGetNextGlobal(gv)) {
                        LLVMValueRef init = LLVMGetInitializer(gv);
                        if (!init) continue;
                        sh_trace_scan_const(gv, init, 0);
                    }
                }
            if (vmsg) LLVMDisposeMessage(vmsg);
            fprintf(stderr,
                    "shard: %d objects emitted, %d fns / %lld insns moved "
                    "(%.1f%% of module body)\n",
                    nshard, nm - dropped, mov_insns,
                    m->total_insns ? 100.0 * (double)mov_insns /
                                        (double)m->total_insns : 0.0);
            rc = nshard;
            *out_objs = objs;
            objs = NULL;
        }
    }

cleanup_planning:
    if (tm) LLVMDisposeTargetMachine(tm);
    for (int c = 0; c < ncomp; c++) free(comps[c].idx);
    free(comps);
    free(shard_of_comp);
    free(mem);
    sh_map_free(&mf2mem);
    free(st.link_recs);
    sh_map_free(&st.fn_v); sh_map_free(&st.glob_v);
    sh_map_free(&st.members_all);
    sh_map_free(&st.needs_decl);
    sh_map_free(&st.ext_globs);
    sh_map_free(&st.ext_fns);
    if (st.type_done.keys) sh_map_free(&st.type_done);
    return rc;
}
