/* embedres */

#include "embedres.h"
#include "win_utf8.h"
#include "miniz.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define fopen zan_utf8_fopen
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

typedef struct {
    char          *name; /* 核心系统底层抽象与内存语义契约 */
    unsigned char *data;
    long long      len;
} zan_embed_file_t;

typedef struct {
    zan_embed_file_t *v;
    int               n;
    int               cap;
} zan_embed_list_t;

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

/* 底层系统交互与数据协议契约 */
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

/* 内部辅助逻辑 */
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

static int embed_is_dir(const char *path) {
#ifdef _WIN32
    DWORD a = zan_utf8_get_file_attributes(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static const char *embed_basename(const char *path) {
    const char *fwd = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *sep = fwd > back ? fwd : back;
    return sep ? sep + 1 : path;
}

/* 内部辅助逻辑 */
#define ZAN_EMBED_COMPRESS_MIN 512
/* 内部辅助逻辑 */
#define ZAN_EMBED_COMPRESSED 0x8000000000000000ULL

/* 内部辅助逻辑 */
static unsigned char *embed_maybe_compress(const unsigned char *data,
                                           long long len, long long *out_len) {
    if (len < ZAN_EMBED_COMPRESS_MIN) return NULL;
    int flags = tdefl_create_comp_flags_from_zip_params(MZ_DEFAULT_LEVEL,
                                                        -MZ_DEFAULT_WINDOW_BITS,
                                                        0);
    size_t comp_len = 0;
    void *comp = tdefl_compress_mem_to_heap(data, (size_t)len, &comp_len,
                                            flags);
    if (!comp) return NULL;
    if ((long long)comp_len + 8 >= len) { free(comp); return NULL; }
    unsigned char *out = (unsigned char *)malloc((size_t)comp_len + 8);
    if (!out) { free(comp); return NULL; }
    uint32_t raw32 = (uint32_t)len, c32 = (uint32_t)comp_len;
    memcpy(out, &raw32, 4);
    memcpy(out + 4, &c32, 4);
    memcpy(out + 8, comp, comp_len);
    free(comp);
    *out_len = (long long)comp_len + 8;
    return out;
}

/* 内部辅助逻辑 */
static LLVMValueRef embed_bytes_global(zan_irgen_t *g, const char *label,
                                       const unsigned char *data,
                                       long long len) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    /* 内部辅助逻辑 */
    if (len < 0 || len > 0xFFFFFFFEULL) {
        fprintf(stderr,
                "error: embedded resource '%s' is too large (%lld bytes, "
                "max 4 GiB - 2)\n", label ? label : "?", len);
        return NULL;
    }
    LLVMTypeRef arr_ty = LLVMArrayType(i8, (unsigned)len + 1);
    LLVMValueRef init = LLVMConstStringInContext(g->ctx, (const char *)data,
                                                 (unsigned)len, 0);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, label);
    LLVMSetInitializer(gv, init);
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv, 1);
    LLVMValueRef zero = LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    LLVMValueRef idx[] = { zero, zero };
    return LLVMConstInBoundsGEP2(arr_ty, gv, idx, 2);
}

/* 核心系统底层抽象与内存语义契约 */
static LLVMValueRef embed_define(zan_irgen_t *g, const char *name,
                                 LLVMTypeRef fty) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, name);
    if (fn && LLVMCountBasicBlocks(fn) == 0) return fn;
    if (fn) return NULL;
    return LLVMAddFunction(g->mod, name, fty);
}

static LLVMValueRef embed_libc(zan_irgen_t *g, const char *name,
                               LLVMTypeRef ret, LLVMTypeRef *args, int nargs,
                               LLVMTypeRef *out_ty) {
    LLVMTypeRef ty = LLVMFunctionType(ret, args, (unsigned)nargs, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, name);
    if (!fn) fn = LLVMAddFunction(g->mod, name, ty);
    *out_ty = ty;
    return fn;
}

/* 底层系统交互与数据协议契约 */
struct embed_api_ctx {
    LLVMTypeRef  i8, i8p, i32, i64, ent_ty;
    LLVMValueRef gtbl, gcnt, gbuf, empty;
    LLVMValueRef own_tbl, own_cnt;  /* 核心系统底层抽象与内存语义契约 */
    long long    buf_cap;
};

/* 底层系统交互与数据协议契约 */
static LLVMValueRef embed_entry_at(LLVMBuilderRef b, struct embed_api_ctx *c,
                                   LLVMValueRef base, LLVMValueRef i) {
    return LLVMBuildGEP2(b, c->ent_ty, base, &i, 1, "ent");
}

