/* NativeMemory 底层离堆内存操作原语与 CRC32 等系统调用封装 */
{
  LLVMValueRef nm_out = NULL;
  if (emit_native_memory_call(g, expr, locals, &nm_out))
    return nm_out;
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

/* String.CompareOrdinal / string.CompareOrdinal：
 * 指针相等 → null 守卫 → 首字节无符号差值快路径 → strcmp(a+1, b+1)。 */
if ((is_call_to(expr, "String", "CompareOrdinal") ||
     is_call_to(expr, "string", "CompareOrdinal")) &&
    expr->call.args.count == 2) {
  LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8t, 0);
  LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
  zan_ast_node_t *a_ast = expr->call.args.items[0];
  zan_ast_node_t *b_ast = expr->call.args.items[1];
  LLVMValueRef a = emit_expr(g, a_ast, locals);
  LLVMValueRef b = emit_expr(g, b_ast, locals);
  LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
  LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef null_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "oc.null");
  LLVMBasicBlockRef bnull_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "oc.bnull");
  LLVMBasicBlockRef b0_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "oc.b0");
  LLVMBasicBlockRef slow_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "oc.slow");
  LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "oc.done");
  LLVMValueRef zero32 = LLVMConstInt(i32t, 0, 0);
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntEQ, a, b, "oc.peq"),
                  done_bb, null_bb);
  LLVMPositionBuilderAtEnd(g->builder, null_bb);
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntEQ, a, LLVMConstNull(i8ptr), "oc.an"),
                  done_bb, bnull_bb);
  LLVMPositionBuilderAtEnd(g->builder, bnull_bb);
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntEQ, b, LLVMConstNull(i8ptr), "oc.bn"),
                  done_bb, b0_bb);
  LLVMPositionBuilderAtEnd(g->builder, b0_bb);
  LLVMValueRef c0 = LLVMBuildZExt(g->builder, LLVMBuildLoad2(g->builder, i8t, a, "oc.a0"),
                                  i32t, "oc.c0");
  LLVMValueRef c1 = LLVMBuildZExt(g->builder, LLVMBuildLoad2(g->builder, i8t, b, "oc.b0"),
                                  i32t, "oc.c1");
  LLVMValueRef diff = LLVMBuildSub(g->builder, c0, c1, "oc.diff");
  LLVMValueRef quick = LLVMBuildOr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntNE, diff, zero32, "oc.dne"),
      LLVMBuildICmp(g->builder, LLVMIntEQ, c0, zero32, "oc.z0"), "oc.q");
  LLVMBuildCondBr(g->builder, quick, done_bb, slow_bb);
  LLVMPositionBuilderAtEnd(g->builder, slow_bb);
  LLVMValueRef one64 = LLVMConstInt(i64t, 1, 0);
  LLVMValueRef a1 = LLVMBuildInBoundsGEP2(g->builder, i8t, a, &one64, 1, "oc.a1");
  LLVMValueRef b1 = LLVMBuildInBoundsGEP2(g->builder, i8t, b, &one64, 1, "oc.b1");
  LLVMTypeRef strcmp_ty =
      LLVMFunctionType(i32t, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0);
  LLVMValueRef strcmp_fn = get_libc_fn(g, "strcmp", strcmp_ty);
  LLVMValueRef r_slow = zan_call2(g->builder, strcmp_ty, strcmp_fn,
                                  (LLVMValueRef[]){a1, b1}, 2, "ordcmp");
  LLVMBuildBr(g->builder, done_bb);
  LLVMPositionBuilderAtEnd(g->builder, done_bb);
  LLVMValueRef res32 = LLVMBuildPhi(g->builder, i32t, "oc.res");
  LLVMAddIncoming(
      res32,
      (LLVMValueRef[]){zero32, LLVMConstInt(i32t, (unsigned long long)-1, 1),
                       LLVMConstInt(i32t, 1, 0), diff, r_slow},
      (LLVMBasicBlockRef[]){entry_bb, null_bb, bnull_bb, b0_bb, slow_bb}, 5);
  emit_release_owned_call_temp(g, a_ast, a, locals);
  emit_release_owned_call_temp(g, b_ast, b, locals);
  return LLVMBuildSExt(g->builder, res32, i64t, "ordcmp64");
}

/* Array.Sort(T[])：原生零越界/零 ARC 原始缓冲内省排序（int/long/double/string 数组） */
if (is_call_to(expr, "Array", "Sort") && expr->call.args.count == 1) {
  zan_ast_node_t *a_ast = expr->call.args.items[0];
  zan_type_t *at = infer_expr_type(g, a_ast, locals);
  if (at && at->kind == TYPE_ARRAY && at->array_rank <= 1 && at->element_type) {
    int sort_kind = -1;
    if (at->element_type->kind == TYPE_INT) sort_kind = ZAN_SORT_I32;
    else if (at->element_type->kind == TYPE_LONG) sort_kind = ZAN_SORT_I64;
    else if (at->element_type->kind == TYPE_DOUBLE) sort_kind = ZAN_SORT_F64;
    else if (at->element_type->kind == TYPE_STRING) sort_kind = ZAN_SORT_STR;
    if (sort_kind >= 0) {
      LLVMValueRef arr = emit_expr(g, a_ast, locals);
      LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
      LLVMBasicBlockRef go_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "asort.go");
      LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "asort.end");
      LLVMValueRef is_null = LLVMBuildICmp(g->builder, LLVMIntEQ,
          arr, LLVMConstNull(LLVMTypeOf(arr)), "asort.null");
      LLVMBuildCondBr(g->builder, is_null, end_bb, go_bb);
      LLVMPositionBuilderAtEnd(g->builder, go_bb);
      LLVMValueRef len = zan_array_len(g, arr);
      emit_raw_buffer_sort(g, sort_kind, arr, len, NULL);
      LLVMBuildBr(g->builder, end_bb);
      LLVMPositionBuilderAtEnd(g->builder, end_bb);
      emit_release_owned_call_temp(g, a_ast, arr, locals);
      return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    }
  }
}

