static LLVMValueRef get_eh_self_slot(zan_irgen_t *g) {
    if (g->target_is_windows ||
        strstr(g->target_triple, "wasm") ||
        strstr(g->target_triple, "android") ||
        strncmp(g->target_triple, "riscv32", 7) == 0) return NULL;
    LLVMValueRef v = LLVMGetNamedGlobal(g->mod, "__zan_eh_self");
    if (v) return v;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    v = LLVMAddGlobal(g->mod, i8ptr, "__zan_eh_self");
    LLVMSetLinkage(v, LLVMInternalLinkage);
    LLVMSetInitializer(v, LLVMConstNull(i8ptr));
    LLVMSetThreadLocal(v, 1);
    LLVMSetThreadLocalMode(v, g->emit_shared ? LLVMGeneralDynamicTLSModel
                                             : LLVMInitialExecTLSModel);
    return v;
}

static LLVMTypeRef get_eh_state_ty(zan_irgen_t *g) {
    if (g->eh_state_ty) return g->eh_state_ty;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fields[EH_F_COUNT];
    fields[EH_F_TOP] = i32t;
    fields[EH_F_TMPS_TOP] = i32t;
    fields[EH_F_EXC_OWNED] = i32t;
    fields[EH_F_PAD] = i32t;
    fields[EH_F_EXC] = i8ptr;
    fields[EH_F_EXC_TID] = i8ptr;
    fields[EH_F_BUFS] = LLVMArrayType(i8ptr, ZAN_EH_CHUNKS);
    fields[EH_F_TMPS] = LLVMArrayType(i8ptr, ZAN_EH_CHUNKS);
    LLVMTypeRef ty = LLVMStructCreateNamed(g->ctx, "zan.eh.state");
    LLVMStructSetBody(ty, fields, EH_F_COUNT, 0);
    g->eh_state_ty = ty;
    return ty;
}

static LLVMValueRef emit_eh_thread_id(zan_irgen_t *g) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (strstr(g->target_triple, "wasm"))
        return LLVMConstInt(i64t, 1, 0);
    if (g->target_is_windows) {
        LLVMTypeRef ty = LLVMFunctionType(i32t, NULL, 0, 0);
        LLVMValueRef id = zan_call2(g->builder, ty,
            get_libc_fn(g, "GetCurrentThreadId", ty), NULL, 0, "tid32");
        return LLVMBuildZExt(g->builder, id, i64t, "tid");
    }
    LLVMTypeRef ty = LLVMFunctionType(i8ptr, NULL, 0, 0);
    LLVMValueRef self = zan_call2(g->builder, ty,
        get_libc_fn(g, "pthread_self", ty), NULL, 0, "self");
    return LLVMBuildPtrToInt(g->builder, self, i64t, "tid");
}

static LLVMValueRef get_eh_tab_keys(zan_irgen_t *g) {
    LLVMValueRef v = LLVMGetNamedGlobal(g->mod, "__zan_eh_keys");
    if (v) return v;
    LLVMTypeRef i64ptr = LLVMPointerType(LLVMInt64TypeInContext(g->ctx), 0);
    return eh_add_global(g, i64ptr, "__zan_eh_keys", LLVMConstNull(i64ptr));
}

static LLVMValueRef get_eh_tab_states(zan_irgen_t *g) {
    LLVMValueRef v = LLVMGetNamedGlobal(g->mod, "__zan_eh_states");
    if (v) return v;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ty = LLVMPointerType(i8ptr, 0);
    return eh_add_global(g, ty, "__zan_eh_states", LLVMConstNull(ty));
}

static LLVMValueRef get_eh_tab_cap(zan_irgen_t *g) {
    LLVMValueRef v = LLVMGetNamedGlobal(g->mod, "__zan_eh_cap");
    if (v) return v;
    return eh_add_global(g, LLVMInt64TypeInContext(g->ctx), "__zan_eh_cap",
                         LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0));
}

static LLVMValueRef get_eh_tab_lock(zan_irgen_t *g) {
    LLVMValueRef v = LLVMGetNamedGlobal(g->mod, "__zan_eh_tab_lock");
    if (v) return v;
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef ty = LLVMArrayType(i64t, 8);
    return eh_add_global(g, ty, "__zan_eh_tab_lock", LLVMConstNull(ty));
}

static void emit_eh_tab_lock_acquire(zan_irgen_t *g, LLVMValueRef lock_gv) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef voidt = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef ty = LLVMFunctionType(voidt, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    const char *name = g->target_is_windows
        ? "AcquireSRWLockExclusive" : "pthread_mutex_lock";
    LLVMValueRef fn = get_libc_fn(g, name, ty);
    LLVMValueRef p = LLVMBuildBitCast(g->builder, lock_gv,
        LLVMPointerType(i8ptr, 0), "lock.p");
    zan_call2(g->builder, ty, fn, &p, 1, "");
}

static void emit_eh_tab_lock_release(zan_irgen_t *g, LLVMValueRef lock_gv) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef voidt = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef ty = LLVMFunctionType(voidt, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    const char *name = g->target_is_windows
        ? "ReleaseSRWLockExclusive" : "pthread_mutex_unlock";
    LLVMValueRef fn = get_libc_fn(g, name, ty);
    LLVMValueRef p = LLVMBuildBitCast(g->builder, lock_gv,
        LLVMPointerType(i8ptr, 0), "lock.p");
    zan_call2(g->builder, ty, fn, &p, 1, "");
}

