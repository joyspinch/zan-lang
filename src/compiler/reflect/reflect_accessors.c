static LLVMValueRef refl_hdr_ptr(zan_irgen_t *g, LLVMValueRef ti, int off,
                                 const char *name) {
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMValueRef d = LLVMConstInt(i64, (unsigned long long)(long long)off, 1);
  LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, ti, &d, 1, "refl.pp");
  p = LLVMBuildBitCast(g->builder, p, LLVMPointerType(i8ptr, 0), "refl.ppc");
  return LLVMBuildLoad2(g->builder, i8ptr, p, name);
}

static LLVMValueRef refl_empty_string(zan_irgen_t *g) {
  if (!g->refl_empty_str)
    g->refl_empty_str = refl_const_string(g, "", 0);
  return g->refl_empty_str;
}

static LLVMValueRef refl_find_fn(zan_irgen_t *g) {
  if (g->fn_refl_find)
    return g->fn_refl_find;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i32ptr = LLVMPointerType(i32, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_find", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_find = fn;

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef bhead = LLVMAppendBasicBlockInContext(g->ctx, fn, "bhead");
  LLVMBasicBlockRef bbody = LLVMAppendBasicBlockInContext(g->ctx, fn, "bbody");
  LLVMBasicBlockRef bleft = LLVMAppendBasicBlockInContext(g->ctx, fn, "bleft");
  LLVMBasicBlockRef bright =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "bright");
  LLVMBasicBlockRef bfound =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "bfound");
  LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(g->ctx, fn, "head");
  LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
  LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
  LLVMBasicBlockRef found = LLVMAppendBasicBlockInContext(g->ctx, fn, "found");
  LLVMBasicBlockRef miss = LLVMAppendBasicBlockInContext(g->ctx, fn, "miss");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef want = LLVMGetParam(fn, 1);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef fields = refl_hdr_ptr(g, ti, ZAN_REFL_FIELDS_OFF, "refl.fields");
  LLVMValueRef count = refl_hdr_i64(g, ti, ZAN_REFL_COUNT_OFF, "refl.count");
  LLVMValueRef has_f = LLVMBuildAnd(
      g->builder, LLVMBuildIsNotNull(g->builder, fields, "refl.fnn"),
      LLVMBuildICmp(g->builder, LLVMIntSGT, count, LLVMConstInt(i64, 0, 0),
                    "refl.fpos"),
      "refl.hasf");
  LLVMBasicBlockRef dispatch =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "dispatch");
  LLVMBuildCondBr(g->builder, has_f, dispatch, miss);

  LLVMPositionBuilderAtEnd(g->builder, dispatch);
  LLVMValueRef fsort = refl_hdr_ptr(g, ti, ZAN_REFL_FSORT_OFF, "refl.fsort");
  LLVMValueRef fsort32 = LLVMBuildBitCast(g->builder, fsort, i32ptr, "refl.fs32");
  LLVMValueRef bhi0 =
      LLVMBuildSub(g->builder, count, LLVMConstInt(i64, 1, 0), "refl.hi0");
  LLVMBuildCondBr(g->builder, LLVMBuildIsNotNull(g->builder, fsort, "refl.hasbs"),
                  bhead, head);

  /* O(log N) binary search over fsort[] preserving logical row indices */
  LLVMPositionBuilderAtEnd(g->builder, bhead);
  LLVMValueRef blo = LLVMBuildPhi(g->builder, i64, "refl.lo");
  LLVMValueRef bhi = LLVMBuildPhi(g->builder, i64, "refl.hi");
  LLVMValueRef bmore =
      LLVMBuildICmp(g->builder, LLVMIntSLE, blo, bhi, "refl.bmore");
  LLVMBuildCondBr(g->builder, bmore, bbody, miss);

  LLVMPositionBuilderAtEnd(g->builder, bbody);
  LLVMValueRef bdiff = LLVMBuildSub(g->builder, bhi, blo, "refl.bdiff");
  LLVMValueRef bhalf =
      LLVMBuildLShr(g->builder, bdiff, LLVMConstInt(i64, 1, 0), "refl.bhalf");
  LLVMValueRef bmid = LLVMBuildAdd(g->builder, blo, bhalf, "refl.bmid");
  LLVMValueRef bsp =
      LLVMBuildGEP2(g->builder, i32, fsort32, &bmid, 1, "refl.bsp");
  LLVMValueRef lidx = LLVMBuildZExt(
      g->builder, LLVMBuildLoad2(g->builder, i32, bsp, "refl.bsi"), i64,
      "refl.lidx");
  LLVMValueRef bfe = refl_member_row(g, fields, lidx, false);
  LLVMValueRef bfname = refl_field_ptr(g, fields, bfe, 0);
  LLVMValueRef bc = zan_call2(
      g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
      g->fn_strcmp, (LLVMValueRef[]){bfname, want}, 2, "refl.bcmp");
  LLVMValueRef bhit = LLVMBuildICmp(g->builder, LLVMIntEQ, bc,
                                    LLVMConstInt(i32, 0, 0), "refl.bhit");
  LLVMBasicBlockRef bdir = LLVMAppendBasicBlockInContext(g->ctx, fn, "bdir");
  LLVMBuildCondBr(g->builder, bhit, bfound, bdir);

  LLVMPositionBuilderAtEnd(g->builder, bdir);
  LLVMValueRef blt = LLVMBuildICmp(g->builder, LLVMIntSLT, bc,
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

  /* < 4 fields linear fast path */
  LLVMPositionBuilderAtEnd(g->builder, head);
  LLVMValueRef i = LLVMBuildPhi(g->builder, i64, "refl.i");
  LLVMValueRef more =
      LLVMBuildICmp(g->builder, LLVMIntSLT, i, count, "refl.more");
  LLVMBuildCondBr(g->builder, more, body, miss);

  LLVMPositionBuilderAtEnd(g->builder, body);
  LLVMValueRef fe = refl_member_row(g, fields, i, false);
  LLVMValueRef fname = refl_field_ptr(g, fields, fe, 0);
  LLVMValueRef c = zan_call2(
      g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
      g->fn_strcmp, (LLVMValueRef[]){fname, want}, 2, "refl.cmp");
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntEQ, c,
                                LLVMConstInt(i32, 0, 0), "refl.hit"),
                  found, next);

  LLVMPositionBuilderAtEnd(g->builder, next);
  LLVMValueRef inext =
      LLVMBuildAdd(g->builder, i, LLVMConstInt(i64, 1, 0), "refl.i.n");
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

static LLVMValueRef refl_pget_fn(zan_irgen_t *g);
static LLVMValueRef refl_spget_fn(zan_irgen_t *g);

static LLVMValueRef refl_pget_as_i64(zan_irgen_t *g, LLVMValueRef slot,
                                     LLVMValueRef kind) {
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMValueRef is_fp = LLVMBuildOr(
      g->builder,
      LLVMBuildOr(
          g->builder,
          LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                        LLVMConstInt(i64, ZAN_REFL_FK_FLOAT, 0), "refl.pf"),
          LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                        LLVMConstInt(i64, ZAN_REFL_FK_DOUBLE, 0), "refl.pd"),
          "refl.pfp0"),
      LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                    LLVMConstInt(i64, ZAN_REFL_FK_DECIMAL, 0), "refl.pdec"),
      "refl.pfp");
  /* B-ID180：fp 槽位型重释越界 poison 收口。arm_mask=is_fp——整型槽
   * bitcast 出的位型是垃圾浮点，守卫只在 fp 臂生效。 */
  return LLVMBuildSelect(
      g->builder, is_fp,
      emit_fptosi_guarded(g,
                          LLVMBuildBitCast(g->builder, slot, dbl, "refl.pbc"),
                          i64, (zan_loc_t){0}, is_fp),
      slot, "refl.pi");
}

static LLVMValueRef refl_pget_as_f64(zan_irgen_t *g, LLVMValueRef slot,
                                     LLVMValueRef kind) {
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMValueRef is_fp = LLVMBuildOr(
      g->builder,
      LLVMBuildOr(
          g->builder,
          LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                        LLVMConstInt(i64, ZAN_REFL_FK_FLOAT, 0), "refl.pf"),
          LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                        LLVMConstInt(i64, ZAN_REFL_FK_DOUBLE, 0), "refl.pd"),
          "refl.pfp0"),
      LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                    LLVMConstInt(i64, ZAN_REFL_FK_DECIMAL, 0), "refl.pdec"),
      "refl.pfp");
  return LLVMBuildSelect(
      g->builder, is_fp, LLVMBuildBitCast(g->builder, slot, dbl, "refl.pbc"),
      LLVMBuildSIToFP(g->builder, slot, dbl, "refl.pi2f"), "refl.pdv");
}

