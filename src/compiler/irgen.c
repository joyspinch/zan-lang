/* 内部辅助实现 */

#include "irgen.h"
#include "irgen_compact.h"
#include "optimizer.h"
#include "builtin_api.h"
#include "reflect_api.h"
#include "arena.h"
#include "diag.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* 内部辅助实现 */
/* 模块核心语义抽象与接口调用契约 */
static LLVMValueRef zan_iwiden(LLVMBuilderRef b, LLVMValueRef v, LLVMTypeRef to) {
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(vt) != LLVMIntegerTypeKind) return v;
    return LLVMGetIntTypeWidth(vt) <= 8 ? LLVMBuildZExt(b, v, to, "zx")
                                        : LLVMBuildSExt(b, v, to, "wx");
}

static void zan_ipair(LLVMBuilderRef b, LLVMValueRef *l, LLVMValueRef *r) {
    LLVMTypeRef tl = LLVMTypeOf(*l), tr = LLVMTypeOf(*r);
    if (LLVMGetTypeKind(tl) != LLVMIntegerTypeKind ||
        LLVMGetTypeKind(tr) != LLVMIntegerTypeKind) return;
    unsigned wl = LLVMGetIntTypeWidth(tl), wr = LLVMGetIntTypeWidth(tr);
    if (wl == wr) {
        /* 内部辅助逻辑 */
        if (wl == 8) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(LLVMGetTypeContext(tl));
            *l = LLVMBuildZExt(b, *l, i64, "zx");
            *r = LLVMBuildZExt(b, *r, i64, "zx");
        }
        return;
    }
    if (wl < wr) *l = zan_iwiden(b, *l, tr);
    else         *r = zan_iwiden(b, *r, tl);
}

#define ZAN_IBIN(name, builder)                                              \
static LLVMValueRef name(LLVMBuilderRef b, LLVMValueRef l, LLVMValueRef r,    \
                         const char *n) {                                    \
    zan_ipair(b, &l, &r);                                                    \
    return builder(b, l, r, n);                                              \
}
ZAN_IBIN(zan_add,  LLVMBuildAdd)
ZAN_IBIN(zan_sub,  LLVMBuildSub)
ZAN_IBIN(zan_mul,  LLVMBuildMul)
ZAN_IBIN(zan_sdiv, LLVMBuildSDiv)
ZAN_IBIN(zan_udiv, LLVMBuildUDiv)
ZAN_IBIN(zan_srem, LLVMBuildSRem)
ZAN_IBIN(zan_urem, LLVMBuildURem)
ZAN_IBIN(zan_and,  LLVMBuildAnd)
ZAN_IBIN(zan_or,   LLVMBuildOr)
ZAN_IBIN(zan_xor,  LLVMBuildXor)
ZAN_IBIN(zan_shl,  LLVMBuildShl)
ZAN_IBIN(zan_lshr, LLVMBuildLShr)
ZAN_IBIN(zan_ashr, LLVMBuildAShr)
#undef ZAN_IBIN

static LLVMValueRef zan_icmp(LLVMBuilderRef b, LLVMIntPredicate p,
                             LLVMValueRef l, LLVMValueRef r, const char *n) {
    zan_ipair(b, &l, &r);
    return LLVMBuildICmp(b, p, l, r, n);
}

/* Normalize a condition to i1 */
static LLVMValueRef zan_tobool(LLVMBuilderRef b, LLVMValueRef v, const char *n) {
    LLVMTypeRef ty = LLVMTypeOf(v);
    LLVMTypeKind k = LLVMGetTypeKind(ty);
    if (k == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(ty) == 1) { return v; }
    if (k == LLVMFloatTypeKind || k == LLVMDoubleTypeKind) {
        return LLVMBuildFCmp(b, LLVMRealUNE, v, LLVMConstNull(ty), n);
    }
    return LLVMBuildICmp(b, LLVMIntNE, v, LLVMConstNull(ty), n);
}

#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/DebugInfo.h>
#if defined(__has_include)
#if __has_include(<llvm/Config/llvm-config.h>)
#include <llvm/Config/llvm-config.h>
#endif
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define zan_getcwd _getcwd
#else
#include <unistd.h>
#define zan_getcwd getcwd
#endif

/* 底层系统交互与数据协议契约 */
static LLVMMetadataRef di_file_for(zan_irgen_t *g, uint32_t file_id);

static void di_ensure(zan_irgen_t *g) {
    if (!g->emit_debug || g->di_builder) return;
    g->di_builder = LLVMCreateDIBuilder(g->mod);

    /* 模块核心语义抽象与接口调用契约 */
    LLVMContextRef c = g->ctx;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMAddModuleFlag(g->mod, LLVMModuleFlagBehaviorWarning,
                      "Debug Info Version", 18,
                      LLVMValueAsMetadata(LLVMConstInt(i32, 3, 0)));
    LLVMAddModuleFlag(g->mod, LLVMModuleFlagBehaviorWarning,
                      "Dwarf Version", 13,
                      LLVMValueAsMetadata(LLVMConstInt(i32, 4, 0)));

    LLVMMetadataRef file = di_file_for(g, 0);
    const char *producer = "zanc";
    g->di_cu = LLVMDIBuilderCreateCompileUnit(
        g->di_builder, LLVMDWARFSourceLanguageC, file,
        producer, strlen(producer),
        /* isOptimized */ 0, /*Flags*/ "", 0, /*RuntimeVer*/ 0,
        /* SplitName */ "", 0, LLVMDWARFEmissionFull,
        /* DWOId */ 0, /*SplitDebugInlining*/ 0,
        /* DebugInfoForProfiling */ 0, /*SysRoot*/ "", 0, /*SDK*/ "", 0);
}

static LLVMMetadataRef di_file_for(zan_irgen_t *g, uint32_t file_id) {
    if (!g->emit_debug) return NULL;
    /* 底层系统交互与数据协议契约 */
    if (!zan_tab_reserve((void **)&g->di_files, &g->di_file_cap,
                         sizeof(*g->di_files), (int)file_id, 64))
        return NULL;
    if (g->di_files[file_id]) return g->di_files[file_id];
    if (!g->di_builder) g->di_builder = LLVMCreateDIBuilder(g->mod);

    const char *path = NULL;
    if (g->diag && file_id < (uint32_t)g->diag->file_count && g->diag->file_names)
        path = g->diag->file_names[file_id];
    if (!path || !path[0]) path = g->src_file ? g->src_file : "<unknown>.zan";

    /* 内部辅助逻辑 */
    char dir[1024];
    if (path[0] == '/' || (path[0] && path[1] == ':')) {
        /* 底层系统交互与数据协议契约 */
        const char *slash = strrchr(path, '/');
        const char *bslash = strrchr(path, '\\');
        const char *cut = slash > bslash ? slash : bslash;
        if (cut) {
            size_t n = (size_t)(cut - path);
            if (n >= sizeof(dir)) n = sizeof(dir) - 1;
            memcpy(dir, path, n);
            dir[n] = '\0';
            path = cut + 1;
        } else {
            dir[0] = '\0';
        }
    } else if (!zan_getcwd(dir, sizeof(dir))) {
        dir[0] = '\0';
    }

    LLVMMetadataRef f = LLVMDIBuilderCreateFile(
        g->di_builder, path, strlen(path), dir, strlen(dir));
    g->di_files[file_id] = f;
    return f;
}

/* 内部辅助逻辑 */
static const char *loc_site_file(zan_irgen_t *g, zan_loc_t loc) {
    const char *p = NULL;
    if (g->diag && g->diag->file_names &&
        (int)loc.file_id < g->diag->file_count) {
        const char *q = g->diag->file_names[loc.file_id];
        if (q && q[0]) p = q;
    }
    if (!p) p = g->src_file ? g->src_file : "<unknown>";
    /* 模块核心语义抽象与接口调用契约 */
    const char *slash = NULL, *prev = NULL;
    for (const char *c = p; *c; c++) {
        if (*c == '/' || *c == '\\') {
            prev = slash;
            slash = c;
        }
    }
    if (slash && prev) return prev + 1;
    return p;
}

/* 模块核心语义抽象与接口调用契约 */
#define ZAN_STR_INTERN_BUCKETS 1024
LLVMValueRef zan_irgen_intern_string(zan_irgen_t *g, const char *text) {
    if (!g->str_intern) {
        g->str_intern = zan_arena_alloc(g->arena,
            ZAN_STR_INTERN_BUCKETS * sizeof(zan_str_intern_t *));
        g->str_intern_cap = ZAN_STR_INTERN_BUCKETS;
    }
    unsigned h = 5381;
    for (const char *p = text; *p; p++) h = h * 33 + (unsigned char)*p;
    unsigned b = h & (ZAN_STR_INTERN_BUCKETS - 1);
    for (zan_str_intern_t *e = g->str_intern[b]; e; e = e->next)
        if (strcmp(e->text, text) == 0) return e->gv;
    zan_str_intern_t *e = zan_arena_alloc(g->arena, sizeof(*e));
    size_t n = strlen(text) + 1;
    e->text = zan_arena_alloc(g->arena, n);
    memcpy(e->text, text, n);
    e->gv = LLVMBuildGlobalStringPtr(g->builder, text, "rterr");
    e->next = g->str_intern[b];
    g->str_intern[b] = e;
    return e->gv;
}

/* 内部辅助实现 */
static LLVMMetadataRef di_ensure_sp(zan_irgen_t *g, uint32_t file_id, unsigned line) {
    if (!g->emit_debug || !g->builder) return NULL;
    LLVMBasicBlockRef bb = LLVMGetInsertBlock(g->builder);
    if (!bb) return NULL;
    LLVMValueRef fn = LLVMGetBasicBlockParent(bb);
    if (!fn) return NULL;
    di_ensure(g);
    LLVMMetadataRef sp = LLVMGetSubprogram(fn);
    if (sp) return sp;
    size_t nlen = 0;
    const char *name = LLVMGetValueName2(fn, &nlen);
    if (!name || nlen == 0) { name = "fn"; nlen = 2; }
    LLVMMetadataRef file = di_file_for(g, file_id);
    LLVMMetadataRef subty = LLVMDIBuilderCreateSubroutineType(
        g->di_builder, file, NULL, 0, LLVMDIFlagZero);
    unsigned l = line ? line : 1;
    sp = LLVMDIBuilderCreateFunction(
        g->di_builder, file, name, nlen, name, nlen, file, l, subty,
        /* IsLocalToUnit */ 0, /*IsDefinition*/ 1, /*ScopeLine*/ l,
        LLVMDIFlagZero, /* IsOptimized */ 0);
    LLVMSetSubprogram(fn, sp);
    return sp;
}

/* 底层系统交互与数据协议契约 */
static void di_clear(zan_irgen_t *g) {
    if (!g->emit_debug || !g->builder) return;
    LLVMSetCurrentDebugLocation2(g->builder, NULL);
    g->di_cur_line = 0;
    g->di_cur_file = 0;
}

/* 内部辅助逻辑 */
static void di_set_loc(zan_irgen_t *g, zan_loc_t loc) {
    if (!g->emit_debug || !g->builder) return;
    LLVMMetadataRef sp = di_ensure_sp(g, loc.file_id, loc.line);
    if (!sp) return;
    g->di_cur_line = loc.line;
    g->di_cur_file = loc.file_id;
    unsigned line = loc.line ? loc.line : 1;
    LLVMMetadataRef dl = LLVMDIBuilderCreateDebugLocation(
        g->ctx, line, loc.col, sp, NULL);
    LLVMSetCurrentDebugLocation2(g->builder, dl);
}

/* 模块核心语义抽象与接口调用契约 */
static LLVMMetadataRef di_type_from_llvm(zan_irgen_t *g, LLVMTypeRef ty) {
    LLVMTypeKind k = LLVMGetTypeKind(ty);
    switch (k) {
    case LLVMIntegerTypeKind: {
        unsigned w = LLVMGetIntTypeWidth(ty);
        if (w == 1)
            return LLVMDIBuilderCreateBasicType(g->di_builder, "bool", 4, 8,
                                                /* DW_ATE_boolean */ 0x02,
                                                LLVMDIFlagZero);
        char nm[16];
        int n = snprintf(nm, sizeof(nm), "i%u", w);
        return LLVMDIBuilderCreateBasicType(g->di_builder, nm, (size_t)n, w,
                                            /* DW_ATE_signed */ 0x05,
                                            LLVMDIFlagZero);
    }
    case LLVMDoubleTypeKind:
        return LLVMDIBuilderCreateBasicType(g->di_builder, "f64", 3, 64,
                                            /* DW_ATE_float */ 0x04, LLVMDIFlagZero);
    case LLVMFloatTypeKind:
        return LLVMDIBuilderCreateBasicType(g->di_builder, "f32", 3, 32,
                                            /* DW_ATE_float */ 0x04, LLVMDIFlagZero);
    case LLVMPointerTypeKind: {
        LLVMMetadataRef byte = LLVMDIBuilderCreateBasicType(
            g->di_builder, "byte", 4, 8, /* DW_ATE_unsigned_char */ 0x08,
            LLVMDIFlagZero);
        return LLVMDIBuilderCreatePointerType(g->di_builder, byte, 64, 0, 0,
                                              "ptr", 3);
    }
    default:
        return NULL;
    }
}

/* 发射an llvm */
static void di_declare_var(zan_irgen_t *g, zan_istr_t name, LLVMValueRef storage,
                           zan_type_t *zt);

/* 内部辅助实现 */

/* 核心系统底层抽象与内存语义契约 */
#define ZAN_DI_TAG_STRUCTURE 0x13u /* DW_TAG_structure_type */
#define ZAN_DI_ATE_BOOLEAN   0x02u
#define ZAN_DI_ATE_FLOAT     0x04u
#define ZAN_DI_ATE_SIGNED    0x05u
#define ZAN_DI_ATE_UNSIGNED  0x07u
#define ZAN_DI_ATE_UCHAR     0x08u
#define ZAN_DI_ATE_UTF       0x10u

/* 底层系统交互与数据协议契约 */
static bool class_has_virtual_methods(zan_symbol_t *sym);
static bool field_member_is_static(zan_symbol_t *m);
static unsigned long abi_size_of(LLVMTypeRef t);
static unsigned long abi_align_of(LLVMTypeRef t);

typedef struct {
    zan_type_t *type;        /* 核心系统底层抽象与内存语义契约 */
    LLVMMetadataRef placeholder; /* 核心系统底层抽象与内存语义契约 */
    LLVMMetadataRef composite;   /* 核心系统底层抽象与内存语义契约 */
    int building;
} zan_di_type_rec_t;
/* 内部辅助实现 */
static zan_di_type_rec_t **g_di_types = NULL;
static int g_di_type_count = 0, g_di_type_cap = 0;

/* 内部辅助实现 */
static void di_debug_types_reset(void) {
    for (int i = 0; i < g_di_type_count; i++) free(g_di_types[i]);
    free(g_di_types);
    g_di_types = NULL;
    g_di_type_count = g_di_type_cap = 0;
}

static zan_di_type_rec_t *di_type_rec(zan_type_t *t) {
    for (int i = 0; i < g_di_type_count; i++)
        if (g_di_types[i]->type == t) return g_di_types[i];
    if (g_di_type_count >= g_di_type_cap) {
        int cap = g_di_type_cap > 0 ? g_di_type_cap * 2 : 64;
        zan_di_type_rec_t **grown = (zan_di_type_rec_t **)realloc(g_di_types,
            sizeof(zan_di_type_rec_t *) * (size_t)cap);
        if (!grown) return NULL;
        g_di_types = grown;
        g_di_type_cap = cap;
    }
    zan_di_type_rec_t *r = (zan_di_type_rec_t *)malloc(sizeof(*r));
    if (!r) return NULL;
    r->type = t;
    r->placeholder = NULL;
    r->composite = NULL;
    r->building = 0;
    g_di_types[g_di_type_count++] = r;
    return r;
}

static LLVMMetadataRef di_type_for_zan(zan_irgen_t *g, zan_type_t *t,
                                       zan_type_t *inst, int depth);

static LLVMMetadataRef di_basic(zan_irgen_t *g, const char *nm, uint64_t bits,
                                unsigned encoding) {
    return LLVMDIBuilderCreateBasicType(g->di_builder, nm, strlen(nm), bits,
                                        encoding, LLVMDIFlagZero);
}

/* 模块核心语义抽象与接口调用契约 */
static LLVMMetadataRef di_byte_ptr(zan_irgen_t *g) {
    LLVMMetadataRef byte = di_basic(g, "byte", 8, ZAN_DI_ATE_UCHAR);
    return LLVMDIBuilderCreatePointerType(g->di_builder, byte, 64, 0, 0,
                                          "", 0);
}

/* 模块核心语义抽象与接口调用契约 */
static void di_type_name(zan_type_t *t, char *buf, size_t cap) {
    if (cap == 0) return;
    buf[0] = '\0';
    if (!t) return;
    if (t->kind == TYPE_ARRAY) {
        di_type_name(t->element_type, buf, cap);
        size_t n = strlen(buf);
        snprintf(buf + n, cap - n, "[]");
        return;
    }
    if (t->name.str && t->name.len) {
        size_t n = t->name.len < cap - 1 ? t->name.len : cap - 1;
        memcpy(buf, t->name.str, n);
        buf[n] = '\0';
    } else {
        snprintf(buf, cap, "<anon>");
    }
    if (t->type_arg_count > 0) {
        size_t n = strlen(buf);
        snprintf(buf + n, cap - n, "<");
        for (int i = 0; i < t->type_arg_count && n < cap; i++) {
            n = strlen(buf);
            char arg[64];
            di_type_name(t->type_args[i], arg, sizeof(arg));
            snprintf(buf + n, cap - n, "%s%s", i ? ", " : "", arg);
        }
        n = strlen(buf);
        snprintf(buf + n, cap - n, ">");
    }
}

/* 内部辅助实现 */
static zan_type_t *di_subst_param(zan_type_t *t, zan_type_t *inst) {
    if (t->kind != TYPE_TYPE_PARAM || !inst || !inst->sym ||
        inst->type_arg_count <= 0 || !t->name.str)
        return NULL;
    zan_symbol_t *cls = inst->sym;
    int idx = 0;
    for (int i = 0; i < cls->member_count; i++) {
        zan_symbol_t *m = cls->members[i];
        if (m->kind != SYM_TYPE_PARAM) continue;
        if (m->name.len == t->name.len &&
            memcmp(m->name.str, t->name.str, t->name.len) == 0)
            return inst->type_args[idx];
        idx++;
    }
    /* 模块核心语义抽象与接口调用契约 */
    if (cls->decl &&
        cls->decl->type_decl.type_params.count == inst->type_arg_count) {
        zan_ast_list_t *tps = &cls->decl->type_decl.type_params;
        for (int i = 0; i < tps->count; i++) {
            zan_ast_node_t *tp = tps->items[i];
            if (tp->kind != AST_IDENTIFIER || !tp->ident.name.str) continue;
            if (tp->ident.name.len == t->name.len &&
                memcmp(tp->ident.name.str, t->name.str, t->name.len) == 0)
                return inst->type_args[i];
        }
    }
    return NULL;
}

/* 内部辅助逻辑 */
static struct zan_struct_type_entry *di_struct_entry(zan_irgen_t *g,
                                                     zan_symbol_t *sym) {
    for (int i = 0; i < g->struct_type_count; i++)
        if (g->struct_types[i].sym == sym) return &g->struct_types[i];
    return NULL;
}

static unsigned long di_align_up(unsigned long v, unsigned long a) {
    if (a == 0) return v;
    unsigned long r = v % a;
    return r ? v + (a - r) : v;
}

/* 模块核心语义抽象与接口调用契约 */
static LLVMMetadataRef di_member(zan_irgen_t *g, LLVMMetadataRef scope,
                                 LLVMMetadataRef file, const char *nm,
                                 LLVMMetadataRef ty, unsigned long off,
                                 unsigned long size_bytes, unsigned line) {
    return LLVMDIBuilderCreateMemberType(
        g->di_builder, scope, nm, strlen(nm), file, line,
        size_bytes * 8, /* AlignInBits */ 0, off * 8, LLVMDIFlagZero, ty);
}

/* 内部辅助实现 */
static int di_type_named(zan_type_t *t, const char *n) {
    return t && t->kind != TYPE_ARRAY && t->name.str &&
           (int)t->name.len == (int)strlen(n) &&
           memcmp(t->name.str, n, strlen(n)) == 0;
}

static LLVMMetadataRef di_builtin_composite(zan_irgen_t *g, zan_type_t *t,
                                            int depth) {
    char name[128];
    di_type_name(t, name, sizeof(name));
    LLVMMetadataRef file = di_file_for(g, g->di_cur_file);
    LLVMMetadataRef i64 = di_basic(g, "long", 64, ZAN_DI_ATE_SIGNED);
    LLVMMetadataRef bytep = di_byte_ptr(g);

    /* 底层系统交互与数据协议契约 */
    if (di_type_named(t, "List") || di_type_named(t, "StringBuilder")) {
        LLVMMetadataRef data = i64;
        if (di_type_named(t, "StringBuilder")) data = bytep;
        LLVMMetadataRef datap = LLVMDIBuilderCreatePointerType(
            g->di_builder, data, 64, 0, 0, "", 0);
        LLVMMetadataRef members[3] = {
            di_member(g, file, file, "count", i64, 0, 8, 1),
            di_member(g, file, file, "capacity", i64, 8, 8, 1),
            di_member(g, file, file, "data", datap, 16, 8, 1),
        };
        LLVMMetadataRef composite = LLVMDIBuilderCreateStructType(
            g->di_builder, file, name, strlen(name), file, 1, 24 * 8, 0,
            LLVMDIFlagZero, NULL, members, 3, 0, NULL, NULL, 0);
        return LLVMDIBuilderCreatePointerType(g->di_builder, composite, 64, 0,
                                              0, "", 0);
    }

    /* { i64 count, i64 capacity, i8** keys, i64* values, */
    if (di_type_named(t, "Dict") || di_type_named(t, "Dictionary")) {
        LLVMMetadataRef keys = LLVMDIBuilderCreatePointerType(
            g->di_builder, bytep, 64, 0, 0, "", 0);
        LLVMMetadataRef vals = LLVMDIBuilderCreatePointerType(
            g->di_builder, i64, 64, 0, 0, "", 0);
        LLVMMetadataRef members[4] = {
            di_member(g, file, file, "count", i64, 0, 8, 1),
            di_member(g, file, file, "capacity", i64, 8, 8, 1),
            di_member(g, file, file, "keys", keys, 16, 8, 1),
            di_member(g, file, file, "values", vals, 24, 8, 1),
        };
        LLVMMetadataRef composite = LLVMDIBuilderCreateStructType(
            g->di_builder, file, name, strlen(name), file, 1, 0, 0,
            LLVMDIFlagZero, NULL, members, 4, 0, NULL, NULL, 0);
        return LLVMDIBuilderCreatePointerType(g->di_builder, composite, 64, 0,
                                              0, "", 0);
    }

    /* { i64 completed, i64 result, i64 thread_handle } -- Task */
    if (di_type_named(t, "Task")) {
        LLVMMetadataRef members[3] = {
            di_member(g, file, file, "completed", i64, 0, 8, 1),
            di_member(g, file, file, "result", i64, 8, 8, 1),
            di_member(g, file, file, "thread_handle", i64, 16, 8, 1),
        };
        LLVMMetadataRef composite = LLVMDIBuilderCreateStructType(
            g->di_builder, file, name, strlen(name), file, 1, 24 * 8, 0,
            LLVMDIFlagZero, NULL, members, 3, 0, NULL, NULL, 0);
        return LLVMDIBuilderCreatePointerType(g->di_builder, composite, 64, 0,
                                              0, "", 0);
    }

    /* 底层系统交互与数据协议契约 */
    if (di_type_named(t, "Span")) {
        LLVMMetadataRef members[2] = {
            di_member(g, file, file, "base", bytep, 0, 8, 1),
            di_member(g, file, file, "length", i64, 8, 8, 1),
        };
        return LLVMDIBuilderCreateStructType(
            g->di_builder, file, name, strlen(name), file, 1, 16 * 8, 0,
            LLVMDIFlagZero, NULL, members, 2, 0, NULL, NULL, 0);
    }
    return NULL;
}

/* 内部辅助实现 */
static LLVMMetadataRef di_class_composite(zan_irgen_t *g, zan_type_t *t,
                                          int depth) {
    zan_symbol_t *sym = t->sym;
    if (!sym || !sym->decl) return NULL;
    struct zan_struct_type_entry *e = di_struct_entry(g, sym);
    if (!e) return NULL;

    zan_di_type_rec_t *rec = di_type_rec(t);
    if (!rec) return NULL;
    if (rec->building) return rec->placeholder; /* 核心系统底层抽象与内存语义契约 */
    if (rec->composite) {
        return LLVMDIBuilderCreatePointerType(g->di_builder, rec->composite,
                                              64, 0, 0, "", 0);
    }

    char name[128];
    di_type_name(t, name, sizeof(name));
    uint32_t fid = sym->decl->loc.file_id ? sym->decl->loc.file_id
                                          : g->di_cur_file;
    LLVMMetadataRef file = di_file_for(g, fid);
    unsigned line = sym->decl->loc.line ? sym->decl->loc.line : 1;

    rec->placeholder = LLVMDIBuilderCreateReplaceableCompositeType(
        g->di_builder, ZAN_DI_TAG_STRUCTURE, name, strlen(name),
        /* Scope */ file, file, line, /*RuntimeLang*/ 0,
        /* SizeInBits */ 0, /*AlignInBits*/ 0, LLVMDIFlagZero, NULL, 0);
    rec->building = 1;

    /* 内部辅助实现 */
    int nslots = e->field_count;
    int vptr = class_has_virtual_methods(sym) ? 1 : 0;
    LLVMMetadataRef *members =
        (LLVMMetadataRef *)calloc((size_t)(nslots > 0 ? nslots : 1),
                                  sizeof(LLVMMetadataRef));
    if (!members) { rec->building = 0; return NULL; }

    unsigned long cursor = 0, max_align = 1, size = 0;
    int slot = 0;

    if (vptr && slot < nslots) {
        unsigned long fsize = abi_size_of(e->field_llvm[0]);
        unsigned long off;
        if (e->explicit_layout) {
            off = e->field_offsets[0];
        } else {
            unsigned long fa = abi_align_of(e->field_llvm[0]);
            off = di_align_up(cursor, fa);
            cursor = off + fsize;
            if (fa > max_align) max_align = fa;
            if (cursor > size) size = cursor;
        }
        LLVMMetadataRef mty = di_byte_ptr(g); /* 核心系统底层抽象与内存语义契约 */
        members[slot] = di_member(g, rec->placeholder, file, "$vptr", mty, off,
                                  fsize, line);
        slot++;
    }

    for (int i = 0; i < sym->member_count && slot < nslots; i++) {
        zan_symbol_t *m = sym->members[i];
        if ((m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) ||
            field_member_is_static(m)) {
            continue;
        }
        unsigned long off, fsize = abi_size_of(e->field_llvm[slot]);
        if (e->explicit_layout) {
            off = e->field_offsets[slot];
        } else {
            unsigned long fa = abi_align_of(e->field_llvm[slot]);
            off = di_align_up(cursor, fa);
            cursor = off + fsize;
            if (fa > max_align) max_align = fa;
            if (cursor > size) size = cursor;
        }

        zan_type_t *ft = di_subst_param(m->type, t);
        if (!ft) ft = m->type;
        LLVMMetadataRef mty = di_type_for_zan(g, ft, t, depth + 1);
        if (!mty) mty = di_byte_ptr(g);

        char mname[128];
        snprintf(mname, sizeof(mname), "%.*s", (int)m->name.len,
                 m->name.str);
        members[slot] = di_member(g, rec->placeholder, file, mname, mty, off,
                                  fsize, line);
        slot++;
    }

    unsigned long total = e->explicit_layout
                              ? 0
                              : di_align_up(size, max_align);
    LLVMMetadataRef composite = LLVMDIBuilderCreateStructType(
        g->di_builder, /* Scope */ file, name, strlen(name),
        file, line, total * 8,
        (uint32_t)(max_align > 1 ? di_align_up(max_align, 8) : 0) * 8,
        LLVMDIFlagZero, /* DerivedFrom */ NULL, members, (unsigned)slot,
        /* RunTimeLang */ 0, /*VTableHolder*/ NULL, /*UniqueId*/ NULL, 0);
    free(members);
    LLVMMetadataReplaceAllUsesWith(rec->placeholder, composite);
    rec->composite = composite;
    rec->building = 0;
    return LLVMDIBuilderCreatePointerType(g->di_builder, composite, 64, 0, 0,
                                          "", 0);
}

