/* irgen_builtins.c -- builtin string-method helpers and EH-owned temporaries.
 *
 * Part of the irgen translation unit: this file is #include'd by irgen.c
 * (in a fixed order) and must not be compiled standalone. Splitting keeps
 * the single-TU static linkage while keeping each concern in its own file.
 */

/* ---- builtin string-method helpers (lazily emitted once per module) ----
 * Bodies use libc (strlen/strstr/memcpy/toupper) plus zan_rt_str_alloc, so
 * every returned string is an ordinary owned rc string. */

static LLVMValueRef get_libc_fn(zan_irgen_t *g, const char *name, LLVMTypeRef ty) {
    LLVMValueRef f = LLVMGetNamedFunction(g->mod, name);
    if (!f) f = LLVMAddFunction(g->mod, name, ty);
    return f;
}

/* __zan_str_last_index_of(s, sub) -> i64: byte index of the last occurrence
 * of `sub` in `s`, or -1. An empty needle yields strlen(s). */
static LLVMValueRef get_str_last_index_of_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_last_index_of");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_last_index_of", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef strstr_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMValueRef strstr_fn = get_libc_fn(g, "strstr", strstr_ty);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef empty_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "empty");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "loop");
    LLVMBasicBlockRef found = LLVMAppendBasicBlockInContext(g->ctx, fn, "found");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef sub = LLVMGetParam(fn, 1);
    LLVMValueRef best = LLVMBuildAlloca(g->builder, i64, "best");
    LLVMValueRef cur = LLVMBuildAlloca(g->builder, i8ptr, "cur");
    LLVMBuildStore(g->builder, LLVMConstInt(i64, (uint64_t)-1, 1), best);
    LLVMBuildStore(g->builder, s, cur);
    LLVMValueRef sub0 = LLVMBuildLoad2(g->builder, i8, sub, "sub0");
    LLVMValueRef sub_empty = zan_icmp(g->builder, LLVMIntEQ, sub0,
        LLVMConstInt(i8, 0, 0), "subempty");
    LLVMBuildCondBr(g->builder, sub_empty, empty_bb, loop);
    LLVMPositionBuilderAtEnd(g->builder, empty_bb);
    LLVMValueRef slen = zan_call2(g->builder, strlen_ty, g->fn_strlen, &s, 1, "slen");
    LLVMBuildRet(g->builder, slen);
    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef p = LLVMBuildLoad2(g->builder, i8ptr, cur, "p");
    LLVMValueRef ss_args[] = { p, sub };
    LLVMValueRef hit = zan_call2(g->builder, strstr_ty, strstr_fn, ss_args, 2, "hit");
    LLVMValueRef miss = zan_icmp(g->builder, LLVMIntEQ, hit,
        LLVMConstNull(i8ptr), "miss");
    LLVMBuildCondBr(g->builder, miss, done, found);
    LLVMPositionBuilderAtEnd(g->builder, found);
    LLVMValueRef idx = zan_sub(g->builder,
        LLVMBuildPtrToInt(g->builder, hit, i64, "hi"),
        LLVMBuildPtrToInt(g->builder, s, i64, "si"), "idx");
    LLVMBuildStore(g->builder, idx, best);
    LLVMValueRef one = LLVMConstInt(i64, 1, 0);
    LLVMValueRef next = LLVMBuildGEP2(g->builder, i8, hit, &one, 1, "next");
    LLVMBuildStore(g->builder, next, cur);
    LLVMBuildBr(g->builder, loop);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildRet(g->builder, LLVMBuildLoad2(g->builder, i64, best, "res"));
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* __zan_str_trim(s) -> i8*: fresh rc string with leading/trailing ASCII
 * whitespace (space, \t, \r, \n, \v, \f) removed. */
static LLVMValueRef get_str_trim_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_trim");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_trim", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef isspace_ty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i32 }, 1, 0);
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef isspace_fn = get_libc_fn(g, "isspace", isspace_ty);
    LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef lead = LLVMAppendBasicBlockInContext(g->ctx, fn, "lead");
    LLVMBasicBlockRef lead_ws = LLVMAppendBasicBlockInContext(g->ctx, fn, "lead_ws");
    LLVMBasicBlockRef tail_init = LLVMAppendBasicBlockInContext(g->ctx, fn, "tail_init");
    LLVMBasicBlockRef tail = LLVMAppendBasicBlockInContext(g->ctx, fn, "tail");
    LLVMBasicBlockRef tail_chk = LLVMAppendBasicBlockInContext(g->ctx, fn, "tail_chk");
    LLVMBasicBlockRef tail_ws = LLVMAppendBasicBlockInContext(g->ctx, fn, "tail_ws");
    LLVMBasicBlockRef copy = LLVMAppendBasicBlockInContext(g->ctx, fn, "copy");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef pvar = LLVMBuildAlloca(g->builder, i8ptr, "pvar");
    LLVMValueRef nvar = LLVMBuildAlloca(g->builder, i64, "nvar");
    LLVMBuildStore(g->builder, s, pvar);
    LLVMBuildBr(g->builder, lead);
    LLVMPositionBuilderAtEnd(g->builder, lead);
    LLVMValueRef p = LLVMBuildLoad2(g->builder, i8ptr, pvar, "p");
    LLVMValueRef c = LLVMBuildLoad2(g->builder, i8, p, "c");
    LLVMValueRef c32 = LLVMBuildZExt(g->builder, c, i32, "c32");
    LLVMValueRef ws = zan_call2(g->builder, isspace_ty, isspace_fn, &c32, 1, "ws");
    LLVMValueRef nz = zan_icmp(g->builder, LLVMIntNE, ws,
        LLVMConstInt(i32, 0, 0), "isws");
    LLVMValueRef not_nul = zan_icmp(g->builder, LLVMIntNE, c,
        LLVMConstInt(i8, 0, 0), "notnul");
    LLVMValueRef adv = zan_and(g->builder, nz, not_nul, "adv");
    LLVMBuildCondBr(g->builder, adv, lead_ws, tail_init);
    LLVMPositionBuilderAtEnd(g->builder, lead_ws);
    LLVMValueRef one64 = LLVMConstInt(i64, 1, 0);
    LLVMValueRef p1 = LLVMBuildGEP2(g->builder, i8, p, &one64, 1, "p1");
    LLVMBuildStore(g->builder, p1, pvar);
    LLVMBuildBr(g->builder, lead);
    LLVMPositionBuilderAtEnd(g->builder, tail_init);
    LLVMValueRef base = LLVMBuildLoad2(g->builder, i8ptr, pvar, "base");
    LLVMValueRef n0 = zan_call2(g->builder, strlen_ty, g->fn_strlen, &base, 1, "n0");
    LLVMBuildStore(g->builder, n0, nvar);
    LLVMBuildBr(g->builder, tail);
    LLVMPositionBuilderAtEnd(g->builder, tail);
    LLVMValueRef n = LLVMBuildLoad2(g->builder, i64, nvar, "n");
    LLVMValueRef npos = zan_icmp(g->builder, LLVMIntSGT, n,
        LLVMConstInt(i64, 0, 0), "npos");
    LLVMBuildCondBr(g->builder, npos, tail_chk, copy);
    LLVMPositionBuilderAtEnd(g->builder, tail_chk);
    LLVMValueRef nm1 = zan_sub(g->builder, n, one64, "nm1");
    LLVMValueRef lastp = LLVMBuildGEP2(g->builder, i8, base, &nm1, 1, "lastp");
    LLVMValueRef lc = LLVMBuildLoad2(g->builder, i8, lastp, "lc");
    LLVMValueRef lc32 = LLVMBuildZExt(g->builder, lc, i32, "lc32");
    LLVMValueRef lws = zan_call2(g->builder, isspace_ty, isspace_fn, &lc32, 1, "lws");
    LLVMValueRef lnz = zan_icmp(g->builder, LLVMIntNE, lws,
        LLVMConstInt(i32, 0, 0), "lisws");
    LLVMBuildCondBr(g->builder, lnz, tail_ws, copy);
    LLVMPositionBuilderAtEnd(g->builder, tail_ws);
    LLVMBuildStore(g->builder, nm1, nvar);
    LLVMBuildBr(g->builder, tail);
    LLVMPositionBuilderAtEnd(g->builder, copy);
    LLVMValueRef fin_n = LLVMBuildLoad2(g->builder, i64, nvar, "finn");
    LLVMValueRef bufsz = zan_add(g->builder, fin_n, one64, "bsz");
    LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
    LLVMValueRef mcargs[] = { buf, base, fin_n };
    zan_call2(g->builder, memcpy_ty, memcpy_fn, mcargs, 3, "");
    LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, buf, &fin_n, 1, "endp");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
    LLVMBuildRet(g->builder, buf);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* __zan_str_to_upper / __zan_str_to_lower: per-byte toupper()/tolower() into
 * a fresh rc string. */
static LLVMValueRef get_str_case_fn(zan_irgen_t *g, int upper) {
    const char *fname = upper ? "__zan_str_to_upper" : "__zan_str_to_lower";
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fname);
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    fn = LLVMAddFunction(g->mod, fname, fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef conv_ty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i32 }, 1, 0);
    LLVMValueRef conv_fn = get_libc_fn(g, upper ? "toupper" : "tolower", conv_ty);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "loop");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef n = zan_call2(g->builder, strlen_ty, g->fn_strlen, &s, 1, "n");
    LLVMValueRef bufsz = zan_add(g->builder, n, LLVMConstInt(i64, 1, 0), "bsz");
    LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
    LLVMValueRef ivar = LLVMBuildAlloca(g->builder, i64, "ivar");
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), ivar);
    LLVMBuildBr(g->builder, loop);
    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef i = LLVMBuildLoad2(g->builder, i64, ivar, "i");
    LLVMValueRef more = zan_icmp(g->builder, LLVMIntULT, i, n, "more");
    LLVMBuildCondBr(g->builder, more, body, done);
    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef sp = LLVMBuildGEP2(g->builder, i8, s, &i, 1, "sp");
    LLVMValueRef c = LLVMBuildLoad2(g->builder, i8, sp, "c");
    LLVMValueRef c32 = LLVMBuildZExt(g->builder, c, i32, "c32");
    LLVMValueRef conv = zan_call2(g->builder, conv_ty, conv_fn, &c32, 1, "conv");
    LLVMValueRef c8 = LLVMBuildTrunc(g->builder, conv, i8, "c8");
    LLVMValueRef dp = LLVMBuildGEP2(g->builder, i8, buf, &i, 1, "dp");
    LLVMBuildStore(g->builder, c8, dp);
    LLVMValueRef i1v = zan_add(g->builder, i, LLVMConstInt(i64, 1, 0), "i1");
    LLVMBuildStore(g->builder, i1v, ivar);
    LLVMBuildBr(g->builder, loop);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, buf, &n, 1, "endp");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
    LLVMBuildRet(g->builder, buf);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* __zan_str_replace(s, from, to) -> i8*: fresh rc string with every
 * non-overlapping occurrence of `from` replaced by `to`. An empty `from`
 * returns a copy of `s`. */
static LLVMValueRef get_str_replace_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_replace");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_replace", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef strstr_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef strstr_fn = get_libc_fn(g, "strstr", strstr_ty);
    LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef dup_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "dup");
    LLVMBasicBlockRef cnt_loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "cnt_loop");
    LLVMBasicBlockRef cnt_hit = LLVMAppendBasicBlockInContext(g->ctx, fn, "cnt_hit");
    LLVMBasicBlockRef alloc_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "alloc");
    LLVMBasicBlockRef bld_loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "bld_loop");
    LLVMBasicBlockRef bld_hit = LLVMAppendBasicBlockInContext(g->ctx, fn, "bld_hit");
    LLVMBasicBlockRef tail_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "tail");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef from = LLVMGetParam(fn, 1);
    LLVMValueRef to = LLVMGetParam(fn, 2);
    LLVMValueRef one64 = LLVMConstInt(i64, 1, 0);
    LLVMValueRef ls = zan_call2(g->builder, strlen_ty, g->fn_strlen, &s, 1, "ls");
    LLVMValueRef lf = zan_call2(g->builder, strlen_ty, g->fn_strlen, &from, 1, "lf");
    LLVMValueRef lt = zan_call2(g->builder, strlen_ty, g->fn_strlen, &to, 1, "lt");
    LLVMValueRef cntv = LLVMBuildAlloca(g->builder, i64, "cntv");
    LLVMValueRef pvar = LLVMBuildAlloca(g->builder, i8ptr, "pvar");
    LLVMValueRef dvar = LLVMBuildAlloca(g->builder, i8ptr, "dvar");
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), cntv);
    LLVMBuildStore(g->builder, s, pvar);
    LLVMValueRef from_empty = zan_icmp(g->builder, LLVMIntEQ, lf,
        LLVMConstInt(i64, 0, 0), "fempty");
    LLVMBuildCondBr(g->builder, from_empty, dup_bb, cnt_loop);
    LLVMPositionBuilderAtEnd(g->builder, dup_bb);
    LLVMValueRef dupsz = zan_add(g->builder, ls, one64, "dupsz");
    LLVMValueRef dupbuf = emit_string_alloc_rc(g, dupsz);
    LLVMValueRef dupargs[] = { dupbuf, s, dupsz };
    zan_call2(g->builder, memcpy_ty, memcpy_fn, dupargs, 3, "");
    LLVMBuildRet(g->builder, dupbuf);
    LLVMPositionBuilderAtEnd(g->builder, cnt_loop);
    LLVMValueRef cp = LLVMBuildLoad2(g->builder, i8ptr, pvar, "cp");
    LLVMValueRef cargs[] = { cp, from };
    LLVMValueRef chit = zan_call2(g->builder, strstr_ty, strstr_fn, cargs, 2, "chit");
    LLVMValueRef cmiss = zan_icmp(g->builder, LLVMIntEQ, chit,
        LLVMConstNull(i8ptr), "cmiss");
    LLVMBuildCondBr(g->builder, cmiss, alloc_bb, cnt_hit);
    LLVMPositionBuilderAtEnd(g->builder, cnt_hit);
    LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntv, "cnt");
    LLVMBuildStore(g->builder, zan_add(g->builder, cnt, one64, "cnt1"), cntv);
    LLVMValueRef cnext = LLVMBuildGEP2(g->builder, i8, chit, &lf, 1, "cnext");
    LLVMBuildStore(g->builder, cnext, pvar);
    LLVMBuildBr(g->builder, cnt_loop);
    LLVMPositionBuilderAtEnd(g->builder, alloc_bb);
    LLVMValueRef total_cnt = LLVMBuildLoad2(g->builder, i64, cntv, "tcnt");
    LLVMValueRef delta = zan_sub(g->builder, lt, lf, "delta");
    LLVMValueRef grow = zan_mul(g->builder, total_cnt, delta, "grow");
    LLVMValueRef newlen = zan_add(g->builder, ls, grow, "newlen");
    LLVMValueRef bufsz = zan_add(g->builder, newlen, one64, "bsz");
    LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
    LLVMBuildStore(g->builder, s, pvar);
    LLVMBuildStore(g->builder, buf, dvar);
    LLVMBuildBr(g->builder, bld_loop);
    LLVMPositionBuilderAtEnd(g->builder, bld_loop);
    LLVMValueRef bp = LLVMBuildLoad2(g->builder, i8ptr, pvar, "bp");
    LLVMValueRef bargs[] = { bp, from };
    LLVMValueRef bhit = zan_call2(g->builder, strstr_ty, strstr_fn, bargs, 2, "bhit");
    LLVMValueRef bmiss = zan_icmp(g->builder, LLVMIntEQ, bhit,
        LLVMConstNull(i8ptr), "bmiss");
    LLVMBuildCondBr(g->builder, bmiss, tail_bb, bld_hit);
    LLVMPositionBuilderAtEnd(g->builder, bld_hit);
    LLVMValueRef d = LLVMBuildLoad2(g->builder, i8ptr, dvar, "d");
    LLVMValueRef pre = zan_sub(g->builder,
        LLVMBuildPtrToInt(g->builder, bhit, i64, "bhi"),
        LLVMBuildPtrToInt(g->builder, bp, i64, "bpi"), "pre");
    LLVMValueRef pre_args[] = { d, bp, pre };
    zan_call2(g->builder, memcpy_ty, memcpy_fn, pre_args, 3, "");
    LLVMValueRef d1 = LLVMBuildGEP2(g->builder, i8, d, &pre, 1, "d1");
    LLVMValueRef to_args[] = { d1, to, lt };
    zan_call2(g->builder, memcpy_ty, memcpy_fn, to_args, 3, "");
    LLVMValueRef d2 = LLVMBuildGEP2(g->builder, i8, d1, &lt, 1, "d2");
    LLVMBuildStore(g->builder, d2, dvar);
    LLVMValueRef bnext = LLVMBuildGEP2(g->builder, i8, bhit, &lf, 1, "bnext");
    LLVMBuildStore(g->builder, bnext, pvar);
    LLVMBuildBr(g->builder, bld_loop);
    LLVMPositionBuilderAtEnd(g->builder, tail_bb);
    LLVMValueRef tp = LLVMBuildLoad2(g->builder, i8ptr, pvar, "tp");
    LLVMValueRef td = LLVMBuildLoad2(g->builder, i8ptr, dvar, "td");
    LLVMValueRef rest = zan_call2(g->builder, strlen_ty, g->fn_strlen, &tp, 1, "rest");
    LLVMValueRef restsz = zan_add(g->builder, rest, one64, "restsz");
    LLVMValueRef rest_args[] = { td, tp, restsz };
    zan_call2(g->builder, memcpy_ty, memcpy_fn, rest_args, 3, "");
    LLVMBuildRet(g->builder, buf);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* void __zan_list_push_strn(i8* lst, i8* p, i64 n): copy n bytes of p into a
 * fresh rc string and push it onto the List (growing the i64 data buffer). */
static LLVMValueRef get_list_push_strn_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_list_push_strn");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_list_push_strn", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "grow");
    LLVMBasicBlockRef push_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "push");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef lst = LLVMGetParam(fn, 0);
    LLVMValueRef p = LLVMGetParam(fn, 1);
    LLVMValueRef n = LLVMGetParam(fn, 2);
    LLVMValueRef one64 = LLVMConstInt(i64, 1, 0);
    /* fresh rc string with the segment bytes */
    LLVMValueRef ssz = zan_add(g->builder, n, one64, "ssz");
    LLVMValueRef str = emit_string_alloc_rc(g, ssz);
    zan_call2(g->builder, memcpy_ty, memcpy_fn,
        (LLVMValueRef[]){ str, p, n }, 3, "");
    LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, str, &n, 1, "endp");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
    LLVMValueRef lp = LLVMBuildBitCast(g->builder, lst,
        LLVMPointerType(g->list_struct_type, 0), "lp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "cntp");
    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
    LLVMValueRef capp = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 1, "capp");
    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, capp, "cap");
    LLVMValueRef full = zan_icmp(g->builder, LLVMIntSGE, count, cap, "full");
    LLVMBuildCondBr(g->builder, full, grow_bb, push_bb);
    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
    LLVMValueRef cap0 = zan_icmp(g->builder, LLVMIntEQ, cap,
        LLVMConstInt(i64, 0, 0), "cap0");
    LLVMValueRef ncap = LLVMBuildSelect(g->builder, cap0,
        LLVMConstInt(i64, 4, 0),
        zan_mul(g->builder, cap, LLVMConstInt(i64, 2, 0), "cap2"), "ncap");
    LLVMValueRef datap = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "datap");
    LLVMValueRef odata = LLVMBuildLoad2(g->builder, i64ptr, datap, "odata");
    LLVMValueRef odata8 = LLVMBuildBitCast(g->builder, odata, i8ptr, "odata8");
    LLVMValueRef nbytes = zan_mul(g->builder, ncap, LLVMConstInt(i64, 8, 0), "nbytes");
    LLVMValueRef ndata8 = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
        g->fn_realloc, (LLVMValueRef[]){ odata8, nbytes }, 2, "ndata8");
    zan_irgen_emit_oom_check(g, fn, ndata8);
    LLVMValueRef ndata = LLVMBuildBitCast(g->builder, ndata8, i64ptr, "ndata");
    LLVMBuildStore(g->builder, ndata, datap);
    LLVMBuildStore(g->builder, ncap, capp);
    LLVMBuildBr(g->builder, push_bb);
    LLVMPositionBuilderAtEnd(g->builder, push_bb);
    LLVMValueRef datap2 = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "datap2");
    LLVMValueRef data = LLVMBuildLoad2(g->builder, i64ptr, datap2, "data");
    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &count, 1, "slot");
    LLVMBuildStore(g->builder, LLVMBuildPtrToInt(g->builder, str, i64, "sv"), slot);
    LLVMBuildStore(g->builder, zan_add(g->builder, count, one64, "ncnt"), cntp);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* void __zan_str_split(i8* s, i8* sep, i8* lst): split s on every occurrence
 * of sep and push the segments (fresh rc strings) onto the List. An empty
 * separator yields the whole string as a single element. */