static LLVMValueRef refl_call_vops(zan_irgen_t *g, LLVMValueRef vops, int op,
                                   LLVMValueRef slot_ptr, LLVMValueRef val,
                                   LLVMValueRef vkind) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i64, i8ptr, i64, i64}, 4, 0);
  LLVMValueRef fn =
      LLVMBuildBitCast(g->builder, vops, LLVMPointerType(fn_ty, 0), "refl.vfn");
  return zan_call2(
      g->builder, fn_ty, fn,
      (LLVMValueRef[]){LLVMConstInt(i64, (unsigned long long)op, 0), slot_ptr,
                       val ? val : LLVMConstInt(i64, 0, 0),
                       vkind ? vkind : LLVMConstInt(i64, 0, 0)},
      4, "refl.vop");
}

static LLVMValueRef refl_call_pget(zan_irgen_t *g, LLVMValueRef ti,
                                   LLVMValueRef obj, LLVMValueRef name,
                                   LLVMValueRef *out_kind) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
  LLVMValueRef kslot = LLVMBuildAlloca(g->builder, i64, "refl.pk");
  LLVMValueRef v = zan_call2(
      g->builder,
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i8ptr, i64ptr}, 4, 0),
      refl_pget_fn(g), (LLVMValueRef[]){ti, obj, name, kslot}, 4, "refl.pv");
  *out_kind = LLVMBuildLoad2(g->builder, i64, kslot, "refl.pkv");
  return v;
}

static LLVMValueRef refl_call_spget(zan_irgen_t *g, LLVMValueRef ti,
                                    LLVMValueRef name,
                                    LLVMValueRef *out_kind) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
  LLVMValueRef kslot = LLVMBuildAlloca(g->builder, i64, "refl.spk");
  LLVMValueRef v = zan_call2(
      g->builder,
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i64ptr}, 3, 0),
      refl_spget_fn(g), (LLVMValueRef[]){ti, name, kslot}, 3, "refl.spv");
  *out_kind = LLVMBuildLoad2(g->builder, i64, kslot, "refl.spkv");
  return v;
}

static LLVMValueRef refl_sfind_fn(zan_irgen_t *g) {
  if (g->fn_refl_sfind)
    return g->fn_refl_sfind;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_sfind", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_sfind = fn;

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
  LLVMValueRef fields =
      refl_hdr_ptr(g, ti, ZAN_REFL_SFIELDS_OFF, "refl.sfields");
  LLVMValueRef count = refl_hdr_i64(g, ti, ZAN_REFL_SCOUNT_OFF, "refl.scount");
  LLVMValueRef has_f = LLVMBuildAnd(
      g->builder, LLVMBuildIsNotNull(g->builder, fields, "refl.sfnn"),
      LLVMBuildICmp(g->builder, LLVMIntSGT, count, LLVMConstInt(i64, 0, 0),
                    "refl.sfpos"),
      "refl.hassf");
  LLVMBuildCondBr(g->builder, has_f, head, miss);

  LLVMPositionBuilderAtEnd(g->builder, head);
  LLVMValueRef i = LLVMBuildPhi(g->builder, i64, "refl.si");
  LLVMBuildCondBr(
      g->builder, LLVMBuildICmp(g->builder, LLVMIntSLT, i, count, "refl.smore"),
      body, miss);

  LLVMPositionBuilderAtEnd(g->builder, body);
  LLVMValueRef fe = refl_member_row(g, fields, i, false);
  LLVMValueRef fname = refl_field_ptr(g, fields, fe, 0);
  LLVMValueRef c = zan_call2(
      g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
      g->fn_strcmp, (LLVMValueRef[]){fname, want}, 2, "refl.scmp");
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntEQ, c, LLVMConstInt(i32, 0, 0), "refl.shit"),
      found, next);

  LLVMPositionBuilderAtEnd(g->builder, next);
  LLVMValueRef inext =
      LLVMBuildAdd(g->builder, i, LLVMConstInt(i64, 1, 0), "refl.si.n");
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

static void refl_emit_load_i64_slot(zan_irgen_t *g, LLVMValueRef fn,
                                    LLVMValueRef p, LLVMValueRef kind,
                                    LLVMValueRef vops, LLVMBasicBlockRef zero) {
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i16 = LLVMInt16TypeInContext(g->ctx);
  LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef flt = LLVMFloatTypeInContext(g->ctx);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMTypeRef dec_ty = get_decimal_type(g);
  struct {
    int kind;
    LLVMTypeRef ty;
    int sext;
    const char *nm;
  } cases[] = {
      {ZAN_REFL_FK_BOOL, i8, 0, "b"},      {ZAN_REFL_FK_BYTE, i8, 0, "u8"},
      {ZAN_REFL_FK_SBYTE, i8, 1, "i8"},    {ZAN_REFL_FK_SHORT, i16, 1, "i16"},
      {ZAN_REFL_FK_USHORT, i16, 0, "u16"}, {ZAN_REFL_FK_INT, i32, 1, "i32"},
      {ZAN_REFL_FK_UINT, i32, 0, "u32"},   {ZAN_REFL_FK_LONG, i64, 1, "i64"},
      {ZAN_REFL_FK_ULONG, i64, 0, "u64"},  {ZAN_REFL_FK_CHAR, i64, 1, "ch"},
      {ZAN_REFL_FK_ENUM, i64, 1, "en"},    {ZAN_REFL_FK_FLOAT, flt, 0, "f32"},
      {ZAN_REFL_FK_DOUBLE, dbl, 0, "f64"},
  };
  int ncases = (int)(sizeof(cases) / sizeof(cases[0]));
  LLVMValueRef sw =
      LLVMBuildSwitch(g->builder, kind, zero, (unsigned)ncases + 2);
  for (int ci = 0; ci < ncases; ci++) {
    char bn[24];
    snprintf(bn, sizeof(bn), "ld.%s", cases[ci].nm);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, bn);
    LLVMAddCase(sw, LLVMConstInt(i64, (unsigned long long)cases[ci].kind, 0),
                bb);
    LLVMPositionBuilderAtEnd(g->builder, bb);
    LLVMValueRef v = LLVMBuildLoad2(
        g->builder, cases[ci].ty,
        LLVMBuildBitCast(g->builder, p, LLVMPointerType(cases[ci].ty, 0),
                         "refl.sp"),
        "refl.v");
    if (cases[ci].ty == flt || cases[ci].ty == dbl)
      v = emit_fptosi_guarded(g, v, i64, (zan_loc_t){0}, NULL);
    else if (cases[ci].ty != i64)
      v = cases[ci].sext ? LLVMBuildSExt(g->builder, v, i64, "refl.sx")
                         : LLVMBuildZExt(g->builder, v, i64, "refl.zx");
    LLVMBuildRet(g->builder, v);
  }
  LLVMBasicBlockRef bdec = LLVMAppendBasicBlockInContext(g->ctx, fn, "ld.dec");
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_DECIMAL, 0), bdec);
  LLVMPositionBuilderAtEnd(g->builder, bdec);
  LLVMValueRef dv = LLVMBuildLoad2(
      g->builder, dec_ty,
      LLVMBuildBitCast(g->builder, p, LLVMPointerType(dec_ty, 0), "refl.dp"),
      "refl.dv");
  LLVMBuildRet(g->builder, refl_decimal_to_i64(g, dv));

  LLVMBasicBlockRef bnull =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "ld.null");
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_NULLABLE, 0), bnull);
  LLVMPositionBuilderAtEnd(g->builder, bnull);
  LLVMBasicBlockRef bnull_call =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "ld.nullc");
  LLVMBuildCondBr(g->builder, LLVMBuildIsNotNull(g->builder, vops, "refl.vh"),
                  bnull_call, zero);
  LLVMPositionBuilderAtEnd(g->builder, bnull_call);
  LLVMBuildRet(g->builder,
               refl_call_vops(g, vops, ZAN_REFL_VOP_GET_I64, p, NULL, NULL));
}