/* 内部辅助实现 */
static LLVMMetadataRef di_array_composite(zan_irgen_t *g, zan_type_t *t,
                                          zan_type_t *inst, int depth) {
    zan_di_type_rec_t *rec = di_type_rec(t);
    if (!rec) return NULL;
    if (rec->building) return rec->placeholder;
    if (rec->composite) {
        return LLVMDIBuilderCreatePointerType(g->di_builder, rec->composite,
                                              64, 0, 0, "", 0);
    }

    char name[128];
    di_type_name(t, name, sizeof(name));
    LLVMMetadataRef file = di_file_for(g, g->di_cur_file);

    rec->placeholder = LLVMDIBuilderCreateReplaceableCompositeType(
        g->di_builder, ZAN_DI_TAG_STRUCTURE, name, strlen(name),
        /* Scope */ file, file, /*Line*/ 1, /*RuntimeLang*/ 0,
        /* SizeInBits */ 0, /*AlignInBits*/ 0, LLVMDIFlagZero, NULL, 0);
    rec->building = 1;

    LLVMMetadataRef i64 = di_basic(g, "long", 64, ZAN_DI_ATE_SIGNED);
    LLVMMetadataRef lenm = di_member(g, rec->placeholder, file, "len", i64,
                                     (unsigned long)-16, 8, 1);

    LLVMMetadataRef elem = di_type_for_zan(g, t->element_type, inst, depth + 1);
    if (!elem) elem = di_byte_ptr(g);
    LLVMMetadataRef subs[1] = {
        LLVMDIBuilderGetOrCreateSubrange(g->di_builder, /* LowerBound */ 0,
                                         /* Count */ 0)
    };
    LLVMMetadataRef arrty = LLVMDIBuilderCreateArrayType(
        g->di_builder, /* SizeInBits */ 0, /*AlignInBits*/ 0, elem, subs, 1);
    LLVMMetadataRef elemsm = di_member(g, rec->placeholder, file, "elements",
                                       arrty, 0, 0, 1);

    LLVMMetadataRef members[2] = { lenm, elemsm };
    LLVMMetadataRef composite = LLVMDIBuilderCreateStructType(
        g->di_builder, /* Scope */ file, name, strlen(name),
        file, /* Line */ 1, /*SizeInBits*/ 0, /*AlignInBits*/ 0,
        LLVMDIFlagZero, /* DerivedFrom */ NULL, members, 2,
        /* RunTimeLang */ 0, /*VTableHolder*/ NULL, /*UniqueId*/ NULL, 0);
    LLVMMetadataReplaceAllUsesWith(rec->placeholder, composite);
    rec->composite = composite;
    rec->building = 0;
    return LLVMDIBuilderCreatePointerType(g->di_builder, composite, 64, 0, 0,
                                          "", 0);
}

/* 模块核心语义抽象与接口调用契约 */
static LLVMMetadataRef di_struct_composite(zan_irgen_t *g, zan_type_t *t,
                                           zan_type_t *inst, int depth) {
    zan_symbol_t *sym = t->sym;
    if (!sym || !sym->decl) return NULL;
    struct zan_struct_type_entry *e = di_struct_entry(g, sym);
    if (!e) return NULL;

    char name[128];
    di_type_name(t, name, sizeof(name));
    uint32_t fid = sym->decl->loc.file_id ? sym->decl->loc.file_id
                                          : g->di_cur_file;
    LLVMMetadataRef file = di_file_for(g, fid);
    unsigned line = sym->decl->loc.line ? sym->decl->loc.line : 1;

    int nslots = e->field_count;
    LLVMMetadataRef *members =
        (LLVMMetadataRef *)calloc((size_t)(nslots > 0 ? nslots : 1),
                                  sizeof(LLVMMetadataRef));
    if (!members) return NULL;

    unsigned long cursor = 0, max_align = 1, size = 0;
    int slot = 0;
    for (int i = 0; i < sym->member_count && slot < nslots; i++) {
        zan_symbol_t *m = sym->members[i];
        if ((m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) ||
            field_member_is_static(m)) {
            continue;
        }
        unsigned long off, fsize = abi_size_of(e->field_llvm[slot]);
        if (e->explicit_layout) {
            off = e->field_offsets[slot];
        } else {
            unsigned long fa = abi_align_of(e->field_llvm[slot]);
            off = di_align_up(cursor, fa);
            cursor = off + fsize;
            if (fa > max_align) max_align = fa;
            if (cursor > size) size = cursor;
        }
        zan_type_t *ft = di_subst_param(m->type, inst);
        if (!ft) ft = m->type;
        LLVMMetadataRef mty = di_type_for_zan(g, ft, inst, depth + 1);
        if (!mty) mty = di_byte_ptr(g);
        char mname[128];
        snprintf(mname, sizeof(mname), "%.*s", (int)m->name.len, m->name.str);
        members[slot] = di_member(g, file, file, mname, mty, off, fsize, line);
        slot++;
    }
    unsigned long total = e->explicit_layout ? 0
                                             : di_align_up(size, max_align);
    LLVMMetadataRef composite = LLVMDIBuilderCreateStructType(
        g->di_builder, /* Scope */ file, name, strlen(name),
        file, line, total * 8,
        (uint32_t)(max_align > 1 ? di_align_up(max_align, 8) : 0) * 8,
        LLVMDIFlagZero, /* DerivedFrom */ NULL, members, (unsigned)slot,
        /* RunTimeLang */ 0, /*VTableHolder*/ NULL, /*UniqueId*/ NULL, 0);
    free(members);
    return composite;
}

/* 底层系统交互与数据协议契约 */
static LLVMMetadataRef di_type_for_zan(zan_irgen_t *g, zan_type_t *t,
                                       zan_type_t *inst, int depth) {
    if (!g->emit_debug || !g->di_builder || !t || depth > 8) return NULL;
    switch (t->kind) {
    case TYPE_BOOL:
        return di_basic(g, "bool", 8, ZAN_DI_ATE_BOOLEAN);
    case TYPE_BYTE:
        return di_basic(g, "byte", 8, ZAN_DI_ATE_UCHAR);
    case TYPE_SBYTE:
        return di_basic(g, "sbyte", 8, ZAN_DI_ATE_SIGNED);
    case TYPE_SHORT:
        return di_basic(g, "short", 16, ZAN_DI_ATE_SIGNED);
    case TYPE_USHORT:
        return di_basic(g, "ushort", 16, ZAN_DI_ATE_UNSIGNED);
    case TYPE_INT:
        return di_basic(g, "int", 32, ZAN_DI_ATE_SIGNED);
    case TYPE_UINT:
        return di_basic(g, "uint", 32, ZAN_DI_ATE_UNSIGNED);
    case TYPE_LONG:
    case TYPE_NINT:
        return di_basic(g, "long", 64, ZAN_DI_ATE_SIGNED);
    case TYPE_ULONG:
        return di_basic(g, "ulong", 64, ZAN_DI_ATE_UNSIGNED);
    case TYPE_FLOAT:
        return di_basic(g, "float", 32, ZAN_DI_ATE_FLOAT);
    case TYPE_DOUBLE:
        return di_basic(g, "double", 64, ZAN_DI_ATE_FLOAT);
    case TYPE_CHAR:
        return di_basic(g, "char", 16, ZAN_DI_ATE_UTF);
    case TYPE_STRING: {
        /* 内部辅助逻辑 */
        LLVMMetadataRef byte = di_basic(g, "byte", 8, ZAN_DI_ATE_UCHAR);
        LLVMMetadataRef ptr = LLVMDIBuilderCreatePointerType(
            g->di_builder, byte, 64, 0, 0, "", 0);
        return LLVMDIBuilderCreateTypedef(g->di_builder, ptr, "string", 6,
                                          di_file_for(g, g->di_cur_file),
                                          1, /* Scope */ NULL, 0);
    }
    case TYPE_ARRAY:
        if (t->array_rank != 1) return di_byte_ptr(g);
        return di_array_composite(g, t, inst, depth);
    case TYPE_CLASS:
    case TYPE_INTERFACE:
        /* 内部辅助逻辑 */
        if (!t->sym) {
            LLVMMetadataRef bi = di_builtin_composite(g, t, depth);
            if (bi) return bi;
            return di_byte_ptr(g);
        }
        return di_class_composite(g, t, depth);
    case TYPE_STRUCT:
        return di_struct_composite(g, t, inst, depth);
    case TYPE_NULLABLE:
        return di_type_for_zan(g, t->element_type, inst, depth + 1);
    case TYPE_ENUM:
        return di_basic(g, "int", 32, ZAN_DI_ATE_SIGNED);
    case TYPE_TASK: {
        LLVMMetadataRef bi = di_builtin_composite(g, t, depth);
        if (bi) return bi;
        return di_byte_ptr(g);
    }
    case TYPE_DELEGATE:
    case TYPE_OBJECT:
        return di_byte_ptr(g);
    default:
        return NULL;
    }
}

/* 发射an llvm */
static void di_declare_var(zan_irgen_t *g, zan_istr_t name, LLVMValueRef storage,
                           zan_type_t *zt) {
    if (!g || !g->emit_debug || !g->builder) return;
    if (!storage) return;
    if (!LLVMIsAAllocaInst(storage) && !zt) return;
    if (name.len == 0 || !name.str) return;
    LLVMBasicBlockRef bb = LLVMGetInsertBlock(g->builder);
    if (!bb) return;
    LLVMMetadataRef sp = di_ensure_sp(g, g->di_cur_file, g->di_cur_line);
    if (!sp) return;
    LLVMMetadataRef ty = NULL;
    if (zt) ty = di_type_for_zan(g, zt, zt, 0);
    if (!ty && LLVMIsAAllocaInst(storage)) ty = di_type_from_llvm(g, LLVMGetAllocatedType(storage));
    if (!ty) return; /* 核心系统底层抽象与内存语义契约 */
    LLVMMetadataRef file = di_file_for(g, g->di_cur_file);
    unsigned line = g->di_cur_line ? g->di_cur_line : 1;
    LLVMMetadataRef var = LLVMDIBuilderCreateAutoVariable(
        g->di_builder, sp, name.str, name.len, file, line, ty,
        /* AlwaysPreserve */ 1, LLVMDIFlagZero, /*AlignInBits*/ 0);
    LLVMMetadataRef expr = LLVMDIBuilderCreateExpression(g->di_builder, NULL, 0);
    LLVMMetadataRef dl = LLVMDIBuilderCreateDebugLocation(g->ctx, line, 0, sp, NULL);
    /* 内部辅助实现 */
#if defined(LLVM_VERSION_MAJOR) && LLVM_VERSION_MAJOR < 19
    LLVMDIBuilderInsertDeclareAtEnd(g->di_builder, storage, var, expr, dl, bb);
#else
    LLVMDIBuilderInsertDeclareRecordAtEnd(g->di_builder, storage, var, expr, dl, bb);
#endif
}

/* 内部辅助实现 */
static zan_irgen_t *g_di_emit_ctx = NULL;

#include "../common/host_oom.h"
#include "../common/zan_abi.h"
/* 模块核心语义抽象与接口调用契约 */

static bool types_equal(zan_type_t *a, zan_type_t *b);
static LLVMValueRef zan_call2(LLVMBuilderRef b, LLVMTypeRef ty, LLVMValueRef fn,
                              LLVMValueRef *args, unsigned n, const char *nm);
static void emit_weak_runtime(zan_irgen_t *g);

/* 核心系统底层抽象与内存语义契约 */
static zan_irgen_t *s_current_irgen = NULL;

/* 内部辅助逻辑 */
static LLVMValueRef zan_hdr_is_string(zan_irgen_t *g, LLVMValueRef word,
                                     const char *nm) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef tag = LLVMBuildLShr(g->builder, word,
                                     LLVMConstInt(i64, 32, 0), "hdr.tag");
    return zan_icmp(g->builder, LLVMIntEQ, tag,
                    LLVMConstInt(i64, ZAN_STRING_TAG, 0), nm);
}

/* 内部辅助逻辑 */
static LLVMValueRef zan_hdr_read_ok(zan_irgen_t *g, LLVMValueRef obj) {
    if (!g->target_is_windows) return NULL;
    LLVMTypeRef i1t = LLVMInt1TypeInContext(g->ctx);
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8p = LLVMPointerType(i8t, 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef args[] = { i8p, i64t };
    LLVMTypeRef fnty = LLVMFunctionType(i32t, args, 2, 0);
    LLVMValueRef isbad = LLVMGetNamedFunction(g->mod, "IsBadReadPtr");
    if (!isbad) isbad = LLVMAddFunction(g->mod, "IsBadReadPtr", fnty);
    LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->builder);
    LLVMValueRef fn = LLVMGetBasicBlockParent(entry_bb);
    LLVMBasicBlockRef probe_bb = LLVMAppendBasicBlockInContext(g->ctx, fn,
        "hdr.probe");
    LLVMBasicBlockRef join_bb = LLVMAppendBasicBlockInContext(g->ctx, fn,
        "hdr.join");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(LLVMTypeOf(obj)),
                 "hdr.isnull"),
        join_bb, probe_bb);
    LLVMPositionBuilderAtEnd(g->builder, probe_bb);
    LLVMValueRef ptr8 = LLVMBuildGEP2(g->builder, i8t, obj,
        &(LLVMValueRef){ LLVMConstInt(i64t, (uint64_t)ZAN_OBJ_SITE_OFF, 1) },
        1, "hdr.ptr8");
    LLVMValueRef cargs[] = { ptr8, LLVMConstInt(i64t, 8, 0) };
    LLVMValueRef bad = zan_call2(g->builder, fnty, isbad, cargs, 2,
        "hdr.badread");
    LLVMValueRef ok = zan_icmp(g->builder, LLVMIntEQ, bad,
        LLVMConstInt(i32t, 0, 0), "hdr.readok");
    LLVMBuildBr(g->builder, join_bb);
    LLVMPositionBuilderAtEnd(g->builder, join_bb);
    LLVMValueRef phi = LLVMBuildPhi(g->builder, i1t, "hdr.ok");
    LLVMValueRef vals[] = { LLVMConstInt(i1t, 0, 0), ok };
    LLVMBasicBlockRef preds[] = { entry_bb, probe_bb };
    LLVMAddIncoming(phi, vals, preds, 2);
    return phi;
}

/* 内部辅助实现 */
static LLVMTypeRef arc_desc_type(zan_irgen_t *g) {
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    return LLVMStructType((LLVMTypeRef[]){ i8p, i8p, i8p, i64t }, 4, 0);
}

static LLVMValueRef create_arc_desc(zan_irgen_t *g, int site_idx) {
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef dt = arc_desc_type(g);
    char dname[64];
    snprintf(dname, sizeof(dname), "__zan_desc_%d", site_idx);
    LLVMValueRef dg = LLVMAddGlobal(g->mod, dt, dname);
    LLVMSetLinkage(dg, LLVMInternalLinkage);
    LLVMSetGlobalConstant(dg, 1);
    /* 内部辅助实现 */
    LLVMValueRef z = LLVMConstNull(i8p);
    LLVMSetInitializer(dg, LLVMConstNamedStruct(dt,
        (LLVMValueRef[]){ z, z, z, LLVMConstInt(i64t, 0, 0) }, 4));
    g->desc_gv[site_idx] = dg;
    return dg;
}

/* 内部辅助逻辑 */
static bool site_arrays_reserve(zan_irgen_t *g, int want) {
    if (want <= g->leak_site_cap) return true;
    int newcap = g->leak_site_cap ? g->leak_site_cap * 2 : 256;
    while (newcap < want) newcap *= 2;
    zan_symbol_t **ns = (zan_symbol_t **)realloc(g->site_syms,
        (size_t)newcap * sizeof(zan_symbol_t *));
    if (ns) g->site_syms = ns;
    int *nc = (int *)realloc(g->site_coll, (size_t)newcap * sizeof(int));
    if (nc) g->site_coll = nc;
    zan_type_t **nce = (zan_type_t **)realloc(g->site_coll_elem,
        (size_t)newcap * sizeof(zan_type_t *));
    if (nce) g->site_coll_elem = nce;
    zan_type_t **ni = (zan_type_t **)realloc(g->site_inst,
        (size_t)newcap * sizeof(zan_type_t *));
    if (ni) g->site_inst = ni;
    uint32_t *nlf = (uint32_t *)realloc(g->site_loc_file,
        (size_t)newcap * sizeof(uint32_t));
    if (nlf) g->site_loc_file = nlf;
    uint32_t *nll = (uint32_t *)realloc(g->site_loc_line,
        (size_t)newcap * sizeof(uint32_t));
    if (nll) g->site_loc_line = nll;
    if (!ns || !nc || !nce || !ni || !nlf || !nll) return false;
    memset(g->site_syms + g->leak_site_cap, 0,
        (size_t)(newcap - g->leak_site_cap) * sizeof(zan_symbol_t *));
    memset(g->site_coll + g->leak_site_cap, 0,
        (size_t)(newcap - g->leak_site_cap) * sizeof(int));
    memset(g->site_coll_elem + g->leak_site_cap, 0,
        (size_t)(newcap - g->leak_site_cap) * sizeof(zan_type_t *));
    memset(g->site_inst + g->leak_site_cap, 0,
        (size_t)(newcap - g->leak_site_cap) * sizeof(zan_type_t *));
    memset(g->site_loc_file + g->leak_site_cap, 0,
        (size_t)(newcap - g->leak_site_cap) * sizeof(uint32_t));
    memset(g->site_loc_line + g->leak_site_cap, 0,
        (size_t)(newcap - g->leak_site_cap) * sizeof(uint32_t));
    g->leak_site_cap = newcap;
    return true;
}

static bool desc_gv_reserve(zan_irgen_t *g, int want) {
    if (want <= g->desc_gv_cap) return true;
    int newcap = g->desc_gv_cap ? g->desc_gv_cap * 2 : 256;
    while (newcap < want) newcap *= 2;
    LLVMValueRef *nd = (LLVMValueRef *)realloc(g->desc_gv,
        (size_t)newcap * sizeof(LLVMValueRef));
    if (!nd) return false;
    g->desc_gv = nd;
    memset(g->desc_gv + g->desc_gv_cap, 0,
        (size_t)(newcap - g->desc_gv_cap) * sizeof(LLVMValueRef));
    g->desc_gv_cap = newcap;
    return true;
}

static int reserve_arc_site(zan_irgen_t *g, zan_symbol_t *sym,
                            zan_type_t *inst, int coll_kind,
                            zan_type_t *coll_elem) {
    /* 内部辅助实现 */
    uint32_t key_file = g->check_leaks ? g->di_cur_file : 0;
    uint32_t key_line = g->check_leaks ? g->di_cur_line : 0;
    for (int i = 0; i < g->leak_site_count; i++) {
        int existing_kind = g->site_coll ? g->site_coll[i] : 0;
        if (existing_kind != coll_kind) continue;
        if (g->site_loc_file && g->site_loc_file[i] != key_file) continue;
        if (g->site_loc_line && g->site_loc_line[i] != key_line) continue;
        if (coll_kind != 0) {
            zan_type_t *existing_elem = g->site_coll_elem
                ? g->site_coll_elem[i] : NULL;
            if (types_equal(existing_elem, coll_elem)) return i;
        } else {
            zan_symbol_t *existing_sym = g->site_syms ? g->site_syms[i] : NULL;
            zan_type_t *existing_inst = g->site_inst ? g->site_inst[i] : NULL;
            if (existing_sym == sym && types_equal(existing_inst, inst)) return i;
        }
    }
    /* 内部辅助实现 */
    if (!site_arrays_reserve(g, g->leak_site_count + 1) ||
        (g->desc_hdr && !desc_gv_reserve(g, g->leak_site_count + 1))) {
        fprintf(stderr, "zanc: out of memory growing ARC site tables\n");
        exit(1);
    }
    int site_idx = g->leak_site_count++;
    if (g->site_syms) g->site_syms[site_idx] = sym;
    if (g->site_inst) g->site_inst[site_idx] = inst;
    if (g->site_coll) g->site_coll[site_idx] = coll_kind;
    if (g->site_coll_elem) g->site_coll_elem[site_idx] = coll_elem;
    if (g->site_loc_file) g->site_loc_file[site_idx] = key_file;
    if (g->site_loc_line) g->site_loc_line[site_idx] = key_line;
    if (g->desc_hdr) create_arc_desc(g, site_idx);
    return site_idx;
}

/* 内部辅助实现 */
static int reserve_closure_site(zan_irgen_t *g) {
    if (!site_arrays_reserve(g, g->leak_site_count + 1) ||
        (g->desc_hdr && !desc_gv_reserve(g, g->leak_site_count + 1))) {
        fprintf(stderr, "zanc: out of memory growing ARC site tables\n");
        exit(1);
    }
    int site_idx = g->leak_site_count++;
    if (g->site_syms) g->site_syms[site_idx] = NULL;
    if (g->site_inst) g->site_inst[site_idx] = NULL;
    if (g->site_coll) g->site_coll[site_idx] = 0;
    if (g->site_coll_elem) g->site_coll_elem[site_idx] = NULL;
    if (g->desc_hdr) {
        /* 内部辅助实现 */
        create_arc_desc(g, site_idx);
    }
    return site_idx;
}

/* 内部辅助实现 */
static LLVMValueRef arc_site_arg(zan_irgen_t *g, int site_idx) {
    if (!g->desc_hdr)
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx),
                            (unsigned long long)site_idx, 0);
    return LLVMBuildPtrToInt(g->builder, g->desc_gv[site_idx],
                             LLVMInt64TypeInContext(g->ctx), "desc.arg");
}

/* 内部辅助逻辑 */

/* ---- initialization ---- */

/* 模块核心语义抽象与接口调用契约 */

/* 内部辅助实现 */
static LLVMValueRef zan_call2(LLVMBuilderRef b, LLVMTypeRef ty, LLVMValueRef fn,
                              LLVMValueRef *args, unsigned n, const char *nm) {
    LLVMValueRef callee = fn;
    if (fn && LLVMIsAFunction(fn) && LLVMGlobalGetValueType(fn) != ty) {
        fn = LLVMConstBitCast(fn, LLVMPointerType(ty, 0));
    } else if (fn && !LLVMIsAFunction(fn) &&
               LLVMGetTypeKind(LLVMTypeOf(fn)) == LLVMPointerTypeKind &&
               LLVMTypeOf(fn) != LLVMPointerType(ty, 0)) {
        /* 内部辅助实现 */
        fn = LLVMBuildBitCast(b, fn, LLVMPointerType(ty, 0), "callee.fp");
    }
    /* 内部辅助实现 */
    if (false && s_current_irgen && s_current_irgen->target_is_wasm &&
        s_current_irgen->wasm_try_depth > 0 &&
        !s_current_irgen->in_wasm_throw_op &&
        !(fn && LLVMIsAFunction(fn) && fn == s_current_irgen->wasm_eh_state_fn)) {
        zan_irgen_t *g = s_current_irgen;
        LLVMBasicBlockRef cur = LLVMGetInsertBlock(b);
        LLVMValueRef parent = LLVMGetBasicBlockParent(cur);
        LLVMBasicBlockRef lpad = g->wasm_lpad_stack[g->wasm_try_depth - 1];
        /* 内部辅助实现 */
        if (!lpad ||
            LLVMGetBasicBlockParent(cur) != LLVMGetBasicBlockParent(lpad)) {
            LLVMValueRef call = LLVMBuildCall2(b, ty, fn, args, n, nm);
            /* 模块核心语义抽象与接口调用契约 */
            if (callee && LLVMIsAFunction(callee)) {
                static const char *ext[2] = { "signext", "zeroext" };
                for (int e = 0; e < 2; e++) {
                    unsigned kind = LLVMGetEnumAttributeKindForName(ext[e], strlen(ext[e]));
                    if (!kind) continue;
                    for (unsigned idx = 0; idx <= n; idx++) {
                        unsigned at = idx == 0 ? (unsigned)LLVMAttributeReturnIndex : idx;
                        if (!LLVMGetEnumAttributeAtIndex(callee, at, kind)) continue;
                        LLVMAddCallSiteAttribute(call, at,
                            LLVMCreateEnumAttribute(LLVMGetTypeContext(ty), kind, 0));
                    }
                }
            }
            return call;
        }
        LLVMBasicBlockRef cont =
            LLVMAppendBasicBlockInContext(LLVMGetTypeContext(ty), parent, "invoke.cont");
        LLVMValueRef inv = LLVMBuildInvoke2(b, ty, fn, args, n, cont, lpad, nm);
        if (callee && LLVMIsAFunction(callee)) {
            static const char *ext[2] = { "signext", "zeroext" };
            for (int e = 0; e < 2; e++) {
                unsigned kind = LLVMGetEnumAttributeKindForName(ext[e], strlen(ext[e]));
                if (!kind) continue;
                for (unsigned idx = 0; idx <= n; idx++) {
                    unsigned at = idx == 0 ? (unsigned)LLVMAttributeReturnIndex : idx;
                    if (!LLVMGetEnumAttributeAtIndex(callee, at, kind)) continue;
                    LLVMAddCallSiteAttribute(inv, at,
                        LLVMCreateEnumAttribute(LLVMGetTypeContext(ty), kind, 0));
                }
            }
        }
        LLVMPositionBuilderAtEnd(b, cont);
        return inv;
    }
    LLVMValueRef call = LLVMBuildCall2(b, ty, fn, args, n, nm);
    /* 内部辅助实现 */
    if (callee && LLVMIsAFunction(callee)) {
        static const char *ext[2] = { "signext", "zeroext" };
        for (int e = 0; e < 2; e++) {
            unsigned kind = LLVMGetEnumAttributeKindForName(ext[e], strlen(ext[e]));
            if (!kind) continue;
            for (unsigned idx = 0; idx <= n; idx++) {
                unsigned at = idx == 0 ? (unsigned)LLVMAttributeReturnIndex : idx;
                if (!LLVMGetEnumAttributeAtIndex(callee, at, kind)) continue;
                LLVMAddCallSiteAttribute(call, at,
                    LLVMCreateEnumAttribute(LLVMGetTypeContext(ty), kind, 0));
            }
        }
    }
    return call;
}

