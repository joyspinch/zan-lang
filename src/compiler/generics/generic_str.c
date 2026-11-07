
/* `string */
LLVMValueRef emit_string_length(zan_irgen_t *g, LLVMValueRef payload,
                                       zan_loc_t loc) {
    return emit_string_len_ex(g, payload, 1, loc);
}
LLVMValueRef get_str_ordinal_cmp_fn(zan_irgen_t *g, zan_loc_t loc) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_ocmp");
    if (fn) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr },
                                        2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_ocmp", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef memcmp_ty = LLVMFunctionType(i32t,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef memcmp_fn = get_libc_fn(g, "memcmp", memcmp_ty);

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef ret0 = LLVMAppendBasicBlockInContext(g->ctx, fn, "same");
    LLVMBasicBlockRef nullbb = LLVMAppendBasicBlockInContext(g->ctx, fn, "null");
    LLVMBasicBlockRef retneg = LLVMAppendBasicBlockInContext(g->ctx, fn, "neq");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMValueRef a = LLVMGetParam(fn, 0);
    LLVMValueRef b2 = LLVMGetParam(fn, 1);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, a, b2, "same"),
        ret0, nullbb);
    LLVMPositionBuilderAtEnd(g->builder, ret0);
    LLVMBuildRet(g->builder, LLVMConstInt(i32t, 0, 0));
    LLVMPositionBuilderAtEnd(g->builder, nullbb);
    LLVMValueRef an = zan_icmp(g->builder, LLVMIntEQ, a,
        LLVMConstNull(i8ptr), "an");
    LLVMValueRef bn = zan_icmp(g->builder, LLVMIntEQ, b2,
        LLVMConstNull(i8ptr), "bn");
    LLVMBuildCondBr(g->builder, zan_or(g->builder, an, bn, "en"),
        retneg, body);
    LLVMPositionBuilderAtEnd(g->builder, retneg);
    LLVMBuildRet(g->builder, LLVMConstInt(i32t, (uint64_t)-1, 1));
    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef la = emit_string_len_ex(g, a, 1, loc);
    LLVMValueRef lb = emit_string_len_ex(g, b2, 1, loc);
    LLVMValueRef llt = zan_icmp(g->builder, LLVMIntSLT, la, lb, "llt");
    LLVMValueRef m = LLVMBuildSelect(g->builder, llt, la, lb, "minlen");
    LLVMValueRef mr = zan_call2(g->builder, memcmp_ty, memcmp_fn,
        (LLVMValueRef[]){ a, b2, m }, 3, "ocmp.memcmp");
    LLVMValueRef tail = LLVMBuildSelect(g->builder, llt,
        LLVMConstInt(i32t, (uint64_t)-1, 1), LLVMConstInt(i32t, 1, 0),
        "tail");
    LLVMValueRef inner = LLVMBuildSelect(g->builder,
        zan_icmp(g->builder, LLVMIntNE, mr, LLVMConstInt(i32t, 0, 0), "nz"),
        mr, tail, "body");
    LLVMValueRef r = LLVMBuildSelect(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, la, lb, "leq"), mr, inner, "ocmp");
    LLVMBuildRet(g->builder, r);

    g->current_fn = saved_fn;
    if (saved_bb) LLVMPositionBuilderAtEnd(g->builder, saved_bb);
    return fn;
}
void emit_span_window_check(zan_irgen_t *g, LLVMValueRef start,
                                   LLVMValueRef window, LLVMValueRef length,
                                   zan_loc_t loc, const char *kind) {
    if (!g->runtime_checks || !g->current_fn) return;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    start = emit_index_i64(g, start, "span.start");
    window = emit_index_i64(g, window, "span.count");
    length = emit_index_i64(g, length, "span.length");
    LLVMValueRef bad_start = zan_icmp(g->builder, LLVMIntSLT, start,
                                      LLVMConstInt(i64, 0, 0), "span.start.neg");
    LLVMValueRef bad_window = zan_icmp(g->builder, LLVMIntSLT, window,
                                      LLVMConstInt(i64, 0, 0), "span.count.neg");
    LLVMValueRef after_end = zan_icmp(g->builder, LLVMIntSGT, start, length,
                                      "span.start.high");
    LLVMValueRef remaining = LLVMBuildSub(g->builder, length, start,
                                          "span.remaining");
    LLVMValueRef too_long = zan_icmp(g->builder, LLVMIntSGT, window, remaining,
                                     "span.count.high");
    LLVMValueRef bad = LLVMBuildOr(g->builder, bad_start, bad_window, "span.bad0");
    bad = LLVMBuildOr(g->builder, bad, after_end, "span.bad1");
    bad = LLVMBuildOr(g->builder, bad, too_long, "span.bad2");
    char msg[96];
    snprintf(msg, sizeof(msg), "%s range out of bounds", kind ? kind : "span");
    emit_runtime_check(g, bad, loc, msg);
}
void emit_span_safe_window(zan_irgen_t *g, LLVMValueRef *start_out,
                                  LLVMValueRef *window_out,
                                  LLVMValueRef length, zan_loc_t loc,
                                  const char *kind) {
    if (!g->runtime_checks || !g->current_fn) return;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef start = emit_index_i64(g, *start_out, "safe.start");
    LLVMValueRef window = emit_index_i64(g, *window_out, "safe.count");
    length = emit_index_i64(g, length, "safe.length");
    LLVMValueRef bad_start = zan_icmp(g->builder, LLVMIntSLT, start,
                                      LLVMConstInt(i64, 0, 0), "safe.start.neg");
    LLVMValueRef bad_window = zan_icmp(g->builder, LLVMIntSLT, window,
                                      LLVMConstInt(i64, 0, 0), "safe.count.neg");
    LLVMValueRef after_end = zan_icmp(g->builder, LLVMIntSGT, start, length,
                                      "safe.start.high");
    LLVMValueRef remaining = LLVMBuildSub(g->builder, length, start,
                                          "safe.remaining");
    LLVMValueRef too_long = zan_icmp(g->builder, LLVMIntSGT, window, remaining,
                                     "safe.count.high");
    LLVMValueRef bad = LLVMBuildOr(g->builder, bad_start, bad_window, "safe.bad0");
    bad = LLVMBuildOr(g->builder, bad, after_end, "safe.bad1");
    bad = LLVMBuildOr(g->builder, bad, too_long, "safe.bad2");
    *start_out = LLVMBuildSelect(g->builder, bad,
                                 LLVMConstInt(i64, 0, 0), start, "safe.start.ok");
    *window_out = LLVMBuildSelect(g->builder, bad,
                                  LLVMConstInt(i64, 0, 0), window, "safe.count.ok");
}
LLVMValueRef struct_base_ptr(zan_irgen_t *g, local_var_t *local, LLVMTypeRef st) {
    LLVMValueRef base = local->alloca;
    if (local->box_cell && local->type && local->type->kind == TYPE_CLASS) {
        base = LLVMBuildLoad2(g->builder, LLVMPointerType(st, 0), base, "objld");
    } else if (LLVMIsAAllocaInst(base)) {
        LLVMTypeRef alloc_t = LLVMGetAllocatedType(base);
        if (LLVMGetTypeKind(alloc_t) == LLVMPointerTypeKind) {
            base = LLVMBuildLoad2(g->builder, LLVMPointerType(st, 0), base, "objld");
        }
    } else if (local->byref_slot && local->type &&
               local->type->kind == TYPE_CLASS) {
        base = LLVMBuildLoad2(g->builder, LLVMPointerType(st, 0), base, "objld");
    }
    return base;
}
void coerce_int_pair(zan_irgen_t *g, LLVMValueRef *a, LLVMValueRef *b) {
    LLVMTypeRef ta = LLVMTypeOf(*a), tb = LLVMTypeOf(*b);
    /* 浮点二元运算提升规则：float 与 double 运算提升至 double 精度计算 */
    if (LLVMGetTypeKind(ta) == LLVMFloatTypeKind &&
        LLVMGetTypeKind(tb) == LLVMDoubleTypeKind) {
        *a = LLVMBuildFPExt(g->builder, *a, tb, "fpair.ext");
        return;
    }
    if (LLVMGetTypeKind(tb) == LLVMFloatTypeKind &&
        LLVMGetTypeKind(ta) == LLVMDoubleTypeKind) {
        *b = LLVMBuildFPExt(g->builder, *b, ta, "fpair.ext");
        return;
    }
    /* 混合类型乘法隐式提升：整数端隐式转换为浮点精度参与计算 */
    if ((LLVMGetTypeKind(ta) == LLVMDoubleTypeKind ||
         LLVMGetTypeKind(ta) == LLVMFloatTypeKind) &&
        LLVMGetTypeKind(tb) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(tb) > 1) {
        *b = LLVMBuildSIToFP(g->builder, *b, ta, "fpair.sitofp");
        return;
    }
    if ((LLVMGetTypeKind(tb) == LLVMDoubleTypeKind ||
         LLVMGetTypeKind(tb) == LLVMFloatTypeKind) &&
        LLVMGetTypeKind(ta) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(ta) > 1) {
        *a = LLVMBuildSIToFP(g->builder, *a, tb, "fpair.sitofp");
        return;
    }
    if (LLVMGetTypeKind(ta) != LLVMIntegerTypeKind ||
        LLVMGetTypeKind(tb) != LLVMIntegerTypeKind) return;
    unsigned wa = LLVMGetIntTypeWidth(ta), wb = LLVMGetIntTypeWidth(tb);
    if (wa == wb) return;
    if (wa < wb) *a = zan_iwiden(g->builder, *a, tb);
    else         *b = zan_iwiden(g->builder, *b, ta);
}
LLVMValueRef coerce_to_decimal(zan_irgen_t *g, LLVMValueRef val);
LLVMValueRef coerce_int_to(zan_irgen_t *g, LLVMValueRef v, LLVMTypeRef target) {
    if (!v || !target) return v;
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (vt == target) return v;
    if (llvm_is_nullable(target)) {
        if (LLVMGetTypeKind(vt) == LLVMPointerTypeKind)
            return LLVMIsNull(v) ? nullable_none(target) : v;
        if (llvm_is_nullable(vt)) {
            LLVMTypeRef pl = nullable_payload_type(target);
            LLVMValueRef fit = coerce_int_to(g, nullable_get_payload(g, v), pl);
            if (LLVMTypeOf(fit) != pl) return v;
            LLVMValueRef agg = LLVMBuildInsertValue(g->builder,
                LLVMGetUndef(target), fit, 0, "nv.val");
            return LLVMBuildInsertValue(g->builder, agg,
                nullable_has_value(g, v), 1, "nv.fit");
        }
        LLVMValueRef wrapped = nullable_some(g, target, v);
        return wrapped ? wrapped : v;
    }
    if (LLVMIsNull(v)) return LLVMConstNull(target);
    /* decimal 目标 + 整数来源：变量初始化 / 赋值 / 字段与可空包装的隐式
     * int→decimal 转换走运行时构造（__zan_decimal_from_int64）。此前整数值
     * 穿透到下方放行分支，i64 原样写进 Decimal 槽首 8 字节——flags 吞掉
     * 尾数低位、lo64 留栈上垃圾，值静默变成 0 或乱数（A548）。 */
    if (LLVMGetTypeKind(target) == LLVMStructTypeKind &&
        LLVMGetTypeKind(vt) == LLVMIntegerTypeKind &&
        target == get_decimal_type(g))
        return coerce_to_decimal(g, v);
    if (LLVMGetTypeKind(vt) == LLVMPointerTypeKind &&
        LLVMGetTypeKind(target) == LLVMIntegerTypeKind)
        return LLVMBuildPtrToInt(g->builder, v, target, "p2i");
    if (LLVMGetTypeKind(vt) == LLVMIntegerTypeKind &&
        LLVMGetTypeKind(target) == LLVMPointerTypeKind)
        return LLVMBuildIntToPtr(g->builder, v, target, "i2p");
    if (LLVMGetTypeKind(target) == LLVMFloatTypeKind &&
        LLVMGetTypeKind(vt) == LLVMDoubleTypeKind)
        return LLVMBuildFPTrunc(g->builder, v, target, "fit.fptrunc");
    if (LLVMGetTypeKind(target) == LLVMDoubleTypeKind &&
        LLVMGetTypeKind(vt) == LLVMFloatTypeKind)
        return LLVMBuildFPExt(g->builder, v, target, "fit.fpext");
    if ((LLVMGetTypeKind(target) == LLVMDoubleTypeKind ||
         LLVMGetTypeKind(target) == LLVMFloatTypeKind) &&
        LLVMGetTypeKind(vt) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(vt) > 1)
        return LLVMBuildSIToFP(g->builder, v, target, "fit.sitofp");
    if (LLVMGetTypeKind(vt) != LLVMIntegerTypeKind ||
        LLVMGetTypeKind(target) != LLVMIntegerTypeKind) return v;
    unsigned wv = LLVMGetIntTypeWidth(vt), wt = LLVMGetIntTypeWidth(target);
    if (wv == wt) return v;
    if (wv < wt) return zan_iwiden(g->builder, v, target);
    return LLVMBuildTrunc(g->builder, v, target, "trunc");
}
LLVMValueRef zan_store_fit(zan_irgen_t *g, LLVMValueRef val, LLVMValueRef ptr) {
    LLVMTypeRef target = NULL;
    if (LLVMIsAAllocaInst(ptr)) {
        target = LLVMGetAllocatedType(ptr);
    } else if (LLVMIsAGlobalVariable(ptr)) {
        target = LLVMGlobalGetValueType(ptr);
    } else if (LLVMIsAGetElementPtrInst(ptr)) {
        LLVMTypeRef src = LLVMGetGEPSourceElementType(ptr);
        if (src && LLVMGetTypeKind(src) == LLVMStructTypeKind &&
            LLVMGetNumOperands(ptr) == 3) {
            LLVMValueRef idx = LLVMGetOperand(ptr, 2);
            if (LLVMIsAConstantInt(idx))
                target = LLVMStructGetTypeAtIndex(
                    src, (unsigned)LLVMConstIntGetZExtValue(idx));
        } else if (src && LLVMGetTypeKind(src) == LLVMStructTypeKind &&
                   LLVMGetNumOperands(ptr) == 2) {
            /* 一维数组元素槽：GEP(结构体元素类型, 基址, 下标)——指针形
             * 结构体右值（new T{...}）进槽前必须取值，否则指针 8 字节
             * 裸写进槽（与 A548 decimal 同族坑） */
            target = src;
        } else if (src && (LLVMGetTypeKind(src) == LLVMFloatTypeKind ||
                           LLVMGetTypeKind(src) == LLVMDoubleTypeKind)) {
            target = src;
        }
    }
    if (target && llvm_is_nullable(target) && LLVMTypeOf(val) != target)
        return LLVMBuildStore(g->builder, coerce_int_to(g, val, target), ptr);
    if (target && LLVMGetTypeKind(target) == LLVMStructTypeKind &&
        LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind)
        val = LLVMBuildLoad2(g->builder, target, val, "sv.copy");
    if (target) val = coerce_int_to(g, val, target);
    if (target && LLVMTypeOf(val) != target &&
        LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind &&
        (LLVMGetTypeKind(target) == LLVMIntegerTypeKind ||
         LLVMGetTypeKind(target) == LLVMFloatTypeKind ||
         LLVMGetTypeKind(target) == LLVMDoubleTypeKind))
        val = emit_boundary_coerce(g, val, target);
    return LLVMBuildStore(g->builder, val, ptr);
}
LLVMValueRef emit_widen_i64_for_print(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(vt) != LLVMIntegerTypeKind) return v;
    unsigned w = LLVMGetIntTypeWidth(vt);
    if (w >= 64) return v;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    return zan_iwiden(g->builder, v, i64);
}
LLVMValueRef emit_string_alloc_rc(zan_irgen_t *g, LLVMValueRef payload_size) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef thirty_one = LLVMConstInt(i64, 31, 0);
    LLVMValueRef padded = zan_add(g->builder, payload_size, thirty_one, "str.pad");
    LLVMValueRef aligned = zan_and(g->builder, padded,
        LLVMConstInt(i64, ~UINT64_C(31), 0), "str.align");
    LLVMValueRef min_payload = LLVMConstInt(i64, 32, 0);
    LLVMValueRef payload = LLVMBuildSelect(g->builder,
        zan_icmp(g->builder, LLVMIntULT, aligned, min_payload, "str.small"),
        min_payload, aligned, "str.payload");
    LLVMValueRef user_ptr = zan_call2(g->builder,
        LLVMFunctionType(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                         (LLVMTypeRef[]){ i64 }, 1, 0),
        g->rt_str_alloc, &payload, 1, "str.raw");
    return user_ptr;
}
LLVMValueRef emit_widen_i64_for_print(zan_irgen_t *g, LLVMValueRef v);
LLVMValueRef emit_string_literal_rc(zan_irgen_t *g, zan_istr_t text);
LLVMValueRef emit_entry_scratch(zan_irgen_t *g, unsigned size,
                                       const char *name) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef arr = LLVMArrayType(i8, size);
    LLVMValueRef slot = emit_entry_alloca(g, arr, name);
    LLVMValueRef idxs[] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0) };
    return LLVMBuildInBoundsGEP2(g->builder, arr, slot, idxs, 2, name);
}

