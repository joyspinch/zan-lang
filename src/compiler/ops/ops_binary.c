LLVMValueRef emit_expr_binary(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* 短路逻辑运算符（&& / ||）：左操作数满足短路条件时不求值右操作数 */
        if (expr->binary.op == TK_AMP_AMP || expr->binary.op == TK_PIPE_PIPE) {
            bool is_and = (expr->binary.op == TK_AMP_AMP);
            LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
            LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));

            LLVMValueRef lval = emit_expr(g, expr->binary.left, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(lval)) != LLVMIntegerTypeKind ||
                LLVMGetIntTypeWidth(LLVMTypeOf(lval)) != 1) {
                lval = zan_tobool(g->builder, lval, "tobool");
            }
            LLVMBasicBlockRef left_bb = LLVMGetInsertBlock(g->builder);
            LLVMBasicBlockRef rhs_bb  = LLVMAppendBasicBlockInContext(g->ctx, fn, "sc.rhs");
            LLVMBasicBlockRef merge   = LLVMAppendBasicBlockInContext(g->ctx, fn, "sc.end");
            /* 逻辑与/或短路求值控制流生成 */
            if (is_and)
                LLVMBuildCondBr(g->builder, lval, rhs_bb, merge);
            else
                LLVMBuildCondBr(g->builder, lval, merge, rhs_bb);

            LLVMPositionBuilderAtEnd(g->builder, rhs_bb);
            LLVMValueRef rval = emit_expr(g, expr->binary.right, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(rval)) != LLVMIntegerTypeKind ||
                LLVMGetIntTypeWidth(LLVMTypeOf(rval)) != 1) {
                rval = zan_tobool(g->builder, rval, "tobool");
            }
            LLVMBasicBlockRef rhs_end = LLVMGetInsertBlock(g->builder);
            LLVMBuildBr(g->builder, merge);

            LLVMPositionBuilderAtEnd(g->builder, merge);
            LLVMValueRef phi = LLVMBuildPhi(g->builder, i1, "sc");
            /* 逻辑与/或短路常量折叠优化 */
            LLVMValueRef sc_const = LLVMConstInt(i1, is_and ? 0 : 1, 0);
            LLVMValueRef vals[] = { sc_const, rval };
            LLVMBasicBlockRef bbs[] = { left_bb, rhs_end };
            LLVMAddIncoming(phi, vals, bbs, 2);
            return phi;
        }

        /* 3+ 字符串连续拼接扁平化优化：合并单次分配 */
        if (expr->binary.op == TK_PLUS && is_str_concat_node(g, expr, locals) &&
            (is_str_concat_node(g, expr->binary.left, locals) ||
             is_str_concat_node(g, expr->binary.right, locals))) {
            return emit_str_concat_n(g, expr, locals);
        }

        /* ?? 短路求值：右支只在左支为 null 时求值（C# 语义）。旧实现对两臂
         * 无条件求值后再 select——带副作用的右支（日志/计数/惰性初始化）被
         * 错误触发，是 skill 二.2 禁止的 select 死臂形态（TASKS.md A515）。 */
        if (expr->binary.op == TK_QUESTION_QUESTION) {
            LLVMValueRef coal = emit_null_coalesce(g, expr, locals);
            if (coal) return coal;
        }

        LLVMValueRef left = emit_expr(g, expr->binary.left, locals);
        LLVMValueRef right = emit_expr(g, expr->binary.right, locals);

        {
            zan_type_t *ltype = infer_expr_type(g, expr->binary.left, locals);
            /* 与 null 比较保留引用指针判空，防止递归调用重载操作符 */
            int null_cmp = expr->binary.left->kind == AST_NULL_LITERAL ||
                           expr->binary.right->kind == AST_NULL_LITERAL;
            if (!null_cmp && ltype && ltype->sym &&
                (ltype->kind == TYPE_CLASS || ltype->kind == TYPE_STRUCT)) {
                const char *op_name = NULL;
                switch (expr->binary.op) {
                case TK_PLUS:       op_name = "op_add"; break;
                case TK_MINUS:      op_name = "op_sub"; break;
                case TK_STAR:       op_name = "op_mul"; break;
                case TK_SLASH:      op_name = "op_div"; break;
                case TK_PERCENT:    op_name = "op_mod"; break;
                case TK_EQ_EQ:      op_name = "op_eq"; break;
                case TK_BANG_EQ:    op_name = "op_neq"; break;
                case TK_LESS:       op_name = "op_lt"; break;
                case TK_GREATER:    op_name = "op_gt"; break;
                case TK_LESS_EQ:    op_name = "op_le"; break;
                case TK_GREATER_EQ: op_name = "op_ge"; break;
                default: break;
                }
                if (op_name) {
                    /* 在类中解析重载的自定义运算符方法 */
                    zan_istr_t op_istr = { (char *)op_name, (int)strlen(op_name) };
                    zan_ast_node_t *op_probe = zan_ast_new(g->arena, AST_CALL,
                                                           expr->loc);
                    op_probe->call.callee = NULL;
                    zan_ast_list_init(&op_probe->call.args);
                    zan_ast_list_init(&op_probe->call.type_args);
                    zan_ast_list_push(&op_probe->call.args, expr->binary.right,
                                      g->arena);
                    zan_symbol_t *op_sym = resolve_op_overload(g, ltype->sym,
                                                               op_istr, op_probe,
                                                               locals);
                    if (op_sym) {
                        for (int fi = irgen_find_function(g, op_sym); fi >= 0; fi = -1) {
                            if (g->functions[fi].sym == op_sym) {
                                /* 调用前将实参类型适配转换至声明形参类型 */
                                LLVMValueRef cargs[2] = { left, right };
                                /* 非静态运算符绑定隐式 this 接收者 */
                                bool op_is_static =
                                    (op_sym->modifiers & MOD_STATIC) != 0;
                                /* 接收者与实参既可能是"值"，也可能是"槽位指针"
                                 * （new 结构体表达式返回的是槽位地址）。常规调用
                                 * 走 coerce_args_to_params 会补这次加载，运算符
                                 * 路径没有——按值传参就会把指针塞进调用里，
                                 * LLVM 校验直接失败（Call parameter type does
                                 * not match function signature）。 */
                                LLVMTypeRef recv_t = map_type(g, ltype);
                                if (LLVMGetTypeKind(LLVMTypeOf(cargs[0])) ==
                                        LLVMPointerTypeKind &&
                                    LLVMGetTypeKind(recv_t) == LLVMStructTypeKind) {
                                    cargs[0] = LLVMBuildLoad2(g->builder, recv_t,
                                                              cargs[0], "op.recv");
                                }
                                if (!op_is_static &&
                                    LLVMGetTypeKind(LLVMTypeOf(cargs[0])) == LLVMStructTypeKind) {
                                    LLVMValueRef rslot = emit_entry_alloca(g,
                                        LLVMTypeOf(cargs[0]), "opb.recv");
                                    LLVMBuildStore(g->builder, cargs[0], rslot);
                                    cargs[0] = rslot;
                                }
                                /* 通过泛型特化具体方法发射调用 */
                                LLVMTypeRef mft = g->functions[fi].fn_type;
                                LLVMValueRef mfn = route_generic_method(g, ltype,
                                    op_sym, g->functions[fi].fn, mft, &mft);
                                zan_type_t *p1 = method_param_type_at(g, op_sym, 1,
                                    expr, expr->binary.left, locals);
                                bool op_generic = p1 && type_mentions_tp(p1);
                                if (p1) p1 = subst_type_param_deep(g, p1, ltype);
                                if (p1 && !op_generic) {
                                    LLVMTypeRef p1t = map_type(g, p1);
                                    if (LLVMGetTypeKind(p1t) == LLVMStructTypeKind &&
                                        LLVMGetTypeKind(LLVMTypeOf(cargs[1])) == LLVMPointerTypeKind) {
                                        cargs[1] = LLVMBuildLoad2(g->builder, p1t,
                                                                  cargs[1], "op.sval");
                                    }
                                    if (LLVMTypeOf(cargs[1]) != p1t)
                                        cargs[1] = coerce_int_to(g, cargs[1], p1t);
                                }
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "opcall";
                                LLVMValueRef opres = zan_call2(g->builder,
                                    mft, mfn, cargs, 2, cn);
                                /* 原地委托组合赋值 (E += obj.OnX)：包装为委托实例后执行组合操作 */
                                /* 运算符重载实参入栈发射 */
                                if (expr_yields_delegate_value(g, expr->binary.right, locals))
                                    emit_closure_release(g, right);
                                else
                                    emit_release_owned_call_temp(g, expr->binary.right, right, locals);
                                emit_release_owned_call_temp(g, expr->binary.left, left, locals);
                                return opres;
                            }
                        }
                    }
                }
            }
        }

        /* 操作数包含可空类型时提升运算；字符串则走拼接逻辑 */
        if ((llvm_is_nullable(LLVMTypeOf(left)) ||
             llvm_is_nullable(LLVMTypeOf(right))) &&
            !is_string_expr(g, expr->binary.left, locals) &&
            !is_string_expr(g, expr->binary.right, locals))
            return emit_nullable_binary(g, expr, left, right, locals);

        /* 128 位定点数 decimal 二元运算与比较 */
        bool has_str_op = is_string_expr(g, expr->binary.left, locals) ||
                          is_string_expr(g, expr->binary.right, locals);
        if ((LLVMTypeOf(left) == get_decimal_type(g) ||
             LLVMTypeOf(right) == get_decimal_type(g)) && !has_str_op) {
            return emit_decimal_binary(g, expr, left, right, locals);
        }

        /* 结构体逐字段相等性比较 */
        if ((expr->binary.op == TK_EQ_EQ || expr->binary.op == TK_BANG_EQ) &&
            LLVMGetTypeKind(LLVMTypeOf(left)) == LLVMStructTypeKind &&
            LLVMTypeOf(left) == LLVMTypeOf(right)) {
            LLVMTypeRef st = LLVMTypeOf(left);
            unsigned nf = LLVMCountStructElementTypes(st);
            LLVMValueRef acc = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0);
            for (unsigned fi = 0; fi < nf; fi++) {
                LLVMValueRef lf = LLVMBuildExtractValue(g->builder, left, fi, "sq.l");
                LLVMValueRef rf = LLVMBuildExtractValue(g->builder, right, fi, "sq.r");
                LLVMTypeKind fk = LLVMGetTypeKind(LLVMTypeOf(lf));
                LLVMValueRef eq;
                if (fk == LLVMDoubleTypeKind || fk == LLVMFloatTypeKind)
                    eq = LLVMBuildFCmp(g->builder, LLVMRealOEQ, lf, rf, "sq.f");
                else if (fk == LLVMPointerTypeKind)
                    eq = LLVMBuildICmp(g->builder, LLVMIntEQ,
                        LLVMBuildPtrToInt(g->builder, lf, LLVMInt64TypeInContext(g->ctx), "sq.lp"),
                        LLVMBuildPtrToInt(g->builder, rf, LLVMInt64TypeInContext(g->ctx), "sq.rp"),
                        "sq.p");
                else
                    eq = zan_icmp(g->builder, LLVMIntEQ, lf, rf, "sq.i");
                acc = LLVMBuildAnd(g->builder, acc, eq, "sq.and");
            }
            if (expr->binary.op == TK_BANG_EQ)
                acc = LLVMBuildNot(g->builder, acc, "sq.not");
            return acc;
        }

        bool both_ptr =
            LLVMGetTypeKind(LLVMTypeOf(left)) == LLVMPointerTypeKind &&
            LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMPointerTypeKind;

        /* 委托判等：比对函数指针与绑定目标实例指针 */
        if ((expr->binary.op == TK_EQ_EQ || expr->binary.op == TK_BANG_EQ) &&
            both_ptr) {
            zan_type_t *lt = infer_expr_type(g, expr->binary.left, locals);
            zan_type_t *rt = infer_expr_type(g, expr->binary.right, locals);
            if (lt && rt && lt->kind == TYPE_DELEGATE && rt->kind == TYPE_DELEGATE) {
                LLVMValueRef eq = emit_delegate_equals(g, left, right);
                return expr->binary.op == TK_BANG_EQ
                    ? LLVMBuildNot(g->builder, eq, "dneq") : eq;
            }
        }
        bool str_operand = is_string_expr(g, expr->binary.left, locals) ||
                           is_string_expr(g, expr->binary.right, locals);

        /* 字符串拼接 (a + b)：操作数含 string 时发射 String.Concat */
        if (expr->binary.op == TK_PLUS && str_operand) {
            LLVMValueRef ls = emit_to_cstr_of(g, left, expr->binary.left, locals);
            LLVMValueRef rs = emit_to_cstr_of(g, right, expr->binary.right, locals);
            LLVMValueRef out = emit_str_concat(g, ls, rs);
            if (!is_string_expr(g, expr->binary.left, locals) ||
                expr_yields_owned_rc_value(g, expr->binary.left, locals)) {
                emit_string_release(g, ls);
            }
            if (!is_string_expr(g, expr->binary.right, locals) ||
                expr_yields_owned_rc_value(g, expr->binary.right, locals)) {
                emit_string_release(g, rs);
            }
            return out;
        }

        /* 托管字符串内容判等 */
        if ((expr->binary.op == TK_EQ_EQ || expr->binary.op == TK_BANG_EQ) &&
            both_ptr && str_operand &&
            expr->binary.left->kind != AST_NULL_LITERAL &&
            expr->binary.right->kind != AST_NULL_LITERAL) {
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef lc = emit_str_nonnull(g, left);
            LLVMValueRef rc = emit_str_nonnull(g, right);
            /* 带长度的托管字符串字典序比对 */
            LLVMValueRef ocmp = get_str_ordinal_cmp_fn(g, expr->loc);
            LLVMValueRef r = zan_call2(g->builder,
                LLVMGlobalGetValueType(ocmp), ocmp,
                (LLVMValueRef[]){ lc, rc }, 2, "scmp");
            LLVMValueRef seq = zan_icmp(g->builder,
                expr->binary.op == TK_EQ_EQ ? LLVMIntEQ : LLVMIntNE,
                r, LLVMConstInt(i32t, 0, 0), "seq");
            if (is_string_expr(g, expr->binary.left, locals) &&
                expr_yields_owned_rc_value(g, expr->binary.left, locals)) {
                emit_string_release(g, left);
            }
            if (is_string_expr(g, expr->binary.right, locals) &&
                expr_yields_owned_rc_value(g, expr->binary.right, locals)) {
                emit_string_release(g, right);
            }
            return seq;
        }

        /* 字符串大小比较：将 < / <= / > / >= 路由至 strcmp 字符序比对 */
        if ((expr->binary.op == TK_LESS || expr->binary.op == TK_LESS_EQ ||
             expr->binary.op == TK_GREATER || expr->binary.op == TK_GREATER_EQ) &&
            both_ptr && str_operand &&
            expr->binary.left->kind != AST_NULL_LITERAL &&
            expr->binary.right->kind != AST_NULL_LITERAL) {
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef lc = emit_str_nonnull(g, left);
            LLVMValueRef rc = emit_str_nonnull(g, right);
            LLVMValueRef ocmp = get_str_ordinal_cmp_fn(g, expr->loc);
            LLVMValueRef r = zan_call2(g->builder,
                LLVMGlobalGetValueType(ocmp), ocmp,
                (LLVMValueRef[]){ lc, rc }, 2, "scmp");
            LLVMIntPredicate pred =
                expr->binary.op == TK_LESS       ? LLVMIntSLT :
                expr->binary.op == TK_LESS_EQ    ? LLVMIntSLE :
                expr->binary.op == TK_GREATER    ? LLVMIntSGT :
                                                   LLVMIntSGE;
            LLVMValueRef sord = zan_icmp(g->builder, pred,
                r, LLVMConstInt(i32t, 0, 0), "sord");
            if (is_string_expr(g, expr->binary.left, locals) &&
                expr_yields_owned_rc_value(g, expr->binary.left, locals)) {
                emit_string_release(g, left);
            }
            if (is_string_expr(g, expr->binary.right, locals) &&
                expr_yields_owned_rc_value(g, expr->binary.right, locals)) {
                emit_string_release(g, right);
            }
            return sord;
        }

        /* 指针与空值相等性判断 (== null) */
        if ((expr->binary.op == TK_EQ_EQ || expr->binary.op == TK_BANG_EQ) && both_ptr) {
            LLVMValueRef rcast = right;
            if (LLVMTypeOf(right) != LLVMTypeOf(left))
                rcast = LLVMBuildBitCast(g->builder, right, LLVMTypeOf(left), "pcast");
            LLVMValueRef pcmp = zan_icmp(g->builder,
                expr->binary.op == TK_EQ_EQ ? LLVMIntEQ : LLVMIntNE,
                left, rcast, "pcmp");
            emit_release_owned_call_temp(g, expr->binary.left, left, locals);
            emit_release_owned_call_temp(g, expr->binary.right, right, locals);
            return pcmp;
        }

        /* 值类型与 null 的比较：泛型形参 T 在特化成结构体/数值等值类型后，
         * 值永远不可能为 null，直接比较会生成「聚合值 vs 空指针」的非法 IR
         * （LLVM 校验失败：Both operands to ICmp instruction are not of the
         * same type）。这里按「值类型不是 null」折叠成常量。 */
        if ((expr->binary.op == TK_EQ_EQ || expr->binary.op == TK_BANG_EQ) &&
            !both_ptr) {
            bool l_null = LLVMGetTypeKind(LLVMTypeOf(left)) == LLVMPointerTypeKind &&
                          LLVMIsNull(left);
            bool r_null = LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMPointerTypeKind &&
                          LLVMIsNull(right);
            if (l_null != r_null) {
                LLVMValueRef val = l_null ? right : left;
                LLVMTypeRef vt = LLVMTypeOf(val);
                LLVMTypeKind vk = LLVMGetTypeKind(vt);
                bool value_kind = vk == LLVMStructTypeKind ||
                                  vk == LLVMIntegerTypeKind ||
                                  vk == LLVMDoubleTypeKind ||
                                  vk == LLVMFloatTypeKind;
                /* 可空值类型走 zan.nullable.* 聚合体，其「是否为 null」由
                 * has-value 标记决定，不能按值类型折叠 */
                if (value_kind &&
                    !(vk == LLVMStructTypeKind && llvm_is_nullable(vt))) {
                    emit_release_owned_call_temp(g, expr->binary.left, left, locals);
                    emit_release_owned_call_temp(g, expr->binary.right, right, locals);
                    return LLVMConstInt(LLVMInt1TypeInContext(g->ctx),
                                        expr->binary.op == TK_BANG_EQ ? 1 : 0, 0);
                }
            }
        }

        return emit_binary_op_values(g, expr, left, right, locals);
}