/* Math.Sqrt(expr) → llvm.sqrt.f64（单条 sqrtsd / 自动向量化，不碰 errno） */
if (!zan_type_defines(g, "Math", "Sqrt") && is_call_to(expr, "Math", "Sqrt") &&
    expr->call.args.count == 1) {
  LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  if (!math_require_numeric(g, expr, expr->call.args.items[0], arg, locals,
                            "Sqrt"))
    return LLVMConstReal(dbl, 0.0);
  arg = math_arg_to_double(g, arg);
  LLVMTypeRef sqrt_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){dbl}, 1, 0);
  LLVMValueRef sqrt_fn = LLVMGetNamedFunction(g->mod, "llvm.sqrt.f64");
  if (!sqrt_fn) {
    sqrt_fn = LLVMAddFunction(g->mod, "llvm.sqrt.f64", sqrt_type);
  }
  return zan_call2(g->builder, sqrt_type, sqrt_fn, &arg, 1, "sqrt");
}

/* Math 一元超越函数：补齐 nounwind + willreturn + memory(none)，解锁 LICM/GVN/向量化 */
{
  static const char *math1[] = {"Sin",  "sin",  "Cos",  "cos",  "Tan",  "tan",
                                "Log",  "log",  "Exp",  "exp",  "Atan", "atan",
                                "Asin", "asin", "Acos", "acos", NULL};
  for (int mi = 0; math1[mi]; mi += 2) {
    if (!zan_type_defines(g, "Math", math1[mi]) &&
        is_call_to(expr, "Math", math1[mi]) && expr->call.args.count == 1) {
      LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
      LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
      if (!math_require_numeric(g, expr, expr->call.args.items[0], arg, locals,
                                math1[mi]))
        return LLVMConstReal(dbl, 0.0);
      arg = math_arg_to_double(g, arg);
      LLVMTypeRef fty = LLVMFunctionType(dbl, (LLVMTypeRef[]){dbl}, 1, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, math1[mi + 1]);
      if (!fn) {
        fn = LLVMAddFunction(g->mod, math1[mi + 1], fty);
        unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
        unsigned wr = LLVMGetEnumAttributeKindForName("willreturn", 10);
        unsigned mm = LLVMGetEnumAttributeKindForName("memory", 6);
        if (nw) LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(g->ctx, nw, 0));
        if (wr) LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(g->ctx, wr, 0));
        if (mm) LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(g->ctx, mm, 0));
      }
      return zan_call2(g->builder, fty, fn, &arg, 1, math1[mi + 1]);
    }
  }
}

if (!zan_type_defines(g, "Math", "Atan2") &&
    is_call_to(expr, "Math", "Atan2") && expr->call.args.count == 2) {
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMValueRef y_v = emit_expr(g, expr->call.args.items[0], locals);
  LLVMValueRef x_v = emit_expr(g, expr->call.args.items[1], locals);
  if (!math_require_numeric(g, expr, expr->call.args.items[0], y_v, locals,
                            "Atan2") ||
      !math_require_numeric(g, expr, expr->call.args.items[1], x_v, locals,
                            "Atan2"))
    return LLVMConstReal(dbl, 0.0);
  y_v = math_arg_to_double(g, y_v);
  x_v = math_arg_to_double(g, x_v);
  LLVMTypeRef atan2_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){dbl, dbl}, 2, 0);
  LLVMValueRef atan2_fn = LLVMGetNamedFunction(g->mod, "atan2");
  if (!atan2_fn) {
    atan2_fn = LLVMAddFunction(g->mod, "atan2", atan2_type);
    unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
    unsigned wr = LLVMGetEnumAttributeKindForName("willreturn", 10);
    unsigned mm = LLVMGetEnumAttributeKindForName("memory", 6);
    if (nw) LLVMAddAttributeAtIndex(atan2_fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(g->ctx, nw, 0));
    if (wr) LLVMAddAttributeAtIndex(atan2_fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(g->ctx, wr, 0));
    if (mm) LLVMAddAttributeAtIndex(atan2_fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(g->ctx, mm, 0));
  }
  LLVMValueRef args[] = {y_v, x_v};
  return zan_call2(g->builder, atan2_type, atan2_fn, args, 2, "atan2");
}

