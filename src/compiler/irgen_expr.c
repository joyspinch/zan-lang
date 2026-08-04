/* irgen_expr.c -- NativeMemory intrinsics and the main expression emitter
 * (emit_expr) plus lambda emission and generic-method call inference.
 *
 * Part of the irgen translation unit: this file is #include'd by irgen.c
 * (in a fixed order) and must not be compiled standalone. Splitting keeps
 * the single-TU static linkage while keeping each concern in its own file.
 */

/* ===== NativeMemory intrinsics =====================================
 * Raw off-heap memory for binary IO. An address is a plain nint (i64) that
 * never enters ARC. Each operation lowers to a single libc call, so bulk work
 * in Zan code (page encoding, checksums, network framing) costs what the
 * equivalent C would. Scalar access is Span<T>'s job: `new Span<int>(p, n)`
 * views a raw address and indexes it with align-1 typed loads/stores.
 *
 *   nint  Alloc(int size)                     zeroed malloc
 *   void  Free(nint p)
 *   void  Copy(nint dst, nint src, int n)     memmove (overlap-safe)
 *   void  Fill(nint p, int byte, int n)       memset
 *   int   Compare(nint a, nint b, int n)      memcmp
 *   string GetString(nint p, int off, int len)  copies into a real ARC string
 *   void  PutString(nint p, int off, string s, int len)
 *   int   Crc32(nint p, int len)              hardware-independent CRC32 (IEEE)
 */

/* Binds a receiver to an instance method group (defined with the closure
 * machinery further down); used by the two method-group value sites above it. */
static LLVMValueRef emit_method_group_closure(zan_irgen_t *g, zan_symbol_t *msym,
                                              LLVMValueRef recv, zan_loc_t loc);

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

/* __zan_nm_crc32(i8*, i64) -> i64: CRC32 (IEEE 802.3, reflected polynomial
 * 0xEDB88320), table-driven, one table lookup per byte. The 256-entry table
 * is computed at compile time and emitted as a constant global, so the
 * function is self-contained (no runtime object to link). */
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

/* Span<T> intrinsics lowering a member call to a { i8* base, i64 len } value:
 * arr.AsSpan([start[,len]]) is a non-owning view over a compact array's
 * elements; span.Slice(start[,len]) narrows an existing view. */
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
            LLVMValueRef off = LLVMBuildMul(g->builder, start,
                LLVMSizeOf(elem_llvm), "span.boff");
            base = LLVMBuildGEP2(g->builder, i8, base, &off, 1, "span.base2");
            len = (expr->call.args.count >= 2)
                ? coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals), i64t)
                : LLVMBuildSub(g->builder, len, start, "span.len");
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
        LLVMValueRef off = LLVMBuildMul(g->builder, start,
            LLVMSizeOf(elem_llvm), "sl.boff");
        LLVMValueRef nbase = LLVMBuildGEP2(g->builder, i8, base, &off, 1, "sl.base2");
        LLVMValueRef nlen = (expr->call.args.count >= 2)
            ? coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals), i64t)
            : LLVMBuildSub(g->builder, len, start, "sl.len2");
        LLVMValueRef v = LLVMGetUndef(g->span_struct_type);
        v = LLVMBuildInsertValue(g->builder, v, nbase, 0, "sl.iv0");
        v = LLVMBuildInsertValue(g->builder, v, nlen, 1, "sl.iv1");
        *out = v;
        return true;
    }
    return false;
}

/* Byte-buffer bridge: `byte[]` is the owned buffer the library allocates and
 * hands to C (an array value already points at its first element, so it is a
 * `char*` as far as an extern is concerned), and these two convert between it
 * and `string` without going through the "calloc returns a string" idiom.
 *   s.ToBytes()          -> byte[] holding the bytes of s (no NUL)
 *   b.ToStr([off, len])  -> owned rc string copied out of the buffer */
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
        LLVMValueRef s = emit_expr(g, callee->member.object, locals);
        LLVMTypeRef strlen_ty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i8ptr }, 1, 0);
        LLVMValueRef len = zan_call2(g->builder, strlen_ty, g->fn_strlen, &s, 1, "tb.len");
        LLVMValueRef arr = zan_array_alloc(g, len, len);
        zan_call2(g->builder, memcpy_ty, memcpy_fn,
                  (LLVMValueRef[]){ arr, s, len }, 3, "");
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
        src = LLVMBuildGEP2(g->builder, i8, arr, &off, 1, "ts.src");
    }
    LLVMTypeRef alloc_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64t }, 1, 0);
    LLVMValueRef total = LLVMBuildAdd(g->builder, len, LLVMConstInt(i64t, 1, 0), "ts.n");
    LLVMValueRef s = zan_call2(g->builder, alloc_ty, g->rt_str_alloc, &total, 1, "ts.str");
    zan_call2(g->builder, memcpy_ty, memcpy_fn,
              (LLVMValueRef[]){ s, src, len }, 3, "");
    LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, s, &len, 1, "ts.end");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
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
        /* a negative size would turn into an enormous calloc (likely null,
         * but only after an absurd attempt); reject it up front by returning
         * address 0. */
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

    if (is_call_to(expr, "NativeMemory", "GetString") && expr->call.args.count == 3) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef off = nm_arg(g, expr, 1, locals);
        LLVMValueRef len = nm_arg(g, expr, 2, locals);
        LLVMValueRef one = LLVMConstInt(i64t, 1, 0);
        LLVMValueRef total = zan_add(g->builder, len, one, "nm.gs.sz");
        LLVMValueRef s = emit_string_alloc_rc(g, total);
        LLVMTypeRef ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
        LLVMValueRef fn = get_libc_fn(g, "memcpy", ty);
        zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            s, nm_addr(g, p, off), len }, 3, "");
        LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, s, &len, 1, "nm.gs.end");
        zan_store_fit(g, LLVMConstInt(i8, 0, 0), endp);
        *out = s;
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "PutString") && expr->call.args.count == 4) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef off = nm_arg(g, expr, 1, locals);
        zan_ast_node_t *s_ast = expr->call.args.items[2];
        LLVMValueRef s = emit_expr(g, s_ast, locals);
        LLVMValueRef len = nm_arg(g, expr, 3, locals);
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
    return false;
}

/* ---- per-node-kind expression emitters --------------------------------
 * Each function below is one arm of the old monolithic emit_expr switch;
 * emit_expr itself is now just the literal cases plus dispatch.
 */

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
                LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                if (st) {
                    LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(st, 0), g->current_this, "this");
                    LLVMValueRef fptr = emit_field_ptr(g, g->current_type_sym, st, this_ptr, fi, "fld");
                    zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->ident.name);
                    LLVMTypeRef ft = fsym ? map_type(g, fsym->type) : LLVMInt64TypeInContext(g->ctx);
                    return promote_loaded(g,
                        LLVMBuildLoad2(g->builder, ft, fptr, "fval"),
                        fsym ? fsym->type : NULL);
                }
            }
        }
        /* bare-name static field of the enclosing class: `field` -> global.
         * Works in both static and instance methods. */
        if (g->current_type_sym) {
            zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->ident.name);
            LLVMValueRef gv = get_static_field_global(g, g->current_type_sym, fsym);
            if (gv) {
                LLVMTypeRef ft = fsym->type ? map_type(g, fsym->type)
                                            : LLVMInt64TypeInContext(g->ctx);
                return promote_loaded(g,
                    LLVMBuildLoad2(g->builder, ft, gv, "sfld"), fsym->type);
            }
        }
        /* method reference as delegate value: MethodName used as a value (not
         * called). A static method is the bare function pointer; an instance
         * method binds the current receiver into a closure (A33-2). */
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
        /* Genuinely unresolved bare name: not a local, field, static,
         * method, or global function, and the binder does not know it
         * either. Silently lowering to 0/null hides real bugs (an
         * out-of-scope variable compiled to null -> runtime AV), so fail
         * the build. Guarded on the binder not knowing the name so known
         * types/namespaces used in value position keep the old behavior. */
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
            LLVMTypeKind kind = LLVMGetTypeKind(LLVMTypeOf(lv));
            LLVMValueRef equal;
            if (kind == LLVMDoubleTypeKind || kind == LLVMFloatTypeKind) {
                equal = LLVMBuildFCmp(
                    g->builder, LLVMRealOEQ, lv, rv, "eq.f");
            } else if (kind == LLVMPointerTypeKind) {
                equal = LLVMBuildICmp(g->builder, LLVMIntEQ, lv, rv, "eq.p");
            } else {
                equal = zan_icmp(g->builder, LLVMIntEQ, lv, rv, "eq.i");
            }
            result = LLVMBuildAnd(g->builder, result, equal, "eq.all");
        }
        return result;
    }
    if (type && type->kind == TYPE_STRING) {
        LLVMTypeRef i8ptr = LLVMPointerType(
            LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
        LLVMValueRef args[] = {left, right};
        LLVMValueRef cmp = zan_call2(
            g->builder,
            LLVMFunctionType(i32, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
            g->fn_strcmp, args, 2, "eq.str");
        return zan_icmp(g->builder, LLVMIntEQ, cmp,
                        LLVMConstInt(i32, 0, 0), "eq.s");
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

/* Emit the operator itself once both operands are values: everything above it
 * in emit_expr_binary (operator overloads, strings, pointers) has already had
 * its chance. Split out so that the lifted nullable forms can reuse the exact
 * same lowering on the unwrapped payloads. */
static LLVMValueRef emit_binary_op_values(zan_irgen_t *g, zan_ast_node_t *expr,
                                          LLVMValueRef left, LLVMValueRef right,
                                          local_scope_t *locals);

/* `int?` and friends in a binary expression: null propagates through the
 * arithmetic operators, a comparison with a null operand is false, and `??`
 * unwraps. */
static LLVMValueRef emit_nullable_binary(zan_irgen_t *g, zan_ast_node_t *expr,
                                         LLVMValueRef left, LLVMValueRef right,
                                         local_scope_t *locals);

static LLVMValueRef emit_expr_binary(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* Short-circuit logical operators must not evaluate the right operand
         * unconditionally (e.g. `i < len && arr[i]`). Handle them before the
         * eager left/right evaluation used by the arithmetic/relational ops. */
        if (expr->binary.op == TK_AMP_AMP || expr->binary.op == TK_PIPE_PIPE) {
            bool is_and = (expr->binary.op == TK_AMP_AMP);
            LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
            LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));

            LLVMValueRef lval = emit_expr(g, expr->binary.left, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(lval)) != LLVMIntegerTypeKind ||
                LLVMGetIntTypeWidth(LLVMTypeOf(lval)) != 1) {
                lval = zan_icmp(g->builder, LLVMIntNE, lval,
                                     LLVMConstNull(LLVMTypeOf(lval)), "tobool");
            }
            LLVMBasicBlockRef left_bb = LLVMGetInsertBlock(g->builder);
            LLVMBasicBlockRef rhs_bb  = LLVMAppendBasicBlockInContext(g->ctx, fn, "sc.rhs");
            LLVMBasicBlockRef merge   = LLVMAppendBasicBlockInContext(g->ctx, fn, "sc.end");
            /* AND: if left is true, test right; else short-circuit false.
             * OR:  if left is true, short-circuit true; else test right. */
            if (is_and)
                LLVMBuildCondBr(g->builder, lval, rhs_bb, merge);
            else
                LLVMBuildCondBr(g->builder, lval, merge, rhs_bb);

            LLVMPositionBuilderAtEnd(g->builder, rhs_bb);
            LLVMValueRef rval = emit_expr(g, expr->binary.right, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(rval)) != LLVMIntegerTypeKind ||
                LLVMGetIntTypeWidth(LLVMTypeOf(rval)) != 1) {
                rval = zan_icmp(g->builder, LLVMIntNE, rval,
                                     LLVMConstNull(LLVMTypeOf(rval)), "tobool");
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

        /* A chain of 3+ string `+` parts is emitted as one flattened
         * allocation instead of pairwise (see emit_str_concat_n). */
        if (expr->binary.op == TK_PLUS && is_str_concat_node(g, expr, locals) &&
            (is_str_concat_node(g, expr->binary.left, locals) ||
             is_str_concat_node(g, expr->binary.right, locals))) {
            return emit_str_concat_n(g, expr, locals);
        }

        LLVMValueRef left = emit_expr(g, expr->binary.left, locals);
        LLVMValueRef right = emit_expr(g, expr->binary.right, locals);

        /* Operator overloading: if left operand is a user class instance,
         * look for a static op_add/op_sub/etc method and call it. */
        {
            zan_type_t *ltype = infer_expr_type(g, expr->binary.left, locals);
            /* comparisons against the null literal stay reference compares,
             * so an op_eq body can null-check without recursing into itself */
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
                    /* search for the operator method in the class */
                    zan_istr_t op_istr = { (char *)op_name, (int)strlen(op_name) };
                    zan_symbol_t *op_sym = get_method_sym(ltype->sym, op_istr);
                    if (op_sym) {
                        for (int fi = irgen_find_function(g, op_sym); fi >= 0; fi = -1) {
                            if (g->functions[fi].sym == op_sym) {
                                LLVMValueRef args[] = { left, right };
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(g->functions[fi].fn_type)) == LLVMVoidTypeKind) ? "" : "opcall";
                                LLVMValueRef opres = zan_call2(g->builder,
                                    g->functions[fi].fn_type,
                                    g->functions[fi].fn, args, 2, cn);
                                /* A delegate written in place -- `E += obj.OnX`,
                                 * `E += (v) => ...` -- is a closure this call
                                 * site owns; op_add stores its own reference
                                 * (the handler list retains), so drop ours.
                                 * Other operand shapes keep their existing
                                 * ownership contract. */
                                if (expr_yields_delegate_value(g, expr->binary.right, locals))
                                    emit_closure_release(g, right);
                                return opres;
                            }
                        }
                    }
                }
            }
        }

        /* A nullable operand lifts the operator (below); a string one means
         * this is a concatenation and is handled further down. */
        if ((llvm_is_nullable(LLVMTypeOf(left)) ||
             llvm_is_nullable(LLVMTypeOf(right))) &&
            !is_string_expr(g, expr->binary.left, locals) &&
            !is_string_expr(g, expr->binary.right, locals))
            return emit_nullable_binary(g, expr, left, right, locals);

        /* Two structs compare memberwise, like a C# value type without a
         * user-defined operator: LLVM has no icmp for aggregates, so the
         * fields are compared one by one and the results and-ed. */
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

        /* Two delegates compare by function + bound target, as in C#, so
         * `event -= obj.Handler` finds the handler it subscribed. */
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

        /* string concatenation: `a + b` when either operand is a string. A
         * numeric operand is formatted to its decimal string first (e.g.
         * "%t" + counter), so concat is not limited to two pointers. The
         * numeric temporaries are released after the concat copies them. */
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

        /* string equality: route `==`/`!=` on strings through strcmp. `== null`
         * keeps pointer semantics. */
        if ((expr->binary.op == TK_EQ_EQ || expr->binary.op == TK_BANG_EQ) &&
            both_ptr && str_operand &&
            expr->binary.left->kind != AST_NULL_LITERAL &&
            expr->binary.right->kind != AST_NULL_LITERAL) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef cmp_args[] = { left, right };
            LLVMValueRef r = zan_call2(g->builder,
                LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcmp, cmp_args, 2, "scmp");
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

        /* string ordering: route `<`/`<=`/`>`/`>=` on strings through strcmp
         * and compare its result against 0. Without this the operands (i8*)
         * would be compared as raw pointer addresses rather than by content. */
        if ((expr->binary.op == TK_LESS || expr->binary.op == TK_LESS_EQ ||
             expr->binary.op == TK_GREATER || expr->binary.op == TK_GREATER_EQ) &&
            both_ptr && str_operand &&
            expr->binary.left->kind != AST_NULL_LITERAL &&
            expr->binary.right->kind != AST_NULL_LITERAL) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef cmp_args[] = { left, right };
            LLVMValueRef r = zan_call2(g->builder,
                LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcmp, cmp_args, 2, "scmp");
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

        /* Pointer equality (incl. `== null`): compare the raw pointers, then
         * release owned call-result temps so `obj.Get() != null` does not leak
         * the +1 value the callee returned. */
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

