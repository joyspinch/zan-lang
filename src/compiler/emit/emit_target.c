zan_status_t zan_irgen_emit(zan_irgen_t *g, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return ZAN_ERROR;
    g_di_emit_ctx = g;
    di_debug_types_reset();
    if (g->publish_mode && !g->emit_debug && !g->function_compactor) {
        char error[4096];
        g->function_compactor = zan_irgen_compactor_create(
            g->mod, error, sizeof(error));
        if (!g->function_compactor) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, unit->loc, 3089,
                          "LLVM function compactor initialization failed: %s", error);
            return ZAN_ERROR;
        }
    }

    discover_generic_insts(g, unit);

    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind == AST_STRUCT_DECL || decl->kind == AST_CLASS_DECL) {
            zan_symbol_t *sym = zan_binder_lookup(g->binder, decl->type_decl.name);
            if (sym) register_struct_type(g, sym);
        }
    }

    int work_count = 0;
    method_body_work_t *work = declare_user_methods(g, unit, &work_count);
    if (zan_diag_has_errors(g->diag)) { free(work); return ZAN_ERROR; }

    bool prune_stdlib_bodies = (g->publish_mode || g->obfuscate_strings) && !g->emit_debug;
    bool prune_user = prune_stdlib_bodies;
    unsigned char *live = (unsigned char *)calloc((size_t)work_count + 1, 1);
    for (int w = 0; w < work_count; w++) {
        zan_symbol_t *owner = work[w].type_sym;
        if (!prune_stdlib_bodies || !owner || !owner->decl) {
            live[w] = 1;
            continue;
        }
        if (!owner->decl->from_stdlib) {
            if (!prune_user) {
                live[w] = 1;
            } else {
                zan_ast_node_t *m = work[w].member;
                bool is_cctor = (m->kind == AST_CONSTRUCTOR_DECL &&
                                 (m->method_decl.modifiers & MOD_STATIC) != 0);
                bool is_entry = (m->kind == AST_METHOD_DECL &&
                                 (m->method_decl.modifiers & MOD_STATIC) != 0 &&
                                 ((m->method_decl.name.len == 4 &&
                                   memcmp(m->method_decl.name.str, "Main", 4) == 0) ||
                                  (m->method_decl.name.len == 12 &&
                                   memcmp(m->method_decl.name.str, "__DesignMain", 12) == 0)));
                bool is_lib_export = g->emit_lib &&
                    ((m->method_decl.modifiers & MOD_PUBLIC) ||
                     m->kind == AST_CONSTRUCTOR_DECL);
                bool is_refl_root = false;
                if (g->refl_used && owner) {
                    if (g->refl_mtabs) {
                        for (int k = 0; k < g->refl_mtab_count; k++) {
                            if (g->refl_mtabs[k].sym == owner) {
                                is_refl_root = true;
                                break;
                            }
                        }
                    }
                    if (!is_refl_root && g->refl_metas) {
                        for (int k = 0; k < g->refl_meta_count; k++) {
                            if (g->refl_metas[k].sym == owner) {
                                is_refl_root = true;
                                break;
                            }
                        }
                    }
                    if (!is_refl_root && g->site_syms) {
                        for (int k = 0; k < g->leak_site_count; k++) {
                            if (g->site_syms[k] == owner) {
                                if (!g->publish_mode || refl_type_needs_site_meta(g, owner)) {
                                    is_refl_root = true;
                                    break;
                                }
                            }
                        }
                    }
                }
                bool has_attrs = (m->meta && m->meta->attributes.count > 0);
                if (is_entry || is_cctor || is_lib_export || is_refl_root || has_attrs) {
                    live[w] = 1;
                }
            }
        }
    }
    emit_user_method_bodies(g, work, work_count, live);
    emit_pending_method_specs(g);
    discover_generic_insts(g, unit);
    emit_pending_method_specs(g);
    if (zan_diag_has_errors(g->diag)) {
        free(live);
        free(work);
        return ZAN_ERROR;
    }

    {
        zan_ast_node_t *design_main = NULL;
        /* C# 同步：泛型类中的 static Main 不是合法入口——按开放泛型编译
         * 会让体内对该类类型参数的一切显式引用（调用类型实参等）落回
         * 开放参数（探针实证扩展收到 entity=T cols=0），静默产坏码。 */
        bool generic_main = false;
        zan_loc_t generic_main_loc = zan_loc(0, 0, 0, 0);
        for (int i = 0; i < unit->comp_unit.decls.count; i++) {
            zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
            if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL)
                continue;

            for (int j = 0; j < decl->type_decl.members.count; j++) {
                zan_ast_node_t *member = decl->type_decl.members.items[j];
                if (member->kind != AST_METHOD_DECL ||
                    !(member->method_decl.modifiers & MOD_STATIC)) continue;
                if (member->method_decl.name.len == 4 &&
                    memcmp(member->method_decl.name.str, "Main", 4) == 0) {
                    if (decl->type_decl.type_params.count > 0) {
                        if (!generic_main) { generic_main = true; generic_main_loc = member->loc; }
                        continue;
                    }
                    zan_symbol_t *main_type_sym =
                        zan_binder_lookup(g->binder, decl->type_decl.name);
                    emit_main_method(g, member, main_type_sym, unit);
                    goto done;
                }
                if (!design_main && member->method_decl.name.len == 12 &&
                    memcmp(member->method_decl.name.str, "__DesignMain",
                           12) == 0 &&
                    decl->type_decl.type_params.count == 0) {
                    design_main = member;
                }
            }
        }
        if (design_main) {
            for (int i = 0; i < unit->comp_unit.decls.count; i++) {
                zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
                if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL)
                    continue;
                bool has = false;
                for (int j = 0; j < decl->type_decl.members.count; j++) {
                    if (decl->type_decl.members.items[j] == design_main) {
                        has = true;
                        break;
                    }
                }
                if (has) {
                    zan_symbol_t *main_type_sym =
                        zan_binder_lookup(g->binder, decl->type_decl.name);
                    emit_main_method(g, design_main, main_type_sym, unit);
                    break;
                }
            }
        }
        if (!LLVMGetNamedFunction(g->mod, "main") && generic_main) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, generic_main_loc, 3102,
                "entry point 'Main' cannot be declared in a generic class"
                " -- move it to a non-generic type (C# parity)");
        }
    }
