/* embedres.c -- bakes files into the produced executable.
 *
 * The IDE's per-item build action "embed" ends up here: instead of copying a
 * project's config/, views/ or wwwroot/ next to the exe, their bytes become
 * constants in the program's own module and are handed to the runtime registry
 * (zan_embed_register) by a module constructor, so System.IO reads them with
 * no file on disk. Generating the data straight into the module keeps this
 * working for every target and needs no external C compiler -- unlike the
 * scripts/gen_embed.ps1 route, which compiles a generated .c with clang.
 */

#include "embedres.h"
#include "win_utf8.h"
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
    char          *name; /* logical resource name, '/'-separated */
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

/* Walks `dir`, adding every file below it under the logical prefix `name`. */
static void embed_walk(zan_embed_list_t *l, const char *dir, const char *name) {
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
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                embed_walk(l, path, sub);
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
        if (S_ISDIR(st.st_mode)) embed_walk(l, path, sub);
        else if (S_ISREG(st.st_mode)) embed_add_file(l, path, sub);
        free(sub);
    }
    closedir(d);
#endif
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

/* A private constant holding `len` bytes plus a trailing NUL, so text
 * resources round-trip as C strings while `len` stays the true size. */
static LLVMValueRef embed_bytes_global(zan_irgen_t *g, const char *label,
                                       const unsigned char *data,
                                       long long len) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    /* LLVM array lengths are 32-bit; a resource of 4 GiB or more would
     * truncate here and emit a bogus smaller constant. Refuse it instead. */
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

/* The function to give a body to. A declaration already exists whenever the
 * program references the API from Zan (Skin.EmbedRead's externs); defining
 * that very function is what makes those calls resolve here, where adding a
 * second one would leave them pointing at an undefined symbol. */
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

/* The read API the stdlib calls (Skin.EmbedRead, System.IO's embedded-resource
 * fallback). Emitted into the program's own module rather than linked from the
 * shipped zan_embed_api object, so an embedding program stays linkable for
 * cross targets whose toolchain directory carries no such object. This
 * module's own --embed table lives in two immutable slots; a generated data
 * object (scripts/gen_embed.ps1) linked alongside appends ITS table through
 * zan_embed_register (slots 2+), and lookups consult every slot with the
 * registered ones winning duplicate names -- the same accumulate semantics the
 * shipped runtime implementation (src/runtime/zan_embed_api.c) follows. */
struct embed_api_ctx {
    LLVMTypeRef  i8, i8p, i32, i64, ent_ty;
    LLVMValueRef gtbl, gcnt, gbuf, empty;
    LLVMValueRef own_tbl, own_cnt;  /* this module's --embed table (immutable) */
    long long    buf_cap;
};

/* entry pointer for index `i`, or the field pointers of it */
static LLVMValueRef embed_entry_at(LLVMBuilderRef b, struct embed_api_ctx *c,
                                   LLVMValueRef base, LLVMValueRef i) {
    return LLVMBuildGEP2(b, c->ent_ty, base, &i, 1, "ent");
}

/* i8* zan.embed.find(i8* name): the entry with that name, or null. Scans two
 * slots in turn -- first the table registered through zan_embed_register (a
 * generated data object's group, which wins duplicate names), then this
 * module's own --embed table. */
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
    /* slot 0 = registered table (gtbl/gcnt, mutable), slot 1 = this module's
     * own table (own_tbl/own_cnt, immutable). */
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

    /* the current slot is exhausted: advance to slot 1 (own table) or stop */
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

static void embed_emit_read_has_bytes(zan_irgen_t *g, struct embed_api_ctx *c,
                                      LLVMValueRef find) {
    LLVMTypeRef find_args[] = { c->i8p };
    LLVMTypeRef find_ty = LLVMFunctionType(c->i8p, find_args, 1, 0);
    LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);

    /* const char* zan_embed_read(const char* name) */
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
    LLVMValueRef rdp = LLVMBuildStructGEP2(b, c->ent_ty, re, 1, "dp");
    LLVMBuildRet(b, LLVMBuildLoad2(b, c->i8p, rdp, "d"));
    LLVMPositionBuilderAtEnd(b, rnil);
    LLVMBuildRet(b, c->empty);

    /* int zan_embed_has(const char* name) */
    LLVMTypeRef hty = LLVMFunctionType(c->i32, find_args, 1, 0);
    LLVMValueRef hfn = embed_define(g, "zan_embed_has", hty);
    if (!hfn) { LLVMDisposeBuilder(b); return; }
    LLVMBasicBlockRef hb = LLVMAppendBasicBlockInContext(g->ctx, hfn, "entry");
    LLVMPositionBuilderAtEnd(b, hb);
    LLVMValueRef ha = LLVMGetParam(hfn, 0);
    LLVMValueRef he = LLVMBuildCall2(b, find_ty, find, &ha, 1, "e");
    LLVMValueRef hv = LLVMBuildIsNotNull(b, he, "hit");
    LLVMBuildRet(b, LLVMBuildZExt(b, hv, c->i32, "hi"));

    /* const unsigned char* zan_embed_bytes(const char* name, int* outLen) */
    LLVMTypeRef bargs[] = { c->i8p, c->i8p };
    LLVMTypeRef bty = LLVMFunctionType(c->i8p, bargs, 2, 0);
    LLVMValueRef bfn = embed_define(g, "zan_embed_bytes", bty);
    if (!bfn) { LLVMDisposeBuilder(b); return; }
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
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, bo, "noout"), bdg, bsg);
    LLVMPositionBuilderAtEnd(b, bsg);
    LLVMValueRef blp = LLVMBuildStructGEP2(b, c->ent_ty, be, 2, "lp");
    LLVMValueRef bl = LLVMBuildLoad2(b, c->i64, blp, "l");
    LLVMBuildStore(b, LLVMBuildTrunc(b, bl, c->i32, "l32"), bo);
    LLVMBuildBr(b, bdg);
    LLVMPositionBuilderAtEnd(b, bdg);
    LLVMValueRef bdp = LLVMBuildStructGEP2(b, c->ent_ty, be, 1, "dp");
    LLVMBuildRet(b, LLVMBuildLoad2(b, c->i8p, bdp, "d"));
    LLVMPositionBuilderAtEnd(b, bnil);
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, bo, "noout2"), bdn, bsn);
    LLVMPositionBuilderAtEnd(b, bsn);
    LLVMBuildStore(b, LLVMConstInt(c->i32, 0, 0), bo);
    LLVMBuildBr(b, bdn);
    LLVMPositionBuilderAtEnd(b, bdn);
    LLVMBuildRet(b, LLVMConstNull(c->i8p));

    LLVMDisposeBuilder(b);
}

