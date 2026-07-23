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
    LLVMValueRef sub_empty = LLVMBuildICmp(g->builder, LLVMIntEQ, sub0,
        LLVMConstInt(i8, 0, 0), "subempty");
    LLVMBuildCondBr(g->builder, sub_empty, empty_bb, loop);
    LLVMPositionBuilderAtEnd(g->builder, empty_bb);
    LLVMValueRef slen = zan_call2(g->builder, strlen_ty, g->fn_strlen, &s, 1, "slen");
    LLVMBuildRet(g->builder, slen);
    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef p = LLVMBuildLoad2(g->builder, i8ptr, cur, "p");
    LLVMValueRef ss_args[] = { p, sub };
    LLVMValueRef hit = zan_call2(g->builder, strstr_ty, strstr_fn, ss_args, 2, "hit");
    LLVMValueRef miss = LLVMBuildICmp(g->builder, LLVMIntEQ, hit,
        LLVMConstNull(i8ptr), "miss");
    LLVMBuildCondBr(g->builder, miss, done, found);
    LLVMPositionBuilderAtEnd(g->builder, found);
    LLVMValueRef idx = LLVMBuildSub(g->builder,
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
    LLVMValueRef nz = LLVMBuildICmp(g->builder, LLVMIntNE, ws,
        LLVMConstInt(i32, 0, 0), "isws");
    LLVMValueRef not_nul = LLVMBuildICmp(g->builder, LLVMIntNE, c,
        LLVMConstInt(i8, 0, 0), "notnul");
    LLVMValueRef adv = LLVMBuildAnd(g->builder, nz, not_nul, "adv");
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
    LLVMValueRef npos = LLVMBuildICmp(g->builder, LLVMIntSGT, n,
        LLVMConstInt(i64, 0, 0), "npos");
    LLVMBuildCondBr(g->builder, npos, tail_chk, copy);
    LLVMPositionBuilderAtEnd(g->builder, tail_chk);
    LLVMValueRef nm1 = LLVMBuildSub(g->builder, n, one64, "nm1");
    LLVMValueRef lastp = LLVMBuildGEP2(g->builder, i8, base, &nm1, 1, "lastp");
    LLVMValueRef lc = LLVMBuildLoad2(g->builder, i8, lastp, "lc");
    LLVMValueRef lc32 = LLVMBuildZExt(g->builder, lc, i32, "lc32");
    LLVMValueRef lws = zan_call2(g->builder, isspace_ty, isspace_fn, &lc32, 1, "lws");
    LLVMValueRef lnz = LLVMBuildICmp(g->builder, LLVMIntNE, lws,
        LLVMConstInt(i32, 0, 0), "lisws");
    LLVMBuildCondBr(g->builder, lnz, tail_ws, copy);
    LLVMPositionBuilderAtEnd(g->builder, tail_ws);
    LLVMBuildStore(g->builder, nm1, nvar);
    LLVMBuildBr(g->builder, tail);
    LLVMPositionBuilderAtEnd(g->builder, copy);
    LLVMValueRef fin_n = LLVMBuildLoad2(g->builder, i64, nvar, "finn");
    LLVMValueRef bufsz = LLVMBuildAdd(g->builder, fin_n, one64, "bsz");
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
    LLVMValueRef bufsz = LLVMBuildAdd(g->builder, n, LLVMConstInt(i64, 1, 0), "bsz");
    LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
    LLVMValueRef ivar = LLVMBuildAlloca(g->builder, i64, "ivar");
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), ivar);
    LLVMBuildBr(g->builder, loop);
    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef i = LLVMBuildLoad2(g->builder, i64, ivar, "i");
    LLVMValueRef more = LLVMBuildICmp(g->builder, LLVMIntULT, i, n, "more");
    LLVMBuildCondBr(g->builder, more, body, done);
    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef sp = LLVMBuildGEP2(g->builder, i8, s, &i, 1, "sp");
    LLVMValueRef c = LLVMBuildLoad2(g->builder, i8, sp, "c");
    LLVMValueRef c32 = LLVMBuildZExt(g->builder, c, i32, "c32");
    LLVMValueRef conv = zan_call2(g->builder, conv_ty, conv_fn, &c32, 1, "conv");
    LLVMValueRef c8 = LLVMBuildTrunc(g->builder, conv, i8, "c8");
    LLVMValueRef dp = LLVMBuildGEP2(g->builder, i8, buf, &i, 1, "dp");
    LLVMBuildStore(g->builder, c8, dp);
    LLVMValueRef i1v = LLVMBuildAdd(g->builder, i, LLVMConstInt(i64, 1, 0), "i1");
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
    LLVMValueRef from_empty = LLVMBuildICmp(g->builder, LLVMIntEQ, lf,
        LLVMConstInt(i64, 0, 0), "fempty");
    LLVMBuildCondBr(g->builder, from_empty, dup_bb, cnt_loop);
    LLVMPositionBuilderAtEnd(g->builder, dup_bb);
    LLVMValueRef dupsz = LLVMBuildAdd(g->builder, ls, one64, "dupsz");
    LLVMValueRef dupbuf = emit_string_alloc_rc(g, dupsz);
    LLVMValueRef dupargs[] = { dupbuf, s, dupsz };
    zan_call2(g->builder, memcpy_ty, memcpy_fn, dupargs, 3, "");
    LLVMBuildRet(g->builder, dupbuf);
    LLVMPositionBuilderAtEnd(g->builder, cnt_loop);
    LLVMValueRef cp = LLVMBuildLoad2(g->builder, i8ptr, pvar, "cp");
    LLVMValueRef cargs[] = { cp, from };
    LLVMValueRef chit = zan_call2(g->builder, strstr_ty, strstr_fn, cargs, 2, "chit");
    LLVMValueRef cmiss = LLVMBuildICmp(g->builder, LLVMIntEQ, chit,
        LLVMConstNull(i8ptr), "cmiss");
    LLVMBuildCondBr(g->builder, cmiss, alloc_bb, cnt_hit);
    LLVMPositionBuilderAtEnd(g->builder, cnt_hit);
    LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntv, "cnt");
    LLVMBuildStore(g->builder, LLVMBuildAdd(g->builder, cnt, one64, "cnt1"), cntv);
    LLVMValueRef cnext = LLVMBuildGEP2(g->builder, i8, chit, &lf, 1, "cnext");
    LLVMBuildStore(g->builder, cnext, pvar);
    LLVMBuildBr(g->builder, cnt_loop);
    LLVMPositionBuilderAtEnd(g->builder, alloc_bb);
    LLVMValueRef total_cnt = LLVMBuildLoad2(g->builder, i64, cntv, "tcnt");
    LLVMValueRef delta = LLVMBuildSub(g->builder, lt, lf, "delta");
    LLVMValueRef grow = LLVMBuildMul(g->builder, total_cnt, delta, "grow");
    LLVMValueRef newlen = LLVMBuildAdd(g->builder, ls, grow, "newlen");
    LLVMValueRef bufsz = LLVMBuildAdd(g->builder, newlen, one64, "bsz");
    LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
    LLVMBuildStore(g->builder, s, pvar);
    LLVMBuildStore(g->builder, buf, dvar);
    LLVMBuildBr(g->builder, bld_loop);
    LLVMPositionBuilderAtEnd(g->builder, bld_loop);
    LLVMValueRef bp = LLVMBuildLoad2(g->builder, i8ptr, pvar, "bp");
    LLVMValueRef bargs[] = { bp, from };
    LLVMValueRef bhit = zan_call2(g->builder, strstr_ty, strstr_fn, bargs, 2, "bhit");
    LLVMValueRef bmiss = LLVMBuildICmp(g->builder, LLVMIntEQ, bhit,
        LLVMConstNull(i8ptr), "bmiss");
    LLVMBuildCondBr(g->builder, bmiss, tail_bb, bld_hit);
    LLVMPositionBuilderAtEnd(g->builder, bld_hit);
    LLVMValueRef d = LLVMBuildLoad2(g->builder, i8ptr, dvar, "d");
    LLVMValueRef pre = LLVMBuildSub(g->builder,
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
    LLVMValueRef restsz = LLVMBuildAdd(g->builder, rest, one64, "restsz");
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
    LLVMValueRef ssz = LLVMBuildAdd(g->builder, n, one64, "ssz");
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
    LLVMValueRef full = LLVMBuildICmp(g->builder, LLVMIntSGE, count, cap, "full");
    LLVMBuildCondBr(g->builder, full, grow_bb, push_bb);
    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
    LLVMValueRef cap0 = LLVMBuildICmp(g->builder, LLVMIntEQ, cap,
        LLVMConstInt(i64, 0, 0), "cap0");
    LLVMValueRef ncap = LLVMBuildSelect(g->builder, cap0,
        LLVMConstInt(i64, 4, 0),
        LLVMBuildMul(g->builder, cap, LLVMConstInt(i64, 2, 0), "cap2"), "ncap");
    LLVMValueRef datap = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "datap");
    LLVMValueRef odata = LLVMBuildLoad2(g->builder, i64ptr, datap, "odata");
    LLVMValueRef odata8 = LLVMBuildBitCast(g->builder, odata, i8ptr, "odata8");
    LLVMValueRef nbytes = LLVMBuildMul(g->builder, ncap, LLVMConstInt(i64, 8, 0), "nbytes");
    LLVMValueRef ndata8 = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
        g->fn_realloc, (LLVMValueRef[]){ odata8, nbytes }, 2, "ndata8");
    LLVMValueRef ndata = LLVMBuildBitCast(g->builder, ndata8, i64ptr, "ndata");
    LLVMBuildStore(g->builder, ndata, datap);
    LLVMBuildStore(g->builder, ncap, capp);
    LLVMBuildBr(g->builder, push_bb);
    LLVMPositionBuilderAtEnd(g->builder, push_bb);
    LLVMValueRef datap2 = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "datap2");
    LLVMValueRef data = LLVMBuildLoad2(g->builder, i64ptr, datap2, "data");
    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &count, 1, "slot");
    LLVMBuildStore(g->builder, LLVMBuildPtrToInt(g->builder, str, i64, "sv"), slot);
    LLVMBuildStore(g->builder, LLVMBuildAdd(g->builder, count, one64, "ncnt"), cntp);
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
    LLVMValueRef sep_empty = LLVMBuildICmp(g->builder, LLVMIntEQ, seplen,
        LLVMConstInt(i64, 0, 0), "sempty");
    LLVMBuildCondBr(g->builder, sep_empty, last_bb, loop_bb);
    LLVMPositionBuilderAtEnd(g->builder, loop_bb);
    LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr, pvar, "cur");
    LLVMValueRef hit = zan_call2(g->builder, strstr_ty, strstr_fn,
        (LLVMValueRef[]){ cur, sep }, 2, "hit");
    LLVMValueRef miss = LLVMBuildICmp(g->builder, LLVMIntEQ, hit,
        LLVMConstNull(i8ptr), "miss");
    LLVMBuildCondBr(g->builder, miss, last_bb, hit_bb);
    LLVMPositionBuilderAtEnd(g->builder, hit_bb);
    LLVMValueRef seg = LLVMBuildSub(g->builder,
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
    LLVMValueRef isv = LLVMBuildICmp(g->builder, LLVMIntNE, is_str,
        LLVMConstInt(i64, 0, 0), "isv");
    LLVMBuildCondBr(g->builder, isv, str_bb, raw_bb);
    LLVMPositionBuilderAtEnd(g->builder, str_bb);
    LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty, strcmp_fn,
        (LLVMValueRef[]){ a, b2 }, 2, "cmp");
    LLVMBuildRet(g->builder, LLVMBuildICmp(g->builder, LLVMIntEQ, cmp,
        LLVMConstInt(i32t, 0, 0), "eq"));
    LLVMPositionBuilderAtEnd(g->builder, raw_bb);
    LLVMBuildRet(g->builder, LLVMBuildICmp(g->builder, LLVMIntEQ, a, b2, "peq"));
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
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
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64, i64 }, 4, 0);
    fn = LLVMAddFunction(g->mod, "__zan_dict_set", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMValueRef keq = get_dict_key_eq_fn(g);
    LLVMTypeRef keq_ty = LLVMGlobalGetValueType(keq);
    LLVMTypeRef realloc_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef hit_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "hit");
    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
    LLVMBasicBlockRef app_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "app");
    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "grow");
    LLVMBasicBlockRef put_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "put");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef draw = LLVMGetParam(fn, 0);
    LLVMValueRef key = LLVMGetParam(fn, 1);
    LLVMValueRef val = LLVMGetParam(fn, 2);
    LLVMValueRef is_str = LLVMGetParam(fn, 3);
    LLVMValueRef dp = LLVMBuildBitCast(g->builder, draw,
        LLVMPointerType(g->dict_struct_type, 0), "dp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
    LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
    LLVMValueRef capp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 1, "capp");
    LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
    LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
    LLVMValueRef ks = LLVMBuildLoad2(g->builder, i8pp, kp, "ks");
    LLVMValueRef vs = LLVMBuildLoad2(g->builder, i64ptr, vp, "vs");
    LLVMValueRef idx_a = LLVMBuildAlloca(g->builder, i64, "di");
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
    LLVMBuildBr(g->builder, cond_bb);
    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
    LLVMValueRef done = LLVMBuildICmp(g->builder, LLVMIntUGE, ci, cnt, "done");
    LLVMBuildCondBr(g->builder, done, app_bb, body_bb);
    LLVMPositionBuilderAtEnd(g->builder, body_bb);
    LLVMValueRef kslot = LLVMBuildGEP2(g->builder, i8ptr, ks, &ci, 1, "ksl");
    LLVMValueRef kv = LLVMBuildLoad2(g->builder, i8ptr, kslot, "kv");
    LLVMValueRef eq = zan_call2(g->builder, keq_ty, keq,
        (LLVMValueRef[]){ kv, key, is_str }, 3, "eq");
    LLVMBuildCondBr(g->builder, eq, hit_bb, next_bb);
    LLVMPositionBuilderAtEnd(g->builder, hit_bb);
    LLVMValueRef vslot = LLVMBuildGEP2(g->builder, i64, vs, &ci, 1, "vsl");
    LLVMBuildStore(g->builder, val, vslot);
    LLVMBuildRetVoid(g->builder);
    LLVMPositionBuilderAtEnd(g->builder, next_bb);
    LLVMBuildStore(g->builder,
        LLVMBuildAdd(g->builder, ci, LLVMConstInt(i64, 1, 0), "ni"), idx_a);
    LLVMBuildBr(g->builder, cond_bb);
    /* append: grow when full */
    LLVMPositionBuilderAtEnd(g->builder, app_bb);
    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, capp, "cap");
    LLVMValueRef full = LLVMBuildICmp(g->builder, LLVMIntSGE, cnt, cap, "full");
    LLVMBuildCondBr(g->builder, full, grow_bb, put_bb);
    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
    LLVMValueRef cap0 = LLVMBuildICmp(g->builder, LLVMIntEQ, cap,
        LLVMConstInt(i64, 0, 0), "cap0");
    LLVMValueRef ncap = LLVMBuildSelect(g->builder, cap0,
        LLVMConstInt(i64, 16, 0),
        LLVMBuildMul(g->builder, cap, LLVMConstInt(i64, 2, 0), "cap2"), "ncap");
    LLVMValueRef nbytes = LLVMBuildMul(g->builder, ncap, LLVMConstInt(i64, 8, 0), "nbytes");
    LLVMValueRef ks8 = LLVMBuildBitCast(g->builder, ks, i8ptr, "ks8");
    LLVMValueRef nks8 = zan_call2(g->builder, realloc_ty, g->fn_realloc,
        (LLVMValueRef[]){ ks8, nbytes }, 2, "nks8");
    LLVMBuildStore(g->builder, LLVMBuildBitCast(g->builder, nks8, i8pp, "nks"), kp);
    LLVMValueRef vs8 = LLVMBuildBitCast(g->builder, vs, i8ptr, "vs8");
    LLVMValueRef nvs8 = zan_call2(g->builder, realloc_ty, g->fn_realloc,
        (LLVMValueRef[]){ vs8, nbytes }, 2, "nvs8");
    LLVMBuildStore(g->builder, LLVMBuildBitCast(g->builder, nvs8, i64ptr, "nvs"), vp);
    LLVMBuildStore(g->builder, ncap, capp);
    LLVMBuildBr(g->builder, put_bb);
    LLVMPositionBuilderAtEnd(g->builder, put_bb);
    LLVMValueRef ks2 = LLVMBuildLoad2(g->builder, i8pp, kp, "ks2");
    LLVMValueRef vs2 = LLVMBuildLoad2(g->builder, i64ptr, vp, "vs2");
    LLVMValueRef kslot2 = LLVMBuildGEP2(g->builder, i8ptr, ks2, &cnt, 1, "ksl2");
    LLVMBuildStore(g->builder, key, kslot2);
    LLVMValueRef vslot2 = LLVMBuildGEP2(g->builder, i64, vs2, &cnt, 1, "vsl2");
    LLVMBuildStore(g->builder, val, vslot2);
    LLVMBuildStore(g->builder,
        LLVMBuildAdd(g->builder, cnt, LLVMConstInt(i64, 1, 0), "ncnt"), cntp);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* Exception-handling globals: a jmp_buf stack (16 deep, 1024 bytes each,
 * covering every supported target's jmp_buf), its top index (-1 = empty),
 * and the in-flight exception object. */
static void get_eh_globals(zan_irgen_t *g, LLVMValueRef *top,
                           LLVMValueRef *bufs, LLVMValueRef *exc) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMValueRef t = LLVMGetNamedGlobal(g->mod, "__zan_eh_top");
    if (!t) {
        t = LLVMAddGlobal(g->mod, i32t, "__zan_eh_top");
        LLVMSetLinkage(t, LLVMInternalLinkage);
        LLVMSetInitializer(t, LLVMConstInt(i32t, (unsigned long long)-1, 1));
    }
    LLVMValueRef b = LLVMGetNamedGlobal(g->mod, "__zan_eh_bufs");
    if (!b) {
        LLVMTypeRef buf_ty = LLVMArrayType(i8, 1024);
        LLVMTypeRef arr_ty = LLVMArrayType(buf_ty, 16);
        b = LLVMAddGlobal(g->mod, arr_ty, "__zan_eh_bufs");
        LLVMSetLinkage(b, LLVMInternalLinkage);
        LLVMSetInitializer(b, LLVMConstNull(arr_ty));
    }
    LLVMValueRef e = LLVMGetNamedGlobal(g->mod, "__zan_eh_exc");
    if (!e) {
        e = LLVMAddGlobal(g->mod, i8ptr, "__zan_eh_exc");
        LLVMSetLinkage(e, LLVMInternalLinkage);
        LLVMSetInitializer(e, LLVMConstNull(i8ptr));
    }
    *top = t; *bufs = b; *exc = e;
}

