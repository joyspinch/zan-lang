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
    /* 只有种类 1 是字符串；ZAN_DICT_KEY_INT 等原始键按位比较 */
    LLVMValueRef isv = zan_icmp(g->builder, LLVMIntEQ, is_str, LLVMConstInt(i64, 1, 0), "isv");
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

static LLVMValueRef get_dict_rt_hash_fn(zan_irgen_t *g) {
    /* B-ID80：字符串键哈希在运行时——SipHash + 进程级随机盐
     * （hosted 用 OS 熵盐；wasm/bare 确定性盐）。曾是 IR 内联的
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

/* 键哈希（IR 内部函数，可被 LLVM 内联进探测循环）：
 *  - 字符串键：调运行时 SipHash（抗碰撞构造，见上）。
 *  - 非字符串键（整数/引用同一性）：内联带种子的 64 位乘-移位混合（只用
 *    i64 乘法，wasm32 等无 128 位乘法的目标同样适用）+ 进程级秘密种子。
 *    曾经每次查找都外调 SipHash，LLVM 无法内联，是 Dict<int,…> 比 C++/Java
 *    慢 5 倍的主因。种子由运行时 SipHash（随机盐）对固定常量求值得到——同
 *    进程所有模块一致、跨进程不可预测，且不新增运行时 ABI 符号（预编译的
 *    交叉运行时库照常可链）。惰性求值，竞态只会重复写入同一值。 */
static LLVMValueRef get_dict_hash_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_dict_hash_ir");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef fnty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_dict_hash_ir", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMValueRef rth = get_dict_rt_hash_fn(g);
    LLVMTypeRef rth_ty = LLVMGlobalGetValueType(rth);

    LLVMTypeRef seed_ty = LLVMArrayType(i64, 2);
    LLVMValueRef seed = LLVMAddGlobal(g->mod, seed_ty, "__zan_dict_seed");
    LLVMSetLinkage(seed, LLVMInternalLinkage);
    LLVMSetInitializer(seed, LLVMConstNull(seed_ty));
    LLVMValueRef s0p = LLVMConstInBoundsGEP2(seed_ty, seed,
        (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0) }, 2);
    LLVMValueRef s1p = LLVMConstInBoundsGEP2(seed_ty, seed,
        (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 1, 0) }, 2);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef str_bb = LLVMAppendBasicBlockInContext(c, fn, "str");
    LLVMBasicBlockRef raw_bb = LLVMAppendBasicBlockInContext(c, fn, "raw");
    LLVMBasicBlockRef init_bb = LLVMAppendBasicBlockInContext(c, fn, "seed.init");
    LLVMBasicBlockRef mix_bb = LLVMAppendBasicBlockInContext(c, fn, "mix");
    LLVMBasicBlockRef nid_bb = LLVMAppendBasicBlockInContext(c, fn, "notid");
    LLVMBasicBlockRef id_bb = LLVMAppendBasicBlockInContext(c, fn, "ident");
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef key = LLVMGetParam(fn, 0);
    LLVMValueRef is_str = LLVMGetParam(fn, 1);
    LLVMBuildCondBr(b,
                    zan_icmp(b, LLVMIntEQ, is_str, LLVMConstInt(i64, ZAN_DICT_KEY_INT, 0), "isid"),
                    id_bb, nid_bb);

    /* 恒等定位（整数键自适应模式，见 ZAN_DICT_KEY_INT）：h1 = 键本身，相邻键
     * 落相邻槽、顺序访问可被预取；h2 指纹取黄金比乘法高 7 位，只为区分同组
     * 近邻键。无需秘密——最坏探测长度由索引的 plim 上限兜住，不靠指纹。 */
    LLVMPositionBuilderAtEnd(b, id_bb);
    LLVMValueRef ikv = LLVMBuildPtrToInt(b, key, i64, "ikv");
    LLVMValueRef itag =
        zan_lshr(b, zan_mul(b, ikv, LLVMConstInt(i64, 0x9E3779B97F4A7C15ull, 0), "itm"),
                 LLVMConstInt(i64, 57, 0), "itag");
    LLVMBuildRet(b, zan_or(b, zan_shl(b, ikv, LLVMConstInt(i64, 7, 0), "ih1"), itag, "ih"));

    LLVMPositionBuilderAtEnd(b, nid_bb);
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, is_str, LLVMConstInt(i64, 1, 0), "isv"), str_bb,
                    raw_bb);

    LLVMPositionBuilderAtEnd(b, str_bb);
    LLVMBuildRet(b, zan_call2(b, rth_ty, rth, (LLVMValueRef[]){ key, is_str }, 2, "sh"));

    LLVMPositionBuilderAtEnd(b, raw_bb);
    LLVMValueRef s0 = LLVMBuildLoad2(b, i64, s0p, "s0");
    LLVMSetAlignment(s0, 8);
    LLVMSetOrdering(s0, LLVMAtomicOrderingAcquire);
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, s0, LLVMConstInt(i64, 0, 0), "nos"),
        init_bb, mix_bb);

    LLVMPositionBuilderAtEnd(b, init_bb);
    LLVMValueRef k0 = zan_call2(b, rth_ty, rth, (LLVMValueRef[]){
        LLVMConstIntToPtr(LLVMConstInt(i64, 0x5A616E4B30ull, 0), i8ptr),
        LLVMConstInt(i64, 0, 0) }, 2, "k0");
    LLVMValueRef k1 = zan_call2(b, rth_ty, rth, (LLVMValueRef[]){
        LLVMConstIntToPtr(LLVMConstInt(i64, 0x5A616E4B31ull, 0), i8ptr),
        LLVMConstInt(i64, 0, 0) }, 2, "k1");
    k0 = zan_or(b, k0, LLVMConstInt(i64, 1, 0), "k0nz");
    LLVMBuildStore(b, k1, s1p);
    LLVMValueRef st0 = LLVMBuildStore(b, k0, s0p);
    LLVMSetAlignment(st0, 8);
    LLVMSetOrdering(st0, LLVMAtomicOrderingRelease);
    LLVMBuildBr(b, mix_bb);

    LLVMPositionBuilderAtEnd(b, mix_bb);
    LLVMValueRef ks0 = LLVMBuildPhi(b, i64, "ks0");
    LLVMAddIncoming(ks0, (LLVMValueRef[]){ s0, k0 }, (LLVMBasicBlockRef[]){ raw_bb, init_bb }, 2);
    LLVMValueRef ks1 = LLVMBuildLoad2(b, i64, s1p, "ks1");
    /* h = fmix64((key ^ s0) * (s1 | 1))：种子乘子 + murmur3 终结器，低 7 位
     * （h2 指纹）与高位（h1 组号）都充分雪崩 */
    LLVMValueRef h = zan_xor(b, LLVMBuildPtrToInt(b, key, i64, "kv"), ks0, "kx");
    h = zan_mul(b, h, zan_or(b, ks1, LLVMConstInt(i64, 1, 0), "km"), "h0");
    h = zan_xor(b, h, zan_lshr(b, h, LLVMConstInt(i64, 33, 0), "s1"), "h1");
    h = zan_mul(b, h, LLVMConstInt(i64, 0xff51afd7ed558ccdull, 0), "h2m");
    h = zan_xor(b, h, zan_lshr(b, h, LLVMConstInt(i64, 33, 0), "s2"), "h3");
    h = zan_mul(b, h, LLVMConstInt(i64, 0xc4ceb9fe1a85ec53ull, 0), "h4m");
    h = zan_xor(b, h, zan_lshr(b, h, LLVMConstInt(i64, 33, 0), "s3"), "h5");
    LLVMBuildRet(b, h);
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
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

    /* 规范形 icmp eq <16 x i8> → bitcast <16 x i1> 为 i16：LLVM 在 x86 下成
     * pcmpeqb+pmovmskb，在 AArch64 下成 cmeq+按位权归约，其余目标照样得到同一
     * 位掩码。曾走 emit_byte_eq_mask_i64/emit_pmovmskb_portable 的 64 位 SWAR
     * 模拟（每组约 25 条向量指令、每次探测两遍），Mac 上整数键查找比 C++
     * 慢 9 倍、80% 时间耗在这里。那两个助手仍服务 SIMD 内建，语义不动。 */
    LLVMValueRef eq = LLVMBuildICmp(b, LLVMIntEQ, ctrl_vec, h2_vec, "h2.eq");
    LLVMValueRef bits = LLVMBuildBitCast(b, eq, LLVMInt16TypeInContext(c), "h2.bits");
    return LLVMBuildZExt(b, bits, i32, "h2.mask");
}

