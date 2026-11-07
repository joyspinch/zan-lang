static LLVMValueRef refl_mfind_fn(zan_irgen_t *g) {
    if (g->fn_refl_mfind) return g->fn_refl_mfind;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_mfind", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_mfind = fn;

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef head  = LLVMAppendBasicBlockInContext(g->ctx, fn, "head");
    LLVMBasicBlockRef body  = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef cmp   = LLVMAppendBasicBlockInContext(g->ctx, fn, "cmp");
    LLVMBasicBlockRef next  = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
    LLVMBasicBlockRef found = LLVMAppendBasicBlockInContext(g->ctx, fn, "found");
    LLVMBasicBlockRef miss  = LLVMAppendBasicBlockInContext(g->ctx, fn, "miss");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef want = LLVMGetParam(fn, 1);
    LLVMValueRef flags = LLVMGetParam(fn, 2);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef methods = refl_hdr_ptr(g, ti, ZAN_REFL_METHODS_OFF, "refl.mt");
    LLVMValueRef count = refl_hdr_i64(g, ti, ZAN_REFL_MCOUNT_OFF, "refl.mc");
    LLVMBuildCondBr(g->builder,
        LLVMBuildIsNull(g->builder, methods, "refl.nomt"), miss, head);

    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef i = LLVMBuildPhi(g->builder, i64, "refl.mi");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSLT, i, count, "refl.more"), body, miss);

    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef off = LLVMBuildMul(g->builder, i,
        LLVMConstInt(i64, ZAN_REFL_METHOD_SIZE, 0), "refl.mo");
    LLVMValueRef rec = LLVMBuildGEP2(g->builder, i8, methods, &off, 1, "refl.mr");
    LLVMValueRef mname = refl_hdr_ptr(g, rec, ZAN_REFL_MO_NAME, "refl.mn");
    LLVMValueRef mflags = refl_hdr_i64(g, rec, ZAN_REFL_MO_FLAGS, "refl.mf");
    LLVMValueRef acc = LLVMBuildICmp(g->builder, LLVMIntNE, flags,
                                     LLVMConstInt(i64, 0, 0), "refl.isacc");
    LLVMValueRef four = LLVMBuildSelect(g->builder, acc,
        LLVMConstInt(i64, 4, 0), LLVMConstInt(i64, 0, 0), "refl.skip");
    LLVMValueRef cand = LLVMBuildGEP2(g->builder, i8, mname, &four, 1, "refl.mn2");
    LLVMValueRef flag_ok = LLVMBuildOr(g->builder,
        LLVMBuildNot(g->builder, acc, "refl.noacc"),
        LLVMBuildICmp(g->builder, LLVMIntNE,
            LLVMBuildAnd(g->builder, mflags, flags, "refl.fand"),
            LLVMConstInt(i64, 0, 0), "refl.fhit"), "refl.fok");
    LLVMBuildCondBr(g->builder, flag_ok, cmp, next);

    LLVMPositionBuilderAtEnd(g->builder, cmp);
    LLVMValueRef c = zan_call2(g->builder,
        LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
        g->fn_strcmp, (LLVMValueRef[]){ cand, want }, 2, "refl.mcmp");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, c, LLVMConstInt(i32, 0, 0),
                      "refl.mhit"), found, next);

    LLVMPositionBuilderAtEnd(g->builder, next);
    LLVMValueRef inext = LLVMBuildAdd(g->builder, i, LLVMConstInt(i64, 1, 0),
                                      "refl.mi.n");
    LLVMBuildBr(g->builder, head);
    LLVMAddIncoming(i, (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), inext },
                    (LLVMBasicBlockRef[]){ entry, next }, 2);

    LLVMPositionBuilderAtEnd(g->builder, found);
    LLVMBuildRet(g->builder, i);
    LLVMPositionBuilderAtEnd(g->builder, miss);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, (unsigned long long)-1LL, 1));

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

