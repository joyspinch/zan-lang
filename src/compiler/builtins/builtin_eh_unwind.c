static LLVMValueRef get_eh_slot_fn(zan_irgen_t *g) {
    return get_eh_chunk_fn(g, "__zan_eh_slot", EH_F_BUFS,
        eh_slot_shift(g), ZAN_EH_SLOT_BYTES,
        "zan: exception handler stack exhausted (too many nested try blocks)\n");
}
LLVMValueRef emit_eh_buf_ptr(zan_irgen_t *g, LLVMValueRef idx) {
    return emit_eh_chunk_call(g, get_eh_slot_fn(g), idx, "eh.bufp");
}
LLVMValueRef emit_eh_mark_ptr(zan_irgen_t *g, LLVMValueRef idx) {
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef off = LLVMConstInt(i32t, ZAN_EH_MARK_OFF, 0);
    return LLVMBuildBitCast(g->builder,
        LLVMBuildGEP2(g->builder, i8t, emit_eh_buf_ptr(g, idx), &off, 1, "eh.markb"),
        LLVMPointerType(i32t, 0), "eh.markp");
}

static void add_enum_attr(zan_irgen_t *g, LLVMValueRef fn, LLVMValueRef call,
                          const char *name) {
    unsigned kind = LLVMGetEnumAttributeKindForName(name, strlen(name));
    if (kind == 0) return;
    LLVMAttributeRef attr = LLVMCreateEnumAttribute(g->ctx, kind, 0);
    LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, attr);
    if (call)
        LLVMAddCallSiteAttribute(call, (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                                 attr);
}

static bool target_is_windows_arm64(zan_irgen_t *g) {
    return g->target_is_windows &&
           (strstr(g->target_triple, "aarch64") != NULL ||
            strstr(g->target_triple, "arm64") != NULL);
}
LLVMValueRef emit_eh_setjmp(zan_irgen_t *g, LLVMValueRef bufp) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (target_is_windows_arm64(g)) {
        LLVMTypeRef ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr }, 1, 0);
        LLVMValueRef fn = get_libc_fn(g, "__mingw_setjmp", ty);
        LLVMValueRef call = zan_call2(g->builder, ty, fn, &bufp, 1, "sj");
        add_enum_attr(g, fn, call, "returns_twice");
        return call;
    }
    if (g->target_is_windows) {
        LLVMTypeRef ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
        LLVMValueRef fn = get_libc_fn(g, "_setjmp", ty);
        LLVMValueRef call = zan_call2(g->builder, ty, fn,
            (LLVMValueRef[]){ bufp, LLVMConstNull(i8ptr) }, 2, "sj");
        add_enum_attr(g, fn, call, "returns_twice");
        return call;
    }
    bool riscv_bare = strncmp(g->target_triple, "riscv", 5) == 0 &&
                      !strstr(g->target_triple, "linux");
    LLVMTypeRef ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMValueRef fn = get_libc_fn(g,
        (riscv_bare || g->target_is_wasm) ? "setjmp" : "_setjmp", ty);
    LLVMValueRef call = zan_call2(g->builder, ty, fn, &bufp, 1, "sj");
    add_enum_attr(g, fn, call, "returns_twice");
    return call;
}
void emit_eh_longjmp(zan_irgen_t *g, LLVMValueRef bufp) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0);
    const char *name = target_is_windows_arm64(g) ? "__mingw_longjmp" : "longjmp";
    LLVMValueRef fn = get_libc_fn(g, name, ty);
    LLVMValueRef call = zan_call2(g->builder, ty, fn,
        (LLVMValueRef[]){ bufp, LLVMConstInt(i32t, 1, 0) }, 2, "");
    add_enum_attr(g, fn, call, "noreturn");
}


/* void @llvm */
static LLVMValueRef get_wasm_throw_intrinsic_fn(zan_irgen_t *g) {
    if (g->wasm_eh_throw_intrinsic_fn)
        return g->wasm_eh_throw_intrinsic_fn;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i32t, i8ptr }, 2, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "llvm.wasm.throw", ty);
    add_enum_attr(g, fn, NULL, "noreturn");
    g->wasm_eh_throw_intrinsic_fn = fn;
    g->wasm_eh_used = true;
    return fn;
}

static LLVMValueRef get_wasm_cxa_throw_fn(zan_irgen_t *g) {
    if (g->wasm_eh_throw_fn) return g->wasm_eh_throw_fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr }, 3, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__cxa_throw", ty);
    add_enum_attr(g, fn, NULL, "noreturn");
    g->wasm_eh_throw_fn = fn;
    g->wasm_eh_used = true;
    return fn;
}

