static LLVMValueRef get_dict_key_eq_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_dict_key_eq");
    if (fn) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i1, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_dict_key_eq", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMValueRef ocmp = get_str_ordinal_cmp_fn(g, (zan_loc_t){0});
    LLVMTypeRef ocmp_ty = LLVMGlobalGetValueType(ocmp);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef str_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "str");
    LLVMBasicBlockRef raw_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "raw");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef a = LLVMGetParam(fn, 0);
    LLVMValueRef b2 = LLVMGetParam(fn, 1);
    LLVMValueRef is_str = LLVMGetParam(fn, 2);
    LLVMValueRef isv = zan_icmp(g->builder, LLVMIntNE, is_str,
        LLVMConstInt(i64, 0, 0), "isv");
    LLVMBuildCondBr(g->builder, isv, str_bb, raw_bb);
    LLVMPositionBuilderAtEnd(g->builder, str_bb);
    LLVMValueRef cmp = zan_call2(g->builder, ocmp_ty, ocmp,
        (LLVMValueRef[]){ a, b2 }, 2, "cmp");
    LLVMBuildRet(g->builder, zan_icmp(g->builder, LLVMIntEQ, cmp,
        LLVMConstInt(i32t, 0, 0), "eq"));
    LLVMPositionBuilderAtEnd(g->builder, raw_bb);
    LLVMBuildRet(g->builder, zan_icmp(g->builder, LLVMIntEQ, a, b2, "peq"));
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

static LLVMValueRef get_dict_hash_fn(zan_irgen_t *g) {
    /* B-ID80：哈希算法挪到运行时——SipHash-2-4 + 进程级随机盐
     * （hosted rt_mem.c 用 OS 熵盐；wasm/bare 确定性盐）。曾是 IR 内联的
     * 定种 FNV-1a，基值写死，攻击者可离线构造碰撞键族打满哈希桶。
     * IR 只留外部声明，链接由运行时对象供给。 */
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_dict_hash");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0);
    return LLVMAddFunction(g->mod, "__zan_dict_hash", fnty);
}

/* SwissTable Group matching: returns i32 mask where bit k is 1 if byte k == h2 */
static LLVMValueRef emit_swisstable_match_mask(zan_irgen_t *g, LLVMValueRef ctrl_vec, LLVMValueRef h2_byte) {
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef v16i8 = LLVMVectorType(i8, 16);

    LLVMValueRef ins = LLVMBuildInsertElement(b, LLVMGetUndef(v16i8), h2_byte, LLVMConstInt(i32, 0, 0), "h2.0");
    LLVMValueRef mask_elems[16];
    for (int i = 0; i < 16; i++) mask_elems[i] = LLVMConstInt(i32, 0, 0);
    LLVMValueRef mask = LLVMConstVector(mask_elems, 16);
    LLVMValueRef h2_vec = LLVMBuildShuffleVector(b, ins, LLVMGetUndef(v16i8), mask, "h2.vec");

    LLVMValueRef eq_vec = emit_byte_eq_mask_i64(g, ctrl_vec, h2_vec, 16);
    return emit_pmovmskb_portable(g, eq_vec, 16);
}

/* SwissTable empty mask: in SwissTable, EMPTY is 0x80 (bit 7 == 1), while occupied buckets
 * have h2 = hash & 0x7F (bit 7 == 0). So pmovmskb directly returns mask of empty slots! */
static LLVMValueRef emit_swisstable_empty_mask(zan_irgen_t *g, LLVMValueRef ctrl_vec) {
    return emit_pmovmskb_portable(g, ctrl_vec, 16);
}

static LLVMValueRef emit_cttz32(zan_irgen_t *g, LLVMValueRef val32) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
    LLVMTypeRef fty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i32, i1 }, 2, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.cttz.i32");
    if (!fn) fn = LLVMAddFunction(g->mod, "llvm.cttz.i32", fty);
    LLVMValueRef args[] = { val32, LLVMConstInt(i1, 1, 0) };
    return zan_call2(g->builder, fty, fn, args, 2, "cttz");
}

