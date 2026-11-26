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
    {
        unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
        if (nw) LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex,
                                        LLVMCreateEnumAttribute(c, nw, 0));
    }
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
/* 内联快速 ASCII 十进制 i64 解析器（替代 CRT strtoll + TLS _errno）：
 * 返回状态码：0 = 成功（结果写入 *out_val），1 = 格式错（空串/无数字/尾部残留），
 * 2 = 64 位有符号溢出（且剩余字符均为数字，尾部无非法字符）。
 * 语义与原 strtoll + emit_parse_bad_pred + ERANGE 100% 等价：
 *   - 跳过前导 C-locale ASCII 空白（' ', \t..\r）
 *   - 可选 '+' / '-'
 *   - 至少 1 位十进制数字
 *   - 溢出后继续消费后续数字；若停在非 NUL 字符则先报格式错（返回 1），
 *     停在 NUL 才报溢出（返回 2）。 */
LLVMValueRef get_try_parse_i64_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_try_parse_i64");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i1 = LLVMInt1TypeInContext(c);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef fnty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64ptr }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_try_parse_i64", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    {
        unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
        if (nw) LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex,
                                        LLVMCreateEnumAttribute(c, nw, 0));
    }

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef ws_loop = LLVMAppendBasicBlockInContext(c, fn, "ws.loop");
    LLVMBasicBlockRef ws_adv = LLVMAppendBasicBlockInContext(c, fn, "ws.adv");
    LLVMBasicBlockRef sign_bb = LLVMAppendBasicBlockInContext(c, fn, "sign");
    LLVMBasicBlockRef dig_init = LLVMAppendBasicBlockInContext(c, fn, "dig.init");
    LLVMBasicBlockRef dig_loop = LLVMAppendBasicBlockInContext(c, fn, "dig.loop");
    LLVMBasicBlockRef dig_step = LLVMAppendBasicBlockInContext(c, fn, "dig.step");
    LLVMBasicBlockRef end_chk = LLVMAppendBasicBlockInContext(c, fn, "end.chk");
    LLVMBasicBlockRef ov_chk = LLVMAppendBasicBlockInContext(c, fn, "ov.chk");
    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(c, fn, "ok");
    LLVMBasicBlockRef fmt_err = LLVMAppendBasicBlockInContext(c, fn, "fmt.err");
    LLVMBasicBlockRef ov_err = LLVMAppendBasicBlockInContext(c, fn, "ov.err");

    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef out_val = LLVMGetParam(fn, 1);
    LLVMValueRef zero64 = LLVMConstInt(i64, 0, 0);
    LLVMValueRef one64 = LLVMConstInt(i64, 1, 0);

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMBuildStore(b, zero64, out_val);
    LLVMBuildBr(b, ws_loop);

    LLVMPositionBuilderAtEnd(b, ws_loop);
    LLVMValueRef p_ws = LLVMBuildPhi(b, i8ptr, "p.ws");
    LLVMValueRef c_ws = LLVMBuildLoad2(b, i8, p_ws, "c.ws");
    LLVMValueRef is_sp = zan_icmp(b, LLVMIntEQ, c_ws, LLVMConstInt(i8, 32, 0), "is.sp");
    LLVMValueRef c_sub9 = zan_sub(b, c_ws, LLVMConstInt(i8, 9, 0), "c.sub9");
    LLVMValueRef is_ctl = zan_icmp(b, LLVMIntULE, c_sub9, LLVMConstInt(i8, 4, 0), "is.ctl");
    LLVMValueRef is_ws = zan_or(b, is_sp, is_ctl, "is.ws");
    LLVMBuildCondBr(b, is_ws, ws_adv, sign_bb);

    LLVMPositionBuilderAtEnd(b, ws_adv);
    LLVMValueRef p_ws1 = LLVMBuildInBoundsGEP2(b, i8, p_ws, &one64, 1, "p.ws1");
    LLVMBuildBr(b, ws_loop);
    LLVMAddIncoming(p_ws, (LLVMValueRef[]){ s, p_ws1 },
                    (LLVMBasicBlockRef[]){ entry, ws_adv }, 2);

    LLVMPositionBuilderAtEnd(b, sign_bb);
    LLVMValueRef is_neg = zan_icmp(b, LLVMIntEQ, c_ws, LLVMConstInt(i8, '-', 0), "is.neg");
    LLVMValueRef is_pos = zan_icmp(b, LLVMIntEQ, c_ws, LLVMConstInt(i8, '+', 0), "is.pos");
    LLVMValueRef has_sign = zan_or(b, is_neg, is_pos, "has.sign");
    LLVMValueRef sign_off = LLVMBuildZExt(b, has_sign, i64, "sign.off");
    LLVMValueRef p0 = LLVMBuildInBoundsGEP2(b, i8, p_ws, &sign_off, 1, "p0");
    LLVMValueRef c0 = LLVMBuildLoad2(b, i8, p0, "c0");
    LLVMValueRef d0 = zan_sub(b, c0, LLVMConstInt(i8, '0', 0), "d0");
    LLVMValueRef is_d0 = zan_icmp(b, LLVMIntULE, d0, LLVMConstInt(i8, 9, 0), "is.d0");
    zan_set_branch_weights(g, LLVMBuildCondBr(b, is_d0, dig_init, fmt_err), 2000, 1);

    LLVMPositionBuilderAtEnd(b, dig_init);
    LLVMValueRef acc0 = LLVMBuildZExt(b, d0, i64, "acc0");
    LLVMValueRef p1 = LLVMBuildInBoundsGEP2(b, i8, p0, &one64, 1, "p1");
    LLVMValueRef lim_div10 = LLVMConstInt(i64, 922337203685477580ULL, 0);
    LLVMValueRef lim_mod10 = LLVMBuildSelect(b, is_neg,
        LLVMConstInt(i64, 8, 0), LLVMConstInt(i64, 7, 0), "lim.mod");
    LLVMBuildBr(b, dig_loop);

    LLVMPositionBuilderAtEnd(b, dig_loop);
    LLVMValueRef p = LLVMBuildPhi(b, i8ptr, "p");
    LLVMValueRef acc = LLVMBuildPhi(b, i64, "acc");
    LLVMValueRef ov = LLVMBuildPhi(b, i1, "ov");
    LLVMValueRef ch = LLVMBuildLoad2(b, i8, p, "ch");
    LLVMValueRef dig = zan_sub(b, ch, LLVMConstInt(i8, '0', 0), "dig");
    LLVMValueRef is_dig = zan_icmp(b, LLVMIntULE, dig, LLVMConstInt(i8, 9, 0), "is.dig");
    LLVMBuildCondBr(b, is_dig, dig_step, end_chk);

    LLVMPositionBuilderAtEnd(b, dig_step);
    LLVMValueRef d64 = LLVMBuildZExt(b, dig, i64, "d64");
    LLVMValueRef gt_lim = zan_icmp(b, LLVMIntUGT, acc, lim_div10, "gt.lim");
    LLVMValueRef eq_lim = zan_icmp(b, LLVMIntEQ, acc, lim_div10, "eq.lim");
    LLVMValueRef gt_mod = zan_icmp(b, LLVMIntUGT, d64, lim_mod10, "gt.mod");
    LLVMValueRef ov_now = zan_or(b, gt_lim, zan_and(b, eq_lim, gt_mod, "ov.edge"), "ov.now");
    LLVMValueRef ov_next = zan_or(b, ov, ov_now, "ov.next");
    LLVMValueRef acc10 = zan_mul(b, acc, LLVMConstInt(i64, 10, 0), "acc10");
    LLVMValueRef acc_cand = zan_add(b, acc10, d64, "acc.cand");
    LLVMValueRef acc_next = LLVMBuildSelect(b, ov_next, acc, acc_cand, "acc.next");
    LLVMValueRef p_next = LLVMBuildInBoundsGEP2(b, i8, p, &one64, 1, "p.next");
    LLVMBuildBr(b, dig_loop);
    LLVMAddIncoming(p, (LLVMValueRef[]){ p1, p_next },
                    (LLVMBasicBlockRef[]){ dig_init, dig_step }, 2);
    LLVMAddIncoming(acc, (LLVMValueRef[]){ acc0, acc_next },
                    (LLVMBasicBlockRef[]){ dig_init, dig_step }, 2);
    LLVMAddIncoming(ov, (LLVMValueRef[]){ LLVMConstInt(i1, 0, 0), ov_next },
                    (LLVMBasicBlockRef[]){ dig_init, dig_step }, 2);

    LLVMPositionBuilderAtEnd(b, end_chk);
    LLVMValueRef at_nul = zan_icmp(b, LLVMIntEQ, ch, LLVMConstInt(i8, 0, 0), "at.nul");
    zan_set_branch_weights(g, LLVMBuildCondBr(b, at_nul, ov_chk, fmt_err), 2000, 1);

    LLVMPositionBuilderAtEnd(b, ov_chk);
    zan_set_branch_weights(g, LLVMBuildCondBr(b, ov, ov_err, ok_bb), 1, 2000);

    LLVMPositionBuilderAtEnd(b, ok_bb);
    LLVMValueRef neg_acc = zan_sub(b, zero64, acc, "neg.acc");
    LLVMValueRef res = LLVMBuildSelect(b, is_neg, neg_acc, acc, "res");
    LLVMBuildStore(b, res, out_val);
    LLVMBuildRet(b, LLVMConstInt(i32, 0, 0));

    LLVMPositionBuilderAtEnd(b, fmt_err);
    LLVMBuildRet(b, LLVMConstInt(i32, 1, 0));

    LLVMPositionBuilderAtEnd(b, ov_err);
    LLVMBuildRet(b, LLVMConstInt(i32, 2, 0));

    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}