static void emit_eh_oom_abort(zan_irgen_t *g, const char *msg);
static void emit_eh_tab_grow(zan_irgen_t *g, LLVMTypeRef state_ty,
                             LLVMValueRef calloc_fn,
                             LLVMTypeRef free_ty, LLVMValueRef free_fn) {
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64ptr = LLVMPointerType(i64t, 0);
    LLVMTypeRef states_ptr_ty = LLVMPointerType(i8ptr, 0);

    LLVMValueRef keys_gv = get_eh_tab_keys(g);
    LLVMValueRef states_gv = get_eh_tab_states(g);
    LLVMValueRef cap_gv = get_eh_tab_cap(g);
    LLVMValueRef lock_gv = get_eh_tab_lock(g);

    LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
    LLVMValueRef fn = LLVMGetBasicBlockParent(cur);
    LLVMBasicBlockRef head  = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.head");
    LLVMBasicBlockRef walk  = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.walk");
    LLVMBasicBlockRef body  = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.body");
    LLVMBasicBlockRef place = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.place");
    LLVMBasicBlockRef npos  = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.npos");
    LLVMBasicBlockRef scan  = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.scan");
    LLVMBasicBlockRef full  = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.full");
    LLVMBasicBlockRef bump  = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.bump");
    LLVMBasicBlockRef fin   = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.fin");
    LLVMBasicBlockRef done  = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.done");

    LLVMBasicBlockRef src = cur;
    LLVMPositionBuilderAtEnd(g->builder, cur);
    LLVMBuildBr(g->builder, head);
    LLVMPositionBuilderAtEnd(g->builder, head);

    LLVMValueRef oldcap = LLVMBuildLoad2(g->builder, i64t, cap_gv, "gw.oldcap");
    LLVMValueRef newcap = LLVMBuildSelect(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, oldcap, LLVMConstInt(i64t, 0, 0), "zero"),
        LLVMConstInt(i64t, ZAN_EH_THREADS, 0),
        LLVMBuildMul(g->builder, oldcap, LLVMConstInt(i64t, 2, 0), "dbl"),
        "gw.newcap");
    LLVMValueRef kb = zan_call2(g->builder,
        LLVMFunctionType(i64ptr, (LLVMTypeRef[]){ i64t, i64t }, 2, 0),
        calloc_fn, (LLVMValueRef[]){ newcap,
            LLVMConstInt(i64t, 8, 0) }, 2, "gw.kb");
    LLVMValueRef sb = zan_call2(g->builder,
        LLVMFunctionType(states_ptr_ty, (LLVMTypeRef[]){ i64t, i64t }, 2, 0),
        calloc_fn, (LLVMValueRef[]){ newcap, LLVMConstInt(i64t, 8, 0) }, 2,
        "gw.sb");
    LLVMBasicBlockRef oom = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.oom");
    LLVMBuildCondBr(g->builder,
        LLVMBuildOr(g->builder,
            LLVMBuildIsNull(g->builder, kb, "kn"),
            LLVMBuildIsNull(g->builder, sb, "sn"), "oom"),
        oom, walk);
    LLVMPositionBuilderAtEnd(g->builder, oom);
    emit_eh_oom_abort(g,
        "zan: out of memory growing the exception-handling table\n");

    LLVMPositionBuilderAtEnd(g->builder, walk);
    LLVMValueRef j_slot = LLVMBuildAlloca(g->builder, i64t, "gw.j");
    LLVMBuildStore(g->builder, LLVMConstInt(i64t, 0, 0), j_slot);
    LLVMBasicBlockRef head2 = LLVMAppendBasicBlockInContext(g->ctx, fn, "gw.head2");
    LLVMBuildBr(g->builder, head2);

    LLVMPositionBuilderAtEnd(g->builder, head2);
    LLVMValueRef j = LLVMBuildLoad2(g->builder, i64t, j_slot, "gw.jv");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntULT, j, oldcap, "j.ok"), body, fin);

    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef oldkeys = LLVMBuildLoad2(g->builder, i64ptr, keys_gv, "gw.ok");
    LLVMValueRef kp = LLVMBuildGEP2(g->builder, i64ptr, oldkeys, &j, 1, "gw.kp");
    LLVMValueRef k = LLVMBuildLoad2(g->builder, i64t, kp, "gw.k");
    LLVMValueRef live = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntNE, k, LLVMConstInt(i64t, 0, 0), "nz"),
        LLVMBuildICmp(g->builder, LLVMIntNE, k,
            LLVMConstInt(i64t, ZAN_EH_TOMBSTONE, 0), "nt"), "live");
    LLVMBuildCondBr(g->builder, live, place, bump);

    LLVMPositionBuilderAtEnd(g->builder, place);
    LLVMValueRef h = zan_and(g->builder,
        LLVMBuildXor(g->builder, k,
            LLVMBuildLShr(g->builder, k, LLVMConstInt(i64t, 32, 0), "hi"),
            "mix"),
        LLVMBuildSub(g->builder, newcap, LLVMConstInt(i64t, 1, 0), "m1"),
        "gw.h");
    LLVMValueRef p_slot = LLVMBuildAlloca(g->builder, i64t, "gw.p");
    LLVMBuildStore(g->builder, h, p_slot);
    LLVMBuildBr(g->builder, scan);

    LLVMPositionBuilderAtEnd(g->builder, scan);
    LLVMValueRef p = LLVMBuildLoad2(g->builder, i64t, p_slot, "gw.pv");
    LLVMValueRef nkp = LLVMBuildGEP2(g->builder, i64ptr, kb, &p, 1, "gw.nkp");
    LLVMValueRef nk = LLVMBuildLoad2(g->builder, i64t, nkp, "gw.nk");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, nk, LLVMConstInt(i64t, 0, 0), "free"),
        full, npos);

    LLVMPositionBuilderAtEnd(g->builder, full);
    LLVMBuildStore(g->builder, k, nkp);
    LLVMValueRef nsp = LLVMBuildGEP2(g->builder, states_ptr_ty, sb, &p, 1,
                                     "gw.nsp");
    LLVMValueRef osp = LLVMBuildGEP2(g->builder, states_ptr_ty,
        LLVMBuildLoad2(g->builder, states_ptr_ty, states_gv, "gw.os"), &j, 1,
        "gw.osp");
    LLVMValueRef stp = LLVMBuildLoad2(g->builder, states_ptr_ty, osp, "gw.stp");
    LLVMBuildStore(g->builder, stp, nsp);
    LLVMBuildBr(g->builder, bump);

    LLVMPositionBuilderAtEnd(g->builder, npos);
    LLVMValueRef p2 = zan_and(g->builder,
        zan_add(g->builder, p, LLVMConstInt(i64t, 1, 0), "p.next"),
        LLVMBuildSub(g->builder, newcap, LLVMConstInt(i64t, 1, 0), "gw.m1b"),
        "p.wrap");
    LLVMBuildStore(g->builder, p2, p_slot);
    LLVMBuildBr(g->builder, scan);

    LLVMPositionBuilderAtEnd(g->builder, bump);
    LLVMBuildStore(g->builder,
        zan_add(g->builder, j, LLVMConstInt(i64t, 1, 0), "j.next"), j_slot);
    LLVMBuildBr(g->builder, head2);

    LLVMPositionBuilderAtEnd(g->builder, fin);
    /* Install */
    LLVMValueRef ok = LLVMBuildLoad2(g->builder, i64ptr, keys_gv, "gw.oldk2");
    LLVMValueRef os = LLVMBuildLoad2(g->builder, states_ptr_ty, states_gv, "gw.olds");
    LLVMBuildStore(g->builder, kb, keys_gv);
    LLVMBuildStore(g->builder,
        LLVMBuildBitCast(g->builder, sb, states_ptr_ty, "sb.t"), states_gv);
    LLVMBuildStore(g->builder, newcap, cap_gv);
    zan_call2(g->builder, free_ty, free_fn, (LLVMValueRef[]){ ok }, 1, "");
    zan_call2(g->builder, free_ty, free_fn,
        (LLVMValueRef[]){ LLVMBuildBitCast(g->builder, os, i8ptr, "os.c") }, 1, "");
    LLVMBuildBr(g->builder, done);
    LLVMPositionBuilderAtEnd(g->builder, done);
    (void)src;
}

