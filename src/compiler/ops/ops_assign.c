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
LLVMValueRef emit_expr_call(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals);