static LLVMValueRef refl_mstr_fn(zan_irgen_t *g) {
    if (g->fn_refl_mstr) return g->fn_refl_mstr;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i8ptr,
        (LLVMTypeRef[]){ i8ptr, i64, i64, i64, i64 }, 5, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_mstr", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_mstr = fn;
    LLVMValueRef empty = refl_empty_string(g);

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef have  = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
    LLVMBasicBlockRef par   = LLVMAppendBasicBlockInContext(g->ctx, fn, "param");
    LLVMBasicBlockRef own   = LLVMAppendBasicBlockInContext(g->ctx, fn, "own");
    LLVMBasicBlockRef bad   = LLVMAppendBasicBlockInContext(g->ctx, fn, "bad");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef tbl = LLVMGetParam(fn, 1);
    LLVMValueRef idx = LLVMGetParam(fn, 2);
    LLVMValueRef which = LLVMGetParam(fn, 3);
    LLVMValueRef k = LLVMGetParam(fn, 4);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef arr = NULL, count = NULL;
    refl_table_of(g, ti, tbl, &arr, &count);
    LLVMValueRef ok = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0), "refl.ge"),
        LLVMBuildICmp(g->builder, LLVMIntSLT, idx, count, "refl.lt"), "refl.ok");
    ok = LLVMBuildAnd(g->builder, ok,
        LLVMBuildIsNotNull(g->builder, arr, "refl.hastbl"), "refl.ok2");
    LLVMBuildCondBr(g->builder, ok, have, bad);

    LLVMPositionBuilderAtEnd(g->builder, have);
    LLVMValueRef off = LLVMBuildMul(g->builder, idx,
        LLVMConstInt(i64, ZAN_REFL_METHOD_SIZE, 0), "refl.mo");
    LLVMValueRef rec = LLVMBuildGEP2(g->builder, i8, arr, &off, 1, "refl.mr");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                      LLVMConstInt(i64, ZAN_REFL_MW_PARAM, 0), "refl.isp"),
        par, own);

    LLVMPositionBuilderAtEnd(g->builder, own);
    LLVMValueRef sel = LLVMBuildSelect(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                      LLVMConstInt(i64, ZAN_REFL_MW_NAME, 0), "refl.isn"),
        LLVMConstInt(i64, ZAN_REFL_MO_NAME, 0),
        LLVMBuildSelect(g->builder,
            LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                          LLVMConstInt(i64, ZAN_REFL_MW_RET, 0), "refl.isret"),
            LLVMConstInt(i64, ZAN_REFL_MO_RET, 0),
            LLVMConstInt(i64, ZAN_REFL_MO_ATTRS, 0), "refl.sel2"),
        "refl.sel");
    LLVMValueRef sp = LLVMBuildGEP2(g->builder, i8, rec, &sel, 1, "refl.sp");
    LLVMValueRef sv = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildBitCast(g->builder, sp, LLVMPointerType(i8ptr, 0), "refl.spc"),
        "refl.sv");
    LLVMBuildRet(g->builder, LLVMBuildSelect(g->builder,
        LLVMBuildIsNull(g->builder, sv, "refl.snull"), empty, sv, "refl.s"));

    LLVMPositionBuilderAtEnd(g->builder, par);
    LLVMValueRef pc = refl_hdr_i64(g, rec, ZAN_REFL_MO_PCOUNT, "refl.pc");
    LLVMValueRef ptypes = refl_hdr_ptr(g, rec, ZAN_REFL_MO_PTYPES, "refl.pt");
    LLVMValueRef pok = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSGE, k, LLVMConstInt(i64, 0, 0), "refl.kge"),
        LLVMBuildICmp(g->builder, LLVMIntSLT, k, pc, "refl.klt"), "refl.pok");
    pok = LLVMBuildAnd(g->builder, pok,
        LLVMBuildIsNotNull(g->builder, ptypes, "refl.haspt"), "refl.pok2");
    LLVMBasicBlockRef pld = LLVMAppendBasicBlockInContext(g->ctx, fn, "pload");
    LLVMBuildCondBr(g->builder, pok, pld, bad);

    LLVMPositionBuilderAtEnd(g->builder, pld);
    LLVMValueRef pp = LLVMBuildGEP2(g->builder, i8ptr,
        LLVMBuildBitCast(g->builder, ptypes, LLVMPointerType(i8ptr, 0), "refl.ptc"),
        &k, 1, "refl.pe");
    LLVMValueRef pv = LLVMBuildLoad2(g->builder, i8ptr, pp, "refl.pv");
    LLVMBuildRet(g->builder, LLVMBuildSelect(g->builder,
        LLVMBuildIsNull(g->builder, pv, "refl.pnull"), empty, pv, "refl.p"));

    LLVMPositionBuilderAtEnd(g->builder, bad);
    LLVMBuildRet(g->builder, empty);

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

