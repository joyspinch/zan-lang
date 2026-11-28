/* __zan_cpu_feature(i32 id): 底层 CPUID 特性探测接口 */
LLVMValueRef cpu_feature_fn(zan_irgen_t *g) {
  LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_cpu_feature");
  if (fn)
    return fn;
  LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef fn_ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){i32t}, 1, 0);
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
    LLVMValueRef is_neon = zan_icmp(g->builder, LLVMIntEQ, id,
                                    LLVMConstInt(i32t, 6, 0), "is_neon");
    LLVMValueRef is_pop =
        zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 1, 0), "is_pop");
    LLVMValueRef is_lz =
        zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 2, 0), "is_lz");
    LLVMValueRef ok =
        zan_or(g->builder, is_neon,
               zan_or(g->builder, is_pop, is_lz, "pop_or_lz"), "ok");
    LLVMBuildRet(g->builder, LLVMBuildZExt(g->builder, ok, i32t, "res"));
  } else if (is_x86) {
    LLVMBasicBlockRef b_neon =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "b_neon");
    LLVMBasicBlockRef b_cpuid =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "b_cpuid");

    LLVMValueRef is_neon = zan_icmp(g->builder, LLVMIntEQ, id,
                                    LLVMConstInt(i32t, 6, 0), "is_neon");
    LLVMBuildCondBr(g->builder, is_neon, b_neon, b_cpuid);

    LLVMPositionBuilderAtEnd(g->builder, b_neon);
    LLVMBuildRet(g->builder, LLVMConstInt(i32t, 0, 0));

    LLVMPositionBuilderAtEnd(g->builder, b_cpuid);
    LLVMValueRef is_id4 = zan_icmp(g->builder, LLVMIntEQ, id,
                                   LLVMConstInt(i32t, 4, 0), "is_avx2");
    LLVMValueRef is_id2 = zan_icmp(g->builder, LLVMIntEQ, id,
                                   LLVMConstInt(i32t, 2, 0), "is_lzcnt");

    LLVMValueRef leaf = LLVMBuildSelect(
        g->builder, is_id4, LLVMConstInt(i32t, 7, 0),
        LLVMBuildSelect(g->builder, is_id2, LLVMConstInt(i32t, 0x80000001u, 0),
                        LLVMConstInt(i32t, 1, 0), "leaf2"),
        "leaf");
    LLVMValueRef subleaf = LLVMConstInt(i32t, 0, 0);

    LLVMTypeRef cpuid_ret_ty = LLVMStructTypeInContext(
        g->ctx, (LLVMTypeRef[]){i32t, i32t, i32t, i32t}, 4, 0);
    LLVMTypeRef cpuid_fn_ty =
        LLVMFunctionType(cpuid_ret_ty, (LLVMTypeRef[]){i32t, i32t}, 2, 0);
    LLVMValueRef cpuid_asm = LLVMGetInlineAsm(
        cpuid_fn_ty, "cpuid", 5, "={ax},={bx},={cx},={dx},{ax},{cx}", 34, 1, 0,
        LLVMInlineAsmDialectATT, 0);

    LLVMValueRef regs =
        LLVMBuildCall2(g->builder, cpuid_fn_ty, cpuid_asm,
                       (LLVMValueRef[]){leaf, subleaf}, 2, "cpuid_res");
    LLVMValueRef ebx = LLVMBuildExtractValue(g->builder, regs, 1, "ebx");
    LLVMValueRef ecx = LLVMBuildExtractValue(g->builder, regs, 2, "ecx");

    LLVMValueRef is_id1 =
        zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 1, 0), "is_id1");
    LLVMValueRef is_id3 =
        zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 3, 0), "is_id3");
    LLVMValueRef is_id5 =
        zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 5, 0), "is_id5");

    LLVMValueRef shift = LLVMBuildSelect(
        g->builder, is_id1, LLVMConstInt(i32t, 23, 0),
        LLVMBuildSelect(
            g->builder, is_id2, LLVMConstInt(i32t, 5, 0),
            LLVMBuildSelect(
                g->builder, is_id3, LLVMConstInt(i32t, 20, 0),
                LLVMBuildSelect(g->builder, is_id4, LLVMConstInt(i32t, 5, 0),
                                LLVMBuildSelect(g->builder, is_id5,
                                                LLVMConstInt(i32t, 25, 0),
                                                LLVMConstInt(i32t, 0, 0),
                                                "sh5"),
                                "sh4"),
                "sh3"),
            "sh2"),
        "shift");

    LLVMValueRef reg = LLVMBuildSelect(g->builder, is_id4, ebx, ecx, "reg");
    LLVMValueRef bit =
        zan_and(g->builder, zan_lshr(g->builder, reg, shift, "s"),
                LLVMConstInt(i32t, 1, 0), "bit");
    LLVMBuildRet(g->builder, bit);
  } else {
    /* 非 x86 目标平台 CPU 特性回退：默认报告不支持该专属特性 */
    LLVMBuildRet(g->builder, LLVMConstInt(i32t, 0, 0));
  }

  if (saved)
    LLVMPositionBuilderAtEnd(g->builder, saved);
  return fn;
}
bool emit_cpu_call(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals,
                   LLVMValueRef *out) {
  if (expr->kind != AST_CALL)
    return false;
  zan_ast_node_t *callee = expr->call.callee;
  if (callee->kind != AST_MEMBER_ACCESS)
    return false;

  zan_ast_node_t *obj = callee->member.object;
  bool is_cpu = false;
  if (obj->kind == AST_IDENTIFIER) {
    if (obj->ident.name.len == 3 && memcmp(obj->ident.name.str, "Cpu", 3) == 0)
      is_cpu = true;
  } else if (obj->kind == AST_MEMBER_ACCESS) {
    if (obj->member.name.len == 3 &&
        memcmp(obj->member.name.str, "Cpu", 3) == 0)
      is_cpu = true;
  }
  if (!is_cpu)
    return false;

  LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
  zan_istr_t method = callee->member.name;

  if (method.len == 10 && memcmp(method.str, "CpuFeature", 10) == 0 &&
      expr->call.args.count == 1) {
    LLVMValueRef id =
        coerce_int_to(g, emit_expr(g, expr->call.args.items[0], locals), i32t);
    LLVMValueRef fn = cpu_feature_fn(g);
    LLVMTypeRef fnty = LLVMFunctionType(i32t, (LLVMTypeRef[]){i32t}, 1, 0);
    *out = zan_call2(g->builder, fnty, fn, &id, 1, "cpu_feat");
    return true;
  }

  int feat_id = 0;
  if (method.len == 9 && memcmp(method.str, "HasPopcnt", 9) == 0)
    feat_id = 1;
  else if (method.len == 8 && memcmp(method.str, "HasLzcnt", 8) == 0)
    feat_id = 2;
  else if (method.len == 8 && memcmp(method.str, "HasSse42", 8) == 0)
    feat_id = 3;
  else if (method.len == 7 && memcmp(method.str, "HasAvx2", 7) == 0)
    feat_id = 4;
  else if (method.len == 8 && memcmp(method.str, "HasAesNi", 8) == 0)
    feat_id = 5;
  else if (method.len == 7 && memcmp(method.str, "HasNeon", 7) == 0)
    feat_id = 6;

  if (feat_id > 0) {
    LLVMValueRef id = LLVMConstInt(i32t, feat_id, 0);
    LLVMValueRef fn = cpu_feature_fn(g);
    LLVMTypeRef fnty = LLVMFunctionType(i32t, (LLVMTypeRef[]){i32t}, 1, 0);
    LLVMValueRef val = zan_call2(g->builder, fnty, fn, &id, 1, "cpu_feat");
    *out = zan_icmp(g->builder, LLVMIntNE, val, LLVMConstInt(i32t, 0, 0),
                    "has_feat");
    return true;
  }
  return false;
}

