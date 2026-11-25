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
    emit_string_len_set(g, buf, fin_n);
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
    emit_string_len_set(g, buf, n);
    LLVMBuildRet(g->builder, buf);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}
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
    LLVMValueRef ssz = zan_add(g->builder, n, one64, "ssz");
    LLVMValueRef str = emit_string_alloc_rc(g, ssz);
    zan_call2(g->builder, memcpy_ty, memcpy_fn,
        (LLVMValueRef[]){ str, p, n }, 3, "");
    LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, str, &n, 1, "endp");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
    /* 记长度：切出的片段以后 .Length / IndexOf 直接读头，不再 strlen。 */
    emit_string_len_set(g, str, n);
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

