bool emit_native_memory_call(zan_irgen_t *g, zan_ast_node_t *expr,
                             local_scope_t *locals, LLVMValueRef *out) {
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
  LLVMValueRef zero64 = LLVMConstInt(i64t, 0, 0);

  if (is_call_to(expr, "NativeMemory", "Alloc") && expr->call.args.count == 1) {
    LLVMValueRef size = nm_arg(g, expr, 0, locals);
    LLVMTypeRef ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i64t, i64t}, 2, 0);
    LLVMValueRef fn = get_libc_fn(g, "calloc", ty);
    /* 负数分配大小校验：直接返回空指针 0 */
    LLVMValueRef ok = zan_icmp(g->builder, LLVMIntSGE, size, zero64, "nm.nneg");
    LLVMValueRef fnh = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef ok_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.ok");
    LLVMBasicBlockRef bad_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.bad");
    LLVMBasicBlockRef done_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.done");
    LLVMBuildCondBr(g->builder, ok, ok_bb, bad_bb);

    LLVMPositionBuilderAtEnd(g->builder, ok_bb);
    LLVMValueRef p = zan_call2(g->builder, ty, fn,
                               (LLVMValueRef[]){size, LLVMConstInt(i64t, 1, 0)},
                               2, "nm.alloc");
    LLVMBuildBr(g->builder, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, bad_bb);
    LLVMBuildBr(g->builder, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    LLVMValueRef ptr = LLVMBuildPhi(g->builder, i8ptr, "nm.p");
    LLVMAddIncoming(ptr, (LLVMValueRef[]){p, LLVMConstNull(i8ptr)},
                    (LLVMBasicBlockRef[]){ok_bb, bad_bb}, 2);
    *out = LLVMBuildPtrToInt(g->builder, ptr, i64t, "nm.addr");
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "Free") && expr->call.args.count == 1) {
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    LLVMTypeRef ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                      (LLVMTypeRef[]){i8ptr}, 1, 0);
    LLVMValueRef fn = get_libc_fn(g, "free", ty);
    LLVMValueRef pp = LLVMBuildIntToPtr(g->builder, p, i8ptr, "nm.fp");
    zan_call2(g->builder, ty, fn, &pp, 1, "");
    *out = zero64;
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "Realloc") &&
      expr->call.args.count == 2) {
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    LLVMValueRef size = nm_arg(g, expr, 1, locals);
    LLVMTypeRef ty =
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i64t}, 2, 0);
    LLVMValueRef fn = get_libc_fn(g, "realloc", ty);
    LLVMValueRef ok = zan_icmp(g->builder, LLVMIntSGE, size, zero64, "nm.rn");
    LLVMValueRef fnh = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.rok");
    LLVMBasicBlockRef bad_bb = LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.rbad");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.rdone");
    LLVMBuildCondBr(g->builder, ok, ok_bb, bad_bb);
    LLVMPositionBuilderAtEnd(g->builder, ok_bb);
    LLVMValueRef pp = LLVMBuildIntToPtr(g->builder, p, i8ptr, "nm.rp");
    LLVMValueRef rp = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){pp, size}, 2, "nm.re");
    LLVMBuildBr(g->builder, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, bad_bb);
    LLVMBuildBr(g->builder, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    LLVMValueRef res = LLVMBuildPhi(g->builder, i8ptr, "nm.rphi");
    LLVMAddIncoming(res, (LLVMValueRef[]){rp, LLVMConstNull(i8ptr)},
                    (LLVMBasicBlockRef[]){ok_bb, bad_bb}, 2);
    *out = LLVMBuildPtrToInt(g->builder, res, i64t, "nm.raddr");
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "AlignedAlloc") &&
      expr->call.args.count == 2) {
    LLVMValueRef size = nm_arg(g, expr, 0, locals);
    LLVMValueRef align = nm_arg(g, expr, 1, locals);
    LLVMValueRef one64 = LLVMConstInt(i64t, 1, 0);
    LLVMValueRef eight64 = LLVMConstInt(i64t, 8, 0);
    LLVMValueRef sz_ok = zan_icmp(g->builder, LLVMIntSGE, size, zero64, "nm.aa.sz");
    LLVMValueRef al_pos = zan_icmp(g->builder, LLVMIntSGT, align, zero64, "nm.aa.pos");
    LLVMValueRef al_m1 = LLVMBuildSub(g->builder, align, one64, "nm.aa.m1");
    LLVMValueRef al_pow2 = zan_icmp(g->builder, LLVMIntEQ,
                                    LLVMBuildAnd(g->builder, align, al_m1, "nm.aa.and"),
                                    zero64, "nm.aa.p2");
    LLVMValueRef ok = LLVMBuildAnd(g->builder, sz_ok,
                                   LLVMBuildAnd(g->builder, al_pos, al_pow2, "nm.aa.aok"),
                                   "nm.aa.ok");
    LLVMValueRef fnh = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.aa.run");
    LLVMBasicBlockRef nonnull_bb = LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.aa.nn");
    LLVMBasicBlockRef bad_bb = LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.aa.bad");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.aa.done");
    LLVMBuildCondBr(g->builder, ok, ok_bb, bad_bb);

    LLVMPositionBuilderAtEnd(g->builder, ok_bb);
    LLVMValueRef lt8 = zan_icmp(g->builder, LLVMIntSLT, align, eight64, "nm.aa.lt8");
    LLVMValueRef eff = LLVMBuildSelect(g->builder, lt8, eight64, align, "nm.aa.eff");
    LLVMValueRef total = zan_add(g->builder, zan_add(g->builder, size, eff, "nm.aa.t0"),
                                 eight64, "nm.aa.tot");
    LLVMTypeRef cty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i64t, i64t}, 2, 0);
    LLVMValueRef cfn = get_libc_fn(g, "calloc", cty);
    LLVMValueRef raw = zan_call2(g->builder, cty, cfn,
                                 (LLVMValueRef[]){total, one64}, 2, "nm.aa.raw");
    LLVMValueRef nn = zan_icmp(g->builder, LLVMIntNE, raw, LLVMConstNull(i8ptr), "nm.aa.chk");
    LLVMBuildCondBr(g->builder, nn, nonnull_bb, bad_bb);

    LLVMPositionBuilderAtEnd(g->builder, nonnull_bb);
    LLVMValueRef raw_i = LLVMBuildPtrToInt(g->builder, raw, i64t, "nm.aa.ri");
    LLVMValueRef eff_m1 = LLVMBuildSub(g->builder, eff, one64, "nm.aa.em1");
    LLVMValueRef mask = LLVMBuildXor(g->builder, eff_m1, LLVMConstInt(i64t, (uint64_t)-1, 1), "nm.aa.msk");
    LLVMValueRef bumped = zan_add(g->builder, zan_add(g->builder, raw_i, eight64, "nm.aa.b0"),
                                  eff_m1, "nm.aa.b1");
    LLVMValueRef aligned = LLVMBuildAnd(g->builder, bumped, mask, "nm.aa.al");
    LLVMValueRef hdr_i = LLVMBuildSub(g->builder, aligned, eight64, "nm.aa.hi");
    LLVMValueRef hdr_p = LLVMBuildIntToPtr(g->builder, hdr_i, LLVMPointerType(i64t, 0), "nm.aa.hp");
    LLVMBuildStore(g->builder, raw_i, hdr_p);
    LLVMBuildBr(g->builder, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, bad_bb);
    LLVMBuildBr(g->builder, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    LLVMValueRef res = LLVMBuildPhi(g->builder, i64t, "nm.aa.res");
    LLVMAddIncoming(res, (LLVMValueRef[]){aligned, zero64},
                    (LLVMBasicBlockRef[]){nonnull_bb, bad_bb}, 2);
    *out = res;
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "AlignedFree") &&
      expr->call.args.count == 1) {
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    LLVMValueRef nn = zan_icmp(g->builder, LLVMIntNE, p, zero64, "nm.af.nn");
    LLVMValueRef fnh = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef run_bb = LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.af.run");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.af.done");
    LLVMBuildCondBr(g->builder, nn, run_bb, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, run_bb);
    LLVMValueRef hdr_i = LLVMBuildSub(g->builder, p, LLVMConstInt(i64t, 8, 0), "nm.af.hi");
    LLVMValueRef hdr_p = LLVMBuildIntToPtr(g->builder, hdr_i, LLVMPointerType(i64t, 0), "nm.af.hp");
    LLVMValueRef raw_i = LLVMBuildLoad2(g->builder, i64t, hdr_p, "nm.af.ri");
    LLVMValueRef raw_p = LLVMBuildIntToPtr(g->builder, raw_i, i8ptr, "nm.af.rp");
    LLVMTypeRef fty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                       (LLVMTypeRef[]){i8ptr}, 1, 0);
    LLVMValueRef ffn = get_libc_fn(g, "free", fty);
    zan_call2(g->builder, fty, ffn, &raw_p, 1, "");
    LLVMBuildBr(g->builder, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    *out = zero64;
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "AddressOf") &&
      expr->call.args.count == 1 && expr->call.args.items[0] &&
      expr->call.args.items[0]->kind == AST_REF_ARG) {
    LLVMValueRef ptr =
        emit_ref_lvalue_ptr(g, expr->call.args.items[0]->ref_arg.expr, locals);
    *out = ptr ? LLVMBuildPtrToInt(g->builder, ptr, i64t, "nm.addrof") : zero64;
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "FunctionPointer") &&
      expr->call.args.count == 1) {
    zan_ast_node_t *a0 = expr->call.args.items[0];
    zan_symbol_t *msym = NULL;
    if (a0 && a0->kind == AST_MEMBER_ACCESS && a0->member.object &&
        a0->member.object->kind == AST_IDENTIFIER) {
      zan_symbol_t *ts = zan_binder_lookup(g->binder, a0->member.object->ident.name);
      if (ts) msym = get_method_sym(ts, a0->member.name);
    } else if (a0 && a0->kind == AST_IDENTIFIER && g->current_type_sym) {
      msym = get_method_sym(g->current_type_sym, a0->ident.name);
    }
    char csym[512];
    bool has_csym = false;
    if (msym && msym->decl) {
      if (method_c_export_name(msym->decl, csym, sizeof(csym))) {
        has_csym = true;
      } else if (msym->decl->kind == AST_METHOD_DECL &&
                 msym->decl->method_decl.ext &&
                 msym->decl->method_decl.ext->extern_lib.str) {
        zan_istr_t *ep = msym->decl->method_decl.ext->entry_point;
        if (ep && ep->str && ep->len > 0)
          snprintf(csym, sizeof(csym), "%.*s", (int)ep->len, ep->str);
        else
          snprintf(csym, sizeof(csym), "%.*s", (int)msym->name.len, msym->name.str);
        has_csym = true;
      }
    }
    if (has_csym) {
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, csym);
      if (!fn) {
        LLVMTypeRef vty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 1);
        fn = LLVMAddFunction(g->mod, csym, vty);
      }
      if (fn) {
        *out = LLVMBuildPtrToInt(g->builder, fn, i64t, "nm.fnptr");
        return true;
      }
    }
    *out = zero64;
    return true;
  }
  if ((is_call_to(expr, "NativeMemory", "Invoke") ||
       is_call_to(expr, "NativeMemory", "InvokeVoid") ||
       is_call_to(expr, "NativeMemory", "InvokeWithCC") ||
       is_call_to(expr, "NativeMemory", "InvokeVoidWithCC") ||
       is_call_to(expr, "NativeMemory", "InvokeVariadic") ||
       is_call_to(expr, "NativeMemory", "InvokeVariadicVoid")) &&
      expr->call.args.count >= 1) {
    bool with_cc = is_call_to(expr, "NativeMemory", "InvokeWithCC") ||
                   is_call_to(expr, "NativeMemory", "InvokeVoidWithCC");
    bool is_variadic = is_call_to(expr, "NativeMemory", "InvokeVariadic") ||
                       is_call_to(expr, "NativeMemory", "InvokeVariadicVoid");
    bool is_void = is_call_to(expr, "NativeMemory", "InvokeVoid") ||
                   is_call_to(expr, "NativeMemory", "InvokeVoidWithCC") ||
                   is_call_to(expr, "NativeMemory", "InvokeVariadicVoid");
    int arg_base = with_cc ? 1 : 0;
    int params_start = is_variadic ? 2 : (arg_base + 1);
    if (expr->call.args.count < params_start) {
      *out = zero64;
      return true;
    }
    unsigned call_cc = LLVMCCallConv;
    if (with_cc && expr->call.args.items[0] &&
        expr->call.args.items[0]->kind == AST_STRING_LITERAL) {
      zan_istr_t cs = expr->call.args.items[0]->str_val;
      char ccb[64];
      int cl = cs.len < 63 ? cs.len : 63;
      for (int ci = 0; ci < cl; ci++) {
        char ch = cs.str[ci];
        ccb[ci] = (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
      }
      ccb[cl] = '\0';
      if (strcmp(ccb, "stdcall") == 0) call_cc = LLVMX86StdcallCallConv;
      else if (strcmp(ccb, "fastcall") == 0) call_cc = LLVMX86FastcallCallConv;
      else if (strcmp(ccb, "win64") == 0) call_cc = LLVMWin64CallConv;
      else if (strcmp(ccb, "sysv64") == 0) call_cc = LLVMX8664SysVCallConv;
      else if (strcmp(ccb, "aapcs") == 0) call_cc = LLVMARMAAPCSCallConv;
      else if (strcmp(ccb, "vectorcall") == 0) call_cc = LLVMX86VectorCallCallConv;
      else if (strcmp(ccb, "preserve_most") == 0) call_cc = LLVMPreserveMostCallConv;
      else if (strcmp(ccb, "preserve_all") == 0) call_cc = LLVMPreserveAllCallConv;
    }
    LLVMTypeRef ret_lt = LLVMVoidTypeInContext(g->ctx);
    if (!is_void && expr->call.type_args.count == 1) {
      zan_type_t *rt = resolve_type_ctx(g, expr->call.type_args.items[0]);
      if (rt && rt->kind != TYPE_VOID)
        ret_lt = map_type(g, rt);
      else
        is_void = true;
    }
    LLVMValueRef fn_addr = nm_arg(g, expr, arg_base, locals);
    int nparams = expr->call.args.count - params_start;
    int fixed_count = nparams;
    if (is_variadic && expr->call.args.items[1] &&
        expr->call.args.items[1]->kind == AST_INT_LITERAL) {
      fixed_count = (int)expr->call.args.items[1]->int_val;
      if (fixed_count < 0) fixed_count = 0;
      if (fixed_count > nparams) fixed_count = nparams;
    }
    LLVMTypeRef ptys[32];
    LLVMValueRef pvals[32];
    if (nparams > 32) nparams = 32;
    if (fixed_count > nparams) fixed_count = nparams;
    for (int i = 0; i < nparams; i++) {
      zan_ast_node_t *ai = expr->call.args.items[i + params_start];
      if (ai && ai->kind == AST_REF_ARG && ai->ref_arg.expr) {
        LLVMValueRef ptr = emit_ref_lvalue_ptr(g, ai->ref_arg.expr, locals);
        pvals[i] = ptr ? ptr : LLVMConstNull(i8ptr);
        ptys[i] = LLVMTypeOf(pvals[i]);
      } else {
        LLVMValueRef v = emit_expr(g, ai, locals);
        zan_type_t *at = infer_expr_type(g, ai, locals);
        LLVMTypeRef lt = at ? map_type(g, at) : LLVMTypeOf(v);
        v = coerce_struct_value_arg(g, v, lt);
        LLVMTypeRef vt = LLVMTypeOf(v);
        if (LLVMGetTypeKind(lt) == LLVMIntegerTypeKind &&
            LLVMGetTypeKind(vt) == LLVMIntegerTypeKind && lt != vt) {
          v = coerce_int_to(g, v, lt);
        } else if (LLVMGetTypeKind(lt) == LLVMFloatTypeKind &&
                   LLVMGetTypeKind(vt) == LLVMDoubleTypeKind) {
          v = LLVMBuildFPTrunc(g->builder, v, lt, "nm.inv.f32");
        } else if (LLVMGetTypeKind(lt) == LLVMDoubleTypeKind &&
                   LLVMGetTypeKind(vt) == LLVMFloatTypeKind) {
          v = LLVMBuildFPExt(g->builder, v, lt, "nm.inv.f64");
        }
        /* ISO C default argument promotions for variadic arguments */
        if (is_variadic && i >= fixed_count) {
          if (LLVMGetTypeKind(lt) == LLVMFloatTypeKind) {
            lt = LLVMDoubleTypeInContext(g->ctx);
            v = LLVMBuildFPExt(g->builder, v, lt, "nm.va.f64");
          } else if (LLVMGetTypeKind(lt) == LLVMIntegerTypeKind &&
                     LLVMGetIntTypeWidth(lt) < 32) {
            bool is_uns = at && (at->kind == TYPE_BOOL || at->kind == TYPE_BYTE ||
                                 at->kind == TYPE_USHORT || at->kind == TYPE_CHAR);
            lt = i32t;
            v = is_uns ? LLVMBuildZExt(g->builder, v, i32t, "nm.va.z32")
                       : LLVMBuildSExt(g->builder, v, i32t, "nm.va.s32");
          }
        }
        pvals[i] = v;
        ptys[i] = lt;
      }
    }
    LLVMTypeRef fty = LLVMFunctionType(ret_lt, ptys,
                                       (unsigned)(is_variadic ? fixed_count : nparams),
                                       is_variadic ? 1 : 0);
    LLVMValueRef fptr = LLVMBuildIntToPtr(g->builder, fn_addr,
                                          LLVMPointerType(fty, 0), "nm.inv.fn");
    LLVMValueRef call = LLVMBuildCall2(g->builder, fty, fptr, pvals,
                                       (unsigned)nparams, is_void ? "" : "nm.inv");
    LLVMSetInstructionCallConv(call, call_cc);
    *out = is_void ? zero64 : call;
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "BitCast") &&
      expr->call.args.count == 1 && expr->call.type_args.count == 2) {
    zan_type_t *src_t = resolve_type_ctx(g, expr->call.type_args.items[0]);
    zan_type_t *dst_t = resolve_type_ctx(g, expr->call.type_args.items[1]);
    LLVMTypeRef src_lt = src_t ? map_type(g, src_t) : i64t;
    LLVMTypeRef dst_lt = dst_t ? map_type(g, dst_t) : i64t;
    LLVMTargetDataRef td = LLVMGetModuleDataLayout(g->mod);
    unsigned long long sz_src = td ? LLVMABISizeOfType(td, src_lt) : 0;
    unsigned long long sz_dst = td ? LLVMABISizeOfType(td, dst_lt) : 0;
    if (sz_src != sz_dst) {
      zan_diag_emit_code(
          g->diag, DIAG_ERROR, expr->loc, 3101,
          "NativeMemory.BitCast<TFrom, TTo> requires identical sizes (got %llu vs %llu bytes)",
          sz_src, sz_dst);
      *out = LLVMConstNull(dst_lt);
      return true;
    }
    LLVMValueRef v = emit_expr(g, expr->call.args.items[0], locals);
    v = coerce_struct_value_arg(g, v, src_lt);
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(src_lt) == LLVMIntegerTypeKind &&
        LLVMGetTypeKind(vt) == LLVMIntegerTypeKind && src_lt != vt) {
      v = coerce_int_to(g, v, src_lt);
    } else if (LLVMGetTypeKind(src_lt) == LLVMFloatTypeKind &&
               LLVMGetTypeKind(vt) == LLVMDoubleTypeKind) {
      v = LLVMBuildFPTrunc(g->builder, v, src_lt, "nm.bc.f32");
    } else if (LLVMGetTypeKind(src_lt) == LLVMDoubleTypeKind &&
               LLVMGetTypeKind(vt) == LLVMFloatTypeKind) {
      v = LLVMBuildFPExt(g->builder, v, src_lt, "nm.bc.f64");
    }
    LLVMTypeKind sk = LLVMGetTypeKind(src_lt);
    LLVMTypeKind dk = LLVMGetTypeKind(dst_lt);
    bool scalar_ok =
        (sk == LLVMIntegerTypeKind || sk == LLVMFloatTypeKind || sk == LLVMDoubleTypeKind) &&
        (dk == LLVMIntegerTypeKind || dk == LLVMFloatTypeKind || dk == LLVMDoubleTypeKind);
    if (scalar_ok && src_lt == dst_lt) {
      *out = v;
    } else if (scalar_ok) {
      *out = LLVMBuildBitCast(g->builder, v, dst_lt, "nm.bc");
    } else {
      LLVMValueRef tmp = emit_entry_alloca(g, src_lt, "nm.bc.tmp");
      LLVMBuildStore(g->builder, v, tmp);
      LLVMValueRef dp = LLVMBuildBitCast(g->builder, tmp, LLVMPointerType(dst_lt, 0), "nm.bc.p");
      *out = LLVMBuildLoad2(g->builder, dst_lt, dp, "nm.bc.v");
    }
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "ZeroInit") &&
      expr->call.args.count == 1 && expr->call.args.items[0] &&
      expr->call.args.items[0]->kind == AST_REF_ARG) {
    zan_ast_node_t *tgt = expr->call.args.items[0]->ref_arg.expr;
    LLVMValueRef ptr = emit_ref_lvalue_ptr(g, tgt, locals);
    zan_type_t *tt = infer_expr_type(g, tgt, locals);
    LLVMTypeRef lt = tt ? map_type(g, tt) : i64t;
    if (ptr) {
      LLVMValueRef tp = LLVMBuildBitCast(g->builder, ptr, LLVMPointerType(lt, 0), "nm.zi.p");
      LLVMBuildStore(g->builder, LLVMConstNull(lt), tp);
    }
    *out = zero64;
    return true;
  }
  if ((is_call_to(expr, "NativeMemory", "Read") ||
       is_call_to(expr, "NativeMemory", "ReadUnaligned") ||
       is_call_to(expr, "NativeMemory", "StreamLoad")) &&
      expr->call.args.count == 1 && expr->call.type_args.count == 1) {
    bool unaligned = is_call_to(expr, "NativeMemory", "ReadUnaligned");
    bool nontemp = is_call_to(expr, "NativeMemory", "StreamLoad");
    zan_type_t *et = resolve_type_ctx(g, expr->call.type_args.items[0]);
    LLVMTypeRef lt = et ? map_type(g, et) : i64t;
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    LLVMValueRef tp = LLVMBuildIntToPtr(g->builder, p, LLVMPointerType(lt, 0), "nm.rd.p");
    LLVMValueRef ld = LLVMBuildLoad2(g->builder, lt, tp, nontemp ? "nm.ntld" : "nm.rd");
    if (unaligned)
      LLVMSetAlignment(ld, 1);
    if (nontemp) {
      unsigned nt_kind = LLVMGetMDKindIDInContext(g->ctx, "nontemporal", 11);
      LLVMMetadataRef one_md = LLVMValueAsMetadata(LLVMConstInt(i32t, 1, 0));
      LLVMMetadataRef nt_node = LLVMMDNodeInContext2(g->ctx, &one_md, 1);
      LLVMSetMetadata(ld, nt_kind, LLVMMetadataAsValue(g->ctx, nt_node));
    }
    *out = ld;
    return true;
  }
  if ((is_call_to(expr, "NativeMemory", "Write") ||
       is_call_to(expr, "NativeMemory", "WriteUnaligned") ||
       is_call_to(expr, "NativeMemory", "StreamStore")) &&
      expr->call.args.count == 2 && expr->call.type_args.count == 1) {
    bool unaligned = is_call_to(expr, "NativeMemory", "WriteUnaligned");
    bool nontemp = is_call_to(expr, "NativeMemory", "StreamStore");
    zan_type_t *et = resolve_type_ctx(g, expr->call.type_args.items[0]);
    LLVMTypeRef lt = et ? map_type(g, et) : i64t;
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    LLVMValueRef v = emit_expr(g, expr->call.args.items[1], locals);
    v = coerce_struct_value_arg(g, v, lt);
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(lt) == LLVMIntegerTypeKind &&
        LLVMGetTypeKind(vt) == LLVMIntegerTypeKind && lt != vt) {
      v = coerce_int_to(g, v, lt);
    } else if (LLVMGetTypeKind(lt) == LLVMFloatTypeKind &&
               LLVMGetTypeKind(vt) == LLVMDoubleTypeKind) {
      v = LLVMBuildFPTrunc(g->builder, v, lt, "nm.wr.f32");
    } else if (LLVMGetTypeKind(lt) == LLVMDoubleTypeKind &&
               LLVMGetTypeKind(vt) == LLVMFloatTypeKind) {
      v = LLVMBuildFPExt(g->builder, v, lt, "nm.wr.f64");
    }
    LLVMValueRef tp = LLVMBuildIntToPtr(g->builder, p, LLVMPointerType(lt, 0), "nm.wr.p");
    LLVMValueRef st = LLVMBuildStore(g->builder, v, tp);
    if (unaligned)
      LLVMSetAlignment(st, 1);
    if (nontemp) {
      unsigned nt_kind = LLVMGetMDKindIDInContext(g->ctx, "nontemporal", 11);
      LLVMMetadataRef one_md = LLVMValueAsMetadata(LLVMConstInt(i32t, 1, 0));
      LLVMMetadataRef nt_node = LLVMMDNodeInContext2(g->ctx, &one_md, 1);
      LLVMSetMetadata(st, nt_kind, LLVMMetadataAsValue(g->ctx, nt_node));
    }
    *out = zero64;
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "AsRef") &&
      expr->call.args.count == 1 && expr->call.type_args.count == 1) {
    zan_type_t *et = resolve_type_ctx(g, expr->call.type_args.items[0]);
    LLVMTypeRef lt = et ? map_type(g, et) : i64t;
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    *out = LLVMBuildIntToPtr(g->builder, p, LLVMPointerType(lt, 0), "nm.asref");
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "At") &&
      expr->call.args.count == 2 && expr->call.type_args.count == 1) {
    zan_type_t *et = resolve_type_ctx(g, expr->call.type_args.items[0]);
    LLVMTypeRef lt = et ? map_type(g, et) : i64t;
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    LLVMValueRef idx = nm_arg(g, expr, 1, locals);
    LLVMValueRef base = LLVMBuildIntToPtr(g->builder, p, LLVMPointerType(lt, 0), "nm.at.base");
    *out = LLVMBuildGEP2(g->builder, lt, base, &idx, 1, "nm.at");
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "Add") &&
      expr->call.args.count == 2 && expr->call.type_args.count == 1) {
    zan_type_t *et = resolve_type_ctx(g, expr->call.type_args.items[0]);
    LLVMTypeRef lt = et ? map_type(g, et) : i64t;
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    LLVMValueRef cnt = nm_arg(g, expr, 1, locals);
    LLVMTargetDataRef td = LLVMGetModuleDataLayout(g->mod);
    unsigned long long sz = td ? LLVMABISizeOfType(td, lt) : 0;
    LLVMValueRef bytes = LLVMBuildMul(g->builder, cnt, LLVMConstInt(i64t, sz, 0), "nm.add.b");
    *out = LLVMBuildAdd(g->builder, p, bytes, "nm.add");
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "ByteOffset") &&
      expr->call.args.count == 2 &&
      expr->call.args.items[0] && expr->call.args.items[0]->kind == AST_REF_ARG &&
      expr->call.args.items[1] && expr->call.args.items[1]->kind == AST_REF_ARG) {
    LLVMValueRef p0 = emit_ref_lvalue_ptr(g, expr->call.args.items[0]->ref_arg.expr, locals);
    LLVMValueRef p1 = emit_ref_lvalue_ptr(g, expr->call.args.items[1]->ref_arg.expr, locals);
    LLVMValueRef i0 = p0 ? LLVMBuildPtrToInt(g->builder, p0, i64t, "nm.bo.0") : zero64;
    LLVMValueRef i1 = p1 ? LLVMBuildPtrToInt(g->builder, p1, i64t, "nm.bo.1") : zero64;
    *out = LLVMBuildSub(g->builder, i1, i0, "nm.bo");
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "IncludeBytes") &&
      expr->call.args.count == 1 && expr->call.args.items[0] &&
      expr->call.args.items[0]->kind == AST_STRING_LITERAL) {
    zan_istr_t ps = expr->call.args.items[0]->str_val;
    char full[1024];
    bool is_abs = (ps.len > 0 && (ps.str[0] == '/' || ps.str[0] == '\\')) ||
                  (ps.len > 1 && ((ps.str[0] >= 'A' && ps.str[0] <= 'Z') ||
                                  (ps.str[0] >= 'a' && ps.str[0] <= 'z')) &&
                   ps.str[1] == ':');
    if (is_abs || !g->diag || !g->diag->file_names ||
        expr->loc.file_id >= (uint32_t)g->diag->file_count ||
        !g->diag->file_names[expr->loc.file_id]) {
      snprintf(full, sizeof(full), "%.*s", (int)ps.len, ps.str ? ps.str : "");
    } else {
      const char *cf = g->diag->file_names[expr->loc.file_id];
      const char *slash = strrchr(cf, '/');
      const char *bslash = strrchr(cf, '\\');
      const char *sep = (!slash || (bslash && bslash > slash)) ? bslash : slash;
      if (sep) {
        int dlen = (int)(sep - cf);
        snprintf(full, sizeof(full), "%.*s/%.*s", dlen, cf, (int)ps.len, ps.str ? ps.str : "");
      } else {
        snprintf(full, sizeof(full), "%.*s", (int)ps.len, ps.str ? ps.str : "");
      }
    }
    FILE *fp = fopen(full, "rb");
    long fsz = 0;
    char *buf = NULL;
    if (fp) {
      fseek(fp, 0, SEEK_END);
      fsz = ftell(fp);
      if (fsz < 0) fsz = 0;
      fseek(fp, 0, SEEK_SET);
      if (fsz > 0) {
        buf = (char *)malloc((size_t)fsz);
        if (buf) {
          size_t rd = fread(buf, 1, (size_t)fsz, fp);
          fsz = (long)rd;
        } else {
          fsz = 0;
        }
      }
      fclose(fp);
    }
    LLVMValueRef base = LLVMConstNull(i8ptr);
    if (fsz > 0 && buf) {
      LLVMTypeRef arr_ty = LLVMArrayType(i8, (unsigned)fsz);
      LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, ".zan.embed");
      LLVMSetLinkage(gv, LLVMPrivateLinkage);
      LLVMSetGlobalConstant(gv, 1);
      LLVMSetUnnamedAddr(gv, LLVMGlobalUnnamedAddr);
      LLVMSetAlignment(gv, 1);
      LLVMSetInitializer(gv, LLVMConstStringInContext(g->ctx, buf, (unsigned)fsz, 1));
      base = LLVMConstBitCast(gv, i8ptr);
    }
    if (buf) free(buf);
    LLVMValueRef sv = LLVMGetUndef(g->span_struct_type);
    sv = LLVMBuildInsertValue(g->builder, sv, base, 0, "nm.emb0");
    sv = LLVMBuildInsertValue(g->builder, sv, LLVMConstInt(i64t, (unsigned long long)fsz, 0), 1, "nm.emb1");
    *out = sv;
    return true;
  }
  if ((is_call_to(expr, "NativeMemory", "ReturnAddress") ||
       is_call_to(expr, "NativeMemory", "FrameAddress")) &&
      expr->call.args.count == 1 && expr->call.args.items[0] &&
      expr->call.args.items[0]->kind == AST_INT_LITERAL) {
    bool is_ret = is_call_to(expr, "NativeMemory", "ReturnAddress");
    unsigned lvl = (unsigned)(expr->call.args.items[0]->int_val >= 0
                                  ? expr->call.args.items[0]->int_val
                                  : 0);
    const char *iname = is_ret ? "llvm.returnaddress" : "llvm.frameaddress.p0";
    LLVMTypeRef fty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i32t}, 1, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, iname);
    if (!fn) fn = LLVMAddFunction(g->mod, iname, fty);
    LLVMValueRef lvl_v = LLVMConstInt(i32t, lvl, 0);
    LLVMValueRef ptr_v = zan_call2(g->builder, fty, fn, &lvl_v, 1, is_ret ? "nm.retaddr" : "nm.frmaddr");
    *out = LLVMBuildPtrToInt(g->builder, ptr_v, i64t, "nm.addr.i");
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "AsSpan") &&
      expr->call.args.count == 2 && expr->call.type_args.count == 1) {
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    LLVMValueRef len = nm_arg(g, expr, 1, locals);
    LLVMValueRef neg = zan_icmp(g->builder, LLVMIntSLT, len, zero64, "nm.sp.neg");
    len = LLVMBuildSelect(g->builder, neg, zero64, len, "nm.sp.len");
    LLVMValueRef base = LLVMBuildIntToPtr(g->builder, p, i8ptr, "nm.sp.base");
    LLVMValueRef sv = LLVMGetUndef(g->span_struct_type);
    sv = LLVMBuildInsertValue(g->builder, sv, base, 0, "nm.sp0");
    sv = LLVMBuildInsertValue(g->builder, sv, len, 1, "nm.sp1");
    *out = sv;
    return true;
  }
  if ((is_call_to(expr, "NativeMemory", "PrefetchRead") ||
       is_call_to(expr, "NativeMemory", "PrefetchWrite")) &&
      expr->call.args.count >= 1 && expr->call.args.count <= 2) {
    bool is_write = is_call_to(expr, "NativeMemory", "PrefetchWrite");
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    int loc = 3;
    if (expr->call.args.count == 2 && expr->call.args.items[1] &&
        expr->call.args.items[1]->kind == AST_INT_LITERAL) {
      int64_t lv = expr->call.args.items[1]->int_val;
      loc = lv < 0 ? 0 : (lv > 3 ? 3 : (int)lv);
    }
    LLVMTypeRef pfty = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){i8ptr, i32t, i32t, i32t}, 4, 0);
    LLVMValueRef pffn = LLVMGetNamedFunction(g->mod, "llvm.prefetch.p0");
    if (!pffn)
      pffn = LLVMAddFunction(g->mod, "llvm.prefetch.p0", pfty);
    LLVMValueRef addr = LLVMBuildIntToPtr(g->builder, p, i8ptr, "nm.pf.p");
    zan_call2(g->builder, pfty, pffn,
              (LLVMValueRef[]){addr, LLVMConstInt(i32t, is_write ? 1 : 0, 0),
                               LLVMConstInt(i32t, (unsigned long long)loc, 0),
                               LLVMConstInt(i32t, 1, 0)},
              4, "");
    *out = zero64;
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "Copy") && expr->call.args.count == 3) {
    LLVMValueRef dst = nm_arg(g, expr, 0, locals);
    LLVMValueRef src = nm_arg(g, expr, 1, locals);
    LLVMValueRef n = nm_arg(g, expr, 2, locals);
    LLVMTypeRef i1t = LLVMInt1TypeInContext(g->ctx);
    LLVMTypeRef ty = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){i8ptr, i8ptr, i64t, i1t}, 4, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.memmove.p0.p0.i64");
    if (!fn)
      fn = LLVMAddFunction(g->mod, "llvm.memmove.p0.p0.i64", ty);
    LLVMValueRef pos = zan_icmp(g->builder, LLVMIntSGT, n, zero64, "nm.cp.pos");
    LLVMValueRef fnh = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef run_bb = LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.cp.run");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.cp.done");
    LLVMBuildCondBr(g->builder, pos, run_bb, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, run_bb);
    zan_call2(g->builder, ty, fn,
              (LLVMValueRef[]){nm_addr(g, dst, zero64), nm_addr(g, src, zero64),
                               n, LLVMConstInt(i1t, 0, 0)},
              4, "");
    LLVMBuildBr(g->builder, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    *out = zero64;
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "Copy2D") &&
      expr->call.args.count == 6) {
    LLVMValueRef dst = nm_arg(g, expr, 0, locals);
    LLVMValueRef dst_stride = nm_arg(g, expr, 1, locals);
    LLVMValueRef src = nm_arg(g, expr, 2, locals);
    LLVMValueRef src_stride = nm_arg(g, expr, 3, locals);
    LLVMValueRef row_bytes = nm_arg(g, expr, 4, locals);
    LLVMValueRef height = nm_arg(g, expr, 5, locals);
    LLVMValueRef fn = nm_copy2d_fn(g);
    LLVMTypeRef ty = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){i8ptr, i64t, i8ptr, i64t, i64t, i64t}, 6, 0);
    zan_call2(g->builder, ty, fn,
              (LLVMValueRef[]){nm_addr(g, dst, zero64), dst_stride,
                               nm_addr(g, src, zero64), src_stride, row_bytes,
                               height},
              6, "");
    *out = zero64;
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "Fill") && expr->call.args.count == 3) {
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    LLVMValueRef v = nm_arg(g, expr, 1, locals);
    LLVMValueRef n = nm_arg(g, expr, 2, locals);
    LLVMTypeRef i1t = LLVMInt1TypeInContext(g->ctx);
    LLVMTypeRef ty = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){i8ptr, i8, i64t, i1t}, 4, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.memset.p0.i64");
    if (!fn)
      fn = LLVMAddFunction(g->mod, "llvm.memset.p0.i64", ty);
    LLVMValueRef pos = zan_icmp(g->builder, LLVMIntSGT, n, zero64, "nm.fl.pos");
    LLVMValueRef fnh = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef run_bb = LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.fl.run");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.fl.done");
    LLVMBuildCondBr(g->builder, pos, run_bb, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, run_bb);
    zan_call2(g->builder, ty, fn,
              (LLVMValueRef[]){nm_addr(g, p, zero64),
                               LLVMBuildTrunc(g->builder, v, i8, "nm.b"), n,
                               LLVMConstInt(i1t, 0, 0)},
              4, "");
    LLVMBuildBr(g->builder, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    *out = zero64;
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "Compare") &&
      expr->call.args.count == 3) {
    LLVMValueRef a = nm_arg(g, expr, 0, locals);
    LLVMValueRef b = nm_arg(g, expr, 1, locals);
    LLVMValueRef n = nm_arg(g, expr, 2, locals);
    LLVMTypeRef ty =
        LLVMFunctionType(i32t, (LLVMTypeRef[]){i8ptr, i8ptr, i64t}, 3, 0);
    LLVMValueRef fn = get_libc_fn(g, "memcmp", ty);
    LLVMValueRef r = zan_call2(
        g->builder, ty, fn,
        (LLVMValueRef[]){nm_addr(g, a, zero64), nm_addr(g, b, zero64), n}, 3,
        "nm.cmp");
    *out = LLVMBuildSExt(g->builder, r, i64t, "nm.cmp64");
    return true;
  }

  /* Find(p, off, b, n): 基于 memchr 查找目标字节在窗口内的相对偏移 */
  if (is_call_to(expr, "NativeMemory", "Find") && expr->call.args.count == 4) {
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    LLVMValueRef off = nm_arg(g, expr, 1, locals);
    LLVMValueRef b = nm_arg(g, expr, 2, locals);
    LLVMValueRef n = nm_arg(g, expr, 3, locals);
    LLVMTypeRef ty =
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i32t, i64t}, 3, 0);
    LLVMValueRef fn = get_libc_fn(g, "memchr", ty);
    LLVMValueRef hit = zan_call2(
        g->builder, ty, fn,
        (LLVMValueRef[]){nm_addr(g, p, off),
                         LLVMBuildTrunc(g->builder, b, i32t, "nm.find.b"), n},
        3, "nm.find");
    LLVMValueRef found = zan_icmp(g->builder, LLVMIntNE, hit,
                                  LLVMConstNull(i8ptr), "nm.find.ok");
    LLVMValueRef at = LLVMBuildPtrToInt(g->builder, hit, i64t, "nm.find.a");
    LLVMValueRef rel = LLVMBuildSub(g->builder, at, p, "nm.find.rel");
    *out = LLVMBuildSelect(g->builder, found, rel,
                           LLVMConstInt(i64t, (uint64_t)-1, 1), "nm.find.r");
    return true;
  }

  /* ScanNotAnyOf: 基于 strspn 扫描首个不属于白名单集合的字符偏移 */
  if (is_call_to(expr, "NativeMemory", "ScanNotAnyOf") &&
      expr->call.args.count == 4) {
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    LLVMValueRef off = nm_arg(g, expr, 1, locals);
    zan_ast_node_t *set_ast = expr->call.args.items[2];
    LLVMValueRef setv = emit_expr(g, set_ast, locals);
    LLVMValueRef n = nm_arg(g, expr, 3, locals);
    LLVMTypeRef ty =
        LLVMFunctionType(i64t, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0);
    LLVMValueRef fn = get_libc_fn(g, "strspn", ty);
    LLVMValueRef start = nm_addr(g, p, off);
    LLVMValueRef run = zan_call2(
        g->builder, ty, fn, (LLVMValueRef[]){start, setv}, 2, "nm.scanany");
    LLVMValueRef over =
        zan_icmp(g->builder, LLVMIntUGT, run, n, "nm.scanany.o");
    *out = LLVMBuildSelect(g->builder, over, n, run, "nm.scanany.r");
    emit_release_owned_call_temp(g, set_ast, setv, locals);
    return true;
  }

  /* FindNotAnyOf: 基于 strcspn 查找首个命中黑名单字符集的偏移 */
  if (is_call_to(expr, "NativeMemory", "FindNotAnyOf") &&
      expr->call.args.count == 4) {
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    LLVMValueRef off = nm_arg(g, expr, 1, locals);
    zan_ast_node_t *set_ast = expr->call.args.items[2];
    LLVMValueRef setv = emit_expr(g, set_ast, locals);
    LLVMValueRef n = nm_arg(g, expr, 3, locals);
    LLVMTypeRef ty =
        LLVMFunctionType(i64t, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0);
    LLVMValueRef fn = get_libc_fn(g, "strcspn", ty);
    LLVMValueRef start = nm_addr(g, p, off);
    LLVMValueRef run = zan_call2(g->builder, ty, fn,
                                 (LLVMValueRef[]){start, setv}, 2, "nm.fna");
    /* 扫描至窗口末尾未发现匹配项时返回 -1 哨兵值 */
    LLVMValueRef clean = zan_icmp(g->builder, LLVMIntUGE, run, n, "nm.fna.c");
    LLVMValueRef rel =
        LLVMBuildSelect(g->builder, clean, LLVMConstInt(i64t, (uint64_t)-1, 1),
                        run, "nm.fna.r");
    *out = rel;
    emit_release_owned_call_temp(g, set_ast, setv, locals);
    return true;
  }

  /* Load64: 从指定偏移读取 8 字节未对齐标量 */
  if (is_call_to(expr, "NativeMemory", "Load64") &&
      expr->call.args.count == 2) {
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    LLVMValueRef off = nm_arg(g, expr, 1, locals);
    LLVMValueRef r =
        LLVMBuildLoad2(g->builder, i64t, nm_addr(g, p, off), "nm.ld64");
    LLVMSetAlignment(r, 1);
    *out = r;
    return true;
  }

  /* AsI32 / AsF32 / AsI64 / AsF64: 浮点与整数标量之间的零开销二进制重解释位转换
   */
  if (is_call_to(expr, "NativeMemory", "AsI32") && expr->call.args.count == 1) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef v = emit_expr(g, expr->call.args.items[0], locals);
    *out = LLVMBuildBitCast(g->builder, v, i32t, "nm.asi32");
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "AsF32") && expr->call.args.count == 1) {
    LLVMTypeRef fltt = LLVMFloatTypeInContext(g->ctx);
    LLVMValueRef v =
        coerce_int_to(g, emit_expr(g, expr->call.args.items[0], locals), i32t);
    *out = LLVMBuildBitCast(g->builder, v, fltt, "nm.asf32");
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "AsI64") && expr->call.args.count == 1) {
    LLVMValueRef v = nm_arg(g, expr, 0, locals);
    *out = LLVMBuildBitCast(g->builder, v, i64t, "nm.asi64");
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "AsF64") && expr->call.args.count == 1) {
    LLVMTypeRef dblt = LLVMDoubleTypeInContext(g->ctx);
    LLVMValueRef v = nm_arg(g, expr, 0, locals);
    *out = LLVMBuildBitCast(g->builder, v, dblt, "nm.asf64");
    return true;
  }

  /* ScanNotByte: 扫描首个不等于指定字节 b 的位置偏移 */
  if (is_call_to(expr, "NativeMemory", "ScanNotByte") &&
      expr->call.args.count == 4) {
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    LLVMValueRef off = nm_arg(g, expr, 1, locals);
    LLVMValueRef b = nm_arg(g, expr, 2, locals);
    LLVMValueRef n = nm_arg(g, expr, 3, locals);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef arr2 = LLVMArrayType(i8, 2);
    LLVMValueRef accept;
    if (LLVMIsConstant(b)) {
      /* 构建双字节 NUL 终止字符集用于快速字符匹配 */
      accept = LLVMAddGlobal(g->mod, arr2, "nm.scan.accept");
      LLVMSetLinkage(accept, LLVMPrivateLinkage);
      LLVMSetInitializer(
          accept,
          LLVMConstArray(
              i8,
              (LLVMValueRef[]){LLVMBuildTrunc(g->builder, b, i8, "nm.scan.b"),
                               LLVMConstInt(i8, 0, 0)},
              2));
    } else {
      /* 静态常量全局变量初始化约束检查 */
      accept = emit_entry_alloca(g, arr2, "nm.scan.acc");
      LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
      LLVMValueRef b8 = LLVMBuildTrunc(g->builder, b, i8, "nm.scan.b");
      LLVMValueRef e0 = LLVMBuildGEP2(
          g->builder, i8, accept,
          (LLVMValueRef[]){LLVMConstInt(i32t, 0, 0), LLVMConstInt(i32t, 0, 0)},
          2, "nm.scan.a0");
      LLVMBuildStore(g->builder, b8, e0);
      LLVMValueRef e1 = LLVMBuildGEP2(
          g->builder, i8, accept,
          (LLVMValueRef[]){LLVMConstInt(i32t, 0, 0), LLVMConstInt(i32t, 1, 0)},
          2, "nm.scan.a1");
      LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), e1);
    }
    LLVMTypeRef ty =
        LLVMFunctionType(i64t, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0);
    LLVMValueRef fn = get_libc_fn(g, "strspn", ty);
    LLVMValueRef start = nm_addr(g, p, off);
    LLVMValueRef run = zan_call2(g->builder, ty, fn,
                                 (LLVMValueRef[]){start, accept}, 2, "nm.scan");
    LLVMValueRef over = zan_icmp(g->builder, LLVMIntUGT, run, n, "nm.scan.o");
    *out = LLVMBuildSelect(g->builder, over, n, run, "nm.scan.r");
    return true;
  }

  if (is_call_to(expr, "NativeMemory", "GetString") &&
      expr->call.args.count == 3) {
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    LLVMValueRef off = nm_arg(g, expr, 1, locals);
    LLVMValueRef len = nm_arg(g, expr, 2, locals);
    len = LLVMBuildSelect(
        g->builder, zan_icmp(g->builder, LLVMIntSLT, len, zero64, "nm.gs.neg"),
        zero64, len, "nm.gs.len");
    LLVMValueRef one = LLVMConstInt(i64t, 1, 0);
    LLVMValueRef total = zan_add(g->builder, len, one, "nm.gs.sz");
    LLVMValueRef s = emit_string_alloc_rc(g, total);
    LLVMTypeRef ty =
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr, i64t}, 3, 0);
    LLVMValueRef fn = get_libc_fn(g, "memcpy", ty);
    zan_call2(g->builder, ty, fn, (LLVMValueRef[]){s, nm_addr(g, p, off), len},
              3, "");
    LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, s, &len, 1, "nm.gs.end");
    zan_store_fit(g, LLVMConstInt(i8, 0, 0), endp);
    /* 已知确切长度的字符串直接构建并标记长度 */
    emit_string_len_set(g, s, len);
    *out = s;
    return true;
  }
  if (is_call_to(expr, "NativeMemory", "PutString") &&
      expr->call.args.count == 4) {
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    LLVMValueRef off = nm_arg(g, expr, 1, locals);
    zan_ast_node_t *s_ast = expr->call.args.items[2];
    LLVMValueRef s = emit_expr(g, s_ast, locals);
    LLVMValueRef len = nm_arg(g, expr, 3, locals);
    /* 长度合法性校验：拒绝负数防止越界内存拷贝 */
    len = LLVMBuildSelect(
        g->builder, zan_icmp(g->builder, LLVMIntSLT, len, zero64, "nm.ps.neg"),
        zero64, len, "nm.ps.len");
    LLVMTypeRef ty =
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr, i64t}, 3, 0);
    LLVMValueRef fn = get_libc_fn(g, "memcpy", ty);
    zan_call2(g->builder, ty, fn, (LLVMValueRef[]){nm_addr(g, p, off), s, len},
              3, "");
    emit_release_owned_call_temp(g, s_ast, s, locals);
    *out = zero64;
    return true;
  }
