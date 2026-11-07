LLVMValueRef zan_hdr_is_string(zan_irgen_t *g, LLVMValueRef word,
                                     const char *nm) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef tag = LLVMBuildLShr(g->builder, word,
                                     LLVMConstInt(i64, 32, 0), "hdr.tag");
    return zan_icmp(g->builder, LLVMIntEQ, tag,
                    LLVMConstInt(i64, ZAN_STRING_TAG, 0), nm);
}
LLVMValueRef zan_hdr_read_ok(zan_irgen_t *g, LLVMValueRef obj) {
    /* 移除已过时且危险的 Win32 IsBadReadPtr API 调用。
     * IsBadReadPtr 会破坏 Windows 线程栈增长保护页机制并引入多线程竞态与高昂系统调用开销。
     * 保持与 Linux/macOS/POSIX 平台统一的行为：返回 NULL，仅依靠非空判定与魔数校验。 */
    (void)g;
    (void)obj;
    return NULL;
}
LLVMTypeRef arc_desc_type(zan_irgen_t *g) {
    /* 4 字段：{dtor_fn, tynames_arr_ptr, meta_ptr, field_graph_ptr}，共 32 字节。
     * 前 3 字段偏移不变 (0, 8, 16)，不影响 runtime 硬编码 GEP(8)/GEP(16)。
     * 第 4 字段 (offset 24) 承载 B-ID89 字段走图元数据指针。 */
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    return LLVMStructType((LLVMTypeRef[]){ i8p, i8p, i8p, i8p }, 4, 0);
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
    LLVMValueRef z = LLVMConstNull(i8p);
    LLVMSetInitializer(dg, LLVMConstNamedStruct(dt,
        (LLVMValueRef[]){ z, z, z, z }, 4));
    g->desc_gv[site_idx] = dg;
    return dg;
}

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
int reserve_arc_site(zan_irgen_t *g, zan_symbol_t *sym,
                            zan_type_t *inst, int coll_kind,
                            zan_type_t *coll_elem) {
    uint32_t key_file = g->check_leaks ? g->di_cur_file : 0;
    uint32_t key_line = g->check_leaks ? g->di_cur_line : 0;
    /* 从 1 起扫：0 号是哨兵槽，永不参与去重复用。 */
    for (int i = 1; i < g->leak_site_count; i++) {
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
int reserve_closure_site(zan_irgen_t *g) {
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
        create_arc_desc(g, site_idx);
    }
    return site_idx;
}
LLVMValueRef arc_site_arg(zan_irgen_t *g, int site_idx) {
    if (!g->desc_hdr)
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx),
                            (unsigned long long)site_idx, 0);
    return LLVMBuildPtrToInt(g->builder, g->desc_gv[site_idx],
                             LLVMInt64TypeInContext(g->ctx), "desc.arg");
}


/* ---- initialization ---- */
LLVMValueRef zan_call2(LLVMBuilderRef b, LLVMTypeRef ty, LLVMValueRef fn,
                              LLVMValueRef *args, unsigned n, const char *nm) {
    di_ensure_loc(b);
    LLVMValueRef callee = fn;
    if (fn && LLVMIsAFunction(fn) && LLVMGlobalGetValueType(fn) != ty) {
        fn = LLVMConstBitCast(fn, LLVMPointerType(ty, 0));
    } else if (fn && !LLVMIsAFunction(fn) &&
               LLVMGetTypeKind(LLVMTypeOf(fn)) == LLVMPointerTypeKind &&
               LLVMTypeOf(fn) != LLVMPointerType(ty, 0)) {
        fn = LLVMBuildBitCast(b, fn, LLVMPointerType(ty, 0), "callee.fp");
    }
    LLVMValueRef call = LLVMBuildCall2(b, ty, fn, args, n, nm);
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
void zan_emit_frame_free(zan_irgen_t *g, LLVMValueRef frame_i8) {
    /* A380 统一释放口：两种驱动一律走运行时 __zan_co_frame_free（帧首 CO_DEAD
     * 幂等守卫 + 检疫区）。2026-10-24 曾把默认路径还原回裸 free——那 3 条 AV
     * 后经 A549 证明是存量账，且自举后端九个发射点本来就无条件走守卫释放、
     * 自举门长期全绿，"默认驱动 + 守卫释放"组合早有在证；此处与自举协议归一。
     * 无守卫运行时的平台（rt_io.c #else 支）该符号即裸 free，退化为旧行为。 */
    if (g->rt_co_frame_free) {
        zan_call2(g->builder, g->rt_co_frame_free_type, g->rt_co_frame_free,
                  &frame_i8, 1, "");
        return;
    }
    zan_call2(g->builder, LLVMGlobalGetValueType(g->fn_free), g->fn_free,
              &frame_i8, 1, "");
}
void emit_rt_alloc_add(zan_irgen_t *g, LLVMValueRef bytes) {
    if (!bytes || !g || !g->builder) return;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i64 }, 1, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_rt_alloc_add");
    if (!fn) fn = LLVMAddFunction(g->mod, "zan_rt_alloc_add", fn_ty);
    LLVMValueRef b_val = bytes;
    if (LLVMTypeOf(b_val) != i64) {
        if (LLVMGetTypeKind(LLVMTypeOf(b_val)) == LLVMPointerTypeKind) {
            b_val = LLVMBuildPtrToInt(g->builder, b_val, i64, "alloc.bytes");
        } else {
            b_val = LLVMBuildZExtOrBitCast(g->builder, b_val, i64, "alloc.bytes");
        }
    }
    zan_call2(g->builder, fn_ty, fn, &b_val, 1, "");
}
LLVMValueRef get_eh_personality_fn(zan_irgen_t *g) {
    if (g->eh_personality_fn) return g->eh_personality_fn;
    const char *pers_name;
    if (g->target_is_wasm) {
        pers_name = "__gxx_wasm_personality_v0";
    } else if (g->target_is_windows) {
        pers_name = "__gxx_personality_seh0";
    } else {
        pers_name = "__gxx_personality_v0";
    }
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, pers_name);
    if (!fn) {
        LLVMTypeRef pers_ty = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), NULL, 0, 1);
        fn = LLVMAddFunction(g->mod, pers_name, pers_ty);
    }
    g->eh_personality_fn = fn;
    return fn;
}