/* Math.Abs(expr) → 浮点直降 llvm.fabs.f64（单条 andpd） */
if (!zan_type_defines(g, "Math", "Abs") && is_call_to(expr, "Math", "Abs") &&
    expr->call.args.count == 1) {
  LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
  LLVMTypeRef arg_type = LLVMTypeOf(arg);
  if (!math_require_numeric(g, expr, expr->call.args.items[0], arg, locals,
                            "Abs"))
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
  if (LLVMGetTypeKind(arg_type) == LLVMDoubleTypeKind ||
      LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind) {
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    if (LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind)
      arg = LLVMBuildFPExt(g->builder, arg, dbl, "ext");
    LLVMTypeRef fabs_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){dbl}, 1, 0);
    LLVMValueRef fabs_fn = LLVMGetNamedFunction(g->mod, "llvm.fabs.f64");
    if (!fabs_fn) {
      fabs_fn = LLVMAddFunction(g->mod, "llvm.fabs.f64", fabs_type);
    }
    return zan_call2(g->builder, fabs_type, fabs_fn, &arg, 1, "fabs");
  } else {
    unsigned abs_bits = LLVMGetIntTypeWidth(arg_type);
    LLVMValueRef shift = zan_ashr(
        g->builder, arg, LLVMConstInt(arg_type, abs_bits - 1, 0), "sh");
    LLVMValueRef xor = zan_xor(g->builder, arg, shift, "xor");
    return zan_sub(g->builder, xor, shift, "abs");
  }
}

/* Math */
if (!zan_type_defines(g, "Math", "Max") &&
    !zan_type_defines(g, "Math", "Min") &&
    (is_call_to(expr, "Math", "Max") || is_call_to(expr, "Math", "Min")) &&
    expr->call.args.count == 2) {
  int want_max = is_call_to(expr, "Math", "Max");
  LLVMValueRef a = emit_expr(g, expr->call.args.items[0], locals);
  LLVMValueRef b = emit_expr(g, expr->call.args.items[1], locals);
  zan_type_t *at = infer_expr_type(g, expr->call.args.items[0], locals);
  zan_type_t *bt = infer_expr_type(g, expr->call.args.items[1], locals);
  const char *result =
      zan_builtin_minmax_result("Math", want_max ? "Max" : "Min", 3, at, bt);
  /* 守卫要逐个参数判：原来的写法是 "ka != Int && kb != Int"，
     遇到「一个指针 + 一个整数」就不报错了，pointer 随后被
     sitofp 成 double——生成非法 IR，编译死在代码生成阶段。 */
  if (!result || !math_arg_is_numeric(a) || !math_arg_is_numeric(b)) {
    int numeric_pair = zan_builtin_minmax_result("Math", "Min", 3, at, at) &&
                       zan_builtin_minmax_result("Math", "Min", 3, bt, bt);
    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3055,
                       numeric_pair ? "Math.%s has no matching numeric "
                                      "overload; convert arguments explicitly"
                                    : "Math.%s requires numeric arguments",
                       want_max ? "Max" : "Min");
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
  }
  if (strcmp(result, "double") == 0) {
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    a = at->kind == TYPE_BYTE || at->kind == TYPE_USHORT ||
                at->kind == TYPE_CHAR || at->kind == TYPE_UINT ||
                at->kind == TYPE_ULONG
            ? LLVMBuildUIToFP(g->builder, a, dbl, "tofp")
            : math_arg_to_double(g, a);
    b = bt->kind == TYPE_BYTE || bt->kind == TYPE_USHORT ||
                bt->kind == TYPE_CHAR || bt->kind == TYPE_UINT ||
                bt->kind == TYPE_ULONG
            ? LLVMBuildUIToFP(g->builder, b, dbl, "tofp")
            : math_arg_to_double(g, b);
    LLVMValueRef a_nan = LLVMBuildFCmp(g->builder, LLVMRealUNO, a, a, "an");
    LLVMValueRef b_nan = LLVMBuildFCmp(g->builder, LLVMRealUNO, b, b, "bn");
    LLVMValueRef cmp = LLVMBuildFCmp(
        g->builder, want_max ? LLVMRealOGT : LLVMRealOLT, a, b, "cmp");
    LLVMValueRef picked =
        LLVMBuildSelect(g->builder, cmp, a, b, want_max ? "max" : "min");
    LLVMValueRef from_b =
        LLVMBuildSelect(g->builder, b_nan, b, picked, want_max ? "max" : "min");
    return LLVMBuildSelect(g->builder, a_nan, a, from_b,
                           want_max ? "max" : "min");
  }
  /* Integer literals are emitted as i64 even when semantically int.
   * Normalize both operands to the selected overload before comparing. */
  int is_unsigned = strcmp(result, "uint") == 0 || strcmp(result, "ulong") == 0;
  LLVMTypeRef it = strcmp(result, "nint") == 0
                       ? map_type(g, g->binder->type_nint)
                   : strcmp(result, "long") == 0 || strcmp(result, "ulong") == 0
                       ? LLVMInt64TypeInContext(g->ctx)
                       : LLVMInt32TypeInContext(g->ctx);
  if (LLVMTypeOf(a) != it)
    a = LLVMBuildIntCast2(g->builder, a, it,
                          at->kind != TYPE_UINT && at->kind != TYPE_USHORT &&
                              at->kind != TYPE_BYTE && at->kind != TYPE_CHAR,
                          "minmax.a");
  if (LLVMTypeOf(b) != it)
    b = LLVMBuildIntCast2(g->builder, b, it,
                          bt->kind != TYPE_UINT && bt->kind != TYPE_USHORT &&
                              bt->kind != TYPE_BYTE && bt->kind != TYPE_CHAR,
                          "minmax.b");
  LLVMValueRef cmp =
      zan_icmp(g->builder,
               is_unsigned ? (want_max ? LLVMIntUGT : LLVMIntULT)
                           : (want_max ? LLVMIntSGT : LLVMIntSLT),
               a, b, "cmp");
  return LLVMBuildSelect(g->builder, cmp, a, b, want_max ? "max" : "min");
}

