bool is_target_class_obj(zan_ast_node_t *obj, const char *name, size_t len, local_scope_t *locals) {
    if (!obj) return false;
    (void)len;
    if (obj->kind == AST_IDENTIFIER) {
        if (locals && local_find(locals, obj->ident.name)) return false;
        return istr_matches_class(obj->ident.name, name);
    } else if (obj->kind == AST_MEMBER_ACCESS) {
        return istr_matches_class(obj->member.name, name);
    }
    return false;
}

static LLVMTypeRef get_vector128_struct_type(zan_irgen_t *g) {
    LLVMTypeRef st = LLVMGetTypeByName2(g->ctx, "struct.Vector128");
    if (st) return st;
    for (int i = 0; i < g->struct_type_count; i++) {
        if (g->struct_types[i].sym && g->struct_types[i].sym->name.len == 9 &&
            memcmp(g->struct_types[i].sym->name.str, "Vector128", 9) == 0) {
            return g->struct_types[i].llvm_type;
        }
    }
    LLVMTypeRef fields[2] = { LLVMInt64TypeInContext(g->ctx), LLVMInt64TypeInContext(g->ctx) };
    return LLVMStructTypeInContext(g->ctx, fields, 2, 0);
}

static LLVMValueRef vec128_to_v16i8(zan_irgen_t *g, LLVMValueRef val) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef v16i8 = LLVMVectorType(LLVMInt8TypeInContext(g->ctx), 16);
    LLVMTypeRef v2i64 = LLVMVectorType(LLVMInt64TypeInContext(g->ctx), 2);
    LLVMTypeRef val_ty = LLVMTypeOf(val);

    if (val_ty == v16i8) return val;
    if (val_ty == v2i64) return LLVMBuildBitCast(g->builder, val, v16i8, "v16");

    if (LLVMGetTypeKind(val_ty) == LLVMPointerTypeKind) {
        LLVMValueRef ptr = LLVMBuildBitCast(g->builder, val, LLVMPointerType(v16i8, 0), "vptr");
        LLVMValueRef ld = LLVMBuildLoad2(g->builder, v16i8, ptr, "vld");
        LLVMSetAlignment(ld, 1);
        return ld;
    }
    if (LLVMGetTypeKind(val_ty) == LLVMStructTypeKind) {
        LLVMValueRef low = LLVMBuildExtractValue(g->builder, val, 0, "v.low");
        LLVMValueRef high = LLVMBuildExtractValue(g->builder, val, 1, "v.high");
        LLVMValueRef v2 = LLVMBuildInsertElement(g->builder, LLVMGetUndef(v2i64), low, LLVMConstInt(i32t, 0, 0), "v2_0");
        v2 = LLVMBuildInsertElement(g->builder, v2, high, LLVMConstInt(i32t, 1, 0), "v2_1");
        return LLVMBuildBitCast(g->builder, v2, v16i8, "v16");
    }
    return val;
}
LLVMValueRef v16i8_to_vec128(zan_irgen_t *g, LLVMValueRef v16) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef v2i64 = LLVMVectorType(LLVMInt64TypeInContext(g->ctx), 2);
    LLVMValueRef v2 = LLVMBuildBitCast(g->builder, v16, v2i64, "v2");
    LLVMValueRef low = LLVMBuildExtractElement(g->builder, v2, LLVMConstInt(i32t, 0, 0), "r.low");
    LLVMValueRef high = LLVMBuildExtractElement(g->builder, v2, LLVMConstInt(i32t, 1, 0), "r.high");

    LLVMTypeRef st = get_vector128_struct_type(g);
    LLVMValueRef res = LLVMBuildInsertValue(g->builder, LLVMGetUndef(st), low, 0, "s0");
    return LLVMBuildInsertValue(g->builder, res, high, 1, "s1");
}

static LLVMValueRef vec128_to_v2i64(zan_irgen_t *g, LLVMValueRef val) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef v2i64 = LLVMVectorType(LLVMInt64TypeInContext(g->ctx), 2);
    LLVMTypeRef val_ty = LLVMTypeOf(val);

    if (val_ty == v2i64) return val;
    if (LLVMGetTypeKind(val_ty) == LLVMVectorTypeKind) {
        return LLVMBuildBitCast(g->builder, val, v2i64, "v2");
    }
    if (LLVMGetTypeKind(val_ty) == LLVMPointerTypeKind) {
        LLVMValueRef ptr = LLVMBuildBitCast(g->builder, val, LLVMPointerType(v2i64, 0), "vptr2");
        LLVMValueRef ld = LLVMBuildLoad2(g->builder, v2i64, ptr, "vld2");
        LLVMSetAlignment(ld, 1);
        return ld;
    }
    if (LLVMGetTypeKind(val_ty) == LLVMStructTypeKind) {
        LLVMValueRef low = LLVMBuildExtractValue(g->builder, val, 0, "v.low");
        LLVMValueRef high = LLVMBuildExtractValue(g->builder, val, 1, "v.high");
        LLVMValueRef v2 = LLVMBuildInsertElement(g->builder, LLVMGetUndef(v2i64), low, LLVMConstInt(i32t, 0, 0), "v2_0");
        return LLVMBuildInsertElement(g->builder, v2, high, LLVMConstInt(i32t, 1, 0), "v2_1");
    }
    return val;
}

static LLVMValueRef v2i64_to_vec128(zan_irgen_t *g, LLVMValueRef v2) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef low = LLVMBuildExtractElement(g->builder, v2, LLVMConstInt(i32t, 0, 0), "r.low");
    LLVMValueRef high = LLVMBuildExtractElement(g->builder, v2, LLVMConstInt(i32t, 1, 0), "r.high");

    LLVMTypeRef st = get_vector128_struct_type(g);
    LLVMValueRef res = LLVMBuildInsertValue(g->builder, LLVMGetUndef(st), low, 0, "s0");
    return LLVMBuildInsertValue(g->builder, res, high, 1, "s1");
}

