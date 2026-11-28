LLVMValueRef emit_expr_assignment(zan_irgen_t *g, zan_ast_node_t *expr,
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
            /* S12：`发布者.事件 += lambda` 且发布者以 this 为根——lambda 弱捕获
             * this，订阅随订阅者释放自动失效。须在左值提升（ca_hoist_spine 把
             * 左值换成临时量）之前判定。 */
            if (expr->binary.compound_base == TK_PLUS && g->current_this &&
                expr->binary.right && expr->binary.right->kind == AST_BINARY &&
                expr->binary.right->binary.right &&
                expr->binary.right->binary.right->kind == AST_LAMBDA &&
                zan_subscription_rooted_at_this(g, expr->binary.left, locals))
                g->weak_this_lambda = expr->binary.right->binary.right;
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
        emit_move_clear_drop_flag(g, expr->binary.right, locals);
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
            } else if (local && local->drop_flag) {
                /* ~Copyable RAII reassignment: drop previous value if alive,
                 * store incoming value, and re-arm drop flag. */
                emit_raii_drop_local(g, local);
                LLVMTypeRef slot_ty = local_slot_type(g, local);
                LLVMValueRef rv = (LLVMGetTypeKind(slot_ty) == LLVMStructTypeKind)
                    ? coerce_struct_value(g, right, local->type) : right;
                LLVMValueRef sv = coerce_int_to(g, rv, slot_ty);
                zan_store_fit(g, sv, local->alloca);
                LLVMBuildStore(g->builder,
                               LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0),
                               local->drop_flag);
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
                    LLVMTypeRef slot_ty = local_slot_type(g, owner);
                    LLVMValueRef rv = (LLVMGetTypeKind(slot_ty) == LLVMStructTypeKind)
                        ? coerce_struct_value(g, right, owner->type) : right;
                    LLVMValueRef sv = coerce_int_to(g, rv, slot_ty);
                    zan_store_fit(g, sv, owner->alloca);
                }
            } else if (local) {
                /* 值结构体赋给 ref/out 参数时 local->alloca 是 LLVMGetParam，
                 * zan_store_fit 无法反推 target，必须先按值解引用构造临时地址。 */
                LLVMTypeRef slot_ty = local_slot_type(g, local);
                LLVMValueRef rv = (LLVMGetTypeKind(slot_ty) == LLVMStructTypeKind)
                    ? coerce_struct_value(g, right, local->type) : right;
                LLVMValueRef sv = coerce_int_to(g, rv, slot_ty);
                zan_store_fit(g, sv, local->alloca);
            } else if (g->current_type_sym &&
                       get_field_sym(g->current_type_sym,
                           expr->binary.left->ident.name) &&
                       field_member_is_static(get_field_sym(
                           g->current_type_sym,
                           expr->binary.left->ident.name))) {
                /* 封闭类静态字段裸名访问降级为全局槽位寻址 */
                zan_symbol_t *fs = get_field_sym(g->current_type_sym,
                    expr->binary.left->ident.name);
                zan_type_t *sinst = (g->cur_inst &&
                    g->cur_inst->sym == g->current_type_sym)
                    ? g->cur_inst : g->current_type_sym->type;
                /* 裸名静态属性写入：派发至 static set_Prop(value) */
                zan_symbol_t *setter = property_setter_sym(g, fs);
                if (setter) {
                    emit_property_setter_call(g, setter, sinst,
                        NULL, right, expr->binary.left,
                        expr->binary.right, locals);
                } else {
                LLVMValueRef gv = get_static_field_global(g, g->current_type_sym, fs, NULL);
                zan_type_t *fst = field_store_type(g, fs, g->cur_inst);
                zan_type_t *rft = fst ? fst : fs->type;
                if (rft && (is_rc_managed_type(rft) || rft->kind == TYPE_OBJECT)) {
                    emit_rc_store_member(g, fs, rft, gv, right, expr->binary.right, locals);
                } else if (rft && rft->kind == TYPE_STRUCT &&
                           type_contains_collection_rc(g, rft, 0)) {
                    emit_struct_field_capture(g, rft, gv, right,
                                              expr->binary.right, locals);
                } else if (rft && rft->kind == TYPE_STRUCT) {
                    /* 值结构体字段按值整体赋值（实参可能是构造临时对象的地址） */
                    zan_store_fit(g, coerce_struct_value(g, right, rft), gv);
                } else {
                    LLVMTypeRef ft = rft ? map_type(g, rft)
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
                        /* Cleanup must describe borrowed this, not the owned
                         * result of reading the property being assigned. */
                        zan_ast_node_t receiver = {0};
                        receiver.kind = AST_THIS_EXPR;
                        receiver.loc = expr->binary.left->loc;
                        zan_type_t *rct = g->cur_inst ? g->cur_inst
                                                      : g->current_type_sym->type;
                        emit_property_setter_call(g, setter, rct,
                            this_ptr, right, &receiver,
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
                            emit_rc_store_member(g, fsym, fst, fptr, right, expr->binary.right, locals);
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
                                LLVMValueRef gctx_arg =
                                    route_generic_method_gctx(g, oist, op_sym);
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "ops";
                                LLVMValueRef result = emit_dispatch_call_gctx(g,
                                    oist->sym, op_sym, mfn, mft, call_args, 3,
                                    gctx_arg, cn);
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
                    int bce_scope = 0;
                    bool bce_ok = irgen_bce_check(g, arr_expr,
                        expr->binary.left->index.index, locals, &bce_scope);
                    LLVMBasicBlockRef sp_do = NULL, sp_end = NULL;
                    if (!bce_ok) {
                        LLVMValueRef sp_oob =
                            emit_index_bounds_check(g, idx, span_len, expr->loc, "span");
                        emit_oob_store_split(g, sp_oob, &sp_do, &sp_end);
                    }
                    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64)
                        idx = LLVMBuildSExt(g->builder, idx,
                            LLVMInt64TypeInContext(g->ctx), "sps.ix");
                    LLVMValueRef ep = LLVMBuildInBoundsGEP2(g->builder, elem_llvm, typed, &idx, 1, "sps.ep");
                    LLVMValueRef sv = right;
                    if (LLVMTypeOf(sv) != elem_llvm) {
                        if (LLVMGetTypeKind(elem_llvm) == LLVMStructTypeKind &&
                            LLVMGetTypeKind(LLVMTypeOf(sv)) == LLVMPointerTypeKind)
                            sv = LLVMBuildLoad2(g->builder, elem_llvm, sv, "sps.deref");
                        else
                            sv = coerce_int_to(g, sv, elem_llvm);
                    }
                    /* 自然对齐 + 可选 ScopedNoAlias 元数据，释放 LLVM 自动向量化 */
                    LLVMValueRef st = LLVMBuildStore(g->builder, sv, ep);
                    LLVMSetAlignment(st, zan_span_elem_align(g, elem_llvm));
                    int na_scope = bce_scope > 0
                        ? bce_scope
                        : zan_local_noalias_scope(arr_expr, locals);
                    if (na_scope > 0)
                        zan_attach_noalias_md(g, st, na_scope);
                    if (!bce_ok)
                        emit_oob_store_finish(g, sp_do, sp_end);
                    return right;
                }
                if (is_inline_array_type(sot)) {
                    int64_t arr_n = inline_array_len(sot);
                    zan_type_t *et = inline_array_elem(sot);
                    LLVMTypeRef elem_llvm = span_elem_storage_type(g, et);
                    LLVMValueRef ptr = emit_ref_lvalue_ptr(g, arr_expr, locals);
                    if (ptr) {
                        LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                        bool safe = false;
                        if (LLVMIsAConstantInt(idx)) {
                            long long ci = LLVMConstIntGetSExtValue(idx);
                            if (ci >= 0 && ci < arr_n)
                                safe = true;
                        }
                        if (!safe) {
                            int bce_scope = 0;
                            safe = irgen_bce_check(g, arr_expr,
                                expr->binary.left->index.index, locals, &bce_scope);
                        }
                        LLVMBasicBlockRef ia_do = NULL, ia_end = NULL;
                        if (!safe) {
                            LLVMValueRef ia_oob = emit_index_bounds_check(
                                g, idx,
                                LLVMConstInt(i64t, (unsigned long long)arr_n, 0),
                                expr->loc, "InlineArray");
                            emit_oob_store_split(g, ia_oob, &ia_do, &ia_end);
                        }
                        if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64)
                            idx = LLVMBuildSExt(g->builder, idx, i64t, "ias.ix");
                        LLVMValueRef typed = LLVMBuildBitCast(
                            g->builder, ptr, LLVMPointerType(elem_llvm, 0), "ias.p");
                        LLVMValueRef ep = LLVMBuildInBoundsGEP2(
                            g->builder, elem_llvm, typed, &idx, 1, "ias.ep");
                        LLVMValueRef sv = right;
                        if (LLVMTypeOf(sv) != elem_llvm) {
                            if (LLVMGetTypeKind(elem_llvm) == LLVMStructTypeKind &&
                                LLVMGetTypeKind(LLVMTypeOf(sv)) == LLVMPointerTypeKind)
                                sv = LLVMBuildLoad2(g->builder, elem_llvm, sv, "ias.deref");
                            else
                                sv = coerce_int_to(g, sv, elem_llvm);
                        }
                        LLVMValueRef st = LLVMBuildStore(g->builder, sv, ep);
                        LLVMSetAlignment(st, zan_span_elem_align(g, elem_llvm));
                        if (!safe)
                            emit_oob_store_finish(g, ia_do, ia_end);
                        return right;
                    }
                }
                if (is_vec_type(sot)) {
                    int64_t vn = vec_len(sot);
                    zan_type_t *et = vec_elem(sot);
                    LLVMTypeRef elem_llvm = span_elem_storage_type(g, et);
                    LLVMTypeRef vec_llvm = LLVMVectorType(elem_llvm, (unsigned)(vn > 0 ? vn : 1));
                    LLVMValueRef ptr = emit_ref_lvalue_ptr(g, arr_expr, locals);
                    if (ptr) {
                        LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                        if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(LLVMTypeOf(idx)) != 32)
                            idx = LLVMBuildIntCast2(g->builder, idx, i32t, 1, "vecs.ix");
                        LLVMValueRef cur = LLVMBuildLoad2(g->builder, vec_llvm, ptr, "vecs.cur");
                        LLVMValueRef sv = right;
                        if (LLVMTypeOf(sv) != elem_llvm) {
                            if (LLVMGetTypeKind(elem_llvm) == LLVMFloatTypeKind ||
                                LLVMGetTypeKind(elem_llvm) == LLVMDoubleTypeKind) {
                                if (LLVMGetTypeKind(LLVMTypeOf(sv)) == LLVMIntegerTypeKind)
                                    sv = LLVMBuildSIToFP(g->builder, sv, elem_llvm, "vecs.fp");
                                else
                                    sv = LLVMBuildFPCast(g->builder, sv, elem_llvm, "vecs.fpc");
                            } else {
                                sv = coerce_int_to(g, sv, elem_llvm);
                            }
                        }
                        LLVMValueRef upd = LLVMBuildInsertElement(g->builder, cur, sv, idx, "vecs.ins");
                        LLVMBuildStore(g->builder, upd, ptr);
                        return right;
                    }
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
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    LLVMValueRef count_field = LLVMBuildStructGEP2(g->builder,
                        g->list_struct_type, list_ptr, 0, "countf");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder,
                        LLVMInt64TypeInContext(g->ctx), count_field, "count");
                    zan_tbaa_tag_cnt(g, count);
                    LLVMValueRef ls_oob = emit_index_bounds_check(g, idx, count, expr->loc, "list");
                    LLVMBasicBlockRef ls_do = NULL, ls_end = NULL;
                    emit_oob_store_split(g, ls_oob, &ls_do, &ls_end);
                    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                        idx = LLVMBuildSExt(g->builder, idx, LLVMInt64TypeInContext(g->ctx), "idxext");
                    }
                    LLVMValueRef data =
                        irgen_is_loop_guarded_list(g, local)
                            ? list_data_direct(g, list_ptr, "data")
                            : list_data_for(g, list_ptr, container_elem_type(local->type), "data");
                    LLVMValueRef slot_ptr = list_elem_ptr(g, data, idx, container_elem_type(local->type), "ep");
                    emit_collection_slot_store(g, container_elem_type(local->type), list_slot_type(g, container_elem_type(local->type)), slot_ptr,
                        right, expr->binary.right, locals, 1);
                    emit_oob_store_finish(g, ls_do, ls_end);
                } else if (local) {
                    LLVMValueRef arr_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                        local->alloca, "arrload");
                    if (local->type && local->type->kind == TYPE_STRING) {
                        LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                        LLVMValueRef st_oob = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
                        if (!local->opaque_string) {
                            arr_ptr = emit_string_base_guard(g, arr_ptr, expr->loc);
                            LLVMValueRef st_len = emit_string_buffer_len(g, arr_ptr, expr->loc);
                            st_oob = emit_index_bounds_check(g, idx, st_len, expr->loc, "string");
                        } else {
                            idx = emit_string_elem_guard(g, arr_ptr, idx, expr->loc, &arr_ptr);
                        }
                        LLVMBasicBlockRef st_do = NULL, st_end = NULL;
                        emit_oob_store_split(g, st_oob, &st_do, &st_end);
                        LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                        LLVMValueRef elem_ptr = !local->opaque_string
                            ? LLVMBuildInBoundsGEP2(g->builder, i8, arr_ptr, &idx, 1, "eidx")
                            : LLVMBuildGEP2(g->builder, i8, arr_ptr, &idx, 1, "eidx");
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
                        LLVMValueRef elem_ptr = NULL;
                        LLVMValueRef idx_1d = NULL;
                        LLVMValueRef ar_oob = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
                        int is_arr_1d = 0;
                        if (local->type && local->type->kind == TYPE_ARRAY)
                            arr_ptr = emit_array_null_guard(
                                g, arr_ptr, expr->loc,
                                "null reference where an array is required (element store)");
                        if (local->type && local->type->kind == TYPE_ARRAY &&
                            local->type->array_rank > 1) {
                            /* 多维矩形数组槽位：多维越界检查与行优先平铺寻址 */
                            elem_ptr = emit_mdarray_elem_ptr(g, arr_ptr,
                                expr->binary.left, elem_llvm, locals, &ar_oob);
                        } else {
                            idx_1d = emit_expr(g, expr->binary.left->index.index, locals);
                            if (local->type && local->type->kind == TYPE_ARRAY) {
                                is_arr_1d = 1;
                                LLVMValueRef arr_len = zan_array_len(g, arr_ptr);
                                ar_oob = emit_index_bounds_check(g, idx_1d, arr_len,
                                    expr->loc, "array");
                            }
                        }
                        LLVMBasicBlockRef ar_do = NULL, ar_end = NULL;
                        emit_oob_store_split(g, ar_oob, &ar_do, &ar_end);
                        if (!elem_ptr) {
                            LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                                LLVMPointerType(elem_llvm, 0), "arrp");
                            elem_ptr = is_arr_1d
                                ? emit_checked_elem_gep(g, elem_llvm, typed_arr, idx_1d, "eidx")
                                : LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx_1d, 1, "eidx");
                        }
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
                            idx = emit_index_safe_bounds(g, idx, len, expr->loc, "string");
                        } else if (fsym->type && fsym->type->kind == TYPE_STRING) {
                            idx = emit_string_elem_guard(g, arr_ptr, idx, expr->loc, &arr_ptr);
                        } else if (fsym->type && fsym->type->kind == TYPE_ARRAY &&
                                   fsym->type->array_rank <= 1) {
                            arr_ptr = emit_array_null_guard(
                                g, arr_ptr, expr->loc,
                                "null reference where an array is required (element store)");
                            LLVMValueRef arr_len = zan_array_len(g, arr_ptr);
                            ar_oob = emit_index_bounds_check(g, idx, arr_len,
                                expr->loc, "array");
                        }
                        if (fsym->type && type_named(fsym->type, "List", 4)) {
                            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                            LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, arr_ptr,
                                LLVMPointerType(g->list_struct_type, 0), "lptr");
                            LLVMValueRef count_field = LLVMBuildStructGEP2(g->builder,
                                g->list_struct_type, list_ptr, 0, "countf");
                            LLVMValueRef count = LLVMBuildLoad2(g->builder, i64t,
                                count_field, "count");
                            zan_tbaa_tag_cnt(g, count);
                            LLVMValueRef lf_oob = emit_index_bounds_check(g, idx, count, expr->loc, "list");
                            LLVMBasicBlockRef lf_do = NULL, lf_end = NULL;
                            emit_oob_store_split(g, lf_oob, &lf_do, &lf_end);
                            if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                                LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                                idx = LLVMBuildSExt(g->builder, idx, i64t, "idxext");
                            }
                            LLVMValueRef data = list_data_for(g, list_ptr, container_elem_type(fsym->type), "data");
                            LLVMValueRef slot_ptr = list_elem_ptr(g, data, idx, container_elem_type(fsym->type), "ep");
                            emit_collection_slot_store(g, container_elem_type(fsym->type), list_slot_type(g, container_elem_type(fsym->type)), slot_ptr,
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
                        /* B-ID216：字段数组元素写必须走替后元素类型。读路径
                         * （access_index.c）对 arr_type 做 concretize，克隆体
                         * 里 T[] 变 int[]、元素 GEP 按 4 字节步；这里裸读
                         * fsym->type（开放 T[]，map_type 落 default 的 i8*）
                         * 会按 ptr 8 字节步寻址——alloc/读都是 elem 步、只有
                         * 写是 ptr 步：Set(1,42) 越过 2×4 字节数据区写进堆
                         * 邻位（无可见损坏），At(1) 读到从未写过的槽=0。 */
                        zan_type_t *ft = (fsym->type && g->cur_inst)
                            ? concretize(g, fsym->type) : fsym->type;
                        LLVMTypeRef elem_llvm = LLVMInt32TypeInContext(g->ctx);
                        if (ft && ft->element_type) {
                            zan_type_t *et = ft->element_type;
                            elem_llvm = map_type(g, et);
                        }
                        LLVMValueRef elem_ptr = NULL;
                        if (ft && ft->kind == TYPE_ARRAY &&
                            ft->array_rank > 1) {
                            elem_ptr = emit_mdarray_elem_ptr(g, arr_ptr,
                                expr->binary.left, elem_llvm, locals, &ar_oob);
                        }
                        LLVMBasicBlockRef ar_do = NULL, ar_end = NULL;
                        emit_oob_store_split(g, ar_oob, &ar_do, &ar_end);
                        if (!elem_ptr) {
                            LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                                LLVMPointerType(elem_llvm, 0), "arrp");
                            elem_ptr = (ft && ft->kind == TYPE_ARRAY)
                                ? LLVMBuildInBoundsGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx")
                                : LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
                        }
                        zan_type_t *et = ft ? ft->element_type : NULL;
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
                        LLVMValueRef count = LLVMBuildLoad2(g->builder, i64t,
                            LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                                list_ptr, 0, "countf"), "count");
                        zan_tbaa_tag_cnt(g, count);
                        LLVMValueRef lm_oob = emit_index_bounds_check(g, idx, count, expr->loc, "list");
                        LLVMBasicBlockRef lm_do = NULL, lm_end = NULL;
                        emit_oob_store_split(g, lm_oob, &lm_do, &lm_end);
                        if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                            idx = LLVMBuildSExt(g->builder, idx, i64t, "idxext");
                        }
                        LLVMValueRef data = list_data_for(g, list_ptr, container_elem_type(at), "data");
                        LLVMValueRef slot_ptr = list_elem_ptr(g, data, idx, container_elem_type(at), "ep");
                        emit_collection_slot_store(g, container_elem_type(at), list_slot_type(g, container_elem_type(at)), slot_ptr,
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
                        LLVMValueRef elem_ptr = NULL;
                        if (at->kind == TYPE_ARRAY)
                            arr_ptr = emit_array_null_guard(
                                g, arr_ptr, expr->loc,
                                "null reference where an array is required (element store)");
                        if (at->kind == TYPE_ARRAY && at->array_rank > 1) {
                            elem_ptr = emit_mdarray_elem_ptr(g, arr_ptr,
                                expr->binary.left, elem_llvm, locals, &ar_oob);
                        } else if (at->kind == TYPE_ARRAY && at->array_rank == 1) {
                            LLVMValueRef arr_len = zan_array_len(g, arr_ptr);
                            ar_oob = emit_index_bounds_check(g, idx, arr_len,
                                expr->loc, "array");
                        }
                        LLVMBasicBlockRef ar_do = NULL, ar_end = NULL;
                        emit_oob_store_split(g, ar_oob, &ar_do, &ar_end);
                        if (!elem_ptr) {
                            LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                                LLVMPointerType(elem_llvm, 0), "arrp");
                            elem_ptr = (at->kind == TYPE_ARRAY && at->array_rank == 1)
                                ? emit_checked_elem_gep(g, elem_llvm, typed_arr, idx, "eidx")
                                : LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
                        }
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
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64t,
                        LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                            list_ptr, 0, "countf"), "count");
                    zan_tbaa_tag_cnt(g, count);
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    LLVMValueRef le_oob = emit_index_bounds_check(g, idx, count, expr->loc, "list");
                    LLVMBasicBlockRef le_do = NULL, le_end = NULL;
                    emit_oob_store_split(g, le_oob, &le_do, &le_end);
                    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                        idx = LLVMBuildSExt(g->builder, idx, i64t, "idxext");
                    }
                    LLVMValueRef data = list_data_for(g, list_ptr, container_elem_type(at), "data");
                    LLVMValueRef slot_ptr = list_elem_ptr(g, data, idx, container_elem_type(at), "ep");
                    emit_collection_slot_store(g, container_elem_type(at), list_slot_type(g, container_elem_type(at)), slot_ptr,
                        right, expr->binary.right, locals, 1);
                    emit_oob_store_finish(g, le_do, le_end);
                    emit_release_owned_call_temp(g, arr_expr, arr_ptr, locals);
                } else if (at && at->kind == TYPE_ARRAY) {
                    LLVMTypeRef elem_llvm = at->element_type
                        ? map_type(g, at->element_type)
                        : LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
                    LLVMValueRef elem_ptr = NULL;
                    LLVMValueRef idx_1d = NULL;
                    LLVMValueRef ar_oob = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
                    arr_ptr = emit_array_null_guard(
                        g, arr_ptr, expr->loc,
                        "null reference where an array is required (element store)");
                    if (at->array_rank > 1) {
                        elem_ptr = emit_mdarray_elem_ptr(g, arr_ptr,
                            expr->binary.left, elem_llvm, locals, &ar_oob);
                    } else {
                        idx_1d = emit_expr(g, expr->binary.left->index.index, locals);
                        LLVMValueRef arr_len = zan_array_len(g, arr_ptr);
                        ar_oob = emit_index_bounds_check(g, idx_1d, arr_len,
                            expr->loc, "array");
                    }
                    LLVMBasicBlockRef ar_do = NULL, ar_end = NULL;
                    emit_oob_store_split(g, ar_oob, &ar_do, &ar_end);
                    if (!elem_ptr) {
                        LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                            LLVMPointerType(elem_llvm, 0), "arrp");
                        elem_ptr = emit_checked_elem_gep(g, elem_llvm,
                            typed_arr, idx_1d, "eidx");
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
            if (obj_expr->kind == AST_IDENTIFIER) {
                zan_symbol_t *cs = receiver_type_symbol(g, obj_expr, locals);
                if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                    zan_symbol_t *fs = get_field_sym(cs, expr->binary.left->member.name);
                    zan_type_t *sinst = static_access_inst(g, obj_expr);
                    zan_symbol_t *setter = property_setter_sym(g, fs);
                    if (setter) {
                        emit_property_setter_call(g, setter,
                            sinst ? sinst : cs->type, NULL,
                            right, obj_expr, expr->binary.right, locals);
                        stored = true;
                    } else {
                    LLVMValueRef gv = get_static_field_global(g, cs, fs, sinst);
                    if (gv) {
                        zan_type_t *fst = field_store_type(g, fs, sinst);
                        zan_type_t *rft = fst ? fst : fs->type;
                        if (rft && (is_rc_managed_type(rft) || rft->kind == TYPE_OBJECT)) {
                            emit_rc_store_member(g, fs, rft, gv, right,
                                                expr->binary.right, locals);
                        } else if (rft && rft->kind == TYPE_STRUCT &&
                                   type_contains_collection_rc(g, rft, 0)) {
                            emit_struct_field_capture(g, rft, gv, right,
                                                      expr->binary.right, locals);
                        } else if (rft && rft->kind == TYPE_STRUCT) {
                            /* 值结构体字段按值整体赋值（实参可能是构造临时对象的地址） */
                            zan_store_fit(g, coerce_struct_value(g, right, rft), gv);
                        } else {
                            LLVMTypeRef ft = rft ? map_type(g, rft)
                                                 : LLVMInt64TypeInContext(g->ctx);
                            zan_store_fit(g, coerce_int_to(g, right, ft), gv);
                        }
                        stored = true;
                    }
                    }
                }
            }
            if (!stored && obj_expr->kind == AST_IDENTIFIER) {
                local_var_t *local = receiver_local(obj_expr, locals);
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
                            LLVMValueRef struct_ptr =
                                struct_recv_ptr_guarded(g, local, st, obj_expr);
                            LLVMValueRef fptr = emit_field_ptr(g, local->type->sym, st, struct_ptr, fi, "fld");
                            zan_symbol_t *afsym = get_field_sym(local->type->sym, expr->binary.left->member.name);
                            zan_type_t *aft = afsym ? field_store_type(g, afsym, local->type) : NULL;
                            if (aft && (is_rc_managed_type(aft) || aft->kind == TYPE_OBJECT)) {
                                emit_rc_store_member(g, afsym, aft, fptr, right, expr->binary.right, locals);
                            } else if (aft && aft->kind == TYPE_STRUCT &&
                                       type_contains_collection_rc(g, aft, 0)) {
                                emit_struct_field_capture(g, aft, fptr, right,
                                                          expr->binary.right, locals);
                            } else if (aft && aft->kind == TYPE_STRUCT) {
                                /* 值结构体字段按值整体赋值（实参可能是构造临时对象的地址） */
                                LLVMValueRef sv = coerce_struct_value(g, right, aft);
                                LLVMValueRef st_i = zan_store_fit(g, sv, fptr);
                                apply_pack_field_align(g, local->type->sym, st_i, LLVMTypeOf(sv));
                            } else {
                                LLVMValueRef st_i = zan_store_fit(g, right, fptr);
                                apply_pack_field_align(g, local->type->sym, st_i,
                                                       aft ? map_type(g, aft) : LLVMTypeOf(right));
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
                        if ((obj_expr->kind == AST_INDEX ||
                             (obj_expr->kind == AST_CALL &&
                              call_returns_ref(g, obj_expr, locals))) &&
                            cls->kind == SYM_STRUCT) {
                            /* 集合/数组/ref 返回值内结构体元素字段就地赋值 */
                            zan_type_t *et = infer_expr_type(g, obj_expr, locals);
                            LLVMTypeRef st = get_struct_llvm_type(g, cls);
                            LLVMValueRef ep = (st && !type_is_binding(et))
                                ? (obj_expr->kind == AST_INDEX
                                    ? emit_struct_elem_ptr(g, obj_expr, locals)
                                    : emit_ref_lvalue_ptr(g, obj_expr, locals))
                                : NULL;
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
                                        emit_rc_store_member(g, gfsym, gft, fptr, right,
                                            expr->binary.right, locals);
                                    } else if (gft && gft->kind == TYPE_STRUCT &&
                                               type_contains_collection_rc(g, gft, 0)) {
                                        emit_struct_field_capture(g, gft, fptr, right,
                                                                  expr->binary.right, locals);
                                    } else if (gft && gft->kind == TYPE_STRUCT) {
                                        /* 值结构体字段按值整体赋值（实参可能是构造临时对象的地址） */
                                        LLVMValueRef sv = coerce_struct_value(g, right, gft);
                                        LLVMValueRef st_i = zan_store_fit(g, sv, fptr);
                                        apply_pack_field_align(g, cls, st_i, LLVMTypeOf(sv));
                                    } else {
                                        LLVMValueRef st_i = zan_store_fit(g, right, fptr);
                                        apply_pack_field_align(g, cls, st_i,
                                                               gft ? map_type(g, gft) : LLVMTypeOf(right));
                                    }
                                }
                                stored = true;
                            }
                        } else if (setter) {
                            zan_type_t *rct = infer_expr_type(g, obj_expr, locals);
                            LLVMValueRef rval = emit_guarded_member_object(
                                g, expr->binary.left, locals);
                            /* The setter helper consumes the owned receiver temporary. */
                            emit_property_setter_call(g, setter, rct, rval,
                                right, obj_expr, expr->binary.right, locals);
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
                                    emit_rc_store_member(g, gfsym, gft, fptr, right, expr->binary.right, locals);
                                } else if (gft && gft->kind == TYPE_STRUCT &&
                                           type_contains_collection_rc(g, gft, 0)) {
                                    emit_struct_field_capture(g, gft, fptr, right,
                                                              expr->binary.right, locals);
                                } else if (gft && gft->kind == TYPE_STRUCT) {
                                    /* 值结构体字段按值整体赋值（实参可能是构造临时对象的地址） */
                                    LLVMValueRef sv = coerce_struct_value(g, right, gft);
                                    LLVMValueRef st_i = zan_store_fit(g, sv, fptr);
                                    apply_pack_field_align(g, cls, st_i, LLVMTypeOf(sv));
                                } else {
                                    LLVMValueRef st_i = zan_store_fit(g, right, fptr);
                                    apply_pack_field_align(g, cls, st_i,
                                                           gft ? map_type(g, gft) : LLVMTypeOf(right));
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
                    } else if (!stored && obj_expr->kind == AST_THIS_EXPR) {
                        /* `this.S = v` 而 S 是静态字段：读路径同口径报 E3009
                         * （实例引用不能访问静态成员）。以前这里什么都不存，
                         * 赋值静默丢失。 */
                        zan_symbol_t *sf = get_field_sym(acls, an);
                        if (sf && field_member_is_static(sf))
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
        } else if (expr->binary.left->kind == AST_CALL &&
                   call_returns_ref(g, expr->binary.left, locals)) {
            zan_type_t *lt = infer_expr_type(g, expr->binary.left, locals);
            LLVMValueRef ref_ptr = emit_ref_lvalue_ptr(g, expr->binary.left, locals);
            if (ref_ptr && lt) {
                LLVMTypeRef elem_llvm = map_type(g, lt);
                if (LLVMTypeOf(ref_ptr) != LLVMPointerType(elem_llvm, 0))
                    ref_ptr = LLVMBuildBitCast(g->builder, ref_ptr,
                                               LLVMPointerType(elem_llvm, 0),
                                               "ref.asg");
                if (is_rc_managed_type(lt) || lt->kind == TYPE_OBJECT) {
                    if (!expr_yields_owned_rc_value(g, expr->binary.right, locals)) {
                        if (is_rc_managed_type(lt))
                            emit_rc_retain_for_type(g, lt, right);
                        else if (LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMPointerTypeKind)
                            emit_arc_retain(g, right);
                    }
                    LLVMValueRef old = LLVMBuildLoad2(g->builder, elem_llvm, ref_ptr, "ref.old");
                    zan_store_fit(g, right, ref_ptr);
                    if (is_rc_managed_type(lt))
                        emit_rc_release_for_type(g, lt, old);
                    else
                        emit_arc_release_typed(g, lt, old);
                } else if (lt->kind == TYPE_STRUCT &&
                           type_contains_collection_rc(g, lt, 0)) {
                    emit_struct_field_capture(g, lt, ref_ptr, right,
                                              expr->binary.right, locals);
                } else {
                    LLVMValueRef stored = right;
                    if (lt->kind == TYPE_STRUCT)
                        stored = coerce_struct_value(g, right, lt);
                    else if (LLVMGetTypeKind(elem_llvm) == LLVMIntegerTypeKind &&
                             LLVMGetTypeKind(LLVMTypeOf(stored)) == LLVMIntegerTypeKind)
                        stored = coerce_int_to(g, stored, elem_llvm);
                    zan_store_fit(g, stored, ref_ptr);
                }
            }
        }
        return right;
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}
LLVMValueRef emit_expr_call(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals);