static LLVMValueRef get_dict_find_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_dict_find");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef v16i8 = LLVMVectorType(i8, 16);
    LLVMTypeRef dty = g->dict_struct_type;
    LLVMTypeRef fnty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_dict_find", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMValueRef hashf = get_dict_hash_fn(g);
    LLVMTypeRef hash_ty = LLVMGlobalGetValueType(hashf);
    LLVMValueRef keq = get_dict_key_eq_fn(g);
    LLVMTypeRef keq_ty = LLVMGlobalGetValueType(keq);
    LLVMTypeRef malloc_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0);
    LLVMValueRef malloc_fn = get_libc_fn(g, "malloc", malloc_ty);
    LLVMTypeRef memset_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32, i64 }, 3, 0);
    LLVMValueRef memset_fn = get_libc_fn(g, "memset", memset_ty);
    LLVMTypeRef ixfree_ty = LLVMFunctionType(LLVMVoidTypeInContext(c), (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMValueRef ixfree_fn = get_libc_fn(g, "free", ixfree_ty);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMBasicBlockRef entry  = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef miss   = LLVMAppendBasicBlockInContext(c, fn, "miss");
    LLVMBasicBlockRef chk    = LLVMAppendBasicBlockInContext(c, fn, "chk");
    LLVMBasicBlockRef live   = LLVMAppendBasicBlockInContext(c, fn, "live");
    LLVMBasicBlockRef needc  = LLVMAppendBasicBlockInContext(c, fn, "rb.need");
    LLVMBasicBlockRef capc   = LLVMAppendBasicBlockInContext(c, fn, "rb.capc");
    LLVMBasicBlockRef capb   = LLVMAppendBasicBlockInContext(c, fn, "rb.capb");
    LLVMBasicBlockRef alloc  = LLVMAppendBasicBlockInContext(c, fn, "rb.alloc");
    LLVMBasicBlockRef append_bb = LLVMAppendBasicBlockInContext(c, fn, "rb.append");
    LLVMBasicBlockRef fcond  = LLVMAppendBasicBlockInContext(c, fn, "rb.cond");
    LLVMBasicBlockRef fbody  = LLVMAppendBasicBlockInContext(c, fn, "rb.body");
    LLVMBasicBlockRef fp_loop = LLVMAppendBasicBlockInContext(c, fn, "rb.ploop");
    LLVMBasicBlockRef fp_dup  = LLVMAppendBasicBlockInContext(c, fn, "rb.pdup");
    LLVMBasicBlockRef fp_empty = LLVMAppendBasicBlockInContext(c, fn, "rb.pempty");
    LLVMBasicBlockRef fp_put  = LLVMAppendBasicBlockInContext(c, fn, "rb.put");
    LLVMBasicBlockRef fp_step = LLVMAppendBasicBlockInContext(c, fn, "rb.step");
    LLVMBasicBlockRef fnext  = LLVMAppendBasicBlockInContext(c, fn, "rb.next");

    LLVMBasicBlockRef probe  = LLVMAppendBasicBlockInContext(c, fn, "probe");
    LLVMBasicBlockRef p_loop = LLVMAppendBasicBlockInContext(c, fn, "p.loop");
    LLVMBasicBlockRef p_mchk = LLVMAppendBasicBlockInContext(c, fn, "p.mchk");
    LLVMBasicBlockRef p_hit  = LLVMAppendBasicBlockInContext(c, fn, "p.hit");
    LLVMBasicBlockRef p_echk = LLVMAppendBasicBlockInContext(c, fn, "p.echk");
    LLVMBasicBlockRef p_step = LLVMAppendBasicBlockInContext(c, fn, "p.step");

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef draw = LLVMGetParam(fn, 0);
    LLVMValueRef key = LLVMGetParam(fn, 1);
    LLVMValueRef is_str = LLVMGetParam(fn, 2);

    LLVMValueRef i_a = LLVMBuildAlloca(b, i64, "i");
    LLVMValueRef cap_a = LLVMBuildAlloca(b, i64, "ncap");
    LLVMValueRef gidx_a = LLVMBuildAlloca(b, i64, "gidx");
    LLVMValueRef step_a = LLVMBuildAlloca(b, i64, "step");
    LLVMValueRef mmask_a = LLVMBuildAlloca(b, i32, "mmask");
    LLVMValueRef fh2_a = LLVMBuildAlloca(b, i8, "fh2");

    LLVMValueRef dnull = zan_icmp(b, LLVMIntEQ, draw, LLVMConstNull(i8ptr), "dnull");
    LLVMBuildCondBr(b, dnull, miss, chk);
    LLVMPositionBuilderAtEnd(b, miss);
    LLVMBuildRet(b, LLVMConstInt(i64, (unsigned long long)-1, 1));

    LLVMPositionBuilderAtEnd(b, chk);
    LLVMValueRef dp = LLVMBuildBitCast(b, draw, LLVMPointerType(dty, 0), "dp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(b, dty, dp, 0, "cntp");
    LLVMValueRef cnt = LLVMBuildLoad2(b, i64, cntp, "cnt");
    LLVMValueRef kp = LLVMBuildStructGEP2(b, dty, dp, 2, "kp");
    LLVMValueRef ixp = LLVMBuildStructGEP2(b, dty, dp, 4, "ixp");
    LLVMValueRef icapp = LLVMBuildStructGEP2(b, dty, dp, 5, "icapp");
    LLVMValueRef icntp = LLVMBuildStructGEP2(b, dty, dp, 6, "icntp");
    LLVMValueRef empty = zan_icmp(b, LLVMIntSLE, cnt, LLVMConstInt(i64, 0, 0), "empty");
    LLVMValueRef ix = LLVMBuildLoad2(b, i8ptr, ixp, "ix");
    LLVMValueRef icap = LLVMBuildLoad2(b, i64, icapp, "icap");
    LLVMValueRef icnt = LLVMBuildLoad2(b, i64, icntp, "icnt");
    LLVMValueRef noidx = zan_icmp(b, LLVMIntEQ, ix, LLVMConstNull(i8ptr), "noidx");
    LLVMValueRef nocap = zan_icmp(b, LLVMIntSLE, icap, LLVMConstInt(i64, 0, 0), "nocap");
    LLVMValueRef stale = zan_icmp(b, LLVMIntNE, icnt, cnt, "stale");
    LLVMValueRef need = zan_or(b, zan_or(b, noidx, nocap, "o1"), stale, "need");
    LLVMBuildCondBr(b, empty, miss, live);

    LLVMPositionBuilderAtEnd(b, live);
    /* SwissTable 87.5% load factor check: cnt * 8 <= icap * 7 */
    LLVMValueRef cnt8 = zan_mul(b, cnt, LLVMConstInt(i64, 8, 0), "cnt8");
    LLVMValueRef cap7 = zan_mul(b, icap, LLVMConstInt(i64, 7, 0), "cap7");
    LLVMValueRef fits = zan_icmp(b, LLVMIntSLE, cnt8, cap7, "fits");
    LLVMValueRef grew = zan_icmp(b, LLVMIntSLT, icnt, cnt, "grew");
    LLVMValueRef kept = zan_icmp(b, LLVMIntSGT, icnt, LLVMConstInt(i64, 0, 0), "kept");
    LLVMValueRef append = zan_and(b, zan_and(b, zan_and(b,
        LLVMBuildNot(b, noidx, "haveidx"), fits, "a1"), grew, "a2"), kept, "append");
    LLVMBuildCondBr(b, need, needc, probe);

    LLVMPositionBuilderAtEnd(b, needc);
    LLVMBuildCondBr(b, append, append_bb, capc);

    LLVMPositionBuilderAtEnd(b, append_bb);
    LLVMBuildStore(b, cnt, icntp);
    LLVMBuildStore(b, icnt, i_a);
    LLVMBuildBr(b, fcond);

    LLVMPositionBuilderAtEnd(b, capc);
    LLVMBuildStore(b, LLVMConstInt(i64, 16, 0), cap_a);
    /* want = (cnt * 8 + 6) / 7 */
    LLVMValueRef want = zan_sdiv(b,
        zan_add(b, cnt8, LLVMConstInt(i64, 6, 0), "c8p6"),
        LLVMConstInt(i64, 7, 0), "want");
    LLVMBasicBlockRef caploop = LLVMAppendBasicBlockInContext(c, fn, "rb.caploop");
    LLVMBuildBr(b, caploop);
    LLVMPositionBuilderAtEnd(b, caploop);
    LLVMValueRef curcap = LLVMBuildLoad2(b, i64, cap_a, "curcap");
    LLVMValueRef small = zan_icmp(b, LLVMIntSLT, curcap, want, "small");
    LLVMBuildCondBr(b, small, capb, alloc);
    LLVMPositionBuilderAtEnd(b, capb);
    LLVMBuildStore(b, zan_shl(b, curcap, LLVMConstInt(i64, 1, 0), "cap2"), cap_a);
    LLVMBuildBr(b, caploop);

    LLVMPositionBuilderAtEnd(b, alloc);
    zan_call2(b, ixfree_ty, ixfree_fn, &ix, 1, "");
    LLVMValueRef ncap = LLVMBuildLoad2(b, i64, cap_a, "ncap.v");
    /* total_bytes = (ncap + 16) + (ncap * 8) = ncap * 9 + 16 */
    LLVMValueRef total_bytes = zan_add(b,
        zan_mul(b, ncap, LLVMConstInt(i64, 9, 0), "ncap9"),
        LLVMConstInt(i64, 16, 0), "tot_sz");
    LLVMValueRef nix = zan_call2(b, malloc_ty, malloc_fn, &total_bytes, 1, "nix");
    LLVMValueRef nix_null = zan_icmp(b, LLVMIntEQ, nix, LLVMConstNull(i8ptr), "nix_null");
    /* Initialize control bytes: memset(nix, 0x80, ncap + 16) */
    LLVMValueRef ctrl_sz = zan_add(b, ncap, LLVMConstInt(i64, 16, 0), "ctrl_sz");
    zan_call2(b, memset_ty, memset_fn,
        (LLVMValueRef[]){ nix, LLVMConstInt(i32, 0x80, 0), ctrl_sz }, 3, "");
    LLVMBuildStore(b, nix, ixp);
    LLVMBuildStore(b, ncap, icapp);
    LLVMBuildStore(b, cnt, icntp);
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), i_a);
    LLVMBuildCondBr(b, nix_null, miss, fcond);

    /* ---- Rebuild / Populate Table Loop ---- */
    LLVMPositionBuilderAtEnd(b, fcond);
    LLVMValueRef fi = LLVMBuildLoad2(b, i64, i_a, "fi");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntSLT, fi, cnt, "fmore"), fbody, probe);

    LLVMPositionBuilderAtEnd(b, fbody);
    LLVMValueRef cur_cap = LLVMBuildLoad2(b, i64, icapp, "cur_cap");
    LLVMValueRef fmask = zan_sub(b, cur_cap, LLVMConstInt(i64, 1, 0), "fmask");
    LLVMValueRef cur_ix = LLVMBuildLoad2(b, i8ptr, ixp, "cur_ix");
    LLVMValueRef slots_off = zan_add(b, cur_cap, LLVMConstInt(i64, 16, 0), "slots_off");
    LLVMValueRef slots_base = LLVMBuildBitCast(b, LLVMBuildGEP2(b, i8, cur_ix, &slots_off, 1, "sbase8"), i64ptr, "sbase");
    LLVMValueRef rks = LLVMBuildLoad2(b, i8pp, kp, "rks");
    LLVMValueRef fkey = LLVMBuildLoad2(b, i8ptr, LLVMBuildGEP2(b, i8ptr, rks, &fi, 1, "fkeyp"), "fkey");
    LLVMValueRef fh = zan_call2(b, hash_ty, hashf, (LLVMValueRef[]){ fkey, is_str }, 2, "fh");
    LLVMValueRef fh1 = zan_lshr(b, fh, LLVMConstInt(i64, 7, 0), "fh1");
    LLVMValueRef fh2 = LLVMBuildTrunc(b, zan_and(b, fh, LLVMConstInt(i64, 0x7F, 0), "fh2.and"), i8, "fh2");
    LLVMBuildStore(b, fh2, fh2_a);
    LLVMBuildStore(b, zan_and(b, fh1, fmask, "fgidx0"), gidx_a);
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), step_a);
    LLVMBuildBr(b, fp_loop);

    LLVMPositionBuilderAtEnd(b, fp_loop);
    LLVMValueRef cur_gidx = LLVMBuildLoad2(b, i64, gidx_a, "cur_gidx");
    LLVMValueRef cptr = LLVMBuildGEP2(b, i8, cur_ix, &cur_gidx, 1, "cptr");
    LLVMValueRef cvec_p = LLVMBuildBitCast(b, cptr, LLVMPointerType(v16i8, 0), "cvec_p");
    LLVMValueRef cvec = LLVMBuildLoad2(b, v16i8, cvec_p, "cvec");
    LLVMSetAlignment(cvec, 1);
    LLVMValueRef cur_fh2 = LLVMBuildLoad2(b, i8, fh2_a, "cur_fh2");
    LLVMValueRef feq_mask = emit_swisstable_match_mask(g, cvec, cur_fh2);
    LLVMBuildStore(b, feq_mask, mmask_a);
    LLVMValueRef has_dup = zan_icmp(b, LLVMIntNE, feq_mask, LLVMConstInt(i32, 0, 0), "has_dup");
    LLVMBuildCondBr(b, has_dup, fp_dup, fp_empty);

    LLVMPositionBuilderAtEnd(b, fp_dup);
    LLVMValueRef cur_mmask = LLVMBuildLoad2(b, i32, mmask_a, "cur_mmask");
    LLVMValueRef bit = emit_cttz32(g, cur_mmask);
    LLVMValueRef bit64 = LLVMBuildZExt(b, bit, i64, "bit64");
    LLVMValueRef dslot = zan_and(b, zan_add(b, cur_gidx, bit64, "dslot0"), fmask, "dslot");
    LLVMValueRef dentry = LLVMBuildLoad2(b, i64, LLVMBuildGEP2(b, i64, slots_base, &dslot, 1, "dentryp"), "dentry");
    LLVMValueRef valid_idx = zan_icmp(b, LLVMIntSLT, dentry, cnt, "valid_idx");
    LLVMBasicBlockRef fp_keq = LLVMAppendBasicBlockInContext(c, fn, "rb.pkeq");
    LLVMBasicBlockRef fp_dnext = LLVMAppendBasicBlockInContext(c, fn, "rb.pdnext");
    LLVMBuildCondBr(b, valid_idx, fp_keq, fp_dnext);

    LLVMPositionBuilderAtEnd(b, fp_keq);
    LLVMValueRef dkey = LLVMBuildLoad2(b, i8ptr, LLVMBuildGEP2(b, i8ptr, rks, &dentry, 1, "dkeyp"), "dkey");
    LLVMValueRef is_same = zan_call2(b, keq_ty, keq, (LLVMValueRef[]){ dkey, fkey, is_str }, 3, "is_same");
    LLVMBasicBlockRef fp_dup_hit = LLVMAppendBasicBlockInContext(c, fn, "rb.duphit");
    LLVMBuildCondBr(b, is_same, fp_dup_hit, fp_dnext);

    LLVMPositionBuilderAtEnd(b, fp_dup_hit);
    LLVMBuildStore(b, fi, LLVMBuildGEP2(b, i64, slots_base, &dslot, 1, "upd_slot"));
    LLVMBuildBr(b, fnext);

    LLVMPositionBuilderAtEnd(b, fp_dnext);
    /* clear bit: mmask = mmask & (mmask - 1) */
    LLVMValueRef msub1 = zan_sub(b, cur_mmask, LLVMConstInt(i32, 1, 0), "msub1");
    LLVMValueRef mnext = zan_and(b, cur_mmask, msub1, "mnext");
    LLVMBuildStore(b, mnext, mmask_a);
    LLVMValueRef mmore = zan_icmp(b, LLVMIntNE, mnext, LLVMConstInt(i32, 0, 0), "mmore");
    LLVMBuildCondBr(b, mmore, fp_dup, fp_empty);

    LLVMPositionBuilderAtEnd(b, fp_empty);
    LLVMValueRef emask = emit_swisstable_empty_mask(g, cvec);
    LLVMValueRef has_empty = zan_icmp(b, LLVMIntNE, emask, LLVMConstInt(i32, 0, 0), "has_empty");
    LLVMBuildCondBr(b, has_empty, fp_put, fp_step);

    LLVMPositionBuilderAtEnd(b, fp_put);
    LLVMValueRef ebit = emit_cttz32(g, emask);
    LLVMValueRef ebit64 = LLVMBuildZExt(b, ebit, i64, "ebit64");
    LLVMValueRef eslot = zan_and(b, zan_add(b, cur_gidx, ebit64, "eslot0"), fmask, "eslot");
    /* Update ctrl[eslot] = fh2 */
    LLVMBuildStore(b, cur_fh2, LLVMBuildGEP2(b, i8, cur_ix, &eslot, 1, "eslotp"));
    /* If eslot < 16, clone sentinel: ctrl[cur_cap + eslot] = fh2 */
    LLVMValueRef eslot_lt16 = zan_icmp(b, LLVMIntSLT, eslot, LLVMConstInt(i64, 16, 0), "eslot_lt16");
    LLVMBasicBlockRef fp_clone = LLVMAppendBasicBlockInContext(c, fn, "rb.pclone");
    LLVMBasicBlockRef fp_store_entry = LLVMAppendBasicBlockInContext(c, fn, "rb.pstore");
    LLVMBuildCondBr(b, eslot_lt16, fp_clone, fp_store_entry);

    LLVMPositionBuilderAtEnd(b, fp_clone);
    LLVMValueRef clone_idx = zan_add(b, cur_cap, eslot, "clone_idx");
    LLVMBuildStore(b, cur_fh2, LLVMBuildGEP2(b, i8, cur_ix, &clone_idx, 1, "clonep"));
    LLVMBuildBr(b, fp_store_entry);

    LLVMPositionBuilderAtEnd(b, fp_store_entry);
    LLVMBuildStore(b, fi, LLVMBuildGEP2(b, i64, slots_base, &eslot, 1, "eslot_val"));
    LLVMBuildBr(b, fnext);

    LLVMPositionBuilderAtEnd(b, fp_step);
    LLVMValueRef cur_step = LLVMBuildLoad2(b, i64, step_a, "cur_step");
    LLVMValueRef next_step = zan_add(b, cur_step, LLVMConstInt(i64, 16, 0), "next_step");
    LLVMBuildStore(b, next_step, step_a);
    LLVMBuildStore(b, zan_and(b, zan_add(b, cur_gidx, next_step, "gidx_next"), fmask, "gidx_wrap"), gidx_a);
    LLVMBuildBr(b, fp_loop);

    LLVMPositionBuilderAtEnd(b, fnext);
    LLVMValueRef fi_next = zan_add(b, fi, LLVMConstInt(i64, 1, 0), "fi_next");
    LLVMBuildStore(b, fi_next, i_a);
    LLVMBuildBr(b, fcond);

    /* ---- Hot Path Lookup Probe ---- */
    LLVMPositionBuilderAtEnd(b, probe);
    LLVMValueRef pix = LLVMBuildLoad2(b, i8ptr, ixp, "pix");
    LLVMValueRef pcap = LLVMBuildLoad2(b, i64, icapp, "pcap");
    LLVMValueRef pks = LLVMBuildLoad2(b, i8pp, kp, "pks");
    LLVMValueRef pmask = zan_sub(b, pcap, LLVMConstInt(i64, 1, 0), "pmask");
    LLVMValueRef p_slots_off = zan_add(b, pcap, LLVMConstInt(i64, 16, 0), "p_slots_off");
    LLVMValueRef p_slots_base = LLVMBuildBitCast(b,
        LLVMBuildGEP2(b, i8, pix, &p_slots_off, 1, "p_sbase8"), i64ptr, "p_sbase");
    LLVMValueRef ph = zan_call2(b, hash_ty, hashf, (LLVMValueRef[]){ key, is_str }, 2, "ph");
    LLVMValueRef ph1 = zan_lshr(b, ph, LLVMConstInt(i64, 7, 0), "ph1");
    LLVMValueRef ph2 = LLVMBuildTrunc(b, zan_and(b, ph, LLVMConstInt(i64, 0x7F, 0), "ph2.and"), i8, "ph2");
    LLVMBuildStore(b, ph2, fh2_a);
    LLVMBuildStore(b, zan_and(b, ph1, pmask, "pgidx0"), gidx_a);
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), step_a);
    LLVMBuildBr(b, p_loop);

    LLVMPositionBuilderAtEnd(b, p_loop);
    LLVMValueRef p_gidx = LLVMBuildLoad2(b, i64, gidx_a, "p_gidx");
    LLVMValueRef p_cptr = LLVMBuildGEP2(b, i8, pix, &p_gidx, 1, "p_cptr");
    LLVMValueRef p_cvec_p = LLVMBuildBitCast(b, p_cptr, LLVMPointerType(v16i8, 0), "p_cvec_p");
    LLVMValueRef p_cvec = LLVMBuildLoad2(b, v16i8, p_cvec_p, "p_cvec");
    LLVMSetAlignment(p_cvec, 1);
    LLVMValueRef p_h2 = LLVMBuildLoad2(b, i8, fh2_a, "p_h2");
    LLVMValueRef p_mmask = emit_swisstable_match_mask(g, p_cvec, p_h2);
    LLVMBuildStore(b, p_mmask, mmask_a);
    LLVMValueRef p_has_match = zan_icmp(b, LLVMIntNE, p_mmask, LLVMConstInt(i32, 0, 0), "p_has_match");
    LLVMBuildCondBr(b, p_has_match, p_mchk, p_echk);

    LLVMPositionBuilderAtEnd(b, p_mchk);
    LLVMValueRef p_cur_mmask = LLVMBuildLoad2(b, i32, mmask_a, "p_cur_mmask");
    LLVMValueRef p_bit = emit_cttz32(g, p_cur_mmask);
    LLVMValueRef p_bit64 = LLVMBuildZExt(b, p_bit, i64, "p_bit64");
    LLVMValueRef p_slot = zan_and(b, zan_add(b, p_gidx, p_bit64, "p_slot0"), pmask, "p_slot");
    LLVMValueRef p_entry = LLVMBuildLoad2(b, i64,
        LLVMBuildGEP2(b, i64, p_slots_base, &p_slot, 1, "p_entryp"), "p_entry");
    LLVMValueRef p_valid = zan_icmp(b, LLVMIntSLT, p_entry, cnt, "p_valid");
    LLVMBasicBlockRef p_keq_bb = LLVMAppendBasicBlockInContext(c, fn, "p.keq");
    LLVMBasicBlockRef p_mnext_bb = LLVMAppendBasicBlockInContext(c, fn, "p.mnext");
    LLVMBuildCondBr(b, p_valid, p_keq_bb, p_mnext_bb);

    LLVMPositionBuilderAtEnd(b, p_keq_bb);
    LLVMValueRef p_cand_key = LLVMBuildLoad2(b, i8ptr,
        LLVMBuildGEP2(b, i8ptr, pks, &p_entry, 1, "p_cand_keyp"), "p_cand_key");
    LLVMValueRef p_matched = zan_call2(b, keq_ty, keq,
        (LLVMValueRef[]){ p_cand_key, key, is_str }, 3, "p_matched");
    LLVMBuildCondBr(b, p_matched, p_hit, p_mnext_bb);

    LLVMPositionBuilderAtEnd(b, p_hit);
    LLVMBuildRet(b, p_entry);

    LLVMPositionBuilderAtEnd(b, p_mnext_bb);
    LLVMValueRef p_msub1 = zan_sub(b, p_cur_mmask, LLVMConstInt(i32, 1, 0), "p_msub1");
    LLVMValueRef p_mnext = zan_and(b, p_cur_mmask, p_msub1, "p_mnext");
    LLVMBuildStore(b, p_mnext, mmask_a);
    LLVMValueRef p_mmore = zan_icmp(b, LLVMIntNE, p_mnext, LLVMConstInt(i32, 0, 0), "p_mmore");
    LLVMBuildCondBr(b, p_mmore, p_mchk, p_echk);

    LLVMPositionBuilderAtEnd(b, p_echk);
    LLVMValueRef p_emask = emit_swisstable_empty_mask(g, p_cvec);
    LLVMValueRef p_has_empty = zan_icmp(b, LLVMIntNE, p_emask, LLVMConstInt(i32, 0, 0), "p_has_empty");
    LLVMBuildCondBr(b, p_has_empty, miss, p_step);

    LLVMPositionBuilderAtEnd(b, p_step);
    LLVMValueRef p_cur_step = LLVMBuildLoad2(b, i64, step_a, "p_cur_step");
    LLVMValueRef p_next_step = zan_add(b, p_cur_step, LLVMConstInt(i64, 16, 0), "p_next_step");
    LLVMBuildStore(b, p_next_step, step_a);
    LLVMBuildStore(b, zan_and(b, zan_add(b, p_gidx, p_next_step, "p_gidx_next"), pmask, "p_gidx_wrap"), gidx_a);
    LLVMBuildBr(b, p_loop);

    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}
