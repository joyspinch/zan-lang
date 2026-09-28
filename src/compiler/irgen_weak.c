/* Module-local weak-reference registry. The registry is emitted into each
 * generated module so targets do not need an additional runtime object. The
 * bucket array is lazily allocated on first use by weak_buckets_ensure under
 * the weak spinlock. */

#define ZAN_WEAK_BUCKET_COUNT 8192

static LLVMTypeRef weak_i8ptr(zan_irgen_t *g) {
    return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
}

static LLVMTypeRef weak_slot_type(zan_irgen_t *g) {
    return LLVMPointerType(weak_i8ptr(g), 0);
}

static LLVMTypeRef weak_node_type(zan_irgen_t *g) {
    if (!g->weak_node_type) {
        LLVMTypeRef fields[] = { weak_i8ptr(g), weak_i8ptr(g), weak_slot_type(g) };
        g->weak_node_type = LLVMStructCreateNamed(g->ctx, "zan.weak.node");
        LLVMStructSetBody(g->weak_node_type, fields, 3, 0);
    }
    return g->weak_node_type;
}

static LLVMValueRef weak_bucket_for(zan_irgen_t *g, LLVMValueRef buckets,
                                    LLVMValueRef obj, const char *name) {
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef addr = LLVMBuildPtrToInt(b, obj, i64, "weak.addr");
    LLVMValueRef shifted = LLVMBuildLShr(b, addr, LLVMConstInt(i64, 4, 0),
                                         "weak.addr.shift");
    LLVMValueRef mixed = LLVMBuildMul(b, shifted,
        LLVMConstInt(i64, UINT64_C(0x9E3779B97F4A7C15), 0), "weak.addr.mix");
    LLVMValueRef index = LLVMBuildLShr(b, mixed, LLVMConstInt(i64, 51, 0),
                                       "weak.bucket.index");
    index = LLVMBuildAnd(b, index,
        LLVMConstInt(i64, ZAN_WEAK_BUCKET_COUNT - 1, 0), "weak.bucket.mask");
    LLVMTypeRef buckets_t = LLVMArrayType(weak_i8ptr(g), ZAN_WEAK_BUCKET_COUNT);
    LLVMValueRef typed = LLVMBuildBitCast(b, buckets,
        LLVMPointerType(buckets_t, 0), "weak.buckets.typed");
    LLVMValueRef indices[] = {
        LLVMConstInt(i64, 0, 0), index
    };
    return LLVMBuildGEP2(b, buckets_t, typed, indices, 2, name);
}

static LLVMValueRef weak_node_field(zan_irgen_t *g, LLVMValueRef node,
                                    unsigned index, const char *name) {
    LLVMTypeRef nt = weak_node_type(g);
    LLVMValueRef np = LLVMBuildBitCast(g->builder, node,
        LLVMPointerType(nt, 0), "weak.node");
    return LLVMBuildStructGEP2(g->builder, nt, np, index, name);
}

static LLVMValueRef weak_atomic_count_add(zan_irgen_t *g, LLVMAtomicRMWBinOp op,
                                          LLVMValueRef value) {
    return LLVMBuildAtomicRMW(g->builder, op, g->weak_count, value,
                               LLVMAtomicOrderingMonotonic, 0);
}

static LLVMBasicBlockRef emit_weak_lock(zan_irgen_t *g, LLVMValueRef fn) {
    LLVMBasicBlockRef try_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "weak.try");
    LLVMBasicBlockRef spin_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "weak.spin");
    LLVMBasicBlockRef got_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "weak.locked");
    LLVMBuildBr(g->builder, try_bb);
    LLVMPositionBuilderAtEnd(g->builder, try_bb);
    LLVMValueRef old = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpXchg,
        g->weak_lock, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1, 0),
        LLVMAtomicOrderingAcquire, 0);
    LLVMValueRef held = LLVMBuildICmp(g->builder, LLVMIntNE, old,
        LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0), "weak.held");
    LLVMBuildCondBr(g->builder, held, spin_bb, got_bb);
    LLVMPositionBuilderAtEnd(g->builder, spin_bb);
    LLVMBuildBr(g->builder, try_bb);
    LLVMPositionBuilderAtEnd(g->builder, got_bb);
    return got_bb;
}

static void emit_weak_unlock(zan_irgen_t *g) {
    LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpXchg, g->weak_lock,
        LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0),
        LLVMAtomicOrderingRelease, 0);
}

