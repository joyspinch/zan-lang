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

/* ---- per-node-kind expression emitters --------------------------------
 * Each function below is one arm of the old monolithic emit_expr switch;
 * emit_expr itself is now just the literal cases plus dispatch.
 */

static LLVMValueRef emit_expr_identifier(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
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
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

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
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

static LLVMValueRef emit_expr_unary(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
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
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

static LLVMValueRef emit_expr_assignment(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
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
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

static LLVMValueRef emit_expr_call(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals);

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
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

static LLVMValueRef emit_expr_member_access(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
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
                LLVMBuildStore(g->builder, cnt,
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "lcnt"));
                LLVMValueRef cap = LLVMBuildAdd(g->builder, cnt,
                    LLVMConstInt(i64, 8, 0), "lcap");
                LLVMBuildStore(g->builder, cap,
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 1, "lcapp"));
                LLVMValueRef data_size = LLVMBuildMul(g->builder, cap,
                    LLVMConstInt(i64, 8, 0), "lsz");
                LLVMValueRef data = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g),
                    (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), data_size }, 2, "ldata");
                LLVMValueRef data_typed = LLVMBuildBitCast(g->builder, data,
                    LLVMPointerType(i64, 0), "ldp");
                LLVMBuildStore(g->builder, data_typed,
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "ldf"));
                /* copy loop */
                LLVMValueRef idx_a = emit_entry_alloca(g, i64, "dk.i");
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dk.cond");
                LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dk.body");
                LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dk.done");
                LLVMBuildBr(g->builder, cond_bb);
                LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                LLVMBuildCondBr(g->builder,
                    LLVMBuildICmp(g->builder, LLVMIntUGE, ci, cnt, "cdone"),
                    done_bb, body_bb);
                LLVMPositionBuilderAtEnd(g->builder, body_bb);
                LLVMValueRef sslot = LLVMBuildGEP2(g->builder, src_slot_ty, src, &ci, 1, "ssl");
                LLVMValueRef sval = LLVMBuildLoad2(g->builder, src_slot_ty, sslot, "sv");
                /* reinterpret a value slot per the concrete element type so
                 * the slot-store retain sees a pointer for rc elements */
                if (!want_keys && elem) {
                    LLVMTypeRef m = map_type(g, elem);
                    if (LLVMGetTypeKind(m) == LLVMPointerTypeKind)
                        sval = LLVMBuildIntToPtr(g->builder, sval, m, "svp");
                }
                LLVMValueRef dslot = LLVMBuildGEP2(g->builder, i64, data_typed, &ci, 1, "dsl");
                emit_collection_slot_store(g, elem, i64, dslot, sval, NULL, locals, 0);
                LLVMBuildStore(g->builder,
                    LLVMBuildAdd(g->builder, ci, LLVMConstInt(i64, 1, 0), "ni"), idx_a);
                LLVMBuildBr(g->builder, cond_bb);
                LLVMPositionBuilderAtEnd(g->builder, done_bb);
                emit_release_owned_call_temp(g, expr->member.object, raw, locals);
                return LLVMBuildBitCast(g->builder, lp, i8ptr, "keysv");
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
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

static LLVMValueRef emit_expr_index(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
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
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

static LLVMValueRef emit_expr_new_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
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
                        ? resolve_type_ctx(g, expr->new_expr.type) : NULL);
                    zan_type_t *vt = dict_value_type(expr->new_expr.type
                        ? resolve_type_ctx(g, expr->new_expr.type) : NULL);
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
            zan_type_t *elem_type = resolve_type_ctx(g, expr->new_expr.type);
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
                    zan_type_t *new_inst = resolve_type_ctx(g, expr->new_expr.type);
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
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

static LLVMValueRef emit_expr_conditional(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
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
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
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
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

static LLVMValueRef emit_expr_cast_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* (Type)x — explicit numeric cast honoring the target type. */
        LLVMValueRef val = emit_expr(g, expr->cast.expr, locals);
        zan_type_t *tt = resolve_type_ctx(g, expr->cast.type);
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
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
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

    case AST_IDENTIFIER:
        return emit_expr_identifier(g, expr, locals);

    case AST_BINARY:
        return emit_expr_binary(g, expr, locals);

    case AST_UNARY:
        return emit_expr_unary(g, expr, locals);

    case AST_ASSIGNMENT:
        return emit_expr_assignment(g, expr, locals);

    case AST_CALL:
        return emit_expr_call(g, expr, locals);

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

    case AST_CAST_EXPR:
        return emit_expr_cast_expr(g, expr, locals);

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

    case AST_AWAIT_EXPR:
        return emit_expr_await_expr(g, expr, locals);

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
        zan_type_t *dt = resolve_type_ctx(g, arg->ref_arg.decl_type);
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
