
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 3 && memcmp(method_name.str, "Add", 3) == 0 &&
                expr->call.args.count == 1) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    int recv_own = emit_intrinsic_own_recv(g, lobj, raw_ptr, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    zan_type_t *add_et = container_elem_type(ltype);
                    /* 先求值实参再检查容量/取数据指针：符合求值顺序且杜绝实参重入
                     * 改动列表导致的二次 count/list_data_for 加载；扩容外提至冷助手。 */
                    LLVMValueRef val = emit_expr(g, expr->call.args.items[0], locals);
                    LLVMValueRef data0 = list_data_for(g, list_ptr, add_et, "d0");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    zan_tbaa_tag_cnt(g, count);
                    LLVMValueRef cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 1, "capp");
                    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, cap_ptr, "cap");
                    zan_tbaa_tag_meta(g, cap);
                    LLVMValueRef need_grow = zan_icmp(g->builder, LLVMIntUGE, count, cap, "grow");
                    LLVMBasicBlockRef pre_bb = LLVMGetInsertBlock(g->builder);
                    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "list.grow");
                    LLVMBasicBlockRef add_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "list.add");
                    LLVMValueRef grow_br = LLVMBuildCondBr(g->builder, need_grow, grow_bb, add_bb);
                    zan_set_branch_weights(g, grow_br, 1, 2000);
                    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
                    LLVMValueRef grow_fn = get_list_grow_fn(g);
                    LLVMBuildCall2(g->builder, LLVMGlobalGetValueType(grow_fn), grow_fn,
                        (LLVMValueRef[]){ list_ptr,
                            LLVMConstInt(i64, (unsigned long long)list_elem_bytes(g, add_et), 0) },
                        2, "");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data1 = LLVMBuildLoad2(g->builder, i64ptr, data_field, "d1");
                    zan_tbaa_tag_meta(g, data1);
                    LLVMBuildBr(g->builder, add_bb);
                    LLVMPositionBuilderAtEnd(g->builder, add_bb);
                    LLVMValueRef data = LLVMBuildPhi(g->builder, i64ptr, "d");
                    LLVMAddIncoming(data, (LLVMValueRef[]){ data0, data1 },
                        (LLVMBasicBlockRef[]){ pre_bb, grow_bb }, 2);
                    LLVMValueRef elem_ptr = list_elem_ptr(g, data, count, add_et, "ep");
                    emit_collection_slot_store(g, add_et, list_slot_type(g, add_et), elem_ptr,
                        val, expr->call.args.items[0], locals, 0);
                    LLVMValueRef new_count = zan_add(g->builder, count, LLVMConstInt(i64, 1, 0), "nc");
                    zan_tbaa_tag_cnt(g, LLVMBuildStore(g->builder, new_count, count_ptr));
                    emit_intrinsic_drop_recv(g, lobj, raw_ptr, locals, recv_own);
                    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                }
            }
        }

        /* List */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 7 && memcmp(method_name.str, "Reserve", 7) == 0 &&
                expr->call.args.count == 1) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    int recv_own = emit_intrinsic_own_recv(g, lobj, raw_ptr, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    zan_type_t *rsv_et = container_elem_type(ltype);
                    (void)list_data_for(g, list_ptr, rsv_et, "d0");
                    LLVMValueRef cap_ptr = LLVMBuildStructGEP2(g->builder,
                        g->list_struct_type, list_ptr, 1, "capp");
                    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, cap_ptr, "cap");
                    LLVMValueRef want = coerce_int_to(g,
                        emit_expr(g, expr->call.args.items[0], locals), i64);
                    LLVMValueRef need_grow = zan_icmp(g->builder, LLVMIntSGT,
                        want, cap, "grow");
                    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, g->current_fn, "list.reserve");
                    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, g->current_fn, "list.reserved");
                    LLVMBuildCondBr(g->builder, need_grow, grow_bb, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder,
                        g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef old_data = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(i64, 0), data_field, "od");
                    LLVMValueRef old_data_raw = LLVMBuildBitCast(g->builder,
                        old_data, i8ptr, "odr");
                    LLVMValueRef new_size = zan_mul(g->builder, want,
                        LLVMConstInt(i64, (unsigned long long)list_elem_bytes(g, rsv_et), 0), "nsz");
                    LLVMValueRef realloc_args[] = { old_data_raw, new_size };
                    LLVMValueRef new_data_raw = zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
                        g->fn_realloc, realloc_args, 2, "nd");
                    zan_irgen_emit_oom_check(g, g->current_fn, new_data_raw);
                    LLVMValueRef new_data = LLVMBuildBitCast(g->builder, new_data_raw,
                        LLVMPointerType(i64, 0), "ndt");
                    LLVMBuildStore(g->builder, new_data, data_field);
                    LLVMBuildStore(g->builder, want, cap_ptr);
                    LLVMBuildBr(g->builder, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, done_bb);
                    emit_intrinsic_drop_recv(g, lobj, raw_ptr, locals, recv_own);
                    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 8 && memcmp(method_name.str, "AddRange", 8) == 0 &&
                expr->call.args.count == 1) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
                    LLVMTypeRef list_pt = LLVMPointerType(g->list_struct_type, 0);
                    zan_type_t *elem_type = container_elem_type(ltype);
                    LLVMTypeRef elem_llvm = elem_type ? map_type(g, elem_type) : i64;
                    LLVMValueRef self_raw = emit_expr(g, lobj, locals);
                    LLVMValueRef self_ptr = LLVMBuildBitCast(g->builder, self_raw, list_pt, "ar.self");
                    zan_type_t *otype = infer_expr_type(g, expr->call.args.items[0], locals);
                    int src_array = otype && otype->kind == TYPE_ARRAY &&
                                    otype->array_rank <= 1;
                    LLVMTypeRef src_elem_llvm = elem_llvm;
                    if (src_array)
                        src_elem_llvm = otype->element_type
                            ? map_type(g, otype->element_type) : i64;
                    LLVMValueRef other_raw = emit_expr(g, expr->call.args.items[0], locals);
                    int source_own = emit_call_arg_eh_push(g,
                        expr->call.args.items[0], other_raw, locals);
                    LLVMValueRef other_ptr = LLVMBuildBitCast(g->builder, other_raw, list_pt, "ar.other");
                    LLVMValueRef other_i = LLVMBuildPtrToInt(g->builder, other_ptr, i64, "ar.oi");
                    LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, other_i,
                        LLVMConstInt(i64, 0, 0), "ar.null");
                    LLVMBasicBlockRef ar_body0 = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.enter");
                    LLVMBasicBlockRef ar_done = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.done");
                    LLVMBuildCondBr(g->builder, is_null, ar_done, ar_body0);
                    LLVMPositionBuilderAtEnd(g->builder, ar_body0);
                    LLVMValueRef ocnt;
                    if (src_array) {
                        ocnt = zan_array_len(g, other_raw);
                    } else {
                        LLVMValueRef ocnt_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, other_ptr, 0, "ar.ocp");
                        ocnt = LLVMBuildLoad2(g->builder, i64, ocnt_ptr, "ar.ocnt");
                    }
                    LLVMBasicBlockRef ar_go = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.go");
                    LLVMBuildCondBr(g->builder,
                        zan_icmp(g->builder, LLVMIntSGT, ocnt, LLVMConstInt(i64, 0, 0), "ar.pos"),
                        ar_go, ar_done);
                    LLVMPositionBuilderAtEnd(g->builder, ar_go);
                    /* 一次性确保目标槽宽与容量 s_cnt + ocnt（含 list.AddRange(list) 自追加安全） */
                    (void)list_data_for(g, self_ptr, elem_type, "ar.sd0");
                    LLVMValueRef s_cnt_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, self_ptr, 0, "ar.scp");
                    LLVMValueRef s_cnt0 = LLVMBuildLoad2(g->builder, i64, s_cnt_ptr, "ar.sc0");
                    LLVMValueRef need_cap = zan_add(g->builder, s_cnt0, ocnt, "ar.need");
                    unsigned eb = list_elem_bytes(g, elem_type);
                    LLVMValueRef rsv_fn = get_list_reserve_fn(g);
                    LLVMBuildCall2(g->builder, LLVMGlobalGetValueType(rsv_fn), rsv_fn,
                        (LLVMValueRef[]){ self_ptr, need_cap,
                            LLVMConstInt(i64, (unsigned long long)eb, 0) }, 3, "");
                    LLVMValueRef s_df = LLVMBuildStructGEP2(g->builder, g->list_struct_type, self_ptr, 2, "ar.sdf");
                    LLVMValueRef s_data = LLVMBuildLoad2(g->builder, i64ptr, s_df, "ar.sd");

                    bool no_rc = elem_type && !type_contains_collection_rc(g, elem_type, 0);
                    bool can_memcpy = false;
                    if (no_rc) {
                        if (!src_array) {
                            zan_type_t *o_et = container_elem_type(otype);
                            if (o_et &&
                                list_elem_bytes(g, elem_type) == list_elem_bytes(g, o_et) &&
                                list_elem_tag(g, elem_type) == list_elem_tag(g, o_et)) {
                                can_memcpy = true;
                            }
                        } else if (otype->element_type && elem_type->kind == otype->element_type->kind) {
                            switch (elem_type->kind) {
                            case TYPE_BYTE: case TYPE_SBYTE:
                            case TYPE_SHORT: case TYPE_USHORT:
                            case TYPE_INT: case TYPE_UINT:
                            case TYPE_LONG: case TYPE_ULONG:
                            case TYPE_DOUBLE:
                                can_memcpy = true;
                                break;
                            default:
                                break;
                            }
                        }
                    }

                    if (can_memcpy) {
                        LLVMValueRef src_raw_p;
                        if (src_array) {
                            src_raw_p = LLVMBuildBitCast(g->builder, other_raw, i8ptr, "ar.src8");
                        } else {
                            zan_type_t *o_et = container_elem_type(otype);
                            LLVMValueRef odata = list_data_for(g, other_ptr, o_et, "ar.od");
                            src_raw_p = LLVMBuildBitCast(g->builder, odata, i8ptr, "ar.src8");
                        }
                        LLVMValueRef dst_slot = list_elem_ptr(g, s_data, s_cnt0, elem_type, "ar.dsts");
                        LLVMValueRef dst_raw_p = LLVMBuildBitCast(g->builder, dst_slot, i8ptr, "ar.dst8");
                        LLVMValueRef nbytes = zan_mul(g->builder, ocnt,
                            LLVMConstInt(i64, (unsigned long long)eb, 0), "ar.nb");
                        LLVMTypeRef mc_ty = LLVMFunctionType(i8ptr,
                            (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
                        zan_call2(g->builder, mc_ty, get_libc_fn(g, "memcpy", mc_ty),
                            (LLVMValueRef[]){ dst_raw_p, src_raw_p, nbytes }, 3, "");
                        LLVMBuildStore(g->builder, need_cap, s_cnt_ptr);
                        LLVMBuildBr(g->builder, ar_done);
                    } else {
                        LLVMValueRef odata_hoist = NULL;
                        zan_type_t *o_et = src_array ? NULL : container_elem_type(otype);
                        if (!src_array)
                            odata_hoist = list_data_for(g, other_ptr, o_et, "ar.od");
                        LLVMBasicBlockRef ar_loop_pre = LLVMGetInsertBlock(g->builder);
                        LLVMBasicBlockRef c_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.cond");
                        LLVMBuildBr(g->builder, c_bb);
                        LLVMPositionBuilderAtEnd(g->builder, c_bb);
                        LLVMValueRef ci = LLVMBuildPhi(g->builder, i64, "ar.ci");
                        LLVMValueRef rawv;
                        if (src_array) {
                            LLVMValueRef typed = LLVMBuildBitCast(g->builder, other_raw,
                                LLVMPointerType(src_elem_llvm, 0), "ar.ap");
                            LLVMValueRef ep = LLVMBuildInBoundsGEP2(g->builder, src_elem_llvm,
                                typed, &ci, 1, "ar.aep");
                            rawv = LLVMBuildLoad2(g->builder, src_elem_llvm, ep, "ar.av");
                            switch (LLVMGetTypeKind(src_elem_llvm)) {
                            case LLVMStructTypeKind:
                                break;
                            case LLVMPointerTypeKind:
                                rawv = LLVMBuildPtrToInt(g->builder, rawv, i64, "ar.api");
                                break;
                            case LLVMDoubleTypeKind:
                            case LLVMFloatTypeKind:
                                if (LLVMGetTypeKind(src_elem_llvm) == LLVMFloatTypeKind)
                                    rawv = LLVMBuildFPExt(g->builder, rawv,
                                        LLVMDoubleTypeInContext(g->ctx), "ar.afx");
                                rawv = LLVMBuildBitCast(g->builder, rawv, i64, "ar.afb");
                                break;
                            default:
                                if (LLVMGetIntTypeWidth(LLVMTypeOf(rawv)) < 64)
                                    rawv = extend_int_for_slot(g, rawv,
                                        otype->element_type, i64);
                                break;
                            }
                        } else {
                            LLVMValueRef oslot = list_elem_ptr(g, odata_hoist, ci, o_et, "ar.os");
                            rawv = LLVMGetTypeKind(elem_llvm) == LLVMStructTypeKind
                                ? load_struct_from_slot(g, oslot, elem_llvm)
                                : list_slot_load_word(g, oslot, o_et, "ar.rv");
                        }
                        LLVMValueRef dst_i = zan_add(g->builder, s_cnt0, ci, "ar.di");
                        LLVMValueRef s_slot = list_elem_ptr(g, s_data, dst_i, elem_type, "ar.ss");
                        if (LLVMGetTypeKind(elem_llvm) == LLVMStructTypeKind)
                            store_struct_in_slot(g, rawv, s_slot, expr);
                        else
                            list_slot_store_word(g, rawv, s_slot, elem_type);
                        if (is_rc_managed_type(elem_type)) {
                            LLVMTypeRef mt = map_type(g, elem_type);
                            if (LLVMGetTypeKind(mt) == LLVMPointerTypeKind) {
                                LLVMValueRef pv = LLVMBuildIntToPtr(g->builder, rawv, mt, "ar.pv");
                                emit_rc_retain_for_type(g, elem_type, pv);
                            }
                        }
                        LLVMValueRef ni = zan_add(g->builder, ci, LLVMConstInt(i64, 1, 0), "ar.ni");
                        LLVMBasicBlockRef c_end = LLVMGetInsertBlock(g->builder);
                        LLVMBasicBlockRef ar_fin = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.fin");
                        LLVMBuildCondBr(g->builder,
                            zan_icmp(g->builder, LLVMIntULT, ni, ocnt, "ar.more"),
                            c_bb, ar_fin);
                        LLVMAddIncoming(ci, (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), ni },
                            (LLVMBasicBlockRef[]){ ar_loop_pre, c_end }, 2);
                        LLVMPositionBuilderAtEnd(g->builder, ar_fin);
                        LLVMBuildStore(g->builder, need_cap, s_cnt_ptr);
                        LLVMBuildBr(g->builder, ar_done);
                    }
                    LLVMPositionBuilderAtEnd(g->builder, ar_done);
                    if (source_own) emit_eh_tmp_pop(g);
                    emit_release_owned_call_temp(g, expr->call.args.items[0],
                        other_raw, locals);
                    return LLVMConstInt(i32t, 0, 0);
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 5 && memcmp(method_name.str, "Clear", 5) == 0 &&
                expr->call.args.count == 0) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    int recv_own = emit_intrinsic_own_recv(g, lobj, raw_ptr, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    zan_type_t *clr_et = container_elem_type(ltype);
                    if (clr_et && !type_contains_collection_rc(g, clr_et, 0)) {
                        /* 无 RC 托管元素（全标量及值结构体）：O(1) 归零计数，免 O(N) 扫描 */
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), count_ptr);
                        emit_intrinsic_drop_recv(g, lobj, raw_ptr, locals, recv_own);
                        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                    }
                    LLVMValueRef data = list_data_for(g, list_ptr, clr_et, "data");
                    LLVMValueRef idx_a = emit_entry_alloca(g, i64, "lc");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                    LLVMBasicBlockRef c_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "lc.cond");
                    LLVMBasicBlockRef b_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "lc.body");
                    LLVMBasicBlockRef d_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "lc.done");
                    LLVMBuildBr(g->builder, c_bb);
                    LLVMPositionBuilderAtEnd(g->builder, c_bb);
                    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                    LLVMValueRef more = zan_icmp(g->builder, LLVMIntULT, ci, count, "more");
                    LLVMBuildCondBr(g->builder, more, b_bb, d_bb);
                    LLVMPositionBuilderAtEnd(g->builder, b_bb);
                    LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                    LLVMValueRef cw = slot_word_index(g, ci2,
                        elem_slot_words(g, container_elem_type(ltype)));
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &cw, 1, "sl");
                    zan_type_t *elem_type = container_elem_type(ltype);
                    LLVMValueRef val = load_collection_slot_value(g, elem_type, slot);
                    emit_collection_release_raw_slot(g, elem_type, val, i64);
                    LLVMValueRef ni = zan_add(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                    LLVMBuildStore(g->builder, ni, idx_a);
                    LLVMBuildBr(g->builder, c_bb);
                    LLVMPositionBuilderAtEnd(g->builder, d_bb);
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), count_ptr);
                    emit_intrinsic_drop_recv(g, lobj, raw_ptr, locals, recv_own);
                    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 8 && memcmp(method_name.str, "RemoveAt", 8) == 0 &&
                expr->call.args.count == 1) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    int recv_own = emit_intrinsic_own_recv(g, lobj, raw_ptr, locals);
                    LLVMValueRef idx = emit_expr(g, expr->call.args.items[0], locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                        idx = LLVMBuildSExt(g->builder, idx, i64, "idx.sx");
                    }
                    /* Index evaluation may resize or clear this same list.
                     * Read its current count and buffer only after it returns. */
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    zan_type_t *removed_type = container_elem_type(ltype);
                    LLVMValueRef data = list_data_for(g, list_ptr, removed_type, "data");
                    idx = emit_index_safe_bounds(g, idx, count, expr->loc, "list");
                    LLVMValueRef removed_ptr = list_elem_ptr(g, data, idx, removed_type, "rmp");
                    if (!list_elem_tag(g, removed_type)) {
                        LLVMValueRef removed = load_collection_slot_value(g,
                            removed_type, removed_ptr);
                        emit_collection_release_raw_slot(g, removed_type, removed, i64);
                    }
                    /* 其后元素整体前移一格（按字节 memmove，任意槽宽通用），末槽清零 */
                    LLVMValueRef esz_b = LLVMConstInt(i64,
                        (unsigned long long)list_elem_bytes(g, removed_type), 0);
                    LLVMValueRef last = zan_sub(g->builder, count, LLVMConstInt(i64, 1, 0), "last");
                    LLVMValueRef tail_n = zan_mul(g->builder,
                        zan_sub(g->builder, last, idx, "ra.n"), esz_b, "ra.bytes");
                    LLVMValueRef dst8 = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                        data, (LLVMValueRef[]){ zan_mul(g->builder, idx, esz_b, "ra.do") }, 1, "ra.dst");
                    LLVMValueRef src8 = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                        data, (LLVMValueRef[]){ zan_mul(g->builder,
                            zan_add(g->builder, idx, LLVMConstInt(i64, 1, 0), "ra.i1"),
                            esz_b, "ra.so") }, 1, "ra.src");
                    LLVMBuildMemMove(g->builder, dst8, 1, src8, 1, tail_n);
                    LLVMBuildStore(g->builder, last, count_ptr);
                    LLVMValueRef tail8 = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                        data, (LLVMValueRef[]){ zan_mul(g->builder, last, esz_b, "ra.to") }, 1, "ra.tail");
                    LLVMBuildMemSet(g->builder, tail8,
                        LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0), esz_b, 1);
                    (void)i8ptr;
                    emit_intrinsic_drop_recv(g, lobj, raw_ptr, locals, recv_own);
                    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                }
            }
        }
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 7 && memcmp(method_name.str, "IndexOf", 7) == 0 &&
                expr->call.args.count == 1) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    int recv_own = emit_intrinsic_own_recv(g, lobj, raw_ptr, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    zan_type_t *elem_type = concretize(g,
                        container_elem_type(ltype));
                    LLVMValueRef data = list_data_for(g, list_ptr, elem_type, "data");
                    LLVMValueRef search = emit_expr(g, expr->call.args.items[0], locals);
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
                    LLVMValueRef cmp_d = zan_icmp(g->builder, LLVMIntUGE, ci, count, "cdone");
                    LLVMBuildCondBr(g->builder, cmp_d, done_bb, body_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                    LLVMValueRef slot = list_elem_ptr(g, data, ci2, elem_type, "sl");
                    LLVMValueRef val = list_load_value(g, elem_type, slot);
                    LLVMValueRef eq = emit_typed_equality(
                        g, elem_type, val, search);
                    LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.found");
                    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.next");
                    LLVMBuildCondBr(g->builder, eq, found_bb, next_bb);
                    LLVMPositionBuilderAtEnd(g->builder, found_bb);
                    LLVMBuildStore(g->builder, ci2, res);
                    LLVMBuildBr(g->builder, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, next_bb);
                    LLVMValueRef ni = zan_add(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                    LLVMBuildStore(g->builder, ni, idx_a);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, done_bb);
                    emit_release_owned_call_temp(g, expr->call.args.items[0],
                                                 search, locals);
                    emit_intrinsic_drop_recv(g, lobj, raw_ptr, locals, recv_own);
                    return LLVMBuildLoad2(g->builder, i64, res, "iofres");
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 11 && memcmp(method_name.str, "LastIndexOf", 11) == 0 &&
                expr->call.args.count == 1) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    int recv_own = emit_intrinsic_own_recv(g, lobj, raw_ptr, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    zan_type_t *elem_type = concretize(g,
                        container_elem_type(ltype));
                    LLVMValueRef data = list_data_for(g, list_ptr, elem_type, "data");
                    LLVMValueRef search = emit_expr(g, expr->call.args.items[0], locals);
                    LLVMValueRef res = emit_entry_alloca(g, i64, "liofr");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, (uint64_t)-1LL, 1), res);
                    LLVMValueRef idx_a = emit_entry_alloca(g, i64, "lii");
                    LLVMValueRef count_sub1 = zan_sub(g->builder, count, LLVMConstInt(i64, 1, 0), "cnt.m1");
                    LLVMBuildStore(g->builder, count_sub1, idx_a);
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "lio.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "lio.body");
                    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "lio.done");
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                    LLVMValueRef in_bounds = zan_icmp(g->builder, LLVMIntSGE, ci, LLVMConstInt(i64, 0, 0), "cinb");
                    LLVMBuildCondBr(g->builder, in_bounds, body_bb, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                    LLVMValueRef slot = list_elem_ptr(g, data, ci2, elem_type, "sl");
                    LLVMValueRef val = list_load_value(g, elem_type, slot);
                    LLVMValueRef eq = emit_typed_equality(
                        g, elem_type, val, search);
                    LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "lio.found");
                    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "lio.next");
                    LLVMBuildCondBr(g->builder, eq, found_bb, next_bb);
                    LLVMPositionBuilderAtEnd(g->builder, found_bb);
                    LLVMBuildStore(g->builder, ci2, res);
                    LLVMBuildBr(g->builder, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, next_bb);
                    LLVMValueRef ni = zan_sub(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                    LLVMBuildStore(g->builder, ni, idx_a);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, done_bb);
                    emit_release_owned_call_temp(g, expr->call.args.items[0],
                                                 search, locals);
                    emit_intrinsic_drop_recv(g, lobj, raw_ptr, locals, recv_own);
                    return LLVMBuildLoad2(g->builder, i64, res, "liofres");
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 8 && memcmp(method_name.str, "Contains", 8) == 0 &&
                expr->call.args.count == 1) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    int recv_own = emit_intrinsic_own_recv(g, lobj, raw_ptr, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    zan_type_t *elem_type = concretize(g,
                        container_elem_type(ltype));
                    LLVMValueRef data = list_data_for(g, list_ptr, elem_type, "data");
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
                    LLVMValueRef cmp_d = zan_icmp(g->builder, LLVMIntUGE, ci, count, "cdone");
                    LLVMBuildCondBr(g->builder, cmp_d, done_bb, body_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                    LLVMValueRef slot = list_elem_ptr(g, data, ci2, elem_type, "sl");
                    LLVMValueRef val = list_load_value(g, elem_type, slot);
                    LLVMValueRef eq = emit_typed_equality(
                        g, elem_type, val, search);
                    LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ct.found");
                    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ct.next");
                    LLVMBuildCondBr(g->builder, eq, found_bb, next_bb);
                    LLVMPositionBuilderAtEnd(g->builder, found_bb);
                    LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1, 0), res);
                    LLVMBuildBr(g->builder, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, next_bb);
                    LLVMValueRef ni = zan_add(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                    LLVMBuildStore(g->builder, ni, idx_a);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, done_bb);
                    emit_release_owned_call_temp(g, expr->call.args.items[0],
                                                 search, locals);
                    emit_intrinsic_drop_recv(g, lobj, raw_ptr, locals, recv_own);
                    return LLVMBuildLoad2(g->builder, LLVMInt32TypeInContext(g->ctx), res, "ctres");
                }
            }
        }

        /* List */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 6 && memcmp(method_name.str, "Insert", 6) == 0 &&
                expr->call.args.count == 2) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    zan_type_t *elem_type = concretize(g,
                        container_elem_type(ltype));
                    /* 先过宽度守卫：改宽会换缓冲并调整 cap */
                    LLVMValueRef data = list_data_for(g, list_ptr, elem_type, "data");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 1, "capp");
                    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, cap_ptr, "cap");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef esz_b = LLVMConstInt(i64,
                        (unsigned long long)list_elem_bytes(g, elem_type), 0);
                    LLVMValueRef idx = coerce_int_to(g,
                        emit_expr(g, expr->call.args.items[0], locals), i64);
                    idx = emit_index_safe_check(g, idx, count, true,
                                                expr->loc, "list");
                    LLVMValueRef item = emit_expr(g, expr->call.args.items[1], locals);
                    LLVMValueRef need = zan_icmp(g->builder, LLVMIntUGE, count, cap, "need");
                    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.grow");
                    LLVMBasicBlockRef shift_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.shift");
                    LLVMBuildCondBr(g->builder, need, grow_bb, shift_bb);
                    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
                    LLVMValueRef cap_zero = zan_icmp(g->builder, LLVMIntEQ, cap,
                        LLVMConstInt(i64, 0, 0), "ins.cap.zero");
                    LLVMValueRef dbl_cap = zan_mul(g->builder, cap, LLVMConstInt(i64, 2, 0), "nc");
                    LLVMValueRef new_cap = LLVMBuildSelect(g->builder, cap_zero,
                        LLVMConstInt(i64, 8, 0), dbl_cap, "new_cap");
                    LLVMBuildStore(g->builder, new_cap, cap_ptr);
                    LLVMValueRef nbytes = zan_mul(g->builder, new_cap,
                        esz_b, "nb");
                    LLVMValueRef old_data = LLVMBuildBitCast(g->builder, data, i8ptr, "od");
                    LLVMValueRef new_data = zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
                        g->fn_realloc, (LLVMValueRef[]){ old_data, nbytes }, 2, "nd");
                    zan_irgen_emit_oom_check(g, g->current_fn, new_data);
                    LLVMValueRef new_data_i = LLVMBuildBitCast(g->builder, new_data, LLVMPointerType(i64, 0), "ndi");
                    LLVMBuildStore(g->builder, new_data_i, data_field);
                    LLVMBuildBr(g->builder, shift_bb);
                    LLVMPositionBuilderAtEnd(g->builder, shift_bb);
                    LLVMValueRef phi_data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "phid");
                    /* [idx, count) 整体后移一格（字节 memmove，任意槽宽通用） */
                    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
                    LLVMValueRef ins_src = LLVMBuildGEP2(g->builder, i8t, phi_data,
                        (LLVMValueRef[]){ zan_mul(g->builder, idx, esz_b, "ins.so") }, 1, "ins.src");
                    LLVMValueRef ins_dst = LLVMBuildGEP2(g->builder, i8t, phi_data,
                        (LLVMValueRef[]){ zan_mul(g->builder,
                            zan_add(g->builder, idx, LLVMConstInt(i64, 1, 0), "ins.i1"),
                            esz_b, "ins.do") }, 1, "ins.dst");
                    LLVMBuildMemMove(g->builder, ins_dst, 1, ins_src, 1,
                        zan_mul(g->builder, zan_sub(g->builder, count, idx, "ins.n"),
                                esz_b, "ins.bytes"));
                    LLVMValueRef ins_slot = list_elem_ptr(g, phi_data, idx, elem_type, "is");
                    emit_collection_slot_store(g, elem_type, list_slot_type(g, elem_type), ins_slot,
                        item, expr->call.args.items[1], locals, 0);
                    LLVMValueRef nc = zan_add(g->builder, count, LLVMConstInt(i64, 1, 0), "nc");
                    LLVMBuildStore(g->builder, nc, count_ptr);
                    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 7 && memcmp(method_name.str, "Reverse", 7) == 0 &&
                expr->call.args.count == 0) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    zan_type_t *elem_type = concretize(g,
                        container_elem_type(ltype));
                    LLVMValueRef data = list_data_for(g, list_ptr, elem_type, "data");
                    unsigned rv_words = elem_slot_words(g, elem_type);
                    LLVMValueRef lo_a = emit_entry_alloca(g, i64, "lo");
                    LLVMValueRef hi_a = emit_entry_alloca(g, i64, "hi");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), lo_a);
                    LLVMValueRef count_zero = zan_icmp(g->builder, LLVMIntEQ,
                        count, LLVMConstInt(i64, 0, 0), "rv.empty");
                    LLVMValueRef hi_last = zan_sub(g->builder, count,
                        LLVMConstInt(i64, 1, 0), "hi.last");
                    LLVMValueRef hi_init = LLVMBuildSelect(g->builder, count_zero,
                        LLVMConstInt(i64, 0, 0), hi_last, "hi");
                    LLVMBuildStore(g->builder, hi_init, hi_a);
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rv.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rv.body");
                    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rv.done");
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef lo = LLVMBuildLoad2(g->builder, i64, lo_a, "lo");
                    LLVMValueRef hi = LLVMBuildLoad2(g->builder, i64, hi_a, "hi");
                    LLVMValueRef cmp = zan_icmp(g->builder, LLVMIntULT, lo, hi, "cmp");
                    LLVMBuildCondBr(g->builder, cmp, body_bb, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    LLVMValueRef lo2 = LLVMBuildLoad2(g->builder, i64, lo_a, "lo2");
                    LLVMValueRef hi2 = LLVMBuildLoad2(g->builder, i64, hi_a, "hi2");
                    LLVMValueRef lo_word = slot_word_index(g, lo2, rv_words);
                    LLVMValueRef hi_word = slot_word_index(g, hi2, rv_words);
                    if (list_elem_tag(g, elem_type)) {
                        LLVMTypeRef nt = list_slot_type(g, elem_type);
                        LLVMValueRef lo_slot = list_elem_ptr(g, data, lo2, elem_type, "ls");
                        LLVMValueRef hi_slot = list_elem_ptr(g, data, hi2, elem_type, "hs");
                        LLVMValueRef lv = LLVMBuildLoad2(g->builder, nt, lo_slot, "lv");
                        LLVMValueRef hv = LLVMBuildLoad2(g->builder, nt, hi_slot, "hv");
                        LLVMBuildStore(g->builder, hv, lo_slot);
                        LLVMBuildStore(g->builder, lv, hi_slot);
                    } else
                    for (unsigned wi = 0; wi < rv_words; wi++) {
                        LLVMValueRef off = LLVMConstInt(i64, wi, 0);
                        LLVMValueRef li = zan_add(g->builder, lo_word, off, "rv.lw");
                        LLVMValueRef hi = zan_add(g->builder, hi_word, off, "rv.hw");
                        LLVMValueRef lo_slot = LLVMBuildGEP2(g->builder, i64, data, &li, 1, "ls");
                        LLVMValueRef hi_slot = LLVMBuildGEP2(g->builder, i64, data, &hi, 1, "hs");
                        LLVMValueRef lv = LLVMBuildLoad2(g->builder, i64, lo_slot, "lv");
                        LLVMValueRef hv = LLVMBuildLoad2(g->builder, i64, hi_slot, "hv");
                        LLVMBuildStore(g->builder, hv, lo_slot);
                        LLVMBuildStore(g->builder, lv, hi_slot);
                    }
                    LLVMBuildStore(g->builder, zan_add(g->builder, lo2, LLVMConstInt(i64, 1, 0), "lo3"), lo_a);
                    LLVMBuildStore(g->builder, zan_sub(g->builder, hi2, LLVMConstInt(i64, 1, 0), "hi3"), hi_a);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, done_bb);
                    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                }
            }
            /* 原生零 ARC / 零重排检查 / 零越界检查的原始缓冲内省排序：
             * 覆盖 List<int/long/double/string>.Sort() 与单字元素的 List<T>.Sort(Comparison<T>) */
            if (method_name.len == 4 && memcmp(method_name.str, "Sort", 4) == 0 &&
                (expr->call.args.count == 0 || expr->call.args.count == 1)) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    zan_type_t *elem_type = concretize(g, container_elem_type(ltype));
                    int sort_kind = -1;
                    if (elem_type) {
                        if (expr->call.args.count == 0) {
                            if (elem_type->kind == TYPE_INT) sort_kind = ZAN_SORT_I32;
                            else if (elem_type->kind == TYPE_LONG) sort_kind = ZAN_SORT_I64;
                            else if (elem_type->kind == TYPE_DOUBLE) sort_kind = ZAN_SORT_F64;
                            else if (elem_type->kind == TYPE_STRING) sort_kind = ZAN_SORT_STR;
                        } else if (elem_slot_words(g, elem_type) == 1) {
                            if (elem_type->kind == TYPE_INT) sort_kind = ZAN_SORT_CMP_I32;
                            else if (elem_type->kind == TYPE_LONG) sort_kind = ZAN_SORT_CMP_I64;
                            else if (elem_type->kind == TYPE_DOUBLE) sort_kind = ZAN_SORT_CMP_F64;
                            else if (elem_type->kind == TYPE_STRING ||
                                     elem_type->kind == TYPE_CLASS ||
                                     elem_type->kind == TYPE_INTERFACE)
                                sort_kind = ZAN_SORT_CMP_PTR;
                        }
                    }
                    if (sort_kind >= 0) {
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                        int recv_own = emit_intrinsic_own_recv(g, lobj, raw_ptr, locals);
                        LLVMValueRef cmp_val = NULL;
                        int cmp_eh = 0;
                        if (expr->call.args.count == 1) {
                            zan_type_t cmp_dt;
                            memset(&cmp_dt, 0, sizeof(cmp_dt));
                            cmp_dt.kind = TYPE_DELEGATE;
                            zan_type_t *cmp_pt[2] = { elem_type, elem_type };
                            cmp_dt.delegate_param_types = cmp_pt;
                            cmp_dt.delegate_param_count = 2;
                            cmp_dt.delegate_ret_type = g->binder->type_int;
                            cmp_val = emit_arg_typed(g, expr->call.args.items[0], &cmp_dt, locals);
                            if (LLVMTypeOf(cmp_val) != i8ptr)
                                cmp_val = LLVMBuildBitCast(g->builder, cmp_val, i8ptr, "sort.cmp");
                            if (!g->current_fn_no_runtime)
                                cmp_eh = emit_call_arg_eh_push(g, expr->call.args.items[0], cmp_val, locals);
                        }
                        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
                        LLVMBasicBlockRef go_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "lsort.go");
                        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "lsort.end");
                        LLVMValueRef is_null = LLVMBuildICmp(g->builder, LLVMIntEQ,
                            raw_ptr, LLVMConstNull(LLVMTypeOf(raw_ptr)), "lsort.null");
                        LLVMBuildCondBr(g->builder, is_null, end_bb, go_bb);
                        LLVMPositionBuilderAtEnd(g->builder, go_bb);
                        LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                            LLVMPointerType(g->list_struct_type, 0), "lptr");
                        LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder,
                            g->list_struct_type, list_ptr, 0, "cntp");
                        LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                        LLVMValueRef data = list_data_for(g, list_ptr, elem_type, "data");
                        emit_raw_buffer_sort(g, sort_kind, data, count, cmp_val);
                        LLVMBuildBr(g->builder, end_bb);
                        LLVMPositionBuilderAtEnd(g->builder, end_bb);
                        if (cmp_eh) emit_eh_tmp_pop(g);
                        if (expr->call.args.count == 1)
                            emit_release_owned_call_temp(g, expr->call.args.items[0], cmp_val, locals);
                        if (recv_own) emit_release_obj_value(g, raw_ptr);
                        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                    }
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 7 && memcmp(method_name.str, "ToArray", 7) == 0 &&
                expr->call.args.count == 0) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    int recv_own = emit_intrinsic_own_recv(g, lobj, raw_ptr, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64,
                        LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                            list_ptr, 0, "cntp"), "cnt");
                    zan_type_t *elem_type = concretize(g,
                        container_elem_type(ltype));
                    LLVMValueRef data = list_data_for(g, list_ptr, elem_type, "data");
                    LLVMTypeRef elem_llvm = elem_type ? map_type(g, elem_type) : i64;
                    LLVMValueRef total = zan_mul(g->builder, count,
                        LLVMSizeOf(elem_llvm), "ta.total");
                    bool exact_scalar = false;
                    if (elem_type && !type_contains_collection_rc(g, elem_type, 0)) {
                        switch (elem_type->kind) {
                        case TYPE_BYTE: case TYPE_SBYTE:
                        case TYPE_SHORT: case TYPE_USHORT:
                        case TYPE_INT: case TYPE_UINT:
                        case TYPE_LONG: case TYPE_ULONG:
                        case TYPE_DOUBLE:
                            exact_scalar = true;
                            break;
                        default:
                            break;
                        }
                    }
                    if (exact_scalar) {
                        LLVMValueRef arr = zan_array_alloc_uninit_typed(g, total, count, elem_type);
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMBasicBlockRef cp_bb = LLVMAppendBasicBlockInContext(
                            g->ctx, g->current_fn, "ta.cp");
                        LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(
                            g->ctx, g->current_fn, "ta.done");
                        LLVMBuildCondBr(g->builder,
                            zan_icmp(g->builder, LLVMIntSGT, count,
                                LLVMConstInt(i64, 0, 0), "ta.pos"),
                            cp_bb, done_bb);
                        LLVMPositionBuilderAtEnd(g->builder, cp_bb);
                        LLVMTypeRef mc_ty = LLVMFunctionType(i8ptr,
                            (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
                        zan_call2(g->builder, mc_ty, get_libc_fn(g, "memcpy", mc_ty),
                            (LLVMValueRef[]){
                                LLVMBuildBitCast(g->builder, arr, i8ptr, "ta.dst8"),
                                LLVMBuildBitCast(g->builder, data, i8ptr, "ta.src8"),
                                total }, 3, "");
                        LLVMBuildBr(g->builder, done_bb);
                        LLVMPositionBuilderAtEnd(g->builder, done_bb);
                        emit_intrinsic_drop_recv(g, lobj, raw_ptr, locals, recv_own);
                        return arr;
                    }
                    LLVMValueRef arr = zan_array_alloc_typed(g, total, count, elem_type);
                    LLVMValueRef arrp = LLVMBuildBitCast(g->builder, arr,
                        LLVMPointerType(elem_llvm, 0), "ta.ap");
                    LLVMValueRef idx_a = emit_entry_alloca(g, i64, "ta.i");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, g->current_fn, "ta.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, g->current_fn, "ta.body");
                    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, g->current_fn, "ta.done");
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef i = LLVMBuildLoad2(g->builder, i64, idx_a, "ta.iv");
                    LLVMBuildCondBr(g->builder,
                        zan_icmp(g->builder, LLVMIntULT, i, count, "ta.lt"),
                        body_bb, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    LLVMValueRef i2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ta.i2");
                    LLVMValueRef slot = list_elem_ptr(g, data, i2, elem_type, "ta.slot");
                    LLVMValueRef val = list_load_value(g, elem_type, slot);
                    emit_collection_value_retain(g, elem_type, val, 0);
                    LLVMValueRef aep = LLVMBuildGEP2(g->builder, elem_llvm, arrp,
                        &i2, 1, "ta.aep");
                    LLVMBuildStore(g->builder,
                        emit_boundary_coerce(g, val, elem_llvm), aep);
                    LLVMBuildStore(g->builder,
                        zan_add(g->builder, i2, LLVMConstInt(i64, 1, 0), "ta.ni"),
                        idx_a);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, done_bb);
                    emit_intrinsic_drop_recv(g, lobj, raw_ptr, locals, recv_own);
                    return arr;
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee_d = expr->call.callee;
            zan_istr_t mname = callee_d->member.name;
            zan_type_t *dict_type = infer_expr_type(g, callee_d->member.object, locals);
            if (dict_type && type_named(dict_type, "Dict", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw = emit_expr(g, callee_d->member.object, locals);
                    LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                        LLVMPointerType(g->dict_struct_type, 0), "dp");

                    if (mname.len == 3 && memcmp(mname.str, "Add", 3) == 0 && expr->call.args.count == 2) {
                        zan_type_t *key_type = dict_key_type(g, dict_type);
                        LLVMValueRef key_val = coerce_dict_key(g,
                            emit_expr(g, expr->call.args.items[0], locals), key_type);
                        LLVMValueRef val_v = emit_expr(g, expr->call.args.items[1], locals);
                        emit_dict_value_set(g, dict_type, raw, key_val, val_v,
                            expr->call.args.items[0], expr->call.args.items[1], locals);
                        emit_release_owned_call_temp(g, callee_d->member.object, raw, locals);
                        return LLVMConstInt(i32t, 0, 0);
                    }

                    if (mname.len == 11 && memcmp(mname.str, "ContainsKey", 11) == 0 && expr->call.args.count == 1) {
                        LLVMValueRef search = coerce_dict_key(g, emit_expr(g, expr->call.args.items[0], locals), dict_key_type(g, dict_type));
                        LLVMValueRef found = emit_dict_find(g, dict_type, raw, search);
                        LLVMValueRef hit = zan_icmp(g->builder, LLVMIntSGE, found,
                            LLVMConstInt(i64, 0, 0), "ckhit");
                        emit_release_owned_call_temp(g, expr->call.args.items[0], search, locals);
                        emit_release_owned_call_temp(g, callee_d->member.object, raw, locals);
                        return LLVMBuildZExt(g->builder, hit, i32t, "ckres");
                    }
                    if (mname.len == 11 && memcmp(mname.str, "TryGetValue", 11) == 0 && expr->call.args.count == 2) {
                        LLVMValueRef search = coerce_dict_key(g,
                            emit_expr(g, expr->call.args.items[0], locals),
                            dict_key_type(g, dict_type));
                        LLVMValueRef found = emit_dict_find(g, dict_type, raw, search);
                        LLVMValueRef hit = zan_icmp(g->builder, LLVMIntSGE, found,
                            LLVMConstInt(i64, 0, 0), "tgv.hit");
                        zan_ast_node_t *out_arg = expr->call.args.items[1];
                        LLVMValueRef out_ptr = NULL;
                        if (out_arg->kind == AST_REF_ARG) {
                            out_ptr = emit_expr(g, out_arg, locals);
                        } else if (out_arg->kind == AST_IDENTIFIER) {
                            local_var_t *ol = local_find(locals, out_arg->ident.name);
                            if (ol) { out_ptr = ol->alloca; }
                        }
                        zan_type_t *value_type = dict_value_type(dict_type);
                        LLVMTypeRef value_llvm = value_type ? map_type(g, value_type) : i64;
                        LLVMValueRef res_a = emit_entry_alloca(g, i32t, "tgv.res");
                        LLVMBasicBlockRef hit_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "tgv.hitbb");
                        LLVMBasicBlockRef miss_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "tgv.miss");
                        LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "tgv.done");
                        LLVMBuildCondBr(g->builder, hit, hit_bb, miss_bb);
                        LLVMPositionBuilderAtEnd(g->builder, hit_bb);
                        LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
                        LLVMValueRef vs = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp, "vs");
                        bool tgv_vw1 = (value_type && !type_mentions_canon(value_type) &&
                                        elem_slot_words(g, value_type) == 1);
                        LLVMValueRef word = tgv_vw1
                            ? found
                            : zan_mul(g->builder, found,
                                load_dict_value_words(g, raw), "tgv.word");
                        LLVMValueRef vslot = LLVMBuildInBoundsGEP2(g->builder, i64, vs,
                            &word, 1, "tgv.vsl");
                        LLVMValueRef vv = load_collection_slot_value(g, value_type, vslot);
                        if (out_ptr) {
                            emit_typed_out_store(g, value_type, out_ptr, vv);
                        }
                        LLVMBuildStore(g->builder, LLVMConstInt(i32t, 1, 0), res_a);
                        LLVMBuildBr(g->builder, done_bb);
                        LLVMPositionBuilderAtEnd(g->builder, miss_bb);
                        /* 显式 `out` 实参未命中按 C# 语义置 default(TValue)——曾
                         * 原样留下未初始化的局部，`TryGetValue(k, out v);
                         * d[k] = v + 1` 计数写法读到栈垃圾。不带 out 的裸标识符
                         * 写法保持旧约定（未命中不动原值，case_tryget_pinyin 钉住）。 */
                        if (out_ptr && value_type && out_arg->kind == AST_REF_ARG &&
                            out_arg->ref_arg.is_out)
                            emit_typed_out_store(g, value_type, out_ptr, LLVMConstNull(value_llvm));
                        LLVMBuildStore(g->builder, LLVMConstInt(i32t, 0, 0), res_a);
                        LLVMBuildBr(g->builder, done_bb);
                        LLVMPositionBuilderAtEnd(g->builder, done_bb);
                        emit_release_owned_call_temp(g, expr->call.args.items[0], search, locals);
                        emit_release_owned_call_temp(g, callee_d->member.object, raw, locals);
                        return LLVMBuildLoad2(g->builder, i32t, res_a, "tgv.out");
                    }
                    if (mname.len == 5 && memcmp(mname.str, "Clear", 5) == 0 && expr->call.args.count == 0) {
                        LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                        zan_type_t *key_type = dict_key_type(g, dict_type);
                        zan_type_t *value_type = dict_value_type(dict_type);
                        bool rc_k = !key_type || type_contains_collection_rc(g, key_type, 0);
                        bool rc_v = !value_type || type_contains_collection_rc(g, value_type, 0);
                        LLVMValueRef cnt_clr = LLVMBuildLoad2(g->builder, i64, cntp, "cnt.clr");
                        LLVMValueRef hp_clr = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 8, "dc.hp");
                        LLVMValueRef dp_clr = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 9, "dc.dp");
                        LLVMValueRef holes_clr = LLVMBuildLoad2(g->builder, i64, hp_clr, "dc.holes");
                        LLVMValueRef dead_clr = LLVMBuildLoad2(g->builder, i8ptr, dp_clr, "dc.dead");
                        LLVMValueRef has_holes_clr = zan_and(g->builder,
                            zan_icmp(g->builder, LLVMIntNE, holes_clr, LLVMConstInt(i64, 0, 0), "dc.hh1"),
                            zan_icmp(g->builder, LLVMIntNE, dead_clr, LLVMConstNull(i8ptr), "dc.hh2"), "dc.hh");
                        if (rc_k || rc_v) {
                            LLVMValueRef cnt = cnt_clr;
                            LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
                            LLVMValueRef ks = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), kp, "ks");
                            LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
                            LLVMValueRef vs = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp, "vs");
                            bool dc_vw1 = (value_type && !type_mentions_canon(value_type) &&
                                           elem_slot_words(g, value_type) == 1);
                            LLVMValueRef value_words = (rc_v && !dc_vw1) ? load_dict_value_words(g, raw) : NULL;
                            LLVMBasicBlockRef pre_bb = LLVMGetInsertBlock(g->builder);
                            LLVMBasicBlockRef cbb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.cond");
                            LLVMBasicBlockRef bbb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.body");
                            LLVMBasicBlockRef chkd_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.chkd");
                            LLVMBasicBlockRef rel_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.rel");
                            LLVMBasicBlockRef step_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.step");
                            LLVMBasicBlockRef dbb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.done");
                            LLVMBuildBr(g->builder, cbb);
                            LLVMPositionBuilderAtEnd(g->builder, cbb);
                            LLVMValueRef ci = LLVMBuildPhi(g->builder, i64, "ci");
                            LLVMValueRef more = zan_icmp(g->builder, LLVMIntULT, ci, cnt, "more");
                            LLVMBuildCondBr(g->builder, more, bbb, dbb);
                            LLVMPositionBuilderAtEnd(g->builder, bbb);
                            LLVMBuildCondBr(g->builder, has_holes_clr, chkd_bb, rel_bb);
                            LLVMPositionBuilderAtEnd(g->builder, chkd_bb);
                            LLVMValueRef db = LLVMBuildLoad2(g->builder, LLVMInt8TypeInContext(g->ctx),
                                LLVMBuildInBoundsGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                                    dead_clr, &ci, 1, "dc.dbp"), "dc.db");
                            LLVMBuildCondBr(g->builder,
                                LLVMBuildICmp(g->builder, LLVMIntNE, db,
                                    LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0), "dc.isd"),
                                step_bb, rel_bb);
                            LLVMPositionBuilderAtEnd(g->builder, rel_bb);
                            if (rc_k) {
                                LLVMValueRef kslot = LLVMBuildInBoundsGEP2(g->builder, i8ptr, ks, &ci, 1, "ksl");
                                LLVMValueRef kv = LLVMBuildLoad2(g->builder, i8ptr, kslot, "kv");
                                emit_collection_release_raw_slot(g, key_type, kv, i8ptr);
                            }
                            if (rc_v) {
                                LLVMValueRef value_word = dc_vw1
                                    ? ci
                                    : zan_mul(g->builder, ci, value_words, "dc.word");
                                LLVMValueRef vslot = LLVMBuildInBoundsGEP2(g->builder, i64, vs,
                                    &value_word, 1, "vsl");
                                LLVMValueRef vv = load_collection_slot_value(g, value_type, vslot);
                                emit_collection_release_raw_slot(g, value_type, vv, i64);
                            }
                            LLVMBuildBr(g->builder, step_bb);
                            LLVMPositionBuilderAtEnd(g->builder, step_bb);
                            LLVMValueRef ni = zan_add(g->builder, ci, LLVMConstInt(i64, 1, 0), "ni");
                            LLVMBasicBlockRef b_end = LLVMGetInsertBlock(g->builder);
                            LLVMBuildBr(g->builder, cbb);
                            LLVMAddIncoming(ci, (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), ni },
                                (LLVMBasicBlockRef[]){ pre_bb, b_end }, 2);
                            LLVMPositionBuilderAtEnd(g->builder, dbb);
                        }
                        /* 若有空洞，清空 dead 标记并归零 holes（无空洞时跳过 memset） */
                        LLVMBasicBlockRef zdead_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.zdead");
                        LLVMBasicBlockRef zdone_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.zdone");
                        LLVMBuildCondBr(g->builder, has_holes_clr, zdead_bb, zdone_bb);
                        LLVMPositionBuilderAtEnd(g->builder, zdead_bb);
                        LLVMTypeRef zms_ty = LLVMFunctionType(i8ptr,
                            (LLVMTypeRef[]){ i8ptr, i32t, i64 }, 3, 0);
                        zan_call2(g->builder, zms_ty, get_libc_fn(g, "memset", zms_ty),
                            (LLVMValueRef[]){ dead_clr, LLVMConstInt(i32t, 0, 0), cnt_clr }, 3, "");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), hp_clr);
                        LLVMBuildBr(g->builder, zdone_bb);
                        LLVMPositionBuilderAtEnd(g->builder, zdone_bb);
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), cntp);
                        /* 若已有中小尺寸 SwissTable 索引（icap <= 65536），就地 memset 复位
                         * 控制字节与墓碑计数并保留槽表，避免 Clear+重填循环反复 malloc/重建。 */
                        LLVMValueRef ixp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 4, "dc.ixp");
                        LLVMValueRef icapp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 5, "dc.icapp");
                        LLVMValueRef icntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 6, "dc.icntp");
                        LLVMValueRef d_ix = LLVMBuildLoad2(g->builder, i8ptr, ixp, "dc.ix");
                        LLVMValueRef d_icap = LLVMBuildLoad2(g->builder, i64, icapp, "dc.icap");
                        LLVMValueRef keep_ix = zan_and(g->builder,
                            zan_icmp(g->builder, LLVMIntNE, d_ix, LLVMConstNull(i8ptr), "dc.hasix"),
                            zan_and(g->builder,
                                zan_icmp(g->builder, LLVMIntSGT, d_icap, LLVMConstInt(i64, 0, 0), "dc.pos"),
                                zan_icmp(g->builder, LLVMIntSLE, d_icap, LLVMConstInt(i64, 65536, 0), "dc.small"),
                                "dc.rng"),
                            "dc.keep");
                        LLVMBasicBlockRef keep_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.keepix");
                        LLVMBasicBlockRef drop_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.dropix");
                        LLVMBasicBlockRef fin_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.fin");
                        LLVMBuildCondBr(g->builder, keep_ix, keep_bb, drop_bb);
                        LLVMPositionBuilderAtEnd(g->builder, keep_bb);
                        LLVMTypeRef ms_ty = LLVMFunctionType(i8ptr,
                            (LLVMTypeRef[]){ i8ptr, i32t, i64 }, 3, 0);
                        LLVMValueRef ctrl_sz = zan_add(g->builder, d_icap, LLVMConstInt(i64, 16, 0), "dc.csz");
                        zan_call2(g->builder, ms_ty, get_libc_fn(g, "memset", ms_ty),
                            (LLVMValueRef[]){ d_ix, LLVMConstInt(i32t, 0x80, 0), ctrl_sz }, 3, "");
                        LLVMValueRef tomb_off = zan_add(g->builder,
                            zan_mul(g->builder, d_icap, LLVMConstInt(i64, 5, 0), "dc.c5"),
                            LLVMConstInt(i64, 16, 0), "dc.toff");
                        LLVMValueRef tomb_p = LLVMBuildBitCast(g->builder,
                            LLVMBuildInBoundsGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                                d_ix, &tomb_off, 1, "dc.tp8"),
                            LLVMPointerType(i64, 0), "dc.tp");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), tomb_p);
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), icntp);
                        LLVMBuildBr(g->builder, fin_bb);
                        LLVMPositionBuilderAtEnd(g->builder, drop_bb);
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), icapp);
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, (unsigned long long)-1, 1), icntp);
                        LLVMBuildBr(g->builder, fin_bb);
                        LLVMPositionBuilderAtEnd(g->builder, fin_bb);
                        emit_release_owned_call_temp(g, callee_d->member.object, raw, locals);
                        return LLVMConstInt(i32t, 0, 0);
                    }
                }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee_d = expr->call.callee;
            zan_istr_t mname = callee_d->member.name;
            zan_type_t *dict_type = infer_expr_type(g, callee_d->member.object, locals);
            if (mname.len == 6 && memcmp(mname.str, "Remove", 6) == 0 && expr->call.args.count == 1 &&
                dict_type && type_named(dict_type, "Dict", 4)) {
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMValueRef raw = emit_expr(g, callee_d->member.object, locals);
                        LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                            LLVMPointerType(g->dict_struct_type, 0), "dp");
                        LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                        LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
                        LLVMValueRef hp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 8, "hp");
                        LLVMValueRef holes = LLVMBuildLoad2(g->builder, i64, hp, "holes");
                        LLVMValueRef live_cnt = zan_sub(g->builder, cnt, holes, "dr.live");
                        LLVMValueRef search = coerce_dict_key(g, emit_expr(g, expr->call.args.items[0], locals), dict_key_type(g, dict_type));
                        zan_type_t *value_type = dict_value_type(dict_type);
                        bool dr_vw1 = (value_type && !type_mentions_canon(value_type) &&
                                       elem_slot_words(g, value_type) == 1);
                        LLVMValueRef value_words = dr_vw1
                            ? LLVMConstInt(i64, 1, 0)
                            : load_dict_value_words(g, raw);
                        LLVMValueRef res_a = emit_entry_alloca(g, i32t, "dr.res");
                        LLVMBuildStore(g->builder, LLVMConstInt(i32t, 0, 0), res_a);
                        LLVMBasicBlockRef miss_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.miss");
                        LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.done");
                        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.body");
                        LLVMValueRef nonempty = zan_icmp(g->builder, LLVMIntSGT, live_cnt,
                            LLVMConstInt(i64, 0, 0), "dr.nonempty");
                        LLVMBuildCondBr(g->builder, nonempty, body_bb, miss_bb);
                        LLVMPositionBuilderAtEnd(g->builder, body_bb);
                        LLVMValueRef rmfn = get_dict_remove_fn(g);
                        LLVMValueRef is_str = LLVMConstInt(
                            i64, dict_key_hash_kind(dict_key_type(g, dict_type), 0), 0);
                        LLVMValueRef fi = zan_call2(g->builder,
                            LLVMGlobalGetValueType(rmfn), rmfn,
                            (LLVMValueRef[]){ raw, search, is_str }, 3, "dr.rm");
                        LLVMValueRef hit = zan_icmp(g->builder, LLVMIntSGE, fi,
                            LLVMConstInt(i64, 0, 0), "dr.hit");
                        LLVMBasicBlockRef hit_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.hitbb");
                        LLVMBuildCondBr(g->builder, hit, hit_bb, miss_bb);
                        LLVMPositionBuilderAtEnd(g->builder, hit_bb);
                        LLVMValueRef kp0 = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp0");
                        LLVMValueRef ks0 = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), kp0, "ks0");
                        LLVMValueRef rkey = LLVMBuildGEP2(g->builder, i8ptr, ks0, &fi, 1, "rkey");
                        LLVMValueRef rkv = LLVMBuildLoad2(g->builder, i8ptr, rkey, "rkv");
                        emit_collection_release_raw_slot(g, dict_key_type(g, dict_type), rkv, i8ptr);
                        LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), rkey);
                        LLVMValueRef vp0 = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp0");
                        LLVMValueRef vs0 = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp0, "vs0");
                        LLVMValueRef removed_word = dr_vw1
                            ? fi
                            : zan_mul(g->builder, fi, value_words, "dr.word");
                        LLVMValueRef rvslot = LLVMBuildGEP2(g->builder, i64, vs0,
                            &removed_word, 1, "rvslot");
                        LLVMValueRef rv = load_collection_slot_value(g, value_type, rvslot);
                        emit_collection_release_raw_slot(g, value_type, rv, i64);
                        if (dr_vw1) {
                            LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), rvslot);
                        } else {
                            LLVMTypeRef zero_ty = LLVMFunctionType(i8ptr,
                                (LLVMTypeRef[]){ i8ptr, i32t, i64 }, 3, 0);
                            LLVMValueRef memset_fn = get_libc_fn(g, "memset", zero_ty);
                            LLVMValueRef tail_bytes = zan_mul(g->builder, value_words,
                                LLVMConstInt(i64, 8, 0), "tail.bytes");
                            zan_call2(g->builder, zero_ty, memset_fn,
                                (LLVMValueRef[]){ LLVMBuildBitCast(g->builder, rvslot, i8ptr, "tv8"),
                                                  LLVMConstInt(i32t, 0, 0),
                                                  tail_bytes }, 3, "");
                        }
                        LLVMBuildStore(g->builder, LLVMConstInt(i32t, 1, 0), res_a);
                        LLVMBuildBr(g->builder, done_bb);
                        LLVMPositionBuilderAtEnd(g->builder, miss_bb);
                        LLVMBuildBr(g->builder, done_bb);
                        LLVMPositionBuilderAtEnd(g->builder, done_bb);
                        emit_release_owned_call_temp(g, expr->call.args.items[0], search, locals);
                        emit_release_owned_call_temp(g, callee_d->member.object, raw, locals);
                        return LLVMBuildLoad2(g->builder, i32t, res_a, "dr.out");
            }
        }