/* SwissTable 控制字节：EMPTY=0x80，DELETED（墓碑）=0xFE，占用槽 = h2（0..0x7F）。
 * 探测终止/插入落点只认 EMPTY——墓碑既不终止探测也不复用（复用需先确认键
 * 不在后续组，代价不划算）；墓碑数记在索引尾部，累积到负载上限即整表重建。 */
#define ZAN_DICT_CTRL_EMPTY 0x80
#define ZAN_DICT_CTRL_DELETED 0xFE
static LLVMValueRef emit_swisstable_empty_mask(zan_irgen_t *g, LLVMValueRef ctrl_vec) {
    return emit_swisstable_match_mask(g, ctrl_vec,
        LLVMConstInt(LLVMInt8TypeInContext(g->ctx), ZAN_DICT_CTRL_EMPTY, 0));
}

/* 索引布局：ctrl[cap+16] | slots[cap]（i64 条目下标）| tomb（i64 墓碑数）
 *          | plim（i64 探测上限：ZAN_DICT_PLIM_SEEDED = 种子哈希模式；否则为
 *            恒等模式，值是迄今任一插入用到的最大探测组序号，查找探过这么多组
 *            仍未命中即判缺失——稠密键整段占满时缺失查找也不会扫长跑道） */
static LLVMValueRef emit_dict_tomb_ptr(zan_irgen_t *g, LLVMValueRef ix, LLVMValueRef icap) {
    LLVMContextRef c = g->ctx;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMValueRef off = zan_add(g->builder,
        zan_mul(g->builder, icap, LLVMConstInt(i64, 9, 0), "tb.c9"),
        LLVMConstInt(i64, 16, 0), "tb.off");
    LLVMValueRef p = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(c), ix, &off, 1, "tb.p8");
    return LLVMBuildBitCast(g->builder, p, LLVMPointerType(i64, 0), "tb.p");
}

