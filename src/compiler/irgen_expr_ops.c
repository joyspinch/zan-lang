/* 表达式 AST 节点发射器实现 */

static LLVMValueRef emit_expr_identifier(zan_irgen_t *g, zan_ast_node_t *expr,
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
                    g->current_type_sym->type, NULL, expr, locals);
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

static LLVMValueRef coerce_to_decimal(zan_irgen_t *g, LLVMValueRef val) {
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

static LLVMValueRef emit_expr_binary(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* 短路逻辑运算符（&& / ||）：左操作数满足短路条件时不求值右操作数 */
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
            /* 逻辑与/或短路求值控制流生成 */
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
            /* 逻辑与/或短路常量折叠优化 */
            LLVMValueRef sc_const = LLVMConstInt(i1, is_and ? 0 : 1, 0);
            LLVMValueRef vals[] = { sc_const, rval };
            LLVMBasicBlockRef bbs[] = { left_bb, rhs_end };
            LLVMAddIncoming(phi, vals, bbs, 2);
            return phi;
        }

        /* 3+ 字符串连续拼接扁平化优化：合并单次分配 */
        if (expr->binary.op == TK_PLUS && is_str_concat_node(g, expr, locals) &&
            (is_str_concat_node(g, expr->binary.left, locals) ||
             is_str_concat_node(g, expr->binary.right, locals))) {
            return emit_str_concat_n(g, expr, locals);
        }

        /* ?? 短路求值：右支只在左支为 null 时求值（C# 语义）。旧实现对两臂
         * 无条件求值后再 select——带副作用的右支（日志/计数/惰性初始化）被
         * 错误触发，是 skill 二.2 禁止的 select 死臂形态（TASKS.md A515）。 */
        if (expr->binary.op == TK_QUESTION_QUESTION) {
            LLVMValueRef coal = emit_null_coalesce(g, expr, locals);
            if (coal) return coal;
        }

        LLVMValueRef left = emit_expr(g, expr->binary.left, locals);
        LLVMValueRef right = emit_expr(g, expr->binary.right, locals);

        {
            zan_type_t *ltype = infer_expr_type(g, expr->binary.left, locals);
            /* 与 null 比较保留引用指针判空，防止递归调用重载操作符 */
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
                    /* 在类中解析重载的自定义运算符方法 */
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
                                /* 调用前将实参类型适配转换至声明形参类型 */
                                LLVMValueRef cargs[2] = { left, right };
                                /* 非静态运算符绑定隐式 this 接收者 */
                                bool op_is_static =
                                    (op_sym->modifiers & MOD_STATIC) != 0;
                                /* 接收者与实参既可能是"值"，也可能是"槽位指针"
                                 * （new 结构体表达式返回的是槽位地址）。常规调用
                                 * 走 coerce_args_to_params 会补这次加载，运算符
                                 * 路径没有——按值传参就会把指针塞进调用里，
                                 * LLVM 校验直接失败（Call parameter type does
                                 * not match function signature）。 */
                                LLVMTypeRef recv_t = map_type(g, ltype);
                                if (LLVMGetTypeKind(LLVMTypeOf(cargs[0])) ==
                                        LLVMPointerTypeKind &&
                                    LLVMGetTypeKind(recv_t) == LLVMStructTypeKind) {
                                    cargs[0] = LLVMBuildLoad2(g->builder, recv_t,
                                                              cargs[0], "op.recv");
                                }
                                if (!op_is_static &&
                                    LLVMGetTypeKind(LLVMTypeOf(cargs[0])) == LLVMStructTypeKind) {
                                    LLVMValueRef rslot = emit_entry_alloca(g,
                                        LLVMTypeOf(cargs[0]), "opb.recv");
                                    LLVMBuildStore(g->builder, cargs[0], rslot);
                                    cargs[0] = rslot;
                                }
                                /* 通过泛型特化具体方法发射调用 */
                                LLVMTypeRef mft = g->functions[fi].fn_type;
                                LLVMValueRef mfn = route_generic_method(g, ltype,
                                    op_sym, g->functions[fi].fn, mft, &mft);
                                zan_type_t *p1 = method_param_type_at(g, op_sym, 1,
                                    expr, expr->binary.left, locals);
                                bool op_generic = p1 && type_mentions_tp(p1);
                                if (p1) p1 = subst_type_param_deep(g, p1, ltype);
                                if (p1 && !op_generic) {
                                    LLVMTypeRef p1t = map_type(g, p1);
                                    if (LLVMGetTypeKind(p1t) == LLVMStructTypeKind &&
                                        LLVMGetTypeKind(LLVMTypeOf(cargs[1])) == LLVMPointerTypeKind) {
                                        cargs[1] = LLVMBuildLoad2(g->builder, p1t,
                                                                  cargs[1], "op.sval");
                                    }
                                    if (LLVMTypeOf(cargs[1]) != p1t)
                                        cargs[1] = coerce_int_to(g, cargs[1], p1t);
                                }
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "opcall";
                                LLVMValueRef opres = zan_call2(g->builder,
                                    mft, mfn, cargs, 2, cn);
                                /* 原地委托组合赋值 (E += obj.OnX)：包装为委托实例后执行组合操作 */
                                /* 运算符重载实参入栈发射 */
                                if (expr_yields_delegate_value(g, expr->binary.right, locals))
                                    emit_closure_release(g, right);
                                else
                                    emit_release_owned_call_temp(g, expr->binary.right, right, locals);
                                emit_release_owned_call_temp(g, expr->binary.left, left, locals);
                                return opres;
                            }
                        }
                    }
                }
            }
        }

        /* 操作数包含可空类型时提升运算；字符串则走拼接逻辑 */
        if ((llvm_is_nullable(LLVMTypeOf(left)) ||
             llvm_is_nullable(LLVMTypeOf(right))) &&
            !is_string_expr(g, expr->binary.left, locals) &&
            !is_string_expr(g, expr->binary.right, locals))
            return emit_nullable_binary(g, expr, left, right, locals);

        /* 128 位定点数 decimal 二元运算与比较 */
        bool has_str_op = is_string_expr(g, expr->binary.left, locals) ||
                          is_string_expr(g, expr->binary.right, locals);
        if ((LLVMTypeOf(left) == get_decimal_type(g) ||
             LLVMTypeOf(right) == get_decimal_type(g)) && !has_str_op) {
            return emit_decimal_binary(g, expr, left, right, locals);
        }

        /* 结构体逐字段相等性比较 */
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

        /* 委托判等：比对函数指针与绑定目标实例指针 */
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

        /* 字符串拼接 (a + b)：操作数含 string 时发射 String.Concat */
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

        /* 托管字符串内容判等 */
        if ((expr->binary.op == TK_EQ_EQ || expr->binary.op == TK_BANG_EQ) &&
            both_ptr && str_operand &&
            expr->binary.left->kind != AST_NULL_LITERAL &&
            expr->binary.right->kind != AST_NULL_LITERAL) {
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef lc = emit_str_nonnull(g, left);
            LLVMValueRef rc = emit_str_nonnull(g, right);
            /* 带长度的托管字符串字典序比对 */
            LLVMValueRef ocmp = get_str_ordinal_cmp_fn(g, expr->loc);
            LLVMValueRef r = zan_call2(g->builder,
                LLVMGlobalGetValueType(ocmp), ocmp,
                (LLVMValueRef[]){ lc, rc }, 2, "scmp");
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

        /* 字符串大小比较：将 < / <= / > / >= 路由至 strcmp 字符序比对 */
        if ((expr->binary.op == TK_LESS || expr->binary.op == TK_LESS_EQ ||
             expr->binary.op == TK_GREATER || expr->binary.op == TK_GREATER_EQ) &&
            both_ptr && str_operand &&
            expr->binary.left->kind != AST_NULL_LITERAL &&
            expr->binary.right->kind != AST_NULL_LITERAL) {
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef lc = emit_str_nonnull(g, left);
            LLVMValueRef rc = emit_str_nonnull(g, right);
            LLVMValueRef ocmp = get_str_ordinal_cmp_fn(g, expr->loc);
            LLVMValueRef r = zan_call2(g->builder,
                LLVMGlobalGetValueType(ocmp), ocmp,
                (LLVMValueRef[]){ lc, rc }, 2, "scmp");
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

        /* 指针与空值相等性判断 (== null) */
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

        /* 值类型与 null 的比较：泛型形参 T 在特化成结构体/数值等值类型后，
         * 值永远不可能为 null，直接比较会生成「聚合值 vs 空指针」的非法 IR
         * （LLVM 校验失败：Both operands to ICmp instruction are not of the
         * same type）。这里按「值类型不是 null」折叠成常量。 */
        if ((expr->binary.op == TK_EQ_EQ || expr->binary.op == TK_BANG_EQ) &&
            !both_ptr) {
            bool l_null = LLVMGetTypeKind(LLVMTypeOf(left)) == LLVMPointerTypeKind &&
                          LLVMIsNull(left);
            bool r_null = LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMPointerTypeKind &&
                          LLVMIsNull(right);
            if (l_null != r_null) {
                LLVMValueRef val = l_null ? right : left;
                LLVMTypeRef vt = LLVMTypeOf(val);
                LLVMTypeKind vk = LLVMGetTypeKind(vt);
                bool value_kind = vk == LLVMStructTypeKind ||
                                  vk == LLVMIntegerTypeKind ||
                                  vk == LLVMDoubleTypeKind ||
                                  vk == LLVMFloatTypeKind;
                /* 可空值类型走 zan.nullable.* 聚合体，其「是否为 null」由
                 * has-value 标记决定，不能按值类型折叠 */
                if (value_kind &&
                    !(vk == LLVMStructTypeKind && llvm_is_nullable(vt))) {
                    emit_release_owned_call_temp(g, expr->binary.left, left, locals);
                    emit_release_owned_call_temp(g, expr->binary.right, right, locals);
                    return LLVMConstInt(LLVMInt1TypeInContext(g->ctx),
                                        expr->binary.op == TK_BANG_EQ ? 1 : 0, 0);
                }
            }
        }

        return emit_binary_op_values(g, expr, left, right, locals);
}

/* 判断表达式运行时是否可能包含 string 对象 */
static bool recv_is_stringlike(zan_irgen_t *g, zan_ast_node_t *e,
                               local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    if (!t) return true;
    return t->kind == TYPE_STRING || t->kind == TYPE_OBJECT ||
           t->kind == TYPE_ERROR || t->kind == TYPE_TYPE_PARAM;
}

/* 移位计数按左操作数位宽取模 (1 << 33 规范化为 1 << 1) */
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
            bits = 32;
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
        /* 混合位宽整数与浮点数类型隐式提升转换 */
        coerce_int_pair(g, &left, &right);

        /* A510：定型为 32 位 int/uint 的算术/移位/位运算按 32 位环绕（C#
         * unchecked 口径）——曾一律 64 位寄存器，表达式级溢出不环绕
         * （int.MaxValue+1 打出 2147483648、0u-1u 打出 64 位回绕、-8>>>1
         * 按 64 位移）。比较运算定型是 bool 不走这条；臂内层的 int 表达式
         * 已各自按 i32 包回，这里只截对齐后的公共位宽。 */
        {
            zan_type_t *bty = infer_expr_type(g, expr, locals);
            LLVMTypeRef lty = LLVMTypeOf(left);
            if (bty && (bty->kind == TYPE_INT || bty->kind == TYPE_UINT) &&
                LLVMGetTypeKind(lty) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(lty) > 32) {
                LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                left = LLVMBuildTrunc(g->builder, left, i32t, "n32.l");
                right = LLVMBuildTrunc(g->builder, right, i32t, "n32.r");
            }
        }

        /* 双操作数类型对齐后根据统一类型发射相应指令 */
        LLVMTypeRef left_type = LLVMTypeOf(left);
        bool is_float = (LLVMGetTypeKind(left_type) == LLVMDoubleTypeKind ||
                         LLVMGetTypeKind(left_type) == LLVMFloatTypeKind ||
                         LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMDoubleTypeKind ||
                         LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMFloatTypeKind);

        /* 无符号数选择 udiv/urem/lshr/icmp 指令 */
        bool is_unsigned = !is_float &&
            (expr_is_ulong(g, expr->binary.left, locals) ||
             expr_is_ulong(g, expr->binary.right, locals) ||
             expr_is_uint(g, expr->binary.left, locals) ||
             expr_is_uint(g, expr->binary.right, locals));

        /* checked 上下文下的整数溢出捕获陷阱 */
        int check_ctx = expr->binary.checked
            ? (expr->binary.checked > 0 ? 1 : -1)
            : (g->irgen_checked_depth > 0 ? 1 : 0);
        bool want_overflow_check = check_ctx > 0 && !is_float;
        LLVMTypeRef res_ty = LLVMTypeOf(left);
        if (want_overflow_check &&
            !(LLVMGetTypeKind(res_ty) == LLVMIntegerTypeKind &&
              LLVMGetIntTypeWidth(res_ty) > 1))
            want_overflow_check = false;

        switch (expr->binary.op) {
        case TK_PLUS:
            if (is_float) return LLVMBuildFAdd(g->builder, left, right, "add");
            if (want_overflow_check)
                return emit_checked_int_arith(g, expr, left, right,
                                              CHK_ADD, locals);
            return zan_add(g->builder, left, right, "add");
        case TK_MINUS:
            if (is_float) return LLVMBuildFSub(g->builder, left, right, "sub");
            if (want_overflow_check)
                return emit_checked_int_arith(g, expr, left, right,
                                              CHK_SUB, locals);
            return zan_sub(g->builder, left, right, "sub");
        case TK_STAR:
            if (is_float) return LLVMBuildFMul(g->builder, left, right, "mul");
            if (want_overflow_check)
                return emit_checked_int_arith(g, expr, left, right,
                                              CHK_MUL, locals);
            return zan_mul(g->builder, left, right, "mul");
        case TK_SLASH:
            if (is_float) return LLVMBuildFDiv(g->builder, left, right, "div");
            {
                /* 整数除法软路径零除与溢出防护 */
                LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(right), 0, 0);
                LLVMValueRef one = LLVMConstInt(LLVMTypeOf(right), 1, 0);
                LLVMValueRef is_zero = zan_icmp(g->builder, LLVMIntEQ, right, zero, "divz");
                emit_runtime_check(g, is_zero, expr->loc, "division by zero");
                LLVMValueRef unsafe = is_zero;
                if (!is_unsigned) {
                    unsigned w = LLVMGetIntTypeWidth(LLVMTypeOf(left));
                    LLVMValueRef smin = LLVMConstInt(LLVMTypeOf(left),
                        (unsigned long long)1 << (w - 1), 1);
                    LLVMValueRef minus_one = LLVMConstAllOnes(LLVMTypeOf(right));
                    LLVMValueRef is_min = zan_icmp(g->builder, LLVMIntEQ, left, smin, "divmin");
                    LLVMValueRef is_m1 = zan_icmp(g->builder, LLVMIntEQ, right, minus_one, "divm1");
                    unsafe = LLVMBuildOr(g->builder, unsafe,
                        LLVMBuildAnd(g->builder, is_min, is_m1, "divovf"),
                        "divunsafe");
                }
                LLVMValueRef divisor = LLVMBuildSelect(g->builder, unsafe,
                    one, right, "divz.safe");
                LLVMValueRef div_res = is_unsigned ? zan_udiv(g->builder, left, divisor, "div")
                                                   : zan_sdiv(g->builder, left, divisor, "div");
                return LLVMBuildSelect(g->builder, unsafe, zero, div_res, "divz.res");
            }
        case TK_PERCENT:
            if (is_float) return LLVMBuildFRem(g->builder, left, right, "rem");
            {
                LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(right), 0, 0);
                LLVMValueRef one = LLVMConstInt(LLVMTypeOf(right), 1, 0);
                LLVMValueRef is_zero = zan_icmp(g->builder, LLVMIntEQ, right, zero, "remz");
                emit_runtime_check(g, is_zero, expr->loc, "division by zero (modulo)");
                LLVMValueRef unsafe = is_zero;
                if (!is_unsigned) {
                    unsigned w = LLVMGetIntTypeWidth(LLVMTypeOf(left));
                    LLVMValueRef smin = LLVMConstInt(LLVMTypeOf(left),
                        (unsigned long long)1 << (w - 1), 1);
                    LLVMValueRef minus_one = LLVMConstAllOnes(LLVMTypeOf(right));
                    LLVMValueRef is_min = zan_icmp(g->builder, LLVMIntEQ, left, smin, "remmin");
                    LLVMValueRef is_m1 = zan_icmp(g->builder, LLVMIntEQ, right, minus_one, "remm1");
                    unsafe = LLVMBuildOr(g->builder, unsafe,
                        LLVMBuildAnd(g->builder, is_min, is_m1, "removf"),
                        "remunsafe");
                }
                LLVMValueRef divisor = LLVMBuildSelect(g->builder, unsafe,
                    one, right, "remz.safe");
                LLVMValueRef rem_res = is_unsigned ? zan_urem(g->builder, left, divisor, "rem")
                                                   : zan_srem(g->builder, left, divisor, "rem");
                return LLVMBuildSelect(g->builder, unsafe, zero, rem_res, "remz.res");
            }
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
        case TK_GREATER_GREATER_GREATER:
            /* 无符号逻辑右移指令发射 (lshr) */
            right = mask_shift_count(g, expr->binary.left, right, locals);
            return zan_lshr(g->builder, left, right, "ushr");
        case TK_EQ_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOEQ, left, right, "eq")
                            : zan_icmp(g->builder, LLVMIntEQ, left, right, "eq");
        case TK_BANG_EQ:
            /* 浮点数 NaN 比较符合 IEEE 规范 */
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealUNE, left, right, "ne")
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