/* i32 @__gxx_wasm_personality_v0( */
static LLVMValueRef get_wasm_personality_fn(zan_irgen_t *g) {
    if (g->wasm_eh_personality_fn) return g->wasm_eh_personality_fn;
    LLVMTypeRef ty = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), NULL, 0, 1);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__gxx_wasm_personality_v0", ty);
    g->wasm_eh_personality_fn = fn;
    g->wasm_eh_used = true;
    return fn;
}

static void wasm_eh_set_personality(zan_irgen_t *g) {
    if (!g->target_is_wasm || !g->current_fn) return;
    LLVMSetPersonalityFn(g->current_fn, get_wasm_personality_fn(g));
}

static void wasm_note_state_fn(zan_irgen_t *g, LLVMValueRef state_fn) {
    if (g->target_is_wasm) g->wasm_eh_state_fn = state_fn;
}

static void emit_wasm_throw_op(zan_irgen_t *g, LLVMValueRef exc_obj) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef fn = get_wasm_cxa_throw_fn(g);
    if (LLVMGetTypeKind(LLVMTypeOf(exc_obj)) == LLVMPointerTypeKind &&
        LLVMTypeOf(exc_obj) != i8ptr)
        exc_obj = LLVMBuildBitCast(g->builder, exc_obj, i8ptr, "wt.exc");
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef lpad = g->wasm_try_depth > 0
        ? g->wasm_lpad_stack[g->wasm_try_depth - 1] : NULL;
    if (lpad &&
        LLVMGetBasicBlockParent(lpad) == LLVMGetBasicBlockParent(cur) &&
        !LLVMGetBasicBlockTerminator(cur)) {
        LLVMBasicBlockRef cont = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(cur), "wt.cont");
        LLVMBuildInvoke2(g->builder, LLVMGlobalGetValueType(fn), fn,
            (LLVMValueRef[]){ exc_obj, LLVMConstNull(i8ptr),
                              LLVMConstNull(i8ptr) }, 3, cont, lpad, "");
        LLVMPositionBuilderAtEnd(g->builder, cont);
        LLVMBuildUnreachable(g->builder);
        return;
    }
    LLVMValueRef inl = get_wasm_throw_intrinsic_fn(g);
    LLVMBuildCall2(g->builder,
        LLVMGlobalGetValueType(inl), inl,
        (LLVMValueRef[]){ LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0),
                          exc_obj }, 2, "");
    LLVMBuildUnreachable(g->builder);
}

static LLVMBasicBlockRef emit_wasm_lpad(zan_irgen_t *g,
                                        LLVMBasicBlockRef catch_bb) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef lpad_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "wtry.lpad");
    LLVMBasicBlockRef pad_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "wtry.pad");
    LLVMValueRef pers = get_wasm_personality_fn(g);
    LLVMPositionBuilderAtEnd(g->builder, lpad_bb);
    LLVMValueRef cs = LLVMBuildCatchSwitch(g->builder, NULL, NULL, 1, "wtry.cs");
    LLVMAddHandler(cs, pad_bb);
    LLVMPositionBuilderAtEnd(g->builder, pad_bb);
    LLVMValueRef cpa[1] = { LLVMConstNull(i8ptr) }; /* catch-all */
    LLVMValueRef cp = LLVMBuildCatchPad(g->builder, cs, cpa, 1, "wtry.cp");
    LLVMBuildCatchRet(g->builder, cp, catch_bb);
    (void)pers;
    return lpad_bb;
}
LLVMValueRef get_eh_tmp_top_global(zan_irgen_t *g) {
    return emit_eh_field_ptr(g, EH_F_TMPS_TOP, "eh.tmptopp");
}

