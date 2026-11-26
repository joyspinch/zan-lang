static void dict_add_fn_attr(zan_irgen_t *g, LLVMValueRef fn, const char *name) {
    unsigned kind = LLVMGetEnumAttributeKindForName(name, strlen(name));
    if (kind != 0) {
        LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                                LLVMCreateEnumAttribute(g->ctx, kind, 0));
    }
}

static void dict_set_branch_weights(zan_irgen_t *g, LLVMValueRef br,
                                    uint32_t true_w, uint32_t false_w) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef md_elems[3] = {
        LLVMMDStringInContext(g->ctx, "branch_weights", 14),
        LLVMConstInt(i32t, true_w, 0),
        LLVMConstInt(i32t, false_w, 0),
    };
    unsigned prof_kind = LLVMGetMDKindIDInContext(g->ctx, "prof", 4);
    LLVMSetMetadata(br, prof_kind, LLVMMDNodeInContext(g->ctx, md_elems, 3));
}

static LLVMValueRef get_dict_key_eq_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_dict_key_eq");
    if (fn) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i1, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_dict_key_eq", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    dict_add_fn_attr(g, fn, "nounwind");
    LLVMValueRef eqfn = get_str_eq_fn(g, (zan_loc_t){0});
    LLVMTypeRef eqfn_ty = LLVMGlobalGetValueType(eqfn);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef same_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "same");
    LLVMBasicBlockRef diff_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "diff");
    LLVMBasicBlockRef str_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "str");
    LLVMBasicBlockRef false_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "neq");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef a = LLVMGetParam(fn, 0);
    LLVMValueRef b2 = LLVMGetParam(fn, 1);
    LLVMValueRef is_str = LLVMGetParam(fn, 2);
    /* 先查指针同一性：非字符串键一步出结果，字符串键同指针零调用命中 */
    LLVMBuildCondBr(g->builder, zan_icmp(g->builder, LLVMIntEQ, a, b2, "peq"),
        same_bb, diff_bb);
    LLVMPositionBuilderAtEnd(g->builder, same_bb);
    LLVMBuildRet(g->builder, LLVMConstInt(i1, 1, 0));
    LLVMPositionBuilderAtEnd(g->builder, diff_bb);
    LLVMValueRef isv = zan_icmp(g->builder, LLVMIntNE, is_str,
        LLVMConstInt(i64, 0, 0), "isv");
    LLVMBuildCondBr(g->builder, isv, str_bb, false_bb);
    LLVMPositionBuilderAtEnd(g->builder, str_bb);
    LLVMBuildRet(g->builder, zan_call2(g->builder, eqfn_ty, eqfn,
        (LLVMValueRef[]){ a, b2 }, 2, "eq"));
    LLVMPositionBuilderAtEnd(g->builder, false_bb);
    LLVMBuildRet(g->builder, LLVMConstInt(i1, 0, 0));
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
    fn = LLVMAddFunction(g->mod, "__zan_dict_hash", fnty);
    dict_add_fn_attr(g, fn, "nounwind");
    return fn;
}

/* 键哈希（IR 内部函数，可被 LLVM 内联进探测循环）：
 *  - 字符串键：调运行时 SipHash（抗碰撞构造，见上）。
 *  - 非字符串键（整数/引用同一性）：内联带种子的 64 位乘-移位混合（只用
 *    i64 乘法，wasm32 等无 128 位乘法的目标同样适用）+ 进程级秘密种子。
 *    曾经每次查找都外调 SipHash，LLVM 无法内联，是 Dict<int,…> 比 C++/Java
 *    慢 5 倍的主因。种子由运行时 SipHash（随机盐）对固定常量求值得到——同
 *    进程所有模块一致、跨进程不可预测，且不新增运行时 ABI 符号（预编译的
 *    交叉运行时库照常可链）。惰性求值，竞态只会重复写入同一值。 */
static LLVMValueRef get_dict_seed_global(zan_irgen_t *g) {
    LLVMValueRef seed = LLVMGetNamedGlobal(g->mod, "__zan_dict_seed");
    if (seed) return seed;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef seed_ty = LLVMArrayType(i64, 2);
    seed = LLVMAddGlobal(g->mod, seed_ty, "__zan_dict_seed");
    LLVMSetLinkage(seed, LLVMInternalLinkage);
    LLVMSetInitializer(seed, LLVMConstNull(seed_ty));
    return seed;
}

static LLVMValueRef get_dict_seed_init_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_dict_seed_init");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef seed_ty = LLVMArrayType(i64, 2);
    LLVMValueRef seed = get_dict_seed_global(g);
    LLVMValueRef s0p = LLVMConstInBoundsGEP2(seed_ty, seed,
        (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0) }, 2);
    LLVMValueRef s1p = LLVMConstInBoundsGEP2(seed_ty, seed,
        (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 1, 0) }, 2);
    LLVMTypeRef fnty = LLVMFunctionType(i64, NULL, 0, 0);
    fn = LLVMAddFunction(g->mod, "__zan_dict_seed_init", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    dict_add_fn_attr(g, fn, "noinline");
    dict_add_fn_attr(g, fn, "cold");
    dict_add_fn_attr(g, fn, "nounwind");
    LLVMValueRef rth = get_dict_rt_hash_fn(g);
    LLVMTypeRef rth_ty = LLVMGlobalGetValueType(rth);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMPositionBuilderAtEnd(b, entry);
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
    LLVMBuildRet(b, k0);
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}

static LLVMValueRef get_dict_hash_raw_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_dict_hash_raw");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef seed_ty = LLVMArrayType(i64, 2);
    LLVMValueRef seed = get_dict_seed_global(g);
    LLVMValueRef s0p = LLVMConstInBoundsGEP2(seed_ty, seed,
        (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0) }, 2);
    LLVMValueRef s1p = LLVMConstInBoundsGEP2(seed_ty, seed,
        (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 1, 0) }, 2);
    LLVMTypeRef fnty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_dict_hash_raw", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    dict_add_fn_attr(g, fn, "nounwind");
    dict_add_fn_attr(g, fn, "inlinehint");
    LLVMValueRef initf = get_dict_seed_init_fn(g);
    LLVMTypeRef initf_ty = LLVMGlobalGetValueType(initf);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef init_bb = LLVMAppendBasicBlockInContext(c, fn, "seed.init");
    LLVMBasicBlockRef mix_bb = LLVMAppendBasicBlockInContext(c, fn, "mix");
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef key = LLVMGetParam(fn, 0);
    LLVMValueRef s0 = LLVMBuildLoad2(b, i64, s0p, "s0");
    LLVMSetAlignment(s0, 8);
    LLVMSetOrdering(s0, LLVMAtomicOrderingAcquire);
    LLVMValueRef seed_br = LLVMBuildCondBr(
        b, zan_icmp(b, LLVMIntEQ, s0, LLVMConstInt(i64, 0, 0), "nos"),
        init_bb, mix_bb);
    dict_set_branch_weights(g, seed_br, 1, 2000);

    LLVMPositionBuilderAtEnd(b, init_bb);
    LLVMValueRef k0 = zan_call2(b, initf_ty, initf, NULL, 0, "k0");
    LLVMBuildBr(b, mix_bb);

    LLVMPositionBuilderAtEnd(b, mix_bb);
    LLVMValueRef ks0 = LLVMBuildPhi(b, i64, "ks0");
    LLVMAddIncoming(ks0, (LLVMValueRef[]){ s0, k0 }, (LLVMBasicBlockRef[]){ entry, init_bb }, 2);
    LLVMValueRef ks1 = LLVMBuildLoad2(b, i64, s1p, "ks1");
    /* 128 位单乘法随机种子混合（foldhash/wymix）：
     * (key ^ s0) * (s1 | 1) -> lo ^ hi，单条 64x64->128 硬件乘法即可把高低位
     * 充分雪崩到 h1（组号）与 h2（低 7 位指纹），且保持每进程 CSPRNG 防 HashDoS。 */
    LLVMTypeRef i128 = LLVMInt128TypeInContext(c);
    LLVMValueRef kx = zan_xor(b, LLVMBuildPtrToInt(b, key, i64, "kv"), ks0, "kx");
    LLVMValueRef km = zan_or(b, ks1, LLVMConstInt(i64, 1, 0), "km");
    LLVMValueRef prod = zan_mul(b,
        LLVMBuildZExt(b, kx, i128, "kx128"),
        LLVMBuildZExt(b, km, i128, "km128"), "prod");
    LLVMValueRef lo = LLVMBuildTrunc(b, prod, i64, "hlo");
    LLVMValueRef hi = LLVMBuildTrunc(b,
        zan_lshr(b, prod, LLVMConstInt(i128, 64, 0), "prod.hi"), i64, "hhi");
    LLVMValueRef h = zan_xor(b, lo, hi, "hmix");
    LLVMBuildRet(b, h);
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}

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
    dict_add_fn_attr(g, fn, "nounwind");
    LLVMValueRef rth = get_dict_rt_hash_fn(g);
    LLVMTypeRef rth_ty = LLVMGlobalGetValueType(rth);
    LLVMValueRef rawh = get_dict_hash_raw_fn(g);
    LLVMTypeRef rawh_ty = LLVMGlobalGetValueType(rawh);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef str_bb = LLVMAppendBasicBlockInContext(c, fn, "str");
    LLVMBasicBlockRef raw_bb = LLVMAppendBasicBlockInContext(c, fn, "raw");
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef key = LLVMGetParam(fn, 0);
    LLVMValueRef is_str = LLVMGetParam(fn, 1);
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntNE, is_str, LLVMConstInt(i64, 0, 0), "isv"),
        str_bb, raw_bb);

    LLVMPositionBuilderAtEnd(b, str_bb);
    LLVMBuildRet(b, zan_call2(b, rth_ty, rth, (LLVMValueRef[]){ key, is_str }, 2, "sh"));

    LLVMPositionBuilderAtEnd(b, raw_bb);
    LLVMBuildRet(b, zan_call2(b, rawh_ty, rawh, (LLVMValueRef[]){ key }, 1, "rh"));
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

