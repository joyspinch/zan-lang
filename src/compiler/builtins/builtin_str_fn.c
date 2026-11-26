#include "irgen_internal.h"
/* irgen_builtins */

#include "charcase_table.h"

/* char 大小写映射（审计 P1-12 / A525 后续）：libc toupper/tolower 在 C
 * locale 下只翻 ASCII，与 C# 的 Unicode 不变文化语义不符。改为惰性发射
 * 模块内映射表 + 二分助手（首次用到 ToUpper/ToLower 才发，不用的程序
 * 零开销）。表由 .NET Invariant 生成（charcase_table.h），只收 1:1
 * 简单映射——扩张型（ß→SS）与无映射码点原样返回，同 C# char 语义。 */
LLVMValueRef get_char_case_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_case_map");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(c);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(c);
    LLVMTypeRef i64ptr = LLVMPointerType(i64t, 0);
    /* 两个只读全局表：槽位打包 (from<<32)|to，按 from 升序 */
    LLVMValueRef tabs[2];
    const uint64_t *data[2];
    int counts[2] = { ZAN_CASE_UP_N, ZAN_CASE_DOWN_N };
    data[0] = zan_case_up;
    data[1] = zan_case_down;
    for (int ti = 0; ti < 2; ti++) {
        LLVMValueRef ev[2048];
        for (int i = 0; i < counts[ti]; i++) {
            ev[i] = LLVMConstInt(i64t, data[ti][i], 0);
        }
        tabs[ti] = LLVMAddGlobal(g->mod, LLVMArrayType(i64t, counts[ti]),
                                 ti == 0 ? "zan.case.up" : "zan.case.down");
        LLVMSetGlobalConstant(tabs[ti], 1);
        LLVMSetLinkage(tabs[ti], LLVMPrivateLinkage);
        LLVMSetInitializer(tabs[ti], LLVMConstArray(i64t, ev, counts[ti]));
    }
    LLVMTypeRef fnty = LLVMFunctionType(i32t,
        (LLVMTypeRef[]){ i32t, i32t }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_case_map", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(c, fn, "loop");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(c, fn, "body");
    LLVMBasicBlockRef advance = LLVMAppendBasicBlockInContext(c, fn, "advance");
    LLVMBasicBlockRef hit = LLVMAppendBasicBlockInContext(c, fn, "hit");
    LLVMBasicBlockRef miss = LLVMAppendBasicBlockInContext(c, fn, "miss");

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef cp = LLVMGetParam(fn, 0);
    LLVMValueRef is_up = LLVMGetParam(fn, 1);
    LLVMValueRef is_up_cond = zan_icmp(g->builder, LLVMIntNE, is_up, LLVMConstInt(i32t, 0, 0), "cc.is_up");
    LLVMValueRef p0 = LLVMBuildBitCast(g->builder, tabs[0], i64ptr, "cc.p0");
    LLVMValueRef p1 = LLVMBuildBitCast(g->builder, tabs[1], i64ptr, "cc.p1");
    LLVMValueRef tabp = LLVMBuildSelect(g->builder, is_up_cond, p0, p1, "cc.tab");
    LLVMValueRef hi_init = LLVMBuildSelect(g->builder, is_up_cond,
        LLVMConstInt(i32t, ZAN_CASE_UP_N, 0),
        LLVMConstInt(i32t, ZAN_CASE_DOWN_N, 0), "cc.hi_init");
    LLVMValueRef lo_a = LLVMBuildAlloca(g->builder, i32t, "cc.lo");
    LLVMValueRef hi_a = LLVMBuildAlloca(g->builder, i32t, "cc.hi");
    LLVMBuildStore(g->builder, LLVMConstInt(i32t, 0, 0), lo_a);
    LLVMBuildStore(g->builder, hi_init, hi_a);
    LLVMBuildBr(g->builder, loop);

    /* loop: lo/hi 复查；空 → miss */
    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef lo = LLVMBuildLoad2(g->builder, i32t, lo_a, "cc.lo");
    LLVMValueRef hi = LLVMBuildLoad2(g->builder, i32t, hi_a, "cc.hi");
    LLVMValueRef empty = zan_icmp(g->builder, LLVMIntUGE, lo, hi, "cc.ge");
    LLVMBuildCondBr(g->builder, empty, miss, body);

    /* body: mid 槽位取打包值；命中 → hit，未中 → advance */
    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef mid = LLVMBuildLShr(g->builder,
        LLVMBuildAdd(g->builder, lo, hi, "cc.sum"),
        LLVMConstInt(i32t, 1, 0), "cc.mid");
    LLVMValueRef slot = LLVMBuildInBoundsGEP2(g->builder, i64t, tabp,
        &mid, 1, "cc.slot");
    LLVMValueRef v = LLVMBuildLoad2(g->builder, i64t, slot, "cc.v");
    LLVMValueRef from64 = LLVMBuildLShr(g->builder, v,
        LLVMConstInt(i64t, 32, 0), "cc.from");
    LLVMValueRef from = LLVMBuildTrunc(g->builder, from64, i32t, "cc.from32");
    LLVMValueRef to = LLVMBuildTrunc(g->builder, v, i32t, "cc.to");
    LLVMValueRef eq = zan_icmp(g->builder, LLVMIntEQ, cp, from, "cc.eq");
    LLVMBuildCondBr(g->builder, eq, hit, advance);

    /* advance: cp<from → hi=mid（左半），否则 lo=mid+1（右半）；回 loop */
    LLVMPositionBuilderAtEnd(g->builder, advance);
    LLVMValueRef lt = zan_icmp(g->builder, LLVMIntULT, cp, from, "cc.lt");
    LLVMValueRef newhi = LLVMBuildSelect(g->builder, lt, mid, hi, "cc.nh");
    LLVMValueRef newlo = LLVMBuildSelect(g->builder, lt, lo,
        LLVMBuildAdd(g->builder, mid, LLVMConstInt(i32t, 1, 0), "cc.lo1"),
        "cc.nl");
    LLVMBuildStore(g->builder, newlo, lo_a);
    LLVMBuildStore(g->builder, newhi, hi_a);
    LLVMBuildBr(g->builder, loop);

    /* hit / miss */
    LLVMPositionBuilderAtEnd(g->builder, hit);
    LLVMBuildRet(g->builder, to);
    LLVMPositionBuilderAtEnd(g->builder, miss);
    LLVMBuildRet(g->builder, cp);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}