done:
    ;
    if (g->function_compactor) {
        LLVMClearInsertionPosition(g->builder);
        LLVMValueRef main_fn = LLVMGetNamedFunction(g->mod, "main");
        if (main_fn) zan_irgen_compact_completed(g, main_fn);
    }
    emit_pending_method_specs(g);
    if (zan_diag_has_errors(g->diag)) {
        free(live);
        free(work);
        return ZAN_ERROR;
    }

    if (prune_stdlib_bodies) emit_vtables(g);
    work_fn_index_t work_ix = { 0 };
    work_fn_index_build(&work_ix, work, work_count);
    int pending;
    do {
        pending = 0;
        for (int w = 0; w < work_count; w++) {
            if (live[w]) continue;
            zan_ast_node_t *member = work[w].member;
            bool is_refl_root = false;
            if (g->refl_used && work[w].type_sym) {
                zan_symbol_t *tsym = work[w].type_sym;
                if (g->refl_mtabs) {
                    for (int m = 0; m < g->refl_mtab_count; m++) {
                        if (g->refl_mtabs[m].sym == tsym) {
                            is_refl_root = true;
                            break;
                        }
                    }
                }
                    if (!is_refl_root && g->refl_metas) {
                        for (int m = 0; m < g->refl_meta_count; m++) {
                            if (g->refl_metas[m].sym == tsym) {
                                is_refl_root = true;
                                break;
                            }
                        }
                    }
                    if (!is_refl_root && g->site_syms) {
                        for (int m = 0; m < g->leak_site_count; m++) {
                            if (g->site_syms[m] == tsym) {
                                if (!g->publish_mode || refl_type_needs_site_meta(g, tsym)) {
                                    is_refl_root = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            bool root = (g->emit_lib &&
                         ((member->method_decl.modifiers & MOD_PUBLIC) ||
                          member->kind == AST_CONSTRUCTOR_DECL)) ||
                        is_refl_root ||
                        (member->kind == AST_METHOD_DECL &&
                         refl_is_html_handler_name(g,
                             (const char *)member->method_decl.name.str,
                             (size_t)member->method_decl.name.len));
            if (!root)
                root = body_has_live_use(work[w].fn, live, &work_ix);
            if (root) { live[w] = 1; pending++; }
        }
        if (pending) {
            emit_user_method_bodies(g, work, work_count, live);
            emit_pending_method_specs(g);
            if (zan_diag_has_errors(g->diag)) break;
        }
    } while (pending);
    free(work_ix.slots);
    for (int w = 0; w < work_count; w++) {
        if (live[w]) continue;
        free(work[w].param_types);
        LLVMValueRef fn = work[w].fn;
        if (LLVMIsDeclaration(fn)) {
            LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "dead");
            LLVMPositionBuilderAtEnd(g->builder, bb);
            LLVMBuildUnreachable(g->builder);
        }
        fn = work[w].resume_fn;
        if (fn && LLVMIsDeclaration(fn)) {
            LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "dead");
            LLVMPositionBuilderAtEnd(g->builder, bb);
            LLVMBuildUnreachable(g->builder);
        }
    }
    free(live);
    free(work);
    if (zan_diag_has_errors(g->diag)) return ZAN_ERROR;
    di_clear(g);
    /* 不变量：全部分配点登记必须先于任何按站点索引的表发射
     * （live/dtor/tyname/meta/desc）。反射 ctor thunk 的分配点原本在
     * finalize 才登记，名表早已冻结——反射造出的对象过不了任何
     * is/下转（MapRow (T)obj 全军覆没）。 */
    refl_reserve_thunk_sites(g);
    emit_all_class_releases(g);
    emit_site_live_tables(g);
    emit_site_dtor_table(g);
    emit_site_tyname_table(g);
    emit_site_meta_table(g);
    emit_site_field_graph_table(g);
    zan_irgen_emit_arc_desc_init(g);
    refl_finalize_mtabs(g);
    if (!prune_stdlib_bodies) emit_vtables(g);
    if (g->emit_lib && g->emit_shared && g->target_is_windows)
        emit_windows_dll_main(g);
    zan_irgen_emit_string_deobf(g);
    if (g->tid_name_reg_global) {
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        unsigned n = (unsigned)g->tid_name_count;
        LLVMTypeRef reg_ty = LLVMArrayType(g->tid_name_reg_ent_ty, n + 1);
        LLVMValueRef newg = LLVMAddGlobal(g->mod, reg_ty,
                                          LLVMGetValueName(g->tid_name_reg_global));
        LLVMSetLinkage(newg, LLVMInternalLinkage);
        LLVMReplaceAllUsesWith(g->tid_name_reg_global, newg);
        LLVMDeleteGlobal(g->tid_name_reg_global);
        g->tid_name_reg_global = newg;
        LLVMValueRef *elems = zan_arena_alloc(g->arena,
            (int)(n + 1) * sizeof(LLVMValueRef));
        LLVMValueRef *fields = zan_arena_alloc(g->arena, 2 * sizeof(LLVMValueRef));
        for (unsigned i = 0; i < n; i++) {
            fields[0] = LLVMConstBitCast(g->tid_names[i].tid, i8ptr);
            fields[1] = zan_irgen_intern_string(g, g->tid_names[i].name);
            elems[i] = LLVMConstStruct(fields, 2, 0);
        }
        fields[0] = LLVMConstNull(i8ptr);
        fields[1] = LLVMConstNull(i8ptr);
        elems[n] = LLVMConstStruct(fields, 2, 0); /* terminator */
        LLVMSetInitializer(newg, LLVMConstArray(g->tid_name_reg_ent_ty,
                                                elems, (unsigned)(n + 1)));
    }
    if (zan_diag_has_errors(g->diag)) {
        return ZAN_ERROR;
    }
    if (g->emit_debug && g->di_builder) {
        LLVMSetCurrentDebugLocation2(g->builder, NULL);
        LLVMDIBuilderFinalize(g->di_builder);
    }
    if (g->function_compactor) {
        LLVMClearInsertionPosition(g->builder);
        for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
             fn = LLVMGetNextFunction(fn)) {
            if (LLVMIsDeclaration(fn)) continue;
            zan_irgen_compact_completed(g, fn);
            if (zan_diag_has_errors(g->diag)) break;
        }
        zan_irgen_compactor_destroy((zan_irgen_compactor_t *)g->function_compactor);
        g->function_compactor = NULL;
        if (zan_diag_has_errors(g->diag)) return ZAN_ERROR;
    }
    char *error = NULL;
    if (LLVMVerifyModule(g->mod, LLVMReturnStatusAction, &error)) {
        const char *culprit = NULL;
        for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
             fn = LLVMGetNextFunction(fn)) {
            if (LLVMIsDeclaration(fn)) continue;
            if (LLVMVerifyFunction(fn, LLVMReturnStatusAction)) {
                culprit = LLVMGetValueName(fn);
                break;
            }
        }
        zan_diag_emit_code(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0), 3090,
                      "LLVM verification failed%s%s: %s",
                      culprit ? " in " : "", culprit ? culprit : "", error);
        if (getenv("ZANC_DUMP_BAD_IR")) {
            for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
                 fn = LLVMGetNextFunction(fn)) {
                if (LLVMIsDeclaration(fn)) continue;
                if (!LLVMVerifyFunction(fn, LLVMReturnStatusAction)) continue;
                char p[512];
                snprintf(p, sizeof(p), "_scratch/bad_%s.txt",
                         LLVMGetValueName(fn));
                FILE *df = fopen(p, "w");
                if (df) {
                    char *txt = LLVMPrintValueToString(fn);
                    fputs(txt, df);
                    free(txt);
                    fclose(df);
                    fprintf(stderr, "dumped %s\n", p);
                }
            }
        }
        LLVMDisposeMessage(error);
        return ZAN_ERROR;
    }
    if (error) LLVMDisposeMessage(error);

    return ZAN_OK;
}

