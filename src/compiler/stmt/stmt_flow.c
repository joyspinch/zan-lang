    case AST_RETURN_STMT:
        if (stmt->ret.value) {
            zan_type_t *rty = infer_expr_type(g, stmt->ret.value, locals);
            check_implicit_narrowing(g, g->current_fn_zan_ret_type,
                rty, stmt->ret.value, "return");
            /* lambda 体里的 return：委托返回类型与返回值类型必须兼容。
             * 普通方法由 checker 负责，这里只补 lambda（checker 看不到
             * lambda 的委托返回类型）。 */
            if (g->lambda_depth > 0)
                check_no_boxing_conversion(g, g->current_fn_zan_ret_type, rty,
                                           stmt->ret.value, "lambda return");
        }
        if (g->current_async_frame) {
            LLVMValueRef ri = NULL, rv = NULL;
            zan_type_t *ret_type = g->current_async_ret_type;
            if (stmt->ret.value) {
                rv = emit_expr(g, stmt->ret.value, locals);
                zan_type_t *value_type = concretize(g,
                    infer_expr_type(g, stmt->ret.value, locals));
                if (!ret_type) ret_type = value_type;
                /* A556-1 隐式装箱：object/接口异步返回类型收值形状——箱是
                 * fresh owned，跳过来源 retain（ARC 移交语义）。 */
                LLVMValueRef boxed = g->current_async_ret_type
                    ? emit_implicit_box(g, g->current_async_ret_type, rv,
                                        stmt->ret.value, locals) : rv;
                if (boxed != rv) {
                    rv = boxed;
                    ret_type = g->current_async_ret_type;
                } else if (type_contains_collection_rc(g, value_type, 0) &&
                    !expr_yields_owned_rc_value(g, stmt->ret.value, locals)) {
                    emit_collection_value_retain(g, value_type, rv, 0);
                } else if (value_type &&
                    (is_rc_managed_type(value_type) ||
                     value_type->kind == TYPE_OBJECT) &&
                    !expr_yields_owned_rc_value(g, stmt->ret.value, locals)) {
                    if (is_rc_managed_type(value_type)) {
                        emit_rc_retain_for_type(g, value_type, rv);
                    } else if (LLVMGetTypeKind(LLVMTypeOf(rv)) == LLVMPointerTypeKind) {
                        emit_arc_retain(g, rv);
                    }
                }
                rv = coerce_async_ret(g, rv);
            }
            if (g->finally_count > 0) {
                if (rv) emit_async_pending_return(g, rv);
                emit_pending_finallys(g, locals, 0, rv != NULL);
                if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                    break;
                if (rv) ri = emit_async_pending_take_return(g);
            } else if (rv) {
                ri = coerce_to_frame_result(g, rv, ret_type);
            }
            emit_release_active_catch_excs(g, 0);
            emit_async_complete(g, locals, ri);
            break;
        }
        if (stmt->ret.value) {
            LLVMValueRef val = emit_expr(g, stmt->ret.value, locals);
            bool conv_ret = false;
            if (g->current_fn_zan_ret_type) {
                zan_type_t *vty = infer_expr_type(g, stmt->ret.value, locals);
                if (find_user_conversion(g, vty, g->current_fn_zan_ret_type,
                                         "op_implicit")) {
                    val = emit_user_conversion(g, vty, g->current_fn_zan_ret_type,
                                               "op_implicit", val,
                                               stmt->ret.value, locals);
                    conv_ret = true;
                }
            }
            /* A556-1 隐式装箱：结构体值流向 object/接口返回类型——箱是
             * fresh owned 堆对象，按 conv_ret 交 ARC 移交契约（不再对
             * 返回值 retain）。 */
            if (g->current_fn_zan_ret_type) {
                LLVMValueRef boxed = emit_implicit_box(g,
                    g->current_fn_zan_ret_type, val, stmt->ret.value, locals);
                if (boxed != val) {
                    val = boxed;
                    conv_ret = true;
                }
            }
            /* ARC 返回契约：向调用方移交 +1 强引用所有权，随后释放本作用域局部变量 */
            zan_type_t *ret_type = concretize(g,
                infer_expr_type(g, stmt->ret.value, locals));
            if (conv_ret) ret_type = g->current_fn_zan_ret_type;
            if (!ret_type && g->current_fn_zan_ret_type)
                ret_type = g->current_fn_zan_ret_type;
            if (is_rc_managed_type(ret_type) &&
                !expr_yields_owned_rc_value(g, stmt->ret.value, locals) &&
                !conv_ret) {
                emit_rc_retain_for_type(g, ret_type, val);
            } else if (ret_type &&
                       ret_type->kind == TYPE_OBJECT &&
                       LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind &&
                       !expr_yields_owned_rc_value(g, stmt->ret.value, locals) &&
                       !conv_ret) {
                emit_arc_retain(g, val);
            } else if (ret_type && ret_type->kind == TYPE_STRUCT &&
                       type_contains_collection_rc(g, ret_type, 0) &&
                       !expr_yields_owned_rc_value(g, stmt->ret.value, locals) &&
                       !conv_ret) {
                emit_collection_value_retain(g, ret_type, val, 0);
            }
            if (g->finally_count > 0) {
                LLVMValueRef v_slot =
                    emit_entry_alloca(g, LLVMTypeOf(val), "ret.fin.slot");
                zan_store_fit(g, val, v_slot);
                emit_pending_finallys(g, locals, 0, false);
                if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                    break;
                val = LLVMBuildLoad2(g->builder, LLVMTypeOf(val), v_slot,
                                     "ret.fin");
            }
            /* 退出顺序：先释放 catch 捕获的异常对象，再释放局部变量。
             * 被捕获的异常还压在异常临时栈顶（catch 进入时压入），若先弹局部
             * 变量的 eh_slot，弹出的是异常而不是该局部变量的槽位标记——标记滞留
             * 栈上，栈深与标记错位；后续展开会拿滞留标记再释放一次同一对象，
             * 变成对已释放内存的二次释放（访问冲突）。 */
            emit_release_active_catch_excs(g, 0);
            emit_release_owned_locals(g, locals);
            LLVMTypeRef fn_ret = g->current_fn_ret_type;
            LLVMTypeRef val_t = LLVMTypeOf(val);
            if (val_t != fn_ret) {
                if (llvm_is_nullable(fn_ret)) {
                    val = coerce_int_to(g, val, fn_ret);
                } else if (LLVMGetTypeKind(fn_ret) == LLVMFloatTypeKind &&
                    LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                    val = LLVMBuildFPTrunc(g->builder, val, fn_ret, "rettrunc");
                } else if (LLVMGetTypeKind(fn_ret) == LLVMDoubleTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
                    val = LLVMBuildFPExt(g->builder, val, fn_ret, "retext");
                } else if ((LLVMGetTypeKind(fn_ret) == LLVMDoubleTypeKind ||
                            LLVMGetTypeKind(fn_ret) == LLVMFloatTypeKind) &&
                           LLVMGetTypeKind(val_t) == LLVMIntegerTypeKind &&
                           LLVMGetIntTypeWidth(val_t) > 1) {
                    val = LLVMBuildSIToFP(g->builder, val, fn_ret, "retsitofp");
                } else if (LLVMGetTypeKind(fn_ret) == LLVMIntegerTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMIntegerTypeKind) {
                    unsigned fn_bits = LLVMGetIntTypeWidth(fn_ret);
                    unsigned val_bits = LLVMGetIntTypeWidth(val_t);
                    if (fn_bits > val_bits) {
                        val = val_bits <= 8
                            ? LLVMBuildZExt(g->builder, val, fn_ret, "retzext")
                            : LLVMBuildSExt(g->builder, val, fn_ret, "retext");
                    } else if (fn_bits < val_bits) {
                        val = LLVMBuildTrunc(g->builder, val, fn_ret, "rettrunc");
                    }
                } else if (fn_ret == get_decimal_type(g) &&
                           LLVMGetTypeKind(val_t) == LLVMIntegerTypeKind) {
                    /* int 字面量/变量 → decimal 返回值：隐式转换走运行时
                     * 构造，漏掉就是 LLVM 校验失败（ret i64 7 对 %Decimal
                     * 返回型，A548）。 */
                    val = coerce_to_decimal(g, val);
                } else if (LLVMGetTypeKind(fn_ret) == LLVMStructTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
                    val = LLVMBuildLoad2(g->builder, fn_ret, val, "ret.struct");
                } else if (LLVMGetTypeKind(fn_ret) == LLVMPointerTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
                    /* e */
                    val = LLVMBuildBitCast(g->builder, val, fn_ret, "retcast");
                } else if (LLVMGetTypeKind(fn_ret) == LLVMPointerTypeKind ||
                           LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
                    val = emit_boundary_coerce(g, val, fn_ret);
                }
            }
            if (g->current_fn_is_main) emit_release_static_rc_fields(g, NULL);
            emit_eh_disarm_from(g, 0);
            LLVMBuildRet(g->builder, val);
        } else {
            emit_pending_finallys(g, locals, 0, false);
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                break;
            /* 同上：catch 异常先出栈，局部变量后出栈 */
            emit_release_active_catch_excs(g, 0);
            emit_release_owned_locals(g, locals);
            LLVMTypeRef fn_ret = g->current_fn_ret_type;
            if (g->current_fn_is_main) emit_release_static_rc_fields(g, NULL);
            emit_eh_disarm_from(g, 0);
            if (fn_ret && LLVMGetTypeKind(fn_ret) != LLVMVoidTypeKind) {
                LLVMBuildRet(g->builder, LLVMConstNull(fn_ret));
            } else {
                LLVMBuildRetVoid(g->builder);
            }
        }
        break;

    case AST_IF_STMT: {
        int then_start = locals->count;
        LLVMValueRef cond = emit_expr(g, stmt->if_stmt.cond, locals);
        cond = zan_tobool(g->builder, cond, "tobool");

        LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "then");
        LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "else");
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "merge");

        LLVMBuildCondBr(g->builder, cond, then_bb, stmt->if_stmt.else_body ? else_bb : merge_bb);

        LLVMPositionBuilderAtEnd(g->builder, then_bb);
        emit_stmt(g, stmt->if_stmt.then_body, locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, then_start);
            LLVMBuildBr(g->builder, merge_bb);
        } else {
            emit_release_owned_locals_from(g, locals, then_start);
        }

        int else_start = locals->count;
        LLVMPositionBuilderAtEnd(g->builder, else_bb);
        if (stmt->if_stmt.else_body) {
            emit_stmt(g, stmt->if_stmt.else_body, locals);
        }
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, else_start);
            LLVMBuildBr(g->builder, merge_bb);
        } else {
            emit_release_owned_locals_from(g, locals, else_start);
        }

        LLVMPositionBuilderAtEnd(g->builder, merge_bb);
        break;
    }

    case AST_WHILE_STMT: {
        int body_start = locals->count;
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.body");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.end");

        zan_irgen_pending_context_t saved_pending = g->pending;
        LLVMBasicBlockRef saved_break = g->break_target;
        LLVMBasicBlockRef saved_cont = g->continue_target;
        int saved_loop_base = g->loop_locals_base;
        int saved_break_base = g->break_locals_base;
        int saved_loop_cbase = g->loop_catch_base;
        int saved_loop_fbase = g->finally_loop_base;
        g->pending.break_scope = g->pending.scope;
        g->pending.continue_scope = g->pending.scope;
        g->break_target = end_bb;
        g->continue_target = cond_bb;
        g->loop_locals_base = body_start;
        g->break_locals_base = body_start;
        g->loop_catch_base = g->catch_cleanup_count;
        g->finally_loop_base = g->finally_count;
        int saved_loop_ehbase = g->eh_armed_loop_base;
        g->eh_armed_loop_base = g->eh_armed_count;

        LLVMBuildBr(g->builder, cond_bb);
        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        LLVMValueRef cond = emit_expr(g, stmt->while_stmt.cond, locals);
        cond = zan_tobool(g->builder, cond, "tobool");
        LLVMBuildCondBr(g->builder, cond, body_bb, end_bb);

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        emit_stmt(g, stmt->while_stmt.body, locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, body_start);
            if (!emit_async_preempt_site(g, cond_bb))
                LLVMBuildBr(g->builder, cond_bb);
        } else {
            emit_release_owned_locals_from(g, locals, body_start);
        }

        g->pending.break_scope = saved_pending.break_scope;
        g->pending.continue_scope = saved_pending.continue_scope;
        g->break_target = saved_break;
        g->continue_target = saved_cont;
        g->loop_locals_base = saved_loop_base;
        g->break_locals_base = saved_break_base;
        g->loop_catch_base = saved_loop_cbase;
        g->finally_loop_base = saved_loop_fbase;
        g->eh_armed_loop_base = saved_loop_ehbase;

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        break;
    }

    case AST_FOR_STMT: {
        int for_start = locals->count;
        if (stmt->for_stmt.init) emit_stmt(g, stmt->for_stmt.init, locals);
        /* C# 的 for 头变量是【共享单变量】：闭包捕获共享同一个存储，循环里
         * 的写入对先前创建的闭包可见（TASKS.md A519）。曾给捕获打
         * per_iteration=1（每次迭代快照）——那是 foreach 的 C#5 语义，
         * 用在 for 上让经典"共享循环计数"模式迁移即错。 */
        int for_body_start = locals->count;

        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.body");
        LLVMBasicBlockRef step_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.step");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.end");

        zan_irgen_pending_context_t saved_pending = g->pending;
        LLVMBasicBlockRef saved_break = g->break_target;
        LLVMBasicBlockRef saved_cont = g->continue_target;
        int saved_loop_base = g->loop_locals_base;
        int saved_break_base = g->break_locals_base;
        int saved_loop_cbase = g->loop_catch_base;
        int saved_loop_fbase = g->finally_loop_base;
        g->pending.break_scope = g->pending.scope;
        g->pending.continue_scope = g->pending.scope;
        g->break_target = end_bb;
        g->continue_target = step_bb;
        g->loop_locals_base = for_body_start;
        g->break_locals_base = for_body_start;
        g->loop_catch_base = g->catch_cleanup_count;
        g->finally_loop_base = g->finally_count;
        int saved_loop_ehbase = g->eh_armed_loop_base;
        g->eh_armed_loop_base = g->eh_armed_count;

        LLVMBuildBr(g->builder, cond_bb);
        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        if (stmt->for_stmt.cond) {
            LLVMValueRef cond = emit_expr(g, stmt->for_stmt.cond, locals);
            cond = zan_tobool(g->builder, cond, "tobool");
            LLVMBuildCondBr(g->builder, cond, body_bb, end_bb);
        } else {
            LLVMBuildBr(g->builder, body_bb);
        }

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        emit_stmt(g, stmt->for_stmt.body, locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, for_body_start);
            LLVMBuildBr(g->builder, step_bb);
        } else {
            emit_release_owned_locals_from(g, locals, for_body_start);
        }

        LLVMPositionBuilderAtEnd(g->builder, step_bb);
        if (stmt->for_stmt.step) {
            if (stmt->for_stmt.step->kind == AST_BLOCK ||
                stmt->for_stmt.step->kind == AST_EXPR_STMT) {
                emit_stmt(g, stmt->for_stmt.step, locals);
            } else {
                emit_expr(g, stmt->for_stmt.step, locals);
            }
        }
        if (!emit_async_preempt_site(g, cond_bb))
            LLVMBuildBr(g->builder, cond_bb);

        g->pending.break_scope = saved_pending.break_scope;
        g->pending.continue_scope = saved_pending.continue_scope;
        g->break_target = saved_break;
        g->continue_target = saved_cont;
        g->loop_locals_base = saved_loop_base;
        g->break_locals_base = saved_break_base;
        g->loop_catch_base = saved_loop_cbase;
        g->finally_loop_base = saved_loop_fbase;
        g->eh_armed_loop_base = saved_loop_ehbase;

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        emit_release_owned_locals_from(g, locals, for_start);
        break;
    }

    case AST_BREAK_STMT:
        if (g->break_target) {
            emit_pending_finallys(g, locals, g->finally_loop_base, false);
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                break;
            /* 同上：catch 异常先出栈，局部变量后出栈 */
            emit_release_active_catch_excs(g, g->loop_catch_base);
            emit_release_owned_locals_range(g, locals, g->break_locals_base);
            emit_eh_disarm_from(g, g->eh_armed_loop_base);
            emit_pending_scope_exit(g, g->pending.break_scope, false);
            LLVMBuildBr(g->builder, g->break_target);
        }
        break;

    case AST_CONTINUE_STMT:
        if (g->continue_target) {
            emit_pending_finallys(g, locals, g->finally_loop_base, false);
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                break;
            /* 同上：catch 异常先出栈，局部变量后出栈 */
            emit_release_active_catch_excs(g, g->loop_catch_base);
            emit_release_owned_locals_range(g, locals, g->loop_locals_base);
            emit_eh_disarm_from(g, g->eh_armed_loop_base);
            emit_pending_scope_exit(g, g->pending.continue_scope, false);
            LLVMBuildBr(g->builder, g->continue_target);
        }
        break;

    case AST_SWITCH_STMT: {
        int switch_start = locals->count;
        LLVMValueRef switch_val = emit_expr(g, stmt->switch_stmt.expr, locals);
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.end");

        int num_cases = 0;
        zan_ast_node_t *default_case = NULL;
        for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            if (sc->switch_case.pattern || sc->switch_case.type_pattern ||
                sc->switch_case.when_cond || sc->switch_case.var_name.len > 0) {
                num_cases++;
            } else {
                default_case = sc;
            }
        }

        LLVMBasicBlockRef default_bb = default_case
            ? LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.default")
            : end_bb;

        zan_irgen_pending_scope_t *sw_saved_pending_break = g->pending.break_scope;
        g->pending.break_scope = g->pending.scope;
        LLVMBasicBlockRef sw_saved_break = g->break_target;
        g->break_target = end_bb;
        int sw_saved_break_base = g->break_locals_base;
        g->break_locals_base = switch_start;

        bool has_patterns = false;
        for (int i = 0; i < stmt->switch_stmt.cases.count && !has_patterns; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            if (sc->switch_case.type_pattern || sc->switch_case.when_cond ||
                sc->switch_case.var_name.len > 0 ||
                (sc->switch_case.pattern &&
                 sc->switch_case.pattern->kind == AST_NULL_LITERAL))
                has_patterns = true;
        }
        if (has_patterns) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            (void)LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            bool sw_string = is_string_expr(g, stmt->switch_stmt.expr, locals);
            LLVMValueRef strcmp_fn = NULL;
            LLVMTypeRef strcmp_ty = NULL;
            if (sw_string) {
                strcmp_fn = LLVMGetNamedFunction(g->mod, "strcmp");
                strcmp_ty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                if (!strcmp_fn) strcmp_fn = LLVMAddFunction(g->mod, "strcmp", strcmp_ty);
            }

            int nc = stmt->switch_stmt.cases.count;
            LLVMBasicBlockRef *pmatch = (LLVMBasicBlockRef *)calloc(
                (size_t)(nc > 0 ? nc : 1), sizeof(LLVMBasicBlockRef));
            int *psrc = (int *)calloc((size_t)(nc > 0 ? nc : 1), sizeof(int));
            int ci = 0;

            LLVMBasicBlockRef test_bb = LLVMAppendBasicBlockInContext(
                g->ctx, g->current_fn, "sw.ptest");
            LLVMBuildBr(g->builder, test_bb);

            for (int i = 0; i < nc; i++) {
                zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
                if (!sc->switch_case.pattern && !sc->switch_case.type_pattern)
                    continue;
                LLVMPositionBuilderAtEnd(g->builder, test_bb);
                LLVMBasicBlockRef match_bb = LLVMAppendBasicBlockInContext(
                    g->ctx, g->current_fn, "sw.pmatch");
                LLVMBasicBlockRef fail_bb = LLVMAppendBasicBlockInContext(
                    g->ctx, g->current_fn, "sw.pnext");

                emit_switch_pattern_bind(g, locals, sc, switch_val);

                LLVMValueRef cond;
                if (sc->switch_case.type_pattern) {
                    zan_type_t *pt = resolve_type_ctx(g, sc->switch_case.type_pattern);
                    if (pt && (pt->kind == TYPE_CLASS || pt->kind == TYPE_STRING ||
                               pt->kind == TYPE_OBJECT || pt->kind == TYPE_INTERFACE))
                        cond = emit_runtime_is_check_name(g, switch_val, pt);
                    else {
                        zan_type_t *dt = infer_expr_type(g, stmt->switch_stmt.expr,
                                                         locals);
                        cond = LLVMConstInt(LLVMInt1TypeInContext(g->ctx),
                            (pt && dt && types_equal(dt, pt)) ? 1 : 0, 0);
                    }
                } else if (sc->switch_case.pattern->kind == AST_NULL_LITERAL) {
                    cond = zan_icmp(g->builder, LLVMIntEQ, switch_val,
                        LLVMConstNull(LLVMTypeOf(switch_val)), "sw.null");
                } else if (sw_string) {
                    LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
                    LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty, strcmp_fn,
                        (LLVMValueRef[]){ switch_val, case_val }, 2, "swcmp");
                    cond = zan_icmp(g->builder, LLVMIntEQ, cmp,
                        LLVMConstInt(i32, 0, 0), "sweq");
                } else {
                    LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
                    cond = zan_icmp(g->builder, LLVMIntEQ, switch_val, case_val,
                                    "sw.eq");
                }
                if (sc->switch_case.when_cond) {
                    LLVMBasicBlockRef guard_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, g->current_fn, "sw.pguard");
                    LLVMBuildCondBr(g->builder, cond, guard_bb, fail_bb);
                    LLVMPositionBuilderAtEnd(g->builder, guard_bb);
                    LLVMValueRef gv = emit_expr(g, sc->switch_case.when_cond, locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(gv)) != LLVMIntegerTypeKind ||
                        LLVMGetIntTypeWidth(LLVMTypeOf(gv)) != 1)
                        gv = LLVMBuildICmp(g->builder, LLVMIntNE, gv,
                            LLVMConstInt(LLVMTypeOf(gv), 0, 0), "sw.guard");
                    LLVMBuildCondBr(g->builder, gv, match_bb, fail_bb);
                } else {
                    LLVMBuildCondBr(g->builder, cond, match_bb, fail_bb);
                }
                LLVMPositionBuilderAtEnd(g->builder, fail_bb);
                emit_switch_pattern_fail_release(g, locals, sc);
                pmatch[ci] = match_bb;
                psrc[ci] = i;
                ci++;
                test_bb = LLVMGetInsertBlock(g->builder);
            }

            LLVMPositionBuilderAtEnd(g->builder, test_bb);
            if (default_case)
                LLVMBuildBr(g->builder, default_bb);
            else
                LLVMBuildBr(g->builder, end_bb);

            for (int k = 0; k < ci; k++) {
                int i = psrc[k];
                zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
                LLVMPositionBuilderAtEnd(g->builder, pmatch[k]);
                bool case_empty = (sc->switch_case.body &&
                                   sc->switch_case.body->block.stmts.count == 0);
                if (case_empty) {
                    LLVMBasicBlockRef fall = NULL;
                    for (int k2 = k + 1; k2 < ci; k2++) {
                        zan_ast_node_t *nxt = stmt->switch_stmt.cases.items[psrc[k2]];
                        if (nxt->switch_case.body &&
                            nxt->switch_case.body->block.stmts.count > 0) {
                            fall = pmatch[k2];
                            break;
                        }
                    }
                    if (!fall) fall = default_bb;
                    LLVMBuildBr(g->builder, fall);
                    continue;
                }
                emit_switch_pattern_bind(g, locals, sc, switch_val);
                emit_stmt(g, sc->switch_case.body, locals);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                    emit_release_owned_locals_from(g, locals, switch_start);
                    LLVMBuildBr(g->builder, end_bb);
                } else {
                    emit_release_owned_locals_from(g, locals, switch_start);
                }
            }
            free(pmatch);
            free(psrc);

            if (default_case) {
                LLVMPositionBuilderAtEnd(g->builder, default_bb);
                emit_stmt(g, default_case->switch_case.body, locals);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                    emit_release_owned_locals_from(g, locals, switch_start);
                    LLVMBuildBr(g->builder, end_bb);
                } else {
                    emit_release_owned_locals_from(g, locals, switch_start);
                }
            }

            g->pending.break_scope = sw_saved_pending_break;
            g->break_target = sw_saved_break;
            g->break_locals_base = sw_saved_break_base;
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            break;
        }

        if (is_string_expr(g, stmt->switch_stmt.expr, locals)) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef strcmp_fn = LLVMGetNamedFunction(g->mod, "strcmp");
            LLVMTypeRef strcmp_ty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
            if (!strcmp_fn) strcmp_fn = LLVMAddFunction(g->mod, "strcmp", strcmp_ty);

            LLVMBasicBlockRef *case_bbs = (LLVMBasicBlockRef *)calloc(
                (size_t)(num_cases > 0 ? num_cases : 1), sizeof(LLVMBasicBlockRef));
            int *case_src = (int *)calloc((size_t)(num_cases > 0 ? num_cases : 1),
                                          sizeof(int));
            int ci = 0;
            for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
                zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
                if (!sc->switch_case.pattern) continue;
                LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
                LLVMBasicBlockRef case_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.case");
                LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.next");
                LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty, strcmp_fn,
                    (LLVMValueRef[]){ switch_val, case_val }, 2, "swcmp");
                LLVMValueRef eq = zan_icmp(g->builder, LLVMIntEQ, cmp,
                    LLVMConstInt(i32, 0, 0), "sweq");
                LLVMBuildCondBr(g->builder, eq, case_bb, next_bb);
                LLVMPositionBuilderAtEnd(g->builder, next_bb);
                case_bbs[ci] = case_bb;
                case_src[ci] = i;
                ci++;
            }
            LLVMBuildBr(g->builder, default_bb);

            for (int k = 0; k < ci; k++) {
                int i = case_src[k];
                zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
                LLVMPositionBuilderAtEnd(g->builder, case_bbs[k]);
                bool case_empty = (sc->switch_case.body &&
                                   sc->switch_case.body->block.stmts.count == 0);
                if (case_empty) {
                    LLVMBasicBlockRef fall_bb = NULL;
                    for (int k2 = k + 1; k2 < ci; k2++) {
                        zan_ast_node_t *nxt =
                            stmt->switch_stmt.cases.items[case_src[k2]];
                        if (nxt->switch_case.body &&
                            nxt->switch_case.body->block.stmts.count > 0) {
                            fall_bb = case_bbs[k2];
                            break;
                        }
                    }
                    if (!fall_bb) fall_bb = default_bb;
                    LLVMBuildBr(g->builder, fall_bb);
                    continue;
                }
                emit_stmt(g, sc->switch_case.body, locals);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                    emit_release_owned_locals_from(g, locals, switch_start);
                    LLVMBuildBr(g->builder, end_bb);
                } else {
                    emit_release_owned_locals_from(g, locals, switch_start);
                }
            }
            free(case_bbs);
            free(case_src);

            if (default_case) {
                LLVMPositionBuilderAtEnd(g->builder, default_bb);
                emit_stmt(g, default_case->switch_case.body, locals);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                    emit_release_owned_locals_from(g, locals, switch_start);
                    LLVMBuildBr(g->builder, end_bb);
                } else {
                    emit_release_owned_locals_from(g, locals, switch_start);
                }
            }

            g->pending.break_scope = sw_saved_pending_break;
            g->break_target = sw_saved_break;
            g->break_locals_base = sw_saved_break_base;
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            break;
        }

        LLVMValueRef sw = LLVMBuildSwitch(g->builder, switch_val, default_bb, (unsigned)num_cases);

        LLVMBasicBlockRef *case_bbs = (LLVMBasicBlockRef *)calloc(
            (size_t)(num_cases > 0 ? num_cases : 1), sizeof(LLVMBasicBlockRef));
        int *case_src = (int *)calloc((size_t)(num_cases > 0 ? num_cases : 1),
                                      sizeof(int));
        int nci = 0;
        for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            if (!sc->switch_case.pattern) continue;

            LLVMBasicBlockRef case_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.case");
            LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->builder);
            LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
            if (!case_val || !LLVMIsAConstantInt(case_val)) {
                zan_diag_emit_code(g->diag, DIAG_ERROR, sc->loc, 3074,
                              "case label must be a compile-time constant "
                              "in an integer switch");
                LLVMPositionBuilderAtEnd(g->builder, case_bb);
                emit_release_owned_locals_from(g, locals, switch_start);
                LLVMBuildBr(g->builder, end_bb);
                LLVMPositionBuilderAtEnd(g->builder, entry_bb);
                continue;
            }
            LLVMTypeRef swty = LLVMTypeOf(switch_val);
            if (LLVMGetTypeKind(LLVMTypeOf(case_val)) == LLVMIntegerTypeKind &&
                LLVMGetTypeKind(swty) == LLVMIntegerTypeKind &&
                LLVMTypeOf(case_val) != swty) {
                long long cv = LLVMConstIntGetSExtValue(case_val);
                case_val = LLVMConstInt(swty, (unsigned long long)cv, 1);
            }
            LLVMAddCase(sw, case_val, case_bb);
            case_bbs[nci] = case_bb;
            case_src[nci] = i;
            nci++;
        }

        for (int k = 0; k < nci; k++) {
            int i = case_src[k];
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            LLVMBasicBlockRef case_bb = case_bbs[k];
            bool case_empty = (sc->switch_case.body &&
                               sc->switch_case.body->block.stmts.count == 0);
            LLVMBasicBlockRef fall_bb = NULL;
            if (case_empty) {
                for (int k2 = k + 1; k2 < nci; k2++) {
                    zan_ast_node_t *nxt =
                        stmt->switch_stmt.cases.items[case_src[k2]];
                    if (nxt->switch_case.body &&
                        nxt->switch_case.body->block.stmts.count > 0) {
                        fall_bb = case_bbs[k2];
                        break;
                    }
                }
                if (!fall_bb) fall_bb = default_bb;
            }

            LLVMPositionBuilderAtEnd(g->builder, case_bb);
            if (case_empty) {
                LLVMBuildBr(g->builder, fall_bb);
                continue;
            }
            emit_stmt(g, sc->switch_case.body, locals);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                emit_release_owned_locals_from(g, locals, switch_start);
                LLVMBuildBr(g->builder, end_bb);
            } else {
                emit_release_owned_locals_from(g, locals, switch_start);
            }
        }
        free(case_bbs);
        free(case_src);

        if (default_case) {
            LLVMPositionBuilderAtEnd(g->builder, default_bb);
            emit_stmt(g, default_case->switch_case.body, locals);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                emit_release_owned_locals_from(g, locals, switch_start);
                LLVMBuildBr(g->builder, end_bb);
            } else {
                emit_release_owned_locals_from(g, locals, switch_start);
            }
        }

        g->pending.break_scope = sw_saved_pending_break;
        g->break_target = sw_saved_break;
        g->break_locals_base = sw_saved_break_base;
        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        break;
    }
