/* PixelOps 图形像素栅格化原语：双线性插值、Alpha 混合与颜色格式转换 */

static LLVMValueRef pixel_blend_over_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_pixel_blend_over");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(void_ty,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    return LLVMAddFunction(g->mod, "zan_hw_pixel_blend_over", fn_ty);
}

static LLVMValueRef pixel_swap_rb_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_pixel_swap_rb");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(void_ty,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    return LLVMAddFunction(g->mod, "zan_hw_pixel_swap_rb", fn_ty);
}

static LLVMValueRef pixel_fill_rect_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_pixel_fill_rect");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(void_ty,
        (LLVMTypeRef[]){ i8ptr, i64t, i64t, i64t, i64t, i64t, i32t }, 7, 0);
    return LLVMAddFunction(g->mod, "zan_hw_pixel_fill_rect", fn_ty);
}

static LLVMValueRef pixel_resample_bilinear_row_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_pixel_resample_bilinear_row");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(void_ty,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr, i8ptr, i8ptr, i32t, i64t }, 7, 0);
    return LLVMAddFunction(g->mod, "zan_hw_pixel_resample_bilinear_row", fn_ty);
}
bool emit_pixel_ops_call(zan_irgen_t *g, zan_ast_node_t *expr,
                                local_scope_t *locals, LLVMValueRef *out) {
    if (expr->kind != AST_CALL) return false;
    zan_ast_node_t *callee = expr->call.callee;
    if (callee->kind != AST_MEMBER_ACCESS) return false;

    zan_ast_node_t *obj = callee->member.object;
    bool is_pixelops = false;
    if (obj->kind == AST_IDENTIFIER) {
        if (obj->ident.name.len == 8 && memcmp(obj->ident.name.str, "PixelOps", 8) == 0)
            is_pixelops = true;
    } else if (obj->kind == AST_MEMBER_ACCESS) {
        if (obj->member.name.len == 8 && memcmp(obj->member.name.str, "PixelOps", 8) == 0)
            is_pixelops = true;
    }
    if (!is_pixelops) return false;

    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(g->ctx);
    LLVMValueRef zero64 = LLVMConstInt(i64t, 0, 0);

    zan_istr_t method = callee->member.name;

    if (method.len == 9 && memcmp(method.str, "BlendOver", 9) == 0 && expr->call.args.count == 3) {
        LLVMValueRef dst = nm_arg(g, expr, 0, locals);
        LLVMValueRef src = nm_arg(g, expr, 1, locals);
        LLVMValueRef count = nm_arg(g, expr, 2, locals);
        LLVMValueRef fn = pixel_blend_over_fn(g);
        LLVMTypeRef fn_ty = LLVMFunctionType(void_ty, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
        zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){
            nm_addr(g, dst, zero64), nm_addr(g, src, zero64), count }, 3, "");
        *out = zero64;
        return true;
    }

    if (method.len == 6 && memcmp(method.str, "SwapRB", 6) == 0 && expr->call.args.count == 3) {
        LLVMValueRef dst = nm_arg(g, expr, 0, locals);
        LLVMValueRef src = nm_arg(g, expr, 1, locals);
        LLVMValueRef count = nm_arg(g, expr, 2, locals);
        LLVMValueRef fn = pixel_swap_rb_fn(g);
        LLVMTypeRef fn_ty = LLVMFunctionType(void_ty, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
        zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){
            nm_addr(g, dst, zero64), nm_addr(g, src, zero64), count }, 3, "");
        *out = zero64;
        return true;
    }

    if (method.len == 8 && memcmp(method.str, "FillRect", 8) == 0 && expr->call.args.count == 7) {
        LLVMValueRef dst = nm_arg(g, expr, 0, locals);
        LLVMValueRef dst_stride = nm_arg(g, expr, 1, locals);
        LLVMValueRef x = nm_arg(g, expr, 2, locals);
        LLVMValueRef y = nm_arg(g, expr, 3, locals);
        LLVMValueRef w = nm_arg(g, expr, 4, locals);
        LLVMValueRef h = nm_arg(g, expr, 5, locals);
        LLVMValueRef color = coerce_int_to(g, emit_expr(g, expr->call.args.items[6], locals), i32t);
        LLVMValueRef fn = pixel_fill_rect_fn(g);
        LLVMTypeRef fn_ty = LLVMFunctionType(void_ty,
            (LLVMTypeRef[]){ i8ptr, i64t, i64t, i64t, i64t, i64t, i32t }, 7, 0);
        zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){
            nm_addr(g, dst, zero64), dst_stride, x, y, w, h, color }, 7, "");
        *out = zero64;
        return true;
    }

    if (method.len == 19 && memcmp(method.str, "ResampleBilinearRow", 19) == 0 && expr->call.args.count == 7) {
        LLVMValueRef dst = nm_arg(g, expr, 0, locals);
        LLVMValueRef src0 = nm_arg(g, expr, 1, locals);
        LLVMValueRef src1 = nm_arg(g, expr, 2, locals);
        LLVMValueRef x_idx = nm_arg(g, expr, 3, locals);
        LLVMValueRef x_wt = nm_arg(g, expr, 4, locals);
        LLVMValueRef wy = coerce_int_to(g, emit_expr(g, expr->call.args.items[5], locals), i32t);
        LLVMValueRef width = nm_arg(g, expr, 6, locals);
        LLVMValueRef fn = pixel_resample_bilinear_row_fn(g);
        LLVMTypeRef fn_ty = LLVMFunctionType(void_ty,
            (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr, i8ptr, i8ptr, i32t, i64t }, 7, 0);
        zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){
            nm_addr(g, dst, zero64), nm_addr(g, src0, zero64), nm_addr(g, src1, zero64),
            nm_addr(g, x_idx, zero64), nm_addr(g, x_wt, zero64), wy, width }, 7, "");
        *out = zero64;
        return true;
    }

    return false;
}