/* Ensure the bucket array exists, calloc'ing it on first use. Call with the
 * weak lock held, right after emit_weak_lock, so the lazy allocation cannot
 * race. Positions the builder at the join block and returns the array base,
 * which then dominates every bucket access in the body. */
static LLVMValueRef weak_buckets_ensure(zan_irgen_t *g, LLVMValueRef fn,
                                        LLVMValueRef calloc_fn) {
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8ptr = weak_i8ptr(g);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMBasicBlockRef lock_bb = LLVMGetInsertBlock(b);
    LLVMValueRef cur = LLVMBuildLoad2(b, i8ptr, g->weak_buckets,
                                      "weak.buckets.load");
    LLVMValueRef have = LLVMBuildICmp(b, LLVMIntNE, cur,
        LLVMConstPointerNull(i8ptr), "weak.buckets.have");
    LLVMBasicBlockRef alloc_bb = LLVMAppendBasicBlockInContext(g->ctx, fn,
        "weak.buckets.alloc");
    LLVMBasicBlockRef join_bb = LLVMAppendBasicBlockInContext(g->ctx, fn,
        "weak.buckets.ready");
    LLVMBuildCondBr(b, have, join_bb, alloc_bb);

    LLVMPositionBuilderAtEnd(b, alloc_bb);
    LLVMValueRef args[] = {
        LLVMConstInt(i64, ZAN_WEAK_BUCKET_COUNT, 0),
        LLVMConstInt(i64, sizeof(void *), 0)
    };
    LLVMValueRef mem = zan_call2(b, LLVMGlobalGetValueType(calloc_fn),
        calloc_fn, args, 2, "weak.buckets.calloc");
    zan_irgen_emit_oom_check(g, fn, mem);
    LLVMBuildStore(b, mem, g->weak_buckets);
    /* The oom check may have split alloc_bb (fail/ok); the phi's predecessor
     * is whichever block now falls through to the join. */
    LLVMBasicBlockRef tail_bb = LLVMGetInsertBlock(b);
    LLVMBuildBr(b, join_bb);

    LLVMPositionBuilderAtEnd(b, join_bb);
    LLVMValueRef phi = LLVMBuildPhi(b, i8ptr, "weak.buckets.base");
    LLVMValueRef incoming[] = { cur, mem };
    LLVMBasicBlockRef blocks[] = { lock_bb, tail_bb };
    LLVMAddIncoming(phi, incoming, blocks, 2);
    return phi;
}