/* 索引布局：ctrl[cap+16] | slots[cap]（i32 条目下标）| tomb（i64 墓碑数）
 * cap 为 >=16 的 2 的幂，因此 ctrl(cap+16) 与 slots(4*cap) 结尾均天然 16 字节对齐。 */
static LLVMValueRef emit_dict_tomb_ptr(zan_irgen_t *g, LLVMValueRef ix, LLVMValueRef icap) {
    LLVMContextRef c = g->ctx;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMValueRef off = zan_add(g->builder,
        zan_mul(g->builder, icap, LLVMConstInt(i64, 5, 0), "tb.c5"),
        LLVMConstInt(i64, 16, 0), "tb.off");
    LLVMValueRef p = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(c), ix, &off, 1, "tb.p8");
    return LLVMBuildBitCast(g->builder, p, LLVMPointerType(i64, 0), "tb.p");
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

/* 冷路径：原地紧缩存在删除空洞的 Dict（保持插入序，消除非尾删的 O(N) 搬移）。 */
LLVMValueRef get_dict_compact_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_dict_compact");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef voidt = LLVMVoidTypeInContext(c);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef dty = g->dict_struct_type;
    LLVMTypeRef fnty = LLVMFunctionType(voidt, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_dict_compact", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    dict_add_fn_attr(g, fn, "noinline");
    dict_add_fn_attr(g, fn, "cold");
    dict_add_fn_attr(g, fn, "nounwind");
    LLVMTypeRef memset_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32, i64 }, 3, 0);
    LLVMValueRef memset_fn = get_libc_fn(g, "memset", memset_ty);
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(c, fn, "ret");
    LLVMBasicBlockRef prep = LLVMAppendBasicBlockInContext(c, fn, "prep");
    LLVMBasicBlockRef cond = LLVMAppendBasicBlockInContext(c, fn, "cond");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(c, fn, "body");
    LLVMBasicBlockRef live = LLVMAppendBasicBlockInContext(c, fn, "live");
    LLVMBasicBlockRef mv = LLVMAppendBasicBlockInContext(c, fn, "mv");
    LLVMBasicBlockRef mv1 = LLVMAppendBasicBlockInContext(c, fn, "mv1");
    LLVMBasicBlockRef mvn = LLVMAppendBasicBlockInContext(c, fn, "mvn");
    LLVMBasicBlockRef mvd = LLVMAppendBasicBlockInContext(c, fn, "mvd");
    LLVMBasicBlockRef step = LLVMAppendBasicBlockInContext(c, fn, "step");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(c, fn, "done");

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef draw = LLVMGetParam(fn, 0);
    LLVMValueRef dnull = zan_icmp(b, LLVMIntEQ, draw, LLVMConstNull(i8ptr), "dnull");
    LLVMBuildCondBr(b, dnull, ret_bb, prep);

    LLVMPositionBuilderAtEnd(b, ret_bb);
    LLVMBuildRetVoid(b);

    LLVMPositionBuilderAtEnd(b, prep);
    LLVMValueRef dp = LLVMBuildBitCast(b, draw, LLVMPointerType(dty, 0), "dp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(b, dty, dp, 0, "cntp");
    LLVMValueRef kp = LLVMBuildStructGEP2(b, dty, dp, 2, "kp");
    LLVMValueRef vp = LLVMBuildStructGEP2(b, dty, dp, 3, "vp");
    LLVMValueRef icntp = LLVMBuildStructGEP2(b, dty, dp, 6, "icntp");
    LLVMValueRef vwp = LLVMBuildStructGEP2(b, dty, dp, 7, "vwp");
    LLVMValueRef hp = LLVMBuildStructGEP2(b, dty, dp, 8, "hp");
    LLVMValueRef deadp = LLVMBuildStructGEP2(b, dty, dp, 9, "deadp");
    LLVMValueRef holes = LLVMBuildLoad2(b, i64, hp, "holes");
    LLVMValueRef dead = LLVMBuildLoad2(b, i8ptr, deadp, "dead");
    LLVMValueRef skip = zan_or(b,
        zan_icmp(b, LLVMIntEQ, holes, LLVMConstInt(i64, 0, 0), "noholes"),
        zan_icmp(b, LLVMIntEQ, dead, LLVMConstNull(i8ptr), "nodead"), "skip");
    LLVMValueRef cnt = LLVMBuildLoad2(b, i64, cntp, "cnt");
    LLVMValueRef ks = LLVMBuildLoad2(b, i8pp, kp, "ks");
    LLVMValueRef vs = LLVMBuildLoad2(b, i64ptr, vp, "vs");
    LLVMValueRef vw = LLVMBuildLoad2(b, i64, vwp, "vw");
    LLVMValueRef vw_is1 = zan_icmp(b, LLVMIntEQ, vw, LLVMConstInt(i64, 1, 0), "vw1");
    LLVMValueRef vw_bytes = zan_mul(b, vw, LLVMConstInt(i64, 8, 0), "vwb");
    LLVMBuildCondBr(b, skip, ret_bb, cond);

    LLVMPositionBuilderAtEnd(b, cond);
    LLVMValueRef ri = LLVMBuildPhi(b, i64, "r");
    LLVMValueRef wi = LLVMBuildPhi(b, i64, "w");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntSLT, ri, cnt, "more"), body, done);

    LLVMPositionBuilderAtEnd(b, body);
    LLVMValueRef db = LLVMBuildLoad2(b, i8,
        LLVMBuildInBoundsGEP2(b, i8, dead, &ri, 1, "dbp"), "db");
    LLVMBuildCondBr(b,
        LLVMBuildICmp(b, LLVMIntNE, db, LLVMConstInt(i8, 0, 0), "isd"),
        step, live);

    LLVMPositionBuilderAtEnd(b, live);
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntNE, wi, ri, "diff"), mv, mvd);

    LLVMPositionBuilderAtEnd(b, mv);
    LLVMValueRef kv = LLVMBuildLoad2(b, i8ptr,
        LLVMBuildInBoundsGEP2(b, i8ptr, ks, &ri, 1, "kr"), "kv");
    LLVMBuildStore(b, kv, LLVMBuildInBoundsGEP2(b, i8ptr, ks, &wi, 1, "kw"));
    LLVMBuildCondBr(b, vw_is1, mv1, mvn);

    LLVMPositionBuilderAtEnd(b, mv1);
    LLVMValueRef vv = LLVMBuildLoad2(b, i64,
        LLVMBuildInBoundsGEP2(b, i64, vs, &ri, 1, "vr"), "vv");
    LLVMBuildStore(b, vv, LLVMBuildInBoundsGEP2(b, i64, vs, &wi, 1, "vw1s"));
    LLVMBuildBr(b, mvd);

    LLVMPositionBuilderAtEnd(b, mvn);
    LLVMValueRef rw = zan_mul(b, ri, vw, "rw");
    LLVMValueRef ww = zan_mul(b, wi, vw, "ww");
    LLVMValueRef vsrc = LLVMBuildBitCast(b,
        LLVMBuildInBoundsGEP2(b, i64, vs, &rw, 1, "vsp"), i8ptr, "vs8");
    LLVMValueRef vdst = LLVMBuildBitCast(b,
        LLVMBuildInBoundsGEP2(b, i64, vs, &ww, 1, "vdp"), i8ptr, "vd8");
    zan_call2(b, memcpy_ty, memcpy_fn, (LLVMValueRef[]){ vdst, vsrc, vw_bytes }, 3, "");
    LLVMBuildBr(b, mvd);

    LLVMPositionBuilderAtEnd(b, mvd);
    LLVMValueRef wi_inc = zan_add(b, wi, LLVMConstInt(i64, 1, 0), "wn");
    LLVMBuildBr(b, step);

    LLVMPositionBuilderAtEnd(b, step);
    LLVMValueRef wi_next = LLVMBuildPhi(b, i64, "wn.phi");
    LLVMAddIncoming(wi_next, (LLVMValueRef[]){ wi, wi_inc },
        (LLVMBasicBlockRef[]){ body, mvd }, 2);
    LLVMValueRef ri_next = zan_add(b, ri, LLVMConstInt(i64, 1, 0), "rn");
    LLVMBuildBr(b, cond);
    LLVMAddIncoming(ri, (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), ri_next },
        (LLVMBasicBlockRef[]){ prep, step }, 2);
    LLVMAddIncoming(wi, (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), wi_next },
        (LLVMBasicBlockRef[]){ prep, step }, 2);

    LLVMPositionBuilderAtEnd(b, done);
    zan_call2(b, memset_ty, memset_fn,
        (LLVMValueRef[]){ dead, LLVMConstInt(i32, 0, 0), cnt }, 3, "");
    LLVMBuildStore(b, wi, cntp);
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), hp);
    LLVMBuildStore(b, LLVMConstInt(i64, (unsigned long long)-1, 1), icntp);
    LLVMBuildRetVoid(b);

    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}