/* i8* zan */
static LLVMValueRef embed_emit_find(zan_irgen_t *g, struct embed_api_ctx *c) {
    LLVMTypeRef args[] = { c->i8p };
    LLVMTypeRef fty = LLVMFunctionType(c->i8p, args, 1, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "zan.embed.find", fty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(g->ctx, fn, "head");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef hit = LLVMAppendBasicBlockInContext(g->ctx, fn, "hit");
    LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
    LLVMBasicBlockRef nextslot = LLVMAppendBasicBlockInContext(g->ctx, fn,
                                                               "nextslot");
    LLVMBasicBlockRef miss = LLVMAppendBasicBlockInContext(g->ctx, fn, "miss");

    LLVMTypeRef scmp_args[] = { c->i8p, c->i8p };
    LLVMTypeRef scmp_ty;
    LLVMValueRef strcmp_fn = embed_libc(g, "strcmp", c->i32, scmp_args, 2,
                                        &scmp_ty);

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef iv = LLVMBuildAlloca(b, c->i64, "i");
    LLVMValueRef bv = LLVMBuildAlloca(b, c->i8p, "b");
    LLVMValueRef nv = LLVMBuildAlloca(b, c->i64, "n");
    LLVMValueRef sv = LLVMBuildAlloca(b, c->i64, "slot");
    LLVMBuildStore(b, LLVMConstInt(c->i64, 0, 0), iv);
    /* 内部辅助逻辑 */
    LLVMBuildStore(b, LLVMBuildLoad2(b, c->i8p, c->gtbl, "tbl0"), bv);
    LLVMBuildStore(b, LLVMBuildLoad2(b, c->i64, c->gcnt, "cnt0"), nv);
    LLVMBuildStore(b, LLVMConstInt(c->i64, 0, 0), sv);
    LLVMValueRef name = LLVMGetParam(fn, 0);
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, name, "nullname"), miss, head);

    LLVMPositionBuilderAtEnd(b, head);
    LLVMValueRef i = LLVMBuildLoad2(b, c->i64, iv, "iv");
    LLVMValueRef n = LLVMBuildLoad2(b, c->i64, nv, "ncur");
    LLVMValueRef base = LLVMBuildLoad2(b, c->i8p, bv, "bcur");
    LLVMValueRef ok = LLVMBuildAnd(b,
        LLVMBuildICmp(b, LLVMIntSLT, i, n, "more"),
        LLVMBuildIsNotNull(b, base, "hastbl"), "go");
    LLVMBuildCondBr(b, ok, body, nextslot);

    LLVMPositionBuilderAtEnd(b, body);
    i = LLVMBuildLoad2(b, c->i64, iv, "iv2");
    LLVMValueRef ent = embed_entry_at(b, c, base, i);
    LLVMValueRef nmp = LLVMBuildStructGEP2(b, c->ent_ty, ent, 0, "nmp");
    LLVMValueRef nm = LLVMBuildLoad2(b, c->i8p, nmp, "nm");
    LLVMBasicBlockRef cmp = LLVMAppendBasicBlockInContext(g->ctx, fn, "cmp");
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, nm, "nonm"), next, cmp);

    LLVMPositionBuilderAtEnd(b, cmp);
    LLVMValueRef cargs[] = { nm, name };
    LLVMValueRef eq = LLVMBuildCall2(b, scmp_ty, strcmp_fn, cargs, 2, "eq");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntEQ, eq,
        LLVMConstInt(c->i32, 0, 0), "iseq"), hit, next);

    LLVMPositionBuilderAtEnd(b, hit);
    LLVMBuildRet(b, ent);

    LLVMPositionBuilderAtEnd(b, next);
    i = LLVMBuildLoad2(b, c->i64, iv, "iv3");
    LLVMBuildStore(b, LLVMBuildAdd(b, i, LLVMConstInt(c->i64, 1, 0), "i1"), iv);
    LLVMBuildBr(b, head);

    /* 底层系统交互与数据协议契约 */
    LLVMPositionBuilderAtEnd(b, nextslot);
    LLVMValueRef s = LLVMBuildLoad2(b, c->i64, sv, "scur");
    LLVMBasicBlockRef adv = LLVMAppendBasicBlockInContext(g->ctx, fn, "adv");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntEQ, s,
        LLVMConstInt(c->i64, 0, 0), "was0"), adv, miss);
    LLVMPositionBuilderAtEnd(b, adv);
    LLVMBuildStore(b, LLVMConstInt(c->i64, 1, 0), sv);
    LLVMBuildStore(b, LLVMConstInt(c->i64, 0, 0), iv);
    LLVMBuildStore(b, c->own_tbl, bv);
    LLVMBuildStore(b, c->own_cnt, nv);
    LLVMBuildBr(b, head);

    LLVMPositionBuilderAtEnd(b, miss);
    LLVMBuildRet(b, LLVMConstNull(c->i8p));
    LLVMDisposeBuilder(b);
    return fn;
}