/* 核心系统底层抽象与内存语义契约 */
static void zan_emit_frame_free(zan_irgen_t *g, LLVMValueRef frame_i8) {
    if (g->external_async_executor && g->rt_co_frame_free) {
        zan_call2(g->builder, g->rt_co_frame_free_type, g->rt_co_frame_free,
                  &frame_i8, 1, "");
        return;
    }
    zan_call2(g->builder, LLVMGlobalGetValueType(g->fn_free), g->fn_free,
              &frame_i8, 1, "");
}

/* 底层系统交互与数据协议契约 */
static void *irgen_grow(void *base, int *cap, int need, size_t elem) {
    if (need <= *cap) return base;
    int ncap = *cap ? *cap * 2 : 256;
    while (ncap < need) ncap *= 2;
    void *p = realloc(base, (size_t)ncap * elem);
    if (!p) {
        fprintf(stderr, "zanc: out of memory growing an IR registry\n");
        exit(1);
    }
    *cap = ncap;
    return p;
}

/* 底层系统交互与数据协议契约 */
static size_t irgen_ptr_bucket(const void *p, int cap) {
    uint64_t h = (uint64_t)(uintptr_t)p * 0x9E3779B97F4A7C15ull;
    return (size_t)((h >> 32) & (uint64_t)(cap - 1));
}

/* 底层系统交互与数据协议契约 */

static size_t fn_index_hash(zan_symbol_t *sym, int cap) {
    return irgen_ptr_bucket(sym, cap);
}

/* 模块核心语义抽象与接口调用契约 */
static void fn_index_put(zan_irgen_t *g, zan_symbol_t *sym, int idx) {
    size_t i = fn_index_hash(sym, g->fn_index_cap);
    while (g->fn_index[i].sym) {
        if (g->fn_index[i].sym == sym) return;
        i = (i + 1) & (size_t)(g->fn_index_cap - 1);
    }
    g->fn_index[i].sym = sym;
    g->fn_index[i].idx = idx;
}

/* 模块核心语义抽象与接口调用契约 */
static void fn_index_rehash(zan_irgen_t *g, int ncap) {
    struct zan_fn_index_slot *slots =
        calloc((size_t)ncap, sizeof(*g->fn_index));
    if (!slots) {
        fprintf(stderr, "zanc: out of memory growing the function index\n");
        exit(1);
    }
    free(g->fn_index);
    g->fn_index = slots;
    g->fn_index_cap = ncap;
    for (int i = 0; i < g->function_count; i++)
        if (g->functions[i].sym) fn_index_put(g, g->functions[i].sym, i);
}

/* 底层系统交互与数据协议契约 */
static int irgen_find_function(zan_irgen_t *g, zan_symbol_t *sym) {
    if (!sym || !g->fn_index) return -1;
    size_t i = fn_index_hash(sym, g->fn_index_cap);
    while (g->fn_index[i].sym) {
        if (g->fn_index[i].sym == sym) return g->fn_index[i].idx;
        i = (i + 1) & (size_t)(g->fn_index_cap - 1);
    }
    return -1;
}

static void irgen_register_function(zan_irgen_t *g, zan_symbol_t *sym,
                                    LLVMValueRef fn, LLVMTypeRef fn_type) {
    if (g->function_count >= g->function_cap) {
        int ncap = g->function_cap ? g->function_cap * 2 : 1024;
        g->functions = realloc(g->functions,
                               (size_t)ncap * sizeof(*g->functions));
        g->function_cap = ncap;
    }
    g->functions[g->function_count].sym = sym;
    g->functions[g->function_count].fn = fn;
    g->functions[g->function_count].fn_type = fn_type;
    g->functions[g->function_count].modifiers = sym ? sym->modifiers : 0;
    /* 模块核心语义抽象与接口调用契约 */
    if ((g->function_count + 1) * 2 >= g->fn_index_cap)
        fn_index_rehash(g, g->fn_index_cap ? g->fn_index_cap * 2 : 2048);
    if (sym) fn_index_put(g, sym, g->function_count);
    g->function_count++;
}

/* 内部辅助实现 */

typedef struct {
    zan_symbol_t *owner;
    int built_count;    /* 模块核心语义抽象与接口调用契约 */
    int cap;            /* 核心系统底层抽象与内存语义契约 */
    int *name_buckets;  /* 核心系统底层抽象与内存语义契约 */
    int *name_next;     /* 底层系统交互与数据协议契约 */
    int *decl_buckets;  /* 核心系统底层抽象与内存语义契约 */
    int *decl_next;     /* 底层系统交互与数据协议契约 */
} zan_class_index_t;

static zan_class_index_t *g_class_indexes;
static int g_class_index_cap;   /* slots, a power of two */
static int g_class_index_count;

static uint64_t istr_hash(zan_istr_t s) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < s.len; i++) {
        h ^= (unsigned char)s.str[i];
        h *= 1099511628211ull;
    }
    return h;
}

static void *class_index_alloc(size_t n, size_t elem) {
    void *p = malloc(n * elem);
    if (!p) {
        fprintf(stderr, "zanc: out of memory building a member index\n");
        exit(1);
    }
    return p;
}

static void class_index_free(zan_class_index_t *ci) {
    free(ci->name_buckets);
    free(ci->name_next);
    free(ci->decl_buckets);
    free(ci->decl_next);
    ci->name_buckets = ci->name_next = ci->decl_buckets = ci->decl_next = NULL;
}

static void class_index_build(zan_class_index_t *ci, zan_symbol_t *owner) {
    class_index_free(ci);
    int n = owner->member_count;
    int cap = 16;
    while (cap < n * 2) cap *= 2;
    ci->owner = owner;
    ci->built_count = n;
    ci->cap = cap;
    ci->name_buckets = class_index_alloc((size_t)cap, sizeof(int));
    ci->decl_buckets = class_index_alloc((size_t)cap, sizeof(int));
    ci->name_next = class_index_alloc((size_t)(n > 0 ? n : 1), sizeof(int));
    ci->decl_next = class_index_alloc((size_t)(n > 0 ? n : 1), sizeof(int));
    for (int i = 0; i < cap; i++) {
        ci->name_buckets[i] = -1;
        ci->decl_buckets[i] = -1;
    }
    /* 模块核心语义抽象与接口调用契约 */
    for (int i = n - 1; i >= 0; i--) {
        zan_symbol_t *m = owner->members[i];
        size_t nb = (size_t)(istr_hash(m->name) & (uint64_t)(cap - 1));
        ci->name_next[i] = ci->name_buckets[nb];
        ci->name_buckets[nb] = i;
        size_t db = irgen_ptr_bucket(m->decl, cap);
        ci->decl_next[i] = ci->decl_buckets[db];
        ci->decl_buckets[db] = i;
    }
}

static void class_index_table_grow(void) {
    int ncap = g_class_index_cap ? g_class_index_cap * 2 : 256;
    zan_class_index_t *slots = calloc((size_t)ncap, sizeof(*slots));
    if (!slots) {
        fprintf(stderr, "zanc: out of memory growing the member index table\n");
        exit(1);
    }
    for (int i = 0; i < g_class_index_cap; i++) {
        if (!g_class_indexes[i].owner) continue;
        size_t j = irgen_ptr_bucket(g_class_indexes[i].owner, ncap);
        while (slots[j].owner) j = (j + 1) & (size_t)(ncap - 1);
        slots[j] = g_class_indexes[i];
    }
    free(g_class_indexes);
    g_class_indexes = slots;
    g_class_index_cap = ncap;
}

static void class_index_reset(void) {
    for (int i = 0; i < g_class_index_cap; i++)
        if (g_class_indexes[i].owner) class_index_free(&g_class_indexes[i]);
    free(g_class_indexes);
    g_class_indexes = NULL;
    g_class_index_cap = g_class_index_count = 0;
}

static zan_class_index_t *class_index_for(zan_symbol_t *owner) {
    if ((g_class_index_count + 1) * 2 >= g_class_index_cap)
        class_index_table_grow();
    size_t i = irgen_ptr_bucket(owner, g_class_index_cap);
    while (g_class_indexes[i].owner && g_class_indexes[i].owner != owner)
        i = (i + 1) & (size_t)(g_class_index_cap - 1);
    zan_class_index_t *ci = &g_class_indexes[i];
    if (ci->owner != owner) {
        class_index_build(ci, owner);
        g_class_index_count++;
    } else if (ci->built_count != owner->member_count) {
        class_index_build(ci, owner);
    }
    return ci;
}

/* 模块核心语义抽象与接口调用契约 */
static int member_first_named(zan_class_index_t *ci, zan_istr_t name) {
    return ci->name_buckets[istr_hash(name) & (uint64_t)(ci->cap - 1)];
}

static int member_next_named(zan_class_index_t *ci, int i) {
    return ci->name_next[i];
}

static bool member_name_is(zan_symbol_t *m, zan_istr_t name) {
    return m->name.len == name.len &&
           memcmp(m->name.str, name.str, (size_t)name.len) == 0;
}

/* Defined in irgen_generics */
static void emit_fatal_report(zan_irgen_t *g, LLVMValueRef text, int exit_code);

/* 内部辅助实现 */
void zan_irgen_emit_oom_check(zan_irgen_t *g, LLVMValueRef fn, LLVMValueRef raw) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, raw,
        LLVMConstPointerNull(i8ptr), "oom");
    LLVMBasicBlockRef oom_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "oom");
    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "oom.ok");
    LLVMBuildCondBr(g->builder, isnull, oom_bb, ok_bb);
    LLVMPositionBuilderAtEnd(g->builder, oom_bb);
    /* 内部辅助逻辑 */
    LLVMValueRef text = zan_irgen_intern_string(g, "out of memory\n");
    emit_fatal_report(g, text, 1);
    LLVMPositionBuilderAtEnd(g->builder, ok_bb);
}

/* 内部辅助逻辑 */
static void emit_leak_counter_add(zan_irgen_t *g, LLVMValueRef ptr, long long delta) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMBuildAtomicRMW(g->builder,
        delta < 0 ? LLVMAtomicRMWBinOpSub : LLVMAtomicRMWBinOpAdd, ptr,
        LLVMConstInt(i64, (unsigned long long)(delta < 0 ? -delta : delta), 0),
        LLVMAtomicOrderingMonotonic, 0);
}

/* 内部辅助逻辑 */
#define ZAN_ARC_FAULT_RETAIN      0xE0A2C001u
#define ZAN_ARC_FAULT_RELEASE     0xE0A2C002u
#define ZAN_ARC_FAULT_STR_RETAIN  0xE0A2C003u
#define ZAN_ARC_FAULT_STR_RELEASE 0xE0A2C004u
#define ZAN_ARC_FAULT_USE_OBJ     0xE0A2C005u
#define ZAN_ARC_FAULT_USE_STR     0xE0A2C006u
#define ZAN_ARC_FAULT_ARR_RETAIN  0xE0A2C007u
#define ZAN_ARC_FAULT_ARR_RELEASE 0xE0A2C008u
#define ZAN_ARC_FAULT_USE_ARR     0xE0A2C009u

/* 模块核心语义抽象与接口调用契约 */
#define ZAN_ARC_FREED_MARK 0xDEAD0000DEAD0000ull

/* 内部辅助实现 */
static void emit_arc_fault_report(zan_irgen_t *g, LLVMValueRef obj,
                                  LLVMValueRef rc_old, LLVMValueRef site,
                                  unsigned code, const char *what) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    char msg[192];
    snprintf(msg, sizeof(msg),
             "zan runtime: ARC integrity failure: %s (refcount %%lld, "
             "object %%p, site/freed-by 0x%%llX)\n", what);
    LLVMValueRef fmt = zan_irgen_intern_string(g, msg);
    LLVMValueRef pargs[] = { fmt, rc_old, obj,
                             site ? site : LLVMConstInt(i64t, -1, 1) };
    zan_call2(g->builder, g->printf_type, g->fn_printf, pargs, 4, "");

    if (g->target_is_windows) {
        /* RaiseException(code, EXCEPTION_NONCONTINUABLE, 3, args) */
        LLVMTypeRef re_args[] = { i32t, i32t, i32t, LLVMPointerType(i64t, 0) };
        LLVMTypeRef re_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                             re_args, 4, 0);
        LLVMValueRef re = LLVMGetNamedFunction(g->mod, "RaiseException");
        if (!re) re = LLVMAddFunction(g->mod, "RaiseException", re_ty);
        LLVMValueRef slots = LLVMBuildArrayAlloca(g->builder, i64t,
            LLVMConstInt(i32t, 3, 0), "arcargs");
        LLVMValueRef objn = LLVMBuildPtrToInt(g->builder, obj, i64t, "objn");
        LLVMValueRef vals[3] = { objn, rc_old,
                                 site ? site : LLVMConstInt(i64t, -1, 1) };
        for (int i = 0; i < 3; i++) {
            LLVMValueRef idx = LLVMConstInt(i32t, (unsigned long long)i, 0);
            LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64t, slots, &idx, 1, "arcslot");
            LLVMBuildStore(g->builder, vals[i], slot);
        }
        LLVMValueRef cargs[] = { LLVMConstInt(i32t, code, 0),
                                 LLVMConstInt(i32t, 1 /* NONCONTINUABLE */, 0),
                                 LLVMConstInt(i32t, 3, 0), slots };
        zan_call2(g->builder, re_ty, re, cargs, 4, "");
    }
    LLVMValueRef exit_code = LLVMConstInt(i32t, 70, 0);
    zan_call2(g->builder, g->exit_type, g->fn_exit, &exit_code, 1, "");
    LLVMBuildUnreachable(g->builder);
}

/* Defined in irgen_arc */
static LLVMValueRef get_arc_free_decl(zan_irgen_t *g);

static void emit_arc_underflow_check(zan_irgen_t *g, LLVMValueRef fn,
                                    LLVMValueRef rc_old, LLVMValueRef obj,
                                    LLVMValueRef site, unsigned code,
                                    const char *what) {
    /* 内部辅助逻辑 */
    if (g->arc_guard) {
        if (!g->runtime_checks) return;
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
        LLVMValueRef dead = zan_icmp(g->builder, LLVMIntSLE, rc_old,
            LLVMConstInt(i64t, 0, 0), "arcdead");
        LLVMBasicBlockRef bad_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "arc.bad");
        LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "arc.ok");
        LLVMBuildCondBr(g->builder, dead, bad_bb, ok_bb);
        LLVMPositionBuilderAtEnd(g->builder, bad_bb);
        emit_arc_fault_report(g, obj, rc_old, site, code, what);
        LLVMPositionBuilderAtEnd(g->builder, ok_bb);
        return;
    }
    /* 内部辅助实现 */
    if (!g->arc_net) return;
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef dead = zan_icmp(g->builder, LLVMIntSLE, rc_old,
        LLVMConstInt(i64t, 0, 0), "arcdead");
    LLVMBasicBlockRef bad_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "arcnet.bad");
    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "arcnet.ok");
    LLVMBuildCondBr(g->builder, dead, bad_bb, ok_bb);
    LLVMPositionBuilderAtEnd(g->builder, bad_bb);
    if (g->rt_guard_split) {
        LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef note_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                               (LLVMTypeRef[]){ i8p, i8p }, 2, 0);
        LLVMValueRef note_fn = LLVMGetNamedFunction(g->mod, "zan_rt_soft_note2");
        if (!note_fn) note_fn = LLVMAddFunction(g->mod, "zan_rt_soft_note2",
                                                note_ty);
        LLVMValueRef prefix = zan_irgen_intern_string(g, what);
        LLVMValueRef msg = zan_irgen_intern_string(g,
            " [ARC over-release net: object leaked, execution continues]\n");
        LLVMValueRef nargs[] = { prefix, msg };
        zan_call2(g->builder, note_ty, note_fn, nargs, 2, "");
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s [ARC over-release net: object leaked, "
                 "execution continues]\n", what);
        LLVMValueRef text = zan_irgen_intern_string(g, buf);
        LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef note_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                               (LLVMTypeRef[]){ i8p }, 1, 0);
        LLVMValueRef note_fn = LLVMGetNamedFunction(g->mod, "zan_rt_soft_note");
        if (!note_fn) note_fn = LLVMAddFunction(g->mod, "zan_rt_soft_note",
                                                note_ty);
        zan_call2(g->builder, note_ty, note_fn, &text, 1, "");
    }
    LLVMBuildBr(g->builder, ok_bb);
    LLVMPositionBuilderAtEnd(g->builder, ok_bb);
}

/* 内部辅助实现 */
static void emit_arc_freed_use_check(zan_irgen_t *g, LLVMValueRef fn,
                                    LLVMValueRef rc, LLVMValueRef obj,
                                    unsigned code, const char *what) {
    if (!g->arc_guard) return;
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMValueRef freed = zan_icmp(g->builder, LLVMIntEQ, rc,
        LLVMConstInt(i64t, ZAN_ARC_FREED_MARK, 0), "arcfreed");
    LLVMBasicBlockRef bad_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "uaf.bad");
    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "uaf.ok");
    LLVMBuildCondBr(g->builder, freed, bad_bb, ok_bb);
    LLVMPositionBuilderAtEnd(g->builder, bad_bb);
    /* 内部辅助实现 */
    LLVMValueRef sp = LLVMBuildGEP2(g->builder, i8t, obj,
        (LLVMValueRef[]){ LLVMConstInt(i64t, (uint64_t)ZAN_OBJ_SITE_OFF, 1) }, 1, "uafsp");
    LLVMValueRef sip = LLVMBuildBitCast(g->builder, sp, LLVMPointerType(i64t, 0), "uafsip");
    LLVMValueRef freed_by = LLVMBuildLoad2(g->builder, i64t, sip, "freedby");
    emit_arc_fault_report(g, obj, LLVMConstInt(i64t, 0, 0), freed_by, code, what);
    LLVMPositionBuilderAtEnd(g->builder, ok_bb);
}

/* 内部辅助逻辑 */
static void emit_arc_quarantine(zan_irgen_t *g, LLVMValueRef obj,
                               LLVMValueRef rc_iptr) {
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8p = LLVMPointerType(i8t, 0);
    LLVMBuildStore(g->builder, LLVMConstInt(i64t, ZAN_ARC_FREED_MARK, 0), rc_iptr);
    LLVMBuildStore(g->builder, LLVMConstInt(i8t, 0xDD, 0), obj);
    /* 内部辅助实现 */
    LLVMTypeRef ra_ty = LLVMFunctionType(i8p, &i32t, 1, 0);
    LLVMValueRef ra_fn = LLVMGetNamedFunction(g->mod, "llvm.returnaddress");
    if (!ra_fn) ra_fn = LLVMAddFunction(g->mod, "llvm.returnaddress", ra_ty);
    LLVMValueRef zero = LLVMConstInt(i32t, 0, 0);
    LLVMValueRef ra = zan_call2(g->builder, ra_ty, ra_fn, &zero, 1, "retaddr");
    LLVMValueRef sp = LLVMBuildGEP2(g->builder, i8t, obj,
        (LLVMValueRef[]){ LLVMConstInt(i64t, (uint64_t)ZAN_OBJ_SITE_OFF, 1) }, 1, "qsp");
    LLVMValueRef sip = LLVMBuildBitCast(g->builder, sp, LLVMPointerType(i64t, 0), "qsip");
    LLVMBuildStore(g->builder, LLVMBuildPtrToInt(g->builder, ra, i64t, "ran"), sip);
}

/* 内部辅助实现 */
static void emit_header_read_guard(zan_irgen_t *g, LLVMValueRef fn,
                                   LLVMValueRef ptr8, LLVMBasicBlockRef ret_bb) {
    if (!g->target_is_windows) return;
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef args[] = { i8p, i64t };
    LLVMTypeRef fnty = LLVMFunctionType(i32t, args, 2, 0);
    LLVMValueRef isbad = LLVMGetNamedFunction(g->mod, "IsBadReadPtr");
    if (!isbad) isbad = LLVMAddFunction(g->mod, "IsBadReadPtr", fnty);
    LLVMValueRef cargs[] = { ptr8, LLVMConstInt(i64t, 8, 0) };
    LLVMValueRef bad = zan_call2(g->builder, fnty, isbad, cargs, 2, "badread");
    LLVMValueRef isbadnz = zan_icmp(g->builder, LLVMIntNE, bad,
        LLVMConstInt(i32t, 0, 0), "isbadnz");
    LLVMBasicBlockRef readable_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "readable");
    LLVMBuildCondBr(g->builder, isbadnz, ret_bb, readable_bb);
    LLVMPositionBuilderAtEnd(g->builder, readable_bb);
}

/* 内部辅助实现 */
static void emit_arc_trace_call(zan_irgen_t *g, LLVMValueRef ev_fn,
                                const char *tag, LLVMValueRef obj,
                                LLVMValueRef rc, LLVMValueRef site) {
    if (!ev_fn) return;
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef ra_ty = LLVMFunctionType(i8p, (LLVMTypeRef[]){ i32t }, 1, 0);
    LLVMValueRef ra_fn = LLVMGetNamedFunction(g->mod, "llvm.returnaddress");
    if (!ra_fn) return;
    LLVMValueRef ra_arg = LLVMConstInt(i32t, 0, 0);
    LLVMValueRef ra = zan_call2(g->builder, ra_ty, ra_fn, &ra_arg, 1, "tra");
    LLVMValueRef tagc = zan_irgen_intern_string(g, tag);
    LLVMValueRef args[5] = { tagc, obj, rc, site, ra };
    zan_call2(g->builder, LLVMGlobalGetValueType(ev_fn), ev_fn, args, 5, "");
}