/* checked 溢出检测整数算术实现 */
static LLVMValueRef emit_checked_int_arith(zan_irgen_t *g, zan_ast_node_t *expr,
                                           LLVMValueRef left, LLVMValueRef right,
                                           int kind, local_scope_t *locals) {
    LLVMTypeRef ty = LLVMTypeOf(left);
    unsigned bits = LLVMGetIntTypeWidth(ty);
    bool is_u64 = expr_is_ulong(g, expr->binary.left, locals) ||
                  expr_is_ulong(g, expr->binary.right, locals);
    /* uint 运算溢出范围检查 */
    bool is_u32 = !is_u64 &&
        (expr_is_uint(g, expr->binary.left, locals) ||
         expr_is_uint(g, expr->binary.right, locals)) &&
        !expr_is_longish(g, expr->binary.left, locals) &&
        !expr_is_longish(g, expr->binary.right, locals);
    bool is_unsigned = is_u64 || is_u32;
    /* 字面量整数运算在 i64 拓宽精度下求值 */
    bool clamp32 = !is_unsigned && bits > 32 &&
                   expr_is_int32(g, expr->binary.left, locals) &&
                   expr_is_int32(g, expr->binary.right, locals);
    LLVMValueRef res, ovf;
    if (is_u32) {
        /* uint 算术运算中间值类型适配与截断 */
        LLVMTypeRef i32t = LLVMIntTypeInContext(g->ctx, 32);
        LLVMTypeRef i64t = LLVMIntTypeInContext(g->ctx, 64);
        LLVMValueRef l64 = LLVMBuildZExt(g->builder,
            LLVMBuildTrunc(g->builder, left, i32t, "ck.u32l"), i64t, "ck.u32lz");
        LLVMValueRef r64 = LLVMBuildZExt(g->builder,
            LLVMBuildTrunc(g->builder, right, i32t, "ck.u32r"), i64t, "ck.u32rz");
        LLVMValueRef u32max = LLVMConstInt(i64t, 0xFFFFFFFFULL, 0);
        if (kind == CHK_ADD) {
            res = zan_add(g->builder, l64, r64, "ck.u32.add");
            ovf = zan_icmp(g->builder, LLVMIntUGT, res, u32max, "ck.u32.ovf");
        } else if (kind == CHK_SUB) {
            res = zan_sub(g->builder, l64, r64, "ck.u32.sub");
            ovf = zan_icmp(g->builder, LLVMIntSLT, res,
                LLVMConstInt(i64t, 0, 0), "ck.u32.ovf");
        } else {
            res = zan_mul(g->builder, l64, r64, "ck.u32.mul");
            ovf = zan_icmp(g->builder, LLVMIntUGT, res, u32max, "ck.u32.ovf");
        }
        res = LLVMBuildTrunc(g->builder, res, ty, "ck.u32.res");
    } else if (kind == CHK_ADD) {
        res = zan_add(g->builder, left, right, "ck.add");
        if (is_unsigned)
            ovf = zan_icmp(g->builder, LLVMIntULT, res, left, "ck.add.ovf");
        else if (clamp32) {
            LLVMValueRef lo = LLVMConstInt(ty, -(1LL << 31), 1);
            LLVMValueRef hi = LLVMConstInt(ty, (1LL << 31) - 1, 1);
            LLVMValueRef under = zan_icmp(g->builder, LLVMIntSLT, res, lo,
                "ck.lo");
            LLVMValueRef over = zan_icmp(g->builder, LLVMIntSGT, res, hi,
                "ck.hi");
            ovf = zan_or(g->builder, under, over, "ck.add.ovf");
        }
        else {
            /* 有符号加法溢出检测：同号相加符号突变判定 */
            LLVMValueRef lneg = zan_icmp(g->builder, LLVMIntSLT, left,
                LLVMConstInt(ty, 0, 0), "ck.sl");
            LLVMValueRef rneg = zan_icmp(g->builder, LLVMIntSLT, right,
                LLVMConstInt(ty, 0, 0), "ck.sr");
            LLVMValueRef same_sign_lr = zan_icmp(g->builder, LLVMIntEQ,
                lneg, rneg, "ck.sign.lr");
            LLVMValueRef res_neg = zan_icmp(g->builder, LLVMIntSLT, res,
                LLVMConstInt(ty, 0, 0), "ck.sr2");
            LLVMValueRef res_diff_left = zan_xor(g->builder, res_neg, lneg,
                "ck.sign.r");
            ovf = zan_and(g->builder, same_sign_lr, res_diff_left,
                          "ck.add.ovf");
        }
    } else if (kind == CHK_SUB) {
        res = zan_sub(g->builder, left, right, "ck.sub");
        if (is_unsigned)
            ovf = zan_icmp(g->builder, LLVMIntULT, left, right, "ck.sub.ovf");
        else if (clamp32) {
            LLVMValueRef lo = LLVMConstInt(ty, -(1LL << 31), 1);
            LLVMValueRef hi = LLVMConstInt(ty, (1LL << 31) - 1, 1);
            LLVMValueRef under = zan_icmp(g->builder, LLVMIntSLT, res, lo,
                "ck.lo");
            LLVMValueRef over = zan_icmp(g->builder, LLVMIntSGT, res, hi,
                "ck.hi");
            ovf = zan_or(g->builder, under, over, "ck.sub.ovf");
        }
        else {
            /* 有符号减法溢出检测：异号相减符号突变判定 */
            LLVMValueRef lneg = zan_icmp(g->builder, LLVMIntSLT, left,
                LLVMConstInt(ty, 0, 0), "ck.sl");
            LLVMValueRef rneg = zan_icmp(g->builder, LLVMIntSLT, right,
                LLVMConstInt(ty, 0, 0), "ck.sr");
            LLVMValueRef diff_sign = zan_xor(g->builder, lneg, rneg,
                "ck.diff");
            LLVMValueRef res_neg = zan_icmp(g->builder, LLVMIntSLT, res,
                LLVMConstInt(ty, 0, 0), "ck.sr2");
            LLVMValueRef res_diff_left = zan_xor(g->builder, res_neg, lneg,
                "ck.sign.r");
            ovf = zan_and(g->builder, diff_sign, res_diff_left, "ck.sub.ovf");
        }
    } else { /* CHK_MUL */
        if (is_unsigned) {
            /* 乘法溢出检测：反除法校验结果是否回绕 */
            res = zan_mul(g->builder, left, right, "ck.mul");
            LLVMValueRef nonzero = zan_icmp(g->builder, LLVMIntNE, left,
                LLVMConstInt(ty, 0, 0), "ck.nz");
            LLVMValueRef dvs = LLVMBuildSelect(g->builder, nonzero, left,
                LLVMConstInt(ty, 1, 0), "ck.mul.dv");
            LLVMValueRef q = zan_udiv(g->builder, res, dvs, "ck.mul.q");
            LLVMValueRef back = zan_icmp(g->builder, LLVMIntNE, q, right,
                "ck.mul.back");
            ovf = zan_and(g->builder, nonzero, back, "ck.mul.ovf");
        } else {
            /* 双倍位宽乘法溢出校验 */
            LLVMTypeRef wide = LLVMIntTypeInContext(g->ctx, bits * 2);
            LLVMValueRef wl, wr, wp;
            wl = LLVMBuildSExt(g->builder, left, wide, "ck.mul.wl");
            wr = LLVMBuildSExt(g->builder, right, wide, "ck.mul.wr");
            wp = LLVMBuildMul(g->builder, wl, wr, "ck.mul.wp");
            LLVMValueRef narrow = LLVMBuildTrunc(g->builder, wp, ty, "ck.mul.tr");
            LLVMValueRef back = LLVMBuildSExt(g->builder, narrow, wide,
                "ck.mul.back");
            ovf = zan_icmp(g->builder, LLVMIntNE, wp, back, "ck.mul.ovf");
            res = narrow;
        }
    }
    emit_runtime_check(g, ovf, expr->loc,
        "integer overflow in checked operation");
    (void)locals;
    return res;
}