/* i8* zan */
static LLVMValueRef embed_emit_unzip(zan_irgen_t *g, struct embed_api_ctx *c) {
    LLVMTypeRef args[] = { c->i8p };
    LLVMTypeRef fty = LLVMFunctionType(c->i8p, args, 1, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "zan.embed.unzip", fty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef dargs[] = { c->i8p, c->i64 };
    LLVMTypeRef dty;
    LLVMValueRef dec = embed_libc(g, "zan_embed_decode", c->i8p, dargs, 2,
                                  &dty);
    LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef raw = LLVMAppendBasicBlockInContext(g->ctx, fn, "raw");
    LLVMBasicBlockRef comp = LLVMAppendBasicBlockInContext(g->ctx, fn, "comp");
    LLVMBasicBlockRef ok = LLVMAppendBasicBlockInContext(g->ctx, fn, "ok");
    LLVMBasicBlockRef bad = LLVMAppendBasicBlockInContext(g->ctx, fn, "bad");
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef e = LLVMGetParam(fn, 0);
    LLVMValueRef lp = LLVMBuildStructGEP2(b, c->ent_ty, e, 2, "lp");
    LLVMValueRef len = LLVMBuildLoad2(b, c->i64, lp, "l");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSLT, len,
        LLVMConstInt(c->i64, 0, 0), "flag"), comp, raw);
    LLVMPositionBuilderAtEnd(b, raw);
    LLVMValueRef dp0 = LLVMBuildStructGEP2(b, c->ent_ty, e, 1, "dp0");
    LLVMBuildRet(b, LLVMBuildLoad2(b, c->i8p, dp0, "d0"));
    LLVMPositionBuilderAtEnd(b, comp);
    LLVMValueRef dp = LLVMBuildStructGEP2(b, c->ent_ty, e, 1, "dp");
    LLVMValueRef d = LLVMBuildLoad2(b, c->i8p, dp, "d");
    /* 内部辅助逻辑 */
    LLVMValueRef plain = LLVMBuildOr(b,
        LLVMBuildAnd(b, len,
            LLVMConstInt(c->i64, 0x7FFFFFFFFFFFFFFFULL, 0), "pl0"),
        LLVMConstInt(c->i64, 0x4000000000000000ULL, 0), "plain");
    LLVMValueRef cargs[] = { d, plain };
    LLVMValueRef out = LLVMBuildCall2(b, dty, dec, cargs, 2, "out");
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, out, "fail"), bad, ok);
    LLVMPositionBuilderAtEnd(b, bad);
    LLVMBuildRet(b, LLVMConstNull(c->i8p));
    LLVMPositionBuilderAtEnd(b, ok);
    /* 内部辅助逻辑 */
    LLVMValueRef rl = LLVMBuildLoad2(b, c->i32, d, "rawlen");
    LLVMValueRef rl64 = LLVMBuildZExt(b, rl, c->i64, "raw64");
    LLVMBuildStore(b, out, dp);
    LLVMBuildStore(b, rl64, lp);
    LLVMBuildRet(b, out);
    LLVMDisposeBuilder(b);
    return fn;
}

