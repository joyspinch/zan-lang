/* 内部辅助实现 */

/* 核心系统底层抽象与内存语义契约 */

static void emit_main_method(zan_irgen_t *g, zan_ast_node_t *method, zan_symbol_t *type_sym,
                             zan_ast_node_t *unit) {
    /* 内部辅助逻辑 */
    LLVMTypeRef i32ty = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i8ptrptr = LLVMPointerType(i8ptr, 0);
    LLVMTypeRef main_params[] = { i32ty, i8ptrptr };
    LLVMTypeRef main_type = LLVMFunctionType(i32ty, main_params, 2, 0);
    LLVMValueRef main_fn = LLVMAddFunction(g->mod, "main", main_type);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, main_fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    di_clear(g);

    /* 内部辅助逻辑 */
    if (g->target_is_windows) {
        LLVMTypeRef uintt = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef setcp_type = LLVMFunctionType(uintt, (LLVMTypeRef[]){ uintt }, 1, 0);
        LLVMValueRef fn_set_out = LLVMGetNamedFunction(g->mod, "SetConsoleOutputCP");
        if (!fn_set_out) fn_set_out = LLVMAddFunction(g->mod, "SetConsoleOutputCP", setcp_type);
        LLVMValueRef fn_set_in = LLVMGetNamedFunction(g->mod, "SetConsoleCP");
        if (!fn_set_in) fn_set_in = LLVMAddFunction(g->mod, "SetConsoleCP", setcp_type);
        LLVMValueRef cp_utf8 = LLVMConstInt(uintt, 65001, 0);
        zan_call2(g->builder, setcp_type, fn_set_out, &cp_utf8, 1, "");
        zan_call2(g->builder, setcp_type, fn_set_in, &cp_utf8, 1, "");
    }

    /* 编译器代码生成与运行时系统底层调用契约 */
    LLVMValueRef main_argc = LLVMGetParam(main_fn, 0);
    LLVMValueRef main_argv = LLVMGetParam(main_fn, 1);
    if (g->target_is_windows) {
        LLVMTypeRef i32ptr = LLVMPointerType(i32ty, 0);
        LLVMTypeRef i8ptrptrptr = LLVMPointerType(i8ptrptr, 0);
        LLVMTypeRef utf8_argv_type = LLVMFunctionType(i32ty,
            (LLVMTypeRef[]){ i32ptr, i8ptrptrptr }, 2, 0);
        LLVMValueRef utf8_argv_fn = LLVMGetNamedFunction(g->mod, "zan_utf8_argv");
        if (!utf8_argv_fn)
            utf8_argv_fn = LLVMAddFunction(g->mod, "zan_utf8_argv", utf8_argv_type);
        LLVMValueRef argc_slot = LLVMBuildAlloca(g->builder, i32ty, "utf8_argc");
        LLVMValueRef argv_slot = LLVMBuildAlloca(g->builder, i8ptrptr, "utf8_argv");
        LLVMBuildStore(g->builder, main_argc, argc_slot);
        LLVMBuildStore(g->builder, main_argv, argv_slot);
        LLVMValueRef utf8_argv_args[] = { argc_slot, argv_slot };
        zan_call2(g->builder, utf8_argv_type, utf8_argv_fn, utf8_argv_args, 2, "");
        main_argc = LLVMBuildLoad2(g->builder, i32ty, argc_slot, "utf8_argc.value");
        main_argv = LLVMBuildLoad2(g->builder, i8ptrptr, argv_slot, "utf8_argv.value");
    }

    /* 模块核心语义抽象与接口调用契约 */
    LLVMValueRef g_argc = LLVMGetNamedGlobal(g->mod, "__zan_argc");
    if (!g_argc) {
        g_argc = LLVMAddGlobal(g->mod, i32ty, "__zan_argc");
        LLVMSetInitializer(g_argc, LLVMConstInt(i32ty, 0, 0));
    }
    LLVMValueRef g_argv = LLVMGetNamedGlobal(g->mod, "__zan_argv");
    if (!g_argv) {
        g_argv = LLVMAddGlobal(g->mod, i8ptrptr, "__zan_argv");
        LLVMSetInitializer(g_argv, LLVMConstNull(i8ptrptr));
    }
    LLVMBuildStore(g->builder, main_argc, g_argc);
    LLVMBuildStore(g->builder, main_argv, g_argv);

    /* 内部辅助逻辑 */
    {
        LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef svi32 = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef svi64 = LLVMInt64TypeInContext(g->ctx);
        LLVMValueRef stdout_ptr;
        if (g->target_is_windows) {
            LLVMTypeRef iob_type = LLVMFunctionType(i8p, (LLVMTypeRef[]){ svi32 }, 1, 0);
            LLVMValueRef iobfn = LLVMGetNamedFunction(g->mod, "__acrt_iob_func");
            if (!iobfn) iobfn = LLVMAddFunction(g->mod, "__acrt_iob_func", iob_type);
            LLVMValueRef one = LLVMConstInt(svi32, 1, 0);
            stdout_ptr = zan_call2(g->builder, iob_type, iobfn, &one, 1, "stdout");
        } else {
            const char *soname = g->target_is_macos ? "__stdoutp" : "stdout";
            LLVMValueRef sg = LLVMGetNamedGlobal(g->mod, soname);
            if (!sg) sg = LLVMAddGlobal(g->mod, i8p, soname);
            stdout_ptr = LLVMBuildLoad2(g->builder, i8p, sg, "stdout");
        }
        LLVMTypeRef sv_type = LLVMFunctionType(svi32,
            (LLVMTypeRef[]){ i8p, i8p, svi32, svi64 }, 4, 0);
        LLVMValueRef sv = LLVMGetNamedFunction(g->mod, "setvbuf");
        if (!sv) sv = LLVMAddFunction(g->mod, "setvbuf", sv_type);
        int sv_mode = g->target_is_windows ? 4 /* _IONBF */ : 1 /* _IOLBF */;
        LLVMValueRef sv_args[] = { stdout_ptr, LLVMConstNull(i8p),
            LLVMConstInt(svi32, sv_mode, 0),
            LLVMConstInt(svi64, g->target_is_windows ? 0 : 4096, 0) };
        zan_call2(g->builder, sv_type, sv, sv_args, 4, "");
    }

    /* 内部辅助实现 */
    if (g->target_is_windows) {
        LLVMValueRef set_out_cp = LLVMGetNamedFunction(g->mod, "SetConsoleOutputCP");
        LLVMTypeRef setcp_type = LLVMFunctionType(i32ty, (LLVMTypeRef[]){ i32ty }, 1, 0);
        if (!set_out_cp)
            set_out_cp = LLVMAddFunction(g->mod, "SetConsoleOutputCP", setcp_type);
        LLVMValueRef set_in_cp = LLVMGetNamedFunction(g->mod, "SetConsoleCP");
        if (!set_in_cp)
            set_in_cp = LLVMAddFunction(g->mod, "SetConsoleCP", setcp_type);
        LLVMValueRef utf8cp = LLVMConstInt(i32ty, 65001, 0);
        zan_call2(g->builder, setcp_type, set_out_cp, &utf8cp, 1, "");
        zan_call2(g->builder, setcp_type, set_in_cp, &utf8cp, 1, "");
    }

    g->current_fn = main_fn;
    g->current_fn_ret_type = LLVMInt32TypeInContext(g->ctx);
    /* 内部辅助逻辑 */
    if (g->strict_runtime) {
        LLVMTypeRef strict_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                                 NULL, 0, 0);
        LLVMValueRef strict_fn = LLVMGetNamedFunction(g->mod, "zan_rt_set_strict");
        if (!strict_fn)
            strict_fn = LLVMAddFunction(g->mod, "zan_rt_set_strict", strict_ty);
        zan_call2(g->builder, strict_ty, strict_fn, NULL, 0, "");
    }
    g->throw_locals_base = 0;
    g->catch_cleanup_count = 0;
    g->throw_catch_base = 0;
    g->finally_count = 0;
    g->pending = (zan_irgen_pending_context_t){0};
    g->finally_loop_base = 0;
    g->eh_armed_count = 0;
    g->eh_armed_base = 0;
    g->eh_armed_loop_base = 0;
    g->current_type_sym = type_sym;
    g->current_this = NULL;
    g->current_fn_body = method->method_decl.body;

    /* 底层系统交互与数据协议契约 */
    if (g->check_leaks) {
        emit_leak_report_support(g);
        zan_call2(g->builder, g->atexit_type, g->fn_atexit,
                       &g->fn_report_leaks, 1, "");
    }

    /* 协程工作窃取调度器 */
    zan_call2(g->builder, g->rt_co_sched_init_type, g->rt_co_sched_init, NULL, 0, "");

    /* 编译器代码生成与运行时系统底层调用契约 */
    di_set_loc(g, method->loc);

    /* 内部辅助逻辑 */
    if (unit && unit->kind == AST_COMPILATION_UNIT) {
        local_scope_t *sf_locals = local_scope_new(g->arena);
        zan_symbol_t *saved_type = g->current_type_sym;
        LLVMValueRef saved_this = g->current_this;
        g->current_this = NULL;
        for (int di = 0; di < unit->comp_unit.decls.count; di++) {
            zan_ast_node_t *d = unit->comp_unit.decls.items[di];
            if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL) continue;
            zan_symbol_t *csym = zan_binder_lookup(g->binder, d->type_decl.name);
            if (!csym) continue;
            g->current_type_sym = csym;
            for (int mi = 0; mi < d->type_decl.members.count; mi++) {
                zan_ast_node_t *m = d->type_decl.members.items[mi];
                if (m->kind != AST_FIELD_DECL && m->kind != AST_PROPERTY_DECL) continue;
                if (!(m->field_decl.modifiers & MOD_STATIC)) continue;
                if (!m->field_decl.initializer) continue;
                /* 内部辅助逻辑 */
                if (m->kind == AST_PROPERTY_DECL &&
                    (m->field_decl.getter_body || m->field_decl.setter_body))
                    continue;
                zan_symbol_t *fs = get_field_sym(csym, m->field_decl.name);
                /* 内部辅助实现 */
                zan_type_t **insts = NULL;
                int ninst = 0, inst_cap = 0;
                if (d->type_decl.type_params.count > 0) {
                    for (int gi = 0; gi < g->generic_inst_count; gi++) {
                        if (g->generic_insts[gi].type_sym != csym) continue;
                        bool seen = false;
                        for (int k = 0; k < ninst && !seen; k++)
                            seen = types_equal(insts[k], g->generic_insts[gi].inst);
                        if (seen) continue;
                        if (ninst == inst_cap) {
                            inst_cap = inst_cap ? inst_cap * 2 : 8;
                            insts = (zan_type_t **)realloc(insts,
                                (size_t)inst_cap * sizeof(*insts));
                        }
                        insts[ninst++] = g->generic_insts[gi].inst;
                    }
                } else {
                    insts = (zan_type_t **)malloc(sizeof(*insts));
                    insts[ninst++] = NULL;
                }
                for (int ii = 0; ii < ninst; ii++) {
                zan_type_t *saved_field_inst = g->cur_inst;
                g->cur_inst = insts[ii];
                LLVMValueRef gv = get_static_field_global(g, csym, fs, insts[ii]);
                if (gv) {
                zan_type_t *source_type = infer_expr_type(
                    g, m->field_decl.initializer, sf_locals);
                check_implicit_narrowing(g, fs->type, source_type,
                    m->field_decl.initializer, "field initializer");
                check_value_type_mismatch(g, fs->type, source_type,
                    m->field_decl.initializer, "field initializer");
                LLVMValueRef v = fs->type && fs->type->kind == TYPE_DELEGATE &&
                                 m->field_decl.initializer->kind == AST_LAMBDA
                    ? emit_lambda_typed(g, m->field_decl.initializer,
                                        fs->type, sf_locals)
                    : emit_expr(g, m->field_decl.initializer, sf_locals);
                if (fs->type && (is_rc_managed_type(fs->type) || fs->type->kind == TYPE_OBJECT)) {
                    emit_rc_store_field(g, fs->type, gv, v, m->field_decl.initializer, sf_locals,
                                        (fs->modifiers & MOD_WEAK) ? 1 : 0);
                } else {
                    LLVMTypeRef ft = fs->type ? map_type(g, fs->type)
                                              : LLVMInt64TypeInContext(g->ctx);
                    LLVMBuildStore(g->builder, coerce_int_to(g, v, ft), gv);
                }
                }
                g->cur_inst = saved_field_inst;
                }
                free(insts);
            }
        }
        /* 内部辅助实现 */
        for (int di = 0; di < unit->comp_unit.decls.count; di++) {
            zan_ast_node_t *d = unit->comp_unit.decls.items[di];
            if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL) continue;
            char cctor_name[512];
            snprintf(cctor_name, sizeof(cctor_name), "%.*s_cctor",
                     (int)d->type_decl.name.len, d->type_decl.name.str);
            LLVMValueRef cctor = LLVMGetNamedFunction(g->mod, cctor_name);
            if (!cctor) continue;
            zan_call2(g->builder, LLVMGlobalGetValueType(cctor), cctor,
                      NULL, 0, "");
        }
        g->current_type_sym = saved_type;
        g->current_this = saved_this;
    }

    /* 内部辅助逻辑 */
    if ((method->method_decl.modifiers & MOD_ASYNC) &&
        method->method_decl.params.count == 0 && type_sym) {
        char ramp_name[512];
        snprintf(ramp_name, sizeof(ramp_name), "%.*s_Main",
                 (int)type_sym->name.len, type_sym->name.str);
        LLVMValueRef ramp = LLVMGetNamedFunction(g->mod, ramp_name);
        char res_name[520];
        snprintf(res_name, sizeof(res_name), "%s$resume", ramp_name);
        LLVMValueRef resume = LLVMGetNamedFunction(g->mod, res_name);
        if (ramp && resume) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef mi8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef aw_tmp_mark = LLVMBuildLoad2(g->builder,
                LLVMInt32TypeInContext(g->ctx), get_eh_tmp_top_global(g),
                "main.tmpmark");
            LLVMValueRef sub = zan_call2(g->builder, LLVMGlobalGetValueType(ramp),
                ramp, NULL, 0, "main.task");
            LLVMValueRef sub_i8 = LLVMBuildBitCast(g->builder, sub, mi8ptr, "main.task8");
            LLVMValueRef sched_args[] = { sub_i8, resume };
            zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
            zan_call2(g->builder, g->rt_co_sched_run_type, g->rt_co_sched_run, NULL, 0, "");
            emit_async_check_sub_exc(g, sub_i8, aw_tmp_mark);
            LLVMValueRef rptr = LLVMBuildStructGEP2(g->builder, g->co_header_type,
                sub_i8, ASYNC_FRAME_RESULT, "main.res.p");
            LLVMValueRef res = LLVMBuildLoad2(g->builder, i64, rptr, "main.res");
            zan_emit_frame_free(g, sub_i8);
            emit_release_static_rc_fields(g, unit);
            /* 编译器代码生成与运行时系统底层调用契约 */
            LLVMBuildRet(g->builder, LLVMBuildTrunc(g->builder, res,
                LLVMInt32TypeInContext(g->ctx), "main.ret"));
            g->current_type_sym = NULL;
            g->current_fn_body = NULL;
            return;
        }
    }

    local_scope_t *locals = local_scope_new(g->arena);

    /* 内部辅助逻辑 */
    if (method->method_decl.params.count == 1) {
        zan_ast_node_t *param = method->method_decl.params.items[0];
        zan_type_t *pt = zan_binder_resolve_type(g->binder, param->param.type);
        if (pt && pt->kind == TYPE_ARRAY && pt->element_type &&
            pt->element_type->kind == TYPE_STRING) {
            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
            LLVMTypeRef i8ptrptr = LLVMPointerType(i8ptr, 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef g_argc = LLVMGetNamedGlobal(g->mod, "__zan_argc");
            if (!g_argc) {
                g_argc = LLVMAddGlobal(g->mod, i32, "__zan_argc");
                LLVMSetInitializer(g_argc, LLVMConstInt(i32, 0, 0));
            }
            LLVMValueRef g_argv = LLVMGetNamedGlobal(g->mod, "__zan_argv");
            if (!g_argv) {
                g_argv = LLVMAddGlobal(g->mod, i8ptrptr, "__zan_argv");
                LLVMSetInitializer(g_argv, LLVMConstNull(i8ptrptr));
            }
            LLVMValueRef argc = LLVMBuildLoad2(g->builder, i32, g_argc, "ma.argc");
            LLVMValueRef argc64 = LLVMBuildSExt(g->builder, argc, i64t, "ma.argc64");
            /* 底层系统交互与数据协议契约 */
            LLVMValueRef nneg = zan_icmp(g->builder, LLVMIntSGT, argc64,
                LLVMConstInt(i64t, 0, 0), "ma.nneg");
            LLVMValueRef n = LLVMBuildSelect(g->builder, nneg,
                zan_sub(g->builder, argc64, LLVMConstInt(i64t, 1, 0), "ma.n"),
                LLVMConstInt(i64t, 0, 0), "ma.count");
            LLVMValueRef total = zan_mul(g->builder, n,
                LLVMSizeOf(i8ptrptr), "ma.total");
            LLVMValueRef arr = zan_array_alloc_typed(g, total, n, g->binder->type_string);
            LLVMValueRef argv = LLVMBuildLoad2(g->builder, i8ptrptr, g_argv, "ma.argv");
            /* 模块核心语义抽象与接口调用契约 */
            LLVMValueRef lp = emit_entry_alloca(g, i64t, "ma.i");
            zan_store_fit(g, LLVMConstInt(i64t, 0, 0), lp);
            LLVMValueRef ffn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, ffn, "ma.cond");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, ffn, "ma.fill");
            LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, ffn, "ma.filled");
            LLVMBuildBr(g->builder, cond_bb);
            LLVMPositionBuilderAtEnd(g->builder, cond_bb);
            LLVMValueRef iv = LLVMBuildLoad2(g->builder, i64t, lp, "ma.iv");
            LLVMBuildCondBr(g->builder, zan_icmp(g->builder, LLVMIntSLT, iv, n, "ma.more"),
                body_bb, done_bb);
            LLVMPositionBuilderAtEnd(g->builder, body_bb);
            LLVMValueRef iv1 = zan_add(g->builder, iv, LLVMConstInt(i64t, 1, 0), "ma.i1");
            LLVMValueRef slot = LLVMBuildGEP2(g->builder, i8ptr, argv, &iv1, 1, "ma.slot");
            LLVMValueRef cstr = LLVMBuildLoad2(g->builder, i8ptr, slot, "ma.cstr");
            LLVMValueRef len = zan_call2(g->builder,
                LLVMFunctionType(i64t, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                g->fn_strlen, &cstr, 1, "ma.len");
            LLVMValueRef cap = zan_add(g->builder, len, LLVMConstInt(i64t, 1, 0), "ma.cap");
            LLVMValueRef buf = emit_string_alloc_rc(g, cap);
            LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr,
                (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
            LLVMValueRef mc = get_libc_fn(g, "memcpy", memcpy_ty);
            zan_call2(g->builder, memcpy_ty, mc,
                (LLVMValueRef[]){ buf, cstr, len }, 3, "");
            LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, buf, &len, 1, "ma.ep");
            zan_store_fit(g, LLVMConstInt(i8, 0, 0), endp);
            LLVMTypeRef i64p = LLVMPointerType(i64t, 0);
            LLVMValueRef fits = zan_icmp(g->builder, LLVMIntULE, len,
                LLVMConstInt(i64t, ZAN_STR_LEN_MASK, 0), "ma.fits");
            LLVMValueRef half = LLVMBuildSelect(g->builder, fits, len,
                LLVMConstInt(i64t, ZAN_STR_LEN_UNKNOWN, 0), "ma.half");
            LLVMValueRef word = LLVMBuildOr(g->builder,
                LLVMConstInt(i64t, ZAN_STRING_TAG << 32, 0), half, "ma.hdr");
            LLVMValueRef hdr_ptr = LLVMBuildGEP2(g->builder, i8, buf,
                &(LLVMValueRef){ LLVMConstInt(i64t, (uint64_t)ZAN_OBJ_SITE_OFF, 1) }, 1,
                "ma.hdrp");
            LLVMBuildStore(g->builder, word,
                LLVMBuildBitCast(g->builder, hdr_ptr, i64p, "ma.hdrip"));
            LLVMValueRef elem = LLVMBuildGEP2(g->builder, i8ptr, arr, &iv, 1, "ma.ep.slot");
            zan_store_fit(g, buf, elem);
            zan_store_fit(g, iv1, lp);
            LLVMBuildBr(g->builder, cond_bb);
            LLVMPositionBuilderAtEnd(g->builder, done_bb);
            zan_type_t *args_type = zan_binder_make_array_type(g->binder,
                g->binder->type_string);
            /* 内部辅助逻辑 */
            LLVMValueRef slot_a = emit_entry_alloca(g, i8ptrptr, "ma.slot");
            zan_store_fit(g, arr, slot_a);
            local_add(locals, param->param.name, slot_a, args_type);
            box_captured_parameter(g, locals, param, args_type, i8ptrptr, arr,
                                   method->method_decl.body);
            if (locals->vars[locals->count - 1].box_cell) {
                /* 内部辅助逻辑 */
                emit_rc_release_for_type(g, args_type, arr);
            } else {
                arc_own_local(g, locals);
            }
        }
    }

    g->current_fn_is_main = true;
    if (method->method_decl.body) {
        emit_stmt(g, method->method_decl.body, locals);
    }
    g->current_fn_is_main = false;

    /* 核心系统底层抽象与内存语义契约 */
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
        emit_release_owned_locals(g, locals);
        emit_release_static_rc_fields(g, unit);
        LLVMBuildRet(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0));
    }
    g->current_type_sym = NULL;
    g->current_fn_body = NULL;
}