zan_status_t zan_irgen_init(zan_irgen_t *g, zan_arena_t *arena,
                            zan_diag_t *diag, zan_binder_t *binder,
                            const char *module_name,
                            const char *target_triple,
                            bool target_is_windows, bool external_async_executor,
                            bool check_leaks, bool runtime_checks,
                            bool arc_guard, bool arc_net) {
    memset(g, 0, sizeof(*g));
    class_index_reset();
    s_current_irgen = g;
    g->arena = arena;
    g->diag = diag;
    g->binder = binder;
    /* 内部辅助实现 */
    g->runtime_checks = runtime_checks;
    g->arc_guard = arc_guard;
    g->arc_net = arc_net;
    g->check_leaks = check_leaks;
    /* 模块核心语义抽象与接口调用契约 */
    if (target_triple && target_triple[0])
        snprintf(g->target_triple, sizeof(g->target_triple), "%s", target_triple);
    g->target_is_windows = target_is_windows;
    if (g->target_triple[0]) {
        g->target_is_macos = strstr(g->target_triple, "apple") != NULL
                             || strstr(g->target_triple, "darwin") != NULL;
        g->target_is_wasm = strstr(g->target_triple, "wasm") != NULL;
    } else {
#ifdef __APPLE__
        g->target_is_macos = true;
#endif
    }
    /* 内部辅助逻辑 */
    g->external_async_executor = external_async_executor;
    g->has_async_work = false;

    /* 模块核心语义抽象与接口调用契约 */
    {
        static const unsigned char zan_obf_pad[16] = {
            0x5a, 0x67, 0xa3, 0x1c, 0xd9, 0x84, 0x2f, 0x70,
            0xbe, 0x11, 0x4d, 0xe6, 0x93, 0x28, 0xc5, 0x7a
        };
        uint64_t h = 0xcbf29ce484222325ULL; /* 核心系统底层抽象与内存语义契约 */
        const char *seeds[2] = { module_name ? module_name : "",
                                 g->target_triple };
        for (int s = 0; s < 2; s++) {
            for (const char *p = seeds[s]; p && *p; p++) {
                h ^= (unsigned char)*p;
                h *= 0x100000001b3ULL;
            }
            h ^= 0x9e3779b97f4a7c15ULL;
            h *= 0x100000001b3ULL;
        }
        for (int i = 0; i < 16; i++) {
            uint64_t mix = h + (uint64_t)i * 0x9e3779b97f4a7c15ULL;
            mix ^= mix >> 29;
            mix *= 0xbf58476d1ce4e5b9ULL;
            mix ^= mix >> 32;
            g->obf_key[i] = (unsigned char)(zan_obf_pad[i] ^ (unsigned char)mix);
        }
    }
    g->catch_cleanups = NULL;
    g->catch_cleanup_count = 0;
    g->catch_cleanup_cap = 0;
    g->obfuscate_strings = false;
    g->obf_literals = NULL;
    g->obf_literal_count = 0;
    g->obf_literal_cap = 0;
    g->string_literals = NULL;
    g->string_literal_count = 0;
    g->string_literal_cap = 0;

    g->ctx = LLVMContextCreate();
    g->mod = LLVMModuleCreateWithNameInContext(module_name, g->ctx);
    g->builder = LLVMCreateBuilderInContext(g->ctx);

    /* 内部辅助逻辑 */
    g->emit_debug = false;
    g->di_builder = NULL;
    g->di_cu = NULL;
    g->di_files = NULL;
    g->di_file_cap = 0;
    g->di_cur_line = 0;
    g->di_cur_file = 0;

    /* 核心系统底层抽象与内存语义契约 */
    g->src_file = module_name;

    /* 内部辅助实现 */
    g->g_live = LLVMAddGlobal(g->mod, LLVMInt64TypeInContext(g->ctx), "__zan_live");
    LLVMSetInitializer(g->g_live, LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0));
    LLVMSetLinkage(g->g_live, LLVMInternalLinkage);

    g->leak_site_count = 0;
    /* 内部辅助实现 */
    g->desc_hdr = !check_leaks;
    g->desc_gv_cap = 0;
    g->leak_site_cap = 256;
    {
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        if (!g->desc_hdr) {
            /* 内部辅助实现 */
            g->g_site_count = LLVMAddGlobal(g->mod, i64t, "__zan_site_count");
            LLVMSetInitializer(g->g_site_count, LLVMConstInt(i64t, 0, 0));
            LLVMSetLinkage(g->g_site_count, LLVMInternalLinkage);
            g->g_site_live = LLVMAddGlobal(g->mod, LLVMPointerType(i64t, 0), "__zan_site_live");
            LLVMSetInitializer(g->g_site_live, LLVMConstNull(LLVMPointerType(i64t, 0)));
            LLVMSetLinkage(g->g_site_live, LLVMInternalLinkage);
            g->g_site_names = LLVMAddGlobal(g->mod, LLVMPointerType(i8p, 0), "__zan_site_names");
            LLVMSetInitializer(g->g_site_names, LLVMConstNull(LLVMPointerType(i8p, 0)));
            LLVMSetLinkage(g->g_site_names, LLVMInternalLinkage);
            /* 内部辅助实现 */
            g->g_site_dtors = LLVMAddGlobal(g->mod, LLVMPointerType(i8p, 0), "__zan_site_dtors");
            LLVMSetInitializer(g->g_site_dtors, LLVMConstNull(LLVMPointerType(i8p, 0)));
            LLVMSetLinkage(g->g_site_dtors, LLVMInternalLinkage);
            g->g_site_tynames = LLVMAddGlobal(g->mod, LLVMPointerType(i8p, 0), "__zan_site_tynames");
            LLVMSetInitializer(g->g_site_tynames, LLVMConstNull(LLVMPointerType(i8p, 0)));
            LLVMSetLinkage(g->g_site_tynames, LLVMInternalLinkage);
            g->g_site_meta = LLVMAddGlobal(g->mod, LLVMPointerType(i8p, 0), "__zan_site_meta");
            LLVMSetInitializer(g->g_site_meta, LLVMConstNull(LLVMPointerType(i8p, 0)));
            LLVMSetLinkage(g->g_site_meta, LLVMInternalLinkage);
        }
        g->site_syms = (zan_symbol_t **)calloc(g->leak_site_cap, sizeof(zan_symbol_t *));
        g->site_coll = (int *)calloc(g->leak_site_cap, sizeof(int));
        g->site_coll_elem = (zan_type_t **)calloc(g->leak_site_cap, sizeof(zan_type_t *));
        g->site_inst = (zan_type_t **)calloc(g->leak_site_cap, sizeof(zan_type_t *));
        g->site_loc_file = (uint32_t *)calloc(g->leak_site_cap, sizeof(uint32_t));
        g->site_loc_line = (uint32_t *)calloc(g->leak_site_cap, sizeof(uint32_t));
    }

    /* 核心系统底层抽象与内存语义契约 */
    LLVMTypeRef printf_args[] = { LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0) };
    LLVMTypeRef printf_type = LLVMFunctionType(
        LLVMInt32TypeInContext(g->ctx), printf_args, 1, 1 /* varargs */);
    LLVMValueRef printf_fn = LLVMAddFunction(g->mod, "printf", printf_type);
    g->fn_printf = printf_fn;
    g->printf_type = printf_type;

    /* 模块核心语义抽象与接口调用契约 */
    LLVMTypeRef println_args[] = { LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0) };
    LLVMTypeRef println_type = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx), println_args, 1, 0);
    g->rt_println = LLVMAddFunction(g->mod, "zan_rt_println", println_type);

    /* 核心系统底层抽象与内存语义契约 */
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_println, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);

    LLVMValueRef fmt_str = zan_irgen_intern_string(g, "%s\n");
    LLVMValueRef args[] = { fmt_str, LLVMGetParam(g->rt_println, 0) };
    zan_call2(g->builder, printf_type, printf_fn, args, 2, "");
    LLVMBuildRetVoid(g->builder);

    /* 核心系统底层抽象与内存语义契约 */
    LLVMTypeRef pint_args[] = { LLVMInt64TypeInContext(g->ctx) };
    LLVMTypeRef pint_type = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx), pint_args, 1, 0);
    g->rt_print_int = LLVMAddFunction(g->mod, "zan_rt_print_int", pint_type);

    LLVMBasicBlockRef pint_entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_print_int, "entry");
    LLVMPositionBuilderAtEnd(g->builder, pint_entry);
    LLVMValueRef int_fmt = zan_irgen_intern_string(g, "%lld\n");
    LLVMValueRef iargs[] = { int_fmt, LLVMGetParam(g->rt_print_int, 0) };
    zan_call2(g->builder, printf_type, printf_fn, iargs, 2, "");
    LLVMBuildRetVoid(g->builder);

    /* 模块核心语义抽象与接口调用契约 */
    g->rt_print_uint = LLVMAddFunction(g->mod, "zan_rt_print_uint", pint_type);

    LLVMBasicBlockRef puint_entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_print_uint, "entry");
    LLVMPositionBuilderAtEnd(g->builder, puint_entry);
    LLVMValueRef uint_fmt = zan_irgen_intern_string(g, "%llu\n");
    LLVMValueRef uargs[] = { uint_fmt, LLVMGetParam(g->rt_print_uint, 0) };
    zan_call2(g->builder, printf_type, printf_fn, uargs, 2, "");
    LLVMBuildRetVoid(g->builder);

    /* 核心系统底层抽象与内存语义契约 */
    LLVMTypeRef pdbl_args[] = { LLVMDoubleTypeInContext(g->ctx) };
    LLVMTypeRef pdbl_type = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx), pdbl_args, 1, 0);
    g->rt_print_double = LLVMAddFunction(g->mod, "zan_rt_print_double", pdbl_type);

    LLVMBasicBlockRef pdbl_entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_print_double, "entry");
    LLVMPositionBuilderAtEnd(g->builder, pdbl_entry);
    /* 内部辅助逻辑 */
    {
        LLVMTypeRef i8p_t = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef i64_t = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef ds_fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8p_t, i64_t, LLVMDoubleTypeInContext(g->ctx) }, 3, 0);
        LLVMValueRef ds_fn = LLVMGetNamedFunction(g->mod, "zan_rt_dbl_str");
        if (!ds_fn) ds_fn = LLVMAddFunction(g->mod, "zan_rt_dbl_str", ds_fn_ty);
        LLVMValueRef ds_buf = LLVMBuildAlloca(g->builder,
            LLVMArrayType(LLVMInt8TypeInContext(g->ctx), 40), "pdbl.buf");
        LLVMValueRef ds_bufp = LLVMBuildBitCast(g->builder, ds_buf, i8p_t,
                                                "pdbl.bufp");
        LLVMValueRef ds_args[] = { ds_bufp,
            LLVMConstInt(i64_t, 40, 0), LLVMGetParam(g->rt_print_double, 0) };
        zan_call2(g->builder, ds_fn_ty, ds_fn, ds_args, 3, "");
        LLVMValueRef nl_fmt = zan_irgen_intern_string(g, "%s\n");
        LLVMValueRef dargs[] = { nl_fmt, ds_bufp };
        zan_call2(g->builder, printf_type, printf_fn, dargs, 2, "");
    }
    LLVMBuildRetVoid(g->builder);

    /* 底层系统交互与数据协议契约 */
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);

    /* 底层系统交互与数据协议契约 */
    LLVMTypeRef snprintf_args[] = { i8ptr, i64, i8ptr };
    LLVMTypeRef snprintf_type = LLVMFunctionType(i32, snprintf_args, 3, 1);
    g->fn_snprintf = LLVMAddFunction(g->mod, "snprintf", snprintf_type);

    /* void *malloc(size_t) */
    LLVMTypeRef malloc_args[] = { i64 };
    LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr, malloc_args, 1, 0);
    g->fn_malloc = LLVMAddFunction(g->mod, "malloc", malloc_type);

    /* 核心系统底层抽象与内存语义契约 */
    LLVMTypeRef free_args[] = { i8ptr };
    LLVMTypeRef free_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), free_args, 1, 0);
    g->fn_free = LLVMAddFunction(g->mod, "free", free_type);

    /* 底层系统交互与数据协议契约 */
    LLVMTypeRef exit_args[] = { i32 };
    g->exit_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), exit_args, 1, 0);
    g->fn_exit = LLVMAddFunction(g->mod, "exit", g->exit_type);

    if (g->check_leaks) {
        /* 模块核心语义抽象与接口调用契约 */
        LLVMTypeRef void_fn_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
        LLVMTypeRef void_fn_ptr = LLVMPointerType(void_fn_type, 0);
        LLVMTypeRef atexit_args[] = { void_fn_ptr };
        g->atexit_type = LLVMFunctionType(i32, atexit_args, 1, 0);
        g->fn_atexit = LLVMAddFunction(g->mod, "atexit", g->atexit_type);
    }

    /* void* realloc(void*, size_t) */
    LLVMTypeRef realloc_args[] = { i8ptr, i64 };
    LLVMTypeRef realloc_type = LLVMFunctionType(i8ptr, realloc_args, 2, 0);
    g->fn_realloc = LLVMAddFunction(g->mod, "realloc", realloc_type);

    /* 底层系统交互与数据协议契约 */
    LLVMTypeRef list_fields[] = { i64, i64, LLVMPointerType(i64, 0) };
    g->list_struct_type = LLVMStructCreateNamed(g->ctx, "List");
    LLVMStructSetBody(g->list_struct_type, list_fields, 3, 0);

    /* 核心系统底层抽象与内存语义契约 */
    LLVMTypeRef span_fields[] = { i8ptr, i64 };
    g->span_struct_type = LLVMStructCreateNamed(g->ctx, "Span");
    LLVMStructSetBody(g->span_struct_type, span_fields, 2, 0);

    /* 内部辅助实现 */
    LLVMTypeRef dict_fields[] = { i64, i64, LLVMPointerType(i8ptr, 0), LLVMPointerType(i64, 0),
                                  LLVMPointerType(i64, 0), i64, i64, i64 };
    g->dict_struct_type = LLVMStructCreateNamed(g->ctx, "Dict");
    LLVMStructSetBody(g->dict_struct_type, dict_fields, 8, 0);

    /* 底层系统交互与数据协议契约 */
    g->task_struct_type = LLVMStructCreateNamed(g->ctx, "Task");
    LLVMTypeRef task_fields[] = { i64, i64, i64 };
    LLVMStructSetBody(g->task_struct_type, task_fields, 3, 0);

    /* 底层系统交互与数据协议契约 */
    LLVMTypeRef sb_fields[] = { i64, i64, i8ptr };
    g->sb_struct_type = LLVMStructCreateNamed(g->ctx, "StringBuilder");
    LLVMStructSetBody(g->sb_struct_type, sb_fields, 3, 0);

    /* 模块核心语义抽象与接口调用契约 */
    LLVMTypeRef co_step_args[] = { i8ptr };
    g->co_step_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), co_step_args, 1, 0);
    g->co_step_ptr = LLVMPointerType(g->co_step_type, 0);
    LLVMTypeRef co_ready_args[] = { i8ptr, g->co_step_ptr };
    g->rt_co_ready_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), co_ready_args, 2, 0);
    g->rt_co_ready = LLVMAddFunction(g->mod, "zan_co_ready", g->rt_co_ready_type);
    /* 底层系统交互与数据协议契约 */
    g->rt_co_poll_type = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), NULL, 0, 0);
    g->rt_co_poll = LLVMAddFunction(g->mod, "zan_co_poll", g->rt_co_poll_type);
    /* 内部辅助实现 */
    g->rt_co_frame_free_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                                (LLVMTypeRef[]){ i8ptr }, 1, 0);
    g->rt_co_frame_free = LLVMAddFunction(g->mod, "__zan_co_frame_free",
                                          g->rt_co_frame_free_type);
    /* 编译期中间表示与代码生成内部规范 */
    g->rt_co_sched_init_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    g->rt_co_sched_init = LLVMAddFunction(g->mod, "zan_co_sched_init", g->rt_co_sched_init_type);
    g->rt_co_sched_run_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    g->rt_co_sched_run = LLVMAddFunction(g->mod, "zan_co_sched_run", g->rt_co_sched_run_type);
    /* 内部辅助逻辑 */
    g->rt_co_sched_run_until_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ LLVMPointerType(LLVMInt32TypeInContext(g->ctx), 0) }, 1, 0);
    g->rt_co_sched_run_until = LLVMAddFunction(g->mod, "zan_co_sched_run_until",
        g->rt_co_sched_run_until_type);
    {
        LLVMTypeRef i64d = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef delay_args[] = { i64d, i8ptr, g->co_step_ptr };
        g->rt_co_delay_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), delay_args, 3, 0);
        g->rt_co_delay = LLVMAddFunction(g->mod, "zan_co_delay", g->rt_co_delay_type);
    }

    /* 核心系统底层抽象与内存语义契约 */
    {
        LLVMTypeRef i64d = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i32d = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef wc_args[] = { i64d, i32d, i8ptr, g->co_step_ptr };
        g->rt_io_wait_co_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), wc_args, 4, 0);
        g->rt_io_wait_co = LLVMAddFunction(g->mod, "zan_io_wait_co", g->rt_io_wait_co_type);
        LLVMTypeRef i64ptr = LLVMPointerType(i64d, 0);
        LLVMTypeRef rc_args[] = { i64d, i8ptr, i32d, i8ptr, g->co_step_ptr, i64ptr };
        g->rt_io_recv_co_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), rc_args, 6, 0);
        g->rt_io_recv_co = LLVMAddFunction(g->mod, "zan_io_recv_co", g->rt_io_recv_co_type);
        LLVMTypeRef rtc_args[] = { i64d, i8ptr, i32d, i64d, i8ptr, g->co_step_ptr, i64ptr };
        g->rt_io_recv_to_co_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), rtc_args, 7, 0);
        g->rt_io_recv_to_co = LLVMAddFunction(g->mod, "zan_io_recv_to_co", g->rt_io_recv_to_co_type);
        LLVMTypeRef ac_args[] = { i64d, i8ptr, g->co_step_ptr, i64ptr };
        g->rt_io_accept_co_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), ac_args, 4, 0);
        g->rt_io_accept_co = LLVMAddFunction(g->mod, "zan_io_accept_co", g->rt_io_accept_co_type);
        LLVMTypeRef i32ptr = LLVMPointerType(i32d, 0);
        LLVMTypeRef dc_args[] = { i8ptr, i8ptr, g->co_step_ptr, i32ptr };
        g->rt_io_resolve_co_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), dc_args, 4, 0);
        g->rt_io_resolve_co = LLVMAddFunction(g->mod, "zan_io_resolve_co", g->rt_io_resolve_co_type);
        LLVMTypeRef sa_args[] = { i8ptr, i32d, i8ptr, i32d, i8ptr,
                                  g->co_step_ptr, i32ptr };
        g->rt_io_resolve_sa_co_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), sa_args, 7, 0);
        g->rt_io_resolve_sa_co = LLVMAddFunction(g->mod, "zan_io_resolve_sa_co",
            g->rt_io_resolve_sa_co_type);
        LLVMTypeRef fnptr = i8ptr;
        LLVMTypeRef argc_t = i32d;
        LLVMTypeRef blocking_args[] = { fnptr, argc_t, i64d, i64d, i64d,
                                        i64d, i8ptr, g->co_step_ptr,
                                        LLVMPointerType(i64d, 0) };
        g->rt_blocking_co_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                                   blocking_args, 9, 0);
        g->rt_blocking_co = LLVMAddFunction(g->mod, "zan_rt_blocking_co",
                                            g->rt_blocking_co_type);
        LLVMTypeRef pump_args[] = { i64d };
        g->rt_io_pump_timeout_type = LLVMFunctionType(i32d, pump_args, 1, 0);
        g->rt_io_pump_timeout = LLVMAddFunction(g->mod, "zan_io_pump_timeout",
            g->rt_io_pump_timeout_type);
        LLVMSetLinkage(g->rt_io_pump_timeout,
            g->external_async_executor ? LLVMExternalLinkage : LLVMExternalWeakLinkage);

        /* 内部辅助实现 */
        g->rt_io_has_pending_type = LLVMFunctionType(i32d, NULL, 0, 0);
        g->rt_io_has_pending = LLVMAddFunction(g->mod, "zan_io_has_pending",
            g->rt_io_has_pending_type);
        LLVMSetLinkage(g->rt_io_has_pending,
            g->external_async_executor ? LLVMExternalLinkage : LLVMExternalWeakLinkage);

        LLVMTypeRef legacy_pump_type = LLVMFunctionType(i32d, NULL, 0, 0);
        LLVMValueRef legacy_pump = LLVMAddFunction(g->mod, "zan_io_pump",
            legacy_pump_type);
        LLVMSetLinkage(legacy_pump, LLVMWeakAnyLinkage);
        LLVMBasicBlockRef legacy_entry = LLVMAppendBasicBlockInContext(g->ctx,
            legacy_pump, "entry");
        LLVMPositionBuilderAtEnd(g->builder, legacy_entry);
        LLVMValueRef legacy_woke = zan_call2(g->builder,
            g->rt_io_pump_timeout_type, g->rt_io_pump_timeout,
            (LLVMValueRef[]){ LLVMConstInt(i64d, (uint64_t)-1, 1) }, 1, "woke");
        LLVMBuildRet(g->builder, legacy_woke);
    }
    g->uses_socket_async = false;

    /* 内部辅助实现 */
    if (!g->external_async_executor) {
        LLVMTypeRef voidt = LLVMVoidTypeInContext(g->ctx);
        LLVMTypeRef node_fields[] = { i8ptr /* next */, i8ptr /*frame*/, g->co_step_ptr /*step*/ };
        LLVMTypeRef node_ty = LLVMStructCreateNamed(g->ctx, "zan.co.node");
        LLVMStructSetBody(node_ty, node_fields, 3, 0);
        LLVMValueRef g_head = LLVMAddGlobal(g->mod, i8ptr, "__zan_co_head");
        LLVMSetInitializer(g_head, LLVMConstNull(i8ptr));
        LLVMSetLinkage(g_head, LLVMInternalLinkage);
        LLVMValueRef g_tail = LLVMAddGlobal(g->mod, i8ptr, "__zan_co_tail");
        LLVMSetInitializer(g_tail, LLVMConstNull(i8ptr));
        LLVMSetLinkage(g_tail, LLVMInternalLinkage);
        /* 内部辅助实现 */
        LLVMValueRef g_nodes = LLVMAddGlobal(g->mod, i8ptr, "__zan_co_nodes");
        LLVMSetInitializer(g_nodes, LLVMConstNull(i8ptr));
        LLVMSetLinkage(g_nodes, LLVMInternalLinkage);
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);

        /* 内部辅助实现 */
        LLVMValueRef g_slice_start = LLVMAddGlobal(g->mod, i64t, "__zan_co_slice_start");
        LLVMSetInitializer(g_slice_start, LLVMConstInt(i64t, 0, 0));
        /* 内部辅助实现 */
        LLVMValueRef g_poll_tick = LLVMAddGlobal(g->mod, i64t, "__zan_co_poll_tick");
        LLVMSetInitializer(g_poll_tick, LLVMConstInt(i64t, 0, 0));
        LLVMSetLinkage(g_slice_start, LLVMInternalLinkage);
        LLVMValueRef g_quantum = LLVMAddGlobal(g->mod, i64t, "__zan_co_quantum_us");
        LLVMSetInitializer(g_quantum, LLVMConstInt(i64t, 0, 0));
        LLVMSetLinkage(g_quantum, LLVMInternalLinkage);
        /* 内部辅助实现 */
        LLVMTypeRef now_type = LLVMFunctionType(i64t, NULL, 0, 0);
        LLVMValueRef co_quantum = LLVMAddFunction(g->mod, "zan_co_quantum_ms", now_type);
        LLVMValueRef precise_now = LLVMAddFunction(g->mod, "zan_co_precise_us", now_type);

        /* 模块核心语义抽象与接口调用契约 */
        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef timer_reset_type = LLVMFunctionType(voidt, NULL, 0, 0);
        LLVMValueRef timer_reset = LLVMAddFunction(g->mod, "zan_timer_runtime_reset", timer_reset_type);
        LLVMTypeRef timer_hook_type = LLVMFunctionType(voidt,
            (LLVMTypeRef[]){ g->rt_co_ready_type ? LLVMPointerType(g->rt_co_ready_type, 0) : i8ptr }, 1, 0);
        LLVMValueRef timer_set_hook = LLVMAddFunction(g->mod, "zan_timer_set_ready_hook", timer_hook_type);
        LLVMTypeRef timer_delay_type = LLVMFunctionType(voidt,
            (LLVMTypeRef[]){ i64t, i8ptr, g->co_step_ptr }, 3, 0);
        LLVMValueRef timer_delay = LLVMAddFunction(g->mod, "zan_timer_delay", timer_delay_type);
        LLVMTypeRef timer_next_type = LLVMFunctionType(i64t, NULL, 0, 0);
        LLVMValueRef timer_next = LLVMAddFunction(g->mod, "zan_timer_next_timeout", timer_next_type);
        LLVMValueRef timer_dispatch = LLVMAddFunction(g->mod, "zan_timer_dispatch_due", timer_next_type);

        /* 内部辅助实现 */
        LLVMTypeRef sleep_args[] = { i32t };
        LLVMTypeRef sleep_type = LLVMFunctionType(voidt, sleep_args, 1, 0);
        LLVMValueRef fn_sleep = NULL;
        LLVMTypeRef poll_args[] = { i8ptr, i64t, i32t };
        LLVMTypeRef poll_type = LLVMFunctionType(i32t, poll_args, 3, 0);
        LLVMValueRef fn_poll = NULL;
        if (g->target_is_windows)
            fn_sleep = LLVMAddFunction(g->mod, "Sleep", sleep_type);
        else
            fn_poll = LLVMAddFunction(g->mod, "poll", poll_type);

        /* 模块核心语义抽象与接口调用契约 */
        {
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_io_pump_timeout, "entry");
            LLVMBasicBlockRef sleep = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_io_pump_timeout, "sleep");
            LLVMBasicBlockRef ret = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_io_pump_timeout, "ret");
            LLVMPositionBuilderAtEnd(g->builder, entry);
            LLVMValueRef timeout = LLVMGetParam(g->rt_io_pump_timeout, 0);
            LLVMValueRef positive = zan_icmp(g->builder, LLVMIntSGT, timeout,
                LLVMConstInt(i64t, 0, 0), "positive");
            LLVMBuildCondBr(g->builder, positive, sleep, ret);

            LLVMPositionBuilderAtEnd(g->builder, sleep);
            LLVMValueRef timeout32 = LLVMBuildTrunc(g->builder, timeout, i32t, "timeout32");
            if (g->target_is_windows)
                zan_call2(g->builder, sleep_type, fn_sleep,
                    (LLVMValueRef[]){ timeout32 }, 1, "");
            else
                zan_call2(g->builder, poll_type, fn_poll,
                    (LLVMValueRef[]){ LLVMConstNull(i8ptr), LLVMConstInt(i64t, 0, 0), timeout32 }, 3, "");
            LLVMBuildBr(g->builder, ret);

            LLVMPositionBuilderAtEnd(g->builder, ret);
            LLVMBuildRet(g->builder, LLVMConstInt(i32t, 0, 0));
        }

        /* 内部辅助实现 */
        {
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_io_has_pending, "entry");
            LLVMPositionBuilderAtEnd(g->builder, entry);
            LLVMBuildRet(g->builder, LLVMConstInt(i32t, 0, 0));
        }

        /* 模块核心语义抽象与接口调用契约 */
        {
            LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_init, "entry");
            LLVMPositionBuilderAtEnd(g->builder, bb);
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), g_head);
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), g_tail);
            LLVMValueRef q = zan_call2(g->builder, now_type, co_quantum, NULL, 0, "q");
            q = LLVMBuildMul(g->builder, q, LLVMConstInt(i64t, 1000, 0), "q.us");
            LLVMBuildStore(g->builder, q, g_quantum);
            LLVMValueRef t0 = zan_call2(g->builder, now_type, precise_now, NULL, 0, "t0");
            LLVMBuildStore(g->builder, t0, g_slice_start);
            zan_call2(g->builder, timer_reset_type, timer_reset, NULL, 0, "");
            zan_call2(g->builder, timer_hook_type, timer_set_hook,
                (LLVMValueRef[]){ g->rt_co_ready }, 1, "");
            LLVMBuildRetVoid(g->builder);
        }

        /* 内部辅助逻辑 */
        {
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_poll, "entry");
            LLVMBasicBlockRef check = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_poll, "check");
            LLVMBasicBlockRef gate  = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_poll, "gate");
            LLVMBasicBlockRef yes   = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_poll, "yes");
            LLVMBasicBlockRef no    = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_poll, "no");
            LLVMPositionBuilderAtEnd(g->builder, entry);
            LLVMValueRef q = LLVMBuildLoad2(g->builder, i64t, g_quantum, "q");
            LLVMValueRef enabled = zan_icmp(g->builder, LLVMIntSGT, q,
                LLVMConstInt(i64t, 0, 0), "q.on");
            LLVMBuildCondBr(g->builder, enabled, check, no);

            LLVMPositionBuilderAtEnd(g->builder, check);
            LLVMValueRef tick = LLVMBuildLoad2(g->builder, i64t, g_poll_tick, "tick");
            LLVMValueRef tick1 = LLVMBuildAdd(g->builder, tick,
                LLVMConstInt(i64t, 1, 0), "tick1");
            LLVMBuildStore(g->builder, tick1, g_poll_tick);
            LLVMValueRef hit = zan_icmp(g->builder, LLVMIntEQ,
                LLVMBuildAnd(g->builder, tick1, LLVMConstInt(i64t, 255, 0), "tick.lo"),
                LLVMConstInt(i64t, 0, 0), "gate.hit");
            LLVMBuildCondBr(g->builder, hit, gate, no);

            LLVMPositionBuilderAtEnd(g->builder, gate);
            LLVMValueRef now = zan_call2(g->builder, now_type, precise_now, NULL, 0, "now");
            LLVMValueRef start = LLVMBuildLoad2(g->builder, i64t, g_slice_start, "start");
            LLVMValueRef elapsed = LLVMBuildSub(g->builder, now, start, "elapsed");
            LLVMValueRef over = zan_icmp(g->builder, LLVMIntSGE, elapsed, q, "over");
            LLVMBuildCondBr(g->builder, over, yes, no);

            LLVMPositionBuilderAtEnd(g->builder, yes);
            LLVMBuildRet(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1, 0));
            LLVMPositionBuilderAtEnd(g->builder, no);
            LLVMBuildRet(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0));
        }

        /* 内部辅助逻辑 */
        {
            LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_delay, "entry");
            LLVMPositionBuilderAtEnd(g->builder, bb);
            zan_call2(g->builder, timer_delay_type, timer_delay,
                (LLVMValueRef[]){ LLVMGetParam(g->rt_co_delay, 0),
                                  LLVMGetParam(g->rt_co_delay, 1),
                                  LLVMGetParam(g->rt_co_delay, 2) }, 3, "");
            LLVMBuildRetVoid(g->builder);
        }

        /* 模块核心语义抽象与接口调用契约 */
        {
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_ready, "entry");
            LLVMBasicBlockRef cont  = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_ready, "cont");
            LLVMBasicBlockRef empty = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_ready, "empty");
            LLVMBasicBlockRef nonempty = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_ready, "append");
            LLVMBasicBlockRef done  = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_ready, "done");
            LLVMBasicBlockRef ret   = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_ready, "ret");
            LLVMPositionBuilderAtEnd(g->builder, entry);
            LLVMValueRef frame = LLVMGetParam(g->rt_co_ready, 0);
            LLVMValueRef step  = LLVMGetParam(g->rt_co_ready, 1);
            LLVMValueRef step_null = zan_icmp(g->builder, LLVMIntEQ, step,
                LLVMConstNull(g->co_step_ptr), "step.null");
            LLVMBuildCondBr(g->builder, step_null, ret, cont);

            LLVMPositionBuilderAtEnd(g->builder, cont);
            /* 底层系统交互与数据协议契约 */
            LLVMBasicBlockRef reuse_bb = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_co_ready, "node.reuse");
            LLVMBasicBlockRef alloc_bb = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_co_ready, "node.alloc");
            LLVMBasicBlockRef have_bb = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_co_ready, "node.have");
            LLVMValueRef spare = LLVMBuildLoad2(g->builder, i8ptr, g_nodes, "spare");
            LLVMValueRef has_spare = zan_icmp(g->builder, LLVMIntNE, spare,
                LLVMConstNull(i8ptr), "has.spare");
            LLVMBuildCondBr(g->builder, has_spare, reuse_bb, alloc_bb);

            LLVMPositionBuilderAtEnd(g->builder, reuse_bb);
            LLVMValueRef spare_next = LLVMBuildLoad2(g->builder, i8ptr,
                LLVMBuildStructGEP2(g->builder, node_ty, spare, 0, "spare.next.p"),
                "spare.next");
            LLVMBuildStore(g->builder, spare_next, g_nodes);
            LLVMBuildBr(g->builder, have_bb);

            LLVMPositionBuilderAtEnd(g->builder, alloc_bb);
            LLVMValueRef fresh = zan_call2(g->builder, malloc_type, g->fn_malloc,
                (LLVMValueRef[]){ LLVMSizeOf(node_ty) }, 1, "node");
            zan_irgen_emit_oom_check(g, g->rt_co_ready, fresh);
            /* 内部辅助逻辑 */
            LLVMBasicBlockRef fresh_bb = LLVMGetInsertBlock(g->builder);
            LLVMBuildBr(g->builder, have_bb);

            LLVMPositionBuilderAtEnd(g->builder, have_bb);
            LLVMValueRef node = LLVMBuildPhi(g->builder, i8ptr, "node");
            LLVMAddIncoming(node, (LLVMValueRef[]){ spare, fresh },
                (LLVMBasicBlockRef[]){ reuse_bb, fresh_bb }, 2);
            LLVMValueRef nnext = LLVMBuildStructGEP2(g->builder, node_ty, node, 0, "n.next");
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), nnext);
            LLVMValueRef nframe = LLVMBuildStructGEP2(g->builder, node_ty, node, 1, "n.frame");
            LLVMBuildStore(g->builder, frame, nframe);
            LLVMValueRef nstep = LLVMBuildStructGEP2(g->builder, node_ty, node, 2, "n.step");
            LLVMBuildStore(g->builder, step, nstep);
            LLVMValueRef tail = LLVMBuildLoad2(g->builder, i8ptr, g_tail, "tail");
            LLVMValueRef is_empty = zan_icmp(g->builder, LLVMIntEQ, tail,
                LLVMConstNull(i8ptr), "q.empty");
            LLVMBuildCondBr(g->builder, is_empty, empty, nonempty);

            LLVMPositionBuilderAtEnd(g->builder, empty);
            LLVMBuildStore(g->builder, node, g_head);
            LLVMBuildBr(g->builder, done);

            LLVMPositionBuilderAtEnd(g->builder, nonempty);
            LLVMValueRef tail_next = LLVMBuildStructGEP2(g->builder, node_ty, tail, 0, "tail.next");
            LLVMBuildStore(g->builder, node, tail_next);
            LLVMBuildBr(g->builder, done);

            LLVMPositionBuilderAtEnd(g->builder, done);
            LLVMBuildStore(g->builder, node, g_tail);
            LLVMBuildBr(g->builder, ret);

            LLVMPositionBuilderAtEnd(g->builder, ret);
            LLVMBuildRetVoid(g->builder);
        }

        /* 内部辅助逻辑 */
        {
            LLVMValueRef runfn = g->rt_co_sched_run_until;
            LLVMTypeRef i32p = LLVMPointerType(i32t, 0);
            LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "entry");
            LLVMBasicBlockRef head_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "loop");
            LLVMBasicBlockRef chk_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "done.chk");
            LLVMBasicBlockRef pump_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "pump");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "body");
            LLVMBasicBlockRef last_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "last");
            LLVMBasicBlockRef after_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "after");
            LLVMBasicBlockRef timers_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "timers");
            LLVMBasicBlockRef wait_timer = LLVMAppendBasicBlockInContext(g->ctx, runfn, "timer.wait");
            LLVMBasicBlockRef io_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "io.pump");
            LLVMBasicBlockRef exit_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "exit");
            LLVMPositionBuilderAtEnd(g->builder, entry_bb);
            LLVMValueRef doneptr = LLVMGetParam(runfn, 0);
            LLVMBuildBr(g->builder, head_bb);

            /* 模块核心语义抽象与接口调用契约 */
            LLVMPositionBuilderAtEnd(g->builder, head_bb);
            LLVMValueRef no_flag = zan_icmp(g->builder, LLVMIntEQ, doneptr,
                LLVMConstNull(i32p), "done.noflag");
            LLVMBuildCondBr(g->builder, no_flag, pump_bb, chk_bb);

            LLVMPositionBuilderAtEnd(g->builder, chk_bb);
            LLVMValueRef dflag = LLVMBuildLoad2(g->builder, i32t, doneptr, "done.flag");
            LLVMValueRef is_done = zan_icmp(g->builder, LLVMIntNE, dflag,
                LLVMConstInt(i32t, 0, 0), "done.set");
            LLVMBuildCondBr(g->builder, is_done, exit_bb, pump_bb);

            LLVMPositionBuilderAtEnd(g->builder, pump_bb);
            LLVMValueRef head = LLVMBuildLoad2(g->builder, i8ptr, g_head, "head");
            LLVMValueRef empty = zan_icmp(g->builder, LLVMIntEQ, head,
                LLVMConstNull(i8ptr), "q.empty");
            LLVMBuildCondBr(g->builder, empty, timers_bb, body_bb);

            LLVMPositionBuilderAtEnd(g->builder, body_bb);
            LLVMValueRef fr_ptr = LLVMBuildStructGEP2(g->builder, node_ty, head, 1, "n.frame");
            LLVMValueRef fr = LLVMBuildLoad2(g->builder, i8ptr, fr_ptr, "frame");
            LLVMValueRef st_ptr = LLVMBuildStructGEP2(g->builder, node_ty, head, 2, "n.step");
            LLVMValueRef st = LLVMBuildLoad2(g->builder, g->co_step_ptr, st_ptr, "step");
            LLVMValueRef nx_ptr = LLVMBuildStructGEP2(g->builder, node_ty, head, 0, "n.next");
            LLVMValueRef nx = LLVMBuildLoad2(g->builder, i8ptr, nx_ptr, "next");
            LLVMBuildStore(g->builder, nx, g_head);
            LLVMValueRef is_last = zan_icmp(g->builder, LLVMIntEQ, nx,
                LLVMConstNull(i8ptr), "is.last");
            LLVMBuildCondBr(g->builder, is_last, last_bb, after_bb);

            LLVMPositionBuilderAtEnd(g->builder, last_bb);
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), g_tail);
            LLVMBuildBr(g->builder, after_bb);

            LLVMPositionBuilderAtEnd(g->builder, after_bb);
            /* 内部辅助逻辑 */
            LLVMBuildStore(g->builder,
                LLVMBuildLoad2(g->builder, i8ptr, g_nodes, "nodes.head"),
                LLVMBuildStructGEP2(g->builder, node_ty, head, 0, "n.recycle"));
            LLVMBuildStore(g->builder, head, g_nodes);
            /* 内部辅助实现 */
            (void)zan_call2(g->builder, timer_next_type, timer_dispatch,
                NULL, 0, "due.busy");
            /* 内部辅助实现 */
            LLVMValueRef slice_now = zan_call2(g->builder, now_type, precise_now,
                NULL, 0, "slice.now");
            LLVMBuildStore(g->builder, slice_now, g_slice_start);
            zan_call2(g->builder, g->co_step_type, st,
                (LLVMValueRef[]){ fr }, 1, "");
            LLVMBuildBr(g->builder, head_bb);

            LLVMPositionBuilderAtEnd(g->builder, timers_bb);
            LLVMValueRef dispatched = zan_call2(g->builder, timer_next_type,
                timer_dispatch, NULL, 0, "timer.dispatched");
            LLVMValueRef has_due = zan_icmp(g->builder, LLVMIntSGT, dispatched,
                LLVMConstInt(i64t, 0, 0), "timer.has_due");
            LLVMBuildCondBr(g->builder, has_due, head_bb, wait_timer);

            LLVMPositionBuilderAtEnd(g->builder, wait_timer);
            LLVMValueRef timeout = zan_call2(g->builder, timer_next_type,
                timer_next, NULL, 0, "timer.timeout");
            LLVMValueRef has_timer = zan_icmp(g->builder, LLVMIntSGE, timeout,
                LLVMConstInt(i64t, 0, 0), "timer.pending");
            LLVMBuildBr(g->builder, io_bb);

            LLVMPositionBuilderAtEnd(g->builder, io_bb);
            LLVMValueRef woke = zan_call2(g->builder,
                g->rt_io_pump_timeout_type, g->rt_io_pump_timeout,
                (LLVMValueRef[]){ timeout }, 1, "woke");
            LLVMValueRef more = zan_icmp(g->builder, LLVMIntSGT, woke,
                LLVMConstInt(i64t, 0, 0), "io.more");
            /* 内部辅助实现 */
            LLVMValueRef pend = zan_call2(g->builder,
                g->rt_io_has_pending_type, g->rt_io_has_pending, NULL, 0,
                "io.pending");
            LLVMValueRef has_pending = zan_icmp(g->builder, LLVMIntSGT, pend,
                LLVMConstInt(i64t, 0, 0), "io.has_pending");
            LLVMValueRef cont_timer = LLVMBuildOr(g->builder, more, has_timer,
                "sched.more");
            LLVMValueRef continue_run = LLVMBuildOr(g->builder, cont_timer,
                has_pending, "sched.more2");
            LLVMBuildCondBr(g->builder, continue_run, head_bb, exit_bb);

            LLVMPositionBuilderAtEnd(g->builder, exit_bb);
            LLVMBuildRetVoid(g->builder);
        }

        /* 模块核心语义抽象与接口调用契约 */
        {
            LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_co_sched_run, "entry");
            LLVMPositionBuilderAtEnd(g->builder, bb);
            zan_call2(g->builder, g->rt_co_sched_run_until_type,
                g->rt_co_sched_run_until,
                (LLVMValueRef[]){ LLVMConstNull(LLVMPointerType(i32t, 0)) }, 1, "");
            LLVMBuildRetVoid(g->builder);
        }
        (void)voidt;
    }
    /* 内部辅助实现 */
    LLVMTypeRef co_hdr_fields[] = {
        i64, g->co_step_ptr,   /* SCHED, SCHED_STEP: scheduler-owned */
        LLVMInt32TypeInContext(g->ctx), LLVMInt32TypeInContext(g->ctx),
        i8ptr, g->co_step_ptr, i64,
        g->co_step_ptr, LLVMInt32TypeInContext(g->ctx),
        g->co_step_ptr, /* 核心系统底层抽象与内存语义契约 */
        i8ptr, i8ptr, LLVMInt32TypeInContext(g->ctx), /* 核心系统底层抽象与内存语义契约 */
        LLVMInt32TypeInContext(g->ctx), /* 核心系统底层抽象与内存语义契约 */
        i8ptr, /* 核心系统底层抽象与内存语义契约 */
        i8ptr  /* 底层系统交互与数据协议契约 */
    };
    /* 内部辅助实现 */
    g->co_header_type = LLVMStructCreateNamed(g->ctx, "zan.co.header");
    LLVMStructSetBody(g->co_header_type, co_hdr_fields, 16, 0);
    g->current_async_frame = NULL;
    g->current_async_frame_type = NULL;
    g->current_async_resume_fn = NULL;
    g->current_async_ret_type = NULL;
    g->current_async_switch = NULL;
    g->current_async_next_state = 1;
    g->current_async_sub_base = 0;
    g->current_async_sub_next = 0;
    g->current_async_ret_agg_slot = -1;
    g->current_async_slots = NULL;
    g->current_async_slot_count = 0;
    g->current_async_sub_rethrow_bb = NULL;
    g->current_async_sub_rethrow_phi_sub = NULL;
    g->current_async_sub_rethrow_phi_ev = NULL;
    g->current_async_sub_slot_ptr = NULL;

    /* 底层系统交互与数据协议契约 */
    LLVMTypeRef strcmp_args[] = { i8ptr, i8ptr };
    LLVMTypeRef strcmp_type = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), strcmp_args, 2, 0);
    g->fn_strcmp = LLVMAddFunction(g->mod, "strcmp", strcmp_type);

    /* 核心系统底层抽象与内存语义契约 */
    LLVMTypeRef strrchr_args[] = { i8ptr, LLVMInt32TypeInContext(g->ctx) };
    LLVMTypeRef strrchr_type = LLVMFunctionType(i8ptr, strrchr_args, 2, 0);
    LLVMAddFunction(g->mod, "strrchr", strrchr_type);

    /* 核心系统底层抽象与内存语义契约 */
    LLVMTypeRef strlen_args[] = { i8ptr };
    LLVMTypeRef strlen_type = LLVMFunctionType(i64, strlen_args, 1, 0);
    g->fn_strlen = LLVMAddFunction(g->mod, "strlen", strlen_type);

    /* 核心系统底层抽象与内存语义契约 */
    LLVMTypeRef strcpy_args[] = { i8ptr, i8ptr };
    LLVMTypeRef strcpy_type = LLVMFunctionType(i8ptr, strcpy_args, 2, 0);
    g->fn_strcpy = LLVMAddFunction(g->mod, "strcpy", strcpy_type);

    /* 核心系统底层抽象与内存语义契约 */
    LLVMTypeRef strcat_args[] = { i8ptr, i8ptr };
    LLVMTypeRef strcat_type = LLVMFunctionType(i8ptr, strcat_args, 2, 0);
    g->fn_strcat = LLVMAddFunction(g->mod, "strcat", strcat_type);

    /* 模块核心语义抽象与接口调用契约 */
    LLVMValueRef arc_trace_ev = NULL;
    LLVMTypeRef arc_ra_ty = NULL;
    LLVMTypeRef arc_ev_ty = NULL;
    if (g->check_leaks) {
        LLVMTypeRef arc_i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef arc_i32t = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef arc_i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMValueRef arc_trace_glob = LLVMAddGlobal(g->mod, arc_i32t, "__zan_arc_trace");
        LLVMSetInitializer(arc_trace_glob,
            LLVMConstInt(arc_i32t, (uint64_t)-1, 0));
        LLVMSetLinkage(arc_trace_glob, LLVMInternalLinkage);
        LLVMTypeRef arc_getenv_ty = LLVMFunctionType(arc_i8p,
            (LLVMTypeRef[]){ arc_i8p }, 1, 0);
        LLVMValueRef arc_getenv = LLVMGetNamedFunction(g->mod, "getenv");
        if (!arc_getenv) arc_getenv = LLVMAddFunction(g->mod, "getenv", arc_getenv_ty);
        arc_ra_ty = LLVMFunctionType(arc_i8p, (LLVMTypeRef[]){ arc_i32t }, 1, 0);
        LLVMValueRef arc_returnaddr = LLVMAddFunction(g->mod, "llvm.returnaddress", arc_ra_ty);
        LLVMTypeRef arc_ev_args[] = { arc_i8p, arc_i8p, arc_i64, arc_i64, arc_i8p };
        arc_ev_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), arc_ev_args, 5, 0);
        arc_trace_ev = LLVMAddFunction(g->mod, "__zan_arc_trace_ev", arc_ev_ty);
        LLVMSetLinkage(arc_trace_ev, LLVMInternalLinkage);
        LLVMBasicBlockRef ev_bb = LLVMAppendBasicBlockInContext(g->ctx, arc_trace_ev, "entry");
        LLVMBasicBlockRef ev_init = LLVMAppendBasicBlockInContext(g->ctx, arc_trace_ev, "init");
        LLVMBasicBlockRef ev_check = LLVMAppendBasicBlockInContext(g->ctx, arc_trace_ev, "check");
        LLVMBasicBlockRef ev_print = LLVMAppendBasicBlockInContext(g->ctx, arc_trace_ev, "print");
        LLVMBasicBlockRef ev_ret = LLVMAppendBasicBlockInContext(g->ctx, arc_trace_ev, "ret");
        LLVMPositionBuilderAtEnd(g->builder, ev_bb);
        LLVMValueRef ev_tag = LLVMGetParam(arc_trace_ev, 0);
        LLVMValueRef ev_obj = LLVMGetParam(arc_trace_ev, 1);
        LLVMValueRef ev_rc = LLVMGetParam(arc_trace_ev, 2);
        LLVMValueRef ev_site = LLVMGetParam(arc_trace_ev, 3);
        LLVMValueRef ev_ra = LLVMGetParam(arc_trace_ev, 4);
        LLVMValueRef ev_t = LLVMBuildLoad2(g->builder, arc_i32t, arc_trace_glob, "t");
        LLVMBuildCondBr(g->builder,
            zan_icmp(g->builder, LLVMIntEQ, ev_t,
                LLVMConstInt(arc_i32t, (uint64_t)-1, 0), "uninit"),
            ev_init, ev_check);
        LLVMPositionBuilderAtEnd(g->builder, ev_init);
        LLVMValueRef ev_key = zan_irgen_intern_string(g, "ZAN_ARC_TRACE");
        LLVMValueRef ev_hit = zan_call2(g->builder, arc_getenv_ty, arc_getenv,
            &ev_key, 1, "hit");
        LLVMValueRef ev_on = zan_icmp(g->builder, LLVMIntNE, ev_hit,
            LLVMConstNull(arc_i8p), "on");
        LLVMValueRef ev_on32 = LLVMBuildZExt(g->builder, ev_on, arc_i32t, "on32");
        LLVMBuildStore(g->builder, ev_on32, arc_trace_glob);
        LLVMBuildBr(g->builder, ev_check);
        LLVMPositionBuilderAtEnd(g->builder, ev_check);
        LLVMValueRef ev_phi = LLVMBuildPhi(g->builder, arc_i32t, "t2");
        LLVMValueRef ev_incomings[2] = { ev_t, ev_on32 };
        LLVMBasicBlockRef ev_inblocks[2] = { ev_bb, ev_init };
        LLVMAddIncoming(ev_phi, ev_incomings, ev_inblocks, 2);
        LLVMValueRef ev_off = zan_icmp(g->builder, LLVMIntEQ, ev_phi,
            LLVMConstInt(arc_i32t, 0, 0), "off");
        LLVMBuildCondBr(g->builder, ev_off, ev_ret, ev_print);
        LLVMPositionBuilderAtEnd(g->builder, ev_print);
        LLVMValueRef ev_obji = LLVMBuildPtrToInt(g->builder, ev_obj, arc_i64, "obji");
        LLVMValueRef ev_rai = LLVMBuildPtrToInt(g->builder, ev_ra, arc_i64, "rai");
        LLVMValueRef ev_msg = zan_irgen_intern_string(g,
            "[arc] %s obj=0x%llx site=%lld rc=%lld ra=0x%llx\n");
        LLVMValueRef ev_pargs[6] = { ev_msg, ev_tag, ev_obji, ev_site, ev_rc, ev_rai };
        zan_call2(g->builder, g->printf_type, g->fn_printf, ev_pargs, 6, "");
        LLVMBuildBr(g->builder, ev_ret);
        LLVMPositionBuilderAtEnd(g->builder, ev_ret);
        LLVMBuildRetVoid(g->builder);
        (void)ev_phi; (void)ev_obj; (void)ev_rc; (void)ev_site; (void)ev_ra;
        (void)ev_tag;
    }

    /* 底层系统交互与数据协议契约 */
    LLVMTypeRef retain_args[] = { i8ptr };
    LLVMTypeRef retain_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), retain_args, 1, 0);
    g->rt_retain = LLVMAddFunction(g->mod, "zan_rt_retain", retain_type);

    /* 底层系统交互与数据协议契约 */
    {
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_retain, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef obj = LLVMGetParam(g->rt_retain, 0);
        /* 核心系统底层抽象与内存语义契约 */
        LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, obj,
            LLVMConstNull(i8ptr), "isnull");
        LLVMBasicBlockRef do_retain = LLVMAppendBasicBlockInContext(g->ctx, g->rt_retain, "retain");
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_retain, "ret");
        LLVMBuildCondBr(g->builder, is_null, ret_bb, do_retain);
        LLVMPositionBuilderAtEnd(g->builder, do_retain);
        /* 底层系统交互与数据协议契约 */
        LLVMValueRef neg16 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_RC_OFF, 1);
        LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "rcptr");
        LLVMValueRef rc_iptr = LLVMBuildBitCast(g->builder, rc_ptr,
            LLVMPointerType(i64, 0), "rciptr");
        if (g->arc_guard) {
            LLVMValueRef rc_now = LLVMBuildLoad2(g->builder, i64, rc_iptr, "rcnow");
            emit_arc_freed_use_check(g, g->rt_retain, rc_now, obj,
                                     ZAN_ARC_FAULT_USE_OBJ,
                                     "retain through a stale reference (object "
                                     "was freed: missing retain when stored)");
        }
        LLVMValueRef rc_pre = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpAdd,
            rc_iptr, LLVMConstInt(i64, 1, 0), LLVMAtomicOrderingMonotonic, 0);
        emit_arc_underflow_check(g, g->rt_retain, rc_pre, obj, NULL,
                                 ZAN_ARC_FAULT_RETAIN,
                                 "retain of an already-freed object");
        if (arc_trace_ev) {
            LLVMValueRef trc_new = zan_add(g->builder, rc_pre,
                LLVMConstInt(i64, 1, 0), "trcnew");
            LLVMValueRef tneg8 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
            LLVMValueRef tsp = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                obj, &tneg8, 1, "trsp");
            LLVMValueRef tsite = LLVMBuildLoad2(g->builder, i64,
                LLVMBuildBitCast(g->builder, tsp, LLVMPointerType(i64, 0), "trsip"),
                "trsite");
            emit_arc_trace_call(g, arc_trace_ev, "R", obj, trc_new, tsite);
        }
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, ret_bb);
        LLVMBuildRetVoid(g->builder);
    }

    /* 内部辅助实现 */
    emit_weak_runtime(g);

    /* 底层系统交互与数据协议契约 */
    LLVMTypeRef release_args[] = { i8ptr };
    LLVMTypeRef release_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), release_args, 1, 0);
    g->rt_release = LLVMAddFunction(g->mod, "zan_rt_release", release_type);

    /* 底层系统交互与数据协议契约 */
    {
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef obj = LLVMGetParam(g->rt_release, 0);
        LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, obj,
            LLVMConstNull(i8ptr), "isnull");
        LLVMBasicBlockRef do_release = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "release");
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "ret");
        LLVMBuildCondBr(g->builder, is_null, ret_bb, do_release);
        LLVMPositionBuilderAtEnd(g->builder, do_release);
        LLVMValueRef neg16 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_RC_OFF, 1);
        LLVMValueRef neg8  = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
        LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "rcptr");
        LLVMValueRef rc_iptr = LLVMBuildBitCast(g->builder, rc_ptr,
            LLVMPointerType(i64, 0), "rciptr");
        if (g->arc_guard) {
            LLVMValueRef rc_now = LLVMBuildLoad2(g->builder, i64, rc_iptr, "rcnow");
            emit_arc_freed_use_check(g, g->rt_release, rc_now, obj,
                                     ZAN_ARC_FAULT_USE_OBJ,
                                     "release through a stale reference "
                                     "(object was already freed)");
        }
        /* 底层系统交互与数据协议契约 */
        LLVMValueRef rc_old = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpSub, rc_iptr,
            LLVMConstInt(i64, 1, 0), LLVMAtomicOrderingAcquireRelease, 0);
        {
            LLVMValueRef sp = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                obj, &neg8, 1, "arcsp");
            LLVMValueRef sip = LLVMBuildBitCast(g->builder, sp,
                LLVMPointerType(i64, 0), "arcsip");
            LLVMValueRef s = LLVMBuildLoad2(g->builder, i64, sip, "arcsite");
            emit_arc_underflow_check(g, g->rt_release, rc_old, obj, s,
                                     ZAN_ARC_FAULT_RELEASE,
                                     "release of an already-freed object");
        }
        LLVMValueRef rc1 = zan_sub(g->builder, rc_old, LLVMConstInt(i64, 1, 0), "rc1");
        if (arc_trace_ev) {
            LLVMValueRef rsneg8 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
            LLVMValueRef rsp = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                obj, &rsneg8, 1, "rrsp");
            LLVMValueRef rsite = LLVMBuildLoad2(g->builder, i64,
                LLVMBuildBitCast(g->builder, rsp, LLVMPointerType(i64, 0), "rrsip"),
                "rrsite");
            emit_arc_trace_call(g, arc_trace_ev, "r", obj, rc1, rsite);
        }
        /* 底层系统交互与数据协议契约 */
        LLVMValueRef is_zero = zan_icmp(g->builder, LLVMIntEQ, rc1,
            LLVMConstInt(i64, 0, 0), "iszero");
        LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "dofree");
        LLVMBuildCondBr(g->builder, is_zero, free_bb, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, free_bb);
        zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_weak_nil_all),
                  g->rt_weak_nil_all, &obj, 1, "");
        LLVMValueRef freefn = get_arc_free_decl(g);
        zan_call2(g->builder, LLVMGlobalGetValueType(freefn), freefn, &obj, 1, "");
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, ret_bb);
        LLVMBuildRetVoid(g->builder);
    }

    /* 内部辅助实现 */
    {
        if (!g->rt_arr_release) {
            LLVMTypeRef arr_fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), release_args, 1, 0);
            g->rt_arr_release = LLVMAddFunction(g->mod, "zan_rt_arr_release", arr_fnty);
        }
        LLVMTypeRef reld_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), release_args, 1, 0);
        g->rt_release_dyn = LLVMAddFunction(g->mod, "zan_rt_release_dyn", reld_type);
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef obj = LLVMGetParam(g->rt_release_dyn, 0);
        LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8ptr), "isnull");
        LLVMBasicBlockRef cont = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "cont");
        LLVMBasicBlockRef lookup = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "lookup");
        LLVMBasicBlockRef calld = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "calld");
        LLVMBasicBlockRef fb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "fallback");
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "ret");
        LLVMBuildCondBr(g->builder, is_null, ret_bb, cont);
        LLVMPositionBuilderAtEnd(g->builder, cont);
        /* 内部辅助实现 */
        LLVMValueRef sent16 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_RC_OFF, 1);
        LLVMValueRef sent_rc = LLVMBuildLoad2(g->builder, i64,
            LLVMBuildBitCast(g->builder,
                LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &sent16, 1, "sentp"),
                LLVMPointerType(i64, 0), "sentip"), "sentrc");
        LLVMBasicBlockRef literal = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "islit");
        LLVMBuildCondBr(g->builder,
            zan_icmp(g->builder, LLVMIntEQ, sent_rc,
                LLVMConstInt(i64, ZAN_STRING_SENTINEL_RC, 0), "issent"),
            ret_bb, literal);
        LLVMPositionBuilderAtEnd(g->builder, literal);
        if (arc_trace_ev) {
            LLVMValueRef dneg16 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_RC_OFF, 1);
            LLVMValueRef drcp = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                obj, &dneg16, 1, "drcp");
            LLVMValueRef drc = LLVMBuildLoad2(g->builder, i64,
                LLVMBuildBitCast(g->builder, drcp, LLVMPointerType(i64, 0), "drcip"),
                "drc");
            LLVMValueRef dneg8 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
            LLVMValueRef dsp = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                obj, &dneg8, 1, "ddsp");
            LLVMValueRef dsite = LLVMBuildLoad2(g->builder, i64,
                LLVMBuildBitCast(g->builder, dsp, LLVMPointerType(i64, 0), "ddsip"),
                "dsite");
            emit_arc_trace_call(g, arc_trace_ev, "D", obj, drc, dsite);
        }
        LLVMValueRef neg8 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
        LLVMValueRef sptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg8, 1, "sptr");
        LLVMValueRef siptr = LLVMBuildBitCast(g->builder, sptr, LLVMPointerType(i64, 0), "siptr");
        LLVMValueRef dpp;
        if (!g->desc_hdr) {
            /* 模块核心语义抽象与接口调用契约 */
            LLVMValueRef site = LLVMBuildLoad2(g->builder, i64, siptr, "site");
            LLVMValueRef bound = LLVMBuildLoad2(g->builder, i64,
                g->g_site_count, "bound");
            LLVMValueRef inrange = zan_icmp(g->builder, LLVMIntULT, site,
                bound, "inrange");
            LLVMBuildCondBr(g->builder, inrange, lookup, fb);
            LLVMPositionBuilderAtEnd(g->builder, lookup);
            LLVMValueRef dtbl = LLVMBuildLoad2(g->builder,
                LLVMPointerType(i8ptr, 0), g->g_site_dtors, "dtbl");
            LLVMValueRef dtor = LLVMBuildLoad2(g->builder, i8ptr,
                LLVMBuildGEP2(g->builder, i8ptr, dtbl, &site, 1, "dtorp"), "dtor");
            dpp = dtor;
            LLVMValueRef hasd = zan_icmp(g->builder, LLVMIntNE, dtor, LLVMConstNull(i8ptr), "hasd");
            LLVMBuildCondBr(g->builder, hasd, calld, fb);
        } else {
            /* 内部辅助逻辑 */
            LLVMValueRef desc = LLVMBuildLoad2(g->builder, i64, siptr, "desc");
            LLVMValueRef is_arr_magic = zan_icmp(g->builder, LLVMIntEQ, desc,
                LLVMConstInt(i64, ZAN_ARRAY_MAGIC, 0), "is.arrmagic");
            LLVMValueRef is_rank = zan_and(g->builder,
                zan_icmp(g->builder, LLVMIntUGT, desc, LLVMConstInt(i64, 0, 0), "is.rnz"),
                zan_icmp(g->builder, LLVMIntULT, desc, LLVMConstInt(i64, 4096, 0), "is.rank"),
                "is.isrank");
            LLVMValueRef is_raw_arr = zan_or(g->builder, is_arr_magic, is_rank, "is.rawarr");
            LLVMBasicBlockRef rel_arr_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "relarr");
            LLVMBasicBlockRef not_raw = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "notraw");
            LLVMBuildCondBr(g->builder, is_raw_arr, rel_arr_bb, not_raw);

            LLVMPositionBuilderAtEnd(g->builder, rel_arr_bb);
            zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
                g->rt_arr_release, &obj, 1, "");
            LLVMBuildBr(g->builder, ret_bb);

            LLVMPositionBuilderAtEnd(g->builder, not_raw);
            LLVMValueRef dnz = zan_icmp(g->builder, LLVMIntNE, desc,
                LLVMConstInt(i64, 0, 0), "descnz");
            LLVMBuildCondBr(g->builder, dnz, lookup, fb);
            LLVMPositionBuilderAtEnd(g->builder, lookup);
            LLVMValueRef desc_p = LLVMBuildIntToPtr(g->builder, desc,
                LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0), "desc.p");
            dpp = LLVMBuildLoad2(g->builder, i8ptr, desc_p, "dtor");
            LLVMValueRef hasd = zan_icmp(g->builder, LLVMIntNE, dpp,
                LLVMConstNull(i8ptr), "hasd");
            LLVMBuildCondBr(g->builder, hasd, calld, fb);
        }
        LLVMPositionBuilderAtEnd(g->builder, calld);
        LLVMTypeRef dfnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
        LLVMValueRef dfn = LLVMBuildBitCast(g->builder, dpp, LLVMPointerType(dfnty, 0), "dfn");
        zan_call2(g->builder, dfnty, dfn, &obj, 1, "");
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, fb);
        zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
            g->rt_release, &obj, 1, "");
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, ret_bb);
        LLVMBuildRetVoid(g->builder);
    }

    /* 内部辅助实现 */
    LLVMTypeRef alloc_args[] = { i64, i64, i8ptr };
    LLVMTypeRef alloc_type = LLVMFunctionType(i8ptr, alloc_args, 3, 0);
    g->rt_alloc = LLVMAddFunction(g->mod, "zan_rt_alloc", alloc_type);

    {
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_alloc, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef size = LLVMGetParam(g->rt_alloc, 0);
        LLVMValueRef site = LLVMGetParam(g->rt_alloc, 1);
        LLVMValueRef name = LLVMGetParam(g->rt_alloc, 2);
        /* 核心系统底层抽象与内存语义契约 */
        LLVMValueRef total = zan_add(g->builder, size, LLVMConstInt(i64, ZAN_OBJ_HDR_SIZE, 0), "total");
        LLVMTypeRef malloc_fn_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0);
        LLVMValueRef raw = zan_call2(g->builder, malloc_fn_type, g->fn_malloc, &total, 1, "raw");
        zan_irgen_emit_oom_check(g, g->rt_alloc, raw);
        /* refcount = 1 at raw[0..7] */
        LLVMValueRef rc_ptr = LLVMBuildBitCast(g->builder, raw, LLVMPointerType(i64, 0), "rcptr");
        LLVMBuildStore(g->builder, LLVMConstInt(i64, 1, 0), rc_ptr);
        /* 底层系统交互与数据协议契约 */
        LLVMValueRef eight = LLVMConstInt(i64, (uint64_t)(-ZAN_OBJ_SITE_OFF), 0);
        LLVMValueRef site_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), raw, &eight, 1, "sptr");
        LLVMValueRef site_iptr = LLVMBuildBitCast(g->builder, site_ptr, LLVMPointerType(i64, 0), "siptr");
        LLVMBuildStore(g->builder, site, site_iptr);
        /* 核心系统底层抽象与内存语义契约 */
        LLVMValueRef hdr_off = LLVMConstInt(i64, ZAN_OBJ_HDR_SIZE, 0);
        LLVMValueRef user_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), raw, &hdr_off, 1, "usr");
        if (g->check_leaks) {
            /* 模块核心语义抽象与接口调用契约 */
            emit_leak_counter_add(g, g->g_live, 1);
            LLVMValueRef ltbl = LLVMBuildLoad2(g->builder,
                LLVMPointerType(i64, 0), g->g_site_live, "ltbl");
            LLVMValueRef sc_ptr = LLVMBuildGEP2(g->builder, i64, ltbl, &site, 1, "scptr");
            emit_leak_counter_add(g, sc_ptr, 1);
            LLVMValueRef ntbl = LLVMBuildLoad2(g->builder,
                LLVMPointerType(i8ptr, 0), g->g_site_names, "ntbl");
            LLVMValueRef nm_ptr = LLVMBuildGEP2(g->builder, i8ptr, ntbl, &site, 1, "nmptr");
            LLVMBuildStore(g->builder, name, nm_ptr);
            emit_arc_trace_call(g, arc_trace_ev, "A", user_ptr,
                LLVMConstInt(i64, 1, 0), site);
        }
        LLVMBuildRet(g->builder, user_ptr);
    }

    /* 内部辅助实现 */
    {
        LLVMTypeRef str_alloc_args[] = { i64 };
        LLVMTypeRef str_alloc_type = LLVMFunctionType(i8ptr, str_alloc_args, 1, 0);
        g->rt_str_alloc = LLVMAddFunction(g->mod, "zan_rt_str_alloc", str_alloc_type);
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_alloc, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef size = LLVMGetParam(g->rt_str_alloc, 0);
        LLVMValueRef total = zan_add(g->builder, size, LLVMConstInt(i64, ZAN_OBJ_HDR_SIZE, 0), "total");
        LLVMTypeRef malloc_fn_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0);
        LLVMValueRef raw = zan_call2(g->builder, malloc_fn_type, g->fn_malloc, &total, 1, "raw");
        zan_irgen_emit_oom_check(g, g->rt_str_alloc, raw);
        LLVMValueRef rc_ptr = LLVMBuildBitCast(g->builder, raw, LLVMPointerType(i64, 0), "rcptr");
        LLVMBuildStore(g->builder, LLVMConstInt(i64, 1, 0), rc_ptr);
        LLVMValueRef eight = LLVMConstInt(i64, (uint64_t)(-ZAN_OBJ_SITE_OFF), 0);
        LLVMValueRef magic_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), raw, &eight, 1, "magicp");
        LLVMValueRef magic_iptr = LLVMBuildBitCast(g->builder, magic_ptr, LLVMPointerType(i64, 0), "magicip");
        LLVMBuildStore(g->builder,
                       LLVMConstInt(i64, ZAN_STR_HDR_WORD(ZAN_STR_LEN_UNKNOWN), 0),
                       magic_iptr);
        LLVMValueRef hdr_off = LLVMConstInt(i64, ZAN_OBJ_HDR_SIZE, 0);
        LLVMValueRef user_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), raw, &hdr_off, 1, "usr");
        if (g->check_leaks) {
            emit_leak_counter_add(g, g->g_live, 1);
        }
        LLVMBuildRet(g->builder, user_ptr);
    }

    /* 内部辅助逻辑 */
    for (int rel = 0; rel < 2; rel++) {
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
        LLVMTypeRef i8p = LLVMPointerType(i8t, 0);
        LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                            (LLVMTypeRef[]){ i8p }, 1, 0);
        const char *fn_name = rel ? "zan_rt_arr_release" : "zan_rt_arr_retain";
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fn_name);
        if (!fn)
            fn = LLVMAddFunction(g->mod, fn_name, fnty);
        if (rel) g->rt_arr_release = fn; else g->rt_arr_retain = fn;
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef obj = LLVMGetParam(fn, 0);
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "ret");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "cont");
        LLVMBuildCondBr(g->builder,
            zan_icmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8p), "isnull"),
            ret_bb, cont_bb);
        LLVMPositionBuilderAtEnd(g->builder, cont_bb);
        LLVMValueRef gmoff = LLVMConstInt(i64t, (uint64_t)ZAN_ARR_RC_MAGIC_OFF, 1);
        LLVMValueRef magic_ptr = LLVMBuildGEP2(g->builder, i8t, obj, &gmoff, 1, "magicp");
        emit_header_read_guard(g, fn, magic_ptr, ret_bb);
        LLVMValueRef magic = LLVMBuildLoad2(g->builder, i64t,
            LLVMBuildBitCast(g->builder, magic_ptr, LLVMPointerType(i64t, 0), "magicip"),
            "magic");
        LLVMValueRef has_magic = zan_icmp(g->builder, LLVMIntEQ, magic,
            LLVMConstInt(i64t, ZAN_ARRAY_RC_MAGIC, 0), "hasmagic");
        LLVMBasicBlockRef work_bb = LLVMAppendBasicBlockInContext(g->ctx, fn,
            rel ? "release" : "retain");
        LLVMBuildCondBr(g->builder, has_magic, work_bb, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, work_bb);
        LLVMValueRef rcoff = LLVMConstInt(i64t, (uint64_t)ZAN_ARR_RC_OFF, 1);
        LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, i8t, obj, &rcoff, 1, "rcptr");
        LLVMValueRef rc_iptr = LLVMBuildBitCast(g->builder, rc_ptr,
            LLVMPointerType(i64t, 0), "rciptr");
        LLVMValueRef rc = LLVMBuildLoad2(g->builder, i64t, rc_iptr, "rc");
        emit_arc_freed_use_check(g, fn, rc, obj, ZAN_ARC_FAULT_USE_ARR,
            rel ? "release through a stale reference (array was already freed)"
                : "retain through a stale reference (array was freed: missing "
                  "retain when stored)");
        if (!rel) {
            LLVMValueRef rc_pre = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpAdd,
                rc_iptr, LLVMConstInt(i64t, 1, 0), LLVMAtomicOrderingMonotonic, 0);
            emit_arc_underflow_check(g, fn, rc_pre, obj, NULL,
                ZAN_ARC_FAULT_ARR_RETAIN, "retain of an already-freed array");
            LLVMBuildBr(g->builder, ret_bb);
        } else {
            LLVMValueRef rc_old = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpSub,
                rc_iptr, LLVMConstInt(i64t, 1, 0), LLVMAtomicOrderingAcquireRelease, 0);
            emit_arc_underflow_check(g, fn, rc_old, obj, NULL,
                ZAN_ARC_FAULT_ARR_RELEASE, "release of an already-freed array");
            LLVMValueRef rc1 = zan_sub(g->builder, rc_old, LLVMConstInt(i64t, 1, 0), "rc1");
            LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "dofree");
            LLVMBuildCondBr(g->builder,
                zan_icmp(g->builder, LLVMIntEQ, rc1, LLVMConstInt(i64t, 0, 0), "iszero"),
                free_bb, ret_bb);
            LLVMPositionBuilderAtEnd(g->builder, free_bb);
            if (g->arc_guard) {
                emit_arc_quarantine(g, obj, rc_iptr);
            } else {
                LLVMValueRef raw = LLVMBuildGEP2(g->builder, i8t, obj, &rcoff, 1, "raw");
                zan_call2(g->builder,
                    LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8p, 1, 0),
                    g->fn_free, &raw, 1, "");
            }
            LLVMBuildBr(g->builder, ret_bb);
        }
        LLVMPositionBuilderAtEnd(g->builder, ret_bb);
        LLVMBuildRetVoid(g->builder);
    }

    /* 模块核心语义抽象与接口调用契约 */
    {
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef retain_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8p }, 1, 0);
        g->rt_str_retain = LLVMAddFunction(g->mod, "zan_rt_str_retain", retain_type);
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef obj = LLVMGetParam(g->rt_str_retain, 0);
        LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8p), "isnull");
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "ret");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "cont");
        LLVMBuildCondBr(g->builder, is_null, ret_bb, cont_bb);
        LLVMPositionBuilderAtEnd(g->builder, cont_bb);
        LLVMValueRef neg8 = LLVMConstInt(i64t, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
        LLVMValueRef magic_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg8, 1, "magicp");
        emit_header_read_guard(g, g->rt_str_retain, magic_ptr, ret_bb);
        LLVMValueRef magic_iptr = LLVMBuildBitCast(g->builder, magic_ptr, LLVMPointerType(i64t, 0), "magicip");
        LLVMValueRef magic = LLVMBuildLoad2(g->builder, i64t, magic_iptr, "magic");
        LLVMValueRef has_magic = zan_hdr_is_string(g, magic, "hasmagic");
        LLVMBasicBlockRef retain_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "retain");
        LLVMBasicBlockRef arr_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "arrfwd");
        LLVMBuildCondBr(g->builder, has_magic, retain_bb, arr_bb);
        LLVMPositionBuilderAtEnd(g->builder, arr_bb);
        zan_call2(g->builder, retain_type, g->rt_arr_retain, &obj, 1, "");
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, retain_bb);
        LLVMValueRef neg16 = LLVMConstInt(i64t, (uint64_t)ZAN_OBJ_RC_OFF, 1);
        LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "rcptr");
        LLVMValueRef rc_iptr = LLVMBuildBitCast(g->builder, rc_ptr, LLVMPointerType(i64t, 0), "rciptr");
        LLVMValueRef rc = LLVMBuildLoad2(g->builder, i64t, rc_iptr, "rc");
        LLVMValueRef is_sent = zan_icmp(g->builder, LLVMIntEQ, rc,
            LLVMConstInt(i64t, ZAN_STRING_SENTINEL_RC, 0), "issent");
        LLVMBasicBlockRef add_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "add");
        LLVMBuildCondBr(g->builder, is_sent, ret_bb, add_bb);
        LLVMPositionBuilderAtEnd(g->builder, add_bb);
        emit_arc_freed_use_check(g, g->rt_str_retain, rc, obj,
                                 ZAN_ARC_FAULT_USE_STR,
                                 "retain through a stale reference (string was "
                                 "freed: missing retain when stored)");
        LLVMValueRef rc_pre = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpAdd,
            rc_iptr, LLVMConstInt(i64t, 1, 0), LLVMAtomicOrderingMonotonic, 0);
        emit_arc_underflow_check(g, g->rt_str_retain, rc_pre, obj, NULL,
                                 ZAN_ARC_FAULT_STR_RETAIN,
                                 "retain of an already-freed string");
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, ret_bb);
        LLVMBuildRetVoid(g->builder);
    }

    {
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef release_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8p }, 1, 0);
        g->rt_str_release = LLVMAddFunction(g->mod, "zan_rt_str_release", release_type);
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef obj = LLVMGetParam(g->rt_str_release, 0);
        LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8p), "isnull");
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "ret");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "cont");
        LLVMBuildCondBr(g->builder, is_null, ret_bb, cont_bb);
        LLVMPositionBuilderAtEnd(g->builder, cont_bb);
        LLVMValueRef neg8 = LLVMConstInt(i64t, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
        LLVMValueRef magic_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg8, 1, "magicp");
        emit_header_read_guard(g, g->rt_str_release, magic_ptr, ret_bb);
        LLVMValueRef magic_iptr = LLVMBuildBitCast(g->builder, magic_ptr, LLVMPointerType(i64t, 0), "magicip");
        LLVMValueRef magic = LLVMBuildLoad2(g->builder, i64t, magic_iptr, "magic");
        LLVMValueRef has_magic = zan_hdr_is_string(g, magic, "hasmagic");
        LLVMBasicBlockRef rel_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "release");
        LLVMBasicBlockRef arr_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "arrfwd");
        LLVMBuildCondBr(g->builder, has_magic, rel_bb, arr_bb);
        LLVMPositionBuilderAtEnd(g->builder, arr_bb);
        zan_call2(g->builder, release_type, g->rt_arr_release, &obj, 1, "");
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, rel_bb);
        LLVMValueRef neg16 = LLVMConstInt(i64t, (uint64_t)ZAN_OBJ_RC_OFF, 1);
        LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "rcptr");
        LLVMValueRef rc_iptr = LLVMBuildBitCast(g->builder, rc_ptr, LLVMPointerType(i64t, 0), "rciptr");
        LLVMValueRef rc = LLVMBuildLoad2(g->builder, i64t, rc_iptr, "rc");
        LLVMValueRef is_sent = zan_icmp(g->builder, LLVMIntEQ, rc,
            LLVMConstInt(i64t, ZAN_STRING_SENTINEL_RC, 0), "issent");
        LLVMBasicBlockRef dec_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "dec");
        LLVMBuildCondBr(g->builder, is_sent, ret_bb, dec_bb);
        LLVMPositionBuilderAtEnd(g->builder, dec_bb);
        emit_arc_freed_use_check(g, g->rt_str_release, rc, obj,
                                 ZAN_ARC_FAULT_USE_STR,
                                 "release through a stale reference "
                                 "(string was already freed)");
        LLVMValueRef rc_old = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpSub, rc_iptr,
            LLVMConstInt(i64t, 1, 0), LLVMAtomicOrderingAcquireRelease, 0);
        emit_arc_underflow_check(g, g->rt_str_release, rc_old, obj, NULL,
                                 ZAN_ARC_FAULT_STR_RELEASE,
                                 "release of an already-freed string");
        LLVMValueRef rc1 = zan_sub(g->builder, rc_old, LLVMConstInt(i64t, 1, 0), "rc1");
        LLVMValueRef is_zero = zan_icmp(g->builder, LLVMIntEQ, rc1, LLVMConstInt(i64t, 0, 0), "iszero");
        LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "dofree");
        LLVMBuildCondBr(g->builder, is_zero, free_bb, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, free_bb);
        LLVMValueRef header_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "hdr");
        LLVMTypeRef free_fn_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8p }, 1, 0);
        if (g->arc_guard) emit_arc_quarantine(g, obj, rc_iptr);
        else zan_call2(g->builder, free_fn_type, g->fn_free, &header_ptr, 1, "");
        {
            emit_leak_counter_add(g, g->g_live, -1);
        }
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, ret_bb);
        LLVMBuildRetVoid(g->builder);
    }

    return ZAN_OK;
}

