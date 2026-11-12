static uint64_t work_fn_hash(LLVMValueRef fn) {
    uint64_t h = (uint64_t)(uintptr_t)fn;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 29;
    return h;
}

static void work_fn_index_build(work_fn_index_t *ix,
                                method_body_work_t *work, int work_count) {
    int cap = 16;
    while (cap < work_count * 4) cap *= 2;
    ix->slots = (work_fn_slot_t *)calloc((size_t)cap, sizeof(*ix->slots));
    if (!ix->slots) return;
    ix->cap = cap;
    ix->work = work;
    ix->work_count = work_count;
    for (int w = 0; w < work_count; w++) {
        for (int which = 0; which < 2; which++) {
            LLVMValueRef key = which == 0 ? work[w].fn : work[w].resume_fn;
            if (!key) continue;
            size_t j = (size_t)work_fn_hash(key) & (size_t)(cap - 1);
            while (ix->slots[j].key && ix->slots[j].key != key)
                j = (j + 1) & (size_t)(cap - 1);
            if (!ix->slots[j].key) {
                ix->slots[j].key = key;
                ix->slots[j].idx = w;
            }
        }
    }
}

static int work_fn_index_lookup(const work_fn_index_t *ix, LLVMValueRef key) {
    if (!ix->cap) return -1;
    size_t j = (size_t)work_fn_hash(key) & (size_t)(ix->cap - 1);
    while (ix->slots[j].key) {
        if (ix->slots[j].key == key) return ix->slots[j].idx;
        j = (j + 1) & (size_t)(ix->cap - 1);
    }
    return -1;
}

static LLVMValueRef find_owning_global(LLVMValueRef val, int depth) {
    if (!val || depth > 4) return NULL;
    if (LLVMIsAGlobalVariable(val)) return val;
    for (LLVMUseRef u = LLVMGetFirstUse(val); u; u = LLVMGetNextUse(u)) {
        LLVMValueRef user = LLVMGetUser(u);
        if (LLVMIsAGlobalVariable(user)) return user;
        if (LLVMIsAConstant(user)) {
            LLVMValueRef gv = find_owning_global(user, depth + 1);
            if (gv) return gv;
        }
    }
    return NULL;
}

static bool class_ctor_is_live(const work_fn_index_t *ix, const char *cname,
                               const unsigned char *live) {
    if (!ix || !ix->work || !cname) return false;
    size_t clen = strlen(cname);
    for (int w = 0; w < ix->work_count; w++) {
        zan_symbol_t *sym = ix->work[w].type_sym;
        if (sym && sym->name.len == clen &&
            memcmp(sym->name.str, cname, clen) == 0 &&
            ix->work[w].member->kind == AST_CONSTRUCTOR_DECL &&
            !(ix->work[w].member->method_decl.modifiers & MOD_STATIC)) {
            if (live[w] >= 1) return true;
        }
    }
    return false;
}

static bool vtable_has_live_use(LLVMValueRef vtg, const unsigned char *live,
                                const work_fn_index_t *ix, int depth) {
    if (!vtg || depth > 4) return false;
    for (LLVMUseRef u = LLVMGetFirstUse(vtg); u; u = LLVMGetNextUse(u)) {
        LLVMValueRef user = LLVMGetUser(u);
        if (LLVMIsAInstruction(user)) {
            LLVMValueRef parent = LLVMGetBasicBlockParent(LLVMGetInstructionParent(user));
            if (!parent) return true;
            int i = work_fn_index_lookup(ix, parent);
            if (i < 0 || live[i] == 2) return true;
        } else if (LLVMIsAConstantExpr(user)) {
            if (vtable_has_live_use(user, live, ix, depth + 1))
                return true;
        }
    }
    return false;
}

static bool body_has_live_use(LLVMValueRef fn, const unsigned char *live,
                              const work_fn_index_t *ix) {
    for (LLVMUseRef u = LLVMGetFirstUse(fn); u; u = LLVMGetNextUse(u)) {
        LLVMValueRef user = LLVMGetUser(u);
        if (LLVMIsAConstantExpr(user) || LLVMIsAConstantArray(user) ||
            LLVMIsAConstantStruct(user)) {
            LLVMValueRef gv = find_owning_global(user, 0);
            if (gv && LLVMIsAGlobalVariable(gv)) {
                const char *gname = LLVMGetValueName(gv);
                if (gname && strncmp(gname, "__zan_vtable_", 13) == 0) {
                    const char *cname = gname + 13;
                    if (class_ctor_is_live(ix, cname, live) ||
                        vtable_has_live_use(gv, live, ix, 0)) {
                        return true;
                    }
                    continue;
                }
            }
            return true;
        }
        LLVMValueRef parent = LLVMIsAInstruction(user)
            ? LLVMGetBasicBlockParent(LLVMGetInstructionParent(user)) : NULL;
        if (!parent) return true;
        int i = work_fn_index_lookup(ix, parent);
        if (i < 0 || live[i] == 2) return true;
    }
    return false;
}

