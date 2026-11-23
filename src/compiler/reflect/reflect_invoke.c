static LLVMValueRef refl_mfind_fn(zan_irgen_t *g) {
  if (g->fn_refl_mfind)
    return g->fn_refl_mfind;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i32ptr = LLVMPointerType(i32, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i64}, 3, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_mfind", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_mfind = fn;
  LLVMValueRef get_pfx = refl_const_string(g, "get_", 4);
  LLVMValueRef set_pfx = refl_const_string(g, "set_", 4);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef dispatch =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "dispatch");
  LLVMBasicBlockRef bhead = LLVMAppendBasicBlockInContext(g->ctx, fn, "bhead");
  LLVMBasicBlockRef bbody = LLVMAppendBasicBlockInContext(g->ctx, fn, "bbody");
  LLVMBasicBlockRef bpfx = LLVMAppendBasicBlockInContext(g->ctx, fn, "bpfx");
  LLVMBasicBlockRef bpfx_next =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "bpfx.next");
  LLVMBasicBlockRef bcmp_tail =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "bcmp.tail");
  LLVMBasicBlockRef bdir = LLVMAppendBasicBlockInContext(g->ctx, fn, "bdir");
  LLVMBasicBlockRef bleft = LLVMAppendBasicBlockInContext(g->ctx, fn, "bleft");
  LLVMBasicBlockRef bright =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "bright");
  LLVMBasicBlockRef bfound =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "bfound");
  LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(g->ctx, fn, "head");
  LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
  LLVMBasicBlockRef cmp = LLVMAppendBasicBlockInContext(g->ctx, fn, "cmp");
  LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
  LLVMBasicBlockRef found = LLVMAppendBasicBlockInContext(g->ctx, fn, "found");
  LLVMBasicBlockRef miss = LLVMAppendBasicBlockInContext(g->ctx, fn, "miss");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef want = LLVMGetParam(fn, 1);
  LLVMValueRef flags = LLVMGetParam(fn, 2);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef methods = refl_hdr_ptr(g, ti, ZAN_REFL_METHODS_OFF, "refl.mt");
  LLVMValueRef count = refl_hdr_i64(g, ti, ZAN_REFL_MCOUNT_OFF, "refl.mc");
  LLVMValueRef has_m = LLVMBuildAnd(
      g->builder, LLVMBuildIsNotNull(g->builder, methods, "refl.hasmt"),
      LLVMBuildICmp(g->builder, LLVMIntSGT, count, LLVMConstInt(i64, 0, 0),
                    "refl.mcpos"),
      "refl.hasm");
  LLVMBuildCondBr(g->builder, has_m, dispatch, miss);

  LLVMPositionBuilderAtEnd(g->builder, dispatch);
  LLVMValueRef msort = refl_hdr_ptr(g, ti, ZAN_REFL_MSORT_OFF, "refl.msort");
  LLVMValueRef msort32 =
      LLVMBuildBitCast(g->builder, msort, i32ptr, "refl.ms32");
  LLVMValueRef acc = LLVMBuildICmp(
      g->builder, LLVMIntNE,
      LLVMBuildAnd(g->builder, flags,
                   LLVMConstInt(i64, ZAN_REFL_MF_PROPGET | ZAN_REFL_MF_PROPSET,
                                0),
                   "refl.accand"),
      LLVMConstInt(i64, 0, 0), "refl.isacc");
  LLVMValueRef is_get = LLVMBuildICmp(
      g->builder, LLVMIntNE,
      LLVMBuildAnd(g->builder, flags, LLVMConstInt(i64, ZAN_REFL_MF_PROPGET, 0),
                   "refl.gand"),
      LLVMConstInt(i64, 0, 0), "refl.isget");
  LLVMValueRef pfx =
      LLVMBuildSelect(g->builder, is_get, get_pfx, set_pfx, "refl.pfx");
  LLVMValueRef bhi0 =
      LLVMBuildSub(g->builder, count, LLVMConstInt(i64, 1, 0), "refl.hi0");
  LLVMBuildCondBr(g->builder,
                  LLVMBuildIsNotNull(g->builder, msort, "refl.hasbs"), bhead,
                  head);

  /* O(log M) binary search over msort[]; handles "get_"/"set_" prefixes
   * without heap allocation. */
  LLVMPositionBuilderAtEnd(g->builder, bhead);
  LLVMValueRef blo = LLVMBuildPhi(g->builder, i64, "refl.lo");
  LLVMValueRef bhi = LLVMBuildPhi(g->builder, i64, "refl.hi");
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntSLE, blo, bhi, "refl.bmore"), bbody,
      miss);

  LLVMPositionBuilderAtEnd(g->builder, bbody);
  LLVMValueRef bdiff = LLVMBuildSub(g->builder, bhi, blo, "refl.bdiff");
  LLVMValueRef bhalf =
      LLVMBuildLShr(g->builder, bdiff, LLVMConstInt(i64, 1, 0), "refl.bhalf");
  LLVMValueRef bmid = LLVMBuildAdd(g->builder, blo, bhalf, "refl.bmid");
  LLVMValueRef bsp =
      LLVMBuildGEP2(g->builder, i32, msort32, &bmid, 1, "refl.bsp");
  LLVMValueRef lidx = LLVMBuildZExt(
      g->builder, LLVMBuildLoad2(g->builder, i32, bsp, "refl.bsi"), i64,
      "refl.lidx");
  LLVMValueRef brec = refl_member_row(g, methods, lidx, true);
  LLVMValueRef bmname = refl_method_ptr(g, methods, brec, ZAN_REFL_MO_NAME);
  LLVMValueRef bmflags = refl_method_int(g, brec, ZAN_REFL_MO_FLAGS);
  LLVMBuildCondBr(g->builder, acc, bpfx, bcmp_tail);

  LLVMPositionBuilderAtEnd(g->builder, bpfx);
  LLVMValueRef pi = LLVMBuildPhi(g->builder, i64, "refl.pi");
  LLVMValueRef c0p = LLVMBuildGEP2(g->builder, i8, bmname, &pi, 1, "refl.c0p");
  LLVMValueRef c1p = LLVMBuildGEP2(g->builder, i8, pfx, &pi, 1, "refl.c1p");
  LLVMValueRef c0 = LLVMBuildZExt(
      g->builder, LLVMBuildLoad2(g->builder, i8, c0p, "refl.c0b"), i32,
      "refl.c0");
  LLVMValueRef c1 = LLVMBuildZExt(
      g->builder, LLVMBuildLoad2(g->builder, i8, c1p, "refl.c1b"), i32,
      "refl.c1");
  LLVMValueRef pdiff = LLVMBuildSub(g->builder, c0, c1, "refl.pdiff");
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntNE, pdiff,
                                LLVMConstInt(i32, 0, 0), "refl.pne"),
                  bdir, bpfx_next);

  LLVMPositionBuilderAtEnd(g->builder, bpfx_next);
  LLVMValueRef pi_n =
      LLVMBuildAdd(g->builder, pi, LLVMConstInt(i64, 1, 0), "refl.pi.n");
  LLVMValueRef four_c = LLVMConstInt(i64, 4, 0);
  LLVMValueRef bmname4 =
      LLVMBuildGEP2(g->builder, i8, bmname, &four_c, 1, "refl.mn4");
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntULT, pi_n, four_c, "refl.pmore"), bpfx,
      bcmp_tail);
  LLVMAddIncoming(pi, (LLVMValueRef[]){LLVMConstInt(i64, 0, 0), pi_n},
                  (LLVMBasicBlockRef[]){bbody, bpfx_next}, 2);

  LLVMPositionBuilderAtEnd(g->builder, bcmp_tail);
  LLVMValueRef bcand = LLVMBuildPhi(g->builder, i8ptr, "refl.bcand");
  LLVMAddIncoming(bcand, (LLVMValueRef[]){bmname, bmname4},
                  (LLVMBasicBlockRef[]){bbody, bpfx_next}, 2);
  LLVMValueRef bc = zan_call2(
      g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
      g->fn_strcmp, (LLVMValueRef[]){bcand, want}, 2, "refl.bcmp");
  LLVMValueRef bhit = LLVMBuildICmp(g->builder, LLVMIntEQ, bc,
                                    LLVMConstInt(i32, 0, 0), "refl.bhit");
  LLVMBasicBlockRef bhitchk =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "bhitchk");
  LLVMBuildCondBr(g->builder, bhit, bhitchk, bdir);

  LLVMPositionBuilderAtEnd(g->builder, bhitchk);
  LLVMValueRef bflag_ok = LLVMBuildICmp(
      g->builder, LLVMIntEQ,
      LLVMBuildAnd(g->builder, bmflags, flags, "refl.bfand"), flags,
      "refl.bfok");
  LLVMBuildCondBr(g->builder, bflag_ok, bfound, miss);

  LLVMPositionBuilderAtEnd(g->builder, bdir);
  LLVMValueRef cmpval = LLVMBuildPhi(g->builder, i32, "refl.cmpval");
  LLVMAddIncoming(cmpval, (LLVMValueRef[]){pdiff, bc},
                  (LLVMBasicBlockRef[]){bpfx, bcmp_tail}, 2);
  LLVMValueRef blt = LLVMBuildICmp(g->builder, LLVMIntSLT, cmpval,
                                   LLVMConstInt(i32, 0, 0), "refl.blt");
  LLVMBuildCondBr(g->builder, blt, bright, bleft);

  LLVMPositionBuilderAtEnd(g->builder, bright);
  LLVMValueRef blo_n =
      LLVMBuildAdd(g->builder, bmid, LLVMConstInt(i64, 1, 0), "refl.lo.n");
  LLVMBuildBr(g->builder, bhead);

  LLVMPositionBuilderAtEnd(g->builder, bleft);
  LLVMValueRef bhi_n =
      LLVMBuildSub(g->builder, bmid, LLVMConstInt(i64, 1, 0), "refl.hi.n");
  LLVMBuildBr(g->builder, bhead);

  LLVMAddIncoming(
      blo, (LLVMValueRef[]){LLVMConstInt(i64, 0, 0), blo_n, blo},
      (LLVMBasicBlockRef[]){dispatch, bright, bleft}, 3);
  LLVMAddIncoming(
      bhi, (LLVMValueRef[]){bhi0, bhi, bhi_n},
      (LLVMBasicBlockRef[]){dispatch, bright, bleft}, 3);

  LLVMPositionBuilderAtEnd(g->builder, bfound);
  LLVMBuildRet(g->builder, lidx);

  /* < 4 methods linear fast path */
  LLVMPositionBuilderAtEnd(g->builder, head);
  LLVMValueRef i = LLVMBuildPhi(g->builder, i64, "refl.mi");
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntSLT, i, count, "refl.more"),
                  body, miss);

  LLVMPositionBuilderAtEnd(g->builder, body);
  LLVMValueRef rec = refl_member_row(g, methods, i, true);
  LLVMValueRef mname = refl_method_ptr(g, methods, rec, ZAN_REFL_MO_NAME);
  LLVMValueRef mflags = refl_method_int(g, rec, ZAN_REFL_MO_FLAGS);
  LLVMValueRef four = LLVMBuildSelect(g->builder, acc, LLVMConstInt(i64, 4, 0),
                                      LLVMConstInt(i64, 0, 0), "refl.skip");
  LLVMValueRef cand =
      LLVMBuildGEP2(g->builder, i8, mname, &four, 1, "refl.mn2");
  LLVMValueRef flag_ok = LLVMBuildICmp(
      g->builder, LLVMIntEQ,
      LLVMBuildAnd(g->builder, mflags, flags, "refl.fand"), flags,
      "refl.fok");
  LLVMBuildCondBr(g->builder, flag_ok, cmp, next);

  LLVMPositionBuilderAtEnd(g->builder, cmp);
  LLVMValueRef c = zan_call2(
      g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
      g->fn_strcmp, (LLVMValueRef[]){cand, want}, 2, "refl.mcmp");
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntEQ, c,
                                LLVMConstInt(i32, 0, 0), "refl.mhit"),
                  found, next);

  LLVMPositionBuilderAtEnd(g->builder, next);
  LLVMValueRef inext =
      LLVMBuildAdd(g->builder, i, LLVMConstInt(i64, 1, 0), "refl.mi.n");
  LLVMBuildBr(g->builder, head);
  LLVMAddIncoming(i, (LLVMValueRef[]){LLVMConstInt(i64, 0, 0), inext},
                  (LLVMBasicBlockRef[]){dispatch, next}, 2);

  LLVMPositionBuilderAtEnd(g->builder, found);
  LLVMBuildRet(g->builder, i);
  LLVMPositionBuilderAtEnd(g->builder, miss);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, (unsigned long long)-1LL, 1));

  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_mstr_fn(zan_irgen_t *g) {
  if (g->fn_refl_mstr)
    return g->fn_refl_mstr;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i64, i64, i64, i64}, 5, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_mstr", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_mstr = fn;
  LLVMValueRef empty = refl_empty_string(g);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef table = LLVMAppendBasicBlockInContext(g->ctx, fn, "table");
  LLVMBasicBlockRef decl =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "declattrs");
  LLVMBasicBlockRef dload =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "declload");
  LLVMBasicBlockRef have = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
  LLVMBasicBlockRef par = LLVMAppendBasicBlockInContext(g->ctx, fn, "param");
  LLVMBasicBlockRef own = LLVMAppendBasicBlockInContext(g->ctx, fn, "own");
  LLVMBasicBlockRef bad = LLVMAppendBasicBlockInContext(g->ctx, fn, "bad");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef tbl = LLVMGetParam(fn, 1);
  LLVMValueRef idx = LLVMGetParam(fn, 2);
  LLVMValueRef which = LLVMGetParam(fn, 3);
  LLVMValueRef k = LLVMGetParam(fn, 4);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMBuildCondBr(g->builder, LLVMBuildIsNull(g->builder, ti, "refl.noti"), bad,
                  table);

  LLVMPositionBuilderAtEnd(g->builder, table);
  LLVMValueRef arr = NULL, count = NULL;
  refl_table_of(g, ti, tbl, &arr, &count);
  LLVMValueRef ok = LLVMBuildAnd(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0),
                    "refl.ge"),
      LLVMBuildICmp(g->builder, LLVMIntSLT, idx, count, "refl.lt"), "refl.ok");
  ok = LLVMBuildAnd(g->builder, ok,
                    LLVMBuildIsNotNull(g->builder, arr, "refl.hastbl"),
                    "refl.ok2");
  LLVMBuildCondBr(g->builder, ok, have, bad);

  LLVMPositionBuilderAtEnd(g->builder, have);
  LLVMValueRef rec = refl_member_row(g, arr, idx, true);
  LLVMValueRef select = LLVMBuildSwitch(g->builder, which, own, 2);
  LLVMAddCase(select, LLVMConstInt(i64, ZAN_REFL_MW_PARAM, 0), par);
  LLVMAddCase(select, LLVMConstInt(i64, ZAN_REFL_MW_DECLATTRS, 0), decl);

  LLVMPositionBuilderAtEnd(g->builder, decl);
  LLVMValueRef flags = refl_method_int(g, rec, ZAN_REFL_MO_FLAGS);
  LLVMValueRef attr_flag = LLVMBuildAnd(
      g->builder, flags, LLVMConstInt(i64, ZAN_REFL_MF_DECLATTRS, 0),
      "refl.attrflag");
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntNE, attr_flag,
                                LLVMConstInt(i64, 0, 0), "refl.hasattrs"),
                  dload, bad);

  LLVMPositionBuilderAtEnd(g->builder, dload);
  LLVMValueRef attr_idx = LLVMBuildAdd(g->builder, count, idx, "refl.attridx");
  LLVMBuildRet(g->builder,
               refl_payload_ptr(g, arr, refl_table_id(g, arr, attr_idx)));

  LLVMPositionBuilderAtEnd(g->builder, own);
  LLVMValueRef sel = LLVMBuildSelect(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                    LLVMConstInt(i64, ZAN_REFL_MW_NAME, 0), "refl.isn"),
      LLVMConstInt(i64, 0, 0),
      LLVMBuildSelect(
          g->builder,
          LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                        LLVMConstInt(i64, ZAN_REFL_MW_RET, 0), "refl.isret"),
          LLVMConstInt(i64, 1, 0), LLVMConstInt(i64, 4, 0), "refl.sel2"),
      "refl.sel");
  LLVMValueRef sv = refl_member_ptr(g, arr, rec, sel);
  LLVMBuildRet(g->builder,
               LLVMBuildSelect(g->builder,
                               LLVMBuildIsNull(g->builder, sv, "refl.snull"),
                               empty, sv, "refl.s"));

  LLVMPositionBuilderAtEnd(g->builder, par);
  LLVMValueRef pc = refl_method_int(g, rec, ZAN_REFL_MO_PCOUNT);
  LLVMValueRef ptypes = refl_method_ptr(g, arr, rec, ZAN_REFL_MO_PTYPES);
  LLVMValueRef pok = LLVMBuildAnd(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntSGE, k, LLVMConstInt(i64, 0, 0),
                    "refl.kge"),
      LLVMBuildICmp(g->builder, LLVMIntSLT, k, pc, "refl.klt"), "refl.pok");
  pok = LLVMBuildAnd(g->builder, pok,
                     LLVMBuildIsNotNull(g->builder, ptypes, "refl.haspt"),
                     "refl.pok2");
  LLVMBasicBlockRef pld = LLVMAppendBasicBlockInContext(g->ctx, fn, "pload");
  LLVMBuildCondBr(g->builder, pok, pld, bad);

  LLVMPositionBuilderAtEnd(g->builder, pld);
  LLVMValueRef pp =
      LLVMBuildGEP2(g->builder, i8ptr,
                    LLVMBuildBitCast(g->builder, ptypes,
                                     LLVMPointerType(i8ptr, 0), "refl.ptc"),
                    &k, 1, "refl.pe");
  LLVMValueRef pv = LLVMBuildLoad2(g->builder, i8ptr, pp, "refl.pv");
  LLVMBuildRet(g->builder,
               LLVMBuildSelect(g->builder,
                               LLVMBuildIsNull(g->builder, pv, "refl.pnull"),
                               empty, pv, "refl.p"));

  LLVMPositionBuilderAtEnd(g->builder, bad);
  LLVMBuildRet(g->builder, empty);

  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_mi64_fn(zan_irgen_t *g) {
  if (g->fn_refl_mi64)
    return g->fn_refl_mi64;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i64, i64, i64}, 4, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_mi64", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_mi64 = fn;

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef cnt = LLVMAppendBasicBlockInContext(g->ctx, fn, "count");
  LLVMBasicBlockRef one = LLVMAppendBasicBlockInContext(g->ctx, fn, "one");
  LLVMBasicBlockRef zero = LLVMAppendBasicBlockInContext(g->ctx, fn, "zero");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef tbl = LLVMGetParam(fn, 1);
  LLVMValueRef idx = LLVMGetParam(fn, 2);
  LLVMValueRef which = LLVMGetParam(fn, 3);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef arr = NULL, count = NULL;
  refl_table_of(g, ti, tbl, &arr, &count);
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                                LLVMConstInt(i64, ZAN_REFL_MI_COUNT, 0),
                                "refl.iscnt"),
                  cnt, one);

  LLVMPositionBuilderAtEnd(g->builder, cnt);
  LLVMBuildRet(g->builder, count);

  LLVMPositionBuilderAtEnd(g->builder, one);
  LLVMValueRef ok = LLVMBuildAnd(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0),
                    "refl.ge"),
      LLVMBuildICmp(g->builder, LLVMIntSLT, idx, count, "refl.lt"), "refl.ok");
  ok = LLVMBuildAnd(g->builder, ok,
                    LLVMBuildIsNotNull(g->builder, arr, "refl.hastbl"),
                    "refl.ok2");
  LLVMBasicBlockRef ld = LLVMAppendBasicBlockInContext(g->ctx, fn, "load");
  LLVMBuildCondBr(g->builder, ok, ld, zero);

  LLVMPositionBuilderAtEnd(g->builder, ld);
  LLVMValueRef rec = refl_member_row(g, arr, idx, true);
  LLVMValueRef pc = refl_method_int(g, rec, ZAN_REFL_MO_PCOUNT);
  LLVMValueRef fl = refl_method_int(g, rec, ZAN_REFL_MO_FLAGS);
  LLVMValueRef rk = refl_method_int(g, rec, ZAN_REFL_MO_RETKIND);
  LLVMValueRef v = LLVMBuildSelect(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                    LLVMConstInt(i64, ZAN_REFL_MI_PCOUNT, 0), "refl.ispc"),
      pc,
      LLVMBuildSelect(g->builder,
                      LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                                    LLVMConstInt(i64, ZAN_REFL_MI_FLAGS, 0),
                                    "refl.isfl"),
                      fl, rk, "refl.v1"),
      "refl.v");
  LLVMBuildRet(g->builder, v);

  LLVMPositionBuilderAtEnd(g->builder, zero);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_invoke_fn(zan_irgen_t *g) {
  if (g->fn_refl_invoke)
    return g->fn_refl_invoke;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
  LLVMTypeRef vd = LLVMVoidTypeInContext(g->ctx);
  LLVMTypeRef fn_ty = LLVMFunctionType(
      i64, (LLVMTypeRef[]){i8ptr, i64, i64, i8ptr, i64ptr, i64ptr}, 6, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_invoke", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_invoke = fn;

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef have = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
  LLVMBasicBlockRef call = LLVMAppendBasicBlockInContext(g->ctx, fn, "call");
  LLVMBasicBlockRef miss = LLVMAppendBasicBlockInContext(g->ctx, fn, "miss");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef tbl = LLVMGetParam(fn, 1);
  LLVMValueRef idx = LLVMGetParam(fn, 2);
  LLVMValueRef obj = LLVMGetParam(fn, 3);
  LLVMValueRef args = LLVMGetParam(fn, 4);
  LLVMValueRef kindout = LLVMGetParam(fn, 5);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef arr = NULL, count = NULL;
  refl_table_of(g, ti, tbl, &arr, &count);
  LLVMValueRef ok = LLVMBuildAnd(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0),
                    "refl.ge"),
      LLVMBuildICmp(g->builder, LLVMIntSLT, idx, count, "refl.lt"), "refl.ok");
  ok = LLVMBuildAnd(g->builder, ok,
                    LLVMBuildIsNotNull(g->builder, arr, "refl.hastbl"),
                    "refl.ok2");
  LLVMBuildCondBr(g->builder, ok, have, miss);

  LLVMPositionBuilderAtEnd(g->builder, have);
  LLVMValueRef rec = refl_member_row(g, arr, idx, true);
  LLVMValueRef thunk = refl_method_ptr(g, arr, rec, ZAN_REFL_MO_THUNK);
  LLVMValueRef rk = refl_method_int(g, rec, ZAN_REFL_MO_RETKIND);
  LLVMBuildCondBr(g->builder,
                  LLVMBuildIsNull(g->builder, thunk, "refl.nothunk"), miss,
                  call);

  LLVMPositionBuilderAtEnd(g->builder, call);
  LLVMValueRef ret = LLVMBuildAlloca(g->builder, i64, "refl.ret");
  LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), ret);
  LLVMTypeRef th_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i64ptr, i64ptr}, 3, 0);
  LLVMValueRef th_ok = zan_call2(
      g->builder, th_ty,
      LLVMBuildBitCast(g->builder, thunk, LLVMPointerType(th_ty, 0),
                       "refl.thf"),
      (LLVMValueRef[]){obj, args, ret}, 3, "refl.thok");
  LLVMValueRef ok_bool = LLVMBuildICmp(g->builder, LLVMIntNE, th_ok,
                                       LLVMConstInt(i64, 0, 0), "refl.thokb");
  LLVMBuildStore(
      g->builder,
      LLVMBuildSelect(
          g->builder, ok_bool,
          LLVMBuildAdd(g->builder, rk, LLVMConstInt(i64, 1, 0), "refl.k1"),
          LLVMConstInt(i64, 0, 0), "refl.ksel"),
      kindout);
  LLVMBuildRet(
      g->builder,
      LLVMBuildSelect(g->builder, ok_bool,
                      LLVMBuildLoad2(g->builder, i64, ret, "refl.rv"),
                      LLVMConstInt(i64, 0, 0), "refl.rvs"));

  LLVMPositionBuilderAtEnd(g->builder, miss);
  LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), kindout);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_pget_fn(zan_irgen_t *g) {
  if (g->fn_refl_pget)
    return g->fn_refl_pget;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i8ptr, i64ptr}, 4, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_pget", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_pget = fn;
  LLVMValueRef mfind = refl_mfind_fn(g);
  LLVMValueRef invoke = refl_invoke_fn(g);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef call = LLVMAppendBasicBlockInContext(g->ctx, fn, "call");
  LLVMBasicBlockRef miss = LLVMAppendBasicBlockInContext(g->ctx, fn, "miss");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef obj = LLVMGetParam(fn, 1);
  LLVMValueRef name = LLVMGetParam(fn, 2);
  LLVMValueRef kindout = LLVMGetParam(fn, 3);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef idx = zan_call2(
      g->builder,
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i64}, 3, 0), mfind,
      (LLVMValueRef[]){ti, name, LLVMConstInt(i64, ZAN_REFL_MF_PROPGET, 0)}, 3,
      "refl.gi");
  LLVMValueRef ok = LLVMBuildAnd(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0),
                    "refl.gok"),
      LLVMBuildIsNotNull(g->builder, obj, "refl.gobj"), "refl.gok2");
  LLVMBuildCondBr(g->builder, ok, call, miss);

  LLVMPositionBuilderAtEnd(g->builder, call);
  LLVMValueRef slots = LLVMBuildArrayAlloca(
      g->builder, i64, LLVMConstInt(i64, ZAN_REFL_ARG_SLOTS, 0), "refl.gargs");
  LLVMValueRef kout = LLVMBuildAlloca(g->builder, i64, "refl.gk");
  LLVMValueRef v = zan_call2(
      g->builder,
      LLVMFunctionType(
          i64, (LLVMTypeRef[]){i8ptr, i64, i64, i8ptr, i64ptr, i64ptr}, 6, 0),
      invoke,
      (LLVMValueRef[]){ti, LLVMConstInt(i64, ZAN_REFL_TBL_METHOD, 0), idx, obj,
                       slots, kout},
      6, "refl.gv");
  LLVMValueRef k = LLVMBuildLoad2(g->builder, i64, kout, "refl.gkv");
  LLVMBuildStore(
      g->builder,
      LLVMBuildSelect(
          g->builder,
          LLVMBuildICmp(g->builder, LLVMIntEQ, k, LLVMConstInt(i64, 0, 0),
                        "refl.gnk"),
          LLVMConstInt(i64, 0, 0),
          LLVMBuildSub(g->builder, k, LLVMConstInt(i64, 1, 0), "refl.gk1"),
          "refl.gks"),
      kindout);
  LLVMBuildRet(g->builder, v);

  LLVMPositionBuilderAtEnd(g->builder, miss);
  LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), kindout);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_spget_fn(zan_irgen_t *g) {
  if (g->fn_refl_spget)
    return g->fn_refl_spget;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i64ptr}, 3, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_spget", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_spget = fn;
  LLVMValueRef mfind = refl_mfind_fn(g);
  LLVMValueRef invoke = refl_invoke_fn(g);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef call = LLVMAppendBasicBlockInContext(g->ctx, fn, "call");
  LLVMBasicBlockRef miss = LLVMAppendBasicBlockInContext(g->ctx, fn, "miss");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef name = LLVMGetParam(fn, 1);
  LLVMValueRef kindout = LLVMGetParam(fn, 2);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef idx = zan_call2(
      g->builder,
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i64}, 3, 0), mfind,
      (LLVMValueRef[]){
          ti, name,
          LLVMConstInt(i64, ZAN_REFL_MF_STATIC | ZAN_REFL_MF_PROPGET, 0)},
      3, "refl.sgi");
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0),
                    "refl.sgok"),
      call, miss);

  LLVMPositionBuilderAtEnd(g->builder, call);
  LLVMValueRef slots = LLVMBuildArrayAlloca(
      g->builder, i64, LLVMConstInt(i64, ZAN_REFL_ARG_SLOTS, 0), "refl.gargs");
  LLVMValueRef kout = LLVMBuildAlloca(g->builder, i64, "refl.gk");
  LLVMValueRef v = zan_call2(
      g->builder,
      LLVMFunctionType(
          i64, (LLVMTypeRef[]){i8ptr, i64, i64, i8ptr, i64ptr, i64ptr}, 6, 0),
      invoke,
      (LLVMValueRef[]){ti, LLVMConstInt(i64, ZAN_REFL_TBL_METHOD, 0), idx,
                       LLVMConstNull(i8ptr), slots, kout},
      6, "refl.gv");
  LLVMValueRef k = LLVMBuildLoad2(g->builder, i64, kout, "refl.gkv");
  LLVMBuildStore(
      g->builder,
      LLVMBuildSelect(
          g->builder,
          LLVMBuildICmp(g->builder, LLVMIntEQ, k, LLVMConstInt(i64, 0, 0),
                        "refl.gnk"),
          LLVMConstInt(i64, 0, 0),
          LLVMBuildSub(g->builder, k, LLVMConstInt(i64, 1, 0), "refl.gk1"),
          "refl.gks"),
      kindout);
  LLVMBuildRet(g->builder, v);

  LLVMPositionBuilderAtEnd(g->builder, miss);
  LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), kindout);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static void refl_emit_store_slot(zan_irgen_t *g, LLVMValueRef fn,
                                 LLVMValueRef p, LLVMValueRef fkind,
                                 LLVMValueRef vops, LLVMValueRef val,
                                 LLVMValueRef vkind, LLVMBasicBlockRef yes,
                                 LLVMBasicBlockRef no) {
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i16 = LLVMInt16TypeInContext(g->ctx);
  LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef flt = LLVMFloatTypeInContext(g->ctx);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMValueRef is_fp =
      LLVMBuildICmp(g->builder, LLVMIntEQ, vkind,
                    LLVMConstInt(i64, ZAN_REFL_VK_DOUBLE, 0), "refl.visfp");
  LLVMValueRef as_d = LLVMBuildSelect(
      g->builder, is_fp, LLVMBuildBitCast(g->builder, val, dbl, "refl.vbc"),
      LLVMBuildSIToFP(g->builder, val, dbl, "refl.vi2f"), "refl.vd");
  LLVMValueRef as_i = LLVMBuildSelect(
      g->builder, is_fp,
      emit_fptosi_guarded(g,
                          LLVMBuildBitCast(g->builder, val, dbl, "refl.vbc2"),
                          i64, (zan_loc_t){0}, is_fp),
      val, "refl.vi");
  struct {
    int kind;
    LLVMTypeRef ty;
    int fp;
    const char *nm;
  } cases[] = {
      {ZAN_REFL_FK_BOOL, i8, 0, "b"},      {ZAN_REFL_FK_BYTE, i8, 0, "u8"},
      {ZAN_REFL_FK_SBYTE, i8, 0, "i8"},    {ZAN_REFL_FK_SHORT, i16, 0, "i16"},
      {ZAN_REFL_FK_USHORT, i16, 0, "u16"}, {ZAN_REFL_FK_INT, i32, 0, "i32"},
      {ZAN_REFL_FK_UINT, i32, 0, "u32"},   {ZAN_REFL_FK_LONG, i64, 0, "i64"},
      {ZAN_REFL_FK_ULONG, i64, 0, "u64"},  {ZAN_REFL_FK_CHAR, i64, 0, "ch"},
      {ZAN_REFL_FK_ENUM, i64, 0, "en"},    {ZAN_REFL_FK_FLOAT, flt, 1, "f32"},
      {ZAN_REFL_FK_DOUBLE, dbl, 1, "f64"},
  };
  int ncases = (int)(sizeof(cases) / sizeof(cases[0]));
  LLVMValueRef sw =
      LLVMBuildSwitch(g->builder, fkind, no, (unsigned)ncases + 5);
  for (int ci = 0; ci < ncases; ci++) {
    char bn[24];
    snprintf(bn, sizeof(bn), "st.%s", cases[ci].nm);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, bn);
    LLVMAddCase(sw, LLVMConstInt(i64, (unsigned long long)cases[ci].kind, 0),
                bb);
    LLVMPositionBuilderAtEnd(g->builder, bb);
    LLVMValueRef sv;
    if (cases[ci].fp)
      sv = cases[ci].ty == flt
               ? LLVMBuildFPTrunc(g->builder, as_d, flt, "refl.stf")
               : as_d;
    else if (cases[ci].kind == ZAN_REFL_FK_BOOL)
      sv = LLVMBuildZExt(g->builder,
                         LLVMBuildICmp(g->builder, LLVMIntNE, as_i,
                                       LLVMConstInt(i64, 0, 0), "refl.stb"),
                         i8, "refl.stbz");
    else
      sv = cases[ci].ty == i64
               ? as_i
               : LLVMBuildTrunc(g->builder, as_i, cases[ci].ty, "refl.sttr");
    LLVMBuildStore(g->builder, sv,
                   LLVMBuildBitCast(g->builder, p,
                                    LLVMPointerType(cases[ci].ty, 0),
                                    "refl.stp"));
    LLVMBuildBr(g->builder, yes);
  }
  LLVMTypeRef arc_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                        (LLVMTypeRef[]){i8ptr}, 1, 0);
  LLVMBasicBlockRef sstr = LLVMAppendBasicBlockInContext(g->ctx, fn, "st.str");
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0), sstr);
  LLVMPositionBuilderAtEnd(g->builder, sstr);
  LLVMBasicBlockRef sstr2 =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "st.str2");
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntEQ, vkind,
                                LLVMConstInt(i64, ZAN_REFL_VK_PTR, 0),
                                "refl.visptr"),
                  sstr2, no);
  LLVMPositionBuilderAtEnd(g->builder, sstr2);
  LLVMValueRef sp =
      LLVMBuildBitCast(g->builder, p, LLVMPointerType(i8ptr, 0), "refl.stsp");
  LLVMValueRef nv = LLVMBuildIntToPtr(g->builder, val, i8ptr, "refl.stsv");
  LLVMValueRef ov = LLVMBuildLoad2(g->builder, i8ptr, sp, "refl.stold");
  zan_call2(g->builder, arc_ty, g->rt_str_retain, &nv, 1, "");
  LLVMBuildStore(g->builder, nv, sp);
  zan_call2(g->builder, arc_ty, g->rt_str_release, &ov, 1, "");
  LLVMBuildBr(g->builder, yes);

  LLVMBasicBlockRef sref = LLVMAppendBasicBlockInContext(g->ctx, fn, "st.ref");
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_REF, 0), sref);
  LLVMPositionBuilderAtEnd(g->builder, sref);
  LLVMBasicBlockRef sref2 =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "st.ref2");
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntEQ, vkind,
                                LLVMConstInt(i64, ZAN_REFL_VK_PTR, 0),
                                "refl.visptr_ref"),
                  sref2, no);
  LLVMPositionBuilderAtEnd(g->builder, sref2);
  LLVMValueRef ref_sp = LLVMBuildBitCast(
      g->builder, p, LLVMPointerType(i8ptr, 0), "refl.stref_p");
  LLVMValueRef ref_nv =
      LLVMBuildIntToPtr(g->builder, val, i8ptr, "refl.stref_nv");
  LLVMValueRef ref_ov =
      LLVMBuildLoad2(g->builder, i8ptr, ref_sp, "refl.stref_ov");
  zan_call2(g->builder, arc_ty, g->rt_retain, &ref_nv, 1, "");
  LLVMBuildStore(g->builder, ref_nv, ref_sp);
  zan_call2(g->builder, arc_ty, g->rt_release_dyn, &ref_ov, 1, "");
  LLVMBuildBr(g->builder, yes);

  LLVMBasicBlockRef sdec = LLVMAppendBasicBlockInContext(g->ctx, fn, "st.dec");
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_DECIMAL, 0), sdec);
  LLVMPositionBuilderAtEnd(g->builder, sdec);
  {
    LLVMTypeRef dec_ty = get_decimal_type(g);
    LLVMValueRef dp = LLVMBuildBitCast(g->builder, p,
                                       LLVMPointerType(dec_ty, 0), "refl.stdp");
    LLVMBasicBlockRef sdec_d =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "st.dec.d");
    LLVMBasicBlockRef sdec_i =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "st.dec.i");
    LLVMBasicBlockRef sdec_f =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "st.dec.f");
    LLVMValueRef dsw = LLVMBuildSwitch(g->builder, vkind, no, 3);
    LLVMAddCase(dsw, LLVMConstInt(i64, ZAN_REFL_VK_DECIMAL, 0), sdec_d);
    LLVMAddCase(dsw, LLVMConstInt(i64, ZAN_REFL_VK_INT, 0), sdec_i);
    LLVMAddCase(dsw, LLVMConstInt(i64, ZAN_REFL_VK_DOUBLE, 0), sdec_f);

    LLVMPositionBuilderAtEnd(g->builder, sdec_d);
    LLVMBasicBlockRef sdec_dl =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "st.dec.dl");
    LLVMBuildCondBr(
        g->builder,
        LLVMBuildICmp(g->builder, LLVMIntNE, val, LLVMConstInt(i64, 0, 0),
                      "refl.dptrnn"),
        sdec_dl, no);
    LLVMPositionBuilderAtEnd(g->builder, sdec_dl);
    LLVMValueRef dv = LLVMBuildLoad2(
        g->builder, dec_ty,
        LLVMBuildIntToPtr(g->builder, val, LLVMPointerType(dec_ty, 0),
                          "refl.dsrc"),
        "refl.dv");
    LLVMBuildStore(g->builder, dv, dp);
    LLVMBuildBr(g->builder, yes);

    LLVMPositionBuilderAtEnd(g->builder, sdec_i);
    LLVMBuildStore(g->builder, refl_decimal_from_i64(g, val), dp);
    LLVMBuildBr(g->builder, yes);

    LLVMPositionBuilderAtEnd(g->builder, sdec_f);
    LLVMBuildStore(
        g->builder,
        refl_decimal_from_f64(g,
                              LLVMBuildBitCast(g->builder, val, dbl, "refl.df")),
        dp);
    LLVMBuildBr(g->builder, yes);
  }

  LLVMBasicBlockRef sval = LLVMAppendBasicBlockInContext(g->ctx, fn, "st.val");
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_STRUCT, 0), sval);
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_NULLABLE, 0), sval);
  LLVMPositionBuilderAtEnd(g->builder, sval);
  LLVMBasicBlockRef sval_c =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "st.valc");
  LLVMBuildCondBr(g->builder, LLVMBuildIsNotNull(g->builder, vops, "refl.vh"),
                  sval_c, no);
  LLVMPositionBuilderAtEnd(g->builder, sval_c);
  LLVMValueRef vok =
      refl_call_vops(g, vops, ZAN_REFL_VOP_SET, p, val, vkind);
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntNE, vok,
                                LLVMConstInt(i64, 0, 0), "refl.vok"),
                  yes, no);
}

