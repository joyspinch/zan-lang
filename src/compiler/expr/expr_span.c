#include "irgen_internal.h"
/* irgen_expr.c: 表达式代码生成、NativeMemory 原语、Lambda 发射与泛型方法推导 (由 irgen.c 包含) */
LLVMValueRef get_async_result_ptr(zan_irgen_t *g);
LLVMValueRef get_async_sub_slot_ptr(zan_irgen_t *g);

/* 实参里"拥有型临时量"的 EH 临时槽登记（定义在 irgen_call.c，晚于本文件
   被包含）：普通方法调用路径用它保证"调用抛异常时由展开器释放临时量"，
   构造路径此前漏了这一步，构造函数一抛，实参里的临时量就泄漏了。 */
int emit_call_arg_eh_push(zan_irgen_t *g, zan_ast_node_t *arg,
                                 LLVMValueRef val, local_scope_t *locals);

/* NativeMemory 原语：堆外内存操作，地址使用 nint (i64)，不计入 ARC */

/* 将实例接收者绑定至方法组闭包记录；recv 为 NULL 时生成静态方法组 */
LLVMValueRef emit_method_group_closure(zan_irgen_t *g, zan_symbol_t *msym,
                                              LLVMValueRef recv, zan_loc_t loc);

/* 静态方法组记录构建器 */
LLVMValueRef build_static_mg_record(zan_irgen_t *g, zan_loc_t loc,
                                           const char *lname, LLVMTypeRef rec_ty,
                                           LLVMValueRef thunk);

/* 合成的 Binding<T> 访问器委托：原生平台使用纯函数指针，wasm32 采用静态方法组记录 */
LLVMValueRef emit_binding_acc_delegate(zan_irgen_t *g, LLVMValueRef acc,
                                              LLVMTypeRef acc_ty);

/* 检查类或其基类中是否声明了指定名称的成员（字段/方法/属性/事件/常量） */
int type_declares_member(zan_symbol_t *cls, zan_istr_t name) {
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
int expr_has_reliable_string_bounds(zan_ast_node_t *expr,
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
LLVMTypeRef span_elem_storage_type(zan_irgen_t *g, zan_type_t *elem_type) {
    if (!elem_type) return LLVMInt32TypeInContext(g->ctx);
    if (elem_type->kind == TYPE_CHAR) return LLVMInt8TypeInContext(g->ctx);
    return map_type(g, elem_type);
}

/* Span<T> 原语：将切片操作降解为 { i8* base, i64 len } 胖指针视图 */
bool emit_span_call(zan_irgen_t *g, zan_ast_node_t *expr,
                           local_scope_t *locals, LLVMValueRef *out) {
    zan_ast_node_t *callee = expr->call.callee;
    if (!callee || callee->kind != AST_MEMBER_ACCESS) return false;
    zan_istr_t mm = callee->member.name;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    if ((mm.len == 6 && memcmp(mm.str, "AsSpan", 6) == 0) ||
        (mm.len == 14 && memcmp(mm.str, "AsReadOnlySpan", 14) == 0)) {
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
bool emit_bytes_call(zan_irgen_t *g, zan_ast_node_t *expr,
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