static LLVMValueRef emit_binary_op_values(zan_irgen_t *g, zan_ast_node_t *expr,
                                          LLVMValueRef left, LLVMValueRef right,
                                          local_scope_t *locals) {
        /* reconcile mixed-width integer operands (e.g. i32 int vs i64 length)
         * and convert an integer meeting a float/double one */
        coerce_int_pair(g, &left, &right);

        /* Decided after the operands are reconciled, and from both of them:
         * `2 * d` is floating-point arithmetic even though its left operand
         * arrived as an integer -- selecting `mul` there emitted an integer
         * operator on doubles. */
        LLVMTypeRef left_type = LLVMTypeOf(left);
        bool is_float = (LLVMGetTypeKind(left_type) == LLVMDoubleTypeKind ||
                         LLVMGetTypeKind(left_type) == LLVMFloatTypeKind ||
                         LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMDoubleTypeKind ||
                         LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMFloatTypeKind);

        /* ulong operands select unsigned division/remainder/shift/compare */
        bool is_unsigned = !is_float &&
            (expr_is_ulong(g, expr->binary.left, locals) ||
             expr_is_ulong(g, expr->binary.right, locals));

        switch (expr->binary.op) {
        case TK_PLUS:
            return is_float ? LLVMBuildFAdd(g->builder, left, right, "add")
                            : zan_add(g->builder, left, right, "add");
        case TK_MINUS:
            return is_float ? LLVMBuildFSub(g->builder, left, right, "sub")
                            : zan_sub(g->builder, left, right, "sub");
        case TK_STAR:
            return is_float ? LLVMBuildFMul(g->builder, left, right, "mul")
                            : zan_mul(g->builder, left, right, "mul");
        case TK_SLASH:
            if (is_float) return LLVMBuildFDiv(g->builder, left, right, "div");
            {
                LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(right), 0, 0);
                LLVMValueRef is_zero = zan_icmp(g->builder, LLVMIntEQ, right, zero, "divz");
                emit_runtime_check(g, is_zero, expr->loc, "division by zero");
            }
            return is_unsigned ? zan_udiv(g->builder, left, right, "div")
                               : zan_sdiv(g->builder, left, right, "div");
        case TK_PERCENT:
            if (is_float) return LLVMBuildFRem(g->builder, left, right, "rem");
            {
                LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(right), 0, 0);
                LLVMValueRef is_zero = zan_icmp(g->builder, LLVMIntEQ, right, zero, "remz");
                emit_runtime_check(g, is_zero, expr->loc, "division by zero (modulo)");
            }
            return is_unsigned ? zan_urem(g->builder, left, right, "rem")
                               : zan_srem(g->builder, left, right, "rem");
        case TK_AMP:
            return zan_and(g->builder, left, right, "and");
        case TK_PIPE:
            return zan_or(g->builder, left, right, "or");
        case TK_CARET:
            return zan_xor(g->builder, left, right, "xor");
        case TK_LESS_LESS:
            return zan_shl(g->builder, left, right, "shl");
        case TK_GREATER_GREATER:
            return is_unsigned ? zan_lshr(g->builder, left, right, "shr")
                               : zan_ashr(g->builder, left, right, "shr");
        case TK_EQ_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOEQ, left, right, "eq")
                            : zan_icmp(g->builder, LLVMIntEQ, left, right, "eq");
        case TK_BANG_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealONE, left, right, "ne")
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

/* A binary node with the same operands but a different operator, so a lifted
 * comparison can ask for the payload equality it needs. */
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

    /* `v ?? alt`: the payload when there is one. Both sides were evaluated
     * eagerly above, exactly as the non-nullable `??` does. */
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
        /* Two nullables are equal when both hold none, or both hold the same
         * value -- the C# lifted equality, which (unlike the other operators)
         * yields a plain bool. */
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

    /* Arithmetic: the result is null when either operand is. The divisor is
     * forced to 1 whenever the result is null, so the division-by-zero check
     * cannot fire on an operation a lifted operator never performs. */
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

/* Shared lowering for prefix/postfix ++/-- (defined after
 * emit_expr_assignment; its lvalue resolution mirrors the assignment paths). */
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
            /* Logical not: true iff the operand is zero/null. A bitwise not
             * would be wrong for bools materialized wider than i1 (e.g. an i8
             * `1` bitwise-nots to 0xFE, which is still truthy). */
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

/* ---- Binding<T> lowering --------------------------------------------------
 * When the assignment target is a Binding<T>-typed field/local and the RHS is
 * not itself a Binding, the assignment is sugar for constructing a binding:
 *   comp.prop = user.name;   ->  live binding (Get/Set access User.name)
 *   comp.prop = <expr>;      ->  const binding storing the evaluated value
 * The synthesized accessor pair reads/writes the model field with normal ARC
 * semantics, so the model and the UI observe each other with no extra code. */

static int type_is_binding(zan_type_t *t) {
    return t && t->kind == TYPE_CLASS && t->name.len == 7 &&
           memcmp(t->name.str, "Binding", 7) == 0;
}

/* A delegate-typed field declared on a generic class can spell its signature
 * in the class's type parameters (`RowPaint<T> painter` inside `List<T>`).
 * Rebuild the signature against the receiver's instantiation so callers see
 * concrete parameter/return types (`RowPaint<Person>`). Returns `t` unchanged
 * when nothing needs substituting. */
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

/* Synthesize (or fetch cached) accessor functions for one class field:
 *   <T'> __zbind_get_<Class>_<field>(i8* obj)      returns a +1 value
 *   void __zbind_set_<Class>_<field>(i8* obj, T'v) retains new/releases old */
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
        LLVMValueRef val = LLVMBuildLoad2(g->builder, vt, fptr, "val");
        if (is_rc_managed_type(fs->type))
            emit_rc_retain_for_type(g, fs->type, val);
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
        if (is_rc_managed_type(fs->type)) {
            /* borrowed param: retain incoming, release the previous occupant */
            LLVMValueRef old = LLVMBuildLoad2(g->builder, vt, fptr, "old");
            emit_rc_retain_for_type(g, fs->type, v);
            zan_store_fit(g, v, fptr);
            emit_rc_release_for_type(g, fs->type, old);
        } else {
            zan_store_fit(g, v, fptr);
        }
        LLVMBuildRetVoid(g->builder);
    }

    g->current_fn = saved_fn;
    if (saved_bb) LLVMPositionBuilderAtEnd(g->builder, saved_bb);

    if (g->bind_acc_count < 512) {
        g->bind_accs[g->bind_acc_count].cls = cls;
        g->bind_accs[g->bind_acc_count].field = fs;
        g->bind_accs[g->bind_acc_count].get_fn = get_fn;
        g->bind_accs[g->bind_acc_count].set_fn = set_fn;
        g->bind_acc_count++;
    }
    *out_get = get_fn;
    *out_set = set_fn;
}

/* Construct a Binding<T> object for `rhs` (owned +1 result, or NULL when the
 * lowering does not apply and the caller should emit a plain assignment). */
static LLVMValueRef emit_binding_value(zan_irgen_t *g, zan_type_t *bind_t,
        zan_ast_node_t *rhs, local_scope_t *locals) {
    /* `b = null` clears the binding rather than wrapping null in a const one,
     * so an unbound slot stays testable with `b == null`. */
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

    /* A bare field name inside its own class means `this.<field>`, so it binds
     * live like any other field lvalue (`spec.str = Icon;`). */
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
    LLVMValueRef site_val = LLVMConstInt(i64, 0, 0);
    {
        int site_idx = reserve_arc_site(g, bsym, bind_t, 0, NULL);
        site_val = LLVMConstInt(i64, (unsigned long long)site_idx, 0);
        if (g->check_leaks) {
            char site_buf[600];
            const char *sfile = leak_site_file(g, rhs->loc);
            snprintf(site_buf, sizeof(site_buf), "%s:%u:%u",
                     sfile, rhs->loc.line, rhs->loc.col);
            site_name = LLVMBuildGlobalStringPtr(g->builder, site_buf, "site");
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
        /* non-owning target reference (like a weak field): the binding must
         * not keep the model alive, or model<->component graphs would leak */
        zan_store_fit(g, obj_cast, tptr);
        {
            /* an owned temp (e.g. `f().name`) is still released normally */
            zan_type_t *ot = infer_expr_type(g, rhs->member.object, locals);
            if (ot && is_rc_managed_type(ot) &&
                expr_yields_owned_rc_value(g, rhs->member.object, locals))
                emit_rc_release_for_type(g, ot, obj_val);
        }

        LLVMValueRef gptr = LLVMBuildStructGEP2(g->builder, st, objp, (unsigned)fi_getter, "b.get");
        LLVMTypeRef gslot_t = LLVMStructGetTypeAtIndex(st, (unsigned)fi_getter);
        zan_store_fit(g,
            LLVMBuildBitCast(g->builder, get_fn, gslot_t, "b.get.bc"), gptr);
        LLVMValueRef sptr = LLVMBuildStructGEP2(g->builder, st, objp, (unsigned)fi_setter, "b.set");
        LLVMTypeRef sslot_t = LLVMStructGetTypeAtIndex(st, (unsigned)fi_setter);
        zan_store_fit(g,
            LLVMBuildBitCast(g->builder, set_fn, sslot_t, "b.set.bc"), sptr);
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

/* Emit `\e[<code>m` for a Console.ForegroundColor/BackgroundColor assignment,
 * mapping a C# ConsoleColor (0..15) to its ANSI SGR code via a private lookup
 * table indexed by the (runtime) colour value. */
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
    LLVMValueRef gep = LLVMBuildInBoundsGEP2(g->builder, arrTy, tbl,
        (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), idx }, 2, "ansip");
    LLVMValueRef code = LLVMBuildLoad2(g->builder, i32, gep, "ansicode");
    LLVMTypeRef pty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 1);
    LLVMValueRef pf = LLVMGetNamedFunction(g->mod, "printf");
    if (!pf) pf = LLVMAddFunction(g->mod, "printf", pty);
    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "\033[%dm", "sgrfmt");
    zan_call2(g->builder, pty, pf, (LLVMValueRef[]){ fmt, code }, 2, "");
}

/* Emit an OSC title sequence for Console.Title = value (works on Windows
 * Terminal / conhost VT and xterm-compatible terminals). */
static void emit_console_title(zan_irgen_t *g, LLVMValueRef s) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef pty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 1);
    LLVMValueRef pf = LLVMGetNamedFunction(g->mod, "printf");
    if (!pf) pf = LLVMAddFunction(g->mod, "printf", pty);
    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "\033]0;%s\007", "titlefmt");
    zan_call2(g->builder, pty, pf, (LLVMValueRef[]){ fmt, s }, 2, "");
}

/* The type a field store must manage, as opposed to the type the field was
 * declared with: a field declared `T` in `Acc<T>` holds a `Node` in `Acc<Node>`
 * and has to be retained/released like one. Without this the store lowered to a
 * bare pointer store, so the +1 the caller handed over was dropped and the
 * object was freed while the field still pointed at it. */
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
        if (member->kind == SYM_FIELD && member->decl == decl) return member;
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
        if (!field || field->kind != AST_FIELD_DECL ||
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
        LLVMValueRef value = field_type && field_type->kind == TYPE_DELEGATE &&
                             field->field_decl.initializer->kind == AST_LAMBDA
            ? emit_lambda_typed(g, field->field_decl.initializer,
                                field_type, locals)
            : emit_expr(g, field->field_decl.initializer, locals);
        LLVMValueRef field_ptr = emit_field_ptr(
            g, type_sym, st, object_ptr, fi, "field.init");
        LLVMTypeRef slot_type = map_type(g, fsym->type);
        value = emit_boundary_coerce(g, value, slot_type);
        if (field_type && is_rc_managed_type(field_type)) {
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

/* A receiver that is itself a temporary -- Make().name, Get().Inner -- must be
 * released once the field is out, and an rc-managed field retained first so it
 * outlives the object it was read from. expr_member_of_owned_temp reports the
 * member expression as owning its result, so the consumer releases it. */
static LLVMValueRef finish_member_of_temp(zan_irgen_t *g, zan_ast_node_t *expr,
                                          local_scope_t *locals,
                                          zan_type_t *obj_type,
                                          LLVMValueRef obj_val, LLVMValueRef fv) {
    if (!obj_val || !obj_type || !is_rc_managed_type(obj_type) ||
        expr_is_local_ident(expr->member.object, locals) ||
        !expr_yields_owned_rc_value(g, expr->member.object, locals))
        return fv;
    zan_type_t *ft = member_owned_field_type(g, expr, locals);
    if (ft && is_rc_managed_type(ft) &&
        LLVMGetTypeKind(LLVMTypeOf(fv)) == LLVMPointerTypeKind)
        emit_rc_retain_for_type(g, ft, fv);
    emit_rc_release_for_type(g, obj_type, obj_val);
    return fv;
}

/* `obj.field = v` where the field is declared readonly. The checker enforces
 * the forms it can resolve without a local scope (`x = v`, `this.x = v`); the
 * receiver's type is only known here, where locals are typed. A readonly field
 * is writable from a constructor of its declaring type and nowhere else. */
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

static LLVMValueRef emit_expr_assignment(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        LLVMValueRef right;
        check_readonly_store(g, expr->binary.left, locals);
        {
            zan_type_t *lt = assign_lhs_type(g, expr->binary.left, locals);
            check_implicit_narrowing(g, lt,
                infer_expr_type(g, expr->binary.right, locals),
                expr->binary.right, "assignment");
            check_value_type_mismatch(g, lt,
                infer_expr_type(g, expr->binary.right, locals),
                expr->binary.right, "assignment");
            if (type_is_binding(lt) &&
                !type_is_binding(infer_expr_type(g, expr->binary.right, locals))) {
                LLVMValueRef bv = emit_binding_value(g, lt, expr->binary.right, locals);
                if (bv) {
                    /* the binding object is freshly owned (+1): swap in a
                     * dummy `new` node so the store paths below treat it as
                     * an owned value (no extra retain). */
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
            /* `target = (a, b) => ...`: hand the delegate type down so the
             * lambda's unannotated parameters borrow their types from it. */
            right = (lt && lt->kind == TYPE_DELEGATE &&
                     expr->binary.right->kind == AST_LAMBDA)
                        ? emit_lambda_typed(g, expr->binary.right, lt, locals)
                        : emit_expr(g, expr->binary.right, locals);
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
            } else if (local) {
                LLVMValueRef sv = coerce_int_to(g, right,
                    local_slot_type(g, local));
                zan_store_fit(g, sv, local->alloca);
            } else if (g->current_type_sym &&
                       get_static_field_global(g, g->current_type_sym,
                           get_field_sym(g->current_type_sym, expr->binary.left->ident.name))) {
                /* bare-name static field of the enclosing class: `field = v` */
                zan_symbol_t *fs = get_field_sym(g->current_type_sym,
                    expr->binary.left->ident.name);
                LLVMValueRef gv = get_static_field_global(g, g->current_type_sym, fs);
                if (fs->type && is_rc_managed_type(fs->type)) {
                    emit_rc_store_field(g, fs->type, gv, right, expr->binary.right, locals,
                                        (fs->modifiers & MOD_WEAK) ? 1 : 0);
                } else {
                    LLVMTypeRef ft = fs->type ? map_type(g, fs->type)
                                              : LLVMInt64TypeInContext(g->ctx);
                    zan_store_fit(g, coerce_int_to(g, right, ft), gv);
                }
            } else if (g->current_this && g->current_type_sym) {
                /* implicit this.Field assignment */
                int fi = get_field_index(g->current_type_sym, expr->binary.left->ident.name);
                if (fi >= 0) {
                    LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                    if (st) {
                        LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                            LLVMPointerType(st, 0), g->current_this, "this");
                        LLVMValueRef fptr = emit_field_ptr(g, g->current_type_sym, st, this_ptr, fi, "fld");
                        /* type conversion if needed */
                        zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->binary.left->ident.name);
                        /* `item = x` on a field declared `T`: the store has to
                         * manage the instantiation's concrete type, or the +1
                         * handed over by the caller is dropped. */
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
                        if (fst && is_rc_managed_type(fst)) {
                            emit_rc_store_field(g, fst, fptr, right, expr->binary.right, locals,
                                                (fsym->modifiers & MOD_WEAK) ? 1 : 0);
                        } else {
                            zan_store_fit(g, right, fptr);
                        }
                    }
                }
            }
        } else if (expr->binary.left->kind == AST_INDEX) {
            /* arr[i] = value */
            zan_ast_node_t *arr_expr = expr->binary.left->index.object;
            /* op_index_set: `obj[i] = v` on a class/struct instance lowers to
             * a call of the static `op_index_set(self, index, value)` method.
             * The value parameter is borrowed (like a method argument), so an
             * owned right side is released after the call. */
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
                                    method_param_type_at(g, op_sym, 1, NULL, arr_expr, locals), locals);
                                zan_type_t *vt = method_param_type_at(g, op_sym, 2, NULL, arr_expr, locals);
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
                    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64)
                        idx = LLVMBuildSExt(g->builder, idx,
                            LLVMInt64TypeInContext(g->ctx), "sps.ix");
                    LLVMValueRef ep = LLVMBuildGEP2(g->builder, elem_llvm, typed, &idx, 1, "sps.ep");
                    LLVMValueRef sv = right;
                    if (LLVMGetTypeKind(LLVMTypeOf(sv)) == LLVMIntegerTypeKind &&
                        LLVMGetTypeKind(elem_llvm) == LLVMIntegerTypeKind &&
                        LLVMTypeOf(sv) != elem_llvm)
                        sv = coerce_int_to(g, sv, elem_llvm);
                    /* align 1: a span may view a raw address (protocol framing,
                     * FFI structs) whose elements are not naturally aligned. */
                    LLVMSetAlignment(LLVMBuildStore(g->builder, sv, ep), 1);
                    return right;
                }
            }
            if (arr_expr->kind == AST_IDENTIFIER) {
                local_var_t *local = local_find(locals, arr_expr->ident.name);
                if (local && local->type && local->type->name.len == 4 &&
                    memcmp(local->type->name.str, "Dict", 4) == 0) {
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
                } else if (local && local->type && local->type->name.len == 4 &&
                    memcmp(local->type->name.str, "List", 4) == 0) {
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
                    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                        idx = LLVMBuildSExt(g->builder, idx, LLVMInt64TypeInContext(g->ctx), "idxext");
                    }
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
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    /* string (byte buffer): use i8 element type and truncate value */
                    if (local->type && local->type->kind == TYPE_STRING) {
                        LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i8, arr_ptr, &idx, 1, "eidx");
                        LLVMValueRef val8 = LLVMBuildTrunc(g->builder, right, i8, "byte");
                        zan_store_fit(g, val8, elem_ptr);
                    } else {
                        LLVMTypeRef elem_llvm = LLVMInt32TypeInContext(g->ctx);
                        if (local->type && local->type->element_type) {
                            zan_type_t *et = local->type->element_type;
                            elem_llvm = map_type(g, et);
                        }
                        LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                            LLVMPointerType(elem_llvm, 0), "arrp");
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
                        zan_type_t *et = local->type ? local->type->element_type : NULL;
                        LLVMValueRef stored = right;
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
                                    /* fit the value to the element width in
                                     * both directions: an i64 into a byte
                                     * slot used to write over its neighbours */
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
                        if (fsym->type && fsym->type->name.len == 4 &&
                            memcmp(fsym->type->name.str, "List", 4) == 0) {
                            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                            LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, arr_ptr,
                                LLVMPointerType(g->list_struct_type, 0), "lptr");
                            LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                                list_ptr, 2, "df");
                            LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64t, 0),
                                data_field, "data");
                            if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                                LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                                idx = LLVMBuildSExt(g->builder, idx, i64t, "idxext");
                            }
                            idx = slot_word_index(g, idx,
                                elem_slot_words(g, container_elem_type(fsym->type)));
                            LLVMValueRef slot_ptr = LLVMBuildGEP2(g->builder, i64t, data, &idx, 1, "ep");
                            emit_collection_slot_store(g, container_elem_type(fsym->type), i64t, slot_ptr,
                                right, expr->binary.right, locals, 1);
                        } else if (fsym->type && fsym->type->name.len == 4 &&
                                   memcmp(fsym->type->name.str, "Dict", 4) == 0) {
                            /* implicit this.dict[key] = value — upsert via the
                             * shared helper (same shape as the local path) */
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
                        } else {
                        LLVMTypeRef elem_llvm = LLVMInt32TypeInContext(g->ctx);
                        if (fsym->type && fsym->type->element_type) {
                            zan_type_t *et = fsym->type->element_type;
                            elem_llvm = map_type(g, et);
                        }
                        LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                            LLVMPointerType(elem_llvm, 0), "arrp");
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
                        zan_type_t *et = fsym->type ? fsym->type->element_type : NULL;
                        LLVMValueRef stored = right;
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
                                    /* fit the value to the element width in
                                     * both directions: an i64 into a byte
                                     * slot used to write over its neighbours */
                                    stored = coerce_int_to(g, stored, elem_llvm);
                                }
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
                    if (at->name.len == 4 && memcmp(at->name.str, "List", 4) == 0) {
                        /* List field: index into the data buffer, not the
                         * struct — otherwise the store clobbers count/cap/data. */
                        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                        LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, arr_ptr,
                            LLVMPointerType(g->list_struct_type, 0), "lptr");
                        LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                            list_ptr, 2, "df");
                        LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64t, 0),
                            data_field, "data");
                        if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                            idx = LLVMBuildSExt(g->builder, idx, i64t, "idxext");
                        }
                        idx = slot_word_index(g, idx,
                            elem_slot_words(g, container_elem_type(at)));
                        LLVMValueRef slot_ptr = LLVMBuildGEP2(g->builder, i64t, data, &idx, 1, "ep");
                        emit_collection_slot_store(g, container_elem_type(at), i64t, slot_ptr,
                            right, expr->binary.right, locals, 1);
                    } else if (at->name.len == 4 && memcmp(at->name.str, "Dict", 4) == 0) {
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
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i8, arr_ptr, &idx, 1, "eidx");
                        LLVMValueRef val8 = LLVMBuildTrunc(g->builder, right, i8, "byte");
                        zan_store_fit(g, val8, elem_ptr);
                        } else {
                        LLVMTypeRef elem_llvm = LLVMInt32TypeInContext(g->ctx);
                        if (at->element_type) {
                            elem_llvm = map_type(g, at->element_type);
                        }
                        LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                            LLVMPointerType(elem_llvm, 0), "arrp");
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
                        zan_type_t *et = at->element_type;
                        LLVMValueRef stored = right;
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
                                    /* fit the value to the element width in
                                     * both directions: an i64 into a byte
                                     * slot used to write over its neighbours */
                                    stored = coerce_int_to(g, stored, elem_llvm);
                                }
                            }
                            zan_store_fit(g, stored, elem_ptr);
                        }
                    }
                }
            } else {
                /* any other array expression: `obj.Buffer()[i] = v`, and the
                 * like. Without this the store was dropped silently. */
                zan_type_t *at = infer_expr_type(g, arr_expr, locals);
                if (at && at->kind == TYPE_ARRAY) {
                    LLVMTypeRef elem_llvm = at->element_type
                        ? map_type(g, at->element_type)
                        : LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                        LLVMPointerType(elem_llvm, 0), "arrp");
                    LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm,
                        typed_arr, &idx, 1, "eidx");
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
            /* Console.ForegroundColor/BackgroundColor/Title = value are console
             * state, not field stores: lower them to ANSI escapes. */
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
                    LLVMValueRef gv = get_static_field_global(g, cs, fs);
                    if (gv) {
                        if (fs->type && is_rc_managed_type(fs->type)) {
                            emit_rc_store_field(g, fs->type, gv, right,
                                                expr->binary.right, locals,
                                                (fs->modifiers & MOD_WEAK) ? 1 : 0);
                        } else {
                            LLVMTypeRef ft = fs->type ? map_type(g, fs->type)
                                                      : LLVMInt64TypeInContext(g->ctx);
                            zan_store_fit(g, coerce_int_to(g, right, ft), gv);
                        }
                        stored = true;
                    }
                }
            }
            if (!stored && obj_expr->kind == AST_IDENTIFIER) {
                local_var_t *local = local_find(locals, obj_expr->ident.name);
                if (local && local->type && local->type->sym) {
                    int fi = get_field_index(local->type->sym, expr->binary.left->member.name);
                    if (fi >= 0) {
                        LLVMTypeRef st = get_struct_llvm_type(g, local->type->sym);
                        if (st) {
                            LLVMValueRef struct_ptr = struct_base_ptr(g, local, st);
                            LLVMValueRef fptr = emit_field_ptr(g, local->type->sym, st, struct_ptr, fi, "fld");
                            zan_symbol_t *afsym = get_field_sym(local->type->sym, expr->binary.left->member.name);
                            zan_type_t *aft = afsym ? field_store_type(g, afsym, local->type) : NULL;
                            if (aft && is_rc_managed_type(aft)) {
                                emit_rc_store_field(g, aft, fptr, right, expr->binary.right, locals,
                                                    (afsym->modifiers & MOD_WEAK) ? 1 : 0);
                            } else {
                                zan_store_fit(g, right, fptr);
                            }
                            stored = true;
                        }
                    }
                }
            }
            /* general: <expr>.field = value where <expr> yields a class pointer
             * (e.g. list[i].field, a.b.field, this.field). */
            if (!stored) {
                zan_symbol_t *cls = expr_class_sym(g, obj_expr, locals);
                if (cls) {
                    int fi = get_field_index(cls, expr->binary.left->member.name);
                    if (fi >= 0) {
                        LLVMTypeRef st = get_struct_llvm_type(g, cls);
                        LLVMValueRef obj_val = emit_expr(g, obj_expr, locals);
                        if (st && LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                            LLVMValueRef fptr = emit_field_ptr(g, cls, st, obj_val, fi, "gfld");
                            zan_symbol_t *gfsym = get_field_sym(cls, expr->binary.left->member.name);
                            zan_type_t *gft = gfsym
                                ? field_store_type(g, gfsym,
                                      infer_expr_type(g, obj_expr, locals))
                                : NULL;
                            if (gft && is_rc_managed_type(gft)) {
                                emit_rc_store_field(g, gft, fptr, right, expr->binary.right, locals,
                                                    (gfsym->modifiers & MOD_WEAK) ? 1 : 0);
                            } else {
                                zan_store_fit(g, right, fptr);
                            }
                        }
                    }
                }
            }
            /* Robustness: `obj.name = v` where obj's class (or its base chain)
             * has no member of that name: nothing is stored, silently. A
             * dropped field then looks like a working assignment while the
             * value is lost, so diagnose it like the missing-method case. */
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
                    int afound = 0;
                    zan_symbol_t *acur = acls;
                    while (acur && !afound) {
                        for (int ami = 0; ami < acur->member_count; ami++) {
                            zan_symbol_t *am = acur->members[ami];
                            if (am && am->name.len == an.len &&
                                memcmp(am->name.str, an.str, an.len) == 0) {
                                afound = 1;
                                break;
                            }
                        }
                        zan_symbol_t *abase = (acur->type && acur->type->base_type)
                            ? acur->type->base_type->sym : NULL;
                        acur = (abase && abase != acur) ? abase : NULL;
                    }
                    if (!afound) {
                        zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                            "'%.*s' has no member '%.*s'",
                            (int)acls->name.len, acls->name.str,
                            (int)an.len, an.str);
                    }
                }
            }
        }
        return right;
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

