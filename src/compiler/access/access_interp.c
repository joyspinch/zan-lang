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
LLVMValueRef emit_expr_string_interp(zan_irgen_t *g, zan_ast_node_t *expr,
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
