
/* 转换字符串插值格式化说明符（如冒号后的格式标记）。插值与标量
 * ToString("F2") 调用两条路共用（irgen.h 声明，irgen_call.c 也用）。 */
int interp_format_to_printf(const zan_istr_t *spec, bool is_float,
                            bool is_ulong, bool *want_float,
                            char *out, size_t outcap) {
    if (want_float) *want_float = false;
    if (!spec || spec->len == 0) return 0;
    const char *s = spec->str;
    int len = (int)spec->len;
    char code = s[0];
    int digits = 0;
    for (int i = 1; i < len; i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            if (digits <= 9999) digits = digits * 10 + (s[i] - '0');
        }
    }
    switch (code) {
    case 'D': case 'd': /* decimal, zero-padded to `digits` */
        if (is_float) break;
        if (digits <= 0 || digits > 512) return 0;
        if (is_ulong)
            return snprintf(out, outcap, "%%0%dllu", digits);
        return snprintf(out, outcap, "%%0%dlld", digits);
    case 'X': case 'x': 
        if (is_float) break;
        if (digits <= 0 || digits > 512) return 0;
        return snprintf(out, outcap, "%%0%dll%c", digits,
                        code == 'X' ? 'X' : 'x');
    case 'F': case 'f':
        if (digits < 0) digits = 0;
        if (digits > 512) digits = 512;
        if (want_float) *want_float = true;
        return snprintf(out, outcap, "%%.%dlf", digits);
    case 'E': case 'e': /* scientific */
        if (digits < 0) digits = 0;
        if (digits > 512) digits = 512;
        if (want_float) *want_float = true;
        return snprintf(out, outcap, "%%.%dE", digits);
    case 'G': case 'g': 
        if (digits > 0)
            return snprintf(out, outcap, "%%.%dg", digits);
        return 0;
    case '0': case '#': {
        int ipad = 0, frac = 0;
        bool after_dot = false, have_dot = false;
        for (int i = 0; i < len; i++) {
            if (s[i] == '.') { after_dot = true; have_dot = true; continue; }
            if (s[i] == '0' || s[i] == '#') {
                if (after_dot) frac++;
                else if (s[i] == '0') ipad++;
            }
        }
        if (have_dot) {
            if (want_float) *want_float = true;
            return snprintf(out, outcap, "%%.%dlf", frac);
        }
        if (ipad > 0) {
            if (is_ulong) return snprintf(out, outcap, "%%0%dllu", ipad);
            return snprintf(out, outcap, "%%0%dlld", ipad);
        }
        return 0;
    }
    default:
        break;
    }
    return 0;
}

