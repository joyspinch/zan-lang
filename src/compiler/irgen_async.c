/* irgen_async.c -- async/await CPS lowering, await A-normal-form normalization and
 * the rc-element array escape analysis.
 *
 * Part of the irgen translation unit: this file is #include'd by irgen.c
 * (in a fixed order) and must not be compiled standalone. Splitting keeps
 * the single-TU static linkage while keeping each concern in its own file.
 */

/* ---- async/await CPS lowering helpers ---- */

/* Does `t` fill the frame result slot with zeros rather than a sign bit? The
 * slot is 64 bits wide, so a narrower value is extended into it and truncated
 * back out; extending an unsigned type with its sign bit would turn `uint`
 * 0xFFFFFFFF into -1 for anything that reads the slot as 64 bits. */
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

/* Encode a completed coroutine's return value into the 64-bit frame result
 * slot, using the callee's declared return type `ty` (may be NULL when it is
 * not known). `coerce_from_frame_result` is the exact inverse: the pair is what
 * makes `await` give back the value the coroutine returned rather than the
 * bits that happened to fit an i64, for a narrow (`short`), unsigned (`uint`)
 * or 32-bit floating (`float`) type -- and for `int` once it is 32 bits (A0). */
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
    case LLVMPointerTypeKind:
        return LLVMBuildPtrToInt(g->builder, v, i64, "res.slot");
    case LLVMDoubleTypeKind:
        return LLVMBuildBitCast(g->builder, v, i64, "res.slot");
    case LLVMFloatTypeKind: {
        /* keep the 32 float bits as they are: widening to double here would
         * make the awaiter's `float` read (a 32-bit bitcast) see the low half
         * of a double instead of the value */
        LLVMValueRef fb = LLVMBuildBitCast(g->builder, v, i32, "res.f32");
        return LLVMBuildZExt(g->builder, fb, i64, "res.slot");
    }
    default:
        return LLVMConstInt(i64, 0, 0);
    }
}

/* Convert a returned value to the async method's declared return type, the way
 * a synchronous `return` converts to the function's LLVM return type. The
 * frame result encoding is type-directed, so the value has to reach it in the
 * declared type -- otherwise `return 0;` from an async `double` method encodes
 * an integer that the awaiter then reads as a double. */
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
        if (wb > hb) return LLVMBuildSExt(g->builder, val, want, "ret.ext");
        if (wb < hb) return LLVMBuildTrunc(g->builder, val, want, "ret.trunc");
        return val;
    }
    if (wk == LLVMPointerTypeKind && hk == LLVMPointerTypeKind)
        return LLVMBuildBitCast(g->builder, val, want, "ret.cast");
    return val;
}

/* Decode a frame result slot back into the callee's declared type `ty`. */
static LLVMValueRef coerce_from_frame_result(zan_irgen_t *g, LLVMValueRef res,
                                             zan_type_t *ty) {
    if (!res || !ty) return res;
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
    default:
        return res;
    }
}

/* Coerce an arbitrary scalar value to the i64 used by the frame result slot,
 * without a declared type to go by (untyped lambda bodies). Prefer
 * coerce_to_frame_result wherever the declared type is available. */
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

/* Emit the completion epilogue of an async $resume body: store the result,
 * mark the frame done, wake a waiting awaiter (if any), then `ret void`.
 * `result_i64` may be NULL for a void async fn. Assumes the builder is
 * positioned at a block without a terminator.
 *
 * The awaiter-wake handshake schedules the frame that awaited us: when a
 * caller `await`s this task it stores itself + its own $resume into our
 * awaiter/awaiter_step header slots (see the await protocol). On completion we
 * re-enqueue that awaiter via zan_co_ready so the cooperative driver re-steps
 * it and it can read our result. A root (non-async) driver leaves awaiter null
 * and instead polls the result after zan_co_sched_run drains. */
static void emit_async_complete(zan_irgen_t *g, local_scope_t *locals, LLVMValueRef result_i64) {
    LLVMValueRef frame = g->current_async_frame;
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);

    LLVMValueRef res_ptr = LLVMBuildStructGEP2(g->builder, ft, frame,
        ASYNC_FRAME_RESULT, "fr.result");
    LLVMBuildStore(g->builder, result_i64 ? result_i64 : LLVMConstInt(i64, 0, 0),
        res_ptr);

    LLVMValueRef done_ptr = LLVMBuildStructGEP2(g->builder, ft, frame,
        ASYNC_FRAME_DONE, "fr.done");
    LLVMBuildStore(g->builder, LLVMConstInt(i32, 1, 0), done_ptr);
    LLVMBuildStore(g->builder, LLVMConstInt(i32, -1, 1),
        LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_STATE, "fr.state"));
    /* a `return` inside a try leaves that try's armed-handler count behind;
     * a completed frame has no live handlers, so reset it for the unwinder */
    LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0),
        LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_HCOUNT, "fr.hc"));

    emit_release_owned_locals(g, locals);
    emit_async_eh_unarm(g);

    /* if (awaiter != null) zan_co_ready(awaiter, awaiter_step); */
    LLVMValueRef aw_ptr = LLVMBuildStructGEP2(g->builder, ft, frame,
        ASYNC_FRAME_AWAITER, "fr.awaiter");
    LLVMValueRef awaiter = LLVMBuildLoad2(g->builder, i8ptr, aw_ptr, "awaiter");
    LLVMValueRef has_awaiter = zan_icmp(g->builder, LLVMIntNE, awaiter,
        LLVMConstNull(i8ptr), "has.awaiter");
    LLVMValueRef fn = g->current_fn;
    LLVMBasicBlockRef wake_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.wake");
    LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.ret");
    LLVMBuildCondBr(g->builder, has_awaiter, wake_bb, ret_bb);

    LLVMPositionBuilderAtEnd(g->builder, wake_bb);
    LLVMValueRef aws_ptr = LLVMBuildStructGEP2(g->builder, ft, frame,
        ASYNC_FRAME_AWAITER_STEP, "fr.awaiter.step");
    LLVMValueRef aw_step = LLVMBuildLoad2(g->builder, g->co_step_ptr, aws_ptr, "awaiter.step");
    LLVMValueRef wake_args[] = { awaiter, aw_step };
    zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, wake_args, 2, "");
    LLVMBuildBr(g->builder, ret_bb);

    LLVMPositionBuilderAtEnd(g->builder, ret_bb);
    LLVMBuildRetVoid(g->builder);
}