/* 反向扫描 LastIndexOf：1 字节逆向扫描 + 多字节首尾字节快滤 + memcmp，
 * 替代从头循环调用标量 strstr 的 O(N*M) 路径，保留 strlen 内嵌 NUL 语义。 */
LLVMValueRef get_str_last_index_of_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_last_index_of");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef fnty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_last_index_of", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    {
        unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
        unsigned wr = LLVMGetEnumAttributeKindForName("willreturn", 10);
        unsigned mm = LLVMGetEnumAttributeKindForName("memory", 6);
        unsigned nc = LLVMGetEnumAttributeKindForName("nocapture", 9);
        if (nw) LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(c, nw, 0));
        if (wr) LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(c, wr, 0));
        if (mm) LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(c, mm, 0x01u));
        if (nc) {
            LLVMAddAttributeAtIndex(fn, 1, LLVMCreateEnumAttribute(c, nc, 0));
            LLVMAddAttributeAtIndex(fn, 2, LLVMCreateEnumAttribute(c, nc, 0));
        }
    }
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef memcmp_ty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef memcmp_fn = get_libc_fn(g, "memcmp", memcmp_ty);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef empty_bb = LLVMAppendBasicBlockInContext(c, fn, "empty");
    LLVMBasicBlockRef non_empty = LLVMAppendBasicBlockInContext(c, fn, "nonempty");
    LLVMBasicBlockRef single_loop = LLVMAppendBasicBlockInContext(c, fn, "single.loop");
    LLVMBasicBlockRef single_chk = LLVMAppendBasicBlockInContext(c, fn, "single.chk");
    LLVMBasicBlockRef multi_prep = LLVMAppendBasicBlockInContext(c, fn, "multi.prep");
    LLVMBasicBlockRef multi_loop = LLVMAppendBasicBlockInContext(c, fn, "multi.loop");
    LLVMBasicBlockRef multi_last = LLVMAppendBasicBlockInContext(c, fn, "multi.last");
    LLVMBasicBlockRef multi_cmp = LLVMAppendBasicBlockInContext(c, fn, "multi.cmp");
    LLVMBasicBlockRef multi_dec = LLVMAppendBasicBlockInContext(c, fn, "multi.dec");
    LLVMBasicBlockRef ret_miss = LLVMAppendBasicBlockInContext(c, fn, "miss");

    LLVMValueRef zero64 = LLVMConstInt(i64, 0, 0);
    LLVMValueRef one64 = LLVMConstInt(i64, 1, 0);
    LLVMValueRef two64 = LLVMConstInt(i64, 2, 0);
    LLVMValueRef neg1 = LLVMConstInt(i64, (uint64_t)-1, 1);

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef sub = LLVMGetParam(fn, 1);
    LLVMValueRef slen = zan_call2(b, strlen_ty, g->fn_strlen, &s, 1, "slen");
    LLVMValueRef nlen = zan_call2(b, strlen_ty, g->fn_strlen, &sub, 1, "nlen");
    LLVMValueRef n_is0 = zan_icmp(b, LLVMIntEQ, nlen, zero64, "n0");
    LLVMBuildCondBr(b, n_is0, empty_bb, non_empty);

    LLVMPositionBuilderAtEnd(b, empty_bb);
    LLVMBuildRet(b, slen);

    LLVMPositionBuilderAtEnd(b, non_empty);
    LLVMValueRef too_long = zan_icmp(b, LLVMIntUGT, nlen, slen, "toolong");
    LLVMBasicBlockRef dispatch_bb = LLVMAppendBasicBlockInContext(c, fn, "dispatch");
    LLVMBuildCondBr(b, too_long, ret_miss, dispatch_bb);

    LLVMPositionBuilderAtEnd(b, dispatch_bb);
    LLVMValueRef sub0 = LLVMBuildLoad2(b, i8, sub, "sub0");
    LLVMValueRef start_i = zan_sub(b, slen, nlen, "start_i");
    LLVMValueRef n_is1 = zan_icmp(b, LLVMIntEQ, nlen, one64, "n1");
    LLVMBuildCondBr(b, n_is1, single_loop, multi_prep);

    /* 1 字节快速反向扫描 */
    LLVMPositionBuilderAtEnd(b, single_loop);
    LLVMValueRef si = LLVMBuildPhi(b, i64, "si");
    LLVMValueRef s_gep = LLVMBuildInBoundsGEP2(b, i8, s, &si, 1, "s.p");
    LLVMValueRef sc = LLVMBuildLoad2(b, i8, s_gep, "sc");
    LLVMValueRef smatch = zan_icmp(b, LLVMIntEQ, sc, sub0, "smatch");
    LLVMBasicBlockRef single_hit = LLVMAppendBasicBlockInContext(c, fn, "single.hit");
    LLVMBuildCondBr(b, smatch, single_hit, single_chk);

    LLVMPositionBuilderAtEnd(b, single_hit);
    LLVMBuildRet(b, si);

    LLVMPositionBuilderAtEnd(b, single_chk);
    LLVMValueRef si_zero = zan_icmp(b, LLVMIntEQ, si, zero64, "si0");
    LLVMValueRef si_next = zan_sub(b, si, one64, "si.dec");
    LLVMAddIncoming(si, (LLVMValueRef[]){ start_i, si_next },
                    (LLVMBasicBlockRef[]){ dispatch_bb, single_chk }, 2);
    LLVMBuildCondBr(b, si_zero, ret_miss, single_loop);

    /* 多字节反向扫描：首字节 + 尾字节快滤 + memcmp 中段 */
    LLVMPositionBuilderAtEnd(b, multi_prep);
    LLVMValueRef last_off = zan_sub(b, nlen, one64, "last_off");
    LLVMValueRef sub_last_p = LLVMBuildInBoundsGEP2(b, i8, sub, &last_off, 1, "sub.lp");
    LLVMValueRef sub_last = LLVMBuildLoad2(b, i8, sub_last_p, "sub.last");
    LLVMValueRef mid_len = zan_sub(b, nlen, two64, "mid_len");
    LLVMValueRef sub_mid = LLVMBuildInBoundsGEP2(b, i8, sub, &one64, 1, "sub.mid");
    LLVMBuildBr(b, multi_loop);

    LLVMPositionBuilderAtEnd(b, multi_loop);
    LLVMValueRef mi = LLVMBuildPhi(b, i64, "mi");
    LLVMValueRef mp0 = LLVMBuildInBoundsGEP2(b, i8, s, &mi, 1, "mp0");
    LLVMValueRef mc0 = LLVMBuildLoad2(b, i8, mp0, "mc0");
    LLVMValueRef m0_ok = zan_icmp(b, LLVMIntEQ, mc0, sub0, "m0.ok");
    LLVMBuildCondBr(b, m0_ok, multi_last, multi_dec);

    LLVMPositionBuilderAtEnd(b, multi_last);
    LLVMValueRef mpl = LLVMBuildInBoundsGEP2(b, i8, mp0, &last_off, 1, "mpl");
    LLVMValueRef mcl = LLVMBuildLoad2(b, i8, mpl, "mcl");
    LLVMValueRef ml_ok = zan_icmp(b, LLVMIntEQ, mcl, sub_last, "ml.ok");
    LLVMBuildCondBr(b, ml_ok, multi_cmp, multi_dec);

    LLVMPositionBuilderAtEnd(b, multi_cmp);
    LLVMValueRef s_mid = LLVMBuildInBoundsGEP2(b, i8, mp0, &one64, 1, "s.mid");
    LLVMValueRef cmp_r = zan_call2(b, memcmp_ty, memcmp_fn,
        (LLVMValueRef[]){ s_mid, sub_mid, mid_len }, 3, "cmpr");
    LLVMValueRef cmp_ok = zan_icmp(b, LLVMIntEQ, cmp_r, LLVMConstInt(i32, 0, 0), "cmp.ok");
    LLVMBasicBlockRef multi_hit = LLVMAppendBasicBlockInContext(c, fn, "multi.hit");
    LLVMBuildCondBr(b, cmp_ok, multi_hit, multi_dec);

    LLVMPositionBuilderAtEnd(b, multi_hit);
    LLVMBuildRet(b, mi);

    LLVMPositionBuilderAtEnd(b, multi_dec);
    LLVMValueRef mi_zero = zan_icmp(b, LLVMIntEQ, mi, zero64, "mi0");
    LLVMValueRef mi_next = zan_sub(b, mi, one64, "mi.dec");
    LLVMAddIncoming(mi, (LLVMValueRef[]){ start_i, mi_next },
                    (LLVMBasicBlockRef[]){ multi_prep, multi_dec }, 2);
    LLVMBuildCondBr(b, mi_zero, ret_miss, multi_loop);

    LLVMPositionBuilderAtEnd(b, ret_miss);
    LLVMBuildRet(b, neg1);

    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}