LLVMValueRef get_libc_fn(zan_irgen_t *g, const char *name, LLVMTypeRef ty) {
    LLVMValueRef f = LLVMGetNamedFunction(g->mod, name);
    if (!f) f = LLVMAddFunction(g->mod, name, ty);
    return f;
}
LLVMValueRef get_str_last_index_of_fn(zan_irgen_t *g) {
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
LLVMValueRef get_str_trim_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_trim");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_trim", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
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
    LLVMValueRef is_sp = zan_icmp(g->builder, LLVMIntEQ, c, LLVMConstInt(i8, 32, 0), "is_sp");
    LLVMValueRef c_sub_9 = zan_sub(g->builder, c, LLVMConstInt(i8, 9, 0), "c_sub");
    LLVMValueRef is_ctl_ws = zan_icmp(g->builder, LLVMIntULE, c_sub_9, LLVMConstInt(i8, 4, 0), "is_ctl");
    LLVMValueRef nz = zan_or(g->builder, is_sp, is_ctl_ws, "isws");
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
    LLVMValueRef lis_sp = zan_icmp(g->builder, LLVMIntEQ, lc, LLVMConstInt(i8, 32, 0), "lis_sp");
    LLVMValueRef lc_sub_9 = zan_sub(g->builder, lc, LLVMConstInt(i8, 9, 0), "lc_sub");
    LLVMValueRef lis_ctl_ws = zan_icmp(g->builder, LLVMIntULE, lc_sub_9, LLVMConstInt(i8, 4, 0), "lis_ctl");
    LLVMValueRef lnz = zan_or(g->builder, lis_sp, lis_ctl_ws, "lisws");
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
LLVMValueRef get_str_case_fn(zan_irgen_t *g, int upper) {
    const char *fname = upper ? "__zan_str_to_upper" : "__zan_str_to_lower";
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fname);
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    fn = LLVMAddFunction(g->mod, fname, fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
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
    LLVMValueRef low_b = LLVMConstInt(i8, upper ? 'a' : 'A', 0);
    LLVMValueRef high_b = LLVMConstInt(i8, upper ? 'z' : 'Z', 0);
    LLVMValueRef diff = LLVMConstInt(i8, upper ? (uint64_t)(unsigned char)-32 : 32, 0);
    LLVMValueRef ge = zan_icmp(g->builder, LLVMIntUGE, c, low_b, "ge");
    LLVMValueRef le = zan_icmp(g->builder, LLVMIntULE, c, high_b, "le");
    LLVMValueRef in_rng = zan_and(g->builder, ge, le, "in_rng");
    LLVMValueRef conv = LLVMBuildAdd(g->builder, c, diff, "conv");
    LLVMValueRef c8 = LLVMBuildSelect(g->builder, in_rng, conv, c, "c8");
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
LLVMValueRef get_str_replace_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_replace");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_replace", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    {
        unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
        if (nw) LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(c, nw, 0));
    }
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef strstr_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMTypeRef memchr_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32, i64 }, 3, 0);
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef strstr_fn = get_libc_fn(g, "strstr", strstr_ty);
    LLVMValueRef memchr_fn = get_libc_fn(g, "memchr", memchr_ty);
    LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef dup_bb = LLVMAppendBasicBlockInContext(c, fn, "dup");
    LLVMBasicBlockRef chk_1to1 = LLVMAppendBasicBlockInContext(c, fn, "chk1to1");
    LLVMBasicBlockRef same_bb = LLVMAppendBasicBlockInContext(c, fn, "same");
    LLVMBasicBlockRef same_ret = LLVMAppendBasicBlockInContext(c, fn, "same.ret");
    LLVMBasicBlockRef r1_prep = LLVMAppendBasicBlockInContext(c, fn, "r1.prep");
    LLVMBasicBlockRef r1_loop = LLVMAppendBasicBlockInContext(c, fn, "r1.loop");
    LLVMBasicBlockRef r1_done = LLVMAppendBasicBlockInContext(c, fn, "r1.done");
    LLVMBasicBlockRef gen_prep = LLVMAppendBasicBlockInContext(c, fn, "gen.prep");
    LLVMBasicBlockRef cnt_loop = LLVMAppendBasicBlockInContext(c, fn, "cnt_loop");
    LLVMBasicBlockRef cnt_mc = LLVMAppendBasicBlockInContext(c, fn, "cnt.mc");
    LLVMBasicBlockRef cnt_ss = LLVMAppendBasicBlockInContext(c, fn, "cnt.ss");
    LLVMBasicBlockRef cnt_chk = LLVMAppendBasicBlockInContext(c, fn, "cnt.chk");
    LLVMBasicBlockRef cnt_hit = LLVMAppendBasicBlockInContext(c, fn, "cnt_hit");
    LLVMBasicBlockRef alloc_bb = LLVMAppendBasicBlockInContext(c, fn, "alloc");
    LLVMBasicBlockRef bld_prep = LLVMAppendBasicBlockInContext(c, fn, "bld.prep");
    LLVMBasicBlockRef bld_loop = LLVMAppendBasicBlockInContext(c, fn, "bld_loop");
    LLVMBasicBlockRef bld_mc = LLVMAppendBasicBlockInContext(c, fn, "bld.mc");
    LLVMBasicBlockRef bld_ss = LLVMAppendBasicBlockInContext(c, fn, "bld.ss");
    LLVMBasicBlockRef bld_chk = LLVMAppendBasicBlockInContext(c, fn, "bld.chk");
    LLVMBasicBlockRef bld_hit = LLVMAppendBasicBlockInContext(c, fn, "bld_hit");
    LLVMBasicBlockRef tail_bb = LLVMAppendBasicBlockInContext(c, fn, "tail");

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef from = LLVMGetParam(fn, 1);
    LLVMValueRef to = LLVMGetParam(fn, 2);
    LLVMValueRef zero64 = LLVMConstInt(i64, 0, 0);
    LLVMValueRef one64 = LLVMConstInt(i64, 1, 0);
    /* 按 C 可见长度截断于首个 NUL（与自举/string_nul_search 契约一致）；
     * 区间 [s, s + ls) 内无内嵌 NUL，后续 memchr 与单遍替换可安全执行。 */
    LLVMValueRef ls = zan_call2(b, strlen_ty, g->fn_strlen, &s, 1, "ls");
    LLVMValueRef lf = zan_call2(b, strlen_ty, g->fn_strlen, &from, 1, "lf");
    LLVMValueRef lt = zan_call2(b, strlen_ty, g->fn_strlen, &to, 1, "lt");
    LLVMValueRef s_end_i = zan_add(b, LLVMBuildPtrToInt(b, s, i64, "s.i"), ls, "send.i");
    LLVMValueRef cntv = LLVMBuildAlloca(b, i64, "cntv");
    LLVMValueRef pvar = LLVMBuildAlloca(b, i8ptr, "pvar");
    LLVMValueRef dvar = LLVMBuildAlloca(b, i8ptr, "dvar");
    LLVMValueRef from_empty = zan_icmp(b, LLVMIntEQ, lf, zero64, "fempty");
    LLVMBuildCondBr(b, from_empty, dup_bb, chk_1to1);

    LLVMPositionBuilderAtEnd(b, dup_bb);
    LLVMValueRef dupsz = zan_add(b, ls, one64, "dupsz");
    LLVMValueRef dupbuf = emit_string_alloc_rc(g, dupsz);
    zan_call2(b, memcpy_ty, memcpy_fn, (LLVMValueRef[]){ dupbuf, s, dupsz }, 3, "");
    emit_string_len_set(g, dupbuf, ls);
    LLVMBuildRet(b, dupbuf);

    /* 单字节 -> 单字节替换快路径：单遍扫描拷贝（LLVM 可自动 AVX2 向量化），无需两遍 strstr */
    LLVMPositionBuilderAtEnd(b, chk_1to1);
    LLVMValueRef lf_is1 = zan_icmp(b, LLVMIntEQ, lf, one64, "lf1");
    LLVMValueRef lt_is1 = zan_icmp(b, LLVMIntEQ, lt, one64, "lt1");
    LLVMValueRef is_1to1 = LLVMBuildAnd(b, lf_is1, lt_is1, "is1to1");
    LLVMBuildCondBr(b, is_1to1, r1_prep, gen_prep);

    /* 零命中/无变化时：仅当无内嵌 NUL（strlen == 精确长度）才零拷贝 retain 返回，否则截断复制 */
    LLVMPositionBuilderAtEnd(b, same_bb);
    LLVMValueRef exact_ls = emit_string_length(g, s, (zan_loc_t){0});
    zan_set_branch_weights(g,
        LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, ls, exact_ls, "nonul"), same_ret, dup_bb),
        2000, 1);

    LLVMPositionBuilderAtEnd(b, same_ret);
    emit_string_retain(g, s);
    LLVMBuildRet(b, s);

    LLVMPositionBuilderAtEnd(b, r1_prep);
    LLVMValueRef fc1 = LLVMBuildLoad2(b, i8, from, "fc1");
    LLVMValueRef tc1 = LLVMBuildLoad2(b, i8, to, "tc1");
    LLVMValueRef same_ch = LLVMBuildOr(b,
        zan_icmp(b, LLVMIntEQ, fc1, tc1, "eqch"),
        zan_icmp(b, LLVMIntEQ, ls, zero64, "ls0"), "noop1");
    LLVMBasicBlockRef r1_alloc = LLVMAppendBasicBlockInContext(c, fn, "r1.alloc");
    LLVMBuildCondBr(b, same_ch, same_bb, r1_alloc);

    LLVMPositionBuilderAtEnd(b, r1_alloc);
    LLVMValueRef r1_sz = zan_add(b, ls, one64, "r1.sz");
    LLVMValueRef r1_buf = emit_string_alloc_rc(g, r1_sz);
    LLVMBuildBr(b, r1_loop);

    LLVMPositionBuilderAtEnd(b, r1_loop);
    LLVMValueRef r1_i = LLVMBuildPhi(b, i64, "r1.i");
    LLVMValueRef r1_srcp = LLVMBuildInBoundsGEP2(b, i8, s, &r1_i, 1, "r1.sp");
    LLVMValueRef r1_ch = LLVMBuildLoad2(b, i8, r1_srcp, "r1.ch");
    LLVMValueRef r1_eq = zan_icmp(b, LLVMIntEQ, r1_ch, fc1, "r1.eq");
    LLVMValueRef r1_out = LLVMBuildSelect(b, r1_eq, tc1, r1_ch, "r1.out");
    LLVMValueRef r1_dstp = LLVMBuildInBoundsGEP2(b, i8, r1_buf, &r1_i, 1, "r1.dp");
    LLVMBuildStore(b, r1_out, r1_dstp);
    LLVMValueRef r1_next = zan_add(b, r1_i, one64, "r1.next");
    LLVMAddIncoming(r1_i, (LLVMValueRef[]){ zero64, r1_next },
                    (LLVMBasicBlockRef[]){ r1_alloc, r1_loop }, 2);
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntULT, r1_next, ls, "r1.more"), r1_loop, r1_done);

    LLVMPositionBuilderAtEnd(b, r1_done);
    LLVMValueRef r1_endp = LLVMBuildInBoundsGEP2(b, i8, r1_buf, &ls, 1, "r1.endp");
    LLVMBuildStore(b, LLVMConstInt(i8, 0, 0), r1_endp);
    emit_string_len_set(g, r1_buf, ls);
    LLVMBuildRet(b, r1_buf);

    /* 通用路径：lf == 1 时走 memchr，否则走 strstr；零命中直接 retain+返回 */
    LLVMPositionBuilderAtEnd(b, gen_prep);
    LLVMValueRef fc_i32 = LLVMBuildZExt(b, LLVMBuildLoad2(b, i8, from, "fc"), i32, "fc32");
    LLVMBuildStore(b, zero64, cntv);
    LLVMBuildStore(b, s, pvar);
    LLVMBuildBr(b, cnt_loop);

    LLVMPositionBuilderAtEnd(b, cnt_loop);
    LLVMValueRef cp = LLVMBuildLoad2(b, i8ptr, pvar, "cp");
    LLVMBuildCondBr(b, lf_is1, cnt_mc, cnt_ss);

    LLVMPositionBuilderAtEnd(b, cnt_mc);
    LLVMValueRef crem = zan_sub(b, s_end_i, LLVMBuildPtrToInt(b, cp, i64, "cp.i"), "crem");
    LLVMValueRef chit_mc = zan_call2(b, memchr_ty, memchr_fn,
        (LLVMValueRef[]){ cp, fc_i32, crem }, 3, "chit.mc");
    LLVMBuildBr(b, cnt_chk);

    LLVMPositionBuilderAtEnd(b, cnt_ss);
    LLVMValueRef chit_ss = zan_call2(b, strstr_ty, strstr_fn,
        (LLVMValueRef[]){ cp, from }, 2, "chit.ss");
    LLVMBuildBr(b, cnt_chk);

    LLVMPositionBuilderAtEnd(b, cnt_chk);
    LLVMValueRef chit = LLVMBuildPhi(b, i8ptr, "chit");
    LLVMAddIncoming(chit, (LLVMValueRef[]){ chit_mc, chit_ss },
                    (LLVMBasicBlockRef[]){ cnt_mc, cnt_ss }, 2);
    LLVMValueRef cmiss = zan_icmp(b, LLVMIntEQ, chit, LLVMConstNull(i8ptr), "cmiss");
    LLVMBuildCondBr(b, cmiss, alloc_bb, cnt_hit);

    LLVMPositionBuilderAtEnd(b, cnt_hit);
    LLVMValueRef cnt = LLVMBuildLoad2(b, i64, cntv, "cnt");
    LLVMBuildStore(b, zan_add(b, cnt, one64, "cnt1"), cntv);
    LLVMValueRef cnext = LLVMBuildGEP2(b, i8, chit, &lf, 1, "cnext");
    LLVMBuildStore(b, cnext, pvar);
    LLVMBuildBr(b, cnt_loop);

    LLVMPositionBuilderAtEnd(b, alloc_bb);
    LLVMValueRef total_cnt = LLVMBuildLoad2(b, i64, cntv, "tcnt");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, total_cnt, zero64, "nohit"),
                    same_bb, bld_prep);

    LLVMPositionBuilderAtEnd(b, bld_prep);
    LLVMValueRef delta = zan_sub(b, lt, lf, "delta");
    LLVMValueRef grow = zan_mul(b, total_cnt, delta, "grow");
    LLVMValueRef newlen = zan_add(b, ls, grow, "newlen");
    LLVMValueRef bufsz = zan_add(b, newlen, one64, "bsz");
    LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
    LLVMBuildStore(b, s, pvar);
    LLVMBuildStore(b, buf, dvar);
    LLVMBuildBr(b, bld_loop);

    LLVMPositionBuilderAtEnd(b, bld_loop);
    LLVMValueRef bp = LLVMBuildLoad2(b, i8ptr, pvar, "bp");
    LLVMBuildCondBr(b, lf_is1, bld_mc, bld_ss);

    LLVMPositionBuilderAtEnd(b, bld_mc);
    LLVMValueRef brem = zan_sub(b, s_end_i, LLVMBuildPtrToInt(b, bp, i64, "bp.i"), "brem");
    LLVMValueRef bhit_mc = zan_call2(b, memchr_ty, memchr_fn,
        (LLVMValueRef[]){ bp, fc_i32, brem }, 3, "bhit.mc");
    LLVMBuildBr(b, bld_chk);

    LLVMPositionBuilderAtEnd(b, bld_ss);
    LLVMValueRef bhit_ss = zan_call2(b, strstr_ty, strstr_fn,
        (LLVMValueRef[]){ bp, from }, 2, "bhit.ss");
    LLVMBuildBr(b, bld_chk);

    LLVMPositionBuilderAtEnd(b, bld_chk);
    LLVMValueRef bhit = LLVMBuildPhi(b, i8ptr, "bhit");
    LLVMAddIncoming(bhit, (LLVMValueRef[]){ bhit_mc, bhit_ss },
                    (LLVMBasicBlockRef[]){ bld_mc, bld_ss }, 2);
    LLVMValueRef bmiss = zan_icmp(b, LLVMIntEQ, bhit, LLVMConstNull(i8ptr), "bmiss");
    LLVMBuildCondBr(b, bmiss, tail_bb, bld_hit);

    LLVMPositionBuilderAtEnd(b, bld_hit);
    LLVMValueRef d = LLVMBuildLoad2(b, i8ptr, dvar, "d");
    LLVMValueRef pre = zan_sub(b,
        LLVMBuildPtrToInt(b, bhit, i64, "bhi"),
        LLVMBuildPtrToInt(b, bp, i64, "bpi"), "pre");
    zan_call2(b, memcpy_ty, memcpy_fn, (LLVMValueRef[]){ d, bp, pre }, 3, "");
    LLVMValueRef d1 = LLVMBuildGEP2(b, i8, d, &pre, 1, "d1");
    zan_call2(b, memcpy_ty, memcpy_fn, (LLVMValueRef[]){ d1, to, lt }, 3, "");
    LLVMValueRef d2 = LLVMBuildGEP2(b, i8, d1, &lt, 1, "d2");
    LLVMBuildStore(b, d2, dvar);
    LLVMValueRef bnext = LLVMBuildGEP2(b, i8, bhit, &lf, 1, "bnext");
    LLVMBuildStore(b, bnext, pvar);
    LLVMBuildBr(b, bld_loop);

    LLVMPositionBuilderAtEnd(b, tail_bb);
    LLVMValueRef tp = LLVMBuildLoad2(b, i8ptr, pvar, "tp");
    LLVMValueRef td = LLVMBuildLoad2(b, i8ptr, dvar, "td");
    LLVMValueRef rest = zan_sub(b, s_end_i, LLVMBuildPtrToInt(b, tp, i64, "tp.i"), "rest");
    LLVMValueRef restsz = zan_add(b, rest, one64, "restsz");
    zan_call2(b, memcpy_ty, memcpy_fn, (LLVMValueRef[]){ td, tp, restsz }, 3, "");
    emit_string_len_set(g, buf, newlen);
    LLVMBuildRet(b, buf);

    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}

