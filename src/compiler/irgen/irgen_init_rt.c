/* FRAGMENT: host=zan_irgen_init
 * 拼接片段（非独立 TU）：由 irgen_init.c 的 zan_irgen_init() 函数体末尾
 * #include。 consumes: g defines: none （运行时助手 LLVM
 * 声明与泄漏追踪初始化，修改 g 上下文。） */
LLVMValueRef arc_trace_ev = NULL;
LLVMTypeRef arc_ra_ty = NULL;
LLVMTypeRef arc_ev_ty = NULL;
if (g->check_leaks) {
  LLVMTypeRef arc_i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef arc_i32t = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef arc_i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMValueRef arc_trace_glob =
      LLVMAddGlobal(g->mod, arc_i32t, "__zan_arc_trace");
  LLVMSetInitializer(arc_trace_glob, LLVMConstInt(arc_i32t, (uint64_t)-1, 0));
  LLVMSetLinkage(arc_trace_glob, LLVMInternalLinkage);
  LLVMTypeRef arc_getenv_ty =
      LLVMFunctionType(arc_i8p, (LLVMTypeRef[]){arc_i8p}, 1, 0);
  LLVMValueRef arc_getenv = LLVMGetNamedFunction(g->mod, "getenv");
  if (!arc_getenv)
    arc_getenv = LLVMAddFunction(g->mod, "getenv", arc_getenv_ty);
  (void)zan_irgen_returnaddress_fn(g, &arc_ra_ty);
  LLVMTypeRef arc_ev_args[] = {arc_i8p, arc_i8p, arc_i64, arc_i64, arc_i8p};
  arc_ev_ty =
      LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), arc_ev_args, 5, 0);
  arc_trace_ev = LLVMAddFunction(g->mod, "__zan_arc_trace_ev", arc_ev_ty);
  LLVMSetLinkage(arc_trace_ev, LLVMInternalLinkage);
  LLVMBasicBlockRef ev_bb =
      LLVMAppendBasicBlockInContext(g->ctx, arc_trace_ev, "entry");
  LLVMBasicBlockRef ev_init =
      LLVMAppendBasicBlockInContext(g->ctx, arc_trace_ev, "init");
  LLVMBasicBlockRef ev_check =
      LLVMAppendBasicBlockInContext(g->ctx, arc_trace_ev, "check");
  LLVMBasicBlockRef ev_print =
      LLVMAppendBasicBlockInContext(g->ctx, arc_trace_ev, "print");
  LLVMBasicBlockRef ev_ret =
      LLVMAppendBasicBlockInContext(g->ctx, arc_trace_ev, "ret");
  LLVMPositionBuilderAtEnd(g->builder, ev_bb);
  LLVMValueRef ev_tag = LLVMGetParam(arc_trace_ev, 0);
  LLVMValueRef ev_obj = LLVMGetParam(arc_trace_ev, 1);
  LLVMValueRef ev_rc = LLVMGetParam(arc_trace_ev, 2);
  LLVMValueRef ev_site = LLVMGetParam(arc_trace_ev, 3);
  LLVMValueRef ev_ra = LLVMGetParam(arc_trace_ev, 4);
  LLVMValueRef ev_t = LLVMBuildLoad2(g->builder, arc_i32t, arc_trace_glob, "t");
  LLVMBuildCondBr(g->builder,
                  zan_icmp(g->builder, LLVMIntEQ, ev_t,
                           LLVMConstInt(arc_i32t, (uint64_t)-1, 0), "uninit"),
                  ev_init, ev_check);
  LLVMPositionBuilderAtEnd(g->builder, ev_init);
  LLVMValueRef ev_key = zan_irgen_intern_string(g, "ZAN_ARC_TRACE");
  LLVMValueRef ev_hit =
      zan_call2(g->builder, arc_getenv_ty, arc_getenv, &ev_key, 1, "hit");
  LLVMValueRef ev_on =
      zan_icmp(g->builder, LLVMIntNE, ev_hit, LLVMConstNull(arc_i8p), "on");
  LLVMValueRef ev_on32 = LLVMBuildZExt(g->builder, ev_on, arc_i32t, "on32");
  LLVMBuildStore(g->builder, ev_on32, arc_trace_glob);
  LLVMBuildBr(g->builder, ev_check);
  LLVMPositionBuilderAtEnd(g->builder, ev_check);
  LLVMValueRef ev_phi = LLVMBuildPhi(g->builder, arc_i32t, "t2");
  LLVMValueRef ev_incomings[2] = {ev_t, ev_on32};
  LLVMBasicBlockRef ev_inblocks[2] = {ev_bb, ev_init};
  LLVMAddIncoming(ev_phi, ev_incomings, ev_inblocks, 2);
  LLVMValueRef ev_off = zan_icmp(g->builder, LLVMIntEQ, ev_phi,
                                 LLVMConstInt(arc_i32t, 0, 0), "off");
  LLVMBuildCondBr(g->builder, ev_off, ev_ret, ev_print);
  LLVMPositionBuilderAtEnd(g->builder, ev_print);
  LLVMValueRef ev_obji = LLVMBuildPtrToInt(g->builder, ev_obj, arc_i64, "obji");
  LLVMValueRef ev_rai = LLVMBuildPtrToInt(g->builder, ev_ra, arc_i64, "rai");
  LLVMValueRef ev_msg = zan_irgen_intern_string(
      g, "[arc] %s obj=0x%llx site=%lld rc=%lld ra=0x%llx\n");
  LLVMValueRef ev_pargs[6] = {ev_msg, ev_tag, ev_obji, ev_site, ev_rc, ev_rai};
  zan_call2(g->builder, g->printf_type, g->fn_printf, ev_pargs, 6, "");
  LLVMBuildBr(g->builder, ev_ret);
  LLVMPositionBuilderAtEnd(g->builder, ev_ret);
  LLVMBuildRetVoid(g->builder);
  (void)ev_phi;
  (void)ev_obj;
  (void)ev_rc;
  (void)ev_site;
  (void)ev_ra;
  (void)ev_tag;
}

{
  LLVMTypeRef live_reg_args[] = {i8ptr, i64};
  g->rt_live_register = LLVMAddFunction(
      g->mod, "zan_rt_live_register",
      LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), live_reg_args, 2, 0));
  LLVMTypeRef live_named_args[] = {i8ptr, i64, i8ptr};
  g->rt_live_register_named = LLVMAddFunction(
      g->mod, "zan_rt_live_register_named",
      LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), live_named_args, 3, 0));
  LLVMTypeRef live_array_args[] = {i8ptr, i64, i64,
                                   LLVMInt32TypeInContext(g->ctx)};
  g->rt_live_register_array = LLVMAddFunction(
      g->mod, "zan_rt_live_register_array",
      LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), live_array_args, 4, 0));
  LLVMTypeRef live_unreg_args[] = {i8ptr};
  g->rt_live_unregister = LLVMAddFunction(
      g->mod, "zan_rt_live_unregister",
      LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), live_unreg_args, 1, 0));
}

LLVMTypeRef retain_args[] = {i8ptr};
LLVMTypeRef retain_type =
    LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), retain_args, 1, 0);
g->rt_retain = LLVMAddFunction(g->mod, "zan_rt_retain", retain_type);
zan_mark_nounwind(g, g->rt_retain);

