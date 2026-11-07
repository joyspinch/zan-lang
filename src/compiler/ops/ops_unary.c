static LLVMValueRef emit_unary_op_overload(zan_irgen_t *g, zan_ast_node_t *expr,
                                            local_scope_t *locals) {
    zan_type_t *ot = infer_expr_type(g, expr->unary.operand, locals);
    if (!ot || !ot->sym ||
        (ot->kind != TYPE_CLASS && ot->kind != TYPE_STRUCT))
        return NULL;

    const char *op_name =
        (expr->unary.op == TK_MINUS) ? "op_sub" :
        (expr->unary.op == TK_PLUS) ? "op_add" :
        (expr->unary.op == TK_PLUS_PLUS) ? "op_inc" : "op_dec";
    const char *op_sign =
        (expr->unary.op == TK_MINUS) ? "-" :
        (expr->unary.op == TK_PLUS) ? "+" :
        (expr->unary.op == TK_PLUS_PLUS) ? "++" : "--";
    zan_istr_t op_istr = { (char *)op_name, (int)strlen(op_name) };

    zan_ast_node_t *probe = zan_ast_new(g->arena, AST_CALL, expr->loc);
    probe->call.callee = NULL;
    zan_ast_list_init(&probe->call.args);
    zan_ast_list_init(&probe->call.type_args);
    /* 一元重载：探针不带显式实参 */
    zan_symbol_t *op_sym = resolve_op_overload(g, ot->sym, op_istr, probe, locals);
    if (!op_sym) {
        /* ++/-- 的调用方（emit_incdec_expr）负责自己的诊断：这里静默
         * 返回 NULL，让"requires a numeric operand"维持原口径。 */
        if (expr->unary.op == TK_PLUS_PLUS || expr->unary.op == TK_MINUS_MINUS)
            return NULL;
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3004,
                      "type '%s' does not define a unary '%s' operator overload",
                      ot->name.str ? ot->name.str : "<unknown>", op_sign);
        return LLVMGetUndef(map_type(g, ot));
    }
    int fi = irgen_find_function(g, op_sym);
    if (fi < 0) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3005,
                      "unary '%s' operator overload of '%s' has no body to call",
                      op_sign, ot->name.str ? ot->name.str : "<unknown>");
        return LLVMGetUndef(map_type(g, ot));
    }

    /* 泛型方法的类型实参绑定要看到实参 → 探针补上操作数 */
    zan_ast_list_push(&probe->call.args, expr->unary.operand, g->arena);

    LLVMValueRef arg = emit_expr(g, expr->unary.operand, locals);
    LLVMTypeRef recv_t = map_type(g, ot);
    /* 接收者既可能是"值"，也可能是"槽位指针"（new 表达式返回的是槽位地址）：
     * 按值传参会把指针塞进调用里，LLVM 校验直接失败。 */
    if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMPointerTypeKind &&
        LLVMGetTypeKind(recv_t) == LLVMStructTypeKind) {
        arg = LLVMBuildLoad2(g->builder, recv_t, arg, "unop.recv");
    }
    bool op_is_static = (op_sym->modifiers & MOD_STATIC) != 0;
    if (!op_is_static &&
        LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMStructTypeKind) {
        LLVMValueRef rslot = emit_entry_alloca(g, LLVMTypeOf(arg), "unop.recv");
        LLVMBuildStore(g->builder, arg, rslot);
        arg = rslot;
    }
    /* 泛型特化：重载方法走具体实例的变体 */
    LLVMTypeRef mft = g->functions[fi].fn_type;
    LLVMValueRef mfn = route_generic_method(g, ot, op_sym, g->functions[fi].fn,
                                            mft, &mft);
    zan_type_t *p1 = method_param_type_at(g, op_sym, 1, probe,
                                          expr->unary.operand, locals);
    bool op_generic = p1 && type_mentions_tp(p1);
    if (p1) p1 = subst_type_param_deep(g, p1, ot);
    if (p1 && !op_generic) {
        LLVMTypeRef p1t = map_type(g, p1);
        if (LLVMGetTypeKind(p1t) == LLVMStructTypeKind &&
            LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMPointerTypeKind) {
            arg = LLVMBuildLoad2(g->builder, p1t, arg, "unop.sval");
        }
        if (LLVMTypeOf(arg) != p1t)
            arg = coerce_int_to(g, arg, p1t);
    }
    LLVMValueRef res = zan_call2(g->builder, mft, mfn, &arg, 1, "opcall");
    emit_release_owned_call_temp(g, expr->unary.operand, arg, locals);
    return res;
}
LLVMValueRef emit_expr_unary(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
    if (expr->unary.op == TK_PLUS_PLUS || expr->unary.op == TK_MINUS_MINUS)
        return emit_incdec_expr(g, expr, locals, 1);
    if (expr->unary.op == TK_MINUS || expr->unary.op == TK_PLUS) {
        LLVMValueRef ov = emit_unary_op_overload(g, expr, locals);
        if (ov) return ov;
    }
    LLVMValueRef operand = emit_expr(g, expr->unary.operand, locals);
        switch (expr->unary.op) {
        case TK_MINUS: {
            LLVMTypeRef t = LLVMTypeOf(operand);
            if (t == get_decimal_type(g)) {
                LLVMValueRef slot = emit_entry_alloca(g, t, "dec.uneg_in");
                LLVMValueRef res_slot = emit_entry_alloca(g, t, "dec.uneg_res");
                LLVMBuildStore(g->builder, operand, slot);
                LLVMTypeRef ptr_ty = LLVMPointerType(t, 0);
                LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                    (LLVMTypeRef[]){ ptr_ty, ptr_ty }, 2, 0);
                LLVMValueRef fn = get_libc_fn(g, "__zan_decimal_neg", fn_ty);
                zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){ slot, res_slot }, 2, "");
                return LLVMBuildLoad2(g->builder, t, res_slot, "dec.uneg_val");
            }
            if (LLVMGetTypeKind(t) == LLVMDoubleTypeKind ||
                LLVMGetTypeKind(t) == LLVMFloatTypeKind) {
                return LLVMBuildFNeg(g->builder, operand, "neg");
            }
            if (g->irgen_checked_depth > 0 &&
                LLVMGetTypeKind(t) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(t) > 1 &&
                !expr_is_uint(g, expr->unary.operand, locals) &&
                !expr_is_ulong(g, expr->unary.operand, locals)) {
                /* checked 下 -MIN 溢出陷阱（B-ID76）；int 值可能以 i64 承载，
                 * 按静态类型取 MIN。 */
                unsigned w = LLVMGetIntTypeWidth(t);
                long long minv = (w > 32 && expr_is_int32(g, expr->unary.operand, locals))
                    ? -2147483647LL - 1
                    : (long long)(1ULL << (w - 1)) * -1;
                if (w >= 64) minv = (long long)(1ULL << 63);
                LLVMValueRef smin = LLVMConstInt(t, (unsigned long long)minv, 1);
                LLVMValueRef ovf = zan_icmp(g->builder, LLVMIntEQ, operand, smin, "ck.neg.ovf");
                emit_runtime_check(g, ovf, expr->loc,
                    "integer overflow in checked operation");
            }
            return LLVMBuildNeg(g->builder, operand, "neg");
        }
        case TK_PLUS:
            return operand;
        case TK_BANG: {
            /* 逻辑非与按位取反指令发射 */
            LLVMTypeRef ot = LLVMTypeOf(operand);
            LLVMTypeKind otk = LLVMGetTypeKind(ot);
            if (otk == LLVMIntegerTypeKind) {
                return zan_icmp(g->builder, LLVMIntEQ, operand,
                    LLVMConstInt(ot, 0, 0), "lnot");
            }
            if (otk == LLVMPointerTypeKind) {
                return zan_icmp(g->builder, LLVMIntEQ, operand,
                    LLVMConstNull(ot), "lnotp");
            }
            return LLVMBuildNot(g->builder, operand, "not");
        }
        case TK_TILDE:
            return LLVMBuildNot(g->builder, operand, "bnot");
        default:
            return operand;
        }
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

