    case AST_BLOCK: {
        int block_start = locals->count;
        for (int i = 0; i < stmt->block.stmts.count; i++) {
            zan_ast_node_t *bs = stmt->block.stmts.items[i];
            emit_stmt(g, bs, locals);
        }
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, block_start);
        }
        break;
    }

    case AST_VAR_DECL: {
        if (stmt->var_decl.type && stmt->var_decl.initializer) {
            check_implicit_narrowing(g, resolve_type_ctx(g, stmt->var_decl.type),
                           infer_expr_type(g, stmt->var_decl.initializer, locals),
                           stmt->var_decl.initializer, "initializer");
        }
        if (emit_boxed_var_decl(g, stmt, locals)) return;
        if (g->current_async_frame && g->current_async_slot_count > 0) {
            local_var_t *pre = local_find_async_decl(locals, stmt);
            if (pre) {
                int pre_idx = (int)(pre - locals->vars);
                zan_type_t *type = stmt->var_decl.type
                    ? resolve_type_ctx(g, stmt->var_decl.type)
                    : NULL;
                if (!type && stmt->var_decl.initializer)
                    type = infer_expr_type(g, stmt->var_decl.initializer, locals);
                if (!type) type = pre->type;
                for (int i = 0; i < g->current_async_slot_count; i++) {
                    if (g->current_async_slots[i].slot_alloca == pre->alloca &&
                        !async_slot_type_compatible(type, pre->type))
                        continue;
                    if (g->current_async_slots[i].slot_alloca == pre->alloca) {
                        int arc_own = (type && is_rc_managed_type(type) &&
                                       LLVMGetTypeKind(g->current_async_slots[i].llvm) == LLVMPointerTypeKind);
                        if (stmt->var_decl.initializer) {
                            LLVMValueRef iv = (type->kind == TYPE_DELEGATE &&
                                stmt->var_decl.initializer->kind == AST_LAMBDA)
                                ? emit_lambda_typed(g, stmt->var_decl.initializer, type, locals)
                                : emit_expr(g, stmt->var_decl.initializer, locals);
                            LLVMTypeRef slot_ty = g->current_async_slots[i].llvm;
                            if (arc_own) {
                                emit_rc_capture_local(g, type, pre->alloca, iv,
                                    stmt->var_decl.initializer, locals);
                            } else if (pre->struct_rc) {
                                emit_struct_local_capture(g, type, pre->alloca, iv,
                                    stmt->var_decl.initializer, locals);
                            } else {
                                iv = coerce_int_to(g, iv, slot_ty);
                                if (LLVMGetTypeKind(slot_ty) == LLVMPointerTypeKind &&
                                    LLVMGetTypeKind(LLVMTypeOf(iv)) == LLVMPointerTypeKind &&
                                    LLVMTypeOf(iv) != slot_ty) {
                                    iv = LLVMBuildBitCast(g->builder, iv, slot_ty, "fl.bc");
                                }
                                zan_store_fit(g, iv, pre->alloca);
                            }
                        }
                        if (arc_own) pre->arc_owned = 1;
                        if (type && type->kind == TYPE_STRING &&
                            stmt->var_decl.initializer &&
                            call_targets_extern(g, stmt->var_decl.initializer))
                            pre->opaque_string = 1;
                        local_add(locals, stmt->var_decl.name,
                                  locals->vars[pre_idx].alloca, type);
                        locals->vars[locals->count - 1].frame_owner = pre_idx;
                        return;
                    }
                }
            }
        }
        zan_type_t *type = g->binder->type_int; /* default */
        if (stmt->var_decl.type) {
            type = resolve_type_ctx(g, stmt->var_decl.type);
        } else if (stmt->var_decl.initializer) {
            zan_ast_node_t *init = stmt->var_decl.initializer;

            if (init->kind == AST_NEW_EXPR && init->new_expr.is_array) {
                LLVMValueRef arr_val = emit_expr(g, init, locals);
                LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef alloca = emit_entry_alloca(g, ptr_type, "arr");
                zan_store_fit(g, arr_val, alloca);

                zan_type_t *arr_type = resolve_type_ctx(g, init->new_expr.type);
                if (!arr_type || arr_type->kind != TYPE_ARRAY) {
                    zan_type_t *elem_type = arr_type ? arr_type : g->binder->type_int;
                    arr_type = (zan_type_t *)zan_arena_alloc(g->arena, sizeof(zan_type_t));
                    memset(arr_type, 0, sizeof(zan_type_t));
                    arr_type->kind = TYPE_ARRAY;
                    arr_type->element_type = elem_type;
                    arr_type->array_rank = 1;
                }
                local_add(locals, stmt->var_decl.name, alloca, arr_type);
                arc_own_local(g, locals);
                return;
            }

            if (init->kind == AST_NEW_EXPR && init->new_expr.type &&
                init->new_expr.type->kind == AST_TYPE_REF) {
                zan_istr_t tname = init->new_expr.type->type_ref.name;
                if (tname.len == 4 && memcmp(tname.str, "List", 4) == 0) {
                    LLVMValueRef list_val = emit_expr(g, init, locals);
                    LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef alloca = emit_entry_alloca(g, ptr_type, "list");
                    zan_store_fit(g, list_val, alloca);
                    zan_type_t *list_type = resolve_type_ctx(g, init->new_expr.type);
                    local_add(locals, stmt->var_decl.name, alloca, list_type);
                    if (list_type && is_rc_managed_type(list_type))
                        arc_own_local(g, locals);
                    return;
                }
                if (tname.len == 13 && memcmp(tname.str, "StringBuilder", 13) == 0) {
                    LLVMValueRef sb_val = emit_expr(g, init, locals);
                    LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef alloca = emit_entry_alloca(g, ptr_type, "sb");
                    zan_store_fit(g, sb_val, alloca);
                    zan_type_t *sb_type = resolve_type_ctx(g, init->new_expr.type);
                    local_add(locals, stmt->var_decl.name, alloca, sb_type);
                    if (sb_type && is_rc_managed_type(sb_type))
                        arc_own_local(g, locals);
                    return;
                }
                if ((tname.len == 4 && memcmp(tname.str, "Dict", 4) == 0) ||
                    (tname.len == 10 && memcmp(tname.str, "Dictionary", 10) == 0)) {
                    LLVMValueRef dict_val = emit_expr(g, init, locals);
                    LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef alloca = emit_entry_alloca(g, ptr_type, "dict");
                    zan_store_fit(g, dict_val, alloca);
                    zan_type_t *dict_type = resolve_type_ctx(g, init->new_expr.type);
                    local_add(locals, stmt->var_decl.name, alloca, dict_type);
                    arc_own_local(g, locals);
                    return;
                }
            }

            if (init->kind == AST_NEW_EXPR && init->new_expr.type) {
                zan_istr_t type_name = {NULL, 0};
                if (init->new_expr.type->kind == AST_IDENTIFIER) {
                    type_name = init->new_expr.type->ident.name;
                } else if (init->new_expr.type->kind == AST_TYPE_REF) {
                    type_name = init->new_expr.type->type_ref.name;
                }
                if (type_name.str) {
                    zan_symbol_t *sym = zan_binder_lookup(g->binder, type_name);
                    if (sym && sym->type && sym->type->kind == TYPE_STRUCT) {
                        type = sym->type;
                        LLVMTypeRef st = get_struct_llvm_type(g, sym);
                        if (st) {
                            LLVMValueRef alloca = emit_entry_alloca(g, st, "var");
                            zan_store_fit(g, LLVMConstNull(st), alloca);

                            bool ctor_called = false;
                            zan_type_t *new_inst = resolve_type_ctx(
                                g, init->new_expr.type);
                            int init_start = init->new_expr.args.count;
                            while (init_start > 0) {
                                zan_ast_node_t *a =
                                    init->new_expr.args.items[init_start - 1];
                                if (a->kind == AST_COLL_INIT) {
                                    if (get_field_index(sym, a->coll_init.name) < 0)
                                        break;
                                    init_start--;
                                    continue;
                                }
                                if (a->kind != AST_ASSIGNMENT ||
                                    a->binary.left->kind != AST_IDENTIFIER ||
                                    get_field_index(sym,
                                        a->binary.left->ident.name) < 0)
                                    break;
                                init_start--;
                            }
                            zan_ast_list_t ctor_args = init->new_expr.args;
                            ctor_args.count = init_start;
                            struct zan_ctor_entry *ctor = find_ctor(
                                g, sym, &ctor_args, locals, NULL);
                            if (!ctor) {
                                zan_ast_list_t filled;
                                if (fill_ctor_default_args(g, sym, &ctor_args,
                                                           &filled)) {
                                    struct zan_ctor_entry *dc = find_ctor(
                                        g, sym, &filled, locals, NULL);
                                    if (dc) {
                                        ctor = dc;
                                        ctor_args = filled;
                                    }
                                }
                            }
                            if (ctor) {
                                int argc = ctor_args.count + 1;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                                    (size_t)argc, sizeof(LLVMValueRef));
                                call_args[0] = alloca;
                                /* 实参里的拥有型临时量：构造函数抛异常时由展开器
                                 * 释放（与普通方法调用路径同源判定）。 */
                                int *arg_eh_pushed = (int *)calloc(
                                    (size_t)argc, sizeof(int));
                                for (int k = 0; k < ctor_args.count; k++) {
                                    zan_ast_node_t *param =
                                        ctor->decl->method_decl.params.items[k];
                                    zan_type_t *pt = zan_binder_resolve_type(
                                        g->binder, param->param.type);
                                    call_args[k + 1] = emit_arg_typed(
                                        g, ctor_args.items[k], pt, locals);
                                    arg_eh_pushed[k + 1] = emit_call_arg_eh_push(
                                        g, ctor_args.items[k], call_args[k + 1],
                                        locals);
                                }
                                coerce_args_to_params(g, ctor->fn_type, call_args, argc);
                                zan_call2(g->builder, ctor->fn_type, ctor->fn,
                                    call_args, (unsigned)argc, "");
                                for (int k = argc - 1; k >= 0; k--)
                                    if (arg_eh_pushed[k]) emit_eh_tmp_pop(g);
                                for (int k = 0; k < ctor_args.count; k++)
                                    emit_release_owned_call_temp(g,
                                        ctor_args.items[k],
                                        call_args[k + 1], locals);
                                free(arg_eh_pushed);
                                free(call_args);
                                ctor_called = true;
                            }

                            if (!ctor_called) {
                                emit_implicit_field_initializers(
                                    g, sym, new_inst ? new_inst : sym->type,
                                    alloca, locals);
                            }
                            zan_ast_list_t object_inits;
                            zan_ast_list_init(&object_inits);
                            for (int oi = init_start;
                                 oi < init->new_expr.args.count; oi++) {
                                zan_ast_list_push(&object_inits,
                                    init->new_expr.args.items[oi], g->arena);
                            }
                            for (int oi = 0;
                                 oi < init->new_expr.arg_inits.count; oi++) {
                                zan_ast_list_push(&object_inits,
                                    init->new_expr.arg_inits.items[oi], g->arena);
                            }
                            for (int i = 0; i < object_inits.count; i++) {
                                zan_ast_node_t *arg = object_inits.items[i];
                                if (arg->kind == AST_COLL_INIT) {
                                    zan_istr_t cname = arg->coll_init.name;
                                    zan_symbol_t *msym = get_field_sym(sym, cname);
                                    if (!msym || !msym->type) continue;
                                    int getter_owned = 0;
                                    zan_ast_node_t *recv = zan_ast_new(g->arena,
                                        AST_IDENTIFIER, arg->loc);
                                    recv->ident.name = cname;
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
                                        zan_ast_list_push(&addcall->call.args,
                                                          item, g->arena);
                                        if (msym->type &&
                                            type_named(msym->type, "Dict", 4) &&
                                            k + 1 < arg->coll_init.items.count) {
                                            zan_ast_list_push(&addcall->call.args,
                                                arg->coll_init.items.items[++k],
                                                g->arena);
                                        }
                                        emit_expr(g, addcall, locals);
                                    }
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
                                        LLVMValueRef fval = emit_expr(g, arg->binary.right, locals);
                                        zan_type_t *fst = field_store_type(
                                            g, fsym, new_inst ? new_inst : sym->type);
                                        if (fsym && fsym->type) {
                                            LLVMTypeRef target_t = map_type(g, fsym->type);
                                            LLVMTypeRef val_t = LLVMTypeOf(fval);
                                            if (LLVMGetTypeKind(target_t) == LLVMFloatTypeKind &&
                                                LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                                                fval = LLVMBuildFPTrunc(g->builder, fval, target_t, "trunc");
                                            } else if (LLVMGetTypeKind(target_t) == LLVMDoubleTypeKind &&
                                                       LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
                                                fval = LLVMBuildFPExt(g->builder, fval, target_t, "ext");
                                            }
                                        }
                                        if (fst && (is_rc_managed_type(fst) || fst->kind == TYPE_OBJECT)) {
                                            emit_rc_store_member(g, fsym, fst, fptr, fval,
                                                arg->binary.right, locals);
                                        } else if (fst && fst->kind == TYPE_STRUCT &&
                                                   type_contains_collection_rc(g, fst, 0)) {
                                            emit_struct_field_capture(g, fst, fptr, fval,
                                                arg->binary.right, locals);
                                        } else {
                                            zan_store_fit(g, fval, fptr);
                                        }
                                    }
                                }
                            }
                            local_add(locals, stmt->var_decl.name, alloca, type);
                            if (type && type->kind == TYPE_STRUCT &&
                                type_contains_collection_rc(g, type, 0)) {
                                locals->vars[locals->count - 1].struct_rc = 1;
                                lpad_register_local(g, &locals->vars[locals->count - 1]);
                            }
                            return;
                        }
                    }
                }
            }

            if (init->kind == AST_TUPLE_EXPR) {
                zan_type_t *ttype = infer_expr_type(g, init, locals);
                if (ttype && ttype->kind == TYPE_STRUCT && ttype->sym) {
                    LLVMTypeRef st = get_struct_llvm_type(g, ttype->sym);
                    if (!st) st = map_type(g, ttype);
                    if (st) {
                        LLVMValueRef alloca = emit_entry_alloca(g, st, "tup");
                        zan_store_fit(g, LLVMConstNull(st), alloca);
                        LLVMValueRef tup = emit_expr(g, init, locals);
                        LLVMValueRef tupval =
                            LLVMGetTypeKind(LLVMTypeOf(tup)) == LLVMPointerTypeKind
                                ? LLVMBuildLoad2(g->builder, st, tup, "tup.load")
                                : tup;
                        zan_store_fit(g, tupval, alloca);
                        local_add(locals, stmt->var_decl.name, alloca, ttype);
                        if (type_contains_collection_rc(g, ttype, 0)) {
                            locals->vars[locals->count - 1].struct_rc = 1;
                            lpad_register_local(g, &locals->vars[locals->count - 1]);
                        }
                        return;
                    }
                }
            }

            type = infer_expr_type(g, init, locals);
            LLVMValueRef init_val = emit_expr(g, init, locals);
            LLVMTypeRef init_type = LLVMTypeOf(init_val);
            LLVMValueRef alloca = emit_entry_alloca(g, init_type, "var");
            zan_store_fit(g, init_val, alloca);
            if (type) {
                LLVMTypeRef mt = map_type(g, type);
                bool shape_ok =
                    LLVMGetTypeKind(mt) == LLVMGetTypeKind(init_type);
                if (LLVMGetTypeKind(init_type) == LLVMPointerTypeKind)
                    shape_ok = LLVMGetTypeKind(mt) == LLVMPointerTypeKind ||
                        type->kind == TYPE_NULLABLE;
                if (!shape_ok) type = NULL;
            }
            if (!type) {
                if (LLVMGetTypeKind(init_type) == LLVMDoubleTypeKind) {
                    type = g->binder->type_double;
                } else if (LLVMGetTypeKind(init_type) == LLVMFloatTypeKind) {
                    type = g->binder->type_float;
                } else if (LLVMGetTypeKind(init_type) == LLVMPointerTypeKind) {
                    type = g->binder->type_string;
                } else if (LLVMGetTypeKind(init_type) == LLVMIntegerTypeKind) {
                    unsigned bits = LLVMGetIntTypeWidth(init_type);
                    if (bits <= 32) type = g->binder->type_int;
                    else type = g->binder->type_long;
                } else if (llvm_is_nullable(init_type)) {
                    zan_type_t *it = infer_expr_type(g, init, locals);
                    type = (it && it->kind == TYPE_NULLABLE) ? it : g->binder->type_int;
                } else {
                    type = g->binder->type_int;
                }
            }
            if (type && type->kind == TYPE_NULLABLE &&
                !llvm_is_nullable(init_type)) {
                type = NULL;
            }
            if (type && type->kind == TYPE_NULLABLE) {
                LLVMTypeRef nst = map_type(g, type);
                LLVMValueRef nslot = emit_entry_alloca(g, nst, "var");
                zan_store_fit(g, init_val, nslot);
                local_add(locals, stmt->var_decl.name, nslot, type);
                return;
            }
            bool type_is_string = type && type->kind == TYPE_STRING;
            bool type_is_ptr_class = type &&
                (type->kind == TYPE_CLASS || type->kind == TYPE_INTERFACE ||
                 ((type->kind == TYPE_STRUCT) && type->sym &&
                  get_struct_llvm_type(g, type->sym) &&
                  LLVMGetTypeKind(get_struct_llvm_type(g, type->sym)) ==
                      LLVMPointerTypeKind));
            if (type_is_ptr_class || (type_is_string && init_val &&
                LLVMGetTypeKind(init_type) != LLVMPointerTypeKind)) {
                LLVMTypeRef llvm_t = map_type(g, type);
                LLVMValueRef slot = emit_entry_alloca(g, llvm_t, "var");
                zan_store_fit(g, LLVMConstNull(llvm_t), slot);
                init_val = emit_implicit_box(g, type, init_val, init, locals);
                emit_rc_capture_local(g, type, slot, init_val, init, locals);
                local_add(locals, stmt->var_decl.name, slot, type);
                arc_own_local(g, locals);
                return;
            }
            if (type_is_string) {
                LLVMTypeRef llvm_string = map_type(g, type);
                LLVMValueRef slot = emit_entry_alloca(g, llvm_string, "var");
                zan_store_fit(g, LLVMConstNull(llvm_string), slot);
                emit_rc_capture_local(g, type, slot, init_val, init, locals);
                local_add(locals, stmt->var_decl.name, slot, type);
                if (call_targets_extern(g, init))
                    locals->vars[locals->count - 1].opaque_string = 1;
                arc_own_local(g, locals);
                return;
            }
            if (type && is_rc_managed_type(type) &&
                LLVMGetTypeKind(init_type) == LLVMPointerTypeKind &&
                !type_is_ptr_class) {
                LLVMTypeRef llvm_t = map_type(g, type);
                LLVMValueRef slot = emit_entry_alloca(g, llvm_t, "var");
                zan_store_fit(g, LLVMConstNull(llvm_t), slot);
                init_val = emit_implicit_box(g, type, init_val, init, locals);
                emit_rc_capture_local(g, type, slot, init_val, init, locals);
                local_add(locals, stmt->var_decl.name, slot, type);
                arc_own_local(g, locals);
                return;
            }
            if (type && (type->kind == TYPE_ARRAY)) {
                LLVMTypeRef llvm_t = map_type(g, type);
                LLVMValueRef slot = emit_entry_alloca(g, llvm_t, "var");
                zan_store_fit(g, init_val, slot);
                local_add(locals, stmt->var_decl.name, slot, type);
                arc_own_local(g, locals);
                return;
            }
            local_add(locals, stmt->var_decl.name, alloca, type);
            return;
        }

        LLVMTypeRef llvm_type = map_type(g, type);
        LLVMValueRef alloca = (type && type->kind == TYPE_STRING)
            ? emit_entry_alloca(g, llvm_type, "var")
            : emit_entry_alloca(g, llvm_type, "var");

        int arc_own = (type && is_rc_managed_type(type) &&
                       LLVMGetTypeKind(llvm_type) == LLVMPointerTypeKind);
        int struct_own = (type && type->kind == TYPE_STRUCT &&
                          LLVMGetTypeKind(llvm_type) == LLVMStructTypeKind &&
                          type_contains_collection_rc(g, type, 0));
        if (arc_own || struct_own)
            zan_store_fit(g, LLVMConstNull(llvm_type), alloca);
        if (type && type->kind == TYPE_STRING && !stmt->var_decl.initializer) {
            zan_istr_t empty = { (char *)"", 0 };
            zan_store_fit(g, emit_string_literal_rc(g, empty), alloca);
        }
        local_var_t obj_slot;
        memset(&obj_slot, 0, sizeof(obj_slot));
        obj_slot.alloca = alloca;
        obj_slot.type = type;
        int obj_own = local_is_dyn_obj(g, &obj_slot);
        if (obj_own) zan_store_fit(g, LLVMConstNull(llvm_type), alloca);

        if (stmt->var_decl.initializer) {
            check_value_type_mismatch(g, type,
                infer_expr_type(g, stmt->var_decl.initializer, locals),
                stmt->var_decl.initializer, "initializer");
            check_generic_invariance(g, type,
                infer_expr_type(g, stmt->var_decl.initializer, locals),
                stmt->var_decl.initializer, "initializer");
            if (type && type->kind == TYPE_DELEGATE &&
                stmt->var_decl.initializer->kind != AST_LAMBDA)
                check_delegate_async_match(g, stmt->var_decl.initializer, type, locals);
            LLVMValueRef init_val = NULL;
            int init_owned = 0;
            if (type && type_is_binding(type) &&
                !type_is_binding(infer_expr_type(g, stmt->var_decl.initializer, locals))) {
                init_val = emit_binding_value(g, type,
                                              stmt->var_decl.initializer, locals);
                if (init_val) init_owned = 1;
            }
            if (!init_val)
                init_val =
                    (type && type->kind == TYPE_DELEGATE &&
                     stmt->var_decl.initializer->kind == AST_LAMBDA)
                        ? emit_lambda_typed(g, stmt->var_decl.initializer, type, locals)
                        : emit_expr(g, stmt->var_decl.initializer, locals);
            zan_ast_node_t *init_src = stmt->var_decl.initializer;
            bool conv_owned = false;
            bool bind_owned = init_owned;
            if (type && !bind_owned) {
                zan_type_t *ity = infer_expr_type(g, init_src, locals);
                if (ity) {
                    conv_owned =
                        (find_user_conversion(g, ity, type, "op_implicit") != NULL) &&
                        is_rc_managed_type(type);
                    init_val = emit_user_conversion(g, ity, type, "op_implicit",
                                                    init_val, init_src, locals);
                }
            }
            if (arc_own) {
                init_val = emit_implicit_box(g, type, init_val, init_src, locals);
                emit_rc_capture_local(g, type, alloca, init_val,
                    (conv_owned || bind_owned)
                        ? owned_rhs_marker(g, init_src->loc) : init_src,
                    locals);
            } else if (obj_own) {
                emit_obj_local_store(g, &obj_slot, init_val,
                    infer_expr_type(g, stmt->var_decl.initializer, locals),
                    stmt->var_decl.initializer, locals);
            } else if (type && type->kind == TYPE_STRUCT &&
                       type_contains_collection_rc(g, type, 0)) {
                init_val = coerce_int_to(g, init_val, llvm_type);
                zan_store_fit(g, init_val, alloca);
                if (!expr_yields_owned_rc_value(g, init_src, locals) &&
                    !conv_owned)
                    emit_struct_local_retain(g, type, alloca);
            } else {
                init_val = coerce_int_to(g, init_val, llvm_type);
                zan_store_fit(g, init_val, alloca);
            }
        }

        if (type && type->kind == TYPE_ARRAY && stmt->var_decl.initializer) {
            zan_type_t *it = infer_expr_type(g, stmt->var_decl.initializer, locals);
            if (it && it->kind != TYPE_ARRAY && it->name.str &&
                ((type_named(it, "List", 4)) ||
                 (type_named(it, "Dictionary", 10)) ||
                 (type_named(it, "Dict", 4))))
                zan_diag_emit_code(g->diag, DIAG_ERROR, stmt->loc, 3072,
                    "cannot initialize an array from '%.*s': declare the "
                    "variable as '%.*s' instead",
                    (int)it->name.len, it->name.str,
                    (int)it->name.len, it->name.str);
        }

        local_add(locals, stmt->var_decl.name, alloca, type);
        if (type && type->kind == TYPE_STRING && stmt->var_decl.initializer &&
            call_targets_extern(g, stmt->var_decl.initializer))
            locals->vars[locals->count - 1].opaque_string = 1;
        if (struct_own) {
            locals->vars[locals->count - 1].struct_rc = 1;
            lpad_register_local(g, &locals->vars[locals->count - 1]);
        }
        if (arc_own) arc_own_local(g, locals);
        if (obj_own)
            locals->vars[locals->count - 1].obj_rc_flag = obj_slot.obj_rc_flag;
        if (type &&
            ((type_named(type, "Dict", 4)) ||
             (type_named(type, "Dictionary", 10))) &&
            stmt->var_decl.initializer &&
            stmt->var_decl.initializer->kind == AST_NEW_EXPR)
            arc_own_local(g, locals);
        if (type && type->kind == TYPE_ARRAY &&
            stmt->var_decl.initializer &&
            stmt->var_decl.initializer->kind == AST_NEW_EXPR &&
            stmt->var_decl.initializer->new_expr.is_array &&
            stmt->var_decl.initializer->new_expr.args.count > 0) {
            if (stmt->var_decl.initializer->new_expr.array_init) {
                LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef lslot = emit_entry_alloca(g, i64t, "arr.lenslot3");
                zan_store_fit(g, LLVMConstInt(i64t,
                    (unsigned long long)stmt->var_decl.initializer->new_expr.args.count, 0),
                    lslot);
                locals->vars[locals->count - 1].arr_len_slot = lslot;
                break;
            }
            zan_ast_node_t *sz = stmt->var_decl.initializer->new_expr.args.items[0];
            if (sz->kind == AST_INT_LITERAL || sz->kind == AST_IDENTIFIER) {
                LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef szv = emit_expr(g, sz, locals);
                if (LLVMGetTypeKind(LLVMTypeOf(szv)) == LLVMIntegerTypeKind &&
                    LLVMGetIntTypeWidth(LLVMTypeOf(szv)) < 64)
                    szv = LLVMBuildSExt(g->builder, szv, i64t, "arr.len");
                LLVMValueRef lslot = emit_entry_alloca(g, i64t, "arr.lenslot2");
                zan_store_fit(g, szv, lslot);
                locals->vars[locals->count - 1].arr_len_slot = lslot;
            }
        }
        break;
    }

    case AST_TUPLE_DECON: {
        zan_ast_node_t *init = stmt->tuple_decon.initializer;
        zan_type_t *ttype = infer_expr_type(g, init, locals);
        if (!ttype || ttype->kind != TYPE_STRUCT || !ttype->sym) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, stmt->loc, 3073,
                          "right-hand side of a deconstruction is not a tuple");
            break;
        }
        LLVMTypeRef st = get_struct_llvm_type(g, ttype->sym);
        if (!st) st = map_type(g, ttype);
        LLVMValueRef tmp = NULL;
        if (st) {
            tmp = emit_entry_alloca(g, st, "dtmp");
            zan_store_fit(g, LLVMConstNull(st), tmp);
            if (init->kind == AST_TUPLE_EXPR) {
                int n = init->tuple_expr.items.count;
                for (int i = 0; i < n; i++) {
                    zan_ast_node_t *item = init->tuple_expr.items.items[i];
                    char fname[16];
                    snprintf(fname, sizeof fname, "Item%d", i + 1);
                    zan_istr_t f_istr = { fname, (uint32_t)strlen(fname) };
                    zan_symbol_t *fsym = get_field_sym(ttype->sym, f_istr);
                    if (!fsym) continue;
                    int fi = get_field_index(ttype->sym, f_istr);
                    if (fi < 0) fi = i;
                    LLVMValueRef fptr = emit_field_ptr(g, ttype->sym, st, tmp,
                                                       fi, "df");
                    LLVMValueRef fval = emit_arg_typed(g, item, fsym->type,
                                                       locals);
                    if (fsym->type && is_rc_managed_type(fsym->type) &&
                        LLVMGetTypeKind(LLVMTypeOf(fval)) ==
                            LLVMPointerTypeKind &&
                        !expr_yields_owned_rc_value(g, item, locals)) {
                        emit_rc_retain_for_type(g, fsym->type, fval);
                    } else if (fsym->type &&
                               fsym->type->kind == TYPE_STRUCT &&
                               type_contains_collection_rc(g, fsym->type, 0) &&
                               !expr_yields_owned_rc_value(g, item, locals)) {
                        emit_collection_value_retain(g, fsym->type, fval, 0);
                    }
                    zan_store_fit(g, fval, fptr);
                }
            } else {
                LLVMValueRef tv = emit_expr(g, init, locals);
                LLVMValueRef tvv =
                    LLVMGetTypeKind(LLVMTypeOf(tv)) == LLVMPointerTypeKind
                        ? LLVMBuildLoad2(g->builder, st, tv, "dtmp.load")
                        : tv;
                zan_store_fit(g, tvv, tmp);
            }
        }
        bool de_consumed[64];
        int de_fields = 0;
        while (de_fields < 64) {
            char dfn[16];
            snprintf(dfn, sizeof dfn, "Item%d", de_fields + 1);
            zan_istr_t di = { dfn, (uint32_t)strlen(dfn) };
            if (!get_field_sym(ttype->sym, di)) break;
            de_fields++;
        }
        for (int i = 0; i < 64; i++) de_consumed[i] = false;
        bool de_src_owns = expr_yields_owned_rc_value(g, init, locals);
        for (int i = 0; i < stmt->tuple_decon.names.count; i++) {
            zan_ast_node_t *nm = stmt->tuple_decon.names.items[i];
            zan_ast_node_t *ty = stmt->tuple_decon.types.items[i];
            if (!nm || nm->kind != AST_IDENTIFIER) continue;
            zan_type_t *et = ty ? resolve_type_ctx(g, ty) : NULL;
            int fi = -1;
            if (!et) {
                char fname[16];
                snprintf(fname, sizeof fname, "Item%d", i + 1);
                zan_istr_t f_istr = { fname, (uint32_t)strlen(fname) };
                zan_symbol_t *fsym = ttype->sym
                    ? get_field_sym(ttype->sym, f_istr) : NULL;
                et = fsym ? fsym->type : g->binder->type_int;
            }
            {
                char fname[16];
                snprintf(fname, sizeof fname, "Item%d", i + 1);
                zan_istr_t f_istr = { fname, (uint32_t)strlen(fname) };
                fi = get_field_index(ttype->sym, f_istr);
                if (fi < 0) fi = i;
            }
            LLVMTypeRef elt = map_type(g, et);
            LLVMValueRef slot = emit_entry_alloca(g, elt, "d");
            zan_store_fit(g, LLVMConstNull(elt), slot);
            LLVMValueRef de_fval = NULL;
            if (st && tmp) {
                LLVMValueRef fptr = emit_field_ptr(g, ttype->sym, st, tmp,
                                                   fi, "de");
                de_fval = LLVMBuildLoad2(g->builder, elt, fptr, "de.load");
                zan_store_fit(g, de_fval, slot);
            }
            local_add(locals, nm->ident.name, slot, et);
            if (et && is_rc_managed_type(et)) {
                if (!de_src_owns && de_fval &&
                    LLVMGetTypeKind(elt) == LLVMPointerTypeKind)
                    emit_rc_retain_for_type(g, et, de_fval);
                arc_own_local(g, locals);
            } else if (et && et->kind == TYPE_STRUCT &&
                       type_contains_collection_rc(g, et, 0)) {
                if (!de_src_owns && de_fval)
                    emit_collection_value_retain(g, et, de_fval, 0);
                locals->vars[locals->count - 1].struct_rc = 1;
                lpad_register_local(g, &locals->vars[locals->count - 1]);
            }
            if (fi >= 0 && fi < 64) de_consumed[fi] = true;
        }
        if (de_src_owns) {
            for (int i = 0; i < de_fields; i++) {
                if (de_consumed[i]) continue;
                char fname[16];
                snprintf(fname, sizeof fname, "Item%d", i + 1);
                zan_istr_t f_istr = { fname, (uint32_t)strlen(fname) };
                zan_symbol_t *fsym = ttype->sym
                    ? get_field_sym(ttype->sym, f_istr) : NULL;
                if (!fsym || !fsym->type) continue;
                if (!st || !tmp) break;
                int fi = get_field_index(ttype->sym, f_istr);
                if (fi < 0) fi = i;
                LLVMTypeRef felt = map_type(g, fsym->type);
                LLVMValueRef fptr = emit_field_ptr(g, ttype->sym, st, tmp,
                                                   fi, "dx");
                LLVMValueRef fval = LLVMBuildLoad2(g->builder, felt, fptr,
                                                   "dx.load");
                if (is_rc_managed_type(fsym->type) &&
                    LLVMGetTypeKind(felt) == LLVMPointerTypeKind)
                    emit_rc_release_for_type(g, fsym->type, fval);
                else if (fsym->type->kind == TYPE_STRUCT &&
                         type_contains_collection_rc(g, fsym->type, 0))
                    emit_collection_value_release(g, fsym->type, fval, 0);
            }
        }
        break;
    }

    case AST_EXPR_STMT: {
        zan_ast_node_t *e = stmt->expr_stmt.expr;
        LLVMValueRef ev = emit_expr(g, e, locals);
        if (ev && e->kind == AST_CALL && emit_detach_async_call(g, ev, false, call_is_async_callee(g, e, locals)))
            break;
        if (ev && expr_yields_owned_rc_value(g, e, locals)) {
            zan_type_t *et = infer_expr_type(g, e, locals);
            if (et && is_rc_managed_type(et)) {
                LLVMTypeKind evk = LLVMGetTypeKind(LLVMTypeOf(ev));
                if (evk == LLVMPointerTypeKind) {
                    emit_rc_release_for_type(g, et, ev);
                } else if (evk == LLVMIntegerTypeKind &&
                           e->kind == AST_AWAIT_EXPR) {
                    LLVMValueRef p = emit_boundary_coerce(g, ev,
                        LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
                    emit_rc_release_for_type(g, et, p);
                }
            } else if (et && et->kind == TYPE_STRUCT &&
                       type_contains_collection_rc(g, et, 0)) {
                emit_collection_value_release(g, et, ev, 0);
            }
        }
        break;
    }