{
  LLVMBasicBlockRef bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_retain, "entry");
  LLVMPositionBuilderAtEnd(g->builder, bb);
  LLVMValueRef obj = LLVMGetParam(g->rt_retain, 0);
  LLVMValueRef is_null =
      zan_icmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8ptr), "isnull");
  LLVMBasicBlockRef do_retain =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_retain, "retain");
  LLVMBasicBlockRef ret_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_retain, "ret");
  LLVMBuildCondBr(g->builder, is_null, ret_bb, do_retain);
  LLVMPositionBuilderAtEnd(g->builder, do_retain);
  LLVMTypeRef rc_ty = LLVMInt32TypeInContext(g->ctx);
  LLVMValueRef rc_iptr = zan_rc_ptr(g, g->builder, obj, "rcptr");
  /* 原子读一次引用计数（monotonic, align 4）：既是 immortal 判定，也是 freed 守卫的输入。 */
  LLVMValueRef rc_now = LLVMBuildLoad2(g->builder, rc_ty, rc_iptr, "rcnow");
  LLVMSetOrdering(rc_now, LLVMAtomicOrderingMonotonic);
  LLVMSetAlignment(rc_now, 4);
  if (g->arc_guard) {
    emit_arc_freed_use_check(g, g->rt_retain, rc_now, obj,
                             ZAN_ARC_FAULT_USE_OBJ,
                             "retain through a stale reference (object "
                             "was freed: missing retain when stored)");
  }
  /* immortal 对象（字符串字面量等静态对象）的引用计数是哨兵值，不参与
   * 计数，更不能写——静态对象在只读段，写它就是访问冲突。字面量存进
   * object 槽时走的就是这条通用 retain。与 zan_rt_str_retain /
   * zan_rt_release_dyn 的哨兵判定同口径。 */
  LLVMValueRef is_immortal =
      zan_icmp(g->builder, LLVMIntEQ, rc_now,
               LLVMConstInt(rc_ty, ZAN_RC_IMMORTAL, 0), "isimmortal");
  LLVMBasicBlockRef imm_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_retain, "immortal");
  LLVMBasicBlockRef add_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_retain, "add");
  LLVMBuildCondBr(g->builder, is_immortal, imm_bb, add_bb);
  LLVMPositionBuilderAtEnd(g->builder, imm_bb);
  LLVMBuildBr(g->builder, ret_bb);
  LLVMPositionBuilderAtEnd(g->builder, add_bb);
  LLVMValueRef rc_pre = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpAdd,
                                           rc_iptr, LLVMConstInt(rc_ty, 1, 0),
                                           LLVMAtomicOrderingMonotonic, 0);
  emit_arc_underflow_check(g, g->rt_retain, rc_pre, obj, NULL,
                           ZAN_ARC_FAULT_RETAIN,
                           "retain of an already-freed object");
  if (arc_trace_ev) {
    LLVMValueRef trc_new = LLVMBuildZExt(
        g->builder,
        zan_add(g->builder, rc_pre, LLVMConstInt(rc_ty, 1, 0), "trcnew32"), i64,
        "trcnew");
    LLVMValueRef tsite = LLVMBuildLoad2(
        g->builder, i64, zan_hdr_word_ptr(g, g->builder, obj, "trsp"),
        "trsite");
    emit_arc_trace_call(g, arc_trace_ev, "R", obj, trc_new,
                        zan_hdr_site(g, g->builder, tsite, "trsite.n"));
  }
  LLVMBuildBr(g->builder, ret_bb);
  LLVMPositionBuilderAtEnd(g->builder, ret_bb);
  LLVMBuildRetVoid(g->builder);
}

emit_weak_runtime(g);

LLVMTypeRef release_args[] = {i8ptr};
LLVMTypeRef release_type =
    LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), release_args, 1, 0);
g->rt_release = LLVMAddFunction(g->mod, "zan_rt_release", release_type);
zan_mark_nounwind(g, g->rt_release);

{
  LLVMBasicBlockRef bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "entry");
  LLVMPositionBuilderAtEnd(g->builder, bb);
  LLVMValueRef obj = LLVMGetParam(g->rt_release, 0);
  LLVMValueRef is_null =
      zan_icmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8ptr), "isnull");
  LLVMBasicBlockRef do_release =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "release");
  LLVMBasicBlockRef ret_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "ret");
  LLVMBuildCondBr(g->builder, is_null, ret_bb, do_release);
  LLVMPositionBuilderAtEnd(g->builder, do_release);
  LLVMTypeRef rc_ty = LLVMInt32TypeInContext(g->ctx);
  LLVMValueRef rc_iptr = zan_rc_ptr(g, g->builder, obj, "rcptr");
  /* 原子读一次引用计数（monotonic, align 4）：既是 immortal 判定，也是 freed 守卫的输入。 */
  LLVMValueRef rc_now = LLVMBuildLoad2(g->builder, rc_ty, rc_iptr, "rcnow");
  LLVMSetOrdering(rc_now, LLVMAtomicOrderingMonotonic);
  LLVMSetAlignment(rc_now, 4);
  if (g->arc_guard) {
    emit_arc_freed_use_check(g, g->rt_release, rc_now, obj,
                             ZAN_ARC_FAULT_USE_OBJ,
                             "release through a stale reference "
                             "(object was already freed)");
  }
  /* immortal 对象（字符串字面量等静态对象）不参与引用计数：既不减计数，
   * 也不释放——静态对象在只读段，减计数同样是写只读段。 */
  LLVMValueRef is_immortal =
      zan_icmp(g->builder, LLVMIntEQ, rc_now,
               LLVMConstInt(rc_ty, ZAN_RC_IMMORTAL, 0), "isimmortal");
  LLVMBasicBlockRef imm_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "immortal");
  LLVMBasicBlockRef sub_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "sub");
  LLVMBasicBlockRef free_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "dofree");
  zan_set_branch_weights(g,
                         LLVMBuildCondBr(g->builder, is_immortal, imm_bb, sub_bb),
                         1, 2000);
  LLVMPositionBuilderAtEnd(g->builder, imm_bb);
  LLVMBuildBr(g->builder, ret_bb);
  LLVMPositionBuilderAtEnd(g->builder, sub_bb);
  if (!g->arc_guard && !arc_trace_ev) {
    /* 独占快路径：读到 rc == 1 且全局无活跃弱引用时，本线程是唯一持有者，
     * 直接写 0 后跳转 free_bb（acquire fence + free），免一次 LOCK XADD；
     * 写 0 保证后续误二次释放仍触发 over-release 网（arc_net）。 */
    LLVMValueRef zan_emit_weak_any(zan_irgen_t *);
    LLVMBasicBlockRef uniq_chk =
        LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "uniq.chk");
    LLVMBasicBlockRef uniq_free =
        LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "uniq.free");
    LLVMBasicBlockRef atomic_bb =
        LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "sub.atomic");
    LLVMValueRef is_uniq = zan_icmp(g->builder, LLVMIntEQ, rc_now,
                                    LLVMConstInt(rc_ty, 1, 0), "isuniq");
    zan_set_branch_weights(
        g, LLVMBuildCondBr(g->builder, is_uniq, uniq_chk, atomic_bb), 2000, 1);
    LLVMPositionBuilderAtEnd(g->builder, uniq_chk);
    zan_set_branch_weights(
        g, LLVMBuildCondBr(g->builder, zan_emit_weak_any(g), atomic_bb, uniq_free),
        1, 2000);
    LLVMPositionBuilderAtEnd(g->builder, uniq_free);
    LLVMValueRef st0 =
        LLVMBuildStore(g->builder, LLVMConstInt(rc_ty, 0, 0), rc_iptr);
    LLVMSetOrdering(st0, LLVMAtomicOrderingMonotonic);
    LLVMSetAlignment(st0, 4);
    LLVMBuildBr(g->builder, free_bb);
    LLVMPositionBuilderAtEnd(g->builder, atomic_bb);
  }
  LLVMValueRef rc_old = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpSub,
                                           rc_iptr, LLVMConstInt(rc_ty, 1, 0),
                                           LLVMAtomicOrderingRelease, 0);
  {
    LLVMValueRef s = NULL;
    if (g->arc_guard && g->runtime_checks) {
      s = LLVMBuildLoad2(g->builder, i64,
                         zan_hdr_word_ptr(g, g->builder, obj, "arcsp"),
                         "arcsite");
    }
    emit_arc_underflow_check(g, g->rt_release, rc_old, obj, s,
                             ZAN_ARC_FAULT_RELEASE,
                             "release of an already-freed object");
  }
  LLVMValueRef rc1 =
      zan_sub(g->builder, rc_old, LLVMConstInt(rc_ty, 1, 0), "rc1");
  if (arc_trace_ev) {
    LLVMValueRef rsite = LLVMBuildLoad2(
        g->builder, i64, zan_hdr_word_ptr(g, g->builder, obj, "rrsp"),
        "rrsite");
    emit_arc_trace_call(g, arc_trace_ev, "r", obj,
                        LLVMBuildSExt(g->builder, rc1, i64, "rc1w"),
                        zan_hdr_site(g, g->builder, rsite, "rrsite.n"));
  }
  LLVMValueRef is_zero =
      zan_icmp(g->builder, LLVMIntEQ, rc1, LLVMConstInt(rc_ty, 0, 0), "iszero");
  LLVMBuildCondBr(g->builder, is_zero, free_bb, ret_bb);
  LLVMPositionBuilderAtEnd(g->builder, free_bb);
  LLVMBuildFence(g->builder, LLVMAtomicOrderingAcquire, 0, "");
  {
    LLVMValueRef zan_emit_weak_any(zan_irgen_t *);
    void zan_emit_call_if(zan_irgen_t *, LLVMValueRef, LLVMValueRef,
                          LLVMValueRef *, unsigned);
    zan_emit_call_if(g, zan_emit_weak_any(g), g->rt_weak_nil_all, &obj, 1);
  }
  LLVMValueRef freefn = get_arc_free_decl(g);
  zan_call2(g->builder, LLVMGlobalGetValueType(freefn), freefn, &obj, 1, "");
  LLVMBuildBr(g->builder, ret_bb);
  LLVMPositionBuilderAtEnd(g->builder, ret_bb);
  LLVMBuildRetVoid(g->builder);
}