/* 判断表达式运行时是否可能包含 string 对象 */
bool recv_is_stringlike(zan_irgen_t *g, zan_ast_node_t *e,
                               local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    if (!t) return true;
    return t->kind == TYPE_STRING || t->kind == TYPE_OBJECT ||
           t->kind == TYPE_ERROR || t->kind == TYPE_TYPE_PARAM;
}

/* 移位计数按左操作数位宽取模 (1 << 33 规范化为 1 << 1) */
static LLVMValueRef mask_shift_count(zan_irgen_t *g, zan_ast_node_t *lhs,
                                     LLVMValueRef count, local_scope_t *locals) {
    if (LLVMGetTypeKind(LLVMTypeOf(count)) != LLVMIntegerTypeKind)
        return count;
    unsigned bits = 64;
    zan_type_t *lt = infer_expr_type(g, lhs, locals);
    if (lt) {
        switch (lt->kind) {
        case TYPE_BYTE: case TYPE_SBYTE:
        case TYPE_SHORT: case TYPE_USHORT:
        case TYPE_CHAR:
        case TYPE_INT: case TYPE_UINT:
            bits = 32;
            break;
        default:
            break;
        }
    }
    return zan_and(g->builder, count,
                   LLVMConstInt(LLVMTypeOf(count), bits - 1, 0), "sh.mask");
}