/* i32 setjmp on the current target: `_setjmp(buf, NULL)` on Windows (frame
 * NULL disables SEH unwinding), `_setjmp(buf)` elsewhere (no sigmask save). */
static LLVMValueRef emit_eh_setjmp(zan_irgen_t *g, LLVMValueRef bufp) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (g->target_is_windows) {
        LLVMTypeRef ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
        LLVMValueRef fn = get_libc_fn(g, "_setjmp", ty);
        LLVMValueRef call = zan_call2(g->builder, ty, fn,
            (LLVMValueRef[]){ bufp, LLVMConstNull(i8ptr) }, 2, "sj");
        return call;
    }
    LLVMTypeRef ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMValueRef fn = get_libc_fn(g, "_setjmp", ty);
    return zan_call2(g->builder, ty, fn, &bufp, 1, "sj");
}

static void emit_eh_longjmp(zan_irgen_t *g, LLVMValueRef bufp) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0);
    LLVMValueRef fn = get_libc_fn(g, "longjmp", ty);
    zan_call2(g->builder, ty, fn,
        (LLVMValueRef[]){ bufp, LLVMConstInt(i32t, 1, 0) }, 2, "");
}

/* ---- EH-owned temporaries -------------------------------------------------
 * `throw` unwinds with longjmp, which skips the compiler-emitted releases of
 * owned temporaries that are live across a call (a partially-constructed
 * object during its ctor, or an owned receiver temp of a method call), so a
 * throwing callee would leak them. Each such temp is pushed onto a small
 * global stack for the duration of the call and popped on normal return;
 * entering a catch releases every entry pushed after its try was entered. */
