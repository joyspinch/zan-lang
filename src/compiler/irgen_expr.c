/* irgen_expr.c: 表达式代码生成、NativeMemory 原语、Lambda 发射与泛型方法推导 (由 irgen.c 包含) */

static LLVMValueRef get_async_result_ptr(zan_irgen_t *g);
static LLVMValueRef get_async_sub_slot_ptr(zan_irgen_t *g);

/* 实参里"拥有型临时量"的 EH 临时槽登记（定义在 irgen_call.c，晚于本文件
   被包含）：普通方法调用路径用它保证"调用抛异常时由展开器释放临时量"，
   构造路径此前漏了这一步，构造函数一抛，实参里的临时量就泄漏了。 */
static int emit_call_arg_eh_push(zan_irgen_t *g, zan_ast_node_t *arg,
                                 LLVMValueRef val, local_scope_t *locals);

/* NativeMemory 原语：堆外内存操作，地址使用 nint (i64)，不计入 ARC */

/* 将实例接收者绑定至方法组闭包记录；recv 为 NULL 时生成静态方法组 */
static LLVMValueRef emit_method_group_closure(zan_irgen_t *g, zan_symbol_t *msym,
                                              LLVMValueRef recv, zan_loc_t loc);

/* 静态方法组记录构建器 */
static LLVMValueRef build_static_mg_record(zan_irgen_t *g, zan_loc_t loc,
                                           const char *lname, LLVMTypeRef rec_ty,
                                           LLVMValueRef thunk);

/* 合成的 Binding<T> 访问器委托：原生平台使用纯函数指针，wasm32 采用静态方法组记录 */
static LLVMValueRef emit_binding_acc_delegate(zan_irgen_t *g, LLVMValueRef acc,
                                              LLVMTypeRef acc_ty);

/* 检查类或其基类中是否声明了指定名称的成员（字段/方法/属性/事件/常量） */
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

/* 计算内存地址偏移指针 (base + offset) */
static LLVMValueRef nm_addr(zan_irgen_t *g, LLVMValueRef base, LLVMValueRef off) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMValueRef p = LLVMBuildIntToPtr(g->builder, base, i8ptr, "nm.p");
    return LLVMBuildGEP2(g->builder, i8, p, &off, 1, "nm.at");
}

/* 将发射的实参整型符号扩展或截断至 i64 */
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

/* CRC32C (Castagnoli) 查表法内部实现 */
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
    /* 长度下溢校验：拒绝负数长度防止堆分配溢出 */
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

    /* 连续内存块判断：步长与行宽一致时走高速拷贝 */
    LLVMPositionBuilderAtEnd(g->builder, check_fast);
    LLVMValueRef eq_dst = zan_icmp(g->builder, LLVMIntEQ, dst_stride, row_bytes, "eq_dst");
    LLVMValueRef eq_src = zan_icmp(g->builder, LLVMIntEQ, src_stride, row_bytes, "eq_src");
    LLVMValueRef is_fast = zan_and(g->builder, eq_dst, eq_src, "is_fast");
    LLVMBuildCondBr(g->builder, is_fast, fast_path, slow_loop);

    LLVMTypeRef memmove_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    LLVMValueRef memmove_fn = get_libc_fn(g, "memmove", memmove_ty);

    /* 快路径：单次 memmove 连续拷贝整块像素数据 */
    LLVMPositionBuilderAtEnd(g->builder, fast_path);
    LLVMValueRef total_bytes = zan_mul(g->builder, row_bytes, height, "total_b");
    zan_call2(g->builder, memmove_ty, memmove_fn, (LLVMValueRef[]){ dst, src, total_bytes }, 3, "");
    LLVMBuildBr(g->builder, done);

    /* 慢路径：逐行步进跨步拷贝 */
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

