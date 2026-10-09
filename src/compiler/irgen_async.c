/* irgen_async.c: 异步函数 CPS 变换、await ANF 正规化与协程帧生成 (由 irgen.c 包含) */

/* 判断类型写入 64 位帧结果槽时是否需零扩展（无符号标量类型） */
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
static LLVMValueRef coerce_to_frame_result(zan_irgen_t *g, LLVMValueRef v,
                                           zan_type_t *ty) {
    if (!v) return NULL;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef t = LLVMTypeOf(v);
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
                g->current_async_frame && g->current_async_ret_agg_slot >= 0) {
                LLVMValueRef slot_ptr = LLVMBuildStructGEP2(g->builder,
                    g->current_async_frame_type, g->current_async_frame,
                    (unsigned)g->current_async_ret_agg_slot, "ret.agg.slot");
                LLVMValueRef loaded = LLVMBuildLoad2(g->builder, want, v, "ret.agg.load");
                LLVMBuildStore(g->builder, loaded, slot_ptr);
                return LLVMBuildPtrToInt(g->builder, slot_ptr, i64, "res.slot");
            }
        }
        return LLVMBuildPtrToInt(g->builder, v, i64, "res.slot");
    }
    case LLVMStructTypeKind: {
        if (g->current_async_frame && g->current_async_ret_agg_slot >= 0) {
            LLVMValueRef slot_ptr = LLVMBuildStructGEP2(g->builder,
                g->current_async_frame_type, g->current_async_frame,
                (unsigned)g->current_async_ret_agg_slot, "ret.agg.slot");
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
static LLVMValueRef coerce_async_ret(zan_irgen_t *g, LLVMValueRef val) {
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
static LLVMValueRef coerce_from_frame_result(zan_irgen_t *g, LLVMValueRef res,
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
static LLVMValueRef coerce_to_i64(zan_irgen_t *g, LLVMValueRef v) {
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

enum { ASYNC_PENDING_EXCEPTION = 1, ASYNC_PENDING_RETURN = 2,
       ASYNC_PENDING_BORROWED_EXCEPTION = 3 };

static LLVMValueRef async_pending_count_ptr(zan_irgen_t *g, LLVMValueRef frame,
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

static LLVMValueRef async_pending_field(zan_irgen_t *g, LLVMTypeRef ft,
                                        LLVMValueRef record, unsigned field) {
    LLVMTypeRef rt = LLVMGetElementType(
        LLVMStructGetTypeAtIndex(ft, ASYNC_FRAME_PENDING));
    return LLVMBuildStructGEP2(g->builder, rt, record, field, "pending.field");
}

static LLVMValueRef async_pending_discard_fn(zan_irgen_t *g, LLVMTypeRef ft) {
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

static void emit_async_pending_discard(zan_irgen_t *g, LLVMValueRef frame,
                                       LLVMTypeRef ft, LLVMValueRef depth) {
    if (!LLVMGetArrayLength(LLVMStructGetTypeAtIndex(ft, ASYNC_FRAME_PENDING))) return;
    LLVMValueRef fn = async_pending_discard_fn(g, ft);
    LLVMValueRef args[] = { frame, depth };
    zan_call2(g->builder, LLVMGlobalGetValueType(fn), fn, args, 2, "");
}

/* 待定引用所有权账本：在移动或作用域丢弃前持有引用，catch 处清理中断分支未决引用 */
static LLVMValueRef emit_async_pending_push(zan_irgen_t *g, LLVMValueRef value,
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

static LLVMValueRef emit_async_pending_top(zan_irgen_t *g) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef count = LLVMBuildLoad2(g->builder, i32,
        async_pending_count_ptr(g, g->current_async_frame,
                                g->current_async_frame_type), "pending.count");
    return async_pending_record(g, g->current_async_frame, g->current_async_frame_type,
        LLVMBuildSub(g->builder, count, LLVMConstInt(i32, 1, 0), "pending.last"));
}

static void emit_async_pending_pop(zan_irgen_t *g, LLVMValueRef record) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0),
                   async_pending_field(g, ft, record, 2));
    LLVMValueRef cp = async_pending_count_ptr(g, g->current_async_frame, ft);
    LLVMBuildStore(g->builder, LLVMBuildSub(g->builder,
        LLVMBuildLoad2(g->builder, i32, cp, "pending.count"),
        LLVMConstInt(i32, 1, 0), "pending.previous"), cp);
}

static void emit_async_pending_discard_preserving(zan_irgen_t *g,
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

static void emit_async_pending_return(zan_irgen_t *g, LLVMValueRef value) {
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

static LLVMValueRef emit_async_pending_take_return(zan_irgen_t *g) {
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
static void emit_async_pending_discard_body(zan_irgen_t *g, LLVMTypeRef ft,
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
static void emit_async_complete(zan_irgen_t *g, local_scope_t *locals,
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

/* 发射共享完成尾声：存入结果、标记完成、释放持有者、唤醒等待者并 ret void */
static void emit_async_complete_epilogue(zan_irgen_t *g, local_scope_t *locals) {
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
    LLVMValueRef fn = g->current_fn;
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
}

/* 声明活跃协程帧注册表接口（zan_co_live_add/del/has） */
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

/* 模块核心语义抽象与接口调用契约 */
static LLVMValueRef emit_co_live_has(zan_irgen_t *g, LLVMValueRef frame) {
    LLVMTypeRef ty;
    LLVMValueRef fn = get_co_live_fn(g, "zan_co_live_has", true, &ty);
    return zan_call2(g->builder, ty, fn, &frame, 1, "co.live");
}

/* 发射 zan_timer_cancel_delay(frame)，从定时器堆中移除该帧的未决延迟任务 */
static void emit_co_cancel_delay(zan_irgen_t *g, LLVMValueRef frame) {
    /* 运行时定义返回取消数量 i32，为兼容 wasm32 链接签名必须严格匹配 */
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ty = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), &i8ptr, 1, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_timer_cancel_delay");
    if (!fn) fn = LLVMAddFunction(g->mod, "zan_timer_cancel_delay", ty);
    zan_call2(g->builder, ty, fn, &frame, 1, "");
}

/* 创建内部辅助函数 `void f(i8*)` 并将 builder 定位到 entry 块 */
static bool open_co_helper(zan_irgen_t *g, const char *name, LLVMValueRef *fn,
                           LLVMBasicBlockRef *saved, LLVMValueRef *saved_fn) {
    LLVMValueRef existing = LLVMGetNamedFunction(g->mod, name);
    if (existing) { *fn = existing; return false; }
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    *fn = LLVMAddFunction(g->mod, name,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0));
    LLVMSetLinkage(*fn, LLVMInternalLinkage);
    *saved = LLVMGetInsertBlock(g->builder);
    *saved_fn = g->current_fn;
    g->current_fn = *fn;
    LLVMPositionBuilderAtEnd(g->builder,
        LLVMAppendBasicBlockInContext(g->ctx, *fn, "entry"));
    di_clear(g);
    return true;
}

static void close_co_helper(zan_irgen_t *g, LLVMBasicBlockRef saved,
                            LLVMValueRef saved_fn) {
    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
}

/* 底层系统交互与数据协议契约 */
static LLVMValueRef get_co_track_fn(zan_irgen_t *g) {
    LLVMValueRef fn; LLVMBasicBlockRef saved; LLVMValueRef saved_fn;
    if (!open_co_helper(g, "__zan_co_track", &fn, &saved, &saved_fn)) return fn;
    LLVMTypeRef ty;
    LLVMValueRef add = get_co_live_fn(g, "zan_co_live_add", false, &ty);
    LLVMValueRef f = LLVMGetParam(fn, 0);
    zan_call2(g->builder, ty, add, &f, 1, "");
    LLVMBuildRetVoid(g->builder);
    close_co_helper(g, saved, saved_fn);
    return fn;
}

/* 编译器代码生成与运行时系统底层调用契约 */
static LLVMValueRef get_co_untrack_fn(zan_irgen_t *g) {
    LLVMValueRef fn; LLVMBasicBlockRef saved; LLVMValueRef saved_fn;
    if (!open_co_helper(g, "__zan_co_untrack", &fn, &saved, &saved_fn)) return fn;
    LLVMTypeRef ty;
    LLVMValueRef del = get_co_live_fn(g, "zan_co_live_del", false, &ty);
    LLVMValueRef f = LLVMGetParam(fn, 0);
    zan_call2(g->builder, ty, del, &f, 1, "");
    LLVMBuildRetVoid(g->builder);
    close_co_helper(g, saved, saved_fn);
    return fn;
}

/* 获取或声明模块内部的 `__zan_co_cancel(i8*)`（协作式取消标记置位） */
static LLVMValueRef get_co_cancel_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_co_cancel");
    if (fn) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef hdr = g->co_header_type;
    fn = LLVMAddFunction(g->mod, "__zan_co_cancel",
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0));
    LLVMSetLinkage(fn, LLVMInternalLinkage);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef head_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "cc.head");
    LLVMBasicBlockRef mark_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "cc.mark");
    LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "cc.ret");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    di_clear(g);
    LLVMValueRef arg = LLVMGetParam(fn, 0);
    LLVMValueRef cur = LLVMBuildAlloca(g->builder, i8ptr, "cc.cur");
    LLVMBuildStore(g->builder, arg, cur);
    /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef live = emit_co_live_has(g, arg);
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntNE, live, LLVMConstInt(i32, 0, 0), "cc.islive"),
        head_bb, ret_bb);

    LLVMPositionBuilderAtEnd(g->builder, head_bb);
    LLVMValueRef f = LLVMBuildLoad2(g->builder, i8ptr, cur, "cc.f");
    LLVMValueRef nn = zan_icmp(g->builder, LLVMIntNE, f, LLVMConstNull(i8ptr), "cc.nn");
    LLVMBuildCondBr(g->builder, nn, mark_bb, ret_bb);

    LLVMPositionBuilderAtEnd(g->builder, mark_bb);
    LLVMBuildStore(g->builder, LLVMConstInt(i32, 1, 0),
        LLVMBuildStructGEP2(g->builder, hdr, f, ASYNC_FRAME_CANCEL, "cc.flag"));
    LLVMValueRef child = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildStructGEP2(g->builder, hdr, f, ASYNC_FRAME_CHILD, "cc.child.p"),
        "cc.child");
    LLVMBuildStore(g->builder, child, cur);
    LLVMBuildBr(g->builder, head_bb);

    LLVMPositionBuilderAtEnd(g->builder, ret_bb);
    LLVMBuildRetVoid(g->builder);

    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* 获取或声明模块内部的 `__zan_co_isdone(i8*)`（查询协程完成状态） */