{
  if (!g->rt_arr_release) {
    LLVMTypeRef arr_fnty =
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), release_args, 1, 0);
    g->rt_arr_release = LLVMAddFunction(g->mod, "zan_rt_arr_release", arr_fnty);
  }
  if (!g->rt_str_release) {
    LLVMTypeRef str_fnty =
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), release_args, 1, 0);
    g->rt_str_release = LLVMAddFunction(g->mod, "zan_rt_str_release", str_fnty);
  }
  LLVMTypeRef reld_type =
      LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), release_args, 1, 0);
  g->rt_release_dyn = LLVMAddFunction(g->mod, "zan_rt_release_dyn", reld_type);
  zan_mark_nounwind(g, g->rt_release_dyn);
  LLVMTypeRef rc32 = LLVMInt32TypeInContext(g->ctx);
  /* 无锁快慢分裂（desc_hdr 且非调试守卫模式）：rc > 1 的对象在
   * zan_rt_release_dyn 内联快路径用强 cmpxchg 原子减 1 即返回，rc == 1 的对象
   * 直接查 __zan_desc_tbl 尾调 dtor，免去跨函数调用与 TLS 延迟队列开销；
   * 仅竞态失败 / 字符串 / 数组 / 空 desc / over-release 落入冷 noinline
   * zan_rt_release_dyn_slow。 */
  int use_dyn_split = (g->desc_hdr && !g->arc_guard && !arc_trace_ev);
  LLVMValueRef rel_body_fn = g->rt_release_dyn;
  if (use_dyn_split) {
    rel_body_fn =
        LLVMAddFunction(g->mod, "zan_rt_release_dyn_slow", reld_type);
    LLVMSetLinkage(rel_body_fn, LLVMInternalLinkage);
    unsigned ck = LLVMGetEnumAttributeKindForName("cold", 4);
    unsigned nk = LLVMGetEnumAttributeKindForName("noinline", 8);
    unsigned ih = LLVMGetEnumAttributeKindForName("inlinehint", 10);
    if (ck)
      LLVMAddAttributeAtIndex(rel_body_fn, LLVMAttributeFunctionIndex,
                              LLVMCreateEnumAttribute(g->ctx, ck, 0));
    if (nk)
      LLVMAddAttributeAtIndex(rel_body_fn, LLVMAttributeFunctionIndex,
                              LLVMCreateEnumAttribute(g->ctx, nk, 0));
    zan_mark_nounwind(g, rel_body_fn);
    if (ih)
      LLVMAddAttributeAtIndex(g->rt_release_dyn, LLVMAttributeFunctionIndex,
                              LLVMCreateEnumAttribute(g->ctx, ih, 0));

    LLVMBasicBlockRef f_ent =
        LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "entry");
    LLVMBasicBlockRef f_cnt =
        LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "cont");
    LLVMBasicBlockRef f_chk =
        LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "chk");
    LLVMBasicBlockRef f_dec =
        LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "fast.dec");
    LLVMBasicBlockRef f_uchk =
        LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "fast.uchk");
    LLVMBasicBlockRef f_uniq =
        LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "fast.uniq");
    LLVMBasicBlockRef f_uload =
        LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "fast.uload");
    LLVMBasicBlockRef f_ucall =
        LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "fast.ucall");
    LLVMBasicBlockRef f_slw =
        LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "slow");
    LLVMBasicBlockRef f_ret =
        LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "ret");

    LLVMPositionBuilderAtEnd(g->builder, f_ent);
    LLVMValueRef f_obj = LLVMGetParam(g->rt_release_dyn, 0);
    zan_set_branch_weights(
        g,
        LLVMBuildCondBr(
            g->builder,
            zan_icmp(g->builder, LLVMIntEQ, f_obj, LLVMConstNull(i8ptr),
                     "isnull"),
            f_ret, f_cnt),
        1, 2000);

    LLVMPositionBuilderAtEnd(g->builder, f_cnt);
    LLVMValueRef f_hdr = LLVMBuildLoad2(
        g->builder, i64, zan_hdr_word_ptr(g, g->builder, f_obj, "hp"), "hdr");
    LLVMSetOrdering(f_hdr, LLVMAtomicOrderingMonotonic);
    LLVMSetAlignment(f_hdr, 8);
    LLVMValueRef f_rc = LLVMBuildTrunc(g->builder, f_hdr, rc32, "rc");
    LLVMValueRef f_rcp = zan_rc_ptr(g, g->builder, f_obj, "rcip");
    zan_set_branch_weights(
        g,
        LLVMBuildCondBr(
            g->builder,
            zan_icmp(g->builder, LLVMIntEQ, f_rc,
                     LLVMConstInt(rc32, ZAN_RC_IMMORTAL, 0), "issent"),
            f_ret, f_chk),
        1, 2000);

    LLVMPositionBuilderAtEnd(g->builder, f_chk);
    LLVMValueRef f_desc =
        zan_and(g->builder, f_hdr, LLVMConstInt(i64, ~ZAN_OBJ_STACK_FLAG, 0),
                "desc");
    LLVMValueRef f_is_obj_mark = zan_icmp(
        g->builder, LLVMIntNE,
        zan_and(g->builder, f_desc,
                LLVMConstInt(i64, (uint64_t)ZAN_OBJ_TW_MARK << 32, 0),
                "obj.mark"),
        LLVMConstInt(i64, 0, 0), "is.objmark");
    LLVMValueRef f_not_str = LLVMBuildNot(
        g->builder, zan_hdr_is_string(g, f_hdr, "is.str"), "not.str");
    LLVMValueRef f_dcount = NULL;
    LLVMValueRef f_dtbl = zan_desc_tbl_global(g, &f_dcount);
    LLVMValueRef f_osite = zan_hdr_site(g, g->builder, f_desc, "osite");
    LLVMValueRef f_inrange =
        zan_icmp(g->builder, LLVMIntULT, f_osite,
                 LLVMBuildLoad2(g->builder, i64, f_dcount, "desc.n"),
                 "inrange");
    LLVMValueRef f_is_obj = zan_and(
        g->builder, zan_and(g->builder, f_is_obj_mark, f_not_str, "obj.mns"),
        f_inrange, "is.obj");
    LLVMValueRef f_shared =
        zan_icmp(g->builder, LLVMIntSGT, f_rc, LLVMConstInt(rc32, 1, 0),
                 "shared");
    LLVMValueRef br_fast = LLVMBuildCondBr(
        g->builder, zan_and(g->builder, f_is_obj, f_shared, "can.fast"), f_dec,
        f_uchk);
    zan_set_branch_weights(g, br_fast, 1000, 1000);

    LLVMPositionBuilderAtEnd(g->builder, f_dec);
    LLVMValueRef f_rc1 =
        zan_sub(g->builder, f_rc, LLVMConstInt(rc32, 1, 0), "rc1");
    LLVMValueRef f_cx = LLVMBuildAtomicCmpXchg(
        g->builder, f_rcp, f_rc, f_rc1, LLVMAtomicOrderingRelease,
        LLVMAtomicOrderingMonotonic, 0);
    LLVMSetAlignment(f_cx, 4);
    LLVMValueRef f_ok = LLVMBuildExtractValue(g->builder, f_cx, 1, "cx.ok");
    LLVMValueRef br_ok = LLVMBuildCondBr(g->builder, f_ok, f_ret, f_slw);
    zan_set_branch_weights(g, br_ok, 2000, 1);

    /* rc == 1 独占析构快路径：直接查 __zan_desc_tbl[osite] 尾调 dtor，避免跳入
     * 冷慢路径重复判空、重复读 rc/hdr。字符串/数组/空 desc/over-release 仍回退
     * f_slw。 */
    LLVMPositionBuilderAtEnd(g->builder, f_uchk);
    LLVMValueRef f_is1 =
        zan_icmp(g->builder, LLVMIntEQ, f_rc, LLVMConstInt(rc32, 1, 0), "is1");
    LLVMValueRef f_uok = zan_and(g->builder, f_is1, f_is_obj, "can.uniq");
    zan_set_branch_weights(
        g, LLVMBuildCondBr(g->builder, f_uok, f_uniq, f_slw), 2000, 1);

    LLVMPositionBuilderAtEnd(g->builder, f_uniq);
    LLVMValueRef f_descp = LLVMBuildLoad2(
        g->builder, i8ptr,
        LLVMBuildGEP2(g->builder, i8ptr, f_dtbl, &f_osite, 1, "f.dslot"),
        "f.descp");
    LLVMValueRef f_dnz =
        zan_icmp(g->builder, LLVMIntNE, f_descp, LLVMConstNull(i8ptr), "f.dnz");
    zan_set_branch_weights(
        g, LLVMBuildCondBr(g->builder, f_dnz, f_uload, f_slw), 2000, 1);

    LLVMPositionBuilderAtEnd(g->builder, f_uload);
    LLVMValueRef f_dtor = LLVMBuildLoad2(g->builder, i8ptr, f_descp, "f.dtor");
    LLVMValueRef f_hasd =
        zan_icmp(g->builder, LLVMIntNE, f_dtor, LLVMConstNull(i8ptr), "f.hasd");
    zan_set_branch_weights(
        g, LLVMBuildCondBr(g->builder, f_hasd, f_ucall, f_slw), 2000, 1);

    LLVMPositionBuilderAtEnd(g->builder, f_ucall);
    LLVMValueRef f_dfn = LLVMBuildBitCast(
        g->builder, f_dtor, LLVMPointerType(reld_type, 0), "f.dfn");
    LLVMValueRef uc = zan_call2(g->builder, reld_type, f_dfn, &f_obj, 1, "");
    zan_mark_nounwind(g, uc);
    LLVMSetTailCall(uc, 1);
    LLVMBuildBr(g->builder, f_ret);

    LLVMPositionBuilderAtEnd(g->builder, f_slw);
    LLVMValueRef sc =
        zan_call2(g->builder, reld_type, rel_body_fn, &f_obj, 1, "");
    LLVMSetTailCall(sc, 1);
    LLVMBuildBr(g->builder, f_ret);

    LLVMPositionBuilderAtEnd(g->builder, f_ret);
    LLVMBuildRetVoid(g->builder);
  }
  LLVMBasicBlockRef bb =
      LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "entry");
  LLVMPositionBuilderAtEnd(g->builder, bb);
  LLVMValueRef obj = LLVMGetParam(rel_body_fn, 0);
  LLVMValueRef is_null =
      zan_icmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8ptr), "isnull");
  LLVMBasicBlockRef cont =
      LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "cont");
  LLVMBasicBlockRef lookup =
      LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "lookup");
  LLVMBasicBlockRef calld =
      LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "calld");
  LLVMBasicBlockRef fb =
      LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "fallback");
  LLVMBasicBlockRef ret_bb =
      LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "ret");
  LLVMBuildCondBr(g->builder, is_null, ret_bb, cont);
  LLVMPositionBuilderAtEnd(g->builder, cont);
  LLVMValueRef sent_rc = LLVMBuildLoad2(
      g->builder, rc32, zan_rc_ptr(g, g->builder, obj, "sentp"), "sentrc");
  LLVMSetOrdering(sent_rc, LLVMAtomicOrderingMonotonic);
  LLVMSetAlignment(sent_rc, 4);
  LLVMBasicBlockRef literal =
      LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "islit");
  LLVMBuildCondBr(
      g->builder,
      zan_icmp(g->builder, LLVMIntEQ, sent_rc,
               LLVMConstInt(rc32, ZAN_RC_IMMORTAL, 0), "issent"),
      ret_bb, literal);
  LLVMPositionBuilderAtEnd(g->builder, literal);
  LLVMValueRef raw_hdr = LLVMBuildLoad2(
      g->builder, i64, zan_hdr_word_ptr(g, g->builder, obj, "sptr"), "hdr.raw");
  LLVMValueRef is_str_hdr = zan_hdr_is_string(g, raw_hdr, "is.str");
  LLVMBasicBlockRef rel_str_bb =
      LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "relstr");
  LLVMBasicBlockRef non_str_bb =
      LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "nonstr");
  LLVMBuildCondBr(g->builder, is_str_hdr, rel_str_bb, non_str_bb);
  LLVMPositionBuilderAtEnd(g->builder, rel_str_bb);
  zan_call2(g->builder,
            LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
            g->rt_str_release, &obj, 1, "");
  LLVMBuildBr(g->builder, ret_bb);
  LLVMPositionBuilderAtEnd(g->builder, non_str_bb);
  if (arc_trace_ev) {
    LLVMValueRef drc = LLVMBuildZExt(
        g->builder,
        LLVMBuildLoad2(g->builder, rc32, zan_rc_ptr(g, g->builder, obj, "drcp"),
                       "drc32"),
        i64, "drc");
    emit_arc_trace_call(g, arc_trace_ev, "D", obj, drc,
                        zan_hdr_site(g, g->builder, raw_hdr, "dsite.n"));
  }
  LLVMValueRef dpp;
  /* 快路径的"计数 > 1"只对对象头成立：数组头字里的描述符指针低半不是 rc */
  LLVMValueRef fast_ok = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0);
  if (!g->desc_hdr) {
    LLVMValueRef site = zan_hdr_site(g, g->builder, raw_hdr, "site");
    LLVMValueRef is_obj = zan_icmp(
        g->builder, LLVMIntNE,
        zan_and(g->builder, raw_hdr,
                LLVMConstInt(i64, (uint64_t)ZAN_OBJ_TW_MARK << 32, 0),
                "site.mark"),
        LLVMConstInt(i64, 0, 0), "site.isobj");
    LLVMValueRef bound =
        LLVMBuildLoad2(g->builder, i64, g->g_site_count, "bound");
    LLVMValueRef inrange = zan_and(
        g->builder, is_obj,
        zan_icmp(g->builder, LLVMIntULT, site, bound, "site.lt"), "inrange");
    LLVMBuildCondBr(g->builder, inrange, lookup, fb);
    LLVMPositionBuilderAtEnd(g->builder, lookup);
    LLVMValueRef dtbl = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0),
                                       g->g_site_dtors, "dtbl");
    LLVMValueRef dtor = LLVMBuildLoad2(
        g->builder, i8ptr,
        LLVMBuildGEP2(g->builder, i8ptr, dtbl, &site, 1, "dtorp"), "dtor");
    dpp = dtor;
    LLVMValueRef hasd =
        zan_icmp(g->builder, LLVMIntNE, dtor, LLVMConstNull(i8ptr), "hasd");
    LLVMBuildCondBr(g->builder, hasd, calld, fb);
  } else {
    LLVMValueRef desc =
        zan_and(g->builder, raw_hdr, LLVMConstInt(i64, ~ZAN_OBJ_STACK_FLAG, 0),
                "desc");
    /* 先判原始 1D 数组魔数（其高 32 位也带 ZAN_OBJ_TW_MARK），再判对象头查
     * __zan_desc_tbl（越界如 soft scratch 前缀走兜底），其余是秩/数组描述符指针。 */
    LLVMBasicBlockRef rel_arr_bb =
        LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "relarr");
    LLVMBasicBlockRef not_am_bb =
        LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "notam");
    LLVMBasicBlockRef obj_bb =
        LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "dobj");
    LLVMBasicBlockRef objtbl_bb =
        LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "dobj.tbl");
    LLVMBasicBlockRef arrw_bb =
        LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "darr");
    LLVMBasicBlockRef not_raw =
        LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "notraw");
    LLVMBasicBlockRef dsel_bb =
        LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "dsel");
    LLVMValueRef is_arr_magic =
        zan_icmp(g->builder, LLVMIntEQ, desc,
                 LLVMConstInt(i64, ZAN_ARRAY_MAGIC, 0), "is.arrmagic");
    LLVMBuildCondBr(g->builder, is_arr_magic, rel_arr_bb, not_am_bb);

    LLVMPositionBuilderAtEnd(g->builder, not_am_bb);
    LLVMValueRef is_obj = zan_icmp(
        g->builder, LLVMIntNE,
        zan_and(g->builder, desc,
                LLVMConstInt(i64, (uint64_t)ZAN_OBJ_TW_MARK << 32, 0),
                "desc.mark"),
        LLVMConstInt(i64, 0, 0), "desc.isobj");
    LLVMBuildCondBr(g->builder, is_obj, obj_bb, arrw_bb);

    LLVMPositionBuilderAtEnd(g->builder, obj_bb);
    LLVMValueRef dcount = NULL;
    LLVMValueRef dtbl = zan_desc_tbl_global(g, &dcount);
    LLVMValueRef osite = zan_hdr_site(g, g->builder, desc, "desc.site");
    LLVMBuildCondBr(
        g->builder,
        zan_icmp(g->builder, LLVMIntULT, osite,
                 LLVMBuildLoad2(g->builder, i64, dcount, "desc.n"),
                 "desc.inrange"),
        objtbl_bb, fb);
    LLVMPositionBuilderAtEnd(g->builder, objtbl_bb);
    LLVMValueRef odesc = LLVMBuildLoad2(
        g->builder, i8ptr,
        LLVMBuildGEP2(g->builder, i8ptr, dtbl, &osite, 1, "desc.slot"),
        "desc.tbl");
    LLVMBuildBr(g->builder, dsel_bb);

    LLVMPositionBuilderAtEnd(g->builder, arrw_bb);
    LLVMValueRef is_rank = zan_and(
        g->builder,
        zan_icmp(g->builder, LLVMIntUGT, desc, LLVMConstInt(i64, 0, 0),
                 "is.rnz"),
        zan_icmp(g->builder, LLVMIntULT, desc, LLVMConstInt(i64, 4096, 0),
                 "is.rank"),
        "is.isrank");
    LLVMBuildCondBr(g->builder, is_rank, rel_arr_bb, not_raw);

    LLVMPositionBuilderAtEnd(g->builder, rel_arr_bb);
    zan_call2(g->builder,
              LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
              g->rt_arr_release, &obj, 1, "");
    LLVMBuildBr(g->builder, ret_bb);

    LLVMPositionBuilderAtEnd(g->builder, not_raw);
    LLVMValueRef adesc =
        LLVMBuildIntToPtr(g->builder, desc, i8ptr, "desc.arr");
    LLVMBuildBr(g->builder, dsel_bb);

    LLVMPositionBuilderAtEnd(g->builder, dsel_bb);
    LLVMValueRef desc_p = LLVMBuildPhi(g->builder, i8ptr, "desc.p");
    LLVMAddIncoming(desc_p, (LLVMValueRef[]){odesc, adesc},
                    (LLVMBasicBlockRef[]){objtbl_bb, not_raw}, 2);
    /* 快路径的"计数 > 1"只对对象头成立 */
    LLVMValueRef fphi =
        LLVMBuildPhi(g->builder, LLVMInt1TypeInContext(g->ctx), "desc.isobjp");
    LLVMAddIncoming(
        fphi,
        (LLVMValueRef[]){LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0),
                         LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0)},
        (LLVMBasicBlockRef[]){objtbl_bb, not_raw}, 2);
    fast_ok = fphi;
    LLVMValueRef dnz =
        zan_icmp(g->builder, LLVMIntNE, desc_p, LLVMConstNull(i8ptr), "descnz");
    LLVMBuildCondBr(g->builder, dnz, lookup, fb);
    LLVMPositionBuilderAtEnd(g->builder, lookup);
    dpp = LLVMBuildLoad2(g->builder, i8ptr, desc_p, "dtor");
    LLVMValueRef hasd =
        zan_icmp(g->builder, LLVMIntNE, dpp, LLVMConstNull(i8ptr), "hasd");
    LLVMBuildCondBr(g->builder, hasd, calld, fb);
  }
  LLVMPositionBuilderAtEnd(g->builder, calld);
  LLVMTypeRef dfnty =
      LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
  LLVMValueRef dfn =
      LLVMBuildBitCast(g->builder, dpp, LLVMPointerType(dfnty, 0), "dfn");
  if (!g->target_is_wasm) {
    LLVMBasicBlockRef fast_bb =
        LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "rel.fast");
    LLVMBasicBlockRef slow_bb =
        LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "rel.slow");
    LLVMBuildCondBr(
        g->builder,
        zan_and(g->builder, fast_ok,
                zan_icmp(g->builder, LLVMIntSGT, sent_rc,
                         LLVMConstInt(rc32, 1, 0), "rel.shared"),
                "rel.fastok"),
        fast_bb, slow_bb);
    LLVMPositionBuilderAtEnd(g->builder, fast_bb);
    zan_mark_nounwind(g, zan_call2(g->builder, dfnty, dfn, &obj, 1, ""));
    LLVMBuildBr(g->builder, ret_bb);
    LLVMPositionBuilderAtEnd(g->builder, slow_bb);
  }
  /* B-ID78 深树级联析构防护：类释放深度超过阈值时不再同步下钻，
   * 把 (释放函数, 死对象) 交给延迟队列，最外层出口迭代排空，
   * 数千层链表/树的释放不再耗尽线程栈 */
  {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    if (!g->arc_defer_depth) {
      g->arc_defer_depth = LLVMAddGlobal(g->mod, i32t, "__zan_arc_rel_depth");
      LLVMSetInitializer(g->arc_defer_depth, LLVMConstNull(i32t));
      LLVMSetLinkage(g->arc_defer_depth, LLVMInternalLinkage);
      /* This is recursion depth, not a shared queue count. A second
       * thread must never decrement or inherit another thread's frame. */
      if (!g->target_is_wasm)
        LLVMSetThreadLocal(g->arc_defer_depth, 1);
      LLVMTypeRef push_ty = LLVMFunctionType(
          LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0);
      g->rt_defer_push =
          LLVMAddFunction(g->mod, "__zan_arc_defer_push", push_ty);
      LLVMTypeRef pend_ty = LLVMFunctionType(i32t, NULL, 0, 0);
      g->rt_defer_pending =
          LLVMAddFunction(g->mod, "__zan_arc_defer_pending", pend_ty);
      LLVMTypeRef drain_ty =
          LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
      g->rt_defer_drain =
          LLVMAddFunction(g->mod, "__zan_arc_defer_drain", drain_ty);
    }
    enum { ZAN_ARC_DEFER_DEPTH = 100 };
    LLVMValueRef depth_p = g->arc_defer_depth;
    if (LLVMIsThreadLocal(g->arc_defer_depth)) {
      unsigned tla = LLVMLookupIntrinsicID("llvm.threadlocal.address",
                                           strlen("llvm.threadlocal.address"));
      LLVMTypeRef pty = LLVMTypeOf(g->arc_defer_depth);
      LLVMValueRef tlf = LLVMGetIntrinsicDeclaration(g->mod, tla, &pty, 1);
      depth_p = zan_call2(g->builder, LLVMGlobalGetValueType(tlf), tlf,
                          &g->arc_defer_depth, 1, "adepth.p");
    }
    LLVMValueRef dcur = LLVMBuildLoad2(g->builder, i32t, depth_p, "adepth");
    LLVMBasicBlockRef defer_bb =
        LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "adefer");
    LLVMBasicBlockRef sync_bb =
        LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "async2");
    LLVMBasicBlockRef drain_bb =
        LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "adrain");
    LLVMBasicBlockRef tail_bb =
        LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "atail");
    LLVMValueRef is_heap = zan_icmp(
        g->builder, LLVMIntEQ,
        zan_and(g->builder, raw_hdr, LLVMConstInt(i64, ZAN_OBJ_STACK_FLAG, 0),
                "adefer.stack"),
        LLVMConstInt(i64, 0, 0), "adefer.heap");
    LLVMValueRef is_deep =
        zan_icmp(g->builder, LLVMIntUGE, dcur,
                 LLVMConstInt(i32t, ZAN_ARC_DEFER_DEPTH, 0), "adeep");
    LLVMBuildCondBr(g->builder,
                    zan_and(g->builder, is_deep, is_heap, "adefer.allowed"),
                    defer_bb, sync_bb);
    LLVMPositionBuilderAtEnd(g->builder, defer_bb);
    zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_defer_push),
              g->rt_defer_push, (LLVMValueRef[]){dpp, obj}, 2, "");
    LLVMBuildBr(g->builder, ret_bb);
    LLVMPositionBuilderAtEnd(g->builder, sync_bb);
    LLVMBuildStore(g->builder,
                   zan_add(g->builder, dcur, LLVMConstInt(i32t, 1, 0), "aup"),
                   depth_p);
    zan_mark_nounwind(g, zan_call2(g->builder, dfnty, dfn, &obj, 1, ""));
    LLVMValueRef dnow = LLVMBuildLoad2(g->builder, i32t, depth_p, "adnow");
    LLVMValueRef ddown =
        zan_add(g->builder, dnow, LLVMConstInt(i32t, (unsigned)-1, 0), "adown");
    LLVMBuildStore(g->builder, ddown, depth_p);
    LLVMValueRef dzero = zan_icmp(g->builder, LLVMIntEQ, ddown,
                                  LLVMConstInt(i32t, 0, 0), "azero");
    LLVMBasicBlockRef pchk_bb =
        LLVMAppendBasicBlockInContext(g->ctx, rel_body_fn, "apchk");
    LLVMBuildCondBr(g->builder, dzero, pchk_bb, tail_bb);
    LLVMPositionBuilderAtEnd(g->builder, pchk_bb);
    LLVMValueRef pend =
        zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_defer_pending),
                  g->rt_defer_pending, NULL, 0, "apend");
    LLVMValueRef pend1 = zan_icmp(g->builder, LLVMIntNE, pend,
                                  LLVMConstInt(i32t, 0, 0), "apend1");
    LLVMBuildCondBr(g->builder, pend1, drain_bb, tail_bb);
    LLVMPositionBuilderAtEnd(g->builder, drain_bb);
    zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_defer_drain),
              g->rt_defer_drain, NULL, 0, "");
    LLVMBuildBr(g->builder, tail_bb);
    LLVMPositionBuilderAtEnd(g->builder, tail_bb);
  }
  /* B-ID78 回归补丁：defer 三符号在此补弱回退体（必须等声明建成，
   * 见 irgen_weak.c emit_defer_fallback 注释） */
  emit_defer_fallback(g);
  LLVMBuildBr(g->builder, ret_bb);
  LLVMPositionBuilderAtEnd(g->builder, fb);
  zan_call2(g->builder,
            LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
            g->rt_release, &obj, 1, "");
  LLVMBuildBr(g->builder, ret_bb);
  LLVMPositionBuilderAtEnd(g->builder, ret_bb);
  LLVMBuildRetVoid(g->builder);
}