/* 核心系统底层抽象与内存语义契约 */

/* 内部辅助逻辑 */
typedef struct {
    zan_ast_node_t *member;
    zan_symbol_t   *type_sym;
    LLVMValueRef    fn;
    LLVMTypeRef    *param_types;
    int             param_count;
    int             param_offset;
    bool            is_static;
    LLVMTypeRef     llvm_ret;
    zan_type_t     *ret_type;
    /* 内部辅助逻辑 */
    bool            is_async;
    LLVMValueRef    resume_fn;
    LLVMTypeRef     frame_type;
    int             await_count;   /* 核心系统底层抽象与内存语义契约 */
    async_local_t  *alocals;       /* 底层系统交互与数据协议契约 */
    int             alocal_count;
    int             sub_base;       /* 底层系统交互与数据协议契约 */
    int             ret_agg_slot;   /* 底层系统交互与数据协议契约 */
    int             handler_cap;    /* 核心系统底层抽象与内存语义契约 */
    int             try_count;      /* 底层系统交互与数据协议契约 */
    /* 内部辅助逻辑 */
    int             fin_depth_max;
    zan_type_t     *cur_inst;       /* 核心系统底层抽象与内存语义契约 */
    LLVMTypeRef     fn_type;        /* 核心系统底层抽象与内存语义契约 */
    zan_ast_list_t *mtps;           /* 核心系统底层抽象与内存语义契约 */
    zan_type_t    **mbind;          /* 核心系统底层抽象与内存语义契约 */
} method_body_work_t;

/* 编译器代码生成与运行时系统底层调用契约 */
static int generic_variant_count(zan_irgen_t *g, zan_symbol_t *type_sym) {
    int n = 0;
    for (int i = 0; i < g->generic_inst_count; i++)
        if (g->generic_insts[i].type_sym == type_sym) n++;
    return n;
}

static bool method_is_tp_template(zan_irgen_t *g, zan_ast_node_t *member);
static bool class_member_uses_tp(zan_irgen_t *g, zan_ast_node_t *decl,
                                 zan_ast_node_t *member);

/* 内部辅助实现 */
static void emit_tp_erased_stub(zan_irgen_t *g, LLVMValueRef fn) {
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, bb);
    LLVMValueRef ab = LLVMGetNamedFunction(g->mod, "abort");
    LLVMTypeRef vfn = ab ? LLVMGlobalGetValueType(ab)
                         : LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    if (!ab) ab = LLVMAddFunction(g->mod, "abort", vfn);
    zan_call2(g->builder, vfn, ab, NULL, 0, "");
    LLVMBuildUnreachable(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    else if (g->function_compactor) LLVMClearInsertionPosition(g->builder);
    zan_irgen_compact_completed(g, fn);
}

