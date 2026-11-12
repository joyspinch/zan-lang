/* FRAGMENT（拼接片段，非独立 TU）：由 irgen_async.c composite dispatcher 拼入。
 * 域：pending 栈（push/top/pop/discard/take_return）与 complete/epilogue、
 * 孤儿结果释放。
 * 文件级 static（async_pending_record/emit_async_orphan_result_release）
 * 为本片段私有。 */
LLVMValueRef async_pending_count_ptr(zan_irgen_t *g, LLVMValueRef frame,
                                            LLVMTypeRef ft) {
    return LLVMBuildStructGEP2(g->builder, ft, frame,
                              ASYNC_FRAME_PENDING_COUNT, "pending.count.p");
}

static LLVMValueRef async_pending_record(zan_irgen_t *g, LLVMValueRef frame,
                                         LLVMTypeRef ft, LLVMValueRef index) {
    LLVMTypeRef array = LLVMStructGetTypeAtIndex(ft, ASYNC_FRAME_PENDING);
    LLVMValueRef indices[] = { LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0),
                               index };
    return LLVMBuildGEP2(g->builder, array,
        LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_PENDING, "pending.p"),
        indices, 2, "pending.record");
}
LLVMValueRef async_pending_field(zan_irgen_t *g, LLVMTypeRef ft,
                                        LLVMValueRef record, unsigned field) {
    LLVMTypeRef rt = LLVMGetElementType(
        LLVMStructGetTypeAtIndex(ft, ASYNC_FRAME_PENDING));
    return LLVMBuildStructGEP2(g->builder, rt, record, field, "pending.field");
}
LLVMValueRef async_pending_discard_fn(zan_irgen_t *g, LLVMTypeRef ft) {
    char name[640];
    snprintf(name, sizeof(name), "%s$pending.discard", LLVMGetStructName(ft));
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, name);
    if (!fn) {
        LLVMTypeRef args[] = { LLVMPointerType(ft, 0), LLVMInt32TypeInContext(g->ctx) };
        fn = LLVMAddFunction(g->mod, name,
            LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), args, 2, 0));
        LLVMSetLinkage(fn, LLVMInternalLinkage);
    }
    return fn;
}
void emit_async_pending_discard(zan_irgen_t *g, LLVMValueRef frame,
                                       LLVMTypeRef ft, LLVMValueRef depth) {
    if (!LLVMGetArrayLength(LLVMStructGetTypeAtIndex(ft, ASYNC_FRAME_PENDING))) return;
    LLVMValueRef fn = async_pending_discard_fn(g, ft);
    LLVMValueRef args[] = { frame, depth };
    zan_call2(g->builder, LLVMGlobalGetValueType(fn), fn, args, 2, "");
}

