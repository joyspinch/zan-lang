
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