static LLVMValueRef emit_expr_call(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals);

/* Shared lowering for prefix/postfix ++/--. Resolves the operand's storage
 * slot (local, static/instance field, or array/string element), reads the
 * current value at the slot's real width, applies +/-1 (integer or
 * float/double), writes it back, and yields the old (postfix) or new
 * (prefix) value. Operands that are not a numeric lvalue are diagnosed
 * instead of silently compiling to a no-op. */
static LLVMValueRef emit_incdec_expr(zan_irgen_t *g, zan_ast_node_t *expr,
                                     local_scope_t *locals, int is_prefix) {
    zan_ast_node_t *operand = expr->unary.operand;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef slot_ptr = NULL;
    LLVMTypeRef slot_ty = NULL;

    if (operand->kind == AST_IDENTIFIER) {
        local_var_t *lv = local_find(locals, operand->ident.name);
        if (lv) {
            slot_ptr = lv->alloca;
            slot_ty = local_slot_type(g, lv);
        } else if (g->current_type_sym) {
            /* bare-name static field of the enclosing class */
            zan_symbol_t *fs = get_field_sym(g->current_type_sym, operand->ident.name);
            LLVMValueRef gv = fs ? get_static_field_global(g, g->current_type_sym, fs) : NULL;
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
                    slot_ptr = emit_field_ptr(g, g->current_type_sym, st, this_ptr, fi, "fld");
                    zan_symbol_t *fsym = get_field_sym(g->current_type_sym, operand->ident.name);
                    slot_ty = fsym && fsym->type ? map_type(g, fsym->type) : i64;
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
                LLVMValueRef gv = get_static_field_global(g, cs, fs);
                if (gv) {
                    slot_ptr = gv;
                    slot_ty = fs->type ? map_type(g, fs->type) : i64;
                }
            }
        }
        /* local struct field: `s.field++` */
        if (!slot_ptr && obj_expr->kind == AST_IDENTIFIER) {
            local_var_t *local = local_find(locals, obj_expr->ident.name);
            if (local && local->type && local->type->sym) {
                int fi = get_field_index(local->type->sym, operand->member.name);
                LLVMTypeRef st = get_struct_llvm_type(g, local->type->sym);
                if (fi >= 0 && st) {
                    LLVMValueRef struct_ptr = struct_base_ptr(g, local, st);
                    slot_ptr = emit_field_ptr(g, local->type->sym, st, struct_ptr, fi, "fld");
                    zan_symbol_t *fsym = get_field_sym(local->type->sym, operand->member.name);
                    slot_ty = fsym && fsym->type ? map_type(g, fsym->type) : i64;
                }
            }
        }
        /* general class instance field: `obj.field++` / `this.field++` */
        if (!slot_ptr) {
            zan_symbol_t *cls = expr_class_sym(g, obj_expr, locals);
            if (cls) {
                int fi = get_field_index(cls, operand->member.name);
                LLVMTypeRef st = get_struct_llvm_type(g, cls);
                if (fi >= 0 && st) {
                    LLVMValueRef obj_val = emit_expr(g, obj_expr, locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                        slot_ptr = emit_field_ptr(g, cls, st, obj_val, fi, "gfld");
                        zan_symbol_t *fsym = get_field_sym(cls, operand->member.name);
                        slot_ty = fsym && fsym->type ? map_type(g, fsym->type) : i64;
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
            LLVMValueRef idx = emit_expr(g, operand->index.index, locals);
            LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                LLVMPointerType(elem_llvm, 0), "arrp");
            slot_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
            slot_ty = elem_llvm;
        } else if (at && at->kind == TYPE_STRING) {
            /* string[i] is a byte slot */
            LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
            LLVMValueRef idx = emit_expr(g, operand->index.index, locals);
            slot_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                arr_ptr, &idx, 1, "eidx");
            slot_ty = LLVMInt8TypeInContext(g->ctx);
        }
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
    return is_prefix ? new_val : old_val;
}

static LLVMValueRef emit_expr_string_interp(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* String interpolation: convert each part to i8*, then concatenate.
         * Parts alternate: string_literal, expr, string_literal, expr, ... string_literal
         * Strategy: for each part, get an i8* string. Then compute total length,
         * malloc a buffer, strcpy+strcat all parts, return the buffer pointer. */
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
                LLVMValueRef val = emit_expr(g, part, locals);
                LLVMTypeRef vt = LLVMTypeOf(val);
                LLVMTypeKind vtk = LLVMGetTypeKind(vt);

                if (vtk == LLVMPointerTypeKind) {
                    /* already a string */
                    strs[i] = val;
                    lens[i] = zan_call2(g->builder, strlen_type, g->fn_strlen, &val, 1, "len");
                    owns[i] = expr_yields_owned_rc_value(g, part, locals) ? 1 : 0;
                } else if (vtk == LLVMDoubleTypeKind || vtk == LLVMFloatTypeKind) {
                    /* snprintf(NULL, 0, "%g", val) to get length, then snprintf into buffer */
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%g", "dfmt");
                    LLVMValueRef null_ptr = LLVMConstNull(i8ptr);
                    LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
                    LLVMValueRef snp_args1[] = { null_ptr, zero, fmt, val };
                    LLVMValueRef needed = zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args1, 4, "needed");
                    LLVMValueRef needed64 = LLVMBuildSExt(g->builder, needed, i64, "n64");
                    LLVMValueRef buf_size = zan_add(g->builder, needed64, LLVMConstInt(i64, 1, 0), "bsz");
                    LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
                    LLVMValueRef snp_args2[] = { buf, buf_size, fmt, val };
                    zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args2, 4, "");
                    strs[i] = buf;
                    lens[i] = needed64;
                    owns[i] = 1;
                } else if (expr_is_char(g, part, locals)) {
                    /* char interpolates as the character (C#), not its code. */
                    LLVMValueRef buf = emit_char_to_cstr(g, val);
                    strs[i] = buf;
                    lens[i] = zan_call2(g->builder, strlen_type, g->fn_strlen, &buf, 1, "clen");
                    owns[i] = 1;
                } else {
                    /* integer types — format with %lld (%llu for ulong) */
                    LLVMValueRef val64 = val;
                    if (LLVMGetIntTypeWidth(vt) < 64) {
                        val64 = LLVMBuildSExt(g->builder, val, i64, "ext");
                    }
                    LLVMValueRef fmt = expr_is_ulong(g, part, locals)
                        ? LLVMBuildGlobalStringPtr(g->builder, "%llu", "ufmt")
                        : LLVMBuildGlobalStringPtr(g->builder, "%lld", "ifmt");
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

        /* strcpy first, strcat rest */
        LLVMTypeRef strcpy_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
        LLVMValueRef strcpy_args[] = { result, strs[0] };
        zan_call2(g->builder, strcpy_type, g->fn_strcpy, strcpy_args, 2, "");

        for (int i = 1; i < n; i++) {
            LLVMValueRef cat_args[] = { result, strs[i] };
            zan_call2(g->builder, strcpy_type, g->fn_strcat, cat_args, 2, "");
        }

        if (owns) {
            for (int i = 0; i < n; i++) {
                if (owns[i]) emit_string_release(g, strs[i]);
            }
        }
        free(strs);
        free(lens);
        free(owns);
        return result;
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

static LLVMValueRef emit_expr_member_access(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* `v.HasValue` / `v.Value` on a nullable value type. `Value` on a null
         * one is a program error, reported like the other runtime checks
         * instead of handing back the zeroed payload. */
        {
            zan_type_t *nt = infer_expr_type(g, expr->member.object, locals);
            bool is_has = expr->member.name.len == 8 &&
                memcmp(expr->member.name.str, "HasValue", 8) == 0;
            bool is_val = expr->member.name.len == 5 &&
                memcmp(expr->member.name.str, "Value", 5) == 0;
            if (nt && nt->kind == TYPE_NULLABLE && (is_has || is_val)) {
                LLVMValueRef nv = emit_expr(g, expr->member.object, locals);
                if (llvm_is_nullable(LLVMTypeOf(nv))) {
                    if (is_has) return nullable_has_value(g, nv);
                    emit_runtime_check(g,
                        LLVMBuildNot(g->builder, nullable_has_value(g, nv), "nv.novalue"),
                        expr->loc, "Nullable object must have a value");
                    return nullable_get_payload(g, nv);
                }
            }
        }
        /* Guard: a member access whose receiver is a builtin scalar type is
         * only valid for the compiler-lowered string.Length property. Any
         * other name was a typo that the checker rejects; this second line of
         * defence emits an error instead of silently lowering to the constant
         * 0 (which crashed when the zero was later used as a pointer —
         * `tabs[i].path.path`). */
        {
            zan_type_t *mt = infer_expr_type(g, expr->member.object, locals);
            if (mt && mt->kind != TYPE_CLASS && mt->kind != TYPE_STRUCT &&
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

        /* .Length property on Span -- read the length field from the value */
        if (expr->member.name.len == 6 &&
            memcmp(expr->member.name.str, "Length", 6) == 0) {
            zan_type_t *st = infer_expr_type(g, expr->member.object, locals);
            if (is_span_type(st)) {
                LLVMValueRef span_val = emit_expr(g, expr->member.object, locals);
                LLVMValueRef len = LLVMBuildExtractValue(g->builder, span_val, 1, "sp.len");
                return LLVMBuildTrunc(g->builder, len,
                    LLVMInt32TypeInContext(g->ctx), "sp.len32");
            }
        }

        /* .Count property on List — read count field from list struct
         * (works for local vars and fields). */
        if (expr->member.name.len == 5 && memcmp(expr->member.name.str, "Count", 5) == 0) {
            zan_type_t *lt = infer_expr_type(g, expr->member.object, locals);
            if (lt && lt->name.len == 4 && memcmp(lt->name.str, "List", 4) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef raw_ptr = emit_expr(g, expr->member.object, locals);
                LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                    LLVMPointerType(g->list_struct_type, 0), "lptr");
                LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                /* an owned receiver temp (e.g. `Coll.FindAll().Count`) is
                 * consumed by this read and must be released, or the returned
                 * List and its RC elements leak. */
                emit_release_owned_call_temp(g, expr->member.object, raw_ptr, locals);
                return count;
            }
        }

        /* Dict.Count — return number of entries (locals and fields alike).
         * An owned receiver temp (`dictFactory.Get().Count`) is consumed by
         * the read and must be released, exactly as List.Count does below;
         * without it a method returning the dict retained one reference per
         * count read. */
        if (expr->member.name.len == 5 && memcmp(expr->member.name.str, "Count", 5) == 0) {
            zan_type_t *dt = infer_expr_type(g, expr->member.object, locals);
            if (dt && dt->name.len == 4 && memcmp(dt->name.str, "Dict", 4) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef raw = emit_expr(g, expr->member.object, locals);
                LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                    LLVMPointerType(g->dict_struct_type, 0), "dp");
                LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "dcnt");
                emit_release_owned_call_temp(g, expr->member.object, raw, locals);
                return cnt;
            }
        }

        /* Dict.Keys / Dict.Values — copy the entries into a fresh owned List
         * of the key/value type (retaining rc-managed elements: the dict still
         * owns its slots). */
        if ((expr->member.name.len == 4 && memcmp(expr->member.name.str, "Keys", 4) == 0) ||
            (expr->member.name.len == 6 && memcmp(expr->member.name.str, "Values", 6) == 0)) {
            zan_type_t *dt = infer_expr_type(g, expr->member.object, locals);
            if (dt && dt->name.len == 4 && memcmp(dt->name.str, "Dict", 4) == 0) {
                bool want_keys = (expr->member.name.len == 4);
                zan_type_t *elem = want_keys ? dict_key_type(g, dt) : dict_value_type(dt);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef raw = emit_expr(g, expr->member.object, locals);
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

        /* array.Length: read the element count from the array header. */
        if (expr->member.name.len == 6 && memcmp(expr->member.name.str, "Length", 6) == 0) {
            zan_type_t *at = infer_expr_type(g, expr->member.object, locals);
            if (at && at->kind == TYPE_ARRAY) {
                LLVMValueRef arr = emit_expr(g, expr->member.object, locals);
                return zan_array_len(g, arr);
            }
        }

        /* StringBuilder.Length: load the count field. */
        if (expr->member.name.len == 6 && memcmp(expr->member.name.str, "Length", 6) == 0) {
            zan_type_t *sbt = infer_expr_type(g, expr->member.object, locals);
            if (sbt && sbt->name.len == 13 &&
                memcmp(sbt->name.str, "StringBuilder", 13) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef raw = emit_expr(g, expr->member.object, locals);
                LLVMValueRef sbp = LLVMBuildBitCast(g->builder, raw,
                    LLVMPointerType(g->sb_struct_type, 0), "sbp");
                LLVMValueRef cptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 0, "sbcp");
                LLVMValueRef sblen = LLVMBuildLoad2(g->builder, i64, cptr, "sblen");
                emit_release_owned_call_temp(g, expr->member.object, raw, locals);
                return sblen;
            }
        }

        /* String.Length property — call strlen on string pointer */
        if (expr->member.name.len == 6 && memcmp(expr->member.name.str, "Length", 6) == 0) {
            LLVMValueRef obj_val = emit_expr(g, expr->member.object, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef strlen_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                /* strlen returns i64; `int` is i64 so return it directly. */
                LLVMValueRef len = zan_call2(g->builder, strlen_type, g->fn_strlen, &obj_val, 1, "len");
                emit_release_owned_call_temp(g, expr->member.object, obj_val, locals);
                return len;
            }
        }

        /* ConsoleColor.<Member> -> its fixed 0..15 ordinal (C# order). Handled
         * here so console-colour code yields the right value regardless of how
         * the stdlib enum's members bind. */
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

        /* Enum member access: EnumType.MemberName -> integer constant */
        if (expr->member.object->kind == AST_IDENTIFIER) {
            zan_symbol_t *enum_sym = zan_binder_lookup(g->binder, expr->member.object->ident.name);
            if (enum_sym && enum_sym->kind == SYM_ENUM) {
                int enum_val = 0;
                for (int ei = 0; ei < enum_sym->member_count; ei++) {
                    if (enum_sym->members[ei]->kind == SYM_ENUM_MEMBER) {
                        if (enum_sym->members[ei]->name.len == expr->member.name.len &&
                            memcmp(enum_sym->members[ei]->name.str, expr->member.name.str,
                                   (size_t)expr->member.name.len) == 0) {
                            zan_ast_node_t *em_decl = enum_sym->members[ei]->decl;
                            if (em_decl && em_decl->kind == AST_ENUM_MEMBER &&
                                em_decl->enum_member.value &&
                                em_decl->enum_member.value->kind == AST_INT_LITERAL) {
                                enum_val = (int)em_decl->enum_member.value->int_val;
                            }
                            return LLVMConstInt(LLVMInt64TypeInContext(g->ctx),
                                               (uint64_t)enum_val, 0);
                        }
                        zan_ast_node_t *em_decl = enum_sym->members[ei]->decl;
                        if (em_decl && em_decl->kind == AST_ENUM_MEMBER &&
                            em_decl->enum_member.value &&
                            em_decl->enum_member.value->kind == AST_INT_LITERAL) {
                            enum_val = (int)em_decl->enum_member.value->int_val + 1;
                        } else {
                            enum_val++;
                        }
                    }
                }
                return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
            }
        }

        /* Static class/struct field access: ClassName.StaticField.
         * Load from the field's backing global (shared mutable storage);
         * the initializer is applied at main() entry, not folded here. */
        if (expr->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->member.object->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder, expr->member.object->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *fs = get_field_sym(cs, expr->member.name);
                LLVMValueRef gv = get_static_field_global(g, cs, fs);
                if (gv) {
                    LLVMTypeRef ft = fs->type ? map_type(g, fs->type)
                                              : LLVMInt64TypeInContext(g->ctx);
                    return promote_loaded(g,
                        LLVMBuildLoad2(g->builder, ft, gv, "sfld"), fs->type);
                }
            }
        }

        /* Static method reference used as a delegate value: ClassName.Method
         * (not immediately called) → the function pointer. Instance-method
         * groups are not bound here (they'd require a captured receiver). */
        if (expr->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->member.object->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder, expr->member.object->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *ms = get_method_sym(cs, expr->member.name);
                if (ms) {
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
                        LLVMTypeRef st = get_struct_llvm_type(g, type_sym);
                        if (st) {
                            /* load struct pointer or value, then GEP to field */
                            LLVMValueRef struct_ptr = struct_base_ptr(g, local, st);
                            LLVMValueRef field_ptr = emit_field_ptr(g, type_sym, st, struct_ptr, fi, "fld");
                            zan_symbol_t *fsym = get_field_sym(type_sym, expr->member.name);
                            LLVMTypeRef field_type = fsym ? map_type(g, fsym->type) : LLVMInt64TypeInContext(g->ctx);
                            LLVMValueRef fv = promote_loaded(g,
                                LLVMBuildLoad2(g->builder, field_type, field_ptr, "fval"),
                                fsym ? fsym->type : NULL);
                            if (fsym) {
                                zan_type_t *ct = subst_type_param(fsym->type, local->type);
                                if (ct != fsym->type) fv = emit_boundary_coerce(g, fv, map_type(g, ct));
                            }
                            return fv;
                        }
                    }
                }
            }
        }

        /* general field access: <expr>.field where <expr> yields a class
         * instance pointer (e.g. list[i].field, a.b.field, foo().field). */
        {
            zan_symbol_t *cls = expr_class_sym(g, expr->member.object, locals);
            if (cls) {
                int fi = get_field_index(cls, expr->member.name);
                if (fi >= 0) {
                    LLVMTypeRef st = get_struct_llvm_type(g, cls);
                    LLVMValueRef obj_val = emit_expr(g, expr->member.object, locals);
                    /* a value struct rvalue (list[i] of a struct element,
                     * a call result) is in a register, not behind a pointer */
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
                        LLVMTypeRef ft = fsym ? map_type(g, fsym->type)
                                              : LLVMInt64TypeInContext(g->ctx);
                        LLVMValueRef gfv = promote_loaded(g,
                            LLVMBuildLoad2(g->builder, ft, field_ptr, "gfval"),
                            fsym ? fsym->type : NULL);
                        zan_type_t *rct = infer_expr_type(g, expr->member.object, locals);
                        if (fsym) {
                            zan_type_t *ct = subst_type_param(fsym->type, rct);
                            if (ct != fsym->type) gfv = emit_boundary_coerce(g, gfv, map_type(g, ct));
                        }
                        return finish_member_of_temp(g, expr, locals, rct,
                                                     obj_val, gfv);
                    }
                }
            }
        }
        /* instance method group used as a value -- `Act a = this.Touch;` or
         * `Act a = obj.Touch;`: the receiver is bound into a closure record
         * (A33-2). Static method groups were handled above. */
        {
            zan_symbol_t *cls = expr_class_sym(g, expr->member.object, locals);
            if (cls) {
                zan_symbol_t *ms = get_method_sym(cls, expr->member.name);
                if (ms && ms->decl && ms->decl->kind == AST_METHOD_DECL &&
                    (ms->decl->method_decl.modifiers & MOD_STATIC) == 0) {
                    LLVMValueRef recv = emit_expr(g, expr->member.object, locals);
                    LLVMValueRef clo = recv
                        ? emit_method_group_closure(g, ms, recv, expr->loc)
                        : NULL;
                    if (clo) return clo;
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "instance method group '%.*s' cannot be used as a value",
                        (int)expr->member.name.len, expr->member.name.str);
                    return LLVMConstNull(
                        LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
                }
            }
        }
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