/* Head of the module's list of live detached (Task.Spawn) frames, linked
 * through ASYNC_FRAME_LNEXT. Created on first use. */
static LLVMValueRef get_co_live_head(zan_irgen_t *g) {
    LLVMValueRef gv = LLVMGetNamedGlobal(g->mod, "__zan_co_live");
    if (gv) return gv;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    gv = LLVMAddGlobal(g->mod, i8ptr, "__zan_co_live");
    LLVMSetInitializer(gv, LLVMConstNull(i8ptr));
    LLVMSetLinkage(gv, LLVMInternalLinkage);
    return gv;
}

/* Open an internal `void f(i8*)` helper: creates it, positions the builder in a
 * fresh entry block and returns it through *fn. Returns false if it existed. */
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

/* __zan_co_track(f): push a detached frame onto the live list. */
static LLVMValueRef get_co_track_fn(zan_irgen_t *g) {
    LLVMValueRef fn; LLVMBasicBlockRef saved; LLVMValueRef saved_fn;
    if (!open_co_helper(g, "__zan_co_track", &fn, &saved, &saved_fn)) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef head = get_co_live_head(g);
    LLVMValueRef f = LLVMGetParam(fn, 0);
    LLVMValueRef old = LLVMBuildLoad2(g->builder, i8ptr, head, "live.head");
    LLVMBuildStore(g->builder, old,
        LLVMBuildStructGEP2(g->builder, g->co_header_type, f,
                            ASYNC_FRAME_LNEXT, "tr.next"));
    LLVMBuildStore(g->builder, f, head);
    LLVMBuildRetVoid(g->builder);
    close_co_helper(g, saved, saved_fn);
    return fn;
}

/* __zan_co_untrack(f): unlink a frame from the live list (before it is freed). */
static LLVMValueRef get_co_untrack_fn(zan_irgen_t *g) {
    LLVMValueRef fn; LLVMBasicBlockRef saved; LLVMValueRef saved_fn;
    if (!open_co_helper(g, "__zan_co_untrack", &fn, &saved, &saved_fn)) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef hdr = g->co_header_type;
    LLVMValueRef head = get_co_live_head(g);
    LLVMValueRef f = LLVMGetParam(fn, 0);
    LLVMValueRef prev = LLVMBuildAlloca(g->builder, i8ptr, "ut.prev");
    LLVMValueRef cur = LLVMBuildAlloca(g->builder, i8ptr, "ut.cur");
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), prev);
    LLVMBuildStore(g->builder, LLVMBuildLoad2(g->builder, i8ptr, head, "h"), cur);
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "ut.loop");
    LLVMBasicBlockRef test = LLVMAppendBasicBlockInContext(g->ctx, fn, "ut.test");
    LLVMBasicBlockRef hit = LLVMAppendBasicBlockInContext(g->ctx, fn, "ut.hit");
    LLVMBasicBlockRef first = LLVMAppendBasicBlockInContext(g->ctx, fn, "ut.first");
    LLVMBasicBlockRef mid = LLVMAppendBasicBlockInContext(g->ctx, fn, "ut.mid");
    LLVMBasicBlockRef step = LLVMAppendBasicBlockInContext(g->ctx, fn, "ut.step");
    LLVMBasicBlockRef out = LLVMAppendBasicBlockInContext(g->ctx, fn, "ut.out");
    LLVMBuildBr(g->builder, loop);

    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef c = LLVMBuildLoad2(g->builder, i8ptr, cur, "ut.c");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntNE, c, LLVMConstNull(i8ptr), "ut.nn"), test, out);

    LLVMPositionBuilderAtEnd(g->builder, test);
    LLVMValueRef c2 = LLVMBuildLoad2(g->builder, i8ptr, cur, "ut.c2");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, c2, f, "ut.eq"), hit, step);

    LLVMPositionBuilderAtEnd(g->builder, hit);
    LLVMValueRef next = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildStructGEP2(g->builder, hdr, f, ASYNC_FRAME_LNEXT, "ut.np"), "ut.n");
    LLVMValueRef p = LLVMBuildLoad2(g->builder, i8ptr, prev, "ut.p");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, p, LLVMConstNull(i8ptr), "ut.isfirst"),
        first, mid);

    LLVMPositionBuilderAtEnd(g->builder, first);
    LLVMBuildStore(g->builder, next, head);
    LLVMBuildBr(g->builder, out);

    LLVMPositionBuilderAtEnd(g->builder, mid);
    LLVMValueRef p2 = LLVMBuildLoad2(g->builder, i8ptr, prev, "ut.p2");
    LLVMBuildStore(g->builder, next,
        LLVMBuildStructGEP2(g->builder, hdr, p2, ASYNC_FRAME_LNEXT, "ut.pn"));
    LLVMBuildBr(g->builder, out);

    LLVMPositionBuilderAtEnd(g->builder, step);
    LLVMValueRef c3 = LLVMBuildLoad2(g->builder, i8ptr, cur, "ut.c3");
    LLVMBuildStore(g->builder, c3, prev);
    LLVMBuildStore(g->builder, LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildStructGEP2(g->builder, hdr, c3, ASYNC_FRAME_LNEXT, "ut.cn"),
        "ut.cnv"), cur);
    LLVMBuildBr(g->builder, loop);

    LLVMPositionBuilderAtEnd(g->builder, out);
    LLVMBuildRetVoid(g->builder);
    close_co_helper(g, saved, saved_fn);
    return fn;
}