/* 待定引用所有权账本：在移动或作用域丢弃前持有引用，catch 处清理中断分支未决引用 */
LLVMValueRef emit_async_pending_push(zan_irgen_t *g, LLVMValueRef value,
                                            LLVMValueRef tid, LLVMValueRef kind) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMValueRef cp = async_pending_count_ptr(g, g->current_async_frame, ft);
    LLVMValueRef count = LLVMBuildLoad2(g->builder, i32, cp, "pending.count");
    unsigned capacity = LLVMGetArrayLength(LLVMStructGetTypeAtIndex(ft, ASYNC_FRAME_PENDING));
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef valid = LLVMAppendBasicBlockInContext(g->ctx, fn, "pending.valid");
    LLVMBasicBlockRef overflow = LLVMAppendBasicBlockInContext(g->ctx, fn, "pending.overflow");
    LLVMBuildCondBr(g->builder, LLVMBuildICmp(g->builder, LLVMIntULT, count,
        LLVMConstInt(i32, capacity, 0), "pending.fits"), valid, overflow);
    LLVMPositionBuilderAtEnd(g->builder, overflow);
    LLVMTypeRef abort_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    zan_call2(g->builder, abort_ty, get_libc_fn(g, "abort", abort_ty), NULL, 0, "");
    LLVMBuildUnreachable(g->builder);
    LLVMPositionBuilderAtEnd(g->builder, valid);
    LLVMValueRef record = async_pending_record(g, g->current_async_frame, ft, count);
    if (value) LLVMBuildStore(g->builder, value, async_pending_field(g, ft, record, 0));
    LLVMBuildStore(g->builder, tid, async_pending_field(g, ft, record, 1));
    LLVMBuildStore(g->builder, kind, async_pending_field(g, ft, record, 2));
    LLVMBuildStore(g->builder, LLVMBuildAdd(g->builder, count,
        LLVMConstInt(i32, 1, 0), "pending.next"), cp);
    return record;
}
LLVMValueRef emit_async_pending_top(zan_irgen_t *g) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef count = LLVMBuildLoad2(g->builder, i32,
        async_pending_count_ptr(g, g->current_async_frame,
                                g->current_async_frame_type), "pending.count");
    return async_pending_record(g, g->current_async_frame, g->current_async_frame_type,
        LLVMBuildSub(g->builder, count, LLVMConstInt(i32, 1, 0), "pending.last"));
}
void emit_async_pending_pop(zan_irgen_t *g, LLVMValueRef record) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0),
                   async_pending_field(g, ft, record, 2));
    LLVMValueRef cp = async_pending_count_ptr(g, g->current_async_frame, ft);
    LLVMBuildStore(g->builder, LLVMBuildSub(g->builder,
        LLVMBuildLoad2(g->builder, i32, cp, "pending.count"),
        LLVMConstInt(i32, 1, 0), "pending.previous"), cp);
}
void emit_async_pending_discard_preserving(zan_irgen_t *g,
                                                 LLVMValueRef depth) {
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMTypeRef record_ty = LLVMGetElementType(
        LLVMStructGetTypeAtIndex(ft, ASYNC_FRAME_PENDING));
    LLVMValueRef record = emit_async_pending_top(g);
    LLVMValueRef selected = LLVMBuildLoad2(g->builder, record_ty, record,
                                          "pending.selected");
    emit_async_pending_pop(g, record);
    emit_async_pending_discard(g, g->current_async_frame, ft, depth);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef cp = async_pending_count_ptr(g, g->current_async_frame, ft);
    LLVMValueRef count = LLVMBuildLoad2(g->builder, i32, cp, "pending.kept");
    LLVMBuildStore(g->builder, selected,
        async_pending_record(g, g->current_async_frame, ft, count));
    LLVMBuildStore(g->builder, LLVMBuildAdd(g->builder, count,
        LLVMConstInt(i32, 1, 0), "pending.restored"), cp);
}
void emit_async_pending_return(zan_irgen_t *g, LLVMValueRef value) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    zan_type_t *ty = g->current_async_ret_type;
    LLVMTypeRef rt = ty ? map_type(g, ty) : NULL;
    bool aggregate = rt && LLVMGetTypeKind(rt) == LLVMStructTypeKind;
    LLVMValueRef encoded = aggregate ? NULL : coerce_to_frame_result(g, value, ty);
    LLVMValueRef record = emit_async_pending_push(g, encoded,
        LLVMConstNull(i8ptr), LLVMConstInt(i32, ASYNC_PENDING_RETURN, 0));
    if (aggregate) {
        LLVMValueRef payload = async_pending_field(g, g->current_async_frame_type,
                                                  record, 3);
        LLVMBuildStore(g->builder, value, payload);
    }
}
LLVMValueRef emit_async_pending_take_return(zan_irgen_t *g) {
    LLVMValueRef record = emit_async_pending_top(g);
    LLVMTypeRef ft = g->current_async_frame_type;
    zan_type_t *ty = g->current_async_ret_type;
    LLVMTypeRef rt = ty ? map_type(g, ty) : NULL;
    LLVMValueRef result;
    if (rt && LLVMGetTypeKind(rt) == LLVMStructTypeKind) {
        result = coerce_to_frame_result(g, LLVMBuildLoad2(g->builder, rt,
            async_pending_field(g, ft, record, 3), "pending.return.agg"), ty);
    } else {
        result = LLVMBuildLoad2(g->builder, LLVMInt64TypeInContext(g->ctx),
            async_pending_field(g, ft, record, 0), "pending.return");
    }
    emit_async_pending_pop(g, record);
    return result;
}