static LLVMValueRef vec128_to_v4f32(zan_irgen_t *g, LLVMValueRef val) {
    LLVMTypeRef v4f32 = LLVMVectorType(LLVMFloatTypeInContext(g->ctx), 4);
    LLVMValueRef v16 = vec128_to_v16i8(g, val);
    return LLVMBuildBitCast(g->builder, v16, v4f32, "v4f");
}

static LLVMValueRef v4f32_to_vec128(zan_irgen_t *g, LLVMValueRef v4) {
    LLVMTypeRef v16i8 = LLVMVectorType(LLVMInt8TypeInContext(g->ctx), 16);
    LLVMValueRef v16 = LLVMBuildBitCast(g->builder, v4, v16i8, "v16");
    return v16i8_to_vec128(g, v16);
}

static int get_simd_array_element_size(zan_irgen_t *g, zan_ast_node_t *arg, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, arg, locals);
    if (!t || t->kind != TYPE_ARRAY || !t->element_type) return 1;
    switch (t->element_type->kind) {
    case TYPE_FLOAT:
    case TYPE_INT:
    case TYPE_UINT:
        return 4;
    case TYPE_LONG:
    case TYPE_ULONG:
    case TYPE_DOUBLE:
        return 8;
    case TYPE_SHORT:
    case TYPE_USHORT:
    case TYPE_CHAR:
        return 2;
    default:
        return 1;
    }
}

/* 判断目标平台是否为 x86/x86-64 架构（SSE/AVX 向量内建指令门控） */
bool emit_target_is_x86(const zan_irgen_t *g)
{
    if (g->target_triple[0] == '\0') {
#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__)
        return false;
#else
        return true;
#endif
    }
    return strstr(g->target_triple, "x86") != NULL ||
           (g->target_triple[0] == 'i' && strstr(g->target_triple, "86") != NULL);
}

/* Match the Android baseline selected by zan_bind_target_layout. Architecture
   alone does not permit AVX2; generic vector IR can split into SSE operations. */
static bool emit_target_has_avx2(const zan_irgen_t *g)
{
    return emit_target_is_x86(g) && strstr(g->target_triple, "android") == NULL;
}

/* 跨平台 pmovmskb 实现：提取每个字节符号位组装为整型掩码。
 * 全程在 <lanes/8 x i64> 泳道向量上做 SWAR，不产生任何 i1 向量——
 * <32 x i1> 这类非法宽度会在通用 CPU（无 AVX2）的合法化阶段触发
 * "Do not know how to split this operator's operand"（A439）。 */
LLVMValueRef emit_pmovmskb_portable(zan_irgen_t *g, LLVMValueRef v,
                                           unsigned lanes)
{
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    unsigned wl = lanes / 8;
    LLVMTypeRef wv = LLVMVectorType(i64t, wl);
    /* 只留各字节符号位：0x80 per 字节。 */
    LLVMValueRef hi_elems[4];
    for (unsigned k = 0; k < wl; k++) {
        hi_elems[k] = LLVMConstInt(i64t, 0x8080808080808080ULL, 0);
    }
    LLVMValueRef hi = LLVMConstVector(hi_elems, wl);
    LLVMValueRef wide = LLVMBuildBitCast(g->builder, v, wv, "vpmask.wide");
    LLVMValueRef m = LLVMBuildAnd(g->builder, wide, hi, "vpmask.hi");
    /* 每泳道：字节 k 的符号位右移 7(k+1) 恰好落到位 k（其余字节的
     * 落点全在位 7 之外，末尾 0xFF 掩掉），8 次移位取或得 8 位掩码。 */
    LLVMValueRef gathered = LLVMConstNull(wv);
    for (unsigned k = 0; k < 8; k++) {
        LLVMValueRef sh_elems[4];
        for (unsigned j = 0; j < wl; j++) {
            sh_elems[j] = LLVMConstInt(i64t, 7 * (k + 1), 0);
        }
        LLVMValueRef sh = LLVMConstVector(sh_elems, wl);
        LLVMValueRef t = LLVMBuildLShr(g->builder, m, sh, "vpmask.sh");
        if (k == 0) {
            gathered = t;
        } else {
            gathered = LLVMBuildOr(g->builder, gathered, t, "vpmask.or");
        }
    }
    LLVMValueRef ff_elems[4];
    for (unsigned j = 0; j < wl; j++) {
        ff_elems[j] = LLVMConstInt(i64t, 0xFF, 0);
    }
    gathered = LLVMBuildAnd(g->builder, gathered,
                            LLVMConstVector(ff_elems, wl), "vpmask.ff");
    /* 泳道收拢为 i32：位 [8k, 8k+8) ← 泳道 k 的低 8 位（纯标量）。 */
    LLVMValueRef acc = LLVMConstInt(i32t, 0, 0);
    for (unsigned k = 0; k < wl; k++) {
        LLVMValueRef lane = LLVMBuildExtractElement(g->builder, gathered,
            LLVMConstInt(i32t, k, 0), "vpmask.lane");
        LLVMValueRef b = LLVMBuildAnd(g->builder, lane,
            LLVMConstInt(i64t, 0xFF, 0), "vpmask.b");
        LLVMValueRef ext = LLVMBuildTrunc(g->builder, b, i32t, "vpmask.b32");
        if (k > 0) {
            ext = LLVMBuildShl(g->builder, ext,
                LLVMConstInt(i32t, 8 * k, 0), "vpmask.bsh");
        }
        acc = k == 0 ? ext : LLVMBuildOr(g->builder, acc, ext, "vpmask.acc");
    }
    return acc;
}