static void emit_weak_store_body(zan_irgen_t *g, LLVMValueRef weak_calloc) {
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8ptr = weak_i8ptr(g);
    LLVMTypeRef slot_t = weak_slot_type(g);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef nt = weak_node_type(g);
    LLVMValueRef slot = LLVMGetParam(g->rt_weak_store, 0);
    LLVMValueRef newobj = LLVMGetParam(g->rt_weak_store, 1);
    LLVMValueRef null = LLVMConstPointerNull(i8ptr);

    emit_weak_lock(g, g->rt_weak_store);
    LLVMValueRef buckets = weak_buckets_ensure(g, g->rt_weak_store, weak_calloc);

    LLVMValueRef old = LLVMBuildLoad2(b, i8ptr, slot, "weak.old");
    LLVMValueRef old_present = LLVMBuildICmp(b, LLVMIntNE, old, null, "weak.hasold");
    LLVMBasicBlockRef old_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_store, "weak.remove.old");
    LLVMBasicBlockRef set_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_store, "weak.set");
    LLVMBuildCondBr(b, old_present, old_bb, set_bb);

    LLVMPositionBuilderAtEnd(b, old_bb);
    LLVMValueRef bucket = weak_bucket_for(g, buckets, old, "weak.old.bucket");
    LLVMValueRef head = LLVMBuildLoad2(b, i8ptr, bucket, "weak.old.head");
    LLVMValueRef curr_mem = LLVMBuildAlloca(b, i8ptr, "weak.curr");
    LLVMValueRef prev_mem = LLVMBuildAlloca(b, i8ptr, "weak.prev");
    LLVMBuildStore(b, head, curr_mem);
    LLVMBuildStore(b, null, prev_mem);
    LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_store, "weak.remove.loop");
    LLVMBasicBlockRef check_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_store, "weak.remove.check");
    LLVMBasicBlockRef old_done_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_store, "weak.remove.done");
    LLVMBuildBr(b, loop_bb);

    LLVMPositionBuilderAtEnd(b, loop_bb);
    LLVMValueRef curr = LLVMBuildLoad2(b, i8ptr, curr_mem, "weak.curr.load");
    LLVMValueRef curr_present = LLVMBuildICmp(b, LLVMIntNE, curr, null,
                                              "weak.curr.present");
    LLVMBuildCondBr(b, curr_present, check_bb, old_done_bb);

    LLVMPositionBuilderAtEnd(b, check_bb);
    LLVMValueRef next = LLVMBuildLoad2(b, i8ptr,
        weak_node_field(g, curr, 0, "weak.node.next"), "weak.node.next.load");
    LLVMValueRef node_slot = LLVMBuildLoad2(b, slot_t,
        weak_node_field(g, curr, 2, "weak.node.slot"), "weak.node.slot.load");
    LLVMValueRef match = LLVMBuildICmp(b, LLVMIntEQ, node_slot, slot,
                                       "weak.slot.match");
    LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_store, "weak.remove.found");
    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_store, "weak.remove.next");
    LLVMBuildCondBr(b, match, found_bb, next_bb);

    LLVMPositionBuilderAtEnd(b, found_bb);
    LLVMValueRef prev = LLVMBuildLoad2(b, i8ptr, prev_mem, "weak.prev.load");
    LLVMValueRef has_prev = LLVMBuildICmp(b, LLVMIntNE, prev, null,
                                          "weak.hasprev");
    LLVMBasicBlockRef unlink_head_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_store, "weak.unlink.head");
    LLVMBasicBlockRef unlink_prev_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_store, "weak.unlink.prev");
    LLVMBasicBlockRef removed_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_store, "weak.unlinked");
    LLVMBuildCondBr(b, has_prev, unlink_prev_bb, unlink_head_bb);

    LLVMPositionBuilderAtEnd(b, unlink_head_bb);
    LLVMBuildStore(b, next, bucket);
    LLVMBuildBr(b, removed_bb);

    LLVMPositionBuilderAtEnd(b, unlink_prev_bb);
    LLVMBuildStore(b, next, weak_node_field(g, prev, 0, "weak.prev.next"));
    LLVMBuildBr(b, removed_bb);

    LLVMPositionBuilderAtEnd(b, removed_bb);
    zan_call2(b, LLVMGlobalGetValueType(g->fn_free), g->fn_free, &curr, 1, "");
    weak_atomic_count_add(g, LLVMAtomicRMWBinOpSub,
        LLVMConstInt(i64, 1, 0));
    LLVMBuildBr(b, set_bb);

    LLVMPositionBuilderAtEnd(b, next_bb);
    LLVMBuildStore(b, curr, prev_mem);
    LLVMBuildStore(b, next, curr_mem);
    LLVMBuildBr(b, loop_bb);

    LLVMPositionBuilderAtEnd(b, old_done_bb);
    LLVMBuildBr(b, set_bb);

    LLVMPositionBuilderAtEnd(b, set_bb);
    LLVMBuildStore(b, newobj, slot);
    LLVMValueRef new_present = LLVMBuildICmp(b, LLVMIntNE, newobj, null,
                                             "weak.hasnew");
    LLVMBasicBlockRef add_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_store, "weak.add");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_store, "weak.done");
    LLVMBuildCondBr(b, new_present, add_bb, done_bb);

    LLVMPositionBuilderAtEnd(b, add_bb);
    LLVMValueRef node_size = LLVMSizeOf(nt);
    LLVMValueRef node = zan_call2(b, LLVMGlobalGetValueType(g->fn_malloc),
        g->fn_malloc, &node_size, 1, "weak.node.alloc");
    zan_irgen_emit_oom_check(g, g->rt_weak_store, node);
    LLVMValueRef new_bucket = weak_bucket_for(g, buckets, newobj, "weak.new.bucket");
    LLVMValueRef new_head = LLVMBuildLoad2(b, i8ptr, new_bucket,
                                           "weak.new.head");
    LLVMBuildStore(b, new_head, weak_node_field(g, node, 0, "weak.node.next"));
    LLVMBuildStore(b, newobj, weak_node_field(g, node, 1, "weak.node.target"));
    LLVMBuildStore(b, slot, weak_node_field(g, node, 2, "weak.node.slot"));
    LLVMBuildStore(b, node, new_bucket);
    weak_atomic_count_add(g, LLVMAtomicRMWBinOpAdd,
        LLVMConstInt(i64, 1, 0));
    LLVMBuildBr(b, done_bb);

    LLVMPositionBuilderAtEnd(b, done_bb);
    emit_weak_unlock(g);
    LLVMBuildRetVoid(b);
}

