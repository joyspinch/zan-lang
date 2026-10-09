/* 底层系统交互与数据协议契约 */

enum {
    ZAN_MF_USER = 0,       /* 核心系统底层抽象与内存语义契约 */
    ZAN_MF_ASYNC_RESUME,   /* 核心系统底层抽象与内存语义契约 */
    ZAN_MF_RELEASE,        /* 底层系统交互与数据协议契约 */
    ZAN_MF_VTABLE,         /* 核心系统底层抽象与内存语义契约 */
    ZAN_MF_REFLECT,        /* __zan.refl.* / __zan_refl* thunks */
    ZAN_MF_ADAPTER,        /* 核心系统底层抽象与内存语义契约 */
    ZAN_MF_OTHER           /* 核心系统底层抽象与内存语义契约 */
};

/* 底层系统交互与数据协议契约 */
enum {
    ZAN_MF_R_ASYNC, ZAN_MF_R_SPEC, ZAN_MF_R_VIRTUAL, ZAN_MF_R_ADDR,
    ZAN_MF_R_INDIRECT, ZAN_MF_R_ABI, ZAN_MF_R_SYNTH, ZAN_MF_R_POLICY,
    ZAN_MF_R_REASON_COUNT
};

static const char *mf_kind_name(unsigned char k) {
    switch (k) {
    case ZAN_MF_USER:         return "user";
    case ZAN_MF_ASYNC_RESUME: return "async-resume";
    case ZAN_MF_RELEASE:      return "release";
    case ZAN_MF_VTABLE:       return "vtable";
    case ZAN_MF_REFLECT:      return "reflect";
    case ZAN_MF_ADAPTER:      return "adapter";
    default:                  return "other";
    }
}