/* bool -> 托管串："true" / "false"。
   所有把值转成字符串的路径都要走这一条：i1 落进 itoa 会渲染成 "1"/"0"，
   同一个 bool 在不同上下文里长得不一样（`Convert.ToString(b)` -> "1"，
   `"x" + b` -> "x1"，而 `b.ToString()` -> "true"）。C# 的口径是 true/false。 */
LLVMValueRef emit_bool_to_cstr(zan_irgen_t *g, LLVMValueRef v) {
    zan_istr_t t = { "true", 4 }, f = { "false", 5 };
    return LLVMBuildSelect(g->builder, v,
        emit_string_literal_rc(g, t), emit_string_literal_rc(g, f), "b2s");
}

/* bool 的**语义类型**优先于 LLVM 值的宽度。泛型实例化的方法返回值
   （List<T>.Contains 这类）被提升成 i64，按宽度判就掉进 itoa，渲染成 "1"——
   同一个 bool 在不同上下文里长得不一样（插值 -> "1"，而 b.ToString() -> "true"）。
   C# 的口径是 true/false，所有取串路径都要按语义类型判。
   返回 i1 条件（非零为真）；这个值不是 bool 就返回 NULL。 */
LLVMValueRef emit_bool_cond(zan_irgen_t *g, LLVMValueRef val,
                                    zan_ast_node_t *ast, local_scope_t *locals) {
    if (!ast || !val) return NULL;
    zan_type_t *bt = infer_expr_type(g, ast, locals);
    if (!bt || bt->kind != TYPE_BOOL) return NULL;
    LLVMTypeRef vk = LLVMTypeOf(val);
    if (LLVMGetTypeKind(vk) != LLVMIntegerTypeKind) return NULL;
    if (LLVMGetIntTypeWidth(vk) == 1) return val;
    return LLVMBuildICmp(g->builder, LLVMIntNE, val,
                         LLVMConstInt(vk, 0, 0), "bcond");
}
LLVMValueRef emit_value_as_cstr(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(vt) == LLVMPointerTypeKind) return v;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (LLVMGetTypeKind(vt) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(vt) == 1) {
        return emit_bool_to_cstr(g, v);
    }
    LLVMValueRef buf = emit_entry_scratch(g, 40, "v2s.buf");
    if (LLVMGetTypeKind(vt) == LLVMDoubleTypeKind || LLVMGetTypeKind(vt) == LLVMFloatTypeKind) {
        emit_dbl_str(g, buf, LLVMConstInt(i64, 40, 0), v);
        return buf;
    }
    emit_itoa_into(g, buf, emit_widen_i64_for_print(g, v), 0);
    return buf;
}
void emit_sb_append_bytes(zan_irgen_t *g, LLVMValueRef sbp,
                                 LLVMValueRef s, LLVMValueRef slen) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef cptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 0, "sbcp");
    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, cptr, "sbcv");
    LLVMValueRef capptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 1, "sbcapp");
    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, capptr, "sbcapv");
    LLVMValueRef need = zan_add(g->builder, count, slen, "sbneed");
    LLVMValueRef full = zan_icmp(g->builder, LLVMIntSGT, need, cap, "sbfull");
    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sb.grow");
    LLVMBasicBlockRef st_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sb.st");
    LLVMBuildCondBr(g->builder, full, grow_bb, st_bb);
    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
    LLVMValueRef nc0 = zan_mul(g->builder, cap, LLVMConstInt(i64, 2, 0), "sbnc0");
    LLVMValueRef small = zan_icmp(g->builder, LLVMIntSLT, nc0, need, "sbsm");
    LLVMValueRef nc = LLVMBuildSelect(g->builder, small, need, nc0, "sbnc");
    LLVMValueRef tiny = zan_icmp(g->builder, LLVMIntSLT, nc,
                                 LLVMConstInt(i64, 64, 0), "sbtiny");
    nc = LLVMBuildSelect(g->builder, tiny, LLVMConstInt(i64, 64, 0), nc, "sbnc2");
    LLVMValueRef dptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 2, "sbdp");
    LLVMValueRef olddata = LLVMBuildLoad2(g->builder, i8ptr, dptr, "sbod");
    LLVMValueRef newdata = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
        g->fn_realloc, (LLVMValueRef[]){ olddata, nc }, 2, "sbnd");
    zan_irgen_emit_oom_check(g, g->current_fn, newdata);
    LLVMBuildStore(g->builder, newdata, dptr);
    LLVMBuildStore(g->builder, nc, capptr);
    LLVMBuildBr(g->builder, st_bb);
    LLVMPositionBuilderAtEnd(g->builder, st_bb);
    LLVMValueRef dptr2 = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 2, "sbdp2");
    LLVMValueRef data = LLVMBuildLoad2(g->builder, i8ptr, dptr2, "sbdv");
    LLVMValueRef dest = LLVMBuildGEP2(g->builder, i8, data, &count, 1, "sbdest");
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_ty);
    zan_call2(g->builder, memcpy_ty, memcpy_fn,
        (LLVMValueRef[]){ dest, s, slen }, 3, "");
    LLVMValueRef ncount = zan_add(g->builder, count, slen, "sbncount");
    LLVMBuildStore(g->builder, ncount, cptr);
}
LLVMValueRef emit_string_literal_rc(zan_irgen_t *g, zan_istr_t text) {
    for (int i = 0; i < g->string_literal_count; i++) {
        if ((size_t)g->string_literals[i].text.len == (size_t)text.len &&
            memcmp(g->string_literals[i].text.str, text.str, (size_t)text.len) == 0) {
            LLVMValueRef global = g->string_literals[i].value;
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
            LLVMTypeRef lit_ty = LLVMArrayType(i8, (unsigned)(16u + (size_t)text.len + 1u));
            LLVMValueRef idxs[] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 16, 0) };
            return LLVMBuildGEP2(g->builder, lit_ty, global, idxs, 2, "str.lit.ptr");
        }
    }

    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    size_t total = 16u + (size_t)text.len + 1u;
    LLVMTypeRef lit_ty = LLVMArrayType(i8, (unsigned)total);
    char name[64];
    snprintf(name, sizeof(name), "__zan.strlit.%d", g->string_literal_count);
    LLVMValueRef global = LLVMAddGlobal(g->mod, lit_ty, name);
    LLVMSetLinkage(global, LLVMPrivateLinkage);
    LLVMSetUnnamedAddr(global, LLVMGlobalUnnamedAddr);
    unsigned char *blob = (unsigned char *)calloc(total, 1);
    if (!blob) return LLVMConstNull(LLVMPointerType(i8, 0));
    memcpy(blob + 0, &((uint64_t){ ZAN_STRING_SENTINEL_RC }), 8);
    memcpy(blob + 8, &((uint64_t){ ZAN_STR_HDR_WORD(text.len) }), 8);
    if (text.len > 0) memcpy(blob + 16, text.str, (size_t)text.len);
    blob[16 + (size_t)text.len] = 0;
    bool obf = g->obfuscate_strings && text.len > 0;
    if (obf && !ZAN_TAB_ENSURE(g->obf_literals, g->obf_literal_count,
                               g->obf_literal_cap, 256)) {
        zan_loc_t oloc = {0};
        zan_diag_emit_code(g->diag, DIAG_ERROR, oloc, 3101,
                      "out of memory recording obfuscated string literals "
                      "(%d recorded)", g->obf_literal_count);
        obf = false;
    }
    if (obf) {
        for (size_t j = 0; j < (size_t)text.len; j++) {
            unsigned char ks = g->obf_key[j & 15]
                ^ (unsigned char)((unsigned)text.len * 31u + (unsigned)j * 89u);
            blob[16 + j] = (unsigned char)(blob[16 + j] ^ ks);
        }
        g->obf_literals[g->obf_literal_count].global = global;
        g->obf_literals[g->obf_literal_count].len = (uint32_t)text.len;
        g->obf_literal_count++;
    }
    LLVMSetGlobalConstant(global, obf ? 0 : 1);
    LLVMValueRef *init_elems = (LLVMValueRef *)calloc(total, sizeof(LLVMValueRef));
    if (!init_elems) {
        free(blob);
        return LLVMConstNull(LLVMPointerType(i8, 0));
    }
    for (size_t i = 0; i < total; i++) {
        init_elems[i] = LLVMConstInt(i8, blob[i], 0);
    }
    LLVMValueRef init = LLVMConstArray(i8, init_elems, (unsigned)total);
    LLVMSetInitializer(global, init);
    free(init_elems);
    free(blob);

    LLVMValueRef idxs[] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 16, 0) };
    LLVMValueRef user_ptr = LLVMBuildGEP2(g->builder, lit_ty, global, idxs, 2, "str.lit.ptr");

    if (ZAN_TAB_ENSURE(g->string_literals, g->string_literal_count,
                       g->string_literal_cap, 512)) {
        g->string_literals[g->string_literal_count].text = text;
        g->string_literals[g->string_literal_count].value = global;
        g->string_literal_count++;
    }
    return user_ptr;
}
LLVMValueRef emit_str_nonnull(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef empty = emit_string_literal_rc(g, (zan_istr_t){ "", 0 });
    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, v,
        LLVMConstNull(i8ptr), "s.null");
    return LLVMBuildSelect(g->builder, isnull, empty, v, "s.nn");
}