static LLVMValueRef get_eh_state_fn(zan_irgen_t *g);
static LLVMValueRef get_eh_state_fast_fn(zan_irgen_t *g);
static void wasm_note_state_fn(zan_irgen_t *g, LLVMValueRef state_fn);

static LLVMBasicBlockRef eh_enter_entry_block(zan_irgen_t *g) {
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry =
        LLVMGetEntryBasicBlock(LLVMGetBasicBlockParent(cur));
    LLVMValueRef term = LLVMGetBasicBlockTerminator(entry);
    if (term && entry != cur) LLVMPositionBuilderBefore(g->builder, term);
    else LLVMPositionBuilderAtEnd(g->builder, entry);
    return cur;
}

static LLVMValueRef emit_eh_state(zan_irgen_t *g) {
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
    LLVMValueRef fn = cur ? LLVMGetBasicBlockParent(cur) : NULL;
    if (fn && fn == g->eh_state_owner && g->eh_state_cached)
        return g->eh_state_cached;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef state_fn = get_eh_state_fast_fn(g);
    wasm_note_state_fn(g, state_fn);
    zan_builder_point_t pt = zan_builder_save(g);
    eh_enter_entry_block(g);
    LLVMValueRef p = zan_call2(g->builder, LLVMFunctionType(i8ptr, NULL, 0, 0),
        state_fn, NULL, 0, "eh.st");
    LLVMValueRef typed = LLVMBuildBitCast(g->builder, p,
        LLVMPointerType(get_eh_state_ty(g), 0), "eh.stp");
    zan_builder_restore(g, pt);
    g->eh_state_owner = fn;
    g->eh_state_cached = typed;
    for (int i = 0; i < EH_F_COUNT; i++) g->eh_state_fields[i] = NULL;
    return typed;
}

static void add_enum_attr(zan_irgen_t *g, LLVMValueRef fn, LLVMValueRef call,
                          const char *name);

/* ---- Apple arm64: thread-specific data instead of a TLV slot ----------
 * On Mach-O a thread_local access is a call through its TLV descriptor
 * (_tlv_get_addr) whatever the TLS model; every function that touches the
 * EH state paid one on entry (~0.5 ns, about half an empty call). Darwin's
 * pthread TSD array sits at TPIDRRO_EL0 and pthread_getspecific(k) is
 * literally base[k], so the fast path reads that slot directly: no call.
 * The key is created lazily and only enabled after a self-check that the
 * direct read sees what pthread_setspecific stored; a mismatch (a future
 * layout change) parks the key at -1 and every lookup takes the portable
 * slow path, exactly as before. Key states: 0 unset, -1 disabled, >0 key. */
static bool eh_apple_tsd(zan_irgen_t *g) {
    if (!g->target_is_macos && !g->target_is_ios) return false;
    /* g->target_triple is empty for a host build: the module's is set */
    const char *t = LLVMGetTarget(g->mod);
    if (!t || !t[0]) t = g->target_triple;
    return t && (strncmp(t, "arm64", 5) == 0 || strncmp(t, "aarch64", 7) == 0);
}

static LLVMValueRef get_eh_tsd_key(zan_irgen_t *g) {
    LLVMValueRef v = LLVMGetNamedGlobal(g->mod, "__zan_eh_tsd_key");
    if (v) return v;
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    v = LLVMAddGlobal(g->mod, i64t, "__zan_eh_tsd_key");
    LLVMSetLinkage(v, LLVMInternalLinkage);
    LLVMSetInitializer(v, LLVMConstInt(i64t, 0, 0));
    return v;
}

/* &base[key] of the calling thread's TSD array. */
static LLVMValueRef emit_eh_tsd_slot(zan_irgen_t *g, LLVMValueRef key) {
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef asm_ty = LLVMFunctionType(i64t, NULL, 0, 0);
    LLVMValueRef rd = LLVMGetInlineAsm(asm_ty, "mrs $0, TPIDRRO_EL0", 19,
        "=r", 2, 0, 0, LLVMInlineAsmDialectATT, 0);
    LLVMValueRef raw = LLVMBuildCall2(g->builder, asm_ty, rd, NULL, 0, "tsd.raw");
    LLVMValueRef base = LLVMBuildAnd(g->builder, raw,
        LLVMConstInt(i64t, ~(unsigned long long)7, 0), "tsd.base");
    LLVMValueRef off = LLVMBuildShl(g->builder, key,
        LLVMConstInt(i64t, 3, 0), "tsd.off");
    return LLVMBuildIntToPtr(g->builder,
        LLVMBuildAdd(g->builder, base, off, "tsd.addr"),
        LLVMPointerType(i8ptr, 0), "tsd.slot");
}

/* Create, self-check and publish the key once (racing threads: the CAS
 * winner's key stays, a loser deletes its own). */