/* A container that is itself a temporary -- Split(",")[0], Keys[i] -- has to be
 * released once the element is out, and the element retained first so it
 * outlives its container. The index expression then owns its result, which
 * expr_yields_owned_rc_value reports so the receiver does not retain twice. */
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
        /* op_index operator: `obj[i]` on a class/struct instance lowers to a
         * call of the static `op_index(self, index)` method, mirroring how
         * binary operators lower to static op_add/op_sub/etc. */
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
                                method_param_type_at(g, op_sym, 1, NULL,
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
                if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                    LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64)
                    idx = LLVMBuildSExt(g->builder, idx,
                        LLVMInt64TypeInContext(g->ctx), "spx.ix");
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
                if (arr_type && arr_type->name.len == 4 && memcmp(arr_type->name.str, "List", 4) == 0) {
                    is_list = 1;
                }
            } else if (g->current_type_sym) {
                /* implicit this.field[i] — bare identifier naming an array field */
                zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->index.object->ident.name);
                if (fsym) {
                    arr_type = fsym->type;
                    arr_ptr = emit_expr(g, expr->index.object, locals);
                    if (arr_type && arr_type->name.len == 4 && memcmp(arr_type->name.str, "List", 4) == 0) {
                        is_list = 1;
                    }
                }
            }
        } else if (expr->index.object->kind == AST_MEMBER_ACCESS) {
            /* obj.field[i] — array stored in a struct/class field. Load the
             * field value (the array data pointer) and recover its element
             * type from the field declaration. */
            arr_type = member_access_field_type(g, locals, expr->index.object);
            if (arr_type) {
                arr_ptr = emit_expr(g, expr->index.object, locals);
                if (arr_type->name.len == 4 && memcmp(arr_type->name.str, "List", 4) == 0) {
                    is_list = 1;
                }
            }
        } else {
            /* General case: the base is any other expression that produces an
             * array/string/list value — e.g. a method call `foo()[i]`, a
             * parenthesized expression, or a nested index `a[i][j]`. Without
             * this, arr_ptr stays NULL and the whole access silently folds to
             * the constant 0 below. Evaluate the base value and recover its
             * element/container type from the expression. */
            arr_type = infer_expr_type(g, expr->index.object, locals);
            if (arr_type) {
                arr_ptr = emit_expr(g, expr->index.object, locals);
                if (arr_type->name.len == 4 && memcmp(arr_type->name.str, "List", 4) == 0) {
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
            if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                idx = LLVMBuildSExt(g->builder, idx, i64, "ext");
            }
            zan_type_t *et = container_elem_type(arr_type);
            LLVMValueRef widx = slot_word_index(g, idx, elem_slot_words(g, et));
            LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &widx, 1, "ep");
            LLVMTypeRef em = et ? map_type(g, et) : NULL;
            if (em && LLVMGetTypeKind(em) == LLVMStructTypeKind)
                return finish_index_of_temp(g, expr, locals, arr_type, et,
                    arr_ptr, load_struct_from_slot(g, elem_ptr, em));
            LLVMValueRef raw = LLVMBuildLoad2(g->builder, i64, elem_ptr, "elem");
            /* reinterpret non-integer elements: slots physically hold an i64,
             * so pointer (class/string) elements need inttoptr and floating
             * (double) elements need a bitcast back to the value type. */
            LLVMValueRef out = raw;
            if (em) {
                LLVMTypeKind mk = LLVMGetTypeKind(em);
                if (mk == LLVMPointerTypeKind)
                    out = LLVMBuildIntToPtr(g->builder, raw, em, "elp");
                else if (mk == LLVMDoubleTypeKind)
                    out = LLVMBuildBitCast(g->builder, raw, em, "elf");
            }
            return finish_index_of_temp(g, expr, locals, arr_type, et,
                                        arr_ptr, out);
        }
        /* Dict indexer: dict[key] -> hash probe for the entry, then its value
         * (0 when the key is absent, as the linear scan this replaces did). */
        if (arr_ptr && arr_type && arr_type->name.len == 4 && memcmp(arr_type->name.str, "Dict", 4) == 0) {
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
            LLVMValueRef dout = LLVMBuildLoad2(g->builder, value_llvm, res, "dval");
            return finish_index_of_temp(g, expr, locals, arr_type, dvt,
                                        arr_ptr, dout);
        }
        /* string[i] — load a single byte (i8) and zero-extend to i32 so that
         * indexing a string yields the character code, enabling char-level
         * lexing of real source text. */
        if (arr_ptr && arr_type && arr_type->kind == TYPE_STRING) {
            LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(LLVMTypeOf(idx)) != 64) {
                idx = LLVMBuildSExt(g->builder, idx, i64, "idxext");
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
            LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
            LLVMTypeRef elem_llvm = LLVMInt64TypeInContext(g->ctx);
            if (arr_type->element_type) {
                elem_llvm = map_type(g, arr_type->element_type);
            }
            LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                LLVMPointerType(elem_llvm, 0), "arrp");
            LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
            return promote_loaded(g,
                LLVMBuildLoad2(g->builder, elem_llvm, elem_ptr, "elem"),
                arr_type->element_type);
        }
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