static LLVMValueRef get_eh_tmp_slot_fast_fn(zan_irgen_t *g, LLVMValueRef slow) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_tmp_slot_fast");
    if (fn) return fn;
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8t, 0);
    LLVMTypeRef state_ty = get_eh_state_ty(g);
    LLVMTypeRef tab_ty = LLVMArrayType(i8ptr, ZAN_EH_CHUNKS);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr,
        (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_eh_tmp_slot_fast", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    add_enum_attr(g, fn, NULL, "alwaysinline");
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef look = LLVMAppendBasicBlockInContext(g->ctx, fn, "look");
    LLVMBasicBlockRef have = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
    LLVMBasicBlockRef slowbb = LLVMAppendBasicBlockInContext(g->ctx, fn, "slow");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMValueRef st = LLVMGetParam(fn, 0);
    LLVMValueRef idx = LLVMGetParam(fn, 1);
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntUGE, idx,
            LLVMConstInt(i32t, 1u << eh_tmp_shift(g), 0), "tmp.deep"),
        slowbb, look);
    LLVMPositionBuilderAtEnd(g->builder, look);
    LLVMValueRef tab = LLVMBuildStructGEP2(g->builder, state_ty,
        LLVMBuildBitCast(g->builder, st, LLVMPointerType(state_ty, 0), "st.t"),
        EH_F_TMPS, "tmp.tab");
    LLVMValueRef c0p = LLVMBuildGEP2(g->builder, tab_ty, tab,
        (LLVMValueRef[]){ LLVMConstInt(i32t, 0, 0), LLVMConstInt(i32t, 0, 0) }, 2,
        "tmp.c0p");
    LLVMValueRef c0 = LLVMBuildLoad2(g->builder, i8ptr, c0p, "tmp.c0");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, c0, LLVMConstNull(i8ptr), "tmp.fresh"),
        slowbb, have);
    LLVMPositionBuilderAtEnd(g->builder, have);
    LLVMValueRef off = LLVMBuildMul(g->builder,
        LLVMBuildZExt(g->builder, idx, i64t, "tmp.i64"),
        LLVMConstInt(i64t, 8, 0), "tmp.off");
    LLVMValueRef ep = LLVMBuildGEP2(g->builder, i8t, c0, &off, 1, "tmp.ep");
    LLVMBuildBr(g->builder, done);
    LLVMPositionBuilderAtEnd(g->builder, slowbb);
    LLVMValueRef sp = zan_call2(g->builder, fnty, slow,
        (LLVMValueRef[]){ st, idx }, 2, "tmp.slow");
    LLVMBuildBr(g->builder, done);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMValueRef phi = LLVMBuildPhi(g->builder, i8ptr, "tmp.r");
    LLVMAddIncoming(phi, (LLVMValueRef[]){ ep, sp },
        (LLVMBasicBlockRef[]){ have, slowbb }, 2);
    LLVMBuildRet(g->builder, phi);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