static LLVMValueRef emit_binary_op_values(zan_irgen_t *g, zan_ast_node_t *expr,
                                          LLVMValueRef left, LLVMValueRef right,
                                          local_scope_t *locals) {
        /* 比较类的 int/指针 混比必须当场诊断：checker 在开放泛型 lambda
         * 上下文里判不出成员类型（特化后才知道，E2045 被静默跳过），裸
         * icmp i32 vs ptr 会被 LLVM verify 整模块击毙（E3090 报在 0:0，
         * 比编译错难懂得多——B-ID59 的 diag_orm_where_int_string 形状）。 */
        {
            int op = expr->binary.op;
            bool cmp = op == TK_EQ_EQ || op == TK_BANG_EQ || op == TK_LESS ||
                       op == TK_GREATER || op == TK_LESS_EQ ||
                       op == TK_GREATER_EQ;
            if (cmp) {
                LLVMTypeKind lk = LLVMGetTypeKind(LLVMTypeOf(left));
                LLVMTypeKind rk = LLVMGetTypeKind(LLVMTypeOf(right));
                bool li = lk == LLVMIntegerTypeKind;
                bool ri = rk == LLVMIntegerTypeKind;
                bool lp = lk == LLVMPointerTypeKind;
                bool rp = rk == LLVMPointerTypeKind;
                if ((li && rp) || (lp && ri)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 2045,
                        "cannot compare a %s operand and a %s operand -- "
                        "convert one side explicitly",
                        li ? "numeric" : "reference",
                        ri ? "numeric" : "reference");
                    return LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
                }
            }
        }

        /* 混合位宽整数与浮点数类型隐式提升转换 */
        coerce_int_pair(g, &left, &right);

        /* A510：定型为 32 位 int/uint 的算术/移位/位运算按 32 位环绕（C#
         * unchecked 口径）——曾一律 64 位寄存器，表达式级溢出不环绕
         * （int.MaxValue+1 打出 2147483648、0u-1u 打出 64 位回绕、-8>>>1
         * 按 64 位移）。比较运算定型是 bool 不走这条；臂内层的 int 表达式
         * 已各自按 i32 包回，这里只截对齐后的公共位宽。 */
        {
            zan_type_t *bty = infer_expr_type(g, expr, locals);
            LLVMTypeRef lty = LLVMTypeOf(left);
            if (bty && (bty->kind == TYPE_INT || bty->kind == TYPE_UINT) &&
                LLVMGetTypeKind(lty) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(lty) > 32) {
                LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                left = LLVMBuildTrunc(g->builder, left, i32t, "n32.l");
                right = LLVMBuildTrunc(g->builder, right, i32t, "n32.r");
            }
        }

        /* 双操作数类型对齐后根据统一类型发射相应指令 */
        LLVMTypeRef left_type = LLVMTypeOf(left);
        bool is_float = (LLVMGetTypeKind(left_type) == LLVMDoubleTypeKind ||
                         LLVMGetTypeKind(left_type) == LLVMFloatTypeKind ||
                         LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMDoubleTypeKind ||
                         LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMFloatTypeKind);

        /* 无符号数选择 udiv/urem/lshr/icmp 指令 */
        bool is_unsigned = !is_float &&
            (expr_is_ulong(g, expr->binary.left, locals) ||
             expr_is_ulong(g, expr->binary.right, locals) ||
             expr_is_uint(g, expr->binary.left, locals) ||
             expr_is_uint(g, expr->binary.right, locals));

        /* checked 上下文下的整数溢出捕获陷阱 */
        int check_ctx = expr->binary.checked
            ? (expr->binary.checked > 0 ? 1 : -1)
            : (g->irgen_checked_depth > 0 ? 1 : 0);
        bool want_overflow_check = check_ctx > 0 && !is_float;
        LLVMTypeRef res_ty = LLVMTypeOf(left);
        if (want_overflow_check &&
            !(LLVMGetTypeKind(res_ty) == LLVMIntegerTypeKind &&
              LLVMGetIntTypeWidth(res_ty) > 1))
            want_overflow_check = false;

        switch (expr->binary.op) {
        case TK_PLUS:
            if (is_float) return LLVMBuildFAdd(g->builder, left, right, "add");
            if (want_overflow_check)
                return emit_checked_int_arith(g, expr, left, right,
                                              CHK_ADD, locals);
            return zan_add(g->builder, left, right, "add");
        case TK_MINUS:
            if (is_float) return LLVMBuildFSub(g->builder, left, right, "sub");
            if (want_overflow_check)
                return emit_checked_int_arith(g, expr, left, right,
                                              CHK_SUB, locals);
            return zan_sub(g->builder, left, right, "sub");
        case TK_STAR:
            if (is_float) return LLVMBuildFMul(g->builder, left, right, "mul");
            if (want_overflow_check)
                return emit_checked_int_arith(g, expr, left, right,
                                              CHK_MUL, locals);
            return zan_mul(g->builder, left, right, "mul");
        case TK_SLASH:
            if (is_float) return LLVMBuildFDiv(g->builder, left, right, "div");
            {
                /* 整数除法软路径零除与溢出防护 */
                LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(right), 0, 0);
                LLVMValueRef one = LLVMConstInt(LLVMTypeOf(right), 1, 0);
                LLVMValueRef is_zero = zan_icmp(g->builder, LLVMIntEQ, right, zero, "divz");
                emit_runtime_check(g, is_zero, expr->loc, "division by zero");
                LLVMValueRef unsafe = is_zero;
                if (!is_unsigned) {
                    unsigned w = LLVMGetIntTypeWidth(LLVMTypeOf(left));
                    LLVMValueRef smin = LLVMConstInt(LLVMTypeOf(left),
                        (unsigned long long)1 << (w - 1), 1);
                    LLVMValueRef minus_one = LLVMConstAllOnes(LLVMTypeOf(right));
                    LLVMValueRef is_min = zan_icmp(g->builder, LLVMIntEQ, left, smin, "divmin");
                    LLVMValueRef is_m1 = zan_icmp(g->builder, LLVMIntEQ, right, minus_one, "divm1");
                    unsafe = LLVMBuildOr(g->builder, unsafe,
                        LLVMBuildAnd(g->builder, is_min, is_m1, "divovf"),
                        "divunsafe");
                    if (want_overflow_check) {
                        /* checked 下 MIN / -1 抛溢出（B-ID76；零除另有诊断） */
                        emit_runtime_check(g,
                            LLVMBuildAnd(g->builder, is_min, is_m1, "divovf.ck"),
                            expr->loc, "integer overflow in checked operation");
                    }
                }
                LLVMValueRef divisor = LLVMBuildSelect(g->builder, unsafe,
                    one, right, "divz.safe");
                LLVMValueRef div_res = is_unsigned ? zan_udiv(g->builder, left, divisor, "div")
                                                   : zan_sdiv(g->builder, left, divisor, "div");
                return LLVMBuildSelect(g->builder, unsafe, zero, div_res, "divz.res");
            }
        case TK_PERCENT:
            if (is_float) return LLVMBuildFRem(g->builder, left, right, "rem");
            {
                LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(right), 0, 0);
                LLVMValueRef one = LLVMConstInt(LLVMTypeOf(right), 1, 0);
                LLVMValueRef is_zero = zan_icmp(g->builder, LLVMIntEQ, right, zero, "remz");
                emit_runtime_check(g, is_zero, expr->loc, "division by zero (modulo)");
                LLVMValueRef unsafe = is_zero;
                if (!is_unsigned) {
                    unsigned w = LLVMGetIntTypeWidth(LLVMTypeOf(left));
                    LLVMValueRef smin = LLVMConstInt(LLVMTypeOf(left),
                        (unsigned long long)1 << (w - 1), 1);
                    LLVMValueRef minus_one = LLVMConstAllOnes(LLVMTypeOf(right));
                    LLVMValueRef is_min = zan_icmp(g->builder, LLVMIntEQ, left, smin, "remmin");
                    LLVMValueRef is_m1 = zan_icmp(g->builder, LLVMIntEQ, right, minus_one, "remm1");
                    unsafe = LLVMBuildOr(g->builder, unsafe,
                        LLVMBuildAnd(g->builder, is_min, is_m1, "removf"),
                        "remunsafe");
                }
                LLVMValueRef divisor = LLVMBuildSelect(g->builder, unsafe,
                    one, right, "remz.safe");
                LLVMValueRef rem_res = is_unsigned ? zan_urem(g->builder, left, divisor, "rem")
                                                   : zan_srem(g->builder, left, divisor, "rem");
                return LLVMBuildSelect(g->builder, unsafe, zero, rem_res, "remz.res");
            }
        case TK_AMP:
            return zan_and(g->builder, left, right, "and");
        case TK_PIPE:
            return zan_or(g->builder, left, right, "or");
        case TK_CARET:
            return zan_xor(g->builder, left, right, "xor");
        case TK_LESS_LESS:
            right = mask_shift_count(g, expr->binary.left, right, locals);
            return zan_shl(g->builder, left, right, "shl");
        case TK_GREATER_GREATER:
            right = mask_shift_count(g, expr->binary.left, right, locals);
            return is_unsigned ? zan_lshr(g->builder, left, right, "shr")
                               : zan_ashr(g->builder, left, right, "shr");
        case TK_GREATER_GREATER_GREATER:
            /* 无符号逻辑右移指令发射 (lshr) */
            right = mask_shift_count(g, expr->binary.left, right, locals);
            return zan_lshr(g->builder, left, right, "ushr");
        case TK_EQ_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOEQ, left, right, "eq")
                            : zan_icmp(g->builder, LLVMIntEQ, left, right, "eq");
        case TK_BANG_EQ:
            /* 浮点数 NaN 比较符合 IEEE 规范 */
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealUNE, left, right, "ne")
                            : zan_icmp(g->builder, LLVMIntNE, left, right, "ne");
        case TK_LESS:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOLT, left, right, "lt")
                            : zan_icmp(g->builder, is_unsigned ? LLVMIntULT : LLVMIntSLT, left, right, "lt");
        case TK_GREATER:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOGT, left, right, "gt")
                            : zan_icmp(g->builder, is_unsigned ? LLVMIntUGT : LLVMIntSGT, left, right, "gt");
        case TK_LESS_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOLE, left, right, "le")
                            : zan_icmp(g->builder, is_unsigned ? LLVMIntULE : LLVMIntSLE, left, right, "le");
        case TK_GREATER_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOGE, left, right, "ge")
                            : zan_icmp(g->builder, is_unsigned ? LLVMIntUGE : LLVMIntSGE, left, right, "ge");
        case TK_QUESTION_QUESTION: {
            LLVMValueRef is_null;
            if (LLVMGetTypeKind(left_type) == LLVMPointerTypeKind) {
                LLVMValueRef null_ptr = LLVMConstNull(left_type);
                is_null = zan_icmp(g->builder, LLVMIntEQ, left, null_ptr, "isnull");
            } else {
                is_null = zan_icmp(g->builder, LLVMIntEQ, left,
                    LLVMConstInt(left_type, 0, 0), "isnull");
            }
            LLVMValueRef coal_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef use_right = LLVMAppendBasicBlockInContext(g->ctx, coal_fn, "coal.r");
            LLVMBasicBlockRef merge = LLVMAppendBasicBlockInContext(g->ctx, coal_fn, "coal.m");
            LLVMBasicBlockRef left_bb = LLVMGetInsertBlock(g->builder);
            LLVMBuildCondBr(g->builder, is_null, use_right, merge);
            LLVMPositionBuilderAtEnd(g->builder, use_right);
            LLVMBuildBr(g->builder, merge);
            LLVMPositionBuilderAtEnd(g->builder, merge);
            LLVMValueRef phi = LLVMBuildPhi(g->builder, left_type, "coal");
            LLVMValueRef vals[] = { left, right };
            LLVMBasicBlockRef bbs[] = { left_bb, use_right };
            LLVMAddIncoming(phi, vals, bbs, 2);
            return phi;
        }
        default:
            return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
        }
}

