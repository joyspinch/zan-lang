        if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER &&
            expr->call.callee->ident.name.len == 6 &&
            memcmp(expr->call.callee->ident.name.str, "nameof", 6) == 0 &&
            expr->call.args.count == 1) {
            zan_ast_node_t *na = expr->call.args.items[0];
            if (na && na->kind == AST_IDENTIFIER)
                return emit_string_literal_rc(g, na->ident.name);
            if (na && na->kind == AST_MEMBER_ACCESS)
                return emit_string_literal_rc(g, na->member.name);
        }
        /* Reflection (irgen_reflect */
        if (expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *recv = expr->call.callee->member.object;
            zan_istr_t mname = expr->call.callee->member.name;
            zan_ast_node_t **cargs = expr->call.args.items;
            int cargc = expr->call.args.count;
            zan_type_t *rt = infer_expr_type(g, recv, locals);
            LLVMValueRef rv = NULL;
            if (zan_refl_is_typeinfo(rt)) {
                if (refl_emit_typeinfo_member(g,
                                              emit_guarded_member_object(
                                                  g, expr->call.callee, locals),
                                              mname, cargs, cargc, locals, &rv))
                    return rv;
            } else if (zan_refl_is_reflectable(rt) &&
                       (!rt->sym || !get_method_sym(rt->sym, mname)) &&
                       zan_refl_instance_method(mname, NULL)) {
                if (refl_emit_instance_call(g, rt,
                                            emit_guarded_member_object(
                                                g, expr->call.callee, locals),
                                            mname, cargs, cargc, locals, &rv))
                    return rv;
            }
        }

        /* `v */
        if (expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 0) {
            zan_ast_node_t *recv = expr->call.callee->member.object;
            zan_istr_t mname = expr->call.callee->member.name;
            zan_type_t *nt = infer_expr_type(g, recv, locals);
            bool is_gvod = mname.len == 17 &&
                memcmp(mname.str, "GetValueOrDefault", 17) == 0;
            bool is_tostr = mname.len == 8 && memcmp(mname.str, "ToString", 8) == 0;
            if (nt && nt->kind == TYPE_NULLABLE && (is_gvod || is_tostr)) {
                LLVMValueRef nv = emit_expr(g, recv, locals);
                if (llvm_is_nullable(LLVMTypeOf(nv)))
                    return is_gvod ? nullable_get_payload(g, nv)
                                   : emit_to_cstr(g, nv);
            }
        }

        if (expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 1) {
            zan_ast_node_t *recv = expr->call.callee->member.object;
            zan_istr_t mname = expr->call.callee->member.name;
            zan_type_t *nt = infer_expr_type(g, recv, locals);
            bool is_gvod = mname.len == 17 &&
                memcmp(mname.str, "GetValueOrDefault", 17) == 0;
            if (nt && nt->kind == TYPE_NULLABLE && is_gvod) {
                LLVMValueRef nv = emit_expr(g, recv, locals);
                if (llvm_is_nullable(LLVMTypeOf(nv))) {
                    LLVMValueRef payload = nullable_get_payload(g, nv);
                    LLVMValueRef has_val = nullable_has_value(g, nv);
                    LLVMValueRef dflt = emit_expr(g, expr->call.args.items[0], locals);
                    if (LLVMTypeOf(dflt) != LLVMTypeOf(payload)) {
                        dflt = coerce_int_to(g, dflt, LLVMTypeOf(payload));
                    }
                    return LLVMBuildSelect(g->builder, has_val, payload, dflt, "gvod.val");
                }
            }
        }

        /* `md */
        if (expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 1) {
            zan_ast_node_t *recv = expr->call.callee->member.object;
            zan_istr_t mname = expr->call.callee->member.name;
            if (mname.len == 9 && memcmp(mname.str, "GetLength", 9) == 0) {
                zan_type_t *at = infer_expr_type(g, recv, locals);
                if (at && at->kind == TYPE_ARRAY && at->array_rank > 1) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef arr = emit_expr(g, recv, locals);
                    LLVMValueRef dim = emit_expr(g, expr->call.args.items[0], locals);
                    dim = emit_index_i64(g, dim, "gl.dim");
                    LLVMValueRef oob = zan_icmp(g->builder, LLVMIntUGE, dim,
                        LLVMConstInt(i64, at->array_rank, 0), "gl.oob");
                    emit_runtime_check(g, oob, expr->loc,
                        "GetLength dimension out of range");
                    dim = LLVMBuildSelect(g->builder, oob,
                        LLVMConstInt(i64, 0, 0), dim, "gl.dim.safe");
                    LLVMValueRef dim_ptr = LLVMBuildBitCast(g->builder, arr,
                        LLVMPointerType(i64, 0), "gl.dp");
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, dim_ptr,
                        &dim, 1, "gl.slot");
                    return LLVMBuildLoad2(g->builder, i64, slot, "gl.val");
                }
            }
        }

        if (expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 0) {
            zan_ast_node_t *recv = expr->call.callee->member.object;
            zan_istr_t mname = expr->call.callee->member.name;
            zan_type_t *ot = infer_expr_type(g, recv, locals);
            if (ot && ot->kind == TYPE_TASK) {
                bool is_wait = mname.len == 4 && memcmp(mname.str, "Wait", 4) == 0;
                bool is_res  = mname.len == 6 && memcmp(mname.str, "Result", 6) == 0;
                bool is_comp = mname.len == 11 && memcmp(mname.str, "IsCompleted", 11) == 0;
                bool is_release = mname.len == 7 && memcmp(mname.str, "Release", 7) == 0;
                if (is_wait || is_res || is_comp || is_release) {
                    LLVMTypeRef ti64 = LLVMInt64TypeInContext(g->ctx);
                    if (is_res && ot->type_arg_count != 1) {
                        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3012,
                            "Task.Result requires a Task<T> value");
                        return LLVMConstInt(ti64, 0, 0);
                    }
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef h = emit_expr(g, recv, locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(h)) == LLVMPointerTypeKind)
                        h = LLVMBuildPtrToInt(g->builder, h, ti64, "task.h");
                    else if (LLVMGetIntTypeWidth(LLVMTypeOf(h)) < 64)
                        h = zan_iwiden(g->builder, h, ti64);
                    LLVMValueRef hp = LLVMBuildIntToPtr(g->builder, h, i8ptr, "task.fp");
                    int mode = is_comp ? 2 : (is_res ? 1 : (is_release ? 4 : 0));
                    return emit_task_member(g, hp,
                        ot->type_arg_count == 1 ? ot->type_args[0] : NULL, mode);
                }
            }
        }

        if ((is_call_to(expr, "Task", "Spawn") || is_call_to(expr, "Task", "Run")) &&
            expr->call.args.count == 1) {
            LLVMTypeRef sp_i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef sp_i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            zan_ast_node_t *sub_arg = expr->call.args.items[0];
            zan_type_t *sub_ty = infer_expr_type(g, sub_arg, locals);
            bool is_del = sub_ty && sub_ty->kind == TYPE_DELEGATE;
            zan_symbol_t *action_owner = NULL;
            zan_istr_t action_name = {0};
            int action_receiver = 0, action_error = 0;
            if (!is_del && sub_arg->kind == AST_IDENTIFIER &&
                (!locals || !local_find(locals, sub_arg->ident.name)) &&
                !ident_names_own_field(g, sub_arg)) {
                action_owner = g->current_type_sym;
                action_name = sub_arg->ident.name;
            } else if (!is_del && sub_arg->kind == AST_MEMBER_ACCESS) {
                zan_ast_node_t *obj = sub_arg->member.object;
                action_owner = expr_class_sym(g, obj, locals);
                action_name = sub_arg->member.name;
                action_receiver = 2;
                if (obj->kind == AST_IDENTIFIER &&
                    (!locals || !local_find(locals, obj->ident.name)) &&
                    !ident_names_own_field(g, obj)) {
                    zan_symbol_t *ts = zan_binder_lookup(g->binder, obj->ident.name);
                    if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT)) {
                        /* Type names have no expression value/type in irgen;
                         * use the bound type symbol as the candidate owner. */
                        action_owner = ts;
                        action_receiver = 1;
                    }
                }
            }
            zan_symbol_t *action_method = zan_binder_task_action_method(g->binder,
                action_owner, action_name, action_receiver, g->current_this != NULL, &action_error);
            bool is_method = action_method && action_method->decl &&
                action_method->decl->kind == AST_METHOD_DECL;
            if (sub_arg->kind != AST_CALL && sub_arg->kind != AST_LAMBDA && !is_del && !is_method) {
            } else {

            if (sub_arg->kind == AST_LAMBDA || is_del || is_method) {
                LLVMValueRef lv = NULL;
                if (is_method) {
                    zan_ast_node_t *decl = action_method->decl;
                    if (action_error) {
                        zan_diag_emit_code(g->diag, DIAG_ERROR, sub_arg->loc, 3052,
                            "invalid Task Action method group signature or receiver");
                        return LLVMConstInt(sp_i64, 0, 0);
                    }
                    LLVMValueRef receiver = NULL;
                    if (!(decl->method_decl.modifiers & MOD_STATIC)) {
                        if (sub_arg->kind == AST_MEMBER_ACCESS)
                            receiver = emit_guarded_member_object(g, sub_arg, locals);
                        else if (g->current_this)
                            receiver = LLVMBuildLoad2(g->builder, LLVMGetAllocatedType(g->current_this),
                                g->current_this, "task.this");
                    }
                    lv = emit_method_group_closure(g, action_method, receiver, sub_arg->loc);
                    if (!lv) {
                        zan_diag_emit_code(g->diag, DIAG_ERROR, sub_arg->loc, 3052,
                            "cannot bind Task Action method group");
                        return LLVMConstInt(sp_i64, 0, 0);
                    }
                    if (receiver && sub_arg->kind == AST_MEMBER_ACCESS)
                        emit_release_owned_call_temp(g, sub_arg->member.object, receiver, locals);
                } else if (sub_arg->kind == AST_LAMBDA) {
                    if (sub_arg->lambda.params.count != 0 ||
                        lambda_body_has_value_return(sub_arg->lambda.body)) {
                        zan_diag_emit_code(g->diag, DIAG_ERROR, sub_arg->loc, 3052,
                            "Task.Run delegate must be a parameterless void body "
                            "(an Action); pass an async call for results");
                        return LLVMConstInt(sp_i64, 0, 0);
                    }
                    zan_type_t *adt = (zan_type_t *)zan_arena_alloc(
                        g->arena, sizeof(zan_type_t));
                    memset(adt, 0, sizeof(*adt));
                    adt->kind = TYPE_DELEGATE;
                    adt->name = (zan_istr_t){ (char *)"__TaskAction", 12 };
                    adt->delegate_is_async = 0;
                    adt->delegate_param_count = 0;
                    adt->delegate_ret_type = g->binder->type_void;
                    lv = emit_lambda_typed(g, sub_arg, adt, locals);
                } else {
                    if (sub_ty->delegate_param_count != 0) {
                        zan_diag_emit_code(g->diag, DIAG_ERROR, sub_arg->loc, 3053,
                            "Task.Run delegate must take no parameters");
                        return LLVMConstInt(sp_i64, 0, 0);
                    }
                    lv = emit_expr(g, sub_arg, locals);
                }
                /* The method-group closure is freshly allocated, not a borrowed
                 * delegate expression. Transfer its initial owner explicitly. */
                LLVMValueRef task = emit_task_action_run(g, lv,
                    is_method ? NULL : sub_arg, locals);
                if (is_method) emit_closure_release(g, lv);
                return task;
            }
            LLVMValueRef sub = emit_expr(g, sub_arg, locals);
            bool keep_result = is_call_to(expr, "Task", "Run") &&
                !is_call_to(expr, "Task", "Spawn") &&
                sub_ty && sub_ty->kind != TYPE_VOID;
            LLVMValueRef sub_i8 = emit_detach_async_call(g, sub, keep_result, true);
            emit_release_owned_call_temp(g, sub_arg, sub, locals);
            /* Task */
            if (sub_i8) return LLVMBuildPtrToInt(g->builder, sub_i8, sp_i64, "spawn.h");
            return LLVMConstInt(sp_i64, 0, 0);
            }
        }

        if (is_call_to(expr, "Task", "Release") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef h = emit_expr(g, expr->call.args.items[0], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(h)) == LLVMIntegerTypeKind)
                h = LLVMBuildIntToPtr(g->builder, h, i8ptr, "release.handle");
            else h = LLVMBuildBitCast(g->builder, h, i8ptr, "release.handle");
            return emit_task_member(g, h, NULL, 4);
        }
        if (is_call_to(expr, "Task", "Cancel") && expr->call.args.count == 1) {
            LLVMTypeRef ci8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef ci64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef h = emit_expr(g, expr->call.args.items[0], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(h)) == LLVMIntegerTypeKind) {
                if (LLVMGetIntTypeWidth(LLVMTypeOf(h)) < 64)
                    h = zan_iwiden(g->builder, h, ci64);
                h = LLVMBuildIntToPtr(g->builder, h, ci8ptr, "cancel.h");
            } else {
                h = LLVMBuildBitCast(g->builder, h, ci8ptr, "cancel.h");
            }
            LLVMValueRef token = LLVMBuildPtrToInt(g->builder, h, ci64, "cancel.token");
            LLVMValueRef pin_args[] = { token, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0) };
            LLVMValueRef frame = emit_task_runtime_call(g, "zan_task_acquire", ci8ptr, pin_args, 2);
            LLVMValueRef cf = get_co_cancel_fn(g);
            zan_call2(g->builder, LLVMGlobalGetValueType(cf), cf, &frame, 1, "");
            emit_task_runtime_call(g, "zan_task_unpin", LLVMVoidTypeInContext(g->ctx), &frame, 1);
            return LLVMConstInt(ci64, 0, 0);
        }

        if (is_call_to(expr, "Task", "IsDone") && expr->call.args.count == 1) {
            LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef h = emit_expr(g, expr->call.args.items[0], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(h)) == LLVMIntegerTypeKind) {
                if (LLVMGetIntTypeWidth(LLVMTypeOf(h)) < 64)
                    h = zan_iwiden(g->builder, h, di64);
                h = LLVMBuildIntToPtr(g->builder, h, di8ptr, "isdone.h");
            } else {
                h = LLVMBuildBitCast(g->builder, h, di8ptr, "isdone.h");
            }
            LLVMValueRef token = LLVMBuildPtrToInt(g->builder, h, di64, "isdone.token");
            return emit_task_runtime_call(g, "zan_task_isdone", LLVMInt32TypeInContext(g->ctx), &token, 1);
        }

        /* Task */
        if (is_call_to(expr, "Task", "JoinNew") && expr->call.args.count == 2) {
            LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef a0 = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef a1 = emit_expr(g, expr->call.args.items[1], locals);
            if (LLVMTypeOf(a0) != di32) a0 = LLVMBuildTrunc(g->builder, a0, di32, "join.n");
            if (LLVMTypeOf(a1) != di32) a1 = LLVMBuildTrunc(g->builder, a1, di32, "join.any");
            LLVMTypeRef jn_type = LLVMFunctionType(di64,
                (LLVMTypeRef[]){ di32, di32 }, 2, 0);
            LLVMValueRef jn = LLVMGetNamedFunction(g->mod, "zan_join_new");
            if (!jn) jn = LLVMAddFunction(g->mod, "zan_join_new", jn_type);
            return zan_call2(g->builder, jn_type, jn,
                (LLVMValueRef[]){ a0, a1 }, 2, "join.new");
        }

        /* Task */
        if (is_call_to(expr, "Task", "JoinBind") && expr->call.args.count == 3) {
            LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef a0 = emit_expr(g, expr->call.args.items[0], locals);
            if (LLVMTypeOf(a0) != di64) a0 = LLVMBuildIntCast2(g->builder, a0, di64, 1, "join64");
            LLVMValueRef a1 = emit_expr(g, expr->call.args.items[1], locals);
            if (LLVMTypeOf(a1) == di64)
                a1 = LLVMBuildIntToPtr(g->builder, a1, di8ptr, "join.h");
            else
                a1 = LLVMBuildBitCast(g->builder, a1, di8ptr, "join.h");
            LLVMValueRef a2 = emit_expr(g, expr->call.args.items[2], locals);
            if (LLVMTypeOf(a2) != di32) a2 = LLVMBuildTrunc(g->builder, a2, di32, "join.i");
            LLVMTypeRef jb_type = LLVMFunctionType(di32,
                (LLVMTypeRef[]){ di64, di8ptr, di32 }, 3, 0);
            LLVMValueRef jb = LLVMGetNamedFunction(g->mod, "zan_join_bind");
            if (!jb) jb = LLVMAddFunction(g->mod, "zan_join_bind", jb_type);
            LLVMValueRef token = LLVMBuildPtrToInt(g->builder, a1, di64, "join.token");
            LLVMValueRef pin_args[] = { token, LLVMConstInt(di32, 0, 0) };
            LLVMValueRef frame = emit_task_runtime_call(g, "zan_task_acquire", di8ptr, pin_args, 2);
            LLVMValueRef bound = zan_call2(g->builder, jb_type, jb,
                (LLVMValueRef[]){ a0, frame, a2 }, 3, "join.bind");
            emit_task_runtime_call(g, "zan_task_unpin", LLVMVoidTypeInContext(g->ctx), &frame, 1);
            return bound;
        }

        /* Task */
        if (is_call_to(expr, "Task", "JoinCancel") && expr->call.args.count == 1) {
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef dvoid = LLVMVoidTypeInContext(g->ctx);
            LLVMValueRef a0 = emit_expr(g, expr->call.args.items[0], locals);
            if (LLVMTypeOf(a0) != di64) a0 = LLVMBuildIntCast2(g->builder, a0, di64, 1, "join64");
            LLVMTypeRef jc_type = LLVMFunctionType(dvoid, (LLVMTypeRef[]){ di64 }, 1, 0);
            LLVMValueRef jc = LLVMGetNamedFunction(g->mod, "zan_join_cancel");
            if (!jc) jc = LLVMAddFunction(g->mod, "zan_join_cancel", jc_type);
            zan_call2(g->builder, jc_type, jc, (LLVMValueRef[]){ a0 }, 1, "");
            return LLVMConstInt(di64, 0, 0);
        }

        /* Task */
        if (is_call_to(expr, "Task", "IsCancellationRequested") &&
            expr->call.args.count == 0) {
            LLVMTypeRef ci32 = LLVMInt32TypeInContext(g->ctx);
            if (!g->current_async_frame) return LLVMConstInt(ci32, 0, 0);
            return LLVMBuildLoad2(g->builder, ci32,
                LLVMBuildStructGEP2(g->builder, g->current_async_frame_type,
                    g->current_async_frame, ASYNC_FRAME_CANCEL, "fr.cancel.p"),
                "cancel.req");
        }

        /* StringBuilder */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sbcallee = expr->call.callee;
            zan_istr_t sbm = sbcallee->member.name;
            zan_type_t *sbt = infer_expr_type(g, sbcallee->member.object, locals);
            if (sbt && type_named(sbt, "StringBuilder", 13)) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef raw = emit_expr(g, sbcallee->member.object, locals);
                LLVMValueRef sbp = LLVMBuildBitCast(g->builder, raw,
                    LLVMPointerType(g->sb_struct_type, 0), "sbp");
                int sb_is_append = (sbm.len == 6 && memcmp(sbm.str, "Append", 6) == 0 &&
                                    expr->call.args.count == 1);
                int sb_is_appendline = (sbm.len == 10 && memcmp(sbm.str, "AppendLine", 10) == 0 &&
                                        expr->call.args.count <= 1);
                int sb_is_clear = (sbm.len == 5 && memcmp(sbm.str, "Clear", 5) == 0 &&
                                   expr->call.args.count == 0);
                int sb_is_bufptr = (sbm.len == 9 && memcmp(sbm.str, "BufferPtr", 9) == 0 &&
                                    expr->call.args.count == 0);
                int sb_is_tostring = (sbm.len == 8 && memcmp(sbm.str, "ToString", 8) == 0 &&
                                      expr->call.args.count == 0);
                int sb_recv_own = 0;
                if (sb_is_append || sb_is_appendline || sb_is_tostring || sb_is_clear || sb_is_bufptr)
                    sb_recv_own = emit_intrinsic_own_recv(g,
                        sbcallee->member.object, raw, locals);
                if (sb_is_bufptr) {
                    LLVMValueRef dptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 2, "sbdp");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, i8ptr, dptr, "sbdv");
                    LLVMValueRef pval = LLVMBuildPtrToInt(g->builder, data, i64, "sbi64");
                    emit_intrinsic_drop_recv(g, sbcallee->member.object, raw,
                                             locals, sb_recv_own);
                    return pval;
                }
                if (sb_is_clear) {
                    LLVMValueRef cptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 0, "sbcp");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), cptr);
                    LLVMValueRef dptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 2, "sbdp");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, i8ptr, dptr, "sbdv");
                    LLVMBasicBlockRef has_data = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sb_clr_data");
                    LLVMBasicBlockRef cont = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sb_clr_cont");
                    LLVMValueRef is_null = LLVMBuildICmp(g->builder, LLVMIntEQ, data, LLVMConstNull(i8ptr), "sb_data_null");
                    LLVMBuildCondBr(g->builder, is_null, cont, has_data);
                    LLVMPositionBuilderAtEnd(g->builder, has_data);
                    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), data);
                    LLVMBuildBr(g->builder, cont);
                    LLVMPositionBuilderAtEnd(g->builder, cont);
                    emit_intrinsic_drop_recv(g, sbcallee->member.object, raw,
                                             locals, sb_recv_own);
                    return raw;
                }
                if (sb_is_append || sb_is_appendline) {
                    if (expr->call.args.count == 1) {
                        zan_ast_node_t *arg0 = expr->call.args.items[0];
                        LLVMValueRef v = emit_expr(g, arg0, locals);
                        zan_type_t *at = infer_expr_type(g, arg0, locals);
                        int sv_arg = at && at->kind == TYPE_STRUCT &&
                            at->name.len == 10 && memcmp(at->name.str, "StringView", 10) == 0;
                        if (sv_arg) {
                            LLVMValueRef sv_ptr_i64, sv_len_i32;
                            if (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind) {
                                LLVMTypeRef st_ty = map_type(g, at);
                                LLVMValueRef p_ptr = LLVMBuildStructGEP2(g->builder, st_ty, v, 0, "svp.ptr");
                                sv_ptr_i64 = LLVMBuildLoad2(g->builder, i64, p_ptr, "svp.ptrv");
                                LLVMValueRef l_ptr = LLVMBuildStructGEP2(g->builder, st_ty, v, 1, "svp.len");
                                sv_len_i32 = LLVMBuildLoad2(g->builder, LLVMInt32TypeInContext(g->ctx), l_ptr, "svp.lenv");
                            } else {
                                sv_ptr_i64 = LLVMBuildExtractValue(g->builder, v, 0, "sv.ptr");
                                sv_len_i32 = LLVMBuildExtractValue(g->builder, v, 1, "sv.len");
                            }
                            LLVMValueRef s_bytes = LLVMBuildIntToPtr(g->builder, sv_ptr_i64, i8ptr, "sv.bptr");
                            LLVMValueRef slen_i64 = LLVMBuildZExt(g->builder, sv_len_i32, i64, "sv.len64");
                            emit_sb_append_bytes(g, sbp, s_bytes, slen_i64);
                        } else {
                            int char_arg =
                                LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind &&
                                expr_is_char(g, arg0, locals);
                            int int_arg = !char_arg &&
                                LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind &&
                                LLVMGetIntTypeWidth(LLVMTypeOf(v)) > 1;
                            LLVMValueRef s, slen;
                            if (int_arg) {
                                s = emit_entry_scratch(g, 40, "sb.i2s");
                                slen = emit_itoa_into(g, s,
                                    emit_widen_i64_for_print(g, v),
                                    expr_is_ulong(g, arg0, locals) ? 1 : 0);
                            } else {
                                s = char_arg ? emit_char_to_cstr(g, v)
                                             : emit_value_as_cstr(g, v);
                                int str_arg = !char_arg &&
                                    LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind &&
                                    is_string_expr(g, arg0, locals);
                                if (str_arg) {
                                    s = emit_str_nonnull(g, s);
                                    slen = emit_string_buffer_len(g, s, arg0->loc);
                                } else {
                                    slen = emit_cstr_len_of(g, s, arg0);
                                }
                            }
                            emit_sb_append_bytes(g, sbp, s, slen);
                            if (char_arg)
                                emit_string_release(g, s);
                            else if (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind)
                                emit_release_owned_call_temp(g, arg0, v, locals);
                        }
                    }
                    if (sb_is_appendline) {
                        zan_istr_t nl = { "\n", 1 };
                        LLVMValueRef nls = emit_string_literal_rc(g, nl);
                        emit_sb_append_bytes(g, sbp, nls, LLVMConstInt(i64, 1, 0));
                    }
                    emit_intrinsic_drop_recv(g, sbcallee->member.object, raw,
                                             locals, sb_recv_own);
                    return raw;
                }
                if (sb_is_tostring) {
                    LLVMValueRef cptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 0, "sbcp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, cptr, "sbcv");
                    LLVMValueRef dptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 2, "sbdp");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, i8ptr, dptr, "sbdv");
                    LLVMValueRef bsz = zan_add(g->builder, count, LLVMConstInt(i64, 1, 0), "sbbsz");
                    LLVMValueRef buf = emit_string_alloc_rc(g, bsz);
                    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
                    if (!memcpy_fn) {
                        memcpy_fn = LLVMAddFunction(g->mod, "memcpy",
                            LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0));
                    }
                    zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0),
                        memcpy_fn, (LLVMValueRef[]){ buf, data, count }, 3, "");
                    LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, buf, &count, 1, "sbend");
                    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
                    emit_string_len_set(g, buf, count);
                    emit_intrinsic_drop_recv(g, sbcallee->member.object, raw,
                                             locals, sb_recv_own);
                    return buf;
                }
            }
        }

        /* Console */
        if (!zan_type_defines(g, "Console", "WriteLine") &&
            (is_call_to(expr, "Console", "WriteLine") ||
             is_call_to(expr, "Console", "PrintLine"))) {
            if (expr->call.args.count > 0) {
                zan_ast_node_t *arg_ast = expr->call.args.items[0];
                LLVMValueRef arg = emit_expr(g, arg_ast, locals);
                LLVMTypeRef arg_type = LLVMTypeOf(arg);

                if (LLVMGetTypeKind(arg_type) == LLVMPointerTypeKind) {
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0) },
                        1, 0);
                    zan_call2(g->builder, fn_type, g->rt_println, &arg, 1, "");
                } else if (LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind) {
                    LLVMTypeRef flt = LLVMFloatTypeInContext(g->ctx);
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ flt },
                        1, 0);
                    zan_call2(g->builder, fn_type, g->rt_print_float, &arg, 1, "");
                } else if (LLVMGetTypeKind(arg_type) == LLVMDoubleTypeKind) {
                    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ dbl },
                        1, 0);
                    zan_call2(g->builder, fn_type, g->rt_print_double, &arg, 1, "");
                } else if (llvm_is_nullable(arg_type)) {
                    LLVMValueRef ns = emit_to_cstr(g, arg);
                    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    zan_call2(g->builder,
                        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                         (LLVMTypeRef[]){ i8p }, 1, 0),
                        g->rt_println, &ns, 1, "");
                    emit_string_release(g, ns);
                } else if (expr_is_char(g, arg_ast, locals)) {
                    LLVMValueRef cs = emit_char_to_cstr(g, arg);
                    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    zan_call2(g->builder,
                        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                         (LLVMTypeRef[]){ i8p }, 1, 0),
                        g->rt_println, &cs, 1, "");
                    emit_string_release(g, cs);
                } else if (expr_is_bool(g, arg_ast, locals)) {
                    LLVMValueRef t = emit_string_literal_rc(g,
                        (zan_istr_t){ "true", 4 });
                    LLVMValueRef f = emit_string_literal_rc(g,
                        (zan_istr_t){ "false", 5 });
                    LLVMValueRef cond = arg;
                    if (LLVMGetTypeKind(LLVMTypeOf(arg)) ==
                            LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(arg)) != 1)
                        cond = LLVMBuildICmp(g->builder, LLVMIntNE, arg,
                            LLVMConstNull(LLVMTypeOf(arg)), "wl.bnz");
                    LLVMValueRef bs = LLVMBuildSelect(g->builder, cond, t, f,
                                                      "wl.b");
                    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    zan_call2(g->builder,
                        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                         (LLVMTypeRef[]){ i8p }, 1, 0),
                        g->rt_println, &bs, 1, "");
                } else if (arg_type == get_decimal_type(g)) {
                    /* decimal 结构体不能掉进 print_int（曾触发 LLVM 校验失败，
                     * TASKS.md A511③）：转托管串后按字符串打印。 */
                    LLVMValueRef ds = emit_to_cstr_u(g, arg, false);
                    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    zan_call2(g->builder,
                        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                         (LLVMTypeRef[]){ i8p }, 1, 0),
                        g->rt_println, &ds, 1, "");
                    emit_string_release(g, ds);
                } else {
                    bool print_unsigned = expr_is_unsigned_int(g, arg_ast, locals);
                    if (print_unsigned) {
                        LLVMTypeRef vt = LLVMTypeOf(arg);
                        if (LLVMGetTypeKind(vt) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(vt) < 64)
                            arg = LLVMBuildZExt(g->builder, arg,
                                LLVMInt64TypeInContext(g->ctx), "print.zext");
                        else
                            arg = emit_widen_i64_for_print(g, arg);
                    } else {
                        /* Preserve sbyte signedness when nullable Value yields i8. */
                        zan_type_t *print_ty = infer_expr_type(g, arg_ast, locals);
                        LLVMTypeRef vt = LLVMTypeOf(arg);
                        if (print_ty && print_ty->kind == TYPE_SBYTE &&
                            LLVMGetTypeKind(vt) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(vt) < 64)
                            arg = LLVMBuildSExt(g->builder, arg,
                                LLVMInt64TypeInContext(g->ctx), "print.sbyte");
                        else
                            arg = emit_widen_i64_for_print(g, arg);
                    }
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ LLVMInt64TypeInContext(g->ctx) },
                        1, 0);
                    LLVMValueRef print_fn = print_unsigned
                        ? g->rt_print_uint : g->rt_print_int;
                    zan_call2(g->builder, fn_type, print_fn, &arg, 1, "");
                }
                emit_release_owned_call_temp(g, arg_ast, arg, locals);
            } else {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef empty = zan_irgen_intern_string(g, "");
                zan_call2(g->builder,
                    LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    g->rt_println, &empty, 1, "");
            }
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Console.ErrorWriteLine：stderr 行输出（A383——图表引擎的未知
         * option 键告警走 stderr，不污染 stdout 黄金）。收窄成 string 单
         * 签名：告警都是预拼好的文本，非串实参请先 Convert.ToString
         * （C# 的多态重载在 Zan 面上按内建收窄先例处理）。 */
        if (!zan_type_defines(g, "Console", "ErrorWriteLine") &&
            is_call_to(expr, "Console", "ErrorWriteLine")) {
            if (expr->call.args.count > 0) {
                zan_ast_node_t *arg_ast = expr->call.args.items[0];
                LLVMValueRef arg = emit_expr(g, arg_ast, locals);
                if (LLVMGetTypeKind(LLVMTypeOf(arg)) != LLVMPointerTypeKind) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3054,
                        "Console.ErrorWriteLine takes a string: format the "
                        "value with Convert.ToString first");
                    return emit_unresolved_call_result(g, expr, locals);
                }
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef fn_type = LLVMFunctionType(
                    LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0);
                LLVMValueRef efn = LLVMGetNamedFunction(g->mod, "zan_rt_eprint_line");
                if (!efn) efn = LLVMAddFunction(g->mod, "zan_rt_eprint_line", fn_type);
                zan_call2(g->builder, fn_type, efn, &arg, 1, "");
                emit_release_owned_call_temp(g, arg_ast, arg, locals);
            }
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Console.Write (no newline) */
        if (!zan_type_defines(g, "Console", "Write") &&
            is_call_to(expr, "Console", "Write")) {
            if (expr->call.args.count > 0) {
                zan_ast_node_t *arg_ast = expr->call.args.items[0];
                LLVMValueRef arg = emit_expr(g, arg_ast, locals);
                LLVMTypeRef arg_type = LLVMTypeOf(arg);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef printf_args[] = { i8ptr };
                LLVMTypeRef printf_type = LLVMFunctionType(
                    LLVMInt32TypeInContext(g->ctx), printf_args, 1, 1);
                LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");

                if (LLVMGetTypeKind(arg_type) == LLVMPointerTypeKind) {
                    LLVMValueRef fmt = zan_irgen_intern_string(g, "%s");
                    LLVMValueRef args[] = { fmt, arg };
                    zan_call2(g->builder, printf_type, printf_fn, args, 2, "");
                } else if (LLVMGetTypeKind(arg_type) == LLVMDoubleTypeKind ||
                           LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind) {
                    LLVMValueRef sbuf = emit_entry_scratch(g, 40, "wl.dbl");
                    emit_dbl_str(g, sbuf,
                        LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 40, 0),
                        arg);
                    LLVMValueRef fmt = zan_irgen_intern_string(g, "%s");
                    LLVMValueRef args[] = { fmt, sbuf };
                    zan_call2(g->builder, printf_type, printf_fn, args, 2, "");
                } else if (llvm_is_nullable(arg_type)) {
                    LLVMValueRef ns = emit_to_cstr(g, arg);
                    LLVMValueRef nfmt = zan_irgen_intern_string(g, "%s");
                    LLVMValueRef nargs[] = { nfmt, ns };
                    zan_call2(g->builder, printf_type, printf_fn, nargs, 2, "");
                    emit_string_release(g, ns);
                } else if (expr_is_char(g, arg_ast, locals)) {
                    LLVMValueRef cs = emit_char_to_cstr(g, arg);
                    LLVMValueRef cfmt = zan_irgen_intern_string(g, "%s");
                    LLVMValueRef cargs[] = { cfmt, cs };
                    zan_call2(g->builder, printf_type, printf_fn, cargs, 2, "");
                    emit_string_release(g, cs);
                } else if (expr_is_bool(g, arg_ast, locals)) {
                    LLVMValueRef t = emit_string_literal_rc(g,
                        (zan_istr_t){ "true", 4 });
                    LLVMValueRef f = emit_string_literal_rc(g,
                        (zan_istr_t){ "false", 5 });
                    LLVMValueRef cond = arg;
                    if (LLVMGetTypeKind(LLVMTypeOf(arg)) ==
                            LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(arg)) != 1)
                        cond = LLVMBuildICmp(g->builder, LLVMIntNE, arg,
                            LLVMConstNull(LLVMTypeOf(arg)), "w.bnz");
                    LLVMValueRef bs = LLVMBuildSelect(g->builder, cond, t, f,
                                                      "w.b");
                    LLVMValueRef bfmt = zan_irgen_intern_string(g, "%s");
                    LLVMValueRef bargs[] = { bfmt, bs };
                    zan_call2(g->builder, printf_type, printf_fn, bargs, 2, "");
                } else {
                    LLVMValueRef fmt = expr_is_ulong(g, arg_ast, locals)
                        ? zan_irgen_intern_string(g, "%llu")
                        : zan_irgen_intern_string(g, "%lld");
                    zan_type_t *print_ty = infer_expr_type(g, arg_ast, locals);
                    LLVMTypeRef vt = LLVMTypeOf(arg);
                    LLVMValueRef int_arg;
                    if (print_ty && print_ty->kind == TYPE_SBYTE &&
                        LLVMGetTypeKind(vt) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(vt) < 64)
                        int_arg = LLVMBuildSExt(g->builder, arg,
                            LLVMInt64TypeInContext(g->ctx), "print.sbyte");
                    else
                        int_arg = emit_widen_i64_for_print(g, arg);
                    LLVMValueRef args[] = { fmt, int_arg };
                    zan_call2(g->builder, printf_type, printf_fn, args, 2, "");
                }
                emit_release_owned_call_temp(g, arg_ast, arg, locals);
            }
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        if (!zan_type_defines(g, "Console", "ReadLine") &&
            is_call_to(expr, "Console", "ReadLine")) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf_size = LLVMConstInt(i64, 1024, 0);
            LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
            LLVMValueRef fgets_fn = LLVMGetNamedFunction(g->mod, "fgets");
            if (!fgets_fn) {
                LLVMTypeRef fgets_args[] = { i8ptr, LLVMInt32TypeInContext(g->ctx), i8ptr };
                LLVMTypeRef fgets_type = LLVMFunctionType(i8ptr, fgets_args, 3, 0);
                fgets_fn = LLVMAddFunction(g->mod, "fgets", fgets_type);
            }
            LLVMValueRef stdin_ptr;
            if (g->target_is_windows) {
                LLVMValueRef stdin_fn = LLVMGetNamedFunction(g->mod, "__acrt_iob_func");
                if (!stdin_fn) {
                    LLVMTypeRef iob_args[] = { LLVMInt32TypeInContext(g->ctx) };
                    LLVMTypeRef iob_type = LLVMFunctionType(i8ptr, iob_args, 1, 0);
                    stdin_fn = LLVMAddFunction(g->mod, "__acrt_iob_func", iob_type);
                }
                LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                stdin_ptr = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ LLVMInt32TypeInContext(g->ctx) }, 1, 0),
                    stdin_fn, &zero, 1, "stdin");
            } else {
                const char *siname = g->target_is_macos ? "__stdinp" : "stdin";
                LLVMValueRef stdin_g = LLVMGetNamedGlobal(g->mod, siname);
                if (!stdin_g) { stdin_g = LLVMAddGlobal(g->mod, i8ptr, siname); }
                stdin_ptr = LLVMBuildLoad2(g->builder, i8ptr, stdin_g, "stdin");
            }
            LLVMValueRef sz = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1024, 0);
            LLVMValueRef fgets_args[] = { buf, sz, stdin_ptr };
            LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0),
                buf);
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, LLVMInt32TypeInContext(g->ctx), i8ptr }, 3, 0),
                fgets_fn, fgets_args, 3, "");
            return buf;
        }

        /* Console */
        if (!zan_type_defines(g, "Console", "Read") &&
            is_call_to(expr, "Console", "Read")) {
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef getchar_ty = LLVMFunctionType(i32t, NULL, 0, 0);
            LLVMValueRef getchar_fn = get_libc_fn(g, "getchar", getchar_ty);
            LLVMValueRef r = zan_call2(g->builder, getchar_ty, getchar_fn, NULL, 0, "cread");
            return LLVMBuildSExt(g->builder, r, i64t, "cread64");
        }

        if (!zan_type_defines(g, "Console", "ReadKey") &&
            is_call_to(expr, "Console", "ReadKey")) {
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            const char *fname = g->target_is_windows ? "_getch" : "getchar";
            LLVMTypeRef fty = LLVMFunctionType(i32t, NULL, 0, 0);
            LLVMValueRef fn = get_libc_fn(g, fname, fty);
            LLVMValueRef r = zan_call2(g->builder, fty, fn, NULL, 0, "readkey");
            return LLVMBuildSExt(g->builder, r, i64t, "readkey64");
        }

        /* Console */
        if (!zan_type_defines(g, "Console", "Clear") &&
            is_call_to(expr, "Console", "Clear")) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef printf_type = LLVMFunctionType(
                LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 1);
            LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");
            if (!printf_fn) printf_fn = LLVMAddFunction(g->mod, "printf", printf_type);
            LLVMValueRef esc = zan_irgen_intern_string(g, "\033[2J\033[H");
            zan_call2(g->builder, printf_type, printf_fn, &esc, 1, "");
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        if (!zan_type_defines(g, "Console", "ResetColor") &&
            is_call_to(expr, "Console", "ResetColor")) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef printf_type = LLVMFunctionType(
                LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 1);
            LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");
            if (!printf_fn) printf_fn = LLVMAddFunction(g->mod, "printf", printf_type);
            LLVMValueRef esc = zan_irgen_intern_string(g, "\033[0m");
            zan_call2(g->builder, printf_type, printf_fn, &esc, 1, "");
            emit_console_color_reset(g);
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Console.IsOutputRedirected() / Console.IsTerminal()
         *
         * 问 libc "stdout 是不是终端"。直接发 libc 的 isatty 调用——**不新增
         * 运行时导出符号**：各平台的预编译运行时目标在非目标平台上重建不了，
         * 新增导出符号会让交叉目标全部失效。 */
        if (!zan_type_defines(g, "Console", "IsOutputRedirected") &&
            !zan_type_defines(g, "Console", "IsTerminal") &&
            (is_call_to(expr, "Console", "IsOutputRedirected") ||
             is_call_to(expr, "Console", "IsTerminal"))) {
            bool want_terminal = is_call_to(expr, "Console", "IsTerminal");
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef flag;
            if (g->target_is_wasm) {
                /* wasm 没有终端概念：stdout 永远不是终端（= 已重定向） */
                flag = LLVMConstInt(i32, want_terminal ? 0 : 1, 0);
            } else {
                const char *fname = g->target_is_windows ? "_isatty" : "isatty";
                LLVMTypeRef fty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i32 }, 1, 0);
                LLVMValueRef fn = get_libc_fn(g, fname, fty);
                LLVMValueRef fd = LLVMConstInt(i32, 1, 0);   /* stdout */
                LLVMValueRef r = zan_call2(g->builder, fty, fn, &fd, 1, "isatty");
                flag = want_terminal
                    ? zan_icmp(g->builder, LLVMIntNE, r, LLVMConstInt(i32, 0, 0), "tty")
                    : zan_icmp(g->builder, LLVMIntEQ, r, LLVMConstInt(i32, 0, 0), "redir");
            }
            return LLVMBuildZExt(g->builder, flag,
                                 LLVMInt64TypeInContext(g->ctx), "tty64");
        }