/* 构造同操作数不同操作符的二元 AST 节点用于降解 */
static zan_ast_node_t *binary_with_op(zan_irgen_t *g, zan_ast_node_t *expr,
                                      int op) {
    zan_ast_node_t *n = zan_ast_new(g->arena, AST_BINARY, expr->loc);
    n->binary.op = op;
    n->binary.left = expr->binary.left;
    n->binary.right = expr->binary.right;
    return n;
}

/* ?? 的短路求值：右支只在左支为 null 时求值（C# 语义，TASKS.md A515）。
 * 旧实现对两臂无条件求值后再 select——带副作用的右支（日志/计数/惰性初始化）
 * 被错误触发，是 skill 二.2 禁止的 select 死臂形态。
 * 只对三种干净形态走惰性路径（指针同型 / 双可空聚合 / 可空左+裸负载同型）；
 * 形态覆盖不了在**发码前**返回 NULL，调用方回退旧的急切两臂路径——
 * 绝不在发码中途放弃，避免悬空块与二次求值。 */
static LLVMValueRef emit_null_coalesce(zan_irgen_t *g, zan_ast_node_t *expr,
                                       local_scope_t *locals) {
    if (!g->current_fn || !expr || !expr->binary.left || !expr->binary.right)
        return NULL;
    zan_type_t *lst = infer_expr_type(g, expr->binary.left, locals);
    zan_type_t *rst = infer_expr_type(g, expr->binary.right, locals);
    if (!lst || !rst || expr->binary.left->kind == AST_NULL_LITERAL ||
        expr->binary.right->kind == AST_NULL_LITERAL)
        return NULL;
    LLVMTypeRef llt_static = map_type(g, lst);
    LLVMTypeRef rlt_static = rst ? map_type(g, rst) : NULL;
    if (!llt_static || !rlt_static) return NULL;
    bool l_nullable = llvm_is_nullable(llt_static) != 0;
    bool l_ptr = LLVMGetTypeKind(llt_static) == LLVMPointerTypeKind;
    if (!l_ptr && !l_nullable) return NULL;

    if (l_ptr) {
        /* 指针左支：右支静态类型必须映射到同一 LLVM 指针型 */
        if (LLVMGetTypeKind(rlt_static) != LLVMPointerTypeKind ||
            rlt_static != llt_static)
            return NULL;
    } else {
        bool r_nullable = llvm_is_nullable(rlt_static) != 0;
        if (r_nullable) {
            /* T? ?? T?：聚合体直取，要求两臂聚合 LLVM 型一致 */
            if (rlt_static != llt_static) return NULL;
        } else {
            /* T? ?? T：结果取负载型，要求右支静态 LLVM 型 == 左负载型 */
            if (!lst->element_type) return NULL;
            LLVMTypeRef payload_ty = map_type(g, lst->element_type);
            if (!payload_ty || payload_ty != rlt_static) return NULL;
        }
    }

    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMValueRef left = emit_expr(g, expr->binary.left, locals);
    LLVMTypeRef lt = LLVMTypeOf(left);
    l_nullable = llvm_is_nullable(lt) != 0;
    LLVMValueRef is_null;
    if (l_nullable)
        is_null = LLVMBuildNot(g->builder, nullable_has_value(g, left),
                               "coal.lnull");
    else
        is_null = zan_icmp(g->builder, LLVMIntEQ, left, LLVMConstNull(lt),
                           "coal.lnull");

    /* p07 批次回归照出的存量缺陷：负载提取必须在 left_bb（分支前）完成
     * ——曾在 merge 块里先发 nullable_get_payload 提取指令再建 phi，触发
     * "PHI nodes not grouped at top of basic block!"（`int y = x ?? 5;`）。 */
    LLVMTypeRef res_ty = lt;
    LLVMValueRef lres = left;
    if (l_nullable) {
        LLVMValueRef lpayload = nullable_get_payload(g, left);
        res_ty = LLVMTypeOf(lpayload);
        lres = lpayload;
    }

    LLVMBasicBlockRef left_bb = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef rhs_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "coal.rhs");
    LLVMBasicBlockRef lhs_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "coal.lhs");
    LLVMBasicBlockRef merge = LLVMAppendBasicBlockInContext(g->ctx, fn, "coal.end");
    LLVMBuildCondBr(g->builder, is_null, rhs_bb, lhs_bb);

    LLVMPositionBuilderAtEnd(g->builder, lhs_bb);
    int left_owned = expr_yields_owned_rc_value(g, expr->binary.left, locals);
    int right_owned = expr_yields_owned_rc_value(g, expr->binary.right, locals);
    zan_type_t *coal_type = infer_expr_type(g, expr, locals);
    if (!coal_type) coal_type = lst;
    if (coal_type && is_rc_managed_type(coal_type) && !left_owned && right_owned) {
        emit_rc_retain_for_type(g, coal_type, lres);
    }
    LLVMBasicBlockRef lhs_end = LLVMGetInsertBlock(g->builder);
    LLVMBuildBr(g->builder, merge);

    LLVMPositionBuilderAtEnd(g->builder, rhs_bb);
    LLVMValueRef right = emit_expr(g, expr->binary.right, locals);
    LLVMBasicBlockRef rhs_end = LLVMGetInsertBlock(g->builder);
    LLVMValueRef rres = right;
    if (l_nullable && LLVMTypeOf(right) != res_ty)
        rres = coerce_int_to(g, right, res_ty);
    if (coal_type && is_rc_managed_type(coal_type) && left_owned && !right_owned) {
        emit_rc_retain_for_type(g, coal_type, rres);
    }
    LLVMBuildBr(g->builder, merge);

    LLVMPositionBuilderAtEnd(g->builder, merge);
    LLVMValueRef phi = LLVMBuildPhi(g->builder, res_ty, "coal");
    LLVMValueRef vals[] = { lres, rres };
    LLVMBasicBlockRef bbs[] = { lhs_end, rhs_end };
    LLVMAddIncoming(phi, vals, bbs, 2);
    return phi;
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

    if ((op == TK_EQ_EQ || op == TK_BANG_EQ) &&
        (left_is_null_lit || right_is_null_lit)) {
        LLVMValueRef has = left_is_null_lit ? rhas : lhas;
        return op == TK_EQ_EQ ? LLVMBuildNot(g->builder, has, "nv.isnull") : has;
    }

    /* 可空合并运算符 ?? 求值 */
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
        /* 可空类型相等性判断：均为 null 或内部值均相等 */
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
        LLVMValueRef cmp = emit_binary_op_values(g, expr, lval, rval, locals);
        return LLVMBuildAnd(g->builder, both, cmp, "nv.cmp");
    }

    /* 可空类型四则运算：任一操作数为 null 则结果为 null */
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

static LLVMValueRef emit_incdec_expr(zan_irgen_t *g, zan_ast_node_t *expr,
                                     local_scope_t *locals, int is_prefix);

/* 一元运算符重载：类/结构体上的 static `operator -` / `operator +`。
 *
 * 解析器把 `operator -` / `operator +` 统一命名成 op_sub / op_add，一元与
 * 二元靠**形参个数**区分：重载是 static 的，形参 1 个是一元、2 个是二元。
 * 因此这里的探针**不带实参**（arity 期望 0 个显式实参 + 1 个接收者）。
 *
 * 找不到重载时报一条人话诊断并返回 undef——不能落到内建整数算术去对结构体
 * 指针做 sub（那是 LLVM 校验失败："Integer arithmetic operators only work
 * with integral types"），更不能默默当 0。
 *
 * 返回 NULL 表示"操作数不是类/结构体"，交回内建算术路径。 */
static LLVMValueRef emit_unary_op_overload(zan_irgen_t *g, zan_ast_node_t *expr,
                                            local_scope_t *locals) {
    zan_type_t *ot = infer_expr_type(g, expr->unary.operand, locals);
    if (!ot || !ot->sym ||
        (ot->kind != TYPE_CLASS && ot->kind != TYPE_STRUCT))
        return NULL;

    const char *op_name =
        (expr->unary.op == TK_MINUS) ? "op_sub" :
        (expr->unary.op == TK_PLUS) ? "op_add" :
        (expr->unary.op == TK_PLUS_PLUS) ? "op_inc" : "op_dec";
    const char *op_sign =
        (expr->unary.op == TK_MINUS) ? "-" :
        (expr->unary.op == TK_PLUS) ? "+" :
        (expr->unary.op == TK_PLUS_PLUS) ? "++" : "--";
    zan_istr_t op_istr = { (char *)op_name, (int)strlen(op_name) };

    zan_ast_node_t *probe = zan_ast_new(g->arena, AST_CALL, expr->loc);
    probe->call.callee = NULL;
    zan_ast_list_init(&probe->call.args);
    zan_ast_list_init(&probe->call.type_args);
    /* 一元重载：探针不带显式实参 */
    zan_symbol_t *op_sym = resolve_op_overload(g, ot->sym, op_istr, probe, locals);
    if (!op_sym) {
        /* ++/-- 的调用方（emit_incdec_expr）负责自己的诊断：这里静默
         * 返回 NULL，让"requires a numeric operand"维持原口径。 */
        if (expr->unary.op == TK_PLUS_PLUS || expr->unary.op == TK_MINUS_MINUS)
            return NULL;
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3004,
                      "type '%s' does not define a unary '%s' operator overload",
                      ot->name.str ? ot->name.str : "<unknown>", op_sign);
        return LLVMGetUndef(map_type(g, ot));
    }
    int fi = irgen_find_function(g, op_sym);
    if (fi < 0) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3005,
                      "unary '%s' operator overload of '%s' has no body to call",
                      op_sign, ot->name.str ? ot->name.str : "<unknown>");
        return LLVMGetUndef(map_type(g, ot));
    }

    /* 泛型方法的类型实参绑定要看到实参 → 探针补上操作数 */
    zan_ast_list_push(&probe->call.args, expr->unary.operand, g->arena);

    LLVMValueRef arg = emit_expr(g, expr->unary.operand, locals);
    LLVMTypeRef recv_t = map_type(g, ot);
    /* 接收者既可能是"值"，也可能是"槽位指针"（new 表达式返回的是槽位地址）：
     * 按值传参会把指针塞进调用里，LLVM 校验直接失败。 */
    if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMPointerTypeKind &&
        LLVMGetTypeKind(recv_t) == LLVMStructTypeKind) {
        arg = LLVMBuildLoad2(g->builder, recv_t, arg, "unop.recv");
    }
    bool op_is_static = (op_sym->modifiers & MOD_STATIC) != 0;
    if (!op_is_static &&
        LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMStructTypeKind) {
        LLVMValueRef rslot = emit_entry_alloca(g, LLVMTypeOf(arg), "unop.recv");
        LLVMBuildStore(g->builder, arg, rslot);
        arg = rslot;
    }
    /* 泛型特化：重载方法走具体实例的变体 */
    LLVMTypeRef mft = g->functions[fi].fn_type;
    LLVMValueRef mfn = route_generic_method(g, ot, op_sym, g->functions[fi].fn,
                                            mft, &mft);
    zan_type_t *p1 = method_param_type_at(g, op_sym, 1, probe,
                                          expr->unary.operand, locals);
    bool op_generic = p1 && type_mentions_tp(p1);
    if (p1) p1 = subst_type_param_deep(g, p1, ot);
    if (p1 && !op_generic) {
        LLVMTypeRef p1t = map_type(g, p1);
        if (LLVMGetTypeKind(p1t) == LLVMStructTypeKind &&
            LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMPointerTypeKind) {
            arg = LLVMBuildLoad2(g->builder, p1t, arg, "unop.sval");
        }
        if (LLVMTypeOf(arg) != p1t)
            arg = coerce_int_to(g, arg, p1t);
    }
    LLVMValueRef res = zan_call2(g->builder, mft, mfn, &arg, 1, "opcall");
    emit_release_owned_call_temp(g, expr->unary.operand, arg, locals);
    return res;
}

