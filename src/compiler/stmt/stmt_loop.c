    case AST_FOREACH_STMT: {
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));

        LLVMValueRef collection = emit_expr(g, stmt->foreach_stmt.collection, locals);

        zan_type_t *col_type0 = infer_expr_type(g, stmt->foreach_stmt.collection,
                                                locals);
        zan_type_t *fe_enum_ty = foreach_proto_enum_type(g, col_type0);
        zan_type_t *fe_cur_ty = foreach_proto_current_type(g, col_type0);
        if (!fe_enum_ty && col_type0 && (col_type0->kind == TYPE_CLASS || col_type0->kind == TYPE_INTERFACE) &&
            col_type0->sym) {
            zan_istr_t gi = { (char *)"GetEnumerator", 13 };
            zan_symbol_t *gm = (col_type0->kind == TYPE_INTERFACE)
                ? resolve_iface_overload(col_type0->sym, gi, 0, 0)
                : resolve_overload(col_type0->sym, gi, 0, 0);
            if (gm && gm->decl && gm->decl->kind == AST_METHOD_DECL &&
                gm->decl->method_decl.return_type) {
                zan_type_t *rt = resolve_type_ctx(g,
                    gm->decl->method_decl.return_type);
                /* 协议齐备的接口枚举器（MoveNext/Current 都有）已被
                 * foreach_proto_enum_type 收下；走到这里说明接口缺协议成员，
                 * foreach 无法直接驱动，才报不支持。 */
                if (rt && rt->kind == TYPE_INTERFACE)
                    zan_diag_emit_code(g->diag, DIAG_ERROR, stmt->loc, 3078,
                        "foreach: GetEnumerator() returning interface '%.*s' "
                        "is not supported yet; return the concrete enumerator "
                        "type so the loop can call MoveNext/Current directly",
                        (int)rt->name.len, rt->name.str);
            }
        }
        zan_symbol_t *fe_get_m = NULL;
        zan_symbol_t *fe_next_m = NULL, *fe_cur_m = NULL, *fe_cur_getter = NULL;
        if (fe_enum_ty) {
            zan_istr_t gi = { (char *)"GetEnumerator", 13 };
            zan_istr_t ni = { (char *)"MoveNext", 8 };
            zan_istr_t ci = { (char *)"Current", 7 };
            fe_get_m = (col_type0->kind == TYPE_INTERFACE)
                ? resolve_iface_overload(col_type0->sym, gi, 0, 0)
                : resolve_overload(col_type0->sym, gi, 0, 0);
            fe_next_m = (fe_enum_ty->kind == TYPE_INTERFACE)
                ? resolve_iface_overload(fe_enum_ty->sym, ni, 0, 0)
                : resolve_overload(fe_enum_ty->sym, ni, 0, 0);
            fe_cur_m = (fe_enum_ty->kind == TYPE_INTERFACE)
                ? resolve_iface_overload(fe_enum_ty->sym, ci, 0, 0)
                : resolve_overload(fe_enum_ty->sym, ci, 0, 0);
            if (!fe_cur_m) {
                for (int i = 0; i < fe_enum_ty->sym->member_count &&
                                 !fe_cur_getter; i++) {
                    zan_symbol_t *m = fe_enum_ty->sym->members[i];
                    if (m && m->kind == SYM_PROPERTY && member_name_is(m, ci))
                        fe_cur_getter = property_getter_sym(g, m);
                }
            }
        }

        zan_type_t *elem_type = NULL;
        if (stmt->foreach_stmt.var_type)
            elem_type = resolve_type_ctx(g, stmt->foreach_stmt.var_type);
        if ((!elem_type || elem_type->kind == TYPE_ERROR) && fe_cur_ty)
            elem_type = fe_cur_ty;
        if (!elem_type || elem_type->kind == TYPE_ERROR)
            elem_type = container_elem_type(col_type0);
        if (!elem_type) elem_type = g->binder->type_int;
        LLVMTypeRef elem_llvm = map_type(g, elem_type);

        zan_type_t *col_type = col_type0;
        bool fe_array = col_type && col_type->kind == TYPE_ARRAY;
        bool fe_string = col_type && col_type->kind == TYPE_STRING;
        LLVMValueRef fe_len = NULL;
        if (fe_array) {
            fe_len = zan_array_len(g, collection);
        } else if (fe_string) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef sp = LLVMBuildBitCast(g->builder, collection, i8ptr, "fe.sp");
            fe_len = emit_string_length(g, sp, stmt->loc);
        }

        LLVMValueRef col_slot = NULL, idx_alloc = NULL, iter_alloc = NULL;
        LLVMValueRef enum_slot = NULL, enum_alloc = NULL, snap_slot = NULL;
        if (g->current_async_frame) {
            if (fe_enum_ty) {
                local_var_t *ev2 = local_find_async_role(locals, stmt,
                    ASYNC_FOREACH_ENUMERATOR);
                if (ev2) enum_slot = ev2->alloca;
                if (!enum_slot)
                    zan_diag_emit_code(g->diag, DIAG_ERROR, stmt->loc, 3079,
                                  "foreach protocol enumerator missing an "
                                  "async frame slot");
            }
            local_var_t *cv = local_find_async_role(locals, stmt,
                ASYNC_FOREACH_COLLECTION);
            if (cv) col_slot = cv->alloca;
            local_var_t *iv = local_find_async_role(locals, stmt,
                ASYNC_FOREACH_INDEX);
            if (iv) idx_alloc = iv->alloca;
            local_var_t *sv = local_find_async_role(locals, stmt,
                ASYNC_FOREACH_SNAP);
            if (sv) snap_slot = sv->alloca;
            local_var_t *ev = local_find_async_decl(locals, stmt);
            if (ev && !ev->type && LLVMGetTypeKind(elem_llvm) ==
                    LLVMGetTypeKind(local_slot_type(g, ev)))
                iter_alloc = ev->alloca;
        }
        if (col_slot)
            zan_store_fit(g,
                LLVMBuildBitCast(g->builder, collection,
                    LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0), "fe.colp"),
                col_slot);

        LLVMTypeRef fe_enum_ll = fe_enum_ty ? map_type(g, fe_enum_ty) : NULL;
        if (fe_enum_ty) {
            if (!enum_slot) enum_alloc = emit_entry_alloca(g, fe_enum_ll, "fe.enum");
            LLVMValueRef ev0 = (col_type0->kind == TYPE_INTERFACE)
                ? emit_iface_call0(g, col_type0, "GetEnumerator", collection,
                                   locals, stmt->loc)
                : emit_foreach_call0(g, col_type0, fe_get_m, collection);
            zan_store_fit(g, ev0, enum_slot ? enum_slot : enum_alloc);
        }

        bool fe_coll_registered = false;
        {
            zan_type_t *coll_t = infer_expr_type(g,
                stmt->foreach_stmt.collection, locals);
            if (collection && coll_t && is_rc_managed_type(coll_t) &&
                LLVMGetTypeKind(LLVMTypeOf(collection)) ==
                    LLVMPointerTypeKind &&
                !expr_is_local_ident(stmt->foreach_stmt.collection, locals) &&
                expr_yields_owned_rc_value(g, stmt->foreach_stmt.collection,
                                           locals)) {
                LLVMValueRef cslot = col_slot;
                if (!cslot) {
                    cslot = emit_entry_alloca(g, LLVMTypeOf(collection),
                                              "fe.coll");
                    zan_store_fit(g, collection, cslot);
                }
                char cn2[32];
                snprintf(cn2, sizeof(cn2), "$fe.coll");
                zan_istr_t cni = { cn2, (uint32_t)strlen(cn2) };
                local_add(locals, cni, cslot, coll_t);
                arc_own_local(g, locals);
                fe_coll_registered = true;
            }
            if (fe_enum_ty) {
                char enm2[32];
                snprintf(enm2, sizeof(enm2), "$fe.enum");
                zan_istr_t eni = { enm2, (uint32_t)strlen(enm2) };
                local_add(locals, eni, enum_slot ? enum_slot : enum_alloc,
                          fe_enum_ty);
                arc_own_local(g, locals);
            }
        }

        int fe_start = locals->count;
        if (!idx_alloc) idx_alloc = emit_entry_alloca(g, i64, "fi");
        zan_store_fit(g, LLVMConstInt(i64, 0, 0), idx_alloc);

        if (!iter_alloc) iter_alloc = emit_entry_alloca(g, elem_llvm, "fv");
        local_add(locals, stmt->foreach_stmt.var_name, iter_alloc, elem_type);
        int fe_body_start = locals->count;
        if (fe_enum_ty && elem_type && is_rc_managed_type(elem_type))
            zan_store_fit(g, LLVMConstNull(elem_llvm), iter_alloc);

        LLVMTypeRef list_ptr_ty = LLVMTypeOf(collection);
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fe.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fe.body");
        LLVMBasicBlockRef step_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fe.step");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fe.end");

        zan_irgen_pending_context_t fe_saved_pending = g->pending;
        LLVMBasicBlockRef fe_saved_break = g->break_target;
        LLVMBasicBlockRef fe_saved_cont = g->continue_target;
        int fe_saved_loop_base = g->loop_locals_base;
        int fe_saved_break_base = g->break_locals_base;
        int fe_saved_loop_cbase = g->loop_catch_base;
        int fe_saved_loop_fbase = g->finally_loop_base;
        int fe_saved_loop_ehbase = g->eh_armed_loop_base;
        g->pending.break_scope = g->pending.scope;
        g->pending.continue_scope = g->pending.scope;
        g->break_target = end_bb;
        g->continue_target = step_bb;
        g->loop_locals_base = fe_start;
        g->break_locals_base = fe_start;
        g->loop_catch_base = g->catch_cleanup_count;
        g->finally_loop_base = g->finally_count;
        g->eh_armed_loop_base = g->eh_armed_count;

        /* 计数快照（A521）：C# 在枚举中改动集合抛 InvalidOperationException。
         * 先把起始计数拍下来，cond 处检测活计数是否变化——变了就软诊断一次
         * 并终止枚举（--strict-runtime 下 exit 70）。曾每次迭代重读计数：
         * Add 跟着长（死循环）、Remove 后索引跳元素。 */
        LLVMValueRef fe_snap_alloc = NULL;
        if (!fe_enum_ty) {
            fe_snap_alloc = snap_slot ? snap_slot : emit_entry_alloca(g, i64, "fe.snap");
            LLVMValueRef col0 = col_slot
                ? LLVMBuildBitCast(g->builder,
                      LLVMBuildLoad2(g->builder,
                          LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                          col_slot, "fe.col"),
                      list_ptr_ty, "fe.col0")
                : collection;
            LLVMValueRef c0;
            if (fe_array || fe_string) {
                c0 = fe_len;
            } else {
                LLVMValueRef cnt_ptr0 = LLVMBuildStructGEP2(g->builder,
                    g->list_struct_type, col0, 0, "cnt_ptr");
                c0 = LLVMBuildLoad2(g->builder, i64, cnt_ptr0, "cnt0");
            }
            zan_store_fit(g, c0, fe_snap_alloc);
        }

        LLVMBuildBr(g->builder, cond_bb);
        LLVMPositionBuilderAtEnd(g->builder, cond_bb);

        if (fe_enum_ty) {
            LLVMValueRef fev = enum_slot
                ? LLVMBuildBitCast(g->builder,
                      LLVMBuildLoad2(g->builder,
                          LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                          enum_slot, "fe.ens"),
                      fe_enum_ll, "fe.env")
                : LLVMBuildLoad2(g->builder, fe_enum_ll, enum_alloc, "fe.env");
            LLVMValueRef mr = (fe_enum_ty->kind == TYPE_INTERFACE)
                ? emit_iface_call0(g, fe_enum_ty, "MoveNext", fev, locals,
                                   stmt->loc)
                : emit_foreach_call0(g, fe_enum_ty, fe_next_m, fev);
            LLVMTypeRef mrt = LLVMTypeOf(mr);
            LLVMValueRef more =
                (LLVMGetTypeKind(mrt) == LLVMIntegerTypeKind &&
                 LLVMGetIntTypeWidth(mrt) == 1)
                    ? mr
                    : zan_icmp(g->builder, LLVMIntNE, mr,
                               LLVMConstNull(mrt), "fe.more");
            LLVMBuildCondBr(g->builder, more, body_bb, end_bb);

            LLVMPositionBuilderAtEnd(g->builder, body_bb);
            fev = enum_slot
                ? LLVMBuildBitCast(g->builder,
                      LLVMBuildLoad2(g->builder,
                          LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                          enum_slot, "fe.bes"),
                      fe_enum_ll, "fe.bev")
                : LLVMBuildLoad2(g->builder, fe_enum_ll, enum_alloc, "fe.bev");
            LLVMValueRef cur;
            if (fe_cur_m)
                cur = (fe_enum_ty->kind == TYPE_INTERFACE)
                    ? emit_iface_call0(g, fe_enum_ty, "Current", fev, locals,
                                       stmt->loc)
                    : emit_foreach_call0(g, fe_enum_ty, fe_cur_m, fev);
            else
                cur = emit_property_getter_call(g, fe_cur_getter, fe_enum_ty,
                                                fev, NULL, locals);
            if (cur && elem_type &&
                LLVMGetTypeKind(LLVMTypeOf(cur)) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(LLVMTypeOf(cur)) == 64 &&
                LLVMGetTypeKind(elem_llvm) != LLVMIntegerTypeKind) {
                LLVMValueRef cslot = emit_entry_alloca(g,
                    LLVMTypeOf(cur), "fe.curs");
                zan_store_fit(g, cur, cslot);
                cur = load_collection_slot_value(g, elem_type, cslot);
            }
            zan_store_fit(g, cur, iter_alloc);

            emit_stmt(g, stmt->foreach_stmt.body, locals);
            emit_release_owned_locals_from(g, locals, fe_body_start);
            if (elem_type && is_rc_managed_type(elem_type)) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef cv = LLVMBuildLoad2(g->builder, i8ptr, iter_alloc, "fe.cur_rel");
                emit_rc_release_for_type(g, elem_type, cv);
                LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), iter_alloc);
            }
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                LLVMBuildBr(g->builder, step_bb);

            LLVMPositionBuilderAtEnd(g->builder, step_bb);
            LLVMBuildBr(g->builder, cond_bb);

            g->pending.break_scope = fe_saved_pending.break_scope;
            g->pending.continue_scope = fe_saved_pending.continue_scope;
            g->break_target = fe_saved_break;
            g->continue_target = fe_saved_cont;
            g->loop_locals_base = fe_saved_loop_base;
            g->break_locals_base = fe_saved_break_base;
            g->loop_catch_base = fe_saved_loop_cbase;
            g->finally_loop_base = fe_saved_loop_fbase;
            g->eh_armed_loop_base = fe_saved_loop_ehbase;
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            if (elem_type && is_rc_managed_type(elem_type)) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef cv = LLVMBuildLoad2(g->builder, i8ptr, iter_alloc, "fe.cur_end_rel");
                emit_rc_release_for_type(g, elem_type, cv);
                LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), iter_alloc);
            }
            emit_release_owned_locals_from(g, locals, fe_start);
            if (!fe_coll_registered)
                emit_release_owned_call_temp(g, stmt->foreach_stmt.collection,
                                             collection, locals);
            break;
        }

        LLVMValueRef col_cond = col_slot
            ? LLVMBuildBitCast(g->builder,
                  LLVMBuildLoad2(g->builder,
                      LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                      col_slot, "fe.col"),
                  list_ptr_ty, "fe.colc")
            : collection;
        LLVMValueRef count;
        if (fe_array || fe_string) {
            if (col_slot && fe_array) {
                count = zan_array_len(g, col_cond);
            } else if (col_slot) {
                LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef sp2 = LLVMBuildBitCast(g->builder, col_cond, i8p, "fe.sp2");
                count = emit_string_length(g, sp2, stmt->loc);
            } else {
                count = fe_len;
            }
        } else {
            /* A521：活计数 != 快照 → 枚举期间集合被改。软诊断一次后终止
             * 枚举（br end）；--strict-runtime 下 emit_runtime_check 直接
             * exit 70。计数取快照，Add 不再跟随、Remove 不再跳元素。 */
            LLVMValueRef cnt_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                col_cond, 0, "cnt_ptr");
            LLVMValueRef live = LLVMBuildLoad2(g->builder, i64, cnt_ptr, "cnt");
            LLVMValueRef snap = LLVMBuildLoad2(g->builder, i64, fe_snap_alloc,
                                               "fe.snap");
            LLVMValueRef mutated = zan_icmp(g->builder, LLVMIntNE, live, snap,
                                            "fe.mut");
            LLVMBasicBlockRef mut_bb = LLVMAppendBasicBlockInContext(g->ctx, fn,
                                                                     "fe.mut");
            LLVMBasicBlockRef iter_bb = LLVMAppendBasicBlockInContext(g->ctx, fn,
                                                                      "fe.iter");
            LLVMBuildCondBr(g->builder, mutated, mut_bb, iter_bb);
            LLVMPositionBuilderAtEnd(g->builder, mut_bb);
            emit_runtime_check(g,
                LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0), stmt->loc,
                "Collection was modified; enumeration operation may not execute");
            LLVMBuildBr(g->builder, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, iter_bb);
            count = snap;
        }
        LLVMValueRef idx_val = LLVMBuildLoad2(g->builder, i64, idx_alloc, "i");
        LLVMValueRef cmp = zan_icmp(g->builder, LLVMIntSLT, idx_val, count, "fcmp");
        LLVMBuildCondBr(g->builder, cmp, body_bb, end_bb);

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        LLVMValueRef col_body = col_slot
            ? LLVMBuildBitCast(g->builder,
                  LLVMBuildLoad2(g->builder,
                      LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                      col_slot, "fe.col"),
                  list_ptr_ty, "fe.colb")
            : collection;
        if (fe_array || fe_string) {
            LLVMValueRef ai = LLVMBuildLoad2(g->builder, i64, idx_alloc, "ib");
            LLVMTypeRef slot_ty = fe_string
                ? LLVMInt8TypeInContext(g->ctx) : elem_llvm;
            LLVMValueRef ep = LLVMBuildGEP2(g->builder, slot_ty, col_body, &ai, 1, "aep");
            LLVMValueRef av = LLVMBuildLoad2(g->builder, slot_ty, ep, "aelem");
            if (fe_string && slot_ty != elem_llvm)
                av = zan_iwiden(g->builder, av, elem_llvm);
            zan_store_fit(g, av, iter_alloc);

            emit_stmt(g, stmt->foreach_stmt.body, locals);
            emit_release_owned_locals_from(g, locals, fe_start);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                LLVMBuildBr(g->builder, step_bb);

            LLVMPositionBuilderAtEnd(g->builder, step_bb);
            zan_store_fit(g, zan_add(g->builder,
                LLVMBuildLoad2(g->builder, i64, idx_alloc, "i2"),
                LLVMConstInt(i64, 1, 0), "next"), idx_alloc);
            LLVMBuildBr(g->builder, cond_bb);

            g->pending.break_scope = fe_saved_pending.break_scope;
            g->pending.continue_scope = fe_saved_pending.continue_scope;
            g->break_target = fe_saved_break;
            g->continue_target = fe_saved_cont;
            g->loop_locals_base = fe_saved_loop_base;
            g->break_locals_base = fe_saved_break_base;
            g->loop_catch_base = fe_saved_loop_cbase;
            g->finally_loop_base = fe_saved_loop_fbase;
            g->eh_armed_loop_base = fe_saved_loop_ehbase;
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            emit_release_owned_locals_from(g, locals, fe_start);
            if (!fe_coll_registered) {
                LLVMValueRef col_end = col_slot
                    ? LLVMBuildBitCast(g->builder,
                          LLVMBuildLoad2(g->builder,
                              LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                              col_slot, "fe.col"),
                          list_ptr_ty, "fe.cole")
                    : collection;
                emit_release_owned_call_temp(g, stmt->foreach_stmt.collection,
                                             col_end, locals);
            }
            break;
        }
        LLVMValueRef data_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
            col_body, 2, "data_ptr");
        LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0),
            data_ptr, "data");
        LLVMValueRef idx_body = LLVMBuildLoad2(g->builder, i64, idx_alloc, "ib");
        LLVMValueRef fe_widx = slot_word_index(g, idx_body,
            elem_slot_words(g, elem_type));
        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &fe_widx, 1, "ep");
        LLVMTypeKind ek = LLVMGetTypeKind(elem_llvm);
        LLVMValueRef elem = (ek == LLVMStructTypeKind)
            ? load_struct_from_slot(g, elem_ptr, elem_llvm)
            : LLVMBuildLoad2(g->builder, i64, elem_ptr, "elem");
        if (ek == LLVMPointerTypeKind)
            elem = LLVMBuildIntToPtr(g->builder, elem, elem_llvm, "elp");
        else if (ek == LLVMDoubleTypeKind)
            elem = LLVMBuildBitCast(g->builder, elem, elem_llvm, "elf");
        else if (ek == LLVMFloatTypeKind) {
            LLVMValueRef nb = LLVMBuildTrunc(g->builder, elem,
                LLVMInt32TypeInContext(g->ctx), "elf32");
            elem = LLVMBuildBitCast(g->builder, nb, elem_llvm, "elf32b");
        }
        else if (ek == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(elem_llvm) < 64)
            elem = LLVMBuildTrunc(g->builder, elem, elem_llvm, "elt");
        zan_store_fit(g, elem, iter_alloc);

        emit_stmt(g, stmt->foreach_stmt.body, locals);

        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, fe_start);
            LLVMBuildBr(g->builder, step_bb);
        } else {
            emit_release_owned_locals_from(g, locals, fe_start);
        }

        LLVMPositionBuilderAtEnd(g->builder, step_bb);
        LLVMValueRef next = zan_add(g->builder,
            LLVMBuildLoad2(g->builder, i64, idx_alloc, "i2"),
            LLVMConstInt(i64, 1, 0), "next");
        zan_store_fit(g, next, idx_alloc);
        LLVMBuildBr(g->builder, cond_bb);

        g->pending.break_scope = fe_saved_pending.break_scope;
        g->pending.continue_scope = fe_saved_pending.continue_scope;
        g->break_target = fe_saved_break;
        g->continue_target = fe_saved_cont;
        g->loop_locals_base = fe_saved_loop_base;
        g->break_locals_base = fe_saved_break_base;
        g->loop_catch_base = fe_saved_loop_cbase;
        g->finally_loop_base = fe_saved_loop_fbase;
        g->eh_armed_loop_base = fe_saved_loop_ehbase;

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        emit_release_owned_locals_from(g, locals, fe_start);
        if (!fe_coll_registered) {
            LLVMValueRef col_end = col_slot
                ? LLVMBuildBitCast(g->builder,
                      LLVMBuildLoad2(g->builder,
                          LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                          col_slot, "fe.col"),
                      list_ptr_ty, "fe.cole")
                : collection;
            emit_release_owned_call_temp(g, stmt->foreach_stmt.collection,
                                         col_end, locals);
        }
        break;
    }