static LLVMValueRef emit_expr_query_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* from x in src [where c]... select e — lowered to an eager loop that
         * filters and projects into a fresh List<sel> */
        static int query_counter = 0;
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        int mark = locals->count;

        LLVMValueRef collection = emit_expr(g, expr->query.source, locals);
        zan_type_t *src_ty = infer_expr_type(g, expr->query.source, locals);
        zan_type_t *elem = container_elem_type(src_ty);
        if (!elem) elem = g->binder->type_int;
        LLVMTypeRef elem_llvm = map_type(g, elem);

        /* select type, inferred with the range var briefly in scope */
        local_add(locals, expr->query.var, NULL, elem);
        zan_type_t *sel = infer_expr_type(g, expr->query.select, locals);
        locals->count = mark;
        if (!sel) sel = elem;

        /* result list: synthesize and emit `new List<sel>()` */
        zan_ast_node_t *sel_tr = zan_ast_new(g->arena, AST_TYPE_REF, expr->loc);
        sel_tr->type_ref.name = sel->name;
        zan_ast_list_init(&sel_tr->type_ref.type_args);
        zan_ast_node_t *list_tr = zan_ast_new(g->arena, AST_TYPE_REF, expr->loc);
        list_tr->type_ref.name = (zan_istr_t){"List", 4};
        zan_ast_list_init(&list_tr->type_ref.type_args);
        zan_ast_list_push(&list_tr->type_ref.type_args, sel_tr, g->arena);
        zan_ast_node_t *new_list = zan_ast_new(g->arena, AST_NEW_EXPR, expr->loc);
        new_list->new_expr.type = list_tr;
        new_list->new_expr.is_array = false;
        zan_ast_list_init(&new_list->new_expr.args);
        LLVMValueRef list_val = emit_expr(g, new_list, locals);

        /* hidden local holding the result so a synthetic `__q.Add(e)` call
         * resolves through the normal List.Add path */
        char qbuf[32];
        snprintf(qbuf, sizeof(qbuf), "__q%d", query_counter++);
        char *qn = zan_arena_strdup(g->arena, qbuf, strlen(qbuf));
        zan_istr_t qname = {qn, (int)strlen(qn)};
        LLVMValueRef list_alloc = emit_entry_alloca(g, LLVMTypeOf(list_val), "q");
        zan_store_fit(g, list_val, list_alloc);
        local_add(locals, qname, list_alloc,
                  zan_binder_make_list_type(g->binder, sel));

        /* iteration state */
        LLVMValueRef col = LLVMBuildBitCast(g->builder, collection,
            LLVMPointerType(g->list_struct_type, 0), "qcol");
        LLVMValueRef cnt_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
            col, 0, "qcntp");
        LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, cnt_ptr, "qcnt");
        LLVMValueRef data_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
            col, 2, "qdatap");
        LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0),
            data_ptr, "qdata");
        LLVMValueRef idx_alloc = emit_entry_alloca(g, i64, "qi");
        zan_store_fit(g, LLVMConstInt(i64, 0, 0), idx_alloc);
        LLVMValueRef iter_alloc = emit_entry_alloca(g, elem_llvm, "qv");
        local_add(locals, expr->query.var, iter_alloc, elem);

        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "q.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "q.body");
        LLVMBasicBlockRef inc_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "q.inc");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "q.end");

        LLVMBuildBr(g->builder, cond_bb);
        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        LLVMValueRef idx_val = LLVMBuildLoad2(g->builder, i64, idx_alloc, "qiv");
        LLVMValueRef cmp = zan_icmp(g->builder, LLVMIntSLT, idx_val, count, "qcmp");
        LLVMBuildCondBr(g->builder, cmp, body_bb, end_bb);

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        LLVMValueRef qwidx = slot_word_index(g, idx_val, elem_slot_words(g, elem));
        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &qwidx, 1, "qep");
        LLVMTypeKind ek = LLVMGetTypeKind(elem_llvm);
        LLVMValueRef ev = (ek == LLVMStructTypeKind)
            ? load_struct_from_slot(g, elem_ptr, elem_llvm)
            : LLVMBuildLoad2(g->builder, i64, elem_ptr, "qelem");
        if (ek == LLVMPointerTypeKind)
            ev = LLVMBuildIntToPtr(g->builder, ev, elem_llvm, "qelp");
        else if (ek == LLVMDoubleTypeKind)
            ev = LLVMBuildBitCast(g->builder, ev, elem_llvm, "qelf");
        else if (ek == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(elem_llvm) < 64)
            ev = LLVMBuildTrunc(g->builder, ev, elem_llvm, "qelt");
        zan_store_fit(g, ev, iter_alloc);

        /* where clauses: any false condition skips to the increment */
        for (int wi = 0; wi < expr->query.wheres.count; wi++) {
            LLVMValueRef c = emit_expr(g, expr->query.wheres.items[wi], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(c)) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(LLVMTypeOf(c)) != 1)
                c = zan_icmp(g->builder, LLVMIntNE, c,
                                  LLVMConstNull(LLVMTypeOf(c)), "qw");
            LLVMBasicBlockRef pass_bb =
                LLVMAppendBasicBlockInContext(g->ctx, fn, "q.pass");
            LLVMBuildCondBr(g->builder, c, pass_bb, inc_bb);
            LLVMPositionBuilderAtEnd(g->builder, pass_bb);
        }

        /* __q.Add(select) through the normal lowering */
        zan_ast_node_t *qid = zan_ast_new(g->arena, AST_IDENTIFIER, expr->loc);
        qid->ident.name = qname;
        zan_ast_node_t *madd = zan_ast_new(g->arena, AST_MEMBER_ACCESS, expr->loc);
        madd->member.object = qid;
        madd->member.name = (zan_istr_t){"Add", 3};
        madd->member.null_cond = 0;
        zan_ast_node_t *addcall = zan_ast_new(g->arena, AST_CALL, expr->loc);
        addcall->call.callee = madd;
        zan_ast_list_init(&addcall->call.args);
        zan_ast_list_init(&addcall->call.type_args);
        zan_ast_list_push(&addcall->call.args, expr->query.select, g->arena);
        emit_expr(g, addcall, locals);
        LLVMBuildBr(g->builder, inc_bb);

        LLVMPositionBuilderAtEnd(g->builder, inc_bb);
        LLVMValueRef next = zan_add(g->builder,
            LLVMBuildLoad2(g->builder, i64, idx_alloc, "qi2"),
            LLVMConstInt(i64, 1, 0), "qnext");
        zan_store_fit(g, next, idx_alloc);
        LLVMBuildBr(g->builder, cond_bb);

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        emit_release_owned_call_temp(g, expr->query.source, collection, locals);
        locals->count = mark;
        (void)i8ptr;
        return list_val;
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

static LLVMValueRef emit_expr_new_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* Anonymous object literal `new { ... }` must have been consumed by
         * the dbgen query pass; anything that survives to codegen is an error
         * (it has no real runtime type). */
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
                LLVMValueRef v = LLVMGetUndef(g->span_struct_type);
                v = LLVMBuildInsertValue(g->builder, v, base, 0, "span.n0");
                v = LLVMBuildInsertValue(g->builder, v, len, 1, "span.n1");
                return v;
            }
        }
        /* new List<T>() — built-in dynamic list */
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF) {
            zan_istr_t tname = expr->new_expr.type->type_ref.name;
            if (tname.len == 4 && memcmp(tname.str, "List", 4) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                /* allocate List struct on heap with an rc header (24 bytes:
                 * 3 * i64), so ARC frees it and its data buffer on release. */
                zan_type_t *lelem = NULL;
                {
                    zan_type_t *lt = resolve_type_ctx(g, expr->new_expr.type);
                    if (lt) lelem = container_elem_type(lt);
                }
                LLVMValueRef list_ptr = emit_alloc_rc_collection(g, expr, 24, 1, lelem);
                /* cast to List* */
                LLVMValueRef typed_ptr = LLVMBuildBitCast(g->builder, list_ptr,
                    LLVMPointerType(g->list_struct_type, 0), "lptr");
                /* collection initializer items: new List<T>{ a, b, c }. The
                 * parser stores them in new_expr.args (List has no ctor args). */
                int ninit = expr->new_expr.args.count;
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
                LLVMValueRef data_typed = LLVMBuildBitCast(g->builder, data_ptr,
                    LLVMPointerType(i64, 0), "dptr");
                LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 2, "df");
                zan_store_fit(g, data_typed, data_field);
                /* store each initializer item (with proper rc retain semantics) */
                for (int ii = 0; ii < ninit; ii++) {
                    zan_ast_node_t *item = expr->new_expr.args.items[ii];
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
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF) {
            zan_istr_t sbname = expr->new_expr.type->type_ref.name;
            if (sbname.len == 13 && memcmp(sbname.str, "StringBuilder", 13) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                /* allocate StringBuilder struct { i64 count, i64 cap, i8* data }
                 * = 24 bytes, with an rc header so ARC frees the struct and its
                 * data buffer on release. */
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
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF) {
            zan_istr_t tname2 = expr->new_expr.type->type_ref.name;
            if ((tname2.len == 4 && memcmp(tname2.str, "Dict", 4) == 0) ||
                (tname2.len == 10 && memcmp(tname2.str, "Dictionary", 10) == 0)) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                /* allocate the Dict struct (7 i64-sized fields) with an rc
                 * header, so a dict is owned like any other collection. */
                LLVMValueRef dict_raw = emit_alloc_rc_collection(g, expr, 64, 3,
                    resolve_type_ctx(g, expr->new_expr.type));
                LLVMValueRef typed_ptr = LLVMBuildBitCast(g->builder, dict_raw,
                    LLVMPointerType(g->dict_struct_type, 0), "dptr");
                /* zan_rt_alloc does not zero: the hash index fields must start
                 * at "no index yet" explicitly. */
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
                LLVMValueRef keys_typed = LLVMBuildBitCast(g->builder, keys_raw,
                    LLVMPointerType(i8ptr, 0), "kptr");
                LLVMValueRef kf = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 2, "kf");
                zan_store_fit(g, keys_typed, kf);
                /* values = malloc(16 * 8) */
                LLVMValueRef vals_sz = LLVMConstInt(i64, 128ULL * value_words, 0);
                LLVMValueRef vals_raw = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), vals_sz }, 2, "vals");
                LLVMValueRef vals_typed = LLVMBuildBitCast(g->builder, vals_raw,
                    LLVMPointerType(i64, 0), "vptr");
                LLVMValueRef vf = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 3, "vf");
                zan_store_fit(g, vals_typed, vf);
                /* dictionary initializer entries: new Dict<K,V>{ {k, v}, ... }.
                 * The parser flattens each `{ k, v }` pair into new_expr.args,
                 * so consume them pairwise via the shared upsert helper. */
                int ninit = expr->new_expr.args.count;
                if (ninit >= 2) {
                    zan_type_t *kt = dict_key_type(g, dict_type);
                    for (int ii = 0; ii + 1 < ninit; ii += 2) {
                        zan_ast_node_t *kexpr = expr->new_expr.args.items[ii];
                        zan_ast_node_t *vexpr = expr->new_expr.args.items[ii + 1];
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

        /* new Type[] { a, b, c } — the braces hold the elements, so the
         * buffer is as long as the element list and each one is stored into
         * it. Without this the node fell through to the class path below and
         * produced a pointer to nothing. */
        if (expr->new_expr.is_array && expr->new_expr.array_init) {
            /* the type node is written `T[]` here, so it resolves to the
             * array type; the elements are of its element type */
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
            LLVMValueRef arr = zan_array_alloc(g, total,
                LLVMConstInt(i64t, (unsigned long long)n, 0));
            for (int k = 0; k < n; k++) {
                LLVMValueRef v = emit_arg_typed(g, expr->new_expr.args.items[k],
                                                elem_type, locals);
                LLVMValueRef idx = LLVMConstInt(i64t, (unsigned long long)k, 0);
                LLVMValueRef slot = LLVMBuildGEP2(g->builder, elem_llvm, arr,
                                                  &idx, 1, "aep");
                /* fit the value to the element width: storing an i64 into a
                 * byte slot would write over the elements after it */
                LLVMBuildStore(g->builder, emit_boundary_coerce(g, v, elem_llvm), slot);
            }
            return arr;
        }

        /* new Type[size] — array creation */
        if (expr->new_expr.is_array && expr->new_expr.args.count > 0) {
            zan_type_t *elem_type = resolve_type_ctx(g, expr->new_expr.type);
            if (!elem_type) elem_type = g->binder->type_int;
            LLVMTypeRef elem_llvm = map_type(g, elem_type);
            LLVMValueRef size_val = emit_expr(g, expr->new_expr.args.items[0], locals);

            /* calloc(size, sizeof(elem)) */
            LLVMValueRef elem_size = LLVMSizeOf(elem_llvm);
            LLVMValueRef total = zan_mul(g->builder,
                LLVMBuildZExt(g->builder, size_val, LLVMInt64TypeInContext(g->ctx), "zext"),
                elem_size, "total");
            return zan_array_alloc(g, total,
                LLVMBuildZExt(g->builder, size_val,
                    LLVMInt64TypeInContext(g->ctx), "arr.count"));
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
        if (type_name.str) {
            zan_symbol_t *sym = zan_binder_lookup(g->binder, type_name);
            if (sym && sym->type && (sym->type->kind == TYPE_STRUCT || sym->type->kind == TYPE_CLASS)) {
                LLVMTypeRef st = get_struct_llvm_type(g, sym);
                if (st) {
                    LLVMValueRef alloca;
                    if (sym->type->kind == TYPE_CLASS) {
                        /* reference type: heap-allocate via ARC so instances
                         * outlive the enclosing frame and are leak-tracked. */
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        LLVMValueRef sz = LLVMSizeOf(st);
                        LLVMValueRef site_name = LLVMConstNull(i8ptr);
                        LLVMValueRef site_val = LLVMConstInt(i64, 0, 0);
                        {
                            /* Assign a stable allocation-site index (always: it
                             * also keys dynamic release dispatch to this site's
                             * concrete class). The "file:line:col" descriptor is
                             * only needed for --check-leaks reporting. */
                            zan_type_t *site_inst =
                                resolve_type_ctx(g, expr->new_expr.type);
                            int site_idx = reserve_arc_site(
                                g, sym, site_inst, 0, NULL);
                            site_val = LLVMConstInt(i64, (unsigned long long)site_idx, 0);
                            if (g->check_leaks) {
                                char site_buf[600];
                                const char *sfile = leak_site_file(g, expr->loc);
                                snprintf(site_buf, sizeof(site_buf), "%s:%u:%u",
                                         sfile, expr->loc.line, expr->loc.col);
                                site_name = LLVMBuildGlobalStringPtr(g->builder, site_buf, "site");
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

                    /* install the vtable pointer (field 0) before the ctor runs
                     * so virtual calls made during construction dispatch to the
                     * most-derived implementation. */
                    if (sym->type->kind == TYPE_CLASS && class_has_virtual_methods(sym)) {
                        LLVMTypeRef i8ptr_vt = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMValueRef vtg = get_vtable_global(g, sym);
                        LLVMValueRef vpf0 = LLVMBuildStructGEP2(g->builder, st, alloca, 0, "vpf.init");
                        zan_store_fit(g,
                            LLVMBuildBitCast(g->builder, vtg, i8ptr_vt, "vt.i8"), vpf0);
                    }

                    /* look for constructor. For a concrete instantiation of a
                     * user generic class, prefer the specialized constructor so
                     * the body runs with the instantiation context. */
                    zan_type_t *new_inst = resolve_type_ctx(g, expr->new_expr.type);
                    /* An object initializer (`new T(a) { Field = v, ... }`) is
                     * parsed with the field writes appended to the arg list.
                     * Split them off the tail so construction still resolves
                     * and runs the constructor for the positional args alone,
                     * and the field writes are applied to the built object. */
                    int init_start = expr->new_expr.args.count;
                    while (init_start > 0) {
                        zan_ast_node_t *a =
                            expr->new_expr.args.items[init_start - 1];
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
                    if (ctor_fn) {
                        int argc = ctor_arg_list.count + 1;
                        LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                        call_args[0] = alloca; /* this ptr */
                        /* the fresh object must survive a throwing ctor (or a
                         * throwing arg expression): keep it on the EH temp
                         * stack so a catch can release it */
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
                            /* A generic class's constructor can spell a
                             * parameter in the class's type parameters
                             * (`TextOf<T> f`). Resolve it against the
                             * instantiation, or a lambda argument would be
                             * converted against an erased signature and the
                             * stored delegate crashed when invoked. */
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
                        /* Arguments were supplied but no constructor accepts
                         * them: the object used to be built with its fields at
                         * their initializers and no constructor run at all, so
                         * it came back half-formed. `new C()` keeps falling
                         * back to the field initializers, which is how a class
                         * with only a parameterised constructor is built from
                         * an object initializer. */
                        if (sym->type->kind == TYPE_CLASS &&
                            ctor_arg_list.count > 0 && type_has_ctor(g, sym))
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
                        for (int i = init_start; i < expr->new_expr.args.count; i++) {
                            zan_ast_node_t *arg = expr->new_expr.args.items[i];
                            if (arg->kind == AST_ASSIGNMENT && arg->binary.left->kind == AST_IDENTIFIER) {
                                int fi = get_field_index(sym, arg->binary.left->ident.name);
                                if (fi >= 0) {
                                    LLVMValueRef fptr = emit_field_ptr(g, sym, st, alloca, fi, "finit");
                                    zan_symbol_t *fsym = get_field_sym(sym, arg->binary.left->ident.name);
                                    /* `Binding<T> Field = <T expr>` is the same
                                     * sugar as in a plain assignment: wrap the
                                     * value in a binding instead of storing a
                                     * raw T where a Binding is expected. */
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
                                    /* ARC: retain a borrowed RC value captured into the
                                     * field so it survives release of any source local. */
                                    if (fsym && fsym->type && is_rc_managed_type(fsym->type) &&
                                        !fval_owned &&
                                        !expr_yields_owned_ref(arg->binary.right)) {
                                        emit_rc_retain_for_type(g, fsym->type, fval);
                                    }
                                    zan_store_fit(g, fval, fptr);
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

/* Unify a ternary branch value to the PHI's type. Integer widths and
 * float/double widths are handled by coerce_int_to; an integer branch meeting
 * a float PHI (or vice versa) is converted with sitofp/fptosi so a mixed
 * `cond ? 1.5 : 0` never feeds a mismatched PHI operand. */
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
        if (LLVMTypeOf(cond) != LLVMInt1TypeInContext(g->ctx)) {
            cond = zan_icmp(g->builder, LLVMIntNE, cond,
                                 LLVMConstInt(LLVMTypeOf(cond), 0, 0), "cond");
        }
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

        /* The branch values can disagree in width: an integer literal is always
         * i64 while a method call returns its declared type (i32 for `int`), so
         * `on ? F() : 0` would feed an i64 constant into an i32 PHI and fail
         * LLVM verification. Unify both sides to the inferred type (the then
         * branch's type, as the checker types conditionals) when one exists,
         * otherwise to the wider of the two branches; the conversions live in
         * their branch blocks so they dominate the PHI (which must stay grouped
         * at the top of the merge block). */
        LLVMTypeRef tty = LLVMTypeOf(then_val);
        LLVMTypeRef ety = LLVMTypeOf(else_val);
        LLVMTypeRef phi_ty = tty;
        zan_type_t *conditional_type = infer_expr_type(g, expr, locals);
        if (conditional_type && is_rc_managed_type(conditional_type)) {
            int then_owned = expr_yields_owned_rc_value(
                g, expr->conditional.then_expr, locals);
            int else_owned = expr_yields_owned_rc_value(
                g, expr->conditional.else_expr, locals);
            /* A conditional that can produce an owned reference must have the
             * same (+1) contract on both edges. Retain only the borrowed edge;
             * the receiving assignment/call will then consume or release the
             * PHI exactly as it does any other owned expression. */
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

/* An async frame's result slot is 64 bits wide, so `await f()` loads 64 bits no
 * matter what `f` returns. Decode them back into the callee's declared type --
 * the exact inverse of the encoding the completing coroutine applied (see
 * coerce_to_frame_result). Without it an inferred declaration
 * (`var s = await Echo()`) keeps a string as an integer, and a narrow or
 * 32-bit floating result is read at the wrong width. */
static LLVMValueRef coerce_await_result(zan_irgen_t *g, zan_ast_node_t *expr,
        LLVMValueRef res, local_scope_t *locals) {
    zan_type_t *rt = concretize(g, infer_expr_type(g, expr->await_expr.expr, locals));
    return coerce_from_frame_result(g, res, rt);
}

static LLVMValueRef emit_expr_await_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* await Task.Delay(ms) — time-based suspension (no sub-frame). Inside an
         * async body: register a one-shot timer that will re-ready this frame at
         * now+ms, then SUSPEND; the resume-k block just continues. At a non-async
         * root, sleep synchronously. Delay yields no value (returns i64 0). */
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
                LLVMValueRef selfframe = g->current_async_frame;
                LLVMTypeRef self_ft = g->current_async_frame_type;
                LLVMValueRef self_i8 = LLVMBuildBitCast(g->builder, selfframe, di8ptr, "self");
                emit_async_save_slots(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe, ASYNC_FRAME_STATE, "self.state"));
                zan_call2(g->builder, g->rt_co_delay_type, g->rt_co_delay,
                    (LLVMValueRef[]){ ms, self_i8, g->current_async_resume_fn }, 3, "");
                emit_async_eh_unarm(g);
                LLVMBuildRetVoid(g->builder);

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                emit_async_reload_slots(g);
                return LLVMConstInt(di64, 0, 0);
            }
            /* root: sleep synchronously (no scheduler frame to suspend). Use the
             * same platform primitive the scheduler uses (Sleep / poll), already
             * declared in the module by the runtime emission. */
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

        /* await Gate.Park(handle) — event-driven coroutine wait. Mirrors the
         * Task.Delay lowering but suspends on a runtime gate rather than a
         * timer: the frame is re-readied by zan_gate_signal (see rt_io.c).
         * Yields no value; only valid inside an async body. */
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
            /* the gate runtime rides in the socket-async reactor object; force
             * it to be linked whenever a program parks on a gate. */
            g->uses_socket_async = true;
            LLVMTypeRef gate_park_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                (LLVMTypeRef[]){ di64, di8ptr, g->co_step_ptr }, 3, 0);
            LLVMValueRef gate_park = LLVMGetNamedFunction(g->mod, "zan_gate_park");
            if (!gate_park) {
                gate_park = LLVMAddFunction(g->mod, "zan_gate_park", gate_park_type);
            }
            int k = g->current_async_next_state++;
            LLVMValueRef selfframe = g->current_async_frame;
            LLVMTypeRef self_ft = g->current_async_frame_type;
            LLVMValueRef self_i8 = LLVMBuildBitCast(g->builder, selfframe, di8ptr, "self");
            emit_async_save_slots(g);
            zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0),
                LLVMBuildStructGEP2(g->builder, self_ft, selfframe, ASYNC_FRAME_STATE, "self.state"));
            zan_call2(g->builder, gate_park_type, gate_park,
                (LLVMValueRef[]){ handle, self_i8, g->current_async_resume_fn }, 3, "");
            emit_async_eh_unarm(g);
            LLVMBuildRetVoid(g->builder);

            LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                g->current_async_resume_fn, "co.resume");
            LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
            LLVMPositionBuilderAtEnd(g->builder, rk);
            emit_async_reload_slots(g);
            return LLVMConstInt(di64, 0, 0);
        }

        /* await Socket.ReadReady(fd) / Socket.WriteReady(fd) — readiness-based
         * suspension driven by the IO reactor (S4b-2, path A). Inside an async
         * body: register a one-shot fd watcher via zan_io_wait_co that re-readies
         * this frame when the fd becomes readable/writable, then SUSPEND; the
         * resume-k block continues once ready. The intrinsic yields no value
         * (returns i64 0). Only valid inside an async body — there is no CPS frame
         * to suspend at a non-async root. */
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
                LLVMValueRef selfframe = g->current_async_frame;
                LLVMTypeRef self_ft = g->current_async_frame_type;
                LLVMValueRef self_i8 = LLVMBuildBitCast(g->builder, selfframe, di8ptr, "self");
                emit_async_save_slots(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe, ASYNC_FRAME_STATE, "self.state"));
                zan_call2(g->builder, g->rt_io_wait_co_type, g->rt_io_wait_co,
                    (LLVMValueRef[]){ fd, interest, self_i8, g->current_async_resume_fn }, 4, "");
                emit_async_eh_unarm(g);
                LLVMBuildRetVoid(g->builder);

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                emit_async_reload_slots(g);
                return LLVMConstInt(di64, 0, 0);
            }
        }

        /* await Socket.RecvOv(fd, buf, len) — overlapped receive. Unlike
         * ReadReady (a bare readiness probe followed by a synchronous recv in
         * zan), this posts the real recv via zan_io_recv_co and SUSPENDS; the
         * byte count is written by the reactor into this frame's RESULT slot
         * before it is re-readied, and the resume-k block loads it as the await
         * value. Issuing the recv as one op removes the probe/recv window that
         * leaked completions under the multi-worker driver at high load. */
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
                LLVMValueRef selfframe = g->current_async_frame;
                LLVMTypeRef self_ft = g->current_async_frame_type;
                LLVMValueRef self_i8 = LLVMBuildBitCast(g->builder, selfframe, di8ptr, "self");
                /* &self.result — the reactor stores the recv byte count here. */
                LLVMValueRef out_n = LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                    ASYNC_FRAME_RESULT, "self.iores");
                emit_async_save_slots(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe, ASYNC_FRAME_STATE, "self.state"));
                zan_call2(g->builder, g->rt_io_recv_co_type, g->rt_io_recv_co,
                    (LLVMValueRef[]){ fd, buf, len, self_i8,
                        g->current_async_resume_fn, out_n }, 6, "");
                emit_async_eh_unarm(g);
                LLVMBuildRetVoid(g->builder);

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                emit_async_reload_slots(g);
                /* recompute the result GEP: entry dominates rk, the pre-suspend
                 * block does not. */
                LLVMValueRef res_slot = LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                    ASYNC_FRAME_RESULT, "self.iores2");
                return LLVMBuildLoad2(g->builder, di64, res_slot, "recvn");
            }
        }

        /* await Socket.AcceptOv(fd) — Windows AcceptEx completion. The
         * accepted socket is written into the frame result slot before the
         * suspended state machine is re-readied. */
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
                LLVMValueRef selfframe = g->current_async_frame;
                LLVMTypeRef self_ft = g->current_async_frame_type;
                LLVMValueRef self_i8 = LLVMBuildBitCast(g->builder, selfframe, di8ptr, "self");
                LLVMValueRef out_fd = LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                    ASYNC_FRAME_RESULT, "self.acceptfd");
                emit_async_save_slots(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                        ASYNC_FRAME_STATE, "self.state"));
                zan_call2(g->builder, g->rt_io_accept_co_type,
                    g->rt_io_accept_co, (LLVMValueRef[]){ fd, self_i8,
                        g->current_async_resume_fn, out_fd }, 4, "");
                emit_async_eh_unarm(g);
                LLVMBuildRetVoid(g->builder);

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch,
                    LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                emit_async_reload_slots(g);
                LLVMValueRef res_slot = LLVMBuildStructGEP2(g->builder, self_ft,
                    selfframe, ASYNC_FRAME_RESULT, "self.acceptfd2");
                return LLVMBuildLoad2(g->builder, di64, res_slot, "acceptfd");
            }
        }

        /* await <call> — the awaited expression is a call to an async method's
         * ramp, yielding an i8* task handle (heap frame). The sub's resume/step
         * fn is `<ramp>$resume`, resolved from the call target's name. Two
         * lowerings (see docs/ASYNC_CPS_DESIGN.md):
         *   - inside an async body: register self as the sub's awaiter, schedule
         *     the sub, SUSPEND (save live slots, state=k, ret void); a resume-k
         *     block reloads slots and reads sub.result.
         *   - at a non-async root (e.g. Main): drive the cooperative scheduler
         *     synchronously (init/ready/run) and then read sub.result. */
        LLVMValueRef sub = emit_expr(g, expr->await_expr.expr, locals);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef hdr = g->co_header_type;

        LLVMValueRef sub_resume = NULL;
        if (LLVMIsACallInst(sub)) {
            /* Direct async call: the callee is a known function (the ramp), so
             * its resume is the sibling symbol `<ramp>$resume`. An *indirect*
             * async call (a delegate / function pointer, e.g. `await handler(req)`)
             * has a non-function callee (a loaded value) -- do not mistake its
             * SSA temp name for a function name. */
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

        /* Indirect async call: the `<ramp>$resume` symbol cannot be recovered from
         * the awaited expression. A delegate invoke merges its closure and bare
         * shapes in a phi, an interface dispatch has no called value at all, and
         * a loaded function pointer has no name -- none of them is a call to a
         * named function. Every async ramp stores its own resume fn in the frame
         * header (ASYNC_FRAME_SELF_STEP), so read it off the returned task handle;
         * awaiting through any of them then behaves like a direct async call. */
        if (!sub_resume &&
            LLVMGetTypeKind(LLVMTypeOf(sub)) == LLVMPointerTypeKind) {
            LLVMValueRef sub_hdr = LLVMBuildBitCast(g->builder, sub, i8ptr, "sub.hdr");
            LLVMValueRef ssp = LLVMBuildStructGEP2(g->builder, hdr, sub_hdr,
                ASYNC_FRAME_SELF_STEP, "sub.selfstep.p");
            sub_resume = LLVMBuildLoad2(g->builder, g->co_step_ptr, ssp, "sub.selfstep");
        }

        if (sub_resume && LLVMGetTypeKind(LLVMTypeOf(sub)) == LLVMPointerTypeKind) {
            LLVMValueRef sub_i8 = LLVMBuildBitCast(g->builder, sub, i8ptr, "sub");

            if (g->current_async_frame && g->current_async_switch) {
                int k = g->current_async_next_state++;
                int j = g->current_async_sub_next++;
                LLVMValueRef selfframe = g->current_async_frame;
                LLVMTypeRef self_ft = g->current_async_frame_type;

                /* stash sub handle in a frame slot (survives the suspension) */
                zan_store_fit(g, sub_i8,
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                        (unsigned)(g->current_async_sub_base + j), "sub.slot"));

                /* sub.awaiter = self; sub.awaiter_step = Self$resume */
                LLVMValueRef self_i8 = LLVMBuildBitCast(g->builder, selfframe, i8ptr, "self");
                zan_store_fit(g, self_i8,
                    LLVMBuildStructGEP2(g->builder, hdr, sub_i8, ASYNC_FRAME_AWAITER, "sub.aw"));
                zan_store_fit(g, g->current_async_resume_fn,
                    LLVMBuildStructGEP2(g->builder, hdr, sub_i8, ASYNC_FRAME_AWAITER_STEP, "sub.aws"));
                /* self.child = sub: cancelling this coroutine has to reach the
                 * one it is actually waiting on (cleared again at the top of
                 * the next resume) */
                zan_store_fit(g, sub_i8,
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                        ASYNC_FRAME_CHILD, "self.child"));

                emit_async_save_slots(g);
                zan_store_fit(g, LLVMConstInt(i32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe, ASYNC_FRAME_STATE, "self.state"));
                LLVMValueRef sched_args[] = { sub_i8, sub_resume };
                zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
                emit_async_eh_unarm(g);
                LLVMBuildRetVoid(g->builder);

                /* resume-k: re-entered by the driver once the sub completes */
                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(i32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                emit_async_reload_slots(g);
                /* recompute the sub-slot GEP here (entry dominates rk; the
                 * pre-suspend block does not). */
                LLVMValueRef sub_slot = LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                    (unsigned)(g->current_async_sub_base + j), "sub.slot2");
                LLVMValueRef sub_rl = LLVMBuildLoad2(g->builder, i8ptr, sub_slot, "sub.rl");
                /* the sub may have completed by throwing: re-throw it here,
                 * where this frame has a live invocation and its handlers are
                 * armed again */
                emit_async_check_sub_exc(g, sub_rl);
                LLVMValueRef rptr = LLVMBuildStructGEP2(g->builder, hdr, sub_rl,
                    ASYNC_FRAME_RESULT, "sub.result");
                LLVMValueRef awres = LLVMBuildLoad2(g->builder, i64, rptr, "awres");
                /* The awaited sub-coroutine has completed and we have copied its
                 * result out of its heap frame; free the frame now (the awaiter
                 * owns it once the sub is done). Without this, every awaited
                 * async call leaks its frame -- a per-request leak that grows a
                 * long-running socket server's memory without bound. */
                zan_call2(g->builder, LLVMGlobalGetValueType(g->fn_free),
                    g->fn_free, &sub_rl, 1, "");
                return coerce_await_result(g, expr, awres, locals);
            }

            /* root drive (non-async caller). The scheduler is initialized once
             * at program entry (see emit_main_method); re-initializing here
             * would reset the ready queue and discard any coroutines already
             * enqueued by Task.Spawn before this await -- e.g. a spawned server
             * in a concurrent client/server program would never run. */
            LLVMValueRef sched_args[] = { sub_i8, sub_resume };
            zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
            zan_call2(g->builder, g->rt_co_sched_run_type, g->rt_co_sched_run, NULL, 0, "");
            emit_async_check_sub_exc(g, sub_i8);
            LLVMValueRef rptr = LLVMBuildStructGEP2(g->builder, hdr, sub_i8,
                ASYNC_FRAME_RESULT, "sub.result");
            LLVMValueRef awres = LLVMBuildLoad2(g->builder, i64, rptr, "awres");
            /* Root-driven sub has run to completion; copy out its result then
             * free its heap frame (no awaiter will). */
            zan_call2(g->builder, LLVMGlobalGetValueType(g->fn_free),
                g->fn_free, &sub_i8, 1, "");
            return coerce_await_result(g, expr, awres, locals);
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

