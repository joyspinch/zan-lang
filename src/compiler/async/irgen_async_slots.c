/* FRAGMENT（拼接片段，非独立 TU）：由 irgen_async.c composite dispatcher 拼入。
 * 域：帧结果槽零扩展判定与 coerce_to/from_frame_result/async_ret/i64、
 * async_self/state/cancel/child/sub_slot/result 槽访问、cancel_check/
 * eh_unarm/finalize_slots/preempt_site。
 * 文件级 static（type_is_unsigned_scalar/position_before_entry_terminator/
 * scope_has_owned_cleanups）均为本片段私有。 */
static bool type_is_unsigned_scalar(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BOOL:
    case TYPE_BYTE:
    case TYPE_USHORT:
    case TYPE_UINT:
    case TYPE_ULONG:
    case TYPE_CHAR:
        return true;
    default:
        return false;
    }
}

/* 将协程返回值编码写入 64 位帧结果槽，与 coerce_from_frame_result 互为逆操作 */
LLVMValueRef coerce_to_frame_result(zan_irgen_t *g, LLVMValueRef v,
                                           zan_type_t *ty) {
    if (!v) return NULL;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef t = LLVMTypeOf(v);
    /* B-ID64 类型化返回值字段：同步把声明类型的值存入帧尾 RVAL 槽（聚合返回
     * 的权威存储此前即此槽，标量补齐后终结析构按真实类型释放）。类型对不上
     * 时跳过，i64 槽仍承载编码。 */
    if (g->current_async_frame && g->current_async_rval_slot >= 0) {
        LLVMTypeRef rwant = LLVMStructGetTypeAtIndex(g->current_async_frame_type,
            (unsigned)g->current_async_rval_slot);
        if (LLVMTypeOf(v) == rwant) {
            LLVMValueRef rp = LLVMBuildStructGEP2(g->builder,
                g->current_async_frame_type, g->current_async_frame,
                (unsigned)g->current_async_rval_slot, "rval.p");
            LLVMBuildStore(g->builder, v, rp);
        }
    }
    switch (LLVMGetTypeKind(t)) {
    case LLVMIntegerTypeKind: {
        unsigned bits = LLVMGetIntTypeWidth(t);
        if (bits == 64) return v;
        if (bits > 64) return LLVMBuildTrunc(g->builder, v, i64, "res.slot");
        return type_is_unsigned_scalar(ty)
            ? LLVMBuildZExt(g->builder, v, i64, "res.slot")
            : LLVMBuildSExt(g->builder, v, i64, "res.slot");
    }
    case LLVMPointerTypeKind: {
        if (ty) {
            LLVMTypeRef want = map_type(g, ty);
            if (want && LLVMGetTypeKind(want) == LLVMStructTypeKind &&
                g->current_async_frame && g->current_async_rval_slot >= 0) {
                LLVMValueRef slot_ptr = LLVMBuildStructGEP2(g->builder,
                    g->current_async_frame_type, g->current_async_frame,
                    (unsigned)g->current_async_rval_slot, "ret.agg.slot");
                LLVMValueRef loaded = LLVMBuildLoad2(g->builder, want, v, "ret.agg.load");
                LLVMBuildStore(g->builder, loaded, slot_ptr);
                return LLVMBuildPtrToInt(g->builder, slot_ptr, i64, "res.slot");
            }
        }
        return LLVMBuildPtrToInt(g->builder, v, i64, "res.slot");
    }
    case LLVMStructTypeKind: {
        if (g->current_async_frame && g->current_async_rval_slot >= 0) {
            LLVMValueRef slot_ptr = LLVMBuildStructGEP2(g->builder,
                g->current_async_frame_type, g->current_async_frame,
                (unsigned)g->current_async_rval_slot, "ret.agg.slot");
            LLVMBuildStore(g->builder, v, slot_ptr);
            return LLVMBuildPtrToInt(g->builder, slot_ptr, i64, "res.slot");
        }
        return LLVMConstInt(i64, 0, 0);
    }
    case LLVMDoubleTypeKind:
        return LLVMBuildBitCast(g->builder, v, i64, "res.slot");
    case LLVMFloatTypeKind: {
        /* 保持 32 位浮点原始位模式，避免拓宽为 double 导致按 float 位转换时失真 */
        LLVMValueRef fb = LLVMBuildBitCast(g->builder, v, i32, "res.f32");
        return LLVMBuildZExt(g->builder, fb, i64, "res.slot");
    }
    default:
        /* 单字槽无法容纳时的类型兜底转换 */
        return LLVMConstInt(i64, 0, 0);
    }
}

