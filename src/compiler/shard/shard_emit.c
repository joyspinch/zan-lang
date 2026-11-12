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
    int         *idx;
    int          n, cap;
    long long    insns;
    const char  *minname;
} sh_comp_t;

typedef struct {
    int mf_i;
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

static bool sh_emit_one_file(sh_state_t *st, int shard_idx, const char *frag_path,
                             LLVMTargetMachineRef tm, const char *path,
                             char *errbuf, size_t errsz) {
    zan_irgen_t *g = st->g;
    sh_ensure_parent_dir(path);
    const char *dump = getenv("ZAN_SHARD_DUMP");
    if (dump && *dump) {
        FILE *df = fopen(dump, shard_idx == 0 ? "wb" : "ab");
        FILE *sf = fopen(frag_path, "rb");
        if (df && sf) {
            char cbuf[8192];
            size_t nr;
            while ((nr = fread(cbuf, 1, sizeof(cbuf), sf)) > 0)
                fwrite(cbuf, 1, nr, df);
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
    if (LLVMParseIRInContext(ctx, mb, &mod, &perr)) {
        snprintf(errbuf, errsz, "parse: %.160s", perr ? perr : "?");
        if (perr) LLVMDisposeMessage(perr);
        LLVMContextDispose(ctx);
        return false;
    }
    LLVMSetModuleIdentifier(mod, "zan-shard", strlen("zan-shard"));
    LLVMSetSourceFileName(mod, "zan-shard", strlen("zan-shard"));
    const char *triple = LLVMGetTarget(g->mod);
    if (triple && *triple) LLVMSetTarget(mod, triple);
    LLVMSetDataLayout(mod, LLVMGetDataLayoutStr(g->mod));
    char *vmsg = NULL;
    SH_TRACE("shard emit: parse ok -> verify\n");
    if (LLVMVerifyModule(mod, LLVMReturnStatusAction, &vmsg)) {
        st->optimizer_failed = true;
        snprintf(errbuf, errsz, "verify: %.160s", vmsg ? vmsg : "?");
        if (vmsg) LLVMDisposeMessage(vmsg);
        LLVMDisposeModule(mod);
        LLVMContextDispose(ctx);
        return false;
    }
    if (vmsg) LLVMDisposeMessage(vmsg);
    /* shard 模块必须和主模块跑同一级别的 LLVM pass pipeline，否则
     * publish 模式下 shard 里的函数完全没经过体积优化，会导致发布产物
     * 体积几乎不降。ZAN_SHARD_OPT 环境变量留作逃生口。 */
    if (zan_opt_validate_cpu(g, mod) != ZAN_OK) {
        st->optimizer_failed = true;
        snprintf(errbuf, errsz, "unsupported CPU intrinsic");
        LLVMDisposeModule(mod);
        LLVMContextDispose(ctx);
        return false;
    }
    if (g->publish_mode || g->obfuscate_strings || getenv("ZAN_SHARD_OPT")) {
        if (zan_opt_run_passes_on_module(mod, tm,
                (g->publish_mode || g->obfuscate_strings) ? ZAN_OPT_SIZE : ZAN_OPT_FULL) != ZAN_OK) {
            st->optimizer_failed = true;
            snprintf(errbuf, errsz, "LLVM optimization failed");
            LLVMDisposeModule(mod);
            LLVMContextDispose(ctx);
            return false;
        }
    }
    if (zan_opt_verify_module(mod, "before shard object output") != ZAN_OK) {
        st->optimizer_failed = true;
        snprintf(errbuf, errsz, "final LLVM verification failed");
        LLVMDisposeModule(mod);
        LLVMContextDispose(ctx);
        return false;
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

/* ---- entry ---------------------------------------------------------------- */
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