static void emit_weak_nil_all_body(zan_irgen_t *g, LLVMValueRef weak_calloc) {
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8ptr = weak_i8ptr(g);
    LLVMTypeRef slot_t = weak_slot_type(g);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef obj = LLVMGetParam(g->rt_weak_nil_all, 0);
    LLVMValueRef null = LLVMConstPointerNull(i8ptr);

    LLVMValueRef count = LLVMBuildLoad2(b, i64, g->weak_count, "weak.count");
    LLVMSetAlignment(count, 8);
    LLVMSetOrdering(count, LLVMAtomicOrderingMonotonic);
    LLVMValueRef empty = LLVMBuildICmp(b, LLVMIntEQ, count,
        LLVMConstInt(i64, 0, 0), "weak.empty");
    LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_nil_all, "weak.nil.empty");
    LLVMBasicBlockRef lock_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_nil_all, "weak.nil.lock");
    LLVMBuildCondBr(b, empty, ret_bb, lock_bb);
    LLVMPositionBuilderAtEnd(b, ret_bb);
    LLVMBuildRetVoid(b);

    LLVMPositionBuilderAtEnd(b, lock_bb);
    emit_weak_lock(g, g->rt_weak_nil_all);
    LLVMValueRef buckets = weak_buckets_ensure(g, g->rt_weak_nil_all, weak_calloc);
    LLVMValueRef bucket = weak_bucket_for(g, buckets, obj, "weak.nil.bucket");
    LLVMValueRef head = LLVMBuildLoad2(b, i8ptr, bucket, "weak.nil.head");
    LLVMValueRef curr_mem = LLVMBuildAlloca(b, i8ptr, "weak.nil.curr");
    LLVMValueRef prev_mem = LLVMBuildAlloca(b, i8ptr, "weak.nil.prev");
    LLVMBuildStore(b, head, curr_mem);
    LLVMBuildStore(b, null, prev_mem);
    LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_nil_all, "weak.nil.loop");
    LLVMBasicBlockRef check_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_nil_all, "weak.nil.check");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_nil_all, "weak.nil.done");
    LLVMBuildBr(b, loop_bb);

    LLVMPositionBuilderAtEnd(b, loop_bb);
    LLVMValueRef curr = LLVMBuildLoad2(b, i8ptr, curr_mem, "weak.nil.curr.load");
    LLVMValueRef present = LLVMBuildICmp(b, LLVMIntNE, curr, null,
                                         "weak.nil.curr.present");
    LLVMBuildCondBr(b, present, check_bb, done_bb);

    LLVMPositionBuilderAtEnd(b, check_bb);
    LLVMValueRef next = LLVMBuildLoad2(b, i8ptr,
        weak_node_field(g, curr, 0, "weak.nil.next"), "weak.nil.next.load");
    LLVMValueRef target = LLVMBuildLoad2(b, i8ptr,
        weak_node_field(g, curr, 1, "weak.nil.target"), "weak.nil.target.load");
    LLVMValueRef match = LLVMBuildICmp(b, LLVMIntEQ, target, obj,
                                       "weak.nil.target.match");
    LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_nil_all, "weak.nil.found");
    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_nil_all, "weak.nil.next");
    LLVMBuildCondBr(b, match, found_bb, next_bb);

    LLVMPositionBuilderAtEnd(b, found_bb);
    LLVMValueRef slot = LLVMBuildLoad2(b, slot_t,
        weak_node_field(g, curr, 2, "weak.nil.slot"), "weak.nil.slot.load");
    LLVMBuildStore(b, null, slot);
    LLVMValueRef prev = LLVMBuildLoad2(b, i8ptr, prev_mem, "weak.nil.prev.load");
    LLVMValueRef has_prev = LLVMBuildICmp(b, LLVMIntNE, prev, null,
                                          "weak.nil.hasprev");
    LLVMBasicBlockRef unlink_head_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_nil_all, "weak.nil.unlink.head");
    LLVMBasicBlockRef unlink_prev_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_nil_all, "weak.nil.unlink.prev");
    LLVMBasicBlockRef removed_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_nil_all, "weak.nil.unlinked");
    LLVMBuildCondBr(b, has_prev, unlink_prev_bb, unlink_head_bb);

    LLVMPositionBuilderAtEnd(b, unlink_head_bb);
    LLVMBuildStore(b, next, bucket);
    LLVMBuildBr(b, removed_bb);

    LLVMPositionBuilderAtEnd(b, unlink_prev_bb);
    LLVMBuildStore(b, next, weak_node_field(g, prev, 0, "weak.nil.prev.next"));
    LLVMBuildBr(b, removed_bb);

    LLVMPositionBuilderAtEnd(b, removed_bb);
    zan_call2(b, LLVMGlobalGetValueType(g->fn_free), g->fn_free, &curr, 1, "");
    weak_atomic_count_add(g, LLVMAtomicRMWBinOpSub,
        LLVMConstInt(i64, 1, 0));
    LLVMBuildStore(b, next, curr_mem);
    LLVMBuildBr(b, loop_bb);

    LLVMPositionBuilderAtEnd(b, next_bb);
    LLVMBuildStore(b, curr, prev_mem);
    LLVMBuildStore(b, next, curr_mem);
    LLVMBuildBr(b, loop_bb);

    LLVMPositionBuilderAtEnd(b, done_bb);
    emit_weak_unlock(g);
    LLVMBuildRetVoid(b);
}