static LLVMValueRef emit_expr_cast_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* (Type)x — explicit numeric cast honoring the target type. */
        LLVMValueRef val = emit_expr(g, expr->cast.expr, locals);
        zan_type_t *tt = resolve_type_ctx(g, expr->cast.type);
        LLVMTypeRef target = tt ? map_type(g, tt) : LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef src = LLVMTypeOf(val);
        /* `(int)v` on an `int?` is the explicit unwrapping conversion, and
         * `(int?)x` the wrapping one. */
        if (llvm_is_nullable(src) && !llvm_is_nullable(target)) {
            emit_runtime_check(g,
                LLVMBuildNot(g->builder, nullable_has_value(g, val), "nv.novalue"),
                expr->loc, "Nullable object must have a value");
            val = nullable_get_payload(g, val);
            src = LLVMTypeOf(val);
        } else if (llvm_is_nullable(target) && src != target) {
            return coerce_int_to(g, val, target);
        }
        /* Unsigned targets keep the 64-bit register form even though they are
         * stored narrow, so their cast semantics are wrap-to-width masks
         * (zero-extend for uint/ushort, sign-extend from 8 bits for sbyte)
         * applied on the value widened to 64 bits. */
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
        /* Address <-> pointer: `(WndProc)addr` turns a runtime-resolved
         * address into a callable function pointer, and `(nint)handler` hands
         * a Zan function to native code as a callback address. Both are the
         * building blocks for calling vtable-based APIs (COM) and for any
         * entry point resolved with GetProcAddress/dlsym. */
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

/* True when `t` is the same class as `s` or a base class of it (single
 * inheritance, so the runtime type of an `s`-typed value is always a
 * subclass of `s`). */
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

/* Runtime `x is T` for the strict-ancestor case: the object's allocation site
 * (recorded in its rc header at obj-8) names its concrete class; walk that
 * class's ancestor-name list (see emit_site_tyname_table) looking for the
 * target class name. Returns an i1. */