#define ZAN_EH_TMP_CAP 256

static void get_eh_tmp_globals(zan_irgen_t *g, LLVMValueRef *arr, LLVMValueRef *top) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef a = LLVMGetNamedGlobal(g->mod, "__zan_eh_tmps");
    if (!a) {
        LLVMTypeRef at = LLVMArrayType(i8ptr, ZAN_EH_TMP_CAP);
        a = LLVMAddGlobal(g->mod, at, "__zan_eh_tmps");
        LLVMSetLinkage(a, LLVMInternalLinkage);
        LLVMSetInitializer(a, LLVMConstNull(at));
    }
    LLVMValueRef t = LLVMGetNamedGlobal(g->mod, "__zan_eh_tmps_top");
    if (!t) {
        t = LLVMAddGlobal(g->mod, i32t, "__zan_eh_tmps_top");
        LLVMSetLinkage(t, LLVMInternalLinkage);
        LLVMSetInitializer(t, LLVMConstInt(i32t, 0, 0));
    }
    *arr = a; *top = t;
}

/* i32 flag: the in-flight __zan_eh_exc holds a +1 reference (class-typed
 * throw) that the catch must release after the handler runs. */
static LLVMValueRef get_eh_exc_owned_global(zan_irgen_t *g) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef v = LLVMGetNamedGlobal(g->mod, "__zan_eh_exc_owned");
    if (!v) {
        v = LLVMAddGlobal(g->mod, i32t, "__zan_eh_exc_owned");
        LLVMSetLinkage(v, LLVMInternalLinkage);
        LLVMSetInitializer(v, LLVMConstInt(i32t, 0, 0));
    }
    return v;
}

