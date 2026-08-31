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

/* Whether `cls` or one of its base classes declares a member named `name`,
 * of any kind (field, method, property, event, constant). */
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

/* Whether a string-typed expression's NUL terminator is a reliable ordinal
 * bound for the string index guards. Only string literals and non-opaque
 * local identifiers qualify. Everything else -- string parameters, extern
 * call results and raw FFI buffers stored in fields or returned by methods
 * (FbReader/Tds codecs keep wire data in `string` fields indexed against an
 * explicit length) -- may carry bytes past the first NUL, so strlen would
 * either fail good code or, worse, silently pass with a wrong bound. */
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
            LLVMValueRef window = (expr->call.args.count >= 2)
                ? coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals), i64t)
                : LLVMBuildSub(g->builder, len, start, "span.len");
            emit_span_window_check(g, start, window, len, expr->loc, "span");
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
        /* normalize a null receiver to the shared empty string before
         * handing it to strlen */
        LLVMValueRef obj_v = emit_expr(g, callee->member.object, locals);
        LLVMValueRef s = emit_str_nonnull(g, obj_v);
        LLVMTypeRef strlen_ty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i8ptr }, 1, 0);
        LLVMValueRef len = zan_call2(g->builder, strlen_ty, g->fn_strlen, &s, 1, "tb.len");
        LLVMValueRef arr = zan_array_alloc(g, len, len);
        zan_call2(g->builder, memcpy_ty, memcpy_fn,
                  (LLVMValueRef[]){ arr, s, len }, 3, "");
        /* A chained receiver (`sb.ToString().ToBytes()`) is an owned
         * temporary this lowering consumed; a plain local passes through
         * untouched. Missing this release leaked the receiver string on
         * every call. */
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
        /* Clamp the requested window into the buffer: a negative or
         * oversized offset/length would otherwise drive the memcpy below
         * far past the allocation. An out-of-range request yields an
         * empty string instead of memory corruption. */
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
    /* Same owned-receiver release as the to_bytes branch above — a chained
     * `zip.Extract(...).ToStr()` leaked the receiver array on every call. */
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

    /* Find(p, off, b, n) -> memchr(p + off, b, n), reported as an offset from
     * `p` (not from `p + off`) so a scan can be resumed with the returned
     * index, and -1 when the byte is absent. A byte-at-a-time Zan loop pays a
     * bounds check and a load per byte; memchr is the vectorised equivalent
     * and it is what the framer's header scan spends its time in. */
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

    if (is_call_to(expr, "NativeMemory", "GetString") && expr->call.args.count == 3) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef off = nm_arg(g, expr, 1, locals);
        LLVMValueRef len = nm_arg(g, expr, 2, locals);
        /* a negative length would wrap the allocation size below; treat it
         * as zero instead */
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
    return false;
}

/* A property with a custom getter (`{ get { ... } }` or `=> expr`) lowers a
 * read `obj.Prop` to a call of the synthesized `get_Prop` method. Returns the
 * getter's SYM_METHOD symbol when one exists, NULL when the property is
 * automatic (`{ get; }`) and the read should hit the backing field slot.
 * `prop` is the SYM_PROPERTY symbol. */
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

/* Emit a call of a property's getter method on a receiver value already
 * computed. `recv` is the class/struct instance (a pointer, or a struct by
 * value which is spilled); for a static property it is ignored (the accessor
 * is a static method with no receiver). Returns the getter result. */
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
            if (!is_static &&
                LLVMGetTypeKind(LLVMTypeOf(rval)) == LLVMStructTypeKind) {
                /* a struct receiver that arrived by value has no address;
                 * spill it like the op_call path does */
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
            /* recv_type == NULL marks a `base.Prop` read: bind to the base
             * getter statically, exactly like the `base.Method()` call path
             * (a vtable dispatch would re-enter the overriding getter). */
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

/* Property setter counterpart of property_getter_sym: returns the synthesized
 * `set_Prop` SYM_METHOD when the property declares a custom setter body, NULL
 * for an automatic setter (`{ set; }`, which writes the backing slot). */
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

/* Emit `set_Prop(value)` on a receiver. The right-hand value is passed as the
 * setter's `value` argument (coerced to the declared parameter type). For a
 * static property the receiver is ignored (the setter is a static method). */
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
            emit_dispatch_call(g, is_static ? NULL : recv_type->sym, setter,
                mfn, mft, args, (int)argc, "");
            emit_release_owned_call_temp(g, obj_ast, recv, locals);
            emit_release_owned_call_temp(g, rhs_ast, value, locals);
            return;
        }
    }
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
                /* custom-getter property read through the bare name: dispatch
                 * to the getter with `this` as the receiver */
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
                    return promote_loaded(g,
                        LLVMBuildLoad2(g->builder, ft, fptr, "fval"), fty);
                }
            }
        }
        /* bare-name static field of the enclosing class: `field` -> global.
         * Works in both static and instance methods. */
        if (g->current_type_sym) {
            zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->ident.name);
            LLVMValueRef gv = get_static_field_global(g, g->current_type_sym, fsym, NULL);
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
                lval = zan_tobool(g->builder, lval, "tobool");
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
                    /* search for the operator method in the class. resolve_op
                     * _overload ranks same-named operators (op_add(V,int) vs
                     * op_add(V,long) vs op_add(V,double)) by the actual right
                     * operand; get_method_sym just took the first one, so
                     * `w + 2.5` called the `int` overload with a double. */
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
                                /* Fit the operands to the declared parameter
                                 * types before the call. Every literal and
                                 * arithmetic result is emitted as 64-bit, so an
                                 * operator taking `int` (`v + 5`) used to hand
                                 * the callee an i64 5 against an i32 parameter
                                 * -- LLVM verification failure instead of a
                                 * call. coerce_int_to narrows/widens/rounds a
                                 * value already emitted, so a complex right
                                 * operand is evaluated exactly once. A struct
                                 * receiver that arrived by value has no
                                 * address; `self` is passed by pointer, so
                                 * spill it like the op_call path does. */
                                LLVMValueRef cargs[2] = { left, right };
                                /* Only a non-static operator has a receiver
                                 * (`self`) passed by pointer. A static operator
                                 * (`static Point operator +(Point a, Point b)`)
                                 * takes both operands by value, so a struct left
                                 * operand must stay a value -- spilling it to an
                                 * address here would hand the callee a `ptr`
                                 * against its declared `Point` parameter. */
                                bool op_is_static =
                                    (op_sym->modifiers & MOD_STATIC) != 0;
                                if (!op_is_static &&
                                    LLVMGetTypeKind(LLVMTypeOf(cargs[0])) == LLVMStructTypeKind) {
                                    LLVMValueRef rslot = emit_entry_alloca(g,
                                        LLVMTypeOf(cargs[0]), "opb.recv");
                                    LLVMBuildStore(g->builder, cargs[0], rslot);
                                    cargs[0] = rslot;
                                }
                                /* Route through the instantiated specialization
                                 * when the receiver is a concrete user generic
                                 * (`Vec<int>`), exactly like the op_call path. The
                                 * specialization's signature stays erased (type
                                 * parameters lower to 64-bit), so a generic
                                 * operator passes the operand through as emitted
                                 * (i64), while a non-generic operator narrows it
                                 * to its declared parameter type. */
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
         * keeps pointer semantics. A NULL string operand compares as "" (C#),
         * so strcmp never receives a NULL pointer. */
        if ((expr->binary.op == TK_EQ_EQ || expr->binary.op == TK_BANG_EQ) &&
            both_ptr && str_operand &&
            expr->binary.left->kind != AST_NULL_LITERAL &&
            expr->binary.right->kind != AST_NULL_LITERAL) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef lc = emit_str_nonnull(g, left);
            LLVMValueRef rc = emit_str_nonnull(g, right);
            LLVMValueRef cmp_args[] = { lc, rc };
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
            LLVMValueRef lc = emit_str_nonnull(g, left);
            LLVMValueRef rc = emit_str_nonnull(g, right);
            LLVMValueRef cmp_args[] = { lc, rc };
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

/* True when `e` can carry a string at run time: a string, an untyped
 * `object`, or an expression whose type could not be inferred. Used to gate
 * the string-only member fallbacks, which would otherwise answer for any
 * rc-managed receiver (every one of them is a pointer). */
static bool recv_is_stringlike(zan_irgen_t *g, zan_ast_node_t *e,
                               local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    if (!t) return true;
    return t->kind == TYPE_STRING || t->kind == TYPE_OBJECT ||
           t->kind == TYPE_ERROR || t->kind == TYPE_TYPE_PARAM;
}

/* A shift counts modulo the width of its left operand: `1 << 33` is `1 << 1`
 * on an int and `1 << 33` on a long. Zan promotes both operands to i64 before
 * arithmetic, so the hardware masked every count mod 64 and a 32-bit shift by
 * 33 silently produced a value the truncation back to int turned into 0.
 * Mask the count against the *declared* width of the left operand instead. */
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
            right = mask_shift_count(g, expr->binary.left, right, locals);
            return zan_shl(g->builder, left, right, "shl");
        case TK_GREATER_GREATER:
            right = mask_shift_count(g, expr->binary.left, right, locals);
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

/* The slot holding the colour Console.ForegroundColor/BackgroundColor was last
 * set to. A terminal cannot be asked for its current SGR state, so the getter
 * reads back what the setter wrote; the initial value is the C# default
 * (Gray on Black), which is also what ResetColor() restores. */
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

/* Emit an OSC title sequence for Console.Title = value (works on Windows
 * Terminal / conhost VT and xterm-compatible terminals). */
static void emit_console_title(zan_irgen_t *g, LLVMValueRef s) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef pty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 1);
    LLVMValueRef pf = LLVMGetNamedFunction(g->mod, "printf");
    if (!pf) pf = LLVMAddFunction(g->mod, "printf", pty);
    LLVMValueRef fmt = zan_irgen_intern_string(g, "\033]0;%s\007");
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
            /* a custom-accessor property has no backing slot (its value lives
             * behind the getter/setter), so an initializer on it is ignored
             * like C#; only automatic properties (`{ get; set; }`) accept one */
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

/* User-defined conversion operators (A43-B12), defined after the assignment
 * emitter: find_user_conversion / emit_user_conversion. */