void emit_dict_compact_if_holes(zan_irgen_t *g, LLVMValueRef draw) {
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef dty = g->dict_struct_type;
    LLVMValueRef dp = LLVMBuildBitCast(b, draw, LLVMPointerType(dty, 0), "dcmp.dp");
    LLVMValueRef hp = LLVMBuildStructGEP2(b, dty, dp, 8, "dcmp.hp");
    LLVMValueRef holes = LLVMBuildLoad2(b, i64, hp, "dcmp.h");
    zan_tbaa_tag_cnt(g, holes);
    LLVMValueRef has = zan_icmp(b, LLVMIntNE, holes, LLVMConstInt(i64, 0, 0), "dcmp.has");
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(b));
    LLVMBasicBlockRef do_bb = LLVMAppendBasicBlockInContext(c, cur_fn, "dcmp.do");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(c, cur_fn, "dcmp.done");
    LLVMValueRef br = LLVMBuildCondBr(b, has, do_bb, done_bb);
    dict_set_branch_weights(g, br, 1, 2000);
    LLVMPositionBuilderAtEnd(b, do_bb);
    LLVMValueRef cfn = get_dict_compact_fn(g);
    zan_call2(b, LLVMGlobalGetValueType(cfn), cfn, &draw, 1, "");
    LLVMBuildBr(b, done_bb);
    LLVMPositionBuilderAtEnd(b, done_bb);
}