LLVMValueRef get_dict_remove_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_dict_remove");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef dty = g->dict_struct_type;
    LLVMTypeRef fnty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_dict_remove", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMValueRef findf = get_dict_find_fn(g);
    LLVMTypeRef find_ty = LLVMGlobalGetValueType(findf);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef miss  = LLVMAppendBasicBlockInContext(c, fn, "miss");
    LLVMBasicBlockRef prep  = LLVMAppendBasicBlockInContext(c, fn, "prep");
    LLVMBasicBlockRef hit   = LLVMAppendBasicBlockInContext(c, fn, "hit");

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef draw = LLVMGetParam(fn, 0);
    LLVMValueRef key = LLVMGetParam(fn, 1);
    LLVMValueRef is_str = LLVMGetParam(fn, 2);
    LLVMValueRef dnull = zan_icmp(b, LLVMIntEQ, draw, LLVMConstNull(i8ptr), "dnull");
    LLVMBuildCondBr(b, dnull, miss, prep);

    LLVMPositionBuilderAtEnd(b, miss);
    LLVMBuildRet(b, LLVMConstInt(i64, (unsigned long long)-1, 1));

    LLVMPositionBuilderAtEnd(b, prep);
    LLVMValueRef dp = LLVMBuildBitCast(b, draw, LLVMPointerType(dty, 0), "dp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(b, dty, dp, 0, "cntp");
    LLVMValueRef icntp = LLVMBuildStructGEP2(b, dty, dp, 6, "icntp");
    LLVMValueRef fi = zan_call2(b, find_ty, findf,
        (LLVMValueRef[]){ draw, key, is_str }, 3, "rm.find");
    LLVMValueRef nokey = zan_icmp(b, LLVMIntSLT, fi, LLVMConstInt(i64, 0, 0), "nokey");
    LLVMBuildCondBr(b, nokey, miss, hit);

    LLVMPositionBuilderAtEnd(b, hit);
    LLVMValueRef cnt = LLVMBuildLoad2(b, i64, cntp, "cnt");
    LLVMBuildStore(b, zan_sub(b, cnt, LLVMConstInt(i64, 1, 0), "cnt.d"), cntp);
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), icntp);
    LLVMBuildRet(b, fi);
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}
LLVMValueRef get_dict_set_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_dict_set");
    if (fn) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
    LLVMTypeRef fnty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_dict_set", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef realloc_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef hit_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "hit");
    LLVMBasicBlockRef app_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "app");
    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "grow");
    LLVMBasicBlockRef put_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "put");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef draw = LLVMGetParam(fn, 0);
    LLVMValueRef key = LLVMGetParam(fn, 1);
    LLVMValueRef is_str = LLVMGetParam(fn, 2);
    LLVMValueRef dp = LLVMBuildBitCast(g->builder, draw,
        LLVMPointerType(g->dict_struct_type, 0), "dp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
    LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
    LLVMValueRef capp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 1, "capp");
    LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
    LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
    LLVMValueRef vwp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 7, "vwp");
    LLVMValueRef ks = LLVMBuildLoad2(g->builder, i8pp, kp, "ks");
    LLVMValueRef vs = LLVMBuildLoad2(g->builder, i64ptr, vp, "vs");
    LLVMValueRef value_words = LLVMBuildLoad2(g->builder, i64, vwp, "vw");
    LLVMValueRef findf = get_dict_find_fn(g);
    LLVMValueRef ci = zan_call2(g->builder, LLVMGlobalGetValueType(findf), findf,
        (LLVMValueRef[]){ draw, key, is_str }, 3, "dset.find");
    LLVMValueRef eq = zan_icmp(g->builder, LLVMIntSGE, ci,
        LLVMConstInt(i64, 0, 0), "eq");
    LLVMBuildCondBr(g->builder, eq, hit_bb, app_bb);
    LLVMPositionBuilderAtEnd(g->builder, hit_bb);
    LLVMBuildRet(g->builder, ci);
    LLVMPositionBuilderAtEnd(g->builder, app_bb);
    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, capp, "cap");
    LLVMValueRef full = zan_icmp(g->builder, LLVMIntSGE, cnt, cap, "full");
    LLVMBuildCondBr(g->builder, full, grow_bb, put_bb);
    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
    LLVMValueRef cap0 = zan_icmp(g->builder, LLVMIntEQ, cap,
        LLVMConstInt(i64, 0, 0), "cap0");
    LLVMValueRef ncap = LLVMBuildSelect(g->builder, cap0,
        LLVMConstInt(i64, 16, 0),
        zan_mul(g->builder, cap, LLVMConstInt(i64, 2, 0), "cap2"), "ncap");
    LLVMValueRef key_bytes = zan_mul(g->builder, ncap, LLVMConstInt(i64, 8, 0), "kbytes");
    LLVMValueRef value_bytes = zan_mul(g->builder, key_bytes, value_words, "vbytes");
    LLVMValueRef ks8 = LLVMBuildBitCast(g->builder, ks, i8ptr, "ks8");
    LLVMValueRef nks8 = zan_call2(g->builder, realloc_ty, g->fn_realloc,
        (LLVMValueRef[]){ ks8, key_bytes }, 2, "nks8");
    zan_irgen_emit_oom_check(g, fn, nks8);
    LLVMBuildStore(g->builder, LLVMBuildBitCast(g->builder, nks8, i8pp, "nks"), kp);
    LLVMValueRef vs8 = LLVMBuildBitCast(g->builder, vs, i8ptr, "vs8");
    LLVMValueRef nvs8 = zan_call2(g->builder, realloc_ty, g->fn_realloc,
        (LLVMValueRef[]){ vs8, value_bytes }, 2, "nvs8");
    zan_irgen_emit_oom_check(g, fn, nvs8);
    LLVMBuildStore(g->builder, LLVMBuildBitCast(g->builder, nvs8, i64ptr, "nvs"), vp);
    LLVMBuildStore(g->builder, ncap, capp);
    LLVMBuildBr(g->builder, put_bb);
    LLVMPositionBuilderAtEnd(g->builder, put_bb);
    LLVMValueRef ks2 = LLVMBuildLoad2(g->builder, i8pp, kp, "ks2");
    LLVMValueRef kslot2 = LLVMBuildGEP2(g->builder, i8ptr, ks2, &cnt, 1, "ksl2");
    LLVMBuildStore(g->builder, key, kslot2);
    LLVMBuildStore(g->builder,
        zan_add(g->builder, cnt, LLVMConstInt(i64, 1, 0), "ncnt"), cntp);
    LLVMBuildRet(g->builder, cnt);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* 异常展开栈与处理器栈存储：每线程独立状态块 */

enum {
    EH_F_TOP = 0,
    EH_F_TMPS_TOP,
    EH_F_EXC_OWNED,
    EH_F_PAD,
    EH_F_EXC,
    EH_F_EXC_TID,
    EH_F_BUFS,
    EH_F_TMPS,
    EH_F_COUNT
};

static LLVMValueRef eh_add_global(zan_irgen_t *g, LLVMTypeRef ty,
                                 const char *name, LLVMValueRef init) {
    LLVMValueRef v = LLVMAddGlobal(g->mod, ty, name);
    LLVMSetLinkage(v, LLVMInternalLinkage);
    LLVMSetInitializer(v, init);
    return v;
}