LLVMTypeRef alloc_args[] = {i64, i64, i8ptr};
LLVMTypeRef alloc_type = LLVMFunctionType(i8ptr, alloc_args, 3, 0);
g->rt_alloc = LLVMAddFunction(g->mod, "zan_rt_alloc", alloc_type);
{
  unsigned nonnull_kind = LLVMGetEnumAttributeKindForName("nonnull", 7);
  if (nonnull_kind) {
    LLVMAddAttributeAtIndex(g->rt_alloc, LLVMAttributeReturnIndex,
                            LLVMCreateEnumAttribute(g->ctx, nonnull_kind, 0));
  }
  unsigned noalias_kind = LLVMGetEnumAttributeKindForName("noalias", 7);
  if (noalias_kind) {
    LLVMAddAttributeAtIndex(g->rt_alloc, LLVMAttributeReturnIndex,
                            LLVMCreateEnumAttribute(g->ctx, noalias_kind, 0));
  }
}

{
  LLVMBasicBlockRef bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_alloc, "entry");
  LLVMPositionBuilderAtEnd(g->builder, bb);
  LLVMValueRef size = LLVMGetParam(g->rt_alloc, 0);
  LLVMValueRef site = LLVMGetParam(g->rt_alloc, 1);
  LLVMValueRef name = LLVMGetParam(g->rt_alloc, 2);
  /* 对齐 16 的类（站点实参带 ZAN_OBJ_TW_PAD16）：块前垫 8 字节，p = 块 + 16 */
  LLVMValueRef pad = LLVMBuildSelect(
      g->builder,
      zan_icmp(g->builder, LLVMIntNE,
               zan_and(g->builder, site,
                       LLVMConstInt(i64, ZAN_OBJ_TW_PAD16, 0), "pad16"),
               LLVMConstInt(i64, 0, 0), "haspad"),
      LLVMConstInt(i64, 8, 0), LLVMConstInt(i64, 0, 0), "pad");
  LLVMValueRef site_idx = zan_and(
      g->builder, site, LLVMConstInt(i64, ZAN_OBJ_TW_SITE_MASK, 0), "siteidx");
  LLVMValueRef total = zan_add(
      g->builder, size,
      zan_add(g->builder, pad, LLVMConstInt(i64, ZAN_OBJ_HDR_SIZE, 0), "hdrsz"),
      "total");
  LLVMTypeRef malloc_fn_type =
      LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i64}, 1, 0);
  /* 链了 rt_mem（同 str_pages 的口径）：对象走 zan_mem_obj_malloc，
   * total 为 16 的奇数倍 8 时进 8 字节粒度的对象页（两指针无多态类
   * 8 头 + 16 = 24 字节一块）。对齐 16 的字段让 LLVM 尺寸成为 16 的倍数，自动
   * 落回通用类。 */
  LLVMValueRef obj_malloc = g->fn_malloc;
  if (g->str_pages) {
    obj_malloc = LLVMGetNamedFunction(g->mod, "zan_mem_obj_malloc");
    if (!obj_malloc) {
      obj_malloc =
          LLVMAddFunction(g->mod, "zan_mem_obj_malloc", malloc_fn_type);
      LLVMAddAttributeAtIndex(
          obj_malloc, LLVMAttributeReturnIndex,
          LLVMCreateEnumAttribute(
              g->ctx, LLVMGetEnumAttributeKindForName("noalias", 7), 0));
      zan_mark_nounwind(g, obj_malloc);
    }
  }
  LLVMValueRef raw =
      zan_call2(g->builder, malloc_fn_type, obj_malloc, &total, 1, "raw");
  zan_irgen_emit_oom_check(g, g->rt_alloc, raw);
  emit_rt_alloc_add(g, total);
  /* 头字一次 64 位写：raw[pad..pad+3]（= obj-8）rc = 1，raw[pad+4..pad+7] tw =
   * 对象标记 | 站点号 */
  LLVMValueRef hdr_word = LLVMBuildOr(
      g->builder,
      LLVMBuildShl(
          g->builder,
          LLVMBuildOr(g->builder, site, LLVMConstInt(i64, ZAN_OBJ_TW_MARK, 0),
                      "tw"),
          LLVMConstInt(i64, 32, 0), "tw.hi"),
      LLVMConstInt(i64, 1, 0), "hdrword");
  LLVMValueRef hdrp = LLVMBuildBitCast(
      g->builder,
      LLVMBuildInBoundsGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), raw,
                            &pad, 1, "hdrp"),
      LLVMPointerType(i64, 0), "hdrip");
  LLVMSetAlignment(LLVMBuildStore(g->builder, hdr_word, hdrp), 8);
  LLVMValueRef hdr_off =
      zan_add(g->builder, pad, LLVMConstInt(i64, ZAN_OBJ_HDR_SIZE, 0), "usroff");
  LLVMValueRef user_ptr = LLVMBuildInBoundsGEP2(
      g->builder, LLVMInt8TypeInContext(g->ctx), raw, &hdr_off, 1, "usr");
  if (g->rt_live_register_named) {
    LLVMValueRef zan_emit_live_on(zan_irgen_t *);
    void zan_emit_call_if(zan_irgen_t *, LLVMValueRef, LLVMValueRef,
                          LLVMValueRef *, unsigned);
    zan_emit_call_if(g, zan_emit_live_on(g), g->rt_live_register_named,
                     (LLVMValueRef[]){user_ptr, total, name}, 3);
  }
  if (g->check_leaks) {
    emit_leak_counter_add(g, g->g_live, 1);
    LLVMValueRef ltbl = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0),
                                       g->g_site_live, "ltbl");
    LLVMValueRef sc_ptr =
        LLVMBuildGEP2(g->builder, i64, ltbl, &site_idx, 1, "scptr");
    emit_leak_counter_add(g, sc_ptr, 1);
    LLVMValueRef ntbl = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0),
                                       g->g_site_names, "ntbl");
    LLVMValueRef nm_ptr =
        LLVMBuildGEP2(g->builder, i8ptr, ntbl, &site_idx, 1, "nmptr");
    LLVMBuildStore(g->builder, name, nm_ptr);
    emit_arc_trace_call(g, arc_trace_ev, "A", user_ptr, LLVMConstInt(i64, 1, 0),
                        site_idx);
  }
  LLVMBuildRet(g->builder, user_ptr);
}