static LLVMValueRef emit_runtime_is_check(zan_irgen_t *g, LLVMValueRef x,
                                          const char *tname) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
    LLVMValueRef table = LLVMGetNamedGlobal(g->mod, "__zan_site_tynames");
    if (!table) return LLVMConstInt(i1, 0, 0);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef false_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.f");
    LLVMBasicBlockRef true_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.t");
    LLVMBasicBlockRef merge = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.m");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.h");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.l");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.b");
    LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.n");

    /* null object: not a T */
    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, x,
        LLVMConstNull(LLVMTypeOf(x)), "is.nul");
    LLVMBuildCondBr(g->builder, isnull, false_bb, head);

    /* site index at obj-8; out of range -> not a T */
    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef neg8 = LLVMConstInt(i64, (uint64_t)-8, 1);
    LLVMValueRef sptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
        x, &neg8, 1, "is.sptr");
    LLVMValueRef site = LLVMBuildLoad2(g->builder, i64,
        LLVMBuildBitCast(g->builder, sptr, LLVMPointerType(i64, 0), "is.sp"),
        "is.site");
    LLVMValueRef oob = zan_or(g->builder,
        zan_icmp(g->builder, LLVMIntSLT, site, LLVMConstInt(i64, 0, 0), "is.neg"),
        zan_icmp(g->builder, LLVMIntSGE, site,
            LLVMConstInt(i64, g->leak_site_count, 0), "is.oob"),
        "is.oob2");
    LLVMBuildCondBr(g->builder, oob, false_bb, loop);

    /* walk the ancestor-name list */
    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef idx = LLVMBuildPhi(g->builder, i64, "is.i");
    LLVMValueRef list_p = LLVMBuildGEP2(g->builder,
        LLVMArrayType(i8ptr, ZAN_MAX_LEAK_SITES), table,
        (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), site }, 2, "is.lp");
    LLVMValueRef list = LLVMBuildLoad2(g->builder, i8ptr, list_p, "is.list");
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
    LLVMValueRef tstr = LLVMBuildGlobalStringPtr(g->builder, tname, "is.tn");
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
                    (LLVMBasicBlockRef[]){ head, next }, 2);

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
    case AST_INT_LITERAL:
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), (uint64_t)expr->int_val, 1);

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

    case AST_CONDITIONAL:
        return emit_expr_conditional(g, expr, locals);

    case AST_POSTFIX_UNARY:
        /* x++ / x-- — postfix yields the value before the change. */
        return emit_incdec_expr(g, expr, locals, 0);

    case AST_IS_EXPR: {
        /* x is T. With single inheritance, the runtime type of a value whose
         * static type is S is always a subclass of S, so:
         *   - T == S or T a base of S:    true iff x != null
         *   - S a strict base of T:       check the object's runtime class
         *   - S is object/interface:      same runtime check (any class or a
         *                                 different implementor may hide there)
         *   - otherwise:                  false (an S can never be a T)
         * Value types have no runtime type variation: true iff S == T.
         * Strings never match a class: their rc header at obj-8 holds a magic
         * (far above the site range), which the runtime check treats as "no
         * site" and answers false, so `x is SomeClass` is safely false for a
         * string; `x is string` is handled by static types. */
        zan_type_t *st = infer_expr_type(g, expr->type_test.expr, locals);
        zan_type_t *tt = resolve_type_ctx(g, expr->type_test.type);
        if (!st || !tt) return LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
        bool st_ref = st->kind == TYPE_OBJECT || st->kind == TYPE_INTERFACE ||
                      st->kind == TYPE_STRING || st->kind == TYPE_CLASS;
        bool tt_ref = tt->kind == TYPE_OBJECT || tt->kind == TYPE_INTERFACE ||
                      tt->kind == TYPE_STRING || tt->kind == TYPE_CLASS;
        if (!st_ref || !tt_ref)
            return LLVMConstInt(LLVMInt1TypeInContext(g->ctx),
                                types_equal(st, tt) ? 1 : 0, 0);
        /* both reference kinds: value must be non-null for any `is` to hold */
        LLVMValueRef v = emit_expr(g, expr->type_test.expr, locals);
        LLVMValueRef nn = zan_icmp(g->builder, LLVMIntNE, v,
            LLVMConstNull(LLVMTypeOf(v)), "is.nonnull");
        if (st->kind == TYPE_STRING || tt->kind == TYPE_STRING)
            return st->kind == tt->kind ? nn : LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
        if (tt->kind == TYPE_OBJECT || tt->kind == TYPE_INTERFACE)
            return nn;
        if (st->kind == TYPE_CLASS && type_is_or_derives(st, tt))
            return nn;
        if (st->kind == TYPE_CLASS && type_is_or_derives(tt, st)) {
            char tbuf[320];
            int tlen = (int)tt->sym->name.len;
            if (tlen > 319) tlen = 319;
            memcpy(tbuf, tt->sym->name.str, (size_t)tlen);
            tbuf[tlen] = 0;
            return emit_runtime_is_check(g, v, tbuf);
        }
        if (st->kind == TYPE_OBJECT || st->kind == TYPE_INTERFACE) {
            char tbuf[320];
            int tlen = (int)tt->sym->name.len;
            if (tlen > 319) tlen = 319;
            memcpy(tbuf, tt->sym->name.str, (size_t)tlen);
            tbuf[tlen] = 0;
            return emit_runtime_is_check(g, v, tbuf);
        }
        return LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
    }

    case AST_AS_EXPR: {
        /* x as T — downcasts check the runtime class and yield null on
         * failure; upcasts/identity/value casts pass the value through. */
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
        /* target is a class; static source may be a class, object or
         * interface */
        LLVMValueRef v = emit_expr(g, expr->type_test.expr, locals);
        if (st->kind == TYPE_CLASS && type_is_or_derives(st, tt))
            return v; /* upcast: always succeeds */
        if (st->kind == TYPE_CLASS && !type_is_or_derives(tt, st))
            return v; /* unrelated classes: pass through unchanged (never
                       * matches at runtime either way) */
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
        /* typeof(T) — the type's display name as a string */
        char buf[256];
        int len = render_type_ref_name(expr->cast.type, buf, (int)sizeof(buf));
        char *copy = zan_arena_strdup(g->arena, buf, (size_t)len);
        return emit_string_literal_rc(g, (zan_istr_t){ copy, (uint32_t)len });
    }

    case AST_THIS_EXPR: {
        /* `this` — load from the receiver alloca (g->current_this) when set. That
         * alloca is the single source of truth for the receiver and, in an async
         * method, is reloaded from the heap frame at every state block. The
         * function's param 0 is NOT usable here for async methods: the resume
         * function's param 0 is the frame pointer, not the receiver. Fall back to
         * param 0 only when there is no receiver alloca (defensive). */
        if (g->current_this) {
            return LLVMBuildLoad2(g->builder, LLVMGetAllocatedType(g->current_this),
                g->current_this, "this");
        }
        /* No receiver is bound: inside a lambda (non-capturing, A33) `this` was
         * silently lowered to param 0 / null and crashed on first field access.
         * A static method/initializer has no receiver at all. Reject instead of
         * emitting garbage. */
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

    default:
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    }
}

/* ---- lambda captures (A33-2) ---------------------------------------------
 * A lambda that mentions an enclosing local or `this` is lowered to a closure:
 * the mentioned values are copied into a heap record at the point the lambda
 * value is created (by value, rc values retained) and the lambda body reads
 * them from that record. Capture is by value, so a later write to the
 * enclosing local is not observed by the lambda -- and, symmetrically, the
 * lambda cannot dangle once the enclosing frame is gone. */
#define ZAN_MAX_CAPTURES 32

typedef struct {
    zan_istr_t   name;   /* captured local's name; empty for the receiver */
    LLVMValueRef slot;   /* the enclosing alloca, NULL for the receiver;
                          * for a boxed capture, the heap cell instead */
    zan_type_t  *type;
    LLVMTypeRef  llvm;
    /* 1 when the enclosing local is boxed (A33-2b): the record holds a
     * reference to its cell, not a copy of its value, so writes on either
     * side are writes to the one variable. */
    int          boxed;
} lambda_capture_t;

typedef struct {
    zan_irgen_t     *g;
    local_scope_t   *outer;
    lambda_capture_t caps[ZAN_MAX_CAPTURES];
    int              count;
    int              needs_this;
    zan_istr_t       shadow[ZAN_MAX_CAPTURES * 4];
    int              shadow_count;
    int              overflow;
    /* write scan (A33-2b): instead of collecting captures, report whether a
     * lambda somewhere below assigns to `want_write`. */
    zan_istr_t       want_write;
    int              lam_depth;
    int              found_write;
} capture_scan_t;

static int istr_eq_c(zan_istr_t a, zan_istr_t b) {
    return a.len == b.len && a.len > 0 &&
           memcmp(a.str, b.str, (size_t)a.len) == 0;
}

static void cap_shadow(capture_scan_t *cs, zan_istr_t name) {
    if (!name.len) return;
    for (int i = 0; i < cs->shadow_count; i++)
        if (istr_eq_c(cs->shadow[i], name)) return;
    if (cs->shadow_count >= (int)(sizeof(cs->shadow) / sizeof(cs->shadow[0]))) {
        cs->overflow = 1;
        return;
    }
    cs->shadow[cs->shadow_count++] = name;
}

static int cap_is_shadowed(capture_scan_t *cs, zan_istr_t name) {
    for (int i = 0; i < cs->shadow_count; i++)
        if (istr_eq_c(cs->shadow[i], name)) return 1;
    return 0;
}

/* Instance member of the enclosing type referred to by a bare name: using one
 * inside a lambda is an implicit `this.` and therefore captures the receiver. */
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
    if (cs->count >= ZAN_MAX_CAPTURES) { cs->overflow = 1; return; }
    lambda_capture_t *c = &cs->caps[cs->count++];
    c->name = name;
    c->type = lv->type;
    c->boxed = lv->box_cell ? 1 : 0;
    if (c->boxed) {
        c->slot = lv->box_cell;
        c->llvm = LLVMPointerType(LLVMInt8TypeInContext(cs->g->ctx), 0);
    } else {
        c->slot = lv->alloca;
        c->llvm = local_slot_type(cs->g, lv);
    }
}

/* Does this node assign to `name`? Assignment, `++`/`--` and passing it as
 * `ref`/`out` all write the variable. */
static int node_writes_ident(zan_ast_node_t *n, zan_istr_t name) {
    zan_ast_node_t *t = NULL;
    if (n->kind == AST_ASSIGNMENT) t = n->binary.left;
    else if ((n->kind == AST_UNARY || n->kind == AST_POSTFIX_UNARY) &&
             (n->unary.op == TK_PLUS_PLUS || n->unary.op == TK_MINUS_MINUS))
        t = n->unary.operand;
    else if (n->kind == AST_REF_ARG) t = n->ref_arg.expr;
    return t && t->kind == AST_IDENTIFIER && istr_eq_c(t->ident.name, name);
}

static void cap_scan(capture_scan_t *cs, zan_ast_node_t *n);

static void cap_scan_list(capture_scan_t *cs, zan_ast_list_t *l) {
    for (int i = 0; l && i < l->count; i++) cap_scan(cs, l->items[i]);
}

static void cap_scan(capture_scan_t *cs, zan_ast_node_t *n) {
    if (!n) return;
    if (cs->want_write.len) {
        if (cs->found_write) return;
        if (cs->lam_depth > 0 && node_writes_ident(n, cs->want_write)) {
            cs->found_write = 1;
            return;
        }
    }
    switch (n->kind) {
    case AST_IDENTIFIER:      cap_use(cs, n->ident.name); return;
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
    case AST_CAST_EXPR:       cap_scan(cs, n->cast.expr); return;
    case AST_IS_EXPR:
    case AST_AS_EXPR:         cap_scan(cs, n->type_test.expr); return;
    case AST_AWAIT_EXPR:      cap_scan(cs, n->await_expr.expr); return;
    case AST_REF_ARG:         cap_scan(cs, n->ref_arg.expr); return;
    case AST_STRING_INTERP:   cap_scan_list(cs, &n->string_interp.parts); return;
    case AST_QUERY_EXPR:      cap_scan(cs, n->query.source);
                              cap_shadow(cs, n->query.var);
                              cap_scan_list(cs, &n->query.wheres);
                              cap_scan(cs, n->query.select); return;
    case AST_LAMBDA:
        /* a nested lambda's own parameters shadow the enclosing names */
        for (int i = 0; i < n->lambda.params.count; i++)
            cap_shadow(cs, n->lambda.params.items[i]->param.name);
        cs->lam_depth++;
        cap_scan(cs, n->lambda.body);
        cs->lam_depth--;
        return;
    case AST_BLOCK:           cap_scan_list(cs, &n->block.stmts); return;
    case AST_VAR_DECL:        cap_scan(cs, n->var_decl.initializer);
                              cap_shadow(cs, n->var_decl.name); return;
    case AST_EXPR_STMT:       cap_scan(cs, n->expr_stmt.expr); return;
    case AST_RETURN_STMT:     cap_scan(cs, n->ret.value); return;
    case AST_IF_STMT:         cap_scan(cs, n->if_stmt.cond);
                              cap_scan(cs, n->if_stmt.then_body);
                              cap_scan(cs, n->if_stmt.else_body); return;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:   cap_scan(cs, n->while_stmt.cond);
                              cap_scan(cs, n->while_stmt.body); return;
    case AST_FOR_STMT:        cap_scan(cs, n->for_stmt.init);
                              cap_scan(cs, n->for_stmt.cond);
                              cap_scan(cs, n->for_stmt.step);
                              cap_scan(cs, n->for_stmt.body); return;
    case AST_FOREACH_STMT:    cap_scan(cs, n->foreach_stmt.collection);
                              cap_shadow(cs, n->foreach_stmt.var_name);
                              cap_scan(cs, n->foreach_stmt.body); return;
    case AST_THROW_STMT:      cap_scan(cs, n->throw_stmt.value); return;
    case AST_TRY_STMT:        cap_scan(cs, n->try_stmt.try_body);
                              cap_scan_list(cs, &n->try_stmt.catches);
                              cap_scan(cs, n->try_stmt.finally_body); return;
    case AST_CATCH_CLAUSE:    cap_shadow(cs, n->catch_clause.var_name);
                              cap_scan(cs, n->catch_clause.body); return;
    case AST_SWITCH_STMT:     cap_scan(cs, n->switch_stmt.expr);
                              cap_scan_list(cs, &n->switch_stmt.cases); return;
    case AST_SWITCH_CASE:     cap_scan(cs, n->switch_case.pattern);
                              cap_scan(cs, n->switch_case.body); return;
    case AST_LOCK_STMT:       cap_scan(cs, n->lock_stmt.expr);
                              cap_scan(cs, n->lock_stmt.body); return;
    case AST_YIELD_STMT:      cap_scan(cs, n->yield_stmt.value); return;
    default: return;
    }
}

/* void __zan_clo_dtor_N(i8* rec): on the release that drops the last
 * reference, release the rc-managed captures, then hand the record to the
 * ordinary rc decrement (which frees it). */
static LLVMValueRef build_closure_dtor(zan_irgen_t *g, const char *lname,
                                       LLVMTypeRef rec_ty,
                                       lambda_capture_t *caps, int capc,
                                       int has_this) {
    LLVMContextRef c = g->ctx;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    char name[160];
    snprintf(name, sizeof(name), "__zan_clo_dtor_%s", lname);
    /* method-group records reuse one dtor per method (see the stable name
     * built there); lambda names are already unique */
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
    LLVMValueRef neg16 = LLVMConstInt(i64, (unsigned long long)-16, 1);
    LLVMValueRef rcp = LLVMBuildGEP2(b, LLVMInt8TypeInContext(c), rec, &neg16, 1, "rcp");
    LLVMValueRef rcip = LLVMBuildBitCast(b, rcp, LLVMPointerType(i64, 0), "rcip");
    LLVMValueRef rc = LLVMBuildLoad2(b, i64, rcip, "rc");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, rc, LLVMConstInt(i64, 1, 0), "is1"),
                    drop, dec);
    LLVMPositionBuilderAtEnd(b, drop);
    /* the bound receiver of a method group (null for a lambda; the release is
     * null-tolerant) */
    {
        LLVMValueRef p = LLVMBuildStructGEP2(b, rec_ty, rec, 2, "tgp");
        emit_arc_release_typed(g, NULL, LLVMBuildLoad2(b, i8ptr, p, "tgv"));
    }
    for (int i = 0; i < capc; i++) {
        if (!caps[i].boxed && !is_rc_managed_type(caps[i].type)) continue;
        LLVMValueRef p = LLVMBuildStructGEP2(g->builder, rec_ty, rec,
            (unsigned)(ZAN_CLOSURE_HDR_FIELDS + i), "cp");
        LLVMValueRef v = LLVMBuildLoad2(g->builder, caps[i].llvm, p, "cv");
        /* a boxed capture is a reference to the variable's cell, so this
         * closure drops its reference to the cell, not to a value */
        if (caps[i].boxed) emit_closure_record_release(g, v);
        else emit_rc_release_for_type(g, caps[i].type, v);
    }
    if (has_this) {
        LLVMValueRef p = LLVMBuildStructGEP2(g->builder, rec_ty, rec,
            (unsigned)(ZAN_CLOSURE_HDR_FIELDS + capc), "tp");
        LLVMValueRef v = LLVMBuildLoad2(g->builder, i8ptr, p, "tv");
        emit_arc_release_typed(g, NULL, v);
    }
    LLVMBuildBr(g->builder, dec);
    LLVMPositionBuilderAtEnd(g->builder, dec);
    zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0),
              g->rt_release, &rec, 1, "");
    LLVMBuildRetVoid(g->builder);
    LLVMDisposeBuilder(b);
    g->builder = saved;
    return fn;
}

/* Allocate and populate a closure record in the current frame and return the
 * tagged delegate value. `caps` are read from their enclosing slots, `self`
 * (when the record has a receiver field) is already loaded. */
static LLVMValueRef emit_closure_record(zan_irgen_t *g, zan_loc_t loc,
                                        const char *lname, LLVMTypeRef rec_ty,
                                        LLVMValueRef fn_ptr, LLVMValueRef target,
                                        lambda_capture_t *caps, int capc,
                                        int has_this, LLVMValueRef self) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef dtor = build_closure_dtor(g, lname, rec_ty, caps, capc, has_this);
    int site_idx = reserve_closure_site(g);
    LLVMValueRef site_name = LLVMConstNull(i8ptr);
    if (g->check_leaks) {
        char site_buf[600];
        snprintf(site_buf, sizeof(site_buf), "%s:%u:%u [closure]",
                 leak_site_file(g, loc), loc.line, loc.col);
        site_name = LLVMBuildGlobalStringPtr(g->builder, site_buf, "site");
    }
    LLVMValueRef alloc_args[3] = {
        LLVMBuildPtrToInt(g->builder, LLVMSizeOf(rec_ty), i64, "clo.size"),
        LLVMConstInt(i64, (unsigned long long)site_idx, 0), site_name };
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
        emit_arc_retain(g, target);
    }
    LLVMBuildStore(g->builder, target ? target : LLVMConstNull(i8ptr),
        LLVMBuildStructGEP2(g->builder, rec_ty, rec, 2, "clo.tgp"));
    for (int i = 0; i < capc; i++) {
        if (caps[i].boxed) {
            LLVMValueRef cell = caps[i].slot;
            emit_arc_retain(g, cell);
            LLVMBuildStore(g->builder, cell,
                LLVMBuildStructGEP2(g->builder, rec_ty, rec,
                                    (unsigned)(ZAN_CLOSURE_HDR_FIELDS + i), "cap.bp"));
            continue;
        }
        LLVMValueRef v = LLVMBuildLoad2(g->builder, caps[i].llvm, caps[i].slot, "cap.v");
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

/* ---- boxed locals (A33-2b) ------------------------------------------------
 * C# captures variables, not values: a lambda that writes an enclosing local
 * writes *that* local, and the enclosing method sees it. Such a local is
 * therefore moved off the stack into a heap cell with the closure-record shape
 * { fn = null, dtor, target = null, value }, and its storage slot becomes a
 * pointer into that cell. Both sides then load and store the same memory, and
 * the cell's lifetime is the ordinary closure lifetime: the declaring scope
 * holds one reference, every closure capturing the variable holds another, and
 * the last one out frees it -- so the variable also outlives the frame when the
 * lambda does.
 *
 * Only locals a lambda actually assigns to are boxed; a read-only capture stays
 * a copy, which is cheaper and observationally identical. */
#define ZAN_BOX_VALUE_FIELD ZAN_CLOSURE_HDR_FIELDS

static LLVMTypeRef box_cell_type(zan_irgen_t *g, LLVMTypeRef payload) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fields[ZAN_BOX_VALUE_FIELD + 1] = { i8ptr, i8ptr, i8ptr, payload };
    return LLVMStructTypeInContext(g->ctx, fields, ZAN_BOX_VALUE_FIELD + 1, 0);
}

