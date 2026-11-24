static bool reflected_method_has_derived_owner(zan_irgen_t *g, zan_symbol_t *owner,
                                               zan_ast_node_t *member) {
    if (!g->refl_used || !owner || member->kind != AST_METHOD_DECL ||
        (member->method_decl.modifiers & MOD_STATIC))
        return false;
    /* Method tables include inherited instance hooks. Their declaring type need
     * not have its own metadata or allocation site before late thunk emission. */
    for (int m = 0; m < g->refl_mtab_count; m++) {
        zan_symbol_t *sym = g->refl_mtabs[m].sym;
        if (!refl_owner_can_have_instance(g, sym)) continue;
        zan_type_t *base = sym && sym->type ? sym->type->base_type : NULL;
        for (int depth = 0; base && base->sym && depth < 32; base = base->base_type, depth++) {
            if (base->sym == owner) return true;
        }
    }
    return false;
}

static bool extern_type_can_supply_instances(zan_irgen_t *g, zan_type_t *type, int depth) {
    if (!type || depth >= 32) return true;
    switch (type->kind) {
    case TYPE_CLASS:
    case TYPE_INTERFACE:
    case TYPE_OBJECT:
    case TYPE_TYPE_PARAM:
    case TYPE_TASK:
    case TYPE_ERROR:
        return true;
    case TYPE_DELEGATE: {
        /* Native delegates with arguments may invoke nested callbacks that
         * supply objects. Parameterless Action has no such object channel. */
        if (type->delegate_param_count > 0) return true;
        zan_type_t *result = type->type_arg_count
                                 ? subst_type_param_deep(g, type->delegate_ret_type, type)
                                 : type->delegate_ret_type;
        return extern_type_can_supply_instances(g, result, depth + 1);
    }
    case TYPE_ARRAY:
    case TYPE_NULLABLE:
        return extern_type_can_supply_instances(g, type->element_type, depth + 1);
    case TYPE_STRUCT:
        if (!type->sym) return true;
        for (int i = 0; i < type->sym->member_count; i++) {
            zan_symbol_t *member = type->sym->members[i];
            if (!member || (member->modifiers & MOD_STATIC) ||
                (member->kind != SYM_FIELD && member->kind != SYM_PROPERTY))
                continue;
            zan_type_t *field =
                type->type_arg_count ? subst_type_param_deep(g, member->type, type) : member->type;
            if (extern_type_can_supply_instances(g, field, depth + 1)) return true;
        }
        return false;
    default:
        return false;
    }
}

static bool extern_can_supply_instances(zan_irgen_t *g) {
    for (int i = 0; i < g->function_count; i++) {
        zan_symbol_t *sym = g->functions[i].sym;
        if (!sym_declares_extern(sym)) continue;
        zan_ast_node_t *decl = sym->decl;
        zan_type_t *ret = decl->method_decl.return_type
                              ? zan_binder_resolve_type(g->binder, decl->method_decl.return_type)
                              : NULL;
        if (decl->method_decl.return_type && extern_type_can_supply_instances(g, ret, 0)) {
            zan_compile_trace("publish instance fallback: extern %.*s result kind=%d type=%.*s",
                              (int)sym->name.len, sym->name.str, ret ? (int)ret->kind : -1,
                              ret ? (int)ret->name.len : 0, ret ? ret->name.str : "");
            return true;
        }
        for (int p = 0; p < decl->method_decl.params.count; p++) {
            zan_ast_node_t *param = decl->method_decl.params.items[p];
            /* Native code can also fill reference containers or invoke a
             * callback passed by value with objects allocated outside Zan. */
            zan_type_t *type = zan_binder_resolve_type(g->binder, param->param.type);
            if (extern_type_can_supply_instances(g, type, 0)) {
                zan_compile_trace(
                    "publish instance fallback: extern %.*s argument kind=%d type=%.*s",
                    (int)sym->name.len, sym->name.str, type ? (int)type->kind : -1,
                    type ? (int)type->name.len : 0, type ? type->name.str : "");
                return true;
            }
        }
    }
    return false;
}