{
  LLVMTypeRef str_alloc_args[] = {i64};
  LLVMTypeRef str_alloc_type = LLVMFunctionType(i8ptr, str_alloc_args, 1, 0);
  g->rt_str_alloc = LLVMAddFunction(g->mod, "zan_rt_str_alloc", str_alloc_type);
  {
    unsigned nonnull_kind = LLVMGetEnumAttributeKindForName("nonnull", 7);
    if (nonnull_kind) {
      LLVMAddAttributeAtIndex(g->rt_str_alloc, LLVMAttributeReturnIndex,
                              LLVMCreateEnumAttribute(g->ctx, nonnull_kind, 0));
    }
    unsigned noalias_kind = LLVMGetEnumAttributeKindForName("noalias", 7);
    if (noalias_kind) {
      LLVMAddAttributeAtIndex(g->rt_str_alloc, LLVMAttributeReturnIndex,
                              LLVMCreateEnumAttribute(g->ctx, noalias_kind, 0));
    }
  }
  LLVMBasicBlockRef bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_alloc, "entry");
  LLVMPositionBuilderAtEnd(g->builder, bb);
  /* 串头 12 字节：[-12] u32 len、[-8] 头字 (ZANS<<32)|rc，负载紧随其后。
   * 返回的 raw 是"虚基址"（用户指针 - 16，free 传它）：
   *  - 链了 rt_mem（str_pages）：zan_mem_str_malloc(n) 从 8 字节粒度的
   *    串页里切 n 字节、块起点在 raw+4（4 mod 8），于是 [-8] 头字
   *    8 字节对齐而串只占 n 字节——"item1234567" 24 字节（曾 32）；
   *  - 否则 malloc(n + 4)，raw[0..3] 是串自己的空位。
   * 两种情况都绝不写 raw[0..3]（串页里那是相邻块的尾巴）。 */
  LLVMValueRef size = LLVMGetParam(g->rt_str_alloc, 0);
  LLVMValueRef need = zan_add(
      g->builder, size, LLVMConstInt(i64, ZAN_STR_HDR_SIZE, 0), "need");
  LLVMTypeRef malloc_fn_type =
      LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i64}, 1, 0);
  LLVMValueRef total, raw;
  if (g->str_pages) {
    LLVMValueRef sm = LLVMGetNamedFunction(g->mod, "zan_mem_str_malloc");
    if (!sm) {
      sm = LLVMAddFunction(g->mod, "zan_mem_str_malloc", malloc_fn_type);
      /* 与 malloc 同等的别名事实：返回的新块不与任何既有内存重叠
       * （否则串构建循环里每次写字符后都得重读列表/长度字段） */
      static const char *const attrs[] = {"noalias", "nounwind"};
      for (int ai = 0; ai < 2; ai++) {
        unsigned kind =
            LLVMGetEnumAttributeKindForName(attrs[ai], strlen(attrs[ai]));
        LLVMAddAttributeAtIndex(
            sm, ai == 0 ? LLVMAttributeReturnIndex : LLVMAttributeFunctionIndex,
            LLVMCreateEnumAttribute(g->ctx, kind, 0));
      }
    }
    total = need;
    raw = zan_call2(g->builder, malloc_fn_type, sm, &need, 1, "raw");
  } else {
    total = zan_add(g->builder, need,
                    LLVMConstInt(i64, ZAN_STR_VBASE - ZAN_STR_HDR_SIZE, 0),
                    "total");
    raw = zan_call2(g->builder, malloc_fn_type, g->fn_malloc, &total, 1, "raw");
  }
  zan_irgen_emit_oom_check(g, g->rt_str_alloc, raw);
  emit_rt_alloc_add(g, total);
  LLVMValueRef hdr_off = LLVMConstInt(i64, ZAN_STR_VBASE, 0);
  LLVMValueRef user_ptr = LLVMBuildInBoundsGEP2(
      g->builder, LLVMInt8TypeInContext(g->ctx), raw, &hdr_off, 1, "usr");
  LLVMBuildStore(
      g->builder,
      LLVMConstInt(LLVMInt32TypeInContext(g->ctx), ZAN_STR_LEN_UNKNOWN, 0),
      zan_str_len_ptr(g, g->builder, user_ptr, "lenp"));
  LLVMSetAlignment(
      LLVMBuildStore(g->builder, LLVMConstInt(i64, ZAN_STR_TAG_WORD(1), 0),
                     zan_hdr_word_ptr(g, g->builder, user_ptr, "magicp")),
      8);
  if (g->rt_live_register) {
    LLVMValueRef zan_emit_live_on(zan_irgen_t *);
    void zan_emit_call_if(zan_irgen_t *, LLVMValueRef, LLVMValueRef,
                          LLVMValueRef *, unsigned);
    zan_emit_call_if(g, zan_emit_live_on(g), g->rt_live_register,
                     (LLVMValueRef[]){user_ptr, total}, 2);
  }
  if (g->check_leaks) {
    emit_leak_counter_add(g, g->g_live, 1);
  }
  LLVMBuildRet(g->builder, user_ptr);
}