/* i8* pointing at the thrown object's class type-descriptor (see
 * get_class_tid_global); null for string/non-class throws. Catch clauses use
 * it for type-based dispatch. */
static LLVMValueRef get_eh_exc_tid_global(zan_irgen_t *g) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef v = LLVMGetNamedGlobal(g->mod, "__zan_eh_exc_tid");
    if (!v) {
        v = LLVMAddGlobal(g->mod, i8ptr, "__zan_eh_exc_tid");
        LLVMSetLinkage(v, LLVMInternalLinkage);
        LLVMSetInitializer(v, LLVMConstNull(i8ptr));
    }
    return v;
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
 * throw) matches any clause, preserving pre-dispatch behaviour. */
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
    LLVMValueRef thrown_null = LLVMBuildICmp(g->builder, LLVMIntEQ, thrown,
        LLVMConstNull(i8ptr), "tnull");
    LLVMBuildCondBr(g->builder, thrown_null, yes, loop);
    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef c = LLVMBuildLoad2(g->builder, i8ptr, cur, "c");
    LLVMValueRef cnull = LLVMBuildICmp(g->builder, LLVMIntEQ, c,
        LLVMConstNull(i8ptr), "cnull");
    LLVMBuildCondBr(g->builder, cnull, no, test);
    LLVMPositionBuilderAtEnd(g->builder, test);
    LLVMValueRef eq = LLVMBuildICmp(g->builder, LLVMIntEQ, c, want, "eq");
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
    LLVMValueRef arr_g, top_g;
    get_eh_tmp_globals(g, &arr_g, &top_g);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef store_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "store");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef top = LLVMBuildLoad2(g->builder, i32t, top_g, "top");
    LLVMValueRef fits = LLVMBuildICmp(g->builder, LLVMIntULT, top,
        LLVMConstInt(i32t, ZAN_EH_TMP_CAP, 0), "fits");
    LLVMBuildCondBr(g->builder, fits, store_bb, done);
    LLVMPositionBuilderAtEnd(g->builder, store_bb);
    LLVMValueRef gi[2] = { LLVMConstInt(i32t, 0, 0), top };
    LLVMValueRef slot = LLVMBuildGEP2(g->builder, LLVMGlobalGetValueType(arr_g),
        arr_g, gi, 2, "slot");
    LLVMBuildStore(g->builder, LLVMGetParam(fn, 0), slot);
    LLVMBuildBr(g->builder, done);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildStore(g->builder,
        LLVMBuildAdd(g->builder, top, LLVMConstInt(i32t, 1, 0), "ntop"), top_g);
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
    LLVMValueRef arr_g, top_g;
    get_eh_tmp_globals(g, &arr_g, &top_g);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef dec = LLVMAppendBasicBlockInContext(g->ctx, fn, "dec");
    LLVMBasicBlockRef clr = LLVMAppendBasicBlockInContext(g->ctx, fn, "clr");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef top = LLVMBuildLoad2(g->builder, i32t, top_g, "top");
    LLVMValueRef pos = LLVMBuildICmp(g->builder, LLVMIntSGT, top,
        LLVMConstInt(i32t, 0, 0), "pos");
    LLVMBuildCondBr(g->builder, pos, dec, done);
    LLVMPositionBuilderAtEnd(g->builder, dec);
    LLVMValueRef ntop = LLVMBuildSub(g->builder, top, LLVMConstInt(i32t, 1, 0), "ntop");
    LLVMBuildStore(g->builder, ntop, top_g);
    LLVMValueRef fits = LLVMBuildICmp(g->builder, LLVMIntULT, ntop,
        LLVMConstInt(i32t, ZAN_EH_TMP_CAP, 0), "fits");
    LLVMBuildCondBr(g->builder, fits, clr, done);
    LLVMPositionBuilderAtEnd(g->builder, clr);
    LLVMValueRef gi[2] = { LLVMConstInt(i32t, 0, 0), ntop };
    LLVMValueRef slot = LLVMBuildGEP2(g->builder, LLVMGlobalGetValueType(arr_g),
        arr_g, gi, 2, "slot");
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), slot);
    LLVMBuildBr(g->builder, done);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* void __zan_eh_tmp_unwind(i32 mark): release (via zan_rt_release_dyn) every
 * stacked temp above `mark`, restoring the stack to the try-entry depth. */