static LLVMValueRef emit_eh_tmp_slot_ptr(zan_irgen_t *g, LLVMValueRef idx) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef fn = get_eh_tmp_slot_fast_fn(g,
        get_eh_chunk_fn(g, "__zan_eh_tmp_slot", EH_F_TMPS,
            eh_tmp_shift(g), 8, "zan: exception unwind stack exhausted\n"));
    return LLVMBuildBitCast(g->builder,
        emit_eh_chunk_call(g, fn, idx, "eh.tslot"),
        LLVMPointerType(i8ptr, 0), "eh.tslotp");
}
LLVMValueRef get_eh_exc_owned_global(zan_irgen_t *g) {
    return emit_eh_field_ptr(g, EH_F_EXC_OWNED, "eh.ownp");
}
LLVMValueRef get_eh_exc_tid_global(zan_irgen_t *g) {
    return emit_eh_field_ptr(g, EH_F_EXC_TID, "eh.tidp");
}
LLVMValueRef get_class_tid_global(zan_irgen_t *g, zan_symbol_t *sym) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    char name[512];
    snprintf(name, sizeof(name), "__zan_tid_%.*s", sym->name.len, sym->name.str);
    LLVMValueRef v = LLVMGetNamedGlobal(g->mod, name);
    if (v) return v;
    v = LLVMAddGlobal(g->mod, i8ptr, name);
    LLVMSetLinkage(v, LLVMInternalLinkage);
    LLVMSetInitializer(v, LLVMConstNull(i8ptr));
    if (sym->type && sym->type->base_type && sym->type->base_type->sym) {
        LLVMValueRef base_tid = get_class_tid_global(g, sym->type->base_type->sym);
        LLVMSetInitializer(v, LLVMConstBitCast(base_tid, i8ptr));
    }
    {
        char *cls = zan_arena_alloc(g->arena, (int)sym->name.len + 1);
        memcpy(cls, sym->name.str, sym->name.len);
        cls[sym->name.len] = '\0';
        if (ZAN_TAB_ENSURE(g->tid_names, g->tid_name_count,
                           g->tid_name_cap, 16)) {
            g->tid_names[g->tid_name_count].tid = v;
            g->tid_names[g->tid_name_count].name = cls;
            g->tid_name_count++;
        }
    }
    return v;
}
LLVMValueRef get_eh_tid_match_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_tid_match");
    if (fn) return fn;
    LLVMTypeRef i1t = LLVMInt1TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fnty = LLVMFunctionType(i1t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_eh_tid_match", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "loop");
    LLVMBasicBlockRef test = LLVMAppendBasicBlockInContext(g->ctx, fn, "test");
    LLVMBasicBlockRef step = LLVMAppendBasicBlockInContext(g->ctx, fn, "step");
    LLVMBasicBlockRef yes = LLVMAppendBasicBlockInContext(g->ctx, fn, "yes");
    LLVMBasicBlockRef no = LLVMAppendBasicBlockInContext(g->ctx, fn, "no");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef thrown = LLVMGetParam(fn, 0);
    LLVMValueRef want = LLVMGetParam(fn, 1);
    LLVMValueRef cur = LLVMBuildAlloca(g->builder, i8ptr, "cur");
    LLVMBuildStore(g->builder, thrown, cur);
    LLVMValueRef thrown_null = zan_icmp(g->builder, LLVMIntEQ, thrown,
        LLVMConstNull(i8ptr), "tnull");
    LLVMBuildCondBr(g->builder, thrown_null, no, loop);
    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef c = LLVMBuildLoad2(g->builder, i8ptr, cur, "c");
    LLVMValueRef cnull = zan_icmp(g->builder, LLVMIntEQ, c,
        LLVMConstNull(i8ptr), "cnull");
    LLVMBuildCondBr(g->builder, cnull, no, test);
    LLVMPositionBuilderAtEnd(g->builder, test);
    LLVMValueRef eq = zan_icmp(g->builder, LLVMIntEQ, c, want, "eq");
    LLVMBuildCondBr(g->builder, eq, yes, step);
    LLVMPositionBuilderAtEnd(g->builder, step);
    LLVMValueRef cp = LLVMBuildBitCast(g->builder, c,
        LLVMPointerType(i8ptr, 0), "cp");
    LLVMValueRef next = LLVMBuildLoad2(g->builder, i8ptr, cp, "next");
    LLVMBuildStore(g->builder, next, cur);
    LLVMBuildBr(g->builder, loop);
    LLVMPositionBuilderAtEnd(g->builder, yes);
    LLVMBuildRet(g->builder, LLVMConstInt(i1t, 1, 0));
    LLVMPositionBuilderAtEnd(g->builder, no);
    LLVMBuildRet(g->builder, LLVMConstInt(i1t, 0, 0));
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}
LLVMValueRef get_eh_tid_name_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_tid_name");
    if (fn) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_eh_tid_name", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef chain = LLVMAppendBasicBlockInContext(g->ctx, fn, "chain");
    LLVMBasicBlockRef scan = LLVMAppendBasicBlockInContext(g->ctx, fn, "scan");
    LLVMBasicBlockRef ent = LLVMAppendBasicBlockInContext(g->ctx, fn, "ent");
    LLVMBasicBlockRef nextc = LLVMAppendBasicBlockInContext(g->ctx, fn, "nextc");
    LLVMBasicBlockRef yes = LLVMAppendBasicBlockInContext(g->ctx, fn, "yes");
    LLVMBasicBlockRef no = LLVMAppendBasicBlockInContext(g->ctx, fn, "no");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef thrown = LLVMGetParam(fn, 0);
    LLVMValueRef tn = zan_icmp(g->builder, LLVMIntEQ, thrown,
        LLVMConstNull(i8ptr), "tnull");
    LLVMBuildCondBr(g->builder, tn, no, chain);
    LLVMPositionBuilderAtEnd(g->builder, chain);
    LLVMValueRef cur = LLVMBuildAlloca(g->builder, i8ptr, "cur");
    LLVMBuildStore(g->builder, thrown, cur);
    LLVMBuildBr(g->builder, scan);
    LLVMPositionBuilderAtEnd(g->builder, scan);
    LLVMValueRef c = LLVMBuildLoad2(g->builder, i8ptr, cur, "c");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, c, LLVMConstNull(i8ptr), "cnull"),
        no, ent);
    LLVMPositionBuilderAtEnd(g->builder, ent);
    /* 遍历异常注册表，比对类型标识与当前继承链节点 */
    LLVMTypeRef ent_ty = LLVMStructTypeInContext(g->ctx,
        (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMTypeRef reg_ty = LLVMArrayType(ent_ty, 0);
    LLVMValueRef reg = LLVMAddGlobal(g->mod, reg_ty, "__zan_tid_name_reg");
    LLVMSetLinkage(reg, LLVMInternalLinkage);
    /* zero-initialized: entry */
    LLVMValueRef idx[2] = { LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0),
                            LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0) };
    LLVMValueRef reg0 = LLVMBuildInBoundsGEP2(g->builder, reg_ty, reg, idx, 2,
                                              "reg0");
    LLVMValueRef slot = LLVMBuildAlloca(g->builder, i8ptr, "slot");
    LLVMBuildStore(g->builder, LLVMBuildBitCast(g->builder, reg0, i8ptr, "r0"),
                   slot);
    LLVMBasicBlockRef tryent = LLVMAppendBasicBlockInContext(g->ctx, fn, "tryent");
    LLVMBasicBlockRef regnext = LLVMAppendBasicBlockInContext(g->ctx, fn, "regnext");
    LLVMBasicBlockRef regcheck = LLVMAppendBasicBlockInContext(g->ctx, fn, "regcheck");
    LLVMBuildBr(g->builder, tryent);
    LLVMPositionBuilderAtEnd(g->builder, tryent);
    LLVMValueRef sp = LLVMBuildLoad2(g->builder, i8ptr, slot, "sp");
    LLVMValueRef entp = LLVMBuildBitCast(g->builder, sp,
        LLVMPointerType(ent_ty, 0), "entp");
    LLVMValueRef etidp = LLVMBuildStructGEP2(g->builder, ent_ty, entp, 0, "etidp");
    LLVMValueRef etid = LLVMBuildLoad2(g->builder, i8ptr, etidp, "etid");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, etid, LLVMConstNull(i8ptr), "erend"),
        no, regcheck);
    LLVMPositionBuilderAtEnd(g->builder, regcheck);
    LLVMValueRef hit = zan_icmp(g->builder, LLVMIntEQ, c, etid, "hit");
    LLVMBuildCondBr(g->builder, hit, yes, regnext);
    LLVMPositionBuilderAtEnd(g->builder, regnext);
    LLVMValueRef nslot = LLVMBuildGEP2(g->builder, i8ptr, sp,
        (LLVMValueRef[]){ LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 2, 0) }, 1,
        "nslot");
    LLVMBuildStore(g->builder, nslot, slot);
    LLVMBuildBr(g->builder, tryent);
    LLVMPositionBuilderAtEnd(g->builder, yes);
    LLVMValueRef nmp = LLVMBuildStructGEP2(g->builder, ent_ty, entp, 1, "nmp");
    LLVMValueRef nm = LLVMBuildLoad2(g->builder, i8ptr, nmp, "nm");
    LLVMBuildRet(g->builder, nm);
    LLVMPositionBuilderAtEnd(g->builder, nextc);
    {
        LLVMValueRef cp = LLVMBuildBitCast(g->builder, c,
            LLVMPointerType(i8ptr, 0), "cp");
        LLVMValueRef next = LLVMBuildLoad2(g->builder, i8ptr, cp, "next");
        LLVMBuildStore(g->builder, next, cur);
        LLVMBuildBr(g->builder, scan);
    }
    LLVMPositionBuilderAtEnd(g->builder, no);
    LLVMBuildRet(g->builder, LLVMConstNull(i8ptr));
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    g->tid_name_reg_global = reg;
    g->tid_name_reg_ent_ty = ent_ty;
    return fn;
}