static LLVMValueRef emit_expr_string_interp(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* 字符串插值：将各子表达式格式化为字符串后统一拼接 */
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);

        int n = expr->string_interp.parts.count;
        if (n == 0) return emit_string_literal_rc(g, (zan_istr_t){ "", 0 });

        LLVMValueRef *strs = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
        LLVMValueRef *lens = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
        unsigned char *owns = (unsigned char *)calloc((size_t)n, sizeof(unsigned char));

        LLVMTypeRef strlen_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
        LLVMTypeRef snprintf_type = LLVMFunctionType(
            LLVMInt32TypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8ptr, i64, i8ptr }, 3, 1);

        for (int i = 0; i < n; i++) {
            zan_ast_node_t *part = expr->string_interp.parts.items[i];
            if (part->kind == AST_STRING_LITERAL) {
                strs[i] = emit_string_literal_rc(g, part->str_val);
                lens[i] = LLVMConstInt(i64, (uint64_t)part->str_val.len, 0);
            } else {
                int k = (i - 1) / 2;
                zan_ast_node_t *fnode = (k >= 0 &&
                    k < expr->string_interp.formats.count)
                    ? expr->string_interp.formats.items[k] : NULL;
                zan_istr_t spec = fnode ? fnode->str_val
                                        : (zan_istr_t){ NULL, 0 };

                LLVMValueRef val = emit_expr(g, part, locals);
                LLVMTypeRef vt = LLVMTypeOf(val);
                LLVMTypeKind vtk = LLVMGetTypeKind(vt);

                /* bool 的**语义类型**优先于 LLVM 宽度：泛型方法返回值
                   （List<T>.Contains 这类）被提升成 i64，按宽度判会掉进整数
                   分支渲染成 "1"（C# 的口径是 true/false）。 */
                LLVMValueRef bcond = (vtk == LLVMIntegerTypeKind)
                    ? emit_bool_cond(g, val, part, locals) : NULL;

                if (bcond) {
                    LLVMValueRef btrue = emit_string_literal_rc(g,
                        (zan_istr_t){ "true", 4 });
                    LLVMValueRef bfalse = emit_string_literal_rc(g,
                        (zan_istr_t){ "false", 5 });
                    strs[i] = LLVMBuildSelect(g->builder, bcond, btrue, bfalse,
                        "bstr");
                    lens[i] = LLVMBuildSelect(g->builder, bcond,
                        LLVMConstInt(i64, 4, 0), LLVMConstInt(i64, 5, 0),
                        "blen");
                    owns[i] = 0;
                } else if (vtk == LLVMPointerTypeKind) {
                    strs[i] = emit_str_nonnull(g, val);
                    lens[i] = emit_string_length(g, strs[i], expr->loc);
                    owns[i] = expr_yields_owned_rc_value(g, part, locals) ? 1 : 0;
                } else if (vtk == LLVMDoubleTypeKind || vtk == LLVMFloatTypeKind) {
                    /* 两段式 snprintf：首轮预计算格式化长度，次轮写入缓冲区 */
                    char fbuf[32];
                    bool want_f = false;
                    int flen = interp_format_to_printf(&spec, true, false,
                        &want_f, fbuf, sizeof(fbuf));
                    LLVMValueRef fval = (vtk == LLVMFloatTypeKind)
                        ? LLVMBuildFPExt(g->builder, val,
                            LLVMDoubleTypeInContext(g->ctx), "f2d")
                        : val;
                    if (flen <= 0) {
                        LLVMValueRef buf = emit_string_alloc_rc(g,
                            LLVMConstInt(i64, 40, 0));
                        emit_dbl_str(g, buf, LLVMConstInt(i64, 40, 0), val);
                        strs[i] = buf;
                        lens[i] = emit_string_length(g, buf, expr->loc);
                        owns[i] = 1;
                    } else {
                    LLVMValueRef fmt = zan_irgen_intern_string(g, fbuf);
                    LLVMValueRef null_ptr = LLVMConstNull(i8ptr);
                    LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
                    LLVMValueRef snp_args1[] = { null_ptr, zero, fmt, fval };
                    LLVMValueRef needed = zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args1, 4, "needed");
                    LLVMValueRef needed64 = LLVMBuildSExt(g->builder, needed, i64, "n64");
                    LLVMValueRef buf_size = zan_add(g->builder, needed64, LLVMConstInt(i64, 1, 0), "bsz");
                    LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
                    LLVMValueRef snp_args2[] = { buf, buf_size, fmt, fval };
                    zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args2, 4, "");
                    strs[i] = buf;
                    lens[i] = needed64;
                    owns[i] = 1;
                    }
                } else if (vtk == LLVMIntegerTypeKind &&
                           LLVMGetIntTypeWidth(vt) == 1) {
                    /* bool 类型插值为字面量 "true" 或 "false" */
                    LLVMValueRef btrue = emit_string_literal_rc(g,
                        (zan_istr_t){ "true", 4 });
                    LLVMValueRef bfalse = emit_string_literal_rc(g,
                        (zan_istr_t){ "false", 5 });
                    strs[i] = LLVMBuildSelect(g->builder, val, btrue, bfalse,
                        "bstr");
                    lens[i] = LLVMBuildSelect(g->builder, val,
                        LLVMConstInt(i64, 4, 0), LLVMConstInt(i64, 5, 0),
                        "blen");
                    owns[i] = 0;
                } else if (expr_is_char(g, part, locals)) {
                    /* char 类型插值为字符本身而非数值编码 */
                    LLVMValueRef buf = emit_char_to_cstr(g, val);
                    strs[i] = buf;
                    lens[i] = emit_string_length(g, buf, expr->loc);
                    owns[i] = 1;
                } else if (llvm_is_nullable(vt)) {
                    /* 可空类型插值：与字符串拼接使用一致的强制转换 */
                    zan_type_t *st = infer_expr_type(g, part, locals);
                    bool uns = st && st->element_type &&
                               (st->element_type->kind == TYPE_UINT ||
                                st->element_type->kind == TYPE_ULONG);
                    strs[i] = emit_to_cstr_u(g, val, uns ? 1 : 0);
                    lens[i] = zan_call2(g->builder, strlen_type, g->fn_strlen,
                                        &strs[i], 1, "nvlen");
                    owns[i] = 1;
                } else if (vt == get_decimal_type(g)) {
                    LLVMValueRef dslot = emit_entry_alloca(g, vt, "dec.interp.slot");
                    LLVMBuildStore(g->builder, val, dslot);
                    LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 48, 0));
                    LLVMTypeRef to_str_fn_ty = LLVMFunctionType(
                        LLVMInt32TypeInContext(g->ctx),
                        (LLVMTypeRef[]){ LLVMPointerType(vt, 0), i8ptr, i64 },
                        3, 0);
                    LLVMValueRef to_str_fn = get_libc_fn(g, "__zan_decimal_to_string", to_str_fn_ty);
                    LLVMValueRef slen = zan_call2(g->builder, to_str_fn_ty, to_str_fn,
                        (LLVMValueRef[]){ dslot, buf, LLVMConstInt(i64, 48, 0) }, 3, "dec.slen");
                    LLVMValueRef slen64 = LLVMBuildZExt(g->builder, slen, i64, "dec.slen64");
                    emit_string_len_set(g, buf, slen64);
                    strs[i] = buf;
                    lens[i] = slen64;
                    owns[i] = 1;
                } else {
                    /* 整数插值格式化：基于 snprintf 转换为十进制字符串 */
                    LLVMValueRef val64 =
                        (LLVMGetIntTypeWidth(vt) < 64)
                            ? zan_iwiden(g->builder, val, i64) : val;
                    bool iu = expr_is_ulong(g, part, locals);
                    char fbuf[32];
                    bool want_f = false;
                    int flen = interp_format_to_printf(&spec, false, iu,
                        &want_f, fbuf, sizeof(fbuf));
                    if (want_f) {
                        LLVMValueRef as_d = (vt == LLVMFloatTypeKind)
                            ? LLVMBuildFPExt(g->builder, val,
                                LLVMDoubleTypeInContext(g->ctx), "f2d")
                            : LLVMBuildSIToFP(g->builder, val64,
                                LLVMDoubleTypeInContext(g->ctx), "i2d");
                        LLVMValueRef fmt = zan_irgen_intern_string(g,
                            fbuf);
                        LLVMValueRef null_ptr = LLVMConstNull(i8ptr);
                        LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
                        LLVMValueRef snp_args1[] = { null_ptr, zero, fmt, as_d };
                        LLVMValueRef needed = zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args1, 4, "needed");
                        LLVMValueRef needed64 = LLVMBuildSExt(g->builder, needed, i64, "n64");
                        LLVMValueRef buf_size = zan_add(g->builder, needed64, LLVMConstInt(i64, 1, 0), "bsz");
                        LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
                        LLVMValueRef snp_args2[] = { buf, buf_size, fmt, as_d };
                        zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args2, 4, "");
                        strs[i] = buf;
                        lens[i] = needed64;
                        owns[i] = 1;
                        continue;
                    }
                    if (flen <= 0) {
                        /* 无格式说明符的整型直接使用固定 21 字节栈缓冲区转换 */
                        LLVMValueRef buf = emit_string_alloc_rc(g,
                            LLVMConstInt(i64, 24, 0));
                        strs[i] = buf;
                        lens[i] = emit_itoa_into(g, buf, val64, iu ? 1 : 0);
                        owns[i] = 1;
                        continue;
                    }
                    LLVMValueRef fmt = zan_irgen_intern_string(g,
                        fbuf);
                    LLVMValueRef null_ptr = LLVMConstNull(i8ptr);
                    LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
                    LLVMValueRef snp_args1[] = { null_ptr, zero, fmt, val64 };
                    LLVMValueRef needed = zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args1, 4, "needed");
                    LLVMValueRef needed64 = LLVMBuildSExt(g->builder, needed, i64, "n64");
                    LLVMValueRef buf_size = zan_add(g->builder, needed64, LLVMConstInt(i64, 1, 0), "bsz");
                    LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
                    LLVMValueRef snp_args2[] = { buf, buf_size, fmt, val64 };
                    zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args2, 4, "");
                    strs[i] = buf;
                    lens[i] = needed64;
                    owns[i] = 1;
                }
            }
        }

        LLVMValueRef total_len = LLVMConstInt(i64, 0, 0);
        for (int i = 0; i < n; i++) {
            total_len = zan_add(g->builder, total_len, lens[i], "tlen");
        }
        LLVMValueRef alloc_size = zan_add(g->builder, total_len, LLVMConstInt(i64, 1, 0), "asz");

        LLVMValueRef result = emit_string_alloc_rc(g, alloc_size);

        /* 按托管长度 memcpy 各片段拼接字符串，避免 NUL 字符截断 */
        LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
        LLVMTypeRef memcpy_type = LLVMFunctionType(i8ptr,
            (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
        LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
        if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_type);
        LLVMValueRef off = LLVMConstInt(i64, 0, 0);
        for (int i = 0; i < n; i++) {
            LLVMValueRef dst = LLVMBuildGEP2(g->builder, i8, result, &off, 1,
                                             "ip.dst");
            zan_call2(g->builder, memcpy_type, memcpy_fn,
                (LLVMValueRef[]){ dst, strs[i], lens[i] }, 3, "");
            off = zan_add(g->builder, off, lens[i], "ip.off");
        }
        LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, result, &off, 1,
                                          "ip.end");
        LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);

        if (owns) {
            for (int i = 0; i < n; i++) {
                if (owns[i]) emit_string_release(g, strs[i]);
            }
        }
        emit_string_len_set(g, result, total_len);
        free(strs);
        free(lens);
        free(owns);
        return result;
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