static zan_symbol_t *find_user_conversion(zan_irgen_t *g, zan_type_t *from_type,
                                          zan_type_t *to_type,
                                          const char *op_name);
static LLVMValueRef emit_user_conversion(zan_irgen_t *g, zan_type_t *from_type,
                                         zan_type_t *to_type, const char *op_name,
                                         LLVMValueRef val, zan_ast_node_t *val_expr,
                                         local_scope_t *locals);

/* Rank-N rectangular array element address (B8). The shape lives in the
 * allocation header (see zan_mdarray_alloc): dims[d] at arr + 8*d, data at
 * arr + 8*rank, row-major. Each index is bounds-checked against its own
 * dimension, then (i0, i1, ...) flattens to ((i0*D1 + i1)*D2 + ...). */
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
    LLVMValueRef flat = idx;
    for (int d = 1; d < rank; d++) {
        LLVMValueRef off = LLVMConstInt(i64, (unsigned long long)d, 0);
        LLVMValueRef dimv = LLVMBuildLoad2(g->builder, i64,
            LLVMBuildGEP2(g->builder, i64, dim_ptr, &off, 1, "md.dd"), "md.dim");
        LLVMValueRef ix = emit_expr(g, expr->index.extra.items[d - 1], locals);
        ix = emit_index_i64(g, ix, "md.ix");
        emit_index_bounds_check(g, ix, dimv, expr->loc, "array");
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

/* ---- compound assignment: evaluate the store target exactly once ----
 * The parser desugars `lhs op= rhs` into `lhs = (lhs op rhs)`, sharing one
 * AST subtree for both occurrences of `lhs`. Emitting that naively runs every
 * effectful node along the target spine twice -- once when folding the value,
 * once when relocating the store address -- and the two evaluations can even
 * land on different storage (`Slot[NextIndex()] += 1`). Before lowering, each
 * effectful spine node is rewritten in place into a load from a hidden temp
 * local evaluated exactly once here. Leaves that cannot diverge (identifiers,
 * literals, this) stay put; an INDEX / MEMBER_ACCESS at the root is the
 * storage location itself and only gets its children rewritten. */
static int ca_synth_counter = 0;

static void ca_hoist_spine(zan_irgen_t *g, zan_ast_node_t **slot,
                           local_scope_t *locals, int is_root);

/* Evaluate `*slot` once into a hidden temp local and replace it with an
 * identifier bound to that temp. Owned (+1) results are consumed by the temp
 * (released with the enclosing scope); borrowed values are stored raw. */
static void ca_replace_with_temp(zan_irgen_t *g, zan_ast_node_t **slot,
                                 local_scope_t *locals) {
    zan_ast_node_t *node = *slot;
    if (!node) return;
    zan_type_t *type = infer_expr_type(g, node, locals);
    /* without a type we cannot size the temp; re-emitting is then the lesser
     * risk, and the checker will have flagged the expression anyway */
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
            /* container position: a property getter or computed object must
             * not run twice, so freeze the whole reference into a temp */
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

    static LLVMValueRef emit_expr_assignment(zan_irgen_t *g, zan_ast_node_t *expr,
            local_scope_t *locals) {
            LLVMValueRef right;
            /* `(a, b) = rhs` (assigning into an existing tuple slot) has no
             * lowering: a tuple literal is a fresh synthesized struct, so the
             * write would go to a discarded temp. Deconstruction declares the
             * names instead — point the user there rather than silently
             * dropping the assignment. */
            if (expr->binary.left && expr->binary.left->kind == AST_TUPLE_EXPR) {
                zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                    "tuple assignment is not supported; use `var (a, b) = rhs` "
                    "to declare the names");
                return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
            }
            check_readonly_store(g, expr->binary.left, locals);
            /* `lhs op= rhs`: the parser shared one target subtree for the
             * value read and the store address. Freeze every effectful spine
             * node into a temp so both occurrences evaluate exactly once and
             * agree on the location (see ca_hoist_spine). */
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
            /* user-defined implicit conversion: `d = c;` where the source
             * type declares `implicit operator double` (B12). */
            if (lt && !type_is_binding(lt)) {
                zan_type_t *rty = infer_expr_type(g, expr->binary.right, locals);
                if (find_user_conversion(g, rty, lt, "op_implicit")) {
                    right = emit_user_conversion(g, rty, lt, "op_implicit",
                                                 right, expr->binary.right, locals);
                    if (is_rc_managed_type(lt)) {
                        /* the conversion result is owned (+1) like a method-call
                         * result; mirror the binding path and swap in a dummy
                         * `new` node so every store path below moves the
                         * reference instead of retaining a second one */
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
                /* custom-setter static property written bare-name: dispatch to
                 * the static set_Prop(value) (no receiver) */
                zan_symbol_t *setter = property_setter_sym(g, fs);
                if (setter) {
                    emit_property_setter_call(g, setter, g->current_type_sym->type,
                        NULL, right, expr->binary.left,
                        expr->binary.right, locals);
                } else {
                LLVMValueRef gv = get_static_field_global(g, g->current_type_sym, fs, NULL);
                if (fs->type && is_rc_managed_type(fs->type)) {
                    emit_rc_store_field(g, fs->type, gv, right, expr->binary.right, locals,
                                        (fs->modifiers & MOD_WEAK) ? 1 : 0);
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
                    /* custom-setter property written bare-name: dispatch to
                     * this.set_Prop(value) instead of the backing slot */
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
                            LLVMValueRef len = emit_string_buffer_len(g, arr_ptr, expr->loc);
                            emit_index_bounds_check(g, idx, len, expr->loc, "string");
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
                            /* rank-N rectangular slot: per-dim bounds checks
                             * and row-major flattening inside the helper */
                            elem_ptr = emit_mdarray_elem_ptr(g, arr_ptr,
                                expr->binary.left, elem_llvm, locals);
                        } else {
                            LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                            if (local->type && local->type->kind == TYPE_ARRAY) {
                                emit_index_bounds_check(g, idx, zan_array_len(g, arr_ptr),
                                    expr->loc, "array");
                            }
                            LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                                LLVMPointerType(elem_llvm, 0), "arrp");
                            elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
                        }
                        zan_type_t *et = local->type ? local->type->element_type : NULL;
                        LLVMValueRef stored = right;
                        /* `int?[]` element slot: wrap a raw payload value into
                         * the nullable struct before storing (coerce_int_to
                         * also fits int widths and handles the null literal). */
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
                        /* A bare-name field is not a local, so a string field
                         * may be a raw byte buffer; only literal/local string
                         * receivers carry a reliable NUL bound. */
                        if (fsym->type && fsym->type->kind == TYPE_STRING &&
                            expr_has_reliable_string_bounds(arr_expr, locals)) {
                            LLVMValueRef len = emit_string_buffer_len(g, arr_ptr, expr->loc);
                            emit_index_bounds_check(g, idx, len, expr->loc, "string");
                        } else if (fsym->type && fsym->type->kind == TYPE_STRING) {
                            /* No reliable bound: keep null/negative bare faults out. */
                            idx = emit_string_elem_guard(g, arr_ptr, idx, expr->loc, &arr_ptr);
                        } else if (fsym->type && fsym->type->kind == TYPE_ARRAY &&
                                   fsym->type->array_rank <= 1) {
                            emit_index_bounds_check(g, idx, zan_array_len(g, arr_ptr),
                                expr->loc, "array");
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
                            idx = slot_word_index(g, idx,
                                elem_slot_words(g, container_elem_type(fsym->type)));
                            LLVMValueRef slot_ptr = LLVMBuildGEP2(g->builder, i64t, data, &idx, 1, "ep");
                            emit_collection_slot_store(g, container_elem_type(fsym->type), i64t, slot_ptr,
                                right, expr->binary.right, locals, 1);
                        } else if (fsym->type && type_named(fsym->type, "Dict", 4)) {
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
                        /* `int?[]` element slot: wrap a raw payload value into
                         * the nullable struct before storing. */
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
                                    /* fit the value to the element width in
                                     * both directions: an i64 into a byte
                                     * slot used to write over its neighbours */
                                    stored = coerce_int_to(g, stored, elem_llvm);
                                }
                            } else if (et && et->kind == TYPE_NULLABLE) {
                                /* `int?[]` element slot in a local/field array:
                                 * wrap a raw payload value into the nullable
                                 * struct (coerce_int_to also fits int widths). */
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
                        /* List field: index into the data buffer, not the
                         * struct — otherwise the store clobbers count/cap/data. */
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
                        /* A string field may hold a raw FFI buffer, whose
                         * first NUL says nothing about its capacity; only a
                         * literal/local receiver carries a reliable bound. */
                        if (expr_has_reliable_string_bounds(arr_expr, locals))
                            emit_index_bounds_check(g, idx,
                                emit_string_buffer_len(g, arr_ptr, expr->loc), expr->loc,
                                "string");
                        else
                            idx = emit_string_elem_guard(g, arr_ptr, idx, expr->loc, &arr_ptr);
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i8, arr_ptr, &idx, 1, "eidx");
                        LLVMValueRef val8 = LLVMBuildTrunc(g->builder, right, i8, "byte");
                        zan_store_fit(g, val8, elem_ptr);
                        emit_string_len_invalidate(g, arr_ptr);
                        } else {
                        if (at->kind == TYPE_ARRAY && at->array_rank == 1) {
                            /* rank>1 goes through emit_mdarray_elem_ptr, which
                             * checks each dimension itself */
                            emit_index_bounds_check(g, idx,
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
                        /* `int?[]` element slot: wrap a raw payload value into
                         * the nullable struct before storing. */
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
                    LLVMValueRef elem_ptr;
                    if (at->array_rank > 1) {
                        elem_ptr = emit_mdarray_elem_ptr(g, arr_ptr,
                            expr->binary.left, elem_llvm, locals);
                    } else {
                        LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                        emit_index_bounds_check(g, idx, zan_array_len(g, arr_ptr),
                            expr->loc, "array");
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
                            /* `int?[]` element slot: wrap a raw payload value
                             * into the nullable struct (coerce_int_to also
                             * fits int widths and handles the null literal). */
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
                    /* custom-setter static property: dispatch to the static
                     * set_Prop(value) (no receiver) instead of the global slot */
                    zan_symbol_t *setter = property_setter_sym(g, fs);
                    if (setter) {
                        emit_property_setter_call(g, setter, cs->type, NULL,
                            right, obj_expr, expr->binary.right, locals);
                        stored = true;
                    } else {
                    LLVMValueRef gv = get_static_field_global(g, cs, fs,
                        static_access_inst(g, obj_expr));
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
            }
            if (!stored && obj_expr->kind == AST_IDENTIFIER) {
                local_var_t *local = local_find(locals, obj_expr->ident.name);
                if (local && local->type && local->type->sym) {
                    int fi = get_field_index(local->type->sym, expr->binary.left->member.name);
                    if (fi >= 0) {
                        LLVMTypeRef st = get_struct_llvm_type(g, local->type->sym);
                        /* custom-setter property on a local receiver: dispatch
                         * to set_Prop(value) instead of writing the slot */
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
                        /* property with a custom setter: dispatch the write to
                         * set_Prop(value) instead of the backing slot */
                        zan_symbol_t *psym = get_field_sym(cls, expr->binary.left->member.name);
                        zan_symbol_t *setter = property_setter_sym(g, psym);
                        if (setter) {
                            zan_type_t *rct = infer_expr_type(g, obj_expr, locals);
                            LLVMValueRef rval = emit_guarded_member_object(
                                g, expr->binary.left, locals);
                            emit_property_setter_call(g, setter, rct, rval,
                                right, obj_expr, expr->binary.right, locals);
                            stored = true;
                        } else {
                            LLVMTypeRef st = get_struct_llvm_type(g, cls);
                            LLVMValueRef obj_val = emit_guarded_member_object(
                                g, expr->binary.left, locals);
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
                    if (!type_declares_member(acls, an)) {
                        zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                            "'%.*s' has no member '%.*s'",
                            (int)acls->name.len, acls->name.str,
                            (int)an.len, an.str);
                    }
                }
                /* `name.field = v` where `name` is not a local, not a member
                 * of the enclosing type and not a name the binder knows: no
                 * store was emitted and, unlike the same name in a value
                 * position, nothing was reported. A static factory converted
                 * into a constructor left `app.isRunning = true` behind and
                 * the window flag was never set, so every GUI program built
                 * from the library exited before its first frame. */
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
    /* set when the slot is a byte of a string, whose cached length the store
     * at the end of this function invalidates */
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
            /* bare-name static field of the enclosing class */
            zan_symbol_t *fs = get_field_sym(g->current_type_sym, operand->ident.name);
            zan_symbol_t *getter = property_getter_sym(g, fs);
            if (getter) {
                prop_getter = getter;
                prop_setter = property_setter_sym(g, fs);
                prop_recv_type = g->current_type_sym->type;
                prop_recv = NULL;
            } else {
            LLVMValueRef gv = fs ? get_static_field_global(g, g->current_type_sym, fs, NULL)
                                 : NULL;
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
                LLVMValueRef str_len = emit_string_buffer_len(g, arr_ptr, operand->loc);
                emit_index_bounds_check(g, idx, str_len, operand->loc, "string");
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
        /* property operand: read via getter, mutate, write via setter.
         * The value type comes from the getter's return type so the +/-1
         * arithmetic uses the property's real width. */
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

/* Translate a C# interpolation format specifier (the text after `:` in
 * {v:D4}) into a printf format string. Returns the number of chars written
 * to out (excluding NUL), or 0 when the spec is empty/absent/unrecognized
 * (caller uses its default format). `is_float` says the operand is a real;
 * `*want_float` is set when the spec formats as fixed/scientific (`F2`,
 * `E2`, `0.00`), which for an integer operand means the value must first be
 * widened to double. Only the common numeric specs are lowered -- D (decimal
 * pad), X/x (hex), F/f (fixed), E/e (scientific), G/g (general) and the
 * custom `0`/`#` patterns; everything else (N, C, P, ...) falls back to the
 * default format (N's thousands separators are not expressible with
 * snprintf). */
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
        if (s[i] >= '0' && s[i] <= '9') digits = digits * 10 + (s[i] - '0');
    }
    switch (code) {
    case 'D': case 'd': /* decimal, zero-padded to `digits` */
        if (is_float) break;
        if (digits <= 0) return 0;
        if (is_ulong)
            return snprintf(out, outcap, "%%0%dllu", digits);
        return snprintf(out, outcap, "%%0%dlld", digits);
    case 'X': case 'x': /* hex (upper/lower), zero-padded to `digits` */
        if (is_float) break;
        if (digits <= 0) return 0;
        return snprintf(out, outcap, "%%0%dllX", digits);
    case 'F': case 'f': /* fixed-point with `digits` decimals */
        if (digits < 0) digits = 0;
        if (want_float) *want_float = true;
        return snprintf(out, outcap, "%%.%dlf", digits);
    case 'E': case 'e': /* scientific */
        if (digits < 0) digits = 0;
        if (want_float) *want_float = true;
        return snprintf(out, outcap, "%%.%dE", digits);
    case 'G': case 'g': /* general: default is %g; a digit count is
                         * significant digits, so {v:G3} -> %.3g */
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
        /* String interpolation: convert each part to i8*, then concatenate.
         * Parts alternate: string_literal, expr, string_literal, expr, ... string_literal
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
                    /* already a string — a null one concatenates as "" (C#),
                     * so coerce before the length is taken */
                    strs[i] = emit_str_nonnull(g, val);
                    lens[i] = emit_string_length(g, strs[i], expr->loc);
                    owns[i] = expr_yields_owned_rc_value(g, part, locals) ? 1 : 0;
                } else if (vtk == LLVMDoubleTypeKind || vtk == LLVMFloatTypeKind) {
                    /* snprintf(NULL, 0, fmt, val) to get length, then snprintf
                     * into buffer. A {v:F2} format spec selects the printf
                     * pattern; absent, the default %g is used. A float operand
                     * is widened to double so %.Nlf sees a matching arg. */
                    char fbuf[32];
                    bool want_f = false;
                    int flen = interp_format_to_printf(&spec, true, false,
                        &want_f, fbuf, sizeof(fbuf));
                    LLVMValueRef fval = (vtk == LLVMFloatTypeKind)
                        ? LLVMBuildFPExt(g->builder, val,
                            LLVMDoubleTypeInContext(g->ctx), "f2d")
                        : val;
                    LLVMValueRef fmt = flen > 0
                        ? zan_irgen_intern_string(g, fbuf)
                        : zan_irgen_intern_string(g, "%g");
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
                } else if (vtk == LLVMIntegerTypeKind &&
                           LLVMGetIntTypeWidth(vt) == 1) {
                    /* bool interpolates as true/false, like `b.ToString()`
                     * and C#. Falling through to the integer path printed the
                     * i1 as a number instead (1, or -1 back when it was
                     * sign-extended). A format spec has no meaning here. */
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
                    /* char interpolates as the character (C#), not its code. */
                    LLVMValueRef buf = emit_char_to_cstr(g, val);
                    strs[i] = buf;
                    lens[i] = zan_call2(g->builder, strlen_type, g->fn_strlen, &buf, 1, "clen");
                    owns[i] = 1;
                } else {
                    /* integer types — format with %lld (%llu for ulong), or a
                     * {v:D4}/{v:X2} spec lowered by interp_format_to_printf.
                     * A fixed/scientific spec (F2, E2, 0.00) on an integer
                     * widens it to double first, like C#. */
                    /* zan_iwiden, not a bare SExt: `byte` is unsigned in this
                     * lowering, so 200 must widen to 200 not -56 */
                    LLVMValueRef val64 =
                        (LLVMGetIntTypeWidth(vt) < 64)
                            ? zan_iwiden(g->builder, val, i64) : val;
                    bool iu = expr_is_ulong(g, part, locals);
                    char fbuf[32];
                    bool want_f = false;
                    int flen = interp_format_to_printf(&spec, false, iu,
                        &want_f, fbuf, sizeof(fbuf));
                    if (want_f) {
                        /* widen the integer to double and fall into the float
                         * path so the %.Nlf spec sees a matching arg */
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
                        /* plain {v}: the digits fit in 21 bytes, so format them
                         * straight into the result instead of paying for two
                         * snprintf calls (one just to measure) */
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
        /* Console.ForegroundColor / Console.BackgroundColor read back the
         * colour the setter last wrote (the store side is in
         * emit_expr_assignment); without this they lowered to a zeroed slot. */
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
        /* `ti.Name` / `ti.Kind` / `ti.FieldCount` on a TypeInfo, read straight
         * off the reflection record (irgen_reflect.c). */
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
        /* Task properties: `t.Result` (Task<T> only) and `t.IsCompleted` read
         * the coroutine a Task value names — the property twin of the
         * Wait()/Result()/IsCompleted() call handling in emit_expr_call. */
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
        /* Guard: a member access whose receiver is a builtin scalar type is
         * only valid for the compiler-lowered string.Length property. Any
         * other name was a typo that the checker rejects; this second line of
         * defence emits an error instead of silently lowering to the constant
         * 0 (which crashed when the zero was later used as a pointer —
         * `tabs[i].path.path`). Primitive-type constants (`int.MaxValue`,
         * `double.NaN`, ...) are a deliberate exception -- they are lowered
         * further down, so the guard must not reject them. */
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

        /* Primitive-type constants: `int.MaxValue`, `double.NaN`, ... These
         * are compiler-lowered like Math.PI above -- the builtin scalar types
         * have no class symbol to hang a static field off, so member access on
         * them was falling through to the generic path and silently lowering
         * to 0 (the guard above even rejects other member names on scalars).
         * Widths follow map_type: int/uint are 32-bit, long/ulong 64-bit. */
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
                    if (m.len == 3 && memcmp(m.str, "NaN", 3) == 0) {
                        unsigned long long nan_bits = 0x7FF8000000000000ULL;
                        return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx),
                            *(double *)&nan_bits);
                    }
                    if (m.len == 16 && memcmp(m.str, "PositiveInfinity", 16) == 0) {
                        unsigned long long pinf_bits = 0x7FF0000000000000ULL;
                        return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx),
                            *(double *)&pinf_bits);
                    }
                    if (m.len == 16 && memcmp(m.str, "NegativeInfinity", 16) == 0) {
                        unsigned long long ninf_bits = 0xFFF0000000000000ULL;
                        return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx),
                            *(double *)&ninf_bits);
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

        /* .Count property on List — read count field from list struct
         * (works for local vars and fields). */
        if (expr->member.name.len == 5 && memcmp(expr->member.name.str, "Count", 5) == 0) {
            zan_type_t *lt = infer_expr_type(g, expr->member.object, locals);
            if (lt && type_named(lt, "List", 4)) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef raw_ptr = emit_guarded_member_object(g, expr, locals);
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

        /* Dict.Keys / Dict.Values — copy the entries into a fresh owned List
         * of the key/value type (retaining rc-managed elements: the dict still
         * owns its slots). */
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

        /* array .Length: read the element count from the array header. A null
         * array value would fault on the obj-16 GEP before any bounds check
         * sees it, so the receiver gets the same null guard as members. */
        /* array .Count is the same header read, aliased to .Length: the
         * params bundle is a plain array, so a callee that reads
         * `kindIds.Count` (List spelling) used to fall through to the
         * constant-0 fallback and report an empty bundle. */
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

        /* String.Length property — the cached header length, falling back to a
         * strlen walk. Only for a receiver that really is a string: every
         * rc-managed value is a pointer, so an unguarded fallback answered
         * `list.Length` with the strlen of the List's own storage instead of
         * rejecting the member. */
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
                /* custom-getter static property: dispatch to the static
                 * get_Prop() (no receiver) instead of the global slot */
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
                        /* custom-getter property on a local receiver: dispatch
                         * to the getter instead of reading the slot */
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
                            /* load struct pointer or value, then GEP to field.
                             * A null class local would fault on the GEP, so
                             * the receiver gets the guard + scratch select. */
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
                            /* A `T` field of Box<Vec> holds a Vec, not a
                             * pointer to one: read the slot at the concrete
                             * type so a value type comes back by value. */
                            zan_type_t *fty = fsym
                                ? subst_type_param(fsym->type, local->type)
                                : NULL;
                            LLVMTypeRef field_type = fty ? map_type(g, fty)
                                : LLVMInt64TypeInContext(g->ctx);
                            LLVMValueRef fv = promote_loaded(g,
                                LLVMBuildLoad2(g->builder, field_type, field_ptr, "fval"),
                                fty);
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
                    /* A property with a custom getter is computed, not read out
                     * of a backing slot: dispatch to the synthesized getter. */
                    zan_symbol_t *psym = get_field_sym(cls, expr->member.name);
                    zan_symbol_t *getter = property_getter_sym(g, psym);
                    if (getter) {
                        zan_type_t *rct = infer_expr_type(g, expr->member.object, locals);
                        LLVMValueRef rval = emit_guarded_member_object(g, expr, locals);
                        /* `base.Prop` must call the base getter directly: the
                         * normal vtable dispatch re-enters the override that
                         * is executing (slot still points at B_get_Name). */
                        if (expr->member.object->kind == AST_BASE_EXPR)
                            return emit_property_getter_call(g, getter, NULL,
                                rval, expr->member.object, locals);
                        return emit_property_getter_call(g, getter, rct, rval,
                                                         expr->member.object,
                                                         locals);
                    }
                    LLVMTypeRef st = get_struct_llvm_type(g, cls);
                    LLVMValueRef obj_val = emit_guarded_member_object(g, expr, locals);
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
                        zan_type_t *rct = infer_expr_type(g, expr->member.object, locals);
                        zan_type_t *fty = fsym
                            ? subst_type_param(fsym->type, rct) : NULL;
                        LLVMTypeRef ft = fty ? map_type(g, fty)
                                             : LLVMInt64TypeInContext(g->ctx);
                        LLVMValueRef gfv = promote_loaded(g,
                            LLVMBuildLoad2(g->builder, ft, field_ptr, "gfval"),
                            fty);
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
                    LLVMValueRef recv = emit_guarded_member_object(g, expr, locals);
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
        /* Robustness: reading `obj.name` where obj's class (or its base chain)
         * declares no such member fell through to the constant 0 below -- a
         * renamed or removed field then read as a wrong number, or crashed the
         * program when the zero was used as a pointer. The assignment form is
         * already diagnosed; the read is diagnosed the same way. */
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
                /* Same for a builtin collection: its members are all handled
                 * above, so anything reaching here is a member it does not
                 * have (`list.Length`) and used to read as the 0 below. */
                zan_type_t *rt = infer_expr_type(g, expr->member.object, locals);
                if (rt && is_builtin_collection_type(rt))
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "'%.*s' has no member '%.*s'",
                        (int)rt->name.len, rt->name.str,
                        (int)expr->member.name.len, expr->member.name.str);
                /* And when the receiver is itself a call the class does not
                 * declare (`t.GetType().Name`): the chain returns the 0 below
                 * without ever emitting the inner call, so the unresolved
                 * method went undiagnosed -- only the unchained form
                 * (`var g = t.GetType();`) reported it. */
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
            /* obj.field[i] — array stored in a struct/class field. Load the
             * field value (the array data pointer) and recover its element
             * type from the field declaration. */
            arr_type = member_access_field_type(g, locals, expr->index.object);
            if (arr_type) {
                arr_ptr = emit_expr(g, expr->index.object, locals);
                if (type_named(arr_type, "List", 4)) {
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
            LLVMValueRef dout = LLVMBuildLoad2(g->builder, value_llvm, res, "dval");
            return finish_index_of_temp(g, expr, locals, arr_type, dvt,
                                        arr_ptr, dout);
        }
        /* string[i] — load a single byte (i8) and zero-extend to i32 so that
         * indexing a string yields the character code, enabling char-level
         * lexing of real source text. */
        if (arr_ptr && arr_type && arr_type->kind == TYPE_STRING) {
            LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
            if (expr_has_reliable_string_bounds(expr->index.object, locals)) {
                LLVMValueRef str_len = emit_string_buffer_len(g, arr_ptr, expr->loc);
                emit_index_bounds_check(g, idx, str_len, expr->loc, "string");
            } else {
                /* No reliable bound (field/param/extern receiver): still keep
                 * null and negative indexes from faulting bare. */
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
                /* rank-N rectangular read: per-dim bounds checks and
                 * row-major flattening inside the helper */
                elem_ptr = emit_mdarray_elem_ptr(g, arr_ptr, expr,
                                                 elem_llvm, locals);
            } else {
                LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
                LLVMValueRef arr_len = zan_array_len(g, arr_ptr);
                emit_index_bounds_check(g, idx, arr_len, expr->loc, "array");
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

/* ==================== B17: full query-expression lowering ====================
 *
 * `from x in src [clauses] [group e by k [into g]] [select p]` is lowered
 * eagerly over materialized Lists. The sub-clauses are pure per-element
 * functions of the range var, so:
 *  - where/let are evaluated in the final iteration pass, in source order
 *    (a `let` is registered when reached, so only later clauses see it);
 *  - orderby sorts the CURRENT sequence before the pass: the keys are
 *    extracted in a pre-pass loop (range var, row fields and `let`s are
 *    re-registered there) into a List<K>, and the sequence is replaced by
 *    Enumerable.OrderByKeys{Int,Long,Num,Str}[Descending](seq, keys). A
 *    stable sort by a pure key is equivalent to filtering first, so hoisting
 *    the sort above the wheres is safe; a multi-key clause sorts least
 *    significant key first (C# OrderBy(k1, k2) semantics), and repeated
 *    orderby clauses apply in source order.
 *  - join materializes the sequence into List<(x, y, ...)> rows — an
 *    anonymous tuple struct carrying the range var and every join variable —
 *    so the join variable survives for later clauses and sorts. Without a
 *    later orderby/group a join stays a nested loop in the final pass
 *    instead, which avoids the row materialization entirely.
 *  - group extracts parallel (key, element) lists and calls
 *    Enumerable.GroupByKeys{Int,Str}, yielding List<Grouping<e>>; with
 *    `into g` the final pass then iterates the groupings with only g in
 *    scope (C# drops the range variable after a group clause).
 *
 * All intermediate lists are held in hidden locals and released explicitly;
 * `locals` marks keep the synthetic names out of the caller's scope. */

typedef struct {
    zan_istr_t seq_name;    /* hidden local holding the current sequence */
    LLVMValueRef seq_value; /* its value (kept in sync with the alloca) */
    /* Whether seq_value is a list this lowering allocated. It starts false --
     * the sequence *is* the source collection, borrowed from whatever
     * expression produced it -- and turns true the first time a sort or a join
     * row-materialization replaces it. Every replacement releases the previous
     * sequence, and releasing a borrowed source there dropped a reference the
     * caller still held: the first `orderby` over a local list freed that list
     * and the next allocation corrupted the heap. */
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

/* register a hidden local holding `value`, typed `value_type`; returns the
 * registered name (the alloca is the source of truth for the value) */
/* A struct value is represented by a pointer to its storage (that is what
 * emit_expr yields for a tuple/struct), so for a struct-typed hold that pointer
 * already *is* the slot. Wrapping it in a second slot left the local's declared
 * type (the struct) disagreeing with what its storage held (the address), and
 * consumers then read the struct out of the slot containing the address: a join
 * row came out holding the stack address of the row builder instead of the range
 * variable, so the join key never matched and `join` + `orderby`/`group`
 * produced nothing. */
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

/* register a named local (let vars, join vars, the range var) holding a
 * freshly emitted value */
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
    LLVMTypeKind ek = LLVMGetTypeKind(elm);
    LLVMValueRef ev = (ek == LLVMStructTypeKind)
        ? load_struct_from_slot(g, ep, elm)
        : LLVMBuildLoad2(g->builder, i64, ep, "qlel");
    if (ek == LLVMPointerTypeKind)
        ev = LLVMBuildIntToPtr(g->builder, ev, elm, "qelp");
    else if (ek == LLVMDoubleTypeKind)
        ev = LLVMBuildBitCast(g->builder, ev, elm, "qelf");
    else if (ek == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(elm) < 64)
        ev = LLVMBuildTrunc(g->builder, ev, elm, "qelt");
    LLVMValueRef slot = emit_entry_alloca(g, elm, "qels");
    zan_store_fit(g, ev, slot);
    return slot;
}

/* close a loop; the current block must be the body end */
static void query_loop_close(zan_irgen_t *g, query_loop_t *l) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    /* Branch the body's tail block to the increment -- unless the caller
     * already terminated it. A join wires its no-match block straight to
     * `inc` (`br inc` in qj.skip / qm.skip), and appending a second terminator
     * there is invalid IR: that was the "Terminator found in the middle of a
     * basic block" failure, repeated at all four join emission sites. */
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

/* register the iteration variable(s): the range var (raw mode) or every row
 * field by name (row mode); `slot` holds the loaded element/row struct */
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

/* key type classification: 0=int, 1=long, 2=double, 3=string, -1=unsupported.
 * Group keys are restricted to int/string (Grouping carries IntKey/Key). */
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

/* one orderby key: extract List<K> of keys over the current sequence and
 * replace the sequence with the sorted one */
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

/* one join clause: expand the current sequence into rows carrying the join
 * variable (or its match list for `join ... into g`) */
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
        /* The hidden sequence local changes *type* here too: it now holds rows,
         * not source elements. That recorded type is what a later generic call
         * infers its type argument from, so leaving it at List<P> monomorphized
         * `Enumerable.OrderByKeysStr<P>` and walked 2-word rows with a 1-word
         * stride -- every `join` followed by an `orderby`/`group` came out empty,
         * and faulted outright once rows really were produced. */
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

    /* Left key, once per outer row -- and it has to be emitted *here*: the
     * row's fields (and any `let`) only come into scope inside this loop, via
     * query_register_iter above. It used to be emitted before the loop, where
     * the range variable was out of scope, so `on p.dept equals d.id` read the
     * undeclared `p` and fell back to constant 0: the key never matched, the
     * row list came out empty, and every `join` followed by an `orderby` or a
     * `group` silently produced zero results. (query_do_group has the right
     * shape: open the loop, register the variables, then emit the key.) */
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
    /* same as the row1 step: the sequence local's recorded type follows the
     * rows, or the generic sort/group instantiates on the wrong element type */
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

/* terminal group: extract (key, element) lists and call GroupByKeys*;
 * returns the List<Grouping<e>> */
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

/* final iteration pass: where/let in source order, nested loops for deferred
 * joins, then the projection. The current loop's iteration variable(s) are
 * already registered; join clauses at or after `from_idx` that were
 * materialized (join_in_rows) are skipped. */
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
            /* `into` does not test the key out here: it gives *every* outer
             * element a group (an empty one when nothing matches, as in C#),
             * so it owns its own loop and match test below. Testing first also
             * left the `qj.skip` block of that test with no terminator at all,
             * which is what "Basic Block does not have terminator" was. */
            if (cl->query_clause.into.len > 0) {
                /* join ... into g: collect the matches, then continue in the
                 * outer scope with g bound to the match list */
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
            /* plain join: one nested loop over the join source, and the rest
             * of the clauses run inside each match. */
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
        /* any pending joins must be rows before the sort (their variables
         * may appear in the key) */
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
        /* an orderby clause is one or more adjacent keys; sort least
         * significant first (C# OrderBy(k1, k2)) */
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

/* (a, b, ...) builds a value of the synthesized anonymous tuple struct: each
 * element is evaluated and stored into its Item1..ItemN field of a fresh
 * stack slot, and the slot pointer is returned (the same shape
 * `new SomeStruct(...)` produces, so var decls, returns and assignments keep
 * working). */
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
        zan_store_fit(g, fval, fptr);
    }
    return alloca;
}

static LLVMValueRef emit_expr_new_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* `FactoryCall(...) { Members = {...} }`: the braces continue a call
         * (e.g. a GUI static factory), so lower the call first, then apply
         * the member-writes to the returned object and hand it back. The
         * whole thing is a call result: owned (+1) for rc-managed returns,
         * which the consumers' expr_yields_owned_rc_value already reports. */
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
                        /* an Add that returns the element (the GUI
                         * ControlChildren facade) hands the caller +1; the
                         * initializer discards that value, so release it --
                         * same rule as a discarded expression statement */
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
                /* plain field/property write: go through a synthetic
                 * `obj.Member = value` assignment so setters, ARC rules and
                 * bindings come from the ordinary assignment lowering. */
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
        /* new List<T>() — built-in dynamic list. `new List<string>[3]` is an
         * array of lists, not a list: the type node carries the collection's
         * name at every array level, so the array paths below own it. */
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF &&
            !expr->new_expr.is_array) {
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
                /* `new List<T>(src)` — copy constructor (checker flagged
                 * list_copy): a fresh list carrying src's count and a cloned
                 * data buffer. Each slot goes through
                 * emit_collection_slot_store so RC occupants are retained
                 * exactly like Add would retain them; the old slot is empty
                 * (calloc), so overwrite_old stays 0. */
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

                    if (lwords != 1) {
                        /* Wide value-struct slots have no populated form to
                         * copy (Add rejects them too); say so instead of
                         * word-copying possible RC fields by hand. */
                        zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                            "List copy constructor is not supported yet for a "
                            "list whose element is a value struct wider than "
                            "8 bytes");
                    } else {
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
                            body_bb, end_bb);                        LLVMPositionBuilderAtEnd(g->builder, body_bb);
                        iv = LLVMBuildLoad2(g->builder, i64, ip, "cpy.iv2");
                        LLVMValueRef off = LLVMBuildMul(g->builder, iv,
                            LLVMConstInt(i64, lwords, 0), "cpy.off");
                        LLVMValueRef s_slot = LLVMBuildGEP2(g->builder, i64,
                            src_data, &off, 1, "cpy.ss");
                        LLVMValueRef d_slot = LLVMBuildGEP2(g->builder, i64,
                            dst_data, &off, 1, "cpy.ds");
                        LLVMTypeRef elem_llvm = lelem ? map_type(g, lelem) : i64;
                        LLVMValueRef v = LLVMBuildLoad2(g->builder, i64, s_slot,
                                                        "cpy.sv");
                        if (LLVMGetTypeKind(elem_llvm) == LLVMStructTypeKind) {
                            v = load_struct_from_slot(g, s_slot, elem_llvm);
                        } else if (LLVMGetTypeKind(elem_llvm) == LLVMPointerTypeKind) {
                            v = LLVMBuildIntToPtr(g->builder, v, elem_llvm, "cpy.pv");
                        } else if (LLVMGetTypeKind(elem_llvm) == LLVMDoubleTypeKind) {
                            v = LLVMBuildBitCast(g->builder, v, elem_llvm, "cpy.dv");
                        }
                        /* rhs NULL: the loaded element is borrowed from src,
                         * so the store must retain it (owned-check yields 0). */
                        emit_collection_slot_store(g, lelem, i64, d_slot, v,
                                                   NULL, locals, 0);
                        LLVMValueRef nxt = LLVMBuildAdd(g->builder, iv,
                            LLVMConstInt(i64, 1, 0), "cpy.nx");
                        LLVMBuildStore(g->builder, nxt, ip);
                        LLVMBuildBr(g->builder, cond_bb);
                        LLVMPositionBuilderAtEnd(g->builder, end_bb);
                    }
                    return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr, "listv");
                }
                /* collection initializer items: new List<T>{ a, b, c }. The
                 * parser stores them in new_expr.args (List has no ctor args);
                 * the postfix spelling (`List<int> { ... }`) keeps them in
                 * arg_inits instead, so both shapes feed the same build. */
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
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF &&
            !expr->new_expr.is_array) {
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
                /* dictionary initializer entries: new Dict<K,V>{ {k, v}, ... }.
                 * The parser flattens each `{ k, v }` pair into new_expr.args
                 * (the postfix `Dict<K,V> { ... }` spelling keeps them in
                 * arg_inits), so consume them pairwise via the shared upsert
                 * helper. */
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

        /* new Type[d1, d2, ...] — array creation. Rank-N shapes allocate the
         * extended header (zan_mdarray_alloc); a trailing element list
         * (`new int[3] { 1, 2, 3 }`, `new int[2,2] { {..}, {..} }`) fills the
         * freshly allocated buffer row-major. */
        if (expr->new_expr.is_array && expr->new_expr.args.count > 0) {
            zan_type_t *alloc_type = resolve_type_ctx(g, expr->new_expr.type);
            if (!alloc_type) alloc_type = g->binder->type_int;
            int rank = expr->new_expr.array_rank > 0
                ? expr->new_expr.array_rank : 1;
            /* element type: one level below the sized level, so
             * `new int[3][]` allocates a buffer of int[] row pointers */
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
                dims[d] = dv;
            }
            LLVMValueRef arr;
            if (rank <= 1) {
                LLVMValueRef total = zan_mul(g->builder, dims[0],
                    LLVMSizeOf(elem_llvm), "total");
                arr = zan_array_alloc(g, total, dims[0]);
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
                    LLVMValueRef idx = LLVMConstInt(i64t, (unsigned long long)k, 0);
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, elem_llvm,
                        typed, &idx, 1, "aep");
                    /* fit the value to the element width: storing an i64 into
                     * a byte slot would write over the elements after it */
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
                        LLVMValueRef site_val;
                        {
                            /* Assign a stable allocation-site index (always: it
                             * also keys dynamic release dispatch to this site's
                             * concrete class). The "file:line:col" descriptor is
                             * only needed for --check-leaks reporting. */
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
                        /* named constructor args (`new T(b: 2, x: 1)`) are
                         * reordered to the chosen ctor's parameter order now
                         * that its symbol is known. find_ctor matched on arity
                         * alone, so a permutation of names still resolved. */
                        reorder_named_args_impl(g, &ctor_arg_list,
                                                expr->loc, ctor->decl);
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
                        /* Ordinary initializers trail constructor arguments in
                         * args; the generic-postfix form stores the same tail
                         * in arg_inits. Present both shapes as one list so the
                         * lowering and ARC rules stay identical. */
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
                            /* `Members = { a, b }`: lower to a synthetic
                             * `<member>.Add(item)` AST per element and run the
                             * ordinary call lowering, so every collection kind
                             * (builtin List intrinsics, user Add methods,
                             * Dict pairs) and every ARC rule comes from the one
                             * code path that already owns them. The member is
                             * read, never stored, so a getter-only property is
                             * as valid as a field. */
                            if (arg->kind == AST_COLL_INIT) {
                                zan_istr_t cname = arg->coll_init.name;
                                zan_symbol_t *msym = get_field_sym(sym, cname);
                                if (!msym || !msym->type) continue;
                                int getter_owned = 0;
                                zan_ast_node_t *recv = zan_ast_new(g->arena,
                                    AST_IDENTIFIER, arg->loc);
                                recv->ident.name = cname;
                                /* the receiver reads off `this`-of-the-new-
                                 * object, which emit_expr_identifier cannot
                                 * reach (the object is not registered in the
                                 * local scope); splice the alloca in through a
                                 * member read of the AST the lowering walks --
                                 * i.e. build `member.Add(item)` calls against a
                                 * temp local registered for this scope. */
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
                                    /* the getter handed us +1 on the
                                     * collection; the temp slot took that
                                     * reference and is dropped without a
                                     * scope-exit release, so hand it back
                                     * once the Adds are done */
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
                                /* a class-typed snapshot is a borrowed load:
                                 * the object itself owns the collection, so the
                                 * temp must not release it at scope exit --
                                 * keep it out of arc_own tracking by leaving
                                 * arc_owned unset (local_add default). */
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
                                    /* Dict members take (key, value): the
                                     * parser flattened `{ k, v }` braces into
                                     * consecutive items, so pair them here. */
                                    if (msym->type &&
                                        type_named(msym->type, "Dict", 4) &&
                                        k + 1 < arg->coll_init.items.count) {
                                        zan_ast_list_push(&addcall->call.args,
                                            arg->coll_init.items.items[++k],
                                            g->arena);
                                    }
                                    emit_expr(g, addcall, locals);
                                }
                                /* drop the temp registration so a later
                                 * same-named declaration is unaffected */
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
                                /* custom-setter property in an object
                                 * initializer: `new Foo { Prop = v }`
                                 * dispatches to set_Prop(v) */
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
                                    /* ARC: the constructor ran before these initializer
                                     * writes and may have populated RC fields
                                     * (`new Label { Text = "x" }` — Label's ctor wraps
                                     * Text in a binding), so the overwritten value must
                                     * be released or it leaks on every construction.
                                     * Weak fields are non-owning slots: releasing the
                                     * previous value there would over-release, and
                                     * value-typed fields have nothing to release. */
                                    if (fsym && fsym->type && fval &&
                                        is_rc_managed_type(fsym->type) &&
                                        !(fsym->modifiers & MOD_WEAK) &&
                                        LLVMGetTypeKind(LLVMTypeOf(fval)) == LLVMPointerTypeKind) {
                                        LLVMValueRef fval_old = LLVMBuildLoad2(g->builder,
                                            LLVMTypeOf(fval), fptr, "arc.fold");
                                        zan_store_fit(g, fval, fptr);
                                        emit_rc_release_for_type(g, fsym->type, fval_old);
                                    } else {
                                        zan_store_fit(g, fval, fptr);
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

/* `expr switch { arm, ... }` (B6). Each arm is `pattern => result` (with an
 * optional `when` guard and an optional pattern variable), lowered to a
 * synthetic switch statement whose case bodies store their arm result into a
 * hidden local; the statement path already implements the full pattern chain
 * (constant/type/null/discard patterns, guards, break-via-loop_locals_base),
 * so the expression form reuses it instead of duplicating it. */
static LLVMValueRef emit_expr_switch_expr(zan_irgen_t *g, zan_ast_node_t *expr,
                                          local_scope_t *locals) {
    zan_type_t *rty = infer_expr_type(g, expr, locals);
    if (!rty || rty->kind == TYPE_ERROR || rty->kind == TYPE_VOID)
        rty = g->binder->type_int;
    LLVMTypeRef rtll = map_type(g, rty);
    LLVMValueRef rslot = emit_entry_alloca(g, rtll, "swx.result");
    zan_store_fit(g, LLVMConstNull(rtll), rslot);

    /* hidden local so each arm's `result` assignment lowers as a plain store
     * (the name starts with \x01, which no source identifier can contain) */
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
        /* no discard arm: an unmatched value keeps the zero-initialised slot;
         * an empty-but-breaking default keeps every path defined */
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
    if (!sym->decl->method_decl.extern_lib.str &&
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

/* `await native(...)` is the one suspension primitive whose callee is not a
 * Zan coroutine.  Keep the validation here, next to the lowering, so an
 * invalid direct extern await cannot fall through to the synchronous call
 * emitter and accidentally block the reactor. */
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

    /* The socket-async flag selects rt_io.o, which also owns the generic
     * blocking queue and reactor wake-up path used by this await. */
    g->uses_socket_async = true;
    int state = g->current_async_next_state++;
    LLVMValueRef frame = g->current_async_frame;
    LLVMTypeRef frame_type = g->current_async_frame_type;
    LLVMValueRef frame_i8 = LLVMBuildBitCast(g->builder, frame, i8ptr,
                                             "blocking.frame");
    LLVMValueRef out = NULL;
    if (ret->kind != TYPE_VOID) {
        out = LLVMBuildStructGEP2(g->builder, frame_type, frame,
                                  ASYNC_FRAME_RESULT, "blocking.out");
    } else {
        out = LLVMConstNull(LLVMPointerType(i64, 0));
    }
    LLVMValueRef fnptr = LLVMBuildBitCast(g->builder, native, i8ptr,
                                          "blocking.fn");
    emit_async_save_slots(g);
    zan_store_fit(g, LLVMConstInt(i32, (unsigned)state, 0),
        LLVMBuildStructGEP2(g->builder, frame_type, frame,
                            ASYNC_FRAME_STATE, "blocking.state"));
    LLVMValueRef rt_args[] = {
        fnptr, LLVMConstInt(i32, (unsigned)argc, 0),
        args[0], args[1], args[2], args[3],
        frame_i8, g->current_async_resume_fn, out
    };
    zan_call2(g->builder, g->rt_blocking_co_type, g->rt_blocking_co,
              rt_args, 9, "");
    emit_async_eh_unarm(g);
    LLVMBuildRetVoid(g->builder);

    LLVMBasicBlockRef resume = LLVMAppendBasicBlockInContext(g->ctx,
        g->current_async_resume_fn, "co.blocking.resume");
    LLVMAddCase(g->current_async_switch,
        LLVMConstInt(i32, (unsigned)state, 0), resume);
    LLVMPositionBuilderAtEnd(g->builder, resume);
    emit_async_reload_slots(g);
    if (ret->kind == TYPE_VOID) return LLVMConstInt(i64, 0, 0);
    LLVMValueRef raw = LLVMBuildLoad2(g->builder, i64,
        LLVMBuildStructGEP2(g->builder, frame_type, frame,
                            ASYNC_FRAME_RESULT, "blocking.result"),
        "blocking.raw");
    return coerce_await_result(g, expr, raw, locals);
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

        /* await Socket.ResolveAsync(hostname) — async DNS. The lookup runs on
         * a worker thread inside zan_io_resolve_co, so the reactor never
         * blocks on the resolver; the resolved IPv4 address (0 on failure,
         * same value as Socket.ResolveIpv4) is written into this frame's
         * RESULT slot before the suspended state machine is re-readied, and
         * the resume-k block loads it as the await value. */
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
                LLVMValueRef selfframe = g->current_async_frame;
                LLVMTypeRef self_ft = g->current_async_frame_type;
                LLVMValueRef self_i8 = LLVMBuildBitCast(g->builder, selfframe, di8ptr, "self");
                /* &self.result viewed as i32* — the reactor stores the
                 * resolved address here (RecvOv stores an i64 count into the
                 * same slot; both are read before the next suspension). */
                LLVMValueRef res_gep = LLVMBuildStructGEP2(g->builder, self_ft,
                    selfframe, ASYNC_FRAME_RESULT, "self.dnsres");
                LLVMValueRef out32 = LLVMBuildBitCast(g->builder, res_gep,
                    LLVMPointerType(di32, 0), "self.dnsout");
                emit_async_save_slots(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                        ASYNC_FRAME_STATE, "self.state"));
                zan_call2(g->builder, g->rt_io_resolve_co_type,
                    g->rt_io_resolve_co, (LLVMValueRef[]){ host, self_i8,
                        g->current_async_resume_fn, out32 }, 4, "");
                emit_async_eh_unarm(g);
                LLVMBuildRetVoid(g->builder);

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch,
                    LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                emit_async_reload_slots(g);
                LLVMValueRef res_slot = LLVMBuildBitCast(g->builder,
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                        ASYNC_FRAME_RESULT, "self.dnsres2"),
                    LLVMPointerType(di32, 0), "self.dnsout2");
                return LLVMBuildLoad2(g->builder, di32, res_slot, "dnsaddr");
            }
        }

        /* await Socket.ResolveSockAddr(name, port, buf, cap) — async
         * getaddrinfo: resolves a hostname OR literal IPv4/IPv6 to a complete
         * sockaddr (16 or 28 bytes) written into the caller's buffer. The
         * sockaddr length (0 on failure/timeout) lands in this frame's RESULT
         * slot before the state machine resumes. Same worker-thread machinery
         * as ResolveAsync, so the reactor never blocks on the resolver. */
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
                LLVMValueRef selfframe = g->current_async_frame;
                LLVMTypeRef self_ft = g->current_async_frame_type;
                LLVMValueRef self_i8 = LLVMBuildBitCast(g->builder, selfframe, di8ptr, "self");
                /* &self.result viewed as i32* — the reactor stores the
                 * sockaddr length here before re-readying the frame. */
                LLVMValueRef res_gep = LLVMBuildStructGEP2(g->builder, self_ft,
                    selfframe, ASYNC_FRAME_RESULT, "self.sares");
                LLVMValueRef out32 = LLVMBuildBitCast(g->builder, res_gep,
                    LLVMPointerType(di32, 0), "self.saout");
                emit_async_save_slots(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                        ASYNC_FRAME_STATE, "self.state"));
                zan_call2(g->builder, g->rt_io_resolve_sa_co_type,
                    g->rt_io_resolve_sa_co, (LLVMValueRef[]){ name, port, buf,
                        cap, self_i8, g->current_async_resume_fn, out32 }, 7, "");
                emit_async_eh_unarm(g);
                LLVMBuildRetVoid(g->builder);

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch,
                    LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                emit_async_reload_slots(g);
                LLVMValueRef res_slot = LLVMBuildBitCast(g->builder,
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                        ASYNC_FRAME_RESULT, "self.sares2"),
                    LLVMPointerType(di32, 0), "self.saout2");
                return LLVMBuildLoad2(g->builder, di32, res_slot, "salen");
            }
        }

        {
            zan_symbol_t *native = direct_extern_method(g, expr->await_expr.expr);
            if (native)
                return emit_await_blocking_extern(g, expr, locals, native);
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
                zan_emit_frame_free(g, sub_rl);
                return coerce_await_result(g, expr, awres, locals);
            }

            /* root drive (non-async caller). The scheduler is initialized once
             * at program entry (see emit_main_method); re-initializing here
             * would reset the ready queue and discard any coroutines already
             * enqueued by Task.Spawn before this await -- e.g. a spawned server
             * in a concurrent client/server program would never run. */
            LLVMValueRef sched_args[] = { sub_i8, sub_resume };
            zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
            /* Pump until *this* coroutine is done, not until the whole queue
             * drains: a background coroutine spawned meanwhile (a metrics
             * flusher, a spawned server) never completes, and draining would
             * turn a root-level await into a program that never continues. */
            LLVMValueRef done_p = LLVMBuildStructGEP2(g->builder, hdr, sub_i8,
                ASYNC_FRAME_DONE, "sub.done.p");
            zan_call2(g->builder, g->rt_co_sched_run_until_type,
                g->rt_co_sched_run_until, (LLVMValueRef[]){ done_p }, 1, "");
            emit_async_check_sub_exc(g, sub_i8);
            LLVMValueRef rptr = LLVMBuildStructGEP2(g->builder, hdr, sub_i8,
                ASYNC_FRAME_RESULT, "sub.result");
            LLVMValueRef awres = LLVMBuildLoad2(g->builder, i64, rptr, "awres");
            /* Root-driven sub has run to completion; copy out its result then
             * free its heap frame (no awaiter will). */
            zan_emit_frame_free(g, sub_i8);
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