/* 冷路径：SwissTable 增量追加或扩容重建（从 __zan_dict_find 拆出，避免阻塞热查找内联） */
static LLVMValueRef get_dict_rebuild_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_dict_rebuild");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i1 = LLVMInt1TypeInContext(c);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef i32ptr = LLVMPointerType(i32, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef v16i8 = LLVMVectorType(i8, 16);
    LLVMTypeRef dty = g->dict_struct_type;
    LLVMTypeRef fnty = LLVMFunctionType(i1, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_dict_rebuild", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    dict_add_fn_attr(g, fn, "noinline");
    dict_add_fn_attr(g, fn, "cold");
    dict_add_fn_attr(g, fn, "nounwind");
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
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;

    LLVMBasicBlockRef entry    = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef fail_bb  = LLVMAppendBasicBlockInContext(c, fn, "rb.fail");
    LLVMBasicBlockRef ok_bb    = LLVMAppendBasicBlockInContext(c, fn, "rb.ok");
    LLVMBasicBlockRef needc2   = LLVMAppendBasicBlockInContext(c, fn, "rb.need2");
    LLVMBasicBlockRef capc     = LLVMAppendBasicBlockInContext(c, fn, "rb.capc");
    LLVMBasicBlockRef caploop  = LLVMAppendBasicBlockInContext(c, fn, "rb.caploop");
    LLVMBasicBlockRef capb     = LLVMAppendBasicBlockInContext(c, fn, "rb.capb");
    LLVMBasicBlockRef alloc    = LLVMAppendBasicBlockInContext(c, fn, "rb.alloc");
    LLVMBasicBlockRef reuse_bb = LLVMAppendBasicBlockInContext(c, fn, "rb.reuse");
    LLVMBasicBlockRef do_alloc = LLVMAppendBasicBlockInContext(c, fn, "rb.doalloc");
    LLVMBasicBlockRef alloc_ok = LLVMAppendBasicBlockInContext(c, fn, "rb.allocok");
    LLVMBasicBlockRef append_bb = LLVMAppendBasicBlockInContext(c, fn, "rb.append");
    LLVMBasicBlockRef fcond    = LLVMAppendBasicBlockInContext(c, fn, "rb.cond");
    LLVMBasicBlockRef fbody    = LLVMAppendBasicBlockInContext(c, fn, "rb.body");
    LLVMBasicBlockRef fp_loop  = LLVMAppendBasicBlockInContext(c, fn, "rb.ploop");
    LLVMBasicBlockRef fp_dup   = LLVMAppendBasicBlockInContext(c, fn, "rb.pdup");
    LLVMBasicBlockRef fp_keq   = LLVMAppendBasicBlockInContext(c, fn, "rb.pkeq");
    LLVMBasicBlockRef fp_dup_hit = LLVMAppendBasicBlockInContext(c, fn, "rb.duphit");
    LLVMBasicBlockRef fp_dnext = LLVMAppendBasicBlockInContext(c, fn, "rb.pdnext");
    LLVMBasicBlockRef fp_empty = LLVMAppendBasicBlockInContext(c, fn, "rb.pempty");
    LLVMBasicBlockRef fp_put   = LLVMAppendBasicBlockInContext(c, fn, "rb.put");
    LLVMBasicBlockRef fp_clone = LLVMAppendBasicBlockInContext(c, fn, "rb.pclone");
    LLVMBasicBlockRef fp_store_entry = LLVMAppendBasicBlockInContext(c, fn, "rb.pstore");
    LLVMBasicBlockRef fp_step  = LLVMAppendBasicBlockInContext(c, fn, "rb.step");
    LLVMBasicBlockRef fnext    = LLVMAppendBasicBlockInContext(c, fn, "rb.next");

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef draw = LLVMGetParam(fn, 0);
    LLVMValueRef is_str = LLVMGetParam(fn, 1);

    LLVMValueRef i_a = LLVMBuildAlloca(b, i64, "i");
    LLVMValueRef cnt_a = LLVMBuildAlloca(b, i64, "cnt.cur");
    LLVMValueRef cap_a = LLVMBuildAlloca(b, i64, "ncap");
    LLVMValueRef gidx_a = LLVMBuildAlloca(b, i64, "gidx");
    LLVMValueRef step_a = LLVMBuildAlloca(b, i64, "step");
    LLVMValueRef mmask_a = LLVMBuildAlloca(b, i32, "mmask");
    LLVMValueRef fh2_a = LLVMBuildAlloca(b, i8, "fh2");

    LLVMValueRef dp = LLVMBuildBitCast(b, draw, LLVMPointerType(dty, 0), "dp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(b, dty, dp, 0, "cntp");
    LLVMValueRef cnt = LLVMBuildLoad2(b, i64, cntp, "cnt");
    LLVMBuildStore(b, cnt, cnt_a);
    LLVMValueRef kp = LLVMBuildStructGEP2(b, dty, dp, 2, "kp");
    LLVMValueRef ixp = LLVMBuildStructGEP2(b, dty, dp, 4, "ixp");
    LLVMValueRef icapp = LLVMBuildStructGEP2(b, dty, dp, 5, "icapp");
    LLVMValueRef icntp = LLVMBuildStructGEP2(b, dty, dp, 6, "icntp");
    LLVMValueRef hp = LLVMBuildStructGEP2(b, dty, dp, 8, "hp");
    LLVMValueRef ix = LLVMBuildLoad2(b, i8ptr, ixp, "ix");
    LLVMValueRef icap = LLVMBuildLoad2(b, i64, icapp, "icap");
    LLVMValueRef icnt = LLVMBuildLoad2(b, i64, icntp, "icnt");
    LLVMValueRef noidx = zan_icmp(b, LLVMIntEQ, ix, LLVMConstNull(i8ptr), "noidx");
    LLVMValueRef nocap = zan_icmp(b, LLVMIntSLE, icap, LLVMConstInt(i64, 0, 0), "nocap");

    /* 增量追加：已有索引、只多出尾部新条目、且（存活 + 墓碑）仍在 87.5% 负载内 */
    LLVMBuildCondBr(b, zan_or(b, noidx, nocap, "noix"), capc, needc2);

    LLVMPositionBuilderAtEnd(b, fail_bb);
    LLVMBuildRet(b, LLVMConstInt(i1, 0, 0));

    LLVMPositionBuilderAtEnd(b, ok_bb);
    LLVMBuildRet(b, LLVMConstInt(i1, 1, 0));

    LLVMPositionBuilderAtEnd(b, needc2);
    LLVMValueRef holes = LLVMBuildLoad2(b, i64, hp, "holes");
    LLVMValueRef live_cnt = zan_sub(b, cnt, holes, "live");
    LLVMValueRef tomb = LLVMBuildLoad2(b, i64, emit_dict_tomb_ptr(g, ix, icap), "tomb");
    LLVMValueRef used8 = zan_mul(b, zan_add(b, live_cnt, tomb, "used"),
        LLVMConstInt(i64, 8, 0), "used8");
    LLVMValueRef cap7 = zan_mul(b, icap, LLVMConstInt(i64, 7, 0), "cap7");
    LLVMValueRef fits = zan_icmp(b, LLVMIntSLE, used8, cap7, "fits");
    LLVMValueRef grew = zan_icmp(b, LLVMIntSLT, icnt, cnt, "grew");
    /* icnt >= 0 表示索引与表头一致（含 Clear() 保留的已清空索引）；
     * icnt < 0（-1）表示脏索引，必须整表重建。 */
    LLVMValueRef kept = zan_icmp(b, LLVMIntSGE, icnt, LLVMConstInt(i64, 0, 0), "kept");
    LLVMValueRef append = zan_and(b, zan_and(b, fits, grew, "a1"), kept, "append");
    LLVMBuildCondBr(b, append, append_bb, capc);

    LLVMPositionBuilderAtEnd(b, append_bb);
    LLVMBuildStore(b, cnt, icntp);
    LLVMBuildStore(b, icnt, i_a);
    LLVMBuildBr(b, fcond);

    LLVMPositionBuilderAtEnd(b, capc);
    /* 整表重建前若有删除空洞，先原地紧缩，保证键数组致密且不带无效槽。 */
    emit_dict_compact_if_holes(g, draw);
    LLVMValueRef cnt_rb = LLVMBuildLoad2(b, i64, cntp, "cnt.rb");
    LLVMBuildStore(b, cnt_rb, cnt_a);
    LLVMValueRef cnt8 = zan_mul(b, cnt_rb, LLVMConstInt(i64, 8, 0), "cnt8");
    LLVMBuildStore(b, LLVMConstInt(i64, 16, 0), cap_a);
    /* want = (cnt * 8 + 6) / 7 */
    LLVMValueRef want = zan_sdiv(b,
        zan_add(b, cnt8, LLVMConstInt(i64, 6, 0), "c8p6"),
        LLVMConstInt(i64, 7, 0), "want");
    LLVMBuildBr(b, caploop);
    LLVMPositionBuilderAtEnd(b, caploop);
    LLVMValueRef curcap = LLVMBuildLoad2(b, i64, cap_a, "curcap");
    LLVMValueRef small = zan_icmp(b, LLVMIntSLT, curcap, want, "small");
    LLVMBuildCondBr(b, small, capb, alloc);
    LLVMPositionBuilderAtEnd(b, capb);
    LLVMBuildStore(b, zan_shl(b, curcap, LLVMConstInt(i64, 1, 0), "cap2"), cap_a);
    LLVMBuildBr(b, caploop);

    LLVMPositionBuilderAtEnd(b, alloc);
    LLVMValueRef ncap = LLVMBuildLoad2(b, i64, cap_a, "ncap.v");
    LLVMValueRef can_reuse = zan_and(b,
        zan_icmp(b, LLVMIntNE, ix, LLVMConstNull(i8ptr), "has_oldix"),
        zan_icmp(b, LLVMIntEQ, ncap, icap, "same_cap"), "can_reuse");
    LLVMBuildCondBr(b, can_reuse, reuse_bb, do_alloc);

    LLVMPositionBuilderAtEnd(b, reuse_bb);
    LLVMValueRef r_ctrl_sz = zan_add(b, icap, LLVMConstInt(i64, 16, 0), "r_ctrl_sz");
    zan_call2(b, memset_ty, memset_fn,
        (LLVMValueRef[]){ ix, LLVMConstInt(i32, ZAN_DICT_CTRL_EMPTY, 0), r_ctrl_sz }, 3, "");
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), emit_dict_tomb_ptr(g, ix, icap));
    LLVMBuildStore(b, LLVMBuildLoad2(b, i64, cnt_a, "cnt.ru"), icntp);
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), i_a);
    LLVMBuildBr(b, fcond);

    LLVMPositionBuilderAtEnd(b, do_alloc);
    zan_call2(b, ixfree_ty, ixfree_fn, &ix, 1, "");
    /* total_bytes = (ncap + 16) + (ncap * 4) + 8（墓碑计数）= ncap * 5 + 24 */
    LLVMValueRef total_bytes = zan_add(b,
        zan_mul(b, ncap, LLVMConstInt(i64, 5, 0), "ncap5"),
        LLVMConstInt(i64, 24, 0), "tot_sz");
    LLVMValueRef nix = zan_call2(b, malloc_ty, malloc_fn, &total_bytes, 1, "nix");
    LLVMValueRef nix_null = zan_icmp(b, LLVMIntEQ, nix, LLVMConstNull(i8ptr), "nix_null");
    LLVMBuildStore(b, LLVMConstNull(i8ptr), ixp);
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), icapp);
    LLVMBuildCondBr(b, nix_null, fail_bb, alloc_ok);
    LLVMPositionBuilderAtEnd(b, alloc_ok);
    /* Initialize control bytes: memset(nix, EMPTY, ncap + 16) */
    LLVMValueRef ctrl_sz = zan_add(b, ncap, LLVMConstInt(i64, 16, 0), "ctrl_sz");
    zan_call2(b, memset_ty, memset_fn,
        (LLVMValueRef[]){ nix, LLVMConstInt(i32, ZAN_DICT_CTRL_EMPTY, 0), ctrl_sz }, 3, "");
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), emit_dict_tomb_ptr(g, nix, ncap));
    LLVMBuildStore(b, nix, ixp);
    LLVMBuildStore(b, ncap, icapp);
    LLVMBuildStore(b, LLVMBuildLoad2(b, i64, cnt_a, "cnt.al"), icntp);
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), i_a);
    LLVMBuildBr(b, fcond);

    /* ---- Rebuild / Populate Table Loop ---- */
    LLVMPositionBuilderAtEnd(b, fcond);
    LLVMValueRef fi = LLVMBuildLoad2(b, i64, i_a, "fi");
    LLVMValueRef cnt_lim = LLVMBuildLoad2(b, i64, cnt_a, "cnt.lim");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntSLT, fi, cnt_lim, "fmore"), fbody, ok_bb);

    LLVMPositionBuilderAtEnd(b, fbody);
    LLVMValueRef cur_cap = LLVMBuildLoad2(b, i64, icapp, "cur_cap");
    LLVMValueRef fmask = zan_sub(b, cur_cap, LLVMConstInt(i64, 1, 0), "fmask");
    LLVMValueRef cur_ix = LLVMBuildLoad2(b, i8ptr, ixp, "cur_ix");
    LLVMValueRef slots_off = zan_add(b, cur_cap, LLVMConstInt(i64, 16, 0), "slots_off");
    LLVMValueRef slots_base = LLVMBuildBitCast(b, LLVMBuildInBoundsGEP2(b, i8, cur_ix, &slots_off, 1, "sbase8"), i32ptr, "sbase");
    LLVMValueRef rks = LLVMBuildLoad2(b, i8pp, kp, "rks");
    LLVMValueRef fkey = LLVMBuildLoad2(b, i8ptr, LLVMBuildInBoundsGEP2(b, i8ptr, rks, &fi, 1, "fkeyp"), "fkey");
    LLVMValueRef fh = zan_call2(b, hash_ty, hashf, (LLVMValueRef[]){ fkey, is_str }, 2, "fh");
    LLVMValueRef fh1 = zan_lshr(b, fh, LLVMConstInt(i64, 7, 0), "fh1");
    LLVMValueRef fh2 = LLVMBuildTrunc(b, zan_and(b, fh, LLVMConstInt(i64, 0x7F, 0), "fh2.and"), i8, "fh2");
    LLVMBuildStore(b, fh2, fh2_a);
    LLVMBuildStore(b, zan_and(b, fh1, fmask, "fgidx0"), gidx_a);
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), step_a);
    LLVMBuildBr(b, fp_loop);

    LLVMPositionBuilderAtEnd(b, fp_loop);
    LLVMValueRef cur_gidx = LLVMBuildLoad2(b, i64, gidx_a, "cur_gidx");
    LLVMValueRef cptr = LLVMBuildInBoundsGEP2(b, i8, cur_ix, &cur_gidx, 1, "cptr");
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
    LLVMValueRef dentry32 = LLVMBuildLoad2(b, i32, LLVMBuildInBoundsGEP2(b, i32, slots_base, &dslot, 1, "dentryp"), "dentry32");
    LLVMValueRef dentry = LLVMBuildZExt(b, dentry32, i64, "dentry");
    LLVMValueRef valid_idx = zan_icmp(b, LLVMIntSLT, dentry,
        LLVMBuildLoad2(b, i64, cnt_a, "cnt.dup"), "valid_idx");
    LLVMBuildCondBr(b, valid_idx, fp_keq, fp_dnext);

    LLVMPositionBuilderAtEnd(b, fp_keq);
    LLVMValueRef dkey = LLVMBuildLoad2(b, i8ptr, LLVMBuildInBoundsGEP2(b, i8ptr, rks, &dentry, 1, "dkeyp"), "dkey");
    LLVMValueRef is_same = zan_call2(b, keq_ty, keq, (LLVMValueRef[]){ dkey, fkey, is_str }, 3, "is_same");
    LLVMBuildCondBr(b, is_same, fp_dup_hit, fp_dnext);

    LLVMPositionBuilderAtEnd(b, fp_dup_hit);
    LLVMBuildStore(b, LLVMBuildTrunc(b, fi, i32, "fi32.upd"), LLVMBuildInBoundsGEP2(b, i32, slots_base, &dslot, 1, "upd_slot"));
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
    LLVMBuildStore(b, cur_fh2, LLVMBuildInBoundsGEP2(b, i8, cur_ix, &eslot, 1, "eslotp"));
    /* If eslot < 16, clone sentinel: ctrl[cur_cap + eslot] = fh2 */
    LLVMValueRef eslot_lt16 = zan_icmp(b, LLVMIntSLT, eslot, LLVMConstInt(i64, 16, 0), "eslot_lt16");
    LLVMBuildCondBr(b, eslot_lt16, fp_clone, fp_store_entry);

    LLVMPositionBuilderAtEnd(b, fp_clone);
    LLVMValueRef clone_idx = zan_add(b, cur_cap, eslot, "clone_idx");
    LLVMBuildStore(b, cur_fh2, LLVMBuildInBoundsGEP2(b, i8, cur_ix, &clone_idx, 1, "clonep"));
    LLVMBuildBr(b, fp_store_entry);

    LLVMPositionBuilderAtEnd(b, fp_store_entry);
    LLVMBuildStore(b, LLVMBuildTrunc(b, fi, i32, "fi32.put"), LLVMBuildInBoundsGEP2(b, i32, slots_base, &eslot, 1, "eslot_val"));
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

    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(b, saved);
    return fn;
}