/* 跨平台 pshufb 实现：基于掩码索引逐字节置换，最高位为 1 时清零 */
static LLVMValueRef emit_pshufb_portable(zan_irgen_t *g, LLVMValueRef v,
                                         LLVMValueRef m, unsigned lanes)
{
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef vt = LLVMVectorType(i8t, lanes);
    LLVMValueRef res = LLVMGetUndef(vt);
    LLVMValueRef c128 = LLVMConstInt(i8t, 0x80, 0);
    LLVMValueRef c15 = LLVMConstInt(i8t, 0x0f, 0);
    LLVMValueRef c0 = LLVMConstInt(i8t, 0, 0);
    for (unsigned i = 0; i < lanes; i++) {
        LLVMValueRef lane = LLVMConstInt(i32t, i, 0);
        LLVMValueRef mi = LLVMBuildExtractElement(g->builder, m, lane, "pshufb_mi");
        LLVMValueRef hi = LLVMBuildAnd(g->builder, mi, c128, "pshufb_hi");
        LLVMValueRef pick = LLVMBuildICmp(g->builder, LLVMIntEQ, hi, c0, "pshufb_pick");
        LLVMValueRef idx = LLVMBuildZExt(g->builder,
            LLVMBuildAnd(g->builder, mi, c15, "pshufb_idx"), i32t, "pshufb_idxw");
        LLVMValueRef gathered = LLVMBuildExtractElement(g->builder, v, idx, "pshufb_got");
        LLVMValueRef laneval = LLVMBuildSelect(g->builder, pick, gathered, c0, "pshufb_lane");
        res = LLVMBuildInsertElement(g->builder, res, laneval, lane, "pshufb_ins");
    }
    return res;
}

/* 跨平台 pavg.b 实现：逐字节四舍五入均值计算 */
static LLVMValueRef emit_pavg_portable(zan_irgen_t *g, LLVMValueRef a,
                                       LLVMValueRef b)
{
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    unsigned lanes = LLVMGetVectorSize(LLVMTypeOf(a));
    LLVMValueRef one = LLVMConstInt(i8t, 1, 0);
    LLVMValueRef ones[64];
    for (unsigned i = 0; i < lanes; i++)
        ones[i] = one;
    LLVMValueRef ones_v = LLVMConstVector(ones, lanes);
    LLVMValueRef and_ab = LLVMBuildAnd(g->builder, a, b, "pavg_and");
    LLVMValueRef xor_ab = LLVMBuildXor(g->builder, a, b, "pavg_xor");
    LLVMValueRef half = LLVMBuildLShr(g->builder, xor_ab, ones_v, "pavg_half");
    LLVMValueRef odd = LLVMBuildAnd(g->builder, xor_ab, ones_v, "pavg_odd");
    LLVMValueRef sum = LLVMBuildAdd(g->builder, and_ab, half, "pavg_sum");
    return LLVMBuildAdd(g->builder, sum, odd, "pavg_r");
}

/* 字节等值掩码的 SWAR 合成（A439）：全程在 <lanes/8 x i64> 泳道算子上
 * 完成，返回 <lanes x i8>（等值字节 0xFF，不等 0x00），不产生任何
 * i1 向量——<32 x i1> 这类非法宽度在通用 CPU（无 AVX2）的合法化阶段
 * 会触发 "Do not know how to split this operator's operand"。
 * 每泳道：x = a^b（等值字节为 0）；经典 haszero 掩码标出等值字节
 * （0x80 per byte）；(t >> 7) * 0xFF 展开成字节掩码。 */
LLVMValueRef emit_byte_eq_mask_i64(zan_irgen_t *g, LLVMValueRef a,
                                          LLVMValueRef b, unsigned lanes)
{
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    unsigned wl = lanes / 8;
    LLVMTypeRef wv = LLVMVectorType(i64t, wl);
    LLVMValueRef ones_elems[4];
    LLVMValueRef hi_elems[4];
    for (unsigned j = 0; j < wl; j++) {
        ones_elems[j] = LLVMConstInt(i64t, 0x0101010101010101ULL, 0);
        hi_elems[j] = LLVMConstInt(i64t, 0x8080808080808080ULL, 0);
    }
    LLVMValueRef ones = LLVMConstVector(ones_elems, wl);
    LLVMValueRef his = LLVMConstVector(hi_elems, wl);
    LLVMValueRef aw = LLVMBuildBitCast(g->builder, a, wv, "veq.a");
    LLVMValueRef bw = LLVMBuildBitCast(g->builder, b, wv, "veq.b");
    LLVMValueRef x = LLVMBuildXor(g->builder, aw, bw, "veq.xor");
    LLVMValueRef t = LLVMBuildSub(g->builder, x, ones, "veq.sub");
    t = LLVMBuildAnd(g->builder, t,
                     LLVMBuildNot(g->builder, x, "veq.nx"), "veq.t1");
    t = LLVMBuildAnd(g->builder, t, his, "veq.t2");
    LLVMValueRef sh_elems[4];
    LLVMValueRef ff_elems[4];
    for (unsigned j = 0; j < wl; j++) {
        sh_elems[j] = LLVMConstInt(i64t, 7, 0);
        ff_elems[j] = LLVMConstInt(i64t, 0xFF, 0);
    }
    t = LLVMBuildLShr(g->builder, t, LLVMConstVector(sh_elems, wl), "veq.shr");
    t = LLVMBuildMul(g->builder, t, LLVMConstVector(ff_elems, wl), "veq.m");
    return LLVMBuildBitCast(g->builder, t,
                            LLVMVectorType(LLVMInt8TypeInContext(g->ctx), lanes),
                            "veq.mask");
}

/* 字节带符号大于掩码的 SWAR 合成（A439）：返回 <lanes x i8>（a > b 的
 * 字节处 0xFF）。先异或 0x80 把带符号序偏成无符号序，再做无符号逐字节
 * 大于：位 7 独大者直接胜（t1），位 7 相同者比低 7 位（ge7 & ~e7，e7
 * 为低 7 位相等标志）。全程 <lanes/8 x i64> 泳道算子，不产生任何
 * i1 向量（同 emit_byte_eq_mask_i64 的 A439 缘由）。 */