static LLVMValueRef get_eh_tmp_push_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_tmp_push");
    if (fn) return fn;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_eh_tmp_push", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef top_g = get_eh_tmp_top_global(g);
    LLVMValueRef top = LLVMBuildLoad2(g->builder, i32t, top_g, "top");
    LLVMBuildStore(g->builder, LLVMGetParam(fn, 0), emit_eh_tmp_slot_ptr(g, top));
    LLVMBuildStore(g->builder,
        zan_add(g->builder, top, LLVMConstInt(i32t, 1, 0), "ntop"), top_g);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

static LLVMValueRef get_eh_tmp_pop_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_tmp_pop");
    if (fn) return fn;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    fn = LLVMAddFunction(g->mod, "__zan_eh_tmp_pop", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef dec = LLVMAppendBasicBlockInContext(g->ctx, fn, "dec");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef top_g = get_eh_tmp_top_global(g);
    LLVMValueRef top = LLVMBuildLoad2(g->builder, i32t, top_g, "top");
    LLVMValueRef pos = zan_icmp(g->builder, LLVMIntSGT, top,
        LLVMConstInt(i32t, 0, 0), "pos");
    LLVMBuildCondBr(g->builder, pos, dec, done);
    LLVMPositionBuilderAtEnd(g->builder, dec);
    LLVMValueRef ntop = zan_sub(g->builder, top, LLVMConstInt(i32t, 1, 0), "ntop");
    LLVMBuildStore(g->builder, ntop, top_g);
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr),
        emit_eh_tmp_slot_ptr(g, ntop));
    LLVMBuildBr(g->builder, done);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}
