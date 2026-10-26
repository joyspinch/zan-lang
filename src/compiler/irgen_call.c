
/* bool -> 托管串（irgen_generics.c）："true" / "false" */
LLVMValueRef emit_bool_to_cstr(zan_irgen_t *g, LLVMValueRef v);

/* A550：定义在 irgen_stmt.c（unity 排序在其后），这里前置声明。 */
static bool call_is_async_callee(zan_irgen_t *g, zan_ast_node_t *call, local_scope_t *locals);

static LLVMValueRef emit_detach_async_call(zan_irgen_t *g, LLVMValueRef sub,
                                           bool keep_result, bool is_explicit_spawn) {
    if (!sub || LLVMGetTypeKind(LLVMTypeOf(sub)) != LLVMPointerTypeKind)
        return NULL;

    /* 直接调用：被调函数是具名符号，按 "<callee>$resume" 直接取 resume 入口。
     *
     * 只有真正的调用指令才能取被调值：async 调用的返回值是协程帧指针
     * （实测是 phi/load），不是 CallInst。
     * LLVMGetCalledValue 只对 CallInst 有效——对别的值它会按 CallInst 的
     * 内存布局去解读操作数数组，拿到的是垃圾指针；紧接着的
     * LLVMGetValueName2 会解引用它，于是 zanc 间歇性访问冲突
     * (0xC0000005)：崩不崩取决于那块内存当时是不是恰好可读。
     *
     * 这条按名字取 resume 的路径对 async 调用其实一直是死路（sub 永远
     * 不是 CallInst，取到的名字是垃圾，LLVMGetNamedFunction 必然返回
     * NULL），真正干活的是下面的 SELF_STEP 兜底。 */
    LLVMValueRef sub_resume = NULL;
    LLVMValueRef callee = LLVMIsACallInst(sub) ? LLVMGetCalledValue(sub) : NULL;
    if (callee) {
        size_t nl = 0;
        const char *cn = LLVMGetValueName2(callee, &nl);
        if (cn && nl != 0 && nl < 240) {
            char rn[256];
            memcpy(rn, cn, nl);
            memcpy(rn + nl, "$resume", 8);
            sub_resume = LLVMGetNamedFunction(g->mod, rn);
        }
    }

    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef sub_i8 = LLVMBuildBitCast(g->builder, sub, i8ptr, "spawn.sub");

    /* 间接调用（接口/虚方法/委托分发）：被调入口不是静态符号，按名字取不到
       $resume。此时从帧头的 SELF_STEP 槽加载本协程的 resume 入口——ramp 里
       一定会写入它，await 的间接分支用的就是同一招（irgen_expr.c）。
       缺了这一步，Task.Spawn/Task.Run 会静默丢弃协程：返回 0 句柄、循环体
       永不执行，而 IsDone(0) 对空帧报"已完成"，症状极难与业务 bug 区分。 */
    if (!sub_resume && is_explicit_spawn) {
        LLVMValueRef ssp = LLVMBuildStructGEP2(g->builder, g->co_header_type,
            sub_i8, ASYNC_FRAME_SELF_STEP, "spawn.selfstep.p");
        sub_resume = LLVMBuildLoad2(g->builder, g->co_step_ptr, ssp,
                                    "spawn.selfstep");
    }
    if (!sub_resume) return NULL;
    if (!keep_result) {
        LLVMTypeRef hdr = g->co_header_type;
        LLVMBuildStore(g->builder, sub_i8,
            LLVMBuildStructGEP2(g->builder, hdr, sub_i8, ASYNC_FRAME_AWAITER, "spawn.aw"));
        LLVMBuildStore(g->builder, get_co_reap_fn(g),
            LLVMBuildStructGEP2(g->builder, hdr, sub_i8, ASYNC_FRAME_AWAITER_STEP, "spawn.aws"));
    }
    LLVMValueRef sched_args[] = { sub_i8, sub_resume };
    zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
    LLVMValueRef track = get_co_track_fn(g);
    zan_call2(g->builder, LLVMGlobalGetValueType(track), track, &sub_i8, 1, "");
    return sub_i8;
}

static LLVMTypeRef get_task_action_frame_type(zan_irgen_t *g) {
    LLVMTypeRef ty = LLVMGetTypeByName2(g->ctx, "zan.co.task_action_frame");
    if (ty) return ty;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef fields[] = {
        i64, g->co_step_ptr,                           /* SCHED, SCHED_STEP */
        i32, i32,                                     /* STATE, DONE */
        i8ptr, g->co_step_ptr, i64,                   /* AWAITER, AWAITER_STEP, RESULT */
        g->co_step_ptr, i32,                          /* CLEANUP, HCOUNT */
        g->co_step_ptr,                               /* SELF_STEP */
        i8ptr, i8ptr, i32,                            /* EXC, TID, EXC_OWNED */
        i32,                                          /* CANCEL */
        i8ptr,                                        /* CHILD */
        i8ptr,                                        /* LNEXT */
        i8ptr                                         /* DELEGATE (slot 16) */
    };
    ty = LLVMStructCreateNamed(g->ctx, "zan.co.task_action_frame");
    LLVMStructSetBody(ty, fields, 17, 0);
    return ty;
}

static LLVMValueRef get_task_action_cleanup_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_task_action_cleanup");
    if (fn) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_task_action_cleanup", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    di_clear(g);
    LLVMValueRef arg = LLVMGetParam(fn, 0);
    LLVMTypeRef ft = get_task_action_frame_type(g);
    LLVMValueRef frame = LLVMBuildBitCast(g->builder, arg, LLVMPointerType(ft, 0), "fr");
    LLVMValueRef dlg_p = LLVMBuildStructGEP2(g->builder, ft, frame, 16, "dlg.p");
    LLVMValueRef dlg = LLVMBuildLoad2(g->builder, i8ptr, dlg_p, "dlg");
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), dlg_p);
    emit_closure_release(g, dlg);
    zan_emit_frame_free(g, arg);
    LLVMBuildRetVoid(g->builder);

    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

static LLVMValueRef get_task_action_step_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_task_action_step");
    if (fn) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_task_action_step", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef exec_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "exec");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    di_clear(g);
    LLVMValueRef arg = LLVMGetParam(fn, 0);
    LLVMTypeRef ft = get_task_action_frame_type(g);
    LLVMValueRef frame = LLVMBuildBitCast(g->builder, arg, LLVMPointerType(ft, 0), "fr");

    LLVMValueRef cancel_p = LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_CANCEL, "cancel.p");
    LLVMValueRef cancel = LLVMBuildLoad2(g->builder, i32, cancel_p, "cancel");
    LLVMValueRef is_cancelled = zan_icmp(g->builder, LLVMIntNE, cancel, LLVMConstInt(i32, 0, 0), "is_cancelled");
    LLVMBuildCondBr(g->builder, is_cancelled, done_bb, exec_bb);

    LLVMPositionBuilderAtEnd(g->builder, exec_bb);
    LLVMValueRef dlg_p = LLVMBuildStructGEP2(g->builder, ft, frame, 16, "dlg.p");
    LLVMValueRef dlg = LLVMBuildLoad2(g->builder, i8ptr, dlg_p, "dlg");
    LLVMValueRef has_dlg = zan_icmp(g->builder, LLVMIntNE, dlg, LLVMConstNull(i8ptr), "has.dlg");

    LLVMBasicBlockRef invoke_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "invoke");
    LLVMBasicBlockRef after_invoke_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "after_invoke");
    LLVMBuildCondBr(g->builder, has_dlg, invoke_bb, after_invoke_bb);

    LLVMPositionBuilderAtEnd(g->builder, invoke_bb);
    LLVMTypeRef void_fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    emit_delegate_invoke(g, dlg, void_fn_ty, LLVMVoidTypeInContext(g->ctx), NULL, 0, "");
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), dlg_p);
    emit_closure_release(g, dlg);
    LLVMBuildBr(g->builder, after_invoke_bb);

    LLVMPositionBuilderAtEnd(g->builder, after_invoke_bb);
    LLVMBuildBr(g->builder, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    emit_co_cancel_delay(g, arg);

    LLVMValueRef res_ptr = LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_RESULT, "fr.result");
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), res_ptr);

    LLVMValueRef done_ptr = LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_DONE, "fr.done");
    LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpXchg, done_ptr,
        LLVMConstInt(i32, 1, 0), LLVMAtomicOrderingRelease, 0);
    LLVMBuildStore(g->builder, LLVMConstInt(i32, -1, 1),
        LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_STATE, "fr.state"));

    {
        LLVMTypeRef jc_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8ptr }, 1, 0);
        LLVMValueRef jc = LLVMGetNamedFunction(g->mod, "zan_join_complete");
        if (!jc) jc = LLVMAddFunction(g->mod, "zan_join_complete", jc_type);
        zan_call2(g->builder, jc_type, jc, &arg, 1, "");
    }

    LLVMTypeRef ptr_int_ty = g->target_is_wasm ? i32 : i64;
    LLVMValueRef aw_ptr = LLVMBuildStructGEP2(g->builder, ft, frame,
        ASYNC_FRAME_AWAITER, "fr.awaiter");
    LLVMValueRef aw_iptr = LLVMBuildBitCast(g->builder, aw_ptr,
        LLVMPointerType(ptr_int_ty, 0), "fr.aw.iptr");
    LLVMValueRef old_aw = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpXchg,
        aw_iptr, LLVMConstInt(ptr_int_ty, 1, 0),
        LLVMAtomicOrderingSequentiallyConsistent, 0);
    LLVMValueRef has_awaiter = zan_icmp(g->builder, LLVMIntUGT, old_aw,
        LLVMConstInt(ptr_int_ty, 1, 0), "has.awaiter");

    LLVMBasicBlockRef wake_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.wake");
    LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.ret");
    LLVMBuildCondBr(g->builder, has_awaiter, wake_bb, ret_bb);

    LLVMPositionBuilderAtEnd(g->builder, wake_bb);
    LLVMValueRef awaiter = LLVMBuildIntToPtr(g->builder, old_aw, i8ptr, "awaiter");
    LLVMValueRef aws_ptr = LLVMBuildStructGEP2(g->builder, ft, frame,
        ASYNC_FRAME_AWAITER_STEP, "fr.awaiter.step");
    LLVMValueRef aw_step = LLVMBuildLoad2(g->builder, g->co_step_ptr, aws_ptr, "awaiter.step");
    LLVMValueRef wake_args[] = { awaiter, aw_step };
    zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, wake_args, 2, "");
    LLVMBuildBr(g->builder, ret_bb);

    LLVMPositionBuilderAtEnd(g->builder, ret_bb);
    LLVMBuildRetVoid(g->builder);

    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

static LLVMValueRef emit_task_action_run(zan_irgen_t *g, LLVMValueRef lv,
                                         zan_ast_node_t *sub_arg,
                                         local_scope_t *locals) {
    LLVMTypeRef sp_i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef lv_i8 = LLVMBuildBitCast(g->builder, lv, i8ptr, "task.del");

    emit_closure_retain(g, lv_i8);

    LLVMTypeRef ft = get_task_action_frame_type(g);
    LLVMTypeRef malloc_ty = LLVMGlobalGetValueType(g->fn_malloc);
    LLVMValueRef fsize = LLVMSizeOf(ft);
    LLVMValueRef raw = zan_call2(g->builder, malloc_ty, g->fn_malloc, &fsize, 1, "task.raw");
    zan_irgen_emit_oom_check(g, g->current_fn, raw);
    emit_rt_alloc_add(g, fsize);

    {
        LLVMTypeRef memset_ty = LLVMFunctionType(i8ptr,
            (LLVMTypeRef[]){ i8ptr, i32, sp_i64 }, 3, 0);
        LLVMValueRef memset_fn = LLVMGetNamedFunction(g->mod, "memset");
        if (!memset_fn) memset_fn = LLVMAddFunction(g->mod, "memset", memset_ty);
        zan_call2(g->builder, memset_ty, memset_fn,
            (LLVMValueRef[]){ raw, LLVMConstInt(i32, 0, 0), fsize }, 3, "");
    }

    LLVMValueRef rframe = LLVMBuildBitCast(g->builder, raw, LLVMPointerType(ft, 0), "task.frame");

    LLVMValueRef step_fn = get_task_action_step_fn(g);
    LLVMValueRef cleanup_fn = get_task_action_cleanup_fn(g);
    LLVMValueRef reap_fn = get_co_reap_fn(g);

    LLVMBuildStore(g->builder, cleanup_fn,
        LLVMBuildStructGEP2(g->builder, ft, rframe, ASYNC_FRAME_CLEANUP, "task.cl"));
    LLVMBuildStore(g->builder, step_fn,
        LLVMBuildStructGEP2(g->builder, ft, rframe, ASYNC_FRAME_SELF_STEP, "task.selfstep"));
    LLVMBuildStore(g->builder, lv_i8,
        LLVMBuildStructGEP2(g->builder, ft, rframe, 16, "task.dlg"));

    LLVMBuildStore(g->builder, raw,
        LLVMBuildStructGEP2(g->builder, ft, rframe, ASYNC_FRAME_AWAITER, "task.aw"));
    LLVMBuildStore(g->builder, reap_fn,
        LLVMBuildStructGEP2(g->builder, ft, rframe, ASYNC_FRAME_AWAITER_STEP, "task.aws"));

    emit_release_owned_call_temp(g, sub_arg, lv, locals);

    LLVMValueRef sched_args[] = { raw, step_fn };
    zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");

    LLVMValueRef track = get_co_track_fn(g);
    zan_call2(g->builder, LLVMGlobalGetValueType(track), track, &raw, 1, "");

    return LLVMBuildPtrToInt(g->builder, raw, sp_i64, "task.h");
}

static bool ident_names_own_field(zan_irgen_t *g, zan_ast_node_t *e) {
    if (!e || e->kind != AST_IDENTIFIER) return false;
    if (!g->current_type_sym) return false;
    if (g->current_this &&
        get_field_index(g->current_type_sym, e->ident.name) >= 0)
        return true;
    zan_symbol_t *fs = get_field_sym(g->current_type_sym, e->ident.name);
    return fs && fs->kind == SYM_FIELD && (fs->modifiers & MOD_STATIC) != 0;
}

static bool type_mentions_type_param(zan_type_t *t, int depth) {
    if (!t || depth > 8) return false;
    if (t->kind == TYPE_TYPE_PARAM || t->kind == TYPE_ERROR) return true;
    if (t->element_type && type_mentions_type_param(t->element_type, depth + 1))
        return true;
    for (int i = 0; i < t->type_arg_count; i++)
        if (type_mentions_type_param(t->type_args[i], depth + 1)) return true;
    return false;
}

static bool call_receiver_is_open_generic(zan_irgen_t *g, zan_ast_node_t *call,
                                         local_scope_t *locals) {
    if (g->current_type_sym && g->current_type_sym->decl &&
        (g->current_type_sym->decl->kind == AST_CLASS_DECL ||
         g->current_type_sym->decl->kind == AST_STRUCT_DECL) &&
        g->current_type_sym->decl->type_decl.type_params.count > 0)
        return true;
    zan_ast_node_t *callee = call->call.callee;
    if (!callee) return false;
    if (callee->kind == AST_MEMBER_ACCESS)
        return type_mentions_type_param(
            infer_expr_type(g, callee->member.object, locals), 0);
    for (int i = 0; i < call->call.args.count; i++)
        if (type_mentions_type_param(
                infer_expr_type(g, call->call.args.items[i], locals), 0))
            return true;
    return false;
}

static void diagnose_unresolved_static_chain(zan_irgen_t *g,
                                             zan_ast_node_t *recv) {
    while (recv && recv->kind == AST_CALL) {
        zan_ast_node_t *callee = recv->call.callee;
        if (!callee || callee->kind != AST_MEMBER_ACCESS) return;
        zan_ast_node_t *obj = callee->member.object;
        if (obj->kind == AST_IDENTIFIER && !ident_names_own_field(g, obj)) {
            zan_symbol_t *ts = zan_binder_lookup(g->binder, obj->ident.name);
            if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT) &&
                !get_method_sym(ts, callee->member.name)) {
                zan_diag_emit_code(g->diag, DIAG_ERROR, recv->loc, 3050,
                    "unresolved call '%.*s.%.*s': type '%.*s' has no method "
                    "'%.*s'",
                    (int)obj->ident.name.len, obj->ident.name.str,
                    (int)callee->member.name.len, callee->member.name.str,
                    (int)obj->ident.name.len, obj->ident.name.str,
                    (int)callee->member.name.len, callee->member.name.str);
                return;
            }
        }
        recv = obj;
    }
}

static int format_name_path(zan_ast_node_t *n, char *buf, int cap) {
    if (!n || cap <= 0) return 0;
    if (n->kind == AST_IDENTIFIER) {
        int l = (int)n->ident.name.len;
        if (l > cap) l = cap;
        memcpy(buf, n->ident.name.str, (size_t)l);
        return l;
    }
    if (n->kind == AST_MEMBER_ACCESS) {
        int off = format_name_path(n->member.object, buf, cap);
        if (off > 0 && off < cap) {
            buf[off++] = '.';
            int l = (int)n->member.name.len;
            if (l > cap - off) l = cap - off;
            memcpy(buf + off, n->member.name.str, (size_t)l);
            off += l;
        }
        return off;
    }
    return 0;
}

static bool src_method_takes_over(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
    zan_ast_node_t *callee = expr->call.callee;
    if (callee->kind != AST_MEMBER_ACCESS ||
        callee->member.object->kind != AST_IDENTIFIER) return false;
    if (local_find(locals, callee->member.object->ident.name)) return false;
    if (ident_names_own_field(g, callee->member.object)) return false;
    zan_symbol_t *ts = zan_binder_lookup(g->binder, callee->member.object->ident.name);
    if (!ts || (ts->kind != SYM_CLASS && ts->kind != SYM_STRUCT)) return false;
    return get_method_sym(ts, callee->member.name) != NULL;
}

static int try_method_spec(zan_irgen_t *g, zan_symbol_t *msym,
                           zan_ast_node_t *call, zan_ast_node_t *recv_expr,
                           local_scope_t *locals) {
    if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return -1;
    zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
    if (tps->count == 0 || tps->count > 8) return -1;
    bool m_static = (msym->decl->method_decl.modifiers & MOD_STATIC) != 0;
    int recv_params = (recv_expr && m_static) ? 1 : 0;
    if (msym->decl->method_decl.params.count !=
        call->call.args.count + recv_params) return -1;
    zan_type_t *owner_inst = NULL;
    if (!m_static) {
        zan_type_t *rt = recv_expr ? infer_expr_type(g, recv_expr, locals)
                                   : (g->current_type_sym ? g->current_type_sym->type
                                                          : NULL);
        if (!rt || rt->kind != TYPE_CLASS) return -1;
        if (!recv_expr && !g->current_this) return -1;
        /* `Pool<string> */
        if (rt->type_arg_count > 0) owner_inst = rt;
        else if (g->cur_inst && (!rt->sym || rt->sym == g->cur_inst->sym))
            owner_inst = g->cur_inst;
    }
    for (int j = 0; j < msym->decl->method_decl.params.count; j++)
        if (msym->decl->method_decl.params.items[j]->param.by_ref) return -1;
    zan_type_t *bind[8] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
    infer_method_tp_bindings(g, msym, call, recv_expr, locals, bind);
    zan_compile_trace("method spec %.*s.%.*s bindc=%d bind0=%d bind0args=%d",
                      (int)(msym->parent ? msym->parent->name.len : 0),
                      msym->parent ? msym->parent->name.str : "",
                      (int)msym->name.len, msym->name.str, tps->count,
                      bind[0] ? (int)bind[0]->kind : -1,
                      bind[0] ? bind[0]->type_arg_count : -1);
    return get_or_create_method_spec(g, msym, bind, tps->count, owner_inst);
}

/* 实参入异常展开栈（临时量的所有权在调用方，调用抛异常时要释放） */
static int emit_call_arg_eh_push(zan_irgen_t *g, zan_ast_node_t *arg,
                                 LLVMValueRef val, local_scope_t *locals);

static LLVMValueRef emit_method_spec_call(zan_irgen_t *g, int idx,
                                          zan_ast_node_t *call,
                                          zan_ast_node_t *recv_expr,
                                          local_scope_t *locals) {
    struct zan_method_spec sp = g->method_specs[idx];
    zan_ast_list_t *tps = &sp.msym->decl->method_decl.type_params;
    int pcount = sp.msym->decl->method_decl.params.count;
    bool m_static = (sp.msym->decl->method_decl.modifiers & MOD_STATIC) != 0;
    int this_off = m_static ? 0 : 1;
    int arg_base = (recv_expr && m_static) ? 1 : 0;
    int total = pcount + this_off;
    LLVMValueRef *call_args = (LLVMValueRef *)calloc(
        (size_t)(total > 0 ? total : 1), sizeof(LLVMValueRef));
    zan_ast_node_t **aexprs = (zan_ast_node_t **)calloc(
        (size_t)(total > 0 ? total : 1), sizeof(zan_ast_node_t *));
    int *arg_eh_pushed = (int *)calloc(
        (size_t)(total > 0 ? total : 1), sizeof(int));
    if (this_off) {
        LLVMTypeRef this_ty = LLVMTypeOf(LLVMGetParam(sp.fn, 0));
        LLVMValueRef self;
        if (recv_expr) {
            bool is_async = (sp.msym && sp.msym->decl &&
                             sp.msym->decl->kind == AST_METHOD_DECL &&
                             (sp.msym->decl->method_decl.modifiers & MOD_ASYNC) != 0);
            if (call && call->kind == AST_CALL && call->call.callee &&
                call->call.callee->kind == AST_MEMBER_ACCESS) {
                self = emit_guarded_member_object_ex(g, call->call.callee, locals, is_async);
            } else {
                self = emit_expr(g, recv_expr, locals);
            }
        } else {
            self = LLVMBuildLoad2(g->builder,
                LLVMGetAllocatedType(g->current_this), g->current_this, "self");
        }
        if (LLVMTypeOf(self) != this_ty)
            self = LLVMBuildBitCast(g->builder, self, this_ty, "self.c");
        call_args[0] = self;
        aexprs[0] = recv_expr;
        zan_type_t *rt = recv_expr ? infer_expr_type(g, recv_expr, locals) : NULL;
        if (rt && is_rc_managed_type(rt) &&
            !expr_is_local_ident(recv_expr, locals) &&
            expr_yields_owned_rc_value(g, recv_expr, locals) &&
            LLVMGetTypeKind(LLVMTypeOf(self)) == LLVMPointerTypeKind) {
            emit_eh_tmp_push(g, self);
            arg_eh_pushed[0] = 1;
        }
    }
    for (int j = 0; j < pcount; j++) {
        aexprs[j + this_off] = (arg_base == 1 && j == 0)
            ? recv_expr : call->call.args.items[j - arg_base];
        zan_type_t *pt = subst_method_tp(g,
            method_param_type(g, sp.msym, j), tps, sp.bind);
        if (sp.owner_inst) pt = subst_type_param_deep(g, pt, sp.owner_inst);
        call_args[j + this_off] =
            emit_arg_typed(g, aexprs[j + this_off], pt, locals);
        zan_ast_node_t *arg = aexprs[j + this_off];
        arg_eh_pushed[j + this_off] = emit_call_arg_eh_push(g, arg,
            call_args[j + this_off], locals);
    }
    coerce_args_to_params(g, sp.fn_type, call_args, total);
    const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(sp.fn_type)) ==
                      LLVMVoidTypeKind) ? "" : "gspec";
    LLVMValueRef result = zan_call2(g->builder, sp.fn_type, sp.fn,
                                    call_args, (unsigned)total, cn);
    for (int j = total - 1; j >= 0; j--)
        if (arg_eh_pushed[j]) emit_eh_tmp_pop(g);
    for (int j = 0; j < total; j++)
        if (aexprs[j])
            emit_release_owned_call_temp(g, aexprs[j], call_args[j], locals);
    free(arg_eh_pushed);
    free(aexprs);
    free(call_args);
    return result;
}

static LLVMValueRef emit_string_copy_range(zan_irgen_t *g,
        LLVMValueRef start, LLVMValueRef end) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef len = zan_sub(g->builder,
        LLVMBuildPtrToInt(g->builder, end, i64, "e.i"),
        LLVMBuildPtrToInt(g->builder, start, i64, "s.i"), "slen");
    LLVMValueRef total = zan_add(g->builder, len,
        LLVMConstInt(i64, 1, 0), "cap");
    LLVMValueRef buf = emit_string_alloc_rc(g, total);
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef mc = get_libc_fn(g, "memcpy", memcpy_ty);
    zan_call2(g->builder, memcpy_ty, mc,
        (LLVMValueRef[]){ buf, start, len }, 3, "");
    LLVMValueRef endp = LLVMBuildGEP2(g->builder,
        LLVMInt8TypeInContext(g->ctx), buf, &len, 1, "ep");
    zan_store_fit(g, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0), endp);
    return buf;
}

/* 检查 Lambda 函数体内是否包含显式带值返回 (return <expr>;) */
static bool lambda_body_has_value_return(zan_ast_node_t *n) {
    if (!n) return false;
    switch (n->kind) {
    case AST_RETURN_STMT:
        return n->ret.value != NULL;
    case AST_BLOCK:
        for (int i = 0; i < n->block.stmts.count; i++)
            if (lambda_body_has_value_return(n->block.stmts.items[i])) return true;
        return false;
    case AST_IF_STMT:
        return lambda_body_has_value_return(n->if_stmt.then_body) ||
               lambda_body_has_value_return(n->if_stmt.else_body);
    case AST_WHILE_STMT:
        return lambda_body_has_value_return(n->while_stmt.body);
    case AST_DO_WHILE_STMT:
        return lambda_body_has_value_return(n->while_stmt.body);
    case AST_FOR_STMT:
        return lambda_body_has_value_return(n->for_stmt.body);
    case AST_FOREACH_STMT:
        return lambda_body_has_value_return(n->foreach_stmt.body);
    case AST_SWITCH_STMT:
        for (int i = 0; i < n->switch_stmt.cases.count; i++) {
            zan_ast_node_t *cs = n->switch_stmt.cases.items[i];
            if (!cs) continue;
            if (lambda_body_has_value_return(cs->switch_case.body)) return true;
        }
        return false;
    case AST_TRY_STMT:
        return lambda_body_has_value_return(n->try_stmt.try_body) ||
               (n->try_stmt.finally_body &&
                lambda_body_has_value_return(n->try_stmt.finally_body));
    default:
        return false;
    }
}

static int is_string_like_expr(zan_irgen_t *g, zan_ast_node_t *e,
                               local_scope_t *locals) {
    return is_string_expr(g, e, locals) || expr_is_char(g, e, locals);
}

/* 发射`e` as a C string */
static LLVMValueRef emit_string_like_arg(zan_irgen_t *g, zan_ast_node_t *e,
                                         local_scope_t *locals, int *owned) {
    LLVMValueRef v = emit_expr(g, e, locals);
    *owned = 0;
    if (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind &&
        expr_is_char(g, e, locals)) {
        *owned = 1;
        return emit_char_to_cstr(g, v);
    }
    return v;
}