/* checked 溢出检测整数算术实现 */
static LLVMValueRef emit_checked_int_arith(zan_irgen_t *g, zan_ast_node_t *expr,
                                           LLVMValueRef left, LLVMValueRef right,
                                           int kind, local_scope_t *locals) {
    LLVMTypeRef ty = LLVMTypeOf(left);
    unsigned bits = LLVMGetIntTypeWidth(ty);
    bool is_u64 = expr_is_ulong(g, expr->binary.left, locals) ||
                  expr_is_ulong(g, expr->binary.right, locals);
    /* uint 运算溢出范围检查 */
    bool is_u32 = !is_u64 &&
        (expr_is_uint(g, expr->binary.left, locals) ||
         expr_is_uint(g, expr->binary.right, locals)) &&
        !expr_is_longish(g, expr->binary.left, locals) &&
        !expr_is_longish(g, expr->binary.right, locals);
    bool is_unsigned = is_u64 || is_u32;
    /* 字面量整数运算在 i64 拓宽精度下求值 */
    bool clamp32 = !is_unsigned && bits > 32 &&
                   expr_is_int32(g, expr->binary.left, locals) &&
                   expr_is_int32(g, expr->binary.right, locals);
    LLVMValueRef res, ovf;
    if (is_u32) {
        /* uint 算术运算中间值类型适配与截断 */
        LLVMTypeRef i32t = LLVMIntTypeInContext(g->ctx, 32);
        LLVMTypeRef i64t = LLVMIntTypeInContext(g->ctx, 64);
        LLVMValueRef l64 = LLVMBuildZExt(g->builder,
            LLVMBuildTrunc(g->builder, left, i32t, "ck.u32l"), i64t, "ck.u32lz");
        LLVMValueRef r64 = LLVMBuildZExt(g->builder,
            LLVMBuildTrunc(g->builder, right, i32t, "ck.u32r"), i64t, "ck.u32rz");
        LLVMValueRef u32max = LLVMConstInt(i64t, 0xFFFFFFFFULL, 0);
        if (kind == CHK_ADD) {
            res = zan_add(g->builder, l64, r64, "ck.u32.add");
            ovf = zan_icmp(g->builder, LLVMIntUGT, res, u32max, "ck.u32.ovf");
        } else if (kind == CHK_SUB) {
            res = zan_sub(g->builder, l64, r64, "ck.u32.sub");
            ovf = zan_icmp(g->builder, LLVMIntSLT, res,
                LLVMConstInt(i64t, 0, 0), "ck.u32.ovf");
        } else {
            res = zan_mul(g->builder, l64, r64, "ck.u32.mul");
            ovf = zan_icmp(g->builder, LLVMIntUGT, res, u32max, "ck.u32.ovf");
        }
        res = LLVMBuildTrunc(g->builder, res, ty, "ck.u32.res");
    } else if (kind == CHK_ADD) {
        res = zan_add(g->builder, left, right, "ck.add");
        if (is_unsigned)
            ovf = zan_icmp(g->builder, LLVMIntULT, res, left, "ck.add.ovf");
        else if (clamp32) {
            LLVMValueRef lo = LLVMConstInt(ty, -(1LL << 31), 1);
            LLVMValueRef hi = LLVMConstInt(ty, (1LL << 31) - 1, 1);
            LLVMValueRef under = zan_icmp(g->builder, LLVMIntSLT, res, lo,
                "ck.lo");
            LLVMValueRef over = zan_icmp(g->builder, LLVMIntSGT, res, hi,
                "ck.hi");
            ovf = zan_or(g->builder, under, over, "ck.add.ovf");
        }
        else {
            /* 有符号加法溢出检测：同号相加符号突变判定 */
            LLVMValueRef lneg = zan_icmp(g->builder, LLVMIntSLT, left,
                LLVMConstInt(ty, 0, 0), "ck.sl");
            LLVMValueRef rneg = zan_icmp(g->builder, LLVMIntSLT, right,
                LLVMConstInt(ty, 0, 0), "ck.sr");
            LLVMValueRef same_sign_lr = zan_icmp(g->builder, LLVMIntEQ,
                lneg, rneg, "ck.sign.lr");
            LLVMValueRef res_neg = zan_icmp(g->builder, LLVMIntSLT, res,
                LLVMConstInt(ty, 0, 0), "ck.sr2");
            LLVMValueRef res_diff_left = zan_xor(g->builder, res_neg, lneg,
                "ck.sign.r");
            ovf = zan_and(g->builder, same_sign_lr, res_diff_left,
                          "ck.add.ovf");
        }
    } else if (kind == CHK_SUB) {
        res = zan_sub(g->builder, left, right, "ck.sub");
        if (is_unsigned)
            ovf = zan_icmp(g->builder, LLVMIntULT, left, right, "ck.sub.ovf");
        else if (clamp32) {
            LLVMValueRef lo = LLVMConstInt(ty, -(1LL << 31), 1);
            LLVMValueRef hi = LLVMConstInt(ty, (1LL << 31) - 1, 1);
            LLVMValueRef under = zan_icmp(g->builder, LLVMIntSLT, res, lo,
                "ck.lo");
            LLVMValueRef over = zan_icmp(g->builder, LLVMIntSGT, res, hi,
                "ck.hi");
            ovf = zan_or(g->builder, under, over, "ck.sub.ovf");
        }
        else {
            /* 有符号减法溢出检测：异号相减符号突变判定 */
            LLVMValueRef lneg = zan_icmp(g->builder, LLVMIntSLT, left,
                LLVMConstInt(ty, 0, 0), "ck.sl");
            LLVMValueRef rneg = zan_icmp(g->builder, LLVMIntSLT, right,
                LLVMConstInt(ty, 0, 0), "ck.sr");
            LLVMValueRef diff_sign = zan_xor(g->builder, lneg, rneg,
                "ck.diff");
            LLVMValueRef res_neg = zan_icmp(g->builder, LLVMIntSLT, res,
                LLVMConstInt(ty, 0, 0), "ck.sr2");
            LLVMValueRef res_diff_left = zan_xor(g->builder, res_neg, lneg,
                "ck.sign.r");
            ovf = zan_and(g->builder, diff_sign, res_diff_left, "ck.sub.ovf");
        }
    } else { /* CHK_MUL */
        if (is_unsigned) {
            /* 乘法溢出检测：反除法校验结果是否回绕 */
            res = zan_mul(g->builder, left, right, "ck.mul");
            LLVMValueRef nonzero = zan_icmp(g->builder, LLVMIntNE, left,
                LLVMConstInt(ty, 0, 0), "ck.nz");
            LLVMValueRef dvs = LLVMBuildSelect(g->builder, nonzero, left,
                LLVMConstInt(ty, 1, 0), "ck.mul.dv");
            LLVMValueRef q = zan_udiv(g->builder, res, dvs, "ck.mul.q");
            LLVMValueRef back = zan_icmp(g->builder, LLVMIntNE, q, right,
                "ck.mul.back");
            ovf = zan_and(g->builder, nonzero, back, "ck.mul.ovf");
        } else {
            /* 双倍位宽乘法溢出校验 */
            LLVMTypeRef wide = LLVMIntTypeInContext(g->ctx, bits * 2);
            LLVMValueRef wl, wr, wp;
            wl = LLVMBuildSExt(g->builder, left, wide, "ck.mul.wl");
            wr = LLVMBuildSExt(g->builder, right, wide, "ck.mul.wr");
            wp = LLVMBuildMul(g->builder, wl, wr, "ck.mul.wp");
            LLVMValueRef narrow = LLVMBuildTrunc(g->builder, wp, ty, "ck.mul.tr");
            LLVMValueRef back = LLVMBuildSExt(g->builder, narrow, wide,
                "ck.mul.back");
            ovf = zan_icmp(g->builder, LLVMIntNE, wp, back, "ck.mul.ovf");
            res = narrow;
        }
    }
    emit_runtime_check(g, ovf, expr->loc,
        "integer overflow in checked operation");
    (void)locals;
    return res;
}