static LLVMValueRef refl_get_i64_at_fn(zan_irgen_t *g) {
  if (g->fn_refl_get_i64_at)
    return g->fn_refl_get_i64_at;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i16 = LLVMInt16TypeInContext(g->ctx);
  LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef flt = LLVMFloatTypeInContext(g->ctx);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i64}, 3, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_get_i64_at", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_get_i64_at = fn;

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMValueRef save_fn = g->current_fn;
  g->current_fn = fn;
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef have = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
  LLVMBasicBlockRef propchk =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "propchk");
  LLVMBasicBlockRef prop = LLVMAppendBasicBlockInContext(g->ctx, fn, "prop");
  LLVMBasicBlockRef kindchk =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "kindchk");
  LLVMBasicBlockRef konst = LLVMAppendBasicBlockInContext(g->ctx, fn, "const");
  LLVMBasicBlockRef load = LLVMAppendBasicBlockInContext(g->ctx, fn, "load");
  LLVMBasicBlockRef slot = LLVMAppendBasicBlockInContext(g->ctx, fn, "slot");
  LLVMBasicBlockRef zero = LLVMAppendBasicBlockInContext(g->ctx, fn, "zero");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef obj = LLVMGetParam(fn, 1);
  LLVMValueRef idx = LLVMGetParam(fn, 2);

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
      LLVMBuildIsNotNull(g->builder, fields, "refl.fnn"), "refl.valid");
  LLVMBuildCondBr(g->builder, valid, have, zero);

  LLVMPositionBuilderAtEnd(g->builder, have);
  LLVMValueRef fe = refl_member_row(g, fields, idx, false);
  LLVMValueRef fflags = refl_member_u32(g, fe, false, 8);
  LLVMValueRef is_prop = LLVMBuildAnd(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntNE,
                    LLVMBuildAnd(g->builder, fflags,
                                 LLVMConstInt(i64, ZAN_REFL_FF_PROPERTY, 0),
                                 "refl.pf"),
                    LLVMConstInt(i64, 0, 0), "refl.isp"),
      LLVMBuildIsNotNull(g->builder, obj, "refl.objnn"), "refl.tryprop");
  LLVMBuildCondBr(g->builder, is_prop, propchk, kindchk);

  LLVMPositionBuilderAtEnd(g->builder, propchk);
  LLVMValueRef fname = refl_field_ptr(g, fields, fe, 0);
  LLVMValueRef pk = NULL;
  LLVMValueRef pv = refl_call_pget(g, ti, obj, fname, &pk);
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntNE, pk,
                                LLVMConstInt(i64, 0, 0), "refl.hasprop"),
                  prop, kindchk);

  LLVMPositionBuilderAtEnd(g->builder, prop);
  LLVMBuildRet(g->builder, refl_pget_as_i64(g, pv, pk));

  LLVMPositionBuilderAtEnd(g->builder, kindchk);
  LLVMValueRef kind = refl_member_u32(g, fe, false, 7);
  LLVMValueRef foff = refl_field_offset(g, fe);
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                                LLVMConstInt(i64, ZAN_REFL_FK_ENUM_MEMBER, 0),
                                "refl.isem"),
                  konst, load);

  LLVMPositionBuilderAtEnd(g->builder, konst);
  LLVMBuildRet(g->builder, foff);

  LLVMPositionBuilderAtEnd(g->builder, zero);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

  LLVMPositionBuilderAtEnd(g->builder, load);
  LLVMBuildCondBr(g->builder, LLVMBuildIsNull(g->builder, obj, "refl.objnull"),
                  zero, slot);

  LLVMPositionBuilderAtEnd(g->builder, slot);
  LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, obj, &foff, 1, "refl.slot");
  LLVMValueRef vops = refl_field_ptr(g, fields, fe, 3);
  refl_emit_load_i64_slot(g, fn, p, kind, vops, zero);

  g->current_fn = save_fn;
  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_get_i64_fn(zan_irgen_t *g) {
  if (g->fn_refl_get_i64)
    return g->fn_refl_get_i64;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i8ptr}, 3, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_get_i64", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_get_i64 = fn;
  LLVMValueRef find = refl_find_fn(g);
  LLVMValueRef get_at = refl_get_i64_at_fn(g);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMValueRef save_fn = g->current_fn;
  g->current_fn = fn;
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef prop = LLVMAppendBasicBlockInContext(g->ctx, fn, "prop");
  LLVMBasicBlockRef lookup =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "lookup");
  LLVMBasicBlockRef have = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
  LLVMBasicBlockRef zero = LLVMAppendBasicBlockInContext(g->ctx, fn, "zero");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef obj = LLVMGetParam(fn, 1);
  LLVMValueRef name = LLVMGetParam(fn, 2);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef pk = NULL;
  LLVMValueRef pv = refl_call_pget(g, ti, obj, name, &pk);
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntNE, pk,
                                LLVMConstInt(i64, 0, 0), "refl.hasprop"),
                  prop, lookup);

  LLVMPositionBuilderAtEnd(g->builder, prop);
  LLVMBuildRet(g->builder, refl_pget_as_i64(g, pv, pk));

  LLVMPositionBuilderAtEnd(g->builder, lookup);
  LLVMValueRef idx = zan_call2(
      g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
      find, (LLVMValueRef[]){ti, name}, 2, "refl.idx");
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntSLT, idx,
                                LLVMConstInt(i64, 0, 0), "refl.nf"),
                  zero, have);

  LLVMPositionBuilderAtEnd(g->builder, have);
  LLVMBuildRet(
      g->builder,
      zan_call2(
          g->builder,
          LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i64}, 3, 0),
          get_at, (LLVMValueRef[]){ti, obj, idx}, 3, "refl.at"));

  LLVMPositionBuilderAtEnd(g->builder, zero);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

  g->current_fn = save_fn;
  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_get_f64_at_fn(zan_irgen_t *g) {
  if (g->fn_refl_get_f64_at)
    return g->fn_refl_get_f64_at;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef flt = LLVMFloatTypeInContext(g->ctx);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(dbl, (LLVMTypeRef[]){i8ptr, i8ptr, i64}, 3, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_get_f64_at", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_get_f64_at = fn;
  LLVMValueRef geti_at = refl_get_i64_at_fn(g);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMValueRef save_fn = g->current_fn;
  g->current_fn = fn;
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef have = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
  LLVMBasicBlockRef propchk =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "propchk");
  LLVMBasicBlockRef prop = LLVMAppendBasicBlockInContext(g->ctx, fn, "prop");
  LLVMBasicBlockRef objchk =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "objchk");
  LLVMBasicBlockRef kinds = LLVMAppendBasicBlockInContext(g->ctx, fn, "kinds");
  LLVMBasicBlockRef ld32 = LLVMAppendBasicBlockInContext(g->ctx, fn, "f32");
  LLVMBasicBlockRef ld64 = LLVMAppendBasicBlockInContext(g->ctx, fn, "f64");
  LLVMBasicBlockRef lddec = LLVMAppendBasicBlockInContext(g->ctx, fn, "dec");
  LLVMBasicBlockRef ldnull = LLVMAppendBasicBlockInContext(g->ctx, fn, "null");
  LLVMBasicBlockRef ldnullc =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "nullc");
  LLVMBasicBlockRef via_i =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "via_int");
  LLVMBasicBlockRef zero = LLVMAppendBasicBlockInContext(g->ctx, fn, "zero");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef obj = LLVMGetParam(fn, 1);
  LLVMValueRef idx = LLVMGetParam(fn, 2);

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
      LLVMBuildIsNotNull(g->builder, fields, "refl.fnn"), "refl.valid");
  LLVMBuildCondBr(g->builder, valid, have, zero);

  LLVMPositionBuilderAtEnd(g->builder, have);
  LLVMValueRef fe = refl_member_row(g, fields, idx, false);
  LLVMValueRef fflags = refl_member_u32(g, fe, false, 8);
  LLVMValueRef is_prop = LLVMBuildAnd(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntNE,
                    LLVMBuildAnd(g->builder, fflags,
                                 LLVMConstInt(i64, ZAN_REFL_FF_PROPERTY, 0),
                                 "refl.pf"),
                    LLVMConstInt(i64, 0, 0), "refl.isp"),
      LLVMBuildIsNotNull(g->builder, obj, "refl.objnn"), "refl.tryprop");
  LLVMBuildCondBr(g->builder, is_prop, propchk, objchk);

  LLVMPositionBuilderAtEnd(g->builder, propchk);
  LLVMValueRef fname = refl_field_ptr(g, fields, fe, 0);
  LLVMValueRef pk = NULL;
  LLVMValueRef pv = refl_call_pget(g, ti, obj, fname, &pk);
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntNE, pk,
                                LLVMConstInt(i64, 0, 0), "refl.hasprop"),
                  prop, objchk);

  LLVMPositionBuilderAtEnd(g->builder, prop);
  LLVMBuildRet(g->builder, refl_pget_as_f64(g, pv, pk));

  LLVMPositionBuilderAtEnd(g->builder, objchk);
  LLVMBuildCondBr(g->builder, LLVMBuildIsNull(g->builder, obj, "refl.objnull"),
                  via_i, kinds);

  LLVMPositionBuilderAtEnd(g->builder, kinds);
  LLVMValueRef kind = refl_member_u32(g, fe, false, 7);
  LLVMValueRef foff = refl_field_offset(g, fe);
  LLVMValueRef sw = LLVMBuildSwitch(g->builder, kind, via_i, 4);
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_FLOAT, 0), ld32);
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_DOUBLE, 0), ld64);
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_DECIMAL, 0), lddec);
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_NULLABLE, 0), ldnull);

  LLVMPositionBuilderAtEnd(g->builder, ld32);
  LLVMValueRef p32 = LLVMBuildGEP2(g->builder, i8, obj, &foff, 1, "refl.s32");
  LLVMValueRef v32 = LLVMBuildLoad2(
      g->builder, flt,
      LLVMBuildBitCast(g->builder, p32, LLVMPointerType(flt, 0), "refl.p32"),
      "refl.v32");
  LLVMBuildRet(g->builder, LLVMBuildFPExt(g->builder, v32, dbl, "refl.ext"));

  LLVMPositionBuilderAtEnd(g->builder, ld64);
  LLVMValueRef p64 = LLVMBuildGEP2(g->builder, i8, obj, &foff, 1, "refl.s64");
  LLVMBuildRet(
      g->builder,
      LLVMBuildLoad2(g->builder, dbl,
                     LLVMBuildBitCast(g->builder, p64, LLVMPointerType(dbl, 0),
                                      "refl.p64"),
                     "refl.v64"));

  LLVMPositionBuilderAtEnd(g->builder, lddec);
  LLVMTypeRef dec_ty = get_decimal_type(g);
  LLVMValueRef pdec = LLVMBuildGEP2(g->builder, i8, obj, &foff, 1, "refl.sdec");
  LLVMValueRef dv = LLVMBuildLoad2(
      g->builder, dec_ty,
      LLVMBuildBitCast(g->builder, pdec, LLVMPointerType(dec_ty, 0),
                       "refl.pdec"),
      "refl.dv");
  LLVMBuildRet(g->builder, refl_decimal_to_f64(g, dv));

  LLVMPositionBuilderAtEnd(g->builder, ldnull);
  LLVMValueRef vops = refl_field_ptr(g, fields, fe, 3);
  LLVMBuildCondBr(g->builder, LLVMBuildIsNotNull(g->builder, vops, "refl.vh"),
                  ldnullc, zero);

  LLVMPositionBuilderAtEnd(g->builder, ldnullc);
  LLVMValueRef pn = LLVMBuildGEP2(g->builder, i8, obj, &foff, 1, "refl.sn");
  LLVMValueRef fb = refl_call_vops(g, vops, ZAN_REFL_VOP_GET_F64, pn, NULL, NULL);
  LLVMBuildRet(g->builder, LLVMBuildBitCast(g->builder, fb, dbl, "refl.nf64"));

  LLVMPositionBuilderAtEnd(g->builder, via_i);
  LLVMValueRef iv = zan_call2(
      g->builder,
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i64}, 3, 0), geti_at,
      (LLVMValueRef[]){ti, obj, idx}, 3, "refl.iv");
  LLVMBuildRet(g->builder, LLVMBuildSIToFP(g->builder, iv, dbl, "refl.i2f"));

  LLVMPositionBuilderAtEnd(g->builder, zero);
  LLVMBuildRet(g->builder, LLVMConstReal(dbl, 0.0));

  g->current_fn = save_fn;
  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_get_f64_fn(zan_irgen_t *g) {
  if (g->fn_refl_get_f64)
    return g->fn_refl_get_f64;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(dbl, (LLVMTypeRef[]){i8ptr, i8ptr, i8ptr}, 3, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_get_f64", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_get_f64 = fn;
  LLVMValueRef find = refl_find_fn(g);
  LLVMValueRef get_at = refl_get_f64_at_fn(g);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef prop = LLVMAppendBasicBlockInContext(g->ctx, fn, "prop");
  LLVMBasicBlockRef notprop =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "notprop");
  LLVMBasicBlockRef have = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
  LLVMBasicBlockRef zero = LLVMAppendBasicBlockInContext(g->ctx, fn, "zero");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef obj = LLVMGetParam(fn, 1);
  LLVMValueRef name = LLVMGetParam(fn, 2);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef pk = NULL;
  LLVMValueRef pv = refl_call_pget(g, ti, obj, name, &pk);
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntNE, pk,
                                LLVMConstInt(i64, 0, 0), "refl.hasprop"),
                  prop, notprop);

  LLVMPositionBuilderAtEnd(g->builder, prop);
  LLVMBuildRet(g->builder, refl_pget_as_f64(g, pv, pk));

  LLVMPositionBuilderAtEnd(g->builder, notprop);
  LLVMValueRef idx = zan_call2(
      g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
      find, (LLVMValueRef[]){ti, name}, 2, "refl.idx");
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntSLT, idx,
                                LLVMConstInt(i64, 0, 0), "refl.nf"),
                  zero, have);

  LLVMPositionBuilderAtEnd(g->builder, have);
  LLVMBuildRet(
      g->builder,
      zan_call2(
          g->builder,
          LLVMFunctionType(dbl, (LLVMTypeRef[]){i8ptr, i8ptr, i64}, 3, 0),
          get_at, (LLVMValueRef[]){ti, obj, idx}, 3, "refl.at"));

  LLVMPositionBuilderAtEnd(g->builder, zero);
  LLVMBuildRet(g->builder, LLVMConstReal(dbl, 0.0));

  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_get_str_at_fn(zan_irgen_t *g) {
  if (g->fn_refl_get_str_at)
    return g->fn_refl_get_str_at;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr, i64}, 3, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_get_str_at", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_get_str_at = fn;
  LLVMValueRef empty = refl_empty_string(g);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef have = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
  LLVMBasicBlockRef propchk =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "propchk");
  LLVMBasicBlockRef prop = LLVMAppendBasicBlockInContext(g->ctx, fn, "prop");
  LLVMBasicBlockRef chk = LLVMAppendBasicBlockInContext(g->ctx, fn, "chk");
  LLVMBasicBlockRef load = LLVMAppendBasicBlockInContext(g->ctx, fn, "load");
  LLVMBasicBlockRef none = LLVMAppendBasicBlockInContext(g->ctx, fn, "none");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef obj = LLVMGetParam(fn, 1);
  LLVMValueRef idx = LLVMGetParam(fn, 2);

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
  LLVMBuildCondBr(g->builder, valid, have, none);

  LLVMPositionBuilderAtEnd(g->builder, have);
  LLVMValueRef fe = refl_member_row(g, fields, idx, false);
  LLVMValueRef fflags = refl_member_u32(g, fe, false, 8);
  LLVMValueRef is_prop = LLVMBuildICmp(
      g->builder, LLVMIntNE,
      LLVMBuildAnd(g->builder, fflags,
                   LLVMConstInt(i64, ZAN_REFL_FF_PROPERTY, 0), "refl.pf"),
      LLVMConstInt(i64, 0, 0), "refl.isp");
  LLVMBuildCondBr(g->builder, is_prop, propchk, chk);

  LLVMPositionBuilderAtEnd(g->builder, propchk);
  LLVMValueRef fname = refl_field_ptr(g, fields, fe, 0);
  LLVMValueRef pk = NULL;
  LLVMValueRef pv = refl_call_pget(g, ti, obj, fname, &pk);
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntEQ, pk,
                                LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0),
                                "refl.propstr"),
                  prop, chk);

  LLVMPositionBuilderAtEnd(g->builder, prop);
  LLVMBuildRet(g->builder, LLVMBuildIntToPtr(g->builder, pv, i8ptr, "refl.psv"));

  LLVMPositionBuilderAtEnd(g->builder, chk);
  LLVMValueRef kind = refl_member_u32(g, fe, false, 7);
  LLVMValueRef foff = refl_field_offset(g, fe);
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                                LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0),
                                "refl.isstr"),
                  load, none);

  LLVMPositionBuilderAtEnd(g->builder, load);
  LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, obj, &foff, 1, "refl.slot");
  LLVMValueRef sv = LLVMBuildLoad2(
      g->builder, i8ptr,
      LLVMBuildBitCast(g->builder, p, LLVMPointerType(i8ptr, 0), "refl.sp"),
      "refl.sv");
  LLVMTypeRef arc_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                        (LLVMTypeRef[]){i8ptr}, 1, 0);
  zan_call2(g->builder, arc_ty, g->rt_str_retain, &sv, 1, "");
  LLVMBuildRet(g->builder, sv);

  LLVMPositionBuilderAtEnd(g->builder, none);
  LLVMBuildRet(g->builder, empty);

  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_get_str_fn(zan_irgen_t *g) {
  if (g->fn_refl_get_str)
    return g->fn_refl_get_str;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr, i8ptr}, 3, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_get_str", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_get_str = fn;
  LLVMValueRef find = refl_find_fn(g);
  LLVMValueRef get_at = refl_get_str_at_fn(g);
  LLVMValueRef empty = refl_empty_string(g);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef prop = LLVMAppendBasicBlockInContext(g->ctx, fn, "prop");
  LLVMBasicBlockRef notprop =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "notprop");
  LLVMBasicBlockRef have = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
  LLVMBasicBlockRef chk = LLVMAppendBasicBlockInContext(g->ctx, fn, "chk");
  LLVMBasicBlockRef none = LLVMAppendBasicBlockInContext(g->ctx, fn, "none");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef obj = LLVMGetParam(fn, 1);
  LLVMValueRef name = LLVMGetParam(fn, 2);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef pk = NULL;
  LLVMValueRef pv = refl_call_pget(g, ti, obj, name, &pk);
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntEQ, pk,
                                LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0),
                                "refl.propstr"),
                  prop, notprop);

  LLVMPositionBuilderAtEnd(g->builder, prop);
  LLVMValueRef psv = LLVMBuildIntToPtr(g->builder, pv, i8ptr, "refl.psv");
  LLVMBuildRet(g->builder, psv);

  LLVMPositionBuilderAtEnd(g->builder, notprop);
  LLVMBuildCondBr(g->builder, LLVMBuildIsNull(g->builder, obj, "refl.objnull"),
                  none, have);

  LLVMPositionBuilderAtEnd(g->builder, have);
  LLVMValueRef idx = zan_call2(
      g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
      find, (LLVMValueRef[]){ti, name}, 2, "refl.idx");
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntSLT, idx,
                                LLVMConstInt(i64, 0, 0), "refl.nf"),
                  none, chk);

  LLVMPositionBuilderAtEnd(g->builder, chk);
  LLVMBuildRet(
      g->builder,
      zan_call2(
          g->builder,
          LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr, i64}, 3, 0),
          get_at, (LLVMValueRef[]){ti, obj, idx}, 3, "refl.at"));

  LLVMPositionBuilderAtEnd(g->builder, none);
  LLVMBuildRet(g->builder, empty);

  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_get_obj_at_fn(zan_irgen_t *g) {
  if (g->fn_refl_get_obj_at)
    return g->fn_refl_get_obj_at;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr, i64}, 3, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_get_obj_at", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_get_obj_at = fn;
  LLVMValueRef null_obj = LLVMConstNull(i8ptr);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef have = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
  LLVMBasicBlockRef propchk =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "propchk");
  LLVMBasicBlockRef prop = LLVMAppendBasicBlockInContext(g->ctx, fn, "prop");
  LLVMBasicBlockRef chk = LLVMAppendBasicBlockInContext(g->ctx, fn, "chk");
  LLVMBasicBlockRef valchk =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "valchk");
  LLVMBasicBlockRef valcall =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "valcall");
  LLVMBasicBlockRef load = LLVMAppendBasicBlockInContext(g->ctx, fn, "load");
  LLVMBasicBlockRef load_ret =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "load.ret");
  LLVMBasicBlockRef load_retain =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "load.retain");
  LLVMBasicBlockRef none = LLVMAppendBasicBlockInContext(g->ctx, fn, "none");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef obj = LLVMGetParam(fn, 1);
  LLVMValueRef idx = LLVMGetParam(fn, 2);

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
  LLVMBuildCondBr(g->builder, valid, have, none);

  LLVMPositionBuilderAtEnd(g->builder, have);
  LLVMValueRef fe = refl_member_row(g, fields, idx, false);
  LLVMValueRef fflags = refl_member_u32(g, fe, false, 8);
  LLVMValueRef is_prop = LLVMBuildICmp(
      g->builder, LLVMIntNE,
      LLVMBuildAnd(g->builder, fflags,
                   LLVMConstInt(i64, ZAN_REFL_FF_PROPERTY, 0), "refl.pf"),
      LLVMConstInt(i64, 0, 0), "refl.isp");
  LLVMBuildCondBr(g->builder, is_prop, propchk, chk);

  LLVMPositionBuilderAtEnd(g->builder, propchk);
  LLVMValueRef fname = refl_field_ptr(g, fields, fe, 0);
  LLVMValueRef pk = NULL;
  LLVMValueRef pv = refl_call_pget(g, ti, obj, fname, &pk);
  LLVMValueRef is_prop_ref = LLVMBuildOr(
      g->builder,
      LLVMBuildOr(
          g->builder,
          LLVMBuildICmp(g->builder, LLVMIntEQ, pk,
                        LLVMConstInt(i64, ZAN_REFL_FK_REF, 0), "refl.propref"),
          LLVMBuildICmp(g->builder, LLVMIntEQ, pk,
                        LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0),
                        "refl.propstr"),
          "refl.propreforstr"),
      LLVMBuildICmp(g->builder, LLVMIntEQ, pk,
                    LLVMConstInt(i64, ZAN_REFL_FK_STRUCT, 0), "refl.propst"),
      "refl.propobj");
  LLVMBuildCondBr(g->builder, is_prop_ref, prop, chk);

  LLVMPositionBuilderAtEnd(g->builder, prop);
  LLVMBuildRet(g->builder, LLVMBuildIntToPtr(g->builder, pv, i8ptr, "refl.ppv"));

  LLVMPositionBuilderAtEnd(g->builder, chk);
  LLVMValueRef kind = refl_member_u32(g, fe, false, 7);
  LLVMValueRef foff = refl_field_offset(g, fe);
  LLVMValueRef is_ref = LLVMBuildOr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                    LLVMConstInt(i64, ZAN_REFL_FK_REF, 0), "refl.isref"),
      LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                    LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0), "refl.isstr"),
      "refl.isref_or_str");
  LLVMBuildCondBr(g->builder, is_ref, load, valchk);

  LLVMPositionBuilderAtEnd(g->builder, valchk);
  LLVMValueRef is_val = LLVMBuildOr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                    LLVMConstInt(i64, ZAN_REFL_FK_STRUCT, 0), "refl.isst"),
      LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                    LLVMConstInt(i64, ZAN_REFL_FK_NULLABLE, 0), "refl.isnu"),
      "refl.isval");
  LLVMValueRef vops = refl_field_ptr(g, fields, fe, 3);
  LLVMValueRef can_val =
      LLVMBuildAnd(g->builder, is_val,
                   LLVMBuildIsNotNull(g->builder, vops, "refl.vh"), "refl.cv");
  LLVMBuildCondBr(g->builder, can_val, valcall, none);

  LLVMPositionBuilderAtEnd(g->builder, valcall);
  LLVMValueRef vp = LLVMBuildGEP2(g->builder, i8, obj, &foff, 1, "refl.vslot");
  LLVMValueRef boxi =
      refl_call_vops(g, vops, ZAN_REFL_VOP_GET_OBJ, vp, NULL, NULL);
  LLVMBuildRet(g->builder, LLVMBuildIntToPtr(g->builder, boxi, i8ptr, "refl.bv"));

  LLVMTypeRef arc_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                        (LLVMTypeRef[]){i8ptr}, 1, 0);
  LLVMPositionBuilderAtEnd(g->builder, load);
  LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, obj, &foff, 1, "refl.slot");
  LLVMValueRef ov = LLVMBuildLoad2(
      g->builder, i8ptr,
      LLVMBuildBitCast(g->builder, p, LLVMPointerType(i8ptr, 0), "refl.op"),
      "refl.ov");
  LLVMBuildCondBr(g->builder, LLVMBuildIsNull(g->builder, ov, "refl.onull"),
                  load_ret, load_retain);

  LLVMPositionBuilderAtEnd(g->builder, load_retain);
  zan_call2(g->builder, arc_ty, g->rt_retain, &ov, 1, "");
  LLVMBuildBr(g->builder, load_ret);

  LLVMPositionBuilderAtEnd(g->builder, load_ret);
  LLVMBuildRet(g->builder, ov);

  LLVMPositionBuilderAtEnd(g->builder, none);
  LLVMBuildRet(g->builder, null_obj);

  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_get_obj_fn(zan_irgen_t *g) {
  if (g->fn_refl_get_obj)
    return g->fn_refl_get_obj;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr, i8ptr}, 3, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_get_obj", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_get_obj = fn;
  LLVMValueRef find = refl_find_fn(g);
  LLVMValueRef get_at = refl_get_obj_at_fn(g);
  LLVMValueRef null_obj = LLVMConstNull(i8ptr);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef prop = LLVMAppendBasicBlockInContext(g->ctx, fn, "prop");
  LLVMBasicBlockRef notprop =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "notprop");
  LLVMBasicBlockRef have = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
  LLVMBasicBlockRef chk = LLVMAppendBasicBlockInContext(g->ctx, fn, "chk");
  LLVMBasicBlockRef none = LLVMAppendBasicBlockInContext(g->ctx, fn, "none");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef obj = LLVMGetParam(fn, 1);
  LLVMValueRef name = LLVMGetParam(fn, 2);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef pk = NULL;
  LLVMValueRef pv = refl_call_pget(g, ti, obj, name, &pk);
  LLVMValueRef is_prop_ref = LLVMBuildOr(
      g->builder,
      LLVMBuildOr(
          g->builder,
          LLVMBuildICmp(g->builder, LLVMIntEQ, pk,
                        LLVMConstInt(i64, ZAN_REFL_FK_REF, 0), "refl.propref"),
          LLVMBuildICmp(g->builder, LLVMIntEQ, pk,
                        LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0),
                        "refl.propstr"),
          "refl.propreforstr"),
      LLVMBuildICmp(g->builder, LLVMIntEQ, pk,
                    LLVMConstInt(i64, ZAN_REFL_FK_STRUCT, 0), "refl.propst"),
      "refl.propobj");
  LLVMBuildCondBr(g->builder, is_prop_ref, prop, notprop);

  LLVMPositionBuilderAtEnd(g->builder, prop);
  LLVMValueRef ppv = LLVMBuildIntToPtr(g->builder, pv, i8ptr, "refl.ppv");
  LLVMBuildRet(g->builder, ppv);

  LLVMPositionBuilderAtEnd(g->builder, notprop);
  LLVMBuildCondBr(g->builder, LLVMBuildIsNull(g->builder, obj, "refl.objnull"),
                  none, have);

  LLVMPositionBuilderAtEnd(g->builder, have);
  LLVMValueRef idx = zan_call2(
      g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
      find, (LLVMValueRef[]){ti, name}, 2, "refl.idx");
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntSLT, idx,
                                LLVMConstInt(i64, 0, 0), "refl.nf"),
                  none, chk);

  LLVMPositionBuilderAtEnd(g->builder, chk);
  LLVMBuildRet(
      g->builder,
      zan_call2(
          g->builder,
          LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr, i64}, 3, 0),
          get_at, (LLVMValueRef[]){ti, obj, idx}, 3, "refl.at"));

  LLVMPositionBuilderAtEnd(g->builder, none);
  LLVMBuildRet(g->builder, null_obj);

  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_sget_i64_fn(zan_irgen_t *g) {
  if (g->fn_refl_sget_i64)
    return g->fn_refl_sget_i64;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_sget_i64", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_sget_i64 = fn;
  LLVMValueRef sfind = refl_sfind_fn(g);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMValueRef save_fn = g->current_fn;
  g->current_fn = fn;
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef prop = LLVMAppendBasicBlockInContext(g->ctx, fn, "prop");
  LLVMBasicBlockRef lookup =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "lookup");
  LLVMBasicBlockRef gchk = LLVMAppendBasicBlockInContext(g->ctx, fn, "gchk");
  LLVMBasicBlockRef slot = LLVMAppendBasicBlockInContext(g->ctx, fn, "slot");
  LLVMBasicBlockRef zero = LLVMAppendBasicBlockInContext(g->ctx, fn, "zero");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef name = LLVMGetParam(fn, 1);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef pk = NULL;
  LLVMValueRef pv = refl_call_spget(g, ti, name, &pk);
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntNE, pk, LLVMConstInt(i64, 0, 0),
                    "refl.hasprop"),
      prop, lookup);

  LLVMPositionBuilderAtEnd(g->builder, prop);
  LLVMBuildRet(g->builder, refl_pget_as_i64(g, pv, pk));

  LLVMPositionBuilderAtEnd(g->builder, lookup);
  LLVMValueRef idx = zan_call2(
      g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
      sfind, (LLVMValueRef[]){ti, name}, 2, "refl.idx");
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntSLT, idx, LLVMConstInt(i64, 0, 0),
                    "refl.nf"),
      zero, gchk);

  LLVMPositionBuilderAtEnd(g->builder, gchk);
  LLVMValueRef sfields =
      refl_hdr_ptr(g, ti, ZAN_REFL_SFIELDS_OFF, "refl.sfields");
  LLVMValueRef fe = refl_member_row(g, sfields, idx, false);
  LLVMValueRef p = refl_field_ptr(g, sfields, fe, 3);
  LLVMBuildCondBr(g->builder, LLVMBuildIsNotNull(g->builder, p, "refl.gpnn"),
                  slot, zero);

  LLVMPositionBuilderAtEnd(g->builder, slot);
  LLVMValueRef kind = refl_member_u32(g, fe, false, 7);
  LLVMValueRef vops = LLVMBuildIntToPtr(
      g->builder, refl_field_offset(g, fe), i8ptr, "refl.svops");
  refl_emit_load_i64_slot(g, fn, p, kind, vops, zero);

  LLVMPositionBuilderAtEnd(g->builder, zero);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

  g->current_fn = save_fn;
  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_sget_f64_fn(zan_irgen_t *g) {
  if (g->fn_refl_sget_f64)
    return g->fn_refl_sget_f64;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef flt = LLVMFloatTypeInContext(g->ctx);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(dbl, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_sget_f64", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_sget_f64 = fn;
  LLVMValueRef sfind = refl_sfind_fn(g);
  LLVMValueRef sgeti = refl_sget_i64_fn(g);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMValueRef save_fn = g->current_fn;
  g->current_fn = fn;
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef prop = LLVMAppendBasicBlockInContext(g->ctx, fn, "prop");
  LLVMBasicBlockRef lookup =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "lookup");
  LLVMBasicBlockRef gchk = LLVMAppendBasicBlockInContext(g->ctx, fn, "gchk");
  LLVMBasicBlockRef kinds = LLVMAppendBasicBlockInContext(g->ctx, fn, "kinds");
  LLVMBasicBlockRef ld32 = LLVMAppendBasicBlockInContext(g->ctx, fn, "f32");
  LLVMBasicBlockRef ld64 = LLVMAppendBasicBlockInContext(g->ctx, fn, "f64");
  LLVMBasicBlockRef lddec = LLVMAppendBasicBlockInContext(g->ctx, fn, "dec");
  LLVMBasicBlockRef ldnull = LLVMAppendBasicBlockInContext(g->ctx, fn, "null");
  LLVMBasicBlockRef ldnullc =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "nullc");
  LLVMBasicBlockRef via_i =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "via_int");
  LLVMBasicBlockRef zero = LLVMAppendBasicBlockInContext(g->ctx, fn, "zero");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef name = LLVMGetParam(fn, 1);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef pk = NULL;
  LLVMValueRef pv = refl_call_spget(g, ti, name, &pk);
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntNE, pk, LLVMConstInt(i64, 0, 0),
                    "refl.hasprop"),
      prop, lookup);

  LLVMPositionBuilderAtEnd(g->builder, prop);
  LLVMBuildRet(g->builder, refl_pget_as_f64(g, pv, pk));

  LLVMPositionBuilderAtEnd(g->builder, lookup);
  LLVMValueRef idx = zan_call2(
      g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
      sfind, (LLVMValueRef[]){ti, name}, 2, "refl.idx");
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntSLT, idx, LLVMConstInt(i64, 0, 0),
                    "refl.nf"),
      zero, gchk);

  LLVMPositionBuilderAtEnd(g->builder, gchk);
  LLVMValueRef sfields =
      refl_hdr_ptr(g, ti, ZAN_REFL_SFIELDS_OFF, "refl.sfields");
  LLVMValueRef fe = refl_member_row(g, sfields, idx, false);
  LLVMValueRef p = refl_field_ptr(g, sfields, fe, 3);
  LLVMBuildCondBr(g->builder, LLVMBuildIsNotNull(g->builder, p, "refl.gpnn"),
                  kinds, zero);

  LLVMPositionBuilderAtEnd(g->builder, kinds);
  LLVMValueRef kind = refl_member_u32(g, fe, false, 7);
  LLVMValueRef sw = LLVMBuildSwitch(g->builder, kind, via_i, 4);
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_FLOAT, 0), ld32);
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_DOUBLE, 0), ld64);
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_DECIMAL, 0), lddec);
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_NULLABLE, 0), ldnull);

  LLVMPositionBuilderAtEnd(g->builder, ld32);
  LLVMValueRef v32 = LLVMBuildLoad2(
      g->builder, flt,
      LLVMBuildBitCast(g->builder, p, LLVMPointerType(flt, 0), "refl.p32"),
      "refl.v32");
  LLVMBuildRet(g->builder, LLVMBuildFPExt(g->builder, v32, dbl, "refl.ext"));

  LLVMPositionBuilderAtEnd(g->builder, ld64);
  LLVMBuildRet(
      g->builder,
      LLVMBuildLoad2(
          g->builder, dbl,
          LLVMBuildBitCast(g->builder, p, LLVMPointerType(dbl, 0), "refl.p64"),
          "refl.v64"));

  LLVMPositionBuilderAtEnd(g->builder, lddec);
  LLVMTypeRef dec_ty = get_decimal_type(g);
  LLVMValueRef dv = LLVMBuildLoad2(
      g->builder, dec_ty,
      LLVMBuildBitCast(g->builder, p, LLVMPointerType(dec_ty, 0), "refl.pdec"),
      "refl.dv");
  LLVMBuildRet(g->builder, refl_decimal_to_f64(g, dv));

  LLVMPositionBuilderAtEnd(g->builder, ldnull);
  LLVMValueRef vops = LLVMBuildIntToPtr(
      g->builder, refl_field_offset(g, fe), i8ptr, "refl.svops");
  LLVMBuildCondBr(g->builder, LLVMBuildIsNotNull(g->builder, vops, "refl.vh"),
                  ldnullc, zero);

  LLVMPositionBuilderAtEnd(g->builder, ldnullc);
  LLVMValueRef fb = refl_call_vops(g, vops, ZAN_REFL_VOP_GET_F64, p, NULL, NULL);
  LLVMBuildRet(g->builder, LLVMBuildBitCast(g->builder, fb, dbl, "refl.nf64"));

  LLVMPositionBuilderAtEnd(g->builder, via_i);
  LLVMValueRef iv = zan_call2(
      g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
      sgeti, (LLVMValueRef[]){ti, name}, 2, "refl.iv");
  LLVMBuildRet(g->builder, LLVMBuildSIToFP(g->builder, iv, dbl, "refl.i2f"));

  LLVMPositionBuilderAtEnd(g->builder, zero);
  LLVMBuildRet(g->builder, LLVMConstReal(dbl, 0.0));

  g->current_fn = save_fn;
  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_sget_str_fn(zan_irgen_t *g) {
  if (g->fn_refl_sget_str)
    return g->fn_refl_sget_str;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_sget_str", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_sget_str = fn;
  LLVMValueRef sfind = refl_sfind_fn(g);
  LLVMValueRef empty = refl_empty_string(g);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef prop = LLVMAppendBasicBlockInContext(g->ctx, fn, "prop");
  LLVMBasicBlockRef lookup =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "lookup");
  LLVMBasicBlockRef chk = LLVMAppendBasicBlockInContext(g->ctx, fn, "chk");
  LLVMBasicBlockRef load = LLVMAppendBasicBlockInContext(g->ctx, fn, "load");
  LLVMBasicBlockRef none = LLVMAppendBasicBlockInContext(g->ctx, fn, "none");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef name = LLVMGetParam(fn, 1);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef pk = NULL;
  LLVMValueRef pv = refl_call_spget(g, ti, name, &pk);
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntEQ, pk,
                    LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0), "refl.propstr"),
      prop, lookup);

  LLVMPositionBuilderAtEnd(g->builder, prop);
  LLVMBuildRet(g->builder, LLVMBuildIntToPtr(g->builder, pv, i8ptr, "refl.psv"));

  LLVMPositionBuilderAtEnd(g->builder, lookup);
  LLVMValueRef idx = zan_call2(
      g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
      sfind, (LLVMValueRef[]){ti, name}, 2, "refl.idx");
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntSLT, idx, LLVMConstInt(i64, 0, 0),
                    "refl.nf"),
      none, chk);

  LLVMPositionBuilderAtEnd(g->builder, chk);
  LLVMValueRef sfields =
      refl_hdr_ptr(g, ti, ZAN_REFL_SFIELDS_OFF, "refl.sfields");
  LLVMValueRef fe = refl_member_row(g, sfields, idx, false);
  LLVMValueRef kind = refl_member_u32(g, fe, false, 7);
  LLVMValueRef p = refl_field_ptr(g, sfields, fe, 3);
  LLVMValueRef ok = LLVMBuildAnd(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                    LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0), "refl.isstr"),
      LLVMBuildIsNotNull(g->builder, p, "refl.gpnn"), "refl.strok");
  LLVMBuildCondBr(g->builder, ok, load, none);

  LLVMPositionBuilderAtEnd(g->builder, load);
  LLVMValueRef sv = LLVMBuildLoad2(
      g->builder, i8ptr,
      LLVMBuildBitCast(g->builder, p, LLVMPointerType(i8ptr, 0), "refl.sp"),
      "refl.sv");
  LLVMTypeRef arc_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                        (LLVMTypeRef[]){i8ptr}, 1, 0);
  zan_call2(g->builder, arc_ty, g->rt_str_retain, &sv, 1, "");
  LLVMBuildRet(g->builder, sv);

  LLVMPositionBuilderAtEnd(g->builder, none);
  LLVMBuildRet(g->builder, empty);

  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_sget_obj_fn(zan_irgen_t *g) {
  if (g->fn_refl_sget_obj)
    return g->fn_refl_sget_obj;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_sget_obj", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_sget_obj = fn;
  LLVMValueRef sfind = refl_sfind_fn(g);
  LLVMValueRef null_obj = LLVMConstNull(i8ptr);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef prop = LLVMAppendBasicBlockInContext(g->ctx, fn, "prop");
  LLVMBasicBlockRef lookup =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "lookup");
  LLVMBasicBlockRef chk = LLVMAppendBasicBlockInContext(g->ctx, fn, "chk");
  LLVMBasicBlockRef valchk =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "valchk");
  LLVMBasicBlockRef valcall =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "valcall");
  LLVMBasicBlockRef load = LLVMAppendBasicBlockInContext(g->ctx, fn, "load");
  LLVMBasicBlockRef load_retain =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "load.retain");
  LLVMBasicBlockRef load_ret =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "load.ret");
  LLVMBasicBlockRef none = LLVMAppendBasicBlockInContext(g->ctx, fn, "none");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef name = LLVMGetParam(fn, 1);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef pk = NULL;
  LLVMValueRef pv = refl_call_spget(g, ti, name, &pk);
  LLVMValueRef is_prop_ref = LLVMBuildOr(
      g->builder,
      LLVMBuildOr(
          g->builder,
          LLVMBuildICmp(g->builder, LLVMIntEQ, pk,
                        LLVMConstInt(i64, ZAN_REFL_FK_REF, 0), "refl.propref"),
          LLVMBuildICmp(g->builder, LLVMIntEQ, pk,
                        LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0),
                        "refl.propstr"),
          "refl.propreforstr"),
      LLVMBuildICmp(g->builder, LLVMIntEQ, pk,
                    LLVMConstInt(i64, ZAN_REFL_FK_STRUCT, 0), "refl.propst"),
      "refl.propobj");
  LLVMBuildCondBr(g->builder, is_prop_ref, prop, lookup);

  LLVMPositionBuilderAtEnd(g->builder, prop);
  LLVMBuildRet(g->builder, LLVMBuildIntToPtr(g->builder, pv, i8ptr, "refl.ppv"));

  LLVMPositionBuilderAtEnd(g->builder, lookup);
  LLVMValueRef idx = zan_call2(
      g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
      sfind, (LLVMValueRef[]){ti, name}, 2, "refl.idx");
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntSLT, idx, LLVMConstInt(i64, 0, 0),
                    "refl.nf"),
      none, chk);

  LLVMPositionBuilderAtEnd(g->builder, chk);
  LLVMValueRef sfields =
      refl_hdr_ptr(g, ti, ZAN_REFL_SFIELDS_OFF, "refl.sfields");
  LLVMValueRef fe = refl_member_row(g, sfields, idx, false);
  LLVMValueRef kind = refl_member_u32(g, fe, false, 7);
  LLVMValueRef p = refl_field_ptr(g, sfields, fe, 3);
  LLVMValueRef is_ref = LLVMBuildOr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                    LLVMConstInt(i64, ZAN_REFL_FK_REF, 0), "refl.isref"),
      LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                    LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0), "refl.isstr"),
      "refl.isref_or_str");
  LLVMValueRef ok =
      LLVMBuildAnd(g->builder, is_ref,
                   LLVMBuildIsNotNull(g->builder, p, "refl.gpnn"), "refl.ok");
  LLVMBuildCondBr(g->builder, ok, load, valchk);

  LLVMPositionBuilderAtEnd(g->builder, valchk);
  LLVMValueRef is_val = LLVMBuildOr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                    LLVMConstInt(i64, ZAN_REFL_FK_STRUCT, 0), "refl.isst"),
      LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                    LLVMConstInt(i64, ZAN_REFL_FK_NULLABLE, 0), "refl.isnu"),
      "refl.isval");
  LLVMValueRef vops = LLVMBuildIntToPtr(
      g->builder, refl_field_offset(g, fe), i8ptr, "refl.svops");
  LLVMValueRef can_val = LLVMBuildAnd(
      g->builder,
      LLVMBuildAnd(g->builder, is_val,
                   LLVMBuildIsNotNull(g->builder, p, "refl.vpnn"), "refl.cv0"),
      LLVMBuildIsNotNull(g->builder, vops, "refl.vh"), "refl.cv");
  LLVMBuildCondBr(g->builder, can_val, valcall, none);

  LLVMPositionBuilderAtEnd(g->builder, valcall);
  LLVMValueRef boxi =
      refl_call_vops(g, vops, ZAN_REFL_VOP_GET_OBJ, p, NULL, NULL);
  LLVMBuildRet(g->builder, LLVMBuildIntToPtr(g->builder, boxi, i8ptr, "refl.bv"));

  LLVMTypeRef arc_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                        (LLVMTypeRef[]){i8ptr}, 1, 0);
  LLVMPositionBuilderAtEnd(g->builder, load);
  LLVMValueRef ov = LLVMBuildLoad2(
      g->builder, i8ptr,
      LLVMBuildBitCast(g->builder, p, LLVMPointerType(i8ptr, 0), "refl.op"),
      "refl.ov");
  LLVMBuildCondBr(g->builder, LLVMBuildIsNull(g->builder, ov, "refl.onull"),
                  load_ret, load_retain);

  LLVMPositionBuilderAtEnd(g->builder, load_retain);
  zan_call2(g->builder, arc_ty, g->rt_retain, &ov, 1, "");
  LLVMBuildBr(g->builder, load_ret);

  LLVMPositionBuilderAtEnd(g->builder, load_ret);
  LLVMBuildRet(g->builder, ov);

  LLVMPositionBuilderAtEnd(g->builder, none);
  LLVMBuildRet(g->builder, null_obj);

  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static LLVMValueRef refl_fname_fn(zan_irgen_t *g) {
  if (g->fn_refl_fname)
    return g->fn_refl_fname;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i64, i64}, 3, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_fname", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_fname = fn;
  LLVMValueRef empty = refl_empty_string(g);

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef load = LLVMAppendBasicBlockInContext(g->ctx, fn, "load");
  LLVMBasicBlockRef bad = LLVMAppendBasicBlockInContext(g->ctx, fn, "bad");

  LLVMValueRef ti = LLVMGetParam(fn, 0);
  LLVMValueRef idx = LLVMGetParam(fn, 1);
  LLVMValueRef which = LLVMGetParam(fn, 2);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef count = refl_hdr_i64(g, ti, ZAN_REFL_COUNT_OFF, "refl.count");
  LLVMValueRef ok = LLVMBuildAnd(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0),
                    "refl.ge"),
      LLVMBuildICmp(g->builder, LLVMIntSLT, idx, count, "refl.lt"), "refl.ok");
  LLVMBuildCondBr(g->builder, ok, load, bad);

  LLVMPositionBuilderAtEnd(g->builder, load);
  LLVMValueRef fields = refl_hdr_ptr(g, ti, ZAN_REFL_FIELDS_OFF, "refl.fields");
  LLVMValueRef row = refl_member_row(g, fields, idx, false);
  LLVMValueRef sel = LLVMBuildSelect(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                    LLVMConstInt(i64, ZAN_REFL_WHICH_NAME, 0), "refl.isname"),
      LLVMConstInt(i64, 0, 0),
      LLVMBuildSelect(
          g->builder,
          LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                        LLVMConstInt(i64, ZAN_REFL_WHICH_TYPE, 0),
                        "refl.istype"),
          LLVMConstInt(i64, 1, 0),
          LLVMBuildSelect(
              g->builder,
              LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                            LLVMConstInt(i64, ZAN_REFL_WHICH_DECLATTRS, 0),
                            "refl.isdeclattrs"),
              LLVMConstInt(i64, 2, 0), LLVMConstInt(i64, 5, 0), "refl.sel3"),
          "refl.sel2"),
      "refl.sel");
  LLVMValueRef sv = refl_member_ptr(g, fields, row, sel);
  LLVMBuildRet(g->builder,
               LLVMBuildSelect(g->builder,
                               LLVMBuildIsNull(g->builder, sv, "refl.fnull"),
                               empty, sv, "refl.name"));

  LLVMPositionBuilderAtEnd(g->builder, bad);
  LLVMBuildRet(g->builder, empty);

  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}
