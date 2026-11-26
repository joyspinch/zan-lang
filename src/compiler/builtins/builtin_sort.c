/* 原始缓冲零 ARC / 零重排检查 / 零越界检查的内省排序（Introsort）IR 生成器。
 * 统一服务 List<T>.Sort()、List<T>.Sort(Comparison<T>) 与 Array.Sort(T[])：
 *   - 入口只做一次 list_data_for / 长度读取，内部直接操作原始 T* 缓冲；
 *   - string/类/接口排序通过纯置换（不增减引用计数）彻底消除每趟 swap 的 6 次原子操作；
 *   - 自定义比较器排序在指针元素插入排序阶段使用相邻交换，确保任意 cmp 抛异常时数组始终是
 *     完整置换（零泄漏、零双释）；
 *   - 三数取中 + Hoare 划分 + 小段插入排序（<=16）+ 深度超限堆排序兜底（最坏 O(N log N)）。 */

static void sort_add_fn_attr(zan_irgen_t *g, LLVMValueRef fn, const char *name) {
    unsigned k = LLVMGetEnumAttributeKindForName(name, strlen(name));
    if (k)
        LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                                LLVMCreateEnumAttribute(g->ctx, k, 0));
}

static LLVMTypeRef sort_elem_type(zan_irgen_t *g, int kind) {
    switch (kind) {
    case ZAN_SORT_I32:
    case ZAN_SORT_CMP_I32:
        return LLVMInt32TypeInContext(g->ctx);
    case ZAN_SORT_I64:
    case ZAN_SORT_CMP_I64:
        return LLVMInt64TypeInContext(g->ctx);
    case ZAN_SORT_F64:
    case ZAN_SORT_CMP_F64:
        return LLVMDoubleTypeInContext(g->ctx);
    default: /* ZAN_SORT_STR, ZAN_SORT_CMP_PTR */
        return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    }
}

static bool sort_is_cmp(int kind) {
    return kind >= ZAN_SORT_CMP_I32;
}

static const char *sort_kind_tag(int kind) {
    switch (kind) {
    case ZAN_SORT_I32:     return "i32";
    case ZAN_SORT_I64:     return "i64";
    case ZAN_SORT_F64:     return "f64";
    case ZAN_SORT_STR:     return "str";
    case ZAN_SORT_CMP_I32: return "cmp_i32";
    case ZAN_SORT_CMP_I64: return "cmp_i64";
    case ZAN_SORT_CMP_F64: return "cmp_f64";
    default:               return "cmp_ptr";
    }
}

/* 内联优先的字符串词典序小于（a < b）：指针相等→null 守卫→首字节快路径→strcmp(a+1,b+1)<0 */
static LLVMValueRef get_sort_str_lt_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_sort_str_lt");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i1 = LLVMInt1TypeInContext(c);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef fnty = LLVMFunctionType(i1, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_sort_str_lt", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    sort_add_fn_attr(g, fn, "nounwind");
    sort_add_fn_attr(g, fn, "inlinehint");

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef anull = LLVMAppendBasicBlockInContext(c, fn, "anull");
    LLVMBasicBlockRef bnull = LLVMAppendBasicBlockInContext(c, fn, "bnull");
    LLVMBasicBlockRef b0_bb = LLVMAppendBasicBlockInContext(c, fn, "b0");
    LLVMBasicBlockRef slow  = LLVMAppendBasicBlockInContext(c, fn, "slow");
    LLVMBasicBlockRef done  = LLVMAppendBasicBlockInContext(c, fn, "done");

    LLVMValueRef pa = LLVMGetParam(fn, 0);
    LLVMValueRef pb = LLVMGetParam(fn, 1);

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntEQ, pa, pb, "peq"), done, anull);

    LLVMPositionBuilderAtEnd(b, anull);
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntEQ, pa, LLVMConstNull(i8ptr), "an"),
                    done, bnull);

    LLVMPositionBuilderAtEnd(b, bnull);
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntEQ, pb, LLVMConstNull(i8ptr), "bn"),
                    done, b0_bb);

    LLVMPositionBuilderAtEnd(b, b0_bb);
    LLVMValueRef c0 = LLVMBuildLoad2(b, i8, pa, "c0");
    LLVMValueRef c1 = LLVMBuildLoad2(b, i8, pb, "c1");
    LLVMValueRef ne = LLVMBuildICmp(b, LLVMIntNE, c0, c1, "ne");
    LLVMValueRef z0 = LLVMBuildICmp(b, LLVMIntEQ, c0, LLVMConstInt(i8, 0, 0), "z0");
    LLVMValueRef b0_lt = LLVMBuildICmp(b, LLVMIntULT, c0, c1, "b0lt");
    LLVMBuildCondBr(b, LLVMBuildOr(b, ne, z0, "q"), done, slow);

    LLVMPositionBuilderAtEnd(b, slow);
    LLVMValueRef one = LLVMConstInt(i64, 1, 0);
    LLVMValueRef a1 = LLVMBuildInBoundsGEP2(b, i8, pa, &one, 1, "a1");
    LLVMValueRef b1 = LLVMBuildInBoundsGEP2(b, i8, pb, &one, 1, "b1");
    LLVMTypeRef strcmp_ty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMValueRef strcmp_fn = get_libc_fn(g, "strcmp", strcmp_ty);
    LLVMValueRef cr = zan_call2(b, strcmp_ty, strcmp_fn, (LLVMValueRef[]){ a1, b1 }, 2, "cr");
    LLVMValueRef slt = LLVMBuildICmp(b, LLVMIntSLT, cr, LLVMConstInt(i32, 0, 0), "slt");
    LLVMBuildBr(b, done);

    LLVMPositionBuilderAtEnd(b, done);
    LLVMValueRef res = LLVMBuildPhi(b, i1, "res");
    LLVMValueRef f_v = LLVMConstInt(i1, 0, 0);
    LLVMValueRef t_v = LLVMConstInt(i1, 1, 0);
    LLVMAddIncoming(res, (LLVMValueRef[]){ f_v, t_v, f_v, b0_lt, slt },
                    (LLVMBasicBlockRef[]){ entry, anull, bnull, b0_bb, slow }, 5);
    LLVMBuildRet(b, res);

    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}