static LLVMValueRef refl_mi64_fn(zan_irgen_t *g) {
    if (g->fn_refl_mi64) return g->fn_refl_mi64;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ i8ptr, i64, i64, i64 }, 4, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_mi64", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_mi64 = fn;

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef cnt   = LLVMAppendBasicBlockInContext(g->ctx, fn, "count");
    LLVMBasicBlockRef one   = LLVMAppendBasicBlockInContext(g->ctx, fn, "one");
    LLVMBasicBlockRef zero  = LLVMAppendBasicBlockInContext(g->ctx, fn, "zero");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef tbl = LLVMGetParam(fn, 1);
    LLVMValueRef idx = LLVMGetParam(fn, 2);
    LLVMValueRef which = LLVMGetParam(fn, 3);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef arr = NULL, count = NULL;
    refl_table_of(g, ti, tbl, &arr, &count);
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                      LLVMConstInt(i64, ZAN_REFL_MI_COUNT, 0), "refl.iscnt"),
        cnt, one);

    LLVMPositionBuilderAtEnd(g->builder, cnt);
    LLVMBuildRet(g->builder, count);

    LLVMPositionBuilderAtEnd(g->builder, one);
    LLVMValueRef ok = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0), "refl.ge"),
        LLVMBuildICmp(g->builder, LLVMIntSLT, idx, count, "refl.lt"), "refl.ok");
    ok = LLVMBuildAnd(g->builder, ok,
        LLVMBuildIsNotNull(g->builder, arr, "refl.hastbl"), "refl.ok2");
    LLVMBasicBlockRef ld = LLVMAppendBasicBlockInContext(g->ctx, fn, "load");
    LLVMBuildCondBr(g->builder, ok, ld, zero);

    LLVMPositionBuilderAtEnd(g->builder, ld);
    LLVMValueRef off = LLVMBuildMul(g->builder, idx,
        LLVMConstInt(i64, ZAN_REFL_METHOD_SIZE, 0), "refl.mo");
    LLVMValueRef rec = LLVMBuildGEP2(g->builder, i8, arr, &off, 1, "refl.mr");
    LLVMValueRef pc = refl_hdr_i64(g, rec, ZAN_REFL_MO_PCOUNT, "refl.pc");
    LLVMValueRef fl = refl_hdr_i64(g, rec, ZAN_REFL_MO_FLAGS, "refl.fl");
    LLVMValueRef rk = refl_hdr_i64(g, rec, ZAN_REFL_MO_RETKIND, "refl.rk");
    LLVMValueRef v = LLVMBuildSelect(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                      LLVMConstInt(i64, ZAN_REFL_MI_PCOUNT, 0), "refl.ispc"),
        pc, LLVMBuildSelect(g->builder,
            LLVMBuildICmp(g->builder, LLVMIntEQ, which,
                          LLVMConstInt(i64, ZAN_REFL_MI_FLAGS, 0), "refl.isfl"),
            fl, rk, "refl.v1"), "refl.v");
    LLVMBuildRet(g->builder, v);

    LLVMPositionBuilderAtEnd(g->builder, zero);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

