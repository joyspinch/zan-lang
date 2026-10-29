/* irgen_call.c - 表达式调用求值、虚表与实例分发、方法重载解析与未解析诊断 */
static LLVMValueRef emit_expr_call(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
    bool matched = false;
    LLVMValueRef builtin_res = emit_builtin_call_intercept(g, expr, locals, &matched);
    if (matched) return builtin_res;

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

        {
            zan_type_t *delegate_type =
                infer_expr_type(g, expr->call.callee, locals);
            if (delegate_type &&
                delegate_type->kind == TYPE_DELEGATE) {
                LLVMValueRef fn_ptr =
                    emit_expr(g, expr->call.callee, locals);
                return emit_delegate_call(
                    g, delegate_type, fn_ptr, expr, locals);
            }
        }

        {
            zan_type_t *oc_ty = infer_expr_type(g, expr->call.callee, locals);
            if (oc_ty && (oc_ty->kind == TYPE_CLASS || oc_ty->kind == TYPE_STRUCT) &&
                oc_ty->sym) {
                zan_istr_t op_istr = {(char *)"op_call", 7};
                zan_symbol_t *op_sym = resolve_op_overload(g, oc_ty->sym,
                                                           op_istr, expr, locals);
                if (op_sym) {
                    pack_params_args(g, expr, op_sym, locals);
                    for (int fi = irgen_find_function(g, op_sym); fi >= 0; fi = -1) {
                        if (g->functions[fi].sym == op_sym) {
                            int argc = expr->call.args.count + 1;
                            LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                                (size_t)argc, sizeof(LLVMValueRef));
                            LLVMValueRef recv_val = emit_expr(g, expr->call.callee, locals);
                            call_args[0] = recv_val;
                            if (LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMStructTypeKind) {
                                LLVMValueRef rslot = emit_entry_alloca(g,
                                    LLVMTypeOf(recv_val), "opc.recv");
                                LLVMBuildStore(g->builder, recv_val, rslot);
                                call_args[0] = rslot;
                            }
                            int recv_eh_pushed = 0;
                            if (oc_ty->kind == TYPE_CLASS &&
                                !expr_is_local_ident(expr->call.callee, locals) &&
                                expr_yields_owned_rc_value(g, expr->call.callee, locals) &&
                                LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMPointerTypeKind) {
                                emit_eh_tmp_push(g, recv_val);
                                recv_eh_pushed = 1;
                            }
                            int n_eh_args = 0;
                            for (int k = 0; k < expr->call.args.count; k++) {
                                int self_off =
                                    (op_sym->modifiers & MOD_STATIC) ? 1 : 0;
                                call_args[k + 1] = emit_arg_typed(g,
                                    expr->call.args.items[k],
                                    method_param_type_at(g, op_sym,
                                        k + self_off, expr,
                                        expr->call.callee, locals), locals);
                                n_eh_args += emit_call_arg_eh_push(g,
                                    expr->call.args.items[k],
                                    call_args[k + 1], locals);
                            }
                            LLVMTypeRef mft = g->functions[fi].fn_type;
                            LLVMValueRef mfn = route_generic_method(g, oc_ty,
                                op_sym, g->functions[fi].fn, mft, &mft);
                            const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "opcall";
                            LLVMValueRef result = emit_dispatch_call(g,
                                oc_ty->sym, op_sym, mfn, mft, call_args, argc, cn);
                            result = coerce_generic_result(g, result, op_sym, oc_ty);
                            for (int k = 0; k < n_eh_args; k++)
                                emit_eh_tmp_pop(g);
                            if (recv_eh_pushed) emit_eh_tmp_pop(g);
                            emit_release_owned_call_temp(g, expr->call.callee,
                                recv_val, locals);
                            for (int k = 0; k < expr->call.args.count; k++) {
                                emit_release_owned_call_temp(g, expr->call.args.items[k],
                                    call_args[k + 1], locals);
                            }
                            free(call_args);
                            return result;
                        }
                    }
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            if (callee->member.object->kind == AST_IDENTIFIER) {
                local_var_t *local = local_find(locals, callee->member.object->ident.name);
                if (local && local->type && local->type->sym) {
                    zan_symbol_t *type_sym = local->type->sym;
                    zan_symbol_t *method_sym = resolve_overload_typed(g, type_sym, callee->member.name, expr, locals);
                    if (method_sym) fill_default_args(g, expr, method_sym);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                    if (method_sym) {
                        /* `d */
                        bool callee_static = (method_sym->modifiers & MOD_STATIC) != 0;
                        int recv_off = callee_static ? 0 : 1;
                        int spec = try_method_spec(g, method_sym, expr,
                                                   callee->member.object, locals);
                        if (spec >= 0)
                            return emit_method_spec_call(g, spec, expr,
                                                         callee->member.object, locals);
                        for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                            if (g->functions[fi].sym == method_sym) {
                                int argc = expr->call.args.count + recv_off;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                                    (size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                                if (!callee_static) {
                                    LLVMTypeRef at = local_slot_type(g, local);
                                    if (LLVMGetTypeKind(at) == LLVMPointerTypeKind) {
                                        call_args[0] = LLVMBuildLoad2(g->builder, at, local->alloca, "recv");
                                    } else {
                                        call_args[0] = local->alloca;
                                    }
                                    /* null 接收者 guard（TASKS.md A507）：这条
                                     * 局部变量快速路径曾完全无检查——this=null
                                     * 一路进方法体读字段，SIGSEGV 且无任何诊断
                                     * （字段访问路径早有 emit_weak_read_guard，
                                     * 方法调用偏偏漏了）。补齐同口径：软诊断 +
                                     * 软基址（scratch），--strict-runtime 下
                                     * exit 70。 */
                                    if (LLVMGetTypeKind(LLVMTypeOf(call_args[0]))
                                            == LLVMPointerTypeKind &&
                                        !callee->member.null_cond) {
                                        zan_type_t *rty = local->type;
                                        bool reflike = rty &&
                                            (rty->kind == TYPE_OBJECT ||
                                             rty->kind == TYPE_CLASS ||
                                             rty->kind == TYPE_INTERFACE ||
                                             rty->kind == TYPE_TYPE_PARAM ||
                                             rty->kind == TYPE_ERROR);
                                        if (reflike) {
                                            LLVMValueRef isnull = zan_icmp(
                                                g->builder, LLVMIntEQ,
                                                call_args[0],
                                                LLVMConstNull(LLVMTypeOf(call_args[0])),
                                                "recv.null");
                                            char nm[256];
                                            snprintf(nm, sizeof(nm),
                                                "null reference: receiver '%.*s' is null",
                                                (int)callee->member.object->ident.name.len,
                                                callee->member.object->ident.name.str);
                                            emit_runtime_check(g, isnull,
                                                callee->member.object->loc, nm);
                                            /* A550：async 被调者 + null 接收者
                                             * 不能"报告后继续"——软基址会把
                                             * scratch 当协程帧调度，协程序言
                                             * 虚表派发读哨兵字节。走到这里 =
                                             * 不在 try（在 try 的已 longjmp
                                             * 转 NRE），无有效续延，响亮中止。 */
                                            if (call_is_async_callee(g, expr, locals)) {
                                                LLVMValueRef gfn = LLVMGetBasicBlockParent(
                                                    LLVMGetInsertBlock(g->builder));
                                                LLVMBasicBlockRef live_bb =
                                                    LLVMAppendBasicBlockInContext(g->ctx, gfn, "recv.live");
                                                LLVMBasicBlockRef dead_bb =
                                                    LLVMAppendBasicBlockInContext(g->ctx, gfn, "recv.dead");
                                                LLVMBuildCondBr(g->builder, isnull, dead_bb, live_bb);
                                                LLVMPositionBuilderAtEnd(g->builder, dead_bb);
                                                LLVMTypeRef abty = LLVMFunctionType(
                                                    LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
                                                zan_call2(g->builder, abty,
                                                    get_libc_fn(g, "abort", abty), NULL, 0, "");
                                                LLVMBuildUnreachable(g->builder);
                                                LLVMPositionBuilderAtEnd(g->builder, live_bb);
                                            }
                                            call_args[0] = emit_soft_base_select(
                                                g, call_args[0], isnull,
                                                callee->member.object->loc);
                                        }
                                    }
                                }
                                int n_eh_args = 0;
                                for (int k = 0; k < expr->call.args.count; k++) {
                                    call_args[k + recv_off] = emit_arg_typed(g, expr->call.args.items[k],
                                        method_param_type_at(g, method_sym, k, expr,
                                                             callee->member.object, locals), locals);
                                    n_eh_args += emit_call_arg_eh_push(g,
                                        expr->call.args.items[k],
                                        call_args[k + recv_off], locals);
                                }
                                LLVMTypeRef mft = g->functions[fi].fn_type;
                                LLVMValueRef mfn = route_generic_method(g, local->type,
                                    method_sym, g->functions[fi].fn, mft, &mft);
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "mcall";
                                LLVMValueRef result = emit_dispatch_call(g,
                                    callee_static ? NULL : type_sym, method_sym,
                                    mfn, mft, call_args, argc, cn);
                                result = coerce_generic_result(g, result, method_sym, local->type);
                                for (int k = 0; k < n_eh_args; k++)
                                    emit_eh_tmp_pop(g);
                                for (int k = 0; k < expr->call.args.count; k++) {
                                    emit_release_owned_call_temp(g, expr->call.args.items[k],
                                        call_args[k + recv_off], locals);
                                }
                                free(call_args);
                                return result;
                            }
                        }
                    }
                }

                zan_symbol_t *type_sym = NULL;
                if (!ident_names_own_field(g, callee->member.object))
                    type_sym = zan_binder_lookup(g->binder,
                        callee->member.object->ident.name);
                if (type_sym && (type_sym->kind == SYM_CLASS || type_sym->kind == SYM_STRUCT)) {
                    zan_symbol_t *method_sym = resolve_overload_typed(g, type_sym, callee->member.name, expr, locals);
                    if (method_sym) fill_default_args(g, expr, method_sym);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                    if (method_sym) {
                        if ((method_sym->modifiers & MOD_STATIC) == 0) {
                            zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3062,
                                "cannot call instance method '%.*s.%.*s' via "
                                "the type name: call it on an instance",
                                (int)type_sym->name.len, type_sym->name.str,
                                (int)callee->member.name.len,
                                callee->member.name.str);
                        }
                        int spec = try_method_spec(g, method_sym, expr, NULL, locals);
                        if (spec >= 0)
                            return emit_method_spec_call(g, spec, expr, NULL, locals);
                        for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                            if (g->functions[fi].sym == method_sym) {
                                int argc = expr->call.args.count;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                                int *arg_eh_pushed = (int *)calloc(
                                    (size_t)(argc > 0 ? argc : 1), sizeof(int));
                                for (int k = 0; k < argc; k++) {
                                    zan_ast_node_t *arg = expr->call.args.items[k];
                                    call_args[k] = emit_arg_typed(g, arg,
                                        method_param_type_at(g, method_sym, k, expr, NULL, locals), locals);
                                    arg_eh_pushed[k] = emit_call_arg_eh_push(g, arg,
                                        call_args[k], locals);
                                }
                                /* `Box<int> */
                                LLVMTypeRef sft = g->functions[fi].fn_type;
                                LLVMValueRef sfn = route_generic_method(g,
                                    ident_inst_type(g, callee->member.object),
                                    method_sym, g->functions[fi].fn, sft, &sft);
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(sft)) == LLVMVoidTypeKind) ? "" : "scall";
                                coerce_args_to_params(g, sft, call_args, argc);
                                LLVMValueRef result = zan_call2(g->builder, sft,
                                    sfn, call_args, (unsigned)argc, cn);
                                zan_type_t *gret = generic_method_ret(g, method_sym, expr, locals);
                                if (!gret && method_ret_is_bare_tp(method_sym)) {
                                    zan_type_t *ir = method_ret_type_at(g, method_sym,
                                        expr, NULL, locals);
                                    if (ir && ir->kind != TYPE_TYPE_PARAM) gret = ir;
                                }
                                if (gret) {
                                    result = emit_boundary_coerce(g, result, map_type(g, gret));
                                    if (gret->kind == TYPE_CLASS &&
                                        method_ret_is_bare_tp(method_sym))
                                        emit_arc_retain(g, result);
                                }
                                int consumes_free_arg =
                                    argc == 1 && call_consumes_free_arg(g->functions[fi].fn);
                                if (consumes_free_arg) {
                                    emit_invalidate_freed_string(g,
                                        expr->call.args.items[0], locals);
                                }
                                emit_extern_call_len_invalidate(g,
                                    g->functions[fi].fn, call_args, argc,
                                    expr, locals);
                                for (int k = argc - 1; k >= 0; k--) {
                                    if (arg_eh_pushed[k]) emit_eh_tmp_pop(g);
                                }
                                for (int k = 0; k < argc; k++) {
                                    if (!consumes_free_arg || k != 0) {
                                        emit_release_owned_call_temp(g, expr->call.args.items[k],
                                            call_args[k], locals);
                                    }
                                }
                                free(arg_eh_pushed);
                                free(call_args);
                                return result;
                            }
                        }
                    }
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_symbol_t *recv_cls = expr_class_sym(g, callee->member.object, locals);
            if (recv_cls) {
                zan_symbol_t *method_sym = resolve_overload_typed(g, recv_cls, callee->member.name, expr, locals);
                    if (method_sym) fill_default_args(g, expr, method_sym);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                if (method_sym) {
                    bool callee_static = (method_sym->modifiers & MOD_STATIC) != 0;
                    int spec = try_method_spec(g, method_sym, expr,
                                               callee->member.object, locals);
                    if (spec >= 0)
                        return emit_method_spec_call(g, spec, expr,
                                                     callee->member.object, locals);
                    for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                        if (g->functions[fi].sym == method_sym) {
                            int recv_off = callee_static ? 0 : 1;
                            int argc = expr->call.args.count + recv_off;
                            LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                                (size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                            LLVMValueRef recv_val = NULL;
                            int recv_eh_pushed = 0;
                            if (!callee_static) {
                                recv_val = emit_guarded_member_object_ex(
                                    g, callee, locals,
                                    call_is_async_callee(g, expr, locals));
                                call_args[0] = recv_val;
                                if (LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMStructTypeKind) {
                                    LLVMValueRef rslot = emit_entry_alloca(g, LLVMTypeOf(recv_val), "recv.tmp");
                                    LLVMBuildStore(g->builder, recv_val, rslot);
                                    call_args[0] = rslot;
                                }
                                {
                                    zan_type_t *rty = infer_expr_type(g, callee->member.object, locals);
                                    if (rty && rty->kind == TYPE_CLASS &&
                                        !expr_is_local_ident(callee->member.object, locals) &&
                                        expr_yields_owned_rc_value(g, callee->member.object, locals) &&
                                        LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMPointerTypeKind) {
                                        emit_eh_tmp_push(g, recv_val);
                                        recv_eh_pushed = 1;
                                    }
                                }
                            }
                            int n_eh_args = 0;
                            for (int k = 0; k < expr->call.args.count; k++) {
                                call_args[k + recv_off] = emit_arg_typed(g, expr->call.args.items[k],
                                    method_param_type_at(g, method_sym, k, expr,
                                                         callee->member.object, locals),
                                    locals);
                                n_eh_args += emit_call_arg_eh_push(g,
                                    expr->call.args.items[k],
                                    call_args[k + recv_off], locals);
                            }
                            zan_type_t *recv_ty = callee_static ? g->cur_inst
                                : infer_expr_type(g, callee->member.object, locals);
                            LLVMTypeRef mft = g->functions[fi].fn_type;
                            LLVMValueRef mfn = route_generic_method(g, recv_ty,
                                method_sym, g->functions[fi].fn, mft, &mft);
                            const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "mcall";
                            bool base_recv =
                                callee->member.object->kind == AST_BASE_EXPR;
                            LLVMValueRef result = emit_dispatch_call(g,
                                (callee_static || base_recv) ? NULL : recv_cls,
                                method_sym, mfn, mft, call_args, argc, cn);
                            result = coerce_generic_result(g, result, method_sym, recv_ty);
                            for (int k = 0; k < n_eh_args; k++)
                                emit_eh_tmp_pop(g);
                            if (recv_eh_pushed) emit_eh_tmp_pop(g);
                            if (!callee_static) {
                                emit_release_owned_call_temp(g, callee->member.object, recv_val, locals);
                            }
                            for (int k = 0; k < expr->call.args.count; k++) {
                                emit_release_owned_call_temp(g, expr->call.args.items[k],
                                    call_args[k + recv_off], locals);
                            }
                            free(call_args);
                            return result;
                        }
                    }
                }
            }
        }

        /* A558：object/接口/类接收者上的 System.Object 方法族。静态类型
         * （含基类链/接口自身）声明了同名成员时走普通成员调用或接口
         * 分派路径，不进这里。 */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            g->current_fn) {
            zan_ast_node_t *ofc = expr->call.callee;
            zan_istr_t fmn = ofc->member.name;
            int fam = -1;
            if (fmn.len == 8 && memcmp(fmn.str, "ToString", 8) == 0 &&
                expr->call.args.count == 0)
                fam = 0;
            else if (fmn.len == 6 && memcmp(fmn.str, "Equals", 6) == 0 &&
                     expr->call.args.count == 1)
                fam = 1;
            else if (fmn.len == 11 && memcmp(fmn.str, "GetHashCode", 11) == 0 &&
                     expr->call.args.count == 0)
                fam = 2;
            if (fam >= 0) {
                zan_type_t *ot = infer_expr_type(g, ofc->member.object, locals);
                if (ot && (ot->kind == TYPE_OBJECT || ot->kind == TYPE_INTERFACE ||
                           ot->kind == TYPE_CLASS)) {
                    zan_symbol_t *osym = ot->sym;
                    bool declared = false;
                    if (osym) {
                        declared = (ot->kind == TYPE_INTERFACE)
                            ? resolve_iface_overload(osym, fmn,
                                                     expr->call.args.count, 0) != NULL
                            : resolve_overload(osym, fmn,
                                               expr->call.args.count, 0) != NULL;
                    }
                    if (!declared)
                        return emit_object_family_call(g, expr, fam, locals);
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_type_t *obj_ty = infer_expr_type(g, callee->member.object, locals);
            if (obj_ty && obj_ty->kind == TYPE_INTERFACE && obj_ty->sym && g->current_fn) {
                zan_symbol_t *iface = obj_ty->sym;
                zan_symbol_t *iface_m = resolve_iface_overload(iface,
                                            callee->member.name,
                                            expr->call.args.count,
                                            expr->call.type_args.count);
                if (iface_m) {
                    LLVMContextRef c = g->ctx;
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
                    zan_type_t *rty = NULL;
                    if (iface_m->decl && iface_m->decl->kind == AST_METHOD_DECL &&
                        iface_m->decl->method_decl.return_type)
                        rty = zan_binder_resolve_type(g->binder,
                                  iface_m->decl->method_decl.return_type);
                    zan_ast_list_t *ifc_tps = &iface->decl->type_decl.type_params;
                    bool ifc_generic = obj_ty->type_arg_count > 0 &&
                        obj_ty->type_arg_count == ifc_tps->count &&
                        iface->decl->kind == AST_INTERFACE_DECL;
                    bool m_async = iface_m->decl &&
                        iface_m->decl->kind == AST_METHOD_DECL &&
                        (iface_m->decl->method_decl.modifiers & MOD_ASYNC) != 0;
                    bool has_res = m_async || (rty && rty->kind != TYPE_VOID);
                    if (ifc_generic && rty)
                        rty = zan_binder_subst_named(g->binder, rty,
                                                     ifc_tps, obj_ty->type_args);
                    LLVMTypeRef res_ty = m_async ? i8ptr
                                                 : (has_res ? map_type(g, rty) : NULL);

                    int uargc = expr->call.args.count;
                    int n_eh_args = 0;
                    LLVMValueRef recv = emit_guarded_member_object_ex(
                        g, callee, locals, m_async != 0);
                    LLVMValueRef *avals = (LLVMValueRef *)calloc((size_t)(uargc > 0 ? uargc : 1),
                                                                sizeof(LLVMValueRef));
                    for (int k = 0; k < uargc; k++) {
                        zan_type_t *pat = method_param_type_at(g, iface_m, k, expr,
                                                               callee->member.object,
                                                               locals);
                        if (ifc_generic && pat)
                            pat = zan_binder_subst_named(g->binder, pat,
                                                         ifc_tps,
                                                         obj_ty->type_args);
                        avals[k] = emit_arg_typed(g, expr->call.args.items[k],
                                                  pat, locals);
                        n_eh_args += emit_call_arg_eh_push(g, expr->call.args.items[k],
                                                          avals[k], locals);
                    }
                    typedef struct {
                        zan_symbol_t *cls;
                        zan_symbol_t *impl_m;
                        LLVMValueRef  fn;
                        LLVMTypeRef   fnty;
                        LLVMValueRef  tagc;
                    } ifc_candidate_t;

                    int cand_cap = g->struct_type_count + g->generic_inst_count + 4;
                    ifc_candidate_t *cands = (ifc_candidate_t *)calloc(
                        (size_t)cand_cap, sizeof(ifc_candidate_t));
                    int cand_count = 0;

                    /* 1. 非泛型类候选 */
                    for (int si = 0; si < g->struct_type_count; si++) {
                        zan_symbol_t *cls = g->struct_types[si].sym;
                        if (!cls || is_user_generic_sym(cls)) continue;
                        if (!class_implements_iface(cls, iface)) continue;
                        if (obj_ty->type_arg_count > 0 &&
                            !class_inst_implements_iface(g, cls, NULL, obj_ty)) continue;
                        zan_symbol_t *impl_m = resolve_overload(cls, callee->member.name, uargc, 0);
                        if (!impl_m) continue;
                        LLVMValueRef ifn = NULL; LLVMTypeRef ifnty = NULL;
                        for (int fi = irgen_find_function(g, impl_m); fi >= 0; fi = -1)
                            if (g->functions[fi].sym == impl_m) {
                                ifn = g->functions[fi].fn; ifnty = g->functions[fi].fn_type; break;
                            }
                        if (!ifn || !ifnty) continue;
                        cands[cand_count].cls = cls;
                        cands[cand_count].impl_m = impl_m;
                        cands[cand_count].fn = ifn;
                        cands[cand_count].fnty = ifnty;
                        cands[cand_count].tagc = LLVMConstBitCast(get_vtable_global(g, cls), i8ptr);
                        cand_count++;
                    }

                    /* 2. 泛型实例化类候选 */
                    for (int gi = 0; gi < g->generic_inst_count; gi++) {
                        zan_symbol_t *cls = g->generic_insts[gi].type_sym;
                        zan_type_t *inst = g->generic_insts[gi].inst;
                        if (!cls || !inst || !is_user_generic_sym(cls)) continue;
                        if (!class_inst_implements_iface(g, cls, inst, obj_ty)) continue;
                        zan_symbol_t *impl_m = resolve_overload(cls, callee->member.name, uargc, 0);
                        if (!impl_m) continue;
                        zan_symbol_t *msym_owner = impl_m->parent ? impl_m->parent : cls;
                        zan_type_t *owner = is_user_generic_sym(msym_owner) ? vtable_owner_inst(g, inst, msym_owner) : NULL;
                        LLVMValueRef ifn = NULL; LLVMTypeRef ifnty = NULL;
                        if (owner && owner->type_arg_count > 0) {
                            ifn = find_generic_fn(g, impl_m, owner->type_args,
                                                  owner->type_arg_count, &ifnty);
                        } else if (!is_user_generic_sym(msym_owner)) {
                            for (int fi = irgen_find_function(g, impl_m); fi >= 0; fi = -1)
                                if (g->functions[fi].sym == impl_m) {
                                    ifn = g->functions[fi].fn; ifnty = g->functions[fi].fn_type; break;
                                }
                        }
                        if (!ifn || !ifnty) continue;
                        cands[cand_count].cls = cls;
                        cands[cand_count].impl_m = impl_m;
                        cands[cand_count].fn = ifn;
                        cands[cand_count].fnty = ifnty;
                        cands[cand_count].tagc = LLVMConstBitCast(
                            get_vtable_global_inst(g, cls, inst), i8ptr);
                        cand_count++;
                    }

                    zan_compile_trace("ifc dispatch %.*s: cands=%d (structs=%d ginsts=%d)",
                        (int)callee->member.name.len, callee->member.name.str,
                        cand_count, g->struct_type_count, g->generic_inst_count);
                    if (cand_count == 1 && cands[0].cls && cands[0].impl_m &&
                        cands[0].fn && cands[0].fnty) {
                        unsigned npar = LLVMCountParamTypes(cands[0].fnty);
                        LLVMTypeRef *pts = (LLVMTypeRef *)calloc((size_t)(npar > 0 ? npar : 1), sizeof(LLVMTypeRef));
                        LLVMGetParamTypes(cands[0].fnty, pts);
                        int cargc = uargc + 1;
                        LLVMValueRef *ca = (LLVMValueRef *)calloc((size_t)cargc, sizeof(LLVMValueRef));
                        /* A556-2：结构体箱的载荷在首字 vtable tag 之后（+8），
                         * 结构体方法的 this 指向载荷。 */
                        LLVMValueRef thisv = recv;
                        if (cands[0].cls->kind == SYM_STRUCT)
                            thisv = LLVMBuildGEP2(g->builder,
                                LLVMInt8TypeInContext(c), recv,
                                (LLVMValueRef[]){ LLVMConstInt(LLVMInt64TypeInContext(c), 8, 0) },
                                1, "ifc.p8");
                        ca[0] = (npar > 0) ? LLVMBuildBitCast(g->builder, thisv, pts[0], "ifc.this") : thisv;
                        for (int k = 0; k < uargc; k++)
                            ca[k + 1] = (k + 1 < (int)npar)
                                ? emit_boundary_coerce(g, avals[k], pts[k + 1]) : avals[k];
                        const char *cn = has_res ? "ifccall" : "";
                        LLVMValueRef r = emit_dispatch_call(g, cands[0].cls, cands[0].impl_m,
                                                            cands[0].fn, cands[0].fnty, ca, cargc, cn);
                        if (has_res) {
                            r = emit_boundary_coerce(g, r, res_ty);
                        } else {
                            r = LLVMConstInt(LLVMInt32TypeInContext(c), 0, 0);
                        }
                        for (int k = 0; k < n_eh_args; k++)
                            emit_eh_tmp_pop(g);
                        for (int k = 0; k < uargc; k++)
                            emit_release_owned_call_temp(g, expr->call.args.items[k], avals[k], locals);
                        emit_release_owned_call_temp(g, callee->member.object, recv, locals);
                        free(ca); free(pts); free(avals); free(cands);
                        return r;
                    }

                    LLVMValueRef recv_pp = LLVMBuildBitCast(g->builder, recv,
                                              LLVMPointerType(i8ptr, 0), "ifc.recvpp");
                    LLVMValueRef tag = LLVMBuildLoad2(g->builder, i8ptr, recv_pp, "ifc.tag");

                    LLVMBasicBlockRef merge = LLVMAppendBasicBlockInContext(c, g->current_fn, "ifc.merge");
                    int cap = cand_count + 1;
                    LLVMValueRef *phi_vals = (LLVMValueRef *)calloc((size_t)cap, sizeof(LLVMValueRef));
                    LLVMBasicBlockRef *phi_bbs = (LLVMBasicBlockRef *)calloc((size_t)cap, sizeof(LLVMBasicBlockRef));
                    int np = 0;

                    for (int ci = 0; ci < cand_count; ci++) {
                        LLVMBasicBlockRef callbb = LLVMAppendBasicBlockInContext(c, g->current_fn, "ifc.call");
                        LLVMBasicBlockRef nextbb = LLVMAppendBasicBlockInContext(c, g->current_fn, "ifc.next");
                        LLVMValueRef tagc = cands[ci].tagc;
                        LLVMValueRef cmp = zan_icmp(g->builder, LLVMIntEQ, tag, tagc, "ifc.eq");
                        LLVMBuildCondBr(g->builder, cmp, callbb, nextbb);

                        LLVMPositionBuilderAtEnd(g->builder, callbb);
                        unsigned npar = LLVMCountParamTypes(cands[ci].fnty);
                        LLVMTypeRef *pts = (LLVMTypeRef *)calloc((size_t)(npar > 0 ? npar : 1), sizeof(LLVMTypeRef));
                        LLVMGetParamTypes(cands[ci].fnty, pts);
                        int cargc = uargc + 1;
                        LLVMValueRef *ca = (LLVMValueRef *)calloc((size_t)cargc, sizeof(LLVMValueRef));
                        /* A556-2：结构体箱的 this 指向载荷（+8），见单候选分支 */
                        LLVMValueRef thisv = recv;
                        if (cands[ci].cls->kind == SYM_STRUCT)
                            thisv = LLVMBuildGEP2(g->builder,
                                LLVMInt8TypeInContext(c), recv,
                                (LLVMValueRef[]){ LLVMConstInt(LLVMInt64TypeInContext(c), 8, 0) },
                                1, "ifc.p8");
                        ca[0] = (npar > 0) ? LLVMBuildBitCast(g->builder, thisv, pts[0], "ifc.this") : thisv;
                        for (int k = 0; k < uargc; k++)
                            ca[k + 1] = (k + 1 < (int)npar)
                                ? emit_boundary_coerce(g, avals[k], pts[k + 1]) : avals[k];
                        const char *cn = has_res ? "ifccall" : "";
                        LLVMValueRef r = zan_call2(g->builder, cands[ci].fnty, cands[ci].fn, ca, (unsigned)cargc, cn);
                        if (has_res) {
                            r = emit_boundary_coerce(g, r, res_ty);
                            phi_vals[np] = r; phi_bbs[np] = LLVMGetInsertBlock(g->builder); np++;
                        }
                        LLVMBuildBr(g->builder, merge);
                        free(ca); free(pts);
                        LLVMPositionBuilderAtEnd(g->builder, nextbb);
                    }
                    free(cands);

                    emit_runtime_check(g, LLVMConstInt(LLVMInt1TypeInContext(c), 1, 0),
                                       expr->loc, "interface dispatch has no implementation");
                    if (has_res) {
                        phi_vals[np] = LLVMConstNull(res_ty);
                        phi_bbs[np] = LLVMGetInsertBlock(g->builder); np++;
                    }
                    LLVMBuildBr(g->builder, merge);
                    LLVMPositionBuilderAtEnd(g->builder, merge);
                    LLVMValueRef result;
                    if (has_res) {
                        LLVMValueRef phi = LLVMBuildPhi(g->builder, res_ty, "ifc.res");
                        LLVMAddIncoming(phi, phi_vals, phi_bbs, (unsigned)np);
                        result = phi;
                    } else {
                        result = LLVMConstInt(LLVMInt32TypeInContext(c), 0, 0);
                    }
                    for (int k = 0; k < n_eh_args; k++)
                        emit_eh_tmp_pop(g);
                    for (int k = 0; k < uargc; k++)
                        emit_release_owned_call_temp(g, expr->call.args.items[k],
                                                     avals[k], locals);
                    emit_release_owned_call_temp(g, callee->member.object, recv,
                                                 locals);
                    free(avals); free(phi_vals); free(phi_bbs);
                    return result;
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_ast_node_t *obj = callee->member.object;
            zan_ast_node_t *head = name_path_head(obj);
            if (is_name_path(obj) && head && !local_find(locals, head->ident.name)) {
                zan_symbol_t *type_sym = zan_binder_lookup(g->binder, obj->member.name);
                if (type_sym && (type_sym->kind == SYM_CLASS || type_sym->kind == SYM_STRUCT) &&
                    qpath_ns_ok(g, obj, type_sym)) {
                    zan_symbol_t *method_sym = resolve_overload_typed(g, type_sym, callee->member.name, expr, locals);
                    if (method_sym) fill_default_args(g, expr, method_sym);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                    if (method_sym) {
                        for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                            if (g->functions[fi].sym == method_sym) {
                                int argc = expr->call.args.count;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                                int n_eh_args = 0;
                                for (int k = 0; k < argc; k++) {
                                    call_args[k] = emit_arg_typed(g, expr->call.args.items[k],
                                        method_param_type_at(g, method_sym, k, expr, NULL, locals), locals);
                                    n_eh_args += emit_call_arg_eh_push(g, expr->call.args.items[k],
                                                          call_args[k], locals);
                                }
                                if (g->runtime_checks && g->current_fn &&
                                    sym_declares_extern(method_sym)) {
                                    for (int k = 0; k < argc; k++) {
                                        if (LLVMGetTypeKind(LLVMTypeOf(call_args[k]))
                                                != LLVMPointerTypeKind) continue;
                                        if (!is_string_expr(g,
                                                expr->call.args.items[k], locals))
                                            continue;
                                        LLVMValueRef isnull = zan_icmp(g->builder,
                                            LLVMIntEQ, call_args[k],
                                            LLVMConstNull(LLVMTypeOf(call_args[k])),
                                            "extarg.null");
                                        emit_runtime_check(g, isnull, expr->loc,
                                            "null string passed to an extern function");
                                        call_args[k] = LLVMBuildSelect(g->builder,
                                            isnull,
                                            zan_irgen_intern_string(g, ""),
                                            call_args[k], "extarg.safe");
                                    }
                                }
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(g->functions[fi].fn_type)) == LLVMVoidTypeKind) ? "" : "scall";
                                coerce_args_to_params(g, g->functions[fi].fn_type, call_args, argc);
                                LLVMValueRef result = zan_call2(g->builder, g->functions[fi].fn_type,
                                    g->functions[fi].fn, call_args, (unsigned)argc, cn);
                                zan_type_t *gret = generic_method_ret(g, method_sym, expr, locals);
                                if (!gret && method_ret_is_bare_tp(method_sym)) {
                                    zan_type_t *ir = method_ret_type_at(g, method_sym,
                                        expr, NULL, locals);
                                    if (ir && ir->kind != TYPE_TYPE_PARAM) gret = ir;
                                }
                                if (gret) {
                                    result = emit_boundary_coerce(g, result, map_type(g, gret));
                                    if (gret->kind == TYPE_CLASS &&
                                        method_ret_is_bare_tp(method_sym))
                                        emit_arc_retain(g, result);
                                }
                                int consumes_free_arg =
                                    argc == 1 && call_consumes_free_arg(g->functions[fi].fn);
                                if (consumes_free_arg) {
                                    emit_invalidate_freed_string(g,
                                        expr->call.args.items[0], locals);
                                }
                                emit_extern_call_len_invalidate(g,
                                    g->functions[fi].fn, call_args, argc,
                                    expr, locals);
                                for (int k = 0; k < n_eh_args; k++)
                                    emit_eh_tmp_pop(g);
                                for (int k = 0; k < argc; k++) {
                                    if (!consumes_free_arg || k != 0) {
                                        emit_release_owned_call_temp(g, expr->call.args.items[k],
                                            call_args[k], locals);
                                    }
                                }
                                free(call_args);
                                return result;
                            }
                        }
                    }
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_type_t *recv_ty = infer_expr_type(g, callee->member.object, locals);
            zan_symbol_t *method_sym =
                find_extension_method(g, recv_ty, callee->member.name,
                                      expr->call.args.count,
                                      expr, callee->member.object, locals);
            if (method_sym) {
                int spec = try_method_spec(g, method_sym, expr,
                                           callee->member.object, locals);
                if (spec >= 0)
                    return emit_method_spec_call(g, spec, expr,
                                                 callee->member.object, locals);
                for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                    if (g->functions[fi].sym == method_sym) {
                        int argc = expr->call.args.count + 1;
                        LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                        call_args[0] = emit_arg_typed(g, callee->member.object,
                            method_param_type(g, method_sym, 0), locals);
                        int n_eh_args = 0;
                        for (int k = 0; k < expr->call.args.count; k++) {
                            call_args[k + 1] = emit_arg_typed(g, expr->call.args.items[k],
                                method_param_type_at(g, method_sym, k + 1, expr,
                                                     callee->member.object, locals), locals);
                            n_eh_args += emit_call_arg_eh_push(g, expr->call.args.items[k],
                                                  call_args[k + 1], locals);
                        }
                        const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(g->functions[fi].fn_type)) == LLVMVoidTypeKind) ? "" : "extcall";
                        coerce_args_to_params(g, g->functions[fi].fn_type, call_args, argc);
                        LLVMValueRef result = zan_call2(g->builder, g->functions[fi].fn_type,
                            g->functions[fi].fn, call_args, (unsigned)argc, cn);
                        if (method_ret_is_bare_tp(method_sym)) {
                            zan_type_t *gret = method_ret_type_at(g, method_sym,
                                expr, callee->member.object, locals);
                            if (gret && gret->kind != TYPE_TYPE_PARAM) {
                                result = emit_boundary_coerce(g, result, map_type(g, gret));
                                if (gret->kind == TYPE_CLASS)
                                    emit_arc_retain(g, result);
                            }
                        }
                        for (int k = 0; k < n_eh_args; k++)
                            emit_eh_tmp_pop(g);
                        emit_release_owned_call_temp(g, callee->member.object, call_args[0], locals);
                        for (int k = 0; k < expr->call.args.count; k++) {
                            emit_release_owned_call_temp(g, expr->call.args.items[k],
                                call_args[k + 1], locals);
                        }
                        free(call_args);
                        return result;
                    }
                }
            }
        }

        /* 裸名函数调用：优先查找当前类实例/静态方法，未匹配则查找全局函数 */
        if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER) {
            zan_istr_t fn_name = expr->call.callee->ident.name;

            if (g->current_type_sym) {
                zan_symbol_t *method_sym = resolve_overload_typed(
            g, g->current_type_sym, fn_name, expr, locals);
                    if (method_sym) fill_default_args(g, expr, method_sym);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                if (method_sym) {
                    int spec = try_method_spec(g, method_sym, expr, NULL, locals);
                    if (spec >= 0)
                        return emit_method_spec_call(g, spec, expr, NULL, locals);
                    for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                        if (g->functions[fi].sym == method_sym) {
                            int argc = expr->call.args.count;
                            bool is_static = (method_sym->modifiers & MOD_STATIC) != 0;
                            int extra = is_static ? 0 : 1;
                            int n_eh_args = 0;
                            LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                                (size_t)(argc + extra > 0 ? argc + extra : 1), sizeof(LLVMValueRef));
                            if (!is_static && g->current_this) {
                                LLVMTypeRef this_ty = LLVMGetAllocatedType(g->current_this);
                                LLVMValueRef this_val = LLVMBuildLoad2(g->builder,
                                    this_ty, g->current_this, "this");
                                unsigned np = LLVMCountParamTypes(g->functions[fi].fn_type);
                                if (np > 0) {
                                    LLVMTypeRef *pts = (LLVMTypeRef *)calloc(np, sizeof(LLVMTypeRef));
                                    LLVMGetParamTypes(g->functions[fi].fn_type, pts);
                                    if (LLVMTypeOf(this_val) != pts[0]) {
                                        this_val = LLVMBuildBitCast(g->builder, this_val, pts[0], "thisc");
                                    }
                                    free(pts);
                                }
                                call_args[0] = this_val;
                            }
                            for (int k = 0; k < argc; k++) {
                                call_args[k + extra] = emit_arg_typed(g,
                                    expr->call.args.items[k],
                                    method_param_type_at(g, method_sym, k, expr,
                                                         NULL, locals),
                                    locals);
                                n_eh_args += emit_call_arg_eh_push(g,
                                    expr->call.args.items[k],
                                    call_args[k + extra], locals);
                            }
                            LLVMTypeRef mft = g->functions[fi].fn_type;
                            LLVMValueRef mfn = route_generic_method(g, g->cur_inst,
                                method_sym, g->functions[fi].fn, mft, &mft);
                            if (g->runtime_checks && g->current_fn &&
                                sym_declares_extern(method_sym)) {
                                for (int k = 0; k < argc; k++) {
                                    if (LLVMGetTypeKind(LLVMTypeOf(call_args[k + extra]))
                                            != LLVMPointerTypeKind) continue;
                                    if (!is_string_expr(g,
                                            expr->call.args.items[k], locals))
                                        continue;
                                    LLVMValueRef isnull = zan_icmp(g->builder,
                                        LLVMIntEQ, call_args[k + extra],
                                        LLVMConstNull(LLVMTypeOf(call_args[k + extra])),
                                        "extarg.null");
                                    emit_runtime_check(g, isnull, expr->loc,
                                        "null string passed to an extern function");
                                    call_args[k + extra] = LLVMBuildSelect(g->builder,
                                        isnull,
                                        zan_irgen_intern_string(g, ""),
                                        call_args[k + extra], "extarg.safe");
                                }
                            }
                            const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "bcall";
                            LLVMValueRef result = emit_dispatch_call(g,
                                is_static ? NULL : g->current_type_sym, method_sym,
                                mfn, mft, call_args, argc + extra, cn);
                            int consumes_free_arg =
                                argc == 1 && call_consumes_free_arg(g->functions[fi].fn);
                            if (consumes_free_arg) {
                                emit_invalidate_freed_string(g,
                                    expr->call.args.items[0], locals);
                            }
                            emit_extern_call_len_invalidate(g,
                                g->functions[fi].fn, call_args, argc + extra,
                                expr, locals);
                            for (int k = 0; k < n_eh_args; k++)
                                emit_eh_tmp_pop(g);
                            for (int k = 0; k < argc; k++) {
                                if (!consumes_free_arg || k != 0) {
                                    emit_release_owned_call_temp(g, expr->call.args.items[k],
                                        call_args[k + extra], locals);
                                }
                            }
                            free(call_args);
                            return result;
                        }
                    }
                }
            }

            char name_buf[256];
            int nlen = fn_name.len < 255 ? fn_name.len : 255;
            memcpy(name_buf, fn_name.str, (size_t)nlen);
            name_buf[nlen] = '\0';
            LLVMValueRef global_fn = LLVMGetNamedFunction(g->mod, name_buf);
            if (global_fn) {
                int argc = expr->call.args.count;
                int n_eh_args = 0;
                LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                    (size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                LLVMTypeRef fn_type = LLVMGlobalGetValueType(global_fn);
                unsigned nparams = LLVMCountParamTypes(fn_type);
                LLVMTypeRef *ptypes = nparams > 0
                    ? (LLVMTypeRef *)calloc(nparams, sizeof(LLVMTypeRef))
                    : NULL;
                if (!ptypes) nparams = 0;
                else LLVMGetParamTypes(fn_type, ptypes);
                for (int k = 0; k < argc; k++) {
                    call_args[k] = emit_expr(g, expr->call.args.items[k], locals);
                    n_eh_args += emit_call_arg_eh_push(g, expr->call.args.items[k],
                                          call_args[k], locals);
                    if ((unsigned)k >= nparams) { continue; }
                    LLVMTypeRef at = LLVMTypeOf(call_args[k]);
                    LLVMTypeRef pt = ptypes[k];
                    if (at == pt) { continue; }
                    if (LLVMGetTypeKind(at) == LLVMIntegerTypeKind
                        && LLVMGetTypeKind(pt) == LLVMIntegerTypeKind) {
                        unsigned aw = LLVMGetIntTypeWidth(at);
                        unsigned pw = LLVMGetIntTypeWidth(pt);
                        if (aw < pw) {
                            call_args[k] = (aw == 1)
                                ? LLVMBuildZExt(g->builder, call_args[k], pt,
                                                "arg.zext")
                                : LLVMBuildSExt(g->builder, call_args[k], pt,
                                                "arg.sext");
                        } else if (aw > pw) {
                            call_args[k] = LLVMBuildTrunc(g->builder,
                                call_args[k], pt, "arg.trunc");
                        }
                    } else if (LLVMGetTypeKind(at) == LLVMPointerTypeKind
                        && LLVMGetTypeKind(pt) == LLVMPointerTypeKind) {
                        call_args[k] = LLVMBuildBitCast(g->builder,
                            call_args[k], pt, "arg.ptrc");
                    }
                }
                if (LLVMIsFunctionVarArg(fn_type)) {
                    for (int k = (int)nparams; k < argc; k++) {
                        LLVMTypeRef at = LLVMTypeOf(call_args[k]);
                        if (LLVMGetTypeKind(at) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(at) < 32)
                            call_args[k] =
                                (LLVMGetIntTypeWidth(at) == 1)
                                ? LLVMBuildZExt(g->builder, call_args[k],
                                    LLVMInt32TypeInContext(g->ctx), "va.prom")
                                : LLVMBuildSExt(g->builder, call_args[k],
                                    LLVMInt32TypeInContext(g->ctx), "va.prom");
                        else if (LLVMGetTypeKind(at) == LLVMFloatTypeKind)
                            call_args[k] = LLVMBuildFPExt(g->builder,
                                call_args[k],
                                LLVMDoubleTypeInContext(g->ctx), "va.prom");
                    }
                }
                if (g->runtime_checks && g->current_fn) {
                    for (int k = 0; k < argc; k++) {
                        if (LLVMGetTypeKind(LLVMTypeOf(call_args[k]))
                                != LLVMPointerTypeKind) continue;
                        if (!is_string_expr(g, expr->call.args.items[k], locals))
                            continue;
                        LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ,
                            call_args[k], LLVMConstNull(LLVMTypeOf(call_args[k])),
                            "extarg.null");
                        emit_runtime_check(g, isnull, expr->loc,
                            "null string passed to an extern function");
                        call_args[k] = LLVMBuildSelect(g->builder, isnull,
                            zan_irgen_intern_string(g, ""),
                            call_args[k], "extarg.safe");
                    }
                }
                const char *gcn = (LLVMGetTypeKind(LLVMGetReturnType(fn_type)) == LLVMVoidTypeKind) ? "" : "gcall";
                LLVMValueRef result = zan_call2(g->builder, fn_type,
                    global_fn, call_args, (unsigned)argc, gcn);
                int consumes_free_arg =
                    argc == 1 && call_consumes_free_arg(global_fn);
                if (consumes_free_arg) {
                    emit_invalidate_freed_string(g,
                        expr->call.args.items[0], locals);
                }
                emit_extern_call_len_invalidate(g, global_fn, call_args, argc,
                                               expr, locals);
                for (int k = 0; k < n_eh_args; k++)
                    emit_eh_tmp_pop(g);
                for (int k = 0; k < argc; k++) {
                    if (!consumes_free_arg || k != 0) {
                        emit_release_owned_call_temp(g, expr->call.args.items[k],
                            call_args[k], locals);
                    }
                }
                free(ptypes);
                free(call_args);
                return result;
            }

            if (g->current_type_sym &&
                (g->current_type_sym->kind == SYM_CLASS ||
                 g->current_type_sym->kind == SYM_STRUCT)) {
                zan_symbol_t *cur = g->current_type_sym;
                int member_found = 0;
                while (cur && !member_found) {
                    for (int mi = 0; mi < cur->member_count; mi++) {
                        zan_symbol_t *m = cur->members[mi];
                        if (m && m->name.len == fn_name.len &&
                            memcmp(m->name.str, fn_name.str,
                                   (size_t)fn_name.len) == 0) {
                            member_found = 1;
                            break;
                        }
                    }
                    zan_symbol_t *base = (cur->type && cur->type->base_type)
                        ? cur->type->base_type->sym : NULL;
                    cur = (base && base != cur) ? base : NULL;
                }
                if (!member_found && !local_find(locals, expr->call.callee->ident.name)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3009,
                        "'%.*s' has no member '%.*s'",
                        (int)g->current_type_sym->name.len,
                        g->current_type_sym->name.str,
                        (int)fn_name.len, fn_name.str);
                }
            }
        }

        /* Robustness: a call `obj */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_symbol_t *recv_cls = expr_class_sym(g, callee->member.object, locals);
            if (!recv_cls && callee->member.object->kind == AST_IDENTIFIER &&
                !local_find(locals, callee->member.object->ident.name)) {
                zan_symbol_t *ts = zan_binder_lookup(g->binder, callee->member.object->ident.name);
                if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT)) recv_cls = ts;
            }
            if (recv_cls && (recv_cls->kind == SYM_CLASS || recv_cls->kind == SYM_STRUCT)) {
                zan_istr_t mn = callee->member.name;
                int found = 0;
                zan_symbol_t *cur = recv_cls;
                while (cur && !found) {
                    for (int mi = 0; mi < cur->member_count; mi++) {
                        zan_symbol_t *m = cur->members[mi];
                        if (m && m->name.len == mn.len &&
                            memcmp(m->name.str, mn.str, mn.len) == 0) { found = 1; break; }
                    }
                    zan_symbol_t *base = (cur->type && cur->type->base_type)
                        ? cur->type->base_type->sym : NULL;
                    cur = (base && base != cur) ? base : NULL;
                }
                if (!found) {
                    zan_istr_t ns = recv_cls->decl
                        ? zan_ast_ns_name(recv_cls->decl) : (zan_istr_t){0};
                    if (ns.len)
                        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3063,
                            "type '%.*s.%.*s' has no member '%.*s'",
                            (int)ns.len, ns.str,
                            (int)recv_cls->name.len, recv_cls->name.str,
                            (int)mn.len, mn.str);
                    else
                        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3009,
                            "'%.*s' has no member '%.*s'",
                            (int)recv_cls->name.len, recv_cls->name.str,
                            (int)mn.len, mn.str);
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            is_name_path(expr->call.callee->member.object)) {
            zan_ast_node_t *obj = expr->call.callee->member.object;
            zan_ast_node_t *head = name_path_head(obj);
            if (head && !local_find(locals, head->ident.name)) {
                zan_istr_t on = (obj->kind == AST_IDENTIFIER)
                    ? obj->ident.name : obj->member.name;
                zan_istr_t mn = expr->call.callee->member.name;
                zan_symbol_t *osym = zan_binder_lookup(g->binder, on);
                /* 限定名路径与类型命名空间对不上（例如把 System.Csv.Csv 写成
                 * System.Text.Csv）：当未解析处理，别按最后一段猜一个类。 */
                if (osym && obj->kind == AST_MEMBER_ACCESS &&
                    !qpath_ns_ok(g, obj, osym))
                    osym = NULL;
                char path[256];
                int plen = format_name_path(expr->call.callee, path,
                                            (int)sizeof(path));
                bool enum_const_recv = false;
                if (!osym && obj->kind == AST_MEMBER_ACCESS &&
                    obj->member.object->kind == AST_IDENTIFIER) {
                    zan_symbol_t *es = zan_binder_lookup(g->binder,
                        obj->member.object->ident.name);
                    if (es && es->kind == SYM_ENUM) {
                        for (int ei = 0; ei < es->member_count; ei++) {
                            zan_symbol_t *em = es->members[ei];
                            if (em && em->kind == SYM_ENUM_MEMBER &&
                                em->name.len == obj->member.name.len &&
                                memcmp(em->name.str, obj->member.name.str,
                                       (size_t)obj->member.name.len) == 0) {
                                enum_const_recv =
                                    (mn.len == 8 &&
                                     memcmp(mn.str, "ToString", 8) == 0) ||
                                    (mn.len == 8 &&
                                     memcmp(mn.str, "TryParse", 8) == 0);
                                break;
                            }
                        }
                    }
                }
                if (!osym && !enum_const_recv) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3064,
                        "unresolved call '%.*s': '%.*s' is not a known variable, "
                        "type, or namespace (is the class imported and registered "
                        "in the stdlib map?)",
                        plen, path, (int)on.len, on.str);
                }
                else if (osym && (osym->kind == SYM_CLASS || osym->kind == SYM_STRUCT) &&
                         !get_method_sym(osym, mn)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3065,
                        "unresolved call '%.*s': type '%.*s' has no method "
                        "'%.*s'",
                        plen, path, (int)on.len, on.str, (int)mn.len, mn.str);
                }
            }
        }

        /* Robustness: `recv */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *bcallee = expr->call.callee;
            zan_istr_t bmn = bcallee->member.name;
            zan_type_t *brt = infer_expr_type(g, bcallee->member.object, locals);
            const char *brecv = NULL;
            if (brt && brt->kind == TYPE_STRING) brecv = "string";
            else if (brt && type_named(brt, "List", 4)) brecv = "List";
            else if (brt && type_named(brt, "Dict", 4)) brecv = "Dict";
            else if (brt && type_named(brt, "StringBuilder", 13))
                brecv = "StringBuilder";
            if (brecv && !zan_builtin_has_member(brecv, bmn.str, (int)bmn.len)) {
                const zan_builtin_type_t *bt = zan_builtin_find(brecv);
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3066,
                    "'%s' has no member '%.*s'",
                    bt ? bt->display : brecv, (int)bmn.len, bmn.str);
            }
            else if (brecv &&
                     zan_builtin_member_kind(brecv, bmn.str, (int)bmn.len) == 'P') {
                const zan_builtin_type_t *bt = zan_builtin_find(brecv);
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3067,
                    "'%s.%.*s' is a property, not a method: drop the '()'",
                    bt ? bt->display : brecv, (int)bmn.len, bmn.str);
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS)
            diagnose_unresolved_static_chain(g, expr->call.callee->member.object);

        if (!zan_diag_has_errors(g->diag)) {
            if (!call_receiver_is_open_generic(g, expr, locals))
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3068,
                              "this call could not be resolved to any callable "
                              "(no method, delegate, operator or builtin matches)");
            else if (g->runtime_checks && g->current_fn) {
                /* A494：这个调用点只在"直接实例化开放泛型模板"时才会执行
                 * （真身由派生类特化补出，模板体没有运行期实现）。原行为
                 * 是静默返回默认值——写库 0 行、查表 null，比编译失败更
                 * 危险。这里响亮报告：软诊断口径（每位点一次，
                 * --strict-runtime exit 70），返回值仍走同型默认值，
                 * 特化出的真身不经过这里。 */
                zan_istr_t cname = { NULL, 0 };
                if (expr->call.callee) {
                    if (expr->call.callee->kind == AST_MEMBER_ACCESS)
                        cname = expr->call.callee->member.name;
                    else if (expr->call.callee->kind == AST_IDENTIFIER)
                        cname = expr->call.callee->ident.name;
                }
                char msg[280];
                if (cname.str && cname.len > 0)
                    snprintf(msg, sizeof(msg),
                             "call '%.*s' has no runtime body: resolvable "
                             "only after generic specialization; instantiate "
                             "the template via a derived class "
                             "(default value returned)",
                             (int)cname.len, cname.str);
                else
                    snprintf(msg, sizeof(msg),
                             "call has no runtime body: resolvable only "
                             "after generic specialization; instantiate the "
                             "template via a derived class (default value "
                             "returned)");
                if (!g->rt_guard_split) {
                    char buf[420];
                    const char *file0 = loc_site_file(g, expr->loc);
                    if (g->hide_diagnostics)
                        snprintf(buf, sizeof(buf), "%s: runtime error: %s\n",
                                 file0, msg);
                    else
                        snprintf(buf, sizeof(buf),
                                 "%s:%u:%u: runtime error: %s\n",
                                 file0, expr->loc.line, expr->loc.col, msg);
                    emit_guard_report_merged(g,
                        zan_irgen_intern_string(g, buf));
                } else {
                    LLVMValueRef file_gv = zan_irgen_intern_string(g,
                        loc_site_file(g, expr->loc));
                    char mbuf[300];
                    snprintf(mbuf, sizeof(mbuf), "%s\n", msg);
                    LLVMValueRef msg_gv = zan_irgen_intern_string(g, mbuf);
                    emit_guard_report3(g, file_gv,
                        g->hide_diagnostics ? 0 : (uint32_t)expr->loc.line,
                        g->hide_diagnostics ? 0 : (uint32_t)expr->loc.col,
                        msg_gv);
                }
            }
        }
        return emit_unresolved_call_result(g, expr, locals);
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}