static void emit_weak_runtime(zan_irgen_t *g) {
    LLVMTypeRef i8ptr = weak_i8ptr(g);
    LLVMTypeRef slot_t = weak_slot_type(g);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);

    /* The bucket array itself is a NULL pointer global; the array behind it
     * is calloc'ed by weak_buckets_ensure on the first weak store. */
    g->weak_buckets = LLVMAddGlobal(g->mod, i8ptr, "zan_weak_buckets");
    LLVMSetInitializer(g->weak_buckets, LLVMConstNull(i8ptr));
    LLVMSetLinkage(g->weak_buckets, LLVMInternalLinkage);
    g->weak_lock = LLVMAddGlobal(g->mod, i32, "zan_weak_lock");
    LLVMSetInitializer(g->weak_lock, LLVMConstInt(i32, 0, 0));
    LLVMSetLinkage(g->weak_lock, LLVMInternalLinkage);
    g->weak_count = LLVMAddGlobal(g->mod, i64, "zan_weak_count");
    LLVMSetInitializer(g->weak_count, LLVMConstInt(i64, 0, 0));
    LLVMSetLinkage(g->weak_count, LLVMInternalLinkage);
    LLVMSetAlignment(g->weak_count, 8);
    (void)weak_node_type(g);

    /* void *calloc(size_t, size_t) -- the lazy bucket array */
    LLVMTypeRef calloc_args[] = { i64, i64 };
    LLVMTypeRef calloc_type = LLVMFunctionType(i8ptr, calloc_args, 2, 0);
    LLVMValueRef weak_calloc = LLVMAddFunction(g->mod, "calloc", calloc_type);

    LLVMTypeRef store_args[] = { slot_t, i8ptr };
    LLVMTypeRef store_type = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx), store_args, 2, 0);
    g->rt_weak_store = LLVMAddFunction(g->mod, "zan_rt_weak_store", store_type);
    LLVMTypeRef nil_args[] = { i8ptr };
    LLVMTypeRef nil_type = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx), nil_args, 1, 0);
    g->rt_weak_nil_all = LLVMAddFunction(g->mod, "zan_rt_weak_nil_all", nil_type);

    LLVMBasicBlockRef store_entry = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_store, "entry");
    LLVMPositionBuilderAtEnd(g->builder, store_entry);
    emit_weak_store_body(g, weak_calloc);

    LLVMBasicBlockRef nil_entry = LLVMAppendBasicBlockInContext(g->ctx,
        g->rt_weak_nil_all, "entry");
    LLVMPositionBuilderAtEnd(g->builder, nil_entry);
    emit_weak_nil_all_body(g, weak_calloc);
}

static void emit_weak_store(zan_irgen_t *g, LLVMValueRef field_ptr,
                            LLVMValueRef value) {
    LLVMTypeRef i8ptr = weak_i8ptr(g);
    LLVMTypeRef slot_t = weak_slot_type(g);
    LLVMValueRef slot = LLVMBuildBitCast(g->builder, field_ptr, slot_t,
                                         "weak.slot");
    LLVMValueRef obj = value;
    if (LLVMTypeOf(obj) != i8ptr)
        obj = LLVMBuildBitCast(g->builder, obj, i8ptr, "weak.value");
    LLVMValueRef args[] = { slot, obj };
    zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_weak_store),
              g->rt_weak_store, args, 2, "");
}