/* __zan_cpu_feature(i32 id): 底层 CPUID 特性探测接口 */
LLVMValueRef cpu_feature_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_cpu_feature");
    if (fn) return fn;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t }, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_cpu_feature", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef id = LLVMGetParam(fn, 0);

    bool is_arm = (strstr(g->target_triple, "aarch64") != NULL ||
                   strstr(g->target_triple, "arm64") != NULL);
    bool is_x86 = (strstr(g->target_triple, "x86") != NULL ||
                   strstr(g->target_triple, "amd64") != NULL);

    if (is_arm) {
        LLVMValueRef is_neon = zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 6, 0), "is_neon");
        LLVMValueRef is_pop = zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 1, 0), "is_pop");
        LLVMValueRef is_lz = zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 2, 0), "is_lz");
        LLVMValueRef ok = zan_or(g->builder, is_neon, zan_or(g->builder, is_pop, is_lz, "pop_or_lz"), "ok");
        LLVMBuildRet(g->builder, LLVMBuildZExt(g->builder, ok, i32t, "res"));
    } else if (is_x86) {
        LLVMBasicBlockRef b_neon = LLVMAppendBasicBlockInContext(g->ctx, fn, "b_neon");
        LLVMBasicBlockRef b_cpuid = LLVMAppendBasicBlockInContext(g->ctx, fn, "b_cpuid");

        LLVMValueRef is_neon = zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 6, 0), "is_neon");
        LLVMBuildCondBr(g->builder, is_neon, b_neon, b_cpuid);

        LLVMPositionBuilderAtEnd(g->builder, b_neon);
        LLVMBuildRet(g->builder, LLVMConstInt(i32t, 0, 0));

        LLVMPositionBuilderAtEnd(g->builder, b_cpuid);
        LLVMValueRef is_id4 = zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 4, 0), "is_avx2");
        LLVMValueRef is_id2 = zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 2, 0), "is_lzcnt");

        LLVMValueRef leaf = LLVMBuildSelect(g->builder, is_id4,
            LLVMConstInt(i32t, 7, 0),
            LLVMBuildSelect(g->builder, is_id2,
                LLVMConstInt(i32t, 0x80000001u, 0),
                LLVMConstInt(i32t, 1, 0), "leaf2"), "leaf");
        LLVMValueRef subleaf = LLVMConstInt(i32t, 0, 0);

        LLVMTypeRef cpuid_ret_ty = LLVMStructTypeInContext(g->ctx,
            (LLVMTypeRef[]){ i32t, i32t, i32t, i32t }, 4, 0);
        LLVMTypeRef cpuid_fn_ty = LLVMFunctionType(cpuid_ret_ty,
            (LLVMTypeRef[]){ i32t, i32t }, 2, 0);
        LLVMValueRef cpuid_asm = LLVMGetInlineAsm(cpuid_fn_ty,
            "cpuid", 5,
            "={ax},={bx},={cx},={dx},{ax},{cx}", 34,
            1, 0, LLVMInlineAsmDialectATT, 0);

        LLVMValueRef regs = LLVMBuildCall2(g->builder, cpuid_fn_ty, cpuid_asm,
            (LLVMValueRef[]){ leaf, subleaf }, 2, "cpuid_res");
        LLVMValueRef ebx = LLVMBuildExtractValue(g->builder, regs, 1, "ebx");
        LLVMValueRef ecx = LLVMBuildExtractValue(g->builder, regs, 2, "ecx");

        LLVMValueRef is_id1 = zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 1, 0), "is_id1");
        LLVMValueRef is_id3 = zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 3, 0), "is_id3");
        LLVMValueRef is_id5 = zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 5, 0), "is_id5");

        LLVMValueRef shift = LLVMBuildSelect(g->builder, is_id1, LLVMConstInt(i32t, 23, 0),
            LLVMBuildSelect(g->builder, is_id2, LLVMConstInt(i32t, 5, 0),
            LLVMBuildSelect(g->builder, is_id3, LLVMConstInt(i32t, 20, 0),
            LLVMBuildSelect(g->builder, is_id4, LLVMConstInt(i32t, 5, 0),
            LLVMBuildSelect(g->builder, is_id5, LLVMConstInt(i32t, 25, 0),
                LLVMConstInt(i32t, 0, 0), "sh5"), "sh4"), "sh3"), "sh2"), "shift");

        LLVMValueRef reg = LLVMBuildSelect(g->builder, is_id4, ebx, ecx, "reg");
        LLVMValueRef bit = zan_and(g->builder, zan_lshr(g->builder, reg, shift, "s"), LLVMConstInt(i32t, 1, 0), "bit");
        LLVMBuildRet(g->builder, bit);
    } else {
        /* 非 x86 目标平台 CPU 特性回退：默认报告不支持该专属特性 */
        LLVMBuildRet(g->builder, LLVMConstInt(i32t, 0, 0));
    }

    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}