static LLVMValueRef get_eh_tsd_init_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_tsd_init");
    if (fn) return fn;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef voidt = LLVMVoidTypeInContext(g->ctx);
    LLVMValueRef key_gv = get_eh_tsd_key(g);
    LLVMTypeRef create_ty = LLVMFunctionType(i32t,
        (LLVMTypeRef[]){ LLVMPointerType(i64t, 0), i8ptr }, 2, 0);
    LLVMTypeRef set_ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i64t, i8ptr }, 2, 0);
    LLVMTypeRef del_ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i64t }, 1, 0);
    LLVMValueRef create_fn = get_libc_fn(g, "pthread_key_create", create_ty);
    LLVMValueRef set_fn = get_libc_fn(g, "pthread_setspecific", set_ty);
    LLVMValueRef del_fn = get_libc_fn(g, "pthread_key_delete", del_ty);
    fn = LLVMAddFunction(g->mod, "__zan_eh_tsd_init", LLVMFunctionType(voidt, NULL, 0, 0));
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    add_enum_attr(g, fn, NULL, "noinline");
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef check = LLVMAppendBasicBlockInContext(g->ctx, fn, "check");
    LLVMBasicBlockRef bad = LLVMAppendBasicBlockInContext(g->ctx, fn, "bad");
    LLVMBasicBlockRef pub = LLVMAppendBasicBlockInContext(g->ctx, fn, "publish");
    LLVMBasicBlockRef lost = LLVMAppendBasicBlockInContext(g->ctx, fn, "lost");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef tmp = LLVMBuildAlloca(g->builder, i64t, "k.tmp");
    LLVMBuildStore(g->builder, LLVMConstInt(i64t, 0, 0), tmp);
    LLVMValueRef rc = zan_call2(g->builder, create_ty, create_fn,
        (LLVMValueRef[]){ tmp, LLVMConstNull(i8ptr) }, 2, "k.rc");
    LLVMValueRef k = LLVMBuildLoad2(g->builder, i64t, tmp, "k");
    LLVMValueRef ok_rc = zan_icmp(g->builder, LLVMIntEQ, rc,
        LLVMConstInt(i32t, 0, 0), "k.created");
    LLVMValueRef ok_k = zan_icmp(g->builder, LLVMIntSGT, k,
        LLVMConstInt(i64t, 0, 0), "k.pos");
    LLVMBuildCondBr(g->builder, LLVMBuildAnd(g->builder, ok_rc, ok_k, "k.ok"),
        check, bad);
    /* self-check: the direct read must see what setspecific stored */
    LLVMPositionBuilderAtEnd(g->builder, check);
    LLVMValueRef marker = LLVMBuildBitCast(g->builder, tmp, i8ptr, "k.marker");
    zan_call2(g->builder, set_ty, set_fn, (LLVMValueRef[]){ k, marker }, 2, "");
    LLVMValueRef seen = LLVMBuildLoad2(g->builder, i8ptr, emit_eh_tsd_slot(g, k), "k.seen");
    zan_call2(g->builder, set_ty, set_fn,
        (LLVMValueRef[]){ k, LLVMConstNull(i8ptr) }, 2, "");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, seen, marker, "k.match"), pub, bad);
    LLVMPositionBuilderAtEnd(g->builder, bad);
    LLVMValueRef bad_x = LLVMBuildAtomicCmpXchg(g->builder, key_gv,
        LLVMConstInt(i64t, 0, 0), LLVMConstInt(i64t, (unsigned long long)-1, 1),
        LLVMAtomicOrderingSequentiallyConsistent,
        LLVMAtomicOrderingSequentiallyConsistent, 0);
    (void)bad_x;
    LLVMBuildBr(g->builder, done);
    LLVMPositionBuilderAtEnd(g->builder, pub);
    LLVMValueRef x = LLVMBuildAtomicCmpXchg(g->builder, key_gv,
        LLVMConstInt(i64t, 0, 0), k,
        LLVMAtomicOrderingSequentiallyConsistent,
        LLVMAtomicOrderingSequentiallyConsistent, 0);
    LLVMBuildCondBr(g->builder, LLVMBuildExtractValue(g->builder, x, 1, "k.won"),
        done, lost);
    LLVMPositionBuilderAtEnd(g->builder, lost);
    zan_call2(g->builder, del_ty, del_fn, &k, 1, "");
    LLVMBuildBr(g->builder, done);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* Slow path: the portable lookup, then publish the state in the TSD slot
 * so this thread's next lookups take the direct read. */
static LLVMValueRef get_eh_tsd_state_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_state_tsd");
    if (fn) return fn;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, NULL, 0, 0);
    LLVMTypeRef set_ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i64t, i8ptr }, 2, 0);
    LLVMValueRef set_fn = get_libc_fn(g, "pthread_setspecific", set_ty);
    LLVMValueRef slow = get_eh_state_fn(g);
    LLVMValueRef init_fn = get_eh_tsd_init_fn(g);
    LLVMValueRef key_gv = get_eh_tsd_key(g);
    fn = LLVMAddFunction(g->mod, "__zan_eh_state_tsd", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    add_enum_attr(g, fn, NULL, "noinline");
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef init = LLVMAppendBasicBlockInContext(g->ctx, fn, "init");
    LLVMBasicBlockRef reload = LLVMAppendBasicBlockInContext(g->ctx, fn, "reload");
    LLVMBasicBlockRef set = LLVMAppendBasicBlockInContext(g->ctx, fn, "set");
    LLVMBasicBlockRef out = LLVMAppendBasicBlockInContext(g->ctx, fn, "out");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef st = zan_call2(g->builder, fnty, slow, NULL, 0, "st");
    LLVMValueRef k0 = LLVMBuildLoad2(g->builder, i64t, key_gv, "k0");
    LLVMSetOrdering(k0, LLVMAtomicOrderingAcquire);
    LLVMBuildCondBr(g->builder, zan_icmp(g->builder, LLVMIntEQ, k0,
        LLVMConstInt(i64t, 0, 0), "k.unset"), init, reload);
    LLVMPositionBuilderAtEnd(g->builder, init);
    zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0),
        init_fn, NULL, 0, "");
    LLVMBuildBr(g->builder, reload);
    LLVMPositionBuilderAtEnd(g->builder, reload);
    LLVMValueRef k = LLVMBuildLoad2(g->builder, i64t, key_gv, "k");
    LLVMSetOrdering(k, LLVMAtomicOrderingAcquire);
    LLVMBuildCondBr(g->builder, zan_icmp(g->builder, LLVMIntSGT, k,
        LLVMConstInt(i64t, 0, 0), "k.live"), set, out);
    LLVMPositionBuilderAtEnd(g->builder, set);
    zan_call2(g->builder, set_ty, set_fn, (LLVMValueRef[]){ k, st }, 2, "");
    LLVMBuildBr(g->builder, out);
    LLVMPositionBuilderAtEnd(g->builder, out);
    LLVMBuildRet(g->builder, st);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* Thread release: the state is freed, so the TSD slot must not keep it. */