static LLVMValueRef emit_dict_plim_ptr(zan_irgen_t *g, LLVMValueRef ix, LLVMValueRef icap) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef one = LLVMConstInt(i64, 1, 0);
    return LLVMBuildGEP2(g->builder, i64, emit_dict_tomb_ptr(g, ix, icap), &one, 1, "plim.p");
}

/* 本次查找/重建用的哈希种类：字符串键走运行时 SipHash；其余看索引模式 */
static LLVMValueRef emit_dict_hash_kind(zan_irgen_t *g, LLVMValueRef is_str, LLVMValueRef plim) {
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef seeded =
        zan_icmp(b, LLVMIntEQ, plim, LLVMConstInt(i64, ZAN_DICT_PLIM_SEEDED, 0), "hk.seeded");
    LLVMValueRef raw = LLVMBuildSelect(b, seeded, LLVMConstInt(i64, 0, 0),
                                       LLVMConstInt(i64, ZAN_DICT_KEY_INT, 0), "hk.raw");
    return LLVMBuildSelect(b, zan_icmp(b, LLVMIntEQ, is_str, LLVMConstInt(i64, 1, 0), "hk.str"),
                           is_str, raw, "hk");
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
    LLVMValueRef cnt8 = zan_mul(b, cnt, LLVMConstInt(i64, 8, 0), "cnt8");
    LLVMBuildCondBr(b, need, needc, probe);

    /* 增量追加：已有索引、只多出尾部新条目、且（存活 + 墓碑）仍在 87.5% 负载内 */
    LLVMPositionBuilderAtEnd(b, needc);
    LLVMBasicBlockRef needc2 = LLVMAppendBasicBlockInContext(c, fn, "rb.need2");
    LLVMBuildCondBr(b, zan_or(b, noidx, nocap, "noix"), capc, needc2);
    LLVMPositionBuilderAtEnd(b, needc2);
    LLVMValueRef tomb = LLVMBuildLoad2(b, i64, emit_dict_tomb_ptr(g, ix, icap), "tomb");
    LLVMValueRef used8 = zan_mul(b, zan_add(b, cnt, tomb, "used"),
        LLVMConstInt(i64, 8, 0), "used8");
    LLVMValueRef cap7 = zan_mul(b, icap, LLVMConstInt(i64, 7, 0), "cap7");
    LLVMValueRef fits = zan_icmp(b, LLVMIntSLE, used8, cap7, "fits");
    LLVMValueRef grew = zan_icmp(b, LLVMIntSLT, icnt, cnt, "grew");
    LLVMValueRef kept = zan_icmp(b, LLVMIntSGT, icnt, LLVMConstInt(i64, 0, 0), "kept");
    LLVMValueRef append = zan_and(b, zan_and(b, fits, grew, "a1"), kept, "append");
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

    /* 整表重建继承旧索引的哈希模式：恒等模式一旦因探测过长退回种子哈希，
     * 扩容后不再重试（否则每次扩容都白跑一遍恒等建索引） */
    LLVMPositionBuilderAtEnd(b, alloc);
    LLVMBasicBlockRef oldm_bb = LLVMAppendBasicBlockInContext(c, fn, "rb.oldmode");
    LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(c, fn, "rb.free");
    LLVMBuildCondBr(b, zan_or(b, noidx, nocap, "rb.noold"), free_bb, oldm_bb);
    LLVMPositionBuilderAtEnd(b, oldm_bb);
    LLVMValueRef old_plim = LLVMBuildLoad2(b, i64, emit_dict_plim_ptr(g, ix, icap), "old.plim");
    LLVMBuildBr(b, free_bb);
    LLVMPositionBuilderAtEnd(b, free_bb);
    LLVMValueRef inh_plim = LLVMBuildPhi(b, i64, "inh.plim");
    LLVMAddIncoming(inh_plim, (LLVMValueRef[]){LLVMConstInt(i64, 0, 0), old_plim},
                    (LLVMBasicBlockRef[]){alloc, oldm_bb}, 2);
    LLVMValueRef plim_seeded = LLVMConstInt(i64, ZAN_DICT_PLIM_SEEDED, 0);
    LLVMValueRef init_plim = LLVMBuildSelect(
        b,
        zan_and(b, zan_icmp(b, LLVMIntEQ, is_str, LLVMConstInt(i64, ZAN_DICT_KEY_INT, 0), "k.int"),
                zan_icmp(b, LLVMIntNE, inh_plim, plim_seeded, "inh.id"), "start.id"),
        LLVMConstInt(i64, 0, 0), plim_seeded, "init.plim");
    zan_call2(b, ixfree_ty, ixfree_fn, &ix, 1, "");
    LLVMValueRef ncap = LLVMBuildLoad2(b, i64, cap_a, "ncap.v");
    /* total_bytes = (ncap + 16) + (ncap * 8) + 16（墓碑计数 + plim）= ncap * 9 + 32 */
    LLVMValueRef total_bytes = zan_add(b, zan_mul(b, ncap, LLVMConstInt(i64, 9, 0), "ncap9"),
                                       LLVMConstInt(i64, 32, 0), "tot_sz");
    LLVMValueRef nix = zan_call2(b, malloc_ty, malloc_fn, &total_bytes, 1, "nix");
    LLVMValueRef nix_null = zan_icmp(b, LLVMIntEQ, nix, LLVMConstNull(i8ptr), "nix_null");
    LLVMBasicBlockRef alloc_ok = LLVMAppendBasicBlockInContext(c, fn, "rb.allocok");
    LLVMBuildStore(b, LLVMConstNull(i8ptr), ixp);
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), icapp);
    LLVMBuildCondBr(b, nix_null, miss, alloc_ok);
    LLVMPositionBuilderAtEnd(b, alloc_ok);
    /* Initialize control bytes: memset(nix, EMPTY, ncap + 16) */
    LLVMValueRef ctrl_sz = zan_add(b, ncap, LLVMConstInt(i64, 16, 0), "ctrl_sz");
    zan_call2(b, memset_ty, memset_fn,
        (LLVMValueRef[]){ nix, LLVMConstInt(i32, ZAN_DICT_CTRL_EMPTY, 0), ctrl_sz }, 3, "");
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), emit_dict_tomb_ptr(g, nix, ncap));
    LLVMBuildStore(b, init_plim, emit_dict_plim_ptr(g, nix, ncap));
    LLVMBuildStore(b, nix, ixp);
    LLVMBuildStore(b, ncap, icapp);
    LLVMBuildStore(b, cnt, icntp);
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), i_a);
    LLVMBuildBr(b, fcond);

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
    LLVMValueRef fplim_p = emit_dict_plim_ptr(g, cur_ix, cur_cap);
    LLVMValueRef fplim = LLVMBuildLoad2(b, i64, fplim_p, "fplim");
    LLVMValueRef fh = zan_call2(
        b, hash_ty, hashf, (LLVMValueRef[]){fkey, emit_dict_hash_kind(g, is_str, fplim)}, 2, "fh");
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

    /* 恒等模式记最大探测组序号；超过 ZAN_DICT_IDENT_MAX_PROBE 说明键在低位
     * 上成簇（步长为 2 的高次幂、打包键、蓄意构造的碰撞族），整表改用种子
     * 哈希重建——每次操作的探测代价因此始终有常数上界。 */
    LLVMPositionBuilderAtEnd(b, fp_put);
    LLVMValueRef fp_t =
        zan_lshr(b, LLVMBuildLoad2(b, i64, step_a, "fp.step"), LLVMConstInt(i64, 4, 0), "fp.t");
    LLVMBasicBlockRef fp_far = LLVMAppendBasicBlockInContext(c, fn, "rb.far");
    LLVMBasicBlockRef fp_reseed = LLVMAppendBasicBlockInContext(c, fn, "rb.reseed");
    LLVMBasicBlockRef fp_raise = LLVMAppendBasicBlockInContext(c, fn, "rb.raise");
    LLVMBasicBlockRef fp_place = LLVMAppendBasicBlockInContext(c, fn, "rb.place");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntUGT, fp_t, fplim, "fp.far"), fp_far, fp_place);
    LLVMPositionBuilderAtEnd(b, fp_far);
    LLVMBuildCondBr(
        b,
        zan_icmp(b, LLVMIntUGT, fp_t, LLVMConstInt(i64, ZAN_DICT_IDENT_MAX_PROBE, 0), "fp.toofar"),
        fp_reseed, fp_raise);
    LLVMPositionBuilderAtEnd(b, fp_raise);
    LLVMBuildStore(b, fp_t, fplim_p);
    LLVMBuildBr(b, fp_place);
    LLVMPositionBuilderAtEnd(b, fp_reseed);
    LLVMBuildStore(b, LLVMConstInt(i64, ZAN_DICT_PLIM_SEEDED, 0), fplim_p);
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), emit_dict_tomb_ptr(g, cur_ix, cur_cap));
    zan_call2(b, memset_ty, memset_fn,
              (LLVMValueRef[]){cur_ix, LLVMConstInt(i32, ZAN_DICT_CTRL_EMPTY, 0),
                               zan_add(b, cur_cap, LLVMConstInt(i64, 16, 0), "rs.csz")},
              3, "");
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), i_a);
    LLVMBuildBr(b, fcond);
    LLVMPositionBuilderAtEnd(b, fp_place);
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
    LLVMValueRef pplim = LLVMBuildLoad2(b, i64, emit_dict_plim_ptr(g, pix, pcap), "pplim");
    LLVMValueRef ph = zan_call2(
        b, hash_ty, hashf, (LLVMValueRef[]){key, emit_dict_hash_kind(g, is_str, pplim)}, 2, "ph");
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
    LLVMBasicBlockRef p_adv = LLVMAppendBasicBlockInContext(c, fn, "p.adv");
    LLVMBuildCondBr(b,
                    zan_icmp(b, LLVMIntUGE, zan_lshr(b, p_cur_step, LLVMConstInt(i64, 4, 0), "p_t"),
                             pplim, "p_past"),
                    miss, p_adv);
    LLVMPositionBuilderAtEnd(b, p_adv);
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

    /* 命中后调用方会把条目 fi 之后的键值整体前移一格（保持插入序）。
     * 曾在这里把 icnt 清零，逼下一次查找对全部 n 个键重新哈希建索引——
     * 删除密集负载退化为 O(n²)（2 万次删插查 4.5s）。现就地修正索引：
     * 被删条目的槽打墓碑，下标 > fi 的槽减一；只扫控制字节与槽、不哈希。
     * 墓碑累积到负载上限时才回退到整表重建。 */
    LLVMPositionBuilderAtEnd(b, hit);
    LLVMContextRef cx = c;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(cx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMValueRef cnt = LLVMBuildLoad2(b, i64, cntp, "cnt");
    LLVMValueRef ncnt = zan_sub(b, cnt, LLVMConstInt(i64, 1, 0), "cnt.d");
    LLVMBuildStore(b, ncnt, cntp);
    LLVMValueRef ixp = LLVMBuildStructGEP2(b, dty, dp, 4, "ixp");
    LLVMValueRef icapp = LLVMBuildStructGEP2(b, dty, dp, 5, "icapp");
    LLVMValueRef ix = LLVMBuildLoad2(b, i8ptr, ixp, "ix");
    LLVMValueRef icap = LLVMBuildLoad2(b, i64, icapp, "icap");
    LLVMValueRef icnt = LLVMBuildLoad2(b, i64, icntp, "icnt");
    LLVMValueRef cur = zan_and(b, zan_and(b,
        zan_icmp(b, LLVMIntNE, ix, LLVMConstNull(i8ptr), "hasix"),
        zan_icmp(b, LLVMIntSGT, icap, LLVMConstInt(i64, 0, 0), "hascap"), "c1"),
        zan_icmp(b, LLVMIntEQ, icnt, cnt, "fresh"), "cur");
    LLVMBasicBlockRef stale_bb = LLVMAppendBasicBlockInContext(cx, fn, "rm.stale");
    LLVMBasicBlockRef fix_bb = LLVMAppendBasicBlockInContext(cx, fn, "rm.fix");
    LLVMBasicBlockRef lcond = LLVMAppendBasicBlockInContext(cx, fn, "rm.lcond");
    LLVMBasicBlockRef lbody = LLVMAppendBasicBlockInContext(cx, fn, "rm.lbody");
    LLVMBasicBlockRef locc = LLVMAppendBasicBlockInContext(cx, fn, "rm.occ");
    LLVMBasicBlockRef lkill = LLVMAppendBasicBlockInContext(cx, fn, "rm.kill");
    LLVMBasicBlockRef lmirror = LLVMAppendBasicBlockInContext(cx, fn, "rm.mirror");
    LLVMBasicBlockRef lgt = LLVMAppendBasicBlockInContext(cx, fn, "rm.gtchk");
    LLVMBasicBlockRef ldec = LLVMAppendBasicBlockInContext(cx, fn, "rm.dec");
    LLVMBasicBlockRef lnext = LLVMAppendBasicBlockInContext(cx, fn, "rm.next");
    LLVMBasicBlockRef ldone = LLVMAppendBasicBlockInContext(cx, fn, "rm.ldone");
    LLVMBasicBlockRef lfull = LLVMAppendBasicBlockInContext(cx, fn, "rm.full");
    LLVMBasicBlockRef lret = LLVMAppendBasicBlockInContext(cx, fn, "rm.ret");
    LLVMBuildCondBr(b, cur, fix_bb, stale_bb);

    LLVMPositionBuilderAtEnd(b, stale_bb);
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), icntp);
    LLVMBuildRet(b, fi);

    LLVMPositionBuilderAtEnd(b, fix_bb);
    LLVMValueRef soff = zan_add(b, icap, LLVMConstInt(i64, 16, 0), "rm.soff");
    LLVMValueRef slots = LLVMBuildBitCast(b,
        LLVMBuildGEP2(b, i8, ix, &soff, 1, "rm.s8"), i64ptr, "rm.slots");
    LLVMBuildBr(b, lcond);

    LLVMPositionBuilderAtEnd(b, lcond);
    LLVMValueRef iv = LLVMBuildPhi(b, i64, "rm.i");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntSLT, iv, icap, "rm.more"), lbody, ldone);

    LLVMPositionBuilderAtEnd(b, lbody);
    LLVMValueRef ctrl = LLVMBuildLoad2(b, i8, LLVMBuildGEP2(b, i8, ix, &iv, 1, "rm.cp"), "rm.ctrl");
    LLVMValueRef occ = zan_icmp(b, LLVMIntEQ,
        zan_and(b, ctrl, LLVMConstInt(i8, 0x80, 0), "rm.hb"), LLVMConstInt(i8, 0, 0), "rm.isocc");
    LLVMBuildCondBr(b, occ, locc, lnext);

    LLVMPositionBuilderAtEnd(b, locc);
    LLVMValueRef sp = LLVMBuildGEP2(b, i64, slots, &iv, 1, "rm.sp");
    LLVMValueRef sv = LLVMBuildLoad2(b, i64, sp, "rm.sv");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, sv, fi, "rm.self"), lkill, lgt);

    LLVMPositionBuilderAtEnd(b, lkill);
    LLVMValueRef del = LLVMConstInt(i8, ZAN_DICT_CTRL_DELETED, 0);
    LLVMBuildStore(b, del, LLVMBuildGEP2(b, i8, ix, &iv, 1, "rm.kp"));
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntSLT, iv, LLVMConstInt(i64, 16, 0), "rm.lt16"),
        lmirror, lnext);
    LLVMPositionBuilderAtEnd(b, lmirror);
    LLVMValueRef mi = zan_add(b, icap, iv, "rm.mi");
    LLVMBuildStore(b, del, LLVMBuildGEP2(b, i8, ix, &mi, 1, "rm.mp"));
    LLVMBuildBr(b, lnext);

    LLVMPositionBuilderAtEnd(b, lgt);
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntSGT, sv, fi, "rm.after"), ldec, lnext);
    LLVMPositionBuilderAtEnd(b, ldec);
    LLVMBuildStore(b, zan_sub(b, sv, LLVMConstInt(i64, 1, 0), "rm.svd"), sp);
    LLVMBuildBr(b, lnext);

    LLVMPositionBuilderAtEnd(b, lnext);
    LLVMValueRef inext = zan_add(b, iv, LLVMConstInt(i64, 1, 0), "rm.inext");
    LLVMBuildBr(b, lcond);
    LLVMAddIncoming(iv, (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), inext },
        (LLVMBasicBlockRef[]){ fix_bb, lnext }, 2);

    LLVMPositionBuilderAtEnd(b, ldone);
    LLVMValueRef tp = emit_dict_tomb_ptr(g, ix, icap);
    LLVMValueRef nt = zan_add(b, LLVMBuildLoad2(b, i64, tp, "rm.t"),
        LLVMConstInt(i64, 1, 0), "rm.nt");
    LLVMBuildStore(b, nt, tp);
    LLVMBuildStore(b, ncnt, icntp);
    LLVMValueRef over = zan_icmp(b, LLVMIntSGT,
        zan_mul(b, zan_add(b, ncnt, nt, "rm.used"), LLVMConstInt(i64, 8, 0), "rm.u8"),
        zan_mul(b, icap, LLVMConstInt(i64, 7, 0), "rm.c7"), "rm.over");
    LLVMBuildCondBr(b, over, lfull, lret);
    LLVMPositionBuilderAtEnd(b, lfull);
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), icntp);
    LLVMBuildBr(b, lret);
    LLVMPositionBuilderAtEnd(b, lret);
    LLVMBuildRet(b, fi);
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}
/* __zan_dict_set(dict, key, kind)：命中返回条目下标（≥0）；未命中则追加键
 * （值槽由调用方写）并返回 ~新下标（<0）。 */
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
    /* 追加返回 ~cnt（恒负），与命中的非负下标区分，调用方据此一次判定 */
    LLVMBuildRet(g->builder, LLVMBuildNot(g->builder, cnt, "app.ix"));
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* 异常展开栈与处理器栈存储：每线程独立状态块 */

enum {
    EH_F_TOP = 0,
    EH_F_TMPS_TOP,
    EH_F_EXC_OWNED,
    EH_F_CO_DEPTH, /* inline coroutine starts nested on this thread */
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

