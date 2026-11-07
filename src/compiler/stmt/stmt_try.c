    case AST_TRY_STMT: {
        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMValueRef top_g, bufs_g, exc_g;
        get_eh_globals(g, &top_g, &bufs_g, &exc_g);
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        LLVMBasicBlockRef try_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "try.body");
        LLVMBasicBlockRef catch_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "try.catch");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "try.end");

        LLVMValueRef eh_tmptop_g = get_eh_tmp_top_global(g);
        int async_hid = -1;
        LLVMValueRef old_top_slot = NULL;
        LLVMValueRef tmp_mark_slot = NULL;
        if (!g->current_async_frame) {
            LLVMValueRef old_top = LLVMBuildLoad2(g->builder, i32t, top_g, "eh.old");
            old_top_slot = emit_entry_alloca(g, i32t, "eh.old.slot");
            zan_store_fit(g, old_top, old_top_slot);
            LLVMValueRef new_top = zan_add(g->builder, old_top,
                LLVMConstInt(i32t, 1, 0), "eh.new");
            zan_store_fit(g, new_top, top_g);

            tmp_mark_slot = emit_entry_alloca(g, i32t, "try.tmp.slot");
            zan_store_fit(g, LLVMBuildLoad2(g->builder, i32t, eh_tmptop_g, "try.tmp.cur"), tmp_mark_slot);
        }
        if (g->current_async_frame) {
            LLVMValueRef hc_ptr = LLVMBuildStructGEP2(g->builder,
                g->current_async_frame_type, g->current_async_frame,
                ASYNC_FRAME_HCOUNT, "eh.hc");
            LLVMValueRef hc = LLVMBuildLoad2(g->builder, i32t, hc_ptr, "eh.hcv");
            zan_store_fit(g, zan_add(g->builder, hc,
                LLVMConstInt(i32t, 1, 0), "eh.hc1"), hc_ptr);
            async_hid = g->current_async_handler_next++;
            /* A549：hid 是编译期常量索引，超容量就是帧布局账目错了——
             * 编译期报错而不是让 GEP 静默越界（容量由 irgen_emit 按发射
             * 上限定，正常永远走不到）。 */
            if (async_hid >= g->current_async_handler_cap) {
                zan_diag_emit_code(g->diag, DIAG_ERROR, stmt->loc, 3075,
                              "internal: async handler id %d exceeds frame "
                              "handler capacity %d", async_hid,
                              g->current_async_handler_cap);
            }
            if (LLVMGetArrayLength(LLVMStructGetTypeAtIndex(
                    g->current_async_frame_type, ASYNC_FRAME_PENDING))) {
                LLVMTypeRef hp_type = LLVMStructGetTypeAtIndex(
                    g->current_async_frame_type, ASYNC_FRAME_HPENDING);
                LLVMValueRef hp_indices[] = { LLVMConstInt(i32t, 0, 0),
                    LLVMConstInt(i32t, (unsigned)async_hid, 0) };
                LLVMValueRef hp = LLVMBuildGEP2(g->builder, hp_type,
                    LLVMBuildStructGEP2(g->builder, g->current_async_frame_type,
                        g->current_async_frame, ASYNC_FRAME_HPENDING, "eh.pending.p"),
                    hp_indices, 2, "eh.pending.slot");
                LLVMBuildStore(g->builder, LLVMBuildLoad2(g->builder, i32t,
                    async_pending_count_ptr(g, g->current_async_frame,
                        g->current_async_frame_type), "eh.pending.depth"), hp);
            }
            if (g->current_async_rearm_switch) {
                int hid = async_hid;
                /* A549：hc 是运行时值，压栈前按容量硬守卫——越界宁可响亮
                 * abort 也不写进帧内相邻字段（对齐 pending.push 的 overflow
                 * 模式；landing 侧迭代早有 select 封顶，这里补齐 push 侧）。 */
                LLVMValueRef hs_cap = LLVMConstInt(i32t,
                    (unsigned)g->current_async_handler_cap, 0);
                LLVMValueRef hsf = LLVMGetBasicBlockParent(
                    LLVMGetInsertBlock(g->builder));
                LLVMBasicBlockRef hs_ok = LLVMAppendBasicBlockInContext(g->ctx,
                    hsf, "eh.hs.ok");
                LLVMBasicBlockRef hs_ovf = LLVMAppendBasicBlockInContext(g->ctx,
                    hsf, "eh.hs.overflow");
                LLVMBuildCondBr(g->builder,
                    zan_icmp(g->builder, LLVMIntULT, hc, hs_cap, "eh.hs.fits"),
                    hs_ok, hs_ovf);
                LLVMPositionBuilderAtEnd(g->builder, hs_ovf);
                LLVMTypeRef abrt_ty = LLVMFunctionType(
                    LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
                zan_call2(g->builder, abrt_ty,
                    get_libc_fn(g, "abort", abrt_ty), NULL, 0, "");
                LLVMBuildUnreachable(g->builder);
                LLVMPositionBuilderAtEnd(g->builder, hs_ok);
                LLVMValueRef hs = LLVMBuildStructGEP2(g->builder,
                    g->current_async_frame_type, g->current_async_frame,
                    ASYNC_FRAME_HSTACK, "eh.hs");
                LLVMValueRef hs_idx[2] = { LLVMConstInt(i32t, 0, 0), hc };
                LLVMValueRef hs_slot = LLVMBuildGEP2(g->builder,
                    LLVMArrayType(i32t, (unsigned)g->current_async_handler_cap),
                    hs, hs_idx, 2, "eh.hs.slot");
                zan_store_fit(g, LLVMConstInt(i32t, (unsigned)hid, 0), hs_slot);

                LLVMBasicBlockRef here = LLVMGetInsertBlock(g->builder);
                struct { const char *nm; LLVMValueRef sw; LLVMBasicBlockRef dst; } re[2] = {
                    { "try.rearm.init", g->current_async_rearm_init_switch,
                      g->current_async_rearm_next_bb },
                    { "try.rearm", g->current_async_rearm_switch, catch_bb }
                };
                for (int ri = 0; ri < 2; ri++) {
                    if (!re[ri].sw) continue;
                    LLVMBasicBlockRef re_bb = LLVMAppendBasicBlockInContext(g->ctx, fn,
                        re[ri].nm);
                    LLVMPositionBuilderAtEnd(g->builder, re_bb);
                    LLVMValueRef rtop = LLVMBuildLoad2(g->builder, i32t, top_g, "eh.rtop");
                    zan_store_fit(g, zan_sub(g->builder, rtop,
                        LLVMConstInt(i32t, 1, 0), "eh.rtop0"), old_top_slot);
                    zan_store_fit(g,
                        LLVMBuildLoad2(g->builder, i32t, eh_tmptop_g, "eh.rtmp"),
                        tmp_mark_slot);
                    LLVMBuildBr(g->builder, re[ri].dst);
                    LLVMAddCase(re[ri].sw, LLVMConstInt(i32t, (unsigned)hid, 0), re_bb);
                }
                LLVMPositionBuilderAtEnd(g->builder, here);
            }

        }
        LLVMBasicBlockRef lpad_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "try.lpad");
        LLVMBuildBr(g->builder, try_bb);

        int ncatch = stmt->try_stmt.catches.count;
        zan_builder_point_t lpad_pt = zan_builder_save(g);
        LLVMPositionBuilderAtEnd(g->builder, lpad_bb);
        ensure_function_personality(g, fn);
        LLVMTypeRef lp_ret_ty = LLVMStructTypeInContext(g->ctx, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0);
        LLVMValueRef lp_slot = emit_entry_alloca(g, lp_ret_ty, "lp.slot");
        LLVMValueRef lp_val = LLVMBuildLandingPad(g->builder, lp_ret_ty, get_eh_personality_fn(g), (ncatch > 0) ? 1 : 0, "lp");
        if (ncatch > 0) {
            LLVMAddClause(lp_val, LLVMConstNull(i8ptr));
        }
        LLVMSetCleanup(lp_val, 1);
        LLVMBuildStore(g->builder, lp_val, lp_slot);
        LLVMBuildBr(g->builder, catch_bb);
        zan_builder_restore(g, lpad_pt);

        LLVMPositionBuilderAtEnd(g->builder, try_bb);
        int try_start = locals->count;
        int saved_throw_base = g->throw_locals_base;
        int saved_throw_cbase = g->throw_catch_base;
        g->throw_locals_base = try_start;
        g->throw_catch_base = g->catch_cleanup_count;
        int fin_idx = -1;
        if (stmt->try_stmt.finally_body &&
            ZAN_TAB_ENSURE(g->finallys, g->finally_count, g->finally_cap, 16)) {
            fin_idx = g->finally_count++;
            g->finallys[fin_idx].body = stmt->try_stmt.finally_body;
            g->finallys[fin_idx].monitor_obj = NULL;
            g->finallys[fin_idx].continuation_slot = NULL;
            g->finallys[fin_idx].shared = NULL;
            g->finallys[fin_idx].pending_parent = g->pending.scope;
            g->finallys[fin_idx].pending_scope = NULL;
            if (g->current_async_frame) {
                zan_irgen_pending_scope_t *scope = zan_arena_alloc(g->arena, sizeof(*scope));
                scope->parent = g->pending.scope;
                scope->handler_id = async_hid;
                g->finallys[fin_idx].pending_scope = scope;
            }
            g->finallys[fin_idx].outer_armed_depth = g->eh_armed_count;
            g->finallys[fin_idx].outer_throw_locals_base = saved_throw_base;
            g->finallys[fin_idx].outer_throw_catch_base = saved_throw_cbase;
            if (g->current_async_frame) {
                local_var_t *selector = local_find_async_decl(locals, stmt);
                if (selector)
                    g->finallys[fin_idx].continuation_slot = selector->alloca;
            }
            g->finallys[fin_idx].in_try_body = true;
        } else if (stmt->try_stmt.finally_body) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, stmt->loc, 3076,
                "too many nested try/finally blocks in one function body");
        }

        int saved_lpad_depth = g->lpad_depth;
        zan_lpad_entry_t *try_lpad_entry = NULL;
        if (g->lpad_depth < ZAN_MAX_ARMED_TRY) {
            try_lpad_entry = &g->lpad_stack[g->lpad_depth];
            try_lpad_entry->lpad_bb = lpad_bb;
            try_lpad_entry->catch_bb = (ncatch > 0) ? catch_bb : NULL;
            try_lpad_entry->fn = fn;
            try_lpad_entry->throw_locals_base = try_start;
            try_lpad_entry->fin_idx = fin_idx;
            try_lpad_entry->cleanups = NULL;
            try_lpad_entry->cleanup_count = 0;
            try_lpad_entry->cleanup_cap = 0;
            g->lpad_depth++;
        }
        int armed_idx = -1;
        if (!g->current_async_frame &&
            ZAN_TAB_ENSURE(g->eh_armed, g->eh_armed_count, g->eh_armed_cap, 16)) {
            armed_idx = g->eh_armed_count++;
            g->eh_armed[armed_idx].old_top_slot = old_top_slot;
            g->eh_armed[armed_idx].restore_tmps = false;
        }
        emit_stmt(g, stmt->try_stmt.try_body, locals);
        if (armed_idx >= 0) g->eh_armed_count = armed_idx;
        if (fin_idx >= 0) g->finallys[fin_idx].in_try_body = false;
        g->throw_locals_base = saved_throw_base;
        g->throw_catch_base = saved_throw_cbase;
        g->lpad_depth = saved_lpad_depth;
        int try_locals_end = locals->count;
        locals->count = try_start;
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            if (!g->current_async_frame && old_top_slot) {
                LLVMValueRef ot = LLVMBuildLoad2(g->builder, i32t, old_top_slot, "eh.old.re");
                zan_store_fit(g, ot, top_g);
            }
            if (g->current_async_frame) {
                LLVMValueRef hc_ptr = LLVMBuildStructGEP2(g->builder,
                    g->current_async_frame_type, g->current_async_frame,
                    ASYNC_FRAME_HCOUNT, "eh.hc");
                LLVMValueRef hc = LLVMBuildLoad2(g->builder, i32t, hc_ptr, "eh.hcv");
                zan_store_fit(g, zan_sub(g->builder, hc,
                    LLVMConstInt(i32t, 1, 0), "eh.hc0"), hc_ptr);
            }
            LLVMBuildBr(g->builder, end_bb);
        }

        LLVMPositionBuilderAtEnd(g->builder, catch_bb);
        if (!g->current_async_frame && old_top_slot) {
            LLVMValueRef ot = LLVMBuildLoad2(g->builder, i32t, old_top_slot, "eh.old.catch");
            zan_store_fit(g, ot, top_g);
        }
        if (!g->current_async_frame && tmp_mark_slot) {
            LLVMValueRef mark = LLVMBuildLoad2(g->builder, i32t, tmp_mark_slot, "try.tmp.mark");
            emit_eh_tmp_unwind_to(g, mark);
        }
        if (!g->current_async_frame && try_lpad_entry) {
            emit_lpad_cleanups(g, try_lpad_entry);
        } else if (g->current_async_frame) {
            LLVMValueRef hc_ptr = LLVMBuildStructGEP2(g->builder,
                g->current_async_frame_type, g->current_async_frame,
                ASYNC_FRAME_HCOUNT, "eh.hc");
            LLVMValueRef hc = LLVMBuildLoad2(g->builder, i32t, hc_ptr, "eh.hcv");
            zan_store_fit(g, zan_sub(g->builder, hc,
                LLVMConstInt(i32t, 1, 0), "eh.hc0"), hc_ptr);
        }
        if (async_hid >= 0 && LLVMGetArrayLength(LLVMStructGetTypeAtIndex(
                g->current_async_frame_type, ASYNC_FRAME_PENDING))) {
            LLVMTypeRef ft = g->current_async_frame_type;
            LLVMTypeRef hp_type = LLVMStructGetTypeAtIndex(ft, ASYNC_FRAME_HPENDING);
            LLVMValueRef indices[] = { LLVMConstInt(i32t, 0, 0),
                LLVMConstInt(i32t, (unsigned)async_hid, 0) };
            LLVMValueRef hp = LLVMBuildGEP2(g->builder, hp_type,
                LLVMBuildStructGEP2(g->builder, ft, g->current_async_frame,
                    ASYNC_FRAME_HPENDING, "eh.pending.p"), indices, 2, "eh.pending.slot");
            emit_async_pending_discard(g, g->current_async_frame, ft,
                LLVMBuildLoad2(g->builder, i32t, hp, "eh.pending.depth"));
        }
        LLVMValueRef exc_val = LLVMBuildLoad2(g->builder, i8ptr, exc_g, "exc");
        LLVMValueRef exc_slot, exc_owned_slot, exc_tid_slot;
        if (g->current_async_frame && async_hid >= 0 &&
            async_hid < g->current_async_handler_cap) {
            LLVMValueRef cidx[2] = { LLVMConstInt(i32t, 0, 0),
                                     LLVMConstInt(i32t, (unsigned)async_hid, 0) };
            zan_builder_point_t pt = zan_builder_save(g);
            LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(
                LLVMGetBasicBlockParent(pt.bb));
            LLVMValueRef entry_term = LLVMGetBasicBlockTerminator(entry_bb);
            if (entry_term) LLVMPositionBuilderBefore(g->builder, entry_term);
            else LLVMPositionBuilderAtEnd(g->builder, entry_bb);
            exc_slot = LLVMBuildGEP2(g->builder,
                LLVMArrayType(i8ptr, (unsigned)g->current_async_handler_cap),
                LLVMBuildStructGEP2(g->builder, g->current_async_frame_type,
                    g->current_async_frame, ASYNC_FRAME_CEXC, "eh.cexc"),
                cidx, 2, "eh.exc.slot");
            exc_owned_slot = LLVMBuildGEP2(g->builder,
                LLVMArrayType(i32t, (unsigned)g->current_async_handler_cap),
                LLVMBuildStructGEP2(g->builder, g->current_async_frame_type,
                    g->current_async_frame, ASYNC_FRAME_CEXC_OWNED, "eh.cexcown"),
                cidx, 2, "eh.excown.slot");
            exc_tid_slot = LLVMBuildGEP2(g->builder,
                LLVMArrayType(i8ptr, (unsigned)g->current_async_handler_cap),
                LLVMBuildStructGEP2(g->builder, g->current_async_frame_type,
                    g->current_async_frame, ASYNC_FRAME_CEXC_TID, "eh.cexctid"),
                cidx, 2, "eh.exctid.slot");
            zan_builder_restore(g, pt);
        } else {
            exc_slot = emit_entry_alloca(g, i8ptr, "eh.exc.slot");
            exc_owned_slot = emit_entry_alloca(g, i32t, "eh.excown.slot");
            exc_tid_slot = emit_entry_alloca(g, i8ptr, "eh.exctid.slot");
        }
        zan_store_fit(g, exc_val, exc_slot);
        zan_store_fit(g, LLVMConstInt(i32t, 0, 0), exc_owned_slot);
        zan_store_fit(g, LLVMConstNull(i8ptr), exc_tid_slot);
        int catch_start = locals->count;
        ncatch = stmt->try_stmt.catches.count;
        if (ncatch > 0) {
            LLVMValueRef tid_g = get_eh_exc_tid_global(g);
            LLVMValueRef thrown_tid = LLVMBuildLoad2(g->builder, i8ptr, tid_g, "exc.tid");
            zan_store_fit(g, thrown_tid, exc_tid_slot);
            LLVMValueRef match_fn = get_eh_tid_match_fn(g);
            LLVMTypeRef match_ty = LLVMFunctionType(LLVMInt1TypeInContext(g->ctx),
                (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
            LLVMBasicBlockRef done_bb =
                LLVMAppendBasicBlockInContext(g->ctx, fn, "catch.done");
            LLVMBasicBlockRef rethrow_bb =
                LLVMAppendBasicBlockInContext(g->ctx, fn, "catch.rethrow");

            for (int ci = 0; ci < ncatch; ci++) {
                zan_ast_node_t *cc = stmt->try_stmt.catches.items[ci];
                zan_type_t *et = cc->catch_clause.type
                    ? resolve_type_ctx(g, cc->catch_clause.type)
                    : NULL;
                LLVMBasicBlockRef body_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, fn, "catch.body");
                LLVMBasicBlockRef miss_bb = (ci + 1 < ncatch)
                    ? LLVMAppendBasicBlockInContext(g->ctx, fn, "catch.test")
                    : rethrow_bb;
                if (et && et->kind == TYPE_CLASS && et->sym) {
                    LLVMValueRef want = LLVMBuildBitCast(g->builder,
                        get_class_tid_global(g, et->sym), i8ptr, "want.tid");
                    LLVMValueRef hits = zan_call2(g->builder, match_ty, match_fn,
                        (LLVMValueRef[]){ thrown_tid, want }, 2, "tid.hit");
                    LLVMBuildCondBr(g->builder, hits, body_bb, miss_bb);
                } else {
                    LLVMBuildBr(g->builder, body_bb);
                    if (miss_bb != rethrow_bb) {
                        LLVMPositionBuilderAtEnd(g->builder, miss_bb);
                        LLVMBuildBr(g->builder, rethrow_bb);
                    }
                    miss_bb = NULL;
                }

                LLVMPositionBuilderAtEnd(g->builder, body_bb);
                if (cc->catch_clause.var_name.len > 0) {
                    LLVMValueRef ev = LLVMBuildLoad2(g->builder, i8ptr, exc_slot, "exc.re");
                    LLVMValueRef ea = NULL;
                    if (g->current_async_frame) {
                        local_var_t *fv = local_find_async_decl(locals, cc);
                        if (fv && !fv->type &&
                            LLVMGetTypeKind(local_slot_type(g, fv)) == LLVMPointerTypeKind)
                            ea = fv->alloca;
                    }
                    if (!ea) ea = emit_entry_alloca(g, i8ptr, "exc.var");
                    zan_store_fit(g, ev, ea);
                    local_add(locals, cc->catch_clause.var_name, ea, et);
                }
                {
                    LLVMValueRef owned_g2 = get_eh_exc_owned_global(g);
                    LLVMValueRef ofl = LLVMBuildLoad2(g->builder, i32t, owned_g2, "exc.ofl");
                    zan_store_fit(g, ofl, exc_owned_slot);
                    zan_store_fit(g, LLVMConstInt(i32t, 0, 0), owned_g2);
                    if (!g->current_async_frame) {
                        LLVMValueRef isown = zan_icmp(g->builder, LLVMIntNE, ofl,
                            LLVMConstInt(i32t, 0, 0), "exc.isown");
                        LLVMBasicBlockRef push_bb =
                            LLVMAppendBasicBlockInContext(g->ctx, fn, "exc.push");
                        LLVMBasicBlockRef pcont_bb =
                            LLVMAppendBasicBlockInContext(g->ctx, fn, "exc.pcont");
                        LLVMBuildCondBr(g->builder, isown, push_bb, pcont_bb);
                        LLVMPositionBuilderAtEnd(g->builder, push_bb);
                        LLVMValueRef ev2 = LLVMBuildLoad2(g->builder, i8ptr, exc_slot, "exc.re");
                        emit_eh_tmp_push(g, ev2);
                        LLVMBuildBr(g->builder, pcont_bb);
                        LLVMPositionBuilderAtEnd(g->builder, pcont_bb);
                    }
                }
                if (ZAN_TAB_ENSURE(g->catch_cleanups, g->catch_cleanup_count,
                                   g->catch_cleanup_cap, 16)) {
                    g->catch_cleanups[g->catch_cleanup_count].exc_slot = exc_slot;
                    g->catch_cleanups[g->catch_cleanup_count].owned_slot =
                        exc_owned_slot;
                    g->catch_cleanups[g->catch_cleanup_count].tid_slot =
                        exc_tid_slot;
                    g->catch_cleanup_count++;
                } else {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, stmt->loc, 3077,
                        "out of memory tracking nested catch handlers");
                }
                int saved_cc = g->catch_cleanup_count;
                emit_stmt(g, cc->catch_clause.body, locals);
                g->catch_cleanup_count = saved_cc - 1;
                locals->count = catch_start;
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                    LLVMBuildBr(g->builder, done_bb);

                if (!miss_bb) break;
                LLVMPositionBuilderAtEnd(g->builder, miss_bb);
            }
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)) &&
                LLVMGetInsertBlock(g->builder) != rethrow_bb)
                LLVMBuildBr(g->builder, rethrow_bb);

            LLVMPositionBuilderAtEnd(g->builder, rethrow_bb);
            emit_finally_on_exception_path(g, locals, fin_idx);
            emit_finallys_below_for_propagate(g, locals, fin_idx);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                emit_eh_propagate_tail_lpad(g, locals, lp_slot, lp_ret_ty, true);

            LLVMPositionBuilderAtEnd(g->builder, done_bb);
        } else {
            emit_finally_on_exception_path(g, locals, fin_idx);
            emit_finallys_below_for_propagate(g, locals, fin_idx);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                emit_eh_propagate_tail_lpad(g, locals, lp_slot, lp_ret_ty, false);
        }
        locals->count = catch_start;
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            LLVMValueRef cfn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMValueRef hofl = LLVMBuildLoad2(g->builder, i32t, exc_owned_slot,
                "exc.hown");
            LLVMValueRef h_owned = zan_icmp(g->builder, LLVMIntNE, hofl,
                LLVMConstInt(i32t, 0, 0), "exc.hisown");
            LLVMBasicBlockRef hrel_bb = LLVMAppendBasicBlockInContext(g->ctx, cfn, "exc.hrel");
            LLVMBasicBlockRef hcont_bb = LLVMAppendBasicBlockInContext(g->ctx, cfn, "exc.hcont");
            LLVMBuildCondBr(g->builder, h_owned, hrel_bb, hcont_bb);
            LLVMPositionBuilderAtEnd(g->builder, hrel_bb);
            if (!g->current_async_frame) {
                emit_eh_tmp_pop(g);
            }
            LLVMValueRef hev = LLVMBuildLoad2(g->builder, i8ptr, exc_slot, "exc.hre");
            zan_store_fit(g, LLVMConstInt(i32t, 0, 0), exc_owned_slot);
            zan_store_fit(g, LLVMConstNull(i8ptr), exc_slot);
            zan_call2(g->builder,
                LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
                g->rt_release_dyn, &hev, 1, "");
            zan_store_fit(g, LLVMConstNull(i8ptr), exc_g);
            LLVMBuildBr(g->builder, hcont_bb);
            LLVMPositionBuilderAtEnd(g->builder, hcont_bb);
            LLVMBuildBr(g->builder, end_bb);
        }

        if (fin_idx >= 0) g->finally_count = fin_idx;
        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        if (stmt->try_stmt.finally_body) {
            zan_irgen_finally_entry_t entry = g->finallys[fin_idx];
            emit_finally_body(g, locals, &entry);
        }
        if (try_lpad_entry) {
            free(try_lpad_entry->cleanups);
            try_lpad_entry->cleanups = NULL;
            try_lpad_entry->cleanup_count = 0;
            try_lpad_entry->cleanup_cap = 0;
        }
        break;
    }

    case AST_DO_WHILE_STMT: {
        int body_start = locals->count;
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "do.body");
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "do.cond");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "do.end");
        zan_irgen_pending_context_t saved_pending = g->pending;
        LLVMBasicBlockRef saved_break = g->break_target;
        LLVMBasicBlockRef saved_cont = g->continue_target;
        int saved_loop_base = g->loop_locals_base;
        int saved_break_base = g->break_locals_base;
        int saved_loop_cbase = g->loop_catch_base;
        int saved_loop_fbase = g->finally_loop_base;
        int saved_loop_ehbase = g->eh_armed_loop_base;
        g->pending.break_scope = g->pending.scope;
        g->pending.continue_scope = g->pending.scope;
        g->break_target = end_bb;
        g->continue_target = cond_bb;
        g->loop_locals_base = body_start;
        g->break_locals_base = body_start;
        g->loop_catch_base = g->catch_cleanup_count;
        g->finally_loop_base = g->finally_count;
        g->eh_armed_loop_base = g->eh_armed_count;
        LLVMBuildBr(g->builder, body_bb);

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        emit_stmt(g, stmt->while_stmt.body, locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, body_start);
            if (!emit_async_preempt_site(g, cond_bb))
                LLVMBuildBr(g->builder, cond_bb);
        } else {
            emit_release_owned_locals_from(g, locals, body_start);
        }

        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        LLVMValueRef cond = NULL;
        if (stmt->while_stmt.cond && stmt->while_stmt.cond->kind == AST_BLOCK) {
            int block_start = locals->count;
            zan_ast_list_t *stmts = &stmt->while_stmt.cond->block.stmts;
            for (int i = 0; i < stmts->count; i++) {
                if (i == stmts->count - 1 && stmts->items[i]->kind == AST_EXPR_STMT) {
                    cond = emit_expr(g, stmts->items[i]->expr_stmt.expr, locals);
                } else {
                    emit_stmt(g, stmts->items[i], locals);
                    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                        break;
                }
            }
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                emit_release_owned_locals_from(g, locals, block_start);
            locals->count = block_start;
        } else {
            cond = emit_expr(g, stmt->while_stmt.cond, locals);
        }
        cond = zan_tobool(g->builder, cond, "dcond");
        LLVMBuildCondBr(g->builder, cond, body_bb, end_bb);

        g->pending.break_scope = saved_pending.break_scope;
        g->pending.continue_scope = saved_pending.continue_scope;
        g->break_target = saved_break;
        g->continue_target = saved_cont;
        g->loop_locals_base = saved_loop_base;
        g->break_locals_base = saved_break_base;
        g->loop_catch_base = saved_loop_cbase;
        g->finally_loop_base = saved_loop_fbase;
        g->eh_armed_loop_base = saved_loop_ehbase;

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        break;
    }