static LLVMValueRef refl_invoke_fn(zan_irgen_t *g) {
    if (g->fn_refl_invoke) return g->fn_refl_invoke;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef vd = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr, i64ptr, i64ptr }, 6, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_invoke", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_invoke = fn;

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef have  = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
    LLVMBasicBlockRef call  = LLVMAppendBasicBlockInContext(g->ctx, fn, "call");
    LLVMBasicBlockRef miss  = LLVMAppendBasicBlockInContext(g->ctx, fn, "miss");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef tbl = LLVMGetParam(fn, 1);
    LLVMValueRef idx = LLVMGetParam(fn, 2);
    LLVMValueRef obj = LLVMGetParam(fn, 3);
    LLVMValueRef args = LLVMGetParam(fn, 4);
    LLVMValueRef kindout = LLVMGetParam(fn, 5);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef arr = NULL, count = NULL;
    refl_table_of(g, ti, tbl, &arr, &count);
    LLVMValueRef ok = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0), "refl.ge"),
        LLVMBuildICmp(g->builder, LLVMIntSLT, idx, count, "refl.lt"), "refl.ok");
    ok = LLVMBuildAnd(g->builder, ok,
        LLVMBuildIsNotNull(g->builder, arr, "refl.hastbl"), "refl.ok2");
    LLVMBuildCondBr(g->builder, ok, have, miss);

    LLVMPositionBuilderAtEnd(g->builder, have);
    LLVMValueRef off = LLVMBuildMul(g->builder, idx,
        LLVMConstInt(i64, ZAN_REFL_METHOD_SIZE, 0), "refl.mo");
    LLVMValueRef rec = LLVMBuildGEP2(g->builder, i8, arr, &off, 1, "refl.mr");
    LLVMValueRef thunk = refl_hdr_ptr(g, rec, ZAN_REFL_MO_THUNK, "refl.th");
    LLVMValueRef rk = refl_hdr_i64(g, rec, ZAN_REFL_MO_RETKIND, "refl.rk");
    LLVMBuildCondBr(g->builder,
        LLVMBuildIsNull(g->builder, thunk, "refl.nothunk"), miss, call);

    LLVMPositionBuilderAtEnd(g->builder, call);
    LLVMValueRef ret = LLVMBuildAlloca(g->builder, i64, "refl.ret");
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), ret);
    LLVMTypeRef th_ty = LLVMFunctionType(vd,
        (LLVMTypeRef[]){ i8ptr, i64ptr, i64ptr }, 3, 0);
    zan_call2(g->builder, th_ty,
        LLVMBuildBitCast(g->builder, thunk, LLVMPointerType(th_ty, 0), "refl.thf"),
        (LLVMValueRef[]){ obj, args, ret }, 3, "");
    LLVMBuildStore(g->builder,
        LLVMBuildAdd(g->builder, rk, LLVMConstInt(i64, 1, 0), "refl.k1"), kindout);
    LLVMBuildRet(g->builder, LLVMBuildLoad2(g->builder, i64, ret, "refl.rv"));

    LLVMPositionBuilderAtEnd(g->builder, miss);
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), kindout);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

static LLVMValueRef refl_pget_fn(zan_irgen_t *g) {
    if (g->fn_refl_pget) return g->fn_refl_pget;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr, i64ptr }, 4, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_pget", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_pget = fn;
    LLVMValueRef mfind = refl_mfind_fn(g);
    LLVMValueRef invoke = refl_invoke_fn(g);

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef call  = LLVMAppendBasicBlockInContext(g->ctx, fn, "call");
    LLVMBasicBlockRef miss  = LLVMAppendBasicBlockInContext(g->ctx, fn, "miss");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef obj = LLVMGetParam(fn, 1);
    LLVMValueRef name = LLVMGetParam(fn, 2);
    LLVMValueRef kindout = LLVMGetParam(fn, 3);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef idx = zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0),
        mfind, (LLVMValueRef[]){ ti, name,
            LLVMConstInt(i64, ZAN_REFL_MF_PROPGET, 0) }, 3, "refl.gi");
    LLVMValueRef ok = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0), "refl.gok"),
        LLVMBuildIsNotNull(g->builder, obj, "refl.gobj"), "refl.gok2");
    LLVMBuildCondBr(g->builder, ok, call, miss);

    LLVMPositionBuilderAtEnd(g->builder, call);
    LLVMValueRef slots = LLVMBuildArrayAlloca(g->builder, i64,
        LLVMConstInt(i64, ZAN_REFL_ARG_SLOTS, 0), "refl.gargs");
    LLVMValueRef kout = LLVMBuildAlloca(g->builder, i64, "refl.gk");
    LLVMValueRef v = zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr,
                                               i64ptr, i64ptr }, 6, 0),
        invoke, (LLVMValueRef[]){ ti,
            LLVMConstInt(i64, ZAN_REFL_TBL_METHOD, 0), idx, obj, slots, kout },
        6, "refl.gv");
    LLVMValueRef k = LLVMBuildLoad2(g->builder, i64, kout, "refl.gkv");
    LLVMBuildStore(g->builder,
        LLVMBuildSelect(g->builder,
            LLVMBuildICmp(g->builder, LLVMIntEQ, k, LLVMConstInt(i64, 0, 0),
                          "refl.gnk"),
            LLVMConstInt(i64, 0, 0),
            LLVMBuildSub(g->builder, k, LLVMConstInt(i64, 1, 0), "refl.gk1"),
            "refl.gks"), kindout);
    LLVMBuildRet(g->builder, v);

    LLVMPositionBuilderAtEnd(g->builder, miss);
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), kindout);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