static void emit_eh_tsd_clear(zan_irgen_t *g) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef k = LLVMBuildLoad2(g->builder, i64t, get_eh_tsd_key(g), "k.rel");
    LLVMSetOrdering(k, LLVMAtomicOrderingAcquire);
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
    LLVMValueRef fn = LLVMGetBasicBlockParent(cur);
    LLVMBasicBlockRef clr = LLVMAppendBasicBlockInContext(g->ctx, fn, "tsd.clr");
    LLVMBasicBlockRef cont = LLVMAppendBasicBlockInContext(g->ctx, fn, "tsd.cont");
    LLVMBuildCondBr(g->builder, zan_icmp(g->builder, LLVMIntSGT, k,
        LLVMConstInt(i64t, 0, 0), "k.rel.live"), clr, cont);
    LLVMPositionBuilderAtEnd(g->builder, clr);
    LLVMTypeRef set_ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i64t, i8ptr }, 2, 0);
    zan_call2(g->builder, set_ty, get_libc_fn(g, "pthread_setspecific", set_ty),
        (LLVMValueRef[]){ k, LLVMConstNull(i8ptr) }, 2, "");
    LLVMBuildBr(g->builder, cont);
    LLVMPositionBuilderAtEnd(g->builder, cont);
}
static LLVMValueRef get_eh_state_fast_tsd_fn(zan_irgen_t *g) {
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, NULL, 0, 0);
    LLVMValueRef slow = get_eh_tsd_state_fn(g);
    LLVMValueRef key_gv = get_eh_tsd_key(g);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_eh_state_fast", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    add_enum_attr(g, fn, NULL, "alwaysinline");
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef rd = LLVMAppendBasicBlockInContext(g->ctx, fn, "tsd");
    LLVMBasicBlockRef tls = LLVMAppendBasicBlockInContext(g->ctx, fn, "tls");
    LLVMBasicBlockRef slowbb = LLVMAppendBasicBlockInContext(g->ctx, fn, "slow");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef k = LLVMBuildLoad2(g->builder, i64t, key_gv, "st.k");
    LLVMSetOrdering(k, LLVMAtomicOrderingMonotonic);
    LLVMBuildCondBr(g->builder, zan_icmp(g->builder, LLVMIntSGT, k,
        LLVMConstInt(i64t, 0, 0), "st.k.live"), rd, tls);
    LLVMPositionBuilderAtEnd(g->builder, rd);
    LLVMValueRef cached = LLVMBuildLoad2(g->builder, i8ptr,
        emit_eh_tsd_slot(g, k), "st.c");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, cached, LLVMConstNull(i8ptr), "st.miss"),
        slowbb, done);
    /* key unset or disabled: the TLV cache, as before this fast path */
    LLVMPositionBuilderAtEnd(g->builder, tls);
    LLVMValueRef tcached = LLVMBuildLoad2(g->builder, i8ptr,
        get_eh_self_slot(g), "st.t");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, tcached, LLVMConstNull(i8ptr), "st.tmiss"),
        slowbb, done);
    LLVMPositionBuilderAtEnd(g->builder, slowbb);
    LLVMValueRef fresh = zan_call2(g->builder, fnty, slow, NULL, 0, "st.slow");
    LLVMBuildBr(g->builder, done);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMValueRef phi = LLVMBuildPhi(g->builder, i8ptr, "st.r");
    LLVMAddIncoming(phi, (LLVMValueRef[]){ cached, tcached, fresh },
        (LLVMBasicBlockRef[]){ rd, tls, slowbb }, 3);
    LLVMBuildRet(g->builder, phi);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

static LLVMValueRef get_eh_state_fast_fn(zan_irgen_t *g) {
    LLVMValueRef slow = get_eh_state_fn(g);
    LLVMValueRef self_slot = get_eh_self_slot(g);
    if (!self_slot) return slow;
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_state_fast");
    if (fn) return fn;
    if (eh_apple_tsd(g)) return get_eh_state_fast_tsd_fn(g);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, NULL, 0, 0);
    fn = LLVMAddFunction(g->mod, "__zan_eh_state_fast", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    add_enum_attr(g, fn, NULL, "alwaysinline");
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef slowbb = LLVMAppendBasicBlockInContext(g->ctx, fn, "slow");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef cached = LLVMBuildLoad2(g->builder, i8ptr, self_slot, "st.c");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, cached, LLVMConstNull(i8ptr), "st.miss"),
        slowbb, done);
    LLVMPositionBuilderAtEnd(g->builder, slowbb);
    LLVMValueRef fresh = zan_call2(g->builder, fnty, slow, NULL, 0, "st.slow");
    LLVMBuildBr(g->builder, done);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMValueRef phi = LLVMBuildPhi(g->builder, i8ptr, "st.r");
    LLVMAddIncoming(phi, (LLVMValueRef[]){ cached, fresh },
        (LLVMBasicBlockRef[]){ entry, slowbb }, 2);
    LLVMBuildRet(g->builder, phi);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

static LLVMValueRef emit_eh_field_ptr(zan_irgen_t *g, unsigned field,
                                      const char *name) {
    LLVMValueRef st = emit_eh_state(g);
    if (g->eh_state_fields[field]) return g->eh_state_fields[field];
    LLVMBasicBlockRef cur = eh_enter_entry_block(g);
    LLVMValueRef p = LLVMBuildStructGEP2(g->builder, get_eh_state_ty(g), st,
        field, name);
    LLVMPositionBuilderAtEnd(g->builder, cur);
    g->eh_state_fields[field] = p;
    return p;
}
void get_eh_globals(zan_irgen_t *g, LLVMValueRef *top,
                           LLVMValueRef *bufs, LLVMValueRef *exc) {
    *top = emit_eh_field_ptr(g, EH_F_TOP, "eh.topp");
    *exc = emit_eh_field_ptr(g, EH_F_EXC, "eh.excp");
    if (bufs) *bufs = emit_eh_field_ptr(g, EH_F_BUFS, "eh.bufsp");
}

static void emit_eh_oom_abort(zan_irgen_t *g, const char *msg) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef pty = LLVMFunctionType(i32t, &i8ptr, 1, 1);
    LLVMValueRef fmt = zan_irgen_intern_string(g, msg);
    zan_call2(g->builder, pty, get_libc_fn(g, "printf", pty), &fmt, 1, "");
    LLVMTypeRef ety = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i32t, 1, 0);
    zan_call2(g->builder, ety, get_libc_fn(g, "exit", ety),
        (LLVMValueRef[]){ LLVMConstInt(i32t, 1, 0) }, 1, "");
    LLVMBuildUnreachable(g->builder);
}