static LLVMValueRef get_str_split_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_split");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_split", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef strstr_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMValueRef strstr_fn = get_libc_fn(g, "strstr", strstr_ty);
    LLVMValueRef push_fn = get_list_push_strn_fn(g);
    LLVMTypeRef push_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "loop");
    LLVMBasicBlockRef hit_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "hit");
    LLVMBasicBlockRef last_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "last");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef sep = LLVMGetParam(fn, 1);
    LLVMValueRef lst = LLVMGetParam(fn, 2);
    LLVMValueRef seplen = zan_call2(g->builder, strlen_ty, g->fn_strlen, &sep, 1, "seplen");
    LLVMValueRef pvar = LLVMBuildAlloca(g->builder, i8ptr, "pvar");
    LLVMBuildStore(g->builder, s, pvar);
    LLVMValueRef sep_empty = zan_icmp(g->builder, LLVMIntEQ, seplen,
        LLVMConstInt(i64, 0, 0), "sempty");
    LLVMBuildCondBr(g->builder, sep_empty, last_bb, loop_bb);
    LLVMPositionBuilderAtEnd(g->builder, loop_bb);
    LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr, pvar, "cur");
    LLVMValueRef hit = zan_call2(g->builder, strstr_ty, strstr_fn,
        (LLVMValueRef[]){ cur, sep }, 2, "hit");
    LLVMValueRef miss = zan_icmp(g->builder, LLVMIntEQ, hit,
        LLVMConstNull(i8ptr), "miss");
    LLVMBuildCondBr(g->builder, miss, last_bb, hit_bb);
    LLVMPositionBuilderAtEnd(g->builder, hit_bb);
    LLVMValueRef seg = zan_sub(g->builder,
        LLVMBuildPtrToInt(g->builder, hit, i64, "hi"),
        LLVMBuildPtrToInt(g->builder, cur, i64, "ci"), "seg");
    zan_call2(g->builder, push_ty, push_fn,
        (LLVMValueRef[]){ lst, cur, seg }, 3, "");
    LLVMValueRef next = LLVMBuildGEP2(g->builder, i8, hit, &seplen, 1, "next");
    LLVMBuildStore(g->builder, next, pvar);
    LLVMBuildBr(g->builder, loop_bb);
    LLVMPositionBuilderAtEnd(g->builder, last_bb);
    LLVMValueRef tp = LLVMBuildLoad2(g->builder, i8ptr, pvar, "tp");
    LLVMValueRef rest = zan_call2(g->builder, strlen_ty, g->fn_strlen, &tp, 1, "rest");
    zan_call2(g->builder, push_ty, push_fn,
        (LLVMValueRef[]){ lst, tp, rest }, 3, "");
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* i1 __zan_dict_key_eq(i8* a, i8* b, i64 is_str): key comparison for Dict
 * scans — strcmp equality for string keys, raw pointer/bit equality for
 * scalar keys (stored inttoptr'd in the i8** key slots). */
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
    LLVMTypeRef strcmp_ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMValueRef strcmp_fn = get_libc_fn(g, "strcmp", strcmp_ty);
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
    LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty, strcmp_fn,
        (LLVMValueRef[]){ a, b2 }, 2, "cmp");
    LLVMBuildRet(g->builder, zan_icmp(g->builder, LLVMIntEQ, cmp,
        LLVMConstInt(i32t, 0, 0), "eq"));
    LLVMPositionBuilderAtEnd(g->builder, raw_bb);
    LLVMBuildRet(g->builder, zan_icmp(g->builder, LLVMIntEQ, a, b2, "peq"));
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* i64 __zan_dict_hash(i8* key, i64 is_str): FNV-1a over the key bytes for
 * string keys, a 64-bit avalanche of the pointer/scalar bits otherwise. */
static LLVMValueRef get_dict_hash_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_dict_hash");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef fnty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_dict_hash", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef nul   = LLVMAppendBasicBlockInContext(c, fn, "nul");
    LLVMBasicBlockRef raw   = LLVMAppendBasicBlockInContext(c, fn, "raw");
    LLVMBasicBlockRef sini  = LLVMAppendBasicBlockInContext(c, fn, "s.init");
    LLVMBasicBlockRef scond = LLVMAppendBasicBlockInContext(c, fn, "s.cond");
    LLVMBasicBlockRef sbody = LLVMAppendBasicBlockInContext(c, fn, "s.body");
    LLVMBasicBlockRef sdone = LLVMAppendBasicBlockInContext(c, fn, "s.done");
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef key = LLVMGetParam(fn, 0);
    LLVMValueRef is_str = LLVMGetParam(fn, 1);
    LLVMValueRef h_a = LLVMBuildAlloca(b, i64, "h");
    LLVMValueRef p_a = LLVMBuildAlloca(b, i8ptr, "p");
    LLVMValueRef isn = zan_icmp(b, LLVMIntEQ, key, LLVMConstNull(i8ptr), "isn");
    LLVMBuildCondBr(b, isn, nul, sini);
    LLVMPositionBuilderAtEnd(b, nul);
    LLVMBuildRet(b, LLVMConstInt(i64, 0, 0));
    LLVMPositionBuilderAtEnd(b, sini);
    LLVMValueRef strk = zan_icmp(b, LLVMIntNE, is_str, LLVMConstInt(i64, 0, 0), "strk");
    LLVMBuildCondBr(b, strk, scond, raw);
    /* scalar key: splitmix-style finaliser over the raw bits */
    LLVMPositionBuilderAtEnd(b, raw);
    LLVMValueRef v = LLVMBuildPtrToInt(b, key, i64, "kv");
    v = zan_xor(b, v, zan_lshr(b, v, LLVMConstInt(i64, 33, 0), "s1"), "x1");
    v = zan_mul(b, v, LLVMConstInt(i64, 0xff51afd7ed558ccdULL, 0), "m1");
    v = zan_xor(b, v, zan_lshr(b, v, LLVMConstInt(i64, 29, 0), "s2"), "x2");
    v = zan_mul(b, v, LLVMConstInt(i64, 0xc4ceb9fe1a85ec53ULL, 0), "m2");
    v = zan_xor(b, v, zan_lshr(b, v, LLVMConstInt(i64, 32, 0), "s3"), "x3");
    LLVMBuildRet(b, v);
    /* string key: FNV-1a to the NUL terminator */
    LLVMPositionBuilderAtEnd(b, scond);
    LLVMBuildStore(b, LLVMConstInt(i64, 1469598103934665603ULL, 0), h_a);
    LLVMBuildStore(b, key, p_a);
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(c, fn, "s.loop");
    LLVMBuildBr(b, loop);
    LLVMPositionBuilderAtEnd(b, loop);
    LLVMValueRef p = LLVMBuildLoad2(b, i8ptr, p_a, "p.cur");
    LLVMValueRef ch = LLVMBuildLoad2(b, i8, p, "ch");
    LLVMValueRef end = zan_icmp(b, LLVMIntEQ, ch, LLVMConstInt(i8, 0, 0), "end");
    LLVMBuildCondBr(b, end, sdone, sbody);
    LLVMPositionBuilderAtEnd(b, sbody);
    LLVMValueRef hv = LLVMBuildLoad2(b, i64, h_a, "h.cur");
    LLVMValueRef c64 = LLVMBuildZExt(b, ch, i64, "ch64");
    hv = zan_xor(b, hv, c64, "h.x");
    hv = zan_mul(b, hv, LLVMConstInt(i64, 1099511628211ULL, 0), "h.m");
    LLVMBuildStore(b, hv, h_a);
    LLVMValueRef one = LLVMConstInt(i64, 1, 0);
    LLVMBuildStore(b, LLVMBuildGEP2(b, i8, p, &one, 1, "p.next"), p_a);
    LLVMBuildBr(b, loop);
    LLVMPositionBuilderAtEnd(b, sdone);
    LLVMBuildRet(b, LLVMBuildLoad2(b, i64, h_a, "h.out"));
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}

/* i64 __zan_dict_find(i8* draw, i8* key, i64 is_str): index of the entry with
 * `key`, or -1. Probes the dict's open-addressed hash index, (re)building it
 * from the insertion-ordered key buffer when it is missing or stale -- so the
 * inline mutators (Add/Clear/the indexer's upsert) need no bookkeeping beyond
 * the count they already maintain; Remove clears index_capacity because it can
 * leave the count unchanged relative to a previous build. Duplicate keys keep
 * their first occurrence, matching the linear scan this replaces. */
static LLVMValueRef get_dict_find_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_dict_find");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef dty = g->dict_struct_type;
    LLVMTypeRef fnty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_dict_find", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMValueRef hashf = get_dict_hash_fn(g);
    LLVMTypeRef hash_ty = LLVMGlobalGetValueType(hashf);
    LLVMValueRef keq = get_dict_key_eq_fn(g);
    LLVMTypeRef keq_ty = LLVMGlobalGetValueType(keq);
    LLVMTypeRef calloc_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0);
    LLVMValueRef calloc_fn = get_calloc_fn(g);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMBasicBlockRef entry  = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef miss   = LLVMAppendBasicBlockInContext(c, fn, "miss");
    LLVMBasicBlockRef chk    = LLVMAppendBasicBlockInContext(c, fn, "chk");
    LLVMBasicBlockRef capc   = LLVMAppendBasicBlockInContext(c, fn, "rb.capc");
    LLVMBasicBlockRef capb   = LLVMAppendBasicBlockInContext(c, fn, "rb.capb");
    LLVMBasicBlockRef alloc  = LLVMAppendBasicBlockInContext(c, fn, "rb.alloc");
    LLVMBasicBlockRef fcond  = LLVMAppendBasicBlockInContext(c, fn, "rb.cond");
    LLVMBasicBlockRef fbody  = LLVMAppendBasicBlockInContext(c, fn, "rb.body");
    LLVMBasicBlockRef fpcond = LLVMAppendBasicBlockInContext(c, fn, "rb.pcond");
    LLVMBasicBlockRef fput   = LLVMAppendBasicBlockInContext(c, fn, "rb.put");
    LLVMBasicBlockRef fdup   = LLVMAppendBasicBlockInContext(c, fn, "rb.dup");
    LLVMBasicBlockRef fpnext = LLVMAppendBasicBlockInContext(c, fn, "rb.pnext");
    LLVMBasicBlockRef fnext  = LLVMAppendBasicBlockInContext(c, fn, "rb.next");
    LLVMBasicBlockRef probe  = LLVMAppendBasicBlockInContext(c, fn, "probe");
    LLVMBasicBlockRef pcond  = LLVMAppendBasicBlockInContext(c, fn, "p.cond");
    LLVMBasicBlockRef pchk   = LLVMAppendBasicBlockInContext(c, fn, "p.chk");
    LLVMBasicBlockRef phit   = LLVMAppendBasicBlockInContext(c, fn, "p.hit");
    LLVMBasicBlockRef pnext  = LLVMAppendBasicBlockInContext(c, fn, "p.next");

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef draw = LLVMGetParam(fn, 0);
    LLVMValueRef key = LLVMGetParam(fn, 1);
    LLVMValueRef is_str = LLVMGetParam(fn, 2);
    LLVMValueRef i_a = LLVMBuildAlloca(b, i64, "i");
    LLVMValueRef h_a = LLVMBuildAlloca(b, i64, "h");
    LLVMValueRef cap_a = LLVMBuildAlloca(b, i64, "ncap");
    LLVMValueRef ix_a = LLVMBuildAlloca(b, i64ptr, "ixcur");
    LLVMValueRef mask_a = LLVMBuildAlloca(b, i64, "mask");
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
    LLVMValueRef ix = LLVMBuildLoad2(b, i64ptr, ixp, "ix");
    LLVMValueRef icap = LLVMBuildLoad2(b, i64, icapp, "icap");
    LLVMValueRef icnt = LLVMBuildLoad2(b, i64, icntp, "icnt");
    LLVMValueRef noidx = zan_icmp(b, LLVMIntEQ, ix, LLVMConstNull(i64ptr), "noidx");
    LLVMValueRef nocap = zan_icmp(b, LLVMIntSLE, icap, LLVMConstInt(i64, 0, 0), "nocap");
    LLVMValueRef stale = zan_icmp(b, LLVMIntNE, icnt, cnt, "stale");
    LLVMValueRef need = zan_or(b, zan_or(b, noidx, nocap, "o1"), stale, "need");
    LLVMBasicBlockRef live = LLVMAppendBasicBlockInContext(c, fn, "live");
    LLVMBasicBlockRef needc = LLVMAppendBasicBlockInContext(c, fn, "rb.need");
    LLVMBasicBlockRef grow = LLVMAppendBasicBlockInContext(c, fn, "rb.grow");
    LLVMBuildCondBr(b, empty, miss, live);
    LLVMPositionBuilderAtEnd(b, live);
    /* Add only bumps the count, so the index reads as stale on the next lookup.
     * Indexing just the appended keys keeps insertion amortized O(1): a full
     * rebuild per insert made insert-then-lookup loops quadratic in time and in
     * allocation (gigabytes for a dictionary with tens of thousands of keys). */
    LLVMValueRef fits = zan_icmp(b, LLVMIntSGE, icap,
        zan_mul(b, cnt, LLVMConstInt(i64, 4, 0), "want4"), "fits");
    LLVMValueRef grew = zan_icmp(b, LLVMIntSLT, icnt, cnt, "grew");
    LLVMValueRef kept = zan_icmp(b, LLVMIntSGT, icnt, LLVMConstInt(i64, 0, 0), "kept");
    LLVMValueRef append = zan_and(b, zan_and(b, zan_and(b,
        LLVMBuildNot(b, noidx, "haveidx"), fits, "a1"), grew, "a2"), kept, "append");
    LLVMBuildCondBr(b, need, needc, probe);
    LLVMPositionBuilderAtEnd(b, needc);
    LLVMBuildCondBr(b, append, grow, capc);

    /* incremental: index the entries appended since the last build */
    LLVMPositionBuilderAtEnd(b, grow);
    LLVMBuildStore(b, ix, ix_a);
    LLVMBuildStore(b, zan_sub(b, icap, LLVMConstInt(i64, 1, 0), "gmask"), mask_a);
    LLVMBuildStore(b, cnt, icntp);
    LLVMBuildStore(b, icnt, i_a);
    LLVMBuildBr(b, fcond);

    /* rebuild: capacity = next power of two >= 4 * count, at least 16 */
    LLVMPositionBuilderAtEnd(b, capc);
    LLVMBuildStore(b, LLVMConstInt(i64, 16, 0), cap_a);
    LLVMValueRef want = zan_mul(b, cnt, LLVMConstInt(i64, 4, 0), "want");
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
    /* The old table is dead once a bigger one is allocated: dropping it on the
     * floor leaked one table per rebuild. */
    LLVMTypeRef ixfree_ty = LLVMFunctionType(LLVMVoidTypeInContext(c),
        (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMValueRef ixfree = get_libc_fn(g, "free", ixfree_ty);
    LLVMValueRef oldix8 = LLVMBuildBitCast(b, ix, i8ptr, "oldix8");
    zan_call2(b, ixfree_ty, ixfree, &oldix8, 1, "");
    LLVMValueRef ncap = LLVMBuildLoad2(b, i64, cap_a, "ncap.v");
    LLVMValueRef nix8 = zan_call2(b, calloc_ty, calloc_fn,
        (LLVMValueRef[]){ ncap, LLVMConstInt(i64, 8, 0) }, 2, "nix8");
    LLVMValueRef nix = LLVMBuildBitCast(b, nix8, i64ptr, "nix");
    LLVMBuildStore(b, nix, ixp);
    LLVMBuildStore(b, ncap, icapp);
    LLVMBuildStore(b, cnt, icntp);
    LLVMBuildStore(b, nix, ix_a);
    LLVMBuildStore(b, zan_sub(b, ncap, LLVMConstInt(i64, 1, 0), "rmask"), mask_a);
    LLVMValueRef nixnull = zan_icmp(b, LLVMIntEQ, nix8, LLVMConstNull(i8ptr), "nixnull");
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), i_a);
    LLVMBuildCondBr(b, nixnull, miss, fcond);
    LLVMPositionBuilderAtEnd(b, fcond);
    LLVMValueRef fi = LLVMBuildLoad2(b, i64, i_a, "fi");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntSLT, fi, cnt, "fmore"), fbody, probe);
    LLVMPositionBuilderAtEnd(b, fbody);
    LLVMValueRef fmask = LLVMBuildLoad2(b, i64, mask_a, "fmask");
    LLVMValueRef rks = LLVMBuildLoad2(b, i8pp, kp, "rks");
    LLVMValueRef fi2 = LLVMBuildLoad2(b, i64, i_a, "fi2");
    LLVMValueRef fkey = LLVMBuildLoad2(b, i8ptr, LLVMBuildGEP2(b, i8ptr, rks, &fi2, 1, "fksl"), "fkey");
    LLVMValueRef fh = zan_call2(b, hash_ty, hashf, (LLVMValueRef[]){ fkey, is_str }, 2, "fh");
    LLVMBuildStore(b, zan_and(b, fh, fmask, "fh.m"), h_a);
    LLVMBuildBr(b, fpcond);
    LLVMPositionBuilderAtEnd(b, fpcond);
    LLVMValueRef fix = LLVMBuildLoad2(b, i64ptr, ix_a, "fix");
    LLVMValueRef fh2 = LLVMBuildLoad2(b, i64, h_a, "fh2");
    LLVMValueRef fslotp = LLVMBuildGEP2(b, i64, fix, &fh2, 1, "fslotp");
    LLVMValueRef fslot = LLVMBuildLoad2(b, i64, fslotp, "fslot");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, fslot, LLVMConstInt(i64, 0, 0), "ffree"),
                    fput, fdup);
    LLVMPositionBuilderAtEnd(b, fput);
    LLVMValueRef fi3 = LLVMBuildLoad2(b, i64, i_a, "fi3");
    LLVMBuildStore(b, zan_add(b, fi3, LLVMConstInt(i64, 1, 0), "fi3p1"),
                   LLVMBuildGEP2(b, i64, fix, &fh2, 1, "fslotp2"));
    LLVMBuildBr(b, fnext);
    LLVMPositionBuilderAtEnd(b, fdup);
    LLVMValueRef dks = LLVMBuildLoad2(b, i8pp, kp, "dks");
    LLVMValueRef fold = zan_sub(b, fslot, LLVMConstInt(i64, 1, 0), "fold");
    LLVMValueRef foldk = LLVMBuildLoad2(b, i8ptr, LLVMBuildGEP2(b, i8ptr, dks, &fold, 1, "foldkp"), "foldk");
    LLVMValueRef fsame = zan_call2(b, keq_ty, keq, (LLVMValueRef[]){ foldk, fkey, is_str }, 3, "fsame");
    LLVMBuildCondBr(b, fsame, fnext, fpnext);
    LLVMPositionBuilderAtEnd(b, fpnext);
    LLVMValueRef wmask = LLVMBuildLoad2(b, i64, mask_a, "wmask");
    LLVMBuildStore(b, zan_and(b,
        zan_add(b, fh2, LLVMConstInt(i64, 1, 0), "fh.inc"), wmask, "fh.wrap"), h_a);
    LLVMBuildBr(b, fpcond);
    LLVMPositionBuilderAtEnd(b, fnext);
    LLVMValueRef fi4 = LLVMBuildLoad2(b, i64, i_a, "fi4");
    LLVMBuildStore(b, zan_add(b, fi4, LLVMConstInt(i64, 1, 0), "fi.inc"), i_a);
    LLVMBuildBr(b, fcond);

    /* probe for the requested key */
    LLVMPositionBuilderAtEnd(b, probe);
    LLVMValueRef pix = LLVMBuildLoad2(b, i64ptr, ixp, "pix");
    LLVMValueRef pcap = LLVMBuildLoad2(b, i64, icapp, "pcap");
    LLVMValueRef pks = LLVMBuildLoad2(b, i8pp, kp, "pks");
    LLVMValueRef pmask = zan_sub(b, pcap, LLVMConstInt(i64, 1, 0), "pmask");
    LLVMValueRef ph = zan_call2(b, hash_ty, hashf, (LLVMValueRef[]){ key, is_str }, 2, "ph");
    LLVMBuildStore(b, zan_and(b, ph, pmask, "ph.m"), h_a);
    LLVMBuildBr(b, pcond);
    LLVMPositionBuilderAtEnd(b, pcond);
    LLVMValueRef ph2 = LLVMBuildLoad2(b, i64, h_a, "ph2");
    LLVMValueRef pslot = LLVMBuildLoad2(b, i64, LLVMBuildGEP2(b, i64, pix, &ph2, 1, "pslotp"), "pslot");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, pslot, LLVMConstInt(i64, 0, 0), "pfree"),
                    miss, pchk);
    LLVMPositionBuilderAtEnd(b, pchk);
    LLVMValueRef pidx = zan_sub(b, pslot, LLVMConstInt(i64, 1, 0), "pidx");
    LLVMValueRef pk = LLVMBuildLoad2(b, i8ptr, LLVMBuildGEP2(b, i8ptr, pks, &pidx, 1, "pkp"), "pk");
    LLVMBuildCondBr(b, zan_call2(b, keq_ty, keq, (LLVMValueRef[]){ pk, key, is_str }, 3, "peq"),
                    phit, pnext);
    LLVMPositionBuilderAtEnd(b, phit);
    LLVMBuildRet(b, pidx);
    LLVMPositionBuilderAtEnd(b, pnext);
    LLVMBuildStore(b, zan_and(b,
        zan_add(b, ph2, LLVMConstInt(i64, 1, 0), "ph.inc"), pmask, "ph.wrap"), h_a);
    LLVMBuildBr(b, pcond);
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}