/* 编译器代码生成与运行时系统底层调用契约 */
static bool is_task_like_type(zan_type_t *t) {
    if (!t) return false;
    if (t->kind == TYPE_TASK) return true;
    if (t->name.str && ((t->name.len >= 4 && memcmp(t->name.str, "Task", 4) == 0) ||
                        (t->name.len >= 9 && memcmp(t->name.str, "ValueTask", 9) == 0))) return true;
    if (t->sym && t->sym->name.str &&
        ((t->sym->name.len == 4 && memcmp(t->sym->name.str, "Task", 4) == 0) ||
         (t->sym->name.len == 9 && memcmp(t->sym->name.str, "ValueTask", 9) == 0))) return true;
    return false;
}

static void declare_async_method(zan_irgen_t *g, method_body_work_t *w,
                                 const char *fn_name) {
    zan_ast_node_t *member = w->member;
    zan_symbol_t *type_sym = w->type_sym;
    LLVMTypeRef *param_types = w->param_types;
    int param_count = w->param_count;
    int param_offset = w->param_offset;
    bool is_static = w->is_static;
    int total_params = param_count + param_offset;
    LLVMValueRef fn = NULL, resume_fn = NULL;
    LLVMTypeRef frame_type = NULL;
    w->is_async = true;
    g->has_async_work = true;
    w->handler_cap = 1;
        LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);

        /* 内部辅助实现 */
        {
            int anf_counter = 0;
            anf_normalize_block(g, member->method_decl.body, &anf_counter);
        }

        /* 内部辅助逻辑 */
        async_scan_t scan = { .g = g, .scope =
                              local_scope_new(g->arena) };
        scan.body = member->method_decl.body;
        if (!is_static)
            local_add(scan.scope, (zan_istr_t){(char *)"this", 4}, NULL,
                      type_sym->type);
        for (int k = 0; k < param_count; k++) {
            zan_ast_node_t *param = member->method_decl.params.items[k];
            local_add(scan.scope, param->param.name, NULL,
                      resolve_type_ctx(g, param->param.type));
        }
        zan_symbol_t *scan_saved_type = g->current_type_sym;
        g->current_type_sym = type_sym;
        async_scan_stmt(&scan, member->method_decl.body);
        g->current_type_sym = scan_saved_type;
        w->await_count = scan.await_count;
        w->alocals = scan.locals;
        w->alocal_count = scan.local_count;
        w->fin_depth_max = scan.fin_depth_max;
        w->try_count = scan.try_count;
        /* 编译器代码生成与运行时系统底层调用契约 */
        w->handler_cap = scan.try_count > 0 ? scan.try_count : 1;

        /* 内部辅助逻辑 */
        int locals_base = ASYNC_FRAME_FIRST_PARAM + total_params;
        w->sub_base = locals_base + w->alocal_count;
        int nfields = w->sub_base + (w->await_count > 0 ? 1 : 0);

        int ret_agg_slot = -1;
        zan_type_t *raw_ret = w->ret_type ? w->ret_type : g->binder->type_void;
        if (member->method_decl.return_type) {
            raw_ret = zan_binder_resolve_type(g->binder, member->method_decl.return_type);
            if (w->cur_inst) raw_ret = subst_type_param_deep(g, raw_ret, w->cur_inst);
        }
        zan_type_t *ret_type = raw_ret;
        bool is_task = member->method_decl.is_task_return;
        if (ret_type && ret_type->type_arg_count > 0 && is_task_like_type(ret_type)) {
            ret_type = concretize(g, ret_type->type_args[0]);
            is_task = true;
        }
        if (!is_task && raw_ret && raw_ret->kind != TYPE_VOID) {
            LLVMTypeRef art = map_type(g, raw_ret);
            if (art && LLVMGetTypeKind(art) == LLVMStructTypeKind) {
                zan_diag_emit(g->diag, DIAG_ERROR, member->loc,
                              "an async method cannot return an aggregate type: "
                              "the coroutine result slot is one machine word");
            }
        }
        LLVMTypeRef lret = map_type(g, ret_type);
        bool is_agg_ret = (lret && LLVMGetTypeKind(lret) == LLVMStructTypeKind);
        if (is_agg_ret) {
            ret_agg_slot = nfields++;
        }
        w->ret_agg_slot = ret_agg_slot;

        LLVMTypeRef *fields = (LLVMTypeRef *)calloc((size_t)nfields, sizeof(LLVMTypeRef));
        fields[ASYNC_FRAME_SCHED] = i64;
        fields[ASYNC_FRAME_SCHED_STEP] = g->co_step_ptr;
        fields[ASYNC_FRAME_STATE] = i32;
        fields[ASYNC_FRAME_DONE] = i32;
        fields[ASYNC_FRAME_AWAITER] = i8ptr;
        fields[ASYNC_FRAME_AWAITER_STEP] = g->co_step_ptr;
        fields[ASYNC_FRAME_RESULT] = i64;
        fields[ASYNC_FRAME_PENDING_COUNT] = i32;
        fields[ASYNC_FRAME_CLEANUP] = g->co_step_ptr;
        fields[ASYNC_FRAME_HCOUNT] = i32;
        fields[ASYNC_FRAME_SELF_STEP] = g->co_step_ptr;
        fields[ASYNC_FRAME_EXC] = i8ptr;
        fields[ASYNC_FRAME_EXC_TID] = i8ptr;
        fields[ASYNC_FRAME_EXC_OWNED] = i32;
        fields[ASYNC_FRAME_CANCEL] = i32;
        fields[ASYNC_FRAME_CHILD] = i8ptr;
        fields[ASYNC_FRAME_LNEXT] = i8ptr;
        fields[ASYNC_FRAME_HSTACK] = LLVMArrayType(i32, (unsigned)w->handler_cap);
        fields[ASYNC_FRAME_CEXC] = LLVMArrayType(i8ptr, (unsigned)w->handler_cap);
        fields[ASYNC_FRAME_CEXC_OWNED] = LLVMArrayType(i32, (unsigned)w->handler_cap);
        fields[ASYNC_FRAME_CEXC_TID] = LLVMArrayType(i8ptr, (unsigned)w->handler_cap);
        /* 底层系统交互与数据协议契约 */
        {
            unsigned pending_cap = w->fin_depth_max > 0
                ? (unsigned)w->fin_depth_max + 1 : 0;
            LLVMTypeRef record_fields[] = { i64, i8ptr, i32, lret };
            LLVMTypeRef record = LLVMStructTypeInContext(g->ctx, record_fields,
                                                         is_agg_ret ? 4 : 3, 0);
            fields[ASYNC_FRAME_PENDING] = LLVMArrayType(record, pending_cap);
            fields[ASYNC_FRAME_HPENDING] = LLVMArrayType(i32, (unsigned)w->handler_cap);
        }
        for (int k = 0; k < total_params; k++) {
            fields[ASYNC_FRAME_FIRST_PARAM + k] = param_types[k];
        }
        for (int k = 0; k < w->alocal_count; k++) {
            w->alocals[k].frame_index = locals_base + k;
            fields[locals_base + k] = w->alocals[k].llvm;
        }
        if (w->await_count > 0) {
            fields[w->sub_base] = i8ptr;
        }
        if (is_agg_ret) {
            fields[ret_agg_slot] = lret;
        }
        char frame_name[560];
        snprintf(frame_name, sizeof(frame_name), "%s$frame", fn_name);
        w->frame_type = frame_type = LLVMStructCreateNamed(g->ctx, frame_name);
        LLVMStructSetBody(frame_type, fields, (unsigned)nfields, 0);
        free(fields);

        /* 内部辅助逻辑 */
        w->fn_type = LLVMFunctionType(i8ptr, param_types, (unsigned)total_params, 0);
        w->fn = fn = LLVMAddFunction(g->mod, fn_name, w->fn_type);
        if (!(g->emit_lib &&
              (member->method_decl.modifiers & MOD_PUBLIC) != 0))
            zan_set_module_local(fn);

        char resume_name[560];
        snprintf(resume_name, sizeof(resume_name), "%s$resume", fn_name);
        w->resume_fn = resume_fn = LLVMAddFunction(g->mod, resume_name, g->co_step_type);
        zan_set_module_local(resume_fn);
    (void)fn; (void)resume_fn; (void)frame_type; (void)total_params;
    (void)type_sym; (void)is_static;
}