static LLVMValueRef emit_sort_lt(zan_irgen_t *g, int kind, LLVMValueRef a,
                                 LLVMValueRef b_val, LLVMValueRef cmp) {
    LLVMBuilderRef b = g->builder;
    switch (kind) {
    case ZAN_SORT_I32:
    case ZAN_SORT_I64:
        return LLVMBuildICmp(b, LLVMIntSLT, a, b_val, "lt");
    case ZAN_SORT_F64:
        return LLVMBuildFCmp(b, LLVMRealOLT, a, b_val, "lt");
    case ZAN_SORT_STR: {
        LLVMValueRef sfn = get_sort_str_lt_fn(g);
        return zan_call2(b, LLVMGlobalGetValueType(sfn), sfn,
                         (LLVMValueRef[]){ a, b_val }, 2, "lt");
    }
    default: {
        LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef et = sort_elem_type(g, kind);
        LLVMTypeRef cmp_ty = LLVMFunctionType(i32, (LLVMTypeRef[]){ et, et }, 2, 0);
        LLVMValueRef r = emit_delegate_invoke(g, cmp, cmp_ty, i32,
                                              (LLVMValueRef[]){ a, b_val }, 2, "scmp");
        return LLVMBuildICmp(b, LLVMIntSLT, r, LLVMConstInt(i32, 0, 0), "lt");
    }
    }
}

static void emit_sort_swap(LLVMBuilderRef b, LLVMTypeRef et, LLVMValueRef data,
                           LLVMValueRef idx_a, LLVMValueRef idx_b) {
    LLVMValueRef pa = LLVMBuildInBoundsGEP2(b, et, data, &idx_a, 1, "swa");
    LLVMValueRef pb = LLVMBuildInBoundsGEP2(b, et, data, &idx_b, 1, "swb");
    LLVMValueRef va = LLVMBuildLoad2(b, et, pa, "va");
    LLVMValueRef vb = LLVMBuildLoad2(b, et, pb, "vb");
    LLVMBuildStore(b, vb, pa);
    LLVMBuildStore(b, va, pb);
}