/* Does a lambda in the body being compiled assign to `name`? */
static int local_is_lambda_written(zan_irgen_t *g, local_scope_t *locals,
                                   zan_istr_t name) {
    if (!g->current_fn_body || !name.len) return 0;
    capture_scan_t cs;
    memset(&cs, 0, sizeof(cs));
    cs.g = g;
    cs.outer = locals;
    cs.want_write = name;
    cap_scan(&cs, g->current_fn_body);
    return cs.found_write;
}

/* Allocate the cell for a boxed local and return it; `init` (may be null) is
 * its initial value. */
static LLVMValueRef emit_box_cell(zan_irgen_t *g, zan_loc_t loc,
                                  LLVMTypeRef payload, zan_type_t *vtype,
                                  LLVMValueRef init) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef rec_ty = box_cell_type(g, payload);
    static int box_id = 0;
    char bname[64];
    snprintf(bname, sizeof(bname), "box_%d", box_id++);
    lambda_capture_t val = { .name = (zan_istr_t){ NULL, 0 }, .slot = NULL,
                             .type = vtype, .llvm = payload, .boxed = 0 };
    LLVMValueRef dtor = build_closure_dtor(g, bname, rec_ty, &val, 1, 0);
    int site_idx = reserve_closure_site(g);
    LLVMValueRef site_name = LLVMConstNull(i8ptr);
    if (g->check_leaks) {
        char site_buf[600];
        snprintf(site_buf, sizeof(site_buf), "%s:%u:%u [captured local]",
                 leak_site_file(g, loc), loc.line, loc.col);
        site_name = LLVMBuildGlobalStringPtr(g->builder, site_buf, "site");
    }
    LLVMValueRef alloc_args[3] = {
        LLVMBuildPtrToInt(g->builder, LLVMSizeOf(rec_ty), i64, "box.size"),
        LLVMConstInt(i64, (unsigned long long)site_idx, 0), site_name };
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

/* The storage slot of a boxed local: a pointer to the value inside its cell,
 * used exactly like an alloca by every read and write of the variable. */
static LLVMValueRef box_value_ptr(zan_irgen_t *g, LLVMValueRef cell,
                                  LLVMTypeRef payload) {
    return LLVMBuildStructGEP2(g->builder, box_cell_type(g, payload), cell,
                               ZAN_BOX_VALUE_FIELD, "box.v");
}

/* `obj.M` / bare `M` naming an instance method: bind the receiver into a
 * closure whose function is a thunk that re-supplies it, so the delegate is
 * callable through the ordinary (receiver-less) delegate signature. */
static LLVMValueRef emit_method_group_closure(zan_irgen_t *g, zan_symbol_t *msym,
                                              LLVMValueRef recv, zan_loc_t loc) {
    int fi = irgen_find_function(g, msym);
    if (fi < 0 || !recv) return NULL;
    LLVMValueRef target = g->functions[fi].fn;
    LLVMTypeRef target_ty = g->functions[fi].fn_type;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    unsigned np = LLVMCountParamTypes(target_ty);
    if (np < 1) return NULL;   /* no receiver parameter: not an instance method */
    LLVMTypeRef *tp = (LLVMTypeRef *)calloc((size_t)np, sizeof(LLVMTypeRef));
    LLVMGetParamTypes(target_ty, tp);
    LLVMTypeRef ret = LLVMGetReturnType(target_ty);

    /* One thunk per method, not per use site: delegate equality compares the
     * function pointer, so `E -= obj.M` must produce the same function as the
     * `E += obj.M` that subscribed it. */
    char lname[128];
    snprintf(lname, sizeof(lname), "mg_%s", LLVMGetValueName(target));

    /* record: { fn, dtor, target } -- the bound receiver is the target */
    LLVMTypeRef fields[ZAN_CLOSURE_HDR_FIELDS] = { i8ptr, i8ptr, i8ptr };
    char rname[128];
    snprintf(rname, sizeof(rname), "%s$clo", lname);
    LLVMTypeRef rec_ty = LLVMStructCreateNamed(g->ctx, rname);
    LLVMStructSetBody(rec_ty, fields, ZAN_CLOSURE_HDR_FIELDS, 0);

    LLVMTypeRef *thunk_params = (LLVMTypeRef *)calloc((size_t)np, sizeof(LLVMTypeRef));
    thunk_params[0] = i8ptr;
    for (unsigned i = 1; i < np; i++) thunk_params[i] = tp[i];
    LLVMTypeRef thunk_ty = LLVMFunctionType(ret, thunk_params, np, 0);
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
        return emit_closure_record(g, loc, lname, rec_ty, thunk, recv,
                                   NULL, 0, 0, NULL);
    }
    LLVMPositionBuilderAtEnd(g->builder,
        LLVMAppendBasicBlockInContext(g->ctx, thunk, "entry"));
    LLVMValueRef rec = LLVMGetParam(thunk, 0);
    LLVMValueRef sp = LLVMBuildStructGEP2(g->builder, rec_ty, rec, 2, "mg.selfp");
    LLVMValueRef self = LLVMBuildLoad2(g->builder, i8ptr, sp, "mg.self");
    LLVMValueRef *cargs = (LLVMValueRef *)calloc((size_t)np, sizeof(LLVMValueRef));
    cargs[0] = LLVMTypeOf(self) == tp[0]
        ? self : LLVMBuildBitCast(g->builder, self, tp[0], "mg.self.c");
    for (unsigned i = 1; i < np; i++) cargs[i] = LLVMGetParam(thunk, i);
    LLVMValueRef r = zan_call2(g->builder, target_ty, target, cargs, np,
        LLVMGetTypeKind(ret) == LLVMVoidTypeKind ? "" : "mg.r");
    if (LLVMGetTypeKind(ret) == LLVMVoidTypeKind) LLVMBuildRetVoid(g->builder);
    else LLVMBuildRet(g->builder, r);
    free(cargs);
    free(thunk_params);
    free(tp);
    LLVMPositionBuilderAtEnd(g->builder, saved_bb);

    return emit_closure_record(g, loc, lname, rec_ty, thunk, recv, NULL, 0, 0, NULL);
}

static LLVMValueRef emit_lambda_typed(zan_irgen_t *g, zan_ast_node_t *expr,
                                      zan_type_t *expected, local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int pc = expr->lambda.params.count;
    bool exp_delegate = expected && expected->kind == TYPE_DELEGATE;

    /* Resolve each parameter's zan type: prefer an explicit annotation on the
     * lambda, otherwise borrow it from the target delegate signature. */
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

    /* Which enclosing locals (and receiver) does the body mention? Those are
     * copied into a closure record and the body reads them from there. */
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
            "lambda captures too many variables (limit %d)", ZAN_MAX_CAPTURES);
        cs.count = 0;
        cs.needs_this = 0;
    }
    int capc = cs.count;
    int has_this = cs.needs_this ? 1 : 0;
    bool is_closure = (capc + has_this) > 0;

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
    snprintf(lname, sizeof(lname), "lambda_%d", lambda_id++);
    LLVMValueRef lambda_fn = LLVMAddFunction(g->mod, lname, fn_type);

    /* { ptr fn, ptr dtor, ptr target, <captures>, [ptr this] }; a lambda has no
     * bound target, so that slot stays null (see the closure record layout). */
    LLVMTypeRef rec_ty = NULL;
    if (is_closure) {
        LLVMTypeRef fields[ZAN_CLOSURE_HDR_FIELDS + ZAN_MAX_CAPTURES + 1];
        for (int i = 0; i < ZAN_CLOSURE_HDR_FIELDS; i++) fields[i] = i8ptr;
        for (int i = 0; i < capc; i++)
            fields[ZAN_CLOSURE_HDR_FIELDS + i] = cs.caps[i].llvm;
        if (has_this) fields[ZAN_CLOSURE_HDR_FIELDS + capc] = i8ptr;
        char rname[80];
        snprintf(rname, sizeof(rname), "%s$clo", lname);
        rec_ty = LLVMStructCreateNamed(g->ctx, rname);
        LLVMStructSetBody(rec_ty, fields,
                          (unsigned)(ZAN_CLOSURE_HDR_FIELDS + capc + has_this), 0);
    }

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, lambda_fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);

    /* The body must emit into the lambda's own function: control-flow blocks
     * append to current_fn and `return` coerces to current_fn_ret. The
     * instance/async context of the enclosing method does not carry over; a
     * captured receiver is rebound below from the closure record. */
    LLVMValueRef saved_fn = g->current_fn;
    LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
    zan_type_t *saved_fn_zan_ret = g->current_fn_zan_ret_type;
    LLVMValueRef saved_this = g->current_this;
    LLVMValueRef saved_async_frame = g->current_async_frame;
    zan_ast_node_t *saved_fn_body = g->current_fn_body;
    /* the lambda body is the body being compiled: a local declared in it is
     * boxed when a nested lambda writes it, exactly like a method's */
    g->current_fn_body = expr->lambda.body;
    g->current_fn = lambda_fn;
    g->current_fn_ret_type = ret_type;
    g->current_fn_zan_ret_type = rett;
    int saved_throw_base = g->throw_locals_base;
    int saved_catch_cc = g->catch_cleanup_count;
    int saved_throw_cb = g->throw_catch_base;
    g->throw_locals_base = 0;
    g->catch_cleanup_count = 0;
    g->throw_catch_base = 0;
    g->current_this = NULL;
    g->current_async_frame = NULL;
    g->lambda_depth++;

    local_scope_t lambda_locals;
    local_scope_init(&lambda_locals, g->arena);
    /* Captures come first: they are plain locals inside the body, loaded once
     * from the record so the body needs no further closure awareness. */
    if (is_closure) {
        LLVMValueRef rec = LLVMGetParam(lambda_fn, 0);
        for (int i = 0; i < capc; i++) {
            LLVMValueRef p = LLVMBuildStructGEP2(g->builder, rec_ty, rec,
                (unsigned)(ZAN_CLOSURE_HDR_FIELDS + i), "cap.p");
            LLVMValueRef v = LLVMBuildLoad2(g->builder, cs.caps[i].llvm, p, "cap");
            if (cs.caps[i].boxed) {
                /* the record holds the variable's cell: bind the body's local
                 * to the value inside it, so writes here are writes there */
                LLVMTypeRef payload = map_type(g, cs.caps[i].type);
                local_add(&lambda_locals, cs.caps[i].name,
                          box_value_ptr(g, v, payload), cs.caps[i].type);
                /* borrowed: the closure record owns this reference to the
                 * cell, so a nested lambda can share it but the body's scope
                 * exit must not release it */
                lambda_locals.vars[lambda_locals.count - 1].box_cell = v;
                /* an rc-managed variable is owned by the cell wherever it is
                 * written, so a write here swaps the reference too */
                if (is_rc_managed_type(cs.caps[i].type))
                    lambda_locals.vars[lambda_locals.count - 1].arc_owned = 1;
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
        LLVMValueRef alloc = LLVMBuildAlloca(g->builder, lt, "lp");
        zan_store_fit(g, LLVMGetParam(lambda_fn, (unsigned)(is_closure ? k + 1 : k)), alloc);
        local_add(&lambda_locals, param->param.name, alloc,
                  ptypes[k] ? ptypes[k] : g->binder->type_int);
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
            /* An expression-bodied lambda returning a borrowed managed value
             * (field/local/index access) must hand it out +1, matching the
             * method expression-body path: callers treat a delegate-call
             * result as owned and release it, so a borrowed return would be
             * over-released. Owned expressions (calls, new, concat) are left
             * as-is. Numeric returns are not rc-managed and are untouched. */
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
    g->current_fn_ret_type = saved_fn_ret;
    g->current_fn_zan_ret_type = saved_fn_zan_ret;
    g->throw_locals_base = saved_throw_base;
    g->catch_cleanup_count = saved_catch_cc;
    g->throw_catch_base = saved_throw_cb;
    g->current_this = saved_this;
    g->current_async_frame = saved_async_frame;
    g->current_fn_body = saved_fn_body;
    g->lambda_depth--;
    LLVMPositionBuilderAtEnd(g->builder, saved_bb);
    free(param_types);
    free(ptypes);
    if (!is_closure) return lambda_fn;

    LLVMValueRef self = has_this
        ? LLVMBuildLoad2(g->builder, LLVMGetAllocatedType(saved_this),
                         saved_this, "cap.self")
        : NULL;
    return emit_closure_record(g, expr->loc, lname, rec_ty, lambda_fn, NULL,
                               cs.caps, capc, has_this, self);
}

static zan_type_t *method_param_type(zan_irgen_t *g, zan_symbol_t *msym, int idx) {
    if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return NULL;
    zan_ast_list_t *params = &msym->decl->method_decl.params;
    if (idx < 0 || idx >= params->count) return NULL;
    zan_ast_node_t *p = params->items[idx];
    if (!p || !p->param.type) return NULL;
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

/* Structurally match a declared (possibly type-parameterised) type against a
 * concrete argument type, recording each type parameter's binding. */
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

/* Substitute a generic method's own type parameters in `t` using `bind`,
 * cloning composite types (delegates, generic instantiations) as needed. */
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

/* Determine the bindings of a generic method's own type parameters at a call
 * site: explicit type arguments when present, otherwise inferred by matching
 * declared parameter types against the actual argument types (receiver
 * included for extension methods, in which case `recv_expr` is the receiver
 * bound to declared parameter 0). */
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

    /* Second pass: lambda arguments. A delegate parameter can be the only
     * mention of a type parameter (e.g. R in `Sel<T, R> key`): once the
     * lambda's parameter types are known (explicit annotation, or the
     * delegate's parameter types after the first pass), the lambda body's
     * inferred type binds the delegate's declared return type. Explicitly
     * annotated lambda parameters also bind the delegate's parameter types. */
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

/* method_param_type with the generic method's type parameters substituted for
 * this call site. This is what lets an untyped lambda argument (`x => ...`)
 * get its parameter types from e.g. a `Pred<T>` delegate once T is known. */
static zan_type_t *method_param_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                        int idx, zan_ast_node_t *call,
                                        zan_ast_node_t *recv_expr,
                                        local_scope_t *locals) {
    zan_type_t *pt = method_param_type(g, msym, idx);
    if (!pt) return pt;
    /* A delegate parameter spelled in the enclosing generic class's type
     * parameters (`DblOfG<T> f` inside `Col<T>`) must be rebuilt against the
     * receiver's instantiation so an untyped lambda argument gets concrete
     * types (`DblOfG<Person>`). Without this the lambda's parameter stays the
     * bare type parameter T and its body -- `x => x.balance` -- cannot resolve
     * the field, emitting a default whose type clashes with the (concrete)
     * delegate return type: an LLVM verify failure for numeric returns.
     * Mirrors the constructor-argument path (subst_delegate_sig on new_inst). */
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

/* True when the method's declared return type is one of its own bare type
 * parameters (e.g. `static T First<T>(...)`). The erased generic body cannot
 * know T is a class, so it returns a borrowed (+0) reference; call sites that
 * treat call results as owned must retain such results. */
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

/* Declared return type of a generic method with its type parameters
 * substituted for this call site (identity for non-generic methods). */
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

/* An `async delegate` is invoked by awaiting the callee's coroutine frame,
 * while a plain delegate call consumes the returned value directly, so the
 * two conversions are not interchangeable: binding a method of the wrong
 * kind makes the invocation await something that is not a frame (or return a
 * frame as the result). Diagnose the conversion instead of crashing at
 * runtime. */
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

static LLVMValueRef emit_arg_typed(zan_irgen_t *g, zan_ast_node_t *arg,
                                   zan_type_t *ptype, local_scope_t *locals) {
    zan_type_t *atype = infer_expr_type(g, arg, locals);
    check_implicit_narrowing(g, ptype, atype, arg, "argument");
    check_value_type_mismatch(g, ptype, atype, arg, "argument");
    if (arg && ptype && ptype->kind == TYPE_DELEGATE) {
        if (arg->kind == AST_LAMBDA)
            return emit_lambda_typed(g, arg, ptype, locals);
        check_delegate_async_match(g, arg, ptype, locals);
    }
    LLVMValueRef v = emit_expr(g, arg, locals);
    /* An integer meeting a float/double parameter is converted, not passed as
     * its bit pattern: `D(2)` against `double D(double)` used to fail LLVM
     * verification at the call site. */
    if (v && ptype && (ptype->kind == TYPE_DOUBLE || ptype->kind == TYPE_FLOAT) &&
        LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(LLVMTypeOf(v)) > 1)
        v = coerce_int_to(g, v, map_type(g, ptype));
    /* `f(5)` / `f(null)` against a `T?` parameter passes the wrapped value. */
    if (v && ptype && ptype->kind == TYPE_NULLABLE)
        v = coerce_int_to(g, v, map_type(g, ptype));
    return v;
}

/* `ref x` / `out x` / `out T x` argument: pass the address of the local's
 * storage slot. An inline `out T x` declares a fresh zero-initialised local
 * in the caller's scope first. */
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
    }
    return emit_expr(g, tgt, locals);
}
