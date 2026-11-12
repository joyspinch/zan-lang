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
bool emit_vector256_call(zan_irgen_t *g, zan_ast_node_t *expr,
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
bool emit_sse2_call(zan_irgen_t *g, zan_ast_node_t *expr,
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
bool emit_sse42_call(zan_irgen_t *g, zan_ast_node_t *expr,
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
    bool hw_crc = emit_target_is_x86(g) &&
                  (!g->cpu_profile || strcmp(g->cpu_profile, "baseline") != 0);

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
bool emit_aes_call(zan_irgen_t *g, zan_ast_node_t *expr,
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

