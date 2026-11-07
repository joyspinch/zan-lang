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
            int bool_recv = (rn.len == 4 && memcmp(rn.str, "bool", 4) == 0) ||
                            (rn.len == 7 && memcmp(rn.str, "Boolean", 7) == 0);
            if ((int_recv || dbl_recv || bool_recv) &&
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
                    if (bool_recv) {
                        return LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
                    }
                    return int_recv ? LLVMConstInt(i64, 0, 0)
                                    : LLVMConstReal(dbl, 0.0);
                }
                LLVMValueRef r;
                if (bool_recv) {
                    /* C# bool.Parse：只认 true/false 的大小写任意变体，
                     * 其余 FormatException（消息含 "not a valid" 命中
                     * guard 的 Format 路由）。strcasecmp 在目标 exe 的
                     * CRT 可用（runtime gui_runtime_font.c 同款先例）。 */
                    LLVMTypeRef i1t = LLVMInt1TypeInContext(g->ctx);
                    LLVMTypeRef sc_ty = LLVMFunctionType(i32t,
                        (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                    LLVMValueRef f = get_libc_fn(g, "strcasecmp", sc_ty);
                    LLVMValueRef s_safe = emit_parse_str_guard(g, s, expr->loc);
                    LLVMValueRef s_true = LLVMBuildGlobalStringPtr(
                        g->builder, "true", "bool.ptrue");
                    LLVMValueRef s_false = LLVMBuildGlobalStringPtr(
                        g->builder, "false", "bool.pfalse");
                    LLVMValueRef st = zan_call2(g->builder, sc_ty, f,
                        (LLVMValueRef[]){ s_safe, s_true }, 2, "bool.pt");
                    LLVMValueRef sf = zan_call2(g->builder, sc_ty, f,
                        (LLVMValueRef[]){ s_safe, s_false }, 2, "bool.pf");
                    LLVMValueRef is_t = zan_icmp(g->builder, LLVMIntEQ, st,
                        LLVMConstInt(i32t, 0, 0), "bool.ist");
                    LLVMValueRef is_f = zan_icmp(g->builder, LLVMIntEQ, sf,
                        LLVMConstInt(i32t, 0, 0), "bool.isf");
                    LLVMValueRef ok = LLVMBuildOr(g->builder, is_t, is_f,
                        "bool.ok");
                    emit_runtime_check(g, LLVMBuildNot(g->builder, ok,
                        "bool.bad"), expr->loc,
                        "bool.Parse: input is not a valid Boolean");
                    LLVMValueRef v = LLVMBuildSelect(g->builder, is_t,
                        LLVMConstInt(i1t, 1, 0),
                        LLVMConstInt(i1t, 0, 0), "bool.v");
                    emit_release_owned_call_temp(
                        g, expr->call.args.items[0], s, locals);
                    return LLVMBuildSelect(g->builder, ok, v,
                        LLVMConstInt(i1t, 0, 0), "bool.safe");
                }
                if (int_recv) {
                    int is64 = (rn.len == 4 && memcmp(rn.str, "long", 4) == 0) ||
                               (rn.len == 5 && memcmp(rn.str, "Int64", 5) == 0);
                    LLVMTypeRef strtoll_ty = LLVMFunctionType(i64,
                        (LLVMTypeRef[]){ i8ptr, i8pp, i32t }, 3, 0);
                    LLVMValueRef f = get_libc_fn(g, "strtoll", strtoll_ty);
                    LLVMValueRef endp = emit_entry_alloca(g, i8ptr, "parse.endp");
                    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), endp);
                    LLVMValueRef s_safe = emit_parse_str_guard(g, s, expr->loc);
                    LLVMValueRef errnop = emit_errno_preset(g, i32t);
                    r = zan_call2(g->builder, strtoll_ty, f,
                        (LLVMValueRef[]){ s_safe, endp,
                                          LLVMConstInt(i32t, 10, 0) }, 3, "parse");
                    LLVMValueRef ep = LLVMBuildLoad2(g->builder, i8ptr, endp, "parse.ep");
                    /* C# 语义两路分流：格式错 → FormatException，数值越界 →
                     * OverflowException。曾共用一条带 "(or out of range)" 的
                     * 消息，被关键词匹配整体误判成 IndexOutOfRange。两条检查
                     * 顺序发射：try 内第一条命中即抛不走第二条；try 外软诊断
                     * 各报各的，最后统一 select 归零。 */
                    LLVMValueRef bad = emit_parse_bad_pred(g, s_safe, ep);
                    emit_runtime_check(g, bad, expr->loc,
                        "int.Parse: input is not a valid integer");
                    /* strtoll 饱和返回 LLONG_MIN/MAX 并置 errno=ERANGE——返回
                     * 值分不清「恰好极值」与「饱和」，long.Parse 的全宽范围
                     * 只有 ERANGE 抓得住（曾按 INT32 边界判，把合法的
                     * long.Parse("9223372036854775807") 误杀）。i32 档再补中段
                     * 越界：3000000000 吃进 64 位不置 ERANGE，但超出 int 范围
                     * 要按 C# 抛 Overflow。 */
                    LLVMValueRef er = LLVMBuildLoad2(g->builder, i32t, errnop,
                        "parse.errno");
                    LLVMValueRef over = zan_icmp(g->builder, LLVMIntEQ, er,
                        LLVMConstInt(i32t, 34, 0), "parse.erange");
                    if (!is64) {
                        over = LLVMBuildOr(g->builder, over, LLVMBuildOr(g->builder,
                            zan_icmp(g->builder, LLVMIntSLT, r,
                                LLVMConstInt(i64, (int64_t)INT32_MIN, 0), "parse.lo"),
                            zan_icmp(g->builder, LLVMIntSGT, r,
                                LLVMConstInt(i64, INT32_MAX, 0), "parse.hi"),
                            "parse.over32"), "parse.over");
                    }
                    emit_runtime_check(g, over, expr->loc,
                        "int.Parse: value is outside the range of the target type");
                    bad = LLVMBuildOr(g->builder, bad, over, "parse.bad2");
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
                    /* 溢出（strtod ERANGE）由 zan_rt_dbl_parse 把 endp 拨回
                     * 起点走零消费判坏——"Infinity" 特殊拼写是合法 INF 回读，
                     * 不能按值判 INF 当溢出杀（曾误杀致 case_double_format 红）。 */
                    emit_runtime_check(g, bad, expr->loc,
                        "double.Parse: input is not a valid number");
                    r = LLVMBuildSelect(g->builder, bad,
                        LLVMConstReal(dbl, 0.0), r, "parse.safe");
                }
                emit_release_owned_call_temp(g, expr->call.args.items[0], s, locals);
                return r;
            }
            if ((int_recv || dbl_recv || bool_recv) &&
                pm.len == 8 && memcmp(pm.str, "TryParse", 8) == 0 &&
                expr->call.args.count == 2) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef i1t = LLVMInt1TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, expr->call.args.items[0], locals);
                zan_type_t *at = infer_expr_type(g, expr->call.args.items[0], locals);
                if (!type_is_text_arg(at)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3058,
                                  "%.*s.TryParse requires a string argument",
                                  (int)rn.len, rn.str);
                    return LLVMConstInt(i1t, 0, 0);
                }
                /* null 文本不抛异常，按标准语义 TryParse 失败返回 false */
                LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, s,
                    LLVMConstNull(LLVMTypeOf(s)), "tp.snull");
                LLVMValueRef s_safe = LLVMBuildSelect(g->builder, isnull,
                    zan_irgen_intern_string(g, ""), s, "tp.ssafe");
                /* out 实参取槽位地址而非值：AST_REF_ARG 由 emit_ref_arg 直接
                 * 给地址，裸标识符查局部槽 alloca。曾按值 emit_expr——类型
                 * 非指针被下方守卫静默跳过写入，out 变量永远不更新
                 * （int.TryParse("123", out okv) 的 okv 原值原样保留）。 */
                zan_ast_node_t *out_arg = expr->call.args.items[1];
                LLVMValueRef outp = NULL;
                zan_type_t *out_ty = NULL;
                if (out_arg->kind == AST_REF_ARG) {
                    outp = emit_expr(g, out_arg, locals);
                    if (out_arg->ref_arg.expr)
                        out_ty = infer_expr_type(g, out_arg->ref_arg.expr, locals);
                } else if (out_arg->kind == AST_IDENTIFIER) {
                    local_var_t *ol = local_find(locals, out_arg->ident.name);
                    if (ol) { outp = ol->alloca; out_ty = ol->type; }
                }

                if (bool_recv) {
                    LLVMTypeRef sc_ty = LLVMFunctionType(i32t,
                        (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                    LLVMValueRef f = get_libc_fn(g, "strcasecmp", sc_ty);
                    LLVMValueRef s_true = LLVMBuildGlobalStringPtr(
                        g->builder, "true", "btp.ptrue");
                    LLVMValueRef s_false = LLVMBuildGlobalStringPtr(
                        g->builder, "false", "btp.pfalse");
                    LLVMValueRef st = zan_call2(g->builder, sc_ty, f,
                        (LLVMValueRef[]){ s_safe, s_true }, 2, "btp.pt");
                    LLVMValueRef sf = zan_call2(g->builder, sc_ty, f,
                        (LLVMValueRef[]){ s_safe, s_false }, 2, "btp.pf");
                    LLVMValueRef is_t = zan_icmp(g->builder, LLVMIntEQ, st,
                        LLVMConstInt(i32t, 0, 0), "btp.ist");
                    LLVMValueRef is_f = zan_icmp(g->builder, LLVMIntEQ, sf,
                        LLVMConstInt(i32t, 0, 0), "btp.isf");
                    LLVMValueRef ok = LLVMBuildOr(g->builder, is_t, is_f, "btp.ok");
                    LLVMValueRef not_null = LLVMBuildNot(g->builder, isnull, "btp.notnull");
                    ok = zan_and(g->builder, not_null, ok, "btp.valid");
                    LLVMValueRef stored = LLVMBuildSelect(g->builder, is_t,
                        LLVMConstInt(i1t, 1, 0),
                        LLVMConstInt(i1t, 0, 0), "btp.v");
                    if (outp) {
                        if (out_ty) {
                            emit_typed_out_store(g, out_ty, outp, stored);
                        } else if (LLVMGetTypeKind(LLVMTypeOf(outp)) == LLVMPointerTypeKind) {
                            LLVMBuildStore(g->builder, stored, outp);
                        }
                    }
                    emit_release_owned_call_temp(g, expr->call.args.items[0], s, locals);
                    return ok;
                }

                LLVMValueRef endp = emit_entry_alloca(g, i8ptr, "endp");
                LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), endp);
                LLVMValueRef v;
                LLVMValueRef over = NULL;
                if (int_recv) {
                    int is64 = (rn.len == 4 && memcmp(rn.str, "long", 4) == 0) ||
                               (rn.len == 5 && memcmp(rn.str, "Int64", 5) == 0);
                    LLVMValueRef errnop = emit_errno_preset(g, i32t);
                    LLVMTypeRef strtoll_ty = LLVMFunctionType(i64,
                        (LLVMTypeRef[]){ i8ptr, i8pp, i32t }, 3, 0);
                    LLVMValueRef f = get_libc_fn(g, "strtoll", strtoll_ty);
                    v = zan_call2(g->builder, strtoll_ty, f,
                        (LLVMValueRef[]){ s_safe, endp, LLVMConstInt(i32t, 10, 0) }, 3, "tp");
                    LLVMValueRef er = LLVMBuildLoad2(g->builder, i32t, errnop, "tp.errno");
                    over = zan_icmp(g->builder, LLVMIntEQ, er, LLVMConstInt(i32t, 34, 0), "tp.erange");
                    if (!is64) {
                        over = LLVMBuildOr(g->builder, over, LLVMBuildOr(g->builder,
                            zan_icmp(g->builder, LLVMIntSLT, v,
                                LLVMConstInt(i64, (int64_t)INT32_MIN, 0), "tp.lo"),
                            zan_icmp(g->builder, LLVMIntSGT, v,
                                LLVMConstInt(i64, INT32_MAX, 0), "tp.hi"),
                            "tp.over32"), "tp.over");
                    }
                } else {
                    LLVMTypeRef strtod_ty = LLVMFunctionType(dbl,
                        (LLVMTypeRef[]){ i8ptr, i8pp }, 2, 0);
                    LLVMValueRef f = LLVMGetNamedFunction(g->mod, "zan_rt_dbl_parse");
                    if (!f) f = LLVMAddFunction(g->mod, "zan_rt_dbl_parse", strtod_ty);
                    v = zan_call2(g->builder, strtod_ty, f,
                        (LLVMValueRef[]){ s_safe, endp }, 2, "tp");
                }
                LLVMValueRef e = LLVMBuildLoad2(g->builder, i8ptr, endp, "e");
                LLVMValueRef moved = zan_icmp(g->builder, LLVMIntNE, e, s_safe, "moved");
                LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8, e, "ch");
                LLVMValueRef at_end = zan_icmp(g->builder, LLVMIntEQ, ch,
                    LLVMConstInt(i8, 0, 0), "atend");
                LLVMValueRef not_null = LLVMBuildNot(g->builder, isnull, "tp.notnull");
                LLVMValueRef ok = zan_and(g->builder, not_null,
                    zan_and(g->builder, moved, at_end, "moved_end"), "ok");
                if (over) {
                    LLVMValueRef not_over = LLVMBuildNot(g->builder, over, "tp.notover");
                    ok = zan_and(g->builder, ok, not_over, "ok2");
                }
                LLVMValueRef stored = LLVMBuildSelect(g->builder, ok, v,
                    int_recv ? LLVMConstInt(i64, 0, 0) : (LLVMValueRef)LLVMConstReal(dbl, 0.0), "tpv");
                if (outp) {
                    if (out_ty) {
                        /* 槽位宽度和值宽度不一致（如 int 槽收 i64 解析值）按
                         * 类型收窄，与 Dict.TryGetValue 同一套 out 落槽机制。 */
                        emit_typed_out_store(g, out_ty, outp, stored);
                    } else if (LLVMGetTypeKind(LLVMTypeOf(outp)) == LLVMPointerTypeKind) {
                        LLVMBuildStore(g->builder, stored, outp);
                    }
                }
                emit_release_owned_call_temp(g, expr->call.args.items[0], s, locals);
                return ok;
            }
        }