static void emit_eh_release_fn(zan_irgen_t *g) {
    if (LLVMGetNamedFunction(g->mod, "__zan_eh_release")) return;
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64ptr = LLVMPointerType(i64t, 0);
    LLVMTypeRef states_ptr_ty = LLVMPointerType(i8ptr, 0);
    LLVMTypeRef voidt = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef state_ty = get_eh_state_ty(g);
    LLVMTypeRef tab_ty = LLVMArrayType(i8ptr, ZAN_EH_CHUNKS);
    LLVMTypeRef free_ty = LLVMFunctionType(voidt, &i8ptr, 1, 0);
    LLVMValueRef free_fn = get_libc_fn(g, "free", free_ty);
    LLVMValueRef keys_gv = get_eh_tab_keys(g);
    LLVMValueRef states_gv = get_eh_tab_states(g);
    LLVMValueRef cap_gv = get_eh_tab_cap(g);
    LLVMValueRef lock_gv = get_eh_tab_lock(g);

    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_eh_release",
        LLVMFunctionType(voidt, NULL, 0, 0));
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef probe = LLVMAppendBasicBlockInContext(g->ctx, fn, "probe");
    LLVMBasicBlockRef found = LLVMAppendBasicBlockInContext(g->ctx, fn, "found");
    LLVMBasicBlockRef live  = LLVMAppendBasicBlockInContext(g->ctx, fn, "live");
    LLVMBasicBlockRef cloop = LLVMAppendBasicBlockInContext(g->ctx, fn, "cloop");
    LLVMBasicBlockRef cbody = LLVMAppendBasicBlockInContext(g->ctx, fn, "cbody");
    LLVMBasicBlockRef drop  = LLVMAppendBasicBlockInContext(g->ctx, fn, "drop");
    LLVMBasicBlockRef next  = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
    LLVMBasicBlockRef done  = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef key = zan_add(g->builder, emit_eh_thread_id(g),
        LLVMConstInt(i64t, 1, 0), "key");
    LLVMValueRef i_slot = LLVMBuildAlloca(g->builder, i64t, "i");
    LLVMValueRef c_slot = LLVMBuildAlloca(g->builder, i64t, "c");
    LLVMValueRef sp_slot = LLVMBuildAlloca(g->builder, LLVMPointerType(i8ptr, 0), "spp");
    LLVMValueRef st_slot = LLVMBuildAlloca(g->builder, i8ptr, "stp");
    LLVMValueRef kp_slot = LLVMBuildAlloca(g->builder, LLVMPointerType(i64t, 0), "kpp");
    emit_eh_tab_lock_acquire(g, lock_gv);
    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64t, cap_gv, "st.cap");
    LLVMValueRef capm1 = LLVMBuildSub(g->builder, cap,
        LLVMConstInt(i64t, 1, 0), "st.capm1");
    LLVMValueRef h = zan_and(g->builder,
        LLVMBuildXor(g->builder, key,
            LLVMBuildLShr(g->builder, key, LLVMConstInt(i64t, 32, 0), "key.hi"),
            "key.mix"),
        capm1, "h");
    LLVMBuildStore(g->builder, LLVMConstInt(i64t, 0, 0), i_slot);
    LLVMValueRef keys = LLVMBuildLoad2(g->builder, i64ptr, keys_gv, "st.keys");
    LLVMValueRef states = LLVMBuildLoad2(g->builder, states_ptr_ty, states_gv,
                                         "st.states");
    LLVMBuildBr(g->builder, probe);

    LLVMPositionBuilderAtEnd(g->builder, probe);
    LLVMValueRef zero64 = LLVMConstInt(i64t, 0, 0);
    LLVMValueRef i = LLVMBuildLoad2(g->builder, i64t, i_slot, "i.v");
    LLVMValueRef pos = zan_and(g->builder, zan_add(g->builder, h, i, "pos.raw"),
        capm1, "pos");
    LLVMValueRef kp = LLVMBuildGEP2(g->builder, i64ptr, keys, &pos, 1, "kp");
    LLVMValueRef sp = LLVMBuildGEP2(g->builder, states_ptr_ty, states, &pos, 1, "sp");
    LLVMBuildStore(g->builder, kp, kp_slot);
    LLVMBuildStore(g->builder, sp, sp_slot);
    LLVMValueRef k = LLVMBuildLoad2(g->builder, i64t, kp, "k");
    LLVMSetAlignment(k, 8);
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, k, key, "is.mine"), found, next);

    LLVMPositionBuilderAtEnd(g->builder, next);
    LLVMValueRef k2 = LLVMBuildLoad2(g->builder, i64t,
        LLVMBuildLoad2(g->builder, LLVMPointerType(i64t, 0), kp_slot, "kp.v"),
        "k2");
    LLVMValueRef ni = zan_add(g->builder,
        LLVMBuildLoad2(g->builder, i64t, i_slot, "i.v2"),
        LLVMConstInt(i64t, 1, 0), "i.next");
    LLVMBuildStore(g->builder, ni, i_slot);
    LLVMBuildCondBr(g->builder,
        LLVMBuildOr(g->builder,
            zan_icmp(g->builder, LLVMIntEQ, k2, zero64, "is.empty"),
            zan_icmp(g->builder, LLVMIntUGE, ni, cap, "wrapped"), "stop"),
        done, probe);

    LLVMPositionBuilderAtEnd(g->builder, found);
    LLVMValueRef st = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), sp_slot, "sp.v"),
        "st");
    LLVMBuildStore(g->builder, st, st_slot);
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, st, LLVMConstNull(i8ptr), "no.state"),
        drop, live);

    LLVMPositionBuilderAtEnd(g->builder, live);
    LLVMBuildStore(g->builder, zero64, c_slot);
    LLVMBuildBr(g->builder, cloop);

    LLVMPositionBuilderAtEnd(g->builder, cloop);
    LLVMValueRef c = LLVMBuildLoad2(g->builder, i64t, c_slot, "c.v");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntUGE, c,
            LLVMConstInt(i64t, ZAN_EH_CHUNKS, 0), "chunks.done"), drop, cbody);

    LLVMPositionBuilderAtEnd(g->builder, cbody);
    LLVMValueRef stp = LLVMBuildBitCast(g->builder,
        LLVMBuildLoad2(g->builder, i8ptr, st_slot, "st.v"),
        LLVMPointerType(state_ty, 0), "st.typed");
    unsigned tabs[2] = { EH_F_BUFS, EH_F_TMPS };
    for (int t = 0; t < 2; t++) {
        LLVMValueRef tab = LLVMBuildStructGEP2(g->builder, state_ty, stp,
            tabs[t], "tab");
        LLVMValueRef cp = LLVMBuildGEP2(g->builder, tab_ty, tab,
            (LLVMValueRef[]){ zero64, c }, 2, "cp");
        LLVMValueRef chunk = LLVMBuildLoad2(g->builder, i8ptr, cp, "chunk");
        zan_call2(g->builder, free_ty, free_fn, &chunk, 1, "");
    }
    LLVMBuildStore(g->builder,
        zan_add(g->builder, c, LLVMConstInt(i64t, 1, 0), "c.next"), c_slot);
    LLVMBuildBr(g->builder, cloop);

    LLVMPositionBuilderAtEnd(g->builder, drop);
    LLVMValueRef stv = LLVMBuildLoad2(g->builder, i8ptr, st_slot, "st.v2");
    zan_call2(g->builder, free_ty, free_fn, &stv, 1, "");
    LLVMValueRef eh_self = get_eh_self_slot(g);
    if (eh_self)
        LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), eh_self);
    if (eh_self && eh_apple_tsd(g)) emit_eh_tsd_clear(g);
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr),
        LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), sp_slot, "sp.v2"));
    LLVMBuildStore(g->builder,
        LLVMConstInt(i64t, ZAN_EH_TOMBSTONE, 0),
        LLVMBuildLoad2(g->builder, LLVMPointerType(i64t, 0), kp_slot, "kp.v2"));
    LLVMBuildBr(g->builder, done);

    LLVMPositionBuilderAtEnd(g->builder, done);
    emit_eh_tab_lock_release(g, lock_gv);
    LLVMBuildRetVoid(g->builder);
}

