/* irgen_call_builtins.c - 语言内置原语、数学、字符串、IO 与系统调用求值拦截 */
static LLVMValueRef emit_builtin_call_intercept(zan_irgen_t *g, zan_ast_node_t *expr,
                                                local_scope_t *locals, bool *matched) {
    if (matched) *matched = true;
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
                if (is_wait || is_res || is_comp) {
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
                    int mode = is_comp ? 2 : (is_res ? 1 :
                              (ot->type_arg_count == 1 ? 3 : 0));
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
            if (sub_arg->kind != AST_CALL && sub_arg->kind != AST_LAMBDA && !is_del) {
            } else {

            if (sub_arg->kind == AST_LAMBDA || is_del) {
                LLVMValueRef lv = NULL;
                if (sub_arg->kind == AST_LAMBDA) {
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
                return emit_task_action_run(g, lv, sub_arg, locals);
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
            LLVMValueRef cf = get_co_cancel_fn(g);
            zan_call2(g->builder, LLVMGlobalGetValueType(cf), cf, &h, 1, "");
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
            LLVMValueRef idf = get_co_isdone_fn(g);
            return zan_call2(g->builder, LLVMGlobalGetValueType(idf),
                             idf, &h, 1, "isdone");
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
            return zan_call2(g->builder, jb_type, jb,
                (LLVMValueRef[]){ a0, a1, a2 }, 3, "join.bind");
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
                    LLVMValueRef int_arg = emit_widen_i64_for_print(g, arg);
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

        /* NativeMemory 底层离堆内存操作原语与 CRC32 等系统调用封装 */
        {
            LLVMValueRef nm_out = NULL;
            if (emit_native_memory_call(g, expr, locals, &nm_out))
                return nm_out;
        }

        {
            LLVMValueRef px_out = NULL;
            if (emit_pixel_ops_call(g, expr, locals, &px_out))
                return px_out;
        }

        {
            LLVMValueRef bit_out = NULL;
            if (emit_bit_operations_call(g, expr, locals, &bit_out))
                return bit_out;
        }

        {
            LLVMValueRef cpu_out = NULL;
            if (emit_cpu_call(g, expr, locals, &cpu_out))
                return cpu_out;
        }

        {
            LLVMValueRef vec_out = NULL;
            if (emit_vector128_call(g, expr, locals, &vec_out))
                return vec_out;
            if (emit_vector256_call(g, expr, locals, &vec_out))
                return vec_out;
            if (emit_sse2_call(g, expr, locals, &vec_out))
                return vec_out;
            if (emit_sse42_call(g, expr, locals, &vec_out))
                return vec_out;
            if (emit_aes_call(g, expr, locals, &vec_out))
                return vec_out;
        }

        {
            LLVMValueRef sp_out = NULL;
            if (emit_span_call(g, expr, locals, &sp_out))
                return sp_out;
        }

        {
            LLVMValueRef by_out = NULL;
            if (emit_bytes_call(g, expr, locals, &by_out))
                return by_out;
        }

        /* String */
        if (is_call_to(expr, "String", "CompareOrdinal") && expr->call.args.count == 2) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            zan_ast_node_t *a_ast = expr->call.args.items[0];
            zan_ast_node_t *b_ast = expr->call.args.items[1];
            LLVMValueRef a = emit_expr(g, a_ast, locals);
            LLVMValueRef b = emit_expr(g, b_ast, locals);
            LLVMTypeRef strcmp_ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
            LLVMValueRef strcmp_fn = get_libc_fn(g, "strcmp", strcmp_ty);
            LLVMValueRef r = zan_call2(g->builder, strcmp_ty, strcmp_fn,
                (LLVMValueRef[]){ a, b }, 2, "ordcmp");
            emit_release_owned_call_temp(g, a_ast, a, locals);
            emit_release_owned_call_temp(g, b_ast, b, locals);
            return LLVMBuildSExt(g->builder, r, i64t, "ordcmp64");
        }

        /* Math.Sqrt(expr) → llvm.sqrt */
        if (!zan_type_defines(g, "Math", "Sqrt") &&
            is_call_to(expr, "Math", "Sqrt") && expr->call.args.count == 1) {
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            if (!math_require_numeric(g, expr, expr->call.args.items[0], arg,
                                      locals, "Sqrt"))
                return LLVMConstReal(dbl, 0.0);
            arg = math_arg_to_double(g, arg);
            LLVMValueRef sqrt_fn = LLVMGetNamedFunction(g->mod, "sqrt");
            if (!sqrt_fn) {
                LLVMTypeRef sqrt_args[] = { dbl };
                LLVMTypeRef sqrt_type = LLVMFunctionType(dbl, sqrt_args, 1, 0);
                sqrt_fn = LLVMAddFunction(g->mod, "sqrt", sqrt_type);
            }
            LLVMTypeRef sqrt_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
            return zan_call2(g->builder, sqrt_type, sqrt_fn, &arg, 1, "sqrt");
        }

        /* Math */
        {
            static const char *math1[] = { "Sin", "sin", "Cos", "cos", "Tan", "tan",
                                           "Log", "log", "Exp", "exp",
                                           "Atan", "atan", "Asin", "asin", "Acos", "acos", NULL };
            for (int mi = 0; math1[mi]; mi += 2) {
                if (!zan_type_defines(g, "Math", math1[mi]) &&
                    is_call_to(expr, "Math", math1[mi]) && expr->call.args.count == 1) {
                    LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                    if (!math_require_numeric(g, expr, expr->call.args.items[0], arg, locals, math1[mi]))
                        return LLVMConstReal(dbl, 0.0);
                    arg = math_arg_to_double(g, arg);
                    LLVMTypeRef fty = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
                    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, math1[mi + 1]);
                    if (!fn) fn = LLVMAddFunction(g->mod, math1[mi + 1], fty);
                    return zan_call2(g->builder, fty, fn, &arg, 1, math1[mi + 1]);
                }
            }
        }

        if (!zan_type_defines(g, "Math", "Atan2") &&
            is_call_to(expr, "Math", "Atan2") && expr->call.args.count == 2) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMValueRef y_v = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef x_v = emit_expr(g, expr->call.args.items[1], locals);
            if (!math_require_numeric(g, expr, expr->call.args.items[0], y_v, locals, "Atan2") ||
                !math_require_numeric(g, expr, expr->call.args.items[1], x_v, locals, "Atan2"))
                return LLVMConstReal(dbl, 0.0);
            y_v = math_arg_to_double(g, y_v);
            x_v = math_arg_to_double(g, x_v);
            LLVMValueRef atan2_fn = LLVMGetNamedFunction(g->mod, "atan2");
            if (!atan2_fn) {
                LLVMTypeRef atan2_args[] = { dbl, dbl };
                LLVMTypeRef atan2_type = LLVMFunctionType(dbl, atan2_args, 2, 0);
                atan2_fn = LLVMAddFunction(g->mod, "atan2", atan2_type);
            }
            LLVMValueRef args[] = { y_v, x_v };
            return zan_call2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl, dbl }, 2, 0),
                atan2_fn, args, 2, "atan2");
        }

        /* Math.Abs(expr) */
        if (!zan_type_defines(g, "Math", "Abs") &&
            is_call_to(expr, "Math", "Abs") && expr->call.args.count == 1) {
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef arg_type = LLVMTypeOf(arg);
            if (!math_require_numeric(g, expr, expr->call.args.items[0], arg, locals, "Abs"))
                return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
            if (LLVMGetTypeKind(arg_type) == LLVMDoubleTypeKind ||
                LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind) {
                LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                if (LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind)
                    arg = LLVMBuildFPExt(g->builder, arg, dbl, "ext");
                LLVMValueRef fabs_fn = LLVMGetNamedFunction(g->mod, "fabs");
                if (!fabs_fn) {
                    LLVMTypeRef fabs_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
                    fabs_fn = LLVMAddFunction(g->mod, "fabs", fabs_type);
                }
                return zan_call2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0),
                    fabs_fn, &arg, 1, "fabs");
            } else {
                unsigned abs_bits = LLVMGetIntTypeWidth(arg_type);
                LLVMValueRef shift = zan_ashr(g->builder, arg,
                    LLVMConstInt(arg_type, abs_bits - 1, 0), "sh");
                LLVMValueRef xor = zan_xor(g->builder, arg, shift, "xor");
                return zan_sub(g->builder, xor, shift, "abs");
            }
        }

        /* Math */
        if (!zan_type_defines(g, "Math", "Max") && !zan_type_defines(g, "Math", "Min") &&
            (is_call_to(expr, "Math", "Max") || is_call_to(expr, "Math", "Min")) &&
            expr->call.args.count == 2) {
            int want_max = is_call_to(expr, "Math", "Max");
            LLVMValueRef a = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef b = emit_expr(g, expr->call.args.items[1], locals);
            LLVMTypeKind ka = LLVMGetTypeKind(LLVMTypeOf(a));
            LLVMTypeKind kb = LLVMGetTypeKind(LLVMTypeOf(b));
            int fa = (ka == LLVMDoubleTypeKind || ka == LLVMFloatTypeKind);
            int fb = (kb == LLVMDoubleTypeKind || kb == LLVMFloatTypeKind);
            /* 守卫要逐个参数判：原来的写法是 "ka != Int && kb != Int"，
               遇到「一个指针 + 一个整数」就不报错了，pointer 随后被
               sitofp 成 double——生成非法 IR，编译死在代码生成阶段。 */
            if (!math_arg_is_numeric(a) || !math_arg_is_numeric(b)) {
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3055,
                              "Math.%s requires numeric arguments",
                              want_max ? "Max" : "Min");
                return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
            }
            if (fa || fb) {
                LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                a = fa ? (ka == LLVMFloatTypeKind
                            ? LLVMBuildFPExt(g->builder, a, dbl, "ext")
                            : a)
                       : LLVMBuildSIToFP(g->builder, a, dbl, "tofp");
                b = fb ? (kb == LLVMFloatTypeKind
                            ? LLVMBuildFPExt(g->builder, b, dbl, "ext")
                            : b)
                       : LLVMBuildSIToFP(g->builder, b, dbl, "tofp");
                LLVMValueRef a_nan = LLVMBuildFCmp(g->builder,
                    LLVMRealUNO, a, a, "an");
                LLVMValueRef b_nan = LLVMBuildFCmp(g->builder,
                    LLVMRealUNO, b, b, "bn");
                LLVMValueRef cmp = LLVMBuildFCmp(g->builder,
                    want_max ? LLVMRealOGT : LLVMRealOLT, a, b, "cmp");
                LLVMValueRef picked = LLVMBuildSelect(g->builder, cmp, a, b,
                    want_max ? "max" : "min");
                LLVMValueRef from_b = LLVMBuildSelect(g->builder, b_nan, b,
                    picked, want_max ? "max" : "min");
                return LLVMBuildSelect(g->builder, a_nan, a, from_b,
                    want_max ? "max" : "min");
            }
            unsigned wa = LLVMGetIntTypeWidth(LLVMTypeOf(a));
            unsigned wb = LLVMGetIntTypeWidth(LLVMTypeOf(b));
            if (wa < wb) a = LLVMBuildSExt(g->builder, a, LLVMTypeOf(b), "sx");
            else if (wb < wa) b = LLVMBuildSExt(g->builder, b, LLVMTypeOf(a), "sx");
            LLVMValueRef cmp = zan_icmp(g->builder,
                want_max ? LLVMIntSGT : LLVMIntSLT, a, b, "cmp");
            return LLVMBuildSelect(g->builder, cmp, a, b, want_max ? "max" : "min");
        }

        if (!zan_type_defines(g, "Math", "Pow") &&
            is_call_to(expr, "Math", "Pow") && expr->call.args.count == 2) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMValueRef base_v = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef exp_v = emit_expr(g, expr->call.args.items[1], locals);
            if (!math_require_numeric(g, expr, expr->call.args.items[0], base_v, locals, "Pow") ||
                !math_require_numeric(g, expr, expr->call.args.items[1], exp_v, locals, "Pow"))
                return LLVMConstReal(dbl, 0.0);
            base_v = math_arg_to_double(g, base_v);
            exp_v = math_arg_to_double(g, exp_v);
            LLVMValueRef pow_fn = LLVMGetNamedFunction(g->mod, "pow");
            if (!pow_fn) {
                LLVMTypeRef pow_args[] = { dbl, dbl };
                LLVMTypeRef pow_type = LLVMFunctionType(dbl, pow_args, 2, 0);
                pow_fn = LLVMAddFunction(g->mod, "pow", pow_type);
            }
            LLVMValueRef args[] = { base_v, exp_v };
            return zan_call2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl, dbl }, 2, 0),
                pow_fn, args, 2, "pow");
        }

        if (!zan_type_defines(g, "Math", "Floor") &&
            is_call_to(expr, "Math", "Floor") && expr->call.args.count == 1) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            if (!math_require_numeric(g, expr, expr->call.args.items[0], arg, locals, "Floor"))
                return LLVMConstReal(dbl, 0.0);
            arg = math_arg_to_double(g, arg);
            LLVMValueRef floor_fn = LLVMGetNamedFunction(g->mod, "floor");
            if (!floor_fn) {
                LLVMTypeRef floor_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
                floor_fn = LLVMAddFunction(g->mod, "floor", floor_type);
            }
            return zan_call2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0),
                floor_fn, &arg, 1, "floor");
        }

        if (!zan_type_defines(g, "Math", "Ceiling") &&
            is_call_to(expr, "Math", "Ceiling") && expr->call.args.count == 1) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            if (!math_require_numeric(g, expr, expr->call.args.items[0], arg, locals, "Ceiling"))
                return LLVMConstReal(dbl, 0.0);
            arg = math_arg_to_double(g, arg);
            LLVMValueRef ceil_fn = LLVMGetNamedFunction(g->mod, "ceil");
            if (!ceil_fn) {
                LLVMTypeRef ceil_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
                ceil_fn = LLVMAddFunction(g->mod, "ceil", ceil_type);
            }
            return zan_call2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0),
                ceil_fn, &arg, 1, "ceil");
        }

        /* Math */
        if (!zan_type_defines(g, "Math", "Round") &&
            is_call_to(expr, "Math", "Round") &&
            (expr->call.args.count == 1 || expr->call.args.count == 2)) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMTypeRef d1_ty = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            if (!math_require_numeric(g, expr, expr->call.args.items[0], arg, locals, "Round"))
                return LLVMConstReal(dbl, 0.0);
            arg = math_arg_to_double(g, arg);
            /* C# Math.Round 是银行家舍入（to-even）——libc rint 按默认 FP
             * 舍入模式（最近偶数）实现；libc round 是远离零，曾与 C# 分叉
             * （Round(2.5) 得 3，C# 是 2；TASKS.md A513）。 */
            LLVMValueRef round_fn = get_libc_fn(g, "rint", d1_ty);
            if (expr->call.args.count == 1)
                return zan_call2(g->builder, d1_ty, round_fn, &arg, 1, "rint");
            /* digits: rint(x * 10^d) / 10^d（同样银行家） */
            LLVMValueRef digits = emit_expr(g, expr->call.args.items[1], locals);
            if (!math_require_numeric(g, expr, expr->call.args.items[1], digits, locals, "Round"))
                return LLVMConstReal(dbl, 0.0);
            digits = math_arg_to_double(g, digits);
            LLVMTypeRef d2_ty = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl, dbl }, 2, 0);
            LLVMValueRef pow_fn = get_libc_fn(g, "pow", d2_ty);
            LLVMValueRef scale = zan_call2(g->builder, d2_ty, pow_fn,
                (LLVMValueRef[]){ LLVMConstReal(dbl, 10.0), digits }, 2, "scale");
            LLVMValueRef scaled = LLVMBuildFMul(g->builder, arg, scale, "scaled");
            LLVMValueRef r = zan_call2(g->builder, d1_ty, round_fn, &scaled, 1, "rint");
            return LLVMBuildFDiv(g->builder, r, scale, "rdig");
        }

        /* char 的静态类（A525）：char.IsDigit('5') 等。谓词走 <ctype.h>
         * （ASCII 口径；非 ASCII 字符与 C# 的 Unicode 类目有偏差）。
         * ToUpper/ToLower 走 Unicode 简单映射表（见 get_char_case_fn）。 */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->call.callee->member.object->ident.name) &&
            expr->call.args.count == 1) {
            zan_istr_t cn = expr->call.callee->member.object->ident.name;
            int char_cls = (cn.len == 4 &&
                            (memcmp(cn.str, "char", 4) == 0 ||
                             memcmp(cn.str, "Char", 4) == 0));
            if (char_cls) {
                zan_istr_t cm = expr->call.callee->member.name;
                static const struct { const char *name; const char *libc; } preds[] = {
                    { "IsDigit", "isdigit" }, { "IsLetter", "isalpha" },
                    { "IsLetterOrDigit", "isalnum" }, { "IsUpper", "isupper" },
                    { "IsLower", "islower" }, { "IsWhiteSpace", "isspace" },
                };
                static const struct { const char *name; const char *libc; } mappers[] = {
                    { "ToUpper", "toupper" }, { "ToLower", "tolower" },
                };
                LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                LLVMTypeRef aty = LLVMFunctionType(i32t,
                    (LLVMTypeRef[]){ i32t }, 1, 0);
                LLVMValueRef cp = arg;
                if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMIntegerTypeKind &&
                    LLVMGetIntTypeWidth(LLVMTypeOf(arg)) > 32)
                    cp = LLVMBuildTrunc(g->builder, arg, i32t, "ch.cp32");
                for (size_t pi = 0; pi < sizeof(preds) / sizeof(preds[0]); pi++) {
                    size_t nl = strlen(preds[pi].name);
                    if (cm.len != (int)nl || memcmp(cm.str, preds[pi].name, nl) != 0)
                        continue;
                    LLVMValueRef r = zan_call2(g->builder, aty,
                        get_libc_fn(g, preds[pi].libc, aty), &cp, 1, "ch.pred");
                    LLVMValueRef nz = zan_icmp(g->builder, LLVMIntNE, r,
                        LLVMConstInt(i32t, 0, 0), "ch.nz");
                    emit_release_owned_call_temp(g, expr->call.args.items[0],
                                                 arg, locals);
                    return nz;
                }
                for (size_t mi = 0; mi < sizeof(mappers) / sizeof(mappers[0]); mi++) {
                    size_t nl = strlen(mappers[mi].name);
                    if (cm.len != (int)nl || memcmp(cm.str, mappers[mi].name, nl) != 0)
                        continue;
                    /* Unicode 简单大小写映射（审计 P1-12 / A525 后续）：
                     * libc toupper/tolower 是 C locale ASCII 口径，非 ASCII
                     * 原样返回；表助手覆盖全 Unicode 1:1 简单映射。 */
                    LLVMTypeRef case_ty = LLVMFunctionType(i32t,
                        (LLVMTypeRef[]){ i32t, i32t }, 2, 0);
                    LLVMValueRef upc = LLVMConstInt(i32t,
                        strcmp(mappers[mi].name, "ToUpper") == 0 ? 1 : 0, 0);
                    LLVMValueRef args2[2] = { cp, upc };
                    LLVMValueRef r = zan_call2(g->builder, case_ty,
                        get_char_case_fn(g), args2, 2, "ch.map");
                    if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(arg)) > 32)
                        r = LLVMBuildSExt(g->builder, r, LLVMTypeOf(arg), "ch.map.w");
                    emit_release_owned_call_temp(g, expr->call.args.items[0],
                                                 arg, locals);
                    return r;
                }
                /* 未命中的 char.* 成员落回通用路径（会得到相应的未解析诊断） */
            }
        }

        /* Convert.ToDouble(x) */
        if (!zan_type_defines(g, "Convert", "ToDouble") &&
            is_call_to(expr, "Convert", "ToDouble") && expr->call.args.count == 1) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            zan_type_t *at = infer_expr_type(g, expr->call.args.items[0], locals);
            if (!type_is_textual_or_numeric(at)) {
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3056,
                              "Convert.ToDouble: cannot convert '%.*s' to a "
                              "number -- pass a string or a numeric value",
                              at && at->name.len ? (int)at->name.len : 6,
                              at && at->name.len ? at->name.str : "<value>");
                return LLVMConstReal(dbl, 0.0);
            }
            LLVMTypeKind ak = LLVMGetTypeKind(LLVMTypeOf(arg));
            if (ak == LLVMPointerTypeKind) {
                LLVMTypeRef strtod_ty = LLVMFunctionType(dbl,
                    (LLVMTypeRef[]){ i8ptr, LLVMPointerType(i8ptr, 0) }, 2, 0);
                LLVMValueRef f = LLVMGetNamedFunction(g->mod, "zan_rt_dbl_parse");
                if (!f) f = LLVMAddFunction(g->mod, "zan_rt_dbl_parse", strtod_ty);
                /* A512：失败/残留 → 软诊断 + 安全值 0（曾静默返 0 继续执行） */
                LLVMValueRef endp = emit_entry_alloca(g, i8ptr, "todbl.endp");
                LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), endp);
                LLVMValueRef s_safe = emit_parse_str_guard(g, arg, expr->loc);
                LLVMValueRef r = zan_call2(g->builder, strtod_ty, f,
                    (LLVMValueRef[]){ s_safe, endp }, 2, "todbl");
                LLVMValueRef ep = LLVMBuildLoad2(g->builder, i8ptr, endp, "todbl.ep");
                LLVMValueRef bad = emit_parse_bad_pred(g, s_safe, ep);
                emit_runtime_check(g, bad, expr->loc,
                    "Convert.ToDouble: input is not a valid number");
                r = LLVMBuildSelect(g->builder, bad,
                    LLVMConstReal(dbl, 0.0), r, "todbl.safe");
                emit_release_owned_call_temp(g, expr->call.args.items[0], arg, locals);
                return r;
            }
            if (ak == LLVMIntegerTypeKind)
                return LLVMBuildSIToFP(g->builder, arg, dbl, "todbl");
            if (ak == LLVMFloatTypeKind)
                return LLVMBuildFPExt(g->builder, arg, dbl, "todbl");
            return arg;
        }

        /* int/long/Int32/Int64 */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->call.callee->member.object->ident.name)) {
            zan_istr_t rn = expr->call.callee->member.object->ident.name;
            zan_istr_t pm = expr->call.callee->member.name;
            int int_recv = (rn.len == 3 && memcmp(rn.str, "int", 3) == 0) ||
                           (rn.len == 4 && memcmp(rn.str, "long", 4) == 0) ||
                           (rn.len == 5 && memcmp(rn.str, "Int32", 5) == 0) ||
                           (rn.len == 5 && memcmp(rn.str, "Int64", 5) == 0);
            int dbl_recv = (rn.len == 6 && memcmp(rn.str, "double", 6) == 0) ||
                           (rn.len == 5 && memcmp(rn.str, "float", 5) == 0) ||
                           (rn.len == 6 && memcmp(rn.str, "Double", 6) == 0) ||
                           (rn.len == 6 && memcmp(rn.str, "Single", 6) == 0);
            if ((int_recv || dbl_recv) &&
                pm.len == 5 && memcmp(pm.str, "Parse", 5) == 0 &&
                expr->call.args.count == 1) {                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, expr->call.args.items[0], locals);
                zan_type_t *at = infer_expr_type(g, expr->call.args.items[0], locals);
                if (!type_is_text_arg(at)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3057,
                                  "%.*s.Parse requires a string argument",
                                  (int)rn.len, rn.str);
                    return int_recv ? LLVMConstInt(i64, 0, 0)
                                    : LLVMConstReal(dbl, 0.0);
                }
                LLVMValueRef r;
                if (int_recv) {
                    LLVMTypeRef strtoll_ty = LLVMFunctionType(i64,
                        (LLVMTypeRef[]){ i8ptr, i8pp, i32t }, 3, 0);
                    LLVMValueRef f = get_libc_fn(g, "strtoll", strtoll_ty);
                    LLVMValueRef endp = emit_entry_alloca(g, i8ptr, "parse.endp");
                    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), endp);
                    LLVMValueRef s_safe = emit_parse_str_guard(g, s, expr->loc);
                    r = zan_call2(g->builder, strtoll_ty, f,
                        (LLVMValueRef[]){ s_safe, endp,
                                          LLVMConstInt(i32t, 10, 0) }, 3, "parse");
                    LLVMValueRef ep = LLVMBuildLoad2(g->builder, i8ptr, endp, "parse.ep");
                    LLVMValueRef bad = emit_parse_bad_pred(g, s_safe, ep);
                    /* C# int.Parse 超出 int 范围抛 OverflowException：
                     * strtoll 会静默吃下 64 位值，这里同样判坏。 */
                    LLVMValueRef over = LLVMBuildOr(g->builder,
                        zan_icmp(g->builder, LLVMIntSLT, r,
                            LLVMConstInt(i64, (int64_t)INT32_MIN, 0), "parse.lo"),
                        zan_icmp(g->builder, LLVMIntSGT, r,
                            LLVMConstInt(i64, INT32_MAX, 0), "parse.hi"),
                        "parse.over");
                    bad = LLVMBuildOr(g->builder, bad, over, "parse.bad2");
                    emit_runtime_check(g, bad, expr->loc,
                        "int.Parse: input is not a valid integer (or out of range)");
                    r = LLVMBuildSelect(g->builder, bad,
                        LLVMConstInt(i64, 0, 0), r, "parse.safe");
                } else {
                    LLVMTypeRef strtod_ty = LLVMFunctionType(dbl,
                        (LLVMTypeRef[]){ i8ptr, i8pp }, 2, 0);
                    LLVMValueRef f = LLVMGetNamedFunction(g->mod, "zan_rt_dbl_parse");
                    if (!f) f = LLVMAddFunction(g->mod, "zan_rt_dbl_parse", strtod_ty);
                    LLVMValueRef endp = emit_entry_alloca(g, i8ptr, "parse.endp");
                    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), endp);
                    LLVMValueRef s_safe = emit_parse_str_guard(g, s, expr->loc);
                    r = zan_call2(g->builder, strtod_ty, f,
                        (LLVMValueRef[]){ s_safe, endp }, 2, "parse");
                    LLVMValueRef ep = LLVMBuildLoad2(g->builder, i8ptr, endp, "parse.ep");
                    LLVMValueRef bad = emit_parse_bad_pred(g, s_safe, ep);
                    /* C# double.Parse 溢出抛 OverflowException：strtod 返
                     * ±HUGE_VAL（inf），判坏返 0。 */
                    LLVMTypeRef fabs_ty = LLVMFunctionType(dbl,
                        (LLVMTypeRef[]){ dbl }, 1, 0);
                    LLVMValueRef av = zan_call2(g->builder, fabs_ty,
                        get_libc_fn(g, "fabs", fabs_ty), &r, 1, "parse.fabs");
                    LLVMValueRef isinf = LLVMBuildFCmp(g->builder,
                        LLVMRealOEQ, av,
                        LLVMConstReal(dbl, 1.7976931348623157e308 * 2.0), "parse.inf");
                    bad = LLVMBuildOr(g->builder, bad, isinf, "parse.bad2");
                    emit_runtime_check(g, bad, expr->loc,
                        "double.Parse: input is not a valid number (or out of range)");
                    r = LLVMBuildSelect(g->builder, bad,
                        LLVMConstReal(dbl, 0.0), r, "parse.safe");
                }
                emit_release_owned_call_temp(g, expr->call.args.items[0], s, locals);
                return r;
            }
            if ((int_recv || dbl_recv) &&
                pm.len == 8 && memcmp(pm.str, "TryParse", 8) == 0 &&
                expr->call.args.count == 2) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, expr->call.args.items[0], locals);
                zan_type_t *at = infer_expr_type(g, expr->call.args.items[0], locals);
                if (!type_is_text_arg(at)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3058,
                                  "%.*s.TryParse requires a string argument",
                                  (int)rn.len, rn.str);
                    return LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
                }
                LLVMValueRef outp = emit_expr(g, expr->call.args.items[1], locals);
                LLVMValueRef endp = emit_entry_alloca(g, i8ptr, "endp");
                LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), endp);
                LLVMValueRef v;
                if (int_recv) {
                    LLVMTypeRef strtoll_ty = LLVMFunctionType(i64,
                        (LLVMTypeRef[]){ i8ptr, i8pp, i32t }, 3, 0);
                    LLVMValueRef f = get_libc_fn(g, "strtoll", strtoll_ty);
                    v = zan_call2(g->builder, strtoll_ty, f,
                        (LLVMValueRef[]){ s, endp, LLVMConstInt(i32t, 10, 0) }, 3, "tp");
                } else {
                    LLVMTypeRef strtod_ty = LLVMFunctionType(dbl,
                        (LLVMTypeRef[]){ i8ptr, i8pp }, 2, 0);
                    LLVMValueRef f = LLVMGetNamedFunction(g->mod, "zan_rt_dbl_parse");
                    if (!f) f = LLVMAddFunction(g->mod, "zan_rt_dbl_parse", strtod_ty);
                    v = zan_call2(g->builder, strtod_ty, f,
                        (LLVMValueRef[]){ s, endp }, 2, "tp");
                }
                LLVMValueRef e = LLVMBuildLoad2(g->builder, i8ptr, endp, "e");
                LLVMValueRef moved = zan_icmp(g->builder, LLVMIntNE, e, s, "moved");
                LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8, e, "ch");
                LLVMValueRef at_end = zan_icmp(g->builder, LLVMIntEQ, ch,
                    LLVMConstInt(i8, 0, 0), "atend");
                LLVMValueRef ok = zan_and(g->builder, moved, at_end, "ok");
                LLVMValueRef stored = LLVMBuildSelect(g->builder, ok, v,
                    int_recv ? LLVMConstInt(i64, 0, 0) : (LLVMValueRef)LLVMConstReal(dbl, 0.0), "tpv");
                if (LLVMGetTypeKind(LLVMTypeOf(outp)) == LLVMPointerTypeKind)
                    LLVMBuildStore(g->builder, stored, outp);
                emit_release_owned_call_temp(g, expr->call.args.items[0], s, locals);
                return ok;
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->call.callee->member.object->ident.name)) {
            zan_istr_t rn = expr->call.callee->member.object->ident.name;
            int str_recv = (rn.len == 6 && memcmp(rn.str, "string", 6) == 0) ||
                           (rn.len == 6 && memcmp(rn.str, "String", 6) == 0);
            zan_istr_t pm = expr->call.callee->member.name;
            if (str_recv && pm.len == 13 &&
                memcmp(pm.str, "IsNullOrEmpty", 13) == 0 &&
                expr->call.args.count == 1) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMValueRef s = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, s,
                    LLVMConstNull(i8ptr), "isnull");
                LLVMBasicBlockRef chk_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sne.chk");
                LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sne.done");
                LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
                LLVMBuildCondBr(g->builder, isnull, done_bb, chk_bb);
                LLVMPositionBuilderAtEnd(g->builder, chk_bb);
                LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8, s, "ch");
                LLVMValueRef isempty = zan_icmp(g->builder, LLVMIntEQ, ch,
                    LLVMConstInt(i8, 0, 0), "isempty");
                LLVMBasicBlockRef chk_end = LLVMGetInsertBlock(g->builder);
                LLVMBuildBr(g->builder, done_bb);
                LLVMPositionBuilderAtEnd(g->builder, done_bb);
                LLVMValueRef phi = LLVMBuildPhi(g->builder, LLVMInt1TypeInContext(g->ctx), "sne");
                LLVMValueRef inv[2] = { LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0), isempty };
                LLVMBasicBlockRef inb[2] = { cur_bb, chk_end };
                LLVMAddIncoming(phi, inv, inb, 2);
                emit_release_owned_call_temp(g, expr->call.args.items[0], s, locals);
                return phi;
            }
            if (str_recv && pm.len == 4 && memcmp(pm.str, "Join", 4) == 0 &&
                expr->call.args.count == 2) {
                LLVMValueRef sep = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef lst = emit_expr(g, expr->call.args.items[1], locals);
                LLVMValueRef hf = get_str_join_fn(g);
                LLVMValueRef res = zan_call2(g->builder,
                    LLVMGlobalGetValueType(hf), hf,
                    (LLVMValueRef[]){ sep, lst }, 2, "join");
                emit_release_owned_call_temp(g, expr->call.args.items[0], sep, locals);
                emit_release_owned_call_temp(g, expr->call.args.items[1], lst, locals);
                return res;
            }
            if (str_recv && pm.len == 6 && memcmp(pm.str, "Format", 6) == 0 &&
                expr->call.args.count >= 1 &&
                expr->call.args.items[0]->kind == AST_STRING_LITERAL) {
                zan_istr_t f = expr->call.args.items[0]->str_val;
                zan_type_t *str_ty = g->binder->type_string;
                LLVMValueRef res = NULL;
                int seg_start = 0;
                for (int i = 0; i < f.len; i++) {
                    if (f.str[i] == '{' && i + 2 < f.len + 1) {
                        int j = i + 1, idx = 0, has = 0, oflow = 0;
                        while (j < f.len && f.str[j] >= '0' && f.str[j] <= '9') {
                            if (!oflow) {
                                if (idx > 214748363 ||
                                    idx >= expr->call.args.count) oflow = 1;
                                else idx = idx * 10 + (f.str[j] - '0');
                            }
                            j++; has = 1;
                        }
                        if (has && !oflow && j < f.len && f.str[j] == '}' &&
                            idx + 1 < expr->call.args.count) {
                            if (i > seg_start) {
                                zan_istr_t seg = { f.str + seg_start, i - seg_start };
                                LLVMValueRef sl = emit_string_literal_rc(g, seg);
                                if (res) {
                                    LLVMValueRef nr = emit_str_concat(g, res, sl);
                                    emit_rc_release_for_type(g, str_ty, res);
                                    emit_rc_release_for_type(g, str_ty, sl);
                                    res = nr;
                                } else {
                                    res = sl;
                                }
                            }
                            zan_ast_node_t *arg_ast = expr->call.args.items[idx + 1];
                            LLVMValueRef av = emit_expr(g, arg_ast, locals);
                            LLVMValueRef as = emit_to_cstr_of(g, av, arg_ast, locals);
                            int as_owned =
                                LLVMGetTypeKind(LLVMTypeOf(av)) != LLVMPointerTypeKind ||
                                expr_yields_owned_rc_value(g, arg_ast, locals);
                            if (res) {
                                LLVMValueRef nr = emit_str_concat(g, res, as);
                                emit_rc_release_for_type(g, str_ty, res);
                                if (as_owned)
                                    emit_rc_release_for_type(g, str_ty, as);
                                res = nr;
                            } else if (as_owned) {
                                res = as;
                            } else {
                                emit_rc_retain_for_type(g, str_ty, as);
                                res = as;
                            }
                            i = j;
                            seg_start = j + 1;
                        }
                    }
                }
                if (seg_start < f.len || !res) {
                    zan_istr_t seg = { f.str + seg_start, f.len - seg_start };
                    LLVMValueRef sl = emit_string_literal_rc(g, seg);
                    if (res) {
                        LLVMValueRef nr = emit_str_concat(g, res, sl);
                        emit_rc_release_for_type(g, str_ty, res);
                        emit_rc_release_for_type(g, str_ty, sl);
                        res = nr;
                    } else {
                        res = sl;
                    }
                }
                return res;
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;

            if (callee->member.object->kind == AST_IDENTIFIER) {
                local_var_t *str_local = local_find(locals, callee->member.object->ident.name);
                if (str_local && LLVMGetTypeKind(LLVMTypeOf(LLVMBuildLoad2(g->builder,
                    LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0), str_local->alloca, "chk"))) == LLVMPointerTypeKind) {
                }
            }
            
            if (callee->member.object->kind == AST_IDENTIFIER) {
                zan_istr_t obj_name = callee->member.object->ident.name;
                if (obj_name.len == 7 && memcmp(obj_name.str, "Convert", 7) == 0) {
                    if (((method_name.len == 7 && memcmp(method_name.str, "ToInt32", 7) == 0) ||
                         (method_name.len == 7 && memcmp(method_name.str, "ToInt64", 7) == 0) ||
                         (method_name.len == 5 && memcmp(method_name.str, "ToInt", 5) == 0)) &&
                        !zan_type_defines(g, "Convert", "ToInt32") &&
                        !zan_type_defines(g, "Convert", "ToInt64") &&
                        !zan_type_defines(g, "Convert", "ToInt") &&
                        expr->call.args.count == 1) {
                        LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                        zan_type_t *at = infer_expr_type(g, expr->call.args.items[0], locals);
                        if (!type_is_textual_or_numeric(at)) {
                            zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3059,
                                          "Convert.%.*s: cannot convert '%.*s' "
                                          "to a number -- pass a string or a "
                                          "numeric value",
                                          (int)method_name.len, method_name.str,
                                          at && at->name.len ? (int)at->name.len : 6,
                                          at && at->name.len ? at->name.str : "<value>");
                            return LLVMConstInt(i64t, 0, 0);
                        }
                        LLVMTypeKind ak = LLVMGetTypeKind(LLVMTypeOf(arg));
                        LLVMValueRef parsed;
                        if (ak == LLVMPointerTypeKind) {
                            /* 字符串转 64 位有符号整数 (strtoll 规避 atoi 的 32 位截断)；
                             * A512：失败/残留/溢 int 范围 → 软诊断 + 安全值 0 */
                            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                            LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
                            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                            LLVMTypeRef strtoll_ty = LLVMFunctionType(i64t,
                                (LLVMTypeRef[]){ i8ptr, i8pp, i32t }, 3, 0);
                            LLVMValueRef f = get_libc_fn(g, "strtoll", strtoll_ty);
                            LLVMValueRef endp = emit_entry_alloca(g, i8ptr, "toint.endp");
                            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), endp);
                            LLVMValueRef s_safe = emit_parse_str_guard(g, arg, expr->loc);
                            /* 审计 P1-12 余项：strtoll 对超出 long 范围的
                             * 输入饱和返回 INT64_MAX/MIN 并置 errno=ERANGE
                             * ——只看返回值分不清「输入恰好是极值」与「饱和」
                             * （ToInt64("99999999999999999999") 曾静默钳成
                             * INT64_MAX，C# 语义是 OverflowException）。
                             * 先清零 errno，调用后读回比对 34（ERANGE）。 */
                            LLVMTypeRef errno_ty = LLVMFunctionType(
                                LLVMPointerType(i32t, 0), NULL, 0, 0);
                            /* 按**目标**平台选 TLS errno 取址函数，不是按宿主
                             * #if：Windows 宿主交叉编 macOS 时 IR 名 `_errno`
                             * 会被 Mach-O 再加前导下划线成 `__errno`，而
                             * libSystem 导出的 C 符号是 `__error`——链接期才
                             * 炸，且炸在离病灶最远的 Convert.ToInt 调用点上。
                             * 分支次序对齐 Socket.zan 的 DllImport 守卫
                             * （ANDROID 也定义 LINUX，安卓分支必须在前）。 */
                            const char *errno_sym = "__errno_location";
                            if (strstr(g->target_triple, "android") ||
                                strstr(g->target_triple, "ohos")) {
                                errno_sym = "__errno";
                            } else if (g->target_is_macos || g->target_is_ios) {
                                errno_sym = "__error";
                            } else if (g->target_is_windows) {
                                errno_sym = "_errno";
                            }
                            LLVMValueRef errno_fn = get_libc_fn(g, errno_sym, errno_ty);
                            LLVMValueRef errnop = zan_call2(g->builder, errno_ty,
                                errno_fn, NULL, 0, "toint.errnop");
                            LLVMBuildStore(g->builder, LLVMConstInt(i32t, 0, 0),
                                errnop);
                            parsed = zan_call2(g->builder, strtoll_ty, f,
                                (LLVMValueRef[]){ s_safe, endp,
                                                  LLVMConstInt(i32t, 10, 0) }, 3, "toint");
                            LLVMValueRef ep = LLVMBuildLoad2(g->builder, i8ptr,
                                endp, "toint.ep");
                            LLVMValueRef bad = emit_parse_bad_pred(g, s_safe, ep);
                            if (method_name.str[6] != '4') {
                                /* ToInt32/ToInt：C# 超出 int 范围抛 OverflowException */
                                bad = LLVMBuildOr(g->builder, bad, LLVMBuildOr(
                                    g->builder,
                                    zan_icmp(g->builder, LLVMIntSLT, parsed,
                                        LLVMConstInt(i64t, (int64_t)INT32_MIN, 0), "toint.lo"),
                                    zan_icmp(g->builder, LLVMIntSGT, parsed,
                                        LLVMConstInt(i64t, INT32_MAX, 0), "toint.hi"),
                                    "toint.over"), "toint.bad2");
                            }
                            /* ERANGE 兜底：两档共用（i32 越界必然先饱和到
                             * i64 边缘再置 ERANGE，与上面的范围谓词等价；
                             * i64 档只有它能抓到）。 */
                            LLVMValueRef er = LLVMBuildLoad2(g->builder, i32t,
                                errnop, "toint.errno");
                            bad = LLVMBuildOr(g->builder, bad,
                                zan_icmp(g->builder, LLVMIntEQ, er,
                                    LLVMConstInt(i32t, 34, 0), "toint.erange"),
                                "toint.bad3");
                            emit_runtime_check(g, bad, expr->loc,
                                "Convert.ToInt*: input is not a valid integer (or out of range)");
                            parsed = LLVMBuildSelect(g->builder, bad,
                                LLVMConstInt(i64t, 0, 0), parsed, "toint.safe");
                        } else if (ak == LLVMDoubleTypeKind || ak == LLVMFloatTypeKind) {
                            /* C# Convert.ToInt32(double) 是舍入到最近（银行家），
                             * 不是截断——先 rint 再转整（曾 ToInt32(3.9)=3，
                             * C# 是 4；TASKS.md A513）。 */
                            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                            LLVMTypeRef d1_ty = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
                            LLVMValueRef as_dbl = (ak == LLVMDoubleTypeKind)
                                ? arg : LLVMBuildFPExt(g->builder, arg, dbl, "toint.ext");
                            LLVMValueRef r = zan_call2(g->builder, d1_ty,
                                get_libc_fn(g, "rint", d1_ty), &as_dbl, 1, "toint.rint");
                            parsed = LLVMBuildFPToSI(g->builder, r, i64t, "toint");
                        } else if (ak == LLVMIntegerTypeKind) {
                            if (LLVMTypeOf(arg) == i64t)
                                parsed = arg;
                            else if (LLVMGetIntTypeWidth(LLVMTypeOf(arg)) == 1)
                                /* bool 按 0/1 零扩展——C# Convert.ToInt32(true)=1；
                                 * 符号扩展曾得 -1（TASKS.md A498）。 */
                                parsed = LLVMBuildZExt(g->builder, arg, i64t, "toint.zext");
                            else
                                parsed = LLVMBuildSExt(g->builder, arg, i64t, "toint");
                            /* 审计 P1-12：i64 实参超 int 范围曾静默截断回绕
                             * （ToInt32(2147483648L) = -2147483648）——C# 语义是
                             * OverflowException。与字符串路径同判：软诊断 + 0
                             * （--strict-runtime 下 exit 70）。ToInt64 不需要
                             * （全宽）。窄整型 sext 后必在范围内，不用查。 */
                            if (LLVMTypeOf(arg) == i64t && method_name.str[6] != '4') {
                                LLVMValueRef over = LLVMBuildOr(g->builder,
                                    zan_icmp(g->builder, LLVMIntSLT, parsed,
                                        LLVMConstInt(i64t, (int64_t)INT32_MIN, 0), "tointi.lo"),
                                    zan_icmp(g->builder, LLVMIntSGT, parsed,
                                        LLVMConstInt(i64t, INT32_MAX, 0), "tointi.hi"),
                                    "tointi.over");
                                emit_runtime_check(g, over, expr->loc,
                                    "Convert.ToInt*: value is outside the range of int");
                                parsed = LLVMBuildSelect(g->builder, over,
                                    LLVMConstInt(i64t, 0, 0), parsed, "tointi.safe");
                            }
                        } else {
                            parsed = LLVMConstInt(i64t, 0, 0);
                        }
                        emit_release_owned_call_temp(
                            g, expr->call.args.items[0], arg, locals);
                        return parsed;
                    }
                    if (method_name.len == 8 && memcmp(method_name.str, "ToString", 8) == 0 &&
                        (!zan_type_defines(g, "Convert", "ToString") ||
                         !zan_convert_tostring_exact_decl(g, expr->call.args.items[0],
                                                          locals)) &&
                        expr->call.args.count == 1) {
                        LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        zan_type_t *at = infer_expr_type(g, expr->call.args.items[0], locals);
                        if (!type_is_textual_or_numeric(at)) {
                            /* A463: the type is named the way the user wrote
                               it -- the shape comes along (`int[]`, `Box`,
                               `D`). One renderer for every diagnostic, so
                               this one cannot drift from the checker's. */
                            char tname[256];
                            zan_type_display(at, tname, sizeof(tname));
                            if (!tname[0])
                                snprintf(tname, sizeof(tname), "<value>");
                            zan_diag_emit_code(g->diag, DIAG_ERROR,
                                expr->call.args.items[0]->loc, 3060,
                                "Convert.ToString: cannot convert '%s' to a "
                                "string -- call .ToString() on the value instead",
                                tname);
                            return LLVMConstNull(i8ptr);
                        }
                        if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMPointerTypeKind) {
                            /* A444 安全栏：静态类型是 object 时负载可能是类
                             * 实例——运行期判别，字符串才 strlen+memcpy，
                             * 非字符串走软运行时错（可 catch、strict 模式
                             * exit 70）+ 空串兜底。原先拿 strlen 量对象
                             * 指针 → 越界读 + 内存信息泄露。判别器在
                             * rt_sync.c：字符串 [-8] 高 32 位恒为 ZANS
                             * 标签，类实例的 ARC 站点指针撞不上；null
                             * 一并判为非字符串（顺带修掉 null 的 strlen UB）。 */
                            LLVMValueRef safe_arg = arg;
                            if (at && at->kind == TYPE_OBJECT) {
                                /* 判别器在 rt_sync.c：把 sync 运行时拉进链接集 */
                                g->uses_sync_runtime = true;
                                LLVMTypeRef isstr_ty = LLVMFunctionType(
                                    LLVMInt32TypeInContext(g->ctx),
                                    (LLVMTypeRef[]){ i8ptr }, 1, 0);
                                LLVMValueRef isstr_fn =
                                    LLVMGetNamedFunction(g->mod, "zan_obj_slot_is_string");
                                if (!isstr_fn)
                                    isstr_fn = LLVMAddFunction(g->mod,
                                        "zan_obj_slot_is_string", isstr_ty);
                                LLVMValueRef is_str = zan_call2(g->builder,
                                    isstr_ty, isstr_fn, &arg, 1, "is.str");
                                LLVMValueRef bad = zan_icmp(g->builder,
                                    LLVMIntEQ, is_str,
                                    LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0),
                                    "str.bad");
                                emit_runtime_check(g, bad,
                                    expr->call.args.items[0]->loc,
                                    "Convert.ToString: object holds a "
                                    "non-string value -- call .ToString() "
                                    "on the concrete value instead");
                                LLVMValueRef empty =
                                    zan_irgen_intern_string(g, "");
                                LLVMValueRef hit = zan_icmp(g->builder,
                                    LLVMIntNE, is_str,
                                    LLVMConstInt(LLVMInt32TypeInContext(g->ctx),
                                        0, 0), "str.hit");
                                safe_arg = LLVMBuildSelect(g->builder, hit,
                                    arg, empty, "str.safe");
                            }
                            LLVMTypeRef strlen_ty = LLVMFunctionType(i64,
                                (LLVMTypeRef[]){ i8ptr }, 1, 0);
                            LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr,
                                (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
                            LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);
                            LLVMValueRef n = zan_call2(g->builder, strlen_ty,
                                g->fn_strlen, &safe_arg, 1, "n");
                            LLVMValueRef bufsz = zan_add(g->builder, n,
                                LLVMConstInt(i64, 1, 0), "bsz");
                            LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
                            LLVMValueRef mcargs[] = { buf, safe_arg, bufsz };
                            zan_call2(g->builder, memcpy_ty, memcpy_fn, mcargs, 3, "");
                            emit_release_owned_call_temp(
                                g, expr->call.args.items[0], arg, locals);
                            return buf;
                        }
                        if (expr_is_char(g, expr->call.args.items[0], locals))
                            return emit_char_to_cstr(g, arg);
                        if (llvm_is_nullable(LLVMTypeOf(arg))) {
                            zan_type_t *st = infer_expr_type(
                                g, expr->call.args.items[0], locals);
                            bool uns = st && st->element_type &&
                                       (st->element_type->kind == TYPE_UINT ||
                                        st->element_type->kind == TYPE_ULONG);
                            return emit_to_cstr_u(g, arg, uns ? 1 : 0);
                        }
                        /* bool 渲染成 "true"/"false"（不是 itoa 的 "1"）——
                           与 b.ToString() / Console.WriteLine / 插值同口径。
                           判据是**语义类型**不是 LLVM 宽度：泛型方法返回值
                           （List<T>.Contains 这类）被提升成 i64，按宽度判会
                           掉进 itoa 渲染成 "1"。 */
                        {
                            LLVMValueRef bc = emit_bool_cond(g, arg,
                                expr->call.args.items[0], locals);
                            if (bc) return emit_bool_to_cstr(g, bc);
                        }
                        /* 双精度浮点数最短往返格式化缓冲区 (40 字节容量) */
                        LLVMValueRef buf_size = LLVMConstInt(i64, 40, 0);
                        LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
                        LLVMTypeKind atk = LLVMGetTypeKind(LLVMTypeOf(arg));
                        if (atk == LLVMDoubleTypeKind || atk == LLVMFloatTypeKind) {
                            emit_dbl_str(g, buf, buf_size, arg);
                            return buf;
                        }
                        bool is_ul = expr_is_ulong(
                            g, expr->call.args.items[0], locals);
                        emit_itoa_into(g, buf,
                            emit_widen_i64_for_print(g, arg), is_ul ? 1 : 0);
                        return buf;
                    }
                }
            }
        }

        /* str */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 9 && memcmp(sm.str, "Substring", 9) == 0 &&
                (expr->call.args.count == 1 || expr->call.args.count == 2) &&
                is_string_expr(g, sc->member.object, locals)) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef start = coerce_int_to(g,
                    emit_expr(g, expr->call.args.items[0], locals), i64);
                bool bounded = expr_has_reliable_string_bounds(sc->member.object,
                                                              locals) != 0;
                LLVMValueRef total = (bounded || expr->call.args.count == 1)
                    ? emit_string_length(g, s, expr->loc) : NULL;
                LLVMValueRef slen;
                if (expr->call.args.count == 2) {
                    slen = coerce_int_to(g,
                        emit_expr(g, expr->call.args.items[1], locals), i64);
                    if (total) {
                        emit_span_window_check(g, start, slen, total, expr->loc,
                                               "substring");
                        emit_span_safe_window(g, &start, &slen, total, expr->loc,
                                              "substring");
                    }
                } else {
                    emit_index_range_check(g, start, total, true, expr->loc,
                                           "substring");
                    start = emit_index_safe_check(g, start, total, true, expr->loc,
                                                  "substring");
                    slen = zan_sub(g->builder, total, start, "subl");
                }
                slen = LLVMBuildSelect(g->builder,
                    zan_icmp(g->builder, LLVMIntSLT, slen,
                             LLVMConstInt(i64, 0, 0), "subl.neg"),
                    LLVMConstInt(i64, 0, 0), slen, "subl.safe");
                {
                    LLVMValueRef snull = zan_icmp(g->builder, LLVMIntEQ, s,
                        LLVMConstNull(LLVMTypeOf(s)), "subnull");
                    s = emit_soft_base_select(g, s, snull, expr->loc);
                }

                LLVMContextRef c = g->ctx;
                LLVMBasicBlockRef bb_slice = LLVMAppendBasicBlockInContext(c, g->current_fn, "sub.slice");
                LLVMBasicBlockRef bb_full = total ? LLVMAppendBasicBlockInContext(c, g->current_fn, "sub.full") : NULL;
                LLVMBasicBlockRef bb_empty = LLVMAppendBasicBlockInContext(c, g->current_fn, "sub.empty");
                LLVMBasicBlockRef bb_join = LLVMAppendBasicBlockInContext(c, g->current_fn, "sub.join");

                LLVMValueRef is_empty = zan_icmp(g->builder, LLVMIntEQ, slen, LLVMConstInt(i64, 0, 0), "sub.isz");
                if (total) {
                    LLVMValueRef is_st0 = zan_icmp(g->builder, LLVMIntEQ, start, LLVMConstInt(i64, 0, 0), "sub.st0");
                    LLVMValueRef is_len_all = zan_icmp(g->builder, LLVMIntEQ, slen, total, "sub.lall");
                    LLVMValueRef is_full = zan_and(g->builder, is_st0, is_len_all, "sub.isfull");
                    LLVMBasicBlockRef bb_chk_full = LLVMAppendBasicBlockInContext(c, g->current_fn, "sub.chkfull");
                    LLVMBuildCondBr(g->builder, is_empty, bb_empty, bb_chk_full);

                    LLVMPositionBuilderAtEnd(g->builder, bb_chk_full);
                    LLVMBuildCondBr(g->builder, is_full, bb_full, bb_slice);

                    LLVMPositionBuilderAtEnd(g->builder, bb_full);
                    LLVMTypeRef str_rel_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    zan_call2(g->builder, str_rel_type, g->rt_str_retain, &s, 1, "");
                    LLVMBuildBr(g->builder, bb_join);
                } else {
                    LLVMBuildCondBr(g->builder, is_empty, bb_empty, bb_slice);
                }

                LLVMPositionBuilderAtEnd(g->builder, bb_empty);
                LLVMValueRef empty_str = zan_irgen_intern_string(g, "");
                LLVMBuildBr(g->builder, bb_join);

                LLVMPositionBuilderAtEnd(g->builder, bb_slice);
                LLVMValueRef bufsz = zan_add(g->builder, slen, LLVMConstInt(i64, 1, 0), "bsz");
                LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
                LLVMValueRef srcp = LLVMBuildGEP2(g->builder, i8, s, &start, 1, "srcp");
                LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
                if (!memcpy_fn) {
                    memcpy_fn = LLVMAddFunction(g->mod, "memcpy",
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0));
                }
                LLVMValueRef mcargs[] = { buf, srcp, slen };
                zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0),
                    memcpy_fn, mcargs, 3, "");
                LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, buf, &slen, 1, "endp");
                LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
                /* 无 \0 范围内的切片：slen 为切片结果字符串长度 */
                emit_string_len_set(g, buf, slen);
                LLVMBuildBr(g->builder, bb_join);

                LLVMPositionBuilderAtEnd(g->builder, bb_join);
                LLVMValueRef phi = LLVMBuildPhi(g->builder, i8ptr, "sub.res");
                LLVMValueRef phi_in_vals[3];
                LLVMBasicBlockRef phi_in_bbs[3];
                int np = 0;
                phi_in_vals[np] = empty_str; phi_in_bbs[np] = bb_empty; np++;
                if (total) {
                    phi_in_vals[np] = s; phi_in_bbs[np] = bb_full; np++;
                }
                phi_in_vals[np] = buf; phi_in_bbs[np] = bb_slice; np++;
                LLVMAddIncoming(phi, phi_in_vals, phi_in_bbs, (unsigned)np);

                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                return phi;
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 8 && memcmp(sm.str, "Contains", 8) == 0 &&
                expr->call.args.count == 1 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals)) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                int sub_owned;
                LLVMValueRef sub = emit_string_like_arg(g, expr->call.args.items[0],
                                                        locals, &sub_owned);
                LLVMTypeRef strstr_type = LLVMFunctionType(i8ptr,
                    (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                LLVMValueRef strstr_fn = LLVMGetNamedFunction(g->mod, "strstr");
                if (!strstr_fn)
                    strstr_fn = LLVMAddFunction(g->mod, "strstr", strstr_type);
                LLVMValueRef ss_args[] = { s, sub };
                LLVMValueRef hit = zan_call2(g->builder, strstr_type,
                    strstr_fn, ss_args, 2, "ss");
                LLVMValueRef res = zan_icmp(g->builder, LLVMIntNE, hit,
                    LLVMConstPointerNull(i8ptr), "ctn");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], sub,
                                        sub_owned, locals);
                return res;
            }
        }

        /* str */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            int is_idx = (sm.len == 7 && memcmp(sm.str, "IndexOf", 7) == 0);
            int is_lidx = (sm.len == 11 && memcmp(sm.str, "LastIndexOf", 11) == 0);
            if (is_idx && (expr->call.args.count == 1 || expr->call.args.count == 2) &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals)) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef search = s;
                LLVMValueRef start = LLVMConstInt(i64, 0, 0);
                LLVMValueRef valid_start = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0);
                if (expr->call.args.count == 2) {
                    start = emit_expr(g, expr->call.args.items[1], locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(start)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(start)) < 64)
                        start = LLVMBuildSExt(g->builder, start, i64, "idx.start");
                    LLVMValueRef slen = emit_string_length(g, s, expr->loc);
                    LLVMValueRef nonneg = zan_icmp(g->builder, LLVMIntSGE, start,
                        LLVMConstInt(i64, 0, 0), "idx.nonneg");
                    LLVMValueRef within = zan_icmp(g->builder, LLVMIntSLE, start,
                        slen, "idx.within");
                    valid_start = zan_and(g->builder, nonneg, within, "idx.valid");
                    search = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                        s, &start, 1, "idx.search");
                }
                int sub_owned;
                LLVMValueRef sub = emit_string_like_arg(g, expr->call.args.items[0],
                                                        locals, &sub_owned);
                LLVMTypeRef strstr_type = LLVMFunctionType(i8ptr,
                    (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                LLVMValueRef strstr_fn = get_libc_fn(g, "strstr", strstr_type);
                LLVMValueRef ss_args[] = { search, sub };
                LLVMValueRef hit = zan_call2(g->builder, strstr_type,
                    strstr_fn, ss_args, 2, "ss");
                LLVMValueRef diff = zan_sub(g->builder,
                    LLVMBuildPtrToInt(g->builder, hit, i64, "hi"),
                    LLVMBuildPtrToInt(g->builder, s, i64, "si"), "diff");
                LLVMValueRef missed = zan_icmp(g->builder, LLVMIntEQ, hit,
                    LLVMConstPointerNull(i8ptr), "missed");
                LLVMValueRef invalid_start = zan_icmp(g->builder, LLVMIntEQ,
                    valid_start, LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0),
                    "idx.invalid");
                LLVMValueRef failed = zan_or(g->builder, missed,
                    invalid_start, "idx.failed");
                LLVMValueRef res = LLVMBuildSelect(g->builder, failed,
                    LLVMConstInt(i64, (uint64_t)-1, 1), diff, "idx");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], sub,
                                        sub_owned, locals);
                return res;
            }
            if (is_lidx && expr->call.args.count == 1 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals)) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                int sub_owned;
                LLVMValueRef sub = emit_string_like_arg(g, expr->call.args.items[0],
                                                        locals, &sub_owned);
                LLVMValueRef lif = get_str_last_index_of_fn(g);
                LLVMValueRef li_args[] = { s, sub };
                LLVMValueRef res = zan_call2(g->builder, LLVMGlobalGetValueType(lif),
                    lif, li_args, 2, "lidx");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], sub,
                                        sub_owned, locals);
                return res;
            }
        }

        /* str */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 6 && memcmp(sm.str, "Equals", 6) == 0 &&
                expr->call.args.count == 1 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals)) {
                LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                int p_owned;
                LLVMValueRef p = emit_string_like_arg(g, expr->call.args.items[0],
                                                      locals, &p_owned);
                s = emit_str_nonnull(g, s);
                LLVMValueRef ocmp = get_str_ordinal_cmp_fn(g, expr->loc);
                LLVMValueRef cmp = zan_call2(g->builder,
                    LLVMGlobalGetValueType(ocmp), ocmp,
                    (LLVMValueRef[]){ s, p }, 2, "eq.cmp");
                LLVMValueRef res = zan_icmp(g->builder, LLVMIntEQ, cmp,
                    LLVMConstInt(i32, 0, 0), "eq.str");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], p,
                                        p_owned, locals);
                return res;
            }
        }

        /* str.StartsWith(p) / str.EndsWith(p) -> bool. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            int is_sw = (sm.len == 10 && memcmp(sm.str, "StartsWith", 10) == 0);
            int is_ew = (sm.len == 8 && memcmp(sm.str, "EndsWith", 8) == 0);
            if ((is_sw || is_ew) && expr->call.args.count == 1 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals)) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                int p_owned;
                LLVMValueRef p = emit_string_like_arg(g, expr->call.args.items[0],
                                                      locals, &p_owned);
                LLVMValueRef lp = zan_call2(g->builder, strlen_ty, g->fn_strlen, &p, 1, "lp");
                LLVMValueRef res;
                if (is_sw) {
                    LLVMTypeRef strncmp_ty = LLVMFunctionType(i32,
                        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
                    LLVMValueRef strncmp_fn = get_libc_fn(g, "strncmp", strncmp_ty);
                    LLVMValueRef nc_args[] = { s, p, lp };
                    LLVMValueRef cmp = zan_call2(g->builder, strncmp_ty,
                        strncmp_fn, nc_args, 3, "swcmp");
                    res = zan_icmp(g->builder, LLVMIntEQ, cmp,
                        LLVMConstInt(i32, 0, 0), "sw");
                } else {
                    LLVMValueRef ls = zan_call2(g->builder, strlen_ty, g->fn_strlen, &s, 1, "ls");
                    LLVMValueRef off = zan_sub(g->builder, ls, lp, "off");
                    LLVMValueRef fits = zan_icmp(g->builder, LLVMIntSGE, off,
                        LLVMConstInt(i64, 0, 0), "fits");
                    LLVMValueRef offc = LLVMBuildSelect(g->builder, fits, off,
                        LLVMConstInt(i64, 0, 0), "offc");
                    LLVMValueRef tail = LLVMBuildGEP2(g->builder, i8, s, &offc, 1, "tailp");
                    LLVMTypeRef strcmp_ty = LLVMFunctionType(i32,
                        (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                    LLVMValueRef strcmp_fn = get_libc_fn(g, "strcmp", strcmp_ty);
                    LLVMValueRef sc_args[] = { tail, p };
                    LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty,
                        strcmp_fn, sc_args, 2, "ewcmp");
                    LLVMValueRef eq = zan_icmp(g->builder, LLVMIntEQ, cmp,
                        LLVMConstInt(i32, 0, 0), "eweq");
                    res = zan_and(g->builder, fits, eq, "ew");
                }
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], p,
                                        p_owned, locals);
                return res;
            }
        }

        /* str.Replace(from, to) -> fresh rc string. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 7 && memcmp(sm.str, "Replace", 7) == 0 &&
                expr->call.args.count == 2 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals) &&
                is_string_like_expr(g, expr->call.args.items[1], locals)) {
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                int from_owned, to_owned;
                LLVMValueRef from = emit_string_like_arg(g, expr->call.args.items[0],
                                                         locals, &from_owned);
                LLVMValueRef to = emit_string_like_arg(g, expr->call.args.items[1],
                                                       locals, &to_owned);
                LLVMValueRef rf = get_str_replace_fn(g);
                LLVMValueRef rargs[] = { s, from, to };
                LLVMValueRef res = zan_call2(g->builder,
                    LLVMGlobalGetValueType(rf), rf, rargs, 3, "repl");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], from,
                                        from_owned, locals);
                release_string_like_arg(g, expr->call.args.items[1], to,
                                        to_owned, locals);
                return res;
            }
        }

        /* str */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 0) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            int is_trim = (sm.len == 4 && memcmp(sm.str, "Trim", 4) == 0);
            int is_up = (sm.len == 7 && memcmp(sm.str, "ToUpper", 7) == 0);
            int is_low = (sm.len == 7 && memcmp(sm.str, "ToLower", 7) == 0);
            if ((is_trim || is_up || is_low) &&
                is_string_expr(g, sc->member.object, locals)) {
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef hf = is_trim ? get_str_trim_fn(g)
                                          : get_str_case_fn(g, is_up);
                LLVMValueRef res = zan_call2(g->builder,
                    LLVMGlobalGetValueType(hf), hf, &s, 1, "strh");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                return res;
            }
        }

        /* str */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            (expr->call.args.count == 1 || expr->call.args.count == 2)) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 5 && memcmp(sm.str, "Split", 5) == 0 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals)) {
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                int sep_owned;
                LLVMValueRef sep = emit_string_like_arg(g, expr->call.args.items[0],
                                                        locals, &sep_owned);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef opt = LLVMConstInt(i64, 0, 0);
                if (expr->call.args.count == 2) {
                    LLVMValueRef raw_opt = emit_expr(g, expr->call.args.items[1], locals);
                    opt = coerce_int_to(g, raw_opt, i64);
                }
                LLVMValueRef lst = emit_alloc_rc_collection(g, expr, 24, 1,
                                                            g->binder->type_string);
                LLVMValueRef lp = LLVMBuildBitCast(g->builder, lst,
                    LLVMPointerType(g->list_struct_type, 0), "lp");
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0),
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "cnt"));
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0),
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 1, "cap"));
                LLVMBuildStore(g->builder, LLVMConstNull(LLVMPointerType(i64, 0)),
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "df"));
                LLVMValueRef hf = get_str_split_fn(g);
                zan_call2(g->builder, LLVMGlobalGetValueType(hf), hf,
                    (LLVMValueRef[]){ s, sep, lst, opt }, 4, "");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], sep,
                                        sep_owned, locals);
                return lst;
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_IDENTIFIER &&
            expr->call.args.count == 2 &&
            expr->call.callee->member.name.len == 8 &&
            memcmp(expr->call.callee->member.name.str, "TryParse", 8) == 0) {
            zan_symbol_t *es = zan_binder_lookup(g->binder,
                expr->call.callee->member.object->ident.name);
            if (es && es->kind == SYM_ENUM && es->member_count > 0) {
                zan_symbol_t **mems = (zan_symbol_t **)calloc(
                    (size_t)es->member_count, sizeof(*mems));
                long long *vals = (long long *)calloc(
                    (size_t)es->member_count, sizeof(*vals));
                int n = irgen_enum_members(es, mems, vals,
                                           es->member_count);
                LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8pt = LLVMPointerType(i8t, 0);
                LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef strcmp_ty = LLVMFunctionType(i32t,
                    (LLVMTypeRef[]){ i8pt, i8pt }, 2, 0);
                LLVMValueRef s = emit_expr(g, expr->call.args.items[0],
                                           locals);
                zan_ast_node_t *out_arg = expr->call.args.items[1];
                LLVMValueRef out_ptr = NULL;
                if (out_arg->kind == AST_REF_ARG) {
                    out_ptr = emit_expr(g, out_arg, locals);
                } else if (out_arg->kind == AST_IDENTIFIER) {
                    local_var_t *ol = local_find(locals,
                        out_arg->ident.name);
                    if (ol) { out_ptr = ol->alloca; }
                }
                LLVMValueRef res_a = emit_entry_alloca(g, i32t, "tp.res");
                LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(
                    g->ctx, g->current_fn, "tp.done");
                LLVMBasicBlockRef miss_bb = LLVMAppendBasicBlockInContext(
                    g->ctx, g->current_fn, "tp.miss");
                for (int i = 0; i < n; i++) {
                    LLVMBasicBlockRef next_bb =
                        (i == n - 1)
                            ? miss_bb
                            : LLVMAppendBasicBlockInContext(g->ctx,
                                g->current_fn, "tp.chk");
                    LLVMValueRef lit = emit_string_literal_rc(g,
                        mems[i]->name);
                    LLVMValueRef cmpv = zan_call2(g->builder, strcmp_ty,
                        g->fn_strcmp, (LLVMValueRef[]){ s, lit }, 2,
                        "tp.cmp");
                    LLVMValueRef eq = LLVMBuildICmp(g->builder, LLVMIntEQ,
                        cmpv, LLVMConstInt(i32t, 0, 0), "tp.eq");
                    LLVMBasicBlockRef hit_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, g->current_fn, "tp.hit");
                    LLVMBuildCondBr(g->builder, eq, hit_bb, next_bb);
                    LLVMPositionBuilderAtEnd(g->builder, hit_bb);
                    if (out_ptr) {
                        emit_typed_out_store(g, es->type, out_ptr,
                            LLVMConstInt(i64t,
                                (unsigned long long)vals[i], 0));
                    }
                    LLVMBuildStore(g->builder, LLVMConstInt(i32t, 1, 0),
                                   res_a);
                    LLVMBuildBr(g->builder, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, next_bb);
                }
                if (out_ptr) {
                    emit_typed_out_store(g, es->type, out_ptr,
                        LLVMConstInt(i64t, 0, 0));
                }
                LLVMBuildStore(g->builder, LLVMConstInt(i32t, 0, 0), res_a);
                LLVMBuildBr(g->builder, done_bb);
                LLVMPositionBuilderAtEnd(g->builder, done_bb);
                emit_release_owned_call_temp(g, expr->call.args.items[0], s,
                                             locals);
                free(mems);
                free(vals);
                return LLVMBuildLoad2(g->builder, i32t, res_a, "tp.out");
            }
        }

        /* value */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 0) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 8 && memcmp(sm.str, "ToString", 8) == 0) {
                zan_type_t *rt_ty = infer_expr_type(g, sc->member.object, locals);
                int handled_kind = rt_ty &&
                    (rt_ty->kind == TYPE_BOOL || rt_ty->kind == TYPE_BYTE ||
                     rt_ty->kind == TYPE_SHORT || rt_ty->kind == TYPE_INT ||
                     rt_ty->kind == TYPE_LONG || rt_ty->kind == TYPE_SBYTE ||
                     rt_ty->kind == TYPE_USHORT || rt_ty->kind == TYPE_UINT ||
                     rt_ty->kind == TYPE_ULONG || rt_ty->kind == TYPE_FLOAT ||
                     rt_ty->kind == TYPE_DOUBLE || rt_ty->kind == TYPE_CHAR ||
                     rt_ty->kind == TYPE_STRING || rt_ty->kind == TYPE_ENUM);
                if (handled_kind) {
                    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef v = emit_expr(g, sc->member.object, locals);
                    LLVMTypeKind vk = LLVMGetTypeKind(LLVMTypeOf(v));
                    if (rt_ty->kind == TYPE_BOOL && vk == LLVMIntegerTypeKind) {
                        /* 语义类型是 bool 就渲染成 "true"/"false"：泛型方法
                           返回值被提升成 i64 时也要认（按宽度判会渲染成 "1"） */
                        LLVMValueRef bc = emit_bool_cond(g, v,
                            sc->member.object, locals);
                        if (bc) return emit_bool_to_cstr(g, bc);
                    }
                    if (vk == LLVMPointerTypeKind) {
                        LLVMTypeRef strlen_ty = LLVMFunctionType(i64,
                            (LLVMTypeRef[]){ i8ptr }, 1, 0);
                        LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr,
                            (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
                        LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);
                        LLVMValueRef n = zan_call2(g->builder, strlen_ty,
                            g->fn_strlen, &v, 1, "n");
                        LLVMValueRef bufsz = zan_add(g->builder, n,
                            LLVMConstInt(i64, 1, 0), "bsz");
                        LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
                        LLVMValueRef mcargs[] = { buf, v, bufsz };
                        zan_call2(g->builder, memcpy_ty, memcpy_fn, mcargs, 3, "");
                        emit_release_owned_call_temp(g, sc->member.object, v, locals);
                        return buf;
                    }
                    if (rt_ty->kind == TYPE_ENUM && rt_ty->sym &&
                        rt_ty->sym->member_count > 0) {
                        int cap = rt_ty->sym->member_count;
                        zan_symbol_t **mems = (zan_symbol_t **)calloc(
                            (size_t)cap, sizeof(*mems));
                        long long *vals = (long long *)calloc(
                            (size_t)cap, sizeof(*vals));
                        int n = irgen_enum_members(rt_ty->sym, mems, vals,
                                                   cap);
                        if (n == 0) {
                            free(mems);
                            free(vals);
                        }
                        if (n > 0) {
                            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                            LLVMValueRef v64 = emit_widen_i64_for_print(g, v);
                            LLVMValueRef fn = LLVMGetBasicBlockParent(
                                LLVMGetInsertBlock(g->builder));
                            LLVMBasicBlockRef joinbb =
                                LLVMAppendBasicBlockInContext(g->ctx, fn,
                                                              "em.join");
                            LLVMBasicBlockRef *hitbbs = (LLVMBasicBlockRef *)
                                calloc((size_t)n, sizeof(*hitbbs));
                            LLVMValueRef *lits = (LLVMValueRef *)calloc(
                                (size_t)n, sizeof(*lits));
                            LLVMBasicBlockRef cur =
                                LLVMGetInsertBlock(g->builder);
                            for (int i = 0; i < n; i++) {
                                LLVMBasicBlockRef miss =
                                    (i == n - 1)
                                        ? LLVMAppendBasicBlockInContext(g->ctx,
                                            fn, "em.num")
                                        : LLVMAppendBasicBlockInContext(g->ctx,
                                            fn, "em.chk");
                                LLVMValueRef hit = LLVMBuildICmp(g->builder,
                                    LLVMIntEQ, v64,
                                    LLVMConstInt(i64t,
                                        (unsigned long long)vals[i], 0),
                                    "em.val");
                                hitbbs[i] = LLVMAppendBasicBlockInContext(
                                    g->ctx, fn, "em.hit");
                                LLVMBuildCondBr(g->builder, hit, hitbbs[i],
                                                miss);
                                /* 字面量在对应分支基本块内具化以保证支配 PHI 节点输入 */
                                LLVMPositionBuilderAtEnd(g->builder, hitbbs[i]);
                                lits[i] = emit_string_literal_rc(g,
                                    mems[i]->name);
                                LLVMBuildBr(g->builder, joinbb);
                                LLVMPositionBuilderAtEnd(g->builder, miss);
                                cur = miss;
                            }
                            LLVMValueRef ebuf = emit_string_alloc_rc(g,
                                LLVMConstInt(i64t, 32, 0));
                            emit_itoa_into(g, ebuf, v64, 0);
                            LLVMBuildBr(g->builder, joinbb);
                            LLVMPositionBuilderAtEnd(g->builder, joinbb);
                            LLVMValueRef phi = LLVMBuildPhi(g->builder, i8ptr,
                                                            "em.name");
                            LLVMValueRef *vals_in = (LLVMValueRef *)calloc(
                                (size_t)n + 1, sizeof(LLVMValueRef));
                            LLVMBasicBlockRef *blks_in = (LLVMBasicBlockRef *)
                                calloc((size_t)n + 1, sizeof(*blks_in));
                            for (int i = 0; i < n; i++) {
                                vals_in[i] = lits[i];
                                blks_in[i] = hitbbs[i];
                            }
                            vals_in[n] = ebuf;
                            blks_in[n] = cur;
                            LLVMAddIncoming(phi, vals_in, blks_in, n + 1);
                            free(vals_in);
                            free(blks_in);
                            free(hitbbs);
                            free(lits);
                            free(mems);
                            free(vals);
                            return phi;
                        }
                    }
                    if (rt_ty->kind == TYPE_CHAR &&
                        LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind) {
                        /* A523：char.ToString() 曾落通用 itoa 返回码点数字
                         * "65"（C# "A"）；WriteLine(char) 与拼接路径早就是
                         * 字符渲染——两条路对齐到同一个 UTF-8 编码帮手。 */
                        return emit_char_to_cstr(g, v);
                    }
                    LLVMValueRef buf = emit_string_alloc_rc(g,
                        LLVMConstInt(i64, 40, 0));
                    if (vk == LLVMDoubleTypeKind || vk == LLVMFloatTypeKind) {
                        emit_dbl_str(g, buf, LLVMConstInt(i64, 40, 0), v);
                        return buf;
                    }
                    emit_itoa_into(g, buf, emit_widen_i64_for_print(g, v),
                                   rt_ty->kind == TYPE_ULONG ? 1 : 0);
                    return buf;
                }
            }
        }

        /* A525 一件：标量 ToString("F2"/"D5"/"X4"/"E2"/"G3") 格式串——复用
         * 插值路径的 interp_format_to_printf（同一 spec→printf 映射，同一
         * 口径，含 D/X/F/E/G 与 0/# 形状）。数值接收者之外（bool/char/
         * 枚举的自定义格式）或不认识的说明符编译期显性拒绝，不静默。 */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 1 &&
            expr->call.args.items[0]->kind == AST_STRING_LITERAL) {
            zan_ast_node_t *scf = expr->call.callee;
            zan_istr_t smf = scf->member.name;
            if (smf.len == 8 && memcmp(smf.str, "ToString", 8) == 0) {
                zan_type_t *rf_ty = infer_expr_type(g, scf->member.object, locals);
                int numeric_kind = rf_ty &&
                    (rf_ty->kind == TYPE_BYTE ||
                     rf_ty->kind == TYPE_SHORT || rf_ty->kind == TYPE_INT ||
                     rf_ty->kind == TYPE_LONG || rf_ty->kind == TYPE_SBYTE ||
                     rf_ty->kind == TYPE_USHORT || rf_ty->kind == TYPE_UINT ||
                     rf_ty->kind == TYPE_ULONG || rf_ty->kind == TYPE_FLOAT ||
                     rf_ty->kind == TYPE_DOUBLE);
                if (numeric_kind) {
                    zan_istr_t spec = expr->call.args.items[0]->str_val;
                    bool want_float = false;
                    bool is_f = rf_ty->kind == TYPE_FLOAT ||
                                rf_ty->kind == TYPE_DOUBLE;
                    bool is_ul = rf_ty->kind == TYPE_ULONG;
                    char pfmt[64];
                    if (interp_format_to_printf(&spec, is_f, is_ul,
                                                &want_float, pfmt,
                                                sizeof(pfmt)) == 0) {
                        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3061,
                            "unsupported format specifier '%.*s' for numeric "
                            "ToString() (supported: D, X, F, E, G, 0/# shapes)",
                            (int)spec.len, spec.str);
                        return LLVMConstNull(LLVMPointerType(
                            LLVMInt8TypeInContext(g->ctx), 0));
                    }
                    LLVMValueRef vf = emit_expr(g, scf->member.object, locals);
                    LLVMValueRef arg = vf;
                    if (want_float) {
                        if (LLVMGetTypeKind(LLVMTypeOf(vf)) ==
                            LLVMFloatTypeKind)
                            arg = LLVMBuildFPExt(g->builder, vf,
                                                 LLVMDoubleTypeInContext(g->ctx),
                                                 "tos.f64");
                    } else {
                        arg = emit_widen_i64_for_print(g, vf);
                    }
                    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
                    LLVMTypeRef i8pt = LLVMPointerType(i8t, 0);
                    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef pfmt_lit = zan_irgen_intern_string(g, pfmt);
                    LLVMValueRef buf = emit_string_alloc_rc(g,
                        LLVMConstInt(i64t, 640, 0));
                    LLVMValueRef snp_args[] = { buf,
                        LLVMConstInt(i64t, 640, 0), pfmt_lit, arg };
                    LLVMTypeRef snp_ty = LLVMFunctionType(i8pt,
                        (LLVMTypeRef[]){ i8pt, i64t, i8pt }, 3, 1);
                    zan_call2(g->builder, snp_ty, g->fn_snprintf, snp_args,
                              4, "");
                    return buf;
                }
            }
        }

        /* Environment */
        if (!zan_type_defines(g, "Environment", "ArgCount") &&
            is_call_to(expr, "Environment", "ArgCount") && expr->call.args.count == 0) {
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef g_argc = LLVMGetNamedGlobal(g->mod, "__zan_argc");
            if (!g_argc) {
                g_argc = LLVMAddGlobal(g->mod, i32, "__zan_argc");
                LLVMSetInitializer(g_argc, LLVMConstInt(i32, 0, 0));
            }
            LLVMValueRef ac = LLVMBuildLoad2(g->builder, i32, g_argc, "argc");
            LLVMValueRef ac64 = LLVMBuildSExt(g->builder, ac, i64, "argc64");
            return zan_sub(g->builder, ac64, LLVMConstInt(i64, 1, 0), "nargs");
        }

        /* Environment */
        if (!zan_type_defines(g, "Environment", "ArgAt") &&
            is_call_to(expr, "Environment", "ArgAt") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i8ptrptr = LLVMPointerType(i8ptr, 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            zan_ast_node_t *idx_ast = expr->call.args.items[0];
            LLVMValueRef idx = emit_expr(g, idx_ast, locals);
            LLVMValueRef g_argc = LLVMGetNamedGlobal(g->mod, "__zan_argc");
            if (!g_argc) {
                g_argc = LLVMAddGlobal(g->mod, i32, "__zan_argc");
                LLVMSetInitializer(g_argc, LLVMConstInt(i32, 0, 0));
            }
            LLVMValueRef argc = LLVMBuildLoad2(g->builder, i32, g_argc, "argc");
            LLVMValueRef argc64 = LLVMBuildSExt(g->builder, argc, i64, "argc64");
            LLVMValueRef idx1 = zan_add(g->builder, idx, LLVMConstInt(i64, 1, 0), "argi");
            LLVMValueRef in_range = zan_and(g->builder,
                zan_icmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0), "arg.nneg"),
                zan_icmp(g->builder, LLVMIntULT, idx1, argc64, "arg.range"),
                "arg.ok");
            emit_release_owned_call_temp(g, idx_ast, idx, locals);

            LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
            LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "argat.ok");
            LLVMBasicBlockRef out_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "argat.out");
            LLVMBuildCondBr(g->builder, in_range, ok_bb, out_bb);

            LLVMPositionBuilderAtEnd(g->builder, ok_bb);
            LLVMValueRef g_argv = LLVMGetNamedGlobal(g->mod, "__zan_argv");
            if (!g_argv) {
                g_argv = LLVMAddGlobal(g->mod, i8ptrptr, "__zan_argv");
                LLVMSetInitializer(g_argv, LLVMConstNull(i8ptrptr));
            }
            LLVMValueRef argv = LLVMBuildLoad2(g->builder, i8ptrptr, g_argv, "argv");
            LLVMValueRef slot = LLVMBuildGEP2(g->builder, i8ptr, argv, &idx1, 1, "argslot");
            LLVMValueRef arg = LLVMBuildLoad2(g->builder, i8ptr, slot, "arg");
            LLVMBuildBr(g->builder, out_bb);

            LLVMPositionBuilderAtEnd(g->builder, out_bb);
            LLVMValueRef empty = emit_string_literal_rc(g, (zan_istr_t){ "", 0 });
            LLVMValueRef res = LLVMBuildPhi(g->builder, i8ptr, "argres");
            LLVMValueRef vals[] = { arg, empty };
            LLVMBasicBlockRef bbs[] = { ok_bb, cur_bb };
            LLVMAddIncoming(res, vals, bbs, 2);
            return res;
        }

        /* File.ReadAllText(path) -> string */
        if (!zan_type_defines(g, "File", "ReadAllText") &&
            is_call_to(expr, "File", "ReadAllText") && expr->call.args.count == 1 &&
            !src_method_takes_over(g, expr, locals)) {
            zan_ast_node_t *path_ast = expr->call.args.items[0];
            LLVMValueRef path_arg = emit_expr(g, path_ast, locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef fopen_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", fopen_type);
            }
            LLVMValueRef fseek_fn = LLVMGetNamedFunction(g->mod, "fseek");
            if (!fseek_fn) {
                LLVMTypeRef fseek_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0);
                fseek_fn = LLVMAddFunction(g->mod, "fseek", fseek_type);
            }
            LLVMValueRef ftell_fn = LLVMGetNamedFunction(g->mod, "ftell");
            if (!ftell_fn) {
                LLVMTypeRef ftell_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                ftell_fn = LLVMAddFunction(g->mod, "ftell", ftell_type);
            }
            LLVMValueRef fread_fn = LLVMGetNamedFunction(g->mod, "fread");
            if (!fread_fn) {
                LLVMTypeRef fread_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0);
                fread_fn = LLVMAddFunction(g->mod, "fread", fread_type);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef fclose_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", fclose_type);
            }
            LLVMValueRef mode = zan_irgen_intern_string(g, "rb");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            emit_fopen_check(g, fp, "cannot read file\n");
            LLVMValueRef seek_end_args[] = { fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 2, 0) };
            LLVMValueRef se_end = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_end_args, 3, "se_end");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntNE, se_end, LLVMConstInt(i32, 0, 0), "se_end.err"),
                "cannot read file\n");
            LLVMValueRef size = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), ftell_fn, &fp, 1, "sz");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntSLT, size, LLVMConstInt(i64, 0, 0), "sz.err"),
                "cannot read file\n");
            LLVMValueRef seek_start_args[] = { fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 0, 0) };
            LLVMValueRef se_start = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_start_args, 3, "se_start");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntNE, se_start, LLVMConstInt(i32, 0, 0), "se_start.err"),
                "cannot read file\n");
            LLVMValueRef buf_size = zan_add(g->builder, size, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
            LLVMValueRef fread_args[] = { buf, LLVMConstInt(i64, 1, 0), size, fp };
            LLVMValueRef got = zan_call2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fread_fn, fread_args, 4, "got");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntNE, got, size, "got.err"),
                "cannot read file\n");
            LLVMValueRef end_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), buf, &size, 1, "end");
            LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0), end_ptr);
            LLVMValueRef rd_close = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "rd_close");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntNE, rd_close, LLVMConstInt(i32, 0, 0), "rd_close.err"),
                "cannot read file\n");
            emit_release_owned_call_temp(g, path_ast, path_arg, locals);
            return buf;
        }

        /* File.WriteAllText(path, content) */
        if (!zan_type_defines(g, "File", "WriteAllText") &&
            is_call_to(expr, "File", "WriteAllText") && expr->call.args.count == 2 &&
            !src_method_takes_over(g, expr, locals)) {
            zan_ast_node_t *path_ast = expr->call.args.items[0];
            zan_ast_node_t *content_ast = expr->call.args.items[1];
            LLVMValueRef path_arg = emit_expr(g, path_ast, locals);
            LLVMValueRef content_arg = emit_expr(g, content_ast, locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef fopen_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", fopen_type);
            }
            LLVMValueRef fputs_fn = LLVMGetNamedFunction(g->mod, "fputs");
            if (!fputs_fn) {
                LLVMTypeRef fputs_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fputs_fn = LLVMAddFunction(g->mod, "fputs", fputs_type);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef fclose_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", fclose_type);
            }
            LLVMValueRef mode = zan_irgen_intern_string(g, "w");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            emit_fopen_check(g, fp, "cannot write file\n");
            LLVMValueRef fputs_args[] = { content_arg, fp };
            LLVMValueRef put = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fputs_fn, fputs_args, 2, "put");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntSLT, put, LLVMConstInt(i32, 0, 0), "put.err"),
                "cannot write file\n");
            LLVMValueRef wr_close = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "wr_close");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntNE, wr_close, LLVMConstInt(i32, 0, 0), "wr_close.err"),
                "cannot write file\n");
            emit_release_owned_call_temp(g, path_ast, path_arg, locals);
            emit_release_owned_call_temp(g, content_ast, content_arg, locals);
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        if (!zan_type_defines(g, "Path", "GetFileName") &&
            is_call_to(expr, "Path", "GetFileName") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            zan_ast_node_t *path_ast = expr->call.args.items[0];
            LLVMValueRef path_val = emit_expr(g, path_ast, locals);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef slash_args[] = { path_val, LLVMConstInt(i32t, '/', 0) };
            LLVMValueRef slash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, slash_args, 2, "slash");
            LLVMValueRef bslash_args[] = { path_val, LLVMConstInt(i32t, 92, 0) }; /* 92 = backslash */
            LLVMValueRef bslash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, bslash_args, 2, "bslash");
            LLVMTypeRef strlen_ty = LLVMFunctionType(LLVMInt64TypeInContext(g->ctx),
                (LLVMTypeRef[]){ i8ptr }, 1, 0);
            LLVMValueRef plen = zan_call2(g->builder, strlen_ty, g->fn_strlen,
                &path_val, 1, "plen");
            LLVMValueRef pend = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                path_val, &plen, 1, "pend");
            LLVMValueRef s_null = zan_icmp(g->builder, LLVMIntEQ, slash,
                LLVMConstNull(i8ptr), "snull");
            LLVMValueRef b_null = zan_icmp(g->builder, LLVMIntEQ, bslash,
                LLVMConstNull(i8ptr), "bnull");
            LLVMValueRef both_null = zan_and(g->builder, s_null, b_null, "bothnull");
            LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
            LLVMBasicBlockRef both_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.both");
            LLVMBasicBlockRef notboth_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.notboth");
            LLVMBasicBlockRef use_bs_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.bs");
            LLVMBasicBlockRef check_b_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.chkb");
            LLVMBasicBlockRef use_sl_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.sl");
            LLVMBasicBlockRef max_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.max");
            LLVMBasicBlockRef emit_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.emit");
            LLVMBuildCondBr(g->builder, both_null, both_bb, notboth_bb);

            LLVMPositionBuilderAtEnd(g->builder, both_bb);
            LLVMBuildBr(g->builder, emit_bb);

            LLVMPositionBuilderAtEnd(g->builder, notboth_bb);
            LLVMBuildCondBr(g->builder, s_null, use_bs_bb, check_b_bb);

            LLVMPositionBuilderAtEnd(g->builder, use_bs_bb);
            LLVMValueRef bs1 = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                bslash, (LLVMValueRef[]){ LLVMConstInt(i32t, 1, 0) }, 1, "bs1");
            LLVMBuildBr(g->builder, emit_bb);

            LLVMPositionBuilderAtEnd(g->builder, check_b_bb);
            LLVMBuildCondBr(g->builder, b_null, use_sl_bb, max_bb);

            LLVMPositionBuilderAtEnd(g->builder, use_sl_bb);
            LLVMValueRef sl1 = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                slash, (LLVMValueRef[]){ LLVMConstInt(i32t, 1, 0) }, 1, "sl1");
            LLVMBuildBr(g->builder, emit_bb);

            LLVMPositionBuilderAtEnd(g->builder, max_bb);
            LLVMValueRef s_gt = zan_icmp(g->builder, LLVMIntUGT, slash, bslash, "sgt");
            LLVMValueRef best = LLVMBuildSelect(g->builder, s_gt, slash, bslash, "best");
            LLVMValueRef b2 = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                best, (LLVMValueRef[]){ LLVMConstInt(i32t, 1, 0) }, 1, "b2");
            LLVMBuildBr(g->builder, emit_bb);

            LLVMPositionBuilderAtEnd(g->builder, emit_bb);
            LLVMValueRef start = LLVMBuildPhi(g->builder, i8ptr, "fn.start");
            LLVMAddIncoming(start,
                (LLVMValueRef[]){ path_val, bs1, sl1, b2 },
                (LLVMBasicBlockRef[]){ both_bb, use_bs_bb, use_sl_bb, max_bb }, 4);
            LLVMValueRef fname = emit_string_copy_range(g, start, pend);
            emit_release_owned_call_temp(g, path_ast, path_val, locals);
            return fname;
        }

        if (!zan_type_defines(g, "Path", "GetExtension") &&
            is_call_to(expr, "Path", "GetExtension") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            zan_ast_node_t *path_ast = expr->call.args.items[0];
            LLVMValueRef path_val = emit_expr(g, path_ast, locals);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef dot_args[] = { path_val, LLVMConstInt(i32t, '.', 0) };
            LLVMValueRef dot = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, dot_args, 2, "dot");
            LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, dot,
                LLVMConstNull(i8ptr), "dnull");
            LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
            LLVMBasicBlockRef empty_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "ext.empty");
            LLVMBasicBlockRef copy_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "ext.copy");
            LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "ext.done");
            LLVMBuildCondBr(g->builder, is_null, empty_bb, copy_bb);

            LLVMPositionBuilderAtEnd(g->builder, empty_bb);
            LLVMValueRef empty = emit_string_literal_rc(g, (zan_istr_t){ "", 0 });
            LLVMBuildBr(g->builder, done_bb);

            LLVMPositionBuilderAtEnd(g->builder, copy_bb);
            LLVMTypeRef strlen_ty = LLVMFunctionType(LLVMInt64TypeInContext(g->ctx),
                (LLVMTypeRef[]){ i8ptr }, 1, 0);
            LLVMValueRef plen = zan_call2(g->builder, strlen_ty, g->fn_strlen,
                &path_val, 1, "plen");
            LLVMValueRef pend = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                path_val, &plen, 1, "pend");
            LLVMValueRef ext = emit_string_copy_range(g, dot, pend);
            LLVMBuildBr(g->builder, done_bb);

            LLVMPositionBuilderAtEnd(g->builder, done_bb);
            LLVMValueRef res = LLVMBuildPhi(g->builder, i8ptr, "ext.res");
            LLVMAddIncoming(res, (LLVMValueRef[]){ empty, ext },
                (LLVMBasicBlockRef[]){ empty_bb, copy_bb }, 2);
            emit_release_owned_call_temp(g, path_ast, path_val, locals);
            return res;
        }

        if (!zan_type_defines(g, "Path", "Combine") &&
            is_call_to(expr, "Path", "Combine") && expr->call.args.count == 2) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef a = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef b = emit_expr(g, expr->call.args.items[1], locals);
            LLVMTypeRef strlen_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
            LLVMValueRef len_a = zan_call2(g->builder, strlen_type, g->fn_strlen, &a, 1, "la");
            LLVMValueRef len_b = zan_call2(g->builder, strlen_type, g->fn_strlen, &b, 1, "lb");
            LLVMValueRef total = zan_add(g->builder, len_a, len_b, "t");
            total = zan_add(g->builder, total, LLVMConstInt(i64, 2, 0), "t2"); /* +separator+null */
            LLVMValueRef buf = emit_string_alloc_rc(g, total);
            LLVMValueRef strcpy_args[] = { buf, a };
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcpy, strcpy_args, 2, "");
            LLVMValueRef sep = (g->target_is_windows ? zan_irgen_intern_string(g, "\\")
                                  : zan_irgen_intern_string(g, "/"));
            LLVMValueRef cat1_args[] = { buf, sep };
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcat, cat1_args, 2, "");
            LLVMValueRef cat2_args[] = { buf, b };
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcat, cat2_args, 2, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], a, locals);
            emit_release_owned_call_temp(g, expr->call.args.items[1], b, locals);
            return buf;
        }

        /* File.AppendAllText(path, content) */
        if (!zan_type_defines(g, "File", "AppendAllText") &&
            is_call_to(expr, "File", "AppendAllText") && expr->call.args.count == 2 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef content_arg = emit_expr(g, expr->call.args.items[1], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef fopen_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", fopen_type);
            }
            LLVMValueRef fputs_fn = LLVMGetNamedFunction(g->mod, "fputs");
            if (!fputs_fn) {
                LLVMTypeRef fputs_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fputs_fn = LLVMAddFunction(g->mod, "fputs", fputs_type);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef fclose_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", fclose_type);
            }
            LLVMValueRef mode = zan_irgen_intern_string(g, "a");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            emit_fopen_check(g, fp, "cannot write file\n");
            LLVMValueRef fputs_args[] = { content_arg, fp };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fputs_fn, fputs_args, 2, "");
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            emit_release_owned_call_temp(g, expr->call.args.items[1], content_arg, locals);
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* File.Exists(path) -> bool */
        if (!zan_type_defines(g, "File", "Exists") &&
            is_call_to(expr, "File", "Exists") && expr->call.args.count == 1 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef fopen_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", fopen_type);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef fclose_type = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", fclose_type);
            }
            LLVMValueRef mode = zan_irgen_intern_string(g, "rb");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntNE, fp,
                LLVMConstNull(i8ptr), "exists");
            LLVMBasicBlockRef close_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "fexist.close");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "fexist.end");
            LLVMBuildCondBr(g->builder, is_null, close_bb, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, close_bb);
            zan_call2(g->builder, LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "");
            LLVMBuildBr(g->builder, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            LLVMValueRef result = LLVMBuildZExt(g->builder, is_null, LLVMInt64TypeInContext(g->ctx), "fex");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return result;
        }

        /* File.Delete(path) */
        if (!zan_type_defines(g, "File", "Delete") &&
            is_call_to(expr, "File", "Delete") && expr->call.args.count == 1 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef remove_fn = LLVMGetNamedFunction(g->mod, "remove");
            if (!remove_fn) {
                LLVMTypeRef remove_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                remove_fn = LLVMAddFunction(g->mod, "remove", remove_type);
            }
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                remove_fn, &path_arg, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* File.Move(source, dest) — rename */
        if (!zan_type_defines(g, "File", "Move") &&
            is_call_to(expr, "File", "Move") && expr->call.args.count == 2 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef src = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef dst = emit_expr(g, expr->call.args.items[1], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef rename_fn = LLVMGetNamedFunction(g->mod, "rename");
            if (!rename_fn) {
                LLVMTypeRef rename_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                rename_fn = LLVMAddFunction(g->mod, "rename", rename_type);
            }
            LLVMValueRef args[] = { src, dst };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                rename_fn, args, 2, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], src, locals);
            emit_release_owned_call_temp(g, expr->call.args.items[1], dst, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        if (!zan_type_defines(g, "File", "Copy") &&
            is_call_to(expr, "File", "Copy") && expr->call.args.count == 2 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef src = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef dst = emit_expr(g, expr->call.args.items[1], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", ft);
            }
            LLVMValueRef fread_fn = LLVMGetNamedFunction(g->mod, "fread");
            if (!fread_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0);
                fread_fn = LLVMAddFunction(g->mod, "fread", ft);
            }
            LLVMValueRef fwrite_fn = LLVMGetNamedFunction(g->mod, "fwrite");
            if (!fwrite_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0);
                fwrite_fn = LLVMAddFunction(g->mod, "fwrite", ft);
            }
            LLVMValueRef fseek_fn = LLVMGetNamedFunction(g->mod, "fseek");
            if (!fseek_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0);
                fseek_fn = LLVMAddFunction(g->mod, "fseek", ft);
            }
            LLVMValueRef ftell_fn = LLVMGetNamedFunction(g->mod, "ftell");
            if (!ftell_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                ftell_fn = LLVMAddFunction(g->mod, "ftell", ft);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", ft);
            }
            LLVMValueRef rb = zan_irgen_intern_string(g, "rb");
            LLVMValueRef sargs[] = { src, rb };
            LLVMValueRef sfp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, sargs, 2, "sfp");
            emit_fopen_check(g, sfp, "cannot read file\n");
            LLVMValueRef zero64 = LLVMConstInt(i64, 0, 0);
            LLVMValueRef seek_end[] = { sfp, zero64, LLVMConstInt(i32, 2, 0) };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_end, 3, "");
            LLVMValueRef sz = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), ftell_fn, &sfp, 1, "sz");
            sz = LLVMBuildSelect(g->builder,
                zan_icmp(g->builder, LLVMIntSLT, sz, zero64, "sz.neg"),
                zero64, sz, "sz.clamp");
            LLVMValueRef seek_start[] = { sfp, zero64, LLVMConstInt(i32, 0, 0) };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_start, 3, "");
            LLVMValueRef buf = zan_call2(g->builder,
                LLVMGlobalGetValueType(g->fn_malloc), g->fn_malloc,
                &sz, 1, "cp.buf");
            {
                LLVMTypeRef i8ptr_t = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntEQ, buf,
                    LLVMConstPointerNull(i8ptr_t), "cp.buf.null"),
                    "out of memory\n");
            }
            LLVMValueRef fread_args[] = { buf, LLVMConstInt(i64, 1, 0), sz, sfp };
            zan_call2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fread_fn, fread_args, 4, "");
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &sfp, 1, "");
            LLVMValueRef wb = zan_irgen_intern_string(g, "wb");
            LLVMValueRef dargs[] = { dst, wb };
            LLVMValueRef dfp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, dargs, 2, "dfp");
            emit_fopen_check(g, dfp, "cannot write file\n");
            LLVMValueRef fwrite_args[] = { buf, LLVMConstInt(i64, 1, 0), sz, dfp };
            zan_call2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fwrite_fn, fwrite_args, 4, "");
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &dfp, 1, "");
            zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0),
                g->fn_free, &buf, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], src, locals);
            emit_release_owned_call_temp(g, expr->call.args.items[1], dst, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* File.GetSize(path) -> int */
        if (!zan_type_defines(g, "File", "GetSize") &&
            is_call_to(expr, "File", "GetSize") && expr->call.args.count == 1 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", ft);
            }
            LLVMValueRef fseek_fn = LLVMGetNamedFunction(g->mod, "fseek");
            if (!fseek_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0);
                fseek_fn = LLVMAddFunction(g->mod, "fseek", ft);
            }
            LLVMValueRef ftell_fn = LLVMGetNamedFunction(g->mod, "ftell");
            if (!ftell_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                ftell_fn = LLVMAddFunction(g->mod, "ftell", ft);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", ft);
            }
            LLVMValueRef mode = zan_irgen_intern_string(g, "rb");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            LLVMValueRef seek_end[] = { fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 2, 0) };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_end, 3, "");
            LLVMValueRef sz = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), ftell_fn, &fp, 1, "fsz");
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return sz;
        }

        /* Directory.Exists(path) -> bool */
        if (!zan_type_defines(g, "Directory", "Exists") &&
            is_call_to(expr, "Directory", "Exists") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            /* Windows: GetFileAttributesA & FILE_ATTRIBUTE_DIRECTORY */
            LLVMValueRef result;
            if (g->target_is_windows) {
                LLVMValueRef gfa_fn = LLVMGetNamedFunction(g->mod, "GetFileAttributesA");
                if (!gfa_fn) {
                    LLVMTypeRef gfa_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    gfa_fn = LLVMAddFunction(g->mod, "GetFileAttributesA", gfa_type);
                }
                LLVMValueRef attrs = zan_call2(g->builder,
                    LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    gfa_fn, &path_arg, 1, "attrs");
                LLVMValueRef not_invalid = zan_icmp(g->builder, LLVMIntNE, attrs,
                    LLVMConstInt(i32, 0xFFFFFFFF, 0), "noinv");
                LLVMValueRef is_dir = zan_and(g->builder, attrs,
                    LLVMConstInt(i32, 0x10, 0), "isdir");
                LLVMValueRef is_dir_bool = zan_icmp(g->builder, LLVMIntNE, is_dir,
                    LLVMConstInt(i32, 0, 0), "isdirb");
                result = zan_and(g->builder, not_invalid, is_dir_bool, "dexist");
            } else {
                LLVMValueRef opendir_fn = LLVMGetNamedFunction(g->mod, "opendir");
                if (!opendir_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    opendir_fn = LLVMAddFunction(g->mod, "opendir", ft);
                }
                LLVMValueRef dirp = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    opendir_fn, &path_arg, 1, "dirp");
                result = zan_icmp(g->builder, LLVMIntNE, dirp, LLVMConstNull(i8ptr), "dopen");
                LLVMValueRef closedir_fn = LLVMGetNamedFunction(g->mod, "closedir");
                if (!closedir_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    closedir_fn = LLVMAddFunction(g->mod, "closedir", ft);
                }
                LLVMBasicBlockRef cl_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "de.close");
                LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "de.end");
                LLVMBuildCondBr(g->builder, result, cl_bb, end_bb);
                LLVMPositionBuilderAtEnd(g->builder, cl_bb);
                zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    closedir_fn, &dirp, 1, "");
                LLVMBuildBr(g->builder, end_bb);
                LLVMPositionBuilderAtEnd(g->builder, end_bb);
            }
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMBuildZExt(g->builder, result, i64, "dex");
        }

        /* Environment */
        if (!zan_type_defines(g, "Environment", "ExeDir") &&
            is_call_to(expr, "Environment", "ExeDir") && expr->call.args.count == 0) {
            g->uses_sync_runtime = true;
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 1024, 0));
            LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_exe_dir_into");
            if (!fn) fn = LLVMAddFunction(g->mod, "zan_exe_dir_into", ft);
            LLVMValueRef args2[] = { buf, LLVMConstInt(i64, 1024, 0) };
            zan_call2(g->builder, ft, fn, args2, 2, "");
            return buf;
        }

        /* Directory */
        if (!zan_type_defines(g, "Directory", "ListNames") &&
            is_call_to(expr, "Directory", "ListNames") && expr->call.args.count == 1) {
            g->uses_sync_runtime = true;
            LLVMValueRef pat = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 65536, 0));
            LLVMTypeRef ft = LLVMFunctionType(i64,
                (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_dir_list_into");
            if (!fn) fn = LLVMAddFunction(g->mod, "zan_dir_list_into", ft);
            LLVMValueRef args3[] = { pat, buf, LLVMConstInt(i64, 65536, 0) };
            zan_call2(g->builder, ft, fn, args3, 3, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], pat, locals);
            return buf;
        }

        /* Directory.CreateDirectory(path) */
        if (!zan_type_defines(g, "Directory", "CreateDirectory") &&
            is_call_to(expr, "Directory", "CreateDirectory") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            if (g->target_is_windows) {
                LLVMValueRef mkdir_fn = LLVMGetNamedFunction(g->mod, "_mkdir");
                if (!mkdir_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    mkdir_fn = LLVMAddFunction(g->mod, "_mkdir", ft);
                }
                zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    mkdir_fn, &path_arg, 1, "");
            } else {
                LLVMValueRef mkdir_fn = LLVMGetNamedFunction(g->mod, "mkdir");
                if (!mkdir_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0);
                    mkdir_fn = LLVMAddFunction(g->mod, "mkdir", ft);
                }
                LLVMValueRef margs[] = { path_arg, LLVMConstInt(i32, 0777, 0) };
                zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0),
                    mkdir_fn, margs, 2, "");
            }
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* Directory.Delete(path) */
        if (!zan_type_defines(g, "Directory", "Delete") &&
            is_call_to(expr, "Directory", "Delete") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            const char *rmname = g->target_is_windows ? "_rmdir" : "rmdir";
            LLVMValueRef rmdir_fn = LLVMGetNamedFunction(g->mod, rmname);
            if (!rmdir_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                rmdir_fn = LLVMAddFunction(g->mod, rmname, ft);
            }
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                rmdir_fn, &path_arg, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* Directory.GetCurrentDirectory() -> string */
        if (!zan_type_defines(g, "Directory", "GetCurrentDirectory") &&
            is_call_to(expr, "Directory", "GetCurrentDirectory") && expr->call.args.count == 0) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 4096, 0));
            const char *cwdname = g->target_is_windows ? "_getcwd" : "getcwd";
            LLVMValueRef getcwd_fn = LLVMGetNamedFunction(g->mod, cwdname);
            if (!getcwd_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0);
                getcwd_fn = LLVMAddFunction(g->mod, cwdname, ft);
            }
            LLVMValueRef cwd_args[] = { buf, LLVMConstInt(i32, 4096, 0) };
            zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0),
                getcwd_fn, cwd_args, 2, "");
            return buf;
        }

        /* Directory.SetCurrentDirectory(path) */
        if (!zan_type_defines(g, "Directory", "SetCurrentDirectory") &&
            is_call_to(expr, "Directory", "SetCurrentDirectory") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            const char *chname = g->target_is_windows ? "_chdir" : "chdir";
            LLVMValueRef chdir_fn = LLVMGetNamedFunction(g->mod, chname);
            if (!chdir_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                chdir_fn = LLVMAddFunction(g->mod, chname, ft);
            }
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                chdir_fn, &path_arg, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* Path.GetDirectoryName(path) -> string */
        if (!zan_type_defines(g, "Path", "GetDirectoryName") &&
            is_call_to(expr, "Path", "GetDirectoryName") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
            LLVMValueRef path_val = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef len = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), g->fn_strlen, &path_val, 1, "plen");
            LLVMValueRef bsz = zan_add(g->builder, len, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = emit_string_alloc_rc(g, bsz);
            LLVMValueRef cpy_args[] = { buf, path_val };
            zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcpy, cpy_args, 2, "");
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef slash_args[] = { buf, LLVMConstInt(i32t, '/', 0) };
            LLVMValueRef slash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, slash_args, 2, "sl");
            LLVMValueRef bslash_args[] = { buf, LLVMConstInt(i32t, 92, 0) };
            LLVMValueRef bslash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, bslash_args, 2, "bsl");
            LLVMValueRef sl_null = zan_icmp(g->builder, LLVMIntEQ, slash, LLVMConstNull(i8ptr), "snul");
            LLVMValueRef pick = LLVMBuildSelect(g->builder, sl_null, bslash, slash, "pk1");
            LLVMValueRef bs_null = zan_icmp(g->builder, LLVMIntEQ, bslash, LLVMConstNull(i8ptr), "bnul");
            LLVMValueRef sep_ptr = LLVMBuildSelect(g->builder, bs_null, pick,
                LLVMBuildSelect(g->builder,
                    zan_icmp(g->builder, LLVMIntUGT, bslash, pick, "bgt"),
                    bslash, pick, "pk2"), "sep");
            LLVMValueRef found = zan_icmp(g->builder, LLVMIntNE, sep_ptr, LLVMConstNull(i8ptr), "fnd");
            LLVMBasicBlockRef trunc_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dn.trunc");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dn.end");
            LLVMBuildCondBr(g->builder, found, trunc_bb, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, trunc_bb);
            LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), sep_ptr);
            LLVMBuildBr(g->builder, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_val, locals);
            return buf;
        }

        /* Path.HasExtension(path) -> bool */
        if (!zan_type_defines(g, "Path", "HasExtension") &&
            is_call_to(expr, "Path", "HasExtension") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef path_val = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef dot_args[] = { path_val, LLVMConstInt(i32t, '.', 0) };
            LLVMValueRef dot = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, dot_args, 2, "dot");
            LLVMValueRef has = zan_icmp(g->builder, LLVMIntNE, dot, LLVMConstNull(i8ptr), "hasext");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_val, locals);
            return LLVMBuildZExt(g->builder, has, i64, "he");
        }

        /* Path.GetTempPath() -> string */
        if (!zan_type_defines(g, "Path", "GetTempPath") &&
            is_call_to(expr, "Path", "GetTempPath") && expr->call.args.count == 0) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 260, 0));
            if (g->target_is_windows) {
                LLVMValueRef gtp_fn = LLVMGetNamedFunction(g->mod, "GetTempPathA");
                if (!gtp_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i32, i8ptr }, 2, 0);
                    gtp_fn = LLVMAddFunction(g->mod, "GetTempPathA", ft);
                }
                LLVMValueRef gtp_args[] = { LLVMConstInt(i32, 260, 0), buf };
                zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i32, i8ptr }, 2, 0),
                    gtp_fn, gtp_args, 2, "");
            } else {
                LLVMValueRef getenv_fn = LLVMGetNamedFunction(g->mod, "getenv");
                if (!getenv_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    getenv_fn = LLVMAddFunction(g->mod, "getenv", ft);
                }
                LLVMValueRef key = zan_irgen_intern_string(g, "TMPDIR");
                LLVMValueRef env = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    getenv_fn, &key, 1, "tmpenv");
                LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, env, LLVMConstNull(i8ptr), "tnull");
                LLVMValueRef deflt = zan_irgen_intern_string(g, "/tmp/");
                LLVMValueRef src = LLVMBuildSelect(g->builder, isnull, deflt, env, "tmpsrc");
                LLVMValueRef cpy_args[] = { buf, src };
                zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                    g->fn_strcpy, cpy_args, 2, "");
            }
            return buf;
        }

        /* Path.GetFileNameWithoutExtension(path) -> string */
        if (!zan_type_defines(g, "Path", "GetFileNameWithoutExtension") &&
            is_call_to(expr, "Path", "GetFileNameWithoutExtension") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef path_val = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef slash_args[] = { path_val, LLVMConstInt(i32t, '/', 0) };
            LLVMValueRef slash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, slash_args, 2, "sl");
            LLVMValueRef bslash_args[] = { path_val, LLVMConstInt(i32t, 92, 0) };
            LLVMValueRef bslash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, bslash_args, 2, "bsl");
            LLVMValueRef sl_null = zan_icmp(g->builder, LLVMIntEQ, slash, LLVMConstNull(i8ptr), "snul");
            LLVMValueRef bs_null = zan_icmp(g->builder, LLVMIntEQ, bslash, LLVMConstNull(i8ptr), "bnul");
            LLVMValueRef best = LLVMBuildSelect(g->builder, sl_null, bslash,
                LLVMBuildSelect(g->builder, bs_null, slash,
                    LLVMBuildSelect(g->builder, zan_icmp(g->builder, LLVMIntUGT, bslash, slash, "bgt"),
                        bslash, slash, "mx"), "pk"), "sep");
            LLVMValueRef has_sep = zan_icmp(g->builder, LLVMIntNE, best, LLVMConstNull(i8ptr), "hs");
            LLVMValueRef after = LLVMBuildGEP2(g->builder, i8, best, &(LLVMValueRef){LLVMConstInt(i64, 1, 0)}, 1, "aft");
            LLVMValueRef fname = LLVMBuildSelect(g->builder, has_sep, after, path_val, "fn");
            LLVMValueRef flen = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), g->fn_strlen, &fname, 1, "flen");
            LLVMValueRef bsz = zan_add(g->builder, flen, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = emit_string_alloc_rc(g, bsz);
            LLVMValueRef cpy_args[] = { buf, fname };
            zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcpy, cpy_args, 2, "");
            LLVMValueRef dot_args[] = { buf, LLVMConstInt(i32t, '.', 0) };
            LLVMValueRef dot = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, dot_args, 2, "dot");
            LLVMValueRef has_dot = zan_icmp(g->builder, LLVMIntNE, dot, LLVMConstNull(i8ptr), "hd");
            LLVMBasicBlockRef trunc_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "fnwe.trunc");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "fnwe.end");
            LLVMBuildCondBr(g->builder, has_dot, trunc_bb, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, trunc_bb);
            LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), dot);
            LLVMBuildBr(g->builder, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            return buf;
        }
    if (matched) *matched = false;
    return NULL;
}
