/* FRAGMENT（拼接片段，非独立 TU）：由 irgen_async.c composite dispatcher 拼入。
 * 域：co 运行时 thunk（live/track/untrack/cancel/isdone 助手）与异步
 * 异常处理——resume 蹦床 prologue、exc epilogue、rethrow 块、子任务异常
 * 检查与未捕获类异常打印。
 * 文件级 static（get_co_live_fn/emit_co_live_has/open_co_helper/
 * close_co_helper/emit_eh_rethrow_current/get_async_rethrow_bb/
 * get_async_sub_rethrow_bb）为本片段私有。 */
static LLVMValueRef get_co_live_fn(zan_irgen_t *g, const char *name,
                                   bool returns_i32, LLVMTypeRef *out_ty) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ret = returns_i32 ? LLVMInt32TypeInContext(g->ctx)
                                  : LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef ty = LLVMFunctionType(ret, &i8ptr, 1, 0);
    if (out_ty) *out_ty = ty;
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, name);
    if (!fn) fn = LLVMAddFunction(g->mod, name, ty);
    return fn;
}

static LLVMValueRef emit_co_live_has(zan_irgen_t *g, LLVMValueRef frame) {
    LLVMTypeRef ty;
    LLVMValueRef fn = get_co_live_fn(g, "zan_co_live_has", true, &ty);
    return zan_call2(g->builder, ty, fn, &frame, 1, "co.live");
}

/* 发射 zan_timer_cancel_delay(frame)，从定时器堆中移除该帧的未决延迟任务 */
void emit_co_cancel_delay(zan_irgen_t *g, LLVMValueRef frame) {
    /* 运行时定义返回取消数量 i32，为兼容 wasm32 链接签名必须严格匹配 */
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ty = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), &i8ptr, 1, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_timer_cancel_delay");
    if (!fn) fn = LLVMAddFunction(g->mod, "zan_timer_cancel_delay", ty);
    zan_call2(g->builder, ty, fn, &frame, 1, "");
}

/* 创建内部辅助函数 `void f(i8*)` 并将 builder 定位到 entry 块 */
static bool open_co_helper(zan_irgen_t *g, const char *name, LLVMValueRef *fn,
                           zan_builder_point_t *saved_pt) {
    LLVMValueRef existing = LLVMGetNamedFunction(g->mod, name);
    if (existing) { *fn = existing; return false; }
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    *fn = LLVMAddFunction(g->mod, name,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0));
    LLVMSetLinkage(*fn, LLVMInternalLinkage);
    *saved_pt = zan_builder_save(g);
    g->current_fn = *fn;
    LLVMPositionBuilderAtEnd(g->builder,
        LLVMAppendBasicBlockInContext(g->ctx, *fn, "entry"));
    di_clear(g);
    return true;
}

static void close_co_helper(zan_irgen_t *g, zan_builder_point_t saved_pt) {
    zan_builder_restore(g, saved_pt);
}
LLVMValueRef get_co_track_fn(zan_irgen_t *g) {
    LLVMValueRef fn; zan_builder_point_t saved_pt;
    if (!open_co_helper(g, "__zan_co_track", &fn, &saved_pt)) return fn;
    LLVMTypeRef ty;
    LLVMValueRef add = get_co_live_fn(g, "zan_co_live_add", false, &ty);
    LLVMValueRef f = LLVMGetParam(fn, 0);
    zan_call2(g->builder, ty, add, &f, 1, "");
    LLVMBuildRetVoid(g->builder);
    close_co_helper(g, saved_pt);
    return fn;
}
LLVMValueRef get_co_untrack_fn(zan_irgen_t *g) {
    LLVMValueRef fn; zan_builder_point_t saved_pt;
    if (!open_co_helper(g, "__zan_co_untrack", &fn, &saved_pt)) return fn;
    LLVMTypeRef ty;
    LLVMValueRef del = get_co_live_fn(g, "zan_co_live_del", false, &ty);
    LLVMValueRef f = LLVMGetParam(fn, 0);
    zan_call2(g->builder, ty, del, &f, 1, "");
    LLVMBuildRetVoid(g->builder);
    close_co_helper(g, saved_pt);
    return fn;
}