/* 发射the ramp, resume state machine and cleanup fn of one async method */
static void emit_async_method_ir(zan_irgen_t *g, method_body_work_t *w) {
    zan_ast_node_t *member = w->member;
    zan_symbol_t *type_sym = w->type_sym;
    LLVMValueRef fn = w->fn;
    LLVMTypeRef *param_types = w->param_types;
    int param_count = w->param_count;
    int param_offset = w->param_offset;
    bool is_static = w->is_static;
    zan_ast_list_t *saved_mtps = g->cur_mtps;
    zan_type_t **saved_mbind = g->cur_mbind;
    zan_type_t *saved_inst = g->cur_inst;
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    g->cur_mtps = w->mtps;
    g->cur_mbind = w->mbind;
    g->cur_inst = w->cur_inst;
        LLVMValueRef ramp_fn = fn;
        LLVMValueRef resume_fn = w->resume_fn;
        LLVMTypeRef frame_type = w->frame_type;
        LLVMTypeRef frame_ptr_ty = LLVMPointerType(frame_type, 0);
        LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        int total_params = param_count + param_offset;

        /* 内部辅助逻辑 */
        LLVMValueRef cleanup_fn;
        {
            size_t rn_len = 0;
            const char *rn = LLVMGetValueName2(ramp_fn, &rn_len);
            char cleanup_name[560];
            snprintf(cleanup_name, sizeof(cleanup_name), "%.*s$cleanup",
                     (int)rn_len, rn ? rn : "");
            cleanup_fn = LLVMAddFunction(g->mod, cleanup_name, g->co_step_type);
            LLVMSetLinkage(cleanup_fn, LLVMInternalLinkage);
        }

        /* ---- ramp ---- */
        LLVMBasicBlockRef ramp_entry = LLVMAppendBasicBlockInContext(g->ctx, ramp_fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, ramp_entry);
        /* 内部辅助实现 */
        di_set_loc(g, member->loc);
        LLVMTypeRef malloc_ty = LLVMGlobalGetValueType(g->fn_malloc);
        LLVMValueRef fsize = LLVMSizeOf(frame_type);
        LLVMValueRef raw = zan_call2(g->builder, malloc_ty, g->fn_malloc, &fsize, 1, "frame.raw");
        zan_irgen_emit_oom_check(g, ramp_fn, raw);
        /* 模块核心语义抽象与接口调用契约 */
        {
            LLVMTypeRef i8ptr0 = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef memset_ty = LLVMFunctionType(i8ptr0,
                (LLVMTypeRef[]){ i8ptr0, LLVMInt32TypeInContext(g->ctx),
                                 LLVMInt64TypeInContext(g->ctx) }, 3, 0);
            LLVMValueRef memset_fn = LLVMGetNamedFunction(g->mod, "memset");
            if (!memset_fn) memset_fn = LLVMAddFunction(g->mod, "memset", memset_ty);
            zan_call2(g->builder, memset_ty, memset_fn,
                (LLVMValueRef[]){ raw, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0),
                                  fsize }, 3, "");
        }
        LLVMValueRef rframe = LLVMBuildBitCast(g->builder, raw, frame_ptr_ty, "frame");
        int this_owned = 0;
        zan_type_t *this_owned_type = NULL;
        LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0),
            LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_STATE, "st"));
        LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0),
            LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_DONE, "dn"));
        LLVMBuildStore(g->builder, LLVMConstNull(i8ptr),
            LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_AWAITER, "aw"));
        LLVMBuildStore(g->builder, LLVMConstNull(g->co_step_ptr),
            LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_AWAITER_STEP, "aws"));
        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0),
            LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_RESULT, "rs"));
        LLVMBuildStore(g->builder, cleanup_fn,
            LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_CLEANUP, "cl"));
        /* 内部辅助逻辑 */
        LLVMBuildStore(g->builder, resume_fn,
            LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_SELF_STEP, "selfstep"));
        for (int k = 0; k < total_params; k++) {
            LLVMValueRef pv = LLVMGetParam(ramp_fn, (unsigned)k);
            LLVMValueRef slot = LLVMBuildStructGEP2(g->builder, frame_type, rframe,
                (unsigned)(ASYNC_FRAME_FIRST_PARAM + k), "arg");
            LLVMBuildStore(g->builder, pv, slot);
            /* 内部辅助实现 */
            if (k < param_offset) {
                zan_type_t *rt = type_sym ? type_sym->type : NULL;
                if (is_rc_managed_type(rt) &&
                    LLVMGetTypeKind(LLVMTypeOf(pv)) == LLVMPointerTypeKind) {
                    emit_rc_retain_for_type(g, rt, pv);
                    this_owned = 1;
                    this_owned_type = rt;
                }
            } else {
                zan_ast_node_t *pn = member->method_decl.params.items[k - param_offset];
                if (!pn->param.by_ref) {
                    zan_type_t *pt = resolve_type_ctx(g, pn->param.type);
                    if (is_rc_managed_type(pt) &&
                        LLVMGetTypeKind(LLVMTypeOf(pv)) == LLVMPointerTypeKind) {
                        emit_rc_retain_for_type(g, pt, pv);
                    } else if (pt && pt->kind == TYPE_STRUCT &&
                               type_contains_collection_rc(g, pt, 0)) {
                        emit_collection_value_retain(g, pt, pv, 0);
                    }
                }
            }
        }
        /* 模块核心语义抽象与接口调用契约 */
        for (int k = 0; k < w->alocal_count; k++) {
            async_local_t *al = &w->alocals[k];
            if (!al->boxed) continue;
            LLVMValueRef cell = emit_box_cell(g, al->decl->loc,
                map_type(g, al->ztype), al->ztype, NULL);
            LLVMValueRef tagged = LLVMBuildIntToPtr(g->builder,
                LLVMBuildOr(g->builder,
                    LLVMBuildPtrToInt(g->builder, cell, i64, "fl.box.i"),
                    LLVMConstInt(i64, ZAN_CLOSURE_TAG, 0), "fl.box.tag"),
                i8ptr, "fl.box.owner");
            LLVMBuildStore(g->builder, tagged,
                LLVMBuildStructGEP2(g->builder, frame_type, rframe,
                    (unsigned)al->frame_index, "fl.box.slot"));
        }
        LLVMBuildRet(g->builder, raw);

        /* ---- resume ---- */
        LLVMBasicBlockRef res_entry = LLVMAppendBasicBlockInContext(g->ctx, resume_fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, res_entry);
        di_set_loc(g, member->loc);
        LLVMValueRef fparam = LLVMGetParam(resume_fn, 0);
        LLVMValueRef sframe = LLVMBuildBitCast(g->builder, fparam, frame_ptr_ty, "frame");

        local_scope_t *locals = local_scope_new(g->arena);

        /* 编译器代码生成与运行时系统底层调用契约 */
        int alocal_count = w->alocal_count;
        int slot_total = total_params + alocal_count;
        zan_async_slot_t *slots = (zan_async_slot_t *)zan_arena_alloc(g->arena,
            sizeof(zan_async_slot_t) * (size_t)(slot_total > 0 ? slot_total : 1));
        int si = 0;
        LLVMValueRef res_this = NULL;
        if (!is_static) {
            res_this = LLVMBuildAlloca(g->builder, param_types[0], "this");
            slots[si].slot_alloca = res_this;
            slots[si].llvm = param_types[0];
            slots[si].frame_index = ASYNC_FRAME_FIRST_PARAM;
            si++;
        }
        for (int k = 0; k < param_count; k++) {
            zan_ast_node_t *param = member->method_decl.params.items[k];
            LLVMTypeRef pty = param_types[k + param_offset];
            LLVMValueRef pa = LLVMBuildAlloca(g->builder, pty, "p");
            zan_type_t *pt = resolve_type_ctx(g, param->param.type);
            /* 编译期中间表示与代码生成内部规范 */
            LLVMValueRef binding = param->param.by_ref
                ? LLVMBuildLoad2(g->builder, pty, pa, "p.ref") : pa;
            local_add(locals, param->param.name, binding, pt);
            locals->vars[locals->count - 1].byref_slot = param->param.by_ref;
            if (param->param.by_ref && is_rc_managed_type(pt))
                locals->vars[locals->count - 1].arc_owned = 1;
            if (pt && pt->kind == TYPE_STRING)
                locals->vars[locals->count - 1].opaque_string = 1;
            /* 内部辅助逻辑 */
            if (!param->param.by_ref && is_rc_managed_type(pt) &&
                LLVMGetTypeKind(pty) == LLVMPointerTypeKind) {
                locals->vars[locals->count - 1].arc_owned = 1;
            } else if (!param->param.by_ref && pt && pt->kind == TYPE_STRUCT &&
                       type_contains_collection_rc(g, pt, 0)) {
                locals->vars[locals->count - 1].struct_rc = 1;
            }
            slots[si].slot_alloca = pa;
            slots[si].llvm = pty;
            slots[si].frame_index = ASYNC_FRAME_FIRST_PARAM + param_offset + k;
            si++;
        }
        for (int k = 0; k < alocal_count; k++) {
            LLVMValueRef la = LLVMBuildAlloca(g->builder, w->alocals[k].llvm, "fl");
            /* 内部辅助实现 */
            zan_istr_t fname;
            if (w->alocals[k].decl) {
                char flbuf[80];
                int fln = snprintf(flbuf, sizeof(flbuf), "$fl%d.%s", k,
                    w->alocals[k].name.str ? w->alocals[k].name.str : "l");
                char *fp = (char *)zan_arena_alloc(g->arena, (size_t)fln + 1);
                memcpy(fp, flbuf, (size_t)fln + 1);
                fname.str = fp;
                fname.len = (uint32_t)fln;
            } else {
                fname = w->alocals[k].name;
            }
            local_add(locals, fname, la, w->alocals[k].ztype);
            /* 内部辅助逻辑 */
            local_var_t *lv = &locals->vars[locals->count - 1];
            lv->async_decl = w->alocals[k].decl;
            lv->async_role = w->alocals[k].role;
            if (w->alocals[k].boxed) {
                LLVMValueRef tagged = LLVMBuildLoad2(g->builder, i8ptr, la,
                                                     "fl.box.owner");
                LLVMValueRef cell = emit_closure_untag(g, tagged);
                lv->alloca = box_value_ptr(g, cell,
                                          map_type(g, w->alocals[k].ztype));
                lv->box_cell = cell;
                lv->box_owner_slot = la;
                lv->box_owned = 1;
                /* 编译期中间表示与代码生成内部规范 */
            } else if (!w->alocals[k].no_arc &&
                       is_rc_managed_type(w->alocals[k].ztype) &&
                       LLVMGetTypeKind(w->alocals[k].llvm) == LLVMPointerTypeKind) {
                lv->arc_owned = 1;
            } else if (!w->alocals[k].no_arc && w->alocals[k].ztype &&
                       w->alocals[k].ztype->kind == TYPE_STRUCT &&
                       type_contains_collection_rc(g, w->alocals[k].ztype, 0)) {
                lv->struct_rc = 1;
            }
            slots[si].slot_alloca = la;
            slots[si].llvm = w->alocals[k].llvm;
            slots[si].frame_index = w->alocals[k].frame_index;
            si++;
        }

        LLVMValueRef saved_fn = g->current_fn;
        LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
        zan_type_t *saved_fn_zan_ret = g->current_fn_zan_ret_type;
        LLVMValueRef saved_this = g->current_this;
        zan_symbol_t *saved_type_sym = g->current_type_sym;
        LLVMValueRef saved_async_frame = g->current_async_frame;
        LLVMTypeRef saved_async_frame_type = g->current_async_frame_type;
        LLVMValueRef saved_async_resume_fn = g->current_async_resume_fn;
        zan_ast_node_t *saved_async_body = g->current_async_body;
        zan_type_t *saved_async_ret_type = g->current_async_ret_type;
        LLVMValueRef saved_async_switch = g->current_async_switch;
        int saved_next_state = g->current_async_next_state;
        int saved_sub_base = g->current_async_sub_base;
        int saved_sub_next = g->current_async_sub_next;
        int saved_ret_agg_slot = g->current_async_ret_agg_slot;
        void *saved_slots = (void *)g->current_async_slots;
        int saved_slot_count = g->current_async_slot_count;
        int saved_frame_local_count = g->current_async_frame_local_count;
        LLVMBasicBlockRef saved_complete_bb = g->current_async_complete_bb;
        LLVMValueRef saved_result_phi = g->current_async_result_phi;
        LLVMBasicBlockRef saved_requeue_bb = g->current_async_requeue_bb;
        LLVMBasicBlockRef saved_cancel_bb = g->current_async_cancel_bb;
        LLVMBasicBlockRef saved_rethrow_bb = g->current_async_rethrow_bb;
        LLVMBasicBlockRef saved_sub_rethrow_bb = g->current_async_sub_rethrow_bb;
        LLVMValueRef saved_sub_rethrow_phi_sub = g->current_async_sub_rethrow_phi_sub;
        LLVMValueRef saved_sub_rethrow_phi_ev = g->current_async_sub_rethrow_phi_ev;
        LLVMValueRef saved_state_ptr = g->current_async_state_ptr;
        LLVMValueRef saved_cancel_ptr = g->current_async_cancel_ptr;
        LLVMValueRef saved_self_i8 = g->current_async_self_i8;
        LLVMValueRef saved_self_int = g->current_async_self_int;
        LLVMValueRef saved_child_ptr = g->current_async_child_ptr;
        LLVMValueRef saved_sub_slot_ptr = g->current_async_sub_slot_ptr;
        LLVMValueRef saved_result_ptr = g->current_async_result_ptr;
        LLVMValueRef saved_eh_entry = g->current_async_eh_entry;
        LLVMBasicBlockRef saved_exc_bb = g->current_async_exc_bb;
        LLVMValueRef saved_rearm = g->current_async_rearm_switch;
        int saved_handler_next = g->current_async_handler_next;
        int saved_handler_cap = g->current_async_handler_cap;
        int saved_try_count = g->current_async_try_count;
        int saved_foreach_next = g->current_async_foreach_next;
        int saved_this_owned = g->current_async_this_owned;
        zan_type_t *saved_this_owned_type = g->current_async_this_type;

        g->current_fn = resume_fn;
        g->current_fn_ret_type = LLVMVoidTypeInContext(g->ctx);
        /* 模块核心语义抽象与接口调用契约 */
        int saved_throw_base = g->throw_locals_base;
        int saved_catch_cc = g->catch_cleanup_count;
        int saved_throw_cb = g->throw_catch_base;
        int saved_fin_c = g->finally_count;
        zan_irgen_pending_context_t saved_pending = g->pending;
        int saved_fin_lb = g->finally_loop_base;
        int saved_eh_c = g->eh_armed_count;
        int saved_eh_b = g->eh_armed_base;
        int saved_eh_lb = g->eh_armed_loop_base;
        g->throw_locals_base = 0;
        g->catch_cleanup_count = 0;
        g->throw_catch_base = 0;
        g->finally_count = 0;
        g->pending = (zan_irgen_pending_context_t){0};
        g->finally_loop_base = 0;
        g->eh_armed_base = g->eh_armed_count;
        g->eh_armed_loop_base = g->eh_armed_count;
        g->current_this = is_static ? NULL : res_this;
        g->current_type_sym = type_sym;
        g->current_async_frame = sframe;
        g->current_async_frame_type = frame_type;
        g->current_async_resume_fn = resume_fn;
        g->current_async_body = member->method_decl.body;
        g->current_async_ret_type = concretize(g,
            member->method_decl.return_type
                ? zan_binder_resolve_type(g->binder, member->method_decl.return_type)
                : g->binder->type_void);
        if (g->current_async_ret_type && g->current_async_ret_type->type_arg_count > 0 &&
            is_task_like_type(g->current_async_ret_type)) {
            g->current_async_ret_type = concretize(g, g->current_async_ret_type->type_args[0]);
        }
        g->current_fn_zan_ret_type = g->current_async_ret_type;
        g->current_async_ret_agg_slot = w->ret_agg_slot;
        g->current_async_next_state = 1;
        g->current_async_sub_base = w->sub_base;
        g->current_async_sub_next = 0;
        g->current_async_slots = slots;
        g->current_async_slot_count = slot_total;
        g->current_async_frame_local_count = locals->count;
        g->current_async_complete_bb = NULL;
        g->current_async_result_phi = NULL;
        g->current_async_suspend_ret_bb = NULL;
        g->current_async_requeue_bb = NULL;
        g->current_async_cancel_bb = NULL;
        g->current_async_rethrow_bb = NULL;
        g->current_async_sub_rethrow_bb = NULL;
        g->current_async_sub_rethrow_phi_sub = NULL;
        g->current_async_sub_rethrow_phi_ev = NULL;
        g->current_async_state_ptr = NULL;
        g->current_async_cancel_ptr = NULL;
        g->current_async_self_i8 = NULL;
        g->current_async_self_int = NULL;
        g->current_async_child_ptr = NULL;
        g->current_async_sub_slot_ptr = NULL;
        g->current_async_result_ptr = NULL;
        g->current_async_eh_entry = NULL;
        g->current_async_exc_bb = NULL;
        g->current_async_rearm_switch = NULL;
        g->current_async_handler_next = 0;
        g->current_async_handler_cap = w->handler_cap;
        g->current_async_try_count = w->try_count;
        g->current_async_foreach_next = 0;
        g->current_async_this_owned = this_owned;
        g->current_async_this_type = this_owned_type;

        /* 内部辅助实现 */
        emit_async_eh_prologue(g);

        /* 内部辅助实现 */
        LLVMBuildStore(g->builder,
            LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0)),
            get_async_child_ptr(g));

        /* 内部辅助实现 */
        LLVMValueRef cancel_ptr = get_async_cancel_ptr(g);
        LLVMValueRef cancel_val = LLVMBuildLoad2(g->builder, i32, cancel_ptr, "fr.cancelled");
        LLVMBasicBlockRef can_bb = get_async_cancel_bb(g, locals);
        if (!can_bb) {
            can_bb = LLVMAppendBasicBlockInContext(g->ctx, resume_fn, "co.cancelled");
            LLVMPositionBuilderAtEnd(g->builder, can_bb);
            emit_async_complete(g, locals, NULL);
            g->current_async_cancel_bb = can_bb;
        }
        LLVMBasicBlockRef disp_bb = LLVMAppendBasicBlockInContext(g->ctx, resume_fn, "co.dispatch");
        LLVMBuildCondBr(g->builder,
            zan_icmp(g->builder, LLVMIntNE, cancel_val, LLVMConstInt(i32, 0, 0), "is.cancelled"),
            can_bb, disp_bb);

        LLVMPositionBuilderAtEnd(g->builder, disp_bb);
        LLVMValueRef state = LLVMBuildLoad2(g->builder, i32,
            get_async_state_ptr(g),
            "state");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, resume_fn, "co.start");
        /* 内部辅助逻辑 */
        LLVMValueRef sw = LLVMBuildSwitch(g->builder, state, body_bb, (unsigned)(w->await_count + 1));
        LLVMAddCase(sw, LLVMConstInt(i32, 0, 0), body_bb);
        g->current_async_switch = sw;

        LLVMPositionBuilderAtEnd(g->builder, body_bb);

        if (member->method_decl.body->kind == AST_BLOCK) {
            for (int k = 0; k < member->method_decl.body->block.stmts.count; k++) {
                zan_ast_node_t *bs = member->method_decl.body->block.stmts.items[k];
                emit_stmt(g, bs, locals);
            }
        } else {
            /* 底层系统交互与数据协议契约 */
            check_implicit_narrowing(g, g->current_fn_zan_ret_type,
                infer_expr_type(g, member->method_decl.body, locals),
                member->method_decl.body, "return");
            LLVMValueRef val = emit_expr(g, member->method_decl.body, locals);
            emit_async_complete(g, locals,
                coerce_to_frame_result(g, coerce_async_ret(g, val),
                                       g->current_async_ret_type));
        }

        /* 模块核心语义抽象与接口调用契约 */
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_async_complete(g, locals, NULL);
        }

        /* 内部辅助逻辑 */
        emit_async_exc_epilogue(g, locals);
        emit_async_complete_epilogue(g, locals);
        emit_async_finalize_slots(g);

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
        g->current_async_frame = saved_async_frame;
        g->current_async_frame_type = saved_async_frame_type;
        g->current_async_resume_fn = saved_async_resume_fn;
        g->current_async_body = saved_async_body;
        g->current_async_ret_type = saved_async_ret_type;
        g->current_async_switch = saved_async_switch;
        g->current_async_next_state = saved_next_state;
        g->current_async_sub_base = saved_sub_base;
        g->current_async_sub_next = saved_sub_next;
        g->current_async_ret_agg_slot = saved_ret_agg_slot;
        g->current_async_slots = saved_slots;
        g->current_async_slot_count = saved_slot_count;
        g->current_async_frame_local_count = saved_frame_local_count;
        g->current_async_complete_bb = saved_complete_bb;
        g->current_async_result_phi = saved_result_phi;
        g->current_async_requeue_bb = saved_requeue_bb;
        g->current_async_cancel_bb = saved_cancel_bb;
        g->current_async_rethrow_bb = saved_rethrow_bb;
        g->current_async_sub_rethrow_bb = saved_sub_rethrow_bb;
        g->current_async_sub_rethrow_phi_sub = saved_sub_rethrow_phi_sub;
        g->current_async_sub_rethrow_phi_ev = saved_sub_rethrow_phi_ev;
        g->current_async_state_ptr = saved_state_ptr;
        g->current_async_cancel_ptr = saved_cancel_ptr;
        g->current_async_self_i8 = saved_self_i8;
        g->current_async_self_int = saved_self_int;
        g->current_async_child_ptr = saved_child_ptr;
        g->current_async_sub_slot_ptr = saved_sub_slot_ptr;
        g->current_async_result_ptr = saved_result_ptr;
        g->current_async_eh_entry = saved_eh_entry;
        g->current_async_exc_bb = saved_exc_bb;
        g->current_async_rearm_switch = saved_rearm;
        g->current_async_handler_next = saved_handler_next;
        g->current_async_handler_cap = saved_handler_cap;
        g->current_async_try_count = saved_try_count;
        g->current_async_foreach_next = saved_foreach_next;
        g->current_async_this_owned = saved_this_owned;
        g->current_async_this_type = saved_this_owned_type;

        /* 模块核心语义抽象与接口调用契约 */
        {
            LLVMBasicBlockRef cl_entry =
                LLVMAppendBasicBlockInContext(g->ctx, cleanup_fn, "entry");
            LLVMPositionBuilderAtEnd(g->builder, cl_entry);
            di_clear(g);
            LLVMValueRef saved_cl_fn = g->current_fn;
            g->current_fn = cleanup_fn;
            LLVMValueRef cparam = LLVMGetParam(cleanup_fn, 0);
            LLVMValueRef cframe = LLVMBuildBitCast(g->builder, cparam,
                frame_ptr_ty, "frame");
            if (this_owned && this_owned_type) {
                LLVMValueRef sp = LLVMBuildStructGEP2(g->builder, frame_type,
                    cframe, (unsigned)ASYNC_FRAME_FIRST_PARAM, "cl.this");
                emit_rc_release_for_type(g, this_owned_type,
                    LLVMBuildLoad2(g->builder, param_types[0], sp, "cl.thisv"));
            }
            for (int k = 0; k < param_count; k++) {
                zan_ast_node_t *param = member->method_decl.params.items[k];
                if (param->param.by_ref) continue;
                zan_type_t *pt = resolve_type_ctx(g, param->param.type);
                LLVMTypeRef pty = param_types[k + param_offset];
                if (!type_contains_collection_rc(g, pt, 0)) continue;
                LLVMValueRef sp = LLVMBuildStructGEP2(g->builder, frame_type, cframe,
                    (unsigned)(ASYNC_FRAME_FIRST_PARAM + param_offset + k), "cl.p");
                LLVMValueRef v = LLVMBuildLoad2(g->builder, pty, sp, "cl.pv");
                emit_collection_value_release(g, pt, v, 0);
            }
            for (int k = 0; k < w->alocal_count; k++) {
                async_local_t *al = &w->alocals[k];
                if (al->boxed) {
                    LLVMValueRef sp = LLVMBuildStructGEP2(g->builder, frame_type,
                        cframe, (unsigned)al->frame_index, "cl.box");
                    emit_closure_release(g,
                        LLVMBuildLoad2(g->builder, i8ptr, sp, "cl.box.owner"));
                    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), sp);
                    continue;
                }
                if (al->no_arc || !type_contains_collection_rc(g, al->ztype, 0))
                    continue;
                LLVMValueRef sp = LLVMBuildStructGEP2(g->builder, frame_type, cframe,
                    (unsigned)al->frame_index, "cl.l");
                LLVMValueRef v = LLVMBuildLoad2(g->builder, al->llvm, sp, "cl.lv");
                emit_collection_value_release(g, al->ztype, v, 0);
            }
            emit_async_pending_discard(g, cframe, frame_type, LLVMConstInt(i32, 0, 0));
            /* 内部辅助逻辑 */
            emit_co_cancel_delay(g, cparam);
            zan_emit_frame_free(g, cparam);
            LLVMBuildRetVoid(g->builder);
            g->current_fn = saved_cl_fn;
        }
        LLVMValueRef pending_discard_fn = NULL;
        if (LLVMGetArrayLength(LLVMStructGetTypeAtIndex(frame_type, ASYNC_FRAME_PENDING))) {
            emit_async_pending_discard_body(g, frame_type, w->ret_type);
            pending_discard_fn = async_pending_discard_fn(g, frame_type);
        }
        /* 编译器代码生成与运行时系统底层调用契约 */
        if (saved_bb) LLVMPositionBuilderAtEnd(g->builder, saved_bb);
        else LLVMClearInsertionPosition(g->builder);
        zan_irgen_compact_completed(g, resume_fn);
        zan_irgen_compact_completed(g, ramp_fn);
        zan_irgen_compact_completed(g, cleanup_fn);
        if (pending_discard_fn) zan_irgen_compact_completed(g, pending_discard_fn);
        free(param_types);
    g->cur_mtps = saved_mtps;
    g->cur_mbind = saved_mbind;
    g->cur_inst = saved_inst;
    if (saved_bb) LLVMPositionBuilderAtEnd(g->builder, saved_bb);
    (void)type_sym; (void)fn;
}