static LLVMTypeRef span_elem_storage_type(zan_irgen_t *g, zan_type_t *elem_type) {
    if (!elem_type) return LLVMInt32TypeInContext(g->ctx);
    if (elem_type->kind == TYPE_CHAR) return LLVMInt8TypeInContext(g->ctx);
    return map_type(g, elem_type);
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
        if (!ot) return false;
        LLVMValueRef base = NULL;
        LLVMValueRef len = NULL;
        LLVMTypeRef elem_llvm = NULL;
        if (ot->kind == TYPE_ARRAY && ot->element_type) {
            LLVMValueRef arr = emit_expr(g, callee->member.object, locals);
            base = LLVMBuildBitCast(g->builder, arr, i8ptr, "span.base");
            len = zan_array_len(g, arr);
            elem_llvm = span_elem_storage_type(g, ot->element_type);
        } else if (ot->kind == TYPE_STRING) {
            LLVMValueRef sobj = emit_expr(g, callee->member.object, locals);
            base = emit_str_nonnull(g, sobj);
            len = emit_string_length(g, base, callee->loc);
            elem_llvm = span_elem_storage_type(g, g->binder->type_char);
        } else {
            return false;
        }
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
        LLVMTypeRef elem_llvm = span_elem_storage_type(g, elem);
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
    /* 释放接收者数组所有权，防止链式调用内存泄漏 */
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
        /* 负数分配大小校验：直接返回空指针 0 */
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

    /* AsI32 / AsF32 / AsI64 / AsF64: 浮点与整数标量之间的零开销二进制重解释位转换 */
    if (is_call_to(expr, "NativeMemory", "AsI32") &&
        expr->call.args.count == 1) {
        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
        LLVMValueRef v = emit_expr(g, expr->call.args.items[0], locals);
        *out = LLVMBuildBitCast(g->builder, v, i32t, "nm.asi32");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "AsF32") &&
        expr->call.args.count == 1) {
        LLVMTypeRef fltt = LLVMFloatTypeInContext(g->ctx);
        LLVMValueRef v = coerce_int_to(g, emit_expr(g, expr->call.args.items[0], locals), i32t);
        *out = LLVMBuildBitCast(g->builder, v, fltt, "nm.asf32");
        return true;
    }
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
        /* 长度合法性校验：拒绝负数防止越界内存拷贝 */
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
    if (is_call_to(expr, "NativeMemory", "JsonParseFast") && expr->call.args.count == 7) {
        LLVMValueRef buf = nm_arg(g, expr, 0, locals);
        LLVMValueRef len = nm_arg(g, expr, 1, locals);
        LLVMValueRef slots = nm_arg(g, expr, 2, locals);
        LLVMValueRef slot_cap = nm_arg(g, expr, 3, locals);
        LLVMValueRef pairs = nm_arg(g, expr, 4, locals);
        LLVMValueRef pair_cap = nm_arg(g, expr, 5, locals);
        LLVMValueRef counts = nm_arg(g, expr, 6, locals);

        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i8ptr, i64t, i8ptr, i64t, i8ptr, i64t, i8ptr }, 7, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_json_parse_fast");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_json_parse_fast", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, buf, zero64), len,
            nm_addr(g, slots, zero64), slot_cap,
            nm_addr(g, pairs, zero64), pair_cap,
            nm_addr(g, counts, zero64)
        }, 7, "nm.json_parse_fast");
        return true;
    }
    /* AtomicInt 核心操作一律内联发射为单指令硬件原子操作，消除 C ABI 跨语言调用开销 */
    #define IS_ATOMIC_INT(m) (is_call_to(expr, "AtomicInt", m) || \
        (expr->kind == AST_CALL && expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER && \
         (int)expr->call.callee->ident.name.len == (int)strlen(m) && \
         memcmp(expr->call.callee->ident.name.str, m, (size_t)expr->call.callee->ident.name.len) == 0))

    if (IS_ATOMIC_INT("NativeLoad") && expr->call.args.count == 1) {
        LLVMValueRef handle = nm_arg(g, expr, 0, locals);
        LLVMValueRef ptr = LLVMBuildIntToPtr(g->builder, handle, LLVMPointerType(i64t, 0), "atom.ptr");
        LLVMValueRef val = LLVMBuildLoad2(g->builder, i64t, ptr, "atom.load");
        LLVMSetOrdering(val, LLVMAtomicOrderingSequentiallyConsistent);
        *out = val;
        return true;
    }
    if (IS_ATOMIC_INT("NativeStore") && expr->call.args.count == 2) {
        LLVMValueRef handle = nm_arg(g, expr, 0, locals);
        LLVMValueRef val = nm_arg(g, expr, 1, locals);
        LLVMValueRef ptr = LLVMBuildIntToPtr(g->builder, handle, LLVMPointerType(i64t, 0), "atom.ptr");
        LLVMValueRef st = LLVMBuildStore(g->builder, val, ptr);
        LLVMSetOrdering(st, LLVMAtomicOrderingSequentiallyConsistent);
        *out = zero64;
        return true;
    }
    if (IS_ATOMIC_INT("NativeAdd") && expr->call.args.count == 2) {
        LLVMValueRef handle = nm_arg(g, expr, 0, locals);
        LLVMValueRef delta = nm_arg(g, expr, 1, locals);
        LLVMValueRef ptr = LLVMBuildIntToPtr(g->builder, handle, LLVMPointerType(i64t, 0), "atom.ptr");
        LLVMValueRef old = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpAdd, ptr, delta,
                                              LLVMAtomicOrderingSequentiallyConsistent, false);
        *out = LLVMBuildAdd(g->builder, old, delta, "atom.add.res");
        return true;
    }
    if (IS_ATOMIC_INT("NativeExchange") && expr->call.args.count == 2) {
        LLVMValueRef handle = nm_arg(g, expr, 0, locals);
        LLVMValueRef val = nm_arg(g, expr, 1, locals);
        LLVMValueRef ptr = LLVMBuildIntToPtr(g->builder, handle, LLVMPointerType(i64t, 0), "atom.ptr");
        *out = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpXchg, ptr, val,
                                  LLVMAtomicOrderingSequentiallyConsistent, false);
        return true;
    }
    if (IS_ATOMIC_INT("NativeCompareExchange") && expr->call.args.count == 3) {
        LLVMValueRef handle = nm_arg(g, expr, 0, locals);
        LLVMValueRef expected = nm_arg(g, expr, 1, locals);
        LLVMValueRef desired = nm_arg(g, expr, 2, locals);
        LLVMValueRef ptr = LLVMBuildIntToPtr(g->builder, handle, LLVMPointerType(i64t, 0), "atom.ptr");
        LLVMValueRef pair = LLVMBuildAtomicCmpXchg(g->builder, ptr, expected, desired,
                                                   LLVMAtomicOrderingSequentiallyConsistent,
                                                   LLVMAtomicOrderingSequentiallyConsistent, false);
        *out = LLVMBuildExtractValue(g->builder, pair, 0, "atom.cmpxchg.old");
        return true;
    }
    #undef IS_ATOMIC_INT
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
            /* SWAR 等值掩码：不产生 <32 x i1>（A439）。 */
            *out = v32i8_to_vec256(g, emit_byte_eq_mask_i64(g, a32, b32, 32));
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
            if (emit_target_has_avx2(g)) {
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

    /* 向量条件掩码选择指令发射 */
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

    /* 硬件预取指令发射 (llvm.prefetch) */
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
            /* 数组源元素索引计算，对齐 arr[i] 寻址规则 */
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
        /* 恒走 SWAR：llvm.x86.avx2.pmovmskb 在通用 CPU（无 AVX2）上
         * 是不可合法化的调用，emit-ir 产物会因此过不了 llc（A439）。 */
        *out = emit_pmovmskb_portable(g, a32, 32);
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
        LLVMValueRef a16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[0], locals));
        LLVMValueRef b16 = vec128_to_v16i8(g, emit_expr(g, expr->call.args.items[1], locals));
        /* SWAR 等值掩码：不产生 <16 x i1>（A439）。 */
        *out = v16i8_to_vec128(g, emit_byte_eq_mask_i64(g, a16, b16, 16));
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

/* ARM64 硬件加密内建函数声明与发射 */
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
                zan_diag_emit_code(g->diag, DIAG_ERROR,
                              obj_ast ? obj_ast->loc : zan_loc(0, 0, 0, 0), 3000,
                              "property getter requires a receiver");
                return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
            }
            if (!is_static &&
                LLVMGetTypeKind(LLVMTypeOf(rval)) == LLVMStructTypeKind) {
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
            /* 接收者强引用临时变量生命周期延长契约 */
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
                zan_diag_emit_code(g->diag, DIAG_ERROR,
                              obj_ast ? obj_ast->loc : zan_loc(0, 0, 0, 0), 3001,
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
            /* 属性 setter 赋值实参位置绑定 */
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