static void emit_user_method_bodies(zan_irgen_t *g, method_body_work_t *work,
                                    int work_count, unsigned char *live) {
    for (int w = 0; w < work_count; w++) {
        if (live[w] != 1) continue;
        live[w] = 2;
        zan_ast_node_t *member = work[w].member;
        zan_symbol_t *type_sym = work[w].type_sym;
        zan_compile_trace("emit %.*s.%.*s",
                        type_sym ? (int)type_sym->name.len : 1,
                        type_sym ? type_sym->name.str : "?",
                        (int)member->method_decl.name.len,
                        member->method_decl.name.str);
        LLVMValueRef fn = work[w].fn;
        LLVMTypeRef *param_types = work[w].param_types;
        int param_count = work[w].param_count;
        int param_offset = work[w].param_offset;
        bool is_static = work[w].is_static;
        LLVMTypeRef llvm_ret = work[w].llvm_ret;
        zan_type_t *ret_type = work[w].ret_type;
        LLVMValueRef this_alloca = NULL;
        g->cur_inst = work[w].cur_inst;

        if (work[w].is_async) {
            emit_async_method_ir(g, &work[w]);
            continue;
        }

        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, entry);
        di_set_loc(g, member->loc);

        local_scope_t *locals = local_scope_new(g->arena);

        if (!is_static) {
            this_alloca = LLVMBuildAlloca(g->builder, param_types[0], "this");
            LLVMBuildStore(g->builder, LLVMGetParam(fn, 0), this_alloca);
        }

        for (int k = 0; k < param_count; k++) {
            zan_ast_node_t *param = member->method_decl.params.items[k];
            zan_type_t *pt = zan_binder_resolve_type(g->binder, param->param.type);
            if (g->cur_inst) pt = subst_type_param_deep(g, pt, g->cur_inst);
            if (param->param.by_ref) {
                local_add(locals, param->param.name,
                          LLVMGetParam(fn, (unsigned)(k + param_offset)), pt);
                if (pt && pt->kind == TYPE_STRING)
                    locals->vars[locals->count - 1].opaque_string = 1;
                if (is_rc_managed_type(pt))
                    locals->vars[locals->count - 1].byref_slot =
                        locals->vars[locals->count - 1].arc_owned = 1;
                box_captured_parameter(g, locals, param, pt,
                                       param_types[k + param_offset],
                                       LLVMGetParam(fn, (unsigned)(k + param_offset)),
                                       member->method_decl.body);
                continue;
            }
            LLVMValueRef pv = LLVMGetParam(fn, (unsigned)(k + param_offset));
            LLVMValueRef param_alloca = LLVMBuildAlloca(g->builder, param_types[k + param_offset], "p");
            LLVMBuildStore(g->builder, pv, param_alloca);
            local_add(locals, param->param.name, param_alloca, pt);
            if (pt && pt->kind == TYPE_STRING)
                locals->vars[locals->count - 1].opaque_string = 1;
            box_captured_parameter(g, locals, param, pt,
                                   param_types[k + param_offset], pv,
                                   member->method_decl.body);
            if (!locals->vars[locals->count - 1].box_cell && pt &&
                pt->kind == TYPE_STRUCT &&
                LLVMGetTypeKind(param_types[k + param_offset]) ==
                    LLVMStructTypeKind &&
                type_contains_collection_rc(g, pt, 0)) {
                locals->vars[locals->count - 1].struct_rc = 1;
                emit_struct_local_retain(g, pt, param_alloca);
            }
            own_written_param(g, locals, param, pt,
                              param_types[k + param_offset], pv,
                              member->method_decl.body);
        }

        LLVMValueRef saved_fn = g->current_fn;
        LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
        zan_type_t *saved_fn_zan_ret = g->current_fn_zan_ret_type;
        LLVMValueRef saved_this = g->current_this;
        zan_symbol_t *saved_type_sym = g->current_type_sym;
        zan_ast_node_t *saved_fn_body = g->current_fn_body;
        g->current_fn = fn;
        g->current_fn_ret_type = llvm_ret;
        g->current_fn_zan_ret_type = ret_type;
        int saved_throw_base = g->throw_locals_base;
        int saved_catch_cc = g->catch_cleanup_count;
        int saved_throw_cb = g->throw_catch_base;
        int saved_fin_c = g->finally_count;
        zan_irgen_pending_context_t saved_pending = g->pending;
        int saved_fin_lb = g->finally_loop_base;
        int saved_eh_c = g->eh_armed_count;
        int saved_eh_b = g->eh_armed_base;
        int saved_eh_lb = g->eh_armed_loop_base;
        int saved_lpad_depth = g->lpad_depth;
        LLVMBasicBlockRef fn_cleanup_bb = NULL;
        LLVMValueRef fn_tmp_mark = NULL;
        if (!g->current_async_frame && g->lpad_depth < ZAN_MAX_ARMED_TRY) {
            fn_cleanup_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.lpad");
            g->lpad_stack[g->lpad_depth].fn = fn;
            g->lpad_stack[g->lpad_depth].lpad_bb = fn_cleanup_bb;
            g->lpad_stack[g->lpad_depth].catch_bb = NULL;
            g->lpad_stack[g->lpad_depth].throw_locals_base = 0;
            g->lpad_stack[g->lpad_depth].fin_idx = -1;
            g->lpad_stack[g->lpad_depth].finally_depth = 0;
            g->lpad_stack[g->lpad_depth].cleanups = NULL;
            g->lpad_stack[g->lpad_depth].cleanup_count = 0;
            g->lpad_stack[g->lpad_depth].cleanup_cap = 0;
            g->lpad_depth++;
            for (int pi = 0; pi < locals->count; pi++) {
                if (local_needs_cleanup(&locals->vars[pi]))
                    lpad_register_local(g, &locals->vars[pi]);
            }
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            fn_tmp_mark = emit_entry_alloca(g, i32t, "fn.tmp.mark");
            zan_store_fit(g, LLVMBuildLoad2(g->builder, i32t, get_eh_tmp_top_global(g), "fn.tmp.top0"), fn_tmp_mark);
        }
        g->throw_locals_base = 0;
        g->catch_cleanup_count = 0;
        g->throw_catch_base = 0;
        g->finally_count = 0;
        g->pending = (zan_irgen_pending_context_t){0};
        g->finally_loop_base = 0;
        g->eh_armed_base = g->eh_armed_count;
        g->eh_armed_loop_base = g->eh_armed_count;
        g->current_this = is_static ? NULL : this_alloca;
        g->current_type_sym = type_sym;
        g->current_fn_body = member->method_decl.body;
        g->current_fn_is_ctor = member->kind == AST_CONSTRUCTOR_DECL;
        g->current_fn_no_runtime = zan_ast_has_attr(member, "NoRuntime");

        if (member->kind == AST_CONSTRUCTOR_DECL && !is_static) {
            /* A528：结构体构造函数体内写拥有型字段后抛异常，字段引用没人
             * 释放（类路径靠对象析构级联，结构体没有析构）。入口处裸武装
             * 一个 EH 标记：字段写（emit_rc_store_field 的 ctor 分支）把
             * 字段槽推上 EH 临时栈，正常返回走既有 disarm（免释放回退，
             * 所有权交给调用方），抛出走 unwind 按字段类型释放。 */
            if (type_sym->type && type_sym->type->kind == TYPE_STRUCT &&
                !g->current_async_frame &&
                ZAN_TAB_ENSURE(g->eh_armed, g->eh_armed_count, g->eh_armed_cap, 16)) {
                LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                LLVMValueRef tt = get_eh_tmp_top_global(g);
                LLVMValueRef base = LLVMBuildLoad2(g->builder, i32t, tt,
                    "ctor.eh.base");
                LLVMValueRef base_slot = emit_entry_alloca(g, i32t,
                    "ctor.eh.base.slot");
                zan_store_fit(g, base, base_slot);
                g->eh_armed[g->eh_armed_count].old_top_slot = base_slot;
                g->eh_armed[g->eh_armed_count].restore_tmps = true;
                g->eh_armed_count++;
            }
            bool this_init = member->method_decl.has_this_init;
            bool initializer_target_called = false;
            zan_symbol_t *target_sym = NULL;
            if (this_init) {
                target_sym = type_sym;
            } else if (type_sym->type && type_sym->type->base_type &&
                       type_sym->type->base_type->sym) {
                target_sym = type_sym->type->base_type->sym;
            }
            /* C# 顺序（A516）：派生字段初始化器先于基类构造链——
             * 基类构造器里的虚调用因此能读到派生内联字段（C# 42，曾 0）。
             * this 链（: this(...)）不在此发：由链目标那个构造器统一发。 */
            if (!this_init) {
                LLVMValueRef thisv_f = LLVMBuildLoad2(
                    g->builder, param_types[0], this_alloca, "this.fields");
                zan_type_t *recv_type_f =
                    g->cur_inst ? g->cur_inst : type_sym->type;
                emit_decl_field_initializers(
                    g, type_sym, recv_type_f, thisv_f, locals);
            }
            if (target_sym) {
                zan_ast_list_t *init_args = zan_ast_method_base_args(member);
                struct zan_ctor_entry *target = find_ctor(
                    g, target_sym, init_args, locals, this_init ? member : NULL);
                zan_ast_list_t init_args_filled;
                if (!target &&
                    fill_ctor_default_args(g, target_sym, init_args,
                                           &init_args_filled)) {
                    struct zan_ctor_entry *dt = find_ctor(
                        g, target_sym, &init_args_filled, locals,
                        this_init ? member : NULL);
                    if (dt) {
                        target = dt;
                        init_args = &init_args_filled;
                    }
                }
                LLVMValueRef target_fn = target ? target->fn : NULL;
                LLVMTypeRef target_fn_type = target ? target->fn_type : NULL;
                if (target && g->cur_inst && target_sym == type_sym) {
                    LLVMValueRef specialized = find_generic_ctor(
                        g, target_sym, target->decl, g->cur_inst->type_args,
                        g->cur_inst->type_arg_count, &target_fn_type);
                    if (specialized) target_fn = specialized;
                }
                if (!target) {
                    if (this_init || member->method_decl.has_base_init ||
                        has_declared_ctor(g, target_sym)) {
                        zan_diag_emit_code(g->diag, DIAG_ERROR, member->loc, 3088,
                            "no matching %s constructor with %d argument(s)",
                            this_init ? "this" : "base", init_args->count);
                    }
                } else {
                    LLVMValueRef thisv = LLVMBuildLoad2(
                        g->builder, param_types[0], this_alloca,
                        this_init ? "this.chain" : "this.base");
                    if (!this_init) {
                        LLVMTypeRef bst = get_struct_llvm_type(g, target_sym);
                        if (bst)
                            thisv = LLVMBuildBitCast(g->builder, thisv,
                                LLVMPointerType(bst, 0), "base.this");
                    }
                    int argc = init_args->count + 1;
                    LLVMValueRef *cargs = (LLVMValueRef *)malloc(
                        sizeof(LLVMValueRef) * (size_t)argc);
                    cargs[0] = thisv;
                    for (int ai = 0; ai < init_args->count; ai++) {
                        zan_ast_node_t *param =
                            target->decl->method_decl.params.items[ai];
                        zan_type_t *pt = zan_binder_resolve_type(
                            g->binder, param->param.type);
                        cargs[ai + 1] = emit_arg_typed(
                            g, init_args->items[ai], pt, locals);
                    }
                    coerce_args_to_params(g, target_fn_type, cargs, argc);
                    zan_call2(g->builder, target_fn_type, target_fn,
                              cargs, (unsigned)argc, "");
                    initializer_target_called = true;
                    for (int ai = 0; ai < init_args->count; ai++)
                        emit_release_owned_call_temp(
                            g, init_args->items[ai], cargs[ai + 1], locals);
                    free(cargs);
                }
            }
            if (!this_init) {
                LLVMValueRef thisv = LLVMBuildLoad2(
                    g->builder, param_types[0], this_alloca, "this.fields");
                zan_type_t *base_type = type_sym->type
                    ? type_sym->type->base_type : NULL;
                if (base_type && base_type->sym && !initializer_target_called) {
                    /* 基类构造链没接上（没找到可调的基类构造器）时兜底发
                     * 基类字段初始化；正常路径基类构造器自己的 prologue 会发。 */
                    emit_implicit_field_initializers(
                        g, base_type->sym, base_type, thisv, locals);
                }
            }
        }

        if (member->method_decl.body->kind == AST_BLOCK) {
            for (int k = 0; k < member->method_decl.body->block.stmts.count; k++) {
                emit_stmt(g, member->method_decl.body->block.stmts.items[k], locals);
            }
        } else {
            check_implicit_narrowing(g, g->current_fn_zan_ret_type,
                infer_expr_type(g, member->method_decl.body, locals),
                member->method_decl.body, "return");
            LLVMValueRef val = emit_expr(g, member->method_decl.body, locals);
            LLVMTypeRef val_t = LLVMTypeOf(val);
            if (val_t != llvm_ret) {
                if (LLVMGetTypeKind(llvm_ret) == LLVMFloatTypeKind &&
                    LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                    val = LLVMBuildFPTrunc(g->builder, val, llvm_ret, "trunc");
                } else if (LLVMGetTypeKind(llvm_ret) == LLVMDoubleTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
                    val = LLVMBuildFPExt(g->builder, val, llvm_ret, "ext");
                }
            }
            if (is_rc_managed_type(ret_type) &&
                !expr_yields_owned_rc_value(g, member->method_decl.body, locals)) {
                emit_rc_retain_for_type(g, ret_type, val);
            }
            emit_release_owned_locals(g, locals);
            LLVMBuildRet(g->builder, val);
        }

        LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
        if (!LLVMGetBasicBlockTerminator(cur_bb)) {
            emit_release_owned_locals(g, locals);
            /* A528：隐式落到函数尾的返回与显式 return 同口径解除武装
             * （无 try/无裸武装时 eh_armed_count == base，是无害 no-op）。
             * 必须在局部变量 EH 出栈之后——disarm 只改栈顶指针，先做会把
             * 尚未出栈的局部槽位闷在栈里，随后 pop 反而下溢冲掉调用方帧。 */
            emit_eh_disarm_from(g, 0);
            if (ret_type->kind == TYPE_VOID) {
                LLVMBuildRetVoid(g->builder);
            } else {
                LLVMBuildRet(g->builder, LLVMConstNull(llvm_ret));
            }
        }

        g->lpad_depth = saved_lpad_depth;
        if (fn_cleanup_bb) {
            zan_lpad_entry_t *fn_entry = &g->lpad_stack[saved_lpad_depth];
            if (LLVMGetFirstUse(LLVMBasicBlockAsValue(fn_cleanup_bb)) != NULL) {
                zan_builder_point_t cl_pt = zan_builder_save(g);
                LLVMPositionBuilderAtEnd(g->builder, fn_cleanup_bb);
                ensure_function_personality(g, fn);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef lp_ret_ty = LLVMStructTypeInContext(g->ctx, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0);
                LLVMValueRef lp = LLVMBuildLandingPad(g->builder, lp_ret_ty, get_eh_personality_fn(g), 0, "fn.lp");
                LLVMSetCleanup(lp, 1);
                if (fn_tmp_mark) {
                    LLVMValueRef mark = LLVMBuildLoad2(g->builder, i32t, fn_tmp_mark, "fn.tmp.mark.val");
                    emit_eh_tmp_unwind_to(g, mark);
                }
                emit_lpad_cleanups(g, fn_entry);
                LLVMBuildResume(g->builder, lp);
                zan_builder_restore(g, cl_pt);
            } else {
                LLVMDeleteBasicBlock(fn_cleanup_bb);
            }
            free(fn_entry->cleanups);
            fn_entry->cleanups = NULL;
            fn_entry->cleanup_count = 0;
            fn_entry->cleanup_cap = 0;
        }

        g->current_fn = saved_fn;
        g->current_fn_ret_type = saved_fn_ret;
        g->current_fn_zan_ret_type = saved_fn_zan_ret;
        g->throw_locals_base = saved_throw_base;
        g->catch_cleanup_count = saved_catch_cc;
        g->throw_catch_base = saved_throw_cb;
        g->finally_count = saved_fin_c;
        g->pending = saved_pending;
        g->finally_loop_base = saved_fin_lb;
        g->eh_armed_count = saved_eh_c;
        g->eh_armed_base = saved_eh_b;
        g->eh_armed_loop_base = saved_eh_lb;
        g->current_this = saved_this;
        g->current_type_sym = saved_type_sym;
        g->current_fn_body = saved_fn_body;
        g->current_fn_no_runtime = false;
        free(param_types);

        bool is_ctor = (member->kind == AST_CONSTRUCTOR_DECL);
        bool is_method = (member->kind == AST_METHOD_DECL);
        bool is_entry = is_method &&
                        ((member->method_decl.name.len == 4 &&
                          memcmp(member->method_decl.name.str, "Main", 4) == 0) ||
                         (member->method_decl.name.len == 12 &&
                          memcmp(member->method_decl.name.str, "__DesignMain", 12) == 0));
        if (!is_entry && (is_method || is_ctor)) {
            bool has_tparams = is_method && member->method_decl.type_params.count > 0;
            bool owner_generic = work[w].type_sym && work[w].type_sym->decl &&
                                 work[w].type_sym->decl->type_decl.type_params.count > 0;
            zan_ast_list_t *attrs = zan_ast_attributes(member);
            bool has_attrs = attrs && attrs->count > 0;
            zan_ast_list_t *oattrs = work[w].type_sym && work[w].type_sym->decl
                                     ? zan_ast_attributes(work[w].type_sym->decl) : NULL;
            bool owner_has_attrs = oattrs && oattrs->count > 0;
            if (!has_tparams && !owner_generic && !has_attrs && !owner_has_attrs) {
                member->method_decl.body = NULL;
            }
        }
        if (g->function_compactor) LLVMClearInsertionPosition(g->builder);
        zan_irgen_compact_completed(g, fn);
        if (zan_diag_has_errors(g->diag)) break;
    }

    g->cur_inst = NULL;
}



