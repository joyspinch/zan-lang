#include "irgen_internal.h"
/* 表达式 AST 节点发射器实现 */
LLVMValueRef emit_expr_identifier(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        local_var_t *local = local_find(locals, expr->ident.name);
        if (local) {
            return promote_loaded(g, LLVMBuildLoad2(g->builder,
                map_type(g, local->type), local->alloca, "load"),
                local->type);
        }
        /* 实例方法内省略 this 的成员字段寻址 */
        if (g->current_this && g->current_type_sym) {
            int fi = get_field_index(g->current_type_sym, expr->ident.name);
            if (fi >= 0) {
                /* 裸名自定义属性读取：以 this 作为接收者派发调用 getter */
                zan_symbol_t *psym = get_field_sym(g->current_type_sym, expr->ident.name);
                zan_symbol_t *getter = property_getter_sym(g, psym);
                if (getter) {
                    LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                    LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(st, 0), g->current_this, "this");
                    zan_type_t *rct = g->cur_inst ? g->cur_inst
                                                  : g->current_type_sym->type;
                    /* Cleanup describes the receiver, not the owned getter
                     * result: implicit this is borrowed just like this.Prop. */
                    zan_ast_node_t receiver = {0};
                    receiver.kind = AST_THIS_EXPR;
                    receiver.loc = expr->loc;
                    return emit_property_getter_call(g, getter, rct, this_ptr,
                                                     &receiver, locals);
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
        /* 外层类的裸名静态字段：直接映射为全局符号 */
        if (g->current_type_sym) {
            zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->ident.name);
            /* 静态自定义 getter 属性读取分发 */
            zan_symbol_t *getter = property_getter_sym(g, fsym);
            if (getter) {
                return emit_property_getter_call(g, getter,
                    g->current_type_sym->type, NULL, NULL, locals);
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
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3002,
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
        /* 全局 LLVM 符号名查找回退 */
        {
            char nbuf[256];
            int nl = expr->ident.name.len < 255 ? expr->ident.name.len : 255;
            memcpy(nbuf, expr->ident.name.str, (size_t)nl);
            nbuf[nl] = '\0';
            LLVMValueRef gfn = LLVMGetNamedFunction(g->mod, nbuf);
            if (gfn) return gfn;
        }
        /* 静态检查已报错的未决符号占位返回 */
        /* 未解析标识符报错与诊断兜底 */
        if (!zan_binder_lookup(g->binder, expr->ident.name)) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3003,
                "use of undeclared identifier '%.*s'",
                (int)expr->ident.name.len, expr->ident.name.str);
        }
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}
LLVMValueRef load_collection_slot_value(zan_irgen_t *g,
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
void emit_dict_value_set(zan_irgen_t *g, zan_type_t *dict_type,
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
LLVMValueRef emit_typed_equality(zan_irgen_t *g, zan_type_t *type,
                                         LLVMValueRef left,
                                         LLVMValueRef right) {
    type = concretize(g, type);
    if (type && type->kind == TYPE_DECIMAL) {
        LLVMTypeRef dt = get_decimal_type(g);
        LLVMTypeRef dp = LLVMPointerType(dt, 0);
        LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
        LLVMValueRef lp = emit_entry_alloca(g, dt, "eq.dec.l");
        LLVMValueRef rp = emit_entry_alloca(g, dt, "eq.dec.r");
        LLVMBuildStore(g->builder, coerce_to_decimal(g, left), lp);
        LLVMBuildStore(g->builder, coerce_to_decimal(g, right), rp);
        LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ dp, dp }, 2, 0);
        LLVMValueRef fn = get_libc_fn(g, "__zan_decimal_cmp", ft);
        LLVMValueRef cmp = zan_call2(g->builder, ft, fn,
            (LLVMValueRef[]){ lp, rp }, 2, "eq.dec.cmp");
        return zan_icmp(g->builder, LLVMIntEQ, cmp,
            LLVMConstInt(i32, 0, 0), "eq.dec");
    }
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
        /* 带长度感知的字符串字典序比对（防止嵌入 0 截断） */
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

/* 二元表达式中可空类型 (T?) 运算：null 传播与 ?? 解包规则 */
static LLVMValueRef emit_nullable_binary(zan_irgen_t *g, zan_ast_node_t *expr,
                                         LLVMValueRef left, LLVMValueRef right,
                                         local_scope_t *locals);

/* ?? 的短路求值（C# 语义）：右支只在左支为 null 时求值。
 * 返回 NULL 表示形态覆盖不了，调用方回退旧的急切两臂路径。 */
static LLVMValueRef emit_null_coalesce(zan_irgen_t *g, zan_ast_node_t *expr,
                                       local_scope_t *locals);
LLVMValueRef coerce_to_decimal(zan_irgen_t *g, LLVMValueRef val) {
    LLVMTypeRef dec_ty = get_decimal_type(g);
    if (!val || LLVMTypeOf(val) == dec_ty) return val;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef ptr_ty = LLVMPointerType(dec_ty, 0);
    LLVMValueRef val_i64 = val;
    if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMIntegerTypeKind) {
        if (LLVMGetIntTypeWidth(LLVMTypeOf(val)) < 64) {
            val_i64 = LLVMBuildSExt(g->builder, val, i64, "dec.sext");
        }
    } else {
        return val;
    }
    LLVMValueRef slot = emit_entry_alloca(g, dec_ty, "dec.conv");
    LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i64, ptr_ty }, 2, 0);
    LLVMValueRef fn = get_libc_fn(g, "__zan_decimal_from_int64", fn_ty);
    zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){ val_i64, slot }, 2, "");
    return LLVMBuildLoad2(g->builder, dec_ty, slot, "dec.val");
}