static LLVMValueRef emit_expr_unary(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
    if (expr->unary.op == TK_PLUS_PLUS || expr->unary.op == TK_MINUS_MINUS)
        return emit_incdec_expr(g, expr, locals, 1);
    if (expr->unary.op == TK_MINUS || expr->unary.op == TK_PLUS) {
        LLVMValueRef ov = emit_unary_op_overload(g, expr, locals);
        if (ov) return ov;
    }
    LLVMValueRef operand = emit_expr(g, expr->unary.operand, locals);
        switch (expr->unary.op) {
        case TK_MINUS: {
            LLVMTypeRef t = LLVMTypeOf(operand);
            if (t == get_decimal_type(g)) {
                LLVMValueRef slot = emit_entry_alloca(g, t, "dec.uneg_in");
                LLVMValueRef res_slot = emit_entry_alloca(g, t, "dec.uneg_res");
                LLVMBuildStore(g->builder, operand, slot);
                LLVMTypeRef ptr_ty = LLVMPointerType(t, 0);
                LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                    (LLVMTypeRef[]){ ptr_ty, ptr_ty }, 2, 0);
                LLVMValueRef fn = get_libc_fn(g, "__zan_decimal_neg", fn_ty);
                zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){ slot, res_slot }, 2, "");
                return LLVMBuildLoad2(g->builder, t, res_slot, "dec.uneg_val");
            }
            if (LLVMGetTypeKind(t) == LLVMDoubleTypeKind ||
                LLVMGetTypeKind(t) == LLVMFloatTypeKind) {
                return LLVMBuildFNeg(g->builder, operand, "neg");
            }
            return LLVMBuildNeg(g->builder, operand, "neg");
        }
        case TK_PLUS:
            return operand;
        case TK_BANG: {
            /* 逻辑非与按位取反指令发射 */
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

/* Binding<T> 属性与字段双向绑定原语降解 */

static int type_is_binding(zan_type_t *t) {
    return t && t->kind == TYPE_CLASS && t->name.len == 7 &&
           memcmp(t->name.str, "Binding", 7) == 0;
}

/* 泛型类上的委托字段签名特化与解析 */
static zan_type_t *subst_delegate_sig(zan_irgen_t *g, zan_type_t *t,
                                      zan_type_t *recv) {
    if (!t || t->kind != TYPE_DELEGATE || !recv || recv->type_arg_count <= 0)
        return t;
    bool needs = (t->delegate_ret_type && type_mentions_tp(t->delegate_ret_type));
    for (int i = 0; !needs && i < t->delegate_param_count; i++)
        needs = (t->delegate_param_types[i] && type_mentions_tp(t->delegate_param_types[i]));
    if (!needs) return t;

    zan_type_t *out = (zan_type_t *)zan_arena_alloc(g->arena, sizeof(zan_type_t));
    *out = *t;
    out->delegate_ret_type = subst_type_param_deep(g, t->delegate_ret_type, recv);
    if (t->delegate_param_count > 0) {
        out->delegate_param_types = (zan_type_t **)zan_arena_alloc(g->arena,
            sizeof(zan_type_t *) * (size_t)t->delegate_param_count);
        for (int i = 0; i < t->delegate_param_count; i++)
            out->delegate_param_types[i] =
                subst_type_param_deep(g, t->delegate_param_types[i], recv);
    }
    return out;
}

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

/* 为类字段合成或查询缓存的 getter/setter 访问器函数 */
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
        LLVMValueRef val;
        if (fs->modifiers & MOD_WEAK) {
            val = emit_weak_field_load(g, fptr, vt);
        } else {
            val = LLVMBuildLoad2(g->builder, vt, fptr, "val");
            if (is_rc_managed_type(fs->type))
                emit_rc_retain_for_type(g, fs->type, val);
        }
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
        /* 弱引用字段写入防护：维护弱引用注册表槽位 */
        emit_rc_store_field(g, fs->type, fptr, v, NULL, NULL,
                            (fs->modifiers & MOD_WEAK) ? 1 : 0);
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

/* 为右值构建 Binding<T> 实例对象（返回 +1 持有引用） */
static LLVMValueRef emit_binding_value(zan_irgen_t *g, zan_type_t *bind_t,
        zan_ast_node_t *rhs, local_scope_t *locals) {
    /* b = null 清除数据绑定状态 */
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

    /* 类内部裸字段名解析为 this 字段并绑定目标 */
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

    zan_symbol_t *cls = NULL;
    zan_symbol_t *ffs = NULL;
    if (rhs && rhs->kind == AST_MEMBER_ACCESS && !rhs->member.null_cond) {
        cls = expr_class_sym(g, rhs->member.object, locals);
        if (cls) {
            ffs = get_field_sym(cls, rhs->member.name);
            if (ffs && field_member_is_static(ffs)) ffs = NULL;
            if (ffs && !ffs->type) ffs = NULL;
            if (ffs && T && ffs->type->kind != T->kind) ffs = NULL;
            /* 绑定目标使用非持有弱引用，防止双向绑定循环引用导致内存泄漏 */
            if (ffs) {
                zan_type_t *ot = infer_expr_type(g, rhs->member.object, locals);
                if (ot && is_rc_managed_type(ot) &&
                    expr_yields_owned_rc_value(g, rhs->member.object, locals))
                    ffs = NULL;
            }
        }
    }
    LLVMValueRef get_fn = NULL, set_fn = NULL;
    if (ffs) {
        get_binding_accessors(g, cls, ffs, &get_fn, &set_fn);
        if (!get_fn || !set_fn) ffs = NULL;
    }

    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);

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
        LLVMValueRef obj_val = emit_expr(g, rhs->member.object, locals);
        LLVMValueRef tptr = LLVMBuildStructGEP2(g->builder, st, objp, (unsigned)fi_target, "b.tgt");
        LLVMTypeRef tgt_t = LLVMStructGetTypeAtIndex(st, (unsigned)fi_target);
        LLVMValueRef obj_cast = obj_val;
        if (LLVMTypeOf(obj_cast) != tgt_t &&
            LLVMGetTypeKind(LLVMTypeOf(obj_cast)) == LLVMPointerTypeKind)
            obj_cast = LLVMBuildBitCast(g->builder, obj_cast, tgt_t, "b.tgt.bc");
        /* 非持有弱引用绑定目标管理 */
        zan_store_fit(g, obj_cast, tptr);

        LLVMValueRef gptr = LLVMBuildStructGEP2(g->builder, st, objp, (unsigned)fi_getter, "b.get");
        LLVMTypeRef gslot_t = LLVMStructGetTypeAtIndex(st, (unsigned)fi_getter);
        zan_store_fit(g,
            LLVMBuildBitCast(g->builder,
                emit_binding_acc_delegate(g, get_fn,
                    LLVMGlobalGetValueType(get_fn)),
                gslot_t, "b.get.bc"), gptr);
        LLVMValueRef sptr = LLVMBuildStructGEP2(g->builder, st, objp, (unsigned)fi_setter, "b.set");
        LLVMTypeRef sslot_t = LLVMStructGetTypeAtIndex(st, (unsigned)fi_setter);
        zan_store_fit(g,
            LLVMBuildBitCast(g->builder,
                emit_binding_acc_delegate(g, set_fn,
                    LLVMGlobalGetValueType(set_fn)),
                sslot_t, "b.set.bc"), sptr);
        zan_store_fit(g, LLVMConstInt(live_t, 1, 0), live_ptr);
    } else {
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

/* 控制台颜色属性赋值跟踪槽 */
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

static LLVMValueRef emit_console_color_get(zan_irgen_t *g, int is_bg) {
    return LLVMBuildLoad2(g->builder, LLVMInt64TypeInContext(g->ctx),
                          console_color_slot(g, is_bg), "concolor");
}

static void emit_console_color_reset(zan_irgen_t *g) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMBuildStore(g->builder,
        LLVMConstInt(i64, ZAN_CONSOLE_FG_DEFAULT, 0), console_color_slot(g, 0));
    LLVMBuildStore(g->builder,
        LLVMConstInt(i64, ZAN_CONSOLE_BG_DEFAULT, 0), console_color_slot(g, 1));
}

/* 为控制台前景色/背景色赋值发射 ANSI 转义序列 */
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

/* 通过 ANSI/VT OSC 序列设置控制台窗口标题 */
static void emit_console_title(zan_irgen_t *g, LLVMValueRef s) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef pty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 1);
    LLVMValueRef pf = LLVMGetNamedFunction(g->mod, "printf");
    if (!pf) pf = LLVMAddFunction(g->mod, "printf", pty);
    LLVMValueRef fmt = zan_irgen_intern_string(g, "\033]0;%s\007");
    zan_call2(g->builder, pty, pf, (LLVMValueRef[]){ fmt, s }, 2, "");
}