/* 将返回值转换为方法声明的返回类型，确保帧结果编码类型一致 */
LLVMValueRef coerce_async_ret(zan_irgen_t *g, LLVMValueRef val) {
    zan_type_t *rt = g->current_async_ret_type;
    if (!val || !rt || rt->kind == TYPE_VOID) return val;
    LLVMTypeRef want = map_type(g, rt);
    LLVMTypeRef have = LLVMTypeOf(val);
    if (!want || have == want) return val;
    LLVMTypeKind wk = LLVMGetTypeKind(want), hk = LLVMGetTypeKind(have);
    if (wk == LLVMDoubleTypeKind || wk == LLVMFloatTypeKind) {
        if (hk == LLVMIntegerTypeKind)
            return type_is_unsigned_scalar(rt)
                ? LLVMBuildUIToFP(g->builder, val, want, "ret.fp")
                : LLVMBuildSIToFP(g->builder, val, want, "ret.fp");
        if (hk == LLVMDoubleTypeKind)
            return LLVMBuildFPTrunc(g->builder, val, want, "ret.fptrunc");
        if (hk == LLVMFloatTypeKind)
            return LLVMBuildFPExt(g->builder, val, want, "ret.fpext");
        return val;
    }
    if (wk == LLVMIntegerTypeKind && hk == LLVMIntegerTypeKind) {
        unsigned wb = LLVMGetIntTypeWidth(want), hb = LLVMGetIntTypeWidth(have);
        /* bool/byte 无符号类型采用零扩展 */
        if (wb > hb) return hb <= 8
            ? LLVMBuildZExt(g->builder, val, want, "ret.zext")
            : LLVMBuildSExt(g->builder, val, want, "ret.ext");
        if (wb < hb) return LLVMBuildTrunc(g->builder, val, want, "ret.trunc");
        return val;
    }
    if (wk == LLVMPointerTypeKind && hk == LLVMPointerTypeKind)
        return LLVMBuildBitCast(g->builder, val, want, "ret.cast");
    if (wk == LLVMStructTypeKind && hk == LLVMPointerTypeKind)
        return LLVMBuildLoad2(g->builder, want, val, "ret.struct");
    return val;
}

/* 将 64 位帧结果槽中的位解码还原为声明类型 ty */
LLVMValueRef coerce_from_frame_result(zan_irgen_t *g, LLVMValueRef res,
                                             zan_type_t *ty) {
    if (!res || !ty) return res;
    if (ty && ty->type_arg_count > 0) {
        if (ty->kind == TYPE_TASK ||
            (ty->name.str && ((ty->name.len >= 4 && memcmp(ty->name.str, "Task", 4) == 0) ||
                             (ty->name.len >= 9 && memcmp(ty->name.str, "ValueTask", 9) == 0))) ||
            (ty->sym &&
             ((ty->sym->name.len == 4 && memcmp(ty->sym->name.str, "Task", 4) == 0) ||
              (ty->sym->name.len == 9 && memcmp(ty->sym->name.str, "ValueTask", 9) == 0)))) {
            ty = concretize(g, ty->type_args[0]);
        }
    }
    if (LLVMGetTypeKind(LLVMTypeOf(res)) != LLVMIntegerTypeKind) return res;
    LLVMTypeRef want = map_type(g, ty);
    if (!want || LLVMTypeOf(res) == want) return res;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    switch (LLVMGetTypeKind(want)) {
    case LLVMIntegerTypeKind:
        return LLVMGetIntTypeWidth(want) < LLVMGetIntTypeWidth(LLVMTypeOf(res))
            ? LLVMBuildTrunc(g->builder, res, want, "aw.int")
            : res;
    case LLVMPointerTypeKind:
        return LLVMBuildIntToPtr(g->builder, res, want, "aw.ptr");
    case LLVMDoubleTypeKind:
        return LLVMBuildBitCast(g->builder, res, want, "aw.dbl");
    case LLVMFloatTypeKind: {
        LLVMValueRef fb = LLVMBuildTrunc(g->builder, res, i32, "aw.f32");
        return LLVMBuildBitCast(g->builder, fb, want, "aw.flt");
    }
    case LLVMStructTypeKind: {
        LLVMValueRef ptr = LLVMBuildIntToPtr(g->builder, res,
            LLVMPointerType(want, 0), "aw.agg.p");
        return LLVMBuildLoad2(g->builder, want, ptr, "aw.agg");
    }
    default:
        return res;
    }
}