/* Return the module's `__zan_co_cancel(i8*)`, creating it once.
 *
 * Cancellation is cooperative and never touches the scheduler: it only sets the
 * CANCEL flag on the target frame and, following the CHILD links, on the
 * coroutines it is transitively suspended on. Each of those frames observes the
 * flag at its next state block and completes early (see
 * emit_async_cancel_check), so every frame still finishes through the normal
 * completion protocol -- its awaiter is woken, its sub-frame is consumed and
 * freed, and nothing is resumed twice. The cost is that a coroutine parked on a
 * timer or socket wait is only cancelled once that wait completes.
 *
 * The handle comes from Task.Spawn and may name a frame the reaper has already
 * freed, so the root handle is looked up in the live detached-frame list
 * first; the CHILD chain below it is alive by construction (a suspended
 * awaiter owns its sub-frame until it resumes). */
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
    LLVMBasicBlockRef scan_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "cc.scan");
    LLVMBasicBlockRef scmp_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "cc.scmp");
    LLVMBasicBlockRef snext_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "cc.snext");
    LLVMBasicBlockRef head_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "cc.head");
    LLVMBasicBlockRef mark_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "cc.mark");
    LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "cc.ret");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    di_clear(g);
    LLVMValueRef arg = LLVMGetParam(fn, 0);
    LLVMValueRef cur = LLVMBuildAlloca(g->builder, i8ptr, "cc.cur");
    LLVMValueRef scan = LLVMBuildAlloca(g->builder, i8ptr, "cc.scan.p");
    LLVMBuildStore(g->builder, arg, cur);
    LLVMBuildStore(g->builder, LLVMBuildLoad2(g->builder, i8ptr,
        get_co_live_head(g), "cc.live"), scan);
    LLVMBuildBr(g->builder, scan_bb);

    /* is `arg` still a live detached frame? */
    LLVMPositionBuilderAtEnd(g->builder, scan_bb);
    LLVMValueRef s = LLVMBuildLoad2(g->builder, i8ptr, scan, "cc.s");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntNE, s, LLVMConstNull(i8ptr), "cc.snn"),
        scmp_bb, ret_bb);

    LLVMPositionBuilderAtEnd(g->builder, scmp_bb);
    LLVMValueRef s2 = LLVMBuildLoad2(g->builder, i8ptr, scan, "cc.s2");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, s2, arg, "cc.seq"), head_bb, snext_bb);

    LLVMPositionBuilderAtEnd(g->builder, snext_bb);
    LLVMValueRef s3 = LLVMBuildLoad2(g->builder, i8ptr, scan, "cc.s3");
    LLVMBuildStore(g->builder, LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildStructGEP2(g->builder, hdr, s3, ASYNC_FRAME_LNEXT, "cc.sn"),
        "cc.snv"), scan);
    LLVMBuildBr(g->builder, scan_bb);

    /* while (cur != null) { cur->cancel = 1; cur = cur->child; } */
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

/* Emit `if (frame->cancel) <complete with no result>` at a point where the
 * body could just as well have executed `return;`: the locals in scope are
 * released by the completion, the awaiter is woken, and the rest of the body
 * never runs. Emitted at the start of the body and after every statement that
 * awaited, i.e. at each point where cancellation can newly have been
 * requested. */
static void emit_async_cancel_check(zan_irgen_t *g, local_scope_t *locals) {
    if (!g->current_async_frame) return;
    /* Inside a try with a finally, an early completion would skip the finally
     * body. Cancellation is cooperative, so it simply waits for the next
     * statement boundary outside the protected region -- correctness of the
     * unwind machinery beats reacting one statement sooner. */
    if (g->finally_count > 0 || g->catch_cleanup_count > 0) return;
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) return;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef frame = g->current_async_frame;
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMValueRef fn = g->current_fn;
    LLVMValueRef flag = LLVMBuildLoad2(g->builder, i32,
        LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_CANCEL, "fr.cancel"),
        "cancelled");
    LLVMBasicBlockRef can_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.cancelled");
    LLVMBasicBlockRef go_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.notcancelled");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntNE, flag, LLVMConstInt(i32, 0, 0), "is.cancelled"),
        can_bb, go_bb);
    LLVMPositionBuilderAtEnd(g->builder, can_bb);
    emit_async_complete(g, locals, NULL);
    LLVMPositionBuilderAtEnd(g->builder, go_bb);
}

/* Pop every eh handler this $resume invocation armed (its own trampoline plus
 * the user handlers re-armed from the frame). A jmp_buf records a stack frame,
 * so a handler armed by an invocation is unusable once that invocation returns
 * -- leaving it on the eh stack is what made a throw after a suspension jump
 * into a dead frame. Handlers armed by a try that spans the suspension stay
 * recorded in the frame (hcount/hstack) and are re-armed on the next resume. */
static void emit_async_eh_unarm(zan_irgen_t *g) {
    if (!g->current_async_eh_entry) return;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);
    LLVMValueRef entry = LLVMBuildLoad2(g->builder, i32,
        g->current_async_eh_entry, "eh.entry");
    LLVMBuildStore(g->builder, entry, top_g);
}

/* Save all frame-resident slots (params + named scalar locals) from their
 * stack allocas back into the heap frame. Emitted right before a suspend so
 * live values survive across the `ret void`. */
static void emit_async_save_slots(zan_irgen_t *g) {
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMValueRef frame = g->current_async_frame;
    for (int i = 0; i < g->current_async_slot_count; i++) {
        LLVMValueRef v = LLVMBuildLoad2(g->builder, g->current_async_slots[i].llvm,
            g->current_async_slots[i].slot_alloca, "sv");
        LLVMValueRef slot = LLVMBuildStructGEP2(g->builder, ft, frame,
            (unsigned)g->current_async_slots[i].frame_index, "sv.slot");
        LLVMBuildStore(g->builder, v, slot);
    }
}

/* Reload all frame-resident slots from the heap frame into their stack allocas.
 * Emitted at the top of each state block (co.start and every resume-k) so the
 * body reads live values that survived a suspension. */
static void emit_async_reload_slots(zan_irgen_t *g) {
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMValueRef frame = g->current_async_frame;
    for (int i = 0; i < g->current_async_slot_count; i++) {
        LLVMValueRef slot = LLVMBuildStructGEP2(g->builder, ft, frame,
            (unsigned)g->current_async_slots[i].frame_index, "rl.slot");
        LLVMValueRef v = LLVMBuildLoad2(g->builder, g->current_async_slots[i].llvm,
            slot, "rl");
        LLVMBuildStore(g->builder, v, g->current_async_slots[i].slot_alloca);
    }
}

