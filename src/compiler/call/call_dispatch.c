        {
            zan_type_t *delegate_type =
                infer_expr_type(g, expr->call.callee, locals);
            if (delegate_type &&
                delegate_type->kind == TYPE_DELEGATE) {
                LLVMValueRef fn_ptr =
                    emit_expr(g, expr->call.callee, locals);
                return emit_delegate_call(
                    g, delegate_type, fn_ptr, expr, locals);
            }
        }

        {
            zan_type_t *oc_ty = infer_expr_type(g, expr->call.callee, locals);
            if (oc_ty && (oc_ty->kind == TYPE_CLASS || oc_ty->kind == TYPE_STRUCT) &&
                oc_ty->sym) {
                zan_istr_t op_istr = {(char *)"op_call", 7};
                zan_symbol_t *op_sym = resolve_op_overload(g, oc_ty->sym,
                                                           op_istr, expr, locals);
                if (op_sym) {
                    pack_params_args(g, expr, op_sym, locals);
                    for (int fi = irgen_find_function(g, op_sym); fi >= 0; fi = -1) {
                        if (g->functions[fi].sym == op_sym) {
                            int argc = expr->call.args.count + 1;
                            LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                                (size_t)argc, sizeof(LLVMValueRef));
                            LLVMValueRef recv_val = emit_expr(g, expr->call.callee, locals);
                            call_args[0] = recv_val;
                            if (LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMStructTypeKind) {
                                LLVMValueRef rslot = emit_entry_alloca(g,
                                    LLVMTypeOf(recv_val), "opc.recv");
                                LLVMBuildStore(g->builder, recv_val, rslot);
                                call_args[0] = rslot;
                            }
                            int recv_eh_pushed = 0;
                            if (oc_ty->kind == TYPE_CLASS &&
                                !expr_is_local_ident(expr->call.callee, locals) &&
                                expr_yields_owned_rc_value(g, expr->call.callee, locals) &&
                                LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMPointerTypeKind) {
                                emit_eh_tmp_push(g, recv_val);
                                recv_eh_pushed = 1;
                            }
                            int n_eh_args = 0;
                            for (int k = 0; k < expr->call.args.count; k++) {
                                int self_off =
                                    (op_sym->modifiers & MOD_STATIC) ? 1 : 0;
                                call_args[k + 1] = emit_arg_typed(g,
                                    expr->call.args.items[k],
                                    method_param_type_at(g, op_sym,
                                        k + self_off, expr,
                                        expr->call.callee, locals), locals);
                                n_eh_args += emit_call_arg_eh_push(g,
                                    expr->call.args.items[k],
                                    call_args[k + 1], locals);
                            }
                            LLVMTypeRef mft = g->functions[fi].fn_type;
                            LLVMValueRef mfn = route_generic_method(g, oc_ty,
                                op_sym, g->functions[fi].fn, mft, &mft);
                            const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "opcall";
                            LLVMValueRef result = emit_dispatch_call(g,
                                oc_ty->sym, op_sym, mfn, mft, call_args, argc, cn);
                            result = coerce_generic_result(g, result, op_sym, oc_ty);
                            for (int k = 0; k < n_eh_args; k++)
                                emit_eh_tmp_pop(g);
                            if (recv_eh_pushed) emit_eh_tmp_pop(g);
                            emit_release_owned_call_temp(g, expr->call.callee,
                                recv_val, locals);
                            for (int k = 0; k < expr->call.args.count; k++) {
                                emit_release_owned_call_temp(g, expr->call.args.items[k],
                                    call_args[k + 1], locals);
                            }
                            free(call_args);
                            return result;
                        }
                    }
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            if (callee->member.object->kind == AST_IDENTIFIER) {
                local_var_t *local = local_find(locals, callee->member.object->ident.name);
                if (local && local->type && local->type->sym) {
                    zan_symbol_t *type_sym = local->type->sym;
                    zan_symbol_t *method_sym = resolve_overload_typed(g, type_sym, callee->member.name, expr, locals);
                    zan_compile_trace("call local method: type='%.*s' method='%.*s' owner='%.*s'",
                        (int)type_sym->name.len, type_sym->name.str,
                        (int)callee->member.name.len, callee->member.name.str,
                        method_sym && method_sym->parent ? (int)method_sym->parent->name.len : 0,
                        method_sym && method_sym->parent ? method_sym->parent->name.str : "");
                    if (method_sym) fill_default_args(g, expr, method_sym);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                    if (method_sym) {
                        /* `d */
                        bool callee_static = (method_sym->modifiers & MOD_STATIC) != 0;
                        int recv_off = callee_static ? 0 : 1;
                        int spec = try_method_spec(g, method_sym, expr,
                                                   callee->member.object, locals);
                        if (spec >= 0)
                            return emit_method_spec_call(g, spec, expr,
                                                         callee->member.object, locals);
                        for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                            if (g->functions[fi].sym == method_sym) {
                                int argc = expr->call.args.count + recv_off;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                                    (size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                                if (!callee_static) {
                                    LLVMTypeRef at = local_slot_type(g, local);
                                    if (LLVMGetTypeKind(at) == LLVMPointerTypeKind) {
                                        call_args[0] = LLVMBuildLoad2(g->builder, at, local->alloca, "recv");
                                    } else {
                                        call_args[0] = local->alloca;
                                    }
                                    /* null 接收者 guard（TASKS.md A507）：这条
                                     * 局部变量快速路径曾完全无检查——this=null
                                     * 一路进方法体读字段，SIGSEGV 且无任何诊断
                                     * （字段访问路径早有 emit_weak_read_guard，
                                     * 方法调用偏偏漏了）。补齐同口径：软诊断 +
                                     * 软基址（scratch），--strict-runtime 下
                                     * exit 70。 */
                                    if (LLVMGetTypeKind(LLVMTypeOf(call_args[0]))
                                            == LLVMPointerTypeKind &&
                                        !callee->member.null_cond) {
                                        zan_type_t *rty = local->type;
                                        bool reflike = rty &&
                                            (rty->kind == TYPE_OBJECT ||
                                             rty->kind == TYPE_CLASS ||
                                             rty->kind == TYPE_INTERFACE ||
                                             rty->kind == TYPE_TYPE_PARAM ||
                                             rty->kind == TYPE_ERROR);
                                        if (reflike) {
                                            LLVMValueRef isnull = zan_icmp(
                                                g->builder, LLVMIntEQ,
                                                call_args[0],
                                                LLVMConstNull(LLVMTypeOf(call_args[0])),
                                                "recv.null");
                                            char nm[256];
                                            snprintf(nm, sizeof(nm),
                                                "null reference: receiver '%.*s' is null",
                                                (int)callee->member.object->ident.name.len,
                                                callee->member.object->ident.name.str);
                                            emit_runtime_check(g, isnull,
                                                callee->member.object->loc, nm);
                                            /* A550：async 被调者 + null 接收者
                                             * 不能"报告后继续"——软基址会把
                                             * scratch 当协程帧调度，协程序言
                                             * 虚表派发读哨兵字节。走到这里 =
                                             * 不在 try（在 try 的已 longjmp
                                             * 转 NRE），无有效续延，响亮中止。 */
                                            if (call_is_async_callee(g, expr, locals)) {
                                                LLVMValueRef gfn = LLVMGetBasicBlockParent(
                                                    LLVMGetInsertBlock(g->builder));
                                                LLVMBasicBlockRef live_bb =
                                                    LLVMAppendBasicBlockInContext(g->ctx, gfn, "recv.live");
                                                LLVMBasicBlockRef dead_bb =
                                                    LLVMAppendBasicBlockInContext(g->ctx, gfn, "recv.dead");
                                                LLVMBuildCondBr(g->builder, isnull, dead_bb, live_bb);
                                                LLVMPositionBuilderAtEnd(g->builder, dead_bb);
                                                LLVMTypeRef abty = LLVMFunctionType(
                                                    LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
                                                zan_call2(g->builder, abty,
                                                    get_libc_fn(g, "abort", abty), NULL, 0, "");
                                                LLVMBuildUnreachable(g->builder);
                                                LLVMPositionBuilderAtEnd(g->builder, live_bb);
                                            }
                                            call_args[0] = emit_soft_base_select(
                                                g, call_args[0], isnull,
                                                callee->member.object->loc);
                                        }
                                    }
                                }
                                int n_eh_args = 0;
                                for (int k = 0; k < expr->call.args.count; k++) {
                                    call_args[k + recv_off] = emit_arg_typed(g, expr->call.args.items[k],
                                        method_param_type_at(g, method_sym, k, expr,
                                                             callee->member.object, locals), locals);
                                    n_eh_args += emit_call_arg_eh_push(g,
                                        expr->call.args.items[k],
                                        call_args[k + recv_off], locals);
                                }
                                LLVMTypeRef mft = g->functions[fi].fn_type;
                                LLVMValueRef mfn = route_generic_method(g, local->type,
                                    method_sym, g->functions[fi].fn, mft, &mft);
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "mcall";
                                LLVMValueRef result = emit_dispatch_call(g,
                                    callee_static ? NULL : type_sym, method_sym,
                                    mfn, mft, call_args, argc, cn);
                                result = coerce_generic_result(g, result, method_sym, local->type);
                                for (int k = 0; k < n_eh_args; k++)
                                    emit_eh_tmp_pop(g);
                                for (int k = 0; k < expr->call.args.count; k++) {
                                    emit_release_owned_call_temp(g, expr->call.args.items[k],
                                        call_args[k + recv_off], locals);
                                }
                                free(call_args);
                                return result;
                            }
                        }
                    }
                }

                zan_symbol_t *type_sym = NULL;
                if (!ident_names_own_field(g, callee->member.object))
                    type_sym = zan_binder_lookup(g->binder,
                        callee->member.object->ident.name);
                if (type_sym && (type_sym->kind == SYM_CLASS || type_sym->kind == SYM_STRUCT)) {
                    zan_symbol_t *method_sym = resolve_overload_typed(g, type_sym, callee->member.name, expr, locals);
                    if (method_sym) fill_default_args(g, expr, method_sym);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                    if (method_sym) {
                        if ((method_sym->modifiers & MOD_STATIC) == 0) {
                            zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3062,
                                "cannot call instance method '%.*s.%.*s' via "
                                "the type name: call it on an instance",
                                (int)type_sym->name.len, type_sym->name.str,
                                (int)callee->member.name.len,
                                callee->member.name.str);
                        }
                        int spec = try_method_spec(g, method_sym, expr, NULL, locals);
                        if (spec >= 0)
                            return emit_method_spec_call(g, spec, expr, NULL, locals);
                        for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                            if (g->functions[fi].sym == method_sym) {
                                int argc = expr->call.args.count;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                                int *arg_eh_pushed = (int *)calloc(
                                    (size_t)(argc > 0 ? argc : 1), sizeof(int));
                                for (int k = 0; k < argc; k++) {
                                    zan_ast_node_t *arg = expr->call.args.items[k];
                                    call_args[k] = emit_arg_typed(g, arg,
                                        method_param_type_at(g, method_sym, k, expr, NULL, locals), locals);
                                    arg_eh_pushed[k] = emit_call_arg_eh_push(g, arg,
                                        call_args[k], locals);
                                }
                                /* `Box<int> */
                                LLVMTypeRef sft = g->functions[fi].fn_type;
                                LLVMValueRef sfn = route_generic_method(g,
                                    ident_inst_type(g, callee->member.object),
                                    method_sym, g->functions[fi].fn, sft, &sft);
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(sft)) == LLVMVoidTypeKind) ? "" : "scall";
                                coerce_args_to_params(g, sft, call_args, argc);
                                LLVMValueRef result = zan_call_or_invoke(g, g->builder, sft,
                                    sfn, call_args, (unsigned)argc, cn);
                                zan_type_t *gret = generic_method_ret(g, method_sym, expr, locals);
                                if (!gret && method_ret_is_bare_tp(method_sym)) {
                                    zan_type_t *ir = method_ret_type_at(g, method_sym,
                                        expr, NULL, locals);
                                    if (ir && ir->kind != TYPE_TYPE_PARAM) gret = ir;
                                }
                                if (gret) {
                                    result = emit_boundary_coerce(g, result, map_type(g, gret));
                                    if (gret->kind == TYPE_CLASS &&
                                        method_ret_is_bare_tp(method_sym))
                                        emit_arc_retain(g, result);
                                }
                                int consumes_free_arg =
                                    argc == 1 && call_consumes_free_arg(g->functions[fi].fn);
                                if (consumes_free_arg) {
                                    emit_invalidate_freed_string(g,
                                        expr->call.args.items[0], locals);
                                }
                                emit_extern_call_len_invalidate(g,
                                    g->functions[fi].fn, call_args, argc,
                                    expr, locals);
                                for (int k = argc - 1; k >= 0; k--) {
                                    if (arg_eh_pushed[k]) emit_eh_tmp_pop(g);
                                }
                                for (int k = 0; k < argc; k++) {
                                    if (!consumes_free_arg || k != 0) {
                                        emit_release_owned_call_temp(g, expr->call.args.items[k],
                                            call_args[k], locals);
                                    }
                                }
                                free(arg_eh_pushed);
                                free(call_args);
                                return result;
                            }
                        }
                    }
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_symbol_t *recv_cls = expr_class_sym(g, callee->member.object, locals);
            if (recv_cls) {
                zan_symbol_t *method_sym = resolve_overload_typed(g, recv_cls, callee->member.name, expr, locals);
                    if (method_sym) fill_default_args(g, expr, method_sym);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                if (method_sym) {
                    bool callee_static = (method_sym->modifiers & MOD_STATIC) != 0;
                    int spec = try_method_spec(g, method_sym, expr,
                                               callee->member.object, locals);
                    if (spec >= 0)
                        return emit_method_spec_call(g, spec, expr,
                                                     callee->member.object, locals);
                    for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                        if (g->functions[fi].sym == method_sym) {
                            int recv_off = callee_static ? 0 : 1;
                            int argc = expr->call.args.count + recv_off;
                            LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                                (size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                            LLVMValueRef recv_val = NULL;
                            int recv_eh_pushed = 0;
                            if (!callee_static) {
                                recv_val = emit_guarded_member_object_ex(
                                    g, callee, locals,
                                    call_is_async_callee(g, expr, locals));
                                call_args[0] = recv_val;
                                if (LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMStructTypeKind) {
                                    LLVMValueRef rslot = emit_entry_alloca(g, LLVMTypeOf(recv_val), "recv.tmp");
                                    LLVMBuildStore(g->builder, recv_val, rslot);
                                    call_args[0] = rslot;
                                }
                                {
                                    zan_type_t *rty = infer_expr_type(g, callee->member.object, locals);
                                    if (rty && rty->kind == TYPE_CLASS &&
                                        !expr_is_local_ident(callee->member.object, locals) &&
                                        expr_yields_owned_rc_value(g, callee->member.object, locals) &&
                                        LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMPointerTypeKind) {
                                        emit_eh_tmp_push(g, recv_val);
                                        recv_eh_pushed = 1;
                                    }
                                }
                            }
                            int n_eh_args = 0;
                            for (int k = 0; k < expr->call.args.count; k++) {
                                call_args[k + recv_off] = emit_arg_typed(g, expr->call.args.items[k],
                                    method_param_type_at(g, method_sym, k, expr,
                                                         callee->member.object, locals),
                                    locals);
                                n_eh_args += emit_call_arg_eh_push(g,
                                    expr->call.args.items[k],
                                    call_args[k + recv_off], locals);
                            }
                            zan_type_t *recv_ty = callee_static ? g->cur_inst
                                : infer_expr_type(g, callee->member.object, locals);
                            LLVMTypeRef mft = g->functions[fi].fn_type;
                            LLVMValueRef mfn = route_generic_method(g, recv_ty,
                                method_sym, g->functions[fi].fn, mft, &mft);
                            const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "mcall";
                            bool base_recv =
                                callee->member.object->kind == AST_BASE_EXPR;
                            LLVMValueRef result = emit_dispatch_call(g,
                                (callee_static || base_recv) ? NULL : recv_cls,
                                method_sym, mfn, mft, call_args, argc, cn);
                            result = coerce_generic_result(g, result, method_sym, recv_ty);
                            for (int k = 0; k < n_eh_args; k++)
                                emit_eh_tmp_pop(g);
                            if (recv_eh_pushed) emit_eh_tmp_pop(g);
                            if (!callee_static) {
                                emit_release_owned_call_temp(g, callee->member.object, recv_val, locals);
                            }
                            for (int k = 0; k < expr->call.args.count; k++) {
                                emit_release_owned_call_temp(g, expr->call.args.items[k],
                                    call_args[k + recv_off], locals);
                            }
                            free(call_args);
                            return result;
                        }
                    }
                }
            }
        }

        /* A558：object/接口/类接收者上的 System.Object 方法族。静态类型
         * （含基类链/接口自身）声明了同名成员时走普通成员调用或接口
         * 分派路径，不进这里。 */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            g->current_fn) {
            zan_ast_node_t *ofc = expr->call.callee;
            zan_istr_t fmn = ofc->member.name;
            int fam = -1;
            if (fmn.len == 8 && memcmp(fmn.str, "ToString", 8) == 0 &&
                expr->call.args.count == 0)
                fam = 0;
            else if (fmn.len == 6 && memcmp(fmn.str, "Equals", 6) == 0 &&
                     expr->call.args.count == 1)
                fam = 1;
            else if (fmn.len == 11 && memcmp(fmn.str, "GetHashCode", 11) == 0 &&
                     expr->call.args.count == 0)
                fam = 2;
            if (fam >= 0) {
                zan_type_t *ot = infer_expr_type(g, ofc->member.object, locals);
                if (ot && (ot->kind == TYPE_OBJECT || ot->kind == TYPE_INTERFACE ||
                           ot->kind == TYPE_CLASS)) {
                    zan_symbol_t *osym = ot->sym;
                    bool declared = false;
                    if (osym) {
                        declared = (ot->kind == TYPE_INTERFACE)
                            ? resolve_iface_overload(osym, fmn,
                                                     expr->call.args.count, 0) != NULL
                            : resolve_overload(osym, fmn,
                                               expr->call.args.count, 0) != NULL;
                    }
                    if (!declared)
                        return emit_object_family_call(g, expr, fam, locals);
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_type_t *obj_ty = infer_expr_type(g, callee->member.object, locals);
            if (obj_ty && obj_ty->kind == TYPE_INTERFACE && obj_ty->sym && g->current_fn) {
                zan_symbol_t *iface = obj_ty->sym;
                zan_symbol_t *iface_m = resolve_iface_overload(iface,
                                            callee->member.name,
                                            expr->call.args.count,
                                            expr->call.type_args.count);
                if (iface_m) {
                    LLVMContextRef c = g->ctx;
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
                    zan_type_t *rty = NULL;
                    if (iface_m->decl && iface_m->decl->kind == AST_METHOD_DECL &&
                        iface_m->decl->method_decl.return_type)
                        rty = zan_binder_resolve_type(g->binder,
                                  iface_m->decl->method_decl.return_type);
                    zan_ast_list_t *ifc_tps = &iface->decl->type_decl.type_params;
                    bool ifc_generic = obj_ty->type_arg_count > 0 &&
                        obj_ty->type_arg_count == ifc_tps->count &&
                        iface->decl->kind == AST_INTERFACE_DECL;
                    bool m_async = iface_m->decl &&
                        iface_m->decl->kind == AST_METHOD_DECL &&
                        (iface_m->decl->method_decl.modifiers & MOD_ASYNC) != 0;
                    bool has_res = m_async || (rty && rty->kind != TYPE_VOID);
                    if (ifc_generic && rty)
                        rty = zan_binder_subst_named(g->binder, rty,
                                                     ifc_tps, obj_ty->type_args);
                    LLVMTypeRef res_ty = m_async ? i8ptr
                                                 : (has_res ? map_type(g, rty) : NULL);

                    int uargc = expr->call.args.count;
                    int n_eh_args = 0;
                    LLVMValueRef recv = emit_guarded_member_object_ex(
                        g, callee, locals, m_async != 0);
                    LLVMValueRef *avals = (LLVMValueRef *)calloc((size_t)(uargc > 0 ? uargc : 1),
                                                                sizeof(LLVMValueRef));
                    for (int k = 0; k < uargc; k++) {
                        zan_type_t *pat = method_param_type_at(g, iface_m, k, expr,
                                                               callee->member.object,
                                                               locals);
                        if (ifc_generic && pat)
                            pat = zan_binder_subst_named(g->binder, pat,
                                                         ifc_tps,
                                                         obj_ty->type_args);
                        avals[k] = emit_arg_typed(g, expr->call.args.items[k],
                                                  pat, locals);
                        n_eh_args += emit_call_arg_eh_push(g, expr->call.args.items[k],
                                                          avals[k], locals);
                    }
                    typedef struct {
                        zan_symbol_t *cls;
                        zan_symbol_t *impl_m;
                        LLVMValueRef  fn;
                        LLVMTypeRef   fnty;
                        LLVMValueRef  tagc;
                    } ifc_candidate_t;

                    int cand_cap = g->struct_type_count + g->generic_inst_count + 4;
                    ifc_candidate_t *cands = (ifc_candidate_t *)calloc(
                        (size_t)cand_cap, sizeof(ifc_candidate_t));
                    int cand_count = 0;

                    /* 1. 非泛型类候选 */
                    for (int si = 0; si < g->struct_type_count; si++) {
                        zan_symbol_t *cls = g->struct_types[si].sym;
                        if (!cls || is_user_generic_sym(cls)) continue;
                        if (!class_implements_iface(cls, iface)) continue;
                        if (obj_ty->type_arg_count > 0 &&
                            !class_inst_implements_iface(g, cls, NULL, obj_ty)) continue;
                        zan_symbol_t *impl_m = resolve_overload(cls, callee->member.name, uargc, 0);
                        if (!impl_m) continue;
                        LLVMValueRef ifn = NULL; LLVMTypeRef ifnty = NULL;
                        for (int fi = irgen_find_function(g, impl_m); fi >= 0; fi = -1)
                            if (g->functions[fi].sym == impl_m) {
                                ifn = g->functions[fi].fn; ifnty = g->functions[fi].fn_type; break;
                            }
                        if (!ifn || !ifnty) continue;
                        cands[cand_count].cls = cls;
                        cands[cand_count].impl_m = impl_m;
                        cands[cand_count].fn = ifn;
                        cands[cand_count].fnty = ifnty;
                        cands[cand_count].tagc = LLVMConstBitCast(get_vtable_global(g, cls), i8ptr);
                        cand_count++;
                    }

                    /* 2. 泛型实例化类候选 */
                    for (int gi = 0; gi < g->generic_inst_count; gi++) {
                        zan_symbol_t *cls = g->generic_insts[gi].type_sym;
                        zan_type_t *inst = g->generic_insts[gi].inst;
                        if (!cls || !inst || !is_user_generic_sym(cls)) continue;
                        if (!class_inst_implements_iface(g, cls, inst, obj_ty)) continue;
                        zan_symbol_t *impl_m = resolve_overload(cls, callee->member.name, uargc, 0);
                        if (!impl_m) continue;
                        zan_symbol_t *msym_owner = impl_m->parent ? impl_m->parent : cls;
                        zan_type_t *owner = is_user_generic_sym(msym_owner) ? vtable_owner_inst(g, inst, msym_owner) : NULL;
                        LLVMValueRef ifn = NULL; LLVMTypeRef ifnty = NULL;
                        if (owner && owner->type_arg_count > 0) {
                            ifn = find_generic_fn(g, impl_m, owner->type_args,
                                                  owner->type_arg_count, &ifnty);
                        } else if (!is_user_generic_sym(msym_owner)) {
                            for (int fi = irgen_find_function(g, impl_m); fi >= 0; fi = -1)
                                if (g->functions[fi].sym == impl_m) {
                                    ifn = g->functions[fi].fn; ifnty = g->functions[fi].fn_type; break;
                                }
                        }
                        if (!ifn || !ifnty) continue;
                        cands[cand_count].cls = cls;
                        cands[cand_count].impl_m = impl_m;
                        cands[cand_count].fn = ifn;
                        cands[cand_count].fnty = ifnty;
                        cands[cand_count].tagc = LLVMConstBitCast(
                            get_vtable_global_inst(g, cls, inst), i8ptr);
                        cand_count++;
                    }

                    zan_compile_trace("ifc dispatch %.*s: cands=%d (structs=%d ginsts=%d)",
                        (int)callee->member.name.len, callee->member.name.str,
                        cand_count, g->struct_type_count, g->generic_inst_count);
                    if (cand_count == 1 && cands[0].cls && cands[0].impl_m &&
                        cands[0].fn && cands[0].fnty) {
                        unsigned npar = LLVMCountParamTypes(cands[0].fnty);
                        LLVMTypeRef *pts = (LLVMTypeRef *)calloc((size_t)(npar > 0 ? npar : 1), sizeof(LLVMTypeRef));
                        LLVMGetParamTypes(cands[0].fnty, pts);
                        int cargc = uargc + 1;
                        LLVMValueRef *ca = (LLVMValueRef *)calloc((size_t)cargc, sizeof(LLVMValueRef));
                        /* A556-2：结构体箱的载荷在首字 vtable tag 之后（+8），
                         * 结构体方法的 this 指向载荷。 */
                        LLVMValueRef thisv = recv;
                        if (cands[0].cls->kind == SYM_STRUCT)
                            thisv = LLVMBuildGEP2(g->builder,
                                LLVMInt8TypeInContext(c), recv,
                                (LLVMValueRef[]){ LLVMConstInt(LLVMInt64TypeInContext(c), 8, 0) },
                                1, "ifc.p8");
                        ca[0] = (npar > 0) ? LLVMBuildBitCast(g->builder, thisv, pts[0], "ifc.this") : thisv;
                        for (int k = 0; k < uargc; k++)
                            ca[k + 1] = (k + 1 < (int)npar)
                                ? emit_boundary_coerce(g, avals[k], pts[k + 1]) : avals[k];
                        const char *cn = has_res ? "ifccall" : "";
                        LLVMValueRef r = emit_dispatch_call(g, cands[0].cls, cands[0].impl_m,
                                                            cands[0].fn, cands[0].fnty, ca, cargc, cn);
                        if (has_res) {
                            r = emit_boundary_coerce(g, r, res_ty);
                        } else {
                            r = LLVMConstInt(LLVMInt32TypeInContext(c), 0, 0);
                        }
                        for (int k = 0; k < n_eh_args; k++)
                            emit_eh_tmp_pop(g);
                        for (int k = 0; k < uargc; k++)
                            emit_release_owned_call_temp(g, expr->call.args.items[k], avals[k], locals);
                        emit_release_owned_call_temp(g, callee->member.object, recv, locals);
                        free(ca); free(pts); free(avals); free(cands);
                        return r;
                    }

                    LLVMValueRef recv_pp = LLVMBuildBitCast(g->builder, recv,
                                              LLVMPointerType(i8ptr, 0), "ifc.recvpp");
                    LLVMValueRef tag = LLVMBuildLoad2(g->builder, i8ptr, recv_pp, "ifc.tag");

                    LLVMBasicBlockRef merge = LLVMAppendBasicBlockInContext(c, g->current_fn, "ifc.merge");
                    int cap = cand_count + 1;
                    LLVMValueRef *phi_vals = (LLVMValueRef *)calloc((size_t)cap, sizeof(LLVMValueRef));
                    LLVMBasicBlockRef *phi_bbs = (LLVMBasicBlockRef *)calloc((size_t)cap, sizeof(LLVMBasicBlockRef));
                    int np = 0;

                    for (int ci = 0; ci < cand_count; ci++) {
                        LLVMBasicBlockRef callbb = LLVMAppendBasicBlockInContext(c, g->current_fn, "ifc.call");
                        LLVMBasicBlockRef nextbb = LLVMAppendBasicBlockInContext(c, g->current_fn, "ifc.next");
                        LLVMValueRef tagc = cands[ci].tagc;
                        LLVMValueRef cmp = zan_icmp(g->builder, LLVMIntEQ, tag, tagc, "ifc.eq");
                        LLVMBuildCondBr(g->builder, cmp, callbb, nextbb);

                        LLVMPositionBuilderAtEnd(g->builder, callbb);
                        unsigned npar = LLVMCountParamTypes(cands[ci].fnty);
                        LLVMTypeRef *pts = (LLVMTypeRef *)calloc((size_t)(npar > 0 ? npar : 1), sizeof(LLVMTypeRef));
                        LLVMGetParamTypes(cands[ci].fnty, pts);
                        int cargc = uargc + 1;
                        LLVMValueRef *ca = (LLVMValueRef *)calloc((size_t)cargc, sizeof(LLVMValueRef));
                        /* A556-2：结构体箱的 this 指向载荷（+8），见单候选分支 */
                        LLVMValueRef thisv = recv;
                        if (cands[ci].cls->kind == SYM_STRUCT)
                            thisv = LLVMBuildGEP2(g->builder,
                                LLVMInt8TypeInContext(c), recv,
                                (LLVMValueRef[]){ LLVMConstInt(LLVMInt64TypeInContext(c), 8, 0) },
                                1, "ifc.p8");
                        ca[0] = (npar > 0) ? LLVMBuildBitCast(g->builder, thisv, pts[0], "ifc.this") : thisv;
                        for (int k = 0; k < uargc; k++)
                            ca[k + 1] = (k + 1 < (int)npar)
                                ? emit_boundary_coerce(g, avals[k], pts[k + 1]) : avals[k];
                        const char *cn = has_res ? "ifccall" : "";
                        LLVMValueRef r = zan_call_or_invoke(g, g->builder, cands[ci].fnty, cands[ci].fn, ca, (unsigned)cargc, cn);
                        if (has_res) {
                            r = emit_boundary_coerce(g, r, res_ty);
                            phi_vals[np] = r; phi_bbs[np] = LLVMGetInsertBlock(g->builder); np++;
                        }
                        LLVMBuildBr(g->builder, merge);
                        free(ca); free(pts);
                        LLVMPositionBuilderAtEnd(g->builder, nextbb);
                    }
                    free(cands);

                    emit_runtime_check(g, LLVMConstInt(LLVMInt1TypeInContext(c), 1, 0),
                                       expr->loc, "interface dispatch has no implementation");
                    if (has_res) {
                        phi_vals[np] = LLVMConstNull(res_ty);
                        phi_bbs[np] = LLVMGetInsertBlock(g->builder); np++;
                    }
                    LLVMBuildBr(g->builder, merge);
                    LLVMPositionBuilderAtEnd(g->builder, merge);
                    LLVMValueRef result;
                    if (has_res) {
                        LLVMValueRef phi = LLVMBuildPhi(g->builder, res_ty, "ifc.res");
                        LLVMAddIncoming(phi, phi_vals, phi_bbs, (unsigned)np);
                        result = phi;
                    } else {
                        result = LLVMConstInt(LLVMInt32TypeInContext(c), 0, 0);
                    }
                    for (int k = 0; k < n_eh_args; k++)
                        emit_eh_tmp_pop(g);
                    for (int k = 0; k < uargc; k++)
                        emit_release_owned_call_temp(g, expr->call.args.items[k],
                                                     avals[k], locals);
                    emit_release_owned_call_temp(g, callee->member.object, recv,
                                                 locals);
                    free(avals); free(phi_vals); free(phi_bbs);
                    return result;
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_ast_node_t *obj = callee->member.object;
            zan_ast_node_t *head = name_path_head(obj);
            if (is_name_path(obj) && head && !local_find(locals, head->ident.name)) {
                zan_symbol_t *type_sym = zan_binder_lookup(g->binder, obj->member.name);
                if (type_sym && (type_sym->kind == SYM_CLASS || type_sym->kind == SYM_STRUCT) &&
                    qpath_ns_ok(g, obj, type_sym)) {
                    zan_symbol_t *method_sym = resolve_overload_typed(g, type_sym, callee->member.name, expr, locals);
                    if (method_sym) fill_default_args(g, expr, method_sym);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                    if (method_sym) {
                        for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                            if (g->functions[fi].sym == method_sym) {
                                int argc = expr->call.args.count;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                                int n_eh_args = 0;
                                for (int k = 0; k < argc; k++) {
                                    call_args[k] = emit_arg_typed(g, expr->call.args.items[k],
                                        method_param_type_at(g, method_sym, k, expr, NULL, locals), locals);
                                    n_eh_args += emit_call_arg_eh_push(g, expr->call.args.items[k],
                                                          call_args[k], locals);
                                }
                                if (g->runtime_checks && g->current_fn &&
                                    sym_declares_extern(method_sym)) {
                                    for (int k = 0; k < argc; k++) {
                                        if (LLVMGetTypeKind(LLVMTypeOf(call_args[k]))
                                                != LLVMPointerTypeKind) continue;
                                        if (!is_string_expr(g,
                                                expr->call.args.items[k], locals))
                                            continue;
                                        LLVMValueRef isnull = zan_icmp(g->builder,
                                            LLVMIntEQ, call_args[k],
                                            LLVMConstNull(LLVMTypeOf(call_args[k])),
                                            "extarg.null");
                                        emit_runtime_check(g, isnull, expr->loc,
                                            "null string passed to an extern function");
                                        call_args[k] = LLVMBuildSelect(g->builder,
                                            isnull,
                                            zan_irgen_intern_string(g, ""),
                                            call_args[k], "extarg.safe");
                                    }
                                }
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(g->functions[fi].fn_type)) == LLVMVoidTypeKind) ? "" : "scall";
                                coerce_args_to_params(g, g->functions[fi].fn_type, call_args, argc);
                                LLVMValueRef result = zan_call_or_invoke(g, g->builder, g->functions[fi].fn_type,
                                    g->functions[fi].fn, call_args, (unsigned)argc, cn);
                                zan_type_t *gret = generic_method_ret(g, method_sym, expr, locals);
                                if (!gret && method_ret_is_bare_tp(method_sym)) {
                                    zan_type_t *ir = method_ret_type_at(g, method_sym,
                                        expr, NULL, locals);
                                    if (ir && ir->kind != TYPE_TYPE_PARAM) gret = ir;
                                }
                                if (gret) {
                                    result = emit_boundary_coerce(g, result, map_type(g, gret));
                                    if (gret->kind == TYPE_CLASS &&
                                        method_ret_is_bare_tp(method_sym))
                                        emit_arc_retain(g, result);
                                }
                                int consumes_free_arg =
                                    argc == 1 && call_consumes_free_arg(g->functions[fi].fn);
                                if (consumes_free_arg) {
                                    emit_invalidate_freed_string(g,
                                        expr->call.args.items[0], locals);
                                }
                                emit_extern_call_len_invalidate(g,
                                    g->functions[fi].fn, call_args, argc,
                                    expr, locals);
                                for (int k = 0; k < n_eh_args; k++)
                                    emit_eh_tmp_pop(g);
                                for (int k = 0; k < argc; k++) {
                                    if (!consumes_free_arg || k != 0) {
                                        emit_release_owned_call_temp(g, expr->call.args.items[k],
                                            call_args[k], locals);
                                    }
                                }
                                free(call_args);
                                return result;
                            }
                        }
                    }
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_type_t *recv_ty = infer_expr_type(g, callee->member.object, locals);
            zan_symbol_t *method_sym =
                find_extension_method(g, recv_ty, callee->member.name,
                                      expr->call.args.count,
                                      expr, callee->member.object, locals);
            if (method_sym) {
                int spec = try_method_spec(g, method_sym, expr,
                                           callee->member.object, locals);
                if (spec >= 0)
                    return emit_method_spec_call(g, spec, expr,
                                                 callee->member.object, locals);
                for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                    if (g->functions[fi].sym == method_sym) {
                        int argc = expr->call.args.count + 1;
                        LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                        call_args[0] = emit_arg_typed(g, callee->member.object,
                            method_param_type(g, method_sym, 0), locals);
                        int n_eh_args = 0;
                        for (int k = 0; k < expr->call.args.count; k++) {
                            call_args[k + 1] = emit_arg_typed(g, expr->call.args.items[k],
                                method_param_type_at(g, method_sym, k + 1, expr,
                                                     callee->member.object, locals), locals);
                            n_eh_args += emit_call_arg_eh_push(g, expr->call.args.items[k],
                                                  call_args[k + 1], locals);
                        }
                        const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(g->functions[fi].fn_type)) == LLVMVoidTypeKind) ? "" : "extcall";
                        coerce_args_to_params(g, g->functions[fi].fn_type, call_args, argc);
                        LLVMValueRef result = zan_call_or_invoke(g, g->builder, g->functions[fi].fn_type,
                            g->functions[fi].fn, call_args, (unsigned)argc, cn);
                        if (method_ret_is_bare_tp(method_sym)) {
                            zan_type_t *gret = method_ret_type_at(g, method_sym,
                                expr, callee->member.object, locals);
                            if (gret && gret->kind != TYPE_TYPE_PARAM) {
                                result = emit_boundary_coerce(g, result, map_type(g, gret));
                                if (gret->kind == TYPE_CLASS)
                                    emit_arc_retain(g, result);
                            }
                        }
                        for (int k = 0; k < n_eh_args; k++)
                            emit_eh_tmp_pop(g);
                        emit_release_owned_call_temp(g, callee->member.object, call_args[0], locals);
                        for (int k = 0; k < expr->call.args.count; k++) {
                            emit_release_owned_call_temp(g, expr->call.args.items[k],
                                call_args[k + 1], locals);
                        }
                        free(call_args);
                        return result;
                    }
                }
            }
        }