typedef struct {
    zan_irgen_t    *g;
    zan_ast_node_t *body;
    zan_ast_list_t *tps;
    zan_istr_t     *names;
    int             count;
    int             names_cap;
    zan_istr_t     *fields;
    int             field_count;
    int             fields_cap;
    bool            found;
} tp_use_scan_t;

static void tp_scan_bind(tp_use_scan_t *s, zan_istr_t name) {
    if (s->count == s->names_cap) {
        int nc = s->names_cap > 0 ? s->names_cap * 2 : 16;
        zan_istr_t *grown = (zan_istr_t *)realloc(s->names,
                                                 (size_t)nc * sizeof(*grown));
        if (!grown) return;
        s->names = grown;
        s->names_cap = nc;
    }
    s->names[s->count++] = name;
}

static void tp_scan_add_field(tp_use_scan_t *s, zan_istr_t name) {
    if (s->field_count == s->fields_cap) {
        int nc = s->fields_cap > 0 ? s->fields_cap * 2 : 16;
        zan_istr_t *grown = (zan_istr_t *)realloc(s->fields,
                                                 (size_t)nc * sizeof(*grown));
        if (!grown) return;
        s->fields = grown;
        s->fields_cap = nc;
    }
    s->fields[s->field_count++] = name;
}

static void tp_scan_free(tp_use_scan_t *s) {
    free(s->names);
    free(s->fields);
    s->names = NULL;
    s->fields = NULL;
}