/* 字段写入操作所需处理的真实存储类型推导 */
static zan_type_t *field_store_type(zan_irgen_t *g, zan_symbol_t *fsym,
                                    zan_type_t *recv) {
    zan_type_t *t = field_type_at(g, fsym, recv);
    if (!t) return NULL;
    return concretize(g, t);
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
            /* 自定义访问器属性无后备存储槽，通过方法调用存取 */
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
        if (field_type && (is_rc_managed_type(field_type) || field_type->kind == TYPE_OBJECT)) {
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

/* 判断接收者表达式是否持有消费方需释放的所有权引用 */
static int receiver_is_owned_temp(zan_irgen_t *g, zan_ast_node_t *object,
                                  local_scope_t *locals, zan_type_t *obj_type,
                                  LLVMValueRef obj_val) {
    return obj_val && obj_type && is_rc_managed_type(obj_type) &&
           !expr_is_local_ident(object, locals) &&
           expr_yields_owned_rc_value(g, object, locals);
}

/* 链式调用中的临时接收者对象在访问完成后及时释放 */
static LLVMValueRef finish_member_of_temp(zan_irgen_t *g, zan_ast_node_t *expr,
                                          local_scope_t *locals,
                                          zan_type_t *obj_type,
                                          LLVMValueRef obj_val, LLVMValueRef fv) {
    if (!receiver_is_owned_temp(g, expr->member.object, locals, obj_type, obj_val))
        return fv;
    zan_type_t *ft = member_owned_field_type(g, expr, locals);
    if (ft && is_rc_managed_type(ft) &&
        LLVMGetTypeKind(LLVMTypeOf(fv)) == LLVMPointerTypeKind &&
        /* 弱引用字段读取时已从注册表获取 +1 强引用 */
        !member_field_is_weak(g, expr, locals))
        emit_rc_retain_for_type(g, ft, fv);
    emit_rc_release_for_type(g, obj_type, obj_val);
    return fv;
}

/* readonly 字段赋值校验与写入约束 */
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
    zan_diag_emit_code(g->diag, DIAG_ERROR, lhs->loc, 3006,
                  "cannot assign to readonly field '%.*s' outside a "
                  "constructor of '%.*s'",
                  (int)lhs->member.name.len, lhs->member.name.str,
                  (int)ot->sym->name.len, ot->sym->name.str);
}

/* 用户自定义隐式/显式类型转换运算符 */
static zan_symbol_t *find_user_conversion(zan_irgen_t *g, zan_type_t *from_type,
                                          zan_type_t *to_type,
                                          const char *op_name);
static LLVMValueRef emit_user_conversion(zan_irgen_t *g, zan_type_t *from_type,
                                         zan_type_t *to_type, const char *op_name,
                                         LLVMValueRef val, zan_ast_node_t *val_expr,
                                         local_scope_t *locals);

/* 多维矩形数组元素寻址：结合各维度形状计算一维平铺偏移 */
static LLVMValueRef emit_mdarray_elem_ptr(zan_irgen_t *g, LLVMValueRef arr_ptr,
        zan_ast_node_t *expr, LLVMTypeRef elem_llvm, local_scope_t *locals,
        LLVMValueRef *out_oob) {
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
    LLVMValueRef total_oob = emit_index_oob_pred(g, idx, dim0);
    emit_index_bounds_check(g, idx, dim0, expr->loc, "array");
    idx = emit_index_safe_bounds(g, idx, dim0, expr->loc, "array");
    LLVMValueRef flat = idx;
    for (int d = 1; d < rank; d++) {
        LLVMValueRef off = LLVMConstInt(i64, (unsigned long long)d, 0);
        LLVMValueRef dimv = LLVMBuildLoad2(g->builder, i64,
            LLVMBuildGEP2(g->builder, i64, dim_ptr, &off, 1, "md.dd"), "md.dim");
        zan_ast_node_t *extra_node = (d - 1 < expr->index.extra.count)
            ? expr->index.extra.items[d - 1] : NULL;
        LLVMValueRef ix = extra_node ? emit_expr(g, extra_node, locals) : LLVMConstInt(i64, 0, 0);
        ix = emit_index_i64(g, ix, "md.ix");
        LLVMValueRef cur_oob = emit_index_oob_pred(g, ix, dimv);
        total_oob = LLVMBuildOr(g->builder, total_oob, cur_oob, "md.oob");
        emit_index_bounds_check(g, ix, dimv, expr->loc, "array");
        ix = emit_index_safe_bounds(g, ix, dimv, expr->loc, "array");
        flat = zan_mul(g->builder, flat, dimv, "md.fm");
        flat = zan_add(g->builder, flat, ix, "md.fa");
    }
    if (out_oob) {
        *out_oob = total_oob;
    }
    LLVMValueRef data_off = LLVMConstInt(i64,
        (unsigned long long)rank * 8, 0);
    LLVMValueRef data = LLVMBuildGEP2(g->builder, i8, arr_ptr, &data_off, 1, "md.data");
    LLVMValueRef typed = LLVMBuildBitCast(g->builder, data,
        LLVMPointerType(elem_llvm, 0), "md.typed");
    return LLVMBuildGEP2(g->builder, elem_llvm, typed, &flat, 1, "md.ep");
}

/* 复合赋值：目标操作数仅求值一次，防止副作用重复触发 */
static int ca_synth_counter = 0;

static void ca_hoist_spine(zan_irgen_t *g, zan_ast_node_t **slot,
                           local_scope_t *locals, int is_root);

/* 将目标表达式求值入隐藏临时变量，并在原地替换为读取引用 */
static void ca_replace_with_temp(zan_irgen_t *g, zan_ast_node_t **slot,
                                 local_scope_t *locals) {
    zan_ast_node_t *node = *slot;
    if (!node) return;
    zan_type_t *type = infer_expr_type(g, node, locals);
    /* 缺乏明确类型时避免分配错误尺寸的临时槽 */
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
        ca_replace_with_temp(g, slot, locals);
        return;
    }
}

/* 计算索引表达式 (AST_INDEX) 选中的目标元素内存地址 */
static LLVMValueRef emit_struct_elem_ptr(zan_irgen_t *g, zan_ast_node_t *expr,
                                         local_scope_t *locals) {
    zan_ast_node_t *base = expr->index.object;
    zan_type_t *bt = infer_expr_type(g, base, locals);
    if (!bt) return NULL;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64)
        idx = LLVMBuildSExt(g->builder, idx, i64, "ix");
    if (type_named(bt, "List", 4)) {
        LLVMValueRef raw = emit_expr(g, base, locals);
        LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw,
            LLVMPointerType(g->list_struct_type, 0), "lptr");
        LLVMValueRef count = LLVMBuildLoad2(g->builder, i64,
            LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0,
                "cf"), "cnt");
        emit_index_bounds_check(g, idx, count, expr->loc, "list");
        idx = emit_index_safe_bounds(g, idx, count, expr->loc, "list");
        idx = slot_word_index(g, idx, elem_slot_words(g, container_elem_type(bt)));
        LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0),
            LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2,
                "df"), "data");
        LLVMValueRef ep = LLVMBuildGEP2(g->builder, i64, data, &idx, 1, "ep");
        return LLVMBuildBitCast(g->builder, ep, i8p, "epb");
    }
    if (bt->kind == TYPE_ARRAY && bt->array_rank == 1) {
        LLVMValueRef arr = emit_expr(g, base, locals);
        emit_index_bounds_check(g, idx, zan_array_len(g, arr), expr->loc, "array");
        idx = emit_index_safe_bounds(g, idx, zan_array_len(g, arr),
            expr->loc, "array");
        zan_type_t *et = container_elem_type(bt);
        LLVMTypeRef etl = et ? map_type(g, et) : i64;
        LLVMValueRef typed = LLVMBuildBitCast(g->builder, arr,
            LLVMPointerType(etl, 0), "arrp");
        LLVMValueRef ep = LLVMBuildGEP2(g->builder, etl, typed, &idx, 1, "ep");
        return LLVMBuildBitCast(g->builder, ep, i8p, "epb");
    }
    if (is_span_type(bt)) {
        LLVMValueRef sv = emit_expr(g, base, locals);
        LLVMValueRef sbase = LLVMBuildExtractValue(g->builder, sv, 0, "spb");
        LLVMValueRef slen = LLVMBuildExtractValue(g->builder, sv, 1, "spl");
        emit_index_bounds_check(g, idx, slen, expr->loc, "span");
        idx = emit_index_safe_bounds(g, idx, slen, expr->loc, "span");
        zan_type_t *et = container_elem_type(bt);
        LLVMTypeRef etl = span_elem_storage_type(g, et);
        LLVMValueRef typed = LLVMBuildBitCast(g->builder, sbase,
            LLVMPointerType(etl, 0), "spp");
        LLVMValueRef ep = LLVMBuildGEP2(g->builder, etl, typed, &idx, 1, "ep");
        return LLVMBuildBitCast(g->builder, ep, i8p, "epb");
    }
    return NULL;
}

static LLVMValueRef emit_ref_lvalue_ptr(zan_irgen_t *g, zan_ast_node_t *tgt,
                                        local_scope_t *locals);
static bool irgen_boxed_eligible_struct(zan_irgen_t *g, zan_type_t *t);
static LLVMValueRef emit_box_struct(zan_irgen_t *g, zan_type_t *st,
                                    zan_ast_node_t *src_expr,
                                    LLVMValueRef val, local_scope_t *locals);

/* A556-1 隐式装箱：值形状（struct 聚合）流向 object/接口槽时自动装箱。
 * C# 同语义：隐式装箱等价一次隐藏的 (object) cast——箱是新堆对象
 * （fresh owned），消费方不再 retain；箱内字段对来源 retain/转移的判定
 * 由 emit_box_struct 按操作数 owned 性处理。目标不是引用槽或值已是
 * 指针时原样返回。装箱发生后把 RHS 节点登记进 boxed 集合
 * （irgen_note_boxed_expr），下游所有权判定按 owned 走。 */