void zan_irgen_release_llvm(zan_irgen_t *g) {
    if (!g) return;
    if (s_current_irgen == g) s_current_irgen = NULL;
    if (g_di_emit_ctx == g) g_di_emit_ctx = NULL;
    zan_irgen_compactor_destroy((zan_irgen_compactor_t *)g->function_compactor);
    g->function_compactor = NULL;
    if (g->builder) {
        LLVMDisposeBuilder(g->builder);
        g->builder = NULL;
    }
    if (g->mod) {
        LLVMDisposeModule(g->mod);
        g->mod = NULL;
    }
    free(g->functions);
    g->functions = NULL;
    g->function_count = g->function_cap = 0;
    free(g->fn_index);
    g->fn_index = NULL;
    g->fn_index_cap = 0;
    free(g->struct_types);
    g->struct_types = NULL;
    g->struct_type_count = g->struct_type_cap = 0;
    free(g->obf_literals);
    g->obf_literals = NULL;
    g->obf_literal_count = g->obf_literal_cap = 0;
    free(g->string_literals);
    g->string_literals = NULL;
    g->string_literal_count = g->string_literal_cap = 0;
    free(g->goto_labels);
    g->goto_labels = NULL;
    g->goto_label_count = g->goto_label_cap = 0;
    free(g->goto_fixups);
    g->goto_fixups = NULL;
    g->goto_fixup_count = g->goto_fixup_cap = 0;
    free(g->bind_accs);
    g->bind_accs = NULL;
    g->bind_acc_count = g->bind_acc_cap = 0;
    free(g->catch_cleanups);
    g->catch_cleanups = NULL;
    g->catch_cleanup_count = g->catch_cleanup_cap = 0;
    free(g->body_write_memo);
    g->body_write_memo = NULL;
    g->body_write_memo_cap = g->body_write_memo_count = 0;
    g->body_write_scan_done = NULL;
    free(g->site_inst);
    g->site_inst = NULL;
    free(g->class_release);
    g->class_release = NULL;
    g->class_release_count = g->class_release_cap = 0;
    class_index_reset();
}