static bool mf_name_starts(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static unsigned char mf_classify(const char *name, int reg_idx) {
    if (mf_name_starts(name, "zan.abi.")) return ZAN_MF_ADAPTER;
    if (reg_idx >= 0) return ZAN_MF_USER;
    size_t len = strlen(name);
    if (len > 7 && !strcmp(name + len - 7, "$resume"))
        return ZAN_MF_ASYNC_RESUME;
    if (mf_name_starts(name, "__zan_release_") ||
        mf_name_starts(name, "__zan_arr_release_") ||
        mf_name_starts(name, "__zan_release_coll_"))
        return ZAN_MF_RELEASE;
    if (mf_name_starts(name, "__zan_vtable_")) return ZAN_MF_VTABLE;
    if (mf_name_starts(name, "__zan.refl.") || mf_name_starts(name, "__zan_refl"))
        return ZAN_MF_REFLECT;
    if (mf_name_starts(name, "__zan_w32ir_")) return ZAN_MF_ADAPTER;
    return ZAN_MF_OTHER;
}

/* 内部辅助逻辑 */
static bool mf_is_virtual_dispatch(uint32_t modifiers) {
    return (modifiers & (MOD_VIRTUAL | MOD_OVERRIDE | MOD_ABSTRACT)) != 0;
}

/* 内部辅助逻辑 */
static bool mf_simple_abi(LLVMTypeRef ft) {
    LLVMTypeKind rk = LLVMGetTypeKind(LLVMGetReturnType(ft));
    if (rk != LLVMVoidTypeKind && rk != LLVMPointerTypeKind &&
        rk != LLVMIntegerTypeKind && rk != LLVMFloatTypeKind &&
        rk != LLVMDoubleTypeKind)
        return false;
    unsigned pcount = LLVMCountParamTypes(ft);
    if (pcount > 0) {
        LLVMTypeRef *pts = (LLVMTypeRef *)malloc(pcount * sizeof(LLVMTypeRef));
        if (!pts) return false;
        LLVMGetParamTypes(ft, pts);
        for (unsigned i = 0; i < pcount; i++) {
            LLVMTypeKind k = LLVMGetTypeKind(pts[i]);
            if (k == LLVMStructTypeKind || k == LLVMArrayTypeKind ||
                k == LLVMVectorTypeKind) {
                free(pts);
                return false;
            }
        }
        free(pts);
    }
    return true;
}

/* 内部辅助逻辑 */
static bool mf_is_spec(zan_irgen_t *g, LLVMValueRef fn) {
    for (int i = 0; i < g->generic_fn_count; i++)
        if (g->generic_fns[i].fn == fn) return true;
    return false;
}

/* 内部辅助逻辑 */
static bool mf_is_async_ramp(zan_irgen_t *g, const char *name) {
    char buf[512];
    if (strlen(name) + 8 >= sizeof(buf)) return false;
    snprintf(buf, sizeof(buf), "%s$resume", name);
    return LLVMGetNamedFunction(g->mod, buf) != NULL;
}

static void mf_push(int **arr, int *cnt, int *cap, int v) {
    if (*cnt < *cap) { (*arr)[(*cnt)++] = v; return; }
    int ncap = *cap ? *cap * 2 : 8;
    int *n = (int *)realloc(*arr, (size_t)ncap * sizeof(int));
    if (!n) { *cnt = 0; return; } /* 底层系统交互与数据协议契约 */
    *arr = n; *cap = ncap; (*arr)[(*cnt)++] = v;
}

static void mf_push_name(const char ***arr, int *cnt, int *cap, const char *v) {
    for (int i = 0; i < *cnt; i++)
        if (!strcmp((*arr)[i], v)) return; /* 核心系统底层抽象与内存语义契约 */
    if (*cnt < *cap) { (*arr)[(*cnt)++] = v; return; }
    int ncap = *cap ? *cap * 2 : 8;
    const char **n = (const char **)realloc(*arr,
                                            (size_t)ncap * sizeof(char *));
    if (!n) { *cnt = 0; return; }
    *arr = n; *cap = ncap; (*arr)[(*cnt)++] = v;
}

/* 底层系统交互与数据协议契约 */
typedef struct { const char *name; int idx; } mf_name_map_t;

static int mf_name_map_cmp(const void *a, const void *b) {
    return strcmp(((const mf_name_map_t *)a)->name,
                  ((const mf_name_map_t *)b)->name);
}

static int mf_defined_lookup(const mf_name_map_t *map, int n, const char *name) {
    mf_name_map_t key = { name, 0 };
    mf_name_map_t *hit = (mf_name_map_t *)bsearch(&key, map, (size_t)n,
                                                  sizeof(map[0]),
                                                  mf_name_map_cmp);
    return hit ? hit->idx : -1;
}

/* 内部辅助逻辑 */
static void mf_scan_insn_operand(zan_mf_fn *F, zan_mf_fn *fns,
                                 const mf_name_map_t *map, int map_n,
                                 LLVMValueRef op) {
    if (!op) return;
    if (LLVMIsAFunction(op)) {
        const char *nm = LLVMGetValueName(op);
        int idx = mf_defined_lookup(map, map_n, nm);
        if (idx >= 0) fns[idx].addr_taken = 1;
        mf_push_name(&F->exts, &F->ext_cnt, &F->ext_cap, nm);
        return;
    }
    if (LLVMIsAGlobalValue(op))
        mf_push_name(&F->globs, &F->glob_cnt, &F->glob_cap,
                     LLVMGetValueName(op));
}

/* 内部辅助逻辑 */
static void mf_scan_const(zan_mf_fn *F, const mf_name_map_t *map, int map_n,
                          LLVMValueRef v, int depth) {
    if (!v || depth > 8) return;
    if (LLVMIsAFunction(v)) {
        mf_push_name(&F->exts, &F->ext_cnt, &F->ext_cap, LLVMGetValueName(v));
        return;
    }
    if (LLVMIsAConstantExpr(v)) {
        unsigned n = LLVMGetNumOperands(v);
        for (unsigned i = 0; i < n; i++)
            mf_scan_const(F, map, map_n, LLVMGetOperand(v, (int)i), depth + 1);
        return;
    }
}

static void mf_build(zan_irgen_t *g, zan_cg_manifest_t *m, bool native) {
    memset(m, 0, sizeof(*m));

    int cap = 256;
    zan_mf_fn *fns = (zan_mf_fn *)calloc((size_t)cap, sizeof(zan_mf_fn));
    LLVMValueRef *llfns = (LLVMValueRef *)malloc((size_t)cap *
                                                 sizeof(LLVMValueRef));
    if (!fns || !llfns) { free(fns); free(llfns); return; }

    /* 底层系统交互与数据协议契约 */
    for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
         fn = LLVMGetNextFunction(fn)) {
        if (m->fn_count >= cap) {
            int ncap = cap * 2;
            zan_mf_fn *nf = (zan_mf_fn *)realloc(fns,
                                                 (size_t)ncap * sizeof(zan_mf_fn));
            LLVMValueRef *nl = (LLVMValueRef *)realloc(
                llfns, (size_t)ncap * sizeof(LLVMValueRef));
            if (!nf || !nl) break;
            fns = nf; llfns = nl;
            memset(fns + m->fn_count, 0,
                   (size_t)(ncap - m->fn_count) * sizeof(zan_mf_fn));
            cap = ncap;
        }
        zan_mf_fn *F = &fns[m->fn_count];
        F->name = LLVMGetValueName(fn);
        F->defined = !LLVMIsDeclaration(fn);
        F->internal_linkage =
            LLVMGetLinkage(fn) == LLVMInternalLinkage ||
            LLVMGetLinkage(fn) == LLVMPrivateLinkage;
        LLVMTypeRef fnty = LLVMGlobalGetValueType(fn);
        F->varargs = LLVMGetTypeKind(fnty) == LLVMFunctionTypeKind &&
                     LLVMIsFunctionVarArg(fnty);
        llfns[m->fn_count] = fn;
        m->fn_count++;
        if (F->defined) {
            m->defined_count++;
            for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn); bb;
                 bb = LLVMGetNextBasicBlock(bb)) {
                F->blocks++;
                for (LLVMValueRef in = LLVMGetFirstInstruction(bb); in;
                     in = LLVMGetNextInstruction(in))
                    F->insns++;
            }
            m->total_insns += F->insns;
        } else {
            m->extern_count++;
        }
    }
    m->fns = fns;
    for (LLVMValueRef gv = LLVMGetFirstGlobal(g->mod); gv;
         gv = LLVMGetNextGlobal(gv))
        m->global_count++;

    /* 底层系统交互与数据协议契约 */
    for (int i = 0; i < m->fn_count; i++) {
        zan_mf_fn *F = &fns[i];
        F->reg_idx = -1;
        for (int r = 0; r < g->function_count; r++)
            if (g->functions[r].fn == llfns[i]) { F->reg_idx = r; break; }
        F->kind = mf_classify(F->name, F->reg_idx);
        if (F->kind != ZAN_MF_USER || !F->defined) continue;
        F->is_async = mf_is_async_ramp(g, F->name);
        F->is_spec = mf_is_spec(g, llfns[i]);
        F->virtual_dispatch = mf_is_virtual_dispatch(g->functions[F->reg_idx].modifiers);
        F->simple_abi = mf_simple_abi(g->functions[F->reg_idx].fn_type);
    }

    /* 底层系统交互与数据协议契约 */
    mf_name_map_t *map = (mf_name_map_t *)malloc(
        (size_t)(m->defined_count ? m->defined_count : 1) * sizeof(*map));
    int mi = 0;
    for (int i = 0; i < m->fn_count; i++)
        if (fns[i].defined) {
            map[mi].name = fns[i].name;
            map[mi].idx = i;
            mi++;
        }
    qsort(map, (size_t)mi, sizeof(*map), mf_name_map_cmp);

    /* 底层系统交互与数据协议契约 */
    for (int i = 0; i < m->fn_count; i++) {
        zan_mf_fn *F = &fns[i];
        if (!F->defined) continue;
        for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(llfns[i]); bb;
             bb = LLVMGetNextBasicBlock(bb)) {
            for (LLVMValueRef in = LLVMGetFirstInstruction(bb); in;
                 in = LLVMGetNextInstruction(in)) {
                if (LLVMGetInstructionOpcode(in) == LLVMCall) {
                    /* 内部辅助逻辑 */
                    unsigned nop = LLVMGetNumOperands(in);
                    LLVMValueRef callee =
                        nop ? LLVMGetOperand(in, (int)(nop - 1)) : NULL;
                    if (callee && LLVMIsAFunction(callee)) {
                        const char *nm = LLVMGetValueName(callee);
                        int idx = mf_defined_lookup(map, mi, nm);
                        if (idx >= 0 && idx != i)
                            mf_push(&F->calls, &F->call_cnt, &F->call_cap, idx);
                        else if (idx < 0)
                            mf_push_name(&F->exts, &F->ext_cnt, &F->ext_cap,
                                         nm);
                        for (unsigned k = 0; k + 1 < nop; k++)
                            mf_scan_insn_operand(F, fns, map, mi,
                                                 LLVMGetOperand(in, (int)k));
                        continue;
                    }
                    F->indirect_call = 1;
                    for (unsigned k = 0; k < nop; k++)
                        mf_scan_insn_operand(F, fns, map, mi,
                                             LLVMGetOperand(in, (int)k));
                    continue;
                }
                if (LLVMGetInstructionOpcode(in) == LLVMLandingPad)
                    continue; /* 核心系统底层抽象与内存语义契约 */
                unsigned nop = LLVMGetNumOperands(in);
                for (unsigned k = 0; k < nop; k++)
                    mf_scan_insn_operand(F, fns, map, mi,
                                         LLVMGetOperand(in, (int)k));
            }
        }
    }

    /* 内部辅助逻辑 */
    for (LLVMValueRef gv = LLVMGetFirstGlobal(g->mod); gv;
         gv = LLVMGetNextGlobal(gv)) {
        LLVMValueRef init = LLVMGetInitializer(gv);
        if (!init) continue;
        zan_mf_fn scratch;
        memset(&scratch, 0, sizeof(scratch));
        mf_scan_const(&scratch, map, mi, init, 0);
        for (int k = 0; k < scratch.ext_cnt; k++) {
            int idx = mf_defined_lookup(map, mi, scratch.exts[k]);
            if (idx >= 0) fns[idx].addr_taken = 1;
        }
        free(scratch.exts);
    }
    free(llfns);

    /* 内部辅助逻辑 */
    bool policy_ok = native && !g->emit_debug && !g->check_leaks &&
                     !g->arc_guard && !g->emit_lib;
    for (int i = 0; i < m->fn_count; i++) {
        zan_mf_fn *F = &fns[i];
        if (!F->defined) continue;
        F->eligible = policy_ok &&
                      (F->kind == ZAN_MF_USER || F->kind == ZAN_MF_RELEASE) &&
                      !F->is_async;
    }
    /* 内部辅助逻辑 */
    unsigned char *seen = (unsigned char *)calloc((size_t)m->fn_count, 1);
    int *stack = (int *)malloc((size_t)(m->fn_count ? m->fn_count : 1) *
                               sizeof(int));
    if (seen && stack) {
        for (int i = 0; i < m->fn_count; i++) {
            if (!fns[i].eligible) continue;
            memset(seen, 0, (size_t)m->fn_count);
            int top = 0;
            stack[top++] = i;
            seen[i] = 1;
            bool clean = true;
            while (top > 0 && clean) {
                int cur = stack[--top];
                for (int k = 0; k < fns[cur].call_cnt && clean; k++) {
                    int nxt = fns[cur].calls[k];
                    if (seen[nxt]) continue;
                    seen[nxt] = 1;
                    if (fns[nxt].eligible || fns[nxt].kind == ZAN_MF_RELEASE) {
                        if (fns[nxt].eligible) stack[top++] = nxt;
                    } else {
                        clean = false;
                    }
                }
            }
            fns[i].clean_root = clean;
        }
    }
    free(seen);
    free(stack);
    free(map);
}