/* 无声明返回类型时标量值到 i64 帧结果槽的强制转换兜底 */
LLVMValueRef coerce_to_i64(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef t = LLVMTypeOf(v);
    switch (LLVMGetTypeKind(t)) {
    case LLVMIntegerTypeKind: {
        unsigned bits = LLVMGetIntTypeWidth(t);
        if (bits < 64) return LLVMBuildSExt(g->builder, v, i64, "res.i64");
        if (bits > 64) return LLVMBuildTrunc(g->builder, v, i64, "res.i64");
        return v;
    }
    case LLVMPointerTypeKind:
        return LLVMBuildPtrToInt(g->builder, v, i64, "res.i64");
    case LLVMDoubleTypeKind:
        return LLVMBuildBitCast(g->builder, v, i64, "res.i64");
    case LLVMFloatTypeKind: {
        LLVMValueRef d = LLVMBuildFPExt(g->builder, v,
            LLVMDoubleTypeInContext(g->ctx), "res.f64");
        return LLVMBuildBitCast(g->builder, d, i64, "res.i64");
    }
    default:
        return LLVMConstInt(i64, 0, 0);
    }
}
static void position_before_entry_terminator(zan_irgen_t *g) {
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(g->current_async_resume_fn);
    if (!entry) return;
    LLVMValueRef term = LLVMGetBasicBlockTerminator(entry);
    if (term) {
        LLVMPositionBuilderBefore(g->builder, term);
    } else {
        LLVMPositionBuilderAtEnd(g->builder, entry);
    }
}
LLVMValueRef get_async_self_i8(zan_irgen_t *g) {
    if (g->current_async_self_i8) return g->current_async_self_i8;
    if (!g->current_async_frame) return NULL;
    zan_builder_point_t pt = zan_builder_save(g);
    position_before_entry_terminator(g);
    LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    g->current_async_self_i8 = LLVMBuildBitCast(g->builder,
        g->current_async_frame, di8ptr, "self.i8");
    zan_builder_restore(g, pt);
    return g->current_async_self_i8;
}
LLVMValueRef get_async_state_ptr(zan_irgen_t *g) {
    if (g->current_async_state_ptr) return g->current_async_state_ptr;
    if (!g->current_async_frame || !g->current_async_frame_type) return NULL;
    zan_builder_point_t pt = zan_builder_save(g);
    position_before_entry_terminator(g);
    g->current_async_state_ptr = LLVMBuildStructGEP2(g->builder,
        g->current_async_frame_type, g->current_async_frame,
        ASYNC_FRAME_STATE, "self.state");
    zan_builder_restore(g, pt);
    return g->current_async_state_ptr;
}
LLVMValueRef get_async_cancel_ptr(zan_irgen_t *g) {
    if (g->current_async_cancel_ptr) return g->current_async_cancel_ptr;
    if (!g->current_async_frame || !g->current_async_frame_type) return NULL;
    zan_builder_point_t pt = zan_builder_save(g);
    position_before_entry_terminator(g);
    g->current_async_cancel_ptr = LLVMBuildStructGEP2(g->builder,
        g->current_async_frame_type, g->current_async_frame,
        ASYNC_FRAME_CANCEL, "fr.cancel.p");
    zan_builder_restore(g, pt);
    return g->current_async_cancel_ptr;
}
LLVMValueRef get_async_self_int(zan_irgen_t *g, LLVMTypeRef ptr_int_ty) {
    if (g->current_async_self_int) return g->current_async_self_int;
    LLVMValueRef self_i8 = get_async_self_i8(g);
    if (!self_i8) return NULL;
    zan_builder_point_t pt = zan_builder_save(g);
    position_before_entry_terminator(g);
    g->current_async_self_int = LLVMBuildPtrToInt(g->builder, self_i8, ptr_int_ty, "self.int");
    zan_builder_restore(g, pt);
    return g->current_async_self_int;
}
LLVMValueRef get_async_child_ptr(zan_irgen_t *g) {
    if (g->current_async_child_ptr) return g->current_async_child_ptr;
    if (!g->current_async_frame || !g->current_async_frame_type) return NULL;
    zan_builder_point_t pt = zan_builder_save(g);
    position_before_entry_terminator(g);
    g->current_async_child_ptr = LLVMBuildStructGEP2(g->builder,
        g->current_async_frame_type, g->current_async_frame,
        ASYNC_FRAME_CHILD, "self.child");
    zan_builder_restore(g, pt);
    return g->current_async_child_ptr;
}
LLVMValueRef get_async_sub_slot_ptr(zan_irgen_t *g) {
    if (g->current_async_sub_slot_ptr) return g->current_async_sub_slot_ptr;
    if (!g->current_async_frame || !g->current_async_frame_type) return NULL;
    zan_builder_point_t pt = zan_builder_save(g);
    position_before_entry_terminator(g);
    g->current_async_sub_slot_ptr = LLVMBuildStructGEP2(g->builder,
        g->current_async_frame_type, g->current_async_frame,
        (unsigned)g->current_async_sub_base, "self.sub_slot");
    zan_builder_restore(g, pt);
    return g->current_async_sub_slot_ptr;
}
LLVMValueRef get_async_result_ptr(zan_irgen_t *g) {
    if (g->current_async_result_ptr) return g->current_async_result_ptr;
    if (!g->current_async_frame || !g->current_async_frame_type) return NULL;
    zan_builder_point_t pt = zan_builder_save(g);
    position_before_entry_terminator(g);
    g->current_async_result_ptr = LLVMBuildStructGEP2(g->builder,
        g->current_async_frame_type, g->current_async_frame,
        ASYNC_FRAME_RESULT, "self.res_slot");
    zan_builder_restore(g, pt);
    return g->current_async_result_ptr;
}