/* void __zan_dict_set(i8* draw, i8* key, i64 val, i64 is_str): upsert for the
 * Dict indexer. Replaces the value of an existing key, else appends the pair,
 * growing the parallel keys/values buffers when full. */
static LLVMValueRef get_dict_set_fn(zan_irgen_t *g) {
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
    /* append: grow when full */
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

/* Handler-stack and unwind-stack storage: one independent state block per
 * thread.
 *
 * Handler depth is *runtime* nesting -- one slot per try currently entered, so
 * a recursive function with a try/catch has no static bound. The previous
 * fixed 16-slot global array did no bound check at all: arming the 17th
 * handler wrote a jmp_buf past the end (access violation, see A20), and
 * growing a single buffer with realloc moves armed jmp_bufs out from under
 * whoever is inside a try.
 *
 * Sharing one stack process-wide is wrong on its own: `System.Threading`
 * programs run Zan code on real OS threads (zan_thread_start), each with its
 * own call stack, so a throw on thread B would longjmp into thread A's frames
 * (A21). The state therefore lives in a per-thread block reached through
 * __zan_eh_state(): a thread-id keyed open-addressed table, since native TLS
 * is unusable in emitted code on the bundled MinGW/lld toolchain (the first
 * access to a thread_local global faults on _tls_index) and adding a runtime
 * object would have to be cross-built for every supported target.
 *
 * Inside a block both stacks use a chunk table: a fixed array of chunk
 * pointers, each chunk calloc'd on first use and never freed, moved or
 * reallocated, so an armed jmp_buf's address is stable for the life of the
 * thread. A block is only ever touched by its owning thread, so no atomics
 * are needed past claiming its table slot. Handler slots carry their jmp_buf
 * (1024 bytes, covering every supported target's) plus the unwind-stack depth
 * the handler was armed at, which a throw uses to release the skipped frames'
 * locals while they are still alive (see emit_eh_unwind_to_handler).
 * The numeric constants (ZAN_EH_*) are in ../common/zan_abi.h. */

/* Fields of the per-thread state block. */
enum {
    EH_F_TOP = 0,      /* i32: handler stack top, -1 = no handler armed */
    EH_F_TMPS_TOP,     /* i32: unwind stack depth */
    EH_F_EXC_OWNED,    /* i32: in-flight exception holds a +1 reference */
    EH_F_PAD,          /* i32: keeps the pointer fields naturally aligned */
    EH_F_EXC,          /* i8*: in-flight exception object */
    EH_F_EXC_TID,      /* i8*: its class type descriptor (null = string) */
    EH_F_BUFS,         /* [ZAN_EH_CHUNKS x i8*]: handler slot chunks */
    EH_F_TMPS,         /* [ZAN_EH_CHUNKS x i8*]: unwind stack chunks */
    EH_F_COUNT
};

static LLVMValueRef eh_add_global(zan_irgen_t *g, LLVMTypeRef ty,
                                 const char *name, LLVMValueRef init) {
    LLVMValueRef v = LLVMAddGlobal(g->mod, ty, name);
    LLVMSetLinkage(v, LLVMInternalLinkage);
    LLVMSetInitializer(v, init);
    return v;
}

/* The calling thread's cached EH state pointer: `__zan_eh_state` otherwise
 * repeats pthread_self plus an open-addressed probe of the thread table on
 * every use, and a keep-alive HTTP request makes ~36 of them (each temporary
 * pushed for exception safety asks twice). A thread-local slot answers the
 * steady-state call in two instructions; the table stays the source of truth,
 * and `__zan_eh_release` clears the slot when it drops the block.
 *
 * The TLS model is picked rather than left to the default: an executable or a
 * load-time shared object can use initial-exec (a GOT-relative load), while a
 * dlopen'able library must stay general-dynamic so it never runs out of the
 * static TLS surplus.
 *
 * Returns NULL where the cache is unavailable, and callers then go straight to
 * the table:
 *   - WASM has one thread and no TLS, so the table probe is already the whole
 *     cost of a lookup;
 *   - on Windows the bundled linker (GNU ld 2.36.1, i386pep) emits a base
 *     relocation over the section-relative displacement of a COFF TLS access
 *     (`mov 0x8(%rax),%rax` after the %gs:0x58 / _tls_index pair), so ASLR
 *     rewrites the offset at load time and the very first lookup reads a wild
 *     address. Emitting no thread-local at all is the only way to keep both
 *     the relocation section and a working binary. */
static LLVMValueRef get_eh_self_slot(zan_irgen_t *g) {
    if (g->target_is_windows || strstr(g->target_triple, "wasm")) return NULL;
    LLVMValueRef v = LLVMGetNamedGlobal(g->mod, "__zan_eh_self");
    if (v) return v;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    v = LLVMAddGlobal(g->mod, i8ptr, "__zan_eh_self");
    LLVMSetLinkage(v, LLVMInternalLinkage);
    LLVMSetInitializer(v, LLVMConstNull(i8ptr));
    LLVMSetThreadLocal(v, 1);
    LLVMSetThreadLocalMode(v, g->emit_shared ? LLVMGeneralDynamicTLSModel
                                             : LLVMInitialExecTLSModel);
    return v;
}

/* The layout of a thread's EH state. Private to emitted code (nothing outside
 * this module allocates or reads a block), so it needs no fixed ABI. */
static LLVMTypeRef get_eh_state_ty(zan_irgen_t *g) {
    if (g->eh_state_ty) return g->eh_state_ty;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fields[EH_F_COUNT];
    fields[EH_F_TOP] = i32t;
    fields[EH_F_TMPS_TOP] = i32t;
    fields[EH_F_EXC_OWNED] = i32t;
    fields[EH_F_PAD] = i32t;
    fields[EH_F_EXC] = i8ptr;
    fields[EH_F_EXC_TID] = i8ptr;
    fields[EH_F_BUFS] = LLVMArrayType(i8ptr, ZAN_EH_CHUNKS);
    fields[EH_F_TMPS] = LLVMArrayType(i8ptr, ZAN_EH_CHUNKS);
    LLVMTypeRef ty = LLVMStructCreateNamed(g->ctx, "zan.eh.state");
    LLVMStructSetBody(ty, fields, EH_F_COUNT, 0);
    g->eh_state_ty = ty;
    return ty;
}

/* i64 id of the calling thread, non-zero on every supported target.
 * GetCurrentThreadId (kernel32) on Windows, pthread_self elsewhere -- both are
 * already in every produced program's link line. wasm32 has no threads. */
static LLVMValueRef emit_eh_thread_id(zan_irgen_t *g) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (strstr(g->target_triple, "wasm"))
        return LLVMConstInt(i64t, 1, 0);
    if (g->target_is_windows) {
        LLVMTypeRef ty = LLVMFunctionType(i32t, NULL, 0, 0);
        LLVMValueRef id = zan_call2(g->builder, ty,
            get_libc_fn(g, "GetCurrentThreadId", ty), NULL, 0, "tid32");
        return LLVMBuildZExt(g->builder, id, i64t, "tid");
    }
    /* pthread_t is a pointer on macOS and an unsigned long on Linux; both are
     * returned in a register, so a pointer-typed declaration is ABI-correct on
     * 32- and 64-bit alike. */
    LLVMTypeRef ty = LLVMFunctionType(i8ptr, NULL, 0, 0);
    LLVMValueRef self = zan_call2(g->builder, ty,
        get_libc_fn(g, "pthread_self", ty), NULL, 0, "self");
    return LLVMBuildPtrToInt(g->builder, self, i64t, "tid");
}

/* Table globals. The arrays start NULL and the first claimer installs a
 * ZAN_EH_THREADS-entry pair, so a program that never throws pays nothing; the
 * cap global lets __zan_eh_state grow the table when the last free slot goes.
 * Keys and states move together and are only swapped under the spinlock, so
 * readers either take the lock (slow path, release) or hold it (state). */
static LLVMValueRef get_eh_tab_keys(zan_irgen_t *g) {
    LLVMValueRef v = LLVMGetNamedGlobal(g->mod, "__zan_eh_keys");
    if (v) return v;
    LLVMTypeRef i64ptr = LLVMPointerType(LLVMInt64TypeInContext(g->ctx), 0);
    return eh_add_global(g, i64ptr, "__zan_eh_keys", LLVMConstNull(i64ptr));
}

static LLVMValueRef get_eh_tab_states(zan_irgen_t *g) {
    LLVMValueRef v = LLVMGetNamedGlobal(g->mod, "__zan_eh_states");
    if (v) return v;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ty = LLVMPointerType(i8ptr, 0);
    return eh_add_global(g, ty, "__zan_eh_states", LLVMConstNull(ty));
}

static LLVMValueRef get_eh_tab_cap(zan_irgen_t *g) {
    LLVMValueRef v = LLVMGetNamedGlobal(g->mod, "__zan_eh_cap");
    if (v) return v;
    return eh_add_global(g, LLVMInt64TypeInContext(g->ctx), "__zan_eh_cap",
                         LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0));
}

/* Spinlock guarding the table arrays and their capacity. Held across a whole
 * lookup-or-claim-or-grow, released before return; the per-thread cache keeps
 * every later use of the state block lock-free. Fits in one word, so a
 * compare-exchange on an i64 global is the whole implementation. */
static LLVMValueRef get_eh_tab_lock(zan_irgen_t *g) {
    LLVMValueRef v = LLVMGetNamedGlobal(g->mod, "__zan_eh_tab_lock");
    if (v) return v;
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef ty = LLVMArrayType(i64t, 8);
    return eh_add_global(g, ty, "__zan_eh_tab_lock", LLVMConstNull(ty));
}

/* Acquire the table mutex: one plain call to AcquireSRWLockExclusive /
 * pthread_mutex_lock into the zero-initialized storage. */
static void emit_eh_tab_lock_acquire(zan_irgen_t *g, LLVMValueRef lock_gv) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef voidt = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef ty = LLVMFunctionType(voidt, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    const char *name = g->target_is_windows
        ? "AcquireSRWLockExclusive" : "pthread_mutex_lock";
    LLVMValueRef fn = get_libc_fn(g, name, ty);
    LLVMValueRef p = LLVMBuildBitCast(g->builder, lock_gv,
        LLVMPointerType(i8ptr, 0), "lock.p");
    zan_call2(g->builder, ty, fn, &p, 1, "");
}

/* Release the table mutex. */
static void emit_eh_tab_lock_release(zan_irgen_t *g, LLVMValueRef lock_gv) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef voidt = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef ty = LLVMFunctionType(voidt, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    const char *name = g->target_is_windows
        ? "ReleaseSRWLockExclusive" : "pthread_mutex_unlock";
    LLVMValueRef fn = get_libc_fn(g, name, ty);
    LLVMValueRef p = LLVMBuildBitCast(g->builder, lock_gv,
        LLVMPointerType(i8ptr, 0), "lock.p");
    zan_call2(g->builder, ty, fn, &p, 1, "");
}

/* Grow the thread table: calloc a doubled keys/states pair, rehash every live
 * key into the new arrays (tombstones drop out, which is the point of doing
 * this here), then install. Called with the spinlock held; the fresh arrays
 * are unreachable until the pointer stores, so plain stores are enough.
 * Growers set the lock to 2 first so a spinner can see the delay is bounded
 * work, not a deadlock. */
static void emit_eh_oom_abort(zan_irgen_t *g, const char *msg);
static void emit_eh_tab_grow(zan_irgen_t *g, LLVMTypeRef state_ty,
                             LLVMValueRef calloc_fn,
                             LLVMTypeRef free_ty, LLVMValueRef free_fn) {
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64ptr = LLVMPointerType(i64t, 0);
    LLVMTypeRef states_ptr_ty = LLVMPointerType(i8ptr, 0);

    LLVMValueRef keys_gv = get_eh_tab_keys(g);
    LLVMValueRef states_gv = get_eh_tab_states(g);
    LLVMValueRef cap_gv = get_eh_tab_cap(g);
    LLVMValueRef lock_gv = get_eh_tab_lock(g);

    LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
    LLVMValueRef fn = LLVMGetBasicBlockParent(cur);
    /* The caller's current block (under the lock) falls into the grow. */
    LLVMBasicBlockRef head  = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.head");
    LLVMBasicBlockRef walk  = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.walk");
    LLVMBasicBlockRef body  = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.body");
    LLVMBasicBlockRef place = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.place");
    LLVMBasicBlockRef npos  = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.npos");
    LLVMBasicBlockRef scan  = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.scan");
    LLVMBasicBlockRef full  = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.full");
    LLVMBasicBlockRef bump  = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.bump");
    LLVMBasicBlockRef fin   = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.fin");
    LLVMBasicBlockRef done  = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.done");

    LLVMBasicBlockRef src = cur;
    LLVMPositionBuilderAtEnd(g->builder, cur);
    LLVMBuildBr(g->builder, head);
    LLVMPositionBuilderAtEnd(g->builder, head);

    LLVMValueRef oldcap = LLVMBuildLoad2(g->builder, i64t, cap_gv, "gw.oldcap");
    LLVMValueRef newcap = LLVMBuildSelect(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, oldcap, LLVMConstInt(i64t, 0, 0), "zero"),
        LLVMConstInt(i64t, ZAN_EH_THREADS, 0),
        LLVMBuildMul(g->builder, oldcap, LLVMConstInt(i64t, 2, 0), "dbl"),
        "gw.newcap");
    LLVMValueRef kb = zan_call2(g->builder,
        LLVMFunctionType(i64ptr, (LLVMTypeRef[]){ i64t, i64t }, 2, 0),
        calloc_fn, (LLVMValueRef[]){ newcap,
            LLVMConstInt(i64t, 8, 0) }, 2, "gw.kb");
    LLVMValueRef sb = zan_call2(g->builder,
        LLVMFunctionType(states_ptr_ty, (LLVMTypeRef[]){ i64t, i64t }, 2, 0),
        calloc_fn, (LLVMValueRef[]){ newcap, LLVMConstInt(i64t, 8, 0) }, 2,
        "gw.sb");
    LLVMBasicBlockRef oom = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.oom");
    LLVMBuildCondBr(g->builder,
        LLVMBuildOr(g->builder,
            LLVMBuildIsNull(g->builder, kb, "kn"),
            LLVMBuildIsNull(g->builder, sb, "sn"), "oom"),
        oom, walk);
    LLVMPositionBuilderAtEnd(g->builder, oom);
    emit_eh_oom_abort(g,
        "zan: out of memory growing the exception-handling table\n");

    LLVMPositionBuilderAtEnd(g->builder, walk);
    LLVMValueRef j_slot = LLVMBuildAlloca(g->builder, i64t, "gw.j");
    LLVMBuildStore(g->builder, LLVMConstInt(i64t, 0, 0), j_slot);
    LLVMBasicBlockRef head2 = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.head2");
    LLVMBuildBr(g->builder, head2);

    LLVMPositionBuilderAtEnd(g->builder, head2);
    LLVMValueRef j = LLVMBuildLoad2(g->builder, i64t, j_slot, "gw.jv");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntULT, j, oldcap, "j.ok"), body, fin);

    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef oldkeys = LLVMBuildLoad2(g->builder, i64ptr, keys_gv, "gw.ok");
    LLVMValueRef kp = LLVMBuildGEP2(g->builder, i64ptr, oldkeys, &j, 1, "gw.kp");
    LLVMValueRef k = LLVMBuildLoad2(g->builder, i64t, kp, "gw.k");
    LLVMValueRef live = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntNE, k, LLVMConstInt(i64t, 0, 0), "nz"),
        LLVMBuildICmp(g->builder, LLVMIntNE, k,
            LLVMConstInt(i64t, ZAN_EH_TOMBSTONE, 0), "nt"), "live");
    LLVMBuildCondBr(g->builder, live, place, bump);

    LLVMPositionBuilderAtEnd(g->builder, place);
    /* Same mix as the reader: key ^ (key >> 32), masked to the new width. */
    LLVMValueRef h = zan_and(g->builder,
        LLVMBuildXor(g->builder, k,
            LLVMBuildLShr(g->builder, k, LLVMConstInt(i64t, 32, 0), "hi"),
            "mix"),
        LLVMBuildSub(g->builder, newcap, LLVMConstInt(i64t, 1, 0), "m1"),
        "gw.h");
    LLVMValueRef p_slot = LLVMBuildAlloca(g->builder, i64t, "gw.p");
    LLVMBuildStore(g->builder, h, p_slot);
    LLVMBuildBr(g->builder, scan);

    LLVMPositionBuilderAtEnd(g->builder, scan);
    LLVMValueRef p = LLVMBuildLoad2(g->builder, i64t, p_slot, "gw.pv");
    LLVMValueRef nkp = LLVMBuildGEP2(g->builder, i64ptr, kb, &p, 1, "gw.nkp");
    LLVMValueRef nk = LLVMBuildLoad2(g->builder, i64t, nkp, "gw.nk");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, nk, LLVMConstInt(i64t, 0, 0), "free"),
        full, npos);

    LLVMPositionBuilderAtEnd(g->builder, full);
    /* Zeroed fresh array: this is a plain store. */
    LLVMBuildStore(g->builder, k, nkp);
    /* sb is ptr-to-i8*, so GEP2 with that element type scales p by 8 itself;
     * multiplying here too wrote new_states[p] at sb + p*64 -- out of bounds
     * on the first real rehash, and the >1024-thread segfault. */
    LLVMValueRef nsp = LLVMBuildGEP2(g->builder, states_ptr_ty, sb, &p, 1,
                                     "gw.nsp");
    LLVMValueRef osp = LLVMBuildGEP2(g->builder, states_ptr_ty,
        LLVMBuildLoad2(g->builder, states_ptr_ty, states_gv, "gw.os"), &j, 1,
        "gw.osp");
    LLVMValueRef stp = LLVMBuildLoad2(g->builder, states_ptr_ty, osp, "gw.stp");
    LLVMBuildStore(g->builder, stp, nsp);
    LLVMBuildBr(g->builder, bump);

    LLVMPositionBuilderAtEnd(g->builder, npos);
    /* Wrap the probe back to slot 0 at the array end: the start slot is masked
     * and the reader probes mask every step, so without this mask a collision
     * chain that crosses the last slot walks off the allocation and writes the
     * key/state pair into unowned heap -- first reachable once more than
     * ZAN_EH_THREADS threads claim states, i.e. at the first real rehash. */
    LLVMValueRef p2 = zan_and(g->builder,
        zan_add(g->builder, p, LLVMConstInt(i64t, 1, 0), "p.next"),
        LLVMBuildSub(g->builder, newcap, LLVMConstInt(i64t, 1, 0), "gw.m1b"),
        "p.wrap");
    LLVMBuildStore(g->builder, p2, p_slot);
    LLVMBuildBr(g->builder, scan);

    LLVMPositionBuilderAtEnd(g->builder, bump);
    LLVMBuildStore(g->builder,
        zan_add(g->builder, j, LLVMConstInt(i64t, 1, 0), "j.next"), j_slot);
    LLVMBuildBr(g->builder, head2);

    LLVMPositionBuilderAtEnd(g->builder, fin);
    /* Install. A stale reader cannot exist: only lock holders read the arrays
     * (slow path) or the per-thread cache (state), and this thread holds the
     * lock. Old arrays are freed after the pointer swap. */
    LLVMValueRef ok = LLVMBuildLoad2(g->builder, i64ptr, keys_gv, "gw.oldk2");
    LLVMValueRef os = LLVMBuildLoad2(g->builder, states_ptr_ty, states_gv, "gw.olds");
    /* Plain stores: the mutex that guards them also provides the
     * visibility ordering between the installer and later claimers. */
    LLVMBuildStore(g->builder, kb, keys_gv);
    LLVMBuildStore(g->builder,
        LLVMBuildBitCast(g->builder, sb, states_ptr_ty, "sb.t"), states_gv);
    LLVMBuildStore(g->builder, newcap, cap_gv);
    zan_call2(g->builder, free_ty, free_fn, (LLVMValueRef[]){ ok }, 1, "");
    zan_call2(g->builder, free_ty, free_fn,
        (LLVMValueRef[]){ LLVMBuildBitCast(g->builder, os, i8ptr, "os.c") }, 1, "");
    LLVMBuildBr(g->builder, done);
    LLVMPositionBuilderAtEnd(g->builder, done);
    /* The builder is left at gw.done so the caller's next emitted instruction
     * lands there; the caller re-positions as it needs (it re-probes). */
    (void)src;
}