static void mf_free(zan_cg_manifest_t *m) {
    for (int i = 0; i < m->fn_count; i++) {
        free(m->fns[i].calls);
        free(m->fns[i].exts);
        free(m->fns[i].globs);
    }
    free(m->fns);
    memset(m, 0, sizeof(*m));
}

static void mf_report(zan_irgen_t *g, const zan_cg_manifest_t *m) {
    int kind_count[ZAN_MF_OTHER + 1] = {0};
    unsigned long long elig_insns = 0, clean_insns = 0;
    int elig = 0, clean = 0, spec = 0, async = 0;
    int reasons[ZAN_MF_R_REASON_COUNT] = {0};
    for (int i = 0; i < m->fn_count; i++) {
        const zan_mf_fn *F = &m->fns[i];
        if (!F->defined) continue; /* 核心系统底层抽象与内存语义契约 */
        kind_count[F->kind]++;
        if (F->kind == ZAN_MF_USER) {
            if (F->is_async) async++;
            if (F->is_spec) spec++;
        }
        if (!F->eligible) {
            if (F->kind != ZAN_MF_USER) reasons[ZAN_MF_R_SYNTH]++;
            else if (F->is_async) reasons[ZAN_MF_R_ASYNC]++;
            else if (F->is_spec) reasons[ZAN_MF_R_SPEC]++;
            else if (F->virtual_dispatch) reasons[ZAN_MF_R_VIRTUAL]++;
            else if (F->addr_taken) reasons[ZAN_MF_R_ADDR]++;
            else if (F->indirect_call) reasons[ZAN_MF_R_INDIRECT]++;
            else if (!F->simple_abi) reasons[ZAN_MF_R_ABI]++;
            else reasons[ZAN_MF_R_POLICY]++;
            continue;
        }
        elig++;
        elig_insns += F->insns;
        if (F->clean_root) { clean++; clean_insns += F->insns; }
    }
    fprintf(stderr,
            "[cg-manifest] module: functions=%d (defined=%d extern=%d) "
            "globals=%d instructions=%llu\n",
            m->fn_count, m->defined_count, m->extern_count,
            m->global_count, m->total_insns);
    fprintf(stderr,
            "[cg-manifest] defined kinds: user=%d resume=%d release=%d "
            "vtable=%d reflect=%d adapter=%d other=%d; user specs=%d "
            "async=%d\n",
            kind_count[ZAN_MF_USER], kind_count[ZAN_MF_ASYNC_RESUME],
            kind_count[ZAN_MF_RELEASE], kind_count[ZAN_MF_VTABLE],
            kind_count[ZAN_MF_REFLECT], kind_count[ZAN_MF_ADAPTER],
            kind_count[ZAN_MF_OTHER], spec, async);
    fprintf(stderr,
            "[cg-manifest] policy: native=%d debug=%d leaks=%d arcguard=%d "
            "obf=%d refl=%d lib=%d\n",
            g->mf_native, g->emit_debug, g->check_leaks, g->arc_guard,
            g->obfuscate_strings, g->refl_used, g->emit_lib);
    fprintf(stderr,
            "[cg-manifest] stage-4 allowlist: eligible=%d insns=%llu "
            "(%.1f%% of defined body); clean-roots=%d insns=%llu\n",
            elig, elig_insns,
            m->total_insns
                ? 100.0 * (double)elig_insns / (double)m->total_insns
                : 0.0,
            clean, clean_insns);
    fprintf(stderr,
            "[cg-manifest] ineligible: synth=%d async=%d spec=%d virtual=%d "
            "addr-taken=%d indirect=%d aggr-abi=%d policy=%d\n",
            reasons[ZAN_MF_R_SYNTH], reasons[ZAN_MF_R_ASYNC],
            reasons[ZAN_MF_R_SPEC], reasons[ZAN_MF_R_VIRTUAL],
            reasons[ZAN_MF_R_ADDR], reasons[ZAN_MF_R_INDIRECT],
            reasons[ZAN_MF_R_ABI], reasons[ZAN_MF_R_POLICY]);
}