/* 单个帧类型的统一丢弃循环，供 catch、正常完成与协程放弃共享调用 */
void emit_async_pending_discard_body(zan_irgen_t *g, LLVMTypeRef ft,
                                            zan_type_t *ret_type) {
    LLVMValueRef fn = async_pending_discard_fn(g, ft);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;
    LLVMValueRef frame = LLVMGetParam(fn, 0), depth = LLVMGetParam(fn, 1);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(g->ctx, fn, "pending.head");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "pending.drop");
    LLVMBasicBlockRef exc = LLVMAppendBasicBlockInContext(g->ctx, fn, "pending.exc");
    LLVMBasicBlockRef ret = LLVMAppendBasicBlockInContext(g->ctx, fn, "pending.ret");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "pending.done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef cp = async_pending_count_ptr(g, frame, ft);
    LLVMBuildBr(g->builder, head);
    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef count = LLVMBuildLoad2(g->builder, i32, cp, "pending.count");
    LLVMBuildCondBr(g->builder, LLVMBuildICmp(g->builder, LLVMIntUGT,
        count, depth, "pending.more"), body, done);
    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef index = LLVMBuildSub(g->builder, count, LLVMConstInt(i32, 1, 0),
                                     "pending.last");
    LLVMBuildStore(g->builder, index, cp);
    LLVMValueRef record = async_pending_record(g, frame, ft, index);
    LLVMValueRef kp = async_pending_field(g, ft, record, 2);
    LLVMValueRef kind = LLVMBuildLoad2(g->builder, i32, kp, "pending.kind");
    LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0), kp);
    LLVMValueRef sw = LLVMBuildSwitch(g->builder, kind, head, 2);
    LLVMAddCase(sw, LLVMConstInt(i32, ASYNC_PENDING_EXCEPTION, 0), exc);
    LLVMAddCase(sw, LLVMConstInt(i32, ASYNC_PENDING_RETURN, 0), ret);
    LLVMPositionBuilderAtEnd(g->builder, exc);
    LLVMValueRef ev = LLVMBuildIntToPtr(g->builder,
        LLVMBuildLoad2(g->builder, i64, async_pending_field(g, ft, record, 0),
                       "pending.exc.bits"), i8ptr, "pending.exception");
    zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
              g->rt_release_dyn, &ev, 1, "");
    LLVMBuildBr(g->builder, head);
    LLVMPositionBuilderAtEnd(g->builder, ret);
    if (ret_type && type_contains_collection_rc(g, ret_type, 0)) {
        LLVMTypeRef rt = map_type(g, ret_type);
        LLVMValueRef value;
        if (LLVMGetTypeKind(rt) == LLVMStructTypeKind) {
            value = LLVMBuildLoad2(g->builder, rt,
                async_pending_field(g, ft, record, 3), "pending.return.agg");
        } else {
            value = coerce_from_frame_result(g, LLVMBuildLoad2(g->builder, i64,
                async_pending_field(g, ft, record, 0), "pending.return.bits"), ret_type);
        }
        emit_collection_value_release(g, ret_type, value, 0);
    }
    LLVMBuildBr(g->builder, head);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildRetVoid(g->builder);
    g->current_fn = saved_fn;
}