for (int rel = 0; rel < 2; rel++) {
  LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8p = LLVMPointerType(i8t, 0);
  LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                      (LLVMTypeRef[]){i8p}, 1, 0);
  const char *fn_name = rel ? "zan_rt_arr_release" : "zan_rt_arr_retain";
  LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fn_name);
  if (!fn)
    fn = LLVMAddFunction(g->mod, fn_name, fnty);
  if (rel)
    g->rt_arr_release = fn;
  else
    g->rt_arr_retain = fn;
  LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMPositionBuilderAtEnd(g->builder, bb);
  LLVMValueRef obj = LLVMGetParam(fn, 0);
  LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "ret");
  LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "cont");
  LLVMBuildCondBr(
      g->builder,
      zan_icmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8p), "isnull"),
      ret_bb, cont_bb);
  LLVMPositionBuilderAtEnd(g->builder, cont_bb);
  LLVMValueRef gmoff = LLVMConstInt(i64t, (uint64_t)ZAN_ARR_RC_MAGIC_OFF, 1);
  LLVMValueRef magic_ptr =
      LLVMBuildGEP2(g->builder, i8t, obj, &gmoff, 1, "magicp");
  emit_header_read_guard(g, fn, magic_ptr, ret_bb);
  LLVMValueRef magic =
      LLVMBuildLoad2(g->builder, i64t,
                     LLVMBuildBitCast(g->builder, magic_ptr,
                                      LLVMPointerType(i64t, 0), "magicip"),
                     "magic");
  LLVMValueRef has_magic =
      zan_icmp(g->builder, LLVMIntEQ, magic,
               LLVMConstInt(i64t, ZAN_ARRAY_RC_MAGIC, 0), "hasmagic");
  LLVMBasicBlockRef work_bb =
      LLVMAppendBasicBlockInContext(g->ctx, fn, rel ? "release" : "retain");
  LLVMBuildCondBr(g->builder, has_magic, work_bb, ret_bb);
  LLVMPositionBuilderAtEnd(g->builder, work_bb);
  LLVMValueRef rcoff = LLVMConstInt(i64t, (uint64_t)ZAN_ARR_RC_OFF, 1);
  LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, i8t, obj, &rcoff, 1, "rcptr");
  LLVMValueRef rc_iptr =
      LLVMBuildBitCast(g->builder, rc_ptr, LLVMPointerType(i64t, 0), "rciptr");
  if (g->arc_guard && g->runtime_checks) {
    LLVMValueRef rc = LLVMBuildLoad2(g->builder, i64t, rc_iptr, "rc");
    LLVMSetOrdering(rc, LLVMAtomicOrderingMonotonic);
    LLVMSetAlignment(rc, 8);
    emit_arc_freed_use_check(
        g, fn, rc, obj, ZAN_ARC_FAULT_USE_ARR,
        rel ? "release through a stale reference (array was already freed)"
            : "retain through a stale reference (array was freed: missing "
              "retain when stored)");
  }
  if (!rel) {
    LLVMValueRef rc_pre = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpAdd,
                                             rc_iptr, LLVMConstInt(i64t, 1, 0),
                                             LLVMAtomicOrderingMonotonic, 0);
    emit_arc_underflow_check(g, fn, rc_pre, obj, NULL, ZAN_ARC_FAULT_ARR_RETAIN,
                             "retain of an already-freed array");
    LLVMBuildBr(g->builder, ret_bb);
  } else {
    LLVMBasicBlockRef free_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "dofree");
    if (!g->arc_guard) {
      /* 数组不进弱引用表：读到 rc == 1 说明本线程独占唯一引用，写 0 后直接进
       * free_bb（acquire fence + free），免一次硬件 LOCK XADD。 */
      LLVMValueRef rc_now = LLVMBuildLoad2(g->builder, i64t, rc_iptr, "rcnow");
      LLVMSetOrdering(rc_now, LLVMAtomicOrderingMonotonic);
      LLVMSetAlignment(rc_now, 8);
      LLVMBasicBlockRef uniq_bb =
          LLVMAppendBasicBlockInContext(g->ctx, fn, "uniq.free");
      LLVMBasicBlockRef atomic_bb =
          LLVMAppendBasicBlockInContext(g->ctx, fn, "dec.atomic");
      LLVMValueRef is_uniq = zan_icmp(g->builder, LLVMIntEQ, rc_now,
                                      LLVMConstInt(i64t, 1, 0), "isuniq");
      zan_set_branch_weights(
          g, LLVMBuildCondBr(g->builder, is_uniq, uniq_bb, atomic_bb), 2000, 1);
      LLVMPositionBuilderAtEnd(g->builder, uniq_bb);
      LLVMValueRef st0 =
          LLVMBuildStore(g->builder, LLVMConstInt(i64t, 0, 0), rc_iptr);
      LLVMSetOrdering(st0, LLVMAtomicOrderingMonotonic);
      LLVMSetAlignment(st0, 8);
      LLVMBuildBr(g->builder, free_bb);
      LLVMPositionBuilderAtEnd(g->builder, atomic_bb);
    }
    LLVMValueRef rc_old = LLVMBuildAtomicRMW(
        g->builder, LLVMAtomicRMWBinOpSub, rc_iptr, LLVMConstInt(i64t, 1, 0),
        LLVMAtomicOrderingRelease, 0);
    emit_arc_underflow_check(g, fn, rc_old, obj, NULL,
                             ZAN_ARC_FAULT_ARR_RELEASE,
                             "release of an already-freed array");
    LLVMValueRef rc1 =
        zan_sub(g->builder, rc_old, LLVMConstInt(i64t, 1, 0), "rc1");
    LLVMBuildCondBr(g->builder,
                    zan_icmp(g->builder, LLVMIntEQ, rc1,
                             LLVMConstInt(i64t, 0, 0), "iszero"),
                    free_bb, ret_bb);
    LLVMPositionBuilderAtEnd(g->builder, free_bb);
    LLVMBuildFence(g->builder, LLVMAtomicOrderingAcquire, 0, "");
    if (g->rt_live_unregister) {
      LLVMValueRef zan_emit_live_on(zan_irgen_t *);
      void zan_emit_call_if(zan_irgen_t *, LLVMValueRef, LLVMValueRef,
                            LLVMValueRef *, unsigned);
      zan_emit_call_if(g, zan_emit_live_on(g), g->rt_live_unregister, &obj, 1);
    }
    if (g->arc_guard) {
      emit_arc_quarantine(g, obj, rc_iptr, i64t);
    } else {
      LLVMValueRef raw = LLVMBuildGEP2(g->builder, i8t, obj, &rcoff, 1, "raw");
      zan_call2(g->builder,
                LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8p, 1, 0),
                g->fn_free, &raw, 1, "");
    }
    LLVMBuildBr(g->builder, ret_bb);
  }
  LLVMPositionBuilderAtEnd(g->builder, ret_bb);
  LLVMBuildRetVoid(g->builder);
}