/* ---- user-defined conversion operators (A43-B12) --------------------------
 * `static implicit operator T2(T1 v)` / `explicit operator T2(T1 v)` lower
 * to static `op_implicit`/`op_explicit` members. Like C#, the operator may
 * be declared on either the source type (T1) or the target type (T2);
 * find_user_conversion checks both. emit_user_conversion calls the matched
 * method with the source value fitted to its declared parameter type. */
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

/* A dummy `new` node used purely as an ownership signal: store paths
 * (emit_rc_capture_local / emit_rc_store_field) decide whether the incoming
 * value is an owned (+1) reference via expr_yields_owned_rc_value(rhs), which
 * reports true for any AST_NEW_EXPR. A synthesized conversion result is owned
 * exactly like a method-call result, so the caller hands it a marker to make
 * the store move the reference instead of retaining a second one. */
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
    /* Fit the source value to the declared parameter type (a struct source
     * passed by value vs. the method's `T1 v` by value). */
    zan_type_t *p0 = method_param_type_at(g, op, 0, val_expr, val_expr, locals);
    if (p0) {
        LLVMTypeRef p0t = map_type(g, p0);
        if (LLVMTypeOf(arg) != p0t) arg = coerce_int_to(g, arg, p0t);
    }
    return zan_call2(g->builder, mft, mfn, &arg, 1, "uc");
}