static LLVMValueRef get_eh_tmp_unwind_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_tmp_unwind");
    if (fn) return fn;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i32t, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_eh_tmp_unwind", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMValueRef arr_g, top_g;
    get_eh_tmp_globals(g, &arr_g, &top_g);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(g->ctx, fn, "head");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef rel = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMBuildBr(g->builder, head);
    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef top = LLVMBuildLoad2(g->builder, i32t, top_g, "top");
    LLVMValueRef above = LLVMBuildICmp(g->builder, LLVMIntSGT, top,
        LLVMGetParam(fn, 0), "above");
    LLVMBuildCondBr(g->builder, above, body, done);
    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef ntop = LLVMBuildSub(g->builder, top, LLVMConstInt(i32t, 1, 0), "ntop");
    LLVMBuildStore(g->builder, ntop, top_g);
    LLVMValueRef fits = LLVMBuildICmp(g->builder, LLVMIntULT, ntop,
        LLVMConstInt(i32t, ZAN_EH_TMP_CAP, 0), "fits");
    LLVMBuildCondBr(g->builder, fits, rel, head);
    LLVMPositionBuilderAtEnd(g->builder, rel);
    LLVMValueRef gi[2] = { LLVMConstInt(i32t, 0, 0), ntop };
    LLVMValueRef slot = LLVMBuildGEP2(g->builder, LLVMGlobalGetValueType(arr_g),
        arr_g, gi, 2, "slot");
    LLVMValueRef obj = LLVMBuildLoad2(g->builder, i8ptr, slot, "obj");
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), slot);
    zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_release_dyn, &obj, 1, "");
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