static bool is_64bit_int_arg(zan_irgen_t *g, zan_ast_node_t *arg,
                             LLVMValueRef v, local_scope_t *locals) {
  zan_type_t *t = infer_expr_type(g, arg, locals);
  if (t) {
    return (t->kind == TYPE_LONG || t->kind == TYPE_ULONG);
  }
  return (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind &&
          LLVMGetIntTypeWidth(LLVMTypeOf(v)) == 64);
}
bool emit_bit_operations_call(zan_irgen_t *g, zan_ast_node_t *expr,
                              local_scope_t *locals, LLVMValueRef *out) {
  if (expr->kind != AST_CALL)
    return false;
  if (expr->call.resolved_sym != NULL && !expr->call.is_intrinsic)
    return false;
  zan_ast_node_t *callee = expr->call.callee;
  if (callee->kind != AST_MEMBER_ACCESS)
    return false;

  zan_ast_node_t *obj = callee->member.object;
  bool is_bitops = false;
  if (obj->kind == AST_IDENTIFIER) {
    if (istr_matches_class(obj->ident.name, "BitOperations"))
      is_bitops = true;
  } else if (obj->kind == AST_MEMBER_ACCESS) {
    if (istr_matches_class(obj->member.name, "BitOperations"))
      is_bitops = true;
  }
  if (!is_bitops && expr->call.resolved_sym &&
      expr->call.resolved_sym->parent) {
    if (istr_matches_class(expr->call.resolved_sym->parent->name,
                           "BitOperations"))
      is_bitops = true;
  }
  if (!is_bitops)
    return false;

  LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i1t = LLVMInt1TypeInContext(g->ctx);
  zan_istr_t method = callee->member.name;

  if (method.len == 8 && memcmp(method.str, "PopCount", 8) == 0 &&
      expr->call.args.count == 1) {
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    LLVMValueRef v = emit_expr(g, arg0, locals);
    bool is64 = is_64bit_int_arg(g, arg0, v, locals);
    if (is64) {
      v = coerce_int_to(g, v, i64t);
      LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){i64t}, 1, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctpop.i64");
      if (!fn)
        fn = LLVMAddFunction(g->mod, "llvm.ctpop.i64", fty);
      LLVMValueRef res = zan_call2(g->builder, fty, fn, &v, 1, "popcnt");
      *out = LLVMBuildTrunc(g->builder, res, i32t, "popcnt32");
    } else {
      v = coerce_int_to(g, v, i32t);
      LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){i32t}, 1, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctpop.i32");
      if (!fn)
        fn = LLVMAddFunction(g->mod, "llvm.ctpop.i32", fty);
      *out = zan_call2(g->builder, fty, fn, &v, 1, "popcnt");
    }
    return true;
  }

  if (method.len == 16 && memcmp(method.str, "LeadingZeroCount", 16) == 0 &&
      expr->call.args.count == 1) {
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    LLVMValueRef v = emit_expr(g, arg0, locals);
    bool is64 = is_64bit_int_arg(g, arg0, v, locals);
    if (is64) {
      v = coerce_int_to(g, v, i64t);
      LLVMTypeRef fty =
          LLVMFunctionType(i64t, (LLVMTypeRef[]){i64t, i1t}, 2, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i64");
      if (!fn)
        fn = LLVMAddFunction(g->mod, "llvm.ctlz.i64", fty);
      LLVMValueRef args[] = {v, LLVMConstInt(i1t, 0, 0)};
      LLVMValueRef res = zan_call2(g->builder, fty, fn, args, 2, "ctlz");
      *out = LLVMBuildTrunc(g->builder, res, i32t, "ctlz32");
    } else {
      v = coerce_int_to(g, v, i32t);
      LLVMTypeRef fty =
          LLVMFunctionType(i32t, (LLVMTypeRef[]){i32t, i1t}, 2, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i32");
      if (!fn)
        fn = LLVMAddFunction(g->mod, "llvm.ctlz.i32", fty);
      LLVMValueRef args[] = {v, LLVMConstInt(i1t, 0, 0)};
      *out = zan_call2(g->builder, fty, fn, args, 2, "ctlz");
    }
    return true;
  }

  if (method.len == 17 && memcmp(method.str, "TrailingZeroCount", 17) == 0 &&
      expr->call.args.count == 1) {
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    LLVMValueRef v = emit_expr(g, arg0, locals);
    bool is64 = is_64bit_int_arg(g, arg0, v, locals);
    if (is64) {
      v = coerce_int_to(g, v, i64t);
      LLVMTypeRef fty =
          LLVMFunctionType(i64t, (LLVMTypeRef[]){i64t, i1t}, 2, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.cttz.i64");
      if (!fn)
        fn = LLVMAddFunction(g->mod, "llvm.cttz.i64", fty);
      LLVMValueRef args[] = {v, LLVMConstInt(i1t, 0, 0)};
      LLVMValueRef res = zan_call2(g->builder, fty, fn, args, 2, "cttz");
      *out = LLVMBuildTrunc(g->builder, res, i32t, "cttz32");
    } else {
      v = coerce_int_to(g, v, i32t);
      LLVMTypeRef fty =
          LLVMFunctionType(i32t, (LLVMTypeRef[]){i32t, i1t}, 2, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.cttz.i32");
      if (!fn)
        fn = LLVMAddFunction(g->mod, "llvm.cttz.i32", fty);
      LLVMValueRef args[] = {v, LLVMConstInt(i1t, 0, 0)};
      *out = zan_call2(g->builder, fty, fn, args, 2, "cttz");
    }
    return true;
  }

  if (method.len == 10 && memcmp(method.str, "RotateLeft", 10) == 0 &&
      expr->call.args.count == 2) {
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    LLVMValueRef v = emit_expr(g, arg0, locals);
    LLVMValueRef off = emit_expr(g, expr->call.args.items[1], locals);
    bool is64 = is_64bit_int_arg(g, arg0, v, locals);
    if (is64) {
      v = coerce_int_to(g, v, i64t);
      off = coerce_int_to(g, off, i64t);
      LLVMTypeRef fty =
          LLVMFunctionType(i64t, (LLVMTypeRef[]){i64t, i64t, i64t}, 3, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.fshl.i64");
      if (!fn)
        fn = LLVMAddFunction(g->mod, "llvm.fshl.i64", fty);
      LLVMValueRef args[] = {v, v, off};
      *out = zan_call2(g->builder, fty, fn, args, 3, "rotl");
    } else {
      v = coerce_int_to(g, v, i32t);
      off = coerce_int_to(g, off, i32t);
      LLVMTypeRef fty =
          LLVMFunctionType(i32t, (LLVMTypeRef[]){i32t, i32t, i32t}, 3, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.fshl.i32");
      if (!fn)
        fn = LLVMAddFunction(g->mod, "llvm.fshl.i32", fty);
      LLVMValueRef args[] = {v, v, off};
      *out = zan_call2(g->builder, fty, fn, args, 3, "rotl");
    }
    return true;
  }

  if (method.len == 11 && memcmp(method.str, "RotateRight", 11) == 0 &&
      expr->call.args.count == 2) {
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    LLVMValueRef v = emit_expr(g, arg0, locals);
    LLVMValueRef off = emit_expr(g, expr->call.args.items[1], locals);
    bool is64 = is_64bit_int_arg(g, arg0, v, locals);
    if (is64) {
      v = coerce_int_to(g, v, i64t);
      off = coerce_int_to(g, off, i64t);
      LLVMTypeRef fty =
          LLVMFunctionType(i64t, (LLVMTypeRef[]){i64t, i64t, i64t}, 3, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.fshr.i64");
      if (!fn)
        fn = LLVMAddFunction(g->mod, "llvm.fshr.i64", fty);
      LLVMValueRef args[] = {v, v, off};
      *out = zan_call2(g->builder, fty, fn, args, 3, "rotr");
    } else {
      v = coerce_int_to(g, v, i32t);
      off = coerce_int_to(g, off, i32t);
      LLVMTypeRef fty =
          LLVMFunctionType(i32t, (LLVMTypeRef[]){i32t, i32t, i32t}, 3, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.fshr.i32");
      if (!fn)
        fn = LLVMAddFunction(g->mod, "llvm.fshr.i32", fty);
      LLVMValueRef args[] = {v, v, off};
      *out = zan_call2(g->builder, fty, fn, args, 3, "rotr");
    }
    return true;
  }

  if (method.len == 17 && memcmp(method.str, "ReverseEndianness", 17) == 0 &&
      expr->call.args.count == 1) {
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    zan_type_t *t0 = infer_expr_type(g, arg0, locals);
    LLVMValueRef v = emit_expr(g, arg0, locals);
    bool is16 = t0 && (t0->kind == TYPE_SHORT || t0->kind == TYPE_USHORT);
    bool is64 = !is16 && is_64bit_int_arg(g, arg0, v, locals);
    if (is16) {
      LLVMTypeRef i16t = LLVMInt16TypeInContext(g->ctx);
      v = coerce_int_to(g, v, i16t);
      LLVMTypeRef fty = LLVMFunctionType(i16t, (LLVMTypeRef[]){i16t}, 1, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.bswap.i16");
      if (!fn)
        fn = LLVMAddFunction(g->mod, "llvm.bswap.i16", fty);
      *out = zan_call2(g->builder, fty, fn, &v, 1, "bswap16");
    } else if (is64) {
      v = coerce_int_to(g, v, i64t);
      LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){i64t}, 1, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.bswap.i64");
      if (!fn)
        fn = LLVMAddFunction(g->mod, "llvm.bswap.i64", fty);
      *out = zan_call2(g->builder, fty, fn, &v, 1, "bswap");
    } else {
      v = coerce_int_to(g, v, i32t);
      LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){i32t}, 1, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.bswap.i32");
      if (!fn)
        fn = LLVMAddFunction(g->mod, "llvm.bswap.i32", fty);
      *out = zan_call2(g->builder, fty, fn, &v, 1, "bswap");
    }
    return true;
  }

  if (method.len == 11 && memcmp(method.str, "ExtractBits", 11) == 0 &&
      expr->call.args.count == 3) {
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    LLVMValueRef val = emit_expr(g, arg0, locals);
    LLVMValueRef start = emit_expr(g, expr->call.args.items[1], locals);
    LLVMValueRef count = emit_expr(g, expr->call.args.items[2], locals);
    bool is64 = is_64bit_int_arg(g, arg0, val, locals);
    LLVMTypeRef wt = is64 ? i64t : i32t;
    unsigned bits = is64 ? 64u : 32u;
    val = coerce_int_to(g, val, wt);
    start = coerce_int_to(g, start, wt);
    count = coerce_int_to(g, count, wt);
    LLVMValueRef zero = LLVMConstInt(wt, 0, 0);
    LLVMValueRef one = LLVMConstInt(wt, 1, 0);
    LLVMValueRef all1 = LLVMConstAllOnes(wt);
    LLVMValueRef wconst = LLVMConstInt(wt, bits, 0);
    LLVMValueRef start_ok = LLVMBuildICmp(g->builder, LLVMIntULT, start, wconst, "bfx.sok");
    LLVMValueRef cnt_pos = LLVMBuildICmp(g->builder, LLVMIntSGT, count, zero, "bfx.cpos");
    LLVMValueRef valid = LLVMBuildAnd(g->builder, start_ok, cnt_pos, "bfx.ok");
    LLVMValueRef safe_start = LLVMBuildSelect(g->builder, start_ok, start, zero, "bfx.s");
    LLVMValueRef shifted = LLVMBuildLShr(g->builder, val, safe_start, "bfx.shr");
    LLVMValueRef cnt_lt_w = LLVMBuildICmp(g->builder, LLVMIntULT, count, wconst, "bfx.cltw");
    LLVMValueRef safe_cnt = LLVMBuildSelect(g->builder, cnt_lt_w, count, zero, "bfx.c");
    LLVMValueRef partial_mask = LLVMBuildSub(
        g->builder, LLVMBuildShl(g->builder, one, safe_cnt, "bfx.p2"), one, "bfx.pm");
    LLVMValueRef mask = LLVMBuildSelect(g->builder, cnt_lt_w, partial_mask, all1, "bfx.m");
    LLVMValueRef res = LLVMBuildAnd(g->builder, shifted, mask, "bfx.res");
    *out = LLVMBuildSelect(g->builder, valid, res, zero, "bfx");
    return true;
  }

  if (method.len == 10 && memcmp(method.str, "InsertBits", 10) == 0 &&
      expr->call.args.count == 4) {
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    LLVMValueRef dst = emit_expr(g, arg0, locals);
    LLVMValueRef fld = emit_expr(g, expr->call.args.items[1], locals);
    LLVMValueRef start = emit_expr(g, expr->call.args.items[2], locals);
    LLVMValueRef count = emit_expr(g, expr->call.args.items[3], locals);
    bool is64 = is_64bit_int_arg(g, arg0, dst, locals);
    LLVMTypeRef wt = is64 ? i64t : i32t;
    unsigned bits = is64 ? 64u : 32u;
    dst = coerce_int_to(g, dst, wt);
    fld = coerce_int_to(g, fld, wt);
    start = coerce_int_to(g, start, wt);
    count = coerce_int_to(g, count, wt);
    LLVMValueRef zero = LLVMConstInt(wt, 0, 0);
    LLVMValueRef one = LLVMConstInt(wt, 1, 0);
    LLVMValueRef all1 = LLVMConstAllOnes(wt);
    LLVMValueRef wconst = LLVMConstInt(wt, bits, 0);
    LLVMValueRef start_ok = LLVMBuildICmp(g->builder, LLVMIntULT, start, wconst, "bfi.sok");
    LLVMValueRef cnt_pos = LLVMBuildICmp(g->builder, LLVMIntSGT, count, zero, "bfi.cpos");
    LLVMValueRef valid = LLVMBuildAnd(g->builder, start_ok, cnt_pos, "bfi.ok");
    LLVMValueRef safe_start = LLVMBuildSelect(g->builder, start_ok, start, zero, "bfi.s");
    LLVMValueRef cnt_lt_w = LLVMBuildICmp(g->builder, LLVMIntULT, count, wconst, "bfi.cltw");
    LLVMValueRef safe_cnt = LLVMBuildSelect(g->builder, cnt_lt_w, count, zero, "bfi.c");
    LLVMValueRef partial_mask = LLVMBuildSub(
        g->builder, LLVMBuildShl(g->builder, one, safe_cnt, "bfi.p2"), one, "bfi.pm");
    LLVMValueRef mask = LLVMBuildSelect(g->builder, cnt_lt_w, partial_mask, all1, "bfi.m");
    LLVMValueRef shifted_mask = LLVMBuildShl(g->builder, mask, safe_start, "bfi.sm");
    LLVMValueRef clear_mask = LLVMBuildXor(g->builder, shifted_mask, all1, "bfi.cm");
    LLVMValueRef cleared = LLVMBuildAnd(g->builder, dst, clear_mask, "bfi.clr");
    LLVMValueRef placed = LLVMBuildShl(
        g->builder, LLVMBuildAnd(g->builder, fld, mask, "bfi.f"), safe_start, "bfi.pl");
    LLVMValueRef merged = LLVMBuildOr(g->builder, cleared, placed, "bfi.or");
    *out = LLVMBuildSelect(g->builder, valid, merged, dst, "bfi");
    return true;
  }

  if (((method.len == 15 && memcmp(method.str, "FunnelShiftLeft", 15) == 0) ||
       (method.len == 16 && memcmp(method.str, "FunnelShiftRight", 16) == 0)) &&
      expr->call.args.count == 3) {
    bool is_left = (method.len == 15);
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    zan_ast_node_t *arg1 = expr->call.args.items[1];
    zan_ast_node_t *arg2 = expr->call.args.items[2];
    LLVMValueRef hi = emit_expr(g, arg0, locals);
    LLVMValueRef lo = emit_expr(g, arg1, locals);
    LLVMValueRef sh = emit_expr(g, arg2, locals);
    bool is64 = is_64bit_int_arg(g, arg0, hi, locals) ||
                is_64bit_int_arg(g, arg1, lo, locals);
    LLVMTypeRef it = is64 ? i64t : i32t;
    const char *iname = is_left
                            ? (is64 ? "llvm.fshl.i64" : "llvm.fshl.i32")
                            : (is64 ? "llvm.fshr.i64" : "llvm.fshr.i32");
    hi = coerce_int_to(g, hi, it);
    lo = coerce_int_to(g, lo, it);
    sh = coerce_int_to(g, sh, it);
    LLVMTypeRef fty = LLVMFunctionType(it, (LLVMTypeRef[]){it, it, it}, 3, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, iname);
    if (!fn)
      fn = LLVMAddFunction(g->mod, iname, fty);
    LLVMValueRef args[] = {hi, lo, sh};
    *out = zan_call2(g->builder, fty, fn, args, 3, is_left ? "fshl" : "fshr");
    return true;
  }

  if (method.len == 10 && memcmp(method.str, "BitReverse", 10) == 0 &&
      expr->call.args.count == 1) {
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    LLVMValueRef v = emit_expr(g, arg0, locals);
    bool is64 = is_64bit_int_arg(g, arg0, v, locals);
    LLVMTypeRef it = is64 ? i64t : i32t;
    const char *iname = is64 ? "llvm.bitreverse.i64" : "llvm.bitreverse.i32";
    v = coerce_int_to(g, v, it);
    LLVMTypeRef fty = LLVMFunctionType(it, (LLVMTypeRef[]){it}, 1, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, iname);
    if (!fn)
      fn = LLVMAddFunction(g->mod, iname, fty);
    *out = zan_call2(g->builder, fty, fn, &v, 1, "bitrev");
    return true;
  }

  if (((method.len == 13 && memcmp(method.str, "SaturatingAdd", 13) == 0) ||
       (method.len == 13 && memcmp(method.str, "SaturatingSub", 13) == 0)) &&
      expr->call.args.count == 2) {
    bool is_add = (method.str[10] == 'A');
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    zan_ast_node_t *arg1 = expr->call.args.items[1];
    zan_type_t *t0 = infer_expr_type(g, arg0, locals);
    zan_type_t *t1 = infer_expr_type(g, arg1, locals);
    bool is_unsigned = (t0 && (t0->kind == TYPE_UINT || t0->kind == TYPE_ULONG)) ||
                       (t1 && (t1->kind == TYPE_UINT || t1->kind == TYPE_ULONG));
    LLVMValueRef a = emit_expr(g, arg0, locals);
    LLVMValueRef b = emit_expr(g, arg1, locals);
    bool is64 = is_64bit_int_arg(g, arg0, a, locals) ||
                is_64bit_int_arg(g, arg1, b, locals);
    LLVMTypeRef it = is64 ? i64t : i32t;
    const char *iname = NULL;
    if (is_add) {
      if (is64) iname = is_unsigned ? "llvm.uadd.sat.i64" : "llvm.sadd.sat.i64";
      else      iname = is_unsigned ? "llvm.uadd.sat.i32" : "llvm.sadd.sat.i32";
    } else {
      if (is64) iname = is_unsigned ? "llvm.usub.sat.i64" : "llvm.ssub.sat.i64";
      else      iname = is_unsigned ? "llvm.usub.sat.i32" : "llvm.ssub.sat.i32";
    }
    a = coerce_int_to(g, a, it);
    b = coerce_int_to(g, b, it);
    LLVMTypeRef fty = LLVMFunctionType(it, (LLVMTypeRef[]){it, it}, 2, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, iname);
    if (!fn)
      fn = LLVMAddFunction(g->mod, iname, fty);
    LLVMValueRef args[] = {a, b};
    *out = zan_call2(g->builder, fty, fn, args, 2, is_add ? "sat.add" : "sat.sub");
    return true;
  }

  if (((method.len == 11 && memcmp(method.str, "OverflowAdd", 11) == 0) ||
       (method.len == 11 && memcmp(method.str, "OverflowSub", 11) == 0) ||
       (method.len == 11 && memcmp(method.str, "OverflowMul", 11) == 0)) &&
      expr->call.args.count == 3) {
    char op = method.str[8]; /* 'A', 'S', or 'M' */
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    zan_ast_node_t *arg1 = expr->call.args.items[1];
    zan_ast_node_t *arg2 = expr->call.args.items[2];
    zan_type_t *t0 = infer_expr_type(g, arg0, locals);
    zan_type_t *t1 = infer_expr_type(g, arg1, locals);
    bool is_unsigned = (t0 && (t0->kind == TYPE_UINT || t0->kind == TYPE_ULONG)) ||
                       (t1 && (t1->kind == TYPE_UINT || t1->kind == TYPE_ULONG));
    LLVMValueRef a = emit_expr(g, arg0, locals);
    LLVMValueRef b = emit_expr(g, arg1, locals);
    bool is64 = is_64bit_int_arg(g, arg0, a, locals) ||
                is_64bit_int_arg(g, arg1, b, locals);
    LLVMTypeRef it = is64 ? i64t : i32t;
    const char *iname = NULL;
    if (op == 'A') {
      iname = is64 ? (is_unsigned ? "llvm.uadd.with.overflow.i64" : "llvm.sadd.with.overflow.i64")
                   : (is_unsigned ? "llvm.uadd.with.overflow.i32" : "llvm.sadd.with.overflow.i32");
    } else if (op == 'S') {
      iname = is64 ? (is_unsigned ? "llvm.usub.with.overflow.i64" : "llvm.ssub.with.overflow.i64")
                   : (is_unsigned ? "llvm.usub.with.overflow.i32" : "llvm.ssub.with.overflow.i32");
    } else {
      iname = is64 ? (is_unsigned ? "llvm.umul.with.overflow.i64" : "llvm.smul.with.overflow.i64")
                   : (is_unsigned ? "llvm.umul.with.overflow.i32" : "llvm.smul.with.overflow.i32");
    }
    a = coerce_int_to(g, a, it);
    b = coerce_int_to(g, b, it);
    LLVMTypeRef ret_st = LLVMStructTypeInContext(g->ctx, (LLVMTypeRef[]){it, i1t}, 2, 0);
    LLVMTypeRef fty = LLVMFunctionType(ret_st, (LLVMTypeRef[]){it, it}, 2, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, iname);
    if (!fn)
      fn = LLVMAddFunction(g->mod, iname, fty);
    LLVMValueRef args[] = {a, b};
    LLVMValueRef tup = zan_call2(g->builder, fty, fn, args, 2, "ov.op");
    LLVMValueRef val = LLVMBuildExtractValue(g->builder, tup, 0, "ov.val");
    LLVMValueRef flag = LLVMBuildExtractValue(g->builder, tup, 1, "ov.flag");
    LLVMValueRef ov_ptr = (arg2 && arg2->kind == AST_REF_ARG && arg2->ref_arg.expr)
                              ? emit_ref_lvalue_ptr(g, arg2->ref_arg.expr, locals)
                              : NULL;
    if (ov_ptr) {
      LLVMTypeRef bt = map_type(g, g->binder->type_bool);
      if (bt && bt != i1t)
        flag = LLVMBuildZExt(g->builder, flag, bt, "ov.b");
      LLVMBuildStore(g->builder, flag, ov_ptr);
    }
    *out = val;
    return true;
  }

  if (method.len == 6 && memcmp(method.str, "BigMul", 6) == 0 &&
      expr->call.args.count == 3) {
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    zan_ast_node_t *arg1 = expr->call.args.items[1];
    zan_ast_node_t *arg2 = expr->call.args.items[2];
    zan_type_t *t0 = infer_expr_type(g, arg0, locals);
    zan_type_t *t1 = infer_expr_type(g, arg1, locals);
    bool is_unsigned = (t0 && t0->kind == TYPE_ULONG) ||
                       (t1 && t1->kind == TYPE_ULONG);
    LLVMValueRef a = coerce_int_to(g, emit_expr(g, arg0, locals), i64t);
    LLVMValueRef b = coerce_int_to(g, emit_expr(g, arg1, locals), i64t);
    LLVMTypeRef i128t = LLVMInt128TypeInContext(g->ctx);
    LLVMValueRef a128 = is_unsigned ? LLVMBuildZExt(g->builder, a, i128t, "bm.a")
                                    : LLVMBuildSExt(g->builder, a, i128t, "bm.a");
    LLVMValueRef b128 = is_unsigned ? LLVMBuildZExt(g->builder, b, i128t, "bm.b")
                                    : LLVMBuildSExt(g->builder, b, i128t, "bm.b");
    LLVMValueRef prod = LLVMBuildMul(g->builder, a128, b128, "bm.prod");
    LLVMValueRef lo = LLVMBuildTrunc(g->builder, prod, i64t, "bm.lo");
    LLVMValueRef hi128 = LLVMBuildLShr(g->builder, prod, LLVMConstInt(i128t, 64, 0), "bm.hi128");
    LLVMValueRef hi = LLVMBuildTrunc(g->builder, hi128, i64t, "bm.hi");
    LLVMValueRef lo_ptr = (arg2 && arg2->kind == AST_REF_ARG && arg2->ref_arg.expr)
                              ? emit_ref_lvalue_ptr(g, arg2->ref_arg.expr, locals)
                              : NULL;
    if (lo_ptr)
      LLVMBuildStore(g->builder, lo, lo_ptr);
    *out = hi;
    return true;
  }

  if (method.len == 9 && memcmp(method.str, "DivRem128", 9) == 0 &&
      expr->call.args.count == 4) {
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    zan_ast_node_t *arg1 = expr->call.args.items[1];
    zan_ast_node_t *arg2 = expr->call.args.items[2];
    zan_ast_node_t *arg3 = expr->call.args.items[3];
    zan_type_t *t0 = infer_expr_type(g, arg0, locals);
    zan_type_t *t1 = infer_expr_type(g, arg1, locals);
    zan_type_t *t2 = infer_expr_type(g, arg2, locals);
    bool is_unsigned = (t0 && t0->kind == TYPE_ULONG) ||
                       (t1 && t1->kind == TYPE_ULONG) ||
                       (t2 && t2->kind == TYPE_ULONG);
    LLVMValueRef hi = coerce_int_to(g, emit_expr(g, arg0, locals), i64t);
    LLVMValueRef lo = coerce_int_to(g, emit_expr(g, arg1, locals), i64t);
    LLVMValueRef dv = coerce_int_to(g, emit_expr(g, arg2, locals), i64t);
    LLVMValueRef rem_ptr = (arg3 && arg3->kind == AST_REF_ARG && arg3->ref_arg.expr)
                               ? emit_ref_lvalue_ptr(g, arg3->ref_arg.expr, locals)
                               : NULL;
#if defined(__x86_64__) || defined(_M_X64)
    bool is_x86_64 = (!g->target_triple[0]) || (strstr(g->target_triple, "x86_64") != NULL);
#else
    bool is_x86_64 = (g->target_triple[0] && strstr(g->target_triple, "x86_64") != NULL);
#endif
    LLVMValueRef quot = NULL, rem = NULL;
    if (is_x86_64) {
      LLVMTypeRef ret_st = LLVMStructTypeInContext(g->ctx, (LLVMTypeRef[]){i64t, i64t}, 2, 0);
      LLVMTypeRef fty = LLVMFunctionType(ret_st, (LLVMTypeRef[]){i64t, i64t, i64t}, 3, 0);
      const char *tmpl = is_unsigned ? "divq $4" : "idivq $4";
      const char *cons = "={ax},={dx},{ax},{dx},r,~{cc},~{dirflag},~{fpsr},~{flags}";
      LLVMValueRef asm_fn = LLVMGetInlineAsm(
          fty, (char *)tmpl, strlen(tmpl), (char *)cons, strlen(cons),
          1, 0, LLVMInlineAsmDialectATT, 0);
      LLVMValueRef pair = LLVMBuildCall2(
          g->builder, fty, asm_fn, (LLVMValueRef[]){lo, hi, dv}, 3, "div128");
      quot = LLVMBuildExtractValue(g->builder, pair, 0, "div128.q");
      rem = LLVMBuildExtractValue(g->builder, pair, 1, "div128.r");
    } else {
      LLVMTypeRef i128t = LLVMInt128TypeInContext(g->ctx);
      LLVMValueRef hi128 = LLVMBuildShl(
          g->builder,
          is_unsigned ? LLVMBuildZExt(g->builder, hi, i128t, "d.hiz")
                      : LLVMBuildSExt(g->builder, hi, i128t, "d.his"),
          LLVMConstInt(i128t, 64, 0), "d.hishl");
      LLVMValueRef num = LLVMBuildOr(g->builder, hi128,
                                     LLVMBuildZExt(g->builder, lo, i128t, "d.loz"), "d.num");
      LLVMValueRef den = is_unsigned ? LLVMBuildZExt(g->builder, dv, i128t, "d.dz")
                                     : LLVMBuildSExt(g->builder, dv, i128t, "d.ds");
      LLVMValueRef q128 = is_unsigned ? LLVMBuildUDiv(g->builder, num, den, "d.q128")
                                      : LLVMBuildSDiv(g->builder, num, den, "d.q128");
      LLVMValueRef r128 = is_unsigned ? LLVMBuildURem(g->builder, num, den, "d.r128")
                                      : LLVMBuildSRem(g->builder, num, den, "d.r128");
      quot = LLVMBuildTrunc(g->builder, q128, i64t, "d.q");
      rem = LLVMBuildTrunc(g->builder, r128, i64t, "d.r");
    }
    if (rem_ptr)
      LLVMBuildStore(g->builder, rem, rem_ptr);
    *out = quot;
    return true;
  }

  if (((method.len == 12 && memcmp(method.str, "AddWithCarry", 12) == 0) ||
       (method.len == 13 && memcmp(method.str, "SubWithBorrow", 13) == 0)) &&
      expr->call.args.count == 4) {
    bool is_add = (method.str[0] == 'A');
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    zan_ast_node_t *arg1 = expr->call.args.items[1];
    zan_ast_node_t *arg2 = expr->call.args.items[2];
    zan_ast_node_t *arg3 = expr->call.args.items[3];
    LLVMValueRef a = emit_expr(g, arg0, locals);
    LLVMValueRef b = emit_expr(g, arg1, locals);
    LLVMValueRef cin_raw = emit_expr(g, arg2, locals);
    bool is64 = is_64bit_int_arg(g, arg0, a, locals) ||
                is_64bit_int_arg(g, arg1, b, locals);
    LLVMTypeRef it = is64 ? i64t : i32t;
    const char *iname = is_add
                            ? (is64 ? "llvm.uadd.with.overflow.i64"
                                    : "llvm.uadd.with.overflow.i32")
                            : (is64 ? "llvm.usub.with.overflow.i64"
                                    : "llvm.usub.with.overflow.i32");
    a = coerce_int_to(g, a, it);
    b = coerce_int_to(g, b, it);
    LLVMValueRef cin1 = LLVMTypeOf(cin_raw) == i1t
                            ? cin_raw
                            : LLVMBuildICmp(g->builder, LLVMIntNE, cin_raw,
                                            LLVMConstInt(LLVMTypeOf(cin_raw), 0, 0),
                                            "cb.in1");
    LLVMValueRef cin = LLVMBuildZExt(g->builder, cin1, it, "cb.in");
    LLVMTypeRef ret_st = LLVMStructTypeInContext(g->ctx, (LLVMTypeRef[]){it, i1t}, 2, 0);
    LLVMTypeRef fty = LLVMFunctionType(ret_st, (LLVMTypeRef[]){it, it}, 2, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, iname);
    if (!fn)
      fn = LLVMAddFunction(g->mod, iname, fty);
    LLVMValueRef r1 = zan_call2(g->builder, fty, fn, (LLVMValueRef[]){a, b}, 2, "cb.1");
    LLVMValueRef s1 = LLVMBuildExtractValue(g->builder, r1, 0, "cb.s1");
    LLVMValueRef o1 = LLVMBuildExtractValue(g->builder, r1, 1, "cb.o1");
    LLVMValueRef r2 = zan_call2(g->builder, fty, fn, (LLVMValueRef[]){s1, cin}, 2, "cb.2");
    LLVMValueRef s2 = LLVMBuildExtractValue(g->builder, r2, 0, "cb.s2");
    LLVMValueRef o2 = LLVMBuildExtractValue(g->builder, r2, 1, "cb.o2");
    LLVMValueRef cout = LLVMBuildOr(g->builder, o1, o2, "cb.out");
    LLVMValueRef cout_ptr = (arg3 && arg3->kind == AST_REF_ARG && arg3->ref_arg.expr)
                                ? emit_ref_lvalue_ptr(g, arg3->ref_arg.expr, locals)
                                : NULL;
    if (cout_ptr) {
      LLVMTypeRef bt = map_type(g, g->binder->type_bool);
      if (bt && bt != i1t)
        cout = LLVMBuildZExt(g->builder, cout, bt, "cb.outb");
      LLVMBuildStore(g->builder, cout, cout_ptr);
    }
    *out = s2;
    return true;
  }

  if (method.len == 4 && memcmp(method.str, "Log2", 4) == 0 &&
      expr->call.args.count == 1) {
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    LLVMValueRef v = emit_expr(g, arg0, locals);
    bool is64 = is_64bit_int_arg(g, arg0, v, locals);
    if (is64) {
      v = coerce_int_to(g, v, i64t);
      LLVMValueRef vor = zan_or(g->builder, v, LLVMConstInt(i64t, 1, 0), "vor");
      LLVMTypeRef fty =
          LLVMFunctionType(i64t, (LLVMTypeRef[]){i64t, i1t}, 2, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i64");
      if (!fn)
        fn = LLVMAddFunction(g->mod, "llvm.ctlz.i64", fty);
      LLVMValueRef args[] = {vor, LLVMConstInt(i1t, 0, 0)};
      LLVMValueRef clz = zan_call2(g->builder, fty, fn, args, 2, "ctlz");
      LLVMValueRef sub =
          zan_sub(g->builder, LLVMConstInt(i64t, 63, 0), clz, "log2");
      *out = LLVMBuildTrunc(g->builder, sub, i32t, "log2_32");
    } else {
      v = coerce_int_to(g, v, i32t);
      LLVMValueRef vor = zan_or(g->builder, v, LLVMConstInt(i32t, 1, 0), "vor");
      LLVMTypeRef fty =
          LLVMFunctionType(i32t, (LLVMTypeRef[]){i32t, i1t}, 2, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i32");
      if (!fn)
        fn = LLVMAddFunction(g->mod, "llvm.ctlz.i32", fty);
      LLVMValueRef args[] = {vor, LLVMConstInt(i1t, 0, 0)};
      LLVMValueRef clz = zan_call2(g->builder, fty, fn, args, 2, "ctlz");
      *out = zan_sub(g->builder, LLVMConstInt(i32t, 31, 0), clz, "log2");
    }
    return true;
  }

  if (method.len == 6 && memcmp(method.str, "IsPow2", 6) == 0 &&
      expr->call.args.count == 1) {
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    LLVMValueRef v = emit_expr(g, arg0, locals);
    bool is64 = is_64bit_int_arg(g, arg0, v, locals);
    LLVMValueRef zero =
        is64 ? LLVMConstInt(i64t, 0, 0) : LLVMConstInt(i32t, 0, 0);
    LLVMValueRef one =
        is64 ? LLVMConstInt(i64t, 1, 0) : LLVMConstInt(i32t, 1, 0);
    LLVMValueRef gt0 = zan_icmp(g->builder, LLVMIntSGT, v, zero, "gt0");
    LLVMValueRef sub1 = zan_sub(g->builder, v, one, "sub1");
    LLVMValueRef andv = zan_and(g->builder, v, sub1, "andv");
    LLVMValueRef eq0 = zan_icmp(g->builder, LLVMIntEQ, andv, zero, "eq0");
    *out = zan_and(g->builder, gt0, eq0, "ispow2");
    return true;
  }

  if (method.len == 17 && memcmp(method.str, "RoundUpToPowerOf2", 17) == 0 &&
      expr->call.args.count == 1) {
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    LLVMValueRef v = emit_expr(g, arg0, locals);
    bool is64 = is_64bit_int_arg(g, arg0, v, locals);
    if (is64) {
      v = coerce_int_to(g, v, i64t);
      LLVMValueRef vm1 =
          zan_sub(g->builder, v, LLVMConstInt(i64t, 1, 0), "vm1");
      LLVMTypeRef fty =
          LLVMFunctionType(i64t, (LLVMTypeRef[]){i64t, i1t}, 2, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i64");
      if (!fn)
        fn = LLVMAddFunction(g->mod, "llvm.ctlz.i64", fty);
      LLVMValueRef args[] = {vm1, LLVMConstInt(i1t, 0, 0)};
      LLVMValueRef clz = zan_call2(g->builder, fty, fn, args, 2, "ctlz");
      LLVMValueRef sh =
          zan_sub(g->builder, LLVMConstInt(i64t, 64, 0), clz, "sh");
      LLVMValueRef pow2 =
          zan_shl(g->builder, LLVMConstInt(i64t, 1, 0), sh, "pow2");
      LLVMValueRef le1 =
          zan_icmp(g->builder, LLVMIntSLE, v, LLVMConstInt(i64t, 1, 0), "le1");
      *out = LLVMBuildSelect(g->builder, le1, LLVMConstInt(i64t, 1, 0), pow2,
                             "roundup");
    } else {
      v = coerce_int_to(g, v, i32t);
      LLVMValueRef vm1 =
          zan_sub(g->builder, v, LLVMConstInt(i32t, 1, 0), "vm1");
      LLVMTypeRef fty =
          LLVMFunctionType(i32t, (LLVMTypeRef[]){i32t, i1t}, 2, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i32");
      if (!fn)
        fn = LLVMAddFunction(g->mod, "llvm.ctlz.i32", fty);
      LLVMValueRef args[] = {vm1, LLVMConstInt(i1t, 0, 0)};
      LLVMValueRef clz = zan_call2(g->builder, fty, fn, args, 2, "ctlz");
      LLVMValueRef sh =
          zan_sub(g->builder, LLVMConstInt(i32t, 32, 0), clz, "sh");
      LLVMValueRef pow2 =
          zan_shl(g->builder, LLVMConstInt(i32t, 1, 0), sh, "pow2");
      LLVMValueRef le1 =
          zan_icmp(g->builder, LLVMIntSLE, v, LLVMConstInt(i32t, 1, 0), "le1");
      *out = LLVMBuildSelect(g->builder, le1, LLVMConstInt(i32t, 1, 0), pow2,
                             "roundup");
    }
    return true;
  }

  if (method.len == 17 && memcmp(method.str, "ResetLowestSetBit", 17) == 0 &&
      expr->call.args.count == 1) {
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    LLVMValueRef v = emit_expr(g, arg0, locals);
    bool is64 = is_64bit_int_arg(g, arg0, v, locals);
    LLVMValueRef one =
        is64 ? LLVMConstInt(i64t, 1, 0) : LLVMConstInt(i32t, 1, 0);
    LLVMValueRef sub1 = zan_sub(g->builder, v, one, "blsr_sub");
    *out = zan_and(g->builder, v, sub1, "blsr");
    return true;
  }

  if (method.len == 19 && memcmp(method.str, "ExtractLowestSetBit", 19) == 0 &&
      expr->call.args.count == 1) {
    zan_ast_node_t *arg0 = expr->call.args.items[0];
    LLVMValueRef v = emit_expr(g, arg0, locals);
    bool is64 = is_64bit_int_arg(g, arg0, v, locals);
    LLVMValueRef zero =
        is64 ? LLVMConstInt(i64t, 0, 0) : LLVMConstInt(i32t, 0, 0);
    LLVMValueRef neg = zan_sub(g->builder, zero, v, "blsi_neg");
    *out = zan_and(g->builder, v, neg, "blsi");
    return true;
  }

  return false;
}