static LLVMValueRef emit_expr_cast_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* (Type)x — explicit numeric cast honoring the target type. */
        LLVMValueRef val = emit_expr(g, expr->cast.expr, locals);
        zan_type_t *tt = resolve_type_ctx(g, expr->cast.type);
        LLVMTypeRef target = tt ? map_type(g, tt) : LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef src = LLVMTypeOf(val);
        /* (T2)x where x's static type declares `explicit operator T2` (B12):
         * the cast calls the user-defined conversion instead of
         * reinterpreting the source as a pointer/number. */
        {
            zan_type_t *st = infer_expr_type(g, expr->cast.expr, locals);
            if (st && find_user_conversion(g, st, tt, "op_explicit"))
                return emit_user_conversion(g, st, tt, "op_explicit", val,
                                            expr->cast.expr, locals);
        }
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
                                          const char *tname);

/* Convenience wrapper: runtime `x is T` check from a zan type (extracts the
 * type's display name for the site table lookup). `tt->name` is authoritative
 * -- builtin types like `string` have no declaring symbol -- with the symbol
 * name as a fallback for synthetic types. */
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
    LLVMValueRef table = LLVMGetNamedGlobal(g->mod, "__zan_site_tynames");
    if (!g->desc_hdr && !table) return LLVMConstInt(i1, 0, 0);
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

    /* site word at obj-8 (second header word). A string stored in an
     * `object` slot is a bare buffer whose second header word carries
     * the string tag instead of a site index/descriptor, so probe for it
     * first: `x is string` must match, anything else must not. */
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

    /* out-of-range site index -> not a T. The bound is the constant table
     * size, not g->leak_site_count: site indices are assigned module-wide
     * across every `new` site, and this check may be emitted (e.g. inside a
     * helper function) before later functions register their sites. The
     * per-site name list is null for any never-registered index, so the walk
     * below answers false for those anyway. */
    LLVMPositionBuilderAtEnd(g->builder, oob_bb);
    LLVMValueRef oob;
    if (!g->desc_hdr) {
        oob = zan_or(g->builder,
            zan_icmp(g->builder, LLVMIntSLT, site, LLVMConstInt(i64, 0, 0), "is.neg"),
            zan_icmp(g->builder, LLVMIntSGE, site,
                LLVMConstInt(i64, ZAN_MAX_LEAK_SITES, 0), "is.oob"),
            "is.oob2");
    } else {
        /* descriptor mode: the header word is the record pointer (never a
         * small integer); zero means "no descriptor" -> not a T */
        oob = zan_icmp(g->builder, LLVMIntEQ, site, LLVMConstInt(i64, 0, 0),
                       "is.nodsc");
    }
    LLVMBuildCondBr(g->builder, oob, false_bb, loop);

    /* walk the ancestor-name list */
    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef idx = LLVMBuildPhi(g->builder, i64, "is.i");
    LLVMValueRef list;
    if (!g->desc_hdr) {
        LLVMValueRef list_p = LLVMBuildGEP2(g->builder,
            LLVMArrayType(i8ptr, ZAN_MAX_LEAK_SITES), table,
            (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), site }, 2, "is.lp");
        list = LLVMBuildLoad2(g->builder, i8ptr, list_p, "is.list");
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

    case AST_TUPLE_EXPR:
        return emit_expr_tuple(g, expr, locals);

    case AST_CONDITIONAL:
        return emit_expr_conditional(g, expr, locals);

    case AST_SWITCH_EXPR:
        return emit_expr_switch_expr(g, expr, locals);

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
         * string; `x is string` is handled by static types.
         * B5: `x is not T` negates the test, `x is null` is a null check, and
         * `x is T v` additionally binds a local v holding the (cast) value. */
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
                    /* both reference kinds: value must be non-null for any
                     * `is` to hold */
                    LLVMValueRef nn = zan_icmp(g->builder, LLVMIntNE, v,
                        LLVMConstNull(LLVMTypeOf(v)), "is.nonnull");
                    if (st->kind == TYPE_STRING || tt->kind == TYPE_STRING) {
                        if (st->kind == tt->kind) {
                            test = nn; /* string is string iff non-null */
                        } else if (st->kind == TYPE_OBJECT ||
                                   st->kind == TYPE_INTERFACE ||
                                   tt->kind == TYPE_OBJECT ||
                                   tt->kind == TYPE_INTERFACE) {
                            /* `object o = "hi"; o is string`: the slot may
                             * hold a string, whose obj-8 magic makes the
                             * runtime check answer exactly this. */
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
        /* pattern variable: `x is T v` registers v in the current scope so
         * the matched branch can use it. The value is the tested expression
         * itself (reference casts are identity; the is-test has already
         * verified the runtime class). */
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
        /* typeof(T) — the type's reflection record (irgen_reflect.c). The
         * record's payload IS the display name as a managed string, so the
         * older `Console.WriteLine(typeof(int))` spelling still prints
         * "int". */
        char buf[256];
        int len = render_type_ref_name(expr->cast.type, buf, (int)sizeof(buf));
        zan_type_t *t = resolve_type_ctx(g, expr->cast.type);
        return refl_meta_for(g, t, buf, len);
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
    /* Grown on demand: a fixed cap here rejected the whole lambda once a body
     * mentioned more than a handful of enclosing locals. */
    lambda_capture_t *caps;
    int              count;
    int              cap;
    int              needs_this;
    zan_istr_t      *shadow;
    int              shadow_count;
    int              shadow_cap;
    int              overflow;
    /* write scan (A33-2b): instead of collecting captures, report whether a
     * lambda somewhere below assigns to `want_write`. With `write_any` the
     * scan reports writes at any depth, lambda or not (used to decide whether
     * a parameter's slot has to own its reference). */
    zan_istr_t       want_write;
    int              lam_depth;
    int              found_write;
    int              write_any;
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
    if (!ZAN_TAB_ENSURE(cs->caps, cs->count, cs->cap, 16)) {
        cs->overflow = 1;
        return;
    }
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

/* The identifier `n` writes (assignment target, ++/-- operand, ref/out arg),
 * or {NULL,0} when `n` is not a write. Used by the whole-body write-scan
 * collector (A79-1), which needs the name itself rather than a yes/no. */
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
    case AST_COLL_INIT:       cap_scan_list(cs, &n->coll_init.items); return;
    case AST_CAST_EXPR:       cap_scan(cs, n->cast.expr); return;
    case AST_IS_EXPR:
    case AST_AS_EXPR:         cap_scan(cs, n->type_test.expr); return;
    case AST_AWAIT_EXPR:      cap_scan(cs, n->await_expr.expr); return;
    case AST_REF_ARG:         cap_scan(cs, n->ref_arg.expr); return;
    case AST_STRING_INTERP:   cap_scan_list(cs, &n->string_interp.parts); return;
    case AST_QUERY_EXPR: {
        cap_scan(cs, n->query.source);
        cap_shadow(cs, n->query.var);
        for (int ci = 0; ci < n->query.clauses.count; ci++) {
            zan_ast_node_t *cl = n->query.clauses.items[ci];
            if (cl->kind == AST_QUERY_JOIN) {
                /* s evaluates in the outer scope; the left key sees x and
                 * the lets; the right key sees the join var */
                cap_scan(cs, cl->query_clause.source);
                cap_scan(cs, cl->query_clause.left_key);
                cap_shadow(cs, cl->query_clause.name);
                cap_scan(cs, cl->query_clause.right_key);
                if (cl->query_clause.into.len > 0)
                    cap_shadow(cs, cl->query_clause.into);
            } else {
                cap_scan(cs, cl->query_clause.expr);
                if (cl->kind == AST_QUERY_LET)
                    cap_shadow(cs, cl->query_clause.name);
            }
        }
        if (n->query.group_expr) {
            cap_scan(cs, n->query.group_expr);
            cap_scan(cs, n->query.group_key);
            if (n->query.group_into.len > 0)
                cap_shadow(cs, n->query.group_into);
        }
        if (n->query.select) cap_scan(cs, n->query.select);
        return;
    }
    case AST_SWITCH_EXPR:
        cap_scan(cs, n->switch_expr.expr);
        for (int ai = 0; ai < n->switch_expr.arms.count; ai++) {
            zan_ast_node_t *arm = n->switch_expr.arms.items[ai];
            if (arm->switch_arm.pattern) cap_scan(cs, arm->switch_arm.pattern);
            if (arm->switch_arm.type_pattern && arm->switch_arm.var_name.len > 0)
                cap_shadow(cs, arm->switch_arm.var_name);
            if (arm->switch_arm.when_cond) cap_scan(cs, arm->switch_arm.when_cond);
            cap_scan(cs, arm->switch_arm.result);
        }
        return;
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
    LLVMValueRef neg16 = LLVMConstInt(i64, (unsigned long long)ZAN_OBJ_RC_OFF, 1);
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

/* Does `body` assign to `name` anywhere -- inside a lambda or not? A parameter
 * that is assigned needs a slot that owns its reference (see the parameter
 * binding in irgen_emit.c); one that is only read stays a zero-cost borrow.
 *
 * The answer is memoized per {body, name} in g->body_write_memo (A79-1). The
 * scan itself collects EVERY assigned name of the body in one walk, so the
 * first question about a body costs one full traversal and every later
 * question about the same body is a hash lookup: a method with N locals asks
 * N times, which without the memo re-walked the whole body N times (quadratic
 * -- a 12k-declaration method spent 18 minutes here alone). Identifiers are
 * interned by the lexer, so name equality is pointer equality. */
static int body_writes_ident_scan(zan_irgen_t *g, zan_ast_node_t *body,
                                  zan_istr_t name);

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
        /* copy the live entries into the fresh table (name.str pointers are
         * interned and stable for the whole compilation, so entries carry) */
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
            g->body_write_memo[i].name.str == name.str &&
            g->body_write_memo[i].name.len == name.len)
            return &g->body_write_memo[i];
        i = (i + 1) & mask;
    }
    return &g->body_write_memo[i];
}