/* ---- output ---- */

zan_status_t zan_irgen_write_ir(zan_irgen_t *g, const char *path) {
    char *ir = LLVMPrintModuleToString(g->mod);
    char *owned = NULL;   /* 自述头 + IR 的拼接缓冲（malloc 释放） */
    /* 产物自述目标特性：编译器自己用 AVX2 档后端出码，但 .ll 文本里没有这层
       信息，消费方（llc 等）按 generic CPU 处理时 256 位向量路径拆不开
       （SplitVectorOperand fatal error，见项目台账 A388）。把 CPU/特性写进
       IR 头部，消费方照抄即可复现编译器的选择。 */
    char header[768];
    int hn = 0;
    if (g->target_cpu[0]) {
        hn = snprintf(header, sizeof(header),
                      "; zanc-target-cpu: %s\n"
                      "; zanc-target-features: %s\n"
                      "; zanc: 消费本 IR 必须用同样的目标特性，例如\n"
                      "; zanc:   llc -mcpu=%s -mattr=%s -filetype=obj out.ll\n"
                      "; zanc: generic CPU 下 256 位向量路径无法合法化（台账 A388）。\n",
                      g->target_cpu,
                      g->target_features[0] ? g->target_features : "(default)",
                      g->target_cpu,
                      g->target_features[0] ? g->target_features : "+sse2");
        if (hn < 0) hn = 0;
        if ((size_t)hn > sizeof(header)) hn = (int)sizeof(header);
    }
    if (hn > 0) {
        owned = (char *)malloc((size_t)hn + strlen(ir) + 1);
        if (owned) {
            memcpy(owned, header, (size_t)hn);
            strcpy(owned + hn, ir);
        }
    }
    const char *text = owned ? owned : ir;
    zan_status_t rc = ZAN_OK;
    if (!path) {
        if (fputs(text, stdout) < 0) rc = ZAN_ERROR;
    } else {
        FILE *f = fopen(path, "w");
        if (!f) {
            rc = ZAN_ERROR;
        } else {
            bool ok = (fputs(text, f) >= 0);
            if (fclose(f) != 0) ok = false;
            if (!ok) rc = ZAN_ERROR;
        }
    }
    if (owned) free(owned);
    LLVMDisposeMessage(ir);
    return rc;
}