static LLVMValueRef emit_expr_member_access(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* 读取控制台当前前景色与背景色属性 */
        if (expr->member.object->kind == AST_IDENTIFIER &&
            expr->member.object->ident.name.len == 7 &&
            memcmp(expr->member.object->ident.name.str, "Console", 7) == 0 &&
            !local_find(locals, expr->member.object->ident.name) &&
            expr->member.name.len == 15) {
            if (memcmp(expr->member.name.str, "ForegroundColor", 15) == 0)
                return emit_console_color_get(g, 0);
            if (memcmp(expr->member.name.str, "BackgroundColor", 15) == 0)
                return emit_console_color_get(g, 1);
        }
        {
            bool is_cpu_obj = false;
            if (expr->member.object->kind == AST_IDENTIFIER &&
                expr->member.object->ident.name.len == 3 &&
                memcmp(expr->member.object->ident.name.str, "Cpu", 3) == 0 &&
                !local_find(locals, expr->member.object->ident.name)) {
                is_cpu_obj = true;
            } else if (expr->member.object->kind == AST_MEMBER_ACCESS &&
                       expr->member.object->member.name.len == 3 &&
                       memcmp(expr->member.object->member.name.str, "Cpu", 3) == 0) {
                is_cpu_obj = true;
            }
            if (is_cpu_obj) {
                int feat_id = 0;
                zan_istr_t mn = expr->member.name;
                if (mn.len == 9 && memcmp(mn.str, "HasPopcnt", 9) == 0) feat_id = 1;
                else if (mn.len == 8 && memcmp(mn.str, "HasLzcnt", 8) == 0) feat_id = 2;
                else if (mn.len == 8 && memcmp(mn.str, "HasSse42", 8) == 0) feat_id = 3;
                else if (mn.len == 7 && memcmp(mn.str, "HasAvx2", 7) == 0) feat_id = 4;
                else if (mn.len == 8 && memcmp(mn.str, "HasAesNi", 8) == 0) feat_id = 5;
                else if (mn.len == 7 && memcmp(mn.str, "HasNeon", 7) == 0) feat_id = 6;
                if (feat_id > 0) {
                    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                    LLVMValueRef id = LLVMConstInt(i32t, feat_id, 0);
                    LLVMValueRef fn = cpu_feature_fn(g);
                    LLVMTypeRef fnty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t }, 1, 0);
                    LLVMValueRef val = zan_call2(g->builder, fnty, fn, &id, 1, "cpu_feat");
                    return zan_icmp(g->builder, LLVMIntNE, val, LLVMConstInt(i32t, 0, 0), "has_feat");
                }
            }
        }
        /* Vector128.Zero / Vector128.AllBitsSet */
        {
            if (is_target_class_obj(expr->member.object, "Vector128", 9, locals)) {
                zan_istr_t mn = expr->member.name;
                LLVMTypeRef v16i8 = LLVMVectorType(LLVMInt8TypeInContext(g->ctx), 16);
                if (mn.len == 4 && memcmp(mn.str, "Zero", 4) == 0) {
                    return v16i8_to_vec128(g, LLVMConstNull(v16i8));
                }
                if (mn.len == 10 && memcmp(mn.str, "AllBitsSet", 10) == 0) {
                    return v16i8_to_vec128(g, LLVMConstAllOnes(v16i8));
                }
            }
        }
        {
            zan_type_t *tit = infer_expr_type(g, expr->member.object, locals);
            if (zan_refl_is_typeinfo(tit)) {
                LLVMValueRef rv = NULL;
                if (refl_emit_typeinfo_member(g,
                        emit_guarded_member_object(g, expr, locals),
                        expr->member.name, NULL, 0, locals, &rv))
                    return rv;
            }
        }
        /* 可空值类型 HasValue 与 Value 属性读取 */
        {
            zan_type_t *nt = infer_expr_type(g, expr->member.object, locals);
            bool is_has = expr->member.name.len == 8 &&
                memcmp(expr->member.name.str, "HasValue", 8) == 0;
            bool is_val = expr->member.name.len == 5 &&
                memcmp(expr->member.name.str, "Value", 5) == 0;
            if (nt && nt->kind == TYPE_NULLABLE && (is_has || is_val)) {
                LLVMValueRef nv = emit_guarded_member_object(g, expr, locals);
                if (llvm_is_nullable(LLVMTypeOf(nv))) {
                    if (is_has) return nullable_has_value(g, nv);
                    emit_runtime_check(g,
                        LLVMBuildNot(g->builder, nullable_has_value(g, nv), "nv.novalue"),
                        expr->loc, "Nullable object must have a value");
                    return nullable_get_payload(g, nv);
                }
            }
        }
        /* Task 属性读取：读取帧结果或完成标志 */
        {
            zan_type_t *ot = infer_expr_type(g, expr->member.object, locals);
            if (ot && ot->kind == TYPE_TASK) {
                bool is_res = expr->member.name.len == 6 &&
                    memcmp(expr->member.name.str, "Result", 6) == 0;
                bool is_comp = expr->member.name.len == 11 &&
                    memcmp(expr->member.name.str, "IsCompleted", 11) == 0;
                if (is_res || is_comp) {
                    LLVMTypeRef ti64 = LLVMInt64TypeInContext(g->ctx);
                    if (is_res && ot->type_arg_count != 1) {
                        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3012,
                            "Task.Result requires a Task<T> value");
                        return LLVMConstInt(ti64, 0, 0);
                    }
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef h = emit_guarded_member_object(g, expr, locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(h)) == LLVMPointerTypeKind)
                        h = LLVMBuildPtrToInt(g->builder, h, ti64, "task.h");
                    else if (LLVMGetIntTypeWidth(LLVMTypeOf(h)) < 64)
                        h = zan_iwiden(g->builder, h, ti64);
                    LLVMValueRef hp = LLVMBuildIntToPtr(g->builder, h, i8ptr, "task.fp");
                    int mode = is_comp ? 2 : 1;
                    return emit_task_member(g, hp,
                        is_res ? ot->type_args[0] : NULL, mode);
                }
            }
        }
        /* 标量类型成员访问防护与拦截 */
        {
            bool is_prim_const = false;
            if (expr->member.object->kind == AST_IDENTIFIER) {
                zan_istr_t on = expr->member.object->ident.name;
                zan_istr_t mn = expr->member.name;
                bool is_scalar_name = (on.len == 3 && memcmp(on.str, "int", 3) == 0) ||
                    (on.len == 4 && (memcmp(on.str, "uint", 4) == 0 ||
                                     memcmp(on.str, "long", 4) == 0 ||
                                     memcmp(on.str, "byte", 4) == 0 ||
                                     memcmp(on.str, "char", 4) == 0)) ||
                    (on.len == 5 && (memcmp(on.str, "ulong", 5) == 0 ||
                                     memcmp(on.str, "short", 5) == 0 ||
                                     memcmp(on.str, "sbyte", 5) == 0 ||
                                     memcmp(on.str, "float", 5) == 0)) ||
                    (on.len == 6 && (memcmp(on.str, "ushort", 6) == 0 ||
                                     memcmp(on.str, "double", 6) == 0));
                is_prim_const = is_scalar_name && (
                    (mn.len == 8 && (memcmp(mn.str, "MaxValue", 8) == 0 ||
                                     memcmp(mn.str, "MinValue", 8) == 0)) ||
                    (mn.len == 3 && memcmp(mn.str, "NaN", 3) == 0) ||
                    (mn.len == 16 && (memcmp(mn.str, "PositiveInfinity", 16) == 0 ||
                                      memcmp(mn.str, "NegativeInfinity", 16) == 0)) ||
                    (mn.len == 7 && memcmp(mn.str, "Epsilon", 7) == 0));
            }
            zan_type_t *mt = infer_expr_type(g, expr->member.object, locals);
            if (mt && !is_prim_const && mt->kind != TYPE_CLASS &&
                mt->kind != TYPE_STRUCT &&
                mt->kind != TYPE_INTERFACE && mt->kind != TYPE_ENUM &&
                mt->kind != TYPE_ARRAY && mt->kind != TYPE_NULLABLE &&
                mt->kind != TYPE_DELEGATE && mt->kind != TYPE_TYPE_PARAM &&
                mt->kind != TYPE_OBJECT && mt->kind != TYPE_ERROR &&
                mt->kind != TYPE_VOID && !is_span_type(mt)) {
                bool is_len = expr->member.name.len == 6 &&
                    memcmp(expr->member.name.str, "Length", 6) == 0;
                if (!(mt->kind == TYPE_STRING && is_len)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3013,
                        "type '%s' has no member '%.*s'",
                        mt->name.str ? mt->name.str : "?",
                        (int)expr->member.name.len, expr->member.name.str);
                    return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
                }
            }
        }
        /* Math.PI → constant */
        if (expr->member.object->kind == AST_IDENTIFIER) {
            zan_istr_t obj = expr->member.object->ident.name;
            if (obj.len == 4 && memcmp(obj.str, "Math", 4) == 0) {
                if (expr->member.name.len == 2 && memcmp(expr->member.name.str, "PI", 2) == 0) {
                    return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), 3.14159265358979323846);
                }
                if (expr->member.name.len == 4 && memcmp(expr->member.name.str, "Sqrt", 4) == 0) {
                    return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
                }
            }
        }

        /* 内置标量类型常量（int.MaxValue、double.NaN 等）发射编译期常量 */
        if (expr->member.object->kind == AST_IDENTIFIER) {
            zan_istr_t obj = expr->member.object->ident.name;
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            bool is_int   = obj.len == 3 && memcmp(obj.str, "int", 3) == 0;
            bool is_uint  = obj.len == 4 && memcmp(obj.str, "uint", 4) == 0;
            bool is_long  = obj.len == 4 && memcmp(obj.str, "long", 4) == 0;
            bool is_ulong = obj.len == 5 && memcmp(obj.str, "ulong", 5) == 0;
            bool is_short = obj.len == 5 && memcmp(obj.str, "short", 5) == 0;
            bool is_ushort= obj.len == 6 && memcmp(obj.str, "ushort", 6) == 0;
            bool is_byte  = obj.len == 4 && memcmp(obj.str, "byte", 4) == 0;
            bool is_sbyte = obj.len == 5 && memcmp(obj.str, "sbyte", 5) == 0;
            bool is_float = obj.len == 5 && memcmp(obj.str, "float", 5) == 0;
            bool is_double= obj.len == 6 && memcmp(obj.str, "double", 6) == 0;
            bool is_char  = obj.len == 4 && memcmp(obj.str, "char", 4) == 0;
            if (is_int || is_uint || is_long || is_ulong || is_short ||
                is_ushort || is_byte || is_sbyte || is_float || is_double ||
                is_char) {
                zan_istr_t m = expr->member.name;
                if (m.len == 8 && memcmp(m.str, "MaxValue", 8) == 0) {
                    if (is_int)    return LLVMConstInt(i32, INT32_MAX, 0);
                    if (is_uint)   return LLVMConstInt(i32, UINT32_MAX, 0);
                    if (is_long)   return LLVMConstInt(i64, INT64_MAX, 0);
                    if (is_ulong)  return LLVMConstInt(i64, UINT64_MAX, 0);
                    if (is_short)  return LLVMConstInt(i32, INT16_MAX, 0);
                    if (is_ushort) return LLVMConstInt(i32, UINT16_MAX, 0);
                    if (is_byte)   return LLVMConstInt(i32, UINT8_MAX, 0);
                    if (is_sbyte)  return LLVMConstInt(i32, INT8_MAX, 0);
                    if (is_char)   return LLVMConstInt(i64, 0xFFFF, 0);
                    if (is_float)  return LLVMConstReal(LLVMFloatTypeInContext(g->ctx), 3.40282346638528859812e+38);
                    if (is_double) return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), 1.7976931348623157e+308);
                }
                if (m.len == 8 && memcmp(m.str, "MinValue", 8) == 0) {
                    if (is_int)   return LLVMConstInt(i32, (unsigned long long)INT32_MIN, 1);
                    if (is_long)  return LLVMConstInt(i64, (unsigned long long)INT64_MIN, 1);
                    if (is_short) return LLVMConstInt(i32, (unsigned long long)INT16_MIN, 1);
                    if (is_sbyte) return LLVMConstInt(i32, (unsigned long long)INT8_MIN, 1);
                    if (is_float) return LLVMConstReal(LLVMFloatTypeInContext(g->ctx), -3.40282346638528859812e+38);
                    if (is_double) return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), -1.7976931348623157e+308);
                }
                if (is_double || is_float) {
                    /* 浮点特殊常量位精确表示（如正负无穷与 NaN） */
                    if (m.len == 3 && memcmp(m.str, "NaN", 3) == 0) {
                        if (is_float)
                            return LLVMConstBitCast(
                                LLVMConstInt(i32, 0x7FC00000U, 0),
                                LLVMFloatTypeInContext(g->ctx));
                        return LLVMConstBitCast(
                            LLVMConstInt(i64, 0x7FF8000000000000ULL, 0),
                            LLVMDoubleTypeInContext(g->ctx));
                    }
                    if (m.len == 16 && memcmp(m.str, "PositiveInfinity", 16) == 0) {
                        if (is_float)
                            return LLVMConstBitCast(
                                LLVMConstInt(i32, 0x7F800000U, 0),
                                LLVMFloatTypeInContext(g->ctx));
                        return LLVMConstBitCast(
                            LLVMConstInt(i64, 0x7FF0000000000000ULL, 0),
                            LLVMDoubleTypeInContext(g->ctx));
                    }
                    if (m.len == 16 && memcmp(m.str, "NegativeInfinity", 16) == 0) {
                        if (is_float)
                            return LLVMConstBitCast(
                                LLVMConstInt(i32, 0xFF800000U, 0),
                                LLVMFloatTypeInContext(g->ctx));
                        return LLVMConstBitCast(
                            LLVMConstInt(i64, 0xFFF0000000000000ULL, 0),
                            LLVMDoubleTypeInContext(g->ctx));
                    }
                }
                if (is_float && m.len == 7 && memcmp(m.str, "Epsilon", 7) == 0)
                    return LLVMConstReal(LLVMFloatTypeInContext(g->ctx),
                        1.4012984643248170709e-45);
            }
        }

        if (expr->member.name.len == 6 &&
            memcmp(expr->member.name.str, "Length", 6) == 0) {
            zan_type_t *st = infer_expr_type(g, expr->member.object, locals);
            if (is_span_type(st)) {
                LLVMValueRef span_val = emit_guarded_member_object(g, expr, locals);
                LLVMValueRef len = LLVMBuildExtractValue(g->builder, span_val, 1, "sp.len");
                return LLVMBuildTrunc(g->builder, len,
                    LLVMInt32TypeInContext(g->ctx), "sp.len32");
            }
        }

        if (expr->member.name.len == 5 && memcmp(expr->member.name.str, "Count", 5) == 0) {
            zan_type_t *lt = infer_expr_type(g, expr->member.object, locals);
            if (lt && type_named(lt, "List", 4)) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef raw_ptr = emit_guarded_member_object(g, expr, locals);
                LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                    LLVMPointerType(g->list_struct_type, 0), "lptr");
                LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                emit_release_owned_call_temp(g, expr->member.object, raw_ptr, locals);
                return count;
            }
        }

        /* Dict.Count：读取字典当前项数 */
        if (expr->member.name.len == 5 && memcmp(expr->member.name.str, "Count", 5) == 0) {
            zan_type_t *dt = infer_expr_type(g, expr->member.object, locals);
            if (dt && type_named(dt, "Dict", 4)) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef raw = emit_guarded_member_object(g, expr, locals);
                LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                    LLVMPointerType(g->dict_struct_type, 0), "dp");
                LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "dcnt");
                emit_release_owned_call_temp(g, expr->member.object, raw, locals);
                return cnt;
            }
        }

        /* Dict.Keys 与 Dict.Values：将键/值集合拷贝至独立 List 实例 */
        if ((expr->member.name.len == 4 && memcmp(expr->member.name.str, "Keys", 4) == 0) ||
            (expr->member.name.len == 6 && memcmp(expr->member.name.str, "Values", 6) == 0)) {
            zan_type_t *dt = infer_expr_type(g, expr->member.object, locals);
            if (dt && type_named(dt, "Dict", 4)) {
                bool want_keys = (expr->member.name.len == 4);
                zan_type_t *elem = want_keys ? dict_key_type(g, dt) : dict_value_type(dt);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef raw = emit_guarded_member_object(g, expr, locals);
                LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                    LLVMPointerType(g->dict_struct_type, 0), "dp");
                LLVMValueRef dict_value_words = load_dict_value_words(g, raw);
                LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "dcnt");
                LLVMValueRef srcp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp,
                    want_keys ? 2 : 3, want_keys ? "kp" : "vp");
                LLVMTypeRef src_slot_ty = want_keys ? i8ptr : i64;
                LLVMValueRef src = LLVMBuildLoad2(g->builder,
                    LLVMPointerType(src_slot_ty, 0), srcp, "dsrc");
                LLVMValueRef list_raw = emit_alloc_rc_collection(g, expr, 24, 1, elem);
                LLVMValueRef lp = LLVMBuildBitCast(g->builder, list_raw,
                    LLVMPointerType(g->list_struct_type, 0), "lp");
                zan_store_fit(g, cnt,
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "lcnt"));
                LLVMValueRef cap = zan_add(g->builder, cnt,
                    LLVMConstInt(i64, 8, 0), "lcap");
                zan_store_fit(g, cap,
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 1, "lcapp"));
                unsigned elem_words = elem_slot_words(g, elem);
                LLVMValueRef data_words = zan_mul(g->builder, cap,
                    LLVMConstInt(i64, elem_words, 0), "lwords");
                LLVMValueRef data_size = zan_mul(g->builder, data_words,
                    LLVMConstInt(i64, 8, 0), "lsz");
                LLVMValueRef data = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g),
                    (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), data_size }, 2, "ldata");
                zan_irgen_emit_oom_check(g, g->current_fn, data);
                LLVMValueRef data_typed = LLVMBuildBitCast(g->builder, data,
                    LLVMPointerType(i64, 0), "ldp");
                zan_store_fit(g, data_typed,
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "ldf"));
                LLVMValueRef idx_a = emit_entry_alloca(g, i64, "dk.i");
                zan_store_fit(g, LLVMConstInt(i64, 0, 0), idx_a);
                LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dk.cond");
                LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dk.body");
                LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dk.done");
                LLVMBuildBr(g->builder, cond_bb);
                LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                LLVMBuildCondBr(g->builder,
                    zan_icmp(g->builder, LLVMIntUGE, ci, cnt, "cdone"),
                    done_bb, body_bb);
                LLVMPositionBuilderAtEnd(g->builder, body_bb);
                LLVMValueRef src_index = want_keys ? ci
                    : zan_mul(g->builder, ci, dict_value_words, "dv.word");
                LLVMValueRef sslot = LLVMBuildGEP2(g->builder, src_slot_ty,
                    src, &src_index, 1, "ssl");
                LLVMValueRef sval = want_keys
                    ? LLVMBuildLoad2(g->builder, src_slot_ty, sslot, "sv")
                    : load_collection_slot_value(g, elem, sslot);
                LLVMValueRef dst_index = slot_word_index(g, ci, elem_words);
                LLVMValueRef dslot = LLVMBuildGEP2(g->builder, i64,
                    data_typed, &dst_index, 1, "dsl");
                emit_collection_slot_store(g, elem, i64, dslot, sval, NULL, locals, 0);
                zan_store_fit(g,
                    zan_add(g->builder, ci, LLVMConstInt(i64, 1, 0), "ni"), idx_a);
                LLVMBuildBr(g->builder, cond_bb);
                LLVMPositionBuilderAtEnd(g->builder, done_bb);
                emit_release_owned_call_temp(g, expr->member.object, raw, locals);
                return LLVMBuildBitCast(g->builder, lp, i8ptr, "keysv");
            }
        }

        /* array.Length：从数组头部读取元素个数，空引用返回 0 */
        if (expr->member.name.len == 5 &&
            memcmp(expr->member.name.str, "Count", 5) == 0) {
            zan_type_t *at = infer_expr_type(g, expr->member.object, locals);
            if (at && at->kind == TYPE_ARRAY) {
                LLVMValueRef arr = emit_guarded_member_object(g, expr, locals);
                if (LLVMGetTypeKind(LLVMTypeOf(arr)) == LLVMPointerTypeKind) {
                    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, arr,
                        LLVMConstNull(LLVMTypeOf(arr)), "arr.null");
                    emit_runtime_check(g, isnull, expr->member.object->loc,
                        "null reference where an array is required (.Count)");
                    arr = emit_soft_base_select(g, arr, isnull,
                                                expr->member.object->loc);
                }
                LLVMValueRef len = zan_array_len(g, arr);
                emit_release_owned_call_temp(g, expr->member.object, arr, locals);
                return len;
            }
        }
        if (expr->member.name.len == 6 &&
            memcmp(expr->member.name.str, "Length", 6) == 0) {
            zan_type_t *at = infer_expr_type(g, expr->member.object, locals);
            if (at && at->kind == TYPE_ARRAY) {
                LLVMValueRef arr = emit_guarded_member_object(g, expr, locals);
                if (LLVMGetTypeKind(LLVMTypeOf(arr)) == LLVMPointerTypeKind) {
                    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, arr,
                        LLVMConstNull(LLVMTypeOf(arr)), "arr.null");
                    emit_runtime_check(g, isnull, expr->member.object->loc,
                        "null reference where an array is required (.Length)");
                    arr = emit_soft_base_select(g, arr, isnull,
                                                expr->member.object->loc);
                }
                LLVMValueRef len = zan_array_len(g, arr);
                emit_release_owned_call_temp(g, expr->member.object, arr, locals);
                return len;
            }
        }

        if (expr->member.name.len == 6 && memcmp(expr->member.name.str, "Length", 6) == 0) {
            zan_type_t *sbt = infer_expr_type(g, expr->member.object, locals);
            if (sbt && type_named(sbt, "StringBuilder", 13)) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef raw = emit_guarded_member_object(g, expr, locals);
                LLVMValueRef sbp = LLVMBuildBitCast(g->builder, raw,
                    LLVMPointerType(g->sb_struct_type, 0), "sbp");
                LLVMValueRef cptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 0, "sbcp");
                LLVMValueRef sblen = LLVMBuildLoad2(g->builder, i64, cptr, "sblen");
                emit_release_owned_call_temp(g, expr->member.object, raw, locals);
                return sblen;
            }
        }

        /* String.Length 属性：读取头部缓存长度，必要时回退至 strlen 扫描 */
        if (expr->member.name.len == 6 && memcmp(expr->member.name.str, "Length", 6) == 0 &&
            recv_is_stringlike(g, expr->member.object, locals)) {
            LLVMValueRef obj_val = emit_guarded_member_object(g, expr, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                LLVMValueRef len = emit_string_length(g, obj_val, expr->loc);
                emit_release_owned_call_temp(g, expr->member.object, obj_val, locals);
                return len;
            }
        }

        /* ConsoleColor 枚举成员读取：映射至 0..15 固定序数 */
        if (expr->member.object->kind == AST_IDENTIFIER &&
            expr->member.object->ident.name.len == 12 &&
            memcmp(expr->member.object->ident.name.str, "ConsoleColor", 12) == 0) {
            static const struct { const char *n; int l; } cc[16] = {
                {"Black",5},{"DarkBlue",8},{"DarkGreen",9},{"DarkCyan",8},
                {"DarkRed",7},{"DarkMagenta",11},{"DarkYellow",10},{"Gray",4},
                {"DarkGray",8},{"Blue",4},{"Green",5},{"Cyan",4},
                {"Red",3},{"Magenta",7},{"Yellow",6},{"White",5} };
            for (int i = 0; i < 16; i++) {
                if (expr->member.name.len == cc[i].l &&
                    memcmp(expr->member.name.str, cc[i].n, (size_t)cc[i].l) == 0)
                    return LLVMConstInt(LLVMInt64TypeInContext(g->ctx),
                                        (uint64_t)i, 0);
            }
        }

        /* 枚举成员访问：EnumType.MemberName 降解为整型常量 */
        if (expr->member.object->kind == AST_IDENTIFIER) {
            zan_symbol_t *enum_sym = zan_binder_lookup(g->binder, expr->member.object->ident.name);
            if (enum_sym && enum_sym->kind == SYM_ENUM) {
                int64_t enum_val = 0;
                for (int ei = 0; ei < enum_sym->member_count; ei++) {
                    if (enum_sym->members[ei]->kind == SYM_ENUM_MEMBER) {
                        if (enum_sym->members[ei]->name.len == expr->member.name.len &&
                            memcmp(enum_sym->members[ei]->name.str, expr->member.name.str,
                                   (size_t)expr->member.name.len) == 0) {
                            zan_ast_node_t *em_decl = enum_sym->members[ei]->decl;
                            if (em_decl && em_decl->kind == AST_ENUM_MEMBER &&
                                em_decl->enum_member.value &&
                                em_decl->enum_member.value->kind == AST_INT_LITERAL) {
                                enum_val = em_decl->enum_member.value->int_val;
                            }
                            return LLVMConstInt(LLVMInt64TypeInContext(g->ctx),
                                               (uint64_t)(uint32_t)enum_val, 0);
                        }
                        zan_ast_node_t *em_decl = enum_sym->members[ei]->decl;
                        if (em_decl && em_decl->kind == AST_ENUM_MEMBER &&
                            em_decl->enum_member.value &&
                            em_decl->enum_member.value->kind == AST_INT_LITERAL) {
                            enum_val = em_decl->enum_member.value->int_val + 1;
                        } else {
                            enum_val++;
                        }
                    }
                }
                return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
            }
        }

        /* 静态字段访问：ClassName.StaticField 直接读取全局变量 */
        if (expr->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->member.object->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder, expr->member.object->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *fs = get_field_sym(cs, expr->member.name);
                zan_symbol_t *getter = property_getter_sym(g, fs);
                if (getter) {
                    return emit_property_getter_call(g, getter, cs->type, NULL,
                        expr->member.object, locals);
                }
                LLVMValueRef gv = get_static_field_global(g, cs, fs,
                    static_access_inst(g, expr->member.object));
                if (gv) {
                    LLVMTypeRef ft = fs->type ? map_type(g, fs->type)
                                              : LLVMInt64TypeInContext(g->ctx);
                    return promote_loaded(g,
                        LLVMBuildLoad2(g->builder, ft, gv, "sfld"), fs->type);
                }
            }
        }

        /* 静态方法引用作为委托传递：生成静态方法组记录 */
        if (expr->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->member.object->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder, expr->member.object->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *ms = get_method_sym(cs, expr->member.name);
                if (ms) {
                    if (target_is_wasm32(g)) {
                        LLVMValueRef clo = emit_method_group_closure(g, ms, NULL,
                                                                     expr->loc);
                        if (clo) return clo;
                    }
                    for (int fi = irgen_find_function(g, ms); fi >= 0; fi = -1) {
                        if (g->functions[fi].sym == ms) {
                            return g->functions[fi].fn;
                        }
                    }
                }
            }
        }

        if (expr->member.object->kind == AST_IDENTIFIER) {
            local_var_t *local = local_find(locals, expr->member.object->ident.name);
            if (local && local->type && (local->type->kind == TYPE_STRUCT || local->type->kind == TYPE_CLASS)) {
                zan_symbol_t *type_sym = local->type->sym;
                if (type_sym) {
                    int fi = get_field_index(type_sym, expr->member.name);
                    if (fi >= 0) {
                        zan_symbol_t *psym = get_field_sym(type_sym, expr->member.name);
                        zan_symbol_t *getter = property_getter_sym(g, psym);
                        if (getter) {
                            LLVMTypeRef st = get_struct_llvm_type(g, type_sym);
                            LLVMValueRef rval = struct_base_ptr(g, local, st);
                            return emit_property_getter_call(g, getter,
                                local->type, rval, expr->member.object, locals);
                        }
                        LLVMTypeRef st = get_struct_llvm_type(g, type_sym);
                        if (st) {
                            /* 结构体字段访问：获取指针并通过 GEP 寻址目标字段 */
                            LLVMValueRef struct_ptr = struct_base_ptr(g, local, st);
                            if (local->type->kind == TYPE_CLASS &&
                                LLVMGetTypeKind(LLVMTypeOf(struct_ptr)) == LLVMPointerTypeKind) {
                                LLVMValueRef isnull = zan_icmp(g->builder,
                                    LLVMIntEQ, struct_ptr,
                                    LLVMConstNull(LLVMTypeOf(struct_ptr)),
                                    "recv.null");
                                char recv_msg[256];
                                snprintf(recv_msg, sizeof(recv_msg),
                                    "null reference: receiver '%.*s' is null",
                                    (int)expr->member.object->ident.name.len,
                                    expr->member.object->ident.name.str);
                                emit_runtime_check(g, isnull,
                                    expr->member.object->loc, recv_msg);
                                struct_ptr = emit_soft_base_select(g, struct_ptr,
                                    isnull, expr->member.object->loc);
                            }
                            LLVMValueRef field_ptr = emit_field_ptr(g, type_sym, st, struct_ptr, fi, "fld");
                            zan_symbol_t *fsym = get_field_sym(type_sym, expr->member.name);
                            zan_type_t *fty = fsym
                                ? field_type_at(g, fsym, local->type)
                                : NULL;
                            LLVMTypeRef field_type = fty ? map_type(g, fty)
                                : LLVMInt64TypeInContext(g->ctx);
                            LLVMValueRef fv0 =
                                (fsym && (fsym->modifiers & MOD_WEAK))
                                ? emit_weak_field_load(g, field_ptr, field_type)
                                : LLVMBuildLoad2(g->builder, field_type, field_ptr, "fval");
                            LLVMValueRef fv = promote_loaded(g, fv0, fty);
                            return fv;
                        }
                    }
                }
            }
        }

        {
            zan_symbol_t *cls = expr_class_sym(g, expr->member.object, locals);
            if (cls) {
                int fi = get_field_index(cls, expr->member.name);
                if (fi >= 0) {
                    /* 索引结构体元素字段读取 (l[i].field) */
                    if (expr->member.object->kind == AST_INDEX &&
                        cls->kind == SYM_STRUCT) {
                        zan_type_t *et = infer_expr_type(g,
                            expr->member.object, locals);
                        LLVMTypeRef st = get_struct_llvm_type(g, cls);
                        LLVMValueRef ep = (st && !type_is_binding(et))
                            ? emit_struct_elem_ptr(g, expr->member.object, locals)
                            : NULL;
                        if (ep) {
                            LLVMValueRef sptr = LLVMBuildBitCast(g->builder, ep,
                                LLVMPointerType(st, 0), "ep.s");
                            zan_symbol_t *gpsym = get_field_sym(cls,
                                expr->member.name);
                            zan_symbol_t *getter = property_getter_sym(g, gpsym);
                            if (getter) {
                                return emit_property_getter_call(g, getter, et,
                                    sptr, expr->member.object, locals);
                            }
                            LLVMValueRef field_ptr = emit_field_ptr(g, cls, st,
                                sptr, fi, "gfld");
                            zan_type_t *fty = gpsym
                                ? field_type_at(g, gpsym, et) : NULL;
                            LLVMTypeRef ft = fty ? map_type(g, fty)
                                : LLVMInt64TypeInContext(g->ctx);
                            LLVMValueRef gfv0 =
                                (gpsym && (gpsym->modifiers & MOD_WEAK))
                                ? emit_weak_field_load(g, field_ptr, ft)
                                : LLVMBuildLoad2(g->builder, ft, field_ptr, "gfval");
                            return promote_loaded(g, gfv0, fty);
                        }
                    }
                    zan_symbol_t *psym = get_field_sym(cls, expr->member.name);
                    zan_symbol_t *getter = property_getter_sym(g, psym);
                    if (getter) {
                        zan_type_t *rct = infer_expr_type(g, expr->member.object, locals);
                        LLVMValueRef rval = emit_guarded_member_object(g, expr, locals);
                        /* base.Prop 属性读取：直接调用基类 getter，绕过虚方法分发 */
                        if (expr->member.object->kind == AST_BASE_EXPR)
                            return emit_property_getter_call(g, getter, NULL,
                                rval, expr->member.object, locals);
                        return emit_property_getter_call(g, getter, rct, rval,
                                                         expr->member.object,
                                                         locals);
                    }
                    LLVMTypeRef st = get_struct_llvm_type(g, cls);
                    LLVMValueRef obj_val = emit_guarded_member_object(g, expr, locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMStructTypeKind) {
                        LLVMValueRef sfv = LLVMBuildExtractValue(g->builder,
                            obj_val, (unsigned)fi, "sfval");
                        zan_symbol_t *fsym = get_field_sym(cls, expr->member.name);
                        zan_type_t *rct = infer_expr_type(g, expr->member.object, locals);
                        if (fsym) {
                            zan_type_t *ct = field_type_at(g, fsym, rct);
                            if (ct != fsym->type)
                                sfv = emit_boundary_coerce(g, sfv, map_type(g, ct));
                        }
                        return sfv;
                    }
                    if (st && LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                        LLVMValueRef field_ptr = emit_field_ptr(g, cls, st, obj_val, fi, "gfld");
                        zan_symbol_t *fsym = get_field_sym(cls, expr->member.name);
                        zan_type_t *rct = infer_expr_type(g, expr->member.object, locals);
                        zan_type_t *fty = fsym
                            ? field_type_at(g, fsym, rct) : NULL;
                        LLVMTypeRef ft = fty ? map_type(g, fty)
                                             : LLVMInt64TypeInContext(g->ctx);
                        LLVMValueRef gfv0 =
                            (fsym && (fsym->modifiers & MOD_WEAK))
                            ? emit_weak_field_load(g, field_ptr, ft)
                            : LLVMBuildLoad2(g->builder, ft, field_ptr, "gfval");
                        LLVMValueRef gfv = promote_loaded(g, gfv0, fty);
                        return finish_member_of_temp(g, expr, locals, rct,
                                                     obj_val, gfv);
                    }
                }
            }
        }
        /* 实例方法组作为委托传递：绑定接收者并保留引用 */
        {
            zan_symbol_t *cls = expr_class_sym(g, expr->member.object, locals);
            if (cls) {
                zan_symbol_t *ms = get_method_sym(cls, expr->member.name);
                if (ms && ms->decl && ms->decl->kind == AST_METHOD_DECL &&
                    (ms->decl->method_decl.modifiers & MOD_STATIC) == 0) {
                    LLVMValueRef recv = emit_guarded_member_object(g, expr, locals);
                    LLVMValueRef clo = recv
                        ? emit_method_group_closure(g, ms, recv, expr->loc)
                        : NULL;
                    if (clo) {
                        /* 闭包捕获接收者并维持 +1 强引用 */
                        zan_type_t *rct = infer_expr_type(g, expr->member.object, locals);
                        if (receiver_is_owned_temp(g, expr->member.object, locals,
                                                   rct, recv))
                            emit_rc_release_for_type(g, rct, recv);
                        return clo;
                    }
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3014,
                        "instance method group '%.*s' cannot be used as a value",
                        (int)expr->member.name.len, expr->member.name.str);
                    return LLVMConstNull(
                        LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
                }
            }
        }
        /* 健壮性：未定义字段读取安全报错与诊断 */
        {
            zan_symbol_t *rcls = expr_class_sym(g, expr->member.object, locals);
            if (!rcls && expr->member.object->kind == AST_IDENTIFIER &&
                !local_find(locals, expr->member.object->ident.name)) {
                zan_symbol_t *ts = zan_binder_lookup(g->binder,
                                                     expr->member.object->ident.name);
                if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT))
                    rcls = ts;
            }
            if (rcls && (rcls->kind == SYM_CLASS || rcls->kind == SYM_STRUCT) &&
                !type_declares_member(rcls, expr->member.name)) {
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3009,
                    "'%.*s' has no member '%.*s'",
                    (int)rcls->name.len, rcls->name.str,
                    (int)expr->member.name.len, expr->member.name.str);
            } else if (!rcls) {
                zan_type_t *rt = infer_expr_type(g, expr->member.object, locals);
                if (rt && is_builtin_collection_type(rt))
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3009,
                        "'%.*s' has no member '%.*s'",
                        (int)rt->name.len, rt->name.str,
                        (int)expr->member.name.len, expr->member.name.str);
                /* 链式调用未定义成员安全处理 */
                zan_ast_node_t *ro = expr->member.object;
                if (!rt && ro->kind == AST_CALL && ro->call.callee &&
                    ro->call.callee->kind == AST_MEMBER_ACCESS) {
                    zan_ast_node_t *inner = ro->call.callee;
                    zan_symbol_t *icls = expr_class_sym(g, inner->member.object,
                                                        locals);
                    if (icls &&
                        (icls->kind == SYM_CLASS || icls->kind == SYM_STRUCT) &&
                        !type_declares_member(icls, inner->member.name))
                        zan_diag_emit_code(g->diag, DIAG_ERROR, inner->loc, 3009,
                            "'%.*s' has no member '%.*s'",
                            (int)icls->name.len, icls->name.str,
                            (int)inner->member.name.len,
                            inner->member.name.str);
                }
            }
        }
        if (g->diag->error_count == 0) {
            zan_symbol_t *rcls = expr_class_sym(g, expr->member.object, locals);
            if (rcls) {
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3009,
                    "'%.*s' has no member '%.*s'",
                    (int)rcls->name.len, rcls->name.str,
                    (int)expr->member.name.len, expr->member.name.str);
            } else {
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3015,
                    "cannot resolve member '%.*s'",
                    (int)expr->member.name.len, expr->member.name.str);
            }
        }
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