static LLVMValueRef emit_implicit_box(zan_irgen_t *g, zan_type_t *lt,
                                      LLVMValueRef val, zan_ast_node_t *rhs,
                                      local_scope_t *locals) {
    if (!val || !lt || !rhs) return val;
    if (lt->kind != TYPE_OBJECT && lt->kind != TYPE_INTERFACE) return val;
    /* 同一 RHS 节点在一条语句内会经多条存储路径到达这里（赋值总派发
     * 已装箱，字段/槽位捕获再走一次）：箱按节点登记去重，第二个箱不但
     * 泄漏第一个，还会把箱首字 vtable 当字段 0 装进载荷。 */
    if (irgen_expr_was_boxed(g, rhs)) return val;
    zan_type_t *st = infer_expr_type(g, rhs, locals);
    bool boxable = st && st->kind == TYPE_STRUCT &&
        irgen_boxed_eligible_struct(g, st);
    /* 指针形值一般是引用形状（类/串/数组/集合），无需装箱；唯一例外是
     * 构造临时：new T{...} 的 emit_expr 结果是栈临时地址而不是已装箱
     * 引用，放行会让栈指针存进 object 槽（A556-4），必须交给
     * emit_box_struct 按值取出后再装箱。 */
    if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind && !boxable)
        return val;
    if (!boxable)
        return val;
    irgen_note_boxed_expr(g, rhs);
    return emit_box_struct(g, st, rhs, val, locals);
}


    static LLVMValueRef emit_expr_assignment(zan_irgen_t *g, zan_ast_node_t *expr,
            local_scope_t *locals) {
            LLVMValueRef right;
            /* 元组解构赋值降解处理 */
            if (expr->binary.left && expr->binary.left->kind == AST_TUPLE_EXPR) {
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3007,
                    "tuple assignment is not supported; use `var (a, b) = rhs` "
                    "to declare the names");
                return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
            }
            check_readonly_store(g, expr->binary.left, locals);
            /* 复合赋值左值子树单次求值防护 */
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
            /* Lambda 参数类型推导自赋值目标委托签名 */
            right = (lt && lt->kind == TYPE_DELEGATE &&
                     expr->binary.right->kind == AST_LAMBDA)
                        ? emit_lambda_typed(g, expr->binary.right, lt, locals)
                        : emit_expr(g, expr->binary.right, locals);
            if (lt && !type_is_binding(lt)) {
                zan_type_t *rty = infer_expr_type(g, expr->binary.right, locals);
                if (find_user_conversion(g, rty, lt, "op_implicit")) {
                    right = emit_user_conversion(g, rty, lt, "op_implicit",
                                                 right, expr->binary.right, locals);
                    if (is_rc_managed_type(lt)) {
                        /* 类型转换生成的新对象按 +1 持有所有权管理 */
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
            /* A556-1 隐式装箱：值形状流向 object/接口槽（局部/静态字段/
             * 裸名/索引目标统一在此收口）。箱是 fresh owned 堆对象——
             * emit_implicit_box 会把 RHS 节点登记进 boxed 集合，下游
             * 所有权判定按 owned 走，不再对箱 retain。 */
            right = emit_implicit_box(g, lt, right, expr->binary.right, locals);
        }
binding_lowered:
        if (expr->binary.left->kind == AST_IDENTIFIER) {
            local_var_t *local = local_find(locals, expr->binary.left->ident.name);
            if (local && local->obj_rc_flag && local_is_dyn_obj(g, local)) {
                /* 异步帧/闭包槽：所有权运行期可变（obj_rc_flag 存在才走
                 * 动态派发）。普通 object 局部量没有该档案（owns=0 时
                 * obj 路径从不建档，抢到这里只会退化成裸 store——既不
                 * retain 也不释放旧值，每次重赋值泄漏旧对象，A557）。 */
                emit_obj_local_store(g, local, right,
                    infer_expr_type(g, expr->binary.right, locals),
                    expr->binary.right, locals);
            } else if (local && local_slot_owns_rc(local)) {
                emit_rc_capture_local(g, local->type, local->alloca, right, expr->binary.right, locals);
            } else if (local && local->struct_rc) {
                /* 结构体按值整体拷贝与字段强引用生命周期转移 */
                emit_struct_local_capture(g, local->type, local->alloca, right,
                                          expr->binary.right, locals);
            } else if (local && local->frame_owner >= 0) {
                /* 异步协程帧槽别名：真实存储位于堆帧结构体内 */
                local_var_t *owner = &locals->vars[local->frame_owner];
                if (owner->type && is_rc_managed_type(owner->type) &&
                    LLVMGetTypeKind(local_slot_type(g, owner)) ==
                        LLVMPointerTypeKind) {
                    emit_rc_capture_local(g, owner->type, owner->alloca, right,
                                          expr->binary.right, locals);
                } else if (owner->struct_rc) {
                    emit_struct_local_capture(g, owner->type, owner->alloca, right,
                                              expr->binary.right, locals);
                } else {
                    LLVMValueRef sv = coerce_int_to(g, right,
                        local_slot_type(g, owner));
                    zan_store_fit(g, sv, owner->alloca);
                }
            } else if (local) {
                LLVMValueRef sv = coerce_int_to(g, right,
                    local_slot_type(g, local));
                zan_store_fit(g, sv, local->alloca);
            } else if (g->current_type_sym &&
                       get_static_field_global(g, g->current_type_sym,
                           get_field_sym(g->current_type_sym,
                               expr->binary.left->ident.name), NULL)) {
                /* 封闭类静态字段裸名访问降级为全局槽位寻址 */
                zan_symbol_t *fs = get_field_sym(g->current_type_sym,
                    expr->binary.left->ident.name);
                /* 裸名静态属性写入：派发至 static set_Prop(value) */
                zan_symbol_t *setter = property_setter_sym(g, fs);
                if (setter) {
                    emit_property_setter_call(g, setter, g->current_type_sym->type,
                        NULL, right, expr->binary.left,
                        expr->binary.right, locals);
                } else {
                LLVMValueRef gv = get_static_field_global(g, g->current_type_sym, fs, NULL);
                if (fs->type && (is_rc_managed_type(fs->type) || fs->type->kind == TYPE_OBJECT)) {
                    emit_rc_store_field(g, fs->type, gv, right, expr->binary.right, locals,
                                        (fs->modifiers & MOD_WEAK) ? 1 : 0);
                } else if (fs->type && fs->type->kind == TYPE_STRUCT &&
                           type_contains_collection_rc(g, fs->type, 0)) {
                    emit_struct_field_capture(g, fs->type, gv, right,
                                              expr->binary.right, locals);
                } else if (fs->type && fs->type->kind == TYPE_STRUCT) {
                    /* 值结构体字段按值整体赋值（实参可能是构造临时对象的地址） */
                    zan_store_fit(g, coerce_struct_value(g, right, fs->type), gv);
                } else {
                    LLVMTypeRef ft = fs->type ? map_type(g, fs->type)
                                              : LLVMInt64TypeInContext(g->ctx);
                    zan_store_fit(g, coerce_int_to(g, right, ft), gv);
                }
                }
            } else if (g->current_this && g->current_type_sym) {
                int fi = get_field_index(g->current_type_sym, expr->binary.left->ident.name);
                if (fi >= 0) {
                    LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                    /* 裸名自定义属性 setter 派发至 this.set_Prop(value) */
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
                        zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->binary.left->ident.name);
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
                        if (fst && (is_rc_managed_type(fst) || fst->kind == TYPE_OBJECT)) {
                            emit_rc_store_field(g, fst, fptr, right, expr->binary.right, locals,
                                                (fsym->modifiers & MOD_WEAK) ? 1 : 0);
                        } else if (fst && fst->kind == TYPE_STRUCT &&
                                   type_contains_collection_rc(g, fst, 0)) {
                            /* 结构体字段按值整体赋值 */
                            emit_struct_field_capture(g, fst, fptr, right,
                                                      expr->binary.right, locals);
                        } else if (fst && fst->kind == TYPE_STRUCT) {
                            /* 值结构体字段按值整体赋值（实参可能是构造临时对象的地址） */
                            zan_store_fit(g, coerce_struct_value(g, right, fst), fptr);
                        } else {
                            zan_store_fit(g, right, fptr);
                        }
                    }
                }
            }
        } else if (expr->binary.left->kind == AST_INDEX) {
            /* arr[i] = value */
            zan_ast_node_t *arr_expr = expr->binary.left->index.object;
            /* 索引赋值 obj[i] = v 降解为实例的索引器 setter 调用 */
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
                    LLVMTypeRef elem_llvm = span_elem_storage_type(g, et);
                    LLVMValueRef span_val = emit_expr(g, arr_expr, locals);
                    LLVMValueRef base = LLVMBuildExtractValue(g->builder, span_val, 0, "sps.base");
                    LLVMValueRef typed = LLVMBuildBitCast(g->builder, base,
                        LLVMPointerType(elem_llvm, 0), "sps.p");
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    LLVMValueRef span_len = LLVMBuildExtractValue(g->builder, span_val,
                        1, "sps.len");
                    LLVMValueRef sp_oob = emit_index_oob_pred(g, idx, span_len);
                    emit_index_bounds_check(g, idx, span_len, expr->loc, "span");
                    LLVMBasicBlockRef sp_do = NULL, sp_end = NULL;
                    emit_oob_store_split(g, sp_oob, &sp_do, &sp_end);
                    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64)
                        idx = LLVMBuildSExt(g->builder, idx,
                            LLVMInt64TypeInContext(g->ctx), "sps.ix");
                    idx = emit_index_safe_bounds(g, idx, span_len, expr->loc, "span");
                    LLVMValueRef ep = LLVMBuildGEP2(g->builder, elem_llvm, typed, &idx, 1, "sps.ep");
                    LLVMValueRef sv = right;
                    if (LLVMTypeOf(sv) != elem_llvm) {
                        if (LLVMGetTypeKind(elem_llvm) == LLVMStructTypeKind &&
                            LLVMGetTypeKind(LLVMTypeOf(sv)) == LLVMPointerTypeKind)
                            sv = LLVMBuildLoad2(g->builder, elem_llvm, sv, "sps.deref");
                        else
                            sv = coerce_int_to(g, sv, elem_llvm);
                    }
                    /* 非对齐内存跨度 (Span) 内存读写支持 */
                    LLVMSetAlignment(LLVMBuildStore(g->builder, sv, ep), 1);
                    emit_oob_store_finish(g, sp_do, sp_end);
                    return right;
                }
            }
            if (arr_expr->kind == AST_IDENTIFIER) {
                local_var_t *local = local_find(locals, arr_expr->ident.name);
                if (local && local->type && type_named(local->type, "Dict", 4)) {
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
                    return right;
                } else if (local && local->type && type_named(local->type, "List", 4)) {
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
                    LLVMValueRef ls_oob = emit_index_oob_pred(g, idx, count);
                    emit_index_bounds_check(g, idx, count, expr->loc, "list");
                    LLVMBasicBlockRef ls_do = NULL, ls_end = NULL;
                    emit_oob_store_split(g, ls_oob, &ls_do, &ls_end);
                    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                        idx = LLVMBuildSExt(g->builder, idx, LLVMInt64TypeInContext(g->ctx), "idxext");
                    }
                    idx = emit_index_safe_bounds(g, idx, count, expr->loc, "list");
                    idx = slot_word_index(g, idx,
                        elem_slot_words(g, container_elem_type(local->type)));
                    LLVMValueRef slot_ptr = LLVMBuildGEP2(g->builder, LLVMInt64TypeInContext(g->ctx), data, &idx, 1, "ep");
                    emit_collection_slot_store(g, container_elem_type(local->type),
                        LLVMInt64TypeInContext(g->ctx), slot_ptr,
                        right, expr->binary.right, locals, 1);
                    emit_oob_store_finish(g, ls_do, ls_end);
                } else if (local) {
                    LLVMValueRef arr_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                        local->alloca, "arrload");
                    if (local->type && local->type->kind == TYPE_STRING) {
                        LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                        LLVMValueRef st_oob = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
                        LLVMValueRef st_len = NULL;
                        if (!local->opaque_string) {
                            arr_ptr = emit_string_base_guard(g, arr_ptr, expr->loc);
                            st_len = emit_string_buffer_len(g, arr_ptr, expr->loc);
                            st_oob = emit_index_oob_pred(g, idx, st_len);
                            emit_index_bounds_check(g, idx, st_len, expr->loc, "string");
                        } else {
                            idx = emit_string_elem_guard(g, arr_ptr, idx, expr->loc, &arr_ptr);
                        }
                        LLVMBasicBlockRef st_do = NULL, st_end = NULL;
                        emit_oob_store_split(g, st_oob, &st_do, &st_end);
                        if (!local->opaque_string)
                            idx = emit_index_safe_bounds(g, idx, st_len, expr->loc, "string");
                        LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i8, arr_ptr, &idx, 1, "eidx");
                        LLVMValueRef val8 = LLVMBuildTrunc(g->builder, right, i8, "byte");
                        zan_store_fit(g, val8, elem_ptr);
                        emit_string_len_invalidate(g, arr_ptr);
                        emit_oob_store_finish(g, st_do, st_end);
                    } else {
                        LLVMTypeRef elem_llvm = LLVMInt32TypeInContext(g->ctx);
                        if (local->type && local->type->element_type) {
                            zan_type_t *et = local->type->element_type;
                            elem_llvm = map_type(g, et);
                        }
                        LLVMValueRef elem_ptr;
                        LLVMValueRef ar_oob = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
                        if (local->type && local->type->kind == TYPE_ARRAY &&
                            local->type->array_rank > 1) {
                            /* 多维矩形数组槽位：多维越界检查与行优先平铺寻址 */
                            elem_ptr = emit_mdarray_elem_ptr(g, arr_ptr,
                                expr->binary.left, elem_llvm, locals, &ar_oob);
                        } else {
                            LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                            if (local->type && local->type->kind == TYPE_ARRAY) {
                                ar_oob = emit_index_oob_pred(g, idx,
                                    zan_array_len(g, arr_ptr));
                                emit_index_bounds_check(g, idx, zan_array_len(g, arr_ptr),
                                    expr->loc, "array");
                                idx = emit_index_safe_bounds(g, idx,
                                    zan_array_len(g, arr_ptr), expr->loc, "array");
                            }
                            LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                                LLVMPointerType(elem_llvm, 0), "arrp");
                            elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
                        }
                        LLVMBasicBlockRef ar_do = NULL, ar_end = NULL;
                        emit_oob_store_split(g, ar_oob, &ar_do, &ar_end);
                        zan_type_t *et = local->type ? local->type->element_type : NULL;
                        LLVMValueRef stored = right;
                        /* decimal 元素 + 整数右值：先走运行时构造转成 decimal
                         * 再进槽（A548）。一维元素 GEP 进不了 zan_store_fit 的
                         * 槽型识别（那只认多维双下标 GEP），整数会裸写进结构体槽。 */
                        if (et && et->kind == TYPE_DECIMAL &&
                            LLVMGetTypeKind(LLVMTypeOf(stored)) == LLVMIntegerTypeKind)
                            stored = coerce_to_decimal(g, stored);
                        /* 可空元素槽写入：将标量值封装为可空结构体 */
                        if (et && et->kind == TYPE_NULLABLE)
                            stored = coerce_int_to(g, stored, elem_llvm);
                        /* A556-1 隐式装箱：object/接口元素数组收值形状右值 */
                        stored = emit_implicit_box(g, et, stored,
                                                   expr->binary.right, locals);
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
                                    stored = coerce_int_to(g, stored, elem_llvm);
                                }
                            }
                            zan_store_fit(g, stored, elem_ptr);
                        }
                        emit_oob_store_finish(g, ar_do, ar_end);
                    }
                } else if (g->current_type_sym) {
                    zan_symbol_t *fsym = get_field_sym(g->current_type_sym, arr_expr->ident.name);
                    if (fsym) {
                        LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
                        LLVMValueRef ar_oob = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
                        LLVMValueRef idx = (fsym->type && fsym->type->kind == TYPE_ARRAY && fsym->type->array_rank > 1)
                            ? NULL : emit_expr(g, expr->binary.left->index.index, locals);
                        /* 类内部裸字段访问的字符串引用计数管理 */
                        if (fsym->type && fsym->type->kind == TYPE_STRING &&
                            expr_has_reliable_string_bounds(arr_expr, locals)) {
                            arr_ptr = emit_string_base_guard(g, arr_ptr, expr->loc);
                            LLVMValueRef len = emit_string_buffer_len(g, arr_ptr, expr->loc);
                            emit_index_bounds_check(g, idx, len, expr->loc, "string");
                            idx = emit_index_safe_bounds(g, idx, len, expr->loc, "string");
                        } else if (fsym->type && fsym->type->kind == TYPE_STRING) {
                            idx = emit_string_elem_guard(g, arr_ptr, idx, expr->loc, &arr_ptr);
                        } else if (fsym->type && fsym->type->kind == TYPE_ARRAY &&
                                   fsym->type->array_rank <= 1) {
                            ar_oob = emit_index_oob_pred(g, idx, zan_array_len(g, arr_ptr));
                            emit_index_bounds_check(g, idx, zan_array_len(g, arr_ptr),
                                expr->loc, "array");
                            idx = emit_index_safe_bounds(g, idx,
                                zan_array_len(g, arr_ptr), expr->loc, "array");
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
                            LLVMValueRef lf_oob = emit_index_oob_pred(g, idx, count);
                            emit_index_bounds_check(g, idx, count, expr->loc, "list");
                            LLVMBasicBlockRef lf_do = NULL, lf_end = NULL;
                            emit_oob_store_split(g, lf_oob, &lf_do, &lf_end);
                            if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                                LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                                idx = LLVMBuildSExt(g->builder, idx, i64t, "idxext");
                            }
                            idx = emit_index_safe_bounds(g, idx, count, expr->loc, "list");
                            idx = slot_word_index(g, idx,
                                elem_slot_words(g, container_elem_type(fsym->type)));
                            LLVMValueRef slot_ptr = LLVMBuildGEP2(g->builder, i64t, data, &idx, 1, "ep");
                            emit_collection_slot_store(g, container_elem_type(fsym->type), i64t, slot_ptr,
                                right, expr->binary.right, locals, 1);
                            emit_oob_store_finish(g, lf_do, lf_end);
                        } else if (fsym->type && type_named(fsym->type, "Dict", 4)) {
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
                            return right;
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
                                expr->binary.left, elem_llvm, locals, &ar_oob);
                        } else {
                            LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                                LLVMPointerType(elem_llvm, 0), "arrp");
                            elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
                        }
                        LLVMBasicBlockRef ar_do = NULL, ar_end = NULL;
                        emit_oob_store_split(g, ar_oob, &ar_do, &ar_end);
                        zan_type_t *et = fsym->type ? fsym->type->element_type : NULL;
                        LLVMValueRef stored = right;
                        /* 可空数组元素槽写入：封装标量值为可空对象 */
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
                                    stored = coerce_int_to(g, stored, elem_llvm);
                                }
                            } else if (et && et->kind == TYPE_NULLABLE) {
                                /* 数组中可空元素槽位读取与写入 */
                                stored = coerce_int_to(g, stored, elem_llvm);
                            }
                            zan_store_fit(g, stored, elem_ptr);
                        }
                        emit_oob_store_finish(g, ar_do, ar_end);
                        }
                    }
                }
            } else if (arr_expr->kind == AST_MEMBER_ACCESS) {
                zan_type_t *at = member_access_field_type(g, locals, arr_expr);
                if (at) {
                    LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
                    LLVMValueRef idx = (at->kind == TYPE_ARRAY && at->array_rank > 1)
                        ? NULL : emit_expr(g, expr->binary.left->index.index, locals);
                    if (type_named(at, "List", 4)) {
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
                        LLVMValueRef lm_oob = emit_index_oob_pred(g, idx, count);
                        emit_index_bounds_check(g, idx, count, expr->loc, "list");
                        LLVMBasicBlockRef lm_do = NULL, lm_end = NULL;
                        emit_oob_store_split(g, lm_oob, &lm_do, &lm_end);
                        if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                            idx = LLVMBuildSExt(g->builder, idx, i64t, "idxext");
                        }
                        idx = emit_index_safe_bounds(g, idx, count, expr->loc, "list");
                        idx = slot_word_index(g, idx,
                            elem_slot_words(g, container_elem_type(at)));
                        LLVMValueRef slot_ptr = LLVMBuildGEP2(g->builder, i64t, data, &idx, 1, "ep");
                        emit_collection_slot_store(g, container_elem_type(at), i64t, slot_ptr,
                            right, expr->binary.right, locals, 1);
                        emit_oob_store_finish(g, lm_do, lm_end);
                    } else if (type_named(at, "Dict", 4)) {
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
                        /* 包含底层 FFI 缓冲区的字符串字段读写防护 */
                        if (expr_has_reliable_string_bounds(arr_expr, locals)) {
                            arr_ptr = emit_string_base_guard(g, arr_ptr, expr->loc);
                            idx = emit_index_safe_bounds(g, idx,
                                emit_string_buffer_len(g, arr_ptr, expr->loc), expr->loc,
                                "string");
                        }
                        else
                            idx = emit_string_elem_guard(g, arr_ptr, idx, expr->loc, &arr_ptr);
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i8, arr_ptr, &idx, 1, "eidx");
                        LLVMValueRef val8 = LLVMBuildTrunc(g->builder, right, i8, "byte");
                        zan_store_fit(g, val8, elem_ptr);
                        emit_string_len_invalidate(g, arr_ptr);
                        } else {
                        LLVMValueRef ar_oob = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
                        LLVMTypeRef elem_llvm = LLVMInt32TypeInContext(g->ctx);
                        if (at->element_type) {
                            elem_llvm = map_type(g, at->element_type);
                        }
                        LLVMValueRef elem_ptr;
                        if (at->kind == TYPE_ARRAY && at->array_rank > 1) {
                            elem_ptr = emit_mdarray_elem_ptr(g, arr_ptr,
                                expr->binary.left, elem_llvm, locals, &ar_oob);
                        } else {
                            if (at->kind == TYPE_ARRAY && at->array_rank == 1) {
                                ar_oob = emit_index_oob_pred(g, idx,
                                    zan_array_len(g, arr_ptr));
                                emit_index_bounds_check(g, idx,
                                    zan_array_len(g, arr_ptr), expr->loc, "array");
                                idx = emit_index_safe_bounds(g, idx,
                                    zan_array_len(g, arr_ptr), expr->loc, "array");
                            }
                            LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                                LLVMPointerType(elem_llvm, 0), "arrp");
                            elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
                        }
                        LLVMBasicBlockRef ar_do = NULL, ar_end = NULL;
                        emit_oob_store_split(g, ar_oob, &ar_do, &ar_end);
                        zan_type_t *et = at->element_type;
                        LLVMValueRef stored = right;
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
                                    stored = coerce_int_to(g, stored, elem_llvm);
                                }
                            }
                            zan_store_fit(g, stored, elem_ptr);
                        }
                        emit_oob_store_finish(g, ar_do, ar_end);
                    }
                }
            } else {
                zan_type_t *at = infer_expr_type(g, arr_expr, locals);
                if (at && type_named(at, "Dict", 4)) {
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    zan_type_t *kt = dict_key_type(g, at);
                    LLVMValueRef key = idx;
                    if (LLVMGetTypeKind(LLVMTypeOf(key)) == LLVMIntegerTypeKind) {
                        LLVMTypeRef kem = kt ? map_type(g, kt) : NULL;
                        if (kem && LLVMGetTypeKind(kem) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(kem) < LLVMGetIntTypeWidth(LLVMTypeOf(key)))
                            key = LLVMBuildTrunc(g->builder, key, kem, "k.nw");
                        if (LLVMGetIntTypeWidth(LLVMTypeOf(key)) < 64)
                            key = LLVMBuildSExt(g->builder, key, LLVMInt64TypeInContext(g->ctx), "k.sx");
                        key = LLVMBuildIntToPtr(g->builder, key, i8ptr, "k.ip");
                    } else if (LLVMTypeOf(key) != i8ptr) {
                        key = LLVMBuildBitCast(g->builder, key, i8ptr, "k.bc");
                    }
                    emit_dict_value_set(g, at, arr_ptr, key, right,
                        expr->binary.left->index.index, expr->binary.right, locals);
                    emit_release_owned_call_temp(g, arr_expr, arr_ptr, locals);
                    return right;
                } else if (at && type_named(at, "List", 4)) {
                    /* 多层级联索引写入：基表达式读取与深层寻址 */
                    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, arr_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64t, 0),
                        LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                            list_ptr, 2, "df"), "data");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64t,
                        LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                            list_ptr, 0, "countf"), "count");
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    emit_index_bounds_check(g, idx, count, expr->loc, "list");
                    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                        idx = LLVMBuildSExt(g->builder, idx, i64t, "idxext");
                    }
                    idx = emit_index_safe_bounds(g, idx, count, expr->loc, "list");
                    idx = slot_word_index(g, idx,
                        elem_slot_words(g, container_elem_type(at)));
                    LLVMValueRef slot_ptr = LLVMBuildGEP2(g->builder, i64t, data, &idx, 1, "ep");
                    emit_collection_slot_store(g, container_elem_type(at), i64t, slot_ptr,
                        right, expr->binary.right, locals, 1);
                    emit_release_owned_call_temp(g, arr_expr, arr_ptr, locals);
                } else if (at && at->kind == TYPE_ARRAY) {
                    LLVMTypeRef elem_llvm = at->element_type
                        ? map_type(g, at->element_type)
                        : LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
                    LLVMValueRef elem_ptr;
                    LLVMValueRef ar_oob = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
                    if (at->array_rank > 1) {
                        elem_ptr = emit_mdarray_elem_ptr(g, arr_ptr,
                            expr->binary.left, elem_llvm, locals, &ar_oob);
                    } else {
                        LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                        ar_oob = emit_index_oob_pred(g, idx, zan_array_len(g, arr_ptr));
                        emit_index_bounds_check(g, idx, zan_array_len(g, arr_ptr),
                            expr->loc, "array");
                        idx = emit_index_safe_bounds(g, idx,
                            zan_array_len(g, arr_ptr), expr->loc, "array");
                        LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                            LLVMPointerType(elem_llvm, 0), "arrp");
                        elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm,
                            typed_arr, &idx, 1, "eidx");
                    }
                    LLVMBasicBlockRef ar_do = NULL, ar_end = NULL;
                    emit_oob_store_split(g, ar_oob, &ar_do, &ar_end);
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
                            stored = coerce_int_to(g, stored, elem_llvm);
                        zan_store_fit(g, stored, elem_ptr);
                    }
                    emit_oob_store_finish(g, ar_do, ar_end);
                    emit_release_owned_call_temp(g, arr_expr, arr_ptr, locals);
                } else {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->binary.left->loc, 3008,
                        "cannot assign through this indexed expression");
                }
            }
        } else if (expr->binary.left->kind == AST_MEMBER_ACCESS) {
            /* obj.Field = value */
            zan_ast_node_t *obj_expr = expr->binary.left->member.object;
            bool stored = false;
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
            if (obj_expr->kind == AST_IDENTIFIER &&
                !local_find(locals, obj_expr->ident.name)) {
                zan_symbol_t *cs = zan_binder_lookup(g->binder, obj_expr->ident.name);
                if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                    zan_symbol_t *fs = get_field_sym(cs, expr->binary.left->member.name);
                    zan_symbol_t *setter = property_setter_sym(g, fs);
                    if (setter) {
                        emit_property_setter_call(g, setter, cs->type, NULL,
                            right, obj_expr, expr->binary.right, locals);
                        stored = true;
                    } else {
                    LLVMValueRef gv = get_static_field_global(g, cs, fs,
                        static_access_inst(g, obj_expr));
                    if (gv) {
                        if (fs->type && (is_rc_managed_type(fs->type) || fs->type->kind == TYPE_OBJECT)) {
                            emit_rc_store_field(g, fs->type, gv, right,
                                                expr->binary.right, locals,
                                                (fs->modifiers & MOD_WEAK) ? 1 : 0);
                        } else if (fs->type && fs->type->kind == TYPE_STRUCT &&
                                   type_contains_collection_rc(g, fs->type, 0)) {
                            emit_struct_field_capture(g, fs->type, gv, right,
                                                      expr->binary.right, locals);
                        } else if (fs->type && fs->type->kind == TYPE_STRUCT) {
                            /* 值结构体字段按值整体赋值（实参可能是构造临时对象的地址） */
                            zan_store_fit(g, coerce_struct_value(g, right, fs->type), gv);
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
                            if (aft && (is_rc_managed_type(aft) || aft->kind == TYPE_OBJECT)) {
                                emit_rc_store_field(g, aft, fptr, right, expr->binary.right, locals,
                                                    (afsym->modifiers & MOD_WEAK) ? 1 : 0);
                            } else if (aft && aft->kind == TYPE_STRUCT &&
                                       type_contains_collection_rc(g, aft, 0)) {
                                emit_struct_field_capture(g, aft, fptr, right,
                                                          expr->binary.right, locals);
                            } else if (aft && aft->kind == TYPE_STRUCT) {
                                /* 值结构体字段按值整体赋值（实参可能是构造临时对象的地址） */
                                zan_store_fit(g, coerce_struct_value(g, right, aft), fptr);
                            } else {
                                zan_store_fit(g, right, fptr);
                            }
                            stored = true;
                        }
                    }
                }
            }
            if (!stored) {
                zan_symbol_t *cls = expr_class_sym(g, obj_expr, locals);
                if (cls) {
                    int fi = get_field_index(cls, expr->binary.left->member.name);
                    if (fi >= 0) {
                        zan_symbol_t *psym = get_field_sym(cls, expr->binary.left->member.name);
                        zan_symbol_t *setter = property_setter_sym(g, psym);
                        if (obj_expr->kind == AST_INDEX && cls->kind == SYM_STRUCT) {
                            /* 集合/数组内结构体元素字段就地赋值 */
                            zan_type_t *et = infer_expr_type(g, obj_expr, locals);
                            LLVMTypeRef st = get_struct_llvm_type(g, cls);
                            LLVMValueRef ep = (st && !type_is_binding(et))
                                ? emit_struct_elem_ptr(g, obj_expr, locals) : NULL;
                            if (ep) {
                                LLVMValueRef sptr = LLVMBuildBitCast(g->builder, ep,
                                    LLVMPointerType(st, 0), "ep.s");
                                if (setter) {
                                    emit_property_setter_call(g, setter, et, sptr,
                                        right, obj_expr, expr->binary.right, locals);
                                } else {
                                    LLVMValueRef fptr = emit_field_ptr(g, cls, st,
                                        sptr, fi, "gfld");
                                    zan_symbol_t *gfsym = get_field_sym(cls,
                                        expr->binary.left->member.name);
                                    zan_type_t *gft = gfsym ? field_store_type(g,
                                        gfsym, et) : NULL;
                                    if (gft && (is_rc_managed_type(gft) || gft->kind == TYPE_OBJECT)) {
                                        emit_rc_store_field(g, gft, fptr, right,
                                            expr->binary.right, locals,
                                            (gfsym->modifiers & MOD_WEAK) ? 1 : 0);
                                    } else if (gft && gft->kind == TYPE_STRUCT &&
                                               type_contains_collection_rc(g, gft, 0)) {
                                        emit_struct_field_capture(g, gft, fptr, right,
                                                                  expr->binary.right, locals);
                                    } else if (gft && gft->kind == TYPE_STRUCT) {
                                        /* 值结构体字段按值整体赋值（实参可能是构造临时对象的地址） */
                                        zan_store_fit(g, coerce_struct_value(g, right, gft), fptr);
                                    } else {
                                        zan_store_fit(g, right, fptr);
                                    }
                                }
                                stored = true;
                            }
                        } else if (setter) {
                            zan_type_t *rct = infer_expr_type(g, obj_expr, locals);
                            LLVMValueRef rval = emit_guarded_member_object(
                                g, expr->binary.left, locals);
                            emit_property_setter_call(g, setter, rct, rval,
                                right, obj_expr, expr->binary.right, locals);
                            emit_release_owned_call_temp(g, obj_expr, rval, locals);
                            stored = true;
                        } else {
                            LLVMTypeRef st = get_struct_llvm_type(g, cls);
                            LLVMValueRef obj_val = cls->kind == SYM_STRUCT
                                ? emit_ref_lvalue_ptr(g, obj_expr, locals)
                                : emit_guarded_member_object(g, expr->binary.left, locals);
                            if (obj_val && st &&
                                LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                                LLVMValueRef fptr = emit_field_ptr(g, cls, st, obj_val, fi, "gfld");
                                zan_symbol_t *gfsym = get_field_sym(cls, expr->binary.left->member.name);
                                zan_type_t *gft = gfsym
                                    ? field_store_type(g, gfsym,
                                          infer_expr_type(g, obj_expr, locals))
                                    : NULL;
                                if (gft && (is_rc_managed_type(gft) || gft->kind == TYPE_OBJECT)) {
                                    emit_rc_store_field(g, gft, fptr, right, expr->binary.right, locals,
                                                        (gfsym->modifiers & MOD_WEAK) ? 1 : 0);
                                } else if (gft && gft->kind == TYPE_STRUCT &&
                                           type_contains_collection_rc(g, gft, 0)) {
                                    emit_struct_field_capture(g, gft, fptr, right,
                                                              expr->binary.right, locals);
                                } else if (gft && gft->kind == TYPE_STRUCT) {
                                    /* 值结构体字段按值整体赋值（实参可能是构造临时对象的地址） */
                                    zan_store_fit(g, coerce_struct_value(g, right, gft), fptr);
                                } else {
                                    zan_store_fit(g, right, fptr);
                                }
                                if (cls->kind != SYM_STRUCT)
                                    emit_release_owned_call_temp(g, obj_expr, obj_val, locals);
                            }
                        }
                    }
                }
            }
            /* 健壮性：未定义字段赋值时安全报错与回退 */
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
                        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3009,
                            "'%.*s' has no member '%.*s'",
                            (int)acls->name.len, acls->name.str,
                            (int)an.len, an.str);
                    }
                }
                /* 命名空间/静态类型字段赋值错误防护 */
                if (!stored && !acls && obj_expr->kind == AST_IDENTIFIER &&
                    !local_find(locals, obj_expr->ident.name) &&
                    !zan_binder_lookup(g->binder, obj_expr->ident.name) &&
                    !(g->current_type_sym &&
                      (get_field_sym(g->current_type_sym, obj_expr->ident.name) ||
                       get_method_sym(g->current_type_sym, obj_expr->ident.name)))) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3003,
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