static LLVMValueRef emit_decimal_binary(zan_irgen_t *g, zan_ast_node_t *expr,
                                        LLVMValueRef left, LLVMValueRef right,
                                        local_scope_t *locals) {
    (void)locals;
    LLVMTypeRef dec_ty = get_decimal_type(g);
    left = coerce_to_decimal(g, left);
    right = coerce_to_decimal(g, right);

    LLVMValueRef lslot = emit_entry_alloca(g, dec_ty, "dec.l");
    LLVMValueRef rslot = emit_entry_alloca(g, dec_ty, "dec.r");
    LLVMBuildStore(g->builder, left, lslot);
    LLVMBuildStore(g->builder, right, rslot);
    LLVMTypeRef ptr_ty = LLVMPointerType(dec_ty, 0);

    const char *op_fn = NULL;
    switch (expr->binary.op) {
    case TK_PLUS:    op_fn = "__zan_decimal_add"; break;
    case TK_MINUS:   op_fn = "__zan_decimal_sub"; break;
    case TK_STAR:    op_fn = "__zan_decimal_mul"; break;
    case TK_SLASH:   op_fn = "__zan_decimal_div"; break;
    case TK_PERCENT: op_fn = "__zan_decimal_rem"; break;
    default: break;
    }

    if (op_fn) {
        if (expr->binary.op == TK_SLASH || expr->binary.op == TK_PERCENT) {
            /* decimal 零除与整数路径同口径：抛 DivideByZeroException，
             * 不再由运行时静默退化为 0（B-ID84）。 */
            LLVMTypeRef i32_ty = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef zslot = emit_entry_alloca(g, dec_ty, "dec.zero");
            LLVMBuildStore(g->builder, LLVMConstNull(dec_ty), zslot);
            LLVMTypeRef cmp_fn_ty = LLVMFunctionType(i32_ty,
                (LLVMTypeRef[]){ ptr_ty, ptr_ty }, 2, 0);
            LLVMValueRef cmp_fn = get_libc_fn(g, "__zan_decimal_cmp", cmp_fn_ty);
            LLVMValueRef cmp_z = zan_call2(g->builder, cmp_fn_ty, cmp_fn,
                (LLVMValueRef[]){ rslot, zslot }, 2, "dec.divz.cmp");
            LLVMValueRef is_zero = zan_icmp(g->builder, LLVMIntEQ, cmp_z,
                LLVMConstInt(i32_ty, 0, 0), "dec.divz");
            emit_runtime_check(g, is_zero, expr->loc,
                expr->binary.op == TK_SLASH ? "division by zero"
                                            : "division by zero (modulo)");
        }
        LLVMValueRef res_slot = emit_entry_alloca(g, dec_ty, "dec.res");
        LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            (LLVMTypeRef[]){ ptr_ty, ptr_ty, ptr_ty }, 3, 0);
        LLVMValueRef fn = get_libc_fn(g, op_fn, fn_ty);
        zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){ lslot, rslot, res_slot }, 3, "");
        return LLVMBuildLoad2(g->builder, dec_ty, res_slot, "dec.out");
    }

    /* 比较运算 */
    LLVMTypeRef i32_ty = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef cmp_fn_ty = LLVMFunctionType(i32_ty,
        (LLVMTypeRef[]){ ptr_ty, ptr_ty }, 2, 0);
    LLVMValueRef cmp_fn = get_libc_fn(g, "__zan_decimal_cmp", cmp_fn_ty);
    LLVMValueRef cmp_res = zan_call2(g->builder, cmp_fn_ty, cmp_fn,
        (LLVMValueRef[]){ lslot, rslot }, 2, "dec.cmp");
    LLVMValueRef zero = LLVMConstInt(i32_ty, 0, 0);

    switch (expr->binary.op) {
    case TK_EQ_EQ:
        return zan_icmp(g->builder, LLVMIntEQ, cmp_res, zero, "dec.eq");
    case TK_BANG_EQ:
        return zan_icmp(g->builder, LLVMIntNE, cmp_res, zero, "dec.ne");
    case TK_LESS:
        return zan_icmp(g->builder, LLVMIntSLT, cmp_res, zero, "dec.lt");
    case TK_LESS_EQ:
        return zan_icmp(g->builder, LLVMIntSLE, cmp_res, zero, "dec.le");
    case TK_GREATER:
        return zan_icmp(g->builder, LLVMIntSGT, cmp_res, zero, "dec.gt");
    case TK_GREATER_EQ:
        return zan_icmp(g->builder, LLVMIntSGE, cmp_res, zero, "dec.ge");
    default:
        break;
    }
    return left;
}