zan_status_t zan_irgen_emit(zan_irgen_t *g, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return ZAN_ERROR;
    g_di_emit_ctx = g;
    /* Ordinary (non -g) executable builds follow publish's closed-world
     * rules, so reflection that works in development also works after
     * --publish. They used to emit every body of every pulled-in file and a
     * reflection thunk for every method: a small Gui test was 3.4M LLVM
     * instructions, 90 s and 4 GB. ZAN_NO_DEV_PRUNE=1 restores full emission. */
    const char *no_dev_prune = getenv("ZAN_NO_DEV_PRUNE");
    bool dev_prune = !(no_dev_prune && strcmp(no_dev_prune, "1") == 0);
    g->closed_world = g->publish_mode || (dev_prune && !g->emit_debug && !g->emit_lib);
    const char *no_instance_prune = getenv("ZAN_NO_PUBLISH_INSTANCE_PRUNE");
    g->publish_instance_prune = g->closed_world && !g->emit_debug && !g->emit_lib &&
                                !(no_instance_prune && strcmp(no_instance_prune, "1") == 0);
    const char *no_reflect_demand = getenv("ZAN_NO_PUBLISH_REFLECT_DEMAND");
    g->publish_reflect_demand = g->closed_world && !g->emit_debug && !g->emit_lib &&
                                !(no_reflect_demand && strcmp(no_reflect_demand, "1") == 0);
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
    /* Imported managed results and ref/out slots can supply instances without a
     * local allocation site. Native handles and scalar returns do not do so. */
    if (g->publish_instance_prune && extern_can_supply_instances(g))
        g->publish_instance_prune = false;
    const char *no_vslot_demand = getenv("ZAN_NO_PUBLISH_VSLOT_DEMAND");
    g->publish_vslot_demand = g->publish_instance_prune &&
        !(no_vslot_demand && strcmp(no_vslot_demand, "1") == 0);

    bool prune_stdlib_bodies = (g->closed_world || g->obfuscate_strings) && !g->emit_debug;
    /* Non-publish builds still generate unreached bodies once, for their
     * codegen diagnostics, then drop them before LLVM optimizes. */
    bool diag_unused_bodies = !g->publish_mode && !g->obfuscate_strings;
    unsigned char *live = (unsigned char *)calloc((size_t)work_count + 1, 1);
    for (int w = 0; w < work_count; w++) {
        zan_ast_node_t *m = work[w].member;
        if (sinit_is_cctor(m)) continue;
        zan_symbol_t *owner = work[w].type_sym;
        bool is_entry = m->kind == AST_METHOD_DECL &&
            (m->method_decl.modifiers & MOD_STATIC) &&
            owner && owner->decl && owner->decl->type_decl.type_params.count == 0 &&
            ((m->method_decl.name.len == 4 &&
              memcmp(m->method_decl.name.str, "Main", 4) == 0) ||
             (m->method_decl.name.len == 12 &&
              memcmp(m->method_decl.name.str, "__DesignMain", 12) == 0));
        bool is_lib_export = g->emit_lib &&
            ((m->method_decl.modifiers & MOD_PUBLIC) || m->kind == AST_CONSTRUCTOR_DECL);
        bool has_attrs = m->meta && m->meta->attributes.count > 0;
        if (is_entry || is_lib_export || has_attrs) live[w] = 1;
    }
    emit_demanded_method_bodies(g, work, work_count, live);
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

    emit_vtables(g);
    work_fn_index_t work_ix = { .g = g };
    work_fn_index_build(&work_ix, work, work_count);
    int pending;
    do {
        pending = 0;
        bool ctor_used_before = g->refl_ctor_used;
        const bool site_meta_before = refl_needs_site_meta(g);
        const bool invoke_unknown_before = g->refl_invoke_unknown;
        const int invoke_names_before = g->refl_invoke_name_count;
        const unsigned query_used_before = g->refl_query_used;
        const int vslots_before = g->vslot_demand_count;
        int metas_before = g->refl_meta_count;
        int mtabs_before = g->refl_mtab_count;
        int sites_before = g->leak_site_count;
        /* Warm metadata and demanded ctor thunks before rooting bodies. Their
         * implicit field initializers can discover further reflected types and
         * callable dependencies; no dead-body stubs or site tables exist yet. */
        refl_reserve_thunk_sites(g);
        emit_pending_method_specs(g);
        if (zan_diag_has_errors(g->diag)) break;
        if (g->publish_vslot_demand) emit_vtables(g);
        sinit_exec_graph_t static_graph;
        sinit_graph_build(&static_graph, g, &work_ix);
        pending += emit_pending_static_initializers(g, unit, work, work_count,
                                                    live, &static_graph);
        if (zan_diag_has_errors(g->diag)) {
            sinit_graph_dispose(&static_graph);
            break;
        }
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
                    if (!is_refl_root && refl_needs_site_meta(g) && g->site_syms) {
                        for (int m = 0; m < g->leak_site_count; m++) {
                            if (g->site_syms[m] == tsym) {
                                is_refl_root = true;
                                break;
                            }
                        }
                    }
                }
            if (!is_refl_root)
                is_refl_root = reflected_method_has_derived_owner(g, work[w].type_sym, member);
            /* Metadata/thunk retention is not executable type demand. Explicit
             * CreateInstance/Invoke and established annotated/HTML callbacks
             * are callable roots; cctors are reached only from activated init. */
            if (sinit_is_cctor(member)) {
                is_refl_root = false;
            } else if (is_refl_root && member->kind == AST_METHOD_DECL) {
                is_refl_root = refl_method_needs_thunk(g, work[w].type_sym, member) &&
                    static_init_body_has_live_use(&static_graph, work[w].fn);
            } else if (is_refl_root && member->kind == AST_CONSTRUCTOR_DECL) {
                is_refl_root = g->emit_lib || g->static_init_ctor_demand;
            }
            bool root = !sinit_is_cctor(member) &&
                        ((g->emit_lib &&
                          ((member->method_decl.modifiers & MOD_PUBLIC) ||
                           member->kind == AST_CONSTRUCTOR_DECL)) ||
                         is_refl_root ||
                         (member->kind == AST_METHOD_DECL &&
                          refl_is_exact_html_handler_name(g,
                              (const char *)member->method_decl.name.str,
                              (size_t)member->method_decl.name.len)));
            if (!root)
                root = body_has_live_use(work[w].fn, live, &work_ix) &&
                       static_init_body_has_live_use(&static_graph, work[w].fn);
            if (root) { live[w] = 1; pending++; }
        }
        sinit_graph_dispose(&static_graph);
        if (pending) {
            emit_demanded_method_bodies(g, work, work_count, live);
            emit_pending_method_specs(g);
            if (zan_diag_has_errors(g->diag)) break;
        }
        /* Specializations can emit CreateInstance, enable dynamic object
         * reflection or discover metadata after the prewarm, even if no ordinary
         * body became live. Reach the fixed point before freezing dead stubs. */
        if (ctor_used_before != g->refl_ctor_used ||
            site_meta_before != refl_needs_site_meta(g) ||
            invoke_unknown_before != g->refl_invoke_unknown ||
            invoke_names_before != g->refl_invoke_name_count ||
            query_used_before != g->refl_query_used ||
            vslots_before != g->vslot_demand_count ||
            metas_before != g->refl_meta_count ||
            mtabs_before != g->refl_mtab_count ||
            sites_before != g->leak_site_count) {
            pending++;
        }
    } while (pending);
    if (!zan_diag_has_errors(g->diag)) {
        finalize_static_initializers(g, unit, work, work_count, live, &work_ix);
    }
    free(work_ix.slots);
    /* Preserve ordinary/debug codegen diagnostics after freezing executable
     * initialization demand. These bodies cannot add startup roots. */
    if (diag_unused_bodies && !zan_diag_has_errors(g->diag)) {
        unsigned char *diagnostic_live = (unsigned char *)calloc((size_t)work_count + 1, 1);
        for (int w = 0; w < work_count; w++) {
            if (live[w]) diagnostic_live[w] = 2;
            else if (!sinit_is_cctor(work[w].member)) diagnostic_live[w] = 1;
        }
        emit_demanded_method_bodies(g, work, work_count, diagnostic_live);
        emit_pending_method_specs(g);
        for (int w = 0; w < work_count; w++) {
            if (live[w]) continue;
            if (prune_stdlib_bodies) {
                /* Root-reachable set is fixed above; this body (and whatever
                 * it demanded) existed only for its diagnostics. The dead-stub
                 * loop below gives it an unreachable body, as publish does. */
                zan_llvm_delete_body(work[w].fn);
                if (work[w].resume_fn) zan_llvm_delete_body(work[w].resume_fn);
                /* emitting the body already released param_types */
                if (diagnostic_live[w] == 2) work[w].param_types = NULL;
            } else if (diagnostic_live[w] == 2) {
                live[w] = 3;
            }
        }
        free(diagnostic_live);
    }
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
    sinit_register_library_startup(g, get_static_init_coordinator(g));
    refl_compact_member_tables(g);
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
    if (zan_opt_validate_cpu(g, g->mod) != ZAN_OK ||
        zan_opt_verify_module(g->mod, "before IR output") != ZAN_OK) return ZAN_ERROR;
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
        if (g->cpu_profile && strcmp(g->cpu_profile, "baseline") == 0)
            tm_features = "+sse2,-sse3,-ssse3,-sse4.1,-sse4.2,-crc32,-aes,-avx,-avx2,-fma,-bmi,-bmi2,-popcnt";
        else if (g->cpu_profile && strcmp(g->cpu_profile, "avx2") == 0)
            tm_features = "+sse3,+ssse3,+sse4.1,+sse4.2,+crc32,+aes,+avx,+avx2,+fma,+bmi";
        else
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