static bool tp_istr_eq(zan_istr_t a, zan_istr_t b) {
    return a.len == b.len && a.str && b.str &&
           memcmp(a.str, b.str, (size_t)a.len) == 0;
}

static bool tp_typeref_is_tp(tp_use_scan_t *s, zan_ast_node_t *tref) {
    if (!tref || tref->kind != AST_TYPE_REF) return false;
    for (int i = 0; i < s->tps->count; i++)
        if (tp_istr_eq(s->tps->items[i]->ident.name, tref->type_ref.name))
            return true;
    return false;
}

static bool tp_scan_is_tp_field(tp_use_scan_t *s, zan_istr_t name) {
    for (int i = 0; i < s->field_count; i++)
        if (tp_istr_eq(s->fields[i], name)) return true;
    return false;
}

/* `c` (a local/parameter of type T), `item` / `this */
static bool tp_scan_is_tp_value(tp_use_scan_t *s, zan_ast_node_t *e) {
    if (!e) return false;
    if (e->kind == AST_IDENTIFIER) {
        for (int i = 0; i < s->count; i++)
            if (tp_istr_eq(s->names[i], e->ident.name)) return true;
        return tp_scan_is_tp_field(s, e->ident.name);
    }
    if (e->kind == AST_MEMBER_ACCESS && e->member.object &&
        e->member.object->kind == AST_THIS_EXPR)
        return tp_scan_is_tp_field(s, e->member.name);
    return false;
}

static void tp_scan_stmt(tp_use_scan_t *s, zan_ast_node_t *st);