/* 编译器代码生成与运行时系统底层调用契约 */
static void own_written_param(zan_irgen_t *g, local_scope_t *locals,
                              zan_ast_node_t *param, zan_type_t *pt,
                              LLVMTypeRef pty, LLVMValueRef pv,
                              zan_ast_node_t *body) {
    if (!pt || pt->kind == TYPE_OBJECT || !is_rc_managed_type(pt)) return;
    if (LLVMGetTypeKind(pty) != LLVMPointerTypeKind) return;
    if (locals && locals->count > 0 &&
        locals->vars[locals->count - 1].box_cell) return;
    if (!body_writes_ident(g, body, param->param.name)) return;
    emit_rc_retain_for_type(g, pt, pv);
    arc_own_local(g, locals);
}

static method_body_work_t *declare_user_methods(zan_irgen_t *g,
                                                 zan_ast_node_t *unit,
                                                 int *out_work_count) {
    /* 内部辅助逻辑 */
    discover_generic_insts(g, unit);

    /* 内部辅助逻辑 */
    int work_cap = 0;
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind == AST_CLASS_DECL || decl->kind == AST_STRUCT_DECL) {
            zan_symbol_t *ts = zan_binder_lookup(g->binder, decl->type_decl.name);
            int variants = 1 + (ts ? generic_variant_count(g, ts) : 0);
            work_cap += decl->type_decl.members.count * variants;
        }
    }
    method_body_work_t *work = NULL;
    int work_count = 0;
    if (work_cap > 0) {
        work = (method_body_work_t *)calloc((size_t)work_cap, sizeof(method_body_work_t));
    }

    /* 底层系统交互与数据协议契约 */
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL) continue;

        /* 核心系统底层抽象与内存语义契约 */
        zan_symbol_t *type_sym = zan_binder_lookup(g->binder, decl->type_decl.name);
        if (!type_sym) continue;

        /* 发射one variant per (erased + each concrete instantiation) */
        int nvar = 0, var_cap = 8;
        zan_type_t **variants =
            (zan_type_t **)malloc((size_t)var_cap * sizeof(*variants));
        variants[nvar++] = NULL;
        if (is_user_generic_sym(type_sym)) {
            for (int gi = 0; gi < g->generic_inst_count; gi++) {
                if (g->generic_insts[gi].type_sym != type_sym) continue;
                if (nvar == var_cap) {
                    var_cap *= 2;
                    variants = (zan_type_t **)realloc(variants,
                        (size_t)var_cap * sizeof(*variants));
                }
                variants[nvar++] = g->generic_insts[gi].inst;
            }
        }

        for (int vi = 0; vi < nvar; vi++) {
        zan_type_t *cur_variant = variants[vi];
        char vsuffix[256];
        vsuffix[0] = '\0';
        if (cur_variant) mangle_inst_suffix(vsuffix, sizeof(vsuffix), cur_variant);

        for (int j = 0; j < decl->type_decl.members.count; j++) {
            zan_ast_node_t *member = decl->type_decl.members.items[j];
            bool is_ctor = (member->kind == AST_CONSTRUCTOR_DECL);
            if (member->kind != AST_METHOD_DECL && !is_ctor) continue;
            /* 内部辅助逻辑 */
            bool is_type_init = is_ctor &&
                (member->method_decl.modifiers & MOD_STATIC) != 0;
            /* 内部辅助逻辑 */
            if (is_type_init && cur_variant) continue;
            /* 内部辅助实现 */
            /* 内部辅助实现 */
            if (method_is_tp_template(g, member)) continue;
            /* 内部辅助逻辑 */
            bool is_extern_decl =
                member->kind == AST_METHOD_DECL && !member->method_decl.body &&
                (zan_ast_method_extern_lib(member).str ||
                 (member->method_decl.modifiers & MOD_EXTERN) != 0);
            if (cur_variant && is_extern_decl)
                continue;

            /* 内部辅助逻辑 */
            if (is_extern_decl) {
                /* 核心系统底层抽象与内存语义契约 */
                int pc = member->method_decl.params.count;
                LLVMTypeRef *pt = (LLVMTypeRef *)calloc((size_t)(pc > 0 ? pc : 1), sizeof(LLVMTypeRef));
                zan_type_t **pzt = (zan_type_t **)calloc((size_t)(pc > 0 ? pc : 1), sizeof(zan_type_t *));
                for (int k = 0; k < pc; k++) {
                    zan_ast_node_t *param = member->method_decl.params.items[k];
                    zan_type_t *ptype = zan_binder_resolve_type(g->binder, param->param.type);
                    pzt[k] = ptype;
                    pt[k] = map_type(g, ptype);
                }
                zan_type_t *rt = member->method_decl.return_type
                    ? zan_binder_resolve_type(g->binder, member->method_decl.return_type)
                    : g->binder->type_void;
                LLVMTypeRef llvm_rt = map_type(g, rt);
                /* 内部辅助逻辑 */
                LLVMTypeRef ft = LLVMFunctionType(llvm_rt, pt, (unsigned)pc,
                    member->method_decl.is_variadic ? 1 : 0);
                /* 底层系统交互与数据协议契约 */
                char ext_name[256];
                zan_istr_t *ep = zan_ast_method_entry_point(member);
                if (ep) {
                    snprintf(ext_name, sizeof(ext_name), "%.*s",
                             (int)ep->len, ep->str);
                } else {
                    snprintf(ext_name, sizeof(ext_name), "%.*s",
                             (int)member->method_decl.name.len,
                             member->method_decl.name.str);
                }
                if (strncmp(ext_name, "zan_file_", 9) == 0 ||
                    strncmp(ext_name, "zan_pkg_", 8) == 0) {
                    /* 核心系统底层抽象与内存语义契约 */
                    g->uses_file_runtime = true;
                }
                /* 核心系统底层抽象与内存语义契约 */
                if (strncmp(ext_name, "zan_atomic_int_", 15) == 0 ||
                    strncmp(ext_name, "zan_shared_", 11) == 0 ||
                    strncmp(ext_name, "zan_thread_", 11) == 0 ||
                    strncmp(ext_name, "zan_dispatch_", 13) == 0 ||
                    strncmp(ext_name, "zan_monotonic_", 14) == 0 ||
                    strncmp(ext_name, "zan_stopwatch_", 14) == 0 ||
                    strncmp(ext_name, "zan_monitor_", 12) == 0 ||
                    strncmp(ext_name, "zan_mmap_", 9) == 0 ||
                    strncmp(ext_name, "zan_exe_dir_", 12) == 0 ||
                    strncmp(ext_name, "zan_dir_list_", 13) == 0 ||
                    strncmp(ext_name, "zan_plat_", 9) == 0) {
                    if (getenv("ZAN_TRACE_SYNC"))
                        fprintf(stderr, "[sync-flag] %s\n", ext_name);
                    g->uses_sync_runtime = true;
                }
                /* 内部辅助逻辑 */
                if (strncmp(ext_name, "zan_io_", 7) == 0) {
                    g->uses_socket_async = true;
                }
                if (strncmp(ext_name, "zan_gate_", 9) == 0) {
                    g->uses_socket_async = true;
                }
                if (strncmp(ext_name, "zan_timer_", 10) == 0) {
                    g->uses_timer_runtime = true;
                }
                if (strncmp(ext_name, "zan_embed_", 10) == 0) {
                    g->uses_embed_api = true;
                    /* 底层系统交互与数据协议契约 */
                    if (strncmp(ext_name, "zan_embed_decode", 16) == 0 ||
                        strncmp(ext_name, "zan_embed_rawlen", 16) == 0) {
                        g->uses_inflate = true;
                    }
                }
                /* 模块核心语义抽象与接口调用契约 */
                /* 内部辅助实现 */
                LLVMValueRef efn = NULL;
                if (!member->method_decl.is_variadic)
                    efn = abi_extern_thunk(g, ext_name, ft);
                if (!efn) efn = LLVMGetNamedFunction(g->mod, ext_name);
                if (!efn) {
                    efn = LLVMAddFunction(g->mod, ext_name, ft);
                    abi_add_int_ext_attrs(g, efn, rt, pzt, pc);
                }
                free(pzt);
                /* 核心系统底层抽象与内存语义契约 */
                zan_symbol_t *method_sym = method_sym_for_decl(type_sym, member);
                if (method_sym) {
                    irgen_register_function(g, method_sym, efn, ft);
                }
                /* 核心系统底层抽象与内存语义契约 */
                zan_istr_t ext_lib = zan_ast_method_extern_lib(member);
                if (ext_lib.str) {
                    bool already = false;
                    for (int li = 0; li < g->extern_lib_count; li++) {
                        if (g->extern_libs[li].len == ext_lib.len &&
                            memcmp(g->extern_libs[li].str, ext_lib.str,
                                   ext_lib.len) == 0) {
                            already = true;
                            break;
                        }
                    }
                    if (!already &&
                        ZAN_TAB_ENSURE(g->extern_libs, g->extern_lib_count,
                                       g->extern_lib_cap, 16)) {
                        g->extern_libs[g->extern_lib_count++] = ext_lib;
                    }
                }
                /* 内部辅助逻辑 */
                if (ext_lib.str) {
                    zan_istr_t *ep = zan_ast_method_entry_point(member);
                    zan_istr_t sym = ep ? *ep : member->method_decl.name;
                    bool seen = false;
                    for (int fi = 0; fi < g->extern_fn_count; fi++) {
                        if (g->extern_fns[fi].name.len == sym.len &&
                            memcmp(g->extern_fns[fi].name.str, sym.str, sym.len) == 0) {
                            seen = true;
                            break;
                        }
                    }
                    if (!seen &&
                        ZAN_TAB_ENSURE(g->extern_fns, g->extern_fn_count,
                                       g->extern_fn_cap, 128)) {
                        g->extern_fns[g->extern_fn_count].lib = ext_lib;
                        g->extern_fns[g->extern_fn_count].name = sym;
                        g->extern_fn_count++;
                    }
                }
                free(pt);
                continue;
            }

            if (!member->method_decl.body) continue;

            /* 核心系统底层抽象与内存语义契约 */
            bool is_static = (!is_ctor || is_type_init) &&
                (member->method_decl.modifiers & MOD_STATIC) != 0;
            if (is_static && member->method_decl.name.len == 4 &&
                memcmp(member->method_decl.name.str, "Main", 4) == 0 &&
                !(member->method_decl.modifiers & MOD_ASYNC)) continue;

            /* 内部辅助逻辑 */
            char fn_name[512];
            if (is_type_init) {
                snprintf(fn_name, sizeof(fn_name), "%.*s_cctor",
                         (int)decl->type_decl.name.len, decl->type_decl.name.str);
            } else if (is_ctor) {
                snprintf(fn_name, sizeof(fn_name), "%.*s_ctor%s",
                         (int)decl->type_decl.name.len, decl->type_decl.name.str,
                         vsuffix);
            } else {
                snprintf(fn_name, sizeof(fn_name), "%.*s_%.*s%s",
                         (int)decl->type_decl.name.len, decl->type_decl.name.str,
                         (int)member->method_decl.name.len, member->method_decl.name.str,
                         vsuffix);
                /* 内部辅助逻辑 */
                {
                    char base_name[512];
                    int oi = 2;
                    snprintf(base_name, sizeof(base_name), "%s", fn_name);
                    while (LLVMGetNamedFunction(g->mod, fn_name)) {
                        snprintf(fn_name, sizeof(fn_name), "%s$o%d",
                                 base_name, oi++);
                    }
                }
            }

            /* 核心系统底层抽象与内存语义契约 */
            int param_count = member->method_decl.params.count;
            int total_params = is_static ? param_count : param_count + 1;
            LLVMTypeRef *param_types = (LLVMTypeRef *)calloc((size_t)(total_params > 0 ? total_params : 1), sizeof(LLVMTypeRef));

            int param_offset = 0;
            if (!is_static) {
                /* 核心系统底层抽象与内存语义契约 */
                LLVMTypeRef struct_type = get_struct_llvm_type(g, type_sym);
                param_types[0] = struct_type ? LLVMPointerType(struct_type, 0)
                                             : LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                param_offset = 1;
            }

            /* 内部辅助逻辑 */
            for (int k = 0; k < param_count; k++) {
                zan_ast_node_t *param = member->method_decl.params.items[k];
                zan_type_t *pt = zan_binder_resolve_type(g->binder, param->param.type);
                if (cur_variant) pt = subst_type_param_deep(g, pt, cur_variant);
                param_types[k + param_offset] = param->param.by_ref
                    ? LLVMPointerType(map_type(g, pt), 0)
                    : map_type(g, pt);
            }

            zan_type_t *ret_type = is_ctor ? g->binder->type_void
                : (member->method_decl.return_type
                    ? zan_binder_resolve_type(g->binder, member->method_decl.return_type)
                    : g->binder->type_void);
            if (cur_variant) ret_type = subst_type_param_deep(g, ret_type, cur_variant);
            LLVMTypeRef llvm_ret = map_type(g, ret_type);
            /* 编译期中间表示与代码生成内部规范 */
            bool is_async = !is_ctor && (member->method_decl.modifiers & MOD_ASYNC) != 0;

            LLVMTypeRef fn_type = NULL;
            LLVMValueRef fn = NULL;
            LLVMValueRef resume_fn = NULL;
            LLVMTypeRef frame_type = NULL;
            int a_await_count = 0;
            async_local_t *a_locals = NULL;
            int a_local_count = 0;
            int a_sub_base = 0;
	            int a_ret_agg_slot = -1;
	            int a_handler_cap = 1;
	            int a_try_count = 0;

	            if (is_async) {
	                g->has_async_work = true;
	                method_body_work_t adecl;
	                memset(&adecl, 0, sizeof(adecl));
	                adecl.member = member;
	                adecl.type_sym = type_sym;
	                adecl.is_static = is_static;
	                adecl.param_types = param_types;
	                adecl.param_count = param_count;
	                adecl.param_offset = param_offset;
	                adecl.ret_type = ret_type;
	                adecl.llvm_ret = llvm_ret;
	                adecl.cur_inst = cur_variant;
	                declare_async_method(g, &adecl, fn_name);
	                fn = adecl.fn;
	                fn_type = adecl.fn_type;
	                resume_fn = adecl.resume_fn;
	                frame_type = adecl.frame_type;
	                a_await_count = adecl.await_count;
	                a_locals = adecl.alocals;
	                a_local_count = adecl.alocal_count;
	                a_sub_base = adecl.sub_base;
	                a_ret_agg_slot = adecl.ret_agg_slot;
	                a_handler_cap = adecl.handler_cap;
	                a_try_count = adecl.try_count;
            } else {
                fn_type = LLVMFunctionType(llvm_ret, param_types, (unsigned)total_params, 0);
                fn = LLVMAddFunction(g->mod, fn_name, fn_type);
                if (!(g->emit_lib &&
                      (member->method_decl.modifiers & MOD_PUBLIC) != 0))
                    zan_set_module_local(fn);
            }

            /* 核心系统底层抽象与内存语义契约 */
            if (is_type_init) {
                /* 模块核心语义抽象与接口调用契约 */
            } else if (is_ctor) {
                if (cur_variant) {
                    add_generic_ctor(g, type_sym, member,
                                     cur_variant->type_args,
                                     cur_variant->type_arg_count, param_count,
                                     fn, fn_type);
                } else {
                    g->ctors = irgen_grow(g->ctors, &g->ctor_cap,
                                          g->ctor_count + 1,
                                          sizeof(*g->ctors));
                    g->ctors[g->ctor_count].type_sym = type_sym;
                    g->ctors[g->ctor_count].decl = member;
                    g->ctors[g->ctor_count].fn = fn;
                    g->ctors[g->ctor_count].fn_type = fn_type;
                    g->ctors[g->ctor_count].param_count = param_count;
                    g->ctor_count++;
                }
            } else {
                zan_symbol_t *method_sym = method_sym_for_decl(type_sym, member);
                if (cur_variant) {
                    add_generic_fn(g, method_sym, cur_variant->type_args,
                                   cur_variant->type_arg_count, fn, fn_type);
                } else {
                    irgen_register_function(g, method_sym, fn, fn_type);
                }
            }

            /* 内部辅助逻辑 */
            if (!cur_variant && class_member_uses_tp(g, decl, member)) {
                emit_tp_erased_stub(g, fn);
                if (resume_fn) emit_tp_erased_stub(g, resume_fn);
                free(param_types);
                continue;
            }

            /* 内部辅助逻辑 */
            if (work && work_count < work_cap) {
                work[work_count].member = member;
                work[work_count].type_sym = type_sym;
                work[work_count].fn = fn;
                work[work_count].param_types = param_types;
                work[work_count].param_count = param_count;
                work[work_count].param_offset = param_offset;
                work[work_count].is_static = is_static;
                work[work_count].llvm_ret = llvm_ret;
                work[work_count].ret_type = ret_type;
                work[work_count].is_async = is_async;
                work[work_count].resume_fn = resume_fn;
                work[work_count].frame_type = frame_type;
                work[work_count].await_count = a_await_count;
                work[work_count].alocals = a_locals;
                work[work_count].alocal_count = a_local_count;
	                work[work_count].sub_base = a_sub_base;
	                work[work_count].ret_agg_slot = a_ret_agg_slot;
	                work[work_count].handler_cap = a_handler_cap;
	                work[work_count].try_count = a_try_count;
	                work[work_count].cur_inst = cur_variant;
                work_count++;
            } else {
                free(param_types);
            }
        }
        } /* 核心系统底层抽象与内存语义契约 */
        free(variants);
    }

    *out_work_count = work_count;
    return work;
}