/* Apple --fast-alloc: point every allocation call this module makes at the
 * small-object allocator. Runs on the main and every shard module right
 * before object emission, i.e. after the LLVM pipeline has had its look at
 * the libc names (heap-to-stack, malloc/free pair elision). Only bodiless
 * declarations are renamed; rt_mem.c's zone makes any pointer that still
 * crosses to libc (C runtime, drivers, cached objects) safe either way. */
static void zan_irgen_route_fast_alloc(zan_irgen_t *g, LLVMModuleRef mod) {
    if (!g->route_fast_alloc) return;
    static const char *const names[][2] = {
        { "malloc", "zan_mem_malloc" }, { "free", "zan_mem_free" },
        { "calloc", "zan_mem_calloc" }, { "realloc", "zan_mem_realloc" },
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        LLVMValueRef fn = LLVMGetNamedFunction(mod, names[i][0]);
        if (!fn || !LLVMIsDeclaration(fn)) continue;
        LLVMValueRef have = LLVMGetNamedFunction(mod, names[i][1]);
        if (have) {
            /* Both spellings declared (a DllImport of zan_mem_*): fold. */
            LLVMReplaceAllUsesWith(fn, have);
            LLVMDeleteFunction(fn);
            continue;
        }
        LLVMSetValueName2(fn, names[i][1], strlen(names[i][1]));
    }
}