LLVMValueRef get_eh_tmp_unwind_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_tmp_unwind");
    if (fn) return fn;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i32t, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_eh_tmp_unwind", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(g->ctx, fn, "head");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef rel = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel");
    LLVMBasicBlockRef rel_slot = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel.slot");
    LLVMBasicBlockRef rel_pick = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel.pick");
    LLVMBasicBlockRef rel_dlg = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel.dlg");
    LLVMBasicBlockRef rel_obj = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel.obj");
    LLVMBasicBlockRef rel_dyn = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel.dyn");
    LLVMBasicBlockRef rel_str = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel.str");
    LLVMBasicBlockRef rel_arr = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel.arr");
    LLVMBasicBlockRef rel_pick2 = LLVMAppendBasicBlockInContext(g->ctx, fn, "rel.pick2");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef top_g = get_eh_tmp_top_global(g);
    LLVMBuildBr(g->builder, head);
    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef top = LLVMBuildLoad2(g->builder, i32t, top_g, "top");
    LLVMValueRef above = zan_icmp(g->builder, LLVMIntSGT, top,
        LLVMGetParam(fn, 0), "above");
    LLVMBuildCondBr(g->builder, above, body, done);
    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef ntop = zan_sub(g->builder, top, LLVMConstInt(i32t, 1, 0), "ntop");
    LLVMBuildStore(g->builder, ntop, top_g);
    LLVMBuildBr(g->builder, rel);
    LLVMPositionBuilderAtEnd(g->builder, rel);
    LLVMValueRef slot = emit_eh_tmp_slot_ptr(g, ntop);
    LLVMValueRef obj = LLVMBuildLoad2(g->builder, i8ptr, slot, "obj");
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), slot);
    LLVMValueRef obj_i = LLVMBuildPtrToInt(g->builder, obj,
        LLVMInt64TypeInContext(g->ctx), "obji");
    LLVMValueRef tagged = LLVMBuildTrunc(g->builder,
        obj_i, LLVMInt1TypeInContext(g->ctx), "tagged");
    LLVMBuildCondBr(g->builder, tagged, rel_slot, rel_obj);

    LLVMPositionBuilderAtEnd(g->builder, rel_slot);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef kind = zan_and(g->builder, obj_i,
        LLVMConstInt(i64t, 6, 0), "eh.kind");
    LLVMValueRef vobj = LLVMBuildIntToPtr(g->builder,
        zan_and(g->builder, obj_i,
            LLVMConstInt(i64t, ~(uint64_t)7, 0), "untag"),
        i8ptr, "vobj");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, kind,
            LLVMConstInt(i64t, ZAN_EH_SLOT_DLG << 1, 0), "isdlg"),
        rel_dlg, rel_pick);

    LLVMPositionBuilderAtEnd(g->builder, rel_pick);
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, kind,
            LLVMConstInt(i64t, ZAN_EH_SLOT_STR << 1, 0), "isstr"),
        rel_str, rel_pick2);

    LLVMPositionBuilderAtEnd(g->builder, rel_pick2);
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, kind,
            LLVMConstInt(i64t, ZAN_EH_SLOT_ARR << 1, 0), "isarr"),
        rel_arr, rel_dyn);

    LLVMPositionBuilderAtEnd(g->builder, rel_arr);
    LLVMBasicBlockRef rel_arr_null = LLVMAppendBasicBlockInContext(g->ctx, fn, "relarr.null");
    LLVMBasicBlockRef rel_arr_work = LLVMAppendBasicBlockInContext(g->ctx, fn, "relarr.work");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, vobj, LLVMConstNull(i8ptr), "arr.isnull"),
        rel_arr_null, rel_arr_work);

    LLVMPositionBuilderAtEnd(g->builder, rel_arr_work);
    LLVMValueRef neg8 = LLVMConstInt(i64t, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
    LLVMValueRef site_p = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), vobj, &neg8, 1, "arr.sitep");
    LLVMValueRef raw_site = LLVMBuildLoad2(g->builder, i64t,
        LLVMBuildBitCast(g->builder, site_p, LLVMPointerType(i64t, 0), "arr.siteip"), "arr.site.raw");
    LLVMValueRef site_val = zan_and(g->builder, raw_site,
        LLVMConstInt(i64t, ~ZAN_OBJ_STACK_FLAG, 0), "arr.site");
    LLVMValueRef is_magic = zan_icmp(g->builder, LLVMIntEQ, site_val,
        LLVMConstInt(i64t, ZAN_ARRAY_MAGIC, 0), "arr.ismagic");
    LLVMValueRef is_rank = zan_and(g->builder,
        zan_icmp(g->builder, LLVMIntUGT, site_val, LLVMConstInt(i64t, 0, 0), "arr.nz"),
        zan_icmp(g->builder, LLVMIntULT, site_val, LLVMConstInt(i64t, 4096, 0), "arr.rank"),
        "arr.isrank");
    LLVMValueRef is_raw = zan_or(g->builder, is_magic, is_rank, "arr.israw");
    LLVMBasicBlockRef rel_arr_plain = LLVMAppendBasicBlockInContext(g->ctx, fn, "relarr.plain");
    LLVMBasicBlockRef rel_arr_desc = LLVMAppendBasicBlockInContext(g->ctx, fn, "relarr.desc");
    LLVMBuildCondBr(g->builder, is_raw, rel_arr_plain, rel_arr_desc);

    LLVMPositionBuilderAtEnd(g->builder, rel_arr_desc);
    LLVMValueRef desc_p = LLVMBuildIntToPtr(g->builder, site_val, LLVMPointerType(i8ptr, 0), "adesc.p");
    LLVMValueRef adtor = LLVMBuildLoad2(g->builder, i8ptr, desc_p, "adesc.dtor");
    LLVMValueRef has_adtor = zan_icmp(g->builder, LLVMIntNE, adtor, LLVMConstNull(i8ptr), "has.adtor");
    LLVMBasicBlockRef rel_arr_call = LLVMAppendBasicBlockInContext(g->ctx, fn, "relarr.call");
    LLVMBuildCondBr(g->builder, has_adtor, rel_arr_call, rel_arr_plain);

    LLVMPositionBuilderAtEnd(g->builder, rel_arr_call);
    LLVMTypeRef adfnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    LLVMValueRef adfn = LLVMBuildBitCast(g->builder, adtor, LLVMPointerType(adfnty, 0), "adfn");
    zan_call2(g->builder, adfnty, adfn, &vobj, 1, "");
    LLVMBuildBr(g->builder, head);

    LLVMPositionBuilderAtEnd(g->builder, rel_arr_plain);
    zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_arr_release, &vobj, 1, "");
    LLVMBuildBr(g->builder, head);

    LLVMPositionBuilderAtEnd(g->builder, rel_arr_null);
    LLVMBuildBr(g->builder, head);

    LLVMPositionBuilderAtEnd(g->builder, rel_dlg);
    emit_closure_release(g, vobj);
    LLVMBuildBr(g->builder, head);

    LLVMPositionBuilderAtEnd(g->builder, rel_obj);
    LLVMBuildBr(g->builder, rel_dyn);

    LLVMPositionBuilderAtEnd(g->builder, rel_dyn);
    LLVMValueRef dyn_obj = LLVMBuildPhi(g->builder, i8ptr, "dyn.obj");
    LLVMValueRef dyn_v[2] = { vobj, obj };
    LLVMBasicBlockRef dyn_b[2] = { rel_pick2, rel_obj };
    LLVMAddIncoming(dyn_obj, dyn_v, dyn_b, 2);
    zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_release_dyn, &dyn_obj, 1, "");
    LLVMBuildBr(g->builder, head);

    LLVMPositionBuilderAtEnd(g->builder, rel_str);
    zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_str_release, &vobj, 1, "");
    LLVMBuildBr(g->builder, head);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}