static LLVMValueRef refl_set_fn(zan_irgen_t *g) {
    if (g->fn_refl_set) return g->fn_refl_set;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i16 = LLVMInt16TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef flt = LLVMFloatTypeInContext(g->ctx);
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr, i64, i64 }, 5, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_set", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_set = fn;
    LLVMValueRef mfind = refl_mfind_fn(g);
    LLVMValueRef invoke = refl_invoke_fn(g);
    LLVMValueRef find = refl_find_fn(g);

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMValueRef save_fn = g->current_fn;
    g->current_fn = fn;
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef setr  = LLVMAppendBasicBlockInContext(g->ctx, fn, "setter");
    LLVMBasicBlockRef fld   = LLVMAppendBasicBlockInContext(g->ctx, fn, "field");
    LLVMBasicBlockRef slot  = LLVMAppendBasicBlockInContext(g->ctx, fn, "slot");
    LLVMBasicBlockRef no    = LLVMAppendBasicBlockInContext(g->ctx, fn, "no");
    LLVMBasicBlockRef yes   = LLVMAppendBasicBlockInContext(g->ctx, fn, "yes");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef obj = LLVMGetParam(fn, 1);
    LLVMValueRef name = LLVMGetParam(fn, 2);
    LLVMValueRef val = LLVMGetParam(fn, 3);
    LLVMValueRef vkind = LLVMGetParam(fn, 4);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef sidx = zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0),
        mfind, (LLVMValueRef[]){ ti, name,
            LLVMConstInt(i64, ZAN_REFL_MF_PROPSET, 0) }, 3, "refl.si");
    LLVMValueRef has_setter = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSGE, sidx, LLVMConstInt(i64, 0, 0), "refl.sge"),
        LLVMBuildIsNotNull(g->builder, obj, "refl.sobj"), "refl.hasset");
    LLVMBuildCondBr(g->builder, has_setter, setr, fld);

    LLVMPositionBuilderAtEnd(g->builder, setr);
    LLVMValueRef slots = LLVMBuildArrayAlloca(g->builder, i64,
        LLVMConstInt(i64, ZAN_REFL_ARG_SLOTS, 0), "refl.sargs");
    LLVMBuildStore(g->builder, val, slots);
    LLVMValueRef kout = LLVMBuildAlloca(g->builder, i64, "refl.sk");
    zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr,
                                               i64ptr, i64ptr }, 6, 0),
        invoke, (LLVMValueRef[]){ ti,
            LLVMConstInt(i64, ZAN_REFL_TBL_METHOD, 0), sidx, obj, slots, kout },
        6, "refl.sv");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntNE,
            LLVMBuildLoad2(g->builder, i64, kout, "refl.skv"),
            LLVMConstInt(i64, 0, 0), "refl.sdone"), yes, no);

    LLVMPositionBuilderAtEnd(g->builder, fld);
    LLVMValueRef idx = zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
        find, (LLVMValueRef[]){ ti, name }, 2, "refl.fi");
    LLVMValueRef fok = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0), "refl.fge"),
        LLVMBuildIsNotNull(g->builder, obj, "refl.fobj"), "refl.fok");
    LLVMBuildCondBr(g->builder, fok, slot, no);

    LLVMPositionBuilderAtEnd(g->builder, slot);
    LLVMValueRef fields = refl_hdr_ptr(g, ti, ZAN_REFL_FIELDS_OFF, "refl.fields");
    LLVMValueRef foffr = LLVMBuildMul(g->builder, idx,
        LLVMConstInt(i64, ZAN_REFL_FIELD_SIZE, 0), "refl.fo");
    LLVMValueRef fe = LLVMBuildGEP2(g->builder, i8, fields, &foffr, 1, "refl.fe");
    LLVMValueRef fkind = refl_hdr_i64(g, fe, 16, "refl.fkind");
    LLVMValueRef foff = refl_hdr_i64(g, fe, 24, "refl.foff");
    LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, obj, &foff, 1, "refl.fslot");
    LLVMValueRef is_fp = LLVMBuildICmp(g->builder, LLVMIntEQ, vkind,
        LLVMConstInt(i64, ZAN_REFL_VK_DOUBLE, 0), "refl.visfp");
    LLVMValueRef as_d = LLVMBuildSelect(g->builder, is_fp,
        LLVMBuildBitCast(g->builder, val, dbl, "refl.vbc"),
        LLVMBuildSIToFP(g->builder, val, dbl, "refl.vi2f"), "refl.vd");
    LLVMValueRef as_i = LLVMBuildSelect(g->builder, is_fp,
        emit_fptosi_guarded(g,
            LLVMBuildBitCast(g->builder, val, dbl, "refl.vbc2"), i64,
            (zan_loc_t){0}, is_fp),
        val, "refl.vi");

    struct { int kind; LLVMTypeRef ty; int fp; const char *nm; } cases[] = {
        { ZAN_REFL_FK_BOOL,   i8,  0, "b" },
        { ZAN_REFL_FK_BYTE,   i8,  0, "u8" },
        { ZAN_REFL_FK_SBYTE,  i8,  0, "i8" },
        { ZAN_REFL_FK_SHORT,  i16, 0, "i16" },
        { ZAN_REFL_FK_USHORT, i16, 0, "u16" },
        { ZAN_REFL_FK_INT,    i32, 0, "i32" },
        { ZAN_REFL_FK_UINT,   i32, 0, "u32" },
        { ZAN_REFL_FK_LONG,   i64, 0, "i64" },
        { ZAN_REFL_FK_ULONG,  i64, 0, "u64" },
        { ZAN_REFL_FK_CHAR,   i64, 0, "ch" },
        { ZAN_REFL_FK_ENUM,   i64, 0, "en" },
        { ZAN_REFL_FK_FLOAT,  flt, 1, "f32" },
        { ZAN_REFL_FK_DOUBLE, dbl, 1, "f64" },
    };
    int ncases = (int)(sizeof(cases) / sizeof(cases[0]));
    LLVMValueRef sw = LLVMBuildSwitch(g->builder, fkind, no,
                                      (unsigned)ncases + 1);
    for (int ci = 0; ci < ncases; ci++) {
        char bn[24];
        snprintf(bn, sizeof(bn), "st.%s", cases[ci].nm);
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, bn);
        LLVMAddCase(sw, LLVMConstInt(i64, (unsigned long long)cases[ci].kind, 0), bb);
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef sv;
        if (cases[ci].fp)
            sv = cases[ci].ty == flt
               ? LLVMBuildFPTrunc(g->builder, as_d, flt, "refl.stf")
               : as_d;
        else if (cases[ci].kind == ZAN_REFL_FK_BOOL)
            sv = LLVMBuildZExt(g->builder,
                    LLVMBuildICmp(g->builder, LLVMIntNE, as_i,
                                  LLVMConstInt(i64, 0, 0), "refl.stb"), i8,
                    "refl.stbz");
        else
            sv = cases[ci].ty == i64 ? as_i
               : LLVMBuildTrunc(g->builder, as_i, cases[ci].ty, "refl.sttr");
        LLVMBuildStore(g->builder, sv,
            LLVMBuildBitCast(g->builder, p,
                LLVMPointerType(cases[ci].ty, 0), "refl.stp"));
        LLVMBuildBr(g->builder, yes);
    }
    LLVMBasicBlockRef sstr = LLVMAppendBasicBlockInContext(g->ctx, fn, "st.str");
    LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0), sstr);
    LLVMPositionBuilderAtEnd(g->builder, sstr);
    LLVMBasicBlockRef sstr2 = LLVMAppendBasicBlockInContext(g->ctx, fn, "st.str2");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, vkind,
                      LLVMConstInt(i64, ZAN_REFL_VK_PTR, 0), "refl.visptr"),
        sstr2, no);
    LLVMPositionBuilderAtEnd(g->builder, sstr2);
    LLVMValueRef sp = LLVMBuildBitCast(g->builder, p,
                                       LLVMPointerType(i8ptr, 0), "refl.stsp");
    LLVMValueRef nv = LLVMBuildIntToPtr(g->builder, val, i8ptr, "refl.stsv");
    LLVMValueRef ov = LLVMBuildLoad2(g->builder, i8ptr, sp, "refl.stold");
    LLVMTypeRef arc_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                          (LLVMTypeRef[]){ i8ptr }, 1, 0);
    zan_call2(g->builder, arc_ty, g->rt_str_retain, &nv, 1, "");
    LLVMBuildStore(g->builder, nv, sp);
    zan_call2(g->builder, arc_ty, g->rt_str_release, &ov, 1, "");
    LLVMBuildBr(g->builder, yes);

    LLVMBasicBlockRef sref = LLVMAppendBasicBlockInContext(g->ctx, fn, "st.ref");
    LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_FK_REF, 0), sref);
    LLVMPositionBuilderAtEnd(g->builder, sref);
    LLVMBasicBlockRef sref2 = LLVMAppendBasicBlockInContext(g->ctx, fn, "st.ref2");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, vkind,
                      LLVMConstInt(i64, ZAN_REFL_VK_PTR, 0), "refl.visptr_ref"),
        sref2, no);
    LLVMPositionBuilderAtEnd(g->builder, sref2);
    LLVMValueRef ref_sp = LLVMBuildBitCast(g->builder, p,
                                           LLVMPointerType(i8ptr, 0), "refl.stref_p");
    LLVMValueRef ref_nv = LLVMBuildIntToPtr(g->builder, val, i8ptr, "refl.stref_nv");
    LLVMValueRef ref_ov = LLVMBuildLoad2(g->builder, i8ptr, ref_sp, "refl.stref_ov");
    zan_call2(g->builder, arc_ty, g->rt_retain, &ref_nv, 1, "");
    LLVMBuildStore(g->builder, ref_nv, ref_sp);
    /* 覆写丢弃的旧值是类实例：必须走 dyn 释放（按对象头里的 dtor
     * 分发到类型化释放体），否则它的字符串/引用字段不会被释放——
     * 浅释放只减计数不 walk 字段，旧值的子对象整串泄漏
     * （ORM Include 反复覆写导航字段时每轮漏一个，探针 pH5/pH7）。 */
    zan_call2(g->builder, arc_ty, g->rt_release_dyn, &ref_ov, 1, "");
    LLVMBuildBr(g->builder, yes);

    LLVMPositionBuilderAtEnd(g->builder, yes);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));
    LLVMPositionBuilderAtEnd(g->builder, no);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

    g->current_fn = save_fn;
    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