/* 获取或声明模块内部的 `__zan_co_cancel(i8*)`（协作式取消标记置位） */
LLVMValueRef get_co_cancel_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_co_cancel");
    if (fn) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    fn = LLVMAddFunction(g->mod, "__zan_co_cancel",
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0));
    LLVMSetLinkage(fn, LLVMInternalLinkage);

    zan_builder_point_t saved_pt = zan_builder_save(g);
    g->current_fn = fn;
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    di_clear(g);
    LLVMValueRef arg = LLVMGetParam(fn, 0);
    emit_task_runtime_call(g, "zan_task_frame_cancel", LLVMVoidTypeInContext(g->ctx), &arg, 1);
    LLVMBuildRetVoid(g->builder);

    zan_builder_restore(g, saved_pt);
    return fn;
}

/* 获取或声明模块内部的 `__zan_co_isdone(i8*)`（查询协程完成状态） */
/* 帧首 sched 字段的 CO_DEAD 位（rt_io.c 同名宏的镜像，运行时拥有语义） */
#define ZAN_CO_DEAD_BIT 8LL
LLVMValueRef get_co_isdone_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_co_isdone");
    if (fn) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef hdr = g->co_header_type;
    fn = LLVMAddFunction(g->mod, "__zan_co_isdone",
        LLVMFunctionType(i32, &i8ptr, 1, 0));
    LLVMSetLinkage(fn, LLVMInternalLinkage);

    zan_builder_point_t saved_pt = zan_builder_save(g);
    g->current_fn = fn;
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef live_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "id.live");
    LLVMBasicBlockRef gone_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "id.gone");
    LLVMBasicBlockRef alive_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "id.alive");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    di_clear(g);
    LLVMValueRef arg = LLVMGetParam(fn, 0);
    LLVMValueRef live = emit_co_live_has(g, arg);
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntNE, live, LLVMConstInt(i32, 0, 0), "id.islive"),
        live_bb, gone_bb);

    LLVMPositionBuilderAtEnd(g->builder, live_bb);
    /* A552：mt 驱动下查完活表帧就可能被释放（检疫区里帧首 sched=DEAD 仍可
     * 读）——先看守卫位，DEAD 视为已完成，跳过对已释放帧 DONE 字段的读；
     * 默认驱动 sched 恒 0，恒走 alive 分支不受影响。 */
    LLVMValueRef sv = LLVMBuildLoad2(g->builder, i64,
        LLVMBuildStructGEP2(g->builder, hdr, arg, ASYNC_FRAME_SCHED, "id.sched.p"),
        "id.sched");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntNE,
            LLVMBuildAnd(g->builder, sv, LLVMConstInt(i64, ZAN_CO_DEAD_BIT, 0), "id.dead"),
            LLVMConstInt(i64, 0, 0), "id.isdead"),
        gone_bb, alive_bb);

    LLVMPositionBuilderAtEnd(g->builder, alive_bb);
    LLVMValueRef dp = LLVMBuildStructGEP2(g->builder, hdr, arg, ASYNC_FRAME_DONE, "id.dp");
    LLVMValueRef done = LLVMBuildLoad2(g->builder, i32, dp, "id.done");
    LLVMSetOrdering(done, LLVMAtomicOrderingAcquire);
    LLVMBuildRet(g->builder, LLVMBuildZExt(g->builder,
        zan_icmp(g->builder, LLVMIntNE, done, LLVMConstInt(i32, 0, 0), "id.isdone"),
        i32, "id.r"));

    LLVMPositionBuilderAtEnd(g->builder, gone_bb);
    LLVMBuildRet(g->builder, LLVMConstInt(i32, 1, 0));

    zan_builder_restore(g, saved_pt);
    return fn;
}

void emit_eh_hook_call(zan_irgen_t *g, const char *name);

/* 异步异常传播：未捕获异常存放于帧 exc_val 槽供 awaiter 唤醒时重抛 */