{
  LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef retain_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                             (LLVMTypeRef[]){i8p}, 1, 0);
  g->rt_str_retain = LLVMAddFunction(g->mod, "zan_rt_str_retain", retain_type);
  LLVMBasicBlockRef bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "entry");
  LLVMPositionBuilderAtEnd(g->builder, bb);
  LLVMValueRef obj = LLVMGetParam(g->rt_str_retain, 0);
  LLVMValueRef is_null =
      zan_icmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8p), "isnull");
  LLVMBasicBlockRef ret_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "ret");
  LLVMBasicBlockRef cont_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "cont");
  LLVMBuildCondBr(g->builder, is_null, ret_bb, cont_bb);
  LLVMPositionBuilderAtEnd(g->builder, cont_bb);
  LLVMValueRef neg8 = LLVMConstInt(i64t, (uint64_t)ZAN_HDR_WORD_OFF, 1);
  LLVMValueRef magic_ptr = LLVMBuildGEP2(
      g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg8, 1, "magicp");
  emit_header_read_guard(g, g->rt_str_retain, magic_ptr, ret_bb);
  LLVMValueRef magic_iptr = LLVMBuildBitCast(
      g->builder, magic_ptr, LLVMPointerType(i64t, 0), "magicip");
  LLVMValueRef magic = LLVMBuildLoad2(g->builder, i64t, magic_iptr, "magic");
  LLVMValueRef has_magic = zan_hdr_is_string(g, magic, "hasmagic");
  LLVMBasicBlockRef retain_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "retain");
  LLVMBasicBlockRef arr_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "arrfwd");
  LLVMBuildCondBr(g->builder, has_magic, retain_bb, arr_bb);
  LLVMPositionBuilderAtEnd(g->builder, arr_bb);
  zan_call2(g->builder, retain_type, g->rt_arr_retain, &obj, 1, "");
  LLVMBuildBr(g->builder, ret_bb);
  LLVMPositionBuilderAtEnd(g->builder, retain_bb);
  LLVMTypeRef rc_ty = LLVMInt32TypeInContext(g->ctx);
  LLVMValueRef rc_iptr = zan_rc_ptr(g, g->builder, obj, "rcptr");
  LLVMValueRef rc = LLVMBuildLoad2(g->builder, rc_ty, rc_iptr, "rc");
  LLVMSetOrdering(rc, LLVMAtomicOrderingMonotonic);
  LLVMSetAlignment(rc, 4);
  LLVMValueRef is_sent =
      zan_icmp(g->builder, LLVMIntEQ, rc,
               LLVMConstInt(rc_ty, ZAN_RC_IMMORTAL, 0), "issent");
  LLVMBasicBlockRef add_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "add");
  LLVMBuildCondBr(g->builder, is_sent, ret_bb, add_bb);
  LLVMPositionBuilderAtEnd(g->builder, add_bb);
  emit_arc_freed_use_check(g, g->rt_str_retain, rc, obj, ZAN_ARC_FAULT_USE_STR,
                           "retain through a stale reference (string was "
                           "freed: missing retain when stored)");
  LLVMValueRef rc_pre = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpAdd,
                                           rc_iptr, LLVMConstInt(rc_ty, 1, 0),
                                           LLVMAtomicOrderingMonotonic, 0);
  emit_arc_underflow_check(g, g->rt_str_retain, rc_pre, obj, NULL,
                           ZAN_ARC_FAULT_STR_RETAIN,
                           "retain of an already-freed string");
  LLVMBuildBr(g->builder, ret_bb);
  LLVMPositionBuilderAtEnd(g->builder, ret_bb);
  LLVMBuildRetVoid(g->builder);
}

