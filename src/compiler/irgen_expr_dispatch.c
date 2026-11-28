#include "irgen_internal.h"
static LLVMValueRef emit_expr_impl(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);

LLVMValueRef emit_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals) {
    void *saved_locals = g->active_cleanup_locals;
    LLVMValueRef saved_fn = g->active_cleanup_fn;
    if (locals) {
        g->active_cleanup_locals = locals;
        LLVMBasicBlockRef bb = LLVMGetInsertBlock(g->builder);
        g->active_cleanup_fn = bb ? LLVMGetBasicBlockParent(bb) : NULL;
    }
    LLVMValueRef value = emit_expr_impl(g, expr, locals);
    g->active_cleanup_locals = saved_locals;
    g->active_cleanup_fn = saved_fn;
    return value;
}

static LLVMValueRef emit_expr_impl(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals) {
    if (!expr) return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    if (locals) g->current_locals = locals;
    if (g->emit_debug && expr->loc.line > 0) {
        di_set_loc(g, expr->loc);
    }

    zan_type_t *method_group_target = zan_binder_method_group_target(expr);
    if (method_group_target)
        return emit_method_group_conversion(g, expr, method_group_target, locals);

    if (expr->kind == AST_REF_ARG) {
        return emit_ref_arg(g, expr, locals); }
    if (expr->kind == AST_MEMBER_ACCESS && expr->member.null_cond) {
        return emit_null_cond(g, expr, expr, locals);
    }
    if (expr->kind == AST_CALL && expr->call.callee &&
        expr->call.callee->kind == AST_MEMBER_ACCESS &&
        expr->call.callee->member.null_cond) {
        return emit_null_cond(g, expr, expr->call.callee, locals);
    }

    switch (expr->kind) {
    case AST_INT_LITERAL: {
        /* 运行时值与静态类型匹配检查 */
        int64_t v = expr->int_val;
        if (expr->lit_suffix == 0 && v > 2147483647LL && v <= 4294967295LL &&
            (expr->lit_radix == 2 || expr->lit_radix == 8 ||
             expr->lit_radix == 16))
            v = (int32_t)v;
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), (uint64_t)v, 1);
    }

    case AST_FLOAT_LITERAL:
        /* f/F 后缀字面量发射为 f32 常量:此前一律发射 double 常量,
         * 导致 float 变量与字面量比较时被提升到 double 空间,
         * 非精确表示的值(如 0.2f)永远不等。 */
        if (expr->lit_suffix) {
            return LLVMConstReal(LLVMFloatTypeInContext(g->ctx),
                                 expr->float_val);
        }
        return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), expr->float_val);

    case AST_DECIMAL_LITERAL: {
        zan_decimal_t dec;
        const char *s = expr->str_val.str ? expr->str_val.str : "0";
        if (!zan_decimal_parse(s, (size_t)expr->str_val.len, &dec)) {
            dec = zan_decimal_zero();
        }
        LLVMValueRef fld_vals[3] = {
            LLVMConstInt(LLVMInt32TypeInContext(g->ctx), dec.flags, 0),
            LLVMConstInt(LLVMInt32TypeInContext(g->ctx), dec.hi32, 0),
            LLVMConstInt(LLVMInt64TypeInContext(g->ctx), dec.lo64, 0)
        };
        return LLVMConstNamedStruct(get_decimal_type(g), fld_vals, 3);
    }

    case AST_STRING_LITERAL:
        return emit_string_literal_rc(g, expr->str_val);

    case AST_BOOL_LITERAL:
        return LLVMConstInt(LLVMInt1TypeInContext(g->ctx), expr->bool_val ? 1 : 0, 0);

    case AST_CHAR_LITERAL:
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), (uint64_t)expr->int_val, 0);

    case AST_NULL_LITERAL:
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));

    case AST_IDENTIFIER:
        return emit_expr_identifier(g, expr, locals);

    case AST_BINARY:
        return emit_expr_binary(g, expr, locals);

    case AST_UNARY:
        return emit_expr_unary(g, expr, locals);

    case AST_ASSIGNMENT:
        return emit_expr_assignment(g, expr, locals);

    case AST_CALL: {
        zan_ast_node_t *saved_call = g->hold_call_expr;
        g->hold_call_expr = expr;
        LLVMValueRef call_v = emit_expr_call(g, expr, locals);
        g->hold_call_expr = saved_call;
        zan_type_t *cty = infer_expr_type(g, expr, locals);
        if (call_returns_ref(g, expr, locals) && call_v && cty &&
            LLVMGetTypeKind(LLVMTypeOf(call_v)) == LLVMPointerTypeKind) {
            LLVMTypeRef elem_ty = map_type(g, cty);
            LLVMValueRef typed_p = call_v;
            if (LLVMTypeOf(typed_p) != LLVMPointerType(elem_ty, 0))
                typed_p = LLVMBuildBitCast(g->builder, typed_p,
                                           LLVMPointerType(elem_ty, 0), "ref.p");
            call_v = LLVMBuildLoad2(g->builder, elem_ty, typed_p, "ref.ld");
        }
        return promote_loaded(g, call_v, cty);
    }

    case AST_STRING_INTERP:
        return emit_expr_string_interp(g, expr, locals);

    case AST_MEMBER_ACCESS:
        return emit_expr_member_access(g, expr, locals);

    case AST_INDEX:
        return emit_expr_index(g, expr, locals);

    case AST_QUERY_EXPR:
        return emit_expr_query_expr(g, expr, locals);

    case AST_NEW_EXPR:
        return emit_expr_new_expr(g, expr, locals);

    case AST_TUPLE_EXPR:
        return emit_expr_tuple(g, expr, locals);

    case AST_CONDITIONAL:
        return emit_expr_conditional(g, expr, locals);

    case AST_SWITCH_EXPR:
        return emit_expr_switch_expr(g, expr, locals);

    case AST_WITH_EXPR:
        return emit_expr_with_expr(g, expr, locals);

    case AST_POSTFIX_UNARY:
        if (expr->unary.op == TK_BANG) {
            LLVMValueRef v = emit_expr(g, expr->unary.operand, locals);
            if (v && llvm_is_nullable(LLVMTypeOf(v)))
                return nullable_get_payload(g, v);
            return v;
        }
        if (expr->unary.op == TK_QUESTION) {
            /* Pillar 10: Rust-style `?` error propagation on Result<T, E> ADT */
            zan_type_t *op_t =
                concretize(g, infer_expr_type(g, expr->unary.operand, locals));
            LLVMValueRef rv = emit_expr(g, expr->unary.operand, locals);
            if (!rv || !op_t || op_t->type_arg_count < 2)
                return rv;
            LLVMTypeRef op_llvm = map_type(g, op_t);
            if (LLVMGetTypeKind(LLVMTypeOf(rv)) == LLVMPointerTypeKind &&
                LLVMGetTypeKind(op_llvm) == LLVMStructTypeKind) {
                rv = LLVMBuildLoad2(g->builder, op_llvm, rv, "try.load");
            }
            LLVMValueRef tag =
                LLVMBuildExtractValue(g->builder, rv, 0, "try.tag");
            LLVMValueRef is_ok = zan_icmp(
                g->builder, LLVMIntEQ, tag,
                LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0), "try.ok");
            LLVMBasicBlockRef ok_bb =
                LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "try.ok");
            LLVMBasicBlockRef err_bb =
                LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "try.err");
            LLVMBuildCondBr(g->builder, is_ok, ok_bb, err_bb);

            /* Err branch: construct Result<U, E>.Err(err_val) and return */
            LLVMPositionBuilderAtEnd(g->builder, err_bb);
            LLVMValueRef err_val =
                LLVMBuildExtractValue(g->builder, rv, 2, "try.errval");
            zan_type_t *err_ty = concretize(g, op_t->type_args[1]);
            bool src_owned =
                expr_yields_owned_rc_value(g, expr->unary.operand, locals);
            if (!src_owned && err_ty) {
                if (is_rc_managed_type(err_ty)) {
                    emit_rc_retain_for_type(g, err_ty, err_val);
                } else if (err_ty->kind == TYPE_OBJECT &&
                           LLVMGetTypeKind(LLVMTypeOf(err_val)) ==
                               LLVMPointerTypeKind) {
                    emit_arc_retain(g, err_val);
                } else if (err_ty->kind == TYPE_STRUCT &&
                           type_contains_collection_rc(g, err_ty, 0)) {
                    emit_collection_value_retain(g, err_ty, err_val, 0);
                }
            }
            zan_type_t *ret_zan = g->current_async_frame
                                      ? g->current_async_ret_type
                                      : g->current_fn_zan_ret_type;
            if (ret_zan && ret_zan->kind == TYPE_TASK &&
                ret_zan->type_arg_count == 1)
                ret_zan = ret_zan->type_args[0];
            ret_zan = concretize(g, ret_zan);
            LLVMTypeRef ret_llvm = g->current_async_frame
                                       ? map_type(g, ret_zan)
                                       : g->current_fn_ret_type;
            LLVMValueRef res = LLVMConstNull(ret_llvm);
            res = LLVMBuildInsertValue(
                g->builder, res,
                LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1, 0), 0,
                "try.err.tag");
            res = LLVMBuildInsertValue(g->builder, res, err_val, 2,
                                       "try.err.res");
            if (g->current_async_frame) {
                LLVMValueRef ri = NULL;
                if (g->finally_count > 0) {
                    emit_async_pending_return(g, res);
                    emit_pending_finallys(g, locals, 0, true);
                    if (!LLVMGetBasicBlockTerminator(
                            LLVMGetInsertBlock(g->builder))) {
                        ri = emit_async_pending_take_return(g);
                        emit_release_active_catch_excs(g, 0);
                        emit_async_complete(g, locals, ri);
                    }
                } else {
                    ri = coerce_to_frame_result(g, res, ret_zan);
                    emit_release_active_catch_excs(g, 0);
                    emit_async_complete(g, locals, ri);
                }
            } else {
                if (g->finally_count > 0) {
                    LLVMValueRef v_slot =
                        emit_entry_alloca(g, ret_llvm, "try.fin.slot");
                    zan_store_fit(g, res, v_slot);
                    emit_pending_finallys(g, locals, 0, true);
                    if (!LLVMGetBasicBlockTerminator(
                            LLVMGetInsertBlock(g->builder))) {
                        res = LLVMBuildLoad2(g->builder, ret_llvm, v_slot,
                                             "try.fin.val");
                        emit_release_active_catch_excs(g, 0);
                        emit_release_owned_locals(g, locals);
                        LLVMBuildRet(g->builder, res);
                    }
                } else {
                    emit_release_active_catch_excs(g, 0);
                    emit_release_owned_locals(g, locals);
                    LLVMBuildRet(g->builder, res);
                }
            }

            /* Ok branch: yield payload value T */
            LLVMPositionBuilderAtEnd(g->builder, ok_bb);
            return LLVMBuildExtractValue(g->builder, rv, 1, "try.okval");
        }
        return emit_incdec_expr(g, expr, locals, 0);

    case AST_IS_EXPR: {
        /* 单继承体系下的 x is T 祖先链类型判断 */
        zan_type_t *st = infer_expr_type(g, expr->type_test.expr, locals);
        LLVMValueRef v = emit_expr(g, expr->type_test.expr, locals);
        LLVMValueRef test;
        if (!expr->type_test.type) {
            test = zan_icmp(g->builder, LLVMIntEQ, v,
                LLVMConstNull(LLVMTypeOf(v)), "is.null");
        } else {
            zan_type_t *tt = resolve_type_ctx(g, expr->type_test.type);
            if (!st || !tt) {
                test = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
            } else {
                bool st_ref = st->kind == TYPE_OBJECT || st->kind == TYPE_INTERFACE ||
                              st->kind == TYPE_STRING || st->kind == TYPE_CLASS;
                bool tt_ref = tt->kind == TYPE_OBJECT || tt->kind == TYPE_INTERFACE ||
                              tt->kind == TYPE_STRING || tt->kind == TYPE_CLASS;
                if (!st_ref || !tt_ref) {
                    /* A454：装箱结构体的运行期识别——接收者是引用形状、
                     * 目标是装箱资格结构体时走描述符名表比对（静态
                     * types_equal 对装箱值恒为假）。 */
                    if (st_ref && tt && tt->kind == TYPE_STRUCT &&
                        LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind &&
                        irgen_boxed_eligible_struct(g, tt))
                        test = emit_runtime_is_check_name(g, v, tt);
                    else
                        test = LLVMConstInt(LLVMInt1TypeInContext(g->ctx),
                                            types_equal(st, tt) ? 1 : 0, 0);
                } else {
                    LLVMValueRef nn = zan_icmp(g->builder, LLVMIntNE, v,
                        LLVMConstNull(LLVMTypeOf(v)), "is.nonnull");
                    if (type_mentions_canon(tt)) {
                        if (types_equal(st, tt)) {
                            test = nn;
                        } else {
                            test = emit_runtime_is_check_name(g, v, tt);
                        }
                    } else if (st->kind == TYPE_STRING || tt->kind == TYPE_STRING) {
                        if (st->kind == tt->kind) {
                            test = nn;
                        } else if (st->kind == TYPE_OBJECT ||
                                   st->kind == TYPE_INTERFACE ||
                                   tt->kind == TYPE_OBJECT ||
                                   tt->kind == TYPE_INTERFACE) {
                            test = emit_runtime_is_check_name(g, v, tt);
                        } else {
                            test = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
                        }
                    } else if (tt->kind == TYPE_OBJECT) {
                        test = nn;
                    } else if (tt->kind == TYPE_INTERFACE) {
                        test = emit_runtime_is_check_name(g, v, tt);
                    } else if (st->kind == TYPE_CLASS && type_is_or_derives(st, tt)) {
                        test = nn;
                    } else if (st->kind == TYPE_CLASS && type_is_or_derives(tt, st)) {
                        test = emit_runtime_is_check_name(g, v, tt);
                    } else if (st->kind == TYPE_OBJECT || st->kind == TYPE_INTERFACE) {
                        test = emit_runtime_is_check_name(g, v, tt);
                    } else {
                        test = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
                    }
                }
            }
        }
        LLVMValueRef matched = test;
        if (expr->type_test.is_not)
            test = LLVMBuildNot(g->builder, test, "is.not");
        /* 模式匹配变量 (x is T v)：类型匹配成功时在当前作用域注册变量 v */
        if (expr->type_test.var_name.len > 0) {
            zan_type_t *bt = expr->type_test.type
                ? resolve_type_ctx(g, expr->type_test.type) : st;
            if (!bt) bt = g->binder->type_object;
            LLVMTypeRef btll = map_type(g, bt);
            LLVMValueRef slot = emit_entry_alloca(g, btll, "pat");
            /* A short-circuited pattern may never execute, but its condition
             * scope still cleans the slot. Initialize it in the entry block. */
            zan_builder_point_t pattern_point = zan_builder_save(g);
            LLVMValueRef after_slot = LLVMGetNextInstruction(slot);
            if (after_slot) LLVMPositionBuilderBefore(g->builder, after_slot);
            else LLVMPositionBuilderAtEnd(g->builder, LLVMGetInstructionParent(slot));
            zan_store_fit(g, LLVMConstNull(btll), slot);
            zan_builder_restore(g, pattern_point);
            bool pattern_rc = is_rc_managed_type(bt) &&
                LLVMGetTypeKind(btll) == LLVMPointerTypeKind;
            bool source_owned = expr_yields_owned_rc_value(g,
                expr->type_test.expr, locals) != 0;
            LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef hit = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.bind");
            LLVMBasicBlockRef miss = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.drop");
            LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.done");
            LLVMBuildCondBr(g->builder, matched, hit, miss);
            LLVMPositionBuilderAtEnd(g->builder, hit);
            LLVMValueRef castv = v;
            if (LLVMTypeOf(castv) != btll &&
                LLVMGetTypeKind(btll) == LLVMPointerTypeKind &&
                LLVMGetTypeKind(LLVMTypeOf(castv)) == LLVMPointerTypeKind)
                castv = LLVMBuildBitCast(g->builder, castv, btll, "pat.cast");
            if (pattern_rc && !source_owned)
                emit_rc_retain_for_type(g, bt, castv);
            zan_store_fit(g, castv, slot);
            if (!pattern_rc)
                emit_release_owned_call_temp(g, expr->type_test.expr, v, locals);
            LLVMBuildBr(g->builder, done);
            LLVMPositionBuilderAtEnd(g->builder, miss);
            zan_store_fit(g, LLVMConstNull(btll), slot);
            emit_release_owned_call_temp(g, expr->type_test.expr, v, locals);
            LLVMBuildBr(g->builder, done);
            LLVMPositionBuilderAtEnd(g->builder, done);
            local_add(locals, expr->type_test.var_name, slot, bt);
            if (pattern_rc) arc_own_local(g, locals);
        } else {
            emit_release_owned_call_temp(g, expr->type_test.expr, v, locals);
        }
        return test;
    }

    case AST_AS_EXPR: {
        zan_type_t *st = infer_expr_type(g, expr->type_test.expr, locals);
        zan_type_t *tt = resolve_type_ctx(g, expr->type_test.type);
        if (!st || !tt) return emit_expr(g, expr->type_test.expr, locals);
        bool st_ref = st->kind == TYPE_OBJECT || st->kind == TYPE_INTERFACE ||
                      st->kind == TYPE_STRING || st->kind == TYPE_CLASS;
        bool tt_ref = tt->kind == TYPE_OBJECT || tt->kind == TYPE_INTERFACE ||
                      tt->kind == TYPE_STRING || tt->kind == TYPE_CLASS;
        if (!st_ref || !tt_ref)
            return emit_expr(g, expr->type_test.expr, locals);
        /* B-ID163④：免查透传仅限静态可证安全的两条——object 目标（引用
         * 源上转恒真）、string 源到 string（恒等）。旧实现把 string/
         * interface 目标也零检查透传：异型对象 `as string` 直接把指针当
         * string 用、无关类 `as 接口` 绕过实现检查——类型混淆
         * （TASKS B-ID163④）。其余一律补运行时检查，失败静默 null
         * （as 契约，C# 同语义；与强转 `(T)x` 的抛异常口径不同）。 */
        if (!type_mentions_canon(tt)) {
            if (tt->kind == TYPE_OBJECT)
                return emit_expr(g, expr->type_test.expr, locals);
            if (st->kind == TYPE_STRING && tt->kind == TYPE_STRING)
                return emit_expr(g, expr->type_test.expr, locals);
        }
        LLVMValueRef v = emit_expr(g, expr->type_test.expr, locals);
        if (types_equal(st, tt) ||
            (!type_mentions_canon(tt) && st->kind == TYPE_CLASS &&
             type_is_or_derives(st, tt)))
            return v;
        LLVMValueRef ok = emit_runtime_is_check_name(g, v, tt);
        if (expr_yields_owned_rc_value(g, expr->type_test.expr, locals)) {
            LLVMBasicBlockRef success = LLVMGetInsertBlock(g->builder);
            LLVMValueRef fn = LLVMGetBasicBlockParent(success);
            LLVMBasicBlockRef failed = LLVMAppendBasicBlockInContext(g->ctx, fn, "as.fail");
            LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "as.done");
            LLVMBuildCondBr(g->builder, ok, done, failed);
            LLVMPositionBuilderAtEnd(g->builder, failed);
            emit_release_owned_call_temp(g, expr->type_test.expr, v, locals);
            LLVMBasicBlockRef released = LLVMGetInsertBlock(g->builder);
            LLVMBuildBr(g->builder, done);
            LLVMPositionBuilderAtEnd(g->builder, done);
            LLVMValueRef result = LLVMBuildPhi(g->builder, LLVMTypeOf(v), "as.res");
            LLVMValueRef values[] = {v, LLVMConstNull(LLVMTypeOf(v))};
            LLVMBasicBlockRef blocks[] = {success, released};
            LLVMAddIncoming(result, values, blocks, 2);
            return result;
        }
        return LLVMBuildSelect(g->builder, ok, v,
            LLVMConstNull(LLVMTypeOf(v)), "as.res");
    }

    case AST_CAST_EXPR:
        return emit_expr_cast_expr(g, expr, locals);

    case AST_SIZEOF_EXPR: {
        int64_t qv = 0;
        if (zan_irgen_eval_layout_query(g, expr, &qv))
            return LLVMConstInt(LLVMInt64TypeInContext(g->ctx),
                                (unsigned long long)qv, 0);
        zan_type_t *t = resolve_type_ctx(g, expr->cast.type);
        LLVMTypeRef mt = t ? map_type(g, t) : NULL;
        if (mt && LLVMGetTypeKind(mt) != LLVMVoidTypeKind)
            return LLVMSizeOf(mt);
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 8, 0);
    }

    case AST_STACKALLOC_EXPR: {
        zan_type_t *et = resolve_type_ctx(g, expr->cast.type);
        LLVMTypeRef elem_ty = et ? map_type(g, et) : LLVMInt8TypeInContext(g->ctx);
        LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
        unsigned long ea = abi_align_of(elem_ty);
        unsigned align = (ea < 16) ? 16 : (unsigned)ea;
        unsigned long esz = abi_size_of(elem_ty);
        if (esz == 0) esz = 1;

        LLVMValueRef raw_ptr = NULL;
        LLVMValueRef len64 = NULL;

        if (expr->cast.expr && expr->cast.expr->kind == AST_INT_LITERAL &&
            expr->cast.expr->int_val >= 0) {
            uint64_t cnt = (uint64_t)expr->cast.expr->int_val;
            LLVMTypeRef arr_ty = LLVMArrayType(elem_ty, (unsigned)(cnt > 0 ? cnt : 1));
            LLVMValueRef slot = emit_entry_alloca(g, arr_ty, "stackalloc.buf");
            LLVMSetAlignment(slot, align);
            raw_ptr = LLVMBuildBitCast(g->builder, slot, i8ptr, "stackalloc.p");
            len64 = LLVMConstInt(i64, cnt, 0);
            if (cnt > 0) {
                LLVMBuildMemSet(g->builder, raw_ptr, LLVMConstInt(i8, 0, 0),
                                LLVMConstInt(i64, cnt * esz, 0), align);
            }
        } else {
            LLVMValueRef cv = emit_expr(g, expr->cast.expr, locals);
            cv = coerce_int_to(g, cv, i64);
            LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
            LLVMValueRef neg = LLVMBuildICmp(g->builder, LLVMIntSLT, cv, zero, "sa.neg");
            len64 = LLVMBuildSelect(g->builder, neg, zero, cv, "sa.len");
            LLVMValueRef alloc_n = LLVMBuildSelect(
                g->builder,
                LLVMBuildICmp(g->builder, LLVMIntEQ, len64, zero, "sa.z"),
                LLVMConstInt(i64, 1, 0), len64, "sa.n");
            LLVMValueRef slot = LLVMBuildArrayAlloca(g->builder, elem_ty, alloc_n,
                                                     "stackalloc.dyn");
            LLVMSetAlignment(slot, align);
            raw_ptr = LLVMBuildBitCast(g->builder, slot, i8ptr, "stackalloc.dp");
            LLVMValueRef bytes = LLVMBuildMul(g->builder, len64,
                                              LLVMConstInt(i64, esz, 0), "sa.bytes");
            LLVMBuildMemSet(g->builder, raw_ptr, LLVMConstInt(i8, 0, 0), bytes, align);
        }

        LLVMValueRef sp = LLVMGetUndef(g->span_struct_type);
        sp = LLVMBuildInsertValue(g->builder, sp, raw_ptr, 0, "sa.sp0");
        sp = LLVMBuildInsertValue(g->builder, sp, len64, 1, "sa.sp");
        return sp;
    }

    case AST_TYPEOF_EXPR: {
        /* typeof(T): 获取类型的静态反射元数据记录 */
        zan_type_t *t = resolve_type_ctx(g, expr->cast.type);
        if (g->cur_canon_body && g->cur_gctx_val && t &&
            type_mentions_canon(t)) {
            return emit_gctx_load_slot(g, GCTX_SLOT_TYPEINFO, t->sym, t,
                                       NULL, 0, 0, 0);
        }
        char buf[256];
        int len = 0;
        if (t) {
            len = render_type_full(t, buf, (int)sizeof(buf));
        } else {
            len = render_type_ref_name(expr->cast.type, buf, (int)sizeof(buf));
        }
        return refl_meta_for(g, t, buf, len);
    }

    case AST_THIS_EXPR: {
        /* this 指针读取：从接收者局部变量槽位加载 */
        if (g->current_this) {
            return LLVMBuildLoad2(g->builder, LLVMGetAllocatedType(g->current_this),
                g->current_this, "this");
        }
        /* 非捕获 Lambda 内部无隐式 this 绑定 */
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3041,
            "cannot use 'this' here: %s has no receiver binding",
            g->lambda_depth > 0
                ? "a lambda cannot capture 'this' (lambdas are non-capturing)"
                : "a static context");
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
    }

    case AST_BASE_EXPR: {
        if (g->current_this) {
            return LLVMBuildLoad2(g->builder, LLVMGetAllocatedType(g->current_this),
                g->current_this, "this");
        }
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3042,
            "cannot use 'base' here: %s has no receiver binding",
            g->lambda_depth > 0
                ? "a lambda cannot capture 'this' (lambdas are non-capturing)"
                : "a static context");
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
    }

    case AST_AWAIT_EXPR:
        return emit_expr_await_expr(g, expr, locals);

    case AST_LAMBDA:
        return emit_lambda_typed(g, expr, NULL, locals);

    case AST_CHECKED_STMT:
        /* 表达式级 checked / unchecked 运算溢出开关处理 */
        {
            int saved = g->irgen_checked_depth;
            g->irgen_checked_depth = expr->checked_stmt.checked
                ? saved + 1 : saved - 1;
            LLVMValueRef v = emit_expr(g, expr->checked_stmt.body, locals);
            g->irgen_checked_depth = saved;
            return v;
        }

    default:
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    }
}
