/* 前缀/后缀自增自减 (++ / --) 共享降解发射：操作数原地更新 */
LLVMValueRef emit_incdec_expr(zan_irgen_t *g, zan_ast_node_t *expr,
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
            if (g->irgen_checked_depth > 0 && LLVMGetIntTypeWidth(pty) > 1) {
                bool uns = expr_is_uint(g, operand, locals) ||
                           expr_is_ulong(g, operand, locals);
                LLVMValueRef ovf = (expr->unary.op == TK_PLUS_PLUS)
                    ? zan_icmp(g->builder, uns ? LLVMIntULT : LLVMIntSLT,
                               new_val, old_val, "ck.inc.ovf")
                    : zan_icmp(g->builder, uns ? LLVMIntUGT : LLVMIntSGT,
                               new_val, old_val, "ck.dec.ovf");
                emit_runtime_check(g, ovf, expr->loc,
                    "integer overflow in checked operation");
            }
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
        if (g->irgen_checked_depth > 0 && LLVMGetIntTypeWidth(slot_ty) > 1) {
            /* checked 下 ++/-- 回绕陷阱（B-ID76）：加一变小 / 减一变大即溢出 */
            bool uns = expr_is_uint(g, operand, locals) ||
                       expr_is_ulong(g, operand, locals);
            LLVMValueRef ovf = (expr->unary.op == TK_PLUS_PLUS)
                ? zan_icmp(g->builder, uns ? LLVMIntULT : LLVMIntSLT,
                           new_val, old_val, "ck.inc.ovf")
                : zan_icmp(g->builder, uns ? LLVMIntUGT : LLVMIntSGT,
                           new_val, old_val, "ck.dec.ovf");
            emit_runtime_check(g, ovf, expr->loc,
                "integer overflow in checked operation");
        }
    }
    LLVMBasicBlockRef inc_do = NULL, inc_end = NULL;
    if (slot_oob) emit_oob_store_split(g, slot_oob, &inc_do, &inc_end);
    zan_store_fit(g, new_val, slot_ptr);
    if (slot_oob) emit_oob_store_finish(g, inc_do, inc_end);
    if (slot_str_base) emit_string_len_invalidate(g, slot_str_base);
    return is_prefix ? new_val : old_val;
}