/* 临时容器对象访问完成后及时释放引用 */
static LLVMValueRef finish_index_of_temp(zan_irgen_t *g, zan_ast_node_t *expr,
                                         local_scope_t *locals,
                                         zan_type_t *container_type,
                                         zan_type_t *elem_type,
                                         LLVMValueRef container,
                                         LLVMValueRef elem) {
    if (!container || !container_type ||
        !expr_yields_owned_rc_value(g, expr->index.object, locals) ||
        expr_is_local_ident(expr->index.object, locals) ||
        !is_rc_managed_type(container_type))
        return elem;
    if (elem_type && is_rc_managed_type(elem_type) &&
        LLVMGetTypeKind(LLVMTypeOf(elem)) == LLVMPointerTypeKind)
        emit_rc_retain_for_type(g, elem_type, elem);
    emit_rc_release_for_type(g, container_type, container);
    return elem;
}

static LLVMValueRef emit_expr_index(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* 实例索引器 obj[i] 降解为 op_index 方法调用 */
        {
            zan_type_t *oit = infer_expr_type(g, expr->index.object, locals);
            if (oit && (oit->kind == TYPE_CLASS || oit->kind == TYPE_STRUCT) &&
                oit->sym) {
                zan_istr_t op_istr = {(char *)"op_index", 8};
                zan_ast_node_t *op_call = zan_ast_new(g->arena, AST_CALL, expr->loc);
                op_call->call.callee = NULL;
                zan_ast_list_init(&op_call->call.args);
                zan_ast_list_init(&op_call->call.type_args);
                zan_ast_list_push(&op_call->call.args, expr->index.index, g->arena);
                zan_symbol_t *op_sym = resolve_op_overload(g, oit->sym,
                                                           op_istr, op_call, locals);
                if (op_sym) {
                    for (int fi = irgen_find_function(g, op_sym); fi >= 0; fi = -1) {
                        if (g->functions[fi].sym == op_sym) {
                            LLVMValueRef recv_val = emit_expr(g, expr->index.object, locals);
                            LLVMValueRef *call_args = (LLVMValueRef *)calloc(2, sizeof(LLVMValueRef));
                            call_args[0] = recv_val;
                            if (LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMStructTypeKind) {
                                LLVMValueRef rslot = emit_entry_alloca(g,
                                    LLVMTypeOf(recv_val), "opx.recv");
                                LLVMBuildStore(g->builder, recv_val, rslot);
                                call_args[0] = rslot;
                            }
                            int recv_eh_pushed = 0;
                            if (oit->kind == TYPE_CLASS &&
                                !expr_is_local_ident(expr->index.object, locals) &&
                                expr_yields_owned_rc_value(g, expr->index.object, locals) &&
                                LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMPointerTypeKind) {
                                emit_eh_tmp_push(g, recv_val);
                                recv_eh_pushed = 1;
                            }
                            call_args[1] = emit_arg_typed(g, expr->index.index,
                                method_param_type_at(g, op_sym,
                                    op_index_param_offset(op_sym), NULL,
                                    expr->index.object, locals), locals);
                            LLVMTypeRef mft = g->functions[fi].fn_type;
                            LLVMValueRef mfn = route_generic_method(g, oit,
                                op_sym, g->functions[fi].fn, mft, &mft);
                            const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "opx";
                            LLVMValueRef result = emit_dispatch_call(g,
                                oit->sym, op_sym, mfn, mft, call_args, 2, cn);
                            result = coerce_generic_result(g, result, op_sym, oit);
                            if (recv_eh_pushed) emit_eh_tmp_pop(g);
                            emit_release_owned_call_temp(g, expr->index.object,
                                recv_val, locals);
                            emit_release_owned_call_temp(g, expr->index.index,
                                call_args[1], locals);
                            free(call_args);
                            return result;
                        }
                    }
                }
            }
        }
        {
            zan_type_t *sot = infer_expr_type(g, expr->index.object, locals);
            if (is_span_type(sot)) {
                zan_type_t *et = container_elem_type(sot);
                LLVMTypeRef elem_llvm = span_elem_storage_type(g, et);
                LLVMValueRef span_val = emit_expr(g, expr->index.object, locals);
                LLVMValueRef base = LLVMBuildExtractValue(g->builder, span_val, 0, "spx.base");
                LLVMValueRef typed = LLVMBuildBitCast(g->builder, base,
                    LLVMPointerType(elem_llvm, 0), "spx.p");
                LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
                LLVMValueRef span_len = LLVMBuildExtractValue(g->builder, span_val,
                    1, "spx.len");
                emit_index_bounds_check(g, idx, span_len, expr->loc, "span");
                if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                    LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64)
                    idx = LLVMBuildSExt(g->builder, idx,
                        LLVMInt64TypeInContext(g->ctx), "spx.ix");
                idx = emit_index_safe_bounds(g, idx, span_len, expr->loc, "span");
                LLVMValueRef ep = LLVMBuildGEP2(g->builder, elem_llvm, typed, &idx, 1, "spx.ep");
                LLVMValueRef ld = LLVMBuildLoad2(g->builder, elem_llvm, ep, "spx.elem");
                LLVMSetAlignment(ld, 1);
                return promote_loaded(g, ld, et);
            }
        }
        LLVMValueRef arr_ptr = NULL;
        zan_type_t *arr_type = NULL;
        int is_list = 0;
        if (expr->index.object->kind == AST_IDENTIFIER) {
            local_var_t *local = local_find(locals, expr->index.object->ident.name);
            if (local) {
                arr_ptr = LLVMBuildLoad2(g->builder, LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                    local->alloca, "arrload");
                arr_type = local->type;
                if (arr_type && type_named(arr_type, "List", 4)) {
                    is_list = 1;
                }
            } else if (g->current_type_sym) {
                zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->index.object->ident.name);
                if (fsym) {
                    arr_type = fsym->type;
                    arr_ptr = emit_expr(g, expr->index.object, locals);
                    if (arr_type && type_named(arr_type, "List", 4)) {
                        is_list = 1;
                    }
                }
            }
        } else if (expr->index.object->kind == AST_MEMBER_ACCESS) {
            /* 字段数组索引访问：加载字段指针后进行数组寻址 */
            arr_type = member_access_field_type(g, locals, expr->index.object);
            if (!arr_type)
                arr_type = infer_expr_type(g, expr->index.object, locals);
            if (arr_type) {
                arr_ptr = emit_expr(g, expr->index.object, locals);
                if (type_named(arr_type, "List", 4)) {
                    is_list = 1;
                }
            }
        } else {
            /* 通用索引寻址：支持任意产生集合/数组的表达式 */
            arr_type = infer_expr_type(g, expr->index.object, locals);
            if (arr_type) {
                arr_ptr = emit_expr(g, expr->index.object, locals);
                if (type_named(arr_type, "List", 4)) {
                    is_list = 1;
                }
            }
        }
        arr_type = concretize(g, arr_type);
        if (arr_ptr && is_list) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, arr_ptr,
                LLVMPointerType(g->list_struct_type, 0), "lptr");
            LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
            LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
            LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
            LLVMValueRef count_field = LLVMBuildStructGEP2(g->builder,
                g->list_struct_type, list_ptr, 0, "countf");
            LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_field,
                "count");
            emit_index_bounds_check(g, idx, count, expr->loc, "list");
            if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                idx = LLVMBuildSExt(g->builder, idx, i64, "ext");
            }
            idx = emit_index_safe_bounds(g, idx, count, expr->loc, "list");
            zan_type_t *et = container_elem_type(arr_type);
            LLVMValueRef widx = slot_word_index(g, idx, elem_slot_words(g, et));
            LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &widx, 1, "ep");
            LLVMTypeRef em = et ? map_type(g, et) : NULL;
            if (em && LLVMGetTypeKind(em) == LLVMStructTypeKind)
                return finish_index_of_temp(g, expr, locals, arr_type, et,
                    arr_ptr, load_struct_from_slot(g, elem_ptr, em));
            LLVMValueRef raw = LLVMBuildLoad2(g->builder, i64, elem_ptr, "elem");
            /* 集合物理 i64 槽与非整型元素间的重解释转换 */
            LLVMValueRef out = raw;
            if (em) {
                LLVMTypeKind mk = LLVMGetTypeKind(em);
                if (mk == LLVMPointerTypeKind)
                    out = LLVMBuildIntToPtr(g->builder, raw, em, "elp");
                else if (mk == LLVMDoubleTypeKind)
                    out = LLVMBuildBitCast(g->builder, raw, em, "elf");
                else if (mk == LLVMFloatTypeKind) {
                    /* 浮点槽位 32 位位模式在 64 位槽内的保持 */
                    LLVMValueRef narrow = LLVMBuildTrunc(g->builder, raw,
                        LLVMInt32TypeInContext(g->ctx), "elf32.t");
                    out = LLVMBuildBitCast(g->builder, narrow, em, "elf32.f");
                }
            }
            return finish_index_of_temp(g, expr, locals, arr_type, et,
                                        arr_ptr, out);
        }
        /* Dict 索引读取：哈希探测查找条目并提取值字段 */
        if (arr_ptr && arr_type && type_named(arr_type, "Dict", 4)) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef dp = LLVMBuildBitCast(g->builder, arr_ptr,
                LLVMPointerType(g->dict_struct_type, 0), "dp");
            LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
            LLVMValueRef vs = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp, "vs");
            LLVMValueRef search = coerce_dict_key(g, emit_expr(g, expr->index.index, locals), dict_key_type(g, arr_type));
            zan_type_t *dvt = dict_value_type(arr_type);
            LLVMTypeRef value_llvm = dvt ? map_type(g, dvt) : i64;
            LLVMValueRef res = emit_entry_alloca(g, value_llvm, "dres");
            zan_store_fit(g, LLVMConstNull(value_llvm), res);
            LLVMValueRef found = emit_dict_find(g, arr_type, arr_ptr, search);
            LLVMValueRef hit = zan_icmp(g->builder, LLVMIntSGE, found,
                LLVMConstInt(i64, 0, 0), "dihit");
            LLVMBasicBlockRef hit_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "di.hit");
            LLVMBasicBlockRef miss_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "di.miss");
            LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "di.done");
            LLVMBuildCondBr(g->builder, hit, hit_bb, miss_bb);
            /* 缺 key 读取不能静默返回默认值：至少走 soft runtime error 通道
             * （C# 语义是抛 KeyNotFoundException，见 TASKS.md A508）。 */
            LLVMPositionBuilderAtEnd(g->builder, miss_bb);
            emit_runtime_check(g, LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0),
                expr->loc, "Dict key not found in indexer read");
            LLVMBuildBr(g->builder, done_bb);
            LLVMPositionBuilderAtEnd(g->builder, hit_bb);
            LLVMValueRef word = zan_mul(g->builder, found,
                load_dict_value_words(g, arr_ptr), "di.word");
            LLVMValueRef vslot = LLVMBuildGEP2(g->builder, i64, vs,
                &word, 1, "vsl");
            zan_store_fit(g, load_collection_slot_value(g, dvt, vslot), res);
            LLVMBuildBr(g->builder, done_bb);
            LLVMPositionBuilderAtEnd(g->builder, done_bb);
            emit_release_owned_call_temp(g, expr->index.index, search, locals);
            LLVMValueRef dout = LLVMBuildLoad2(g->builder, value_llvm, res, "dval");
            return finish_index_of_temp(g, expr, locals, arr_type, dvt,
                                        arr_ptr, dout);
        }
        /* 字符串索引访问：读取单字节 (i8) 并零扩展为字符码 */
        if (arr_ptr && arr_type && arr_type->kind == TYPE_STRING) {
            LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
            if (expr_has_reliable_string_bounds(expr->index.object, locals)) {
                arr_ptr = emit_string_base_guard(g, arr_ptr, expr->loc);
                LLVMValueRef str_len = emit_string_buffer_len(g, arr_ptr, expr->loc);
                emit_index_bounds_check(g, idx, str_len, expr->loc, "string");
                idx = emit_index_safe_bounds(g, idx, str_len, expr->loc, "string");
            } else {
                idx = emit_string_elem_guard(g, arr_ptr, idx, expr->loc, &arr_ptr);
            }
            if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(LLVMTypeOf(idx)) != 64) {
                idx = LLVMBuildSExt(g->builder, idx,
                    LLVMInt64TypeInContext(g->ctx), "idxext");
            }
            LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
            LLVMValueRef ch_ptr = LLVMBuildGEP2(g->builder, i8t, arr_ptr, &idx, 1, "chp");
            LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8t, ch_ptr, "ch");
            LLVMValueRef chz = LLVMBuildZExt(g->builder, ch,
                LLVMInt64TypeInContext(g->ctx), "chz");
            return finish_index_of_temp(g, expr, locals, arr_type, NULL,
                                        arr_ptr, chz);
        }
        if (arr_ptr && arr_type) {
            LLVMTypeRef elem_llvm = LLVMInt64TypeInContext(g->ctx);
            if (arr_type->element_type) {
                elem_llvm = map_type(g, arr_type->element_type);
            }
            LLVMValueRef elem_ptr;
            if (arr_type->array_rank > 1) {
                elem_ptr = emit_mdarray_elem_ptr(g, arr_ptr, expr,
                                                 elem_llvm, locals, NULL);
            } else {
                LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
                LLVMValueRef arr_len = zan_array_len(g, arr_ptr);
                emit_index_bounds_check(g, idx, arr_len, expr->loc, "array");
                idx = emit_index_safe_bounds(g, idx, arr_len, expr->loc, "array");
                LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                    LLVMPointerType(elem_llvm, 0), "arrp");
                elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
            }
            return promote_loaded(g,
                LLVMBuildLoad2(g->builder, elem_llvm, elem_ptr, "elem"),
                arr_type->element_type);
        }
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}