/* 汇合 return、取消或未捕获异常至统一步骤：完成清理与帧释放 */
void emit_async_complete(zan_irgen_t *g, local_scope_t *locals,
                                LLVMValueRef result_i64) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (!g->current_async_complete_bb) {
        LLVMBasicBlockRef here = LLVMGetInsertBlock(g->builder);
        g->current_async_complete_bb = LLVMAppendBasicBlockInContext(g->ctx,
            g->current_async_resume_fn, "co.complete");
        LLVMPositionBuilderAtEnd(g->builder, g->current_async_complete_bb);
        g->current_async_result_phi = LLVMBuildPhi(g->builder, i64, "co.result");
        LLVMPositionBuilderAtEnd(g->builder, here);
    }
    emit_release_owned_locals_range(g, locals, g->current_async_frame_local_count);
    emit_clear_owned_locals_range(g, locals, g->current_async_frame_local_count);
    LLVMValueRef value = result_i64 ? result_i64 : LLVMConstInt(i64, 0, 0);
    LLVMBasicBlockRef from = LLVMGetInsertBlock(g->builder);
    LLVMBuildBr(g->builder, g->current_async_complete_bb);
    LLVMAddIncoming(g->current_async_result_phi, &value, &from, 1);
}

/* fire-and-forget（自等待者）帧终结时释放无人认领的结果载荷（B-ID64）：
 * Task.Spawn 带结果协程的返回值在 return 处 retain 后无人读取，帧被 reap
 * 时载荷滞留即孤儿泄漏（探针 BID64_result_orphan_probe 实证 3 对象）。
 * 自等待者惯例（AWAITER=自身，emit_detach_async_call keep_result=false）
 * 是"结果永不经帧读取"的编译期可判形态：完成尾声判 AWAITER==自身时按
 * 返回类型释放 RVAL 载荷并清槽。句柄/根驱动帧（.Result 延迟读）与真
 * awaiter 帧不受影响——前者的释放点仍归 reap/.Result 读取方。 */
static void emit_async_orphan_result_release(zan_irgen_t *g) {
    zan_type_t *rt = g->current_async_ret_type;
    if (!rt || rt->kind == TYPE_VOID || g->current_async_rval_slot < 0 ||
        !g->current_async_frame || !g->current_async_frame_type) return;
    LLVMTypeRef ft = g->current_async_frame_type;
    unsigned idx = (unsigned)g->current_async_rval_slot;
    LLVMTypeRef lt = LLVMStructGetTypeAtIndex(ft, idx);
    LLVMTypeKind lk = LLVMGetTypeKind(lt);
    if (lk != LLVMPointerTypeKind && lk != LLVMStructTypeKind) return;
    LLVMValueRef p = LLVMBuildStructGEP2(g->builder, ft, g->current_async_frame,
                                         idx, "orphan.rval.p");
    LLVMValueRef v = LLVMBuildLoad2(g->builder, lt, p, "orphan.rval");
    LLVMBuildStore(g->builder, LLVMConstNull(lt), p);
    /* RVAL is also scratch storage while a return is pending. Cancellation
     * discards that pending owner and completes with RESULT=0; the stale
     * RVAL bits must not be released again by the self-reaper path. RESULT
     * is authoritative about whether completion transferred a value. */
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef result = LLVMBuildLoad2(g->builder, i64,
        get_async_result_ptr(g), "orphan.result");
    LLVMValueRef fn = g->current_fn;
    LLVMBasicBlockRef release_bb = LLVMAppendBasicBlockInContext(g->ctx, fn,
                                                               "orphan.release");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fn,
                                                            "orphan.done");
    LLVMBuildCondBr(g->builder, zan_icmp(g->builder, LLVMIntNE, result,
        LLVMConstInt(i64, 0, 0), "orphan.has.result"), release_bb, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, release_bb);
    if (lk == LLVMPointerTypeKind) {
        v = LLVMBuildIntToPtr(g->builder, result, lt, "orphan.result.value");
        if (is_rc_managed_type(rt) || rt->kind == TYPE_OBJECT) {
            emit_rc_release_for_type(g, rt, v);
        }
    } else if (type_contains_collection_rc(g, rt, 0)) {
        emit_collection_value_release(g, rt, v, 0);
    }
    LLVMBuildBr(g->builder, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, done_bb);
}