/* 键类型特化版 SwissTable 热查找（__zan_dict_find_str / __zan_dict_find_raw）：
 * 消除运行时 is_str 分支与另一分支的死代码，把热查找缩到 ~45 条 IR 以便内联。 */
static LLVMValueRef get_dict_find_spec_fn(zan_irgen_t *g, int is_str_val) {
    const char *fname = is_str_val ? "__zan_dict_find_str" : "__zan_dict_find_raw";
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fname);
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef i32ptr = LLVMPointerType(i32, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef v16i8 = LLVMVectorType(i8, 16);
    LLVMTypeRef dty = g->dict_struct_type;
    LLVMTypeRef fnty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    fn = LLVMAddFunction(g->mod, fname, fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    dict_add_fn_attr(g, fn, "nounwind");
    dict_add_fn_attr(g, fn, "inlinehint");
    LLVMValueRef hashf = is_str_val ? get_dict_rt_hash_fn(g) : get_dict_hash_raw_fn(g);
    LLVMTypeRef hash_ty = LLVMGlobalGetValueType(hashf);
    LLVMValueRef eqfn = is_str_val ? get_str_eq_fn(g, (zan_loc_t){0}) : NULL;
    LLVMTypeRef eqfn_ty = eqfn ? LLVMGlobalGetValueType(eqfn) : NULL;
    LLVMValueRef rbfn = get_dict_rebuild_fn(g);
    LLVMTypeRef rbfn_ty = LLVMGlobalGetValueType(rbfn);
    LLVMValueRef is_str_const = LLVMConstInt(i64, is_str_val ? 1 : 0, 0);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMBasicBlockRef entry  = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef miss   = LLVMAppendBasicBlockInContext(c, fn, "miss");
    LLVMBasicBlockRef chk    = LLVMAppendBasicBlockInContext(c, fn, "chk");
    LLVMBasicBlockRef live   = LLVMAppendBasicBlockInContext(c, fn, "live");
    LLVMBasicBlockRef do_rb  = LLVMAppendBasicBlockInContext(c, fn, "do_rb");

    LLVMBasicBlockRef probe  = LLVMAppendBasicBlockInContext(c, fn, "probe");
    LLVMBasicBlockRef p_loop = LLVMAppendBasicBlockInContext(c, fn, "p.loop");
    LLVMBasicBlockRef p_mchk = LLVMAppendBasicBlockInContext(c, fn, "p.mchk");
    LLVMBasicBlockRef p_hit  = LLVMAppendBasicBlockInContext(c, fn, "p.hit");
    LLVMBasicBlockRef p_echk = LLVMAppendBasicBlockInContext(c, fn, "p.echk");
    LLVMBasicBlockRef p_step = LLVMAppendBasicBlockInContext(c, fn, "p.step");

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef draw = LLVMGetParam(fn, 0);
    LLVMValueRef key = LLVMGetParam(fn, 1);

    LLVMValueRef gidx_a = LLVMBuildAlloca(b, i64, "gidx");
    LLVMValueRef step_a = LLVMBuildAlloca(b, i64, "step");
    LLVMValueRef mmask_a = LLVMBuildAlloca(b, i32, "mmask");
    LLVMValueRef fh2_a = LLVMBuildAlloca(b, i8, "fh2");

    LLVMValueRef dnull = zan_icmp(b, LLVMIntEQ, draw, LLVMConstNull(i8ptr), "dnull");
    LLVMValueRef dnull_br = LLVMBuildCondBr(b, dnull, miss, chk);
    dict_set_branch_weights(g, dnull_br, 1, 2000);
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
    LLVMValueRef rb_br = LLVMBuildCondBr(b, need, do_rb, probe);
    dict_set_branch_weights(g, rb_br, 1, 2000);

    LLVMPositionBuilderAtEnd(b, do_rb);
    LLVMValueRef rb_ok = zan_call2(b, rbfn_ty, rbfn,
        (LLVMValueRef[]){ draw, is_str_const }, 2, "rb.ok");
    LLVMBuildCondBr(b, rb_ok, probe, miss);

    /* ---- Hot Path Lookup Probe ---- */
    LLVMPositionBuilderAtEnd(b, probe);
    LLVMValueRef pcnt = LLVMBuildLoad2(b, i64, cntp, "pcnt");
    LLVMValueRef pix = LLVMBuildLoad2(b, i8ptr, ixp, "pix");
    LLVMValueRef pcap = LLVMBuildLoad2(b, i64, icapp, "pcap");
    LLVMValueRef pks = LLVMBuildLoad2(b, i8pp, kp, "pks");
    LLVMValueRef pmask = zan_sub(b, pcap, LLVMConstInt(i64, 1, 0), "pmask");
    LLVMValueRef p_slots_off = zan_add(b, pcap, LLVMConstInt(i64, 16, 0), "p_slots_off");
    LLVMValueRef p_slots_base = LLVMBuildBitCast(b,
        LLVMBuildInBoundsGEP2(b, i8, pix, &p_slots_off, 1, "p_sbase8"), i32ptr, "p_sbase");
    LLVMValueRef ph = is_str_val
        ? zan_call2(b, hash_ty, hashf, (LLVMValueRef[]){ key, is_str_const }, 2, "ph")
        : zan_call2(b, hash_ty, hashf, (LLVMValueRef[]){ key }, 1, "ph");
    LLVMValueRef ph1 = zan_lshr(b, ph, LLVMConstInt(i64, 7, 0), "ph1");
    LLVMValueRef ph2 = LLVMBuildTrunc(b, zan_and(b, ph, LLVMConstInt(i64, 0x7F, 0), "ph2.and"), i8, "ph2");
    LLVMBuildStore(b, ph2, fh2_a);
    LLVMBuildStore(b, zan_and(b, ph1, pmask, "pgidx0"), gidx_a);
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), step_a);
    LLVMBuildBr(b, p_loop);

    LLVMPositionBuilderAtEnd(b, p_loop);
    LLVMValueRef p_gidx = LLVMBuildLoad2(b, i64, gidx_a, "p_gidx");
    LLVMValueRef p_cptr = LLVMBuildInBoundsGEP2(b, i8, pix, &p_gidx, 1, "p_cptr");
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
    LLVMValueRef p_entry32 = LLVMBuildLoad2(b, i32,
        LLVMBuildInBoundsGEP2(b, i32, p_slots_base, &p_slot, 1, "p_entryp"), "p_entry32");
    LLVMValueRef p_entry = LLVMBuildZExt(b, p_entry32, i64, "p_entry");
    LLVMValueRef p_valid = zan_icmp(b, LLVMIntSLT, p_entry, pcnt, "p_valid");
    LLVMBasicBlockRef p_keq_bb = LLVMAppendBasicBlockInContext(c, fn, "p.keq");
    LLVMBasicBlockRef p_mnext_bb = LLVMAppendBasicBlockInContext(c, fn, "p.mnext");
    LLVMBuildCondBr(b, p_valid, p_keq_bb, p_mnext_bb);

    LLVMPositionBuilderAtEnd(b, p_keq_bb);
    LLVMValueRef p_cand_key = LLVMBuildLoad2(b, i8ptr,
        LLVMBuildInBoundsGEP2(b, i8ptr, pks, &p_entry, 1, "p_cand_keyp"), "p_cand_key");
    if (is_str_val) {
        LLVMBasicBlockRef p_streq_bb = LLVMAppendBasicBlockInContext(c, fn, "p.streq");
        LLVMValueRef p_peq = zan_icmp(b, LLVMIntEQ, p_cand_key, key, "p_peq");
        LLVMBuildCondBr(b, p_peq, p_hit, p_streq_bb);
        LLVMPositionBuilderAtEnd(b, p_streq_bb);
        LLVMValueRef p_matched = zan_call2(b, eqfn_ty, eqfn,
            (LLVMValueRef[]){ p_cand_key, key }, 2, "p_matched");
        LLVMBuildCondBr(b, p_matched, p_hit, p_mnext_bb);
    } else {
        LLVMValueRef p_matched = zan_icmp(b, LLVMIntEQ, p_cand_key, key, "p_matched");
        LLVMBuildCondBr(b, p_matched, p_hit, p_mnext_bb);
    }

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