void zan_irgen_emit_string_deobf(zan_irgen_t *g) {
    if (!g->obfuscate_strings || g->obf_literal_count <= 0) return;
    int n = g->obf_literal_count;
    LLVMTypeRef i8  = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef ptr = LLVMPointerType(i8, 0);

    LLVMValueRef keyb[16];
    for (int i = 0; i < 16; i++) keyb[i] = LLVMConstInt(i8, g->obf_key[i], 0);
    LLVMTypeRef keyty = LLVMArrayType(i8, 16);
    LLVMValueRef keyg = LLVMAddGlobal(g->mod, keyty, "__zan.obf.key");
    LLVMSetLinkage(keyg, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(keyg, 1);
    LLVMSetUnnamedAddr(keyg, LLVMGlobalUnnamedAddr);
    LLVMSetInitializer(keyg, LLVMConstArray(i8, keyb, 16));

    LLVMValueRef *pinit = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    LLVMValueRef *linit = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    if (!pinit || !linit) { free(pinit); free(linit); return; }
    for (int i = 0; i < n; i++) {
        uint32_t len = g->obf_literals[i].len;
        size_t total = 16u + (size_t)len + 1u;
        LLVMTypeRef lit_ty = LLVMArrayType(i8, (unsigned)total);
        LLVMValueRef idx[] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 16, 0) };
        pinit[i] = LLVMConstGEP2(lit_ty, g->obf_literals[i].global, idx, 2);
        linit[i] = LLVMConstInt(i64, len, 0);
    }
    LLVMTypeRef ptab_ty = LLVMArrayType(ptr, (unsigned)n);
    LLVMTypeRef ltab_ty = LLVMArrayType(i64, (unsigned)n);
    LLVMValueRef ptab = LLVMAddGlobal(g->mod, ptab_ty, "__zan.obf.ptrs");
    LLVMValueRef ltab = LLVMAddGlobal(g->mod, ltab_ty, "__zan.obf.lens");
    LLVMSetLinkage(ptab, LLVMPrivateLinkage); LLVMSetGlobalConstant(ptab, 1);
    LLVMSetLinkage(ltab, LLVMPrivateLinkage); LLVMSetGlobalConstant(ltab, 1);
    LLVMSetInitializer(ptab, LLVMConstArray(ptr, pinit, (unsigned)n));
    LLVMSetInitializer(ltab, LLVMConstArray(i64, linit, (unsigned)n));
    free(pinit); free(linit);

    /* void __zan */
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan.deobf", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef ih    = LLVMAppendBasicBlockInContext(g->ctx, fn, "ih");
    LLVMBasicBlockRef ib    = LLVMAppendBasicBlockInContext(g->ctx, fn, "ib");
    LLVMBasicBlockRef jh    = LLVMAppendBasicBlockInContext(g->ctx, fn, "jh");
    LLVMBasicBlockRef jb    = LLVMAppendBasicBlockInContext(g->ctx, fn, "jb");
    LLVMBasicBlockRef inext = LLVMAppendBasicBlockInContext(g->ctx, fn, "inext");
    LLVMBasicBlockRef done  = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef ia = LLVMBuildAlloca(b, i64, "i");
    LLVMValueRef ja = LLVMBuildAlloca(b, i64, "j");
    LLVMValueRef pa = LLVMBuildAlloca(b, ptr, "p");
    LLVMValueRef la = LLVMBuildAlloca(b, i64, "L");
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), ia);
    LLVMBuildBr(b, ih);

    LLVMValueRef nconst = LLVMConstInt(i64, (unsigned long long)n, 0);
    LLVMPositionBuilderAtEnd(b, ih);
    LLVMValueRef iv = LLVMBuildLoad2(b, i64, ia, "iv");
    LLVMValueRef icmp = LLVMBuildICmp(b, LLVMIntSLT, iv, nconst, "icmp");
    LLVMBuildCondBr(b, icmp, ib, done);

    LLVMPositionBuilderAtEnd(b, ib);
    iv = LLVMBuildLoad2(b, i64, ia, "iv2");
    LLVMValueRef pidx[] = { LLVMConstInt(i64, 0, 0), iv };
    LLVMValueRef pslot = LLVMBuildGEP2(b, ptab_ty, ptab, pidx, 2, "pslot");
    LLVMValueRef pv = LLVMBuildLoad2(b, ptr, pslot, "pv");
    LLVMBuildStore(b, pv, pa);
    LLVMValueRef lslot = LLVMBuildGEP2(b, ltab_ty, ltab, pidx, 2, "lslot");
    LLVMValueRef lv = LLVMBuildLoad2(b, i64, lslot, "lv");
    LLVMBuildStore(b, lv, la);
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), ja);
    LLVMBuildBr(b, jh);

    LLVMPositionBuilderAtEnd(b, jh);
    LLVMValueRef jv = LLVMBuildLoad2(b, i64, ja, "jv");
    LLVMValueRef Lv = LLVMBuildLoad2(b, i64, la, "Lv");
    LLVMValueRef jcmp = LLVMBuildICmp(b, LLVMIntSLT, jv, Lv, "jcmp");
    LLVMBuildCondBr(b, jcmp, jb, inext);

    LLVMPositionBuilderAtEnd(b, jb);
    jv = LLVMBuildLoad2(b, i64, ja, "jv2");
    Lv = LLVMBuildLoad2(b, i64, la, "Lv2");
    LLVMValueRef pcur = LLVMBuildLoad2(b, ptr, pa, "pcur");
    LLVMValueRef bp = LLVMBuildGEP2(b, i8, pcur, &jv, 1, "bp");
    LLVMValueRef cur = LLVMBuildLoad2(b, i8, bp, "cur");
    LLVMValueRef ki = LLVMBuildAnd(b, jv, LLVMConstInt(i64, 15, 0), "ki");
    LLVMValueRef kidx[] = { LLVMConstInt(i64, 0, 0), ki };
    LLVMValueRef kslot = LLVMBuildGEP2(b, keyty, keyg, kidx, 2, "kslot");
    LLVMValueRef kb = LLVMBuildLoad2(b, i8, kslot, "kb");
    LLVMValueRef m1 = LLVMBuildMul(b, Lv, LLVMConstInt(i64, 31, 0), "m1");
    LLVMValueRef m2 = LLVMBuildMul(b, jv, LLVMConstInt(i64, 89, 0), "m2");
    LLVMValueRef ms = LLVMBuildAdd(b, m1, m2, "ms");
    LLVMValueRef mt = LLVMBuildTrunc(b, ms, i8, "mt");
    LLVMValueRef ks = LLVMBuildXor(b, kb, mt, "ks");
    LLVMValueRef nb = LLVMBuildXor(b, cur, ks, "nb");
    LLVMBuildStore(b, nb, bp);
    LLVMValueRef jn = LLVMBuildAdd(b, jv, LLVMConstInt(i64, 1, 0), "jn");
    LLVMBuildStore(b, jn, ja);
    LLVMBuildBr(b, jh);

    LLVMPositionBuilderAtEnd(b, inext);
    iv = LLVMBuildLoad2(b, i64, ia, "iv3");
    LLVMValueRef in = LLVMBuildAdd(b, iv, LLVMConstInt(i64, 1, 0), "in");
    LLVMBuildStore(b, in, ia);
    LLVMBuildBr(b, ih);

    LLVMPositionBuilderAtEnd(b, done);
    LLVMBuildRetVoid(b);
    LLVMDisposeBuilder(b);

    bool is_elf = !g->target_is_windows && !g->target_is_macos && !g->target_is_ios
        && strncmp(g->target_triple, "wasm", 4) != 0;
    if (is_elf) {
        LLVMValueRef ent = LLVMAddGlobal(g->mod, ptr, "__zan.init_array.deobf");
        LLVMSetLinkage(ent, LLVMInternalLinkage);
        LLVMSetSection(ent, ".init_array");
        LLVMSetInitializer(ent, fn);
        LLVMTypeRef usedty = LLVMArrayType(ptr, 1);
        LLVMValueRef usedg = LLVMAddGlobal(g->mod, usedty, "llvm.used");
        LLVMSetLinkage(usedg, LLVMAppendingLinkage);
        LLVMSetSection(usedg, "llvm.metadata");
        LLVMSetInitializer(usedg, LLVMConstArray(ptr, &ent, 1));
    } else {
        /* register in llvm */
        LLVMValueRef fields[] = { LLVMConstInt(i32, 65535, 0), fn, LLVMConstNull(ptr) };
        LLVMValueRef entryc = LLVMConstStruct(fields, 3, 0);
        LLVMTypeRef arrty = LLVMArrayType(LLVMTypeOf(entryc), 1);
        LLVMValueRef gc = LLVMAddGlobal(g->mod, arrty, "llvm.global_ctors");
        LLVMSetLinkage(gc, LLVMAppendingLinkage);
        LLVMSetInitializer(gc, LLVMConstArray(LLVMTypeOf(entryc), &entryc, 1));
    }
}