/* ---- await A-normal-form (ANF) normalization ----
 *
 * S3 keeps a value alive across a suspension only when it is a named scalar
 * local (those live in the heap frame and are reloaded at every state block).
 * An intermediate SSA temp produced *before* an await and consumed *after* it
 * does not survive: the resume-k block is entered from the entry switch, so a
 * value computed in the pre-suspend block does not dominate it and LLVM rejects
 * the module ("instruction does not dominate all uses"). This shows up for
 * compound / multiple awaits, e.g. `c + await f()`, `await a() + await b()`,
 * or `h(await a(), await b())`.
 *
 * This pass rewrites each async body into A-normal form for awaits: every
 * `await E` in a linearly-evaluated position becomes its own preceding
 * statement `int $awN = await E;` and the original occurrence is replaced by a
 * reference to `$awN`. Because `$awN` is a named scalar local it is made
 * frame-resident by async_scan and reloaded after the suspension, so no value
 * crosses a suspend point in a register. Awaits inside short-circuit (`&&`,
 * `||`) and conditional (`?:`) operands, and inside loop conditions/steps, are
 * left in place (their existing control-flow lowering handles them and hoisting
 * would change evaluation semantics). */

typedef struct {
    zan_irgen_t    *g;
    zan_ast_list_t *out;  /* hoisted statements, appended in evaluation order */
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
        return false;
    }
    case AST_CAST_EXPR: return anf_expr_contains_await(e->cast.expr);
    case AST_IS_EXPR:
    case AST_AS_EXPR:  return anf_expr_contains_await(e->type_test.expr);
    default: return false;
    }
}

/* A side-effecting operand (a call, assignment, or ++/--) evaluated *before* an
 * await in the same operand list would be reordered to run *after* the await if
 * we only hoist the await (it stays in the residual). Detect that to reject it
 * with a clear message instead of silently changing evaluation order. Awaits
 * themselves are hoisted in order, so they are not counted here. */