static int mf_json_cmp_fn(const void *a, const void *b) {
    const zan_mf_fn *fa = *(const zan_mf_fn *const *)a;
    const zan_mf_fn *fb = *(const zan_mf_fn *const *)b;
    return strcmp(fa->name, fb->name);
}

static int mf_json_cmp_str(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int mf_json_cmp_int(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

static void mf_json_str(FILE *out, const char *s) {
    fputc('"', out);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') fputc('\\', out);
        fputc(*s, out);
    }
    fputc('"', out);
}

/* 内部辅助逻辑 */
static void mf_json_names(FILE *out, const char **arr, int cnt) {
    if (cnt > 1) qsort(arr, (size_t)cnt, sizeof(char *), mf_json_cmp_str);
    fputc('[', out);
    for (int i = 0; i < cnt; i++) {
        if (i && !strcmp(arr[i], arr[i - 1])) continue;
        if (i) fputc(',', out);
        mf_json_str(out, arr[i]);
    }
    fputc(']', out);
}

static int mf_write_json(zan_irgen_t *g, zan_cg_manifest_t *m,
                         const char *path) {
    FILE *out = fopen(path, "wb");
    if (!out) return ZAN_ERROR;
    fprintf(out, "{\"schema\":\"zan-cg-manifest-v1\",\"policy\":{");
    fprintf(out, "\"native\":%s,\"debug\":%s,\"leaks\":%s,"
                 "\"arcGuard\":%s,\"obfuscate\":%s,\"reflection\":%s,"
                 "\"library\":%s",
            g->mf_native ? "true" : "false",
            g->emit_debug ? "true" : "false",
            g->check_leaks ? "true" : "false",
            g->arc_guard ? "true" : "false",
            g->obfuscate_strings ? "true" : "false",
            g->refl_used ? "true" : "false",
            g->emit_lib ? "true" : "false");
    fprintf(out,
            "},\"totals\":{\"functions\":%d,\"defined\":%d,\"extern\":%d,"
            "\"globals\":%d,\"instructions\":%llu},\"functions\":[",
            m->fn_count, m->defined_count, m->extern_count, m->global_count,
            m->total_insns);
    zan_mf_fn **order = (zan_mf_fn **)malloc(
        (size_t)(m->fn_count ? m->fn_count : 1) * sizeof(*order));
    if (!order) { fclose(out); return ZAN_ERROR; }
    for (int i = 0; i < m->fn_count; i++) order[i] = &m->fns[i];
    qsort(order, (size_t)m->fn_count, sizeof(*order), mf_json_cmp_fn);
    for (int i = 0; i < m->fn_count; i++) {
        zan_mf_fn *F = order[i];
        if (i) fputc(',', out);
        fprintf(out, "{\"name\":");
        mf_json_str(out, F->name);
        fprintf(out, ",\"kind\":\"%s\",\"defined\":%s,\"internal\":%s,"
                     "\"varargs\":%s,\"blocks\":%u,\"insns\":%u",
                mf_kind_name(F->kind),
                F->defined ? "true" : "false",
                F->internal_linkage ? "true" : "false",
                F->varargs ? "true" : "false", F->blocks, F->insns);
        if (F->defined)
            fprintf(out,
                    ",\"async\":%s,\"spec\":%s,\"virtual\":%s,"
                    "\"simpleAbi\":%s,\"addrTaken\":%s,\"indirectCall\":%s,"
                    "\"eligible\":%s,\"cleanRoot\":%s",
                    F->is_async ? "true" : "false",
                    F->is_spec ? "true" : "false",
                    F->virtual_dispatch ? "true" : "false",
                    F->simple_abi ? "true" : "false",
                    F->addr_taken ? "true" : "false",
                    F->indirect_call ? "true" : "false",
                    F->eligible ? "true" : "false",
                    F->clean_root ? "true" : "false");
        fprintf(out, ",\"calls\":");
        if (F->call_cnt > 1)
            qsort(F->calls, (size_t)F->call_cnt, sizeof(int), mf_json_cmp_int);
        fputc('[', out);
        for (int k = 0, printed = 0, prev = -1; k < F->call_cnt; k++) {
            if (k && F->calls[k] == prev) continue;
            prev = F->calls[k];
            if (printed++) fputc(',', out);
            mf_json_str(out, m->fns[prev].name);
        }
        fputc(']', out);
        fprintf(out, ",\"externCalls\":");
        mf_json_names(out, F->exts, F->ext_cnt);
        fprintf(out, ",\"globals\":");
        mf_json_names(out, F->globs, F->glob_cnt);
        fputc('}', out);
    }
    fprintf(out, "]}\n");
    free(order);
    return fclose(out) == 0 ? ZAN_OK : ZAN_ERROR;
}

/* 底层系统交互与数据协议契约 */
void zan_irgen_manifest_build(zan_irgen_t *g, zan_cg_manifest_t *m,
                              bool native) {
    g->mf_native = native;
    mf_build(g, m, native);
}

void zan_irgen_manifest_free(zan_cg_manifest_t *m) { mf_free(m); }

void zan_irgen_manifest_report(zan_irgen_t *g, const zan_cg_manifest_t *m) {
    mf_report(g, m);
}

int zan_irgen_manifest_write_json(zan_irgen_t *g, zan_cg_manifest_t *m,
                                  const char *path) {
    return mf_write_json(g, m, path);
}