void ensure_function_personality(zan_irgen_t *g, LLVMValueRef fn) {
    if (!fn || !LLVMIsAFunction(fn)) return;
    if (!LLVMHasPersonalityFn(fn)) {
        LLVMSetPersonalityFn(fn, get_eh_personality_fn(g));
    }
}

void emit_raise_exception(zan_irgen_t *g) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    if (!g->fn_cxa_allocate_exception) {
        LLVMTypeRef alloc_ty = LLVMFunctionType(i8ptr, &i64t, 1, 0);
        g->fn_cxa_allocate_exception = LLVMGetNamedFunction(g->mod, "__cxa_allocate_exception");
        if (!g->fn_cxa_allocate_exception) {
            g->fn_cxa_allocate_exception = LLVMAddFunction(g->mod, "__cxa_allocate_exception", alloc_ty);
        }
    }
    if (!g->fn_cxa_throw) {
        LLVMTypeRef throw_params[] = { i8ptr, i8ptr, i8ptr };
        LLVMTypeRef throw_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), throw_params, 3, 0);
        g->fn_cxa_throw = LLVMGetNamedFunction(g->mod, "__cxa_throw");
        if (!g->fn_cxa_throw) {
            g->fn_cxa_throw = LLVMAddFunction(g->mod, "__cxa_throw", throw_ty);
        }
    }

    LLVMTypeRef alloc_ty = LLVMFunctionType(i8ptr, &i64t, 1, 0);
    LLVMValueRef exc_mem = zan_call2(g->builder, alloc_ty, g->fn_cxa_allocate_exception,
        (LLVMValueRef[]){ LLVMConstInt(i64t, 8, 0) }, 1, "exc.mem");

    LLVMTypeRef throw_params[] = { i8ptr, i8ptr, i8ptr };
    LLVMTypeRef throw_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), throw_params, 3, 0);

    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
    LLVMValueRef cur_fn = cur_bb ? LLVMGetBasicBlockParent(cur_bb) : NULL;
    if (cur_fn) ensure_function_personality(g, cur_fn);

    if (g->lpad_depth > 0) {
        for (int i = g->lpad_depth - 1; i >= 0; i--) {
            zan_lpad_entry_t *top = &g->lpad_stack[i];
            if (top->fn != cur_fn) break;
            if (top->lpad_bb && top->catch_bb) {
                LLVMBasicBlockRef unreach_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "throw.unreach");
                LLVMBuildInvoke2(g->builder, throw_ty, g->fn_cxa_throw,
                    (LLVMValueRef[]){ exc_mem, LLVMConstNull(i8ptr), LLVMConstNull(i8ptr) }, 3,
                    unreach_bb, top->lpad_bb, "");
                LLVMPositionBuilderAtEnd(g->builder, unreach_bb);
                LLVMBuildUnreachable(g->builder);
                return;
            }
        }
    }

    zan_call2(g->builder, throw_ty, g->fn_cxa_throw,
        (LLVMValueRef[]){ exc_mem, LLVMConstNull(i8ptr), LLVMConstNull(i8ptr) }, 3, "");
    LLVMBuildUnreachable(g->builder);
}