static LLVMValueRef get_list_push_strn_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_list_push_strn");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(c),
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_list_push_strn", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    {
        unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
        if (nw) LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(c, nw, 0));
    }
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef bb_empty = LLVMAppendBasicBlockInContext(c, fn, "str.empty");
    LLVMBasicBlockRef bb_chk1 = LLVMAppendBasicBlockInContext(c, fn, "str.chk1");
    LLVMBasicBlockRef bb_chkasc = LLVMAppendBasicBlockInContext(c, fn, "str.chkasc");
    LLVMBasicBlockRef bb_asc1 = LLVMAppendBasicBlockInContext(c, fn, "str.asc1");
    LLVMBasicBlockRef bb_alloc = LLVMAppendBasicBlockInContext(c, fn, "str.alloc");
    LLVMBasicBlockRef bb_have = LLVMAppendBasicBlockInContext(c, fn, "str.have");
    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(c, fn, "grow");
    LLVMBasicBlockRef push_bb = LLVMAppendBasicBlockInContext(c, fn, "push");

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef lst = LLVMGetParam(fn, 0);
    LLVMValueRef p = LLVMGetParam(fn, 1);
    LLVMValueRef n = LLVMGetParam(fn, 2);
    LLVMValueRef zero64 = LLVMConstInt(i64, 0, 0);
    LLVMValueRef one64 = LLVMConstInt(i64, 1, 0);
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, n, zero64, "n0"), bb_empty, bb_chk1);

    LLVMPositionBuilderAtEnd(b, bb_empty);
    LLVMValueRef empty_s = emit_string_literal_rc(g, (zan_istr_t){ "", 0 });
    LLVMBuildBr(b, bb_have);

    LLVMPositionBuilderAtEnd(b, bb_chk1);
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, n, one64, "n1"), bb_chkasc, bb_alloc);

    LLVMPositionBuilderAtEnd(b, bb_chkasc);
    LLVMValueRef ch0 = LLVMBuildZExt(b, LLVMBuildLoad2(b, i8, p, "c0"), i64, "c0.64");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntULT, ch0, LLVMConstInt(i64, 128, 0), "c0.asc"),
                    bb_asc1, bb_alloc);

    LLVMPositionBuilderAtEnd(b, bb_asc1);
    LLVMValueRef asc_s = emit_ascii_char_ptr(g, ch0);
    LLVMBuildBr(b, bb_have);

    LLVMPositionBuilderAtEnd(b, bb_alloc);
    LLVMValueRef ssz = zan_add(b, n, one64, "ssz");
    LLVMValueRef alloc_s = emit_string_alloc_rc(g, ssz);
    zan_call2(b, memcpy_ty, memcpy_fn, (LLVMValueRef[]){ alloc_s, p, n }, 3, "");
    LLVMValueRef endp = LLVMBuildGEP2(b, i8, alloc_s, &n, 1, "endp");
    LLVMBuildStore(b, LLVMConstInt(i8, 0, 0), endp);
    emit_string_len_set(g, alloc_s, n);
    LLVMBasicBlockRef alloc_end = LLVMGetInsertBlock(b);
    LLVMBuildBr(b, bb_have);

    LLVMPositionBuilderAtEnd(b, bb_have);
    LLVMValueRef str = LLVMBuildPhi(b, i8ptr, "str");
    LLVMAddIncoming(str,
        (LLVMValueRef[]){ empty_s, asc_s, alloc_s },
        (LLVMBasicBlockRef[]){ bb_empty, bb_asc1, alloc_end }, 3);

    LLVMValueRef lp = LLVMBuildBitCast(b, lst, LLVMPointerType(g->list_struct_type, 0), "lp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(b, g->list_struct_type, lp, 0, "cntp");
    LLVMValueRef count = LLVMBuildLoad2(b, i64, cntp, "cnt");
    LLVMValueRef capp = LLVMBuildStructGEP2(b, g->list_struct_type, lp, 1, "capp");
    LLVMValueRef cap = LLVMBuildLoad2(b, i64, capp, "cap");
    LLVMValueRef full = zan_icmp(b, LLVMIntSGE, count, cap, "full");
    zan_set_branch_weights(g, LLVMBuildCondBr(b, full, grow_bb, push_bb), 1, 2000);

    LLVMPositionBuilderAtEnd(b, grow_bb);
    LLVMValueRef cap0 = zan_icmp(b, LLVMIntEQ, cap, zero64, "cap0");
    LLVMValueRef ncap = LLVMBuildSelect(b, cap0,
        LLVMConstInt(i64, 8, 0),
        zan_mul(b, cap, LLVMConstInt(i64, 2, 0), "cap2"), "ncap");
    LLVMValueRef datap = LLVMBuildStructGEP2(b, g->list_struct_type, lp, 2, "datap");
    LLVMValueRef odata = LLVMBuildLoad2(b, i64ptr, datap, "odata");
    LLVMValueRef odata8 = LLVMBuildBitCast(b, odata, i8ptr, "odata8");
    LLVMValueRef nbytes = zan_mul(b, ncap, LLVMConstInt(i64, 8, 0), "nbytes");
    LLVMValueRef ndata8 = zan_call2(b,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
        g->fn_realloc, (LLVMValueRef[]){ odata8, nbytes }, 2, "ndata8");
    zan_irgen_emit_oom_check(g, fn, ndata8);
    LLVMValueRef ndata = LLVMBuildBitCast(b, ndata8, i64ptr, "ndata");
    LLVMBuildStore(b, ndata, datap);
    LLVMBuildStore(b, ncap, capp);
    LLVMBuildBr(b, push_bb);

    LLVMPositionBuilderAtEnd(b, push_bb);
    LLVMValueRef datap2 = LLVMBuildStructGEP2(b, g->list_struct_type, lp, 2, "datap2");
    LLVMValueRef data = LLVMBuildLoad2(b, i64ptr, datap2, "data");
    LLVMValueRef slot = LLVMBuildGEP2(b, i64, data, &count, 1, "slot");
    LLVMBuildStore(b, LLVMBuildPtrToInt(b, str, i64, "sv"), slot);
    LLVMBuildStore(b, zan_add(b, count, one64, "ncnt"), cntp);
    LLVMBuildRetVoid(b);

    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}