{
  LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef release_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                              (LLVMTypeRef[]){i8p}, 1, 0);
  if (!g->rt_str_release) {
    g->rt_str_release =
        LLVMAddFunction(g->mod, "zan_rt_str_release", release_type);
  }
  zan_mark_nounwind(g, g->rt_str_release);
  LLVMBasicBlockRef bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "entry");
  LLVMPositionBuilderAtEnd(g->builder, bb);
  LLVMValueRef obj = LLVMGetParam(g->rt_str_release, 0);
  LLVMValueRef is_null =
      zan_icmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8p), "isnull");
  LLVMBasicBlockRef ret_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "ret");
  LLVMBasicBlockRef cont_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "cont");
  zan_set_branch_weights(g,
                         LLVMBuildCondBr(g->builder, is_null, ret_bb, cont_bb),
                         1, 2000);
  LLVMPositionBuilderAtEnd(g->builder, cont_bb);
  LLVMValueRef neg8 = LLVMConstInt(i64t, (uint64_t)ZAN_HDR_WORD_OFF, 1);
  LLVMValueRef magic_ptr = LLVMBuildGEP2(
      g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg8, 1, "magicp");
  emit_header_read_guard(g, g->rt_str_release, magic_ptr, ret_bb);
  LLVMValueRef magic_iptr = LLVMBuildBitCast(
      g->builder, magic_ptr, LLVMPointerType(i64t, 0), "magicip");
  LLVMValueRef magic = LLVMBuildLoad2(g->builder, i64t, magic_iptr, "magic");
  LLVMValueRef has_magic = zan_hdr_is_string(g, magic, "hasmagic");
  LLVMBasicBlockRef rel_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "release");
  LLVMBasicBlockRef arr_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "arrfwd");
  zan_set_branch_weights(
      g, LLVMBuildCondBr(g->builder, has_magic, rel_bb, arr_bb), 2000, 1);
  LLVMPositionBuilderAtEnd(g->builder, arr_bb);
  zan_call2(g->builder, release_type, g->rt_arr_release, &obj, 1, "");
  LLVMBuildBr(g->builder, ret_bb);
  LLVMPositionBuilderAtEnd(g->builder, rel_bb);
  LLVMTypeRef rc_ty = LLVMInt32TypeInContext(g->ctx);
  LLVMValueRef neg16 = LLVMConstInt(i64t, (uint64_t)-(int64_t)ZAN_STR_VBASE, 1);
  LLVMValueRef rc_iptr = zan_rc_ptr(g, g->builder, obj, "rcptr");
  LLVMValueRef rc = LLVMBuildLoad2(g->builder, rc_ty, rc_iptr, "rc");
  LLVMSetOrdering(rc, LLVMAtomicOrderingMonotonic);
  LLVMSetAlignment(rc, 4);
  LLVMValueRef is_sent =
      zan_icmp(g->builder, LLVMIntEQ, rc,
               LLVMConstInt(rc_ty, ZAN_RC_IMMORTAL, 0), "issent");
  LLVMBasicBlockRef dec_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "dec");
  LLVMBasicBlockRef free_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "dofree");
  LLVMBuildCondBr(g->builder, is_sent, ret_bb, dec_bb);
  LLVMPositionBuilderAtEnd(g->builder, dec_bb);
  emit_arc_freed_use_check(g, g->rt_str_release, rc, obj, ZAN_ARC_FAULT_USE_STR,
                           "release through a stale reference "
                           "(string was already freed)");
  if (!g->arc_guard) {
    /* 字符串不进弱引用表：读到 rc == 1 说明本线程独占唯一引用，
     * 跳至 free_bb 走 acquire fence 直接释放，免一次硬件 LOCK XADD。 */
    LLVMBasicBlockRef atomic_dec_bb =
        LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "dec.atomic");
    LLVMValueRef is_uniq = zan_icmp(g->builder, LLVMIntEQ, rc,
                                    LLVMConstInt(rc_ty, 1, 0), "isuniq");
    zan_set_branch_weights(
        g, LLVMBuildCondBr(g->builder, is_uniq, free_bb, atomic_dec_bb), 2000,
        1);
    LLVMPositionBuilderAtEnd(g->builder, atomic_dec_bb);
  }
  LLVMValueRef rc_old = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpSub,
                                           rc_iptr, LLVMConstInt(rc_ty, 1, 0),
                                           LLVMAtomicOrderingRelease, 0);
  emit_arc_underflow_check(g, g->rt_str_release, rc_old, obj, NULL,
                           ZAN_ARC_FAULT_STR_RELEASE,
                           "release of an already-freed string");
  LLVMValueRef rc1 =
      zan_sub(g->builder, rc_old, LLVMConstInt(rc_ty, 1, 0), "rc1");
  LLVMValueRef is_zero = zan_icmp(g->builder, LLVMIntEQ, rc1,
                                  LLVMConstInt(rc_ty, 0, 0), "iszero");
  LLVMBuildCondBr(g->builder, is_zero, free_bb, ret_bb);
  LLVMPositionBuilderAtEnd(g->builder, free_bb);
  LLVMBuildFence(g->builder, LLVMAtomicOrderingAcquire, 0, "");
  if (g->rt_live_unregister) {
    LLVMValueRef zan_emit_live_on(zan_irgen_t *);
    void zan_emit_call_if(zan_irgen_t *, LLVMValueRef, LLVMValueRef,
                          LLVMValueRef *, unsigned);
    zan_emit_call_if(g, zan_emit_live_on(g), g->rt_live_unregister, &obj, 1);
  }
  LLVMValueRef header_ptr = LLVMBuildGEP2(
      g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "hdr");
  LLVMTypeRef free_fn_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                              (LLVMTypeRef[]){i8p}, 1, 0);
  if (g->arc_guard)
    emit_arc_quarantine(g, obj, rc_iptr, rc_ty);
  else
    zan_call2(g->builder, free_fn_type, g->fn_free, &header_ptr, 1, "");
  if (g->check_leaks) {
    emit_leak_counter_add(g, g->g_live, -1);
  }
  LLVMBuildBr(g->builder, ret_bb);
  LLVMPositionBuilderAtEnd(g->builder, ret_bb);
  LLVMBuildRetVoid(g->builder);
}