/* i8* zan */
static LLVMValueRef embed_emit_passthrough(zan_irgen_t *g,
                                           struct embed_api_ctx *c) {
    LLVMTypeRef args[] = { c->i8p };
    LLVMTypeRef fty = LLVMFunctionType(c->i8p, args, 1, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "zan.embed.pass", fty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef e = LLVMGetParam(fn, 0);
    LLVMValueRef dp = LLVMBuildStructGEP2(b, c->ent_ty, e, 1, "dp");
    LLVMBuildRet(b, LLVMBuildLoad2(b, c->i8p, dp, "d"));
    LLVMDisposeBuilder(b);
    return fn;
}

static void embed_emit_read_has_bytes(zan_irgen_t *g, struct embed_api_ctx *c,
                                      LLVMValueRef find, LLVMValueRef unzip) {
    LLVMTypeRef find_args[] = { c->i8p };
    LLVMTypeRef find_ty = LLVMFunctionType(c->i8p, find_args, 1, 0);
    LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);

    /* 底层系统交互与数据协议契约 */
    LLVMTypeRef rty = LLVMFunctionType(c->i8p, find_args, 1, 0);
    LLVMValueRef rfn = embed_define(g, "zan_embed_read", rty);
    if (!rfn) { LLVMDisposeBuilder(b); return; }
    LLVMBasicBlockRef rb = LLVMAppendBasicBlockInContext(g->ctx, rfn, "entry");
    LLVMBasicBlockRef rgot = LLVMAppendBasicBlockInContext(g->ctx, rfn, "got");
    LLVMBasicBlockRef rnil = LLVMAppendBasicBlockInContext(g->ctx, rfn, "nil");
    LLVMPositionBuilderAtEnd(b, rb);
    LLVMValueRef ra = LLVMGetParam(rfn, 0);
    LLVMValueRef re = LLVMBuildCall2(b, find_ty, find, &ra, 1, "e");
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, re, "none"), rnil, rgot);
    LLVMPositionBuilderAtEnd(b, rgot);
    LLVMValueRef rd = LLVMBuildCall2(b, find_ty, unzip, &re, 1, "d");
    LLVMBasicBlockRef rgot2 = LLVMAppendBasicBlockInContext(g->ctx, rfn, "dec");
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, rd, "undec"), rnil, rgot2);
    LLVMPositionBuilderAtEnd(b, rgot2);
    LLVMBuildRet(b, rd);
    LLVMPositionBuilderAtEnd(b, rnil);
    LLVMBuildRet(b, c->empty);

    /* 底层系统交互与数据协议契约 */
    LLVMTypeRef hty = LLVMFunctionType(c->i32, find_args, 1, 0);
    LLVMValueRef hfn = embed_define(g, "zan_embed_has", hty);
    if (!hfn) { LLVMDisposeBuilder(b); return; }
    LLVMBasicBlockRef hb = LLVMAppendBasicBlockInContext(g->ctx, hfn, "entry");
    LLVMPositionBuilderAtEnd(b, hb);
    LLVMValueRef ha = LLVMGetParam(hfn, 0);
    LLVMValueRef he = LLVMBuildCall2(b, find_ty, find, &ha, 1, "e");
    LLVMValueRef hv = LLVMBuildIsNotNull(b, he, "hit");
    LLVMBuildRet(b, LLVMBuildZExt(b, hv, c->i32, "hi"));

    /* 底层系统交互与数据协议契约 */
    LLVMTypeRef bargs[] = { c->i8p, c->i8p };
    LLVMTypeRef bty = LLVMFunctionType(c->i8p, bargs, 2, 0);
    LLVMValueRef bfn = embed_define(g, "zan_embed_bytes", bty);
    if (!bfn) { LLVMDisposeBuilder(b); return; }
    /* 底层系统交互与数据协议契约 */
    LLVMTypeRef brt = LLVMGetReturnType(LLVMGlobalGetValueType(bfn));
    LLVMBasicBlockRef bb0 = LLVMAppendBasicBlockInContext(g->ctx, bfn, "entry");
    LLVMBasicBlockRef bgot = LLVMAppendBasicBlockInContext(g->ctx, bfn, "got");
    LLVMBasicBlockRef bnil = LLVMAppendBasicBlockInContext(g->ctx, bfn, "nil");
    LLVMBasicBlockRef bsg = LLVMAppendBasicBlockInContext(g->ctx, bfn, "stgot");
    LLVMBasicBlockRef bsn = LLVMAppendBasicBlockInContext(g->ctx, bfn, "stnil");
    LLVMBasicBlockRef bdg = LLVMAppendBasicBlockInContext(g->ctx, bfn, "dogot");
    LLVMBasicBlockRef bdn = LLVMAppendBasicBlockInContext(g->ctx, bfn, "donil");
    LLVMPositionBuilderAtEnd(b, bb0);
    LLVMValueRef ba = LLVMGetParam(bfn, 0);
    LLVMValueRef bo = LLVMGetParam(bfn, 1);
    LLVMValueRef be = LLVMBuildCall2(b, find_ty, find, &ba, 1, "e");
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, be, "none"), bnil, bgot);
    LLVMPositionBuilderAtEnd(b, bgot);
    LLVMValueRef bdu = LLVMBuildCall2(b, find_ty, unzip, &be, 1, "d");
    LLVMBasicBlockRef bdec = LLVMAppendBasicBlockInContext(g->ctx, bfn, "dec");
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, bdu, "undec"), bnil, bdec);
    LLVMPositionBuilderAtEnd(b, bdec);
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, bo, "noout"), bdg, bsg);
    LLVMPositionBuilderAtEnd(b, bsg);
    LLVMValueRef blp = LLVMBuildStructGEP2(b, c->ent_ty, be, 2, "lp");
    LLVMValueRef bl = LLVMBuildLoad2(b, c->i64, blp, "l");
    LLVMBuildStore(b, LLVMBuildTrunc(b, bl, c->i32, "l32"), bo);
    LLVMBuildBr(b, bdg);
    LLVMPositionBuilderAtEnd(b, bdg);
    LLVMValueRef bdp = LLVMBuildStructGEP2(b, c->ent_ty, be, 1, "dp");
    LLVMValueRef bd = LLVMBuildLoad2(b, c->i8p, bdp, "d");
    if (brt != c->i8p) bd = LLVMBuildPtrToInt(b, bd, brt, "d.n");
    LLVMBuildRet(b, bd);
    LLVMPositionBuilderAtEnd(b, bnil);
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, bo, "noout2"), bdn, bsn);
    LLVMPositionBuilderAtEnd(b, bsn);
    LLVMBuildStore(b, LLVMConstInt(c->i32, 0, 0), bo);
    LLVMBuildBr(b, bdn);
    LLVMPositionBuilderAtEnd(b, bdn);
    LLVMBuildRet(b, brt == c->i8p ? LLVMConstNull(c->i8p)
                                  : LLVMConstInt(brt, 0, 0));

    LLVMDisposeBuilder(b);
}