LLVMValueRef get_str_trim_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_trim");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_trim", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    {
        unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
        if (nw) LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex,
                                        LLVMCreateEnumAttribute(c, nw, 0));
    }
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef lead = LLVMAppendBasicBlockInContext(c, fn, "lead");
    LLVMBasicBlockRef lead_ws = LLVMAppendBasicBlockInContext(c, fn, "lead_ws");
    LLVMBasicBlockRef tail_init = LLVMAppendBasicBlockInContext(c, fn, "tail_init");
    LLVMBasicBlockRef tail = LLVMAppendBasicBlockInContext(c, fn, "tail");
    LLVMBasicBlockRef tail_chk = LLVMAppendBasicBlockInContext(c, fn, "tail_chk");
    LLVMBasicBlockRef tail_ws = LLVMAppendBasicBlockInContext(c, fn, "tail_ws");
    LLVMBasicBlockRef fin_chk = LLVMAppendBasicBlockInContext(c, fn, "fin_chk");
    LLVMBasicBlockRef ret_empty = LLVMAppendBasicBlockInContext(c, fn, "ret_empty");
    LLVMBasicBlockRef chk_same = LLVMAppendBasicBlockInContext(c, fn, "chk_same");
    LLVMBasicBlockRef chk_nul = LLVMAppendBasicBlockInContext(c, fn, "chk_nul");
    LLVMBasicBlockRef ret_same = LLVMAppendBasicBlockInContext(c, fn, "ret_same");
    LLVMBasicBlockRef copy = LLVMAppendBasicBlockInContext(c, fn, "copy");

    LLVMValueRef zero64 = LLVMConstInt(i64, 0, 0);
    LLVMValueRef one64 = LLVMConstInt(i64, 1, 0);

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef ls = zan_call2(b, strlen_ty, g->fn_strlen, &s, 1, "ls");
    LLVMBuildBr(b, lead);

    LLVMPositionBuilderAtEnd(b, lead);
    LLVMValueRef off = LLVMBuildPhi(b, i64, "off");
    LLVMValueRef off_lt = zan_icmp(b, LLVMIntULT, off, ls, "off.lt");
    LLVMBasicBlockRef lead_chk = LLVMAppendBasicBlockInContext(c, fn, "lead_chk");
    LLVMBuildCondBr(b, off_lt, lead_chk, ret_empty);

    LLVMPositionBuilderAtEnd(b, lead_chk);
    LLVMValueRef p = LLVMBuildInBoundsGEP2(b, i8, s, &off, 1, "p");
    LLVMValueRef ch = LLVMBuildLoad2(b, i8, p, "c");
    LLVMValueRef is_sp = zan_icmp(b, LLVMIntEQ, ch, LLVMConstInt(i8, 32, 0), "is_sp");
    LLVMValueRef c_sub_9 = zan_sub(b, ch, LLVMConstInt(i8, 9, 0), "c_sub");
    LLVMValueRef is_ctl_ws = zan_icmp(b, LLVMIntULE, c_sub_9, LLVMConstInt(i8, 4, 0), "is_ctl");
    LLVMValueRef nz = zan_or(b, is_sp, is_ctl_ws, "isws");
    LLVMBuildCondBr(b, nz, lead_ws, tail_init);

    LLVMPositionBuilderAtEnd(b, lead_ws);
    LLVMValueRef off1 = zan_add(b, off, one64, "off1");
    LLVMBuildBr(b, lead);
    LLVMAddIncoming(off, (LLVMValueRef[]){ zero64, off1 },
                    (LLVMBasicBlockRef[]){ entry, lead_ws }, 2);

    LLVMPositionBuilderAtEnd(b, tail_init);
    LLVMValueRef n0 = zan_sub(b, ls, off, "n0");
    LLVMBuildBr(b, tail);

    LLVMPositionBuilderAtEnd(b, tail);
    LLVMValueRef n = LLVMBuildPhi(b, i64, "n");
    LLVMValueRef npos = zan_icmp(b, LLVMIntSGT, n, zero64, "npos");
    LLVMBuildCondBr(b, npos, tail_chk, fin_chk);

    LLVMPositionBuilderAtEnd(b, tail_chk);
    LLVMValueRef nm1 = zan_sub(b, n, one64, "nm1");
    LLVMValueRef lastp = LLVMBuildInBoundsGEP2(b, i8, p, &nm1, 1, "lastp");
    LLVMValueRef lc = LLVMBuildLoad2(b, i8, lastp, "lc");
    LLVMValueRef lis_sp = zan_icmp(b, LLVMIntEQ, lc, LLVMConstInt(i8, 32, 0), "lis_sp");
    LLVMValueRef lc_sub_9 = zan_sub(b, lc, LLVMConstInt(i8, 9, 0), "lc_sub");
    LLVMValueRef lis_ctl_ws = zan_icmp(b, LLVMIntULE, lc_sub_9, LLVMConstInt(i8, 4, 0), "lis_ctl");
    LLVMValueRef lnz = zan_or(b, lis_sp, lis_ctl_ws, "lisws");
    LLVMBuildCondBr(b, lnz, tail_ws, fin_chk);

    LLVMPositionBuilderAtEnd(b, tail_ws);
    LLVMBuildBr(b, tail);
    LLVMAddIncoming(n, (LLVMValueRef[]){ n0, nm1 },
                    (LLVMBasicBlockRef[]){ tail_init, tail_ws }, 2);

    /* 空结果直接返回不朽空串；未裁切且无内嵌 NUL 直接 retain 返回（零分配） */
    LLVMPositionBuilderAtEnd(b, fin_chk);
    LLVMValueRef is_zero = zan_icmp(b, LLVMIntEQ, n, zero64, "finz");
    LLVMBuildCondBr(b, is_zero, ret_empty, chk_same);

    LLVMPositionBuilderAtEnd(b, ret_empty);
    LLVMBuildRet(b, emit_string_literal_rc(g, (zan_istr_t){ "", 0 }));

    LLVMPositionBuilderAtEnd(b, chk_same);
    LLVMValueRef unchanged = zan_and(b,
        zan_icmp(b, LLVMIntEQ, off, zero64, "off0"),
        zan_icmp(b, LLVMIntEQ, n, ls, "nfull"), "unch");
    LLVMBuildCondBr(b, unchanged, chk_nul, copy);

    LLVMPositionBuilderAtEnd(b, chk_nul);
    LLVMValueRef hlen = emit_string_length(g, s, (zan_loc_t){0});
    LLVMValueRef no_nul = zan_icmp(b, LLVMIntEQ, hlen, ls, "nonul");
    LLVMBuildCondBr(b, no_nul, ret_same, copy);

    LLVMPositionBuilderAtEnd(b, ret_same);
    emit_string_retain(g, s);
    LLVMBuildRet(b, s);

    LLVMPositionBuilderAtEnd(b, copy);
    LLVMValueRef bufsz = zan_add(b, n, one64, "bsz");
    LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
    LLVMValueRef mcargs[] = { buf, p, n };
    zan_call2(b, memcpy_ty, memcpy_fn, mcargs, 3, "");
    LLVMValueRef endp = LLVMBuildInBoundsGEP2(b, i8, buf, &n, 1, "endp");
    LLVMBuildStore(b, LLVMConstInt(i8, 0, 0), endp);
    emit_string_len_set(g, buf, n);
    LLVMBuildRet(b, buf);

    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}