/* 构造同操作数不同操作符的二元 AST 节点用于降解 */
static zan_ast_node_t *binary_with_op(zan_irgen_t *g, zan_ast_node_t *expr,
                                      int op) {
    zan_ast_node_t *n = zan_ast_new(g->arena, AST_BINARY, expr->loc);
    n->binary.op = op;
    n->binary.left = expr->binary.left;
    n->binary.right = expr->binary.right;
    return n;
}

/* ?? 的短路求值：右支只在左支为 null 时求值（C# 语义，TASKS.md A515）。
 * 旧实现对两臂无条件求值后再 select——带副作用的右支（日志/计数/惰性初始化）
 * 被错误触发，是 skill 二.2 禁止的 select 死臂形态。
 * 只对三种干净形态走惰性路径（指针同型 / 双可空聚合 / 可空左+裸负载同型）；
 * 形态覆盖不了在**发码前**返回 NULL，调用方回退旧的急切两臂路径——
 * 绝不在发码中途放弃，避免悬空块与二次求值。 */
static LLVMValueRef emit_null_coalesce(zan_irgen_t *g, zan_ast_node_t *expr,
                                       local_scope_t *locals) {
    if (!g->current_fn || !expr || !expr->binary.left || !expr->binary.right)
        return NULL;
    zan_type_t *lst = infer_expr_type(g, expr->binary.left, locals);
    zan_type_t *rst = infer_expr_type(g, expr->binary.right, locals);
    if (!lst || !rst || expr->binary.left->kind == AST_NULL_LITERAL ||
        expr->binary.right->kind == AST_NULL_LITERAL)
        return NULL;
    LLVMTypeRef llt_static = map_type(g, lst);
    LLVMTypeRef rlt_static = rst ? map_type(g, rst) : NULL;
    if (!llt_static || !rlt_static) return NULL;
    bool l_nullable = llvm_is_nullable(llt_static) != 0;
    bool l_ptr = LLVMGetTypeKind(llt_static) == LLVMPointerTypeKind;
    if (!l_ptr && !l_nullable) return NULL;

    if (l_ptr) {
        /* 指针左支：右支静态类型必须映射到同一 LLVM 指针型 */
        if (LLVMGetTypeKind(rlt_static) != LLVMPointerTypeKind ||
            rlt_static != llt_static)
            return NULL;
    } else {
        bool r_nullable = llvm_is_nullable(rlt_static) != 0;
        if (r_nullable) {
            /* T? ?? T?：聚合体直取，要求两臂聚合 LLVM 型一致 */
            if (rlt_static != llt_static) return NULL;
        } else {
            /* T? ?? T：结果取负载型，要求右支静态 LLVM 型 == 左负载型 */
            if (!lst->element_type) return NULL;
            LLVMTypeRef payload_ty = map_type(g, lst->element_type);
            if (!payload_ty || payload_ty != rlt_static) return NULL;
        }
    }

    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMValueRef left = emit_expr(g, expr->binary.left, locals);
    LLVMTypeRef lt = LLVMTypeOf(left);
    l_nullable = llvm_is_nullable(lt) != 0;
    LLVMValueRef is_null;
    if (l_nullable)
        is_null = LLVMBuildNot(g->builder, nullable_has_value(g, left),
                               "coal.lnull");
    else
        is_null = zan_icmp(g->builder, LLVMIntEQ, left, LLVMConstNull(lt),
                           "coal.lnull");

    /* p07 批次回归照出的存量缺陷：负载提取必须在 left_bb（分支前）完成
     * ——曾在 merge 块里先发 nullable_get_payload 提取指令再建 phi，触发
     * "PHI nodes not grouped at top of basic block!"（`int y = x ?? 5;`）。 */
    LLVMTypeRef res_ty = lt;
    LLVMValueRef lres = left;
    if (l_nullable) {
        LLVMValueRef lpayload = nullable_get_payload(g, left);
        res_ty = LLVMTypeOf(lpayload);
        lres = lpayload;
    }

    LLVMBasicBlockRef left_bb = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef rhs_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "coal.rhs");
    LLVMBasicBlockRef lhs_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "coal.lhs");
    LLVMBasicBlockRef merge = LLVMAppendBasicBlockInContext(g->ctx, fn, "coal.end");
    LLVMBuildCondBr(g->builder, is_null, rhs_bb, lhs_bb);

    LLVMPositionBuilderAtEnd(g->builder, lhs_bb);
    int left_owned = expr_yields_owned_rc_value(g, expr->binary.left, locals);
    int right_owned = expr_yields_owned_rc_value(g, expr->binary.right, locals);
    zan_type_t *coal_type = infer_expr_type(g, expr, locals);
    if (!coal_type) coal_type = lst;
    if (coal_type && is_rc_managed_type(coal_type) && !left_owned && right_owned) {
        emit_rc_retain_for_type(g, coal_type, lres);
    }
    LLVMBasicBlockRef lhs_end = LLVMGetInsertBlock(g->builder);
    LLVMBuildBr(g->builder, merge);

    LLVMPositionBuilderAtEnd(g->builder, rhs_bb);
    LLVMValueRef right = emit_expr(g, expr->binary.right, locals);
    LLVMBasicBlockRef rhs_end = LLVMGetInsertBlock(g->builder);
    LLVMValueRef rres = right;
    if (l_nullable && LLVMTypeOf(right) != res_ty)
        rres = coerce_int_to(g, right, res_ty);
    if (coal_type && is_rc_managed_type(coal_type) && left_owned && !right_owned) {
        emit_rc_retain_for_type(g, coal_type, rres);
    }
    LLVMBuildBr(g->builder, merge);

    LLVMPositionBuilderAtEnd(g->builder, merge);
    LLVMValueRef phi = LLVMBuildPhi(g->builder, res_ty, "coal");
    LLVMValueRef vals[] = { lres, rres };
    LLVMBasicBlockRef bbs[] = { lhs_end, rhs_end };
    LLVMAddIncoming(phi, vals, bbs, 2);
    return phi;
}