static LLVMValueRef get_eh_state_fn(zan_irgen_t *g);
static LLVMValueRef get_eh_state_fast_fn(zan_irgen_t *g);
/* Remembered in the irgen context so zan_call2's wasm32 invoke conversion can
 * keep the state accessor a plain call: it never raises (pure runtime read of
 * the thread slot), and as an invoke it would split whatever block the EH
 * helpers re-enter (the function entry) mid-emission, stranding the field
 * GEPs that follow against the new terminator. */
static void wasm_note_state_fn(zan_irgen_t *g, LLVMValueRef state_fn);

/* Position the builder in the current function's entry block, where the EH
 * state pointer and its field addresses are materialized (see irgen.h). */
static LLVMBasicBlockRef eh_enter_entry_block(zan_irgen_t *g) {
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry =
        LLVMGetEntryBasicBlock(LLVMGetBasicBlockParent(cur));
    LLVMValueRef term = LLVMGetBasicBlockTerminator(entry);
    if (term && entry != cur) LLVMPositionBuilderBefore(g->builder, term);
    else LLVMPositionBuilderAtEnd(g->builder, entry);
    return cur;
}

/* The calling thread's state block, typed. Emitted once per function; the
 * pointer is stable for the thread's lifetime, so every try, throw and catch
 * in the function shares it. */
static LLVMValueRef emit_eh_state(zan_irgen_t *g) {
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
    LLVMValueRef fn = cur ? LLVMGetBasicBlockParent(cur) : NULL;
    if (fn && fn == g->eh_state_owner && g->eh_state_cached)
        return g->eh_state_cached;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef state_fn = get_eh_state_fast_fn(g);
    wasm_note_state_fn(g, state_fn);
    eh_enter_entry_block(g);
    LLVMValueRef p = zan_call2(g->builder, LLVMFunctionType(i8ptr, NULL, 0, 0),
        state_fn, NULL, 0, "eh.st");
    LLVMValueRef typed = LLVMBuildBitCast(g->builder, p,
        LLVMPointerType(get_eh_state_ty(g), 0), "eh.stp");
    LLVMPositionBuilderAtEnd(g->builder, cur);
    g->eh_state_owner = fn;
    g->eh_state_cached = typed;
    for (int i = 0; i < EH_F_COUNT; i++) g->eh_state_fields[i] = NULL;
    return typed;
}

static void add_enum_attr(zan_irgen_t *g, LLVMValueRef fn, LLVMValueRef call,
                          const char *name);

/* i8* __zan_eh_state_fast(): the thread-local cache read, with the table
 * lookup left out of line. Every push and pop of an owned temporary asks for
 * the state block, and an out-of-line call for what is a GOT-relative load
 * plus a null test showed up as ~6% of a keep-alive request's user time.
 * alwaysinline rather than a hint: the wrapper only pays off when the fast
 * path lands in the caller, and the optimizer's own size-based hint stops at
 * four blocks. Falls back to the table accessor where there is no cache
 * (see get_eh_self_slot). */
static LLVMValueRef get_eh_state_fast_fn(zan_irgen_t *g) {
    LLVMValueRef slow = get_eh_state_fn(g);
    LLVMValueRef self_slot = get_eh_self_slot(g);
    if (!self_slot) return slow;
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_state_fast");
    if (fn) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, NULL, 0, 0);
    fn = LLVMAddFunction(g->mod, "__zan_eh_state_fast", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    add_enum_attr(g, fn, NULL, "alwaysinline");
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef slowbb = LLVMAppendBasicBlockInContext(g->ctx, fn, "slow");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef cached = LLVMBuildLoad2(g->builder, i8ptr, self_slot, "st.c");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, cached, LLVMConstNull(i8ptr), "st.miss"),
        slowbb, done);
    LLVMPositionBuilderAtEnd(g->builder, slowbb);
    LLVMValueRef fresh = zan_call2(g->builder, fnty, slow, NULL, 0, "st.slow");
    LLVMBuildBr(g->builder, done);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMValueRef phi = LLVMBuildPhi(g->builder, i8ptr, "st.r");
    LLVMAddIncoming(phi, (LLVMValueRef[]){ cached, fresh },
        (LLVMBasicBlockRef[]){ entry, slowbb }, 2);
    LLVMBuildRet(g->builder, phi);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

static LLVMValueRef emit_eh_field_ptr(zan_irgen_t *g, unsigned field,
                                      const char *name) {
    LLVMValueRef st = emit_eh_state(g);
    if (g->eh_state_fields[field]) return g->eh_state_fields[field];
    LLVMBasicBlockRef cur = eh_enter_entry_block(g);
    LLVMValueRef p = LLVMBuildStructGEP2(g->builder, get_eh_state_ty(g), st,
        field, name);
    LLVMPositionBuilderAtEnd(g->builder, cur);
    g->eh_state_fields[field] = p;
    return p;
}

/* Exception-handling state pointers for the calling thread: the handler
 * stack's top index (-1 = empty), the handler chunk table and the in-flight
 * exception object. Index handler slots with emit_eh_buf_ptr /
 * emit_eh_mark_ptr, never with a direct GEP. */
static void get_eh_globals(zan_irgen_t *g, LLVMValueRef *top,
                           LLVMValueRef *bufs, LLVMValueRef *exc) {
    *top = emit_eh_field_ptr(g, EH_F_TOP, "eh.topp");
    *exc = emit_eh_field_ptr(g, EH_F_EXC, "eh.excp");
    if (bufs) *bufs = emit_eh_field_ptr(g, EH_F_BUFS, "eh.bufsp");
}

/* Aborts with a message: reached only when a stack cannot be extended. */
static void emit_eh_oom_abort(zan_irgen_t *g, const char *msg) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef pty = LLVMFunctionType(i32t, &i8ptr, 1, 1);
    /* interned: only a handful of texts exist but each was a fresh global;
     * the intern table also dedups across the multiple EH chunk helpers. */
    LLVMValueRef fmt = zan_irgen_intern_string(g, msg);
    zan_call2(g->builder, pty, get_libc_fn(g, "printf", pty), &fmt, 1, "");
    LLVMTypeRef ety = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i32t, 1, 0);
    zan_call2(g->builder, ety, get_libc_fn(g, "exit", ety),
        (LLVMValueRef[]){ LLVMConstInt(i32t, 1, 0) }, 1, "");
    LLVMBuildUnreachable(g->builder);
}

/* void __zan_eh_release(): drop the calling thread's EH state block.
 *
 * Blocks used to be kept forever, so every OS thread that ever entered a try
 * consumed a table slot for the life of the process and running out was fatal
 * ("too many live threads..."). A program holding 1200 threads that each throw
 * once aborted, and a long-lived one leaked a block plus its chunks per thread
 * even when ids were recycled. Releasing marks the slot with a tombstone --
 * claimable again, still transparent to a probe -- and frees the block and
 * every chunk it brought into existence.
 *
 * Called from the thread trampoline once the body has returned, and exported
 * so a foreign thread (an X11 / SDL / Cocoa callback that ran Zan code) can
 * detach itself via zan_thread_detach. Calling it from a thread that never
 * used the runtime, or twice, is a no-op. */
static void emit_eh_release_fn(zan_irgen_t *g) {
    if (LLVMGetNamedFunction(g->mod, "__zan_eh_release")) return;
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64ptr = LLVMPointerType(i64t, 0);
    LLVMTypeRef states_ptr_ty = LLVMPointerType(i8ptr, 0);
    LLVMTypeRef voidt = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef state_ty = get_eh_state_ty(g);
    LLVMTypeRef tab_ty = LLVMArrayType(i8ptr, ZAN_EH_CHUNKS);
    LLVMTypeRef free_ty = LLVMFunctionType(voidt, &i8ptr, 1, 0);
    LLVMValueRef free_fn = get_libc_fn(g, "free", free_ty);
    LLVMValueRef keys_gv = get_eh_tab_keys(g);
    LLVMValueRef states_gv = get_eh_tab_states(g);
    LLVMValueRef cap_gv = get_eh_tab_cap(g);
    LLVMValueRef lock_gv = get_eh_tab_lock(g);

    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_eh_release",
        LLVMFunctionType(voidt, NULL, 0, 0));
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef probe = LLVMAppendBasicBlockInContext(g->ctx, fn, "probe");
    LLVMBasicBlockRef found = LLVMAppendBasicBlockInContext(g->ctx, fn, "found");
    LLVMBasicBlockRef live  = LLVMAppendBasicBlockInContext(g->ctx, fn, "live");
    LLVMBasicBlockRef cloop = LLVMAppendBasicBlockInContext(g->ctx, fn, "cloop");
    LLVMBasicBlockRef cbody = LLVMAppendBasicBlockInContext(g->ctx, fn, "cbody");
    LLVMBasicBlockRef drop  = LLVMAppendBasicBlockInContext(g->ctx, fn, "drop");
    LLVMBasicBlockRef next  = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
    LLVMBasicBlockRef done  = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef key = zan_add(g->builder, emit_eh_thread_id(g),
        LLVMConstInt(i64t, 1, 0), "key");
    LLVMValueRef i_slot = LLVMBuildAlloca(g->builder, i64t, "i");
    LLVMValueRef c_slot = LLVMBuildAlloca(g->builder, i64t, "c");
    LLVMValueRef sp_slot = LLVMBuildAlloca(g->builder, LLVMPointerType(i8ptr, 0), "spp");
    LLVMValueRef st_slot = LLVMBuildAlloca(g->builder, i8ptr, "stp");
    LLVMValueRef kp_slot = LLVMBuildAlloca(g->builder, LLVMPointerType(i64t, 0), "kpp");
    /* The lock stays held across the chunk-freeing walk: it is bounded (64
     * chunk slots) and releasing the block of a live thread's id must not race
     * with that thread claiming it back. */
    emit_eh_tab_lock_acquire(g, lock_gv);
    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64t, cap_gv, "st.cap");
    LLVMValueRef capm1 = LLVMBuildSub(g->builder, cap,
        LLVMConstInt(i64t, 1, 0), "st.capm1");
    LLVMValueRef h = zan_and(g->builder,
        LLVMBuildXor(g->builder, key,
            LLVMBuildLShr(g->builder, key, LLVMConstInt(i64t, 32, 0), "key.hi"),
            "key.mix"),
        capm1, "h");
    LLVMBuildStore(g->builder, LLVMConstInt(i64t, 0, 0), i_slot);
    LLVMValueRef keys = LLVMBuildLoad2(g->builder, i64ptr, keys_gv, "st.keys");
    LLVMValueRef states = LLVMBuildLoad2(g->builder, states_ptr_ty, states_gv,
                                         "st.states");
    LLVMBuildBr(g->builder, probe);

    LLVMPositionBuilderAtEnd(g->builder, probe);
    LLVMValueRef zero64 = LLVMConstInt(i64t, 0, 0);
    LLVMValueRef i = LLVMBuildLoad2(g->builder, i64t, i_slot, "i.v");
    LLVMValueRef pos = zan_and(g->builder, zan_add(g->builder, h, i, "pos.raw"),
        capm1, "pos");
    LLVMValueRef kp = LLVMBuildGEP2(g->builder, i64ptr, keys, &pos, 1, "kp");
    LLVMValueRef sp = LLVMBuildGEP2(g->builder, states_ptr_ty, states, &pos, 1, "sp");
    LLVMBuildStore(g->builder, kp, kp_slot);
    LLVMBuildStore(g->builder, sp, sp_slot);
    LLVMValueRef k = LLVMBuildLoad2(g->builder, i64t, kp, "k");
    LLVMSetAlignment(k, 8);
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, k, key, "is.mine"), found, next);

    /* Not this thread's slot. An empty one ends the search: the thread never
     * claimed a block, so there is nothing to release. */
    LLVMPositionBuilderAtEnd(g->builder, next);
    LLVMValueRef k2 = LLVMBuildLoad2(g->builder, i64t,
        LLVMBuildLoad2(g->builder, LLVMPointerType(i64t, 0), kp_slot, "kp.v"),
        "k2");
    LLVMValueRef ni = zan_add(g->builder,
        LLVMBuildLoad2(g->builder, i64t, i_slot, "i.v2"),
        LLVMConstInt(i64t, 1, 0), "i.next");
    LLVMBuildStore(g->builder, ni, i_slot);
    LLVMBuildCondBr(g->builder,
        LLVMBuildOr(g->builder,
            zan_icmp(g->builder, LLVMIntEQ, k2, zero64, "is.empty"),
            zan_icmp(g->builder, LLVMIntUGE, ni, cap, "wrapped"), "stop"),
        done, probe);

    LLVMPositionBuilderAtEnd(g->builder, found);
    LLVMValueRef st = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), sp_slot, "sp.v"),
        "st");
    LLVMBuildStore(g->builder, st, st_slot);
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, st, LLVMConstNull(i8ptr), "no.state"),
        drop, live);

    LLVMPositionBuilderAtEnd(g->builder, live);
    LLVMBuildStore(g->builder, zero64, c_slot);
    LLVMBuildBr(g->builder, cloop);

    /* Both chunk tables are walked together: a chunk is calloc'd on first use
     * and is this thread's alone, so nothing else can be looking at one. */
    LLVMPositionBuilderAtEnd(g->builder, cloop);
    LLVMValueRef c = LLVMBuildLoad2(g->builder, i64t, c_slot, "c.v");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntUGE, c,
            LLVMConstInt(i64t, ZAN_EH_CHUNKS, 0), "chunks.done"), drop, cbody);

    LLVMPositionBuilderAtEnd(g->builder, cbody);
    LLVMValueRef stp = LLVMBuildBitCast(g->builder,
        LLVMBuildLoad2(g->builder, i8ptr, st_slot, "st.v"),
        LLVMPointerType(state_ty, 0), "st.typed");
    unsigned tabs[2] = { EH_F_BUFS, EH_F_TMPS };
    for (int t = 0; t < 2; t++) {
        LLVMValueRef tab = LLVMBuildStructGEP2(g->builder, state_ty, stp,
            tabs[t], "tab");
        LLVMValueRef cp = LLVMBuildGEP2(g->builder, tab_ty, tab,
            (LLVMValueRef[]){ zero64, c }, 2, "cp");
        LLVMValueRef chunk = LLVMBuildLoad2(g->builder, i8ptr, cp, "chunk");
        zan_call2(g->builder, free_ty, free_fn, &chunk, 1, "");
    }
    LLVMBuildStore(g->builder,
        zan_add(g->builder, c, LLVMConstInt(i64t, 1, 0), "c.next"), c_slot);
    LLVMBuildBr(g->builder, cloop);

    LLVMPositionBuilderAtEnd(g->builder, drop);
    LLVMValueRef stv = LLVMBuildLoad2(g->builder, i8ptr, st_slot, "st.v2");
    zan_call2(g->builder, free_ty, free_fn, &stv, 1, "");
    /* The block is gone, so the caller's cached pointer must go with it: this
     * thread may enter a try again (a detached foreign thread that calls back
     * in) and would otherwise reuse freed memory. */
    LLVMValueRef eh_self = get_eh_self_slot(g);
    if (eh_self)
        LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), eh_self);
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr),
        LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), sp_slot, "sp.v2"));
    LLVMBuildStore(g->builder,
        LLVMConstInt(i64t, ZAN_EH_TOMBSTONE, 0),
        LLVMBuildLoad2(g->builder, LLVMPointerType(i64t, 0), kp_slot, "kp.v2"));
    LLVMBuildBr(g->builder, done);

    LLVMPositionBuilderAtEnd(g->builder, done);
    emit_eh_tab_lock_release(g, lock_gv);
    LLVMBuildRetVoid(g->builder);
}