static LLVMValueRef get_eh_state_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_eh_state");
    if (fn) return fn;
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8t, 0);
    LLVMTypeRef state_ty = get_eh_state_ty(g);
    LLVMTypeRef i64ptr = LLVMPointerType(i64t, 0);
    LLVMTypeRef states_ptr_ty = LLVMPointerType(i8ptr, 0);
    LLVMValueRef keys_gv = get_eh_tab_keys(g);
    LLVMValueRef states_gv = get_eh_tab_states(g);
    LLVMValueRef cap_gv = get_eh_tab_cap(g);
    LLVMValueRef lock_gv = get_eh_tab_lock(g);
    fn = LLVMAddFunction(g->mod, "__zan_eh_state",
        LLVMFunctionType(i8ptr, NULL, 0, 0));
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMTypeRef caty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64t, i64t }, 2, 0);
    LLVMValueRef calloc_fn = get_libc_fn(g, "calloc", caty);
    LLVMTypeRef free_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    LLVMValueRef free_fn = get_libc_fn(g, "free", free_ty);
    LLVMValueRef self_slot = get_eh_self_slot(g);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef hit   = LLVMAppendBasicBlockInContext(g->ctx, fn, "hit");
    LLVMBasicBlockRef look  = LLVMAppendBasicBlockInContext(g->ctx, fn, "look");
    LLVMBasicBlockRef retry = LLVMAppendBasicBlockInContext(g->ctx, fn, "retry");
    LLVMBasicBlockRef probe = LLVMAppendBasicBlockInContext(g->ctx, fn, "probe");
    LLVMBasicBlockRef mine  = LLVMAppendBasicBlockInContext(g->ctx, fn, "mine");
    LLVMBasicBlockRef claim = LLVMAppendBasicBlockInContext(g->ctx, fn, "claim");
    LLVMBasicBlockRef make  = LLVMAppendBasicBlockInContext(g->ctx, fn, "make");
    LLVMBasicBlockRef init  = LLVMAppendBasicBlockInContext(g->ctx, fn, "init");
    LLVMBasicBlockRef oom   = LLVMAppendBasicBlockInContext(g->ctx, fn, "oom");
    LLVMBasicBlockRef ready = LLVMAppendBasicBlockInContext(g->ctx, fn, "ready");
    LLVMBasicBlockRef next  = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
    LLVMBasicBlockRef full  = LLVMAppendBasicBlockInContext(g->ctx, fn, "full");
    LLVMBasicBlockRef probe2 = LLVMAppendBasicBlockInContext(g->ctx, fn, "probe2");

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef i_slot = LLVMBuildAlloca(g->builder, i64t, "i");
    if (self_slot) {
        LLVMValueRef cached = LLVMBuildLoad2(g->builder, i8ptr, self_slot,
                                            "st.cached");
        LLVMBuildCondBr(g->builder,
            zan_icmp(g->builder, LLVMIntNE, cached, LLVMConstNull(i8ptr),
                     "cached.ok"), hit, look);
        LLVMPositionBuilderAtEnd(g->builder, hit);
        LLVMBuildRet(g->builder, cached);
    } else {
        LLVMBuildBr(g->builder, look);
        LLVMDeleteBasicBlock(hit);
    }

    LLVMPositionBuilderAtEnd(g->builder, look);
    emit_eh_tab_lock_acquire(g, lock_gv);
    LLVMValueRef cap0 = LLVMBuildLoad2(g->builder, i64t, cap_gv, "st.cap0");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, cap0, LLVMConstInt(i64t, 0, 0), "st.fresh"),
        retry, probe);

    LLVMPositionBuilderAtEnd(g->builder, retry);
    emit_eh_tab_grow(g, state_ty, calloc_fn, free_ty, free_fn);
    LLVMBuildBr(g->builder, probe);

    LLVMPositionBuilderAtEnd(g->builder, probe);
    LLVMValueRef key = zan_add(g->builder, emit_eh_thread_id(g),
        LLVMConstInt(i64t, 1, 0), "key");
    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64t, cap_gv, "st.cap");
    LLVMValueRef capm1 = LLVMBuildSub(g->builder, cap,
        LLVMConstInt(i64t, 1, 0), "st.capm1");
    LLVMValueRef h = zan_and(g->builder,
        LLVMBuildXor(g->builder, key,
            LLVMBuildLShr(g->builder, key, LLVMConstInt(i64t, 32, 0), "key.hi"),
            "key.mix"),
        capm1, "h");
    LLVMValueRef keys = LLVMBuildLoad2(g->builder, i64ptr, keys_gv, "st.keys");
    LLVMValueRef states = LLVMBuildLoad2(g->builder, states_ptr_ty, states_gv,
                                         "st.states");
    LLVMBuildStore(g->builder, LLVMConstInt(i64t, 0, 0), i_slot);
    LLVMBuildBr(g->builder, probe2);

    LLVMPositionBuilderAtEnd(g->builder, probe2);
    LLVMValueRef i = LLVMBuildLoad2(g->builder, i64t, i_slot, "i.v");
    LLVMValueRef pos = zan_and(g->builder, zan_add(g->builder, h, i, "pos.raw"),
        capm1, "pos");
    LLVMValueRef zero64 = LLVMConstInt(i64t, 0, 0);
    LLVMValueRef kp = LLVMBuildGEP2(g->builder, i64ptr, keys, &pos, 1, "kp");
    LLVMValueRef sp = LLVMBuildGEP2(g->builder, states_ptr_ty, states, &pos, 1, "sp");
    LLVMValueRef k = LLVMBuildLoad2(g->builder, i64t, kp, "k");
    LLVMSetAlignment(k, 8);
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, k, key, "is.mine"), mine, claim);

    LLVMPositionBuilderAtEnd(g->builder, mine);
    LLVMValueRef st_found = LLVMBuildLoad2(g->builder, i8ptr, sp, "st");
    if (self_slot) LLVMBuildStore(g->builder, st_found, self_slot);
    emit_eh_tab_lock_release(g, lock_gv);
    LLVMBuildRet(g->builder, st_found);

    LLVMPositionBuilderAtEnd(g->builder, claim);
    LLVMValueRef reusable = LLVMBuildOr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, k, LLVMConstInt(i64t, 0, 0), "is.free"),
        zan_icmp(g->builder, LLVMIntEQ, k,
            LLVMConstInt(i64t, ZAN_EH_TOMBSTONE, 0), "is.dead"), "reusable");
    LLVMBuildCondBr(g->builder, reusable, make, next);

    /* Claim it */
    LLVMPositionBuilderAtEnd(g->builder, make);
    LLVMValueRef xchg = LLVMBuildAtomicCmpXchg(g->builder, kp,
        k, key,
        LLVMAtomicOrderingSequentiallyConsistent,
        LLVMAtomicOrderingSequentiallyConsistent, 0);
    LLVMBuildCondBr(g->builder,
        LLVMBuildExtractValue(g->builder, xchg, 1, "won"), init, next);

    LLVMPositionBuilderAtEnd(g->builder, init);
    LLVMValueRef st = zan_call2(g->builder, caty, calloc_fn,
        (LLVMValueRef[]){ LLVMConstInt(i64t, 1, 0), LLVMSizeOf(state_ty) }, 2,
        "st.new");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, st, LLVMConstNull(i8ptr), "nomem"),
        oom, ready);

    LLVMPositionBuilderAtEnd(g->builder, oom);
    emit_eh_oom_abort(g,
        "zan: out of memory creating this thread's exception state\n");

    LLVMPositionBuilderAtEnd(g->builder, ready);
    LLVMBuildStore(g->builder, LLVMConstInt(i32t, (unsigned long long)-1, 1),
        LLVMBuildStructGEP2(g->builder, state_ty,
            LLVMBuildBitCast(g->builder, st, LLVMPointerType(state_ty, 0),
                "st.typed"), EH_F_TOP, "st.top"));
    LLVMBuildStore(g->builder, st, sp);
    if (self_slot) LLVMBuildStore(g->builder, st, self_slot);
    emit_eh_tab_lock_release(g, lock_gv);
    LLVMBuildRet(g->builder, st);

    LLVMPositionBuilderAtEnd(g->builder, next);
    LLVMValueRef ni = zan_add(g->builder,
        LLVMBuildLoad2(g->builder, i64t, i_slot, "i.v2"),
        LLVMConstInt(i64t, 1, 0), "i.next");
    LLVMBuildStore(g->builder, ni, i_slot);
    LLVMValueRef cap2 = LLVMBuildLoad2(g->builder, i64t, cap_gv, "st.cap2");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntUGE, ni, cap2, "exhausted"), full, probe2);

    LLVMPositionBuilderAtEnd(g->builder, full);
    emit_eh_tab_grow(g, state_ty, calloc_fn, free_ty, free_fn);
    LLVMBuildBr(g->builder, probe);

    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

