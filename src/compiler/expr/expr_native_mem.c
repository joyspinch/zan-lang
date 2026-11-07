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
    if (is_call_to(expr, "NativeMemory", "Crc32") && expr->call.args.count == 2) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef len = nm_arg(g, expr, 1, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i32t, i8ptr, i64t }, 3, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_crc32_update");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_crc32_update", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            LLVMConstInt(i32t, 0, 0), nm_addr(g, p, zero64), len }, 3, "nm.crc");
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
    if (is_call_to(expr, "NativeMemory", "Crc32Update") && expr->call.args.count == 3) {
        LLVMValueRef crc = nm_arg(g, expr, 0, locals);
        LLVMValueRef p = nm_arg(g, expr, 1, locals);
        LLVMValueRef len = nm_arg(g, expr, 2, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t,
            (LLVMTypeRef[]){ i32t, i8ptr, i64t }, 3, 0);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_hw_crc32_update");
        if (!fn) fn = LLVMAddFunction(g->mod, "zan_hw_crc32_update", ty);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            coerce_int_to(g, crc, i32t), nm_addr(g, p, zero64), len }, 3, "nm.crc.u");
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