static LLVMValueRef get_co_isdone_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_co_isdone");
    if (fn) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef hdr = g->co_header_type;
    fn = LLVMAddFunction(g->mod, "__zan_co_isdone",
        LLVMFunctionType(i32, &i8ptr, 1, 0));
    LLVMSetLinkage(fn, LLVMInternalLinkage);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef live_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "id.live");
    LLVMBasicBlockRef gone_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "id.gone");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    di_clear(g);
    LLVMValueRef arg = LLVMGetParam(fn, 0);
    LLVMValueRef live = emit_co_live_has(g, arg);
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntNE, live, LLVMConstInt(i32, 0, 0), "id.islive"),
        live_bb, gone_bb);

    LLVMPositionBuilderAtEnd(g->builder, live_bb);
    LLVMValueRef done = LLVMBuildLoad2(g->builder, i32,
        LLVMBuildStructGEP2(g->builder, hdr, arg, ASYNC_FRAME_DONE, "id.done.p"),
        "id.done");
    LLVMBuildRet(g->builder, LLVMBuildZExt(g->builder,
        zan_icmp(g->builder, LLVMIntNE, done, LLVMConstInt(i32, 0, 0), "id.isdone"),
        i32, "id.r"));

    LLVMPositionBuilderAtEnd(g->builder, gone_bb);
    LLVMBuildRet(g->builder, LLVMConstInt(i32, 1, 0));

    g->current_fn = saved_fn;
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
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

static LLVMValueRef get_async_self_i8(zan_irgen_t *g) {
    if (g->current_async_self_i8) return g->current_async_self_i8;
    if (!g->current_async_frame) return NULL;
    LLVMBasicBlockRef here = LLVMGetInsertBlock(g->builder);
    position_before_entry_terminator(g);
    LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    g->current_async_self_i8 = LLVMBuildBitCast(g->builder,
        g->current_async_frame, di8ptr, "self.i8");
    if (here) LLVMPositionBuilderAtEnd(g->builder, here);
    return g->current_async_self_i8;
}

static LLVMValueRef get_async_state_ptr(zan_irgen_t *g) {
    if (g->current_async_state_ptr) return g->current_async_state_ptr;
    if (!g->current_async_frame || !g->current_async_frame_type) return NULL;
    LLVMBasicBlockRef here = LLVMGetInsertBlock(g->builder);
    position_before_entry_terminator(g);
    g->current_async_state_ptr = LLVMBuildStructGEP2(g->builder,
        g->current_async_frame_type, g->current_async_frame,
        ASYNC_FRAME_STATE, "self.state");
    if (here) LLVMPositionBuilderAtEnd(g->builder, here);
    return g->current_async_state_ptr;
}

static LLVMValueRef get_async_cancel_ptr(zan_irgen_t *g) {
    if (g->current_async_cancel_ptr) return g->current_async_cancel_ptr;
    if (!g->current_async_frame || !g->current_async_frame_type) return NULL;
    LLVMBasicBlockRef here = LLVMGetInsertBlock(g->builder);
    position_before_entry_terminator(g);
    g->current_async_cancel_ptr = LLVMBuildStructGEP2(g->builder,
        g->current_async_frame_type, g->current_async_frame,
        ASYNC_FRAME_CANCEL, "fr.cancel.p");
    if (here) LLVMPositionBuilderAtEnd(g->builder, here);
    return g->current_async_cancel_ptr;
}

static LLVMValueRef get_async_self_int(zan_irgen_t *g, LLVMTypeRef ptr_int_ty) {
    if (g->current_async_self_int) return g->current_async_self_int;
    LLVMValueRef self_i8 = get_async_self_i8(g);
    if (!self_i8) return NULL;
    LLVMBasicBlockRef here = LLVMGetInsertBlock(g->builder);
    position_before_entry_terminator(g);
    g->current_async_self_int = LLVMBuildPtrToInt(g->builder, self_i8, ptr_int_ty, "self.int");
    if (here) LLVMPositionBuilderAtEnd(g->builder, here);
    return g->current_async_self_int;
}

static LLVMValueRef get_async_child_ptr(zan_irgen_t *g) {
    if (g->current_async_child_ptr) return g->current_async_child_ptr;
    if (!g->current_async_frame || !g->current_async_frame_type) return NULL;
    LLVMBasicBlockRef here = LLVMGetInsertBlock(g->builder);
    position_before_entry_terminator(g);
    g->current_async_child_ptr = LLVMBuildStructGEP2(g->builder,
        g->current_async_frame_type, g->current_async_frame,
        ASYNC_FRAME_CHILD, "self.child");
    if (here) LLVMPositionBuilderAtEnd(g->builder, here);
    return g->current_async_child_ptr;
}

static LLVMValueRef get_async_sub_slot_ptr(zan_irgen_t *g) {
    if (g->current_async_sub_slot_ptr) return g->current_async_sub_slot_ptr;
    if (!g->current_async_frame || !g->current_async_frame_type) return NULL;
    LLVMBasicBlockRef here = LLVMGetInsertBlock(g->builder);
    position_before_entry_terminator(g);
    g->current_async_sub_slot_ptr = LLVMBuildStructGEP2(g->builder,
        g->current_async_frame_type, g->current_async_frame,
        (unsigned)g->current_async_sub_base, "self.sub_slot");
    if (here) LLVMPositionBuilderAtEnd(g->builder, here);
    return g->current_async_sub_slot_ptr;
}

static LLVMValueRef get_async_result_ptr(zan_irgen_t *g) {
    if (g->current_async_result_ptr) return g->current_async_result_ptr;
    if (!g->current_async_frame || !g->current_async_frame_type) return NULL;
    LLVMBasicBlockRef here = LLVMGetInsertBlock(g->builder);
    position_before_entry_terminator(g);
    g->current_async_result_ptr = LLVMBuildStructGEP2(g->builder,
        g->current_async_frame_type, g->current_async_frame,
        ASYNC_FRAME_RESULT, "self.res_slot");
    if (here) LLVMPositionBuilderAtEnd(g->builder, here);
    return g->current_async_result_ptr;
}