LLVMValueRef zan_call_or_invoke(zan_irgen_t *g, LLVMBuilderRef b, LLVMTypeRef ty,
                                LLVMValueRef fn, LLVMValueRef *args, unsigned n,
                                const char *nm) {
    if (g && g->lpad_depth > 0) {
        zan_lpad_entry_t *top = &g->lpad_stack[g->lpad_depth - 1];
        LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(b);
        LLVMValueRef cur_fn = cur_bb ? LLVMGetBasicBlockParent(cur_bb) : NULL;
        if (cur_fn && top->fn == cur_fn && top->lpad_bb) {
            di_ensure_loc(b);
            ensure_function_personality(g, cur_fn);
            LLVMBasicBlockRef norm_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "inv.norm");
            LLVMBasicBlockRef lpad_bb = top->lpad_bb;

            LLVMValueRef callee = fn;
            if (fn && LLVMIsAFunction(fn) && LLVMGlobalGetValueType(fn) != ty) {
                fn = LLVMConstBitCast(fn, LLVMPointerType(ty, 0));
            } else if (fn && !LLVMIsAFunction(fn) &&
                       LLVMGetTypeKind(LLVMTypeOf(fn)) == LLVMPointerTypeKind &&
                       LLVMTypeOf(fn) != LLVMPointerType(ty, 0)) {
                fn = LLVMBuildBitCast(b, fn, LLVMPointerType(ty, 0), "callee.fp");
            }
            LLVMValueRef inv = LLVMBuildInvoke2(b, ty, fn, args, n, norm_bb, lpad_bb, nm);
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
            LLVMPositionBuilderAtEnd(b, norm_bb);
            return inv;
        }
    }
    return zan_call2(b, ty, fn, args, n, nm);
}

void *irgen_grow(void *base, int *cap, int need, size_t elem) {
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

static size_t irgen_ptr_bucket(const void *p, int cap) {
    uint64_t h = (uint64_t)(uintptr_t)p * 0x9E3779B97F4A7C15ull;
    return (size_t)((h >> 32) & (uint64_t)(cap - 1));
}


static size_t fn_index_hash(zan_symbol_t *sym, int cap) {
    return irgen_ptr_bucket(sym, cap);
}

static void fn_index_put(zan_irgen_t *g, zan_symbol_t *sym, int idx) {
    size_t i = fn_index_hash(sym, g->fn_index_cap);
    while (g->fn_index[i].sym) {
        if (g->fn_index[i].sym == sym) return;
        i = (i + 1) & (size_t)(g->fn_index_cap - 1);
    }
    g->fn_index[i].sym = sym;
    g->fn_index[i].idx = idx;
}

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
int irgen_find_function(zan_irgen_t *g, zan_symbol_t *sym) {
    if (!sym || !g->fn_index) return -1;
    size_t i = fn_index_hash(sym, g->fn_index_cap);
    while (g->fn_index[i].sym) {
        if (g->fn_index[i].sym == sym) return g->fn_index[i].idx;
        i = (i + 1) & (size_t)(g->fn_index_cap - 1);
    }
    return -1;
}
void irgen_register_function(zan_irgen_t *g, zan_symbol_t *sym,
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
    if ((g->function_count + 1) * 2 >= g->fn_index_cap)
        fn_index_rehash(g, g->fn_index_cap ? g->fn_index_cap * 2 : 2048);
    if (sym) fn_index_put(g, sym, g->function_count);
    g->function_count++;
}


typedef struct {
    zan_symbol_t *owner;
    int built_count;
    int cap;
    int *name_buckets;
    int *name_next;
    int *decl_buckets;
    int *decl_next;
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

static int member_first_named(zan_class_index_t *ci, zan_istr_t name) {
    return ci->name_buckets[istr_hash(name) & (uint64_t)(ci->cap - 1)];
}

static int member_next_named(zan_class_index_t *ci, int i) {
    return ci->name_next[i];
}
bool member_name_is(zan_symbol_t *m, zan_istr_t name) {
    return m->name.len == name.len &&
           memcmp(m->name.str, name.str, (size_t)name.len) == 0;
}

/* Defined in irgen_generics */
void emit_fatal_report(zan_irgen_t *g, LLVMValueRef text, int exit_code);

void zan_irgen_emit_oom_check(zan_irgen_t *g, LLVMValueRef fn, LLVMValueRef raw) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, raw,
        LLVMConstPointerNull(i8ptr), "oom");
    LLVMBasicBlockRef oom_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "oom");
    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "oom.ok");
    LLVMBuildCondBr(g->builder, isnull, oom_bb, ok_bb);
    LLVMPositionBuilderAtEnd(g->builder, oom_bb);
    LLVMValueRef text = zan_irgen_intern_string(g, "out of memory\n");
    emit_fatal_report(g, text, 1);
    LLVMPositionBuilderAtEnd(g->builder, ok_bb);
}
void emit_leak_counter_add(zan_irgen_t *g, LLVMValueRef ptr, long long delta) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMBuildAtomicRMW(g->builder,
        delta < 0 ? LLVMAtomicRMWBinOpSub : LLVMAtomicRMWBinOpAdd, ptr,
        LLVMConstInt(i64, (unsigned long long)(delta < 0 ? -delta : delta), 0),
        LLVMAtomicOrderingMonotonic, 0);
}