static LLVMValueRef get_dict_find_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_dict_find");
    if (fn) return fn;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef fnty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_dict_find", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    dict_add_fn_attr(g, fn, "nounwind");
    LLVMValueRef fstr = get_dict_find_spec_fn(g, 1);
    LLVMValueRef fraw = get_dict_find_spec_fn(g, 0);
    LLVMTypeRef spec_ty = LLVMGlobalGetValueType(fstr);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
    LLVMBasicBlockRef entry  = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef str_bb = LLVMAppendBasicBlockInContext(c, fn, "str");
    LLVMBasicBlockRef raw_bb = LLVMAppendBasicBlockInContext(c, fn, "raw");
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef draw = LLVMGetParam(fn, 0);
    LLVMValueRef key = LLVMGetParam(fn, 1);
    LLVMValueRef is_str = LLVMGetParam(fn, 2);
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntNE, is_str, LLVMConstInt(i64, 0, 0), "isv"),
        str_bb, raw_bb);
    LLVMPositionBuilderAtEnd(b, str_bb);
    LLVMBuildRet(b, zan_call2(b, spec_ty, fstr, (LLVMValueRef[]){ draw, key }, 2, "fs"));
    LLVMPositionBuilderAtEnd(b, raw_bb);
    LLVMBuildRet(b, zan_call2(b, spec_ty, fraw, (LLVMValueRef[]){ draw, key }, 2, "fr"));
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
    dict_add_fn_attr(g, fn, "nounwind");
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

    /* 命中后统一走 O(1) SIMD 探测把 SwissTable 对应槽打墓碑（尾删与非尾删均 O(1)！）：
     * 尾元素删除直接递减 cnt 并顺手回收紧邻尾部空洞；非尾元素删除惰性分配 dead 标记数组，
     * 置 dead[fi]=1、holes++，零 memmove、零全表扫描；仅在扩容/重建/Keys/Values/foreach
     * 时原地紧缩，保持插入序且均摊严格 O(1)。 */
    LLVMPositionBuilderAtEnd(b, hit);
    LLVMContextRef cx = c;
    LLVMTypeRef i1 = LLVMInt1TypeInContext(cx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(cx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(cx);
    LLVMTypeRef i32ptr = LLVMPointerType(i32, 0);
    LLVMValueRef cnt = LLVMBuildLoad2(b, i64, cntp, "cnt");
    LLVMValueRef capp = LLVMBuildStructGEP2(b, dty, dp, 1, "capp");
    LLVMValueRef ixp = LLVMBuildStructGEP2(b, dty, dp, 4, "ixp");
    LLVMValueRef icapp = LLVMBuildStructGEP2(b, dty, dp, 5, "icapp");
    LLVMValueRef hp = LLVMBuildStructGEP2(b, dty, dp, 8, "hp");
    LLVMValueRef deadp = LLVMBuildStructGEP2(b, dty, dp, 9, "deadp");
    LLVMValueRef ix = LLVMBuildLoad2(b, i8ptr, ixp, "ix");
    LLVMValueRef icap = LLVMBuildLoad2(b, i64, icapp, "icap");
    LLVMValueRef icnt = LLVMBuildLoad2(b, i64, icntp, "icnt");
    LLVMValueRef cur = zan_and(b, zan_and(b,
        zan_icmp(b, LLVMIntNE, ix, LLVMConstNull(i8ptr), "hasix"),
        zan_icmp(b, LLVMIntSGT, icap, LLVMConstInt(i64, 0, 0), "hascap"), "c1"),
        zan_icmp(b, LLVMIntEQ, icnt, cnt, "fresh"), "cur");
    LLVMBasicBlockRef stale_bb = LLVMAppendBasicBlockInContext(cx, fn, "rm.stale");
    LLVMBasicBlockRef tprobe = LLVMAppendBasicBlockInContext(cx, fn, "rm.tprobe");
    LLVMBasicBlockRef tloop  = LLVMAppendBasicBlockInContext(cx, fn, "rm.tloop");
    LLVMBasicBlockRef tmchk  = LLVMAppendBasicBlockInContext(cx, fn, "rm.tmchk");
    LLVMBasicBlockRef tnextm = LLVMAppendBasicBlockInContext(cx, fn, "rm.tnextm");
    LLVMBasicBlockRef tkill  = LLVMAppendBasicBlockInContext(cx, fn, "rm.tkill");
    LLVMBasicBlockRef tmirr  = LLVMAppendBasicBlockInContext(cx, fn, "rm.tmirr");
    LLVMBasicBlockRef techk  = LLVMAppendBasicBlockInContext(cx, fn, "rm.techk");
    LLVMBasicBlockRef killed_bb = LLVMAppendBasicBlockInContext(cx, fn, "rm.killed");
    LLVMBasicBlockRef mark_bb = LLVMAppendBasicBlockInContext(cx, fn, "rm.mark");
    LLVMBasicBlockRef tail_bb = LLVMAppendBasicBlockInContext(cx, fn, "rm.tail");
    LLVMBasicBlockRef trim_cond = LLVMAppendBasicBlockInContext(cx, fn, "rm.trcond");
    LLVMBasicBlockRef trim_chk = LLVMAppendBasicBlockInContext(cx, fn, "rm.trchk");
    LLVMBasicBlockRef trim_step = LLVMAppendBasicBlockInContext(cx, fn, "rm.trstep");
    LLVMBasicBlockRef trim_done = LLVMAppendBasicBlockInContext(cx, fn, "rm.trdone");
    LLVMBasicBlockRef tail_icnt = LLVMAppendBasicBlockInContext(cx, fn, "rm.ticnt");
    LLVMBasicBlockRef nt_bb = LLVMAppendBasicBlockInContext(cx, fn, "rm.nontail");
    LLVMBasicBlockRef nt_alloc = LLVMAppendBasicBlockInContext(cx, fn, "rm.dalloc");
    LLVMBasicBlockRef nt_set = LLVMAppendBasicBlockInContext(cx, fn, "rm.dset");
    LLVMBasicBlockRef ovf_bb = LLVMAppendBasicBlockInContext(cx, fn, "rm.ovf");
    LLVMBasicBlockRef lfull = LLVMAppendBasicBlockInContext(cx, fn, "rm.full");
    LLVMBasicBlockRef lret = LLVMAppendBasicBlockInContext(cx, fn, "rm.ret");
    LLVMBuildCondBr(b, cur, tprobe, stale_bb);

    LLVMPositionBuilderAtEnd(b, stale_bb);
    LLVMBuildStore(b, LLVMConstInt(i64, (unsigned long long)-1, 1), icntp);
    LLVMBuildBr(b, mark_bb);

    LLVMPositionBuilderAtEnd(b, tprobe);
    LLVMValueRef soff = zan_add(b, icap, LLVMConstInt(i64, 16, 0), "rm.soff");
    LLVMValueRef slots = LLVMBuildBitCast(b,
        LLVMBuildInBoundsGEP2(b, i8, ix, &soff, 1, "rm.s8"), i32ptr, "rm.slots");
    LLVMValueRef del = LLVMConstInt(i8, ZAN_DICT_CTRL_DELETED, 0);
    LLVMTypeRef v16i8 = LLVMVectorType(i8, 16);
    LLVMValueRef hashf = get_dict_hash_fn(g);
    LLVMValueRef th = zan_call2(b, LLVMGlobalGetValueType(hashf), hashf,
        (LLVMValueRef[]){ key, is_str }, 2, "rm.th");
    LLVMValueRef tmask = zan_sub(b, icap, LLVMConstInt(i64, 1, 0), "rm.tmask");
    LLVMValueRef th1 = zan_lshr(b, th, LLVMConstInt(i64, 7, 0), "rm.th1");
    LLVMValueRef th2 = LLVMBuildTrunc(b,
        zan_and(b, th, LLVMConstInt(i64, 0x7F, 0), "rm.th2a"), i8, "rm.th2");
    LLVMValueRef tg0 = zan_and(b, th1, tmask, "rm.tg0");
    LLVMValueRef fi32 = LLVMBuildTrunc(b, fi, i32, "rm.fi32");
    LLVMBuildBr(b, tloop);

    LLVMPositionBuilderAtEnd(b, tloop);
    LLVMValueRef tg = LLVMBuildPhi(b, i64, "rm.tg");
    LLVMValueRef tstep = LLVMBuildPhi(b, i64, "rm.tstep");
    LLVMValueRef tcptr = LLVMBuildInBoundsGEP2(b, i8, ix, &tg, 1, "rm.tcp");
    LLVMValueRef tcvec = LLVMBuildLoad2(b, v16i8,
        LLVMBuildBitCast(b, tcptr, LLVMPointerType(v16i8, 0), "rm.tcvp"), "rm.tcv");
    LLVMSetAlignment(tcvec, 1);
    LLVMValueRef tmm0 = emit_swisstable_match_mask(g, tcvec, th2);
    LLVMBuildCondBr(b,
        LLVMBuildICmp(b, LLVMIntNE, tmm0, LLVMConstInt(i32, 0, 0), "rm.thasm"),
        tmchk, techk);

    LLVMPositionBuilderAtEnd(b, tmchk);
    LLVMValueRef tmm = LLVMBuildPhi(b, i32, "rm.tmm");
    LLVMValueRef tbit = emit_cttz32(g, tmm);
    LLVMValueRef tslot = zan_and(b,
        zan_add(b, tg, LLVMBuildZExt(b, tbit, i64, "rm.tb64"), "rm.ts0"),
        tmask, "rm.ts");
    LLVMValueRef tsv32 = LLVMBuildLoad2(b, i32,
        LLVMBuildInBoundsGEP2(b, i32, slots, &tslot, 1, "rm.tsp"), "rm.tsv");
    LLVMBuildCondBr(b,
        LLVMBuildICmp(b, LLVMIntEQ, tsv32, fi32, "rm.thit"),
        tkill, tnextm);

    LLVMPositionBuilderAtEnd(b, tnextm);
    LLVMValueRef tmm_n = LLVMBuildAnd(b, tmm,
        LLVMBuildSub(b, tmm, LLVMConstInt(i32, 1, 0), "rm.tm1"), "rm.tmn");
    LLVMAddIncoming(tmm, (LLVMValueRef[]){ tmm0, tmm_n },
        (LLVMBasicBlockRef[]){ tloop, tnextm }, 2);
    LLVMBuildCondBr(b,
        LLVMBuildICmp(b, LLVMIntNE, tmm_n, LLVMConstInt(i32, 0, 0), "rm.tmmore"),
        tmchk, techk);

    LLVMPositionBuilderAtEnd(b, tkill);
    LLVMBuildStore(b, del, LLVMBuildInBoundsGEP2(b, i8, ix, &tslot, 1, "rm.tkp"));
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntSLT, tslot, LLVMConstInt(i64, 16, 0), "rm.tlt16"),
        tmirr, killed_bb);
    LLVMPositionBuilderAtEnd(b, tmirr);
    LLVMValueRef tmi = zan_add(b, icap, tslot, "rm.tmi");
    LLVMBuildStore(b, del, LLVMBuildInBoundsGEP2(b, i8, ix, &tmi, 1, "rm.tmp"));
    LLVMBuildBr(b, killed_bb);

    LLVMPositionBuilderAtEnd(b, techk);
    LLVMValueRef tem = emit_swisstable_empty_mask(g, tcvec);
    LLVMValueRef tstep_n = zan_add(b, tstep, LLVMConstInt(i64, 16, 0), "rm.tstn");
    LLVMValueRef tg_n = zan_and(b, zan_add(b, tg, tstep_n, "rm.tgn0"), tmask, "rm.tgn");
    LLVMAddIncoming(tg, (LLVMValueRef[]){ tg0, tg_n },
        (LLVMBasicBlockRef[]){ tprobe, techk }, 2);
    LLVMAddIncoming(tstep, (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), tstep_n },
        (LLVMBasicBlockRef[]){ tprobe, techk }, 2);
    LLVMValueRef tstop = zan_or(b,
        LLVMBuildICmp(b, LLVMIntNE, tem, LLVMConstInt(i32, 0, 0), "rm.hasemp"),
        zan_icmp(b, LLVMIntSGE, tstep_n, icap, "rm.tlim"), "rm.tstop");
    LLVMBuildCondBr(b, tstop, stale_bb, tloop);

    LLVMPositionBuilderAtEnd(b, killed_bb);
    LLVMValueRef tp = emit_dict_tomb_ptr(g, ix, icap);
    LLVMValueRef nt = zan_add(b, LLVMBuildLoad2(b, i64, tp, "rm.t"),
        LLVMConstInt(i64, 1, 0), "rm.nt");
    LLVMBuildStore(b, nt, tp);
    LLVMBuildBr(b, mark_bb);

    LLVMPositionBuilderAtEnd(b, mark_bb);
    LLVMValueRef ix_fresh = LLVMBuildPhi(b, i1, "rm.ixok");
    LLVMValueRef nt_val = LLVMBuildPhi(b, i64, "rm.ntv");
    LLVMAddIncoming(ix_fresh,
        (LLVMValueRef[]){ LLVMConstInt(i1, 1, 0), LLVMConstInt(i1, 0, 0) },
        (LLVMBasicBlockRef[]){ killed_bb, stale_bb }, 2);
    LLVMAddIncoming(nt_val,
        (LLVMValueRef[]){ nt, LLVMConstInt(i64, 0, 0) },
        (LLVMBasicBlockRef[]){ killed_bb, stale_bb }, 2);
    LLVMValueRef ncnt0 = zan_sub(b, cnt, LLVMConstInt(i64, 1, 0), "rm.cntd");
    LLVMValueRef is_tail = zan_icmp(b, LLVMIntEQ, fi, ncnt0, "rm.istail");
    LLVMValueRef holes0 = LLVMBuildLoad2(b, i64, hp, "rm.h0");
    LLVMValueRef dead0 = LLVMBuildLoad2(b, i8ptr, deadp, "rm.d0");
    LLVMBuildCondBr(b, is_tail, tail_bb, nt_bb);

    /* 尾元素删除：递减 cnt，若前方紧邻旧空洞一并顺手收缩 */
    LLVMPositionBuilderAtEnd(b, tail_bb);
    LLVMValueRef has_d = zan_and(b,
        zan_icmp(b, LLVMIntSGT, holes0, LLVMConstInt(i64, 0, 0), "rm.trh"),
        zan_icmp(b, LLVMIntNE, dead0, LLVMConstNull(i8ptr), "rm.trd"), "rm.can_tr");
    LLVMBuildCondBr(b, has_d, trim_cond, trim_done);

    LLVMPositionBuilderAtEnd(b, trim_cond);
    LLVMValueRef tc = LLVMBuildPhi(b, i64, "rm.tc");
    LLVMValueRef tholes = LLVMBuildPhi(b, i64, "rm.th");
    LLVMValueRef tr_more = zan_and(b,
        zan_icmp(b, LLVMIntSGT, tc, LLVMConstInt(i64, 0, 0), "rm.tcpos"),
        zan_icmp(b, LLVMIntSGT, tholes, LLVMConstInt(i64, 0, 0), "rm.thpos"), "rm.trm");
    LLVMBuildCondBr(b, tr_more, trim_chk, trim_done);

    LLVMPositionBuilderAtEnd(b, trim_chk);
    LLVMValueRef prev_i = zan_sub(b, tc, LLVMConstInt(i64, 1, 0), "rm.tpi");
    LLVMValueRef prev_dp = LLVMBuildInBoundsGEP2(b, i8, dead0, &prev_i, 1, "rm.tdp");
    LLVMValueRef prev_d = LLVMBuildLoad2(b, i8, prev_dp, "rm.tdv");
    LLVMBuildCondBr(b,
        LLVMBuildICmp(b, LLVMIntNE, prev_d, LLVMConstInt(i8, 0, 0), "rm.tisd"),
        trim_step, trim_done);

    LLVMPositionBuilderAtEnd(b, trim_step);
    LLVMBuildStore(b, LLVMConstInt(i8, 0, 0), prev_dp);
    LLVMValueRef th_dec = zan_sub(b, tholes, LLVMConstInt(i64, 1, 0), "rm.thd");
    LLVMBuildBr(b, trim_cond);
    LLVMAddIncoming(tc, (LLVMValueRef[]){ ncnt0, prev_i },
        (LLVMBasicBlockRef[]){ tail_bb, trim_step }, 2);
    LLVMAddIncoming(tholes, (LLVMValueRef[]){ holes0, th_dec },
        (LLVMBasicBlockRef[]){ tail_bb, trim_step }, 2);

    LLVMPositionBuilderAtEnd(b, trim_done);
    LLVMValueRef fin_cnt = LLVMBuildPhi(b, i64, "rm.fcnt");
    LLVMValueRef fin_holes = LLVMBuildPhi(b, i64, "rm.fh");
    LLVMAddIncoming(fin_cnt, (LLVMValueRef[]){ ncnt0, tc, tc },
        (LLVMBasicBlockRef[]){ tail_bb, trim_cond, trim_chk }, 3);
    LLVMAddIncoming(fin_holes, (LLVMValueRef[]){ holes0, tholes, tholes },
        (LLVMBasicBlockRef[]){ tail_bb, trim_cond, trim_chk }, 3);
    LLVMBuildStore(b, fin_cnt, cntp);
    LLVMBuildStore(b, fin_holes, hp);
    LLVMBuildCondBr(b, ix_fresh, tail_icnt, lret);

    LLVMPositionBuilderAtEnd(b, tail_icnt);
    LLVMBuildStore(b, fin_cnt, icntp);
    LLVMBuildBr(b, ovf_bb);

    /* 非尾元素删除：惰性分配 dead 标记数组，O(1) 标记 dead[fi]=1, holes++ */
    LLVMPositionBuilderAtEnd(b, nt_bb);
    LLVMValueRef no_dead = zan_icmp(b, LLVMIntEQ, dead0, LLVMConstNull(i8ptr), "rm.nod");
    LLVMBuildCondBr(b, no_dead, nt_alloc, nt_set);

    LLVMPositionBuilderAtEnd(b, nt_alloc);
    LLVMValueRef cap_cur = LLVMBuildLoad2(b, i64, capp, "rm.cap");
    LLVMTypeRef calloc_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0);
    LLVMValueRef ndead = zan_call2(b, calloc_ty, get_calloc_fn(g),
        (LLVMValueRef[]){ cap_cur, LLVMConstInt(i64, 1, 0) }, 2, "rm.ndead");
    zan_irgen_emit_oom_check(g, fn, ndead);
    LLVMBuildStore(b, ndead, deadp);
    LLVMBasicBlockRef nt_alloc_end = LLVMGetInsertBlock(b);
    LLVMBuildBr(b, nt_set);

    LLVMPositionBuilderAtEnd(b, nt_set);
    LLVMValueRef dead_use = LLVMBuildPhi(b, i8ptr, "rm.duse");
    LLVMAddIncoming(dead_use, (LLVMValueRef[]){ dead0, ndead },
        (LLVMBasicBlockRef[]){ nt_bb, nt_alloc_end }, 2);
    LLVMBuildStore(b, LLVMConstInt(i8, 1, 0),
        LLVMBuildInBoundsGEP2(b, i8, dead_use, &fi, 1, "rm.setp"));
    LLVMBuildStore(b, zan_add(b, holes0, LLVMConstInt(i64, 1, 0), "rm.hp1"), hp);
    LLVMBuildCondBr(b, ix_fresh, ovf_bb, lret);

    LLVMPositionBuilderAtEnd(b, ovf_bb);
    LLVMValueRef c_now = LLVMBuildLoad2(b, i64, cntp, "rm.cnow");
    LLVMValueRef h_now = LLVMBuildLoad2(b, i64, hp, "rm.hnow");
    LLVMValueRef live_now = zan_sub(b, c_now, h_now, "rm.lnow");
    LLVMValueRef over = zan_icmp(b, LLVMIntSGT,
        zan_mul(b, zan_add(b, live_now, nt_val, "rm.used"), LLVMConstInt(i64, 8, 0), "rm.u8"),
        zan_mul(b, icap, LLVMConstInt(i64, 7, 0), "rm.c7"), "rm.over");
    LLVMBuildCondBr(b, over, lfull, lret);
    LLVMPositionBuilderAtEnd(b, lfull);
    LLVMBuildStore(b, LLVMConstInt(i64, (unsigned long long)-1, 1), icntp);
    LLVMBuildBr(b, lret);
    LLVMPositionBuilderAtEnd(b, lret);
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
    dict_add_fn_attr(g, fn, "nounwind");
    LLVMTypeRef realloc_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0);
    LLVMTypeRef free_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMValueRef free_fn = get_libc_fn(g, "free", free_ty);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef hit_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "hit");
    LLVMBasicBlockRef app_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "app");
    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "grow");
    LLVMBasicBlockRef realloc_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "realloc");
    LLVMBasicBlockRef put_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "put");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef draw = LLVMGetParam(fn, 0);
    LLVMValueRef key = LLVMGetParam(fn, 1);
    LLVMValueRef is_str = LLVMGetParam(fn, 2);
    LLVMValueRef dp = LLVMBuildBitCast(g->builder, draw,
        LLVMPointerType(g->dict_struct_type, 0), "dp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
    LLVMValueRef capp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 1, "capp");
    LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
    LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
    LLVMValueRef vwp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 7, "vwp");
    LLVMValueRef deadp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 9, "deadp");
    LLVMValueRef findf = get_dict_find_fn(g);
    LLVMValueRef ci = zan_call2(g->builder, LLVMGlobalGetValueType(findf), findf,
        (LLVMValueRef[]){ draw, key, is_str }, 3, "dset.find");
    LLVMValueRef eq = zan_icmp(g->builder, LLVMIntSGE, ci,
        LLVMConstInt(i64, 0, 0), "eq");
    LLVMBuildCondBr(g->builder, eq, hit_bb, app_bb);
    LLVMPositionBuilderAtEnd(g->builder, hit_bb);
    LLVMBuildRet(g->builder, ci);
    LLVMPositionBuilderAtEnd(g->builder, app_bb);
    LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, capp, "cap");
    LLVMValueRef full = zan_icmp(g->builder, LLVMIntSGE, cnt, cap, "full");
    LLVMBuildCondBr(g->builder, full, grow_bb, put_bb);
    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
    /* 扩容前先回收删除空洞：若存在空洞则原地紧缩，无需 realloc 即可腾出空间 */
    emit_dict_compact_if_holes(g, draw);
    LLVMBasicBlockRef grow_chk_bb = LLVMGetInsertBlock(g->builder);
    LLVMValueRef cnt_after = LLVMBuildLoad2(g->builder, i64, cntp, "cnt.af");
    LLVMValueRef still_full = zan_icmp(g->builder, LLVMIntSGE, cnt_after, cap, "sfull");
    LLVMBuildCondBr(g->builder, still_full, realloc_bb, put_bb);
    LLVMPositionBuilderAtEnd(g->builder, realloc_bb);
    LLVMValueRef ks = LLVMBuildLoad2(g->builder, i8pp, kp, "ks");
    LLVMValueRef vs = LLVMBuildLoad2(g->builder, i64ptr, vp, "vs");
    LLVMValueRef value_words = LLVMBuildLoad2(g->builder, i64, vwp, "vw");
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
    /* holes 已为 0：释放旧尺寸 dead 数组，下次非尾删时按新 cap 惰性分配 */
    LLVMValueRef old_dead = LLVMBuildLoad2(g->builder, i8ptr, deadp, "odead");
    zan_call2(g->builder, free_ty, free_fn, &old_dead, 1, "");
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), deadp);
    LLVMBuildStore(g->builder, ncap, capp);
    LLVMBasicBlockRef realloc_end_bb = LLVMGetInsertBlock(g->builder);
    LLVMBuildBr(g->builder, put_bb);
    LLVMPositionBuilderAtEnd(g->builder, put_bb);
    LLVMValueRef put_cnt = LLVMBuildPhi(g->builder, i64, "put.cnt");
    LLVMAddIncoming(put_cnt, (LLVMValueRef[]){ cnt, cnt_after, cnt_after },
        (LLVMBasicBlockRef[]){ app_bb, grow_chk_bb, realloc_end_bb }, 3);
    LLVMValueRef ks2 = LLVMBuildLoad2(g->builder, i8pp, kp, "ks2");
    LLVMValueRef kslot2 = LLVMBuildGEP2(g->builder, i8ptr, ks2, &put_cnt, 1, "ksl2");
    LLVMBuildStore(g->builder, key, kslot2);
    LLVMBuildStore(g->builder,
        zan_add(g->builder, put_cnt, LLVMConstInt(i64, 1, 0), "ncnt"), cntp);
    LLVMBuildRet(g->builder, put_cnt);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}
