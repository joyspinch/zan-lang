/* irgen_expr.c -- NativeMemory intrinsics and the main expression emitter
 * (emit_expr) plus lambda emission and generic-method call inference.
 *
 * Part of the irgen translation unit: this file is #include'd by irgen.c
 * (in a fixed order) and must not be compiled standalone. Splitting keeps
 * the single-TU static linkage while keeping each concern in its own file.
 */

/* ===== NativeMemory intrinsics =====================================
 * Raw off-heap memory for binary IO. An address is a plain nint (i64) that
 * never enters ARC. Every accessor lowers to direct (align-1) loads/stores or
 * a single libc call, so byte-level hot loops in Zan code (page encoding,
 * checksums, network framing) cost what the equivalent C would.
 *
 *   nint  Alloc(int size)                     zeroed malloc
 *   void  Free(nint p)
 *   void  Copy(nint dst, nint src, int n)     memmove (overlap-safe)
 *   void  Fill(nint p, int byte, int n)       memset
 *   int   Compare(nint a, nint b, int n)      memcmp
 *   int   GetByte/GetU16/GetU32/GetI64(nint p, int off)   little-endian
 *   void  SetByte/SetU16/SetU32/SetI64(nint p, int off, int v)
 *   string GetString(nint p, int off, int len)  copies into a real ARC string
 *   void  PutString(nint p, int off, string s, int len)
 *   int   Crc32(nint p, int len)              hardware-independent CRC32 (IEEE)
 */

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

/* Little-endian load of `bits` at (p+off), zero-extended to i64 (i64 loads
 * are returned as-is). align 1: page/wire offsets are arbitrary. */
static LLVMValueRef nm_load(zan_irgen_t *g, LLVMValueRef base, LLVMValueRef off, int bits) {
    LLVMTypeRef it = LLVMIntTypeInContext(g->ctx, (unsigned)bits);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef ptr = LLVMBuildBitCast(g->builder, nm_addr(g, base, off),
        LLVMPointerType(it, 0), "nm.lp");
    LLVMValueRef v = LLVMBuildLoad2(g->builder, it, ptr, "nm.ld");
    LLVMSetAlignment(v, 1);
    if (bits < 64) v = LLVMBuildZExt(g->builder, v, i64t, "nm.zx");
    return v;
}

static void nm_store(zan_irgen_t *g, LLVMValueRef base, LLVMValueRef off,
                     LLVMValueRef val, int bits) {
    LLVMTypeRef it = LLVMIntTypeInContext(g->ctx, (unsigned)bits);
    LLVMValueRef ptr = LLVMBuildBitCast(g->builder, nm_addr(g, base, off),
        LLVMPointerType(it, 0), "nm.sp");
    if (bits < 64) val = LLVMBuildTrunc(g->builder, val, it, "nm.tr");
    LLVMValueRef st = LLVMBuildStore(g->builder, val, ptr);
    LLVMSetAlignment(st, 1);
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
    LLVMValueRef in_range = LLVMBuildICmp(g->builder, LLVMIntSLT, idx, len, "inrange");
    LLVMBuildCondBr(g->builder, in_range, body, done);

    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef bp = LLVMBuildGEP2(g->builder, i8, data, &idx, 1, "bp");
    LLVMValueRef byte = LLVMBuildZExt(g->builder,
        LLVMBuildLoad2(g->builder, i8, bp, "b"), i32t, "b32");
    LLVMValueRef ti = LLVMBuildAnd(g->builder,
        LLVMBuildXor(g->builder, crc, byte, "x"),
        LLVMConstInt(i32t, 0xFF, 0), "ti");
    LLVMValueRef ti64 = LLVMBuildZExt(g->builder, ti, i64t, "ti64");
    LLVMValueRef gep_idx[] = { LLVMConstInt(i64t, 0, 0), ti64 };
    LLVMValueRef ep = LLVMBuildGEP2(g->builder, tab_ty, tab, gep_idx, 2, "ep");
    LLVMValueRef te = LLVMBuildLoad2(g->builder, i32t, ep, "te");
    LLVMValueRef next_crc = LLVMBuildXor(g->builder, te,
        LLVMBuildLShr(g->builder, crc, LLVMConstInt(i32t, 8, 0), "sh"), "nc");
    LLVMValueRef next_idx = LLVMBuildAdd(g->builder, idx, LLVMConstInt(i64t, 1, 0), "ni");
    LLVMBuildBr(g->builder, loop);

    LLVMAddIncoming(idx, (LLVMValueRef[]){ LLVMConstInt(i64t, 0, 0), next_idx },
        (LLVMBasicBlockRef[]){ entry, body }, 2);
    LLVMAddIncoming(crc, (LLVMValueRef[]){ LLVMConstInt(i32t, 0xFFFFFFFFu, 0), next_crc },
        (LLVMBasicBlockRef[]){ entry, body }, 2);

    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMValueRef fin = LLVMBuildXor(g->builder, crc,
        LLVMConstInt(i32t, 0xFFFFFFFFu, 0), "fin");
    LLVMBuildRet(g->builder, LLVMBuildZExt(g->builder, fin, i64t, "fin64"));
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
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
        LLVMValueRef p = zan_call2(g->builder, ty, fn,
            (LLVMValueRef[]){ size, LLVMConstInt(i64t, 1, 0) }, 2, "nm.alloc");
        *out = LLVMBuildPtrToInt(g->builder, p, i64t, "nm.addr");
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

    static const struct { const char *get, *set; int bits; } nm_acc[] = {
        { "GetByte", "SetByte", 8 },
        { "GetU16",  "SetU16", 16 },
        { "GetU32",  "SetU32", 32 },
        { "GetI64",  "SetI64", 64 },
    };
    for (size_t i = 0; i < sizeof(nm_acc) / sizeof(nm_acc[0]); i++) {
        if (is_call_to(expr, "NativeMemory", nm_acc[i].get) && expr->call.args.count == 2) {
            LLVMValueRef p = nm_arg(g, expr, 0, locals);
            LLVMValueRef off = nm_arg(g, expr, 1, locals);
            *out = nm_load(g, p, off, nm_acc[i].bits);
            return true;
        }
        if (is_call_to(expr, "NativeMemory", nm_acc[i].set) && expr->call.args.count == 3) {
            LLVMValueRef p = nm_arg(g, expr, 0, locals);
            LLVMValueRef off = nm_arg(g, expr, 1, locals);
            LLVMValueRef v = nm_arg(g, expr, 2, locals);
            nm_store(g, p, off, v, nm_acc[i].bits);
            *out = zero64;
            return true;
        }
    }

    if (is_call_to(expr, "NativeMemory", "GetString") && expr->call.args.count == 3) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef off = nm_arg(g, expr, 1, locals);
        LLVMValueRef len = nm_arg(g, expr, 2, locals);
        LLVMValueRef one = LLVMConstInt(i64t, 1, 0);
        LLVMValueRef total = LLVMBuildAdd(g->builder, len, one, "nm.gs.sz");
        LLVMValueRef s = emit_string_alloc_rc(g, total);
        LLVMTypeRef ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
        LLVMValueRef fn = get_libc_fn(g, "memcpy", ty);
        zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            s, nm_addr(g, p, off), len }, 3, "");
        LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, s, &len, 1, "nm.gs.end");
        LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
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