if (!zan_type_defines(g, "Math", "Pow") && is_call_to(expr, "Math", "Pow") &&
    expr->call.args.count == 2) {
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMValueRef base_v = emit_expr(g, expr->call.args.items[0], locals);
  LLVMValueRef exp_v = emit_expr(g, expr->call.args.items[1], locals);
  if (!math_require_numeric(g, expr, expr->call.args.items[0], base_v, locals,
                            "Pow") ||
      !math_require_numeric(g, expr, expr->call.args.items[1], exp_v, locals,
                            "Pow"))
    return LLVMConstReal(dbl, 0.0);
  base_v = math_arg_to_double(g, base_v);
  exp_v = math_arg_to_double(g, exp_v);
  LLVMTypeRef pow_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){dbl, dbl}, 2, 0);
  LLVMValueRef pow_fn = LLVMGetNamedFunction(g->mod, "pow");
  if (!pow_fn) {
    pow_fn = LLVMAddFunction(g->mod, "pow", pow_type);
    unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
    unsigned wr = LLVMGetEnumAttributeKindForName("willreturn", 10);
    unsigned mm = LLVMGetEnumAttributeKindForName("memory", 6);
    if (nw) LLVMAddAttributeAtIndex(pow_fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(g->ctx, nw, 0));
    if (wr) LLVMAddAttributeAtIndex(pow_fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(g->ctx, wr, 0));
    if (mm) LLVMAddAttributeAtIndex(pow_fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(g->ctx, mm, 0));
  }
  LLVMValueRef args[] = {base_v, exp_v};
  return zan_call2(g->builder, pow_type, pow_fn, args, 2, "pow");
}

if (!zan_type_defines(g, "Math", "Floor") &&
    is_call_to(expr, "Math", "Floor") && expr->call.args.count == 1) {
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
  if (!math_require_numeric(g, expr, expr->call.args.items[0], arg, locals,
                            "Floor"))
    return LLVMConstReal(dbl, 0.0);
  arg = math_arg_to_double(g, arg);
  LLVMTypeRef floor_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){dbl}, 1, 0);
  LLVMValueRef floor_fn = LLVMGetNamedFunction(g->mod, "llvm.floor.f64");
  if (!floor_fn) {
    floor_fn = LLVMAddFunction(g->mod, "llvm.floor.f64", floor_type);
  }
  return zan_call2(g->builder, floor_type, floor_fn, &arg, 1, "floor");
}

if (!zan_type_defines(g, "Math", "Ceiling") &&
    is_call_to(expr, "Math", "Ceiling") && expr->call.args.count == 1) {
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
  if (!math_require_numeric(g, expr, expr->call.args.items[0], arg, locals,
                            "Ceiling"))
    return LLVMConstReal(dbl, 0.0);
  arg = math_arg_to_double(g, arg);
  LLVMTypeRef ceil_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){dbl}, 1, 0);
  LLVMValueRef ceil_fn = LLVMGetNamedFunction(g->mod, "llvm.ceil.f64");
  if (!ceil_fn) {
    ceil_fn = LLVMAddFunction(g->mod, "llvm.ceil.f64", ceil_type);
  }
  return zan_call2(g->builder, ceil_type, ceil_fn, &arg, 1, "ceil");
}