/* AtomicInt 核心操作一律内联发射为单指令硬件原子操作，消除 C ABI 跨语言调用开销
 */
#define IS_ATOMIC_INT(m)                                                       \
  (is_call_to(expr, "AtomicInt", m) ||                                         \
   (expr->kind == AST_CALL && expr->call.callee &&                             \
    expr->call.callee->kind == AST_IDENTIFIER &&                               \
    (int)expr->call.callee->ident.name.len == (int)strlen(m) &&                \
    memcmp(expr->call.callee->ident.name.str, m,                               \
           (size_t)expr->call.callee->ident.name.len) == 0))

  if (IS_ATOMIC_INT("NativeLoad") && expr->call.args.count == 1) {
    LLVMValueRef handle = nm_arg(g, expr, 0, locals);
    LLVMValueRef ptr = LLVMBuildIntToPtr(g->builder, handle,
                                         LLVMPointerType(i64t, 0), "atom.ptr");
    LLVMValueRef val = LLVMBuildLoad2(g->builder, i64t, ptr, "atom.load");
    LLVMSetOrdering(val, LLVMAtomicOrderingSequentiallyConsistent);
    *out = val;
    return true;
  }
  if (IS_ATOMIC_INT("NativeStore") && expr->call.args.count == 2) {
    LLVMValueRef handle = nm_arg(g, expr, 0, locals);
    LLVMValueRef val = nm_arg(g, expr, 1, locals);
    LLVMValueRef ptr = LLVMBuildIntToPtr(g->builder, handle,
                                         LLVMPointerType(i64t, 0), "atom.ptr");
    LLVMValueRef st = LLVMBuildStore(g->builder, val, ptr);
    LLVMSetOrdering(st, LLVMAtomicOrderingSequentiallyConsistent);
    *out = zero64;
    return true;
  }
  if (IS_ATOMIC_INT("NativeAdd") && expr->call.args.count == 2) {
    LLVMValueRef handle = nm_arg(g, expr, 0, locals);
    LLVMValueRef delta = nm_arg(g, expr, 1, locals);
    LLVMValueRef ptr = LLVMBuildIntToPtr(g->builder, handle,
                                         LLVMPointerType(i64t, 0), "atom.ptr");
    LLVMValueRef old =
        LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpAdd, ptr, delta,
                           LLVMAtomicOrderingSequentiallyConsistent, false);
    *out = LLVMBuildAdd(g->builder, old, delta, "atom.add.res");
    return true;
  }
  if (IS_ATOMIC_INT("NativeExchange") && expr->call.args.count == 2) {
    LLVMValueRef handle = nm_arg(g, expr, 0, locals);
    LLVMValueRef val = nm_arg(g, expr, 1, locals);
    LLVMValueRef ptr = LLVMBuildIntToPtr(g->builder, handle,
                                         LLVMPointerType(i64t, 0), "atom.ptr");
    *out = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpXchg, ptr, val,
                              LLVMAtomicOrderingSequentiallyConsistent, false);
    return true;
  }
  if (IS_ATOMIC_INT("NativeCompareExchange") && expr->call.args.count == 3) {
    LLVMValueRef handle = nm_arg(g, expr, 0, locals);
    LLVMValueRef expected = nm_arg(g, expr, 1, locals);
    LLVMValueRef desired = nm_arg(g, expr, 2, locals);
    LLVMValueRef ptr = LLVMBuildIntToPtr(g->builder, handle,
                                         LLVMPointerType(i64t, 0), "atom.ptr");
    LLVMValueRef pair =
        LLVMBuildAtomicCmpXchg(g->builder, ptr, expected, desired,
                               LLVMAtomicOrderingSequentiallyConsistent,
                               LLVMAtomicOrderingSequentiallyConsistent, false);
    *out = LLVMBuildExtractValue(g->builder, pair, 0, "atom.cmpxchg.old");
    return true;
  }
#undef IS_ATOMIC_INT
  return false;
}