static void release_string_like_arg(zan_irgen_t *g, zan_ast_node_t *e,
                                    LLVMValueRef v, int owned,
                                    local_scope_t *locals) {
    if (owned) emit_string_release(g, v);
    else emit_release_owned_call_temp(g, e, v, locals);
}

static int irgen_enum_members(zan_symbol_t *sym, zan_symbol_t **out_syms,
                              long long *out_vals, int cap) {
    if (!sym) { return 0; }
    long long val = 0;
    int n = 0;
    for (int i = 0; i < sym->member_count && n < cap; i++) {
        zan_symbol_t *m = sym->members[i];
        if (!m || m->kind != SYM_ENUM_MEMBER) { continue; }
        if (m->decl && m->decl->kind == AST_ENUM_MEMBER &&
            m->decl->enum_member.value &&
            m->decl->enum_member.value->kind == AST_INT_LITERAL) {
            val = (long long)m->decl->enum_member.value->int_val;
        }
        out_syms[n] = m;
        out_vals[n] = val;
        n++;
        val++;
    }
    return n;
}

static int emit_intrinsic_own_recv(zan_irgen_t *g, zan_ast_node_t *lobj,
                                   LLVMValueRef recv, local_scope_t *locals) {
    zan_type_t *lt = infer_expr_type(g, lobj, locals);
    if (!lt || !is_rc_managed_type(lt) ||
        expr_is_local_ident(lobj, locals) ||
        !expr_yields_owned_rc_value(g, lobj, locals)) return 0;
    emit_eh_tmp_push(g, recv);
    return 1;
}

static void emit_intrinsic_drop_recv(zan_irgen_t *g, zan_ast_node_t *lobj,
                                     LLVMValueRef recv, local_scope_t *locals,
                                     int pushed) {
    if (pushed) emit_eh_tmp_pop(g);
    emit_release_owned_call_temp(g, lobj, recv, locals);
}

static int emit_call_arg_eh_push(zan_irgen_t *g, zan_ast_node_t *arg,
                                 LLVMValueRef val, local_scope_t *locals) {
    zan_type_t *at = infer_expr_type(g, arg, locals);
    if (!call_arg_is_owned_temp(g, arg, at, locals) ||
        LLVMGetTypeKind(LLVMTypeOf(val)) != LLVMPointerTypeKind) return 0;
    if (expr_yields_delegate_value(g, arg, locals)) {
        /* 委托实参：闭包在 IR 里是函数指针，而展开器按 i8ptr 取回闭包
           指针来跑它的析构，所以槽里存的是位转换后的 i8ptr。 */
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMValueRef slot = emit_entry_alloca(g, i8ptr, "arg.eh");
        LLVMBuildStore(g->builder,
            LLVMBuildBitCast(g->builder, val, i8ptr, "arg.dlg"), slot);
        emit_eh_tmp_push_slot(g, slot, ZAN_EH_SLOT_DLG);
        return 1;
    }
    int ehk = eh_slot_kind_of(at);
    if (ehk != ZAN_EH_SLOT_OBJ) {
        LLVMValueRef slot = emit_entry_alloca(g,
            LLVMTypeOf(val), "arg.eh");
        LLVMBuildStore(g->builder, val, slot);
        emit_eh_tmp_push_slot(g, slot, ehk);
    } else {
        emit_eh_tmp_push(g, val);
    }
    return 1;
}

/* 调用无法解析时的兜底返回值。
 *
 * 开放泛型场景（泛型类的擦除变体里，类型实参还没定下来）会走到这里：
 * 诊断被有意压掉，但代码还得能编过。返回值必须与声明的返回类型一致——
 * 早先这里固定返回 i32 0，于是调用点拿 i32 去和字符串指针比较，
 * LLVM 校验直接报 "Both operands to ICmp instruction are not of the same
 * type"，整份程序编不出来。这里按返回类型给同类型的默认值。 */
static LLVMValueRef emit_unresolved_call_result(zan_irgen_t *g,
                                                 zan_ast_node_t *expr,
                                                 local_scope_t *locals) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    zan_type_t *rt = infer_expr_type(g, expr, locals);
    if (!rt || rt->kind == TYPE_ERROR || rt->kind == TYPE_VOID) {
        return LLVMConstInt(i32t, 0, 0);
    }
    LLVMTypeRef lt = map_type(g, rt);
    switch (LLVMGetTypeKind(lt)) {
    case LLVMPointerTypeKind:
    case LLVMStructTypeKind:
    case LLVMArrayTypeKind:
    case LLVMVectorTypeKind:
        return LLVMConstNull(lt);
    case LLVMFloatTypeKind:
        return LLVMConstReal(lt, 0.0f);
    case LLVMDoubleTypeKind:
        return LLVMConstReal(lt, 0.0);
    case LLVMIntegerTypeKind:
        return LLVMConstInt(lt, 0, 0);
    default:
        return LLVMConstInt(i32t, 0, 0);
    }
}

/* Convert.ToString 这条内建只接受"真正有字符串形态"的值。类实例 / 数组 /
   委托 / object / 结构体都不行：
     - 把指针当 C 字符串读是**越界读**——读的是对象头里的指针字节，一路扫到
       进程映像里的某个 0 字节为止（实测打印出过进程里的符号名，并把程序
       带崩，0x80000003）；
     - 结构体落进 itoa 分支则生成 %struct.T 传给 i64 形参的非法 IR，LLVM
       校验直接失败（"Call parameter type does not match function signature"）。
   白名单与检查器的 type_is_concatable 同源；这里多放行三项"推不出来"的
   情形：错误类型（不追加二次报错）、类型参数（泛型实例化后才确定具体
   类型，此时按旧的宽松路径走，不要误伤泛型代码）、object（带标签的引用，
   编译期看不出里面装的是字符串还是类实例——object 上连通用 ToString() 都
   调不通，见 TASKS.md A444）。
   放行 object 的代价是它仍按 C 字符串读：里面装的是字符串时结果正确，装
   的是类实例时仍会越界读；治本要等 object 的通用 ToString 或把
   Convert.ToString 声明进 stdlib（A443），两边一起做。 */
static bool type_is_textual_or_numeric(zan_type_t *t) {
    if (!t) return true;
    switch (t->kind) {
    case TYPE_STRING: case TYPE_CHAR: case TYPE_ENUM: case TYPE_BOOL:
    case TYPE_BYTE: case TYPE_SHORT: case TYPE_INT: case TYPE_LONG:
    case TYPE_SBYTE: case TYPE_USHORT: case TYPE_UINT: case TYPE_ULONG:
    case TYPE_FLOAT: case TYPE_DOUBLE: case TYPE_DECIMAL: case TYPE_NINT:
    case TYPE_ERROR: case TYPE_TYPE_PARAM: case TYPE_OBJECT:
        return true;
    case TYPE_NULLABLE:
        return t->element_type ? type_is_textual_or_numeric(t->element_type) : true;
    default:
        return false;
    }
}

/* 纯文本解析型内建（int.Parse / double.Parse / TryParse）的实参必须是字符串。
   指针一路走到 strtoll/strtod 就是把对象的字节当文本读；数值实参更糟——
   strtoll 收 i8*，传 i64 进去直接生成非法 IR（LLVM 校验失败）。 */
static bool type_is_text_arg(zan_type_t *t) {
    if (!t) return true;
    switch (t->kind) {
    case TYPE_STRING: case TYPE_OBJECT: case TYPE_ERROR: case TYPE_TYPE_PARAM:
        return true;
    case TYPE_NULLABLE:
        return t->element_type ? type_is_text_arg(t->element_type) : true;
    default:
        return false;
    }
}

/* Math.* 的实参必须是数值。指针/结构体落进 SIToFP 会生成非法 IR——LLVM 校验
   失败（"Call parameter type does not match function signature"），编译直接
   死在代码生成阶段，用户看到的是内部错误而不是人话诊断。 */
static bool math_arg_is_numeric(LLVMValueRef v) {
    LLVMTypeKind k = LLVMGetTypeKind(LLVMTypeOf(v));
    /* bool 降成 i1：C# 里 bool 没有到 double 的隐式转换，Math.Sqrt(true)
     * 应编译期拒绝（i1 的 SIToFP 是符号扩展，true 曾被当成 -1.0 →
     * Sqrt 得 NaN，TASKS.md A498）。 */
    if (k == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(LLVMTypeOf(v)) == 1)
        return false;
    return k == LLVMIntegerTypeKind || k == LLVMDoubleTypeKind ||
           k == LLVMFloatTypeKind;
}

/* Math.* 的实参不能是 bool。判据是**语义类型**不是 LLVM 宽度：一个从泛型
 * 方法回来的 bool 走的是 i64 槽位（extend_int_for_slot 的 zeroext），
 * 按宽度判会漏过它 —— 同一个 bool 在两条路径上得到不同的判决
 * （TASKS.md A498；与 A492 的「宽度不是类型」同源）。C# 里 bool 没有到
 * double 的隐式转换，Math.Sqrt(true) 是编译错误。 */
static bool math_arg_is_bool(zan_irgen_t *g, zan_ast_node_t *arg_ast,
                              local_scope_t *locals) {
    /* infer_expr_type does not take a null node */
    if (!arg_ast) return false;
    return expr_is_bool(g, arg_ast, locals);
}

/* 数值实参统一成 double（int 走 sitofp，float 走 fpext）。 */
static LLVMValueRef math_arg_to_double(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    LLVMTypeKind k = LLVMGetTypeKind(LLVMTypeOf(v));
    if (k == LLVMFloatTypeKind) return LLVMBuildFPExt(g->builder, v, dbl, "ext");
    if (k == LLVMIntegerTypeKind) return LLVMBuildSIToFP(g->builder, v, dbl, "tofp");
    return v;
}

/* 不是数值就报人话诊断（fail-closed），返回 false 让调用方回一个 0 值。
 * arg_ast 是实参的 AST 节点：bool 的判决按语义类型做（见 math_arg_is_bool），
 * 宽度判 i1 只是兜底（expr_is_bool 判不出的类型仍按降级后的宽度拒）。 */
static bool math_require_numeric(zan_irgen_t *g, zan_ast_node_t *expr,
                                  zan_ast_node_t *arg_ast, LLVMValueRef v,
                                  local_scope_t *locals, const char *name) {
    if (math_arg_is_numeric(v) && !math_arg_is_bool(g, arg_ast, locals))
        return true;
    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3051,
                  "Math.%s requires an int or floating-point argument", name);
    return false;
}

/* A512：解析型转换的失败检查帮手（TASKS.md）。X.Parse 曾用 NULL endptr
 * 直落 strtoll/strtod——"abc" 返 0 静默继续，"2147483648" 溢出静默接受。
 * 失败语义与 TryParse 对齐：软诊断（stderr 一次 + file:line）+ 安全值 0，
 * --strict-runtime 下 exit 70。 */
static LLVMValueRef emit_parse_bad_pred(zan_irgen_t *g, LLVMValueRef s,
                                        LLVMValueRef ep) {
    if (!g->runtime_checks || !g->current_fn)
        return LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMValueRef ep_null = zan_icmp(g->builder, LLVMIntEQ, ep,
        LLVMConstNull(LLVMTypeOf(ep)), "parse.epnull");
    LLVMValueRef empty = zan_irgen_intern_string(g, "");
    LLVMValueRef ep_safe = LLVMBuildSelect(g->builder, ep_null,
        empty, ep, "parse.epsafe");
    LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8, ep_safe, "parse.c");
    LLVMValueRef trailing = zan_icmp(g->builder, LLVMIntNE, ch,
        LLVMConstInt(i8, 0, 0), "parse.trail");
    LLVMValueRef none = zan_icmp(g->builder, LLVMIntEQ, ep, s,
        "parse.none");
    LLVMValueRef b1 = LLVMBuildOr(g->builder, ep_null, trailing, "parse.b1");
    return LLVMBuildOr(g->builder, b1, none, "parse.bad");
}

/* null 串进 strtoll 是 UB：先 guard（软诊断 + 空串替身） */
static LLVMValueRef emit_parse_str_guard(zan_irgen_t *g, LLVMValueRef s,
                                         zan_loc_t loc) {
    if (!g->runtime_checks || !g->current_fn) return s;
    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, s,
        LLVMConstNull(LLVMTypeOf(s)), "parse.snull");
    emit_runtime_check(g, isnull, loc,
        "null string passed to a parse function");
    return LLVMBuildSelect(g->builder, isnull,
        zan_irgen_intern_string(g, ""), s, "parse.ssafe");
}
/* A555：检查类边是否与泛型实参匹配以实现目标接口 */
static bool ifc_edge_matches_inst(zan_ast_node_t *edge_ref, zan_ast_list_t *tps,
                                   zan_type_t **args, zan_type_t *sup) {
    if (!edge_ref || edge_ref->kind != AST_TYPE_REF || !sup || !sup->sym)
        return false;
    zan_istr_t en = edge_ref->type_ref.name;
    if (en.len != sup->sym->name.len ||
        memcmp(en.str, sup->sym->name.str, (size_t)en.len) != 0)
        return false;
    zan_ast_list_t *eargs = &edge_ref->type_ref.type_args;
    int want = sup->type_arg_count;
    if (eargs->count != want) return false;
    for (int j = 0; j < want; j++) {
        zan_ast_node_t *ea = eargs->items[j];
        if (!ea) return false;
        if (ea->kind == AST_TYPE_REF && ea->type_ref.type_args.count > 0)
            return false;
        zan_istr_t ename;
        if (ea->kind == AST_IDENTIFIER) ename = ea->ident.name;
        else if (ea->kind == AST_TYPE_REF) ename = ea->type_ref.name;
        else return false;
        int k = 0;
        int tps_count = tps ? tps->count : 0;
        for (; k < tps_count; k++) {
            zan_istr_t tn = tps->items[k]->ident.name;
            if (tn.len == ename.len &&
                memcmp(tn.str, ename.str, (size_t)tn.len) == 0)
                break;
        }
        if (k < tps_count) {
            if (!args || !args[k] || !types_equal(args[k], sup->type_args[j]))
                return false;
        } else {
            zan_type_t *sa = sup->type_args[j];
            if (!sa || ename.len != sa->name.len ||
                memcmp(ename.str, sa->name.str, (size_t)sa->name.len) != 0)
                return false;
        }
    }
    return true;
}

static bool class_inst_implements_iface(zan_irgen_t *g, zan_symbol_t *cls,
                                        zan_type_t *inst, zan_type_t *obj_ty) {
    if (!cls || !obj_ty || !obj_ty->sym) return false;
    if (!class_implements_iface(cls, obj_ty->sym)) return false;
    if (obj_ty->type_arg_count == 0) return true;
    if (cls->decl && (cls->decl->kind == AST_CLASS_DECL ||
                      cls->decl->kind == AST_STRUCT_DECL ||
                      cls->decl->kind == AST_INTERFACE_DECL)) {
        zan_ast_list_t *tps = &cls->decl->type_decl.type_params;
        zan_ast_list_t *bases = &cls->decl->type_decl.bases;
        zan_type_t **args = inst ? inst->type_args : NULL;
        for (int i = 0; i < bases->count; i++) {
            if (ifc_edge_matches_inst(bases->items[i], tps, args, obj_ty))
                return true;
            if (bases->items[i] && bases->items[i]->kind == AST_TYPE_REF && g && g->binder) {
                zan_symbol_t *isym = zan_binder_lookup(g->binder,
                                                       bases->items[i]->type_ref.name);
                if (isym && isym->kind == SYM_INTERFACE &&
                    class_inst_implements_iface(g, isym, inst, obj_ty))
                    return true;
            }
        }
    }
    if (cls->type && cls->type->base_type && cls->type->base_type->sym &&
        cls->type->base_type->sym != cls) {
        zan_type_t *binst = inst;
        if (!binst && cls->type->base_type->type_arg_count > 0)
            binst = cls->type->base_type;
        return class_inst_implements_iface(g, cls->type->base_type->sym, binst, obj_ty);
    }
    return false;
}


/* ---- A558：System.Object 方法族 ---------------------------------------
 * object/接口/类接收者上的 ToString()、Equals(o)、GetHashCode()。object
 * 没有成员表，这三者此前连纯类接收者都绑不上（E3068）。
 * 分派沿用接口分派的"身份 tag 候选链"：堆对象首字 = 类型 vtable 全局
 * （类 vptr / A556 结构体箱 tag），逐候选比对命中后直呼该类**最派生**
 * 实现（resolve_overload 沿基类链解析，覆写生效；泛型类走实例 vtable，
 * 同 ifc；结构体候选 this = 载荷 +8，同 A556-2）。
 * 字符串接收者首字不是 vtable：ToString 返还自身，Equals 按内容 strcmp
 * （String.CompareOrdinal 同口径，Zan 字符串保证 NUL 结尾），
 * GetHashCode djb2。
 * 未命中候选 = 没写覆写 → object 缺省：ToString 返回运行期类型名（分配
 * 点 tynames 首项，与 `is` 同一描述符机制；无描述符/数组回落 "object"），
 * Equals 按引用相等，GetHashCode 指针混排。
 * 静态类型（含基类链）声明了同名成员时由普通成员调用路径处理，不进这里；
 * Equals 候选只收引用形参的 1 参覆写（值形参重载经 object 接收者不可达，
 * C# 同款是装箱走 Equals(object)）。 */
static int objfam_ref_kind(zan_type_t *t) {
    return t && (t->kind == TYPE_OBJECT || t->kind == TYPE_CLASS ||
                 t->kind == TYPE_INTERFACE || t->kind == TYPE_STRING ||
                 t->kind == TYPE_ARRAY || t->kind == TYPE_DELEGATE ||
                 t->kind == TYPE_TASK);
}