/* 前缀/后缀自增自减 (++ / --) 共享降解发射：操作数原地更新 */
static LLVMValueRef emit_incdec_expr(zan_irgen_t *g, zan_ast_node_t *expr,
                                     local_scope_t *locals, int is_prefix) {
    zan_ast_node_t *operand = expr->unary.operand;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef slot_ptr = NULL;
    LLVMValueRef slot_str_base = NULL;
    LLVMValueRef slot_oob = NULL;
    LLVMTypeRef slot_ty = NULL;
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
            /* 外层类同名符号访问：根据静态/实例上下文分发 */
            zan_symbol_t *fs = get_field_sym(g->current_type_sym, operand->ident.name);
            LLVMValueRef gv = get_static_field_global(g, g->current_type_sym, fs, NULL);
            zan_symbol_t *getter = gv ? property_getter_sym(g, fs) : NULL;
            if (getter) {
                prop_getter = getter;
                prop_setter = property_setter_sym(g, fs);
                prop_recv_type = g->current_type_sym->type;
                prop_recv = NULL;
            } else {
            if (gv) {
                slot_ptr = gv;
                slot_ty = fs->type ? map_type(g, fs->type) : i64;
            } else {
                int fi = get_field_index(g->current_type_sym, operand->ident.name);
                LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                if (fi >= 0 && st) {
                    LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(st, 0), g->current_this, "this");
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
        if (!slot_ptr && !prop_getter && obj_expr->kind == AST_IDENTIFIER) {
            local_var_t *local = local_find(locals, obj_expr->ident.name);
            if (local && local->type && local->type->sym) {
                int fi = get_field_index(local->type->sym, operand->member.name);
                LLVMTypeRef st = get_struct_llvm_type(g, local->type->sym);
                if (fi >= 0 && st) {
                    zan_symbol_t *fsym = get_field_sym(local->type->sym, operand->member.name);
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
        if (!slot_ptr && !prop_getter) {
            zan_symbol_t *cls = expr_class_sym(g, obj_expr, locals);
            if (cls) {
                int fi = get_field_index(cls, operand->member.name);
                LLVMTypeRef st = get_struct_llvm_type(g, cls);
                if (fi >= 0 && st) {
                    zan_symbol_t *fsym = get_field_sym(cls, operand->member.name);
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
                slot_ptr = emit_mdarray_elem_ptr(g, arr_ptr, operand,
                                                  elem_llvm, locals, &slot_oob);
            } else {
                LLVMValueRef idx = emit_expr(g, operand->index.index, locals);
                slot_oob = emit_index_oob_pred(g, idx, zan_array_len(g, arr_ptr));
                emit_index_bounds_check(g, idx, zan_array_len(g, arr_ptr),
                    operand->loc, "array");
                idx = emit_index_safe_bounds(g, idx, zan_array_len(g, arr_ptr),
                    operand->loc, "array");
                LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                    LLVMPointerType(elem_llvm, 0), "arrp");
                slot_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
            }
            slot_ty = elem_llvm;
        } else if (at && at->kind == TYPE_STRING) {
            LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
            LLVMValueRef idx = emit_expr(g, operand->index.index, locals);
            if (expr_has_reliable_string_bounds(arr_expr, locals)) {
                arr_ptr = emit_string_base_guard(g, arr_ptr, operand->loc);
                LLVMValueRef str_len = emit_string_buffer_len(g, arr_ptr, operand->loc);
                slot_oob = emit_index_oob_pred(g, idx, str_len);
                emit_index_bounds_check(g, idx, str_len, operand->loc, "string");
                idx = emit_index_safe_bounds(g, idx, str_len, operand->loc, "string");
            } else {
                idx = emit_string_elem_guard(g, arr_ptr, idx, operand->loc, &arr_ptr);
            }
            slot_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                arr_ptr, &idx, 1, "eidx");
            slot_ty = LLVMInt8TypeInContext(g->ctx);
            slot_str_base = arr_ptr;
        }
    }

    if (prop_getter) {
        /* 属性自增/自减：通过 getter 读取，计算新值后通过 setter 写回 */
        zan_type_t *pt = prop_getter->type;
        if (pt) pt = subst_type_param_deep(g, pt, prop_recv_type);
        LLVMTypeRef pty = pt ? map_type(g, pt) : i64;
        LLVMTypeKind pk = LLVMGetTypeKind(pty);
        if (pk != LLVMIntegerTypeKind && pk != LLVMFloatTypeKind &&
            pk != LLVMDoubleTypeKind) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, operand->loc, 3010,
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
        zan_diag_emit_code(g->diag, DIAG_ERROR, operand->loc, 3011,
            "cannot apply ++/-- to this expression");
        return LLVMConstInt(i64, 0, 0);
    }
    LLVMTypeKind k = LLVMGetTypeKind(slot_ty);
    if (k != LLVMIntegerTypeKind && k != LLVMFloatTypeKind &&
        k != LLVMDoubleTypeKind) {
        /* A525 一件：非数值槽位先试用户 operator ++/--（parser 命名
         * op_inc/op_dec；emit_unary_op_overload 自带 struct 槽位解引用）。
         * C# 口径：前缀后缀都调同一算符，后缀返回旧值。无重载才维持
         * 原拒绝。属性路径（prop_getter）暂不走用户算符。 */
        if (expr->unary.op == TK_PLUS_PLUS || expr->unary.op == TK_MINUS_MINUS) {
            LLVMValueRef uov = emit_unary_op_overload(g, expr, locals);
            if (uov) {
                LLVMValueRef old_v = LLVMBuildLoad2(g->builder, slot_ty,
                                                    slot_ptr, "inc.old");
                LLVMBasicBlockRef inc_do = NULL, inc_end = NULL;
                if (slot_oob) emit_oob_store_split(g, slot_oob, &inc_do, &inc_end);
                zan_store_fit(g, uov, slot_ptr);
                if (slot_oob) emit_oob_store_finish(g, inc_do, inc_end);
                if (slot_str_base)
                    emit_string_len_invalidate(g, slot_str_base);
                return is_prefix ? uov : old_v;
            }
        }
        zan_diag_emit_code(g->diag, DIAG_ERROR, operand->loc, 3010,
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
    LLVMBasicBlockRef inc_do = NULL, inc_end = NULL;
    if (slot_oob) emit_oob_store_split(g, slot_oob, &inc_do, &inc_end);
    zan_store_fit(g, new_val, slot_ptr);
    if (slot_oob) emit_oob_store_finish(g, inc_do, inc_end);
    if (slot_str_base) emit_string_len_invalidate(g, slot_str_base);
    return is_prefix ? new_val : old_val;
}