/* i8* __zan_eh_state(): the calling thread's EH state block, created on that
/* i8* __zan_eh_state(): the calling thread's EH state block, created on that
 * thread's first use of it.
 *
 * Threads are found in an open-addressed table keyed on the OS thread id,
 * guarded by a spinlock (see get_eh_tab_lock). The lock is held across the
 * whole lookup-or-claim-or-grow: the table arrays can be swapped by a
 * concurrent rehash mid-probe otherwise. Claim is a plain store (the lock
 * serializes claimers); after that only the owner touches its slot, so the
 * state pointer itself needs no synchronization -- and the per-thread cache
 * keeps every later use lock-free.
 *
 * A full table grows instead of failing: ZAN_EH_THREADS (1024) is the initial
 * capacity only, doubled whenever a probe runs off the end. Blocks are never
 * freed while claimed: a thread id recycled by the OS reuses the block of the
 * thread that had it, which is correct because a thread that exits with an
 * exception in flight terminates the program. __zan_eh_release frees the
 * block and tombstones the slot. */
static LLVMValueRef get_eh_state_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_state");
    if (fn) return fn;
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8t, 0);
    LLVMTypeRef state_ty = get_eh_state_ty(g);
    LLVMTypeRef i64ptr = LLVMPointerType(i64t, 0);
    LLVMTypeRef states_ptr_ty = LLVMPointerType(i8ptr, 0);
    LLVMValueRef keys_gv = get_eh_tab_keys(g);
    LLVMValueRef states_gv = get_eh_tab_states(g);
    LLVMValueRef cap_gv = get_eh_tab_cap(g);
    LLVMValueRef lock_gv = get_eh_tab_lock(g);
    fn = LLVMAddFunction(g->mod, "__zan_eh_state",
        LLVMFunctionType(i8ptr, NULL, 0, 0));
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMTypeRef caty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64t, i64t }, 2, 0);
    LLVMValueRef calloc_fn = get_libc_fn(g, "calloc", caty);
    LLVMTypeRef free_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    LLVMValueRef free_fn = get_libc_fn(g, "free", free_ty);
    LLVMValueRef self_slot = get_eh_self_slot(g);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef hit   = LLVMAppendBasicBlockInContext(g->ctx, fn, "hit");
    LLVMBasicBlockRef look  = LLVMAppendBasicBlockInContext(g->ctx, fn, "look");
    LLVMBasicBlockRef retry = LLVMAppendBasicBlockInContext(g->ctx, fn, "retry");
    LLVMBasicBlockRef probe = LLVMAppendBasicBlockInContext(g->ctx, fn, "probe");
    LLVMBasicBlockRef mine  = LLVMAppendBasicBlockInContext(g->ctx, fn, "mine");
    LLVMBasicBlockRef claim = LLVMAppendBasicBlockInContext(g->ctx, fn, "claim");
    LLVMBasicBlockRef make  = LLVMAppendBasicBlockInContext(g->ctx, fn, "make");
    LLVMBasicBlockRef init  = LLVMAppendBasicBlockInContext(g->ctx, fn, "init");
    LLVMBasicBlockRef oom   = LLVMAppendBasicBlockInContext(g->ctx, fn, "oom");
    LLVMBasicBlockRef ready = LLVMAppendBasicBlockInContext(g->ctx, fn, "ready");
    LLVMBasicBlockRef next  = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
    LLVMBasicBlockRef full  = LLVMAppendBasicBlockInContext(g->ctx, fn, "full");
    LLVMBasicBlockRef probe2 = LLVMAppendBasicBlockInContext(g->ctx, fn, "probe2");

    /* Allocas stay in the entry block so mem2reg still promotes them; the
     * thread-local fast path is the entry block's only other content. */
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef i_slot = LLVMBuildAlloca(g->builder, i64t, "i");
    if (self_slot) {
        LLVMValueRef cached = LLVMBuildLoad2(g->builder, i8ptr, self_slot,
                                            "st.cached");
        LLVMBuildCondBr(g->builder,
            zan_icmp(g->builder, LLVMIntNE, cached, LLVMConstNull(i8ptr),
                     "cached.ok"), hit, look);
        LLVMPositionBuilderAtEnd(g->builder, hit);
        LLVMBuildRet(g->builder, cached);
    } else {
        LLVMBuildBr(g->builder, look);
        LLVMDeleteBasicBlock(hit);
    }

    /* Slow path: under the table mutex, lookup or claim a slot. The lock is
     * held for the whole search so a concurrent rehash cannot swap the arrays
     * mid-probe; the per-thread cache keeps every later use lock-free. */
    LLVMPositionBuilderAtEnd(g->builder, look);
    emit_eh_tab_lock_acquire(g, lock_gv);
    LLVMValueRef cap0 = LLVMBuildLoad2(g->builder, i64t, cap_gv, "st.cap0");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, cap0, LLVMConstInt(i64t, 0, 0), "st.fresh"),
        retry, probe);

    LLVMPositionBuilderAtEnd(g->builder, retry);
    emit_eh_tab_grow(g, state_ty, calloc_fn, free_ty, free_fn);
    LLVMBuildBr(g->builder, probe);

    LLVMPositionBuilderAtEnd(g->builder, probe);
    /* Key 0 marks a free slot, so shift the id out of that value. */
    LLVMValueRef key = zan_add(g->builder, emit_eh_thread_id(g),
        LLVMConstInt(i64t, 1, 0), "key");
    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64t, cap_gv, "st.cap");
    LLVMValueRef capm1 = LLVMBuildSub(g->builder, cap,
        LLVMConstInt(i64t, 1, 0), "st.capm1");
    LLVMValueRef h = zan_and(g->builder,
        LLVMBuildXor(g->builder, key,
            LLVMBuildLShr(g->builder, key, LLVMConstInt(i64t, 32, 0), "key.hi"),
            "key.mix"),
        capm1, "h");
    LLVMValueRef keys = LLVMBuildLoad2(g->builder, i64ptr, keys_gv, "st.keys");
    LLVMValueRef states = LLVMBuildLoad2(g->builder, states_ptr_ty, states_gv,
                                         "st.states");
    LLVMBuildStore(g->builder, LLVMConstInt(i64t, 0, 0), i_slot);
    LLVMBuildBr(g->builder, probe2);

    LLVMPositionBuilderAtEnd(g->builder, probe2);
    LLVMValueRef i = LLVMBuildLoad2(g->builder, i64t, i_slot, "i.v");
    LLVMValueRef pos = zan_and(g->builder, zan_add(g->builder, h, i, "pos.raw"),
        capm1, "pos");
    LLVMValueRef zero64 = LLVMConstInt(i64t, 0, 0);
    LLVMValueRef kp = LLVMBuildGEP2(g->builder, i64ptr, keys, &pos, 1, "kp");
    LLVMValueRef sp = LLVMBuildGEP2(g->builder, states_ptr_ty, states, &pos, 1, "sp");
    LLVMValueRef k = LLVMBuildLoad2(g->builder, i64t, kp, "k");
    LLVMSetAlignment(k, 8);
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, k, key, "is.mine"), mine, claim);

    LLVMPositionBuilderAtEnd(g->builder, mine);
    LLVMValueRef st_found = LLVMBuildLoad2(g->builder, i8ptr, sp, "st");
    if (self_slot) LLVMBuildStore(g->builder, st_found, self_slot);
    emit_eh_tab_lock_release(g, lock_gv);
    LLVMBuildRet(g->builder, st_found);

    LLVMPositionBuilderAtEnd(g->builder, claim);
    /* Free (never used) or a tombstone left by a thread that released its
     * block. Both are claimable; only the tombstone must keep a probe passing
     * through it, which it does because the probe stops at key 0 alone. */
    LLVMValueRef reusable = LLVMBuildOr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, k, LLVMConstInt(i64t, 0, 0), "is.free"),
        zan_icmp(g->builder, LLVMIntEQ, k,
            LLVMConstInt(i64t, ZAN_EH_TOMBSTONE, 0), "is.dead"), "reusable");
    LLVMBuildCondBr(g->builder, reusable, make, next);

    /* Claim it. Losing the race means another thread took this slot between the
     * load and here, so move on to the next one. */
    LLVMPositionBuilderAtEnd(g->builder, make);
    LLVMValueRef xchg = LLVMBuildAtomicCmpXchg(g->builder, kp,
        k, key,
        LLVMAtomicOrderingSequentiallyConsistent,
        LLVMAtomicOrderingSequentiallyConsistent, 0);
    LLVMBuildCondBr(g->builder,
        LLVMBuildExtractValue(g->builder, xchg, 1, "won"), init, next);

    LLVMPositionBuilderAtEnd(g->builder, init);
    LLVMValueRef st = zan_call2(g->builder, caty, calloc_fn,
        (LLVMValueRef[]){ LLVMConstInt(i64t, 1, 0), LLVMSizeOf(state_ty) }, 2,
        "st.new");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, st, LLVMConstNull(i8ptr), "nomem"),
        oom, ready);

    LLVMPositionBuilderAtEnd(g->builder, oom);
    emit_eh_oom_abort(g,
        "zan: out of memory creating this thread's exception state\n");

    LLVMPositionBuilderAtEnd(g->builder, ready);
    /* An empty handler stack is -1, the one field a zeroed block gets wrong. */
    LLVMBuildStore(g->builder, LLVMConstInt(i32t, (unsigned long long)-1, 1),
        LLVMBuildStructGEP2(g->builder, state_ty,
            LLVMBuildBitCast(g->builder, st, LLVMPointerType(state_ty, 0),
                "st.typed"), EH_F_TOP, "st.top"));
    LLVMBuildStore(g->builder, st, sp);
    if (self_slot) LLVMBuildStore(g->builder, st, self_slot);
    emit_eh_tab_lock_release(g, lock_gv);
    LLVMBuildRet(g->builder, st);

    LLVMPositionBuilderAtEnd(g->builder, next);
    LLVMValueRef ni = zan_add(g->builder,
        LLVMBuildLoad2(g->builder, i64t, i_slot, "i.v2"),
        LLVMConstInt(i64t, 1, 0), "i.next");
    LLVMBuildStore(g->builder, ni, i_slot);
    LLVMValueRef cap2 = LLVMBuildLoad2(g->builder, i64t, cap_gv, "st.cap2");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntUGE, ni, cap2, "exhausted"), full, probe2);

    /* No free slot at the current capacity: double the table (the mutex is
     * still held) and probe again from the top. */
    LLVMPositionBuilderAtEnd(g->builder, full);
    emit_eh_tab_grow(g, state_ty, calloc_fn, free_ty, free_fn);
    LLVMBuildBr(g->builder, probe);

    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* i8* <name>(i8* state, i32 idx): the address of entry `idx` in one of the
 * calling thread's chunked stacks, bringing its chunk into existence on first
 * use. `shift` entries of `elem` bytes per chunk; a chunk is never freed, so an
 * address handed out here stays valid and stays put for the life of the thread
 * -- required for armed jmp_bufs. Running past the chunk table (4096 live
 * handlers / 256K stacked temporaries per thread) or failing to allocate a
 * chunk is fatal: there is no correct way to continue a try whose handler
 * cannot be armed, and silently dropping the entry is what A19/A20 were
 * about. */
static LLVMValueRef get_eh_chunk_fn(zan_irgen_t *g, const char *name,
                                    unsigned field, unsigned shift,
                                    unsigned elem, const char *deep_msg) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, name);
    if (fn) return fn;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef tab_ty = LLVMArrayType(i8ptr, ZAN_EH_CHUNKS);
    LLVMTypeRef state_ty = get_eh_state_ty(g);
    fn = LLVMAddFunction(g->mod, name,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0));
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMTypeRef caty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64t, i64t }, 2, 0);
    LLVMValueRef calloc_fn = get_libc_fn(g, "calloc", caty);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef deep = LLVMAppendBasicBlockInContext(g->ctx, fn, "deep");
    LLVMBasicBlockRef look = LLVMAppendBasicBlockInContext(g->ctx, fn, "look");
    LLVMBasicBlockRef make = LLVMAppendBasicBlockInContext(g->ctx, fn, "make");
    LLVMBasicBlockRef oom = LLVMAppendBasicBlockInContext(g->ctx, fn, "oom");
    LLVMBasicBlockRef have = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
    LLVMValueRef idx = LLVMGetParam(fn, 1);
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef tab = LLVMBuildStructGEP2(g->builder, state_ty,
        LLVMBuildBitCast(g->builder, LLVMGetParam(fn, 0),
            LLVMPointerType(state_ty, 0), "st"), field, "tab");
    LLVMValueRef ci = LLVMBuildLShr(g->builder, idx,
        LLVMConstInt(i32t, shift, 0), "chunk");
    LLVMBuildCondBr(g->builder, zan_icmp(g->builder, LLVMIntUGE, ci,
        LLVMConstInt(i32t, ZAN_EH_CHUNKS, 0), "toodeep"), deep, look);
    LLVMPositionBuilderAtEnd(g->builder, deep);
    emit_eh_oom_abort(g, deep_msg);
    LLVMPositionBuilderAtEnd(g->builder, look);
    LLVMValueRef cslot = LLVMBuildGEP2(g->builder, tab_ty, tab,
        (LLVMValueRef[]){ LLVMConstInt(i32t, 0, 0), ci }, 2, "cslot");
    LLVMValueRef p = LLVMBuildLoad2(g->builder, i8ptr, cslot, "chunkp");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, p, LLVMConstNull(i8ptr), "fresh"),
        make, have);
    LLVMPositionBuilderAtEnd(g->builder, make);
    LLVMValueRef np = zan_call2(g->builder, caty, calloc_fn, (LLVMValueRef[]){
        LLVMConstInt(i64t, 1ULL << shift, 0),
        LLVMConstInt(i64t, elem, 0) }, 2, "nchunk");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, np, LLVMConstNull(i8ptr), "nomem"),
        oom, have);
    LLVMPositionBuilderAtEnd(g->builder, oom);
    emit_eh_oom_abort(g, "zan: out of memory extending the exception stack\n");
    LLVMPositionBuilderAtEnd(g->builder, have);
    LLVMValueRef base = LLVMBuildPhi(g->builder, i8ptr, "base");
    LLVMAddIncoming(base, (LLVMValueRef[]){ p, np },
        (LLVMBasicBlockRef[]){ look, make }, 2);
    LLVMBuildStore(g->builder, base, cslot);
    LLVMValueRef off = LLVMBuildMul(g->builder,
        LLVMBuildZExt(g->builder, zan_and(g->builder, idx,
            LLVMConstInt(i32t, (1u << shift) - 1, 0), "inchunk"), i64t, "off32"),
        LLVMConstInt(i64t, elem, 0), "off");
    LLVMBuildRet(g->builder,
        LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), base, &off, 1, "ep"));
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* Call one of the chunked-stack accessors for the calling thread. */
static LLVMValueRef emit_eh_chunk_call(zan_irgen_t *g, LLVMValueRef fn,
                                      LLVMValueRef idx, const char *name) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef args[2] = {
        LLVMBuildBitCast(g->builder, emit_eh_state(g), i8ptr, "eh.stb"), idx };
    return zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
        fn, args, 2, name);
}

static LLVMValueRef get_eh_slot_fn(zan_irgen_t *g) {
    return get_eh_chunk_fn(g, "__zan_eh_slot", EH_F_BUFS,
        ZAN_EH_SLOT_SHIFT, ZAN_EH_SLOT_BYTES,
        "zan: exception handler stack exhausted (too many nested try blocks)\n");
}

/* i8* to handler slot `idx`'s jmp_buf. */
static LLVMValueRef emit_eh_buf_ptr(zan_irgen_t *g, LLVMValueRef idx) {
    return emit_eh_chunk_call(g, get_eh_slot_fn(g), idx, "eh.bufp");
}

/* i32* to handler slot `idx`'s unwind-stack mark, which trails its jmp_buf. */
static LLVMValueRef emit_eh_mark_ptr(zan_irgen_t *g, LLVMValueRef idx) {
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef off = LLVMConstInt(i32t, ZAN_EH_MARK_OFF, 0);
    return LLVMBuildBitCast(g->builder,
        LLVMBuildGEP2(g->builder, i8t, emit_eh_buf_ptr(g, idx), &off, 1, "eh.markb"),
        LLVMPointerType(i32t, 0), "eh.markp");
}

/* Marks a function and a call to it with a simple enum attribute. */
static void add_enum_attr(zan_irgen_t *g, LLVMValueRef fn, LLVMValueRef call,
                          const char *name) {
    unsigned kind = LLVMGetEnumAttributeKindForName(name, strlen(name));
    if (kind == 0) return;
    LLVMAttributeRef attr = LLVMCreateEnumAttribute(g->ctx, kind, 0);
    LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, attr);
    if (call)
        LLVMAddCallSiteAttribute(call, (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                                 attr);
}

/* i32 setjmp on the current target: `_setjmp(buf, NULL)` on Windows, `_setjmp(buf)`
 * elsewhere (no sigmask save). The Windows form is load-bearing, not a
 * redundant argument: on the bundled toolchain the generated program links
 * msvcrt.dll, whose `_setjmp` follows the MSVC x64 convention of saving the
 * caller's return address from rdx into jmp_buf[0] (longjmp restores it), and
 * it is the two-argument call shape that makes the LLVM backend emit the
 * `rdx = return address` preamble for `_setjmp`. A one-argument call leaves
 * rdx as garbage, longjmp jumps to it, and every throw crashes. The call
 * carries `returns_twice`: without it the backend is free to keep values in
 * registers across the setjmp, and whatever the longjmp'd-to catch block reads
 * afterwards is garbage (it showed up as corrupted exception messages and
 * access violations in unoptimized builds). */
static LLVMValueRef emit_eh_setjmp(zan_irgen_t *g, LLVMValueRef bufp) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (g->target_is_windows) {
        LLVMTypeRef ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
        LLVMValueRef fn = get_libc_fn(g, "_setjmp", ty);
        LLVMValueRef call = zan_call2(g->builder, ty, fn,
            (LLVMValueRef[]){ bufp, LLVMConstNull(i8ptr) }, 2, "sj");
        add_enum_attr(g, fn, call, "returns_twice");
        return call;
    }
    LLVMTypeRef ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMValueRef fn = get_libc_fn(g, "_setjmp", ty);
    LLVMValueRef call = zan_call2(g->builder, ty, fn, &bufp, 1, "sj");
    add_enum_attr(g, fn, call, "returns_twice");
    return call;
}

static void emit_eh_longjmp(zan_irgen_t *g, LLVMValueRef bufp) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0);
    LLVMValueRef fn = get_libc_fn(g, "longjmp", ty);
    LLVMValueRef call = zan_call2(g->builder, ty, fn,
        (LLVMValueRef[]){ bufp, LLVMConstInt(i32t, 1, 0) }, 2, "");
    add_enum_attr(g, fn, call, "noreturn");
}