/* Collect every assigned identifier of `n` into the memo table (one entry per
 * {body, name}). `body` is the root the walk started from. A write at
 * lam_depth > 0 (inside a nested lambda) sets lam_written as well: the
 * boxed-local rule (A33-2b) asks specifically "does a lambda assign to this
 * local?", while the parameter-ownership rule asks "is it assigned at all?" --
 * the two bits keep both questions answerable from one walk. */
static void body_write_collect(zan_irgen_t *g, zan_ast_node_t *n,
                               zan_ast_node_t *body, int lam_depth) {
    if (!n) return;
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
        /* a nested lambda's parameters shadow enclosing names: writes to them
         * inside the lambda do NOT reach the enclosing frame, so pre-seed them
         * as "known, not written" for the outer body's question */
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
    case AST_SWITCH_ARM:      body_write_collect(g, n->switch_arm.pattern, body, lam_depth);
                              body_write_collect(g, n->switch_arm.when_cond, body, lam_depth);
                              body_write_collect(g, n->switch_arm.result, body, lam_depth); return;
    case AST_BLOCK:           body_write_collect_list(g, &n->block.stmts, body, lam_depth); return;
    case AST_VAR_DECL:        body_write_collect(g, n->var_decl.initializer, body, lam_depth);
                              /* pre-seed the declared name as "known": the
                               * declaration is a binding, not a write, so the
                               * boxed-local rule answers on real writes only */
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
                              body_write_collect(g, n->switch_case.body, body, lam_depth); return;
    case AST_LOCK_STMT:       body_write_collect(g, n->lock_stmt.expr, body, lam_depth);
                              body_write_collect(g, n->lock_stmt.body, body, lam_depth); return;
    case AST_YIELD_STMT:      body_write_collect(g, n->yield_stmt.value, body, lam_depth); return;
    default: return;
    }
}