void emit_eh_tmp_push(zan_irgen_t *g, LLVMValueRef obj) {
    if (!obj || LLVMGetTypeKind(LLVMTypeOf(obj)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(obj) != i8ptr)
        obj = LLVMBuildBitCast(g->builder, obj, i8ptr, "eh.tmp");
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    zan_call2(g->builder, fnty, get_eh_tmp_push_fn(g), &obj, 1, "");
}
void emit_eh_tmp_push_slot(zan_irgen_t *g, LLVMValueRef slot, int kind) {
    if (!slot || LLVMGetTypeKind(LLVMTypeOf(slot)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(slot) != i8ptr)
        slot = LLVMBuildBitCast(g->builder, slot, i8ptr, "eh.slotp");
    uint64_t tag = 1u | ((uint64_t)kind << 1);
    LLVMValueRef tagged = LLVMBuildIntToPtr(g->builder,
        zan_or(g->builder, LLVMBuildPtrToInt(g->builder, slot, i64t, "slot.i"),
            LLVMConstInt(i64t, tag, 0), "slot.t"),
        i8ptr, "eh.slot");
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    zan_call2(g->builder, fnty, get_eh_tmp_push_fn(g), &tagged, 1, "");
}
void emit_eh_unwind_to_handler(zan_irgen_t *g, LLVMValueRef top) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef mp = emit_eh_mark_ptr(g, top);
    LLVMValueRef mark = LLVMBuildLoad2(g->builder, i32t, mp, "eh.mark");
    LLVMTypeRef uwty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i32t, 1, 0);
    zan_call2(g->builder, uwty, get_eh_tmp_unwind_fn(g), &mark, 1, "");
}
void emit_eh_tmp_pop(zan_irgen_t *g) {
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    zan_call2(g->builder, fnty, get_eh_tmp_pop_fn(g), NULL, 0, "");
}
void emit_eh_tmp_unwind_to(zan_irgen_t *g, LLVMValueRef mark) {
    if (!g || !mark) return;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef uwty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i32t, 1, 0);
    zan_call2(g->builder, uwty, get_eh_tmp_unwind_fn(g), &mark, 1, "");
}

