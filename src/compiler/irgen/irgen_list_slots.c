/* FRAGMENT（拼接片段，非独立 TU）：由 irgen.c 拼入，紧随 irgen_type_map.c。
 * 域：List<T> 元素槽——窄槽宽度、表头宽度标签守卫、IR 内的改宽函数、
 * 元素地址/读写。 */
/* ===================== List 元素槽 =====================
 * 历史上 List<T> 的每个元素占 8 字节「槽字」（结构体占若干槽字），
 * List<int> 白占一半内存。窄槽：int/uint/char/float 占 4 字节，short/ushort
 * 2 字节，bool/byte/sbyte 1 字节。窄槽里存的就是原 8 字节槽字的低位：
 * 读回时按类型符号扩展/零扩展还原成同一个槽字，后续各类型的转换代码不变。
 *
 * 擦除版泛型代码（以及特化不到的路径）不知道 T，只会按 8 字节读写；
 * 它新建的 List<T> 也只能是 8 字节槽。所以表头带宽度标签 esz
 * （0 = 8 字节槽字；否则低 4 位宽度、0x10 = 有符号），每个元素访问点都知道
 * 自己要的宽度——特化代码是静态宽度，擦除代码是 0——不一致时调
 * __zan_list_repack 原地改宽一次（发生在擦除/特化交界，罕见）。
 * repack 生成在本模块 IR 里，不依赖运行时库（预编译交叉运行时无需新符号）。 */
#ifndef ZAN_LIST_NARROW
#define ZAN_LIST_NARROW 1
#endif
#define ZAN_ESZ_SIGNED 0x10

static void zan_tbaa_init(zan_irgen_t *g) {
    if (g->tbaa_tag_cnt) return;
    LLVMContextRef c = g->ctx;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMMetadataRef zero = LLVMValueAsMetadata(LLVMConstInt(i64, 0, 0));
    LLVMMetadataRef root_s = LLVMMDStringInContext2(c, "Zan TBAA", 8);
    LLVMMetadataRef root = LLVMMDNodeInContext2(c, &root_s, 1);

    LLVMMetadataRef cnt_f[3] = {
        LLVMMDStringInContext2(c, "Zan Container Count", 19), root, zero
    };
    LLVMMetadataRef cnt_ty = LLVMMDNodeInContext2(c, cnt_f, 3);
    LLVMMetadataRef cnt_tag[3] = { cnt_ty, cnt_ty, zero };

    LLVMMetadataRef meta_f[3] = {
        LLVMMDStringInContext2(c, "Zan Container Meta", 18), root, zero
    };
    LLVMMetadataRef meta_ty = LLVMMDNodeInContext2(c, meta_f, 3);
    LLVMMetadataRef meta_tag[3] = { meta_ty, meta_ty, zero };

    LLVMMetadataRef elem_f[3] = {
        LLVMMDStringInContext2(c, "Zan Container Elem", 18), root, zero
    };
    LLVMMetadataRef elem_ty = LLVMMDNodeInContext2(c, elem_f, 3);
    LLVMMetadataRef elem_tag[3] = { elem_ty, elem_ty, zero };

    g->tbaa_md_kind = LLVMGetMDKindIDInContext(c, "tbaa", 4);
    g->tbaa_tag_cnt = LLVMMetadataAsValue(c, LLVMMDNodeInContext2(c, cnt_tag, 3));
    g->tbaa_tag_meta = LLVMMetadataAsValue(c, LLVMMDNodeInContext2(c, meta_tag, 3));
    g->tbaa_tag_elem = LLVMMetadataAsValue(c, LLVMMDNodeInContext2(c, elem_tag, 3));
}
void zan_tbaa_tag_cnt(zan_irgen_t *g, LLVMValueRef inst) {
    if (!inst) return;
    zan_tbaa_init(g);
    LLVMSetMetadata(inst, g->tbaa_md_kind, g->tbaa_tag_cnt);
    if (LLVMIsALoadInst(inst) &&
        LLVMTypeOf(inst) == LLVMInt64TypeInContext(g->ctx)) {
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMValueRef r_elems[2] = {
            LLVMConstInt(i64, 0, 0),
            LLVMConstInt(i64, 2147483648ULL, 0),
        };
        unsigned rk = LLVMGetMDKindIDInContext(g->ctx, "range", 5);
        LLVMSetMetadata(inst, rk, LLVMMDNodeInContext(g->ctx, r_elems, 2));
    }
}
void zan_tbaa_tag_meta(zan_irgen_t *g, LLVMValueRef inst) {
    if (!inst) return;
    zan_tbaa_init(g);
    LLVMSetMetadata(inst, g->tbaa_md_kind, g->tbaa_tag_meta);
}
void zan_tbaa_tag_elem(zan_irgen_t *g, LLVMValueRef inst) {
    if (!inst) return;
    zan_tbaa_init(g);
    LLVMSetMetadata(inst, g->tbaa_md_kind, g->tbaa_tag_elem);
}
void zan_set_branch_weights(zan_irgen_t *g, LLVMValueRef br,
                            uint32_t true_w, uint32_t false_w) {
    if (!br) return;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef md_elems[3] = {
        LLVMMDStringInContext(g->ctx, "branch_weights", 14),
        LLVMConstInt(i32t, true_w, 0),
        LLVMConstInt(i32t, false_w, 0),
    };
    unsigned prof_kind = LLVMGetMDKindIDInContext(g->ctx, "prof", 4);
    LLVMSetMetadata(br, prof_kind, LLVMMDNodeInContext(g->ctx, md_elems, 3));
}