static bool anf_expr_has_side_effect(zan_ast_node_t *e) {
    if (!e) return false;
    switch (e->kind) {
    case AST_AWAIT_EXPR: return false; /* hoisted separately, order preserved */
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

/* If operand `before` (evaluated first) has side effects and a later operand
 * `after` contains an await, hoisting only the await reorders them. Flag it. */
static void anf_check_order(anf_ctx_t *c, zan_ast_node_t *before, zan_ast_node_t *after) {
    if (after && before && anf_expr_contains_await(after) &&
        anf_expr_has_side_effect(before)) {
        zan_diag_emit(c->g->diag, DIAG_ERROR, before->loc,
            "async: an expression with side effects is evaluated before an "
            "await in the same expression; assign it to a local first");
    }
}

/* Hoist `await E` into `var $awN = await E;` and return a reference to $awN.
 *
 * The temp is inferred, not `int`: an awaited call can yield a string, a list
 * or a class instance, and typing the temp `int` made the residual expression
 * see the reference as a number (`"n=" + await F()` printed a pointer). */
static zan_ast_node_t *anf_hoist_await(anf_ctx_t *c, zan_ast_node_t *aw) {
    /* normalize any nested awaits inside the awaited expression first */
    aw->await_expr.expr = anf_expr(c, aw->await_expr.expr);

    char buf[32];
    int len = snprintf(buf, sizeof buf, "$aw%d", (*c->counter)++);
    char *nm = (char *)zan_arena_alloc(c->g->arena, (size_t)len + 1);
    memcpy(nm, buf, (size_t)len + 1);
    zan_istr_t name = { nm, (uint32_t)len };

    zan_ast_node_t *vd = zan_ast_new(c->g->arena, AST_VAR_DECL, aw->loc);
    vd->var_decl.name = name;
    vd->var_decl.type = NULL; /* inferred from the awaited expression */
    vd->var_decl.initializer = aw;
    zan_ast_list_push(c->out, vd, c->g->arena);

    zan_ast_node_t *id = zan_ast_new(c->g->arena, AST_IDENTIFIER, aw->loc);
    id->ident.name = name;
    return id;
}

/* Recursively lift awaits out of a linearly-evaluated expression, appending
 * hoisted `$awN` declarations to c->out in evaluation order and returning the
 * residual expression (which no longer contains any hoistable await). */
static zan_ast_node_t *anf_expr(anf_ctx_t *c, zan_ast_node_t *e) {
    if (!e) return e;
    switch (e->kind) {
    case AST_AWAIT_EXPR:
        return anf_hoist_await(c, e);
    case AST_BINARY:
    case AST_ASSIGNMENT:
        if (e->binary.op == TK_AMP_AMP || e->binary.op == TK_PIPE_PIPE) {
            /* short-circuit: only the left operand is unconditional. */
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
        /* only the guard is unconditional; leave branch awaits in place. */
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

/* Does a statement subtree contain any await? Used to decide whether a
 * single-statement body must be wrapped in a block for hoisting. */
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
    case AST_BLOCK: {
        for (int i = 0; i < st->block.stmts.count; i++)
            if (anf_stmt_contains_await(st->block.stmts.items[i])) return true;
        return false;
    }
    default: return false;
    }
}

/* Ensure `*slot` is an AST_BLOCK so hoisted statements can be inserted before
 * the awaits it contains, then normalize it. Used for single-statement bodies
 * (e.g. `if (c) return await f();`). */
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

/* Normalize one statement, appending any hoisted declarations to `dst` (in
 * evaluation order) *before* the statement is pushed by the caller. Nested
 * statement bodies are normalized recursively. */
static void anf_normalize_stmt(zan_irgen_t *g, zan_ast_node_t *st,
                               zan_ast_list_t *dst, int *counter) {
    if (!st) return;
    anf_ctx_t c = { g, dst, counter };
    switch (st->kind) {
    case AST_VAR_DECL:
        /* `T x = await E;` is already at statement position: keep the await
         * as the initializer instead of hoisting it into an `int $awN` temp,
         * which would erase its result type -- an inferred declaration
         * (`var s = await Echo()`) would then hold a string as an integer. */
        if (st->var_decl.initializer &&
            st->var_decl.initializer->kind == AST_AWAIT_EXPR) {
            st->var_decl.initializer->await_expr.expr =
                anf_expr(&c, st->var_decl.initializer->await_expr.expr);
        } else {
            st->var_decl.initializer = anf_expr(&c, st->var_decl.initializer);
        }
        break;
    case AST_EXPR_STMT:
        /* A bare `await E;` is already at statement position, so keep the await
         * in place (only normalize awaits nested inside E) instead of hoisting
         * it into an `int $awN` temp. Hoisting would erase the real result type
         * (the temp is always typed `int`), so a discarded owned rc result
         * (string/object) would never be released and would leak. Emitted
         * directly, the expr-statement discard path releases it by its true
         * type. */
        if (st->expr_stmt.expr && st->expr_stmt.expr->kind == AST_AWAIT_EXPR) {
            st->expr_stmt.expr->await_expr.expr =
                anf_expr(&c, st->expr_stmt.expr->await_expr.expr);
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
        /* condition is evaluated once → safe to hoist */
        st->if_stmt.cond = anf_expr(&c, st->if_stmt.cond);
        anf_normalize_body(g, &st->if_stmt.then_body, counter);
        anf_normalize_body(g, &st->if_stmt.else_body, counter);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        /* condition is re-evaluated each iteration → must NOT hoist it out */
        anf_normalize_body(g, &st->while_stmt.body, counter);
        break;
    case AST_FOR_STMT:
        anf_normalize_body(g, &st->for_stmt.body, counter);
        break;
    case AST_FOREACH_STMT:
        anf_normalize_body(g, &st->foreach_stmt.body, counter);
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

/* Rebuild a block's statement list, inserting hoisted `$awN` declarations
 * before each statement that needed them. */
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

/* A named scalar local of an async method that must live in the heap frame so
 * its value survives across suspensions. `frame_index` is assigned when the
 * frame struct is laid out (params first, then these locals). */
typedef struct {
    zan_istr_t   name;
    LLVMTypeRef  llvm;        /* slot element type */
    zan_type_t  *ztype;      /* zan type (for identifier load typing) */
    int          frame_index;
    /* storage-only slot: the frame preserves the bits across suspensions but
     * does not own the value. A `foreach` loop variable borrows its element
     * from the collection, so the coroutine's cleanup must not release it. */
    bool         no_arc;
} async_local_t;

typedef struct {
    zan_irgen_t   *g;
    int            await_count;
    async_local_t *locals;
    int            local_count;
    int            local_cap;
    /* Types of the locals seen so far, so a `var` declaration's type can be
     * inferred here exactly as it will be at emit time. Alloca-less: only the
     * `type` field is read (by infer_expr_type). */
    local_scope_t *scope;
    /* id of the next `foreach`, in the same AST order the emitter walks */
    int            foreach_next;
    /* how many try statements the emitter will lower, counting a try inside a
     * finally body once per copy of that body -- the frame's per-handler slot
     * arrays are sized from this */
    int            try_count;
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

/* A local must be frame-resident (saved to / reloaded from the heap frame
 * around each suspension) if it can be live across an await. Scalars and any
 * pointer-shaped value (string, array, List/Dict, class instances — all lowered
 * to a pointer here) qualify; the save/reload just relocates the bits and never
 * touches a refcount, so ARC ownership (release on throw-cleanup / scope exit,
 * driven off the reloaded alloca) is unaffected. Aggregates passed by value are
 * left alone (they do not cross suspends in the current lowering). */
static bool async_type_is_frame_resident(LLVMTypeRef t) {
    return async_type_is_scalar(t) || LLVMGetTypeKind(t) == LLVMPointerTypeKind;
}

static void async_scan_expr(async_scan_t *s, zan_ast_node_t *e);
static void async_scan_stmt(async_scan_t *s, zan_ast_node_t *st);

static void async_scan_add_local(async_scan_t *s, zan_istr_t name, LLVMTypeRef llvm, zan_type_t *zt) {
    for (int i = 0; i < s->local_count; i++) {
        if (s->locals[i].name.len == name.len &&
            memcmp(s->locals[i].name.str, name.str, name.len) == 0) {
            return; /* dedup by name (flat scope) */
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
    s->locals[s->local_count].name = name;
    s->locals[s->local_count].llvm = llvm;
    s->locals[s->local_count].ztype = zt;
    s->locals[s->local_count].frame_index = -1;
    s->locals[s->local_count].no_arc = false;
    s->local_count++;
}

/* A compiler-generated frame local. The `$` keeps it out of the identifier
 * namespace, so it can never collide with (or shadow) a user local. */
static zan_istr_t async_synth_name(zan_irgen_t *g, const char *prefix, int n) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "$%s%d", prefix, n);
    char *p = (char *)zan_arena_alloc(g->arena, (size_t)len + 1);
    memcpy(p, buf, (size_t)len + 1);
    zan_istr_t s = { p, (uint32_t)len };
    return s;
}

static void async_scan_add_storage_local(async_scan_t *s, zan_istr_t name,
                                         LLVMTypeRef llvm) {
    async_scan_add_local(s, name, llvm, NULL);
    if (s->local_count > 0) s->locals[s->local_count - 1].no_arc = true;
}

/* Record a local's type for the scan's own inference (no storage yet). */
static void async_scan_note_type(async_scan_t *s, zan_istr_t name, zan_type_t *t) {
    if (s->scope && t) local_add(s->scope, name, NULL, t);
}

/* Walk expressions to count await points (state-machine transitions). */
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

/* Count the statements that leave a try region early (each emits its own inline
 * copy of the enclosing finally body, see emit_pending_finallys). Nested
 * function bodies are not walked -- they carry their own finallys. */
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

/* Walk statements to collect named scalar locals (which must live in the frame)
 * and count await points anywhere in the body. */
static void async_scan_stmt(async_scan_t *s, zan_ast_node_t *st) {
    if (!st) return;
    switch (st->kind) {
    case AST_BLOCK:
        for (int i = 0; i < st->block.stmts.count; i++)
            async_scan_stmt(s, st->block.stmts.items[i]);
        break;
    case AST_VAR_DECL: {
        /* `var x = e` must reach the frame just like `T x = e`: infer its type
         * here with the same inference the emitter uses. Skipping inferred
         * declarations left them stack-only, so their value was garbage after
         * any suspension. */
        /* resolved in the specialization's context, so a `T`/`U` local in a
         * monomorphized async body gets its concrete frame slot */
        zan_type_t *t = st->var_decl.type
            ? resolve_type_ctx(s->g, st->var_decl.type)
            : NULL;
        if ((!t || t->kind == TYPE_ERROR) && st->var_decl.initializer)
            t = infer_expr_type(s->g, st->var_decl.initializer, s->scope);
        if (t && t->kind != TYPE_ERROR) {
            LLVMTypeRef lt = map_type(s->g, t);
            if (async_type_is_frame_resident(lt))
                async_scan_add_local(s, st->var_decl.name, lt, t);
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
    case AST_DO_WHILE_STMT:
        async_scan_expr(s, st->while_stmt.cond);
        async_scan_stmt(s, st->while_stmt.body);
        break;
    case AST_FOR_STMT:
        async_scan_stmt(s, st->for_stmt.init);
        async_scan_expr(s, st->for_stmt.cond);
        async_scan_expr(s, st->for_stmt.step);
        async_scan_stmt(s, st->for_stmt.body);
        break;
    case AST_FOREACH_STMT: {
        async_scan_expr(s, st->foreach_stmt.collection);
        /* A `foreach` resumed inside its body re-enters at a block the loop
         * pre-header never reaches, so the whole iteration state -- element,
         * index and collection -- lives in the frame. The collection itself is
         * kept (rather than the data/count pair derived from it) so every
         * iteration reloads them from the live list. */
        int fe_id = s->foreach_next++;
        zan_type_t *et = st->foreach_stmt.var_type
            ? resolve_type_ctx(s->g, st->foreach_stmt.var_type)
            : NULL;
        if (!et || et->kind == TYPE_ERROR)
            et = container_elem_type(
                infer_expr_type(s->g, st->foreach_stmt.collection, s->scope));
        if (et && et->kind != TYPE_ERROR) {
            LLVMTypeRef lt = map_type(s->g, et);
            /* the element is borrowed from the collection: storage only */
            if (async_type_is_frame_resident(lt))
                async_scan_add_storage_local(s, st->foreach_stmt.var_name, lt);
            async_scan_note_type(s, st->foreach_stmt.var_name, et);
        }
        async_scan_add_storage_local(s, async_synth_name(s->g, "fe.i", fe_id),
            LLVMInt64TypeInContext(s->g->ctx));
        async_scan_add_storage_local(s, async_synth_name(s->g, "fe.c", fe_id),
            LLVMPointerType(LLVMInt8TypeInContext(s->g->ctx), 0));
        async_scan_stmt(s, st->foreach_stmt.body);
        break;
    }
    case AST_THROW_STMT:
        async_scan_expr(s, st->throw_stmt.value);
        break;
    case AST_TRY_STMT:
        s->try_count++;
        async_scan_stmt(s, st->try_stmt.try_body);
        for (int i = 0; i < st->try_stmt.catches.count; i++) {
            zan_ast_node_t *cc = st->try_stmt.catches.items[i];
            /* the caught exception binding outlives an await in the handler,
             * so it is frame-resident too; the frame's per-handler exception
             * slot owns the object, this binding only borrows it */
            if (cc->catch_clause.var_name.len > 0) {
                async_scan_add_storage_local(s, cc->catch_clause.var_name,
                    LLVMPointerType(LLVMInt8TypeInContext(s->g->ctx), 0));
                if (cc->catch_clause.type)
                    async_scan_note_type(s, cc->catch_clause.var_name,
                        resolve_type_ctx(s->g, cc->catch_clause.type));
            }
            async_scan_stmt(s, cc->catch_clause.body);
        }
        /* The finally body is emitted once per exit path out of this try, and
         * every copy of an `await` inside it needs its own frame sub-slot, so
         * scan it once per copy: the normal exit, the exception path, and one
         * for each early transfer in the guarded/handler bodies. Over-counting
         * only costs frame bytes; under-counting hands out sub-slot indices past
         * the end of the frame type. */
        if (st->try_stmt.finally_body) {
            int copies = 2 + async_count_transfers(st->try_stmt.try_body);
            for (int i = 0; i < st->try_stmt.catches.count; i++)
                copies += async_count_transfers(
                    st->try_stmt.catches.items[i]->catch_clause.body);
            for (int c = 0; c < copies; c++)
                async_scan_stmt(s, st->try_stmt.finally_body);
        }
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

/* ---- rc-element array escape analysis ----
 * A `new T[n]` array of rc elements is released element-by-element at scope
 * exit. That is only sound when the buffer does not outlive the scope, i.e. it
 * never escapes: used only as an index base `a[i]` or a borrowed member read
 * `a.Length`. Any other use of the bare identifier (return, assignment RHS,
 * call argument, collection/field store, ...) shares the pointer, so we skip
 * the release for it (leak rather than risk a double-free). */
static int arr_ident_named(zan_ast_node_t *e, zan_istr_t nm) {
    return e && e->kind == AST_IDENTIFIER &&
           e->ident.name.len == nm.len &&
           (nm.len == 0 || memcmp(e->ident.name.str, nm.str, (size_t)nm.len) == 0);
}
static void arr_escape_stmt(zan_istr_t nm, zan_ast_node_t *st, int *esc);
static void arr_escape_expr(zan_istr_t nm, zan_ast_node_t *e, int *esc) {
    if (!e || *esc) return;
    switch (e->kind) {
    case AST_IDENTIFIER:
        if (arr_ident_named(e, nm)) *esc = 1;
        break;
    case AST_INDEX:
        if (!arr_ident_named(e->index.object, nm))
            arr_escape_expr(nm, e->index.object, esc);
        arr_escape_expr(nm, e->index.index, esc);
        break;
    case AST_MEMBER_ACCESS:
        if (!arr_ident_named(e->member.object, nm))
            arr_escape_expr(nm, e->member.object, esc);
        break;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        arr_escape_expr(nm, e->binary.left, esc);
        arr_escape_expr(nm, e->binary.right, esc);
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        arr_escape_expr(nm, e->unary.operand, esc);
        break;
    case AST_CALL:
        arr_escape_expr(nm, e->call.callee, esc);
        for (int i = 0; i < e->call.args.count; i++)
            arr_escape_expr(nm, e->call.args.items[i], esc);
        break;
    case AST_CONDITIONAL:
        arr_escape_expr(nm, e->conditional.cond, esc);
        arr_escape_expr(nm, e->conditional.then_expr, esc);
        arr_escape_expr(nm, e->conditional.else_expr, esc);
        break;
    case AST_NEW_EXPR:
        for (int i = 0; i < e->new_expr.args.count; i++)
            arr_escape_expr(nm, e->new_expr.args.items[i], esc);
        break;
    case AST_CAST_EXPR:
        arr_escape_expr(nm, e->cast.expr, esc);
        break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        arr_escape_expr(nm, e->type_test.expr, esc);
        break;
    case AST_AWAIT_EXPR:
        arr_escape_expr(nm, e->await_expr.expr, esc);
        break;
    default:
        break;
    }
}
static void arr_escape_stmt(zan_istr_t nm, zan_ast_node_t *st, int *esc) {
    if (!st || *esc) return;
    switch (st->kind) {
    case AST_BLOCK:
        for (int i = 0; i < st->block.stmts.count; i++)
            arr_escape_stmt(nm, st->block.stmts.items[i], esc);
        break;
    case AST_VAR_DECL:
        arr_escape_expr(nm, st->var_decl.initializer, esc);
        break;
    case AST_EXPR_STMT:
        arr_escape_expr(nm, st->expr_stmt.expr, esc);
        break;
    case AST_RETURN_STMT:
        arr_escape_expr(nm, st->ret.value, esc);
        break;
    case AST_IF_STMT:
        arr_escape_expr(nm, st->if_stmt.cond, esc);
        arr_escape_stmt(nm, st->if_stmt.then_body, esc);
        arr_escape_stmt(nm, st->if_stmt.else_body, esc);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        arr_escape_expr(nm, st->while_stmt.cond, esc);
        arr_escape_stmt(nm, st->while_stmt.body, esc);
        break;
    case AST_FOR_STMT:
        arr_escape_stmt(nm, st->for_stmt.init, esc);
        arr_escape_expr(nm, st->for_stmt.cond, esc);
        arr_escape_expr(nm, st->for_stmt.step, esc);
        arr_escape_stmt(nm, st->for_stmt.body, esc);
        break;
    case AST_FOREACH_STMT:
        arr_escape_expr(nm, st->foreach_stmt.collection, esc);
        arr_escape_stmt(nm, st->foreach_stmt.body, esc);
        break;
    case AST_THROW_STMT:
        arr_escape_expr(nm, st->throw_stmt.value, esc);
        break;
    case AST_TRY_STMT:
        arr_escape_stmt(nm, st->try_stmt.try_body, esc);
        for (int i = 0; i < st->try_stmt.catches.count; i++)
            arr_escape_stmt(nm, st->try_stmt.catches.items[i]->catch_clause.body, esc);
        arr_escape_stmt(nm, st->try_stmt.finally_body, esc);
        break;
    case AST_SWITCH_STMT:
        arr_escape_expr(nm, st->switch_stmt.expr, esc);
        for (int i = 0; i < st->switch_stmt.cases.count; i++)
            arr_escape_stmt(nm, st->switch_stmt.cases.items[i]->switch_case.body, esc);
        break;
    default:
        break;
    }
}
static int rc_array_local_escapes(zan_irgen_t *g, zan_istr_t nm) {
    int esc = 0;
    if (g->current_fn_body) arr_escape_stmt(nm, g->current_fn_body, &esc);
    else esc = 1; /* unknown body: conservative */
    return esc;
}


static void emit_eh_hook_call(zan_irgen_t *g, const char *name);

/* ---- async exception propagation -----------------------------------------
 * A coroutine cannot longjmp into the frame that awaits it: that frame's
 * invocation returned to the scheduler at the suspension. Instead each
 * $resume invocation arms one trampoline handler around its whole body; an
 * exception that escapes the body lands there, is parked in the frame's
 * exception slots, and completes the coroutine. The awaiting frame finds it
 * at its resume point and re-throws it in its own (live) invocation. */

/* Arm the trampoline plus every handler recorded in the frame, then leave the
 * builder in the block where the state dispatch belongs. Emitted at the top of
 * each $resume invocation, after the frame slots have been set up. */
static void emit_async_eh_prologue(zan_irgen_t *g) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef fn = g->current_async_resume_fn;
    LLVMValueRef frame = g->current_async_frame;
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);
    LLVMValueRef zero = LLVMConstInt(i32, 0, 0);

    LLVMValueRef entry_slot = LLVMBuildAlloca(g->builder, i32, "eh.co.entry");
    LLVMValueRef idx_slot = LLVMBuildAlloca(g->builder, i32, "eh.co.i");
    LLVMValueRef id_slot = LLVMBuildAlloca(g->builder, i32, "eh.co.id");
    g->current_async_eh_entry = entry_slot;
    LLVMBuildStore(g->builder, LLVMBuildLoad2(g->builder, i32, top_g, "eh.top0"),
        entry_slot);
    LLVMBuildStore(g->builder, zero, idx_slot);

    LLVMBasicBlockRef exc_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.exc");
    LLVMBasicBlockRef head_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "eh.rearm");
    LLVMBasicBlockRef arm_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "eh.arm");
    LLVMBasicBlockRef init_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "eh.init");
    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "eh.next");
    LLVMBasicBlockRef land_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "eh.land");
    LLVMBasicBlockRef disp_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.dispatch");
    g->current_async_exc_bb = exc_bb;

    /* trampoline: the outermost handler of this invocation */
    {
        LLVMValueRef t = LLVMBuildLoad2(g->builder, i32, top_g, "eh.t");
        LLVMValueRef t1 = zan_add(g->builder, t, LLVMConstInt(i32, 1, 0), "eh.t1");
        LLVMBuildStore(g->builder, t1, top_g);
        LLVMValueRef r = emit_eh_setjmp(g, emit_eh_buf_ptr(g, t1));
        LLVMValueRef took = zan_icmp(g->builder, LLVMIntEQ, r, zero, "eh.took");
        LLVMBuildCondBr(g->builder, took, head_bb, exc_bb);
    }

    /* re-arm the handlers of the tries this frame is currently inside */
    LLVMPositionBuilderAtEnd(g->builder, head_bb);
    {
        LLVMValueRef i = LLVMBuildLoad2(g->builder, i32, idx_slot, "eh.i");
        LLVMValueRef hc = LLVMBuildLoad2(g->builder, i32,
            LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_HCOUNT, "hc.p"), "hc");
        /* the frame has one slot per try in this body, so hc can only exceed it
         * if the bookkeeping is broken; clamp rather than read past the frame */
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
        LLVMValueRef r = emit_eh_setjmp(g, emit_eh_buf_ptr(g, t1));
        LLVMValueRef took = zan_icmp(g->builder, LLVMIntEQ, r, zero, "eh.took2");
        LLVMBuildCondBr(g->builder, took, init_bb, land_bb);
    }

    /* the try's own entry code ran in an earlier invocation, so its eh
     * bookkeeping allocas are uninitialised here: let the try fill them in */
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

    /* a re-armed handler caught: hand control to that try's catch (the cases
     * are added by the try lowering, which owns the catch blocks) */
    LLVMPositionBuilderAtEnd(g->builder, land_bb);
    {
        /* Which of the re-armed handlers was jumped to: a thrower longjmps to
         * bufs[__zan_eh_top], and handler k of this invocation was armed at
         * entry + 2 + k (entry + 1 is the trampoline). id_slot still holds the
         * id armed *last*, which is the innermost handler -- dispatching on it
         * would send an exception raised inside a catch body back into the try
         * it just left. Recover k from the top instead, and truncate the
         * handler count so the handlers armed inside the one that caught are
         * dropped (the catch entry pops the catching handler itself). */
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
    }
    /* No reload here: a longjmp only ever reaches a handler armed by the
     * invocation it is raised in (handlers of finished invocations are unarmed
     * at every suspension), and this invocation's state block already loaded
     * the frame-resident slots. Their stack allocas therefore hold the live
     * values, including the ones written after the last suspension -- copying
     * the frame over them would undo those writes and lose the references
     * they hold. */
    g->current_async_rearm_switch = LLVMBuildSwitch(g->builder,
        LLVMBuildLoad2(g->builder, i32, id_slot, "eh.id"), exc_bb,
        (unsigned)g->current_async_handler_cap);

    LLVMPositionBuilderAtEnd(g->builder, disp_bb);
}

/* Fill the trampoline landing block: park the in-flight exception in the frame
 * and complete the coroutine, so the awaiter can re-throw it. */
static void emit_async_exc_epilogue(zan_irgen_t *g, local_scope_t *locals) {
    if (!g->current_async_exc_bb) return;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef frame = g->current_async_frame;
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);

    LLVMPositionBuilderAtEnd(g->builder, g->current_async_exc_bb);
    /* the allocas are the live copy here too (see eh.land above): the cleanup
     * emit_async_complete runs must see the locals this invocation assigned */
    LLVMBuildStore(g->builder, LLVMBuildLoad2(g->builder, i8ptr, exc_g, "exc.v"),
        LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_EXC, "fr.exc"));
    LLVMBuildStore(g->builder,
        LLVMBuildLoad2(g->builder, i8ptr, get_eh_exc_tid_global(g), "exc.tid"),
        LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_EXC_TID, "fr.exc.tid"));
    LLVMBuildStore(g->builder,
        LLVMBuildLoad2(g->builder, i32, get_eh_exc_owned_global(g), "exc.own"),
        LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_EXC_OWNED, "fr.exc.own"));
    /* the exception now travels in the frame, not in the globals */
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), exc_g);
    LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0), get_eh_exc_owned_global(g));
    emit_async_complete(g, locals, NULL);
}