static LLVMValueRef emit_nullable_binary(zan_irgen_t *g, zan_ast_node_t *expr,
                                         LLVMValueRef left, LLVMValueRef right,
                                         local_scope_t *locals) {
    LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
    LLVMValueRef yes = LLVMConstInt(i1, 1, 0);
    int op = expr->binary.op;
    bool left_is_null_lit = expr->binary.left->kind == AST_NULL_LITERAL;
    bool right_is_null_lit = expr->binary.right->kind == AST_NULL_LITERAL;
    bool ln = llvm_is_nullable(LLVMTypeOf(left));
    bool rn = llvm_is_nullable(LLVMTypeOf(right));
    LLVMValueRef lhas = ln ? nullable_has_value(g, left) : yes;
    LLVMValueRef rhas = rn ? nullable_has_value(g, right) : yes;
    LLVMValueRef lval = ln ? nullable_get_payload(g, left) : left;
    LLVMValueRef rval = rn ? nullable_get_payload(g, right) : right;

    if ((op == TK_EQ_EQ || op == TK_BANG_EQ) &&
        (left_is_null_lit || right_is_null_lit)) {
        LLVMValueRef has = left_is_null_lit ? rhas : lhas;
        return op == TK_EQ_EQ ? LLVMBuildNot(g->builder, has, "nv.isnull") : has;
    }

    /* 可空合并运算符 ?? 求值 */
    if (op == TK_QUESTION_QUESTION) {
        if (!ln) return left;
        if (rn) return LLVMBuildSelect(g->builder, lhas, left, right, "nv.coal");
        LLVMValueRef alt = coerce_int_to(g, right, LLVMTypeOf(lval));
        if (LLVMTypeOf(alt) != LLVMTypeOf(lval)) {
            lval = coerce_int_to(g, lval, LLVMTypeOf(right));
            alt = right;
        }
        if (LLVMTypeOf(alt) != LLVMTypeOf(lval)) return left;
        return LLVMBuildSelect(g->builder, lhas, lval, alt, "nv.coal");
    }

    LLVMValueRef both = LLVMBuildAnd(g->builder, lhas, rhas, "nv.both");

    if (op == TK_EQ_EQ || op == TK_BANG_EQ) {
        /* 可空类型相等性判断：均为 null 或内部值均相等 */
        LLVMValueRef eq = emit_binary_op_values(g,
            binary_with_op(g, expr, TK_EQ_EQ), lval, rval, locals);
        LLVMValueRef neither = LLVMBuildAnd(g->builder,
            LLVMBuildNot(g->builder, lhas, "nv.ln"),
            LLVMBuildNot(g->builder, rhas, "nv.rn"), "nv.neither");
        LLVMValueRef same = LLVMBuildOr(g->builder, neither,
            LLVMBuildAnd(g->builder, both, eq, "nv.veq"), "nv.eq");
        return op == TK_EQ_EQ ? same
                              : LLVMBuildNot(g->builder, same, "nv.ne");
    }
    if (op == TK_LESS || op == TK_LESS_EQ ||
        op == TK_GREATER || op == TK_GREATER_EQ) {
        LLVMValueRef cmp = emit_binary_op_values(g, expr, lval, rval, locals);
        return LLVMBuildAnd(g->builder, both, cmp, "nv.cmp");
    }

    /* 可空类型四则运算：任一操作数为 null 则结果为 null */
    if (op == TK_SLASH || op == TK_PERCENT) {
        LLVMTypeRef rt = LLVMTypeOf(rval);
        LLVMValueRef one = LLVMGetTypeKind(rt) == LLVMDoubleTypeKind ||
                           LLVMGetTypeKind(rt) == LLVMFloatTypeKind
            ? LLVMConstReal(rt, 1.0)
            : LLVMConstInt(rt, 1, 0);
        rval = LLVMBuildSelect(g->builder, both, rval, one, "nv.div1");
    }
    LLVMValueRef res = emit_binary_op_values(g, expr, lval, rval, locals);
    LLVMTypeRef nty = nullable_type_of(g, LLVMTypeOf(res));
    LLVMValueRef some = nullable_some(g, nty, res);
    if (!some) return res;
    return LLVMBuildSelect(g->builder, both, some, nullable_none(nty), "nv.lift");
}
LLVMValueRef emit_incdec_expr(zan_irgen_t *g, zan_ast_node_t *expr,
                                     local_scope_t *locals, int is_prefix);

/* 一元运算符重载：类/结构体上的 static `operator -` / `operator +`。
 *
 * 解析器把 `operator -` / `operator +` 统一命名成 op_sub / op_add，一元与
 * 二元靠**形参个数**区分：重载是 static 的，形参 1 个是一元、2 个是二元。
 * 因此这里的探针**不带实参**（arity 期望 0 个显式实参 + 1 个接收者）。
 *
 * 找不到重载时报一条人话诊断并返回 undef——不能落到内建整数算术去对结构体
 * 指针做 sub（那是 LLVM 校验失败："Integer arithmetic operators only work
 * with integral types"），更不能默默当 0。
 *
 * 返回 NULL 表示"操作数不是类/结构体"，交回内建算术路径。 */