/* ---- WebAssembly EH (wasm32 target) ---------------------------------------
 * wasi-libc ships no setjmp/longjmp, so the wasm32 target lowers try/catch
 * onto the WebAssembly exception-handling proposal instead: `try` becomes a
 * catchswitch whose unwind edges are the invokes zan_call2 emits inside the
 * body, `throw` becomes the `wasm.throw` intrinsic with the C++ exception
 * tag (the tag object itself is toolchain/wasm32/zanrt_ehtag.o, linked on
 * demand). The engine unwinds, so the __zan_eh_* globals keep exactly the
 * role they have in the longjmp lowering: in-flight object, owned flag and
 * type descriptor for catch dispatch, armed-handler stack for cleanup. */

/* void @llvm.wasm.throw(i32 tag, i8* payload), noreturn: the raise used where
 * no same-function pad exists (a throw escaping this frame). The engine
 * unwinds to the caller's region; no link-time symbol is involved. */
static LLVMValueRef get_wasm_throw_intrinsic_fn(zan_irgen_t *g) {
    if (g->wasm_eh_throw_intrinsic_fn)
        return g->wasm_eh_throw_intrinsic_fn;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i32t, i8ptr }, 2, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "llvm.wasm.throw", ty);
    add_enum_attr(g, fn, NULL, "noreturn");
    g->wasm_eh_throw_intrinsic_fn = fn;
    g->wasm_eh_used = true;
    return fn;
}

/* void @__cxa_throw(ptr obj, ptr tinfo, ptr dtor), noreturn: the raise call
 * WasmEHPrepare recognizes. Clang emits the same invoke for a C++ throw; the
 * wasm backend rewrites it into the `throw` instruction (tag __cpp_exception,
 * defined by toolchain/wasm32/zanrt_ehtag.o) and drops the symbol. */
static LLVMValueRef get_wasm_cxa_throw_fn(zan_irgen_t *g) {
    if (g->wasm_eh_throw_fn) return g->wasm_eh_throw_fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr }, 3, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__cxa_throw", ty);
    add_enum_attr(g, fn, NULL, "noreturn");
    g->wasm_eh_throw_fn = fn;
    g->wasm_eh_used = true;
    return fn;
}

/* i32 @__gxx_wasm_personality_v0(...): the name the WebAssembly backend
 * requires on every function carrying EH IR. No libcxxabi is linked -- Zan
 * dispatches clauses on its own globals, the personality is a marker. */
static LLVMValueRef get_wasm_personality_fn(zan_irgen_t *g) {
    if (g->wasm_eh_personality_fn) return g->wasm_eh_personality_fn;
    LLVMTypeRef ty = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), NULL, 0, 1);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__gxx_wasm_personality_v0", ty);
    g->wasm_eh_personality_fn = fn;
    g->wasm_eh_used = true;
    return fn;
}

/* Attach the wasm personality to the function being emitted. Called when a
 * try is lowered (catchswitch requires it) so lambdas and async $resume
 * bodies pick it up through the same path. */
static void wasm_eh_set_personality(zan_irgen_t *g) {
    if (!g->target_is_wasm || !g->current_fn) return;
    LLVMSetPersonalityFn(g->current_fn, get_wasm_personality_fn(g));
}

static void wasm_note_state_fn(zan_irgen_t *g, LLVMValueRef state_fn) {
    if (g->target_is_wasm) g->wasm_eh_state_fn = state_fn;
}

/* Raising the exception: the throw-site releases have already run, the
 * in-flight globals are stored. wasm.throw never returns; the engine unwinds
 * to the innermost enclosing try (of this frame or a caller's). */
static void emit_wasm_throw_op(zan_irgen_t *g, LLVMValueRef exc_obj) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef fn = get_wasm_cxa_throw_fn(g);
    if (LLVMGetTypeKind(LLVMTypeOf(exc_obj)) == LLVMPointerTypeKind &&
        LLVMTypeOf(exc_obj) != i8ptr)
        exc_obj = LLVMBuildBitCast(g->builder, exc_obj, i8ptr, "wt.exc");
    /* The raise is an invoke of __cxa_throw to the current try's pad -- the
     * exact shape WasmEHPrepare rewrites into the `throw` instruction. wasm
     * EH ties a block to its enclosing handler only through unwind edges, so
     * a plain raise call inside the same function would never reach the
     * catchswitch. Inside a catchpad funclet (rethrow) the current pad comes
     * from a different function's stack or is absent, so fall back to a
     * plain call there; the funclet's own catchswitch covers it. */
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef lpad = g->wasm_try_depth > 0
        ? g->wasm_lpad_stack[g->wasm_try_depth - 1] : NULL;
    if (lpad &&
        LLVMGetBasicBlockParent(lpad) == LLVMGetBasicBlockParent(cur) &&
        !LLVMGetBasicBlockTerminator(cur)) {
        LLVMBasicBlockRef cont = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(cur), "wt.cont");
        LLVMBuildInvoke2(g->builder, LLVMGlobalGetValueType(fn), fn,
            (LLVMValueRef[]){ exc_obj, LLVMConstNull(i8ptr),
                              LLVMConstNull(i8ptr) }, 3, cont, lpad, "");
        LLVMPositionBuilderAtEnd(g->builder, cont);
        LLVMBuildUnreachable(g->builder);
        return;
    }
    /* No same-function pad: the throw escapes this frame and the engine
     * unwinds into the caller's region. The intrinsic's plain call lowers
     * straight to the `throw` opcode (__cxa_throw would never be rewritten
     * out here and would fail to link). */
    LLVMValueRef inl = get_wasm_throw_intrinsic_fn(g);
    LLVMBuildCall2(g->builder,
        LLVMGlobalGetValueType(inl), inl,
        (LLVMValueRef[]){ LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0),
                          exc_obj }, 2, "");
    LLVMBuildUnreachable(g->builder);
}

/* The landing pad of one armed try: catchswitch (unwinds to the caller, i.e.
 * the enclosing invoke) with a single catch_all handler; the catchpad ends
 * in a CatchRet that resumes normal control flow in the try's catch block,
 * where the longjmp lowering's catch code takes over. The pad block itself
 * carries no payload handling: Zan reads the exception from its globals. */
static LLVMBasicBlockRef emit_wasm_lpad(zan_irgen_t *g,
                                        LLVMBasicBlockRef catch_bb) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef lpad_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "wtry.lpad");
    LLVMBasicBlockRef pad_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "wtry.pad");
    LLVMValueRef pers = get_wasm_personality_fn(g);
    LLVMPositionBuilderAtEnd(g->builder, lpad_bb);
    LLVMValueRef cs = LLVMBuildCatchSwitch(g->builder, NULL, NULL, 1, "wtry.cs");
    LLVMAddHandler(cs, pad_bb);
    LLVMPositionBuilderAtEnd(g->builder, pad_bb);
    LLVMValueRef cpa[1] = { LLVMConstNull(i8ptr) }; /* catch-all */
    LLVMValueRef cp = LLVMBuildCatchPad(g->builder, cs, cpa, 1, "wtry.cp");
    LLVMBuildCatchRet(g->builder, cp, catch_bb);
    (void)pers;
    return lpad_bb;
}

/* ---- EH-owned temporaries -------------------------------------------------
 * `throw` unwinds with longjmp, which skips the compiler-emitted releases of
 * owned temporaries that are live across a call (a partially-constructed
 * object during its ctor, or an owned receiver temp of a method call), so a
 * throwing callee would leak them. Each such temp is pushed onto a small
 * global stack for the duration of the call and popped on normal return;
 * entering a catch releases every entry pushed after its try was entered. */
/* Storage for the unwind stack: chunked like the handler stack (see
 * get_eh_chunk_fn), because its depth is recursion depth times the number of
 * live owning locals per frame -- nothing the compiler can bound. The fixed
 * array it replaced silently dropped entries past its end and leaked whatever
 * they pointed at when an exception unwound past them (A19). */
static LLVMValueRef get_eh_tmp_top_global(zan_irgen_t *g) {
    return emit_eh_field_ptr(g, EH_F_TMPS_TOP, "eh.tmptopp");
}

/* i8* __zan_eh_tmp_slot_fast(i8* state, i32 idx): entry `idx` when it lives in
 * the unwind stack's first chunk, which is every entry a program under 4096
 * live owned temporaries per thread ever asks for. Only chunk creation and the
 * deeper chunks stay out of line; the accessor call itself was ~6% of a
 * keep-alive request's user time, paid twice per stacked temporary. */
static LLVMValueRef get_eh_tmp_slot_fast_fn(zan_irgen_t *g, LLVMValueRef slow) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_tmp_slot_fast");
    if (fn) return fn;
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8t, 0);
    LLVMTypeRef state_ty = get_eh_state_ty(g);
    LLVMTypeRef tab_ty = LLVMArrayType(i8ptr, ZAN_EH_CHUNKS);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr,
        (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_eh_tmp_slot_fast", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    add_enum_attr(g, fn, NULL, "alwaysinline");
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef look = LLVMAppendBasicBlockInContext(g->ctx, fn, "look");
    LLVMBasicBlockRef have = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
    LLVMBasicBlockRef slowbb = LLVMAppendBasicBlockInContext(g->ctx, fn, "slow");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMValueRef st = LLVMGetParam(fn, 0);
    LLVMValueRef idx = LLVMGetParam(fn, 1);
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntUGE, idx,
            LLVMConstInt(i32t, 1u << ZAN_EH_TMP_SHIFT, 0), "tmp.deep"),
        slowbb, look);
    LLVMPositionBuilderAtEnd(g->builder, look);
    LLVMValueRef tab = LLVMBuildStructGEP2(g->builder, state_ty,
        LLVMBuildBitCast(g->builder, st, LLVMPointerType(state_ty, 0), "st.t"),
        EH_F_TMPS, "tmp.tab");
    LLVMValueRef c0p = LLVMBuildGEP2(g->builder, tab_ty, tab,
        (LLVMValueRef[]){ LLVMConstInt(i32t, 0, 0), LLVMConstInt(i32t, 0, 0) }, 2,
        "tmp.c0p");
    LLVMValueRef c0 = LLVMBuildLoad2(g->builder, i8ptr, c0p, "tmp.c0");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, c0, LLVMConstNull(i8ptr), "tmp.fresh"),
        slowbb, have);
    LLVMPositionBuilderAtEnd(g->builder, have);
    LLVMValueRef off = LLVMBuildMul(g->builder,
        LLVMBuildZExt(g->builder, idx, i64t, "tmp.i64"),
        LLVMConstInt(i64t, 8, 0), "tmp.off");
    LLVMValueRef ep = LLVMBuildGEP2(g->builder, i8t, c0, &off, 1, "tmp.ep");
    LLVMBuildBr(g->builder, done);
    LLVMPositionBuilderAtEnd(g->builder, slowbb);
    LLVMValueRef sp = zan_call2(g->builder, fnty, slow,
        (LLVMValueRef[]){ st, idx }, 2, "tmp.slow");
    LLVMBuildBr(g->builder, done);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMValueRef phi = LLVMBuildPhi(g->builder, i8ptr, "tmp.r");
    LLVMAddIncoming(phi, (LLVMValueRef[]){ ep, sp },
        (LLVMBasicBlockRef[]){ have, slowbb }, 2);
    LLVMBuildRet(g->builder, phi);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* i8** to unwind-stack entry `idx`. */
static LLVMValueRef emit_eh_tmp_slot_ptr(zan_irgen_t *g, LLVMValueRef idx) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef fn = get_eh_tmp_slot_fast_fn(g,
        get_eh_chunk_fn(g, "__zan_eh_tmp_slot", EH_F_TMPS,
            ZAN_EH_TMP_SHIFT, 8, "zan: exception unwind stack exhausted\n"));
    return LLVMBuildBitCast(g->builder,
        emit_eh_chunk_call(g, fn, idx, "eh.tslot"),
        LLVMPointerType(i8ptr, 0), "eh.tslotp");
}

/* i32 flag: the in-flight __zan_eh_exc holds a +1 reference (class-typed
 * throw) that the catch must release after the handler runs. */
static LLVMValueRef get_eh_exc_owned_global(zan_irgen_t *g) {
    return emit_eh_field_ptr(g, EH_F_EXC_OWNED, "eh.ownp");
}

/* i8* pointing at the thrown object's class type-descriptor (see
 * get_class_tid_global); null for string/non-class throws. Catch clauses use
 * it for type-based dispatch. */
static LLVMValueRef get_eh_exc_tid_global(zan_irgen_t *g) {
    return emit_eh_field_ptr(g, EH_F_EXC_TID, "eh.tidp");
}

/* Per-class type descriptor for exception dispatch: an i8* global named
 * __zan_tid_<Class> whose value is the base class's descriptor (or null for
 * a root class). Identity is the descriptor's ADDRESS; the stored pointer
 * links the inheritance chain so `catch (Base b)` matches derived throws. */
static LLVMValueRef get_class_tid_global(zan_irgen_t *g, zan_symbol_t *sym) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    char name[512];
    snprintf(name, sizeof(name), "__zan_tid_%.*s", sym->name.len, sym->name.str);
    LLVMValueRef v = LLVMGetNamedGlobal(g->mod, name);
    if (v) return v;
    v = LLVMAddGlobal(g->mod, i8ptr, name);
    LLVMSetLinkage(v, LLVMInternalLinkage);
    LLVMSetInitializer(v, LLVMConstNull(i8ptr));
    if (sym->type && sym->type->base_type && sym->type->base_type->sym) {
        LLVMValueRef base_tid = get_class_tid_global(g, sym->type->base_type->sym);
        LLVMSetInitializer(v, LLVMConstBitCast(base_tid, i8ptr));
    }
    return v;
}

/* i1 __zan_eh_tid_match(i8* thrown, i8* want): walks the thrown descriptor's
 * base chain looking for `want`. A null thrown descriptor (string / legacy
 * throw) matches NO typed clause: the object is not an Exception subclass, so
 * binding it to a typed catch variable (and then reading fields off it, e.g.
 * e.Message) would dereference string data as an object and crash. A string
 * throw is only caught by an untyped `catch { }` (or a non-class clause like
 * `catch (string s)`), which the emitter routes straight to its body without
 * calling this function. */
static LLVMValueRef get_eh_tid_match_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_tid_match");
    if (fn) return fn;
    LLVMTypeRef i1t = LLVMInt1TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fnty = LLVMFunctionType(i1t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_eh_tid_match", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "loop");
    LLVMBasicBlockRef test = LLVMAppendBasicBlockInContext(g->ctx, fn, "test");
    LLVMBasicBlockRef step = LLVMAppendBasicBlockInContext(g->ctx, fn, "step");
    LLVMBasicBlockRef yes = LLVMAppendBasicBlockInContext(g->ctx, fn, "yes");
    LLVMBasicBlockRef no = LLVMAppendBasicBlockInContext(g->ctx, fn, "no");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef thrown = LLVMGetParam(fn, 0);
    LLVMValueRef want = LLVMGetParam(fn, 1);
    LLVMValueRef cur = LLVMBuildAlloca(g->builder, i8ptr, "cur");
    LLVMBuildStore(g->builder, thrown, cur);
    LLVMValueRef thrown_null = zan_icmp(g->builder, LLVMIntEQ, thrown,
        LLVMConstNull(i8ptr), "tnull");
    LLVMBuildCondBr(g->builder, thrown_null, no, loop);
    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef c = LLVMBuildLoad2(g->builder, i8ptr, cur, "c");
    LLVMValueRef cnull = zan_icmp(g->builder, LLVMIntEQ, c,
        LLVMConstNull(i8ptr), "cnull");
    LLVMBuildCondBr(g->builder, cnull, no, test);
    LLVMPositionBuilderAtEnd(g->builder, test);
    LLVMValueRef eq = zan_icmp(g->builder, LLVMIntEQ, c, want, "eq");
    LLVMBuildCondBr(g->builder, eq, yes, step);
    LLVMPositionBuilderAtEnd(g->builder, step);
    LLVMValueRef cp = LLVMBuildBitCast(g->builder, c,
        LLVMPointerType(i8ptr, 0), "cp");
    LLVMValueRef next = LLVMBuildLoad2(g->builder, i8ptr, cp, "next");
    LLVMBuildStore(g->builder, next, cur);
    LLVMBuildBr(g->builder, loop);
    LLVMPositionBuilderAtEnd(g->builder, yes);
    LLVMBuildRet(g->builder, LLVMConstInt(i1t, 1, 0));
    LLVMPositionBuilderAtEnd(g->builder, no);
    LLVMBuildRet(g->builder, LLVMConstInt(i1t, 0, 0));
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

static LLVMValueRef get_eh_tmp_push_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_tmp_push");
    if (fn) return fn;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_eh_tmp_push", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    /* after positioning: the state pointer is materialized in the entry block
     * of whatever function is being emitted, and must be this one */
    LLVMValueRef top_g = get_eh_tmp_top_global(g);
    LLVMValueRef top = LLVMBuildLoad2(g->builder, i32t, top_g, "top");
    LLVMBuildStore(g->builder, LLVMGetParam(fn, 0), emit_eh_tmp_slot_ptr(g, top));
    LLVMBuildStore(g->builder,
        zan_add(g->builder, top, LLVMConstInt(i32t, 1, 0), "ntop"), top_g);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

static LLVMValueRef get_eh_tmp_pop_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_tmp_pop");
    if (fn) return fn;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    fn = LLVMAddFunction(g->mod, "__zan_eh_tmp_pop", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef dec = LLVMAppendBasicBlockInContext(g->ctx, fn, "dec");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef top_g = get_eh_tmp_top_global(g);
    LLVMValueRef top = LLVMBuildLoad2(g->builder, i32t, top_g, "top");
    LLVMValueRef pos = zan_icmp(g->builder, LLVMIntSGT, top,
        LLVMConstInt(i32t, 0, 0), "pos");
    LLVMBuildCondBr(g->builder, pos, dec, done);
    LLVMPositionBuilderAtEnd(g->builder, dec);
    LLVMValueRef ntop = zan_sub(g->builder, top, LLVMConstInt(i32t, 1, 0), "ntop");
    LLVMBuildStore(g->builder, ntop, top_g);
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr),
        emit_eh_tmp_slot_ptr(g, ntop));
    LLVMBuildBr(g->builder, done);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* void __zan_eh_tmp_unwind(i32 mark): release every stacked temp above
 * `mark`, restoring the stack to the try-entry depth. Object/class entries use
 * zan_rt_release_dyn; string slots use zan_rt_str_release because strings have
 * a different header and are not valid inputs to the dynamic object releaser;
 * delegate slots go through the closure release, which leaves a bare function
 * pointer -- a static method, a non-capturing lambda, a native address --
 * alone instead of decrementing a refcount it never had. Array slots use
 * zan_rt_arr_release: an array keeps its refcount in its own prefix, and its
 * count word sits where an object header keeps a refcount, so the dynamic
 * releaser would decrement the length instead. (An unwound array of
 * rc-managed elements drops the buffer but not the elements: the element type
 * is a compile-time property the runtime entry does not carry.) */