/* 协作式抢占与 Task.Yield 重新入队块（共享于所有让出点） */
static LLVMBasicBlockRef get_async_requeue_bb(zan_irgen_t *g) {
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
static LLVMBasicBlockRef get_async_cancel_bb(zan_irgen_t *g, local_scope_t *locals) {
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
static void emit_async_cancel_check(zan_irgen_t *g, local_scope_t *locals) {
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
static void emit_async_eh_unarm(zan_irgen_t *g) {
    if (!g->current_async_eh_entry) return;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);
    LLVMValueRef entry = LLVMBuildLoad2(g->builder, i32,
        g->current_async_eh_entry, "eh.entry");
    LLVMBuildStore(g->builder, entry, top_g);
}

static LLVMBasicBlockRef get_async_suspend_ret_bb(zan_irgen_t *g) {
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
static void emit_async_finalize_slots(zan_irgen_t *g) {
    LLVMBasicBlockRef here = LLVMGetInsertBlock(g->builder);
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
    if (here) LLVMPositionBuilderAtEnd(g->builder, here);
}

/* 循环回边协作式抢占点：时间片用尽时重新排队当前帧并让出控制权 */
static bool emit_async_preempt_site(zan_irgen_t *g, LLVMBasicBlockRef resume_target) {
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

typedef struct {
    zan_irgen_t    *g;
    zan_ast_list_t *out;  /* 核心系统底层抽象与内存语义契约 */
    int            *counter;
} anf_ctx_t;

static bool anf_expr_contains_await(zan_ast_node_t *e) {
    if (!e) return false;
    switch (e->kind) {
    case AST_AWAIT_EXPR: return true;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        return anf_expr_contains_await(e->binary.left) ||
               anf_expr_contains_await(e->binary.right);
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        return anf_expr_contains_await(e->unary.operand);
    case AST_CALL: {
        if (anf_expr_contains_await(e->call.callee)) return true;
        for (int i = 0; i < e->call.args.count; i++)
            if (anf_expr_contains_await(e->call.args.items[i])) return true;
        return false;
    }
    case AST_MEMBER_ACCESS: return anf_expr_contains_await(e->member.object);
    case AST_INDEX:
        return anf_expr_contains_await(e->index.object) ||
               anf_expr_contains_await(e->index.index);
    case AST_CONDITIONAL:
        return anf_expr_contains_await(e->conditional.cond) ||
               anf_expr_contains_await(e->conditional.then_expr) ||
               anf_expr_contains_await(e->conditional.else_expr);
    case AST_NEW_EXPR: {
        for (int i = 0; i < e->new_expr.args.count; i++)
            if (anf_expr_contains_await(e->new_expr.args.items[i])) return true;
        for (int i = 0; i < e->new_expr.arg_inits.count; i++)
            if (anf_expr_contains_await(e->new_expr.arg_inits.items[i]))
                return true;
        return false;
    }
    case AST_COLL_INIT: {
        for (int i = 0; i < e->coll_init.items.count; i++)
            if (anf_expr_contains_await(e->coll_init.items.items[i])) return true;
        return false;
    }
    case AST_CAST_EXPR: return anf_expr_contains_await(e->cast.expr);
    case AST_IS_EXPR:
    case AST_AS_EXPR:  return anf_expr_contains_await(e->type_test.expr);
    default: return false;
    }
}

/* 提升位于 await 之前且具副作用的操作数至临时变量，保证求值顺序保真 */
static bool anf_expr_has_side_effect(zan_ast_node_t *e) {
    if (!e) return false;
    switch (e->kind) {
    case AST_AWAIT_EXPR: return false; /* 核心系统底层抽象与内存语义契约 */
    case AST_CALL: return true;
    case AST_ASSIGNMENT: return true;
    case AST_POSTFIX_UNARY: return true;
    case AST_UNARY:
        if (e->unary.op == TK_PLUS_PLUS || e->unary.op == TK_MINUS_MINUS) return true;
        return anf_expr_has_side_effect(e->unary.operand);
    case AST_BINARY:
        return anf_expr_has_side_effect(e->binary.left) ||
               anf_expr_has_side_effect(e->binary.right);
    case AST_MEMBER_ACCESS: return anf_expr_has_side_effect(e->member.object);
    case AST_INDEX:
        return anf_expr_has_side_effect(e->index.object) ||
               anf_expr_has_side_effect(e->index.index);
    case AST_CAST_EXPR: return anf_expr_has_side_effect(e->cast.expr);
    case AST_IS_EXPR:
    case AST_AS_EXPR:  return anf_expr_has_side_effect(e->type_test.expr);
    default: return false;
    }
}

static zan_ast_node_t *anf_expr(anf_ctx_t *c, zan_ast_node_t *e);
static void anf_spill_await_receiver(anf_ctx_t *c, zan_ast_node_t *aw);

/* 检查先求值的操作数是否具副作用且后续操作数含 await */
static void anf_check_order(anf_ctx_t *c, zan_ast_node_t *before, zan_ast_node_t *after) {
    if (after && before && anf_expr_contains_await(after) &&
        anf_expr_has_side_effect(before)) {
        zan_diag_emit(c->g->diag, DIAG_ERROR, before->loc,
            "async: an expression with side effects is evaluated before an "
            "await in the same expression; assign it to a local first");
    }
}

/* 将 `await E` 提升为 `var $awN = await E;` 声明并返回该临时变量引用 */
static zan_ast_node_t *anf_hoist_await(anf_ctx_t *c, zan_ast_node_t *aw) {
    /* 模块核心语义抽象与接口调用契约 */
    aw->await_expr.expr = anf_expr(c, aw->await_expr.expr);
    anf_spill_await_receiver(c, aw);

    char buf[32];
    int len = snprintf(buf, sizeof buf, "$aw%d", (*c->counter)++);
    char *nm = (char *)zan_arena_alloc(c->g->arena, (size_t)len + 1);
    memcpy(nm, buf, (size_t)len + 1);
    zan_istr_t name = { nm, (uint32_t)len };

    zan_ast_node_t *vd = zan_ast_new(c->g->arena, AST_VAR_DECL, aw->loc);
    vd->var_decl.name = name;
    vd->var_decl.type = NULL; /* 核心系统底层抽象与内存语义契约 */
    vd->var_decl.initializer = aw;
    zan_ast_list_push(c->out, vd, c->g->arena);

    zan_ast_node_t *id = zan_ast_new(c->g->arena, AST_IDENTIFIER, aw->loc);
    id->ident.name = name;
    return id;
}

/* 将被 await 调用的计算所得接收者提升为独立局部变量，避免跨挂起点丢失 SSA 临时值 */
static bool anf_receiver_needs_spill(zan_ast_node_t *obj) {
    if (!obj) return false;
    switch (obj->kind) {
    case AST_CALL:
    case AST_NEW_EXPR:
    case AST_INDEX:
    case AST_CONDITIONAL:
    case AST_CAST_EXPR:
        return true;
    case AST_MEMBER_ACCESS:
        return anf_receiver_needs_spill(obj->member.object);
    default:
        return false;
    }
}

static void anf_spill_await_receiver(anf_ctx_t *c, zan_ast_node_t *aw) {
    zan_ast_node_t *call = aw->await_expr.expr;
    if (!call || call->kind != AST_CALL) return;
    zan_ast_node_t *callee = call->call.callee;
    if (!callee || callee->kind != AST_MEMBER_ACCESS) return;
    zan_ast_node_t *obj = callee->member.object;
    if (!anf_receiver_needs_spill(obj)) return;

    char buf[32];
    int len = snprintf(buf, sizeof buf, "$rc%d", (*c->counter)++);
    char *nm = (char *)zan_arena_alloc(c->g->arena, (size_t)len + 1);
    memcpy(nm, buf, (size_t)len + 1);
    zan_istr_t name = { nm, (uint32_t)len };

    zan_ast_node_t *vd = zan_ast_new(c->g->arena, AST_VAR_DECL, obj->loc);
    vd->var_decl.name = name;
    vd->var_decl.type = NULL; /* 核心系统底层抽象与内存语义契约 */
    vd->var_decl.initializer = obj;
    zan_ast_list_push(c->out, vd, c->g->arena);

    zan_ast_node_t *id = zan_ast_new(c->g->arena, AST_IDENTIFIER, obj->loc);
    id->ident.name = name;
    callee->member.object = id;
}

/* 递归提取表达式中的 await 并按求值顺序追加至 hoisted 声明列表 */
static zan_ast_node_t *anf_expr(anf_ctx_t *c, zan_ast_node_t *e) {
    if (!e) return e;
    switch (e->kind) {
    case AST_AWAIT_EXPR:
        return anf_hoist_await(c, e);
    case AST_BINARY:
    case AST_ASSIGNMENT:
        if (e->binary.op == TK_AMP_AMP || e->binary.op == TK_PIPE_PIPE) {
            /* 底层系统交互与数据协议契约 */
            e->binary.left = anf_expr(c, e->binary.left);
            return e;
        }
        anf_check_order(c, e->binary.left, e->binary.right);
        e->binary.left  = anf_expr(c, e->binary.left);
        e->binary.right = anf_expr(c, e->binary.right);
        return e;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        e->unary.operand = anf_expr(c, e->unary.operand);
        return e;
    case AST_CALL:
        for (int i = 0; i < e->call.args.count; i++) {
            for (int j = i + 1; j < e->call.args.count; j++)
                anf_check_order(c, e->call.args.items[i], e->call.args.items[j]);
        }
        e->call.callee = anf_expr(c, e->call.callee);
        for (int i = 0; i < e->call.args.count; i++)
            e->call.args.items[i] = anf_expr(c, e->call.args.items[i]);
        return e;
    case AST_MEMBER_ACCESS:
        e->member.object = anf_expr(c, e->member.object);
        return e;
    case AST_INDEX:
        anf_check_order(c, e->index.object, e->index.index);
        e->index.object = anf_expr(c, e->index.object);
        e->index.index  = anf_expr(c, e->index.index);
        return e;
    case AST_CONDITIONAL:
        /* 模块核心语义抽象与接口调用契约 */
        e->conditional.cond = anf_expr(c, e->conditional.cond);
        return e;
    case AST_CAST_EXPR:
        e->cast.expr = anf_expr(c, e->cast.expr);
        return e;
    case AST_NEW_EXPR:
        for (int i = 0; i < e->new_expr.args.count; i++) {
            for (int j = i + 1; j < e->new_expr.args.count; j++)
                anf_check_order(c, e->new_expr.args.items[i], e->new_expr.args.items[j]);
        }
        for (int i = 0; i < e->new_expr.args.count; i++)
            e->new_expr.args.items[i] = anf_expr(c, e->new_expr.args.items[i]);
        for (int i = 0; i < e->new_expr.arg_inits.count; i++)
            e->new_expr.arg_inits.items[i] = anf_expr(c, e->new_expr.arg_inits.items[i]);
        return e;
    case AST_COLL_INIT:
        for (int i = 0; i < e->coll_init.items.count; i++) {
            for (int j = i + 1; j < e->coll_init.items.count; j++)
                anf_check_order(c, e->coll_init.items.items[i], e->coll_init.items.items[j]);
        }
        for (int i = 0; i < e->coll_init.items.count; i++)
            e->coll_init.items.items[i] = anf_expr(c, e->coll_init.items.items[i]);
        return e;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        e->type_test.expr = anf_expr(c, e->type_test.expr);
        return e;
    default:
        return e;
    }
}

static void anf_normalize_block(zan_irgen_t *g, zan_ast_node_t *block, int *counter);

/* 递归判断语句子树中是否包含任何 await 节点 */
static bool anf_stmt_contains_await(zan_ast_node_t *st) {
    if (!st) return false;
    switch (st->kind) {
    case AST_VAR_DECL:    return anf_expr_contains_await(st->var_decl.initializer);
    case AST_EXPR_STMT:   return anf_expr_contains_await(st->expr_stmt.expr);
    case AST_RETURN_STMT: return anf_expr_contains_await(st->ret.value);
    case AST_THROW_STMT:  return anf_expr_contains_await(st->throw_stmt.value);
    case AST_IF_STMT:
        return anf_expr_contains_await(st->if_stmt.cond) ||
               anf_stmt_contains_await(st->if_stmt.then_body) ||
               anf_stmt_contains_await(st->if_stmt.else_body);
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        return anf_expr_contains_await(st->while_stmt.cond) ||
               anf_stmt_contains_await(st->while_stmt.body);
    case AST_FOR_STMT:
        return anf_stmt_contains_await(st->for_stmt.init) ||
               anf_expr_contains_await(st->for_stmt.cond) ||
               anf_expr_contains_await(st->for_stmt.step) ||
               anf_stmt_contains_await(st->for_stmt.body);
    case AST_FOREACH_STMT:
        return anf_expr_contains_await(st->foreach_stmt.collection) ||
               anf_stmt_contains_await(st->foreach_stmt.body);
    case AST_TRY_STMT: {
        if (anf_stmt_contains_await(st->try_stmt.try_body)) return true;
        for (int i = 0; i < st->try_stmt.catches.count; i++)
            if (anf_stmt_contains_await(st->try_stmt.catches.items[i]))
                return true;
        return anf_stmt_contains_await(st->try_stmt.finally_body);
    }
    case AST_CATCH_CLAUSE:
        return anf_stmt_contains_await(st->catch_clause.body);
    case AST_SWITCH_STMT: {
        if (anf_expr_contains_await(st->switch_stmt.expr)) return true;
        for (int i = 0; i < st->switch_stmt.cases.count; i++)
            if (anf_stmt_contains_await(st->switch_stmt.cases.items[i]))
                return true;
        return false;
    }
    case AST_SWITCH_CASE:
        return anf_stmt_contains_await(st->switch_case.body);
    case AST_LOCK_STMT:
        return anf_expr_contains_await(st->lock_stmt.expr) ||
               anf_stmt_contains_await(st->lock_stmt.body);
    case AST_CHECKED_STMT:
        return anf_stmt_contains_await(st->checked_stmt.body);
    case AST_BLOCK: {
        for (int i = 0; i < st->block.stmts.count; i++)
            if (anf_stmt_contains_await(st->block.stmts.items[i])) return true;
        return false;
    }
    default: return false;
    }
}

/* 确保目标槽位为 AST_BLOCK 节点以容纳提升语句并进行正规化 */
static void anf_normalize_body(zan_irgen_t *g, zan_ast_node_t **slot, int *counter) {
    zan_ast_node_t *body = *slot;
    if (!body) return;
    if (body->kind != AST_BLOCK) {
        if (!anf_stmt_contains_await(body)) return;
        zan_ast_node_t *blk = zan_ast_new(g->arena, AST_BLOCK, body->loc);
        zan_ast_list_init(&blk->block.stmts);
        zan_ast_list_push(&blk->block.stmts, body, g->arena);
        *slot = blk;
        body = blk;
    }
    anf_normalize_block(g, body, counter);
}

/* 重写条件含 await 的循环表达式，使其在循环体首部作为语句求值 */
static zan_ast_node_t *anf_loop_cond_guard(zan_irgen_t *g, zan_ast_list_t *dst,
                                           int *counter, zan_ast_node_t **cond_slot,
                                           const zan_loc_t *loc) {
    anf_ctx_t c = { g, dst, counter };
    zan_ast_node_t *residual = anf_expr(&c, *cond_slot);
    zan_ast_node_t *neg = zan_ast_new(g->arena, AST_UNARY, *loc);
    neg->unary.op = TK_BANG;
    neg->unary.operand = residual;
    zan_ast_node_t *tb = zan_ast_new(g->arena, AST_BLOCK, *loc);
    zan_ast_list_init(&tb->block.stmts);
    zan_ast_list_push(&tb->block.stmts,
        zan_ast_new(g->arena, AST_BREAK_STMT, *loc), g->arena);
    zan_ast_node_t *ifs = zan_ast_new(g->arena, AST_IF_STMT, *loc);
    ifs->if_stmt.cond = neg;
    ifs->if_stmt.then_body = tb;
    ifs->if_stmt.else_body = NULL;
    zan_ast_list_push(dst, ifs, g->arena);
    zan_ast_node_t *tru = zan_ast_new(g->arena, AST_BOOL_LITERAL, *loc);
    tru->bool_val = true;
    return tru;
}

/* 单条语句正规化：将提升出的声明按求值顺序追加至目标列表 */
static void anf_normalize_stmt(zan_irgen_t *g, zan_ast_node_t *st,
                               zan_ast_list_t *dst, int *counter) {
    if (!st) return;
    anf_ctx_t c = { g, dst, counter };
    switch (st->kind) {
    case AST_VAR_DECL:
        /* `T x = await E;` 已位于语句位置，直接保留原初始化式避免类型信息丢失 */
        if (st->var_decl.initializer &&
            st->var_decl.initializer->kind == AST_AWAIT_EXPR) {
            st->var_decl.initializer->await_expr.expr =
                anf_expr(&c, st->var_decl.initializer->await_expr.expr);
            anf_spill_await_receiver(&c, st->var_decl.initializer);
        } else {
            st->var_decl.initializer = anf_expr(&c, st->var_decl.initializer);
        }
        break;
    case AST_EXPR_STMT:
        /* 顶层表达式语句 `await E;` 仅递归正规化其内部嵌套 await */
        if (st->expr_stmt.expr && st->expr_stmt.expr->kind == AST_AWAIT_EXPR) {
            st->expr_stmt.expr->await_expr.expr =
                anf_expr(&c, st->expr_stmt.expr->await_expr.expr);
            anf_spill_await_receiver(&c, st->expr_stmt.expr);
        } else {
            st->expr_stmt.expr = anf_expr(&c, st->expr_stmt.expr);
        }
        break;
    case AST_RETURN_STMT:
        st->ret.value = anf_expr(&c, st->ret.value);
        break;
    case AST_THROW_STMT:
        st->throw_stmt.value = anf_expr(&c, st->throw_stmt.value);
        break;
    case AST_IF_STMT:
        /* 核心系统底层抽象与内存语义契约 */
        st->if_stmt.cond = anf_expr(&c, st->if_stmt.cond);
        anf_normalize_body(g, &st->if_stmt.then_body, counter);
        anf_normalize_body(g, &st->if_stmt.else_body, counter);
        break;
    case AST_WHILE_STMT:
        /* 循环条件每轮求值：改写为循环体内首部语句，防止跨挂起点丢失状态 */
        if (anf_expr_contains_await(st->while_stmt.cond)) {
            zan_ast_node_t *blk = zan_ast_new(g->arena, AST_BLOCK, st->loc);
            zan_ast_list_init(&blk->block.stmts);
            st->while_stmt.cond = anf_loop_cond_guard(g, &blk->block.stmts,
                counter, &st->while_stmt.cond, &st->loc);
            anf_normalize_body(g, &st->while_stmt.body, counter);
            zan_ast_list_push(&blk->block.stmts, st->while_stmt.body, g->arena);
            st->while_stmt.body = blk;
        } else {
            anf_normalize_body(g, &st->while_stmt.body, counter);
        }
        break;
    case AST_DO_WHILE_STMT:
        /* do-while 循环条件含 await 时的块包装处理 */
        if (anf_expr_contains_await(st->while_stmt.cond)) {
            zan_ast_node_t *cblk = zan_ast_new(g->arena, AST_BLOCK, st->loc);
            zan_ast_list_init(&cblk->block.stmts);
            anf_ctx_t cc = { g, &cblk->block.stmts, counter };
            zan_ast_node_t *c_residual = anf_expr(&cc, st->while_stmt.cond);
            zan_ast_node_t *es = zan_ast_new(g->arena, AST_EXPR_STMT, st->loc);
            es->expr_stmt.expr = c_residual;
            zan_ast_list_push(&cblk->block.stmts, es, g->arena);
            st->while_stmt.cond = cblk;
            anf_normalize_body(g, &st->while_stmt.body, counter);
        } else {
            anf_normalize_body(g, &st->while_stmt.body, counter);
        }
        break;
    case AST_FOR_STMT: {
        bool cond_awaits = anf_expr_contains_await(st->for_stmt.cond);
        bool step_awaits = anf_expr_contains_await(st->for_stmt.step);
        if (cond_awaits) {
            zan_ast_node_t *blk = zan_ast_new(g->arena, AST_BLOCK, st->loc);
            zan_ast_list_init(&blk->block.stmts);
            st->for_stmt.cond = anf_loop_cond_guard(g, &blk->block.stmts,
                counter, &st->for_stmt.cond, &st->loc);
            anf_normalize_body(g, &st->for_stmt.body, counter);
            zan_ast_list_push(&blk->block.stmts, st->for_stmt.body, g->arena);
            st->for_stmt.body = blk;
        } else {
            anf_normalize_body(g, &st->for_stmt.body, counter);
        }
        if (step_awaits) {
            /* 将 for 循环 step 中的提升语句打包进 step 块 */
            zan_ast_node_t *sblk = zan_ast_new(g->arena, AST_BLOCK, st->loc);
            zan_ast_list_init(&sblk->block.stmts);
            anf_ctx_t c2 = { g, &sblk->block.stmts, counter };
            zan_ast_node_t *step = anf_expr(&c2, st->for_stmt.step);
            zan_ast_node_t *es = zan_ast_new(g->arena, AST_EXPR_STMT, st->loc);
            es->expr_stmt.expr = step;
            zan_ast_list_push(&sblk->block.stmts, es, g->arena);
            st->for_stmt.step = sblk;
        }
        break;
    }
    case AST_FOREACH_STMT:
        anf_normalize_body(g, &st->foreach_stmt.body, counter);
        break;
    case AST_LOCK_STMT:
        st->lock_stmt.expr = anf_expr(&c, st->lock_stmt.expr);
        anf_normalize_body(g, &st->lock_stmt.body, counter);
        break;
    case AST_CHECKED_STMT:
        anf_normalize_body(g, &st->checked_stmt.body, counter);
        break;
    case AST_BLOCK:
        anf_normalize_block(g, st, counter);
        break;
    case AST_TRY_STMT:
        anf_normalize_body(g, &st->try_stmt.try_body, counter);
        for (int i = 0; i < st->try_stmt.catches.count; i++)
            anf_normalize_body(g, &st->try_stmt.catches.items[i]->catch_clause.body, counter);
        anf_normalize_body(g, &st->try_stmt.finally_body, counter);
        break;
    case AST_SWITCH_STMT:
        for (int i = 0; i < st->switch_stmt.cases.count; i++)
            anf_normalize_body(g, &st->switch_stmt.cases.items[i]->switch_case.body, counter);
        break;
    default:
        break;
    }
}

/* 重构块语句列表，将提升出的临时变量声明插入在所属语句之前 */
static void anf_normalize_block(zan_irgen_t *g, zan_ast_node_t *block, int *counter) {
    if (!block || block->kind != AST_BLOCK) return;
    zan_ast_list_t nl;
    zan_ast_list_init(&nl);
    for (int i = 0; i < block->block.stmts.count; i++) {
        zan_ast_node_t *st = block->block.stmts.items[i];
        anf_normalize_stmt(g, st, &nl, counter);
        zan_ast_list_push(&nl, st, g->arena);
    }
    block->block.stmts = nl;
}

/* 异步方法需在堆帧中跨挂起点保持的局部变量记录 */
typedef struct {
    zan_istr_t   name;
    LLVMTypeRef  llvm;        /* 核心系统底层抽象与内存语义契约 */
    zan_type_t  *ztype;      /* 底层系统交互与数据协议契约 */
    int          frame_index;
    /* 借用槽标记：帧仅跨挂起点保存位模式而不持有所有权（如 foreach 循环变量） */
    bool         no_arc;
    /* 捕获变量存储标记 cell 所有者，多次 resume 借用同一 cell */
    bool         boxed;
    /* 关联的 AST 声明节点（按节点区分同名遮蔽变量的独立槽位） */
    zan_ast_node_t *decl;
    int role;
} async_local_t;

typedef struct {
    zan_irgen_t   *g;
    int            await_count;
    async_local_t *locals;
    int            local_count;
    int            local_cap;
    /* 局部变量类型推断环境表 */
    local_scope_t *scope;
    /* 编译器代码生成与运行时系统底层调用契约 */
    int            foreach_next;
    /* 待降解 try 块数量（决定帧内异常处理槽数组容量） */
    int            try_count;
    /* 词法 try/finally 与 lock 嵌套深度 */
    int            fin_depth;
    int            fin_depth_max;
    /* 捕获分析所用的异步方法体 AST 节点 */
    zan_ast_node_t *body;
} async_scan_t;

static bool async_type_is_scalar(LLVMTypeRef t) {
    switch (LLVMGetTypeKind(t)) {
    case LLVMIntegerTypeKind:
    case LLVMFloatTypeKind:
    case LLVMDoubleTypeKind:
        return true;
    default:
        return false;
    }
}

/* 为局部变量分配帧内槽位，确保其在挂起点跨调用存活 */
static bool async_type_is_frame_resident(LLVMTypeRef t) {
    return async_type_is_scalar(t) || LLVMGetTypeKind(t) == LLVMPointerTypeKind ||
           LLVMGetTypeKind(t) == LLVMStructTypeKind;
}

static void async_scan_expr(async_scan_t *s, zan_ast_node_t *e);
static void async_scan_stmt(async_scan_t *s, zan_ast_node_t *st);

static int async_scan_add_local_role(async_scan_t *s, zan_istr_t name, LLVMTypeRef llvm,
                                      zan_type_t *zt, zan_ast_node_t *decl, int role) {
    for (int i = 0; i < s->local_count; i++) {
        if (decl) {
            /* 按声明 AST 节点索引：同名遮蔽变量分配独立帧槽位 */
            if (s->locals[i].decl == decl && s->locals[i].role == role) return i;
        } else if (s->locals[i].name.len == name.len &&
                   memcmp(s->locals[i].name.str, name.str, name.len) == 0) {
            return i; /* 底层系统交互与数据协议契约 */
        }
    }
    if (s->local_count >= s->local_cap) {
        int nc = s->local_cap ? s->local_cap * 2 : 8;
        async_local_t *g2 = (async_local_t *)zan_arena_alloc(s->g->arena,
            sizeof(async_local_t) * (size_t)nc);
        if (s->local_count > 0) memcpy(g2, s->locals, sizeof(async_local_t) * (size_t)s->local_count);
        s->locals = g2;
        s->local_cap = nc;
    }
    bool boxed = decl && decl->kind == AST_VAR_DECL && zt &&
        zt->kind != TYPE_OBJECT && zt->kind != TYPE_TYPE_PARAM &&
        (async_type_is_scalar(llvm) || LLVMGetTypeKind(llvm) == LLVMStructTypeKind ||
         (is_rc_managed_type(zt) &&
          LLVMGetTypeKind(llvm) == LLVMPointerTypeKind)) &&
        local_is_lambda_captured(s->g, s->scope, s->body, decl);
    s->locals[s->local_count].name = name;
    s->locals[s->local_count].llvm = boxed
        ? LLVMPointerType(LLVMInt8TypeInContext(s->g->ctx), 0) : llvm;
    s->locals[s->local_count].ztype = zt;
    s->locals[s->local_count].frame_index = -1;
    s->locals[s->local_count].no_arc = false;
    s->locals[s->local_count].boxed = boxed;
    s->locals[s->local_count].decl = decl;
    s->locals[s->local_count].role = role;
    return s->local_count++;
}

static int async_scan_add_local(async_scan_t *s, zan_istr_t name, LLVMTypeRef llvm,
                                zan_type_t *zt, zan_ast_node_t *decl) {
    return async_scan_add_local_role(s, name, llvm, zt, decl, ASYNC_LOCAL_VALUE);
}

/* 分配以 $ 开头的编译器内部帧临时槽，避免与用户变量冲突 */
static zan_istr_t async_synth_name(zan_irgen_t *g, const char *prefix, int n) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "$%s%d", prefix, n);
    char *p = (char *)zan_arena_alloc(g->arena, (size_t)len + 1);
    memcpy(p, buf, (size_t)len + 1);
    zan_istr_t s = { p, (uint32_t)len };
    return s;
}

static void async_scan_add_storage_role(async_scan_t *s, zan_istr_t name,
                                        LLVMTypeRef llvm, zan_ast_node_t *decl,
                                        int role) {
    int index = async_scan_add_local_role(s, name, llvm, NULL, decl, role);
    s->locals[index].no_arc = true;
}

static void async_scan_add_storage_local(async_scan_t *s, zan_istr_t name,
                                         LLVMTypeRef llvm, zan_ast_node_t *decl) {
    async_scan_add_storage_role(s, name, llvm, decl, ASYNC_LOCAL_VALUE);
}

/* 编译器代码生成与运行时系统底层调用契约 */
static void async_scan_note_type(async_scan_t *s, zan_istr_t name, zan_type_t *t) {
    if (s->scope && t) local_add(s->scope, name, NULL, t);
}

/* 模块核心语义抽象与接口调用契约 */
static void async_scan_expr(async_scan_t *s, zan_ast_node_t *e) {
    if (!e) return;
    switch (e->kind) {
    case AST_AWAIT_EXPR:
        s->await_count++;
        async_scan_expr(s, e->await_expr.expr);
        break;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        async_scan_expr(s, e->binary.left);
        async_scan_expr(s, e->binary.right);
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        async_scan_expr(s, e->unary.operand);
        break;
    case AST_CALL:
        async_scan_expr(s, e->call.callee);
        for (int i = 0; i < e->call.args.count; i++)
            async_scan_expr(s, e->call.args.items[i]);
        break;
    case AST_MEMBER_ACCESS:
        async_scan_expr(s, e->member.object);
        break;
    case AST_INDEX:
        async_scan_expr(s, e->index.object);
        async_scan_expr(s, e->index.index);
        break;
    case AST_CONDITIONAL:
        async_scan_expr(s, e->conditional.cond);
        async_scan_expr(s, e->conditional.then_expr);
        async_scan_expr(s, e->conditional.else_expr);
        break;
    case AST_NEW_EXPR:
        for (int i = 0; i < e->new_expr.args.count; i++)
            async_scan_expr(s, e->new_expr.args.items[i]);
        for (int i = 0; i < e->new_expr.arg_inits.count; i++)
            async_scan_expr(s, e->new_expr.arg_inits.items[i]);
        break;
    case AST_COLL_INIT:
        for (int i = 0; i < e->coll_init.items.count; i++)
            async_scan_expr(s, e->coll_init.items.items[i]);
        break;
    case AST_CAST_EXPR:
        async_scan_expr(s, e->cast.expr);
        break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        async_scan_expr(s, e->type_test.expr);
        break;
    default:
        break;
    }
}

/* 统计提前离开 try 块的跳转语句数（用于估算内联 finally 体数量） */
static int async_count_transfers(zan_ast_node_t *st) {
    if (!st) return 0;
    switch (st->kind) {
    case AST_RETURN_STMT:
    case AST_BREAK_STMT:
    case AST_CONTINUE_STMT:
    case AST_THROW_STMT:
        return 1;
    case AST_BLOCK: {
        int n = 0;
        for (int i = 0; i < st->block.stmts.count; i++)
            n += async_count_transfers(st->block.stmts.items[i]);
        return n;
    }
    case AST_IF_STMT:
        return async_count_transfers(st->if_stmt.then_body) +
               async_count_transfers(st->if_stmt.else_body);
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        return async_count_transfers(st->while_stmt.body);
    case AST_FOR_STMT:
        return async_count_transfers(st->for_stmt.body);
    case AST_FOREACH_STMT:
        return async_count_transfers(st->foreach_stmt.body);
    case AST_LOCK_STMT:
        return async_count_transfers(st->lock_stmt.body);
    case AST_CHECKED_STMT:
        return async_count_transfers(st->checked_stmt.body);
    case AST_SWITCH_STMT: {
        int n = 0;
        for (int i = 0; i < st->switch_stmt.cases.count; i++)
            n += async_count_transfers(st->switch_stmt.cases.items[i]->switch_case.body);
        return n;
    }
    case AST_TRY_STMT: {
        int n = async_count_transfers(st->try_stmt.try_body) +
                async_count_transfers(st->try_stmt.finally_body);
        for (int i = 0; i < st->try_stmt.catches.count; i++)
            n += async_count_transfers(st->try_stmt.catches.items[i]->catch_clause.body);
        return n;
    }
    default:
        return 0;
    }
}

/* foreach 迭代协议接口判断（GetEnumerator / MoveNext / Current） */
static zan_type_t *foreach_proto_enum_type(zan_irgen_t *g, zan_type_t *ct) {
    if (!ct || ct->kind != TYPE_CLASS || !ct->sym) return NULL;
    zan_istr_t gi = { (char *)"GetEnumerator", 13 };
    zan_symbol_t *gm = resolve_overload(ct->sym, gi, 0, 0);
    zan_type_t *et = NULL;
    if (gm && gm->decl && gm->decl->kind == AST_METHOD_DECL &&
        gm->decl->method_decl.return_type)
        et = resolve_type_ctx(g, gm->decl->method_decl.return_type);
    if (!et || et->kind != TYPE_CLASS || !et->sym) return NULL;
    zan_istr_t ni = { (char *)"MoveNext", 8 };
    zan_istr_t ci = { (char *)"Current", 7 };
    if (!resolve_overload(et->sym, ni, 0, 0)) return NULL;
    if (resolve_overload(et->sym, ci, 0, 0)) return et;
    for (int i = 0; i < et->sym->member_count; i++) {
        zan_symbol_t *m = et->sym->members[i];
        if (m && m->kind == SYM_PROPERTY && member_name_is(m, ci) &&
            property_getter_sym(g, m))
            return et;
    }
    return NULL;
}

static zan_type_t *foreach_proto_current_type(zan_irgen_t *g, zan_type_t *ct) {
    zan_type_t *et = foreach_proto_enum_type(g, ct);
    if (!et) return NULL;
    zan_istr_t ci = { (char *)"Current", 7 };
    zan_symbol_t *cm = resolve_overload(et->sym, ci, 0, 0);
    if (cm && cm->decl && cm->decl->kind == AST_METHOD_DECL &&
        cm->decl->method_decl.return_type)
        return resolve_type_ctx(g, cm->decl->method_decl.return_type);
    for (int i = 0; i < et->sym->member_count; i++) {
        zan_symbol_t *m = et->sym->members[i];
        if (m && m->kind == SYM_PROPERTY && member_name_is(m, ci)) {
            zan_symbol_t *getter = property_getter_sym(g, m);
            if (getter && getter->decl && getter->decl->kind == AST_METHOD_DECL &&
                getter->decl->method_decl.return_type)
                return resolve_type_ctx(g,
                    getter->decl->method_decl.return_type);
        }
    }
    return NULL;
}

static void async_scan_stmt(async_scan_t *s, zan_ast_node_t *st) {
    if (!st) return;
    switch (st->kind) {
    case AST_BLOCK:
        for (int i = 0; i < st->block.stmts.count; i++)
            async_scan_stmt(s, st->block.stmts.items[i]);
        break;
    case AST_VAR_DECL: {
        /* 推断 var 声明变量类型并登记至帧槽位 */
        zan_type_t *t = st->var_decl.type
            ? resolve_type_ctx(s->g, st->var_decl.type)
            : NULL;
        if ((!t || t->kind == TYPE_ERROR) && st->var_decl.initializer)
            t = infer_expr_type(s->g, st->var_decl.initializer, s->scope);
        if (t && t->kind != TYPE_ERROR) {
            LLVMTypeRef lt = map_type(s->g, t);
            if (async_type_is_frame_resident(lt))
                async_scan_add_local(s, st->var_decl.name, lt, t, st);
            async_scan_note_type(s, st->var_decl.name, t);
        }
        async_scan_expr(s, st->var_decl.initializer);
        break;
    }
    case AST_EXPR_STMT:
        async_scan_expr(s, st->expr_stmt.expr);
        break;
    case AST_RETURN_STMT:
        async_scan_expr(s, st->ret.value);
        break;
    case AST_IF_STMT:
        async_scan_expr(s, st->if_stmt.cond);
        async_scan_stmt(s, st->if_stmt.then_body);
        async_scan_stmt(s, st->if_stmt.else_body);
        break;
    case AST_WHILE_STMT:
        async_scan_expr(s, st->while_stmt.cond);
        async_scan_stmt(s, st->while_stmt.body);
        break;
    case AST_DO_WHILE_STMT:
        if (st->while_stmt.cond && st->while_stmt.cond->kind == AST_BLOCK)
            async_scan_stmt(s, st->while_stmt.cond);
        else
            async_scan_expr(s, st->while_stmt.cond);
        async_scan_stmt(s, st->while_stmt.body);
        break;
    case AST_FOR_STMT:
        async_scan_stmt(s, st->for_stmt.init);
        async_scan_expr(s, st->for_stmt.cond);
        if (st->for_stmt.step && st->for_stmt.step->kind == AST_BLOCK)
            async_scan_stmt(s, st->for_stmt.step);
        else
            async_scan_expr(s, st->for_stmt.step);
        async_scan_stmt(s, st->for_stmt.body);
        break;
    case AST_FOREACH_STMT: {
        async_scan_expr(s, st->foreach_stmt.collection);
        /* foreach 循环变量与迭代状态跨挂起点分配帧槽位 */
        int fe_id = s->foreach_next++;
        zan_type_t *et = st->foreach_stmt.var_type
            ? resolve_type_ctx(s->g, st->foreach_stmt.var_type)
            : NULL;
        if (!et || et->kind == TYPE_ERROR)
            et = foreach_proto_current_type(s->g,
                infer_expr_type(s->g, st->foreach_stmt.collection, s->scope));
        if (!et || et->kind == TYPE_ERROR)
            et = container_elem_type(
                infer_expr_type(s->g, st->foreach_stmt.collection, s->scope));
        if (et && et->kind != TYPE_ERROR) {
            LLVMTypeRef lt = map_type(s->g, et);
            /* 模块核心语义抽象与接口调用契约 */
            if (async_type_is_frame_resident(lt))
                async_scan_add_storage_local(s, st->foreach_stmt.var_name, lt, st);
            async_scan_note_type(s, st->foreach_stmt.var_name, et);
        }
        async_scan_add_storage_role(s, async_synth_name(s->g, "fe.i", fe_id),
            LLVMInt64TypeInContext(s->g->ctx), st, ASYNC_FOREACH_INDEX);
        async_scan_add_storage_role(s, async_synth_name(s->g, "fe.c", fe_id),
            LLVMPointerType(LLVMInt8TypeInContext(s->g->ctx), 0), st,
            ASYNC_FOREACH_COLLECTION);
        if (foreach_proto_enum_type(s->g,
                infer_expr_type(s->g, st->foreach_stmt.collection, s->scope)))
            /* 枚举器对象在整个循环期间驻留帧槽位 */
            async_scan_add_storage_role(s,
                async_synth_name(s->g, "fe.e", fe_id),
                LLVMPointerType(LLVMInt8TypeInContext(s->g->ctx), 0), st,
                ASYNC_FOREACH_ENUMERATOR);
        async_scan_stmt(s, st->foreach_stmt.body);
        break;
    }
    case AST_THROW_STMT:
        async_scan_expr(s, st->throw_stmt.value);
        break;
    case AST_LOCK_STMT:
        async_scan_expr(s, st->lock_stmt.expr);
        /* 模块核心语义抽象与接口调用契约 */
        s->fin_depth++;
        if (s->fin_depth > s->fin_depth_max) s->fin_depth_max = s->fin_depth;
        async_scan_stmt(s, st->lock_stmt.body);
        s->fin_depth--;
        break;
    case AST_CHECKED_STMT:
        async_scan_stmt(s, st->checked_stmt.body);
        break;
    case AST_TRY_STMT:
        s->try_count++;
        if (st->try_stmt.finally_body) {
            async_scan_add_storage_local(s,
                async_synth_name(s->g, "fin.cont", s->try_count),
                LLVMInt32TypeInContext(s->g->ctx), st);
            s->fin_depth++;
            if (s->fin_depth > s->fin_depth_max) s->fin_depth_max = s->fin_depth;
        }
        async_scan_stmt(s, st->try_stmt.try_body);
        for (int i = 0; i < st->try_stmt.catches.count; i++) {
            zan_ast_node_t *cc = st->try_stmt.catches.items[i];
            /* catch 异常绑定变量跨 await 驻留帧槽位（借用所有权） */
            if (cc->catch_clause.var_name.len > 0) {
                async_scan_add_storage_local(s, cc->catch_clause.var_name,
                    LLVMPointerType(LLVMInt8TypeInContext(s->g->ctx), 0), cc);
                if (cc->catch_clause.type)
                    async_scan_note_type(s, cc->catch_clause.var_name,
                        resolve_type_ctx(s->g, cc->catch_clause.type));
            }
            async_scan_stmt(s, cc->catch_clause.body);
        }
        /* 扫描 finally 块中每个退出分支对应副本的 await 槽位需求 */
        if (st->try_stmt.finally_body) {
            int copies = 2 + async_count_transfers(st->try_stmt.try_body);
            for (int i = 0; i < st->try_stmt.catches.count; i++)
                copies += async_count_transfers(
                    st->try_stmt.catches.items[i]->catch_clause.body);
            for (int c = 0; c < copies; c++)
                async_scan_stmt(s, st->try_stmt.finally_body);
        }
        if (st->try_stmt.finally_body) s->fin_depth--;
        break;
    case AST_SWITCH_STMT:
        async_scan_expr(s, st->switch_stmt.expr);
        for (int i = 0; i < st->switch_stmt.cases.count; i++)
            async_scan_stmt(s, st->switch_stmt.cases.items[i]->switch_case.body);
        break;
    default:
        break;
    }
}

static void emit_eh_hook_call(zan_irgen_t *g, const char *name);

/* 异步异常传播：未捕获异常存放于帧 exc_val 槽供 awaiter 唤醒时重抛 */

/* resume 入口发射：挂载蹦床与帧内活跃的异常处理器，随后分发至当前 step 块 */
static void emit_async_eh_prologue(zan_irgen_t *g) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef fn = g->current_async_resume_fn;
    LLVMValueRef frame = g->current_async_frame;
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);
    LLVMValueRef zero = LLVMConstInt(i32, 0, 0);

    LLVMValueRef entry_slot = LLVMBuildAlloca(g->builder, i32, "eh.co.entry");
    g->current_async_eh_entry = entry_slot;
    LLVMBuildStore(g->builder, LLVMBuildLoad2(g->builder, i32, top_g, "eh.top0"),
        entry_slot);

    LLVMBasicBlockRef exc_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.exc");
    LLVMBasicBlockRef disp_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.dispatch");
    g->current_async_exc_bb = exc_bb;

    if (g->current_async_try_count == 0) {
        /* 无内部 try 语句时直接跳转至分发块 */
        LLVMValueRef t = LLVMBuildLoad2(g->builder, i32, top_g, "eh.t");
        LLVMValueRef t1 = zan_add(g->builder, t, LLVMConstInt(i32, 1, 0), "eh.t1");
        LLVMBuildStore(g->builder, t1, top_g);
        LLVMBuildStore(g->builder,
            LLVMBuildLoad2(g->builder, i32, get_eh_tmp_top_global(g), "eh.t0"),
            emit_eh_mark_ptr(g, t1));
        LLVMValueRef r = emit_eh_setjmp(g, emit_eh_buf_ptr(g, t1));
        LLVMValueRef took = zan_icmp(g->builder, LLVMIntEQ, r, zero, "eh.took");
        LLVMBuildCondBr(g->builder, took, disp_bb, exc_bb);

        LLVMPositionBuilderAtEnd(g->builder, disp_bb);
        return;
    }

    LLVMValueRef idx_slot = LLVMBuildAlloca(g->builder, i32, "eh.co.i");
    LLVMValueRef id_slot = LLVMBuildAlloca(g->builder, i32, "eh.co.id");
    LLVMBuildStore(g->builder, zero, idx_slot);

    LLVMBasicBlockRef head_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "eh.rearm");
    LLVMBasicBlockRef arm_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "eh.arm");
    LLVMBasicBlockRef init_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "eh.init");
    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "eh.next");
    LLVMBasicBlockRef land_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "eh.land");

    /* 底层系统交互与数据协议契约 */
    {
        LLVMValueRef t = LLVMBuildLoad2(g->builder, i32, top_g, "eh.t");
        LLVMValueRef t1 = zan_add(g->builder, t, LLVMConstInt(i32, 1, 0), "eh.t1");
        LLVMBuildStore(g->builder, t1, top_g);
        /* 记录处理器挂载时的临时变量栈深度，供异常展开时清理 */
        LLVMBuildStore(g->builder,
            LLVMBuildLoad2(g->builder, i32, get_eh_tmp_top_global(g), "eh.t0"),
            emit_eh_mark_ptr(g, t1));
        LLVMValueRef r = emit_eh_setjmp(g, emit_eh_buf_ptr(g, t1));
        LLVMValueRef took = zan_icmp(g->builder, LLVMIntEQ, r, zero, "eh.took");
        LLVMBuildCondBr(g->builder, took, head_bb, exc_bb);
    }

    /* 模块核心语义抽象与接口调用契约 */
    LLVMPositionBuilderAtEnd(g->builder, head_bb);
    {
        LLVMValueRef i = LLVMBuildLoad2(g->builder, i32, idx_slot, "eh.i");
        LLVMValueRef hc = LLVMBuildLoad2(g->builder, i32,
            LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_HCOUNT, "hc.p"), "hc");
        /* 限制活跃处理器计数不超过帧分配的容量上限 */
        LLVMValueRef cap = LLVMConstInt(i32, g->current_async_handler_cap, 0);
        hc = LLVMBuildSelect(g->builder,
            zan_icmp(g->builder, LLVMIntSLT, hc, cap, "hc.fits"), hc, cap, "hc.cap");
        LLVMValueRef more = zan_icmp(g->builder, LLVMIntSLT, i, hc, "eh.more");
        LLVMBuildCondBr(g->builder, more, arm_bb, disp_bb);
    }

    LLVMPositionBuilderAtEnd(g->builder, arm_bb);
    {
        LLVMValueRef i = LLVMBuildLoad2(g->builder, i32, idx_slot, "eh.i2");
        LLVMValueRef hs_idx[2] = { zero, i };
        LLVMValueRef hs = LLVMBuildStructGEP2(g->builder, ft, frame,
            ASYNC_FRAME_HSTACK, "hs");
        LLVMValueRef slot = LLVMBuildGEP2(g->builder,
            LLVMArrayType(i32, (unsigned)g->current_async_handler_cap),
            hs, hs_idx, 2, "hs.slot");
        LLVMBuildStore(g->builder,
            LLVMBuildLoad2(g->builder, i32, slot, "hs.id"), id_slot);
        LLVMValueRef t = LLVMBuildLoad2(g->builder, i32, top_g, "eh.t2");
        LLVMValueRef t1 = zan_add(g->builder, t, LLVMConstInt(i32, 1, 0), "eh.t3");
        LLVMBuildStore(g->builder, t1, top_g);
        /* 底层系统交互与数据协议契约 */
        LLVMBuildStore(g->builder,
            LLVMBuildLoad2(g->builder, i32, get_eh_tmp_top_global(g), "eh.t3m"),
            emit_eh_mark_ptr(g, t1));
        LLVMValueRef r = emit_eh_setjmp(g, emit_eh_buf_ptr(g, t1));
        LLVMValueRef took = zan_icmp(g->builder, LLVMIntEQ, r, zero, "eh.took2");
        LLVMBuildCondBr(g->builder, took, init_bb, land_bb);
    }

    /* 恢复此前 invocation 挂载的异常处理器标记 */
    LLVMPositionBuilderAtEnd(g->builder, init_bb);
    g->current_async_rearm_next_bb = next_bb;
    g->current_async_rearm_init_switch = LLVMBuildSwitch(g->builder,
        LLVMBuildLoad2(g->builder, i32, id_slot, "eh.id0"), next_bb,
        (unsigned)g->current_async_handler_cap);

    LLVMPositionBuilderAtEnd(g->builder, next_bb);
    {
        LLVMValueRef i = LLVMBuildLoad2(g->builder, i32, idx_slot, "eh.i3");
        LLVMBuildStore(g->builder,
            zan_add(g->builder, i, LLVMConstInt(i32, 1, 0), "eh.i4"), idx_slot);
        LLVMBuildBr(g->builder, head_bb);
    }

    /* 命中恢复挂载的异常处理器：分发至对应 try 块的 catch 分支 */
    LLVMPositionBuilderAtEnd(g->builder, land_bb);
    {
        /* 根据 longjmp 跳转目标深度反查命中的处理器索引 k */
        LLVMValueRef t = LLVMBuildLoad2(g->builder, i32, top_g, "eh.land.top");
        LLVMValueRef e = LLVMBuildLoad2(g->builder, i32, entry_slot, "eh.land.entry");
        LLVMValueRef k = zan_sub(g->builder,
            zan_sub(g->builder, t, e, "eh.land.d"),
            LLVMConstInt(i32, 2, 0), "eh.land.k");
        LLVMValueRef ok = zan_and(g->builder,
            zan_icmp(g->builder, LLVMIntSGE, k, zero, "eh.land.lo"),
            zan_icmp(g->builder, LLVMIntSLT, k,
                LLVMConstInt(i32, g->current_async_handler_cap, 0), "eh.land.hi"),
            "eh.land.ok");
        LLVMValueRef ksafe = LLVMBuildSelect(g->builder, ok, k, zero, "eh.land.ks");
        LLVMBuildStore(g->builder, ksafe, idx_slot);
        LLVMValueRef hs = LLVMBuildStructGEP2(g->builder, ft, frame,
            ASYNC_FRAME_HSTACK, "hs.l");
        LLVMValueRef hs_idx[2] = { zero, ksafe };
        LLVMValueRef slot = LLVMBuildGEP2(g->builder,
            LLVMArrayType(i32, (unsigned)g->current_async_handler_cap),
            hs, hs_idx, 2, "hs.lslot");
        LLVMValueRef id = LLVMBuildSelect(g->builder, ok,
            LLVMBuildLoad2(g->builder, i32, slot, "hs.lid"),
            LLVMConstInt(i32, -1, 1), "eh.land.id");
        LLVMBuildStore(g->builder, id, id_slot);
        LLVMBuildStore(g->builder,
            zan_add(g->builder, ksafe, LLVMConstInt(i32, 1, 0), "eh.land.hc"),
            LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_HCOUNT, "hc.l"));
        /* 展开并释放捕获处理器之上的临时分配变量 */
        emit_eh_unwind_to_handler(g,
            zan_add(g->builder,
                zan_add(g->builder, e, LLVMConstInt(i32, 2, 0), "eh.land.a2"),
                ksafe, "eh.land.arm"));
    }
    /* longjmp 捕获后从帧中重载局部变量最新值供 catch 块读取 */
    g->current_async_rearm_switch = LLVMBuildSwitch(g->builder,
        LLVMBuildLoad2(g->builder, i32, id_slot, "eh.id"), exc_bb,
        (unsigned)g->current_async_handler_cap);

    LLVMPositionBuilderAtEnd(g->builder, disp_bb);
}