int list_elem_tag(zan_irgen_t *g, zan_type_t *et) {
    (void)g;
#if ZAN_LIST_NARROW
    if (!et) return 0;
    switch (et->kind) {
    case TYPE_BOOL: case TYPE_BYTE:   return 1;
    case TYPE_SBYTE:                  return 1 | ZAN_ESZ_SIGNED;
    case TYPE_SHORT:                  return 2 | ZAN_ESZ_SIGNED;
    case TYPE_USHORT:                 return 2;
    case TYPE_INT:                    return 4 | ZAN_ESZ_SIGNED;
    case TYPE_UINT:                   return 4;
    /* char 不收窄：其 LLVM 类型是 i64，`ref list[i]` 的消费者按元素类型
     * 宽度读写，4 字节窄槽会被写穿。 */
    case TYPE_FLOAT:                  return 4;   /* 槽字 = 位模式零扩展 */
    default: return 0;
    }
#else
    (void)et;
    return 0;
#endif
}
unsigned list_elem_bytes(zan_irgen_t *g, zan_type_t *et) {
    int tag = list_elem_tag(g, et);
    return tag ? (unsigned)(tag & 0xF) : 8u * elem_slot_words(g, et);
}
LLVMTypeRef list_slot_type(zan_irgen_t *g, zan_type_t *et) {
    int tag = list_elem_tag(g, et);
    if (!tag) return LLVMInt64TypeInContext(g->ctx);
    return LLVMIntTypeInContext(g->ctx, 8u * (unsigned)(tag & 0xF));
}