static LLVMValueRef emit_object_family_call(zan_irgen_t *g,
                                            zan_ast_node_t *expr, int fam,
                                            local_scope_t *locals) {
    LLVMContextRef c = g->ctx;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(c);
    /* size_t 随目标位宽（wasm32/arm32 上是 i32），strlen/memcpy 按它发 */
    unsigned psz = LLVMPointerSize(LLVMGetModuleDataLayout(g->mod));
    LLVMTypeRef szt = (psz == 8) ? i64 : i32;
    zan_ast_node_t *callee = expr->call.callee;
    zan_istr_t fmn = callee->member.name;

    zan_type_t *res_zt = (fam == 0) ? g->binder->type_string
                       : (fam == 1) ? g->binder->type_bool
                       : g->binder->type_int;
    LLVMTypeRef res_ty = map_type(g, res_zt);

    LLVMValueRef recv = emit_guarded_member_object_ex(g, callee, locals, false);
    LLVMValueRef aval = NULL;
    int n_eh_args = 0;
    if (fam == 1) {
        aval = emit_arg_typed(g, expr->call.args.items[0],
                              g->binder->type_object, locals);
        n_eh_args += emit_call_arg_eh_push(g, expr->call.args.items[0],
                                           aval, locals);
    }

    typedef struct {
        zan_symbol_t *cls;
        zan_symbol_t *impl_m;
        LLVMValueRef  fn;
        LLVMTypeRef   fnty;
        LLVMValueRef  tagc;
    } objfam_candidate_t;

    int cand_cap = g->struct_type_count + g->generic_inst_count + 4;
    objfam_candidate_t *cands = (objfam_candidate_t *)calloc(
        (size_t)cand_cap, sizeof(objfam_candidate_t));
    int cand_count = 0;
    int want_argc = (fam == 1) ? 1 : 0;

    /* 非泛型类/结构体候选：0 参（ToString/GetHashCode 返回 string/int）
     * 或 1 参引用形参（Equals 返回 bool），非静态。resolve_overload 沿
     * 基类链解析，同一覆写在基类、派生类各产一个候选，tag 比对天然按
     * 运行期类型命中其一。 */
    for (int si = 0; si < g->struct_type_count; si++) {
        zan_symbol_t *cls = g->struct_types[si].sym;
        if (!cls || is_user_generic_sym(cls)) continue;
        zan_symbol_t *m = resolve_overload(cls, fmn, want_argc, 0);
        if (!m || (m->modifiers & MOD_STATIC)) continue;
        if (!m->decl || m->decl->kind != AST_METHOD_DECL ||
            !m->decl->method_decl.return_type)
            continue;
        zan_type_t *mret = zan_binder_resolve_type(g->binder,
            m->decl->method_decl.return_type);
        zan_type_t *want_ret = (fam == 0) ? g->binder->type_string
                             : (fam == 1) ? g->binder->type_bool
                             : g->binder->type_int;
        if (!mret || mret->kind != want_ret->kind) continue;
        if (fam == 1) {
            zan_type_t *mp = method_param_type_at(g, m, 0, expr,
                                                  callee->member.object,
                                                  locals);
            if (mp && !objfam_ref_kind(mp)) continue;
        }
        LLVMValueRef ifn = NULL; LLVMTypeRef ifnty = NULL;
        for (int fi = irgen_find_function(g, m); fi >= 0; fi = -1)
            if (g->functions[fi].sym == m) {
                ifn = g->functions[fi].fn;
                ifnty = g->functions[fi].fn_type;
                break;
            }
        if (!ifn || !ifnty || cand_count >= cand_cap) continue;
        cands[cand_count].cls = cls;
        cands[cand_count].impl_m = m;
        cands[cand_count].fn = ifn;
        cands[cand_count].fnty = ifnty;
        cands[cand_count].tagc =
            LLVMConstBitCast(get_vtable_global(g, cls), i8ptr);
        cand_count++;
    }
    /* 泛型实例化类候选（实例 vtable 作 tag，特化方法体作实现，同 ifc） */
    for (int gi = 0; gi < g->generic_inst_count; gi++) {
        zan_symbol_t *cls = g->generic_insts[gi].type_sym;
        zan_type_t *inst = g->generic_insts[gi].inst;
        if (!cls || !inst || !is_user_generic_sym(cls)) continue;
        zan_symbol_t *impl_m = resolve_overload(cls, fmn, want_argc, 0);
        if (!impl_m || (impl_m->modifiers & MOD_STATIC)) continue;
        zan_symbol_t *msym_owner = impl_m->parent ? impl_m->parent : cls;
        zan_type_t *owner = is_user_generic_sym(msym_owner)
            ? vtable_owner_inst(g, inst, msym_owner) : NULL;
        LLVMValueRef ifn = NULL; LLVMTypeRef ifnty = NULL;
        if (owner && owner->type_arg_count > 0) {
            ifn = find_generic_fn(g, impl_m, owner->type_args,
                                  owner->type_arg_count, &ifnty);
        } else if (!is_user_generic_sym(msym_owner)) {
            for (int fi = irgen_find_function(g, impl_m); fi >= 0; fi = -1)
                if (g->functions[fi].sym == impl_m) {
                    ifn = g->functions[fi].fn;
                    ifnty = g->functions[fi].fn_type;
                    break;
                }
        }
        if (!ifn || !ifnty || cand_count >= cand_cap) continue;
        cands[cand_count].cls = cls;
        cands[cand_count].impl_m = impl_m;
        cands[cand_count].fn = ifn;
        cands[cand_count].fnty = ifnty;
        cands[cand_count].tagc = LLVMConstBitCast(
            get_vtable_global_inst(g, cls, inst), i8ptr);
        cand_count++;
    }

    LLVMValueRef result = LLVMConstNull(res_ty);
    if (LLVMGetTypeKind(LLVMTypeOf(recv)) == LLVMPointerTypeKind) {
        LLVMBasicBlockRef merge = LLVMAppendBasicBlockInContext(c,
            g->current_fn, "objf.m");
        LLVMValueRef *phi_vals = (LLVMValueRef *)calloc(
            (size_t)(cand_count + 4), sizeof(LLVMValueRef));
        LLVMBasicBlockRef *phi_bbs = (LLVMBasicBlockRef *)calloc(
            (size_t)(cand_count + 4), sizeof(LLVMBasicBlockRef));
        int np = 0;
        LLVMBasicBlockRef str_bb = LLVMAppendBasicBlockInContext(c,
            g->current_fn, "objf.s");
        LLVMBasicBlockRef tag_bb = LLVMAppendBasicBlockInContext(c,
            g->current_fn, "objf.t");
        LLVMBasicBlockRef def_bb = LLVMAppendBasicBlockInContext(c,
            g->current_fn, "objf.d");

        /* 接收者 site 字：高 32 位 = ZAN 字符串 tag 即字符串对象 */
        LLVMValueRef neg8 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
        LLVMValueRef sptr = LLVMBuildGEP2(g->builder, i8, recv, &neg8, 1,
                                          "objf.sptr");
        LLVMValueRef site = LLVMBuildLoad2(g->builder, i64,
            LLVMBuildBitCast(g->builder, sptr, LLVMPointerType(i64, 0),
                             "objf.sp"), "objf.site");
        LLVMValueRef isstr = zan_hdr_is_string(g, site, "objf.str");
        LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->builder);
        LLVMBuildCondBr(g->builder, isstr, str_bb, tag_bb);

        /* -- 字符串接收者 ------------------------------------------------ */
        LLVMPositionBuilderAtEnd(g->builder, str_bb);
        if (fam == 0) {
            emit_string_retain(g, recv);
            phi_vals[np] = recv;
            phi_bbs[np] = str_bb;
            np++;
            LLVMBuildBr(g->builder, merge);
        } else if (fam == 1) {
            LLVMBasicBlockRef arg_str_bb = LLVMAppendBasicBlockInContext(c,
                g->current_fn, "objf.as");
            LLVMBasicBlockRef eq_bb = LLVMAppendBasicBlockInContext(c,
                g->current_fn, "objf.eq");
            LLVMBasicBlockRef false_bb = LLVMAppendBasicBlockInContext(c,
                g->current_fn, "objf.f");
            LLVMValueRef argnn = zan_icmp(g->builder, LLVMIntNE, aval,
                LLVMConstNull(LLVMTypeOf(aval)), "objf.ann");
            LLVMBuildCondBr(g->builder, argnn, arg_str_bb, false_bb);
            LLVMPositionBuilderAtEnd(g->builder, arg_str_bb);
            LLVMValueRef site2 = LLVMBuildLoad2(g->builder, i64,
                LLVMBuildBitCast(g->builder,
                    LLVMBuildGEP2(g->builder, i8, aval, &neg8, 1, "objf.a2"),
                    LLVMPointerType(i64, 0), "objf.a2p"), "objf.site2");
            LLVMValueRef isstr2 = zan_hdr_is_string(g, site2, "objf.str2");
            LLVMBuildCondBr(g->builder, isstr2, eq_bb, false_bb);
            LLVMPositionBuilderAtEnd(g->builder, eq_bb);
            LLVMTypeRef strcmp_ty = LLVMFunctionType(i32,
                (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
            LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty,
                get_libc_fn(g, "strcmp", strcmp_ty),
                (LLVMValueRef[]){ recv, aval }, 2, "objf.scmp");
            phi_vals[np] = zan_icmp(g->builder, LLVMIntEQ, cmp,
                LLVMConstInt(i32, 0, 0), "objf.seq");
            phi_bbs[np] = eq_bb;
            np++;
            LLVMBuildBr(g->builder, merge);
            LLVMPositionBuilderAtEnd(g->builder, false_bb);
            phi_vals[np] = LLVMConstInt(i1, 0, 0);
            phi_bbs[np] = false_bb;
            np++;
            LLVMBuildBr(g->builder, merge);
        } else {
            /* djb2：hash = hash*33 + byte（i64 环绕），NUL 截断 */
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(c,
                g->current_fn, "objf.hb");
            LLVMValueRef hash = LLVMBuildPhi(g->builder, i64, "objf.h");
            LLVMValueRef idx = LLVMBuildPhi(g->builder, i64, "objf.i");
            LLVMValueRef chp = LLVMBuildGEP2(g->builder, i8, recv, &idx, 1,
                                             "objf.chp");
            LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8, chp, "objf.ch");
            LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(c,
                g->current_fn, "objf.hd");
            LLVMBuildCondBr(g->builder,
                zan_icmp(g->builder, LLVMIntEQ, ch, LLVMConstInt(i8, 0, 0),
                         "objf.hz"),
                done_bb, body_bb);
            LLVMPositionBuilderAtEnd(g->builder, body_bb);
            LLVMValueRef hash2 = zan_add(g->builder,
                LLVMBuildMul(g->builder, hash, LLVMConstInt(i64, 33, 0),
                             "objf.h33"),
                LLVMBuildZExt(g->builder, ch, i64, "objf.ch6"), "objf.h2");
            LLVMValueRef idx2 = zan_add(g->builder, idx,
                LLVMConstInt(i64, 1, 0), "objf.i2");
            LLVMBuildBr(g->builder, str_bb);
            LLVMPositionBuilderAtEnd(g->builder, done_bb);
            phi_vals[np] = LLVMBuildTrunc(g->builder, hash, i32, "objf.h32");
            phi_bbs[np] = done_bb;
            np++;
            LLVMBuildBr(g->builder, merge);
            LLVMValueRef h_in[2] = { LLVMConstInt(i64, 5381, 0), hash2 };
            LLVMBasicBlockRef h_bb[2] = { entry_bb, body_bb };
            LLVMAddIncoming(hash, h_in, h_bb, 2);
            LLVMValueRef i_in[2] = { LLVMConstInt(i64, 0, 0), idx2 };
            LLVMAddIncoming(idx, i_in, h_bb, 2);
        }

        /* -- 堆对象：tag 候选链 ------------------------------------------ */
        LLVMPositionBuilderAtEnd(g->builder, tag_bb);
        LLVMValueRef tag = LLVMBuildLoad2(g->builder, i8ptr,
            LLVMBuildBitCast(g->builder, recv, LLVMPointerType(i8ptr, 0),
                             "objf.vpp"), "objf.tag");
        for (int ci = 0; ci < cand_count; ci++) {
            LLVMBasicBlockRef callbb = LLVMAppendBasicBlockInContext(c,
                g->current_fn, "objf.call");
            LLVMBasicBlockRef nextbb = LLVMAppendBasicBlockInContext(c,
                g->current_fn, "objf.n");
            LLVMValueRef cmp = zan_icmp(g->builder, LLVMIntEQ, tag,
                cands[ci].tagc, "objf.eq");
            LLVMBuildCondBr(g->builder, cmp, callbb, nextbb);
            LLVMPositionBuilderAtEnd(g->builder, callbb);
            unsigned npar = LLVMCountParamTypes(cands[ci].fnty);
            LLVMTypeRef *pts = (LLVMTypeRef *)calloc(
                (size_t)(npar > 0 ? npar : 1), sizeof(LLVMTypeRef));
            LLVMGetParamTypes(cands[ci].fnty, pts);
            int cargc = want_argc + 1;
            LLVMValueRef *ca = (LLVMValueRef *)calloc((size_t)cargc,
                                                      sizeof(LLVMValueRef));
            /* A556-2：结构体箱载荷在首字 tag 之后（+8） */
            LLVMValueRef thisv = recv;
            if (cands[ci].cls->kind == SYM_STRUCT)
                thisv = LLVMBuildGEP2(g->builder, i8, recv,
                    (LLVMValueRef[]){ LLVMConstInt(i64, 8, 0) }, 1,
                    "objf.p8");
            ca[0] = (npar > 0)
                ? LLVMBuildBitCast(g->builder, thisv, pts[0], "objf.this")
                : thisv;
            if (fam == 1 && npar > 1)
                ca[1] = emit_boundary_coerce(g, aval, pts[1]);
            LLVMValueRef r = zan_call2(g->builder, cands[ci].fnty,
                cands[ci].fn, ca, (unsigned)cargc, "objf.call");
            r = emit_boundary_coerce(g, r, res_ty);
            phi_vals[np] = r;
            phi_bbs[np] = LLVMGetInsertBlock(g->builder);
            np++;
            LLVMBuildBr(g->builder, merge);
            free(ca);
            free(pts);
            LLVMPositionBuilderAtEnd(g->builder, nextbb);
        }
        LLVMBuildBr(g->builder, def_bb);

        /* -- object 缺省 ------------------------------------------------- */
        LLVMPositionBuilderAtEnd(g->builder, def_bb);
        if (fam == 0) {
            LLVMBasicBlockRef def_name_bb = LLVMAppendBasicBlockInContext(c,
                g->current_fn, "objf.dn");
            /* 运行期类型名：分配点 tynames 首项（与 `is` 同一描述符机制），
             * 越界/无描述符/数组 → "object"。 */
            LLVMValueRef nm_vals[4];
            LLVMBasicBlockRef nm_bbs[4];
            int nmi = 0;
            if (g->desc_hdr) {
                LLVMValueRef ok = zan_and(g->builder,
                    zan_and(g->builder,
                        zan_icmp(g->builder, LLVMIntNE, site,
                                 LLVMConstInt(i64, 0, 0), "objf.nz"),
                        zan_icmp(g->builder, LLVMIntNE, site,
                                 LLVMConstInt(i64, ZAN_ARRAY_MAGIC, 0),
                                 "objf.narr"), "objf.ok2"),
                    zan_icmp(g->builder, LLVMIntUGE, site,
                             LLVMConstInt(i64, 4096, 0), "objf.big"),
                    "objf.ok");
                LLVMBasicBlockRef dsc_bb = LLVMAppendBasicBlockInContext(c,
                    g->current_fn, "objf.dsc");
                LLVMBuildCondBr(g->builder, ok, dsc_bb, def_name_bb);
                nm_vals[nmi] = LLVMConstNull(i8ptr);
                nm_bbs[nmi] = def_bb;
                nmi++;
                LLVMPositionBuilderAtEnd(g->builder, dsc_bb);
                LLVMValueRef dsc = LLVMBuildIntToPtr(g->builder, site, i8ptr,
                                                     "objf.dsc.p");
                LLVMValueRef list = LLVMBuildLoad2(g->builder, i8ptr,
                    LLVMBuildBitCast(g->builder,
                        LLVMBuildGEP2(g->builder, i8, dsc,
                            (LLVMValueRef[]){ LLVMConstInt(i64, 8, 0) }, 1,
                            "objf.tnp"),
                        LLVMPointerType(i8ptr, 0), "objf.tnpp"), "objf.tn");
                LLVMValueRef lnn = zan_icmp(g->builder, LLVMIntNE, list,
                    LLVMConstNull(i8ptr), "objf.lnn");
                LLVMBasicBlockRef nm_bb = LLVMAppendBasicBlockInContext(c,
                    g->current_fn, "objf.nm");
                LLVMBuildCondBr(g->builder, lnn, nm_bb, def_name_bb);
                nm_vals[nmi] = LLVMConstNull(i8ptr);
                nm_bbs[nmi] = dsc_bb;
                nmi++;
                LLVMPositionBuilderAtEnd(g->builder, nm_bb);
                LLVMValueRef nm0 = LLVMBuildLoad2(g->builder, i8ptr, list,
                                                  "objf.nm0");
                LLVMBuildBr(g->builder, def_name_bb);
                nm_vals[nmi] = nm0;
                nm_bbs[nmi] = nm_bb;
                nmi++;
            } else if (g->g_site_tynames && g->g_site_count) {
                LLVMValueRef bound = LLVMBuildLoad2(g->builder, i64,
                    g->g_site_count, "objf.bound");
                LLVMValueRef ok = zan_and(g->builder,
                    zan_icmp(g->builder, LLVMIntSGE, site,
                             LLVMConstInt(i64, 0, 0), "objf.neg"),
                    zan_icmp(g->builder, LLVMIntSLT, site, bound,
                             "objf.oob"), "objf.ok");
                LLVMBasicBlockRef tbl_bb = LLVMAppendBasicBlockInContext(c,
                    g->current_fn, "objf.tbl");
                LLVMBuildCondBr(g->builder, ok, tbl_bb, def_name_bb);
                nm_vals[nmi] = LLVMConstNull(i8ptr);
                nm_bbs[nmi] = def_bb;
                nmi++;
                LLVMPositionBuilderAtEnd(g->builder, tbl_bb);
                LLVMValueRef tbl = LLVMBuildLoad2(g->builder,
                    LLVMPointerType(i8ptr, 0), g->g_site_tynames,
                    "objf.tbl.p");
                LLVMValueRef list = LLVMBuildLoad2(g->builder, i8ptr,
                    LLVMBuildGEP2(g->builder, i8ptr, tbl, &site, 1,
                                  "objf.lp"), "objf.list");
                LLVMValueRef lnn = zan_icmp(g->builder, LLVMIntNE, list,
                    LLVMConstNull(i8ptr), "objf.lnn");
                LLVMBasicBlockRef nm_bb = LLVMAppendBasicBlockInContext(c,
                    g->current_fn, "objf.nm");
                LLVMBuildCondBr(g->builder, lnn, nm_bb, def_name_bb);
                nm_vals[nmi] = LLVMConstNull(i8ptr);
                nm_bbs[nmi] = tbl_bb;
                nmi++;
                LLVMPositionBuilderAtEnd(g->builder, nm_bb);
                LLVMValueRef nm0 = LLVMBuildLoad2(g->builder, i8ptr, list,
                                                  "objf.nm0");
                LLVMBuildBr(g->builder, def_name_bb);
                nm_vals[nmi] = nm0;
                nm_bbs[nmi] = nm_bb;
                nmi++;
            } else {
                LLVMBuildBr(g->builder, def_name_bb);
                nm_vals[nmi] = LLVMConstNull(i8ptr);
                nm_bbs[nmi] = def_bb;
                nmi++;
            }
            LLVMPositionBuilderAtEnd(g->builder, def_name_bb);
            LLVMValueRef name0 = LLVMBuildPhi(g->builder, i8ptr, "objf.nmphi");
            LLVMAddIncoming(name0, nm_vals, nm_bbs, (unsigned)nmi);
            LLVMValueRef nm_null = zan_icmp(g->builder, LLVMIntEQ, name0,
                LLVMConstNull(i8ptr), "objf.nmn");
            LLVMValueRef lit_object = zan_irgen_intern_string(g, "object");
            LLVMValueRef nm = LLVMBuildSelect(g->builder, nm_null,
                lit_object, name0, "objf.nm.sel");
            LLVMTypeRef strlen_ty = LLVMFunctionType(szt,
                (LLVMTypeRef[]){ i8ptr }, 1, 0);
            LLVMValueRef len = zan_call2(g->builder, strlen_ty,
                get_libc_fn(g, "strlen", strlen_ty),
                (LLVMValueRef[]){ nm }, 1, "objf.len");
            LLVMValueRef len64 = (psz == 8)
                ? len : LLVMBuildZExt(g->builder, len, i64, "objf.len64");
            LLVMValueRef payload = zan_add(g->builder, len64,
                LLVMConstInt(i64, 1, 0), "objf.pay");
            LLVMValueRef s = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0),
                g->rt_str_alloc, &payload, 1, "objf.s");
            LLVMBuildMemCpy(g->builder, s, 1, nm, 1, len);
            LLVMValueRef nul_p = LLVMBuildGEP2(g->builder, i8, s, &len64, 1,
                                               "objf.nulp");
            LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), nul_p);
            /* 长度字回填：ZANS<<32 | len（rt_str_alloc 只写了 LEN_UNKNOWN） */
            LLVMValueRef hdrw = zan_or(g->builder,
                LLVMBuildShl(g->builder,
                    LLVMConstInt(i64, ZAN_STRING_TAG, 0),
                    LLVMConstInt(i64, 32, 0), "objf.tag32"),
                LLVMBuildAnd(g->builder, len64,
                    LLVMConstInt(i64, ZAN_STR_LEN_MASK, 0), "objf.len32"),
                "objf.hdrw");
            LLVMValueRef sm8 = LLVMBuildGEP2(g->builder, i8, s, &neg8, 1,
                                             "objf.sm8");
            LLVMBuildStore(g->builder, hdrw,
                LLVMBuildBitCast(g->builder, sm8, LLVMPointerType(i64, 0),
                                 "objf.smp"));
            phi_vals[np] = s;
            phi_bbs[np] = def_name_bb;
            np++;
            LLVMBuildBr(g->builder, merge);
        } else if (fam == 1) {
            LLVMValueRef a8 = aval;
            LLVMValueRef r8 = recv;
            if (LLVMTypeOf(a8) != i8ptr)
                a8 = LLVMBuildBitCast(g->builder, a8, i8ptr, "objf.a8");
            if (LLVMTypeOf(r8) != i8ptr)
                r8 = LLVMBuildBitCast(g->builder, r8, i8ptr, "objf.r8");
            phi_vals[np] = zan_icmp(g->builder, LLVMIntEQ, r8, a8,
                                    "objf.req");
            phi_bbs[np] = def_bb;
            np++;
            LLVMBuildBr(g->builder, merge);
        } else {
            LLVMValueRef pi = LLVMBuildPtrToInt(g->builder, recv, i64,
                                                "objf.pi");
            LLVMValueRef sh = LLVMBuildLShr(g->builder, pi,
                LLVMConstInt(i64, 4, 0), "objf.sh");
            LLVMValueRef mix = LLVMBuildMul(g->builder, sh,
                LLVMConstInt(i64, UINT64_C(0x9E3779B9), 0), "objf.mix");
            phi_vals[np] = LLVMBuildTrunc(g->builder, mix, i32, "objf.mix32");
            phi_bbs[np] = def_bb;
            np++;
            LLVMBuildBr(g->builder, merge);
        }

        LLVMPositionBuilderAtEnd(g->builder, merge);
        LLVMValueRef phi = LLVMBuildPhi(g->builder, res_ty, "objf.res");
        LLVMAddIncoming(phi, phi_vals, phi_bbs, (unsigned)np);
        result = phi;
        free(phi_vals);
        free(phi_bbs);
    }
    free(cands);

    for (int k = 0; k < n_eh_args; k++)
        emit_eh_tmp_pop(g);
    if (fam == 1)
        emit_release_owned_call_temp(g, expr->call.args.items[0], aval,
                                     locals);
    emit_release_owned_call_temp(g, callee->member.object, recv, locals);
    return result;
}