/* const char* zan_embed_list(const char* prefix): the matching names joined by
 * '\n' in a lazily allocated buffer. Iterates the two lookup slots (registered
 * table first, then this module's own) and drops names the registered slot
 * already produced, mirroring find's "registered wins duplicates" order. */
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
    /* In slot 0 (registered table) the name is emitted as-is; in slot 1 (own
     * table) it is dropped when the registered table also carries it. */
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

    /* the current slot is exhausted: advance to slot 1 (own table) or stop */
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

/* void zan_embed_register(const zan_embed_ent* tbl, long long n)
 *
 * Called by a generated data object's constructor (scripts/gen_embed.ps1).
 * Stores the table in the registered slot (slot 0 of find/list); this module's
 * own --embed table stays reachable in slot 1, so both groups remain visible.
 * Re-registering the identical table is a no-op, so two ctors carrying the
 * same object (a static archive pulled in twice) stay harmless. */
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
    /* FNV-1a over the whole driver: the fingerprint goes into the resource
     * name, so the extracted copy of one build can never be mistaken for
     * another's (a rebuild against different CEF headers often keeps the
     * very same file size). */
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

int zan_embed_emit_specs(zan_irgen_t *g, const char *const *specs, int count) {
    zan_embed_list_t files;
    memset(&files, 0, sizeof(files));

    for (int i = 0; i < count; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s", specs[i]);
        char *eq = strrchr(path, '=');
        const char *prefix = NULL;
        if (eq) { *eq = 0; prefix = eq + 1; }
        if (!prefix || !prefix[0]) prefix = embed_basename(path);
        int before = files.n;
        if (embed_is_dir(path)) embed_walk(&files, path, prefix);
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
        if (!name || !data) { free(ents); return -1; }
        LLVMValueRef vals[] = { name, data,
            LLVMConstInt(i64, (unsigned long long)files.v[i].len, 0) };
        ents[i] = LLVMConstNamedStruct(ent_ty, vals, 3);
    }
    LLVMValueRef nulls[] = { LLVMConstNull(i8p), LLVMConstNull(i8p),
                             LLVMConstInt(i64, 0, 0) };
    ents[files.n] = LLVMConstNamedStruct(ent_ty, nulls, 3);

    /* With nothing embedded the API is still emitted, with an empty table: a
     * generated data object linked alongside (scripts/gen_embed.ps1) fills
     * the registered slot in through zan_embed_register, and a program that
     * merely calls the API reads empty and falls back to the filesystem. */
    LLVMValueRef tbl0 = LLVMConstNull(i8p);
    if (files.n > 0) {
        LLVMTypeRef tbl_ty = LLVMArrayType(ent_ty, (unsigned)files.n + 1);
        LLVMValueRef tbl = LLVMAddGlobal(g->mod, tbl_ty, "zan.embed.tbl");
        LLVMSetInitializer(tbl,
            LLVMConstArray(ent_ty, ents, (unsigned)files.n + 1));
        LLVMSetLinkage(tbl, LLVMPrivateLinkage);
        LLVMSetGlobalConstant(tbl, 1);
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
    /* The registered slot starts EMPTY (a generated data object fills it in
     * at startup); this module's own table lives in own_tbl/own_cnt. Two
     * slots, no clobbering -- the bug that once made a skins-only generated
     * object hide an --embed-baked assets catalog. */
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
    embed_emit_read_has_bytes(g, &c, find);
    embed_emit_list(g, &c);

    /* The API is defined right here, so the shipped zan_embed_api object must
     * NOT also be linked (duplicate definitions); this is what lets an
     * embedding program link for targets that ship no such object. */
    g->uses_embed_api = false;

    int n = files.n;
    for (int i = 0; i < n; i++) {
        free(files.v[i].name);
        free(files.v[i].data);
    }
    free(files.v);
    return n;
}