static LLVMValueRef get_eh_tmp_drop_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_tmp_drop");
    if (fn) return fn;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_eh_tmp_drop", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(g->ctx, fn, "head");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef hit = LLVMAppendBasicBlockInContext(g->ctx, fn, "hit");
    LLVMBasicBlockRef shrink = LLVMAppendBasicBlockInContext(g->ctx, fn, "shrink");
    LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef top_g = get_eh_tmp_top_global(g);
    LLVMValueRef i_slot = LLVMBuildAlloca(g->builder, i32t, "i");
    LLVMBuildStore(g->builder,
        LLVMBuildLoad2(g->builder, i32t, top_g, "top0"), i_slot);
    LLVMBuildBr(g->builder, head);

    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef i = LLVMBuildLoad2(g->builder, i32t, i_slot, "i.v");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntSGT, i, LLVMConstInt(i32t, 0, 0), "more"),
        body, done);

    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef idx = zan_sub(g->builder,
        LLVMBuildLoad2(g->builder, i32t, i_slot, "i.v2"),
        LLVMConstInt(i32t, 1, 0), "idx");
    LLVMBuildStore(g->builder, idx, i_slot);
    LLVMValueRef slot = emit_eh_tmp_slot_ptr(g, idx);
    LLVMValueRef obj = LLVMBuildLoad2(g->builder, i8ptr, slot, "obj");
    LLVMValueRef obj_i = LLVMBuildPtrToInt(g->builder, obj,
        LLVMInt64TypeInContext(g->ctx), "obji");
    LLVMValueRef untag = LLVMBuildIntToPtr(g->builder,
        zan_and(g->builder, obj_i,
            LLVMConstInt(LLVMInt64TypeInContext(g->ctx), ~(uint64_t)7, 0), "untag"),
        i8ptr, "uobj");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, untag, LLVMGetParam(fn, 0), "same"),
        hit, next);

    LLVMPositionBuilderAtEnd(g->builder, hit);
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), slot);
    LLVMValueRef top = LLVMBuildLoad2(g->builder, i32t, top_g, "top");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, top,
            zan_add(g->builder, idx, LLVMConstInt(i32t, 1, 0), "idx1"), "is.top"),
        shrink, done);
    LLVMPositionBuilderAtEnd(g->builder, shrink);
    LLVMBuildStore(g->builder, idx, top_g);
    LLVMBuildBr(g->builder, done);

    LLVMPositionBuilderAtEnd(g->builder, next);
    LLVMBuildBr(g->builder, head);

    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}
void emit_eh_tmp_drop(zan_irgen_t *g, LLVMValueRef obj) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (!obj || LLVMGetTypeKind(LLVMTypeOf(obj)) != LLVMPointerTypeKind) return;
    if (LLVMTypeOf(obj) != i8ptr)
        obj = LLVMBuildBitCast(g->builder, obj, i8ptr, "eh.drop");
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    zan_call2(g->builder, fnty, get_eh_tmp_drop_fn(g), &obj, 1, "");
}