static LLVMValueRef get_eh_tmp_unwind_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_tmp_unwind");
    if (fn) return fn;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i32t, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_eh_tmp_unwind", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(g->ctx, fn, "head");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef rel = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel");
    LLVMBasicBlockRef rel_slot = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel.slot");
    LLVMBasicBlockRef rel_pick = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel.pick");
    LLVMBasicBlockRef rel_dlg = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel.dlg");
    LLVMBasicBlockRef rel_obj = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel.obj");
    LLVMBasicBlockRef rel_dyn = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel.dyn");
    LLVMBasicBlockRef rel_str = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel.str");
    LLVMBasicBlockRef rel_arr = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel.arr");
    LLVMBasicBlockRef rel_pick2 = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel.pick2");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef top_g = get_eh_tmp_top_global(g);
    LLVMBuildBr(g->builder, head);
    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef top = LLVMBuildLoad2(g->builder, i32t, top_g, "top");
    LLVMValueRef above = zan_icmp(g->builder, LLVMIntSGT, top,
        LLVMGetParam(fn, 0), "above");
    LLVMBuildCondBr(g->builder, above, body, done);
    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef ntop = zan_sub(g->builder, top, LLVMConstInt(i32t, 1, 0), "ntop");
    LLVMBuildStore(g->builder, ntop, top_g);
    LLVMBuildBr(g->builder, rel);
    LLVMPositionBuilderAtEnd(g->builder, rel);
    LLVMValueRef slot = emit_eh_tmp_slot_ptr(g, ntop);
    LLVMValueRef obj = LLVMBuildLoad2(g->builder, i8ptr, slot, "obj");
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), slot);
    /* Entry flavours share the stack. Plain object pointers have their low
     * bits clear (RC allocations are naturally aligned). Variable slots set
     * bit 0 and carry the release flavour of what they hold in bits 1-2
     * (ZAN_EH_SLOT_*). The slot flavour keeps up with reassignment and lets
     * unwind null the variable; the flavour is required because
     * zan_rt_release_dyn expects an object allocation header, which neither a
     * Zan string nor a bare function pointer has. */
    LLVMValueRef obj_i = LLVMBuildPtrToInt(g->builder, obj,
        LLVMInt64TypeInContext(g->ctx), "obji");
    LLVMValueRef tagged = LLVMBuildTrunc(g->builder,
        obj_i, LLVMInt1TypeInContext(g->ctx), "tagged");
    LLVMBuildCondBr(g->builder, tagged, rel_slot, rel_obj);

    LLVMPositionBuilderAtEnd(g->builder, rel_slot);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef kind = zan_and(g->builder, obj_i,
        LLVMConstInt(i64t, 6, 0), "eh.kind");
    /* The slot holds an i8*, so the untagged address is an i8** -- typing it
     * as i8* makes the load/store below fail verification on a typed-pointer
     * LLVM ("Stored value type does not match pointer operand type"). */
    LLVMValueRef untag = LLVMBuildIntToPtr(g->builder,
        zan_and(g->builder, obj_i,
            LLVMConstInt(i64t, ~(uint64_t)7, 0), "untag"),
        LLVMPointerType(i8ptr, 0), "vslot");
    LLVMValueRef vobj = LLVMBuildLoad2(g->builder, i8ptr, untag, "vobj");
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), untag);
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, kind,
            LLVMConstInt(i64t, ZAN_EH_SLOT_DLG << 1, 0), "isdlg"),
        rel_dlg, rel_pick);

    LLVMPositionBuilderAtEnd(g->builder, rel_pick);
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, kind,
            LLVMConstInt(i64t, ZAN_EH_SLOT_STR << 1, 0), "isstr"),
        rel_str, rel_pick2);

    LLVMPositionBuilderAtEnd(g->builder, rel_pick2);
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, kind,
            LLVMConstInt(i64t, ZAN_EH_SLOT_ARR << 1, 0), "isarr"),
        rel_arr, rel_dyn);

    LLVMPositionBuilderAtEnd(g->builder, rel_arr);
    zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_arr_release, &vobj, 1, "");
    LLVMBuildBr(g->builder, head);

    LLVMPositionBuilderAtEnd(g->builder, rel_dlg);
    emit_closure_release(g, vobj);
    LLVMBuildBr(g->builder, head);

    LLVMPositionBuilderAtEnd(g->builder, rel_obj);
    LLVMBuildBr(g->builder, rel_dyn);

    LLVMPositionBuilderAtEnd(g->builder, rel_dyn);
    LLVMValueRef dyn_obj = LLVMBuildPhi(g->builder, i8ptr, "dyn.obj");
    LLVMValueRef dyn_v[2] = { vobj, obj };
    LLVMBasicBlockRef dyn_b[2] = { rel_pick2, rel_obj };
    LLVMAddIncoming(dyn_obj, dyn_v, dyn_b, 2);
    zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_release_dyn, &dyn_obj, 1, "");
    LLVMBuildBr(g->builder, head);

    LLVMPositionBuilderAtEnd(g->builder, rel_str);
    zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_str_release, &vobj, 1, "");
    LLVMBuildBr(g->builder, head);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

static void emit_eh_tmp_push(zan_irgen_t *g, LLVMValueRef obj) {
    if (!obj || LLVMGetTypeKind(LLVMTypeOf(obj)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(obj) != i8ptr)
        obj = LLVMBuildBitCast(g->builder, obj, i8ptr, "eh.tmp");
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    zan_call2(g->builder, fnty, get_eh_tmp_push_fn(g), &obj, 1, "");
}

/* Register a *variable slot* with the unwinder: an exception raised anywhere
 * below this frame releases whatever the variable holds at that moment, which
 * is the only way a longjmp-based unwind can free the locals of the frames it
 * skips over (A8-12). */
static void emit_eh_tmp_push_slot(zan_irgen_t *g, LLVMValueRef slot, int kind) {
    if (!slot || LLVMGetTypeKind(LLVMTypeOf(slot)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    uint64_t tag = 1u | ((uint64_t)kind << 1);
    LLVMValueRef tagged = LLVMBuildIntToPtr(g->builder,
        zan_or(g->builder, LLVMBuildPtrToInt(g->builder, slot, i64t, "slot.i"),
            LLVMConstInt(i64t, tag, 0), "slot.t"),
        i8ptr, "eh.slot");
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    zan_call2(g->builder, fnty, get_eh_tmp_push_fn(g), &tagged, 1, "");
}

/* Release everything the unwind stack holds above the depth handler `top` was
 * armed at. Emitted at the throw site, before the longjmp: the frames between
 * here and the handler are still alive at this point, and their locals are
 * only reachable through the slots they registered. */
static void emit_eh_unwind_to_handler(zan_irgen_t *g, LLVMValueRef top) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef mp = emit_eh_mark_ptr(g, top);
    LLVMValueRef mark = LLVMBuildLoad2(g->builder, i32t, mp, "eh.mark");
    LLVMTypeRef uwty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i32t, 1, 0);
    zan_call2(g->builder, uwty, get_eh_tmp_unwind_fn(g), &mark, 1, "");
}

static void emit_eh_tmp_pop(zan_irgen_t *g) {
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    zan_call2(g->builder, fnty, get_eh_tmp_pop_fn(g), NULL, 0, "");
}

/* void __zan_eh_tmp_drop(i8* obj): unregister one stacked temp by identity.
 * A catch body that is abandoned by `return`/`break`/`continue`/`throw` has to
 * take back the exception it stacked at handler entry, but locals declared in
 * the handler sit above it, so popping the top would unregister the wrong
 * entry. Nulls the topmost entry equal to `obj` (and shrinks the stack when
 * that entry is the top). */
static LLVMValueRef get_eh_tmp_drop_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_tmp_drop");
    if (fn) return fn;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_eh_tmp_drop", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(g->ctx, fn, "head");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef hit = LLVMAppendBasicBlockInContext(g->ctx, fn, "hit");
    LLVMBasicBlockRef shrink = LLVMAppendBasicBlockInContext(g->ctx, fn, "shrink");
    LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef top_g = get_eh_tmp_top_global(g);
    LLVMValueRef i_slot = LLVMBuildAlloca(g->builder, i32t, "i");
    LLVMBuildStore(g->builder,
        LLVMBuildLoad2(g->builder, i32t, top_g, "top0"), i_slot);
    LLVMBuildBr(g->builder, head);

    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef i = LLVMBuildLoad2(g->builder, i32t, i_slot, "i.v");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntSGT, i, LLVMConstInt(i32t, 0, 0), "more"),
        body, done);

    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef idx = zan_sub(g->builder,
        LLVMBuildLoad2(g->builder, i32t, i_slot, "i.v2"),
        LLVMConstInt(i32t, 1, 0), "idx");
    LLVMBuildStore(g->builder, idx, i_slot);
    LLVMValueRef slot = emit_eh_tmp_slot_ptr(g, idx);
    LLVMValueRef obj = LLVMBuildLoad2(g->builder, i8ptr, slot, "obj");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, obj, LLVMGetParam(fn, 0), "same"),
        hit, next);

    LLVMPositionBuilderAtEnd(g->builder, hit);
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), slot);
    LLVMValueRef top = LLVMBuildLoad2(g->builder, i32t, top_g, "top");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, top,
            zan_add(g->builder, idx, LLVMConstInt(i32t, 1, 0), "idx1"), "is.top"),
        shrink, done);
    LLVMPositionBuilderAtEnd(g->builder, shrink);
    LLVMBuildStore(g->builder, idx, top_g);
    LLVMBuildBr(g->builder, done);

    LLVMPositionBuilderAtEnd(g->builder, next);
    LLVMBuildBr(g->builder, head);

    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

static void emit_eh_tmp_drop(zan_irgen_t *g, LLVMValueRef obj) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (!obj || LLVMGetTypeKind(LLVMTypeOf(obj)) != LLVMPointerTypeKind) return;
    if (LLVMTypeOf(obj) != i8ptr)
        obj = LLVMBuildBitCast(g->builder, obj, i8ptr, "eh.drop");
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    zan_call2(g->builder, fnty, get_eh_tmp_drop_fn(g), &obj, 1, "");
}

/* i8* __zan_str_join(i8* sep, i8* lst): join a List<string>'s elements with
 * `sep` into a fresh RC string (two passes: measure, then copy). */
static LLVMValueRef get_str_join_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_join");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_join", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_ty);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef m_cond = LLVMAppendBasicBlockInContext(g->ctx, fn, "m.cond");
    LLVMBasicBlockRef m_body = LLVMAppendBasicBlockInContext(g->ctx, fn, "m.body");
    LLVMBasicBlockRef c_pre = LLVMAppendBasicBlockInContext(g->ctx, fn, "c.pre");
    LLVMBasicBlockRef c_cond = LLVMAppendBasicBlockInContext(g->ctx, fn, "c.cond");
    LLVMBasicBlockRef c_body = LLVMAppendBasicBlockInContext(g->ctx, fn, "c.body");
    LLVMBasicBlockRef c_sep = LLVMAppendBasicBlockInContext(g->ctx, fn, "c.sep");
    LLVMBasicBlockRef c_next = LLVMAppendBasicBlockInContext(g->ctx, fn, "c.next");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef sep = LLVMGetParam(fn, 0);
    LLVMValueRef lraw = LLVMGetParam(fn, 1);
    LLVMValueRef lp = LLVMBuildBitCast(g->builder, lraw,
        LLVMPointerType(g->list_struct_type, 0), "lp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "cntp");
    LLVMValueRef n = LLVMBuildLoad2(g->builder, i64, cntp, "n");
    LLVMValueRef dpp = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "dpp");
    LLVMValueRef data = LLVMBuildLoad2(g->builder, i64ptr, dpp, "data");
    LLVMValueRef seplen = zan_call2(g->builder, strlen_ty, g->fn_strlen, &sep, 1, "seplen");
    LLVMValueRef idx_a = LLVMBuildAlloca(g->builder, i64, "i");
    LLVMValueRef tot_a = LLVMBuildAlloca(g->builder, i64, "tot");
    LLVMValueRef pos_a = LLVMBuildAlloca(g->builder, i64, "pos");
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
    /* total starts at seplen*(n-1), clamped at 0 for empty lists */
    LLVMValueRef nz = zan_icmp(g->builder, LLVMIntSGT, n, LLVMConstInt(i64, 0, 0), "nz");
    LLVMValueRef nm1 = zan_sub(g->builder, n, LLVMConstInt(i64, 1, 0), "nm1");
    LLVMValueRef sepsum = LLVMBuildSelect(g->builder, nz,
        zan_mul(g->builder, seplen, nm1, "ss"), LLVMConstInt(i64, 0, 0), "sepsum");
    LLVMBuildStore(g->builder, sepsum, tot_a);
    LLVMBuildBr(g->builder, m_cond);
    LLVMPositionBuilderAtEnd(g->builder, m_cond);
    LLVMValueRef mi = LLVMBuildLoad2(g->builder, i64, idx_a, "mi");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntSLT, mi, n, "mlt"), m_body, c_pre);
    LLVMPositionBuilderAtEnd(g->builder, m_body);
    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &mi, 1, "slot");
    /* a list may legitimately hold null entries (xs.Add(null)); join treats
     * them as empty strings instead of handing NULL to strlen */
    LLVMValueRef sv = emit_str_nonnull(g, LLVMBuildIntToPtr(g->builder,
        LLVMBuildLoad2(g->builder, i64, slot, "svi"), i8ptr, "sv"));
    LLVMValueRef sl = zan_call2(g->builder, strlen_ty, g->fn_strlen, &sv, 1, "sl");
    LLVMBuildStore(g->builder,
        zan_add(g->builder, LLVMBuildLoad2(g->builder, i64, tot_a, "t0"), sl, "t1"), tot_a);
    LLVMBuildStore(g->builder,
        zan_add(g->builder, mi, LLVMConstInt(i64, 1, 0), "mi1"), idx_a);
    LLVMBuildBr(g->builder, m_cond);
    LLVMPositionBuilderAtEnd(g->builder, c_pre);
    LLVMValueRef total = LLVMBuildLoad2(g->builder, i64, tot_a, "total");
    LLVMValueRef buf = emit_string_alloc_rc(g,
        zan_add(g->builder, total, LLVMConstInt(i64, 1, 0), "tot1"));
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), pos_a);
    LLVMBuildBr(g->builder, c_cond);
    LLVMPositionBuilderAtEnd(g->builder, c_cond);
    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntSLT, ci, n, "clt"), c_body, done);
    LLVMPositionBuilderAtEnd(g->builder, c_body);
    LLVMValueRef slot2 = LLVMBuildGEP2(g->builder, i64, data, &ci, 1, "slot2");
    LLVMValueRef sv2 = emit_str_nonnull(g, LLVMBuildIntToPtr(g->builder,
        LLVMBuildLoad2(g->builder, i64, slot2, "svi2"), i8ptr, "sv2"));
    LLVMValueRef sl2 = zan_call2(g->builder, strlen_ty, g->fn_strlen, &sv2, 1, "sl2");
    LLVMValueRef pos = LLVMBuildLoad2(g->builder, i64, pos_a, "pos");
    LLVMValueRef dst = LLVMBuildGEP2(g->builder, i8, buf, &pos, 1, "dst");
    zan_call2(g->builder, memcpy_ty, memcpy_fn,
        (LLVMValueRef[]){ dst, sv2, sl2 }, 3, "");
    LLVMValueRef pos2 = zan_add(g->builder, pos, sl2, "pos2");
    LLVMBuildStore(g->builder, pos2, pos_a);
    LLVMValueRef last = zan_icmp(g->builder, LLVMIntSGE,
        zan_add(g->builder, ci, LLVMConstInt(i64, 1, 0), "ci1"), n, "last");
    LLVMBuildCondBr(g->builder, last, c_next, c_sep);
    LLVMPositionBuilderAtEnd(g->builder, c_sep);
    LLVMValueRef dst2 = LLVMBuildGEP2(g->builder, i8, buf, &pos2, 1, "dst2");
    zan_call2(g->builder, memcpy_ty, memcpy_fn,
        (LLVMValueRef[]){ dst2, sep, seplen }, 3, "");
    LLVMBuildStore(g->builder,
        zan_add(g->builder, pos2, seplen, "pos3"), pos_a);
    LLVMBuildBr(g->builder, c_next);
    LLVMPositionBuilderAtEnd(g->builder, c_next);
    LLVMBuildStore(g->builder,
        zan_add(g->builder, ci, LLVMConstInt(i64, 1, 0), "ni"), idx_a);
    LLVMBuildBr(g->builder, c_cond);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMValueRef endpos = LLVMBuildLoad2(g->builder, i64, pos_a, "endpos");
    LLVMValueRef endptr = LLVMBuildGEP2(g->builder, i8, buf, &endpos, 1, "endptr");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endptr);
    LLVMBuildRet(g->builder, buf);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* Coerce a Dict key value to the i8* slot representation (scalar keys are
 * stored inttoptr'd; string keys are already pointers). */
static LLVMValueRef coerce_dict_key(zan_irgen_t *g, LLVMValueRef key,
                                    zan_type_t *kt) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (LLVMGetTypeKind(LLVMTypeOf(key)) == LLVMIntegerTypeKind) {
        /* Normalize to the declared key width (Dict<int,...> keys are i32)
         * before sign-extending, so a literal and a same-valued variable map
         * to one raw key. */
        LLVMTypeRef kem = kt ? map_type(g, kt) : NULL;
        if (kem && LLVMGetTypeKind(kem) == LLVMIntegerTypeKind &&
            LLVMGetIntTypeWidth(kem) < LLVMGetIntTypeWidth(LLVMTypeOf(key)))
            key = LLVMBuildTrunc(g->builder, key, kem, "k.nw");
        if (LLVMGetIntTypeWidth(LLVMTypeOf(key)) < 64)
            key = LLVMBuildSExt(g->builder, key, i64, "k.sx");
        return LLVMBuildIntToPtr(g->builder, key, i8ptr, "k.ip");
    }
    if (LLVMTypeOf(key) != i8ptr)
        return LLVMBuildBitCast(g->builder, key, i8ptr, "k.bc");
    return key;
}

/* Emit `keys[i] == search` for a Dict scan via __zan_dict_key_eq. */
static LLVMValueRef emit_dict_key_eq(zan_irgen_t *g, zan_type_t *dict_type,
                                     LLVMValueRef kv, LLVMValueRef search) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    zan_type_t *kt = dict_key_type(g, dict_type);
    LLVMValueRef is_str = LLVMConstInt(i64,
        (!kt || kt->kind == TYPE_STRING) ? 1 : 0, 0);
    LLVMValueRef keq = get_dict_key_eq_fn(g);
    return zan_call2(g->builder, LLVMGlobalGetValueType(keq), keq,
        (LLVMValueRef[]){ kv, search, is_str }, 3, "keq");
}

/* Emit `__zan_dict_find(dict, key, is_str)` for a dict of type `dict_type`. */
static LLVMValueRef emit_dict_find(zan_irgen_t *g, zan_type_t *dict_type,
                                   LLVMValueRef draw, LLVMValueRef key) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    zan_type_t *kt = dict_key_type(g, dict_type);
    LLVMValueRef is_str = LLVMConstInt(i64, (!kt || kt->kind == TYPE_STRING) ? 1 : 0, 0);
    LLVMValueRef fn = get_dict_find_fn(g);
    if (LLVMTypeOf(draw) != i8ptr)
        draw = LLVMBuildBitCast(g->builder, draw, i8ptr, "d8");
    return zan_call2(g->builder, LLVMGlobalGetValueType(fn), fn,
        (LLVMValueRef[]){ draw, key, is_str }, 3, "dfind");
}

/* Backing LLVM global for a static class/struct field `Class.field`.
 * Static fields are shared mutable storage (not compile-time constants):
 * they are lowered to an internal, zero-initialised module global. The
 * field's declared initializer is applied once at main() entry as a runtime
 * store (see emit_main_method), so any initializer expression works with
 * correct ordering. Returns NULL when `fsym` is not a static field. */
/* The instantiation a static access names, if any: `Stat<int>.s` parses as an
 * identifier carrying `inst_type_ref`. Inside a specialized body the reference
 * is still written in terms of the type parameters (`Stat<T>.s`), so it is
 * substituted through the active instantiation. */