int zan_irgen_stub_extern_lib(zan_irgen_t *g, const char *lib, int lib_len) {
    int stubbed = 0;
    for (int i = 0; i < g->extern_fn_count; i++) {
        if ((int)g->extern_fns[i].lib.len != lib_len ||
            memcmp(g->extern_fns[i].lib.str, lib, (size_t)lib_len) != 0)
            continue;
        char nm[256];
        snprintf(nm, sizeof(nm), "%.*s", (int)g->extern_fns[i].name.len,
                 g->extern_fns[i].name.str);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, nm);
        if (!fn) continue;
        if (LLVMCountBasicBlocks(fn) > 0) continue;
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
        LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);
        LLVMPositionBuilderAtEnd(b, bb);
        LLVMTypeRef ft = LLVMGlobalGetValueType(fn);
        LLVMTypeRef rt = LLVMGetReturnType(ft);
        switch (LLVMGetTypeKind(rt)) {
        case LLVMVoidTypeKind:
            LLVMBuildRetVoid(b);
            break;
        case LLVMIntegerTypeKind:
            LLVMBuildRet(b, LLVMConstInt(rt,
                LLVMGetIntTypeWidth(rt) >= 64 ? 0 : (unsigned long long)-1, 1));
            break;
        case LLVMFloatTypeKind:
        case LLVMDoubleTypeKind:
            LLVMBuildRet(b, LLVMConstReal(rt, 0.0));
            break;
        default:
            LLVMBuildRet(b, LLVMConstNull(rt));
            break;
        }
        LLVMDisposeBuilder(b);
        stubbed++;
    }
    return stubbed;
}

int zan_irgen_drop_extern_lib(zan_irgen_t *g, const char *lib, int lib_len) {
    int kept = 0, dropped = 0;
    for (int li = 0; li < g->extern_lib_count; li++) {
        if ((int)g->extern_libs[li].len == lib_len && lib_len > 0 &&
            memcmp(g->extern_libs[li].str, lib, (size_t)lib_len) == 0) {
            dropped++;
            continue;
        }
        g->extern_libs[kept++] = g->extern_libs[li];
    }
    g->extern_lib_count = kept;
    return dropped;
}

int zan_irgen_prune_extern_libs(zan_irgen_t *g) {
    int kept = 0, dropped = 0;
    for (int li = 0; li < g->extern_lib_count; li++) {
        const char *lib = g->extern_libs[li].str;
        int lib_len = (int)g->extern_libs[li].len;
        int live = 0;
        for (int i = 0; i < g->extern_fn_count && !live; i++) {
            if ((int)g->extern_fns[i].lib.len != lib_len ||
                memcmp(g->extern_fns[i].lib.str, lib, (size_t)lib_len) != 0)
                continue;
            char nm[256];
            snprintf(nm, sizeof(nm), "%.*s", (int)g->extern_fns[i].name.len,
                     g->extern_fns[i].name.str);
            if (LLVMGetNamedFunction(g->mod, nm)) live = 1;
        }
        if (!live) {
            int has_fns = 0;
            for (int i = 0; i < g->extern_fn_count && !has_fns; i++) {
                if ((int)g->extern_fns[i].lib.len == lib_len &&
                    memcmp(g->extern_fns[i].lib.str, lib, (size_t)lib_len) == 0)
                    has_fns = 1;
            }
            if (!has_fns) live = 1;
        }
        if (live) {
            g->extern_libs[kept++] = g->extern_libs[li];
        } else {
            dropped++;
        }
    }
    g->extern_lib_count = kept;
    return dropped;
}

bool zan_irgen_defines_prefix(zan_irgen_t *g, const char *prefix) {
    if (!g || !prefix) return false;
    for (int i = 0; i < g->prefix_cache_count; i++) {
        if (strcmp(g->prefix_cache[i], prefix) == 0)
            return g->prefix_cache_val[i];
    }
    if (!g->mod) return false;
    size_t plen = strlen(prefix);
    if (!plen) return false;
    bool found = false;
    for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
         fn = LLVMGetNextFunction(fn)) {
        if (LLVMCountBasicBlocks(fn) == 0) continue;
        size_t nlen = 0;
        const char *nm = LLVMGetValueName2(fn, &nlen);
        if (nm && nlen >= plen && memcmp(nm, prefix, plen) == 0) {
            found = true;
            break;
        }
    }
    if (g->prefix_cache_count < 128) {
        snprintf(g->prefix_cache[g->prefix_cache_count],
                 sizeof(g->prefix_cache[0]), "%s", prefix);
        g->prefix_cache_val[g->prefix_cache_count] = found;
        g->prefix_cache_count++;
    }
    return found;
}