static LLVMValueRef refl_set_at_fn(zan_irgen_t *g) {
  if (g->fn_refl_set_at)
    return g->fn_refl_set_at;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i16 = LLVMInt16TypeInContext(g->ctx);
  LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
  LLVMTypeRef flt = LLVMFloatTypeInContext(g->ctx);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i64, i64, i64}, 5, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_set_at", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_set_at = fn;
  LLVMValueRef mfind = refl_mfind_fn(g);
  LLVMValueRef invoke = refl_invoke_fn(g);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMValueRef save_fn = g->current_fn;
  g->current_fn = fn;
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef have = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
  LLVMBasicBlockRef propchk =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "propchk");
  LLVMBasicBlockRef setr = LLVMAppendBasicBlockInContext(g->ctx, fn, "setter");
  LLVMBasicBlockRef slot = LLVMAppendBasicBlockInContext(g->ctx, fn, "slot");
  LLVMBasicBlockRef no = LLVMAppendBasicBlockInContext(g->ctx, fn, "no");
  LLVMBasicBlockRef yes = LLVMAppendBasicBlockInContext(g->ctx, fn, "yes");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef obj = LLVMGetParam(fn, 1);
  LLVMValueRef idx = LLVMGetParam(fn, 2);
  LLVMValueRef val = LLVMGetParam(fn, 3);
  LLVMValueRef vkind = LLVMGetParam(fn, 4);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef fields = refl_hdr_ptr(g, ti, ZAN_REFL_FIELDS_OFF, "refl.fields");
  LLVMValueRef count = refl_hdr_i64(g, ti, ZAN_REFL_COUNT_OFF, "refl.count");
  LLVMValueRef valid = LLVMBuildAnd(
      g->builder,
      LLVMBuildAnd(
          g->builder,
          LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0),
                        "refl.ge"),
          LLVMBuildICmp(g->builder, LLVMIntSLT, idx, count, "refl.lt"),
          "refl.inb"),
      LLVMBuildAnd(g->builder,
                   LLVMBuildIsNotNull(g->builder, fields, "refl.fnn"),
                   LLVMBuildIsNotNull(g->builder, obj, "refl.onn"),
                   "refl.ptrs"),
      "refl.valid");
  LLVMBuildCondBr(g->builder, valid, have, no);

  LLVMPositionBuilderAtEnd(g->builder, have);
  LLVMValueRef fe = refl_member_row(g, fields, idx, false);
  LLVMValueRef fflags = refl_member_u32(g, fe, false, 8);
  LLVMValueRef is_prop = LLVMBuildICmp(
      g->builder, LLVMIntNE,
      LLVMBuildAnd(g->builder, fflags,
                   LLVMConstInt(i64, ZAN_REFL_FF_PROPERTY, 0), "refl.pf"),
      LLVMConstInt(i64, 0, 0), "refl.isp");
  LLVMBuildCondBr(g->builder, is_prop, propchk, slot);

  LLVMPositionBuilderAtEnd(g->builder, propchk);
  LLVMValueRef fname = refl_field_ptr(g, fields, fe, 0);
  LLVMValueRef sidx = zan_call2(
      g->builder,
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i64}, 3, 0), mfind,
      (LLVMValueRef[]){ti, fname, LLVMConstInt(i64, ZAN_REFL_MF_PROPSET, 0)}, 3,
      "refl.si");
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntSGE, sidx, LLVMConstInt(i64, 0, 0),
                    "refl.sge"),
      setr, slot);

  LLVMPositionBuilderAtEnd(g->builder, setr);
  LLVMValueRef slots = LLVMBuildArrayAlloca(
      g->builder, i64, LLVMConstInt(i64, 2 * ZAN_REFL_ARG_SLOTS, 0),
      "refl.sargs");
  LLVMBuildStore(g->builder, val, slots);
  LLVMValueRef vk_off = LLVMConstInt(i64, ZAN_REFL_ARG_SLOTS, 0);
  LLVMValueRef vkp =
      LLVMBuildGEP2(g->builder, i64, slots, &vk_off, 1, "refl.svkp");
  LLVMBuildStore(g->builder, vkind, vkp);
  LLVMValueRef kout = LLVMBuildAlloca(g->builder, i64, "refl.sk");
  zan_call2(
      g->builder,
      LLVMFunctionType(
          i64, (LLVMTypeRef[]){i8ptr, i64, i64, i8ptr, i64ptr, i64ptr}, 6, 0),
      invoke,
      (LLVMValueRef[]){ti, LLVMConstInt(i64, ZAN_REFL_TBL_METHOD, 0), sidx, obj,
                       slots, kout},
      6, "refl.sv");
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntNE,
                    LLVMBuildLoad2(g->builder, i64, kout, "refl.skv"),
                    LLVMConstInt(i64, 0, 0), "refl.sdone"),
      yes, no);

  LLVMPositionBuilderAtEnd(g->builder, slot);
  LLVMValueRef fkind = refl_member_u32(g, fe, false, 7);
  LLVMValueRef foff = refl_field_offset(g, fe);
  LLVMValueRef vops = refl_field_ptr(g, fields, fe, 3);
  LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, obj, &foff, 1, "refl.fslot");
  refl_emit_store_slot(g, fn, p, fkind, vops, val, vkind, yes, no);

  LLVMPositionBuilderAtEnd(g->builder, yes);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));
  LLVMPositionBuilderAtEnd(g->builder, no);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

  g->current_fn = save_fn;
  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_sset_fn(zan_irgen_t *g) {
  if (g->fn_refl_sset)
    return g->fn_refl_sset;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
  LLVMTypeRef fn_ty = LLVMFunctionType(
      i64, (LLVMTypeRef[]){i8ptr, i8ptr, i64, i64}, 4, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_sset", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_sset = fn;
  LLVMValueRef mfind = refl_mfind_fn(g);
  LLVMValueRef invoke = refl_invoke_fn(g);
  LLVMValueRef sfind = refl_sfind_fn(g);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMValueRef save_fn = g->current_fn;
  g->current_fn = fn;
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef setr = LLVMAppendBasicBlockInContext(g->ctx, fn, "setter");
  LLVMBasicBlockRef fld = LLVMAppendBasicBlockInContext(g->ctx, fn, "field");
  LLVMBasicBlockRef gchk = LLVMAppendBasicBlockInContext(g->ctx, fn, "gchk");
  LLVMBasicBlockRef slot = LLVMAppendBasicBlockInContext(g->ctx, fn, "slot");
  LLVMBasicBlockRef yes = LLVMAppendBasicBlockInContext(g->ctx, fn, "yes");
  LLVMBasicBlockRef no = LLVMAppendBasicBlockInContext(g->ctx, fn, "no");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef name = LLVMGetParam(fn, 1);
  LLVMValueRef val = LLVMGetParam(fn, 2);
  LLVMValueRef vkind = LLVMGetParam(fn, 3);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef sidx = zan_call2(
      g->builder,
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i64}, 3, 0), mfind,
      (LLVMValueRef[]){
          ti, name,
          LLVMConstInt(i64, ZAN_REFL_MF_STATIC | ZAN_REFL_MF_PROPSET, 0)},
      3, "refl.ssi");
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntSGE, sidx, LLVMConstInt(i64, 0, 0),
                    "refl.sge"),
      setr, fld);

  LLVMPositionBuilderAtEnd(g->builder, setr);
  LLVMValueRef slots = LLVMBuildArrayAlloca(
      g->builder, i64, LLVMConstInt(i64, 2 * ZAN_REFL_ARG_SLOTS, 0),
      "refl.sargs");
  LLVMBuildStore(g->builder, val, slots);
  LLVMValueRef vk_off = LLVMConstInt(i64, ZAN_REFL_ARG_SLOTS, 0);
  LLVMValueRef vkp =
      LLVMBuildGEP2(g->builder, i64, slots, &vk_off, 1, "refl.svkp");
  LLVMBuildStore(g->builder, vkind, vkp);
  LLVMValueRef kout = LLVMBuildAlloca(g->builder, i64, "refl.sk");
  zan_call2(
      g->builder,
      LLVMFunctionType(
          i64, (LLVMTypeRef[]){i8ptr, i64, i64, i8ptr, i64ptr, i64ptr}, 6, 0),
      invoke,
      (LLVMValueRef[]){ti, LLVMConstInt(i64, ZAN_REFL_TBL_METHOD, 0), sidx,
                       LLVMConstNull(i8ptr), slots, kout},
      6, "refl.sv");
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntNE,
                    LLVMBuildLoad2(g->builder, i64, kout, "refl.skv"),
                    LLVMConstInt(i64, 0, 0), "refl.sdone"),
      yes, no);

  LLVMPositionBuilderAtEnd(g->builder, fld);
  LLVMValueRef idx = zan_call2(
      g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
      sfind, (LLVMValueRef[]){ti, name}, 2, "refl.sfi");
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0),
                    "refl.fge"),
      gchk, no);

  LLVMPositionBuilderAtEnd(g->builder, gchk);
  LLVMValueRef sfields =
      refl_hdr_ptr(g, ti, ZAN_REFL_SFIELDS_OFF, "refl.sfields");
  LLVMValueRef fe = refl_member_row(g, sfields, idx, false);
  LLVMValueRef p = refl_field_ptr(g, sfields, fe, 3);
  LLVMBuildCondBr(g->builder, LLVMBuildIsNotNull(g->builder, p, "refl.gpnn"),
                  slot, no);

  LLVMPositionBuilderAtEnd(g->builder, slot);
  LLVMValueRef fkind = refl_member_u32(g, fe, false, 7);
  LLVMValueRef vops = LLVMBuildIntToPtr(
      g->builder, refl_field_offset(g, fe), i8ptr, "refl.svops");
  refl_emit_store_slot(g, fn, p, fkind, vops, val, vkind, yes, no);

  LLVMPositionBuilderAtEnd(g->builder, yes);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));
  LLVMPositionBuilderAtEnd(g->builder, no);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

  g->current_fn = save_fn;
  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_set_fn(zan_irgen_t *g) {
  if (g->fn_refl_set)
    return g->fn_refl_set;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
  LLVMTypeRef fn_ty = LLVMFunctionType(
      i64, (LLVMTypeRef[]){i8ptr, i8ptr, i8ptr, i64, i64}, 5, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_set", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_set = fn;
  LLVMValueRef mfind = refl_mfind_fn(g);
  LLVMValueRef invoke = refl_invoke_fn(g);
  LLVMValueRef find = refl_find_fn(g);
  LLVMValueRef set_at = refl_set_at_fn(g);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMValueRef save_fn = g->current_fn;
  g->current_fn = fn;
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef setr = LLVMAppendBasicBlockInContext(g->ctx, fn, "setter");
  LLVMBasicBlockRef fld = LLVMAppendBasicBlockInContext(g->ctx, fn, "field");
  LLVMBasicBlockRef slot = LLVMAppendBasicBlockInContext(g->ctx, fn, "slot");
  LLVMBasicBlockRef no = LLVMAppendBasicBlockInContext(g->ctx, fn, "no");
  LLVMBasicBlockRef yes = LLVMAppendBasicBlockInContext(g->ctx, fn, "yes");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef obj = LLVMGetParam(fn, 1);
  LLVMValueRef name = LLVMGetParam(fn, 2);
  LLVMValueRef val = LLVMGetParam(fn, 3);
  LLVMValueRef vkind = LLVMGetParam(fn, 4);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef sidx = zan_call2(
      g->builder,
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i64}, 3, 0), mfind,
      (LLVMValueRef[]){ti, name, LLVMConstInt(i64, ZAN_REFL_MF_PROPSET, 0)}, 3,
      "refl.si");
  LLVMValueRef has_setter = LLVMBuildAnd(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntSGE, sidx, LLVMConstInt(i64, 0, 0),
                    "refl.sge"),
      LLVMBuildIsNotNull(g->builder, obj, "refl.sobj"), "refl.hasset");
  LLVMBuildCondBr(g->builder, has_setter, setr, fld);

  LLVMPositionBuilderAtEnd(g->builder, setr);
  LLVMValueRef slots = LLVMBuildArrayAlloca(
      g->builder, i64, LLVMConstInt(i64, 2 * ZAN_REFL_ARG_SLOTS, 0),
      "refl.sargs");
  LLVMBuildStore(g->builder, val, slots);
  LLVMValueRef vk_off = LLVMConstInt(i64, ZAN_REFL_ARG_SLOTS, 0);
  LLVMValueRef vkp =
      LLVMBuildGEP2(g->builder, i64, slots, &vk_off, 1, "refl.svkp");
  LLVMBuildStore(g->builder, vkind, vkp);
  LLVMValueRef kout = LLVMBuildAlloca(g->builder, i64, "refl.sk");
  zan_call2(
      g->builder,
      LLVMFunctionType(
          i64, (LLVMTypeRef[]){i8ptr, i64, i64, i8ptr, i64ptr, i64ptr}, 6, 0),
      invoke,
      (LLVMValueRef[]){ti, LLVMConstInt(i64, ZAN_REFL_TBL_METHOD, 0), sidx, obj,
                       slots, kout},
      6, "refl.sv");
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntNE,
                    LLVMBuildLoad2(g->builder, i64, kout, "refl.skv"),
                    LLVMConstInt(i64, 0, 0), "refl.sdone"),
      yes, no);

  LLVMPositionBuilderAtEnd(g->builder, fld);
  LLVMValueRef idx = zan_call2(
      g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
      find, (LLVMValueRef[]){ti, name}, 2, "refl.fi");
  LLVMValueRef fok = LLVMBuildAnd(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0),
                    "refl.fge"),
      LLVMBuildIsNotNull(g->builder, obj, "refl.fobj"), "refl.fok");
  LLVMBuildCondBr(g->builder, fok, slot, no);

  LLVMPositionBuilderAtEnd(g->builder, slot);
  LLVMBuildRet(
      g->builder,
      zan_call2(
          g->builder,
          LLVMFunctionType(
              i64, (LLVMTypeRef[]){i8ptr, i8ptr, i64, i64, i64}, 5, 0),
          set_at, (LLVMValueRef[]){ti, obj, idx, val, vkind}, 5, "refl.sat"));

  LLVMPositionBuilderAtEnd(g->builder, yes);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));
  LLVMPositionBuilderAtEnd(g->builder, no);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

  g->current_fn = save_fn;
  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_cfind_fn(zan_irgen_t *g) {
  if (g->fn_refl_cfind)
    return g->fn_refl_cfind;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i64}, 2, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_cfind", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_cfind = fn;

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(g->ctx, fn, "head");
  LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
  LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
  LLVMBasicBlockRef found = LLVMAppendBasicBlockInContext(g->ctx, fn, "found");
  LLVMBasicBlockRef miss = LLVMAppendBasicBlockInContext(g->ctx, fn, "miss");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef want = LLVMGetParam(fn, 1);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef arr = refl_hdr_ptr(g, ti, ZAN_REFL_CTORS_OFF, "refl.ct");
  LLVMValueRef count = refl_hdr_i64(g, ti, ZAN_REFL_CCOUNT_OFF, "refl.cc");
  LLVMBuildCondBr(g->builder, LLVMBuildIsNull(g->builder, arr, "refl.noct"),
                  miss, head);

  LLVMPositionBuilderAtEnd(g->builder, head);
  LLVMValueRef i = LLVMBuildPhi(g->builder, i64, "refl.ci");
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntSLT, i, count, "refl.more"),
                  body, miss);

  LLVMPositionBuilderAtEnd(g->builder, body);
  LLVMValueRef rec = refl_member_row(g, arr, i, true);
  LLVMValueRef pc = refl_method_int(g, rec, ZAN_REFL_MO_PCOUNT);
  LLVMValueRef th = refl_method_ptr(g, arr, rec, ZAN_REFL_MO_THUNK);
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildAnd(g->builder,
                   LLVMBuildICmp(g->builder, LLVMIntEQ, pc, want, "refl.cpq"),
                   LLVMBuildIsNotNull(g->builder, th, "refl.cthnn"),
                   "refl.chit"),
      found, next);

  LLVMPositionBuilderAtEnd(g->builder, next);
  LLVMValueRef inext =
      LLVMBuildAdd(g->builder, i, LLVMConstInt(i64, 1, 0), "refl.ci.n");
  LLVMBuildBr(g->builder, head);
  LLVMAddIncoming(i, (LLVMValueRef[]){LLVMConstInt(i64, 0, 0), inext},
                  (LLVMBasicBlockRef[]){entry, next}, 2);

  LLVMPositionBuilderAtEnd(g->builder, found);
  LLVMBuildRet(g->builder, i);
  LLVMPositionBuilderAtEnd(g->builder, miss);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, (unsigned long long)-1LL, 1));

  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_tainfo_fn(zan_irgen_t *g) {
  if (g->fn_refl_tainfo)
    return g->fn_refl_tainfo;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i64, i64}, 3, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_tainfo", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_tainfo = fn;

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(g->ctx, fn, "head");
  LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
  LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
  LLVMBasicBlockRef cnt = LLVMAppendBasicBlockInContext(g->ctx, fn, "count");
  LLVMBasicBlockRef ld = LLVMAppendBasicBlockInContext(g->ctx, fn, "load");
  LLVMBasicBlockRef zero = LLVMAppendBasicBlockInContext(g->ctx, fn, "zero");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef idx = LLVMGetParam(fn, 1);
  LLVMValueRef which = LLVMGetParam(fn, 2);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef arr = refl_hdr_ptr(g, ti, ZAN_REFL_TARGS_OFF, "refl.ta");
  LLVMValueRef arrp =
      LLVMBuildBitCast(g->builder, arr, LLVMPointerType(i8ptr, 0), "refl.tap");
  LLVMBuildCondBr(g->builder, LLVMBuildIsNull(g->builder, arr, "refl.nota"),
                  zero, head);

  LLVMPositionBuilderAtEnd(g->builder, head);
  LLVMValueRef i = LLVMBuildPhi(g->builder, i64, "refl.ti");
  LLVMValueRef e = LLVMBuildLoad2(
      g->builder, i8ptr,
      LLVMBuildGEP2(g->builder, i8ptr, arrp, &i, 1, "refl.tae"), "refl.tav");
  LLVMBuildCondBr(g->builder, LLVMBuildIsNull(g->builder, e, "refl.taend"),
                  done, next);

  LLVMPositionBuilderAtEnd(g->builder, next);
  LLVMValueRef inext =
      LLVMBuildAdd(g->builder, i, LLVMConstInt(i64, 1, 0), "refl.ti.n");
  LLVMBuildBr(g->builder, head);
  LLVMAddIncoming(i, (LLVMValueRef[]){LLVMConstInt(i64, 0, 0), inext},
                  (LLVMBasicBlockRef[]){entry, next}, 2);

  LLVMPositionBuilderAtEnd(g->builder, done);
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                                LLVMConstInt(i64, 0, 0), "refl.iscnt"),
                  cnt, ld);

  LLVMPositionBuilderAtEnd(g->builder, cnt);
  LLVMBuildRet(g->builder, i);

  LLVMPositionBuilderAtEnd(g->builder, ld);
  LLVMBasicBlockRef ld2 = LLVMAppendBasicBlockInContext(g->ctx, fn, "load2");
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildAnd(g->builder,
                   LLVMBuildICmp(g->builder, LLVMIntSGE, idx,
                                 LLVMConstInt(i64, 0, 0), "refl.tge"),
                   LLVMBuildICmp(g->builder, LLVMIntSLT, idx, i, "refl.tlt"),
                   "refl.tok"),
      ld2, zero);

  LLVMPositionBuilderAtEnd(g->builder, ld2);
  LLVMValueRef v = LLVMBuildLoad2(
      g->builder, i8ptr,
      LLVMBuildGEP2(g->builder, i8ptr, arrp, &idx, 1, "refl.tap2"), "refl.tv");
  LLVMBuildRet(g->builder, LLVMBuildPtrToInt(g->builder, v, i64, "refl.tvi"));

  LLVMPositionBuilderAtEnd(g->builder, zero);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}