static LLVMValueRef get_eh_chunk_fn(zan_irgen_t *g, const char *name,
                                    unsigned field, unsigned shift,
                                    unsigned elem, const char *deep_msg) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, name);
    if (fn) return fn;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef tab_ty = LLVMArrayType(i8ptr, ZAN_EH_CHUNKS);
    LLVMTypeRef state_ty = get_eh_state_ty(g);
    fn = LLVMAddFunction(g->mod, name,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0));
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMTypeRef caty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64t, i64t }, 2, 0);
    LLVMValueRef calloc_fn = get_libc_fn(g, "calloc", caty);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef deep = LLVMAppendBasicBlockInContext(g->ctx, fn, "deep");
    LLVMBasicBlockRef look = LLVMAppendBasicBlockInContext(g->ctx, fn, "look");
    LLVMBasicBlockRef make = LLVMAppendBasicBlockInContext(g->ctx, fn, "make");
    LLVMBasicBlockRef oom = LLVMAppendBasicBlockInContext(g->ctx, fn, "oom");
    LLVMBasicBlockRef have = LLVMAppendBasicBlockInContext(g->ctx, fn, "have");
    LLVMValueRef idx = LLVMGetParam(fn, 1);
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef tab = LLVMBuildStructGEP2(g->builder, state_ty,
        LLVMBuildBitCast(g->builder, LLVMGetParam(fn, 0),
            LLVMPointerType(state_ty, 0), "st"), field, "tab");
    LLVMValueRef ci = LLVMBuildLShr(g->builder, idx,
        LLVMConstInt(i32t, shift, 0), "chunk");
    LLVMBuildCondBr(g->builder, zan_icmp(g->builder, LLVMIntUGE, ci,
        LLVMConstInt(i32t, ZAN_EH_CHUNKS, 0), "toodeep"), deep, look);
    LLVMPositionBuilderAtEnd(g->builder, deep);
    emit_eh_oom_abort(g, deep_msg);
    LLVMPositionBuilderAtEnd(g->builder, look);
    LLVMValueRef cslot = LLVMBuildGEP2(g->builder, tab_ty, tab,
        (LLVMValueRef[]){ LLVMConstInt(i32t, 0, 0), ci }, 2, "cslot");
    LLVMValueRef p = LLVMBuildLoad2(g->builder, i8ptr, cslot, "chunkp");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, p, LLVMConstNull(i8ptr), "fresh"),
        make, have);
    LLVMPositionBuilderAtEnd(g->builder, make);
    LLVMValueRef np = zan_call2(g->builder, caty, calloc_fn, (LLVMValueRef[]){
        LLVMConstInt(i64t, 1ULL << shift, 0),
        LLVMConstInt(i64t, elem, 0) }, 2, "nchunk");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, np, LLVMConstNull(i8ptr), "nomem"),
        oom, have);
    LLVMPositionBuilderAtEnd(g->builder, oom);
    emit_eh_oom_abort(g, "zan: out of memory extending the exception stack\n");
    LLVMPositionBuilderAtEnd(g->builder, have);
    LLVMValueRef base = LLVMBuildPhi(g->builder, i8ptr, "base");
    LLVMAddIncoming(base, (LLVMValueRef[]){ p, np },
        (LLVMBasicBlockRef[]){ look, make }, 2);
    LLVMBuildStore(g->builder, base, cslot);
    LLVMValueRef off = LLVMBuildMul(g->builder,
        LLVMBuildZExt(g->builder, zan_and(g->builder, idx,
            LLVMConstInt(i32t, (1u << shift) - 1, 0), "inchunk"), i64t, "off32"),
        LLVMConstInt(i64t, elem, 0), "off");
    LLVMBuildRet(g->builder,
        LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), base, &off, 1, "ep"));
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

static LLVMValueRef emit_eh_chunk_call(zan_irgen_t *g, LLVMValueRef fn,
                                      LLVMValueRef idx, const char *name) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef args[2] = {
        LLVMBuildBitCast(g->builder, emit_eh_state(g), i8ptr, "eh.stb"), idx };
    return zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
        fn, args, 2, name);
}

static bool eh_bare_target(zan_irgen_t *g) {
    return strncmp(g->target_triple, "riscv", 5) == 0 &&
           !strstr(g->target_triple, "linux");
}

static unsigned eh_slot_shift(zan_irgen_t *g) {
    return eh_bare_target(g) ? 1 : ZAN_EH_SLOT_SHIFT;
}

static unsigned eh_tmp_shift(zan_irgen_t *g) {
    return eh_bare_target(g) ? 6 : ZAN_EH_TMP_SHIFT;
}