/* 协作式抢占与 Task.Yield 重新入队块（共享于所有让出点） */
LLVMBasicBlockRef get_async_requeue_bb(zan_irgen_t *g) {
    if (!g->current_async_frame || !g->current_async_resume_fn) return NULL;
    if (g->current_async_requeue_bb) return g->current_async_requeue_bb;
    LLVMBasicBlockRef here = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->current_async_resume_fn, "co.requeue");
    LLVMPositionBuilderAtEnd(g->builder, bb);
    LLVMValueRef self_i8 = get_async_self_i8(g);
    zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready,
        (LLVMValueRef[]){ self_i8, g->current_async_resume_fn }, 2, "");
    LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));
    if (here) LLVMPositionBuilderAtEnd(g->builder, here);
    g->current_async_requeue_bb = bb;
    return bb;
}

static bool scope_has_owned_cleanups(zan_irgen_t *g, local_scope_t *locals, int start) {
    (void)g;
    if (!locals) return false;
    for (int i = locals->count - 1; i >= start; i--) {
        if (locals->vars[i].eh_slot) return true;
        if (locals->vars[i].obj_rc_flag) return true;
        if (locals->vars[i].box_cell) return true;
        if (locals->vars[i].struct_rc) return true;
        if (local_owns_arc(&locals->vars[i])) return true;
    }
    return false;
}

/* 共享顶层取消退出块：无词法作用域未决局部变量时直接跳转完成块 */
LLVMBasicBlockRef get_async_cancel_bb(zan_irgen_t *g, local_scope_t *locals) {
    if (scope_has_owned_cleanups(g, locals, g->current_async_frame_local_count)) return NULL;
    if (g->current_async_cancel_bb) return g->current_async_cancel_bb;
    LLVMBasicBlockRef here = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->current_async_resume_fn, "co.cancelled");
    LLVMPositionBuilderAtEnd(g->builder, bb);
    emit_async_complete(g, NULL, NULL);
    if (here) LLVMPositionBuilderAtEnd(g->builder, here);
    g->current_async_cancel_bb = bb;
    return bb;
}