/* 蹦床着陆块：将未捕获异常存入帧并完成协程，供 awaiter 重新抛出 */
static void emit_async_exc_epilogue(zan_irgen_t *g, local_scope_t *locals) {
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
    /* 模块核心语义抽象与接口调用契约 */
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), exc_g);
    LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0), get_eh_exc_owned_global(g));
    emit_async_complete(g, locals, NULL);
}

/* 重抛全局异常：跳转至最近挂载的处理器或终止进程 */
static void emit_eh_rethrow_current(zan_irgen_t *g) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMValueRef top = LLVMBuildLoad2(g->builder, i32, top_g, "reh.top");
    LLVMValueRef has = zan_icmp(g->builder, LLVMIntSGE, top,
        LLVMConstInt(i32, 0, 0), "reh.has");
    LLVMBasicBlockRef jmp_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "aeh.jmp");
    LLVMBasicBlockRef die_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "aeh.die");
    /* wasm32 不挂载异常处理器，直接进入未捕获异常终止分支 */
    if (g->target_is_wasm) {
        LLVMBuildBr(g->builder, die_bb);
        LLVMPositionBuilderAtEnd(g->builder, jmp_bb);
        LLVMBuildUnreachable(g->builder);
        LLVMPositionBuilderAtEnd(g->builder, die_bb);
    } else {
        LLVMBuildCondBr(g->builder, has, jmp_bb, die_bb);
        LLVMPositionBuilderAtEnd(g->builder, jmp_bb);
        emit_eh_longjmp(g, emit_eh_buf_ptr(g, top));
        LLVMBuildUnreachable(g->builder);
        LLVMPositionBuilderAtEnd(g->builder, die_bb);
    }
    {
        emit_eh_hook_call(g, "__zan_eh_unhandled");
        LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");
        if (printf_fn) {
            LLVMTypeRef printf_ty = LLVMFunctionType(i32, &i8ptr, 1, 1);
            /* 打印未捕获异常详细类型与调用栈信息 */
            LLVMValueRef exc = LLVMBuildLoad2(g->builder, i8ptr, exc_g, "aeh.exc");
            LLVMValueRef tid = LLVMBuildLoad2(g->builder, i8ptr,
                get_eh_exc_tid_global(g), "aeh.tid");
            LLVMValueRef hasExc = zan_icmp(g->builder, LLVMIntNE, exc,
                LLVMConstNull(i8ptr), "aeh.has");
            LLVMValueRef isStr = zan_icmp(g->builder, LLVMIntEQ, tid,
                LLVMConstNull(i8ptr), "aeh.isstr");
            LLVMBasicBlockRef str_bb =
                LLVMAppendBasicBlockInContext(g->ctx, fn, "aeh.str");
            LLVMBasicBlockRef cls_bb =
                LLVMAppendBasicBlockInContext(g->ctx, fn, "aeh.cls");
            LLVMBasicBlockRef msg_bb =
                LLVMAppendBasicBlockInContext(g->ctx, fn, "aeh.msg");
            LLVMBasicBlockRef plain_bb =
                LLVMAppendBasicBlockInContext(g->ctx, fn, "aeh.plain");
            LLVMBasicBlockRef done_bb =
                LLVMAppendBasicBlockInContext(g->ctx, fn, "aeh.done");
            /* 核心系统底层抽象与内存语义契约 */
            LLVMBuildCondBr(g->builder, hasExc, str_bb, plain_bb);
            LLVMPositionBuilderAtEnd(g->builder, plain_bb);
            {
                LLVMValueRef pfmt = zan_irgen_intern_string(g,
                    "Unhandled exception\n");
                zan_call2(g->builder, printf_ty, printf_fn, &pfmt, 1, "");
                LLVMBuildBr(g->builder, done_bb);
            }
            /* 模块核心语义抽象与接口调用契约 */
            LLVMPositionBuilderAtEnd(g->builder, str_bb);
            LLVMBuildCondBr(g->builder, isStr, msg_bb, cls_bb);
            LLVMPositionBuilderAtEnd(g->builder, msg_bb);
            {
                LLVMValueRef sfmt = zan_irgen_intern_string(g,
                    "Unhandled exception: %s\n");
                LLVMValueRef sargs[2] = { sfmt, exc };
                zan_call2(g->builder, printf_ty, printf_fn, sargs, 2, "");
                LLVMBuildBr(g->builder, done_bb);
            }
            /* 模块核心语义抽象与接口调用契约 */
            LLVMPositionBuilderAtEnd(g->builder, cls_bb);
            {
                LLVMValueRef name_fn = get_eh_tid_name_fn(g);
                LLVMValueRef cname = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    name_fn, (LLVMValueRef[]){ tid }, 1, "aeh.cname");
                LLVMValueRef empty = LLVMBuildICmp(g->builder, LLVMIntEQ,
                    cname, LLVMConstNull(i8ptr), "aeh.noname");
                LLVMValueRef use = LLVMBuildSelect(g->builder, empty,
                    zan_irgen_intern_string(g, "unknown"),
                    cname, "aeh.use");
                LLVMValueRef cfmt = zan_irgen_intern_string(g,
                    "Unhandled exception: %s\n");
                LLVMValueRef cargs[2] = { cfmt, use };
                zan_call2(g->builder, printf_ty, printf_fn, cargs, 2, "");
                LLVMBuildBr(g->builder, done_bb);
            }
            LLVMPositionBuilderAtEnd(g->builder, done_bb);
        }
        LLVMTypeRef exit_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i32, 1, 0);
        LLVMValueRef exit_fn = get_libc_fn(g, "exit", exit_ty);
        LLVMValueRef one = LLVMConstInt(i32, 1, 0);
        zan_call2(g->builder, exit_ty, exit_fn, &one, 1, "");
        LLVMBuildUnreachable(g->builder);
    }

    LLVMBasicBlockRef cont = LLVMAppendBasicBlockInContext(g->ctx, fn, "aeh.cont");
    LLVMPositionBuilderAtEnd(g->builder, cont);
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

    /* 编译器代码生成与运行时系统底层调用契约 */
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
static void emit_async_check_sub_exc(zan_irgen_t *g, LLVMValueRef sub,
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

    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "sub.ok");

    if (!tmp_mark && g->current_async_resume_fn) {
        /* 路由异常处理至共享子重抛块 */
        LLVMBasicBlockRef sub_rethrow_bb = get_async_sub_rethrow_bb(g);
        LLVMAddIncoming(g->current_async_sub_rethrow_phi_sub, &sub, &cur_bb, 1);
        LLVMAddIncoming(g->current_async_sub_rethrow_phi_ev, &ev, &cur_bb, 1);
        LLVMBuildCondBr(g->builder, threw, sub_rethrow_bb, ok_bb);
        LLVMPositionBuilderAtEnd(g->builder, ok_bb);
        return;
    }

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
    /* 编译器代码生成与运行时系统底层调用契约 */
    zan_emit_frame_free(g, sub);
    LLVMBasicBlockRef rethrow_bb = get_async_rethrow_bb(g);
    if (rethrow_bb) {
        LLVMBuildBr(g->builder, rethrow_bb);
    } else {
        emit_eh_rethrow_current(g);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
            LLVMBuildUnreachable(g->builder);
    }

    LLVMPositionBuilderAtEnd(g->builder, ok_bb);
}