/* 冷路径堆排序兜底（深度耗尽时保证最坏 O(N log N)） */
static LLVMValueRef get_sort_heap_fn(zan_irgen_t *g, int kind) {
    char fname[64];
    snprintf(fname, sizeof(fname), "__zan_sort_%s_heap", sort_kind_tag(kind));
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fname);
    if (fn) return fn;

    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef voidt = LLVMVoidTypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef et = sort_elem_type(g, kind);
    LLVMTypeRef eptr = LLVMPointerType(et, 0);
    bool is_cmp = sort_is_cmp(kind);

    LLVMTypeRef params[4] = { eptr, i64, i64, i8ptr };
    LLVMTypeRef fnty = LLVMFunctionType(voidt, params, is_cmp ? 4 : 3, 0);
    fn = LLVMAddFunction(g->mod, fname, fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    sort_add_fn_attr(g, fn, "noinline");
    sort_add_fn_attr(g, fn, "cold");
    if (!is_cmp) sort_add_fn_attr(g, fn, "nounwind");

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;

    LLVMValueRef data = LLVMGetParam(fn, 0);
    LLVMValueRef lo = LLVMGetParam(fn, 1);
    LLVMValueRef hi = LLVMGetParam(fn, 2);
    LLVMValueRef cmp = is_cmp ? LLVMGetParam(fn, 3) : NULL;
    LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
    LLVMValueRef one = LLVMConstInt(i64, 1, 0);
    LLVMValueRef two = LLVMConstInt(i64, 2, 0);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef h_cond = LLVMAppendBasicBlockInContext(c, fn, "h.cond");
    LLVMBasicBlockRef h_sift = LLVMAppendBasicBlockInContext(c, fn, "h.sift");
    LLVMBasicBlockRef h_chk_r = LLVMAppendBasicBlockInContext(c, fn, "h.chkr");
    LLVMBasicBlockRef h_cmp_r = LLVMAppendBasicBlockInContext(c, fn, "h.cmpr");
    LLVMBasicBlockRef h_pick_r = LLVMAppendBasicBlockInContext(c, fn, "h.pickr");
    LLVMBasicBlockRef h_cmp_p = LLVMAppendBasicBlockInContext(c, fn, "h.cmpp");
    LLVMBasicBlockRef h_swap = LLVMAppendBasicBlockInContext(c, fn, "h.swap");
    LLVMBasicBlockRef h_next = LLVMAppendBasicBlockInContext(c, fn, "h.next");
    LLVMBasicBlockRef e_cond = LLVMAppendBasicBlockInContext(c, fn, "e.cond");
    LLVMBasicBlockRef e_body = LLVMAppendBasicBlockInContext(c, fn, "e.body");
    LLVMBasicBlockRef e_sift = LLVMAppendBasicBlockInContext(c, fn, "e.sift");
    LLVMBasicBlockRef e_chk_r = LLVMAppendBasicBlockInContext(c, fn, "e.chkr");
    LLVMBasicBlockRef e_cmp_r = LLVMAppendBasicBlockInContext(c, fn, "e.cmpr");
    LLVMBasicBlockRef e_pick_r = LLVMAppendBasicBlockInContext(c, fn, "e.pickr");
    LLVMBasicBlockRef e_cmp_p = LLVMAppendBasicBlockInContext(c, fn, "e.cmpp");
    LLVMBasicBlockRef e_swap = LLVMAppendBasicBlockInContext(c, fn, "e.swap");
    LLVMBasicBlockRef e_next = LLVMAppendBasicBlockInContext(c, fn, "e.next");
    LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(c, fn, "ret");

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef n = LLVMBuildAdd(b, LLVMBuildSub(b, hi, lo, "span"), one, "n");
    LLVMValueRef k_init = LLVMBuildSub(b, LLVMBuildAShr(b, n, one, "half"), one, "k0");
    LLVMValueRef end_init = LLVMBuildSub(b, n, one, "end0");
    LLVMBuildBr(b, h_cond);

    /* Phase 1: Floyd build max-heap */
    LLVMPositionBuilderAtEnd(b, h_cond);
    LLVMValueRef hk = LLVMBuildPhi(b, i64, "hk");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSGE, hk, zero, "hmore"), h_sift, e_cond);

    LLVMPositionBuilderAtEnd(b, h_sift);
    LLVMValueRef hr = LLVMBuildPhi(b, i64, "hr");
    LLVMValueRef hc = LLVMBuildAdd(b, LLVMBuildShl(b, hr, one, "hr2"), one, "hc");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSLT, hc, n, "hcin"), h_chk_r, h_next);

    LLVMPositionBuilderAtEnd(b, h_chk_r);
    LLVMValueRef hc1 = LLVMBuildAdd(b, hc, one, "hc1");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSLT, hc1, n, "hrin"), h_cmp_r, h_cmp_p);

    LLVMPositionBuilderAtEnd(b, h_cmp_r);
    LLVMValueRef hl_idx = LLVMBuildAdd(b, lo, hc, "hli");
    LLVMValueRef hr_idx = LLVMBuildAdd(b, lo, hc1, "hri");
    LLVMValueRef hl_v = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &hl_idx, 1, "hlp"), "hlv");
    LLVMValueRef hr_v = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &hr_idx, 1, "hrp"), "hrv");
    LLVMValueRef hr_gt = emit_sort_lt(g, kind, hl_v, hr_v, cmp);
    LLVMBasicBlockRef h_cmp_r_end = LLVMGetInsertBlock(b);
    LLVMBuildCondBr(b, hr_gt, h_pick_r, h_cmp_p);

    LLVMPositionBuilderAtEnd(b, h_pick_r);
    LLVMBuildBr(b, h_cmp_p);

    LLVMPositionBuilderAtEnd(b, h_cmp_p);
    LLVMValueRef hbest = LLVMBuildPhi(b, i64, "hbest");
    LLVMAddIncoming(hbest, (LLVMValueRef[]){ hc, hc, hc1 },
                    (LLVMBasicBlockRef[]){ h_chk_r, h_cmp_r_end, h_pick_r }, 3);
    LLVMValueRef hp_idx = LLVMBuildAdd(b, lo, hr, "hpi");
    LLVMValueRef hb_idx = LLVMBuildAdd(b, lo, hbest, "hbi");
    LLVMValueRef hp_v = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &hp_idx, 1, "hpp"), "hpv");
    LLVMValueRef hb_v = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &hb_idx, 1, "hbp"), "hbv");
    LLVMValueRef hp_lt = emit_sort_lt(g, kind, hp_v, hb_v, cmp);
    LLVMBuildCondBr(b, hp_lt, h_swap, h_next);

    LLVMPositionBuilderAtEnd(b, h_swap);
    emit_sort_swap(b, et, data, hp_idx, hb_idx);
    LLVMBuildBr(b, h_sift);
    LLVMAddIncoming(hr, (LLVMValueRef[]){ hk, hbest },
                    (LLVMBasicBlockRef[]){ h_cond, h_swap }, 2);

    LLVMPositionBuilderAtEnd(b, h_next);
    LLVMValueRef hk_dec = LLVMBuildSub(b, hk, one, "hkd");
    LLVMBuildBr(b, h_cond);
    LLVMAddIncoming(hk, (LLVMValueRef[]){ k_init, hk_dec },
                    (LLVMBasicBlockRef[]){ entry, h_next }, 2);

    /* Phase 2: Extract max */
    LLVMPositionBuilderAtEnd(b, e_cond);
    LLVMValueRef endv = LLVMBuildPhi(b, i64, "end");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSGT, endv, zero, "emore"), e_body, ret_bb);

    LLVMPositionBuilderAtEnd(b, e_body);
    LLVMValueRef end_idx = LLVMBuildAdd(b, lo, endv, "ei");
    emit_sort_swap(b, et, data, lo, end_idx);
    LLVMBuildBr(b, e_sift);

    LLVMPositionBuilderAtEnd(b, e_sift);
    LLVMValueRef er = LLVMBuildPhi(b, i64, "er");
    LLVMValueRef ec = LLVMBuildAdd(b, LLVMBuildShl(b, er, one, "er2"), one, "ec");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSLT, ec, endv, "ecin"), e_chk_r, e_next);

    LLVMPositionBuilderAtEnd(b, e_chk_r);
    LLVMValueRef ec1 = LLVMBuildAdd(b, ec, one, "ec1");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSLT, ec1, endv, "erin"), e_cmp_r, e_cmp_p);

    LLVMPositionBuilderAtEnd(b, e_cmp_r);
    LLVMValueRef el_idx = LLVMBuildAdd(b, lo, ec, "eli");
    LLVMValueRef er_idx = LLVMBuildAdd(b, lo, ec1, "eri");
    LLVMValueRef el_v = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &el_idx, 1, "elp"), "elv");
    LLVMValueRef er_v = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &er_idx, 1, "erp"), "erv");
    LLVMValueRef er_gt = emit_sort_lt(g, kind, el_v, er_v, cmp);
    LLVMBasicBlockRef e_cmp_r_end = LLVMGetInsertBlock(b);
    LLVMBuildCondBr(b, er_gt, e_pick_r, e_cmp_p);

    LLVMPositionBuilderAtEnd(b, e_pick_r);
    LLVMBuildBr(b, e_cmp_p);

    LLVMPositionBuilderAtEnd(b, e_cmp_p);
    LLVMValueRef ebest = LLVMBuildPhi(b, i64, "ebest");
    LLVMAddIncoming(ebest, (LLVMValueRef[]){ ec, ec, ec1 },
                    (LLVMBasicBlockRef[]){ e_chk_r, e_cmp_r_end, e_pick_r }, 3);
    LLVMValueRef ep_idx = LLVMBuildAdd(b, lo, er, "epi");
    LLVMValueRef eb_idx = LLVMBuildAdd(b, lo, ebest, "ebi");
    LLVMValueRef ep_v = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &ep_idx, 1, "epp"), "epv");
    LLVMValueRef eb_v = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &eb_idx, 1, "ebp"), "ebv");
    LLVMValueRef ep_lt = emit_sort_lt(g, kind, ep_v, eb_v, cmp);
    LLVMBuildCondBr(b, ep_lt, e_swap, e_next);

    LLVMPositionBuilderAtEnd(b, e_swap);
    emit_sort_swap(b, et, data, ep_idx, eb_idx);
    LLVMBuildBr(b, e_sift);
    LLVMAddIncoming(er, (LLVMValueRef[]){ zero, ebest },
                    (LLVMBasicBlockRef[]){ e_body, e_swap }, 2);

    LLVMPositionBuilderAtEnd(b, e_next);
    LLVMValueRef end_dec = LLVMBuildSub(b, endv, one, "endd");
    LLVMBuildBr(b, e_cond);
    LLVMAddIncoming(endv, (LLVMValueRef[]){ end_init, end_dec },
                    (LLVMBasicBlockRef[]){ h_cond, e_next }, 2);
    (void)two;

    LLVMPositionBuilderAtEnd(b, ret_bb);
    LLVMBuildRetVoid(b);

    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}

