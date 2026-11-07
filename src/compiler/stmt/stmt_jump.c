    case AST_CHECKED_STMT: {
        int saved = g->irgen_checked_depth;
        g->irgen_checked_depth = stmt->checked_stmt.checked ? saved + 1
                                                            : saved - 1;
        emit_stmt(g, stmt->checked_stmt.body, locals);
        g->irgen_checked_depth = saved;
        break;
    }

    case AST_LOCK_STMT: {
        g->uses_sync_runtime = true;
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef mon_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                              &i8ptr, 1, 0);
        LLVMValueRef enter_fn = LLVMGetNamedFunction(g->mod, "zan_monitor_enter");
        if (!enter_fn)
            enter_fn = LLVMAddFunction(g->mod, "zan_monitor_enter", mon_ty);
        LLVMValueRef exit_fn = LLVMGetNamedFunction(g->mod, "zan_monitor_exit");
        if (!exit_fn)
            exit_fn = LLVMAddFunction(g->mod, "zan_monitor_exit", mon_ty);

        LLVMValueRef obj = emit_expr(g, stmt->lock_stmt.expr, locals);
        LLVMTypeRef ot = LLVMTypeOf(obj);
        if (LLVMGetTypeKind(ot) == LLVMIntegerTypeKind)
            obj = LLVMBuildIntToPtr(g->builder, obj, i8ptr, "lockp");
        else if (LLVMGetTypeKind(ot) == LLVMPointerTypeKind && ot != i8ptr)
            obj = LLVMBuildBitCast(g->builder, obj, i8ptr, "lockp");
        LLVMValueRef obj_slot = emit_entry_alloca(g, i8ptr, "lock.slot");
        zan_store_fit(g, obj, obj_slot);
        zan_call2(g->builder, mon_ty, enter_fn, &obj, 1, "");
        int lock_fin = -1;
        if (ZAN_TAB_ENSURE(g->finallys, g->finally_count, g->finally_cap, 16)) {
            lock_fin = g->finally_count++;
            g->finallys[lock_fin].body = NULL;
            g->finallys[lock_fin].monitor_obj = obj_slot;
            g->finallys[lock_fin].pending_parent = g->pending.scope;
            g->finallys[lock_fin].pending_scope = NULL;
            g->finallys[lock_fin].continuation_slot = NULL;
            g->finallys[lock_fin].shared = NULL;
            g->finallys[lock_fin].outer_armed_depth = g->eh_armed_count;
            g->finallys[lock_fin].outer_throw_locals_base = g->throw_locals_base;
            g->finallys[lock_fin].outer_throw_catch_base = g->throw_catch_base;
            g->finallys[lock_fin].in_try_body = false;
        } else {
            zan_diag_emit_code(g->diag, DIAG_ERROR, stmt->loc, 3080,
                "too many nested lock/try-finally blocks in one function body");
        }
        emit_stmt(g, stmt->lock_stmt.body, locals);
        if (lock_fin >= 0) g->finally_count = lock_fin;
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            (void)exit_fn;
            emit_monitor_exit(g, obj_slot);
        }
        break;
    }

    case AST_LABEL_STMT: {
        int li = irgen_goto_label_idx(g, stmt->ident.name);
        if (li < 0) break;
        if (g->goto_labels[li].defined) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, stmt->loc, 3081,
                "duplicate label '%.*s' in this function",
                (int)stmt->ident.name.len, stmt->ident.name.str);
        } else {
            g->goto_labels[li].defined = 1;
            g->goto_labels[li].pending_scope = g->pending.scope;
            g->goto_labels[li].fin_depth = g->finally_count;
            g->goto_labels[li].eh_armed_base = g->eh_armed_count;
            g->goto_labels[li].catch_base = g->catch_cleanup_count;
            g->goto_labels[li].locals_base = locals->count;
            g->goto_labels[li].locals_owned =
                irgen_goto_count_owned_locals(locals);
            LLVMValueRef lfn = g->goto_labels[li].fn;
            int lab_fin = g->goto_labels[li].fin_depth;
            int lab_eh = g->goto_labels[li].eh_armed_base;
            int lab_catch = g->goto_labels[li].catch_base;
            LLVMBasicBlockRef resume_bb = LLVMGetInsertBlock(g->builder);
            for (int fi = 0; fi < g->goto_fixup_count; fi++) {
                struct zan_goto_fixup *f = &g->goto_fixups[fi];
                if (f->resolved || f->fn != lfn ||
                    f->label_owner != g->goto_labels[li].label_owner ||
                    f->name.len != stmt->ident.name.len ||
                    memcmp(f->name.str, stmt->ident.name.str,
                           (size_t)stmt->ident.name.len) != 0)
                    continue;
                f->resolved = 1;
                struct zan_goto_fixup source_fixup = *f;
                f = &source_fixup;
                if (pending_scope_common(f->pending.scope, g->pending.scope) !=
                        g->pending.scope ||
                    f->fin_depth < lab_fin || f->eh_armed_base < lab_eh ||
                    f->catch_base < lab_catch) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, f->loc, 3082,
                        "goto '%.*s' jumps into a try/catch/finally or lock "
                        "block; C# forbids jumping into one -- restructure "
                        "with break/return",
                        (int)stmt->ident.name.len, stmt->ident.name.str);
                } else if (f->locals_base < locals->count ||
                           f->locals_owned !=
                               g->goto_labels[li].locals_owned) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, f->loc, 3083,
                        "goto '%.*s' leaves (or enters) a scope with owning "
                        "locals; the abandoned references would leak -- "
                        "restructure with break/return",
                        (int)stmt->ident.name.len, stmt->ident.name.str);
                } else if (f->fin_depth > lab_fin || f->catch_base > lab_catch ||
                           f->eh_armed_base > lab_eh ||
                           f->pending.scope != g->pending.scope) {
                    zan_irgen_pending_context_t saved_pending = g->pending;
                    LLVMBasicBlockRef saved_break = g->break_target;
                    LLVMBasicBlockRef saved_continue = g->continue_target;
                    int saved_throw_base = g->throw_locals_base;
                    int saved_throw_cbase = g->throw_catch_base;
                    int saved_loop_base = g->loop_locals_base;
                    int saved_break_base = g->break_locals_base;
                    int saved_loop_cbase = g->loop_catch_base;
                    int saved_fin_lb = g->finally_loop_base;
                    int saved_eh_lb = g->eh_armed_loop_base;
                    int saved_checked = g->irgen_checked_depth;
                    void *saved_armed = finally_shared_copy(g, g->eh_armed,
                        sizeof(g->eh_armed[0]) * (size_t)lab_eh);
                    g->pending = f->pending;
                    g->break_target = f->break_target;
                    g->continue_target = f->continue_target;
                    g->throw_locals_base = f->throw_locals_base;
                    g->throw_catch_base = f->throw_catch_base;
                    g->loop_locals_base = f->loop_locals_base;
                    g->break_locals_base = f->break_locals_base;
                    g->loop_catch_base = f->loop_catch_base;
                    g->finally_loop_base = f->finally_loop_base;
                    g->eh_armed_loop_base = f->eh_armed_loop_base;
                    g->irgen_checked_depth = f->checked_depth;
                    void *saved_catches = finally_shared_copy(g, g->catch_cleanups,
                        sizeof(g->catch_cleanups[0]) * (size_t)lab_catch);
                    if (f->catch_base) memcpy(g->catch_cleanups, f->catch_snap,
                        sizeof(g->catch_cleanups[0]) * (size_t)f->catch_base);
                    g->catch_cleanup_count = f->catch_base;
                    g->eh_armed_count = f->eh_armed_base;
                    for (int ai = 0; ai < f->eh_armed_base; ai++)
                        g->eh_armed[ai].old_top_slot = f->armed_snap[ai];
                    LLVMPositionBuilderAtEnd(g->builder, f->from_bb);
                    if (LLVMGetBasicBlockTerminator(f->from_bb))
                        LLVMInstructionEraseFromParent(
                            LLVMGetBasicBlockTerminator(f->from_bb));
                    if (f->fin_depth > lab_fin && f->finally_snap &&
                        f->finally_snap_n >= f->fin_depth) {
                        memcpy(g->finallys + lab_fin,
                               f->finally_snap + lab_fin,
                               sizeof(g->finallys[0]) *
                                   (size_t)(f->fin_depth - lab_fin));
                        g->finally_count = f->fin_depth;
                        emit_pending_finallys(g, locals, lab_fin, false);
                        g->finally_count = lab_fin;
                    } else if (f->fin_depth > lab_fin) {
                        zan_diag_emit_code(g->diag, DIAG_ERROR, f->loc, 3084,
                            "goto '%.*s' leaves %d finally/lock region(s) "
                            "whose exits cannot be run -- restructure with "
                            "break/return",
                            (int)stmt->ident.name.len, stmt->ident.name.str,
                            f->fin_depth - lab_fin);
                    }
                    if (!LLVMGetBasicBlockTerminator(
                            LLVMGetInsertBlock(g->builder)) &&
                        f->catch_base > lab_catch) {
                        if (f->catch_snap && f->catch_snap_n >= f->catch_base) {
                            memcpy(g->catch_cleanups + lab_catch,
                                   f->catch_snap + lab_catch,
                                   sizeof(g->catch_cleanups[0]) *
                                       (size_t)(f->catch_base - lab_catch));
                            g->catch_cleanup_count = f->catch_base;
                            emit_release_active_catch_excs(g, lab_catch);
                            g->catch_cleanup_count = lab_catch;
                        }
                    }
                    if (!LLVMGetBasicBlockTerminator(
                            LLVMGetInsertBlock(g->builder)) &&
                        f->eh_armed_base > lab_eh) {
                        g->eh_armed_count = f->eh_armed_base;
                        emit_eh_disarm_from(g, lab_eh);
                        g->eh_armed_count = lab_eh;
                    }
                    if (!LLVMGetBasicBlockTerminator(
                            LLVMGetInsertBlock(g->builder))) {
                        emit_pending_scope_exit(g, saved_pending.scope, false);
                        LLVMBuildBr(g->builder, g->goto_labels[li].bb);
                    }
                    if (lab_eh) memcpy(g->eh_armed, saved_armed,
                        sizeof(g->eh_armed[0]) * (size_t)lab_eh);
                    g->eh_armed_count = lab_eh;
                    if (lab_catch) memcpy(g->catch_cleanups, saved_catches,
                        sizeof(g->catch_cleanups[0]) * (size_t)lab_catch);
                    g->catch_cleanup_count = lab_catch;
                    g->pending = saved_pending;
                    g->break_target = saved_break;
                    g->continue_target = saved_continue;
                    g->throw_locals_base = saved_throw_base;
                    g->throw_catch_base = saved_throw_cbase;
                    g->loop_locals_base = saved_loop_base;
                    g->break_locals_base = saved_break_base;
                    g->loop_catch_base = saved_loop_cbase;
                    g->finally_loop_base = saved_fin_lb;
                    g->eh_armed_loop_base = saved_eh_lb;
                    g->irgen_checked_depth = saved_checked;
                }
            }
            LLVMPositionBuilderAtEnd(g->builder, resume_bb);
        }
        LLVMBasicBlockRef bb = g->goto_labels[li].bb;
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
            LLVMBuildBr(g->builder, bb);
        LLVMPositionBuilderAtEnd(g->builder, bb);
        break;
    }

    case AST_GOTO_STMT: {
        LLVMValueRef gfn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        int li = irgen_goto_label_idx(g, stmt->ident.name);
        if (li < 0) break;
        LLVMBasicBlockRef bb = g->goto_labels[li].bb;
        if (g->goto_labels[li].defined) {
            if (pending_scope_common(g->pending.scope,
                    g->goto_labels[li].pending_scope) != g->goto_labels[li].pending_scope) {
                zan_diag_emit_code(g->diag, DIAG_ERROR, stmt->loc, 3085,
                    "goto '%.*s' jumps into a finally body",
                    (int)stmt->ident.name.len, stmt->ident.name.str);
                break;
            }
            emit_pending_finallys(g, locals, g->goto_labels[li].fin_depth, false);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                /* 同上：catch 异常先出栈，局部变量后出栈 */
                emit_release_active_catch_excs(
                    g, g->goto_labels[li].catch_base);
                emit_release_owned_locals_range(
                    g, locals, g->goto_labels[li].locals_base);
                emit_eh_disarm_from(g, g->goto_labels[li].eh_armed_base);
                emit_pending_scope_exit(g, g->goto_labels[li].pending_scope, false);
                LLVMBuildBr(g->builder, bb);
            }
        } else {
            if (ZAN_TAB_ENSURE(g->goto_fixups, g->goto_fixup_count,
                               g->goto_fixup_cap, 8)) {
                struct zan_goto_fixup *f =
                    &g->goto_fixups[g->goto_fixup_count++];
                memset(f, 0, sizeof(*f));
                f->name = stmt->ident.name;
                f->fn = gfn;
                f->loc = stmt->loc;
                f->label_owner = g->goto_labels[li].label_owner;
                f->pending = g->pending;
                f->break_target = g->break_target;
                f->continue_target = g->continue_target;
                f->throw_locals_base = g->throw_locals_base;
                f->throw_catch_base = g->throw_catch_base;
                f->loop_locals_base = g->loop_locals_base;
                f->break_locals_base = g->break_locals_base;
                f->loop_catch_base = g->loop_catch_base;
                f->finally_loop_base = g->finally_loop_base;
                f->eh_armed_loop_base = g->eh_armed_loop_base;
                f->checked_depth = g->irgen_checked_depth;
                f->armed_snap = zan_arena_alloc(g->arena,
                    sizeof(*f->armed_snap) * (size_t)g->eh_armed_count);
                for (int ai = 0; ai < g->eh_armed_count; ai++)
                    f->armed_snap[ai] = g->eh_armed[ai].old_top_slot;
                f->fin_depth = g->finally_count;
                f->eh_armed_base = g->eh_armed_count;
                f->catch_base = g->catch_cleanup_count;
                f->locals_base = locals->count;
                f->locals_owned = irgen_goto_count_owned_locals(locals);
                f->from_bb = LLVMGetInsertBlock(g->builder);
                f->finally_snap_n = g->finally_count;
                if (f->finally_snap_n > 0) {
                    f->finally_snap = zan_arena_alloc(g->arena,
                        sizeof(*f->finally_snap) * (size_t)f->finally_snap_n);
                    if (f->finally_snap)
                        memcpy(f->finally_snap, g->finallys,
                               sizeof(*f->finally_snap) *
                                   (size_t)f->finally_snap_n);
                }
                f->catch_snap_n = g->catch_cleanup_count;
                if (f->catch_snap_n > 0) {
                    f->catch_snap = zan_arena_alloc(g->arena,
                        sizeof(*f->catch_snap) * (size_t)f->catch_snap_n);
                    if (f->catch_snap)
                        memcpy(f->catch_snap, g->catch_cleanups,
                               sizeof(*f->catch_snap) * (size_t)f->catch_snap_n);
                }
            }
            LLVMBuildBr(g->builder, bb);
        }
        LLVMBasicBlockRef cont =
            LLVMAppendBasicBlockInContext(g->ctx, gfn, "goto.cont");
        LLVMPositionBuilderAtEnd(g->builder, cont);
        break;
    }

    case AST_THROW_STMT: {
        bool rethrow = stmt->throw_stmt.value == NULL;
        if (rethrow) {
            LLVMValueRef hslot = g->catch_cleanup_count > 0
                ? g->catch_cleanups[g->catch_cleanup_count - 1].exc_slot : NULL;
            LLVMBasicBlockRef hbb = hslot ? LLVMGetInstructionParent(hslot) : NULL;
            if (!hbb || LLVMGetBasicBlockParent(hbb) !=
                    LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder))) {
                zan_diag_emit_code(g->diag, DIAG_ERROR, stmt->loc, 3086,
                    "a rethrow (`throw;`) is only valid inside a catch block");
                break;
            }
        }
        emit_eh_hook_call(g, "__zan_eh_throw");
        LLVMValueRef val = rethrow
            ? NULL : emit_expr(g, stmt->throw_stmt.value, locals);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        {
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef top_g, bufs_g, exc_g;
            get_eh_globals(g, &top_g, &bufs_g, &exc_g);
            if (rethrow) {
                int ci = g->catch_cleanup_count - 1;
                LLVMValueRef exc_slot = g->catch_cleanups[ci].exc_slot;
                LLVMValueRef own_slot = g->catch_cleanups[ci].owned_slot;
                LLVMValueRef tid_slot = g->catch_cleanups[ci].tid_slot;
                LLVMValueRef ev = LLVMBuildLoad2(g->builder, i8ptr, exc_slot, "reth.exc");
                LLVMValueRef ofl = LLVMBuildLoad2(g->builder, i32t, own_slot, "reth.own");
                zan_store_fit(g, ev, exc_g);
                zan_store_fit(g, ofl, get_eh_exc_owned_global(g));
                zan_store_fit(g,
                    LLVMBuildLoad2(g->builder, i8ptr, tid_slot, "reth.tid"),
                    get_eh_exc_tid_global(g));
                LLVMValueRef rfn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
                if (!g->current_async_frame) {
                    LLVMValueRef isown = zan_icmp(g->builder, LLVMIntNE, ofl,
                        LLVMConstInt(i32t, 0, 0), "reth.isown");
                    LLVMBasicBlockRef drop_bb =
                        LLVMAppendBasicBlockInContext(g->ctx, rfn, "reth.drop");
                    LLVMBasicBlockRef dcont_bb =
                        LLVMAppendBasicBlockInContext(g->ctx, rfn, "reth.dcont");
                    LLVMBuildCondBr(g->builder, isown, drop_bb, dcont_bb);
                    LLVMPositionBuilderAtEnd(g->builder, drop_bb);
                    emit_eh_tmp_drop(g, ev);
                    LLVMBuildBr(g->builder, dcont_bb);
                    LLVMPositionBuilderAtEnd(g->builder, dcont_bb);
                }
                zan_store_fit(g, LLVMConstInt(i32t, 0, 0), own_slot);
                goto throw_unwind;
            }
            LLVMValueRef vail = val;
            if (LLVMGetTypeKind(LLVMTypeOf(vail)) != LLVMPointerTypeKind)
                vail = LLVMConstNull(i8ptr);
            else if (LLVMTypeOf(vail) != i8ptr)
                vail = LLVMBuildBitCast(g->builder, vail, i8ptr, "exc.bc");
            zan_store_fit(g, vail, exc_g);
            {
                LLVMValueRef owned_g = get_eh_exc_owned_global(g);
                LLVMValueRef tid_g = get_eh_exc_tid_global(g);
                zan_type_t *tt = infer_expr_type(g, stmt->throw_stmt.value, locals);
                if (tt && is_rc_managed_type(tt) &&
                    LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind) {
                    if (!expr_yields_owned_rc_value(g, stmt->throw_stmt.value, locals))
                        emit_rc_retain_for_type(g, tt, vail);
                    zan_store_fit(g, LLVMConstInt(i32t, 1, 0), owned_g);
                    /* 记录抛出异常类的类型描述符，供 catch 子句按动态类型分发 */
                    if (tt->kind == TYPE_CLASS && tt->sym) {
                        LLVMValueRef tid = get_class_tid_global(g, tt->sym);
                        zan_store_fit(g,
                            LLVMBuildBitCast(g->builder, tid, i8ptr, "tid.bc"),
                            tid_g);
                    } else {
                        zan_store_fit(g, LLVMConstNull(i8ptr), tid_g);
                    }
                } else {
                    zan_store_fit(g, LLVMConstInt(i32t, 0, 0), owned_g);
                    zan_store_fit(g, LLVMConstNull(i8ptr), tid_g);
                }
            }
throw_unwind:
            emit_finallys_left_by_throw(g, locals);
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) break;
            {
                int rel_base = 0;
                LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
                LLVMValueRef cur_fn = cur_bb ? LLVMGetBasicBlockParent(cur_bb) : NULL;
                for (int i = g->lpad_depth - 1; i >= 0; i--) {
                    zan_lpad_entry_t *top = &g->lpad_stack[i];
                    if (top->fn != cur_fn) break;
                    if (top->catch_bb) {
                        rel_base = top->throw_locals_base;
                        break;
                    }
                }
                emit_release_owned_locals_range(g, locals, rel_base);
                emit_clear_owned_locals_range(g, locals, rel_base);
            }
            emit_release_active_catch_excs(g, g->throw_catch_base);
            emit_raise_exception(g);
            break;
        }
        break;
    }