LLVMValueRef get_str_case_fn(zan_irgen_t *g, int upper) {
    const char *fname = upper ? "__zan_str_to_upper" : "__zan_str_to_lower";
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fname);
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    fn = LLVMAddFunction(g->mod, fname, fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    {
        unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
        if (nw) LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex,
                                        LLVMCreateEnumAttribute(c, nw, 0));
    }
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef empty_bb = LLVMAppendBasicBlockInContext(c, fn, "empty");
    LLVMBasicBlockRef alloc_bb = LLVMAppendBasicBlockInContext(c, fn, "alloc");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(c, fn, "loop");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(c, fn, "done");

    LLVMValueRef zero64 = LLVMConstInt(i64, 0, 0);
    LLVMValueRef one64 = LLVMConstInt(i64, 1, 0);

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef n = zan_call2(b, strlen_ty, g->fn_strlen, &s, 1, "n");
    LLVMValueRef is_empty = zan_icmp(b, LLVMIntEQ, n, zero64, "isz");
    LLVMBuildCondBr(b, is_empty, empty_bb, alloc_bb);

    LLVMPositionBuilderAtEnd(b, empty_bb);
    LLVMBuildRet(b, emit_string_literal_rc(g, (zan_istr_t){ "", 0 }));

    LLVMPositionBuilderAtEnd(b, alloc_bb);
    LLVMValueRef bufsz = zan_add(b, n, one64, "bsz");
    LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
    LLVMBasicBlockRef alloc_end = LLVMGetInsertBlock(b);
    LLVMBuildBr(b, loop);

    /* 纯 SSA PHI + inbounds GEP + 无符号范围差值，便于 LLVM AVX2 向量化 */
    LLVMPositionBuilderAtEnd(b, loop);
    LLVMValueRef i = LLVMBuildPhi(b, i64, "i");
    LLVMValueRef sp = LLVMBuildInBoundsGEP2(b, i8, s, &i, 1, "sp");
    LLVMValueRef ch = LLVMBuildLoad2(b, i8, sp, "c");
    LLVMValueRef low_b = LLVMConstInt(i8, upper ? 'a' : 'A', 0);
    LLVMValueRef sub_c = zan_sub(b, ch, low_b, "subc");
    LLVMValueRef in_rng = zan_icmp(b, LLVMIntULE, sub_c, LLVMConstInt(i8, 25, 0), "in_rng");
    LLVMValueRef conv = LLVMBuildXor(b, ch, LLVMConstInt(i8, 32, 0), "conv");
    LLVMValueRef c8 = LLVMBuildSelect(b, in_rng, conv, ch, "c8");
    LLVMValueRef dp = LLVMBuildInBoundsGEP2(b, i8, buf, &i, 1, "dp");
    LLVMBuildStore(b, c8, dp);
    LLVMValueRef i1v = zan_add(b, i, one64, "i1");
    LLVMValueRef more = zan_icmp(b, LLVMIntULT, i1v, n, "more");
    LLVMBuildCondBr(b, more, loop, done);
    LLVMAddIncoming(i, (LLVMValueRef[]){ zero64, i1v },
                    (LLVMBasicBlockRef[]){ alloc_end, loop }, 2);

    LLVMPositionBuilderAtEnd(b, done);
    LLVMValueRef endp = LLVMBuildInBoundsGEP2(b, i8, buf, &n, 1, "endp");
    LLVMBuildStore(b, LLVMConstInt(i8, 0, 0), endp);
    emit_string_len_set(g, buf, n);
    LLVMBuildRet(b, buf);

    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}

