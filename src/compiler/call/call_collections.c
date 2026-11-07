
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 3 && memcmp(method_name.str, "Add", 3) == 0 &&
                expr->call.args.count == 1) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    int recv_own = emit_intrinsic_own_recv(g, lobj, raw_ptr, locals);
                    (void)i8ptr;
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 1, "capp");
                    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, cap_ptr, "cap");
                    LLVMValueRef need_grow = zan_icmp(g->builder, LLVMIntUGE, count, cap, "grow");
                    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "list.grow");
                    LLVMBasicBlockRef add_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "list.add");
                    LLVMBuildCondBr(g->builder, need_grow, grow_bb, add_bb);
                    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
                    LLVMValueRef cap_zero = zan_icmp(g->builder, LLVMIntEQ, cap,
                        LLVMConstInt(i64, 0, 0), "cap.zero");
                    LLVMValueRef dbl_cap = zan_mul(g->builder, cap, LLVMConstInt(i64, 2, 0), "ncap");
                    LLVMValueRef new_cap = LLVMBuildSelect(g->builder, cap_zero,
                        LLVMConstInt(i64, 8, 0), dbl_cap, "new_cap");
                    LLVMBuildStore(g->builder, new_cap, cap_ptr);
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef old_data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "od");
                    LLVMValueRef old_data_raw = LLVMBuildBitCast(g->builder, old_data, i8ptr, "odr");
                    unsigned lwords = elem_slot_words(g, container_elem_type(ltype));
                    LLVMValueRef new_size = zan_mul(g->builder, new_cap,
                        LLVMConstInt(i64, (unsigned long long)(8 * lwords), 0), "nsz");
                    LLVMValueRef realloc_args[] = { old_data_raw, new_size };
                    LLVMValueRef new_data_raw = zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
                        g->fn_realloc, realloc_args, 2, "nd");
                    zan_irgen_emit_oom_check(g, g->current_fn, new_data_raw);
                    LLVMValueRef new_data = LLVMBuildBitCast(g->builder, new_data_raw, LLVMPointerType(i64, 0), "ndt");
                    LLVMBuildStore(g->builder, new_data, data_field);
                    LLVMBuildBr(g->builder, add_bb);
                    LLVMPositionBuilderAtEnd(g->builder, add_bb);
                    LLVMValueRef data_field2 = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df2");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field2, "d");
                    LLVMValueRef count2 = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt2");
                    LLVMValueRef wpos = slot_word_index(g, count2, lwords);
                    LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &wpos, 1, "ep");
                    LLVMValueRef val = emit_expr(g, expr->call.args.items[0], locals);
                    emit_collection_slot_store(g, container_elem_type(ltype), i64, elem_ptr,
                        val, expr->call.args.items[0], locals, 0);
                    LLVMValueRef new_count = zan_add(g->builder, count2, LLVMConstInt(i64, 1, 0), "nc");
                    LLVMBuildStore(g->builder, new_count, count_ptr);
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
                    unsigned lwords = elem_slot_words(g, container_elem_type(ltype));
                    LLVMValueRef new_size = zan_mul(g->builder, want,
                        LLVMConstInt(i64, (unsigned long long)(8 * lwords), 0), "nsz");
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
                    unsigned ar_words = elem_slot_words(g, elem_type);
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
                    LLVMValueRef idx_a = emit_entry_alloca(g, i64, "ar.i");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                    LLVMBasicBlockRef c_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.cond");
                    LLVMBasicBlockRef b_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.step");
                    LLVMBuildBr(g->builder, c_bb);
                    LLVMPositionBuilderAtEnd(g->builder, c_bb);
                    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ar.ci");
                    LLVMValueRef more = zan_icmp(g->builder, LLVMIntULT, ci, ocnt, "ar.more");
                    LLVMBuildCondBr(g->builder, more, b_bb, ar_done);
                    LLVMPositionBuilderAtEnd(g->builder, b_bb);
                    LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ar.ci2");
                    LLVMValueRef rawv;
                    if (src_array) {
                        LLVMValueRef typed = LLVMBuildBitCast(g->builder, other_raw,
                            LLVMPointerType(src_elem_llvm, 0), "ar.ap");
                        LLVMValueRef ep = LLVMBuildGEP2(g->builder, src_elem_llvm,
                            typed, &ci2, 1, "ar.aep");
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
                        LLVMValueRef odata_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, other_ptr, 2, "ar.odf");
                        LLVMValueRef odata = LLVMBuildLoad2(g->builder, i64ptr, odata_field, "ar.od");
                        LLVMValueRef owpos = slot_word_index(g, ci2, ar_words);
                        LLVMValueRef oslot = LLVMBuildGEP2(g->builder, i64, odata, &owpos, 1, "ar.os");
                        rawv = LLVMGetTypeKind(elem_llvm) == LLVMStructTypeKind
                            ? load_struct_from_slot(g, oslot, elem_llvm)
                            : LLVMBuildLoad2(g->builder, i64, oslot, "ar.rv");
                    }
                    LLVMValueRef s_cnt_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, self_ptr, 0, "ar.scp");
                    LLVMValueRef s_cnt = LLVMBuildLoad2(g->builder, i64, s_cnt_ptr, "ar.sc");
                    LLVMValueRef s_cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, self_ptr, 1, "ar.scapp");
                    LLVMValueRef s_cap = LLVMBuildLoad2(g->builder, i64, s_cap_ptr, "ar.scap");
                    LLVMValueRef need = zan_icmp(g->builder, LLVMIntUGE, s_cnt, s_cap, "ar.grow");
                    LLVMBasicBlockRef g_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.grow");
                    LLVMBasicBlockRef s_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.store");
                    LLVMBuildCondBr(g->builder, need, g_bb, s_bb);
                    LLVMPositionBuilderAtEnd(g->builder, g_bb);
                    LLVMValueRef cap_zero = zan_icmp(g->builder, LLVMIntEQ, s_cap,
                        LLVMConstInt(i64, 0, 0), "ar.cz");
                    LLVMValueRef dbl = zan_mul(g->builder, s_cap, LLVMConstInt(i64, 2, 0), "ar.dbl");
                    LLVMValueRef ncap = LLVMBuildSelect(g->builder, cap_zero,
                        LLVMConstInt(i64, 4, 0), dbl, "ar.ncap");
                    LLVMBuildStore(g->builder, ncap, s_cap_ptr);
                    LLVMValueRef s_df = LLVMBuildStructGEP2(g->builder, g->list_struct_type, self_ptr, 2, "ar.sdf");
                    LLVMValueRef old_data = LLVMBuildLoad2(g->builder, i64ptr, s_df, "ar.oldd");
                    LLVMValueRef old_raw = LLVMBuildBitCast(g->builder, old_data, i8ptr, "ar.oldr");
                    LLVMValueRef nsz = zan_mul(g->builder, ncap,
                        LLVMConstInt(i64, 8ULL * ar_words, 0), "ar.nsz");
                    LLVMValueRef re_args[] = { old_raw, nsz };
                    LLVMValueRef nd_raw = zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
                        g->fn_realloc, re_args, 2, "ar.nd");
                    zan_irgen_emit_oom_check(g, g->current_fn, nd_raw);
                    LLVMValueRef nd = LLVMBuildBitCast(g->builder, nd_raw, i64ptr, "ar.ndt");
                    LLVMBuildStore(g->builder, nd, s_df);
                    LLVMBuildBr(g->builder, s_bb);
                    LLVMPositionBuilderAtEnd(g->builder, s_bb);
                    LLVMValueRef s_df2 = LLVMBuildStructGEP2(g->builder, g->list_struct_type, self_ptr, 2, "ar.sdf2");
                    LLVMValueRef s_data = LLVMBuildLoad2(g->builder, i64ptr, s_df2, "ar.sd");
                    LLVMValueRef s_cnt2 = LLVMBuildLoad2(g->builder, i64, s_cnt_ptr, "ar.sc2");
                    LLVMValueRef swpos = slot_word_index(g, s_cnt2, ar_words);
                    LLVMValueRef s_slot = LLVMBuildGEP2(g->builder, i64, s_data, &swpos, 1, "ar.ss");
                    if (LLVMGetTypeKind(elem_llvm) == LLVMStructTypeKind)
                        store_struct_in_slot(g, rawv, s_slot, expr);
                    else
                        LLVMBuildStore(g->builder, rawv, s_slot);
                    if (is_rc_managed_type(elem_type)) {
                        LLVMTypeRef mt = map_type(g, elem_type);
                        if (LLVMGetTypeKind(mt) == LLVMPointerTypeKind) {
                            LLVMValueRef pv = LLVMBuildIntToPtr(g->builder, rawv, mt, "ar.pv");
                            emit_rc_retain_for_type(g, elem_type, pv);
                        }
                    }
                    LLVMValueRef s_nc = zan_add(g->builder, s_cnt2, LLVMConstInt(i64, 1, 0), "ar.snc");
                    LLVMBuildStore(g->builder, s_nc, s_cnt_ptr);
                    LLVMValueRef ni = zan_add(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ar.ni");
                    LLVMBuildStore(g->builder, ni, idx_a);
                    LLVMBuildBr(g->builder, c_bb);
                    LLVMPositionBuilderAtEnd(g->builder, ar_done);
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
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
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
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    LLVMValueRef idx = emit_expr(g, expr->call.args.items[0], locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                        idx = LLVMBuildSExt(g->builder, idx, i64, "idx.sx");
                    }
                    emit_index_bounds_check(g, idx, count, expr->loc, "list");
                    idx = emit_index_safe_bounds(g, idx, count, expr->loc, "list");
                    unsigned rwords = elem_slot_words(g, container_elem_type(ltype));
                    LLVMValueRef widx = slot_word_index(g, idx, rwords);
                    LLVMValueRef removed_ptr = LLVMBuildGEP2(g->builder, i64, data, &widx, 1, "rmp");
                    zan_type_t *removed_type = container_elem_type(ltype);
                    LLVMValueRef removed = load_collection_slot_value(g,
                        removed_type, removed_ptr);
                    emit_collection_release_raw_slot(g, removed_type, removed, i64);
                    LLVMValueRef j_a = emit_entry_alloca(g, i64, "j");
                    LLVMBuildStore(g->builder, widx, j_a);
                    LLVMValueRef last = zan_sub(g->builder, count, LLVMConstInt(i64, 1, 0), "last");
                    LLVMValueRef last_w = slot_word_index(g, last, rwords);
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ra.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ra.body");
                    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ra.done");
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef j = LLVMBuildLoad2(g->builder, i64, j_a, "j");
                    LLVMValueRef cmp = zan_icmp(g->builder, LLVMIntULT, j, last_w, "cmp");
                    LLVMBuildCondBr(g->builder, cmp, body_bb, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    LLVMValueRef j2 = LLVMBuildLoad2(g->builder, i64, j_a, "j2");
                    LLVMValueRef next = zan_add(g->builder, j2,
                        LLVMConstInt(i64, rwords, 0), "nxt");
                    LLVMValueRef src_slot = LLVMBuildGEP2(g->builder, i64, data, &next, 1, "ss");
                    LLVMValueRef val = LLVMBuildLoad2(g->builder, i64, src_slot, "sv");
                    LLVMValueRef dst_slot = LLVMBuildGEP2(g->builder, i64, data, &j2, 1, "ds");
                    LLVMBuildStore(g->builder, val, dst_slot);
                    LLVMValueRef j3 = zan_add(g->builder, j2, LLVMConstInt(i64, 1, 0), "j3");
                    LLVMBuildStore(g->builder, j3, j_a);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, done_bb);
                    LLVMBuildStore(g->builder, last, count_ptr);
                    for (unsigned tw = 0; tw < rwords; tw++) {
                        LLVMValueRef tpos = zan_add(g->builder, last_w,
                            LLVMConstInt(i64, tw, 0), "tw");
                        LLVMValueRef tail_ptr = LLVMBuildGEP2(g->builder, i64, data,
                            &tpos, 1, "tail");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), tail_ptr);
                    }
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
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    zan_type_t *elem_type = concretize(g,
                        container_elem_type(ltype));
                    unsigned io_words = elem_slot_words(g, elem_type);
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
                    LLVMValueRef iw = slot_word_index(g, ci2, io_words);
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &iw, 1, "sl");
                    LLVMValueRef val = load_collection_slot_value(
                        g, elem_type, slot);
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
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    zan_type_t *elem_type = concretize(g,
                        container_elem_type(ltype));
                    unsigned lio_words = elem_slot_words(g, elem_type);
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
                    LLVMValueRef iw = slot_word_index(g, ci2, lio_words);
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &iw, 1, "sl");
                    LLVMValueRef val = load_collection_slot_value(
                        g, elem_type, slot);
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
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    zan_type_t *elem_type = concretize(g,
                        container_elem_type(ltype));
                    unsigned ct_words = elem_slot_words(g, elem_type);
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
                    LLVMValueRef cw = slot_word_index(g, ci2, ct_words);
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &cw, 1, "sl");
                    LLVMValueRef val = load_collection_slot_value(
                        g, elem_type, slot);
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
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 1, "capp");
                    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, cap_ptr, "cap");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    zan_type_t *elem_type = concretize(g,
                        container_elem_type(ltype));
                    unsigned ins_words = elem_slot_words(g, elem_type);
                    LLVMValueRef idx = coerce_int_to(g,
                        emit_expr(g, expr->call.args.items[0], locals), i64);
                    emit_index_range_check(g, idx, count, true,
                                           expr->loc, "list");
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
                        LLVMConstInt(i64, 8ULL * ins_words, 0), "nb");
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
                    LLVMValueRef j_a = emit_entry_alloca(g, i64, "ij");
                    LLVMBuildStore(g->builder, count, j_a);
                    LLVMBasicBlockRef scond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.scond");
                    LLVMBasicBlockRef sbody_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.sbody");
                    LLVMBasicBlockRef sdone_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.sdone");
                    LLVMBuildBr(g->builder, scond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, scond_bb);
                    LLVMValueRef j = LLVMBuildLoad2(g->builder, i64, j_a, "j");
                    LLVMValueRef cmp = zan_icmp(g->builder, LLVMIntUGT, j, idx, "cmp");
                    LLVMBuildCondBr(g->builder, cmp, sbody_bb, sdone_bb);
                    LLVMPositionBuilderAtEnd(g->builder, sbody_bb);
                    LLVMValueRef j2 = LLVMBuildLoad2(g->builder, i64, j_a, "j2");
                    LLVMValueRef prev = zan_sub(g->builder, j2, LLVMConstInt(i64, 1, 0), "prev");
                    LLVMValueRef prev_word = slot_word_index(g, prev, ins_words);
                    LLVMValueRef dst_word = slot_word_index(g, j2, ins_words);
                    for (unsigned wi = 0; wi < ins_words; wi++) {
                        LLVMValueRef off = LLVMConstInt(i64, wi, 0);
                        LLVMValueRef src_idx = zan_add(g->builder, prev_word, off, "ins.sw");
                        LLVMValueRef dst_idx = zan_add(g->builder, dst_word, off, "ins.dw");
                        LLVMValueRef src_slot = LLVMBuildGEP2(g->builder, i64, phi_data, &src_idx, 1, "ss");
                        LLVMValueRef sv = LLVMBuildLoad2(g->builder, i64, src_slot, "sv");
                        LLVMValueRef dst_slot = LLVMBuildGEP2(g->builder, i64, phi_data, &dst_idx, 1, "ds");
                        LLVMBuildStore(g->builder, sv, dst_slot);
                    }
                    LLVMBuildStore(g->builder, prev, j_a);
                    LLVMBuildBr(g->builder, scond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, sdone_bb);
                    LLVMValueRef ins_word = slot_word_index(g, idx, ins_words);
                    LLVMValueRef ins_slot = LLVMBuildGEP2(g->builder, i64, phi_data, &ins_word, 1, "is");
                    emit_collection_slot_store(g, elem_type, i64, ins_slot,
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
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    zan_type_t *elem_type = concretize(g,
                        container_elem_type(ltype));
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
                    LLVMValueRef data = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(i64, 0),
                        LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                            list_ptr, 2, "df"), "data");
                    zan_type_t *elem_type = concretize(g,
                        container_elem_type(ltype));
                    LLVMTypeRef elem_llvm = elem_type ? map_type(g, elem_type) : i64;
                    unsigned ta_words = elem_slot_words(g, elem_type);
                    LLVMValueRef total = zan_mul(g->builder, count,
                        LLVMSizeOf(elem_llvm), "ta.total");
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
                    LLVMValueRef wpos = slot_word_index(g, i2, ta_words);
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data,
                        &wpos, 1, "ta.slot");
                    LLVMValueRef val = load_collection_slot_value(g, elem_type, slot);
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
                        LLVMValueRef word = zan_mul(g->builder, found,
                            load_dict_value_words(g, raw), "tgv.word");
                        LLVMValueRef vslot = LLVMBuildGEP2(g->builder, i64, vs,
                            &word, 1, "tgv.vsl");
                        LLVMValueRef vv = load_collection_slot_value(g, value_type, vslot);
                        if (out_ptr) {
                            emit_typed_out_store(g, value_type, out_ptr, vv);
                        }
                        LLVMBuildStore(g->builder, LLVMConstInt(i32t, 1, 0), res_a);
                        LLVMBuildBr(g->builder, done_bb);
                        LLVMPositionBuilderAtEnd(g->builder, miss_bb);
                        LLVMBuildStore(g->builder, LLVMConstInt(i32t, 0, 0), res_a);
                        LLVMBuildBr(g->builder, done_bb);
                        LLVMPositionBuilderAtEnd(g->builder, done_bb);
                        emit_release_owned_call_temp(g, expr->call.args.items[0], search, locals);
                        emit_release_owned_call_temp(g, callee_d->member.object, raw, locals);
                        return LLVMBuildLoad2(g->builder, i32t, res_a, "tgv.out");
                    }
                    if (mname.len == 5 && memcmp(mname.str, "Clear", 5) == 0 && expr->call.args.count == 0) {
                        LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                        LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
                        LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
                        LLVMValueRef ks = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), kp, "ks");
                        LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
                        LLVMValueRef vs = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp, "vs");
                        LLVMValueRef idx_a = emit_entry_alloca(g, i64, "dc");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                        LLVMBasicBlockRef cbb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.cond");
                        LLVMBasicBlockRef bbb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.body");
                        LLVMBasicBlockRef dbb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.done");
                        LLVMBuildBr(g->builder, cbb);
                        LLVMPositionBuilderAtEnd(g->builder, cbb);
                        LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                        LLVMValueRef more = zan_icmp(g->builder, LLVMIntULT, ci, cnt, "more");
                        LLVMBuildCondBr(g->builder, more, bbb, dbb);
                        LLVMPositionBuilderAtEnd(g->builder, bbb);
                        LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                        LLVMValueRef kslot = LLVMBuildGEP2(g->builder, i8ptr, ks, &ci2, 1, "ksl");
                        LLVMValueRef kv = LLVMBuildLoad2(g->builder, i8ptr, kslot, "kv");
                        emit_collection_release_raw_slot(g, dict_key_type(g, dict_type), kv, i8ptr);
                        zan_type_t *value_type = dict_value_type(dict_type);
                        LLVMValueRef value_words = load_dict_value_words(g, raw);
                        LLVMValueRef value_word = zan_mul(g->builder, ci2,
                            value_words, "dc.word");
                        LLVMValueRef vslot = LLVMBuildGEP2(g->builder, i64, vs,
                            &value_word, 1, "vsl");
                        LLVMValueRef vv = load_collection_slot_value(g, value_type, vslot);
                        emit_collection_release_raw_slot(g, value_type, vv, i64);
                        LLVMValueRef ni = zan_add(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                        LLVMBuildStore(g->builder, ni, idx_a);
                        LLVMBuildBr(g->builder, cbb);
                        LLVMPositionBuilderAtEnd(g->builder, dbb);
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), cntp);
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0),
                            LLVMBuildStructGEP2(g->builder, g->dict_struct_type,
                                dp, 5, "icapp"));
                        emit_release_owned_call_temp(g, callee_d->member.object, raw, locals);
                        return LLVMConstInt(i32t, 0, 0);
                    }
                }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee_d = expr->call.callee;
            zan_istr_t mname = callee_d->member.name;
            if (mname.len == 5 && memcmp(mname.str, "Clear", 5) == 0 && expr->call.args.count == 0) {
                if (callee_d->member.object->kind == AST_IDENTIFIER) {
                    local_var_t *dict_local = local_find(locals, callee_d->member.object->ident.name);
                    if (dict_local && dict_local->type && type_named(dict_local->type, "Dict", 4)) {
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMValueRef raw = LLVMBuildLoad2(g->builder, i8ptr, dict_local->alloca, "draw");
                        LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                            LLVMPointerType(g->dict_struct_type, 0), "dp");
                        LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), cntp);
                        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                    }
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
                        LLVMValueRef search = coerce_dict_key(g, emit_expr(g, expr->call.args.items[0], locals), dict_key_type(g, dict_type));
                        zan_type_t *value_type = dict_value_type(dict_type);
                        LLVMValueRef value_words = load_dict_value_words(g, raw);
                        LLVMValueRef res_a = emit_entry_alloca(g, i32t, "dr.res");
                        LLVMBuildStore(g->builder, LLVMConstInt(i32t, 0, 0), res_a);
                        LLVMBasicBlockRef miss_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.miss");
                        LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.done");
                        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.body");
                        LLVMValueRef nonempty = zan_icmp(g->builder, LLVMIntSGT, cnt,
                            LLVMConstInt(i64, 0, 0), "dr.nonempty");
                        LLVMBuildCondBr(g->builder, nonempty, body_bb, miss_bb);
                        LLVMPositionBuilderAtEnd(g->builder, body_bb);
                        LLVMValueRef rmfn = get_dict_remove_fn(g);
                        LLVMValueRef is_str = LLVMConstInt(i64,
                            (dict_key_type(g, dict_type) &&
                             dict_key_type(g, dict_type)->kind == TYPE_STRING) ? 1 : 0, 0);
                        LLVMValueRef fi = zan_call2(g->builder,
                            LLVMGlobalGetValueType(rmfn), rmfn,
                            (LLVMValueRef[]){ raw, search, is_str }, 3, "dr.rm");
                        LLVMValueRef hit = zan_icmp(g->builder, LLVMIntSGE, fi,
                            LLVMConstInt(i64, 0, 0), "dr.hit");
                        LLVMBasicBlockRef hit_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.hitbb");
                        LLVMBuildCondBr(g->builder, hit, hit_bb, miss_bb);
                        LLVMPositionBuilderAtEnd(g->builder, hit_bb);
                        LLVMValueRef last = LLVMBuildLoad2(g->builder, i64, cntp, "cnt2");
                        LLVMValueRef kp0 = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp0");
                        LLVMValueRef ks0 = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), kp0, "ks0");
                        LLVMValueRef rkey = LLVMBuildGEP2(g->builder, i8ptr, ks0, &fi, 1, "rkey");
                        LLVMValueRef rkv = LLVMBuildLoad2(g->builder, i8ptr, rkey, "rkv");
                        emit_collection_release_raw_slot(g, dict_key_type(g, dict_type), rkv, i8ptr);
                        LLVMValueRef vp0 = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp0");
                        LLVMValueRef vs0 = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp0, "vs0");
                        LLVMValueRef removed_word = zan_mul(g->builder, fi,
                            value_words, "dr.word");
                        LLVMValueRef rvslot = LLVMBuildGEP2(g->builder, i64, vs0,
                            &removed_word, 1, "rvslot");
                        LLVMValueRef rv = load_collection_slot_value(g, value_type, rvslot);
                        emit_collection_release_raw_slot(g, value_type, rv, i64);
                        LLVMTypeRef move_type = LLVMFunctionType(i8ptr,
                            (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
                        LLVMValueRef move_fn = get_libc_fn(g, "memmove", move_type);
                        LLVMValueRef is_last = zan_icmp(g->builder, LLVMIntEQ, fi, last, "dr.islast");
                        LLVMBasicBlockRef move_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.move");
                        LLVMBasicBlockRef tail_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.tail");
                        LLVMBuildCondBr(g->builder, is_last, tail_bb, move_bb);
                        LLVMPositionBuilderAtEnd(g->builder, move_bb);
                        LLVMValueRef shift_entries = zan_sub(g->builder, last, fi, "dr.shent");
                        LLVMValueRef ksrc_i = zan_add(g->builder, fi,
                            LLVMConstInt(i64, 1, 0), "dr.ksi");
                        LLVMValueRef ksrc = LLVMBuildGEP2(g->builder, i8ptr, ks0,
                            &ksrc_i, 1, "dr.ksrc");
                        LLVMValueRef kbytes = zan_mul(g->builder, shift_entries,
                            LLVMConstInt(i64, 8, 0), "dr.kbytes");
                        zan_call2(g->builder, move_type, move_fn,
                            (LLVMValueRef[]){ LLVMBuildBitCast(g->builder, rkey, i8ptr, "kdst8"),
                                              LLVMBuildBitCast(g->builder, ksrc, i8ptr, "ksrc8"),
                                              kbytes }, 3, "");
                        LLVMValueRef next_word = zan_add(g->builder, removed_word,
                            value_words, "dr.vnw");
                        LLVMValueRef vsrc = LLVMBuildGEP2(g->builder, i64, vs0,
                            &next_word, 1, "dr.vsrc");
                        LLVMValueRef vbytes = zan_mul(g->builder, shift_entries,
                            zan_mul(g->builder, value_words,
                                LLVMConstInt(i64, 8, 0), "dr.vw8"), "dr.vbytes");
                        zan_call2(g->builder, move_type, move_fn,
                            (LLVMValueRef[]){ LLVMBuildBitCast(g->builder, rvslot, i8ptr, "vdst8"),
                                              LLVMBuildBitCast(g->builder, vsrc, i8ptr, "vsrc8"),
                                              vbytes }, 3, "");
                        LLVMBuildBr(g->builder, tail_bb);
                        LLVMPositionBuilderAtEnd(g->builder, tail_bb);
                        LLVMValueRef tkey = LLVMBuildGEP2(g->builder, i8ptr, ks0, &last, 1, "tkey");
                        LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), tkey);
                        LLVMTypeRef zero_ty = LLVMFunctionType(i8ptr,
                            (LLVMTypeRef[]){ i8ptr, i32t, i64 }, 3, 0);
                        LLVMValueRef memset_fn = get_libc_fn(g, "memset", zero_ty);
                        LLVMValueRef tw = zan_mul(g->builder, last, value_words, "dr.tword");
                        LLVMValueRef tvslot = LLVMBuildGEP2(g->builder, i64, vs0, &tw, 1, "tvslot");
                        LLVMValueRef tail_bytes = zan_mul(g->builder, value_words,
                            LLVMConstInt(i64, 8, 0), "tail.bytes");
                        zan_call2(g->builder, zero_ty, memset_fn,
                            (LLVMValueRef[]){ LLVMBuildBitCast(g->builder, tvslot, i8ptr, "tv8"),
                                              LLVMConstInt(i32t, 0, 0),
                                              tail_bytes }, 3, "");
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