/* Coerce a value to an i8* C string */
LLVMValueRef emit_to_cstr_u(zan_irgen_t *g, LLVMValueRef val,
                                   bool is_unsigned) {
    LLVMTypeRef vt = LLVMTypeOf(val);
    LLVMTypeKind vtk = LLVMGetTypeKind(vt);
    if (vtk == LLVMPointerTypeKind) return val;
    /* bool 是 i1：统一渲染成 "true"/"false"（否则掉进 itoa 变成 "1"） */
    if (vtk == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(vt) == 1) {
        return emit_bool_to_cstr(g, val);
    }

    /* 可空类型字符串格式化：有值输出值字符串，无值输出空字符串 "" */
    if (llvm_is_nullable(vt)) {
        LLVMTypeRef nv_i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMValueRef has = nullable_has_value(g, val);
        LLVMValueRef payload = nullable_get_payload(g, val);
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        LLVMBasicBlockRef some_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "nv.str.some");
        LLVMBasicBlockRef none_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "nv.str.none");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "nv.str.end");
        LLVMBuildCondBr(g->builder, has, some_bb, none_bb);

        LLVMPositionBuilderAtEnd(g->builder, some_bb);
        LLVMValueRef sv = emit_to_cstr_u(g, payload, is_unsigned);
        LLVMBasicBlockRef some_end = LLVMGetInsertBlock(g->builder);
        LLVMBuildBr(g->builder, end_bb);

        LLVMPositionBuilderAtEnd(g->builder, none_bb);
        LLVMValueRef empty = emit_string_alloc_rc(g,
            LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 1, 0));
        LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0),
                       empty);
        LLVMBasicBlockRef none_end = LLVMGetInsertBlock(g->builder);
        LLVMBuildBr(g->builder, end_bb);

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        LLVMValueRef phi = LLVMBuildPhi(g->builder, nv_i8ptr, "nv.str");
        LLVMValueRef vals[] = { sv, empty };
        LLVMBasicBlockRef bbs[] = { some_end, none_end };
        LLVMAddIncoming(phi, vals, bbs, 2);
        return phi;
    }

    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (vt == get_decimal_type(g)) {
        LLVMValueRef dslot = emit_entry_alloca(g, vt, "dec.str.slot");
        LLVMBuildStore(g->builder, val, dslot);
        LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 48, 0));
        LLVMTypeRef to_str_fn_ty = LLVMFunctionType(
            LLVMInt32TypeInContext(g->ctx),
            (LLVMTypeRef[]){ LLVMPointerType(vt, 0), i8ptr, i64 },
            3, 0);
        LLVMValueRef to_str_fn = get_libc_fn(g, "__zan_decimal_to_string", to_str_fn_ty);
        LLVMValueRef len = zan_call2(g->builder, to_str_fn_ty, to_str_fn,
            (LLVMValueRef[]){ dslot, buf, LLVMConstInt(i64, 48, 0) }, 3, "dec.slen");
        LLVMValueRef len64 = LLVMBuildZExt(g->builder, len, i64, "dec.slen64");
        emit_string_len_set(g, buf, len64);
        return buf;
    }
    if (vtk == LLVMDoubleTypeKind || vtk == LLVMFloatTypeKind) {
        LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 40, 0));
        emit_dbl_str(g, buf, LLVMConstInt(i64, 40, 0), val);
        return buf;
    }
    LLVMValueRef ibuf = emit_string_alloc_rc(g, LLVMConstInt(i64, 24, 0));
    emit_itoa_into(g, ibuf, emit_widen_i64_for_print(g, val),
                   is_unsigned ? 1 : 0);
    return ibuf;
}
LLVMValueRef emit_to_cstr(zan_irgen_t *g, LLVMValueRef val) {
    return emit_to_cstr_u(g, val, false);
}
LLVMValueRef emit_char_to_cstr(zan_irgen_t *g, LLVMValueRef val) {
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8t, 0);
    LLVMBuilderRef bd = g->builder;

    LLVMValueRef cp = emit_widen_i64_for_print(g, val);
    cp = LLVMBuildAnd(bd, cp, LLVMConstInt(i64t, 0x1FFFFF, 0), "ch.cp");

    LLVMValueRef low6 = LLVMBuildAnd(bd, cp, LLVMConstInt(i64t, 0x3F, 0), "ch.l6");
    LLVMValueRef sh6 = LLVMBuildLShr(bd, cp, LLVMConstInt(i64t, 6, 0), "ch.s6");
    LLVMValueRef sh12 = LLVMBuildLShr(bd, cp, LLVMConstInt(i64t, 12, 0), "ch.s12");
    LLVMValueRef sh18 = LLVMBuildLShr(bd, cp, LLVMConstInt(i64t, 18, 0), "ch.s18");
    LLVMValueRef c6 = LLVMBuildAnd(bd, sh6, LLVMConstInt(i64t, 0x3F, 0), "ch.c6");
    LLVMValueRef c12 = LLVMBuildAnd(bd, sh12, LLVMConstInt(i64t, 0x3F, 0), "ch.c12");

    /* byte(n) = payload | tag, packed at 8*n bits. */
    LLVMValueRef b2 = LLVMBuildOr(bd,
        LLVMBuildOr(bd, LLVMConstInt(i64t, 0xC0, 0), sh6, "ch.b2a"),
        LLVMBuildShl(bd, LLVMBuildOr(bd, LLVMConstInt(i64t, 0x80, 0), low6, "ch.b2b"),
                     LLVMConstInt(i64t, 8, 0), "ch.b2c"), "ch.b2");
    LLVMValueRef b3 = LLVMBuildOr(bd,
        LLVMBuildOr(bd, LLVMConstInt(i64t, 0xE0, 0), sh12, "ch.b3a"),
        LLVMBuildOr(bd,
            LLVMBuildShl(bd, LLVMBuildOr(bd, LLVMConstInt(i64t, 0x80, 0), c6, "ch.b3b"),
                         LLVMConstInt(i64t, 8, 0), "ch.b3c"),
            LLVMBuildShl(bd, LLVMBuildOr(bd, LLVMConstInt(i64t, 0x80, 0), low6, "ch.b3d"),
                         LLVMConstInt(i64t, 16, 0), "ch.b3e"), "ch.b3f"), "ch.b3");
    LLVMValueRef b4 = LLVMBuildOr(bd,
        LLVMBuildOr(bd, LLVMConstInt(i64t, 0xF0, 0), sh18, "ch.b4a"),
        LLVMBuildOr(bd,
            LLVMBuildShl(bd, LLVMBuildOr(bd, LLVMConstInt(i64t, 0x80, 0), c12, "ch.b4b"),
                         LLVMConstInt(i64t, 8, 0), "ch.b4c"),
            LLVMBuildOr(bd,
                LLVMBuildShl(bd, LLVMBuildOr(bd, LLVMConstInt(i64t, 0x80, 0), c6, "ch.b4d"),
                             LLVMConstInt(i64t, 16, 0), "ch.b4e"),
                LLVMBuildShl(bd, LLVMBuildOr(bd, LLVMConstInt(i64t, 0x80, 0), low6, "ch.b4f"),
                             LLVMConstInt(i64t, 24, 0), "ch.b4g"), "ch.b4h"), "ch.b4i"), "ch.b4");

    LLVMValueRef lt80 = LLVMBuildICmp(bd, LLVMIntULT, cp, LLVMConstInt(i64t, 0x80, 0), "ch.lt80");
    LLVMValueRef lt800 = LLVMBuildICmp(bd, LLVMIntULT, cp, LLVMConstInt(i64t, 0x800, 0), "ch.lt800");
    LLVMValueRef lt10000 = LLVMBuildICmp(bd, LLVMIntULT, cp, LLVMConstInt(i64t, 0x10000, 0), "ch.lt1k");
    LLVMValueRef packed = LLVMBuildSelect(bd, lt80, cp,
        LLVMBuildSelect(bd, lt800, b2,
            LLVMBuildSelect(bd, lt10000, b3, b4, "ch.p3"), "ch.p2"), "ch.packed");
    LLVMValueRef len = LLVMBuildSelect(bd, lt80, LLVMConstInt(i64t, 1, 0),
        LLVMBuildSelect(bd, lt800, LLVMConstInt(i64t, 2, 0),
            LLVMBuildSelect(bd, lt10000, LLVMConstInt(i64t, 3, 0),
                            LLVMConstInt(i64t, 4, 0), "ch.n3"), "ch.n2"), "ch.len");

    LLVMValueRef tmp = LLVMBuildAlloca(g->builder, i64t, "ch.tmp");
    LLVMBuildStore(bd, packed, tmp);
    LLVMValueRef buf = emit_string_alloc_rc(g, zan_add(bd, len, LLVMConstInt(i64t, 1, 0), "ch.sz"));
    LLVMTypeRef memcpy_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_type);
    zan_call2(bd, memcpy_type, memcpy_fn, (LLVMValueRef[]){ buf, tmp, len }, 3, "");
    LLVMValueRef endp = LLVMBuildGEP2(bd, i8t, buf, &len, 1, "ch.end");
    LLVMBuildStore(bd, LLVMConstInt(i8t, 0, 0), endp);
    emit_string_len_set(g, buf,
        LLVMBuildSelect(bd, LLVMBuildICmp(bd, LLVMIntEQ, cp,
            LLVMConstInt(i64t, 0, 0), "ch.z"),
            LLVMConstInt(i64t, 0, 0), len, "ch.stamp"));
    return buf;
}
LLVMValueRef emit_to_cstr_of(zan_irgen_t *g, LLVMValueRef val,
                                    zan_ast_node_t *ast, local_scope_t *locals) {
    if (ast && val) {
        LLVMTypeRef vt = LLVMTypeOf(val);
        if (LLVMGetTypeKind(vt) == LLVMStructTypeKind) {
            const char *sn = LLVMGetStructName(vt);
            if (sn && strncmp(sn, "zan.nullable.", 13) == 0) {
                LLVMTypeRef payty = LLVMStructGetTypeAtIndex(vt, 0);
                LLVMTypeKind pk = LLVMGetTypeKind(payty);
                if (pk == LLVMIntegerTypeKind || pk == LLVMFloatTypeKind ||
                    pk == LLVMDoubleTypeKind) {
                    zan_type_t *st = infer_expr_type(g, ast, locals);
                    bool uns = st && st->element_type &&
                               (st->element_type->kind == TYPE_UINT ||
                                st->element_type->kind == TYPE_ULONG);
                    LLVMValueRef pay = LLVMBuildExtractValue(g->builder, val, 0, "nl.pay");
                    LLVMValueRef has = LLVMBuildExtractValue(g->builder, val, 1, "nl.has");
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMBasicBlockRef has_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "nl.has");
                    LLVMBasicBlockRef no_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "nl.no");
                    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "nl.end");
                    LLVMBuildCondBr(g->builder, has, has_bb, no_bb);
                    LLVMPositionBuilderAtEnd(g->builder, has_bb);
                    LLVMValueRef ps = emit_to_cstr_u(g, pay, uns);
                    LLVMBuildBr(g->builder, end_bb);
                    LLVMPositionBuilderAtEnd(g->builder, no_bb);
                    LLVMBuildBr(g->builder, end_bb);
                    LLVMPositionBuilderAtEnd(g->builder, end_bb);
                    LLVMValueRef phi = LLVMBuildPhi(g->builder, i8ptr, "nl.str");
                    LLVMValueRef vals[2] = { ps, LLVMConstNull(i8ptr) };
                    LLVMBasicBlockRef bbs[2] = { has_bb, no_bb };
                    LLVMAddIncoming(phi, vals, bbs, 2);
                    return phi;
                }
            }
        }
    }
    /* bool 的语义类型优先于 LLVM 值的宽度。泛型实例化的方法返回值
       （List<Big>.Contains 这类）被提升成 i64，按宽度判就掉进 itoa，
       渲染成 "1"——同一个 bool 在不同上下文里长得不一样（"x" + b -> "x1"，
       而 b.ToString() -> "true"）。C# 的口径是 true/false，两边都得是它。 */
    if (ast && val) {
        LLVMValueRef bc = emit_bool_cond(g, val, ast, locals);
        if (bc) return emit_bool_to_cstr(g, bc);
    }
    if (ast && val && LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind) {
        zan_type_t *t = infer_expr_type(g, ast, locals);
        LLVMTypeRef want = t ? map_type(g, t) : NULL;
        if (want) {
            LLVMTypeKind wk = LLVMGetTypeKind(want);
            if (wk == LLVMIntegerTypeKind || wk == LLVMFloatTypeKind ||
                wk == LLVMDoubleTypeKind)
                val = emit_boundary_coerce(g, val, want);
        }
    }
    if (ast && LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMIntegerTypeKind &&
        expr_is_char(g, ast, locals)) {
        return emit_char_to_cstr(g, val);
    }
    zan_type_t *st = ast ? infer_expr_type(g, ast, locals) : NULL;
    return emit_to_cstr_u(g, val, st && st->kind == TYPE_ULONG);
}
LLVMValueRef emit_cstr_len_of(zan_irgen_t *g, LLVMValueRef s,
                                     zan_ast_node_t *ast) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (ast && ast->kind == AST_STRING_LITERAL)
        return LLVMConstInt(i64, (uint64_t)ast->str_val.len, 0);
    return emit_string_length(g, s, ast ? ast->loc : (zan_loc_t){0});
}
LLVMValueRef emit_str_concat(zan_irgen_t *g, LLVMValueRef a, LLVMValueRef b) {
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    a = emit_str_nonnull(g, a);
    b = emit_str_nonnull(g, b);
    LLVMValueRef la = emit_string_length(g, a, (zan_loc_t){0});
    LLVMValueRef lb = emit_string_length(g, b, (zan_loc_t){0});
    LLVMValueRef tot = zan_add(g->builder, la, lb, "ct");
    tot = zan_add(g->builder, tot, LLVMConstInt(i64t, 1, 0), "ct1");
    LLVMValueRef buf = emit_string_alloc_rc(g, tot);
    LLVMTypeRef memcpy_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_type);
    zan_call2(g->builder, memcpy_type, memcpy_fn,
        (LLVMValueRef[]){ buf, a, la }, 3, "");
    LLVMValueRef dst_b = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), buf, &la, 1, "dst.b");
    zan_call2(g->builder, memcpy_type, memcpy_fn,
        (LLVMValueRef[]){ dst_b, b, lb }, 3, "");
    LLVMValueRef end_off = zan_add(g->builder, la, lb, "slen");
    LLVMValueRef endp = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), buf, &end_off, 1, "end");
    LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0), endp);
    emit_string_len_set(g, buf, end_off);
    return buf;
}