bool emit_cpu_call(zan_irgen_t *g, zan_ast_node_t *expr,
                          local_scope_t *locals, LLVMValueRef *out) {
    if (expr->kind != AST_CALL) return false;
    zan_ast_node_t *callee = expr->call.callee;
    if (callee->kind != AST_MEMBER_ACCESS) return false;

    zan_ast_node_t *obj = callee->member.object;
    bool is_cpu = false;
    if (obj->kind == AST_IDENTIFIER) {
        if (obj->ident.name.len == 3 && memcmp(obj->ident.name.str, "Cpu", 3) == 0)
            is_cpu = true;
    } else if (obj->kind == AST_MEMBER_ACCESS) {
        if (obj->member.name.len == 3 && memcmp(obj->member.name.str, "Cpu", 3) == 0)
            is_cpu = true;
    }
    if (!is_cpu) return false;

    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    zan_istr_t method = callee->member.name;

    if (method.len == 10 && memcmp(method.str, "CpuFeature", 10) == 0 && expr->call.args.count == 1) {
        LLVMValueRef id = coerce_int_to(g, emit_expr(g, expr->call.args.items[0], locals), i32t);
        LLVMValueRef fn = cpu_feature_fn(g);
        LLVMTypeRef fnty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t }, 1, 0);
        *out = zan_call2(g->builder, fnty, fn, &id, 1, "cpu_feat");
        return true;
    }

    int feat_id = 0;
    if (method.len == 9 && memcmp(method.str, "HasPopcnt", 9) == 0) feat_id = 1;
    else if (method.len == 8 && memcmp(method.str, "HasLzcnt", 8) == 0) feat_id = 2;
    else if (method.len == 8 && memcmp(method.str, "HasSse42", 8) == 0) feat_id = 3;
    else if (method.len == 7 && memcmp(method.str, "HasAvx2", 7) == 0) feat_id = 4;
    else if (method.len == 8 && memcmp(method.str, "HasAesNi", 8) == 0) feat_id = 5;
    else if (method.len == 7 && memcmp(method.str, "HasNeon", 7) == 0) feat_id = 6;

    if (feat_id > 0) {
        LLVMValueRef id = LLVMConstInt(i32t, feat_id, 0);
        LLVMValueRef fn = cpu_feature_fn(g);
        LLVMTypeRef fnty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t }, 1, 0);
        LLVMValueRef val = zan_call2(g->builder, fnty, fn, &id, 1, "cpu_feat");
        *out = zan_icmp(g->builder, LLVMIntNE, val, LLVMConstInt(i32t, 0, 0), "has_feat");
        return true;
    }
    return false;
}