static LLVMValueRef list_repack_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_list_repack");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c), i16 = LLVMInt16TypeInContext(c);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c), i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef fty = LLVMFunctionType(LLVMVoidTypeInContext(c),
        (LLVMTypeRef[]){ ptr, i64 }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_list_repack", fty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    {
        static const char *const attrs[] = { "noinline", "cold", "nounwind" };
        for (size_t i = 0; i < 3; i++) {
            unsigned k = LLVMGetEnumAttributeKindForName(attrs[i], strlen(attrs[i]));
            if (k) LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                                           LLVMCreateEnumAttribute(c, k, 0));
        }
        unsigned nc = LLVMGetEnumAttributeKindForName("nocapture", 9);
        if (nc) LLVMAddAttributeAtIndex(fn, 1, LLVMCreateEnumAttribute(c, nc, 0));
    }
    LLVMBuilderRef b = LLVMCreateBuilderInContext(c);
    LLVMValueRef lp = LLVMGetParam(fn, 0), want = LLVMGetParam(fn, 1);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef work = LLVMAppendBasicBlockInContext(c, fn, "work");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(c, fn, "head");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(c, fn, "body");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(c, fn, "done");
    LLVMBasicBlockRef settag = LLVMAppendBasicBlockInContext(c, fn, "settag");
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMTypeRef lt = g->list_struct_type;
    LLVMValueRef cntp = LLVMBuildStructGEP2(b, lt, lp, 0, "cntp");
    LLVMValueRef capp = LLVMBuildStructGEP2(b, lt, lp, 1, "capp");
    LLVMValueRef datap = LLVMBuildStructGEP2(b, lt, lp, 2, "datap");
    LLVMValueRef eszp = LLVMBuildStructGEP2(b, lt, lp, 3, "eszp");
    LLVMValueRef cur = LLVMBuildLoad2(b, i64, eszp, "cur");
    zan_tbaa_tag_meta(g, cur);
    LLVMValueRef n = LLVMBuildLoad2(b, i64, cntp, "n");
    zan_tbaa_tag_cnt(g, n);
    LLVMValueRef cap = LLVMBuildLoad2(b, i64, capp, "cap");
    zan_tbaa_tag_meta(g, cap);
    LLVMValueRef data = LLVMBuildLoad2(b, ptr, datap, "data");
    zan_tbaa_tag_meta(g, data);
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, data, "nodata"), settag, work);
    /* 宽度：tag==0 → 8，否则 tag & 0xF */
    LLVMPositionBuilderAtEnd(b, work);
    LLVMValueRef fifteen = LLVMConstInt(i64, 15, 0), eight = LLVMConstInt(i64, 8, 0);
    LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
    LLVMValueRef ww = LLVMBuildSelect(b, LLVMBuildICmp(b, LLVMIntEQ, want, zero, "w0"),
        eight, LLVMBuildAnd(b, want, fifteen, "wm"), "ww");
    LLVMValueRef ncap = LLVMBuildSelect(b, LLVMBuildICmp(b, LLVMIntULT, cap, n, "capsmall"),
        n, cap, "ncap");
    ncap = LLVMBuildSelect(b, LLVMBuildICmp(b, LLVMIntEQ, ncap, zero, "c0"),
        LLVMConstInt(i64, 1, 0), ncap, "ncap1");
    LLVMValueRef nd = LLVMBuildCall2(b, LLVMFunctionType(ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
        get_calloc_fn(g), (LLVMValueRef[]){ ncap, ww }, 2, "nd");
    LLVMValueRef oom = LLVMBuildIsNull(b, nd, "oom");
    LLVMBasicBlockRef oom_bb = LLVMAppendBasicBlockInContext(c, fn, "oom");
    LLVMBasicBlockRef go = LLVMAppendBasicBlockInContext(c, fn, "go");
    LLVMBuildCondBr(b, oom, oom_bb, go);
    LLVMPositionBuilderAtEnd(b, oom_bb);
    {
        LLVMTypeRef abty = LLVMFunctionType(LLVMVoidTypeInContext(c), NULL, 0, 0);
        LLVMValueRef ab = LLVMGetNamedFunction(g->mod, "abort");
        if (!ab) ab = LLVMAddFunction(g->mod, "abort", abty);
        LLVMBuildCall2(b, abty, ab, NULL, 0, "");
        LLVMBuildUnreachable(b);
    }
    LLVMPositionBuilderAtEnd(b, go);
    LLVMValueRef cw = LLVMBuildSelect(b, LLVMBuildICmp(b, LLVMIntEQ, cur, zero, "c0w"),
        eight, LLVMBuildAnd(b, cur, fifteen, "cm"), "cw");
    LLVMValueRef csigned = LLVMBuildICmp(b, LLVMIntNE,
        LLVMBuildAnd(b, cur, LLVMConstInt(i64, ZAN_ESZ_SIGNED, 0), "cs"), zero, "csig");
    LLVMBuildBr(b, head);
    LLVMPositionBuilderAtEnd(b, head);
    LLVMValueRef i = LLVMBuildPhi(b, i64, "i");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntULT, i, n, "more"), body, done);
    LLVMPositionBuilderAtEnd(b, body);
    /* 读源槽 → 规范 8 字节槽字 */
    LLVMValueRef sp = LLVMBuildGEP2(b, i8, data, (LLVMValueRef[]){ LLVMBuildMul(b, i, cw, "so") }, 1, "sp");
    /* 按源宽度分支读：不能一律按 8 字节读，会越过窄数组末尾 */
    LLVMBasicBlockRef l8 = LLVMAppendBasicBlockInContext(c, fn, "l8");
    LLVMBasicBlockRef l4 = LLVMAppendBasicBlockInContext(c, fn, "l4");
    LLVMBasicBlockRef l2 = LLVMAppendBasicBlockInContext(c, fn, "l2");
    LLVMBasicBlockRef l1 = LLVMAppendBasicBlockInContext(c, fn, "l1");
    LLVMBasicBlockRef lj = LLVMAppendBasicBlockInContext(c, fn, "lj");
    LLVMValueRef sw = LLVMBuildSwitch(b, cw, l8, 3);
    LLVMAddCase(sw, LLVMConstInt(i64, 4, 0), l4);
    LLVMAddCase(sw, LLVMConstInt(i64, 2, 0), l2);
    LLVMAddCase(sw, LLVMConstInt(i64, 1, 0), l1);
    LLVMValueRef vals[4]; LLVMBasicBlockRef bbs[4];
    LLVMPositionBuilderAtEnd(b, l8);
    vals[0] = LLVMBuildLoad2(b, i64, sp, "v8"); bbs[0] = l8; LLVMBuildBr(b, lj);
    LLVMTypeRef nts[3] = { i32, i16, i8 };
    LLVMBasicBlockRef nbs[3] = { l4, l2, l1 };
    for (int k = 0; k < 3; k++) {
        LLVMPositionBuilderAtEnd(b, nbs[k]);
        LLVMValueRef v = LLVMBuildLoad2(b, nts[k], sp, "vn");
        vals[k + 1] = LLVMBuildSelect(b, csigned,
            LLVMBuildSExt(b, v, i64, "vs"), LLVMBuildZExt(b, v, i64, "vz"), "vx");
        bbs[k + 1] = nbs[k];
        LLVMBuildBr(b, lj);
    }
    LLVMPositionBuilderAtEnd(b, lj);
    LLVMValueRef word = LLVMBuildPhi(b, i64, "word");
    LLVMAddIncoming(word, vals, bbs, 4);
    /* 写目标槽（截断） */
    LLVMValueRef dp = LLVMBuildGEP2(b, i8, nd, (LLVMValueRef[]){ LLVMBuildMul(b, i, ww, "do") }, 1, "dp");
    LLVMBasicBlockRef s8 = LLVMAppendBasicBlockInContext(c, fn, "s8");
    LLVMBasicBlockRef s4 = LLVMAppendBasicBlockInContext(c, fn, "s4");
    LLVMBasicBlockRef s2 = LLVMAppendBasicBlockInContext(c, fn, "s2");
    LLVMBasicBlockRef s1 = LLVMAppendBasicBlockInContext(c, fn, "s1");
    LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(c, fn, "next");
    LLVMValueRef sw2 = LLVMBuildSwitch(b, ww, s8, 3);
    LLVMAddCase(sw2, LLVMConstInt(i64, 4, 0), s4);
    LLVMAddCase(sw2, LLVMConstInt(i64, 2, 0), s2);
    LLVMAddCase(sw2, LLVMConstInt(i64, 1, 0), s1);
    LLVMPositionBuilderAtEnd(b, s8);
    LLVMBuildStore(b, word, dp); LLVMBuildBr(b, next);
    LLVMBasicBlockRef sbs[3] = { s4, s2, s1 };
    for (int k = 0; k < 3; k++) {
        LLVMPositionBuilderAtEnd(b, sbs[k]);
        LLVMBuildStore(b, LLVMBuildTrunc(b, word, nts[k], "wt"), dp);
        LLVMBuildBr(b, next);
    }
    LLVMPositionBuilderAtEnd(b, next);
    LLVMValueRef i1 = LLVMBuildAdd(b, i, LLVMConstInt(i64, 1, 0), "i1");
    LLVMBuildBr(b, head);
    LLVMAddIncoming(i, (LLVMValueRef[]){ zero, i1 }, (LLVMBasicBlockRef[]){ go, next }, 2);
    LLVMPositionBuilderAtEnd(b, done);
    LLVMBuildCall2(b, LLVMFunctionType(LLVMVoidTypeInContext(c), &ptr, 1, 0),
        g->fn_free, &data, 1, "");
    zan_tbaa_tag_meta(g, LLVMBuildStore(b, nd, datap));
    zan_tbaa_tag_meta(g, LLVMBuildStore(b, ncap, capp));
    LLVMBuildBr(b, settag);
    LLVMPositionBuilderAtEnd(b, settag);
    zan_tbaa_tag_meta(g, LLVMBuildStore(b, want, eszp));
    LLVMBuildRetVoid(b);
    LLVMDisposeBuilder(b);
    return fn;
}