/* 内部辅助逻辑 */
static void embed_emit_list(zan_irgen_t *g, struct embed_api_ctx *c) {
    LLVMTypeRef args[] = { c->i8p };
    LLVMTypeRef fty = LLVMFunctionType(c->i8p, args, 1, 0);
    LLVMValueRef fn = embed_define(g, "zan_embed_list", fty);
    if (!fn) return;
    LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);

    LLVMTypeRef slen_args[] = { c->i8p };
    LLVMTypeRef slen_ty;
    LLVMValueRef strlen_fn = embed_libc(g, "strlen", c->i64, slen_args, 1,
                                        &slen_ty);
    LLVMTypeRef sncmp_args[] = { c->i8p, c->i8p, c->i64 };
    LLVMTypeRef sncmp_ty;
    LLVMValueRef strncmp_fn = embed_libc(g, "strncmp", c->i32, sncmp_args, 3,
                                         &sncmp_ty);
    LLVMTypeRef scmp_args[] = { c->i8p, c->i8p };
    LLVMTypeRef scmp_ty;
    LLVMValueRef strcmp_fn = embed_libc(g, "strcmp", c->i32, scmp_args, 2,
                                        &scmp_ty);
    LLVMTypeRef mcpy_args[] = { c->i8p, c->i8p, c->i64 };
    LLVMTypeRef mcpy_ty;
    LLVMValueRef memcpy_fn = embed_libc(g, "memcpy", c->i8p, mcpy_args, 3,
                                        &mcpy_ty);
    LLVMTypeRef mal_args[] = { c->i64 };
    LLVMTypeRef mal_ty;
    LLVMValueRef malloc_fn = embed_libc(g, "malloc", c->i8p, mal_args, 1,
                                        &mal_ty);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef alloc = LLVMAppendBasicBlockInContext(g->ctx, fn, "alloc");
    LLVMBasicBlockRef oom = LLVMAppendBasicBlockInContext(g->ctx, fn, "oom");
    LLVMBasicBlockRef ready = LLVMAppendBasicBlockInContext(g->ctx, fn, "ready");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(g->ctx, fn, "head");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef take = LLVMAppendBasicBlockInContext(g->ctx, fn, "take");
    LLVMBasicBlockRef take2 = LLVMAppendBasicBlockInContext(g->ctx, fn, "take2");
    LLVMBasicBlockRef dup = LLVMAppendBasicBlockInContext(g->ctx, fn, "dup");
    LLVMBasicBlockRef dhead = LLVMAppendBasicBlockInContext(g->ctx, fn, "dhead");
    LLVMBasicBlockRef dbody = LLVMAppendBasicBlockInContext(g->ctx, fn, "dbody");
    LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
    LLVMBasicBlockRef nextslot = LLVMAppendBasicBlockInContext(g->ctx, fn,
                                                               "nextslot");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef prefix = LLVMGetParam(fn, 0);
    LLVMValueRef ia = LLVMBuildAlloca(b, c->i64, "i");
    LLVMValueRef oa = LLVMBuildAlloca(b, c->i64, "o");
    LLVMValueRef bva = LLVMBuildAlloca(b, c->i8p, "b");
    LLVMValueRef nva = LLVMBuildAlloca(b, c->i64, "n");
    LLVMValueRef sva = LLVMBuildAlloca(b, c->i64, "slot");
    LLVMValueRef ja = LLVMBuildAlloca(b, c->i64, "j");
    LLVMBuildStore(b, LLVMConstInt(c->i64, 0, 0), ia);
    LLVMBuildStore(b, LLVMConstInt(c->i64, 0, 0), oa);
    LLVMBuildStore(b, LLVMBuildLoad2(b, c->i8p, c->gtbl, "tbl0"), bva);
    LLVMBuildStore(b, LLVMBuildLoad2(b, c->i64, c->gcnt, "cnt0"), nva);
    LLVMBuildStore(b, LLVMConstInt(c->i64, 0, 0), sva);
    LLVMValueRef buf0 = LLVMBuildLoad2(b, c->i8p, c->gbuf, "buf0");
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, buf0, "nobuf"), alloc, ready);

    LLVMPositionBuilderAtEnd(b, alloc);
    LLVMValueRef cap = LLVMConstInt(c->i64, (unsigned long long)c->buf_cap, 0);
    LLVMValueRef nb = LLVMBuildCall2(b, mal_ty, malloc_fn, &cap, 1, "nb");
    LLVMBuildStore(b, nb, c->gbuf);
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, nb, "failed"), oom, ready);

    LLVMPositionBuilderAtEnd(b, oom);
    LLVMBuildRet(b, c->empty);

    LLVMPositionBuilderAtEnd(b, ready);
    LLVMValueRef buf = LLVMBuildLoad2(b, c->i8p, c->gbuf, "buf");
    LLVMValueRef plen = LLVMBuildAlloca(b, c->i64, "pl");
    LLVMBasicBlockRef plz = LLVMAppendBasicBlockInContext(g->ctx, fn, "plz");
    LLVMBasicBlockRef pls = LLVMAppendBasicBlockInContext(g->ctx, fn, "pls");
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, prefix, "nopre"), plz, pls);
    LLVMPositionBuilderAtEnd(b, plz);
    LLVMBuildStore(b, LLVMConstInt(c->i64, 0, 0), plen);
    LLVMBuildBr(b, head);
    LLVMPositionBuilderAtEnd(b, pls);
    LLVMBuildStore(b, LLVMBuildCall2(b, slen_ty, strlen_fn, &prefix, 1, "pl0"),
                   plen);
    LLVMBuildBr(b, head);

    LLVMPositionBuilderAtEnd(b, head);
    LLVMValueRef i = LLVMBuildLoad2(b, c->i64, ia, "iv");
    LLVMValueRef n = LLVMBuildLoad2(b, c->i64, nva, "ncur");
    LLVMValueRef base = LLVMBuildLoad2(b, c->i8p, bva, "bcur");
    LLVMValueRef more = LLVMBuildICmp(b, LLVMIntSLT, i, n, "more");
    LLVMValueRef ok = LLVMBuildAnd(b, more,
        LLVMBuildIsNotNull(b, base, "hastbl"), "go");
    LLVMBuildCondBr(b, ok, body, nextslot);

    LLVMPositionBuilderAtEnd(b, body);
    i = LLVMBuildLoad2(b, c->i64, ia, "iv2");
    LLVMValueRef ent = embed_entry_at(b, c, base, i);
    LLVMValueRef nmp = LLVMBuildStructGEP2(b, c->ent_ty, ent, 0, "nmp");
    LLVMValueRef nm = LLVMBuildLoad2(b, c->i8p, nmp, "nm");
    LLVMValueRef pl = LLVMBuildLoad2(b, c->i64, plen, "plv");
    LLVMValueRef nmnull = LLVMBuildIsNull(b, nm, "nonm");
    LLVMBasicBlockRef pfx = LLVMAppendBasicBlockInContext(g->ctx, fn, "pfx");
    LLVMBuildCondBr(b, nmnull, next, pfx);

    LLVMPositionBuilderAtEnd(b, pfx);
    LLVMValueRef pargs[] = { nm, prefix, pl };
    LLVMBasicBlockRef pcmp = LLVMAppendBasicBlockInContext(g->ctx, fn, "pcmp");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntEQ, pl,
        LLVMConstInt(c->i64, 0, 0), "nopl"), take, pcmp);
    LLVMPositionBuilderAtEnd(b, pcmp);
    LLVMValueRef pc = LLVMBuildCall2(b, sncmp_ty, strncmp_fn, pargs, 3, "pc");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntEQ, pc,
        LLVMConstInt(c->i32, 0, 0), "pmatch"), take, next);

    LLVMPositionBuilderAtEnd(b, take);
    /* 内部辅助逻辑 */
    LLVMValueRef s0 = LLVMBuildLoad2(b, c->i64, sva, "scur");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntEQ, s0,
        LLVMConstInt(c->i64, 0, 0), "is0"), take2, dup);

    LLVMPositionBuilderAtEnd(b, dup);
    LLVMBuildStore(b, LLVMConstInt(c->i64, 0, 0), ja);
    LLVMBuildBr(b, dhead);

    LLVMPositionBuilderAtEnd(b, dhead);
    LLVMValueRef j = LLVMBuildLoad2(b, c->i64, ja, "jv");
    LLVMValueRef rn = LLVMBuildLoad2(b, c->i64, c->gcnt, "rcnt");
    LLVMValueRef rtbl = LLVMBuildLoad2(b, c->i8p, c->gtbl, "rtbl");
    LLVMValueRef dok = LLVMBuildAnd(b,
        LLVMBuildICmp(b, LLVMIntSLT, j, rn, "jmore"),
        LLVMBuildIsNotNull(b, rtbl, "rhastbl"), "rgo");
    LLVMBuildCondBr(b, dok, dbody, take2);

    LLVMPositionBuilderAtEnd(b, dbody);
    j = LLVMBuildLoad2(b, c->i64, ja, "jv2");
    LLVMValueRef rent = embed_entry_at(b, c, rtbl, j);
    LLVMValueRef rnmp = LLVMBuildStructGEP2(b, c->ent_ty, rent, 0, "rnmp");
    LLVMValueRef rnm = LLVMBuildLoad2(b, c->i8p, rnmp, "rnm");
    LLVMValueRef req = LLVMBuildCall2(b, scmp_ty, strcmp_fn,
        (LLVMValueRef[]){ rnm, nm }, 2, "req");
    LLVMBasicBlockRef rmatch = LLVMAppendBasicBlockInContext(g->ctx, fn,
                                                             "rmatch");
    LLVMBuildCondBr(b, LLVMBuildAnd(b,
        LLVMBuildIsNotNull(b, rnm, "rnonull"),
        LLVMBuildICmp(b, LLVMIntEQ, req, LLVMConstInt(c->i32, 0, 0), "ris"),
        "rdup"), next, rmatch);
    LLVMPositionBuilderAtEnd(b, rmatch);
    LLVMValueRef j2 = LLVMBuildLoad2(b, c->i64, ja, "jv3");
    LLVMBuildStore(b, LLVMBuildAdd(b, j2, LLVMConstInt(c->i64, 1, 0), "j1"),
                   ja);
    LLVMBuildBr(b, dhead);

    LLVMPositionBuilderAtEnd(b, take2);
    LLVMValueRef l = LLVMBuildCall2(b, slen_ty, strlen_fn,
        (LLVMValueRef[]){ nm }, 1, "l");

    LLVMValueRef o = LLVMBuildLoad2(b, c->i64, oa, "o");
    LLVMValueRef need = LLVMBuildAdd(b, LLVMBuildAdd(b, o, l, "ol"),
        LLVMConstInt(c->i64, 2, 0), "need");
    LLVMBasicBlockRef fits = LLVMAppendBasicBlockInContext(g->ctx, fn, "fits");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSGT, need,
        LLVMConstInt(c->i64, (unsigned long long)c->buf_cap, 0), "over"),
        done, fits);
    LLVMPositionBuilderAtEnd(b, fits);
    LLVMValueRef dst = LLVMBuildGEP2(b, c->i8, buf, &o, 1, "dst");
    LLVMValueRef margs[] = { dst, nm, l };
    LLVMBuildCall2(b, mcpy_ty, memcpy_fn, margs, 3, "");
    LLVMValueRef o2 = LLVMBuildAdd(b, o, l, "o2");
    LLVMValueRef nlp = LLVMBuildGEP2(b, c->i8, buf, &o2, 1, "nlp");
    LLVMBuildStore(b, LLVMConstInt(c->i8, 10, 0), nlp);
    LLVMBuildStore(b, LLVMBuildAdd(b, o2, LLVMConstInt(c->i64, 1, 0), "o3"), oa);
    LLVMBuildBr(b, next);

    LLVMPositionBuilderAtEnd(b, next);
    i = LLVMBuildLoad2(b, c->i64, ia, "iv3");
    LLVMBuildStore(b, LLVMBuildAdd(b, i, LLVMConstInt(c->i64, 1, 0), "i1"), ia);
    LLVMBuildBr(b, head);

    /* 底层系统交互与数据协议契约 */
    LLVMPositionBuilderAtEnd(b, nextslot);
    LLVMValueRef s = LLVMBuildLoad2(b, c->i64, sva, "sscur");
    LLVMBasicBlockRef adv = LLVMAppendBasicBlockInContext(g->ctx, fn, "adv");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntEQ, s,
        LLVMConstInt(c->i64, 0, 0), "was0"), adv, done);
    LLVMPositionBuilderAtEnd(b, adv);
    LLVMBuildStore(b, LLVMConstInt(c->i64, 1, 0), sva);
    LLVMBuildStore(b, LLVMConstInt(c->i64, 0, 0), ia);
    LLVMBuildStore(b, c->own_tbl, bva);
    LLVMBuildStore(b, c->own_cnt, nva);
    LLVMBuildBr(b, head);

    LLVMPositionBuilderAtEnd(b, done);
    LLVMValueRef oend = LLVMBuildLoad2(b, c->i64, oa, "oend");
    LLVMValueRef endp = LLVMBuildGEP2(b, c->i8, buf, &oend, 1, "endp");
    LLVMBuildStore(b, LLVMConstInt(c->i8, 0, 0), endp);
    LLVMBuildRet(b, buf);
    LLVMDisposeBuilder(b);
}