/* resume 入口发射：挂载蹦床与帧内活跃的异常处理器，随后分发至当前 step 块 */
void emit_async_eh_prologue(zan_irgen_t *g) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef fn = g->current_async_resume_fn;
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);

    LLVMValueRef entry_slot = LLVMBuildAlloca(g->builder, i32, "eh.co.entry");
    g->current_async_eh_entry = entry_slot;
    LLVMBuildStore(g->builder, LLVMBuildLoad2(g->builder, i32, top_g, "eh.top0"),
        entry_slot);

    LLVMBasicBlockRef exc_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.exc");
    LLVMBasicBlockRef disp_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.dispatch");
    g->current_async_exc_bb = exc_bb;

    LLVMBasicBlockRef lpad_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.lpad");
    zan_builder_point_t lpad_pt = zan_builder_save(g);
    LLVMPositionBuilderAtEnd(g->builder, lpad_bb);
    ensure_function_personality(g, fn);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef lp_ret_ty = LLVMStructTypeInContext(g->ctx, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0);
    LLVMValueRef lp_val = LLVMBuildLandingPad(g->builder, lp_ret_ty, get_eh_personality_fn(g), 1, "lp");
    LLVMAddClause(lp_val, LLVMConstNull(i8ptr));
    LLVMSetCleanup(lp_val, 1);
    LLVMBuildBr(g->builder, exc_bb);
    zan_builder_restore(g, lpad_pt);

    if (g->lpad_depth < ZAN_MAX_ARMED_TRY) {
        g->lpad_stack[g->lpad_depth].lpad_bb = lpad_bb;
        g->lpad_stack[g->lpad_depth].catch_bb = exc_bb;
        g->lpad_stack[g->lpad_depth].fn = fn;
        g->lpad_stack[g->lpad_depth].throw_locals_base = 0;
        /* The coroutine root is never a departing lexical try/finally. */
        g->lpad_stack[g->lpad_depth].fin_idx = -1;
        g->lpad_stack[g->lpad_depth].finally_depth = 0;
        g->lpad_depth++;
    }

    LLVMBuildBr(g->builder, disp_bb);
    LLVMPositionBuilderAtEnd(g->builder, disp_bb);
}