/* 冷路径外提：List 扩容（2 倍增长，初值 8），保持热循环 Add 体积极小以便寄存器化 */
LLVMValueRef get_list_grow_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_list_grow");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef list_pt = LLVMPointerType(g->list_struct_type, 0);
    LLVMTypeRef fty = LLVMFunctionType(LLVMVoidTypeInContext(c),
        (LLVMTypeRef[]){ list_pt, i64 }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_list_grow", fty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    {
        static const char *const attrs[] = { "noinline", "cold", "nounwind" };
        for (size_t i = 0; i < 3; i++) {
            unsigned k = LLVMGetEnumAttributeKindForName(attrs[i], strlen(attrs[i]));
            if (k) LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                                           LLVMCreateEnumAttribute(c, k, 0));
        }
        unsigned nc = LLVMGetEnumAttributeKindForName("nocapture", 9);
        if (nc) LLVMAddAttributeAtIndex(fn, 1, LLVMCreateEnumAttribute(c, nc, 0));
    }
    LLVMBuilderRef b = LLVMCreateBuilderInContext(c);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef oom_bb = LLVMAppendBasicBlockInContext(c, fn, "oom");
    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(c, fn, "ok");
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef lp = LLVMGetParam(fn, 0);
    LLVMValueRef esz = LLVMGetParam(fn, 1);
    LLVMValueRef cap_ptr = LLVMBuildStructGEP2(b, g->list_struct_type, lp, 1, "capp");
    LLVMValueRef cap = LLVMBuildLoad2(b, i64, cap_ptr, "cap");
    zan_tbaa_tag_meta(g, cap);
    LLVMValueRef cap_zero = LLVMBuildICmp(b, LLVMIntEQ, cap, LLVMConstInt(i64, 0, 0), "cz");
    LLVMValueRef dbl = LLVMBuildMul(b, cap, LLVMConstInt(i64, 2, 0), "dbl");
    LLVMValueRef ncap = LLVMBuildSelect(b, cap_zero, LLVMConstInt(i64, 8, 0), dbl, "ncap");
    LLVMValueRef df = LLVMBuildStructGEP2(b, g->list_struct_type, lp, 2, "df");
    LLVMValueRef old_data = LLVMBuildLoad2(b, i64ptr, df, "od");
    zan_tbaa_tag_meta(g, old_data);
    LLVMValueRef old_raw = LLVMBuildBitCast(b, old_data, i8ptr, "odr");
    LLVMValueRef nsz = LLVMBuildMul(b, ncap, esz, "nsz");
    LLVMValueRef nd_raw = LLVMBuildCall2(b,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
        g->fn_realloc, (LLVMValueRef[]){ old_raw, nsz }, 2, "nd");
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, nd_raw, "oom"), oom_bb, ok_bb);
    LLVMPositionBuilderAtEnd(b, oom_bb);
    {
        LLVMTypeRef abty = LLVMFunctionType(LLVMVoidTypeInContext(c), NULL, 0, 0);
        LLVMValueRef ab = LLVMGetNamedFunction(g->mod, "abort");
        if (!ab) ab = LLVMAddFunction(g->mod, "abort", abty);
        LLVMBuildCall2(b, abty, ab, NULL, 0, "");
        LLVMBuildUnreachable(b);
    }
    LLVMPositionBuilderAtEnd(b, ok_bb);
    zan_tbaa_tag_meta(g, LLVMBuildStore(b, ncap, cap_ptr));
    LLVMValueRef nd = LLVMBuildBitCast(b, nd_raw, i64ptr, "ndt");
    zan_tbaa_tag_meta(g, LLVMBuildStore(b, nd, df));
    LLVMBuildRetVoid(b);
    LLVMDisposeBuilder(b);
    return fn;
}