static LLVMValueRef emit_byte_sgt_mask_i64(zan_irgen_t *g, LLVMValueRef a,
                                           LLVMValueRef b, unsigned lanes)
{
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    unsigned wl = lanes / 8;
    LLVMTypeRef wv = LLVMVectorType(i64t, wl);
    LLVMValueRef ones_elems[4], hi_elems[4], l7_elems[4], sh_elems[4], ff_elems[4];
    for (unsigned j = 0; j < wl; j++) {
        ones_elems[j] = LLVMConstInt(i64t, 0x0101010101010101ULL, 0);
        hi_elems[j] = LLVMConstInt(i64t, 0x8080808080808080ULL, 0);
        l7_elems[j] = LLVMConstInt(i64t, 0x7F7F7F7F7F7F7F7FULL, 0);
        sh_elems[j] = LLVMConstInt(i64t, 7, 0);
        ff_elems[j] = LLVMConstInt(i64t, 0xFF, 0);
    }
    LLVMValueRef ones = LLVMConstVector(ones_elems, wl);
    LLVMValueRef his = LLVMConstVector(hi_elems, wl);
    LLVMValueRef l7 = LLVMConstVector(l7_elems, wl);
    LLVMValueRef ffs = LLVMConstVector(ff_elems, wl);
    LLVMValueRef sh7 = LLVMConstVector(sh_elems, wl);
    LLVMValueRef aw = LLVMBuildBitCast(g->builder, a, wv, "vsgt.a");
    LLVMValueRef bw = LLVMBuildBitCast(g->builder, b, wv, "vsgt.b");
    /* 偏置成无符号序：翻转符号位。 */
    LLVMValueRef au = LLVMBuildXor(g->builder, aw, his, "vsgt.au");
    LLVMValueRef bu = LLVMBuildXor(g->builder, bw, his, "vsgt.bu");
    /* 位 7 独大：直接胜。 */
    LLVMValueRef t1 = LLVMBuildAnd(g->builder, au,
        LLVMBuildAnd(g->builder,
            LLVMBuildNot(g->builder, bu, "vsgt.nbu"), his, "vsgt.t1a"), "vsgt.t1");
    /* 低 7 位相等标志：haszero((a^b) & 0x7F..)。 */
    LLVMValueRef x7 = LLVMBuildAnd(g->builder,
        LLVMBuildXor(g->builder, aw, bw, "vsgt.x"), l7, "vsgt.x7");
    LLVMValueRef e7 = LLVMBuildAnd(g->builder,
        LLVMBuildAnd(g->builder,
            LLVMBuildSub(g->builder, x7, ones, "vsgt.sub"),
            LLVMBuildNot(g->builder, x7, "vsgt.nx7"), "vsgt.e7a"),
        his, "vsgt.e7");
    /* 低 7 位 ge：((au|H) - (bu&L7)) 的每字节高位。 */
    LLVMValueRef ge7 = LLVMBuildAnd(g->builder,
        LLVMBuildSub(g->builder,
            LLVMBuildOr(g->builder, au, his, "vsgt.auh"),
            LLVMBuildAnd(g->builder, bu, l7, "vsgt.bul7"), "vsgt.gd"),
        his, "vsgt.ge7");
    /* 位 7 相同才轮到低 7 位。 */
    LLVMValueRef eq7 = LLVMBuildAnd(g->builder,
        LLVMBuildNot(g->builder,
            LLVMBuildXor(g->builder, au, bu, "vsgt.abx"), "vsgt.nabx"),
        his, "vsgt.eq7");
    LLVMValueRef gt = LLVMBuildOr(g->builder, t1,
        LLVMBuildAnd(g->builder,
            LLVMBuildAnd(g->builder, ge7,
                LLVMBuildNot(g->builder, e7, "vsgt.ne7"), "vsgt.g2"), eq7, "vsgt.g3"),
        "vsgt.gt");
    /* (t>>7)*0xFF：0x80 标志展开成 0xFF 字节掩码。 */
    gt = LLVMBuildLShr(g->builder, gt, sh7, "vsgt.shr");
    gt = LLVMBuildMul(g->builder, gt, ffs, "vsgt.m");
    return LLVMBuildBitCast(g->builder, gt,
                            LLVMVectorType(LLVMInt8TypeInContext(g->ctx), lanes),
                            "vsgt.mask");
}
bool emit_vector128_call(zan_irgen_t *g, zan_ast_node_t *expr,
                                local_scope_t *locals, LLVMValueRef *out) {
    if (expr->kind != AST_CALL) return false;
    zan_ast_node_t *callee = expr->call.callee;
    if (callee->kind != AST_MEMBER_ACCESS) return false;
    if (!is_target_class_obj(callee->member.object, "Vector128", 9, locals))
        return false;

    zan_istr_t method = callee->member.name;
    int argc = expr->call.args.count;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef v16i8 = LLVMVectorType(LLVMInt8TypeInContext(g->ctx), 16);

    /* Vector128 零向量 / 全置位向量内建属性发射 */
    if (method.len == 4 && memcmp(method.str, "Zero", 4) == 0 && argc == 0) {
        *out = v16i8_to_vec128(g, LLVMConstNull(v16i8));
        return true;
    }
    if (method.len == 10 && memcmp(method.str, "AllBitsSet", 10) == 0 && argc == 0) {
        *out = v16i8_to_vec128(g, LLVMConstAllOnes(v16i8));
        return true;
    }

    /* Vector128 内存向量加载内建指令发射 */
    if (method.len == 4 && memcmp(method.str, "Load", 4) == 0 && (argc == 1 || argc == 2)) {
        LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
        LLVMValueRef addr_val = emit_expr(g, expr->call.args.items[0], locals);
        LLVMValueRef base;
        if (LLVMGetTypeKind(LLVMTypeOf(addr_val)) == LLVMPointerTypeKind) {
            base = LLVMBuildBitCast(g->builder, addr_val, LLVMPointerType(i8, 0), "vload_base");
        } else {
            base = LLVMBuildIntToPtr(g->builder, addr_val, LLVMPointerType(i8, 0), "vload_base");
        }
        if (argc == 2) {
            LLVMValueRef off_val = coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals), i64t);
            int esz = get_simd_array_element_size(g, expr->call.args.items[0], locals);
            if (esz > 1) {
                off_val = LLVMBuildMul(g->builder, off_val, LLVMConstInt(i64t, esz, 0), "off_bytes");
            }
            base = LLVMBuildGEP2(g->builder, i8, base, &off_val, 1, "vload_gep");
        }
        LLVMValueRef ptr = LLVMBuildBitCast(g->builder, base, LLVMPointerType(v16i8, 0), "vload_ptr");
        LLVMValueRef ld = LLVMBuildLoad2(g->builder, v16i8, ptr, "vload");
        LLVMSetAlignment(ld, 1);
        *out = v16i8_to_vec128(g, ld);
        return true;
    }

    /* Vector128 内存向量存储内建指令发射 */
    if (method.len == 5 && memcmp(method.str, "Store", 5) == 0 && (argc == 2 || argc == 3)) {
        LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
        LLVMValueRef addr_val = emit_expr(g, expr->call.args.items[0], locals);
        LLVMValueRef base;
        if (LLVMGetTypeKind(LLVMTypeOf(addr_val)) == LLVMPointerTypeKind) {
            base = LLVMBuildBitCast(g->builder, addr_val, LLVMPointerType(i8, 0), "vstore_base");
        } else {
            base = LLVMBuildIntToPtr(g->builder, addr_val, LLVMPointerType(i8, 0), "vstore_base");
        }
        LLVMValueRef src_val;
        if (argc == 3) {
            LLVMValueRef off_val = coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals), i64t);
            int esz = get_simd_array_element_size(g, expr->call.args.items[0], locals);
            if (esz > 1) {
                off_val = LLVMBuildMul(g->builder, off_val, LLVMConstInt(i64t, esz, 0), "off_bytes");
            }
            base = LLVMBuildGEP2(g->builder, i8, base, &off_val, 1, "vstore_gep");
            src_val = emit_expr(g, expr->call.args.items[2], locals);
        } else {
            src_val = emit_expr(g, expr->call.args.items[1], locals);
        }
        LLVMValueRef ptr = LLVMBuildBitCast(g->builder, base, LLVMPointerType(v16i8, 0), "vstore_ptr");
        LLVMValueRef v16 = vec128_to_v16i8(g, src_val);
        LLVMValueRef st = LLVMBuildStore(g->builder, v16, ptr);
        LLVMSetAlignment(st, 1);
        *out = NULL;
        return true;
    }

    /* Vector128.Create(...) */
    if (method.len == 6 && memcmp(method.str, "Create", 6) == 0) {
        if (argc == 1) {
            zan_ast_node_t *arg0 = expr->call.args.items[0];
            LLVMValueRef v = emit_expr(g, arg0, locals);
            zan_type_t *arg_type = infer_expr_type(g, arg0, locals);
            if (arg_type && (arg_type->kind == TYPE_FLOAT)) {
                LLVMTypeRef f32t = LLVMFloatTypeInContext(g->ctx);
                LLVMTypeRef v4f32 = LLVMVectorType(f32t, 4);
                LLVMValueRef ins = LLVMBuildInsertElement(g->builder, LLVMGetUndef(v4f32), v, LLVMConstInt(i32t, 0, 0), "f0");
                LLVMValueRef mask_elems[4] = { LLVMConstInt(i32t, 0, 0), LLVMConstInt(i32t, 0, 0), LLVMConstInt(i32t, 0, 0), LLVMConstInt(i32t, 0, 0) };
                LLVMValueRef mask = LLVMConstVector(mask_elems, 4);
                LLVMValueRef bcast = LLVMBuildShuffleVector(g->builder, ins, LLVMGetUndef(v4f32), mask, "bcastf");
                *out = v4f32_to_vec128(g, bcast);
                return true;
            } else if (arg_type && (arg_type->kind == TYPE_LONG || arg_type->kind == TYPE_ULONG)) {
                LLVMTypeRef v2i64 = LLVMVectorType(i64t, 2);
                LLVMValueRef val = coerce_int_to(g, v, i64t);
                LLVMValueRef ins = LLVMBuildInsertElement(g->builder, LLVMGetUndef(v2i64), val, LLVMConstInt(i32t, 0, 0), "l0");
                LLVMValueRef mask_elems[2] = { LLVMConstInt(i32t, 0, 0), LLVMConstInt(i32t, 0, 0) };
                LLVMValueRef mask = LLVMConstVector(mask_elems, 2);
                LLVMValueRef bcast = LLVMBuildShuffleVector(g->builder, ins, LLVMGetUndef(v2i64), mask, "bcast64");
                *out = v16i8_to_vec128(g, LLVMBuildBitCast(g->builder, bcast, v16i8, "v16"));
                return true;
            } else if (arg_type && (arg_type->kind == TYPE_BYTE || arg_type->kind == TYPE_SBYTE)) {
                LLVMValueRef b = coerce_int_to(g, v, LLVMInt8TypeInContext(g->ctx));
                LLVMValueRef ins = LLVMBuildInsertElement(g->builder, LLVMGetUndef(v16i8), b, LLVMConstInt(i32t, 0, 0), "b0");
                LLVMValueRef mask_elems[16];
                for (int i = 0; i < 16; i++) mask_elems[i] = LLVMConstInt(i32t, 0, 0);
                LLVMValueRef mask = LLVMConstVector(mask_elems, 16);
                LLVMValueRef bcast = LLVMBuildShuffleVector(g->builder, ins, LLVMGetUndef(v16i8), mask, "bcast8");
                *out = v16i8_to_vec128(g, bcast);
                return true;
            } else {
                LLVMTypeRef v4i32 = LLVMVectorType(i32t, 4);
                LLVMValueRef val = coerce_int_to(g, v, i32t);
                LLVMValueRef ins = LLVMBuildInsertElement(g->builder, LLVMGetUndef(v4i32), val, LLVMConstInt(i32t, 0, 0), "i0");
                LLVMValueRef mask_elems[4];
                for (int i = 0; i < 4; i++) mask_elems[i] = LLVMConstInt(i32t, 0, 0);
                LLVMValueRef mask = LLVMConstVector(mask_elems, 4);
                LLVMValueRef bcast = LLVMBuildShuffleVector(g->builder, ins, LLVMGetUndef(v4i32), mask, "bcast32");
                *out = v16i8_to_vec128(g, LLVMBuildBitCast(g->builder, bcast, v16i8, "v16"));
                return true;
            }
        } else if (argc == 2) {
            LLVMValueRef low = coerce_int_to(g, emit_expr(g, expr->call.args.items[0], locals), i64t);
            LLVMValueRef high = coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals), i64t);
            LLVMTypeRef st = get_vector128_struct_type(g);
            LLVMValueRef res = LLVMBuildInsertValue(g->builder, LLVMGetUndef(st), low, 0, "s0");
            *out = LLVMBuildInsertValue(g->builder, res, high, 1, "s1");
            return true;
        } else if (argc == 4) {
            LLVMTypeRef f32t = LLVMFloatTypeInContext(g->ctx);
            LLVMTypeRef v4f32 = LLVMVectorType(f32t, 4);
            LLVMValueRef vec = LLVMGetUndef(v4f32);
            for (int i = 0; i < 4; i++) {
                LLVMValueRef elem = emit_expr(g, expr->call.args.items[i], locals);
                if (LLVMGetTypeKind(LLVMTypeOf(elem)) == LLVMIntegerTypeKind) {
                    elem = LLVMBuildSIToFP(g->builder, elem, f32t, "s2f");
                } else if (LLVMTypeOf(elem) != f32t) {
                    elem = LLVMBuildFPCast(g->builder, elem, f32t, "fpcast");
                }
                vec = LLVMBuildInsertElement(g->builder, vec, elem, LLVMConstInt(i32t, i, 0), "v4f_ins");
            }
            *out = v4f32_to_vec128(g, vec);
            return true;
        }
    }

    /* Bitwise: And, Or, Xor, AndNot */
    if (argc == 2) {
        if (method.len == 3 && memcmp(method.str, "And", 3) == 0) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
            *out = v16i8_to_vec128(g, LLVMBuildAnd(g->builder, a16, b16, "vand"));
            return true;
        }
        if (method.len == 2 && memcmp(method.str, "Or", 2) == 0) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
            *out = v16i8_to_vec128(g, LLVMBuildOr(g->builder, a16, b16, "vor"));
            return true;
        }
        if (method.len == 3 && memcmp(method.str, "Xor", 3) == 0) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
            *out = v16i8_to_vec128(g, LLVMBuildXor(g->builder, a16, b16, "vxor"));
            return true;
        }
        if (method.len == 6 && memcmp(method.str, "AndNot", 6) == 0) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
            LLVMValueRef not_a = LLVMBuildNot(g->builder, a16, "vnot");
            *out = v16i8_to_vec128(g, LLVMBuildAnd(g->builder, not_a, b16, "vandn"));
            return true;
        }
        if ((method.len == 6 && memcmp(method.str, "Equals", 6) == 0) ||
            (method.len == 12 && memcmp(method.str, "CompareEqual", 12) == 0)) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
            /* SWAR 等值掩码：不产生 <16 x i1>（A439）。 */
            *out = v16i8_to_vec128(g, emit_byte_eq_mask_i64(g, a16, b16, 16));
            return true;
        }
        if (method.len == 7 && memcmp(method.str, "Shuffle", 7) == 0) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef m16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
            if (emit_target_is_x86(g)) {
                LLVMTypeRef fty = LLVMFunctionType(v16i8, (LLVMTypeRef[]){ v16i8, v16i8 }, 2, 0);
                LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.x86.ssse3.pshuf.b.128");
                if (!fn) fn = LLVMAddFunction(g->mod, "llvm.x86.ssse3.pshuf.b.128", fty);
                LLVMValueRef args[2] = { a16, m16 };
                *out = v16i8_to_vec128(g, zan_call2(g->builder, fty, fn, args, 2, "pshufb"));
            } else {
                *out = v16i8_to_vec128(g, emit_pshufb_portable(g, a16, m16, 16));
            }
            return true;
        }
        if (method.len == 3 && memcmp(method.str, "Add", 3) == 0) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
            *out = v16i8_to_vec128(g, LLVMBuildAdd(g->builder, a16, b16, "vadd"));
            return true;
        }
        if (method.len == 8 && memcmp(method.str, "Subtract", 8) == 0) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
            *out = v16i8_to_vec128(g, LLVMBuildSub(g->builder, a16, b16, "vsub"));
            return true;
        }
        if (method.len == 11 && memcmp(method.str, "AddSaturate", 11) == 0) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
            LLVMTypeRef fty = LLVMFunctionType(v16i8, (LLVMTypeRef[]){ v16i8, v16i8 }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.uadd.sat.v16i8");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.uadd.sat.v16i8", fty);
            LLVMValueRef args[2] = { a16, b16 };
            *out = v16i8_to_vec128(g, zan_call2(g->builder, fty, fn, args, 2, "paddusb"));
            return true;
        }
        if (method.len == 16 && memcmp(method.str, "SubtractSaturate", 16) == 0) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
            LLVMTypeRef fty = LLVMFunctionType(v16i8, (LLVMTypeRef[]){ v16i8, v16i8 }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.usub.sat.v16i8");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.usub.sat.v16i8", fty);
            LLVMValueRef args[2] = { a16, b16 };
            *out = v16i8_to_vec128(g, zan_call2(g->builder, fty, fn, args, 2, "psubusb"));
            return true;
        }
        if (method.len == 3 && memcmp(method.str, "Min", 3) == 0) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
            LLVMTypeRef fty = LLVMFunctionType(v16i8, (LLVMTypeRef[]){ v16i8, v16i8 }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.umin.v16i8");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.umin.v16i8", fty);
            LLVMValueRef args[2] = { a16, b16 };
            *out = v16i8_to_vec128(g, zan_call2(g->builder, fty, fn, args, 2, "pminub"));
            return true;
        }
        if (method.len == 3 && memcmp(method.str, "Max", 3) == 0) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
            LLVMTypeRef fty = LLVMFunctionType(v16i8, (LLVMTypeRef[]){ v16i8, v16i8 }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.umax.v16i8");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.umax.v16i8", fty);
            LLVMValueRef args[2] = { a16, b16 };
            *out = v16i8_to_vec128(g, zan_call2(g->builder, fty, fn, args, 2, "pmaxub"));
            return true;
        }
        if (method.len == 7 && memcmp(method.str, "Average", 7) == 0) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
            if (emit_target_is_x86(g)) {
                LLVMTypeRef fty = LLVMFunctionType(v16i8, (LLVMTypeRef[]){ v16i8, v16i8 }, 2, 0);
                LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.x86.sse2.pavg.b");
                if (!fn) fn = LLVMAddFunction(g->mod, "llvm.x86.sse2.pavg.b", fty);
                LLVMValueRef args[2] = { a16, b16 };
                *out = v16i8_to_vec128(g, zan_call2(g->builder, fty, fn, args, 2, "pavgb"));
            } else {
                *out = v16i8_to_vec128(g, emit_pavg_portable(g, a16, b16));
            }
            return true;
        }
        if (method.len == 9 && memcmp(method.str, "UnpackLow", 9) == 0) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
            LLVMValueRef mask_elems[16];
            for (int i = 0; i < 8; i++) {
                mask_elems[i * 2] = LLVMConstInt(i32t, (unsigned long long)i, 0);
                mask_elems[i * 2 + 1] = LLVMConstInt(i32t, (unsigned long long)(i + 16), 0);
            }
            LLVMValueRef mask = LLVMConstVector(mask_elems, 16);
            *out = v16i8_to_vec128(g, LLVMBuildShuffleVector(g->builder, a16, b16, mask, "punpcklbw"));
            return true;
        }
        if (method.len == 10 && memcmp(method.str, "UnpackHigh", 10) == 0) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
            LLVMValueRef mask_elems[16];
            for (int i = 0; i < 8; i++) {
                mask_elems[i * 2] = LLVMConstInt(i32t, (unsigned long long)(i + 8), 0);
                mask_elems[i * 2 + 1] = LLVMConstInt(i32t, (unsigned long long)(i + 24), 0);
            }
            LLVMValueRef mask = LLVMConstVector(mask_elems, 16);
            *out = v16i8_to_vec128(g, LLVMBuildShuffleVector(g->builder, a16, b16, mask, "punpckhbw"));
            return true;
        }
        if (method.len == 11 && memcmp(method.str, "GreaterThan", 11) == 0) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
            /* SWAR 带符号大于：不产生 <16 x i1>（A439）。 */
            *out = v16i8_to_vec128(g, emit_byte_sgt_mask_i64(g, a16, b16, 16));
            return true;
        }
        if (method.len == 8 && memcmp(method.str, "LessThan", 8) == 0) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
            /* SWAR 带符号小于 = 交换实参的大于。 */
            *out = v16i8_to_vec128(g, emit_byte_sgt_mask_i64(g, b16, a16, 16));
            return true;
        }
        if (method.len == 8 && memcmp(method.str, "Multiply", 8) == 0) {
            LLVMValueRef a = vec128_to_v4f32(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b = vec128_to_v4f32(g, emit_expr(g, expr->call.args.items[1], locals));
            *out = v4f32_to_vec128(g, LLVMBuildFMul(g->builder, a, b, "vmulf"));
            return true;
        }
        if (method.len == 8 && memcmp(method.str, "AddFloat", 8) == 0) {
            LLVMValueRef a = vec128_to_v4f32(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b = vec128_to_v4f32(g, emit_expr(g, expr->call.args.items[1], locals));
            *out = v4f32_to_vec128(g, LLVMBuildFAdd(g->builder, a, b, "vfadd"));
            return true;
        }
        if (method.len == 13 && memcmp(method.str, "SubtractFloat", 13) == 0) {
            LLVMValueRef a = vec128_to_v4f32(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b = vec128_to_v4f32(g, emit_expr(g, expr->call.args.items[1], locals));
            *out = v4f32_to_vec128(g, LLVMBuildFSub(g->builder, a, b, "vfsub"));
            return true;
        }
    }

    if (argc == 1 && method.len == 4 && memcmp(method.str, "Sqrt", 4) == 0) {
        LLVMTypeRef f32t = LLVMFloatTypeInContext(g->ctx);
        LLVMTypeRef v4f32 = LLVMVectorType(f32t, 4);
        LLVMValueRef a = vec128_to_v4f32(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMTypeRef fty = LLVMFunctionType(v4f32, (LLVMTypeRef[]){ v4f32 }, 1, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.sqrt.v4f32");
        if (!fn) fn = LLVMAddFunction(g->mod, "llvm.sqrt.v4f32", fty);
        *out = v4f32_to_vec128(g, zan_call2(g->builder, fty, fn, &a, 1, "vsqrt"));
        return true;
    }
    if (argc == 1 && method.len == 14 && memcmp(method.str, "ReciprocalSqrt", 14) == 0) {
        LLVMTypeRef f32t = LLVMFloatTypeInContext(g->ctx);
        LLVMTypeRef v4f32 = LLVMVectorType(f32t, 4);
        LLVMValueRef a = vec128_to_v4f32(g, emit_expr(g, expr->call.args.items[0], locals));
        if (emit_target_is_x86(g)) {
            /* x86 RSQRTPS 近似平方根倒数发射 */
            LLVMTypeRef fty = LLVMFunctionType(v4f32, (LLVMTypeRef[]){ v4f32 }, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.x86.sse.rsqrt.ps");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.x86.sse.rsqrt.ps", fty);
            *out = v4f32_to_vec128(g, zan_call2(g->builder, fty, fn, &a, 1, "vrsqrt"));
            return true;
        }
        /* 非 x86 平台平方根倒数运算回退至通用 LLVM 浮点除法指令 */
        LLVMTypeRef fty = LLVMFunctionType(v4f32, (LLVMTypeRef[]){ v4f32 }, 1, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.sqrt.v4f32");
        if (!fn) fn = LLVMAddFunction(g->mod, "llvm.sqrt.v4f32", fty);
        LLVMValueRef sq = zan_call2(g->builder, fty, fn, &a, 1, "vsqrt");
        LLVMValueRef one = LLVMConstReal(f32t, 1.0);
        LLVMValueRef ones = LLVMConstVector((LLVMValueRef[]){ one, one, one, one }, 4);
        *out = v4f32_to_vec128(g, LLVMBuildFDiv(g->builder, ones, sq, "vrsqrt"));
        return true;
    }

    /* 向量乘加融合指令发射 (llvm.fma.v4f32) */
    if (argc == 3 && method.len == 11 && memcmp(method.str, "MultiplyAdd", 11) == 0) {
        LLVMTypeRef f32t = LLVMFloatTypeInContext(g->ctx);
        LLVMTypeRef v4f32 = LLVMVectorType(f32t, 4);
        LLVMValueRef a = vec128_to_v4f32(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMValueRef b = vec128_to_v4f32(g, emit_expr(g, expr->call.args.items[1], locals));
        LLVMValueRef c = vec128_to_v4f32(g, emit_expr(g, expr->call.args.items[2], locals));
        LLVMTypeRef fty = LLVMFunctionType(v4f32, (LLVMTypeRef[]){ v4f32, v4f32, v4f32 }, 3, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.fma.v4f32");
        if (!fn) fn = LLVMAddFunction(g->mod, "llvm.fma.v4f32", fty);
        LLVMValueRef args[3] = { a, b, c };
        *out = v4f32_to_vec128(g, zan_call2(g->builder, fty, fn, args, 3, "vfma"));
        return true;
    }

    /* 向量条件掩码选择指令发射 */
    if (argc == 3 && method.len == 17 && memcmp(method.str, "ConditionalSelect", 17) == 0) {
        LLVMValueRef cond = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMValueRef left = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
        LLVMValueRef right = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[2], locals));
        LLVMValueRef a_and = LLVMBuildAnd(g->builder, cond, left, "bld_l");
        LLVMValueRef not_cond = LLVMBuildNot(g->builder, cond, "bld_not");
        LLVMValueRef b_and = LLVMBuildAnd(g->builder, not_cond, right, "bld_r");
        *out = v16i8_to_vec128(g, LLVMBuildOr(g->builder, a_and, b_and, "blend"));
        return true;
    }

    /* 硬件预取指令发射 (llvm.prefetch) */
    if (method.len == 8 && memcmp(method.str, "Prefetch", 8) == 0 && (argc == 1 || argc == 2)) {
        LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
        LLVMValueRef addr_val = emit_expr(g, expr->call.args.items[0], locals);
        LLVMValueRef base;
        if (LLVMGetTypeKind(LLVMTypeOf(addr_val)) == LLVMPointerTypeKind) {
            base = LLVMBuildBitCast(g->builder, addr_val, LLVMPointerType(i8, 0), "pfetch_base");
        } else {
            base = LLVMBuildIntToPtr(g->builder, addr_val, LLVMPointerType(i8, 0), "pfetch_base");
        }
        if (argc == 2) {
            LLVMValueRef off_val = coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals), i64t);
            /* 数组源偏移按元素索引计算，与 Span/指针寻址保持语义一致 */
            zan_type_t *at = infer_expr_type(g, expr->call.args.items[0], locals);
            zan_type_t *et = at ? container_elem_type(at) : NULL;
            if (at && (at->kind == TYPE_ARRAY || is_span_type(at)) && et) {
                LLVMTypeRef etl = map_type(g, et);
                base = LLVMBuildGEP2(g->builder, etl,
                    LLVMBuildBitCast(g->builder, base,
                        LLVMPointerType(etl, 0), "pfetch_t"), &off_val, 1,
                    "pfetch_gep");
            } else {
                base = LLVMBuildGEP2(g->builder, i8, base, &off_val, 1, "pfetch_gep");
            }
        }
        LLVMTypeRef void_ty = LLVMVoidTypeInContext(g->ctx);
        LLVMTypeRef ptr_ty = LLVMPointerType(i8, 0);
        LLVMTypeRef fty = LLVMFunctionType(void_ty, (LLVMTypeRef[]){ ptr_ty, i32t, i32t, i32t }, 4, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.prefetch.p0");
        if (!fn) fn = LLVMAddFunction(g->mod, "llvm.prefetch.p0", fty);
        LLVMValueRef args[4] = {
            base,
            LLVMConstInt(i32t, 0, 0),
            LLVMConstInt(i32t, 3, 0),
            LLVMConstInt(i32t, 1, 0),
        };
        zan_call2(g->builder, fty, fn, args, 4, "");
        *out = NULL;
        return true;
    }

    /* 向量符号掩码提取指令 (movemask) */
    if ((method.len == 26 && memcmp(method.str, "ExtractMostSignificantBits", 26) == 0) ||
        (method.len == 8 && memcmp(method.str, "MoveMask", 8) == 0)) {
        if (argc == 1) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            if (emit_target_is_x86(g)) {
                LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ v16i8 }, 1, 0);
                LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.x86.sse2.pmovmskb.128");
                if (!fn) fn = LLVMAddFunction(g->mod, "llvm.x86.sse2.pmovmskb.128", fty);
                *out = zan_call2(g->builder, fty, fn, &a16, 1, "pmovmskb");
            } else {
                *out = emit_pmovmskb_portable(g, a16, 16);
            }
            return true;
        }
    }

    return false;
}