static void tp_scan_expr(tp_use_scan_t *s, zan_ast_node_t *e) {
    if (!e || s->found) return;
    switch (e->kind) {
    case AST_MEMBER_ACCESS:
        if (tp_scan_is_tp_value(s, e->member.object)) { s->found = true; return; }
        tp_scan_expr(s, e->member.object);
        return;
    case AST_INDEX:
        if (tp_scan_is_tp_value(s, e->index.object)) { s->found = true; return; }
        tp_scan_expr(s, e->index.object);
        tp_scan_expr(s, e->index.index);
        return;
    case AST_CALL:
        for (int i = 0; i < e->call.type_args.count; i++)
            if (tp_typeref_is_tp(s, e->call.type_args.items[i])) {
                s->found = true;
                return;
            }
        tp_scan_expr(s, e->call.callee);
        for (int i = 0; i < e->call.args.count; i++)
            tp_scan_expr(s, e->call.args.items[i]);
        return;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        tp_scan_expr(s, e->binary.left);
        tp_scan_expr(s, e->binary.right);
        return;
    case AST_UNARY:
    case AST_POSTFIX_UNARY: tp_scan_expr(s, e->unary.operand); return;
    case AST_CONDITIONAL:
        tp_scan_expr(s, e->conditional.cond);
        tp_scan_expr(s, e->conditional.then_expr);
        tp_scan_expr(s, e->conditional.else_expr);
        return;
    case AST_NEW_EXPR:
        for (int i = 0; i < e->new_expr.args.count; i++)
            tp_scan_expr(s, e->new_expr.args.items[i]);
        for (int i = 0; i < e->new_expr.arg_inits.count; i++)
            tp_scan_expr(s, e->new_expr.arg_inits.items[i]);
        return;
    case AST_CAST_EXPR:  tp_scan_expr(s, e->cast.expr); return;
    case AST_IS_EXPR:
    case AST_AS_EXPR:    tp_scan_expr(s, e->type_test.expr); return;
    case AST_AWAIT_EXPR: tp_scan_expr(s, e->await_expr.expr); return;
    case AST_LAMBDA:     tp_scan_stmt(s, e->lambda.body); return;
    case AST_STRING_INTERP:
        for (int i = 0; i < e->string_interp.parts.count; i++)
            tp_scan_expr(s, e->string_interp.parts.items[i]);
        return;
    default: return;
    }
}

static void tp_scan_stmt(tp_use_scan_t *s, zan_ast_node_t *st) {
    if (!st || s->found) return;
    switch (st->kind) {
    case AST_BLOCK:
        for (int i = 0; i < st->block.stmts.count; i++)
            tp_scan_stmt(s, st->block.stmts.items[i]);
        return;
    case AST_VAR_DECL:
        if (tp_typeref_is_tp(s, st->var_decl.type)) {
            tp_scan_bind(s, st->var_decl.name);
            if (local_is_lambda_captured(s->g, NULL, s->body, st)) {
                s->found = true;
                return;
            }
        }
        tp_scan_expr(s, st->var_decl.initializer);
        return;
    case AST_EXPR_STMT:   tp_scan_expr(s, st->expr_stmt.expr); return;
    case AST_RETURN_STMT: tp_scan_expr(s, st->ret.value); return;
    case AST_THROW_STMT:  tp_scan_expr(s, st->throw_stmt.value); return;
    case AST_IF_STMT:
        tp_scan_expr(s, st->if_stmt.cond);
        tp_scan_stmt(s, st->if_stmt.then_body);
        tp_scan_stmt(s, st->if_stmt.else_body);
        return;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        tp_scan_expr(s, st->while_stmt.cond);
        tp_scan_stmt(s, st->while_stmt.body);
        return;
    case AST_FOR_STMT:
        tp_scan_stmt(s, st->for_stmt.init);
        tp_scan_expr(s, st->for_stmt.cond);
        tp_scan_stmt(s, st->for_stmt.step);
        tp_scan_stmt(s, st->for_stmt.body);
        return;
    case AST_FOREACH_STMT:
        if (tp_typeref_is_tp(s, st->foreach_stmt.var_type))
            tp_scan_bind(s, st->foreach_stmt.var_name);
        tp_scan_expr(s, st->foreach_stmt.collection);
        tp_scan_stmt(s, st->foreach_stmt.body);
        return;
    case AST_TRY_STMT:
        tp_scan_stmt(s, st->try_stmt.try_body);
        for (int i = 0; i < st->try_stmt.catches.count; i++)
            tp_scan_stmt(s, st->try_stmt.catches.items[i]->catch_clause.body);
        tp_scan_stmt(s, st->try_stmt.finally_body);
        return;
    case AST_SWITCH_STMT:
        tp_scan_expr(s, st->switch_stmt.expr);
        for (int i = 0; i < st->switch_stmt.cases.count; i++)
            tp_scan_stmt(s, st->switch_stmt.cases.items[i]->switch_case.body);
        return;
    case AST_LOCK_STMT:
        tp_scan_expr(s, st->while_stmt.cond);
        tp_scan_stmt(s, st->while_stmt.body);
        return;
    case AST_CHECKED_STMT:
        tp_scan_stmt(s, st->checked_stmt.body);
        return;
    default:
        tp_scan_expr(s, st);
        return;
    }
}
bool method_is_tp_template(zan_irgen_t *g, zan_ast_node_t *member) {
    if (!member || member->kind != AST_METHOD_DECL) return false;
    zan_ast_list_t *tps = &member->method_decl.type_params;
    if (tps->count == 0 || !member->method_decl.body) return false;
    tp_use_scan_t s;
    memset(&s, 0, sizeof(s));
    s.g = g;
    s.body = member->method_decl.body;
    s.tps = tps;
    for (int i = 0; i < member->method_decl.params.count; i++) {
        zan_ast_node_t *p = member->method_decl.params.items[i];
        if (p && tp_typeref_is_tp(&s, p->param.type)) {
            tp_scan_bind(&s, p->param.name);
            if (local_is_lambda_captured(g, NULL, s.body, p)) s.found = true;
        }
    }
    bool result = s.found;
    if (!result && s.count > 0) {
        tp_scan_stmt(&s, member->method_decl.body);
        result = s.found;
    }
    tp_scan_free(&s);
    return result;
}