/* 检查二元 + 表达式任一操作数是否为字符串（判定字符串拼接） */
bool is_str_concat_node(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    return e && e->kind == AST_BINARY && e->binary.op == TK_PLUS &&
        (is_string_expr(g, e->binary.left, locals) ||
         is_string_expr(g, e->binary.right, locals));
}

static void collect_concat_ops(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals,
                               zan_ast_node_t **ops, int *n, int max) {
    if (*n < max - 1 && is_str_concat_node(g, e, locals)) {
        collect_concat_ops(g, e->binary.left, locals, ops, n, max - 1);
        collect_concat_ops(g, e->binary.right, locals, ops, n, max);
    } else {
        ops[(*n)++] = e;
    }
}
LLVMValueRef emit_str_concat_n(zan_irgen_t *g, zan_ast_node_t *expr,
                                      local_scope_t *locals) {
    enum { MAXOPS = 48 };
    zan_ast_node_t *ops[MAXOPS];
    int n = 0;
    collect_concat_ops(g, expr, locals, ops, &n, MAXOPS);

    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8t, 0);
    LLVMTypeRef memcpy_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_type);

    LLVMValueRef vals[MAXOPS];
    LLVMValueRef lens[MAXOPS];
    bool owned[MAXOPS];
    LLVMValueRef total = LLVMConstInt(i64t, 1, 0); /* NUL */
    for (int i = 0; i < n; i++) {
        LLVMValueRef v = emit_expr(g, ops[i], locals);
        LLVMValueRef s = emit_to_cstr_of(g, v, ops[i], locals);
        s = emit_str_nonnull(g, s);
        vals[i] = s;
        owned[i] = !is_string_expr(g, ops[i], locals) ||
                   expr_yields_owned_rc_value(g, ops[i], locals);
        if (owned[i]) {
            /* s 永远是托管字符串的数据指针（整数/字符操作数已由
             * emit_to_cstr_of 转成 itoa 缓冲），所以 EH 槽位种类必须是
             * ZAN_EH_SLOT_STR。若按源操作数的类型取种类，整数操作数会
             * 落到 ZAN_EH_SLOT_OBJ，异常展开时 zan_rt_release_dyn 会把
             * 字符串头字当成 site 下标去查析构表——读坏内存。 */
            emit_eh_tmp_push_slot(g, s, ZAN_EH_SLOT_STR);
        }
        lens[i] = emit_cstr_len_of(g, s, ops[i]);
        total = zan_add(g->builder, total, lens[i], "ct");
    }
    LLVMValueRef buf = emit_string_alloc_rc(g, total);
    LLVMValueRef off = LLVMConstInt(i64t, 0, 0);
    for (int i = 0; i < n; i++) {
        LLVMValueRef dst = LLVMBuildGEP2(g->builder, i8t, buf, &off, 1, "dst");
        zan_call2(g->builder, memcpy_type, memcpy_fn,
            (LLVMValueRef[]){ dst, vals[i], lens[i] }, 3, "");
        off = zan_add(g->builder, off, lens[i], "off");
    }
    LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8t, buf, &off, 1, "end");
    LLVMBuildStore(g->builder, LLVMConstInt(i8t, 0, 0), endp);
    emit_string_len_set(g, buf, off);
    for (int i = 0; i < n; i++) {
        if (owned[i]) {
            emit_eh_tmp_pop(g);
            emit_string_release(g, vals[i]);
        }
    }
    return buf;
}