static void w32_build_adapter_into(zan_irgen_t *g, LLVMValueRef fn,
                                   LLVMValueRef real, LLVMTypeRef lft) {
    LLVMTypeRef src_ft = LLVMGlobalGetValueType(fn);
    unsigned nparams = LLVMCountParamTypes(src_ft);
    if (LLVMCountParamTypes(lft) != nparams) return;
    size_t slots = nparams ? nparams : 1;
    LLVMTypeRef *sps = zan_arena_alloc(g->arena, slots * sizeof(*sps));
    LLVMTypeRef *lps = zan_arena_alloc(g->arena, slots * sizeof(*lps));
    LLVMGetParamTypes(src_ft, sps);
    LLVMGetParamTypes(lft, lps);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);
    LLVMPositionBuilderAtEnd(b, bb);
    LLVMValueRef *args = zan_arena_alloc(g->arena, slots * sizeof(*args));
    for (unsigned p = 0; p < nparams; p++) {
        LLVMValueRef a = LLVMGetParam(fn, p);
        LLVMTypeKind sk = LLVMGetTypeKind(sps[p]);
        LLVMTypeKind lk = LLVMGetTypeKind(lps[p]);
        if (sps[p] == lps[p]) {
            args[p] = a;
        } else if (sk == LLVMIntegerTypeKind && lk == LLVMPointerTypeKind) {
            args[p] = LLVMBuildIntToPtr(b, a, lps[p], "");
        } else if (sk == LLVMPointerTypeKind && lk == LLVMIntegerTypeKind) {
            args[p] = LLVMBuildPtrToInt(b, a, lps[p], "");
        } else if (sk == LLVMIntegerTypeKind && lk == LLVMIntegerTypeKind) {
            args[p] = LLVMBuildIntCast2(b, a, lps[p], 1, "");
        } else if (sk == LLVMPointerTypeKind && lk == LLVMPointerTypeKind) {
            args[p] = LLVMBuildPointerCast(b, a, lps[p], "");
        } else {
            args[p] = a;
        }
    }
    LLVMValueRef rv = zan_call2(b, lft, real, args, nparams, "");
    LLVMTypeRef srt = LLVMGetReturnType(src_ft);
    LLVMTypeRef lrt = LLVMGetReturnType(lft);
    if (LLVMGetTypeKind(srt) == LLVMVoidTypeKind) {
        LLVMBuildRetVoid(b);
    } else if (srt == lrt) {
        LLVMBuildRet(b, rv);
    } else if (LLVMGetTypeKind(lrt) == LLVMVoidTypeKind) {
        LLVMBuildRet(b, LLVMConstNull(srt));
    } else if (LLVMGetTypeKind(lrt) == LLVMPointerTypeKind &&
               LLVMGetTypeKind(srt) == LLVMIntegerTypeKind) {
        LLVMBuildRet(b, LLVMBuildPtrToInt(b, rv, srt, ""));
    } else if (LLVMGetTypeKind(lrt) == LLVMIntegerTypeKind &&
               LLVMGetTypeKind(srt) == LLVMIntegerTypeKind) {
        LLVMBuildRet(b, LLVMBuildIntCast2(b, rv, srt, 1, ""));
    } else if (LLVMGetTypeKind(lrt) == LLVMIntegerTypeKind &&
               LLVMGetTypeKind(srt) == LLVMPointerTypeKind) {
        LLVMBuildRet(b, LLVMBuildIntToPtr(b, rv, srt, ""));
    } else {
        LLVMBuildRet(b, rv);
    }
    LLVMDisposeBuilder(b);
}

static LLVMValueRef w32_build_adapter(zan_irgen_t *g, const char *name,
                                      LLVMTypeRef src_ft, LLVMValueRef real,
                                      LLVMTypeRef lft) {
    if (LLVMCountParamTypes(src_ft) != LLVMCountParamTypes(lft))
        return NULL;
    LLVMValueRef fn = LLVMAddFunction(g->mod, name, src_ft);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    w32_build_adapter_into(g, fn, real, lft);
    return fn;
}

