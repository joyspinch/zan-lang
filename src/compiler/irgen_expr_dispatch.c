static LLVMValueRef emit_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals) {
    if (!expr) return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    if (locals) g->current_locals = locals;

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

    case AST_CALL:
        return promote_loaded(g, emit_expr_call(g, expr, locals),
                              infer_expr_type(g, expr, locals));

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
        if (expr->unary.op == TK_BANG)
            return emit_expr(g, expr->unary.operand, locals);
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
                    if (st->kind == TYPE_STRING || tt->kind == TYPE_STRING) {
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
        if (expr->type_test.is_not)
            test = LLVMBuildNot(g->builder, test, "is.not");
        /* 模式匹配变量 (x is T v)：类型匹配成功时在当前作用域注册变量 v */
        if (expr->type_test.var_name.len > 0) {
            zan_type_t *bt = expr->type_test.type
                ? resolve_type_ctx(g, expr->type_test.type) : st;
            if (!bt) bt = g->binder->type_object;
            LLVMTypeRef btll = map_type(g, bt);
            LLVMValueRef slot = emit_entry_alloca(g, btll, "pat");
            zan_store_fit(g, LLVMConstNull(btll), slot);
            LLVMValueRef castv = v;
            if (LLVMTypeOf(castv) != btll &&
                LLVMGetTypeKind(btll) == LLVMPointerTypeKind &&
                LLVMGetTypeKind(LLVMTypeOf(castv)) == LLVMPointerTypeKind)
                castv = LLVMBuildBitCast(g->builder, castv, btll, "pat.cast");
            zan_store_fit(g, castv, slot);
            local_add(locals, expr->type_test.var_name, slot, bt);
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
        if (!st_ref || !tt_ref || st->kind == TYPE_STRING ||
            tt->kind == TYPE_STRING || tt->kind == TYPE_OBJECT ||
            tt->kind == TYPE_INTERFACE)
            return emit_expr(g, expr->type_test.expr, locals);
        LLVMValueRef v = emit_expr(g, expr->type_test.expr, locals);
        if (st->kind == TYPE_CLASS && type_is_or_derives(st, tt))
            return v;
        if (st->kind == TYPE_CLASS && !type_is_or_derives(tt, st))
            return v; 
        char tbuf[320];
        int tlen = (int)tt->sym->name.len;
        if (tlen > 319) tlen = 319;
        memcpy(tbuf, tt->sym->name.str, (size_t)tlen);
        tbuf[tlen] = 0;
        LLVMValueRef ok = emit_runtime_is_check(g, v, tbuf);
        return LLVMBuildSelect(g->builder, ok, v,
            LLVMConstNull(LLVMTypeOf(v)), "as.res");
    }

    case AST_CAST_EXPR:
        return emit_expr_cast_expr(g, expr, locals);

    case AST_SIZEOF_EXPR: {
        zan_type_t *t = resolve_type_ctx(g, expr->cast.type);
        LLVMTypeRef mt = t ? map_type(g, t) : NULL;
        if (mt && LLVMGetTypeKind(mt) != LLVMVoidTypeKind)
            return LLVMSizeOf(mt);
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 8, 0);
    }

    case AST_TYPEOF_EXPR: {
        /* typeof(T): 获取类型的静态反射元数据记录 */
        zan_type_t *t = resolve_type_ctx(g, expr->cast.type);
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