/* 蹦床着陆块：将未捕获异常存入帧并完成协程，供 awaiter 重新抛出 */
void emit_async_exc_epilogue(zan_irgen_t *g, local_scope_t *locals) {
    if (!g->current_async_exc_bb) return;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef frame = g->current_async_frame;
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);

    LLVMPositionBuilderAtEnd(g->builder, g->current_async_exc_bb);
    /* 未捕获异常时释放本次 resume 期间分配的所有临时变量 */
    if (g->current_async_eh_entry) {
        LLVMValueRef e0 = LLVMBuildLoad2(g->builder, i32,
            g->current_async_eh_entry, "eh.exc.entry");
        emit_eh_unwind_to_handler(g,
            zan_add(g->builder, e0, LLVMConstInt(i32, 1, 0), "eh.exc.tr"));
    }
    /* 协程完成时读取帧字段完成最终清理 */
    LLVMBuildStore(g->builder, LLVMBuildLoad2(g->builder, i8ptr, exc_g, "exc.v"),
        LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_EXC, "fr.exc"));
    LLVMBuildStore(g->builder,
        LLVMBuildLoad2(g->builder, i8ptr, get_eh_exc_tid_global(g), "exc.tid"),
        LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_EXC_TID, "fr.exc.tid"));
    LLVMBuildStore(g->builder,
        LLVMBuildLoad2(g->builder, i32, get_eh_exc_owned_global(g), "exc.own"),
        LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_EXC_OWNED, "fr.exc.own"));
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), exc_g);
    LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0), get_eh_exc_owned_global(g));
    /* B-ID79：结构化并发父子联动——父协程异常退出时，级联取消当前正挂起等待的子协程链路，
     * 避免子协程沦为无主孤儿在后台空转。 */
    LLVMValueRef child_ptr = get_async_child_ptr(g);
    if (child_ptr) {
        LLVMValueRef child = LLVMBuildLoad2(g->builder, i8ptr, child_ptr, "exc.child");
        LLVMValueRef cf = get_co_cancel_fn(g);
        zan_call2(g->builder, LLVMGlobalGetValueType(cf), cf, &child, 1, "");
    }
    emit_async_complete(g, locals, NULL);
}
void emit_unhandled_class_exc_print(zan_irgen_t *g, LLVMValueRef fn,
                                           LLVMTypeRef printf_ty,
                                           LLVMValueRef printf_fn,
                                           LLVMValueRef cname,
                                           LLVMValueRef exc,
                                           LLVMValueRef tid,
                                           LLVMBasicBlockRef cont_bb) {
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8t, 0);
    LLVMTypeRef i1t = LLVMInt1TypeInContext(g->ctx);

    LLVMBasicBlockRef no_msg_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "uexc.nomsg");

    /* 检查抛出的类是否继承自 Exception（或本身就是 Exception） */
    zan_symbol_t *exc_sym = zan_binder_lookup(g->binder, (zan_istr_t){ (char *)"Exception", 9 });
    if (!exc_sym) exc_sym = zan_binder_lookup(g->binder, (zan_istr_t){ (char *)"System.Exception", 16 });

    if (exc_sym && exc_sym->type && exc_sym->type->kind == TYPE_CLASS && exc && tid) {
        LLVMValueRef match_fn = get_eh_tid_match_fn(g);
        LLVMTypeRef match_ty = LLVMFunctionType(i1t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
        LLVMValueRef want_tid = LLVMBuildBitCast(g->builder,
            get_class_tid_global(g, exc_sym), i8ptr, "uexc.want_tid");
        LLVMValueRef is_exc = zan_call2(g->builder, match_ty, match_fn,
            (LLVMValueRef[]){ tid, want_tid }, 2, "uexc.is_exc");

        LLVMBasicBlockRef chk_msg_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "uexc.chkmsg");
        LLVMBuildCondBr(g->builder, is_exc, chk_msg_bb, no_msg_bb);

        LLVMPositionBuilderAtEnd(g->builder, chk_msg_bb);
        LLVMValueRef obj = LLVMBuildBitCast(g->builder, exc, LLVMPointerType(i8ptr, 0), "uexc.obj");
        LLVMValueRef msg_p = LLVMBuildGEP2(g->builder, i8ptr, obj,
            (LLVMValueRef[]){ LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 1, 0) }, 1, "uexc.msgp");
        LLVMValueRef msg = LLVMBuildLoad2(g->builder, i8ptr, msg_p, "uexc.msg");
        LLVMValueRef has_msg = zan_icmp(g->builder, LLVMIntNE, msg, LLVMConstNull(i8ptr), "uexc.hasmsg");

        LLVMBasicBlockRef with_msg_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "uexc.withmsg");
        LLVMBuildCondBr(g->builder, has_msg, with_msg_bb, no_msg_bb);

        LLVMPositionBuilderAtEnd(g->builder, with_msg_bb);
        LLVMValueRef ch0 = LLVMBuildLoad2(g->builder, i8t, msg, "uexc.ch0");
        LLVMValueRef not_empty = zan_icmp(g->builder, LLVMIntNE, ch0, LLVMConstInt(i8t, 0, 0), "uexc.ne");
        LLVMBasicBlockRef print_msg_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "uexc.pmsg");
        LLVMBuildCondBr(g->builder, not_empty, print_msg_bb, no_msg_bb);

        LLVMPositionBuilderAtEnd(g->builder, print_msg_bb);
        LLVMValueRef fmt_msg = zan_irgen_intern_string(g, "Unhandled exception: %s: %s\n");
        LLVMValueRef args3[3] = { fmt_msg, cname, msg };
        zan_call2(g->builder, printf_ty, printf_fn, args3, 3, "");
        LLVMBuildBr(g->builder, cont_bb);
    } else {
        LLVMBuildBr(g->builder, no_msg_bb);
    }

    LLVMPositionBuilderAtEnd(g->builder, no_msg_bb);
    LLVMValueRef cfmt2 = zan_irgen_intern_string(g, "Unhandled exception: %s\n");
    LLVMValueRef cargs[2] = { cfmt2, cname };
    zan_call2(g->builder, printf_ty, printf_fn, cargs, 2, "");
    LLVMBuildBr(g->builder, cont_bb);
}

/* 重抛全局异常：通过 LandingPad 零开销异常系统沿调用栈或协程帧展开 */
static void emit_eh_rethrow_current(zan_irgen_t *g) {
    emit_raise_exception(g);
}

/* 单次 resume 调用内子协程异常传播的共享重抛块 */
static LLVMBasicBlockRef get_async_rethrow_bb(zan_irgen_t *g) {
    if (!g->current_async_resume_fn) return NULL;
    if (g->current_async_rethrow_bb) return g->current_async_rethrow_bb;
    LLVMBasicBlockRef here = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->current_async_resume_fn, "co.rethrow");
    LLVMPositionBuilderAtEnd(g->builder, bb);
    emit_eh_rethrow_current(g);
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
        LLVMBuildUnreachable(g->builder);
    if (here) LLVMPositionBuilderAtEnd(g->builder, here);
    g->current_async_rethrow_bb = bb;
    return bb;
}

