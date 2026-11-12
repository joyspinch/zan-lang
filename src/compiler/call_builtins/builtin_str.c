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
                            LLVMValueRef errnop = emit_errno_preset(g, i32t);
                            parsed = zan_call2(g->builder, strtoll_ty, f,
                                (LLVMValueRef[]){ s_safe, endp,
                                                  LLVMConstInt(i32t, 10, 0) }, 3, "toint");
                            LLVMValueRef ep = LLVMBuildLoad2(g->builder, i8ptr,
                                endp, "toint.ep");
                            /* 与 int.Parse 同口径两路分流：格式错 →
                             * FormatException，越界（范围谓词 / ERANGE 兜底）→
                             * OverflowException。 */
                            LLVMValueRef bad = emit_parse_bad_pred(g, s_safe, ep);
                            emit_runtime_check(g, bad, expr->loc,
                                "Convert.ToInt*: input is not a valid integer");
                            LLVMValueRef er = LLVMBuildLoad2(g->builder, i32t,
                                errnop, "toint.errno");
                            LLVMValueRef over = zan_icmp(g->builder, LLVMIntEQ, er,
                                LLVMConstInt(i32t, 34, 0), "toint.erange");
                            if (method_name.str[6] != '4') {
                                /* ToInt32/ToInt：ERANGE 抓不到的中段越界
                                 * （3000000000 吃进 64 位不置 ERANGE），C# 抛
                                 * Overflow。 */
                                over = LLVMBuildOr(g->builder, over, LLVMBuildOr(
                                    g->builder,
                                    zan_icmp(g->builder, LLVMIntSLT, parsed,
                                        LLVMConstInt(i64t, (int64_t)INT32_MIN, 0), "toint.lo"),
                                    zan_icmp(g->builder, LLVMIntSGT, parsed,
                                        LLVMConstInt(i64t, INT32_MAX, 0), "toint.hi"),
                                    "toint.over32"), "toint.over");
                            }
                            emit_runtime_check(g, over, expr->loc,
                                "Convert.ToInt*: value is outside the range of the target type");
                            bad = LLVMBuildOr(g->builder, bad, over, "toint.bad2");
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
                            /* B-ID180：double 实参路径此前裸 fptosi——1e300
                             * 溢出 poison、NaN 平台各异、try 内无异常分发
                             * （Convert 字符串/整型路径 B-ID169 已收，此处
                             * 补齐）。守卫后 i64 归一化，其后 over 判定与
                             * select-0 兜底不变。 */
                            parsed = emit_fptosi_guarded(g, r, i64t, expr->loc, NULL);
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
                /* Substring returns an ARC-compatible string on every path.
                 * A raw C empty literal may be suffix-merged at an unaligned
                 * address, corrupting the EH temporary stack's low-bit tags. */
                LLVMValueRef empty_str = emit_string_literal_rc(g,
                    (zan_istr_t){ "", 0 });
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
            /* IndexOf 对常量 ASCII 字符针（'\n'、':' 等）降级为 memchr：
               libc memchr 是 SIMD 实现，而 strstr 路径需要把 char 针物化成
               字符串再通用子串搜索，逐行头部扫描场景实测比手写循环慢 ~5x。
               memchr 以显式长度为界，符合 Zan 长度前置的字符串模型；
               needle 为 0（NUL）时与 strstr 的 NUL 截止语义分叉，仍走 strstr。
               start 非法时按 0 长度调用返回 -1，与 strstr 路径的
               invalid_start 判定同口径。接收方限 string 与 byte[]。 */
            if (is_idx && (expr->call.args.count == 1 || expr->call.args.count == 2) &&
                expr->call.args.items[0]->kind == AST_CHAR_LITERAL &&
                expr->call.args.items[0]->int_val > 0 &&
                expr->call.args.items[0]->int_val < 128) {
                zan_type_t *mc_recv_t = infer_expr_type(g, sc->member.object, locals);
                int recv_is_str = is_string_expr(g, sc->member.object, locals);
                int recv_is_bytearr = mc_recv_t && mc_recv_t->kind == TYPE_ARRAY &&
                    mc_recv_t->element_type &&
                    mc_recv_t->element_type->kind == TYPE_BYTE;
                if (recv_is_str || recv_is_bytearr) {
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                    LLVMValueRef slen;
                    if (recv_is_bytearr) {
                        slen = zan_array_len(g, s);
                    } else {
                        slen = emit_string_length(g, s, expr->loc);
                    }
                    LLVMValueRef search = s;
                    LLVMValueRef start = LLVMConstInt(i64, 0, 0);
                    LLVMValueRef valid_start = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0);
                    if (expr->call.args.count == 2) {
                        start = emit_expr(g, expr->call.args.items[1], locals);
                        if (LLVMGetTypeKind(LLVMTypeOf(start)) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(LLVMTypeOf(start)) < 64)
                            start = LLVMBuildSExt(g->builder, start, i64, "mc.start");
                        LLVMValueRef nonneg = zan_icmp(g->builder, LLVMIntSGE, start,
                            LLVMConstInt(i64, 0, 0), "mc.nonneg");
                        LLVMValueRef within = zan_icmp(g->builder, LLVMIntSLE, start,
                            slen, "mc.within");
                        valid_start = zan_and(g->builder, nonneg, within, "mc.valid");
                        search = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                            s, &start, 1, "mc.search");
                    }
                    LLVMValueRef nraw = zan_sub(g->builder, slen, start, "mc.nraw");
                    LLVMValueRef n = LLVMBuildSelect(g->builder, valid_start, nraw,
                        LLVMConstInt(i64, 0, 0), "mc.n");
                    LLVMTypeRef memchr_ty = LLVMFunctionType(i8ptr,
                        (LLVMTypeRef[]){ i8ptr, LLVMInt32TypeInContext(g->ctx), i64 }, 3, 0);
                    LLVMValueRef mc_fn = get_libc_fn(g, "memchr", memchr_ty);
                    LLVMValueRef mc_args[] = {
                        search,
                        LLVMConstInt(LLVMInt32TypeInContext(g->ctx),
                            (uint64_t)expr->call.args.items[0]->int_val, 0),
                        n
                    };
                    LLVMValueRef hit = LLVMBuildCall2(g->builder, memchr_ty, mc_fn,
                        mc_args, 3, "mc.hit");
                    LLVMValueRef diff = zan_sub(g->builder,
                        LLVMBuildPtrToInt(g->builder, hit, i64, "mc.hi"),
                        LLVMBuildPtrToInt(g->builder, s, i64, "mc.si"), "mc.diff");
                    LLVMValueRef missed = zan_icmp(g->builder, LLVMIntEQ, hit,
                        LLVMConstPointerNull(i8ptr), "mc.missed");
                    LLVMValueRef invalid_start = zan_icmp(g->builder, LLVMIntEQ,
                        valid_start, LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0),
                        "mc.invalid");
                    LLVMValueRef failed = zan_or(g->builder, missed, invalid_start,
                        "mc.failed");
                    LLVMValueRef res = LLVMBuildSelect(g->builder, failed,
                        LLVMConstInt(i64, (uint64_t)-1, 1), diff, "mc.idx");
                    emit_release_owned_call_temp(g, sc->member.object, s, locals);
                    return res;
                }
            }
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