/* 内部辅助逻辑 */
typedef struct {
    LLVMValueRef key; /* 核心系统底层抽象与内存语义契约 */
    int idx;
} work_fn_slot_t;

typedef struct {
    work_fn_slot_t *slots;
    int cap; /* 核心系统底层抽象与内存语义契约 */
    const method_body_work_t *work;
    int work_count;
} work_fn_index_t;

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
    if (!ix->slots) return; /* 底层系统交互与数据协议契约 */
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

/* 内部辅助逻辑 */
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

/* 编译器代码生成与运行时系统底层调用契约 */
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

/* 内部辅助逻辑 */
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

/* 内部辅助实现 */
static bool body_has_live_use(LLVMValueRef fn, const unsigned char *live,
                              const work_fn_index_t *ix) {
    for (LLVMUseRef u = LLVMGetFirstUse(fn); u; u = LLVMGetNextUse(u)) {
        LLVMValueRef user = LLVMGetUser(u);
        /* 内部辅助实现 */
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
                    /* 编译器代码生成与运行时系统底层调用契约 */
                    continue;
                }
            }
            return true;
        }
        LLVMValueRef parent = LLVMIsAInstruction(user)
            ? LLVMGetBasicBlockParent(LLVMGetInstructionParent(user)) : NULL;
        if (!parent) return true;
        /* 内部辅助逻辑 */
        int i = work_fn_index_lookup(ix, parent);
        if (i < 0 || live[i] == 2) return true;
    }
    return false;
}