/* 子协程异常传递共享块：转移异常至当前帧并重抛 */
static LLVMBasicBlockRef get_async_sub_rethrow_bb(zan_irgen_t *g) {
    if (!g->current_async_resume_fn) return NULL;
    if (g->current_async_sub_rethrow_bb) return g->current_async_sub_rethrow_bb;

    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef hdr = g->co_header_type;
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);

    LLVMBasicBlockRef here = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->current_async_resume_fn, "co.sub_rethrow");
    LLVMPositionBuilderAtEnd(g->builder, bb);

    LLVMValueRef phi_sub = LLVMBuildPhi(g->builder, i8ptr, "sub.phi");
    LLVMValueRef phi_ev  = LLVMBuildPhi(g->builder, i8ptr, "ev.phi");
    g->current_async_sub_rethrow_phi_sub = phi_sub;
    g->current_async_sub_rethrow_phi_ev  = phi_ev;

    LLVMValueRef tp = LLVMBuildStructGEP2(g->builder, hdr, phi_sub, ASYNC_FRAME_EXC_TID, "sub.exc.tid.p");
    LLVMValueRef tv = LLVMBuildLoad2(g->builder, i8ptr, tp, "sub.exc.tid");
    LLVMValueRef op = LLVMBuildStructGEP2(g->builder, hdr, phi_sub, ASYNC_FRAME_EXC_OWNED, "sub.exc.own.p");
    LLVMValueRef ov = LLVMBuildLoad2(g->builder, i32, op, "sub.exc.own");

    LLVMBuildStore(g->builder, phi_ev, exc_g);
    LLVMBuildStore(g->builder, tv, get_eh_exc_tid_global(g));
    LLVMBuildStore(g->builder, ov, get_eh_exc_owned_global(g));
    LLVMValueRef ep = LLVMBuildStructGEP2(g->builder, hdr, phi_sub, ASYNC_FRAME_EXC, "sub.exc.p");
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), ep);
    LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0), op);

    zan_emit_frame_free(g, phi_sub);

    LLVMBasicBlockRef rethrow_bb = get_async_rethrow_bb(g);
    if (rethrow_bb) {
        LLVMBuildBr(g->builder, rethrow_bb);
    } else {
        emit_eh_rethrow_current(g);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
            LLVMBuildUnreachable(g->builder);
    }

    if (here) LLVMPositionBuilderAtEnd(g->builder, here);
    g->current_async_sub_rethrow_bb = bb;
    return bb;
}

/* await 恢复点检查：若被等待子协程异常退出，提取异常并在当前上下文重抛 */
void emit_async_check_sub_exc(zan_irgen_t *g, LLVMValueRef sub,
                                     LLVMValueRef tmp_mark) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef hdr = g->co_header_type;
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));

    LLVMValueRef ep = LLVMBuildStructGEP2(g->builder, hdr, sub, ASYNC_FRAME_EXC, "sub.exc.p");
    LLVMValueRef ev = LLVMBuildLoad2(g->builder, i8ptr, ep, "sub.exc");
    LLVMValueRef threw = zan_icmp(g->builder, LLVMIntNE, ev,
        LLVMConstNull(i8ptr), "sub.threw");

    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "sub.ok");
    LLVMBasicBlockRef thr_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "sub.rethrow");
    LLVMBuildCondBr(g->builder, threw, thr_bb, ok_bb);

    LLVMPositionBuilderAtEnd(g->builder, thr_bb);
    LLVMValueRef tp = LLVMBuildStructGEP2(g->builder, hdr, sub, ASYNC_FRAME_EXC_TID, "sub.exc.tid.p");
    LLVMValueRef tv = LLVMBuildLoad2(g->builder, i8ptr, tp, "sub.exc.tid");
    LLVMValueRef op = LLVMBuildStructGEP2(g->builder, hdr, sub, ASYNC_FRAME_EXC_OWNED, "sub.exc.own.p");
    LLVMValueRef ov = LLVMBuildLoad2(g->builder, i32, op, "sub.exc.own");
    LLVMBuildStore(g->builder, ev, exc_g);
    LLVMBuildStore(g->builder, tv, get_eh_exc_tid_global(g));
    LLVMBuildStore(g->builder, ov, get_eh_exc_owned_global(g));
    if (tmp_mark) {
        LLVMTypeRef uwty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            &i32, 1, 0);
        zan_call2(g->builder, uwty, get_eh_tmp_unwind_fn(g), &tmp_mark, 1, "");
    }
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), ep);
    LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0), op);
    zan_emit_frame_free(g, sub);
    /* Await has not run any lexical finally bodies yet. Enter the
     * innermost landing pad, including finally-only regions. */
    emit_raise_exception_through_cleanups(g);

    LLVMPositionBuilderAtEnd(g->builder, ok_bb);
}