#define ZAN_ARC_FAULT_RETAIN      0xE0A2C001u
#define ZAN_ARC_FAULT_RELEASE     0xE0A2C002u
#define ZAN_ARC_FAULT_STR_RETAIN  0xE0A2C003u
#define ZAN_ARC_FAULT_STR_RELEASE 0xE0A2C004u
#define ZAN_ARC_FAULT_USE_OBJ     0xE0A2C005u
#define ZAN_ARC_FAULT_USE_STR     0xE0A2C006u
#define ZAN_ARC_FAULT_ARR_RETAIN  0xE0A2C007u
#define ZAN_ARC_FAULT_ARR_RELEASE 0xE0A2C008u
#define ZAN_ARC_FAULT_USE_ARR     0xE0A2C009u

#define ZAN_ARC_FREED_MARK 0xDEAD0000DEAD0000ull

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
LLVMValueRef get_arc_free_decl(zan_irgen_t *g);

static void emit_arc_underflow_check(zan_irgen_t *g, LLVMValueRef fn,
                                    LLVMValueRef rc_old, LLVMValueRef obj,
                                    LLVMValueRef site, unsigned code,
                                    const char *what) {
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
    LLVMValueRef sp = LLVMBuildGEP2(g->builder, i8t, obj,
        (LLVMValueRef[]){ LLVMConstInt(i64t, (uint64_t)ZAN_OBJ_SITE_OFF, 1) }, 1, "uafsp");
    LLVMValueRef sip = LLVMBuildBitCast(g->builder, sp, LLVMPointerType(i64t, 0), "uafsip");
    LLVMValueRef freed_by = LLVMBuildLoad2(g->builder, i64t, sip, "freedby");
    emit_arc_fault_report(g, obj, LLVMConstInt(i64t, 0, 0), freed_by, code, what);
    LLVMPositionBuilderAtEnd(g->builder, ok_bb);
}
void emit_arc_quarantine(zan_irgen_t *g, LLVMValueRef obj,
                               LLVMValueRef rc_iptr) {
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8p = LLVMPointerType(i8t, 0);
    LLVMBuildStore(g->builder, LLVMConstInt(i64t, ZAN_ARC_FREED_MARK, 0), rc_iptr);
    LLVMBuildStore(g->builder, LLVMConstInt(i8t, 0xDD, 0), obj);
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
void emit_header_read_guard(zan_irgen_t *g, LLVMValueRef fn,
                                   LLVMValueRef ptr8, LLVMBasicBlockRef ret_bb) {
    /* 移除已过时且危险的 Win32 IsBadReadPtr API 调用。
     * IsBadReadPtr 会破坏 Windows 线程栈增长保护页机制并引入多线程竞态与高昂系统调用开销。
     * 保持与 Linux/macOS/POSIX 平台统一的行为：直接放行至魔数校验。 */
    (void)g;
    (void)fn;
    (void)ptr8;
    (void)ret_bb;
}

static void emit_arc_trace_call(zan_irgen_t *g, LLVMValueRef ev_fn,
                                const char *tag, LLVMValueRef obj,
                                LLVMValueRef rc, LLVMValueRef site) {
    if (!ev_fn) return;
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef ra_ty = LLVMFunctionType(i8p, (LLVMTypeRef[]){ i32t }, 1, 0);
    /* 声明而不是只查：只查的话，查不到就整段 return——ARC trace 事件被
     * 静默丢弃，而编译一个字都不说。当前靠「调用点都在 check_leaks 分支
     * 里、那里已声明过该 intrinsic」才不出事，但那是跨函数的隐式耦合：
     * 将来把调用点挪到别处，trace 就会无声地少一串记录。 */
    LLVMValueRef ra_fn = LLVMGetNamedFunction(g->mod, "llvm.returnaddress");
    if (!ra_fn) ra_fn = LLVMAddFunction(g->mod, "llvm.returnaddress", ra_ty);
    LLVMValueRef ra_arg = LLVMConstInt(i32t, 0, 0);
    LLVMValueRef ra = zan_call2(g->builder, ra_ty, ra_fn, &ra_arg, 1, "tra");
    LLVMValueRef tagc = zan_irgen_intern_string(g, tag);
    LLVMValueRef args[5] = { tagc, obj, rc, site, ra };
    zan_call2(g->builder, LLVMGlobalGetValueType(ev_fn), ev_fn, args, 5, "");
}