static bool is_64bit_int_arg(zan_irgen_t *g, zan_ast_node_t *arg, LLVMValueRef v, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, arg, locals);
    if (t) {
        return (t->kind == TYPE_LONG || t->kind == TYPE_ULONG);
    }
    return (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(LLVMTypeOf(v)) == 64);
}
bool emit_bit_operations_call(zan_irgen_t *g, zan_ast_node_t *expr,
                                     local_scope_t *locals, LLVMValueRef *out) {
    if (expr->kind != AST_CALL) return false;
    if (expr->call.resolved_sym != NULL && !expr->call.is_intrinsic) return false;
    zan_ast_node_t *callee = expr->call.callee;
    if (callee->kind != AST_MEMBER_ACCESS) return false;

    zan_ast_node_t *obj = callee->member.object;
    bool is_bitops = false;
    if (obj->kind == AST_IDENTIFIER) {
        if (istr_matches_class(obj->ident.name, "BitOperations"))
            is_bitops = true;
    } else if (obj->kind == AST_MEMBER_ACCESS) {
        if (istr_matches_class(obj->member.name, "BitOperations"))
            is_bitops = true;
    }
    if (!is_bitops && expr->call.resolved_sym && expr->call.resolved_sym->parent) {
        if (istr_matches_class(expr->call.resolved_sym->parent->name, "BitOperations"))
            is_bitops = true;
    }
    if (!is_bitops) return false;

    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i1t = LLVMInt1TypeInContext(g->ctx);
    zan_istr_t method = callee->member.name;

    if (method.len == 8 && memcmp(method.str, "PopCount", 8) == 0 && expr->call.args.count == 1) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        if (is64) {
            v = coerce_int_to(g, v, i64t);
            LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i64t }, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctpop.i64");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.ctpop.i64", fty);
            LLVMValueRef res = zan_call2(g->builder, fty, fn, &v, 1, "popcnt");
            *out = LLVMBuildTrunc(g->builder, res, i32t, "popcnt32");
        } else {
            v = coerce_int_to(g, v, i32t);
            LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t }, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctpop.i32");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.ctpop.i32", fty);
            *out = zan_call2(g->builder, fty, fn, &v, 1, "popcnt");
        }
        return true;
    }

    if (method.len == 16 && memcmp(method.str, "LeadingZeroCount", 16) == 0 && expr->call.args.count == 1) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        if (is64) {
            v = coerce_int_to(g, v, i64t);
            LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i64t, i1t }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i64");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.ctlz.i64", fty);
            LLVMValueRef args[] = { v, LLVMConstInt(i1t, 0, 0) };
            LLVMValueRef res = zan_call2(g->builder, fty, fn, args, 2, "ctlz");
            *out = LLVMBuildTrunc(g->builder, res, i32t, "ctlz32");
        } else {
            v = coerce_int_to(g, v, i32t);
            LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t, i1t }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i32");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.ctlz.i32", fty);
            LLVMValueRef args[] = { v, LLVMConstInt(i1t, 0, 0) };
            *out = zan_call2(g->builder, fty, fn, args, 2, "ctlz");
        }
        return true;
    }

    if (method.len == 17 && memcmp(method.str, "TrailingZeroCount", 17) == 0 && expr->call.args.count == 1) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        if (is64) {
            v = coerce_int_to(g, v, i64t);
            LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i64t, i1t }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.cttz.i64");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.cttz.i64", fty);
            LLVMValueRef args[] = { v, LLVMConstInt(i1t, 0, 0) };
            LLVMValueRef res = zan_call2(g->builder, fty, fn, args, 2, "cttz");
            *out = LLVMBuildTrunc(g->builder, res, i32t, "cttz32");
        } else {
            v = coerce_int_to(g, v, i32t);
            LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t, i1t }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.cttz.i32");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.cttz.i32", fty);
            LLVMValueRef args[] = { v, LLVMConstInt(i1t, 0, 0) };
            *out = zan_call2(g->builder, fty, fn, args, 2, "cttz");
        }
        return true;
    }

    if (method.len == 10 && memcmp(method.str, "RotateLeft", 10) == 0 && expr->call.args.count == 2) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        LLVMValueRef off = emit_expr(g, expr->call.args.items[1], locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        if (is64) {
            v = coerce_int_to(g, v, i64t);
            off = coerce_int_to(g, off, i64t);
            LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i64t, i64t, i64t }, 3, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.fshl.i64");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.fshl.i64", fty);
            LLVMValueRef args[] = { v, v, off };
            *out = zan_call2(g->builder, fty, fn, args, 3, "rotl");
        } else {
            v = coerce_int_to(g, v, i32t);
            off = coerce_int_to(g, off, i32t);
            LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t, i32t, i32t }, 3, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.fshl.i32");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.fshl.i32", fty);
            LLVMValueRef args[] = { v, v, off };
            *out = zan_call2(g->builder, fty, fn, args, 3, "rotl");
        }
        return true;
    }

    if (method.len == 11 && memcmp(method.str, "RotateRight", 11) == 0 && expr->call.args.count == 2) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        LLVMValueRef off = emit_expr(g, expr->call.args.items[1], locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        if (is64) {
            v = coerce_int_to(g, v, i64t);
            off = coerce_int_to(g, off, i64t);
            LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i64t, i64t, i64t }, 3, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.fshr.i64");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.fshr.i64", fty);
            LLVMValueRef args[] = { v, v, off };
            *out = zan_call2(g->builder, fty, fn, args, 3, "rotr");
        } else {
            v = coerce_int_to(g, v, i32t);
            off = coerce_int_to(g, off, i32t);
            LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t, i32t, i32t }, 3, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.fshr.i32");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.fshr.i32", fty);
            LLVMValueRef args[] = { v, v, off };
            *out = zan_call2(g->builder, fty, fn, args, 3, "rotr");
        }
        return true;
    }

    if (method.len == 17 && memcmp(method.str, "ReverseEndianness", 17) == 0 && expr->call.args.count == 1) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        if (is64) {
            v = coerce_int_to(g, v, i64t);
            LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i64t }, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.bswap.i64");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.bswap.i64", fty);
            *out = zan_call2(g->builder, fty, fn, &v, 1, "bswap");
        } else {
            v = coerce_int_to(g, v, i32t);
            LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t }, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.bswap.i32");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.bswap.i32", fty);
            *out = zan_call2(g->builder, fty, fn, &v, 1, "bswap");
        }
        return true;
    }

    if (method.len == 4 && memcmp(method.str, "Log2", 4) == 0 && expr->call.args.count == 1) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        if (is64) {
            v = coerce_int_to(g, v, i64t);
            LLVMValueRef vor = zan_or(g->builder, v, LLVMConstInt(i64t, 1, 0), "vor");
            LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i64t, i1t }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i64");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.ctlz.i64", fty);
            LLVMValueRef args[] = { vor, LLVMConstInt(i1t, 0, 0) };
            LLVMValueRef clz = zan_call2(g->builder, fty, fn, args, 2, "ctlz");
            LLVMValueRef sub = zan_sub(g->builder, LLVMConstInt(i64t, 63, 0), clz, "log2");
            *out = LLVMBuildTrunc(g->builder, sub, i32t, "log2_32");
        } else {
            v = coerce_int_to(g, v, i32t);
            LLVMValueRef vor = zan_or(g->builder, v, LLVMConstInt(i32t, 1, 0), "vor");
            LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t, i1t }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i32");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.ctlz.i32", fty);
            LLVMValueRef args[] = { vor, LLVMConstInt(i1t, 0, 0) };
            LLVMValueRef clz = zan_call2(g->builder, fty, fn, args, 2, "ctlz");
            *out = zan_sub(g->builder, LLVMConstInt(i32t, 31, 0), clz, "log2");
        }
        return true;
    }

    if (method.len == 6 && memcmp(method.str, "IsPow2", 6) == 0 && expr->call.args.count == 1) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        LLVMValueRef zero = is64 ? LLVMConstInt(i64t, 0, 0) : LLVMConstInt(i32t, 0, 0);
        LLVMValueRef one = is64 ? LLVMConstInt(i64t, 1, 0) : LLVMConstInt(i32t, 1, 0);
        LLVMValueRef gt0 = zan_icmp(g->builder, LLVMIntSGT, v, zero, "gt0");
        LLVMValueRef sub1 = zan_sub(g->builder, v, one, "sub1");
        LLVMValueRef andv = zan_and(g->builder, v, sub1, "andv");
        LLVMValueRef eq0 = zan_icmp(g->builder, LLVMIntEQ, andv, zero, "eq0");
        *out = zan_and(g->builder, gt0, eq0, "ispow2");
        return true;
    }

    if (method.len == 17 && memcmp(method.str, "RoundUpToPowerOf2", 17) == 0 && expr->call.args.count == 1) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        if (is64) {
            v = coerce_int_to(g, v, i64t);
            LLVMValueRef vm1 = zan_sub(g->builder, v, LLVMConstInt(i64t, 1, 0), "vm1");
            LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i64t, i1t }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i64");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.ctlz.i64", fty);
            LLVMValueRef args[] = { vm1, LLVMConstInt(i1t, 0, 0) };
            LLVMValueRef clz = zan_call2(g->builder, fty, fn, args, 2, "ctlz");
            LLVMValueRef sh = zan_sub(g->builder, LLVMConstInt(i64t, 64, 0), clz, "sh");
            LLVMValueRef pow2 = zan_shl(g->builder, LLVMConstInt(i64t, 1, 0), sh, "pow2");
            LLVMValueRef le1 = zan_icmp(g->builder, LLVMIntSLE, v, LLVMConstInt(i64t, 1, 0), "le1");
            *out = LLVMBuildSelect(g->builder, le1, LLVMConstInt(i64t, 1, 0), pow2, "roundup");
        } else {
            v = coerce_int_to(g, v, i32t);
            LLVMValueRef vm1 = zan_sub(g->builder, v, LLVMConstInt(i32t, 1, 0), "vm1");
            LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t, i1t }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i32");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.ctlz.i32", fty);
            LLVMValueRef args[] = { vm1, LLVMConstInt(i1t, 0, 0) };
            LLVMValueRef clz = zan_call2(g->builder, fty, fn, args, 2, "ctlz");
            LLVMValueRef sh = zan_sub(g->builder, LLVMConstInt(i32t, 32, 0), clz, "sh");
            LLVMValueRef pow2 = zan_shl(g->builder, LLVMConstInt(i32t, 1, 0), sh, "pow2");
            LLVMValueRef le1 = zan_icmp(g->builder, LLVMIntSLE, v, LLVMConstInt(i32t, 1, 0), "le1");
            *out = LLVMBuildSelect(g->builder, le1, LLVMConstInt(i32t, 1, 0), pow2, "roundup");
        }
        return true;
    }

    if (method.len == 17 && memcmp(method.str, "ResetLowestSetBit", 17) == 0 && expr->call.args.count == 1) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        LLVMValueRef one = is64 ? LLVMConstInt(i64t, 1, 0) : LLVMConstInt(i32t, 1, 0);
        LLVMValueRef sub1 = zan_sub(g->builder, v, one, "blsr_sub");
        *out = zan_and(g->builder, v, sub1, "blsr");
        return true;
    }

    if (method.len == 19 && memcmp(method.str, "ExtractLowestSetBit", 19) == 0 && expr->call.args.count == 1) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        LLVMValueRef zero = is64 ? LLVMConstInt(i64t, 0, 0) : LLVMConstInt(i32t, 0, 0);
        LLVMValueRef neg = zan_sub(g->builder, zero, v, "blsi_neg");
        *out = zan_and(g->builder, v, neg, "blsi");
        return true;
    }

    return false;
}