void zan_irgen_destroy(zan_irgen_t *g) {
    if (s_current_irgen == g) s_current_irgen = NULL;
    if (g_di_emit_ctx == g) g_di_emit_ctx = NULL;
    free(g->catch_cleanups);
    g->catch_cleanups = NULL;
    g->catch_cleanup_count = g->catch_cleanup_cap = 0;
    free(g->di_files);
    g->di_files = NULL;
    g->di_file_cap = 0;
    free(g->goto_labels);
    g->goto_labels = NULL;
    g->goto_label_count = g->goto_label_cap = 0;
    free(g->goto_fixups);
    g->goto_fixups = NULL;
    g->goto_fixup_count = g->goto_fixup_cap = 0;
    free(g->bind_accs);
    g->bind_accs = NULL;
    g->bind_acc_count = g->bind_acc_cap = 0;
    free(g->extern_libs);
    g->extern_libs = NULL;
    g->extern_lib_count = g->extern_lib_cap = 0;
    free(g->extern_fns);
    g->extern_fns = NULL;
    g->extern_fn_count = g->extern_fn_cap = 0;
    for (int i = 0; i < g->abi_pending_count; i++) free(g->abi_pending[i]);
    free(g->abi_pending);
    g->abi_pending = NULL;
    g->abi_pending_count = g->abi_pending_cap = 0;
    free(g->obf_literals);
    g->obf_literals = NULL;
    g->obf_literal_count = g->obf_literal_cap = 0;
    free(g->string_literals);
    g->string_literals = NULL;
    g->string_literal_count = g->string_literal_cap = 0;
    zan_irgen_compactor_destroy((zan_irgen_compactor_t *)g->function_compactor);
    g->function_compactor = NULL;
    if (g->builder) LLVMDisposeBuilder(g->builder);
    if (g->mod) LLVMDisposeModule(g->mod);
    /* 内部辅助实现 */
    /* if (g->ctx) LLVMContextDispose(g->ctx); */
    if (g->ctx) g->ctx = NULL;
    free(g->functions);
    g->functions = NULL;
    g->function_count = 0;
    g->function_cap = 0;
    free(g->fn_index);
    g->fn_index = NULL;
    g->fn_index_cap = 0;
    class_index_reset();
    free(g->struct_types);
    g->struct_types = NULL;
    g->struct_type_count = g->struct_type_cap = 0;
    free(g->body_write_memo);
    g->body_write_memo = NULL;
    g->body_write_memo_cap = g->body_write_memo_count = 0;
    g->body_write_scan_done = NULL;
    free(g->site_syms);
    g->site_syms = NULL;
    free(g->site_coll);
    g->site_coll = NULL;
    free(g->site_coll_elem);
    g->site_coll_elem = NULL;
    free(g->site_inst);
    g->site_inst = NULL;
    free(g->site_loc_file);
    g->site_loc_file = NULL;
    free(g->site_loc_line);
    g->site_loc_line = NULL;
    g->leak_site_cap = 0;
    free(g->class_release);
    g->class_release = NULL;
    g->class_release_count = g->class_release_cap = 0;
    free(g->ctors);
    g->ctors = NULL;
    g->ctor_count = g->ctor_cap = 0;
    free(g->generic_fns);
    g->generic_fns = NULL;
    g->generic_fn_count = g->generic_fn_cap = 0;
    free(g->generic_ctors);
    g->generic_ctors = NULL;
    g->generic_ctor_count = g->generic_ctor_cap = 0;
    free(g->generic_insts);
    g->generic_insts = NULL;
    g->generic_inst_count = g->generic_inst_cap = 0;
    free(g->method_specs);
    g->method_specs = NULL;
    g->method_spec_count = g->method_spec_cap = g->method_spec_emitted = 0;
    free(g->static_fields);
    g->static_fields = NULL;
    g->static_field_count = g->static_field_cap = 0;
    g->cur_mtps = NULL;
    g->cur_mbind = NULL;
    zan_irgen_shard_buf_free(g);
}