void zan_irgen_prepare_object_sections(zan_irgen_t *g, LLVMModuleRef mod) {
    zan_irgen_route_fast_alloc(g, mod);
    /* Main and shard objects need the same GC granularity. COFF '$' suffixes
     * merge into ordinary output sections. Preserve special runtime sections,
     * TLS and weak/ODR groups when making strong definitions discardable. */
    bool need_fn_sec = (g->obfuscate_strings || g->publish_mode) && !g->target_is_macos &&
                       !g->target_is_ios && !g->target_is_wasm;
    bool coff_publish_data = g->publish_mode && g->target_is_windows;
    bool need_gv_sec = (g->obfuscate_strings || coff_publish_data) && !g->target_is_macos &&
                       !g->target_is_ios && !g->target_is_wasm;
    if (need_fn_sec) {
        for (LLVMValueRef fn = LLVMGetFirstFunction(mod); fn; fn = LLVMGetNextFunction(fn)) {
            if (LLVMIsDeclaration(fn) || LLVMGetSection(fn)) continue;
            size_t nlen = 0;
            const char *nm = LLVMGetValueName2(fn, &nlen);
            if (!nm || !nlen || nlen > 200) continue;
            LLVMLinkage linkage = LLVMGetLinkage(fn);
            if (g->target_is_windows && !LLVMGetComdat(fn) &&
                (linkage == LLVMExternalLinkage || linkage == LLVMInternalLinkage ||
                 linkage == LLVMPrivateLinkage)) {
                /* COFF GC needs COMDAT and '$' names for MinGW unwind sections.
                 * NODUPLICATES preserves strong-definition errors. Keep weak
                 * fallbacks ungrouped so the native runtime can override them.
                 * Explicit COFF sections need internal rather than private keys. */
                if (LLVMGetLinkage(fn) == LLVMPrivateLinkage)
                    LLVMSetLinkage(fn, LLVMInternalLinkage);
                LLVMComdatRef group = LLVMGetOrInsertComdat(mod, nm);
                LLVMSetComdatSelectionKind(group, LLVMNoDeduplicateComdatSelectionKind);
                LLVMSetComdat(fn, group);
            }
            char sec[256];
            snprintf(sec, sizeof(sec), ".text%s%s", g->target_is_windows ? "$" : ".", nm);
            LLVMSetSection(fn, sec);
        }
    }
    if (need_gv_sec) {
        int coff_obj = g->target_triple[0] ? strstr(g->target_triple, "windows-gnu") != NULL :
#ifdef _WIN32
                                           1;
#else
                                           0;
#endif
        const char *dsep = coff_obj ? "$" : ".";
        for (LLVMValueRef gv = LLVMGetFirstGlobal(mod); gv; gv = LLVMGetNextGlobal(gv)) {
            if (LLVMGetSection(gv)) continue;
            if (!LLVMGetInitializer(gv)) continue;
            if (LLVMIsThreadLocal(gv)) continue;
            size_t nlen = 0;
            const char *nm = LLVMGetValueName2(gv, &nlen);
            if (!nm || !nlen || nlen > 200) continue;
            if (strncmp(nm, "llvm.", 5) == 0) continue;
            if (strncmp(nm, "rterr", 5) == 0) continue;
            if (coff_publish_data) {
                LLVMLinkage linkage = LLVMGetLinkage(gv);
                if (LLVMGetComdat(gv) ||
                    (linkage != LLVMExternalLinkage && linkage != LLVMInternalLinkage &&
                     linkage != LLVMPrivateLinkage))
                    continue;
                if (linkage == LLVMPrivateLinkage) LLVMSetLinkage(gv, LLVMInternalLinkage);
                LLVMComdatRef group = LLVMGetOrInsertComdat(mod, nm);
                LLVMSetComdatSelectionKind(group, LLVMNoDeduplicateComdatSelectionKind);
                LLVMSetComdat(gv, group);
            }
            char sec[260];
            snprintf(sec, sizeof(sec), "%s%s%s", LLVMIsGlobalConstant(gv) ? ".rdata" : ".data",
                     dsep, nm);
            LLVMSetSection(gv, sec);
        }
    }
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

    zan_irgen_prepare_object_sections(g, g->mod);

    LLVMTargetMachineRef tm;
    if (zan_bind_target_layout(g, &tm) != ZAN_OK) return ZAN_ERROR;

    if (zan_opt_validate_cpu(g, g->mod) != ZAN_OK ||
        zan_opt_verify_module(g->mod, "before object output") != ZAN_OK) {
        LLVMDisposeTargetMachine(tm);
        return ZAN_ERROR;
    }
    char *error = NULL;
    if (LLVMTargetMachineEmitToFile(tm, g->mod, (char *)path,
                                     LLVMObjectFile, &error)) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0), 3092,
                      "failed to emit object file: %s", error);
        remove(path); /* the backend may have created a partial object */
        LLVMDisposeMessage(error);
        LLVMDisposeTargetMachine(tm);
        return ZAN_ERROR;
    }

    LLVMDisposeTargetMachine(tm);
    return ZAN_OK;
}