/* 初始化every target family the build links (see CMakeLists */
void zan_irgen_init_llvm_targets(void) {
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmParser();
    LLVMInitializeX86AsmPrinter();

    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmParser();
    LLVMInitializeAArch64AsmPrinter();

#ifdef ZAN_HAVE_LLVM_ARM
    LLVMInitializeARMTargetInfo();
    LLVMInitializeARMTarget();
    LLVMInitializeARMTargetMC();
    LLVMInitializeARMAsmParser();
    LLVMInitializeARMAsmPrinter();
#endif

#ifdef ZAN_HAVE_LLVM_WEBASSEMBLY
    LLVMInitializeWebAssemblyTargetInfo();
    LLVMInitializeWebAssemblyTarget();
    LLVMInitializeWebAssemblyTargetMC();
    LLVMInitializeWebAssemblyAsmParser();
    LLVMInitializeWebAssemblyAsmPrinter();
#endif

#ifdef ZAN_HAVE_LLVM_RISCV
    LLVMInitializeRISCVTargetInfo();
    LLVMInitializeRISCVTarget();
    LLVMInitializeRISCVTargetMC();
    LLVMInitializeRISCVAsmParser();
    LLVMInitializeRISCVAsmPrinter();
#endif
}
zan_status_t zan_bind_target_layout(zan_irgen_t *g,
                                           LLVMTargetMachineRef *out_tm) {
    char *triple;
    if (g->target_triple[0]) {
        triple = LLVMCreateMessage(g->target_triple);
    } else {
        triple = LLVMGetDefaultTargetTriple();
#ifdef _WIN32
        /* 发射GNU-ABI (MinGW) objects so the produced code links against the bundled ld */
        {
            const char *dash = strchr(triple, '-');
            size_t archlen = dash ? (size_t)(dash - triple) : strlen(triple);
            char gnu[128];
            if (archlen > sizeof(gnu) - 20) archlen = sizeof(gnu) - 20;
            memcpy(gnu, triple, archlen);
            snprintf(gnu + archlen, sizeof(gnu) - archlen, "-w64-windows-gnu");
            LLVMDisposeMessage(triple);
            triple = LLVMCreateMessage(gnu);
        }
#endif
    }

    LLVMTargetRef target;
    char *error = NULL;

    if (LLVMGetTargetFromTriple(triple, &target, &error)) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0), 3091,
                      "failed to get target: %s", error);
        LLVMDisposeMessage(error);
        LLVMDisposeMessage(triple);
        return ZAN_ERROR;
    }

    const char *tm_cpu = "generic";
    const char *tm_features = "";
    if (strncmp(triple, "wasm", 4) == 0) {
        tm_features = "+exception-handling,+reference-types";
    } else if (strncmp(triple, "riscv64", 7) == 0) {
        tm_cpu = "generic-rv64";
        tm_features = "+m,+a,+f,+d,+c";
        if (!LLVMGetModuleFlag(g->mod, "target-abi", 10))
            LLVMAddModuleFlag(g->mod, LLVMModuleFlagBehaviorError,
                              "target-abi", strlen("target-abi"),
                              LLVMValueAsMetadata(LLVMMDStringInContext(
                                  g->ctx, "lp64d", 5)));
    } else if (strncmp(triple, "riscv32", 7) == 0) {
        tm_cpu = "generic-rv32";
        tm_features = "+m,+c";
        if (!LLVMGetModuleFlag(g->mod, "target-abi", 10))
            LLVMAddModuleFlag(g->mod, LLVMModuleFlagBehaviorError,
                              "target-abi", strlen("target-abi"),
                              LLVMValueAsMetadata(LLVMMDStringInContext(
                                  g->ctx, "ilp32", 5)));
    } else if (strncmp(triple, "x86_64", 6) == 0) {
        tm_cpu = "x86-64";
        /* Android's x86_64 ABI matches the NDK baseline: SSE4.2, POPCNT,
           CX16, but no AVX/BMI/FMA/AES guarantee. Enabling AVX globally
           vectorizes ordinary initialization before any CPU feature check. */
        tm_features = strstr(triple, "android")
            ? "+sse4.2,+crc32,+popcnt,+cx16"
            : "+sse3,+ssse3,+sse4.1,+sse4.2,+crc32,+aes,+avx,+avx2,+fma,+bmi";
    } else if (strncmp(triple, "aarch64", 7) == 0 || strncmp(triple, "arm64", 5) == 0) {
        tm_features = "+aes";
    }
    LLVMCodeGenOptLevel cg = g->fast_codegen ? LLVMCodeGenLevelNone
                                             : LLVMCodeGenLevelDefault;
    const char *cgenv = getenv("ZAN_CODEGEN_LEVEL");
    if (cgenv && *cgenv) {
        int clvl = atoi(cgenv);
        if (clvl == 0) cg = LLVMCodeGenLevelNone;
        else if (clvl == 1) cg = LLVMCodeGenLevelLess;
        else if (clvl == 2) cg = LLVMCodeGenLevelDefault;
        else if (clvl == 3) cg = LLVMCodeGenLevelAggressive;
    }
    /* 记下后端选用的 CPU/特性：--emit-ir 的产物要靠它自述目标特性，
       否则消费方（llc 等）用 generic CPU 拿到 256 位向量路径会合法化失败。 */
    snprintf(g->target_cpu, sizeof(g->target_cpu), "%s", tm_cpu);
    snprintf(g->target_features, sizeof(g->target_features), "%s", tm_features);
    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target, triple, tm_cpu, tm_features,
        cg, LLVMRelocPIC, LLVMCodeModelDefault);

    LLVMSetTarget(g->mod, triple);
    LLVMTargetDataRef dl = LLVMCreateTargetDataLayout(tm);
    char *dl_str = LLVMCopyStringRepOfTargetData(dl);
    LLVMSetDataLayout(g->mod, dl_str);
    LLVMDisposeMessage(dl_str);
    LLVMDisposeTargetData(dl);
    LLVMDisposeMessage(triple);

    if (out_tm) *out_tm = tm;
    else LLVMDisposeTargetMachine(tm);
    return ZAN_OK;
}

void zan_irgen_bind_target(zan_irgen_t *g) {
    zan_irgen_init_llvm_targets();
    zan_bind_target_layout(g, NULL);
}

zan_status_t zan_irgen_create_target_machine(zan_irgen_t *g, LLVMTargetMachineRef *out_tm) {
    zan_irgen_init_llvm_targets();
    return zan_bind_target_layout(g, out_tm);
}