LLVMValueRef refl_obj_type_fn(zan_irgen_t *g) {
  if (g->fn_refl_obj_type)
    return g->fn_refl_obj_type;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_obj_type", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  g->fn_refl_obj_type = fn;

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef tbl = LLVMAppendBasicBlockInContext(g->ctx, fn, "tbl");
  LLVMBasicBlockRef hit = LLVMAppendBasicBlockInContext(g->ctx, fn, "hit");
  LLVMBasicBlockRef fb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fallback");

  LLVMValueRef obj = LLVMGetParam(fn, 0);
  LLVMValueRef fallback = LLVMGetParam(fn, 1);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMBuildCondBr(g->builder, LLVMBuildIsNull(g->builder, obj, "refl.objnull"),
                  fb, tbl);

  LLVMPositionBuilderAtEnd(g->builder, tbl);
  LLVMValueRef raw_site =
      refl_hdr_i64(g, obj, ZAN_OBJ_SITE_OFF, "refl.site.raw");
  LLVMValueRef site =
      LLVMBuildAnd(g->builder, raw_site,
                   LLVMConstInt(i64, ~ZAN_OBJ_STACK_FLAG, 0), "refl.site");
  LLVMValueRef in_range;
  if (!g->desc_hdr) {
    LLVMValueRef bound =
        LLVMBuildLoad2(g->builder, i64, g->g_site_count, "refl.bound");
    LLVMValueRef is_arr = LLVMBuildICmp(
        g->builder, LLVMIntEQ,
        refl_hdr_i64(g, obj, ZAN_ARR_RC_MAGIC_OFF, "refl.rcm"),
        LLVMConstInt(i64, ZAN_ARRAY_RC_MAGIC, 0), "refl.is_arrmagic");
    LLVMValueRef is_not_obj =
        zan_or(g->builder, zan_hdr_is_string(g, site, "refl.isstr"), is_arr,
               "refl.not_obj");
    in_range = LLVMBuildAnd(
        g->builder,
        LLVMBuildAnd(
            g->builder,
            LLVMBuildICmp(g->builder, LLVMIntUGT, site, LLVMConstInt(i64, 0, 0),
                          "refl.s0"),
            LLVMBuildICmp(g->builder, LLVMIntULT, site, bound, "refl.sn"),
            "refl.sok"),
        LLVMBuildNot(g->builder, is_not_obj, "refl.not_tag"), "refl.in_range");
  } else {
    LLVMValueRef is_tag =
        zan_or(g->builder,
               zan_or(g->builder, zan_hdr_is_string(g, site, "refl.isstr"),
                      LLVMBuildICmp(g->builder, LLVMIntEQ, site,
                                    LLVMConstInt(i64, ZAN_ARRAY_MAGIC, 0),
                                    "refl.isarr"),
                      "refl.istag"),
               LLVMBuildICmp(g->builder, LLVMIntULT, site,
                             LLVMConstInt(i64, 4096, 0), "refl.istiny"),
               "refl.notdsc");
    LLVMBasicBlockRef dsc = LLVMAppendBasicBlockInContext(g->ctx, fn, "dsc");
    LLVMBuildCondBr(g->builder, is_tag, fb, dsc);
    LLVMPositionBuilderAtEnd(g->builder, dsc);
    LLVMValueRef dp = LLVMBuildIntToPtr(g->builder, site, i8ptr, "refl.dsc");
    LLVMValueRef m_off = LLVMConstInt(i64, 16, 0);
    LLVMValueRef m_p = LLVMBuildGEP2(g->builder, i8, dp, &m_off, 1, "refl.mp");
    LLVMValueRef m = LLVMBuildLoad2(g->builder, i8ptr, m_p, "refl.m");
    LLVMBuildCondBr(g->builder, LLVMBuildIsNull(g->builder, m, "refl.mnull"),
                    fb, hit);
    LLVMPositionBuilderAtEnd(g->builder, hit);
    LLVMBuildRet(g->builder, m);
    LLVMPositionBuilderAtEnd(g->builder, fb);
    LLVMBuildRet(g->builder, fallback);
    if (save)
      LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
  }
  LLVMBasicBlockRef ld = LLVMAppendBasicBlockInContext(g->ctx, fn, "ld");
  LLVMBuildCondBr(g->builder, in_range, ld, fb);

  LLVMPositionBuilderAtEnd(g->builder, ld);
  LLVMValueRef mtbl = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0),
                                     g->g_site_meta, "refl.mtbl");
  LLVMValueRef slot =
      LLVMBuildGEP2(g->builder, i8ptr, mtbl, &site, 1, "refl.mslot");
  LLVMValueRef m = LLVMBuildLoad2(g->builder, i8ptr, slot, "refl.m");
  LLVMBuildCondBr(g->builder, LLVMBuildIsNull(g->builder, m, "refl.mnull"), fb,
                  hit);

  LLVMPositionBuilderAtEnd(g->builder, hit);
  LLVMBuildRet(g->builder, m);
  LLVMPositionBuilderAtEnd(g->builder, fb);
  LLVMBuildRet(g->builder, fallback);

  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}

static void refl_table_of(zan_irgen_t *g, LLVMValueRef ti, LLVMValueRef tbl,
                          LLVMValueRef *out_arr, LLVMValueRef *out_count) {
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMValueRef is_m =
      LLVMBuildICmp(g->builder, LLVMIntEQ, tbl,
                    LLVMConstInt(i64, ZAN_REFL_TBL_METHOD, 0), "refl.ism");
  *out_arr = LLVMBuildSelect(
      g->builder, is_m, refl_hdr_ptr(g, ti, ZAN_REFL_METHODS_OFF, "refl.mt"),
      refl_hdr_ptr(g, ti, ZAN_REFL_CTORS_OFF, "refl.ct"), "refl.tbl");
  *out_count = LLVMBuildSelect(
      g->builder, is_m, refl_hdr_i64(g, ti, ZAN_REFL_MCOUNT_OFF, "refl.mc"),
      refl_hdr_i64(g, ti, ZAN_REFL_CCOUNT_OFF, "refl.cc"), "refl.tc");
}