static LLVMValueRef emit_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals) {
    if (!expr) return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);

    if (expr->kind == AST_REF_ARG) {
        return emit_ref_arg(g, expr, locals);
    }
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

    case AST_IDENTIFIER: {
        local_var_t *local = local_find(locals, expr->ident.name);
        if (local) {
            return LLVMBuildLoad2(g->builder, map_type(g, local->type),
                                 local->alloca, "load");
        }
        /* implicit this.Field access in method bodies */
        if (g->current_this && g->current_type_sym) {
            int fi = get_field_index(g->current_type_sym, expr->ident.name);
            if (fi >= 0) {
                LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                if (st) {
                    LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(st, 0), g->current_this, "this");
                    LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st, this_ptr, (unsigned)fi, "fld");
                    zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->ident.name);
                    LLVMTypeRef ft = fsym ? map_type(g, fsym->type) : LLVMInt64TypeInContext(g->ctx);
                    return LLVMBuildLoad2(g->builder, ft, fptr, "fval");
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
                return LLVMBuildLoad2(g->builder, ft, gv, "sfld");
            }
        }
        /* method reference as delegate value: MethodName used as a value
         * (not called) → return the function pointer */
        if (g->current_type_sym) {
            zan_symbol_t *method_sym = get_method_sym(g->current_type_sym, expr->ident.name);
            if (method_sym) {
                for (int fi = 0; fi < g->function_count; fi++) {
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
    }

    case AST_BINARY: {
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
                lval = LLVMBuildICmp(g->builder, LLVMIntNE, lval,
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
                rval = LLVMBuildICmp(g->builder, LLVMIntNE, rval,
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
            if (!null_cmp && ltype && ltype->kind == TYPE_CLASS && ltype->sym) {
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
                        for (int fi = 0; fi < g->function_count; fi++) {
                            if (g->functions[fi].sym == op_sym) {
                                LLVMValueRef args[] = { left, right };
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(g->functions[fi].fn_type)) == LLVMVoidTypeKind) ? "" : "opcall";
                                return zan_call2(g->builder, g->functions[fi].fn_type,
                                    g->functions[fi].fn, args, 2, cn);
                            }
                        }
                    }
                }
            }
        }

        bool both_ptr =
            LLVMGetTypeKind(LLVMTypeOf(left)) == LLVMPointerTypeKind &&
            LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMPointerTypeKind;
        bool str_operand = is_string_expr(g, expr->binary.left, locals) ||
                           is_string_expr(g, expr->binary.right, locals);

        /* string concatenation: `a + b` when either operand is a string. A
         * numeric operand is formatted to its decimal string first (e.g.
         * "%t" + counter), so concat is not limited to two pointers. The
         * numeric temporaries are released after the concat copies them. */
        if (expr->binary.op == TK_PLUS && str_operand) {
            LLVMValueRef ls = emit_to_cstr(g, left);
            LLVMValueRef rs = emit_to_cstr(g, right);
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
            LLVMValueRef seq = LLVMBuildICmp(g->builder,
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
            LLVMValueRef sord = LLVMBuildICmp(g->builder, pred,
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
            LLVMValueRef pcmp = LLVMBuildICmp(g->builder,
                expr->binary.op == TK_EQ_EQ ? LLVMIntEQ : LLVMIntNE,
                left, rcast, "pcmp");
            emit_release_owned_call_temp(g, expr->binary.left, left, locals);
            emit_release_owned_call_temp(g, expr->binary.right, right, locals);
            return pcmp;
        }

        LLVMTypeRef left_type = LLVMTypeOf(left);
        bool is_float = (LLVMGetTypeKind(left_type) == LLVMDoubleTypeKind ||
                         LLVMGetTypeKind(left_type) == LLVMFloatTypeKind);

        /* reconcile mixed-width integer operands (e.g. i32 int vs i64 length) */
        coerce_int_pair(g, &left, &right);

        /* ulong operands select unsigned division/remainder/shift/compare */
        bool is_unsigned = !is_float &&
            (expr_is_ulong(g, expr->binary.left, locals) ||
             expr_is_ulong(g, expr->binary.right, locals));

        switch (expr->binary.op) {
        case TK_PLUS:
            return is_float ? LLVMBuildFAdd(g->builder, left, right, "add")
                            : LLVMBuildAdd(g->builder, left, right, "add");
        case TK_MINUS:
            return is_float ? LLVMBuildFSub(g->builder, left, right, "sub")
                            : LLVMBuildSub(g->builder, left, right, "sub");
        case TK_STAR:
            return is_float ? LLVMBuildFMul(g->builder, left, right, "mul")
                            : LLVMBuildMul(g->builder, left, right, "mul");
        case TK_SLASH:
            if (is_float) return LLVMBuildFDiv(g->builder, left, right, "div");
            {
                LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(right), 0, 0);
                LLVMValueRef is_zero = LLVMBuildICmp(g->builder, LLVMIntEQ, right, zero, "divz");
                emit_runtime_check(g, is_zero, expr->loc, "division by zero");
            }
            return is_unsigned ? LLVMBuildUDiv(g->builder, left, right, "div")
                               : LLVMBuildSDiv(g->builder, left, right, "div");
        case TK_PERCENT:
            if (is_float) return LLVMBuildFRem(g->builder, left, right, "rem");
            {
                LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(right), 0, 0);
                LLVMValueRef is_zero = LLVMBuildICmp(g->builder, LLVMIntEQ, right, zero, "remz");
                emit_runtime_check(g, is_zero, expr->loc, "division by zero (modulo)");
            }
            return is_unsigned ? LLVMBuildURem(g->builder, left, right, "rem")
                               : LLVMBuildSRem(g->builder, left, right, "rem");
        case TK_AMP:
            return LLVMBuildAnd(g->builder, left, right, "and");
        case TK_PIPE:
            return LLVMBuildOr(g->builder, left, right, "or");
        case TK_CARET:
            return LLVMBuildXor(g->builder, left, right, "xor");
        case TK_LESS_LESS:
            return LLVMBuildShl(g->builder, left, right, "shl");
        case TK_GREATER_GREATER:
            return is_unsigned ? LLVMBuildLShr(g->builder, left, right, "shr")
                               : LLVMBuildAShr(g->builder, left, right, "shr");
        case TK_EQ_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOEQ, left, right, "eq")
                            : LLVMBuildICmp(g->builder, LLVMIntEQ, left, right, "eq");
        case TK_BANG_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealONE, left, right, "ne")
                            : LLVMBuildICmp(g->builder, LLVMIntNE, left, right, "ne");
        case TK_LESS:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOLT, left, right, "lt")
                            : LLVMBuildICmp(g->builder, is_unsigned ? LLVMIntULT : LLVMIntSLT, left, right, "lt");
        case TK_GREATER:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOGT, left, right, "gt")
                            : LLVMBuildICmp(g->builder, is_unsigned ? LLVMIntUGT : LLVMIntSGT, left, right, "gt");
        case TK_LESS_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOLE, left, right, "le")
                            : LLVMBuildICmp(g->builder, is_unsigned ? LLVMIntULE : LLVMIntSLE, left, right, "le");
        case TK_GREATER_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOGE, left, right, "ge")
                            : LLVMBuildICmp(g->builder, is_unsigned ? LLVMIntUGE : LLVMIntSGE, left, right, "ge");
        case TK_QUESTION_QUESTION: {
            /* ?? null coalescing: if left != 0/null, use left, else right */
            LLVMValueRef is_null;
            if (LLVMGetTypeKind(left_type) == LLVMPointerTypeKind) {
                LLVMValueRef null_ptr = LLVMConstNull(left_type);
                is_null = LLVMBuildICmp(g->builder, LLVMIntEQ, left, null_ptr, "isnull");
            } else {
                is_null = LLVMBuildICmp(g->builder, LLVMIntEQ, left,
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

    case AST_UNARY: {
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
                return LLVMBuildICmp(g->builder, LLVMIntEQ, operand,
                    LLVMConstInt(ot, 0, 0), "lnot");
            }
            if (otk == LLVMPointerTypeKind) {
                return LLVMBuildICmp(g->builder, LLVMIntEQ, operand,
                    LLVMConstNull(ot), "lnotp");
            }
            return LLVMBuildNot(g->builder, operand, "not");
        }
        case TK_TILDE:
            return LLVMBuildNot(g->builder, operand, "bnot");
        default:
            return operand;
        }
    }

    case AST_ASSIGNMENT: {
        LLVMValueRef right = emit_expr(g, expr->binary.right, locals);
        if (expr->binary.left->kind == AST_IDENTIFIER) {
            local_var_t *local = local_find(locals, expr->binary.left->ident.name);
            if (local && local_owns_arc(local)) {
                /* ARC: release the previous occupant and retain the new one. */
                emit_rc_capture_local(g, local->type, local->alloca, right, expr->binary.right, locals);
            } else if (local) {
                LLVMValueRef sv = coerce_int_to(g, right,
                    local_slot_type(g, local));
                LLVMBuildStore(g->builder, sv, local->alloca);
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
                    LLVMBuildStore(g->builder, coerce_int_to(g, right, ft), gv);
                }
            } else if (g->current_this && g->current_type_sym) {
                /* implicit this.Field assignment */
                int fi = get_field_index(g->current_type_sym, expr->binary.left->ident.name);
                if (fi >= 0) {
                    LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                    if (st) {
                        LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                            LLVMPointerType(st, 0), g->current_this, "this");
                        LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st, this_ptr, (unsigned)fi, "fld");
                        /* type conversion if needed */
                        zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->binary.left->ident.name);
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
                        if (fsym && fsym->type && is_rc_managed_type(fsym->type)) {
                            emit_rc_store_field(g, fsym->type, fptr, right, expr->binary.right, locals,
                                                (fsym->modifiers & MOD_WEAK) ? 1 : 0);
                        } else {
                            LLVMBuildStore(g->builder, right, fptr);
                        }
                    }
                }
            }
        } else if (expr->binary.left->kind == AST_INDEX) {
            /* arr[i] = value */
            zan_ast_node_t *arr_expr = expr->binary.left->index.object;
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
                        if (LLVMGetIntTypeWidth(LLVMTypeOf(key)) < 64)
                            key = LLVMBuildSExt(g->builder, key, i64, "k.sx");
                        key = LLVMBuildIntToPtr(g->builder, key, i8ptr, "k.ip");
                    } else if (LLVMTypeOf(key) != i8ptr) {
                        key = LLVMBuildBitCast(g->builder, key, i8ptr, "k.bc");
                    }
                    if (kt && is_rc_managed_type(kt) &&
                        !expr_yields_owned_rc_value(g, expr->binary.left->index.index, locals))
                        emit_rc_retain_for_type(g, kt, key);
                    LLVMValueRef val = right;
                    LLVMTypeKind vk = LLVMGetTypeKind(LLVMTypeOf(val));
                    if (vk == LLVMPointerTypeKind) {
                        if (vt && is_rc_managed_type(vt) &&
                            !expr_yields_owned_rc_value(g, expr->binary.right, locals))
                            emit_rc_retain_for_type(g, vt, val);
                        val = LLVMBuildPtrToInt(g->builder, val, i64, "v.pi");
                    } else if (vk == LLVMDoubleTypeKind) {
                        val = LLVMBuildBitCast(g->builder, val, i64, "v.bc");
                    } else if (vk == LLVMIntegerTypeKind &&
                               LLVMGetIntTypeWidth(LLVMTypeOf(val)) < 64) {
                        val = LLVMBuildSExt(g->builder, val, i64, "v.sx");
                    }
                    LLVMValueRef is_str = LLVMConstInt(i64,
                        (kt && kt->kind == TYPE_STRING) ? 1 : 0, 0);
                    LLVMValueRef sf = get_dict_set_fn(g);
                    zan_call2(g->builder, LLVMGlobalGetValueType(sf), sf,
                        (LLVMValueRef[]){ raw, key, val, is_str }, 4, "");
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
                        LLVMBuildStore(g->builder, val8, elem_ptr);
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
                            LLVMBuildStore(g->builder, stored, elem_ptr);
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
                                } else if (val_k == LLVMIntegerTypeKind &&
                                           LLVMGetIntTypeWidth(LLVMTypeOf(stored)) < 64) {
                                    stored = LLVMBuildSExt(g->builder, stored, elem_llvm, "slot.sx");
                                }
                            }
                            LLVMBuildStore(g->builder, stored, elem_ptr);
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
                            LLVMValueRef slot_ptr = LLVMBuildGEP2(g->builder, i64t, data, &idx, 1, "ep");
                            emit_collection_slot_store(g, container_elem_type(fsym->type), i64t, slot_ptr,
                                right, expr->binary.right, locals, 1);
                        } else if (fsym->type && fsym->type->kind == TYPE_STRING) {
                            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                            LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i8, arr_ptr, &idx, 1, "eidx");
                            LLVMValueRef val8 = LLVMBuildTrunc(g->builder, right, i8, "byte");
                            LLVMBuildStore(g->builder, val8, elem_ptr);
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
                            LLVMBuildStore(g->builder, stored, elem_ptr);
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
                                } else if (val_k == LLVMIntegerTypeKind &&
                                           LLVMGetIntTypeWidth(LLVMTypeOf(stored)) < 64) {
                                    stored = LLVMBuildSExt(g->builder, stored, elem_llvm, "slot.sx");
                                }
                            }
                            LLVMBuildStore(g->builder, stored, elem_ptr);
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
                        LLVMValueRef slot_ptr = LLVMBuildGEP2(g->builder, i64t, data, &idx, 1, "ep");
                        emit_collection_slot_store(g, container_elem_type(at), i64t, slot_ptr,
                            right, expr->binary.right, locals, 1);
                    } else if (at->kind == TYPE_STRING) {
                        LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i8, arr_ptr, &idx, 1, "eidx");
                        LLVMValueRef val8 = LLVMBuildTrunc(g->builder, right, i8, "byte");
                        LLVMBuildStore(g->builder, val8, elem_ptr);
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
                            LLVMBuildStore(g->builder, stored, elem_ptr);
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
                                } else if (val_k == LLVMIntegerTypeKind &&
                                           LLVMGetIntTypeWidth(LLVMTypeOf(stored)) < 64) {
                                    stored = LLVMBuildSExt(g->builder, stored, elem_llvm, "slot.sx");
                                }
                            }
                            LLVMBuildStore(g->builder, stored, elem_ptr);
                        }
                    }
                }
            }
        } else if (expr->binary.left->kind == AST_MEMBER_ACCESS) {
            /* obj.Field = value */
            zan_ast_node_t *obj_expr = expr->binary.left->member.object;
            bool stored = false;
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
                            LLVMBuildStore(g->builder, coerce_int_to(g, right, ft), gv);
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
                            LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st, struct_ptr, (unsigned)fi, "fld");
                            zan_symbol_t *afsym = get_field_sym(local->type->sym, expr->binary.left->member.name);
                            if (afsym && afsym->type && is_rc_managed_type(afsym->type)) {
                                emit_rc_store_field(g, afsym->type, fptr, right, expr->binary.right, locals,
                                                    (afsym->modifiers & MOD_WEAK) ? 1 : 0);
                            } else {
                                LLVMBuildStore(g->builder, right, fptr);
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
                            LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st,
                                obj_val, (unsigned)fi, "gfld");
                            zan_symbol_t *gfsym = get_field_sym(cls, expr->binary.left->member.name);
                            if (gfsym && gfsym->type && is_rc_managed_type(gfsym->type)) {
                                emit_rc_store_field(g, gfsym->type, fptr, right, expr->binary.right, locals,
                                                    (gfsym->modifiers & MOD_WEAK) ? 1 : 0);
                            } else {
                                LLVMBuildStore(g->builder, right, fptr);
                            }
                        }
                    }
                }
            }
        }
        return right;
    }

    case AST_CALL: {
        /* Task.Spawn(<asyncCall>) — fire-and-forget: run an async call as an
         * independent coroutine WITHOUT awaiting it. Emits the callee's ramp
         * (heap frame) then schedules it on the cooperative driver with no
         * awaiter and without suspending the caller (contrast await, which
         * registers self as awaiter and suspends). This is the concurrency
         * primitive a server accept loop uses to handle each connection on its
         * own coroutine instead of serially. See docs/ASYNC_CPS_DESIGN.md. */
        if (is_call_to(expr, "Task", "Spawn") && expr->call.args.count == 1 &&
            expr->call.args.items[0]->kind == AST_CALL) {
            LLVMTypeRef sp_i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef sp_i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            zan_ast_node_t *sub_arg = expr->call.args.items[0];
            LLVMValueRef sub = emit_expr(g, sub_arg, locals);
            LLVMValueRef sub_resume = NULL;
            if (LLVMIsACallInst(sub)) {
                LLVMValueRef callee = LLVMGetCalledValue(sub);
                if (callee) {
                    size_t nl = 0;
                    const char *cn = LLVMGetValueName2(callee, &nl);
                    if (cn && nl > 0 && nl < 240) {
                        char rn[256];
                        memcpy(rn, cn, nl);
                        memcpy(rn + nl, "$resume", 8); /* includes NUL */
                        sub_resume = LLVMGetNamedFunction(g->mod, rn);
                    }
                }
            }
            if (sub_resume && LLVMGetTypeKind(LLVMTypeOf(sub)) == LLVMPointerTypeKind) {
                LLVMValueRef sub_i8 = LLVMBuildBitCast(g->builder, sub, sp_i8ptr, "spawn.sub");
                /* A spawned coroutine is detached: no caller awaits it, so
                 * nobody frees its heap frame when it completes. Install the
                 * reaper as its own awaiter step (awaiter = the frame itself, a
                 * non-null marker) so emit_async_complete's awaiter-wake path
                 * re-enqueues (frame, __zan_co_reap) and the driver frees the
                 * frame after the body finishes. */
                LLVMTypeRef hdr = g->co_header_type;
                LLVMBuildStore(g->builder, sub_i8,
                    LLVMBuildStructGEP2(g->builder, hdr, sub_i8, ASYNC_FRAME_AWAITER, "spawn.aw"));
                LLVMBuildStore(g->builder, get_co_reap_fn(g),
                    LLVMBuildStructGEP2(g->builder, hdr, sub_i8, ASYNC_FRAME_AWAITER_STEP, "spawn.aws"));
                LLVMValueRef sched_args[] = { sub_i8, sub_resume };
                zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
            }
            emit_release_owned_call_temp(g, sub_arg, sub, locals);
            return LLVMConstInt(sp_i64, 0, 0);
        }

        /* StringBuilder.Append(s) / StringBuilder.ToString() — growable byte
         * buffer with amortised O(1) append (capacity doubling). */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sbcallee = expr->call.callee;
            zan_istr_t sbm = sbcallee->member.name;
            zan_type_t *sbt = infer_expr_type(g, sbcallee->member.object, locals);
            if (sbt && sbt->name.len == 13 &&
                memcmp(sbt->name.str, "StringBuilder", 13) == 0) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef raw = emit_expr(g, sbcallee->member.object, locals);
                LLVMValueRef sbp = LLVMBuildBitCast(g->builder, raw,
                    LLVMPointerType(g->sb_struct_type, 0), "sbp");
                int sb_is_append = (sbm.len == 6 && memcmp(sbm.str, "Append", 6) == 0 &&
                                    expr->call.args.count == 1);
                int sb_is_appendline = (sbm.len == 10 && memcmp(sbm.str, "AppendLine", 10) == 0 &&
                                        expr->call.args.count <= 1);
                if (sb_is_append || sb_is_appendline) {
                    if (expr->call.args.count == 1) {
                        zan_ast_node_t *arg0 = expr->call.args.items[0];
                        LLVMValueRef v = emit_expr(g, arg0, locals);
                        LLVMValueRef s = emit_value_as_cstr(g, v);
                        LLVMValueRef slen = zan_call2(g->builder,
                            LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                            g->fn_strlen, &s, 1, "sblen");
                        emit_sb_append_bytes(g, sbp, s, slen);
                        if (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind)
                            emit_release_owned_call_temp(g, arg0, v, locals);
                    }
                    if (sb_is_appendline) {
                        zan_istr_t nl = { "\n", 1 };
                        LLVMValueRef nls = emit_string_literal_rc(g, nl);
                        emit_sb_append_bytes(g, sbp, nls, LLVMConstInt(i64, 1, 0));
                    }
                    return raw;
                }
                if (sbm.len == 8 && memcmp(sbm.str, "ToString", 8) == 0 &&
                    expr->call.args.count == 0) {
                    LLVMValueRef cptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 0, "sbcp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, cptr, "sbcv");
                    LLVMValueRef dptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 2, "sbdp");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, i8ptr, dptr, "sbdv");
                    LLVMValueRef bsz = LLVMBuildAdd(g->builder, count, LLVMConstInt(i64, 1, 0), "sbbsz");
                    LLVMValueRef buf = emit_string_alloc_rc(g, bsz);
                    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
                    if (!memcpy_fn) {
                        memcpy_fn = LLVMAddFunction(g->mod, "memcpy",
                            LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0));
                    }
                    zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0),
                        memcpy_fn, (LLVMValueRef[]){ buf, data, count }, 3, "");
                    LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, buf, &count, 1, "sbend");
                    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
                    return buf;
                }
            }
        }

        /* special-case Console.WriteLine */
        if (is_call_to(expr, "Console", "WriteLine") ||
            is_call_to(expr, "Console", "PrintLine")) {
            if (expr->call.args.count > 0) {
                zan_ast_node_t *arg_ast = expr->call.args.items[0];
                LLVMValueRef arg = emit_expr(g, arg_ast, locals);
                LLVMTypeRef arg_type = LLVMTypeOf(arg);

                if (LLVMGetTypeKind(arg_type) == LLVMPointerTypeKind) {
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0) },
                        1, 0);
                    zan_call2(g->builder, fn_type, g->rt_println, &arg, 1, "");
                } else if (LLVMGetTypeKind(arg_type) == LLVMDoubleTypeKind ||
                           LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind) {
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ LLVMDoubleTypeInContext(g->ctx) },
                        1, 0);
                    zan_call2(g->builder, fn_type, g->rt_print_double, &arg, 1, "");
                } else {
                    /* ensure integer arg is i64 for print_int */
                    arg = emit_widen_i64_for_print(g, arg);
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ LLVMInt64TypeInContext(g->ctx) },
                        1, 0);
                    LLVMValueRef print_fn = expr_is_ulong(g, arg_ast, locals)
                        ? g->rt_print_uint : g->rt_print_int;
                    zan_call2(g->builder, fn_type, print_fn, &arg, 1, "");
                }
                emit_release_owned_call_temp(g, arg_ast, arg, locals);
            }
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Console.Write (no newline) */
        if (is_call_to(expr, "Console", "Write")) {
            if (expr->call.args.count > 0) {
                zan_ast_node_t *arg_ast = expr->call.args.items[0];
                LLVMValueRef arg = emit_expr(g, arg_ast, locals);
                LLVMTypeRef arg_type = LLVMTypeOf(arg);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef printf_args[] = { i8ptr };
                LLVMTypeRef printf_type = LLVMFunctionType(
                    LLVMInt32TypeInContext(g->ctx), printf_args, 1, 1);
                LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");

                if (LLVMGetTypeKind(arg_type) == LLVMPointerTypeKind) {
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%s", "wfmt_s");
                    LLVMValueRef args[] = { fmt, arg };
                    zan_call2(g->builder, printf_type, printf_fn, args, 2, "");
                } else if (LLVMGetTypeKind(arg_type) == LLVMDoubleTypeKind ||
                           LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind) {
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%g", "wfmt_d");
                    LLVMValueRef dbl_arg = arg;
                    if (LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind)
                        dbl_arg = LLVMBuildFPExt(g->builder, arg, LLVMDoubleTypeInContext(g->ctx), "ext");
                    LLVMValueRef args[] = { fmt, dbl_arg };
                    zan_call2(g->builder, printf_type, printf_fn, args, 2, "");
                } else {
                    LLVMValueRef fmt = expr_is_ulong(g, arg_ast, locals)
                        ? LLVMBuildGlobalStringPtr(g->builder, "%llu", "wfmt_u")
                        : LLVMBuildGlobalStringPtr(g->builder, "%lld", "wfmt_i");
                    LLVMValueRef int_arg = emit_widen_i64_for_print(g, arg);
                    LLVMValueRef args[] = { fmt, int_arg };
                    zan_call2(g->builder, printf_type, printf_fn, args, 2, "");
                }
                emit_release_owned_call_temp(g, arg_ast, arg, locals);
            }
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Console.ReadLine() -> reads a line from stdin, returns i8* */
        if (is_call_to(expr, "Console", "ReadLine")) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            /* allocate 1024 byte buffer */
            LLVMValueRef buf_size = LLVMConstInt(i64, 1024, 0);
            LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
            /* declare fgets if needed */
            LLVMValueRef fgets_fn = LLVMGetNamedFunction(g->mod, "fgets");
            if (!fgets_fn) {
                LLVMTypeRef fgets_args[] = { i8ptr, LLVMInt32TypeInContext(g->ctx), i8ptr };
                LLVMTypeRef fgets_type = LLVMFunctionType(i8ptr, fgets_args, 3, 0);
                fgets_fn = LLVMAddFunction(g->mod, "fgets", fgets_type);
            }
            /* get stdin */
            LLVMValueRef stdin_fn = LLVMGetNamedFunction(g->mod, "__acrt_iob_func");
            if (!stdin_fn) {
                LLVMTypeRef iob_args[] = { LLVMInt32TypeInContext(g->ctx) };
                LLVMTypeRef iob_type = LLVMFunctionType(i8ptr, iob_args, 1, 0);
                stdin_fn = LLVMAddFunction(g->mod, "__acrt_iob_func", iob_type);
            }
            LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
            LLVMValueRef stdin_ptr = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ LLVMInt32TypeInContext(g->ctx) }, 1, 0),
                stdin_fn, &zero, 1, "stdin");
            LLVMValueRef sz = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1024, 0);
            LLVMValueRef fgets_args[] = { buf, sz, stdin_ptr };
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, LLVMInt32TypeInContext(g->ctx), i8ptr }, 3, 0),
                fgets_fn, fgets_args, 3, "");
            return buf;
        }

        /* ==== NativeMemory intrinsics ====
         * Raw off-heap memory access for binary IO (ByteBuffer, ZanDB pages,
         * network framing). Addresses travel as nint (i64); every operation
         * lowers to direct loads/stores or libc calls, so none of these values
         * ever enter the ARC string/object machinery. Multi-byte accessors are
         * little-endian and unaligned-safe (align 1). */
        {
            LLVMValueRef nm_out = NULL;
            if (emit_native_memory_call(g, expr, locals, &nm_out))
                return nm_out;
        }

        /* String.CompareOrdinal(a, b) → strcmp: byte-wise ordinal compare of
         * two NUL-terminated strings in one libc call. */
        if (is_call_to(expr, "String", "CompareOrdinal") && expr->call.args.count == 2) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            zan_ast_node_t *a_ast = expr->call.args.items[0];
            zan_ast_node_t *b_ast = expr->call.args.items[1];
            LLVMValueRef a = emit_expr(g, a_ast, locals);
            LLVMValueRef b = emit_expr(g, b_ast, locals);
            LLVMTypeRef strcmp_ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
            LLVMValueRef strcmp_fn = get_libc_fn(g, "strcmp", strcmp_ty);
            LLVMValueRef r = zan_call2(g->builder, strcmp_ty, strcmp_fn,
                (LLVMValueRef[]){ a, b }, 2, "ordcmp");
            emit_release_owned_call_temp(g, a_ast, a, locals);
            emit_release_owned_call_temp(g, b_ast, b, locals);
            return LLVMBuildSExt(g->builder, r, i64t, "ordcmp64");
        }

        /* Math.Sqrt(expr) → llvm.sqrt */
        if (is_call_to(expr, "Math", "Sqrt") && expr->call.args.count == 1) {
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            /* ensure arg is double */
            if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMFloatTypeKind) {
                arg = LLVMBuildFPExt(g->builder, arg, dbl, "ext");
            } else if (LLVMGetTypeKind(LLVMTypeOf(arg)) != LLVMDoubleTypeKind) {
                arg = LLVMBuildSIToFP(g->builder, arg, dbl, "tofp");
            }
            /* declare sqrt if needed */
            LLVMValueRef sqrt_fn = LLVMGetNamedFunction(g->mod, "sqrt");
            if (!sqrt_fn) {
                LLVMTypeRef sqrt_args[] = { dbl };
                LLVMTypeRef sqrt_type = LLVMFunctionType(dbl, sqrt_args, 1, 0);
                sqrt_fn = LLVMAddFunction(g->mod, "sqrt", sqrt_type);
            }
            LLVMTypeRef sqrt_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
            return zan_call2(g->builder, sqrt_type, sqrt_fn, &arg, 1, "sqrt");
        }

        /* Math.Sin/Cos/Tan/Log/Exp(expr) → libm call (double -> double) */
        {
            static const char *math1[] = { "Sin", "sin", "Cos", "cos", "Tan", "tan",
                                           "Log", "log", "Exp", "exp", NULL };
            for (int mi = 0; math1[mi]; mi += 2) {
                if (is_call_to(expr, "Math", math1[mi]) && expr->call.args.count == 1) {
                    LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                    if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMFloatTypeKind) {
                        arg = LLVMBuildFPExt(g->builder, arg, dbl, "ext");
                    } else if (LLVMGetTypeKind(LLVMTypeOf(arg)) != LLVMDoubleTypeKind) {
                        arg = LLVMBuildSIToFP(g->builder, arg, dbl, "tofp");
                    }
                    LLVMTypeRef fty = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
                    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, math1[mi + 1]);
                    if (!fn) fn = LLVMAddFunction(g->mod, math1[mi + 1], fty);
                    return zan_call2(g->builder, fty, fn, &arg, 1, math1[mi + 1]);
                }
            }
        }

        /* Math.Abs(expr) */
        if (is_call_to(expr, "Math", "Abs") && expr->call.args.count == 1) {
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef arg_type = LLVMTypeOf(arg);
            if (LLVMGetTypeKind(arg_type) == LLVMDoubleTypeKind ||
                LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind) {
                LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                if (LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind)
                    arg = LLVMBuildFPExt(g->builder, arg, dbl, "ext");
                LLVMValueRef fabs_fn = LLVMGetNamedFunction(g->mod, "fabs");
                if (!fabs_fn) {
                    LLVMTypeRef fabs_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
                    fabs_fn = LLVMAddFunction(g->mod, "fabs", fabs_type);
                }
                return zan_call2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0),
                    fabs_fn, &arg, 1, "fabs");
            } else {
                /* integer abs: (x ^ (x >> 31)) - (x >> 31) */
                LLVMValueRef shift = LLVMBuildAShr(g->builder, arg,
                    LLVMConstInt(LLVMTypeOf(arg), 31, 0), "sh");
                LLVMValueRef xor = LLVMBuildXor(g->builder, arg, shift, "xor");
                return LLVMBuildSub(g->builder, xor, shift, "abs");
            }
        }

        /* Math.Max(a, b) */
        if (is_call_to(expr, "Math", "Max") && expr->call.args.count == 2) {
            LLVMValueRef a = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef b = emit_expr(g, expr->call.args.items[1], locals);
            LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntSGT, a, b, "cmp");
            return LLVMBuildSelect(g->builder, cmp, a, b, "max");
        }

        /* Math.Min(a, b) */
        if (is_call_to(expr, "Math", "Min") && expr->call.args.count == 2) {
            LLVMValueRef a = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef b = emit_expr(g, expr->call.args.items[1], locals);
            LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntSLT, a, b, "cmp");
            return LLVMBuildSelect(g->builder, cmp, a, b, "min");
        }

        /* Math.Pow(base, exp) */
        if (is_call_to(expr, "Math", "Pow") && expr->call.args.count == 2) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMValueRef base_v = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef exp_v = emit_expr(g, expr->call.args.items[1], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(base_v)) == LLVMIntegerTypeKind)
                base_v = LLVMBuildSIToFP(g->builder, base_v, dbl, "tofp");
            if (LLVMGetTypeKind(LLVMTypeOf(exp_v)) == LLVMIntegerTypeKind)
                exp_v = LLVMBuildSIToFP(g->builder, exp_v, dbl, "tofp");
            LLVMValueRef pow_fn = LLVMGetNamedFunction(g->mod, "pow");
            if (!pow_fn) {
                LLVMTypeRef pow_args[] = { dbl, dbl };
                LLVMTypeRef pow_type = LLVMFunctionType(dbl, pow_args, 2, 0);
                pow_fn = LLVMAddFunction(g->mod, "pow", pow_type);
            }
            LLVMValueRef args[] = { base_v, exp_v };
            return zan_call2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl, dbl }, 2, 0),
                pow_fn, args, 2, "pow");
        }

        /* Math.Floor(x) */
        if (is_call_to(expr, "Math", "Floor") && expr->call.args.count == 1) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMIntegerTypeKind)
                arg = LLVMBuildSIToFP(g->builder, arg, dbl, "tofp");
            LLVMValueRef floor_fn = LLVMGetNamedFunction(g->mod, "floor");
            if (!floor_fn) {
                LLVMTypeRef floor_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
                floor_fn = LLVMAddFunction(g->mod, "floor", floor_type);
            }
            return zan_call2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0),
                floor_fn, &arg, 1, "floor");
        }

        /* Math.Ceiling(x) */
        if (is_call_to(expr, "Math", "Ceiling") && expr->call.args.count == 1) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMIntegerTypeKind)
                arg = LLVMBuildSIToFP(g->builder, arg, dbl, "tofp");
            LLVMValueRef ceil_fn = LLVMGetNamedFunction(g->mod, "ceil");
            if (!ceil_fn) {
                LLVMTypeRef ceil_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
                ceil_fn = LLVMAddFunction(g->mod, "ceil", ceil_type);
            }
            return zan_call2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0),
                ceil_fn, &arg, 1, "ceil");
        }

        /* Math.Round(x[, digits]) */
        if (is_call_to(expr, "Math", "Round") &&
            (expr->call.args.count == 1 || expr->call.args.count == 2)) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMTypeRef d1_ty = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMIntegerTypeKind)
                arg = LLVMBuildSIToFP(g->builder, arg, dbl, "tofp");
            else if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMFloatTypeKind)
                arg = LLVMBuildFPExt(g->builder, arg, dbl, "ext");
            LLVMValueRef round_fn = get_libc_fn(g, "round", d1_ty);
            if (expr->call.args.count == 1)
                return zan_call2(g->builder, d1_ty, round_fn, &arg, 1, "round");
            /* digits: round(x * 10^d) / 10^d */
            LLVMValueRef digits = emit_expr(g, expr->call.args.items[1], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(digits)) == LLVMIntegerTypeKind)
                digits = LLVMBuildSIToFP(g->builder, digits, dbl, "dtofp");
            LLVMTypeRef d2_ty = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl, dbl }, 2, 0);
            LLVMValueRef pow_fn = get_libc_fn(g, "pow", d2_ty);
            LLVMValueRef scale = zan_call2(g->builder, d2_ty, pow_fn,
                (LLVMValueRef[]){ LLVMConstReal(dbl, 10.0), digits }, 2, "scale");
            LLVMValueRef scaled = LLVMBuildFMul(g->builder, arg, scale, "scaled");
            LLVMValueRef r = zan_call2(g->builder, d1_ty, round_fn, &scaled, 1, "round");
            return LLVMBuildFDiv(g->builder, r, scale, "rdig");
        }

        /* Convert.ToDouble(x) */
        if (is_call_to(expr, "Convert", "ToDouble") && expr->call.args.count == 1) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeKind ak = LLVMGetTypeKind(LLVMTypeOf(arg));
            if (ak == LLVMPointerTypeKind) {
                LLVMTypeRef strtod_ty = LLVMFunctionType(dbl,
                    (LLVMTypeRef[]){ i8ptr, LLVMPointerType(i8ptr, 0) }, 2, 0);
                LLVMValueRef strtod_fn = get_libc_fn(g, "strtod", strtod_ty);
                LLVMValueRef r = zan_call2(g->builder, strtod_ty, strtod_fn,
                    (LLVMValueRef[]){ arg, LLVMConstNull(LLVMPointerType(i8ptr, 0)) }, 2, "todbl");
                emit_release_owned_call_temp(g, expr->call.args.items[0], arg, locals);
                return r;
            }
            if (ak == LLVMIntegerTypeKind)
                return LLVMBuildSIToFP(g->builder, arg, dbl, "todbl");
            if (ak == LLVMFloatTypeKind)
                return LLVMBuildFPExt(g->builder, arg, dbl, "todbl");
            return arg;
        }

        /* int/long/Int32/Int64.Parse(s) and double/float/Double.Parse(s);
         * matching TryParse(s, out v) returning bool. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->call.callee->member.object->ident.name)) {
            zan_istr_t rn = expr->call.callee->member.object->ident.name;
            zan_istr_t pm = expr->call.callee->member.name;
            int int_recv = (rn.len == 3 && memcmp(rn.str, "int", 3) == 0) ||
                           (rn.len == 4 && memcmp(rn.str, "long", 4) == 0) ||
                           (rn.len == 5 && memcmp(rn.str, "Int32", 5) == 0) ||
                           (rn.len == 5 && memcmp(rn.str, "Int64", 5) == 0);
            int dbl_recv = (rn.len == 6 && memcmp(rn.str, "double", 6) == 0) ||
                           (rn.len == 5 && memcmp(rn.str, "float", 5) == 0) ||
                           (rn.len == 6 && memcmp(rn.str, "Double", 6) == 0) ||
                           (rn.len == 6 && memcmp(rn.str, "Single", 6) == 0);
            if ((int_recv || dbl_recv) &&
                pm.len == 5 && memcmp(pm.str, "Parse", 5) == 0 &&
                expr->call.args.count == 1) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef r;
                if (int_recv) {
                    LLVMTypeRef strtoll_ty = LLVMFunctionType(i64,
                        (LLVMTypeRef[]){ i8ptr, i8pp, i32t }, 3, 0);
                    LLVMValueRef f = get_libc_fn(g, "strtoll", strtoll_ty);
                    r = zan_call2(g->builder, strtoll_ty, f,
                        (LLVMValueRef[]){ s, LLVMConstNull(i8pp),
                                          LLVMConstInt(i32t, 10, 0) }, 3, "parse");
                } else {
                    LLVMTypeRef strtod_ty = LLVMFunctionType(dbl,
                        (LLVMTypeRef[]){ i8ptr, i8pp }, 2, 0);
                    LLVMValueRef f = get_libc_fn(g, "strtod", strtod_ty);
                    r = zan_call2(g->builder, strtod_ty, f,
                        (LLVMValueRef[]){ s, LLVMConstNull(i8pp) }, 2, "parse");
                }
                emit_release_owned_call_temp(g, expr->call.args.items[0], s, locals);
                return r;
            }
            if ((int_recv || dbl_recv) &&
                pm.len == 8 && memcmp(pm.str, "TryParse", 8) == 0 &&
                expr->call.args.count == 2) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef outp = emit_expr(g, expr->call.args.items[1], locals);
                LLVMValueRef endp = emit_entry_alloca(g, i8ptr, "endp");
                LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), endp);
                LLVMValueRef v;
                if (int_recv) {
                    LLVMTypeRef strtoll_ty = LLVMFunctionType(i64,
                        (LLVMTypeRef[]){ i8ptr, i8pp, i32t }, 3, 0);
                    LLVMValueRef f = get_libc_fn(g, "strtoll", strtoll_ty);
                    v = zan_call2(g->builder, strtoll_ty, f,
                        (LLVMValueRef[]){ s, endp, LLVMConstInt(i32t, 10, 0) }, 3, "tp");
                } else {
                    LLVMTypeRef strtod_ty = LLVMFunctionType(dbl,
                        (LLVMTypeRef[]){ i8ptr, i8pp }, 2, 0);
                    LLVMValueRef f = get_libc_fn(g, "strtod", strtod_ty);
                    v = zan_call2(g->builder, strtod_ty, f,
                        (LLVMValueRef[]){ s, endp }, 2, "tp");
                }
                LLVMValueRef e = LLVMBuildLoad2(g->builder, i8ptr, endp, "e");
                LLVMValueRef moved = LLVMBuildICmp(g->builder, LLVMIntNE, e, s, "moved");
                LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8, e, "ch");
                LLVMValueRef at_end = LLVMBuildICmp(g->builder, LLVMIntEQ, ch,
                    LLVMConstInt(i8, 0, 0), "atend");
                LLVMValueRef ok = LLVMBuildAnd(g->builder, moved, at_end, "ok");
                LLVMValueRef stored = LLVMBuildSelect(g->builder, ok, v,
                    int_recv ? LLVMConstInt(i64, 0, 0) : (LLVMValueRef)LLVMConstReal(dbl, 0.0), "tpv");
                if (LLVMGetTypeKind(LLVMTypeOf(outp)) == LLVMPointerTypeKind)
                    LLVMBuildStore(g->builder, stored, outp);
                emit_release_owned_call_temp(g, expr->call.args.items[0], s, locals);
                return ok;
            }
        }

        /* string.IsNullOrEmpty / string.Join / string.Format statics. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->call.callee->member.object->ident.name)) {
            zan_istr_t rn = expr->call.callee->member.object->ident.name;
            int str_recv = (rn.len == 6 && memcmp(rn.str, "string", 6) == 0) ||
                           (rn.len == 6 && memcmp(rn.str, "String", 6) == 0);
            zan_istr_t pm = expr->call.callee->member.name;
            if (str_recv && pm.len == 13 &&
                memcmp(pm.str, "IsNullOrEmpty", 13) == 0 &&
                expr->call.args.count == 1) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMValueRef s = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef isnull = LLVMBuildICmp(g->builder, LLVMIntEQ, s,
                    LLVMConstNull(i8ptr), "isnull");
                /* empty check must not deref null: select null ? true : *s==0 */
                LLVMBasicBlockRef chk_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sne.chk");
                LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sne.done");
                LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
                LLVMBuildCondBr(g->builder, isnull, done_bb, chk_bb);
                LLVMPositionBuilderAtEnd(g->builder, chk_bb);
                LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8, s, "ch");
                LLVMValueRef isempty = LLVMBuildICmp(g->builder, LLVMIntEQ, ch,
                    LLVMConstInt(i8, 0, 0), "isempty");
                LLVMBasicBlockRef chk_end = LLVMGetInsertBlock(g->builder);
                LLVMBuildBr(g->builder, done_bb);
                LLVMPositionBuilderAtEnd(g->builder, done_bb);
                LLVMValueRef phi = LLVMBuildPhi(g->builder, LLVMInt1TypeInContext(g->ctx), "sne");
                LLVMValueRef inv[2] = { LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0), isempty };
                LLVMBasicBlockRef inb[2] = { cur_bb, chk_end };
                LLVMAddIncoming(phi, inv, inb, 2);
                emit_release_owned_call_temp(g, expr->call.args.items[0], s, locals);
                return phi;
            }
            if (str_recv && pm.len == 4 && memcmp(pm.str, "Join", 4) == 0 &&
                expr->call.args.count == 2) {
                LLVMValueRef sep = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef lst = emit_expr(g, expr->call.args.items[1], locals);
                LLVMValueRef hf = get_str_join_fn(g);
                LLVMValueRef res = zan_call2(g->builder,
                    LLVMGlobalGetValueType(hf), hf,
                    (LLVMValueRef[]){ sep, lst }, 2, "join");
                emit_release_owned_call_temp(g, expr->call.args.items[0], sep, locals);
                emit_release_owned_call_temp(g, expr->call.args.items[1], lst, locals);
                return res;
            }
            if (str_recv && pm.len == 6 && memcmp(pm.str, "Format", 6) == 0 &&
                expr->call.args.count >= 1 &&
                expr->call.args.items[0]->kind == AST_STRING_LITERAL) {
                /* compile-time expansion of a literal format: split on {N}
                 * placeholders and concatenate pieces with stringified args. */
                zan_istr_t f = expr->call.args.items[0]->str_val;
                LLVMValueRef res = NULL;
                int seg_start = 0;
                for (int i = 0; i < f.len; i++) {
                    if (f.str[i] == '{' && i + 2 < f.len + 1) {
                        int j = i + 1, idx = 0, has = 0;
                        while (j < f.len && f.str[j] >= '0' && f.str[j] <= '9') {
                            idx = idx * 10 + (f.str[j] - '0'); j++; has = 1;
                        }
                        if (has && j < f.len && f.str[j] == '}' &&
                            idx + 1 < expr->call.args.count) {
                            if (i > seg_start) {
                                zan_istr_t seg = { f.str + seg_start, i - seg_start };
                                LLVMValueRef sl = emit_string_literal_rc(g, seg);
                                res = res ? emit_str_concat(g, res, sl) : sl;
                            }
                            LLVMValueRef av = emit_expr(g,
                                expr->call.args.items[idx + 1], locals);
                            LLVMValueRef as = emit_to_cstr(g, av);
                            res = res ? emit_str_concat(g, res, as) : as;
                            i = j;
                            seg_start = j + 1;
                        }
                    }
                }
                if (seg_start < f.len || !res) {
                    zan_istr_t seg = { f.str + seg_start, f.len - seg_start };
                    LLVMValueRef sl = emit_string_literal_rc(g, seg);
                    res = res ? emit_str_concat(g, res, sl) : sl;
                }
                return res;
            }
        }

        /* String methods: str.Substring(start, len), str.Contains(sub), str.IndexOf(ch) */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            
            /* Check if calling on a local variable of string type */
            if (callee->member.object->kind == AST_IDENTIFIER) {
                local_var_t *str_local = local_find(locals, callee->member.object->ident.name);
                if (str_local && LLVMGetTypeKind(LLVMTypeOf(LLVMBuildLoad2(g->builder,
                    LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0), str_local->alloca, "chk"))) == LLVMPointerTypeKind) {
                    /* It's a pointer — might be a string */
                    /* DON'T emit load twice; skip for now, let it fall through to general handler */
                }
            }
            
            /* Convert.ToInt32(str) -> atoi */
            if (callee->member.object->kind == AST_IDENTIFIER) {
                zan_istr_t obj_name = callee->member.object->ident.name;
                if (obj_name.len == 7 && memcmp(obj_name.str, "Convert", 7) == 0) {
                    if (((method_name.len == 7 && memcmp(method_name.str, "ToInt32", 7) == 0) ||
                         (method_name.len == 5 && memcmp(method_name.str, "ToInt", 5) == 0)) &&
                        expr->call.args.count == 1) {
                        LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
                        LLVMValueRef atoi_fn = LLVMGetNamedFunction(g->mod, "atoi");
                        LLVMTypeRef atoi_type;
                        if (!atoi_fn) {
                            atoi_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                            atoi_fn = LLVMAddFunction(g->mod, "atoi", atoi_type);
                        } else {
                            /* a stdlib `static extern int atoi(...)` may have
                             * declared it already (with an i64 return); call
                             * through the existing declaration's type */
                            atoi_type = LLVMGlobalGetValueType(atoi_fn);
                        }
                        LLVMValueRef parsed = zan_call2(g->builder,
                            atoi_type, atoi_fn, &arg, 1, "atoi");
                        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                        if (LLVMTypeOf(parsed) != i64t &&
                            LLVMGetTypeKind(LLVMTypeOf(parsed)) == LLVMIntegerTypeKind)
                            parsed = LLVMBuildSExt(g->builder, parsed, i64t, "atoi64");
                        emit_release_owned_call_temp(
                            g, expr->call.args.items[0], arg, locals);
                        return parsed;
                    }
                    if (method_name.len == 8 && memcmp(method_name.str, "ToString", 8) == 0 &&
                        expr->call.args.count == 1) {
                        LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        /* allocate buffer and sprintf */
                        LLVMValueRef buf_size = LLVMConstInt(i64, 32, 0);
                        LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
                        LLVMTypeKind atk = LLVMGetTypeKind(LLVMTypeOf(arg));
                        LLVMValueRef fmt;
                        LLVMValueRef num_arg = arg;
                        if (atk == LLVMDoubleTypeKind || atk == LLVMFloatTypeKind) {
                            /* floating point: print with %g (varargs promote
                             * float to double), matching Console.WriteLine. */
                            fmt = LLVMBuildGlobalStringPtr(g->builder, "%g", "ftoa_fmt");
                            if (atk == LLVMFloatTypeKind) {
                                num_arg = LLVMBuildFPExt(g->builder, arg,
                                    LLVMDoubleTypeInContext(g->ctx), "ext");
                            }
                        } else {
                            fmt = LLVMBuildGlobalStringPtr(g->builder, "%lld", "itoa_fmt");
                            num_arg = emit_widen_i64_for_print(g, arg);
                        }
                        LLVMValueRef sn_args[] = { buf, LLVMConstInt(i64, 32, 0), fmt, num_arg };
                        zan_call2(g->builder,
                            LLVMFunctionType(LLVMInt32TypeInContext(g->ctx),
                                (LLVMTypeRef[]){ i8ptr, i64, i8ptr }, 3, 1),
                            g->fn_snprintf, sn_args, 4, "");
                        return buf;
                    }
                }
            }
        }

        /* str.Substring(start[, len]) -> heap-allocated copy of the slice.
         * With one argument, copies from `start` to the end of the string. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 9 && memcmp(sm.str, "Substring", 9) == 0 &&
                (expr->call.args.count == 1 || expr->call.args.count == 2) &&
                is_string_expr(g, sc->member.object, locals)) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef start = coerce_int_to(g,
                    emit_expr(g, expr->call.args.items[0], locals), i64);
                LLVMValueRef slen;
                if (expr->call.args.count == 2) {
                    slen = coerce_int_to(g,
                        emit_expr(g, expr->call.args.items[1], locals), i64);
                } else {
                    LLVMValueRef total = zan_call2(g->builder,
                        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                        g->fn_strlen, &s, 1, "slen");
                    slen = LLVMBuildSub(g->builder, total, start, "subl");
                }
                LLVMValueRef bufsz = LLVMBuildAdd(g->builder, slen, LLVMConstInt(i64, 1, 0), "bsz");
                LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
                LLVMValueRef srcp = LLVMBuildGEP2(g->builder, i8, s, &start, 1, "srcp");
                LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
                if (!memcpy_fn) {
                    memcpy_fn = LLVMAddFunction(g->mod, "memcpy",
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0));
                }
                LLVMValueRef mcargs[] = { buf, srcp, slen };
                zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0),
                    memcpy_fn, mcargs, 3, "");
                LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, buf, &slen, 1, "endp");
                LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                return buf;
            }
        }

        /* str.Contains(sub) -> bool: substring search via strstr. Without
         * this, the call fell through to the generic fallback and lowered to
         * constant false for multi-character needles. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 8 && memcmp(sm.str, "Contains", 8) == 0 &&
                expr->call.args.count == 1 &&
                is_string_expr(g, sc->member.object, locals)) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef sub = emit_expr(g, expr->call.args.items[0], locals);
                LLVMTypeRef strstr_type = LLVMFunctionType(i8ptr,
                    (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                LLVMValueRef strstr_fn = LLVMGetNamedFunction(g->mod, "strstr");
                if (!strstr_fn)
                    strstr_fn = LLVMAddFunction(g->mod, "strstr", strstr_type);
                LLVMValueRef ss_args[] = { s, sub };
                LLVMValueRef hit = zan_call2(g->builder, strstr_type,
                    strstr_fn, ss_args, 2, "ss");
                LLVMValueRef res = LLVMBuildICmp(g->builder, LLVMIntNE, hit,
                    LLVMConstPointerNull(i8ptr), "ctn");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                emit_release_owned_call_temp(g, expr->call.args.items[0], sub, locals);
                return res;
            }
        }

        /* str.IndexOf(sub) / str.LastIndexOf(sub) -> i64 byte index or -1. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            int is_idx = (sm.len == 7 && memcmp(sm.str, "IndexOf", 7) == 0);
            int is_lidx = (sm.len == 11 && memcmp(sm.str, "LastIndexOf", 11) == 0);
            if ((is_idx || is_lidx) && expr->call.args.count == 1 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_expr(g, expr->call.args.items[0], locals)) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef sub = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef res;
                if (is_idx) {
                    LLVMTypeRef strstr_type = LLVMFunctionType(i8ptr,
                        (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                    LLVMValueRef strstr_fn = get_libc_fn(g, "strstr", strstr_type);
                    LLVMValueRef ss_args[] = { s, sub };
                    LLVMValueRef hit = zan_call2(g->builder, strstr_type,
                        strstr_fn, ss_args, 2, "ss");
                    LLVMValueRef diff = LLVMBuildSub(g->builder,
                        LLVMBuildPtrToInt(g->builder, hit, i64, "hi"),
                        LLVMBuildPtrToInt(g->builder, s, i64, "si"), "diff");
                    LLVMValueRef missed = LLVMBuildICmp(g->builder, LLVMIntEQ, hit,
                        LLVMConstPointerNull(i8ptr), "missed");
                    res = LLVMBuildSelect(g->builder, missed,
                        LLVMConstInt(i64, (uint64_t)-1, 1), diff, "idx");
                } else {
                    LLVMValueRef lif = get_str_last_index_of_fn(g);
                    LLVMValueRef li_args[] = { s, sub };
                    res = zan_call2(g->builder, LLVMGlobalGetValueType(lif),
                        lif, li_args, 2, "lidx");
                }
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                emit_release_owned_call_temp(g, expr->call.args.items[0], sub, locals);
                return res;
            }
        }

        /* str.StartsWith(p) / str.EndsWith(p) -> bool. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            int is_sw = (sm.len == 10 && memcmp(sm.str, "StartsWith", 10) == 0);
            int is_ew = (sm.len == 8 && memcmp(sm.str, "EndsWith", 8) == 0);
            if ((is_sw || is_ew) && expr->call.args.count == 1 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_expr(g, expr->call.args.items[0], locals)) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef p = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef lp = zan_call2(g->builder, strlen_ty, g->fn_strlen, &p, 1, "lp");
                LLVMValueRef res;
                if (is_sw) {
                    LLVMTypeRef strncmp_ty = LLVMFunctionType(i32,
                        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
                    LLVMValueRef strncmp_fn = get_libc_fn(g, "strncmp", strncmp_ty);
                    LLVMValueRef nc_args[] = { s, p, lp };
                    LLVMValueRef cmp = zan_call2(g->builder, strncmp_ty,
                        strncmp_fn, nc_args, 3, "swcmp");
                    res = LLVMBuildICmp(g->builder, LLVMIntEQ, cmp,
                        LLVMConstInt(i32, 0, 0), "sw");
                } else {
                    LLVMValueRef ls = zan_call2(g->builder, strlen_ty, g->fn_strlen, &s, 1, "ls");
                    LLVMValueRef off = LLVMBuildSub(g->builder, ls, lp, "off");
                    LLVMValueRef fits = LLVMBuildICmp(g->builder, LLVMIntSGE, off,
                        LLVMConstInt(i64, 0, 0), "fits");
                    LLVMValueRef offc = LLVMBuildSelect(g->builder, fits, off,
                        LLVMConstInt(i64, 0, 0), "offc");
                    LLVMValueRef tail = LLVMBuildGEP2(g->builder, i8, s, &offc, 1, "tailp");
                    LLVMTypeRef strcmp_ty = LLVMFunctionType(i32,
                        (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                    LLVMValueRef strcmp_fn = get_libc_fn(g, "strcmp", strcmp_ty);
                    LLVMValueRef sc_args[] = { tail, p };
                    LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty,
                        strcmp_fn, sc_args, 2, "ewcmp");
                    LLVMValueRef eq = LLVMBuildICmp(g->builder, LLVMIntEQ, cmp,
                        LLVMConstInt(i32, 0, 0), "eweq");
                    res = LLVMBuildAnd(g->builder, fits, eq, "ew");
                }
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                emit_release_owned_call_temp(g, expr->call.args.items[0], p, locals);
                return res;
            }
        }

        /* str.Replace(from, to) -> fresh rc string. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 7 && memcmp(sm.str, "Replace", 7) == 0 &&
                expr->call.args.count == 2 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_expr(g, expr->call.args.items[0], locals) &&
                is_string_expr(g, expr->call.args.items[1], locals)) {
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef from = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef to = emit_expr(g, expr->call.args.items[1], locals);
                LLVMValueRef rf = get_str_replace_fn(g);
                LLVMValueRef rargs[] = { s, from, to };
                LLVMValueRef res = zan_call2(g->builder,
                    LLVMGlobalGetValueType(rf), rf, rargs, 3, "repl");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                emit_release_owned_call_temp(g, expr->call.args.items[0], from, locals);
                emit_release_owned_call_temp(g, expr->call.args.items[1], to, locals);
                return res;
            }
        }

        /* str.Trim() / str.ToUpper() / str.ToLower() -> fresh rc string. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 0) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            int is_trim = (sm.len == 4 && memcmp(sm.str, "Trim", 4) == 0);
            int is_up = (sm.len == 7 && memcmp(sm.str, "ToUpper", 7) == 0);
            int is_low = (sm.len == 7 && memcmp(sm.str, "ToLower", 7) == 0);
            if ((is_trim || is_up || is_low) &&
                is_string_expr(g, sc->member.object, locals)) {
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef hf = is_trim ? get_str_trim_fn(g)
                                          : get_str_case_fn(g, is_up);
                LLVMValueRef res = zan_call2(g->builder,
                    LLVMGlobalGetValueType(hf), hf, &s, 1, "strh");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                return res;
            }
        }

        /* str.Split(sep) -> fresh List<string> of the separated segments. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 1) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 5 && memcmp(sm.str, "Split", 5) == 0 &&
                is_string_expr(g, sc->member.object, locals)) {
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef sep = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef lst = emit_alloc_rc_collection(g, expr, 24, 1,
                                                            g->binder->type_string);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef lp = LLVMBuildBitCast(g->builder, lst,
                    LLVMPointerType(g->list_struct_type, 0), "lp");
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0),
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "cnt"));
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0),
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 1, "cap"));
                LLVMBuildStore(g->builder, LLVMConstNull(LLVMPointerType(i64, 0)),
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "df"));
                LLVMValueRef hf = get_str_split_fn(g);
                zan_call2(g->builder, LLVMGlobalGetValueType(hf), hf,
                    (LLVMValueRef[]){ s, sep, lst }, 3, "");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                emit_release_owned_call_temp(g, expr->call.args.items[0], sep, locals);
                return lst;
            }
        }

        /* value.ToString() on a scalar (int/float/bool/char/enum) or string
         * receiver. Class/struct receivers fall through to normal method
         * dispatch. Previously these calls lowered to the constant-0 fallback,
         * so e.g. `n.ToString()` always produced "0". */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 0) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 8 && memcmp(sm.str, "ToString", 8) == 0) {
                zan_type_t *rt_ty = infer_expr_type(g, sc->member.object, locals);
                int handled_kind = rt_ty &&
                    (rt_ty->kind == TYPE_BOOL || rt_ty->kind == TYPE_BYTE ||
                     rt_ty->kind == TYPE_SHORT || rt_ty->kind == TYPE_INT ||
                     rt_ty->kind == TYPE_LONG || rt_ty->kind == TYPE_SBYTE ||
                     rt_ty->kind == TYPE_USHORT || rt_ty->kind == TYPE_UINT ||
                     rt_ty->kind == TYPE_ULONG || rt_ty->kind == TYPE_FLOAT ||
                     rt_ty->kind == TYPE_DOUBLE || rt_ty->kind == TYPE_CHAR ||
                     rt_ty->kind == TYPE_STRING || rt_ty->kind == TYPE_ENUM);
                if (handled_kind) {
                    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef v = emit_expr(g, sc->member.object, locals);
                    LLVMTypeKind vk = LLVMGetTypeKind(LLVMTypeOf(v));
                    if (rt_ty->kind == TYPE_BOOL && vk == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(v)) == 1) {
                        LLVMValueRef t = emit_string_literal_rc(g,
                            (zan_istr_t){ "true", 4 });
                        LLVMValueRef f = emit_string_literal_rc(g,
                            (zan_istr_t){ "false", 5 });
                        return LLVMBuildSelect(g->builder, v, t, f, "bstr");
                    }
                    if (vk == LLVMPointerTypeKind) {
                        /* string receiver: return an owned copy */
                        LLVMTypeRef strlen_ty = LLVMFunctionType(i64,
                            (LLVMTypeRef[]){ i8ptr }, 1, 0);
                        LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr,
                            (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
                        LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);
                        LLVMValueRef n = zan_call2(g->builder, strlen_ty,
                            g->fn_strlen, &v, 1, "n");
                        LLVMValueRef bufsz = LLVMBuildAdd(g->builder, n,
                            LLVMConstInt(i64, 1, 0), "bsz");
                        LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
                        LLVMValueRef mcargs[] = { buf, v, bufsz };
                        zan_call2(g->builder, memcpy_ty, memcpy_fn, mcargs, 3, "");
                        emit_release_owned_call_temp(g, sc->member.object, v, locals);
                        return buf;
                    }
                    /* numeric: format like Convert.ToString */
                    LLVMValueRef buf = emit_string_alloc_rc(g,
                        LLVMConstInt(i64, 32, 0));
                    LLVMValueRef fmt;
                    LLVMValueRef num_arg = v;
                    if (vk == LLVMDoubleTypeKind || vk == LLVMFloatTypeKind) {
                        fmt = LLVMBuildGlobalStringPtr(g->builder, "%g", "ftoa_fmt");
                        if (vk == LLVMFloatTypeKind) {
                            num_arg = LLVMBuildFPExt(g->builder, v,
                                LLVMDoubleTypeInContext(g->ctx), "ext");
                        }
                    } else if (rt_ty->kind == TYPE_ULONG) {
                        fmt = LLVMBuildGlobalStringPtr(g->builder, "%llu", "utoa_fmt");
                        num_arg = emit_widen_i64_for_print(g, v);
                    } else {
                        fmt = LLVMBuildGlobalStringPtr(g->builder, "%lld", "itoa_fmt");
                        num_arg = emit_widen_i64_for_print(g, v);
                    }
                    LLVMValueRef sn_args[] = { buf, LLVMConstInt(i64, 32, 0), fmt, num_arg };
                    zan_call2(g->builder,
                        LLVMFunctionType(LLVMInt32TypeInContext(g->ctx),
                            (LLVMTypeRef[]){ i8ptr, i64, i8ptr }, 3, 1),
                        g->fn_snprintf, sn_args, 4, "");
                    return buf;
                }
            }
        }

        /* Environment.ArgCount() -> number of command-line args (excludes the
         * program name), i.e. argc - 1. */
        if (is_call_to(expr, "Environment", "ArgCount") && expr->call.args.count == 0) {
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef g_argc = LLVMGetNamedGlobal(g->mod, "__zan_argc");
            if (!g_argc) {
                g_argc = LLVMAddGlobal(g->mod, i32, "__zan_argc");
                LLVMSetInitializer(g_argc, LLVMConstInt(i32, 0, 0));
            }
            LLVMValueRef ac = LLVMBuildLoad2(g->builder, i32, g_argc, "argc");
            LLVMValueRef ac64 = LLVMBuildSExt(g->builder, ac, i64, "argc64");
            return LLVMBuildSub(g->builder, ac64, LLVMConstInt(i64, 1, 0), "nargs");
        }

        /* Environment.ArgAt(i) -> string : the (i+1)-th argv entry, so index 0
         * is the first user argument. */
        if (is_call_to(expr, "Environment", "ArgAt") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i8ptrptr = LLVMPointerType(i8ptr, 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            zan_ast_node_t *idx_ast = expr->call.args.items[0];
            LLVMValueRef idx = emit_expr(g, idx_ast, locals);
            LLVMValueRef idx1 = LLVMBuildAdd(g->builder, idx, LLVMConstInt(i64, 1, 0), "argi");
            LLVMValueRef g_argv = LLVMGetNamedGlobal(g->mod, "__zan_argv");
            if (!g_argv) {
                g_argv = LLVMAddGlobal(g->mod, i8ptrptr, "__zan_argv");
                LLVMSetInitializer(g_argv, LLVMConstNull(i8ptrptr));
            }
            LLVMValueRef argv = LLVMBuildLoad2(g->builder, i8ptrptr, g_argv, "argv");
            LLVMValueRef slot = LLVMBuildGEP2(g->builder, i8ptr, argv, &idx1, 1, "argslot");
            emit_release_owned_call_temp(g, idx_ast, idx, locals);
            return LLVMBuildLoad2(g->builder, i8ptr, slot, "arg");
        }

        /* File.ReadAllText(path) -> string */
        if (is_call_to(expr, "File", "ReadAllText") && expr->call.args.count == 1) {
            zan_ast_node_t *path_ast = expr->call.args.items[0];
            LLVMValueRef path_arg = emit_expr(g, path_ast, locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            /* declare fopen */
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef fopen_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", fopen_type);
            }
            /* declare fseek, ftell, fread, fclose */
            LLVMValueRef fseek_fn = LLVMGetNamedFunction(g->mod, "fseek");
            if (!fseek_fn) {
                LLVMTypeRef fseek_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0);
                fseek_fn = LLVMAddFunction(g->mod, "fseek", fseek_type);
            }
            LLVMValueRef ftell_fn = LLVMGetNamedFunction(g->mod, "ftell");
            if (!ftell_fn) {
                LLVMTypeRef ftell_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                ftell_fn = LLVMAddFunction(g->mod, "ftell", ftell_type);
            }
            LLVMValueRef fread_fn = LLVMGetNamedFunction(g->mod, "fread");
            if (!fread_fn) {
                LLVMTypeRef fread_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0);
                fread_fn = LLVMAddFunction(g->mod, "fread", fread_type);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef fclose_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", fclose_type);
            }
            /* open file */
            LLVMValueRef mode = LLVMBuildGlobalStringPtr(g->builder, "rb", "rb");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            emit_fopen_check(g, fp, "cannot read file\n");
            /* seek to end, get size */
            LLVMValueRef seek_end_args[] = { fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 2, 0) };
            LLVMValueRef se_end = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_end_args, 3, "se_end");
            emit_io_abort_if(g, LLVMBuildICmp(g->builder, LLVMIntNE, se_end, LLVMConstInt(i32, 0, 0), "se_end.err"),
                "cannot read file\n");
            LLVMValueRef size = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), ftell_fn, &fp, 1, "sz");
            emit_io_abort_if(g, LLVMBuildICmp(g->builder, LLVMIntSLT, size, LLVMConstInt(i64, 0, 0), "sz.err"),
                "cannot read file\n");
            /* seek back to start */
            LLVMValueRef seek_start_args[] = { fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 0, 0) };
            LLVMValueRef se_start = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_start_args, 3, "se_start");
            emit_io_abort_if(g, LLVMBuildICmp(g->builder, LLVMIntNE, se_start, LLVMConstInt(i32, 0, 0), "se_start.err"),
                "cannot read file\n");
            /* allocate buffer (size+1 for null terminator) */
            LLVMValueRef buf_size = LLVMBuildAdd(g->builder, size, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
            /* read file; require the full byte count */
            LLVMValueRef fread_args[] = { buf, LLVMConstInt(i64, 1, 0), size, fp };
            LLVMValueRef got = zan_call2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fread_fn, fread_args, 4, "got");
            emit_io_abort_if(g, LLVMBuildICmp(g->builder, LLVMIntNE, got, size, "got.err"),
                "cannot read file\n");
            /* null terminate */
            LLVMValueRef end_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), buf, &size, 1, "end");
            LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0), end_ptr);
            /* close */
            LLVMValueRef rd_close = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "rd_close");
            emit_io_abort_if(g, LLVMBuildICmp(g->builder, LLVMIntNE, rd_close, LLVMConstInt(i32, 0, 0), "rd_close.err"),
                "cannot read file\n");
            emit_release_owned_call_temp(g, path_ast, path_arg, locals);
            return buf;
        }

        /* File.WriteAllText(path, content) */
        if (is_call_to(expr, "File", "WriteAllText") && expr->call.args.count == 2) {
            zan_ast_node_t *path_ast = expr->call.args.items[0];
            zan_ast_node_t *content_ast = expr->call.args.items[1];
            LLVMValueRef path_arg = emit_expr(g, path_ast, locals);
            LLVMValueRef content_arg = emit_expr(g, content_ast, locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef fopen_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", fopen_type);
            }
            LLVMValueRef fputs_fn = LLVMGetNamedFunction(g->mod, "fputs");
            if (!fputs_fn) {
                LLVMTypeRef fputs_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fputs_fn = LLVMAddFunction(g->mod, "fputs", fputs_type);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef fclose_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", fclose_type);
            }
            LLVMValueRef mode = LLVMBuildGlobalStringPtr(g->builder, "w", "wmode");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            emit_fopen_check(g, fp, "cannot write file\n");
            LLVMValueRef fputs_args[] = { content_arg, fp };
            LLVMValueRef put = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fputs_fn, fputs_args, 2, "put");
            emit_io_abort_if(g, LLVMBuildICmp(g->builder, LLVMIntSLT, put, LLVMConstInt(i32, 0, 0), "put.err"),
                "cannot write file\n");
            LLVMValueRef wr_close = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "wr_close");
            emit_io_abort_if(g, LLVMBuildICmp(g->builder, LLVMIntNE, wr_close, LLVMConstInt(i32, 0, 0), "wr_close.err"),
                "cannot write file\n");
            emit_release_owned_call_temp(g, path_ast, path_arg, locals);
            emit_release_owned_call_temp(g, content_ast, content_arg, locals);
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Path.GetFileName(path), Path.GetExtension(path), Path.Combine(a,b) */
        if (is_call_to(expr, "Path", "GetFileName") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            zan_ast_node_t *path_ast = expr->call.args.items[0];
            LLVMValueRef path_val = emit_expr(g, path_ast, locals);
            /* call strrchr(path, '/') then strrchr(path, '\\') and pick later one */
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef slash_args[] = { path_val, LLVMConstInt(i32t, '/', 0) };
            LLVMValueRef slash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, slash_args, 2, "slash");
            LLVMValueRef bslash_args[] = { path_val, LLVMConstInt(i32t, 92, 0) }; /* 92 = backslash */
            LLVMValueRef bslash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, bslash_args, 2, "bslash");
            /* pick the one that's later (or non-null) */
            LLVMValueRef s_null = LLVMBuildICmp(g->builder, LLVMIntEQ, slash, LLVMConstNull(i8ptr), "snull");
            LLVMValueRef b_null = LLVMBuildICmp(g->builder, LLVMIntEQ, bslash, LLVMConstNull(i8ptr), "bnull");
            /* if both null, return path; else return max(slash,bslash)+1 */
            LLVMValueRef s_gt = LLVMBuildICmp(g->builder, LLVMIntUGT, slash, bslash, "sgt");
            LLVMValueRef best = LLVMBuildSelect(g->builder, s_gt, slash, bslash, "best");
            LLVMValueRef both_null = LLVMBuildAnd(g->builder, s_null, b_null, "bothnull");
            LLVMValueRef plus1 = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), best,
                (LLVMValueRef[]){ LLVMConstInt(i32t, 1, 0) }, 1, "p1");
            return LLVMBuildSelect(g->builder, both_null, path_val, plus1, "fname");
        }

        if (is_call_to(expr, "Path", "GetExtension") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            zan_ast_node_t *path_ast = expr->call.args.items[0];
            LLVMValueRef path_val = emit_expr(g, path_ast, locals);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef dot_args[] = { path_val, LLVMConstInt(i32t, '.', 0) };
            LLVMValueRef dot = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, dot_args, 2, "dot");
            /* if no dot, return empty string */
            LLVMValueRef is_null = LLVMBuildICmp(g->builder, LLVMIntEQ, dot, LLVMConstNull(i8ptr), "dnull");
            LLVMValueRef empty = emit_string_literal_rc(g, (zan_istr_t){ "", 0 });
            return LLVMBuildSelect(g->builder, is_null, empty, dot, "ext");
        }

        if (is_call_to(expr, "Path", "Combine") && expr->call.args.count == 2) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef a = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef b = emit_expr(g, expr->call.args.items[1], locals);
            /* len = strlen(a) + 1 + strlen(b) + 1 */
            LLVMTypeRef strlen_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
            LLVMValueRef len_a = zan_call2(g->builder, strlen_type, g->fn_strlen, &a, 1, "la");
            LLVMValueRef len_b = zan_call2(g->builder, strlen_type, g->fn_strlen, &b, 1, "lb");
            LLVMValueRef total = LLVMBuildAdd(g->builder, len_a, len_b, "t");
            total = LLVMBuildAdd(g->builder, total, LLVMConstInt(i64, 2, 0), "t2"); /* +separator+null */
            LLVMValueRef buf = emit_string_alloc_rc(g, total);
            /* strcpy(buf, a) */
            LLVMValueRef strcpy_args[] = { buf, a };
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcpy, strcpy_args, 2, "");
            /* strcat(buf, "/") */
            LLVMValueRef sep = LLVMBuildGlobalStringPtr(g->builder, g->target_is_windows ? "\\" : "/", "sep");
            LLVMValueRef cat1_args[] = { buf, sep };
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcat, cat1_args, 2, "");
            /* strcat(buf, b) */
            LLVMValueRef cat2_args[] = { buf, b };
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcat, cat2_args, 2, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], a, locals);
            emit_release_owned_call_temp(g, expr->call.args.items[1], b, locals);
            return buf;
        }

        /* File.AppendAllText(path, content) */
        if (is_call_to(expr, "File", "AppendAllText") && expr->call.args.count == 2) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef content_arg = emit_expr(g, expr->call.args.items[1], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef fopen_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", fopen_type);
            }
            LLVMValueRef fputs_fn = LLVMGetNamedFunction(g->mod, "fputs");
            if (!fputs_fn) {
                LLVMTypeRef fputs_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fputs_fn = LLVMAddFunction(g->mod, "fputs", fputs_type);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef fclose_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", fclose_type);
            }
            LLVMValueRef mode = LLVMBuildGlobalStringPtr(g->builder, "a", "amode");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            emit_fopen_check(g, fp, "cannot write file\n");
            LLVMValueRef fputs_args[] = { content_arg, fp };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fputs_fn, fputs_args, 2, "");
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            emit_release_owned_call_temp(g, expr->call.args.items[1], content_arg, locals);
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* File.Exists(path) -> bool */
        if (is_call_to(expr, "File", "Exists") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef fopen_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", fopen_type);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef fclose_type = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", fclose_type);
            }
            LLVMValueRef mode = LLVMBuildGlobalStringPtr(g->builder, "rb", "rb");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            LLVMValueRef is_null = LLVMBuildICmp(g->builder, LLVMIntNE, fp,
                LLVMConstNull(i8ptr), "exists");
            /* close if opened */
            LLVMBasicBlockRef close_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "fexist.close");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "fexist.end");
            LLVMBuildCondBr(g->builder, is_null, close_bb, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, close_bb);
            zan_call2(g->builder, LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "");
            LLVMBuildBr(g->builder, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            LLVMValueRef result = LLVMBuildZExt(g->builder, is_null, LLVMInt64TypeInContext(g->ctx), "fex");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return result;
        }

        /* File.Delete(path) */
        if (is_call_to(expr, "File", "Delete") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef remove_fn = LLVMGetNamedFunction(g->mod, "remove");
            if (!remove_fn) {
                LLVMTypeRef remove_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                remove_fn = LLVMAddFunction(g->mod, "remove", remove_type);
            }
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                remove_fn, &path_arg, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* File.Move(source, dest) — rename */
        if (is_call_to(expr, "File", "Move") && expr->call.args.count == 2) {
            LLVMValueRef src = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef dst = emit_expr(g, expr->call.args.items[1], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef rename_fn = LLVMGetNamedFunction(g->mod, "rename");
            if (!rename_fn) {
                LLVMTypeRef rename_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                rename_fn = LLVMAddFunction(g->mod, "rename", rename_type);
            }
            LLVMValueRef args[] = { src, dst };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                rename_fn, args, 2, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], src, locals);
            emit_release_owned_call_temp(g, expr->call.args.items[1], dst, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* File.Copy(source, dest) — read source, write dest */
        if (is_call_to(expr, "File", "Copy") && expr->call.args.count == 2) {
            LLVMValueRef src = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef dst = emit_expr(g, expr->call.args.items[1], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            /* declare helper functions */
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", ft);
            }
            LLVMValueRef fread_fn = LLVMGetNamedFunction(g->mod, "fread");
            if (!fread_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0);
                fread_fn = LLVMAddFunction(g->mod, "fread", ft);
            }
            LLVMValueRef fwrite_fn = LLVMGetNamedFunction(g->mod, "fwrite");
            if (!fwrite_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0);
                fwrite_fn = LLVMAddFunction(g->mod, "fwrite", ft);
            }
            LLVMValueRef fseek_fn = LLVMGetNamedFunction(g->mod, "fseek");
            if (!fseek_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0);
                fseek_fn = LLVMAddFunction(g->mod, "fseek", ft);
            }
            LLVMValueRef ftell_fn = LLVMGetNamedFunction(g->mod, "ftell");
            if (!ftell_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                ftell_fn = LLVMAddFunction(g->mod, "ftell", ft);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", ft);
            }
            /* open source for reading */
            LLVMValueRef rb = LLVMBuildGlobalStringPtr(g->builder, "rb", "rb");
            LLVMValueRef sargs[] = { src, rb };
            LLVMValueRef sfp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, sargs, 2, "sfp");
            /* get size */
            LLVMValueRef seek_end[] = { sfp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 2, 0) };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_end, 3, "");
            LLVMValueRef sz = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), ftell_fn, &sfp, 1, "sz");
            LLVMValueRef seek_start[] = { sfp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 0, 0) };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_start, 3, "");
            /* allocate buffer */
            LLVMValueRef buf = emit_string_alloc_rc(g, sz);
            /* read */
            LLVMValueRef fread_args[] = { buf, LLVMConstInt(i64, 1, 0), sz, sfp };
            zan_call2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fread_fn, fread_args, 4, "");
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &sfp, 1, "");
            /* open dest for writing */
            LLVMValueRef wb = LLVMBuildGlobalStringPtr(g->builder, "wb", "wb");
            LLVMValueRef dargs[] = { dst, wb };
            LLVMValueRef dfp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, dargs, 2, "dfp");
            /* write */
            LLVMValueRef fwrite_args[] = { buf, LLVMConstInt(i64, 1, 0), sz, dfp };
            zan_call2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fwrite_fn, fwrite_args, 4, "");
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &dfp, 1, "");
            /* free buffer */
            zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0),
                g->fn_free, &buf, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], src, locals);
            emit_release_owned_call_temp(g, expr->call.args.items[1], dst, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* File.GetSize(path) -> int */
        if (is_call_to(expr, "File", "GetSize") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", ft);
            }
            LLVMValueRef fseek_fn = LLVMGetNamedFunction(g->mod, "fseek");
            if (!fseek_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0);
                fseek_fn = LLVMAddFunction(g->mod, "fseek", ft);
            }
            LLVMValueRef ftell_fn = LLVMGetNamedFunction(g->mod, "ftell");
            if (!ftell_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                ftell_fn = LLVMAddFunction(g->mod, "ftell", ft);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", ft);
            }
            LLVMValueRef mode = LLVMBuildGlobalStringPtr(g->builder, "rb", "rb");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            LLVMValueRef seek_end[] = { fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 2, 0) };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_end, 3, "");
            LLVMValueRef sz = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), ftell_fn, &fp, 1, "fsz");
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return sz;
        }

        /* Directory.Exists(path) -> bool */
        if (is_call_to(expr, "Directory", "Exists") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            /* Windows: GetFileAttributesA & FILE_ATTRIBUTE_DIRECTORY. POSIX: opendir!=NULL. */
            LLVMValueRef result;
            if (g->target_is_windows) {
                LLVMValueRef gfa_fn = LLVMGetNamedFunction(g->mod, "GetFileAttributesA");
                if (!gfa_fn) {
                    LLVMTypeRef gfa_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    gfa_fn = LLVMAddFunction(g->mod, "GetFileAttributesA", gfa_type);
                }
                LLVMValueRef attrs = zan_call2(g->builder,
                    LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    gfa_fn, &path_arg, 1, "attrs");
                LLVMValueRef not_invalid = LLVMBuildICmp(g->builder, LLVMIntNE, attrs,
                    LLVMConstInt(i32, 0xFFFFFFFF, 0), "noinv");
                LLVMValueRef is_dir = LLVMBuildAnd(g->builder, attrs,
                    LLVMConstInt(i32, 0x10, 0), "isdir");
                LLVMValueRef is_dir_bool = LLVMBuildICmp(g->builder, LLVMIntNE, is_dir,
                    LLVMConstInt(i32, 0, 0), "isdirb");
                result = LLVMBuildAnd(g->builder, not_invalid, is_dir_bool, "dexist");
            } else {
                LLVMValueRef opendir_fn = LLVMGetNamedFunction(g->mod, "opendir");
                if (!opendir_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    opendir_fn = LLVMAddFunction(g->mod, "opendir", ft);
                }
                LLVMValueRef dirp = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    opendir_fn, &path_arg, 1, "dirp");
                result = LLVMBuildICmp(g->builder, LLVMIntNE, dirp, LLVMConstNull(i8ptr), "dopen");
                LLVMValueRef closedir_fn = LLVMGetNamedFunction(g->mod, "closedir");
                if (!closedir_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    closedir_fn = LLVMAddFunction(g->mod, "closedir", ft);
                }
                LLVMBasicBlockRef cl_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "de.close");
                LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "de.end");
                LLVMBuildCondBr(g->builder, result, cl_bb, end_bb);
                LLVMPositionBuilderAtEnd(g->builder, cl_bb);
                zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    closedir_fn, &dirp, 1, "");
                LLVMBuildBr(g->builder, end_bb);
                LLVMPositionBuilderAtEnd(g->builder, end_bb);
            }
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMBuildZExt(g->builder, result, i64, "dex");
        }

        /* Environment.ExeDir() — directory containing the running executable
         * (runtime helper zan_exe_dir_into fills an rc string buffer). */
        if (is_call_to(expr, "Environment", "ExeDir") && expr->call.args.count == 0) {
            g->uses_sync_runtime = true;
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 1024, 0));
            LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_exe_dir_into");
            if (!fn) fn = LLVMAddFunction(g->mod, "zan_exe_dir_into", ft);
            LLVMValueRef args2[] = { buf, LLVMConstInt(i64, 1024, 0) };
            zan_call2(g->builder, ft, fn, args2, 2, "");
            return buf;
        }

        /* Directory.ListNames(pattern) — '\n'-joined file names matching a
         * glob (runtime helper zan_dir_list_into fills an rc string buffer). */
        if (is_call_to(expr, "Directory", "ListNames") && expr->call.args.count == 1) {
            g->uses_sync_runtime = true;
            LLVMValueRef pat = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 65536, 0));
            LLVMTypeRef ft = LLVMFunctionType(i64,
                (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_dir_list_into");
            if (!fn) fn = LLVMAddFunction(g->mod, "zan_dir_list_into", ft);
            LLVMValueRef args3[] = { pat, buf, LLVMConstInt(i64, 65536, 0) };
            zan_call2(g->builder, ft, fn, args3, 3, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], pat, locals);
            return buf;
        }

        /* Directory.CreateDirectory(path) */
        if (is_call_to(expr, "Directory", "CreateDirectory") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            if (g->target_is_windows) {
                LLVMValueRef mkdir_fn = LLVMGetNamedFunction(g->mod, "_mkdir");
                if (!mkdir_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    mkdir_fn = LLVMAddFunction(g->mod, "_mkdir", ft);
                }
                zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    mkdir_fn, &path_arg, 1, "");
            } else {
                LLVMValueRef mkdir_fn = LLVMGetNamedFunction(g->mod, "mkdir");
                if (!mkdir_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0);
                    mkdir_fn = LLVMAddFunction(g->mod, "mkdir", ft);
                }
                LLVMValueRef margs[] = { path_arg, LLVMConstInt(i32, 0777, 0) };
                zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0),
                    mkdir_fn, margs, 2, "");
            }
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* Directory.Delete(path) */
        if (is_call_to(expr, "Directory", "Delete") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            const char *rmname = g->target_is_windows ? "_rmdir" : "rmdir";
            LLVMValueRef rmdir_fn = LLVMGetNamedFunction(g->mod, rmname);
            if (!rmdir_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                rmdir_fn = LLVMAddFunction(g->mod, rmname, ft);
            }
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                rmdir_fn, &path_arg, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* Directory.GetCurrentDirectory() -> string */
        if (is_call_to(expr, "Directory", "GetCurrentDirectory") && expr->call.args.count == 0) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 4096, 0));
            const char *cwdname = g->target_is_windows ? "_getcwd" : "getcwd";
            LLVMValueRef getcwd_fn = LLVMGetNamedFunction(g->mod, cwdname);
            if (!getcwd_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0);
                getcwd_fn = LLVMAddFunction(g->mod, cwdname, ft);
            }
            LLVMValueRef cwd_args[] = { buf, LLVMConstInt(i32, 4096, 0) };
            zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0),
                getcwd_fn, cwd_args, 2, "");
            return buf;
        }

        /* Directory.SetCurrentDirectory(path) */
        if (is_call_to(expr, "Directory", "SetCurrentDirectory") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            const char *chname = g->target_is_windows ? "_chdir" : "chdir";
            LLVMValueRef chdir_fn = LLVMGetNamedFunction(g->mod, chname);
            if (!chdir_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                chdir_fn = LLVMAddFunction(g->mod, chname, ft);
            }
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                chdir_fn, &path_arg, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* Path.GetDirectoryName(path) -> string */
        if (is_call_to(expr, "Path", "GetDirectoryName") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
            LLVMValueRef path_val = emit_expr(g, expr->call.args.items[0], locals);
            /* strlen */
            LLVMValueRef len = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), g->fn_strlen, &path_val, 1, "plen");
            /* allocate copy */
            LLVMValueRef bsz = LLVMBuildAdd(g->builder, len, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = emit_string_alloc_rc(g, bsz);
            LLVMValueRef cpy_args[] = { buf, path_val };
            zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcpy, cpy_args, 2, "");
            /* find last separator */
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef slash_args[] = { buf, LLVMConstInt(i32t, '/', 0) };
            LLVMValueRef slash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, slash_args, 2, "sl");
            LLVMValueRef bslash_args[] = { buf, LLVMConstInt(i32t, 92, 0) };
            LLVMValueRef bslash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, bslash_args, 2, "bsl");
            /* pick later one */
            LLVMValueRef sl_null = LLVMBuildICmp(g->builder, LLVMIntEQ, slash, LLVMConstNull(i8ptr), "snul");
            LLVMValueRef pick = LLVMBuildSelect(g->builder, sl_null, bslash, slash, "pk1");
            LLVMValueRef bs_null = LLVMBuildICmp(g->builder, LLVMIntEQ, bslash, LLVMConstNull(i8ptr), "bnul");
            LLVMValueRef sep_ptr = LLVMBuildSelect(g->builder, bs_null, pick,
                LLVMBuildSelect(g->builder,
                    LLVMBuildICmp(g->builder, LLVMIntUGT, bslash, pick, "bgt"),
                    bslash, pick, "pk2"), "sep");
            /* truncate at separator */
            LLVMValueRef found = LLVMBuildICmp(g->builder, LLVMIntNE, sep_ptr, LLVMConstNull(i8ptr), "fnd");
            LLVMBasicBlockRef trunc_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dn.trunc");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dn.end");
            LLVMBuildCondBr(g->builder, found, trunc_bb, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, trunc_bb);
            LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), sep_ptr);
            LLVMBuildBr(g->builder, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_val, locals);
            return buf;
        }

        /* Path.HasExtension(path) -> bool */
        if (is_call_to(expr, "Path", "HasExtension") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef path_val = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef dot_args[] = { path_val, LLVMConstInt(i32t, '.', 0) };
            LLVMValueRef dot = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, dot_args, 2, "dot");
            LLVMValueRef has = LLVMBuildICmp(g->builder, LLVMIntNE, dot, LLVMConstNull(i8ptr), "hasext");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_val, locals);
            return LLVMBuildZExt(g->builder, has, i64, "he");
        }

        /* Path.GetTempPath() -> string */
        if (is_call_to(expr, "Path", "GetTempPath") && expr->call.args.count == 0) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 260, 0));
            if (g->target_is_windows) {
                LLVMValueRef gtp_fn = LLVMGetNamedFunction(g->mod, "GetTempPathA");
                if (!gtp_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i32, i8ptr }, 2, 0);
                    gtp_fn = LLVMAddFunction(g->mod, "GetTempPathA", ft);
                }
                LLVMValueRef gtp_args[] = { LLVMConstInt(i32, 260, 0), buf };
                zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i32, i8ptr }, 2, 0),
                    gtp_fn, gtp_args, 2, "");
            } else {
                LLVMValueRef getenv_fn = LLVMGetNamedFunction(g->mod, "getenv");
                if (!getenv_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    getenv_fn = LLVMAddFunction(g->mod, "getenv", ft);
                }
                LLVMValueRef key = LLVMBuildGlobalStringPtr(g->builder, "TMPDIR", "tmpdir_k");
                LLVMValueRef env = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    getenv_fn, &key, 1, "tmpenv");
                LLVMValueRef isnull = LLVMBuildICmp(g->builder, LLVMIntEQ, env, LLVMConstNull(i8ptr), "tnull");
                LLVMValueRef deflt = LLVMBuildGlobalStringPtr(g->builder, "/tmp/", "tmpdef");
                LLVMValueRef src = LLVMBuildSelect(g->builder, isnull, deflt, env, "tmpsrc");
                LLVMValueRef cpy_args[] = { buf, src };
                zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                    g->fn_strcpy, cpy_args, 2, "");
            }
            return buf;
        }

        /* Path.GetFileNameWithoutExtension(path) -> string */
        if (is_call_to(expr, "Path", "GetFileNameWithoutExtension") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef path_val = emit_expr(g, expr->call.args.items[0], locals);
            /* Get filename first (reuse strrchr logic) */
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef slash_args[] = { path_val, LLVMConstInt(i32t, '/', 0) };
            LLVMValueRef slash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, slash_args, 2, "sl");
            LLVMValueRef bslash_args[] = { path_val, LLVMConstInt(i32t, 92, 0) };
            LLVMValueRef bslash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, bslash_args, 2, "bsl");
            /* pick later separator */
            LLVMValueRef sl_null = LLVMBuildICmp(g->builder, LLVMIntEQ, slash, LLVMConstNull(i8ptr), "snul");
            LLVMValueRef bs_null = LLVMBuildICmp(g->builder, LLVMIntEQ, bslash, LLVMConstNull(i8ptr), "bnul");
            LLVMValueRef best = LLVMBuildSelect(g->builder, sl_null, bslash,
                LLVMBuildSelect(g->builder, bs_null, slash,
                    LLVMBuildSelect(g->builder, LLVMBuildICmp(g->builder, LLVMIntUGT, bslash, slash, "bgt"),
                        bslash, slash, "mx"), "pk"), "sep");
            LLVMValueRef has_sep = LLVMBuildICmp(g->builder, LLVMIntNE, best, LLVMConstNull(i8ptr), "hs");
            /* filename starts after separator+1, or is the whole path */
            LLVMValueRef after = LLVMBuildGEP2(g->builder, i8, best, &(LLVMValueRef){LLVMConstInt(i64, 1, 0)}, 1, "aft");
            LLVMValueRef fname = LLVMBuildSelect(g->builder, has_sep, after, path_val, "fn");
            /* make a copy, then truncate at last dot */
            LLVMValueRef flen = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), g->fn_strlen, &fname, 1, "flen");
            LLVMValueRef bsz = LLVMBuildAdd(g->builder, flen, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = emit_string_alloc_rc(g, bsz);
            LLVMValueRef cpy_args[] = { buf, fname };
            zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcpy, cpy_args, 2, "");
            LLVMValueRef dot_args[] = { buf, LLVMConstInt(i32t, '.', 0) };
            LLVMValueRef dot = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, dot_args, 2, "dot");
            LLVMValueRef has_dot = LLVMBuildICmp(g->builder, LLVMIntNE, dot, LLVMConstNull(i8ptr), "hd");
            LLVMBasicBlockRef trunc_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "fnwe.trunc");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "fnwe.end");
            LLVMBuildCondBr(g->builder, has_dot, trunc_bb, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, trunc_bb);
            LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), dot);
            LLVMBuildBr(g->builder, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            return buf;
        }


                /* List.Add(item) — append to dynamic list */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 3 && memcmp(method_name.str, "Add", 3) == 0 &&
                expr->call.args.count == 1) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && ltype->name.len == 4 &&
                    memcmp(ltype->name.str, "List", 4) == 0) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    /* load list pointer (works for local vars and fields) */
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    (void)i8ptr;
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    /* load count */
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    /* load capacity */
                    LLVMValueRef cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 1, "capp");
                    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, cap_ptr, "cap");
                    /* check if need to grow: if count >= capacity */
                    LLVMValueRef need_grow = LLVMBuildICmp(g->builder, LLVMIntUGE, count, cap, "grow");
                    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "list.grow");
                    LLVMBasicBlockRef add_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "list.add");
                    LLVMBuildCondBr(g->builder, need_grow, grow_bb, add_bb);
                    /* grow block: double capacity, realloc */
                    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
                    LLVMValueRef new_cap = LLVMBuildMul(g->builder, cap, LLVMConstInt(i64, 2, 0), "ncap");
                    LLVMBuildStore(g->builder, new_cap, cap_ptr);
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef old_data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "od");
                    LLVMValueRef old_data_raw = LLVMBuildBitCast(g->builder, old_data, i8ptr, "odr");
                    LLVMValueRef new_size = LLVMBuildMul(g->builder, new_cap, LLVMConstInt(i64, 8, 0), "nsz");
                    LLVMValueRef realloc_args[] = { old_data_raw, new_size };
                    LLVMValueRef new_data_raw = zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
                        g->fn_realloc, realloc_args, 2, "nd");
                    LLVMValueRef new_data = LLVMBuildBitCast(g->builder, new_data_raw, LLVMPointerType(i64, 0), "ndt");
                    LLVMBuildStore(g->builder, new_data, data_field);
                    LLVMBuildBr(g->builder, add_bb);
                    /* add block: store value at data[count], increment count */
                    LLVMPositionBuilderAtEnd(g->builder, add_bb);
                    LLVMValueRef data_field2 = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df2");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field2, "d");
                    LLVMValueRef count2 = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt2");
                    LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &count2, 1, "ep");
                    /* emit the value to add */
                    LLVMValueRef val = emit_expr(g, expr->call.args.items[0], locals);
                    emit_collection_slot_store(g, container_elem_type(ltype), i64, elem_ptr,
                        val, expr->call.args.items[0], locals, 0);
                    /* count++ */
                    LLVMValueRef new_count = LLVMBuildAdd(g->builder, count2, LLVMConstInt(i64, 1, 0), "nc");
                    LLVMBuildStore(g->builder, new_count, count_ptr);
                    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                }
            }
        }

        /* List.AddRange(other) — append every element of another list */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 8 && memcmp(method_name.str, "AddRange", 8) == 0 &&
                expr->call.args.count == 1) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && ltype->name.len == 4 &&
                    memcmp(ltype->name.str, "List", 4) == 0) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
                    LLVMTypeRef list_pt = LLVMPointerType(g->list_struct_type, 0);
                    zan_type_t *elem_type = container_elem_type(ltype);
                    /* self + other list pointers */
                    LLVMValueRef self_raw = emit_expr(g, lobj, locals);
                    LLVMValueRef self_ptr = LLVMBuildBitCast(g->builder, self_raw, list_pt, "ar.self");
                    LLVMValueRef other_raw = emit_expr(g, expr->call.args.items[0], locals);
                    LLVMValueRef other_ptr = LLVMBuildBitCast(g->builder, other_raw, list_pt, "ar.other");
                    /* skip entirely when other is null */
                    LLVMValueRef other_i = LLVMBuildPtrToInt(g->builder, other_ptr, i64, "ar.oi");
                    LLVMValueRef is_null = LLVMBuildICmp(g->builder, LLVMIntEQ, other_i,
                        LLVMConstInt(i64, 0, 0), "ar.null");
                    LLVMBasicBlockRef ar_body0 = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.enter");
                    LLVMBasicBlockRef ar_done = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.done");
                    LLVMBuildCondBr(g->builder, is_null, ar_done, ar_body0);
                    LLVMPositionBuilderAtEnd(g->builder, ar_body0);
                    /* snapshot other's count (so self.AddRange(self) terminates) */
                    LLVMValueRef ocnt_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, other_ptr, 0, "ar.ocp");
                    LLVMValueRef ocnt = LLVMBuildLoad2(g->builder, i64, ocnt_ptr, "ar.ocnt");
                    LLVMValueRef idx_a = emit_entry_alloca(g, i64, "ar.i");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                    LLVMBasicBlockRef c_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.cond");
                    LLVMBasicBlockRef b_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.step");
                    LLVMBuildBr(g->builder, c_bb);
                    LLVMPositionBuilderAtEnd(g->builder, c_bb);
                    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ar.ci");
                    LLVMValueRef more = LLVMBuildICmp(g->builder, LLVMIntULT, ci, ocnt, "ar.more");
                    LLVMBuildCondBr(g->builder, more, b_bb, ar_done);
                    LLVMPositionBuilderAtEnd(g->builder, b_bb);
                    /* read other[i] (reload data each step in case other == self grew) */
                    LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ar.ci2");
                    LLVMValueRef odata_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, other_ptr, 2, "ar.odf");
                    LLVMValueRef odata = LLVMBuildLoad2(g->builder, i64ptr, odata_field, "ar.od");
                    LLVMValueRef oslot = LLVMBuildGEP2(g->builder, i64, odata, &ci2, 1, "ar.os");
                    LLVMValueRef rawv = LLVMBuildLoad2(g->builder, i64, oslot, "ar.rv");
                    /* grow self if full (mirrors List.Add) */
                    LLVMValueRef s_cnt_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, self_ptr, 0, "ar.scp");
                    LLVMValueRef s_cnt = LLVMBuildLoad2(g->builder, i64, s_cnt_ptr, "ar.sc");
                    LLVMValueRef s_cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, self_ptr, 1, "ar.scapp");
                    LLVMValueRef s_cap = LLVMBuildLoad2(g->builder, i64, s_cap_ptr, "ar.scap");
                    LLVMValueRef need = LLVMBuildICmp(g->builder, LLVMIntUGE, s_cnt, s_cap, "ar.grow");
                    LLVMBasicBlockRef g_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.grow");
                    LLVMBasicBlockRef s_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.store");
                    LLVMBuildCondBr(g->builder, need, g_bb, s_bb);
                    LLVMPositionBuilderAtEnd(g->builder, g_bb);
                    /* newcap = cap == 0 ? 4 : cap * 2 */
                    LLVMValueRef cap_zero = LLVMBuildICmp(g->builder, LLVMIntEQ, s_cap,
                        LLVMConstInt(i64, 0, 0), "ar.cz");
                    LLVMValueRef dbl = LLVMBuildMul(g->builder, s_cap, LLVMConstInt(i64, 2, 0), "ar.dbl");
                    LLVMValueRef ncap = LLVMBuildSelect(g->builder, cap_zero,
                        LLVMConstInt(i64, 4, 0), dbl, "ar.ncap");
                    LLVMBuildStore(g->builder, ncap, s_cap_ptr);
                    LLVMValueRef s_df = LLVMBuildStructGEP2(g->builder, g->list_struct_type, self_ptr, 2, "ar.sdf");
                    LLVMValueRef old_data = LLVMBuildLoad2(g->builder, i64ptr, s_df, "ar.oldd");
                    LLVMValueRef old_raw = LLVMBuildBitCast(g->builder, old_data, i8ptr, "ar.oldr");
                    LLVMValueRef nsz = LLVMBuildMul(g->builder, ncap, LLVMConstInt(i64, 8, 0), "ar.nsz");
                    LLVMValueRef re_args[] = { old_raw, nsz };
                    LLVMValueRef nd_raw = zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
                        g->fn_realloc, re_args, 2, "ar.nd");
                    LLVMValueRef nd = LLVMBuildBitCast(g->builder, nd_raw, i64ptr, "ar.ndt");
                    LLVMBuildStore(g->builder, nd, s_df);
                    LLVMBuildBr(g->builder, s_bb);
                    LLVMPositionBuilderAtEnd(g->builder, s_bb);
                    /* store raw value at self.data[count] */
                    LLVMValueRef s_df2 = LLVMBuildStructGEP2(g->builder, g->list_struct_type, self_ptr, 2, "ar.sdf2");
                    LLVMValueRef s_data = LLVMBuildLoad2(g->builder, i64ptr, s_df2, "ar.sd");
                    LLVMValueRef s_cnt2 = LLVMBuildLoad2(g->builder, i64, s_cnt_ptr, "ar.sc2");
                    LLVMValueRef s_slot = LLVMBuildGEP2(g->builder, i64, s_data, &s_cnt2, 1, "ar.ss");
                    LLVMBuildStore(g->builder, rawv, s_slot);
                    /* both lists now reference the element: retain if managed */
                    if (is_rc_managed_type(elem_type)) {
                        LLVMTypeRef mt = map_type(g, elem_type);
                        if (LLVMGetTypeKind(mt) == LLVMPointerTypeKind) {
                            LLVMValueRef pv = LLVMBuildIntToPtr(g->builder, rawv, mt, "ar.pv");
                            emit_rc_retain_for_type(g, elem_type, pv);
                        }
                    }
                    LLVMValueRef s_nc = LLVMBuildAdd(g->builder, s_cnt2, LLVMConstInt(i64, 1, 0), "ar.snc");
                    LLVMBuildStore(g->builder, s_nc, s_cnt_ptr);
                    LLVMValueRef ni = LLVMBuildAdd(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ar.ni");
                    LLVMBuildStore(g->builder, ni, idx_a);
                    LLVMBuildBr(g->builder, c_bb);
                    LLVMPositionBuilderAtEnd(g->builder, ar_done);
                    return LLVMConstInt(i32t, 0, 0);
                }
            }
        }

        /* List.Clear() — reset count to 0 */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 5 && memcmp(method_name.str, "Clear", 5) == 0 &&
                expr->call.args.count == 0) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && ltype->name.len == 4 && memcmp(ltype->name.str, "List", 4) == 0) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    LLVMValueRef idx_a = emit_entry_alloca(g, i64, "lc");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                    LLVMBasicBlockRef c_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "lc.cond");
                    LLVMBasicBlockRef b_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "lc.body");
                    LLVMBasicBlockRef d_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "lc.done");
                    LLVMBuildBr(g->builder, c_bb);
                    LLVMPositionBuilderAtEnd(g->builder, c_bb);
                    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                    LLVMValueRef more = LLVMBuildICmp(g->builder, LLVMIntULT, ci, count, "more");
                    LLVMBuildCondBr(g->builder, more, b_bb, d_bb);
                    LLVMPositionBuilderAtEnd(g->builder, b_bb);
                    LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &ci2, 1, "sl");
                    LLVMValueRef val = LLVMBuildLoad2(g->builder, i64, slot, "sv");
                    emit_collection_release_raw_slot(g, container_elem_type(ltype), val, i64);
                    LLVMValueRef ni = LLVMBuildAdd(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                    LLVMBuildStore(g->builder, ni, idx_a);
                    LLVMBuildBr(g->builder, c_bb);
                    LLVMPositionBuilderAtEnd(g->builder, d_bb);
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), count_ptr);
                    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                }
            }
        }

        /* List.RemoveAt(index) — shift elements left, decrement count */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 8 && memcmp(method_name.str, "RemoveAt", 8) == 0 &&
                expr->call.args.count == 1) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && ltype->name.len == 4 && memcmp(ltype->name.str, "List", 4) == 0) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    LLVMValueRef idx = emit_expr(g, expr->call.args.items[0], locals);
                    LLVMValueRef removed_ptr = LLVMBuildGEP2(g->builder, i64, data, &idx, 1, "rmp");
                    LLVMValueRef removed = LLVMBuildLoad2(g->builder, i64, removed_ptr, "rmv");
                    emit_collection_release_raw_slot(g, container_elem_type(ltype), removed, i64);
                    /* shift loop: for j = idx; j < count-1; j++ : data[j] = data[j+1] */
                    LLVMValueRef j_a = emit_entry_alloca(g, i64, "j");
                    LLVMBuildStore(g->builder, idx, j_a);
                    LLVMValueRef last = LLVMBuildSub(g->builder, count, LLVMConstInt(i64, 1, 0), "last");
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ra.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ra.body");
                    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ra.done");
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef j = LLVMBuildLoad2(g->builder, i64, j_a, "j");
                    LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntULT, j, last, "cmp");
                    LLVMBuildCondBr(g->builder, cmp, body_bb, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    LLVMValueRef j2 = LLVMBuildLoad2(g->builder, i64, j_a, "j2");
                    LLVMValueRef next = LLVMBuildAdd(g->builder, j2, LLVMConstInt(i64, 1, 0), "nxt");
                    LLVMValueRef src_slot = LLVMBuildGEP2(g->builder, i64, data, &next, 1, "ss");
                    LLVMValueRef val = LLVMBuildLoad2(g->builder, i64, src_slot, "sv");
                    LLVMValueRef dst_slot = LLVMBuildGEP2(g->builder, i64, data, &j2, 1, "ds");
                    LLVMBuildStore(g->builder, val, dst_slot);
                    LLVMValueRef j3 = LLVMBuildAdd(g->builder, j2, LLVMConstInt(i64, 1, 0), "j3");
                    LLVMBuildStore(g->builder, j3, j_a);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, done_bb);
                    LLVMBuildStore(g->builder, last, count_ptr);
                    LLVMValueRef tail_ptr = LLVMBuildGEP2(g->builder, i64, data, &last, 1, "tail");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), tail_ptr);
                    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                }
            }
        }

        /* List.IndexOf(item) -> int (-1 if not found) */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 7 && memcmp(method_name.str, "IndexOf", 7) == 0 &&
                expr->call.args.count == 1) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && ltype->name.len == 4 && memcmp(ltype->name.str, "List", 4) == 0) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    LLVMValueRef search = emit_expr(g, expr->call.args.items[0], locals);
                    /* result alloca — -1 for not found */
                    LLVMValueRef res = emit_entry_alloca(g, i64, "iofr");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, (uint64_t)-1LL, 1), res);
                    LLVMValueRef idx_a = emit_entry_alloca(g, i64, "ii");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.body");
                    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.done");
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                    LLVMValueRef cmp_d = LLVMBuildICmp(g->builder, LLVMIntUGE, ci, count, "cdone");
                    LLVMBuildCondBr(g->builder, cmp_d, done_bb, body_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &ci2, 1, "sl");
                    LLVMValueRef val = LLVMBuildLoad2(g->builder, i64, slot, "sv");
                    /* compare: for ints use ==, for strings use strcmp */
                    LLVMValueRef eq;
                    zan_type_t *elem_type = ltype->type_arg_count > 0 ? ltype->type_args[0] : NULL;
                    /* Under monomorphization, a List<T> field of the generic
                     * class being specialized carries the erased type param T;
                     * substitute it to the concrete instantiation argument so
                     * reference elements (e.g. string) use content equality. */
                    elem_type = concretize(g, elem_type);
                    if (elem_type && elem_type->kind == TYPE_STRING) {
                        LLVMValueRef sval = LLVMBuildIntToPtr(g->builder, val, i8ptr, "sptr");
                        LLVMValueRef cmp_args[] = { sval, search };
                        LLVMValueRef c = zan_call2(g->builder,
                            LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                            g->fn_strcmp, cmp_args, 2, "cmp");
                        eq = LLVMBuildICmp(g->builder, LLVMIntEQ, c, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0), "eq");
                    } else {
                        LLVMValueRef sext = search;
                        if (LLVMGetTypeKind(LLVMTypeOf(search)) == LLVMPointerTypeKind)
                            sext = LLVMBuildPtrToInt(g->builder, search, i64, "sp");
                        else if (LLVMGetIntTypeWidth(LLVMTypeOf(search)) < 64)
                            sext = LLVMBuildSExt(g->builder, search, i64, "se");
                        eq = LLVMBuildICmp(g->builder, LLVMIntEQ, val, sext, "eq");
                    }
                    LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.found");
                    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.next");
                    LLVMBuildCondBr(g->builder, eq, found_bb, next_bb);
                    LLVMPositionBuilderAtEnd(g->builder, found_bb);
                    LLVMBuildStore(g->builder, ci2, res);
                    LLVMBuildBr(g->builder, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, next_bb);
                    LLVMValueRef ni = LLVMBuildAdd(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                    LLVMBuildStore(g->builder, ni, idx_a);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, done_bb);
                    return LLVMBuildLoad2(g->builder, i64, res, "iofres");
                }
            }
        }

        /* List.Contains(item) -> bool (uses IndexOf logic) */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 8 && memcmp(method_name.str, "Contains", 8) == 0 &&
                expr->call.args.count == 1) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && ltype->name.len == 4 && memcmp(ltype->name.str, "List", 4) == 0) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    LLVMValueRef search = emit_expr(g, expr->call.args.items[0], locals);
                    LLVMValueRef res = emit_entry_alloca(g, LLVMInt32TypeInContext(g->ctx), "cr");
                    LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0), res);
                    LLVMValueRef idx_a = emit_entry_alloca(g, i64, "ci");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ct.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ct.body");
                    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ct.done");
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                    LLVMValueRef cmp_d = LLVMBuildICmp(g->builder, LLVMIntUGE, ci, count, "cdone");
                    LLVMBuildCondBr(g->builder, cmp_d, done_bb, body_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &ci2, 1, "sl");
                    LLVMValueRef val = LLVMBuildLoad2(g->builder, i64, slot, "sv");
                    LLVMValueRef eq;
                    zan_type_t *elem_type = ltype->type_arg_count > 0 ? ltype->type_args[0] : NULL;
                    /* Under monomorphization, a List<T> field of the generic
                     * class being specialized carries the erased type param T;
                     * substitute it to the concrete instantiation argument so
                     * reference elements (e.g. string) use content equality. */
                    elem_type = concretize(g, elem_type);
                    if (elem_type && elem_type->kind == TYPE_STRING) {
                        LLVMValueRef sval = LLVMBuildIntToPtr(g->builder, val, i8ptr, "sptr");
                        LLVMValueRef cmp_args[] = { sval, search };
                        LLVMValueRef c = zan_call2(g->builder,
                            LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                            g->fn_strcmp, cmp_args, 2, "cmp");
                        eq = LLVMBuildICmp(g->builder, LLVMIntEQ, c, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0), "eq");
                    } else {
                        LLVMValueRef sext = search;
                        if (LLVMGetTypeKind(LLVMTypeOf(search)) == LLVMPointerTypeKind)
                            sext = LLVMBuildPtrToInt(g->builder, search, i64, "sp");
                        else if (LLVMGetIntTypeWidth(LLVMTypeOf(search)) < 64)
                            sext = LLVMBuildSExt(g->builder, search, i64, "se");
                        eq = LLVMBuildICmp(g->builder, LLVMIntEQ, val, sext, "eq");
                    }
                    LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ct.found");
                    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ct.next");
                    LLVMBuildCondBr(g->builder, eq, found_bb, next_bb);
                    LLVMPositionBuilderAtEnd(g->builder, found_bb);
                    LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1, 0), res);
                    LLVMBuildBr(g->builder, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, next_bb);
                    LLVMValueRef ni = LLVMBuildAdd(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                    LLVMBuildStore(g->builder, ni, idx_a);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, done_bb);
                    return LLVMBuildLoad2(g->builder, LLVMInt32TypeInContext(g->ctx), res, "ctres");
                }
            }
        }

        /* List.Insert(index, item) — shift elements right, insert at index */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 6 && memcmp(method_name.str, "Insert", 6) == 0 &&
                expr->call.args.count == 2) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && ltype->name.len == 4 && memcmp(ltype->name.str, "List", 4) == 0) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 1, "capp");
                    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, cap_ptr, "cap");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    LLVMValueRef idx = emit_expr(g, expr->call.args.items[0], locals);
                    LLVMValueRef item = emit_expr(g, expr->call.args.items[1], locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(item)) == LLVMPointerTypeKind)
                        item = LLVMBuildPtrToInt(g->builder, item, i64, "ip");
                    else if (LLVMGetTypeKind(LLVMTypeOf(item)) == LLVMIntegerTypeKind &&
                             LLVMGetIntTypeWidth(LLVMTypeOf(item)) < 64)
                        item = LLVMBuildSExt(g->builder, item, i64, "ie");
                    /* grow if needed */
                    LLVMValueRef need = LLVMBuildICmp(g->builder, LLVMIntUGE, count, cap, "need");
                    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.grow");
                    LLVMBasicBlockRef shift_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.shift");
                    LLVMBuildCondBr(g->builder, need, grow_bb, shift_bb);
                    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
                    LLVMValueRef new_cap = LLVMBuildMul(g->builder, cap, LLVMConstInt(i64, 2, 0), "nc");
                    LLVMBuildStore(g->builder, new_cap, cap_ptr);
                    LLVMValueRef nbytes = LLVMBuildMul(g->builder, new_cap, LLVMConstInt(i64, 8, 0), "nb");
                    LLVMValueRef old_data = LLVMBuildBitCast(g->builder, data, i8ptr, "od");
                    LLVMValueRef new_data = zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
                        g->fn_realloc, (LLVMValueRef[]){ old_data, nbytes }, 2, "nd");
                    LLVMValueRef new_data_i = LLVMBuildBitCast(g->builder, new_data, LLVMPointerType(i64, 0), "ndi");
                    LLVMBuildStore(g->builder, new_data_i, data_field);
                    LLVMBuildBr(g->builder, shift_bb);
                    /* shift elements right from count-1 down to idx */
                    LLVMPositionBuilderAtEnd(g->builder, shift_bb);
                    LLVMValueRef phi_data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "phid");
                    LLVMValueRef j_a = emit_entry_alloca(g, i64, "ij");
                    LLVMBuildStore(g->builder, count, j_a);
                    LLVMBasicBlockRef scond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.scond");
                    LLVMBasicBlockRef sbody_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.sbody");
                    LLVMBasicBlockRef sdone_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.sdone");
                    LLVMBuildBr(g->builder, scond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, scond_bb);
                    LLVMValueRef j = LLVMBuildLoad2(g->builder, i64, j_a, "j");
                    LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntUGT, j, idx, "cmp");
                    LLVMBuildCondBr(g->builder, cmp, sbody_bb, sdone_bb);
                    LLVMPositionBuilderAtEnd(g->builder, sbody_bb);
                    LLVMValueRef j2 = LLVMBuildLoad2(g->builder, i64, j_a, "j2");
                    LLVMValueRef prev = LLVMBuildSub(g->builder, j2, LLVMConstInt(i64, 1, 0), "prev");
                    LLVMValueRef src_slot = LLVMBuildGEP2(g->builder, i64, phi_data, &prev, 1, "ss");
                    LLVMValueRef sv = LLVMBuildLoad2(g->builder, i64, src_slot, "sv");
                    LLVMValueRef dst_slot = LLVMBuildGEP2(g->builder, i64, phi_data, &j2, 1, "ds");
                    LLVMBuildStore(g->builder, sv, dst_slot);
                    LLVMBuildStore(g->builder, prev, j_a);
                    LLVMBuildBr(g->builder, scond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, sdone_bb);
                    /* store item at index. overwrite_old must be 0: Insert
                     * shifts the previous occupant of this slot up to idx+1
                     * where it stays live, so it must not be released here
                     * (doing so frees a still-referenced element). The new
                     * item is still retained by emit_collection_slot_store. */
                    LLVMValueRef ins_slot = LLVMBuildGEP2(g->builder, i64, phi_data, &idx, 1, "is");
                    emit_collection_slot_store(g, container_elem_type(ltype), i64, ins_slot,
                        item, expr->call.args.items[1], locals, 0);
                    LLVMValueRef nc = LLVMBuildAdd(g->builder, count, LLVMConstInt(i64, 1, 0), "nc");
                    LLVMBuildStore(g->builder, nc, count_ptr);
                    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                }
            }
        }

        /* List.Reverse() — in-place reverse */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 7 && memcmp(method_name.str, "Reverse", 7) == 0 &&
                expr->call.args.count == 0) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && ltype->name.len == 4 && memcmp(ltype->name.str, "List", 4) == 0) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    /* two-pointer swap: lo=0, hi=count-1 */
                    LLVMValueRef lo_a = emit_entry_alloca(g, i64, "lo");
                    LLVMValueRef hi_a = emit_entry_alloca(g, i64, "hi");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), lo_a);
                    LLVMValueRef hi_init = LLVMBuildSub(g->builder, count, LLVMConstInt(i64, 1, 0), "hi");
                    LLVMBuildStore(g->builder, hi_init, hi_a);
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rv.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rv.body");
                    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rv.done");
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef lo = LLVMBuildLoad2(g->builder, i64, lo_a, "lo");
                    LLVMValueRef hi = LLVMBuildLoad2(g->builder, i64, hi_a, "hi");
                    LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntULT, lo, hi, "cmp");
                    LLVMBuildCondBr(g->builder, cmp, body_bb, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    LLVMValueRef lo2 = LLVMBuildLoad2(g->builder, i64, lo_a, "lo2");
                    LLVMValueRef hi2 = LLVMBuildLoad2(g->builder, i64, hi_a, "hi2");
                    LLVMValueRef lo_slot = LLVMBuildGEP2(g->builder, i64, data, &lo2, 1, "ls");
                    LLVMValueRef hi_slot = LLVMBuildGEP2(g->builder, i64, data, &hi2, 1, "hs");
                    LLVMValueRef lv = LLVMBuildLoad2(g->builder, i64, lo_slot, "lv");
                    LLVMValueRef hv = LLVMBuildLoad2(g->builder, i64, hi_slot, "hv");
                    LLVMBuildStore(g->builder, hv, lo_slot);
                    LLVMBuildStore(g->builder, lv, hi_slot);
                    LLVMBuildStore(g->builder, LLVMBuildAdd(g->builder, lo2, LLVMConstInt(i64, 1, 0), "lo3"), lo_a);
                    LLVMBuildStore(g->builder, LLVMBuildSub(g->builder, hi2, LLVMConstInt(i64, 1, 0), "hi3"), hi_a);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, done_bb);
                    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                }
            }
        }


                /* Dict method calls: Add, ContainsKey */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee_d = expr->call.callee;
            zan_istr_t mname = callee_d->member.name;
            if (callee_d->member.object->kind == AST_IDENTIFIER) {
                local_var_t *dict_local = local_find(locals, callee_d->member.object->ident.name);
                if (dict_local && dict_local->type && dict_local->type->name.len == 4 &&
                    memcmp(dict_local->type->name.str, "Dict", 4) == 0) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw = LLVMBuildLoad2(g->builder, i8ptr, dict_local->alloca, "draw");
                    LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                        LLVMPointerType(g->dict_struct_type, 0), "dp");

                    if (mname.len == 3 && memcmp(mname.str, "Add", 3) == 0 && expr->call.args.count == 2) {
                        LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                        LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
                        LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
                        LLVMValueRef ks = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), kp, "ks");
                        LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
                        LLVMValueRef vs = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp, "vs");
                        LLVMValueRef key_val = emit_expr(g, expr->call.args.items[0], locals);
                        LLVMValueRef val_v = emit_expr(g, expr->call.args.items[1], locals);
                        LLVMValueRef kslot = LLVMBuildGEP2(g->builder, i8ptr, ks, &cnt, 1, "ksl");
                        LLVMValueRef vslot = LLVMBuildGEP2(g->builder, i64, vs, &cnt, 1, "vsl");
                        emit_collection_slot_store(g, dict_key_type(g, dict_local->type), i8ptr, kslot,
                            key_val, expr->call.args.items[0], locals, 0);
                        emit_collection_slot_store(g, dict_value_type(dict_local->type), i64, vslot,
                            val_v, expr->call.args.items[1], locals, 0);
                        LLVMValueRef nc = LLVMBuildAdd(g->builder, cnt, LLVMConstInt(i64, 1, 0), "nc");
                        LLVMBuildStore(g->builder, nc, cntp);
                        return LLVMConstInt(i32t, 0, 0);
                    }

                    if (mname.len == 11 && memcmp(mname.str, "ContainsKey", 11) == 0 && expr->call.args.count == 1) {
                        LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                        LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
                        LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
                        LLVMValueRef ks = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), kp, "ks");
                        LLVMValueRef search = coerce_dict_key(g, emit_expr(g, expr->call.args.items[0], locals));
                        /* result alloca */
                        LLVMValueRef res = emit_entry_alloca(g, i32t, "ckr");
                        LLVMBuildStore(g->builder, LLVMConstInt(i32t, 0, 0), res);
                        LLVMValueRef idx_a = emit_entry_alloca(g, i64, "di");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ck.cond");
                        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ck.body");
                        LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ck.done");
                        LLVMBuildBr(g->builder, cond_bb);
                        /* cond: i < count? */
                        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                        LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                        LLVMValueRef cmp_done = LLVMBuildICmp(g->builder, LLVMIntUGE, ci, cnt, "cdone");
                        LLVMBuildCondBr(g->builder, cmp_done, done_bb, body_bb);
                        /* body: compare keys[i] with search key */
                        LLVMPositionBuilderAtEnd(g->builder, body_bb);
                        LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                        LLVMValueRef kslot = LLVMBuildGEP2(g->builder, i8ptr, ks, &ci2, 1, "ksl");
                        LLVMValueRef kv = LLVMBuildLoad2(g->builder, i8ptr, kslot, "kv");
                        LLVMValueRef eq = emit_dict_key_eq(g, dict_local->type, kv, search);
                        LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ck.found");
                        LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ck.next");
                        LLVMBuildCondBr(g->builder, eq, found_bb, next_bb);
                        /* found: set result = 1, jump to done */
                        LLVMPositionBuilderAtEnd(g->builder, found_bb);
                        LLVMBuildStore(g->builder, LLVMConstInt(i32t, 1, 0), res);
                        LLVMBuildBr(g->builder, done_bb);
                        /* next: i++, loop back */
                        LLVMPositionBuilderAtEnd(g->builder, next_bb);
                        LLVMValueRef ni = LLVMBuildAdd(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                        LLVMBuildStore(g->builder, ni, idx_a);
                        LLVMBuildBr(g->builder, cond_bb);
                        /* done: return result */
                        LLVMPositionBuilderAtEnd(g->builder, done_bb);
                        return LLVMBuildLoad2(g->builder, i32t, res, "ckres");
                    }
                    if (mname.len == 5 && memcmp(mname.str, "Clear", 5) == 0 && expr->call.args.count == 0) {
                        LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                        LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
                        LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
                        LLVMValueRef ks = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), kp, "ks");
                        LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
                        LLVMValueRef vs = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp, "vs");
                        LLVMValueRef idx_a = emit_entry_alloca(g, i64, "dc");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                        LLVMBasicBlockRef cbb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.cond");
                        LLVMBasicBlockRef bbb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.body");
                        LLVMBasicBlockRef dbb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.done");
                        LLVMBuildBr(g->builder, cbb);
                        LLVMPositionBuilderAtEnd(g->builder, cbb);
                        LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                        LLVMValueRef more = LLVMBuildICmp(g->builder, LLVMIntULT, ci, cnt, "more");
                        LLVMBuildCondBr(g->builder, more, bbb, dbb);
                        LLVMPositionBuilderAtEnd(g->builder, bbb);
                        LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                        LLVMValueRef kslot = LLVMBuildGEP2(g->builder, i8ptr, ks, &ci2, 1, "ksl");
                        LLVMValueRef kv = LLVMBuildLoad2(g->builder, i8ptr, kslot, "kv");
                        emit_collection_release_raw_slot(g, dict_key_type(g, dict_local->type), kv, i8ptr);
                        LLVMValueRef vslot = LLVMBuildGEP2(g->builder, i64, vs, &ci2, 1, "vsl");
                        LLVMValueRef vv = LLVMBuildLoad2(g->builder, i64, vslot, "vv");
                        emit_collection_release_raw_slot(g, dict_value_type(dict_local->type), vv, i64);
                        LLVMValueRef ni = LLVMBuildAdd(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                        LLVMBuildStore(g->builder, ni, idx_a);
                        LLVMBuildBr(g->builder, cbb);
                        LLVMPositionBuilderAtEnd(g->builder, dbb);
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), cntp);
                        return LLVMConstInt(i32t, 0, 0);
                    }
                }
            }
        }

        /* Dict.Clear() — reset count to 0 */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee_d = expr->call.callee;
            zan_istr_t mname = callee_d->member.name;
            if (mname.len == 5 && memcmp(mname.str, "Clear", 5) == 0 && expr->call.args.count == 0) {
                if (callee_d->member.object->kind == AST_IDENTIFIER) {
                    local_var_t *dict_local = local_find(locals, callee_d->member.object->ident.name);
                    if (dict_local && dict_local->type && dict_local->type->name.len == 4 &&
                        memcmp(dict_local->type->name.str, "Dict", 4) == 0) {
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMValueRef raw = LLVMBuildLoad2(g->builder, i8ptr, dict_local->alloca, "draw");
                        LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                            LLVMPointerType(g->dict_struct_type, 0), "dp");
                        LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), cntp);
                        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                    }
                }
            }
        }

        /* Dict.Remove(key) — find and remove key, shift remaining entries */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee_d = expr->call.callee;
            zan_istr_t mname = callee_d->member.name;
            if (mname.len == 6 && memcmp(mname.str, "Remove", 6) == 0 && expr->call.args.count == 1) {
                if (callee_d->member.object->kind == AST_IDENTIFIER) {
                    local_var_t *dict_local = local_find(locals, callee_d->member.object->ident.name);
                    if (dict_local && dict_local->type && dict_local->type->name.len == 4 &&
                        memcmp(dict_local->type->name.str, "Dict", 4) == 0) {
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMValueRef raw = LLVMBuildLoad2(g->builder, i8ptr, dict_local->alloca, "draw");
                        LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                            LLVMPointerType(g->dict_struct_type, 0), "dp");
                        LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                        LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
                        LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
                        LLVMValueRef ks = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), kp, "ks");
                        LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
                        LLVMValueRef vs = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp, "vs");
                        LLVMValueRef search = coerce_dict_key(g, emit_expr(g, expr->call.args.items[0], locals));
                        /* linear search for key */
                        LLVMValueRef idx_a = emit_entry_alloca(g, i64, "di");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.cond");
                        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.body");
                        LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.found");
                        LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.next");
                        LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.done");
                        LLVMBuildBr(g->builder, cond_bb);
                        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                        LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                        LLVMBuildCondBr(g->builder, LLVMBuildICmp(g->builder, LLVMIntUGE, ci, cnt, "cdone"), done_bb, body_bb);
                        LLVMPositionBuilderAtEnd(g->builder, body_bb);
                        LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                        LLVMValueRef kslot = LLVMBuildGEP2(g->builder, i8ptr, ks, &ci2, 1, "ksl");
                        LLVMValueRef kv = LLVMBuildLoad2(g->builder, i8ptr, kslot, "kv");
                        LLVMValueRef eq = emit_dict_key_eq(g, dict_local->type, kv, search);
                        LLVMBuildCondBr(g->builder, eq, found_bb, next_bb);
                        /* found: shift remaining entries left */
                        LLVMPositionBuilderAtEnd(g->builder, found_bb);
                        LLVMValueRef fi = LLVMBuildLoad2(g->builder, i64, idx_a, "fi");
                        LLVMValueRef last = LLVMBuildSub(g->builder, cnt, LLVMConstInt(i64, 1, 0), "last");
                        LLVMValueRef rkey = LLVMBuildGEP2(g->builder, i8ptr, ks, &fi, 1, "rkey");
                        LLVMValueRef rkv = LLVMBuildLoad2(g->builder, i8ptr, rkey, "rkv");
                        emit_collection_release_raw_slot(g, dict_key_type(g, dict_local->type), rkv, i8ptr);
                        LLVMValueRef rvslot = LLVMBuildGEP2(g->builder, i64, vs, &fi, 1, "rvslot");
                        LLVMValueRef rv = LLVMBuildLoad2(g->builder, i64, rvslot, "rv");
                        emit_collection_release_raw_slot(g, dict_value_type(dict_local->type), rv, i64);
                        LLVMValueRef j_a = emit_entry_alloca(g, i64, "fj");
                        LLVMBuildStore(g->builder, fi, j_a);
                        LLVMBasicBlockRef sc_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.sc");
                        LLVMBasicBlockRef sb_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.sb");
                        LLVMBasicBlockRef sd_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.sd");
                        LLVMBuildBr(g->builder, sc_bb);
                        LLVMPositionBuilderAtEnd(g->builder, sc_bb);
                        LLVMValueRef j = LLVMBuildLoad2(g->builder, i64, j_a, "j");
                        LLVMBuildCondBr(g->builder, LLVMBuildICmp(g->builder, LLVMIntULT, j, last, "jlt"), sb_bb, sd_bb);
                        LLVMPositionBuilderAtEnd(g->builder, sb_bb);
                        LLVMValueRef j2 = LLVMBuildLoad2(g->builder, i64, j_a, "j2");
                        LLVMValueRef nxt = LLVMBuildAdd(g->builder, j2, LLVMConstInt(i64, 1, 0), "nxt");
                        /* shift key */
                        LLVMValueRef ksrc = LLVMBuildGEP2(g->builder, i8ptr, ks, &nxt, 1, "ksrc");
                        LLVMValueRef kdst = LLVMBuildGEP2(g->builder, i8ptr, ks, &j2, 1, "kdst");
                        LLVMBuildStore(g->builder, LLVMBuildLoad2(g->builder, i8ptr, ksrc, "ksv"), kdst);
                        /* shift value */
                        LLVMValueRef vsrc = LLVMBuildGEP2(g->builder, i64, vs, &nxt, 1, "vsrc");
                        LLVMValueRef vdst = LLVMBuildGEP2(g->builder, i64, vs, &j2, 1, "vdst");
                        LLVMBuildStore(g->builder, LLVMBuildLoad2(g->builder, i64, vsrc, "vsv"), vdst);
                        LLVMBuildStore(g->builder, nxt, j_a);
                        LLVMBuildBr(g->builder, sc_bb);
                        LLVMPositionBuilderAtEnd(g->builder, sd_bb);
                        LLVMBuildStore(g->builder, last, cntp);
                        LLVMValueRef ktail = LLVMBuildGEP2(g->builder, i8ptr, ks, &last, 1, "ktail");
                        LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), ktail);
                        LLVMValueRef vtail = LLVMBuildGEP2(g->builder, i64, vs, &last, 1, "vtail");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), vtail);
                        LLVMBuildBr(g->builder, done_bb);
                        /* next: increment and loop */
                        LLVMPositionBuilderAtEnd(g->builder, next_bb);
                        LLVMValueRef ni = LLVMBuildAdd(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                        LLVMBuildStore(g->builder, ni, idx_a);
                        LLVMBuildBr(g->builder, cond_bb);
                        LLVMPositionBuilderAtEnd(g->builder, done_bb);
                        return LLVMConstInt(i32t, 0, 0);
                    }
                }
            }
        }


        /* Delegate invocation. Unlike methods, a delegate callee can be a
         * local, an implicit/explicit instance field, a static field or a
         * nested field expression. Resolve the callee's value type first and
         * emit one indirect-call path for all of those forms. */
        {
            zan_type_t *delegate_type =
                infer_expr_type(g, expr->call.callee, locals);
            if (delegate_type &&
                delegate_type->kind == TYPE_DELEGATE) {
                LLVMValueRef fn_ptr =
                    emit_expr(g, expr->call.callee, locals);
                return emit_delegate_call(
                    g, delegate_type, fn_ptr, expr, locals);
            }
        }

                /* user-defined method call: obj.Method(args) or Type.StaticMethod(args) */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            if (callee->member.object->kind == AST_IDENTIFIER) {
                /* first try as instance method: local_var.Method(args) */
                local_var_t *local = local_find(locals, callee->member.object->ident.name);
                if (local && local->type && local->type->sym) {
                    zan_symbol_t *type_sym = local->type->sym;
                    zan_symbol_t *method_sym = resolve_overload(type_sym, callee->member.name, expr->call.args.count);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                    if (method_sym) {
                        for (int fi = 0; fi < g->function_count; fi++) {
                            if (g->functions[fi].sym == method_sym) {
                                int argc = expr->call.args.count + 1;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                                /* receiver: class refs hold the object pointer in
                                 * the local, so load it; struct value types pass
                                 * the storage address directly. */
                                LLVMTypeRef at = local_slot_type(g, local);
                                if (LLVMGetTypeKind(at) == LLVMPointerTypeKind) {
                                    call_args[0] = LLVMBuildLoad2(g->builder, at, local->alloca, "recv");
                                } else {
                                    call_args[0] = local->alloca;
                                }
                                for (int k = 0; k < expr->call.args.count; k++) {
                                    call_args[k + 1] = emit_arg_typed(g, expr->call.args.items[k],
                                        method_param_type(g, method_sym, k), locals);
                                }
                                LLVMTypeRef mft = g->functions[fi].fn_type;
                                LLVMValueRef mfn = route_generic_method(g, local->type,
                                    method_sym, g->functions[fi].fn, mft, &mft);
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "mcall";
                                LLVMValueRef result = emit_dispatch_call(g, type_sym, method_sym,
                                    mfn, mft, call_args, argc, cn);
                                result = coerce_generic_result(g, result, method_sym, local->type);
                                for (int k = 0; k < expr->call.args.count; k++) {
                                    emit_release_owned_call_temp(g, expr->call.args.items[k],
                                        call_args[k + 1], locals);
                                }
                                free(call_args);
                                return result;
                            }
                        }
                    }
                }

                /* try as static method: ClassName.Method(args) */
                zan_symbol_t *type_sym = zan_binder_lookup(g->binder, callee->member.object->ident.name);
                if (type_sym && (type_sym->kind == SYM_CLASS || type_sym->kind == SYM_STRUCT)) {
                    zan_symbol_t *method_sym = resolve_overload(type_sym, callee->member.name, expr->call.args.count);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                    if (method_sym) {
                        for (int fi = 0; fi < g->function_count; fi++) {
                            if (g->functions[fi].sym == method_sym) {
                                int argc = expr->call.args.count;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                                for (int k = 0; k < argc; k++) {
                                    call_args[k] = emit_arg_typed(g, expr->call.args.items[k],
                                        method_param_type_at(g, method_sym, k, expr, NULL, locals), locals);
                                }
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(g->functions[fi].fn_type)) == LLVMVoidTypeKind) ? "" : "scall";
                                coerce_args_to_params(g, g->functions[fi].fn_type, call_args, argc);
                                LLVMValueRef result = zan_call2(g->builder, g->functions[fi].fn_type,
                                    g->functions[fi].fn, call_args, (unsigned)argc, cn);
                                zan_type_t *gret = generic_method_ret(g, method_sym, expr, locals);
                                if (!gret && method_ret_is_bare_tp(method_sym)) {
                                    zan_type_t *ir = method_ret_type_at(g, method_sym,
                                        expr, NULL, locals);
                                    if (ir && ir->kind != TYPE_TYPE_PARAM) gret = ir;
                                }
                                if (gret) {
                                    result = emit_boundary_coerce(g, result, map_type(g, gret));
                                    /* the erased body returned a borrowed +0
                                     * class reference; make it owned like
                                     * every other call result */
                                    if (gret->kind == TYPE_CLASS &&
                                        method_ret_is_bare_tp(method_sym))
                                        emit_arc_retain(g, result);
                                }
                                int consumes_free_arg =
                                    argc == 1 && call_consumes_free_arg(g->functions[fi].fn);
                                if (consumes_free_arg) {
                                    emit_invalidate_freed_string(g,
                                        expr->call.args.items[0], locals);
                                }
                                for (int k = 0; k < argc; k++) {
                                    if (!consumes_free_arg || k != 0) {
                                        emit_release_owned_call_temp(g, expr->call.args.items[k],
                                            call_args[k], locals);
                                    }
                                }
                                free(call_args);
                                return result;
                            }
                        }
                    }
                }
            }
        }

        /* general instance method call: <expr>.Method(args) where <expr> is
         * any expression of class/struct type (a field, this.field, an index
         * or a call result) — not just a local variable or class name. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_symbol_t *recv_cls = expr_class_sym(g, callee->member.object, locals);
            if (recv_cls) {
                zan_symbol_t *method_sym = resolve_overload(recv_cls, callee->member.name, expr->call.args.count);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                if (method_sym) {
                    for (int fi = 0; fi < g->function_count; fi++) {
                        if (g->functions[fi].sym == method_sym) {
                            int argc = expr->call.args.count + 1;
                            LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                            LLVMValueRef recv_val = emit_expr(g, callee->member.object, locals);
                            /* receiver: the object pointer produced by the
                             * expression (field load, index, call, ...). */
                            call_args[0] = recv_val;
                            /* an owned receiver temp must survive a throwing
                             * callee: keep it on the EH temp stack so a catch
                             * can release it (longjmp skips the release below) */
                            int recv_eh_pushed = 0;
                            {
                                zan_type_t *rty = infer_expr_type(g, callee->member.object, locals);
                                if (rty && rty->kind == TYPE_CLASS &&
                                    !expr_is_local_ident(callee->member.object, locals) &&
                                    expr_yields_owned_rc_value(g, callee->member.object, locals) &&
                                    LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMPointerTypeKind) {
                                    emit_eh_tmp_push(g, recv_val);
                                    recv_eh_pushed = 1;
                                }
                            }
                            for (int k = 0; k < expr->call.args.count; k++) {
                                call_args[k + 1] = emit_arg_typed(g, expr->call.args.items[k],
                                    method_param_type(g, method_sym, k), locals);
                            }
                            zan_type_t *recv_ty = infer_expr_type(g, callee->member.object, locals);
                            LLVMTypeRef mft = g->functions[fi].fn_type;
                            LLVMValueRef mfn = route_generic_method(g, recv_ty,
                                method_sym, g->functions[fi].fn, mft, &mft);
                            const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "mcall";
                            LLVMValueRef result = emit_dispatch_call(g, recv_cls, method_sym,
                                mfn, mft, call_args, argc, cn);
                            result = coerce_generic_result(g, result, method_sym, recv_ty);
                            if (recv_eh_pushed) emit_eh_tmp_pop(g);
                            emit_release_owned_call_temp(g, callee->member.object, recv_val, locals);
                            for (int k = 0; k < expr->call.args.count; k++) {
                                emit_release_owned_call_temp(g, expr->call.args.items[k],
                                    call_args[k + 1], locals);
                            }
                            free(call_args);
                            return result;
                        }
                    }
                }
            }
        }

        /* interface method dispatch: <expr>.Method(args) where <expr>'s static
         * type is an interface. The concrete class is unknown at compile time,
         * so compare the object's field-0 vtable pointer (a per-class tag)
         * against every class implementing the interface and call the matching
         * implementation. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_type_t *obj_ty = infer_expr_type(g, callee->member.object, locals);
            if (obj_ty && obj_ty->kind == TYPE_INTERFACE && obj_ty->sym && g->current_fn) {
                zan_symbol_t *iface = obj_ty->sym;
                zan_symbol_t *iface_m = resolve_overload(iface, callee->member.name,
                                                         expr->call.args.count);
                if (iface_m) {
                    LLVMContextRef c = g->ctx;
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
                    zan_type_t *rty = NULL;
                    if (iface_m->decl && iface_m->decl->kind == AST_METHOD_DECL &&
                        iface_m->decl->method_decl.return_type)
                        rty = zan_binder_resolve_type(g->binder,
                                  iface_m->decl->method_decl.return_type);
                    bool has_res = rty && rty->kind != TYPE_VOID;
                    LLVMTypeRef res_ty = has_res ? map_type(g, rty) : NULL;

                    int uargc = expr->call.args.count;
                    /* emit receiver + argument values once, before branching */
                    LLVMValueRef recv = emit_expr(g, callee->member.object, locals);
                    LLVMValueRef *avals = (LLVMValueRef *)calloc((size_t)(uargc > 0 ? uargc : 1),
                                                                sizeof(LLVMValueRef));
                    for (int k = 0; k < uargc; k++)
                        avals[k] = emit_arg_typed(g, expr->call.args.items[k],
                                                  method_param_type(g, iface_m, k), locals);
                    LLVMValueRef recv_pp = LLVMBuildBitCast(g->builder, recv,
                                              LLVMPointerType(i8ptr, 0), "ifc.recvpp");
                    LLVMValueRef tag = LLVMBuildLoad2(g->builder, i8ptr, recv_pp, "ifc.tag");

                    LLVMBasicBlockRef merge = LLVMAppendBasicBlockInContext(c, g->current_fn, "ifc.merge");
                    int cap = g->struct_type_count + 1;
                    LLVMValueRef *phi_vals = (LLVMValueRef *)calloc((size_t)cap, sizeof(LLVMValueRef));
                    LLVMBasicBlockRef *phi_bbs = (LLVMBasicBlockRef *)calloc((size_t)cap, sizeof(LLVMBasicBlockRef));
                    int np = 0;

                    for (int si = 0; si < g->struct_type_count; si++) {
                        zan_symbol_t *cls = g->struct_types[si].sym;
                        if (!cls || !class_implements_iface(cls, iface)) continue;
                        zan_symbol_t *impl_m = resolve_overload(cls, callee->member.name, uargc);
                        if (!impl_m) continue;
                        LLVMValueRef ifn = NULL; LLVMTypeRef ifnty = NULL;
                        for (int fi = 0; fi < g->function_count; fi++)
                            if (g->functions[fi].sym == impl_m) {
                                ifn = g->functions[fi].fn; ifnty = g->functions[fi].fn_type; break;
                            }
                        if (!ifn || !ifnty) continue;

                        LLVMBasicBlockRef callbb = LLVMAppendBasicBlockInContext(c, g->current_fn, "ifc.call");
                        LLVMBasicBlockRef nextbb = LLVMAppendBasicBlockInContext(c, g->current_fn, "ifc.next");
                        LLVMValueRef tagc = LLVMConstBitCast(get_vtable_global(g, cls), i8ptr);
                        LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntEQ, tag, tagc, "ifc.eq");
                        LLVMBuildCondBr(g->builder, cmp, callbb, nextbb);

                        LLVMPositionBuilderAtEnd(g->builder, callbb);
                        unsigned npar = LLVMCountParamTypes(ifnty);
                        LLVMTypeRef *pts = (LLVMTypeRef *)calloc((size_t)(npar > 0 ? npar : 1), sizeof(LLVMTypeRef));
                        LLVMGetParamTypes(ifnty, pts);
                        int cargc = uargc + 1;
                        LLVMValueRef *ca = (LLVMValueRef *)calloc((size_t)cargc, sizeof(LLVMValueRef));
                        ca[0] = (npar > 0) ? LLVMBuildBitCast(g->builder, recv, pts[0], "ifc.this") : recv;
                        for (int k = 0; k < uargc; k++)
                            ca[k + 1] = (k + 1 < (int)npar)
                                ? emit_boundary_coerce(g, avals[k], pts[k + 1]) : avals[k];
                        const char *cn = has_res ? "ifccall" : "";
                        LLVMValueRef r = zan_call2(g->builder, ifnty, ifn, ca, (unsigned)cargc, cn);
                        if (has_res) {
                            r = emit_boundary_coerce(g, r, res_ty);
                            phi_vals[np] = r; phi_bbs[np] = LLVMGetInsertBlock(g->builder); np++;
                        }
                        LLVMBuildBr(g->builder, merge);
                        free(ca); free(pts);
                        LLVMPositionBuilderAtEnd(g->builder, nextbb);
                    }

                    /* no matching class: yield a null/zero result */
                    if (has_res) {
                        phi_vals[np] = LLVMConstNull(res_ty);
                        phi_bbs[np] = LLVMGetInsertBlock(g->builder); np++;
                    }
                    LLVMBuildBr(g->builder, merge);
                    LLVMPositionBuilderAtEnd(g->builder, merge);
                    LLVMValueRef result;
                    if (has_res) {
                        LLVMValueRef phi = LLVMBuildPhi(g->builder, res_ty, "ifc.res");
                        LLVMAddIncoming(phi, phi_vals, phi_bbs, (unsigned)np);
                        result = phi;
                    } else {
                        result = LLVMConstInt(LLVMInt32TypeInContext(c), 0, 0);
                    }
                    /* the merge block post-dominates every dispatch arm, so
                     * owned receiver/argument temps are released once here. */
                    for (int k = 0; k < uargc; k++)
                        emit_release_owned_call_temp(g, expr->call.args.items[k],
                                                     avals[k], locals);
                    emit_release_owned_call_temp(g, callee->member.object, recv,
                                                 locals);
                    free(avals); free(phi_vals); free(phi_bbs);
                    return result;
                }
            }
        }

        /* namespace-qualified static call: Foo.Bar.Widget.Method(args).
         * The callee's object is a name path (Foo.Bar.Widget) rather than a
         * bare class identifier, so the AST_IDENTIFIER static branch above
         * misses it. Types are registered by simple name, so the rightmost
         * path segment is the type name. Guard on the path head not being a
         * local so genuine instance chains (a.b.Method()) fall through to the
         * instance handlers above. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_ast_node_t *obj = callee->member.object;
            zan_ast_node_t *head = name_path_head(obj);
            if (is_name_path(obj) && head && !local_find(locals, head->ident.name)) {
                zan_symbol_t *type_sym = zan_binder_lookup(g->binder, obj->member.name);
                if (type_sym && (type_sym->kind == SYM_CLASS || type_sym->kind == SYM_STRUCT)) {
                    zan_symbol_t *method_sym = resolve_overload(type_sym, callee->member.name, expr->call.args.count);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                    if (method_sym) {
                        for (int fi = 0; fi < g->function_count; fi++) {
                            if (g->functions[fi].sym == method_sym) {
                                int argc = expr->call.args.count;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                                for (int k = 0; k < argc; k++) {
                                    call_args[k] = emit_arg_typed(g, expr->call.args.items[k],
                                        method_param_type_at(g, method_sym, k, expr, NULL, locals), locals);
                                }
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(g->functions[fi].fn_type)) == LLVMVoidTypeKind) ? "" : "scall";
                                coerce_args_to_params(g, g->functions[fi].fn_type, call_args, argc);
                                LLVMValueRef result = zan_call2(g->builder, g->functions[fi].fn_type,
                                    g->functions[fi].fn, call_args, (unsigned)argc, cn);
                                zan_type_t *gret = generic_method_ret(g, method_sym, expr, locals);
                                if (!gret && method_ret_is_bare_tp(method_sym)) {
                                    zan_type_t *ir = method_ret_type_at(g, method_sym,
                                        expr, NULL, locals);
                                    if (ir && ir->kind != TYPE_TYPE_PARAM) gret = ir;
                                }
                                if (gret) {
                                    result = emit_boundary_coerce(g, result, map_type(g, gret));
                                    /* the erased body returned a borrowed +0
                                     * class reference; make it owned like
                                     * every other call result */
                                    if (gret->kind == TYPE_CLASS &&
                                        method_ret_is_bare_tp(method_sym))
                                        emit_arc_retain(g, result);
                                }
                                int consumes_free_arg =
                                    argc == 1 && call_consumes_free_arg(g->functions[fi].fn);
                                if (consumes_free_arg) {
                                    emit_invalidate_freed_string(g,
                                        expr->call.args.items[0], locals);
                                }
                                for (int k = 0; k < argc; k++) {
                                    if (!consumes_free_arg || k != 0) {
                                        emit_release_owned_call_temp(g, expr->call.args.items[k],
                                            call_args[k], locals);
                                    }
                                }
                                free(call_args);
                                return result;
                            }
                        }
                    }
                }
            }
        }

        /* extension method call: recv.M(args) lowers to the static method
         * Ext.M(recv, args) whose first parameter is declared `this T`. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_type_t *recv_ty = infer_expr_type(g, callee->member.object, locals);
            zan_symbol_t *method_sym =
                find_extension_method(g, recv_ty, callee->member.name,
                                      expr->call.args.count);
            if (method_sym) {
                for (int fi = 0; fi < g->function_count; fi++) {
                    if (g->functions[fi].sym == method_sym) {
                        int argc = expr->call.args.count + 1;
                        LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                        call_args[0] = emit_arg_typed(g, callee->member.object,
                            method_param_type(g, method_sym, 0), locals);
                        for (int k = 0; k < expr->call.args.count; k++) {
                            call_args[k + 1] = emit_arg_typed(g, expr->call.args.items[k],
                                method_param_type_at(g, method_sym, k + 1, expr,
                                                     callee->member.object, locals), locals);
                        }
                        const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(g->functions[fi].fn_type)) == LLVMVoidTypeKind) ? "" : "extcall";
                        coerce_args_to_params(g, g->functions[fi].fn_type, call_args, argc);
                        LLVMValueRef result = zan_call2(g->builder, g->functions[fi].fn_type,
                            g->functions[fi].fn, call_args, (unsigned)argc, cn);
                        /* generic ext method returning bare T: coerce the
                         * erased result to the inferred concrete type and, for
                         * classes, own the borrowed +0 reference the erased
                         * body returned */
                        if (method_ret_is_bare_tp(method_sym)) {
                            zan_type_t *gret = method_ret_type_at(g, method_sym,
                                expr, callee->member.object, locals);
                            if (gret && gret->kind != TYPE_TYPE_PARAM) {
                                result = emit_boundary_coerce(g, result, map_type(g, gret));
                                if (gret->kind == TYPE_CLASS)
                                    emit_arc_retain(g, result);
                            }
                        }
                        emit_release_owned_call_temp(g, callee->member.object, call_args[0], locals);
                        for (int k = 0; k < expr->call.args.count; k++) {
                            emit_release_owned_call_temp(g, expr->call.args.items[k],
                                call_args[k + 1], locals);
                        }
                        free(call_args);
                        return result;
                    }
                }
            }
        }

        /* bare function name call: Compute(21) → look up in current class then global */
        if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER) {
            zan_istr_t fn_name = expr->call.callee->ident.name;

            /* try current class methods first */
            if (g->current_type_sym) {
                zan_symbol_t *method_sym = resolve_overload(g->current_type_sym, fn_name, expr->call.args.count);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                if (method_sym) {
                    for (int fi = 0; fi < g->function_count; fi++) {
                        if (g->functions[fi].sym == method_sym) {
                            int argc = expr->call.args.count;
                            bool is_static = (method_sym->modifiers & MOD_STATIC) != 0;
                            int extra = is_static ? 0 : 1;
                            LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                                (size_t)(argc + extra > 0 ? argc + extra : 1), sizeof(LLVMValueRef));
                            if (!is_static && g->current_this) {
                                /* load `this` using the receiver's actual
                                 * pointer type, then make it agree with the
                                 * callee's first parameter type (they can
                                 * differ, e.g. struct* vs i8*). */
                                LLVMTypeRef this_ty = LLVMGetAllocatedType(g->current_this);
                                LLVMValueRef this_val = LLVMBuildLoad2(g->builder,
                                    this_ty, g->current_this, "this");
                                unsigned np = LLVMCountParamTypes(g->functions[fi].fn_type);
                                if (np > 0) {
                                    LLVMTypeRef *pts = (LLVMTypeRef *)calloc(np, sizeof(LLVMTypeRef));
                                    LLVMGetParamTypes(g->functions[fi].fn_type, pts);
                                    if (LLVMTypeOf(this_val) != pts[0]) {
                                        this_val = LLVMBuildBitCast(g->builder, this_val, pts[0], "thisc");
                                    }
                                    free(pts);
                                }
                                call_args[0] = this_val;
                            }
                            for (int k = 0; k < argc; k++) {
                                call_args[k + extra] = emit_expr(g, expr->call.args.items[k], locals);
                            }
                            /* self-call inside a specialized variant stays in
                             * the same instantiation (receiver is `this`). */
                            LLVMTypeRef mft = g->functions[fi].fn_type;
                            LLVMValueRef mfn = route_generic_method(g, g->cur_inst,
                                method_sym, g->functions[fi].fn, mft, &mft);
                            const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "bcall";
                            LLVMValueRef result = emit_dispatch_call(g,
                                is_static ? NULL : g->current_type_sym, method_sym,
                                mfn, mft, call_args, argc + extra, cn);
                            int consumes_free_arg =
                                argc == 1 && call_consumes_free_arg(g->functions[fi].fn);
                            if (consumes_free_arg) {
                                emit_invalidate_freed_string(g,
                                    expr->call.args.items[0], locals);
                            }
                            for (int k = 0; k < argc; k++) {
                                if (!consumes_free_arg || k != 0) {
                                    emit_release_owned_call_temp(g, expr->call.args.items[k],
                                        call_args[k + extra], locals);
                                }
                            }
                            free(call_args);
                            return result;
                        }
                    }
                }
            }

            /* try global LLVM function by name */
            char name_buf[256];
            int nlen = fn_name.len < 255 ? fn_name.len : 255;
            memcpy(name_buf, fn_name.str, (size_t)nlen);
            name_buf[nlen] = '\0';
            LLVMValueRef global_fn = LLVMGetNamedFunction(g->mod, name_buf);
            if (global_fn) {
                int argc = expr->call.args.count;
                LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                    (size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                for (int k = 0; k < argc; k++) {
                    call_args[k] = emit_expr(g, expr->call.args.items[k], locals);
                }
                LLVMTypeRef fn_type = LLVMGlobalGetValueType(global_fn);
                const char *gcn = (LLVMGetTypeKind(LLVMGetReturnType(fn_type)) == LLVMVoidTypeKind) ? "" : "gcall";
                LLVMValueRef result = zan_call2(g->builder, fn_type,
                    global_fn, call_args, (unsigned)argc, gcn);
                int consumes_free_arg =
                    argc == 1 && call_consumes_free_arg(global_fn);
                if (consumes_free_arg) {
                    emit_invalidate_freed_string(g,
                        expr->call.args.items[0], locals);
                }
                for (int k = 0; k < argc; k++) {
                    if (!consumes_free_arg || k != 0) {
                        emit_release_owned_call_temp(g, expr->call.args.items[k],
                            call_args[k], locals);
                    }
                }
                free(call_args);
                return result;
            }
        }

        /* Robustness: a call `obj.Method(...)` on a known class/struct type
         * where no member of that name exists anywhere in the class or its
         * base chain. Historically this silently lowered to a constant 0
         * (e.g. a missing IsOpen() "returned" false), which is very hard to
         * diagnose. Members that do exist (delegate fields, arity-mismatched
         * overloads) are left to the later paths / LLVM verification. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_symbol_t *recv_cls = expr_class_sym(g, callee->member.object, locals);
            if (!recv_cls && callee->member.object->kind == AST_IDENTIFIER &&
                !local_find(locals, callee->member.object->ident.name)) {
                /* static call ClassName.Method(...) */
                zan_symbol_t *ts = zan_binder_lookup(g->binder, callee->member.object->ident.name);
                if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT)) recv_cls = ts;
            }
            if (recv_cls && (recv_cls->kind == SYM_CLASS || recv_cls->kind == SYM_STRUCT)) {
                zan_istr_t mn = callee->member.name;
                int found = 0;
                zan_symbol_t *cur = recv_cls;
                while (cur && !found) {
                    for (int mi = 0; mi < cur->member_count; mi++) {
                        zan_symbol_t *m = cur->members[mi];
                        if (m && m->name.len == mn.len &&
                            memcmp(m->name.str, mn.str, mn.len) == 0) { found = 1; break; }
                    }
                    zan_symbol_t *base = (cur->type && cur->type->base_type)
                        ? cur->type->base_type->sym : NULL;
                    cur = (base && base != cur) ? base : NULL;
                }
                if (!found) {
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "'%.*s' has no member '%.*s'",
                        (int)recv_cls->name.len, recv_cls->name.str,
                        (int)mn.len, mn.str);
                }
            }
        }

        /* Robustness: a member-access call `X.Method(...)` where X is neither a
         * local variable nor any known symbol (class / struct / enum / namespace)
         * is an unresolved reference. This most often means the class was never
         * compiled — e.g. a stdlib file missing from the stdlib_map in main.c —
         * or a typo. Historically such a call silently lowered to a 0/null result
         * (a string method would then return "(null)"), which is very hard to
         * diagnose. Emit a hard compile error instead. Valid builtin calls
         * (Console.*, Math.*, ...) and resolved user methods return earlier, so
         * only genuinely unresolved references reach this point; keying on the
         * object being an unknown name (rather than a known class missing the
         * method) keeps this from firing on extern/DllImport members. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_IDENTIFIER) {
            zan_istr_t on = expr->call.callee->member.object->ident.name;
            zan_istr_t mn = expr->call.callee->member.name;
            zan_symbol_t *osym = local_find(locals, on) ? NULL
                : zan_binder_lookup(g->binder, on);
            if (!local_find(locals, on) && !osym) {
                zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                    "unresolved call '%.*s.%.*s': '%.*s' is not a known variable, "
                    "type, or namespace (is the class imported and registered in "
                    "the stdlib map?)",
                    (int)on.len, on.str, (int)mn.len, mn.str,
                    (int)on.len, on.str);
            }
            /* The object is a known class/struct but the method does not exist
             * on it (and no builtin lowering claimed the call earlier). This is
             * the method-call twin of the check above: silently lowering to
             * 0/null turns a typo or a missing stdlib method into a runtime
             * null-pointer crash far from the call site. */
            else if (osym && (osym->kind == SYM_CLASS || osym->kind == SYM_STRUCT) &&
                     !get_method_sym(osym, mn)) {
                zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                    "unresolved call '%.*s.%.*s': type '%.*s' has no method "
                    "'%.*s'",
                    (int)on.len, on.str, (int)mn.len, mn.str,
                    (int)on.len, on.str, (int)mn.len, mn.str);
            }
        }

        /* generic function call — fallback */
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    }

    case AST_STRING_INTERP: {
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
                    LLVMValueRef buf_size = LLVMBuildAdd(g->builder, needed64, LLVMConstInt(i64, 1, 0), "bsz");
                    LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
                    LLVMValueRef snp_args2[] = { buf, buf_size, fmt, val };
                    zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args2, 4, "");
                    strs[i] = buf;
                    lens[i] = needed64;
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
                    LLVMValueRef buf_size = LLVMBuildAdd(g->builder, needed64, LLVMConstInt(i64, 1, 0), "bsz");
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
            total_len = LLVMBuildAdd(g->builder, total_len, lens[i], "tlen");
        }
        LLVMValueRef alloc_size = LLVMBuildAdd(g->builder, total_len, LLVMConstInt(i64, 1, 0), "asz");

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
    }

    case AST_MEMBER_ACCESS: {
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

        /* Dict.Count — return number of entries */
        if (expr->member.name.len == 5 && memcmp(expr->member.name.str, "Count", 5) == 0 &&
            expr->member.object->kind == AST_IDENTIFIER) {
            local_var_t *dict_local = local_find(locals, expr->member.object->ident.name);
            if (dict_local && dict_local->type && dict_local->type->name.len == 4 &&
                memcmp(dict_local->type->name.str, "Dict", 4) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef raw = LLVMBuildLoad2(g->builder, i8ptr, dict_local->alloca, "draw");
                LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                    LLVMPointerType(g->dict_struct_type, 0), "dp");
                LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                return LLVMBuildLoad2(g->builder, i64, cntp, "dcnt");
            }
        }

        /* array.Length: read the element count captured at declaration. */
        if (expr->member.name.len == 6 && memcmp(expr->member.name.str, "Length", 6) == 0 &&
            expr->member.object->kind == AST_IDENTIFIER) {
            local_var_t *arr_local = local_find(locals, expr->member.object->ident.name);
            if (arr_local && arr_local->type && arr_local->type->kind == TYPE_ARRAY &&
                arr_local->arr_len_slot) {
                return LLVMBuildLoad2(g->builder, LLVMInt64TypeInContext(g->ctx),
                                      arr_local->arr_len_slot, "arr.len");
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
                    return LLVMBuildLoad2(g->builder, ft, gv, "sfld");
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
                    for (int fi = 0; fi < g->function_count; fi++) {
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
                            LLVMValueRef field_ptr = LLVMBuildStructGEP2(g->builder, st, struct_ptr, (unsigned)fi, "fld");
                            zan_symbol_t *fsym = get_field_sym(type_sym, expr->member.name);
                            LLVMTypeRef field_type = fsym ? map_type(g, fsym->type) : LLVMInt64TypeInContext(g->ctx);
                            LLVMValueRef fv = LLVMBuildLoad2(g->builder, field_type, field_ptr, "fval");
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
                    if (st && LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                        LLVMValueRef field_ptr = LLVMBuildStructGEP2(g->builder, st,
                            obj_val, (unsigned)fi, "gfld");
                        zan_symbol_t *fsym = get_field_sym(cls, expr->member.name);
                        LLVMTypeRef ft = fsym ? map_type(g, fsym->type)
                                              : LLVMInt64TypeInContext(g->ctx);
                        LLVMValueRef gfv = LLVMBuildLoad2(g->builder, ft, field_ptr, "gfval");
                        if (fsym) {
                            zan_type_t *rct = infer_expr_type(g, expr->member.object, locals);
                            zan_type_t *ct = subst_type_param(fsym->type, rct);
                            if (ct != fsym->type) gfv = emit_boundary_coerce(g, gfv, map_type(g, ct));
                        }
                        return gfv;
                    }
                }
            }
        }
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    }

    case AST_INDEX: {
        /* arr[i] — array/list element access */
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
            LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &idx, 1, "ep");
            LLVMValueRef raw = LLVMBuildLoad2(g->builder, i64, elem_ptr, "elem");
            /* reinterpret non-integer elements: slots physically hold an i64,
             * so pointer (class/string) elements need inttoptr and floating
             * (double) elements need a bitcast back to the value type. */
            zan_type_t *et = container_elem_type(arr_type);
            if (et) {
                LLVMTypeRef m = map_type(g, et);
                LLVMTypeKind mk = LLVMGetTypeKind(m);
                if (mk == LLVMPointerTypeKind)
                    return LLVMBuildIntToPtr(g->builder, raw, m, "elp");
                if (mk == LLVMDoubleTypeKind)
                    return LLVMBuildBitCast(g->builder, raw, m, "elf");
            }
            return raw;
        }
        /* Dict indexer: dict[key] -> linear scan for key, return value */
        if (arr_ptr && arr_type && arr_type->name.len == 4 && memcmp(arr_type->name.str, "Dict", 4) == 0) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef dp = LLVMBuildBitCast(g->builder, arr_ptr,
                LLVMPointerType(g->dict_struct_type, 0), "dp");
            LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
            LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
            LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
            LLVMValueRef ks = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), kp, "ks");
            LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
            LLVMValueRef vs = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp, "vs");
            LLVMValueRef search = coerce_dict_key(g, emit_expr(g, expr->index.index, locals));
            /* loop to find key */
            LLVMValueRef res = emit_entry_alloca(g, i64, "dres");
            LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), res);
            LLVMValueRef idx_a = emit_entry_alloca(g, i64, "di");
            LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
            LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "di.cond");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "di.body");
            LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "di.done");
            LLVMBuildBr(g->builder, cond_bb);
            LLVMPositionBuilderAtEnd(g->builder, cond_bb);
            LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
            LLVMValueRef cdone = LLVMBuildICmp(g->builder, LLVMIntUGE, ci, cnt, "cdone");
            LLVMBuildCondBr(g->builder, cdone, done_bb, body_bb);
            LLVMPositionBuilderAtEnd(g->builder, body_bb);
            LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
            LLVMValueRef kslot = LLVMBuildGEP2(g->builder, i8ptr, ks, &ci2, 1, "ksl");
            LLVMValueRef kv = LLVMBuildLoad2(g->builder, i8ptr, kslot, "kv");
            LLVMValueRef eq = emit_dict_key_eq(g, arr_type, kv, search);
            LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "di.found");
            LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "di.next");
            LLVMBuildCondBr(g->builder, eq, found_bb, next_bb);
            LLVMPositionBuilderAtEnd(g->builder, found_bb);
            LLVMValueRef vslot = LLVMBuildGEP2(g->builder, i64, vs, &ci2, 1, "vsl");
            LLVMValueRef val = LLVMBuildLoad2(g->builder, i64, vslot, "val");
            LLVMBuildStore(g->builder, val, res);
            LLVMBuildBr(g->builder, done_bb);
            LLVMPositionBuilderAtEnd(g->builder, next_bb);
            LLVMValueRef ni = LLVMBuildAdd(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
            LLVMBuildStore(g->builder, ni, idx_a);
            LLVMBuildBr(g->builder, cond_bb);
            LLVMPositionBuilderAtEnd(g->builder, done_bb);
            LLVMValueRef dval = LLVMBuildLoad2(g->builder, i64, res, "dval");
            zan_type_t *dvt = dict_value_type(arr_type);
            if (dvt) {
                LLVMTypeRef m = map_type(g, dvt);
                LLVMTypeKind mk = LLVMGetTypeKind(m);
                if (mk == LLVMPointerTypeKind)
                    return LLVMBuildIntToPtr(g->builder, dval, m, "dvalp");
                if (mk == LLVMDoubleTypeKind)
                    return LLVMBuildBitCast(g->builder, dval, m, "dvalf");
            }
            return dval;
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
            return LLVMBuildZExt(g->builder, ch, LLVMInt64TypeInContext(g->ctx), "chz");
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
            return LLVMBuildLoad2(g->builder, elem_llvm, elem_ptr, "elem");
        }
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    }

    case AST_QUERY_EXPR: {
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
        LLVMBuildStore(g->builder, list_val, list_alloc);
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
        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_alloc);
        LLVMValueRef iter_alloc = emit_entry_alloca(g, elem_llvm, "qv");
        local_add(locals, expr->query.var, iter_alloc, elem);

        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "q.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "q.body");
        LLVMBasicBlockRef inc_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "q.inc");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "q.end");

        LLVMBuildBr(g->builder, cond_bb);
        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        LLVMValueRef idx_val = LLVMBuildLoad2(g->builder, i64, idx_alloc, "qiv");
        LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntSLT, idx_val, count, "qcmp");
        LLVMBuildCondBr(g->builder, cmp, body_bb, end_bb);

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &idx_val, 1, "qep");
        LLVMValueRef ev = LLVMBuildLoad2(g->builder, i64, elem_ptr, "qelem");
        LLVMTypeKind ek = LLVMGetTypeKind(elem_llvm);
        if (ek == LLVMPointerTypeKind)
            ev = LLVMBuildIntToPtr(g->builder, ev, elem_llvm, "qelp");
        else if (ek == LLVMDoubleTypeKind)
            ev = LLVMBuildBitCast(g->builder, ev, elem_llvm, "qelf");
        else if (ek == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(elem_llvm) < 64)
            ev = LLVMBuildTrunc(g->builder, ev, elem_llvm, "qelt");
        LLVMBuildStore(g->builder, ev, iter_alloc);

        /* where clauses: any false condition skips to the increment */
        for (int wi = 0; wi < expr->query.wheres.count; wi++) {
            LLVMValueRef c = emit_expr(g, expr->query.wheres.items[wi], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(c)) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(LLVMTypeOf(c)) != 1)
                c = LLVMBuildICmp(g->builder, LLVMIntNE, c,
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
        LLVMValueRef next = LLVMBuildAdd(g->builder,
            LLVMBuildLoad2(g->builder, i64, idx_alloc, "qi2"),
            LLVMConstInt(i64, 1, 0), "qnext");
        LLVMBuildStore(g->builder, next, idx_alloc);
        LLVMBuildBr(g->builder, cond_bb);

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        emit_release_owned_call_temp(g, expr->query.source, collection, locals);
        locals->count = mark;
        (void)i8ptr;
        return list_val;
    }

    case AST_NEW_EXPR: {
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
                    zan_type_t *lt = zan_binder_resolve_type(g->binder, expr->new_expr.type);
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
                LLVMBuildStore(g->builder, LLVMConstInt(i64, (unsigned long long)ninit, 0), count_ptr);
                /* capacity = max(8, item count) */
                LLVMValueRef cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 1, "cap");
                LLVMBuildStore(g->builder, LLVMConstInt(i64, (unsigned long long)initcap, 0), cap_ptr);
                /* allocate initial data buffer: capacity * sizeof(i64) */
                LLVMValueRef data_size = LLVMConstInt(i64, (unsigned long long)(initcap * 8), 0);
                LLVMValueRef data_ptr = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), data_size }, 2, "data");
                LLVMValueRef data_typed = LLVMBuildBitCast(g->builder, data_ptr,
                    LLVMPointerType(i64, 0), "dptr");
                LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 2, "df");
                LLVMBuildStore(g->builder, data_typed, data_field);
                /* store each initializer item (with proper rc retain semantics) */
                for (int ii = 0; ii < ninit; ii++) {
                    zan_ast_node_t *item = expr->new_expr.args.items[ii];
                    LLVMValueRef idxk = LLVMConstInt(i64, (unsigned long long)ii, 0);
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
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), cnt_p);
                LLVMValueRef cap_p = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sb_ptr, 1, "sbcap");
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), cap_p);
                LLVMValueRef data_p = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sb_ptr, 2, "sbd");
                LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), data_p);
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
                /* allocate Dict struct: { i64 count, i64 capacity, i8** keys, i64* values } = 32 bytes */
                LLVMValueRef dict_size = LLVMConstInt(i64, 32, 0);
                LLVMValueRef dict_raw = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), dict_size }, 2, "dict");
                LLVMValueRef typed_ptr = LLVMBuildBitCast(g->builder, dict_raw,
                    LLVMPointerType(g->dict_struct_type, 0), "dptr");
                /* count = 0 */
                LLVMValueRef cnt_p = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 0, "cnt");
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), cnt_p);
                /* capacity = 16 */
                LLVMValueRef cap_p = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 1, "cap");
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 16, 0), cap_p);
                /* keys = malloc(16 * 8) */
                LLVMValueRef keys_sz = LLVMConstInt(i64, 128, 0);
                LLVMValueRef keys_raw = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), keys_sz }, 2, "keys");
                LLVMValueRef keys_typed = LLVMBuildBitCast(g->builder, keys_raw,
                    LLVMPointerType(i8ptr, 0), "kptr");
                LLVMValueRef kf = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 2, "kf");
                LLVMBuildStore(g->builder, keys_typed, kf);
                /* values = malloc(16 * 8) */
                LLVMValueRef vals_sz = LLVMConstInt(i64, 128, 0);
                LLVMValueRef vals_raw = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), vals_sz }, 2, "vals");
                LLVMValueRef vals_typed = LLVMBuildBitCast(g->builder, vals_raw,
                    LLVMPointerType(i64, 0), "vptr");
                LLVMValueRef vf = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 3, "vf");
                LLVMBuildStore(g->builder, vals_typed, vf);
                /* dictionary initializer entries: new Dict<K,V>{ {k, v}, ... }.
                 * The parser flattens each `{ k, v }` pair into new_expr.args,
                 * so consume them pairwise via the shared upsert helper. */
                int ninit = expr->new_expr.args.count;
                if (ninit >= 2) {
                    zan_type_t *kt = dict_key_type(g, expr->new_expr.type
                        ? zan_binder_resolve_type(g->binder, expr->new_expr.type) : NULL);
                    zan_type_t *vt = dict_value_type(expr->new_expr.type
                        ? zan_binder_resolve_type(g->binder, expr->new_expr.type) : NULL);
                    LLVMValueRef sf = get_dict_set_fn(g);
                    LLVMValueRef is_str = LLVMConstInt(i64,
                        (kt && kt->kind == TYPE_STRING) ? 1 : 0, 0);
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
                        if (kt && is_rc_managed_type(kt) &&
                            !expr_yields_owned_rc_value(g, kexpr, locals))
                            emit_rc_retain_for_type(g, kt, key);
                        LLVMValueRef val = emit_expr(g, vexpr, locals);
                        LLVMTypeKind vk = LLVMGetTypeKind(LLVMTypeOf(val));
                        if (vk == LLVMPointerTypeKind) {
                            if (vt && is_rc_managed_type(vt) &&
                                !expr_yields_owned_rc_value(g, vexpr, locals))
                                emit_rc_retain_for_type(g, vt, val);
                            val = LLVMBuildPtrToInt(g->builder, val, i64, "v.pi");
                        } else if (vk == LLVMDoubleTypeKind) {
                            val = LLVMBuildBitCast(g->builder, val, i64, "v.bc");
                        } else if (vk == LLVMIntegerTypeKind &&
                                   LLVMGetIntTypeWidth(LLVMTypeOf(val)) < 64) {
                            val = LLVMBuildSExt(g->builder, val, i64, "v.sx");
                        }
                        zan_call2(g->builder, LLVMGlobalGetValueType(sf), sf,
                            (LLVMValueRef[]){ dict_raw, key, val, is_str }, 4, "");
                    }
                }
                return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr, "dictv");
            }
        }

        /* new Type[size] — array creation */
        if (expr->new_expr.is_array && expr->new_expr.args.count > 0) {
            zan_type_t *elem_type = zan_binder_resolve_type(g->binder, expr->new_expr.type);
            if (!elem_type) elem_type = g->binder->type_int;
            LLVMTypeRef elem_llvm = map_type(g, elem_type);
            LLVMValueRef size_val = emit_expr(g, expr->new_expr.args.items[0], locals);

            /* calloc(size, sizeof(elem)) */
            LLVMValueRef elem_size = LLVMSizeOf(elem_llvm);
            LLVMValueRef total = LLVMBuildMul(g->builder,
                LLVMBuildZExt(g->builder, size_val, LLVMInt64TypeInContext(g->ctx), "zext"),
                elem_size, "total");
            LLVMValueRef arr = zan_call2(g->builder,
                LLVMFunctionType(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                    (LLVMTypeRef[]){LLVMInt64TypeInContext(g->ctx), LLVMInt64TypeInContext(g->ctx)}, 2, 0),
                get_calloc_fn(g), (LLVMValueRef[]){ total, LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 1, 0) }, 2, "arr");
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
                        LLVMValueRef site_val = LLVMConstInt(i64, 0, 0);
                        {
                            /* Assign a stable allocation-site index (always: it
                             * also keys dynamic release dispatch to this site's
                             * concrete class). The "file:line:col" descriptor is
                             * only needed for --check-leaks reporting. */
                            int site_idx = g->leak_site_count;
                            if (site_idx >= ZAN_MAX_LEAK_SITES) site_idx = ZAN_MAX_LEAK_SITES - 1;
                            else g->leak_site_count++;
                            if (g->site_syms) g->site_syms[site_idx] = sym;
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
                    LLVMBuildStore(g->builder, LLVMConstNull(st), alloca);

                    /* install the vtable pointer (field 0) before the ctor runs
                     * so virtual calls made during construction dispatch to the
                     * most-derived implementation. */
                    if (sym->type->kind == TYPE_CLASS && class_has_virtual_methods(sym)) {
                        LLVMTypeRef i8ptr_vt = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMValueRef vtg = get_vtable_global(g, sym);
                        LLVMValueRef vpf0 = LLVMBuildStructGEP2(g->builder, st, alloca, 0, "vpf.init");
                        LLVMBuildStore(g->builder,
                            LLVMBuildBitCast(g->builder, vtg, i8ptr_vt, "vt.i8"), vpf0);
                    }

                    /* look for constructor. For a concrete instantiation of a
                     * user generic class, prefer the specialized constructor so
                     * the body runs with the instantiation context. */
                    zan_type_t *new_inst = zan_binder_resolve_type(g->binder, expr->new_expr.type);
                    LLVMValueRef ctor_fn = NULL;
                    LLVMTypeRef ctor_ft = NULL;
                    if (new_inst && new_inst->type_arg_count > 0)
                        ctor_fn = find_generic_ctor(g, sym, new_inst->type_args,
                                                    new_inst->type_arg_count,
                                                    expr->new_expr.args.count, &ctor_ft);
                    if (!ctor_fn) {
                        for (int ci = 0; ci < g->ctor_count; ci++) {
                            if (g->ctors[ci].type_sym == sym) {
                                ctor_fn = g->ctors[ci].fn;
                                ctor_ft = g->ctors[ci].fn_type;
                                break;
                            }
                        }
                    }
                    if (ctor_fn) {
                        int argc = expr->new_expr.args.count + 1;
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
                        for (int k = 0; k < expr->new_expr.args.count; k++) {
                            call_args[k + 1] = emit_expr(g, expr->new_expr.args.items[k], locals);
                        }
                        coerce_args_to_params(g, ctor_ft, call_args, argc);
                        zan_call2(g->builder, ctor_ft, ctor_fn, call_args, (unsigned)argc, "");
                        if (obj_eh_pushed) emit_eh_tmp_pop(g);
                        for (int k = 0; k < expr->new_expr.args.count; k++) {
                            emit_release_owned_call_temp(g, expr->new_expr.args.items[k],
                                call_args[k + 1], locals);
                        }
                        free(call_args);
                    }

                    /* if no constructor, handle field initializers */
                    if (g->ctor_count == 0 || 1) {
                        for (int i = 0; i < expr->new_expr.args.count; i++) {
                            zan_ast_node_t *arg = expr->new_expr.args.items[i];
                            if (arg->kind == AST_ASSIGNMENT && arg->binary.left->kind == AST_IDENTIFIER) {
                                int fi = get_field_index(sym, arg->binary.left->ident.name);
                                if (fi >= 0) {
                                    LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st, alloca, (unsigned)fi, "finit");
                                    zan_symbol_t *fsym = get_field_sym(sym, arg->binary.left->ident.name);
                                    LLVMValueRef fval =
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
                                        !expr_yields_owned_ref(arg->binary.right)) {
                                        emit_rc_retain_for_type(g, fsym->type, fval);
                                    }
                                    LLVMBuildStore(g->builder, fval, fptr);
                                }
                            }
                        }
                    }
                    return alloca;
                }
            }
        }
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    }

    case AST_CONDITIONAL: {
        /* ternary: condition ? then_expr : else_expr */
        LLVMValueRef cond = emit_expr(g, expr->conditional.cond, locals);
        /* normalize to i1 */
        if (LLVMTypeOf(cond) != LLVMInt1TypeInContext(g->ctx)) {
            cond = LLVMBuildICmp(g->builder, LLVMIntNE, cond,
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
        LLVMBuildBr(g->builder, merge_bb);

        LLVMPositionBuilderAtEnd(g->builder, else_bb);
        LLVMValueRef else_val = emit_expr(g, expr->conditional.else_expr, locals);
        LLVMBasicBlockRef else_end = LLVMGetInsertBlock(g->builder);
        LLVMBuildBr(g->builder, merge_bb);

        LLVMPositionBuilderAtEnd(g->builder, merge_bb);
        LLVMValueRef phi = LLVMBuildPhi(g->builder, LLVMTypeOf(then_val), "tern");
        LLVMValueRef incoming_vals[] = { then_val, else_val };
        LLVMBasicBlockRef incoming_bbs[] = { then_end, else_end };
        LLVMAddIncoming(phi, incoming_vals, incoming_bbs, 2);
        return phi;
    }

    case AST_POSTFIX_UNARY: {
        /* x++ or x-- */
        LLVMValueRef ptr = NULL;
        if (expr->unary.operand->kind == AST_IDENTIFIER) {
            local_var_t *lv = local_find(locals, expr->unary.operand->ident.name);
            if (lv) ptr = lv->alloca;
        }
        if (!ptr) return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
        LLVMValueRef old_val = LLVMBuildLoad2(g->builder,
            LLVMInt64TypeInContext(g->ctx), ptr, "post");
        LLVMValueRef one = LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 1, 0);
        LLVMValueRef new_val;
        if (expr->unary.op == TK_PLUS_PLUS) {
            new_val = LLVMBuildAdd(g->builder, old_val, one, "inc");
        } else {
            new_val = LLVMBuildSub(g->builder, old_val, one, "dec");
        }
        LLVMBuildStore(g->builder, new_val, ptr);
        return old_val; /* postfix returns old value */
    }

    case AST_IS_EXPR: {
        /* x is Type — runtime type check (simplified: always true for matching static types) */
        return LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0);
    }

    case AST_AS_EXPR: {
        /* x as Type — type cast (simplified: pass through value) */
        return emit_expr(g, expr->type_test.expr, locals);
    }

    case AST_CAST_EXPR: {
        /* (Type)x — explicit numeric cast honoring the target type. */
        LLVMValueRef val = emit_expr(g, expr->cast.expr, locals);
        zan_type_t *tt = zan_binder_resolve_type(g->binder, expr->cast.type);
        LLVMTypeRef target = tt ? map_type(g, tt) : LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef src = LLVMTypeOf(val);
        /* Unsigned/small targets share the i64 representation, so their cast
         * semantics are wrap-to-width masks (zero-extend for uint/ushort,
         * sign-extend from 8 bits for sbyte) applied on the i64 value. */
        if (tt && LLVMGetTypeKind(src) == LLVMIntegerTypeKind &&
            LLVMGetIntTypeWidth(src) == 64) {
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            switch (tt->kind) {
            case TYPE_UINT:
                return LLVMBuildAnd(g->builder, val,
                    LLVMConstInt(i64t, 0xFFFFFFFFull, 0), "cast.u32");
            case TYPE_USHORT:
                return LLVMBuildAnd(g->builder, val,
                    LLVMConstInt(i64t, 0xFFFFull, 0), "cast.u16");
            case TYPE_SBYTE: {
                LLVMValueRef sh = LLVMConstInt(i64t, 56, 0);
                LLVMValueRef up = LLVMBuildShl(g->builder, val, sh, "cast.i8.l");
                return LLVMBuildAShr(g->builder, up, sh, "cast.i8");
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
        bool src_fp = (sk == LLVMDoubleTypeKind || sk == LLVMFloatTypeKind);
        bool tgt_fp = (tk == LLVMDoubleTypeKind || tk == LLVMFloatTypeKind);
        if (sk == LLVMIntegerTypeKind && tk == LLVMIntegerTypeKind) {
            unsigned sw = LLVMGetIntTypeWidth(src);
            unsigned tw = LLVMGetIntTypeWidth(target);
            if (tw < sw) return LLVMBuildTrunc(g->builder, val, target, "cast");
            if (tw > sw) return LLVMBuildSExt(g->builder, val, target, "cast");
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
    }

    case AST_SIZEOF_EXPR: {
        /* sizeof(Type) — return size in bytes */
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
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        if (LLVMCountParams(fn) > 0) {
            return LLVMGetParam(fn, 0);
        }
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
    }

    case AST_BASE_EXPR: {
        /* base — same as this for single inheritance (see AST_THIS_EXPR). */
        if (g->current_this) {
            return LLVMBuildLoad2(g->builder, LLVMGetAllocatedType(g->current_this),
                g->current_this, "this");
        }
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        if (LLVMCountParams(fn) > 0) {
            return LLVMGetParam(fn, 0);
        }
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
    }

    case AST_AWAIT_EXPR: {
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
                LLVMBuildStore(g->builder, LLVMConstInt(di32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe, ASYNC_FRAME_STATE, "self.state"));
                zan_call2(g->builder, g->rt_co_delay_type, g->rt_co_delay,
                    (LLVMValueRef[]){ ms, self_i8, g->current_async_resume_fn }, 3, "");
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
            LLVMBuildStore(g->builder, LLVMConstInt(di32, (unsigned)k, 0),
                LLVMBuildStructGEP2(g->builder, self_ft, selfframe, ASYNC_FRAME_STATE, "self.state"));
            zan_call2(g->builder, gate_park_type, gate_park,
                (LLVMValueRef[]){ handle, self_i8, g->current_async_resume_fn }, 3, "");
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
                LLVMBuildStore(g->builder, LLVMConstInt(di32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe, ASYNC_FRAME_STATE, "self.state"));
                zan_call2(g->builder, g->rt_io_wait_co_type, g->rt_io_wait_co,
                    (LLVMValueRef[]){ fd, interest, self_i8, g->current_async_resume_fn }, 4, "");
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
                LLVMBuildStore(g->builder, LLVMConstInt(di32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe, ASYNC_FRAME_STATE, "self.state"));
                zan_call2(g->builder, g->rt_io_recv_co_type, g->rt_io_recv_co,
                    (LLVMValueRef[]){ fd, buf, len, self_i8,
                        g->current_async_resume_fn, out_n }, 6, "");
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
                LLVMBuildStore(g->builder, LLVMConstInt(di32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                        ASYNC_FRAME_STATE, "self.state"));
                zan_call2(g->builder, g->rt_io_accept_co_type,
                    g->rt_io_accept_co, (LLVMValueRef[]){ fd, self_i8,
                        g->current_async_resume_fn, out_fd }, 4, "");
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
            LLVMValueRef callee = LLVMGetCalledValue(sub);
            if (callee) {
                size_t nl = 0;
                const char *cn = LLVMGetValueName2(callee, &nl);
                if (cn && nl > 0 && nl < 240) {
                    char rn[256];
                    memcpy(rn, cn, nl);
                    memcpy(rn + nl, "$resume", 8); /* includes NUL */
                    sub_resume = LLVMGetNamedFunction(g->mod, rn);
                }
            }
        }

        if (sub_resume && LLVMGetTypeKind(LLVMTypeOf(sub)) == LLVMPointerTypeKind) {
            LLVMValueRef sub_i8 = LLVMBuildBitCast(g->builder, sub, i8ptr, "sub");

            if (g->current_async_frame && g->current_async_switch) {
                int k = g->current_async_next_state++;
                int j = g->current_async_sub_next++;
                LLVMValueRef selfframe = g->current_async_frame;
                LLVMTypeRef self_ft = g->current_async_frame_type;

                /* stash sub handle in a frame slot (survives the suspension) */
                LLVMBuildStore(g->builder, sub_i8,
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                        (unsigned)(g->current_async_sub_base + j), "sub.slot"));

                /* sub.awaiter = self; sub.awaiter_step = Self$resume */
                LLVMValueRef self_i8 = LLVMBuildBitCast(g->builder, selfframe, i8ptr, "self");
                LLVMBuildStore(g->builder, self_i8,
                    LLVMBuildStructGEP2(g->builder, hdr, sub_i8, ASYNC_FRAME_AWAITER, "sub.aw"));
                LLVMBuildStore(g->builder, g->current_async_resume_fn,
                    LLVMBuildStructGEP2(g->builder, hdr, sub_i8, ASYNC_FRAME_AWAITER_STEP, "sub.aws"));

                emit_async_save_slots(g);
                LLVMBuildStore(g->builder, LLVMConstInt(i32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe, ASYNC_FRAME_STATE, "self.state"));
                LLVMValueRef sched_args[] = { sub_i8, sub_resume };
                zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
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
                return awres;
            }

            /* root drive (non-async caller). The scheduler is initialized once
             * at program entry (see emit_main_method); re-initializing here
             * would reset the ready queue and discard any coroutines already
             * enqueued by Task.Spawn before this await -- e.g. a spawned server
             * in a concurrent client/server program would never run. */
            LLVMValueRef sched_args[] = { sub_i8, sub_resume };
            zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
            zan_call2(g->builder, g->rt_co_sched_run_type, g->rt_co_sched_run, NULL, 0, "");
            LLVMValueRef rptr = LLVMBuildStructGEP2(g->builder, hdr, sub_i8,
                ASYNC_FRAME_RESULT, "sub.result");
            LLVMValueRef awres = LLVMBuildLoad2(g->builder, i64, rptr, "awres");
            /* Root-driven sub has run to completion; copy out its result then
             * free its heap frame (no awaiter will). */
            zan_call2(g->builder, LLVMGlobalGetValueType(g->fn_free),
                g->fn_free, &sub_i8, 1, "");
            return awres;
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
            LLVMValueRef is_done = LLVMBuildICmp(g->builder, LLVMIntNE, comp, LLVMConstInt(i64, 0, 0), "done");
            LLVMBuildCondBr(g->builder, is_done, done_bb, poll_bb);
            LLVMPositionBuilderAtEnd(g->builder, done_bb);
            LLVMValueRef res_ptr = LLVMBuildStructGEP2(g->builder, g->task_struct_type, task_ptr, 1, "resp");
            return LLVMBuildLoad2(g->builder, i64, res_ptr, "awres");
        }
        return sub;
    }

    case AST_LAMBDA:
        return emit_lambda_typed(g, expr, NULL, locals);

    default:
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    }
}