static void emit_user_method_bodies(zan_irgen_t *g, method_body_work_t *work,
                                    int work_count, unsigned char *live) {
    /* 内部辅助逻辑 */
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
        /* 内部辅助逻辑 */
        g->cur_inst = work[w].cur_inst;

        /* 内部辅助逻辑 */
        if (work[w].is_async) {
            emit_async_method_ir(g, &work[w]);
            continue;
        }

        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, entry);
        /* 模块核心语义抽象与接口调用契约 */
        di_set_loc(g, member->loc);

        local_scope_t *locals = local_scope_new(g->arena);

        if (!is_static) {
            /* 核心系统底层抽象与内存语义契约 */
            this_alloca = LLVMBuildAlloca(g->builder, param_types[0], "this");
            LLVMBuildStore(g->builder, LLVMGetParam(fn, 0), this_alloca);
        }

        /* 核心系统底层抽象与内存语义契约 */
        for (int k = 0; k < param_count; k++) {
            zan_ast_node_t *param = member->method_decl.params.items[k];
            zan_type_t *pt = zan_binder_resolve_type(g->binder, param->param.type);
            if (g->cur_inst) pt = subst_type_param_deep(g, pt, g->cur_inst);
            if (param->param.by_ref) {
                /* 内部辅助逻辑 */
                local_add(locals, param->param.name,
                          LLVMGetParam(fn, (unsigned)(k + param_offset)), pt);
                if (pt && pt->kind == TYPE_STRING)
                    locals->vars[locals->count - 1].opaque_string = 1;
                /* 内部辅助实现 */
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
            /* 编译器代码生成与运行时系统底层调用契约 */
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

        /* 内部辅助逻辑 */
        if (member->kind == AST_CONSTRUCTOR_DECL && !is_static) {
            bool this_init = member->method_decl.has_this_init;
            bool initializer_target_called = false;
            zan_symbol_t *target_sym = NULL;
            if (this_init) {
                target_sym = type_sym;
            } else if (type_sym->type && type_sym->type->base_type &&
                       type_sym->type->base_type->sym) {
                target_sym = type_sym->type->base_type->sym;
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
                    if (this_init || member->method_decl.has_base_init) {
                        zan_diag_emit(g->diag, DIAG_ERROR, member->loc,
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
                zan_type_t *recv_type = g->cur_inst ? g->cur_inst : type_sym->type;
                zan_type_t *base_type = type_sym->type
                    ? type_sym->type->base_type : NULL;
                if (base_type && base_type->sym && !initializer_target_called) {
                    emit_implicit_field_initializers(
                        g, base_type->sym, base_type, thisv, locals);
                }
                emit_decl_field_initializers(
                    g, type_sym, recv_type, thisv, locals);
            }
        }

        /* 核心系统底层抽象与内存语义契约 */
        if (member->method_decl.body->kind == AST_BLOCK) {
            for (int k = 0; k < member->method_decl.body->block.stmts.count; k++) {
                emit_stmt(g, member->method_decl.body->block.stmts.items[k], locals);
            }
        } else {
            /* 核心系统底层抽象与内存语义契约 */
            check_implicit_narrowing(g, g->current_fn_zan_ret_type,
                infer_expr_type(g, member->method_decl.body, locals),
                member->method_decl.body, "return");
            LLVMValueRef val = emit_expr(g, member->method_decl.body, locals);
            /* 核心系统底层抽象与内存语义契约 */
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

        /* 核心系统底层抽象与内存语义契约 */
        LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
        if (!LLVMGetBasicBlockTerminator(cur_bb)) {
            emit_release_owned_locals(g, locals);
            if (ret_type->kind == TYPE_VOID) {
                LLVMBuildRetVoid(g->builder);
            } else {
                LLVMBuildRet(g->builder, LLVMConstNull(llvm_ret));
            }
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

        /* 内部辅助实现 */
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
            if (!has_tparams && !owner_generic) {
                member->method_decl.body = NULL;
            }
        }
        /* 模块核心语义抽象与接口调用契约 */
        if (g->function_compactor) LLVMClearInsertionPosition(g->builder);
        zan_irgen_compact_completed(g, fn);
        if (zan_diag_has_errors(g->diag)) break;
    }

    g->cur_inst = NULL;
}

/* 内部辅助逻辑 */

/* 模块核心语义抽象与接口调用契约 */

typedef struct {
    zan_irgen_t    *g;
    zan_ast_node_t *body;
    zan_ast_list_t *tps;
    /* 模块核心语义抽象与接口调用契约 */
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
        if (!grown) return;      /* 底层系统交互与数据协议契约 */
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

/* 底层系统交互与数据协议契约 */
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
        /* 内部辅助逻辑 */
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

/* 内部辅助逻辑 */
static bool method_is_tp_template(zan_irgen_t *g, zan_ast_node_t *member) {
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

/* 内部辅助实现 */
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

/* 内部辅助逻辑 */
static bool mspec_owner_eq(zan_type_t *a, zan_type_t *b) {
    if (a == b) return true;
    if (!a || !b || a->sym != b->sym) return false;
    return type_arglists_equal(a->type_args, a->type_arg_count,
                               b->type_args, b->type_arg_count);
}

static int get_or_create_method_spec(zan_irgen_t *g, zan_symbol_t *msym,
                                     zan_type_t **bind, int bindc,
                                     zan_type_t *owner_inst) {
    if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return -1;
    zan_ast_node_t *member = msym->decl;
    if (!member->method_decl.body) return -1;
    /* 内部辅助逻辑 */
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
    /* 内部辅助逻辑 */
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

    /* 模块核心语义抽象与接口调用契约 */
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
        /* 内部辅助逻辑 */
        air = (method_body_work_t *)zan_arena_alloc(g->arena, sizeof(*air));
        memset(air, 0, sizeof(*air));
        air->member = member;
        air->type_sym = type_sym;
        air->is_static = spec_static;
        air->param_types = param_types;   /* 核心系统底层抽象与内存语义契约 */
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

/* 内部辅助逻辑 */
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
    /* 内部辅助逻辑 */
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
        /* 内部辅助逻辑 */
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
                /* 底层系统交互与数据协议契约 */
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

/* 模块核心语义抽象与接口调用契约 */
static void emit_pending_method_specs(zan_irgen_t *g) {
    while (g->method_spec_emitted < g->method_spec_count) {
        int i = g->method_spec_emitted++;
        emit_method_spec_body(g, i);
        if (zan_diag_has_errors(g->diag)) break;
    }
}

/* 底层系统交互与数据协议契约 */
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

zan_status_t zan_irgen_emit(zan_irgen_t *g, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return ZAN_ERROR;
    g_di_emit_ctx = g; /* 底层系统交互与数据协议契约 */
    di_debug_types_reset(); /* 核心系统底层抽象与内存语义契约 */
    if (g->publish_mode && !g->emit_debug && !g->function_compactor) {
        char error[4096];
        g->function_compactor = zan_irgen_compactor_create(
            g->mod, error, sizeof(error));
        if (!g->function_compactor) {
            zan_diag_emit(g->diag, DIAG_ERROR, unit->loc,
                          "LLVM function compactor initialization failed: %s", error);
            return ZAN_ERROR;
        }
    }

    /* 内部辅助逻辑 */
    discover_generic_insts(g, unit);

    /* 底层系统交互与数据协议契约 */
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind == AST_STRUCT_DECL || decl->kind == AST_CLASS_DECL) {
            zan_symbol_t *sym = zan_binder_lookup(g->binder, decl->type_decl.name);
            if (sym) register_struct_type(g, sym);
        }
    }

    /* 内部辅助逻辑 */
    int work_count = 0;
    method_body_work_t *work = declare_user_methods(g, unit, &work_count);
    if (zan_diag_has_errors(g->diag)) { free(work); return ZAN_ERROR; }

    /* 内部辅助逻辑 */
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
    if (zan_diag_has_errors(g->diag)) {
        free(live);
        free(work);
        return ZAN_ERROR;
    }

    /* 底层系统交互与数据协议契约 */
    {
        zan_ast_node_t *design_main = NULL;
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
                    zan_symbol_t *main_type_sym =
                        zan_binder_lookup(g->binder, decl->type_decl.name);
                    emit_main_method(g, member, main_type_sym, unit);
                    goto done;
                }
                if (!design_main && member->method_decl.name.len == 12 &&
                    memcmp(member->method_decl.name.str, "__DesignMain",
                           12) == 0) {
                    design_main = member;
                }
            }
        }
        if (design_main) {
            /* 编译器代码生成与运行时系统底层调用契约 */
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
    }
done:
    ;
    /* 编译期中间表示与代码生成内部规范 */
    if (g->function_compactor) {
        LLVMClearInsertionPosition(g->builder);
        LLVMValueRef main_fn = LLVMGetNamedFunction(g->mod, "main");
        if (main_fn) zan_irgen_compact_completed(g, main_fn);
    }
    /* 模块核心语义抽象与接口调用契约 */
    emit_pending_method_specs(g);
    if (zan_diag_has_errors(g->diag)) {
        free(live);
        free(work);
        return ZAN_ERROR;
    }

    /* 内部辅助逻辑 */
    if (prune_stdlib_bodies) emit_vtables(g);
    work_fn_index_t work_ix = { 0 };
    work_fn_index_build(&work_ix, work, work_count);
    int pending;
    do {
        pending = 0;
        for (int w = 0; w < work_count; w++) {
            if (live[w]) continue;
            zan_ast_node_t *member = work[w].member;
            /* 模块核心语义抽象与接口调用契约 */
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
            }
            bool root = (g->emit_lib &&
                         ((member->method_decl.modifiers & MOD_PUBLIC) ||
                          member->kind == AST_CONSTRUCTOR_DECL)) ||
                        is_refl_root;
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
        /* 底层系统交互与数据协议契约 */
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
    /* 内部辅助逻辑 */
    di_clear(g); /* 模块核心语义抽象与接口调用契约 */
    emit_all_class_releases(g);
    emit_site_live_tables(g);
    emit_site_dtor_table(g);
    emit_site_tyname_table(g);
    emit_site_meta_table(g);
    /* 内部辅助逻辑 */
    zan_irgen_emit_arc_desc_init(g);
    if (!prune_stdlib_bodies) emit_vtables(g);
    /* 内部辅助逻辑 */
    refl_finalize_mtabs(g);
    /* 内部辅助逻辑 */
    if (g->emit_lib && g->emit_shared && g->target_is_windows)
        emit_windows_dll_main(g);
    /* 核心系统底层抽象与内存语义契约 */
    zan_irgen_emit_string_deobf(g);
    /* 内部辅助实现 */
    if (g->tid_name_reg_global) {
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        unsigned n = (unsigned)g->tid_name_count;
        LLVMTypeRef reg_ty = LLVMArrayType(g->tid_name_reg_ent_ty, n + 1);
        LLVMValueRef newg = LLVMAddGlobal(g->mod, reg_ty,
                                          LLVMGetValueName(g->tid_name_reg_global));
        LLVMSetLinkage(newg, LLVMInternalLinkage);
        /* 内部辅助逻辑 */
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
    /* 核心系统底层抽象与内存语义契约 */
    if (zan_diag_has_errors(g->diag)) {
        return ZAN_ERROR;
    }
    /* 模块核心语义抽象与接口调用契约 */
    if (g->emit_debug && g->di_builder) {
        LLVMSetCurrentDebugLocation2(g->builder, NULL);
        LLVMDIBuilderFinalize(g->di_builder);
    }
    /* 内部辅助逻辑 */
    if (g->function_compactor) {
        LLVMClearInsertionPosition(g->builder);
        for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
             fn = LLVMGetNextFunction(fn)) {
            if (LLVMIsDeclaration(fn)) continue;
            zan_irgen_compact_completed(g, fn);
            if (zan_diag_has_errors(g->diag)) break;
        }
        /* 模块核心语义抽象与接口调用契约 */
        zan_irgen_compactor_destroy((zan_irgen_compactor_t *)g->function_compactor);
        g->function_compactor = NULL;
        if (zan_diag_has_errors(g->diag)) return ZAN_ERROR;
    }
    /* 核心系统底层抽象与内存语义契约 */
    char *error = NULL;
    if (LLVMVerifyModule(g->mod, LLVMReturnStatusAction, &error)) {
        /* 内部辅助逻辑 */
        const char *culprit = NULL;
        for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
             fn = LLVMGetNextFunction(fn)) {
            if (LLVMIsDeclaration(fn)) continue;
            if (LLVMVerifyFunction(fn, LLVMReturnStatusAction)) {
                culprit = LLVMGetValueName(fn);
                break;
            }
        }
        zan_diag_emit(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0),
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
    if (!path) {
        int rc = fputs(ir, stdout);
        LLVMDisposeMessage(ir);
        return rc < 0 ? ZAN_ERROR : ZAN_OK;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        LLVMDisposeMessage(ir);
        return ZAN_ERROR;
    }
    /* 核心系统底层抽象与内存语义契约 */
    bool ok = (fputs(ir, f) >= 0);
    if (fclose(f) != 0) ok = false;
    LLVMDisposeMessage(ir);
    return ok ? ZAN_OK : ZAN_ERROR;
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
        if (!fn) continue; /* 核心系统底层抽象与内存语义契约 */
        if (LLVMCountBasicBlocks(fn) > 0) continue; /* 核心系统底层抽象与内存语义契约 */
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
            /* 内部辅助实现 */
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
            /* 内部辅助逻辑 */
            if (LLVMGetNamedFunction(g->mod, nm)) live = 1;
        }
        /* 内部辅助逻辑 */
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
        if (LLVMCountBasicBlocks(fn) == 0) continue; /* 核心系统底层抽象与内存语义契约 */
        size_t nlen = 0;
        const char *nm = LLVMGetValueName2(fn, &nlen);
        if (nm && nlen >= plen && memcmp(nm, prefix, plen) == 0) {
            found = true;
            break;
        }
    }
    if (g->prefix_cache_count < 32) {
        snprintf(g->prefix_cache[g->prefix_cache_count],
                 sizeof(g->prefix_cache[0]), "%s", prefix);
        g->prefix_cache_val[g->prefix_cache_count] = found;
        g->prefix_cache_count++;
    }
    return found;
}

/* 内部辅助实现 */
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
static void zan_init_llvm_targets(void) {
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

/* 内部辅助逻辑 */
static zan_status_t zan_bind_target_layout(zan_irgen_t *g,
                                           LLVMTargetMachineRef *out_tm) {
    char *triple;
    if (g->target_triple[0]) {
        /* 模块核心语义抽象与接口调用契约 */
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
        zan_diag_emit(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0),
                      "failed to get target: %s", error);
        LLVMDisposeMessage(error);
        LLVMDisposeMessage(triple);
        return ZAN_ERROR;
    }

    /* 内部辅助逻辑 */
    const char *tm_cpu = "generic";
    const char *tm_features = "";
    if (strncmp(triple, "wasm", 4) == 0) {
        /* 内部辅助实现 */
        tm_features = "+exception-handling,+reference-types";
    } else if (strncmp(triple, "riscv64", 7) == 0) {
        tm_cpu = "generic-rv64";
        tm_features = "+m,+a,+f,+d,+c";
        /* 底层系统交互与数据协议契约 */
        if (!LLVMGetModuleFlag(g->mod, "target-abi", 10))
            LLVMAddModuleFlag(g->mod, LLVMModuleFlagBehaviorError,
                              "target-abi", strlen("target-abi"),
                              LLVMValueAsMetadata(LLVMMDStringInContext(
                                  g->ctx, "lp64d", 5)));
    } else if (strncmp(triple, "riscv32", 7) == 0) {
        /* 内部辅助逻辑 */
        tm_cpu = "generic-rv32";
        tm_features = "+m,+c";
        if (!LLVMGetModuleFlag(g->mod, "target-abi", 10))
            LLVMAddModuleFlag(g->mod, LLVMModuleFlagBehaviorError,
                              "target-abi", strlen("target-abi"),
                              LLVMValueAsMetadata(LLVMMDStringInContext(
                                  g->ctx, "ilp32", 5)));
    } else if (strncmp(triple, "x86_64", 6) == 0) {
        tm_cpu = "x86-64";
        tm_features = "+sse3,+ssse3,+sse4.1,+sse4.2,+crc32,+aes,+avx,+avx2,+fma,+bmi";
    } else if (strncmp(triple, "aarch64", 7) == 0) {
        /* 核心系统底层抽象与内存语义契约 */
        tm_features = "+aes";
    }
    /* 核心系统底层抽象与内存语义契约 */
    LLVMCodeGenOptLevel cg = g->fast_codegen ? LLVMCodeGenLevelNone
                                             : LLVMCodeGenLevelDefault;
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
    zan_init_llvm_targets();
    zan_bind_target_layout(g, NULL);
}

zan_status_t zan_irgen_write_obj(zan_irgen_t *g, const char *path) {
    /* 内部辅助逻辑 */
    abi_pending_report(g);
    if (zan_diag_has_errors(g->diag)) return ZAN_ERROR;
    /* 内部辅助逻辑 */
    bool w32_triple = strncmp(g->target_triple, "wasm32", 6) == 0;
    bool rv32_triple = strncmp(g->target_triple, "riscv32", 7) == 0;
    if (w32_triple || rv32_triple) {
        bool wasi = w32_triple;
        if (wasi) {
            /* 内部辅助逻辑 */
            LLVMValueRef mainf = LLVMGetNamedFunction(g->mod, "main");
            if (mainf && LLVMCountBasicBlocks(mainf) > 0)
                LLVMSetValueName2(mainf, "__main_argc_argv",
                                  strlen("__main_argc_argv"));
        }
        /* 内部辅助实现 */
        {
            LLVMValueRef f = LLVMGetNamedFunction(g->mod, "snprintf");
            if (f && LLVMCountBasicBlocks(f) == 0)
                LLVMSetValueName2(f, "zan_w32_snprintf",
                                  strlen("zan_w32_snprintf"));
        }
        /* 内部辅助实现 */
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
            /* 内部辅助实现 */
            { "pthread_mutex_init", "iii" },
            { "pthread_mutex_lock", "ii" },
            { "pthread_mutex_unlock", "ii" },
            { "pthread_mutex_destroy", "ii" },
            /* 模块核心语义抽象与接口调用契约 */
            { "setvbuf", "ipipi" },
            /* 内部辅助实现 */
            { "dlopen", "pip" },     { "dlsym", "ppp" },
            { "dlclose", "ip" },
            /* 核心系统底层抽象与内存语义契约 */
            { "zan_file_fopen", "ppp" },
            { "zan_pkg_fopen", "ppp" },
            /* 内部辅助实现 */
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
            { "zan_game_draw3d", "iiipip" },
            /* 内部辅助实现 */
            { "zan_gui_clear_hit_guards", "ii" },
            { "zan_gui_add_hit_guard", "iiiiii" },
            { "zan_gui_text_stat_read", "ji" },
            /* 核心系统底层抽象与内存语义契约 */
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
            /* 核心系统底层抽象与内存语义契约 */
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
            /* 模块核心语义抽象与接口调用契约 */
            char an[80];
            snprintf(an, sizeof(an), "__zan_w32ir_%s", w32adapt[i].name);
            LLVMSetValueName2(decl, an, strlen(an));
            LLVMValueRef real = LLVMAddFunction(g->mod, w32adapt[i].name, lft);
            /* 内部辅助实现 */
            LLVMTypeRef cts[8];
            LLVMValueRef cad[8];
            int ncts = 0;
            LLVMUseRef use = LLVMGetFirstUse(decl);
            while (use) {
                LLVMUseRef next = LLVMGetNextUse(use);
                LLVMValueRef user = LLVMGetUser(use);
                /* 内部辅助实现 */
                if ((LLVMIsACallInst(user) || LLVMIsAInvokeInst(user)) &&
                    LLVMGetCalledValue(user) == decl) {
                    LLVMTypeRef cft = LLVMGetCalledFunctionType(user);
                    if (cft == lft) {
                        /* 底层系统交互与数据协议契约 */
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
            /* 内部辅助逻辑 */
            if (LLVMGetFirstUse(decl) && dft != lft) {
                w32_build_adapter_into(g, decl, real, lft);
                LLVMSetLinkage(decl, LLVMInternalLinkage);
            }
        }
    }
    /* 编译器代码生成与运行时系统底层调用契约 */
    zan_init_llvm_targets();

    /* 编译器代码生成与运行时系统底层调用契约 */
    if (g->obfuscate_strings /* publish */ && !g->target_is_macos && !g->target_is_wasm) {
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
        /* 内部辅助实现 */
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
            /* llvm */
            if (strncmp(nm, "llvm.", 5) == 0) continue;
            /* 编译期中间表示与代码生成内部规范 */
            if (strncmp(nm, "rterr", 5) == 0) continue;
            char sec[260];
            /* 内部辅助实现 */
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
        zan_diag_emit(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0),
                      "failed to emit object file: %s", error);
        LLVMDisposeMessage(error);
        LLVMDisposeTargetMachine(tm);
        return ZAN_ERROR;
    }

    LLVMDisposeTargetMachine(tm);
    return ZAN_OK;
}