void zan_irgen_shard_buf_append(zan_irgen_t *g, LLVMValueRef fn, const char *txt) {
    if (!g || !txt || !txt[0]) return;
    if (g->streaming_shard_count == 0 || g->streaming_shard_cur_fns >= 250) {
        if (g->streaming_shard_count >= g->streaming_shard_cap) {
            int ncap = g->streaming_shard_cap ? g->streaming_shard_cap * 2 : 16;
            g->streaming_shards = (struct zan_shard_buf *)realloc(
                g->streaming_shards, (size_t)ncap * sizeof(*g->streaming_shards));
            for (int i = g->streaming_shard_cap; i < ncap; i++)
                memset(&g->streaming_shards[i], 0, sizeof(g->streaming_shards[i]));
            g->streaming_shard_cap = ncap;
        }
        g->streaming_shard_count++;
        g->streaming_shard_cur_fns = 0;
    }
    struct zan_shard_buf *sb = &g->streaming_shards[g->streaming_shard_count - 1];
    size_t tlen = strlen(txt);
    if (sb->len + tlen + 2 > sb->cap) {
        size_t ncap = sb->cap ? sb->cap * 2 : 65536;
        while (ncap < sb->len + tlen + 2) ncap *= 2;
        sb->text = (char *)realloc(sb->text, ncap);
        sb->cap = ncap;
    }
    memcpy(sb->text + sb->len, txt, tlen);
    sb->len += tlen;
    sb->text[sb->len++] = '\n';
    sb->text[sb->len] = '\0';
    if (fn) {
        if (sb->fn_count >= sb->fns_cap) {
            int ncap = sb->fns_cap ? sb->fns_cap * 2 : 64;
            sb->fns = (LLVMValueRef *)realloc(sb->fns, (size_t)ncap * sizeof(LLVMValueRef));
            sb->fns_cap = ncap;
        }
        sb->fns[sb->fn_count] = fn;
    }
    sb->fn_count++;
    g->streaming_shard_cur_fns++;
}

void zan_irgen_shard_buf_free(zan_irgen_t *g) {
    if (!g || !g->streaming_shards) return;
    for (int i = 0; i < g->streaming_shard_count; i++) {
        free(g->streaming_shards[i].text);
        free(g->streaming_shards[i].fns);
    }
    free(g->streaming_shards);
    g->streaming_shards = NULL;
    g->streaming_shard_count = g->streaming_shard_cap = g->streaming_shard_cur_fns = 0;
}

/* 核心系统底层抽象与内存语义契约 */

/* 模块核心语义抽象与接口调用契约 */
static zan_type_t *subst_method_tp(zan_irgen_t *g, zan_type_t *t,
                                   zan_ast_list_t *tps, zan_type_t **bind);
static int get_or_create_method_spec(zan_irgen_t *g, zan_symbol_t *msym,
                                     zan_type_t **bind, int bindc,
                                     zan_type_t *owner_inst);
static void emit_pending_method_specs(zan_irgen_t *g);

/* 内部辅助实现 */
static zan_type_t *subst_type_param_deep(zan_irgen_t *g, zan_type_t *t,
                                         zan_type_t *recv);

static zan_type_t *resolve_type_ctx(zan_irgen_t *g, zan_ast_node_t *tref) {
    zan_type_t *t = zan_binder_resolve_type(g->binder, tref);
    if (g->cur_mtps && t) t = subst_method_tp(g, t, g->cur_mtps, g->cur_mbind);
    /* 内部辅助实现 */
    if (t && g->cur_inst) t = subst_type_param_deep(g, t, g->cur_inst);
    return t;
}

/* 内部辅助实现 */
static zan_type_t *field_type_here(zan_irgen_t *g, zan_type_t *t) {
    if (t && g->cur_inst) t = subst_type_param_deep(g, t, g->cur_inst);
    return t;
}

/* 内部辅助实现 */
#define ZAN_NULLABLE_PREFIX "zan.nullable."

static LLVMValueRef coerce_int_to(zan_irgen_t *g, LLVMValueRef v, LLVMTypeRef target);

static bool llvm_is_nullable(LLVMTypeRef t) {
    if (!t || LLVMGetTypeKind(t) != LLVMStructTypeKind) return false;
    const char *n = LLVMGetStructName(t);
    return n && strncmp(n, ZAN_NULLABLE_PREFIX,
                        sizeof(ZAN_NULLABLE_PREFIX) - 1) == 0;
}

static LLVMTypeRef nullable_payload_type(LLVMTypeRef t) {
    return LLVMStructGetTypeAtIndex(t, 0);
}

/* 模块核心语义抽象与接口调用契约 */
static LLVMTypeRef nullable_type_of(zan_irgen_t *g, LLVMTypeRef payload) {
    char name[256];
    size_t off = 0;
    memcpy(name, ZAN_NULLABLE_PREFIX, sizeof(ZAN_NULLABLE_PREFIX) - 1);
    off = sizeof(ZAN_NULLABLE_PREFIX) - 1;
    char *pn = LLVMPrintTypeToString(payload);
    for (const char *p = pn; p && *p && off < sizeof(name) - 1; p++) {
        char c = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                 (*p >= '0' && *p <= '9') ? *p : '_';
        name[off++] = c;
    }
    name[off] = 0;
    if (pn) LLVMDisposeMessage(pn);
    LLVMTypeRef existing = LLVMGetTypeByName2(g->ctx, name);
    if (existing) return existing;
    LLVMTypeRef st = LLVMStructCreateNamed(g->ctx, name);
    LLVMTypeRef body[2] = { payload, LLVMInt1TypeInContext(g->ctx) };
    LLVMStructSetBody(st, body, 2, 0);
    return st;
}

/* 模块核心语义抽象与接口调用契约 */
static LLVMValueRef nullable_none(LLVMTypeRef nty) {
    return LLVMConstNull(nty);
}

/* 内部辅助实现 */
static LLVMValueRef nullable_some(zan_irgen_t *g, LLVMTypeRef nty, LLVMValueRef v) {
    LLVMTypeRef pl = nullable_payload_type(nty);
    LLVMValueRef fit = coerce_int_to(g, v, pl);
    if (LLVMTypeOf(fit) != pl) return NULL;
    LLVMValueRef agg = LLVMBuildInsertValue(g->builder, LLVMGetUndef(nty), fit,
                                            0, "nv.val");
    return LLVMBuildInsertValue(g->builder, agg,
                                LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0),
                                1, "nv.some");
}

static LLVMValueRef nullable_has_value(zan_irgen_t *g, LLVMValueRef v) {
    return LLVMBuildExtractValue(g->builder, v, 1, "nv.has");
}

static LLVMValueRef nullable_get_payload(zan_irgen_t *g, LLVMValueRef v) {
    return LLVMBuildExtractValue(g->builder, v, 0, "nv.get");
}

static void register_struct_type(zan_irgen_t *g, zan_symbol_t *sym);

/* 核心系统底层抽象与内存语义契约 */
static LLVMTypeRef map_tuple_struct(zan_irgen_t *g, zan_type_t *type) {
    if (!type || !type->sym) return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = 0; i < g->struct_type_count; i++) {
        if (g->struct_types[i].sym == type->sym) {
            return g->struct_types[i].llvm_type;
        }
    }
    if (!type->sym->decl) register_struct_type(g, type->sym);
    for (int i = 0; i < g->struct_type_count; i++) {
        if (g->struct_types[i].sym == type->sym) {
            return g->struct_types[i].llvm_type;
        }
    }
    return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
}

static LLVMTypeRef map_type(zan_irgen_t *g, zan_type_t *type) {
    if (!type) return LLVMVoidTypeInContext(g->ctx);
    switch (type->kind) {
    case TYPE_VOID:   return LLVMVoidTypeInContext(g->ctx);
    case TYPE_BOOL:   return LLVMInt1TypeInContext(g->ctx);
    case TYPE_BYTE:   return LLVMInt8TypeInContext(g->ctx);
    case TYPE_SHORT:  return LLVMInt16TypeInContext(g->ctx);
    case TYPE_INT:    return LLVMInt32TypeInContext(g->ctx);
    case TYPE_LONG:   return LLVMInt64TypeInContext(g->ctx);
    case TYPE_SBYTE:  return LLVMInt8TypeInContext(g->ctx);
    case TYPE_USHORT: return LLVMInt16TypeInContext(g->ctx);
    case TYPE_UINT:   return LLVMInt32TypeInContext(g->ctx);
    case TYPE_ULONG:  return LLVMInt64TypeInContext(g->ctx);
    case TYPE_NINT:   return LLVMInt64TypeInContext(g->ctx);
    case TYPE_TASK:   return LLVMInt64TypeInContext(g->ctx); /* 核心系统底层抽象与内存语义契约 */
    case TYPE_FLOAT:  return LLVMFloatTypeInContext(g->ctx);
    case TYPE_DOUBLE: return LLVMDoubleTypeInContext(g->ctx);
    case TYPE_CHAR:   return LLVMInt64TypeInContext(g->ctx);
    case TYPE_STRING: return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    case TYPE_OBJECT: return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    case TYPE_ENUM:
        return LLVMInt64TypeInContext(g->ctx);
    case TYPE_NULLABLE:
        /* 内部辅助逻辑 */
        if (type->element_type)
            return nullable_type_of(g, map_type(g, type->element_type));
        return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    case TYPE_DELEGATE: {
        /* 底层系统交互与数据协议契约 */
        int pc = type->delegate_param_count;
        LLVMTypeRef *param_types = (LLVMTypeRef *)calloc(
            (size_t)(pc > 0 ? pc : 1), sizeof(LLVMTypeRef));
        for (int i = 0; i < pc; i++) {
            param_types[i] = map_type(g, type->delegate_param_types[i]);
        }
        /* 内部辅助实现 */
        LLVMTypeRef ret = type->delegate_is_async
            ? LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0)
            : (type->delegate_ret_type
                ? map_type(g, type->delegate_ret_type)
                : LLVMVoidTypeInContext(g->ctx));
        LLVMTypeRef fn_type = LLVMFunctionType(ret, param_types, (unsigned)pc, 0);
        free(param_types);
        return LLVMPointerType(fn_type, 0);
    }
    case TYPE_STRUCT:
    case TYPE_CLASS: {
        if (type->name.len == 4 && memcmp(type->name.str, "Span", 4) == 0)
            return g->span_struct_type;
        /* 核心系统底层抽象与内存语义契约 */
        for (int i = 0; i < g->struct_type_count; i++) {
            if (g->struct_types[i].sym == type->sym) {
                if (type->kind == TYPE_CLASS) {
                    return LLVMPointerType(g->struct_types[i].llvm_type, 0);
                }
                return g->struct_types[i].llvm_type;
            }
        }
        /* 模块核心语义抽象与接口调用契约 */
        return map_tuple_struct(g, type);
    }
    default:          return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    }
}

/* 内部辅助逻辑 */
static LLVMValueRef promote_loaded(zan_irgen_t *g, LLVMValueRef v,
                                   zan_type_t *type) {
    if (!v || !type) return v;
    if (LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMIntegerTypeKind) return v;
    if (LLVMGetIntTypeWidth(LLVMTypeOf(v)) >= 64) return v;
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    switch (type->kind) {
    case TYPE_SBYTE:
        return LLVMBuildSExt(g->builder, v, i64t, "sx.i8");
    case TYPE_USHORT:
        return LLVMBuildZExt(g->builder, v, i64t, "zx.u16");
    case TYPE_UINT:
        return LLVMBuildZExt(g->builder, v, i64t, "zx.u32");
    default:
        return v;
    }
}

/* 核心系统底层抽象与内存语义契约 */

/* 内部辅助实现 */
static bool class_has_virtual_methods(zan_symbol_t *sym);
static int class_vptr_offset(zan_symbol_t *sym) {
    return class_has_virtual_methods(sym) ? 1 : 0;
}

static LLVMTypeRef get_struct_llvm_type(zan_irgen_t *g, zan_symbol_t *sym) {
    for (int i = 0; i < g->struct_type_count; i++) {
        if (g->struct_types[i].sym == sym) return g->struct_types[i].llvm_type;
    }
    return NULL;
}

static struct zan_struct_type_entry *get_struct_entry(zan_irgen_t *g,
                                                      zan_symbol_t *sym) {
    for (int i = 0; i < g->struct_type_count; i++) {
        if (g->struct_types[i].sym == sym) return &g->struct_types[i];
    }
    return NULL;
}

/* Address a field of an instance */
static LLVMValueRef emit_field_ptr(zan_irgen_t *g, zan_symbol_t *type_sym,
                                   LLVMTypeRef st, LLVMValueRef base,
                                   int fi, const char *name) {
    struct zan_struct_type_entry *e = type_sym ? get_struct_entry(g, type_sym) : NULL;
    if (e && e->explicit_layout && fi >= 0 && fi < e->field_count) {
        LLVMValueRef off = LLVMConstInt(LLVMInt64TypeInContext(g->ctx),
                                        e->field_offsets[fi], 0);
        LLVMValueRef raw = LLVMBuildInBoundsGEP2(g->builder,
            LLVMInt8TypeInContext(g->ctx), base, &off, 1, "fld.off");
        /* 内部辅助实现 */
        LLVMTypeRef wrap = LLVMStructTypeInContext(g->ctx, &e->field_llvm[fi], 1, 0);
        return LLVMBuildStructGEP2(g->builder, wrap, raw, 0, name);
    }
    return LLVMBuildStructGEP2(g->builder, st, base, (unsigned)fi, name);
}

/* 内部辅助实现 */
static bool field_member_is_static(zan_symbol_t *m) {
    if (m->modifiers & MOD_STATIC) return true;
    if (m->kind == SYM_FIELD && m->decl &&
        (m->decl->field_decl.modifiers & MOD_STATIC)) return true;
    return false;
}

/* 底层系统交互与数据协议契约 */
static int get_field_index(zan_symbol_t *type_sym, zan_istr_t field_name) {
    int idx = class_vptr_offset(type_sym);
    int found = -1;
    for (int i = 0; i < type_sym->member_count; i++) {
        if (type_sym->members[i]->kind == SYM_FIELD ||
            type_sym->members[i]->kind == SYM_PROPERTY) {
            if (field_member_is_static(type_sym->members[i])) continue;
            if (type_sym->members[i]->name.len == field_name.len &&
                memcmp(type_sym->members[i]->name.str, field_name.str, field_name.len) == 0) {
                found = idx;
            }
            idx++;
        }
    }
    return found;
}

/* 内部辅助逻辑 */
static zan_symbol_t *get_field_sym(zan_symbol_t *type_sym, zan_istr_t field_name) {
    zan_class_index_t *ci = class_index_for(type_sym);
    zan_symbol_t *found = NULL;
    for (int i = member_first_named(ci, field_name); i >= 0;
         i = member_next_named(ci, i)) {
        zan_symbol_t *m = type_sym->members[i];
        if ((m->kind == SYM_FIELD || m->kind == SYM_PROPERTY) &&
            member_name_is(m, field_name)) {
            found = m;
        }
    }
    return found;
}

static zan_symbol_t *get_method_sym(zan_symbol_t *type_sym, zan_istr_t method_name) {
    /* 核心系统底层抽象与内存语义契约 */
    zan_class_index_t *ci = class_index_for(type_sym);
    for (int i = member_first_named(ci, method_name); i >= 0;
         i = member_next_named(ci, i)) {
        zan_symbol_t *m = type_sym->members[i];
        if (m->kind == SYM_METHOD && member_name_is(m, method_name))
            return m;
    }
    /* 核心系统底层抽象与内存语义契约 */
    if (type_sym->type && type_sym->type->base_type && type_sym->type->base_type->sym) {
        return get_method_sym(type_sym->type->base_type->sym, method_name);
    }
    /* 内部辅助实现 */
    if (type_sym->kind == SYM_INTERFACE && type_sym->type) {
        for (int i = 0; i < type_sym->type->interface_count; i++) {
            zan_type_t *it = type_sym->type->interfaces[i];
            if (!it || !it->sym || it->sym == type_sym) continue;
            zan_symbol_t *m = get_method_sym(it->sym, method_name);
            if (m) return m;
        }
    }
    return NULL;
}

/* 返回the symbol that was created for a specific method-declaration AST node */
static zan_symbol_t *method_sym_for_decl(zan_symbol_t *type_sym, zan_ast_node_t *decl) {
    zan_class_index_t *ci = class_index_for(type_sym);
    for (int i = ci->decl_buckets[irgen_ptr_bucket(decl, ci->cap)]; i >= 0;
         i = ci->decl_next[i]) {
        zan_symbol_t *m = type_sym->members[i];
        if (m->kind == SYM_METHOD && m->decl == decl) return m;
    }
    return NULL;
}

/* 底层系统交互与数据协议契约 */
static int method_is_params_variadic(zan_symbol_t *m) {
    if (!m || !m->decl || m->decl->method_decl.params.count == 0) return 0;
    zan_ast_node_t *last =
        m->decl->method_decl.params.items[m->decl->method_decl.params.count - 1];
    return last->kind == AST_PARAM && last->param.is_params;
}

/* 内部辅助实现 */
static int method_accepts_arity(zan_symbol_t *m, int argc) {
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) return 0;
    int pc = m->decl->method_decl.params.count;
    if (pc == argc) return 1;
    /* 底层系统交互与数据协议契约 */
    if (m->decl->method_decl.is_variadic && argc > pc) return 1;
    if (method_is_params_variadic(m) && argc >= pc - 1) return 1;
    if (argc < pc) {
        for (int i = argc; i < pc; i++) {
            zan_ast_node_t *p = m->decl->method_decl.params.items[i];
            if (!p || p->kind != AST_PARAM || !p->param.default_val) return 0;
        }
        return 1;
    }
    return 0;
}

/* 内部辅助实现 */
static int method_type_param_count(zan_symbol_t *m) {
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) return 0;
    return m->decl->method_decl.type_params.count;
}

static zan_symbol_t *resolve_overload(zan_symbol_t *type_sym, zan_istr_t name,
                                      int argc, int type_arg_count) {
    int depth = 0;
    while (type_sym && depth++ < 512) {
        zan_symbol_t *variadic = NULL;
        zan_class_index_t *ci = class_index_for(type_sym);
        for (int i = member_first_named(ci, name); i >= 0;
             i = member_next_named(ci, i)) {
            zan_symbol_t *m = type_sym->members[i];
            if (m->kind != SYM_METHOD || !member_name_is(m, name)) continue;
            if (type_arg_count > 0 &&
                method_type_param_count(m) != type_arg_count)
                continue;
            if (m->decl && method_accepts_arity(m, argc)) {
                /* 内部辅助逻辑 */
                if (method_is_params_variadic(m)) {
                    if (!variadic) variadic = m;
                } else {
                    return m;
                }
            }
        }
        if (variadic) return variadic;
        if (!type_sym->type || !type_sym->type->base_type ||
            !type_sym->type->base_type->sym)
            break;
        type_sym = type_sym->type->base_type->sym;
    }
    return NULL;
}

/* 模块核心语义抽象与接口调用契约 */
static zan_symbol_t *resolve_iface_overload_depth(zan_symbol_t *iface,
                                                   zan_istr_t name, int argc,
                                                   int type_arg_count,
                                                   int depth) {
    if (!iface || depth > 512) return NULL;
    zan_symbol_t *m = resolve_overload(iface, name, argc, type_arg_count);
    if (m) return m;
    if (!iface->type) return NULL;
    for (int i = 0; i < iface->type->interface_count; i++) {
        zan_type_t *it = iface->type->interfaces[i];
        if (!it || !it->sym || it->sym == iface) continue;
        m = resolve_iface_overload_depth(it->sym, name, argc, type_arg_count,
                                         depth + 1);
        if (m) return m;
    }
    return NULL;
}

static zan_symbol_t *resolve_iface_overload(zan_symbol_t *iface, zan_istr_t name,
                                            int argc, int type_arg_count) {
    return resolve_iface_overload_depth(iface, name, argc, type_arg_count, 0);
}

/* Defined in irgen_abi */
static unsigned long abi_size_of(LLVMTypeRef t);
static unsigned long abi_align_of(LLVMTypeRef t);

static bool decl_is_explicit_layout(zan_symbol_t *sym) {
    return sym->decl &&
           (sym->decl->kind == AST_STRUCT_DECL || sym->decl->kind == AST_CLASS_DECL) &&
           sym->decl->type_decl.is_explicit_layout;
}

/* 模块核心语义抽象与接口调用契约 */
static bool field_offset_attr(zan_symbol_t *field, unsigned long *out) {
    if (!field->decl) return false;
    zan_ast_list_t *attrs = zan_ast_attributes(field->decl);
    for (int i = 0; i < attrs->count; i++) {
        zan_ast_node_t *a = attrs->items[i];
        if (a->kind != AST_ATTRIBUTE || !a->attribute.name) continue;
        zan_istr_t n = a->attribute.name->ident.name;
        if (!n.str || n.len != 11 || memcmp(n.str, "FieldOffset", 11) != 0) continue;
        if (a->attribute.args.count < 1) continue;
        zan_ast_node_t *v = a->attribute.args.items[0];
        if (v->kind != AST_INT_LITERAL || v->int_val < 0) continue;
        *out = (unsigned long)v->int_val;
        return true;
    }
    return false;
}

/* 内部辅助实现 */
static zan_type_t *subst_type_param_deep(zan_irgen_t *g, zan_type_t *t,
                                         zan_type_t *recv);
static unsigned long abi_size_of(LLVMTypeRef t);

static LLVMTypeRef generic_field_slot(zan_irgen_t *g, zan_symbol_t *sym,
                                      zan_type_t *ftype, LLVMTypeRef base) {
    if (!ftype || ftype->kind != TYPE_TYPE_PARAM) return base;
    unsigned long have = abi_size_of(base), need = have;
    for (int i = 0; i < g->generic_inst_count; i++) {
        if (g->generic_insts[i].type_sym != sym) continue;
        zan_type_t *ct = subst_type_param_deep(g, ftype,
                                               g->generic_insts[i].inst);
        if (!ct || ct == ftype) continue;
        unsigned long sz = abi_size_of(map_type(g, ct));
        if (sz > need) need = sz;
    }
    if (need <= have) return base;
    return LLVMArrayType(LLVMInt64TypeInContext(g->ctx),
                         (unsigned)((need + 7) / 8));
}

static void register_struct_type(zan_irgen_t *g, zan_symbol_t *sym) {
    if (get_struct_llvm_type(g, sym)) return;
    g->struct_types = irgen_grow(g->struct_types, &g->struct_type_cap,
                                 g->struct_type_count + 1,
                                 sizeof(*g->struct_types));

    /* 内部辅助实现 */
    char name_buf[256];
    snprintf(name_buf, sizeof(name_buf), "struct.%.*s", (int)sym->name.len, sym->name.str);
    LLVMTypeRef st = LLVMStructCreateNamed(g->ctx, name_buf);
    g->struct_types[g->struct_type_count].sym = sym;
    g->struct_types[g->struct_type_count].llvm_type = st;
    g->struct_type_count++;

    /* 模块核心语义抽象与接口调用契约 */
    int field_count = 0;
    for (int i = 0; i < sym->member_count; i++) {
        if ((sym->members[i]->kind == SYM_FIELD ||
             sym->members[i]->kind == SYM_PROPERTY) &&
            !field_member_is_static(sym->members[i])) field_count++;
    }

    /* 内部辅助逻辑 */
    int vptr = class_has_virtual_methods(sym) ? 1 : 0;
    LLVMTypeRef *field_types = (LLVMTypeRef *)calloc((size_t)(field_count + vptr), sizeof(LLVMTypeRef));
    int fi = 0;
    if (vptr) {
        field_types[fi++] = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    }
    for (int i = 0; i < sym->member_count; i++) {
        if ((sym->members[i]->kind == SYM_FIELD ||
             sym->members[i]->kind == SYM_PROPERTY) &&
            !field_member_is_static(sym->members[i])) {
            field_types[fi++] = generic_field_slot(g, sym,
                sym->members[i]->type, map_type(g, sym->members[i]->type));
        }
    }

    struct zan_struct_type_entry *entry = &g->struct_types[g->struct_type_count - 1];
    entry->field_count = field_count + vptr;
    entry->field_llvm = field_types;
    entry->explicit_layout = decl_is_explicit_layout(sym);

    if (!entry->explicit_layout) {
        /* 内部辅助实现 */
        LLVMStructSetBody(st, field_types, (unsigned)(field_count + vptr), 0);
        return;
    }

    entry->field_offsets = (unsigned long *)calloc((size_t)(field_count + vptr),
                                                   sizeof(unsigned long));
    unsigned long size = 0, align = 1;
    int slot = vptr;
    if (vptr) {
        /* 内部辅助实现 */
        zan_diag_emit(g->diag, DIAG_ERROR, sym->decl ? sym->decl->loc : zan_loc(0, 0, 0, 0),
                      "explicit-layout type '%.*s' cannot have virtual methods",
                      (int)sym->name.len, sym->name.str);
    }
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if ((m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) ||
            field_member_is_static(m)) continue;
        unsigned long off = 0;
        if (!field_offset_attr(m, &off)) {
            zan_diag_emit(g->diag, DIAG_ERROR, m->decl ? m->decl->loc : sym->decl->loc,
                          "field '%.*s' of explicit-layout type '%.*s' needs [FieldOffset(n)]",
                          (int)m->name.len, m->name.str,
                          (int)sym->name.len, sym->name.str);
        }
        entry->field_offsets[slot] = off;
        unsigned long fa = abi_align_of(field_types[slot]);
        if (off % fa != 0) {
            zan_diag_emit(g->diag, DIAG_ERROR, m->decl ? m->decl->loc : sym->decl->loc,
                          "[FieldOffset(%lu)] on '%.*s' is not %lu-byte aligned",
                          off, (int)m->name.len, m->name.str, fa);
        }
        if (fa > align) align = fa;
        unsigned long end = off + abi_size_of(field_types[slot]);
        if (end > size) size = end;
        slot++;
    }
    if (size % align) size += align - size % align;

    /* 内部辅助实现 */
    LLVMTypeRef body[2];
    unsigned nbody = 0;
    body[nbody++] = LLVMIntTypeInContext(g->ctx, (unsigned)(align * 8));
    if (size > align)
        body[nbody++] = LLVMArrayType(LLVMInt8TypeInContext(g->ctx),
                                      (unsigned)(size - align));
    LLVMStructSetBody(st, body, nbody, 0);
}

/* 核心系统底层抽象与内存语义契约 */

static bool class_has_virtual_methods(zan_symbol_t *sym) {
    for (int i = 0; i < sym->member_count; i++) {
        if (sym->members[i]->kind == SYM_METHOD &&
            (sym->members[i]->modifiers & (MOD_VIRTUAL | MOD_OVERRIDE))) {
            return true;
        }
    }
    /* 内部辅助实现 */
    if (sym->type && sym->type->interface_count > 0) return true;
    /* 核心系统底层抽象与内存语义契约 */
    if (sym->type && sym->type->base_type && sym->type->base_type->sym) {
        return class_has_virtual_methods(sym->type->base_type->sym);
    }
    return false;
}