static LLVMValueRef emit_lambda_typed(zan_irgen_t *g, zan_ast_node_t *expr,
                                      zan_type_t *expected, local_scope_t *locals) {
    (void)locals; /* lambdas are non-capturing */
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
            pt = zan_binder_resolve_type(g->binder, param->param.type);
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
    LLVMTypeRef fn_type = LLVMFunctionType(ret_type, param_types, (unsigned)pc, 0);

    char lname[64];
    static int lambda_id = 0;
    snprintf(lname, sizeof(lname), "lambda_%d", lambda_id++);
    LLVMValueRef lambda_fn = LLVMAddFunction(g->mod, lname, fn_type);

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, lambda_fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);

    /* The body must emit into the lambda's own function: control-flow blocks
     * append to current_fn and `return` coerces to current_fn_ret. Lambdas are
     * non-capturing (no `this`/enclosing locals), so clear the instance/async
     * context while keeping the enclosing type for static member access. */
    LLVMValueRef saved_fn = g->current_fn;
    LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
    LLVMValueRef saved_this = g->current_this;
    LLVMValueRef saved_async_frame = g->current_async_frame;
    g->current_fn = lambda_fn;
    g->current_fn_ret_type = ret_type;
    g->current_this = NULL;
    g->current_async_frame = NULL;

    local_scope_t lambda_locals;
    local_scope_init(&lambda_locals, g->arena);
    for (int k = 0; k < pc; k++) {
        zan_ast_node_t *param = expr->lambda.params.items[k];
        LLVMTypeRef lt = param_types[k];
        LLVMValueRef alloc = LLVMBuildAlloca(g->builder, lt, "lp");
        LLVMBuildStore(g->builder, LLVMGetParam(lambda_fn, (unsigned)k), alloc);
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
        LLVMValueRef result = emit_expr(g, expr->lambda.body, &lambda_locals);
        if (ret_void) {
            LLVMBuildRetVoid(g->builder);
        } else if (rett) {
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
    g->current_this = saved_this;
    g->current_async_frame = saved_async_frame;
    LLVMPositionBuilderAtEnd(g->builder, saved_bb);
    free(param_types);
    free(ptypes);
    return lambda_fn;
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
        bind[i] = zan_binder_resolve_type(g->binder, call->call.type_args.items[i]);

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
}

/* method_param_type with the generic method's type parameters substituted for
 * this call site. This is what lets an untyped lambda argument (`x => ...`)
 * get its parameter types from e.g. a `Pred<T>` delegate once T is known. */
static zan_type_t *method_param_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                        int idx, zan_ast_node_t *call,
                                        zan_ast_node_t *recv_expr,
                                        local_scope_t *locals) {
    zan_type_t *pt = method_param_type(g, msym, idx);
    if (!pt || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return pt;
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

static LLVMValueRef emit_arg_typed(zan_irgen_t *g, zan_ast_node_t *arg,
                                   zan_type_t *ptype, local_scope_t *locals) {
    if (arg && arg->kind == AST_LAMBDA && ptype && ptype->kind == TYPE_DELEGATE) {
        return emit_lambda_typed(g, arg, ptype, locals);
    }
    return emit_expr(g, arg, locals);
}

/* `ref x` / `out x` / `out T x` argument: pass the address of the local's
 * storage slot. An inline `out T x` declares a fresh zero-initialised local
 * in the caller's scope first. */
static LLVMValueRef emit_ref_arg(zan_irgen_t *g, zan_ast_node_t *arg,
                                 local_scope_t *locals) {
    zan_ast_node_t *tgt = arg->ref_arg.expr;
    if (arg->ref_arg.decl_type && tgt && tgt->kind == AST_IDENTIFIER) {
        zan_type_t *dt = zan_binder_resolve_type(g->binder, arg->ref_arg.decl_type);
        LLVMTypeRef lt = map_type(g, dt);
        LLVMValueRef a = emit_entry_alloca(g, lt, "out");
        LLVMBuildStore(g->builder, LLVMConstNull(lt), a);
        local_add(locals, tgt->ident.name, a, dt);
        return a;
    }
    if (tgt && tgt->kind == AST_IDENTIFIER) {
        local_var_t *l = local_find(locals, tgt->ident.name);
        if (l) return l->alloca;
    }
    return emit_expr(g, tgt, locals);
}