/* 主内省排序循环：三数取中 + Hoare 划分 + 小段尾递归 + <=16 插入排序 */
static LLVMValueRef get_sort_intro_fn(zan_irgen_t *g, int kind) {
    char fname[64];
    snprintf(fname, sizeof(fname), "__zan_sort_%s_intro", sort_kind_tag(kind));
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fname);
    if (fn) return fn;

    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef voidt = LLVMVoidTypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef et = sort_elem_type(g, kind);
    LLVMTypeRef eptr = LLVMPointerType(et, 0);
    bool is_cmp = sort_is_cmp(kind);

    LLVMTypeRef params[5] = { eptr, i64, i64, i64, i8ptr };
    LLVMTypeRef fnty = LLVMFunctionType(voidt, params, is_cmp ? 5 : 4, 0);
    fn = LLVMAddFunction(g->mod, fname, fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    sort_add_fn_attr(g, fn, "noinline");
    if (!is_cmp) sort_add_fn_attr(g, fn, "nounwind");

    LLVMValueRef heap_fn = get_sort_heap_fn(g, kind);
    LLVMTypeRef heap_ty = LLVMGlobalGetValueType(heap_fn);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;

    LLVMValueRef data = LLVMGetParam(fn, 0);
    LLVMValueRef p_lo = LLVMGetParam(fn, 1);
    LLVMValueRef p_hi = LLVMGetParam(fn, 2);
    LLVMValueRef p_depth = LLVMGetParam(fn, 3);
    LLVMValueRef cmp = is_cmp ? LLVMGetParam(fn, 4) : NULL;
    LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
    LLVMValueRef one = LLVMConstInt(i64, 1, 0);
    LLVMValueRef cutoff = LLVMConstInt(i64, 16, 0);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef outer = LLVMAppendBasicBlockInContext(c, fn, "outer");
    LLVMBasicBlockRef depth_chk = LLVMAppendBasicBlockInContext(c, fn, "dchk");
    LLVMBasicBlockRef do_heap = LLVMAppendBasicBlockInContext(c, fn, "heap");
    LLVMBasicBlockRef med1 = LLVMAppendBasicBlockInContext(c, fn, "med1");
    LLVMBasicBlockRef med1_sw = LLVMAppendBasicBlockInContext(c, fn, "med1.sw");
    LLVMBasicBlockRef med2 = LLVMAppendBasicBlockInContext(c, fn, "med2");
    LLVMBasicBlockRef med2_sw = LLVMAppendBasicBlockInContext(c, fn, "med2.sw");
    LLVMBasicBlockRef med3 = LLVMAppendBasicBlockInContext(c, fn, "med3");
    LLVMBasicBlockRef med3_sw = LLVMAppendBasicBlockInContext(c, fn, "med3.sw");
    LLVMBasicBlockRef part_init = LLVMAppendBasicBlockInContext(c, fn, "p.init");
    LLVMBasicBlockRef part_cond = LLVMAppendBasicBlockInContext(c, fn, "p.cond");
    LLVMBasicBlockRef scan_l = LLVMAppendBasicBlockInContext(c, fn, "p.scanl");
    LLVMBasicBlockRef scan_l_cmp = is_cmp ? LLVMAppendBasicBlockInContext(c, fn, "p.slc") : NULL;
    LLVMBasicBlockRef scan_l_inc = LLVMAppendBasicBlockInContext(c, fn, "p.sli");
    LLVMBasicBlockRef scan_r = LLVMAppendBasicBlockInContext(c, fn, "p.scanr");
    LLVMBasicBlockRef scan_r_cmp = is_cmp ? LLVMAppendBasicBlockInContext(c, fn, "p.src") : NULL;
    LLVMBasicBlockRef scan_r_dec = LLVMAppendBasicBlockInContext(c, fn, "p.srd");
    LLVMBasicBlockRef part_sw_chk = LLVMAppendBasicBlockInContext(c, fn, "p.swchk");
    LLVMBasicBlockRef part_sw = LLVMAppendBasicBlockInContext(c, fn, "p.sw");
    LLVMBasicBlockRef part_done = LLVMAppendBasicBlockInContext(c, fn, "p.done");
    LLVMBasicBlockRef rec_l = LLVMAppendBasicBlockInContext(c, fn, "rec.l");
    LLVMBasicBlockRef rec_l_call = LLVMAppendBasicBlockInContext(c, fn, "rec.lc");
    LLVMBasicBlockRef rec_l_cont = LLVMAppendBasicBlockInContext(c, fn, "rec.lx");
    LLVMBasicBlockRef rec_r = LLVMAppendBasicBlockInContext(c, fn, "rec.r");
    LLVMBasicBlockRef rec_r_call = LLVMAppendBasicBlockInContext(c, fn, "rec.rc");
    LLVMBasicBlockRef rec_r_cont = LLVMAppendBasicBlockInContext(c, fn, "rec.rx");
    LLVMBasicBlockRef ins_entry = LLVMAppendBasicBlockInContext(c, fn, "ins.entry");
    LLVMBasicBlockRef ins_cond = LLVMAppendBasicBlockInContext(c, fn, "ins.cond");
    LLVMBasicBlockRef ins_body = LLVMAppendBasicBlockInContext(c, fn, "ins.body");
    LLVMBasicBlockRef ins_inner = LLVMAppendBasicBlockInContext(c, fn, "ins.in");
    LLVMBasicBlockRef ins_cmp = LLVMAppendBasicBlockInContext(c, fn, "ins.cmp");
    LLVMBasicBlockRef ins_step = LLVMAppendBasicBlockInContext(c, fn, "ins.step");
    LLVMBasicBlockRef ins_put = LLVMAppendBasicBlockInContext(c, fn, "ins.put");
    LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(c, fn, "ret");

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMBuildBr(b, outer);

    LLVMPositionBuilderAtEnd(b, outer);
    LLVMValueRef lo = LLVMBuildPhi(b, i64, "lo");
    LLVMValueRef hi = LLVMBuildPhi(b, i64, "hi");
    LLVMValueRef depth = LLVMBuildPhi(b, i64, "depth");
    LLVMValueRef span = LLVMBuildSub(b, hi, lo, "span");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSGE, span, cutoff, "big"),
                    depth_chk, ins_entry);

    LLVMPositionBuilderAtEnd(b, depth_chk);
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntEQ, depth, zero, "dz"),
                    do_heap, med1);

    LLVMPositionBuilderAtEnd(b, do_heap);
    if (is_cmp)
        zan_call2(b, heap_ty, heap_fn, (LLVMValueRef[]){ data, lo, hi, cmp }, 4, "");
    else
        zan_call2(b, heap_ty, heap_fn, (LLVMValueRef[]){ data, lo, hi }, 3, "");
    LLVMBuildRetVoid(b);

    /* Median-of-3 */
    LLVMPositionBuilderAtEnd(b, med1);
    LLVMValueRef ndepth = LLVMBuildSub(b, depth, one, "nd");
    LLVMValueRef mid = LLVMBuildAdd(b, lo, LLVMBuildAShr(b, span, one, "hspan"), "mid");
    LLVMValueRef v_lo1 = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &lo, 1, "lp1"), "vl1");
    LLVMValueRef v_md1 = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &mid, 1, "mp1"), "vm1");
    LLVMBuildCondBr(b, emit_sort_lt(g, kind, v_md1, v_lo1, cmp), med1_sw, med2);

    LLVMPositionBuilderAtEnd(b, med1_sw);
    emit_sort_swap(b, et, data, lo, mid);
    LLVMBuildBr(b, med2);

    LLVMPositionBuilderAtEnd(b, med2);
    LLVMValueRef v_lo2 = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &lo, 1, "lp2"), "vl2");
    LLVMValueRef v_hi2 = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &hi, 1, "hp2"), "vh2");
    LLVMBuildCondBr(b, emit_sort_lt(g, kind, v_hi2, v_lo2, cmp), med2_sw, med3);

    LLVMPositionBuilderAtEnd(b, med2_sw);
    emit_sort_swap(b, et, data, lo, hi);
    LLVMBuildBr(b, med3);

    LLVMPositionBuilderAtEnd(b, med3);
    LLVMValueRef v_md3 = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &mid, 1, "mp3"), "vm3");
    LLVMValueRef v_hi3 = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &hi, 1, "hp3"), "vh3");
    LLVMBuildCondBr(b, emit_sort_lt(g, kind, v_hi3, v_md3, cmp), med3_sw, part_init);

    LLVMPositionBuilderAtEnd(b, med3_sw);
    emit_sort_swap(b, et, data, mid, hi);
    LLVMBuildBr(b, part_init);

    /* Hoare partition */
    LLVMPositionBuilderAtEnd(b, part_init);
    LLVMValueRef pivot = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &mid, 1, "pivp"), "piv");
    LLVMBuildBr(b, part_cond);

    LLVMPositionBuilderAtEnd(b, part_cond);
    LLVMValueRef pi = LLVMBuildPhi(b, i64, "pi");
    LLVMValueRef pj = LLVMBuildPhi(b, i64, "pj");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSLE, pi, pj, "pgo"), scan_l, part_done);

    LLVMPositionBuilderAtEnd(b, scan_l);
    LLVMValueRef li = LLVMBuildPhi(b, i64, "li");
    if (is_cmp) {
        LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSLE, li, hi, "lib"), scan_l_cmp, scan_r);
        LLVMPositionBuilderAtEnd(b, scan_l_cmp);
    }
    LLVMValueRef lv = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &li, 1, "lip"), "lv");
    LLVMValueRef lv_lt = emit_sort_lt(g, kind, lv, pivot, cmp);
    LLVMBasicBlockRef sl_end = LLVMGetInsertBlock(b);
    LLVMBuildCondBr(b, lv_lt, scan_l_inc, scan_r);

    LLVMPositionBuilderAtEnd(b, scan_l_inc);
    LLVMValueRef li_inc = LLVMBuildAdd(b, li, one, "lii");
    LLVMBuildBr(b, scan_l);
    LLVMAddIncoming(li, (LLVMValueRef[]){ pi, li_inc },
                    (LLVMBasicBlockRef[]){ part_cond, scan_l_inc }, 2);

    LLVMPositionBuilderAtEnd(b, scan_r);
    LLVMValueRef rj = LLVMBuildPhi(b, i64, "rj");
    if (is_cmp) {
        LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSGE, rj, lo, "rjb"), scan_r_cmp, part_sw_chk);
        LLVMPositionBuilderAtEnd(b, scan_r_cmp);
    }
    LLVMValueRef rv = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &rj, 1, "rjp"), "rv");
    LLVMBuildCondBr(b, emit_sort_lt(g, kind, pivot, rv, cmp), scan_r_dec, part_sw_chk);

    LLVMPositionBuilderAtEnd(b, scan_r_dec);
    LLVMValueRef rj_dec = LLVMBuildSub(b, rj, one, "rjd");
    LLVMBuildBr(b, scan_r);
    if (is_cmp) {
        LLVMAddIncoming(rj, (LLVMValueRef[]){ pj, pj, rj_dec },
                        (LLVMBasicBlockRef[]){ scan_l, sl_end, scan_r_dec }, 3);
    } else {
        LLVMAddIncoming(rj, (LLVMValueRef[]){ pj, rj_dec },
                        (LLVMBasicBlockRef[]){ sl_end, scan_r_dec }, 2);
    }
    LLVMPositionBuilderAtEnd(b, part_sw_chk);
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSLE, li, rj, "dosw"), part_sw, part_done);

    LLVMPositionBuilderAtEnd(b, part_sw);
    emit_sort_swap(b, et, data, li, rj);
    LLVMValueRef pi_n = LLVMBuildAdd(b, li, one, "pin");
    LLVMValueRef pj_n = LLVMBuildSub(b, rj, one, "pjn");
    LLVMBuildBr(b, part_cond);
    LLVMAddIncoming(pi, (LLVMValueRef[]){ lo, pi_n },
                    (LLVMBasicBlockRef[]){ part_init, part_sw }, 2);
    LLVMAddIncoming(pj, (LLVMValueRef[]){ hi, pj_n },
                    (LLVMBasicBlockRef[]){ part_init, part_sw }, 2);

    LLVMPositionBuilderAtEnd(b, part_done);
    LLVMValueRef fin_i = LLVMBuildPhi(b, i64, "fini");
    LLVMValueRef fin_j = LLVMBuildPhi(b, i64, "finj");
    LLVMAddIncoming(fin_i, (LLVMValueRef[]){ pi, li },
                    (LLVMBasicBlockRef[]){ part_cond, part_sw_chk }, 2);
    LLVMAddIncoming(fin_j, (LLVMValueRef[]){ pj, rj },
                    (LLVMBasicBlockRef[]){ part_cond, part_sw_chk }, 2);
    LLVMValueRef lsize = LLVMBuildSub(b, fin_j, lo, "lsz");
    LLVMValueRef rsize = LLVMBuildSub(b, hi, fin_i, "rsz");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSLT, lsize, rsize, "lsmall"), rec_l, rec_r);

    LLVMPositionBuilderAtEnd(b, rec_l);
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSLT, lo, fin_j, "rlgo"), rec_l_call, rec_l_cont);
    LLVMPositionBuilderAtEnd(b, rec_l_call);
    if (is_cmp)
        zan_call2(b, fnty, fn, (LLVMValueRef[]){ data, lo, fin_j, ndepth, cmp }, 5, "");
    else
        zan_call2(b, fnty, fn, (LLVMValueRef[]){ data, lo, fin_j, ndepth }, 4, "");
    LLVMBuildBr(b, rec_l_cont);
    LLVMPositionBuilderAtEnd(b, rec_l_cont);
    LLVMBuildBr(b, outer);

    LLVMPositionBuilderAtEnd(b, rec_r);
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSLT, fin_i, hi, "rrgo"), rec_r_call, rec_r_cont);
    LLVMPositionBuilderAtEnd(b, rec_r_call);
    if (is_cmp)
        zan_call2(b, fnty, fn, (LLVMValueRef[]){ data, fin_i, hi, ndepth, cmp }, 5, "");
    else
        zan_call2(b, fnty, fn, (LLVMValueRef[]){ data, fin_i, hi, ndepth }, 4, "");
    LLVMBuildBr(b, rec_r_cont);
    LLVMPositionBuilderAtEnd(b, rec_r_cont);
    LLVMBuildBr(b, outer);

    LLVMAddIncoming(lo, (LLVMValueRef[]){ p_lo, fin_i, lo },
                    (LLVMBasicBlockRef[]){ entry, rec_l_cont, rec_r_cont }, 3);
    LLVMAddIncoming(hi, (LLVMValueRef[]){ p_hi, hi, fin_j },
                    (LLVMBasicBlockRef[]){ entry, rec_l_cont, rec_r_cont }, 3);
    LLVMAddIncoming(depth, (LLVMValueRef[]){ p_depth, ndepth, ndepth },
                    (LLVMBasicBlockRef[]){ entry, rec_l_cont, rec_r_cont }, 3);

    /* Insertion sort on [lo..hi] (hi - lo < 16) */
    LLVMPositionBuilderAtEnd(b, ins_entry);
    LLVMValueRef k0 = LLVMBuildAdd(b, lo, one, "ik0");
    LLVMBuildBr(b, ins_cond);

    LLVMPositionBuilderAtEnd(b, ins_cond);
    LLVMValueRef ik = LLVMBuildPhi(b, i64, "ik");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSLE, ik, hi, "imore"), ins_body, ret_bb);

    bool use_adj_swap = (kind == ZAN_SORT_CMP_PTR);
    if (use_adj_swap) {
        /* RC 指针 + 可抛自定义比较器：相邻交换，任意时刻保持完整置换（零泄漏/零双释） */
        LLVMPositionBuilderAtEnd(b, ins_body);
        LLVMBuildBr(b, ins_inner);

        LLVMPositionBuilderAtEnd(b, ins_inner);
        LLVMValueRef ij = LLVMBuildPhi(b, i64, "ij");
        LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSGT, ij, lo, "ijgt"), ins_cmp, ins_put);

        LLVMPositionBuilderAtEnd(b, ins_cmp);
        LLVMValueRef ij_m1 = LLVMBuildSub(b, ij, one, "ijm1");
        LLVMValueRef vj = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &ij, 1, "ijp"), "vj");
        LLVMValueRef vjm1 = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &ij_m1, 1, "ijmp"), "vjm1");
        LLVMBuildCondBr(b, emit_sort_lt(g, kind, vj, vjm1, cmp), ins_step, ins_put);

        LLVMPositionBuilderAtEnd(b, ins_step);
        emit_sort_swap(b, et, data, ij, ij_m1);
        LLVMBuildBr(b, ins_inner);
        LLVMAddIncoming(ij, (LLVMValueRef[]){ ik, ij_m1 },
                        (LLVMBasicBlockRef[]){ ins_body, ins_step }, 2);

        LLVMPositionBuilderAtEnd(b, ins_put);
        LLVMValueRef ik_n = LLVMBuildAdd(b, ik, one, "ikn");
        LLVMBuildBr(b, ins_cond);
        LLVMAddIncoming(ik, (LLVMValueRef[]){ k0, ik_n },
                        (LLVMBasicBlockRef[]){ ins_entry, ins_put }, 2);
    } else {
        /* 移位插入排序（内建比较器或非 RC 标量） */
        LLVMPositionBuilderAtEnd(b, ins_body);
        LLVMValueRef key = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &ik, 1, "ikp"), "key");
        LLVMValueRef ij0 = LLVMBuildSub(b, ik, one, "ij0");
        LLVMBuildBr(b, ins_inner);

        LLVMPositionBuilderAtEnd(b, ins_inner);
        LLVMValueRef ij = LLVMBuildPhi(b, i64, "ij");
        LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSGE, ij, lo, "ijge"), ins_cmp, ins_put);

        LLVMPositionBuilderAtEnd(b, ins_cmp);
        LLVMValueRef vj = LLVMBuildLoad2(b, et, LLVMBuildInBoundsGEP2(b, et, data, &ij, 1, "ijp"), "vj");
        LLVMValueRef k_lt_vj = emit_sort_lt(g, kind, key, vj, cmp);
        LLVMBasicBlockRef ins_cmp_end = LLVMGetInsertBlock(b);
        LLVMBuildCondBr(b, k_lt_vj, ins_step, ins_put);

        LLVMPositionBuilderAtEnd(b, ins_step);
        LLVMValueRef ijp1 = LLVMBuildAdd(b, ij, one, "ijp1");
        LLVMBuildStore(b, vj, LLVMBuildInBoundsGEP2(b, et, data, &ijp1, 1, "ijp1p"));
        LLVMValueRef ijd = LLVMBuildSub(b, ij, one, "ijd");
        LLVMBuildBr(b, ins_inner);
        LLVMAddIncoming(ij, (LLVMValueRef[]){ ij0, ijd },
                        (LLVMBasicBlockRef[]){ ins_body, ins_step }, 2);

        LLVMPositionBuilderAtEnd(b, ins_put);
        LLVMValueRef put_j = LLVMBuildPhi(b, i64, "putj");
        LLVMAddIncoming(put_j, (LLVMValueRef[]){ ij, ij },
                        (LLVMBasicBlockRef[]){ ins_inner, ins_cmp_end }, 2);
        LLVMValueRef put_i = LLVMBuildAdd(b, put_j, one, "puti");
        LLVMBuildStore(b, key, LLVMBuildInBoundsGEP2(b, et, data, &put_i, 1, "putp"));
        LLVMValueRef ik_n = LLVMBuildAdd(b, ik, one, "ikn");
        LLVMBuildBr(b, ins_cond);
        LLVMAddIncoming(ik, (LLVMValueRef[]){ k0, ik_n },
                        (LLVMBasicBlockRef[]){ ins_entry, ins_put }, 2);
    }

    LLVMPositionBuilderAtEnd(b, ret_bb);
    LLVMBuildRetVoid(b);

    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}

