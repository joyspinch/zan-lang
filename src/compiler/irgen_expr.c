/* irgen_expr.c: 表达式代码生成、NativeMemory 原语、Lambda 发射与泛型方法推导 (由 irgen.c 包含) */

static LLVMValueRef get_async_result_ptr(zan_irgen_t *g);
static LLVMValueRef get_async_sub_slot_ptr(zan_irgen_t *g);

/* NativeMemory 原语：堆外内存操作，地址使用 nint (i64)，不计入 ARC */

/* 将实例接收者绑定至方法组闭包记录；recv 为 NULL 时生成静态方法组 */
static LLVMValueRef emit_method_group_closure(zan_irgen_t *g, zan_symbol_t *msym,
                                              LLVMValueRef recv, zan_loc_t loc);

/* 静态方法组记录构建器 */
static LLVMValueRef build_static_mg_record(zan_irgen_t *g, zan_loc_t loc,
                                           const char *lname, LLVMTypeRef rec_ty,
                                           LLVMValueRef thunk);

/* 判断委托值是否必须全部作为带标记闭包记录（wasm32 平台区分函数指针表索引） */
static bool target_is_wasm32(zan_irgen_t *g);

/* 合成的 Binding<T> 访问器委托：原生平台使用纯函数指针，wasm32 采用静态方法组记录 */
static LLVMValueRef emit_binding_acc_delegate(zan_irgen_t *g, LLVMValueRef acc,
                                              LLVMTypeRef acc_ty);

/*
 * Whether `cls` or one of its base classes declares a member named `name`,
 * of any kind (field, method, property, event, constant).
 */
static int type_declares_member(zan_symbol_t *cls, zan_istr_t name) {
    while (cls) {
        for (int i = 0; i < cls->member_count; i++) {
            zan_symbol_t *m = cls->members[i];
            if (m && m->name.len == name.len &&
                memcmp(m->name.str, name.str, (size_t)name.len) == 0)
                return 1;
        }
        zan_symbol_t *base = (cls->type && cls->type->base_type)
            ? cls->type->base_type->sym : NULL;
        cls = (base && base != cls) ? base : NULL;
    }
    return 0;
}

/* 字符串边界安全判断：字面量与非透明局部变量可信任 NUL 终止符作为有效边界 */
static int expr_has_reliable_string_bounds(zan_ast_node_t *expr,
                                           local_scope_t *locals) {
    if (!expr) return 0;
    if (expr->kind == AST_STRING_LITERAL) return 1;
    if (expr->kind == AST_IDENTIFIER) {
        local_var_t *local = local_find(locals, expr->ident.name);
        return local && local->type && local->type->kind == TYPE_STRING &&
               !local->opaque_string;
    }
    return 0;
}

/* i8* pointer to (base + off); base/off are i64 values. */
static LLVMValueRef nm_addr(zan_irgen_t *g, LLVMValueRef base, LLVMValueRef off) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMValueRef p = LLVMBuildIntToPtr(g->builder, base, i8ptr, "nm.p");
    return LLVMBuildGEP2(g->builder, i8, p, &off, 1, "nm.at");
}

/* Widen/narrow an emitted argument to i64 (args arrive as iN or pointer). */
static LLVMValueRef nm_to_i64(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef t = LLVMTypeOf(v);
    if (LLVMGetTypeKind(t) == LLVMPointerTypeKind)
        return LLVMBuildPtrToInt(g->builder, v, i64t, "nm.pi");
    if (LLVMGetTypeKind(t) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(t) < 64)
        return LLVMBuildSExt(g->builder, v, i64t, "nm.sx");
    return v;
}

static LLVMValueRef nm_arg(zan_irgen_t *g, zan_ast_node_t *expr, int i,
                           local_scope_t *locals) {
    return nm_to_i64(g, emit_expr(g, expr->call.args.items[i], locals));
}

/* __zan_nm_crc32(i8*, i64): 编译期生成 256 项常量查表法的 IEEE 802.3 CRC32 内核 */
static LLVMValueRef nm_crc32_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_nm_crc32");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i8ptr, i64t }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_nm_crc32", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);

    LLVMValueRef entries[256];
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        entries[i] = LLVMConstInt(i32t, c, 0);
    }
    LLVMTypeRef tab_ty = LLVMArrayType(i32t, 256);
    LLVMValueRef tab = LLVMAddGlobal(g->mod, tab_ty, "__zan_crc32_table");
    LLVMSetInitializer(tab, LLVMConstArray(i32t, entries, 256));
    LLVMSetGlobalConstant(tab, 1);
    LLVMSetLinkage(tab, LLVMInternalLinkage);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "loop");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef data = LLVMGetParam(fn, 0);
    LLVMValueRef len = LLVMGetParam(fn, 1);
    LLVMBuildBr(g->builder, loop);

    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef idx = LLVMBuildPhi(g->builder, i64t, "i");
    LLVMValueRef crc = LLVMBuildPhi(g->builder, i32t, "crc");
    LLVMValueRef in_range = zan_icmp(g->builder, LLVMIntSLT, idx, len, "inrange");
    LLVMBuildCondBr(g->builder, in_range, body, done);

    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef bp = LLVMBuildGEP2(g->builder, i8, data, &idx, 1, "bp");
    LLVMValueRef byte = LLVMBuildZExt(g->builder,
        LLVMBuildLoad2(g->builder, i8, bp, "b"), i32t, "b32");
    LLVMValueRef ti = zan_and(g->builder,
        zan_xor(g->builder, crc, byte, "x"),
        LLVMConstInt(i32t, 0xFF, 0), "ti");
    LLVMValueRef ti64 = LLVMBuildZExt(g->builder, ti, i64t, "ti64");
    LLVMValueRef gep_idx[] = { LLVMConstInt(i64t, 0, 0), ti64 };
    LLVMValueRef ep = LLVMBuildGEP2(g->builder, tab_ty, tab, gep_idx, 2, "ep");
    LLVMValueRef te = LLVMBuildLoad2(g->builder, i32t, ep, "te");
    LLVMValueRef next_crc = zan_xor(g->builder, te,
        zan_lshr(g->builder, crc, LLVMConstInt(i32t, 8, 0), "sh"), "nc");
    LLVMValueRef next_idx = zan_add(g->builder, idx, LLVMConstInt(i64t, 1, 0), "ni");
    LLVMBuildBr(g->builder, loop);

    LLVMAddIncoming(idx, (LLVMValueRef[]){ LLVMConstInt(i64t, 0, 0), next_idx },
        (LLVMBasicBlockRef[]){ entry, body }, 2);
    LLVMAddIncoming(crc, (LLVMValueRef[]){ LLVMConstInt(i32t, 0xFFFFFFFFu, 0), next_crc },
        (LLVMBasicBlockRef[]){ entry, body }, 2);

    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMValueRef fin = zan_xor(g->builder, crc,
        LLVMConstInt(i32t, 0xFFFFFFFFu, 0), "fin");
    LLVMBuildRet(g->builder, LLVMBuildZExt(g->builder, fin, i64t, "fin64"));
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* __zan_crc32c_table: 256 项 CRC32C 预计算查找表 */
static LLVMValueRef crc32c_table_global(zan_irgen_t *g) {
    LLVMValueRef tab = LLVMGetNamedGlobal(g->mod, "__zan_crc32c_table");
    if (tab) return tab;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef entries[256];
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0x82F63B78u ^ (c >> 1)) : (c >> 1);
        entries[i] = LLVMConstInt(i32t, c, 0);
    }
    LLVMTypeRef tab_ty = LLVMArrayType(i32t, 256);
    tab = LLVMAddGlobal(g->mod, tab_ty, "__zan_crc32c_table");
    LLVMSetInitializer(tab, LLVMConstArray(i32t, entries, 256));
    LLVMSetGlobalConstant(tab, 1);
    LLVMSetLinkage(tab, LLVMInternalLinkage);
    return tab;
}

/*
 * __zan_nm_crc32c(i8*, i64) -> i64: CRC32C (Castagnoli, reflected polynomial
 * 0x82F63B78), table-driven, self-contained internal function.
 */
static LLVMValueRef nm_crc32c_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_nm_crc32c");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i8ptr, i64t }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_nm_crc32c", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);

    LLVMValueRef tab = crc32c_table_global(g);
    LLVMTypeRef tab_ty = LLVMArrayType(i32t, 256);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "loop");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef data = LLVMGetParam(fn, 0);
    LLVMValueRef len = LLVMGetParam(fn, 1);
    LLVMBuildBr(g->builder, loop);

    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef idx = LLVMBuildPhi(g->builder, i64t, "i");
    LLVMValueRef crc = LLVMBuildPhi(g->builder, i32t, "crc");
    LLVMValueRef in_range = zan_icmp(g->builder, LLVMIntSLT, idx, len, "inrange");
    LLVMBuildCondBr(g->builder, in_range, body, done);

    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef bp = LLVMBuildGEP2(g->builder, i8, data, &idx, 1, "bp");
    LLVMValueRef byte = LLVMBuildZExt(g->builder,
        LLVMBuildLoad2(g->builder, i8, bp, "b"), i32t, "b32");
    LLVMValueRef ti = zan_and(g->builder,
        zan_xor(g->builder, crc, byte, "x"),
        LLVMConstInt(i32t, 0xFF, 0), "ti");
    LLVMValueRef ti64 = LLVMBuildZExt(g->builder, ti, i64t, "ti64");
    LLVMValueRef gep_idx[] = { LLVMConstInt(i64t, 0, 0), ti64 };
    LLVMValueRef ep = LLVMBuildGEP2(g->builder, tab_ty, tab, gep_idx, 2, "ep");
    LLVMValueRef te = LLVMBuildLoad2(g->builder, i32t, ep, "te");
    LLVMValueRef next_crc = zan_xor(g->builder, te,
        zan_lshr(g->builder, crc, LLVMConstInt(i32t, 8, 0), "sh"), "nc");
    LLVMValueRef next_idx = zan_add(g->builder, idx, LLVMConstInt(i64t, 1, 0), "ni");
    LLVMBuildBr(g->builder, loop);

    LLVMAddIncoming(idx, (LLVMValueRef[]){ LLVMConstInt(i64t, 0, 0), next_idx },
        (LLVMBasicBlockRef[]){ entry, body }, 2);
    LLVMAddIncoming(crc, (LLVMValueRef[]){ LLVMConstInt(i32t, 0xFFFFFFFFu, 0), next_crc },
        (LLVMBasicBlockRef[]){ entry, body }, 2);

    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMValueRef fin = zan_xor(g->builder, crc,
        LLVMConstInt(i32t, 0xFFFFFFFFu, 0), "fin");
    LLVMBuildRet(g->builder, LLVMBuildZExt(g->builder, fin, i64t, "fin64"));
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* __zan_crc32c_stepN: 非 x86 目标平台的逐字节/多字节 CRC32C 软降解实现 */
static LLVMValueRef crc32c_step_fn(zan_irgen_t *g, int nbytes) {
    char name[40];
    snprintf(name, sizeof(name), "__zan_crc32c_step%d", nbytes);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, name);
    if (fn) return fn;
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i16t = LLVMInt16TypeInContext(g->ctx);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef rt = (nbytes == 8) ? i64t : i32t;
    LLVMTypeRef dt = (nbytes == 1) ? i8t : (nbytes == 2) ? i16t
                   : (nbytes == 4) ? i32t : i64t;
    LLVMTypeRef fnty = LLVMFunctionType(rt, (LLVMTypeRef[]){ rt, dt }, 2, 0);
    fn = LLVMAddFunction(g->mod, name, fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);

    LLVMValueRef tab = crc32c_table_global(g);
    LLVMTypeRef tab_ty = LLVMArrayType(i32t, 256);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);

    LLVMValueRef crc = LLVMBuildTrunc(g->builder, LLVMGetParam(fn, 0), i32t, "crc32");
    LLVMValueRef data;
    if (dt == i64t) data = LLVMGetParam(fn, 1);
    else data = LLVMBuildZExt(g->builder, LLVMGetParam(fn, 1), i64t, "data64");

    for (int k = 0; k < nbytes; k++) {
        char pn[16];
        snprintf(pn, sizeof(pn), "b%d", k);
        LLVMValueRef sh = LLVMConstInt(i64t, 8 * k, 0);
        LLVMValueRef b64 = zan_and(g->builder,
            zan_lshr(g->builder, data, sh, pn), LLVMConstInt(i64t, 0xFF, 0), "bm");
        LLVMValueRef b32 = LLVMBuildTrunc(g->builder, b64, i32t, pn);
        LLVMValueRef ti = zan_and(g->builder,
            zan_xor(g->builder, crc, b32, "x"), LLVMConstInt(i32t, 0xFF, 0), "ti");
        LLVMValueRef ti64 = LLVMBuildZExt(g->builder, ti, i64t, "ti64");
        LLVMValueRef gep_idx[] = { LLVMConstInt(i64t, 0, 0), ti64 };
        LLVMValueRef ep = LLVMBuildGEP2(g->builder, tab_ty, tab, gep_idx, 2, "ep");
        LLVMValueRef te = LLVMBuildLoad2(g->builder, i32t, ep, "te");
        crc = zan_xor(g->builder, te,
            zan_lshr(g->builder, crc, LLVMConstInt(i32t, 8, 0), "sh"), "nc");
    }

    if (nbytes == 8)
        LLVMBuildRet(g->builder, LLVMBuildZExt(g->builder, crc, i64t, "ret64"));
    else
        LLVMBuildRet(g->builder, crc);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* 硬件哈希内核调用状态：0 为成功写入，-1 为当前硬件不支持需纯 Zan 回退 */
static LLVMValueRef nm_digest_fn(zan_irgen_t *g, const char *name) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, name);
    if (fn) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i64t,
        (LLVMTypeRef[]){ i8ptr, i64t, i8ptr }, 3, 0);
    return LLVMAddFunction(g->mod, name, fnty);
}

/* NativeMemory 哈希原语发射：调用内核写入临时栈块，成功返回 ARC string，失败返回 null */
static bool emit_nm_digest(zan_irgen_t *g, zan_ast_node_t *expr,
                           local_scope_t *locals, LLVMValueRef *out,
                           const char *kernel, int64_t digest_len) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef zero64 = LLVMConstInt(i64t, 0, 0);
    LLVMValueRef p = nm_arg(g, expr, 0, locals);
    LLVMValueRef len = nm_arg(g, expr, 1, locals);
    /* a negative length would wrap the string allocation below */
    len = LLVMBuildSelect(g->builder,
        zan_icmp(g->builder, LLVMIntSLT, len, zero64, "nm.dg.neg"),
        zero64, len, "nm.dg.len");
    /* 栈上分配哈希摘要临时缓冲区 */
    LLVMValueRef cur_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMValueRef buf = LLVMBuildArrayAlloca(g->builder, i8,
        LLVMConstInt(i64t, digest_len, 0), "nm.dg.buf");

    LLVMTypeRef fty = LLVMFunctionType(i64t,
        (LLVMTypeRef[]){ i8ptr, i64t, i8ptr }, 3, 0);
    LLVMValueRef st = zan_call2(g->builder, fty, nm_digest_fn(g, kernel),
        (LLVMValueRef[]){ nm_addr(g, p, zero64), len, buf }, 3, "nm.dg.st");

    LLVMBasicBlockRef has_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "nm.dg.has");
    LLVMBasicBlockRef none_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "nm.dg.none");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "nm.dg.done");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntSGE, st, zero64, "nm.dg.ok"), has_bb, none_bb);

    LLVMPositionBuilderAtEnd(g->builder, has_bb);
    LLVMValueRef dlen = LLVMConstInt(i64t, digest_len, 0);
    LLVMValueRef s = emit_string_alloc_rc(g, LLVMConstInt(i64t, digest_len + 1, 0));
    LLVMTypeRef mty = LLVMFunctionType(i8ptr,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    zan_call2(g->builder, mty, get_libc_fn(g, "memcpy", mty),
        (LLVMValueRef[]){ s, buf, dlen }, 3, "");
    LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, s, &dlen, 1, "nm.dg.end");
    zan_store_fit(g, LLVMConstInt(i8, 0, 0), endp);
    /* 已知哈希输出字节长度，直接标记长度并交由调用方持有所有权 */
    emit_string_len_set(g, s, dlen);
    LLVMBuildBr(g->builder, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, none_bb);
    LLVMBuildBr(g->builder, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    LLVMValueRef res = LLVMBuildPhi(g->builder, i8ptr, "nm.dg.res");
    LLVMAddIncoming(res, (LLVMValueRef[]){ s, LLVMConstNull(i8ptr) },
        (LLVMBasicBlockRef[]){ has_bb, none_bb }, 2);
    *out = res;
    return true;
}


/* 2D 跨步内存复制：按行拷贝指定行数与行宽字节 */
static LLVMValueRef nm_copy2d_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_nm_copy2d");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(g->ctx);

    LLVMTypeRef fn_ty = LLVMFunctionType(void_ty,
        (LLVMTypeRef[]){ i8ptr, i64t, i8ptr, i64t, i64t, i64t }, 6, 0);
    fn = LLVMAddFunction(g->mod, "__zan_nm_copy2d", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef check_fast = LLVMAppendBasicBlockInContext(g->ctx, fn, "check_fast");
    LLVMBasicBlockRef fast_path = LLVMAppendBasicBlockInContext(g->ctx, fn, "fast");
    LLVMBasicBlockRef slow_loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "loop");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef dst = LLVMGetParam(fn, 0);
    LLVMValueRef dst_stride = LLVMGetParam(fn, 1);
    LLVMValueRef src = LLVMGetParam(fn, 2);
    LLVMValueRef src_stride = LLVMGetParam(fn, 3);
    LLVMValueRef row_bytes = LLVMGetParam(fn, 4);
    LLVMValueRef height = LLVMGetParam(fn, 5);

    LLVMValueRef zero64 = LLVMConstInt(i64t, 0, 0);
    LLVMValueRef has_bytes = zan_icmp(g->builder, LLVMIntSGT, row_bytes, zero64, "has_b");
    LLVMValueRef has_lines = zan_icmp(g->builder, LLVMIntSGT, height, zero64, "has_h");
    LLVMValueRef can_copy = zan_and(g->builder, has_bytes, has_lines, "can_cp");
    LLVMBuildCondBr(g->builder, can_copy, check_fast, done);

    /* Check if contiguous: dst_stride == row_bytes && src_stride == row_bytes */
    LLVMPositionBuilderAtEnd(g->builder, check_fast);
    LLVMValueRef eq_dst = zan_icmp(g->builder, LLVMIntEQ, dst_stride, row_bytes, "eq_dst");
    LLVMValueRef eq_src = zan_icmp(g->builder, LLVMIntEQ, src_stride, row_bytes, "eq_src");
    LLVMValueRef is_fast = zan_and(g->builder, eq_dst, eq_src, "is_fast");
    LLVMBuildCondBr(g->builder, is_fast, fast_path, slow_loop);

    LLVMTypeRef memmove_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    LLVMValueRef memmove_fn = get_libc_fn(g, "memmove", memmove_ty);

    /* Fast path: single memmove(dst, src, row_bytes * height) */
    LLVMPositionBuilderAtEnd(g->builder, fast_path);
    LLVMValueRef total_bytes = zan_mul(g->builder, row_bytes, height, "total_b");
    zan_call2(g->builder, memmove_ty, memmove_fn, (LLVMValueRef[]){ dst, src, total_bytes }, 3, "");
    LLVMBuildBr(g->builder, done);

    /* Slow path: loop row = 0 .. height-1 */
    LLVMPositionBuilderAtEnd(g->builder, slow_loop);
    LLVMValueRef row = LLVMBuildPhi(g->builder, i64t, "row");
    LLVMValueRef in_range = zan_icmp(g->builder, LLVMIntSLT, row, height, "in_rng");
    LLVMBuildCondBr(g->builder, in_range, body, done);

    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef dst_off = zan_mul(g->builder, row, dst_stride, "dst_off");
    LLVMValueRef src_off = zan_mul(g->builder, row, src_stride, "src_off");
    LLVMValueRef row_dst = LLVMBuildGEP2(g->builder, i8, dst, &dst_off, 1, "rdst");
    LLVMValueRef row_src = LLVMBuildGEP2(g->builder, i8, src, &src_off, 1, "rsrc");
    zan_call2(g->builder, memmove_ty, memmove_fn, (LLVMValueRef[]){ row_dst, row_src, row_bytes }, 3, "");
    LLVMValueRef next_row = zan_add(g->builder, row, LLVMConstInt(i64t, 1, 0), "next_row");
    LLVMBuildBr(g->builder, slow_loop);

    LLVMAddIncoming(row, (LLVMValueRef[]){ zero64, next_row },
        (LLVMBasicBlockRef[]){ check_fast, body }, 2);

    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildRetVoid(g->builder);

    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* Span<T> 原语：将切片操作降解为 { i8* base, i64 len } 胖指针视图 */
static bool emit_span_call(zan_irgen_t *g, zan_ast_node_t *expr,
                           local_scope_t *locals, LLVMValueRef *out) {
    zan_ast_node_t *callee = expr->call.callee;
    if (!callee || callee->kind != AST_MEMBER_ACCESS) return false;
    zan_istr_t mm = callee->member.name;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    if (mm.len == 6 && memcmp(mm.str, "AsSpan", 6) == 0) {
        zan_type_t *ot = infer_expr_type(g, callee->member.object, locals);
        if (!ot || ot->kind != TYPE_ARRAY || !ot->element_type) return false;
        LLVMValueRef arr = emit_expr(g, callee->member.object, locals);
        LLVMValueRef base = LLVMBuildBitCast(g->builder, arr, i8ptr, "span.base");
        LLVMValueRef len = zan_array_len(g, arr);
        LLVMTypeRef elem_llvm = map_type(g, ot->element_type);
        if (expr->call.args.count >= 1) {
            LLVMValueRef start = coerce_int_to(g,
                emit_expr(g, expr->call.args.items[0], locals), i64t);
            LLVMValueRef window = (expr->call.args.count >= 2)
                ? coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals), i64t)
                : LLVMBuildSub(g->builder, len, start, "span.len");
            emit_span_window_check(g, start, window, len, expr->loc, "span");
            emit_span_safe_window(g, &start, &window, len, expr->loc, "span");
            LLVMValueRef off = LLVMBuildMul(g->builder, start,
                LLVMSizeOf(elem_llvm), "span.boff");
            base = LLVMBuildGEP2(g->builder, i8, base, &off, 1, "span.base2");
            len = window;
        }
        LLVMValueRef v = LLVMGetUndef(g->span_struct_type);
        v = LLVMBuildInsertValue(g->builder, v, base, 0, "span.iv0");
        v = LLVMBuildInsertValue(g->builder, v, len, 1, "span.iv1");
        *out = v;
        return true;
    }
    if (mm.len == 5 && memcmp(mm.str, "Slice", 5) == 0) {
        zan_type_t *ot = infer_expr_type(g, callee->member.object, locals);
        if (!is_span_type(ot) || expr->call.args.count < 1) return false;
        zan_type_t *elem = container_elem_type(ot);
        LLVMTypeRef elem_llvm = elem ? map_type(g, elem)
                                     : LLVMInt32TypeInContext(g->ctx);
        LLVMValueRef span_val = emit_expr(g, callee->member.object, locals);
        LLVMValueRef base = LLVMBuildExtractValue(g->builder, span_val, 0, "sl.base");
        LLVMValueRef len = LLVMBuildExtractValue(g->builder, span_val, 1, "sl.len");
        LLVMValueRef start = coerce_int_to(g,
            emit_expr(g, expr->call.args.items[0], locals), i64t);
        LLVMValueRef nlen = (expr->call.args.count >= 2)
            ? coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals), i64t)
            : LLVMBuildSub(g->builder, len, start, "sl.len2");
        emit_span_window_check(g, start, nlen, len, expr->loc, "span");
        emit_span_safe_window(g, &start, &nlen, len, expr->loc, "span");
        LLVMValueRef off = LLVMBuildMul(g->builder, start,
            LLVMSizeOf(elem_llvm), "sl.boff");
        LLVMValueRef nbase = LLVMBuildGEP2(g->builder, i8, base, &off, 1, "sl.base2");
        LLVMValueRef v = LLVMGetUndef(g->span_struct_type);
        v = LLVMBuildInsertValue(g->builder, v, nbase, 0, "sl.iv0");
        v = LLVMBuildInsertValue(g->builder, v, nlen, 1, "sl.iv1");
        *out = v;
        return true;
    }
    return false;
}

/* byte[] 与 string 零拷贝互转桥接原语（ToBytes / ToStr） */
static bool emit_bytes_call(zan_irgen_t *g, zan_ast_node_t *expr,
                            local_scope_t *locals, LLVMValueRef *out) {
    zan_ast_node_t *callee = expr->call.callee;
    if (!callee || callee->kind != AST_MEMBER_ACCESS) return false;
    zan_istr_t mm = callee->member.name;
    bool to_bytes = mm.len == 7 && memcmp(mm.str, "ToBytes", 7) == 0;
    bool to_str = mm.len == 5 && memcmp(mm.str, "ToStr", 5) == 0;
    if (!to_bytes && !to_str) return false;

    zan_type_t *ot = infer_expr_type(g, callee->member.object, locals);
    if (!ot) return false;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef memcpy_ty =
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);

    if (to_bytes) {
        if (ot->kind != TYPE_STRING || expr->call.args.count != 0) return false;
        /*
         * normalize a null receiver to the shared empty string before
         * handing it to strlen
         */
        LLVMValueRef obj_v = emit_expr(g, callee->member.object, locals);
        LLVMValueRef s = emit_str_nonnull(g, obj_v);
        LLVMValueRef len = emit_string_length(g, s, callee->loc);
        LLVMValueRef arr = zan_array_alloc(g, len, len);
        zan_call2(g->builder, memcpy_ty, memcpy_fn,
                  (LLVMValueRef[]){ arr, s, len }, 3, "");
        /* 链式接收者临时对象需在转换后按 RC 规则及时释放 */
        emit_release_owned_call_temp(g, callee->member.object, obj_v, locals);
        *out = arr;
        return true;
    }

    if (ot->kind != TYPE_ARRAY) return false;
    if (expr->call.args.count != 0 && expr->call.args.count != 2) return false;
    LLVMValueRef arr = emit_expr(g, callee->member.object, locals);
    LLVMValueRef src = arr, len = zan_array_len(g, arr);
    if (expr->call.args.count == 2) {
        LLVMValueRef off = coerce_int_to(g,
            emit_expr(g, expr->call.args.items[0], locals), i64t);
        len = coerce_int_to(g,
            emit_expr(g, expr->call.args.items[1], locals), i64t);
        /* 窗口区间边界截断：越界请求安全返回空字符串 */
        LLVMValueRef zero64 = LLVMConstInt(i64t, 0, 0);
        LLVMValueRef alen = zan_array_len(g, arr);
        off = LLVMBuildSelect(g->builder,
            zan_icmp(g->builder, LLVMIntSLT, off, zero64, "ts.oneg"),
            zero64, off, "ts.off");
        off = LLVMBuildSelect(g->builder,
            zan_icmp(g->builder, LLVMIntSGT, off, alen, "ts.obig"),
            alen, off, "ts.off2");
        LLVMValueRef avail = zan_sub(g->builder, alen, off, "ts.avail");
        len = LLVMBuildSelect(g->builder,
            zan_icmp(g->builder, LLVMIntSLT, len, zero64, "ts.lneg"),
            zero64, len, "ts.len");
        len = LLVMBuildSelect(g->builder,
            zan_icmp(g->builder, LLVMIntSGT, len, avail, "ts.lbig"),
            avail, len, "ts.len2");
        src = LLVMBuildGEP2(g->builder, i8, arr, &off, 1, "ts.src");
    }
    LLVMTypeRef alloc_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64t }, 1, 0);
    LLVMValueRef total = LLVMBuildAdd(g->builder, len, LLVMConstInt(i64t, 1, 0), "ts.n");
    LLVMValueRef s = zan_call2(g->builder, alloc_ty, g->rt_str_alloc, &total, 1, "ts.str");
    zan_call2(g->builder, memcpy_ty, memcpy_fn,
              (LLVMValueRef[]){ s, src, len }, 3, "");
    LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, s, &len, 1, "ts.end");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
    /* 标记调用方显式声明的字节长度，避免后续扫描判断失真 */
    emit_string_len_set(g, s, len);
    /*
     * Same owned-receiver release as the to_bytes branch above — a chained
     * `zip.Extract(...).ToStr()` leaked the receiver array on every call.
     */
    emit_release_owned_call_temp(g, callee->member.object, arr, locals);
    *out = s;
    return true;
}

static bool emit_native_memory_call(zan_irgen_t *g, zan_ast_node_t *expr,
                                    local_scope_t *locals, LLVMValueRef *out) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef zero64 = LLVMConstInt(i64t, 0, 0);

    if (is_call_to(expr, "NativeMemory", "Alloc") && expr->call.args.count == 1) {
        LLVMValueRef size = nm_arg(g, expr, 0, locals);
        LLVMTypeRef ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64t, i64t }, 2, 0);
        LLVMValueRef fn = get_libc_fn(g, "calloc", ty);
        /*
         * a negative size would turn into an enormous calloc (likely null,
         * but only after an absurd attempt); reject it up front by returning
         * address 0.
         */
        LLVMValueRef ok = zan_icmp(g->builder, LLVMIntSGE, size, zero64, "nm.nneg");
        LLVMValueRef fnh = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
        LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.ok");
        LLVMBasicBlockRef bad_bb = LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.bad");
        LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fnh, "nm.done");
        LLVMBuildCondBr(g->builder, ok, ok_bb, bad_bb);

        LLVMPositionBuilderAtEnd(g->builder, ok_bb);
        LLVMValueRef p = zan_call2(g->builder, ty, fn,
            (LLVMValueRef[]){ size, LLVMConstInt(i64t, 1, 0) }, 2, "nm.alloc");
        LLVMBuildBr(g->builder, done_bb);

        LLVMPositionBuilderAtEnd(g->builder, bad_bb);
        LLVMBuildBr(g->builder, done_bb);

        LLVMPositionBuilderAtEnd(g->builder, done_bb);
        LLVMValueRef ptr = LLVMBuildPhi(g->builder, i8ptr, "nm.p");
        LLVMAddIncoming(ptr, (LLVMValueRef[]){ p, LLVMConstNull(i8ptr) },
                        (LLVMBasicBlockRef[]){ ok_bb, bad_bb }, 2);
        *out = LLVMBuildPtrToInt(g->builder, ptr, i64t, "nm.addr");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Free") && expr->call.args.count == 1) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMTypeRef ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8ptr }, 1, 0);
        LLVMValueRef fn = get_libc_fn(g, "free", ty);
        LLVMValueRef pp = LLVMBuildIntToPtr(g->builder, p, i8ptr, "nm.fp");
        zan_call2(g->builder, ty, fn, &pp, 1, "");
        *out = zero64;
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Copy") && expr->call.args.count == 3) {
        LLVMValueRef dst = nm_arg(g, expr, 0, locals);
        LLVMValueRef src = nm_arg(g, expr, 1, locals);
        LLVMValueRef n = nm_arg(g, expr, 2, locals);
        LLVMTypeRef ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
        LLVMValueRef fn = get_libc_fn(g, "memmove", ty);
        zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, dst, zero64), nm_addr(g, src, zero64), n }, 3, "");
        *out = zero64;
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Copy2D") && expr->call.args.count == 6) {
        LLVMValueRef dst = nm_arg(g, expr, 0, locals);
        LLVMValueRef dst_stride = nm_arg(g, expr, 1, locals);
        LLVMValueRef src = nm_arg(g, expr, 2, locals);
        LLVMValueRef src_stride = nm_arg(g, expr, 3, locals);
        LLVMValueRef row_bytes = nm_arg(g, expr, 4, locals);
        LLVMValueRef height = nm_arg(g, expr, 5, locals);
        LLVMValueRef fn = nm_copy2d_fn(g);
        LLVMTypeRef ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8ptr, i64t, i8ptr, i64t, i64t, i64t }, 6, 0);
        zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, dst, zero64), dst_stride, nm_addr(g, src, zero64), src_stride, row_bytes, height }, 6, "");
        *out = zero64;
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Fill") && expr->call.args.count == 3) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef v = nm_arg(g, expr, 1, locals);
        LLVMValueRef n = nm_arg(g, expr, 2, locals);
        LLVMTypeRef ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t, i64t }, 3, 0);
        LLVMValueRef fn = get_libc_fn(g, "memset", ty);
        zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, p, zero64),
            LLVMBuildTrunc(g->builder, v, i32t, "nm.b"), n }, 3, "");
        *out = zero64;
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Compare") && expr->call.args.count == 3) {
        LLVMValueRef a = nm_arg(g, expr, 0, locals);
        LLVMValueRef b = nm_arg(g, expr, 1, locals);
        LLVMValueRef n = nm_arg(g, expr, 2, locals);
        LLVMTypeRef ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
        LLVMValueRef fn = get_libc_fn(g, "memcmp", ty);
        LLVMValueRef r = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, a, zero64), nm_addr(g, b, zero64), n }, 3, "nm.cmp");
        *out = LLVMBuildSExt(g->builder, r, i64t, "nm.cmp64");
        return true;
    }

    /* Find(p, off, b, n): 基于 memchr 查找目标字节在窗口内的相对偏移 */
    if (is_call_to(expr, "NativeMemory", "Find") && expr->call.args.count == 4) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef off = nm_arg(g, expr, 1, locals);
        LLVMValueRef b = nm_arg(g, expr, 2, locals);
        LLVMValueRef n = nm_arg(g, expr, 3, locals);
        LLVMTypeRef ty = LLVMFunctionType(i8ptr,
            (LLVMTypeRef[]){ i8ptr, i32t, i64t }, 3, 0);
        LLVMValueRef fn = get_libc_fn(g, "memchr", ty);
        LLVMValueRef hit = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, p, off),
            LLVMBuildTrunc(g->builder, b, i32t, "nm.find.b"), n }, 3,
            "nm.find");
        LLVMValueRef found = zan_icmp(g->builder, LLVMIntNE, hit,
                                      LLVMConstNull(i8ptr), "nm.find.ok");
        LLVMValueRef at = LLVMBuildPtrToInt(g->builder, hit, i64t, "nm.find.a");
        LLVMValueRef rel = LLVMBuildSub(g->builder, at, p, "nm.find.rel");
        *out = LLVMBuildSelect(g->builder, found, rel,
                               LLVMConstInt(i64t, (uint64_t)-1, 1), "nm.find.r");
        return true;
    }

    /* ScanNotAnyOf: 基于 strspn 扫描首个不属于白名单集合的字符偏移 */
    if (is_call_to(expr, "NativeMemory", "ScanNotAnyOf") &&
        expr->call.args.count == 4) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef off = nm_arg(g, expr, 1, locals);
        zan_ast_node_t *set_ast = expr->call.args.items[2];
        LLVMValueRef setv = emit_expr(g, set_ast, locals);
        LLVMValueRef n = nm_arg(g, expr, 3, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
        LLVMValueRef fn = get_libc_fn(g, "strspn", ty);
        LLVMValueRef start = nm_addr(g, p, off);
        LLVMValueRef run = zan_call2(g->builder, ty, fn,
            (LLVMValueRef[]){ start, setv }, 2, "nm.scanany");
        LLVMValueRef over = zan_icmp(g->builder, LLVMIntUGT, run, n, "nm.scanany.o");
        *out = LLVMBuildSelect(g->builder, over, n, run, "nm.scanany.r");
        emit_release_owned_call_temp(g, set_ast, setv, locals);
        return true;
    }

    /* FindNotAnyOf: 基于 strcspn 查找首个命中黑名单字符集的偏移 */
    if (is_call_to(expr, "NativeMemory", "FindNotAnyOf") &&
        expr->call.args.count == 4) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef off = nm_arg(g, expr, 1, locals);
        zan_ast_node_t *set_ast = expr->call.args.items[2];
        LLVMValueRef setv = emit_expr(g, set_ast, locals);
        LLVMValueRef n = nm_arg(g, expr, 3, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
        LLVMValueRef fn = get_libc_fn(g, "strcspn", ty);
        LLVMValueRef start = nm_addr(g, p, off);
        LLVMValueRef run = zan_call2(g->builder, ty, fn,
            (LLVMValueRef[]){ start, setv }, 2, "nm.fna");
        /* 扫描至窗口末尾未发现匹配项时返回 -1 哨兵值 */
        LLVMValueRef clean = zan_icmp(g->builder, LLVMIntUGE, run, n, "nm.fna.c");
        LLVMValueRef rel = LLVMBuildSelect(g->builder, clean,
            LLVMConstInt(i64t, (uint64_t)-1, 1), run, "nm.fna.r");
        *out = rel;
        emit_release_owned_call_temp(g, set_ast, setv, locals);
        return true;
    }

    /* Load64: 从指定偏移读取 8 字节未对齐标量 */
    if (is_call_to(expr, "NativeMemory", "Load64") &&
        expr->call.args.count == 2) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef off = nm_arg(g, expr, 1, locals);
        LLVMValueRef r = LLVMBuildLoad2(g->builder, i64t,
            nm_addr(g, p, off), "nm.ld64");
        LLVMSetAlignment(r, 1);
        *out = r;
        return true;
    }

    /* AsI64 / AsF64: long 与 double 之间的零开销二进制重解释位转换 */
    if (is_call_to(expr, "NativeMemory", "AsI64") &&
        expr->call.args.count == 1) {
        LLVMValueRef v = nm_arg(g, expr, 0, locals);
        *out = LLVMBuildBitCast(g->builder, v, i64t, "nm.asi64");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "AsF64") &&
        expr->call.args.count == 1) {
        LLVMTypeRef dblt = LLVMDoubleTypeInContext(g->ctx);
        LLVMValueRef v = nm_arg(g, expr, 0, locals);
        *out = LLVMBuildBitCast(g->builder, v, dblt, "nm.asf64");
        return true;
    }

    /* ScanNotByte: 扫描首个不等于指定字节 b 的位置偏移 */
    if (is_call_to(expr, "NativeMemory", "ScanNotByte") &&
        expr->call.args.count == 4) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef off = nm_arg(g, expr, 1, locals);
        LLVMValueRef b = nm_arg(g, expr, 2, locals);
        LLVMValueRef n = nm_arg(g, expr, 3, locals);
        LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
        LLVMTypeRef arr2 = LLVMArrayType(i8, 2);
        LLVMValueRef accept;
        if (LLVMIsConstant(b)) {
            /* 构建双字节 NUL 终止字符集用于快速字符匹配 */
            accept = LLVMAddGlobal(g->mod, arr2, "nm.scan.accept");
            LLVMSetLinkage(accept, LLVMPrivateLinkage);
            LLVMSetInitializer(accept, LLVMConstArray(i8, (LLVMValueRef[]){
                LLVMBuildTrunc(g->builder, b, i8, "nm.scan.b"),
                LLVMConstInt(i8, 0, 0) }, 2));
        } else {
            /* 静态常量全局变量初始化约束检查 */
            accept = emit_entry_alloca(g, arr2, "nm.scan.acc");
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef b8 = LLVMBuildTrunc(g->builder, b, i8, "nm.scan.b");
            LLVMValueRef e0 = LLVMBuildGEP2(g->builder, i8, accept,
                (LLVMValueRef[]){ LLVMConstInt(i32t, 0, 0),
                                  LLVMConstInt(i32t, 0, 0) }, 2, "nm.scan.a0");
            LLVMBuildStore(g->builder, b8, e0);
            LLVMValueRef e1 = LLVMBuildGEP2(g->builder, i8, accept,
                (LLVMValueRef[]){ LLVMConstInt(i32t, 0, 0),
                                  LLVMConstInt(i32t, 1, 0) }, 2, "nm.scan.a1");
            LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), e1);
        }
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
        LLVMValueRef fn = get_libc_fn(g, "strspn", ty);
        LLVMValueRef start = nm_addr(g, p, off);
        LLVMValueRef run = zan_call2(g->builder, ty, fn,
            (LLVMValueRef[]){ start, accept }, 2, "nm.scan");
        LLVMValueRef over = zan_icmp(g->builder, LLVMIntUGT, run, n, "nm.scan.o");
        *out = LLVMBuildSelect(g->builder, over, n, run, "nm.scan.r");
        return true;
    }

    if (is_call_to(expr, "NativeMemory", "GetString") && expr->call.args.count == 3) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef off = nm_arg(g, expr, 1, locals);
        LLVMValueRef len = nm_arg(g, expr, 2, locals);
        /*
         * a negative length would wrap the allocation size below; treat it
         * as zero instead
         */
        len = LLVMBuildSelect(g->builder,
            zan_icmp(g->builder, LLVMIntSLT, len, zero64, "nm.gs.neg"),
            zero64, len, "nm.gs.len");
        LLVMValueRef one = LLVMConstInt(i64t, 1, 0);
        LLVMValueRef total = zan_add(g->builder, len, one, "nm.gs.sz");
        LLVMValueRef s = emit_string_alloc_rc(g, total);
        LLVMTypeRef ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
        LLVMValueRef fn = get_libc_fn(g, "memcpy", ty);
        zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            s, nm_addr(g, p, off), len }, 3, "");
        LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, s, &len, 1, "nm.gs.end");
        zan_store_fit(g, LLVMConstInt(i8, 0, 0), endp);
        /* 已知确切长度的字符串直接构建并标记长度 */
        emit_string_len_set(g, s, len);
        *out = s;
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "PutString") && expr->call.args.count == 4) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef off = nm_arg(g, expr, 1, locals);
        zan_ast_node_t *s_ast = expr->call.args.items[2];
        LLVMValueRef s = emit_expr(g, s_ast, locals);
        LLVMValueRef len = nm_arg(g, expr, 3, locals);
        /* a negative length would wrap into a huge memcpy */
        len = LLVMBuildSelect(g->builder,
            zan_icmp(g->builder, LLVMIntSLT, len, zero64, "nm.ps.neg"),
            zero64, len, "nm.ps.len");
        LLVMTypeRef ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
        LLVMValueRef fn = get_libc_fn(g, "memcpy", ty);
        zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, p, off), s, len }, 3, "");
        emit_release_owned_call_temp(g, s_ast, s, locals);
        *out = zero64;
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Crc32") && expr->call.args.count == 2) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef len = nm_arg(g, expr, 1, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i8ptr, i64t }, 2, 0);
        LLVMValueRef fn = nm_crc32_fn(g);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, p, zero64), len }, 2, "nm.crc");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Crc32C") && expr->call.args.count == 2) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef len = nm_arg(g, expr, 1, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i8ptr, i64t }, 2, 0);
        LLVMValueRef fn = nm_crc32c_fn(g);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, p, zero64), len }, 2, "nm.crc32c");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Sha256") && expr->call.args.count == 2) {
        emit_nm_digest(g, expr, locals, out, "zan_hw_sha256", 32);
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "AesCbcEncrypt") && expr->call.args.count == 6) {
        LLVMValueRef dst = nm_arg(g, expr, 0, locals);
        LLVMValueRef src = nm_arg(g, expr, 1, locals);
        LLVMValueRef size = nm_arg(g, expr, 2, locals);
        LLVMValueRef key = nm_arg(g, expr, 3, locals);
        LLVMValueRef keybits = nm_arg(g, expr, 4, locals);
        LLVMValueRef iv = nm_arg(g, expr, 5, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i64t, i8ptr, i32t, i8ptr, i8ptr }, 6, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_aes_cbc_encrypt");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_aes_cbc_encrypt", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, src, zero64), size, nm_addr(g, key, zero64),
            coerce_int_to(g, keybits, i32t), nm_addr(g, iv, zero64),
            nm_addr(g, dst, zero64) }, 6, "nm.aes_enc");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "AesCbcDecrypt") && expr->call.args.count == 6) {
        LLVMValueRef dst = nm_arg(g, expr, 0, locals);
        LLVMValueRef src = nm_arg(g, expr, 1, locals);
        LLVMValueRef size = nm_arg(g, expr, 2, locals);
        LLVMValueRef key = nm_arg(g, expr, 3, locals);
        LLVMValueRef keybits = nm_arg(g, expr, 4, locals);
        LLVMValueRef iv = nm_arg(g, expr, 5, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i64t, i8ptr, i32t, i8ptr, i8ptr }, 6, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_aes_cbc_decrypt");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_aes_cbc_decrypt", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, src, zero64), size, nm_addr(g, key, zero64),
            coerce_int_to(g, keybits, i32t), nm_addr(g, iv, zero64),
            nm_addr(g, dst, zero64) }, 6, "nm.aes_dec");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "AesEcbBlock") && expr->call.args.count == 4) {
        LLVMValueRef key = nm_arg(g, expr, 0, locals);
        LLVMValueRef keybits = nm_arg(g, expr, 1, locals);
        LLVMValueRef in16 = nm_arg(g, expr, 2, locals);
        LLVMValueRef out16 = nm_arg(g, expr, 3, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i32t, i8ptr, i8ptr }, 4, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_aes_ecb_block");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_aes_ecb_block", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, key, zero64), coerce_int_to(g, keybits, i32t),
            nm_addr(g, in16, zero64), nm_addr(g, out16, zero64) }, 4, "nm.aes_ecb");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "AesCtrCrypt") && expr->call.args.count == 6) {
        LLVMValueRef dst = nm_arg(g, expr, 0, locals);
        LLVMValueRef src = nm_arg(g, expr, 1, locals);
        LLVMValueRef size = coerce_int_to(g, nm_arg(g, expr, 2, locals), i64t);
        LLVMValueRef key = nm_arg(g, expr, 3, locals);
        LLVMValueRef keybits = coerce_int_to(g, nm_arg(g, expr, 4, locals), i32t);
        LLVMValueRef counter = nm_arg(g, expr, 5, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i64t, i8ptr, i32t, i8ptr, i8ptr }, 6, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_aes_ctr_crypt");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_aes_ctr_crypt", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, src, zero64), size, nm_addr(g, key, zero64),
            keybits, nm_addr(g, counter, zero64),
            nm_addr(g, dst, zero64) }, 6, "nm.aes_ctr");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "AesGcmEncrypt") && expr->call.args.count == 9) {
        LLVMValueRef key = nm_arg(g, expr, 0, locals);
        LLVMValueRef keybits = coerce_int_to(g, nm_arg(g, expr, 1, locals), i32t);
        LLVMValueRef iv = nm_arg(g, expr, 2, locals);
        LLVMValueRef aad = nm_arg(g, expr, 3, locals);
        LLVMValueRef aadLen = coerce_int_to(g, nm_arg(g, expr, 4, locals), i64t);
        LLVMValueRef inBuf = nm_arg(g, expr, 5, locals);
        LLVMValueRef inLen = coerce_int_to(g, nm_arg(g, expr, 6, locals), i64t);
        LLVMValueRef outBuf = nm_arg(g, expr, 7, locals);
        LLVMValueRef tag16 = nm_arg(g, expr, 8, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i32t, i8ptr, i8ptr, i64t, i8ptr, i64t, i8ptr, i8ptr }, 9, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_aes_gcm_encrypt");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_aes_gcm_encrypt", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, key, zero64), keybits,
            nm_addr(g, iv, zero64),
            nm_addr(g, aad, zero64), aadLen,
            nm_addr(g, inBuf, zero64), inLen,
            nm_addr(g, outBuf, zero64),
            nm_addr(g, tag16, zero64) }, 9, "nm.aes_gcm_enc");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "AesGcmDecrypt") && expr->call.args.count == 9) {
        LLVMValueRef key = nm_arg(g, expr, 0, locals);
        LLVMValueRef keybits = coerce_int_to(g, nm_arg(g, expr, 1, locals), i32t);
        LLVMValueRef iv = nm_arg(g, expr, 2, locals);
        LLVMValueRef aad = nm_arg(g, expr, 3, locals);
        LLVMValueRef aadLen = coerce_int_to(g, nm_arg(g, expr, 4, locals), i64t);
        LLVMValueRef inBuf = nm_arg(g, expr, 5, locals);
        LLVMValueRef inLen = coerce_int_to(g, nm_arg(g, expr, 6, locals), i64t);
        LLVMValueRef tag16 = nm_arg(g, expr, 7, locals);
        LLVMValueRef outBuf = nm_arg(g, expr, 8, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i32t, i8ptr, i8ptr, i64t, i8ptr, i64t, i8ptr, i8ptr }, 9, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_aes_gcm_decrypt");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_aes_gcm_decrypt", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, key, zero64), keybits,
            nm_addr(g, iv, zero64),
            nm_addr(g, aad, zero64), aadLen,
            nm_addr(g, inBuf, zero64), inLen,
            nm_addr(g, tag16, zero64),
            nm_addr(g, outBuf, zero64) }, 9, "nm.aes_gcm_dec");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "AesGcmInit") && expr->call.args.count == 4) {
        LLVMValueRef ctxBuf = nm_arg(g, expr, 0, locals);
        LLVMValueRef ctxLen = coerce_int_to(g, nm_arg(g, expr, 1, locals), i64t);
        LLVMValueRef key = nm_arg(g, expr, 2, locals);
        LLVMValueRef keybits = coerce_int_to(g, nm_arg(g, expr, 3, locals), i32t);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i64t, i8ptr, i32t }, 4, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_aes_gcm_init");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_aes_gcm_init", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, ctxBuf, zero64), ctxLen,
            nm_addr(g, key, zero64), keybits }, 4, "nm.aes_gcm_init");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "AesGcmEncryptCtx") && expr->call.args.count == 8) {
        LLVMValueRef ctxBuf = nm_arg(g, expr, 0, locals);
        LLVMValueRef iv = nm_arg(g, expr, 1, locals);
        LLVMValueRef aad = nm_arg(g, expr, 2, locals);
        LLVMValueRef aadLen = coerce_int_to(g, nm_arg(g, expr, 3, locals), i64t);
        LLVMValueRef inBuf = nm_arg(g, expr, 4, locals);
        LLVMValueRef inLen = coerce_int_to(g, nm_arg(g, expr, 5, locals), i64t);
        LLVMValueRef outBuf = nm_arg(g, expr, 6, locals);
        LLVMValueRef tag16 = nm_arg(g, expr, 7, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr, i64t, i8ptr, i64t, i8ptr, i8ptr }, 8, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_aes_gcm_encrypt_ctx");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_aes_gcm_encrypt_ctx", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, ctxBuf, zero64),
            nm_addr(g, iv, zero64),
            nm_addr(g, aad, zero64), aadLen,
            nm_addr(g, inBuf, zero64), inLen,
            nm_addr(g, outBuf, zero64),
            nm_addr(g, tag16, zero64) }, 8, "nm.aes_gcm_enc_ctx");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "AesGcmDecryptCtx") && expr->call.args.count == 8) {
        LLVMValueRef ctxBuf = nm_arg(g, expr, 0, locals);
        LLVMValueRef iv = nm_arg(g, expr, 1, locals);
        LLVMValueRef aad = nm_arg(g, expr, 2, locals);
        LLVMValueRef aadLen = coerce_int_to(g, nm_arg(g, expr, 3, locals), i64t);
        LLVMValueRef inBuf = nm_arg(g, expr, 4, locals);
        LLVMValueRef inLen = coerce_int_to(g, nm_arg(g, expr, 5, locals), i64t);
        LLVMValueRef tag16 = nm_arg(g, expr, 6, locals);
        LLVMValueRef outBuf = nm_arg(g, expr, 7, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr, i64t, i8ptr, i64t, i8ptr, i8ptr }, 8, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_aes_gcm_decrypt_ctx");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_aes_gcm_decrypt_ctx", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, ctxBuf, zero64),
            nm_addr(g, iv, zero64),
            nm_addr(g, aad, zero64), aadLen,
            nm_addr(g, inBuf, zero64), inLen,
            nm_addr(g, tag16, zero64),
            nm_addr(g, outBuf, zero64) }, 8, "nm.aes_gcm_dec_ctx");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "GhashBlock") && expr->call.args.count == 3) {
        LLVMValueRef h = nm_arg(g, expr, 0, locals);
        LLVMValueRef x = nm_arg(g, expr, 1, locals);
        LLVMValueRef y = nm_arg(g, expr, 2, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr }, 3, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_ghash_block");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_ghash_block", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, h, zero64), nm_addr(g, x, zero64),
            nm_addr(g, y, zero64) }, 3, "nm.ghash");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "GhashUpdate") && expr->call.args.count == 4) {
        LLVMValueRef h = nm_arg(g, expr, 0, locals);
        LLVMValueRef data = nm_arg(g, expr, 1, locals);
        LLVMValueRef len = coerce_int_to(g, nm_arg(g, expr, 2, locals), i64t);
        LLVMValueRef y = nm_arg(g, expr, 3, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i8ptr, i64t, i8ptr }, 4, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_ghash_update");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_ghash_update", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, h, zero64), nm_addr(g, data, zero64), len,
            nm_addr(g, y, zero64) }, 4, "nm.ghash_upd");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "RsaModPow") && expr->call.args.count == 7) {
        LLVMValueRef base = nm_arg(g, expr, 0, locals);
        LLVMValueRef bLen = coerce_int_to(g, nm_arg(g, expr, 1, locals), i64t);
        LLVMValueRef expVal = nm_arg(g, expr, 2, locals);
        LLVMValueRef eLen = coerce_int_to(g, nm_arg(g, expr, 3, locals), i64t);
        LLVMValueRef mod = nm_arg(g, expr, 4, locals);
        LLVMValueRef mLen = coerce_int_to(g, nm_arg(g, expr, 5, locals), i64t);
        LLVMValueRef outBuf = nm_arg(g, expr, 6, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i64t, i8ptr, i64t, i8ptr, i64t, i8ptr }, 7, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_rsa_mod_pow");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_rsa_mod_pow", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, base, zero64), bLen,
            nm_addr(g, expVal, zero64), eLen,
            nm_addr(g, mod, zero64), mLen,
            nm_addr(g, outBuf, zero64) }, 7, "nm.rsa_mod_pow");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "RsaCrtModPow") && expr->call.args.count == 14) {
        LLVMValueRef msg = nm_arg(g, expr, 0, locals);
        LLVMValueRef mLen = coerce_int_to(g, nm_arg(g, expr, 1, locals), i64t);
        LLVMValueRef p = nm_arg(g, expr, 2, locals);
        LLVMValueRef pLen = coerce_int_to(g, nm_arg(g, expr, 3, locals), i64t);
        LLVMValueRef q = nm_arg(g, expr, 4, locals);
        LLVMValueRef qLen = coerce_int_to(g, nm_arg(g, expr, 5, locals), i64t);
        LLVMValueRef dp = nm_arg(g, expr, 6, locals);
        LLVMValueRef dpLen = coerce_int_to(g, nm_arg(g, expr, 7, locals), i64t);
        LLVMValueRef dq = nm_arg(g, expr, 8, locals);
        LLVMValueRef dqLen = coerce_int_to(g, nm_arg(g, expr, 9, locals), i64t);
        LLVMValueRef qinv = nm_arg(g, expr, 10, locals);
        LLVMValueRef qinvLen = coerce_int_to(g, nm_arg(g, expr, 11, locals), i64t);
        LLVMValueRef outBuf = nm_arg(g, expr, 12, locals);
        LLVMValueRef outLen = coerce_int_to(g, nm_arg(g, expr, 13, locals), i64t);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i64t, i8ptr, i64t, i8ptr, i64t, i8ptr, i64t, i8ptr, i64t, i8ptr, i64t, i8ptr, i64t }, 14, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_rsa_crt");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_rsa_crt", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, msg, zero64), mLen,
            nm_addr(g, p, zero64), pLen,
            nm_addr(g, q, zero64), qLen,
            nm_addr(g, dp, zero64), dpLen,
            nm_addr(g, dq, zero64), dqLen,
            nm_addr(g, qinv, zero64), qinvLen,
            nm_addr(g, outBuf, zero64), outLen }, 14, "nm.rsa_crt");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "X25519") && expr->call.args.count == 3) {
        LLVMValueRef scalar = nm_arg(g, expr, 0, locals);
        LLVMValueRef point = nm_arg(g, expr, 1, locals);
        LLVMValueRef out_buf = nm_arg(g, expr, 2, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr }, 3, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_x25519");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_x25519", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, scalar, zero64), nm_addr(g, point, zero64),
            nm_addr(g, out_buf, zero64) }, 3, "nm.x25519");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Crc32CUpdate") && expr->call.args.count == 3) {
        LLVMValueRef crc = nm_arg(g, expr, 0, locals);
        LLVMValueRef p = nm_arg(g, expr, 1, locals);
        LLVMValueRef len = nm_arg(g, expr, 2, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i32t, i8ptr, i64t }, 3, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_crc32c_update");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_crc32c_update", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            coerce_int_to(g, crc, i32t), nm_addr(g, p, zero64), len }, 3, "nm.crc32c.u");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Sha1") && expr->call.args.count == 2) {
        emit_nm_digest(g, expr, locals, out, "zan_hw_sha1", 20);
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Sha512") && expr->call.args.count == 2) {
        emit_nm_digest(g, expr, locals, out, "zan_hw_sha512", 64);
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Sm3") && expr->call.args.count == 2) {
        emit_nm_digest(g, expr, locals, out, "zan_hw_sm3", 32);
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Sm4CbcEncrypt") && expr->call.args.count == 5) {
        LLVMValueRef dst = nm_arg(g, expr, 0, locals);
        LLVMValueRef src = nm_arg(g, expr, 1, locals);
        LLVMValueRef size = nm_arg(g, expr, 2, locals);
        LLVMValueRef key = nm_arg(g, expr, 3, locals);
        LLVMValueRef iv = nm_arg(g, expr, 4, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i64t, i8ptr, i8ptr, i8ptr }, 5, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_sm4_cbc_encrypt");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_sm4_cbc_encrypt", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, src, zero64), size, nm_addr(g, key, zero64), nm_addr(g, iv, zero64), nm_addr(g, dst, zero64)
        }, 5, "nm.sm4_enc");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Sm4CbcDecrypt") && expr->call.args.count == 5) {
        LLVMValueRef dst = nm_arg(g, expr, 0, locals);
        LLVMValueRef src = nm_arg(g, expr, 1, locals);
        LLVMValueRef size = nm_arg(g, expr, 2, locals);
        LLVMValueRef key = nm_arg(g, expr, 3, locals);
        LLVMValueRef iv = nm_arg(g, expr, 4, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i64t, i8ptr, i8ptr, i8ptr }, 5, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_sm4_cbc_decrypt");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_sm4_cbc_decrypt", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, src, zero64), size, nm_addr(g, key, zero64), nm_addr(g, iv, zero64), nm_addr(g, dst, zero64)
        }, 5, "nm.sm4_dec");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Base64Encode") && expr->call.args.count == 2) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef len = nm_arg(g, expr, 1, locals);
        len = LLVMBuildSelect(g->builder,
            zan_icmp(g->builder, LLVMIntSLT, len, zero64, "nm.b64e.neg"),
            zero64, len, "nm.b64e.len");
        LLVMValueRef plus2 = LLVMBuildAdd(g->builder, len, LLVMConstInt(i64t, 2, 0), "nm.b64e.p2");
        LLVMValueRef div3 = LLVMBuildSDiv(g->builder, plus2, LLVMConstInt(i64t, 3, 0), "nm.b64e.d3");
        LLVMValueRef mul4 = LLVMBuildMul(g->builder, div3, LLVMConstInt(i64t, 4, 0), "nm.b64e.m4");
        LLVMValueRef cap = LLVMBuildAdd(g->builder, mul4, LLVMConstInt(i64t, 1, 0), "nm.b64e.cap");
        LLVMValueRef s = emit_string_alloc_rc(g, cap);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i64t, i8ptr }, 3, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_base64_encode");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_base64_encode", ty);
        LLVMValueRef written = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, p, zero64), len, s
        }, 3, "nm.b64e.w");
        emit_string_len_set(g, s, written);
        *out = s;
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Base64Decode") && expr->call.args.count == 3) {
        LLVMValueRef dst = nm_arg(g, expr, 0, locals);
        LLVMValueRef src = nm_arg(g, expr, 1, locals);
        LLVMValueRef size = nm_arg(g, expr, 2, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i64t, i8ptr }, 3, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_base64_decode");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_base64_decode", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, src, zero64), size, nm_addr(g, dst, zero64)
        }, 3, "nm.b64d.w");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "JsonSkipWhitespace") && expr->call.args.count == 3) {
        LLVMValueRef ptr = nm_arg(g, expr, 0, locals);
        LLVMValueRef pos = nm_arg(g, expr, 1, locals);
        LLVMValueRef len = nm_arg(g, expr, 2, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i64t, i64t }, 3, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_json_skip_whitespace");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_json_skip_whitespace", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, ptr, zero64), pos, len
        }, 3, "nm.json_ws");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "JsonScanString") && expr->call.args.count == 3) {
        LLVMValueRef ptr = nm_arg(g, expr, 0, locals);
        LLVMValueRef pos = nm_arg(g, expr, 1, locals);
        LLVMValueRef len = nm_arg(g, expr, 2, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i64t, i64t }, 3, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_json_scan_string");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_json_scan_string", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, ptr, zero64), pos, len
        }, 3, "nm.json_str");
        return true;
    }
    return false;
}

/* PixelOps 图形像素栅格化原语：双线性插值、Alpha 混合与颜色格式转换 */

static LLVMValueRef pixel_blend_over_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_pixel_blend_over");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(void_ty,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    return LLVMAddFunction(g->mod, "zan_hw_pixel_blend_over", fn_ty);
}

static LLVMValueRef pixel_swap_rb_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_pixel_swap_rb");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(void_ty,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    return LLVMAddFunction(g->mod, "zan_hw_pixel_swap_rb", fn_ty);
}

static LLVMValueRef pixel_fill_rect_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_pixel_fill_rect");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(void_ty,
        (LLVMTypeRef[]){ i8ptr, i64t, i64t, i64t, i64t, i64t, i32t }, 7, 0);
    return LLVMAddFunction(g->mod, "zan_hw_pixel_fill_rect", fn_ty);
}

static LLVMValueRef pixel_resample_bilinear_row_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_pixel_resample_bilinear_row");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(void_ty,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr, i8ptr, i8ptr, i32t, i64t }, 7, 0);
    return LLVMAddFunction(g->mod, "zan_hw_pixel_resample_bilinear_row", fn_ty);
}

static bool emit_pixel_ops_call(zan_irgen_t *g, zan_ast_node_t *expr,
                                local_scope_t *locals, LLVMValueRef *out) {
    if (expr->kind != AST_CALL) return false;
    zan_ast_node_t *callee = expr->call.callee;
    if (callee->kind != AST_MEMBER_ACCESS) return false;

    zan_ast_node_t *obj = callee->member.object;
    bool is_pixelops = false;
    if (obj->kind == AST_IDENTIFIER) {
        if (obj->ident.name.len == 8 && memcmp(obj->ident.name.str, "PixelOps", 8) == 0)
            is_pixelops = true;
    } else if (obj->kind == AST_MEMBER_ACCESS) {
        if (obj->member.name.len == 8 && memcmp(obj->member.name.str, "PixelOps", 8) == 0)
            is_pixelops = true;
    }
    if (!is_pixelops) return false;

    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(g->ctx);
    LLVMValueRef zero64 = LLVMConstInt(i64t, 0, 0);

    zan_istr_t method = callee->member.name;

    if (method.len == 9 && memcmp(method.str, "BlendOver", 9) == 0 && expr->call.args.count == 3) {
        LLVMValueRef dst = nm_arg(g, expr, 0, locals);
        LLVMValueRef src = nm_arg(g, expr, 1, locals);
        LLVMValueRef count = nm_arg(g, expr, 2, locals);
        LLVMValueRef fn = pixel_blend_over_fn(g);
        LLVMTypeRef fn_ty = LLVMFunctionType(void_ty, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
        zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){
            nm_addr(g, dst, zero64), nm_addr(g, src, zero64), count }, 3, "");
        *out = zero64;
        return true;
    }

    if (method.len == 6 && memcmp(method.str, "SwapRB", 6) == 0 && expr->call.args.count == 3) {
        LLVMValueRef dst = nm_arg(g, expr, 0, locals);
        LLVMValueRef src = nm_arg(g, expr, 1, locals);
        LLVMValueRef count = nm_arg(g, expr, 2, locals);
        LLVMValueRef fn = pixel_swap_rb_fn(g);
        LLVMTypeRef fn_ty = LLVMFunctionType(void_ty, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
        zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){
            nm_addr(g, dst, zero64), nm_addr(g, src, zero64), count }, 3, "");
        *out = zero64;
        return true;
    }

    if (method.len == 8 && memcmp(method.str, "FillRect", 8) == 0 && expr->call.args.count == 7) {
        LLVMValueRef dst = nm_arg(g, expr, 0, locals);
        LLVMValueRef dst_stride = nm_arg(g, expr, 1, locals);
        LLVMValueRef x = nm_arg(g, expr, 2, locals);
        LLVMValueRef y = nm_arg(g, expr, 3, locals);
        LLVMValueRef w = nm_arg(g, expr, 4, locals);
        LLVMValueRef h = nm_arg(g, expr, 5, locals);
        LLVMValueRef color = coerce_int_to(g, emit_expr(g, expr->call.args.items[6], locals), i32t);
        LLVMValueRef fn = pixel_fill_rect_fn(g);
        LLVMTypeRef fn_ty = LLVMFunctionType(void_ty,
            (LLVMTypeRef[]){ i8ptr, i64t, i64t, i64t, i64t, i64t, i32t }, 7, 0);
        zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){
            nm_addr(g, dst, zero64), dst_stride, x, y, w, h, color }, 7, "");
        *out = zero64;
        return true;
    }

    if (method.len == 19 && memcmp(method.str, "ResampleBilinearRow", 19) == 0 && expr->call.args.count == 7) {
        LLVMValueRef dst = nm_arg(g, expr, 0, locals);
        LLVMValueRef src0 = nm_arg(g, expr, 1, locals);
        LLVMValueRef src1 = nm_arg(g, expr, 2, locals);
        LLVMValueRef x_idx = nm_arg(g, expr, 3, locals);
        LLVMValueRef x_wt = nm_arg(g, expr, 4, locals);
        LLVMValueRef wy = coerce_int_to(g, emit_expr(g, expr->call.args.items[5], locals), i32t);
        LLVMValueRef width = nm_arg(g, expr, 6, locals);
        LLVMValueRef fn = pixel_resample_bilinear_row_fn(g);
        LLVMTypeRef fn_ty = LLVMFunctionType(void_ty,
            (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr, i8ptr, i8ptr, i32t, i64t }, 7, 0);
        zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){
            nm_addr(g, dst, zero64), nm_addr(g, src0, zero64), nm_addr(g, src1, zero64),
            nm_addr(g, x_idx, zero64), nm_addr(g, x_wt, zero64), wy, width }, 7, "");
        *out = zero64;
        return true;
    }

    return false;
}

/* __zan_cpu_feature(i32 id): 底层 CPUID 特性探测接口 */
static LLVMValueRef cpu_feature_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_cpu_feature");
    if (fn) return fn;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t }, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_cpu_feature", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef id = LLVMGetParam(fn, 0);

    bool is_arm = (strstr(g->target_triple, "aarch64") != NULL ||
                   strstr(g->target_triple, "arm64") != NULL);
    bool is_x86 = (strstr(g->target_triple, "x86") != NULL ||
                   strstr(g->target_triple, "amd64") != NULL);

    if (is_arm) {
        LLVMValueRef is_neon = zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 6, 0), "is_neon");
        LLVMValueRef is_pop = zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 1, 0), "is_pop");
        LLVMValueRef is_lz = zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 2, 0), "is_lz");
        LLVMValueRef ok = zan_or(g->builder, is_neon, zan_or(g->builder, is_pop, is_lz, "pop_or_lz"), "ok");
        LLVMBuildRet(g->builder, LLVMBuildZExt(g->builder, ok, i32t, "res"));
    } else if (is_x86) {
        LLVMBasicBlockRef b_neon = LLVMAppendBasicBlockInContext(g->ctx, fn, "b_neon");
        LLVMBasicBlockRef b_cpuid = LLVMAppendBasicBlockInContext(g->ctx, fn, "b_cpuid");

        LLVMValueRef is_neon = zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 6, 0), "is_neon");
        LLVMBuildCondBr(g->builder, is_neon, b_neon, b_cpuid);

        LLVMPositionBuilderAtEnd(g->builder, b_neon);
        LLVMBuildRet(g->builder, LLVMConstInt(i32t, 0, 0));

        LLVMPositionBuilderAtEnd(g->builder, b_cpuid);
        LLVMValueRef is_id4 = zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 4, 0), "is_avx2");
        LLVMValueRef is_id2 = zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 2, 0), "is_lzcnt");

        LLVMValueRef leaf = LLVMBuildSelect(g->builder, is_id4,
            LLVMConstInt(i32t, 7, 0),
            LLVMBuildSelect(g->builder, is_id2,
                LLVMConstInt(i32t, 0x80000001u, 0),
                LLVMConstInt(i32t, 1, 0), "leaf2"), "leaf");
        LLVMValueRef subleaf = LLVMConstInt(i32t, 0, 0);

        LLVMTypeRef cpuid_ret_ty = LLVMStructTypeInContext(g->ctx,
            (LLVMTypeRef[]){ i32t, i32t, i32t, i32t }, 4, 0);
        LLVMTypeRef cpuid_fn_ty = LLVMFunctionType(cpuid_ret_ty,
            (LLVMTypeRef[]){ i32t, i32t }, 2, 0);
        LLVMValueRef cpuid_asm = LLVMGetInlineAsm(cpuid_fn_ty,
            "cpuid", 5,
            "={ax},={bx},={cx},={dx},{ax},{cx}", 34,
            1, 0, LLVMInlineAsmDialectATT, 0);

        LLVMValueRef regs = LLVMBuildCall2(g->builder, cpuid_fn_ty, cpuid_asm,
            (LLVMValueRef[]){ leaf, subleaf }, 2, "cpuid_res");
        LLVMValueRef ebx = LLVMBuildExtractValue(g->builder, regs, 1, "ebx");
        LLVMValueRef ecx = LLVMBuildExtractValue(g->builder, regs, 2, "ecx");

        LLVMValueRef is_id1 = zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 1, 0), "is_id1");
        LLVMValueRef is_id3 = zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 3, 0), "is_id3");
        LLVMValueRef is_id5 = zan_icmp(g->builder, LLVMIntEQ, id, LLVMConstInt(i32t, 5, 0), "is_id5");

        LLVMValueRef shift = LLVMBuildSelect(g->builder, is_id1, LLVMConstInt(i32t, 23, 0),
            LLVMBuildSelect(g->builder, is_id2, LLVMConstInt(i32t, 5, 0),
            LLVMBuildSelect(g->builder, is_id3, LLVMConstInt(i32t, 20, 0),
            LLVMBuildSelect(g->builder, is_id4, LLVMConstInt(i32t, 5, 0),
            LLVMBuildSelect(g->builder, is_id5, LLVMConstInt(i32t, 25, 0),
                LLVMConstInt(i32t, 0, 0), "sh5"), "sh4"), "sh3"), "sh2"), "shift");

        LLVMValueRef reg = LLVMBuildSelect(g->builder, is_id4, ebx, ecx, "reg");
        LLVMValueRef bit = zan_and(g->builder, zan_lshr(g->builder, reg, shift, "s"), LLVMConstInt(i32t, 1, 0), "bit");
        LLVMBuildRet(g->builder, bit);
    } else {
        /* 非 x86 目标平台 CPU 特性回退：默认报告不支持该专属特性 */
        LLVMBuildRet(g->builder, LLVMConstInt(i32t, 0, 0));
    }

    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

static bool emit_cpu_call(zan_irgen_t *g, zan_ast_node_t *expr,
                          local_scope_t *locals, LLVMValueRef *out) {
    if (expr->kind != AST_CALL) return false;
    zan_ast_node_t *callee = expr->call.callee;
    if (callee->kind != AST_MEMBER_ACCESS) return false;

    zan_ast_node_t *obj = callee->member.object;
    bool is_cpu = false;
    if (obj->kind == AST_IDENTIFIER) {
        if (obj->ident.name.len == 3 && memcmp(obj->ident.name.str, "Cpu", 3) == 0)
            is_cpu = true;
    } else if (obj->kind == AST_MEMBER_ACCESS) {
        if (obj->member.name.len == 3 && memcmp(obj->member.name.str, "Cpu", 3) == 0)
            is_cpu = true;
    }
    if (!is_cpu) return false;

    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    zan_istr_t method = callee->member.name;

    if (method.len == 10 && memcmp(method.str, "CpuFeature", 10) == 0 && expr->call.args.count == 1) {
        LLVMValueRef id = coerce_int_to(g, emit_expr(g, expr->call.args.items[0], locals), i32t);
        LLVMValueRef fn = cpu_feature_fn(g);
        LLVMTypeRef fnty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t }, 1, 0);
        *out = zan_call2(g->builder, fnty, fn, &id, 1, "cpu_feat");
        return true;
    }

    int feat_id = 0;
    if (method.len == 9 && memcmp(method.str, "HasPopcnt", 9) == 0) feat_id = 1;
    else if (method.len == 8 && memcmp(method.str, "HasLzcnt", 8) == 0) feat_id = 2;
    else if (method.len == 8 && memcmp(method.str, "HasSse42", 8) == 0) feat_id = 3;
    else if (method.len == 7 && memcmp(method.str, "HasAvx2", 7) == 0) feat_id = 4;
    else if (method.len == 8 && memcmp(method.str, "HasAesNi", 8) == 0) feat_id = 5;
    else if (method.len == 7 && memcmp(method.str, "HasNeon", 7) == 0) feat_id = 6;

    if (feat_id > 0) {
        LLVMValueRef id = LLVMConstInt(i32t, feat_id, 0);
        LLVMValueRef fn = cpu_feature_fn(g);
        LLVMTypeRef fnty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t }, 1, 0);
        LLVMValueRef val = zan_call2(g->builder, fnty, fn, &id, 1, "cpu_feat");
        *out = zan_icmp(g->builder, LLVMIntNE, val, LLVMConstInt(i32t, 0, 0), "has_feat");
        return true;
    }
    return false;
}

static bool is_64bit_int_arg(zan_irgen_t *g, zan_ast_node_t *arg, LLVMValueRef v, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, arg, locals);
    if (t) {
        return (t->kind == TYPE_LONG || t->kind == TYPE_ULONG);
    }
    return (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(LLVMTypeOf(v)) == 64);
}

static bool emit_bit_operations_call(zan_irgen_t *g, zan_ast_node_t *expr,
                                     local_scope_t *locals, LLVMValueRef *out) {
    if (expr->kind != AST_CALL) return false;
    zan_ast_node_t *callee = expr->call.callee;
    if (callee->kind != AST_MEMBER_ACCESS) return false;

    zan_ast_node_t *obj = callee->member.object;
    bool is_bitops = false;
    if (obj->kind == AST_IDENTIFIER) {
        if (obj->ident.name.len == 13 && memcmp(obj->ident.name.str, "BitOperations", 13) == 0)
            is_bitops = true;
    } else if (obj->kind == AST_MEMBER_ACCESS) {
        if (obj->member.name.len == 13 && memcmp(obj->member.name.str, "BitOperations", 13) == 0)
            is_bitops = true;
    }
    if (!is_bitops) return false;

    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i1t = LLVMInt1TypeInContext(g->ctx);
    zan_istr_t method = callee->member.name;

    if (method.len == 8 && memcmp(method.str, "PopCount", 8) == 0 && expr->call.args.count == 1) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        if (is64) {
            v = coerce_int_to(g, v, i64t);
            LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i64t }, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctpop.i64");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.ctpop.i64", fty);
            LLVMValueRef res = zan_call2(g->builder, fty, fn, &v, 1, "popcnt");
            *out = LLVMBuildTrunc(g->builder, res, i32t, "popcnt32");
        } else {
            v = coerce_int_to(g, v, i32t);
            LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t }, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctpop.i32");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.ctpop.i32", fty);
            *out = zan_call2(g->builder, fty, fn, &v, 1, "popcnt");
        }
        return true;
    }

    if (method.len == 16 && memcmp(method.str, "LeadingZeroCount", 16) == 0 && expr->call.args.count == 1) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        if (is64) {
            v = coerce_int_to(g, v, i64t);
            LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i64t, i1t }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i64");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.ctlz.i64", fty);
            LLVMValueRef args[] = { v, LLVMConstInt(i1t, 0, 0) };
            LLVMValueRef res = zan_call2(g->builder, fty, fn, args, 2, "ctlz");
            *out = LLVMBuildTrunc(g->builder, res, i32t, "ctlz32");
        } else {
            v = coerce_int_to(g, v, i32t);
            LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t, i1t }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i32");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.ctlz.i32", fty);
            LLVMValueRef args[] = { v, LLVMConstInt(i1t, 0, 0) };
            *out = zan_call2(g->builder, fty, fn, args, 2, "ctlz");
        }
        return true;
    }

    if (method.len == 17 && memcmp(method.str, "TrailingZeroCount", 17) == 0 && expr->call.args.count == 1) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        if (is64) {
            v = coerce_int_to(g, v, i64t);
            LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i64t, i1t }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.cttz.i64");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.cttz.i64", fty);
            LLVMValueRef args[] = { v, LLVMConstInt(i1t, 0, 0) };
            LLVMValueRef res = zan_call2(g->builder, fty, fn, args, 2, "cttz");
            *out = LLVMBuildTrunc(g->builder, res, i32t, "cttz32");
        } else {
            v = coerce_int_to(g, v, i32t);
            LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t, i1t }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.cttz.i32");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.cttz.i32", fty);
            LLVMValueRef args[] = { v, LLVMConstInt(i1t, 0, 0) };
            *out = zan_call2(g->builder, fty, fn, args, 2, "cttz");
        }
        return true;
    }

    if (method.len == 10 && memcmp(method.str, "RotateLeft", 10) == 0 && expr->call.args.count == 2) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        LLVMValueRef off = emit_expr(g, expr->call.args.items[1], locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        if (is64) {
            v = coerce_int_to(g, v, i64t);
            off = coerce_int_to(g, off, i64t);
            LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i64t, i64t, i64t }, 3, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.fshl.i64");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.fshl.i64", fty);
            LLVMValueRef args[] = { v, v, off };
            *out = zan_call2(g->builder, fty, fn, args, 3, "rotl");
        } else {
            v = coerce_int_to(g, v, i32t);
            off = coerce_int_to(g, off, i32t);
            LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t, i32t, i32t }, 3, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.fshl.i32");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.fshl.i32", fty);
            LLVMValueRef args[] = { v, v, off };
            *out = zan_call2(g->builder, fty, fn, args, 3, "rotl");
        }
        return true;
    }

    if (method.len == 11 && memcmp(method.str, "RotateRight", 11) == 0 && expr->call.args.count == 2) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        LLVMValueRef off = emit_expr(g, expr->call.args.items[1], locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        if (is64) {
            v = coerce_int_to(g, v, i64t);
            off = coerce_int_to(g, off, i64t);
            LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i64t, i64t, i64t }, 3, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.fshr.i64");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.fshr.i64", fty);
            LLVMValueRef args[] = { v, v, off };
            *out = zan_call2(g->builder, fty, fn, args, 3, "rotr");
        } else {
            v = coerce_int_to(g, v, i32t);
            off = coerce_int_to(g, off, i32t);
            LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t, i32t, i32t }, 3, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.fshr.i32");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.fshr.i32", fty);
            LLVMValueRef args[] = { v, v, off };
            *out = zan_call2(g->builder, fty, fn, args, 3, "rotr");
        }
        return true;
    }

    if (method.len == 17 && memcmp(method.str, "ReverseEndianness", 17) == 0 && expr->call.args.count == 1) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        if (is64) {
            v = coerce_int_to(g, v, i64t);
            LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i64t }, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.bswap.i64");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.bswap.i64", fty);
            *out = zan_call2(g->builder, fty, fn, &v, 1, "bswap");
        } else {
            v = coerce_int_to(g, v, i32t);
            LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t }, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.bswap.i32");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.bswap.i32", fty);
            *out = zan_call2(g->builder, fty, fn, &v, 1, "bswap");
        }
        return true;
    }

    if (method.len == 4 && memcmp(method.str, "Log2", 4) == 0 && expr->call.args.count == 1) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        if (is64) {
            v = coerce_int_to(g, v, i64t);
            LLVMValueRef vor = zan_or(g->builder, v, LLVMConstInt(i64t, 1, 0), "vor");
            LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i64t, i1t }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i64");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.ctlz.i64", fty);
            LLVMValueRef args[] = { vor, LLVMConstInt(i1t, 0, 0) };
            LLVMValueRef clz = zan_call2(g->builder, fty, fn, args, 2, "ctlz");
            LLVMValueRef sub = zan_sub(g->builder, LLVMConstInt(i64t, 63, 0), clz, "log2");
            *out = LLVMBuildTrunc(g->builder, sub, i32t, "log2_32");
        } else {
            v = coerce_int_to(g, v, i32t);
            LLVMValueRef vor = zan_or(g->builder, v, LLVMConstInt(i32t, 1, 0), "vor");
            LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t, i1t }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i32");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.ctlz.i32", fty);
            LLVMValueRef args[] = { vor, LLVMConstInt(i1t, 0, 0) };
            LLVMValueRef clz = zan_call2(g->builder, fty, fn, args, 2, "ctlz");
            *out = zan_sub(g->builder, LLVMConstInt(i32t, 31, 0), clz, "log2");
        }
        return true;
    }

    if (method.len == 6 && memcmp(method.str, "IsPow2", 6) == 0 && expr->call.args.count == 1) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        LLVMValueRef zero = is64 ? LLVMConstInt(i64t, 0, 0) : LLVMConstInt(i32t, 0, 0);
        LLVMValueRef one = is64 ? LLVMConstInt(i64t, 1, 0) : LLVMConstInt(i32t, 1, 0);
        LLVMValueRef gt0 = zan_icmp(g->builder, LLVMIntSGT, v, zero, "gt0");
        LLVMValueRef sub1 = zan_sub(g->builder, v, one, "sub1");
        LLVMValueRef andv = zan_and(g->builder, v, sub1, "andv");
        LLVMValueRef eq0 = zan_icmp(g->builder, LLVMIntEQ, andv, zero, "eq0");
        *out = zan_and(g->builder, gt0, eq0, "ispow2");
        return true;
    }

    if (method.len == 17 && memcmp(method.str, "RoundUpToPowerOf2", 17) == 0 && expr->call.args.count == 1) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        if (is64) {
            v = coerce_int_to(g, v, i64t);
            LLVMValueRef vm1 = zan_sub(g->builder, v, LLVMConstInt(i64t, 1, 0), "vm1");
            LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i64t, i1t }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i64");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.ctlz.i64", fty);
            LLVMValueRef args[] = { vm1, LLVMConstInt(i1t, 0, 0) };
            LLVMValueRef clz = zan_call2(g->builder, fty, fn, args, 2, "ctlz");
            LLVMValueRef sh = zan_sub(g->builder, LLVMConstInt(i64t, 64, 0), clz, "sh");
            LLVMValueRef pow2 = zan_shl(g->builder, LLVMConstInt(i64t, 1, 0), sh, "pow2");
            LLVMValueRef le1 = zan_icmp(g->builder, LLVMIntSLE, v, LLVMConstInt(i64t, 1, 0), "le1");
            *out = LLVMBuildSelect(g->builder, le1, LLVMConstInt(i64t, 1, 0), pow2, "roundup");
        } else {
            v = coerce_int_to(g, v, i32t);
            LLVMValueRef vm1 = zan_sub(g->builder, v, LLVMConstInt(i32t, 1, 0), "vm1");
            LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t, i1t }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.ctlz.i32");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.ctlz.i32", fty);
            LLVMValueRef args[] = { vm1, LLVMConstInt(i1t, 0, 0) };
            LLVMValueRef clz = zan_call2(g->builder, fty, fn, args, 2, "ctlz");
            LLVMValueRef sh = zan_sub(g->builder, LLVMConstInt(i32t, 32, 0), clz, "sh");
            LLVMValueRef pow2 = zan_shl(g->builder, LLVMConstInt(i32t, 1, 0), sh, "pow2");
            LLVMValueRef le1 = zan_icmp(g->builder, LLVMIntSLE, v, LLVMConstInt(i32t, 1, 0), "le1");
            *out = LLVMBuildSelect(g->builder, le1, LLVMConstInt(i32t, 1, 0), pow2, "roundup");
        }
        return true;
    }

    if (method.len == 17 && memcmp(method.str, "ResetLowestSetBit", 17) == 0 && expr->call.args.count == 1) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        LLVMValueRef one = is64 ? LLVMConstInt(i64t, 1, 0) : LLVMConstInt(i32t, 1, 0);
        LLVMValueRef sub1 = zan_sub(g->builder, v, one, "blsr_sub");
        *out = zan_and(g->builder, v, sub1, "blsr");
        return true;
    }

    if (method.len == 19 && memcmp(method.str, "ExtractLowestSetBit", 19) == 0 && expr->call.args.count == 1) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        LLVMValueRef v = emit_expr(g, arg0, locals);
        bool is64 = is_64bit_int_arg(g, arg0, v, locals);
        LLVMValueRef zero = is64 ? LLVMConstInt(i64t, 0, 0) : LLVMConstInt(i32t, 0, 0);
        LLVMValueRef neg = zan_sub(g->builder, zero, v, "blsi_neg");
        *out = zan_and(g->builder, v, neg, "blsi");
        return true;
    }

    return false;
}

static bool is_target_class_obj(zan_ast_node_t *obj, const char *name, size_t len, local_scope_t *locals) {
    if (!obj) return false;
    if (obj->kind == AST_IDENTIFIER) {
        if (locals && local_find(locals, obj->ident.name)) return false;
        return obj->ident.name.len == (int)len && memcmp(obj->ident.name.str, name, len) == 0;
    } else if (obj->kind == AST_MEMBER_ACCESS) {
        return obj->member.name.len == (int)len && memcmp(obj->member.name.str, name, len) == 0;
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

static LLVMValueRef v16i8_to_vec128(zan_irgen_t *g, LLVMValueRef v16) {
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
static bool emit_target_is_x86(const zan_irgen_t *g)
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

/* 跨平台 pmovmskb 实现：提取每个字节符号位组装为整型掩码 */
static LLVMValueRef emit_pmovmskb_portable(zan_irgen_t *g, LLVMValueRef v,
                                           unsigned lanes)
{
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef vt = LLVMVectorType(LLVMInt8TypeInContext(g->ctx), lanes);
    /* 符号位掩码提取逻辑 */
    LLVMValueRef signs = LLVMBuildICmp(g->builder, LLVMIntSLT, v,
        LLVMConstNull(vt), "vsigns");
    LLVMValueRef acc = LLVMConstInt(i32t, 0, 0);
    for (unsigned i = 0; i < lanes; i++) {
        LLVMValueRef bit = LLVMBuildExtractElement(g->builder, signs,
            LLVMConstInt(i32t, i, 0), "vbit");
        LLVMValueRef ext = LLVMBuildZExt(g->builder, bit, i32t, "vbitz");
        if (i == 0)
            acc = ext;
        else
            acc = LLVMBuildOr(g->builder, acc,
                LLVMBuildShl(g->builder, ext, LLVMConstInt(i32t, i, 0), "vbitsh"),
                "vbits");
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

static bool emit_vector128_call(zan_irgen_t *g, zan_ast_node_t *expr,
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

    /* Vector128.Zero / Vector128.AllBitsSet getter call */
    if (method.len == 4 && memcmp(method.str, "Zero", 4) == 0 && argc == 0) {
        *out = v16i8_to_vec128(g, LLVMConstNull(v16i8));
        return true;
    }
    if (method.len == 10 && memcmp(method.str, "AllBitsSet", 10) == 0 && argc == 0) {
        *out = v16i8_to_vec128(g, LLVMConstAllOnes(v16i8));
        return true;
    }

    /* Vector128.Load(nint address) / Load(byte[] buf) / Load(byte[] buf, int offset) / Load(float[] buf, int offset) */
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

    /* Vector128.Store(address/buf, source) / Store(buf, offset, source) */
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
                /* default int32 broadcast */
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
            LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntEQ, a16, b16, "vcmpeq");
            LLVMValueRef mask = LLVMBuildSExt(g->builder, cmp, v16i8, "vmask");
            *out = v16i8_to_vec128(g, mask);
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
            LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntSGT, a16, b16, "vcmpgt");
            *out = v16i8_to_vec128(g, LLVMBuildSExt(g->builder, cmp, v16i8, "vmask"));
            return true;
        }
        if (method.len == 8 && memcmp(method.str, "LessThan", 8) == 0) {
            LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
            LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntSLT, a16, b16, "vcmplt");
            *out = v16i8_to_vec128(g, LLVMBuildSExt(g->builder, cmp, v16i8, "vmask"));
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
            /*
             * RSQRTP is approximate (~12 bits) but keeps the codegen this
             * builtin has always emitted on x86
             */
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

    /* MultiplyAdd(a, b, c): 3 args -> llvm.fma.v4f32 */
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

    /* ConditionalSelect(mask, left, right): 3 args */
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

    /* Prefetch(nint address) / Prefetch(byte[] source, int offset) */
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

    /* ExtractMostSignificantBits(Vector128 value) / MoveMask */
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

static LLVMTypeRef get_vector256_struct_type(zan_irgen_t *g) {
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fields[4] = { i64t, i64t, i64t, i64t };
    return LLVMStructTypeInContext(g->ctx, fields, 4, 0);
}

static LLVMValueRef vec256_to_v32i8(zan_irgen_t *g, LLVMValueRef vec) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef v4i64 = LLVMVectorType(i64t, 4);
    LLVMTypeRef v32i8 = LLVMVectorType(LLVMInt8TypeInContext(g->ctx), 32);

    LLVMValueRef v0 = LLVMBuildExtractValue(g->builder, vec, 0, "v0");
    LLVMValueRef v1 = LLVMBuildExtractValue(g->builder, vec, 1, "v1");
    LLVMValueRef v2 = LLVMBuildExtractValue(g->builder, vec, 2, "v2");
    LLVMValueRef v3 = LLVMBuildExtractValue(g->builder, vec, 3, "v3");

    LLVMValueRef res = LLVMBuildInsertElement(g->builder, LLVMGetUndef(v4i64), v0, LLVMConstInt(i32t, 0, 0), "e0");
    res = LLVMBuildInsertElement(g->builder, res, v1, LLVMConstInt(i32t, 1, 0), "e1");
    res = LLVMBuildInsertElement(g->builder, res, v2, LLVMConstInt(i32t, 2, 0), "e2");
    res = LLVMBuildInsertElement(g->builder, res, v3, LLVMConstInt(i32t, 3, 0), "e3");

    return LLVMBuildBitCast(g->builder, res, v32i8, "v32");
}

static LLVMValueRef v32i8_to_vec256(zan_irgen_t *g, LLVMValueRef v32) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef v4i64 = LLVMVectorType(i64t, 4);
    LLVMValueRef v4 = LLVMBuildBitCast(g->builder, v32, v4i64, "v4i64");

    LLVMValueRef v0 = LLVMBuildExtractElement(g->builder, v4, LLVMConstInt(i32t, 0, 0), "r0");
    LLVMValueRef v1 = LLVMBuildExtractElement(g->builder, v4, LLVMConstInt(i32t, 1, 0), "r1");
    LLVMValueRef v2 = LLVMBuildExtractElement(g->builder, v4, LLVMConstInt(i32t, 2, 0), "r2");
    LLVMValueRef v3 = LLVMBuildExtractElement(g->builder, v4, LLVMConstInt(i32t, 3, 0), "r3");

    LLVMTypeRef st = get_vector256_struct_type(g);
    LLVMValueRef res = LLVMBuildInsertValue(g->builder, LLVMGetUndef(st), v0, 0, "s0");
    res = LLVMBuildInsertValue(g->builder, res, v1, 1, "s1");
    res = LLVMBuildInsertValue(g->builder, res, v2, 2, "s2");
    return LLVMBuildInsertValue(g->builder, res, v3, 3, "s3");
}

static bool emit_vector256_call(zan_irgen_t *g, zan_ast_node_t *expr,
                                local_scope_t *locals, LLVMValueRef *out) {
    if (expr->kind != AST_CALL) return false;
    zan_ast_node_t *callee = expr->call.callee;
    if (callee->kind != AST_MEMBER_ACCESS) return false;
    if (!is_target_class_obj(callee->member.object, "Vector256", 9, locals))
        return false;

    zan_istr_t method = callee->member.name;
    int argc = expr->call.args.count;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef v32i8 = LLVMVectorType(LLVMInt8TypeInContext(g->ctx), 32);

    if (method.len == 4 && memcmp(method.str, "Zero", 4) == 0 && argc == 0) {
        *out = v32i8_to_vec256(g, LLVMConstNull(v32i8));
        return true;
    }
    if (method.len == 10 && memcmp(method.str, "AllBitsSet", 10) == 0 && argc == 0) {
        *out = v32i8_to_vec256(g, LLVMConstAllOnes(v32i8));
        return true;
    }

    if (method.len == 4 && memcmp(method.str, "Load", 4) == 0 && (argc == 1 || argc == 2)) {
        LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
        LLVMValueRef addr_val = emit_expr(g, expr->call.args.items[0], locals);
        LLVMValueRef base;
        if (LLVMGetTypeKind(LLVMTypeOf(addr_val)) == LLVMPointerTypeKind) {
            base = LLVMBuildBitCast(g->builder, addr_val, LLVMPointerType(i8, 0), "vload256_base");
        } else {
            base = LLVMBuildIntToPtr(g->builder, addr_val, LLVMPointerType(i8, 0), "vload256_base");
        }
        if (argc == 2) {
            LLVMValueRef off_val = coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals), i64t);
            int esz = get_simd_array_element_size(g, expr->call.args.items[0], locals);
            if (esz > 1) {
                off_val = LLVMBuildMul(g->builder, off_val, LLVMConstInt(i64t, esz, 0), "off_bytes");
            }
            base = LLVMBuildGEP2(g->builder, i8, base, &off_val, 1, "vload256_gep");
        }
        LLVMValueRef ptr = LLVMBuildBitCast(g->builder, base, LLVMPointerType(v32i8, 0), "vload256_ptr");
        LLVMValueRef ld = LLVMBuildLoad2(g->builder, v32i8, ptr, "vload256");
        LLVMSetAlignment(ld, 1);
        *out = v32i8_to_vec256(g, ld);
        return true;
    }

    if (method.len == 5 && memcmp(method.str, "Store", 5) == 0 && (argc == 2 || argc == 3)) {
        LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
        LLVMValueRef addr_val = emit_expr(g, expr->call.args.items[0], locals);
        LLVMValueRef base;
        if (LLVMGetTypeKind(LLVMTypeOf(addr_val)) == LLVMPointerTypeKind) {
            base = LLVMBuildBitCast(g->builder, addr_val, LLVMPointerType(i8, 0), "vstore256_base");
        } else {
            base = LLVMBuildIntToPtr(g->builder, addr_val, LLVMPointerType(i8, 0), "vstore256_base");
        }
        LLVMValueRef src_val;
        if (argc == 3) {
            LLVMValueRef off_val = coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals), i64t);
            int esz = get_simd_array_element_size(g, expr->call.args.items[0], locals);
            if (esz > 1) {
                off_val = LLVMBuildMul(g->builder, off_val, LLVMConstInt(i64t, esz, 0), "off_bytes");
            }
            base = LLVMBuildGEP2(g->builder, i8, base, &off_val, 1, "vstore256_gep");
            src_val = emit_expr(g, expr->call.args.items[2], locals);
        } else {
            src_val = emit_expr(g, expr->call.args.items[1], locals);
        }
        LLVMValueRef ptr = LLVMBuildBitCast(g->builder, base, LLVMPointerType(v32i8, 0), "vstore256_ptr");
        LLVMValueRef v32 = vec256_to_v32i8(g, src_val);
        LLVMValueRef st = LLVMBuildStore(g->builder, v32, ptr);
        LLVMSetAlignment(st, 1);
        *out = NULL;
        return true;
    }

    if (method.len == 6 && memcmp(method.str, "Create", 6) == 0) {
        if (argc == 1) {
            zan_ast_node_t *arg0 = expr->call.args.items[0];
            LLVMValueRef v = emit_expr(g, arg0, locals);
            zan_type_t *arg_type = infer_expr_type(g, arg0, locals);
            if (arg_type && (arg_type->kind == TYPE_LONG || arg_type->kind == TYPE_ULONG)) {
                LLVMTypeRef v4i64 = LLVMVectorType(i64t, 4);
                LLVMValueRef val = coerce_int_to(g, v, i64t);
                LLVMValueRef ins = LLVMBuildInsertElement(g->builder, LLVMGetUndef(v4i64), val, LLVMConstInt(i32t, 0, 0), "l0");
                LLVMValueRef mask_elems[4] = { LLVMConstInt(i32t, 0, 0), LLVMConstInt(i32t, 0, 0), LLVMConstInt(i32t, 0, 0), LLVMConstInt(i32t, 0, 0) };
                LLVMValueRef mask = LLVMConstVector(mask_elems, 4);
                LLVMValueRef bcast = LLVMBuildShuffleVector(g->builder, ins, LLVMGetUndef(v4i64), mask, "bcast256_64");
                *out = v32i8_to_vec256(g, LLVMBuildBitCast(g->builder, bcast, v32i8, "v32"));
                return true;
            } else if (arg_type && (arg_type->kind == TYPE_BYTE || arg_type->kind == TYPE_SBYTE)) {
                LLVMValueRef b = coerce_int_to(g, v, LLVMInt8TypeInContext(g->ctx));
                LLVMValueRef ins = LLVMBuildInsertElement(g->builder, LLVMGetUndef(v32i8), b, LLVMConstInt(i32t, 0, 0), "b0");
                LLVMValueRef mask_elems[32];
                for (int i = 0; i < 32; i++) mask_elems[i] = LLVMConstInt(i32t, 0, 0);
                LLVMValueRef mask = LLVMConstVector(mask_elems, 32);
                LLVMValueRef bcast = LLVMBuildShuffleVector(g->builder, ins, LLVMGetUndef(v32i8), mask, "bcast256_8");
                *out = v32i8_to_vec256(g, bcast);
                return true;
            } else {
                LLVMTypeRef v8i32 = LLVMVectorType(i32t, 8);
                LLVMValueRef val = coerce_int_to(g, v, i32t);
                LLVMValueRef ins = LLVMBuildInsertElement(g->builder, LLVMGetUndef(v8i32), val, LLVMConstInt(i32t, 0, 0), "i0");
                LLVMValueRef mask_elems[8];
                for (int i = 0; i < 8; i++) mask_elems[i] = LLVMConstInt(i32t, 0, 0);
                LLVMValueRef mask = LLVMConstVector(mask_elems, 8);
                LLVMValueRef bcast = LLVMBuildShuffleVector(g->builder, ins, LLVMGetUndef(v8i32), mask, "bcast256_32");
                *out = v32i8_to_vec256(g, LLVMBuildBitCast(g->builder, bcast, v32i8, "v32"));
                return true;
            }
        } else if (argc == 4) {
            LLVMValueRef v0 = coerce_int_to(g, emit_expr(g, expr->call.args.items[0], locals), i64t);
            LLVMValueRef v1 = coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals), i64t);
            LLVMValueRef v2 = coerce_int_to(g, emit_expr(g, expr->call.args.items[2], locals), i64t);
            LLVMValueRef v3 = coerce_int_to(g, emit_expr(g, expr->call.args.items[3], locals), i64t);
            LLVMTypeRef st = get_vector256_struct_type(g);
            LLVMValueRef res = LLVMBuildInsertValue(g->builder, LLVMGetUndef(st), v0, 0, "s0");
            res = LLVMBuildInsertValue(g->builder, res, v1, 1, "s1");
            res = LLVMBuildInsertValue(g->builder, res, v2, 2, "s2");
            *out = LLVMBuildInsertValue(g->builder, res, v3, 3, "s3");
            return true;
        }
    }

    if (argc == 2) {
        if (method.len == 3 && memcmp(method.str, "And", 3) == 0) {
            LLVMValueRef a32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[1], locals));
            *out = v32i8_to_vec256(g, LLVMBuildAnd(g->builder, a32, b32, "vand256"));
            return true;
        }
        if (method.len == 2 && memcmp(method.str, "Or", 2) == 0) {
            LLVMValueRef a32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[1], locals));
            *out = v32i8_to_vec256(g, LLVMBuildOr(g->builder, a32, b32, "vor256"));
            return true;
        }
        if (method.len == 3 && memcmp(method.str, "Xor", 3) == 0) {
            LLVMValueRef a32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[1], locals));
            *out = v32i8_to_vec256(g, LLVMBuildXor(g->builder, a32, b32, "vxor256"));
            return true;
        }
        if (method.len == 6 && memcmp(method.str, "AndNot", 6) == 0) {
            LLVMValueRef a32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[1], locals));
            LLVMValueRef not_a = LLVMBuildNot(g->builder, a32, "vnot256");
            *out = v32i8_to_vec256(g, LLVMBuildAnd(g->builder, not_a, b32, "vandn256"));
            return true;
        }
        if (method.len == 6 && memcmp(method.str, "Equals", 6) == 0) {
            LLVMValueRef a32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[1], locals));
            LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntEQ, a32, b32, "vcmpeq256");
            LLVMValueRef mask = LLVMBuildSExt(g->builder, cmp, v32i8, "vmask256");
            *out = v32i8_to_vec256(g, mask);
            return true;
        }
        if (method.len == 3 && memcmp(method.str, "Add", 3) == 0) {
            LLVMValueRef a32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[1], locals));
            *out = v32i8_to_vec256(g, LLVMBuildAdd(g->builder, a32, b32, "vpaddb"));
            return true;
        }
        if (method.len == 8 && memcmp(method.str, "Subtract", 8) == 0) {
            LLVMValueRef a32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[1], locals));
            *out = v32i8_to_vec256(g, LLVMBuildSub(g->builder, a32, b32, "vpsubb"));
            return true;
        }
        if (method.len == 11 && memcmp(method.str, "AddSaturate", 11) == 0) {
            LLVMValueRef a32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[1], locals));
            LLVMTypeRef fty = LLVMFunctionType(v32i8, (LLVMTypeRef[]){ v32i8, v32i8 }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.uadd.sat.v32i8");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.uadd.sat.v32i8", fty);
            LLVMValueRef args[2] = { a32, b32 };
            *out = v32i8_to_vec256(g, zan_call2(g->builder, fty, fn, args, 2, "vpaddusb"));
            return true;
        }
        if (method.len == 16 && memcmp(method.str, "SubtractSaturate", 16) == 0) {
            LLVMValueRef a32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[1], locals));
            LLVMTypeRef fty = LLVMFunctionType(v32i8, (LLVMTypeRef[]){ v32i8, v32i8 }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.usub.sat.v32i8");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.usub.sat.v32i8", fty);
            LLVMValueRef args[2] = { a32, b32 };
            *out = v32i8_to_vec256(g, zan_call2(g->builder, fty, fn, args, 2, "vpsubusb"));
            return true;
        }
        if (method.len == 3 && memcmp(method.str, "Min", 3) == 0) {
            LLVMValueRef a32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[1], locals));
            LLVMTypeRef fty = LLVMFunctionType(v32i8, (LLVMTypeRef[]){ v32i8, v32i8 }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.umin.v32i8");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.umin.v32i8", fty);
            LLVMValueRef args[2] = { a32, b32 };
            *out = v32i8_to_vec256(g, zan_call2(g->builder, fty, fn, args, 2, "vpminub"));
            return true;
        }
        if (method.len == 3 && memcmp(method.str, "Max", 3) == 0) {
            LLVMValueRef a32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[1], locals));
            LLVMTypeRef fty = LLVMFunctionType(v32i8, (LLVMTypeRef[]){ v32i8, v32i8 }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.umax.v32i8");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.umax.v32i8", fty);
            LLVMValueRef args[2] = { a32, b32 };
            *out = v32i8_to_vec256(g, zan_call2(g->builder, fty, fn, args, 2, "vpmaxub"));
            return true;
        }
        if (method.len == 7 && memcmp(method.str, "Average", 7) == 0) {
            LLVMValueRef a32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[0], locals));
            LLVMValueRef b32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[1], locals));
            if (emit_target_is_x86(g)) {
                LLVMTypeRef fty = LLVMFunctionType(v32i8, (LLVMTypeRef[]){ v32i8, v32i8 }, 2, 0);
                LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.x86.avx2.pavg.b");
                if (!fn) fn = LLVMAddFunction(g->mod, "llvm.x86.avx2.pavg.b", fty);
                LLVMValueRef args[2] = { a32, b32 };
                *out = v32i8_to_vec256(g, zan_call2(g->builder, fty, fn, args, 2, "vpavgb"));
            } else {
                *out = v32i8_to_vec256(g, emit_pavg_portable(g, a32, b32));
            }
            return true;
        }
    }

    /* ConditionalSelect(mask, left, right): 3 args */
    if (argc == 3 && method.len == 17 && memcmp(method.str, "ConditionalSelect", 17) == 0) {
        LLVMValueRef cond = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMValueRef left = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[1], locals));
        LLVMValueRef right = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[2], locals));
        LLVMValueRef a_and = LLVMBuildAnd(g->builder, cond, left, "bld256_l");
        LLVMValueRef not_cond = LLVMBuildNot(g->builder, cond, "bld256_not");
        LLVMValueRef b_and = LLVMBuildAnd(g->builder, not_cond, right, "bld256_r");
        *out = v32i8_to_vec256(g, LLVMBuildOr(g->builder, a_and, b_and, "blend256"));
        return true;
    }

    /* Prefetch(nint address) / Prefetch(byte[] source, int offset) */
    if (method.len == 8 && memcmp(method.str, "Prefetch", 8) == 0 && (argc == 1 || argc == 2)) {
        LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
        LLVMValueRef addr_val = emit_expr(g, expr->call.args.items[0], locals);
        LLVMValueRef base;
        if (LLVMGetTypeKind(LLVMTypeOf(addr_val)) == LLVMPointerTypeKind) {
            base = LLVMBuildBitCast(g->builder, addr_val, LLVMPointerType(i8, 0), "pfetch256_base");
        } else {
            base = LLVMBuildIntToPtr(g->builder, addr_val, LLVMPointerType(i8, 0), "pfetch256_base");
        }
        if (argc == 2) {
            LLVMValueRef off_val = coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals), i64t);
            /*
             * element index for array sources, mirroring arr[i] indexing
             * (same fix as the Vector128 dispatcher above)
             */
            zan_type_t *at = infer_expr_type(g, expr->call.args.items[0], locals);
            zan_type_t *et = at ? container_elem_type(at) : NULL;
            if (at && (at->kind == TYPE_ARRAY || is_span_type(at)) && et) {
                LLVMTypeRef etl = map_type(g, et);
                base = LLVMBuildGEP2(g->builder, etl,
                    LLVMBuildBitCast(g->builder, base,
                        LLVMPointerType(etl, 0), "pfetch_t"), &off_val, 1,
                    "pfetch256_gep");
            } else {
                base = LLVMBuildGEP2(g->builder, i8, base, &off_val, 1, "pfetch256_gep");
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

    if (method.len == 26 && memcmp(method.str, "ExtractMostSignificantBits", 26) == 0 && argc == 1) {
        LLVMValueRef a32 = vec256_to_v32i8(g, emit_expr(g, expr->call.args.items[0], locals));
        if (emit_target_is_x86(g)) {
            LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ v32i8 }, 1, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.x86.avx2.pmovmskb");
            if (!fn) fn = LLVMAddFunction(g->mod, "llvm.x86.avx2.pmovmskb", fty);
            *out = zan_call2(g->builder, fty, fn, &a32, 1, "vpmovmskb");
        } else {
            *out = emit_pmovmskb_portable(g, a32, 32);
        }
        return true;
    }

    return false;
}

static bool emit_sse2_call(zan_irgen_t *g, zan_ast_node_t *expr,
                           local_scope_t *locals, LLVMValueRef *out) {
    if (expr->kind != AST_CALL) return false;
    zan_ast_node_t *callee = expr->call.callee;
    if (callee->kind != AST_MEMBER_ACCESS) return false;
    if (!is_target_class_obj(callee->member.object, "Sse2", 4, locals))
        return false;

    zan_istr_t method = callee->member.name;
    int argc = expr->call.args.count;

    if (method.len == 13 && memcmp(method.str, "LoadVector128", 13) == 0 && argc == 1) {
        LLVMTypeRef v16i8 = LLVMVectorType(LLVMInt8TypeInContext(g->ctx), 16);
        LLVMValueRef addr_val = emit_expr(g, expr->call.args.items[0], locals);
        LLVMValueRef ptr;
        if (LLVMGetTypeKind(LLVMTypeOf(addr_val)) == LLVMPointerTypeKind) {
            ptr = LLVMBuildBitCast(g->builder, addr_val, LLVMPointerType(v16i8, 0), "vload_ptr");
        } else {
            ptr = LLVMBuildIntToPtr(g->builder, addr_val, LLVMPointerType(v16i8, 0), "vload_ptr");
        }
        LLVMValueRef ld = LLVMBuildLoad2(g->builder, v16i8, ptr, "vload");
        LLVMSetAlignment(ld, 1);
        *out = v16i8_to_vec128(g, ld);
        return true;
    }
    if (method.len == 5 && memcmp(method.str, "Store", 5) == 0 && argc == 2) {
        LLVMTypeRef v16i8 = LLVMVectorType(LLVMInt8TypeInContext(g->ctx), 16);
        LLVMValueRef addr_val = emit_expr(g, expr->call.args.items[0], locals);
        LLVMValueRef src_val = emit_expr(g, expr->call.args.items[1], locals);
        LLVMValueRef v16 = vec128_to_v16i8(g, src_val);
        LLVMValueRef ptr;
        if (LLVMGetTypeKind(LLVMTypeOf(addr_val)) == LLVMPointerTypeKind) {
            ptr = LLVMBuildBitCast(g->builder, addr_val, LLVMPointerType(v16i8, 0), "vstore_ptr");
        } else {
            ptr = LLVMBuildIntToPtr(g->builder, addr_val, LLVMPointerType(v16i8, 0), "vstore_ptr");
        }
        LLVMValueRef st = LLVMBuildStore(g->builder, v16, ptr);
        LLVMSetAlignment(st, 1);
        *out = NULL;
        return true;
    }
    if (method.len == 3 && memcmp(method.str, "Xor", 3) == 0 && argc == 2) {
        LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
        *out = v16i8_to_vec128(g, LLVMBuildXor(g->builder, a16, b16, "vxor"));
        return true;
    }
    if (method.len == 3 && memcmp(method.str, "And", 3) == 0 && argc == 2) {
        LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
        *out = v16i8_to_vec128(g, LLVMBuildAnd(g->builder, a16, b16, "vand"));
        return true;
    }
    if (method.len == 2 && memcmp(method.str, "Or", 2) == 0 && argc == 2) {
        LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
        *out = v16i8_to_vec128(g, LLVMBuildOr(g->builder, a16, b16, "vor"));
        return true;
    }
    if (method.len == 12 && memcmp(method.str, "CompareEqual", 12) == 0 && argc == 2) {
        LLVMTypeRef v16i8 = LLVMVectorType(LLVMInt8TypeInContext(g->ctx), 16);
        LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
        LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntEQ, a16, b16, "vcmpeq");
        LLVMValueRef mask = LLVMBuildSExt(g->builder, cmp, v16i8, "vmask");
        *out = v16i8_to_vec128(g, mask);
        return true;
    }
    if (method.len == 8 && memcmp(method.str, "MoveMask", 8) == 0 && argc == 1) {
        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef v16i8 = LLVMVectorType(LLVMInt8TypeInContext(g->ctx), 16);
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

    return false;
}

static bool emit_sse42_call(zan_irgen_t *g, zan_ast_node_t *expr,
                            local_scope_t *locals, LLVMValueRef *out) {
    if (expr->kind != AST_CALL) return false;
    zan_ast_node_t *callee = expr->call.callee;
    if (callee->kind != AST_MEMBER_ACCESS) return false;
    if (!is_target_class_obj(callee->member.object, "Sse42", 5, locals))
        return false;

    zan_istr_t method = callee->member.name;
    int argc = expr->call.args.count;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    /* 仅在支持的 x86 目标平台生成硬件指令，其他平台使用可移植算法模拟 */
    bool hw_crc = strstr(g->target_triple, "x86") != NULL ||
                  strstr(g->target_triple, "amd64") != NULL;

    if (method.len == 5 && memcmp(method.str, "Crc32", 5) == 0 && argc == 2) {
        zan_ast_node_t *arg0 = expr->call.args.items[0];
        zan_ast_node_t *arg1 = expr->call.args.items[1];
        zan_type_t *arg0_type = infer_expr_type(g, arg0, locals);
        zan_type_t *arg1_type = infer_expr_type(g, arg1, locals);
        LLVMValueRef crc_val = emit_expr(g, arg0, locals);
        LLVMValueRef data_val = emit_expr(g, arg1, locals);

        if (arg0_type && (arg0_type->kind == TYPE_LONG || arg0_type->kind == TYPE_ULONG)) {
            /* ulong Crc32(ulong crc, ulong data) -> llvm.x86.sse42.crc32.64.64 */
            LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i64t, i64t }, 2, 0);
            LLVMValueRef fn = hw_crc
                ? (LLVMGetNamedFunction(g->mod, "llvm.x86.sse42.crc32.64.64")
                   ? LLVMGetNamedFunction(g->mod, "llvm.x86.sse42.crc32.64.64")
                   : LLVMAddFunction(g->mod, "llvm.x86.sse42.crc32.64.64", fty))
                : crc32c_step_fn(g, 8);
            LLVMValueRef args[2] = { coerce_int_to(g, crc_val, i64t), coerce_int_to(g, data_val, i64t) };
            *out = zan_call2(g->builder, fty, fn, args, 2, "crc32_64");
            return true;
        } else {
            /* uint Crc32(uint crc, byte/ushort/uint/ulong data) */
            LLVMValueRef crc32 = coerce_int_to(g, crc_val, i32t);
            if (arg1_type && (arg1_type->kind == TYPE_LONG || arg1_type->kind == TYPE_ULONG)) {
                LLVMTypeRef fty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i64t, i64t }, 2, 0);
                LLVMValueRef fn = hw_crc
                    ? (LLVMGetNamedFunction(g->mod, "llvm.x86.sse42.crc32.64.64")
                       ? LLVMGetNamedFunction(g->mod, "llvm.x86.sse42.crc32.64.64")
                       : LLVMAddFunction(g->mod, "llvm.x86.sse42.crc32.64.64", fty))
                    : crc32c_step_fn(g, 8);
                LLVMValueRef args[2] = { coerce_int_to(g, crc32, i64t), coerce_int_to(g, data_val, i64t) };
                *out = coerce_int_to(g, zan_call2(g->builder, fty, fn, args, 2, "crc32_64"), i32t);
                return true;
            } else if (arg1_type && (arg1_type->kind == TYPE_SHORT || arg1_type->kind == TYPE_USHORT || arg1_type->kind == TYPE_CHAR)) {
                LLVMTypeRef i16t = LLVMInt16TypeInContext(g->ctx);
                LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t, i16t }, 2, 0);
                LLVMValueRef fn = hw_crc
                    ? (LLVMGetNamedFunction(g->mod, "llvm.x86.sse42.crc32.32.16")
                       ? LLVMGetNamedFunction(g->mod, "llvm.x86.sse42.crc32.32.16")
                       : LLVMAddFunction(g->mod, "llvm.x86.sse42.crc32.32.16", fty))
                    : crc32c_step_fn(g, 2);
                LLVMValueRef args[2] = { crc32, coerce_int_to(g, data_val, i16t) };
                *out = zan_call2(g->builder, fty, fn, args, 2, "crc32_16");
                return true;
            } else if (arg1_type && (arg1_type->kind == TYPE_BYTE || arg1_type->kind == TYPE_SBYTE)) {
                LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t, i8t }, 2, 0);
                LLVMValueRef fn = hw_crc
                    ? (LLVMGetNamedFunction(g->mod, "llvm.x86.sse42.crc32.32.8")
                       ? LLVMGetNamedFunction(g->mod, "llvm.x86.sse42.crc32.32.8")
                       : LLVMAddFunction(g->mod, "llvm.x86.sse42.crc32.32.8", fty))
                    : crc32c_step_fn(g, 1);
                LLVMValueRef args[2] = { crc32, coerce_int_to(g, data_val, i8t) };
                *out = zan_call2(g->builder, fty, fn, args, 2, "crc32_8");
                return true;
            } else {
                /* default 32-bit data */
                LLVMTypeRef fty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t, i32t }, 2, 0);
                LLVMValueRef fn = hw_crc
                    ? (LLVMGetNamedFunction(g->mod, "llvm.x86.sse42.crc32.32.32")
                       ? LLVMGetNamedFunction(g->mod, "llvm.x86.sse42.crc32.32.32")
                       : LLVMAddFunction(g->mod, "llvm.x86.sse42.crc32.32.32", fty))
                    : crc32c_step_fn(g, 4);
                LLVMValueRef args[2] = { crc32, coerce_int_to(g, data_val, i32t) };
                *out = zan_call2(g->builder, fty, fn, args, 2, "crc32_32");
                return true;
            }
        }
    }

    return false;
}

/* ARM64 crypto-intrinsic declaration lookup/create (all <16 x i8>). */
static LLVMValueRef aes_crypto_fn(zan_irgen_t *g, const char *name, int two_args) {
    LLVMTypeRef v16i8 = LLVMVectorType(LLVMInt8TypeInContext(g->ctx), 16);
    LLVMTypeRef fty = two_args
        ? LLVMFunctionType(v16i8, (LLVMTypeRef[]){ v16i8, v16i8 }, 2, 0)
        : LLVMFunctionType(v16i8, (LLVMTypeRef[]){ v16i8 }, 1, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, name);
    if (!fn) fn = LLVMAddFunction(g->mod, name, fty);
    return fn;
}

/* AArch64 平台的 AES 加密原语降解：等价映射至 ARMv8-A 硬件 AES 指令 */
static bool emit_aes_arm(zan_irgen_t *g, zan_ast_node_t *expr,
                         local_scope_t *locals, zan_istr_t method, int argc,
                         LLVMValueRef *out) {
    LLVMTypeRef v16i8 = LLVMVectorType(LLVMInt8TypeInContext(g->ctx), 16);
    LLVMValueRef zero = LLVMConstNull(v16i8);

    if (method.len == 7 && memcmp(method.str, "Encrypt", 7) == 0 && argc == 2) {
        LLVMValueRef a = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMValueRef k = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
        LLVMValueRef kimc = zan_call2(g->builder,
            LLVMFunctionType(v16i8, (LLVMTypeRef[]){ v16i8 }, 1, 0),
            aes_crypto_fn(g, "llvm.aarch64.crypto.aesimc", 0), (LLVMValueRef[]){ k }, 1, "kimc");
        LLVMValueRef e = zan_call2(g->builder,
            LLVMFunctionType(v16i8, (LLVMTypeRef[]){ v16i8, v16i8 }, 2, 0),
            aes_crypto_fn(g, "llvm.aarch64.crypto.aese", 1), (LLVMValueRef[]){ a, zero }, 2, "aese");
        LLVMValueRef x = zan_xor(g->builder, e, kimc, "encx");
        LLVMValueRef em = zan_call2(g->builder,
            LLVMFunctionType(v16i8, (LLVMTypeRef[]){ v16i8 }, 1, 0),
            aes_crypto_fn(g, "llvm.aarch64.crypto.aesmc", 0), (LLVMValueRef[]){ x }, 1, "emc");
        *out = v16i8_to_vec128(g, em);
        return true;
    }
    if (method.len == 11 && memcmp(method.str, "EncryptLast", 11) == 0 && argc == 2) {
        LLVMValueRef a = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMValueRef k = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
        LLVMValueRef e = zan_call2(g->builder,
            LLVMFunctionType(v16i8, (LLVMTypeRef[]){ v16i8, v16i8 }, 2, 0),
            aes_crypto_fn(g, "llvm.aarch64.crypto.aese", 1), (LLVMValueRef[]){ a, zero }, 2, "aese");
        *out = v16i8_to_vec128(g, zan_xor(g->builder, e, k, "elast"));
        return true;
    }
    if (method.len == 7 && memcmp(method.str, "Decrypt", 7) == 0 && argc == 2) {
        LLVMValueRef a = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMValueRef k = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
        LLVMValueRef kmc = zan_call2(g->builder,
            LLVMFunctionType(v16i8, (LLVMTypeRef[]){ v16i8 }, 1, 0),
            aes_crypto_fn(g, "llvm.aarch64.crypto.aesmc", 0), (LLVMValueRef[]){ k }, 1, "kmc");
        LLVMValueRef d = zan_call2(g->builder,
            LLVMFunctionType(v16i8, (LLVMTypeRef[]){ v16i8, v16i8 }, 2, 0),
            aes_crypto_fn(g, "llvm.aarch64.crypto.aesd", 1), (LLVMValueRef[]){ a, zero }, 2, "aesd");
        LLVMValueRef x = zan_xor(g->builder, d, kmc, "decx");
        LLVMValueRef dm = zan_call2(g->builder,
            LLVMFunctionType(v16i8, (LLVMTypeRef[]){ v16i8 }, 1, 0),
            aes_crypto_fn(g, "llvm.aarch64.crypto.aesimc", 0), (LLVMValueRef[]){ x }, 1, "dimc");
        *out = v16i8_to_vec128(g, dm);
        return true;
    }
    if (method.len == 11 && memcmp(method.str, "DecryptLast", 11) == 0 && argc == 2) {
        LLVMValueRef a = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMValueRef k = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
        LLVMValueRef d = zan_call2(g->builder,
            LLVMFunctionType(v16i8, (LLVMTypeRef[]){ v16i8, v16i8 }, 2, 0),
            aes_crypto_fn(g, "llvm.aarch64.crypto.aesd", 1), (LLVMValueRef[]){ a, zero }, 2, "aesd");
        *out = v16i8_to_vec128(g, zan_xor(g->builder, d, k, "dlast"));
        return true;
    }
    if (method.len == 17 && memcmp(method.str, "InverseMixColumns", 17) == 0 && argc == 1) {
        LLVMValueRef a = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMValueRef im = zan_call2(g->builder,
            LLVMFunctionType(v16i8, (LLVMTypeRef[]){ v16i8 }, 1, 0),
            aes_crypto_fn(g, "llvm.aarch64.crypto.aesimc", 0), (LLVMValueRef[]){ a }, 1, "imc");
        *out = v16i8_to_vec128(g, im);
        return true;
    }
    return false;
}

static bool emit_aes_call(zan_irgen_t *g, zan_ast_node_t *expr,
                          local_scope_t *locals, LLVMValueRef *out) {
    if (expr->kind != AST_CALL) return false;
    zan_ast_node_t *callee = expr->call.callee;
    if (callee->kind != AST_MEMBER_ACCESS) return false;
    if (!is_target_class_obj(callee->member.object, "Aes", 3, locals))
        return false;

    zan_istr_t method = callee->member.name;
    int argc = expr->call.args.count;
    LLVMTypeRef v2i64 = LLVMVectorType(LLVMInt64TypeInContext(g->ctx), 2);

    if (strstr(g->target_triple, "aarch64") != NULL ||
        strstr(g->target_triple, "arm64") != NULL) {
        return emit_aes_arm(g, expr, locals, method, argc, out);
    }
    /* AES 硬件指令平台防护：无硬件扩展的目标平台直接编译为报错或软回退 */
    if (!emit_target_is_x86(g)) return false;

    if (method.len == 7 && memcmp(method.str, "Encrypt", 7) == 0 && argc == 2) {
        LLVMValueRef v_val = vec128_to_v2i64(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMValueRef v_key = vec128_to_v2i64(g, emit_expr(g, expr->call.args.items[1], locals));
        LLVMTypeRef fty = LLVMFunctionType(v2i64, (LLVMTypeRef[]){ v2i64, v2i64 }, 2, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.x86.aesni.aesenc");
        if (!fn) fn = LLVMAddFunction(g->mod, "llvm.x86.aesni.aesenc", fty);
        LLVMValueRef r = zan_call2(g->builder, fty, fn, (LLVMValueRef[]){ v_val, v_key }, 2, "aesenc");
        *out = v2i64_to_vec128(g, r);
        return true;
    }
    if (method.len == 11 && memcmp(method.str, "EncryptLast", 11) == 0 && argc == 2) {
        LLVMValueRef v_val = vec128_to_v2i64(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMValueRef v_key = vec128_to_v2i64(g, emit_expr(g, expr->call.args.items[1], locals));
        LLVMTypeRef fty = LLVMFunctionType(v2i64, (LLVMTypeRef[]){ v2i64, v2i64 }, 2, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.x86.aesni.aesenclast");
        if (!fn) fn = LLVMAddFunction(g->mod, "llvm.x86.aesni.aesenclast", fty);
        LLVMValueRef r = zan_call2(g->builder, fty, fn, (LLVMValueRef[]){ v_val, v_key }, 2, "aesenclast");
        *out = v2i64_to_vec128(g, r);
        return true;
    }
    if (method.len == 7 && memcmp(method.str, "Decrypt", 7) == 0 && argc == 2) {
        LLVMValueRef v_val = vec128_to_v2i64(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMValueRef v_key = vec128_to_v2i64(g, emit_expr(g, expr->call.args.items[1], locals));
        LLVMTypeRef fty = LLVMFunctionType(v2i64, (LLVMTypeRef[]){ v2i64, v2i64 }, 2, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.x86.aesni.aesdec");
        if (!fn) fn = LLVMAddFunction(g->mod, "llvm.x86.aesni.aesdec", fty);
        LLVMValueRef r = zan_call2(g->builder, fty, fn, (LLVMValueRef[]){ v_val, v_key }, 2, "aesdec");
        *out = v2i64_to_vec128(g, r);
        return true;
    }
    if (method.len == 11 && memcmp(method.str, "DecryptLast", 11) == 0 && argc == 2) {
        LLVMValueRef v_val = vec128_to_v2i64(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMValueRef v_key = vec128_to_v2i64(g, emit_expr(g, expr->call.args.items[1], locals));
        LLVMTypeRef fty = LLVMFunctionType(v2i64, (LLVMTypeRef[]){ v2i64, v2i64 }, 2, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.x86.aesni.aesdeclast");
        if (!fn) fn = LLVMAddFunction(g->mod, "llvm.x86.aesni.aesdeclast", fty);
        LLVMValueRef r = zan_call2(g->builder, fty, fn, (LLVMValueRef[]){ v_val, v_key }, 2, "aesdeclast");
        *out = v2i64_to_vec128(g, r);
        return true;
    }
    if (method.len == 12 && memcmp(method.str, "KeygenAssist", 12) == 0 && argc == 2) {
        LLVMValueRef v_val = vec128_to_v2i64(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMValueRef rcon = coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals), LLVMInt8TypeInContext(g->ctx));
        LLVMTypeRef fty = LLVMFunctionType(v2i64, (LLVMTypeRef[]){ v2i64, LLVMInt8TypeInContext(g->ctx) }, 2, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.x86.aesni.aeskeygenassist");
        if (!fn) fn = LLVMAddFunction(g->mod, "llvm.x86.aesni.aeskeygenassist", fty);
        LLVMValueRef r = zan_call2(g->builder, fty, fn, (LLVMValueRef[]){ v_val, rcon }, 2, "aeskeygenassist");
        *out = v2i64_to_vec128(g, r);
        return true;
    }
    if (method.len == 17 && memcmp(method.str, "InverseMixColumns", 17) == 0 && argc == 1) {
        LLVMValueRef v_val = vec128_to_v2i64(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMTypeRef fty = LLVMFunctionType(v2i64, (LLVMTypeRef[]){ v2i64 }, 1, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "llvm.x86.aesni.aesimc");
        if (!fn) fn = LLVMAddFunction(g->mod, "llvm.x86.aesni.aesimc", fty);
        LLVMValueRef r = zan_call2(g->builder, fty, fn, &v_val, 1, "aesimc");
        *out = v2i64_to_vec128(g, r);
        return true;
    }

    return false;
}

/* 自定义 getter 属性降解为方法调用，读取时发射其专用 getter 函数 */
static zan_symbol_t *property_getter_sym(zan_irgen_t *g, zan_symbol_t *prop) {
    if (!prop || prop->kind != SYM_PROPERTY || !prop->decl) return NULL;
    if (!prop->decl->field_decl.getter_body) return NULL;
    zan_symbol_t *type_sym = prop->parent;
    if (!type_sym) return NULL;
    for (int i = 0; i < type_sym->member_count; i++) {
        zan_symbol_t *m = type_sym->members[i];
        if (!m || m->kind != SYM_METHOD || !m->decl ||
            m->decl->kind != AST_METHOD_DECL) continue;
        zan_istr_t mn = m->name;
        if (mn.len == prop->name.len + 4 &&
            memcmp(mn.str, "get_", 4) == 0 &&
            memcmp(mn.str + 4, prop->name.str, prop->name.len) == 0)
            return m;
    }
    return NULL;
}

/* 在已求值的接收者上发射属性 getter 方法调用 */
static LLVMValueRef emit_property_getter_call(zan_irgen_t *g,
                                              zan_symbol_t *getter,
                                              zan_type_t *recv_type,
                                              LLVMValueRef recv,
                                              zan_ast_node_t *obj_ast,
                                              local_scope_t *locals) {
    bool is_static = (getter->modifiers & MOD_STATIC) != 0;
    for (int fi = irgen_find_function(g, getter); fi >= 0; fi = -1) {
        if (g->functions[fi].sym == getter) {
            LLVMValueRef rval = recv;
            /* 无接收者访问实例属性时默认补齐 this 隐式指针 */
            if (!is_static && !rval) {
                zan_diag_emit(g->diag, DIAG_ERROR,
                              obj_ast ? obj_ast->loc : zan_loc(0, 0, 0, 0),
                              "property getter requires a receiver");
                return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
            }
            if (!is_static &&
                LLVMGetTypeKind(LLVMTypeOf(rval)) == LLVMStructTypeKind) {
                /*
                 * a struct receiver that arrived by value has no address;
                 * spill it like the op_call path does
                 */
                LLVMValueRef rslot = emit_entry_alloca(g, LLVMTypeOf(rval),
                                                       "pg.recv");
                LLVMBuildStore(g->builder, rval, rslot);
                rval = rslot;
            }
            LLVMTypeRef mft = g->functions[fi].fn_type;
            LLVMValueRef mfn = route_generic_method(g, recv_type, getter,
                g->functions[fi].fn, mft, &mft);
            LLVMValueRef args[1];
            unsigned argc = 0;
            if (!is_static) args[argc++] = rval;
            const char *cn = "pget";
            /* base.Prop 属性读取：绑定至基类属性 getter 实现 */
            LLVMValueRef result = emit_dispatch_call(g,
                (is_static || !recv_type) ? NULL : recv_type->sym, getter,
                mfn, mft, args, (int)argc, cn);
            result = coerce_generic_result(g, result, getter, recv_type);
            /* an owned receiver temp must outlive the call */
            emit_release_owned_call_temp(g, obj_ast, recv, locals);
            return result;
        }
    }
    return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
}

/* 获取属性 setter 方法符号 */
static zan_symbol_t *property_setter_sym(zan_irgen_t *g, zan_symbol_t *prop) {
    if (!prop || prop->kind != SYM_PROPERTY || !prop->decl) return NULL;
    if (!prop->decl->field_decl.setter_body) return NULL;
    zan_symbol_t *type_sym = prop->parent;
    if (!type_sym) return NULL;
    for (int i = 0; i < type_sym->member_count; i++) {
        zan_symbol_t *m = type_sym->members[i];
        if (!m || m->kind != SYM_METHOD || !m->decl ||
            m->decl->kind != AST_METHOD_DECL) continue;
        zan_istr_t mn = m->name;
        if (mn.len == prop->name.len + 4 &&
            memcmp(mn.str, "set_", 4) == 0 &&
            memcmp(mn.str + 4, prop->name.str, prop->name.len) == 0)
            return m;
    }
    return NULL;
}

/* 在接收者上发射 set_Prop(value) 调用，传入右值作为实参 */
static void emit_property_setter_call(zan_irgen_t *g, zan_symbol_t *setter,
                                      zan_type_t *recv_type, LLVMValueRef recv,
                                      LLVMValueRef value,
                                      zan_ast_node_t *obj_ast,
                                      zan_ast_node_t *rhs_ast,
                                      local_scope_t *locals) {
    bool is_static = (setter->modifiers & MOD_STATIC) != 0;
    for (int fi = irgen_find_function(g, setter); fi >= 0; fi = -1) {
        if (g->functions[fi].sym == setter) {
            LLVMValueRef rval = recv;
            /* 实例 setter 空接收者防护：自动使用当前 this 指针 */
            if (!is_static && !rval) {
                zan_diag_emit(g->diag, DIAG_ERROR,
                              obj_ast ? obj_ast->loc : zan_loc(0, 0, 0, 0),
                              "property setter requires a receiver");
                return;
            }
            if (!is_static &&
                LLVMGetTypeKind(LLVMTypeOf(rval)) == LLVMStructTypeKind) {
                LLVMValueRef rslot = emit_entry_alloca(g, LLVMTypeOf(rval),
                                                       "ps.recv");
                LLVMBuildStore(g->builder, rval, rslot);
                rval = rslot;
            }
            LLVMTypeRef mft = g->functions[fi].fn_type;
            LLVMValueRef mfn = route_generic_method(g, recv_type, setter,
                g->functions[fi].fn, mft, &mft);
            /* the setter's `value` is its single declared parameter */
            zan_type_t *p0 = method_param_type_at(g, setter, 0, NULL,
                                                  obj_ast, locals);
            if (p0) p0 = subst_type_param_deep(g, p0, recv_type);
            LLVMValueRef v = value;
            if (p0) {
                LLVMTypeRef p0t = map_type(g, p0);
                if (LLVMTypeOf(v) != p0t)
                    v = coerce_int_to(g, v, p0t);
            }
            LLVMValueRef args[2];
            unsigned argc = 0;
            if (!is_static) args[argc++] = rval;
            args[argc++] = v;
            /* 裸属性名赋值 Prop = v 降解处理 */
            emit_dispatch_call(g, (is_static || !recv_type) ? NULL : recv_type->sym,
                setter, mfn, mft, args, (int)argc, "");
            emit_release_owned_call_temp(g, obj_ast, recv, locals);
            emit_release_owned_call_temp(g, rhs_ast, value, locals);
            return;
        }
    }
}

/* 表达式 AST 节点发射器实现 */

static LLVMValueRef emit_expr_identifier(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        local_var_t *local = local_find(locals, expr->ident.name);
        if (local) {
            return promote_loaded(g, LLVMBuildLoad2(g->builder,
                map_type(g, local->type), local->alloca, "load"),
                local->type);
        }
        /* implicit this.Field access in method bodies */
        if (g->current_this && g->current_type_sym) {
            int fi = get_field_index(g->current_type_sym, expr->ident.name);
            if (fi >= 0) {
                /*
                 * custom-getter property read through the bare name: dispatch
                 * to the getter with `this` as the receiver
                 */
                zan_symbol_t *psym = get_field_sym(g->current_type_sym, expr->ident.name);
                zan_symbol_t *getter = property_getter_sym(g, psym);
                if (getter) {
                    LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                    LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(st, 0), g->current_this, "this");
                    zan_type_t *rct = g->cur_inst ? g->cur_inst
                                                  : g->current_type_sym->type;
                    return emit_property_getter_call(g, getter, rct, this_ptr,
                                                     expr, locals);
                }
                LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                if (st) {
                    LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(st, 0), g->current_this, "this");
                    LLVMValueRef fptr = emit_field_ptr(g, g->current_type_sym, st, this_ptr, fi, "fld");
                    zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->ident.name);
                    zan_type_t *fty = fsym ? field_type_here(g, fsym->type) : NULL;
                    LLVMTypeRef ft = fty ? map_type(g, fty) : LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef fval = (fsym && (fsym->modifiers & MOD_WEAK))
                        ? emit_weak_field_load(g, fptr, ft)
                        : LLVMBuildLoad2(g->builder, ft, fptr, "fval");
                    return promote_loaded(g, fval, fty);
                }
            }
        }
        /*
         * bare-name static field of the enclosing class: `field` -> global.
         * Works in both static and instance methods.
         */
        if (g->current_type_sym) {
            zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->ident.name);
            /* 静态自定义 getter 属性读取分发 */
            zan_symbol_t *getter = property_getter_sym(g, fsym);
            if (getter) {
                return emit_property_getter_call(g, getter,
                    g->current_type_sym->type, NULL, expr, locals);
            }
            LLVMValueRef gv = get_static_field_global(g, g->current_type_sym, fsym, NULL);
            if (gv) {
                LLVMTypeRef ft = fsym->type ? map_type(g, fsym->type)
                                            : LLVMInt64TypeInContext(g->ctx);
                return promote_loaded(g,
                    LLVMBuildLoad2(g->builder, ft, gv, "sfld"), fsym->type);
            }
        }
        /* 方法组转换为委托实例（方法名作为第一类值传递） */
        if (g->current_type_sym) {
            zan_symbol_t *method_sym = get_method_sym(g->current_type_sym, expr->ident.name);
            if (method_sym) {
                if (method_sym->decl && method_sym->decl->kind == AST_METHOD_DECL &&
                    (method_sym->decl->method_decl.modifiers & MOD_STATIC) == 0) {
                    LLVMValueRef self = g->current_this
                        ? LLVMBuildLoad2(g->builder,
                              LLVMGetAllocatedType(g->current_this),
                              g->current_this, "this")
                        : NULL;
                    LLVMValueRef clo = self
                        ? emit_method_group_closure(g, method_sym, self, expr->loc)
                        : NULL;
                    if (clo) return clo;
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "instance method '%.*s' cannot be used as a value here: "
                        "no receiver is in scope",
                        (int)expr->ident.name.len, expr->ident.name.str);
                    return LLVMConstNull(
                        LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
                }
                if (target_is_wasm32(g)) {
                    LLVMValueRef clo = emit_method_group_closure(g, method_sym,
                                                                 NULL, expr->loc);
                    if (clo) return clo;
                }
                for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                    if (g->functions[fi].sym == method_sym) {
                        return g->functions[fi].fn;
                    }
                }
            }
        }
        /* try global LLVM function by name */
        {
            char nbuf[256];
            int nl = expr->ident.name.len < 255 ? expr->ident.name.len : 255;
            memcpy(nbuf, expr->ident.name.str, (size_t)nl);
            nbuf[nl] = '\0';
            LLVMValueRef gfn = LLVMGetNamedFunction(g->mod, nbuf);
            if (gfn) return gfn;
        }
        /* return 0 for unresolved — error was reported in checker */
        /* 未解析标识符报错与诊断兜底 */
        if (!zan_binder_lookup(g->binder, expr->ident.name)) {
            zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                "use of undeclared identifier '%.*s'",
                (int)expr->ident.name.len, expr->ident.name.str);
        }
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

static LLVMValueRef load_collection_slot_value(zan_irgen_t *g,
                                                zan_type_t *elem_type,
                                                LLVMValueRef slot_ptr) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef elem_llvm = elem_type ? map_type(g, elem_type) : i64;
    LLVMTypeKind kind = LLVMGetTypeKind(elem_llvm);
    if (kind == LLVMStructTypeKind)
        return load_struct_from_slot(g, slot_ptr, elem_llvm);
    LLVMValueRef raw = LLVMBuildLoad2(g->builder, i64, slot_ptr, "slot.raw");
    if (kind == LLVMPointerTypeKind)
        return LLVMBuildIntToPtr(g->builder, raw, elem_llvm, "slot.ptr");
    if (kind == LLVMDoubleTypeKind)
        return LLVMBuildBitCast(g->builder, raw, elem_llvm, "slot.fp");
    if (kind == LLVMFloatTypeKind) {
        LLVMValueRef narrow = LLVMBuildTrunc(
            g->builder, raw, LLVMInt32TypeInContext(g->ctx), "slot.f32");
        return LLVMBuildBitCast(g->builder, narrow, elem_llvm, "slot.f");
    }
    if (kind == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(elem_llvm) < LLVMGetIntTypeWidth(i64))
        return LLVMBuildTrunc(g->builder, raw, elem_llvm, "slot.int");
    return raw;
}

static void emit_dict_value_set(zan_irgen_t *g, zan_type_t *dict_type,
                                LLVMValueRef raw, LLVMValueRef key,
                                LLVMValueRef value, zan_ast_node_t *key_expr,
                                zan_ast_node_t *value_expr,
                                local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    zan_type_t *key_type = dict_key_type(g, dict_type);
    zan_type_t *value_type = dict_value_type(dict_type);
    LLVMValueRef value_words = load_dict_value_words(g, raw);
    LLVMValueRef existing = emit_dict_find(g, dict_type, raw, key);
    LLVMValueRef was_hit = zan_icmp(g->builder, LLVMIntSGE, existing,
        LLVMConstInt(i64, 0, 0), "dset.hit");
    LLVMValueRef is_str = LLVMConstInt(i64,
        (key_type && key_type->kind == TYPE_STRING) ? 1 : 0, 0);
    LLVMValueRef set_fn = get_dict_set_fn(g);
    LLVMValueRef index = zan_call2(g->builder, LLVMGlobalGetValueType(set_fn),
        set_fn, (LLVMValueRef[]){ raw, key, is_str }, 3, "dset.ix");
    LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
        LLVMPointerType(g->dict_struct_type, 0), "dset.dp");
    LLVMValueRef values_ptr = LLVMBuildStructGEP2(g->builder,
        g->dict_struct_type, dp, 3, "dset.vp");
    LLVMValueRef values = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0),
        values_ptr, "dset.vals");
    LLVMValueRef word = zan_mul(g->builder, index, value_words,
        "dset.word");
    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, values, &word, 1,
        "dset.slot");
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef hit_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "dset.old");
    LLVMBasicBlockRef append_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "dset.new");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "dset.done");
    LLVMBuildCondBr(g->builder, was_hit, hit_bb, append_bb);
    LLVMPositionBuilderAtEnd(g->builder, hit_bb);
    emit_release_owned_call_temp(g, key_expr, key, locals);
    emit_collection_slot_store(g, value_type, i64, slot, value,
        value_expr, locals, 1);
    LLVMBuildBr(g->builder, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, append_bb);
    if (key_type && is_rc_managed_type(key_type) &&
        !expr_yields_owned_rc_value(g, key_expr, locals))
        emit_rc_retain_for_type(g, key_type, key);
    emit_collection_slot_store(g, value_type, i64, slot, value,
        value_expr, locals, 0);
    LLVMBuildBr(g->builder, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, done_bb);
}

static LLVMValueRef emit_typed_equality(zan_irgen_t *g, zan_type_t *type,
                                         LLVMValueRef left,
                                         LLVMValueRef right) {
    type = concretize(g, type);
    if (type && type->sym &&
        (type->kind == TYPE_CLASS || type->kind == TYPE_STRUCT)) {
        zan_istr_t name = {(char *)"op_eq", 5};
        zan_symbol_t *op = get_method_sym(type->sym, name);
        int fi = irgen_find_function(g, op);
        if (fi >= 0) {
            LLVMValueRef args[] = {left, right};
            return zan_call2(g->builder, g->functions[fi].fn_type,
                             g->functions[fi].fn, args, 2, "eq.op");
        }
    }
    if (type && type->kind == TYPE_STRING) {
        LLVMTypeRef i8ptr = LLVMPointerType(
            LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
        if (LLVMGetTypeKind(LLVMTypeOf(left)) == LLVMIntegerTypeKind) {
            left = LLVMBuildIntToPtr(g->builder, left, i8ptr, "str.lp");
        } else if (LLVMTypeOf(left) != i8ptr) {
            left = LLVMBuildBitCast(g->builder, left, i8ptr, "str.lbc");
        }
        if (LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMIntegerTypeKind) {
            right = LLVMBuildIntToPtr(g->builder, right, i8ptr, "str.rp");
        } else if (LLVMTypeOf(right) != i8ptr) {
            right = LLVMBuildBitCast(g->builder, right, i8ptr, "str.rbc");
        }
        LLVMValueRef lc = emit_str_nonnull(g, left);
        LLVMValueRef rc = emit_str_nonnull(g, right);
        /*
         * Length-aware ordinal compare, like the ==/!= lowering: strcmp
         * truncated at the first embedded NUL.
         */
        LLVMValueRef ocmp = get_str_ordinal_cmp_fn(g, (zan_loc_t){0});
        LLVMValueRef cmp = zan_call2(
            g->builder, LLVMGlobalGetValueType(ocmp), ocmp,
            (LLVMValueRef[]){lc, rc}, 2, "eq.str");
        return zan_icmp(g->builder, LLVMIntEQ, cmp,
                        LLVMConstInt(i32, 0, 0), "eq.s");
    }
    if (type && type->kind == TYPE_STRUCT) {
        LLVMTypeRef st = map_type(g, type);
        if (st && LLVMGetTypeKind(st) == LLVMStructTypeKind) {
            if (LLVMGetTypeKind(LLVMTypeOf(left)) == LLVMPointerTypeKind) {
                left = LLVMBuildLoad2(g->builder, st, left, "eq.ld.l");
            }
            if (LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMPointerTypeKind) {
                right = LLVMBuildLoad2(g->builder, st, right, "eq.ld.r");
            }
        }
    }
    LLVMTypeRef lt = LLVMTypeOf(left);
    LLVMTypeRef rt = LLVMTypeOf(right);
    if (LLVMGetTypeKind(lt) == LLVMStructTypeKind && lt == rt) {
        unsigned count = LLVMCountStructElementTypes(lt);
        LLVMValueRef result = LLVMConstInt(
            LLVMInt1TypeInContext(g->ctx), 1, 0);
        for (unsigned i = 0; i < count; i++) {
            LLVMValueRef lv = LLVMBuildExtractValue(
                g->builder, left, i, "eq.l");
            LLVMValueRef rv = LLVMBuildExtractValue(
                g->builder, right, i, "eq.r");
            zan_type_t *field_type = NULL;
            if (type && type->sym) {
                int cur_idx = 0;
                for (int m = 0; m < type->sym->member_count; m++) {
                    zan_symbol_t *sm = type->sym->members[m];
                    if ((sm->kind == SYM_FIELD || sm->kind == SYM_PROPERTY) &&
                        !field_member_is_static(sm)) {
                        if (cur_idx == (int)i) {
                            field_type = sm->type;
                            break;
                        }
                        cur_idx++;
                    }
                }
            }
            LLVMValueRef equal = NULL;
            if (field_type) {
                equal = emit_typed_equality(g, field_type, lv, rv);
            } else {
                LLVMTypeKind kind = LLVMGetTypeKind(LLVMTypeOf(lv));
                if (kind == LLVMDoubleTypeKind || kind == LLVMFloatTypeKind) {
                    equal = LLVMBuildFCmp(
                        g->builder, LLVMRealOEQ, lv, rv, "eq.f");
                } else if (kind == LLVMPointerTypeKind) {
                    equal = LLVMBuildICmp(g->builder, LLVMIntEQ, lv, rv, "eq.p");
                } else {
                    equal = zan_icmp(g->builder, LLVMIntEQ, lv, rv, "eq.i");
                }
            }
            result = LLVMBuildAnd(g->builder, result, equal, "eq.all");
        }
        return result;
    }
    if (LLVMGetTypeKind(lt) == LLVMPointerTypeKind &&
        LLVMGetTypeKind(rt) == LLVMPointerTypeKind) {
        if (lt != rt) right = LLVMBuildBitCast(g->builder, right, lt, "eq.bc");
        return LLVMBuildICmp(g->builder, LLVMIntEQ, left, right, "eq.ptr");
    }
    if (LLVMGetTypeKind(lt) == LLVMDoubleTypeKind ||
        LLVMGetTypeKind(lt) == LLVMFloatTypeKind) {
        return LLVMBuildFCmp(g->builder, LLVMRealOEQ, left, right, "eq.num");
    }
    if (LLVMGetTypeKind(lt) == LLVMIntegerTypeKind &&
        LLVMGetTypeKind(rt) == LLVMIntegerTypeKind && lt != rt) {
        right = coerce_int_to(g, right, lt);
    }
    return zan_icmp(g->builder, LLVMIntEQ, left, right, "eq.raw");
}

/* 双操作数就绪后发射对应二元运算符指令 */
static LLVMValueRef emit_binary_op_values(zan_irgen_t *g, zan_ast_node_t *expr,
                                          LLVMValueRef left, LLVMValueRef right,
                                          local_scope_t *locals);

/* checked 整数运算：发射溢出捕获与断言指令 */
enum { CHK_ADD, CHK_SUB, CHK_MUL };
static LLVMValueRef emit_checked_int_arith(zan_irgen_t *g, zan_ast_node_t *expr,
                                           LLVMValueRef left, LLVMValueRef right,
                                           int kind, local_scope_t *locals);

/*
 * `int?` and friends in a binary expression: null propagates through the
 * arithmetic operators, a comparison with a null operand is false, and `??`
 * unwraps.
 */
static LLVMValueRef emit_nullable_binary(zan_irgen_t *g, zan_ast_node_t *expr,
                                         LLVMValueRef left, LLVMValueRef right,
                                         local_scope_t *locals);

static LLVMValueRef emit_expr_binary(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* 短路逻辑运算符（&& / ||）：左操作数满足短路条件时不求值右操作数 */
        if (expr->binary.op == TK_AMP_AMP || expr->binary.op == TK_PIPE_PIPE) {
            bool is_and = (expr->binary.op == TK_AMP_AMP);
            LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
            LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));

            LLVMValueRef lval = emit_expr(g, expr->binary.left, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(lval)) != LLVMIntegerTypeKind ||
                LLVMGetIntTypeWidth(LLVMTypeOf(lval)) != 1) {
                lval = zan_tobool(g->builder, lval, "tobool");
            }
            LLVMBasicBlockRef left_bb = LLVMGetInsertBlock(g->builder);
            LLVMBasicBlockRef rhs_bb  = LLVMAppendBasicBlockInContext(g->ctx, fn, "sc.rhs");
            LLVMBasicBlockRef merge   = LLVMAppendBasicBlockInContext(g->ctx, fn, "sc.end");
            /*
             * AND: if left is true, test right; else short-circuit false.
             * OR:  if left is true, short-circuit true; else test right.
             */
            if (is_and)
                LLVMBuildCondBr(g->builder, lval, rhs_bb, merge);
            else
                LLVMBuildCondBr(g->builder, lval, merge, rhs_bb);

            LLVMPositionBuilderAtEnd(g->builder, rhs_bb);
            LLVMValueRef rval = emit_expr(g, expr->binary.right, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(rval)) != LLVMIntegerTypeKind ||
                LLVMGetIntTypeWidth(LLVMTypeOf(rval)) != 1) {
                rval = zan_tobool(g->builder, rval, "tobool");
            }
            LLVMBasicBlockRef rhs_end = LLVMGetInsertBlock(g->builder);
            LLVMBuildBr(g->builder, merge);

            LLVMPositionBuilderAtEnd(g->builder, merge);
            LLVMValueRef phi = LLVMBuildPhi(g->builder, i1, "sc");
            /* short-circuit constant: AND -> false, OR -> true */
            LLVMValueRef sc_const = LLVMConstInt(i1, is_and ? 0 : 1, 0);
            LLVMValueRef vals[] = { sc_const, rval };
            LLVMBasicBlockRef bbs[] = { left_bb, rhs_end };
            LLVMAddIncoming(phi, vals, bbs, 2);
            return phi;
        }

        /*
         * A chain of 3+ string `+` parts is emitted as one flattened
         * allocation instead of pairwise (see emit_str_concat_n).
         */
        if (expr->binary.op == TK_PLUS && is_str_concat_node(g, expr, locals) &&
            (is_str_concat_node(g, expr->binary.left, locals) ||
             is_str_concat_node(g, expr->binary.right, locals))) {
            return emit_str_concat_n(g, expr, locals);
        }

        LLVMValueRef left = emit_expr(g, expr->binary.left, locals);
        LLVMValueRef right = emit_expr(g, expr->binary.right, locals);

        /*
         * Operator overloading: if left operand is a user class instance,
         * look for a static op_add/op_sub/etc method and call it.
         */
        {
            zan_type_t *ltype = infer_expr_type(g, expr->binary.left, locals);
            /*
             * comparisons against the null literal stay reference compares,
             * so an op_eq body can null-check without recursing into itself
             */
            int null_cmp = expr->binary.left->kind == AST_NULL_LITERAL ||
                           expr->binary.right->kind == AST_NULL_LITERAL;
            if (!null_cmp && ltype && ltype->sym &&
                (ltype->kind == TYPE_CLASS || ltype->kind == TYPE_STRUCT)) {
                const char *op_name = NULL;
                switch (expr->binary.op) {
                case TK_PLUS:       op_name = "op_add"; break;
                case TK_MINUS:      op_name = "op_sub"; break;
                case TK_STAR:       op_name = "op_mul"; break;
                case TK_SLASH:      op_name = "op_div"; break;
                case TK_PERCENT:    op_name = "op_mod"; break;
                case TK_EQ_EQ:      op_name = "op_eq"; break;
                case TK_BANG_EQ:    op_name = "op_neq"; break;
                case TK_LESS:       op_name = "op_lt"; break;
                case TK_GREATER:    op_name = "op_gt"; break;
                case TK_LESS_EQ:    op_name = "op_le"; break;
                case TK_GREATER_EQ: op_name = "op_ge"; break;
                default: break;
                }
                if (op_name) {
                    /* 在类中解析重载的自定义运算符方法 */
                    zan_istr_t op_istr = { (char *)op_name, (int)strlen(op_name) };
                    zan_ast_node_t *op_probe = zan_ast_new(g->arena, AST_CALL,
                                                           expr->loc);
                    op_probe->call.callee = NULL;
                    zan_ast_list_init(&op_probe->call.args);
                    zan_ast_list_init(&op_probe->call.type_args);
                    zan_ast_list_push(&op_probe->call.args, expr->binary.right,
                                      g->arena);
                    zan_symbol_t *op_sym = resolve_op_overload(g, ltype->sym,
                                                               op_istr, op_probe,
                                                               locals);
                    if (op_sym) {
                        for (int fi = irgen_find_function(g, op_sym); fi >= 0; fi = -1) {
                            if (g->functions[fi].sym == op_sym) {
                                /* 调用前将实参类型适配转换至声明形参类型 */
                                LLVMValueRef cargs[2] = { left, right };
                                /* 非静态运算符绑定隐式 this 接收者 */
                                bool op_is_static =
                                    (op_sym->modifiers & MOD_STATIC) != 0;
                                if (!op_is_static &&
                                    LLVMGetTypeKind(LLVMTypeOf(cargs[0])) == LLVMStructTypeKind) {
                                    LLVMValueRef rslot = emit_entry_alloca(g,
                                        LLVMTypeOf(cargs[0]), "opb.recv");
                                    LLVMBuildStore(g->builder, cargs[0], rslot);
                                    cargs[0] = rslot;
                                }
                                /* 通过泛型特化具体方法发射调用 */
                                LLVMTypeRef mft = g->functions[fi].fn_type;
                                LLVMValueRef mfn = route_generic_method(g, ltype,
                                    op_sym, g->functions[fi].fn, mft, &mft);
                                zan_type_t *p1 = method_param_type_at(g, op_sym, 1,
                                    expr, expr->binary.left, locals);
                                bool op_generic = p1 && type_mentions_tp(p1);
                                if (p1) p1 = subst_type_param_deep(g, p1, ltype);
                                if (p1 && !op_generic) {
                                    LLVMTypeRef p1t = map_type(g, p1);
                                    if (LLVMTypeOf(cargs[1]) != p1t)
                                        cargs[1] = coerce_int_to(g, cargs[1], p1t);
                                }
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "opcall";
                                LLVMValueRef opres = zan_call2(g->builder,
                                    mft, mfn, cargs, 2, cn);
                                /* 原地委托组合赋值 (E += obj.OnX)：包装为委托实例后执行组合操作 */
                                /* 运算符重载实参入栈发射 */
                                if (expr_yields_delegate_value(g, expr->binary.right, locals))
                                    emit_closure_release(g, right);
                                else
                                    emit_release_owned_call_temp(g, expr->binary.right, right, locals);
                                emit_release_owned_call_temp(g, expr->binary.left, left, locals);
                                return opres;
                            }
                        }
                    }
                }
            }
        }

        /*
         * A nullable operand lifts the operator (below); a string one means
         * this is a concatenation and is handled further down.
         */
        if ((llvm_is_nullable(LLVMTypeOf(left)) ||
             llvm_is_nullable(LLVMTypeOf(right))) &&
            !is_string_expr(g, expr->binary.left, locals) &&
            !is_string_expr(g, expr->binary.right, locals))
            return emit_nullable_binary(g, expr, left, right, locals);

        /* 结构体逐字段相等性比较 */
        if ((expr->binary.op == TK_EQ_EQ || expr->binary.op == TK_BANG_EQ) &&
            LLVMGetTypeKind(LLVMTypeOf(left)) == LLVMStructTypeKind &&
            LLVMTypeOf(left) == LLVMTypeOf(right)) {
            LLVMTypeRef st = LLVMTypeOf(left);
            unsigned nf = LLVMCountStructElementTypes(st);
            LLVMValueRef acc = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0);
            for (unsigned fi = 0; fi < nf; fi++) {
                LLVMValueRef lf = LLVMBuildExtractValue(g->builder, left, fi, "sq.l");
                LLVMValueRef rf = LLVMBuildExtractValue(g->builder, right, fi, "sq.r");
                LLVMTypeKind fk = LLVMGetTypeKind(LLVMTypeOf(lf));
                LLVMValueRef eq;
                if (fk == LLVMDoubleTypeKind || fk == LLVMFloatTypeKind)
                    eq = LLVMBuildFCmp(g->builder, LLVMRealOEQ, lf, rf, "sq.f");
                else if (fk == LLVMPointerTypeKind)
                    eq = LLVMBuildICmp(g->builder, LLVMIntEQ,
                        LLVMBuildPtrToInt(g->builder, lf, LLVMInt64TypeInContext(g->ctx), "sq.lp"),
                        LLVMBuildPtrToInt(g->builder, rf, LLVMInt64TypeInContext(g->ctx), "sq.rp"),
                        "sq.p");
                else
                    eq = zan_icmp(g->builder, LLVMIntEQ, lf, rf, "sq.i");
                acc = LLVMBuildAnd(g->builder, acc, eq, "sq.and");
            }
            if (expr->binary.op == TK_BANG_EQ)
                acc = LLVMBuildNot(g->builder, acc, "sq.not");
            return acc;
        }

        bool both_ptr =
            LLVMGetTypeKind(LLVMTypeOf(left)) == LLVMPointerTypeKind &&
            LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMPointerTypeKind;

        /*
         * Two delegates compare by function + bound target, as in C#, so
         * `event -= obj.Handler` finds the handler it subscribed.
         */
        if ((expr->binary.op == TK_EQ_EQ || expr->binary.op == TK_BANG_EQ) &&
            both_ptr) {
            zan_type_t *lt = infer_expr_type(g, expr->binary.left, locals);
            zan_type_t *rt = infer_expr_type(g, expr->binary.right, locals);
            if (lt && rt && lt->kind == TYPE_DELEGATE && rt->kind == TYPE_DELEGATE) {
                LLVMValueRef eq = emit_delegate_equals(g, left, right);
                return expr->binary.op == TK_BANG_EQ
                    ? LLVMBuildNot(g->builder, eq, "dneq") : eq;
            }
        }
        bool str_operand = is_string_expr(g, expr->binary.left, locals) ||
                           is_string_expr(g, expr->binary.right, locals);

        /* 字符串拼接 (a + b)：操作数含 string 时发射 String.Concat */
        if (expr->binary.op == TK_PLUS && str_operand) {
            LLVMValueRef ls = emit_to_cstr_of(g, left, expr->binary.left, locals);
            LLVMValueRef rs = emit_to_cstr_of(g, right, expr->binary.right, locals);
            LLVMValueRef out = emit_str_concat(g, ls, rs);
            if (!is_string_expr(g, expr->binary.left, locals) ||
                expr_yields_owned_rc_value(g, expr->binary.left, locals)) {
                emit_string_release(g, ls);
            }
            if (!is_string_expr(g, expr->binary.right, locals) ||
                expr_yields_owned_rc_value(g, expr->binary.right, locals)) {
                emit_string_release(g, rs);
            }
            return out;
        }

        /*
         * string equality: route `==`/`!=` on strings through strcmp. `== null`
         * keeps pointer semantics. A NULL string operand compares as "" (C#),
         * so strcmp never receives a NULL pointer.
         */
        if ((expr->binary.op == TK_EQ_EQ || expr->binary.op == TK_BANG_EQ) &&
            both_ptr && str_operand &&
            expr->binary.left->kind != AST_NULL_LITERAL &&
            expr->binary.right->kind != AST_NULL_LITERAL) {
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef lc = emit_str_nonnull(g, left);
            LLVMValueRef rc = emit_str_nonnull(g, right);
            /*
             * Length-aware ordinal compare: strcmp truncated a managed string
             * at its first embedded NUL ("a\0b" == "a" was true).
             */
            LLVMValueRef ocmp = get_str_ordinal_cmp_fn(g, expr->loc);
            LLVMValueRef r = zan_call2(g->builder,
                LLVMGlobalGetValueType(ocmp), ocmp,
                (LLVMValueRef[]){ lc, rc }, 2, "scmp");
            LLVMValueRef seq = zan_icmp(g->builder,
                expr->binary.op == TK_EQ_EQ ? LLVMIntEQ : LLVMIntNE,
                r, LLVMConstInt(i32t, 0, 0), "seq");
            if (is_string_expr(g, expr->binary.left, locals) &&
                expr_yields_owned_rc_value(g, expr->binary.left, locals)) {
                emit_string_release(g, left);
            }
            if (is_string_expr(g, expr->binary.right, locals) &&
                expr_yields_owned_rc_value(g, expr->binary.right, locals)) {
                emit_string_release(g, right);
            }
            return seq;
        }

        /* 字符串大小比较：将 < / <= / > / >= 路由至 strcmp 字符序比对 */
        if ((expr->binary.op == TK_LESS || expr->binary.op == TK_LESS_EQ ||
             expr->binary.op == TK_GREATER || expr->binary.op == TK_GREATER_EQ) &&
            both_ptr && str_operand &&
            expr->binary.left->kind != AST_NULL_LITERAL &&
            expr->binary.right->kind != AST_NULL_LITERAL) {
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef lc = emit_str_nonnull(g, left);
            LLVMValueRef rc = emit_str_nonnull(g, right);
            /*
             * Same length-aware ordinal compare as ==: ordering also
             * truncated at the first embedded NUL.
             */
            LLVMValueRef ocmp = get_str_ordinal_cmp_fn(g, expr->loc);
            LLVMValueRef r = zan_call2(g->builder,
                LLVMGlobalGetValueType(ocmp), ocmp,
                (LLVMValueRef[]){ lc, rc }, 2, "scmp");
            LLVMIntPredicate pred =
                expr->binary.op == TK_LESS       ? LLVMIntSLT :
                expr->binary.op == TK_LESS_EQ    ? LLVMIntSLE :
                expr->binary.op == TK_GREATER    ? LLVMIntSGT :
                                                   LLVMIntSGE;
            LLVMValueRef sord = zan_icmp(g->builder, pred,
                r, LLVMConstInt(i32t, 0, 0), "sord");
            if (is_string_expr(g, expr->binary.left, locals) &&
                expr_yields_owned_rc_value(g, expr->binary.left, locals)) {
                emit_string_release(g, left);
            }
            if (is_string_expr(g, expr->binary.right, locals) &&
                expr_yields_owned_rc_value(g, expr->binary.right, locals)) {
                emit_string_release(g, right);
            }
            return sord;
        }

        /* 指针与空值相等性判断 (== null) */
        if ((expr->binary.op == TK_EQ_EQ || expr->binary.op == TK_BANG_EQ) && both_ptr) {
            LLVMValueRef rcast = right;
            if (LLVMTypeOf(right) != LLVMTypeOf(left))
                rcast = LLVMBuildBitCast(g->builder, right, LLVMTypeOf(left), "pcast");
            LLVMValueRef pcmp = zan_icmp(g->builder,
                expr->binary.op == TK_EQ_EQ ? LLVMIntEQ : LLVMIntNE,
                left, rcast, "pcmp");
            emit_release_owned_call_temp(g, expr->binary.left, left, locals);
            emit_release_owned_call_temp(g, expr->binary.right, right, locals);
            return pcmp;
        }

        return emit_binary_op_values(g, expr, left, right, locals);
}

/* 判断表达式运行时是否可能包含 string 对象 */
static bool recv_is_stringlike(zan_irgen_t *g, zan_ast_node_t *e,
                               local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    if (!t) return true;
    return t->kind == TYPE_STRING || t->kind == TYPE_OBJECT ||
           t->kind == TYPE_ERROR || t->kind == TYPE_TYPE_PARAM;
}

/* 移位计数按左操作数位宽取模 (1 << 33 规范化为 1 << 1) */
static LLVMValueRef mask_shift_count(zan_irgen_t *g, zan_ast_node_t *lhs,
                                     LLVMValueRef count, local_scope_t *locals) {
    if (LLVMGetTypeKind(LLVMTypeOf(count)) != LLVMIntegerTypeKind)
        return count;
    unsigned bits = 64;
    zan_type_t *lt = infer_expr_type(g, lhs, locals);
    if (lt) {
        switch (lt->kind) {
        case TYPE_BYTE: case TYPE_SBYTE:
        case TYPE_SHORT: case TYPE_USHORT:
        case TYPE_CHAR:
        case TYPE_INT: case TYPE_UINT:
            bits = 32; /* C# promotes anything narrower than int to int */
            break;
        default:
            break;
        }
    }
    return zan_and(g->builder, count,
                   LLVMConstInt(LLVMTypeOf(count), bits - 1, 0), "sh.mask");
}

static LLVMValueRef emit_binary_op_values(zan_irgen_t *g, zan_ast_node_t *expr,
                                          LLVMValueRef left, LLVMValueRef right,
                                          local_scope_t *locals) {
        /*
         * reconcile mixed-width integer operands (e.g. i32 int vs i64 length)
         * and convert an integer meeting a float/double one
         */
        coerce_int_pair(g, &left, &right);

        /* 双操作数类型对齐后根据统一类型发射相应指令 */
        LLVMTypeRef left_type = LLVMTypeOf(left);
        bool is_float = (LLVMGetTypeKind(left_type) == LLVMDoubleTypeKind ||
                         LLVMGetTypeKind(left_type) == LLVMFloatTypeKind ||
                         LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMDoubleTypeKind ||
                         LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMFloatTypeKind);

        /* ulong/uint operands select unsigned division/remainder/shift/compare */
        bool is_unsigned = !is_float &&
            (expr_is_ulong(g, expr->binary.left, locals) ||
             expr_is_ulong(g, expr->binary.right, locals) ||
             expr_is_uint(g, expr->binary.left, locals) ||
             expr_is_uint(g, expr->binary.right, locals));

        /* checked 上下文下的整数溢出捕获陷阱 */
        int check_ctx = expr->binary.checked
            ? (expr->binary.checked > 0 ? 1 : -1)
            : (g->irgen_checked_depth > 0 ? 1 : 0);
        bool want_overflow_check = check_ctx > 0 && !is_float;
        LLVMTypeRef res_ty = LLVMTypeOf(left);
        if (want_overflow_check &&
            !(LLVMGetTypeKind(res_ty) == LLVMIntegerTypeKind &&
              LLVMGetIntTypeWidth(res_ty) > 1))
            want_overflow_check = false;

        switch (expr->binary.op) {
        case TK_PLUS:
            if (is_float) return LLVMBuildFAdd(g->builder, left, right, "add");
            if (want_overflow_check)
                return emit_checked_int_arith(g, expr, left, right,
                                              CHK_ADD, locals);
            return zan_add(g->builder, left, right, "add");
        case TK_MINUS:
            if (is_float) return LLVMBuildFSub(g->builder, left, right, "sub");
            if (want_overflow_check)
                return emit_checked_int_arith(g, expr, left, right,
                                              CHK_SUB, locals);
            return zan_sub(g->builder, left, right, "sub");
        case TK_STAR:
            if (is_float) return LLVMBuildFMul(g->builder, left, right, "mul");
            if (want_overflow_check)
                return emit_checked_int_arith(g, expr, left, right,
                                              CHK_MUL, locals);
            return zan_mul(g->builder, left, right, "mul");
        case TK_SLASH:
            if (is_float) return LLVMBuildFDiv(g->builder, left, right, "div");
            {
                /* 整数除法软路径零除与溢出防护 */
                LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(right), 0, 0);
                LLVMValueRef one = LLVMConstInt(LLVMTypeOf(right), 1, 0);
                LLVMValueRef is_zero = zan_icmp(g->builder, LLVMIntEQ, right, zero, "divz");
                emit_runtime_check(g, is_zero, expr->loc, "division by zero");
                LLVMValueRef unsafe = is_zero;
                if (!is_unsigned) {
                    unsigned w = LLVMGetIntTypeWidth(LLVMTypeOf(left));
                    LLVMValueRef smin = LLVMConstInt(LLVMTypeOf(left),
                        (unsigned long long)1 << (w - 1), 1);
                    LLVMValueRef minus_one = LLVMConstAllOnes(LLVMTypeOf(right));
                    LLVMValueRef is_min = zan_icmp(g->builder, LLVMIntEQ, left, smin, "divmin");
                    LLVMValueRef is_m1 = zan_icmp(g->builder, LLVMIntEQ, right, minus_one, "divm1");
                    unsafe = LLVMBuildOr(g->builder, unsafe,
                        LLVMBuildAnd(g->builder, is_min, is_m1, "divovf"),
                        "divunsafe");
                }
                LLVMValueRef divisor = LLVMBuildSelect(g->builder, unsafe,
                    one, right, "divz.safe");
                return is_unsigned ? zan_udiv(g->builder, left, divisor, "div")
                                   : zan_sdiv(g->builder, left, divisor, "div");
            }
        case TK_PERCENT:
            if (is_float) return LLVMBuildFRem(g->builder, left, right, "rem");
            {
                LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(right), 0, 0);
                LLVMValueRef one = LLVMConstInt(LLVMTypeOf(right), 1, 0);
                LLVMValueRef is_zero = zan_icmp(g->builder, LLVMIntEQ, right, zero, "remz");
                emit_runtime_check(g, is_zero, expr->loc, "division by zero (modulo)");
                LLVMValueRef unsafe = is_zero;
                if (!is_unsigned) {
                    unsigned w = LLVMGetIntTypeWidth(LLVMTypeOf(left));
                    LLVMValueRef smin = LLVMConstInt(LLVMTypeOf(left),
                        (unsigned long long)1 << (w - 1), 1);
                    LLVMValueRef minus_one = LLVMConstAllOnes(LLVMTypeOf(right));
                    LLVMValueRef is_min = zan_icmp(g->builder, LLVMIntEQ, left, smin, "remmin");
                    LLVMValueRef is_m1 = zan_icmp(g->builder, LLVMIntEQ, right, minus_one, "remm1");
                    unsafe = LLVMBuildOr(g->builder, unsafe,
                        LLVMBuildAnd(g->builder, is_min, is_m1, "removf"),
                        "remunsafe");
                }
                LLVMValueRef divisor = LLVMBuildSelect(g->builder, unsafe,
                    one, right, "remz.safe");
                return is_unsigned ? zan_urem(g->builder, left, divisor, "rem")
                                   : zan_srem(g->builder, left, divisor, "rem");
            }
        case TK_AMP:
            return zan_and(g->builder, left, right, "and");
        case TK_PIPE:
            return zan_or(g->builder, left, right, "or");
        case TK_CARET:
            return zan_xor(g->builder, left, right, "xor");
        case TK_LESS_LESS:
            right = mask_shift_count(g, expr->binary.left, right, locals);
            return zan_shl(g->builder, left, right, "shl");
        case TK_GREATER_GREATER:
            right = mask_shift_count(g, expr->binary.left, right, locals);
            return is_unsigned ? zan_lshr(g->builder, left, right, "shr")
                               : zan_ashr(g->builder, left, right, "shr");
        case TK_GREATER_GREATER_GREATER:
            /* C# >>>: always logical, whatever the operand's signedness */
            right = mask_shift_count(g, expr->binary.left, right, locals);
            return zan_lshr(g->builder, left, right, "ushr");
        case TK_EQ_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOEQ, left, right, "eq")
                            : zan_icmp(g->builder, LLVMIntEQ, left, right, "eq");
        case TK_BANG_EQ:
            /*
             * IEEE: x != y is !(x == y), so NaN != anything must hold --
             * the ordered ONE predicate answers false there.
             */
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealUNE, left, right, "ne")
                            : zan_icmp(g->builder, LLVMIntNE, left, right, "ne");
        case TK_LESS:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOLT, left, right, "lt")
                            : zan_icmp(g->builder, is_unsigned ? LLVMIntULT : LLVMIntSLT, left, right, "lt");
        case TK_GREATER:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOGT, left, right, "gt")
                            : zan_icmp(g->builder, is_unsigned ? LLVMIntUGT : LLVMIntSGT, left, right, "gt");
        case TK_LESS_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOLE, left, right, "le")
                            : zan_icmp(g->builder, is_unsigned ? LLVMIntULE : LLVMIntSLE, left, right, "le");
        case TK_GREATER_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOGE, left, right, "ge")
                            : zan_icmp(g->builder, is_unsigned ? LLVMIntUGE : LLVMIntSGE, left, right, "ge");
        case TK_QUESTION_QUESTION: {
            /* ?? null coalescing: if left != 0/null, use left, else right */
            LLVMValueRef is_null;
            if (LLVMGetTypeKind(left_type) == LLVMPointerTypeKind) {
                LLVMValueRef null_ptr = LLVMConstNull(left_type);
                is_null = zan_icmp(g->builder, LLVMIntEQ, left, null_ptr, "isnull");
            } else {
                is_null = zan_icmp(g->builder, LLVMIntEQ, left,
                    LLVMConstInt(left_type, 0, 0), "isnull");
            }
            LLVMValueRef coal_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef use_right = LLVMAppendBasicBlockInContext(g->ctx, coal_fn, "coal.r");
            LLVMBasicBlockRef merge = LLVMAppendBasicBlockInContext(g->ctx, coal_fn, "coal.m");
            LLVMBasicBlockRef left_bb = LLVMGetInsertBlock(g->builder);
            LLVMBuildCondBr(g->builder, is_null, use_right, merge);
            LLVMPositionBuilderAtEnd(g->builder, use_right);
            LLVMBuildBr(g->builder, merge);
            LLVMPositionBuilderAtEnd(g->builder, merge);
            LLVMValueRef phi = LLVMBuildPhi(g->builder, left_type, "coal");
            LLVMValueRef vals[] = { left, right };
            LLVMBasicBlockRef bbs[] = { left_bb, use_right };
            LLVMAddIncoming(phi, vals, bbs, 2);
            return phi;
        }
        default:
            return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
        }
}

/* checked 溢出检测整数算术实现 */
static LLVMValueRef emit_checked_int_arith(zan_irgen_t *g, zan_ast_node_t *expr,
                                           LLVMValueRef left, LLVMValueRef right,
                                           int kind, local_scope_t *locals) {
    LLVMTypeRef ty = LLVMTypeOf(left);
    unsigned bits = LLVMGetIntTypeWidth(ty);
    bool is_u64 = expr_is_ulong(g, expr->binary.left, locals) ||
                  expr_is_ulong(g, expr->binary.right, locals);
    /* uint 运算溢出范围检查 */
    bool is_u32 = !is_u64 &&
        (expr_is_uint(g, expr->binary.left, locals) ||
         expr_is_uint(g, expr->binary.right, locals)) &&
        !expr_is_longish(g, expr->binary.left, locals) &&
        !expr_is_longish(g, expr->binary.right, locals);
    bool is_unsigned = is_u64 || is_u32;
    /* 字面量整数运算在 i64 拓宽精度下求值 */
    bool clamp32 = !is_unsigned && bits > 32 &&
                   expr_is_int32(g, expr->binary.left, locals) &&
                   expr_is_int32(g, expr->binary.right, locals);
    LLVMValueRef res, ovf;
    if (is_u32) {
        /* uint 算术运算中间值类型适配与截断 */
        LLVMTypeRef i32t = LLVMIntTypeInContext(g->ctx, 32);
        LLVMTypeRef i64t = LLVMIntTypeInContext(g->ctx, 64);
        LLVMValueRef l64 = LLVMBuildZExt(g->builder,
            LLVMBuildTrunc(g->builder, left, i32t, "ck.u32l"), i64t, "ck.u32lz");
        LLVMValueRef r64 = LLVMBuildZExt(g->builder,
            LLVMBuildTrunc(g->builder, right, i32t, "ck.u32r"), i64t, "ck.u32rz");
        LLVMValueRef u32max = LLVMConstInt(i64t, 0xFFFFFFFFULL, 0);
        if (kind == CHK_ADD) {
            res = zan_add(g->builder, l64, r64, "ck.u32.add");
            ovf = zan_icmp(g->builder, LLVMIntUGT, res, u32max, "ck.u32.ovf");
        } else if (kind == CHK_SUB) {
            res = zan_sub(g->builder, l64, r64, "ck.u32.sub");
            /* wrapping below zero is exactly the overflow */
            ovf = zan_icmp(g->builder, LLVMIntSLT, res,
                LLVMConstInt(i64t, 0, 0), "ck.u32.ovf");
        } else {
            res = zan_mul(g->builder, l64, r64, "ck.u32.mul");
            ovf = zan_icmp(g->builder, LLVMIntUGT, res, u32max, "ck.u32.ovf");
        }
        res = LLVMBuildTrunc(g->builder, res, ty, "ck.u32.res");
    } else if (kind == CHK_ADD) {
        res = zan_add(g->builder, left, right, "ck.add");
        if (is_unsigned)
            ovf = zan_icmp(g->builder, LLVMIntULT, res, left, "ck.add.ovf");
        else if (clamp32) {
            /* res in i64: overflow iff res < INT32.MIN || res > INT32.MAX */
            LLVMValueRef lo = LLVMConstInt(ty, -(1LL << 31), 1);
            LLVMValueRef hi = LLVMConstInt(ty, (1LL << 31) - 1, 1);
            LLVMValueRef under = zan_icmp(g->builder, LLVMIntSLT, res, lo,
                "ck.lo");
            LLVMValueRef over = zan_icmp(g->builder, LLVMIntSGT, res, hi,
                "ck.hi");
            ovf = zan_or(g->builder, under, over, "ck.add.ovf");
        }
        else {
            /*
             * overflow iff operands share a sign and the result's sign
             * differs from left's: ovf = same_sign(lr) AND (res != left)
             */
            LLVMValueRef lneg = zan_icmp(g->builder, LLVMIntSLT, left,
                LLVMConstInt(ty, 0, 0), "ck.sl");
            LLVMValueRef rneg = zan_icmp(g->builder, LLVMIntSLT, right,
                LLVMConstInt(ty, 0, 0), "ck.sr");
            LLVMValueRef same_sign_lr = zan_icmp(g->builder, LLVMIntEQ,
                lneg, rneg, "ck.sign.lr");
            LLVMValueRef res_neg = zan_icmp(g->builder, LLVMIntSLT, res,
                LLVMConstInt(ty, 0, 0), "ck.sr2");
            LLVMValueRef res_diff_left = zan_xor(g->builder, res_neg, lneg,
                "ck.sign.r");
            ovf = zan_and(g->builder, same_sign_lr, res_diff_left,
                          "ck.add.ovf");
        }
    } else if (kind == CHK_SUB) {
        res = zan_sub(g->builder, left, right, "ck.sub");
        if (is_unsigned)
            ovf = zan_icmp(g->builder, LLVMIntULT, left, right, "ck.sub.ovf");
        else if (clamp32) {
            LLVMValueRef lo = LLVMConstInt(ty, -(1LL << 31), 1);
            LLVMValueRef hi = LLVMConstInt(ty, (1LL << 31) - 1, 1);
            LLVMValueRef under = zan_icmp(g->builder, LLVMIntSLT, res, lo,
                "ck.lo");
            LLVMValueRef over = zan_icmp(g->builder, LLVMIntSGT, res, hi,
                "ck.hi");
            ovf = zan_or(g->builder, under, over, "ck.sub.ovf");
        }
        else {
            /*
             * overflow iff operand signs differ AND the result's sign
             * differs from left's
             */
            LLVMValueRef lneg = zan_icmp(g->builder, LLVMIntSLT, left,
                LLVMConstInt(ty, 0, 0), "ck.sl");
            LLVMValueRef rneg = zan_icmp(g->builder, LLVMIntSLT, right,
                LLVMConstInt(ty, 0, 0), "ck.sr");
            LLVMValueRef diff_sign = zan_xor(g->builder, lneg, rneg,
                "ck.diff");
            LLVMValueRef res_neg = zan_icmp(g->builder, LLVMIntSLT, res,
                LLVMConstInt(ty, 0, 0), "ck.sr2");
            LLVMValueRef res_diff_left = zan_xor(g->builder, res_neg, lneg,
                "ck.sign.r");
            ovf = zan_and(g->builder, diff_sign, res_diff_left, "ck.sub.ovf");
        }
    } else { /* CHK_MUL */
        if (is_unsigned) {
            /* 乘法溢出检测：反除法校验结果是否回绕 */
            res = zan_mul(g->builder, left, right, "ck.mul");
            LLVMValueRef nonzero = zan_icmp(g->builder, LLVMIntNE, left,
                LLVMConstInt(ty, 0, 0), "ck.nz");
            LLVMValueRef dvs = LLVMBuildSelect(g->builder, nonzero, left,
                LLVMConstInt(ty, 1, 0), "ck.mul.dv");
            LLVMValueRef q = zan_udiv(g->builder, res, dvs, "ck.mul.q");
            LLVMValueRef back = zan_icmp(g->builder, LLVMIntNE, q, right,
                "ck.mul.back");
            ovf = zan_and(g->builder, nonzero, back, "ck.mul.ovf");
        } else {
            /* 双倍位宽乘法溢出校验 */
            LLVMTypeRef wide = LLVMIntTypeInContext(g->ctx, bits * 2);
            LLVMValueRef wl, wr, wp;
            wl = LLVMBuildSExt(g->builder, left, wide, "ck.mul.wl");
            wr = LLVMBuildSExt(g->builder, right, wide, "ck.mul.wr");
            wp = LLVMBuildMul(g->builder, wl, wr, "ck.mul.wp");
            LLVMValueRef narrow = LLVMBuildTrunc(g->builder, wp, ty, "ck.mul.tr");
            LLVMValueRef back = LLVMBuildSExt(g->builder, narrow, wide,
                "ck.mul.back");
            ovf = zan_icmp(g->builder, LLVMIntNE, wp, back, "ck.mul.ovf");
            /* the narrow product IS the (wrapped) result */
            res = narrow;
        }
    }
    emit_runtime_check(g, ovf, expr->loc,
        "integer overflow in checked operation");
    (void)locals;
    return res;
}

/*
 * A binary node with the same operands but a different operator, so a lifted
 * comparison can ask for the payload equality it needs.
 */
static zan_ast_node_t *binary_with_op(zan_irgen_t *g, zan_ast_node_t *expr,
                                      int op) {
    zan_ast_node_t *n = zan_ast_new(g->arena, AST_BINARY, expr->loc);
    n->binary.op = op;
    n->binary.left = expr->binary.left;
    n->binary.right = expr->binary.right;
    return n;
}

static LLVMValueRef emit_nullable_binary(zan_irgen_t *g, zan_ast_node_t *expr,
                                         LLVMValueRef left, LLVMValueRef right,
                                         local_scope_t *locals) {
    LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
    LLVMValueRef yes = LLVMConstInt(i1, 1, 0);
    int op = expr->binary.op;
    bool left_is_null_lit = expr->binary.left->kind == AST_NULL_LITERAL;
    bool right_is_null_lit = expr->binary.right->kind == AST_NULL_LITERAL;
    bool ln = llvm_is_nullable(LLVMTypeOf(left));
    bool rn = llvm_is_nullable(LLVMTypeOf(right));
    LLVMValueRef lhas = ln ? nullable_has_value(g, left) : yes;
    LLVMValueRef rhas = rn ? nullable_has_value(g, right) : yes;
    LLVMValueRef lval = ln ? nullable_get_payload(g, left) : left;
    LLVMValueRef rval = rn ? nullable_get_payload(g, right) : right;

    /* `v == null` / `v != null` reads the flag; the payload is irrelevant. */
    if ((op == TK_EQ_EQ || op == TK_BANG_EQ) &&
        (left_is_null_lit || right_is_null_lit)) {
        LLVMValueRef has = left_is_null_lit ? rhas : lhas;
        return op == TK_EQ_EQ ? LLVMBuildNot(g->builder, has, "nv.isnull") : has;
    }

    /*
     * `v ?? alt`: the payload when there is one. Both sides were evaluated
     * eagerly above, exactly as the non-nullable `??` does.
     */
    if (op == TK_QUESTION_QUESTION) {
        if (!ln) return left;
        if (rn) return LLVMBuildSelect(g->builder, lhas, left, right, "nv.coal");
        LLVMValueRef alt = coerce_int_to(g, right, LLVMTypeOf(lval));
        if (LLVMTypeOf(alt) != LLVMTypeOf(lval)) {
            lval = coerce_int_to(g, lval, LLVMTypeOf(right));
            alt = right;
        }
        if (LLVMTypeOf(alt) != LLVMTypeOf(lval)) return left;
        return LLVMBuildSelect(g->builder, lhas, lval, alt, "nv.coal");
    }

    LLVMValueRef both = LLVMBuildAnd(g->builder, lhas, rhas, "nv.both");

    if (op == TK_EQ_EQ || op == TK_BANG_EQ) {
        /* 可空类型相等性判断：均为 null 或内部值均相等 */
        LLVMValueRef eq = emit_binary_op_values(g,
            binary_with_op(g, expr, TK_EQ_EQ), lval, rval, locals);
        LLVMValueRef neither = LLVMBuildAnd(g->builder,
            LLVMBuildNot(g->builder, lhas, "nv.ln"),
            LLVMBuildNot(g->builder, rhas, "nv.rn"), "nv.neither");
        LLVMValueRef same = LLVMBuildOr(g->builder, neither,
            LLVMBuildAnd(g->builder, both, eq, "nv.veq"), "nv.eq");
        return op == TK_EQ_EQ ? same
                              : LLVMBuildNot(g->builder, same, "nv.ne");
    }
    if (op == TK_LESS || op == TK_LESS_EQ ||
        op == TK_GREATER || op == TK_GREATER_EQ) {
        /* An ordering comparison involving a null operand is false. */
        LLVMValueRef cmp = emit_binary_op_values(g, expr, lval, rval, locals);
        return LLVMBuildAnd(g->builder, both, cmp, "nv.cmp");
    }

    /* 可空类型四则运算：任一操作数为 null 则结果为 null */
    if (op == TK_SLASH || op == TK_PERCENT) {
        LLVMTypeRef rt = LLVMTypeOf(rval);
        LLVMValueRef one = LLVMGetTypeKind(rt) == LLVMDoubleTypeKind ||
                           LLVMGetTypeKind(rt) == LLVMFloatTypeKind
            ? LLVMConstReal(rt, 1.0)
            : LLVMConstInt(rt, 1, 0);
        rval = LLVMBuildSelect(g->builder, both, rval, one, "nv.div1");
    }
    LLVMValueRef res = emit_binary_op_values(g, expr, lval, rval, locals);
    LLVMTypeRef nty = nullable_type_of(g, LLVMTypeOf(res));
    LLVMValueRef some = nullable_some(g, nty, res);
    if (!some) return res;
    return LLVMBuildSelect(g->builder, both, some, nullable_none(nty), "nv.lift");
}

/*
 * Shared lowering for prefix/postfix ++/-- (defined after
 * emit_expr_assignment; its lvalue resolution mirrors the assignment paths).
 */
static LLVMValueRef emit_incdec_expr(zan_irgen_t *g, zan_ast_node_t *expr,
                                     local_scope_t *locals, int is_prefix);

static LLVMValueRef emit_expr_unary(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* Prefix ++/--: read the slot, +/-1, store, yield the new value. */
        if (expr->unary.op == TK_PLUS_PLUS || expr->unary.op == TK_MINUS_MINUS)
            return emit_incdec_expr(g, expr, locals, 1);
        LLVMValueRef operand = emit_expr(g, expr->unary.operand, locals);
        switch (expr->unary.op) {
        case TK_MINUS: {
            LLVMTypeRef t = LLVMTypeOf(operand);
            if (LLVMGetTypeKind(t) == LLVMDoubleTypeKind ||
                LLVMGetTypeKind(t) == LLVMFloatTypeKind) {
                return LLVMBuildFNeg(g->builder, operand, "neg");
            }
            return LLVMBuildNeg(g->builder, operand, "neg");
        }
        case TK_BANG: {
            /* 逻辑非与按位取反指令发射 */
            LLVMTypeRef ot = LLVMTypeOf(operand);
            LLVMTypeKind otk = LLVMGetTypeKind(ot);
            if (otk == LLVMIntegerTypeKind) {
                return zan_icmp(g->builder, LLVMIntEQ, operand,
                    LLVMConstInt(ot, 0, 0), "lnot");
            }
            if (otk == LLVMPointerTypeKind) {
                return zan_icmp(g->builder, LLVMIntEQ, operand,
                    LLVMConstNull(ot), "lnotp");
            }
            return LLVMBuildNot(g->builder, operand, "not");
        }
        case TK_TILDE:
            return LLVMBuildNot(g->builder, operand, "bnot");
        default:
            return operand;
        }
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

/* Binding<T> 属性与字段双向绑定原语降解 */

static int type_is_binding(zan_type_t *t) {
    return t && t->kind == TYPE_CLASS && t->name.len == 7 &&
           memcmp(t->name.str, "Binding", 7) == 0;
}

/* 泛型类上的委托字段签名特化与解析 */
static zan_type_t *subst_delegate_sig(zan_irgen_t *g, zan_type_t *t,
                                      zan_type_t *recv) {
    if (!t || t->kind != TYPE_DELEGATE || !recv || recv->type_arg_count <= 0)
        return t;
    bool needs = t->delegate_ret_type &&
                 t->delegate_ret_type->kind == TYPE_TYPE_PARAM;
    for (int i = 0; !needs && i < t->delegate_param_count; i++)
        needs = t->delegate_param_types[i] &&
                t->delegate_param_types[i]->kind == TYPE_TYPE_PARAM;
    if (!needs) return t;

    zan_type_t *out = (zan_type_t *)zan_arena_alloc(g->arena, sizeof(zan_type_t));
    *out = *t;
    out->delegate_ret_type = subst_type_param(t->delegate_ret_type, recv);
    if (t->delegate_param_count > 0) {
        out->delegate_param_types = (zan_type_t **)zan_arena_alloc(g->arena,
            sizeof(zan_type_t *) * (size_t)t->delegate_param_count);
        for (int i = 0; i < t->delegate_param_count; i++)
            out->delegate_param_types[i] =
                subst_type_param(t->delegate_param_types[i], recv);
    }
    return out;
}

/* Static type of an assignment target (NULL when unknown). */
static zan_type_t *assign_lhs_type(zan_irgen_t *g, zan_ast_node_t *lhs,
                                   local_scope_t *locals) {
    if (!lhs) return NULL;
    if (lhs->kind == AST_IDENTIFIER) {
        local_var_t *lv = local_find(locals, lhs->ident.name);
        if (lv) return lv->type;
        if (g->current_type_sym) {
            zan_symbol_t *fs = get_field_sym(g->current_type_sym, lhs->ident.name);
            if (fs) return subst_delegate_sig(g, fs->type, g->cur_inst);
        }
        return NULL;
    }
    if (lhs->kind == AST_MEMBER_ACCESS)
        return subst_delegate_sig(g, member_access_field_type(g, locals, lhs),
                                  infer_expr_type(g, lhs->member.object, locals));
    return NULL;
}

/* 为类字段合成或查询缓存的 getter/setter 访问器函数 */
static void get_binding_accessors(zan_irgen_t *g, zan_symbol_t *cls,
        zan_symbol_t *fs, LLVMValueRef *out_get, LLVMValueRef *out_set) {
    *out_get = NULL;
    *out_set = NULL;
    for (int i = 0; i < g->bind_acc_count; i++) {
        if (g->bind_accs[i].cls == cls && g->bind_accs[i].field == fs) {
            *out_get = g->bind_accs[i].get_fn;
            *out_set = g->bind_accs[i].set_fn;
            return;
        }
    }
    LLVMTypeRef st = get_struct_llvm_type(g, cls);
    int fi = get_field_index(cls, fs->name);
    if (!st || fi < 0 || !fs->type) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef vt = map_type(g, fs->type);

    /* remember where we were emitting */
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    LLVMValueRef saved_fn = g->current_fn;

    char name[512];
    snprintf(name, sizeof(name), "__zbind_get_%.*s_%.*s",
             (int)cls->name.len, cls->name.str, (int)fs->name.len, fs->name.str);
    LLVMTypeRef get_ty = LLVMFunctionType(vt, &i8ptr, 1, 0);
    LLVMValueRef get_fn = LLVMAddFunction(g->mod, name, get_ty);
    LLVMSetLinkage(get_fn, LLVMInternalLinkage);
    {
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, get_fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        g->current_fn = get_fn;
        LLVMValueRef obj = LLVMBuildBitCast(g->builder, LLVMGetParam(get_fn, 0),
            LLVMPointerType(st, 0), "obj");
        LLVMValueRef fptr = emit_field_ptr(g, cls, st, obj, fi, "fld");
        LLVMValueRef val;
        if (fs->modifiers & MOD_WEAK) {
            val = emit_weak_field_load(g, fptr, vt);
        } else {
            val = LLVMBuildLoad2(g->builder, vt, fptr, "val");
            if (is_rc_managed_type(fs->type))
                emit_rc_retain_for_type(g, fs->type, val);
        }
        LLVMBuildRet(g->builder, val);
    }

    snprintf(name, sizeof(name), "__zbind_set_%.*s_%.*s",
             (int)cls->name.len, cls->name.str, (int)fs->name.len, fs->name.str);
    LLVMTypeRef set_params[2] = { i8ptr, vt };
    LLVMTypeRef set_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), set_params, 2, 0);
    LLVMValueRef set_fn = LLVMAddFunction(g->mod, name, set_ty);
    LLVMSetLinkage(set_fn, LLVMInternalLinkage);
    {
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, set_fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        g->current_fn = set_fn;
        LLVMValueRef obj = LLVMBuildBitCast(g->builder, LLVMGetParam(set_fn, 0),
            LLVMPointerType(st, 0), "obj");
        LLVMValueRef v = LLVMGetParam(set_fn, 1);
        LLVMValueRef fptr = emit_field_ptr(g, cls, st, obj, fi, "fld");
        /* 弱引用字段写入防护：维护弱引用注册表槽位 */
        emit_rc_store_field(g, fs->type, fptr, v, NULL, NULL,
                            (fs->modifiers & MOD_WEAK) ? 1 : 0);
        LLVMBuildRetVoid(g->builder);
    }

    g->current_fn = saved_fn;
    if (saved_bb) LLVMPositionBuilderAtEnd(g->builder, saved_bb);

    if (ZAN_TAB_ENSURE(g->bind_accs, g->bind_acc_count, g->bind_acc_cap, 128)) {
        g->bind_accs[g->bind_acc_count].cls = cls;
        g->bind_accs[g->bind_acc_count].field = fs;
        g->bind_accs[g->bind_acc_count].get_fn = get_fn;
        g->bind_accs[g->bind_acc_count].set_fn = set_fn;
        g->bind_acc_count++;
    }
    *out_get = get_fn;
    *out_set = set_fn;
}

/* 为右值构建 Binding<T> 实例对象（返回 +1 持有引用） */
static LLVMValueRef emit_binding_value(zan_irgen_t *g, zan_type_t *bind_t,
        zan_ast_node_t *rhs, local_scope_t *locals) {
    /*
     * `b = null` clears the binding rather than wrapping null in a const one,
     * so an unbound slot stays testable with `b == null`.
     */
    if (rhs && rhs->kind == AST_NULL_LITERAL) return NULL;
    zan_symbol_t *bsym = bind_t->sym;
    if (!bsym) {
        zan_istr_t bn = { (char *)"Binding", 7 };
        bsym = zan_binder_lookup(g->binder, bn);
    }
    if (!bsym) return NULL;
    LLVMTypeRef st = get_struct_llvm_type(g, bsym);
    if (!st) return NULL;
    zan_istr_t n_target = { (char *)"target", 6 };
    zan_istr_t n_getter = { (char *)"getter", 6 };
    zan_istr_t n_setter = { (char *)"setter", 6 };
    zan_istr_t n_live = { (char *)"live", 4 };
    zan_istr_t n_const = { (char *)"constVal", 8 };
    int fi_target = get_field_index(bsym, n_target);
    int fi_getter = get_field_index(bsym, n_getter);
    int fi_setter = get_field_index(bsym, n_setter);
    int fi_live = get_field_index(bsym, n_live);
    int fi_const = get_field_index(bsym, n_const);
    if (fi_target < 0 || fi_getter < 0 || fi_setter < 0 ||
        fi_live < 0 || fi_const < 0) return NULL;
    zan_type_t *T = bind_t->type_arg_count > 0 ? bind_t->type_args[0] : NULL;

    /* 类内部裸字段名解析为 this 字段并绑定目标 */
    if (rhs && rhs->kind == AST_IDENTIFIER && g->current_this &&
        g->current_type_sym && !local_find(locals, rhs->ident.name)) {
        zan_symbol_t *own = get_field_sym(g->current_type_sym, rhs->ident.name);
        if (own && !field_member_is_static(own)) {
            zan_ast_node_t *self = zan_ast_new(g->arena, AST_THIS_EXPR, rhs->loc);
            zan_ast_node_t *ma = zan_ast_new(g->arena, AST_MEMBER_ACCESS, rhs->loc);
            ma->member.object = self;
            ma->member.name = rhs->ident.name;
            ma->member.null_cond = 0;
            rhs = ma;
        }
    }

    /* is the RHS a live-bindable field lvalue? */
    zan_symbol_t *cls = NULL;
    zan_symbol_t *ffs = NULL;
    if (rhs && rhs->kind == AST_MEMBER_ACCESS && !rhs->member.null_cond) {
        cls = expr_class_sym(g, rhs->member.object, locals);
        if (cls) {
            ffs = get_field_sym(cls, rhs->member.name);
            if (ffs && field_member_is_static(ffs)) ffs = NULL;
            if (ffs && !ffs->type) ffs = NULL;
            /* only bind live when the field's representation matches T */
            if (ffs && T && ffs->type->kind != T->kind) ffs = NULL;
            /* 绑定目标使用非持有弱引用，防止双向绑定循环引用导致内存泄漏 */
            if (ffs) {
                zan_type_t *ot = infer_expr_type(g, rhs->member.object, locals);
                if (ot && is_rc_managed_type(ot) &&
                    expr_yields_owned_rc_value(g, rhs->member.object, locals))
                    ffs = NULL;
            }
        }
    }
    LLVMValueRef get_fn = NULL, set_fn = NULL;
    if (ffs) {
        get_binding_accessors(g, cls, ffs, &get_fn, &set_fn);
        if (!get_fn || !set_fn) ffs = NULL;
    }

    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);

    /* heap-allocate the Binding object (same path as `new Binding<T>()`) */
    LLVMValueRef site_name = LLVMConstNull(i8ptr);
    LLVMValueRef site_val;
    {
        int site_idx = reserve_arc_site(g, bsym, bind_t, 0, NULL);
        site_val = arc_site_arg(g, site_idx);
        if (g->check_leaks) {
            char site_buf[600];
            const char *sfile = loc_site_file(g, rhs->loc);
            snprintf(site_buf, sizeof(site_buf), "%s:%u:%u",
                     sfile, rhs->loc.line, rhs->loc.col);
            site_name = zan_irgen_intern_string(g, site_buf);
        }
    }
    LLVMTypeRef alloc_fn_type = LLVMFunctionType(i8ptr,
        (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0);
    LLVMValueRef alloc_args[] = { LLVMSizeOf(st), site_val, site_name };
    LLVMValueRef raw = zan_call2(g->builder, alloc_fn_type, g->rt_alloc,
        alloc_args, 3, "bindobj");
    LLVMValueRef objp = LLVMBuildBitCast(g->builder, raw, LLVMPointerType(st, 0), "bindp");
    zan_store_fit(g, LLVMConstNull(st), objp);

    LLVMValueRef live_ptr = LLVMBuildStructGEP2(g->builder, st, objp, (unsigned)fi_live, "b.live");
    LLVMTypeRef live_t = LLVMStructGetTypeAtIndex(st, (unsigned)fi_live);

    if (ffs) {
        /* live binding: capture the target object and the accessor pair */
        LLVMValueRef obj_val = emit_expr(g, rhs->member.object, locals);
        LLVMValueRef tptr = LLVMBuildStructGEP2(g->builder, st, objp, (unsigned)fi_target, "b.tgt");
        LLVMTypeRef tgt_t = LLVMStructGetTypeAtIndex(st, (unsigned)fi_target);
        LLVMValueRef obj_cast = obj_val;
        if (LLVMTypeOf(obj_cast) != tgt_t &&
            LLVMGetTypeKind(LLVMTypeOf(obj_cast)) == LLVMPointerTypeKind)
            obj_cast = LLVMBuildBitCast(g->builder, obj_cast, tgt_t, "b.tgt.bc");
        /* 非持有弱引用绑定目标管理 */
        zan_store_fit(g, obj_cast, tptr);

        LLVMValueRef gptr = LLVMBuildStructGEP2(g->builder, st, objp, (unsigned)fi_getter, "b.get");
        LLVMTypeRef gslot_t = LLVMStructGetTypeAtIndex(st, (unsigned)fi_getter);
        zan_store_fit(g,
            LLVMBuildBitCast(g->builder,
                emit_binding_acc_delegate(g, get_fn,
                    LLVMGlobalGetValueType(get_fn)),
                gslot_t, "b.get.bc"), gptr);
        LLVMValueRef sptr = LLVMBuildStructGEP2(g->builder, st, objp, (unsigned)fi_setter, "b.set");
        LLVMTypeRef sslot_t = LLVMStructGetTypeAtIndex(st, (unsigned)fi_setter);
        zan_store_fit(g,
            LLVMBuildBitCast(g->builder,
                emit_binding_acc_delegate(g, set_fn,
                    LLVMGlobalGetValueType(set_fn)),
                sslot_t, "b.set.bc"), sptr);
        zan_store_fit(g, LLVMConstInt(live_t, 1, 0), live_ptr);
    } else {
        /* const binding: evaluate the RHS once and store it */
        LLVMValueRef v = emit_expr(g, rhs, locals);
        LLVMValueRef cptr = LLVMBuildStructGEP2(g->builder, st, objp, (unsigned)fi_const, "b.cv");
        LLVMTypeRef cslot_t = LLVMStructGetTypeAtIndex(st, (unsigned)fi_const);
        LLVMTypeKind vk = LLVMGetTypeKind(LLVMTypeOf(v));
        if (LLVMGetTypeKind(cslot_t) == LLVMIntegerTypeKind &&
            vk == LLVMIntegerTypeKind &&
            LLVMGetIntTypeWidth(LLVMTypeOf(v)) != LLVMGetIntTypeWidth(cslot_t)) {
            v = coerce_int_to(g, v, cslot_t);
        } else if (vk == LLVMPointerTypeKind &&
                   LLVMGetTypeKind(cslot_t) == LLVMPointerTypeKind &&
                   LLVMTypeOf(v) != cslot_t) {
            v = LLVMBuildBitCast(g->builder, v, cslot_t, "b.cv.bc");
        }
        if (T && is_rc_managed_type(T) &&
            !expr_yields_owned_rc_value(g, rhs, locals))
            emit_rc_retain_for_type(g, T, v);
        zan_store_fit(g, v, cptr);
        zan_store_fit(g, LLVMConstInt(live_t, 0, 0), live_ptr);
    }
    return LLVMBuildBitCast(g->builder, objp, i8ptr, "bindv");
}

/* 控制台颜色属性赋值跟踪槽 */
#define ZAN_CONSOLE_FG_DEFAULT 7 /* ConsoleColor.Gray */
#define ZAN_CONSOLE_BG_DEFAULT 0 /* ConsoleColor.Black */

static LLVMValueRef console_color_slot(zan_irgen_t *g, int is_bg) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    const char *nm = is_bg ? "__zan_console_bg" : "__zan_console_fg";
    LLVMValueRef slot = LLVMGetNamedGlobal(g->mod, nm);
    if (!slot) {
        slot = LLVMAddGlobal(g->mod, i64, nm);
        LLVMSetInitializer(slot, LLVMConstInt(i64,
            is_bg ? ZAN_CONSOLE_BG_DEFAULT : ZAN_CONSOLE_FG_DEFAULT, 0));
        LLVMSetLinkage(slot, LLVMPrivateLinkage);
    }
    return slot;
}

/* Read Console.ForegroundColor / Console.BackgroundColor. */
static LLVMValueRef emit_console_color_get(zan_irgen_t *g, int is_bg) {
    return LLVMBuildLoad2(g->builder, LLVMInt64TypeInContext(g->ctx),
                          console_color_slot(g, is_bg), "concolor");
}

/* Restore both channels to their defaults, for Console.ResetColor(). */
static void emit_console_color_reset(zan_irgen_t *g) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMBuildStore(g->builder,
        LLVMConstInt(i64, ZAN_CONSOLE_FG_DEFAULT, 0), console_color_slot(g, 0));
    LLVMBuildStore(g->builder,
        LLVMConstInt(i64, ZAN_CONSOLE_BG_DEFAULT, 0), console_color_slot(g, 1));
}

/* 为控制台前景色/背景色赋值发射 ANSI 转义序列 */
static void emit_console_color(zan_irgen_t *g, LLVMValueRef color, int is_bg) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef arrTy = LLVMArrayType(i32, 16);
    const char *nm = is_bg ? "__zan_ansi_bg" : "__zan_ansi_fg";
    LLVMValueRef tbl = LLVMGetNamedGlobal(g->mod, nm);
    if (!tbl) {
        static const int fg[16] = { 30,34,32,36,31,35,33,37,
                                    90,94,92,96,91,95,93,97 };
        LLVMValueRef vals[16];
        for (int i = 0; i < 16; i++)
            vals[i] = LLVMConstInt(i32, (unsigned)(fg[i] + (is_bg ? 10 : 0)), 0);
        tbl = LLVMAddGlobal(g->mod, arrTy, nm);
        LLVMSetInitializer(tbl, LLVMConstArray(i32, vals, 16));
        LLVMSetGlobalConstant(tbl, 1);
        LLVMSetLinkage(tbl, LLVMPrivateLinkage);
    }
    if (LLVMGetTypeKind(LLVMTypeOf(color)) != LLVMIntegerTypeKind)
        color = LLVMConstInt(i64, 0, 0);
    else if (LLVMGetIntTypeWidth(LLVMTypeOf(color)) < 64)
        color = LLVMBuildSExt(g->builder, color, i64, "csx");
    LLVMValueRef idx = zan_and(g->builder, color, LLVMConstInt(i64, 15, 0), "cidx");
    LLVMBuildStore(g->builder, idx, console_color_slot(g, is_bg));
    LLVMValueRef gep = LLVMBuildInBoundsGEP2(g->builder, arrTy, tbl,
        (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), idx }, 2, "ansip");
    LLVMValueRef code = LLVMBuildLoad2(g->builder, i32, gep, "ansicode");
    LLVMTypeRef pty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 1);
    LLVMValueRef pf = LLVMGetNamedFunction(g->mod, "printf");
    if (!pf) pf = LLVMAddFunction(g->mod, "printf", pty);
    LLVMValueRef fmt = zan_irgen_intern_string(g, "\033[%dm");
    zan_call2(g->builder, pty, pf, (LLVMValueRef[]){ fmt, code }, 2, "");
}

/*
 * Emit an OSC title sequence for Console.Title = value (works on Windows
 * Terminal / conhost VT and xterm-compatible terminals).
 */
static void emit_console_title(zan_irgen_t *g, LLVMValueRef s) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef pty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 1);
    LLVMValueRef pf = LLVMGetNamedFunction(g->mod, "printf");
    if (!pf) pf = LLVMAddFunction(g->mod, "printf", pty);
    LLVMValueRef fmt = zan_irgen_intern_string(g, "\033]0;%s\007");
    zan_call2(g->builder, pty, pf, (LLVMValueRef[]){ fmt, s }, 2, "");
}

/* 字段写入操作所需处理的真实存储类型推导 */
static zan_type_t *field_store_type(zan_irgen_t *g, zan_symbol_t *fsym,
                                    zan_type_t *recv) {
    if (!fsym || !fsym->type) return NULL;
    return concretize(g, subst_type_param(fsym->type, recv));
}

static zan_symbol_t *field_sym_for_decl(zan_symbol_t *type_sym,
                                        zan_ast_node_t *decl) {
    if (!type_sym || !decl) return NULL;
    for (int i = 0; i < type_sym->member_count; i++) {
        zan_symbol_t *member = type_sym->members[i];
        if ((member->kind == SYM_FIELD || member->kind == SYM_PROPERTY) &&
            member->decl == decl) return member;
    }
    return NULL;
}

static void emit_decl_field_initializers(zan_irgen_t *g,
                                         zan_symbol_t *type_sym,
                                         zan_type_t *recv_type,
                                         LLVMValueRef object_ptr,
                                         local_scope_t *locals) {
    if (!type_sym || !type_sym->decl || !object_ptr) return;
    zan_ast_node_t *decl = type_sym->decl;
    if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL) return;
    LLVMTypeRef st = get_struct_llvm_type(g, type_sym);
    if (!st) return;
    LLVMTypeRef object_type = LLVMPointerType(st, 0);
    object_ptr = emit_boundary_coerce(g, object_ptr, object_type);

    zan_symbol_t *saved_type_sym = g->current_type_sym;
    LLVMValueRef saved_this = g->current_this;
    zan_type_t *saved_inst = g->cur_inst;
    LLVMValueRef this_slot = emit_entry_alloca(g, object_type, "field.init.this");
    LLVMBuildStore(g->builder, object_ptr, this_slot);
    g->current_type_sym = type_sym;
    g->current_this = this_slot;
    g->cur_inst = recv_type;

    for (int i = 0; i < decl->type_decl.members.count; i++) {
        zan_ast_node_t *field = decl->type_decl.members.items[i];
        if (!field || (field->kind != AST_FIELD_DECL &&
                       field->kind != AST_PROPERTY_DECL) ||
            /* 自定义访问器属性无后备存储槽，通过方法调用存取 */
            (field->kind == AST_PROPERTY_DECL &&
             (field->field_decl.getter_body || field->field_decl.setter_body)) ||
            (field->field_decl.modifiers & MOD_STATIC) ||
            !field->field_decl.initializer) continue;
        zan_symbol_t *fsym = field_sym_for_decl(type_sym, field);
        if (!fsym) continue;
        int fi = get_field_index(type_sym, field->field_decl.name);
        if (fi < 0) continue;
        zan_type_t *field_type = field_store_type(g, fsym, recv_type);
        zan_type_t *source_type = infer_expr_type(
            g, field->field_decl.initializer, locals);
        check_implicit_narrowing(g, field_type, source_type,
                                 field->field_decl.initializer,
                                 "field initializer");
        check_value_type_mismatch(g, field_type, source_type,
                                  field->field_decl.initializer,
                                  "field initializer");
        check_generic_invariance(g, field_type, source_type,
                                 field->field_decl.initializer,
                                 "field initializer");
        LLVMValueRef value = field_type && field_type->kind == TYPE_DELEGATE &&
                             field->field_decl.initializer->kind == AST_LAMBDA
            ? emit_lambda_typed(g, field->field_decl.initializer,
                                field_type, locals)
            : emit_expr(g, field->field_decl.initializer, locals);
        LLVMValueRef field_ptr = emit_field_ptr(
            g, type_sym, st, object_ptr, fi, "field.init");
        LLVMTypeRef slot_type = map_type(g, fsym->type);
        value = emit_boundary_coerce(g, value, slot_type);
        if (field_type && (is_rc_managed_type(field_type) || field_type->kind == TYPE_OBJECT)) {
            emit_rc_store_field(g, field_type, field_ptr, value,
                                field->field_decl.initializer, locals,
                                (fsym->modifiers & MOD_WEAK) ? 1 : 0);
        } else {
            zan_store_fit(g, value, field_ptr);
        }
    }

    g->cur_inst = saved_inst;
    g->current_this = saved_this;
    g->current_type_sym = saved_type_sym;
}

static void emit_implicit_field_initializers(zan_irgen_t *g,
                                             zan_symbol_t *type_sym,
                                             zan_type_t *recv_type,
                                             LLVMValueRef object_ptr,
                                             local_scope_t *locals) {
    if (!type_sym) return;
    zan_type_t *base_type = type_sym->type ? type_sym->type->base_type : NULL;
    if (base_type && base_type->sym && base_type->sym != type_sym) {
        emit_implicit_field_initializers(g, base_type->sym, base_type,
                                         object_ptr, locals);
    }
    emit_decl_field_initializers(g, type_sym, recv_type, object_ptr, locals);
}

/* 判断接收者表达式是否持有消费方需释放的所有权引用 */
static int receiver_is_owned_temp(zan_irgen_t *g, zan_ast_node_t *object,
                                  local_scope_t *locals, zan_type_t *obj_type,
                                  LLVMValueRef obj_val) {
    return obj_val && obj_type && is_rc_managed_type(obj_type) &&
           !expr_is_local_ident(object, locals) &&
           expr_yields_owned_rc_value(g, object, locals);
}

/* 链式调用中的临时接收者对象在访问完成后及时释放 */
static LLVMValueRef finish_member_of_temp(zan_irgen_t *g, zan_ast_node_t *expr,
                                          local_scope_t *locals,
                                          zan_type_t *obj_type,
                                          LLVMValueRef obj_val, LLVMValueRef fv) {
    if (!receiver_is_owned_temp(g, expr->member.object, locals, obj_type, obj_val))
        return fv;
    zan_type_t *ft = member_owned_field_type(g, expr, locals);
    if (ft && is_rc_managed_type(ft) &&
        LLVMGetTypeKind(LLVMTypeOf(fv)) == LLVMPointerTypeKind &&
        /* 弱引用字段读取时已从注册表获取 +1 强引用 */
        !member_field_is_weak(g, expr, locals))
        emit_rc_retain_for_type(g, ft, fv);
    emit_rc_release_for_type(g, obj_type, obj_val);
    return fv;
}

/* readonly 字段赋值校验与写入约束 */
static void check_readonly_store(zan_irgen_t *g, zan_ast_node_t *lhs,
                                 local_scope_t *locals) {
    if (!lhs || lhs->kind != AST_MEMBER_ACCESS) return;
    if (lhs->member.object && lhs->member.object->kind == AST_THIS_EXPR) return;
    zan_type_t *ot = infer_expr_type(g, lhs->member.object, locals);
    if (!ot || !ot->sym) return;
    zan_symbol_t *f = get_field_sym(ot->sym, lhs->member.name);
    if (!f || !f->decl || f->decl->kind != AST_FIELD_DECL) return;
    if ((f->decl->field_decl.modifiers & MOD_READONLY) == 0) return;
    if (g->current_fn_is_ctor && g->current_type_sym == ot->sym) return;
    zan_diag_emit(g->diag, DIAG_ERROR, lhs->loc,
                  "cannot assign to readonly field '%.*s' outside a "
                  "constructor of '%.*s'",
                  (int)lhs->member.name.len, lhs->member.name.str,
                  (int)ot->sym->name.len, ot->sym->name.str);
}

/*
 * User-defined conversion operators (A43-B12), defined after the assignment
 * emitter: find_user_conversion / emit_user_conversion.
 */
static zan_symbol_t *find_user_conversion(zan_irgen_t *g, zan_type_t *from_type,
                                          zan_type_t *to_type,
                                          const char *op_name);
static LLVMValueRef emit_user_conversion(zan_irgen_t *g, zan_type_t *from_type,
                                         zan_type_t *to_type, const char *op_name,
                                         LLVMValueRef val, zan_ast_node_t *val_expr,
                                         local_scope_t *locals);

/* 多维矩形数组元素寻址：结合各维度形状计算一维平铺偏移 */
static LLVMValueRef emit_mdarray_elem_ptr(zan_irgen_t *g, LLVMValueRef arr_ptr,
        zan_ast_node_t *expr, LLVMTypeRef elem_llvm, local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    zan_type_t *at = infer_expr_type(g, expr->index.object, locals);
    int rank = (at && at->array_rank > 1) ? at->array_rank : 1;
    LLVMValueRef dim_ptr = LLVMBuildBitCast(g->builder, arr_ptr,
        LLVMPointerType(i64, 0), "md.dp");
    LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
    LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
    idx = emit_index_i64(g, idx, "md.i0");
    LLVMValueRef dim0 = LLVMBuildLoad2(g->builder, i64,
        LLVMBuildGEP2(g->builder, i64, dim_ptr, &zero, 1, "md.d0"), "md.dim0");
    emit_index_bounds_check(g, idx, dim0, expr->loc, "array");
    idx = emit_index_safe_bounds(g, idx, dim0, expr->loc, "array");
    LLVMValueRef flat = idx;
    for (int d = 1; d < rank; d++) {
        LLVMValueRef off = LLVMConstInt(i64, (unsigned long long)d, 0);
        LLVMValueRef dimv = LLVMBuildLoad2(g->builder, i64,
            LLVMBuildGEP2(g->builder, i64, dim_ptr, &off, 1, "md.dd"), "md.dim");
        LLVMValueRef ix = emit_expr(g, expr->index.extra.items[d - 1], locals);
        ix = emit_index_i64(g, ix, "md.ix");
        emit_index_bounds_check(g, ix, dimv, expr->loc, "array");
        ix = emit_index_safe_bounds(g, ix, dimv, expr->loc, "array");
        flat = zan_mul(g->builder, flat, dimv, "md.fm");
        flat = zan_add(g->builder, flat, ix, "md.fa");
    }
    LLVMValueRef data_off = LLVMConstInt(i64,
        (unsigned long long)rank * 8, 0);
    LLVMValueRef data = LLVMBuildGEP2(g->builder, i8, arr_ptr, &data_off, 1, "md.data");
    LLVMValueRef typed = LLVMBuildBitCast(g->builder, data,
        LLVMPointerType(elem_llvm, 0), "md.typed");
    return LLVMBuildGEP2(g->builder, elem_llvm, typed, &flat, 1, "md.ep");
}

/* 复合赋值：目标操作数仅求值一次，防止副作用重复触发 */
static int ca_synth_counter = 0;

static void ca_hoist_spine(zan_irgen_t *g, zan_ast_node_t **slot,
                           local_scope_t *locals, int is_root);

/* 将目标表达式求值入隐藏临时变量，并在原地替换为读取引用 */
static void ca_replace_with_temp(zan_irgen_t *g, zan_ast_node_t **slot,
                                 local_scope_t *locals) {
    zan_ast_node_t *node = *slot;
    if (!node) return;
    zan_type_t *type = infer_expr_type(g, node, locals);
    /* 缺乏明确类型时避免分配错误尺寸的临时槽 */
    if (!type) return;
    char nbuf[40];
    int n = (int)__atomic_fetch_add(&ca_synth_counter, 1, __ATOMIC_SEQ_CST);
    snprintf(nbuf, sizeof nbuf, "__zca%d", n);
    uint32_t nlen = (uint32_t)strlen(nbuf);
    zan_istr_t name = { zan_arena_strdup(g->arena, nbuf, nlen), nlen };
    LLVMValueRef alloca = emit_entry_alloca(g, map_type(g, type), nbuf);
    LLVMValueRef val = emit_expr(g, node, locals);
    zan_store_fit(g, val, alloca);
    local_add(locals, name, alloca, type);
    locals->vars[locals->count - 1].arc_owned =
        (is_rc_managed_type(type) && expr_yields_owned_rc_value(g, node, locals))
            ? 1 : 0;
    zan_ast_node_t *id = zan_ast_new(g->arena, AST_IDENTIFIER, node->loc);
    id->ident.name = name;
    *slot = id;
}

static void ca_hoist_spine(zan_irgen_t *g, zan_ast_node_t **slot,
                           local_scope_t *locals, int is_root) {
    zan_ast_node_t *node = *slot;
    if (!node) return;
    switch (node->kind) {
    case AST_INDEX:
        if (is_root) {
            /* storage location: keep it live, rewrite what it is built from */
            ca_hoist_spine(g, &node->index.object, locals, 0);
            ca_hoist_spine(g, &node->index.index, locals, 0);
        } else {
            ca_replace_with_temp(g, slot, locals);
        }
        return;
    case AST_MEMBER_ACCESS:
        if (is_root) {
            ca_hoist_spine(g, &node->member.object, locals, 0);
        } else {
            /*
             * container position: a property getter or computed object must
             * not run twice, so freeze the whole reference into a temp
             */
            ca_replace_with_temp(g, slot, locals);
        }
        return;
    case AST_IDENTIFIER:
    case AST_THIS_EXPR:
    case AST_INT_LITERAL:
    case AST_FLOAT_LITERAL:
    case AST_STRING_LITERAL:
    case AST_CHAR_LITERAL:
    case AST_BOOL_LITERAL:
    case AST_NULL_LITERAL:
        return;
    default:
        /* calls, casts, conditionals, ... can run user code */
        ca_replace_with_temp(g, slot, locals);
        return;
    }
}

/* 计算索引表达式 (AST_INDEX) 选中的目标元素内存地址 */
static LLVMValueRef emit_struct_elem_ptr(zan_irgen_t *g, zan_ast_node_t *expr,
                                         local_scope_t *locals) {
    zan_ast_node_t *base = expr->index.object;
    zan_type_t *bt = infer_expr_type(g, base, locals);
    if (!bt) return NULL;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64)
        idx = LLVMBuildSExt(g->builder, idx, i64, "ix");
    if (type_named(bt, "List", 4)) {
        LLVMValueRef raw = emit_expr(g, base, locals);
        LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw,
            LLVMPointerType(g->list_struct_type, 0), "lptr");
        LLVMValueRef count = LLVMBuildLoad2(g->builder, i64,
            LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0,
                "cf"), "cnt");
        emit_index_bounds_check(g, idx, count, expr->loc, "list");
        idx = emit_index_safe_bounds(g, idx, count, expr->loc, "list");
        idx = slot_word_index(g, idx, elem_slot_words(g, container_elem_type(bt)));
        LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0),
            LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2,
                "df"), "data");
        LLVMValueRef ep = LLVMBuildGEP2(g->builder, i64, data, &idx, 1, "ep");
        return LLVMBuildBitCast(g->builder, ep, i8p, "epb");
    }
    if (bt->kind == TYPE_ARRAY && bt->array_rank == 1) {
        LLVMValueRef arr = emit_expr(g, base, locals);
        emit_index_bounds_check(g, idx, zan_array_len(g, arr), expr->loc, "array");
        idx = emit_index_safe_bounds(g, idx, zan_array_len(g, arr),
            expr->loc, "array");
        zan_type_t *et = container_elem_type(bt);
        LLVMTypeRef etl = et ? map_type(g, et) : i64;
        LLVMValueRef typed = LLVMBuildBitCast(g->builder, arr,
            LLVMPointerType(etl, 0), "arrp");
        LLVMValueRef ep = LLVMBuildGEP2(g->builder, etl, typed, &idx, 1, "ep");
        return LLVMBuildBitCast(g->builder, ep, i8p, "epb");
    }
    if (is_span_type(bt)) {
        LLVMValueRef sv = emit_expr(g, base, locals);
        LLVMValueRef sbase = LLVMBuildExtractValue(g->builder, sv, 0, "spb");
        LLVMValueRef slen = LLVMBuildExtractValue(g->builder, sv, 1, "spl");
        emit_index_bounds_check(g, idx, slen, expr->loc, "span");
        idx = emit_index_safe_bounds(g, idx, slen, expr->loc, "span");
        zan_type_t *et = container_elem_type(bt);
        LLVMTypeRef etl = et ? map_type(g, et) : i64;
        LLVMValueRef typed = LLVMBuildBitCast(g->builder, sbase,
            LLVMPointerType(etl, 0), "spp");
        LLVMValueRef ep = LLVMBuildGEP2(g->builder, etl, typed, &idx, 1, "ep");
        return LLVMBuildBitCast(g->builder, ep, i8p, "epb");
    }
    return NULL;
}

static LLVMValueRef emit_ref_lvalue_ptr(zan_irgen_t *g, zan_ast_node_t *tgt,
                                        local_scope_t *locals);

    static LLVMValueRef emit_expr_assignment(zan_irgen_t *g, zan_ast_node_t *expr,
            local_scope_t *locals) {
            LLVMValueRef right;
            /* 元组解构赋值降解处理 */
            if (expr->binary.left && expr->binary.left->kind == AST_TUPLE_EXPR) {
                zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                    "tuple assignment is not supported; use `var (a, b) = rhs` "
                    "to declare the names");
                return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
            }
            check_readonly_store(g, expr->binary.left, locals);
            /* 复合赋值左值子树单次求值防护 */
            if (expr->binary.compound_base != TK_EOF)
                ca_hoist_spine(g, &expr->binary.left, locals, 1);
        {
            zan_type_t *lt = assign_lhs_type(g, expr->binary.left, locals);
            check_implicit_narrowing(g, lt,
                infer_expr_type(g, expr->binary.right, locals),
                expr->binary.right, "assignment");
            check_value_type_mismatch(g, lt,
                infer_expr_type(g, expr->binary.right, locals),
                expr->binary.right, "assignment");
            check_generic_invariance(g, lt,
                infer_expr_type(g, expr->binary.right, locals),
                expr->binary.right, "assignment");
            if (type_is_binding(lt) &&
                !type_is_binding(infer_expr_type(g, expr->binary.right, locals))) {
                LLVMValueRef bv = emit_binding_value(g, lt, expr->binary.right, locals);
                if (bv) {
                    /*
                     * the binding object is freshly owned (+1): swap in a
                     * dummy `new` node so the store paths below treat it as
                     * an owned value (no extra retain).
                     */
                    zan_ast_node_t *dummy = zan_ast_new(g->arena, AST_NEW_EXPR,
                        expr->binary.right->loc);
                    zan_ast_node_t *clone = zan_ast_new(g->arena, AST_ASSIGNMENT,
                        expr->loc);
                    clone->binary.op = expr->binary.op;
                    clone->binary.left = expr->binary.left;
                    clone->binary.right = dummy;
                    expr = clone;
                    right = bv;
                    goto binding_lowered;
                }
            }
            /*
             * `target = (a, b) => ...`: hand the delegate type down so the
             * lambda's unannotated parameters borrow their types from it.
             */
            right = (lt && lt->kind == TYPE_DELEGATE &&
                     expr->binary.right->kind == AST_LAMBDA)
                        ? emit_lambda_typed(g, expr->binary.right, lt, locals)
                        : emit_expr(g, expr->binary.right, locals);
            /*
             * user-defined implicit conversion: `d = c;` where the source
             * type declares `implicit operator double` (B12).
             */
            if (lt && !type_is_binding(lt)) {
                zan_type_t *rty = infer_expr_type(g, expr->binary.right, locals);
                if (find_user_conversion(g, rty, lt, "op_implicit")) {
                    right = emit_user_conversion(g, rty, lt, "op_implicit",
                                                 right, expr->binary.right, locals);
                    if (is_rc_managed_type(lt)) {
                        /* 类型转换生成的新对象按 +1 持有所有权管理 */
                        zan_ast_node_t *dummy = zan_ast_new(g->arena,
                            AST_NEW_EXPR, expr->binary.right->loc);
                        zan_ast_node_t *clone = zan_ast_new(g->arena,
                            AST_ASSIGNMENT, expr->loc);
                        clone->binary.op = expr->binary.op;
                        clone->binary.left = expr->binary.left;
                        clone->binary.right = dummy;
                        expr = clone;
                    }
                }
            }
        }
binding_lowered:
        if (expr->binary.left->kind == AST_IDENTIFIER) {
            local_var_t *local = local_find(locals, expr->binary.left->ident.name);
            if (local && local_is_dyn_obj(g, local)) {
                emit_obj_local_store(g, local, right,
                    infer_expr_type(g, expr->binary.right, locals),
                    expr->binary.right, locals);
            } else if (local && local_slot_owns_rc(local)) {
                /* ARC: release the previous occupant and retain the new one. */
                emit_rc_capture_local(g, local->type, local->alloca, right, expr->binary.right, locals);
            } else if (local && local->struct_rc) {
                /*
                 * Whole-struct assignment into an owning value slot --
                 * field-wise capture (retain borrowed fields, release the old
                 * occupant's fields).
                 */
                emit_struct_local_capture(g, local->type, local->alloca, right,
                                          expr->binary.right, locals);
            } else if (local && local->frame_owner >= 0) {
                /* 异步协程帧槽别名：真实存储位于堆帧结构体内 */
                local_var_t *owner = &locals->vars[local->frame_owner];
                if (owner->type && is_rc_managed_type(owner->type) &&
                    LLVMGetTypeKind(local_slot_type(g, owner)) ==
                        LLVMPointerTypeKind) {
                    emit_rc_capture_local(g, owner->type, owner->alloca, right,
                                          expr->binary.right, locals);
                } else if (owner->struct_rc) {
                    emit_struct_local_capture(g, owner->type, owner->alloca, right,
                                              expr->binary.right, locals);
                } else {
                    LLVMValueRef sv = coerce_int_to(g, right,
                        local_slot_type(g, owner));
                    zan_store_fit(g, sv, owner->alloca);
                }
            } else if (local) {
                LLVMValueRef sv = coerce_int_to(g, right,
                    local_slot_type(g, local));
                zan_store_fit(g, sv, local->alloca);
            } else if (g->current_type_sym &&
                       get_static_field_global(g, g->current_type_sym,
                           get_field_sym(g->current_type_sym,
                               expr->binary.left->ident.name), NULL)) {
                /* bare-name static field of the enclosing class: `field = v` */
                zan_symbol_t *fs = get_field_sym(g->current_type_sym,
                    expr->binary.left->ident.name);
                /*
                 * custom-setter static property written bare-name: dispatch to
                 * the static set_Prop(value) (no receiver)
                 */
                zan_symbol_t *setter = property_setter_sym(g, fs);
                if (setter) {
                    emit_property_setter_call(g, setter, g->current_type_sym->type,
                        NULL, right, expr->binary.left,
                        expr->binary.right, locals);
                } else {
                LLVMValueRef gv = get_static_field_global(g, g->current_type_sym, fs, NULL);
                if (fs->type && (is_rc_managed_type(fs->type) || fs->type->kind == TYPE_OBJECT)) {
                    emit_rc_store_field(g, fs->type, gv, right, expr->binary.right, locals,
                                        (fs->modifiers & MOD_WEAK) ? 1 : 0);
                } else if (fs->type && fs->type->kind == TYPE_STRUCT &&
                           type_contains_collection_rc(g, fs->type, 0)) {
                    emit_struct_field_capture(g, fs->type, gv, right,
                                              expr->binary.right, locals);
                } else {
                    LLVMTypeRef ft = fs->type ? map_type(g, fs->type)
                                              : LLVMInt64TypeInContext(g->ctx);
                    zan_store_fit(g, coerce_int_to(g, right, ft), gv);
                }
                }
            } else if (g->current_this && g->current_type_sym) {
                /* implicit this.Field assignment */
                int fi = get_field_index(g->current_type_sym, expr->binary.left->ident.name);
                if (fi >= 0) {
                    LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                    /*
                     * custom-setter property written bare-name: dispatch to
                     * this.set_Prop(value) instead of the backing slot
                     */
                    zan_symbol_t *psym = get_field_sym(g->current_type_sym, expr->binary.left->ident.name);
                    zan_symbol_t *setter = property_setter_sym(g, psym);
                    if (setter && st) {
                        LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                            LLVMPointerType(st, 0), g->current_this, "this");
                        emit_property_setter_call(g, setter, g->cur_inst,
                            this_ptr, right, expr->binary.left,
                            expr->binary.right, locals);
                    } else if (st) {
                        LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                            LLVMPointerType(st, 0), g->current_this, "this");
                        LLVMValueRef fptr = emit_field_ptr(g, g->current_type_sym, st, this_ptr, fi, "fld");
                        /* type conversion if needed */
                        zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->binary.left->ident.name);
                        /*
                         * `item = x` on a field declared `T`: the store has to
                         * manage the instantiation's concrete type, or the +1
                         * handed over by the caller is dropped.
                         */
                        zan_type_t *fst = fsym ? field_store_type(g, fsym, g->cur_inst) : NULL;
                        if (fsym && fsym->type) {
                            LLVMTypeRef target_t = map_type(g, fsym->type);
                            LLVMTypeRef val_t = LLVMTypeOf(right);
                            if (target_t != val_t) {
                                if (LLVMGetTypeKind(target_t) == LLVMFloatTypeKind &&
                                    LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                                    right = LLVMBuildFPTrunc(g->builder, right, target_t, "trunc");
                                } else if (LLVMGetTypeKind(target_t) == LLVMDoubleTypeKind &&
                                           LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
                                    right = LLVMBuildFPExt(g->builder, right, target_t, "ext");
                                } else if (LLVMGetTypeKind(target_t) == LLVMIntegerTypeKind &&
                                           LLVMGetTypeKind(val_t) == LLVMIntegerTypeKind) {
                                    unsigned tw = LLVMGetIntTypeWidth(target_t);
                                    unsigned vw = LLVMGetIntTypeWidth(val_t);
                                    if (tw > vw) right = LLVMBuildSExt(g->builder, right, target_t, "ext");
                                    else if (tw < vw) right = LLVMBuildTrunc(g->builder, right, target_t, "trunc");
                                }
                            }
                        }
                        if (fst && (is_rc_managed_type(fst) || fst->kind == TYPE_OBJECT)) {
                            emit_rc_store_field(g, fst, fptr, right, expr->binary.right, locals,
                                                (fsym->modifiers & MOD_WEAK) ? 1 : 0);
                        } else if (fst && fst->kind == TYPE_STRUCT &&
                                   type_contains_collection_rc(g, fst, 0)) {
                            /* 结构体字段按值整体赋值 */
                            emit_struct_field_capture(g, fst, fptr, right,
                                                      expr->binary.right, locals);
                        } else {
                            zan_store_fit(g, right, fptr);
                        }
                    }
                }
            }
        } else if (expr->binary.left->kind == AST_INDEX) {
            /* arr[i] = value */
            zan_ast_node_t *arr_expr = expr->binary.left->index.object;
            /* 索引赋值 obj[i] = v 降解为实例的索引器 setter 调用 */
            {
                zan_type_t *oist = infer_expr_type(g, arr_expr, locals);
                if (oist && (oist->kind == TYPE_CLASS || oist->kind == TYPE_STRUCT) &&
                    oist->sym) {
                    zan_istr_t op_istr = {(char *)"op_index_set", 12};
                    zan_ast_node_t *op_call = zan_ast_new(g->arena, AST_CALL, expr->loc);
                    op_call->call.callee = NULL;
                    zan_ast_list_init(&op_call->call.args);
                    zan_ast_list_init(&op_call->call.type_args);
                    zan_ast_list_push(&op_call->call.args,
                        expr->binary.left->index.index, g->arena);
                    zan_ast_list_push(&op_call->call.args,
                        expr->binary.right, g->arena);
                    zan_symbol_t *op_sym = resolve_op_overload(g, oist->sym,
                                                               op_istr, op_call, locals);
                    if (op_sym) {
                        for (int fi = irgen_find_function(g, op_sym); fi >= 0; fi = -1) {
                            if (g->functions[fi].sym == op_sym) {
                                LLVMValueRef recv_val = emit_expr(g, arr_expr, locals);
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc(3, sizeof(LLVMValueRef));
                                call_args[0] = recv_val;
                                if (LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMStructTypeKind) {
                                    LLVMValueRef rslot = emit_entry_alloca(g,
                                        LLVMTypeOf(recv_val), "ops.recv");
                                    LLVMBuildStore(g->builder, recv_val, rslot);
                                    call_args[0] = rslot;
                                }
                                int recv_eh_pushed = 0;
                                if (oist->kind == TYPE_CLASS &&
                                    !expr_is_local_ident(arr_expr, locals) &&
                                    expr_yields_owned_rc_value(g, arr_expr, locals) &&
                                    LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMPointerTypeKind) {
                                    emit_eh_tmp_push(g, recv_val);
                                    recv_eh_pushed = 1;
                                }
                                call_args[1] = emit_arg_typed(g, expr->binary.left->index.index,
                                    method_param_type_at(g, op_sym,
                                        op_index_param_offset(op_sym), NULL, arr_expr, locals), locals);
                                zan_type_t *vt = method_param_type_at(g, op_sym,
                                    op_index_param_offset(op_sym) + 1, NULL, arr_expr, locals);
                                LLVMValueRef varg = right;
                                LLVMTypeRef pv2 = vt ? map_type(g, vt) : NULL;
                                if (pv2 && LLVMGetTypeKind(LLVMTypeOf(varg)) == LLVMPointerTypeKind &&
                                    LLVMGetTypeKind(pv2) == LLVMPointerTypeKind &&
                                    LLVMTypeOf(varg) != pv2)
                                    varg = LLVMBuildBitCast(g->builder, varg, pv2, "ops.v");
                                call_args[2] = varg;
                                LLVMTypeRef mft = g->functions[fi].fn_type;
                                LLVMValueRef mfn = route_generic_method(g, oist,
                                    op_sym, g->functions[fi].fn, mft, &mft);
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "ops";
                                LLVMValueRef result = emit_dispatch_call(g,
                                    oist->sym, op_sym, mfn, mft, call_args, 3, cn);
                                result = coerce_generic_result(g, result, op_sym, oist);
                                if (recv_eh_pushed) emit_eh_tmp_pop(g);
                                emit_release_owned_call_temp(g, arr_expr, recv_val, locals);
                                emit_release_owned_call_temp(g, expr->binary.left->index.index, call_args[1], locals);
                                emit_release_owned_call_temp(g, expr->binary.right, call_args[2], locals);
                                free(call_args);
                                return right;
                            }
                        }
                    }
                }
            }
            {
                zan_type_t *sot = infer_expr_type(g, arr_expr, locals);
                if (is_span_type(sot)) {
                    zan_type_t *et = container_elem_type(sot);
                    LLVMTypeRef elem_llvm = et ? map_type(g, et)
                        : LLVMInt32TypeInContext(g->ctx);
                    LLVMValueRef span_val = emit_expr(g, arr_expr, locals);
                    LLVMValueRef base = LLVMBuildExtractValue(g->builder, span_val, 0, "sps.base");
                    LLVMValueRef typed = LLVMBuildBitCast(g->builder, base,
                        LLVMPointerType(elem_llvm, 0), "sps.p");
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    LLVMValueRef span_len = LLVMBuildExtractValue(g->builder, span_val,
                        1, "sps.len");
                    emit_index_bounds_check(g, idx, span_len, expr->loc, "span");
                    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64)
                        idx = LLVMBuildSExt(g->builder, idx,
                            LLVMInt64TypeInContext(g->ctx), "sps.ix");
                    idx = emit_index_safe_bounds(g, idx, span_len, expr->loc, "span");
                    LLVMValueRef ep = LLVMBuildGEP2(g->builder, elem_llvm, typed, &idx, 1, "sps.ep");
                    LLVMValueRef sv = right;
                    if (LLVMTypeOf(sv) != elem_llvm)
                        sv = coerce_int_to(g, sv, elem_llvm);
                    /*
                     * align 1: a span may view a raw address (protocol framing,
                     * FFI structs) whose elements are not naturally aligned.
                     */
                    LLVMSetAlignment(LLVMBuildStore(g->builder, sv, ep), 1);
                    return right;
                }
            }
            if (arr_expr->kind == AST_IDENTIFIER) {
                local_var_t *local = local_find(locals, arr_expr->ident.name);
                if (local && local->type && type_named(local->type, "Dict", 4)) {
                    /* dict[key] = value — upsert via the shared helper */
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef raw = LLVMBuildLoad2(g->builder, i8ptr, local->alloca, "draw");
                    zan_type_t *kt = dict_key_type(g, local->type);
                    zan_type_t *vt = dict_value_type(local->type);
                    LLVMValueRef key = emit_expr(g, expr->binary.left->index.index, locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(key)) == LLVMIntegerTypeKind) {
                        LLVMTypeRef kem = kt ? map_type(g, kt) : NULL;
                        if (kem && LLVMGetTypeKind(kem) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(kem) < LLVMGetIntTypeWidth(LLVMTypeOf(key)))
                            key = LLVMBuildTrunc(g->builder, key, kem, "k.nw");
                        if (LLVMGetIntTypeWidth(LLVMTypeOf(key)) < 64)
                            key = LLVMBuildSExt(g->builder, key, i64, "k.sx");
                        key = LLVMBuildIntToPtr(g->builder, key, i8ptr, "k.ip");
                    } else if (LLVMTypeOf(key) != i8ptr) {
                        key = LLVMBuildBitCast(g->builder, key, i8ptr, "k.bc");
                    }
                    emit_dict_value_set(g, local->type, raw, key, right,
                        expr->binary.left->index.index, expr->binary.right,
                        locals);
                } else if (local && local->type && type_named(local->type, "List", 4)) {
                    /* list[i] = value — store into the list's i64 data slots */
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw = LLVMBuildLoad2(g->builder, i8ptr, local->alloca, "lraw");
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                        list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(LLVMInt64TypeInContext(g->ctx), 0),
                        data_field, "data");
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    LLVMValueRef count_field = LLVMBuildStructGEP2(g->builder,
                        g->list_struct_type, list_ptr, 0, "countf");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder,
                        LLVMInt64TypeInContext(g->ctx), count_field, "count");
                    emit_index_bounds_check(g, idx, count, expr->loc, "list");
                    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                        idx = LLVMBuildSExt(g->builder, idx, LLVMInt64TypeInContext(g->ctx), "idxext");
                    }
                    idx = emit_index_safe_bounds(g, idx, count, expr->loc, "list");
                    idx = slot_word_index(g, idx,
                        elem_slot_words(g, container_elem_type(local->type)));
                    LLVMValueRef slot_ptr = LLVMBuildGEP2(g->builder, LLVMInt64TypeInContext(g->ctx), data, &idx, 1, "ep");
                    emit_collection_slot_store(g, container_elem_type(local->type),
                        LLVMInt64TypeInContext(g->ctx), slot_ptr,
                        right, expr->binary.right, locals, 1);
                } else if (local) {
                    LLVMValueRef arr_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                        local->alloca, "arrload");
                    /* string (byte buffer): use i8 element type and truncate value */
                    if (local->type && local->type->kind == TYPE_STRING) {
                        LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                        if (!local->opaque_string) {
                            arr_ptr = emit_string_base_guard(g, arr_ptr, expr->loc);
                            LLVMValueRef len = emit_string_buffer_len(g, arr_ptr, expr->loc);
                            emit_index_bounds_check(g, idx, len, expr->loc, "string");
                            idx = emit_index_safe_bounds(g, idx, len, expr->loc, "string");
                        } else {
                            /* No reliable bound: keep null/negative bare faults out. */
                            idx = emit_string_elem_guard(g, arr_ptr, idx, expr->loc, &arr_ptr);
                        }
                        LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i8, arr_ptr, &idx, 1, "eidx");
                        LLVMValueRef val8 = LLVMBuildTrunc(g->builder, right, i8, "byte");
                        zan_store_fit(g, val8, elem_ptr);
                        emit_string_len_invalidate(g, arr_ptr);
                    } else {
                        LLVMTypeRef elem_llvm = LLVMInt32TypeInContext(g->ctx);
                        if (local->type && local->type->element_type) {
                            zan_type_t *et = local->type->element_type;
                            elem_llvm = map_type(g, et);
                        }
                        LLVMValueRef elem_ptr;
                        if (local->type && local->type->kind == TYPE_ARRAY &&
                            local->type->array_rank > 1) {
                            /*
                             * rank-N rectangular slot: per-dim bounds checks
                             * and row-major flattening inside the helper
                             */
                            elem_ptr = emit_mdarray_elem_ptr(g, arr_ptr,
                                expr->binary.left, elem_llvm, locals);
                        } else {
                            LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                            if (local->type && local->type->kind == TYPE_ARRAY) {
                                emit_index_bounds_check(g, idx, zan_array_len(g, arr_ptr),
                                    expr->loc, "array");
                                idx = emit_index_safe_bounds(g, idx,
                                    zan_array_len(g, arr_ptr), expr->loc, "array");
                            }
                            LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                                LLVMPointerType(elem_llvm, 0), "arrp");
                            elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
                        }
                        zan_type_t *et = local->type ? local->type->element_type : NULL;
                        LLVMValueRef stored = right;
                        /* 可空元素槽写入：将标量值封装为可空结构体 */
                        if (et && et->kind == TYPE_NULLABLE)
                            stored = coerce_int_to(g, stored, elem_llvm);
                        LLVMTypeKind slot_k = LLVMGetTypeKind(elem_llvm);
                        LLVMTypeKind val_k = LLVMGetTypeKind(LLVMTypeOf(stored));
                        if (et && is_rc_managed_type(et)) {
                            if (!expr_yields_owned_rc_value(g, expr->binary.right, locals)) {
                                emit_rc_retain_for_type(g, et, stored);
                            }
                            if (val_k == LLVMPointerTypeKind && LLVMTypeOf(stored) != elem_llvm) {
                                stored = LLVMBuildBitCast(g->builder, stored, elem_llvm, "slot.bc");
                            }
                            LLVMValueRef old = LLVMBuildLoad2(g->builder, elem_llvm, elem_ptr, "old");
                            zan_store_fit(g, stored, elem_ptr);
                            emit_rc_release_for_type(g, et, old);
                        } else {
                            if (slot_k == LLVMPointerTypeKind) {
                                if (val_k == LLVMIntegerTypeKind) {
                                    stored = LLVMBuildIntToPtr(g->builder, stored, elem_llvm, "slot.ip");
                                } else if (val_k == LLVMPointerTypeKind && LLVMTypeOf(stored) != elem_llvm) {
                                    stored = LLVMBuildBitCast(g->builder, stored, elem_llvm, "slot.bc");
                                }
                            } else if (slot_k == LLVMIntegerTypeKind) {
                                if (val_k == LLVMPointerTypeKind) {
                                    stored = LLVMBuildPtrToInt(g->builder, stored, elem_llvm, "slot.pi");
                                } else if (val_k == LLVMIntegerTypeKind) {
                                    /* Fit the value to the element width in both directions. */
                                    stored = coerce_int_to(g, stored, elem_llvm);
                                }
                            }
                            zan_store_fit(g, stored, elem_ptr);
                        }
                    }
                } else if (g->current_type_sym) {
                    /* implicit this.field[i] = value */
                    zan_symbol_t *fsym = get_field_sym(g->current_type_sym, arr_expr->ident.name);
                    if (fsym) {
                        LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
                        LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                        /* 类内部裸字段访问的字符串引用计数管理 */
                        if (fsym->type && fsym->type->kind == TYPE_STRING &&
                            expr_has_reliable_string_bounds(arr_expr, locals)) {
                            arr_ptr = emit_string_base_guard(g, arr_ptr, expr->loc);
                            LLVMValueRef len = emit_string_buffer_len(g, arr_ptr, expr->loc);
                            emit_index_bounds_check(g, idx, len, expr->loc, "string");
                            idx = emit_index_safe_bounds(g, idx, len, expr->loc, "string");
                        } else if (fsym->type && fsym->type->kind == TYPE_STRING) {
                            /* No reliable bound: keep null/negative bare faults out. */
                            idx = emit_string_elem_guard(g, arr_ptr, idx, expr->loc, &arr_ptr);
                        } else if (fsym->type && fsym->type->kind == TYPE_ARRAY &&
                                   fsym->type->array_rank <= 1) {
                            emit_index_bounds_check(g, idx, zan_array_len(g, arr_ptr),
                                expr->loc, "array");
                            idx = emit_index_safe_bounds(g, idx,
                                zan_array_len(g, arr_ptr), expr->loc, "array");
                        }
                        if (fsym->type && type_named(fsym->type, "List", 4)) {
                            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                            LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, arr_ptr,
                                LLVMPointerType(g->list_struct_type, 0), "lptr");
                            LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                                list_ptr, 2, "df");
                            LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64t, 0),
                                data_field, "data");
                            LLVMValueRef count_field = LLVMBuildStructGEP2(g->builder,
                                g->list_struct_type, list_ptr, 0, "countf");
                            LLVMValueRef count = LLVMBuildLoad2(g->builder, i64t,
                                count_field, "count");
                            emit_index_bounds_check(g, idx, count, expr->loc, "list");
                            if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                                LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                                idx = LLVMBuildSExt(g->builder, idx, i64t, "idxext");
                            }
                            idx = emit_index_safe_bounds(g, idx, count, expr->loc, "list");
                            idx = slot_word_index(g, idx,
                                elem_slot_words(g, container_elem_type(fsym->type)));
                            LLVMValueRef slot_ptr = LLVMBuildGEP2(g->builder, i64t, data, &idx, 1, "ep");
                            emit_collection_slot_store(g, container_elem_type(fsym->type), i64t, slot_ptr,
                                right, expr->binary.right, locals, 1);
                        } else if (fsym->type && type_named(fsym->type, "Dict", 4)) {
                            /*
                             * implicit this.dict[key] = value — upsert via the
                             * shared helper (same shape as the local path)
                             */
                            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                            zan_type_t *kt = dict_key_type(g, fsym->type);
                            LLVMValueRef key = idx;
                            if (LLVMGetTypeKind(LLVMTypeOf(key)) == LLVMIntegerTypeKind) {
                                LLVMTypeRef kem = kt ? map_type(g, kt) : NULL;
                                if (kem && LLVMGetTypeKind(kem) == LLVMIntegerTypeKind &&
                                    LLVMGetIntTypeWidth(kem) < LLVMGetIntTypeWidth(LLVMTypeOf(key)))
                                    key = LLVMBuildTrunc(g->builder, key, kem, "k.nw");
                                if (LLVMGetIntTypeWidth(LLVMTypeOf(key)) < 64)
                                    key = LLVMBuildSExt(g->builder, key, i64, "k.sx");
                                key = LLVMBuildIntToPtr(g->builder, key, i8ptr, "k.ip");
                            } else if (LLVMTypeOf(key) != i8ptr) {
                                key = LLVMBuildBitCast(g->builder, key, i8ptr, "k.bc");
                            }
                            emit_dict_value_set(g, fsym->type, arr_ptr, key, right,
                                expr->binary.left->index.index, expr->binary.right, locals);
                        } else if (fsym->type && fsym->type->kind == TYPE_STRING) {
                            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                            LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i8, arr_ptr, &idx, 1, "eidx");
                            LLVMValueRef val8 = LLVMBuildTrunc(g->builder, right, i8, "byte");
                            zan_store_fit(g, val8, elem_ptr);
                            emit_string_len_invalidate(g, arr_ptr);
                        } else {
                        LLVMTypeRef elem_llvm = LLVMInt32TypeInContext(g->ctx);
                        if (fsym->type && fsym->type->element_type) {
                            zan_type_t *et = fsym->type->element_type;
                            elem_llvm = map_type(g, et);
                        }
                        LLVMValueRef elem_ptr;
                        if (fsym->type && fsym->type->kind == TYPE_ARRAY &&
                            fsym->type->array_rank > 1) {
                            elem_ptr = emit_mdarray_elem_ptr(g, arr_ptr,
                                expr->binary.left, elem_llvm, locals);
                        } else {
                            LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                                LLVMPointerType(elem_llvm, 0), "arrp");
                            elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
                        }
                        zan_type_t *et = fsym->type ? fsym->type->element_type : NULL;
                        LLVMValueRef stored = right;
                        /* 可空数组元素槽写入：封装标量值为可空对象 */
                        if (et && et->kind == TYPE_NULLABLE)
                            stored = coerce_int_to(g, stored, elem_llvm);
                        LLVMTypeKind slot_k = LLVMGetTypeKind(elem_llvm);
                        LLVMTypeKind val_k = LLVMGetTypeKind(LLVMTypeOf(stored));
                        if (et && is_rc_managed_type(et)) {
                            if (!expr_yields_owned_rc_value(g, expr->binary.right, locals)) {
                                emit_rc_retain_for_type(g, et, stored);
                            }
                            if (val_k == LLVMPointerTypeKind && LLVMTypeOf(stored) != elem_llvm) {
                                stored = LLVMBuildBitCast(g->builder, stored, elem_llvm, "slot.bc");
                            }
                            LLVMValueRef old = LLVMBuildLoad2(g->builder, elem_llvm, elem_ptr, "old");
                            zan_store_fit(g, stored, elem_ptr);
                            emit_rc_release_for_type(g, et, old);
                        } else {
                            if (slot_k == LLVMPointerTypeKind) {
                                if (val_k == LLVMIntegerTypeKind) {
                                    stored = LLVMBuildIntToPtr(g->builder, stored, elem_llvm, "slot.ip");
                                } else if (val_k == LLVMPointerTypeKind && LLVMTypeOf(stored) != elem_llvm) {
                                    stored = LLVMBuildBitCast(g->builder, stored, elem_llvm, "slot.bc");
                                }
                            } else if (slot_k == LLVMIntegerTypeKind) {
                                if (val_k == LLVMPointerTypeKind) {
                                    stored = LLVMBuildPtrToInt(g->builder, stored, elem_llvm, "slot.pi");
                                } else if (val_k == LLVMIntegerTypeKind) {
                                    /* Fit the value to the element width in both directions. */
                                    stored = coerce_int_to(g, stored, elem_llvm);
                                }
                            } else if (et && et->kind == TYPE_NULLABLE) {
                                /* 数组中可空元素槽位读取与写入 */
                                stored = coerce_int_to(g, stored, elem_llvm);
                            }
                            zan_store_fit(g, stored, elem_ptr);
                        }
                        }
                    }
                }
            } else if (arr_expr->kind == AST_MEMBER_ACCESS) {
                /* obj.field[i] = value — array stored in a struct/class field */
                zan_type_t *at = member_access_field_type(g, locals, arr_expr);
                if (at) {
                    LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    if (type_named(at, "List", 4)) {
                        /*
                         * List field: index into the data buffer, not the
                         * struct — otherwise the store clobbers count/cap/data.
                         */
                        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                        LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, arr_ptr,
                            LLVMPointerType(g->list_struct_type, 0), "lptr");
                        LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                            list_ptr, 2, "df");
                        LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64t, 0),
                            data_field, "data");
                        LLVMValueRef count = LLVMBuildLoad2(g->builder, i64t,
                            LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                                list_ptr, 0, "countf"), "count");
                        emit_index_bounds_check(g, idx, count, expr->loc, "list");
                        if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                            idx = LLVMBuildSExt(g->builder, idx, i64t, "idxext");
                        }
                        idx = emit_index_safe_bounds(g, idx, count, expr->loc, "list");
                        idx = slot_word_index(g, idx,
                            elem_slot_words(g, container_elem_type(at)));
                        LLVMValueRef slot_ptr = LLVMBuildGEP2(g->builder, i64t, data, &idx, 1, "ep");
                        emit_collection_slot_store(g, container_elem_type(at), i64t, slot_ptr,
                            right, expr->binary.right, locals, 1);
                    } else if (type_named(at, "Dict", 4)) {
                        /* obj.dict[key] = value — upsert via the shared helper */
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        zan_type_t *kt = dict_key_type(g, at);
                        LLVMValueRef key = idx;
                        if (LLVMGetTypeKind(LLVMTypeOf(key)) == LLVMIntegerTypeKind) {
                            LLVMTypeRef kem = kt ? map_type(g, kt) : NULL;
                            if (kem && LLVMGetTypeKind(kem) == LLVMIntegerTypeKind &&
                                LLVMGetIntTypeWidth(kem) < LLVMGetIntTypeWidth(LLVMTypeOf(key)))
                                key = LLVMBuildTrunc(g->builder, key, kem, "k.nw");
                            if (LLVMGetIntTypeWidth(LLVMTypeOf(key)) < 64)
                                key = LLVMBuildSExt(g->builder, key, i64, "k.sx");
                            key = LLVMBuildIntToPtr(g->builder, key, i8ptr, "k.ip");
                        } else if (LLVMTypeOf(key) != i8ptr) {
                            key = LLVMBuildBitCast(g->builder, key, i8ptr, "k.bc");
                        }
                        emit_dict_value_set(g, at, arr_ptr, key, right,
                            expr->binary.left->index.index, expr->binary.right, locals);
                    } else if (at->kind == TYPE_STRING) {
                        LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                        /* 包含底层 FFI 缓冲区的字符串字段读写防护 */
                        if (expr_has_reliable_string_bounds(arr_expr, locals)) {
                            arr_ptr = emit_string_base_guard(g, arr_ptr, expr->loc);
                            idx = emit_index_safe_bounds(g, idx,
                                emit_string_buffer_len(g, arr_ptr, expr->loc), expr->loc,
                                "string");
                        }
                        else
                            idx = emit_string_elem_guard(g, arr_ptr, idx, expr->loc, &arr_ptr);
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i8, arr_ptr, &idx, 1, "eidx");
                        LLVMValueRef val8 = LLVMBuildTrunc(g->builder, right, i8, "byte");
                        zan_store_fit(g, val8, elem_ptr);
                        emit_string_len_invalidate(g, arr_ptr);
                        } else {
                        if (at->kind == TYPE_ARRAY && at->array_rank == 1) {
                            /*
                             * rank>1 goes through emit_mdarray_elem_ptr, which
                             * checks each dimension itself
                             */
                            emit_index_bounds_check(g, idx,
                                zan_array_len(g, arr_ptr), expr->loc, "array");
                            idx = emit_index_safe_bounds(g, idx,
                                zan_array_len(g, arr_ptr), expr->loc, "array");
                        }
                        LLVMTypeRef elem_llvm = LLVMInt32TypeInContext(g->ctx);
                        if (at->element_type) {
                            elem_llvm = map_type(g, at->element_type);
                        }
                        LLVMValueRef elem_ptr;
                        if (at->kind == TYPE_ARRAY && at->array_rank > 1) {
                            elem_ptr = emit_mdarray_elem_ptr(g, arr_ptr,
                                expr->binary.left, elem_llvm, locals);
                        } else {
                            LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                                LLVMPointerType(elem_llvm, 0), "arrp");
                            elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
                        }
                        zan_type_t *et = at->element_type;
                        LLVMValueRef stored = right;
                        /*
                         * `int?[]` element slot: wrap a raw payload value into
                         * the nullable struct before storing.
                         */
                        if (et && et->kind == TYPE_NULLABLE)
                            stored = coerce_int_to(g, stored, elem_llvm);
                        LLVMTypeKind slot_k = LLVMGetTypeKind(elem_llvm);
                        LLVMTypeKind val_k = LLVMGetTypeKind(LLVMTypeOf(stored));
                        if (et && is_rc_managed_type(et)) {
                            if (!expr_yields_owned_rc_value(g, expr->binary.right, locals)) {
                                emit_rc_retain_for_type(g, et, stored);
                            }
                            if (val_k == LLVMPointerTypeKind && LLVMTypeOf(stored) != elem_llvm) {
                                stored = LLVMBuildBitCast(g->builder, stored, elem_llvm, "slot.bc");
                            }
                            LLVMValueRef old = LLVMBuildLoad2(g->builder, elem_llvm, elem_ptr, "old");
                            zan_store_fit(g, stored, elem_ptr);
                            emit_rc_release_for_type(g, et, old);
                        } else {
                            if (slot_k == LLVMPointerTypeKind) {
                                if (val_k == LLVMIntegerTypeKind) {
                                    stored = LLVMBuildIntToPtr(g->builder, stored, elem_llvm, "slot.ip");
                                } else if (val_k == LLVMPointerTypeKind && LLVMTypeOf(stored) != elem_llvm) {
                                    stored = LLVMBuildBitCast(g->builder, stored, elem_llvm, "slot.bc");
                                }
                            } else if (slot_k == LLVMIntegerTypeKind) {
                                if (val_k == LLVMPointerTypeKind) {
                                    stored = LLVMBuildPtrToInt(g->builder, stored, elem_llvm, "slot.pi");
                                } else if (val_k == LLVMIntegerTypeKind) {
                                    /* Fit the value to the element width in both directions. */
                                    stored = coerce_int_to(g, stored, elem_llvm);
                                }
                            }
                            zan_store_fit(g, stored, elem_ptr);
                        }
                    }
                }
            } else {
                /*
                 * any other array expression: `obj.Buffer()[i] = v`, and the
                 * like. Without this the store was dropped silently.
                 */
                zan_type_t *at = infer_expr_type(g, arr_expr, locals);
                if (at && type_named(at, "List", 4)) {
                    /* 多层级联索引写入：基表达式读取与深层寻址 */
                    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, arr_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64t, 0),
                        LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                            list_ptr, 2, "df"), "data");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64t,
                        LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                            list_ptr, 0, "countf"), "count");
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    emit_index_bounds_check(g, idx, count, expr->loc, "list");
                    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                        idx = LLVMBuildSExt(g->builder, idx, i64t, "idxext");
                    }
                    idx = emit_index_safe_bounds(g, idx, count, expr->loc, "list");
                    idx = slot_word_index(g, idx,
                        elem_slot_words(g, container_elem_type(at)));
                    LLVMValueRef slot_ptr = LLVMBuildGEP2(g->builder, i64t, data, &idx, 1, "ep");
                    emit_collection_slot_store(g, container_elem_type(at), i64t, slot_ptr,
                        right, expr->binary.right, locals, 1);
                    emit_release_owned_call_temp(g, arr_expr, arr_ptr, locals);
                } else if (at && at->kind == TYPE_ARRAY) {
                    LLVMTypeRef elem_llvm = at->element_type
                        ? map_type(g, at->element_type)
                        : LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
                    LLVMValueRef elem_ptr;
                    if (at->array_rank > 1) {
                        elem_ptr = emit_mdarray_elem_ptr(g, arr_ptr,
                            expr->binary.left, elem_llvm, locals);
                    } else {
                        LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                        emit_index_bounds_check(g, idx, zan_array_len(g, arr_ptr),
                            expr->loc, "array");
                        idx = emit_index_safe_bounds(g, idx,
                            zan_array_len(g, arr_ptr), expr->loc, "array");
                        LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                            LLVMPointerType(elem_llvm, 0), "arrp");
                        elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm,
                            typed_arr, &idx, 1, "eidx");
                    }
                    LLVMValueRef stored = right;
                    LLVMTypeKind slot_k = LLVMGetTypeKind(elem_llvm);
                    LLVMTypeKind val_k = LLVMGetTypeKind(LLVMTypeOf(stored));
                    if (at->element_type && is_rc_managed_type(at->element_type)) {
                        if (!expr_yields_owned_rc_value(g, expr->binary.right, locals))
                            emit_rc_retain_for_type(g, at->element_type, stored);
                        if (val_k == LLVMPointerTypeKind && LLVMTypeOf(stored) != elem_llvm)
                            stored = LLVMBuildBitCast(g->builder, stored, elem_llvm, "slot.bc");
                        LLVMValueRef old = LLVMBuildLoad2(g->builder, elem_llvm, elem_ptr, "old");
                        zan_store_fit(g, stored, elem_ptr);
                        emit_rc_release_for_type(g, at->element_type, old);
                    } else {
                        if (slot_k == LLVMPointerTypeKind && val_k == LLVMIntegerTypeKind)
                            stored = LLVMBuildIntToPtr(g->builder, stored, elem_llvm, "slot.ip");
                        else if (slot_k == LLVMIntegerTypeKind && val_k == LLVMPointerTypeKind)
                            stored = LLVMBuildPtrToInt(g->builder, stored, elem_llvm, "slot.pi");
                        else if (slot_k == LLVMIntegerTypeKind && val_k == LLVMIntegerTypeKind)
                            stored = coerce_int_to(g, stored, elem_llvm);
                        else if (at->element_type &&
                                 at->element_type->kind == TYPE_NULLABLE)
                            /*
                             * `int?[]` element slot: wrap a raw payload value
                             * into the nullable struct (coerce_int_to also
                             * fits int widths and handles the null literal).
                             */
                            stored = coerce_int_to(g, stored, elem_llvm);
                        zan_store_fit(g, stored, elem_ptr);
                    }
                    emit_release_owned_call_temp(g, arr_expr, arr_ptr, locals);
                } else {
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->binary.left->loc,
                        "cannot assign through this indexed expression");
                }
            }
        } else if (expr->binary.left->kind == AST_MEMBER_ACCESS) {
            /* obj.Field = value */
            zan_ast_node_t *obj_expr = expr->binary.left->member.object;
            bool stored = false;
            /*
             * Console.ForegroundColor/BackgroundColor/Title = value are console
             * state, not field stores: lower them to ANSI escapes.
             */
            if (obj_expr->kind == AST_IDENTIFIER &&
                obj_expr->ident.name.len == 7 &&
                memcmp(obj_expr->ident.name.str, "Console", 7) == 0 &&
                !local_find(locals, obj_expr->ident.name)) {
                zan_istr_t mn = expr->binary.left->member.name;
                if (mn.len == 15 && memcmp(mn.str, "ForegroundColor", 15) == 0) {
                    emit_console_color(g, right, 0);
                    return right;
                }
                if (mn.len == 15 && memcmp(mn.str, "BackgroundColor", 15) == 0) {
                    emit_console_color(g, right, 1);
                    return right;
                }
                if (mn.len == 5 && memcmp(mn.str, "Title", 5) == 0) {
                    emit_console_title(g, right);
                    emit_release_owned_call_temp(g, expr->binary.right, right, locals);
                    return right;
                }
            }
            /* ClassName.StaticField = value — store into the backing global. */
            if (obj_expr->kind == AST_IDENTIFIER &&
                !local_find(locals, obj_expr->ident.name)) {
                zan_symbol_t *cs = zan_binder_lookup(g->binder, obj_expr->ident.name);
                if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                    zan_symbol_t *fs = get_field_sym(cs, expr->binary.left->member.name);
                    /*
                     * custom-setter static property: dispatch to the static
                     * set_Prop(value) (no receiver) instead of the global slot
                     */
                    zan_symbol_t *setter = property_setter_sym(g, fs);
                    if (setter) {
                        emit_property_setter_call(g, setter, cs->type, NULL,
                            right, obj_expr, expr->binary.right, locals);
                        stored = true;
                    } else {
                    LLVMValueRef gv = get_static_field_global(g, cs, fs,
                        static_access_inst(g, obj_expr));
                    if (gv) {
                        if (fs->type && (is_rc_managed_type(fs->type) || fs->type->kind == TYPE_OBJECT)) {
                            emit_rc_store_field(g, fs->type, gv, right,
                                                expr->binary.right, locals,
                                                (fs->modifiers & MOD_WEAK) ? 1 : 0);
                        } else if (fs->type && fs->type->kind == TYPE_STRUCT &&
                                   type_contains_collection_rc(g, fs->type, 0)) {
                            emit_struct_field_capture(g, fs->type, gv, right,
                                                      expr->binary.right, locals);
                        } else {
                            LLVMTypeRef ft = fs->type ? map_type(g, fs->type)
                                                      : LLVMInt64TypeInContext(g->ctx);
                            zan_store_fit(g, coerce_int_to(g, right, ft), gv);
                        }
                        stored = true;
                    }
                    }
                }
            }
            if (!stored && obj_expr->kind == AST_IDENTIFIER) {
                local_var_t *local = local_find(locals, obj_expr->ident.name);
                if (local && local->type && local->type->sym) {
                    int fi = get_field_index(local->type->sym, expr->binary.left->member.name);
                    if (fi >= 0) {
                        LLVMTypeRef st = get_struct_llvm_type(g, local->type->sym);
                        /*
                         * custom-setter property on a local receiver: dispatch
                         * to set_Prop(value) instead of writing the slot
                         */
                        zan_symbol_t *psym = get_field_sym(local->type->sym, expr->binary.left->member.name);
                        zan_symbol_t *setter = property_setter_sym(g, psym);
                        if (setter && st) {
                            LLVMValueRef struct_ptr = struct_base_ptr(g, local, st);
                            emit_property_setter_call(g, setter, local->type,
                                struct_ptr, right, obj_expr,
                                expr->binary.right, locals);
                            stored = true;
                        } else if (st) {
                            LLVMValueRef struct_ptr = struct_base_ptr(g, local, st);
                            LLVMValueRef fptr = emit_field_ptr(g, local->type->sym, st, struct_ptr, fi, "fld");
                            zan_symbol_t *afsym = get_field_sym(local->type->sym, expr->binary.left->member.name);
                            zan_type_t *aft = afsym ? field_store_type(g, afsym, local->type) : NULL;
                            if (aft && (is_rc_managed_type(aft) || aft->kind == TYPE_OBJECT)) {
                                emit_rc_store_field(g, aft, fptr, right, expr->binary.right, locals,
                                                    (afsym->modifiers & MOD_WEAK) ? 1 : 0);
                            } else if (aft && aft->kind == TYPE_STRUCT &&
                                       type_contains_collection_rc(g, aft, 0)) {
                                /*
                                 * `x.inner = v` on a struct/class slot --
                                 * the field's +1s belong to the enclosing
                                 * storage, so capture them like a value store.
                                 */
                                emit_struct_field_capture(g, aft, fptr, right,
                                                          expr->binary.right, locals);
                            } else {
                                zan_store_fit(g, right, fptr);
                            }
                            stored = true;
                        }
                    }
                }
            }
            /*
             * general: <expr>.field = value where <expr> yields a class pointer
             * (e.g. list[i].field, a.b.field, this.field).
             */
            if (!stored) {
                zan_symbol_t *cls = expr_class_sym(g, obj_expr, locals);
                if (cls) {
                    int fi = get_field_index(cls, expr->binary.left->member.name);
                    if (fi >= 0) {
                        /*
                         * property with a custom setter: dispatch the write to
                         * set_Prop(value) instead of the backing slot
                         */
                        zan_symbol_t *psym = get_field_sym(cls, expr->binary.left->member.name);
                        zan_symbol_t *setter = property_setter_sym(g, psym);
                        if (obj_expr->kind == AST_INDEX && cls->kind == SYM_STRUCT) {
                            /* 集合/数组内结构体元素字段就地赋值 */
                            zan_type_t *et = infer_expr_type(g, obj_expr, locals);
                            LLVMTypeRef st = get_struct_llvm_type(g, cls);
                            LLVMValueRef ep = (st && !type_is_binding(et))
                                ? emit_struct_elem_ptr(g, obj_expr, locals) : NULL;
                            if (ep) {
                                LLVMValueRef sptr = LLVMBuildBitCast(g->builder, ep,
                                    LLVMPointerType(st, 0), "ep.s");
                                if (setter) {
                                    emit_property_setter_call(g, setter, et, sptr,
                                        right, obj_expr, expr->binary.right, locals);
                                } else {
                                    LLVMValueRef fptr = emit_field_ptr(g, cls, st,
                                        sptr, fi, "gfld");
                                    zan_symbol_t *gfsym = get_field_sym(cls,
                                        expr->binary.left->member.name);
                                    zan_type_t *gft = gfsym ? field_store_type(g,
                                        gfsym, et) : NULL;
                                    if (gft && (is_rc_managed_type(gft) || gft->kind == TYPE_OBJECT)) {
                                        emit_rc_store_field(g, gft, fptr, right,
                                            expr->binary.right, locals,
                                            (gfsym->modifiers & MOD_WEAK) ? 1 : 0);
                                    } else if (gft && gft->kind == TYPE_STRUCT &&
                                               type_contains_collection_rc(g, gft, 0)) {
                                        emit_struct_field_capture(g, gft, fptr, right,
                                                                  expr->binary.right, locals);
                                    } else {
                                        zan_store_fit(g, right, fptr);
                                    }
                                }
                                stored = true;
                            }
                        } else if (setter) {
                            zan_type_t *rct = infer_expr_type(g, obj_expr, locals);
                            LLVMValueRef rval = emit_guarded_member_object(
                                g, expr->binary.left, locals);
                            emit_property_setter_call(g, setter, rct, rval,
                                right, obj_expr, expr->binary.right, locals);
                            stored = true;
                        } else {
                            LLVMTypeRef st = get_struct_llvm_type(g, cls);
                            LLVMValueRef obj_val = cls->kind == SYM_STRUCT
                                ? emit_ref_lvalue_ptr(g, obj_expr, locals)
                                : emit_guarded_member_object(g, expr->binary.left, locals);
                            if (obj_val && st &&
                                LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                                LLVMValueRef fptr = emit_field_ptr(g, cls, st, obj_val, fi, "gfld");
                                zan_symbol_t *gfsym = get_field_sym(cls, expr->binary.left->member.name);
                                zan_type_t *gft = gfsym
                                    ? field_store_type(g, gfsym,
                                          infer_expr_type(g, obj_expr, locals))
                                    : NULL;
                                if (gft && (is_rc_managed_type(gft) || gft->kind == TYPE_OBJECT)) {
                                    emit_rc_store_field(g, gft, fptr, right, expr->binary.right, locals,
                                                        (gfsym->modifiers & MOD_WEAK) ? 1 : 0);
                                } else if (gft && gft->kind == TYPE_STRUCT &&
                                           type_contains_collection_rc(g, gft, 0)) {
                                    emit_struct_field_capture(g, gft, fptr, right,
                                                              expr->binary.right, locals);
                                } else {
                                    zan_store_fit(g, right, fptr);
                                }
                            }
                        }
                    }
                }
            }
            /* 健壮性：未定义字段赋值时安全报错与回退 */
            {
                zan_symbol_t *acls = expr_class_sym(g, obj_expr, locals);
                if (!acls && obj_expr->kind == AST_IDENTIFIER &&
                    !local_find(locals, obj_expr->ident.name)) {
                    zan_symbol_t *ts = zan_binder_lookup(g->binder,
                                                         obj_expr->ident.name);
                    if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT))
                        acls = ts;
                }
                if (acls && (acls->kind == SYM_CLASS || acls->kind == SYM_STRUCT)) {
                    zan_istr_t an = expr->binary.left->member.name;
                    if (!type_declares_member(acls, an)) {
                        zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                            "'%.*s' has no member '%.*s'",
                            (int)acls->name.len, acls->name.str,
                            (int)an.len, an.str);
                    }
                }
                /* 命名空间/静态类型字段赋值错误防护 */
                if (!stored && !acls && obj_expr->kind == AST_IDENTIFIER &&
                    !local_find(locals, obj_expr->ident.name) &&
                    !zan_binder_lookup(g->binder, obj_expr->ident.name) &&
                    !(g->current_type_sym &&
                      (get_field_sym(g->current_type_sym, obj_expr->ident.name) ||
                       get_method_sym(g->current_type_sym, obj_expr->ident.name)))) {
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "use of undeclared identifier '%.*s'",
                        (int)obj_expr->ident.name.len, obj_expr->ident.name.str);
                }
            }
        }
        return right;
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

static LLVMValueRef emit_expr_call(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals);

/* 前缀/后缀自增自减 (++ / --) 共享降解发射：操作数原地更新 */
static LLVMValueRef emit_incdec_expr(zan_irgen_t *g, zan_ast_node_t *expr,
                                     local_scope_t *locals, int is_prefix) {
    zan_ast_node_t *operand = expr->unary.operand;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef slot_ptr = NULL;
    /*
     * set when the slot is a byte of a string, whose cached length the store
     * at the end of this function invalidates
     */
    LLVMValueRef slot_str_base = NULL;
    LLVMTypeRef slot_ty = NULL;
    /* property operand (`obj.Prop++`): resolved via getter+setter calls */
    zan_symbol_t *prop_getter = NULL;
    zan_symbol_t *prop_setter = NULL;
    zan_type_t *prop_recv_type = NULL;
    LLVMValueRef prop_recv = NULL;

    if (operand->kind == AST_IDENTIFIER) {
        local_var_t *lv = local_find(locals, operand->ident.name);
        if (lv) {
            slot_ptr = lv->alloca;
            slot_ty = local_slot_type(g, lv);
        } else if (g->current_type_sym) {
            /* 外层类同名符号访问：根据静态/实例上下文分发 */
            zan_symbol_t *fs = get_field_sym(g->current_type_sym, operand->ident.name);
            LLVMValueRef gv = get_static_field_global(g, g->current_type_sym, fs, NULL);
            zan_symbol_t *getter = gv ? property_getter_sym(g, fs) : NULL;
            if (getter) {
                prop_getter = getter;
                prop_setter = property_setter_sym(g, fs);
                prop_recv_type = g->current_type_sym->type;
                prop_recv = NULL;
            } else {
            if (gv) {
                slot_ptr = gv;
                slot_ty = fs->type ? map_type(g, fs->type) : i64;
            } else {
                /* implicit this.Field */
                int fi = get_field_index(g->current_type_sym, operand->ident.name);
                LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                if (fi >= 0 && st) {
                    LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(st, 0), g->current_this, "this");
                    /* implicit-this property: this.Prop++ */
                    zan_symbol_t *fsym = get_field_sym(g->current_type_sym, operand->ident.name);
                    zan_symbol_t *igetter = property_getter_sym(g, fsym);
                    if (igetter) {
                        prop_getter = igetter;
                        prop_setter = property_setter_sym(g, fsym);
                        prop_recv_type = g->cur_inst;
                        prop_recv = this_ptr;
                    } else {
                    slot_ptr = emit_field_ptr(g, g->current_type_sym, st, this_ptr, fi, "fld");
                    slot_ty = fsym && fsym->type ? map_type(g, fsym->type) : i64;
                    }
                }
            }
            }
        }
    } else if (operand->kind == AST_MEMBER_ACCESS) {
        zan_ast_node_t *obj_expr = operand->member.object;
        /* ClassName.StaticField */
        if (obj_expr->kind == AST_IDENTIFIER && !local_find(locals, obj_expr->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder, obj_expr->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *fs = get_field_sym(cs, operand->member.name);
                /* static property: ClassName.Prop++ */
                zan_symbol_t *getter = property_getter_sym(g, fs);
                if (getter) {
                    prop_getter = getter;
                    prop_setter = property_setter_sym(g, fs);
                    prop_recv_type = cs->type;
                    prop_recv = NULL;
                } else {
                LLVMValueRef gv = get_static_field_global(g, cs, fs,
                    static_access_inst(g, obj_expr));
                if (gv) {
                    slot_ptr = gv;
                    slot_ty = fs->type ? map_type(g, fs->type) : i64;
                }
                }
            }
        }
        /* local struct field: `s.field++` */
        if (!slot_ptr && !prop_getter && obj_expr->kind == AST_IDENTIFIER) {
            local_var_t *local = local_find(locals, obj_expr->ident.name);
            if (local && local->type && local->type->sym) {
                int fi = get_field_index(local->type->sym, operand->member.name);
                LLVMTypeRef st = get_struct_llvm_type(g, local->type->sym);
                if (fi >= 0 && st) {
                    zan_symbol_t *fsym = get_field_sym(local->type->sym, operand->member.name);
                    /* property on a local receiver: local.Prop++ */
                    zan_symbol_t *lgetter = property_getter_sym(g, fsym);
                    if (lgetter) {
                        prop_getter = lgetter;
                        prop_setter = property_setter_sym(g, fsym);
                        prop_recv_type = local->type;
                        prop_recv = struct_base_ptr(g, local, st);
                    } else {
                    LLVMValueRef struct_ptr = struct_base_ptr(g, local, st);
                    slot_ptr = emit_field_ptr(g, local->type->sym, st, struct_ptr, fi, "fld");
                    slot_ty = fsym && fsym->type ? map_type(g, fsym->type) : i64;
                    }
                }
            }
        }
        /* general class instance field: `obj.field++` / `this.field++` */
        if (!slot_ptr && !prop_getter) {
            zan_symbol_t *cls = expr_class_sym(g, obj_expr, locals);
            if (cls) {
                int fi = get_field_index(cls, operand->member.name);
                LLVMTypeRef st = get_struct_llvm_type(g, cls);
                if (fi >= 0 && st) {
                    zan_symbol_t *fsym = get_field_sym(cls, operand->member.name);
                    /* property on a general receiver: obj.Prop++ */
                    zan_symbol_t *ggetter = property_getter_sym(g, fsym);
                    if (ggetter) {
                        prop_getter = ggetter;
                        prop_setter = property_setter_sym(g, fsym);
                        prop_recv_type = infer_expr_type(g, obj_expr, locals);
                        prop_recv = emit_expr(g, obj_expr, locals);
                    } else {
                    LLVMValueRef obj_val = emit_expr(g, obj_expr, locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                        slot_ptr = emit_field_ptr(g, cls, st, obj_val, fi, "gfld");
                        slot_ty = fsym && fsym->type ? map_type(g, fsym->type) : i64;
                    }
                    }
                }
            }
        }
    } else if (operand->kind == AST_INDEX) {
        zan_ast_node_t *arr_expr = operand->index.object;
        zan_type_t *at = infer_expr_type(g, arr_expr, locals);
        if (at && at->kind == TYPE_ARRAY) {
            LLVMTypeRef elem_llvm = at->element_type
                ? map_type(g, at->element_type) : i64;
            LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
            if (at->array_rank > 1) {
                /* rank-N slot: per-dim checks and flattening in the helper */
                slot_ptr = emit_mdarray_elem_ptr(g, arr_ptr, operand,
                                                  elem_llvm, locals);
            } else {
                LLVMValueRef idx = emit_expr(g, operand->index.index, locals);
                emit_index_bounds_check(g, idx, zan_array_len(g, arr_ptr),
                    operand->loc, "array");
                idx = emit_index_safe_bounds(g, idx, zan_array_len(g, arr_ptr),
                    operand->loc, "array");
                LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                    LLVMPointerType(elem_llvm, 0), "arrp");
                slot_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
            }
            slot_ty = elem_llvm;
        } else if (at && at->kind == TYPE_STRING) {
            /* string[i] is a byte slot */
            LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
            LLVMValueRef idx = emit_expr(g, operand->index.index, locals);
            if (expr_has_reliable_string_bounds(arr_expr, locals)) {
                arr_ptr = emit_string_base_guard(g, arr_ptr, operand->loc);
                LLVMValueRef str_len = emit_string_buffer_len(g, arr_ptr, operand->loc);
                emit_index_bounds_check(g, idx, str_len, operand->loc, "string");
                idx = emit_index_safe_bounds(g, idx, str_len, operand->loc, "string");
            } else {
                /* No reliable bound: keep null/negative from faulting bare. */
                idx = emit_string_elem_guard(g, arr_ptr, idx, operand->loc, &arr_ptr);
            }
            slot_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                arr_ptr, &idx, 1, "eidx");
            slot_ty = LLVMInt8TypeInContext(g->ctx);
            slot_str_base = arr_ptr;
        }
    }

    if (prop_getter) {
        /* 属性自增/自减：通过 getter 读取，计算新值后通过 setter 写回 */
        zan_type_t *pt = prop_getter->type;
        if (pt) pt = subst_type_param_deep(g, pt, prop_recv_type);
        LLVMTypeRef pty = pt ? map_type(g, pt) : i64;
        LLVMTypeKind pk = LLVMGetTypeKind(pty);
        if (pk != LLVMIntegerTypeKind && pk != LLVMFloatTypeKind &&
            pk != LLVMDoubleTypeKind) {
            zan_diag_emit(g->diag, DIAG_ERROR, operand->loc,
                "++/-- requires a numeric operand");
            return LLVMConstInt(i64, 0, 0);
        }
        LLVMValueRef old_val = emit_property_getter_call(g, prop_getter,
            prop_recv_type, prop_recv, operand, locals);
        LLVMValueRef new_val;
        if (pk == LLVMFloatTypeKind || pk == LLVMDoubleTypeKind) {
            LLVMValueRef one = LLVMConstReal(pty, 1.0);
            new_val = (expr->unary.op == TK_PLUS_PLUS)
                ? LLVMBuildFAdd(g->builder, old_val, one, "inc")
                : LLVMBuildFSub(g->builder, old_val, one, "dec");
        } else {
            LLVMValueRef one = LLVMConstInt(pty, 1, 0);
            new_val = (expr->unary.op == TK_PLUS_PLUS)
                ? zan_add(g->builder, old_val, one, "inc")
                : zan_sub(g->builder, old_val, one, "dec");
        }
        emit_property_setter_call(g, prop_setter, prop_recv_type, prop_recv,
            new_val, operand, expr->unary.operand, locals);
        return is_prefix ? new_val : old_val;
    }
    if (!slot_ptr || !slot_ty) {
        zan_diag_emit(g->diag, DIAG_ERROR, operand->loc,
            "cannot apply ++/-- to this expression");
        return LLVMConstInt(i64, 0, 0);
    }
    LLVMTypeKind k = LLVMGetTypeKind(slot_ty);
    if (k != LLVMIntegerTypeKind && k != LLVMFloatTypeKind &&
        k != LLVMDoubleTypeKind) {
        zan_diag_emit(g->diag, DIAG_ERROR, operand->loc,
            "++/-- requires a numeric operand");
        return LLVMConstInt(i64, 0, 0);
    }
    LLVMValueRef old_val = LLVMBuildLoad2(g->builder, slot_ty, slot_ptr, "inc.old");
    LLVMValueRef new_val;
    if (k == LLVMFloatTypeKind || k == LLVMDoubleTypeKind) {
        LLVMValueRef one = LLVMConstReal(slot_ty, 1.0);
        new_val = (expr->unary.op == TK_PLUS_PLUS)
            ? LLVMBuildFAdd(g->builder, old_val, one, "inc")
            : LLVMBuildFSub(g->builder, old_val, one, "dec");
    } else {
        LLVMValueRef one = LLVMConstInt(slot_ty, 1, 0);
        new_val = (expr->unary.op == TK_PLUS_PLUS)
            ? zan_add(g->builder, old_val, one, "inc")
            : zan_sub(g->builder, old_val, one, "dec");
    }
    zan_store_fit(g, new_val, slot_ptr);
    if (slot_str_base) emit_string_len_invalidate(g, slot_str_base);
    return is_prefix ? new_val : old_val;
}

/* 转换字符串插值格式化说明符（如冒号后的格式标记） */
static int interp_format_to_printf(const zan_istr_t *spec, bool is_float,
                                   bool is_ulong, bool *want_float,
                                   char *out, size_t outcap) {
    if (want_float) *want_float = false;
    if (!spec || spec->len == 0) return 0;
    const char *s = spec->str;
    int len = (int)spec->len;
    char code = s[0];
    int digits = 0;
    for (int i = 1; i < len; i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            /*
             * saturate instead of overflowing `int` — compiler-level UB —
             * on an absurd spec like {v:}; oversize counts
             * are rejected per code below
             */
            if (digits <= 9999) digits = digits * 10 + (s[i] - '0');
        }
    }
    switch (code) {
    case 'D': case 'd': /* decimal, zero-padded to `digits` */
        if (is_float) break;
        if (digits <= 0 || digits > 512) return 0;
        if (is_ulong)
            return snprintf(out, outcap, "%%0%dllu", digits);
        return snprintf(out, outcap, "%%0%dlld", digits);
    case 'X': case 'x': 
/*
 * hex (upper/lower), zero-padded to `digits`;
 * case follows the spec letter ({v:x2} is
 * lowercase, like C#)
 */
        if (is_float) break;
        if (digits <= 0 || digits > 512) return 0;
        return snprintf(out, outcap, "%%0%dll%c", digits,
                        code == 'X' ? 'X' : 'x');
    case 'F': case 'f': /* fixed-point with `digits` decimals */
        if (digits < 0) digits = 0;
        if (digits > 512) digits = 512;
        if (want_float) *want_float = true;
        return snprintf(out, outcap, "%%.%dlf", digits);
    case 'E': case 'e': /* scientific */
        if (digits < 0) digits = 0;
        if (digits > 512) digits = 512;
        if (want_float) *want_float = true;
        return snprintf(out, outcap, "%%.%dE", digits);
    case 'G': case 'g': 
/*
 * general: default is %g; a digit count is
 * significant digits, so {v:G3} -> %.3g
 */
        if (digits > 0)
            return snprintf(out, outcap, "%%.%dg", digits);
        return 0;
    case '0': case '#': { /* custom numeric: 0 = required digit, # = optional */
        int ipad = 0, frac = 0;
        bool after_dot = false, have_dot = false;
        for (int i = 0; i < len; i++) {
            if (s[i] == '.') { after_dot = true; have_dot = true; continue; }
            if (s[i] == '0' || s[i] == '#') {
                if (after_dot) frac++;
                else if (s[i] == '0') ipad++;
            }
        }
        if (have_dot) {
            if (want_float) *want_float = true;
            return snprintf(out, outcap, "%%.%dlf", frac);
        }
        if (ipad > 0) {
            if (is_ulong) return snprintf(out, outcap, "%%0%dllu", ipad);
            return snprintf(out, outcap, "%%0%dlld", ipad);
        }
        return 0;
    }
    default:
        break; /* unrecognized (N, C, P, ...): default format */
    }
    return 0;
}

static LLVMValueRef emit_expr_string_interp(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* 字符串插值：将各子表达式格式化为字符串后统一拼接 */
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);

        int n = expr->string_interp.parts.count;
        if (n == 0) return emit_string_literal_rc(g, (zan_istr_t){ "", 0 });

        /* convert each part to i8* */
        LLVMValueRef *strs = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
        LLVMValueRef *lens = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
        unsigned char *owns = (unsigned char *)calloc((size_t)n, sizeof(unsigned char));

        LLVMTypeRef strlen_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
        LLVMTypeRef snprintf_type = LLVMFunctionType(
            LLVMInt32TypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8ptr, i64, i8ptr }, 3, 1);

        for (int i = 0; i < n; i++) {
            zan_ast_node_t *part = expr->string_interp.parts.items[i];
            if (part->kind == AST_STRING_LITERAL) {
                strs[i] = emit_string_literal_rc(g, part->str_val);
                lens[i] = LLVMConstInt(i64, (uint64_t)part->str_val.len, 0);
            } else {
                /* the k-th hole (parts i==1,3,5...) pairs with formats[k] */
                int k = (i - 1) / 2;
                zan_ast_node_t *fnode = (k >= 0 &&
                    k < expr->string_interp.formats.count)
                    ? expr->string_interp.formats.items[k] : NULL;
                zan_istr_t spec = fnode ? fnode->str_val
                                        : (zan_istr_t){ NULL, 0 };

                LLVMValueRef val = emit_expr(g, part, locals);
                LLVMTypeRef vt = LLVMTypeOf(val);
                LLVMTypeKind vtk = LLVMGetTypeKind(vt);

                if (vtk == LLVMPointerTypeKind) {
                    /*
                     * already a string — a null one concatenates as "" (C#),
                     * so coerce before the length is taken
                     */
                    strs[i] = emit_str_nonnull(g, val);
                    lens[i] = emit_string_length(g, strs[i], expr->loc);
                    owns[i] = expr_yields_owned_rc_value(g, part, locals) ? 1 : 0;
                } else if (vtk == LLVMDoubleTypeKind || vtk == LLVMFloatTypeKind) {
                    /* 两段式 snprintf：首轮预计算格式化长度，次轮写入缓冲区 */
                    char fbuf[32];
                    bool want_f = false;
                    int flen = interp_format_to_printf(&spec, true, false,
                        &want_f, fbuf, sizeof(fbuf));
                    LLVMValueRef fval = (vtk == LLVMFloatTypeKind)
                        ? LLVMBuildFPExt(g->builder, val,
                            LLVMDoubleTypeInContext(g->ctx), "f2d")
                        : val;
                    if (flen <= 0) {
                        /*
                         * no format spec: shortest round-trip spelling
                         * , not %g
                         */
                        LLVMValueRef buf = emit_string_alloc_rc(g,
                            LLVMConstInt(i64, 40, 0));
                        emit_dbl_str(g, buf, LLVMConstInt(i64, 40, 0), val);
                        strs[i] = buf;
                        lens[i] = emit_string_length(g, buf, expr->loc);
                        owns[i] = 1;
                    } else {
                    LLVMValueRef fmt = zan_irgen_intern_string(g, fbuf);
                    LLVMValueRef null_ptr = LLVMConstNull(i8ptr);
                    LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
                    LLVMValueRef snp_args1[] = { null_ptr, zero, fmt, fval };
                    LLVMValueRef needed = zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args1, 4, "needed");
                    LLVMValueRef needed64 = LLVMBuildSExt(g->builder, needed, i64, "n64");
                    LLVMValueRef buf_size = zan_add(g->builder, needed64, LLVMConstInt(i64, 1, 0), "bsz");
                    LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
                    LLVMValueRef snp_args2[] = { buf, buf_size, fmt, fval };
                    zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args2, 4, "");
                    strs[i] = buf;
                    lens[i] = needed64;
                    owns[i] = 1;
                    }
                } else if (vtk == LLVMIntegerTypeKind &&
                           LLVMGetIntTypeWidth(vt) == 1) {
                    /* bool 类型插值为字面量 "true" 或 "false" */
                    LLVMValueRef btrue = emit_string_literal_rc(g,
                        (zan_istr_t){ "true", 4 });
                    LLVMValueRef bfalse = emit_string_literal_rc(g,
                        (zan_istr_t){ "false", 5 });
                    strs[i] = LLVMBuildSelect(g->builder, val, btrue, bfalse,
                        "bstr");
                    lens[i] = LLVMBuildSelect(g->builder, val,
                        LLVMConstInt(i64, 4, 0), LLVMConstInt(i64, 5, 0),
                        "blen");
                    owns[i] = 0;
                } else if (expr_is_char(g, part, locals)) {
                    /* char 类型插值为字符本身而非数值编码 */
                    LLVMValueRef buf = emit_char_to_cstr(g, val);
                    strs[i] = buf;
                    lens[i] = emit_string_length(g, buf, expr->loc);
                    owns[i] = 1;
                } else if (llvm_is_nullable(vt)) {
                    /* 可空类型插值：与字符串拼接使用一致的强制转换 */
                    zan_type_t *st = infer_expr_type(g, part, locals);
                    bool uns = st && st->element_type &&
                               (st->element_type->kind == TYPE_UINT ||
                                st->element_type->kind == TYPE_ULONG);
                    strs[i] = emit_to_cstr_u(g, val, uns ? 1 : 0);
                    lens[i] = zan_call2(g->builder, strlen_type, g->fn_strlen,
                                        &strs[i], 1, "nvlen");
                    owns[i] = 1;
                } else {
                    /* 整数插值格式化：基于 snprintf 转换为十进制字符串 */
                    /*
                     * zan_iwiden, not a bare SExt: `byte` is unsigned in this
                     * lowering, so 200 must widen to 200 not -56
                     */
                    LLVMValueRef val64 =
                        (LLVMGetIntTypeWidth(vt) < 64)
                            ? zan_iwiden(g->builder, val, i64) : val;
                    bool iu = expr_is_ulong(g, part, locals);
                    char fbuf[32];
                    bool want_f = false;
                    int flen = interp_format_to_printf(&spec, false, iu,
                        &want_f, fbuf, sizeof(fbuf));
                    if (want_f) {
                        /*
                         * widen the integer to double and fall into the float
                         * path so the %.Nlf spec sees a matching arg
                         */
                        LLVMValueRef as_d = (vt == LLVMFloatTypeKind)
                            ? LLVMBuildFPExt(g->builder, val,
                                LLVMDoubleTypeInContext(g->ctx), "f2d")
                            : LLVMBuildSIToFP(g->builder, val64,
                                LLVMDoubleTypeInContext(g->ctx), "i2d");
                        LLVMValueRef fmt = zan_irgen_intern_string(g,
                            fbuf);
                        LLVMValueRef null_ptr = LLVMConstNull(i8ptr);
                        LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
                        LLVMValueRef snp_args1[] = { null_ptr, zero, fmt, as_d };
                        LLVMValueRef needed = zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args1, 4, "needed");
                        LLVMValueRef needed64 = LLVMBuildSExt(g->builder, needed, i64, "n64");
                        LLVMValueRef buf_size = zan_add(g->builder, needed64, LLVMConstInt(i64, 1, 0), "bsz");
                        LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
                        LLVMValueRef snp_args2[] = { buf, buf_size, fmt, as_d };
                        zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args2, 4, "");
                        strs[i] = buf;
                        lens[i] = needed64;
                        owns[i] = 1;
                        continue;
                    }
                    if (flen <= 0) {
                        /* 无格式说明符的整型直接使用固定 21 字节栈缓冲区转换 */
                        LLVMValueRef buf = emit_string_alloc_rc(g,
                            LLVMConstInt(i64, 24, 0));
                        strs[i] = buf;
                        lens[i] = emit_itoa_into(g, buf, val64, iu ? 1 : 0);
                        owns[i] = 1;
                        continue;
                    }
                    LLVMValueRef fmt = zan_irgen_intern_string(g,
                        fbuf);
                    LLVMValueRef null_ptr = LLVMConstNull(i8ptr);
                    LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
                    LLVMValueRef snp_args1[] = { null_ptr, zero, fmt, val64 };
                    LLVMValueRef needed = zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args1, 4, "needed");
                    LLVMValueRef needed64 = LLVMBuildSExt(g->builder, needed, i64, "n64");
                    LLVMValueRef buf_size = zan_add(g->builder, needed64, LLVMConstInt(i64, 1, 0), "bsz");
                    LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
                    LLVMValueRef snp_args2[] = { buf, buf_size, fmt, val64 };
                    zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args2, 4, "");
                    strs[i] = buf;
                    lens[i] = needed64;
                    owns[i] = 1;
                }
            }
        }

        /* compute total length */
        LLVMValueRef total_len = LLVMConstInt(i64, 0, 0);
        for (int i = 0; i < n; i++) {
            total_len = zan_add(g->builder, total_len, lens[i], "tlen");
        }
        LLVMValueRef alloc_size = zan_add(g->builder, total_len, LLVMConstInt(i64, 1, 0), "asz");

        /* allocate result buffer with rc header */
        LLVMValueRef result = emit_string_alloc_rc(g, alloc_size);

        /* 按托管长度 memcpy 各片段拼接字符串，避免 NUL 字符截断 */
        LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
        LLVMTypeRef memcpy_type = LLVMFunctionType(i8ptr,
            (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
        LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
        if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_type);
        LLVMValueRef off = LLVMConstInt(i64, 0, 0);
        for (int i = 0; i < n; i++) {
            LLVMValueRef dst = LLVMBuildGEP2(g->builder, i8, result, &off, 1,
                                             "ip.dst");
            zan_call2(g->builder, memcpy_type, memcpy_fn,
                (LLVMValueRef[]){ dst, strs[i], lens[i] }, 3, "");
            off = zan_add(g->builder, off, lens[i], "ip.off");
        }
        LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, result, &off, 1,
                                          "ip.end");
        LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);

        if (owns) {
            for (int i = 0; i < n; i++) {
                if (owns[i]) emit_string_release(g, strs[i]);
            }
        }
        /* every part was concatenated whole, so the total is the exact length */
        emit_string_len_set(g, result, total_len);
        free(strs);
        free(lens);
        free(owns);
        return result;
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

static LLVMValueRef emit_expr_member_access(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* 读取控制台当前前景色与背景色属性 */
        if (expr->member.object->kind == AST_IDENTIFIER &&
            expr->member.object->ident.name.len == 7 &&
            memcmp(expr->member.object->ident.name.str, "Console", 7) == 0 &&
            !local_find(locals, expr->member.object->ident.name) &&
            expr->member.name.len == 15) {
            if (memcmp(expr->member.name.str, "ForegroundColor", 15) == 0)
                return emit_console_color_get(g, 0);
            if (memcmp(expr->member.name.str, "BackgroundColor", 15) == 0)
                return emit_console_color_get(g, 1);
        }
        /* Cpu.HasPopcnt / Cpu.HasLzcnt / Cpu.HasSse42 / Cpu.HasAvx2 / Cpu.HasAesNi / Cpu.HasNeon */
        {
            bool is_cpu_obj = false;
            if (expr->member.object->kind == AST_IDENTIFIER &&
                expr->member.object->ident.name.len == 3 &&
                memcmp(expr->member.object->ident.name.str, "Cpu", 3) == 0 &&
                !local_find(locals, expr->member.object->ident.name)) {
                is_cpu_obj = true;
            } else if (expr->member.object->kind == AST_MEMBER_ACCESS &&
                       expr->member.object->member.name.len == 3 &&
                       memcmp(expr->member.object->member.name.str, "Cpu", 3) == 0) {
                is_cpu_obj = true;
            }
            if (is_cpu_obj) {
                int feat_id = 0;
                zan_istr_t mn = expr->member.name;
                if (mn.len == 9 && memcmp(mn.str, "HasPopcnt", 9) == 0) feat_id = 1;
                else if (mn.len == 8 && memcmp(mn.str, "HasLzcnt", 8) == 0) feat_id = 2;
                else if (mn.len == 8 && memcmp(mn.str, "HasSse42", 8) == 0) feat_id = 3;
                else if (mn.len == 7 && memcmp(mn.str, "HasAvx2", 7) == 0) feat_id = 4;
                else if (mn.len == 8 && memcmp(mn.str, "HasAesNi", 8) == 0) feat_id = 5;
                else if (mn.len == 7 && memcmp(mn.str, "HasNeon", 7) == 0) feat_id = 6;
                if (feat_id > 0) {
                    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                    LLVMValueRef id = LLVMConstInt(i32t, feat_id, 0);
                    LLVMValueRef fn = cpu_feature_fn(g);
                    LLVMTypeRef fnty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t }, 1, 0);
                    LLVMValueRef val = zan_call2(g->builder, fnty, fn, &id, 1, "cpu_feat");
                    return zan_icmp(g->builder, LLVMIntNE, val, LLVMConstInt(i32t, 0, 0), "has_feat");
                }
            }
        }
        /* Vector128.Zero / Vector128.AllBitsSet */
        {
            if (is_target_class_obj(expr->member.object, "Vector128", 9, locals)) {
                zan_istr_t mn = expr->member.name;
                LLVMTypeRef v16i8 = LLVMVectorType(LLVMInt8TypeInContext(g->ctx), 16);
                if (mn.len == 4 && memcmp(mn.str, "Zero", 4) == 0) {
                    return v16i8_to_vec128(g, LLVMConstNull(v16i8));
                }
                if (mn.len == 10 && memcmp(mn.str, "AllBitsSet", 10) == 0) {
                    return v16i8_to_vec128(g, LLVMConstAllOnes(v16i8));
                }
            }
        }
        /*
         * `ti.Name` / `ti.Kind` / `ti.FieldCount` on a TypeInfo, read straight
         * off the reflection record (irgen_reflect.c).
         */
        {
            zan_type_t *tit = infer_expr_type(g, expr->member.object, locals);
            if (zan_refl_is_typeinfo(tit)) {
                LLVMValueRef rv = NULL;
                if (refl_emit_typeinfo_member(g,
                        emit_guarded_member_object(g, expr, locals),
                        expr->member.name, NULL, 0, locals, &rv))
                    return rv;
            }
        }
        /* 可空值类型 HasValue 与 Value 属性读取 */
        {
            zan_type_t *nt = infer_expr_type(g, expr->member.object, locals);
            bool is_has = expr->member.name.len == 8 &&
                memcmp(expr->member.name.str, "HasValue", 8) == 0;
            bool is_val = expr->member.name.len == 5 &&
                memcmp(expr->member.name.str, "Value", 5) == 0;
            if (nt && nt->kind == TYPE_NULLABLE && (is_has || is_val)) {
                LLVMValueRef nv = emit_guarded_member_object(g, expr, locals);
                if (llvm_is_nullable(LLVMTypeOf(nv))) {
                    if (is_has) return nullable_has_value(g, nv);
                    emit_runtime_check(g,
                        LLVMBuildNot(g->builder, nullable_has_value(g, nv), "nv.novalue"),
                        expr->loc, "Nullable object must have a value");
                    return nullable_get_payload(g, nv);
                }
            }
        }
        /* Task 属性读取：读取帧结果或完成标志 */
        {
            zan_type_t *ot = infer_expr_type(g, expr->member.object, locals);
            if (ot && ot->kind == TYPE_TASK) {
                bool is_res = expr->member.name.len == 6 &&
                    memcmp(expr->member.name.str, "Result", 6) == 0;
                bool is_comp = expr->member.name.len == 11 &&
                    memcmp(expr->member.name.str, "IsCompleted", 11) == 0;
                if (is_res || is_comp) {
                    LLVMTypeRef ti64 = LLVMInt64TypeInContext(g->ctx);
                    if (is_res && ot->type_arg_count != 1) {
                        zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                            "Task.Result requires a Task<T> value");
                        return LLVMConstInt(ti64, 0, 0);
                    }
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef h = emit_guarded_member_object(g, expr, locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(h)) == LLVMPointerTypeKind)
                        h = LLVMBuildPtrToInt(g->builder, h, ti64, "task.h");
                    else if (LLVMGetIntTypeWidth(LLVMTypeOf(h)) < 64)
                        h = zan_iwiden(g->builder, h, ti64);
                    LLVMValueRef hp = LLVMBuildIntToPtr(g->builder, h, i8ptr, "task.fp");
                    int mode = is_comp ? 2 : 1;
                    return emit_task_member(g, hp,
                        is_res ? ot->type_args[0] : NULL, mode);
                }
            }
        }
        /* 标量类型成员访问防护与拦截 */
        {
            bool is_prim_const = false;
            if (expr->member.object->kind == AST_IDENTIFIER) {
                zan_istr_t on = expr->member.object->ident.name;
                zan_istr_t mn = expr->member.name;
                bool is_scalar_name = (on.len == 3 && memcmp(on.str, "int", 3) == 0) ||
                    (on.len == 4 && (memcmp(on.str, "uint", 4) == 0 ||
                                     memcmp(on.str, "long", 4) == 0 ||
                                     memcmp(on.str, "byte", 4) == 0 ||
                                     memcmp(on.str, "char", 4) == 0)) ||
                    (on.len == 5 && (memcmp(on.str, "ulong", 5) == 0 ||
                                     memcmp(on.str, "short", 5) == 0 ||
                                     memcmp(on.str, "sbyte", 5) == 0 ||
                                     memcmp(on.str, "float", 5) == 0)) ||
                    (on.len == 6 && (memcmp(on.str, "ushort", 6) == 0 ||
                                     memcmp(on.str, "double", 6) == 0));
                is_prim_const = is_scalar_name && (
                    (mn.len == 8 && (memcmp(mn.str, "MaxValue", 8) == 0 ||
                                     memcmp(mn.str, "MinValue", 8) == 0)) ||
                    (mn.len == 3 && memcmp(mn.str, "NaN", 3) == 0) ||
                    (mn.len == 16 && (memcmp(mn.str, "PositiveInfinity", 16) == 0 ||
                                      memcmp(mn.str, "NegativeInfinity", 16) == 0)) ||
                    (mn.len == 7 && memcmp(mn.str, "Epsilon", 7) == 0));
            }
            zan_type_t *mt = infer_expr_type(g, expr->member.object, locals);
            if (mt && !is_prim_const && mt->kind != TYPE_CLASS &&
                mt->kind != TYPE_STRUCT &&
                mt->kind != TYPE_INTERFACE && mt->kind != TYPE_ENUM &&
                mt->kind != TYPE_ARRAY && mt->kind != TYPE_NULLABLE &&
                mt->kind != TYPE_DELEGATE && mt->kind != TYPE_TYPE_PARAM &&
                mt->kind != TYPE_OBJECT && mt->kind != TYPE_ERROR &&
                mt->kind != TYPE_VOID && !is_span_type(mt)) {
                bool is_len = expr->member.name.len == 6 &&
                    memcmp(expr->member.name.str, "Length", 6) == 0;
                if (!(mt->kind == TYPE_STRING && is_len)) {
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "type '%s' has no member '%.*s'",
                        mt->name.str ? mt->name.str : "?",
                        (int)expr->member.name.len, expr->member.name.str);
                    return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
                }
            }
        }
        /* Math.PI → constant */
        if (expr->member.object->kind == AST_IDENTIFIER) {
            zan_istr_t obj = expr->member.object->ident.name;
            if (obj.len == 4 && memcmp(obj.str, "Math", 4) == 0) {
                if (expr->member.name.len == 2 && memcmp(expr->member.name.str, "PI", 2) == 0) {
                    return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), 3.14159265358979323846);
                }
                if (expr->member.name.len == 4 && memcmp(expr->member.name.str, "Sqrt", 4) == 0) {
                    /* Math.Sqrt is handled as a call — shouldn't reach here */
                    return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
                }
            }
        }

        /* 内置标量类型常量（int.MaxValue、double.NaN 等）发射编译期常量 */
        if (expr->member.object->kind == AST_IDENTIFIER) {
            zan_istr_t obj = expr->member.object->ident.name;
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            bool is_int   = obj.len == 3 && memcmp(obj.str, "int", 3) == 0;
            bool is_uint  = obj.len == 4 && memcmp(obj.str, "uint", 4) == 0;
            bool is_long  = obj.len == 4 && memcmp(obj.str, "long", 4) == 0;
            bool is_ulong = obj.len == 5 && memcmp(obj.str, "ulong", 5) == 0;
            bool is_short = obj.len == 5 && memcmp(obj.str, "short", 5) == 0;
            bool is_ushort= obj.len == 6 && memcmp(obj.str, "ushort", 6) == 0;
            bool is_byte  = obj.len == 4 && memcmp(obj.str, "byte", 4) == 0;
            bool is_sbyte = obj.len == 5 && memcmp(obj.str, "sbyte", 5) == 0;
            bool is_float = obj.len == 5 && memcmp(obj.str, "float", 5) == 0;
            bool is_double= obj.len == 6 && memcmp(obj.str, "double", 6) == 0;
            bool is_char  = obj.len == 4 && memcmp(obj.str, "char", 4) == 0;
            if (is_int || is_uint || is_long || is_ulong || is_short ||
                is_ushort || is_byte || is_sbyte || is_float || is_double ||
                is_char) {
                zan_istr_t m = expr->member.name;
                if (m.len == 8 && memcmp(m.str, "MaxValue", 8) == 0) {
                    if (is_int)    return LLVMConstInt(i32, INT32_MAX, 0);
                    if (is_uint)   return LLVMConstInt(i32, UINT32_MAX, 0);
                    if (is_long)   return LLVMConstInt(i64, INT64_MAX, 0);
                    if (is_ulong)  return LLVMConstInt(i64, UINT64_MAX, 0);
                    if (is_short)  return LLVMConstInt(i32, INT16_MAX, 0);
                    if (is_ushort) return LLVMConstInt(i32, UINT16_MAX, 0);
                    if (is_byte)   return LLVMConstInt(i32, UINT8_MAX, 0);
                    if (is_sbyte)  return LLVMConstInt(i32, INT8_MAX, 0);
                    if (is_char)   return LLVMConstInt(i64, 0xFFFF, 0);
                    if (is_float)  return LLVMConstReal(LLVMFloatTypeInContext(g->ctx), 3.40282346638528859812e+38);
                    if (is_double) return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), 1.7976931348623157e+308);
                }
                if (m.len == 8 && memcmp(m.str, "MinValue", 8) == 0) {
                    if (is_int)   return LLVMConstInt(i32, (unsigned long long)INT32_MIN, 1);
                    if (is_long)  return LLVMConstInt(i64, (unsigned long long)INT64_MIN, 1);
                    if (is_short) return LLVMConstInt(i32, (unsigned long long)INT16_MIN, 1);
                    if (is_sbyte) return LLVMConstInt(i32, (unsigned long long)INT8_MIN, 1);
                    if (is_float) return LLVMConstReal(LLVMFloatTypeInContext(g->ctx), -3.40282346638528859812e+38);
                    if (is_double) return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), -1.7976931348623157e+308);
                }
                if (is_double || is_float) {
                    /* 浮点特殊常量位精确表示（如正负无穷与 NaN） */
                    if (m.len == 3 && memcmp(m.str, "NaN", 3) == 0) {
                        if (is_float)
                            return LLVMConstBitCast(
                                LLVMConstInt(i32, 0x7FC00000U, 0),
                                LLVMFloatTypeInContext(g->ctx));
                        return LLVMConstBitCast(
                            LLVMConstInt(i64, 0x7FF8000000000000ULL, 0),
                            LLVMDoubleTypeInContext(g->ctx));
                    }
                    if (m.len == 16 && memcmp(m.str, "PositiveInfinity", 16) == 0) {
                        if (is_float)
                            return LLVMConstBitCast(
                                LLVMConstInt(i32, 0x7F800000U, 0),
                                LLVMFloatTypeInContext(g->ctx));
                        return LLVMConstBitCast(
                            LLVMConstInt(i64, 0x7FF0000000000000ULL, 0),
                            LLVMDoubleTypeInContext(g->ctx));
                    }
                    if (m.len == 16 && memcmp(m.str, "NegativeInfinity", 16) == 0) {
                        if (is_float)
                            return LLVMConstBitCast(
                                LLVMConstInt(i32, 0xFF800000U, 0),
                                LLVMFloatTypeInContext(g->ctx));
                        return LLVMConstBitCast(
                            LLVMConstInt(i64, 0xFFF0000000000000ULL, 0),
                            LLVMDoubleTypeInContext(g->ctx));
                    }
                }
                if (is_float && m.len == 7 && memcmp(m.str, "Epsilon", 7) == 0)
                    return LLVMConstReal(LLVMFloatTypeInContext(g->ctx),
                        1.4012984643248170709e-45);
            }
        }

        /* .Length property on Span -- read the length field from the value */
        if (expr->member.name.len == 6 &&
            memcmp(expr->member.name.str, "Length", 6) == 0) {
            zan_type_t *st = infer_expr_type(g, expr->member.object, locals);
            if (is_span_type(st)) {
                LLVMValueRef span_val = emit_guarded_member_object(g, expr, locals);
                LLVMValueRef len = LLVMBuildExtractValue(g->builder, span_val, 1, "sp.len");
                return LLVMBuildTrunc(g->builder, len,
                    LLVMInt32TypeInContext(g->ctx), "sp.len32");
            }
        }

        /*
         * .Count property on List — read count field from list struct
         * (works for local vars and fields).
         */
        if (expr->member.name.len == 5 && memcmp(expr->member.name.str, "Count", 5) == 0) {
            zan_type_t *lt = infer_expr_type(g, expr->member.object, locals);
            if (lt && type_named(lt, "List", 4)) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef raw_ptr = emit_guarded_member_object(g, expr, locals);
                LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                    LLVMPointerType(g->list_struct_type, 0), "lptr");
                LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                /*
                 * an owned receiver temp (e.g. `Coll.FindAll().Count`) is
                 * consumed by this read and must be released, or the returned
                 * List and its RC elements leak.
                 */
                emit_release_owned_call_temp(g, expr->member.object, raw_ptr, locals);
                return count;
            }
        }

        /* Dict.Count：读取字典当前项数 */
        if (expr->member.name.len == 5 && memcmp(expr->member.name.str, "Count", 5) == 0) {
            zan_type_t *dt = infer_expr_type(g, expr->member.object, locals);
            if (dt && type_named(dt, "Dict", 4)) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef raw = emit_guarded_member_object(g, expr, locals);
                LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                    LLVMPointerType(g->dict_struct_type, 0), "dp");
                LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "dcnt");
                emit_release_owned_call_temp(g, expr->member.object, raw, locals);
                return cnt;
            }
        }

        /* Dict.Keys 与 Dict.Values：将键/值集合拷贝至独立 List 实例 */
        if ((expr->member.name.len == 4 && memcmp(expr->member.name.str, "Keys", 4) == 0) ||
            (expr->member.name.len == 6 && memcmp(expr->member.name.str, "Values", 6) == 0)) {
            zan_type_t *dt = infer_expr_type(g, expr->member.object, locals);
            if (dt && type_named(dt, "Dict", 4)) {
                bool want_keys = (expr->member.name.len == 4);
                zan_type_t *elem = want_keys ? dict_key_type(g, dt) : dict_value_type(dt);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef raw = emit_guarded_member_object(g, expr, locals);
                LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                    LLVMPointerType(g->dict_struct_type, 0), "dp");
                LLVMValueRef dict_value_words = load_dict_value_words(g, raw);
                LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "dcnt");
                LLVMValueRef srcp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp,
                    want_keys ? 2 : 3, want_keys ? "kp" : "vp");
                LLVMTypeRef src_slot_ty = want_keys ? i8ptr : i64;
                LLVMValueRef src = LLVMBuildLoad2(g->builder,
                    LLVMPointerType(src_slot_ty, 0), srcp, "dsrc");
                /* fresh List: count = dict count, capacity = count + 8 */
                LLVMValueRef list_raw = emit_alloc_rc_collection(g, expr, 24, 1, elem);
                LLVMValueRef lp = LLVMBuildBitCast(g->builder, list_raw,
                    LLVMPointerType(g->list_struct_type, 0), "lp");
                zan_store_fit(g, cnt,
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "lcnt"));
                LLVMValueRef cap = zan_add(g->builder, cnt,
                    LLVMConstInt(i64, 8, 0), "lcap");
                zan_store_fit(g, cap,
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 1, "lcapp"));
                unsigned elem_words = elem_slot_words(g, elem);
                LLVMValueRef data_words = zan_mul(g->builder, cap,
                    LLVMConstInt(i64, elem_words, 0), "lwords");
                LLVMValueRef data_size = zan_mul(g->builder, data_words,
                    LLVMConstInt(i64, 8, 0), "lsz");
                LLVMValueRef data = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g),
                    (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), data_size }, 2, "ldata");
                zan_irgen_emit_oom_check(g, g->current_fn, data);
                LLVMValueRef data_typed = LLVMBuildBitCast(g->builder, data,
                    LLVMPointerType(i64, 0), "ldp");
                zan_store_fit(g, data_typed,
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "ldf"));
                /* copy loop */
                LLVMValueRef idx_a = emit_entry_alloca(g, i64, "dk.i");
                zan_store_fit(g, LLVMConstInt(i64, 0, 0), idx_a);
                LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dk.cond");
                LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dk.body");
                LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dk.done");
                LLVMBuildBr(g->builder, cond_bb);
                LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                LLVMBuildCondBr(g->builder,
                    zan_icmp(g->builder, LLVMIntUGE, ci, cnt, "cdone"),
                    done_bb, body_bb);
                LLVMPositionBuilderAtEnd(g->builder, body_bb);
                LLVMValueRef src_index = want_keys ? ci
                    : zan_mul(g->builder, ci, dict_value_words, "dv.word");
                LLVMValueRef sslot = LLVMBuildGEP2(g->builder, src_slot_ty,
                    src, &src_index, 1, "ssl");
                LLVMValueRef sval = want_keys
                    ? LLVMBuildLoad2(g->builder, src_slot_ty, sslot, "sv")
                    : load_collection_slot_value(g, elem, sslot);
                LLVMValueRef dst_index = slot_word_index(g, ci, elem_words);
                LLVMValueRef dslot = LLVMBuildGEP2(g->builder, i64,
                    data_typed, &dst_index, 1, "dsl");
                emit_collection_slot_store(g, elem, i64, dslot, sval, NULL, locals, 0);
                zan_store_fit(g,
                    zan_add(g->builder, ci, LLVMConstInt(i64, 1, 0), "ni"), idx_a);
                LLVMBuildBr(g->builder, cond_bb);
                LLVMPositionBuilderAtEnd(g->builder, done_bb);
                emit_release_owned_call_temp(g, expr->member.object, raw, locals);
                return LLVMBuildBitCast(g->builder, lp, i8ptr, "keysv");
            }
        }

        /* array.Length：从数组头部读取元素个数，空引用返回 0 */
        /*
         * Array .Count is aliased to .Length: the params bundle is a plain
         * array, so reading .Count on it reads the array length.
         */
        if (expr->member.name.len == 5 &&
            memcmp(expr->member.name.str, "Count", 5) == 0) {
            zan_type_t *at = infer_expr_type(g, expr->member.object, locals);
            if (at && at->kind == TYPE_ARRAY) {
                LLVMValueRef arr = emit_guarded_member_object(g, expr, locals);
                if (LLVMGetTypeKind(LLVMTypeOf(arr)) == LLVMPointerTypeKind) {
                    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, arr,
                        LLVMConstNull(LLVMTypeOf(arr)), "arr.null");
                    emit_runtime_check(g, isnull, expr->member.object->loc,
                        "null reference where an array is required (.Count)");
                    arr = emit_soft_base_select(g, arr, isnull,
                                                expr->member.object->loc);
                }
                return zan_array_len(g, arr);
            }
        }
        if (expr->member.name.len == 6 &&
            memcmp(expr->member.name.str, "Length", 6) == 0) {
            zan_type_t *at = infer_expr_type(g, expr->member.object, locals);
            if (at && at->kind == TYPE_ARRAY) {
                LLVMValueRef arr = emit_guarded_member_object(g, expr, locals);
                if (LLVMGetTypeKind(LLVMTypeOf(arr)) == LLVMPointerTypeKind) {
                    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, arr,
                        LLVMConstNull(LLVMTypeOf(arr)), "arr.null");
                    emit_runtime_check(g, isnull, expr->member.object->loc,
                        "null reference where an array is required (.Length)");
                    arr = emit_soft_base_select(g, arr, isnull,
                                                expr->member.object->loc);
                }
                return zan_array_len(g, arr);
            }
        }

        /* StringBuilder.Length: load the count field. */
        if (expr->member.name.len == 6 && memcmp(expr->member.name.str, "Length", 6) == 0) {
            zan_type_t *sbt = infer_expr_type(g, expr->member.object, locals);
            if (sbt && type_named(sbt, "StringBuilder", 13)) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef raw = emit_guarded_member_object(g, expr, locals);
                LLVMValueRef sbp = LLVMBuildBitCast(g->builder, raw,
                    LLVMPointerType(g->sb_struct_type, 0), "sbp");
                LLVMValueRef cptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 0, "sbcp");
                LLVMValueRef sblen = LLVMBuildLoad2(g->builder, i64, cptr, "sblen");
                emit_release_owned_call_temp(g, expr->member.object, raw, locals);
                return sblen;
            }
        }

        /* String.Length 属性：读取头部缓存长度，必要时回退至 strlen 扫描 */
        if (expr->member.name.len == 6 && memcmp(expr->member.name.str, "Length", 6) == 0 &&
            recv_is_stringlike(g, expr->member.object, locals)) {
            LLVMValueRef obj_val = emit_guarded_member_object(g, expr, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                /* the length is an i64; `int` is i64 so return it directly. */
                LLVMValueRef len = emit_string_length(g, obj_val, expr->loc);
                emit_release_owned_call_temp(g, expr->member.object, obj_val, locals);
                return len;
            }
        }

        /* ConsoleColor 枚举成员读取：映射至 0..15 固定序数 */
        if (expr->member.object->kind == AST_IDENTIFIER &&
            expr->member.object->ident.name.len == 12 &&
            memcmp(expr->member.object->ident.name.str, "ConsoleColor", 12) == 0) {
            static const struct { const char *n; int l; } cc[16] = {
                {"Black",5},{"DarkBlue",8},{"DarkGreen",9},{"DarkCyan",8},
                {"DarkRed",7},{"DarkMagenta",11},{"DarkYellow",10},{"Gray",4},
                {"DarkGray",8},{"Blue",4},{"Green",5},{"Cyan",4},
                {"Red",3},{"Magenta",7},{"Yellow",6},{"White",5} };
            for (int i = 0; i < 16; i++) {
                if (expr->member.name.len == cc[i].l &&
                    memcmp(expr->member.name.str, cc[i].n, (size_t)cc[i].l) == 0)
                    return LLVMConstInt(LLVMInt64TypeInContext(g->ctx),
                                        (uint64_t)i, 0);
            }
        }

        /* 枚举成员访问：EnumType.MemberName 降解为整型常量 */
        if (expr->member.object->kind == AST_IDENTIFIER) {
            zan_symbol_t *enum_sym = zan_binder_lookup(g->binder, expr->member.object->ident.name);
            if (enum_sym && enum_sym->kind == SYM_ENUM) {
                int64_t enum_val = 0;
                for (int ei = 0; ei < enum_sym->member_count; ei++) {
                    if (enum_sym->members[ei]->kind == SYM_ENUM_MEMBER) {
                        if (enum_sym->members[ei]->name.len == expr->member.name.len &&
                            memcmp(enum_sym->members[ei]->name.str, expr->member.name.str,
                                   (size_t)expr->member.name.len) == 0) {
                            zan_ast_node_t *em_decl = enum_sym->members[ei]->decl;
                            if (em_decl && em_decl->kind == AST_ENUM_MEMBER &&
                                em_decl->enum_member.value &&
                                em_decl->enum_member.value->kind == AST_INT_LITERAL) {
                                enum_val = em_decl->enum_member.value->int_val;
                            }
                            return LLVMConstInt(LLVMInt64TypeInContext(g->ctx),
                                               (uint64_t)(uint32_t)enum_val, 0);
                        }
                        zan_ast_node_t *em_decl = enum_sym->members[ei]->decl;
                        if (em_decl && em_decl->kind == AST_ENUM_MEMBER &&
                            em_decl->enum_member.value &&
                            em_decl->enum_member.value->kind == AST_INT_LITERAL) {
                            enum_val = em_decl->enum_member.value->int_val + 1;
                        } else {
                            enum_val++;
                        }
                    }
                }
                return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
            }
        }

        /* 静态字段访问：ClassName.StaticField 直接读取全局变量 */
        if (expr->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->member.object->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder, expr->member.object->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *fs = get_field_sym(cs, expr->member.name);
                /*
                 * custom-getter static property: dispatch to the static
                 * get_Prop() (no receiver) instead of the global slot
                 */
                zan_symbol_t *getter = property_getter_sym(g, fs);
                if (getter) {
                    return emit_property_getter_call(g, getter, cs->type, NULL,
                        expr->member.object, locals);
                }
                LLVMValueRef gv = get_static_field_global(g, cs, fs,
                    static_access_inst(g, expr->member.object));
                if (gv) {
                    LLVMTypeRef ft = fs->type ? map_type(g, fs->type)
                                              : LLVMInt64TypeInContext(g->ctx);
                    return promote_loaded(g,
                        LLVMBuildLoad2(g->builder, ft, gv, "sfld"), fs->type);
                }
            }
        }

        /* 静态方法引用作为委托传递：生成静态方法组记录 */
        if (expr->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->member.object->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder, expr->member.object->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *ms = get_method_sym(cs, expr->member.name);
                if (ms) {
                    if (target_is_wasm32(g)) {
                        LLVMValueRef clo = emit_method_group_closure(g, ms, NULL,
                                                                     expr->loc);
                        if (clo) return clo;
                    }
                    for (int fi = irgen_find_function(g, ms); fi >= 0; fi = -1) {
                        if (g->functions[fi].sym == ms) {
                            return g->functions[fi].fn;
                        }
                    }
                }
            }
        }

        /* struct field access: obj.Field */
        if (expr->member.object->kind == AST_IDENTIFIER) {
            local_var_t *local = local_find(locals, expr->member.object->ident.name);
            if (local && local->type && (local->type->kind == TYPE_STRUCT || local->type->kind == TYPE_CLASS)) {
                zan_symbol_t *type_sym = local->type->sym;
                if (type_sym) {
                    int fi = get_field_index(type_sym, expr->member.name);
                    if (fi >= 0) {
                        /*
                         * custom-getter property on a local receiver: dispatch
                         * to the getter instead of reading the slot
                         */
                        zan_symbol_t *psym = get_field_sym(type_sym, expr->member.name);
                        zan_symbol_t *getter = property_getter_sym(g, psym);
                        if (getter) {
                            LLVMTypeRef st = get_struct_llvm_type(g, type_sym);
                            LLVMValueRef rval = struct_base_ptr(g, local, st);
                            return emit_property_getter_call(g, getter,
                                local->type, rval, expr->member.object, locals);
                        }
                        LLVMTypeRef st = get_struct_llvm_type(g, type_sym);
                        if (st) {
                            /* 结构体字段访问：获取指针并通过 GEP 寻址目标字段 */
                            LLVMValueRef struct_ptr = struct_base_ptr(g, local, st);
                            if (local->type->kind == TYPE_CLASS &&
                                LLVMGetTypeKind(LLVMTypeOf(struct_ptr)) == LLVMPointerTypeKind) {
                                LLVMValueRef isnull = zan_icmp(g->builder,
                                    LLVMIntEQ, struct_ptr,
                                    LLVMConstNull(LLVMTypeOf(struct_ptr)),
                                    "recv.null");
                                char recv_msg[256];
                                snprintf(recv_msg, sizeof(recv_msg),
                                    "null reference: receiver '%.*s' is null",
                                    (int)expr->member.object->ident.name.len,
                                    expr->member.object->ident.name.str);
                                emit_runtime_check(g, isnull,
                                    expr->member.object->loc, recv_msg);
                                struct_ptr = emit_soft_base_select(g, struct_ptr,
                                    isnull, expr->member.object->loc);
                            }
                            LLVMValueRef field_ptr = emit_field_ptr(g, type_sym, st, struct_ptr, fi, "fld");
                            zan_symbol_t *fsym = get_field_sym(type_sym, expr->member.name);
                            /*
                             * A `T` field of Box<Vec> holds a Vec, not a
                             * pointer to one: read the slot at the concrete
                             * type so a value type comes back by value.
                             */
                            zan_type_t *fty = fsym
                                ? subst_type_param(fsym->type, local->type)
                                : NULL;
                            LLVMTypeRef field_type = fty ? map_type(g, fty)
                                : LLVMInt64TypeInContext(g->ctx);
                            LLVMValueRef fv0 =
                                (fsym && (fsym->modifiers & MOD_WEAK))
                                ? emit_weak_field_load(g, field_ptr, field_type)
                                : LLVMBuildLoad2(g->builder, field_type, field_ptr, "fval");
                            LLVMValueRef fv = promote_loaded(g, fv0, fty);
                            return fv;
                        }
                    }
                }
            }
        }

        /*
         * general field access: <expr>.field where <expr> yields a class
         * instance pointer (e.g. list[i].field, a.b.field, foo().field).
         */
        {
            zan_symbol_t *cls = expr_class_sym(g, expr->member.object, locals);
            if (cls) {
                int fi = get_field_index(cls, expr->member.name);
                if (fi >= 0) {
                    /* 索引结构体元素字段读取 (l[i].field) */
                    if (expr->member.object->kind == AST_INDEX &&
                        cls->kind == SYM_STRUCT) {
                        zan_type_t *et = infer_expr_type(g,
                            expr->member.object, locals);
                        LLVMTypeRef st = get_struct_llvm_type(g, cls);
                        LLVMValueRef ep = (st && !type_is_binding(et))
                            ? emit_struct_elem_ptr(g, expr->member.object, locals)
                            : NULL;
                        if (ep) {
                            LLVMValueRef sptr = LLVMBuildBitCast(g->builder, ep,
                                LLVMPointerType(st, 0), "ep.s");
                            zan_symbol_t *gpsym = get_field_sym(cls,
                                expr->member.name);
                            zan_symbol_t *getter = property_getter_sym(g, gpsym);
                            if (getter) {
                                return emit_property_getter_call(g, getter, et,
                                    sptr, expr->member.object, locals);
                            }
                            LLVMValueRef field_ptr = emit_field_ptr(g, cls, st,
                                sptr, fi, "gfld");
                            zan_type_t *fty = gpsym
                                ? subst_type_param(gpsym->type, et) : NULL;
                            LLVMTypeRef ft = fty ? map_type(g, fty)
                                : LLVMInt64TypeInContext(g->ctx);
                            LLVMValueRef gfv0 =
                                (gpsym && (gpsym->modifiers & MOD_WEAK))
                                ? emit_weak_field_load(g, field_ptr, ft)
                                : LLVMBuildLoad2(g->builder, ft, field_ptr, "gfval");
                            return promote_loaded(g, gfv0, fty);
                        }
                    }
                    /*
                     * A property with a custom getter is computed, not read out
                     * of a backing slot: dispatch to the synthesized getter.
                     */
                    zan_symbol_t *psym = get_field_sym(cls, expr->member.name);
                    zan_symbol_t *getter = property_getter_sym(g, psym);
                    if (getter) {
                        zan_type_t *rct = infer_expr_type(g, expr->member.object, locals);
                        LLVMValueRef rval = emit_guarded_member_object(g, expr, locals);
                        /* base.Prop 属性读取：直接调用基类 getter，绕过虚方法分发 */
                        if (expr->member.object->kind == AST_BASE_EXPR)
                            return emit_property_getter_call(g, getter, NULL,
                                rval, expr->member.object, locals);
                        return emit_property_getter_call(g, getter, rct, rval,
                                                         expr->member.object,
                                                         locals);
                    }
                    LLVMTypeRef st = get_struct_llvm_type(g, cls);
                    LLVMValueRef obj_val = emit_guarded_member_object(g, expr, locals);
                    /*
                     * a value struct rvalue (list[i] of a struct element,
                     * a call result) is in a register, not behind a pointer
                     */
                    if (LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMStructTypeKind) {
                        LLVMValueRef sfv = LLVMBuildExtractValue(g->builder,
                            obj_val, (unsigned)fi, "sfval");
                        zan_symbol_t *fsym = get_field_sym(cls, expr->member.name);
                        zan_type_t *rct = infer_expr_type(g, expr->member.object, locals);
                        if (fsym) {
                            zan_type_t *ct = subst_type_param(fsym->type, rct);
                            if (ct != fsym->type)
                                sfv = emit_boundary_coerce(g, sfv, map_type(g, ct));
                        }
                        return sfv;
                    }
                    if (st && LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                        LLVMValueRef field_ptr = emit_field_ptr(g, cls, st, obj_val, fi, "gfld");
                        zan_symbol_t *fsym = get_field_sym(cls, expr->member.name);
                        zan_type_t *rct = infer_expr_type(g, expr->member.object, locals);
                        zan_type_t *fty = fsym
                            ? subst_type_param(fsym->type, rct) : NULL;
                        LLVMTypeRef ft = fty ? map_type(g, fty)
                                             : LLVMInt64TypeInContext(g->ctx);
                        LLVMValueRef gfv0 =
                            (fsym && (fsym->modifiers & MOD_WEAK))
                            ? emit_weak_field_load(g, field_ptr, ft)
                            : LLVMBuildLoad2(g->builder, ft, field_ptr, "gfval");
                        LLVMValueRef gfv = promote_loaded(g, gfv0, fty);
                        return finish_member_of_temp(g, expr, locals, rct,
                                                     obj_val, gfv);
                    }
                }
            }
        }
        /* 实例方法组作为委托传递：绑定接收者并保留引用 */
        {
            zan_symbol_t *cls = expr_class_sym(g, expr->member.object, locals);
            if (cls) {
                zan_symbol_t *ms = get_method_sym(cls, expr->member.name);
                if (ms && ms->decl && ms->decl->kind == AST_METHOD_DECL &&
                    (ms->decl->method_decl.modifiers & MOD_STATIC) == 0) {
                    LLVMValueRef recv = emit_guarded_member_object(g, expr, locals);
                    LLVMValueRef clo = recv
                        ? emit_method_group_closure(g, ms, recv, expr->loc)
                        : NULL;
                    if (clo) {
                        /* 闭包捕获接收者并维持 +1 强引用 */
                        zan_type_t *rct = infer_expr_type(g, expr->member.object, locals);
                        if (receiver_is_owned_temp(g, expr->member.object, locals,
                                                   rct, recv))
                            emit_rc_release_for_type(g, rct, recv);
                        return clo;
                    }
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "instance method group '%.*s' cannot be used as a value",
                        (int)expr->member.name.len, expr->member.name.str);
                    return LLVMConstNull(
                        LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
                }
            }
        }
        /* 健壮性：未定义字段读取安全报错与诊断 */
        {
            zan_symbol_t *rcls = expr_class_sym(g, expr->member.object, locals);
            if (!rcls && expr->member.object->kind == AST_IDENTIFIER &&
                !local_find(locals, expr->member.object->ident.name)) {
                zan_symbol_t *ts = zan_binder_lookup(g->binder,
                                                     expr->member.object->ident.name);
                if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT))
                    rcls = ts;
            }
            if (rcls && (rcls->kind == SYM_CLASS || rcls->kind == SYM_STRUCT) &&
                !type_declares_member(rcls, expr->member.name)) {
                zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                    "'%.*s' has no member '%.*s'",
                    (int)rcls->name.len, rcls->name.str,
                    (int)expr->member.name.len, expr->member.name.str);
            } else if (!rcls) {
                /* Builtin collection: report an error when accessing an unknown member. */
                zan_type_t *rt = infer_expr_type(g, expr->member.object, locals);
                if (rt && is_builtin_collection_type(rt))
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "'%.*s' has no member '%.*s'",
                        (int)rt->name.len, rt->name.str,
                        (int)expr->member.name.len, expr->member.name.str);
                /* 链式调用未定义成员安全处理 */
                zan_ast_node_t *ro = expr->member.object;
                if (!rt && ro->kind == AST_CALL && ro->call.callee &&
                    ro->call.callee->kind == AST_MEMBER_ACCESS) {
                    zan_ast_node_t *inner = ro->call.callee;
                    zan_symbol_t *icls = expr_class_sym(g, inner->member.object,
                                                        locals);
                    if (icls &&
                        (icls->kind == SYM_CLASS || icls->kind == SYM_STRUCT) &&
                        !type_declares_member(icls, inner->member.name))
                        zan_diag_emit(g->diag, DIAG_ERROR, inner->loc,
                            "'%.*s' has no member '%.*s'",
                            (int)icls->name.len, icls->name.str,
                            (int)inner->member.name.len,
                            inner->member.name.str);
                }
            }
        }
        if (g->diag->error_count == 0) {
            zan_symbol_t *rcls = expr_class_sym(g, expr->member.object, locals);
            if (rcls) {
                zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                    "'%.*s' has no member '%.*s'",
                    (int)rcls->name.len, rcls->name.str,
                    (int)expr->member.name.len, expr->member.name.str);
            } else {
                zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                    "cannot resolve member '%.*s'",
                    (int)expr->member.name.len, expr->member.name.str);
            }
        }
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

/* 临时容器对象访问完成后及时释放引用 */
static LLVMValueRef finish_index_of_temp(zan_irgen_t *g, zan_ast_node_t *expr,
                                         local_scope_t *locals,
                                         zan_type_t *container_type,
                                         zan_type_t *elem_type,
                                         LLVMValueRef container,
                                         LLVMValueRef elem) {
    if (!container || !container_type ||
        !expr_yields_owned_rc_value(g, expr->index.object, locals) ||
        expr_is_local_ident(expr->index.object, locals) ||
        !is_rc_managed_type(container_type))
        return elem;
    if (elem_type && is_rc_managed_type(elem_type) &&
        LLVMGetTypeKind(LLVMTypeOf(elem)) == LLVMPointerTypeKind)
        emit_rc_retain_for_type(g, elem_type, elem);
    emit_rc_release_for_type(g, container_type, container);
    return elem;
}

static LLVMValueRef emit_expr_index(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* 实例索引器 obj[i] 降解为 op_index 方法调用 */
        {
            zan_type_t *oit = infer_expr_type(g, expr->index.object, locals);
            if (oit && (oit->kind == TYPE_CLASS || oit->kind == TYPE_STRUCT) &&
                oit->sym) {
                zan_istr_t op_istr = {(char *)"op_index", 8};
                zan_ast_node_t *op_call = zan_ast_new(g->arena, AST_CALL, expr->loc);
                op_call->call.callee = NULL;
                zan_ast_list_init(&op_call->call.args);
                zan_ast_list_init(&op_call->call.type_args);
                zan_ast_list_push(&op_call->call.args, expr->index.index, g->arena);
                zan_symbol_t *op_sym = resolve_op_overload(g, oit->sym,
                                                           op_istr, op_call, locals);
                if (op_sym) {
                    for (int fi = irgen_find_function(g, op_sym); fi >= 0; fi = -1) {
                        if (g->functions[fi].sym == op_sym) {
                            LLVMValueRef recv_val = emit_expr(g, expr->index.object, locals);
                            LLVMValueRef *call_args = (LLVMValueRef *)calloc(2, sizeof(LLVMValueRef));
                            call_args[0] = recv_val;
                            if (LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMStructTypeKind) {
                                LLVMValueRef rslot = emit_entry_alloca(g,
                                    LLVMTypeOf(recv_val), "opx.recv");
                                LLVMBuildStore(g->builder, recv_val, rslot);
                                call_args[0] = rslot;
                            }
                            int recv_eh_pushed = 0;
                            if (oit->kind == TYPE_CLASS &&
                                !expr_is_local_ident(expr->index.object, locals) &&
                                expr_yields_owned_rc_value(g, expr->index.object, locals) &&
                                LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMPointerTypeKind) {
                                emit_eh_tmp_push(g, recv_val);
                                recv_eh_pushed = 1;
                            }
                            call_args[1] = emit_arg_typed(g, expr->index.index,
                                method_param_type_at(g, op_sym,
                                    op_index_param_offset(op_sym), NULL,
                                    expr->index.object, locals), locals);
                            LLVMTypeRef mft = g->functions[fi].fn_type;
                            LLVMValueRef mfn = route_generic_method(g, oit,
                                op_sym, g->functions[fi].fn, mft, &mft);
                            const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "opx";
                            LLVMValueRef result = emit_dispatch_call(g,
                                oit->sym, op_sym, mfn, mft, call_args, 2, cn);
                            result = coerce_generic_result(g, result, op_sym, oit);
                            if (recv_eh_pushed) emit_eh_tmp_pop(g);
                            emit_release_owned_call_temp(g, expr->index.object,
                                recv_val, locals);
                            emit_release_owned_call_temp(g, expr->index.index,
                                call_args[1], locals);
                            free(call_args);
                            return result;
                        }
                    }
                }
            }
        }
        /* arr[i] — array/list element access */
        {
            zan_type_t *sot = infer_expr_type(g, expr->index.object, locals);
            if (is_span_type(sot)) {
                zan_type_t *et = container_elem_type(sot);
                LLVMTypeRef elem_llvm = et ? map_type(g, et)
                    : LLVMInt32TypeInContext(g->ctx);
                LLVMValueRef span_val = emit_expr(g, expr->index.object, locals);
                LLVMValueRef base = LLVMBuildExtractValue(g->builder, span_val, 0, "spx.base");
                LLVMValueRef typed = LLVMBuildBitCast(g->builder, base,
                    LLVMPointerType(elem_llvm, 0), "spx.p");
                LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
                LLVMValueRef span_len = LLVMBuildExtractValue(g->builder, span_val,
                    1, "spx.len");
                emit_index_bounds_check(g, idx, span_len, expr->loc, "span");
                if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                    LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64)
                    idx = LLVMBuildSExt(g->builder, idx,
                        LLVMInt64TypeInContext(g->ctx), "spx.ix");
                idx = emit_index_safe_bounds(g, idx, span_len, expr->loc, "span");
                LLVMValueRef ep = LLVMBuildGEP2(g->builder, elem_llvm, typed, &idx, 1, "spx.ep");
                LLVMValueRef ld = LLVMBuildLoad2(g->builder, elem_llvm, ep, "spx.elem");
                LLVMSetAlignment(ld, 1);
                return promote_loaded(g, ld, et);
            }
        }
        LLVMValueRef arr_ptr = NULL;
        zan_type_t *arr_type = NULL;
        int is_list = 0;
        if (expr->index.object->kind == AST_IDENTIFIER) {
            local_var_t *local = local_find(locals, expr->index.object->ident.name);
            if (local) {
                arr_ptr = LLVMBuildLoad2(g->builder, LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                    local->alloca, "arrload");
                arr_type = local->type;
                /* detect List type by checking if type name is "List" */
                if (arr_type && type_named(arr_type, "List", 4)) {
                    is_list = 1;
                }
            } else if (g->current_type_sym) {
                /* implicit this.field[i] — bare identifier naming an array field */
                zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->index.object->ident.name);
                if (fsym) {
                    arr_type = fsym->type;
                    arr_ptr = emit_expr(g, expr->index.object, locals);
                    if (arr_type && type_named(arr_type, "List", 4)) {
                        is_list = 1;
                    }
                }
            }
        } else if (expr->index.object->kind == AST_MEMBER_ACCESS) {
            /* 字段数组索引访问：加载字段指针后进行数组寻址 */
            arr_type = member_access_field_type(g, locals, expr->index.object);
            if (!arr_type)
                arr_type = infer_expr_type(g, expr->index.object, locals);
            if (arr_type) {
                arr_ptr = emit_expr(g, expr->index.object, locals);
                if (type_named(arr_type, "List", 4)) {
                    is_list = 1;
                }
            }
        } else {
            /* 通用索引寻址：支持任意产生集合/数组的表达式 */
            arr_type = infer_expr_type(g, expr->index.object, locals);
            if (arr_type) {
                arr_ptr = emit_expr(g, expr->index.object, locals);
                if (type_named(arr_type, "List", 4)) {
                    is_list = 1;
                }
            }
        }
        /* List indexer: list[i] -> load data[i] from list struct */
        if (arr_ptr && is_list) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, arr_ptr,
                LLVMPointerType(g->list_struct_type, 0), "lptr");
            LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
            LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
            LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
            LLVMValueRef count_field = LLVMBuildStructGEP2(g->builder,
                g->list_struct_type, list_ptr, 0, "countf");
            LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_field,
                "count");
            emit_index_bounds_check(g, idx, count, expr->loc, "list");
            if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                idx = LLVMBuildSExt(g->builder, idx, i64, "ext");
            }
            idx = emit_index_safe_bounds(g, idx, count, expr->loc, "list");
            zan_type_t *et = container_elem_type(arr_type);
            LLVMValueRef widx = slot_word_index(g, idx, elem_slot_words(g, et));
            LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &widx, 1, "ep");
            LLVMTypeRef em = et ? map_type(g, et) : NULL;
            if (em && LLVMGetTypeKind(em) == LLVMStructTypeKind)
                return finish_index_of_temp(g, expr, locals, arr_type, et,
                    arr_ptr, load_struct_from_slot(g, elem_ptr, em));
            LLVMValueRef raw = LLVMBuildLoad2(g->builder, i64, elem_ptr, "elem");
            /* 集合物理 i64 槽与非整型元素间的重解释转换 */
            LLVMValueRef out = raw;
            if (em) {
                LLVMTypeKind mk = LLVMGetTypeKind(em);
                if (mk == LLVMPointerTypeKind)
                    out = LLVMBuildIntToPtr(g->builder, raw, em, "elp");
                else if (mk == LLVMDoubleTypeKind)
                    out = LLVMBuildBitCast(g->builder, raw, em, "elf");
                else if (mk == LLVMFloatTypeKind) {
                    /* 浮点槽位 32 位位模式在 64 位槽内的保持 */
                    LLVMValueRef narrow = LLVMBuildTrunc(g->builder, raw,
                        LLVMInt32TypeInContext(g->ctx), "elf32.t");
                    out = LLVMBuildBitCast(g->builder, narrow, em, "elf32.f");
                }
            }
            return finish_index_of_temp(g, expr, locals, arr_type, et,
                                        arr_ptr, out);
        }
        /* Dict 索引读取：哈希探测查找条目并提取值字段 */
        if (arr_ptr && arr_type && type_named(arr_type, "Dict", 4)) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef dp = LLVMBuildBitCast(g->builder, arr_ptr,
                LLVMPointerType(g->dict_struct_type, 0), "dp");
            LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
            LLVMValueRef vs = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp, "vs");
            LLVMValueRef search = coerce_dict_key(g, emit_expr(g, expr->index.index, locals), dict_key_type(g, arr_type));
            zan_type_t *dvt = dict_value_type(arr_type);
            LLVMTypeRef value_llvm = dvt ? map_type(g, dvt) : i64;
            LLVMValueRef res = emit_entry_alloca(g, value_llvm, "dres");
            zan_store_fit(g, LLVMConstNull(value_llvm), res);
            LLVMValueRef found = emit_dict_find(g, arr_type, arr_ptr, search);
            LLVMValueRef hit = zan_icmp(g->builder, LLVMIntSGE, found,
                LLVMConstInt(i64, 0, 0), "dihit");
            LLVMBasicBlockRef hit_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "di.hit");
            LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "di.done");
            LLVMBuildCondBr(g->builder, hit, hit_bb, done_bb);
            LLVMPositionBuilderAtEnd(g->builder, hit_bb);
            LLVMValueRef word = zan_mul(g->builder, found,
                load_dict_value_words(g, arr_ptr), "di.word");
            LLVMValueRef vslot = LLVMBuildGEP2(g->builder, i64, vs,
                &word, 1, "vsl");
            zan_store_fit(g, load_collection_slot_value(g, dvt, vslot), res);
            LLVMBuildBr(g->builder, done_bb);
            LLVMPositionBuilderAtEnd(g->builder, done_bb);
            /*
             * The index expression's rc belongs to this read when it is a
             * temporary (`d[P.MakeKey()]`); drop it before returning.
             */
            emit_release_owned_call_temp(g, expr->index.index, search, locals);
            LLVMValueRef dout = LLVMBuildLoad2(g->builder, value_llvm, res, "dval");
            return finish_index_of_temp(g, expr, locals, arr_type, dvt,
                                        arr_ptr, dout);
        }
        /* 字符串索引访问：读取单字节 (i8) 并零扩展为字符码 */
        if (arr_ptr && arr_type && arr_type->kind == TYPE_STRING) {
            LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
            if (expr_has_reliable_string_bounds(expr->index.object, locals)) {
                arr_ptr = emit_string_base_guard(g, arr_ptr, expr->loc);
                LLVMValueRef str_len = emit_string_buffer_len(g, arr_ptr, expr->loc);
                emit_index_bounds_check(g, idx, str_len, expr->loc, "string");
                idx = emit_index_safe_bounds(g, idx, str_len, expr->loc, "string");
            } else {
                /*
                 * No reliable bound (field/param/extern receiver): still keep
                 * null and negative indexes from faulting bare.
                 */
                idx = emit_string_elem_guard(g, arr_ptr, idx, expr->loc, &arr_ptr);
            }
            if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(LLVMTypeOf(idx)) != 64) {
                idx = LLVMBuildSExt(g->builder, idx,
                    LLVMInt64TypeInContext(g->ctx), "idxext");
            }
            LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
            LLVMValueRef ch_ptr = LLVMBuildGEP2(g->builder, i8t, arr_ptr, &idx, 1, "chp");
            LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8t, ch_ptr, "ch");
            LLVMValueRef chz = LLVMBuildZExt(g->builder, ch,
                LLVMInt64TypeInContext(g->ctx), "chz");
            return finish_index_of_temp(g, expr, locals, arr_type, NULL,
                                        arr_ptr, chz);
        }
        if (arr_ptr && arr_type) {
            LLVMTypeRef elem_llvm = LLVMInt64TypeInContext(g->ctx);
            if (arr_type->element_type) {
                elem_llvm = map_type(g, arr_type->element_type);
            }
            LLVMValueRef elem_ptr;
            if (arr_type->array_rank > 1) {
                /*
                 * rank-N rectangular read: per-dim bounds checks and
                 * row-major flattening inside the helper
                 */
                elem_ptr = emit_mdarray_elem_ptr(g, arr_ptr, expr,
                                                 elem_llvm, locals);
            } else {
                LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
                LLVMValueRef arr_len = zan_array_len(g, arr_ptr);
                emit_index_bounds_check(g, idx, arr_len, expr->loc, "array");
                idx = emit_index_safe_bounds(g, idx, arr_len, expr->loc, "array");
                LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                    LLVMPointerType(elem_llvm, 0), "arrp");
                elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
            }
            return promote_loaded(g,
                LLVMBuildLoad2(g->builder, elem_llvm, elem_ptr, "elem"),
                arr_type->element_type);
        }
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

/* LINQ 查询表达式完整降解实现 */

typedef struct {
    zan_istr_t seq_name;    /* hidden local holding the current sequence */
    LLVMValueRef seq_value; /* its value (kept in sync with the alloca) */
    /* LINQ 查询中间结果列表生命周期管理标记 */
    bool seq_owned;
    zan_type_t *elem_type;  /* element type of the current sequence */
    bool row_mode;          /* sequence elements are row tuples */
    zan_type_t *row_type;   /* the row tuple type (row_mode only) */
    int row_fields;         /* field count in the row */
    zan_istr_t *field_names;  /* per-field variable names (x, y1, ...) */
    zan_type_t **field_types; /* per-field types */
} query_seq_t;

typedef struct {
    LLVMValueRef col;    /* the list as a List* */
    LLVMValueRef data;   /* i64* element buffer */
    LLVMValueRef count;  /* i64 element count */
    LLVMValueRef idx;    /* i64* index alloca */
    LLVMBasicBlockRef cond, body, inc, end;
} query_loop_t;

static int q_synth_counter = 0;

static zan_istr_t query_fresh_name(zan_irgen_t *g, const char *prefix) {
    char buf[40];
    int n = (int)__atomic_fetch_add(&q_synth_counter, 1, __ATOMIC_SEQ_CST);
    snprintf(buf, sizeof buf, "__q%s%d", prefix, n);
    char *p = zan_arena_strdup(g->arena, buf, (size_t)strlen(buf));
    return (zan_istr_t){ p, (uint32_t)strlen(buf) };
}

static zan_ast_node_t *query_ident(zan_irgen_t *g, zan_istr_t name,
                                   zan_loc_t loc) {
    zan_ast_node_t *n = zan_ast_new(g->arena, AST_IDENTIFIER, loc);
    n->ident.name = name;
    return n;
}

/* empty List<elem> at LLVM level — mirrors what `new List<T>()` lowers to */
static LLVMValueRef query_new_list(zan_irgen_t *g, zan_ast_node_t *at,
                                   zan_type_t *elem) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef raw = emit_alloc_rc_collection(g, at, 24, 1, elem);
    LLVMValueRef lp = LLVMBuildBitCast(g->builder, raw,
        LLVMPointerType(g->list_struct_type, 0), "qlp");
    LLVMValueRef cnt = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
        lp, 0, "qlc");
    zan_store_fit(g, LLVMConstInt(i64, 0, 0), cnt);
    LLVMValueRef cap = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
        lp, 1, "qlcap");
    zan_store_fit(g, LLVMConstInt(i64, 8, 0), cap);
    LLVMValueRef data = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
        lp, 2, "qld");
    LLVMValueRef dsz = LLVMConstInt(i64,
        8 * 8 * (long long)elem_slot_words(g, elem), 0);
    LLVMValueRef dp = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
        get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), dsz },
        2, "qdata");
    zan_irgen_emit_oom_check(g, g->current_fn, dp);
    zan_store_fit(g, LLVMBuildBitCast(g->builder, dp,
        LLVMPointerType(i64, 0), "qdp"), data);
    return LLVMBuildBitCast(g->builder, lp, i8ptr, "qlv");
}

/* 注册保存指定值的隐藏局部变量并返回其 AST 符号 */
/* 结构体值传递：采用指针指向其连续内存存储 */
static bool query_struct_slot(zan_irgen_t *g, LLVMValueRef value,
                              zan_type_t *value_type) {
    if (!value_type) return false;
    LLVMTypeRef want = map_type(g, value_type);
    return want && LLVMGetTypeKind(want) == LLVMStructTypeKind &&
           LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMPointerTypeKind;
}

static zan_istr_t query_hold(zan_irgen_t *g, local_scope_t *locals,
                             const char *prefix, LLVMValueRef value,
                             zan_type_t *value_type) {
    zan_istr_t name = query_fresh_name(g, prefix);
    if (query_struct_slot(g, value, value_type)) {
        local_add(locals, name, value, value_type);
        return name;
    }
    LLVMValueRef alloc = emit_entry_alloca(g, LLVMTypeOf(value), "qh");
    zan_store_fit(g, value, alloc);
    local_add(locals, name, alloc, value_type);
    return name;
}

/*
 * register a named local (let vars, join vars, the range var) holding a
 * freshly emitted value
 */
static void query_declare(zan_irgen_t *g, local_scope_t *locals,
                          zan_istr_t name, LLVMValueRef value,
                          zan_type_t *value_type) {
    /* same struct-slot rule as query_hold */
    if (query_struct_slot(g, value, value_type)) {
        local_add(locals, name, value, value_type);
        return;
    }
    LLVMValueRef alloc = emit_entry_alloca(g, LLVMTypeOf(value), "qdecl");
    zan_store_fit(g, value, alloc);
    local_add(locals, name, alloc, value_type);
}

/* `listName.Add(item)` through the normal lowering */
static void query_emit_add(zan_irgen_t *g, zan_istr_t list_name,
                           zan_ast_node_t *item, zan_loc_t loc,
                           local_scope_t *locals) {
    zan_ast_node_t *madd = zan_ast_new(g->arena, AST_MEMBER_ACCESS, loc);
    madd->member.object = query_ident(g, list_name, loc);
    madd->member.name = (zan_istr_t){ "Add", 3 };
    madd->member.null_cond = 0;
    zan_ast_node_t *addcall = zan_ast_new(g->arena, AST_CALL, loc);
    addcall->call.callee = madd;
    zan_ast_list_init(&addcall->call.args);
    zan_ast_list_init(&addcall->call.type_args);
    zan_ast_list_push(&addcall->call.args, item, g->arena);
    emit_expr(g, addcall, locals);
}

/* `Enumerable.<fn>(args...)` static call (T/R inferred from the list args) */
static LLVMValueRef query_emit_enumerable(zan_irgen_t *g, const char *fn,
                                          int fn_len, zan_ast_node_t **args,
                                          int nargs, zan_loc_t loc,
                                          local_scope_t *locals) {
    zan_ast_node_t *m = zan_ast_new(g->arena, AST_MEMBER_ACCESS, loc);
    m->member.object = query_ident(g, (zan_istr_t){ "Enumerable", 10 }, loc);
    m->member.name = (zan_istr_t){ (char *)fn, (uint32_t)fn_len };
    m->member.null_cond = 0;
    zan_ast_node_t *call = zan_ast_new(g->arena, AST_CALL, loc);
    call->call.callee = m;
    zan_ast_list_init(&call->call.args);
    zan_ast_list_init(&call->call.type_args);
    for (int i = 0; i < nargs; i++)
        zan_ast_list_push(&call->call.args, args[i], g->arena);
    return emit_expr(g, call, locals);
}

/* `a == b` on two registered locals (int/string/double operands) */
static LLVMValueRef query_emit_eq(zan_irgen_t *g, zan_istr_t lname,
                                  zan_istr_t rname, zan_loc_t loc,
                                  local_scope_t *locals) {
    zan_ast_node_t *b = zan_ast_new(g->arena, AST_BINARY, loc);
    b->binary.op = TK_EQ_EQ;
    b->binary.left = query_ident(g, lname, loc);
    b->binary.right = query_ident(g, rname, loc);
    return emit_expr(g, b, locals);
}

/* open a for-loop over a list value */
static void query_loop_open(zan_irgen_t *g, query_loop_t *l,
                            LLVMValueRef list_val) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    l->col = LLVMBuildBitCast(g->builder, list_val,
        LLVMPointerType(g->list_struct_type, 0), "qlc");
    LLVMValueRef cp = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
        l->col, 0, "qlcp");
    l->count = LLVMBuildLoad2(g->builder, i64, cp, "qlcnt");
    LLVMValueRef dp = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
        l->col, 2, "qlcdp");
    l->data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), dp, "qldata");
    l->idx = emit_entry_alloca(g, i64, "qlidx");
    zan_store_fit(g, LLVMConstInt(i64, 0, 0), l->idx);
    l->cond = LLVMAppendBasicBlockInContext(g->ctx, fn, "ql.cond");
    l->body = LLVMAppendBasicBlockInContext(g->ctx, fn, "ql.body");
    l->inc = LLVMAppendBasicBlockInContext(g->ctx, fn, "ql.inc");
    l->end = LLVMAppendBasicBlockInContext(g->ctx, fn, "ql.end");
    LLVMBuildBr(g->builder, l->cond);
    LLVMPositionBuilderAtEnd(g->builder, l->cond);
    LLVMValueRef iv = LLVMBuildLoad2(g->builder, i64, l->idx, "qliv");
    LLVMValueRef cmp = zan_icmp(g->builder, LLVMIntSLT, iv, l->count, "qlcmp");
    LLVMBuildCondBr(g->builder, cmp, l->body, l->end);
    LLVMPositionBuilderAtEnd(g->builder, l->body);
}

/* load element `iv` of the loop into a fresh slot; returns the slot */
static LLVMValueRef query_loop_load(zan_irgen_t *g, query_loop_t *l,
                                    zan_type_t *elem, LLVMValueRef iv) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef elm = map_type(g, elem);
    LLVMValueRef qwidx = slot_word_index(g, iv, elem_slot_words(g, elem));
    LLVMValueRef ep = LLVMBuildGEP2(g->builder, i64, l->data, &qwidx, 1, "qlep");
    /* 集合元素加载梯级类型匹配转换 */
    LLVMValueRef ev = load_collection_slot_value(g, elem, ep);
    LLVMValueRef slot = emit_entry_alloca(g, elm, "qels");
    zan_store_fit(g, ev, slot);
    return slot;
}

/* close a loop; the current block must be the body end */
static void query_loop_close(zan_irgen_t *g, query_loop_t *l) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    /* 循环体尾部跳转至步进块 */
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
        LLVMBuildBr(g->builder, l->inc);
    LLVMPositionBuilderAtEnd(g->builder, l->inc);
    LLVMValueRef next = zan_add(g->builder,
        LLVMBuildLoad2(g->builder, i64, l->idx, "qli2"),
        LLVMConstInt(i64, 1, 0), "qlnext");
    zan_store_fit(g, next, l->idx);
    LLVMBuildBr(g->builder, l->cond);
    LLVMPositionBuilderAtEnd(g->builder, l->end);
}

/* 注册 LINQ 迭代范围变量 */
static void query_register_iter(zan_irgen_t *g, zan_ast_node_t *expr,
                                query_seq_t *q, LLVMValueRef slot,
                                local_scope_t *locals) {
    if (!q->row_mode) {
        local_add(locals, expr->query.var, slot, q->elem_type);
        return;
    }
    LLVMTypeRef row_llvm = map_type(g, q->row_type);
    for (int i = 0; i < q->row_fields; i++) {
        LLVMValueRef fp = emit_field_ptr(g, q->row_type->sym, row_llvm,
            slot, i, "qrf");
        LLVMValueRef fv = LLVMBuildLoad2(g->builder,
            map_type(g, q->field_types[i]), fp, "qrfv");
        LLVMValueRef fs = emit_entry_alloca(g, map_type(g, q->field_types[i]),
            "qrfs");
        zan_store_fit(g, fv, fs);
        local_add(locals, q->field_names[i], fs, q->field_types[i]);
    }
}

/* type-only registration of the iteration variables (for inference) */
static void query_scope_vars_for_infer(zan_irgen_t *g, zan_ast_node_t *expr,
                                       query_seq_t *q, local_scope_t *locals) {
    if (q->row_mode) {
        for (int i = 0; i < q->row_fields; i++)
            local_add(locals, q->field_names[i], NULL, q->field_types[i]);
    } else {
        local_add(locals, expr->query.var, NULL, q->elem_type);
    }
}

/* type-only registration of `let` variables with clause index < until */
static void query_scope_lets(zan_irgen_t *g, zan_ast_list_t *clauses,
                             int until, local_scope_t *locals) {
    for (int ci = 0; ci < until; ci++) {
        zan_ast_node_t *cl = clauses->items[ci];
        if (cl->kind == AST_QUERY_LET) {
            zan_type_t *lt = infer_expr_type(g, cl->query_clause.expr, locals);
            if (!lt) lt = g->binder->type_int;
            local_add(locals, cl->query_clause.name, NULL, lt);
        }
    }
}

/*
 * key type classification: 0=int, 1=long, 2=double, 3=string, -1=unsupported.
 * Group keys are restricted to int/string (Grouping carries IntKey/Key).
 */
static int query_key_kind(zan_irgen_t *g, zan_type_t *kt, bool group) {
    if (!kt) return 0;
    switch (kt->kind) {
    case TYPE_INT: case TYPE_UINT: case TYPE_SHORT: case TYPE_USHORT:
    case TYPE_BYTE: case TYPE_SBYTE: case TYPE_CHAR: case TYPE_BOOL:
    case TYPE_NINT:
        return 0;
    case TYPE_LONG: case TYPE_ULONG:
        return group ? -1 : 1;
    case TYPE_FLOAT: case TYPE_DOUBLE:
        return group ? -1 : 2;
    case TYPE_STRING:
        return 3;
    default:
        return -1;
    }
}

static const char *query_sort_fn(int kind, int desc) {
    switch (kind) {
    case 0: return desc ? "OrderByKeysIntDescending" : "OrderByKeysInt";
    case 1: return desc ? "OrderByKeysLongDescending" : "OrderByKeysLong";
    case 2: return desc ? "OrderByKeysNumDescending" : "OrderByKeysNum";
    default: return desc ? "OrderByKeysStrDescending" : "OrderByKeysStr";
    }
}

/*
 * one orderby key: extract List<K> of keys over the current sequence and
 * replace the sequence with the sorted one
 */
static void query_sort_by_key(zan_irgen_t *g, zan_ast_node_t *expr,
                              query_seq_t *q, zan_ast_node_t *key_expr,
                              int desc, int let_until, zan_ast_list_t *clauses,
                              local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int imark = locals->count;
    query_scope_vars_for_infer(g, expr, q, locals);
    query_scope_lets(g, clauses, let_until, locals);
    zan_type_t *kt = infer_expr_type(g, key_expr, locals);
    locals->count = imark;
    if (!kt) kt = g->binder->type_int;
    int kind = query_key_kind(g, kt, false);
    if (kind < 0) {
        zan_diag_emit(g->diag, DIAG_ERROR, key_expr->loc,
                      "orderby key must be int, long, double or string");
        kind = 0;
    }
    zan_type_t *K = kind == 0 ? g->binder->type_int
                  : kind == 1 ? g->binder->type_long
                  : kind == 2 ? g->binder->type_double
                              : g->binder->type_string;

    LLVMValueRef keys = query_new_list(g, expr, K);
    zan_istr_t keys_name = query_hold(g, locals, "k", keys,
        zan_binder_make_list_type(g->binder, K));

    /* extraction loop: x (+row fields), then lets, then the key */
    query_loop_t l;
    int lmark = locals->count;
    query_loop_open(g, &l, q->seq_value);
    LLVMValueRef iv = LLVMBuildLoad2(g->builder, i64, l.idx, "qkiv");
    LLVMValueRef slot = query_loop_load(g, &l,
        q->row_mode ? q->row_type : q->elem_type, iv);
    query_register_iter(g, expr, q, slot, locals);
    for (int ci = 0; ci < let_until; ci++) {
        zan_ast_node_t *cl = clauses->items[ci];
        if (cl->kind == AST_QUERY_LET) {
            zan_type_t *lt = infer_expr_type(g, cl->query_clause.expr, locals);
            if (!lt) lt = g->binder->type_int;
            query_declare(g, locals, cl->query_clause.name,
                          emit_expr(g, cl->query_clause.expr, locals), lt);
        }
    }
    LLVMValueRef kv = emit_expr(g, key_expr, locals);
    kv = emit_boundary_coerce(g, kv, map_type(g, K));
    zan_istr_t kv_name = query_hold(g, locals, "kv", kv, K);
    query_emit_add(g, keys_name, query_ident(g, kv_name, expr->loc),
                   expr->loc, locals);
    locals->count = lmark;
    query_loop_close(g, &l);

    /* sort: seq = Enumerable.OrderByKeys*<T>(seq, keys) */
    zan_ast_node_t *args[2] = { query_ident(g, q->seq_name, expr->loc),
                                query_ident(g, keys_name, expr->loc) };
    LLVMValueRef sorted = query_emit_enumerable(g, query_sort_fn(kind, desc),
        (int)strlen(query_sort_fn(kind, desc)), args, 2, expr->loc, locals);
    LLVMValueRef salloc = local_find(locals, q->seq_name)->alloca;
    zan_store_fit(g, sorted, salloc);
    if (q->seq_owned)
        emit_release_owned_call_temp(g, expr, q->seq_value, locals);
    emit_release_owned_call_temp(g, expr, keys, locals);
    q->seq_value = sorted;
    q->seq_owned = true;
}

/* build a row tuple value from field values */
static LLVMValueRef query_build_row(zan_irgen_t *g, zan_type_t *row_type,
                                    int n, LLVMValueRef *values,
                                    zan_ast_node_t *at) {
    LLVMTypeRef st = get_struct_llvm_type(g, row_type->sym);
    if (!st) st = map_type(g, row_type);
    LLVMValueRef alloca = emit_entry_alloca(g, st, "qrow");
    zan_store_fit(g, LLVMConstNull(st), alloca);
    for (int i = 0; i < n; i++) {
        LLVMValueRef fp = emit_field_ptr(g, row_type->sym, st, alloca, i,
            "qrowf");
        zan_store_fit(g, values[i], fp);
    }
    return alloca;
}

/*
 * one join clause: expand the current sequence into rows carrying the join
 * variable (or its match list for `join ... into g`)
 */
static void query_materialize_join(zan_irgen_t *g, zan_ast_node_t *expr,
                                   query_seq_t *q, zan_ast_node_t *jc,
                                   int jc_idx, zan_ast_list_t *clauses,
                                   local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int mark = locals->count;
    zan_loc_t loc = jc->loc;

    LLVMValueRef s_val = emit_expr(g, jc->query_clause.source, locals);
    zan_type_t *s_ty = infer_expr_type(g, jc->query_clause.source, locals);
    zan_type_t *y_ty = container_elem_type(s_ty);
    if (!y_ty) y_ty = g->binder->type_int;

    /* ensure the sequence is in row mode (rows of x so far) */
    if (!q->row_mode) {
        zan_type_t *row1 = zan_binder_make_tuple_type(g->binder,
            &q->elem_type, 1);
        LLVMValueRef rows1 = query_new_list(g, expr, row1);
        zan_istr_t rows1_name = query_hold(g, locals, "r", rows1,
            zan_binder_make_list_type(g->binder, row1));
        query_loop_t ol;
        int olmark = locals->count;
        query_loop_open(g, &ol, q->seq_value);
        LLVMValueRef oiv = LLVMBuildLoad2(g->builder, i64, ol.idx, "qmiv");
        LLVMValueRef xslot = query_loop_load(g, &ol, q->elem_type, oiv);
        local_add(locals, expr->query.var, xslot, q->elem_type);
        LLVMValueRef xv = LLVMBuildLoad2(g->builder, map_type(g, q->elem_type),
            xslot, "qmx");
        LLVMValueRef rv = query_build_row(g, row1, 1, &xv, expr);
        zan_istr_t rv_name = query_hold(g, locals, "rv", rv, row1);
        query_emit_add(g, rows1_name, query_ident(g, rv_name, loc), loc,
                       locals);
        locals->count = olmark;
        query_loop_close(g, &ol);
        local_var_t *slv = local_find(locals, q->seq_name);
        zan_store_fit(g, rows1, slv->alloca);
        /* 查询中间隐藏序列变量类型更新 */
        slv->type = zan_binder_make_list_type(g->binder, row1);
        if (q->seq_owned)
            emit_release_owned_call_temp(g, expr, q->seq_value, locals);
        q->seq_value = rows1;
        q->seq_owned = true;
        q->row_mode = true;
        q->row_type = row1;
        q->row_fields = 1;
        q->field_names = (zan_istr_t *)zan_arena_alloc(g->arena,
            sizeof(zan_istr_t));
        q->field_names[0] = expr->query.var;
        q->field_types = (zan_type_t **)zan_arena_alloc(g->arena,
            sizeof(zan_type_t *));
        q->field_types[0] = q->elem_type;
    }

    /* new row = old fields + join field (List<Y> for `into g`) */
    zan_istr_t jf_name = jc->query_clause.into.len > 0
        ? jc->query_clause.into : jc->query_clause.name;
    zan_type_t *jf_ty = jc->query_clause.into.len > 0
        ? zan_binder_make_list_type(g->binder, y_ty) : y_ty;
    int nf = q->row_fields + 1;
    zan_type_t **ft = (zan_type_t **)zan_arena_alloc(g->arena,
        sizeof(zan_type_t *) * (size_t)nf);
    zan_istr_t *fn = (zan_istr_t *)zan_arena_alloc(g->arena,
        sizeof(zan_istr_t) * (size_t)nf);
    for (int i = 0; i < q->row_fields; i++) {
        fn[i] = q->field_names[i];
        ft[i] = q->field_types[i];
    }
    fn[q->row_fields] = jf_name;
    ft[q->row_fields] = jf_ty;
    zan_type_t *new_row = zan_binder_make_tuple_type(g->binder, ft, nf);
    LLVMValueRef rows2 = query_new_list(g, expr, new_row);
    zan_istr_t rows2_name = query_hold(g, locals, "r", rows2,
        zan_binder_make_list_type(g->binder, new_row));

    query_loop_t ol;
    int olmark = locals->count;
    query_loop_open(g, &ol, q->seq_value);
    LLVMValueRef oiv = LLVMBuildLoad2(g->builder, i64, ol.idx, "qmiv2");
    LLVMValueRef row_alloc = query_loop_load(g, &ol, q->row_type, oiv);
    query_register_iter(g, expr, q, row_alloc, locals);
    for (int ci = 0; ci < jc_idx; ci++) {
        zan_ast_node_t *cl = clauses->items[ci];
        if (cl->kind == AST_QUERY_LET) {
            zan_type_t *lt = infer_expr_type(g, cl->query_clause.expr, locals);
            if (!lt) lt = g->binder->type_int;
            query_declare(g, locals, cl->query_clause.name,
                          emit_expr(g, cl->query_clause.expr, locals), lt);
        }
    }

    /* 连接查询 (join)：外层循环行左键提取计算 */
    LLVMValueRef lkv = emit_expr(g, jc->query_clause.left_key, locals);
    zan_type_t *lkt = infer_expr_type(g, jc->query_clause.left_key, locals);
    if (!lkt) lkt = g->binder->type_int;
    zan_istr_t lk_name = query_hold(g, locals, "lk", lkv, lkt);

    LLVMValueRef ml_val = NULL;
    zan_istr_t ml_name = { NULL, 0 };
    LLVMValueRef ml_alloc = NULL;
    if (jc->query_clause.into.len > 0) {
        /* collect the matching elements into a List<Y> first */
        LLVMValueRef ml = query_new_list(g, expr, y_ty);
        ml_name = query_hold(g, locals, "m", ml,
            zan_binder_make_list_type(g->binder, y_ty));
        ml_alloc = local_find(locals, ml_name)->alloca;
        ml_val = ml;
        query_loop_t il;
        int imark = locals->count;
        query_loop_open(g, &il, s_val);
        LLVMValueRef iiv = LLVMBuildLoad2(g->builder, i64, il.idx, "qmiv3");
        LLVMValueRef yslot = query_loop_load(g, &il, y_ty, iiv);
        local_add(locals, jc->query_clause.name, yslot, y_ty);
        LLVMValueRef rkv = emit_expr(g, jc->query_clause.right_key, locals);
        zan_type_t *rkt = infer_expr_type(g, jc->query_clause.right_key,
                                          locals);
        if (!rkt) rkt = y_ty;
        if (query_key_kind(g, lkt, false) != query_key_kind(g, rkt, false))
            zan_diag_emit(g->diag, DIAG_ERROR, loc,
                          "join key types must match (int, long, double or "
                          "string)");
        zan_istr_t rk_name = query_hold(g, locals, "rk", rkv, rkt);
        LLVMValueRef eq = query_emit_eq(g, lk_name, rk_name, loc, locals);
        LLVMBasicBlockRef keep_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "qm.keep");
        LLVMBasicBlockRef skip_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "qm.skip");
        LLVMBuildCondBr(g->builder, eq, keep_bb, skip_bb);
        LLVMPositionBuilderAtEnd(g->builder, keep_bb);
        query_emit_add(g, ml_name, query_ident(g, jc->query_clause.name, loc),
                       loc, locals);
        LLVMBuildBr(g->builder, il.inc);
        LLVMPositionBuilderAtEnd(g->builder, skip_bb);
        LLVMBuildBr(g->builder, il.inc);
        locals->count = imark;
        query_loop_close(g, &il);
    } else {
        /* plain join: one output row per match */
        query_loop_t il;
        int imark = locals->count;
        query_loop_open(g, &il, s_val);
        LLVMValueRef iiv = LLVMBuildLoad2(g->builder, i64, il.idx, "qmiv4");
        LLVMValueRef yslot = query_loop_load(g, &il, y_ty, iiv);
        local_add(locals, jc->query_clause.name, yslot, y_ty);
        LLVMValueRef rkv = emit_expr(g, jc->query_clause.right_key, locals);
        zan_type_t *rkt = infer_expr_type(g, jc->query_clause.right_key,
                                          locals);
        if (!rkt) rkt = y_ty;
        if (query_key_kind(g, lkt, false) != query_key_kind(g, rkt, false))
            zan_diag_emit(g->diag, DIAG_ERROR, loc,
                          "join key types must match (int, long, double or "
                          "string)");
        zan_istr_t rk_name = query_hold(g, locals, "rk", rkv, rkt);
        LLVMValueRef eq = query_emit_eq(g, lk_name, rk_name, loc, locals);
        LLVMBasicBlockRef keep_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "qm.keep");
        LLVMBasicBlockRef skip_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "qm.skip");
        LLVMBuildCondBr(g->builder, eq, keep_bb, skip_bb);
        LLVMPositionBuilderAtEnd(g->builder, keep_bb);
        /* row = old fields + y */
        LLVMValueRef *vals = (LLVMValueRef *)zan_arena_alloc(g->arena,
            sizeof(LLVMValueRef) * (size_t)nf);
        for (int i = 0; i < q->row_fields; i++) {
            LLVMValueRef fp = emit_field_ptr(g, q->row_type->sym,
                map_type(g, q->row_type), row_alloc, i, "qmfp");
            vals[i] = LLVMBuildLoad2(g->builder, map_type(g, q->field_types[i]),
                fp, "qmfl");
        }
        LLVMValueRef yv = LLVMBuildLoad2(g->builder, map_type(g, y_ty), yslot,
            "qmy");
        vals[q->row_fields] = yv;
        LLVMValueRef rv2 = query_build_row(g, new_row, nf, vals, expr);
        zan_istr_t rv2_name = query_hold(g, locals, "rv", rv2, new_row);
        query_emit_add(g, rows2_name, query_ident(g, rv2_name, loc), loc,
                       locals);
        LLVMBuildBr(g->builder, il.inc);
        LLVMPositionBuilderAtEnd(g->builder, skip_bb);
        LLVMBuildBr(g->builder, il.inc);
        locals->count = imark;
        query_loop_close(g, &il);
    }

    /* group join: append the match list as the last field */
    if (jc->query_clause.into.len > 0) {
        LLVMValueRef *vals = (LLVMValueRef *)zan_arena_alloc(g->arena,
            sizeof(LLVMValueRef) * (size_t)nf);
        for (int i = 0; i < q->row_fields; i++) {
            LLVMValueRef fp = emit_field_ptr(g, q->row_type->sym,
                map_type(g, q->row_type), row_alloc, i, "qmfp");
            vals[i] = LLVMBuildLoad2(g->builder, map_type(g, q->field_types[i]),
                fp, "qmfl");
        }
        LLVMValueRef mlv = LLVMBuildLoad2(g->builder,
            LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0), ml_alloc,
            "qmml");
        vals[q->row_fields] = mlv;
        LLVMValueRef rv2 = query_build_row(g, new_row, nf, vals, expr);
        zan_istr_t rv2_name = query_hold(g, locals, "rv", rv2, new_row);
        query_emit_add(g, rows2_name, query_ident(g, rv2_name, loc), loc,
                       locals);
        /* the match list is dead once it is in the row */
        emit_release_owned_call_temp(g, expr, ml_val, locals);
    }

    locals->count = olmark;
    query_loop_close(g, &ol);
    emit_release_owned_call_temp(g, jc->query_clause.source, s_val, locals);

    LLVMValueRef salloc = local_find(locals, q->seq_name)->alloca;
    zan_store_fit(g, rows2, salloc);
    /* 序列局部变量类型同步更新 */
    local_find(locals, q->seq_name)->type =
        zan_binder_make_list_type(g->binder, new_row);
    if (q->seq_owned)
        emit_release_owned_call_temp(g, expr, q->seq_value, locals);
    q->seq_value = rows2;
    q->seq_owned = true;
    q->row_type = new_row;
    q->row_fields = nf;
    q->field_names = fn;
    q->field_types = ft;
    locals->count = mark;
    (void)lkt;
}

/*
 * terminal group: extract (key, element) lists and call GroupByKeys*;
 * returns the List<Grouping<e>>
 */
static LLVMValueRef query_do_group(zan_irgen_t *g, zan_ast_node_t *expr,
                                   query_seq_t *q, zan_ast_list_t *clauses,
                                   local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int imark = locals->count;
    query_scope_vars_for_infer(g, expr, q, locals);
    query_scope_lets(g, clauses, clauses->count, locals);
    zan_type_t *kt = infer_expr_type(g, expr->query.group_key, locals);
    zan_type_t *ge = infer_expr_type(g, expr->query.group_expr, locals);
    locals->count = imark;
    if (!kt) kt = g->binder->type_int;
    if (!ge) ge = q->elem_type;
    int kind = query_key_kind(g, kt, true);
    if (kind < 0) {
        zan_diag_emit(g->diag, DIAG_ERROR, expr->query.group_key->loc,
                      "group key must be int or string");
        kind = 0;
    }
    zan_type_t *K = kind == 0 ? g->binder->type_int : g->binder->type_string;

    LLVMValueRef keys = query_new_list(g, expr, K);
    zan_istr_t keys_name = query_hold(g, locals, "gk", keys,
        zan_binder_make_list_type(g->binder, K));
    LLVMValueRef items = query_new_list(g, expr, ge);
    zan_istr_t items_name = query_hold(g, locals, "gi", items,
        zan_binder_make_list_type(g->binder, ge));

    query_loop_t l;
    int lmark = locals->count;
    query_loop_open(g, &l, q->seq_value);
    LLVMValueRef iv = LLVMBuildLoad2(g->builder, i64, l.idx, "qgiv");
    LLVMValueRef slot = query_loop_load(g, &l,
        q->row_mode ? q->row_type : q->elem_type, iv);
    query_register_iter(g, expr, q, slot, locals);
    for (int ci = 0; ci < clauses->count; ci++) {
        zan_ast_node_t *cl = clauses->items[ci];
        if (cl->kind == AST_QUERY_LET) {
            zan_type_t *lt = infer_expr_type(g, cl->query_clause.expr, locals);
            if (!lt) lt = g->binder->type_int;
            query_declare(g, locals, cl->query_clause.name,
                          emit_expr(g, cl->query_clause.expr, locals), lt);
        }
    }
    LLVMValueRef kkv = emit_expr(g, expr->query.group_key, locals);
    kkv = emit_boundary_coerce(g, kkv, map_type(g, K));
    zan_istr_t kkv_name = query_hold(g, locals, "gkv", kkv, K);
    query_emit_add(g, keys_name, query_ident(g, kkv_name, expr->loc),
                   expr->loc, locals);
    LLVMValueRef eev = emit_expr(g, expr->query.group_expr, locals);
    zan_istr_t eev_name = query_hold(g, locals, "gev", eev, ge);
    query_emit_add(g, items_name, query_ident(g, eev_name, expr->loc),
                   expr->loc, locals);
    locals->count = lmark;
    query_loop_close(g, &l);

    const char *fn = kind == 0 ? "GroupByKeysInt" : "GroupByKeysStr";
    zan_ast_node_t *args[3] = { query_ident(g, q->seq_name, expr->loc),
                                query_ident(g, keys_name, expr->loc),
                                query_ident(g, items_name, expr->loc) };
    LLVMValueRef groups = query_emit_enumerable(g, fn, (int)strlen(fn),
        args, 3, expr->loc, locals);
    emit_release_owned_call_temp(g, expr, keys, locals);
    emit_release_owned_call_temp(g, expr, items, locals);
    return groups;
}

/* LINQ 最终迭代阶段：按源码顺序处理 where/let，嵌套循环处理延迟查询 */
static void query_final_pass(zan_irgen_t *g, zan_ast_node_t *expr,
                             zan_ast_list_t *clauses, int *join_in_rows,
                             int from_idx, zan_istr_t result_name,
                             zan_ast_node_t *select,
                             LLVMBasicBlockRef *skip_stack, int *skip_depth,
                             local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    for (int ci = from_idx; ci < clauses->count; ci++) {
        zan_ast_node_t *cl = clauses->items[ci];
        if (cl->kind == AST_QUERY_WHERE) {
            LLVMValueRef c = emit_expr(g, cl->query_clause.expr, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(c)) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(LLVMTypeOf(c)) != 1)
                c = zan_icmp(g->builder, LLVMIntNE, c,
                             LLVMConstNull(LLVMTypeOf(c)), "qw");
            LLVMBasicBlockRef pass_bb = LLVMAppendBasicBlockInContext(g->ctx,
                LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)),
                "q.pass");
            {
                int rd = *skip_depth - 1;
                if (rd >= 16) rd = 15;   /* keep the read inside skip_stack */
                LLVMBuildCondBr(g->builder, c, pass_bb, skip_stack[rd]);
            }
            LLVMPositionBuilderAtEnd(g->builder, pass_bb);
        } else if (cl->kind == AST_QUERY_LET) {
            zan_type_t *lt = infer_expr_type(g, cl->query_clause.expr, locals);
            if (!lt) lt = g->binder->type_int;
            query_declare(g, locals, cl->query_clause.name,
                          emit_expr(g, cl->query_clause.expr, locals), lt);
        } else if (cl->kind == AST_QUERY_ORDERBY) {
            continue; /* already applied as a pre-sort */
        } else if (join_in_rows[ci]) {
            continue; /* materialized: the variable is in the rows */
        } else {
            /* deferred join: a nested loop over the join source */
            LLVMValueRef s_val = emit_expr(g, cl->query_clause.source, locals);
            zan_type_t *s_ty = infer_expr_type(g, cl->query_clause.source,
                                               locals);
            zan_type_t *y_ty = container_elem_type(s_ty);
            if (!y_ty) y_ty = g->binder->type_int;
            /* left key once per outer element */
            LLVMValueRef lkv = emit_expr(g, cl->query_clause.left_key, locals);
            zan_type_t *lkt = infer_expr_type(g, cl->query_clause.left_key,
                                              locals);
            if (!lkt) lkt = g->binder->type_int;
            zan_istr_t lk_name = query_hold(g, locals, "lk", lkv, lkt);
            /* LINQ into 语法处理 */
            if (cl->query_clause.into.len > 0) {
                /*
                 * join ... into g: collect the matches, then continue in the
                 * outer scope with g bound to the match list
                 */
                LLVMValueRef ml = query_new_list(g, expr, y_ty);
                zan_istr_t ml_name = query_hold(g, locals, "m", ml,
                    zan_binder_make_list_type(g->binder, y_ty));
                LLVMValueRef ml_alloc = local_find(locals, ml_name)->alloca;
                int imark = locals->count;
                query_loop_t il2;
                query_loop_open(g, &il2, s_val);
                LLVMValueRef iiv2 = LLVMBuildLoad2(g->builder, i64, il2.idx,
                    "qjiv2");
                LLVMValueRef yslot2 = query_loop_load(g, &il2, y_ty, iiv2);
                local_add(locals, cl->query_clause.name, yslot2, y_ty);
                LLVMValueRef rkv2 = emit_expr(g, cl->query_clause.right_key,
                                              locals);
                zan_type_t *rkt2 = infer_expr_type(g,
                    cl->query_clause.right_key, locals);
                if (!rkt2) rkt2 = y_ty;
                zan_istr_t rk2_name = query_hold(g, locals, "rk", rkv2, rkt2);
                LLVMValueRef eq2 = query_emit_eq(g, lk_name, rk2_name,
                    cl->loc, locals);
                LLVMBasicBlockRef k2 = LLVMAppendBasicBlockInContext(g->ctx,
                    LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)),
                    "qj.k2");
                LLVMBasicBlockRef s2 = LLVMAppendBasicBlockInContext(g->ctx,
                    LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)),
                    "qj.s2");
                LLVMBuildCondBr(g->builder, eq2, k2, s2);
                LLVMPositionBuilderAtEnd(g->builder, k2);
                query_emit_add(g, ml_name,
                    query_ident(g, cl->query_clause.name, cl->loc), cl->loc,
                    locals);
                LLVMBuildBr(g->builder, il2.inc);
                LLVMPositionBuilderAtEnd(g->builder, s2);
                LLVMBuildBr(g->builder, il2.inc);
                locals->count = imark;
                query_loop_close(g, &il2);
                emit_release_owned_call_temp(g, cl->query_clause.source,
                                             s_val, locals);
                /* bind g, continue with the rest of the clauses */
                LLVMValueRef mlv = LLVMBuildLoad2(g->builder,
                    LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                    ml_alloc, "qgv");
                query_declare(g, locals, cl->query_clause.into, mlv,
                    zan_binder_make_list_type(g->binder, y_ty));
                query_final_pass(g, expr, clauses, join_in_rows, ci + 1,
                                 result_name, select, skip_stack, skip_depth,
                                 locals);
                emit_release_owned_call_temp(g, expr, ml, locals);
                return;
            }
            /*
             * plain join: one nested loop over the join source, and the rest
             * of the clauses run inside each match.
             */
            query_loop_t il;
            int lmark = locals->count;
            query_loop_open(g, &il, s_val);
            LLVMValueRef iiv = LLVMBuildLoad2(g->builder, i64, il.idx, "qjiv");
            LLVMValueRef yslot = query_loop_load(g, &il, y_ty, iiv);
            local_add(locals, cl->query_clause.name, yslot, y_ty);
            LLVMValueRef rkv = emit_expr(g, cl->query_clause.right_key,
                                         locals);
            zan_type_t *rkt = infer_expr_type(g, cl->query_clause.right_key,
                                              locals);
            if (!rkt) rkt = y_ty;
            if (query_key_kind(g, lkt, false) != query_key_kind(g, rkt, false))
                zan_diag_emit(g->diag, DIAG_ERROR, cl->loc,
                              "join key types must match (int, long, double "
                              "or string)");
            zan_istr_t rk_name = query_hold(g, locals, "rk", rkv, rkt);
            LLVMValueRef eq = query_emit_eq(g, lk_name, rk_name, cl->loc,
                                            locals);
            LLVMBasicBlockRef keep_bb = LLVMAppendBasicBlockInContext(g->ctx,
                LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)),
                "qj.keep");
            LLVMBasicBlockRef skip_bb = LLVMAppendBasicBlockInContext(g->ctx,
                LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)),
                "qj.skip");
            LLVMBuildCondBr(g->builder, eq, keep_bb, skip_bb);
            LLVMPositionBuilderAtEnd(g->builder, keep_bb);
            *skip_depth = *skip_depth + 1;
            {
                int sd = *skip_depth - 1;
                if (sd >= 16) {
                    zan_diag_emit(g->diag, DIAG_ERROR, cl->loc,
                                  "query expression has too many join "
                                  "clauses (max 15)");
                    sd = 15;
                }
                skip_stack[sd] = il.inc;
            }
            query_final_pass(g, expr, clauses, join_in_rows, ci + 1,
                             result_name, select, skip_stack, skip_depth,
                             locals);
            *skip_depth = *skip_depth - 1;
            LLVMBuildBr(g->builder, il.inc);
            LLVMPositionBuilderAtEnd(g->builder, skip_bb);
            LLVMBuildBr(g->builder, il.inc);
            locals->count = lmark;
            query_loop_close(g, &il);
            emit_release_owned_call_temp(g, cl->query_clause.source, s_val,
                                         locals);
            return;
        }
    }
    /* end of clauses: the projection */
    query_emit_add(g, result_name, select, expr->loc, locals);
}

/* `group e by k into g select p`: iterate the groupings with only g bound */
static void query_group_into_pass(zan_irgen_t *g, zan_ast_node_t *expr,
                                  LLVMValueRef groups, zan_type_t *grp_ty,
                                  zan_istr_t gvar, zan_ast_node_t *select,
                                  zan_istr_t result_name,
                                  local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    query_loop_t l;
    int lmark = locals->count;
    query_loop_open(g, &l, groups);
    LLVMValueRef iv = LLVMBuildLoad2(g->builder, i64, l.idx, "qgiv2");
    LLVMValueRef gslot = query_loop_load(g, &l, grp_ty, iv);
    local_add(locals, gvar, gslot, grp_ty);
    query_emit_add(g, result_name, select, expr->loc, locals);
    locals->count = lmark;
    query_loop_close(g, &l);
}

static LLVMValueRef emit_expr_query_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    int mark = locals->count;

    LLVMValueRef collection = emit_expr(g, expr->query.source, locals);
    zan_type_t *src_ty = infer_expr_type(g, expr->query.source, locals);
    zan_type_t *elem = container_elem_type(src_ty);
    if (!elem) elem = g->binder->type_int;

    query_seq_t q;
    memset(&q, 0, sizeof q);
    q.seq_value = collection;
    q.elem_type = elem;
    q.seq_name = query_hold(g, locals, "s", collection,
        zan_binder_make_list_type(g->binder, elem));

    zan_ast_list_t *clauses = &expr->query.clauses;
    int ncl = clauses->count;
    int *join_in_rows = ncl > 0
        ? (int *)zan_arena_alloc(g->arena, sizeof(int) * (size_t)ncl) : NULL;
    if (join_in_rows) memset(join_in_rows, 0, sizeof(int) * (size_t)ncl);

    /* infer the select/group type with the clause variables in scope */
    int smark = locals->count;
    local_add(locals, expr->query.var, NULL, elem);
    for (int ci = 0; ci < ncl; ci++) {
        zan_ast_node_t *cl = clauses->items[ci];
        if (cl->kind == AST_QUERY_LET) {
            zan_type_t *lt = infer_expr_type(g, cl->query_clause.expr, locals);
            if (!lt) lt = g->binder->type_int;
            local_add(locals, cl->query_clause.name, NULL, lt);
        } else if (cl->kind == AST_QUERY_JOIN) {
            zan_type_t *jty = infer_expr_type(g, cl->query_clause.source,
                                              locals);
            zan_type_t *je = container_elem_type(jty);
            if (!je) je = g->binder->type_int;
            if (cl->query_clause.into.len > 0)
                local_add(locals, cl->query_clause.into, NULL,
                          zan_binder_make_list_type(g->binder, je));
            else
                local_add(locals, cl->query_clause.name, NULL, je);
        }
    }
    zan_type_t *sel = NULL;
    zan_type_t *grp_ty = NULL;
    if (expr->query.group_expr) {
        zan_type_t *ge = infer_expr_type(g, expr->query.group_expr, locals);
        if (!ge) ge = elem;
        grp_ty = zan_binder_make_grouping_type(g->binder, ge);
        if (expr->query.group_into.len > 0) {
            local_add(locals, expr->query.group_into, NULL, grp_ty);
            sel = infer_expr_type(g, expr->query.select, locals);
            if (!sel) sel = ge;
        } else {
            sel = grp_ty;
        }
    } else {
        sel = infer_expr_type(g, expr->query.select, locals);
        if (!sel) sel = elem;
    }
    locals->count = smark;

    LLVMValueRef result = query_new_list(g, expr, sel);
    zan_istr_t result_name = query_hold(g, locals, "q", result,
        zan_binder_make_list_type(g->binder, sel));

    /* pre-sorts and join materializations, in clause order */
    int first_pending_join = -1;
    for (int ci = 0; ci < ncl; ci++) {
        zan_ast_node_t *cl = clauses->items[ci];
        if (cl->kind == AST_QUERY_JOIN) {
            if (first_pending_join < 0) first_pending_join = ci;
            continue;
        }
        if (cl->kind != AST_QUERY_ORDERBY) continue;
        /*
         * any pending joins must be rows before the sort (their variables
         * may appear in the key)
         */
        if (first_pending_join >= 0) {
            for (int ji = first_pending_join; ji < ci; ji++) {
                zan_ast_node_t *jc = clauses->items[ji];
                if (jc->kind == AST_QUERY_JOIN) {
                    query_materialize_join(g, expr, &q, jc, ji, clauses,
                                           locals);
                    join_in_rows[ji] = 1;
                }
            }
            first_pending_join = -1;
        }
        /*
         * an orderby clause is one or more adjacent keys; sort least
         * significant first (C# OrderBy(k1, k2))
         */
        int run_end = ci;
        while (run_end < ncl &&
               clauses->items[run_end]->kind == AST_QUERY_ORDERBY)
            run_end++;
        for (int ki = run_end - 1; ki >= ci; ki--) {
            zan_ast_node_t *ob = clauses->items[ki];
            query_sort_by_key(g, expr, &q, ob->query_clause.expr,
                              ob->query_clause.descending, ci, clauses,
                              locals);
        }
        ci = run_end - 1;
    }

    if (expr->query.group_expr) {
        if (first_pending_join >= 0) {
            for (int ji = first_pending_join; ji < ncl; ji++) {
                zan_ast_node_t *jc = clauses->items[ji];
                if (jc->kind == AST_QUERY_JOIN) {
                    query_materialize_join(g, expr, &q, jc, ji, clauses,
                                           locals);
                    join_in_rows[ji] = 1;
                }
            }
        }
        LLVMValueRef groups = query_do_group(g, expr, &q, clauses, locals);
        if (expr->query.group_into.len == 0) {
            /* `group e by k` (terminal): the grouping list IS the result */
            emit_release_owned_call_temp(g, expr, result, locals);
            if (q.seq_value == collection)
                emit_release_owned_call_temp(g, expr->query.source,
                                             collection, locals);
            else
                emit_release_owned_call_temp(g, expr, q.seq_value, locals);
            locals->count = mark;
            return groups;
        }
        /* `group e by k into g select p` */
        query_group_into_pass(g, expr, groups, grp_ty,
                              expr->query.group_into, expr->query.select,
                              result_name, locals);
        emit_release_owned_call_temp(g, expr, groups, locals);
        if (q.seq_value == collection)
            emit_release_owned_call_temp(g, expr->query.source, collection,
                                         locals);
        else
            emit_release_owned_call_temp(g, expr, q.seq_value, locals);
        locals->count = mark;
        return result;
    }

    /* final pass: iterate the sequence, apply where/let, project */
    query_loop_t ol;
    int lmark = locals->count;
    query_loop_open(g, &ol, q.seq_value);
    LLVMValueRef oiv = LLVMBuildLoad2(g->builder, i64, ol.idx, "qoiv");
    LLVMValueRef islot = query_loop_load(g, &ol,
        q.row_mode ? q.row_type : q.elem_type, oiv);
    query_register_iter(g, expr, &q, islot, locals);
    LLVMBasicBlockRef skip_stack[16];
    int skip_depth = 0;
    skip_stack[skip_depth++] = ol.inc;
    query_final_pass(g, expr, clauses, join_in_rows, 0, result_name,
                     expr->query.select, skip_stack, &skip_depth, locals);
    locals->count = lmark;
    query_loop_close(g, &ol);

    if (q.seq_value == collection)
        emit_release_owned_call_temp(g, expr->query.source, collection,
                                     locals);
    else
        emit_release_owned_call_temp(g, expr, q.seq_value, locals);
    locals->count = mark;
    (void)fn;
    return result;
}

/* 元组字面量 (a, b, ...)：分配并填充合成的匿名元组结构体值 */
static LLVMValueRef emit_expr_tuple(zan_irgen_t *g, zan_ast_node_t *expr,
                                    local_scope_t *locals) {
    zan_type_t *ttype = infer_expr_type(g, expr, locals);
    if (!ttype || ttype->kind != TYPE_STRUCT || !ttype->sym) {
        zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                      "cannot form a tuple here");
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
    }
    LLVMTypeRef st = get_struct_llvm_type(g, ttype->sym);
    if (!st) st = map_type(g, ttype);
    LLVMValueRef alloca = emit_entry_alloca(g, st, "tup");
    zan_store_fit(g, LLVMConstNull(st), alloca);
    int n = expr->tuple_expr.items.count;
    for (int i = 0; i < n; i++) {
        zan_ast_node_t *item = expr->tuple_expr.items.items[i];
        char fname[16];
        snprintf(fname, sizeof fname, "Item%d", i + 1);
        zan_istr_t f_istr = { fname, (uint32_t)strlen(fname) };
        zan_symbol_t *fsym = ttype->sym ? get_field_sym(ttype->sym, f_istr) : NULL;
        if (!fsym) {
            zan_diag_emit(g->diag, DIAG_ERROR, item->loc,
                          "tuple element %d has no field in its struct type",
                          i + 1);
            continue;
        }
        int fi = get_field_index(ttype->sym, f_istr);
        if (fi < 0) fi = i;
        LLVMValueRef fptr = emit_field_ptr(g, ttype->sym, st, alloca, fi, "tf");
        LLVMValueRef fval = emit_arg_typed(g, item, fsym->type, locals);
        /* 规范化结构体所有权契约，维护内部字段引用计数 */
        if (fsym->type && is_rc_managed_type(fsym->type) &&
            LLVMGetTypeKind(LLVMTypeOf(fval)) == LLVMPointerTypeKind &&
            !expr_yields_owned_rc_value(g, item, locals)) {
            emit_rc_retain_for_type(g, fsym->type, fval);
        } else if (fsym->type && fsym->type->kind == TYPE_STRUCT &&
                   type_contains_collection_rc(g, fsym->type, 0) &&
                   !expr_yields_owned_rc_value(g, item, locals)) {
            emit_collection_value_retain(g, fsym->type, fval, 0);
        }
        zan_store_fit(g, fval, fptr);
    }
    return alloca;
}

static zan_ast_node_t *owned_rhs_marker(zan_irgen_t *g, zan_loc_t loc);

static LLVMValueRef emit_expr_new_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* 工厂调用接对象初始化式 */
        if (!expr->new_expr.type && expr->new_expr.call_init) {
            LLVMValueRef obj = emit_expr(g, expr->new_expr.call_init, locals);
            zan_type_t *otype = infer_expr_type(g, expr->new_expr.call_init,
                                                locals);
            if (!otype || !otype->sym ||
                (otype->sym->kind != SYM_CLASS &&
                 otype->sym->kind != SYM_STRUCT)) {
                zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                    "object initializer can only continue a call that "
                    "returns a class instance");
                return obj;
            }
            zan_symbol_t *sym = otype->sym;
            LLVMTypeRef st = get_struct_llvm_type(g, sym);
            if (!st) return obj;
            for (int i = 0; i < expr->new_expr.arg_inits.count; i++) {
                zan_ast_node_t *arg = expr->new_expr.arg_inits.items[i];
                if (arg->kind != AST_COLL_INIT && arg->kind != AST_ASSIGNMENT)
                    continue;
                zan_istr_t mname = (arg->kind == AST_COLL_INIT)
                    ? arg->coll_init.name
                    : arg->binary.left->ident.name;
                zan_symbol_t *msym = get_field_sym(sym, mname);
                if (!msym) continue;
                if (arg->kind == AST_COLL_INIT) {
                    int getter_owned = 0;
                    zan_ast_node_t *recv = zan_ast_new(g->arena,
                        AST_IDENTIFIER, arg->loc);
                    recv->ident.name = mname;
                    LLVMTypeRef coll_lt = map_type(g, msym->type);
                    LLVMValueRef cslot = emit_entry_alloca(g, coll_lt,
                                                           "cinit.slot");
                    zan_store_fit(g, LLVMConstNull(coll_lt), cslot);
                    zan_symbol_t *getter = property_getter_sym(g, msym);
                    if (getter) {
                        LLVMValueRef v = emit_property_getter_call(g, getter,
                            otype, obj, arg, locals);
                        zan_store_fit(g, v, cslot);
                        getter_owned = 1;
                    } else {
                        int cfi = get_field_index(sym, mname);
                        if (cfi < 0) continue;
                        LLVMValueRef cptr = emit_field_ptr(g, sym, st, obj,
                                                           cfi, "cinit.ptr");
                        zan_store_fit(g, LLVMBuildLoad2(g->builder, coll_lt,
                            cptr, "cinit.load"), cslot);
                    }
                    local_add(locals, mname, cslot, msym->type);
                    for (int k = 0; k < arg->coll_init.items.count; k++) {
                        zan_ast_node_t *item = arg->coll_init.items.items[k];
                        zan_ast_node_t *madd = zan_ast_new(g->arena,
                            AST_MEMBER_ACCESS, item->loc);
                        madd->member.object = recv;
                        madd->member.name = (zan_istr_t){"Add", 3};
                        madd->member.null_cond = 0;
                        zan_ast_node_t *addcall = zan_ast_new(g->arena,
                            AST_CALL, item->loc);
                        addcall->call.callee = madd;
                        zan_ast_list_init(&addcall->call.args);
                        zan_ast_list_init(&addcall->call.type_args);
                        zan_ast_list_push(&addcall->call.args, item, g->arena);
                        if (msym->type && type_named(msym->type, "Dict", 4) &&
                            k + 1 < arg->coll_init.items.count) {
                            zan_ast_list_push(&addcall->call.args,
                                arg->coll_init.items.items[++k], g->arena);
                        }
                        /* 支持流式 API 的 Add 方法处理 */
                        LLVMValueRef addres = emit_expr(g, addcall, locals);
                        if (addres && expr_yields_owned_rc_value(g, addcall,
                                                      locals)) {
                            zan_type_t *rt = infer_expr_type(g, addcall,
                                                             locals);
                            if (rt && is_rc_managed_type(rt) &&
                                LLVMGetTypeKind(LLVMTypeOf(addres))
                                    == LLVMPointerTypeKind) {
                                emit_rc_release_for_type(g, rt, addres);
                            }
                        }
                    }
                    for (int lv = locals->count - 1; lv >= 0; lv--) {
                        if (locals->vars[lv].name.len == mname.len &&
                            memcmp(locals->vars[lv].name.str, mname.str,
                                   (size_t)mname.len) == 0) {
                            for (int sh = lv; sh < locals->count - 1; sh++)
                                locals->vars[sh] = locals->vars[sh + 1];
                            locals->count--;
                            break;
                        }
                    }
                    if (getter_owned) {
                        LLVMValueRef cv = LLVMBuildLoad2(g->builder, coll_lt,
                            cslot, "cinit.done");
                        emit_rc_release_for_type(g, msym->type, cv);
                    }
                    continue;
                }
                /* 普通字段/属性写入：通过合成访问器 */
                zan_ast_node_t *mref = zan_ast_new(g->arena,
                    AST_MEMBER_ACCESS, arg->loc);
                mref->member.object = expr->new_expr.call_init;
                mref->member.name = mname;
                mref->member.null_cond = 0;
                zan_ast_node_t *setn = zan_ast_new(g->arena,
                    AST_ASSIGNMENT, arg->loc);
                setn->binary.op = TK_EQ;
                setn->binary.left = mref;
                setn->binary.right = arg->binary.right;
                emit_expr(g, setn, locals);
            }
            return obj;
        }
        /* 匿名对象字面量 new { ... } 校验与降解 */
        if (!expr->new_expr.type) {
            zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                "anonymous object `new { ... }` is only valid inside a typed "
                "ORM query (GroupBy/ToList/ToAggregate)");
            return LLVMConstNull(LLVMInt8TypeInContext(g->ctx));
        }
        /* new Span<T>(nint base, int length) -- non-owning raw-memory view */
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF &&
            !expr->new_expr.is_array) {
            zan_istr_t spn = expr->new_expr.type->type_ref.name;
            if (spn.len == 4 && memcmp(spn.str, "Span", 4) == 0 &&
                expr->new_expr.args.count == 2) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef base = emit_expr(g, expr->new_expr.args.items[0], locals);
                if (LLVMGetTypeKind(LLVMTypeOf(base)) == LLVMIntegerTypeKind)
                    base = LLVMBuildIntToPtr(g->builder, base, i8ptr, "span.b2p");
                else
                    base = LLVMBuildBitCast(g->builder, base, i8ptr, "span.bbc");
                LLVMValueRef len = coerce_int_to(g,
                    emit_expr(g, expr->new_expr.args.items[1], locals), i64t);
                LLVMValueRef negative_len = zan_icmp(g->builder, LLVMIntSLT, len,
                    LLVMConstInt(i64t, 0, 0), "span.len.neg");
                emit_runtime_check(g, negative_len, expr->new_expr.args.items[1]->loc,
                                   "negative span length");
                LLVMValueRef v = LLVMGetUndef(g->span_struct_type);
                v = LLVMBuildInsertValue(g->builder, v, base, 0, "span.n0");
                v = LLVMBuildInsertValue(g->builder, v, len, 1, "span.n1");
                return v;
            }
        }
        /* new List<T>() 内建动态列表实例分配 */
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF &&
            !expr->new_expr.is_array) {
            zan_istr_t tname = expr->new_expr.type->type_ref.name;
            if (tname.len == 4 && memcmp(tname.str, "List", 4) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                /*
                 * allocate List struct on heap with an rc header (24 bytes:
                 * 3 * i64), so ARC frees it and its data buffer on release.
                 */
                zan_type_t *lelem = NULL;
                {
                    zan_type_t *lt = resolve_type_ctx(g, expr->new_expr.type);
                    if (lt) lelem = container_elem_type(lt);
                }
                LLVMValueRef list_ptr = emit_alloc_rc_collection(g, expr, 24, 1, lelem);
                /* cast to List* */
                LLVMValueRef typed_ptr = LLVMBuildBitCast(g->builder, list_ptr,
                    LLVMPointerType(g->list_struct_type, 0), "lptr");
                /* new List<T>(src) 拷贝构造函数降解 */
                if (expr->new_expr.list_copy && expr->new_expr.args.count == 1) {
                    LLVMValueRef src_raw =
                        emit_expr(g, expr->new_expr.args.items[0], locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(src_raw)) != LLVMPointerTypeKind) {
                        src_raw = LLVMBuildIntToPtr(g->builder, src_raw, i8ptr,
                                                    "cpy.src.ip");
                    }
                    LLVMValueRef src_ptr = LLVMBuildBitCast(g->builder, src_raw,
                        LLVMPointerType(g->list_struct_type, 0), "cpy.src");
                    LLVMValueRef src_cnt = LLVMBuildLoad2(g->builder, i64,
                        LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                            src_ptr, 0, "src.cnt"), "src.n");
                    LLVMValueRef src_data = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(i64, 0),
                        LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                            src_ptr, 2, "src.df"), "src.d");

                    LLVMValueRef eight = LLVMConstInt(i64, 8, 0);
                    LLVMValueRef cap = LLVMBuildSelect(g->builder,
                        zan_icmp(g->builder, LLVMIntSGT, src_cnt, eight, "cpy.gt8"),
                        src_cnt, eight, "cpy.cap");
                    zan_store_fit(g, src_cnt, LLVMBuildStructGEP2(g->builder,
                        g->list_struct_type, typed_ptr, 0, "cnt"));
                    zan_store_fit(g, cap, LLVMBuildStructGEP2(g->builder,
                        g->list_struct_type, typed_ptr, 1, "cap"));
                    unsigned lwords = elem_slot_words(g, lelem);
                    LLVMValueRef bytes = LLVMBuildMul(g->builder, cap,
                        LLVMConstInt(i64, (unsigned long long)(8 * lwords), 0),
                        "cpy.bytes");
                    LLVMValueRef data_ptr = zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                        get_calloc_fn(g),
                        (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), bytes }, 2,
                        "cpy.data");
                    zan_irgen_emit_oom_check(g, g->current_fn, data_ptr);
                    LLVMValueRef dst_data = LLVMBuildBitCast(g->builder, data_ptr,
                        LLVMPointerType(i64, 0), "cpy.dp");
                    zan_store_fit(g, dst_data, LLVMBuildStructGEP2(g->builder,
                        g->list_struct_type, typed_ptr, 2, "df"));

                    LLVMValueRef cfn = LLVMGetBasicBlockParent(
                        LLVMGetInsertBlock(g->builder));
                    LLVMBasicBlockRef saved_bb =
                        LLVMGetInsertBlock(g->builder);
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, cfn, "cpy.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, cfn, "cpy.body");
                    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, cfn, "cpy.end");
                    LLVMValueRef ip = LLVMBuildAlloca(g->builder, i64, "cpy.i");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), ip);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef iv = LLVMBuildLoad2(g->builder, i64, ip, "cpy.iv");
                    LLVMBuildCondBr(g->builder,
                        zan_icmp(g->builder, LLVMIntSLT, iv, src_cnt, "cpy.more"),
                        body_bb, end_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    iv = LLVMBuildLoad2(g->builder, i64, ip, "cpy.iv2");
                    LLVMValueRef off = LLVMBuildMul(g->builder, iv,
                        LLVMConstInt(i64, lwords, 0), "cpy.off");
                    LLVMValueRef s_slot = LLVMBuildGEP2(g->builder, i64,
                        src_data, &off, 1, "cpy.ss");
                    LLVMValueRef d_slot = LLVMBuildGEP2(g->builder, i64,
                        dst_data, &off, 1, "cpy.ds");
                    LLVMTypeRef elem_llvm = lelem ? map_type(g, lelem) : i64;
                    LLVMValueRef v;
                    if (LLVMGetTypeKind(elem_llvm) == LLVMStructTypeKind) {
                        v = load_struct_from_slot(g, s_slot, elem_llvm);
                    } else {
                        v = LLVMBuildLoad2(g->builder, i64, s_slot, "cpy.sv");
                        if (LLVMGetTypeKind(elem_llvm) == LLVMPointerTypeKind) {
                            v = LLVMBuildIntToPtr(g->builder, v, elem_llvm, "cpy.pv");
                        } else if (LLVMGetTypeKind(elem_llvm) == LLVMDoubleTypeKind) {
                            v = LLVMBuildBitCast(g->builder, v, elem_llvm, "cpy.dv");
                        }
                    }
                    /*
                     * rhs NULL: the loaded element is borrowed from src,
                     * so the store must retain it (owned-check yields 0).
                     */
                    emit_collection_slot_store(g, lelem, i64, d_slot, v,
                                               NULL, locals, 0);
                    LLVMValueRef nxt = LLVMBuildAdd(g->builder, iv,
                        LLVMConstInt(i64, 1, 0), "cpy.nx");
                    LLVMBuildStore(g->builder, nxt, ip);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, end_bb);
                    return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr, "listv");
                }
                /* 集合初始化式项处理 (new List<T>{ a, b, c })：发射逐项 Add 调用 */
                int ninit = expr->new_expr.args.count +
                            expr->new_expr.arg_inits.count;
                long long initcap = ninit > 8 ? (long long)ninit : 8;
                /* count = ninit */
                LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 0, "cnt");
                zan_store_fit(g, LLVMConstInt(i64, (unsigned long long)ninit, 0), count_ptr);
                /* capacity = max(8, item count) */
                LLVMValueRef cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 1, "cap");
                zan_store_fit(g, LLVMConstInt(i64, (unsigned long long)initcap, 0), cap_ptr);
                /* allocate initial data buffer: capacity * element stride */
                unsigned lwords = elem_slot_words(g, lelem);
                LLVMValueRef data_size = LLVMConstInt(i64,
                    (unsigned long long)(initcap * 8 * lwords), 0);
                LLVMValueRef data_ptr = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), data_size }, 2, "data");
                zan_irgen_emit_oom_check(g, g->current_fn, data_ptr);
                LLVMValueRef data_typed = LLVMBuildBitCast(g->builder, data_ptr,
                    LLVMPointerType(i64, 0), "dptr");
                LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 2, "df");
                zan_store_fit(g, data_typed, data_field);
                /* store each initializer item (with proper rc retain semantics) */
                for (int ii = 0; ii < ninit; ii++) {
                    zan_ast_node_t *item = ii < expr->new_expr.args.count
                        ? expr->new_expr.args.items[ii]
                        : expr->new_expr.arg_inits.items[
                              ii - expr->new_expr.args.count];
                    LLVMValueRef idxk = LLVMConstInt(i64,
                        (unsigned long long)ii * lwords, 0);
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data_typed, &idxk, 1, "iis");
                    LLVMValueRef ival = emit_expr(g, item, locals);
                    emit_collection_slot_store(g, lelem, i64, slot, ival, item, locals, 0);
                }
                return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr, "listv");
            }
        }

        /* new StringBuilder() — built-in growable byte buffer */
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF &&
            !expr->new_expr.is_array) {
            zan_istr_t sbname = expr->new_expr.type->type_ref.name;
            if (sbname.len == 13 && memcmp(sbname.str, "StringBuilder", 13) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                /*
                 * allocate StringBuilder struct { i64 count, i64 cap, i8* data }
                 * = 24 bytes, with an rc header so ARC frees the struct and its
                 * data buffer on release.
                 */
                LLVMValueRef sb_raw = emit_alloc_rc_collection(g, expr, 24, 2, NULL);
                LLVMValueRef sb_ptr = LLVMBuildBitCast(g->builder, sb_raw,
                    LLVMPointerType(g->sb_struct_type, 0), "sbp");
                LLVMValueRef cnt_p = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sb_ptr, 0, "sbc");
                zan_store_fit(g, LLVMConstInt(i64, 0, 0), cnt_p);
                LLVMValueRef cap_p = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sb_ptr, 1, "sbcap");
                zan_store_fit(g, LLVMConstInt(i64, 0, 0), cap_p);
                LLVMValueRef data_p = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sb_ptr, 2, "sbd");
                zan_store_fit(g, LLVMConstNull(i8ptr), data_p);
                return LLVMBuildBitCast(g->builder, sb_ptr, i8ptr, "sbv");
            }
        }

        /* new Dict<K,V>() — built-in hash map */
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF &&
            !expr->new_expr.is_array) {
            zan_istr_t tname2 = expr->new_expr.type->type_ref.name;
            if ((tname2.len == 4 && memcmp(tname2.str, "Dict", 4) == 0) ||
                (tname2.len == 10 && memcmp(tname2.str, "Dictionary", 10) == 0)) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                /*
                 * allocate the Dict struct (7 i64-sized fields) with an rc
                 * header, so a dict is owned like any other collection.
                 */
                LLVMValueRef dict_raw = emit_alloc_rc_collection(g, expr, 64, 3,
                    resolve_type_ctx(g, expr->new_expr.type));
                LLVMValueRef typed_ptr = LLVMBuildBitCast(g->builder, dict_raw,
                    LLVMPointerType(g->dict_struct_type, 0), "dptr");
                /*
                 * zan_rt_alloc does not zero: the hash index fields must start
                 * at "no index yet" explicitly.
                 */
                for (unsigned zf = 4; zf < 7; zf++) {
                    LLVMValueRef zp = LLVMBuildStructGEP2(g->builder,
                        g->dict_struct_type, typed_ptr, zf, "dz");
                    zan_store_fit(g,
                        (zf == 4) ? (LLVMValueRef)LLVMConstNull(LLVMPointerType(i64, 0))
                                  : (LLVMValueRef)LLVMConstInt(i64, 0, 0), zp);
                }
                zan_type_t *dict_type = resolve_type_ctx(g, expr->new_expr.type);
                zan_type_t *value_type = dict_value_type(dict_type);
                unsigned value_words = elem_slot_words(g, value_type);
                LLVMValueRef value_words_const = LLVMConstInt(i64, value_words, 0);
                zan_store_fit(g, value_words_const,
                    LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 7, "vwp"));
                /* count = 0 */
                LLVMValueRef cnt_p = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 0, "cnt");
                zan_store_fit(g, LLVMConstInt(i64, 0, 0), cnt_p);
                /* capacity = 16 */
                LLVMValueRef cap_p = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 1, "cap");
                zan_store_fit(g, LLVMConstInt(i64, 16, 0), cap_p);
                /* keys = malloc(16 * 8) */
                LLVMValueRef keys_sz = LLVMConstInt(i64, 128, 0);
                LLVMValueRef keys_raw = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), keys_sz }, 2, "keys");
                zan_irgen_emit_oom_check(g, g->current_fn, keys_raw);
                LLVMValueRef keys_typed = LLVMBuildBitCast(g->builder, keys_raw,
                    LLVMPointerType(i8ptr, 0), "kptr");
                LLVMValueRef kf = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 2, "kf");
                zan_store_fit(g, keys_typed, kf);
                /* values = malloc(16 * 8) */
                LLVMValueRef vals_sz = LLVMConstInt(i64, 128ULL * value_words, 0);
                LLVMValueRef vals_raw = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), vals_sz }, 2, "vals");
                zan_irgen_emit_oom_check(g, g->current_fn, vals_raw);
                LLVMValueRef vals_typed = LLVMBuildBitCast(g->builder, vals_raw,
                    LLVMPointerType(i64, 0), "vptr");
                LLVMValueRef vf = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 3, "vf");
                zan_store_fit(g, vals_typed, vf);
                /* 字典初始化式条目处理 (new Dict<K,V>{ {k, v}, ... })：发射逐项索引器写入 */
                zan_ast_list_t dict_inits;
                zan_ast_list_init(&dict_inits);
                for (int di = 0; di < expr->new_expr.args.count; di++)
                    zan_ast_list_push(&dict_inits,
                        expr->new_expr.args.items[di], g->arena);
                for (int di = 0; di < expr->new_expr.arg_inits.count; di++)
                    zan_ast_list_push(&dict_inits,
                        expr->new_expr.arg_inits.items[di], g->arena);
                int ninit = dict_inits.count;
                if (ninit >= 2) {
                    zan_type_t *kt = dict_key_type(g, dict_type);
                    for (int ii = 0; ii + 1 < ninit; ii += 2) {
                        zan_ast_node_t *kexpr = dict_inits.items[ii];
                        zan_ast_node_t *vexpr = dict_inits.items[ii + 1];
                        LLVMValueRef key = emit_expr(g, kexpr, locals);
                        if (LLVMGetTypeKind(LLVMTypeOf(key)) == LLVMIntegerTypeKind) {
                            if (LLVMGetIntTypeWidth(LLVMTypeOf(key)) < 64)
                                key = LLVMBuildSExt(g->builder, key, i64, "k.sx");
                            key = LLVMBuildIntToPtr(g->builder, key, i8ptr, "k.ip");
                        } else if (LLVMTypeOf(key) != i8ptr) {
                            key = LLVMBuildBitCast(g->builder, key, i8ptr, "k.bc");
                        }
                        LLVMValueRef val = emit_expr(g, vexpr, locals);
                        emit_dict_value_set(g, dict_type, dict_raw, key, val,
                            kexpr, vexpr, locals);
                    }
                }
                return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr, "dictv");
            }
        }

        /* 数组初始化式 (new Type[] { a, b, c })：分配连续内存并填充初始元素 */
        if (expr->new_expr.is_array && expr->new_expr.array_init) {
            /*
             * the type node is written `T[]` here, so it resolves to the
             * array type; the elements are of its element type
             */
            zan_type_t *elem_type = resolve_type_ctx(g, expr->new_expr.type);
            if (elem_type && elem_type->kind == TYPE_ARRAY && elem_type->element_type)
                elem_type = elem_type->element_type;
            if (!elem_type) elem_type = g->binder->type_int;
            LLVMTypeRef elem_llvm = map_type(g, elem_type);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            int n = expr->new_expr.args.count;
            LLVMValueRef total = zan_mul(g->builder,
                LLVMConstInt(i64t, (unsigned long long)n, 0),
                LLVMSizeOf(elem_llvm), "total");
            LLVMValueRef arr = zan_array_alloc_typed(g, total,
                LLVMConstInt(i64t, (unsigned long long)n, 0), elem_type);
            for (int k = 0; k < n; k++) {
                LLVMValueRef v = emit_arg_typed(g, expr->new_expr.args.items[k],
                                                elem_type, locals);
                /* 数组缓冲区持有元素所有权：ARC 引用类型元素保留 +1 计数 */
                if (elem_type && is_rc_managed_type(elem_type) &&
                    !expr_yields_owned_rc_value(g, expr->new_expr.args.items[k],
                                                locals)) {
                    emit_rc_retain_for_type(g, elem_type, v);
                }
                LLVMValueRef idx = LLVMConstInt(i64t, (unsigned long long)k, 0);
                LLVMValueRef slot = LLVMBuildGEP2(g->builder, elem_llvm, arr,
                                                  &idx, 1, "aep");
                /*
                 * fit the value to the element width: storing an i64 into a
                 * byte slot would write over the elements after it
                 */
                LLVMBuildStore(g->builder, emit_boundary_coerce(g, v, elem_llvm), slot);
            }
            return arr;
        }

        /* 数组分配 (new Type[d1, d2, ...])：多维数组分配形状描述符与平铺缓冲区 */
        if (expr->new_expr.is_array && expr->new_expr.args.count > 0) {
            zan_type_t *alloc_type = resolve_type_ctx(g, expr->new_expr.type);
            if (!alloc_type) alloc_type = g->binder->type_int;
            int rank = expr->new_expr.array_rank > 0
                ? expr->new_expr.array_rank : 1;
            /*
             * element type: one level below the sized level, so
             * `new int[3][]` allocates a buffer of int[] row pointers
             */
            zan_type_t *elem_type = alloc_type;
            if (elem_type->kind == TYPE_ARRAY && elem_type->element_type)
                elem_type = elem_type->element_type;
            LLVMTypeRef elem_llvm = map_type(g, elem_type);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            int ndims = rank > 16 ? 16 : rank;
            LLVMValueRef dims[16];
            for (int d = 0; d < ndims; d++) {
                LLVMValueRef dv = emit_expr(g, expr->new_expr.args.items[d], locals);
                dv = emit_index_i64(g, dv, "arr.dim");
                LLVMValueRef negative = zan_icmp(g->builder, LLVMIntSLT, dv,
                    LLVMConstInt(i64t, 0, 0), "arr.negative");
                emit_runtime_check(g, negative,
                    expr->new_expr.args.items[d]->loc, "negative array length");
                /* 数组维度负数检查与越界防护 */
                dims[d] = LLVMBuildSelect(g->builder, negative,
                    LLVMConstInt(i64t, 0, 0), dv, "arr.dim.safe");
            }
            LLVMValueRef arr;
            if (rank <= 1) {
                LLVMValueRef total = zan_mul(g->builder, dims[0],
                    LLVMSizeOf(elem_llvm), "total");
                arr = zan_array_alloc_typed(g, total, dims[0], elem_type);
            } else {
                arr = zan_mdarray_alloc(g, dims, rank, elem_llvm);
            }
            /* element initializer after the dims */
            int ninit = expr->new_expr.args.count - ndims;
            if (ninit > 0) {
                LLVMValueRef data = arr;
                if (rank > 1) {
                    LLVMValueRef data_off = LLVMConstInt(i64t,
                        (unsigned long long)rank * 8, 0);
                    data = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                        arr, &data_off, 1, "md.data");
                }
                LLVMValueRef typed = LLVMBuildBitCast(g->builder, data,
                    LLVMPointerType(elem_llvm, 0), "aip");
                for (int k = 0; k < ninit; k++) {
                    LLVMValueRef v = emit_arg_typed(g,
                        expr->new_expr.args.items[ndims + k], elem_type, locals);
                    /*
                     * same ARC protocol as the unsized literal above: the
                     * buffer owns refcounted elements, so retain on store
                     */
                    if (elem_type && is_rc_managed_type(elem_type) &&
                        !expr_yields_owned_rc_value(g,
                            expr->new_expr.args.items[ndims + k], locals)) {
                        emit_rc_retain_for_type(g, elem_type, v);
                    }
                    LLVMValueRef idx = LLVMConstInt(i64t, (unsigned long long)k, 0);
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, elem_llvm,
                        typed, &idx, 1, "aep");
                    /*
                     * fit the value to the element width: storing an i64 into
                     * a byte slot would write over the elements after it
                     */
                    LLVMBuildStore(g->builder,
                        emit_boundary_coerce(g, v, elem_llvm), slot);
                }
            }
            return arr;
        }

        /* new ClassName(args) — allocate struct + call constructor */
        zan_istr_t type_name = {NULL, 0};
        if (expr->new_expr.type) {
            if (expr->new_expr.type->kind == AST_IDENTIFIER) {
                type_name = expr->new_expr.type->ident.name;
            } else if (expr->new_expr.type->kind == AST_TYPE_REF) {
                type_name = expr->new_expr.type->type_ref.name;
            }
        }
        /* `new object()`: allocate a minimal ARC heap object */
        if (type_name.str && type_name.len == 6 && memcmp(type_name.str, "object", 6) == 0) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef sz = LLVMConstInt(i64, 8, 0); /* 8-byte minimal payload */
            LLVMValueRef site_name = LLVMConstNull(i8ptr);
            int site_idx = reserve_arc_site(g, NULL, NULL, 0, NULL);
            LLVMValueRef site_val = arc_site_arg(g, site_idx);
            if (g->check_leaks) {
                char site_buf[600];
                const char *sfile = loc_site_file(g, expr->loc);
                snprintf(site_buf, sizeof(site_buf), "%s:%u:%u",
                         sfile, expr->loc.line, expr->loc.col);
                site_name = zan_irgen_intern_string(g, site_buf);
            }
            LLVMTypeRef alloc_fn_type = LLVMFunctionType(i8ptr,
                (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0);
            LLVMValueRef alloc_args3[] = { sz, site_val, site_name };
            LLVMValueRef raw = zan_call2(g->builder, alloc_fn_type, g->rt_alloc, alloc_args3, 3, "newobj");
            LLVMValueRef zero64 = LLVMConstInt(i64, 0, 0);
            LLVMValueRef p64 = LLVMBuildBitCast(g->builder, raw, LLVMPointerType(i64, 0), "objp");
            zan_store_fit(g, zero64, p64);
            return raw;
        }
        if (type_name.str) {
            zan_symbol_t *sym = zan_binder_lookup(g->binder, type_name);
            if (sym && sym->type && (sym->type->kind == TYPE_STRUCT || sym->type->kind == TYPE_CLASS)) {
                LLVMTypeRef st = get_struct_llvm_type(g, sym);
                if (st) {
                    LLVMValueRef alloca;
                    if (sym->type->kind == TYPE_CLASS) {
                        /*
                         * reference type: heap-allocate via ARC so instances
                         * outlive the enclosing frame and are leak-tracked.
                         */
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        LLVMValueRef sz = LLVMSizeOf(st);
                        LLVMValueRef site_name = LLVMConstNull(i8ptr);
                        LLVMValueRef site_val;
                        {
                            /* 分配固定分配点索引用于内存剖析与诊断 */
                            zan_type_t *site_inst =
                                resolve_type_ctx(g, expr->new_expr.type);
                            int site_idx = reserve_arc_site(
                                g, sym, site_inst, 0, NULL);
                            site_val = arc_site_arg(g, site_idx);
                            if (g->check_leaks) {
                                char site_buf[600];
                                const char *sfile = loc_site_file(g, expr->loc);
                                snprintf(site_buf, sizeof(site_buf), "%s:%u:%u",
                                         sfile, expr->loc.line, expr->loc.col);
                                site_name = zan_irgen_intern_string(g, site_buf);
                            }
                        }
                        LLVMTypeRef alloc_fn_type = LLVMFunctionType(i8ptr,
                            (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0);
                        LLVMValueRef alloc_args3[] = { sz, site_val, site_name };
                        LLVMValueRef raw = zan_call2(g->builder, alloc_fn_type, g->rt_alloc, alloc_args3, 3, "newobj");
                        alloca = LLVMBuildBitCast(g->builder, raw, LLVMPointerType(st, 0), "objp");
                    } else {
                        /* value type: stack-allocate as before */
                        alloca = emit_entry_alloca(g, st, "new");
                    }
                    zan_store_fit(g, LLVMConstNull(st), alloca);

                    /*
                     * install the vtable pointer (field 0) before the ctor runs
                     * so virtual calls made during construction dispatch to the
                     * most-derived implementation.
                     */
                    if (sym->type->kind == TYPE_CLASS && class_has_virtual_methods(sym)) {
                        LLVMTypeRef i8ptr_vt = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMValueRef vtg = get_vtable_global(g, sym);
                        LLVMValueRef vpf0 = LLVMBuildStructGEP2(g->builder, st, alloca, 0, "vpf.init");
                        zan_store_fit(g,
                            LLVMBuildBitCast(g->builder, vtg, i8ptr_vt, "vt.i8"), vpf0);
                    }

                    /* 查找并解析类的实例构造函数 */
                    zan_type_t *new_inst = resolve_type_ctx(g, expr->new_expr.type);
                    /* 对象初始化式字段赋值发射 */
                    int init_start = expr->new_expr.args.count;
                    while (init_start > 0) {
                        zan_ast_node_t *a =
                            expr->new_expr.args.items[init_start - 1];
                        if (a->kind == AST_COLL_INIT) {
                            if (get_field_index(sym, a->coll_init.name) < 0)
                                break;
                            init_start--;
                            continue;
                        }
                        if (a->kind != AST_ASSIGNMENT ||
                            a->binary.left->kind != AST_IDENTIFIER ||
                            get_field_index(sym, a->binary.left->ident.name) < 0)
                            break;
                        init_start--;
                    }
                    zan_ast_list_t ctor_arg_list = expr->new_expr.args;
                    ctor_arg_list.count = init_start;
                    struct zan_ctor_entry *ctor = find_ctor(
                        g, sym, &ctor_arg_list, locals, NULL);
                    if (!ctor) {
                        zan_ast_list_t filled;
                        if (fill_ctor_default_args(g, sym, &ctor_arg_list,
                                                   &filled)) {
                            struct zan_ctor_entry *dc = find_ctor(
                                g, sym, &filled, locals, NULL);
                            if (dc) {
                                ctor = dc;
                                ctor_arg_list = filled;
                            }
                        }
                    }
                    LLVMValueRef ctor_fn = NULL;
                    LLVMTypeRef ctor_ft = NULL;
                    if (ctor && new_inst && new_inst->type_arg_count > 0)
                        ctor_fn = find_generic_ctor(g, sym, ctor->decl,
                                                    new_inst->type_args,
                                                    new_inst->type_arg_count,
                                                    &ctor_ft);
                    if (!ctor_fn && ctor) {
                        ctor_fn = ctor->fn;
                        ctor_ft = ctor->fn_type;
                    }
                    if (ctor && ctor->decl) {
                        /* 构造函数命名参数对齐与重排序 */
                        reorder_named_args_impl(g, &ctor_arg_list,
                                                expr->loc, ctor->decl);
                    }
                    if (ctor_fn) {
                        int argc = ctor_arg_list.count + 1;
                        LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                        call_args[0] = alloca; /* this ptr */
                        /*
                         * the fresh object must survive a throwing ctor (or a
                         * throwing arg expression): keep it on the EH temp
                         * stack so a catch can release it
                         */
                        int obj_eh_pushed = 0;
                        if (sym->type->kind == TYPE_CLASS) {
                            emit_eh_tmp_push(g, alloca);
                            obj_eh_pushed = 1;
                        }
                        for (int k = 0; k < ctor_arg_list.count; k++) {
                            zan_ast_node_t *param =
                                ctor->decl->method_decl.params.items[k];
                            zan_type_t *pt = zan_binder_resolve_type(
                                g->binder, param->param.type);
                            /* 泛型类构造函数签名特化与参数映射 */
                            if (new_inst && new_inst->type_arg_count > 0)
                                pt = subst_delegate_sig(g, pt, new_inst);
                            call_args[k + 1] = emit_arg_typed(
                                g, ctor_arg_list.items[k], pt, locals);
                        }
                        coerce_args_to_params(g, ctor_ft, call_args, argc);
                        zan_call2(g->builder, ctor_ft, ctor_fn, call_args, (unsigned)argc, "");
                        if (obj_eh_pushed) emit_eh_tmp_pop(g);
                        for (int k = 0; k < ctor_arg_list.count; k++) {
                            emit_release_owned_call_temp(g, ctor_arg_list.items[k],
                                call_args[k + 1], locals);
                        }
                        free(call_args);
                    } else {
                        /* Arguments were supplied but no constructor accepts them. */
                        if (sym->type->kind == TYPE_CLASS &&
                            ctor_arg_list.count > 0)
                            zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                                "no constructor of '%.*s' accepts %d argument%s",
                                (int)sym->name.len, sym->name.str,
                                ctor_arg_list.count,
                                ctor_arg_list.count == 1 ? "" : "s");
                        int fields_eh_pushed = 0;
                        if (sym->type->kind == TYPE_CLASS) {
                            emit_eh_tmp_push(g, alloca);
                            fields_eh_pushed = 1;
                        }
                        emit_implicit_field_initializers(
                            g, sym, new_inst ? new_inst : sym->type,
                            alloca, locals);
                        if (fields_eh_pushed) emit_eh_tmp_pop(g);
                    }

                    /* object-initializer field writes, after construction */
                    {
                        /* 构造函数参数后置的常规对象初始化式 */
                        zan_ast_list_t object_inits;
                        zan_ast_list_init(&object_inits);
                        for (int oi = init_start;
                             oi < expr->new_expr.args.count; oi++) {
                            zan_ast_list_push(&object_inits,
                                expr->new_expr.args.items[oi], g->arena);
                        }
                        for (int oi = 0;
                             oi < expr->new_expr.arg_inits.count; oi++) {
                            zan_ast_list_push(&object_inits,
                                expr->new_expr.arg_inits.items[oi], g->arena);
                        }
                        for (int i = 0; i < object_inits.count; i++) {
                            zan_ast_node_t *arg = object_inits.items[i];
                            /* 嵌套集合初始化式 Members = { a, b } 降解 */
                            if (arg->kind == AST_COLL_INIT) {
                                zan_istr_t cname = arg->coll_init.name;
                                zan_symbol_t *msym = get_field_sym(sym, cname);
                                if (!msym || !msym->type) continue;
                                int getter_owned = 0;
                                zan_ast_node_t *recv = zan_ast_new(g->arena,
                                    AST_IDENTIFIER, arg->loc);
                                recv->ident.name = cname;
                                /* 对象初始化式中隐式读取新分配实例的 this 指针 */
                                LLVMTypeRef coll_lt = map_type(g, msym->type);
                                LLVMValueRef cslot = emit_entry_alloca(g,
                                    coll_lt, "cinit.slot");
                                zan_store_fit(g, LLVMConstNull(coll_lt), cslot);
                                int cfi = get_field_index(sym, cname);
                                zan_symbol_t *getter =
                                    property_getter_sym(g, msym);
                                if (getter) {
                                    LLVMValueRef v = emit_property_getter_call(
                                        g, getter,
                                        new_inst ? new_inst : sym->type,
                                        alloca, arg, locals);
                                    zan_store_fit(g, v, cslot);
                                    /* getter 返回值持有 +1 引用处理 */
                                    getter_owned = 1;
                                } else if (cfi >= 0) {
                                    LLVMValueRef cptr = emit_field_ptr(g, sym,
                                        st, alloca, cfi, "cinit.ptr");
                                    LLVMValueRef v = LLVMBuildLoad2(g->builder,
                                        coll_lt, cptr, "cinit.load");
                                    zan_store_fit(g, v, cslot);
                                } else {
                                    continue;
                                }
                                local_add(locals, cname, cslot, msym->type);
                                /* 类类型快照借用加载语义 */
                                for (int k = 0;
                                     k < arg->coll_init.items.count; k++) {
                                    zan_ast_node_t *item =
                                        arg->coll_init.items.items[k];
                                    zan_ast_node_t *madd = zan_ast_new(g->arena,
                                        AST_MEMBER_ACCESS, item->loc);
                                    madd->member.object = recv;
                                    madd->member.name = (zan_istr_t){"Add", 3};
                                    madd->member.null_cond = 0;
                                    zan_ast_node_t *addcall = zan_ast_new(
                                        g->arena, AST_CALL, item->loc);
                                    addcall->call.callee = madd;
                                    zan_ast_list_init(&addcall->call.args);
                                    zan_ast_list_init(&addcall->call.type_args);
                                    zan_ast_list_push(&addcall->call.args, item,
                                                      g->arena);
                                    /*
                                     * Dict members take (key, value): the
                                     * parser flattened `{ k, v }` braces into
                                     * consecutive items, so pair them here.
                                     */
                                    if (msym->type &&
                                        type_named(msym->type, "Dict", 4) &&
                                        k + 1 < arg->coll_init.items.count) {
                                        zan_ast_list_push(&addcall->call.args,
                                            arg->coll_init.items.items[++k],
                                            g->arena);
                                    }
                                    emit_expr(g, addcall, locals);
                                }
                                /*
                                 * drop the temp registration so a later
                                 * same-named declaration is unaffected
                                 */
                                for (int lv = locals->count - 1; lv >= 0; lv--) {
                                    if (locals->vars[lv].name.len == cname.len &&
                                        memcmp(locals->vars[lv].name.str,
                                               cname.str, (size_t)cname.len) == 0) {
                                        for (int sh = lv; sh < locals->count - 1; sh++)
                                            locals->vars[sh] = locals->vars[sh + 1];
                                        locals->count--;
                                        break;
                                    }
                                }
                                if (getter_owned) {
                                    LLVMValueRef cv = LLVMBuildLoad2(g->builder,
                                        coll_lt, cslot, "cinit.done");
                                    emit_rc_release_for_type(g, msym->type, cv);
                                }
                                continue;
                            }
                            if (arg->kind == AST_ASSIGNMENT && arg->binary.left->kind == AST_IDENTIFIER) {
                                zan_symbol_t *fsym = get_field_sym(sym, arg->binary.left->ident.name);
                                /*
                                 * custom-setter property in an object
                                 * initializer: `new Foo { Prop = v }`
                                 * dispatches to set_Prop(v)
                                 */
                                zan_symbol_t *setter = property_setter_sym(g, fsym);
                                if (setter) {
                                    emit_property_setter_call(g, setter,
                                        new_inst ? new_inst : sym->type,
                                        alloca, emit_expr(g, arg->binary.right, locals),
                                        arg->binary.left, arg->binary.right, locals);
                                    continue;
                                }
                                int fi = get_field_index(sym, arg->binary.left->ident.name);
                                if (fi >= 0) {
                                    LLVMValueRef fptr = emit_field_ptr(g, sym, st, alloca, fi, "finit");
                                    zan_symbol_t *fsym = get_field_sym(sym, arg->binary.left->ident.name);
                                    /*
                                     * `Binding<T> Field = <T expr>` is the same
                                     * sugar as in a plain assignment: wrap the
                                     * value in a binding instead of storing a
                                     * raw T where a Binding is expected.
                                     */
                                    int fval_owned = 0;
                                    LLVMValueRef fval = NULL;
                                    if (fsym && fsym->type && type_is_binding(fsym->type) &&
                                        !type_is_binding(infer_expr_type(g, arg->binary.right, locals))) {
                                        fval = emit_binding_value(g, fsym->type,
                                                                  arg->binary.right, locals);
                                        if (fval) fval_owned = 1;
                                    }
                                    if (!fval)
                                        fval =
                                        (fsym && fsym->type && fsym->type->kind == TYPE_DELEGATE &&
                                         arg->binary.right->kind == AST_LAMBDA)
                                            ? emit_lambda_typed(g, arg->binary.right, fsym->type, locals)
                                            : emit_expr(g, arg->binary.right, locals);
                                    if (fsym && fsym->type) {
                                        LLVMTypeRef target_t = map_type(g, fsym->type);
                                        LLVMTypeRef val_t = LLVMTypeOf(fval);
                                        if (LLVMGetTypeKind(target_t) == LLVMFloatTypeKind &&
                                            LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                                            fval = LLVMBuildFPTrunc(g->builder, fval, target_t, "trunc");
                                        }
                                    }
                                    /* ARC 字段存储契约：旧值释放与新值保留 */
                                    if (fsym && fsym->type && fval) {
                                        zan_type_t *fsty = field_store_type(
                                            g, fsym, new_inst ? new_inst : sym->type);
                                        if (fsty && (is_rc_managed_type(fsty) || fsty->kind == TYPE_OBJECT) &&
                                            !(fsym->modifiers & MOD_WEAK)) {
                                            /* 绑定值发射结果处理 */
                                            emit_rc_store_field(g, fsty, fptr, fval,
                                                fval_owned
                                                    ? owned_rhs_marker(g, arg->binary.right->loc)
                                                    : arg->binary.right,
                                                locals, 0);
                                        } else if (fsty && fsty->kind == TYPE_STRUCT &&
                                                   type_contains_collection_rc(g, fsty, 0)) {
                                            emit_struct_field_capture(g, fsty, fptr, fval,
                                                fval_owned
                                                    ? owned_rhs_marker(g, arg->binary.right->loc)
                                                    : arg->binary.right, locals);
                                        } else {
                                            zan_store_fit(g, fval, fptr);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    return alloca;
                }
            }
        }
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

/* 统一三元表达式分支值类型至 PHI 节点类型，适配整型位宽与指针类型 */
static LLVMValueRef coerce_ternary_value(zan_irgen_t *g, LLVMValueRef v,
                                         LLVMTypeRef target) {
    if (!v || !target) return v;
    int vk = LLVMGetTypeKind(LLVMTypeOf(v));
    int tk = LLVMGetTypeKind(target);
    if (vk == LLVMIntegerTypeKind &&
        (tk == LLVMFloatTypeKind || tk == LLVMDoubleTypeKind))
        return LLVMBuildSIToFP(g->builder, v, target, "tern.sitofp");
    if ((vk == LLVMFloatTypeKind || vk == LLVMDoubleTypeKind) &&
        tk == LLVMIntegerTypeKind)
        return LLVMBuildFPToSI(g->builder, v, target, "tern.fptosi");
    return coerce_int_to(g, v, target);
}

static LLVMValueRef emit_expr_conditional(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* ternary: condition ? then_expr : else_expr */
        LLVMValueRef cond = emit_expr(g, expr->conditional.cond, locals);
        /* normalize to i1 */
        cond = zan_tobool(g->builder, cond, "cond");
        LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "tern.then");
        LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "tern.else");
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "tern.merge");
        LLVMBuildCondBr(g->builder, cond, then_bb, else_bb);

        LLVMPositionBuilderAtEnd(g->builder, then_bb);
        LLVMValueRef then_val = emit_expr(g, expr->conditional.then_expr, locals);
        LLVMBasicBlockRef then_end = LLVMGetInsertBlock(g->builder);

        LLVMPositionBuilderAtEnd(g->builder, else_bb);
        LLVMValueRef else_val = emit_expr(g, expr->conditional.else_expr, locals);
        LLVMBasicBlockRef else_end = LLVMGetInsertBlock(g->builder);

        /* 分支字面量位宽对齐转换 */
        LLVMTypeRef tty = LLVMTypeOf(then_val);
        LLVMTypeRef ety = LLVMTypeOf(else_val);
        LLVMTypeRef phi_ty = tty;
        zan_type_t *conditional_type = infer_expr_type(g, expr, locals);
        if (conditional_type && is_rc_managed_type(conditional_type)) {
            int then_owned = expr_yields_owned_rc_value(
                g, expr->conditional.then_expr, locals);
            int else_owned = expr_yields_owned_rc_value(
                g, expr->conditional.else_expr, locals);
            /* 产生持有引用的条件表达式需规范化各分支所有权状态 */
            if (then_owned != else_owned) {
                if (!then_owned) {
                    LLVMPositionBuilderAtEnd(g->builder, then_end);
                    emit_rc_retain_for_type(g, conditional_type, then_val);
                } else {
                    LLVMPositionBuilderAtEnd(g->builder, else_end);
                    emit_rc_retain_for_type(g, conditional_type, else_val);
                }
            }
        }
        if (tty != ety) {
            zan_type_t *st = conditional_type;
            if (st) {
                LLVMTypeRef want = map_type(g, st);
                if (want) phi_ty = want;
            }
            if (phi_ty == tty) {
                int tk = LLVMGetTypeKind(tty), ek = LLVMGetTypeKind(ety);
                if (tk == LLVMIntegerTypeKind && ek == LLVMIntegerTypeKind) {
                    if (LLVMGetIntTypeWidth(ety) > LLVMGetIntTypeWidth(tty))
                        phi_ty = ety;
                } else if ((tk == LLVMFloatTypeKind || tk == LLVMDoubleTypeKind) &&
                           (ek == LLVMFloatTypeKind || ek == LLVMDoubleTypeKind)) {
                    if ((ek == LLVMDoubleTypeKind) &&
                        !(tk == LLVMDoubleTypeKind)) phi_ty = ety;
                } else if (ek == LLVMFloatTypeKind || ek == LLVMDoubleTypeKind) {
                    phi_ty = ety;
                }
            }
            LLVMPositionBuilderAtEnd(g->builder, then_end);
            then_val = coerce_ternary_value(g, then_val, phi_ty);
            LLVMPositionBuilderAtEnd(g->builder, else_end);
            else_val = coerce_ternary_value(g, else_val, phi_ty);
        }

        LLVMPositionBuilderAtEnd(g->builder, then_end);
        LLVMBuildBr(g->builder, merge_bb);
        LLVMPositionBuilderAtEnd(g->builder, else_end);
        LLVMBuildBr(g->builder, merge_bb);

        LLVMPositionBuilderAtEnd(g->builder, merge_bb);
        LLVMValueRef phi = LLVMBuildPhi(g->builder, phi_ty, "tern");
        LLVMValueRef incoming_vals[] = { then_val, else_val };
        LLVMBasicBlockRef incoming_bbs[] = { then_end, else_end };
        LLVMAddIncoming(phi, incoming_vals, incoming_bbs, 2);
        return phi;
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

/* switch 表达式 (expr switch { pattern => result, ... }) 模式匹配降解 */
static LLVMValueRef emit_expr_switch_expr(zan_irgen_t *g, zan_ast_node_t *expr,
                                          local_scope_t *locals) {
    zan_type_t *rty = infer_expr_type(g, expr, locals);
    if (!rty || rty->kind == TYPE_ERROR || rty->kind == TYPE_VOID)
        rty = g->binder->type_int;
    LLVMTypeRef rtll = map_type(g, rty);
    LLVMValueRef rslot = emit_entry_alloca(g, rtll, "swx.result");
    zan_store_fit(g, LLVMConstNull(rtll), rslot);

    /*
     * hidden local so each arm's `result` assignment lowers as a plain store
     * (the name starts with \x01, which no source identifier can contain)
     */
    static const char kSwx[] = { 1, 's', 'w', 'x' };
    zan_istr_t hname = { (char *)kSwx, 4 };
    int mark = locals->count;
    local_add(locals, hname, rslot, rty);

    zan_ast_node_t *syn = zan_ast_new(g->arena, AST_SWITCH_STMT, expr->loc);
    syn->switch_stmt.expr = expr->switch_expr.expr;
    zan_ast_list_init(&syn->switch_stmt.cases);

    int narms = expr->switch_expr.arms.count;
    bool has_default = false;
    for (int i = 0; i < narms; i++) {
        zan_ast_node_t *arm = expr->switch_expr.arms.items[i];
        if (arm->switch_arm.is_default) has_default = true;

        zan_ast_node_t *sc = zan_ast_new(g->arena, AST_SWITCH_CASE, arm->loc);
        sc->switch_case.pattern = arm->switch_arm.pattern;
        sc->switch_case.type_pattern = arm->switch_arm.type_pattern;
        sc->switch_case.when_cond = arm->switch_arm.when_cond;
        sc->switch_case.var_name = arm->switch_arm.var_name;

        /* body: { __swx = result; break; } */
        zan_ast_node_t *block = zan_ast_new(g->arena, AST_BLOCK, arm->loc);
        zan_ast_list_init(&block->block.stmts);
        zan_ast_node_t *assign = zan_ast_new(g->arena, AST_ASSIGNMENT, arm->loc);
        zan_ast_node_t *lhs = zan_ast_new(g->arena, AST_IDENTIFIER, arm->loc);
        lhs->ident.name = hname;
        assign->binary.op = TK_EQ;
        assign->binary.left = lhs;
        assign->binary.right = arm->switch_arm.result;
        zan_ast_node_t *es = zan_ast_new(g->arena, AST_EXPR_STMT, arm->loc);
        es->expr_stmt.expr = assign;
        zan_ast_list_push(&block->block.stmts, es, g->arena);
        zan_ast_node_t *brk = zan_ast_new(g->arena, AST_BREAK_STMT, arm->loc);
        zan_ast_list_push(&block->block.stmts, brk, g->arena);
        sc->switch_case.body = block;
        zan_ast_list_push(&syn->switch_stmt.cases, sc, g->arena);
    }
    if (!has_default) {
        /*
         * no discard arm: an unmatched value keeps the zero-initialised slot;
         * an empty-but-breaking default keeps every path defined
         */
        zan_ast_node_t *sc = zan_ast_new(g->arena, AST_SWITCH_CASE, expr->loc);
        zan_ast_node_t *block = zan_ast_new(g->arena, AST_BLOCK, expr->loc);
        zan_ast_list_init(&block->block.stmts);
        zan_ast_node_t *brk = zan_ast_new(g->arena, AST_BREAK_STMT, expr->loc);
        zan_ast_list_push(&block->block.stmts, brk, g->arena);
        sc->switch_case.body = block;
        zan_ast_list_push(&syn->switch_stmt.cases, sc, g->arena);
    }

    emit_stmt(g, syn, locals);
    locals->count = mark;
    return LLVMBuildLoad2(g->builder, rtll, rslot, "swx.load");
}

/* record with 表达式：非破坏性浅拷贝并覆盖指定字段 */
static LLVMValueRef emit_expr_with_expr(zan_irgen_t *g, zan_ast_node_t *expr,
                                        local_scope_t *locals) {
    zan_istr_t cm = { (char *)"__CloneWith", 11 };
    zan_type_t *rty = concretize(g, infer_expr_type(g, expr->with_expr.expr,
                                                    locals));
    if (!rty || !rty->sym ||
        !get_method_sym(rty->sym, cm)) {
        zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
            "'with' requires a record receiver with a synthesized clone");
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
    }
    static const char kWr[] = { 1, 'w', 'r' };
    zan_istr_t hname = { (char *)kWr, 3 };
    LLVMTypeRef rtll = map_type(g, rty);
    LLVMValueRef recv = emit_expr(g, expr->with_expr.expr, locals);
    LLVMValueRef rslot = emit_entry_alloca(g, rtll, "with.recv");
    zan_store_fit(g, recv, rslot);
    int mark = locals->count;
    local_add(locals, hname, rslot, rty);

    /*
     * build `__wr.__CloneWith(a0, a1, ...)`: per field, the with-assignment's
     * value or a read of the hidden local's field
     */
    zan_ast_node_t *recv_id = zan_ast_new(g->arena, AST_IDENTIFIER, expr->loc);
    recv_id->ident.name = hname;
    zan_ast_node_t *callee = zan_ast_new(g->arena, AST_MEMBER_ACCESS, expr->loc);
    callee->member.object = recv_id;
    callee->member.name = cm;
    zan_ast_node_t *call = zan_ast_new(g->arena, AST_CALL, expr->loc);
    call->call.callee = callee;
    zan_ast_list_init(&call->call.args);
    zan_ast_list_init(&call->call.type_args);
    for (int fi = 0; fi < rty->sym->member_count; fi++) {
        zan_symbol_t *m = rty->sym->members[fi];
        if (!m || m->kind != SYM_FIELD) continue;
        zan_ast_node_t *arg = NULL;
        for (int ai = 0; ai < expr->with_expr.assigns.count; ai++) {
            zan_ast_node_t *asg = expr->with_expr.assigns.items[ai];
            if (asg && asg->kind == AST_ASSIGNMENT &&
                asg->binary.left->ident.name.len == m->name.len &&
                memcmp(asg->binary.left->ident.name.str, m->name.str,
                       (size_t)m->name.len) == 0) {
                arg = asg->binary.right;
                break;
            }
        }
        if (!arg) {
            arg = zan_ast_new(g->arena, AST_MEMBER_ACCESS, expr->loc);
            zan_ast_node_t *obj =
                zan_ast_new(g->arena, AST_IDENTIFIER, expr->loc);
            obj->ident.name = hname;
            arg->member.object = obj;
            arg->member.name = m->name;
        }
        zan_ast_list_push(&call->call.args, arg, g->arena);
    }

    LLVMValueRef result = emit_expr(g, call, locals);
    /* 接收者所有权转移与生命周期管理 */
    LLVMValueRef recv_now = LLVMBuildLoad2(g->builder, rtll, rslot,
                                           "with.recv.load");
    emit_release_owned_call_temp(g, expr->with_expr.expr, recv_now, locals);
    locals->count = mark;
    return result;
}

/* 协程帧结果槽为 64 位宽：await 统一加载 64 位槽值并进行反向类型转换 */
static LLVMValueRef coerce_await_result(zan_irgen_t *g, zan_ast_node_t *expr,
        LLVMValueRef res, local_scope_t *locals) {
    zan_type_t *rt = concretize(g, infer_expr_type(g, expr->await_expr.expr, locals));
    if (rt && rt->type_arg_count > 0) {
        if (rt->kind == TYPE_TASK ||
            (rt->name.str && ((rt->name.len >= 4 && memcmp(rt->name.str, "Task", 4) == 0) ||
                             (rt->name.len >= 9 && memcmp(rt->name.str, "ValueTask", 9) == 0))) ||
            (rt->sym &&
             ((rt->sym->name.len == 4 && memcmp(rt->sym->name.str, "Task", 4) == 0) ||
              (rt->sym->name.len == 9 && memcmp(rt->sym->name.str, "ValueTask", 9) == 0)))) {
            rt = concretize(g, rt->type_args[0]);
        }
    }
    return coerce_from_frame_result(g, res, rt);
}

static zan_symbol_t *direct_extern_method(zan_irgen_t *g, zan_ast_node_t *expr) {
    if (!expr || expr->kind != AST_CALL || !expr->call.callee) return NULL;
    zan_symbol_t *sym = NULL;
    if (expr->call.callee->kind == AST_IDENTIFIER) {
        if (g->current_type_sym)
            sym = get_method_sym(g->current_type_sym,
                                 expr->call.callee->ident.name);
        if (!sym)
            sym = zan_binder_lookup(g->binder, expr->call.callee->ident.name);
    } else if (expr->call.callee->kind == AST_MEMBER_ACCESS &&
               expr->call.callee->member.object->kind == AST_IDENTIFIER) {
        zan_symbol_t *cls = zan_binder_lookup(g->binder,
            expr->call.callee->member.object->ident.name);
        if (cls) sym = get_method_sym(cls, expr->call.callee->member.name);
    }
    if (!sym || !sym->decl || sym->decl->kind != AST_METHOD_DECL) return NULL;
    if (!zan_ast_method_extern_lib(sym->decl).str &&
        !(sym->decl->method_decl.modifiers & MOD_EXTERN))
        return NULL;
    return sym;
}

static bool blocking_integer_type(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BOOL:
    case TYPE_BYTE:
    case TYPE_SHORT:
    case TYPE_INT:
    case TYPE_LONG:
    case TYPE_SBYTE:
    case TYPE_USHORT:
    case TYPE_UINT:
    case TYPE_ULONG:
    case TYPE_CHAR:
    case TYPE_NINT:
    case TYPE_ENUM:
        return true;
    default:
        return false;
    }
}

static LLVMValueRef blocking_arg_i64(zan_irgen_t *g, LLVMValueRef v,
                                     zan_type_t *type) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMIntegerTypeKind)
        return LLVMBuildPtrToInt(g->builder, v, i64, "blocking.arg");
    if (LLVMGetIntTypeWidth(LLVMTypeOf(v)) == 64) return v;
    if (type && (type->kind == TYPE_BOOL || type->kind == TYPE_BYTE ||
                 type->kind == TYPE_USHORT || type->kind == TYPE_UINT ||
                 type->kind == TYPE_ULONG || type->kind == TYPE_CHAR))
        return LLVMBuildZExt(g->builder, v, i64, "blocking.arg");
    return LLVMBuildSExt(g->builder, v, i64, "blocking.arg");
}

/* 原生异步挂起原语发射 */
static LLVMValueRef emit_await_blocking_extern(zan_irgen_t *g,
        zan_ast_node_t *expr, local_scope_t *locals, zan_symbol_t *sym) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    zan_ast_node_t *call = expr->await_expr.expr;
    zan_ast_node_t *decl = sym->decl;
    int argc = call->call.args.count;
    zan_type_t *ret = decl->method_decl.return_type
        ? resolve_type_ctx(g, decl->method_decl.return_type)
        : g->binder->type_void;

    if (strstr(g->target_triple, "wasm") != NULL) {
        zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
            "await native extern calls are not supported on wasm32: "
            "the blocking runtime requires native worker threads");
        return LLVMConstInt(i64, 0, 0);
    }

    if (!g->current_async_frame || !g->current_async_switch) {
        zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
            "await native extern calls are only supported inside an async "
            "method");
        return LLVMConstInt(i64, 0, 0);
    }
    if (argc > 4) {
        zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
            "await native extern call has %d arguments; at most four scalar "
            "arguments are supported (prepare data before suspension and "
            "await a handle-only operation)", argc);
        return LLVMConstInt(i64, 0, 0);
    }
    if (ret->kind != TYPE_VOID && ret->kind != TYPE_INT &&
        ret->kind != TYPE_LONG && ret->kind != TYPE_NINT) {
        zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
            "await native extern call returns '%s', but only int, long, nint, "
            "or void can cross a suspension safely; return a scalar handle "
            "instead", ret->name.str ? ret->name.str : "unsupported");
        return LLVMConstInt(i64, 0, 0);
    }

    for (int k = 0; k < argc; k++) {
        zan_ast_node_t *p = decl->method_decl.params.items[k];
        zan_type_t *pt = (p && p->kind == AST_PARAM)
            ? resolve_type_ctx(g, p->param.type) : NULL;
        if (!blocking_integer_type(pt)) {
            zan_diag_emit(g->diag, DIAG_ERROR, call->call.args.items[k]->loc,
                "await native extern argument %d has type '%s'; only integer, "
                "nint, bool, and enum scalars are safe across suspension "
                "(prepare/bind managed data first, then await a handle-only "
                "operation)", k + 1, pt && pt->name.str ? pt->name.str : "unsupported");
            return LLVMConstInt(i64, 0, 0);
        }
    }

    int fi = irgen_find_function(g, sym);
    LLVMValueRef native = fi >= 0 ? g->functions[fi].fn : NULL;
    if (!native) {
        zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
            "cannot lower await native extern call: external function is not "
            "available in the generated module");
        return LLVMConstInt(i64, 0, 0);
    }

    LLVMValueRef args[4] = {
        LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0),
        LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0)
    };
    for (int k = 0; k < argc; k++) {
        zan_ast_node_t *p = decl->method_decl.params.items[k];
        zan_type_t *pt = resolve_type_ctx(g, p->param.type);
        args[k] = blocking_arg_i64(g,
            emit_arg_typed(g, call->call.args.items[k], pt, locals), pt);
    }

    /*
     * The socket-async flag selects rt_io.o, which also owns the generic
     * blocking queue and reactor wake-up path used by this await.
     */
    g->uses_socket_async = true;
    int state = g->current_async_next_state++;
    LLVMValueRef frame = g->current_async_frame;
    LLVMTypeRef frame_type = g->current_async_frame_type;
    LLVMValueRef frame_i8 = LLVMBuildBitCast(g->builder, frame, i8ptr,
                                             "blocking.frame");
    LLVMValueRef out = NULL;
    if (ret->kind != TYPE_VOID) {
        out = get_async_result_ptr(g);
    } else {
        out = LLVMConstNull(LLVMPointerType(i64, 0));
    }
    LLVMValueRef fnptr = LLVMBuildBitCast(g->builder, native, i8ptr,
                                          "blocking.fn");
    zan_store_fit(g, LLVMConstInt(i32, (unsigned)state, 0), get_async_state_ptr(g));
    LLVMValueRef rt_args[] = {
        fnptr, LLVMConstInt(i32, (unsigned)argc, 0),
        args[0], args[1], args[2], args[3],
        frame_i8, g->current_async_resume_fn, out
    };
    zan_call2(g->builder, g->rt_blocking_co_type, g->rt_blocking_co,
              rt_args, 9, "");
    LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

    LLVMBasicBlockRef resume = LLVMAppendBasicBlockInContext(g->ctx,
        g->current_async_resume_fn, "co.blocking.resume");
    LLVMAddCase(g->current_async_switch,
        LLVMConstInt(i32, (unsigned)state, 0), resume);
    LLVMPositionBuilderAtEnd(g->builder, resume);
    if (ret->kind == TYPE_VOID) return LLVMConstInt(i64, 0, 0);
    LLVMValueRef raw = LLVMBuildLoad2(g->builder, i64, get_async_result_ptr(g), "blocking.raw");
    return coerce_await_result(g, expr, raw, locals);
}

static LLVMValueRef emit_expr_await_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* await Task.Yield(): 协作式让出控制权并将当前协程帧重新入队 */
        if (is_call_to(expr->await_expr.expr, "Task", "Yield") &&
            expr->await_expr.expr->call.args.count == 0) {
            if (g->current_async_frame && g->current_async_switch) {
                int k = g->current_async_next_state++;
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                LLVMBuildBr(g->builder, get_async_requeue_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                return LLVMConstInt(di64, 0, 0);
            }
            return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
        }

        /* await Task.Delay(ms): 基于定时器的挂起（复用当前帧，不分配子帧） */
        if (is_call_to(expr->await_expr.expr, "Task", "Delay") &&
            expr->await_expr.expr->call.args.count == 1) {
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef ms = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
            if (LLVMTypeOf(ms) != di64) {
                ms = LLVMBuildIntCast2(g->builder, ms, di64, 1, "ms64");
            }
            if (g->current_async_frame && g->current_async_switch) {
                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_co_delay_type, g->rt_co_delay,
                    (LLVMValueRef[]){ ms, self_i8, g->current_async_resume_fn }, 3, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                return LLVMConstInt(di64, 0, 0);
            }
            /* 顶层非异步上下文：同步休眠等待 */
            LLVMValueRef ms32 = LLVMBuildTrunc(g->builder, ms, di32, "ms32");
            if (g->target_is_windows) {
                LLVMValueRef fn_sleep = LLVMGetNamedFunction(g->mod, "Sleep");
                if (fn_sleep) {
                    LLVMTypeRef sl_args[] = { di32 };
                    LLVMTypeRef sl_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), sl_args, 1, 0);
                    zan_call2(g->builder, sl_ty, fn_sleep, (LLVMValueRef[]){ ms32 }, 1, "");
                }
            } else {
                LLVMValueRef fn_poll = LLVMGetNamedFunction(g->mod, "poll");
                if (fn_poll) {
                    LLVMTypeRef pl_args[] = { di8ptr, di64, di32 };
                    LLVMTypeRef pl_ty = LLVMFunctionType(di32, pl_args, 3, 0);
                    zan_call2(g->builder, pl_ty, fn_poll,
                        (LLVMValueRef[]){ LLVMConstNull(di8ptr), LLVMConstInt(di64, 0, 0), ms32 }, 3, "");
                }
            }
            return LLVMConstInt(di64, 0, 0);
        }

        /* await Gate.Park(handle): 事件驱动协程等待原语 */
        if (is_call_to(expr->await_expr.expr, "Gate", "Park") &&
            expr->await_expr.expr->call.args.count == 1) {
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            if (!(g->current_async_frame && g->current_async_switch)) {
                zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                    "await Gate.Park is only supported inside an async method");
                return LLVMConstInt(di64, 0, 0);
            }
            LLVMValueRef handle = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
            if (LLVMTypeOf(handle) != di64) {
                handle = LLVMBuildIntCast2(g->builder, handle, di64, 1, "gate64");
            }
            /*
             * the gate runtime rides in the socket-async reactor object; force
             * it to be linked whenever a program parks on a gate.
             */
            g->uses_socket_async = true;
            LLVMTypeRef gate_park_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                (LLVMTypeRef[]){ di64, di8ptr, g->co_step_ptr }, 3, 0);
            LLVMValueRef gate_park = LLVMGetNamedFunction(g->mod, "zan_gate_park");
            if (!gate_park) {
                gate_park = LLVMAddFunction(g->mod, "zan_gate_park", gate_park_type);
            }
            int k = g->current_async_next_state++;
            LLVMValueRef self_i8 = get_async_self_i8(g);
            zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
            zan_call2(g->builder, gate_park_type, gate_park,
                (LLVMValueRef[]){ handle, self_i8, g->current_async_resume_fn }, 3, "");
            LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

            LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                g->current_async_resume_fn, "co.resume");
            LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
            LLVMPositionBuilderAtEnd(g->builder, rk);
            return LLVMConstInt(di64, 0, 0);
        }

        /* await Task.JoinWait(entry): 事件驱动汇合挂起 */
        if (is_call_to(expr->await_expr.expr, "Task", "JoinWait") &&
            expr->await_expr.expr->call.args.count == 1) {
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef entry = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
            if (LLVMTypeOf(entry) != di64)
                entry = LLVMBuildIntCast2(g->builder, entry, di64, 1, "join64");
            LLVMTypeRef jw_type = LLVMFunctionType(di32,
                (LLVMTypeRef[]){ di64, di8ptr, g->co_step_ptr }, 3, 0);
            LLVMValueRef jw = LLVMGetNamedFunction(g->mod, "zan_join_wait2");
            if (!jw) jw = LLVMAddFunction(g->mod, "zan_join_wait2", jw_type);
            if (g->current_async_frame && g->current_async_switch) {
                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                LLVMValueRef r = zan_call2(g->builder, jw_type, jw,
                    (LLVMValueRef[]){ entry, self_i8, g->current_async_resume_fn },
                    3, "join.wait");
                /* stash the wait result where resume-k can find it */
                LLVMBuildStore(g->builder,
                    LLVMBuildZExt(g->builder, r, di64, "join.r"),
                    get_async_result_ptr(g));
                LLVMValueRef must_suspend = zan_icmp(g->builder, LLVMIntEQ, r,
                    LLVMConstInt(di32, 0, 0), "join.susp");
                LLVMValueRef fn = g->current_fn;
                LLVMBasicBlockRef susp_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "join.park");
                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMBuildCondBr(g->builder, must_suspend, susp_bb, rk);
                LLVMPositionBuilderAtEnd(g->builder, susp_bb);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                /* no self-ready: Delay shape — the untrack hook readies us */
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                return LLVMBuildLoad2(g->builder, di64, get_async_result_ptr(g), "join.r2");
            }
            /* not inside a suspendable frame: probe-only wait */
            LLVMValueRef r2 = zan_call2(g->builder, jw_type, jw,
                (LLVMValueRef[]){ entry, LLVMConstNull(di8ptr),
                                  LLVMConstNull(g->co_step_ptr) }, 3, "join.wait.root");
            return LLVMBuildZExt(g->builder, r2, di64, "join.r");
        }

        /* 套接字就绪态异步等待（Reactor 反应堆通知） */
        {
            bool is_read  = is_call_to(expr->await_expr.expr, "Socket", "ReadReady");
            bool is_write = is_call_to(expr->await_expr.expr, "Socket", "WriteReady");
            if ((is_read || is_write) && expr->await_expr.expr->call.args.count == 1) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "await Socket.%s is only supported inside an async method",
                        is_read ? "ReadReady" : "WriteReady");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef fd = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(fd) != di64) {
                    fd = LLVMBuildIntCast2(g->builder, fd, di64, 1, "fd64");
                }
                /* ZAN_IO_READ = 1, ZAN_IO_WRITE = 2 (see rt_io.h) */
                LLVMValueRef interest = LLVMConstInt(di32, is_read ? 1 : 2, 0);
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_io_wait_co_type, g->rt_io_wait_co,
                    (LLVMValueRef[]){ fd, interest, self_i8, g->current_async_resume_fn }, 4, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                return LLVMConstInt(di64, 0, 0);
            }
        }

        /* Windows IOCP 重叠接收异步等待 */
        {
            if (is_call_to(expr->await_expr.expr, "Socket", "RecvOv") &&
                expr->await_expr.expr->call.args.count == 3) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "await Socket.RecvOv is only supported inside an async method");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef fd = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(fd) != di64)
                    fd = LLVMBuildIntCast2(g->builder, fd, di64, 1, "fd64");
                LLVMValueRef buf = emit_expr(g, expr->await_expr.expr->call.args.items[1], locals);
                if (LLVMTypeOf(buf) != di8ptr)
                    buf = LLVMBuildBitCast(g->builder, buf, di8ptr, "recvbuf");
                LLVMValueRef len = emit_expr(g, expr->await_expr.expr->call.args.items[2], locals);
                if (LLVMTypeOf(len) != di32)
                    len = LLVMBuildIntCast2(g->builder, len, di32, 1, "len32");
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                /* &self.result — the reactor stores the recv byte count here. */
                LLVMValueRef out_n = get_async_result_ptr(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_io_recv_co_type, g->rt_io_recv_co,
                    (LLVMValueRef[]){ fd, buf, len, self_i8,
                        g->current_async_resume_fn, out_n }, 6, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                return LLVMBuildLoad2(g->builder, di64, get_async_result_ptr(g), "recvn");
            }
        }

        /* 带超时的 Windows 套接字重叠接收挂起 */
        {
            if (is_call_to(expr->await_expr.expr, "Socket", "RecvToOv") &&
                expr->await_expr.expr->call.args.count == 4) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "await Socket.RecvToOv is only supported inside an async method");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef fd = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(fd) != di64)
                    fd = LLVMBuildIntCast2(g->builder, fd, di64, 1, "fd64");
                LLVMValueRef buf = emit_expr(g, expr->await_expr.expr->call.args.items[1], locals);
                if (LLVMTypeOf(buf) != di8ptr)
                    buf = LLVMBuildBitCast(g->builder, buf, di8ptr, "recvbuf");
                LLVMValueRef len = emit_expr(g, expr->await_expr.expr->call.args.items[2], locals);
                if (LLVMTypeOf(len) != di32)
                    len = LLVMBuildIntCast2(g->builder, len, di32, 1, "len32");
                LLVMValueRef tmo = emit_expr(g, expr->await_expr.expr->call.args.items[3], locals);
                if (LLVMTypeOf(tmo) != di64)
                    tmo = LLVMBuildIntCast2(g->builder, tmo, di64, 1, "tmo64");
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                /* &self.result — the reactor stores the byte count or -1 here. */
                LLVMValueRef out_n = get_async_result_ptr(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_io_recv_to_co_type, g->rt_io_recv_to_co,
                    (LLVMValueRef[]){ fd, buf, len, tmo, self_i8,
                        g->current_async_resume_fn, out_n }, 7, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                return LLVMBuildLoad2(g->builder, di64, get_async_result_ptr(g), "recvton");
            }
        }

        /* Windows AcceptEx 异步接收连接完成等待 */
        {
            if (is_call_to(expr->await_expr.expr, "Socket", "AcceptOv") &&
                expr->await_expr.expr->call.args.count == 1) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "await Socket.AcceptOv is only supported inside an async method");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef fd = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(fd) != di64)
                    fd = LLVMBuildIntCast2(g->builder, fd, di64, 1, "fd64");
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                LLVMValueRef out_fd = get_async_result_ptr(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_io_accept_co_type,
                    g->rt_io_accept_co, (LLVMValueRef[]){ fd, self_i8,
                        g->current_async_resume_fn, out_fd }, 4, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch,
                    LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                return LLVMBuildLoad2(g->builder, di64, get_async_result_ptr(g), "acceptfd");
            }
        }

        /* 异步 DNS 解析等待 */
        {
            if (is_call_to(expr->await_expr.expr, "Socket", "ResolveAsync") &&
                expr->await_expr.expr->call.args.count == 1) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "await Socket.ResolveAsync is only supported inside an async method");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef host = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(host) != di8ptr)
                    host = LLVMBuildBitCast(g->builder, host, di8ptr, "dns.host");
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                /* 套接字结果槽位指针绑定至反应堆事件 */
                LLVMValueRef res_gep = get_async_result_ptr(g);
                LLVMValueRef out32 = LLVMBuildBitCast(g->builder, res_gep,
                    LLVMPointerType(di32, 0), "self.dnsout");
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_io_resolve_co_type,
                    g->rt_io_resolve_co, (LLVMValueRef[]){ host, self_i8,
                        g->current_async_resume_fn, out32 }, 4, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch,
                    LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                LLVMValueRef res_slot = LLVMBuildBitCast(g->builder,
                    get_async_result_ptr(g),
                    LLVMPointerType(di32, 0), "self.dnsout2");
                return LLVMBuildLoad2(g->builder, di32, res_slot, "dnsaddr");
            }
        }

        /* 异步套接字地址解析挂起 */
        {
            if (is_call_to(expr->await_expr.expr, "Socket", "ResolveSockAddr") &&
                expr->await_expr.expr->call.args.count == 4) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "await Socket.ResolveSockAddr is only supported inside an async method");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef name = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(name) != di8ptr)
                    name = LLVMBuildBitCast(g->builder, name, di8ptr, "sa.name");
                LLVMValueRef port = emit_expr(g, expr->await_expr.expr->call.args.items[1], locals);
                if (LLVMTypeOf(port) != di32)
                    port = LLVMBuildIntCast2(g->builder, port, di32, 1, "sa.port");
                LLVMValueRef buf = emit_expr(g, expr->await_expr.expr->call.args.items[2], locals);
                if (LLVMTypeOf(buf) != di8ptr)
                    buf = LLVMBuildBitCast(g->builder, buf, di8ptr, "sa.buf");
                LLVMValueRef cap = emit_expr(g, expr->await_expr.expr->call.args.items[3], locals);
                if (LLVMTypeOf(cap) != di32)
                    cap = LLVMBuildIntCast2(g->builder, cap, di32, 1, "sa.cap");
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                /*
                 * &self.result viewed as i32* — the reactor stores the
                 * sockaddr length here before re-readying the frame.
                 */
                LLVMValueRef res_gep = get_async_result_ptr(g);
                LLVMValueRef out32 = LLVMBuildBitCast(g->builder, res_gep,
                    LLVMPointerType(di32, 0), "self.saout");
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_io_resolve_sa_co_type,
                    g->rt_io_resolve_sa_co, (LLVMValueRef[]){ name, port, buf,
                        cap, self_i8, g->current_async_resume_fn, out32 }, 7, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch,
                    LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                LLVMValueRef res_slot = LLVMBuildBitCast(g->builder,
                    get_async_result_ptr(g),
                    LLVMPointerType(di32, 0), "self.saout2");
                return LLVMBuildLoad2(g->builder, di32, res_slot, "salen");
            }
        }

        {
            zan_symbol_t *native = direct_extern_method(g, expr->await_expr.expr);
            if (native)
                return emit_await_blocking_extern(g, expr, locals, native);
        }

        /* await <call>: 等待异步方法子协程执行完成 */
        /* await 挂起点临时栈深度记录，供异常展开清理 */
        LLVMValueRef aw_tmp_mark = LLVMBuildLoad2(g->builder,
            LLVMInt32TypeInContext(g->ctx), get_eh_tmp_top_global(g),
            "aw.tmpmark");
        LLVMValueRef sub = emit_expr(g, expr->await_expr.expr, locals);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef hdr = g->co_header_type;

        LLVMValueRef sub_resume = NULL;
        if (LLVMIsACallInst(sub)) {
            /* 直接异步调用：已知被调用函数入口，直连 ramp 与 resume 符号 */
            LLVMValueRef callee = LLVMGetCalledValue(sub);
            LLVMValueRef callee_fn = callee ? LLVMIsAFunction(callee) : NULL;
            if (callee_fn) {
                size_t nl = 0;
                const char *cn = LLVMGetValueName2(callee_fn, &nl);
                if (cn && nl > 0 && nl < 240) {
                    char rn[256];
                    memcpy(rn, cn, nl);
                    memcpy(rn + nl, "$resume", 8); /* includes NUL */
                    sub_resume = LLVMGetNamedFunction(g->mod, rn);
                }
            }
        }

        /* 间接异步调用：通过虚方法表或委托分发 resume 入口 */
        if (!sub_resume &&
            LLVMGetTypeKind(LLVMTypeOf(sub)) == LLVMPointerTypeKind) {
            LLVMValueRef sub_hdr = (LLVMTypeOf(sub) == i8ptr) ? sub :
                LLVMBuildBitCast(g->builder, sub, i8ptr, "sub.hdr");
            LLVMValueRef ssp = LLVMBuildStructGEP2(g->builder, hdr, sub_hdr,
                ASYNC_FRAME_SELF_STEP, "sub.selfstep.p");
            sub_resume = LLVMBuildLoad2(g->builder, g->co_step_ptr, ssp, "sub.selfstep");
        }

        if (sub_resume && LLVMGetTypeKind(LLVMTypeOf(sub)) == LLVMPointerTypeKind) {
            LLVMValueRef sub_i8 = (LLVMTypeOf(sub) == i8ptr) ? sub :
                LLVMBuildBitCast(g->builder, sub, i8ptr, "sub");

            if (g->current_async_frame && g->current_async_switch) {
                int k = g->current_async_next_state++;
                LLVMTypeRef ptr_int_ty = g->target_is_wasm ? i32 : i64;
                LLVMValueRef fn = g->current_async_resume_fn;

                LLVMBasicBlockRef prep_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "await.prep");
                LLVMBasicBlockRef suspend_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "await.suspend");
                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.resume");
                LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "await.cont");

                LLVMAddCase(g->current_async_switch, LLVMConstInt(i32, (unsigned)k, 0), rk);

                /* 快路径探测：子协程若已同步完成则直接内联提取结果，避免调度开销 */
                LLVMValueRef done_p = LLVMBuildStructGEP2(g->builder, hdr, sub_i8,
                    ASYNC_FRAME_DONE, "sub.done.p");
                LLVMValueRef is_done = LLVMBuildLoad2(g->builder, i32, done_p, "sub.is_done");
                LLVMSetOrdering(is_done, LLVMAtomicOrderingAcquire);
                LLVMValueRef fast_cond = zan_icmp(g->builder, LLVMIntNE, is_done,
                    LLVMConstInt(i32, 0, 0), "sub.already_done");
                LLVMBasicBlockRef probe_bb = LLVMGetInsertBlock(g->builder);
                LLVMBuildCondBr(g->builder, fast_cond, cont_bb, prep_bb);

                /* ---- await.prep: atomic handshake with sub ---- */
                LLVMPositionBuilderAtEnd(g->builder, prep_bb);
                zan_store_fit(g, g->current_async_resume_fn,
                    LLVMBuildStructGEP2(g->builder, hdr, sub_i8, ASYNC_FRAME_AWAITER_STEP, "sub.aws"));
                LLVMValueRef aw_ptr = LLVMBuildStructGEP2(g->builder, hdr, sub_i8,
                    ASYNC_FRAME_AWAITER, "sub.aw");
                LLVMTypeRef aw_target_pty = LLVMPointerType(ptr_int_ty, 0);
                LLVMValueRef aw_iptr = (LLVMTypeOf(aw_ptr) == aw_target_pty) ? aw_ptr :
                    LLVMBuildBitCast(g->builder, aw_ptr, aw_target_pty, "sub.aw.iptr");
                LLVMValueRef self_int = get_async_self_int(g, ptr_int_ty);
                LLVMValueRef cas_res = LLVMBuildAtomicCmpXchg(g->builder, aw_iptr,
                    LLVMConstInt(ptr_int_ty, 0, 0), self_int,
                    LLVMAtomicOrderingSequentiallyConsistent,
                    LLVMAtomicOrderingSequentiallyConsistent, 0);
                LLVMValueRef won = LLVMBuildExtractValue(g->builder, cas_res, 1, "cas.won");
                LLVMBuildCondBr(g->builder, won, suspend_bb, cont_bb);

                /* ---- await.suspend: caller suspends ---- */
                LLVMPositionBuilderAtEnd(g->builder, suspend_bb);
                LLVMValueRef sub_slot = get_async_sub_slot_ptr(g);
                zan_store_fit(g, sub_i8, sub_slot);
                zan_store_fit(g, sub_i8, get_async_child_ptr(g));
                zan_store_fit(g, LLVMConstInt(i32, (unsigned)k, 0), get_async_state_ptr(g));
                LLVMValueRef sched_args[] = { sub_i8, sub_resume };
                zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                /* ---- co.resume (rk): re-entered by driver once sub completes ---- */
                LLVMPositionBuilderAtEnd(g->builder, rk);
                LLVMValueRef sub_rl = LLVMBuildLoad2(g->builder, i8ptr, sub_slot, "sub.rl");
                LLVMBuildBr(g->builder, cont_bb);

                LLVMPositionBuilderAtEnd(g->builder, cont_bb);
                LLVMValueRef completed_sub = LLVMBuildPhi(g->builder, i8ptr, "sub.completed");
                LLVMValueRef sub_vals[] = { sub_i8, sub_i8, sub_rl };
                LLVMBasicBlockRef sub_bbs[] = { probe_bb, prep_bb, rk };
                LLVMAddIncoming(completed_sub, sub_vals, sub_bbs, 3);
                emit_async_check_sub_exc(g, completed_sub, NULL);
                LLVMValueRef rptr = LLVMBuildStructGEP2(g->builder, hdr, completed_sub,
                    ASYNC_FRAME_RESULT, "sub.result");
                LLVMValueRef awres = LLVMBuildLoad2(g->builder, i64, rptr, "awres");
                /* Aggregate results point into the child frame: decode before release. */
                LLVMValueRef val = coerce_await_result(g, expr, awres, locals);
                zan_emit_frame_free(g, completed_sub);
                if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMVoidTypeKind)
                    return LLVMConstInt(i64, 0, 0);
                return val;
            }

            /* 根驱动调度循环（非异步调用方）：驱动调度器推进协程执行至完成 */
            LLVMValueRef done_p = LLVMBuildStructGEP2(g->builder, hdr, sub_i8,
                ASYNC_FRAME_DONE, "sub.done.p");
            /*
             * Acquire probe: pairs with the frame's release DONE store; the
             * pump loop in zan_co_sched_run_until re-probes with acquire.
             */
            LLVMValueRef is_done = LLVMBuildLoad2(g->builder, i32, done_p, "sub.is_done");
            LLVMSetOrdering(is_done, LLVMAtomicOrderingAcquire);
            LLVMValueRef need_pump = zan_icmp(g->builder, LLVMIntEQ, is_done,
                LLVMConstInt(i32, 0, 0), "sub.need_pump");
            LLVMValueRef fn = g->current_fn;
            LLVMBasicBlockRef pump_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "await.pump");
            LLVMBasicBlockRef fin_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "await.fin");
            LLVMBuildCondBr(g->builder, need_pump, pump_bb, fin_bb);

            LLVMPositionBuilderAtEnd(g->builder, pump_bb);
            LLVMValueRef sched_args[] = { sub_i8, sub_resume };
            zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
            /* 单协程调度循环：轮询驱动直至当前协程执行完成 */
            zan_call2(g->builder, g->rt_co_sched_run_until_type,
                g->rt_co_sched_run_until, (LLVMValueRef[]){ done_p }, 1, "");
            LLVMBuildBr(g->builder, fin_bb);

            LLVMPositionBuilderAtEnd(g->builder, fin_bb);
            emit_async_check_sub_exc(g, sub_i8, aw_tmp_mark);
            LLVMValueRef rptr = LLVMBuildStructGEP2(g->builder, hdr, sub_i8,
                ASYNC_FRAME_RESULT, "sub.result");
            LLVMValueRef awres = LLVMBuildLoad2(g->builder, i64, rptr, "awres");
            LLVMValueRef val = coerce_await_result(g, expr, awres, locals);
            zan_emit_frame_free(g, sub_i8);
            return val;
        }

        /* Fallback: awaiting a legacy Task struct (busy-wait) or a plain value. */
        if (LLVMGetTypeKind(LLVMTypeOf(sub)) == LLVMPointerTypeKind) {
            LLVMValueRef task_ptr = LLVMBuildBitCast(g->builder, sub,
                LLVMPointerType(g->task_struct_type, 0), "taskp");
            LLVMBasicBlockRef poll_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "aw.poll");
            LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "aw.done");
            LLVMBuildBr(g->builder, poll_bb);
            LLVMPositionBuilderAtEnd(g->builder, poll_bb);
            LLVMValueRef comp_ptr = LLVMBuildStructGEP2(g->builder, g->task_struct_type, task_ptr, 0, "comp");
            LLVMValueRef comp = LLVMBuildLoad2(g->builder, i64, comp_ptr, "cv");
            LLVMValueRef is_done = zan_icmp(g->builder, LLVMIntNE, comp, LLVMConstInt(i64, 0, 0), "done");
            LLVMBuildCondBr(g->builder, is_done, done_bb, poll_bb);
            LLVMPositionBuilderAtEnd(g->builder, done_bb);
            LLVMValueRef res_ptr = LLVMBuildStructGEP2(g->builder, g->task_struct_type, task_ptr, 1, "resp");
            return coerce_await_result(g, expr,
                LLVMBuildLoad2(g->builder, i64, res_ptr, "awres"), locals);
        }
        return sub;
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

/* 用户自定义隐式/显式类型转换运算符降解 */
static zan_symbol_t *find_conversion_method(zan_irgen_t *g, zan_symbol_t *sym,
                                            zan_type_t *from_type,
                                            zan_type_t *to_type,
                                            const char *op_name) {
    if (!sym) return NULL;
    size_t olen = strlen(op_name);
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (!m || m->kind != SYM_METHOD || !(m->modifiers & MOD_STATIC)) continue;
        if (!m->decl || m->decl->kind != AST_METHOD_DECL) continue;
        if ((size_t)m->name.len != olen ||
            memcmp(m->name.str, op_name, olen) != 0) continue;
        zan_type_t *rt = zan_binder_resolve_type(g->binder,
            m->decl->method_decl.return_type);
        if (!rt || !types_concrete_equal(rt, to_type)) continue;
        zan_ast_list_t *ps = &m->decl->method_decl.params;
        if (ps->count != 1 || !ps->items[0] || ps->items[0]->kind != AST_PARAM)
            continue;
        zan_type_t *pt = zan_binder_resolve_type(g->binder,
            ps->items[0]->param.type);
        if (pt && types_concrete_equal(pt, from_type)) return m;
    }
    return NULL;
}

/* 合成 new 节点作为所有权标记 */
static zan_ast_node_t *owned_rhs_marker(zan_irgen_t *g, zan_loc_t loc) {
    return zan_ast_new(g->arena, AST_NEW_EXPR, loc);
}

static zan_symbol_t *find_user_conversion(zan_irgen_t *g, zan_type_t *from_type,
                                          zan_type_t *to_type,
                                          const char *op_name) {
    if (!from_type || !to_type) return NULL;
    if (from_type->kind == TYPE_CLASS || from_type->kind == TYPE_STRUCT) {
        zan_symbol_t *m = find_conversion_method(g, from_type->sym, from_type,
                                                 to_type, op_name);
        if (m) return m;
    }
    if (to_type->kind == TYPE_CLASS || to_type->kind == TYPE_STRUCT) {
        zan_symbol_t *m = find_conversion_method(g, to_type->sym, from_type,
                                                 to_type, op_name);
        if (m) return m;
    }
    return NULL;
}

static LLVMValueRef emit_user_conversion(zan_irgen_t *g, zan_type_t *from_type,
                                         zan_type_t *to_type, const char *op_name,
                                         LLVMValueRef val, zan_ast_node_t *val_expr,
                                         local_scope_t *locals) {
    zan_symbol_t *op = find_user_conversion(g, from_type, to_type, op_name);
    if (!op) return val;
    int fi = irgen_find_function(g, op);
    if (fi < 0) return val;
    LLVMTypeRef mft = g->functions[fi].fn_type;
    LLVMValueRef mfn = route_generic_method(g, from_type, op,
        g->functions[fi].fn, mft, &mft);
    LLVMValueRef arg = val;
    /*
     * Fit the source value to the declared parameter type (a struct source
     * passed by value vs. the method's `T1 v` by value).
     */
    zan_type_t *p0 = method_param_type_at(g, op, 0, val_expr, val_expr, locals);
    if (p0) {
        LLVMTypeRef p0t = map_type(g, p0);
        if (LLVMTypeOf(arg) != p0t) arg = coerce_int_to(g, arg, p0t);
    }
    return zan_call2(g->builder, mft, mfn, &arg, 1, "uc");
}

/* 委托值形态：普通纯函数指针或带环境上下文的闭包记录 */
static bool target_is_wasm32(zan_irgen_t *g) {
    return g->target_triple[0] &&
           strncmp(g->target_triple, "wasm32", 6) == 0;
}

/* (nint)SomeMethod 原生回调函数指针转换 */
static LLVMValueRef emit_raw_fn_for_cb_cast(zan_irgen_t *g, zan_ast_node_t *e,
                                            local_scope_t *locals) {
    zan_symbol_t *msym = NULL;
    if (e->kind == AST_IDENTIFIER && g->current_type_sym) {
        msym = get_method_sym(g->current_type_sym, e->ident.name);
    } else if (e->kind == AST_MEMBER_ACCESS &&
               e->member.object->kind == AST_IDENTIFIER &&
               !local_find(locals, e->member.object->ident.name)) {
        zan_symbol_t *cs = zan_binder_lookup(g->binder, e->member.object->ident.name);
        if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT))
            msym = get_method_sym(cs, e->member.name);
    }
    if (msym && msym->decl && msym->decl->kind == AST_METHOD_DECL &&
        (msym->decl->method_decl.modifiers & MOD_STATIC) != 0) {
        for (int fi = irgen_find_function(g, msym); fi >= 0; fi = -1)
            if (g->functions[fi].sym == msym)
                return g->functions[fi].fn;
    }
    if (e->kind == AST_IDENTIFIER && !local_find(locals, e->ident.name)) {
        char nbuf[256];
        size_t nl = e->ident.name.len < 255 ? e->ident.name.len : 255;
        memcpy(nbuf, e->ident.name.str, nl);
        nbuf[nl] = '\0';
        LLVMValueRef gfn = LLVMGetNamedFunction(g->mod, nbuf);
        if (gfn && LLVMIsAFunction(gfn)) return gfn;
    }
    return NULL;
}

/* (nint)Method 回调函数指针跨语言传递给宿主 C 代码 */
static LLVMValueRef emit_wasm_cb_thunk(zan_irgen_t *g, LLVMValueRef fn) {
    char name[300];
    snprintf(name, sizeof(name), "__zan_cb_thunk.%s", LLVMGetValueName(fn));
    LLVMValueRef existing = LLVMGetNamedFunction(g->mod, name);
    if (existing && LLVMIsAFunction(existing)) return existing;
    LLVMTypeRef i32T = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64T = LLVMInt64TypeInContext(g->ctx);
    unsigned nparams = LLVMCountParams(fn);
    LLVMTypeRef *param_types = (LLVMTypeRef *)malloc((nparams > 0 ? nparams : 1) * sizeof(LLVMTypeRef));
    for (unsigned i = 0; i < nparams; i++) {
        param_types[i] = i32T;
    }
    LLVMTypeRef ret_ty = LLVMGetReturnType(LLVMGlobalGetValueType(fn));
    LLVMTypeRef thunk_ty = LLVMFunctionType(ret_ty, param_types, nparams, 0);
    free(param_types);

    LLVMValueRef thunk = LLVMAddFunction(g->mod, name, thunk_ty);
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, thunk, "entry");
    LLVMPositionBuilderAtEnd(g->builder, bb);

    LLVMTypeRef fn_ty = LLVMGlobalGetValueType(fn);
    LLVMValueRef *call_args = NULL;
    if (nparams > 0) {
        call_args = (LLVMValueRef *)malloc(nparams * sizeof(LLVMValueRef));
        for (unsigned i = 0; i < nparams; i++) {
            LLVMValueRef p = LLVMGetParam(thunk, i);
            LLVMTypeRef target_param_ty = LLVMTypeOf(LLVMGetParam(fn, i));
            if (LLVMGetTypeKind(target_param_ty) == LLVMIntegerTypeKind) {
                unsigned w = LLVMGetIntTypeWidth(target_param_ty);
                if (w == 64) {
                    call_args[i] = LLVMBuildZExt(g->builder, p, i64T, "arg64");
                } else if (w == 32) {
                    call_args[i] = p;
                } else if (w < 32) {
                    call_args[i] = LLVMBuildTrunc(g->builder, p, target_param_ty, "argtrunc");
                } else {
                    call_args[i] = LLVMBuildZExt(g->builder, p, target_param_ty, "argext");
                }
            } else if (LLVMGetTypeKind(target_param_ty) == LLVMPointerTypeKind) {
                call_args[i] = LLVMBuildIntToPtr(g->builder, p, target_param_ty, "argptr");
            } else {
                call_args[i] = p;
            }
        }
    }
    LLVMValueRef ret_val = LLVMBuildCall2(g->builder, fn_ty, fn, call_args, nparams, "");
    if (call_args) free(call_args);

    if (LLVMGetTypeKind(ret_ty) == LLVMVoidTypeKind) {
        LLVMBuildRetVoid(g->builder);
    } else {
        LLVMBuildRet(g->builder, ret_val);
    }
    LLVMPositionBuilderAtEnd(g->builder, saved_bb);
    return thunk;
}

static LLVMValueRef emit_expr_cast_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* (Type)x — explicit numeric cast honoring the target type. */
        zan_type_t *tt0 = resolve_type_ctx(g, expr->cast.type);
        /* 传递裸函数指针供外部原生代码调用 */
        if (tt0 && expr->cast.expr &&
            (expr->cast.expr->kind == AST_IDENTIFIER ||
             expr->cast.expr->kind == AST_MEMBER_ACCESS) &&
            tt0->kind != TYPE_DELEGATE) {
            LLVMTypeRef tgt0 = map_type(g, tt0);
            if (LLVMGetTypeKind(tgt0) == LLVMIntegerTypeKind) {
                LLVMValueRef raw = emit_raw_fn_for_cb_cast(g, expr->cast.expr, locals);
                if (raw) {
                    if (target_is_wasm32(g))
                        raw = emit_wasm_cb_thunk(g, raw);
                    return LLVMBuildPtrToInt(g->builder, raw, tgt0, "cast.fn");
                }
            }
        }
        LLVMValueRef val = emit_expr(g, expr->cast.expr, locals);
        zan_type_t *tt = resolve_type_ctx(g, expr->cast.type);
        LLVMTypeRef target = tt ? map_type(g, tt) : LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef src = LLVMTypeOf(val);
        /*
         * (T2)x where x's static type declares `explicit operator T2` (B12):
         * the cast calls the user-defined conversion instead of
         * reinterpreting the source as a pointer/number.
         */
        {
            zan_type_t *st = infer_expr_type(g, expr->cast.expr, locals);
            if (st && find_user_conversion(g, st, tt, "op_explicit"))
                return emit_user_conversion(g, st, tt, "op_explicit", val,
                                            expr->cast.expr, locals);
        }
        /*
         * `(int)v` on an `int?` is the explicit unwrapping conversion, and
         * `(int?)x` the wrapping one.
         */
        if (llvm_is_nullable(src) && !llvm_is_nullable(target)) {
            emit_runtime_check(g,
                LLVMBuildNot(g->builder, nullable_has_value(g, val), "nv.novalue"),
                expr->loc, "Nullable object must have a value");
            val = nullable_get_payload(g, val);
            src = LLVMTypeOf(val);
        } else if (llvm_is_nullable(target) && src != target) {
            return coerce_int_to(g, val, target);
        }
        /* 无符号整型目标转换保持 64 位寄存器表示 */
        if (tt && LLVMGetTypeKind(src) == LLVMIntegerTypeKind &&
            LLVMGetIntTypeWidth(src) < 64 &&
            (tt->kind == TYPE_UINT || tt->kind == TYPE_USHORT ||
             tt->kind == TYPE_SBYTE || tt->kind == TYPE_ULONG)) {
            val = zan_iwiden(g->builder, val, LLVMInt64TypeInContext(g->ctx));
            src = LLVMTypeOf(val);
        }
        if (tt && LLVMGetTypeKind(src) == LLVMIntegerTypeKind &&
            LLVMGetIntTypeWidth(src) == 64) {
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            switch (tt->kind) {
            case TYPE_UINT:
                return zan_and(g->builder, val,
                    LLVMConstInt(i64t, 0xFFFFFFFFull, 0), "cast.u32");
            case TYPE_USHORT:
                return zan_and(g->builder, val,
                    LLVMConstInt(i64t, 0xFFFFull, 0), "cast.u16");
            case TYPE_SBYTE: {
                LLVMValueRef sh = LLVMConstInt(i64t, 56, 0);
                LLVMValueRef up = zan_shl(g->builder, val, sh, "cast.i8.l");
                return zan_ashr(g->builder, up, sh, "cast.i8");
            }
            case TYPE_ULONG:
                return val;
            default:
                break;
            }
        }
        if (src == target) return val;
        LLVMTypeKind sk = LLVMGetTypeKind(src);
        LLVMTypeKind tk = LLVMGetTypeKind(target);
        /* 地址与委托互转：(WndProc)addr 动态转换运行时回调 */
        if (sk == LLVMIntegerTypeKind && tk == LLVMPointerTypeKind)
            return LLVMBuildIntToPtr(g->builder, val, target, "cast.p");
        if (sk == LLVMPointerTypeKind && tk == LLVMIntegerTypeKind)
            return LLVMBuildPtrToInt(g->builder, val, target, "cast.a");
        bool src_fp = (sk == LLVMDoubleTypeKind || sk == LLVMFloatTypeKind);
        bool tgt_fp = (tk == LLVMDoubleTypeKind || tk == LLVMFloatTypeKind);
        if (sk == LLVMIntegerTypeKind && tk == LLVMIntegerTypeKind) {
            unsigned sw = LLVMGetIntTypeWidth(src);
            unsigned tw = LLVMGetIntTypeWidth(target);
            if (tw < sw) return LLVMBuildTrunc(g->builder, val, target, "cast");
            if (tw > sw) return zan_iwiden(g->builder, val, target);
            return val;
        }
        if (src_fp && tk == LLVMIntegerTypeKind)
            return LLVMBuildFPToSI(g->builder, val, target, "cast");
        if (sk == LLVMIntegerTypeKind && tgt_fp)
            return LLVMBuildSIToFP(g->builder, val, target, "cast");
        if (src_fp && tgt_fp) {
            if (sk == LLVMDoubleTypeKind && tk == LLVMFloatTypeKind)
                return LLVMBuildFPTrunc(g->builder, val, target, "cast");
            if (sk == LLVMFloatTypeKind && tk == LLVMDoubleTypeKind)
                return LLVMBuildFPExt(g->builder, val, target, "cast");
        }
        return val;
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

/*
 * True when `t` is the same class as `s` or a base class of it (single
 * inheritance, so the runtime type of an `s`-typed value is always a
 * subclass of `s`).
 */
static int type_is_or_derives(zan_type_t *s, zan_type_t *t) {
    if (!s || !t) return 0;
    if (types_equal(s, t)) return 1;
    if (s->kind != TYPE_CLASS || t->kind != TYPE_CLASS) return 0;
    zan_symbol_t *cur = s->sym;
    while (cur && cur->type && cur->type->base_type &&
           cur->type->base_type->sym) {
        cur = cur->type->base_type->sym;
        if (cur == t->sym) return 1;
    }
    return 0;
}

/* 运行时 x is T 严格祖先类型继承检查 */
static LLVMValueRef emit_runtime_is_check(zan_irgen_t *g, LLVMValueRef x,
                                          const char *tname);

/* 运行时 x is T 类型检查辅助封装 */
static LLVMValueRef emit_runtime_is_check_name(zan_irgen_t *g, LLVMValueRef x,
                                               zan_type_t *tt) {
    char tbuf[320];
    const char *ns = NULL;
    int nlen = 0;
    if (tt && tt->name.str) {
        ns = tt->name.str;
        nlen = (int)tt->name.len;
    } else if (tt && tt->sym && tt->sym->name.str) {
        ns = tt->sym->name.str;
        nlen = (int)tt->sym->name.len;
    }
    if (nlen > 319) nlen = 319;
    if (nlen > 0) memcpy(tbuf, ns, (size_t)nlen);
    tbuf[nlen] = 0;
    return emit_runtime_is_check(g, x, tbuf);
}

static LLVMValueRef emit_runtime_is_check(zan_irgen_t *g, LLVMValueRef x,
                                          const char *tname) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
    if (!g->desc_hdr && !g->g_site_tynames) return LLVMConstInt(i1, 0, 0);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef false_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.f");
    LLVMBasicBlockRef true_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.t");
    LLVMBasicBlockRef merge = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.m");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.h");
    LLVMBasicBlockRef str_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.s");
    LLVMBasicBlockRef oob_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.o");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.l");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.b");
    LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.n");
    (void)cur;

    /* null object: not a T */
    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, x,
        LLVMConstNull(LLVMTypeOf(x)), "is.nul");
    LLVMBuildCondBr(g->builder, isnull, false_bb, head);

    /* 对象头部 -8 处分配点标识字（支持字符串/对象运行时类型识别） */
    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef tstr = zan_irgen_intern_string(g, tname);
    LLVMValueRef neg8 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
    LLVMValueRef sptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
        x, &neg8, 1, "is.sptr");
    LLVMValueRef site = LLVMBuildLoad2(g->builder, i64,
        LLVMBuildBitCast(g->builder, sptr, LLVMPointerType(i64, 0), "is.sp"),
        "is.site");
    LLVMValueRef isstr = zan_hdr_is_string(g, site, "is.str");
    LLVMBuildCondBr(g->builder, isstr, str_bb, oob_bb);

    /* string object: `is T` holds iff T is `string` */
    LLVMPositionBuilderAtEnd(g->builder, str_bb);
    LLVMValueRef ststr = zan_irgen_intern_string(g, "string");
    LLVMTypeRef sstrcmp_ty = LLVMFunctionType(i32,
        (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMValueRef sstrcmp_fn = get_libc_fn(g, "strcmp", sstrcmp_ty);
    LLVMValueRef scmp = zan_call2(g->builder, sstrcmp_ty, sstrcmp_fn,
        (LLVMValueRef[]){ ststr, tstr }, 2, "is.scmp");
    LLVMValueRef seq = zan_icmp(g->builder, LLVMIntEQ, scmp,
        LLVMConstInt(i32, 0, 0), "is.seq");
    LLVMBuildCondBr(g->builder, seq, true_bb, false_bb);

    /* 越界分配点索引判定为非目标类型 */
    LLVMPositionBuilderAtEnd(g->builder, oob_bb);
    LLVMValueRef oob;
    if (!g->desc_hdr) {
        LLVMValueRef bound = LLVMBuildLoad2(g->builder, i64,
            g->g_site_count, "is.bound");
        oob = zan_or(g->builder,
            zan_icmp(g->builder, LLVMIntSLT, site, LLVMConstInt(i64, 0, 0), "is.neg"),
            zan_icmp(g->builder, LLVMIntSGE, site, bound, "is.oob"),
            "is.oob2");
    } else {
        /* 描述符模式：头部字作为元数据记录指针 */
        oob = zan_or(g->builder,
            zan_or(g->builder,
                zan_icmp(g->builder, LLVMIntEQ, site, LLVMConstInt(i64, 0, 0),
                         "is.nodsc"),
                zan_icmp(g->builder, LLVMIntEQ, site,
                         LLVMConstInt(i64, ZAN_ARRAY_MAGIC, 0), "is.arrdsc"),
                "is.notdsc"),
            zan_icmp(g->builder, LLVMIntULT, site, LLVMConstInt(i64, 4096, 0),
                     "is.tinydsc"),
            "is.oob2");
    }
    LLVMBuildCondBr(g->builder, oob, false_bb, loop);

    /* walk the ancestor-name list */
    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef idx = LLVMBuildPhi(g->builder, i64, "is.i");
    LLVMValueRef list;
    if (!g->desc_hdr) {
        LLVMValueRef tbl = LLVMBuildLoad2(g->builder,
            LLVMPointerType(i8ptr, 0), g->g_site_tynames, "is.tbl");
        list = LLVMBuildLoad2(g->builder, i8ptr,
            LLVMBuildGEP2(g->builder, i8ptr, tbl, &site, 1, "is.lp"),
            "is.list");
    } else {
        /* load the record, then its tynames field (offset 8) */
        LLVMValueRef dp = LLVMBuildIntToPtr(g->builder, site,
            LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0), "is.dsc");
        LLVMValueRef tn_off = LLVMConstInt(i64, 8, 0);
        LLVMValueRef tn_p = LLVMBuildGEP2(g->builder,
            LLVMInt8TypeInContext(g->ctx), dp, &tn_off, 1, "is.tnp");
        list = LLVMBuildLoad2(g->builder, i8ptr, tn_p, "is.list");
    }
    LLVMValueRef lnull = zan_icmp(g->builder, LLVMIntEQ, list,
        LLVMConstNull(i8ptr), "is.ln");
    LLVMBuildCondBr(g->builder, lnull, false_bb, body);

    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef name_p = LLVMBuildGEP2(g->builder, i8ptr, list, &idx, 1, "is.np");
    LLVMValueRef name = LLVMBuildLoad2(g->builder, i8ptr, name_p, "is.name");
    LLVMValueRef nnull = zan_icmp(g->builder, LLVMIntEQ, name,
        LLVMConstNull(i8ptr), "is.nn");
    LLVMBuildCondBr(g->builder, nnull, false_bb, next);

    LLVMPositionBuilderAtEnd(g->builder, next);
    LLVMTypeRef strcmp_ty = LLVMFunctionType(i32,
        (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMValueRef strcmp_fn = get_libc_fn(g, "strcmp", strcmp_ty);
    LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty, strcmp_fn,
        (LLVMValueRef[]){ name, tstr }, 2, "is.cmp");
    LLVMValueRef eq = zan_icmp(g->builder, LLVMIntEQ, cmp,
        LLVMConstInt(i32, 0, 0), "is.eq");
    LLVMValueRef idx2 = zan_add(g->builder, idx, LLVMConstInt(i64, 1, 0), "is.i2");
    LLVMBuildCondBr(g->builder, eq, true_bb, loop);
    LLVMAddIncoming(idx, (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), idx2 },
                    (LLVMBasicBlockRef[]){ oob_bb, next }, 2);

    LLVMPositionBuilderAtEnd(g->builder, true_bb);
    LLVMBuildBr(g->builder, merge);
    LLVMPositionBuilderAtEnd(g->builder, false_bb);
    LLVMBuildBr(g->builder, merge);
    LLVMPositionBuilderAtEnd(g->builder, merge);
    LLVMValueRef res = LLVMBuildPhi(g->builder, i1, "is.res");
    LLVMAddIncoming(res, (LLVMValueRef[]){ LLVMConstInt(i1, 1, 0),
                                           LLVMConstInt(i1, 0, 0) },
                    (LLVMBasicBlockRef[]){ true_bb, false_bb }, 2);
    return res;
}

static LLVMValueRef emit_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals) {
    if (!expr) return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);

    if (expr->kind == AST_REF_ARG) {
        return emit_ref_arg(g, expr, locals);    }
    if (expr->kind == AST_MEMBER_ACCESS && expr->member.null_cond) {
        return emit_null_cond(g, expr, expr, locals);
    }
    if (expr->kind == AST_CALL && expr->call.callee &&
        expr->call.callee->kind == AST_MEMBER_ACCESS &&
        expr->call.callee->member.null_cond) {
        return emit_null_cond(g, expr, expr->call.callee, locals);
    }

    switch (expr->kind) {
    case AST_INT_LITERAL: {
        /* 运行时值与静态类型匹配检查 */
        int64_t v = expr->int_val;
        if (expr->lit_suffix == 0 && v > 2147483647LL && v <= 4294967295LL &&
            (expr->lit_radix == 2 || expr->lit_radix == 8 ||
             expr->lit_radix == 16))
            v = (int32_t)v;
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), (uint64_t)v, 1);
    }

    case AST_FLOAT_LITERAL:
        return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), expr->float_val);

    case AST_STRING_LITERAL:
        return emit_string_literal_rc(g, expr->str_val);

    case AST_BOOL_LITERAL:
        return LLVMConstInt(LLVMInt1TypeInContext(g->ctx), expr->bool_val ? 1 : 0, 0);

    case AST_CHAR_LITERAL:
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), (uint64_t)expr->int_val, 0);

    case AST_NULL_LITERAL:
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));

    case AST_IDENTIFIER:
        return emit_expr_identifier(g, expr, locals);

    case AST_BINARY:
        return emit_expr_binary(g, expr, locals);

    case AST_UNARY:
        return emit_expr_unary(g, expr, locals);

    case AST_ASSIGNMENT:
        return emit_expr_assignment(g, expr, locals);

    case AST_CALL:
        return promote_loaded(g, emit_expr_call(g, expr, locals),
                              infer_expr_type(g, expr, locals));

    case AST_STRING_INTERP:
        return emit_expr_string_interp(g, expr, locals);

    case AST_MEMBER_ACCESS:
        return emit_expr_member_access(g, expr, locals);

    case AST_INDEX:
        return emit_expr_index(g, expr, locals);

    case AST_QUERY_EXPR:
        return emit_expr_query_expr(g, expr, locals);

    case AST_NEW_EXPR:
        return emit_expr_new_expr(g, expr, locals);

    case AST_TUPLE_EXPR:
        return emit_expr_tuple(g, expr, locals);

    case AST_CONDITIONAL:
        return emit_expr_conditional(g, expr, locals);

    case AST_SWITCH_EXPR:
        return emit_expr_switch_expr(g, expr, locals);

    case AST_WITH_EXPR:
        return emit_expr_with_expr(g, expr, locals);

    case AST_POSTFIX_UNARY:
        /*
         * postfix `!` (null-forgiving): a compile-time assertion with no
         * runtime effect -- the operand's value, unchanged.
         */
        if (expr->unary.op == TK_BANG)
            return emit_expr(g, expr->unary.operand, locals);
        /* x++ / x-- — postfix yields the value before the change. */
        return emit_incdec_expr(g, expr, locals, 0);

    case AST_IS_EXPR: {
        /* 单继承体系下的 x is T 祖先链类型判断 */
        zan_type_t *st = infer_expr_type(g, expr->type_test.expr, locals);
        LLVMValueRef v = emit_expr(g, expr->type_test.expr, locals);
        LLVMValueRef test;
        if (!expr->type_test.type) {
            /* `is null` / `is not null` — plain null comparison. */
            test = zan_icmp(g->builder, LLVMIntEQ, v,
                LLVMConstNull(LLVMTypeOf(v)), "is.null");
        } else {
            zan_type_t *tt = resolve_type_ctx(g, expr->type_test.type);
            if (!st || !tt) {
                test = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
            } else {
                bool st_ref = st->kind == TYPE_OBJECT || st->kind == TYPE_INTERFACE ||
                              st->kind == TYPE_STRING || st->kind == TYPE_CLASS;
                bool tt_ref = tt->kind == TYPE_OBJECT || tt->kind == TYPE_INTERFACE ||
                              tt->kind == TYPE_STRING || tt->kind == TYPE_CLASS;
                if (!st_ref || !tt_ref) {
                    test = LLVMConstInt(LLVMInt1TypeInContext(g->ctx),
                                        types_equal(st, tt) ? 1 : 0, 0);
                } else {
                    /*
                     * both reference kinds: value must be non-null for any
                     * `is` to hold
                     */
                    LLVMValueRef nn = zan_icmp(g->builder, LLVMIntNE, v,
                        LLVMConstNull(LLVMTypeOf(v)), "is.nonnull");
                    if (st->kind == TYPE_STRING || tt->kind == TYPE_STRING) {
                        if (st->kind == tt->kind) {
                            test = nn; /* string is string iff non-null */
                        } else if (st->kind == TYPE_OBJECT ||
                                   st->kind == TYPE_INTERFACE ||
                                   tt->kind == TYPE_OBJECT ||
                                   tt->kind == TYPE_INTERFACE) {
                            /*
                             * `object o = "hi"; o is string`: the slot may
                             * hold a string, whose obj-8 magic makes the
                             * runtime check answer exactly this.
                             */
                            test = emit_runtime_is_check_name(g, v, tt);
                        } else {
                            /* string vs a class: never matches */
                            test = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
                        }
                    } else if (tt->kind == TYPE_OBJECT || tt->kind == TYPE_INTERFACE) {
                        test = nn;
                    } else if (st->kind == TYPE_CLASS && type_is_or_derives(st, tt)) {
                        test = nn;
                    } else if (st->kind == TYPE_CLASS && type_is_or_derives(tt, st)) {
                        test = emit_runtime_is_check_name(g, v, tt);
                    } else if (st->kind == TYPE_OBJECT || st->kind == TYPE_INTERFACE) {
                        test = emit_runtime_is_check_name(g, v, tt);
                    } else {
                        test = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
                    }
                }
            }
        }
        if (expr->type_test.is_not)
            test = LLVMBuildNot(g->builder, test, "is.not");
        /* 模式匹配变量 (x is T v)：类型匹配成功时在当前作用域注册变量 v */
        if (expr->type_test.var_name.len > 0) {
            zan_type_t *bt = expr->type_test.type
                ? resolve_type_ctx(g, expr->type_test.type) : st;
            if (!bt) bt = g->binder->type_object;
            LLVMTypeRef btll = map_type(g, bt);
            LLVMValueRef slot = emit_entry_alloca(g, btll, "pat");
            zan_store_fit(g, LLVMConstNull(btll), slot);
            LLVMValueRef castv = v;
            if (LLVMTypeOf(castv) != btll &&
                LLVMGetTypeKind(btll) == LLVMPointerTypeKind &&
                LLVMGetTypeKind(LLVMTypeOf(castv)) == LLVMPointerTypeKind)
                castv = LLVMBuildBitCast(g->builder, castv, btll, "pat.cast");
            zan_store_fit(g, castv, slot);
            local_add(locals, expr->type_test.var_name, slot, bt);
        }
        return test;
    }

    case AST_AS_EXPR: {
        /*
         * x as T — downcasts check the runtime class and yield null on
         * failure; upcasts/identity/value casts pass the value through.
         */
        zan_type_t *st = infer_expr_type(g, expr->type_test.expr, locals);
        zan_type_t *tt = resolve_type_ctx(g, expr->type_test.type);
        if (!st || !tt) return emit_expr(g, expr->type_test.expr, locals);
        bool st_ref = st->kind == TYPE_OBJECT || st->kind == TYPE_INTERFACE ||
                      st->kind == TYPE_STRING || st->kind == TYPE_CLASS;
        bool tt_ref = tt->kind == TYPE_OBJECT || tt->kind == TYPE_INTERFACE ||
                      tt->kind == TYPE_STRING || tt->kind == TYPE_CLASS;
        if (!st_ref || !tt_ref || st->kind == TYPE_STRING ||
            tt->kind == TYPE_STRING || tt->kind == TYPE_OBJECT ||
            tt->kind == TYPE_INTERFACE)
            return emit_expr(g, expr->type_test.expr, locals);
        /*
         * target is a class; static source may be a class, object or
         * interface
         */
        LLVMValueRef v = emit_expr(g, expr->type_test.expr, locals);
        if (st->kind == TYPE_CLASS && type_is_or_derives(st, tt))
            return v; /* upcast: always succeeds */
        if (st->kind == TYPE_CLASS && !type_is_or_derives(tt, st))
            return v; 
/*
 * unrelated classes: pass through unchanged (never
 * matches at runtime either way)
 */
        /* downcast, or object/interface source: runtime check */
        char tbuf[320];
        int tlen = (int)tt->sym->name.len;
        if (tlen > 319) tlen = 319;
        memcpy(tbuf, tt->sym->name.str, (size_t)tlen);
        tbuf[tlen] = 0;
        LLVMValueRef ok = emit_runtime_is_check(g, v, tbuf);
        return LLVMBuildSelect(g->builder, ok, v,
            LLVMConstNull(LLVMTypeOf(v)), "as.res");
    }

    case AST_CAST_EXPR:
        return emit_expr_cast_expr(g, expr, locals);

    case AST_SIZEOF_EXPR: {
        /* sizeof(Type) — real store size of the mapped type. */
        zan_type_t *t = resolve_type_ctx(g, expr->cast.type);
        LLVMTypeRef mt = t ? map_type(g, t) : NULL;
        if (mt && LLVMGetTypeKind(mt) != LLVMVoidTypeKind)
            return LLVMSizeOf(mt);
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 8, 0);
    }

    case AST_TYPEOF_EXPR: {
        /* typeof(T): 获取类型的静态反射元数据记录 */
        zan_type_t *t = resolve_type_ctx(g, expr->cast.type);
        char buf[256];
        int len = 0;
        if (t) {
            len = render_type_full(t, buf, (int)sizeof(buf));
        } else {
            len = render_type_ref_name(expr->cast.type, buf, (int)sizeof(buf));
        }
        return refl_meta_for(g, t, buf, len);
    }

    case AST_THIS_EXPR: {
        /* this 指针读取：从接收者局部变量槽位加载 */
        if (g->current_this) {
            return LLVMBuildLoad2(g->builder, LLVMGetAllocatedType(g->current_this),
                g->current_this, "this");
        }
        /* 非捕获 Lambda 内部无隐式 this 绑定 */
        zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
            "cannot use 'this' here: %s has no receiver binding",
            g->lambda_depth > 0
                ? "a lambda cannot capture 'this' (lambdas are non-capturing)"
                : "a static context");
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
    }

    case AST_BASE_EXPR: {
        /* base — same as this for single inheritance (see AST_THIS_EXPR). */
        if (g->current_this) {
            return LLVMBuildLoad2(g->builder, LLVMGetAllocatedType(g->current_this),
                g->current_this, "this");
        }
        zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
            "cannot use 'base' here: %s has no receiver binding",
            g->lambda_depth > 0
                ? "a lambda cannot capture 'this' (lambdas are non-capturing)"
                : "a static context");
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
    }

    case AST_AWAIT_EXPR:
        return emit_expr_await_expr(g, expr, locals);

    case AST_LAMBDA:
        return emit_lambda_typed(g, expr, NULL, locals);

    case AST_CHECKED_STMT:
        /* 表达式级 checked / unchecked 运算溢出开关处理 */
        {
            int saved = g->irgen_checked_depth;
            g->irgen_checked_depth = expr->checked_stmt.checked
                ? saved + 1 : saved - 1;
            LLVMValueRef v = emit_expr(g, expr->checked_stmt.body, locals);
            g->irgen_checked_depth = saved;
            return v;
        }

    default:
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    }
}

/* Lambda 表达式变量捕获与闭包环境生成 */
typedef struct {
    zan_istr_t   name;   /* captured local's name; empty for the receiver */
    LLVMValueRef slot;   
/*
 * the enclosing alloca, NULL for the receiver;
 * for a boxed capture, the heap cell instead
 */
    zan_type_t  *type;
    LLVMTypeRef  llvm;
    /* 外层局部变量被闭包修改时装箱为共享 cell */
    int          boxed;
    /* for 循环迭代变量闭包捕获按轮独立装箱 */
    int          per_iter;
} lambda_capture_t;

typedef struct {
    zan_irgen_t     *g;
    local_scope_t   *outer;
    /*
     * Grown on demand: a fixed cap here rejected the whole lambda once a body
     * mentioned more than a handful of enclosing locals.
     */
    lambda_capture_t *caps;
    int              count;
    int              cap;
    int              needs_this;
    zan_istr_t      *shadow;
    int              shadow_count;
    int              shadow_cap;
    int              overflow;
    /* 写入扫描：分析局部变量是否存在写操作以决定是否装箱 */
    zan_istr_t       want_write;
    int              lam_depth;
    int              found_write;
    int              write_any;
    /* 共享捕获 cell 探测：识别被闭包写入的局部变量并提升为堆分配 cell */
    zan_istr_t       want_capture;
    zan_ast_node_t  *capture_decl;
    int              capture_active;
    int              capture_shadow_depth;
    int              found_capture;
} capture_scan_t;

static int istr_eq_c(zan_istr_t a, zan_istr_t b) {
    return a.len == b.len && a.len > 0 &&
           memcmp(a.str, b.str, (size_t)a.len) == 0;
}

static void cap_shadow(capture_scan_t *cs, zan_istr_t name) {
    if (!name.len) return;
    for (int i = 0; i < cs->shadow_count; i++)
        if (istr_eq_c(cs->shadow[i], name)) return;
    if (!ZAN_TAB_ENSURE(cs->shadow, cs->shadow_count, cs->shadow_cap, 64)) {
        cs->overflow = 1;
        return;
    }
    cs->shadow[cs->shadow_count++] = name;
}

static void cap_scan_free(capture_scan_t *cs) {
    free(cs->caps);
    cs->caps = NULL;
    cs->count = cs->cap = 0;
    free(cs->shadow);
    cs->shadow = NULL;
    cs->shadow_count = cs->shadow_cap = 0;
}

static int cap_is_shadowed(capture_scan_t *cs, zan_istr_t name) {
    for (int i = 0; i < cs->shadow_count; i++)
        if (istr_eq_c(cs->shadow[i], name)) return 1;
    return 0;
}

/*
 * Instance member of the enclosing type referred to by a bare name: using one
 * inside a lambda is an implicit `this.` and therefore captures the receiver.
 */
static int name_is_instance_member(zan_irgen_t *g, zan_istr_t name) {
    zan_symbol_t *ts = g->current_type_sym;
    if (!ts) return 0;
    for (zan_symbol_t *s = ts; s; s = (s->type && s->type->base_type)
                                        ? s->type->base_type->sym : NULL) {
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            if (!m || !istr_eq_c(m->name, name)) continue;
            if (m->kind != SYM_FIELD && m->kind != SYM_PROPERTY &&
                m->kind != SYM_METHOD) continue;
            return (m->modifiers & MOD_STATIC) ? 0 : 1;
        }
    }
    return 0;
}

static void cap_use(capture_scan_t *cs, zan_istr_t name) {
    if (!name.len || cap_is_shadowed(cs, name)) return;
    for (int i = 0; i < cs->count; i++)
        if (istr_eq_c(cs->caps[i].name, name)) return;
    local_var_t *lv = cs->outer ? local_find(cs->outer, name) : NULL;
    if (!lv) {
        if (name_is_instance_member(cs->g, name)) cs->needs_this = 1;
        return;
    }
    if (!ZAN_TAB_ENSURE(cs->caps, cs->count, cs->cap, 16)) {
        cs->overflow = 1;
        return;
    }
    lambda_capture_t *c = &cs->caps[cs->count++];
    c->name = name;
    c->type = lv->type;
    c->boxed = lv->box_cell ? 1 : 0;
    c->per_iter = lv->per_iteration;
    if (c->boxed) {
        c->slot = lv->box_cell;
        c->llvm = LLVMPointerType(LLVMInt8TypeInContext(cs->g->ctx), 0);
    } else {
        c->slot = lv->alloca;
        c->llvm = local_slot_type(cs->g, lv);
    }
}

/*
 * Does this node assign to `name`? Assignment, `++`/`--` and passing it as
 * `ref`/`out` all write the variable.
 */
static int node_writes_ident(zan_ast_node_t *n, zan_istr_t name) {
    zan_ast_node_t *t = NULL;
    if (n->kind == AST_ASSIGNMENT) t = n->binary.left;
    else if ((n->kind == AST_UNARY || n->kind == AST_POSTFIX_UNARY) &&
             (n->unary.op == TK_PLUS_PLUS || n->unary.op == TK_MINUS_MINUS))
        t = n->unary.operand;
    else if (n->kind == AST_REF_ARG) t = n->ref_arg.expr;
    return t && t->kind == AST_IDENTIFIER && istr_eq_c(t->ident.name, name);
}

/* 检查标识符是否作为写入目标（赋值、自增减、ref/out 实参） */
static zan_istr_t node_write_name(zan_ast_node_t *n) {
    zan_ast_node_t *t = NULL;
    if (n->kind == AST_ASSIGNMENT) t = n->binary.left;
    else if ((n->kind == AST_UNARY || n->kind == AST_POSTFIX_UNARY) &&
             (n->unary.op == TK_PLUS_PLUS || n->unary.op == TK_MINUS_MINUS))
        t = n->unary.operand;
    else if (n->kind == AST_REF_ARG) t = n->ref_arg.expr;
    return (t && t->kind == AST_IDENTIFIER) ? t->ident.name
                                            : (zan_istr_t){ NULL, 0 };
}

static void cap_scan(capture_scan_t *cs, zan_ast_node_t *n);

static void cap_scan_list(capture_scan_t *cs, zan_ast_list_t *l) {
    for (int i = 0; l && i < l->count; i++) cap_scan(cs, l->items[i]);
}

static void cap_scan(capture_scan_t *cs, zan_ast_node_t *n) {
    if (!n) return;
    if (cs->want_write.len) {
        if (cs->found_write) return;
        if ((cs->lam_depth > 0 || cs->write_any) &&
            node_writes_ident(n, cs->want_write)) {
            cs->found_write = 1;
            return;
        }
    }
    if (cs->want_capture.len && cs->found_capture) return;
    switch (n->kind) {
    case AST_IDENTIFIER:
        if (cs->want_capture.len && cs->capture_active && cs->lam_depth > 0 &&
            cs->capture_shadow_depth == 0 &&
            istr_eq_c(n->ident.name, cs->want_capture))
            cs->found_capture = 1;
        cap_use(cs, n->ident.name);
        return;
    case AST_THIS_EXPR:
    case AST_BASE_EXPR:       cs->needs_this = 1; return;
    case AST_BINARY:
    case AST_ASSIGNMENT:      cap_scan(cs, n->binary.left);
                              cap_scan(cs, n->binary.right); return;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:   cap_scan(cs, n->unary.operand); return;
    case AST_CALL:            cap_scan(cs, n->call.callee);
                              cap_scan_list(cs, &n->call.args); return;
    case AST_MEMBER_ACCESS:   cap_scan(cs, n->member.object); return;
    case AST_INDEX:           cap_scan(cs, n->index.object);
                              cap_scan(cs, n->index.index); return;
    case AST_CONDITIONAL:     cap_scan(cs, n->conditional.cond);
                              cap_scan(cs, n->conditional.then_expr);
                              cap_scan(cs, n->conditional.else_expr); return;
    case AST_NEW_EXPR:        cap_scan_list(cs, &n->new_expr.args); return;
    case AST_COLL_INIT:       cap_scan_list(cs, &n->coll_init.items); return;
    case AST_CAST_EXPR:       cap_scan(cs, n->cast.expr); return;
    case AST_IS_EXPR:
    case AST_AS_EXPR:         cap_scan(cs, n->type_test.expr); return;
    case AST_AWAIT_EXPR:      cap_scan(cs, n->await_expr.expr); return;
    case AST_REF_ARG:         cap_scan(cs, n->ref_arg.expr); return;
    case AST_STRING_INTERP:   cap_scan_list(cs, &n->string_interp.parts); return;
    case AST_QUERY_EXPR: {
        int shadow_base = cs->shadow_count;
        int capture_shadow_base = cs->capture_shadow_depth;
        cap_scan(cs, n->query.source);
        cap_shadow(cs, n->query.var);
        if (cs->capture_active && istr_eq_c(n->query.var, cs->want_capture))
            cs->capture_shadow_depth++;
        for (int ci = 0; ci < n->query.clauses.count; ci++) {
            zan_ast_node_t *cl = n->query.clauses.items[ci];
            if (cl->kind == AST_QUERY_JOIN) {
                /*
                 * s evaluates in the outer scope; the left key sees x and
                 * the lets; the right key sees the join var
                 */
                cap_scan(cs, cl->query_clause.source);
                cap_scan(cs, cl->query_clause.left_key);
                int join_shadow_base = cs->capture_shadow_depth;
                cap_shadow(cs, cl->query_clause.name);
                if (cs->capture_active &&
                    istr_eq_c(cl->query_clause.name, cs->want_capture))
                    cs->capture_shadow_depth++;
                cap_scan(cs, cl->query_clause.right_key);
                cs->capture_shadow_depth = join_shadow_base;
                if (cl->query_clause.into.len > 0) {
                    cap_shadow(cs, cl->query_clause.into);
                    if (cs->capture_active &&
                        istr_eq_c(cl->query_clause.into, cs->want_capture))
                        cs->capture_shadow_depth++;
                }
            } else {
                cap_scan(cs, cl->query_clause.expr);
                if (cl->kind == AST_QUERY_LET) {
                    cap_shadow(cs, cl->query_clause.name);
                    if (cs->capture_active &&
                        istr_eq_c(cl->query_clause.name, cs->want_capture))
                        cs->capture_shadow_depth++;
                }
            }
        }
        if (n->query.group_expr) {
            cap_scan(cs, n->query.group_expr);
            cap_scan(cs, n->query.group_key);
            if (n->query.group_into.len > 0) {
                cap_shadow(cs, n->query.group_into);
                if (cs->capture_active &&
                    istr_eq_c(n->query.group_into, cs->want_capture))
                    cs->capture_shadow_depth++;
            }
        }
        if (n->query.select) cap_scan(cs, n->query.select);
        cs->shadow_count = shadow_base;
        cs->capture_shadow_depth = capture_shadow_base;
        return;
    }
    case AST_SWITCH_EXPR:
        cap_scan(cs, n->switch_expr.expr);
        for (int ai = 0; ai < n->switch_expr.arms.count; ai++) {
            zan_ast_node_t *arm = n->switch_expr.arms.items[ai];
            int shadow_base = cs->shadow_count;
            int capture_shadow_base = cs->capture_shadow_depth;
            if (arm->switch_arm.pattern) cap_scan(cs, arm->switch_arm.pattern);
            if (arm->switch_arm.type_pattern && arm->switch_arm.var_name.len > 0) {
                cap_shadow(cs, arm->switch_arm.var_name);
                if (cs->capture_active &&
                    istr_eq_c(arm->switch_arm.var_name, cs->want_capture))
                    cs->capture_shadow_depth++;
            }
            if (arm->switch_arm.when_cond) cap_scan(cs, arm->switch_arm.when_cond);
            cap_scan(cs, arm->switch_arm.result);
            cs->shadow_count = shadow_base;
            cs->capture_shadow_depth = capture_shadow_base;
        }
        return;
    case AST_WITH_EXPR:
        cap_scan(cs, n->with_expr.expr);
        for (int ai = 0; ai < n->with_expr.assigns.count; ai++)
            cap_scan(cs, n->with_expr.assigns.items[ai]);
        return;
    case AST_LAMBDA: {
        /* Lambda 形参遮蔽与外层作用域符号恢复 */
        int shadow_base = cs->shadow_count;
        int capture_shadow_base = cs->capture_shadow_depth;
        for (int i = 0; i < n->lambda.params.count; i++) {
            zan_istr_t pn = n->lambda.params.items[i]->param.name;
            cap_shadow(cs, pn);
            if (cs->capture_active && istr_eq_c(pn, cs->want_capture))
                cs->capture_shadow_depth++;
        }
        cs->lam_depth++;
        cap_scan(cs, n->lambda.body);
        cs->lam_depth--;
        cs->shadow_count = shadow_base;
        cs->capture_shadow_depth = capture_shadow_base;
        return;
    }
    case AST_BLOCK: {
        int shadow_base = cs->shadow_count;
        int active_base = cs->capture_active;
        int capture_shadow_base = cs->capture_shadow_depth;
        cap_scan_list(cs, &n->block.stmts);
        cs->shadow_count = shadow_base;
        cs->capture_active = active_base;
        cs->capture_shadow_depth = capture_shadow_base;
        return;
    }
    case AST_VAR_DECL:
        cap_scan(cs, n->var_decl.initializer);
        if (cs->want_capture.len && n == cs->capture_decl) {
            cs->capture_active = 1;
            cs->capture_shadow_depth = 0;
        } else {
            cap_shadow(cs, n->var_decl.name);
            if (cs->capture_active &&
                istr_eq_c(n->var_decl.name, cs->want_capture))
                cs->capture_shadow_depth++;
        }
        return;
    case AST_EXPR_STMT:       cap_scan(cs, n->expr_stmt.expr); return;
    case AST_RETURN_STMT:     cap_scan(cs, n->ret.value); return;
    case AST_IF_STMT:         cap_scan(cs, n->if_stmt.cond);
                              cap_scan(cs, n->if_stmt.then_body);
                              cap_scan(cs, n->if_stmt.else_body); return;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:   cap_scan(cs, n->while_stmt.cond);
                              cap_scan(cs, n->while_stmt.body); return;
    case AST_FOR_STMT: {
        int shadow_base = cs->shadow_count;
        int active_base = cs->capture_active;
        int capture_shadow_base = cs->capture_shadow_depth;
        cap_scan(cs, n->for_stmt.init);
        cap_scan(cs, n->for_stmt.cond);
        cap_scan(cs, n->for_stmt.step);
        cap_scan(cs, n->for_stmt.body);
        cs->shadow_count = shadow_base;
        cs->capture_active = active_base;
        cs->capture_shadow_depth = capture_shadow_base;
        return;
    }
    case AST_FOREACH_STMT: {
        int shadow_base = cs->shadow_count;
        int capture_shadow_base = cs->capture_shadow_depth;
        cap_scan(cs, n->foreach_stmt.collection);
        cap_shadow(cs, n->foreach_stmt.var_name);
        if (cs->capture_active &&
            istr_eq_c(n->foreach_stmt.var_name, cs->want_capture))
            cs->capture_shadow_depth++;
        cap_scan(cs, n->foreach_stmt.body);
        cs->shadow_count = shadow_base;
        cs->capture_shadow_depth = capture_shadow_base;
        return;
    }
    case AST_THROW_STMT:      cap_scan(cs, n->throw_stmt.value); return;
    case AST_TRY_STMT:        cap_scan(cs, n->try_stmt.try_body);
                              cap_scan_list(cs, &n->try_stmt.catches);
                              cap_scan(cs, n->try_stmt.finally_body); return;
    case AST_CATCH_CLAUSE: {
        int shadow_base = cs->shadow_count;
        int capture_shadow_base = cs->capture_shadow_depth;
        cap_shadow(cs, n->catch_clause.var_name);
        if (cs->capture_active &&
            istr_eq_c(n->catch_clause.var_name, cs->want_capture))
            cs->capture_shadow_depth++;
        cap_scan(cs, n->catch_clause.body);
        cs->shadow_count = shadow_base;
        cs->capture_shadow_depth = capture_shadow_base;
        return;
    }
    case AST_SWITCH_STMT:     cap_scan(cs, n->switch_stmt.expr);
                              cap_scan_list(cs, &n->switch_stmt.cases); return;
    case AST_SWITCH_CASE: {
        int shadow_base = cs->shadow_count;
        int active_base = cs->capture_active;
        int capture_shadow_base = cs->capture_shadow_depth;
        cap_scan(cs, n->switch_case.pattern);
        if (cs->want_capture.len && n == cs->capture_decl) {
            cs->capture_active = 1;
            cs->capture_shadow_depth = 0;
        } else if (n->switch_case.var_name.len > 0) {
            cap_shadow(cs, n->switch_case.var_name);
            if (cs->capture_active &&
                istr_eq_c(n->switch_case.var_name, cs->want_capture))
                cs->capture_shadow_depth++;
        }
        cap_scan(cs, n->switch_case.when_cond);
        cap_scan(cs, n->switch_case.body);
        cs->shadow_count = shadow_base;
        cs->capture_active = active_base;
        cs->capture_shadow_depth = capture_shadow_base;
        return;
    }
    case AST_LOCK_STMT:       cap_scan(cs, n->lock_stmt.expr);
                              cap_scan(cs, n->lock_stmt.body); return;
    case AST_CHECKED_STMT:    cap_scan(cs, n->checked_stmt.body); return;
    case AST_YIELD_STMT:      cap_scan(cs, n->yield_stmt.value); return;
    default: return;
    }
}

/* 闭包析构函数：释放捕获的环境变量与接收者强引用 */
static LLVMValueRef build_closure_dtor(zan_irgen_t *g, const char *lname,
                                       LLVMTypeRef rec_ty,
                                       lambda_capture_t *caps, int capc,
                                       int has_this, bool release_target) {
    LLVMContextRef c = g->ctx;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    char name[160];
    snprintf(name, sizeof(name), "__zan_clo_dtor_%s", lname);
    /*
     * method-group records reuse one dtor per method (see the stable name
     * built there); lambda names are already unique
     */
    LLVMValueRef existing = LLVMGetNamedFunction(g->mod, name);
    if (existing) return existing;
    LLVMValueRef fn = LLVMAddFunction(g->mod,
        name, LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0));
    LLVMSetLinkage(fn, LLVMInternalLinkage);

    LLVMBuilderRef saved = g->builder;
    LLVMBuilderRef b = LLVMCreateBuilderInContext(c);
    g->builder = b;
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef drop  = LLVMAppendBasicBlockInContext(c, fn, "drop");
    LLVMBasicBlockRef dec   = LLVMAppendBasicBlockInContext(c, fn, "dec");
    LLVMValueRef rec = LLVMGetParam(fn, 0);
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef neg16 = LLVMConstInt(i64, (unsigned long long)ZAN_OBJ_RC_OFF, 1);
    LLVMValueRef rcp = LLVMBuildGEP2(b, LLVMInt8TypeInContext(c), rec, &neg16, 1, "rcp");
    LLVMValueRef rcip = LLVMBuildBitCast(b, rcp, LLVMPointerType(i64, 0), "rcip");
    /* 析构防重入：原子声明防止并发二次释放 */
    LLVMValueRef rc_old = LLVMBuildAtomicRMW(b, LLVMAtomicRMWBinOpSub, rcip,
        LLVMConstInt(i64, 1, 0), LLVMAtomicOrderingAcquireRelease, 0);
    LLVMBasicBlockRef last_bb = LLVMAppendBasicBlockInContext(c, fn, "last");
    LLVMBasicBlockRef freebb = LLVMAppendBasicBlockInContext(c, fn, "freebb");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(c, fn, "done");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntSLE, rc_old, LLVMConstInt(i64, 0, 0), "over"),
                    dec, last_bb);
    LLVMPositionBuilderAtEnd(b, last_bb);
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, rc_old, LLVMConstInt(i64, 1, 0), "is1"),
                    drop, done_bb);
    LLVMPositionBuilderAtEnd(b, drop);
    /* 方法组绑定的接收者引用释放 */
    /*
     * the bound receiver of a method group (null for a lambda; the release is
     * null-tolerant). Static method groups keep the thunk pointer in the
     * target slot purely so delegate equality can recognize them; that slot
     * then holds a function, not an object, and must not be released.
     */
    if (release_target) {
        LLVMValueRef p = LLVMBuildStructGEP2(b, rec_ty, rec, 2, "tgp");
        emit_arc_release_typed(g, NULL, LLVMBuildLoad2(b, i8ptr, p, "tgv"));
    }
    for (int i = 0; i < capc; i++) {
        int aggregate_rc = caps[i].type &&
            caps[i].type->kind == TYPE_STRUCT &&
            type_contains_collection_rc(g, caps[i].type, 0);
        if (!caps[i].boxed && !is_rc_managed_type(caps[i].type) &&
            !aggregate_rc) continue;
        LLVMValueRef p = LLVMBuildStructGEP2(g->builder, rec_ty, rec,
            (unsigned)(ZAN_CLOSURE_HDR_FIELDS + i), "cp");
        LLVMValueRef v = LLVMBuildLoad2(g->builder, caps[i].llvm, p, "cv");
        /*
         * a boxed capture is a reference to the variable's cell, so this
         * closure drops its reference to the cell, not to a value
         */
        if (caps[i].boxed) emit_closure_record_release(g, v);
        else if (aggregate_rc)
            emit_collection_value_release(g, caps[i].type, v, 0);
        else emit_rc_release_for_type(g, caps[i].type, v);
    }
    if (has_this) {
        LLVMValueRef p = LLVMBuildStructGEP2(g->builder, rec_ty, rec,
            (unsigned)(ZAN_CLOSURE_HDR_FIELDS + capc), "tp");
        LLVMValueRef v = LLVMBuildLoad2(g->builder, i8ptr, p, "tv");
        emit_arc_release_typed(g, NULL, v);
    }
    LLVMBuildBr(g->builder, freebb);
    LLVMPositionBuilderAtEnd(g->builder, freebb);
    LLVMValueRef freefn = get_arc_free_decl(g);
    zan_call2(g->builder, LLVMGlobalGetValueType(freefn), freefn, &rec, 1, "");
    LLVMBuildBr(g->builder, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, dec);
    zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0),
              g->rt_release, &rec, 1, "");
    LLVMBuildBr(g->builder, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    LLVMBuildRetVoid(g->builder);
    LLVMDisposeBuilder(b);
    g->builder = saved;
    return fn;
}

/* 在当前帧分配并初始化闭包记录 */
static LLVMValueRef emit_box_cell(zan_irgen_t *g, zan_loc_t loc,
                                  LLVMTypeRef payload, zan_type_t *vtype,
                                  LLVMValueRef init);
static LLVMValueRef box_value_ptr(zan_irgen_t *g, LLVMValueRef cell,
                                  LLVMTypeRef payload);
static LLVMValueRef emit_closure_record(zan_irgen_t *g, zan_loc_t loc,
                                        const char *lname, LLVMTypeRef rec_ty,
                                        LLVMValueRef fn_ptr, LLVMValueRef target,
                                        lambda_capture_t *caps, int capc,
                                        int has_this, LLVMValueRef self,
                                        bool retain_target) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef dtor = build_closure_dtor(g, lname, rec_ty, caps, capc,
                                           has_this, true);
    int site_idx = reserve_closure_site(g);
    LLVMValueRef site_name = LLVMConstNull(i8ptr);
    if (g->check_leaks) {
        char site_buf[600];
        snprintf(site_buf, sizeof(site_buf), "%s:%u:%u [closure]",
                 loc_site_file(g, loc), loc.line, loc.col);
        site_name = zan_irgen_intern_string(g, site_buf);
    }
    LLVMValueRef alloc_args[3] = {
        LLVMBuildPtrToInt(g->builder, LLVMSizeOf(rec_ty), i64, "clo.size"),
        arc_site_arg(g, site_idx), site_name };
    LLVMValueRef rec = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0),
        g->rt_alloc, alloc_args, 3, "clo");
    LLVMBuildStore(g->builder,
        LLVMBuildBitCast(g->builder, fn_ptr, i8ptr, "clo.fn"),
        LLVMBuildStructGEP2(g->builder, rec_ty, rec, 0, "clo.fnp"));
    LLVMBuildStore(g->builder,
        LLVMBuildBitCast(g->builder, dtor, i8ptr, "clo.dt"),
        LLVMBuildStructGEP2(g->builder, rec_ty, rec, 1, "clo.dtp"));
    if (target) {
        if (LLVMTypeOf(target) != i8ptr)
            target = LLVMBuildBitCast(g->builder, target, i8ptr, "clo.tg8");
        if (retain_target) emit_arc_retain(g, target);
    }
    LLVMBuildStore(g->builder, target ? target : LLVMConstNull(i8ptr),
        LLVMBuildStructGEP2(g->builder, rec_ty, rec, 2, "clo.tgp"));
    for (int i = 0; i < capc; i++) {
        if (caps[i].boxed) {
            LLVMValueRef cell = caps[i].slot;
            if (caps[i].per_iter) {
                /* for 循环变量闭包按每次迭代独立捕获 */
                zan_type_t *pt = caps[i].type;
                LLVMTypeRef payload = map_type(g, pt);
                LLVMValueRef cur = LLVMBuildLoad2(g->builder, payload,
                    box_value_ptr(g, cell, payload), "loopvar.cap");
                if (pt->kind == TYPE_STRUCT &&
                    type_contains_collection_rc(g, pt, 0))
                    emit_collection_value_retain(g, pt, cur, 0);
                else if (is_rc_managed_type(pt) &&
                         LLVMGetTypeKind(payload) == LLVMPointerTypeKind)
                    emit_rc_retain_for_type(g, pt, cur);
                cell = emit_box_cell(g, loc, payload, pt, cur);
            } else {
                emit_arc_retain(g, cell);
            }
            LLVMBuildStore(g->builder, cell,
                LLVMBuildStructGEP2(g->builder, rec_ty, rec,
                                    (unsigned)(ZAN_CLOSURE_HDR_FIELDS + i), "cap.bp"));
            continue;
        }
        LLVMValueRef v = LLVMBuildLoad2(g->builder, caps[i].llvm, caps[i].slot, "cap.v");
        if (type_contains_collection_rc(g, caps[i].type, 0))
            emit_collection_value_retain(g, caps[i].type, v, 0);
        else
            emit_rc_retain_for_type(g, caps[i].type, v);
        LLVMBuildStore(g->builder, v,
            LLVMBuildStructGEP2(g->builder, rec_ty, rec,
                                (unsigned)(ZAN_CLOSURE_HDR_FIELDS + i), "cap.sp"));
    }
    if (has_this && self) {
        if (LLVMTypeOf(self) != i8ptr)
            self = LLVMBuildBitCast(g->builder, self, i8ptr, "cap.self8");
        emit_arc_retain(g, self);
        LLVMBuildStore(g->builder, self,
            LLVMBuildStructGEP2(g->builder, rec_ty, rec,
                                (unsigned)(ZAN_CLOSURE_HDR_FIELDS + capc), "cap.selfp"));
    }
    LLVMValueRef tagged = LLVMBuildOr(g->builder,
        LLVMBuildPtrToInt(g->builder, rec, i64, "clo.i"),
        LLVMConstInt(i64, ZAN_CLOSURE_TAG, 0), "clo.tag");
    return LLVMBuildIntToPtr(g->builder, tagged, i8ptr, "clo.v");
}

/* 装箱共享局部变量 (Boxed Locals) 支持 */
#define ZAN_BOX_VALUE_FIELD ZAN_CLOSURE_HDR_FIELDS

static LLVMTypeRef box_cell_type(zan_irgen_t *g, LLVMTypeRef payload) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fields[ZAN_BOX_VALUE_FIELD + 1] = { i8ptr, i8ptr, i8ptr, payload };
    return LLVMStructTypeInContext(g->ctx, fields, ZAN_BOX_VALUE_FIELD + 1, 0);
}

/* 扫描方法体内是否存在对指定名称变量的赋值写入 */
static int body_writes_ident_scan(zan_irgen_t *g, zan_ast_node_t *body,
                                  zan_istr_t name);

/* 标识符字符串内容比对 */
static int memo_name_eq(zan_istr_t a, zan_istr_t b) {
    return a.len == b.len && a.len > 0 &&
           memcmp(a.str, b.str, (size_t)a.len) == 0;
}

static unsigned body_write_memo_hash(zan_ast_node_t *body, zan_istr_t name) {
    uint64_t h = 1469598103934665603ull;
    uintptr_t bp = (uintptr_t)body;
    for (unsigned i = 0; i < sizeof(bp); i++) {
        h ^= (unsigned char)(bp >> (i * 8));
        h *= 1099511628211ull;
    }
    for (int i = 0; i < name.len; i++) {
        h ^= (unsigned char)name.str[i];
        h *= 1099511628211ull;
    }
    return (unsigned)h;
}

static void body_write_collect(zan_irgen_t *g, zan_ast_node_t *n,
                               zan_ast_node_t *body, int lam_depth);

static void body_write_collect_list(zan_irgen_t *g, zan_ast_list_t *l,
                                    zan_ast_node_t *body, int lam_depth) {
    for (int i = 0; l && i < l->count; i++)
        body_write_collect(g, l->items[i], body, lam_depth);
}

static struct zan_body_write_entry *body_write_memo_slot(zan_irgen_t *g,
                                                         zan_ast_node_t *body,
                                                         zan_istr_t name) {
    if (g->body_write_memo_count * 2 >= g->body_write_memo_cap) {
        unsigned ncap = g->body_write_memo_cap ? g->body_write_memo_cap * 2 : 256;
        struct zan_body_write_entry *ns =
            calloc((size_t)ncap, sizeof(*ns));
        if (!ns) return NULL;
        /* 扩容并将存活条目拷贝至新符号哈希表 */
        for (unsigned i = 0; i < g->body_write_memo_cap; i++) {
            if (!g->body_write_memo[i].known) continue;
            unsigned mask = ncap - 1;
            unsigned j = body_write_memo_hash(g->body_write_memo[i].body,
                                              g->body_write_memo[i].name) & mask;
            while (ns[j].known) j = (j + 1) & mask;
            ns[j] = g->body_write_memo[i];
        }
        free(g->body_write_memo);
        g->body_write_memo = ns;
        g->body_write_memo_cap = ncap;
    }
    unsigned mask = g->body_write_memo_cap - 1;
    unsigned i = body_write_memo_hash(body, name) & mask;
    while (g->body_write_memo[i].known) {
        if (g->body_write_memo[i].body == body &&
            memo_name_eq(g->body_write_memo[i].name, name))
            return &g->body_write_memo[i];
        i = (i + 1) & mask;
    }
    return &g->body_write_memo[i];
}

/* 收集 AST 节点内所有被赋值的标识符至备忘表 */
static void body_write_collect(zan_irgen_t *g, zan_ast_node_t *n,
                               zan_ast_node_t *body, int lam_depth) {
    if (!n) return;
    if (lam_depth > 0 && n->kind == AST_IDENTIFIER) {
        struct zan_body_write_entry *e = body_write_memo_slot(g, body,
                                                              n->ident.name);
        if (e) {
            if (!e->known) {
                e->body = body;
                e->name = n->ident.name;
                e->known = 1;
                g->body_write_memo_count++;
            }
            e->lam_captured = 1;
        }
    }
    zan_istr_t w = node_write_name(n);
    if (w.str) {
        struct zan_body_write_entry *e = body_write_memo_slot(g, body, w);
        if (e) {
            if (!e->known) {
                e->body = body;
                e->name = w;
                e->written = 1;
                e->lam_written = (lam_depth > 0) ? 1 : 0;
                e->known = 1;
                g->body_write_memo_count++;
            } else if (lam_depth > 0) {
                e->lam_written = 1;
            }
        }
    }
    switch (n->kind) {
    case AST_LAMBDA: {
        /* 嵌套 Lambda 形参遮蔽外层变量：其写入不视为外层变量修改 */
        for (int i = 0; i < n->lambda.params.count; i++) {
            zan_istr_t pn = n->lambda.params.items[i]->param.name;
            struct zan_body_write_entry *e = body_write_memo_slot(g, body, pn);
            if (e && !e->known) {
                e->body = body;
                e->name = pn;
                e->written = 0;
                e->lam_written = 0;
                e->known = 1;
                g->body_write_memo_count++;
            }
        }
        body_write_collect(g, n->lambda.body, body, lam_depth + 1);
        return;
    }
    case AST_BINARY:
    case AST_ASSIGNMENT:      body_write_collect(g, n->binary.left, body, lam_depth);
                              body_write_collect(g, n->binary.right, body, lam_depth); return;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:   body_write_collect(g, n->unary.operand, body, lam_depth); return;
    case AST_CALL:            body_write_collect(g, n->call.callee, body, lam_depth);
                              body_write_collect_list(g, &n->call.args, body, lam_depth); return;
    case AST_MEMBER_ACCESS:   body_write_collect(g, n->member.object, body, lam_depth); return;
    case AST_INDEX:           body_write_collect(g, n->index.object, body, lam_depth);
                              body_write_collect(g, n->index.index, body, lam_depth); return;
    case AST_CONDITIONAL:     body_write_collect(g, n->conditional.cond, body, lam_depth);
                              body_write_collect(g, n->conditional.then_expr, body, lam_depth);
                              body_write_collect(g, n->conditional.else_expr, body, lam_depth); return;
    case AST_NEW_EXPR:        body_write_collect_list(g, &n->new_expr.args, body, lam_depth); return;
    case AST_COLL_INIT:       body_write_collect_list(g, &n->coll_init.items, body, lam_depth); return;
    case AST_CAST_EXPR:       body_write_collect(g, n->cast.expr, body, lam_depth); return;
    case AST_IS_EXPR:
    case AST_AS_EXPR:         body_write_collect(g, n->type_test.expr, body, lam_depth); return;
    case AST_AWAIT_EXPR:      body_write_collect(g, n->await_expr.expr, body, lam_depth); return;
    case AST_REF_ARG:         body_write_collect(g, n->ref_arg.expr, body, lam_depth); return;
    case AST_STRING_INTERP:   body_write_collect_list(g, &n->string_interp.parts, body, lam_depth); return;
    case AST_QUERY_EXPR:      body_write_collect(g, n->query.source, body, lam_depth);
                              body_write_collect(g, n->query.group_expr, body, lam_depth);
                              body_write_collect(g, n->query.group_key, body, lam_depth);
                              body_write_collect(g, n->query.select, body, lam_depth);
                              body_write_collect_list(g, &n->query.clauses, body, lam_depth); return;
    case AST_QUERY_WHERE:     body_write_collect(g, n->query_clause.expr, body, lam_depth); return;
    case AST_QUERY_LET:       body_write_collect(g, n->query_clause.expr, body, lam_depth); return;
    case AST_QUERY_ORDERBY:   body_write_collect(g, n->query_clause.expr, body, lam_depth); return;
    case AST_QUERY_JOIN:      body_write_collect(g, n->query_clause.source, body, lam_depth);
                              body_write_collect(g, n->query_clause.left_key, body, lam_depth);
                              body_write_collect(g, n->query_clause.right_key, body, lam_depth); return;
    case AST_SWITCH_EXPR:     body_write_collect(g, n->switch_expr.expr, body, lam_depth);
                              body_write_collect_list(g, &n->switch_expr.arms, body, lam_depth); return;
    case AST_WITH_EXPR:       body_write_collect(g, n->with_expr.expr, body, lam_depth);
                              body_write_collect_list(g, &n->with_expr.assigns, body, lam_depth); return;
    case AST_SWITCH_ARM:      body_write_collect(g, n->switch_arm.pattern, body, lam_depth);
                              body_write_collect(g, n->switch_arm.when_cond, body, lam_depth);
                              body_write_collect(g, n->switch_arm.result, body, lam_depth); return;
    case AST_BLOCK:           body_write_collect_list(g, &n->block.stmts, body, lam_depth); return;
    case AST_VAR_DECL:        body_write_collect(g, n->var_decl.initializer, body, lam_depth);
                              /*
                               * pre-seed the declared name as "known": the
                               * declaration is a binding, not a write, so the
                               * boxed-local rule answers on real writes only
                               */
                              {
                                  zan_istr_t dn = n->var_decl.name;
                                  if (dn.str) {
                                      struct zan_body_write_entry *e =
                                          body_write_memo_slot(g, body, dn);
                                      if (e && !e->known) {
                                          e->body = body;
                                          e->name = dn;
                                          e->written = 0;
                                          e->lam_written = 0;
                                          e->known = 1;
                                          g->body_write_memo_count++;
                                      }
                                  }
                              }
                              return;
    case AST_EXPR_STMT:       body_write_collect(g, n->expr_stmt.expr, body, lam_depth); return;
    case AST_RETURN_STMT:     body_write_collect(g, n->ret.value, body, lam_depth); return;
    case AST_IF_STMT:         body_write_collect(g, n->if_stmt.cond, body, lam_depth);
                              body_write_collect(g, n->if_stmt.then_body, body, lam_depth);
                              body_write_collect(g, n->if_stmt.else_body, body, lam_depth); return;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:   body_write_collect(g, n->while_stmt.cond, body, lam_depth);
                              body_write_collect(g, n->while_stmt.body, body, lam_depth); return;
    case AST_FOR_STMT:        body_write_collect(g, n->for_stmt.init, body, lam_depth);
                              body_write_collect(g, n->for_stmt.cond, body, lam_depth);
                              body_write_collect(g, n->for_stmt.step, body, lam_depth);
                              body_write_collect(g, n->for_stmt.body, body, lam_depth); return;
    case AST_FOREACH_STMT:    body_write_collect(g, n->foreach_stmt.collection, body, lam_depth);
                              body_write_collect(g, n->foreach_stmt.body, body, lam_depth); return;
    case AST_THROW_STMT:      body_write_collect(g, n->throw_stmt.value, body, lam_depth); return;
    case AST_TRY_STMT:        body_write_collect(g, n->try_stmt.try_body, body, lam_depth);
                              body_write_collect_list(g, &n->try_stmt.catches, body, lam_depth);
                              body_write_collect(g, n->try_stmt.finally_body, body, lam_depth); return;
    case AST_CATCH_CLAUSE:    body_write_collect(g, n->catch_clause.body, body, lam_depth); return;
    case AST_SWITCH_STMT:     body_write_collect(g, n->switch_stmt.expr, body, lam_depth);
                              body_write_collect_list(g, &n->switch_stmt.cases, body, lam_depth); return;
    case AST_SWITCH_CASE:     body_write_collect(g, n->switch_case.pattern, body, lam_depth);
                              body_write_collect(g, n->switch_case.when_cond, body, lam_depth);
                              body_write_collect(g, n->switch_case.body, body, lam_depth); return;
    case AST_LOCK_STMT:       body_write_collect(g, n->lock_stmt.expr, body, lam_depth);
                              body_write_collect(g, n->lock_stmt.body, body, lam_depth); return;
    case AST_CHECKED_STMT:    body_write_collect(g, n->checked_stmt.body, body, lam_depth); return;
    case AST_YIELD_STMT:      body_write_collect(g, n->yield_stmt.value, body, lam_depth); return;
    default: return;
    }
}

/* 查询变量是否在方法体内被写入 */
static int body_writes_ident_memo(zan_irgen_t *g, zan_ast_node_t *body,
                                  zan_istr_t name) {
    if (!body || !name.len) return 0;
    if (!g->body_write_memo_cap) {
        g->body_write_memo = calloc(256, sizeof(*g->body_write_memo));
        if (!g->body_write_memo) return body_writes_ident_scan(g, body, name);
        g->body_write_memo_cap = 256;
        g->body_write_memo_count = 0;
    }
    if (g->body_write_scan_done != body) {
        body_write_collect(g, body, body, 0);
        g->body_write_scan_done = body;
    }
    unsigned mask = g->body_write_memo_cap - 1;
    unsigned i = body_write_memo_hash(body, name) & mask;
    while (g->body_write_memo[i].known) {
        if (g->body_write_memo[i].body == body &&
            memo_name_eq(g->body_write_memo[i].name, name))
            return g->body_write_memo[i].written;
        i = (i + 1) & mask;
    }
    return 0;
}

static int body_writes_ident_scan(zan_irgen_t *g, zan_ast_node_t *body,
                                  zan_istr_t name) {
    capture_scan_t cs;
    memset(&cs, 0, sizeof(cs));
    cs.g = g;
    cs.want_write = name;
    cs.write_any = 1;
    cap_scan(&cs, body);
    int found = cs.found_write;
    cap_scan_free(&cs);
    return found;
}

static int body_writes_ident(zan_irgen_t *g, zan_ast_node_t *body,
                             zan_istr_t name) {
    if (!body || !name.len) return 0;
    return body_writes_ident_memo(g, body, name);
}

/* 判断当前编译函数内的 Lambda 是否捕获了该声明变量 */
static int local_is_lambda_captured(zan_irgen_t *g, local_scope_t *locals,
                                    zan_ast_node_t *body,
                                    zan_ast_node_t *decl) {
    (void)locals;
    if (!body || !decl) return 0;
    zan_istr_t name;
    memset(&name, 0, sizeof(name));
    if (decl->kind == AST_PARAM)
        name = decl->param.name;
    else if (decl->kind == AST_VAR_DECL)
        name = decl->var_decl.name;
    else if (decl->kind == AST_SWITCH_CASE)
        name = decl->switch_case.var_name;
    else
        return 0;
    if (!name.len) return 0;
    body_writes_ident_memo(g, body, name);
    unsigned mask = g->body_write_memo_cap - 1;
    unsigned i = body_write_memo_hash(body, name) & mask;
    int candidate = 0;
    while (g->body_write_memo[i].known) {
        if (g->body_write_memo[i].body == body &&
            memo_name_eq(g->body_write_memo[i].name, name)) {
            candidate = g->body_write_memo[i].lam_captured;
            break;
        }
        i = (i + 1) & mask;
    }
    if (!candidate) return 0;

    capture_scan_t cs;
    memset(&cs, 0, sizeof(cs));
    cs.g = g;
    cs.want_capture = name;
    cs.capture_decl = decl;
    cs.capture_active = decl->kind == AST_PARAM ||
                          decl->kind == AST_SWITCH_CASE;
    cap_scan(&cs, body);
    int found = cs.found_capture;
    cap_scan_free(&cs);
    return found;
}

/*
 * Allocate the cell for a boxed local and return it; `init` (may be null) is
 * its initial value.
 */
static LLVMValueRef emit_box_cell(zan_irgen_t *g, zan_loc_t loc,
                                  LLVMTypeRef payload, zan_type_t *vtype,
                                  LLVMValueRef init) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef rec_ty = box_cell_type(g, payload);
    static int box_id = 0;
    char bname[64];
    snprintf(bname, sizeof(bname), "box_%d",
             (int)__atomic_fetch_add(&box_id, 1, __ATOMIC_SEQ_CST));
    lambda_capture_t val = { .name = (zan_istr_t){ NULL, 0 }, .slot = NULL,
                             .type = vtype, .llvm = payload, .boxed = 0 };
    LLVMValueRef dtor = build_closure_dtor(g, bname, rec_ty, &val, 1, 0, true);
    int site_idx = reserve_closure_site(g);
    LLVMValueRef site_name = LLVMConstNull(i8ptr);
    if (g->check_leaks) {
        char site_buf[600];
        snprintf(site_buf, sizeof(site_buf), "%s:%u:%u [captured local]",
                 loc_site_file(g, loc), loc.line, loc.col);
        site_name = zan_irgen_intern_string(g, site_buf);
    }
    LLVMValueRef alloc_args[3] = {
        LLVMBuildPtrToInt(g->builder, LLVMSizeOf(rec_ty), i64, "box.size"),
        arc_site_arg(g, site_idx), site_name };
    LLVMValueRef cell = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0),
        g->rt_alloc, alloc_args, 3, "box");
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr),
        LLVMBuildStructGEP2(g->builder, rec_ty, cell, 0, "box.fnp"));
    LLVMBuildStore(g->builder,
        LLVMBuildBitCast(g->builder, dtor, i8ptr, "box.dt"),
        LLVMBuildStructGEP2(g->builder, rec_ty, cell, 1, "box.dtp"));
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr),
        LLVMBuildStructGEP2(g->builder, rec_ty, cell, 2, "box.tgp"));
    LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, rec_ty, cell,
                                          ZAN_BOX_VALUE_FIELD, "box.vp");
    LLVMBuildStore(g->builder, init ? init : LLVMConstNull(payload), vp);
    return cell;
}

/* 装箱局部变量存储槽：指针直达 cell 内部的值字段 */
static LLVMValueRef box_value_ptr(zan_irgen_t *g, LLVMValueRef cell,
                                  LLVMTypeRef payload) {
    return LLVMBuildStructGEP2(g->builder, box_cell_type(g, payload), cell,
                               ZAN_BOX_VALUE_FIELD, "box.v");
}

/* 作用域绑定接管共享 cell 所有权，异常展开时统一释放 */
static void own_box_cell(zan_irgen_t *g, local_var_t *v, LLVMValueRef cell) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef tagged_i = LLVMBuildOr(g->builder,
        LLVMBuildPtrToInt(g->builder, cell, i64, "box.i"),
        LLVMConstInt(i64, ZAN_CLOSURE_TAG, 0), "box.tag");
    LLVMValueRef tagged = LLVMBuildIntToPtr(g->builder, tagged_i, i8ptr,
                                            "box.owner.v");
    LLVMValueRef owner = emit_entry_alloca(g, i8ptr, "box.owner");
    LLVMBuildStore(g->builder, tagged, owner);
    v->box_cell = cell;
    v->box_owner_slot = owner;
    v->box_owned = 1;
    if (!g->current_async_frame) {
        emit_eh_tmp_push_slot(g, owner, ZAN_EH_SLOT_DLG);
        v->eh_slot = 1;
    }
}

/* 将按值传递的形参移动至闭包共享 cell */
static void box_captured_parameter(zan_irgen_t *g, local_scope_t *locals,
                                   zan_ast_node_t *param, zan_type_t *type,
                                   LLVMTypeRef payload, LLVMValueRef value,
                                   zan_ast_node_t *body) {
    if (!locals || locals->count == 0 || !param ||
        !local_is_lambda_captured(g, locals, body, param)) return;
    if (param->param.by_ref) {
        zan_diag_emit(g->diag, DIAG_ERROR, param->loc,
            "cannot capture a ref or out parameter in a lambda");
        return;
    }
    if (!type || type->kind == TYPE_OBJECT || type->kind == TYPE_TYPE_PARAM) {
        zan_diag_emit(g->diag, DIAG_ERROR, param->loc,
            "cannot capture parameter '%.*s' of type '%.*s': its runtime ownership is not representable yet",
            (int)param->param.name.len, param->param.name.str,
            type ? (int)type->name.len : 0,
            type && type->name.str ? type->name.str : "");
        return;
    }
    LLVMTypeKind k = LLVMGetTypeKind(payload);
    int rc = is_rc_managed_type(type) && k == LLVMPointerTypeKind;
    int aggregate_rc = type->kind == TYPE_STRUCT &&
        type_contains_collection_rc(g, type, 0);
    if (!rc && k != LLVMIntegerTypeKind && k != LLVMFloatTypeKind &&
        k != LLVMDoubleTypeKind && k != LLVMStructTypeKind) {
        zan_diag_emit(g->diag, DIAG_ERROR, param->loc,
            "cannot capture parameter '%.*s': unsupported value representation",
            (int)param->param.name.len, param->param.name.str);
        return;
    }
    if (aggregate_rc) emit_collection_value_retain(g, type, value, 0);
    else if (rc) emit_rc_retain_for_type(g, type, value);
    LLVMValueRef cell = emit_box_cell(g, param->loc, payload, type, value);
    local_var_t *v = &locals->vars[locals->count - 1];
    v->alloca = box_value_ptr(g, cell, payload);
    own_box_cell(g, v, cell);
    if (rc) v->arc_owned = 1;
    if (aggregate_rc) v->struct_rc = 1;
}

/* 实例方法组转委托：将接收者绑定入方法组记录 */
static LLVMValueRef emit_method_group_closure(zan_irgen_t *g, zan_symbol_t *msym,
                                              LLVMValueRef recv, zan_loc_t loc) {
    int fi = irgen_find_function(g, msym);
    if (fi < 0) return NULL;
    LLVMValueRef target = g->functions[fi].fn;
    LLVMTypeRef target_ty = g->functions[fi].fn_type;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    bool is_static = (recv == NULL);
    unsigned np = LLVMCountParamTypes(target_ty);
    if (!is_static && np < 1) return NULL;   /* no receiver parameter: not an instance method */
    LLVMTypeRef *tp = (LLVMTypeRef *)calloc((size_t)np, sizeof(LLVMTypeRef));
    LLVMGetParamTypes(target_ty, tp);
    LLVMTypeRef ret = LLVMGetReturnType(target_ty);

    /* 每个方法生成单例 thunk，支持委托判等与比较 */
    char lname[128];
    snprintf(lname, sizeof(lname), "mg_%s", LLVMGetValueName(target));

    /* record: { fn, dtor, target } -- the bound receiver is the target */
    LLVMTypeRef fields[ZAN_CLOSURE_HDR_FIELDS] = { i8ptr, i8ptr, i8ptr };
    char rname[128];
    snprintf(rname, sizeof(rname), "%s$clo", lname);
    LLVMTypeRef rec_ty = LLVMStructCreateNamed(g->ctx, rname);
    LLVMStructSetBody(rec_ty, fields, ZAN_CLOSURE_HDR_FIELDS, 0);

    LLVMTypeRef *thunk_params = (LLVMTypeRef *)calloc((size_t)np + 1, sizeof(LLVMTypeRef));
    unsigned tn;
    if (is_static) {
        thunk_params[0] = i8ptr;
        for (unsigned i = 0; i < np; i++) thunk_params[i + 1] = tp[i];
        tn = np + 1;
    } else {
        thunk_params[0] = i8ptr;
        for (unsigned i = 1; i < np; i++) thunk_params[i] = tp[i];
        tn = np;
    }
    LLVMTypeRef thunk_ty = LLVMFunctionType(ret, thunk_params, tn, 0);
    char tname[160];
    snprintf(tname, sizeof(tname), "__zan_%s", lname);
    LLVMValueRef thunk = LLVMGetNamedFunction(g->mod, tname);
    int thunk_is_new = thunk == NULL;
    if (thunk_is_new) {
        thunk = LLVMAddFunction(g->mod, tname, thunk_ty);
        LLVMSetLinkage(thunk, LLVMInternalLinkage);
    }

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    if (!thunk_is_new) {
        free(thunk_params);
        free(tp);
        if (is_static) return build_static_mg_record(g, loc, lname, rec_ty, thunk);
        return emit_closure_record(g, loc, lname, rec_ty, thunk, recv,
                                   NULL, 0, 0, NULL, true);
    }
    LLVMPositionBuilderAtEnd(g->builder,
        LLVMAppendBasicBlockInContext(g->ctx, thunk, "entry"));
    LLVMValueRef rec = LLVMGetParam(thunk, 0);
    LLVMValueRef *cargs = (LLVMValueRef *)calloc((size_t)np + 1, sizeof(LLVMValueRef));
    unsigned ca = 0;
    LLVMValueRef self = NULL;
    if (!is_static) {
        LLVMValueRef sp = LLVMBuildStructGEP2(g->builder, rec_ty, rec, 2, "mg.selfp");
        self = LLVMBuildLoad2(g->builder, i8ptr, sp, "mg.self");
        cargs[ca++] = LLVMTypeOf(self) == tp[0]
            ? self : LLVMBuildBitCast(g->builder, self, tp[0], "mg.self.c");
        for (unsigned i = 1; i < tn; i++) cargs[ca++] = LLVMGetParam(thunk, i);
    } else {
        for (unsigned i = 1; i < tn; i++) cargs[ca++] = LLVMGetParam(thunk, i);
    }
    LLVMValueRef r = zan_call2(g->builder, target_ty, target, cargs, ca,
        LLVMGetTypeKind(ret) == LLVMVoidTypeKind ? "" : "mg.r");
    if (LLVMGetTypeKind(ret) == LLVMVoidTypeKind) LLVMBuildRetVoid(g->builder);
    else LLVMBuildRet(g->builder, r);
    free(cargs);
    free(thunk_params);
    free(tp);
    LLVMPositionBuilderAtEnd(g->builder, saved_bb);

    if (is_static) return build_static_mg_record(g, loc, lname, rec_ty, thunk);
    return emit_closure_record(g, loc, lname, rec_ty, thunk, recv, NULL, 0, 0, NULL, true);
}

/* 为静态方法组构建闭包记录 */
static LLVMValueRef build_static_mg_record(zan_irgen_t *g, zan_loc_t loc,
                                           const char *lname, LLVMTypeRef rec_ty,
                                           LLVMValueRef thunk) {
    build_closure_dtor(g, lname, rec_ty, NULL, 0, 0, false);
    return emit_closure_record(g, loc, lname, rec_ty, thunk, thunk,
                               NULL, 0, 0, NULL, false);
}

/* 合成双向数据绑定属性访问器 */
static LLVMValueRef emit_binding_acc_delegate(zan_irgen_t *g, LLVMValueRef acc,
                                              LLVMTypeRef acc_ty) {
    if (!target_is_wasm32(g)) return acc;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef tp[2];
    unsigned np = LLVMCountParamTypes(acc_ty);
    if (np == 0 || np > 2) return acc; /* only the 1-target accessors exist */
    LLVMGetParamTypes(acc_ty, tp);
    LLVMTypeRef ret = LLVMGetReturnType(acc_ty);
    const char *accname = LLVMGetValueName(acc);
    char lname[512];
    snprintf(lname, sizeof(lname), "mg_%s", accname);
    /* one thunk per accessor, reused across use sites (stable name) */
    char tname[560];
    snprintf(tname, sizeof(tname), "__zan_%s", lname);
    LLVMValueRef thunk = LLVMGetNamedFunction(g->mod, tname);
    if (!thunk) {
        LLVMTypeRef thunk_params[3] = { i8ptr, tp[0], tp[1 % 2] };
        LLVMTypeRef thunk_ty = LLVMFunctionType(ret, thunk_params, np + 1, 0);
        thunk = LLVMAddFunction(g->mod, tname, thunk_ty);
        LLVMSetLinkage(thunk, LLVMInternalLinkage);
        LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
        LLVMPositionBuilderAtEnd(g->builder,
            LLVMAppendBasicBlockInContext(g->ctx, thunk, "entry"));
        LLVMValueRef cargs[2] = { LLVMGetParam(thunk, 1), LLVMGetParam(thunk, 2) };
        LLVMValueRef r = zan_call2(g->builder, acc_ty, acc, cargs, np,
            LLVMGetTypeKind(ret) == LLVMVoidTypeKind ? "" : "mg.r");
        if (LLVMGetTypeKind(ret) == LLVMVoidTypeKind) LLVMBuildRetVoid(g->builder);
        else LLVMBuildRet(g->builder, r);
        LLVMPositionBuilderAtEnd(g->builder, saved);
    }
    return build_static_mg_record(g, zan_loc(0, 0, 0, 0), lname,
                                  closure_header_type(g), thunk);
}

static LLVMValueRef emit_lambda_typed(zan_irgen_t *g, zan_ast_node_t *expr,
                                      zan_type_t *expected, local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int pc = expr->lambda.params.count;
    bool exp_delegate = expected && expected->kind == TYPE_DELEGATE;

    /*
     * Resolve each parameter's zan type: prefer an explicit annotation on the
     * lambda, otherwise borrow it from the target delegate signature.
     */
    zan_type_t **ptypes = (zan_type_t **)calloc((size_t)(pc > 0 ? pc : 1), sizeof(zan_type_t *));
    LLVMTypeRef *param_types = (LLVMTypeRef *)calloc((size_t)(pc > 0 ? pc : 1), sizeof(LLVMTypeRef));
    for (int k = 0; k < pc; k++) {
        zan_ast_node_t *param = expr->lambda.params.items[k];
        zan_type_t *pt = NULL;
        if (param->param.type) {
            pt = resolve_type_ctx(g, param->param.type);
        }
        if (!pt && exp_delegate && k < expected->delegate_param_count) {
            pt = expected->delegate_param_types[k];
        }
        ptypes[k] = pt;
        param_types[k] = pt ? map_type(g, pt) : i64;
    }
    zan_type_t *rett = exp_delegate ? expected->delegate_ret_type : NULL;
    LLVMTypeRef ret_type = rett ? map_type(g, rett) : i64;
    bool ret_void = rett && rett->kind == TYPE_VOID;
    if (ret_void) ret_type = LLVMVoidTypeInContext(g->ctx);

    /* 收集 Lambda 函数体内引用的外层局部变量与 this 接收者作为捕获集 */
    capture_scan_t cs;
    memset(&cs, 0, sizeof(cs));
    cs.g = g;
    cs.outer = locals;
    for (int k = 0; k < pc; k++)
        cap_shadow(&cs, expr->lambda.params.items[k]->param.name);
    cap_scan(&cs, expr->lambda.body);
    if (cs.needs_this && !g->current_this) cs.needs_this = 0;
    if (cs.overflow) {
        zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
            "out of memory collecting the variables this lambda captures");
        cs.count = 0;
        cs.needs_this = 0;
    }
    int capc = cs.count;
    int has_this = cs.needs_this ? 1 : 0;
    /* wasm32 平台下 Lambda 统一按带环境闭包形式发射 */
    bool is_closure = (capc + has_this) > 0 || target_is_wasm32(g);

    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    /* A closure's function takes the record as a hidden leading parameter. */
    LLVMTypeRef *all_params = (LLVMTypeRef *)calloc((size_t)pc + 2, sizeof(LLVMTypeRef));
    unsigned apc = 0;
    if (is_closure) all_params[apc++] = i8ptr;
    for (int k = 0; k < pc; k++) all_params[apc++] = param_types[k];
    LLVMTypeRef fn_type = LLVMFunctionType(ret_type, all_params, apc, 0);
    free(all_params);

    char lname[64];
    static int lambda_id = 0;
    snprintf(lname, sizeof(lname), "lambda_%d",
             (int)__atomic_fetch_add(&lambda_id, 1, __ATOMIC_SEQ_CST));
    LLVMValueRef lambda_fn = LLVMAddFunction(g->mod, lname, fn_type);
    /* 全程序静态编译：Lambda 设为 internal 链接属性 */
    zan_set_module_local(lambda_fn);

    /* 闭包记录内存布局：{ 函数指针, 析构指针, 目标指针, 捕获槽..., [this] } */
    LLVMTypeRef rec_ty = NULL;
    if (is_closure) {
        LLVMTypeRef *fields = (LLVMTypeRef *)calloc(
            (size_t)(ZAN_CLOSURE_HDR_FIELDS + capc + 1), sizeof(LLVMTypeRef));
        if (!fields) {
            cap_scan_free(&cs);
            return LLVMConstNull(i8ptr);
        }
        for (int i = 0; i < ZAN_CLOSURE_HDR_FIELDS; i++) fields[i] = i8ptr;
        for (int i = 0; i < capc; i++)
            fields[ZAN_CLOSURE_HDR_FIELDS + i] = cs.caps[i].llvm;
        if (has_this) fields[ZAN_CLOSURE_HDR_FIELDS + capc] = i8ptr;
        char rname[80];
        snprintf(rname, sizeof(rname), "%s$clo", lname);
        rec_ty = LLVMStructCreateNamed(g->ctx, rname);
        LLVMStructSetBody(rec_ty, fields,
                          (unsigned)(ZAN_CLOSURE_HDR_FIELDS + capc + has_this), 0);
        free(fields);
    }

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, lambda_fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);

    /* 调试模式 (-g)：Lambda 发射独立 LLVM 函数以生成精确作用域调试信息 */
    LLVMMetadataRef saved_dl = NULL;
    uint32_t saved_di_line = g->di_cur_line;
    uint32_t saved_di_file = g->di_cur_file;
    if (g->emit_debug) {
        saved_dl = LLVMGetCurrentDebugLocation2(g->builder);
        di_set_loc(g, expr->loc);
    }

    /* Lambda 函数体在独立的函数上下文中生成控制流基本块 */
    LLVMValueRef saved_fn = g->current_fn;
    /* 嵌套 Lambda 就地挂起当前生成函数并切换上下文 */
    bool saved_fn_is_main = g->current_fn_is_main;
    g->current_fn_is_main = false;
    LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
    zan_type_t *saved_fn_zan_ret = g->current_fn_zan_ret_type;
    LLVMValueRef saved_this = g->current_this;
    LLVMValueRef saved_async_frame = g->current_async_frame;
    zan_ast_node_t *saved_fn_body = g->current_fn_body;
    /*
     * the lambda body is the body being compiled: a local declared in it is
     * boxed when a nested lambda writes it, exactly like a method's
     */
    g->current_fn_body = expr->lambda.body;
    g->current_fn = lambda_fn;
    g->current_fn_ret_type = ret_type;
    g->current_fn_zan_ret_type = rett;
    int saved_throw_base = g->throw_locals_base;
    int saved_catch_cc = g->catch_cleanup_count;
    int saved_throw_cb = g->throw_catch_base;
    int saved_fin_c = g->finally_count;
    int saved_fin_lb = g->finally_loop_base;
    zan_irgen_pending_context_t saved_pending = g->pending;
    LLVMBasicBlockRef saved_break = g->break_target;
    LLVMBasicBlockRef saved_continue = g->continue_target;
    int saved_loop_base = g->loop_locals_base;
    int saved_loop_cbase = g->loop_catch_base;
    /* The lambda's own regions reuse these slots while the caller is hidden. */
    zan_irgen_finally_entry_t *saved_finallys = NULL;
    zan_irgen_catch_cleanup_t *saved_catches = NULL;
    if (saved_fin_c) {
        size_t bytes = sizeof(g->finallys[0]) * (size_t)saved_fin_c;
        saved_finallys = zan_arena_alloc(g->arena, bytes);
        memcpy(saved_finallys, g->finallys, bytes);
    }
    if (saved_catch_cc) {
        size_t bytes = sizeof(g->catch_cleanups[0]) * (size_t)saved_catch_cc;
        saved_catches = zan_arena_alloc(g->arena, bytes);
        memcpy(saved_catches, g->catch_cleanups, bytes);
    }
    int saved_eh_c = g->eh_armed_count;
    int saved_eh_b = g->eh_armed_base;
    int saved_eh_lb = g->eh_armed_loop_base;
    g->throw_locals_base = 0;
    g->catch_cleanup_count = 0;
    g->throw_catch_base = 0;
    g->finally_count = 0;
    g->finally_loop_base = 0;
    g->pending = (zan_irgen_pending_context_t){0};
    g->break_target = NULL;
    g->continue_target = NULL;
    g->loop_locals_base = 0;
    g->loop_catch_base = 0;
    g->eh_armed_base = g->eh_armed_count;
    g->eh_armed_loop_base = g->eh_armed_count;
    g->current_this = NULL;
    g->current_async_frame = NULL;
    g->lambda_depth++;

    local_scope_t lambda_locals;
    local_scope_init(&lambda_locals, g->arena);
    /* 捕获变量排布于闭包首部，进入函数体时从环境记录中重载为局部变量 */
    if (is_closure) {
        LLVMValueRef rec = LLVMGetParam(lambda_fn, 0);
        for (int i = 0; i < capc; i++) {
            LLVMValueRef p = LLVMBuildStructGEP2(g->builder, rec_ty, rec,
                (unsigned)(ZAN_CLOSURE_HDR_FIELDS + i), "cap.p");
            LLVMValueRef v = LLVMBuildLoad2(g->builder, cs.caps[i].llvm, p, "cap");
            if (cs.caps[i].boxed) {
                /*
                 * the record holds the variable's cell: bind the body's local
                 * to the value inside it, so writes here are writes there
                 */
                LLVMTypeRef payload = map_type(g, cs.caps[i].type);
                local_add(&lambda_locals, cs.caps[i].name,
                          box_value_ptr(g, v, payload), cs.caps[i].type);
                /* 闭包持有被捕获引用的所有权，调用方借用访问 */
                lambda_locals.vars[lambda_locals.count - 1].box_cell = v;
                /*
                 * an rc-managed variable is owned by the cell wherever it is
                 * written, so a write here swaps the reference too
                 */
                if (is_rc_managed_type(cs.caps[i].type))
                    lambda_locals.vars[lambda_locals.count - 1].arc_owned = 1;
                if (type_contains_collection_rc(g, cs.caps[i].type, 0))
                    lambda_locals.vars[lambda_locals.count - 1].struct_rc = 1;
                continue;
            }
            LLVMValueRef alloc = LLVMBuildAlloca(g->builder, cs.caps[i].llvm, "cap.slot");
            LLVMBuildStore(g->builder, v, alloc);
            local_add(&lambda_locals, cs.caps[i].name, alloc, cs.caps[i].type);
        }
        if (has_this) {
            LLVMValueRef p = LLVMBuildStructGEP2(g->builder, rec_ty, rec,
                (unsigned)(ZAN_CLOSURE_HDR_FIELDS + capc), "cap.thisp");
            LLVMValueRef v = LLVMBuildLoad2(g->builder, i8ptr, p, "cap.this");
            LLVMValueRef alloc = LLVMBuildAlloca(g->builder, i8ptr, "this.slot");
            LLVMBuildStore(g->builder, v, alloc);
            g->current_this = alloc;
        }
    }
    for (int k = 0; k < pc; k++) {
        zan_ast_node_t *param = expr->lambda.params.items[k];
        LLVMTypeRef lt = param_types[k];
        LLVMValueRef pv = LLVMGetParam(lambda_fn,
            (unsigned)(is_closure ? k + 1 : k));
        LLVMValueRef alloc = LLVMBuildAlloca(g->builder, lt, "lp");
        zan_store_fit(g, pv, alloc);
        zan_type_t *pt = ptypes[k] ? ptypes[k] : g->binder->type_int;
        local_add(&lambda_locals, param->param.name, alloc, pt);
        box_captured_parameter(g, &lambda_locals, param, pt, lt, pv,
                               expr->lambda.body);
    }

    if (expr->lambda.body && expr->lambda.body->kind == AST_BLOCK) {
        emit_stmt(g, expr->lambda.body, &lambda_locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            if (ret_void) LLVMBuildRetVoid(g->builder);
            else LLVMBuildRet(g->builder, LLVMConstNull(ret_type));
        }
    } else if (expr->lambda.body) {
        check_implicit_narrowing(g, rett,
            infer_expr_type(g, expr->lambda.body, &lambda_locals),
            expr->lambda.body, "return");
        LLVMValueRef result = emit_expr(g, expr->lambda.body, &lambda_locals);
        if (ret_void) {
            LLVMBuildRetVoid(g->builder);
        } else if (rett) {
            /* 表达式主体 Lambda 返回托管引用时按规则保留引用计数 */
            if (is_rc_managed_type(rett) &&
                !expr_yields_owned_rc_value(g, expr->lambda.body, &lambda_locals)) {
                emit_rc_retain_for_type(g, rett, result);
            }
            result = emit_boundary_coerce(g, result, ret_type);
            LLVMBuildRet(g->builder, result);
        } else {
            result = coerce_to_i64(g, result);
            LLVMBuildRet(g->builder, result);
        }
    } else {
        if (ret_void) LLVMBuildRetVoid(g->builder);
        else LLVMBuildRet(g->builder, LLVMConstNull(ret_type));
    }

    g->current_fn = saved_fn;
    g->current_fn_is_main = saved_fn_is_main;
    g->current_fn_ret_type = saved_fn_ret;
    g->current_fn_zan_ret_type = saved_fn_zan_ret;
    g->throw_locals_base = saved_throw_base;
    g->catch_cleanup_count = saved_catch_cc;
    g->throw_catch_base = saved_throw_cb;
    g->finally_count = saved_fin_c;
    g->finally_loop_base = saved_fin_lb;
    g->pending = saved_pending;
    g->break_target = saved_break;
    g->continue_target = saved_continue;
    g->loop_locals_base = saved_loop_base;
    g->loop_catch_base = saved_loop_cbase;
    if (saved_fin_c && saved_finallys) {
        memcpy(g->finallys, saved_finallys,
               sizeof(g->finallys[0]) * (size_t)saved_fin_c);
    }
    if (saved_catch_cc && saved_catches) {
        memcpy(g->catch_cleanups, saved_catches,
               sizeof(g->catch_cleanups[0]) * (size_t)saved_catch_cc);
    }
    g->eh_armed_count = saved_eh_c;
    g->eh_armed_base = saved_eh_b;
    g->eh_armed_loop_base = saved_eh_lb;
    g->current_this = saved_this;
    g->current_async_frame = saved_async_frame;
    g->current_fn_body = saved_fn_body;
    g->lambda_depth--;
    LLVMPositionBuilderAtEnd(g->builder, saved_bb);
    /* 返回外层函数上下文：恢复外层调试位置 */
    if (g->emit_debug) {
        LLVMSetCurrentDebugLocation2(g->builder, saved_dl);
        g->di_cur_line = saved_di_line;
        g->di_cur_file = saved_di_file;
    }
    free(param_types);
    free(ptypes);

    if (!is_closure) {
        cap_scan_free(&cs);
        return lambda_fn;
    }

    LLVMValueRef self = has_this
        ? LLVMBuildLoad2(g->builder, LLVMGetAllocatedType(saved_this),
                         saved_this, "cap.self")
        : NULL;
    LLVMValueRef clo = emit_closure_record(g, expr->loc, lname, rec_ty,
                                           lambda_fn, NULL, cs.caps, capc,
                                           has_this, self, true);
    cap_scan_free(&cs);
    return clo;
}

static zan_type_t *method_param_type(zan_irgen_t *g, zan_symbol_t *msym, int idx) {
    if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return NULL;
    zan_ast_list_t *params = &msym->decl->method_decl.params;
    if (idx < 0 || idx >= params->count) return NULL;
    zan_ast_node_t *p = params->items[idx];
    if (!p || !p->param.type) return NULL;
    /* 优先使用绑定器在声明作用域内绑定的形参符号 */
    for (int i = 0; i < msym->member_count; i++) {
        zan_symbol_t *ps = msym->members[i];
        if (ps && ps->kind == SYM_PARAM && ps->decl == p) return ps->type;
    }
    return zan_binder_resolve_type(g->binder, p->param.type);
}

/* True when `t` mentions one of the method's own type parameters. */
static bool type_mentions_tp(zan_type_t *t) {
    if (!t) return false;
    if (t->kind == TYPE_TYPE_PARAM) return true;
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE)
        return type_mentions_tp(t->element_type);
    if (t->kind == TYPE_DELEGATE) {
        if (type_mentions_tp(t->delegate_ret_type)) return true;
        for (int i = 0; i < t->delegate_param_count; i++)
            if (type_mentions_tp(t->delegate_param_types[i])) return true;
    }
    for (int i = 0; i < t->type_arg_count; i++)
        if (type_mentions_tp(t->type_args[i])) return true;
    return false;
}

/*
 * Structurally match a declared (possibly type-parameterised) type against a
 * concrete argument type, recording each type parameter's binding.
 */
static void unify_method_tp(zan_type_t *dp, zan_type_t *at,
                            zan_ast_list_t *tps, zan_type_t **bind) {
    if (!dp || !at) return;
    if (dp->kind == TYPE_TYPE_PARAM) {
        for (int i = 0; i < tps->count; i++) {
            zan_istr_t tn = tps->items[i]->ident.name;
            if (tn.len == dp->name.len &&
                memcmp(tn.str, dp->name.str, (size_t)dp->name.len) == 0) {
                if (!bind[i]) bind[i] = at;
                return;
            }
        }
        return;
    }
    if ((dp->kind == TYPE_ARRAY || dp->kind == TYPE_NULLABLE) &&
        dp->kind == at->kind) {
        unify_method_tp(dp->element_type, at->element_type, tps, bind);
        return;
    }
    for (int i = 0; i < dp->type_arg_count && i < at->type_arg_count; i++)
        unify_method_tp(dp->type_args[i], at->type_args[i], tps, bind);
}

/*
 * Substitute a generic method's own type parameters in `t` using `bind`,
 * cloning composite types (delegates, generic instantiations) as needed.
 */
static zan_type_t *subst_method_tp(zan_irgen_t *g, zan_type_t *t,
                                   zan_ast_list_t *tps, zan_type_t **bind) {
    if (!t || !type_mentions_tp(t)) return t;
    if (t->kind == TYPE_TYPE_PARAM) {
        for (int i = 0; i < tps->count; i++) {
            zan_istr_t tn = tps->items[i]->ident.name;
            if (bind[i] && tn.len == t->name.len &&
                memcmp(tn.str, t->name.str, (size_t)t->name.len) == 0)
                return bind[i];
        }
        return t;
    }
    zan_type_t *nt = (zan_type_t *)zan_arena_alloc(g->arena, sizeof(zan_type_t));
    *nt = *t;
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE) {
        nt->element_type = subst_method_tp(g, t->element_type, tps, bind);
        return nt;
    }
    if (t->kind == TYPE_DELEGATE) {
        nt->delegate_ret_type = subst_method_tp(g, t->delegate_ret_type, tps, bind);
        if (t->delegate_param_count > 0) {
            nt->delegate_param_types = (zan_type_t **)zan_arena_alloc(
                g->arena, sizeof(zan_type_t *) * (size_t)t->delegate_param_count);
            for (int i = 0; i < t->delegate_param_count; i++)
                nt->delegate_param_types[i] =
                    subst_method_tp(g, t->delegate_param_types[i], tps, bind);
        }
    }
    if (t->type_arg_count > 0) {
        nt->type_args = (zan_type_t **)zan_arena_alloc(
            g->arena, sizeof(zan_type_t *) * (size_t)t->type_arg_count);
        for (int i = 0; i < t->type_arg_count; i++)
            nt->type_args[i] = subst_method_tp(g, t->type_args[i], tps, bind);
    }
    return nt;
}

/* 泛型方法调用点推导类型实参绑定 */
static void infer_method_tp_bindings(zan_irgen_t *g, zan_symbol_t *msym,
                                     zan_ast_node_t *call,
                                     zan_ast_node_t *recv_expr,
                                     local_scope_t *locals,
                                     zan_type_t **bind) {
    zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
    for (int i = 0; i < tps->count && i < call->call.type_args.count; i++)
        bind[i] = resolve_type_ctx(g, call->call.type_args.items[i]);

    zan_ast_list_t *params = &msym->decl->method_decl.params;
    int arg_base = recv_expr ? 1 : 0;
    for (int j = 0; j < params->count; j++) {
        zan_ast_node_t *aexpr = NULL;
        if (arg_base == 1 && j == 0) aexpr = recv_expr;
        else if (j - arg_base < call->call.args.count)
            aexpr = call->call.args.items[j - arg_base];
        if (!aexpr || aexpr->kind == AST_LAMBDA) continue;
        zan_type_t *dp = method_param_type(g, msym, j);
        if (!dp || !type_mentions_tp(dp)) continue;
        zan_type_t *at = infer_expr_type(g, aexpr, locals);
        if (at && !type_mentions_tp(at)) unify_method_tp(dp, at, tps, bind);
    }

    /* 第二遍推导：根据委托形参签名推导 Lambda 参数与返回类型 */
    for (int j = 0; j < params->count; j++) {
        int ai = j - arg_base;
        if (ai < 0 || ai >= call->call.args.count) continue;
        zan_ast_node_t *a = call->call.args.items[ai];
        if (!a || a->kind != AST_LAMBDA) continue;
        zan_type_t *dp = method_param_type(g, msym, j);
        if (!dp || dp->kind != TYPE_DELEGATE || !type_mentions_tp(dp)) continue;
        if (a->lambda.params.count != dp->delegate_param_count) continue;
        zan_type_t *sdp = subst_method_tp(g, dp, tps, bind);
        int mark = locals->count;
        bool params_known = true;
        for (int k = 0; k < a->lambda.params.count; k++) {
            zan_ast_node_t *lp = a->lambda.params.items[k];
            zan_type_t *lpt = lp->param.type
                ? zan_binder_resolve_type(g->binder, lp->param.type)
                : sdp->delegate_param_types[k];
            if (lpt && !type_mentions_tp(lpt)) {
                if (lp->param.type)
                    unify_method_tp(dp->delegate_param_types[k], lpt, tps, bind);
                local_add(locals, lp->param.name, NULL, lpt);
            } else {
                params_known = false;
            }
        }
        if (params_known &&
            type_mentions_tp(dp->delegate_ret_type) &&
            a->lambda.body && a->lambda.body->kind != AST_BLOCK) {
            zan_type_t *bt = infer_expr_type(g, a->lambda.body, locals);
            if (!bt || type_mentions_tp(bt)) {
                switch (expr_family(g, a->lambda.body, locals)) {
                case FAM_BOOL: bt = g->binder->type_bool; break;
                case FAM_INT: bt = g->binder->type_int; break;
                case FAM_FLOAT: bt = g->binder->type_double; break;
                case FAM_STRING: bt = g->binder->type_string; break;
                default: bt = NULL; break;
                }
            }
            if (bt && !type_mentions_tp(bt))
                unify_method_tp(dp->delegate_ret_type, bt, tps, bind);
        }
        locals->count = mark;
    }
}

/* 泛型形参类型实参代换 */
static zan_type_t *method_param_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                        int idx, zan_ast_node_t *call,
                                        zan_ast_node_t *recv_expr,
                                        local_scope_t *locals) {
    zan_type_t *pt = method_param_type(g, msym, idx);
    if (!pt) return pt;
    /* 外层泛型类类型形参代换 */
    if (pt->kind == TYPE_DELEGATE) {
        zan_type_t *recv = recv_expr ? infer_expr_type(g, recv_expr, locals)
                                     : g->cur_inst;
        if (recv && recv->type_arg_count > 0)
            pt = subst_delegate_sig(g, pt, recv);
    }
    if (!msym->decl || msym->decl->kind != AST_METHOD_DECL) return pt;
    zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
    if (tps->count == 0 || tps->count > 8) return pt;
    if (!type_mentions_tp(pt)) return pt;
    if (!call || call->kind != AST_CALL) return pt;

    zan_type_t *bind[8] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
    infer_method_tp_bindings(g, msym, call, recv_expr, locals, bind);
    return subst_method_tp(g, pt, tps, bind);
}

/* 判断方法返回类型是否为该方法自身声明的泛型类型形参 */
static bool method_ret_is_bare_tp(zan_symbol_t *msym) {
    if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return false;
    zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
    if (tps->count == 0) return false;
    zan_ast_node_t *ret_ref = msym->decl->method_decl.return_type;
    if (!ret_ref || ret_ref->kind != AST_TYPE_REF) return false;
    if (ret_ref->type_ref.type_args.count > 0 || ret_ref->type_ref.is_array)
        return false;
    zan_istr_t rn = ret_ref->type_ref.name;
    for (int i = 0; i < tps->count; i++) {
        zan_istr_t tn = tps->items[i]->ident.name;
        if (tn.len == rn.len && memcmp(tn.str, rn.str, (size_t)rn.len) == 0)
            return true;
    }
    return false;
}

/*
 * Declared return type of a generic method with its type parameters
 * substituted for this call site (identity for non-generic methods).
 */
static zan_type_t *method_ret_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                      zan_ast_node_t *call,
                                      zan_ast_node_t *recv_expr,
                                      local_scope_t *locals) {
    zan_type_t *rt = msym ? msym->type : NULL;
    if (!rt || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return rt;
    zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
    if (tps->count == 0 || tps->count > 8) return rt;
    if (!type_mentions_tp(rt)) return rt;
    if (!call || call->kind != AST_CALL) return rt;
    zan_type_t *bind[8] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
    infer_method_tp_bindings(g, msym, call, recv_expr, locals, bind);
    return subst_method_tp(g, rt, tps, bind);
}

/* async 委托调用：调用生成协程帧并在原地 await 挂起等待结果 */
static void check_delegate_async_match(zan_irgen_t *g, zan_ast_node_t *e,
                                       zan_type_t *dt, local_scope_t *locals) {
    if (!e || !dt || dt->kind != TYPE_DELEGATE) return;
    zan_symbol_t *m = NULL;
    if (e->kind == AST_IDENTIFIER) {
        if (local_find(locals, e->ident.name)) return;
        if (g->current_type_sym)
            m = get_method_sym(g->current_type_sym, e->ident.name);
    } else if (e->kind == AST_MEMBER_ACCESS && e->member.object &&
               e->member.object->kind == AST_IDENTIFIER &&
               !local_find(locals, e->member.object->ident.name)) {
        zan_symbol_t *cs =
            zan_binder_lookup(g->binder, e->member.object->ident.name);
        if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT))
            m = get_method_sym(cs, e->member.name);
    }
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) return;
    int m_async = (m->decl->method_decl.modifiers & MOD_ASYNC) != 0;
    if (m_async == (dt->delegate_is_async != 0)) return;
    zan_diag_emit(g->diag, DIAG_ERROR, e->loc,
        "cannot convert %s method '%.*s' to %s delegate '%.*s'",
        m_async ? "async" : "non-async", m->name.len, m->name.str,
        dt->delegate_is_async ? "async" : "non-async",
        dt->name.len, dt->name.str);
}

static zan_ast_node_t *ast_type_from_zan_type(zan_irgen_t *g,
                                                zan_type_t *type,
                                                zan_loc_t loc) {
    if (!g || !type) return NULL;
    zan_ast_node_t *ref = zan_ast_new(g->arena, AST_TYPE_REF, loc);
    ref->type_ref.name = type->name;
    zan_ast_list_init(&ref->type_ref.type_args);
    ref->type_ref.is_nullable = false;
    ref->type_ref.is_array = false;
    for (int i = 0; i < type->type_arg_count; i++) {
        zan_ast_node_t *arg = ast_type_from_zan_type(g, type->type_args[i], loc);
        if (!arg) return NULL;
        zan_ast_list_push(&ref->type_ref.type_args, arg, g->arena);
    }
    return ref;
}

/* 原地改写构造实参以复用类构造管线 */
static bool wrap_implicit_ctor_arg(zan_irgen_t *g, zan_ast_node_t *arg,
                                   zan_type_t *ptype) {
    if (!g || !arg || !ptype) return false;
    zan_ast_node_t *type_ref = ast_type_from_zan_type(g, ptype, arg->loc);
    if (!type_ref) return false;
    zan_ast_node_t *original = (zan_ast_node_t *)zan_arena_alloc(
        g->arena, sizeof(zan_ast_node_t));
    if (!original) return false;
    *original = *arg;
    zan_ast_node_t replacement;
    memset(&replacement, 0, sizeof(replacement));
    replacement.kind = AST_NEW_EXPR;
    replacement.loc = arg->loc;
    replacement.new_expr.type = type_ref;
    replacement.new_expr.is_array = false;
    replacement.new_expr.array_init = false;
    zan_ast_list_init(&replacement.new_expr.args);
    zan_ast_list_push(&replacement.new_expr.args, original, g->arena);
    *arg = replacement;
    /* the node now describes a different expression at the same address */
    infer_cache_invalidate();
    return true;
}

static LLVMValueRef emit_arg_typed(zan_irgen_t *g, zan_ast_node_t *arg,
                                   zan_type_t *ptype, local_scope_t *locals) {
    zan_type_t *atype = infer_expr_type(g, arg, locals);
    check_implicit_narrowing(g, ptype, atype, arg, "argument");
    check_value_type_mismatch(g, ptype, atype, arg, "argument");
    check_generic_invariance(g, ptype, atype, arg, "argument");
    if (ptype && atype && ptype->kind == TYPE_CLASS &&
        is_implicit_ctor_source(atype)) {
        if (implicit_ctor_for_arg(g, ptype, atype, arg, locals)) {
            wrap_implicit_ctor_arg(g, arg, ptype);
            return emit_expr(g, arg, locals);
        }
        zan_diag_emit(g->diag, DIAG_ERROR, arg->loc,
            "cannot convert '%s' to '%s' in argument: no matching constructor",
            atype->name.str ? atype->name.str : "?",
            ptype->name.str ? ptype->name.str : "?");
        return LLVMConstNull(map_type(g, ptype));
    }
    if (arg && ptype && ptype->kind == TYPE_DELEGATE) {
        if (arg->kind == AST_LAMBDA)
            return emit_lambda_typed(g, arg, ptype, locals);
        check_delegate_async_match(g, arg, ptype, locals);
    }
    LLVMValueRef v = emit_expr(g, arg, locals);
    /*
     * user-defined implicit conversion in an argument: `Show(f)` where the
     * parameter is double and Feet declares `implicit operator double` (B12).
     */
    if (v && ptype && atype)
        v = emit_user_conversion(g, atype, ptype, "op_implicit", v, arg, locals);
    /*
     * An integer meeting a float/double parameter is converted, not passed as
     * its bit pattern.
     */
    if (v && ptype && (ptype->kind == TYPE_DOUBLE || ptype->kind == TYPE_FLOAT) &&
        LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(LLVMTypeOf(v)) > 1)
        v = coerce_int_to(g, v, map_type(g, ptype));
    /* `f(5)` / `f(null)` against a `T?` parameter passes the wrapped value. */
    if (v && ptype && ptype->kind == TYPE_NULLABLE)
        v = coerce_int_to(g, v, map_type(g, ptype));
    return v;
}

/* ref / out 实参处理：传递局部变量或存储槽的内存地址 */
static LLVMValueRef emit_ref_lvalue_ptr(zan_irgen_t *g, zan_ast_node_t *tgt,
                                        local_scope_t *locals) {
    if (!tgt) return NULL;
    if (tgt->kind == AST_IDENTIFIER) {
        local_var_t *local = local_find(locals, tgt->ident.name);
        if (local) return local->alloca;
        if (g->current_type_sym) {
            zan_symbol_t *field = get_field_sym(g->current_type_sym, tgt->ident.name);
            LLVMValueRef global = field ? get_static_field_global(g,
                g->current_type_sym, field, NULL) : NULL;
            if (global) return global;
            int fi = get_field_index(g->current_type_sym, tgt->ident.name);
            LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
            if (fi >= 0 && st && g->current_this) {
                LLVMValueRef self = LLVMBuildLoad2(g->builder,
                    LLVMPointerType(st, 0), g->current_this, "ref.this");
                return emit_field_ptr(g, g->current_type_sym, st, self, fi, "ref.fld");
            }
        }
        return NULL;
    }

    if (tgt->kind == AST_MEMBER_ACCESS && !tgt->member.null_cond) {
        zan_ast_node_t *obj_expr = tgt->member.object;
        /* ClassName.StaticField: the backing global IS the slot. */
        if (obj_expr->kind == AST_IDENTIFIER &&
            !local_find(locals, obj_expr->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder, obj_expr->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *fs = get_field_sym(cs, tgt->member.name);
                LLVMValueRef gv = fs ? get_static_field_global(g, cs, fs,
                    static_access_inst(g, obj_expr)) : NULL;
                if (gv) return gv;
            }
        }
        /* obj.Field: the field slot inside the object's storage. */
        zan_symbol_t *cls = expr_class_sym(g, obj_expr, locals);
        if (cls) {
            int fi = get_field_index(cls, tgt->member.name);
            LLVMTypeRef st = get_struct_llvm_type(g, cls);
            if (fi >= 0 && st) {
                /* Loading a struct receiver would mutate a discarded copy. */
                LLVMValueRef obj_val = cls->kind == SYM_STRUCT
                    ? emit_ref_lvalue_ptr(g, obj_expr, locals)
                    : emit_guarded_member_object(g, tgt, locals);
                if (obj_val &&
                    LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                    if (LLVMTypeOf(obj_val) != LLVMPointerType(st, 0))
                        obj_val = LLVMBuildBitCast(g->builder, obj_val,
                            LLVMPointerType(st, 0), "ref.obj");
                    return emit_field_ptr(g, cls, st, obj_val, fi, "ref.fld");
                }
            }
        }
        return NULL;
    }

    /* 集合/数组/Span 元素取地址传参 */
    if (tgt->kind == AST_INDEX)
        return emit_struct_elem_ptr(g, tgt, locals);

    return NULL;
}

/*
 * `ref x` / `out x` / `out T x` argument: pass the address of the local's
 * storage slot. An inline `out T x` declares a fresh zero-initialised local
 * in the caller's scope first.
 */
static LLVMValueRef emit_ref_arg(zan_irgen_t *g, zan_ast_node_t *arg,
                                 local_scope_t *locals) {
    zan_ast_node_t *tgt = arg->ref_arg.expr;
    if (arg->ref_arg.decl_type && tgt && tgt->kind == AST_IDENTIFIER) {
        zan_type_t *dt = resolve_type_ctx(g, arg->ref_arg.decl_type);
        LLVMTypeRef lt = map_type(g, dt);
        LLVMValueRef a = emit_entry_alloca(g, lt, "out");
        zan_store_fit(g, LLVMConstNull(lt), a);
        local_add(locals, tgt->ident.name, a, dt);
        return a;
    }
    if (tgt && tgt->kind == AST_IDENTIFIER) {
        local_var_t *l = local_find(locals, tgt->ident.name);
        if (l) return l->alloca;
        /*
         * bare name of an instance field of the enclosing class: `out field`
         * means `out this.field`.
         */
        if (g->current_type_sym) {
            zan_symbol_t *fs = get_field_sym(g->current_type_sym,
                                             tgt->ident.name);
            LLVMValueRef gv = fs ? get_static_field_global(g,
                g->current_type_sym, fs, NULL) : NULL;
            if (gv) return gv;
            int fi = get_field_index(g->current_type_sym, tgt->ident.name);
            LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
            if (fi >= 0 && st && g->current_this) {
                LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                    LLVMPointerType(st, 0), g->current_this, "this");
                return emit_field_ptr(g, g->current_type_sym, st, this_ptr,
                                      fi, "ref.this");
            }
        }
    }
    /* 位置表达式 (obj.field, arr[i]) 取地址传参 */
    LLVMValueRef place = emit_ref_lvalue_ptr(g, tgt, locals);
    if (place) {
        if (LLVMGetTypeKind(LLVMTypeOf(place)) != LLVMPointerTypeKind)
            return LLVMBuildIntToPtr(g->builder, place,
                LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0), "ref.ip");
        return place;
    }
    zan_diag_emit(g->diag, DIAG_ERROR, arg->loc,
        "ref/out argument must be a variable, a field, or an element");
    return emit_expr(g, tgt, locals);
}