static void emit_eh_tmp_pop(zan_irgen_t *g) {
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    zan_call2(g->builder, fnty, get_eh_tmp_pop_fn(g), NULL, 0, "");
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
    LLVMValueRef nz = LLVMBuildICmp(g->builder, LLVMIntSGT, n, LLVMConstInt(i64, 0, 0), "nz");
    LLVMValueRef nm1 = LLVMBuildSub(g->builder, n, LLVMConstInt(i64, 1, 0), "nm1");
    LLVMValueRef sepsum = LLVMBuildSelect(g->builder, nz,
        LLVMBuildMul(g->builder, seplen, nm1, "ss"), LLVMConstInt(i64, 0, 0), "sepsum");
    LLVMBuildStore(g->builder, sepsum, tot_a);
    LLVMBuildBr(g->builder, m_cond);
    LLVMPositionBuilderAtEnd(g->builder, m_cond);
    LLVMValueRef mi = LLVMBuildLoad2(g->builder, i64, idx_a, "mi");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSLT, mi, n, "mlt"), m_body, c_pre);
    LLVMPositionBuilderAtEnd(g->builder, m_body);
    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &mi, 1, "slot");
    LLVMValueRef sv = LLVMBuildIntToPtr(g->builder,
        LLVMBuildLoad2(g->builder, i64, slot, "svi"), i8ptr, "sv");
    LLVMValueRef sl = zan_call2(g->builder, strlen_ty, g->fn_strlen, &sv, 1, "sl");
    LLVMBuildStore(g->builder,
        LLVMBuildAdd(g->builder, LLVMBuildLoad2(g->builder, i64, tot_a, "t0"), sl, "t1"), tot_a);
    LLVMBuildStore(g->builder,
        LLVMBuildAdd(g->builder, mi, LLVMConstInt(i64, 1, 0), "mi1"), idx_a);
    LLVMBuildBr(g->builder, m_cond);
    LLVMPositionBuilderAtEnd(g->builder, c_pre);
    LLVMValueRef total = LLVMBuildLoad2(g->builder, i64, tot_a, "total");
    LLVMValueRef buf = emit_string_alloc_rc(g,
        LLVMBuildAdd(g->builder, total, LLVMConstInt(i64, 1, 0), "tot1"));
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), pos_a);
    LLVMBuildBr(g->builder, c_cond);
    LLVMPositionBuilderAtEnd(g->builder, c_cond);
    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSLT, ci, n, "clt"), c_body, done);
    LLVMPositionBuilderAtEnd(g->builder, c_body);
    LLVMValueRef slot2 = LLVMBuildGEP2(g->builder, i64, data, &ci, 1, "slot2");
    LLVMValueRef sv2 = LLVMBuildIntToPtr(g->builder,
        LLVMBuildLoad2(g->builder, i64, slot2, "svi2"), i8ptr, "sv2");
    LLVMValueRef sl2 = zan_call2(g->builder, strlen_ty, g->fn_strlen, &sv2, 1, "sl2");
    LLVMValueRef pos = LLVMBuildLoad2(g->builder, i64, pos_a, "pos");
    LLVMValueRef dst = LLVMBuildGEP2(g->builder, i8, buf, &pos, 1, "dst");
    zan_call2(g->builder, memcpy_ty, memcpy_fn,
        (LLVMValueRef[]){ dst, sv2, sl2 }, 3, "");
    LLVMValueRef pos2 = LLVMBuildAdd(g->builder, pos, sl2, "pos2");
    LLVMBuildStore(g->builder, pos2, pos_a);
    LLVMValueRef last = LLVMBuildICmp(g->builder, LLVMIntSGE,
        LLVMBuildAdd(g->builder, ci, LLVMConstInt(i64, 1, 0), "ci1"), n, "last");
    LLVMBuildCondBr(g->builder, last, c_next, c_sep);
    LLVMPositionBuilderAtEnd(g->builder, c_sep);
    LLVMValueRef dst2 = LLVMBuildGEP2(g->builder, i8, buf, &pos2, 1, "dst2");
    zan_call2(g->builder, memcpy_ty, memcpy_fn,
        (LLVMValueRef[]){ dst2, sep, seplen }, 3, "");
    LLVMBuildStore(g->builder,
        LLVMBuildAdd(g->builder, pos2, seplen, "pos3"), pos_a);
    LLVMBuildBr(g->builder, c_next);
    LLVMPositionBuilderAtEnd(g->builder, c_next);
    LLVMBuildStore(g->builder,
        LLVMBuildAdd(g->builder, ci, LLVMConstInt(i64, 1, 0), "ni"), idx_a);
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
static LLVMValueRef coerce_dict_key(zan_irgen_t *g, LLVMValueRef key) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (LLVMGetTypeKind(LLVMTypeOf(key)) == LLVMIntegerTypeKind) {
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

/* Backing LLVM global for a static class/struct field `Class.field`.
 * Static fields are shared mutable storage (not compile-time constants):
 * they are lowered to an internal, zero-initialised module global. The
 * field's declared initializer is applied once at main() entry as a runtime
 * store (see emit_main_method), so any initializer expression works with
 * correct ordering. Returns NULL when `fsym` is not a static field. */
static LLVMValueRef get_static_field_global(zan_irgen_t *g, zan_symbol_t *class_sym,
                                            zan_symbol_t *fsym) {
    if (!class_sym || !fsym || !fsym->decl || fsym->decl->kind != AST_FIELD_DECL)
        return NULL;
    if (!((fsym->modifiers & MOD_STATIC) ||
          (fsym->decl->field_decl.modifiers & MOD_STATIC)))
        return NULL;
    char name[512];
    snprintf(name, sizeof(name), "__zan_sf_%.*s_%.*s",
             (int)class_sym->name.len, class_sym->name.str,
             (int)fsym->name.len, fsym->name.str);
    LLVMValueRef gv = LLVMGetNamedGlobal(g->mod, name);
    if (gv) return gv;
    LLVMTypeRef ft = fsym->type ? map_type(g, fsym->type)
                                : LLVMInt64TypeInContext(g->ctx);
    gv = LLVMAddGlobal(g->mod, ft, name);
    LLVMSetLinkage(gv, LLVMInternalLinkage);
    LLVMSetInitializer(gv, LLVMConstNull(ft));
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
            LLVMValueRef global = get_static_field_global(g, g->current_type_sym, field);
            LLVMTypeRef field_type = map_type(g, field->type);
            if (global) {
                LLVMBuildStore(g->builder, LLVMConstNull(field_type), global);
            } else if (g->current_this) {
                int fi = get_field_index(g->current_type_sym, arg->ident.name);
                LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                if (fi >= 0 && st) {
                    LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(st, 0), g->current_this, "this");
                    LLVMValueRef field_ptr = LLVMBuildStructGEP2(g->builder, st,
                        this_ptr, (unsigned)fi, "freed.fld");
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
                LLVMValueRef global = get_static_field_global(g, class_sym, field);
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
    LLVMValueRef field_ptr = LLVMBuildStructGEP2(g->builder, st, object_ptr,
                                                 (unsigned)fi, "freed.fld");
    LLVMBuildStore(g->builder, LLVMConstNull(map_type(g, field_type)), field_ptr);
}

/* Null-conditional access `a?.b` / `a?.M(...)`: evaluate the receiver once,
 * bind it to a synthetic local, and only evaluate the member/call when it is
 * non-null; a null receiver yields the result type's zero value. The member
 * node is temporarily rewired to read the synthetic local so the ordinary
 * member/call codegen paths apply unchanged, then restored (the same AST may
 * be re-emitted, e.g. per generic instantiation). */
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
    char *nmp = (char *)zan_arena_alloc(g->arena, strlen(nm) + 1);
    strcpy(nmp, nm);
    zan_istr_t iname = { nmp, (int)strlen(nmp) };
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
    LLVMValueRef isnull = LLVMBuildICmp(g->builder, LLVMIntEQ, obj,
        LLVMConstNull(LLVMTypeOf(obj)), "qdot.isnull");
    LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->builder);
    LLVMBuildCondBr(g->builder, isnull, merge_bb, then_bb);

    LLVMPositionBuilderAtEnd(g->builder, then_bb);
    LLVMValueRef v = emit_expr(g, expr, locals);
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
static void pack_params_args(zan_irgen_t *g, zan_ast_node_t *call,
                             zan_symbol_t *method_sym, local_scope_t *locals) {
    if (!method_sym || !method_sym->decl ||
        method_sym->decl->kind != AST_METHOD_DECL) return;
    if (!method_is_params_variadic(method_sym)) return;
    zan_ast_list_t *ps = &method_sym->decl->method_decl.params;
    int fixed = ps->count - 1;
    int argc = call->call.args.count;
    if (argc < fixed) return;
    if (argc == ps->count) {
        zan_ast_node_t *la = call->call.args.items[argc - 1];
        if (la->kind == AST_NEW_EXPR) return; /* already packed / explicit list */
        zan_type_t *lt = infer_expr_type(g, la, locals);
        if (lt && lt->name.len == 4 && memcmp(lt->name.str, "List", 4) == 0) return;
    }
    zan_ast_node_t *last_p = ps->items[ps->count - 1];
    zan_ast_node_t *lst = zan_ast_new(g->arena, AST_NEW_EXPR, call->loc);
    lst->new_expr.type = last_p->param.type; /* List<T> */
    lst->new_expr.is_array = false;
    zan_ast_list_init(&lst->new_expr.args);
    for (int i = fixed; i < argc; i++)
        zan_ast_list_push(&lst->new_expr.args, call->call.args.items[i], g->arena);
    call->call.args.count = fixed;
    zan_ast_list_push(&call->call.args, lst, g->arena);
}

static LLVMValueRef emit_ref_arg(zan_irgen_t *g, zan_ast_node_t *arg,
                                 local_scope_t *locals);