static int count_virtual_methods(zan_symbol_t *sym) {
    int count = 0;
    /* 核心系统底层抽象与内存语义契约 */
    if (sym->type && sym->type->base_type && sym->type->base_type->sym) {
        count = count_virtual_methods(sym->type->base_type->sym);
    }
    /* 底层系统交互与数据协议契约 */
    for (int i = 0; i < sym->member_count; i++) {
        if (sym->members[i]->kind == SYM_METHOD &&
            (sym->members[i]->modifiers & MOD_VIRTUAL) &&
            !(sym->members[i]->modifiers & MOD_OVERRIDE)) {
            count++;
        }
    }
    return count;
}

/* 内部辅助逻辑 */
static int method_declared_param_count(zan_symbol_t *m) {
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) return -1;
    return m->decl->method_decl.params.count;
}

static int get_virtual_method_index(zan_symbol_t *type_sym,
                                    zan_symbol_t *method_sym) {
    if (!method_sym) return -1;
    /* 模块核心语义抽象与接口调用契约 */
    if (type_sym->type && type_sym->type->base_type && type_sym->type->base_type->sym) {
        int idx = get_virtual_method_index(type_sym->type->base_type->sym, method_sym);
        if (idx >= 0) return idx;
    }
    /* 核心系统底层抽象与内存语义契约 */
    int base_count = 0;
    if (type_sym->type && type_sym->type->base_type && type_sym->type->base_type->sym) {
        base_count = count_virtual_methods(type_sym->type->base_type->sym);
    }
    int want = method_declared_param_count(method_sym);
    int idx = base_count;
    for (int i = 0; i < type_sym->member_count; i++) {
        zan_symbol_t *m = type_sym->members[i];
        if (m->kind == SYM_METHOD &&
            (m->modifiers & MOD_VIRTUAL) &&
            !(m->modifiers & MOD_OVERRIDE)) {
            /* 内部辅助实现 */
            if (member_name_is(m, method_sym->name) &&
                method_declared_param_count(m) == want) {
                return idx;
            }
            idx++;
        }
    }
    return -1;
}

/* 底层系统交互与数据协议契约 */

#define INITIAL_LOCALS 16

typedef struct {
    zan_istr_t name;
    LLVMValueRef alloca;
    zan_type_t *type;
    /* 内部辅助实现 */
    int arc_owned;
    /* 内部辅助逻辑 */
    LLVMValueRef arr_len_slot;
    /* 内部辅助逻辑 */
    int eh_slot;
    /* 内部辅助实现 */
    LLVMValueRef box_cell;
    /* 模块核心语义抽象与接口调用契约 */
    LLVMValueRef box_owner_slot;
    /* 内部辅助实现 */
    int          box_owned;
    /* 内部辅助实现 */
    int          opaque_string;
    /* 内部辅助实现 */
    LLVMValueRef obj_rc_flag;
    /* 内部辅助逻辑 */
    int          byref_slot;
    /* 内部辅助实现 */
    zan_ast_node_t *binding_decl;
    /* 内部辅助逻辑 */
    zan_ast_node_t *async_decl;
    int async_role; /* 模块核心语义抽象与接口调用契约 */
    /* 内部辅助逻辑 */
    int frame_owner;
    /* 内部辅助逻辑 */
    int struct_rc;
    /* 内部辅助实现 */
    int per_iteration;
} local_var_t;

/* 底层系统交互与数据协议契约 */
typedef struct {
    zan_ast_node_t *decl;
    LLVMValueRef cell;
    LLVMValueRef owner_slot;
    zan_type_t *type;
    LLVMTypeRef payload;
    int eh_slot;
} pattern_binding_t;

typedef struct {
    local_var_t *vars;
    int count;
    int cap;
    zan_arena_t *arena;
    pattern_binding_t *patterns;
    int pattern_count;
    int pattern_cap;
} local_scope_t;

static void local_scope_init(local_scope_t *s, zan_arena_t *arena) {
    s->count = 0;
    s->cap = INITIAL_LOCALS;
    s->arena = arena;
    s->vars = (local_var_t *)zan_arena_alloc(arena, sizeof(local_var_t) * (size_t)s->cap);
    s->pattern_count = 0;
    s->pattern_cap = 0;
    s->patterns = NULL;
}

static local_scope_t *local_scope_new(zan_arena_t *arena) {
    local_scope_t *s = (local_scope_t *)zan_arena_alloc(arena, sizeof(local_scope_t));
    local_scope_init(s, arena);
    return s;
}

/* 核心系统底层抽象与内存语义契约 */
static LLVMTypeRef map_type(zan_irgen_t *g, zan_type_t *type);
static LLVMTypeRef local_slot_type(zan_irgen_t *g, local_var_t *v) {
    if (LLVMIsAAllocaInst(v->alloca)) return LLVMGetAllocatedType(v->alloca);
    return map_type(g, v->type);
}

/* 内部辅助实现 */
static unsigned g_local_gen;

static void local_add(local_scope_t *scope, zan_istr_t name, LLVMValueRef alloca, zan_type_t *type) {
    g_local_gen++;
    if (scope->count >= scope->cap) {
        int new_cap = scope->cap > 0 ? scope->cap * 2 : INITIAL_LOCALS;
        local_var_t *grown = (local_var_t *)zan_arena_alloc(scope->arena,
            sizeof(local_var_t) * (size_t)new_cap);
        if (scope->count > 0 && scope->vars) {
            memcpy(grown, scope->vars, sizeof(local_var_t) * (size_t)scope->count);
        }
        scope->vars = grown;
        scope->cap = new_cap;
    }
    scope->vars[scope->count].name = name;
    scope->vars[scope->count].alloca = alloca;
    scope->vars[scope->count].type = type;
    scope->vars[scope->count].arc_owned = 0;
    scope->vars[scope->count].byref_slot = 0;
    scope->vars[scope->count].eh_slot = 0;
    scope->vars[scope->count].arr_len_slot = NULL;
    scope->vars[scope->count].box_cell = NULL;
    scope->vars[scope->count].box_owner_slot = NULL;
    scope->vars[scope->count].box_owned = 0;
    scope->vars[scope->count].opaque_string = 0;
    scope->vars[scope->count].obj_rc_flag = NULL;
    scope->vars[scope->count].binding_decl = NULL;
    scope->vars[scope->count].async_decl = NULL;
    scope->vars[scope->count].async_role = 0;
    scope->vars[scope->count].frame_owner = -1;
    scope->vars[scope->count].struct_rc = 0;
    scope->vars[scope->count].per_iteration = 0;
    scope->count++;
    /* 模块核心语义抽象与接口调用契约 */
    di_declare_var(g_di_emit_ctx, name, alloca, type);
}

static LLVMValueRef emit_entry_alloca(zan_irgen_t *g, LLVMTypeRef ty, const char *name) {
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
    LLVMValueRef fn = LLVMGetBasicBlockParent(cur);
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(fn);
    LLVMValueRef term = LLVMGetBasicBlockTerminator(entry);
    if (term) LLVMPositionBuilderBefore(g->builder, term);
    else LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef alloca = LLVMBuildAlloca(g->builder, ty, name);
    /* 内部辅助逻辑 */
    if (LLVMGetTypeKind(ty) == LLVMPointerTypeKind)
        LLVMBuildStore(g->builder, LLVMConstNull(ty), alloca);
    LLVMPositionBuilderAtEnd(g->builder, cur);
    return alloca;
}

static local_var_t *local_find(local_scope_t *scope, zan_istr_t name) {
    for (int i = scope->count - 1; i >= 0; i--) {
        if (scope->vars[i].name.len == name.len &&
            memcmp(scope->vars[i].name.str, name.str, (size_t)name.len) == 0) {
            return &scope->vars[i];
        }
    }
    return NULL;
}

/* 编译期中间表示与代码生成内部规范 */
enum {
    ASYNC_LOCAL_VALUE = 0,
    ASYNC_FOREACH_INDEX,
    ASYNC_FOREACH_COLLECTION,
    ASYNC_FOREACH_ENUMERATOR
};

static local_var_t *local_find_async_role(local_scope_t *scope,
                                         zan_ast_node_t *decl, int role) {
    if (!decl) return NULL;
    for (int i = scope->count - 1; i >= 0; i--) {
        if (scope->vars[i].async_decl == decl && scope->vars[i].async_role == role)
            return &scope->vars[i];
    }
    return NULL;
}

static local_var_t *local_find_async_decl(local_scope_t *scope, zan_ast_node_t *decl) {
    return local_find_async_role(scope, decl, ASYNC_LOCAL_VALUE);
}

/* 模块核心语义抽象与接口调用契约 */
static local_var_t *local_find_binding_decl(local_scope_t *scope,
                                            zan_ast_node_t *decl) {
    if (!scope || !decl) return NULL;
    for (int i = scope->count - 1; i >= 0; i--) {
        if (scope->vars[i].binding_decl == decl) return &scope->vars[i];
    }
    return NULL;
}

static pattern_binding_t *local_find_pattern_binding(local_scope_t *scope,
                                                      zan_ast_node_t *decl) {
    if (!scope || !decl) return NULL;
    for (int i = scope->pattern_count - 1; i >= 0; i--)
        if (scope->patterns[i].decl == decl) return &scope->patterns[i];
    return NULL;
}

static pattern_binding_t *local_add_pattern_binding(local_scope_t *scope,
                                                     zan_ast_node_t *decl) {
    if (!scope || !decl) return NULL;
    if (scope->pattern_count >= scope->pattern_cap) {
        int nc = scope->pattern_cap ? scope->pattern_cap * 2 : 16;
        pattern_binding_t *np = (pattern_binding_t *)zan_arena_alloc(
            scope->arena, sizeof(pattern_binding_t) * (size_t)nc);
        if (scope->pattern_count)
            memcpy(np, scope->patterns,
                   sizeof(pattern_binding_t) * (size_t)scope->pattern_count);
        scope->patterns = np;
        scope->pattern_cap = nc;
    }
    pattern_binding_t *p = &scope->patterns[scope->pattern_count++];
    memset(p, 0, sizeof(*p));
    p->decl = decl;
    return p;
}

/* 底层系统交互与数据协议契约 */
static int type_named(zan_type_t *t, const char *n, int len) {
    return t && t->kind != TYPE_ARRAY && t->name.str &&
           (int)t->name.len == len && memcmp(t->name.str, n, (size_t)len) == 0;
}

/* 内部辅助实现 */
static int is_builtin_collection_type(zan_type_t *t) {
    if (!t || t->kind != TYPE_CLASS) return 0;
    zan_istr_t n = t->name;
    return (n.len == 4 && memcmp(n.str, "List", 4) == 0) ||
           (n.len == 4 && memcmp(n.str, "Dict", 4) == 0) ||
           (n.len == 13 && memcmp(n.str, "StringBuilder", 13) == 0);
}

/* 内部辅助实现 */
static int is_rc_collection_type(zan_type_t *t) {
    if (!t || t->kind != TYPE_CLASS) return 0;
    zan_istr_t n = t->name;
    return (n.len == 4 && memcmp(n.str, "List", 4) == 0) ||
           (n.len == 4 && memcmp(n.str, "Dict", 4) == 0) ||
           (n.len == 13 && memcmp(n.str, "StringBuilder", 13) == 0);
}

/* 内部辅助实现 */
static int is_arc_managed_type(zan_type_t *t) {
    if (!t) return 0;
    /* 内部辅助实现 */
    if (t->kind == TYPE_INTERFACE) return 1;
    if (t->kind != TYPE_CLASS) return 0;
    if (is_builtin_collection_type(t)) return is_rc_collection_type(t);
    return 1;
}

static int is_rc_managed_type(zan_type_t *t) {
    /* 内部辅助实现 */
    return t && (t->kind == TYPE_STRING || t->kind == TYPE_DELEGATE ||
                 t->kind == TYPE_ARRAY || is_arc_managed_type(t));
}

static void emit_rc_release_for_type(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v);
static int expr_yields_owned_rc_value(zan_irgen_t *g, zan_ast_node_t *e,
                                      local_scope_t *locals);
static void emit_rc_retain_for_type(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v);
static void emit_arc_release_typed(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v);
static LLVMValueRef get_class_release_decl(zan_irgen_t *g, zan_symbol_t *sym,
                                          zan_type_t *inst);
static void emit_list_release_elems(zan_irgen_t *g, zan_type_t *elem_type, LLVMValueRef col);
static void emit_dict_release_elems(zan_irgen_t *g, zan_type_t *dict_type, LLVMValueRef col);
static void emit_array_release_elems(zan_irgen_t *g, zan_type_t *elem_type,
                                     LLVMValueRef arr, LLVMValueRef len);
static void emit_release_obj_value(zan_irgen_t *g, LLVMValueRef cur);
static void emit_release_obj_local(zan_irgen_t *g, local_var_t *v);
static void emit_struct_local_release(zan_irgen_t *g, zan_type_t *type,
                                      LLVMValueRef slot);
static LLVMValueRef zan_store_fit(zan_irgen_t *g, LLVMValueRef val, LLVMValueRef ptr);

/* 模块核心语义抽象与接口调用契约 */
static void release_all_arc_locals(zan_irgen_t *g, local_scope_t *locals) {
    for (int i = 0; i < locals->count; i++) {
        /* 模块核心语义抽象与接口调用契约 */
        if (locals->vars[i].box_cell) continue;
        if (locals->vars[i].obj_rc_flag) {
            emit_release_obj_local(g, &locals->vars[i]);
        } else if (locals->vars[i].struct_rc) {
            emit_struct_local_release(g, locals->vars[i].type,
                                      locals->vars[i].alloca);
        } else if (is_rc_managed_type(locals->vars[i].type)) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef val = LLVMBuildLoad2(g->builder, i8ptr,
                locals->vars[i].alloca, "arc_cleanup");
            emit_rc_release_for_type(g, locals->vars[i].type, val);
        }
    }
}

/* 内部辅助实现 */
static unsigned llvm_scalar_size(LLVMTypeRef t) {
    switch (LLVMGetTypeKind(t)) {
    case LLVMIntegerTypeKind: return (LLVMGetIntTypeWidth(t) + 7) / 8;
    case LLVMPointerTypeKind: return 8;
    case LLVMFloatTypeKind:   return 4;
    case LLVMDoubleTypeKind:  return 8;
    case LLVMStructTypeKind: {
        unsigned total = 0;
        unsigned n = LLVMCountStructElementTypes(t);
        for (unsigned i = 0; i < n; i++) {
            unsigned fs = llvm_scalar_size(LLVMStructGetTypeAtIndex(t, i));
            if (!fs) return 0;
            if (fs > 1 && total % fs) total += fs - (total % fs);  /* 核心系统底层抽象与内存语义契约 */
            total += fs;
        }
        return total;
    }
    default: return 0;
    }
}

/* 模块核心语义抽象与接口调用契约 */
static unsigned elem_slot_words(zan_irgen_t *g, zan_type_t *elem) {
    if (!elem || elem->kind != TYPE_STRUCT) return 1;
    LLVMTypeRef st = map_type(g, elem);
    if (!st || LLVMGetTypeKind(st) != LLVMStructTypeKind) return 1;
    unsigned sz = llvm_scalar_size(st);
    if (!sz) return 1;
    return (sz + 7) / 8;
}

/* 模块核心语义抽象与接口调用契约 */
static LLVMValueRef slot_word_index(zan_irgen_t *g, LLVMValueRef idx,
                                    unsigned words) {
    if (words <= 1) return idx;
    return zan_mul(g->builder, idx,
                   LLVMConstInt(LLVMTypeOf(idx), words, 0), "slot.wi");
}

/* 模块核心语义抽象与接口调用契约 */
static void check_struct_fits_slot(zan_irgen_t *g, LLVMTypeRef st, zan_ast_node_t *at) {
    if (llvm_scalar_size(st)) return;
    zan_loc_t loc; memset(&loc, 0, sizeof(loc));
    if (at) loc = at->loc;
    zan_diag_emit(g->diag, DIAG_ERROR, loc,
        "this value struct cannot be a collection element: the slot allocator "
        "cannot compute its layout; use a class instead");
}

/* 内部辅助实现 */
static void store_struct_in_slot(zan_irgen_t *g, LLVMValueRef v,
                                 LLVMValueRef slot_ptr, zan_ast_node_t *at) {
    LLVMTypeRef st = LLVMTypeOf(v);
    check_struct_fits_slot(g, st, at);
    LLVMValueRef sp = LLVMBuildBitCast(g->builder, slot_ptr,
        LLVMPointerType(st, 0), "slot.sp");
    LLVMBuildStore(g->builder, v, sp);
}

static LLVMValueRef load_struct_from_slot(zan_irgen_t *g, LLVMValueRef slot_ptr,
                                          LLVMTypeRef st) {
    LLVMValueRef sp = LLVMBuildBitCast(g->builder, slot_ptr,
        LLVMPointerType(st, 0), "slot.lp");
    return LLVMBuildLoad2(g->builder, st, sp, "slot.struct");
}

/* 内部辅助实现 */
static LLVMValueRef extend_int_for_slot(zan_irgen_t *g, LLVMValueRef v,
                                        zan_type_t *elem_type,
                                        LLVMTypeRef slot_ty) {
    int uns = 0;
    if (elem_type) {
        switch (elem_type->kind) {
        case TYPE_BOOL: case TYPE_BYTE: case TYPE_USHORT:
        case TYPE_UINT: case TYPE_ULONG: case TYPE_CHAR:
            uns = 1; break;
        default: break;
        }
    }
    return uns ? LLVMBuildZExt(g->builder, v, slot_ty, "slot.zx")
               : LLVMBuildSExt(g->builder, v, slot_ty, "slot.sx");
}

/* 内部辅助实现 */
static void emit_collection_slot_store(zan_irgen_t *g, zan_type_t *elem_type,
                                       LLVMTypeRef slot_ty, LLVMValueRef slot_ptr,
                                       LLVMValueRef value,
                                       zan_ast_node_t *rhs, local_scope_t *locals,
                                       int overwrite_old);
static void emit_collection_release_raw_slot(zan_irgen_t *g, zan_type_t *elem_type,
                                             LLVMValueRef raw, LLVMTypeRef slot_ty);

static LLVMValueRef get_calloc_fn(zan_irgen_t *g) {
    LLVMValueRef f = LLVMGetNamedFunction(g->mod, "calloc");
    if (!f) {
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0);
        f = LLVMAddFunction(g->mod, "calloc", ft);
    }
    return f;
}

/* 内部辅助逻辑 */
static LLVMValueRef emit_alloc_rc_collection(zan_irgen_t *g, zan_ast_node_t *expr,
                                             long size, int coll_kind,
                                             zan_type_t *elem_type) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    /* 内部辅助实现 */
    if (g->cur_inst) elem_type = subst_type_param_deep(g, elem_type, g->cur_inst);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int site_idx = reserve_arc_site(g, NULL, NULL, coll_kind, elem_type);
    LLVMValueRef site_name = LLVMConstNull(i8ptr);
    if (g->check_leaks) {
        char site_buf[600];
        const char *sfile = loc_site_file(g, expr->loc);
        const char *knm = (coll_kind == 2) ? "StringBuilder"
                        : (coll_kind == 3) ? "Dictionary" : "List";
        int elen = (elem_type && elem_type->name.len) ? elem_type->name.len : 1;
        const char *estr = (elem_type && elem_type->name.len) ? elem_type->name.str : "?";
        snprintf(site_buf, sizeof(site_buf), "%s:%u:%u [%s<%.*s>]",
                 sfile, expr->loc.line, expr->loc.col, knm, elen, estr);
        site_name = zan_irgen_intern_string(g, site_buf);
    }
    LLVMTypeRef alloc_fn_type = LLVMFunctionType(i8ptr,
        (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0);
    LLVMValueRef args[] = { LLVMConstInt(i64, (unsigned long long)size, 0),
                            arc_site_arg(g, site_idx),
                            site_name };
    return zan_call2(g->builder, alloc_fn_type, g->rt_alloc, args, 3, "coll");
}

/* 内部辅助实现 */

static int eh_slot_kind_of(zan_type_t *t) {
    if (!t) return ZAN_EH_SLOT_OBJ;
    if (t->kind == TYPE_STRING) return ZAN_EH_SLOT_STR;
    if (t->kind == TYPE_DELEGATE) return ZAN_EH_SLOT_DLG;
    if (t->kind == TYPE_ARRAY) return ZAN_EH_SLOT_ARR;
    return ZAN_EH_SLOT_OBJ;
}

/* 内部辅助实现 */
static LLVMValueRef get_itoa64_fn(zan_irgen_t *g) {
    if (g->fn_itoa64) return g->fn_itoa64;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_itoa64", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_itoa64 = fn;

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef digit = LLVMAppendBasicBlockInContext(g->ctx, fn, "digit");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMBasicBlockRef sign = LLVMAppendBasicBlockInContext(g->ctx, fn, "sign");
    LLVMBasicBlockRef copy = LLVMAppendBasicBlockInContext(g->ctx, fn, "copy");

    LLVMValueRef buf = LLVMGetParam(fn, 0);
    LLVMValueRef v = LLVMGetParam(fn, 1);
    LLVMValueRef uns = LLVMGetParam(fn, 2);
    LLVMValueRef zero64 = LLVMConstInt(i64, 0, 0);
    LLVMValueRef ten = LLVMConstInt(i64, 10, 0);
    LLVMValueRef one = LLVMConstInt(i64, 1, 0);
    /* 内部辅助实现 */
    LLVMValueRef cap = LLVMConstInt(i64, 24, 0);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef tmp = LLVMBuildArrayAlloca(g->builder, i8, cap, "itoa.tmp");
    LLVMValueRef is_neg = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSLT, v, zero64, "itoa.slt"),
        LLVMBuildICmp(g->builder, LLVMIntEQ, uns,
                      LLVMConstInt(i32, 0, 0), "itoa.signed"), "itoa.neg");
    /* 内部辅助逻辑 */
    LLVMValueRef mag = LLVMBuildSelect(g->builder, is_neg,
        LLVMBuildNeg(g->builder, v, "itoa.negv"), v, "itoa.mag");
    LLVMBuildBr(g->builder, digit);

    LLVMPositionBuilderAtEnd(g->builder, digit);
    LLVMValueRef rest = LLVMBuildPhi(g->builder, i64, "itoa.rest");
    LLVMValueRef pos = LLVMBuildPhi(g->builder, i64, "itoa.pos");
    LLVMValueRef next_pos = LLVMBuildSub(g->builder, pos, one, "itoa.pos.n");
    LLVMValueRef d = LLVMBuildURem(g->builder, rest, ten, "itoa.d");
    LLVMValueRef ch = LLVMBuildTrunc(g->builder,
        LLVMBuildAdd(g->builder, d, LLVMConstInt(i64, '0', 0), "itoa.dc"),
        i8, "itoa.ch");
    LLVMBuildStore(g->builder, ch,
        LLVMBuildInBoundsGEP2(g->builder, i8, tmp, &next_pos, 1, "itoa.dp"));
    LLVMValueRef next_rest = LLVMBuildUDiv(g->builder, rest, ten, "itoa.rest.n");
    LLVMAddIncoming(rest, (LLVMValueRef[]){ mag, next_rest },
                    (LLVMBasicBlockRef[]){ entry, digit }, 2);
    LLVMAddIncoming(pos, (LLVMValueRef[]){ cap, next_pos },
                    (LLVMBasicBlockRef[]){ entry, digit }, 2);
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, next_rest, zero64, "itoa.last"),
        done, digit);

    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildCondBr(g->builder, is_neg, sign, copy);

    LLVMPositionBuilderAtEnd(g->builder, sign);
    LLVMValueRef sign_pos = LLVMBuildSub(g->builder, next_pos, one, "itoa.sp");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, '-', 0),
        LLVMBuildInBoundsGEP2(g->builder, i8, tmp, &sign_pos, 1, "itoa.spp"));
    LLVMBuildBr(g->builder, copy);

    LLVMPositionBuilderAtEnd(g->builder, copy);
    LLVMValueRef start = LLVMBuildPhi(g->builder, i64, "itoa.start");
    LLVMAddIncoming(start, (LLVMValueRef[]){ next_pos, sign_pos },
                    (LLVMBasicBlockRef[]){ done, sign }, 2);
    LLVMValueRef len = LLVMBuildSub(g->builder, cap, start, "itoa.len");
    LLVMValueRef src = LLVMBuildInBoundsGEP2(g->builder, i8, tmp, &start, 1,
                                             "itoa.src");
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_ty);
    zan_call2(g->builder, memcpy_ty, memcpy_fn,
              (LLVMValueRef[]){ buf, src, len }, 3, "");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0),
        LLVMBuildInBoundsGEP2(g->builder, i8, buf, &len, 1, "itoa.end"));
    LLVMBuildRet(g->builder, len);

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

/* 内部辅助实现 */
static LLVMValueRef emit_itoa_into(zan_irgen_t *g, LLVMValueRef buf,
                                   LLVMValueRef v, int is_unsigned) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef fn = get_itoa64_fn(g);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0);
    LLVMValueRef args[] = { buf, v, LLVMConstInt(i32, is_unsigned ? 1 : 0, 0) };
    return zan_call2(g->builder, fn_ty, fn, args, 3, "itoa");
}

/* 内部辅助实现 */
static void emit_dbl_str(zan_irgen_t *g, LLVMValueRef buf, LLVMValueRef cap,
                         LLVMValueRef v) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i8ptr, i64t, dbl }, 3, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_rt_dbl_str");
    if (!fn) fn = LLVMAddFunction(g->mod, "zan_rt_dbl_str", fn_ty);
    LLVMValueRef arg = v;
    if (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMFloatTypeKind)
        arg = LLVMBuildFPExt(g->builder, v, dbl, "dbl.ext");
    LLVMValueRef args[] = { buf, cap, arg };
    zan_call2(g->builder, fn_ty, fn, args, 3, "");
}

/* 内部辅助实现 */
static void zan_irgen_compact_completed(zan_irgen_t *g, LLVMValueRef fn) {
    if (!g->function_compactor || zan_diag_has_errors(g->diag)) return;
    char error[4096];
    if (!zan_irgen_compactor_run((zan_irgen_compactor_t *)g->function_compactor,
                                 fn, error, sizeof(error))) {
        zan_diag_emit(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0),
                      "LLVM function compaction failed: %s", error);
    }
}

/* 内部辅助实现 */
#include "irgen_expr_core.c"
#include "irgen_weak.c"
#include "irgen_arc.c"
#include "irgen_generics.c"
#include "irgen_reflect.c"
#include "irgen_builtins.c"
#include "irgen_expr.c"
#include "irgen_abi.c"
#include "irgen_call.c"
#include "irgen_async.c"
#include "irgen_stmt.c"
#include "irgen_emit.c"
#include "irgen_manifest.c"
#include "irgen_shard.c"