/* SIMD memchr 驱动的子串搜索（首字节 memchr + 尾字节快拒 + memcmp 中段），
 * 替代 Windows UCRT 标量 strstr，对大文档检索与替换提速 10x+。 */
static LLVMValueRef get_memmem_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_memmem");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64, i8ptr, i64 }, 4, 0);
    fn = LLVMAddFunction(g->mod, "__zan_memmem", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    {
        unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
        if (nw) LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(c, nw, 0));
    }
    LLVMTypeRef memchr_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32, i64 }, 3, 0);
    LLVMTypeRef memcmp_ty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef memchr_fn = get_libc_fn(g, "memchr", memchr_ty);
    LLVMValueRef memcmp_fn = get_libc_fn(g, "memcmp", memcmp_ty);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef ret_hay = LLVMAppendBasicBlockInContext(c, fn, "ret.hay");
    LLVMBasicBlockRef chk_len = LLVMAppendBasicBlockInContext(c, fn, "chk.len");
    LLVMBasicBlockRef ret_null = LLVMAppendBasicBlockInContext(c, fn, "ret.null");
    LLVMBasicBlockRef chk_n1 = LLVMAppendBasicBlockInContext(c, fn, "chk.n1");
    LLVMBasicBlockRef do_n1 = LLVMAppendBasicBlockInContext(c, fn, "do.n1");
    LLVMBasicBlockRef prep = LLVMAppendBasicBlockInContext(c, fn, "prep");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(c, fn, "loop");
    LLVMBasicBlockRef scan = LLVMAppendBasicBlockInContext(c, fn, "scan");
    LLVMBasicBlockRef chk_last = LLVMAppendBasicBlockInContext(c, fn, "chk.last");
    LLVMBasicBlockRef chk_mid_len = LLVMAppendBasicBlockInContext(c, fn, "chk.mlen");
    LLVMBasicBlockRef chk_mid = LLVMAppendBasicBlockInContext(c, fn, "chk.mid");
    LLVMBasicBlockRef ret_hit = LLVMAppendBasicBlockInContext(c, fn, "ret.hit");
    LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(c, fn, "next");

    LLVMValueRef hay = LLVMGetParam(fn, 0);
    LLVMValueRef hlen = LLVMGetParam(fn, 1);
    LLVMValueRef needle = LLVMGetParam(fn, 2);
    LLVMValueRef nlen = LLVMGetParam(fn, 3);
    LLVMValueRef z64 = LLVMConstInt(i64, 0, 0);
    LLVMValueRef one64 = LLVMConstInt(i64, 1, 0);
    LLVMValueRef two64 = LLVMConstInt(i64, 2, 0);

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, nlen, z64, "n0"), ret_hay, chk_len);

    LLVMPositionBuilderAtEnd(b, ret_hay);
    LLVMBuildRet(b, hay);

    LLVMPositionBuilderAtEnd(b, chk_len);
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntULT, hlen, nlen, "short"), ret_null, chk_n1);

    LLVMPositionBuilderAtEnd(b, ret_null);
    LLVMBuildRet(b, LLVMConstPointerNull(i8ptr));

    LLVMPositionBuilderAtEnd(b, chk_n1);
    LLVMValueRef c0 = LLVMBuildZExt(b, LLVMBuildLoad2(b, i8, needle, "c0.b"), i32, "c0");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, nlen, one64, "n1"), do_n1, prep);

    LLVMPositionBuilderAtEnd(b, do_n1);
    LLVMValueRef hit1 = zan_call2(b, memchr_ty, memchr_fn,
        (LLVMValueRef[]){ hay, c0, hlen }, 3, "hit1");
    LLVMBuildRet(b, hit1);

    LLVMPositionBuilderAtEnd(b, prep);
    LLVMValueRef last_idx = zan_sub(b, nlen, one64, "lidx");
    LLVMValueRef clast = LLVMBuildLoad2(b, i8,
        LLVMBuildInBoundsGEP2(b, i8, needle, &last_idx, 1, "lp"), "clast");
    LLVMValueRef mid_len = zan_sub(b, nlen, two64, "mlen");
    LLVMValueRef n_plus1 = LLVMBuildInBoundsGEP2(b, i8, needle, &one64, 1, "np1");
    LLVMValueRef max_off = zan_add(b, zan_sub(b, hlen, nlen, "diff"), one64, "moff");
    LLVMValueRef end_i = zan_add(b, LLVMBuildPtrToInt(b, hay, i64, "hi"), max_off, "endi");
    LLVMValueRef mid_is0 = zan_icmp(b, LLVMIntEQ, mid_len, z64, "m0");
    LLVMBuildBr(b, loop);

    LLVMPositionBuilderAtEnd(b, loop);
    LLVMValueRef p = LLVMBuildPhi(b, i8ptr, "p");
    LLVMValueRef rem = zan_sub(b, end_i, LLVMBuildPtrToInt(b, p, i64, "pi"), "rem");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntSLE, rem, z64, "done"), ret_null, scan);

    LLVMPositionBuilderAtEnd(b, scan);
    LLVMValueRef hit = zan_call2(b, memchr_ty, memchr_fn,
        (LLVMValueRef[]){ p, c0, rem }, 3, "hit");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, hit, LLVMConstPointerNull(i8ptr), "miss"),
                    ret_null, chk_last);

    LLVMPositionBuilderAtEnd(b, chk_last);
    LLVMValueRef hl = LLVMBuildLoad2(b, i8,
        LLVMBuildInBoundsGEP2(b, i8, hit, &last_idx, 1, "hlp"), "hl");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, hl, clast, "lok"), chk_mid_len, next);

    LLVMPositionBuilderAtEnd(b, chk_mid_len);
    LLVMBuildCondBr(b, mid_is0, ret_hit, chk_mid);

    LLVMPositionBuilderAtEnd(b, chk_mid);
    LLVMValueRef h_plus1 = LLVMBuildInBoundsGEP2(b, i8, hit, &one64, 1, "hp1");
    LLVMValueRef cmp = zan_call2(b, memcmp_ty, memcmp_fn,
        (LLVMValueRef[]){ h_plus1, n_plus1, mid_len }, 3, "cmp");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, cmp, LLVMConstInt(i32, 0, 0), "mok"),
                    ret_hit, next);

    LLVMPositionBuilderAtEnd(b, ret_hit);
    LLVMBuildRet(b, hit);

    LLVMPositionBuilderAtEnd(b, next);
    LLVMValueRef p_next = LLVMBuildInBoundsGEP2(b, i8, hit, &one64, 1, "pnext");
    LLVMBuildBr(b, loop);

    LLVMAddIncoming(p, (LLVMValueRef[]){ hay, p_next },
                    (LLVMBasicBlockRef[]){ prep, next }, 2);

    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}