static zan_type_t *static_access_inst(zan_irgen_t *g, zan_ast_node_t *obj_expr) {
    if (!obj_expr || !obj_expr->inst_type_ref) return NULL;
    zan_type_t *t = zan_binder_resolve_type(g->binder, obj_expr->inst_type_ref);
    if (t && g->cur_inst) t = subst_type_param_deep(g, t, g->cur_inst);
    return (t && t->type_arg_count > 0) ? t : NULL;
}

static LLVMValueRef get_static_field_global(zan_irgen_t *g, zan_symbol_t *class_sym,
                                            zan_symbol_t *fsym,
                                            zan_type_t *inst) {
    /* plain fields and automatic properties (`{ get; set; }`) share a backing
     * global; custom-accessor properties are dispatched to their getter/setter
     * methods and never reach here */
    if (!class_sym || !fsym || !fsym->decl ||
        (fsym->decl->kind != AST_FIELD_DECL &&
         fsym->decl->kind != AST_PROPERTY_DECL))
        return NULL;
    if (!((fsym->modifiers & MOD_STATIC) ||
          (fsym->decl->field_decl.modifiers & MOD_STATIC)))
        return NULL;
    /* A static of a generic class is per closed instantiation (C# rules):
     * Stat<int>.s and Stat<string>.s are separate storage, so the backing
     * global carries the instantiation suffix. A single unsuffixed global made
     * every instantiation share one slot. */
    if (!inst && g->cur_inst && g->cur_inst->sym == class_sym) inst = g->cur_inst;
    char suffix[256];
    suffix[0] = '\0';
    if (inst && inst->sym == class_sym && inst->type_arg_count > 0)
        mangle_inst_suffix(suffix, sizeof(suffix), inst);
    char name[512];
    snprintf(name, sizeof(name), "__zan_sf_%.*s_%.*s%s",
             (int)class_sym->name.len, class_sym->name.str,
             (int)fsym->name.len, fsym->name.str, suffix);
    LLVMValueRef gv = LLVMGetNamedGlobal(g->mod, name);
    if (gv) return gv;
    LLVMTypeRef ft = fsym->type ? map_type(g, fsym->type)
                                : LLVMInt64TypeInContext(g->ctx);
    gv = LLVMAddGlobal(g->mod, ft, name);
    LLVMSetLinkage(gv, LLVMInternalLinkage);
    LLVMSetInitializer(gv, LLVMConstNull(ft));
    /* Register rc-managed static fields for program-exit cleanup: the
     * main-unit sweep below can only see the unit containing main(), but
     * stdlib singletons (Pinyin.cache, ...) live in other units. */
    if (fsym->type && is_rc_managed_type(fsym->type)) {
        if (g->static_field_count >= g->static_field_cap) {
            g->static_field_cap = g->static_field_cap ? g->static_field_cap * 2 : 16;
            g->static_fields = (struct zan_static_field_ref *)realloc(
                g->static_fields,
                (size_t)g->static_field_cap * sizeof(*g->static_fields));
        }
        g->static_fields[g->static_field_count].type = fsym->type;
        g->static_fields[g->static_field_count].gv = gv;
        g->static_field_count++;
    }
    return gv;
}

static int is_stable_field_base(zan_ast_node_t *expr) {
    if (!expr) return 0;
    if (expr->kind == AST_IDENTIFIER || expr->kind == AST_THIS_EXPR ||
        expr->kind == AST_BASE_EXPR) return 1;
    return expr->kind == AST_MEMBER_ACCESS &&
           is_stable_field_base(expr->member.object);
}

static void emit_invalidate_freed_string(zan_irgen_t *g, zan_ast_node_t *arg,
                                         local_scope_t *locals) {
    if (!arg || !locals) return;
    if (arg->kind == AST_IDENTIFIER) {
        local_var_t *local = local_find(locals, arg->ident.name);
        if (local && local->type && local->type->kind == TYPE_STRING) {
            LLVMTypeRef slot_type = local_slot_type(g, local);
            if (LLVMGetTypeKind(slot_type) == LLVMPointerTypeKind) {
                LLVMBuildStore(g->builder, LLVMConstNull(slot_type), local->alloca);
            }
            return;
        }
        if (g->current_type_sym) {
            zan_symbol_t *field = get_field_sym(g->current_type_sym, arg->ident.name);
            if (!field || !field->type || field->type->kind != TYPE_STRING) return;
            LLVMValueRef global = get_static_field_global(g, g->current_type_sym, field, NULL);
            LLVMTypeRef field_type = map_type(g, field->type);
            if (global) {
                LLVMBuildStore(g->builder, LLVMConstNull(field_type), global);
            } else if (g->current_this) {
                int fi = get_field_index(g->current_type_sym, arg->ident.name);
                LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                if (fi >= 0 && st) {
                    LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(st, 0), g->current_this, "this");
                    LLVMValueRef field_ptr = emit_field_ptr(g, g->current_type_sym, st,
                        this_ptr, fi, "freed.fld");
                    LLVMBuildStore(g->builder, LLVMConstNull(field_type), field_ptr);
                }
            }
        }
        return;
    }
    if (arg->kind != AST_MEMBER_ACCESS) return;
    zan_type_t *field_type = infer_expr_type(g, arg, locals);
    if (!field_type || field_type->kind != TYPE_STRING) return;
    zan_ast_node_t *obj = arg->member.object;
    zan_symbol_t *class_sym = NULL;
    LLVMValueRef object_ptr = NULL;
    if (obj->kind == AST_IDENTIFIER) {
        local_var_t *local = local_find(locals, obj->ident.name);
        if (local && local->type && local->type->sym) {
            class_sym = local->type->sym;
            LLVMTypeRef st = get_struct_llvm_type(g, class_sym);
            if (st) object_ptr = struct_base_ptr(g, local, st);
        } else {
            class_sym = zan_binder_lookup(g->binder, obj->ident.name);
            if (class_sym && (class_sym->kind == SYM_CLASS ||
                              class_sym->kind == SYM_STRUCT)) {
                zan_symbol_t *field = get_field_sym(class_sym, arg->member.name);
                LLVMValueRef global = get_static_field_global(g, class_sym, field,
                    static_access_inst(g, obj));
                if (global) {
                    LLVMBuildStore(g->builder,
                        LLVMConstNull(map_type(g, field_type)), global);
                    return;
                }
            }
            class_sym = NULL;
        }
    } else if ((obj->kind == AST_THIS_EXPR || obj->kind == AST_BASE_EXPR) &&
               g->current_type_sym && g->current_this) {
        class_sym = g->current_type_sym;
        LLVMTypeRef st = get_struct_llvm_type(g, class_sym);
        if (st) {
            object_ptr = LLVMBuildLoad2(g->builder, LLVMPointerType(st, 0),
                                       g->current_this, "this");
        }
    } else if (is_stable_field_base(obj)) {
        class_sym = expr_class_sym(g, obj, locals);
        if (class_sym) object_ptr = emit_expr(g, obj, locals);
    }
    if (!class_sym || !object_ptr ||
        LLVMGetTypeKind(LLVMTypeOf(object_ptr)) != LLVMPointerTypeKind) return;
    int fi = get_field_index(class_sym, arg->member.name);
    LLVMTypeRef st = get_struct_llvm_type(g, class_sym);
    if (fi < 0 || !st) return;
    LLVMValueRef field_ptr = emit_field_ptr(g, class_sym, st, object_ptr, fi, "freed.fld");
    LLVMBuildStore(g->builder, LLVMConstNull(map_type(g, field_type)), field_ptr);
}

/* Null-conditional access `a?.b` / `a?.M(...)`: evaluate the receiver once,
 * bind it to a synthetic local, and only evaluate the member/call when it is
 * non-null; a null receiver yields the result type's zero value. The member
 * node is temporarily rewired to read the synthetic local so the ordinary
 * member/call codegen paths apply unchanged, then restored (the same AST may
 * be re-emitted, e.g. per generic instantiation). A value-typed result is
 * wrapped as `T?`, as in C#: a value type has no null of its own, so handing
 * back its zero would make the null receiver indistinguishable from a member
 * that really holds zero. */
static LLVMValueRef emit_null_cond(zan_irgen_t *g, zan_ast_node_t *expr,
                                   zan_ast_node_t *qmem, local_scope_t *locals) {
    LLVMValueRef obj = emit_expr(g, qmem->member.object, locals);
    if (LLVMGetTypeKind(LLVMTypeOf(obj)) != LLVMPointerTypeKind) {
        /* value receiver can never be null: plain access */
        qmem->member.null_cond = 0;
        LLVMValueRef v = emit_expr(g, expr, locals);
        qmem->member.null_cond = 1;
        return v;
    }
    zan_type_t *oty = infer_expr_type(g, qmem->member.object, locals);
    char nm[32];
    snprintf(nm, sizeof(nm), "__qdot%d", g->qdot_counter++);
    zan_istr_t iname = { zan_arena_strdup(g->arena, nm, strlen(nm)),
                           (int)strlen(nm) };
    LLVMValueRef slot = emit_entry_alloca(g, LLVMTypeOf(obj), nm);
    LLVMBuildStore(g->builder, obj, slot);
    local_add(locals, iname, slot, oty);

    zan_ast_node_t *ident = zan_ast_new(g->arena, AST_IDENTIFIER, qmem->loc);
    ident->ident.name = iname;
    zan_ast_node_t *saved_obj = qmem->member.object;
    qmem->member.object = ident;
    qmem->member.null_cond = 0;

    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "qdot.then");
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "qdot.end");
    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, obj,
        LLVMConstNull(LLVMTypeOf(obj)), "qdot.isnull");
    LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->builder);
    LLVMBuildCondBr(g->builder, isnull, merge_bb, then_bb);

    LLVMPositionBuilderAtEnd(g->builder, then_bb);
    LLVMValueRef v = emit_expr(g, expr, locals);
    LLVMTypeRef rt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(rt) != LLVMVoidTypeKind &&
        LLVMGetTypeKind(rt) != LLVMPointerTypeKind && !llvm_is_nullable(rt)) {
        LLVMValueRef some = nullable_some(g, nullable_type_of(g, rt), v);
        if (some) v = some;
    }
    LLVMBasicBlockRef then_end = LLVMGetInsertBlock(g->builder);
    LLVMBuildBr(g->builder, merge_bb);

    qmem->member.object = saved_obj;
    qmem->member.null_cond = 1;

    LLVMPositionBuilderAtEnd(g->builder, merge_bb);
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(vt) == LLVMVoidTypeKind) {
        emit_release_owned_call_temp(g, saved_obj, obj, locals);
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    }
    LLVMValueRef phi = LLVMBuildPhi(g->builder, vt, "qdot");
    LLVMValueRef dflt = LLVMConstNull(vt);
    LLVMValueRef vals[] = { dflt, v };
    LLVMBasicBlockRef bbs[] = { entry_bb, then_end };
    LLVMAddIncoming(phi, vals, bbs, 2);
    /* an owned receiver temp (e.g. `M()?.x`, where M returns +1) is consumed
     * by this access; the class release helper is null-safe */
    emit_release_owned_call_temp(g, saved_obj, obj, locals);
    return phi;
}

/* `params T[] rest` call-site packing. The trailing arguments are bundled into
 * a synthetic `new List<T>{ ... }` node (the parameter itself was lowered to
 * List<T> at parse time), so the callee sees a normal List. Passing a List
 * (or an explicit `new List<T>{...}`) as the sole trailing argument passes it
 * through unpacked. The call node is mutated in place; the rewrite is
 * idempotent so re-emission (e.g. per generic instantiation) is safe. */
/* Default parameter values: `F(int a, int b = 2)` invoked as `F(1)` has the
 * declared default expression appended at the call site, which is where C#
 * evaluates it. Without this the call was emitted with fewer arguments than the
 * callee declares and failed LLVM verification ("Incorrect number of arguments
 * passed"). The call node is mutated in place, and the rewrite is idempotent:
 * once the argument list is full there is nothing left to append. A trailing
 * `params` tail and operator methods (whose declared list carries an injected
 * receiver) are left to pack_params_args. */
/* Reorder named call arguments (`F(b: 2, x: 10)`) to the callee's parameter
 * order once the method symbol is known. Positional arguments fill the
 * remaining slots left to right, mirroring C#: a named argument binds to the
 * parameter of the same name, and the position of a named argument is
 * irrelevant. Unknown names and duplicates are diagnosed. */
static void reorder_named_args_impl(zan_irgen_t *g, zan_ast_list_t *args,
                                    zan_loc_t loc, zan_ast_node_t *decl) {
    if (!args || !decl) return;
    if (decl->kind != AST_METHOD_DECL &&
        decl->kind != AST_CONSTRUCTOR_DECL) return;
    int n = args->count;
    int has_named = 0;
    for (int i = 0; i < n; i++) {
        if (args->items[i] &&
            args->items[i]->kind == AST_NAMED_ARG) { has_named = 1; break; }
    }
    if (!has_named) return;

    zan_ast_list_t *ps = &decl->method_decl.params;
    int m = ps->count;
    /* slot[i] = call-arg index placed at AST-parameter position i, or -1. */
    int *slot = (int *)malloc(sizeof(int) * (size_t)(m > n ? m : n));
    for (int i = 0; i < (m > n ? m : n); i++) slot[i] = -1;
    int *used_name = (int *)malloc(sizeof(int) * (size_t)n);
    for (int i = 0; i < n; i++) used_name[i] = 0;

    /* First pass: bind named arguments by parameter name. */
    for (int i = 0; i < n; i++) {
        zan_ast_node_t *arg = args->items[i];
        if (!arg || arg->kind != AST_NAMED_ARG) continue;
        int found = -1;
        for (int j = 0; j < m; j++) {
            zan_ast_node_t *p = ps->items[j];
            if (!p || p->kind != AST_PARAM) continue;
            if (p->param.is_this) continue; /* extension receiver: not nameable */
            if (p->param.name.len == arg->named_arg.name.len &&
                memcmp(p->param.name.str, arg->named_arg.name.str,
                       (size_t)p->param.name.len) == 0) {
                found = j;
                break;
            }
        }
        if (found < 0) {
            zan_diag_emit(g->diag, DIAG_ERROR, arg->loc,
                          "no parameter named '%.*s'",
                          (int)arg->named_arg.name.len,
                          arg->named_arg.name.str);
        } else if (slot[found] >= 0) {
            zan_diag_emit(g->diag, DIAG_ERROR, arg->loc,
                          "parameter '%.*s' is already assigned",
                          (int)arg->named_arg.name.len,
                          arg->named_arg.name.str);
        } else {
            slot[found] = i;
            used_name[i] = 1;
        }
    }

    /* Second pass: positional arguments fill the leftmost free slots. */
    int j = 0;
    for (int i = 0; i < n; i++) {
        if (used_name[i]) continue;
        while (j < m && (slot[j] >= 0 || ps->items[j]->param.is_this)) j++;
        if (j >= m) break;
        slot[j] = i;
        j++;
    }

    /* Rebuild: each named arg contributes its expression; positional args
     * move to their slot. The result list keeps the original length.
     * The rewrite is only meaningful when the bound parameter slots are
     * exactly 0..n-1 (the dense positional prefix downstream expects); any
     * other shape -- an unknown or duplicate name diagnosed above, or a
     * skipped defaulted parameter ahead of a bound one -- used to punch
     * NULL holes into the AST here and crash later phases. Restore the
     * original argument order instead and let the diagnostics stand. */
    zan_ast_node_t **reb = (zan_ast_node_t **)malloc(sizeof(zan_ast_node_t *) *
                                                     (size_t)n);
    if (!reb) { free(slot); free(used_name); return; }
    int ok = 1;
    for (int p = 0; p < n; p++) {
        if (slot[p] < 0 || slot[p] >= n) {
            ok = 0;
            zan_diag_emit(g->diag, DIAG_ERROR, loc,
                          "named arguments must bind to the leading "
                          "parameters in order");
            break;
        }
        zan_ast_node_t *arg = args->items[slot[p]];
        if (arg && arg->kind == AST_NAMED_ARG) arg = arg->named_arg.expr;
        reb[p] = arg;
    }
    if (ok)
        for (int i = 0; i < n; i++)
            args->items[i] = reb[i];
    free(reb);
    free(slot);
    free(used_name);
}

static void reorder_named_args(zan_irgen_t *g, zan_ast_node_t *call,
                               zan_symbol_t *method_sym) {
    if (!call || call->kind != AST_CALL) return;
    if (!method_sym || !method_sym->decl) return;
    reorder_named_args_impl(g, &call->call.args, call->loc,
                            method_sym->decl);
}

static void fill_default_args(zan_irgen_t *g, zan_ast_node_t *call,
                              zan_symbol_t *method_sym) {
    if (!call || call->kind != AST_CALL) return;
    if (!method_sym || !method_sym->decl) return;
    if (method_sym->decl->kind != AST_METHOD_DECL &&
        method_sym->decl->kind != AST_CONSTRUCTOR_DECL) return;
    reorder_named_args(g, call, method_sym);
    if (method_is_params_variadic(method_sym)) return;
    if (method_sym->name.len > 3 &&
        memcmp(method_sym->name.str, "op_", 3) == 0) return;
    zan_ast_list_t *ps = &method_sym->decl->method_decl.params;
    for (int i = call->call.args.count; i < ps->count; i++) {
        zan_ast_node_t *p = ps->items[i];
        if (!p || p->kind != AST_PARAM || !p->param.default_val) return;
        zan_ast_list_push(&call->call.args, p->param.default_val, g->arena);
    }
}

static void pack_params_args(zan_irgen_t *g, zan_ast_node_t *call,
                             zan_symbol_t *method_sym, local_scope_t *locals) {
    if (!method_sym || !method_sym->decl ||
        method_sym->decl->kind != AST_METHOD_DECL) return;
    if (!method_is_params_variadic(method_sym)) return;
    zan_ast_list_t *ps = &method_sym->decl->method_decl.params;
    /* A static op_call operator declares an injected receiver (`self`) which
     * does not appear in the source call's argument list; an instance
     * operator's receiver is `this` and its params start at the real ones. */
    int injected = method_sym->name.len == 7 &&
        memcmp(method_sym->name.str, "op_call", 7) == 0 &&
        (method_sym->modifiers & MOD_STATIC) != 0;
    int visible = ps->count - injected;
    int fixed = visible - 1;
    int argc = call->call.args.count;
    if (argc < fixed) return;
    if (argc == visible) {
        /* A single trailing argument that is already the bundle passes through
         * untouched, as in C#: `Sum(arr)` hands `arr` over instead of wrapping
         * it in a one-element array. */
        zan_ast_node_t *la = call->call.args.items[argc - 1];
        if (la->kind == AST_NEW_EXPR) return; /* already packed / explicit array */
        zan_type_t *lt = infer_expr_type(g, la, locals);
        if (lt && lt->kind == TYPE_ARRAY) return;
    }
    zan_ast_node_t *last_p = ps->items[ps->count - 1];
    zan_ast_node_t *arr = zan_ast_new(g->arena, AST_NEW_EXPR, call->loc);
    arr->new_expr.type = last_p->param.type; /* T[] */
    arr->new_expr.is_array = true;
    arr->new_expr.array_init = true; /* args are the elements */
    zan_ast_list_init(&arr->new_expr.args);
    for (int i = fixed; i < argc; i++)
        zan_ast_list_push(&arr->new_expr.args, call->call.args.items[i], g->arena);
    call->call.args.count = fixed;
    zan_ast_list_push(&call->call.args, arr, g->arena);
}

static LLVMValueRef emit_ref_arg(zan_irgen_t *g, zan_ast_node_t *arg,
                                 local_scope_t *locals);
