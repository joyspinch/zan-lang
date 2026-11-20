bool emit_native_memory_call(zan_irgen_t *g, zan_ast_node_t *expr,
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

