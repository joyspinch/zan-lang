#include "irgen_internal.h"

static LLVMMetadataRef di_file_for(zan_irgen_t *g, uint32_t file_id);

static void di_ensure(zan_irgen_t *g) {
    if (!g->emit_debug || g->di_builder) return;
    g->di_builder = LLVMCreateDIBuilder(g->mod);

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
    /* B-ID162：di_clear 将 di_cur_file 置为 UINT32_MAX。当在作用域初始化或局部
     * 变量发射（di_declare_var）等尚未设立当前源码位置时，传入 UINT32_MAX
     * 被强转为 (int)-1，zan_tab_reserve 误判未超 cap 直接返回，后续
     * g->di_files[UINT32_MAX] 越界读引发 SIGSEGV (rc=139)。
     * 此处强力归一化防线：任何非法或越界 file_id 均回退至默认主源文件（0 号）。 */
    if (file_id == UINT32_MAX || (g->diag && file_id >= (uint32_t)g->diag->file_count)) {
        file_id = 0;
    }
    if (!zan_tab_reserve((void **)&g->di_files, &g->di_file_cap,
                         sizeof(*g->di_files), (int)file_id, 64))
        return NULL;
    if (g->di_files[file_id]) return g->di_files[file_id];
    if (!g->di_builder) g->di_builder = LLVMCreateDIBuilder(g->mod);

    const char *path = NULL;
    if (g->diag && file_id < (uint32_t)g->diag->file_count && g->diag->file_names)
        path = g->diag->file_names[file_id];
    if (!path || !path[0]) path = g->src_file ? g->src_file : "<unknown>.zan";

    char dir[1024];
    if (path[0] == '/' || (path[0] && path[1] == ':')) {
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
const char *loc_site_file(zan_irgen_t *g, zan_loc_t loc) {
    if (g->hide_diagnostics) {
        for (int i = 0; i < g->site_record_count; i++) {
            if (g->site_records[i].file_id == loc.file_id &&
                g->site_records[i].line == loc.line &&
                g->site_records[i].col == loc.col) {
                char sbuf[32];
                snprintf(sbuf, sizeof(sbuf), "#%u", g->site_records[i].site_id);
                return zan_arena_strdup(g->arena, sbuf, strlen(sbuf));
            }
        }
        if (g->site_record_count >= g->site_record_cap) {
            int new_cap = g->site_record_cap ? g->site_record_cap * 2 : 64;
            g->site_records = realloc(g->site_records, new_cap * sizeof(*g->site_records));
            g->site_record_cap = new_cap;
        }
        uint32_t id = ++g->next_site_id;
        g->site_records[g->site_record_count].file_id = loc.file_id;
        g->site_records[g->site_record_count].line = loc.line;
        g->site_records[g->site_record_count].col = loc.col;
        g->site_records[g->site_record_count].site_id = id;
        g->site_record_count++;

        if (g->site_map_fp) {
            const char *raw_file = NULL;
            if (g->diag && g->diag->file_names &&
                (int)loc.file_id < g->diag->file_count) {
                const char *q = g->diag->file_names[loc.file_id];
                if (q && q[0]) raw_file = q;
            }
            if (!raw_file) raw_file = g->src_file ? g->src_file : "<unknown>";
            fprintf(g->site_map_fp, "#%u\t%s:%u:%u\n", id, raw_file, loc.line, loc.col);
            fflush(g->site_map_fp);
        }

        char sbuf[32];
        snprintf(sbuf, sizeof(sbuf), "#%u", id);
        return zan_arena_strdup(g->arena, sbuf, strlen(sbuf));
    }

    const char *p = NULL;
    if (g->diag && g->diag->file_names &&
        (int)loc.file_id < g->diag->file_count) {
        const char *q = g->diag->file_names[loc.file_id];
        if (q && q[0]) p = q;
    }
    if (!p) p = g->src_file ? g->src_file : "<unknown>";
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
    LLVMTypeRef str_arr_ty = LLVMArrayType(LLVMInt8TypeInContext(g->ctx), (unsigned)n);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, str_arr_ty, ".str");
    LLVMSetInitializer(gv, LLVMConstStringInContext(g->ctx, text, (unsigned)(n - 1), 0));
    LLVMSetGlobalConstant(gv, 1);
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetUnnamedAddress(gv, LLVMGlobalUnnamedAddr);
    LLVMValueRef zero = LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    LLVMValueRef idx[] = { zero, zero };
    e->gv = LLVMConstInBoundsGEP2(str_arr_ty, gv, idx, 2);
    e->next = g->str_intern[b];
    g->str_intern[b] = e;
    return e->gv;
}

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
void di_clear(zan_irgen_t *g) {
    if (!g->emit_debug || !g->builder) return;
    LLVMSetCurrentDebugLocation2(g->builder, NULL);
    g->di_cur_line = 0;
    g->di_cur_file = UINT32_MAX;
    g->di_cur_col = 0;
}
void di_set_loc(zan_irgen_t *g, zan_loc_t loc) {
    if (!g->emit_debug || !g->builder) return;
    if (loc.line == 0) return;
    if (g->diag && (int)loc.file_id >= g->diag->file_count) return;
    if (loc.file_id == g->di_cur_file && loc.line == g->di_cur_line &&
        loc.col == g->di_cur_col && g->di_last_loc_fn == g->current_fn &&
        LLVMGetCurrentDebugLocation2(g->builder) == g->di_last_loc) {
        return;
    }
    LLVMMetadataRef sp = di_ensure_sp(g, loc.file_id, loc.line);
    if (!sp) return;
    g->di_cur_line = loc.line;
    g->di_cur_file = loc.file_id;
    g->di_cur_col = loc.col;
    unsigned line = loc.line ? loc.line : 1;
    LLVMMetadataRef dl = LLVMDIBuilderCreateDebugLocation(
        g->ctx, line, loc.col, sp, NULL);
    LLVMSetCurrentDebugLocation2(g->builder, dl);
    /* 记住这个位置：惰性构建的辅助函数会 di_clear 掉"当前"位置且不
       恢复，之后同一表达式里的调用就没了 !dbg（LLVM 校验器直接拒绝
       整个模块）。di_ensure_loc 靠它把位置补回去。 */
    g->di_last_loc = dl;
    g->di_last_loc_fn = g->current_fn;
}

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


#define ZAN_DI_TAG_STRUCTURE 0x13u /* DW_TAG_structure_type */
#define ZAN_DI_ATE_BOOLEAN   0x02u
#define ZAN_DI_ATE_FLOAT     0x04u
#define ZAN_DI_ATE_SIGNED    0x05u
#define ZAN_DI_ATE_UNSIGNED  0x07u
#define ZAN_DI_ATE_UCHAR     0x08u
#define ZAN_DI_ATE_UTF       0x10u
bool class_has_virtual_methods(zan_symbol_t *sym);
int class_vptr_offset(zan_symbol_t *sym);
bool field_member_is_static(zan_symbol_t *m);
unsigned long abi_size_of(LLVMTypeRef t);
unsigned long abi_align_of(LLVMTypeRef t);

typedef struct {
    zan_type_t *type;
    LLVMMetadataRef placeholder;
    LLVMMetadataRef composite;
    int building;
} zan_di_type_rec_t;
static zan_di_type_rec_t **g_di_types = NULL;
static int g_di_type_count = 0, g_di_type_cap = 0;
void di_debug_types_reset(void) {
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

static LLVMMetadataRef di_byte_ptr(zan_irgen_t *g) {
    LLVMMetadataRef byte = di_basic(g, "byte", 8, ZAN_DI_ATE_UCHAR);
    return LLVMDIBuilderCreatePointerType(g->di_builder, byte, 64, 0, 0,
                                          "", 0);
}

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

static struct zan_struct_type_entry *di_struct_entry(zan_irgen_t *g,
                                                     zan_symbol_t *sym) {
    for (int i = 0; i < g->struct_type_count; i++)
        if (g->struct_types[i].sym == sym) return &g->struct_types[i];
    return NULL;
}
unsigned long di_align_up(unsigned long v, unsigned long a) {
    if (a == 0) return v;
    unsigned long r = v % a;
    return r ? v + (a - r) : v;
}

static LLVMMetadataRef di_member(zan_irgen_t *g, LLVMMetadataRef scope,
                                 LLVMMetadataRef file, const char *nm,
                                 LLVMMetadataRef ty, unsigned long off,
                                 unsigned long size_bytes, unsigned line) {
    return LLVMDIBuilderCreateMemberType(
        g->di_builder, scope, nm, strlen(nm), file, line,
        size_bytes * 8, /* AlignInBits */ 0, off * 8, LLVMDIFlagZero, ty);
}

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

static LLVMMetadataRef di_class_composite(zan_irgen_t *g, zan_type_t *t,
                                          int depth) {
    zan_symbol_t *sym = t->sym;
    if (!sym || !sym->decl) return NULL;
    struct zan_struct_type_entry *e = di_struct_entry(g, sym);
    if (!e) return NULL;

    zan_di_type_rec_t *rec = di_type_rec(t);
    if (!rec) return NULL;
    if (rec->building) return rec->placeholder;
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

    int nslots = e->field_count;
    int vptr = class_vptr_offset(sym);
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
        LLVMMetadataRef mty = di_byte_ptr(g);
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
    case TYPE_DECIMAL:
        return di_basic(g, "decimal", 128, ZAN_DI_ATE_FLOAT);
    case TYPE_CHAR:
        return di_basic(g, "char", 16, ZAN_DI_ATE_UTF);
    case TYPE_STRING: {
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
    if (!ty) return;
    LLVMMetadataRef file = di_file_for(g, g->di_cur_file);
    unsigned line = g->di_cur_line ? g->di_cur_line : 1;
    LLVMMetadataRef var = LLVMDIBuilderCreateAutoVariable(
        g->di_builder, sp, name.str, name.len, file, line, ty,
        /* AlwaysPreserve */ 1, LLVMDIFlagZero, /*AlignInBits*/ 0);
    LLVMMetadataRef expr = LLVMDIBuilderCreateExpression(g->di_builder, NULL, 0);
    LLVMMetadataRef dl = LLVMDIBuilderCreateDebugLocation(g->ctx, line, 0, sp, NULL);
#if defined(LLVM_VERSION_MAJOR) && LLVM_VERSION_MAJOR < 19
    LLVMDIBuilderInsertDeclareAtEnd(g->di_builder, storage, var, expr, dl, bb);
#else
    LLVMDIBuilderInsertDeclareRecordAtEnd(g->di_builder, storage, var, expr, dl, bb);
#endif
}

zan_irgen_t *g_di_emit_ctx = NULL;

#include "../common/host_oom.h"
#include "../common/zan_abi.h"
bool types_equal(zan_type_t *a, zan_type_t *b);
LLVMValueRef zan_call2(LLVMBuilderRef b, LLVMTypeRef ty, LLVMValueRef fn,
                              LLVMValueRef *args, unsigned n, const char *nm);
void emit_weak_runtime(zan_irgen_t *g);
void emit_defer_fallback(zan_irgen_t *g);

static zan_irgen_t *s_current_irgen = NULL;

/* 补一个调试位置。
   惰性构建的辅助函数（类的释放体、集合/数组的释放体……）会在用户
   代码中间被建出来，入口 di_clear 把"当前"调试位置清掉后不恢复——
   同一表达式里后面的调用就丢了 !dbg。LLVM 校验器对这种"带调试信息
   的函数里位置缺失的调用"直接报
     inlinable function call in a function with debug info must have a !dbg location
   整个 -g 构建因此失败（Zan.Scripting 的 LuaValue.op_call 就是这么
   把 IDE 的调试构建顶掉的）。
   这里用最近一次设置过的位置补回去；只补在真正带调试信息的函数里
   （运行时辅助函数没有 subprogram，塞位置反而会触发另一条校验），
   而且只在该位置本就属于这个函数时才补。 */
static void di_ensure_loc(LLVMBuilderRef b) {
    zan_irgen_t *g = s_current_irgen;
    if (!g || !g->emit_debug || !g->di_last_loc) return;
    if (LLVMGetCurrentDebugLocation2(b)) return;
    LLVMBasicBlockRef bb = LLVMGetInsertBlock(b);
    if (!bb) return;
    LLVMValueRef parent = LLVMGetBasicBlockParent(bb);
    if (!parent) return;
    if (g->di_last_loc_fn && g->di_last_loc_fn != parent) return;
    if (!LLVMGetSubprogram(parent)) return;
    LLVMSetCurrentDebugLocation2(b, g->di_last_loc);
}