/* 单字节非 NUL 查找（memchr + 命中前段 NUL 拦截，严格 O(N) 且保内嵌 NUL 语义） */
LLVMValueRef get_str_find_ch_from_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_find_ch_from");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef fnty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i32 }, 4, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_find_ch_from", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    {
        unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
        if (nw) LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(c, nw, 0));
    }
    LLVMTypeRef memchr_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32, i64 }, 3, 0);
    LLVMValueRef memchr_fn = get_libc_fn(g, "memchr", memchr_ty);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef ret_neg = LLVMAppendBasicBlockInContext(c, fn, "ret.neg");
    LLVMBasicBlockRef scan = LLVMAppendBasicBlockInContext(c, fn, "scan");
    LLVMBasicBlockRef chk_gap = LLVMAppendBasicBlockInContext(c, fn, "chk.gap");
    LLVMBasicBlockRef chk_nul = LLVMAppendBasicBlockInContext(c, fn, "chk.nul");
    LLVMBasicBlockRef ret_ok = LLVMAppendBasicBlockInContext(c, fn, "ret.ok");

    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef slen = LLVMGetParam(fn, 1);
    LLVMValueRef start = LLVMGetParam(fn, 2);
    LLVMValueRef ch = LLVMGetParam(fn, 3);
    LLVMValueRef z64 = LLVMConstInt(i64, 0, 0);

    LLVMPositionBuilderAtEnd(b, entry);
    zan_set_branch_weights(g,
        LLVMBuildCondBr(b, zan_icmp(b, LLVMIntULE, start, slen, "valid"), scan, ret_neg),
        2000, 1);

    LLVMPositionBuilderAtEnd(b, ret_neg);
    LLVMBuildRet(b, LLVMConstInt(i64, (uint64_t)-1, 1));

    LLVMPositionBuilderAtEnd(b, scan);
    LLVMValueRef search = LLVMBuildInBoundsGEP2(b, i8, s, &start, 1, "search");
    LLVMValueRef rem = zan_sub(b, slen, start, "rem");
    LLVMValueRef hit = zan_call2(b, memchr_ty, memchr_fn,
        (LLVMValueRef[]){ search, ch, rem }, 3, "hit");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, hit, LLVMConstPointerNull(i8ptr), "miss"),
                    ret_neg, chk_gap);

    LLVMPositionBuilderAtEnd(b, chk_gap);
    LLVMValueRef hit_i = LLVMBuildPtrToInt(b, hit, i64, "hi");
    LLVMValueRef gap = zan_sub(b, hit_i, LLVMBuildPtrToInt(b, search, i64, "sei"), "gap");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntUGT, gap, z64, "hgap"), chk_nul, ret_ok);

    LLVMPositionBuilderAtEnd(b, chk_nul);
    LLVMValueRef nul = zan_call2(b, memchr_ty, memchr_fn,
        (LLVMValueRef[]){ search, LLVMConstInt(i32, 0, 0), gap }, 3, "nul");
    zan_set_branch_weights(g,
        LLVMBuildCondBr(b, zan_icmp(b, LLVMIntNE, nul, LLVMConstPointerNull(i8ptr), "hnul"),
                        ret_neg, ret_ok),
        1, 2000);

    LLVMPositionBuilderAtEnd(b, ret_ok);
    LLVMValueRef diff = zan_sub(b, hit_i, LLVMBuildPtrToInt(b, s, i64, "si"), "diff");
    LLVMBuildRet(b, diff);

    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}