zan_status_t zan_irgen_write_obj(zan_irgen_t *g, const char *path) {
    abi_pending_report(g);
    if (zan_diag_has_errors(g->diag)) return ZAN_ERROR;
    bool w32_triple = strncmp(g->target_triple, "wasm32", 6) == 0;
    bool rv32_triple = strncmp(g->target_triple, "riscv32", 7) == 0;
    if (w32_triple || rv32_triple) {
        bool wasi = w32_triple;
        if (wasi) {
            LLVMValueRef mainf = LLVMGetNamedFunction(g->mod, "main");
            if (mainf && LLVMCountBasicBlocks(mainf) > 0)
                LLVMSetValueName2(mainf, "__main_argc_argv",
                                  strlen("__main_argc_argv"));
        }
        {
            LLVMValueRef f = LLVMGetNamedFunction(g->mod, "snprintf");
            if (f && LLVMCountBasicBlocks(f) == 0)
                LLVMSetValueName2(f, "zan_w32_snprintf",
                                  strlen("zan_w32_snprintf"));
        }
        static const struct { const char *name; const char *sig; } w32adapt[] = {
            { "malloc", "ps" },      { "calloc", "pss" },
            { "realloc", "pps" },    { "free", "vp" },
            { "strlen", "sp" },      { "memcpy", "ppps" },
            { "memset", "ppis" },    { "memcmp", "ipps" },
            { "strcat", "ppp" },     { "strcpy", "ppp" },
            { "strncpy", "ppps" },   { "strcmp", "ipp" },
            { "strncmp", "ipps" },   { "strrchr", "ppi" },
            { "strstr", "ppp" },     { "atoi", "ip" },
            { "fopen", "ppp" },      { "fclose", "ip" },
            { "fgetc", "ip" },       { "fputc", "iip" },
            { "fputs", "ipp" },      { "fgets", "ppip" },
            { "fread", "spssp" },    { "fwrite", "spssp" },
            { "fseek", "ipii" },     { "ftell", "ip" },
            { "fflush", "ip" },      { "remove", "ip" },
            { "rename", "ipp" },     { "chdir", "ip" },
            { "getcwd", "pps" },     { "mkdir", "ipi" },
            { "rmdir", "ip" },       { "opendir", "pp" },
            { "readdir", "pp" },     { "closedir", "ip" },
            { "time", "jp" },        { "poll", "ipii" },
            /* NativeMemory */
            { "strcspn", "ipp" },
            { "pthread_mutex_init", "iii" },
            { "pthread_mutex_lock", "ii" },
            { "pthread_mutex_unlock", "ii" },
            { "pthread_mutex_destroy", "ii" },
            { "setvbuf", "ipipi" },
            { "dlopen", "pip" },     { "dlsym", "ppp" },
            { "dlclose", "ip" },
            { "zan_file_fopen", "ppp" },
            { "zan_pkg_fopen", "ppp" },
            { "zan_gui_draw_polyline", "vppiii" },
            { "zan_gui_draw_polyline_fx", "vppiii" },
            { "zan_gui_draw_polybatch", "vippiii" },
            { "zan_gui_fill_rects", "vipi" },
            { "zan_gui_fill_circles", "vipi" },
            { "zan_gui_fill_radials", "vipi" },
            { "zan_gui_get_pixels", "pi" },
            { "zan_gui_blit_pixels", "vipiiiiiiii" },
            { "zan_image_get", "pp" },
            { "zan_image_register_argb", "ippiii" },
            { "zan_game_sprite_batch", "viipi" },
            { "zan_game_mesh_create", "iipipi" },
            { "zan_game_mesh_create_ex", "iipipii" },
            { "zan_game_draw3d", "iiipip" },
            { "zan_game_draw3d_batch_begin", "ii" },
            { "zan_game_draw3d_batch", "iiippi" },
            { "zan_game_draw3d_batch_end", "ii" },
            { "zan_gui_clear_hit_guards", "ii" },
            { "zan_gui_add_hit_guard", "iiiiii" },
            { "zan_gui_text_stat_read", "ji" },
            { "zan_gui_guard_call", "iii" },
            /* NativeMemory */
            { "memmove", "ppps" },
            { "memchr", "ppis" },
            { NULL, NULL }
        };
        LLVMTypeRef w_i32 = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef w_i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef w_ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        for (int i = 0; w32adapt[i].name; i++) {
            LLVMValueRef decl = LLVMGetNamedFunction(g->mod, w32adapt[i].name);
            if (!decl || LLVMCountBasicBlocks(decl) > 0) continue;
            LLVMTypeRef dft = LLVMGlobalGetValueType(decl);
            if (LLVMIsFunctionVarArg(dft)) continue;
            const char *sig = w32adapt[i].sig;
            int nparams = (int)strlen(sig) - 1;
            if ((int)LLVMCountParamTypes(dft) != nparams)
                continue;
            size_t slots = nparams ? (size_t)nparams : 1;
            LLVMTypeRef *lps = zan_arena_alloc(g->arena, slots * sizeof(*lps));
            for (int p = 0; p < nparams; p++) {
                char c = sig[p + 1];
                lps[p] = (c == 'p') ? w_ptr : (c == 'j') ? w_i64 : w_i32;
            }
            char rc = sig[0];
            LLVMTypeRef lrt = (rc == 'v') ? LLVMVoidTypeInContext(g->ctx)
                              : (rc == 'p') ? w_ptr
                              : (rc == 'j') ? w_i64 : w_i32;
            LLVMTypeRef lft = LLVMFunctionType(lrt, lps, (unsigned)nparams, 0);
            char an[80];
            snprintf(an, sizeof(an), "__zan_w32ir_%s", w32adapt[i].name);
            LLVMSetValueName2(decl, an, strlen(an));
            LLVMValueRef real = LLVMAddFunction(g->mod, w32adapt[i].name, lft);
            LLVMTypeRef cts[8];
            LLVMValueRef cad[8];
            int ncts = 0;
            LLVMUseRef use = LLVMGetFirstUse(decl);
            while (use) {
                LLVMUseRef next = LLVMGetNextUse(use);
                LLVMValueRef user = LLVMGetUser(use);
                if ((LLVMIsACallInst(user) || LLVMIsAInvokeInst(user)) &&
                    LLVMGetCalledValue(user) == decl) {
                    LLVMTypeRef cft = LLVMGetCalledFunctionType(user);
                    if (cft == lft) {
                        LLVMSetOperand(user,
                                       LLVMGetNumOperands(user) - 1, real);
                    } else if (!LLVMIsFunctionVarArg(cft) &&
                               (int)LLVMCountParamTypes(cft) == nparams) {
                        LLVMValueRef ad = NULL;
                        for (int k = 0; k < ncts; k++) {
                            if (cts[k] == cft) { ad = cad[k]; break; }
                        }
                        if (!ad) {
                            char nm[96];
                            snprintf(nm, sizeof(nm), "%s.v%d", an, ncts);
                            ad = w32_build_adapter(g, nm, cft, real, lft);
                            if (ad && ncts < 8) {
                                cts[ncts] = cft;
                                cad[ncts] = ad;
                                ncts++;
                            }
                        }
                        if (ad)
                            LLVMSetOperand(user,
                                           LLVMGetNumOperands(user) - 1, ad);
                    }
                }
                use = next;
            }
            if (LLVMGetFirstUse(decl) && dft != lft) {
                w32_build_adapter_into(g, decl, real, lft);
                LLVMSetLinkage(decl, LLVMInternalLinkage);
            }
        }
    }
    if (g->target_is_ios) {
        LLVMValueRef mainf = LLVMGetNamedFunction(g->mod, "main");
        if (mainf && LLVMCountBasicBlocks(mainf) > 0) {
            LLVMSetValueName2(mainf, "zan_user_main", strlen("zan_user_main"));
        }
    }
    zan_irgen_init_llvm_targets();

    /* publish 模式需要按函数拆独立 section，否则链接器 --gc-sections
     * 无法丢弃未引用函数（section 粒度太粗，.text 里任何一个函数被引用
     * 就把整个 .text 留住）。
     * 全局数据不拆——Windows COFF 下成千上万个 .rdata$xxx section 会让
     * REL32 relocation 溢出；全局数据靠 COMDAT/ICF 合并即可。
     * 字符串混淆模式保留旧行为（全局数据也要拆）因为混淆需要精确控制。
     * macOS/iOS/WASM 暂跳过——macOS 链接器默认按 atom 粒度做 dead_strip，
     * iOS 有 App Store 工具链，WASM 靠 wasm-ld 自带 GC。 */
    bool need_fn_sec = (g->obfuscate_strings || g->publish_mode)
                       && !g->target_is_macos && !g->target_is_ios
                       && !g->target_is_wasm;
    bool need_gv_sec = g->obfuscate_strings
                       && !g->target_is_macos && !g->target_is_ios
                       && !g->target_is_wasm;
    if (need_fn_sec) {
        for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
             fn = LLVMGetNextFunction(fn)) {
            if (LLVMIsDeclaration(fn) || LLVMGetSection(fn)) continue;
            size_t nlen = 0;
            const char *nm = LLVMGetValueName2(fn, &nlen);
            if (!nm || !nlen || nlen > 200) continue;
            char sec[256];
            snprintf(sec, sizeof(sec), ".text.%s", nm);
            LLVMSetSection(fn, sec);
        }
    }
    if (need_gv_sec) {
        int coff_obj = g->target_triple[0]
                           ? strstr(g->target_triple, "windows-gnu") != NULL
                           :
#ifdef _WIN32
                           1;
#else
                           0;
#endif
        const char *dsep = coff_obj ? "$" : ".";
        for (LLVMValueRef gv = LLVMGetFirstGlobal(g->mod); gv;
             gv = LLVMGetNextGlobal(gv)) {
            if (LLVMGetSection(gv)) continue;
            if (!LLVMGetInitializer(gv)) continue;
            if (LLVMIsThreadLocal(gv)) continue;
            size_t nlen = 0;
            const char *nm = LLVMGetValueName2(gv, &nlen);
            if (!nm || !nlen || nlen > 200) continue;
            if (strncmp(nm, "llvm.", 5) == 0) continue;
            if (strncmp(nm, "rterr", 5) == 0) continue;
            char sec[260];
            snprintf(sec, sizeof(sec), "%s%s%s",
                     LLVMIsGlobalConstant(gv) ? ".rdata" : ".data",
                     dsep, nm);
            LLVMSetSection(gv, sec);
        }
    }

    LLVMTargetMachineRef tm;
    if (zan_bind_target_layout(g, &tm) != ZAN_OK) return ZAN_ERROR;

    char *error = NULL;
    if (LLVMTargetMachineEmitToFile(tm, g->mod, (char *)path,
                                     LLVMObjectFile, &error)) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0), 3092,
                      "failed to emit object file: %s", error);
        LLVMDisposeMessage(error);
        LLVMDisposeTargetMachine(tm);
        return ZAN_ERROR;
    }

    LLVMDisposeTargetMachine(tm);
    return ZAN_OK;
}