/* Math.Round → llvm.rint.f64（单条 roundsd，保银行家舍入语义；常量 digits 编译期折叠 10^d） */
if (!zan_type_defines(g, "Math", "Round") &&
    is_call_to(expr, "Math", "Round") &&
    (expr->call.args.count == 1 || expr->call.args.count == 2)) {
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMTypeRef d1_ty = LLVMFunctionType(dbl, (LLVMTypeRef[]){dbl}, 1, 0);
  LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
  if (!math_require_numeric(g, expr, expr->call.args.items[0], arg, locals,
                            "Round"))
    return LLVMConstReal(dbl, 0.0);
  arg = math_arg_to_double(g, arg);
  LLVMValueRef round_fn = LLVMGetNamedFunction(g->mod, "llvm.rint.f64");
  if (!round_fn)
    round_fn = LLVMAddFunction(g->mod, "llvm.rint.f64", d1_ty);
  if (expr->call.args.count == 1)
    return zan_call2(g->builder, d1_ty, round_fn, &arg, 1, "rint");
  /* digits: rint(x * 10^d) / 10^d（同样银行家） */
  zan_ast_node_t *dig_node = expr->call.args.items[1];
  LLVMValueRef scale = NULL;
  if (dig_node && dig_node->kind == AST_INT_LITERAL &&
      dig_node->int_val >= 0 && dig_node->int_val <= 15) {
    static const double kPow10[16] = {
        1.0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7,
        1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15};
    scale = LLVMConstReal(dbl, kPow10[dig_node->int_val]);
  } else {
    LLVMValueRef digits = emit_expr(g, dig_node, locals);
    if (!math_require_numeric(g, expr, dig_node, digits, locals, "Round"))
      return LLVMConstReal(dbl, 0.0);
    digits = math_arg_to_double(g, digits);
    LLVMTypeRef d2_ty = LLVMFunctionType(dbl, (LLVMTypeRef[]){dbl, dbl}, 2, 0);
    LLVMValueRef pow_fn = LLVMGetNamedFunction(g->mod, "pow");
    if (!pow_fn) {
      pow_fn = LLVMAddFunction(g->mod, "pow", d2_ty);
      unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
      unsigned wr = LLVMGetEnumAttributeKindForName("willreturn", 10);
      unsigned mm = LLVMGetEnumAttributeKindForName("memory", 6);
      if (nw) LLVMAddAttributeAtIndex(pow_fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(g->ctx, nw, 0));
      if (wr) LLVMAddAttributeAtIndex(pow_fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(g->ctx, wr, 0));
      if (mm) LLVMAddAttributeAtIndex(pow_fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(g->ctx, mm, 0));
    }
    scale = zan_call2(g->builder, d2_ty, pow_fn,
                      (LLVMValueRef[]){LLVMConstReal(dbl, 10.0), digits}, 2, "scale");
  }
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
  int char_cls = (cn.len == 4 && (memcmp(cn.str, "char", 4) == 0 ||
                                  memcmp(cn.str, "Char", 4) == 0));
  if (char_cls) {
    zan_istr_t cm = expr->call.callee->member.name;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
    LLVMValueRef cp = arg;
    if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(LLVMTypeOf(arg)) > 32)
      cp = LLVMBuildTrunc(g->builder, arg, i32t, "ch.cp32");
    else if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMIntegerTypeKind &&
             LLVMGetIntTypeWidth(LLVMTypeOf(arg)) < 32)
      cp = LLVMBuildZExt(g->builder, arg, i32t, "ch.cp32");

    /* 无分支内联 ASCII 判定（等价于 C locale libc isdigit/isalpha/...，
     * 消除每次外部 CRT 调用与 locale 检查，可供 LLVM 自动向量化） */
    LLVMValueRef pred_res = NULL;
    if (cm.len == 7 && memcmp(cm.str, "IsDigit", 7) == 0) {
      pred_res = zan_icmp(g->builder, LLVMIntULE,
                          zan_sub(g->builder, cp, LLVMConstInt(i32t, '0', 0), "ch.d"),
                          LLVMConstInt(i32t, 9, 0), "ch.isdig");
    } else if (cm.len == 7 && memcmp(cm.str, "IsUpper", 7) == 0) {
      pred_res = zan_icmp(g->builder, LLVMIntULE,
                          zan_sub(g->builder, cp, LLVMConstInt(i32t, 'A', 0), "ch.u"),
                          LLVMConstInt(i32t, 25, 0), "ch.isup");
    } else if (cm.len == 7 && memcmp(cm.str, "IsLower", 7) == 0) {
      pred_res = zan_icmp(g->builder, LLVMIntULE,
                          zan_sub(g->builder, cp, LLVMConstInt(i32t, 'a', 0), "ch.l"),
                          LLVMConstInt(i32t, 25, 0), "ch.islo");
    } else if (cm.len == 8 && memcmp(cm.str, "IsLetter", 8) == 0) {
      LLVMValueRef low = zan_or(g->builder, cp, LLVMConstInt(i32t, 32, 0), "ch.fold");
      pred_res = zan_icmp(g->builder, LLVMIntULE,
                          zan_sub(g->builder, low, LLVMConstInt(i32t, 'a', 0), "ch.a"),
                          LLVMConstInt(i32t, 25, 0), "ch.islet");
    } else if (cm.len == 15 && memcmp(cm.str, "IsLetterOrDigit", 15) == 0) {
      LLVMValueRef low = zan_or(g->builder, cp, LLVMConstInt(i32t, 32, 0), "ch.fold");
      LLVMValueRef is_let = zan_icmp(g->builder, LLVMIntULE,
                                     zan_sub(g->builder, low, LLVMConstInt(i32t, 'a', 0), "ch.a"),
                                     LLVMConstInt(i32t, 25, 0), "ch.islet");
      LLVMValueRef is_dig = zan_icmp(g->builder, LLVMIntULE,
                                     zan_sub(g->builder, cp, LLVMConstInt(i32t, '0', 0), "ch.d"),
                                     LLVMConstInt(i32t, 9, 0), "ch.isdig");
      pred_res = zan_or(g->builder, is_let, is_dig, "ch.isalnum");
    } else if (cm.len == 12 && memcmp(cm.str, "IsWhiteSpace", 12) == 0) {
      LLVMValueRef is_sp = zan_icmp(g->builder, LLVMIntEQ, cp,
                                    LLVMConstInt(i32t, 32, 0), "ch.sp");
      LLVMValueRef is_ctl = zan_icmp(g->builder, LLVMIntULE,
                                     zan_sub(g->builder, cp, LLVMConstInt(i32t, 9, 0), "ch.ws"),
                                     LLVMConstInt(i32t, 4, 0), "ch.ctl");
      pred_res = zan_or(g->builder, is_sp, is_ctl, "ch.isws");
    }
    if (pred_res) {
      emit_release_owned_call_temp(g, expr->call.args.items[0], arg, locals);
      return pred_res;
    }

    int is_to_up = (cm.len == 7 && memcmp(cm.str, "ToUpper", 7) == 0);
    int is_to_lo = (cm.len == 7 && memcmp(cm.str, "ToLower", 7) == 0);
    if (is_to_up || is_to_lo) {
      /* ASCII (<128) 3 指令内联快路径；非 ASCII 回落全 Unicode 二分表 */
      LLVMValueRef fn_cur = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
      LLVMBasicBlockRef bb_asc = LLVMAppendBasicBlockInContext(g->ctx, fn_cur, "ch.asc");
      LLVMBasicBlockRef bb_uni = LLVMAppendBasicBlockInContext(g->ctx, fn_cur, "ch.uni");
      LLVMBasicBlockRef bb_m = LLVMAppendBasicBlockInContext(g->ctx, fn_cur, "ch.m");
      LLVMValueRef is_asc = zan_icmp(g->builder, LLVMIntULT, cp,
                                     LLVMConstInt(i32t, 128, 0), "ch.isasc");
      zan_set_branch_weights(g, LLVMBuildCondBr(g->builder, is_asc, bb_asc, bb_uni), 2000, 1);

      LLVMPositionBuilderAtEnd(g->builder, bb_asc);
      LLVMValueRef base_c = LLVMConstInt(i32t, is_to_up ? 'a' : 'A', 0);
      LLVMValueRef in_rng = zan_icmp(g->builder, LLVMIntULE,
                                     zan_sub(g->builder, cp, base_c, "ch.sub"),
                                     LLVMConstInt(i32t, 25, 0), "ch.inrng");
      LLVMValueRef flipped = LLVMBuildXor(g->builder, cp,
                                          LLVMConstInt(i32t, 32, 0), "ch.flip");
      LLVMValueRef r_asc = LLVMBuildSelect(g->builder, in_rng, flipped, cp, "ch.rasc");
      LLVMBuildBr(g->builder, bb_m);

      LLVMPositionBuilderAtEnd(g->builder, bb_uni);
      LLVMTypeRef case_ty =
          LLVMFunctionType(i32t, (LLVMTypeRef[]){i32t, i32t}, 2, 0);
      LLVMValueRef upc = LLVMConstInt(i32t, is_to_up ? 1 : 0, 0);
      LLVMValueRef args2[2] = {cp, upc};
      LLVMValueRef r_uni = zan_call2(g->builder, case_ty, get_char_case_fn(g),
                                     args2, 2, "ch.map");
      LLVMBasicBlockRef uni_end = LLVMGetInsertBlock(g->builder);
      LLVMBuildBr(g->builder, bb_m);

      LLVMPositionBuilderAtEnd(g->builder, bb_m);
      LLVMValueRef r = LLVMBuildPhi(g->builder, i32t, "ch.res");
      LLVMAddIncoming(r, (LLVMValueRef[]){r_asc, r_uni},
                      (LLVMBasicBlockRef[]){bb_asc, uni_end}, 2);
      if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMIntegerTypeKind &&
          LLVMGetIntTypeWidth(LLVMTypeOf(arg)) > 32)
        r = LLVMBuildSExt(g->builder, r, LLVMTypeOf(arg), "ch.map.w");
      emit_release_owned_call_temp(g, expr->call.args.items[0], arg, locals);
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
    LLVMTypeRef strtod_ty = LLVMFunctionType(
        dbl, (LLVMTypeRef[]){i8ptr, LLVMPointerType(i8ptr, 0)}, 2, 0);
    LLVMValueRef f = LLVMGetNamedFunction(g->mod, "zan_rt_dbl_parse");
    if (!f)
      f = LLVMAddFunction(g->mod, "zan_rt_dbl_parse", strtod_ty);
    /* A512：失败/残留 → 软诊断 + 安全值 0（曾静默返 0 继续执行） */
    LLVMValueRef endp = emit_entry_alloca(g, i8ptr, "todbl.endp");
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), endp);
    LLVMValueRef s_safe = emit_parse_str_guard(g, arg, expr->loc);
    LLVMValueRef r = zan_call2(g->builder, strtod_ty, f,
                               (LLVMValueRef[]){s_safe, endp}, 2, "todbl");
    LLVMValueRef ep = LLVMBuildLoad2(g->builder, i8ptr, endp, "todbl.ep");
    LLVMValueRef bad = emit_parse_bad_pred(g, s_safe, ep);
    emit_runtime_check(g, bad, expr->loc,
                       "Convert.ToDouble: input is not a valid number");
    r = LLVMBuildSelect(g->builder, bad, LLVMConstReal(dbl, 0.0), r,
                        "todbl.safe");
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
  if ((int_recv || dbl_recv || bool_recv) && pm.len == 5 &&
      memcmp(pm.str, "Parse", 5) == 0 && expr->call.args.count == 1) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef s = emit_expr(g, expr->call.args.items[0], locals);
    zan_type_t *at = infer_expr_type(g, expr->call.args.items[0], locals);
    if (!type_is_text_arg(at)) {
      zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3057,
                         "%.*s.Parse requires a string argument", (int)rn.len,
                         rn.str);
      if (bool_recv) {
        return LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
      }
      return int_recv ? LLVMConstInt(i64, 0, 0) : LLVMConstReal(dbl, 0.0);
    }
    LLVMValueRef r;
    if (bool_recv) {
      /* C# bool.Parse：只认 true/false 的大小写任意变体，
       * 其余 FormatException（消息含 "not a valid" 命中
       * guard 的 Format 路由）。strcasecmp 在目标 exe 的
       * CRT 可用（runtime gui_runtime_font.c 同款先例）。 */
      LLVMTypeRef i1t = LLVMInt1TypeInContext(g->ctx);
      LLVMTypeRef sc_ty =
          LLVMFunctionType(i32t, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0);
      LLVMValueRef f = get_libc_fn(g, "strcasecmp", sc_ty);
      LLVMValueRef s_safe = emit_parse_str_guard(g, s, expr->loc);
      LLVMValueRef s_true =
          LLVMBuildGlobalStringPtr(g->builder, "true", "bool.ptrue");
      LLVMValueRef s_false =
          LLVMBuildGlobalStringPtr(g->builder, "false", "bool.pfalse");
      LLVMValueRef st = zan_call2(
          g->builder, sc_ty, f, (LLVMValueRef[]){s_safe, s_true}, 2, "bool.pt");
      LLVMValueRef sf =
          zan_call2(g->builder, sc_ty, f, (LLVMValueRef[]){s_safe, s_false}, 2,
                    "bool.pf");
      LLVMValueRef is_t = zan_icmp(g->builder, LLVMIntEQ, st,
                                   LLVMConstInt(i32t, 0, 0), "bool.ist");
      LLVMValueRef is_f = zan_icmp(g->builder, LLVMIntEQ, sf,
                                   LLVMConstInt(i32t, 0, 0), "bool.isf");
      LLVMValueRef ok = LLVMBuildOr(g->builder, is_t, is_f, "bool.ok");
      emit_runtime_check(g, LLVMBuildNot(g->builder, ok, "bool.bad"), expr->loc,
                         "bool.Parse: input is not a valid Boolean");
      LLVMValueRef v =
          LLVMBuildSelect(g->builder, is_t, LLVMConstInt(i1t, 1, 0),
                          LLVMConstInt(i1t, 0, 0), "bool.v");
      emit_release_owned_call_temp(g, expr->call.args.items[0], s, locals);
      return LLVMBuildSelect(g->builder, ok, v, LLVMConstInt(i1t, 0, 0),
                             "bool.safe");
    }
    if (int_recv) {
      int is64 = (rn.len == 4 && memcmp(rn.str, "long", 4) == 0) ||
                 (rn.len == 5 && memcmp(rn.str, "Int64", 5) == 0);
      LLVMValueRef s_safe = emit_parse_str_guard(g, s, expr->loc);
      LLVMValueRef out_slot = emit_entry_alloca(g, i64, "parse.val");
      LLVMValueRef pfn = get_try_parse_i64_fn(g);
      LLVMValueRef st = zan_call2(g->builder, LLVMGlobalGetValueType(pfn), pfn,
                                  (LLVMValueRef[]){s_safe, out_slot}, 2,
                                  "parse.st");
      r = LLVMBuildLoad2(g->builder, i64, out_slot, "parse");
      /* C# 语义两路分流：status==1 格式错 → FormatException，
       * status==2 或 i32 中段越界 → OverflowException。 */
      LLVMValueRef bad = zan_icmp(g->builder, LLVMIntEQ, st,
                                  LLVMConstInt(i32t, 1, 0), "parse.bad");
      emit_runtime_check(g, bad, expr->loc,
                         "int.Parse: input is not a valid integer");
      LLVMValueRef over = zan_icmp(g->builder, LLVMIntEQ, st,
                                   LLVMConstInt(i32t, 2, 0), "parse.erange");
      if (!is64) {
        LLVMValueRef ok0 = zan_icmp(g->builder, LLVMIntEQ, st,
                                    LLVMConstInt(i32t, 0, 0), "parse.ok0");
        LLVMValueRef out32 = LLVMBuildOr(
            g->builder,
            zan_icmp(g->builder, LLVMIntSLT, r,
                     LLVMConstInt(i64, (int64_t)INT32_MIN, 0), "parse.lo"),
            zan_icmp(g->builder, LLVMIntSGT, r,
                     LLVMConstInt(i64, INT32_MAX, 0), "parse.hi"),
            "parse.over32");
        over = LLVMBuildOr(g->builder, over,
                           zan_and(g->builder, ok0, out32, "parse.o32"),
                           "parse.over");
      }
      emit_runtime_check(
          g, over, expr->loc,
          "int.Parse: value is outside the range of the target type");
      bad = LLVMBuildOr(g->builder, bad, over, "parse.bad2");
      r = LLVMBuildSelect(g->builder, bad, LLVMConstInt(i64, 0, 0), r,
                          "parse.safe");
    } else {
      LLVMTypeRef strtod_ty =
          LLVMFunctionType(dbl, (LLVMTypeRef[]){i8ptr, i8pp}, 2, 0);
      LLVMValueRef f = LLVMGetNamedFunction(g->mod, "zan_rt_dbl_parse");
      if (!f)
        f = LLVMAddFunction(g->mod, "zan_rt_dbl_parse", strtod_ty);
      LLVMValueRef endp = emit_entry_alloca(g, i8ptr, "parse.endp");
      LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), endp);
      LLVMValueRef s_safe = emit_parse_str_guard(g, s, expr->loc);
      r = zan_call2(g->builder, strtod_ty, f, (LLVMValueRef[]){s_safe, endp}, 2,
                    "parse");
      LLVMValueRef ep = LLVMBuildLoad2(g->builder, i8ptr, endp, "parse.ep");
      LLVMValueRef bad = emit_parse_bad_pred(g, s_safe, ep);
      /* 溢出（strtod ERANGE）由 zan_rt_dbl_parse 把 endp 拨回
       * 起点走零消费判坏——"Infinity" 特殊拼写是合法 INF 回读，
       * 不能按值判 INF 当溢出杀（曾误杀致 case_double_format 红）。 */
      emit_runtime_check(g, bad, expr->loc,
                         "double.Parse: input is not a valid number");
      r = LLVMBuildSelect(g->builder, bad, LLVMConstReal(dbl, 0.0), r,
                          "parse.safe");
    }
    emit_release_owned_call_temp(g, expr->call.args.items[0], s, locals);
    return r;
  }
  if ((int_recv || dbl_recv || bool_recv) && pm.len == 8 &&
      memcmp(pm.str, "TryParse", 8) == 0 && expr->call.args.count == 2) {
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
    LLVMValueRef s_safe = LLVMBuildSelect(
        g->builder, isnull, zan_irgen_intern_string(g, ""), s, "tp.ssafe");
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
      if (ol) {
        outp = ol->alloca;
        out_ty = ol->type;
      }
    }

    if (bool_recv) {
      LLVMTypeRef sc_ty =
          LLVMFunctionType(i32t, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0);
      LLVMValueRef f = get_libc_fn(g, "strcasecmp", sc_ty);
      LLVMValueRef s_true =
          LLVMBuildGlobalStringPtr(g->builder, "true", "btp.ptrue");
      LLVMValueRef s_false =
          LLVMBuildGlobalStringPtr(g->builder, "false", "btp.pfalse");
      LLVMValueRef st = zan_call2(
          g->builder, sc_ty, f, (LLVMValueRef[]){s_safe, s_true}, 2, "btp.pt");
      LLVMValueRef sf = zan_call2(
          g->builder, sc_ty, f, (LLVMValueRef[]){s_safe, s_false}, 2, "btp.pf");
      LLVMValueRef is_t = zan_icmp(g->builder, LLVMIntEQ, st,
                                   LLVMConstInt(i32t, 0, 0), "btp.ist");
      LLVMValueRef is_f = zan_icmp(g->builder, LLVMIntEQ, sf,
                                   LLVMConstInt(i32t, 0, 0), "btp.isf");
      LLVMValueRef ok = LLVMBuildOr(g->builder, is_t, is_f, "btp.ok");
      LLVMValueRef not_null = LLVMBuildNot(g->builder, isnull, "btp.notnull");
      ok = zan_and(g->builder, not_null, ok, "btp.valid");
      LLVMValueRef stored =
          LLVMBuildSelect(g->builder, is_t, LLVMConstInt(i1t, 1, 0),
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

    LLVMValueRef v;
    LLVMValueRef ok;
    LLVMValueRef not_null = LLVMBuildNot(g->builder, isnull, "tp.notnull");
    if (int_recv) {
      int is64 = (rn.len == 4 && memcmp(rn.str, "long", 4) == 0) ||
                 (rn.len == 5 && memcmp(rn.str, "Int64", 5) == 0);
      LLVMValueRef out_slot = emit_entry_alloca(g, i64, "tp.val");
      LLVMValueRef pfn = get_try_parse_i64_fn(g);
      LLVMValueRef st = zan_call2(g->builder, LLVMGlobalGetValueType(pfn), pfn,
                                  (LLVMValueRef[]){s_safe, out_slot}, 2,
                                  "tp.st");
      v = LLVMBuildLoad2(g->builder, i64, out_slot, "tp");
      ok = zan_and(g->builder, not_null,
                   zan_icmp(g->builder, LLVMIntEQ, st,
                            LLVMConstInt(i32t, 0, 0), "tp.st0"),
                   "ok");
      if (!is64) {
        LLVMValueRef in32 = zan_and(
            g->builder,
            zan_icmp(g->builder, LLVMIntSGE, v,
                     LLVMConstInt(i64, (int64_t)INT32_MIN, 0), "tp.ge32"),
            zan_icmp(g->builder, LLVMIntSLE, v,
                     LLVMConstInt(i64, INT32_MAX, 0), "tp.le32"),
            "tp.in32");
        ok = zan_and(g->builder, ok, in32, "ok2");
      }
    } else {
      LLVMValueRef endp = emit_entry_alloca(g, i8ptr, "endp");
      LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), endp);
      LLVMTypeRef strtod_ty =
          LLVMFunctionType(dbl, (LLVMTypeRef[]){i8ptr, i8pp}, 2, 0);
      LLVMValueRef f = LLVMGetNamedFunction(g->mod, "zan_rt_dbl_parse");
      if (!f)
        f = LLVMAddFunction(g->mod, "zan_rt_dbl_parse", strtod_ty);
      v = zan_call2(g->builder, strtod_ty, f, (LLVMValueRef[]){s_safe, endp}, 2,
                    "tp");
      LLVMValueRef e = LLVMBuildLoad2(g->builder, i8ptr, endp, "e");
      LLVMValueRef moved = zan_icmp(g->builder, LLVMIntNE, e, s_safe, "moved");
      LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8, e, "ch");
      LLVMValueRef at_end =
          zan_icmp(g->builder, LLVMIntEQ, ch, LLVMConstInt(i8, 0, 0), "atend");
      ok = zan_and(g->builder, not_null,
                   zan_and(g->builder, moved, at_end, "moved_end"), "ok");
    }
    LLVMValueRef stored =
        LLVMBuildSelect(g->builder, ok, v,
                        int_recv ? LLVMConstInt(i64, 0, 0)
                                 : (LLVMValueRef)LLVMConstReal(dbl, 0.0),
                        "tpv");
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