static LLVMValueRef get_sort_entry_fn(zan_irgen_t *g, int kind) {
    char fname[64];
    snprintf(fname, sizeof(fname), "__zan_sort_%s", sort_kind_tag(kind));
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fname);
    if (fn) return fn;

    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef voidt = LLVMVoidTypeInContext(c);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef et = sort_elem_type(g, kind);
    LLVMTypeRef eptr = LLVMPointerType(et, 0);
    bool is_cmp = sort_is_cmp(kind);

    LLVMTypeRef params[3] = { eptr, i64, i8ptr };
    LLVMTypeRef fnty = LLVMFunctionType(voidt, params, is_cmp ? 3 : 2, 0);
    fn = LLVMAddFunction(g->mod, fname, fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    if (!is_cmp) sort_add_fn_attr(g, fn, "nounwind");

    LLVMValueRef intro_fn = get_sort_intro_fn(g, kind);
    LLVMTypeRef intro_ty = LLVMGlobalGetValueType(intro_fn);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef go = LLVMAppendBasicBlockInContext(c, fn, "go");
    LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(c, fn, "ret");

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef data = LLVMGetParam(fn, 0);
    LLVMValueRef count = LLVMGetParam(fn, 1);
    LLVMValueRef cmp = is_cmp ? LLVMGetParam(fn, 2) : NULL;
    LLVMValueRef bad = LLVMBuildOr(
        b,
        LLVMBuildICmp(b, LLVMIntEQ, data, LLVMConstNull(eptr), "dnull"),
        LLVMBuildICmp(b, LLVMIntSLE, count, LLVMConstInt(i64, 1, 0), "le1"),
        "skip0");
    if (is_cmp) {
        bad = LLVMBuildOr(b, bad,
            LLVMBuildICmp(b, LLVMIntEQ, cmp, LLVMConstNull(i8ptr), "cnull"), "skip");
    }
    LLVMBuildCondBr(b, bad, ret_bb, go);

    LLVMPositionBuilderAtEnd(b, go);
    LLVMTypeRef ctlz_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i64, i1 }, 2, 0);
    LLVMValueRef ctlz_fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i64");
    if (!ctlz_fn) ctlz_fn = LLVMAddFunction(g->mod, "llvm.ctlz.i64", ctlz_ty);
    LLVMValueRef lz = zan_call2(b, ctlz_ty, ctlz_fn,
        (LLVMValueRef[]){ count, LLVMConstInt(i1, 1, 0) }, 2, "lz");
    LLVMValueRef lg = LLVMBuildSub(b, LLVMConstInt(i64, 63, 0), lz, "lg");
    LLVMValueRef depth = LLVMBuildShl(b, lg, LLVMConstInt(i64, 1, 0), "depth");
    LLVMValueRef hi = LLVMBuildSub(b, count, LLVMConstInt(i64, 1, 0), "hi");
    if (is_cmp)
        zan_call2(b, intro_ty, intro_fn,
                  (LLVMValueRef[]){ data, LLVMConstInt(i64, 0, 0), hi, depth, cmp }, 5, "");
    else
        zan_call2(b, intro_ty, intro_fn,
                  (LLVMValueRef[]){ data, LLVMConstInt(i64, 0, 0), hi, depth }, 4, "");
    LLVMBuildBr(b, ret_bb);

    LLVMPositionBuilderAtEnd(b, ret_bb);
    LLVMBuildRetVoid(b);

    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}

void emit_raw_buffer_sort(zan_irgen_t *g, int kind, LLVMValueRef data_raw,
                          LLVMValueRef count, LLVMValueRef cmp_or_null) {
    LLVMTypeRef et = sort_elem_type(g, kind);
    LLVMTypeRef eptr = LLVMPointerType(et, 0);
    LLVMValueRef data = (LLVMTypeOf(data_raw) == eptr)
        ? data_raw
        : LLVMBuildBitCast(g->builder, data_raw, eptr, "sort.buf");
    LLVMValueRef sfn = get_sort_entry_fn(g, kind);
    LLVMTypeRef sty = LLVMGlobalGetValueType(sfn);
    if (sort_is_cmp(kind)) {
        LLVMValueRef args[3] = { data, count, cmp_or_null };
        zan_call_or_invoke(g, g->builder, sty, sfn, args, 3, "");
    } else {
        LLVMValueRef args[2] = { data, count };
        zan_call2(g->builder, sty, sfn, args, 2, "");
    }
}