static bool class_member_uses_tp(zan_irgen_t *g, zan_ast_node_t *decl,
                                 zan_ast_node_t *member) {
    if (!decl || !member) return false;
    zan_ast_list_t *tps = &decl->type_decl.type_params;
    if (tps->count == 0) return false;
    zan_ast_node_t *body = NULL;
    zan_ast_list_t *params = NULL;
    if (member->kind == AST_METHOD_DECL || member->kind == AST_CONSTRUCTOR_DECL) {
        body = member->method_decl.body;
        params = &member->method_decl.params;
    }
    if (!body) return false;
    tp_use_scan_t s;
    memset(&s, 0, sizeof(s));
    s.g = g;
    s.body = body;
    s.tps = tps;
    for (int i = 0; i < decl->type_decl.members.count; i++) {
        zan_ast_node_t *f = decl->type_decl.members.items[i];
        if (f && f->kind == AST_FIELD_DECL && tp_typeref_is_tp(&s, f->field_decl.type))
            tp_scan_add_field(&s, f->field_decl.name);
    }
    for (int i = 0; params && i < params->count; i++) {
        zan_ast_node_t *p = params->items[i];
        if (p && tp_typeref_is_tp(&s, p->param.type)) {
            tp_scan_bind(&s, p->param.name);
            if (local_is_lambda_captured(g, NULL, body, p)) s.found = true;
        }
    }
    bool result = s.found;
    if (!result && (s.count > 0 || s.field_count > 0)) {
        tp_scan_stmt(&s, body);
        result = s.found;
    }
    tp_scan_free(&s);
    return result;
}

static bool mspec_owner_eq(zan_type_t *a, zan_type_t *b) {
    if (a == b) return true;
    if (!a || !b || a->sym != b->sym) return false;
    return type_arglists_equal(a->type_args, a->type_arg_count,
                               b->type_args, b->type_arg_count);
}
int get_or_create_method_spec(zan_irgen_t *g, zan_symbol_t *msym,
                                     zan_type_t **bind, int bindc,
                                     zan_type_t *owner_inst) {
    if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return -1;
    zan_ast_node_t *member = msym->decl;
    if (!member->method_decl.body) return -1;
    bool spec_async = (member->method_decl.modifiers & MOD_ASYNC) != 0;
    bool spec_static = (member->method_decl.modifiers & MOD_STATIC) != 0;
    zan_ast_list_t *tps = &member->method_decl.type_params;
    if (tps->count != bindc || bindc <= 0 || bindc > 8) return -1;
    for (int i = 0; i < bindc; i++)
        if (!bind[i] || !type_is_concrete(bind[i])) return -1;
    zan_symbol_t *type_sym = msym->parent;
    if (!type_sym ||
        (type_sym->kind != SYM_CLASS && type_sym->kind != SYM_STRUCT))
        return -1;
    if (!is_user_generic_sym(type_sym)) owner_inst = NULL;
    else if (!spec_static) {
        if (!owner_inst || owner_inst->sym != type_sym ||
            !type_is_concrete(owner_inst))
            return -1;
    } else {
        owner_inst = NULL;
    }
    int this_off = spec_static ? 0 : 1;

    for (int i = 0; i < g->method_spec_count; i++)
        if (g->method_specs[i].msym == msym &&
            mspec_owner_eq(g->method_specs[i].owner_inst, owner_inst) &&
            type_arglists_equal(g->method_specs[i].bind,
                                g->method_specs[i].bindc, bind, bindc))
            return i;

    char fn_name[512];
    {
        char osuffix[256];
        osuffix[0] = '\0';
        if (owner_inst) mangle_inst_suffix(osuffix, sizeof(osuffix), owner_inst);
        size_t off = (size_t)snprintf(fn_name, sizeof(fn_name), "%.*s%s_%.*s$",
            (int)type_sym->name.len, type_sym->name.str, osuffix,
            (int)msym->name.len, msym->name.str);
        for (int i = 0; i < bindc; i++) {
            if (off < sizeof(fn_name) - 1) fn_name[off++] = '$';
            fn_name[off < sizeof(fn_name) ? off : sizeof(fn_name) - 1] = '\0';
            mangle_type_token(fn_name, sizeof(fn_name), &off, bind[i]);
        }
        if (off < sizeof(fn_name)) fn_name[off] = '\0';
        else fn_name[sizeof(fn_name) - 1] = '\0';
        char base_name[512];
        int oi = 2;
        snprintf(base_name, sizeof(base_name), "%s", fn_name);
        while (LLVMGetNamedFunction(g->mod, fn_name))
            snprintf(fn_name, sizeof(fn_name), "%s$o%d", base_name, oi++);
    }

    int param_count = member->method_decl.params.count;
    int total_params = param_count + this_off;
    LLVMTypeRef *param_types = (LLVMTypeRef *)calloc(
        (size_t)(total_params > 0 ? total_params : 1), sizeof(LLVMTypeRef));
    if (this_off) {
        LLVMTypeRef st = get_struct_llvm_type(g, type_sym);
        param_types[0] = st ? LLVMPointerType(st, 0)
                            : LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    }
    for (int k = 0; k < param_count; k++) {
        zan_ast_node_t *param = member->method_decl.params.items[k];
        zan_type_t *pt = subst_method_tp(g,
            zan_binder_resolve_type(g->binder, param->param.type), tps, bind);
        if (owner_inst) pt = subst_type_param_deep(g, pt, owner_inst);
        param_types[k + this_off] = param->param.by_ref
            ? LLVMPointerType(map_type(g, pt), 0)
            : map_type(g, pt);
    }
    zan_type_t *ret_type = member->method_decl.return_type
        ? subst_method_tp(g, zan_binder_resolve_type(g->binder,
              member->method_decl.return_type), tps, bind)
        : g->binder->type_void;
    if (owner_inst) ret_type = subst_type_param_deep(g, ret_type, owner_inst);

    zan_type_t **bcopy = (zan_type_t **)zan_arena_alloc(g->arena,
        sizeof(zan_type_t *) * (size_t)bindc);
    for (int i = 0; i < bindc; i++) bcopy[i] = bind[i];

    LLVMTypeRef fn_type = NULL;
    LLVMValueRef fn = NULL;
    method_body_work_t *air = NULL;
    if (spec_async) {
        air = (method_body_work_t *)zan_arena_alloc(g->arena, sizeof(*air));
        memset(air, 0, sizeof(*air));
        air->member = member;
        air->type_sym = type_sym;
        air->is_static = spec_static;
        air->param_types = param_types;
        air->param_count = param_count;
        air->param_offset = this_off;
        air->llvm_ret = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        air->ret_type = ret_type;
        air->cur_inst = owner_inst;
        air->mtps = tps;
        air->mbind = bcopy;
        zan_ast_list_t *saved_mtps = g->cur_mtps;
        zan_type_t **saved_mbind = g->cur_mbind;
        zan_type_t *saved_inst = g->cur_inst;
        g->cur_mtps = tps;
        g->cur_mbind = bcopy;
        g->cur_inst = owner_inst;
        declare_async_method(g, air, fn_name);
        g->cur_mtps = saved_mtps;
        g->cur_mbind = saved_mbind;
        g->cur_inst = saved_inst;
        fn = air->fn;
        fn_type = air->fn_type;
    } else {
        fn_type = LLVMFunctionType(map_type(g, ret_type), param_types,
                                   (unsigned)total_params, 0);
        fn = LLVMAddFunction(g->mod, fn_name, fn_type);
        zan_set_module_local(fn);
        free(param_types);
    }

    if (g->method_spec_count >= g->method_spec_cap) {
        int ncap = g->method_spec_cap ? g->method_spec_cap * 2 : 32;
        g->method_specs = realloc(g->method_specs,
                                  (size_t)ncap * sizeof(*g->method_specs));
        g->method_spec_cap = ncap;
    }
    int idx = g->method_spec_count++;
    g->method_specs[idx].msym = msym;
    g->method_specs[idx].type_sym = type_sym;
    g->method_specs[idx].owner_inst = owner_inst;
    g->method_specs[idx].member = member;
    g->method_specs[idx].bind = bcopy;
    g->method_specs[idx].bindc = bindc;
    g->method_specs[idx].fn = fn;
    g->method_specs[idx].fn_type = fn_type;
    g->method_specs[idx].is_async = spec_async;
    g->method_specs[idx].async_ir = air;
    return idx;
}