/* 返回the internal `__zan_co_reap` step function, creating it once per module */
LLVMValueRef get_co_reap_fn(zan_irgen_t *g) {
    LLVMValueRef reap = LLVMGetNamedFunction(g->mod, "__zan_co_reap");
    if (reap) return reap;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef reap_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    reap = LLVMAddFunction(g->mod, "__zan_co_reap", reap_ty);
    LLVMSetLinkage(reap, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMTypeRef hdr = g->co_header_type;
    LLVMValueRef arg = LLVMGetParam(reap, 0);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, reap, "entry");
    LLVMBasicBlockRef rel_bb = LLVMAppendBasicBlockInContext(g->ctx, reap, "release");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, reap, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef exc_p = LLVMBuildStructGEP2(g->builder, hdr, arg,
        ASYNC_FRAME_EXC, "r.exc.p");
    LLVMValueRef exc = LLVMBuildLoad2(g->builder, i8ptr, exc_p, "r.exc");
    LLVMValueRef own_p = LLVMBuildStructGEP2(g->builder, hdr, arg,
        ASYNC_FRAME_EXC_OWNED, "r.own.p");
    LLVMValueRef own = LLVMBuildLoad2(g->builder, i32, own_p, "r.own");
    LLVMValueRef nonnull = zan_icmp(g->builder, LLVMIntNE, exc,
        LLVMConstNull(i8ptr), "r.nonnull");
    LLVMValueRef owns = zan_icmp(g->builder, LLVMIntNE, own,
        LLVMConstInt(i32, 0, 0), "r.owns");
    LLVMBuildCondBr(g->builder, zan_and(g->builder, nonnull, owns, "r.rel"),
        rel_bb, done);

    LLVMPositionBuilderAtEnd(g->builder, rel_bb);
    {
        LLVMValueRef tid_p = LLVMBuildStructGEP2(g->builder, hdr, arg,
            ASYNC_FRAME_EXC_TID, "r.tid.p");
        LLVMValueRef tid = LLVMBuildLoad2(g->builder, i8ptr, tid_p, "r.tid");
        LLVMValueRef is_obj = zan_icmp(g->builder, LLVMIntNE, tid,
            LLVMConstNull(i8ptr), "r.isobj");
        LLVMBasicBlockRef rel_obj = LLVMAppendBasicBlockInContext(g->ctx, reap, "rel.obj");
        LLVMBasicBlockRef rel_str = LLVMAppendBasicBlockInContext(g->ctx, reap, "rel.str");
        LLVMBasicBlockRef rel_done = LLVMAppendBasicBlockInContext(g->ctx, reap, "rel.done");
        LLVMBuildCondBr(g->builder, is_obj, rel_obj, rel_str);

        LLVMPositionBuilderAtEnd(g->builder, rel_obj);
        zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_release_dyn),
            g->rt_release_dyn, &exc, 1, "");
        LLVMBuildBr(g->builder, rel_done);

        LLVMPositionBuilderAtEnd(g->builder, rel_str);
        zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_str_release),
            g->rt_str_release, &exc, 1, "");
        LLVMBuildBr(g->builder, rel_done);

        LLVMPositionBuilderAtEnd(g->builder, rel_done);
        LLVMBuildBr(g->builder, done);
    }

    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMValueRef untrack = get_co_untrack_fn(g);
    zan_call2(g->builder, LLVMGlobalGetValueType(untrack), untrack, &arg, 1, "");
    emit_co_cancel_delay(g, arg);
    zan_emit_frame_free(g, arg);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return reap;
}