/* 多字节子串查找（__zan_memmem + 命中前段 NUL 拦截，严格 O(N) 且保内嵌 NUL 语义） */
LLVMValueRef get_str_find_from_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_find_from");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef fnty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_find_from", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    {
        unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
        if (nw) LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(c, nw, 0));
    }
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef memchr_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32, i64 }, 3, 0);
    LLVMValueRef memchr_fn = get_libc_fn(g, "memchr", memchr_ty);
    LLVMValueRef memmem_fn = get_memmem_fn(g);
    LLVMTypeRef memmem_ty = LLVMGlobalGetValueType(memmem_fn);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef ret_neg = LLVMAppendBasicBlockInContext(c, fn, "ret.neg");
    LLVMBasicBlockRef chk_sub = LLVMAppendBasicBlockInContext(c, fn, "chk.sub");
    LLVMBasicBlockRef ret_start = LLVMAppendBasicBlockInContext(c, fn, "ret.start");
    LLVMBasicBlockRef scan = LLVMAppendBasicBlockInContext(c, fn, "scan");
    LLVMBasicBlockRef chk_gap = LLVMAppendBasicBlockInContext(c, fn, "chk.gap");
    LLVMBasicBlockRef chk_nul = LLVMAppendBasicBlockInContext(c, fn, "chk.nul");
    LLVMBasicBlockRef ret_ok = LLVMAppendBasicBlockInContext(c, fn, "ret.ok");

    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef slen = LLVMGetParam(fn, 1);
    LLVMValueRef start = LLVMGetParam(fn, 2);
    LLVMValueRef sub = LLVMGetParam(fn, 3);
    LLVMValueRef z64 = LLVMConstInt(i64, 0, 0);

    LLVMPositionBuilderAtEnd(b, entry);
    zan_set_branch_weights(g,
        LLVMBuildCondBr(b, zan_icmp(b, LLVMIntULE, start, slen, "valid"), chk_sub, ret_neg),
        2000, 1);

    LLVMPositionBuilderAtEnd(b, ret_neg);
    LLVMBuildRet(b, LLVMConstInt(i64, (uint64_t)-1, 1));

    LLVMPositionBuilderAtEnd(b, chk_sub);
    LLVMValueRef nlen = zan_call2(b, strlen_ty, g->fn_strlen, &sub, 1, "nlen");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, nlen, z64, "empty"), ret_start, scan);

    LLVMPositionBuilderAtEnd(b, ret_start);
    LLVMBuildRet(b, start);

    LLVMPositionBuilderAtEnd(b, scan);
    LLVMValueRef search = LLVMBuildInBoundsGEP2(b, i8, s, &start, 1, "search");
    LLVMValueRef rem = zan_sub(b, slen, start, "rem");
    LLVMValueRef hit = zan_call2(b, memmem_ty, memmem_fn,
        (LLVMValueRef[]){ search, rem, sub, nlen }, 4, "hit");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, hit, LLVMConstPointerNull(i8ptr), "miss"),
                    ret_neg, chk_gap);

    LLVMPositionBuilderAtEnd(b, chk_gap);
    LLVMValueRef hit_i = LLVMBuildPtrToInt(b, hit, i64, "hi");
    LLVMValueRef gap = zan_sub(b, hit_i, LLVMBuildPtrToInt(b, search, i64, "sei"), "gap");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntUGT, gap, z64, "hgap"), chk_nul, ret_ok);

    LLVMPositionBuilderAtEnd(b, chk_nul);
    LLVMValueRef nul = zan_call2(b, memchr_ty, memchr_fn,
        (LLVMValueRef[]){ search, LLVMConstInt(i32, 0, 0), gap }, 3, "nul");
    zan_set_branch_weights(g,
        LLVMBuildCondBr(b, zan_icmp(b, LLVMIntNE, nul, LLVMConstPointerNull(i8ptr), "hnul"),
                        ret_neg, ret_ok),
        1, 2000);

    LLVMPositionBuilderAtEnd(b, ret_ok);
    LLVMValueRef diff = zan_sub(b, hit_i, LLVMBuildPtrToInt(b, s, i64, "si"), "diff");
    LLVMBuildRet(b, diff);

    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}