/* 冷路径外提：List 批量预留至至少 need_cap（按 max(need_cap, cap*2) 几何扩容） */
LLVMValueRef get_list_reserve_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_list_reserve");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef list_pt = LLVMPointerType(g->list_struct_type, 0);
    LLVMTypeRef fty = LLVMFunctionType(LLVMVoidTypeInContext(c),
        (LLVMTypeRef[]){ list_pt, i64, i64 }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_list_reserve", fty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    {
        static const char *const attrs[] = { "noinline", "cold", "nounwind" };
        for (size_t i = 0; i < 3; i++) {
            unsigned k = LLVMGetEnumAttributeKindForName(attrs[i], strlen(attrs[i]));
            if (k) LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                                           LLVMCreateEnumAttribute(c, k, 0));
        }
        unsigned nc = LLVMGetEnumAttributeKindForName("nocapture", 9);
        if (nc) LLVMAddAttributeAtIndex(fn, 1, LLVMCreateEnumAttribute(c, nc, 0));
    }
    LLVMBuilderRef b = LLVMCreateBuilderInContext(c);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(c, fn, "grow");
    LLVMBasicBlockRef oom_bb = LLVMAppendBasicBlockInContext(c, fn, "oom");
    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(c, fn, "ok");
    LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(c, fn, "ret");
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef lp = LLVMGetParam(fn, 0);
    LLVMValueRef need = LLVMGetParam(fn, 1);
    LLVMValueRef esz = LLVMGetParam(fn, 2);
    LLVMValueRef cap_ptr = LLVMBuildStructGEP2(b, g->list_struct_type, lp, 1, "capp");
    LLVMValueRef cap = LLVMBuildLoad2(b, i64, cap_ptr, "cap");
    zan_tbaa_tag_meta(g, cap);
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntSGT, need, cap, "needg"), grow_bb, ret_bb);
    LLVMPositionBuilderAtEnd(b, grow_bb);
    LLVMValueRef dbl = LLVMBuildMul(b, cap, LLVMConstInt(i64, 2, 0), "dbl");
    LLVMValueRef ncap = LLVMBuildSelect(b, LLVMBuildICmp(b, LLVMIntSGT, dbl, need, "dblgt"),
        dbl, need, "ncap");
    LLVMValueRef df = LLVMBuildStructGEP2(b, g->list_struct_type, lp, 2, "df");
    LLVMValueRef old_data = LLVMBuildLoad2(b, i64ptr, df, "od");
    zan_tbaa_tag_meta(g, old_data);
    LLVMValueRef old_raw = LLVMBuildBitCast(b, old_data, i8ptr, "odr");
    LLVMValueRef nsz = LLVMBuildMul(b, ncap, esz, "nsz");
    LLVMValueRef nd_raw = LLVMBuildCall2(b,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
        g->fn_realloc, (LLVMValueRef[]){ old_raw, nsz }, 2, "nd");
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, nd_raw, "oom"), oom_bb, ok_bb);
    LLVMPositionBuilderAtEnd(b, oom_bb);
    {
        LLVMTypeRef abty = LLVMFunctionType(LLVMVoidTypeInContext(c), NULL, 0, 0);
        LLVMValueRef ab = LLVMGetNamedFunction(g->mod, "abort");
        if (!ab) ab = LLVMAddFunction(g->mod, "abort", abty);
        LLVMBuildCall2(b, abty, ab, NULL, 0, "");
        LLVMBuildUnreachable(b);
    }
    LLVMPositionBuilderAtEnd(b, ok_bb);
    zan_tbaa_tag_meta(g, LLVMBuildStore(b, ncap, cap_ptr));
    LLVMValueRef nd = LLVMBuildBitCast(b, nd_raw, i64ptr, "ndt");
    zan_tbaa_tag_meta(g, LLVMBuildStore(b, nd, df));
    LLVMBuildBr(b, ret_bb);
    LLVMPositionBuilderAtEnd(b, ret_bb);
    LLVMBuildRetVoid(b);
    LLVMDisposeBuilder(b);
    return fn;
}