static LLVMValueRef refl_cfind_fn(zan_irgen_t *g) {
    if (g->fn_refl_cfind) return g->fn_refl_cfind;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_cfind", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_cfind = fn;

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef head  = LLVMAppendBasicBlockInContext(g->ctx, fn, "head");
    LLVMBasicBlockRef body  = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef next  = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
    LLVMBasicBlockRef found = LLVMAppendBasicBlockInContext(g->ctx, fn, "found");
    LLVMBasicBlockRef miss  = LLVMAppendBasicBlockInContext(g->ctx, fn, "miss");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef want = LLVMGetParam(fn, 1);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef arr = refl_hdr_ptr(g, ti, ZAN_REFL_CTORS_OFF, "refl.ct");
    LLVMValueRef count = refl_hdr_i64(g, ti, ZAN_REFL_CCOUNT_OFF, "refl.cc");
    LLVMBuildCondBr(g->builder,
        LLVMBuildIsNull(g->builder, arr, "refl.noct"), miss, head);

    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef i = LLVMBuildPhi(g->builder, i64, "refl.ci");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSLT, i, count, "refl.more"), body, miss);

    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef off = LLVMBuildMul(g->builder, i,
        LLVMConstInt(i64, ZAN_REFL_METHOD_SIZE, 0), "refl.co");
    LLVMValueRef rec = LLVMBuildGEP2(g->builder, i8, arr, &off, 1, "refl.cr");
    LLVMValueRef pc = refl_hdr_i64(g, rec, ZAN_REFL_MO_PCOUNT, "refl.cpc");
    LLVMValueRef th = refl_hdr_ptr(g, rec, ZAN_REFL_MO_THUNK, "refl.cth");
    LLVMBuildCondBr(g->builder, LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, pc, want, "refl.cpq"),
        LLVMBuildIsNotNull(g->builder, th, "refl.cthnn"), "refl.chit"),
        found, next);

    LLVMPositionBuilderAtEnd(g->builder, next);
    LLVMValueRef inext = LLVMBuildAdd(g->builder, i, LLVMConstInt(i64, 1, 0),
                                      "refl.ci.n");
    LLVMBuildBr(g->builder, head);
    LLVMAddIncoming(i, (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), inext },
                    (LLVMBasicBlockRef[]){ entry, next }, 2);

    LLVMPositionBuilderAtEnd(g->builder, found);
    LLVMBuildRet(g->builder, i);
    LLVMPositionBuilderAtEnd(g->builder, miss);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, (unsigned long long)-1LL, 1));

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