/* 内部辅助逻辑 */
static void embed_emit_register(zan_irgen_t *g, struct embed_api_ctx *c) {
    LLVMTypeRef args[] = { c->i8p, c->i64 };
    LLVMTypeRef fty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), args, 2, 0);
    LLVMValueRef fn = embed_define(g, "zan_embed_register", fty);
    if (!fn) return;
    LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef same = LLVMAppendBasicBlockInContext(g->ctx, fn, "same");
    LLVMBasicBlockRef store = LLVMAppendBasicBlockInContext(g->ctx, fn, "store");
    LLVMPositionBuilderAtEnd(b, bb);
    LLVMValueRef cur = LLVMBuildLoad2(b, c->i8p, c->gtbl, "cur");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntEQ, cur, LLVMGetParam(fn, 0),
        "issame"), same, store);
    LLVMPositionBuilderAtEnd(b, same);
    LLVMBuildRetVoid(b);
    LLVMPositionBuilderAtEnd(b, store);
    LLVMBuildStore(b, LLVMGetParam(fn, 0), c->gtbl);
    LLVMBuildStore(b, LLVMGetParam(fn, 1), c->gcnt);
    LLVMBuildRetVoid(b);
    LLVMDisposeBuilder(b);
}

int zan_embed_driver_spec(const char *path, const char *file, char *out,
                         size_t out_sz) {
    long long len = 0;
    unsigned char *data = embed_read_file(path, &len);
    if (!data) return -1;
    /* 内部辅助逻辑 */
    unsigned long long h = 1469598103934665603ULL;
    for (long long i = 0; i < len; i++) {
        h ^= (unsigned long long)data[i];
        h *= 1099511628211ULL;
    }
    free(data);
    int n = snprintf(out, out_sz, "%s=%s/%016llx/%s", path,
                     ZAN_EMBED_DRIVER_PREFIX, h, file);
    return (n < 0 || (size_t)n >= out_sz) ? -1 : 0;
}