LLVMValueRef get_str_split_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_split");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(c),
        (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr, i64 }, 4, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_split", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    {
        unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
        if (nw) LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(c, nw, 0));
    }
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef strstr_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMTypeRef memchr_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32, i64 }, 3, 0);
    LLVMValueRef strstr_fn = get_libc_fn(g, "strstr", strstr_ty);
    LLVMValueRef memchr_fn = get_libc_fn(g, "memchr", memchr_ty);
    LLVMValueRef push_fn = get_list_push_strn_fn(g);
    LLVMTypeRef push_ty = LLVMFunctionType(LLVMVoidTypeInContext(c),
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(c, fn, "loop");
    LLVMBasicBlockRef scan_mc = LLVMAppendBasicBlockInContext(c, fn, "scan.mc");
    LLVMBasicBlockRef scan_ss = LLVMAppendBasicBlockInContext(c, fn, "scan.ss");
    LLVMBasicBlockRef scan_chk = LLVMAppendBasicBlockInContext(c, fn, "scan.chk");
    LLVMBasicBlockRef hit_bb = LLVMAppendBasicBlockInContext(c, fn, "hit");
    LLVMBasicBlockRef do_push_bb = LLVMAppendBasicBlockInContext(c, fn, "do_push");
    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(c, fn, "next_seg");
    LLVMBasicBlockRef last_bb = LLVMAppendBasicBlockInContext(c, fn, "last");
    LLVMBasicBlockRef do_last_push_bb = LLVMAppendBasicBlockInContext(c, fn, "do_last_push");
    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(c, fn, "end_split");

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef sep = LLVMGetParam(fn, 1);
    LLVMValueRef lst = LLVMGetParam(fn, 2);
    LLVMValueRef opt = LLVMGetParam(fn, 3);
    LLVMValueRef zero64 = LLVMConstInt(i64, 0, 0);
    LLVMValueRef one64 = LLVMConstInt(i64, 1, 0);
    /* 按 C 可见长度截断于首个 NUL（与自举/string_nul_search 契约一致） */
    LLVMValueRef slen = zan_call2(b, strlen_ty, g->fn_strlen, &s, 1, "slen");
    LLVMValueRef seplen = zan_call2(b, strlen_ty, g->fn_strlen, &sep, 1, "seplen");
    LLVMValueRef s_end_i = zan_add(b, LLVMBuildPtrToInt(b, s, i64, "s.i"), slen, "send.i");
    LLVMValueRef sep_is1 = zan_icmp(b, LLVMIntEQ, seplen, one64, "sep1");
    LLVMValueRef pvar = LLVMBuildAlloca(b, i8ptr, "pvar");
    LLVMBuildStore(b, s, pvar);
    LLVMValueRef sep_empty = zan_icmp(b, LLVMIntEQ, seplen, zero64, "sempty");
    LLVMBuildCondBr(b, sep_empty, last_bb, loop_bb);

    LLVMPositionBuilderAtEnd(b, loop_bb);
    LLVMValueRef cur = LLVMBuildLoad2(b, i8ptr, pvar, "cur");
    LLVMBuildCondBr(b, sep_is1, scan_mc, scan_ss);

    LLVMPositionBuilderAtEnd(b, scan_mc);
    LLVMValueRef sep_ch32 = LLVMBuildZExt(b, LLVMBuildLoad2(b, i8, sep, "sep.c0"), i32, "sep.c32");
    LLVMValueRef rem = zan_sub(b, s_end_i, LLVMBuildPtrToInt(b, cur, i64, "cur.i"), "rem");
    LLVMValueRef hit_mc = zan_call2(b, memchr_ty, memchr_fn,
        (LLVMValueRef[]){ cur, sep_ch32, rem }, 3, "hit.mc");
    LLVMBuildBr(b, scan_chk);

    LLVMPositionBuilderAtEnd(b, scan_ss);
    LLVMValueRef hit_ss = zan_call2(b, strstr_ty, strstr_fn,
        (LLVMValueRef[]){ cur, sep }, 2, "hit.ss");
    LLVMBuildBr(b, scan_chk);

    LLVMPositionBuilderAtEnd(b, scan_chk);
    LLVMValueRef hit = LLVMBuildPhi(b, i8ptr, "hit");
    LLVMAddIncoming(hit, (LLVMValueRef[]){ hit_mc, hit_ss },
                    (LLVMBasicBlockRef[]){ scan_mc, scan_ss }, 2);
    LLVMValueRef miss = zan_icmp(b, LLVMIntEQ, hit, LLVMConstNull(i8ptr), "miss");
    LLVMBuildCondBr(b, miss, last_bb, hit_bb);

    LLVMPositionBuilderAtEnd(b, hit_bb);
    LLVMValueRef seg = zan_sub(b,
        LLVMBuildPtrToInt(b, hit, i64, "hi"),
        LLVMBuildPtrToInt(b, cur, i64, "ci"), "seg");
    LLVMValueRef skip_push = LLVMBuildAnd(b,
        LLVMBuildTrunc(b, opt, LLVMInt1TypeInContext(c), "rm_empty"),
        zan_icmp(b, LLVMIntEQ, seg, zero64, "seg_zero"),
        "skip_empty");
    LLVMBuildCondBr(b, skip_push, next_bb, do_push_bb);

    LLVMPositionBuilderAtEnd(b, do_push_bb);
    zan_call2(b, push_ty, push_fn, (LLVMValueRef[]){ lst, cur, seg }, 3, "");
    LLVMBuildBr(b, next_bb);

    LLVMPositionBuilderAtEnd(b, next_bb);
    LLVMValueRef next = LLVMBuildGEP2(b, i8, hit, &seplen, 1, "next");
    LLVMBuildStore(b, next, pvar);
    LLVMBuildBr(b, loop_bb);

    LLVMPositionBuilderAtEnd(b, last_bb);
    LLVMValueRef tp = LLVMBuildLoad2(b, i8ptr, pvar, "tp");
    LLVMValueRef rest = zan_sub(b, s_end_i, LLVMBuildPtrToInt(b, tp, i64, "tp.i"), "rest");
    LLVMValueRef skip_last = LLVMBuildAnd(b,
        LLVMBuildTrunc(b, opt, LLVMInt1TypeInContext(c), "rm_empty_last"),
        zan_icmp(b, LLVMIntEQ, rest, zero64, "rest_zero"),
        "skip_last_empty");
    LLVMBuildCondBr(b, skip_last, end_bb, do_last_push_bb);

    LLVMPositionBuilderAtEnd(b, do_last_push_bb);
    zan_call2(b, push_ty, push_fn, (LLVMValueRef[]){ lst, tp, rest }, 3, "");
    LLVMBuildBr(b, end_bb);

    LLVMPositionBuilderAtEnd(b, end_bb);
    LLVMBuildRetVoid(b);

    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}