/* 发射取消检查：若已请求取消则进入完成路径释放作用域变量并退出 */
void emit_async_cancel_check(zan_irgen_t *g, local_scope_t *locals) {
    if (!g->current_async_frame) return;
    /* 在 finally 保护块内暂缓退出，等待离开 finally 后再响应取消 */
    if (g->finally_count > 0 || g->catch_cleanup_count > 0) return;
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) return;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef cancel_ptr = get_async_cancel_ptr(g);
    LLVMValueRef flag = LLVMBuildLoad2(g->builder, i32, cancel_ptr, "cancelled");
    LLVMBasicBlockRef shared_can = get_async_cancel_bb(g, locals);
    LLVMBasicBlockRef can_bb = shared_can ? shared_can
        : LLVMAppendBasicBlockInContext(g->ctx, g->current_async_resume_fn, "co.cancelled");
    LLVMBasicBlockRef go_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_async_resume_fn, "co.notcancelled");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntNE, flag, LLVMConstInt(i32, 0, 0), "is.cancelled"),
        can_bb, go_bb);
    if (!shared_can) {
        LLVMPositionBuilderAtEnd(g->builder, can_bb);
        emit_async_complete(g, locals, NULL);
    }
    LLVMPositionBuilderAtEnd(g->builder, go_bb);
}

/* 弹出本次 resume 挂载的异常处理器并恢复栈上下文 */
void emit_async_eh_unarm(zan_irgen_t *g) {
    if (!g->current_async_eh_entry) return;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);
    LLVMValueRef entry = LLVMBuildLoad2(g->builder, i32,
        g->current_async_eh_entry, "eh.entry");
    LLVMBuildStore(g->builder, entry, top_g);
}
LLVMBasicBlockRef get_async_suspend_ret_bb(zan_irgen_t *g) {
    if (!g->current_async_frame || !g->current_async_resume_fn) return NULL;
    if (g->current_async_suspend_ret_bb) return g->current_async_suspend_ret_bb;
    LLVMBasicBlockRef here = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->current_async_resume_fn, "co.suspend.ret");
    LLVMPositionBuilderAtEnd(g->builder, bb);
    emit_async_eh_unarm(g);
    LLVMBuildRetVoid(g->builder);
    if (here) LLVMPositionBuilderAtEnd(g->builder, here);
    g->current_async_suspend_ret_bb = bb;
    return bb;
}

/* 代理 alloca 替换为帧字段前完成指针类型提取与校验 */
void emit_async_finalize_slots(zan_irgen_t *g) {
    zan_builder_point_t pt = zan_builder_save(g);
    for (int i = 0; i < g->current_async_slot_count; i++) {
        zan_async_slot_t *slot = &g->current_async_slots[i];
        LLVMValueRef proxy = slot->slot_alloca;
        LLVMPositionBuilderBefore(g->builder, proxy);
        LLVMValueRef address = LLVMBuildStructGEP2(g->builder,
            g->current_async_frame_type, g->current_async_frame,
            (unsigned)slot->frame_index, "fr.local");
        LLVMReplaceAllUsesWith(proxy, address);
        /* 作用域记录终结：清理临时句柄 */
        if (g->current_this == proxy) g->current_this = NULL;
        slot->slot_alloca = NULL;
        LLVMInstructionEraseFromParent(proxy);
    }
    zan_builder_restore(g, pt);
}

/* 循环回边协作式抢占点：时间片用尽时重新排队当前帧并让出控制权 */
bool emit_async_preempt_site(zan_irgen_t *g, LLVMBasicBlockRef resume_target) {
    if (!g->current_async_frame || !g->current_async_switch) return false;
    LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
    LLVMBasicBlockRef preempt_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->current_fn, "co.preempt");
    int k = g->current_async_next_state++;
    LLVMValueRef fired = zan_call2(g->builder, g->rt_co_poll_type, g->rt_co_poll,
        NULL, 0, "poll");
    LLVMValueRef want = zan_icmp(g->builder, LLVMIntNE, fired,
        LLVMConstInt(di32, 0, 0), "poll.want");
    LLVMBuildCondBr(g->builder, want, preempt_bb, resume_target);

    LLVMPositionBuilderAtEnd(g->builder, preempt_bb);
    zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
    LLVMBuildBr(g->builder, get_async_requeue_bb(g));

    LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), resume_target);
    return true;
}

/* await A-标准型 (ANF) 正规化：将嵌套表达式中的 await 提升为独立临时变量语句 */