/* 字典尾部追加新键（调用方已确认键不存在，避免 __zan_dict_set 二次哈希查找）。 */
LLVMValueRef get_dict_append_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_dict_append");
    if (fn) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
    LLVMTypeRef fnty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_dict_append", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    dict_add_fn_attr(g, fn, "nounwind");
    LLVMTypeRef realloc_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0);
    LLVMTypeRef free_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMValueRef free_fn = get_libc_fn(g, "free", free_ty);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "grow");
    LLVMBasicBlockRef realloc_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "realloc");
    LLVMBasicBlockRef put_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "put");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef draw = LLVMGetParam(fn, 0);
    LLVMValueRef key = LLVMGetParam(fn, 1);
    LLVMValueRef dp = LLVMBuildBitCast(g->builder, draw,
        LLVMPointerType(g->dict_struct_type, 0), "dp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
    LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
    zan_tbaa_tag_cnt(g, cnt);
    LLVMValueRef capp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 1, "capp");
    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, capp, "cap");
    zan_tbaa_tag_meta(g, cap);
    LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
    LLVMValueRef full = zan_icmp(g->builder, LLVMIntSGE, cnt, cap, "full");
    LLVMValueRef full_br = LLVMBuildCondBr(g->builder, full, grow_bb, put_bb);
    dict_set_branch_weights(g, full_br, 1, 2000);
    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
    /* 扩容前先回收删除空洞：高频率删插交替时原地紧缩复用容量，防止内存膨胀 */
    emit_dict_compact_if_holes(g, draw);
    LLVMBasicBlockRef grow_chk_bb = LLVMGetInsertBlock(g->builder);
    LLVMValueRef cnt_after = LLVMBuildLoad2(g->builder, i64, cntp, "cnt.af");
    LLVMValueRef still_full = zan_icmp(g->builder, LLVMIntSGE, cnt_after, cap, "sfull");
    LLVMBuildCondBr(g->builder, still_full, realloc_bb, put_bb);
    LLVMPositionBuilderAtEnd(g->builder, realloc_bb);
    LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
    LLVMValueRef vwp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 7, "vwp");
    LLVMValueRef deadp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 9, "deadp");
    LLVMValueRef ks = LLVMBuildLoad2(g->builder, i8pp, kp, "ks");
    LLVMValueRef vs = LLVMBuildLoad2(g->builder, i64ptr, vp, "vs");
    LLVMValueRef value_words = LLVMBuildLoad2(g->builder, i64, vwp, "vw");
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
    zan_tbaa_tag_meta(g, LLVMBuildStore(g->builder, LLVMBuildBitCast(g->builder, nks8, i8pp, "nks"), kp));
    LLVMValueRef vs8 = LLVMBuildBitCast(g->builder, vs, i8ptr, "vs8");
    LLVMValueRef nvs8 = zan_call2(g->builder, realloc_ty, g->fn_realloc,
        (LLVMValueRef[]){ vs8, value_bytes }, 2, "nvs8");
    zan_irgen_emit_oom_check(g, fn, nvs8);
    zan_tbaa_tag_meta(g, LLVMBuildStore(g->builder, LLVMBuildBitCast(g->builder, nvs8, i64ptr, "nvs"), vp));
    LLVMValueRef old_dead = LLVMBuildLoad2(g->builder, i8ptr, deadp, "odead");
    zan_call2(g->builder, free_ty, free_fn, &old_dead, 1, "");
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), deadp);
    zan_tbaa_tag_meta(g, LLVMBuildStore(g->builder, ncap, capp));
    LLVMBasicBlockRef realloc_end_bb = LLVMGetInsertBlock(g->builder);
    LLVMBuildBr(g->builder, put_bb);
    LLVMPositionBuilderAtEnd(g->builder, put_bb);
    LLVMValueRef put_cnt = LLVMBuildPhi(g->builder, i64, "put.cnt");
    LLVMAddIncoming(put_cnt, (LLVMValueRef[]){ cnt, cnt_after, cnt_after },
        (LLVMBasicBlockRef[]){ entry, grow_chk_bb, realloc_end_bb }, 3);
    LLVMValueRef ks2 = LLVMBuildLoad2(g->builder, i8pp, kp, "ks2");
    zan_tbaa_tag_meta(g, ks2);
    LLVMValueRef kslot2 = LLVMBuildInBoundsGEP2(g->builder, i8ptr, ks2, &put_cnt, 1, "ksl2");
    LLVMBuildStore(g->builder, key, kslot2);
    zan_tbaa_tag_cnt(g, LLVMBuildStore(g->builder,
        zan_add(g->builder, put_cnt, LLVMConstInt(i64, 1, 0), "ncnt"), cntp));
    LLVMBuildRet(g->builder, put_cnt);
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