/* string.GetHashCode()：与 object.GetHashCode() 字符串分支（djb2 5381*33+b，NUL 截止）严格同口径 */
LLVMValueRef get_str_hashcode_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_hashcode");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef fnty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_hashcode", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    {
        unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
        if (nw) LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(c, nw, 0));
    }

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef ret_zero = LLVMAppendBasicBlockInContext(c, fn, "ret.z");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(c, fn, "loop");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(c, fn, "body");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(c, fn, "done");

    LLVMValueRef s = LLVMGetParam(fn, 0);

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, s, LLVMConstPointerNull(i8ptr), "isnull"),
                    ret_zero, loop);

    LLVMPositionBuilderAtEnd(b, ret_zero);
    LLVMBuildRet(b, LLVMConstInt(i32, 0, 0));

    LLVMPositionBuilderAtEnd(b, loop);
    LLVMValueRef h = LLVMBuildPhi(b, i64, "h");
    LLVMValueRef idx = LLVMBuildPhi(b, i64, "i");
    LLVMValueRef ch = LLVMBuildLoad2(b, i8,
        LLVMBuildInBoundsGEP2(b, i8, s, &idx, 1, "chp"), "ch");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, ch, LLVMConstInt(i8, 0, 0), "ez"),
                    done, body);

    LLVMPositionBuilderAtEnd(b, body);
    LLVMValueRef h2 = zan_add(b,
        LLVMBuildMul(b, h, LLVMConstInt(i64, 33, 0), "h33"),
        LLVMBuildZExt(b, ch, i64, "ch64"), "h2");
    LLVMValueRef idx2 = zan_add(b, idx, LLVMConstInt(i64, 1, 0), "i2");
    LLVMBuildBr(b, loop);

    LLVMAddIncoming(h, (LLVMValueRef[]){ LLVMConstInt(i64, 5381, 0), h2 },
                    (LLVMBasicBlockRef[]){ entry, body }, 2);
    LLVMAddIncoming(idx, (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), idx2 },
                    (LLVMBasicBlockRef[]){ entry, body }, 2);

    LLVMPositionBuilderAtEnd(b, done);
    LLVMBuildRet(b, LLVMBuildTrunc(b, h, i32, "h32"));

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
    /* 记长度：切出的片段以后 .Length / IndexOf 直接读头，不再 strlen。 */
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