/* 内部辅助逻辑 */
int zan_embed_emit_specs_filtered(zan_irgen_t *g, const char *const *specs,
                                  int count, const char *const *filter,
                                  int filter_count);

int zan_embed_emit_specs(zan_irgen_t *g, const char *const *specs, int count) {
    return zan_embed_emit_specs_filtered(g, specs, count, NULL, 0);
}

int zan_embed_emit_specs_filtered(zan_irgen_t *g, const char *const *specs,
                                  int count, const char *const *filter,
                                  int filter_count) {
    zan_embed_list_t files;
    memset(&files, 0, sizeof(files));

    for (int i = 0; i < count; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s", specs[i]);
        char *eq = strrchr(path, '=');
        const char *prefix = NULL;
        if (eq) { *eq = 0; prefix = eq + 1; }
        if (!prefix || !prefix[0]) prefix = embed_basename(path);
#ifdef _WIN32
        for (char *p = path; *p; p++) {
            if (*p == '/') *p = '\\';
        }
#endif
        int before = files.n;
        /* 底层系统交互与数据协议契约 */
        int filtering = filter != NULL && filter_count > 0
                        && embed_is_dir(path)
                        && prefix != NULL && strcmp(prefix, "skins") == 0;
        if (filtering) embed_walk_filtered(&files, path, prefix,
                                           filter, filter_count);
        else if (embed_is_dir(path)) embed_walk(&files, path, prefix);
        else embed_add_file(&files, path, prefix);
        if (files.n == before) {
            fprintf(stderr, "error: --embed '%s' matched no readable file\n",
                    specs[i]);
            for (int f = 0; f < files.n; f++) {
                free(files.v[f].name);
                free(files.v[f].data);
            }
            free(files.v);
            return -1;
        }
    }
    /* 内部辅助逻辑 */
    unsigned char *compressed = (unsigned char *)calloc(
        (size_t)(files.n > 0 ? files.n : 1), 1);
    if (!compressed) {
        for (int f = 0; f < files.n; f++) {
            free(files.v[f].name);
            free(files.v[f].data);
        }
        free(files.v);
        return -1;
    }
    int any_compressed = 0;
    for (int i = 0; i < files.n; i++) {
        long long clen = 0;
        unsigned char *cbuf = embed_maybe_compress(files.v[i].data,
                                                   files.v[i].len, &clen);
        if (!cbuf) continue;
        free(files.v[i].data);
        files.v[i].data = cbuf;
        files.v[i].len = clen;
        compressed[i] = 1;
        any_compressed = 1;
    }
    /* 核心系统底层抽象与内存语义契约 */
    if (any_compressed) g->uses_inflate = true;
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fields[] = { i8p, i8p, i64 };
    LLVMTypeRef ent_ty = LLVMStructTypeInContext(g->ctx, fields, 3, 0);

    LLVMValueRef *ents = (LLVMValueRef *)malloc(
        (size_t)(files.n + 1) * sizeof(LLVMValueRef));
    if (!ents) return -1;
    long long total_names = 0;
    for (int i = 0; i < files.n; i++) {
        total_names += (long long)strlen(files.v[i].name) + 1;
        char label[64];
        snprintf(label, sizeof(label), "zan.embed.n%d", i);
        LLVMValueRef name = embed_bytes_global(g, label,
            (const unsigned char *)files.v[i].name,
            (long long)strlen(files.v[i].name));
        snprintf(label, sizeof(label), "zan.embed.d%d", i);
        LLVMValueRef data = embed_bytes_global(g, label, files.v[i].data,
                                               files.v[i].len);
        if (!name || !data) {
            /* 内部辅助逻辑 */
            free(ents);
            free(compressed);
            for (int f = 0; f < files.n; f++) {
                free(files.v[f].name);
                free(files.v[f].data);
            }
            free(files.v);
            return -1;
        }
        unsigned long long lval = (unsigned long long)files.v[i].len;
        if (compressed[i]) lval |= ZAN_EMBED_COMPRESSED;
        LLVMValueRef vals[] = { name, data, LLVMConstInt(i64, lval, 0) };
        ents[i] = LLVMConstNamedStruct(ent_ty, vals, 3);
    }
    LLVMValueRef nulls[] = { LLVMConstNull(i8p), LLVMConstNull(i8p),
                             LLVMConstInt(i64, 0, 0) };
    ents[files.n] = LLVMConstNamedStruct(ent_ty, nulls, 3);

    /* 内部辅助逻辑 */
    LLVMValueRef tbl0 = LLVMConstNull(i8p);
    if (files.n > 0) {
        LLVMTypeRef tbl_ty = LLVMArrayType(ent_ty, (unsigned)files.n + 1);
        LLVMValueRef tbl = LLVMAddGlobal(g->mod, tbl_ty, "zan.embed.tbl");
        LLVMSetInitializer(tbl,
            LLVMConstArray(ent_ty, ents, (unsigned)files.n + 1));
        LLVMSetLinkage(tbl, LLVMPrivateLinkage);
        /* 核心系统底层抽象与内存语义契约 */
        LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
        LLVMValueRef idx[] = { zero, zero };
        tbl0 = LLVMConstInBoundsGEP2(tbl_ty, tbl, idx, 2);
    }
    free(ents);

    struct embed_api_ctx c;
    c.i8 = LLVMInt8TypeInContext(g->ctx);
    c.i8p = i8p;
    c.i32 = LLVMInt32TypeInContext(g->ctx);
    c.i64 = i64;
    c.ent_ty = ent_ty;
    /* 内部辅助逻辑 */
    c.buf_cap = total_names + 4096;
    c.gtbl = LLVMAddGlobal(g->mod, i8p, "zan.embed.gtbl");
    LLVMSetInitializer(c.gtbl, LLVMConstNull(i8p));
    LLVMSetLinkage(c.gtbl, LLVMInternalLinkage);
    c.gcnt = LLVMAddGlobal(g->mod, i64, "zan.embed.gcnt");
    LLVMSetInitializer(c.gcnt, LLVMConstInt(i64, 0, 0));
    LLVMSetLinkage(c.gcnt, LLVMInternalLinkage);
    c.own_tbl = tbl0;
    c.own_cnt = LLVMConstInt(i64, (unsigned long long)files.n, 0);
    c.gbuf = LLVMAddGlobal(g->mod, i8p, "zan.embed.gbuf");
    LLVMSetInitializer(c.gbuf, LLVMConstNull(i8p));
    LLVMSetLinkage(c.gbuf, LLVMInternalLinkage);
    c.empty = embed_bytes_global(g, "zan.embed.empty",
                                 (const unsigned char *)"", 0);

    embed_emit_register(g, &c);
    LLVMValueRef find = embed_emit_find(g, &c);
    LLVMValueRef unzip = any_compressed ? embed_emit_unzip(g, &c)
                                        : embed_emit_passthrough(g, &c);
    embed_emit_read_has_bytes(g, &c, find, unzip);
    embed_emit_list(g, &c);

    /* 内部辅助逻辑 */
    g->uses_embed_api = false;

    int n = files.n;
    for (int i = 0; i < n; i++) {
        free(files.v[i].name);
        free(files.v[i].data);
    }
    free(files.v);
    free(compressed);
    return n;
}