static LLVMValueRef emit_expr_call(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER &&
            expr->call.callee->ident.name.len == 6 &&
            memcmp(expr->call.callee->ident.name.str, "nameof", 6) == 0 &&
            expr->call.args.count == 1) {
            zan_ast_node_t *na = expr->call.args.items[0];
            if (na && na->kind == AST_IDENTIFIER)
                return emit_string_literal_rc(g, na->ident.name);
            if (na && na->kind == AST_MEMBER_ACCESS)
                return emit_string_literal_rc(g, na->member.name);
        }
        /* Reflection (irgen_reflect */
        if (expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *recv = expr->call.callee->member.object;
            zan_istr_t mname = expr->call.callee->member.name;
            zan_ast_node_t **cargs = expr->call.args.items;
            int cargc = expr->call.args.count;
            zan_type_t *rt = infer_expr_type(g, recv, locals);
            LLVMValueRef rv = NULL;
            if (zan_refl_is_typeinfo(rt)) {
                if (refl_emit_typeinfo_member(g,
                                              emit_guarded_member_object(
                                                  g, expr->call.callee, locals),
                                              mname, cargs, cargc, locals, &rv))
                    return rv;
            } else if (zan_refl_is_reflectable(rt) &&
                       (!rt->sym || !get_method_sym(rt->sym, mname)) &&
                       zan_refl_instance_method(mname, NULL)) {
                if (refl_emit_instance_call(g, rt,
                                            emit_guarded_member_object(
                                                g, expr->call.callee, locals),
                                            mname, cargs, cargc, locals, &rv))
                    return rv;
            }
        }

        /* `v */
        if (expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 0) {
            zan_ast_node_t *recv = expr->call.callee->member.object;
            zan_istr_t mname = expr->call.callee->member.name;
            zan_type_t *nt = infer_expr_type(g, recv, locals);
            bool is_gvod = mname.len == 17 &&
                memcmp(mname.str, "GetValueOrDefault", 17) == 0;
            bool is_tostr = mname.len == 8 && memcmp(mname.str, "ToString", 8) == 0;
            if (nt && nt->kind == TYPE_NULLABLE && (is_gvod || is_tostr)) {
                LLVMValueRef nv = emit_expr(g, recv, locals);
                if (llvm_is_nullable(LLVMTypeOf(nv)))
                    return is_gvod ? nullable_get_payload(g, nv)
                                   : emit_to_cstr(g, nv);
            }
        }

        if (expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 1) {
            zan_ast_node_t *recv = expr->call.callee->member.object;
            zan_istr_t mname = expr->call.callee->member.name;
            zan_type_t *nt = infer_expr_type(g, recv, locals);
            bool is_gvod = mname.len == 17 &&
                memcmp(mname.str, "GetValueOrDefault", 17) == 0;
            if (nt && nt->kind == TYPE_NULLABLE && is_gvod) {
                LLVMValueRef nv = emit_expr(g, recv, locals);
                if (llvm_is_nullable(LLVMTypeOf(nv))) {
                    LLVMValueRef payload = nullable_get_payload(g, nv);
                    LLVMValueRef has_val = nullable_has_value(g, nv);
                    LLVMValueRef dflt = emit_expr(g, expr->call.args.items[0], locals);
                    if (LLVMTypeOf(dflt) != LLVMTypeOf(payload)) {
                        dflt = coerce_int_to(g, dflt, LLVMTypeOf(payload));
                    }
                    return LLVMBuildSelect(g->builder, has_val, payload, dflt, "gvod.val");
                }
            }
        }

        /* `md */
        if (expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 1) {
            zan_ast_node_t *recv = expr->call.callee->member.object;
            zan_istr_t mname = expr->call.callee->member.name;
            if (mname.len == 9 && memcmp(mname.str, "GetLength", 9) == 0) {
                zan_type_t *at = infer_expr_type(g, recv, locals);
                if (at && at->kind == TYPE_ARRAY && at->array_rank > 1) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef arr = emit_expr(g, recv, locals);
                    LLVMValueRef dim = emit_expr(g, expr->call.args.items[0], locals);
                    dim = emit_index_i64(g, dim, "gl.dim");
                    LLVMValueRef oob = zan_icmp(g->builder, LLVMIntUGE, dim,
                        LLVMConstInt(i64, at->array_rank, 0), "gl.oob");
                    emit_runtime_check(g, oob, expr->loc,
                        "GetLength dimension out of range");
                    dim = LLVMBuildSelect(g->builder, oob,
                        LLVMConstInt(i64, 0, 0), dim, "gl.dim.safe");
                    LLVMValueRef dim_ptr = LLVMBuildBitCast(g->builder, arr,
                        LLVMPointerType(i64, 0), "gl.dp");
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, dim_ptr,
                        &dim, 1, "gl.slot");
                    return LLVMBuildLoad2(g->builder, i64, slot, "gl.val");
                }
            }
        }

        if (expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 0) {
            zan_ast_node_t *recv = expr->call.callee->member.object;
            zan_istr_t mname = expr->call.callee->member.name;
            zan_type_t *ot = infer_expr_type(g, recv, locals);
            if (ot && ot->kind == TYPE_TASK) {
                bool is_wait = mname.len == 4 && memcmp(mname.str, "Wait", 4) == 0;
                bool is_res  = mname.len == 6 && memcmp(mname.str, "Result", 6) == 0;
                bool is_comp = mname.len == 11 && memcmp(mname.str, "IsCompleted", 11) == 0;
                if (is_wait || is_res || is_comp) {
                    LLVMTypeRef ti64 = LLVMInt64TypeInContext(g->ctx);
                    if (is_res && ot->type_arg_count != 1) {
                        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3012,
                            "Task.Result requires a Task<T> value");
                        return LLVMConstInt(ti64, 0, 0);
                    }
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef h = emit_expr(g, recv, locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(h)) == LLVMPointerTypeKind)
                        h = LLVMBuildPtrToInt(g->builder, h, ti64, "task.h");
                    else if (LLVMGetIntTypeWidth(LLVMTypeOf(h)) < 64)
                        h = zan_iwiden(g->builder, h, ti64);
                    LLVMValueRef hp = LLVMBuildIntToPtr(g->builder, h, i8ptr, "task.fp");
                    int mode = is_comp ? 2 : (is_res ? 1 :
                              (ot->type_arg_count == 1 ? 3 : 0));
                    return emit_task_member(g, hp,
                        ot->type_arg_count == 1 ? ot->type_args[0] : NULL, mode);
                }
            }
        }

        if ((is_call_to(expr, "Task", "Spawn") || is_call_to(expr, "Task", "Run")) &&
            expr->call.args.count == 1) {
            LLVMTypeRef sp_i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef sp_i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            zan_ast_node_t *sub_arg = expr->call.args.items[0];
            zan_type_t *sub_ty = infer_expr_type(g, sub_arg, locals);
            bool is_del = sub_ty && sub_ty->kind == TYPE_DELEGATE;
            if (sub_arg->kind != AST_CALL && sub_arg->kind != AST_LAMBDA && !is_del) {
            } else {

            if (sub_arg->kind == AST_LAMBDA || is_del) {
                LLVMValueRef lv = NULL;
                if (sub_arg->kind == AST_LAMBDA) {
                    if (sub_arg->lambda.params.count != 0 ||
                        lambda_body_has_value_return(sub_arg->lambda.body)) {
                        zan_diag_emit_code(g->diag, DIAG_ERROR, sub_arg->loc, 3052,
                            "Task.Run delegate must be a parameterless void body "
                            "(an Action); pass an async call for results");
                        return LLVMConstInt(sp_i64, 0, 0);
                    }
                    zan_type_t *adt = (zan_type_t *)zan_arena_alloc(
                        g->arena, sizeof(zan_type_t));
                    memset(adt, 0, sizeof(*adt));
                    adt->kind = TYPE_DELEGATE;
                    adt->name = (zan_istr_t){ (char *)"__TaskAction", 12 };
                    adt->delegate_is_async = 0;
                    adt->delegate_param_count = 0;
                    adt->delegate_ret_type = g->binder->type_void;
                    lv = emit_lambda_typed(g, sub_arg, adt, locals);
                } else {
                    if (sub_ty->delegate_param_count != 0) {
                        zan_diag_emit_code(g->diag, DIAG_ERROR, sub_arg->loc, 3053,
                            "Task.Run delegate must take no parameters");
                        return LLVMConstInt(sp_i64, 0, 0);
                    }
                    lv = emit_expr(g, sub_arg, locals);
                }
                return emit_task_action_run(g, lv, sub_arg, locals);
            }
            LLVMValueRef sub = emit_expr(g, sub_arg, locals);
            bool keep_result = is_call_to(expr, "Task", "Run") &&
                !is_call_to(expr, "Task", "Spawn") &&
                sub_ty && sub_ty->kind != TYPE_VOID;
            LLVMValueRef sub_i8 = emit_detach_async_call(g, sub, keep_result, true);
            emit_release_owned_call_temp(g, sub_arg, sub, locals);
            /* Task */
            if (sub_i8) return LLVMBuildPtrToInt(g->builder, sub_i8, sp_i64, "spawn.h");
            return LLVMConstInt(sp_i64, 0, 0);
            }
        }

        if (is_call_to(expr, "Task", "Cancel") && expr->call.args.count == 1) {
            LLVMTypeRef ci8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef ci64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef h = emit_expr(g, expr->call.args.items[0], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(h)) == LLVMIntegerTypeKind) {
                if (LLVMGetIntTypeWidth(LLVMTypeOf(h)) < 64)
                    h = zan_iwiden(g->builder, h, ci64);
                h = LLVMBuildIntToPtr(g->builder, h, ci8ptr, "cancel.h");
            } else {
                h = LLVMBuildBitCast(g->builder, h, ci8ptr, "cancel.h");
            }
            LLVMValueRef cf = get_co_cancel_fn(g);
            zan_call2(g->builder, LLVMGlobalGetValueType(cf), cf, &h, 1, "");
            return LLVMConstInt(ci64, 0, 0);
        }

        if (is_call_to(expr, "Task", "IsDone") && expr->call.args.count == 1) {
            LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef h = emit_expr(g, expr->call.args.items[0], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(h)) == LLVMIntegerTypeKind) {
                if (LLVMGetIntTypeWidth(LLVMTypeOf(h)) < 64)
                    h = zan_iwiden(g->builder, h, di64);
                h = LLVMBuildIntToPtr(g->builder, h, di8ptr, "isdone.h");
            } else {
                h = LLVMBuildBitCast(g->builder, h, di8ptr, "isdone.h");
            }
            LLVMValueRef idf = get_co_isdone_fn(g);
            return zan_call2(g->builder, LLVMGlobalGetValueType(idf),
                             idf, &h, 1, "isdone");
        }

        /* Task */
        if (is_call_to(expr, "Task", "JoinNew") && expr->call.args.count == 2) {
            LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef a0 = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef a1 = emit_expr(g, expr->call.args.items[1], locals);
            if (LLVMTypeOf(a0) != di32) a0 = LLVMBuildTrunc(g->builder, a0, di32, "join.n");
            if (LLVMTypeOf(a1) != di32) a1 = LLVMBuildTrunc(g->builder, a1, di32, "join.any");
            LLVMTypeRef jn_type = LLVMFunctionType(di64,
                (LLVMTypeRef[]){ di32, di32 }, 2, 0);
            LLVMValueRef jn = LLVMGetNamedFunction(g->mod, "zan_join_new");
            if (!jn) jn = LLVMAddFunction(g->mod, "zan_join_new", jn_type);
            return zan_call2(g->builder, jn_type, jn,
                (LLVMValueRef[]){ a0, a1 }, 2, "join.new");
        }

        /* Task */
        if (is_call_to(expr, "Task", "JoinBind") && expr->call.args.count == 3) {
            LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef a0 = emit_expr(g, expr->call.args.items[0], locals);
            if (LLVMTypeOf(a0) != di64) a0 = LLVMBuildIntCast2(g->builder, a0, di64, 1, "join64");
            LLVMValueRef a1 = emit_expr(g, expr->call.args.items[1], locals);
            if (LLVMTypeOf(a1) == di64)
                a1 = LLVMBuildIntToPtr(g->builder, a1, di8ptr, "join.h");
            else
                a1 = LLVMBuildBitCast(g->builder, a1, di8ptr, "join.h");
            LLVMValueRef a2 = emit_expr(g, expr->call.args.items[2], locals);
            if (LLVMTypeOf(a2) != di32) a2 = LLVMBuildTrunc(g->builder, a2, di32, "join.i");
            LLVMTypeRef jb_type = LLVMFunctionType(di32,
                (LLVMTypeRef[]){ di64, di8ptr, di32 }, 3, 0);
            LLVMValueRef jb = LLVMGetNamedFunction(g->mod, "zan_join_bind");
            if (!jb) jb = LLVMAddFunction(g->mod, "zan_join_bind", jb_type);
            return zan_call2(g->builder, jb_type, jb,
                (LLVMValueRef[]){ a0, a1, a2 }, 3, "join.bind");
        }

        /* Task */
        if (is_call_to(expr, "Task", "JoinCancel") && expr->call.args.count == 1) {
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef dvoid = LLVMVoidTypeInContext(g->ctx);
            LLVMValueRef a0 = emit_expr(g, expr->call.args.items[0], locals);
            if (LLVMTypeOf(a0) != di64) a0 = LLVMBuildIntCast2(g->builder, a0, di64, 1, "join64");
            LLVMTypeRef jc_type = LLVMFunctionType(dvoid, (LLVMTypeRef[]){ di64 }, 1, 0);
            LLVMValueRef jc = LLVMGetNamedFunction(g->mod, "zan_join_cancel");
            if (!jc) jc = LLVMAddFunction(g->mod, "zan_join_cancel", jc_type);
            zan_call2(g->builder, jc_type, jc, (LLVMValueRef[]){ a0 }, 1, "");
            return LLVMConstInt(di64, 0, 0);
        }

        /* Task */
        if (is_call_to(expr, "Task", "IsCancellationRequested") &&
            expr->call.args.count == 0) {
            LLVMTypeRef ci32 = LLVMInt32TypeInContext(g->ctx);
            if (!g->current_async_frame) return LLVMConstInt(ci32, 0, 0);
            return LLVMBuildLoad2(g->builder, ci32,
                LLVMBuildStructGEP2(g->builder, g->current_async_frame_type,
                    g->current_async_frame, ASYNC_FRAME_CANCEL, "fr.cancel.p"),
                "cancel.req");
        }

        /* StringBuilder */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sbcallee = expr->call.callee;
            zan_istr_t sbm = sbcallee->member.name;
            zan_type_t *sbt = infer_expr_type(g, sbcallee->member.object, locals);
            if (sbt && type_named(sbt, "StringBuilder", 13)) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef raw = emit_expr(g, sbcallee->member.object, locals);
                LLVMValueRef sbp = LLVMBuildBitCast(g->builder, raw,
                    LLVMPointerType(g->sb_struct_type, 0), "sbp");
                int sb_is_append = (sbm.len == 6 && memcmp(sbm.str, "Append", 6) == 0 &&
                                    expr->call.args.count == 1);
                int sb_is_appendline = (sbm.len == 10 && memcmp(sbm.str, "AppendLine", 10) == 0 &&
                                        expr->call.args.count <= 1);
                int sb_is_clear = (sbm.len == 5 && memcmp(sbm.str, "Clear", 5) == 0 &&
                                   expr->call.args.count == 0);
                int sb_is_tostring = (sbm.len == 8 && memcmp(sbm.str, "ToString", 8) == 0 &&
                                      expr->call.args.count == 0);
                int sb_recv_own = 0;
                if (sb_is_append || sb_is_appendline || sb_is_tostring || sb_is_clear)
                    sb_recv_own = emit_intrinsic_own_recv(g,
                        sbcallee->member.object, raw, locals);
                if (sb_is_clear) {
                    LLVMValueRef cptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 0, "sbcp");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), cptr);
                    LLVMValueRef dptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 2, "sbdp");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, i8ptr, dptr, "sbdv");
                    LLVMBasicBlockRef has_data = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sb_clr_data");
                    LLVMBasicBlockRef cont = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sb_clr_cont");
                    LLVMValueRef is_null = LLVMBuildICmp(g->builder, LLVMIntEQ, data, LLVMConstNull(i8ptr), "sb_data_null");
                    LLVMBuildCondBr(g->builder, is_null, cont, has_data);
                    LLVMPositionBuilderAtEnd(g->builder, has_data);
                    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), data);
                    LLVMBuildBr(g->builder, cont);
                    LLVMPositionBuilderAtEnd(g->builder, cont);
                    emit_intrinsic_drop_recv(g, sbcallee->member.object, raw,
                                             locals, sb_recv_own);
                    return raw;
                }
                if (sb_is_append || sb_is_appendline) {
                    if (expr->call.args.count == 1) {
                        zan_ast_node_t *arg0 = expr->call.args.items[0];
                        LLVMValueRef v = emit_expr(g, arg0, locals);
                        int char_arg =
                            LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind &&
                            expr_is_char(g, arg0, locals);
                        int int_arg = !char_arg &&
                            LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(LLVMTypeOf(v)) > 1;
                        LLVMValueRef s, slen;
                        if (int_arg) {
                            s = emit_entry_scratch(g, 40, "sb.i2s");
                            slen = emit_itoa_into(g, s,
                                emit_widen_i64_for_print(g, v),
                                expr_is_ulong(g, arg0, locals) ? 1 : 0);
                        } else {
                            s = char_arg ? emit_char_to_cstr(g, v)
                                         : emit_value_as_cstr(g, v);
                            int str_arg = !char_arg &&
                                LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind &&
                                is_string_expr(g, arg0, locals);
                            if (str_arg) {
                                s = emit_str_nonnull(g, s);
                                slen = emit_string_buffer_len(g, s, arg0->loc);
                            } else {
                                slen = emit_cstr_len_of(g, s, arg0);
                            }
                        }
                        emit_sb_append_bytes(g, sbp, s, slen);
                        if (char_arg)
                            emit_string_release(g, s);
                        else if (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind)
                            emit_release_owned_call_temp(g, arg0, v, locals);
                    }
                    if (sb_is_appendline) {
                        zan_istr_t nl = { "\n", 1 };
                        LLVMValueRef nls = emit_string_literal_rc(g, nl);
                        emit_sb_append_bytes(g, sbp, nls, LLVMConstInt(i64, 1, 0));
                    }
                    emit_intrinsic_drop_recv(g, sbcallee->member.object, raw,
                                             locals, sb_recv_own);
                    return raw;
                }
                if (sb_is_tostring) {
                    LLVMValueRef cptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 0, "sbcp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, cptr, "sbcv");
                    LLVMValueRef dptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 2, "sbdp");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, i8ptr, dptr, "sbdv");
                    LLVMValueRef bsz = zan_add(g->builder, count, LLVMConstInt(i64, 1, 0), "sbbsz");
                    LLVMValueRef buf = emit_string_alloc_rc(g, bsz);
                    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
                    if (!memcpy_fn) {
                        memcpy_fn = LLVMAddFunction(g->mod, "memcpy",
                            LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0));
                    }
                    zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0),
                        memcpy_fn, (LLVMValueRef[]){ buf, data, count }, 3, "");
                    LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, buf, &count, 1, "sbend");
                    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
                    emit_string_len_set(g, buf, count);
                    emit_intrinsic_drop_recv(g, sbcallee->member.object, raw,
                                             locals, sb_recv_own);
                    return buf;
                }
            }
        }

        /* Console */
        if (!zan_type_defines(g, "Console", "WriteLine") &&
            (is_call_to(expr, "Console", "WriteLine") ||
             is_call_to(expr, "Console", "PrintLine"))) {
            if (expr->call.args.count > 0) {
                zan_ast_node_t *arg_ast = expr->call.args.items[0];
                LLVMValueRef arg = emit_expr(g, arg_ast, locals);
                LLVMTypeRef arg_type = LLVMTypeOf(arg);

                if (LLVMGetTypeKind(arg_type) == LLVMPointerTypeKind) {
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0) },
                        1, 0);
                    zan_call2(g->builder, fn_type, g->rt_println, &arg, 1, "");
                } else if (LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind) {
                    LLVMTypeRef flt = LLVMFloatTypeInContext(g->ctx);
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ flt },
                        1, 0);
                    zan_call2(g->builder, fn_type, g->rt_print_float, &arg, 1, "");
                } else if (LLVMGetTypeKind(arg_type) == LLVMDoubleTypeKind) {
                    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ dbl },
                        1, 0);
                    zan_call2(g->builder, fn_type, g->rt_print_double, &arg, 1, "");
                } else if (llvm_is_nullable(arg_type)) {
                    LLVMValueRef ns = emit_to_cstr(g, arg);
                    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    zan_call2(g->builder,
                        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                         (LLVMTypeRef[]){ i8p }, 1, 0),
                        g->rt_println, &ns, 1, "");
                    emit_string_release(g, ns);
                } else if (expr_is_char(g, arg_ast, locals)) {
                    LLVMValueRef cs = emit_char_to_cstr(g, arg);
                    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    zan_call2(g->builder,
                        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                         (LLVMTypeRef[]){ i8p }, 1, 0),
                        g->rt_println, &cs, 1, "");
                    emit_string_release(g, cs);
                } else if (expr_is_bool(g, arg_ast, locals)) {
                    LLVMValueRef t = emit_string_literal_rc(g,
                        (zan_istr_t){ "true", 4 });
                    LLVMValueRef f = emit_string_literal_rc(g,
                        (zan_istr_t){ "false", 5 });
                    LLVMValueRef cond = arg;
                    if (LLVMGetTypeKind(LLVMTypeOf(arg)) ==
                            LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(arg)) != 1)
                        cond = LLVMBuildICmp(g->builder, LLVMIntNE, arg,
                            LLVMConstNull(LLVMTypeOf(arg)), "wl.bnz");
                    LLVMValueRef bs = LLVMBuildSelect(g->builder, cond, t, f,
                                                      "wl.b");
                    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    zan_call2(g->builder,
                        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                         (LLVMTypeRef[]){ i8p }, 1, 0),
                        g->rt_println, &bs, 1, "");
                } else if (arg_type == get_decimal_type(g)) {
                    /* decimal 结构体不能掉进 print_int（曾触发 LLVM 校验失败，
                     * TASKS.md A511③）：转托管串后按字符串打印。 */
                    LLVMValueRef ds = emit_to_cstr_u(g, arg, false);
                    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    zan_call2(g->builder,
                        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                         (LLVMTypeRef[]){ i8p }, 1, 0),
                        g->rt_println, &ds, 1, "");
                    emit_string_release(g, ds);
                } else {
                    bool print_unsigned = expr_is_unsigned_int(g, arg_ast, locals);
                    if (print_unsigned) {
                        LLVMTypeRef vt = LLVMTypeOf(arg);
                        if (LLVMGetTypeKind(vt) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(vt) < 64)
                            arg = LLVMBuildZExt(g->builder, arg,
                                LLVMInt64TypeInContext(g->ctx), "print.zext");
                        else
                            arg = emit_widen_i64_for_print(g, arg);
                    } else {
                        arg = emit_widen_i64_for_print(g, arg);
                    }
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ LLVMInt64TypeInContext(g->ctx) },
                        1, 0);
                    LLVMValueRef print_fn = print_unsigned
                        ? g->rt_print_uint : g->rt_print_int;
                    zan_call2(g->builder, fn_type, print_fn, &arg, 1, "");
                }
                emit_release_owned_call_temp(g, arg_ast, arg, locals);
            } else {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef empty = zan_irgen_intern_string(g, "");
                zan_call2(g->builder,
                    LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    g->rt_println, &empty, 1, "");
            }
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Console.ErrorWriteLine：stderr 行输出（A383——图表引擎的未知
         * option 键告警走 stderr，不污染 stdout 黄金）。收窄成 string 单
         * 签名：告警都是预拼好的文本，非串实参请先 Convert.ToString
         * （C# 的多态重载在 Zan 面上按内建收窄先例处理）。 */
        if (!zan_type_defines(g, "Console", "ErrorWriteLine") &&
            is_call_to(expr, "Console", "ErrorWriteLine")) {
            if (expr->call.args.count > 0) {
                zan_ast_node_t *arg_ast = expr->call.args.items[0];
                LLVMValueRef arg = emit_expr(g, arg_ast, locals);
                if (LLVMGetTypeKind(LLVMTypeOf(arg)) != LLVMPointerTypeKind) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3054,
                        "Console.ErrorWriteLine takes a string: format the "
                        "value with Convert.ToString first");
                    return emit_unresolved_call_result(g, expr, locals);
                }
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef fn_type = LLVMFunctionType(
                    LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0);
                LLVMValueRef efn = LLVMGetNamedFunction(g->mod, "zan_rt_eprint_line");
                if (!efn) efn = LLVMAddFunction(g->mod, "zan_rt_eprint_line", fn_type);
                zan_call2(g->builder, fn_type, efn, &arg, 1, "");
                emit_release_owned_call_temp(g, arg_ast, arg, locals);
            }
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Console.Write (no newline) */
        if (!zan_type_defines(g, "Console", "Write") &&
            is_call_to(expr, "Console", "Write")) {
            if (expr->call.args.count > 0) {
                zan_ast_node_t *arg_ast = expr->call.args.items[0];
                LLVMValueRef arg = emit_expr(g, arg_ast, locals);
                LLVMTypeRef arg_type = LLVMTypeOf(arg);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef printf_args[] = { i8ptr };
                LLVMTypeRef printf_type = LLVMFunctionType(
                    LLVMInt32TypeInContext(g->ctx), printf_args, 1, 1);
                LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");

                if (LLVMGetTypeKind(arg_type) == LLVMPointerTypeKind) {
                    LLVMValueRef fmt = zan_irgen_intern_string(g, "%s");
                    LLVMValueRef args[] = { fmt, arg };
                    zan_call2(g->builder, printf_type, printf_fn, args, 2, "");
                } else if (LLVMGetTypeKind(arg_type) == LLVMDoubleTypeKind ||
                           LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind) {
                    LLVMValueRef sbuf = emit_entry_scratch(g, 40, "wl.dbl");
                    emit_dbl_str(g, sbuf,
                        LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 40, 0),
                        arg);
                    LLVMValueRef fmt = zan_irgen_intern_string(g, "%s");
                    LLVMValueRef args[] = { fmt, sbuf };
                    zan_call2(g->builder, printf_type, printf_fn, args, 2, "");
                } else if (llvm_is_nullable(arg_type)) {
                    LLVMValueRef ns = emit_to_cstr(g, arg);
                    LLVMValueRef nfmt = zan_irgen_intern_string(g, "%s");
                    LLVMValueRef nargs[] = { nfmt, ns };
                    zan_call2(g->builder, printf_type, printf_fn, nargs, 2, "");
                    emit_string_release(g, ns);
                } else if (expr_is_char(g, arg_ast, locals)) {
                    LLVMValueRef cs = emit_char_to_cstr(g, arg);
                    LLVMValueRef cfmt = zan_irgen_intern_string(g, "%s");
                    LLVMValueRef cargs[] = { cfmt, cs };
                    zan_call2(g->builder, printf_type, printf_fn, cargs, 2, "");
                    emit_string_release(g, cs);
                } else if (expr_is_bool(g, arg_ast, locals)) {
                    LLVMValueRef t = emit_string_literal_rc(g,
                        (zan_istr_t){ "true", 4 });
                    LLVMValueRef f = emit_string_literal_rc(g,
                        (zan_istr_t){ "false", 5 });
                    LLVMValueRef cond = arg;
                    if (LLVMGetTypeKind(LLVMTypeOf(arg)) ==
                            LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(arg)) != 1)
                        cond = LLVMBuildICmp(g->builder, LLVMIntNE, arg,
                            LLVMConstNull(LLVMTypeOf(arg)), "w.bnz");
                    LLVMValueRef bs = LLVMBuildSelect(g->builder, cond, t, f,
                                                      "w.b");
                    LLVMValueRef bfmt = zan_irgen_intern_string(g, "%s");
                    LLVMValueRef bargs[] = { bfmt, bs };
                    zan_call2(g->builder, printf_type, printf_fn, bargs, 2, "");
                } else {
                    LLVMValueRef fmt = expr_is_ulong(g, arg_ast, locals)
                        ? zan_irgen_intern_string(g, "%llu")
                        : zan_irgen_intern_string(g, "%lld");
                    LLVMValueRef int_arg = emit_widen_i64_for_print(g, arg);
                    LLVMValueRef args[] = { fmt, int_arg };
                    zan_call2(g->builder, printf_type, printf_fn, args, 2, "");
                }
                emit_release_owned_call_temp(g, arg_ast, arg, locals);
            }
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        if (!zan_type_defines(g, "Console", "ReadLine") &&
            is_call_to(expr, "Console", "ReadLine")) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf_size = LLVMConstInt(i64, 1024, 0);
            LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
            LLVMValueRef fgets_fn = LLVMGetNamedFunction(g->mod, "fgets");
            if (!fgets_fn) {
                LLVMTypeRef fgets_args[] = { i8ptr, LLVMInt32TypeInContext(g->ctx), i8ptr };
                LLVMTypeRef fgets_type = LLVMFunctionType(i8ptr, fgets_args, 3, 0);
                fgets_fn = LLVMAddFunction(g->mod, "fgets", fgets_type);
            }
            LLVMValueRef stdin_ptr;
            if (g->target_is_windows) {
                LLVMValueRef stdin_fn = LLVMGetNamedFunction(g->mod, "__acrt_iob_func");
                if (!stdin_fn) {
                    LLVMTypeRef iob_args[] = { LLVMInt32TypeInContext(g->ctx) };
                    LLVMTypeRef iob_type = LLVMFunctionType(i8ptr, iob_args, 1, 0);
                    stdin_fn = LLVMAddFunction(g->mod, "__acrt_iob_func", iob_type);
                }
                LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                stdin_ptr = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ LLVMInt32TypeInContext(g->ctx) }, 1, 0),
                    stdin_fn, &zero, 1, "stdin");
            } else {
                const char *siname = g->target_is_macos ? "__stdinp" : "stdin";
                LLVMValueRef stdin_g = LLVMGetNamedGlobal(g->mod, siname);
                if (!stdin_g) { stdin_g = LLVMAddGlobal(g->mod, i8ptr, siname); }
                stdin_ptr = LLVMBuildLoad2(g->builder, i8ptr, stdin_g, "stdin");
            }
            LLVMValueRef sz = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1024, 0);
            LLVMValueRef fgets_args[] = { buf, sz, stdin_ptr };
            LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0),
                buf);
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, LLVMInt32TypeInContext(g->ctx), i8ptr }, 3, 0),
                fgets_fn, fgets_args, 3, "");
            return buf;
        }

        /* Console */
        if (!zan_type_defines(g, "Console", "Read") &&
            is_call_to(expr, "Console", "Read")) {
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef getchar_ty = LLVMFunctionType(i32t, NULL, 0, 0);
            LLVMValueRef getchar_fn = get_libc_fn(g, "getchar", getchar_ty);
            LLVMValueRef r = zan_call2(g->builder, getchar_ty, getchar_fn, NULL, 0, "cread");
            return LLVMBuildSExt(g->builder, r, i64t, "cread64");
        }

        if (!zan_type_defines(g, "Console", "ReadKey") &&
            is_call_to(expr, "Console", "ReadKey")) {
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            const char *fname = g->target_is_windows ? "_getch" : "getchar";
            LLVMTypeRef fty = LLVMFunctionType(i32t, NULL, 0, 0);
            LLVMValueRef fn = get_libc_fn(g, fname, fty);
            LLVMValueRef r = zan_call2(g->builder, fty, fn, NULL, 0, "readkey");
            return LLVMBuildSExt(g->builder, r, i64t, "readkey64");
        }

        /* Console */
        if (!zan_type_defines(g, "Console", "Clear") &&
            is_call_to(expr, "Console", "Clear")) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef printf_type = LLVMFunctionType(
                LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 1);
            LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");
            if (!printf_fn) printf_fn = LLVMAddFunction(g->mod, "printf", printf_type);
            LLVMValueRef esc = zan_irgen_intern_string(g, "\033[2J\033[H");
            zan_call2(g->builder, printf_type, printf_fn, &esc, 1, "");
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        if (!zan_type_defines(g, "Console", "ResetColor") &&
            is_call_to(expr, "Console", "ResetColor")) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef printf_type = LLVMFunctionType(
                LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 1);
            LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");
            if (!printf_fn) printf_fn = LLVMAddFunction(g->mod, "printf", printf_type);
            LLVMValueRef esc = zan_irgen_intern_string(g, "\033[0m");
            zan_call2(g->builder, printf_type, printf_fn, &esc, 1, "");
            emit_console_color_reset(g);
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Console.IsOutputRedirected() / Console.IsTerminal()
         *
         * 问 libc "stdout 是不是终端"。直接发 libc 的 isatty 调用——**不新增
         * 运行时导出符号**：各平台的预编译运行时目标在非目标平台上重建不了，
         * 新增导出符号会让交叉目标全部失效。 */
        if (!zan_type_defines(g, "Console", "IsOutputRedirected") &&
            !zan_type_defines(g, "Console", "IsTerminal") &&
            (is_call_to(expr, "Console", "IsOutputRedirected") ||
             is_call_to(expr, "Console", "IsTerminal"))) {
            bool want_terminal = is_call_to(expr, "Console", "IsTerminal");
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef flag;
            if (g->target_is_wasm) {
                /* wasm 没有终端概念：stdout 永远不是终端（= 已重定向） */
                flag = LLVMConstInt(i32, want_terminal ? 0 : 1, 0);
            } else {
                const char *fname = g->target_is_windows ? "_isatty" : "isatty";
                LLVMTypeRef fty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i32 }, 1, 0);
                LLVMValueRef fn = get_libc_fn(g, fname, fty);
                LLVMValueRef fd = LLVMConstInt(i32, 1, 0);   /* stdout */
                LLVMValueRef r = zan_call2(g->builder, fty, fn, &fd, 1, "isatty");
                flag = want_terminal
                    ? zan_icmp(g->builder, LLVMIntNE, r, LLVMConstInt(i32, 0, 0), "tty")
                    : zan_icmp(g->builder, LLVMIntEQ, r, LLVMConstInt(i32, 0, 0), "redir");
            }
            return LLVMBuildZExt(g->builder, flag,
                                 LLVMInt64TypeInContext(g->ctx), "tty64");
        }

        /* NativeMemory 底层离堆内存操作原语与 CRC32 等系统调用封装 */
        {
            LLVMValueRef nm_out = NULL;
            if (emit_native_memory_call(g, expr, locals, &nm_out))
                return nm_out;
        }

        {
            LLVMValueRef px_out = NULL;
            if (emit_pixel_ops_call(g, expr, locals, &px_out))
                return px_out;
        }

        {
            LLVMValueRef bit_out = NULL;
            if (emit_bit_operations_call(g, expr, locals, &bit_out))
                return bit_out;
        }

        {
            LLVMValueRef cpu_out = NULL;
            if (emit_cpu_call(g, expr, locals, &cpu_out))
                return cpu_out;
        }

        {
            LLVMValueRef vec_out = NULL;
            if (emit_vector128_call(g, expr, locals, &vec_out))
                return vec_out;
            if (emit_vector256_call(g, expr, locals, &vec_out))
                return vec_out;
            if (emit_sse2_call(g, expr, locals, &vec_out))
                return vec_out;
            if (emit_sse42_call(g, expr, locals, &vec_out))
                return vec_out;
            if (emit_aes_call(g, expr, locals, &vec_out))
                return vec_out;
        }

        {
            LLVMValueRef sp_out = NULL;
            if (emit_span_call(g, expr, locals, &sp_out))
                return sp_out;
        }

        {
            LLVMValueRef by_out = NULL;
            if (emit_bytes_call(g, expr, locals, &by_out))
                return by_out;
        }

        /* String */
        if (is_call_to(expr, "String", "CompareOrdinal") && expr->call.args.count == 2) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            zan_ast_node_t *a_ast = expr->call.args.items[0];
            zan_ast_node_t *b_ast = expr->call.args.items[1];
            LLVMValueRef a = emit_expr(g, a_ast, locals);
            LLVMValueRef b = emit_expr(g, b_ast, locals);
            LLVMTypeRef strcmp_ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
            LLVMValueRef strcmp_fn = get_libc_fn(g, "strcmp", strcmp_ty);
            LLVMValueRef r = zan_call2(g->builder, strcmp_ty, strcmp_fn,
                (LLVMValueRef[]){ a, b }, 2, "ordcmp");
            emit_release_owned_call_temp(g, a_ast, a, locals);
            emit_release_owned_call_temp(g, b_ast, b, locals);
            return LLVMBuildSExt(g->builder, r, i64t, "ordcmp64");
        }

        /* Math.Sqrt(expr) → llvm.sqrt */
        if (!zan_type_defines(g, "Math", "Sqrt") &&
            is_call_to(expr, "Math", "Sqrt") && expr->call.args.count == 1) {
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            if (!math_require_numeric(g, expr, expr->call.args.items[0], arg,
                                      locals, "Sqrt"))
                return LLVMConstReal(dbl, 0.0);
            arg = math_arg_to_double(g, arg);
            LLVMValueRef sqrt_fn = LLVMGetNamedFunction(g->mod, "sqrt");
            if (!sqrt_fn) {
                LLVMTypeRef sqrt_args[] = { dbl };
                LLVMTypeRef sqrt_type = LLVMFunctionType(dbl, sqrt_args, 1, 0);
                sqrt_fn = LLVMAddFunction(g->mod, "sqrt", sqrt_type);
            }
            LLVMTypeRef sqrt_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
            return zan_call2(g->builder, sqrt_type, sqrt_fn, &arg, 1, "sqrt");
        }

        /* Math */
        {
            static const char *math1[] = { "Sin", "sin", "Cos", "cos", "Tan", "tan",
                                           "Log", "log", "Exp", "exp",
                                           "Atan", "atan", "Asin", "asin", "Acos", "acos", NULL };
            for (int mi = 0; math1[mi]; mi += 2) {
                if (!zan_type_defines(g, "Math", math1[mi]) &&
                    is_call_to(expr, "Math", math1[mi]) && expr->call.args.count == 1) {
                    LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                    if (!math_require_numeric(g, expr, expr->call.args.items[0], arg, locals, math1[mi]))
                        return LLVMConstReal(dbl, 0.0);
                    arg = math_arg_to_double(g, arg);
                    LLVMTypeRef fty = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
                    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, math1[mi + 1]);
                    if (!fn) fn = LLVMAddFunction(g->mod, math1[mi + 1], fty);
                    return zan_call2(g->builder, fty, fn, &arg, 1, math1[mi + 1]);
                }
            }
        }

        if (!zan_type_defines(g, "Math", "Atan2") &&
            is_call_to(expr, "Math", "Atan2") && expr->call.args.count == 2) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMValueRef y_v = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef x_v = emit_expr(g, expr->call.args.items[1], locals);
            if (!math_require_numeric(g, expr, expr->call.args.items[0], y_v, locals, "Atan2") ||
                !math_require_numeric(g, expr, expr->call.args.items[1], x_v, locals, "Atan2"))
                return LLVMConstReal(dbl, 0.0);
            y_v = math_arg_to_double(g, y_v);
            x_v = math_arg_to_double(g, x_v);
            LLVMValueRef atan2_fn = LLVMGetNamedFunction(g->mod, "atan2");
            if (!atan2_fn) {
                LLVMTypeRef atan2_args[] = { dbl, dbl };
                LLVMTypeRef atan2_type = LLVMFunctionType(dbl, atan2_args, 2, 0);
                atan2_fn = LLVMAddFunction(g->mod, "atan2", atan2_type);
            }
            LLVMValueRef args[] = { y_v, x_v };
            return zan_call2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl, dbl }, 2, 0),
                atan2_fn, args, 2, "atan2");
        }

        /* Math.Abs(expr) */
        if (!zan_type_defines(g, "Math", "Abs") &&
            is_call_to(expr, "Math", "Abs") && expr->call.args.count == 1) {
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef arg_type = LLVMTypeOf(arg);
            if (!math_require_numeric(g, expr, expr->call.args.items[0], arg, locals, "Abs"))
                return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
            if (LLVMGetTypeKind(arg_type) == LLVMDoubleTypeKind ||
                LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind) {
                LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                if (LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind)
                    arg = LLVMBuildFPExt(g->builder, arg, dbl, "ext");
                LLVMValueRef fabs_fn = LLVMGetNamedFunction(g->mod, "fabs");
                if (!fabs_fn) {
                    LLVMTypeRef fabs_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
                    fabs_fn = LLVMAddFunction(g->mod, "fabs", fabs_type);
                }
                return zan_call2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0),
                    fabs_fn, &arg, 1, "fabs");
            } else {
                unsigned abs_bits = LLVMGetIntTypeWidth(arg_type);
                LLVMValueRef shift = zan_ashr(g->builder, arg,
                    LLVMConstInt(arg_type, abs_bits - 1, 0), "sh");
                LLVMValueRef xor = zan_xor(g->builder, arg, shift, "xor");
                return zan_sub(g->builder, xor, shift, "abs");
            }
        }

        /* Math */
        if (!zan_type_defines(g, "Math", "Max") && !zan_type_defines(g, "Math", "Min") &&
            (is_call_to(expr, "Math", "Max") || is_call_to(expr, "Math", "Min")) &&
            expr->call.args.count == 2) {
            int want_max = is_call_to(expr, "Math", "Max");
            LLVMValueRef a = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef b = emit_expr(g, expr->call.args.items[1], locals);
            LLVMTypeKind ka = LLVMGetTypeKind(LLVMTypeOf(a));
            LLVMTypeKind kb = LLVMGetTypeKind(LLVMTypeOf(b));
            int fa = (ka == LLVMDoubleTypeKind || ka == LLVMFloatTypeKind);
            int fb = (kb == LLVMDoubleTypeKind || kb == LLVMFloatTypeKind);
            /* 守卫要逐个参数判：原来的写法是 "ka != Int && kb != Int"，
               遇到「一个指针 + 一个整数」就不报错了，pointer 随后被
               sitofp 成 double——生成非法 IR，编译死在代码生成阶段。 */
            if (!math_arg_is_numeric(a) || !math_arg_is_numeric(b)) {
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3055,
                              "Math.%s requires numeric arguments",
                              want_max ? "Max" : "Min");
                return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
            }
            if (fa || fb) {
                LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                a = fa ? (ka == LLVMFloatTypeKind
                            ? LLVMBuildFPExt(g->builder, a, dbl, "ext")
                            : a)
                       : LLVMBuildSIToFP(g->builder, a, dbl, "tofp");
                b = fb ? (kb == LLVMFloatTypeKind
                            ? LLVMBuildFPExt(g->builder, b, dbl, "ext")
                            : b)
                       : LLVMBuildSIToFP(g->builder, b, dbl, "tofp");
                LLVMValueRef a_nan = LLVMBuildFCmp(g->builder,
                    LLVMRealUNO, a, a, "an");
                LLVMValueRef b_nan = LLVMBuildFCmp(g->builder,
                    LLVMRealUNO, b, b, "bn");
                LLVMValueRef cmp = LLVMBuildFCmp(g->builder,
                    want_max ? LLVMRealOGT : LLVMRealOLT, a, b, "cmp");
                LLVMValueRef picked = LLVMBuildSelect(g->builder, cmp, a, b,
                    want_max ? "max" : "min");
                LLVMValueRef from_b = LLVMBuildSelect(g->builder, b_nan, b,
                    picked, want_max ? "max" : "min");
                return LLVMBuildSelect(g->builder, a_nan, a, from_b,
                    want_max ? "max" : "min");
            }
            unsigned wa = LLVMGetIntTypeWidth(LLVMTypeOf(a));
            unsigned wb = LLVMGetIntTypeWidth(LLVMTypeOf(b));
            if (wa < wb) a = LLVMBuildSExt(g->builder, a, LLVMTypeOf(b), "sx");
            else if (wb < wa) b = LLVMBuildSExt(g->builder, b, LLVMTypeOf(a), "sx");
            LLVMValueRef cmp = zan_icmp(g->builder,
                want_max ? LLVMIntSGT : LLVMIntSLT, a, b, "cmp");
            return LLVMBuildSelect(g->builder, cmp, a, b, want_max ? "max" : "min");
        }

        if (!zan_type_defines(g, "Math", "Pow") &&
            is_call_to(expr, "Math", "Pow") && expr->call.args.count == 2) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMValueRef base_v = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef exp_v = emit_expr(g, expr->call.args.items[1], locals);
            if (!math_require_numeric(g, expr, expr->call.args.items[0], base_v, locals, "Pow") ||
                !math_require_numeric(g, expr, expr->call.args.items[1], exp_v, locals, "Pow"))
                return LLVMConstReal(dbl, 0.0);
            base_v = math_arg_to_double(g, base_v);
            exp_v = math_arg_to_double(g, exp_v);
            LLVMValueRef pow_fn = LLVMGetNamedFunction(g->mod, "pow");
            if (!pow_fn) {
                LLVMTypeRef pow_args[] = { dbl, dbl };
                LLVMTypeRef pow_type = LLVMFunctionType(dbl, pow_args, 2, 0);
                pow_fn = LLVMAddFunction(g->mod, "pow", pow_type);
            }
            LLVMValueRef args[] = { base_v, exp_v };
            return zan_call2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl, dbl }, 2, 0),
                pow_fn, args, 2, "pow");
        }

        if (!zan_type_defines(g, "Math", "Floor") &&
            is_call_to(expr, "Math", "Floor") && expr->call.args.count == 1) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            if (!math_require_numeric(g, expr, expr->call.args.items[0], arg, locals, "Floor"))
                return LLVMConstReal(dbl, 0.0);
            arg = math_arg_to_double(g, arg);
            LLVMValueRef floor_fn = LLVMGetNamedFunction(g->mod, "floor");
            if (!floor_fn) {
                LLVMTypeRef floor_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
                floor_fn = LLVMAddFunction(g->mod, "floor", floor_type);
            }
            return zan_call2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0),
                floor_fn, &arg, 1, "floor");
        }

        if (!zan_type_defines(g, "Math", "Ceiling") &&
            is_call_to(expr, "Math", "Ceiling") && expr->call.args.count == 1) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            if (!math_require_numeric(g, expr, expr->call.args.items[0], arg, locals, "Ceiling"))
                return LLVMConstReal(dbl, 0.0);
            arg = math_arg_to_double(g, arg);
            LLVMValueRef ceil_fn = LLVMGetNamedFunction(g->mod, "ceil");
            if (!ceil_fn) {
                LLVMTypeRef ceil_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
                ceil_fn = LLVMAddFunction(g->mod, "ceil", ceil_type);
            }
            return zan_call2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0),
                ceil_fn, &arg, 1, "ceil");
        }

        /* Math */
        if (!zan_type_defines(g, "Math", "Round") &&
            is_call_to(expr, "Math", "Round") &&
            (expr->call.args.count == 1 || expr->call.args.count == 2)) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMTypeRef d1_ty = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            if (!math_require_numeric(g, expr, expr->call.args.items[0], arg, locals, "Round"))
                return LLVMConstReal(dbl, 0.0);
            arg = math_arg_to_double(g, arg);
            /* C# Math.Round 是银行家舍入（to-even）——libc rint 按默认 FP
             * 舍入模式（最近偶数）实现；libc round 是远离零，曾与 C# 分叉
             * （Round(2.5) 得 3，C# 是 2；TASKS.md A513）。 */
            LLVMValueRef round_fn = get_libc_fn(g, "rint", d1_ty);
            if (expr->call.args.count == 1)
                return zan_call2(g->builder, d1_ty, round_fn, &arg, 1, "rint");
            /* digits: rint(x * 10^d) / 10^d（同样银行家） */
            LLVMValueRef digits = emit_expr(g, expr->call.args.items[1], locals);
            if (!math_require_numeric(g, expr, expr->call.args.items[1], digits, locals, "Round"))
                return LLVMConstReal(dbl, 0.0);
            digits = math_arg_to_double(g, digits);
            LLVMTypeRef d2_ty = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl, dbl }, 2, 0);
            LLVMValueRef pow_fn = get_libc_fn(g, "pow", d2_ty);
            LLVMValueRef scale = zan_call2(g->builder, d2_ty, pow_fn,
                (LLVMValueRef[]){ LLVMConstReal(dbl, 10.0), digits }, 2, "scale");
            LLVMValueRef scaled = LLVMBuildFMul(g->builder, arg, scale, "scaled");
            LLVMValueRef r = zan_call2(g->builder, d1_ty, round_fn, &scaled, 1, "rint");
            return LLVMBuildFDiv(g->builder, r, scale, "rdig");
        }

        /* char 的静态类（A525）：char.IsDigit('5') 等。谓词走 <ctype.h>
         * （ASCII 口径；非 ASCII 字符与 C# 的 Unicode 类目有偏差）。
         * ToUpper/ToLower 走 Unicode 简单映射表（见 get_char_case_fn）。 */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->call.callee->member.object->ident.name) &&
            expr->call.args.count == 1) {
            zan_istr_t cn = expr->call.callee->member.object->ident.name;
            int char_cls = (cn.len == 4 &&
                            (memcmp(cn.str, "char", 4) == 0 ||
                             memcmp(cn.str, "Char", 4) == 0));
            if (char_cls) {
                zan_istr_t cm = expr->call.callee->member.name;
                static const struct { const char *name; const char *libc; } preds[] = {
                    { "IsDigit", "isdigit" }, { "IsLetter", "isalpha" },
                    { "IsLetterOrDigit", "isalnum" }, { "IsUpper", "isupper" },
                    { "IsLower", "islower" }, { "IsWhiteSpace", "isspace" },
                };
                static const struct { const char *name; const char *libc; } mappers[] = {
                    { "ToUpper", "toupper" }, { "ToLower", "tolower" },
                };
                LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                LLVMTypeRef aty = LLVMFunctionType(i32t,
                    (LLVMTypeRef[]){ i32t }, 1, 0);
                LLVMValueRef cp = arg;
                if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMIntegerTypeKind &&
                    LLVMGetIntTypeWidth(LLVMTypeOf(arg)) > 32)
                    cp = LLVMBuildTrunc(g->builder, arg, i32t, "ch.cp32");
                for (size_t pi = 0; pi < sizeof(preds) / sizeof(preds[0]); pi++) {
                    size_t nl = strlen(preds[pi].name);
                    if (cm.len != (int)nl || memcmp(cm.str, preds[pi].name, nl) != 0)
                        continue;
                    LLVMValueRef r = zan_call2(g->builder, aty,
                        get_libc_fn(g, preds[pi].libc, aty), &cp, 1, "ch.pred");
                    LLVMValueRef nz = zan_icmp(g->builder, LLVMIntNE, r,
                        LLVMConstInt(i32t, 0, 0), "ch.nz");
                    emit_release_owned_call_temp(g, expr->call.args.items[0],
                                                 arg, locals);
                    return nz;
                }
                for (size_t mi = 0; mi < sizeof(mappers) / sizeof(mappers[0]); mi++) {
                    size_t nl = strlen(mappers[mi].name);
                    if (cm.len != (int)nl || memcmp(cm.str, mappers[mi].name, nl) != 0)
                        continue;
                    /* Unicode 简单大小写映射（审计 P1-12 / A525 后续）：
                     * libc toupper/tolower 是 C locale ASCII 口径，非 ASCII
                     * 原样返回；表助手覆盖全 Unicode 1:1 简单映射。 */
                    LLVMTypeRef case_ty = LLVMFunctionType(i32t,
                        (LLVMTypeRef[]){ i32t, i32t }, 2, 0);
                    LLVMValueRef upc = LLVMConstInt(i32t,
                        strcmp(mappers[mi].name, "ToUpper") == 0 ? 1 : 0, 0);
                    LLVMValueRef args2[2] = { cp, upc };
                    LLVMValueRef r = zan_call2(g->builder, case_ty,
                        get_char_case_fn(g), args2, 2, "ch.map");
                    if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(arg)) > 32)
                        r = LLVMBuildSExt(g->builder, r, LLVMTypeOf(arg), "ch.map.w");
                    emit_release_owned_call_temp(g, expr->call.args.items[0],
                                                 arg, locals);
                    return r;
                }
                /* 未命中的 char.* 成员落回通用路径（会得到相应的未解析诊断） */
            }
        }

        /* Convert.ToDouble(x) */
        if (!zan_type_defines(g, "Convert", "ToDouble") &&
            is_call_to(expr, "Convert", "ToDouble") && expr->call.args.count == 1) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            zan_type_t *at = infer_expr_type(g, expr->call.args.items[0], locals);
            if (!type_is_textual_or_numeric(at)) {
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3056,
                              "Convert.ToDouble: cannot convert '%.*s' to a "
                              "number -- pass a string or a numeric value",
                              at && at->name.len ? (int)at->name.len : 6,
                              at && at->name.len ? at->name.str : "<value>");
                return LLVMConstReal(dbl, 0.0);
            }
            LLVMTypeKind ak = LLVMGetTypeKind(LLVMTypeOf(arg));
            if (ak == LLVMPointerTypeKind) {
                LLVMTypeRef strtod_ty = LLVMFunctionType(dbl,
                    (LLVMTypeRef[]){ i8ptr, LLVMPointerType(i8ptr, 0) }, 2, 0);
                LLVMValueRef f = LLVMGetNamedFunction(g->mod, "zan_rt_dbl_parse");
                if (!f) f = LLVMAddFunction(g->mod, "zan_rt_dbl_parse", strtod_ty);
                /* A512：失败/残留 → 软诊断 + 安全值 0（曾静默返 0 继续执行） */
                LLVMValueRef endp = emit_entry_alloca(g, i8ptr, "todbl.endp");
                LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), endp);
                LLVMValueRef s_safe = emit_parse_str_guard(g, arg, expr->loc);
                LLVMValueRef r = zan_call2(g->builder, strtod_ty, f,
                    (LLVMValueRef[]){ s_safe, endp }, 2, "todbl");
                LLVMValueRef ep = LLVMBuildLoad2(g->builder, i8ptr, endp, "todbl.ep");
                LLVMValueRef bad = emit_parse_bad_pred(g, s_safe, ep);
                emit_runtime_check(g, bad, expr->loc,
                    "Convert.ToDouble: input is not a valid number");
                r = LLVMBuildSelect(g->builder, bad,
                    LLVMConstReal(dbl, 0.0), r, "todbl.safe");
                emit_release_owned_call_temp(g, expr->call.args.items[0], arg, locals);
                return r;
            }
            if (ak == LLVMIntegerTypeKind)
                return LLVMBuildSIToFP(g->builder, arg, dbl, "todbl");
            if (ak == LLVMFloatTypeKind)
                return LLVMBuildFPExt(g->builder, arg, dbl, "todbl");
            return arg;
        }

        /* int/long/Int32/Int64 */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->call.callee->member.object->ident.name)) {
            zan_istr_t rn = expr->call.callee->member.object->ident.name;
            zan_istr_t pm = expr->call.callee->member.name;
            int int_recv = (rn.len == 3 && memcmp(rn.str, "int", 3) == 0) ||
                           (rn.len == 4 && memcmp(rn.str, "long", 4) == 0) ||
                           (rn.len == 5 && memcmp(rn.str, "Int32", 5) == 0) ||
                           (rn.len == 5 && memcmp(rn.str, "Int64", 5) == 0);
            int dbl_recv = (rn.len == 6 && memcmp(rn.str, "double", 6) == 0) ||
                           (rn.len == 5 && memcmp(rn.str, "float", 5) == 0) ||
                           (rn.len == 6 && memcmp(rn.str, "Double", 6) == 0) ||
                           (rn.len == 6 && memcmp(rn.str, "Single", 6) == 0);
            if ((int_recv || dbl_recv) &&
                pm.len == 5 && memcmp(pm.str, "Parse", 5) == 0 &&
                expr->call.args.count == 1) {                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, expr->call.args.items[0], locals);
                zan_type_t *at = infer_expr_type(g, expr->call.args.items[0], locals);
                if (!type_is_text_arg(at)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3057,
                                  "%.*s.Parse requires a string argument",
                                  (int)rn.len, rn.str);
                    return int_recv ? LLVMConstInt(i64, 0, 0)
                                    : LLVMConstReal(dbl, 0.0);
                }
                LLVMValueRef r;
                if (int_recv) {
                    LLVMTypeRef strtoll_ty = LLVMFunctionType(i64,
                        (LLVMTypeRef[]){ i8ptr, i8pp, i32t }, 3, 0);
                    LLVMValueRef f = get_libc_fn(g, "strtoll", strtoll_ty);
                    LLVMValueRef endp = emit_entry_alloca(g, i8ptr, "parse.endp");
                    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), endp);
                    LLVMValueRef s_safe = emit_parse_str_guard(g, s, expr->loc);
                    r = zan_call2(g->builder, strtoll_ty, f,
                        (LLVMValueRef[]){ s_safe, endp,
                                          LLVMConstInt(i32t, 10, 0) }, 3, "parse");
                    LLVMValueRef ep = LLVMBuildLoad2(g->builder, i8ptr, endp, "parse.ep");
                    LLVMValueRef bad = emit_parse_bad_pred(g, s_safe, ep);
                    /* C# int.Parse 超出 int 范围抛 OverflowException：
                     * strtoll 会静默吃下 64 位值，这里同样判坏。 */
                    LLVMValueRef over = LLVMBuildOr(g->builder,
                        zan_icmp(g->builder, LLVMIntSLT, r,
                            LLVMConstInt(i64, (int64_t)INT32_MIN, 0), "parse.lo"),
                        zan_icmp(g->builder, LLVMIntSGT, r,
                            LLVMConstInt(i64, INT32_MAX, 0), "parse.hi"),
                        "parse.over");
                    bad = LLVMBuildOr(g->builder, bad, over, "parse.bad2");
                    emit_runtime_check(g, bad, expr->loc,
                        "int.Parse: input is not a valid integer (or out of range)");
                    r = LLVMBuildSelect(g->builder, bad,
                        LLVMConstInt(i64, 0, 0), r, "parse.safe");
                } else {
                    LLVMTypeRef strtod_ty = LLVMFunctionType(dbl,
                        (LLVMTypeRef[]){ i8ptr, i8pp }, 2, 0);
                    LLVMValueRef f = LLVMGetNamedFunction(g->mod, "zan_rt_dbl_parse");
                    if (!f) f = LLVMAddFunction(g->mod, "zan_rt_dbl_parse", strtod_ty);
                    LLVMValueRef endp = emit_entry_alloca(g, i8ptr, "parse.endp");
                    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), endp);
                    LLVMValueRef s_safe = emit_parse_str_guard(g, s, expr->loc);
                    r = zan_call2(g->builder, strtod_ty, f,
                        (LLVMValueRef[]){ s_safe, endp }, 2, "parse");
                    LLVMValueRef ep = LLVMBuildLoad2(g->builder, i8ptr, endp, "parse.ep");
                    LLVMValueRef bad = emit_parse_bad_pred(g, s_safe, ep);
                    /* C# double.Parse 溢出抛 OverflowException：strtod 返
                     * ±HUGE_VAL（inf），判坏返 0。 */
                    LLVMTypeRef fabs_ty = LLVMFunctionType(dbl,
                        (LLVMTypeRef[]){ dbl }, 1, 0);
                    LLVMValueRef av = zan_call2(g->builder, fabs_ty,
                        get_libc_fn(g, "fabs", fabs_ty), &r, 1, "parse.fabs");
                    LLVMValueRef isinf = LLVMBuildFCmp(g->builder,
                        LLVMRealOEQ, av,
                        LLVMConstReal(dbl, 1.7976931348623157e308 * 2.0), "parse.inf");
                    bad = LLVMBuildOr(g->builder, bad, isinf, "parse.bad2");
                    emit_runtime_check(g, bad, expr->loc,
                        "double.Parse: input is not a valid number (or out of range)");
                    r = LLVMBuildSelect(g->builder, bad,
                        LLVMConstReal(dbl, 0.0), r, "parse.safe");
                }
                emit_release_owned_call_temp(g, expr->call.args.items[0], s, locals);
                return r;
            }
            if ((int_recv || dbl_recv) &&
                pm.len == 8 && memcmp(pm.str, "TryParse", 8) == 0 &&
                expr->call.args.count == 2) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, expr->call.args.items[0], locals);
                zan_type_t *at = infer_expr_type(g, expr->call.args.items[0], locals);
                if (!type_is_text_arg(at)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3058,
                                  "%.*s.TryParse requires a string argument",
                                  (int)rn.len, rn.str);
                    return LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
                }
                LLVMValueRef outp = emit_expr(g, expr->call.args.items[1], locals);
                LLVMValueRef endp = emit_entry_alloca(g, i8ptr, "endp");
                LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), endp);
                LLVMValueRef v;
                if (int_recv) {
                    LLVMTypeRef strtoll_ty = LLVMFunctionType(i64,
                        (LLVMTypeRef[]){ i8ptr, i8pp, i32t }, 3, 0);
                    LLVMValueRef f = get_libc_fn(g, "strtoll", strtoll_ty);
                    v = zan_call2(g->builder, strtoll_ty, f,
                        (LLVMValueRef[]){ s, endp, LLVMConstInt(i32t, 10, 0) }, 3, "tp");
                } else {
                    LLVMTypeRef strtod_ty = LLVMFunctionType(dbl,
                        (LLVMTypeRef[]){ i8ptr, i8pp }, 2, 0);
                    LLVMValueRef f = LLVMGetNamedFunction(g->mod, "zan_rt_dbl_parse");
                    if (!f) f = LLVMAddFunction(g->mod, "zan_rt_dbl_parse", strtod_ty);
                    v = zan_call2(g->builder, strtod_ty, f,
                        (LLVMValueRef[]){ s, endp }, 2, "tp");
                }
                LLVMValueRef e = LLVMBuildLoad2(g->builder, i8ptr, endp, "e");
                LLVMValueRef moved = zan_icmp(g->builder, LLVMIntNE, e, s, "moved");
                LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8, e, "ch");
                LLVMValueRef at_end = zan_icmp(g->builder, LLVMIntEQ, ch,
                    LLVMConstInt(i8, 0, 0), "atend");
                LLVMValueRef ok = zan_and(g->builder, moved, at_end, "ok");
                LLVMValueRef stored = LLVMBuildSelect(g->builder, ok, v,
                    int_recv ? LLVMConstInt(i64, 0, 0) : (LLVMValueRef)LLVMConstReal(dbl, 0.0), "tpv");
                if (LLVMGetTypeKind(LLVMTypeOf(outp)) == LLVMPointerTypeKind)
                    LLVMBuildStore(g->builder, stored, outp);
                emit_release_owned_call_temp(g, expr->call.args.items[0], s, locals);
                return ok;
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->call.callee->member.object->ident.name)) {
            zan_istr_t rn = expr->call.callee->member.object->ident.name;
            int str_recv = (rn.len == 6 && memcmp(rn.str, "string", 6) == 0) ||
                           (rn.len == 6 && memcmp(rn.str, "String", 6) == 0);
            zan_istr_t pm = expr->call.callee->member.name;
            if (str_recv && pm.len == 13 &&
                memcmp(pm.str, "IsNullOrEmpty", 13) == 0 &&
                expr->call.args.count == 1) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMValueRef s = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, s,
                    LLVMConstNull(i8ptr), "isnull");
                LLVMBasicBlockRef chk_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sne.chk");
                LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sne.done");
                LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
                LLVMBuildCondBr(g->builder, isnull, done_bb, chk_bb);
                LLVMPositionBuilderAtEnd(g->builder, chk_bb);
                LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8, s, "ch");
                LLVMValueRef isempty = zan_icmp(g->builder, LLVMIntEQ, ch,
                    LLVMConstInt(i8, 0, 0), "isempty");
                LLVMBasicBlockRef chk_end = LLVMGetInsertBlock(g->builder);
                LLVMBuildBr(g->builder, done_bb);
                LLVMPositionBuilderAtEnd(g->builder, done_bb);
                LLVMValueRef phi = LLVMBuildPhi(g->builder, LLVMInt1TypeInContext(g->ctx), "sne");
                LLVMValueRef inv[2] = { LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0), isempty };
                LLVMBasicBlockRef inb[2] = { cur_bb, chk_end };
                LLVMAddIncoming(phi, inv, inb, 2);
                emit_release_owned_call_temp(g, expr->call.args.items[0], s, locals);
                return phi;
            }
            if (str_recv && pm.len == 4 && memcmp(pm.str, "Join", 4) == 0 &&
                expr->call.args.count == 2) {
                LLVMValueRef sep = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef lst = emit_expr(g, expr->call.args.items[1], locals);
                LLVMValueRef hf = get_str_join_fn(g);
                LLVMValueRef res = zan_call2(g->builder,
                    LLVMGlobalGetValueType(hf), hf,
                    (LLVMValueRef[]){ sep, lst }, 2, "join");
                emit_release_owned_call_temp(g, expr->call.args.items[0], sep, locals);
                emit_release_owned_call_temp(g, expr->call.args.items[1], lst, locals);
                return res;
            }
            if (str_recv && pm.len == 6 && memcmp(pm.str, "Format", 6) == 0 &&
                expr->call.args.count >= 1 &&
                expr->call.args.items[0]->kind == AST_STRING_LITERAL) {
                zan_istr_t f = expr->call.args.items[0]->str_val;
                zan_type_t *str_ty = g->binder->type_string;
                LLVMValueRef res = NULL;
                int seg_start = 0;
                for (int i = 0; i < f.len; i++) {
                    if (f.str[i] == '{' && i + 2 < f.len + 1) {
                        int j = i + 1, idx = 0, has = 0, oflow = 0;
                        while (j < f.len && f.str[j] >= '0' && f.str[j] <= '9') {
                            if (!oflow) {
                                if (idx > 214748363 ||
                                    idx >= expr->call.args.count) oflow = 1;
                                else idx = idx * 10 + (f.str[j] - '0');
                            }
                            j++; has = 1;
                        }
                        if (has && !oflow && j < f.len && f.str[j] == '}' &&
                            idx + 1 < expr->call.args.count) {
                            if (i > seg_start) {
                                zan_istr_t seg = { f.str + seg_start, i - seg_start };
                                LLVMValueRef sl = emit_string_literal_rc(g, seg);
                                if (res) {
                                    LLVMValueRef nr = emit_str_concat(g, res, sl);
                                    emit_rc_release_for_type(g, str_ty, res);
                                    emit_rc_release_for_type(g, str_ty, sl);
                                    res = nr;
                                } else {
                                    res = sl;
                                }
                            }
                            zan_ast_node_t *arg_ast = expr->call.args.items[idx + 1];
                            LLVMValueRef av = emit_expr(g, arg_ast, locals);
                            LLVMValueRef as = emit_to_cstr_of(g, av, arg_ast, locals);
                            int as_owned =
                                LLVMGetTypeKind(LLVMTypeOf(av)) != LLVMPointerTypeKind ||
                                expr_yields_owned_rc_value(g, arg_ast, locals);
                            if (res) {
                                LLVMValueRef nr = emit_str_concat(g, res, as);
                                emit_rc_release_for_type(g, str_ty, res);
                                if (as_owned)
                                    emit_rc_release_for_type(g, str_ty, as);
                                res = nr;
                            } else if (as_owned) {
                                res = as;
                            } else {
                                emit_rc_retain_for_type(g, str_ty, as);
                                res = as;
                            }
                            i = j;
                            seg_start = j + 1;
                        }
                    }
                }
                if (seg_start < f.len || !res) {
                    zan_istr_t seg = { f.str + seg_start, f.len - seg_start };
                    LLVMValueRef sl = emit_string_literal_rc(g, seg);
                    if (res) {
                        LLVMValueRef nr = emit_str_concat(g, res, sl);
                        emit_rc_release_for_type(g, str_ty, res);
                        emit_rc_release_for_type(g, str_ty, sl);
                        res = nr;
                    } else {
                        res = sl;
                    }
                }
                return res;
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;

            if (callee->member.object->kind == AST_IDENTIFIER) {
                local_var_t *str_local = local_find(locals, callee->member.object->ident.name);
                if (str_local && LLVMGetTypeKind(LLVMTypeOf(LLVMBuildLoad2(g->builder,
                    LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0), str_local->alloca, "chk"))) == LLVMPointerTypeKind) {
                }
            }
            
            if (callee->member.object->kind == AST_IDENTIFIER) {
                zan_istr_t obj_name = callee->member.object->ident.name;
                if (obj_name.len == 7 && memcmp(obj_name.str, "Convert", 7) == 0) {
                    if (((method_name.len == 7 && memcmp(method_name.str, "ToInt32", 7) == 0) ||
                         (method_name.len == 7 && memcmp(method_name.str, "ToInt64", 7) == 0) ||
                         (method_name.len == 5 && memcmp(method_name.str, "ToInt", 5) == 0)) &&
                        !zan_type_defines(g, "Convert", "ToInt32") &&
                        !zan_type_defines(g, "Convert", "ToInt64") &&
                        !zan_type_defines(g, "Convert", "ToInt") &&
                        expr->call.args.count == 1) {
                        LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                        zan_type_t *at = infer_expr_type(g, expr->call.args.items[0], locals);
                        if (!type_is_textual_or_numeric(at)) {
                            zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3059,
                                          "Convert.%.*s: cannot convert '%.*s' "
                                          "to a number -- pass a string or a "
                                          "numeric value",
                                          (int)method_name.len, method_name.str,
                                          at && at->name.len ? (int)at->name.len : 6,
                                          at && at->name.len ? at->name.str : "<value>");
                            return LLVMConstInt(i64t, 0, 0);
                        }
                        LLVMTypeKind ak = LLVMGetTypeKind(LLVMTypeOf(arg));
                        LLVMValueRef parsed;
                        if (ak == LLVMPointerTypeKind) {
                            /* 字符串转 64 位有符号整数 (strtoll 规避 atoi 的 32 位截断)；
                             * A512：失败/残留/溢 int 范围 → 软诊断 + 安全值 0 */
                            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                            LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
                            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                            LLVMTypeRef strtoll_ty = LLVMFunctionType(i64t,
                                (LLVMTypeRef[]){ i8ptr, i8pp, i32t }, 3, 0);
                            LLVMValueRef f = get_libc_fn(g, "strtoll", strtoll_ty);
                            LLVMValueRef endp = emit_entry_alloca(g, i8ptr, "toint.endp");
                            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), endp);
                            LLVMValueRef s_safe = emit_parse_str_guard(g, arg, expr->loc);
                            /* 审计 P1-12 余项：strtoll 对超出 long 范围的
                             * 输入饱和返回 INT64_MAX/MIN 并置 errno=ERANGE
                             * ——只看返回值分不清「输入恰好是极值」与「饱和」
                             * （ToInt64("99999999999999999999") 曾静默钳成
                             * INT64_MAX，C# 语义是 OverflowException）。
                             * 先清零 errno，调用后读回比对 34（ERANGE）。 */
                            LLVMTypeRef errno_ty = LLVMFunctionType(
                                LLVMPointerType(i32t, 0), NULL, 0, 0);
                            /* 按**目标**平台选 TLS errno 取址函数，不是按宿主
                             * #if：Windows 宿主交叉编 macOS 时 IR 名 `_errno`
                             * 会被 Mach-O 再加前导下划线成 `__errno`，而
                             * libSystem 导出的 C 符号是 `__error`——链接期才
                             * 炸，且炸在离病灶最远的 Convert.ToInt 调用点上。
                             * 分支次序对齐 Socket.zan 的 DllImport 守卫
                             * （ANDROID 也定义 LINUX，安卓分支必须在前）。 */
                            const char *errno_sym = "__errno_location";
                            if (strstr(g->target_triple, "android") ||
                                strstr(g->target_triple, "ohos")) {
                                errno_sym = "__errno";
                            } else if (g->target_is_macos || g->target_is_ios) {
                                errno_sym = "__error";
                            } else if (g->target_is_windows) {
                                errno_sym = "_errno";
                            }
                            LLVMValueRef errno_fn = get_libc_fn(g, errno_sym, errno_ty);
                            LLVMValueRef errnop = zan_call2(g->builder, errno_ty,
                                errno_fn, NULL, 0, "toint.errnop");
                            LLVMBuildStore(g->builder, LLVMConstInt(i32t, 0, 0),
                                errnop);
                            parsed = zan_call2(g->builder, strtoll_ty, f,
                                (LLVMValueRef[]){ s_safe, endp,
                                                  LLVMConstInt(i32t, 10, 0) }, 3, "toint");
                            LLVMValueRef ep = LLVMBuildLoad2(g->builder, i8ptr,
                                endp, "toint.ep");
                            LLVMValueRef bad = emit_parse_bad_pred(g, s_safe, ep);
                            if (method_name.str[6] != '4') {
                                /* ToInt32/ToInt：C# 超出 int 范围抛 OverflowException */
                                bad = LLVMBuildOr(g->builder, bad, LLVMBuildOr(
                                    g->builder,
                                    zan_icmp(g->builder, LLVMIntSLT, parsed,
                                        LLVMConstInt(i64t, (int64_t)INT32_MIN, 0), "toint.lo"),
                                    zan_icmp(g->builder, LLVMIntSGT, parsed,
                                        LLVMConstInt(i64t, INT32_MAX, 0), "toint.hi"),
                                    "toint.over"), "toint.bad2");
                            }
                            /* ERANGE 兜底：两档共用（i32 越界必然先饱和到
                             * i64 边缘再置 ERANGE，与上面的范围谓词等价；
                             * i64 档只有它能抓到）。 */
                            LLVMValueRef er = LLVMBuildLoad2(g->builder, i32t,
                                errnop, "toint.errno");
                            bad = LLVMBuildOr(g->builder, bad,
                                zan_icmp(g->builder, LLVMIntEQ, er,
                                    LLVMConstInt(i32t, 34, 0), "toint.erange"),
                                "toint.bad3");
                            emit_runtime_check(g, bad, expr->loc,
                                "Convert.ToInt*: input is not a valid integer (or out of range)");
                            parsed = LLVMBuildSelect(g->builder, bad,
                                LLVMConstInt(i64t, 0, 0), parsed, "toint.safe");
                        } else if (ak == LLVMDoubleTypeKind || ak == LLVMFloatTypeKind) {
                            /* C# Convert.ToInt32(double) 是舍入到最近（银行家），
                             * 不是截断——先 rint 再转整（曾 ToInt32(3.9)=3，
                             * C# 是 4；TASKS.md A513）。 */
                            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                            LLVMTypeRef d1_ty = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
                            LLVMValueRef as_dbl = (ak == LLVMDoubleTypeKind)
                                ? arg : LLVMBuildFPExt(g->builder, arg, dbl, "toint.ext");
                            LLVMValueRef r = zan_call2(g->builder, d1_ty,
                                get_libc_fn(g, "rint", d1_ty), &as_dbl, 1, "toint.rint");
                            parsed = LLVMBuildFPToSI(g->builder, r, i64t, "toint");
                        } else if (ak == LLVMIntegerTypeKind) {
                            if (LLVMTypeOf(arg) == i64t)
                                parsed = arg;
                            else if (LLVMGetIntTypeWidth(LLVMTypeOf(arg)) == 1)
                                /* bool 按 0/1 零扩展——C# Convert.ToInt32(true)=1；
                                 * 符号扩展曾得 -1（TASKS.md A498）。 */
                                parsed = LLVMBuildZExt(g->builder, arg, i64t, "toint.zext");
                            else
                                parsed = LLVMBuildSExt(g->builder, arg, i64t, "toint");
                            /* 审计 P1-12：i64 实参超 int 范围曾静默截断回绕
                             * （ToInt32(2147483648L) = -2147483648）——C# 语义是
                             * OverflowException。与字符串路径同判：软诊断 + 0
                             * （--strict-runtime 下 exit 70）。ToInt64 不需要
                             * （全宽）。窄整型 sext 后必在范围内，不用查。 */
                            if (LLVMTypeOf(arg) == i64t && method_name.str[6] != '4') {
                                LLVMValueRef over = LLVMBuildOr(g->builder,
                                    zan_icmp(g->builder, LLVMIntSLT, parsed,
                                        LLVMConstInt(i64t, (int64_t)INT32_MIN, 0), "tointi.lo"),
                                    zan_icmp(g->builder, LLVMIntSGT, parsed,
                                        LLVMConstInt(i64t, INT32_MAX, 0), "tointi.hi"),
                                    "tointi.over");
                                emit_runtime_check(g, over, expr->loc,
                                    "Convert.ToInt*: value is outside the range of int");
                                parsed = LLVMBuildSelect(g->builder, over,
                                    LLVMConstInt(i64t, 0, 0), parsed, "tointi.safe");
                            }
                        } else {
                            parsed = LLVMConstInt(i64t, 0, 0);
                        }
                        emit_release_owned_call_temp(
                            g, expr->call.args.items[0], arg, locals);
                        return parsed;
                    }
                    if (method_name.len == 8 && memcmp(method_name.str, "ToString", 8) == 0 &&
                        (!zan_type_defines(g, "Convert", "ToString") ||
                         !zan_convert_tostring_exact_decl(g, expr->call.args.items[0],
                                                          locals)) &&
                        expr->call.args.count == 1) {
                        LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        zan_type_t *at = infer_expr_type(g, expr->call.args.items[0], locals);
                        if (!type_is_textual_or_numeric(at)) {
                            /* A463: the type is named the way the user wrote
                               it -- the shape comes along (`int[]`, `Box`,
                               `D`). One renderer for every diagnostic, so
                               this one cannot drift from the checker's. */
                            char tname[256];
                            zan_type_display(at, tname, sizeof(tname));
                            if (!tname[0])
                                snprintf(tname, sizeof(tname), "<value>");
                            zan_diag_emit_code(g->diag, DIAG_ERROR,
                                expr->call.args.items[0]->loc, 3060,
                                "Convert.ToString: cannot convert '%s' to a "
                                "string -- call .ToString() on the value instead",
                                tname);
                            return LLVMConstNull(i8ptr);
                        }
                        if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMPointerTypeKind) {
                            /* A444 安全栏：静态类型是 object 时负载可能是类
                             * 实例——运行期判别，字符串才 strlen+memcpy，
                             * 非字符串走软运行时错（可 catch、strict 模式
                             * exit 70）+ 空串兜底。原先拿 strlen 量对象
                             * 指针 → 越界读 + 内存信息泄露。判别器在
                             * rt_sync.c：字符串 [-8] 高 32 位恒为 ZANS
                             * 标签，类实例的 ARC 站点指针撞不上；null
                             * 一并判为非字符串（顺带修掉 null 的 strlen UB）。 */
                            LLVMValueRef safe_arg = arg;
                            if (at && at->kind == TYPE_OBJECT) {
                                /* 判别器在 rt_sync.c：把 sync 运行时拉进链接集 */
                                g->uses_sync_runtime = true;
                                LLVMTypeRef isstr_ty = LLVMFunctionType(
                                    LLVMInt32TypeInContext(g->ctx),
                                    (LLVMTypeRef[]){ i8ptr }, 1, 0);
                                LLVMValueRef isstr_fn =
                                    LLVMGetNamedFunction(g->mod, "zan_obj_slot_is_string");
                                if (!isstr_fn)
                                    isstr_fn = LLVMAddFunction(g->mod,
                                        "zan_obj_slot_is_string", isstr_ty);
                                LLVMValueRef is_str = zan_call2(g->builder,
                                    isstr_ty, isstr_fn, &arg, 1, "is.str");
                                LLVMValueRef bad = zan_icmp(g->builder,
                                    LLVMIntEQ, is_str,
                                    LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0),
                                    "str.bad");
                                emit_runtime_check(g, bad,
                                    expr->call.args.items[0]->loc,
                                    "Convert.ToString: object holds a "
                                    "non-string value -- call .ToString() "
                                    "on the concrete value instead");
                                LLVMValueRef empty =
                                    zan_irgen_intern_string(g, "");
                                LLVMValueRef hit = zan_icmp(g->builder,
                                    LLVMIntNE, is_str,
                                    LLVMConstInt(LLVMInt32TypeInContext(g->ctx),
                                        0, 0), "str.hit");
                                safe_arg = LLVMBuildSelect(g->builder, hit,
                                    arg, empty, "str.safe");
                            }
                            LLVMTypeRef strlen_ty = LLVMFunctionType(i64,
                                (LLVMTypeRef[]){ i8ptr }, 1, 0);
                            LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr,
                                (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
                            LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);
                            LLVMValueRef n = zan_call2(g->builder, strlen_ty,
                                g->fn_strlen, &safe_arg, 1, "n");
                            LLVMValueRef bufsz = zan_add(g->builder, n,
                                LLVMConstInt(i64, 1, 0), "bsz");
                            LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
                            LLVMValueRef mcargs[] = { buf, safe_arg, bufsz };
                            zan_call2(g->builder, memcpy_ty, memcpy_fn, mcargs, 3, "");
                            emit_release_owned_call_temp(
                                g, expr->call.args.items[0], arg, locals);
                            return buf;
                        }
                        if (expr_is_char(g, expr->call.args.items[0], locals))
                            return emit_char_to_cstr(g, arg);
                        if (llvm_is_nullable(LLVMTypeOf(arg))) {
                            zan_type_t *st = infer_expr_type(
                                g, expr->call.args.items[0], locals);
                            bool uns = st && st->element_type &&
                                       (st->element_type->kind == TYPE_UINT ||
                                        st->element_type->kind == TYPE_ULONG);
                            return emit_to_cstr_u(g, arg, uns ? 1 : 0);
                        }
                        /* bool 渲染成 "true"/"false"（不是 itoa 的 "1"）——
                           与 b.ToString() / Console.WriteLine / 插值同口径。
                           判据是**语义类型**不是 LLVM 宽度：泛型方法返回值
                           （List<T>.Contains 这类）被提升成 i64，按宽度判会
                           掉进 itoa 渲染成 "1"。 */
                        {
                            LLVMValueRef bc = emit_bool_cond(g, arg,
                                expr->call.args.items[0], locals);
                            if (bc) return emit_bool_to_cstr(g, bc);
                        }
                        /* 双精度浮点数最短往返格式化缓冲区 (40 字节容量) */
                        LLVMValueRef buf_size = LLVMConstInt(i64, 40, 0);
                        LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
                        LLVMTypeKind atk = LLVMGetTypeKind(LLVMTypeOf(arg));
                        if (atk == LLVMDoubleTypeKind || atk == LLVMFloatTypeKind) {
                            emit_dbl_str(g, buf, buf_size, arg);
                            return buf;
                        }
                        bool is_ul = expr_is_ulong(
                            g, expr->call.args.items[0], locals);
                        emit_itoa_into(g, buf,
                            emit_widen_i64_for_print(g, arg), is_ul ? 1 : 0);
                        return buf;
                    }
                }
            }
        }

        /* str */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 9 && memcmp(sm.str, "Substring", 9) == 0 &&
                (expr->call.args.count == 1 || expr->call.args.count == 2) &&
                is_string_expr(g, sc->member.object, locals)) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef start = coerce_int_to(g,
                    emit_expr(g, expr->call.args.items[0], locals), i64);
                bool bounded = expr_has_reliable_string_bounds(sc->member.object,
                                                              locals) != 0;
                LLVMValueRef total = (bounded || expr->call.args.count == 1)
                    ? emit_string_length(g, s, expr->loc) : NULL;
                LLVMValueRef slen;
                if (expr->call.args.count == 2) {
                    slen = coerce_int_to(g,
                        emit_expr(g, expr->call.args.items[1], locals), i64);
                    if (total) {
                        emit_span_window_check(g, start, slen, total, expr->loc,
                                               "substring");
                        emit_span_safe_window(g, &start, &slen, total, expr->loc,
                                              "substring");
                    }
                } else {
                    emit_index_range_check(g, start, total, true, expr->loc,
                                           "substring");
                    start = emit_index_safe_check(g, start, total, true, expr->loc,
                                                  "substring");
                    slen = zan_sub(g->builder, total, start, "subl");
                }
                slen = LLVMBuildSelect(g->builder,
                    zan_icmp(g->builder, LLVMIntSLT, slen,
                             LLVMConstInt(i64, 0, 0), "subl.neg"),
                    LLVMConstInt(i64, 0, 0), slen, "subl.safe");
                {
                    LLVMValueRef snull = zan_icmp(g->builder, LLVMIntEQ, s,
                        LLVMConstNull(LLVMTypeOf(s)), "subnull");
                    s = emit_soft_base_select(g, s, snull, expr->loc);
                }

                LLVMContextRef c = g->ctx;
                LLVMBasicBlockRef bb_slice = LLVMAppendBasicBlockInContext(c, g->current_fn, "sub.slice");
                LLVMBasicBlockRef bb_full = total ? LLVMAppendBasicBlockInContext(c, g->current_fn, "sub.full") : NULL;
                LLVMBasicBlockRef bb_empty = LLVMAppendBasicBlockInContext(c, g->current_fn, "sub.empty");
                LLVMBasicBlockRef bb_join = LLVMAppendBasicBlockInContext(c, g->current_fn, "sub.join");

                LLVMValueRef is_empty = zan_icmp(g->builder, LLVMIntEQ, slen, LLVMConstInt(i64, 0, 0), "sub.isz");
                if (total) {
                    LLVMValueRef is_st0 = zan_icmp(g->builder, LLVMIntEQ, start, LLVMConstInt(i64, 0, 0), "sub.st0");
                    LLVMValueRef is_len_all = zan_icmp(g->builder, LLVMIntEQ, slen, total, "sub.lall");
                    LLVMValueRef is_full = zan_and(g->builder, is_st0, is_len_all, "sub.isfull");
                    LLVMBasicBlockRef bb_chk_full = LLVMAppendBasicBlockInContext(c, g->current_fn, "sub.chkfull");
                    LLVMBuildCondBr(g->builder, is_empty, bb_empty, bb_chk_full);

                    LLVMPositionBuilderAtEnd(g->builder, bb_chk_full);
                    LLVMBuildCondBr(g->builder, is_full, bb_full, bb_slice);

                    LLVMPositionBuilderAtEnd(g->builder, bb_full);
                    LLVMTypeRef str_rel_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    zan_call2(g->builder, str_rel_type, g->rt_str_retain, &s, 1, "");
                    LLVMBuildBr(g->builder, bb_join);
                } else {
                    LLVMBuildCondBr(g->builder, is_empty, bb_empty, bb_slice);
                }

                LLVMPositionBuilderAtEnd(g->builder, bb_empty);
                LLVMValueRef empty_str = zan_irgen_intern_string(g, "");
                LLVMBuildBr(g->builder, bb_join);

                LLVMPositionBuilderAtEnd(g->builder, bb_slice);
                LLVMValueRef bufsz = zan_add(g->builder, slen, LLVMConstInt(i64, 1, 0), "bsz");
                LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
                LLVMValueRef srcp = LLVMBuildGEP2(g->builder, i8, s, &start, 1, "srcp");
                LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
                if (!memcpy_fn) {
                    memcpy_fn = LLVMAddFunction(g->mod, "memcpy",
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0));
                }
                LLVMValueRef mcargs[] = { buf, srcp, slen };
                zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0),
                    memcpy_fn, mcargs, 3, "");
                LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, buf, &slen, 1, "endp");
                LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
                /* 无 \0 范围内的切片：slen 为切片结果字符串长度 */
                emit_string_len_set(g, buf, slen);
                LLVMBuildBr(g->builder, bb_join);

                LLVMPositionBuilderAtEnd(g->builder, bb_join);
                LLVMValueRef phi = LLVMBuildPhi(g->builder, i8ptr, "sub.res");
                LLVMValueRef phi_in_vals[3];
                LLVMBasicBlockRef phi_in_bbs[3];
                int np = 0;
                phi_in_vals[np] = empty_str; phi_in_bbs[np] = bb_empty; np++;
                if (total) {
                    phi_in_vals[np] = s; phi_in_bbs[np] = bb_full; np++;
                }
                phi_in_vals[np] = buf; phi_in_bbs[np] = bb_slice; np++;
                LLVMAddIncoming(phi, phi_in_vals, phi_in_bbs, (unsigned)np);

                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                return phi;
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 8 && memcmp(sm.str, "Contains", 8) == 0 &&
                expr->call.args.count == 1 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals)) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                int sub_owned;
                LLVMValueRef sub = emit_string_like_arg(g, expr->call.args.items[0],
                                                        locals, &sub_owned);
                LLVMTypeRef strstr_type = LLVMFunctionType(i8ptr,
                    (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                LLVMValueRef strstr_fn = LLVMGetNamedFunction(g->mod, "strstr");
                if (!strstr_fn)
                    strstr_fn = LLVMAddFunction(g->mod, "strstr", strstr_type);
                LLVMValueRef ss_args[] = { s, sub };
                LLVMValueRef hit = zan_call2(g->builder, strstr_type,
                    strstr_fn, ss_args, 2, "ss");
                LLVMValueRef res = zan_icmp(g->builder, LLVMIntNE, hit,
                    LLVMConstPointerNull(i8ptr), "ctn");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], sub,
                                        sub_owned, locals);
                return res;
            }
        }

        /* str */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            int is_idx = (sm.len == 7 && memcmp(sm.str, "IndexOf", 7) == 0);
            int is_lidx = (sm.len == 11 && memcmp(sm.str, "LastIndexOf", 11) == 0);
            if (is_idx && (expr->call.args.count == 1 || expr->call.args.count == 2) &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals)) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef search = s;
                LLVMValueRef start = LLVMConstInt(i64, 0, 0);
                LLVMValueRef valid_start = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0);
                if (expr->call.args.count == 2) {
                    start = emit_expr(g, expr->call.args.items[1], locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(start)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(start)) < 64)
                        start = LLVMBuildSExt(g->builder, start, i64, "idx.start");
                    LLVMValueRef slen = emit_string_length(g, s, expr->loc);
                    LLVMValueRef nonneg = zan_icmp(g->builder, LLVMIntSGE, start,
                        LLVMConstInt(i64, 0, 0), "idx.nonneg");
                    LLVMValueRef within = zan_icmp(g->builder, LLVMIntSLE, start,
                        slen, "idx.within");
                    valid_start = zan_and(g->builder, nonneg, within, "idx.valid");
                    search = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                        s, &start, 1, "idx.search");
                }
                int sub_owned;
                LLVMValueRef sub = emit_string_like_arg(g, expr->call.args.items[0],
                                                        locals, &sub_owned);
                LLVMTypeRef strstr_type = LLVMFunctionType(i8ptr,
                    (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                LLVMValueRef strstr_fn = get_libc_fn(g, "strstr", strstr_type);
                LLVMValueRef ss_args[] = { search, sub };
                LLVMValueRef hit = zan_call2(g->builder, strstr_type,
                    strstr_fn, ss_args, 2, "ss");
                LLVMValueRef diff = zan_sub(g->builder,
                    LLVMBuildPtrToInt(g->builder, hit, i64, "hi"),
                    LLVMBuildPtrToInt(g->builder, s, i64, "si"), "diff");
                LLVMValueRef missed = zan_icmp(g->builder, LLVMIntEQ, hit,
                    LLVMConstPointerNull(i8ptr), "missed");
                LLVMValueRef invalid_start = zan_icmp(g->builder, LLVMIntEQ,
                    valid_start, LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0),
                    "idx.invalid");
                LLVMValueRef failed = zan_or(g->builder, missed,
                    invalid_start, "idx.failed");
                LLVMValueRef res = LLVMBuildSelect(g->builder, failed,
                    LLVMConstInt(i64, (uint64_t)-1, 1), diff, "idx");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], sub,
                                        sub_owned, locals);
                return res;
            }
            if (is_lidx && expr->call.args.count == 1 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals)) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                int sub_owned;
                LLVMValueRef sub = emit_string_like_arg(g, expr->call.args.items[0],
                                                        locals, &sub_owned);
                LLVMValueRef lif = get_str_last_index_of_fn(g);
                LLVMValueRef li_args[] = { s, sub };
                LLVMValueRef res = zan_call2(g->builder, LLVMGlobalGetValueType(lif),
                    lif, li_args, 2, "lidx");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], sub,
                                        sub_owned, locals);
                return res;
            }
        }

        /* str */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 6 && memcmp(sm.str, "Equals", 6) == 0 &&
                expr->call.args.count == 1 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals)) {
                LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                int p_owned;
                LLVMValueRef p = emit_string_like_arg(g, expr->call.args.items[0],
                                                      locals, &p_owned);
                s = emit_str_nonnull(g, s);
                LLVMValueRef ocmp = get_str_ordinal_cmp_fn(g, expr->loc);
                LLVMValueRef cmp = zan_call2(g->builder,
                    LLVMGlobalGetValueType(ocmp), ocmp,
                    (LLVMValueRef[]){ s, p }, 2, "eq.cmp");
                LLVMValueRef res = zan_icmp(g->builder, LLVMIntEQ, cmp,
                    LLVMConstInt(i32, 0, 0), "eq.str");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], p,
                                        p_owned, locals);
                return res;
            }
        }

        /* str.StartsWith(p) / str.EndsWith(p) -> bool. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            int is_sw = (sm.len == 10 && memcmp(sm.str, "StartsWith", 10) == 0);
            int is_ew = (sm.len == 8 && memcmp(sm.str, "EndsWith", 8) == 0);
            if ((is_sw || is_ew) && expr->call.args.count == 1 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals)) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                int p_owned;
                LLVMValueRef p = emit_string_like_arg(g, expr->call.args.items[0],
                                                      locals, &p_owned);
                LLVMValueRef lp = zan_call2(g->builder, strlen_ty, g->fn_strlen, &p, 1, "lp");
                LLVMValueRef res;
                if (is_sw) {
                    LLVMTypeRef strncmp_ty = LLVMFunctionType(i32,
                        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
                    LLVMValueRef strncmp_fn = get_libc_fn(g, "strncmp", strncmp_ty);
                    LLVMValueRef nc_args[] = { s, p, lp };
                    LLVMValueRef cmp = zan_call2(g->builder, strncmp_ty,
                        strncmp_fn, nc_args, 3, "swcmp");
                    res = zan_icmp(g->builder, LLVMIntEQ, cmp,
                        LLVMConstInt(i32, 0, 0), "sw");
                } else {
                    LLVMValueRef ls = zan_call2(g->builder, strlen_ty, g->fn_strlen, &s, 1, "ls");
                    LLVMValueRef off = zan_sub(g->builder, ls, lp, "off");
                    LLVMValueRef fits = zan_icmp(g->builder, LLVMIntSGE, off,
                        LLVMConstInt(i64, 0, 0), "fits");
                    LLVMValueRef offc = LLVMBuildSelect(g->builder, fits, off,
                        LLVMConstInt(i64, 0, 0), "offc");
                    LLVMValueRef tail = LLVMBuildGEP2(g->builder, i8, s, &offc, 1, "tailp");
                    LLVMTypeRef strcmp_ty = LLVMFunctionType(i32,
                        (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                    LLVMValueRef strcmp_fn = get_libc_fn(g, "strcmp", strcmp_ty);
                    LLVMValueRef sc_args[] = { tail, p };
                    LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty,
                        strcmp_fn, sc_args, 2, "ewcmp");
                    LLVMValueRef eq = zan_icmp(g->builder, LLVMIntEQ, cmp,
                        LLVMConstInt(i32, 0, 0), "eweq");
                    res = zan_and(g->builder, fits, eq, "ew");
                }
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], p,
                                        p_owned, locals);
                return res;
            }
        }

        /* str.Replace(from, to) -> fresh rc string. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 7 && memcmp(sm.str, "Replace", 7) == 0 &&
                expr->call.args.count == 2 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals) &&
                is_string_like_expr(g, expr->call.args.items[1], locals)) {
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                int from_owned, to_owned;
                LLVMValueRef from = emit_string_like_arg(g, expr->call.args.items[0],
                                                         locals, &from_owned);
                LLVMValueRef to = emit_string_like_arg(g, expr->call.args.items[1],
                                                       locals, &to_owned);
                LLVMValueRef rf = get_str_replace_fn(g);
                LLVMValueRef rargs[] = { s, from, to };
                LLVMValueRef res = zan_call2(g->builder,
                    LLVMGlobalGetValueType(rf), rf, rargs, 3, "repl");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], from,
                                        from_owned, locals);
                release_string_like_arg(g, expr->call.args.items[1], to,
                                        to_owned, locals);
                return res;
            }
        }

        /* str */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 0) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            int is_trim = (sm.len == 4 && memcmp(sm.str, "Trim", 4) == 0);
            int is_up = (sm.len == 7 && memcmp(sm.str, "ToUpper", 7) == 0);
            int is_low = (sm.len == 7 && memcmp(sm.str, "ToLower", 7) == 0);
            if ((is_trim || is_up || is_low) &&
                is_string_expr(g, sc->member.object, locals)) {
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef hf = is_trim ? get_str_trim_fn(g)
                                          : get_str_case_fn(g, is_up);
                LLVMValueRef res = zan_call2(g->builder,
                    LLVMGlobalGetValueType(hf), hf, &s, 1, "strh");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                return res;
            }
        }

        /* str */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            (expr->call.args.count == 1 || expr->call.args.count == 2)) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 5 && memcmp(sm.str, "Split", 5) == 0 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals)) {
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                int sep_owned;
                LLVMValueRef sep = emit_string_like_arg(g, expr->call.args.items[0],
                                                        locals, &sep_owned);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef opt = LLVMConstInt(i64, 0, 0);
                if (expr->call.args.count == 2) {
                    LLVMValueRef raw_opt = emit_expr(g, expr->call.args.items[1], locals);
                    opt = coerce_int_to(g, raw_opt, i64);
                }
                LLVMValueRef lst = emit_alloc_rc_collection(g, expr, 24, 1,
                                                            g->binder->type_string);
                LLVMValueRef lp = LLVMBuildBitCast(g->builder, lst,
                    LLVMPointerType(g->list_struct_type, 0), "lp");
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0),
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "cnt"));
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0),
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 1, "cap"));
                LLVMBuildStore(g->builder, LLVMConstNull(LLVMPointerType(i64, 0)),
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "df"));
                LLVMValueRef hf = get_str_split_fn(g);
                zan_call2(g->builder, LLVMGlobalGetValueType(hf), hf,
                    (LLVMValueRef[]){ s, sep, lst, opt }, 4, "");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], sep,
                                        sep_owned, locals);
                return lst;
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_IDENTIFIER &&
            expr->call.args.count == 2 &&
            expr->call.callee->member.name.len == 8 &&
            memcmp(expr->call.callee->member.name.str, "TryParse", 8) == 0) {
            zan_symbol_t *es = zan_binder_lookup(g->binder,
                expr->call.callee->member.object->ident.name);
            if (es && es->kind == SYM_ENUM && es->member_count > 0) {
                zan_symbol_t **mems = (zan_symbol_t **)calloc(
                    (size_t)es->member_count, sizeof(*mems));
                long long *vals = (long long *)calloc(
                    (size_t)es->member_count, sizeof(*vals));
                int n = irgen_enum_members(es, mems, vals,
                                           es->member_count);
                LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8pt = LLVMPointerType(i8t, 0);
                LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef strcmp_ty = LLVMFunctionType(i32t,
                    (LLVMTypeRef[]){ i8pt, i8pt }, 2, 0);
                LLVMValueRef s = emit_expr(g, expr->call.args.items[0],
                                           locals);
                zan_ast_node_t *out_arg = expr->call.args.items[1];
                LLVMValueRef out_ptr = NULL;
                if (out_arg->kind == AST_REF_ARG) {
                    out_ptr = emit_expr(g, out_arg, locals);
                } else if (out_arg->kind == AST_IDENTIFIER) {
                    local_var_t *ol = local_find(locals,
                        out_arg->ident.name);
                    if (ol) { out_ptr = ol->alloca; }
                }
                LLVMValueRef res_a = emit_entry_alloca(g, i32t, "tp.res");
                LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(
                    g->ctx, g->current_fn, "tp.done");
                LLVMBasicBlockRef miss_bb = LLVMAppendBasicBlockInContext(
                    g->ctx, g->current_fn, "tp.miss");
                for (int i = 0; i < n; i++) {
                    LLVMBasicBlockRef next_bb =
                        (i == n - 1)
                            ? miss_bb
                            : LLVMAppendBasicBlockInContext(g->ctx,
                                g->current_fn, "tp.chk");
                    LLVMValueRef lit = emit_string_literal_rc(g,
                        mems[i]->name);
                    LLVMValueRef cmpv = zan_call2(g->builder, strcmp_ty,
                        g->fn_strcmp, (LLVMValueRef[]){ s, lit }, 2,
                        "tp.cmp");
                    LLVMValueRef eq = LLVMBuildICmp(g->builder, LLVMIntEQ,
                        cmpv, LLVMConstInt(i32t, 0, 0), "tp.eq");
                    LLVMBasicBlockRef hit_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, g->current_fn, "tp.hit");
                    LLVMBuildCondBr(g->builder, eq, hit_bb, next_bb);
                    LLVMPositionBuilderAtEnd(g->builder, hit_bb);
                    if (out_ptr) {
                        emit_typed_out_store(g, es->type, out_ptr,
                            LLVMConstInt(i64t,
                                (unsigned long long)vals[i], 0));
                    }
                    LLVMBuildStore(g->builder, LLVMConstInt(i32t, 1, 0),
                                   res_a);
                    LLVMBuildBr(g->builder, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, next_bb);
                }
                if (out_ptr) {
                    emit_typed_out_store(g, es->type, out_ptr,
                        LLVMConstInt(i64t, 0, 0));
                }
                LLVMBuildStore(g->builder, LLVMConstInt(i32t, 0, 0), res_a);
                LLVMBuildBr(g->builder, done_bb);
                LLVMPositionBuilderAtEnd(g->builder, done_bb);
                emit_release_owned_call_temp(g, expr->call.args.items[0], s,
                                             locals);
                free(mems);
                free(vals);
                return LLVMBuildLoad2(g->builder, i32t, res_a, "tp.out");
            }
        }

        /* value */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 0) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 8 && memcmp(sm.str, "ToString", 8) == 0) {
                zan_type_t *rt_ty = infer_expr_type(g, sc->member.object, locals);
                int handled_kind = rt_ty &&
                    (rt_ty->kind == TYPE_BOOL || rt_ty->kind == TYPE_BYTE ||
                     rt_ty->kind == TYPE_SHORT || rt_ty->kind == TYPE_INT ||
                     rt_ty->kind == TYPE_LONG || rt_ty->kind == TYPE_SBYTE ||
                     rt_ty->kind == TYPE_USHORT || rt_ty->kind == TYPE_UINT ||
                     rt_ty->kind == TYPE_ULONG || rt_ty->kind == TYPE_FLOAT ||
                     rt_ty->kind == TYPE_DOUBLE || rt_ty->kind == TYPE_CHAR ||
                     rt_ty->kind == TYPE_STRING || rt_ty->kind == TYPE_ENUM);
                if (handled_kind) {
                    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef v = emit_expr(g, sc->member.object, locals);
                    LLVMTypeKind vk = LLVMGetTypeKind(LLVMTypeOf(v));
                    if (rt_ty->kind == TYPE_BOOL && vk == LLVMIntegerTypeKind) {
                        /* 语义类型是 bool 就渲染成 "true"/"false"：泛型方法
                           返回值被提升成 i64 时也要认（按宽度判会渲染成 "1"） */
                        LLVMValueRef bc = emit_bool_cond(g, v,
                            sc->member.object, locals);
                        if (bc) return emit_bool_to_cstr(g, bc);
                    }
                    if (vk == LLVMPointerTypeKind) {
                        LLVMTypeRef strlen_ty = LLVMFunctionType(i64,
                            (LLVMTypeRef[]){ i8ptr }, 1, 0);
                        LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr,
                            (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
                        LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);
                        LLVMValueRef n = zan_call2(g->builder, strlen_ty,
                            g->fn_strlen, &v, 1, "n");
                        LLVMValueRef bufsz = zan_add(g->builder, n,
                            LLVMConstInt(i64, 1, 0), "bsz");
                        LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
                        LLVMValueRef mcargs[] = { buf, v, bufsz };
                        zan_call2(g->builder, memcpy_ty, memcpy_fn, mcargs, 3, "");
                        emit_release_owned_call_temp(g, sc->member.object, v, locals);
                        return buf;
                    }
                    if (rt_ty->kind == TYPE_ENUM && rt_ty->sym &&
                        rt_ty->sym->member_count > 0) {
                        int cap = rt_ty->sym->member_count;
                        zan_symbol_t **mems = (zan_symbol_t **)calloc(
                            (size_t)cap, sizeof(*mems));
                        long long *vals = (long long *)calloc(
                            (size_t)cap, sizeof(*vals));
                        int n = irgen_enum_members(rt_ty->sym, mems, vals,
                                                   cap);
                        if (n == 0) {
                            free(mems);
                            free(vals);
                        }
                        if (n > 0) {
                            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                            LLVMValueRef v64 = emit_widen_i64_for_print(g, v);
                            LLVMValueRef fn = LLVMGetBasicBlockParent(
                                LLVMGetInsertBlock(g->builder));
                            LLVMBasicBlockRef joinbb =
                                LLVMAppendBasicBlockInContext(g->ctx, fn,
                                                              "em.join");
                            LLVMBasicBlockRef *hitbbs = (LLVMBasicBlockRef *)
                                calloc((size_t)n, sizeof(*hitbbs));
                            LLVMValueRef *lits = (LLVMValueRef *)calloc(
                                (size_t)n, sizeof(*lits));
                            LLVMBasicBlockRef cur =
                                LLVMGetInsertBlock(g->builder);
                            for (int i = 0; i < n; i++) {
                                LLVMBasicBlockRef miss =
                                    (i == n - 1)
                                        ? LLVMAppendBasicBlockInContext(g->ctx,
                                            fn, "em.num")
                                        : LLVMAppendBasicBlockInContext(g->ctx,
                                            fn, "em.chk");
                                LLVMValueRef hit = LLVMBuildICmp(g->builder,
                                    LLVMIntEQ, v64,
                                    LLVMConstInt(i64t,
                                        (unsigned long long)vals[i], 0),
                                    "em.val");
                                hitbbs[i] = LLVMAppendBasicBlockInContext(
                                    g->ctx, fn, "em.hit");
                                LLVMBuildCondBr(g->builder, hit, hitbbs[i],
                                                miss);
                                /* 字面量在对应分支基本块内具化以保证支配 PHI 节点输入 */
                                LLVMPositionBuilderAtEnd(g->builder, hitbbs[i]);
                                lits[i] = emit_string_literal_rc(g,
                                    mems[i]->name);
                                LLVMBuildBr(g->builder, joinbb);
                                LLVMPositionBuilderAtEnd(g->builder, miss);
                                cur = miss;
                            }
                            LLVMValueRef ebuf = emit_string_alloc_rc(g,
                                LLVMConstInt(i64t, 32, 0));
                            emit_itoa_into(g, ebuf, v64, 0);
                            LLVMBuildBr(g->builder, joinbb);
                            LLVMPositionBuilderAtEnd(g->builder, joinbb);
                            LLVMValueRef phi = LLVMBuildPhi(g->builder, i8ptr,
                                                            "em.name");
                            LLVMValueRef *vals_in = (LLVMValueRef *)calloc(
                                (size_t)n + 1, sizeof(LLVMValueRef));
                            LLVMBasicBlockRef *blks_in = (LLVMBasicBlockRef *)
                                calloc((size_t)n + 1, sizeof(*blks_in));
                            for (int i = 0; i < n; i++) {
                                vals_in[i] = lits[i];
                                blks_in[i] = hitbbs[i];
                            }
                            vals_in[n] = ebuf;
                            blks_in[n] = cur;
                            LLVMAddIncoming(phi, vals_in, blks_in, n + 1);
                            free(vals_in);
                            free(blks_in);
                            free(hitbbs);
                            free(lits);
                            free(mems);
                            free(vals);
                            return phi;
                        }
                    }
                    if (rt_ty->kind == TYPE_CHAR &&
                        LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind) {
                        /* A523：char.ToString() 曾落通用 itoa 返回码点数字
                         * "65"（C# "A"）；WriteLine(char) 与拼接路径早就是
                         * 字符渲染——两条路对齐到同一个 UTF-8 编码帮手。 */
                        return emit_char_to_cstr(g, v);
                    }
                    LLVMValueRef buf = emit_string_alloc_rc(g,
                        LLVMConstInt(i64, 40, 0));
                    if (vk == LLVMDoubleTypeKind || vk == LLVMFloatTypeKind) {
                        emit_dbl_str(g, buf, LLVMConstInt(i64, 40, 0), v);
                        return buf;
                    }
                    emit_itoa_into(g, buf, emit_widen_i64_for_print(g, v),
                                   rt_ty->kind == TYPE_ULONG ? 1 : 0);
                    return buf;
                }
            }
        }

        /* A525 一件：标量 ToString("F2"/"D5"/"X4"/"E2"/"G3") 格式串——复用
         * 插值路径的 interp_format_to_printf（同一 spec→printf 映射，同一
         * 口径，含 D/X/F/E/G 与 0/# 形状）。数值接收者之外（bool/char/
         * 枚举的自定义格式）或不认识的说明符编译期显性拒绝，不静默。 */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 1 &&
            expr->call.args.items[0]->kind == AST_STRING_LITERAL) {
            zan_ast_node_t *scf = expr->call.callee;
            zan_istr_t smf = scf->member.name;
            if (smf.len == 8 && memcmp(smf.str, "ToString", 8) == 0) {
                zan_type_t *rf_ty = infer_expr_type(g, scf->member.object, locals);
                int numeric_kind = rf_ty &&
                    (rf_ty->kind == TYPE_BYTE ||
                     rf_ty->kind == TYPE_SHORT || rf_ty->kind == TYPE_INT ||
                     rf_ty->kind == TYPE_LONG || rf_ty->kind == TYPE_SBYTE ||
                     rf_ty->kind == TYPE_USHORT || rf_ty->kind == TYPE_UINT ||
                     rf_ty->kind == TYPE_ULONG || rf_ty->kind == TYPE_FLOAT ||
                     rf_ty->kind == TYPE_DOUBLE);
                if (numeric_kind) {
                    zan_istr_t spec = expr->call.args.items[0]->str_val;
                    bool want_float = false;
                    bool is_f = rf_ty->kind == TYPE_FLOAT ||
                                rf_ty->kind == TYPE_DOUBLE;
                    bool is_ul = rf_ty->kind == TYPE_ULONG;
                    char pfmt[64];
                    if (interp_format_to_printf(&spec, is_f, is_ul,
                                                &want_float, pfmt,
                                                sizeof(pfmt)) == 0) {
                        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3061,
                            "unsupported format specifier '%.*s' for numeric "
                            "ToString() (supported: D, X, F, E, G, 0/# shapes)",
                            (int)spec.len, spec.str);
                        return LLVMConstNull(LLVMPointerType(
                            LLVMInt8TypeInContext(g->ctx), 0));
                    }
                    LLVMValueRef vf = emit_expr(g, scf->member.object, locals);
                    LLVMValueRef arg = vf;
                    if (want_float) {
                        if (LLVMGetTypeKind(LLVMTypeOf(vf)) ==
                            LLVMFloatTypeKind)
                            arg = LLVMBuildFPExt(g->builder, vf,
                                                 LLVMDoubleTypeInContext(g->ctx),
                                                 "tos.f64");
                    } else {
                        arg = emit_widen_i64_for_print(g, vf);
                    }
                    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
                    LLVMTypeRef i8pt = LLVMPointerType(i8t, 0);
                    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef pfmt_lit = zan_irgen_intern_string(g, pfmt);
                    LLVMValueRef buf = emit_string_alloc_rc(g,
                        LLVMConstInt(i64t, 640, 0));
                    LLVMValueRef snp_args[] = { buf,
                        LLVMConstInt(i64t, 640, 0), pfmt_lit, arg };
                    LLVMTypeRef snp_ty = LLVMFunctionType(i8pt,
                        (LLVMTypeRef[]){ i8pt, i64t, i8pt }, 3, 1);
                    zan_call2(g->builder, snp_ty, g->fn_snprintf, snp_args,
                              4, "");
                    return buf;
                }
            }
        }

        /* Environment */
        if (!zan_type_defines(g, "Environment", "ArgCount") &&
            is_call_to(expr, "Environment", "ArgCount") && expr->call.args.count == 0) {
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef g_argc = LLVMGetNamedGlobal(g->mod, "__zan_argc");
            if (!g_argc) {
                g_argc = LLVMAddGlobal(g->mod, i32, "__zan_argc");
                LLVMSetInitializer(g_argc, LLVMConstInt(i32, 0, 0));
            }
            LLVMValueRef ac = LLVMBuildLoad2(g->builder, i32, g_argc, "argc");
            LLVMValueRef ac64 = LLVMBuildSExt(g->builder, ac, i64, "argc64");
            return zan_sub(g->builder, ac64, LLVMConstInt(i64, 1, 0), "nargs");
        }

        /* Environment */
        if (!zan_type_defines(g, "Environment", "ArgAt") &&
            is_call_to(expr, "Environment", "ArgAt") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i8ptrptr = LLVMPointerType(i8ptr, 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            zan_ast_node_t *idx_ast = expr->call.args.items[0];
            LLVMValueRef idx = emit_expr(g, idx_ast, locals);
            LLVMValueRef g_argc = LLVMGetNamedGlobal(g->mod, "__zan_argc");
            if (!g_argc) {
                g_argc = LLVMAddGlobal(g->mod, i32, "__zan_argc");
                LLVMSetInitializer(g_argc, LLVMConstInt(i32, 0, 0));
            }
            LLVMValueRef argc = LLVMBuildLoad2(g->builder, i32, g_argc, "argc");
            LLVMValueRef argc64 = LLVMBuildSExt(g->builder, argc, i64, "argc64");
            LLVMValueRef idx1 = zan_add(g->builder, idx, LLVMConstInt(i64, 1, 0), "argi");
            LLVMValueRef in_range = zan_and(g->builder,
                zan_icmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0), "arg.nneg"),
                zan_icmp(g->builder, LLVMIntULT, idx1, argc64, "arg.range"),
                "arg.ok");
            emit_release_owned_call_temp(g, idx_ast, idx, locals);

            LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
            LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "argat.ok");
            LLVMBasicBlockRef out_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "argat.out");
            LLVMBuildCondBr(g->builder, in_range, ok_bb, out_bb);

            LLVMPositionBuilderAtEnd(g->builder, ok_bb);
            LLVMValueRef g_argv = LLVMGetNamedGlobal(g->mod, "__zan_argv");
            if (!g_argv) {
                g_argv = LLVMAddGlobal(g->mod, i8ptrptr, "__zan_argv");
                LLVMSetInitializer(g_argv, LLVMConstNull(i8ptrptr));
            }
            LLVMValueRef argv = LLVMBuildLoad2(g->builder, i8ptrptr, g_argv, "argv");
            LLVMValueRef slot = LLVMBuildGEP2(g->builder, i8ptr, argv, &idx1, 1, "argslot");
            LLVMValueRef arg = LLVMBuildLoad2(g->builder, i8ptr, slot, "arg");
            LLVMBuildBr(g->builder, out_bb);

            LLVMPositionBuilderAtEnd(g->builder, out_bb);
            LLVMValueRef empty = emit_string_literal_rc(g, (zan_istr_t){ "", 0 });
            LLVMValueRef res = LLVMBuildPhi(g->builder, i8ptr, "argres");
            LLVMValueRef vals[] = { arg, empty };
            LLVMBasicBlockRef bbs[] = { ok_bb, cur_bb };
            LLVMAddIncoming(res, vals, bbs, 2);
            return res;
        }

        /* File.ReadAllText(path) -> string */
        if (!zan_type_defines(g, "File", "ReadAllText") &&
            is_call_to(expr, "File", "ReadAllText") && expr->call.args.count == 1 &&
            !src_method_takes_over(g, expr, locals)) {
            zan_ast_node_t *path_ast = expr->call.args.items[0];
            LLVMValueRef path_arg = emit_expr(g, path_ast, locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef fopen_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", fopen_type);
            }
            LLVMValueRef fseek_fn = LLVMGetNamedFunction(g->mod, "fseek");
            if (!fseek_fn) {
                LLVMTypeRef fseek_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0);
                fseek_fn = LLVMAddFunction(g->mod, "fseek", fseek_type);
            }
            LLVMValueRef ftell_fn = LLVMGetNamedFunction(g->mod, "ftell");
            if (!ftell_fn) {
                LLVMTypeRef ftell_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                ftell_fn = LLVMAddFunction(g->mod, "ftell", ftell_type);
            }
            LLVMValueRef fread_fn = LLVMGetNamedFunction(g->mod, "fread");
            if (!fread_fn) {
                LLVMTypeRef fread_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0);
                fread_fn = LLVMAddFunction(g->mod, "fread", fread_type);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef fclose_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", fclose_type);
            }
            LLVMValueRef mode = zan_irgen_intern_string(g, "rb");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            emit_fopen_check(g, fp, "cannot read file\n");
            LLVMValueRef seek_end_args[] = { fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 2, 0) };
            LLVMValueRef se_end = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_end_args, 3, "se_end");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntNE, se_end, LLVMConstInt(i32, 0, 0), "se_end.err"),
                "cannot read file\n");
            LLVMValueRef size = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), ftell_fn, &fp, 1, "sz");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntSLT, size, LLVMConstInt(i64, 0, 0), "sz.err"),
                "cannot read file\n");
            LLVMValueRef seek_start_args[] = { fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 0, 0) };
            LLVMValueRef se_start = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_start_args, 3, "se_start");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntNE, se_start, LLVMConstInt(i32, 0, 0), "se_start.err"),
                "cannot read file\n");
            LLVMValueRef buf_size = zan_add(g->builder, size, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
            LLVMValueRef fread_args[] = { buf, LLVMConstInt(i64, 1, 0), size, fp };
            LLVMValueRef got = zan_call2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fread_fn, fread_args, 4, "got");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntNE, got, size, "got.err"),
                "cannot read file\n");
            LLVMValueRef end_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), buf, &size, 1, "end");
            LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0), end_ptr);
            LLVMValueRef rd_close = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "rd_close");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntNE, rd_close, LLVMConstInt(i32, 0, 0), "rd_close.err"),
                "cannot read file\n");
            emit_release_owned_call_temp(g, path_ast, path_arg, locals);
            return buf;
        }

        /* File.WriteAllText(path, content) */
        if (!zan_type_defines(g, "File", "WriteAllText") &&
            is_call_to(expr, "File", "WriteAllText") && expr->call.args.count == 2 &&
            !src_method_takes_over(g, expr, locals)) {
            zan_ast_node_t *path_ast = expr->call.args.items[0];
            zan_ast_node_t *content_ast = expr->call.args.items[1];
            LLVMValueRef path_arg = emit_expr(g, path_ast, locals);
            LLVMValueRef content_arg = emit_expr(g, content_ast, locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef fopen_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", fopen_type);
            }
            LLVMValueRef fputs_fn = LLVMGetNamedFunction(g->mod, "fputs");
            if (!fputs_fn) {
                LLVMTypeRef fputs_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fputs_fn = LLVMAddFunction(g->mod, "fputs", fputs_type);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef fclose_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", fclose_type);
            }
            LLVMValueRef mode = zan_irgen_intern_string(g, "w");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            emit_fopen_check(g, fp, "cannot write file\n");
            LLVMValueRef fputs_args[] = { content_arg, fp };
            LLVMValueRef put = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fputs_fn, fputs_args, 2, "put");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntSLT, put, LLVMConstInt(i32, 0, 0), "put.err"),
                "cannot write file\n");
            LLVMValueRef wr_close = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "wr_close");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntNE, wr_close, LLVMConstInt(i32, 0, 0), "wr_close.err"),
                "cannot write file\n");
            emit_release_owned_call_temp(g, path_ast, path_arg, locals);
            emit_release_owned_call_temp(g, content_ast, content_arg, locals);
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        if (!zan_type_defines(g, "Path", "GetFileName") &&
            is_call_to(expr, "Path", "GetFileName") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            zan_ast_node_t *path_ast = expr->call.args.items[0];
            LLVMValueRef path_val = emit_expr(g, path_ast, locals);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef slash_args[] = { path_val, LLVMConstInt(i32t, '/', 0) };
            LLVMValueRef slash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, slash_args, 2, "slash");
            LLVMValueRef bslash_args[] = { path_val, LLVMConstInt(i32t, 92, 0) }; /* 92 = backslash */
            LLVMValueRef bslash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, bslash_args, 2, "bslash");
            LLVMTypeRef strlen_ty = LLVMFunctionType(LLVMInt64TypeInContext(g->ctx),
                (LLVMTypeRef[]){ i8ptr }, 1, 0);
            LLVMValueRef plen = zan_call2(g->builder, strlen_ty, g->fn_strlen,
                &path_val, 1, "plen");
            LLVMValueRef pend = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                path_val, &plen, 1, "pend");
            LLVMValueRef s_null = zan_icmp(g->builder, LLVMIntEQ, slash,
                LLVMConstNull(i8ptr), "snull");
            LLVMValueRef b_null = zan_icmp(g->builder, LLVMIntEQ, bslash,
                LLVMConstNull(i8ptr), "bnull");
            LLVMValueRef both_null = zan_and(g->builder, s_null, b_null, "bothnull");
            LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
            LLVMBasicBlockRef both_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.both");
            LLVMBasicBlockRef notboth_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.notboth");
            LLVMBasicBlockRef use_bs_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.bs");
            LLVMBasicBlockRef check_b_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.chkb");
            LLVMBasicBlockRef use_sl_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.sl");
            LLVMBasicBlockRef max_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.max");
            LLVMBasicBlockRef emit_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.emit");
            LLVMBuildCondBr(g->builder, both_null, both_bb, notboth_bb);

            LLVMPositionBuilderAtEnd(g->builder, both_bb);
            LLVMBuildBr(g->builder, emit_bb);

            LLVMPositionBuilderAtEnd(g->builder, notboth_bb);
            LLVMBuildCondBr(g->builder, s_null, use_bs_bb, check_b_bb);

            LLVMPositionBuilderAtEnd(g->builder, use_bs_bb);
            LLVMValueRef bs1 = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                bslash, (LLVMValueRef[]){ LLVMConstInt(i32t, 1, 0) }, 1, "bs1");
            LLVMBuildBr(g->builder, emit_bb);

            LLVMPositionBuilderAtEnd(g->builder, check_b_bb);
            LLVMBuildCondBr(g->builder, b_null, use_sl_bb, max_bb);

            LLVMPositionBuilderAtEnd(g->builder, use_sl_bb);
            LLVMValueRef sl1 = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                slash, (LLVMValueRef[]){ LLVMConstInt(i32t, 1, 0) }, 1, "sl1");
            LLVMBuildBr(g->builder, emit_bb);

            LLVMPositionBuilderAtEnd(g->builder, max_bb);
            LLVMValueRef s_gt = zan_icmp(g->builder, LLVMIntUGT, slash, bslash, "sgt");
            LLVMValueRef best = LLVMBuildSelect(g->builder, s_gt, slash, bslash, "best");
            LLVMValueRef b2 = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                best, (LLVMValueRef[]){ LLVMConstInt(i32t, 1, 0) }, 1, "b2");
            LLVMBuildBr(g->builder, emit_bb);

            LLVMPositionBuilderAtEnd(g->builder, emit_bb);
            LLVMValueRef start = LLVMBuildPhi(g->builder, i8ptr, "fn.start");
            LLVMAddIncoming(start,
                (LLVMValueRef[]){ path_val, bs1, sl1, b2 },
                (LLVMBasicBlockRef[]){ both_bb, use_bs_bb, use_sl_bb, max_bb }, 4);
            LLVMValueRef fname = emit_string_copy_range(g, start, pend);
            emit_release_owned_call_temp(g, path_ast, path_val, locals);
            return fname;
        }

        if (!zan_type_defines(g, "Path", "GetExtension") &&
            is_call_to(expr, "Path", "GetExtension") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            zan_ast_node_t *path_ast = expr->call.args.items[0];
            LLVMValueRef path_val = emit_expr(g, path_ast, locals);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef dot_args[] = { path_val, LLVMConstInt(i32t, '.', 0) };
            LLVMValueRef dot = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, dot_args, 2, "dot");
            LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, dot,
                LLVMConstNull(i8ptr), "dnull");
            LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
            LLVMBasicBlockRef empty_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "ext.empty");
            LLVMBasicBlockRef copy_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "ext.copy");
            LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "ext.done");
            LLVMBuildCondBr(g->builder, is_null, empty_bb, copy_bb);

            LLVMPositionBuilderAtEnd(g->builder, empty_bb);
            LLVMValueRef empty = emit_string_literal_rc(g, (zan_istr_t){ "", 0 });
            LLVMBuildBr(g->builder, done_bb);

            LLVMPositionBuilderAtEnd(g->builder, copy_bb);
            LLVMTypeRef strlen_ty = LLVMFunctionType(LLVMInt64TypeInContext(g->ctx),
                (LLVMTypeRef[]){ i8ptr }, 1, 0);
            LLVMValueRef plen = zan_call2(g->builder, strlen_ty, g->fn_strlen,
                &path_val, 1, "plen");
            LLVMValueRef pend = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                path_val, &plen, 1, "pend");
            LLVMValueRef ext = emit_string_copy_range(g, dot, pend);
            LLVMBuildBr(g->builder, done_bb);

            LLVMPositionBuilderAtEnd(g->builder, done_bb);
            LLVMValueRef res = LLVMBuildPhi(g->builder, i8ptr, "ext.res");
            LLVMAddIncoming(res, (LLVMValueRef[]){ empty, ext },
                (LLVMBasicBlockRef[]){ empty_bb, copy_bb }, 2);
            emit_release_owned_call_temp(g, path_ast, path_val, locals);
            return res;
        }

        if (!zan_type_defines(g, "Path", "Combine") &&
            is_call_to(expr, "Path", "Combine") && expr->call.args.count == 2) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef a = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef b = emit_expr(g, expr->call.args.items[1], locals);
            LLVMTypeRef strlen_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
            LLVMValueRef len_a = zan_call2(g->builder, strlen_type, g->fn_strlen, &a, 1, "la");
            LLVMValueRef len_b = zan_call2(g->builder, strlen_type, g->fn_strlen, &b, 1, "lb");
            LLVMValueRef total = zan_add(g->builder, len_a, len_b, "t");
            total = zan_add(g->builder, total, LLVMConstInt(i64, 2, 0), "t2"); /* +separator+null */
            LLVMValueRef buf = emit_string_alloc_rc(g, total);
            LLVMValueRef strcpy_args[] = { buf, a };
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcpy, strcpy_args, 2, "");
            LLVMValueRef sep = (g->target_is_windows ? zan_irgen_intern_string(g, "\\")
                                  : zan_irgen_intern_string(g, "/"));
            LLVMValueRef cat1_args[] = { buf, sep };
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcat, cat1_args, 2, "");
            LLVMValueRef cat2_args[] = { buf, b };
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcat, cat2_args, 2, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], a, locals);
            emit_release_owned_call_temp(g, expr->call.args.items[1], b, locals);
            return buf;
        }

        /* File.AppendAllText(path, content) */
        if (!zan_type_defines(g, "File", "AppendAllText") &&
            is_call_to(expr, "File", "AppendAllText") && expr->call.args.count == 2 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef content_arg = emit_expr(g, expr->call.args.items[1], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef fopen_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", fopen_type);
            }
            LLVMValueRef fputs_fn = LLVMGetNamedFunction(g->mod, "fputs");
            if (!fputs_fn) {
                LLVMTypeRef fputs_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fputs_fn = LLVMAddFunction(g->mod, "fputs", fputs_type);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef fclose_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", fclose_type);
            }
            LLVMValueRef mode = zan_irgen_intern_string(g, "a");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            emit_fopen_check(g, fp, "cannot write file\n");
            LLVMValueRef fputs_args[] = { content_arg, fp };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fputs_fn, fputs_args, 2, "");
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            emit_release_owned_call_temp(g, expr->call.args.items[1], content_arg, locals);
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* File.Exists(path) -> bool */
        if (!zan_type_defines(g, "File", "Exists") &&
            is_call_to(expr, "File", "Exists") && expr->call.args.count == 1 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef fopen_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", fopen_type);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef fclose_type = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", fclose_type);
            }
            LLVMValueRef mode = zan_irgen_intern_string(g, "rb");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntNE, fp,
                LLVMConstNull(i8ptr), "exists");
            LLVMBasicBlockRef close_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "fexist.close");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "fexist.end");
            LLVMBuildCondBr(g->builder, is_null, close_bb, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, close_bb);
            zan_call2(g->builder, LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "");
            LLVMBuildBr(g->builder, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            LLVMValueRef result = LLVMBuildZExt(g->builder, is_null, LLVMInt64TypeInContext(g->ctx), "fex");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return result;
        }

        /* File.Delete(path) */
        if (!zan_type_defines(g, "File", "Delete") &&
            is_call_to(expr, "File", "Delete") && expr->call.args.count == 1 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef remove_fn = LLVMGetNamedFunction(g->mod, "remove");
            if (!remove_fn) {
                LLVMTypeRef remove_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                remove_fn = LLVMAddFunction(g->mod, "remove", remove_type);
            }
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                remove_fn, &path_arg, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* File.Move(source, dest) — rename */
        if (!zan_type_defines(g, "File", "Move") &&
            is_call_to(expr, "File", "Move") && expr->call.args.count == 2 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef src = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef dst = emit_expr(g, expr->call.args.items[1], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef rename_fn = LLVMGetNamedFunction(g->mod, "rename");
            if (!rename_fn) {
                LLVMTypeRef rename_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                rename_fn = LLVMAddFunction(g->mod, "rename", rename_type);
            }
            LLVMValueRef args[] = { src, dst };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                rename_fn, args, 2, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], src, locals);
            emit_release_owned_call_temp(g, expr->call.args.items[1], dst, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        if (!zan_type_defines(g, "File", "Copy") &&
            is_call_to(expr, "File", "Copy") && expr->call.args.count == 2 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef src = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef dst = emit_expr(g, expr->call.args.items[1], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", ft);
            }
            LLVMValueRef fread_fn = LLVMGetNamedFunction(g->mod, "fread");
            if (!fread_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0);
                fread_fn = LLVMAddFunction(g->mod, "fread", ft);
            }
            LLVMValueRef fwrite_fn = LLVMGetNamedFunction(g->mod, "fwrite");
            if (!fwrite_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0);
                fwrite_fn = LLVMAddFunction(g->mod, "fwrite", ft);
            }
            LLVMValueRef fseek_fn = LLVMGetNamedFunction(g->mod, "fseek");
            if (!fseek_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0);
                fseek_fn = LLVMAddFunction(g->mod, "fseek", ft);
            }
            LLVMValueRef ftell_fn = LLVMGetNamedFunction(g->mod, "ftell");
            if (!ftell_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                ftell_fn = LLVMAddFunction(g->mod, "ftell", ft);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", ft);
            }
            LLVMValueRef rb = zan_irgen_intern_string(g, "rb");
            LLVMValueRef sargs[] = { src, rb };
            LLVMValueRef sfp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, sargs, 2, "sfp");
            emit_fopen_check(g, sfp, "cannot read file\n");
            LLVMValueRef zero64 = LLVMConstInt(i64, 0, 0);
            LLVMValueRef seek_end[] = { sfp, zero64, LLVMConstInt(i32, 2, 0) };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_end, 3, "");
            LLVMValueRef sz = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), ftell_fn, &sfp, 1, "sz");
            sz = LLVMBuildSelect(g->builder,
                zan_icmp(g->builder, LLVMIntSLT, sz, zero64, "sz.neg"),
                zero64, sz, "sz.clamp");
            LLVMValueRef seek_start[] = { sfp, zero64, LLVMConstInt(i32, 0, 0) };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_start, 3, "");
            LLVMValueRef buf = zan_call2(g->builder,
                LLVMGlobalGetValueType(g->fn_malloc), g->fn_malloc,
                &sz, 1, "cp.buf");
            {
                LLVMTypeRef i8ptr_t = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntEQ, buf,
                    LLVMConstPointerNull(i8ptr_t), "cp.buf.null"),
                    "out of memory\n");
            }
            LLVMValueRef fread_args[] = { buf, LLVMConstInt(i64, 1, 0), sz, sfp };
            zan_call2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fread_fn, fread_args, 4, "");
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &sfp, 1, "");
            LLVMValueRef wb = zan_irgen_intern_string(g, "wb");
            LLVMValueRef dargs[] = { dst, wb };
            LLVMValueRef dfp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, dargs, 2, "dfp");
            emit_fopen_check(g, dfp, "cannot write file\n");
            LLVMValueRef fwrite_args[] = { buf, LLVMConstInt(i64, 1, 0), sz, dfp };
            zan_call2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fwrite_fn, fwrite_args, 4, "");
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &dfp, 1, "");
            zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0),
                g->fn_free, &buf, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], src, locals);
            emit_release_owned_call_temp(g, expr->call.args.items[1], dst, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* File.GetSize(path) -> int */
        if (!zan_type_defines(g, "File", "GetSize") &&
            is_call_to(expr, "File", "GetSize") && expr->call.args.count == 1 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", ft);
            }
            LLVMValueRef fseek_fn = LLVMGetNamedFunction(g->mod, "fseek");
            if (!fseek_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0);
                fseek_fn = LLVMAddFunction(g->mod, "fseek", ft);
            }
            LLVMValueRef ftell_fn = LLVMGetNamedFunction(g->mod, "ftell");
            if (!ftell_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                ftell_fn = LLVMAddFunction(g->mod, "ftell", ft);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", ft);
            }
            LLVMValueRef mode = zan_irgen_intern_string(g, "rb");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            LLVMValueRef seek_end[] = { fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 2, 0) };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_end, 3, "");
            LLVMValueRef sz = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), ftell_fn, &fp, 1, "fsz");
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return sz;
        }

        /* Directory.Exists(path) -> bool */
        if (!zan_type_defines(g, "Directory", "Exists") &&
            is_call_to(expr, "Directory", "Exists") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            /* Windows: GetFileAttributesA & FILE_ATTRIBUTE_DIRECTORY */
            LLVMValueRef result;
            if (g->target_is_windows) {
                LLVMValueRef gfa_fn = LLVMGetNamedFunction(g->mod, "GetFileAttributesA");
                if (!gfa_fn) {
                    LLVMTypeRef gfa_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    gfa_fn = LLVMAddFunction(g->mod, "GetFileAttributesA", gfa_type);
                }
                LLVMValueRef attrs = zan_call2(g->builder,
                    LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    gfa_fn, &path_arg, 1, "attrs");
                LLVMValueRef not_invalid = zan_icmp(g->builder, LLVMIntNE, attrs,
                    LLVMConstInt(i32, 0xFFFFFFFF, 0), "noinv");
                LLVMValueRef is_dir = zan_and(g->builder, attrs,
                    LLVMConstInt(i32, 0x10, 0), "isdir");
                LLVMValueRef is_dir_bool = zan_icmp(g->builder, LLVMIntNE, is_dir,
                    LLVMConstInt(i32, 0, 0), "isdirb");
                result = zan_and(g->builder, not_invalid, is_dir_bool, "dexist");
            } else {
                LLVMValueRef opendir_fn = LLVMGetNamedFunction(g->mod, "opendir");
                if (!opendir_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    opendir_fn = LLVMAddFunction(g->mod, "opendir", ft);
                }
                LLVMValueRef dirp = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    opendir_fn, &path_arg, 1, "dirp");
                result = zan_icmp(g->builder, LLVMIntNE, dirp, LLVMConstNull(i8ptr), "dopen");
                LLVMValueRef closedir_fn = LLVMGetNamedFunction(g->mod, "closedir");
                if (!closedir_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    closedir_fn = LLVMAddFunction(g->mod, "closedir", ft);
                }
                LLVMBasicBlockRef cl_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "de.close");
                LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "de.end");
                LLVMBuildCondBr(g->builder, result, cl_bb, end_bb);
                LLVMPositionBuilderAtEnd(g->builder, cl_bb);
                zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    closedir_fn, &dirp, 1, "");
                LLVMBuildBr(g->builder, end_bb);
                LLVMPositionBuilderAtEnd(g->builder, end_bb);
            }
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMBuildZExt(g->builder, result, i64, "dex");
        }

        /* Environment */
        if (!zan_type_defines(g, "Environment", "ExeDir") &&
            is_call_to(expr, "Environment", "ExeDir") && expr->call.args.count == 0) {
            g->uses_sync_runtime = true;
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 1024, 0));
            LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_exe_dir_into");
            if (!fn) fn = LLVMAddFunction(g->mod, "zan_exe_dir_into", ft);
            LLVMValueRef args2[] = { buf, LLVMConstInt(i64, 1024, 0) };
            zan_call2(g->builder, ft, fn, args2, 2, "");
            return buf;
        }

        /* Directory */
        if (!zan_type_defines(g, "Directory", "ListNames") &&
            is_call_to(expr, "Directory", "ListNames") && expr->call.args.count == 1) {
            g->uses_sync_runtime = true;
            LLVMValueRef pat = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 65536, 0));
            LLVMTypeRef ft = LLVMFunctionType(i64,
                (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_dir_list_into");
            if (!fn) fn = LLVMAddFunction(g->mod, "zan_dir_list_into", ft);
            LLVMValueRef args3[] = { pat, buf, LLVMConstInt(i64, 65536, 0) };
            zan_call2(g->builder, ft, fn, args3, 3, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], pat, locals);
            return buf;
        }

        /* Directory.CreateDirectory(path) */
        if (!zan_type_defines(g, "Directory", "CreateDirectory") &&
            is_call_to(expr, "Directory", "CreateDirectory") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            if (g->target_is_windows) {
                LLVMValueRef mkdir_fn = LLVMGetNamedFunction(g->mod, "_mkdir");
                if (!mkdir_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    mkdir_fn = LLVMAddFunction(g->mod, "_mkdir", ft);
                }
                zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    mkdir_fn, &path_arg, 1, "");
            } else {
                LLVMValueRef mkdir_fn = LLVMGetNamedFunction(g->mod, "mkdir");
                if (!mkdir_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0);
                    mkdir_fn = LLVMAddFunction(g->mod, "mkdir", ft);
                }
                LLVMValueRef margs[] = { path_arg, LLVMConstInt(i32, 0777, 0) };
                zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0),
                    mkdir_fn, margs, 2, "");
            }
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* Directory.Delete(path) */
        if (!zan_type_defines(g, "Directory", "Delete") &&
            is_call_to(expr, "Directory", "Delete") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            const char *rmname = g->target_is_windows ? "_rmdir" : "rmdir";
            LLVMValueRef rmdir_fn = LLVMGetNamedFunction(g->mod, rmname);
            if (!rmdir_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                rmdir_fn = LLVMAddFunction(g->mod, rmname, ft);
            }
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                rmdir_fn, &path_arg, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* Directory.GetCurrentDirectory() -> string */
        if (!zan_type_defines(g, "Directory", "GetCurrentDirectory") &&
            is_call_to(expr, "Directory", "GetCurrentDirectory") && expr->call.args.count == 0) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 4096, 0));
            const char *cwdname = g->target_is_windows ? "_getcwd" : "getcwd";
            LLVMValueRef getcwd_fn = LLVMGetNamedFunction(g->mod, cwdname);
            if (!getcwd_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0);
                getcwd_fn = LLVMAddFunction(g->mod, cwdname, ft);
            }
            LLVMValueRef cwd_args[] = { buf, LLVMConstInt(i32, 4096, 0) };
            zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0),
                getcwd_fn, cwd_args, 2, "");
            return buf;
        }

        /* Directory.SetCurrentDirectory(path) */
        if (!zan_type_defines(g, "Directory", "SetCurrentDirectory") &&
            is_call_to(expr, "Directory", "SetCurrentDirectory") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            const char *chname = g->target_is_windows ? "_chdir" : "chdir";
            LLVMValueRef chdir_fn = LLVMGetNamedFunction(g->mod, chname);
            if (!chdir_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                chdir_fn = LLVMAddFunction(g->mod, chname, ft);
            }
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                chdir_fn, &path_arg, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* Path.GetDirectoryName(path) -> string */
        if (!zan_type_defines(g, "Path", "GetDirectoryName") &&
            is_call_to(expr, "Path", "GetDirectoryName") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
            LLVMValueRef path_val = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef len = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), g->fn_strlen, &path_val, 1, "plen");
            LLVMValueRef bsz = zan_add(g->builder, len, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = emit_string_alloc_rc(g, bsz);
            LLVMValueRef cpy_args[] = { buf, path_val };
            zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcpy, cpy_args, 2, "");
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef slash_args[] = { buf, LLVMConstInt(i32t, '/', 0) };
            LLVMValueRef slash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, slash_args, 2, "sl");
            LLVMValueRef bslash_args[] = { buf, LLVMConstInt(i32t, 92, 0) };
            LLVMValueRef bslash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, bslash_args, 2, "bsl");
            LLVMValueRef sl_null = zan_icmp(g->builder, LLVMIntEQ, slash, LLVMConstNull(i8ptr), "snul");
            LLVMValueRef pick = LLVMBuildSelect(g->builder, sl_null, bslash, slash, "pk1");
            LLVMValueRef bs_null = zan_icmp(g->builder, LLVMIntEQ, bslash, LLVMConstNull(i8ptr), "bnul");
            LLVMValueRef sep_ptr = LLVMBuildSelect(g->builder, bs_null, pick,
                LLVMBuildSelect(g->builder,
                    zan_icmp(g->builder, LLVMIntUGT, bslash, pick, "bgt"),
                    bslash, pick, "pk2"), "sep");
            LLVMValueRef found = zan_icmp(g->builder, LLVMIntNE, sep_ptr, LLVMConstNull(i8ptr), "fnd");
            LLVMBasicBlockRef trunc_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dn.trunc");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dn.end");
            LLVMBuildCondBr(g->builder, found, trunc_bb, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, trunc_bb);
            LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), sep_ptr);
            LLVMBuildBr(g->builder, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_val, locals);
            return buf;
        }

        /* Path.HasExtension(path) -> bool */
        if (!zan_type_defines(g, "Path", "HasExtension") &&
            is_call_to(expr, "Path", "HasExtension") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef path_val = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef dot_args[] = { path_val, LLVMConstInt(i32t, '.', 0) };
            LLVMValueRef dot = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, dot_args, 2, "dot");
            LLVMValueRef has = zan_icmp(g->builder, LLVMIntNE, dot, LLVMConstNull(i8ptr), "hasext");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_val, locals);
            return LLVMBuildZExt(g->builder, has, i64, "he");
        }

        /* Path.GetTempPath() -> string */
        if (!zan_type_defines(g, "Path", "GetTempPath") &&
            is_call_to(expr, "Path", "GetTempPath") && expr->call.args.count == 0) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 260, 0));
            if (g->target_is_windows) {
                LLVMValueRef gtp_fn = LLVMGetNamedFunction(g->mod, "GetTempPathA");
                if (!gtp_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i32, i8ptr }, 2, 0);
                    gtp_fn = LLVMAddFunction(g->mod, "GetTempPathA", ft);
                }
                LLVMValueRef gtp_args[] = { LLVMConstInt(i32, 260, 0), buf };
                zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i32, i8ptr }, 2, 0),
                    gtp_fn, gtp_args, 2, "");
            } else {
                LLVMValueRef getenv_fn = LLVMGetNamedFunction(g->mod, "getenv");
                if (!getenv_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    getenv_fn = LLVMAddFunction(g->mod, "getenv", ft);
                }
                LLVMValueRef key = zan_irgen_intern_string(g, "TMPDIR");
                LLVMValueRef env = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    getenv_fn, &key, 1, "tmpenv");
                LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, env, LLVMConstNull(i8ptr), "tnull");
                LLVMValueRef deflt = zan_irgen_intern_string(g, "/tmp/");
                LLVMValueRef src = LLVMBuildSelect(g->builder, isnull, deflt, env, "tmpsrc");
                LLVMValueRef cpy_args[] = { buf, src };
                zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                    g->fn_strcpy, cpy_args, 2, "");
            }
            return buf;
        }

        /* Path.GetFileNameWithoutExtension(path) -> string */
        if (!zan_type_defines(g, "Path", "GetFileNameWithoutExtension") &&
            is_call_to(expr, "Path", "GetFileNameWithoutExtension") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef path_val = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef slash_args[] = { path_val, LLVMConstInt(i32t, '/', 0) };
            LLVMValueRef slash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, slash_args, 2, "sl");
            LLVMValueRef bslash_args[] = { path_val, LLVMConstInt(i32t, 92, 0) };
            LLVMValueRef bslash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, bslash_args, 2, "bsl");
            LLVMValueRef sl_null = zan_icmp(g->builder, LLVMIntEQ, slash, LLVMConstNull(i8ptr), "snul");
            LLVMValueRef bs_null = zan_icmp(g->builder, LLVMIntEQ, bslash, LLVMConstNull(i8ptr), "bnul");
            LLVMValueRef best = LLVMBuildSelect(g->builder, sl_null, bslash,
                LLVMBuildSelect(g->builder, bs_null, slash,
                    LLVMBuildSelect(g->builder, zan_icmp(g->builder, LLVMIntUGT, bslash, slash, "bgt"),
                        bslash, slash, "mx"), "pk"), "sep");
            LLVMValueRef has_sep = zan_icmp(g->builder, LLVMIntNE, best, LLVMConstNull(i8ptr), "hs");
            LLVMValueRef after = LLVMBuildGEP2(g->builder, i8, best, &(LLVMValueRef){LLVMConstInt(i64, 1, 0)}, 1, "aft");
            LLVMValueRef fname = LLVMBuildSelect(g->builder, has_sep, after, path_val, "fn");
            LLVMValueRef flen = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), g->fn_strlen, &fname, 1, "flen");
            LLVMValueRef bsz = zan_add(g->builder, flen, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = emit_string_alloc_rc(g, bsz);
            LLVMValueRef cpy_args[] = { buf, fname };
            zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcpy, cpy_args, 2, "");
            LLVMValueRef dot_args[] = { buf, LLVMConstInt(i32t, '.', 0) };
            LLVMValueRef dot = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, dot_args, 2, "dot");
            LLVMValueRef has_dot = zan_icmp(g->builder, LLVMIntNE, dot, LLVMConstNull(i8ptr), "hd");
            LLVMBasicBlockRef trunc_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "fnwe.trunc");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "fnwe.end");
            LLVMBuildCondBr(g->builder, has_dot, trunc_bb, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, trunc_bb);
            LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), dot);
            LLVMBuildBr(g->builder, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            return buf;
        }

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
                    LLVMValueRef new_cap = zan_mul(g->builder, cap, LLVMConstInt(i64, 2, 0), "ncap");
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
                    LLVMValueRef new_cap = zan_mul(g->builder, cap, LLVMConstInt(i64, 2, 0), "nc");
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