static LLVMValueRef refl_tainfo_fn(zan_irgen_t *g) {
    if (g->fn_refl_tainfo) return g->fn_refl_tainfo;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ i8ptr, i64, i64 }, 3, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_refl_tainfo", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_refl_tainfo = fn;

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef head  = LLVMAppendBasicBlockInContext(g->ctx, fn, "head");
    LLVMBasicBlockRef next  = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
    LLVMBasicBlockRef done  = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMBasicBlockRef cnt   = LLVMAppendBasicBlockInContext(g->ctx, fn, "count");
    LLVMBasicBlockRef ld    = LLVMAppendBasicBlockInContext(g->ctx, fn, "load");
    LLVMBasicBlockRef zero  = LLVMAppendBasicBlockInContext(g->ctx, fn, "zero");

    LLVMValueRef ti = LLVMGetParam(fn, 0);
    LLVMValueRef idx = LLVMGetParam(fn, 1);
    LLVMValueRef which = LLVMGetParam(fn, 2);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef arr = refl_hdr_ptr(g, ti, ZAN_REFL_TARGS_OFF, "refl.ta");
    LLVMValueRef arrp = LLVMBuildBitCast(g->builder, arr,
                                         LLVMPointerType(i8ptr, 0), "refl.tap");
    LLVMBuildCondBr(g->builder,
        LLVMBuildIsNull(g->builder, arr, "refl.nota"), zero, head);

    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef i = LLVMBuildPhi(g->builder, i64, "refl.ti");
    LLVMValueRef e = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildGEP2(g->builder, i8ptr, arrp, &i, 1, "refl.tae"), "refl.tav");
    LLVMBuildCondBr(g->builder,
        LLVMBuildIsNull(g->builder, e, "refl.taend"), done, next);

    LLVMPositionBuilderAtEnd(g->builder, next);
    LLVMValueRef inext = LLVMBuildAdd(g->builder, i, LLVMConstInt(i64, 1, 0),
                                      "refl.ti.n");
    LLVMBuildBr(g->builder, head);
    LLVMAddIncoming(i, (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), inext },
                    (LLVMBasicBlockRef[]){ entry, next }, 2);

    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, which, LLVMConstInt(i64, 0, 0),
                      "refl.iscnt"), cnt, ld);

    LLVMPositionBuilderAtEnd(g->builder, cnt);
    LLVMBuildRet(g->builder, i);

    LLVMPositionBuilderAtEnd(g->builder, ld);
    LLVMBasicBlockRef ld2 = LLVMAppendBasicBlockInContext(g->ctx, fn, "load2");
    LLVMBuildCondBr(g->builder, LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0), "refl.tge"),
        LLVMBuildICmp(g->builder, LLVMIntSLT, idx, i, "refl.tlt"), "refl.tok"),
        ld2, zero);

    LLVMPositionBuilderAtEnd(g->builder, ld2);
    LLVMValueRef v = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildGEP2(g->builder, i8ptr, arrp, &idx, 1, "refl.tap2"), "refl.tv");
    LLVMBuildRet(g->builder, LLVMBuildPtrToInt(g->builder, v, i64, "refl.tvi"));

    LLVMPositionBuilderAtEnd(g->builder, zero);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}