/* 发射共享完成尾声：存入结果、标记完成、释放持有者、唤醒等待者并 ret void */
void emit_async_complete_epilogue(zan_irgen_t *g, local_scope_t *locals) {
    if (!g->current_async_complete_bb) return;
    LLVMPositionBuilderAtEnd(g->builder, g->current_async_complete_bb);
    LLVMValueRef result_i64 = g->current_async_result_phi;
    LLVMValueRef frame = g->current_async_frame;
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);

    /* 协程已完成：取消关联该帧的所有未决 Task.Delay 定时器 */
    LLVMValueRef self_i8 = get_async_self_i8(g);
    emit_co_cancel_delay(g, self_i8);

    LLVMValueRef res_ptr = get_async_result_ptr(g);
    LLVMBuildStore(g->builder, result_i64 ? result_i64 : LLVMConstInt(i64, 0, 0),
        res_ptr);

    LLVMValueRef done_ptr = LLVMBuildStructGEP2(g->builder, ft, frame,
        ASYNC_FRAME_DONE, "fr.done");
    /* 原子 release exchange 发布 DONE 标志，保证等待者 acquire 时能观测到结果写入 */
    LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpXchg, done_ptr,
        LLVMConstInt(i32, 1, 0), LLVMAtomicOrderingRelease, 0);
    LLVMBuildStore(g->builder, LLVMConstInt(i32, -1, 1), get_async_state_ptr(g));
    /* 协程完成时已无活跃异常处理器，重置展开器处理器计数 */
    LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0),
        LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_HCOUNT, "fr.hc"));

    /* 发布 DONE 后的事件级汇合通知（支持 Task.WhenAll / WhenAny） */
    {
        LLVMTypeRef jc_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8ptr }, 1, 0);
        LLVMValueRef jc = LLVMGetNamedFunction(g->mod, "zan_join_complete");
        if (!jc) jc = LLVMAddFunction(g->mod, "zan_join_complete", jc_type);
        LLVMValueRef jc_args[] = { self_i8 };
        zan_call2(g->builder, jc_type, jc, jc_args, 1, "");
    }

    emit_async_pending_discard(g, frame, ft, LLVMConstInt(i32, 0, 0));
    local_scope_t frame_locals = *locals;
    frame_locals.count = g->current_async_frame_local_count;
    emit_release_owned_locals(g, &frame_locals);
    /* 帧可能因 Result 读取或展开暂时存活，已释放字段清零避免悬垂引用 */
    emit_clear_owned_locals_range(g, &frame_locals, 0);
    /* 平衡启动时的 this 引用保持：协程运行期间持有 this 的 +1 引用 */
    if (g->current_async_this_owned && g->current_this &&
        g->current_async_this_type) {
        LLVMTypeRef tty = LLVMGetAllocatedType(g->current_this);
        emit_rc_release_for_type(g, g->current_async_this_type,
            LLVMBuildLoad2(g->builder, tty, g->current_this, "this.rel"));
        LLVMBuildStore(g->builder, LLVMConstNull(tty), g->current_this);
    }
    emit_async_eh_unarm(g);

    /* 通过原子交换哨兵值 (1) 安全唤醒已注册的等待协程 */
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
    /* B-ID64 终结自动析构：自等待者（AWAITER==自身）即 fire-and-forget 帧，
     * 结果载荷无人认领，唤醒 reap 前先按类型释放 RVAL。 */
    LLVMValueRef self_int = LLVMBuildPtrToInt(g->builder, self_i8, ptr_int_ty,
                                              "orphan.self.int");
    LLVMValueRef is_orphan = zan_icmp(g->builder, LLVMIntEQ, old_aw, self_int,
                                      "orphan.selfaw");
    LLVMValueRef fn = g->current_fn;
    LLVMBasicBlockRef orphan_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.orphan");
    LLVMBasicBlockRef chk_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.awchk");
    LLVMBasicBlockRef wake_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.wake");
    LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.ret");
    LLVMBuildCondBr(g->builder, is_orphan, orphan_bb, chk_bb);

    LLVMPositionBuilderAtEnd(g->builder, orphan_bb);
    emit_async_orphan_result_release(g);
    LLVMBuildBr(g->builder, wake_bb);

    LLVMPositionBuilderAtEnd(g->builder, chk_bb);
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
}

/* 声明活跃协程帧注册表接口（zan_co_live_add/del/has） */