/* Ensure the memo holds the full write-set of `body`; then answer for `name`
 * from the table. Anything not in the table is "not written" -- the walk
 * pre-seeds declarations and lambda parameters so shadowing stays exact. */
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
            g->body_write_memo[i].name.str == name.str &&
            g->body_write_memo[i].name.len == name.len)
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

/* Does a lambda in the body being compiled assign to `name`? The memo carries
 * two per-name bits: writes anywhere in the body (the parameter-ownership
 * question) and writes from inside a lambda only (the boxed-local question --
 * an enclosing local a lambda assigns must become a shared heap cell, while a
 * local merely assigned by ordinary statements must NOT). */
static int local_is_lambda_written(zan_irgen_t *g, local_scope_t *locals,
                                   zan_istr_t name) {
    (void)locals;
    if (!g->current_fn_body || !name.len) return 0;
    body_writes_ident_memo(g, g->current_fn_body, name);
    unsigned mask = g->body_write_memo_cap - 1;
    unsigned i = body_write_memo_hash(g->current_fn_body, name) & mask;
    while (g->body_write_memo[i].known) {
        if (g->body_write_memo[i].body == g->current_fn_body &&
            g->body_write_memo[i].name.str == name.str &&
            g->body_write_memo[i].name.len == name.len)
            return g->body_write_memo[i].lam_written;
        i = (i + 1) & mask;
    }
    return 0;
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
    snprintf(bname, sizeof(bname), "box_%d",
             (int)__atomic_fetch_add(&box_id, 1, __ATOMIC_SEQ_CST));
    lambda_capture_t val = { .name = (zan_istr_t){ NULL, 0 }, .slot = NULL,
                             .type = vtype, .llvm = payload, .boxed = 0 };
    LLVMValueRef dtor = build_closure_dtor(g, bname, rec_ty, &val, 1, 0);
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
            "out of memory collecting the variables this lambda captures");
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
    snprintf(lname, sizeof(lname), "lambda_%d",
             (int)__atomic_fetch_add(&lambda_id, 1, __ATOMIC_SEQ_CST));
    LLVMValueRef lambda_fn = LLVMAddFunction(g->mod, lname, fn_type);

    /* { ptr fn, ptr dtor, ptr target, <captures>, [ptr this] }; a lambda has no
     * bound target, so that slot stays null (see the closure record layout). */
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

    /* -g: the lambda is its own LLVM function, so its prologue (capture/param
     * allocas) and body must not inherit the enclosing function's
     * DISubprogram scope -- an instruction whose !dbg scope belongs to a
     * different function is a hard verifier error ("!dbg attachment points at
     * wrong subprogram"). Save the caller's location and re-anchor the builder
     * to the lambda's own scope at its source line: di_set_loc, now that the
     * builder sits in lambda_fn, lazily creates lambda_fn's DISubprogram and
     * stamps a valid in-scope location, so both the prologue and an
     * expression-bodied lambda's calls carry a !dbg (LLVM also requires every
     * inlinable call in a debug-info function to have one). Block bodies
     * override this per statement; the caller's location is restored once the
     * builder returns to the caller's block below. */
    LLVMMetadataRef saved_dl = NULL;
    uint32_t saved_di_line = g->di_cur_line;
    uint32_t saved_di_file = g->di_cur_file;
    if (g->emit_debug) {
        saved_dl = LLVMGetCurrentDebugLocation2(g->builder);
        di_set_loc(g, expr->loc);
    }

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
    int saved_eh_c = g->eh_armed_count;
    int saved_eh_b = g->eh_armed_base;
    int saved_eh_lb = g->eh_armed_loop_base;
    g->throw_locals_base = 0;
    g->catch_cleanup_count = 0;
    g->throw_catch_base = 0;
    g->eh_armed_base = g->eh_armed_count;
    g->eh_armed_loop_base = g->eh_armed_count;
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
    g->eh_armed_count = saved_eh_c;
    g->eh_armed_base = saved_eh_b;
    g->eh_armed_loop_base = saved_eh_lb;
    g->current_this = saved_this;
    g->current_async_frame = saved_async_frame;
    g->current_fn_body = saved_fn_body;
    g->lambda_depth--;
    LLVMPositionBuilderAtEnd(g->builder, saved_bb);
    /* Back in the caller's function: restore its debug location so the closure
     * record built below (and any later instruction sharing this statement)
     * carries the enclosing scope again, not the lambda's. */
    if (g->emit_debug) {
        LLVMSetCurrentDebugLocation2(g->builder, saved_dl);
        g->di_cur_line = saved_di_line;
        g->di_cur_file = saved_di_file;
    }
    free(param_types);
    free(ptypes);
    if (!is_closure) return lambda_fn;

    LLVMValueRef self = has_this
        ? LLVMBuildLoad2(g->builder, LLVMGetAllocatedType(saved_this),
                         saved_this, "cap.self")
        : NULL;
    LLVMValueRef clo = emit_closure_record(g, expr->loc, lname, rec_ty,
                                           lambda_fn, NULL, cs.caps, capc,
                                           has_this, self);
    cap_scan_free(&cs);
    return clo;
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

/* Rewrite the call-site argument in place so the normal class-construction
 * path owns and releases the synthesized object exactly like explicit `new`.
 * Keeping the original expression as the new node's child also preserves its
 * evaluation order and source location. */
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
    /* user-defined implicit conversion in an argument: `Show(f)` where the
     * parameter is double and Feet declares `implicit operator double` (B12). */
    if (v && ptype && atype)
        v = emit_user_conversion(g, atype, ptype, "op_implicit", v, arg, locals);
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