/* Re-throw the exception the globals currently hold: jump to the innermost
 * armed handler, or report it as unhandled when none is left. */
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
    LLVMBuildCondBr(g->builder, has, jmp_bb, die_bb);

    LLVMPositionBuilderAtEnd(g->builder, jmp_bb);
    {
        emit_eh_longjmp(g, emit_eh_buf_ptr(g, top));
        LLVMBuildUnreachable(g->builder);
    }

    LLVMPositionBuilderAtEnd(g->builder, die_bb);
    {
        emit_eh_hook_call(g, "__zan_eh_unhandled");
        LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");
        if (printf_fn) {
            LLVMTypeRef printf_ty = LLVMFunctionType(i32, &i8ptr, 1, 1);
            LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder,
                "Unhandled exception\n", "aexfmt");
            zan_call2(g->builder, printf_ty, printf_fn, &fmt, 1, "");
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

/* At an await resume point: if the awaited coroutine completed by throwing,
 * move its exception back into the globals and re-throw it here, inside a live
 * invocation of this frame. `sub` is the (still owned) sub-frame handle. */
static void emit_async_check_sub_exc(zan_irgen_t *g, LLVMValueRef sub) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef hdr = g->co_header_type;
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));

    LLVMValueRef ep = LLVMBuildStructGEP2(g->builder, hdr, sub, ASYNC_FRAME_EXC, "sub.exc.p");
    LLVMValueRef ev = LLVMBuildLoad2(g->builder, i8ptr, ep, "sub.exc");
    LLVMValueRef tp = LLVMBuildStructGEP2(g->builder, hdr, sub, ASYNC_FRAME_EXC_TID, "sub.exc.tid.p");
    LLVMValueRef tv = LLVMBuildLoad2(g->builder, i8ptr, tp, "sub.exc.tid");
    LLVMValueRef op = LLVMBuildStructGEP2(g->builder, hdr, sub, ASYNC_FRAME_EXC_OWNED, "sub.exc.own.p");
    LLVMValueRef ov = LLVMBuildLoad2(g->builder, i32, op, "sub.exc.own");
    LLVMValueRef threw = zan_icmp(g->builder, LLVMIntNE, ev,
        LLVMConstNull(i8ptr), "sub.threw");
    LLVMBasicBlockRef thr_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "sub.rethrow");
    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "sub.ok");
    LLVMBuildCondBr(g->builder, threw, thr_bb, ok_bb);

    LLVMPositionBuilderAtEnd(g->builder, thr_bb);
    LLVMBuildStore(g->builder, ev, exc_g);
    LLVMBuildStore(g->builder, tv, get_eh_exc_tid_global(g));
    LLVMBuildStore(g->builder, ov, get_eh_exc_owned_global(g));
    /* the sub-frame is dead once its exception has been taken over */
    zan_call2(g->builder, LLVMGlobalGetValueType(g->fn_free), g->fn_free, &sub, 1, "");
    emit_eh_rethrow_current(g);
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
        LLVMBuildUnreachable(g->builder);

    LLVMPositionBuilderAtEnd(g->builder, ok_bb);
}