/* 取 List 的数据指针，先保证槽宽是本访问点要的宽度。 */
LLVMValueRef list_data_for(zan_irgen_t *g, LLVMValueRef list_ptr,
                           zan_type_t *et, const char *nm) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef lp = LLVMBuildBitCast(g->builder, list_ptr,
        LLVMPointerType(g->list_struct_type, 0), "lw.lp");
    LLVMValueRef want = LLVMConstInt(i64, (unsigned long long)list_elem_tag(g, et), 0);
    LLVMValueRef eszp = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 3, "lw.eszp");
    LLVMValueRef esz = LLVMBuildLoad2(g->builder, i64, eszp, "lw.esz");
    zan_tbaa_tag_meta(g, esz);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef fix = LLVMAppendBasicBlockInContext(g->ctx, fn, "lw.fix");
    LLVMBasicBlockRef ok = LLVMAppendBasicBlockInContext(g->ctx, fn, "lw.ok");
    LLVMValueRef same = LLVMBuildICmp(g->builder, LLVMIntEQ, esz, want, "lw.same");
    LLVMValueRef br = LLVMBuildCondBr(g->builder, same, ok, fix);
    zan_set_branch_weights(g, br, 2000, 1);
    LLVMPositionBuilderAtEnd(g->builder, fix);
    LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "lw.cntp");
    LLVMValueRef saved_cnt = LLVMBuildLoad2(g->builder, i64, cntp, "lw.cnt");
    zan_tbaa_tag_cnt(g, saved_cnt);
    LLVMValueRef rfn = list_repack_fn(g);
    LLVMBuildCall2(g->builder, LLVMGlobalGetValueType(rfn), rfn,
        (LLVMValueRef[]){ LLVMBuildBitCast(g->builder, lp, ptr, "lw.lp8"), want }, 2, "");
    /* 显式向调用方暴露 repack 后置条件：esz == want 且 count 不变，便于外提守卫与消越界 */
    zan_tbaa_tag_meta(g, LLVMBuildStore(g->builder, want, eszp));
    zan_tbaa_tag_cnt(g, LLVMBuildStore(g->builder, saved_cnt, cntp));
    LLVMBuildBr(g->builder, ok);
    LLVMPositionBuilderAtEnd(g->builder, ok);
    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0),
        LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "lw.df"),
        nm ? nm : "data");
    zan_tbaa_tag_meta(g, data);
    return data;
}
/* 循环 preheader 已确认槽宽且循环体内绝不重排该表时，直接取数据指针。 */
LLVMValueRef list_data_direct(zan_irgen_t *g, LLVMValueRef list_ptr,
                              const char *nm) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef lp = LLVMBuildBitCast(g->builder, list_ptr,
        LLVMPointerType(g->list_struct_type, 0), "lw.lp");
    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0),
        LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "lw.df"),
        nm ? nm : "data");
    zan_tbaa_tag_meta(g, data);
    return data;
}
bool irgen_is_loop_guarded_list(zan_irgen_t *g, const void *lv) {
    if (!g || !lv)
        return false;
    for (int i = 0; i < g->loop_guarded_list_count; i++) {
        if (g->loop_guarded_lists[i] == lv)
            return true;
    }
    return false;
}
/* 新建 List 的表头宽度标签（各建表点在写 count/cap/data 时一并调用）。 */
void list_header_init_esz(zan_irgen_t *g, LLVMValueRef list_ptr, zan_type_t *et) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef lp = LLVMBuildBitCast(g->builder, list_ptr,
        LLVMPointerType(g->list_struct_type, 0), "lh.lp");
    zan_tbaa_tag_meta(g, LLVMBuildStore(g->builder,
        LLVMConstInt(i64, (unsigned long long)list_elem_tag(g, et), 0),
        LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 3, "lh.eszp")));
}
/* 元素地址：窄槽按字节步长，宽槽沿用槽字索引。 */
LLVMValueRef list_elem_ptr(zan_irgen_t *g, LLVMValueRef data, LLVMValueRef idx,
                           zan_type_t *et, const char *nm) {
    int tag = list_elem_tag(g, et);
    if (!tag) {
        LLVMValueRef widx = slot_word_index(g, idx, elem_slot_words(g, et));
        return LLVMBuildInBoundsGEP2(g->builder, LLVMInt64TypeInContext(g->ctx), data,
                                     &widx, 1, nm ? nm : "ep");
    }
    return LLVMBuildInBoundsGEP2(g->builder, list_slot_type(g, et), data, &idx, 1,
                                 nm ? nm : "ep");
}
/* 读槽并还原成规范 8 字节槽字（窄槽按类型符号扩展/零扩展）。 */
LLVMValueRef list_slot_load_word(zan_irgen_t *g, LLVMValueRef ep, zan_type_t *et,
                                 const char *nm) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int tag = list_elem_tag(g, et);
    if (!tag) {
        LLVMValueRef v = LLVMBuildLoad2(g->builder, i64, ep, nm ? nm : "elem");
        zan_tbaa_tag_elem(g, v);
        return v;
    }
    LLVMValueRef v = LLVMBuildLoad2(g->builder, list_slot_type(g, et), ep, "elem.n");
    zan_tbaa_tag_elem(g, v);
    return (tag & ZAN_ESZ_SIGNED) ? LLVMBuildSExt(g->builder, v, i64, nm ? nm : "elem")
                                  : LLVMBuildZExt(g->builder, v, i64, nm ? nm : "elem");
}
/* 读 List 元素为其类型的值（窄槽先还原槽字，再按元素类型取值）。 */
LLVMValueRef load_collection_slot_value(zan_irgen_t *g, zan_type_t *elem_type,
                                        LLVMValueRef slot_ptr);
LLVMValueRef list_load_value(zan_irgen_t *g, zan_type_t *et, LLVMValueRef ep) {
    if (!list_elem_tag(g, et)) return load_collection_slot_value(g, et, ep);
    LLVMValueRef raw = list_slot_load_word(g, ep, et, "slot.raw");
    LLVMTypeRef el = map_type(g, et);
    if (LLVMGetTypeKind(el) == LLVMFloatTypeKind)
        return LLVMBuildBitCast(g->builder,
            LLVMBuildTrunc(g->builder, raw, LLVMInt32TypeInContext(g->ctx), "slot.f32"),
            el, "slot.f");
    if (LLVMGetTypeKind(el) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(el) < 64)
        return LLVMBuildTrunc(g->builder, raw, el, "slot.int");
    return raw;
}
/* 把规范槽字写进槽（窄槽截断）。 */
void list_slot_store_word(zan_irgen_t *g, LLVMValueRef word, LLVMValueRef ep,
                          zan_type_t *et) {
    int tag = list_elem_tag(g, et);
    if (tag) word = LLVMBuildTrunc(g->builder, word, list_slot_type(g, et), "slot.n");
    zan_tbaa_tag_elem(g, LLVMBuildStore(g->builder, word, ep));
}