static LLVMValueRef get_async_unwind_fn(zan_irgen_t *g) {
    LLVMValueRef f = LLVMGetNamedFunction(g->mod, "__zan_async_unwind");
    if (f) return f;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    f = LLVMAddFunction(g->mod, "__zan_async_unwind", fty);
    LLVMSetLinkage(f, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMTypeRef hdr = g->co_header_type;
    LLVMTypeRef hdr_ptr = LLVMPointerType(hdr, 0);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, f, "entry");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(g->ctx, f, "head");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, f, "body");
    LLVMBasicBlockRef clean = LLVMAppendBasicBlockInContext(g->ctx, f, "clean");
    LLVMBasicBlockRef call_bb = LLVMAppendBasicBlockInContext(g->ctx, f, "call");
    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, f, "next");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, f, "done");

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef cur_a = LLVMBuildAlloca(g->builder, i8ptr, "cur.a");
    LLVMValueRef next_a = LLVMBuildAlloca(g->builder, i8ptr, "next.a");
    LLVMBuildStore(g->builder, LLVMGetParam(f, 0), cur_a);
    LLVMBuildBr(g->builder, head);

    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr, cur_a, "cur");
    LLVMTypeRef ptr_int_ty = g->target_is_wasm ? i32 : LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef cur_int = LLVMBuildPtrToInt(g->builder, cur, ptr_int_ty, "cur.int");
    LLVMValueRef valid = zan_icmp(g->builder, LLVMIntUGT, cur_int,
        LLVMConstInt(ptr_int_ty, 1, 0), "cur.valid");
    LLVMBuildCondBr(g->builder, valid, body, done);

    LLVMPositionBuilderAtEnd(g->builder, body);
    cur = LLVMBuildLoad2(g->builder, i8ptr, cur_a, "cur");
    LLVMValueRef hf = LLVMBuildBitCast(g->builder, cur, hdr_ptr, "hf");
    LLVMValueRef hc = LLVMBuildLoad2(g->builder, i32,
        LLVMBuildStructGEP2(g->builder, hdr, hf, ASYNC_FRAME_HCOUNT, "hc.p"), "hc");
    LLVMValueRef armed = zan_icmp(g->builder, LLVMIntSGT, hc,
        LLVMConstInt(i32, 0, 0), "hc.armed");
    LLVMBuildCondBr(g->builder, armed, done, clean);

    LLVMPositionBuilderAtEnd(g->builder, clean);
    LLVMValueRef aw = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildStructGEP2(g->builder, hdr, hf, ASYNC_FRAME_AWAITER, "aw.p"), "aw");
    /* a detached (Task */
    LLVMValueRef aw_int = LLVMBuildPtrToInt(g->builder, aw, ptr_int_ty, "aw.int");
    LLVMValueRef aw_valid = zan_icmp(g->builder, LLVMIntUGT, aw_int,
        LLVMConstInt(ptr_int_ty, 1, 0), "aw.valid");
    LLVMValueRef not_det = zan_icmp(g->builder, LLVMIntNE, aw, cur, "aw.ndet");
    LLVMValueRef has_nxt = zan_and(g->builder, aw_valid, not_det, "has.nxt");
    LLVMValueRef nxt = LLVMBuildSelect(g->builder, has_nxt, aw,
        LLVMConstNull(i8ptr), "aw.next");
    LLVMBuildStore(g->builder, nxt, next_a);
    LLVMValueRef cl = LLVMBuildLoad2(g->builder, g->co_step_ptr,
        LLVMBuildStructGEP2(g->builder, hdr, hf, ASYNC_FRAME_CLEANUP, "cl.p"), "cl");
    LLVMValueRef has_cl = zan_icmp(g->builder, LLVMIntNE, cl,
        LLVMConstNull(g->co_step_ptr), "cl.nn");
    LLVMBuildCondBr(g->builder, has_cl, call_bb, next_bb);

    LLVMPositionBuilderAtEnd(g->builder, call_bb);
    zan_call2(g->builder, g->co_step_type, cl, &cur, 1, "");
    LLVMBuildBr(g->builder, next_bb);

    LLVMPositionBuilderAtEnd(g->builder, next_bb);
    LLVMValueRef nv = LLVMBuildLoad2(g->builder, i8ptr, next_a, "next");
    LLVMBuildStore(g->builder, nv, cur_a);
    LLVMBuildBr(g->builder, head);

    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return f;
}