static void emit_method_spec_body(zan_irgen_t *g, int idx) {
    struct zan_method_spec sp = g->method_specs[idx];
    zan_ast_node_t *member = sp.member;
    zan_ast_list_t *tps = &member->method_decl.type_params;

    if (sp.is_async) {
        emit_async_method_ir(g, (method_body_work_t *)sp.async_ir);
        return;
    }

    zan_ast_list_t *saved_mtps = g->cur_mtps;
    zan_type_t **saved_mbind = g->cur_mbind;
    zan_type_t *saved_inst = g->cur_inst;
    g->cur_mtps = tps;
    g->cur_mbind = sp.bind;
    g->cur_inst = sp.owner_inst;

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, sp.fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    di_set_loc(g, member->loc);

    local_scope_t *locals = local_scope_new(g->arena);
    int param_count = member->method_decl.params.count;
    unsigned npt = LLVMCountParamTypes(sp.fn_type);
    int this_off = (member->method_decl.modifiers & MOD_STATIC) ? 0 : 1;
    LLVMTypeRef *param_types = (LLVMTypeRef *)calloc(
        (size_t)(npt > 0 ? npt : 1), sizeof(LLVMTypeRef));
    LLVMGetParamTypes(sp.fn_type, param_types);
    LLVMValueRef spec_this = NULL;
    if (this_off) {
        spec_this = LLVMBuildAlloca(g->builder, param_types[0], "this");
        LLVMBuildStore(g->builder, LLVMGetParam(sp.fn, 0), spec_this);
    }
    for (int k = 0; k < param_count; k++) {
        zan_ast_node_t *param = member->method_decl.params.items[k];
        zan_type_t *pt = resolve_type_ctx(g, param->param.type);
        unsigned pi = (unsigned)(k + this_off);
        if (param->param.by_ref) {
            local_add(locals, param->param.name, LLVMGetParam(sp.fn, pi), pt);
            if (pt && pt->kind == TYPE_STRING)
                locals->vars[locals->count - 1].opaque_string = 1;
            if (is_rc_managed_type(pt))
                locals->vars[locals->count - 1].byref_slot =
                    locals->vars[locals->count - 1].arc_owned = 1;
            box_captured_parameter(g, locals, param, pt, param_types[pi],
                                   LLVMGetParam(sp.fn, pi),
                                   member->method_decl.body);
            continue;
        }
        LLVMValueRef pv = LLVMGetParam(sp.fn, pi);
        LLVMValueRef param_alloca = LLVMBuildAlloca(g->builder, param_types[pi], "p");
        LLVMBuildStore(g->builder, pv, param_alloca);
        local_add(locals, param->param.name, param_alloca, pt);
        box_captured_parameter(g, locals, param, pt, param_types[pi], pv,
                               member->method_decl.body);
        if (!locals->vars[locals->count - 1].box_cell && pt &&
            pt->kind == TYPE_STRUCT &&
            LLVMGetTypeKind(param_types[pi]) == LLVMStructTypeKind &&
            type_contains_collection_rc(g, pt, 0)) {
            locals->vars[locals->count - 1].struct_rc = 1;
            emit_struct_local_retain(g, pt, param_alloca);
        }
        own_written_param(g, locals, param, pt, param_types[pi], pv,
                          member->method_decl.body);
    }
    free(param_types);

    zan_type_t *ret_type = member->method_decl.return_type
        ? resolve_type_ctx(g, member->method_decl.return_type)
        : g->binder->type_void;
    LLVMTypeRef llvm_ret = LLVMGetReturnType(sp.fn_type);

    LLVMValueRef saved_fn = g->current_fn;
    LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
    zan_type_t *saved_fn_zan_ret = g->current_fn_zan_ret_type;
    LLVMValueRef saved_this = g->current_this;
    zan_symbol_t *saved_type_sym = g->current_type_sym;
    zan_ast_node_t *saved_fn_body = g->current_fn_body;
    g->current_fn = sp.fn;
    g->current_fn_ret_type = llvm_ret;
    g->current_fn_zan_ret_type = ret_type;
    int saved_throw_base = g->throw_locals_base;
    int saved_catch_cc = g->catch_cleanup_count;
    int saved_throw_cb = g->throw_catch_base;
    int saved_fin_c = g->finally_count;
    zan_irgen_pending_context_t saved_pending = g->pending;
    int saved_fin_lb = g->finally_loop_base;
    int saved_eh_c = g->eh_armed_count;
    int saved_eh_b = g->eh_armed_base;
    int saved_eh_lb = g->eh_armed_loop_base;
    int saved_lpad_depth = g->lpad_depth;
    LLVMBasicBlockRef fn_cleanup_bb = NULL;
    LLVMValueRef fn_tmp_mark = NULL;
    if (!g->current_async_frame && g->lpad_depth < ZAN_MAX_ARMED_TRY) {
        fn_cleanup_bb = LLVMAppendBasicBlockInContext(g->ctx, sp.fn, "fn.lpad");
        g->lpad_stack[g->lpad_depth].fn = sp.fn;
        g->lpad_stack[g->lpad_depth].lpad_bb = fn_cleanup_bb;
        g->lpad_stack[g->lpad_depth].catch_bb = NULL;
        g->lpad_stack[g->lpad_depth].throw_locals_base = 0;
        g->lpad_stack[g->lpad_depth].fin_idx = -1;
        g->lpad_stack[g->lpad_depth].finally_depth = 0;
        g->lpad_stack[g->lpad_depth].cleanups = NULL;
        g->lpad_stack[g->lpad_depth].cleanup_count = 0;
        g->lpad_stack[g->lpad_depth].cleanup_cap = 0;
        g->lpad_depth++;
        for (int pi = 0; pi < locals->count; pi++) {
            if (local_needs_cleanup(&locals->vars[pi]))
                lpad_register_local(g, &locals->vars[pi]);
        }
        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
        fn_tmp_mark = emit_entry_alloca(g, i32t, "fn.tmp.mark");
        zan_store_fit(g, LLVMBuildLoad2(g->builder, i32t, get_eh_tmp_top_global(g), "fn.tmp.top0"), fn_tmp_mark);
    }
    g->throw_locals_base = 0;
    g->catch_cleanup_count = 0;
    g->throw_catch_base = 0;
    g->finally_count = 0;
    g->pending = (zan_irgen_pending_context_t){0};
    g->finally_loop_base = 0;
    g->eh_armed_base = g->eh_armed_count;
    g->eh_armed_loop_base = g->eh_armed_count;
    g->current_this = spec_this;
    g->current_type_sym = sp.type_sym;
    g->current_fn_body = member->method_decl.body;

    if (member->method_decl.body->kind == AST_BLOCK) {
        for (int k = 0; k < member->method_decl.body->block.stmts.count; k++) {
            emit_stmt(g, member->method_decl.body->block.stmts.items[k], locals);
        }
    } else {
        check_implicit_narrowing(g, g->current_fn_zan_ret_type,
            infer_expr_type(g, member->method_decl.body, locals),
            member->method_decl.body, "return");
        LLVMValueRef val = emit_expr(g, member->method_decl.body, locals);
        LLVMTypeRef val_t = LLVMTypeOf(val);
        if (val_t != llvm_ret) {
            if (LLVMGetTypeKind(llvm_ret) == LLVMFloatTypeKind &&
                LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                val = LLVMBuildFPTrunc(g->builder, val, llvm_ret, "trunc");
            } else if (LLVMGetTypeKind(llvm_ret) == LLVMDoubleTypeKind &&
                       LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
                val = LLVMBuildFPExt(g->builder, val, llvm_ret, "ext");
            } else if (LLVMGetTypeKind(llvm_ret) == LLVMStructTypeKind &&
                       LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
                val = LLVMBuildLoad2(g->builder, llvm_ret, val, "ret.struct");
            }
        }
        if (is_rc_managed_type(ret_type) &&
            !expr_yields_owned_rc_value(g, member->method_decl.body, locals)) {
            emit_rc_retain_for_type(g, ret_type, val);
        }
        emit_release_owned_locals(g, locals);
        LLVMBuildRet(g->builder, val);
    }

    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
    if (!LLVMGetBasicBlockTerminator(cur_bb)) {
        emit_release_owned_locals(g, locals);
        if (ret_type->kind == TYPE_VOID) {
            LLVMBuildRetVoid(g->builder);
        } else {
            LLVMBuildRet(g->builder, LLVMConstNull(llvm_ret));
        }
    }

    g->lpad_depth = saved_lpad_depth;
    if (fn_cleanup_bb) {
        zan_lpad_entry_t *fn_entry = &g->lpad_stack[saved_lpad_depth];
        if (LLVMGetFirstUse(LLVMBasicBlockAsValue(fn_cleanup_bb)) != NULL) {
            zan_builder_point_t cl_pt = zan_builder_save(g);
            LLVMPositionBuilderAtEnd(g->builder, fn_cleanup_bb);
            ensure_function_personality(g, sp.fn);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef lp_ret_ty = LLVMStructTypeInContext(g->ctx, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0);
            LLVMValueRef lp = LLVMBuildLandingPad(g->builder, lp_ret_ty, get_eh_personality_fn(g), 0, "fn.lp");
            LLVMSetCleanup(lp, 1);
            if (fn_tmp_mark) {
                LLVMValueRef mark = LLVMBuildLoad2(g->builder, i32t, fn_tmp_mark, "fn.tmp.mark.val");
                emit_eh_tmp_unwind_to(g, mark);
            }
            emit_lpad_cleanups(g, fn_entry);
            LLVMBuildResume(g->builder, lp);
            zan_builder_restore(g, cl_pt);
        } else {
            LLVMDeleteBasicBlock(fn_cleanup_bb);
        }
        free(fn_entry->cleanups);
        fn_entry->cleanups = NULL;
        fn_entry->cleanup_count = 0;
        fn_entry->cleanup_cap = 0;
    }

    g->current_fn = saved_fn;
    g->current_fn_ret_type = saved_fn_ret;
    g->current_fn_zan_ret_type = saved_fn_zan_ret;
    g->throw_locals_base = saved_throw_base;
    g->catch_cleanup_count = saved_catch_cc;
    g->throw_catch_base = saved_throw_cb;
    g->finally_count = saved_fin_c;
    g->pending = saved_pending;
    g->finally_loop_base = saved_fin_lb;
    g->eh_armed_count = saved_eh_c;
    g->eh_armed_base = saved_eh_b;
    g->eh_armed_loop_base = saved_eh_lb;
    g->current_this = saved_this;
    g->current_type_sym = saved_type_sym;
    g->current_fn_body = saved_fn_body;
    g->cur_mtps = saved_mtps;
    g->cur_mbind = saved_mbind;
    g->cur_inst = saved_inst;
    if (saved_bb) LLVMPositionBuilderAtEnd(g->builder, saved_bb);
    else if (g->function_compactor) LLVMClearInsertionPosition(g->builder);
    zan_irgen_compact_completed(g, sp.fn);
}
void emit_pending_method_specs(zan_irgen_t *g) {
    while (g->method_spec_emitted < g->method_spec_count) {
        int i = g->method_spec_emitted++;
        emit_method_spec_body(g, i);
        if (zan_diag_has_errors(g->diag)) break;
    }
}

static void emit_windows_dll_main(zan_irgen_t *g) {
    LLVMValueRef existing = LLVMGetNamedFunction(g->mod, "DllMain");
    if (existing) return;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef ft = LLVMFunctionType(i32, NULL, 0, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "DllMain", ft);
    LLVMSetLinkage(fn, LLVMExternalLinkage);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, bb);
    LLVMBuildRet(g->builder, LLVMConstInt(i32, 1, 0));
    if (g->function_compactor) LLVMClearInsertionPosition(g->builder);
    zan_irgen_compact_completed(g, fn);
}

