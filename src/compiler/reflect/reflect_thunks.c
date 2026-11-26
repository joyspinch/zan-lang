static LLVMValueRef refl_to_slot(zan_irgen_t *g, LLVMValueRef v) {
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMTypeRef t = LLVMTypeOf(v);
  switch (LLVMGetTypeKind(t)) {
  case LLVMIntegerTypeKind:
    if (t == i64)
      return v;
    if (LLVMGetIntTypeWidth(t) == 1)
      return LLVMBuildZExt(g->builder, v, i64, "refl.r.z");
    return LLVMBuildSExt(g->builder, v, i64, "refl.r.s");
  case LLVMDoubleTypeKind:
    return LLVMBuildBitCast(g->builder, v, i64, "refl.r.d");
  case LLVMFloatTypeKind:
    return LLVMBuildBitCast(g->builder,
                            LLVMBuildFPExt(g->builder, v, dbl, "refl.r.fe"),
                            i64, "refl.r.f");
  case LLVMPointerTypeKind:
    return LLVMBuildPtrToInt(g->builder, v, i64, "refl.r.p");
  default:
    return LLVMConstInt(i64, 0, 0);
  }
}

static LLVMValueRef refl_to_slot_typed(zan_irgen_t *g, zan_type_t *zt,
                                       LLVMValueRef v, bool already_owned) {
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  if (!v || !zt)
    return v ? refl_to_slot(g, v) : LLVMConstInt(i64, 0, 0);
  if (zt->kind == TYPE_DECIMAL)
    return LLVMBuildBitCast(g->builder, refl_decimal_to_f64(g, v), i64,
                            "refl.r.dec");
  if (zt->kind == TYPE_STRUCT) {
    LLVMValueRef box = refl_emit_box_struct_val(g, zt, v, already_owned);
    return LLVMBuildPtrToInt(g->builder, box, i64, "refl.r.sb");
  }
  if (zt->kind == TYPE_NULLABLE && zt->element_type) {
    zan_type_t *et = zt->element_type;
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef some_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "refl.nsome");
    LLVMBasicBlockRef none_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "refl.nnone");
    LLVMBasicBlockRef done_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "refl.ndone");
    LLVMBuildCondBr(g->builder, nullable_has_value(g, v), some_bb, none_bb);

    LLVMPositionBuilderAtEnd(g->builder, some_bb);
    LLVMValueRef sv =
        refl_to_slot_typed(g, et, nullable_get_payload(g, v), already_owned);
    LLVMBasicBlockRef some_end = LLVMGetInsertBlock(g->builder);
    LLVMBuildBr(g->builder, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, none_bb);
    LLVMBuildBr(g->builder, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    LLVMValueRef phi = LLVMBuildPhi(g->builder, i64, "refl.nv");
    LLVMAddIncoming(phi, (LLVMValueRef[]){sv, LLVMConstInt(i64, 0, 0)},
                    (LLVMBasicBlockRef[]){some_end, none_bb}, 2);
    return phi;
  }
  if (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind)
    return extend_int_for_slot(g, v, zt, i64);
  return refl_to_slot(g, v);
}

static int refl_ret_kind(zan_type_t *rt, bool is_task) {
  if (is_task)
    return ZAN_REFL_VK_TASK;
  if (!rt || rt->kind == TYPE_VOID)
    return 0;
  if (rt->kind == TYPE_NULLABLE && rt->element_type) {
    if (rt->element_type->kind == TYPE_STRUCT)
      return ZAN_REFL_FK_STRUCT;
    if (rt->element_type->kind == TYPE_DECIMAL)
      return ZAN_REFL_FK_DECIMAL;
    return refl_field_kind(rt->element_type);
  }
  return refl_field_kind(rt);
}

static bool refl_ret_type_supported(zan_irgen_t *g, zan_type_t *rt,
                                    LLVMTypeRef rty) {
  LLVMTypeKind tk = LLVMGetTypeKind(rty);
  if (tk == LLVMVoidTypeKind || tk == LLVMIntegerTypeKind ||
      tk == LLVMFloatTypeKind || tk == LLVMDoubleTypeKind ||
      tk == LLVMPointerTypeKind)
    return true;
  if (!rt)
    return false;
  if (rt->kind == TYPE_DECIMAL)
    return true;
  if (rt->kind == TYPE_STRUCT)
    return irgen_boxed_eligible_struct(g, rt);
  if (rt->kind == TYPE_NULLABLE && rt->element_type)
    return refl_ret_type_supported(g, rt->element_type,
                                   map_type(g, rt->element_type));
  return false;
}

static LLVMValueRef refl_unpack_arg(zan_irgen_t *g, LLVMValueRef fn,
                                    zan_type_t *zt, LLVMTypeRef want,
                                    LLVMValueRef slot, LLVMValueRef vkind,
                                    LLVMBasicBlockRef fail_bb, bool *ok) {
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  if (zt && zt->kind == TYPE_STRUCT) {
    if (!irgen_boxed_eligible_struct(g, zt) ||
        LLVMGetTypeKind(want) != LLVMStructTypeKind) {
      *ok = false;
      return LLVMConstNull(want);
    }
    LLVMValueRef is_ptr = LLVMBuildAnd(
        g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, vkind,
                      LLVMConstInt(i64, ZAN_REFL_VK_PTR, 0), "refl.isp"),
        LLVMBuildICmp(g->builder, LLVMIntNE, slot, LLVMConstInt(i64, 0, 0),
                      "refl.nn"),
        "refl.ptrok");
    LLVMBasicBlockRef chk_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "arg.stag");
    LLVMBasicBlockRef ok_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "arg.sok");
    LLVMBuildCondBr(g->builder, is_ptr, chk_bb, fail_bb);

    LLVMPositionBuilderAtEnd(g->builder, chk_bb);
    LLVMValueRef boxp = LLVMBuildIntToPtr(g->builder, slot, i8ptr, "refl.sbp");
    LLVMValueRef tag_ok = emit_runtime_is_check_name(g, boxp, zt);
    LLVMBuildCondBr(g->builder, tag_ok, ok_bb, fail_bb);

    LLVMPositionBuilderAtEnd(g->builder, ok_bb);
    if (LLVMCountStructElementTypes(want) == 0)
      return LLVMConstNull(want);
    LLVMValueRef eight = LLVMConstInt(i64, 8, 0);
    LLVMValueRef p8 = LLVMBuildGEP2(g->builder, i8, boxp, &eight, 1, "refl.sp8");
    return LLVMBuildLoad2(
        g->builder, want,
        LLVMBuildBitCast(g->builder, p8, LLVMPointerType(want, 0), "refl.sp"),
        "refl.sval");
  }
  if (zt && zt->kind == TYPE_DECIMAL) {
    LLVMTypeRef dec_ty = get_decimal_type(g);
    LLVMBasicBlockRef b_dec =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "arg.dec");
    LLVMBasicBlockRef b_decld =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "arg.decld");
    LLVMBasicBlockRef b_int =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "arg.dint");
    LLVMBasicBlockRef b_dbl =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "arg.ddbl");
    LLVMBasicBlockRef b_done =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "arg.ddone");
    LLVMValueRef sw = LLVMBuildSwitch(g->builder, vkind, fail_bb, 3);
    LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_VK_DECIMAL, 0), b_dec);
    LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_VK_INT, 0), b_int);
    LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_VK_DOUBLE, 0), b_dbl);

    LLVMPositionBuilderAtEnd(g->builder, b_dec);
    LLVMBuildCondBr(g->builder,
                    LLVMBuildICmp(g->builder, LLVMIntNE, slot,
                                  LLVMConstInt(i64, 0, 0), "refl.dnn"),
                    b_decld, fail_bb);
    LLVMPositionBuilderAtEnd(g->builder, b_decld);
    LLVMValueRef v_dec = LLVMBuildLoad2(
        g->builder, dec_ty,
        LLVMBuildIntToPtr(g->builder, slot, LLVMPointerType(dec_ty, 0),
                          "refl.dp"),
        "refl.dv");
    LLVMBuildBr(g->builder, b_done);

    LLVMPositionBuilderAtEnd(g->builder, b_int);
    LLVMValueRef v_int = refl_decimal_from_i64(g, slot);
    LLVMBuildBr(g->builder, b_done);

    LLVMPositionBuilderAtEnd(g->builder, b_dbl);
    LLVMValueRef v_dbl = refl_decimal_from_f64(
        g, LLVMBuildBitCast(g->builder, slot, dbl, "refl.dfbc"));
    LLVMBuildBr(g->builder, b_done);

    LLVMPositionBuilderAtEnd(g->builder, b_done);
    LLVMValueRef phi = LLVMBuildPhi(g->builder, dec_ty, "refl.dphi");
    LLVMAddIncoming(phi, (LLVMValueRef[]){v_dec, v_int, v_dbl},
                    (LLVMBasicBlockRef[]){b_decld, b_int, b_dbl}, 3);
    return phi;
  }
  if (zt && zt->kind == TYPE_NULLABLE && zt->element_type &&
      llvm_is_nullable(want)) {
    zan_type_t *et = zt->element_type;
    LLVMValueRef is_null_arg = LLVMBuildAnd(
        g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, vkind,
                      LLVMConstInt(i64, ZAN_REFL_VK_PTR, 0), "refl.nkp"),
        LLVMBuildICmp(g->builder, LLVMIntEQ, slot, LLVMConstInt(i64, 0, 0),
                      "refl.nsz"),
        "refl.nisnull");
    LLVMBasicBlockRef null_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "arg.nnull");
    LLVMBasicBlockRef val_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "arg.nval");
    LLVMBasicBlockRef done_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "arg.ndone");
    LLVMBuildCondBr(g->builder, is_null_arg, null_bb, val_bb);

    LLVMPositionBuilderAtEnd(g->builder, null_bb);
    LLVMBuildBr(g->builder, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, val_bb);
    LLVMValueRef pv = refl_unpack_arg(g, fn, et, nullable_payload_type(want),
                                      slot, vkind, fail_bb, ok);
    LLVMValueRef somev = nullable_some(g, want, pv);
    LLVMBasicBlockRef val_end = LLVMGetInsertBlock(g->builder);
    LLVMBuildBr(g->builder, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    LLVMValueRef phi = LLVMBuildPhi(g->builder, want, "refl.nphi");
    LLVMAddIncoming(phi, (LLVMValueRef[]){nullable_none(want), somev},
                    (LLVMBasicBlockRef[]){null_bb, val_end}, 2);
    return phi;
  }
  LLVMTypeKind tk = LLVMGetTypeKind(want);
  if (tk == LLVMIntegerTypeKind || tk == LLVMFloatTypeKind ||
      tk == LLVMDoubleTypeKind) {
    if (zt && refl_type_is_numeric_scalar(zt)) {
      LLVMValueRef is_num = LLVMBuildOr(
          g->builder,
          LLVMBuildICmp(g->builder, LLVMIntEQ, vkind,
                        LLVMConstInt(i64, ZAN_REFL_VK_INT, 0), "refl.isi"),
          LLVMBuildICmp(g->builder, LLVMIntEQ, vkind,
                        LLVMConstInt(i64, ZAN_REFL_VK_DOUBLE, 0), "refl.isd"),
          "refl.isnum");
      LLVMBasicBlockRef ok_bb =
          LLVMAppendBasicBlockInContext(g->ctx, fn, "arg.num");
      LLVMBuildCondBr(g->builder, is_num, ok_bb, fail_bb);
      LLVMPositionBuilderAtEnd(g->builder, ok_bb);
      return refl_convert_slot_to_scalar(g, zt, slot, vkind);
    }
    if (tk == LLVMFloatTypeKind || tk == LLVMDoubleTypeKind) {
      LLVMValueRef is_fp = LLVMBuildICmp(
          g->builder, LLVMIntEQ, vkind, LLVMConstInt(i64, ZAN_REFL_VK_DOUBLE, 0),
          "refl.isfp");
      LLVMValueRef d = LLVMBuildSelect(
          g->builder, is_fp, LLVMBuildBitCast(g->builder, slot, dbl, "refl.dbc"),
          LLVMBuildSIToFP(g->builder, slot, dbl, "refl.i2d"), "refl.dval");
      return tk == LLVMFloatTypeKind
                 ? LLVMBuildFPTrunc(g->builder, d, want, "refl.fval")
                 : d;
    }
    return want == i64 ? slot : LLVMBuildTrunc(g->builder, slot, want, "refl.tr");
  }
  if (tk == LLVMPointerTypeKind) {
    LLVMValueRef is_ptr = LLVMBuildICmp(
        g->builder, LLVMIntEQ, vkind, LLVMConstInt(i64, ZAN_REFL_VK_PTR, 0),
        "refl.isp");
    LLVMBasicBlockRef ok_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "arg.ptr");
    LLVMBuildCondBr(g->builder, is_ptr, ok_bb, fail_bb);
    LLVMPositionBuilderAtEnd(g->builder, ok_bb);
    return LLVMBuildIntToPtr(g->builder, slot, want, "refl.a.p");
  }
  *ok = false;
  return LLVMConstNull(want);
}

static zan_type_t *refl_concrete_inst(zan_symbol_t *sym, zan_type_t *inst) {
  if (!sym || !inst || !is_user_generic_sym(sym))
    return NULL;
  if (inst->sym != sym || inst->type_arg_count <= 0 || !inst->type_args)
    return NULL;
  if (!type_is_concrete(inst) || type_mentions_canon(inst))
    return NULL;
  return inst;
}

static zan_type_t *refl_method_owner_inst(zan_irgen_t *g, zan_symbol_t *sym,
                                          zan_type_t *inst,
                                          zan_symbol_t *msym) {
  if (!msym || !msym->parent || !is_user_generic_sym(msym->parent))
    return NULL;
  zan_type_t *cur = inst ? inst : (sym ? sym->type : NULL);
  zan_type_t *vo = cur ? vtable_owner_inst(g, cur, msym->parent) : NULL;
  if (vo && vo->type_arg_count > 0 && vo->type_args && type_is_concrete(vo) &&
      !type_mentions_canon(vo))
    return vo;
  return NULL;
}

static LLVMValueRef refl_make_thunk(zan_irgen_t *g, LLVMValueRef fn,
                                    LLVMTypeRef fn_ty, LLVMValueRef gctx_arg,
                                    zan_ast_list_t *params,
                                    zan_type_t *owner_inst,
                                    zan_type_t *ret_type, bool is_static,
                                    bool is_task, const char *tag) {
  if (!fn || !fn_ty)
    return NULL;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);

  unsigned raw_total = LLVMCountParamTypes(fn_ty);
  unsigned expected_user =
      (is_static ? 0u : 1u) + (unsigned)(params ? params->count : 0);
  bool pass_gctx = gctx_arg && raw_total == expected_user + 1u;
  unsigned total = pass_gctx ? (raw_total - 1u) : raw_total;
  unsigned max_params = is_static ? ZAN_REFL_ARG_SLOTS : ZAN_REFL_ARG_SLOTS + 1;
  if (total > max_params)
    return NULL;
  LLVMTypeRef ptypes[ZAN_REFL_ARG_SLOTS + 2];
  LLVMGetParamTypes(fn_ty, ptypes);
  unsigned first = is_static ? 0 : 1;
  if (!is_static &&
      (total < 1 || LLVMGetTypeKind(ptypes[0]) != LLVMPointerTypeKind))
    return NULL;
  LLVMTypeRef rty = LLVMGetReturnType(fn_ty);
  if (!refl_ret_type_supported(g, ret_type, rty))
    return NULL;

  /* Inherited copies must refer to the same callable payload. Key by the
   * complete LLVM callee identity, receiver convention, and bound gctx
   * constant (for __Canon instantiations), never by member name. */
  size_t fn_len = 0;
  const char *fn_name = LLVMGetValueName2(fn, &fn_len);
  size_t gctx_len = 0;
  const char *gctx_name = NULL;
  if (pass_gctx) {
    LLVMValueRef gkey = gctx_arg;
    while (gkey && LLVMIsAConstantExpr(gkey) &&
           LLVMGetConstOpcode(gkey) == LLVMBitCast)
      gkey = LLVMGetOperand(gkey, 0);
    if (gkey && !LLVMIsNull(gkey))
      gctx_name = LLVMGetValueName2(gkey, &gctx_len);
    if (!gctx_name || !gctx_len) {
      gctx_name = "null";
      gctx_len = 4;
    }
  }
  size_t name_cap = fn_len + gctx_len + 64;
  char *nm = (char *)malloc(name_cap);
  if (!nm)
    return NULL;
  if (pass_gctx) {
    snprintf(nm, name_cap, "__zan.refl.thunk.%s.%c.%.*s.%.*s",
             tag ? tag : "m", is_static ? 's' : 'i', (int)fn_len, fn_name,
             (int)gctx_len, gctx_name);
  } else {
    snprintf(nm, name_cap, "__zan.refl.thunk.%s.%c.%.*s", tag ? tag : "m",
             is_static ? 's' : 'i', (int)fn_len, fn_name);
  }
  LLVMValueRef cached = LLVMGetNamedFunction(g->mod, nm);
  if (cached) {
    free(nm);
    return LLVMConstBitCast(cached, i8ptr);
  }
  LLVMTypeRef th_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i64ptr, i64ptr}, 3, 0);
  LLVMValueRef th = LLVMAddFunction(g->mod, nm, th_ty);
  free(nm);
  g->refl_thunk_count++;
  LLVMSetLinkage(th, LLVMInternalLinkage);

  zan_builder_point_t pt = zan_builder_save(g);
  LLVMValueRef saved_fn = g->current_fn;
  LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
  zan_type_t *saved_fn_zan_ret = g->current_fn_zan_ret_type;
  zan_ast_node_t *saved_fn_body = g->current_fn_body;
  bool saved_fn_is_main = g->current_fn_is_main;
  bool saved_fn_is_ctor = g->current_fn_is_ctor;

  g->current_fn = th;
  g->current_fn_ret_type = i64;
  g->current_fn_zan_ret_type = g->binder->type_long;
  g->current_fn_body = NULL;
  g->current_fn_is_main = false;
  g->current_fn_is_ctor = false;

  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, th, "entry");
  LLVMBasicBlockRef fail_bb = LLVMAppendBasicBlockInContext(g->ctx, th, "fail");
  LLVMPositionBuilderAtEnd(g->builder, entry);

  bool ok = true;
  LLVMValueRef call_args[ZAN_REFL_ARG_SLOTS + 2];
  if (!is_static)
    call_args[0] = LLVMBuildBitCast(g->builder, LLVMGetParam(th, 0), ptypes[0],
                                    "refl.self");
  for (unsigned i = first; i < total && ok; i++) {
    unsigned ai = i - first;
    LLVMValueRef k = LLVMConstInt(i64, (unsigned long long)ai, 0);
    LLVMValueRef sp =
        LLVMBuildGEP2(g->builder, i64, LLVMGetParam(th, 1), &k, 1, "refl.as");
    LLVMValueRef slot = LLVMBuildLoad2(g->builder, i64, sp, "refl.av");
    LLVMValueRef vk_off =
        LLVMConstInt(i64, (unsigned long long)(ZAN_REFL_ARG_SLOTS + ai), 0);
    LLVMValueRef vkp = LLVMBuildGEP2(g->builder, i64, LLVMGetParam(th, 1),
                                     &vk_off, 1, "refl.vkp");
    LLVMValueRef vkind = LLVMBuildLoad2(g->builder, i64, vkp, "refl.vk");
    zan_type_t *pzt = NULL;
    if (params && (int)ai < params->count) {
      pzt = zan_binder_resolve_type(g->binder, params->items[ai]->param.type);
      if (pzt && owner_inst)
        pzt = subst_type_param_deep(g, pzt, owner_inst);
    }
    call_args[i] =
        refl_unpack_arg(g, th, pzt, ptypes[i], slot, vkind, fail_bb, &ok);
  }
  if (!ok) {
    LLVMDeleteFunction(th);
    g->current_fn = saved_fn;
    g->current_fn_ret_type = saved_fn_ret;
    g->current_fn_zan_ret_type = saved_fn_zan_ret;
    g->current_fn_body = saved_fn_body;
    g->current_fn_is_main = saved_fn_is_main;
    g->current_fn_is_ctor = saved_fn_is_ctor;
    zan_builder_restore(g, pt);
    return NULL;
  }

  if (pass_gctx) {
    call_args[total] = (LLVMTypeOf(gctx_arg) != ptypes[total])
                           ? LLVMBuildBitCast(g->builder, gctx_arg,
                                              ptypes[total], "refl.gctx")
                           : gctx_arg;
  }
  LLVMValueRef res = zan_call2(g->builder, fn_ty, fn, call_args, raw_total, "");
  LLVMValueRef slot = (is_task || LLVMGetTypeKind(rty) == LLVMVoidTypeKind)
                          ? LLVMConstInt(i64, 0, 0)
                          : refl_to_slot_typed(g, ret_type, res, true);
  if (is_task) {
    /* async 方法：入口（ramp）只建帧不调度——直接把帧指针塞进槽位，
     * 协程永远不会跑（没人 co_ready 它，也没人 track，帧会被回收）。
     * 补齐 spawn 义务：awaiter=reap + token + track + ready，槽位放
     * Task.IsDone 可轮询的 token。 */
    LLVMValueRef sub_i8 =
        LLVMBuildBitCast(g->builder, res, i8ptr, "refl.taskf");
    LLVMValueRef tok = emit_detach_async_call(g, sub_i8, false, true);
    slot = LLVMBuildPtrToInt(g->builder, tok, i64, "refl.tok");
  }
  LLVMBuildStore(g->builder, slot, LLVMGetParam(th, 2));
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));

  LLVMPositionBuilderAtEnd(g->builder, fail_bb);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

  g->current_fn = saved_fn;
  g->current_fn_ret_type = saved_fn_ret;
  g->current_fn_zan_ret_type = saved_fn_zan_ret;
  g->current_fn_body = saved_fn_body;
  g->current_fn_is_main = saved_fn_is_main;
  g->current_fn_is_ctor = saved_fn_is_ctor;
  zan_builder_restore(g, pt);
  return LLVMConstBitCast(th, i8ptr);
}

static LLVMValueRef refl_param_names(zan_irgen_t *g, zan_ast_list_t *params,
                                     zan_type_t *owner_inst) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  if (!params || params->count <= 0)
    return LLVMConstNull(i8ptr);
  LLVMValueRef *names =
      (LLVMValueRef *)calloc((size_t)params->count, sizeof(LLVMValueRef));
  if (!names)
    return LLVMConstNull(i8ptr);
  for (int i = 0; i < params->count; i++) {
    zan_ast_node_t *p = params->items[i];
    zan_type_t *pt = zan_binder_resolve_type(g->binder, p->param.type);
    if (pt && owner_inst)
      pt = subst_type_param_deep(g, pt, owner_inst);
    names[i] = refl_field_type_name(g, pt);
  }
  LLVMTypeRef arr_ty = LLVMArrayType(i8ptr, (unsigned)params->count);
  char nm[64];
  snprintf(nm, sizeof(nm), "__zan.refl.ptypes.%d", g->refl_str_count++);
  LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
  LLVMSetInitializer(gv, LLVMConstArray(i8ptr, names, (unsigned)params->count));
  LLVMSetLinkage(gv, LLVMPrivateLinkage);
  LLVMSetGlobalConstant(gv, 1);
  free(names);
  return LLVMConstBitCast(gv, i8ptr);
}

static LLVMValueRef refl_method_record(zan_irgen_t *g, zan_symbol_t *sym,
                                       zan_type_t *inst, zan_symbol_t *msym,
                                       bool late) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef mrec = refl_method_type(g);
  zan_ast_node_t *decl = msym->decl;
  zan_type_t *owner_inst = refl_method_owner_inst(g, sym, inst, msym);
  zan_type_t *rt =
      decl->method_decl.return_type
          ? zan_binder_resolve_type(g->binder, decl->method_decl.return_type)
          : g->binder->type_void;
  if (rt && owner_inst)
    rt = subst_type_param_deep(g, rt, owner_inst);
  bool is_static = (msym->modifiers & MOD_STATIC) != 0 ||
                   (decl->method_decl.modifiers & MOD_STATIC) != 0;
  int flags = is_static ? ZAN_REFL_MF_STATIC : 0;
  if (decl->method_decl.modifiers & (MOD_VIRTUAL | MOD_OVERRIDE | MOD_ABSTRACT))
    flags |= ZAN_REFL_MF_VIRTUAL;
  flags |= refl_accessor_flag(sym, msym->name);
  /* async 方法：thunk 走 detach spawn 语义，槽位回 task token。
   * 判据用 MOD_ASYNC 而非 is_task_return——后者只标记声明返回
   * Task/ValueTask 的方法（desugar_async_task_method），裸 `async long`
   * 的 ramp 同样返回帧指针，也需要 spawn 义务。 */
  bool is_task = decl && decl->kind == AST_METHOD_DECL &&
                 (decl->method_decl.modifiers & MOD_ASYNC) != 0;
  LLVMValueRef thunk = LLVMConstNull(i8ptr);
  if (late) {
    /* 在 publish 模式下，仅为属性访问器（get_/set_）、带注解方法、
     * 设计稿事件处理器与框架生命周期钩子（B-ID160）生成 thunk，
     * 避免为所有普通方法生成包装桩强行锚定死代码，让链接器 --gc-sections
     * 正常工作 */
    /* F4/S1: 谓词上移共享（P_method_thunk），emit_target body-root 同源判定。
     */
    bool need_thunk = refl_method_needs_thunk(g, sym, decl);
    if (need_thunk) {
      LLVMValueRef target_fn = NULL;
      LLVMTypeRef target_ty = NULL;
      LLVMValueRef gctx_arg = NULL;
      if (msym->parent && is_user_generic_sym(msym->parent)) {
        if (owner_inst) {
          target_fn = find_generic_fn(g, msym, owner_inst->type_args,
                                      owner_inst->type_arg_count, &target_ty);
          if (target_fn) {
            gctx_arg = resolve_gctx_arg_for_inst(
                g, false, owner_inst->sym, owner_inst, NULL, 0);
            if (gctx_arg && !LLVMIsAConstant(gctx_arg)) {
              target_fn = NULL;
              gctx_arg = NULL;
            }
          }
        }
      } else {
        int fi = irgen_find_function(g, msym);
        if (fi >= 0) {
          target_fn = g->functions[fi].fn;
          target_ty = g->functions[fi].fn_type;
        }
      }
      if (target_fn && target_ty) {
        LLVMValueRef t = refl_make_thunk(
            g, target_fn, target_ty, gctx_arg, &decl->method_decl.params,
            owner_inst, rt, is_static, is_task, "m");
        if (t)
          thunk = t;
      }
    }
  }
  char mattrbuf[4096];
  refl_pack_attrs(decl ? zan_ast_attributes(decl) : NULL, mattrbuf,
                  sizeof(mattrbuf));
  LLVMValueRef mattrs = refl_const_string(g, mattrbuf, (int)strlen(mattrbuf));
  return LLVMConstNamedStruct(
      mrec,
      (LLVMValueRef[]){
          refl_const_string(g, msym->name.str, (int)msym->name.len),
          refl_field_type_name(g, rt),
          LLVMConstInt(i64, (unsigned long long)refl_ret_kind(rt, is_task), 0),
          LLVMConstInt(i64, (unsigned long long)decl->method_decl.params.count,
                       0),
          refl_param_names(g, &decl->method_decl.params, owner_inst), thunk,
          LLVMConstInt(i64, (unsigned long long)flags, 0), mattrs,
          zan_ast_attributes_const(decl)->count ? refl_declared_attrs(g, decl)
                                                : LLVMConstNull(i8ptr)},
      9);
}

static LLVMValueRef refl_make_ctor_thunk(zan_irgen_t *g, zan_symbol_t *sym,
                                         zan_type_t *inst,
                                         zan_ast_node_t *decl,
                                         LLVMValueRef ctor_fn,
                                         LLVMTypeRef ctor_ty,
                                         LLVMValueRef ctor_gctx) {
  if (!sym || !sym->type)
    return NULL;
  bool is_struct = sym->type->kind == TYPE_STRUCT;
  if (sym->type->kind != TYPE_CLASS &&
      !(is_struct && irgen_boxed_eligible_struct(g, sym->type)))
    return NULL;
  LLVMTypeRef st = get_struct_llvm_type(g, sym);
  if (!st)
    return NULL;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);

  unsigned raw_total = (ctor_fn && ctor_ty) ? LLVMCountParamTypes(ctor_ty) : 0;
  unsigned total = (ctor_gctx && raw_total > 0) ? (raw_total - 1) : raw_total;
  if (total > ZAN_REFL_ARG_SLOTS + 1)
    return NULL;
  LLVMTypeRef ptypes[ZAN_REFL_ARG_SLOTS + 2];
  if (raw_total > 0)
    LLVMGetParamTypes(ctor_ty, ptypes);

  char nm[128];
  snprintf(nm, sizeof(nm), "__zan.refl.thunk.new.%d", g->refl_thunk_count++);
  LLVMValueRef th = LLVMAddFunction(
      g->mod, nm,
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i64ptr, i64ptr}, 3, 0));
  LLVMSetLinkage(th, LLVMInternalLinkage);

  uint32_t saved_file = g->di_cur_file, saved_line = g->di_cur_line;
  uint32_t saved_col = g->di_cur_col;
  LLVMMetadataRef saved_last_loc = g->di_last_loc;
  LLVMValueRef saved_last_fn = g->di_last_loc_fn;
  zan_builder_point_t pt = zan_builder_save(g);
  LLVMValueRef saved_fn = g->current_fn;
  LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
  zan_type_t *saved_fn_zan_ret = g->current_fn_zan_ret_type;
  zan_ast_node_t *saved_fn_body = g->current_fn_body;
  bool saved_fn_is_main = g->current_fn_is_main;
  bool saved_fn_is_ctor = g->current_fn_is_ctor;

  g->current_fn = th;
  g->current_fn_ret_type = i64;
  g->current_fn_zan_ret_type = g->binder->type_long;
  g->current_fn_body = NULL;
  g->current_fn_is_main = false;
  g->current_fn_is_ctor = false;

  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, th, "entry");
  LLVMBasicBlockRef fail_bb = LLVMAppendBasicBlockInContext(g->ctx, th, "fail");
  LLVMPositionBuilderAtEnd(g->builder, entry);
  di_clear(g);

  bool ok = true;
  LLVMValueRef args[ZAN_REFL_ARG_SLOTS + 1];
  zan_ast_list_t *params = decl ? &decl->method_decl.params : NULL;
  if (total > 0) {
    for (unsigned i = 1; i < total && ok; i++) {
      unsigned ai = i - 1;
      LLVMValueRef k = LLVMConstInt(i64, (unsigned long long)ai, 0);
      LLVMValueRef sp =
          LLVMBuildGEP2(g->builder, i64, LLVMGetParam(th, 1), &k, 1, "refl.as");
      LLVMValueRef vk_off =
          LLVMConstInt(i64, (unsigned long long)(ZAN_REFL_ARG_SLOTS + ai), 0);
      LLVMValueRef vkp = LLVMBuildGEP2(g->builder, i64, LLVMGetParam(th, 1),
                                       &vk_off, 1, "refl.vkp");
      zan_type_t *pzt = NULL;
      if (params && (int)ai < params->count) {
        pzt = zan_binder_resolve_type(g->binder, params->items[ai]->param.type);
        if (pzt && inst)
          pzt = subst_type_param_deep(g, pzt, inst);
      }
      args[i] = refl_unpack_arg(
          g, th, pzt, ptypes[i], LLVMBuildLoad2(g->builder, i64, sp, "refl.av"),
          LLVMBuildLoad2(g->builder, i64, vkp, "refl.vk"), fail_bb, &ok);
    }
    if (!ok) {
      LLVMDeleteFunction(th);
      g->current_fn = saved_fn;
      g->current_fn_ret_type = saved_fn_ret;
      g->current_fn_zan_ret_type = saved_fn_zan_ret;
      g->current_fn_body = saved_fn_body;
      g->current_fn_is_main = saved_fn_is_main;
      g->current_fn_is_ctor = saved_fn_is_ctor;
      zan_builder_restore(g, pt);
      g->di_cur_file = saved_file;
      g->di_cur_line = saved_line;
      g->di_cur_col = saved_col;
      g->di_last_loc = saved_last_loc;
      g->di_last_loc_fn = saved_last_fn;
      return NULL;
    }
  }

  if (is_struct) {
    LLVMValueRef tmp_s = LLVMBuildAlloca(g->builder, st, "refl.news");
    LLVMBuildStore(g->builder, LLVMConstNull(st), tmp_s);
    if (total > 0) {
      args[0] = LLVMBuildBitCast(g->builder, tmp_s, ptypes[0], "refl.this");
      zan_call_with_gctx(g, ctor_ty, ctor_fn, args, (int)total, ctor_gctx, "");
    } else {
      local_scope_t *locals = local_scope_new(g->arena);
      emit_implicit_field_initializers(g, sym, inst ? inst : sym->type, tmp_s,
                                       locals);
    }
    LLVMValueRef sval = LLVMBuildLoad2(g->builder, st, tmp_s, "refl.newsv");
    LLVMValueRef raw =
        refl_emit_box_struct_val(g, inst ? inst : sym->type, sval, true);
    LLVMBuildStore(g->builder,
                   LLVMBuildPtrToInt(g->builder, raw, i64, "refl.newsi"),
                   LLVMGetParam(th, 2));
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));
  } else {
    /* 站点必须在 desc_init（站点→类型名表）冻结**之前**登记：这里晚于
     * 名表发射，新建点查无此名，反射造出的对象过不了任何 is/下转
     * （ORM MapRow 的 (T)obj 全军覆没）。预登记站点见
     * refl_reserve_thunk_sites；此处只取用。 */
    int site = -1;
    for (int i = 0; i < g->refl_mtab_count; i++) {
      if (g->refl_mtabs[i].ctors && g->refl_mtabs[i].sym == sym &&
          g->refl_mtabs[i].inst == inst && g->refl_mtabs[i].site >= 0) {
        site = g->refl_mtabs[i].site;
        break;
      }
    }
    if (site < 0)
      site = reserve_arc_site(g, sym, inst ? inst : sym->type, 0, NULL);
    LLVMValueRef raw = zan_call2(
        g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i64, i64, i8ptr}, 3, 0),
        g->rt_alloc,
        (LLVMValueRef[]){LLVMSizeOf(st),
                         arc_class_site_val(g, arc_site_arg(g, site), st),
                         LLVMConstNull(i8ptr)},
        3, "refl.newobj");
    LLVMValueRef obj =
        LLVMBuildBitCast(g->builder, raw, LLVMPointerType(st, 0), "refl.objp");
    LLVMBuildStore(g->builder, LLVMConstNull(st), obj);
    if (class_has_virtual_methods(sym)) {
      LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, st, obj, 0, "refl.vpf");
      LLVMValueRef vt =
          inst ? get_vtable_global_inst(g, sym, inst)
               : get_vtable_global(g, sym);
      LLVMBuildStore(g->builder, LLVMBuildBitCast(g->builder, vt, i8ptr, "refl.vt"),
                     vp);
    }

    emit_eh_tmp_push(g, obj);
    if (total > 0) {
      args[0] = LLVMBuildBitCast(g->builder, obj, ptypes[0], "refl.this");
      zan_call_with_gctx(g, ctor_ty, ctor_fn, args, (int)total, ctor_gctx, "");
    } else {
      /* 无显式构造的类：new 路径在调用点内联发字段初始化器
       * （emit_implicit_field_initializers），反射造对象没有调用点——
       * thunk 里补发同一份（EH 语义同 new 路径），CreateInstance 与
       * new 对齐。 */
      local_scope_t *locals = local_scope_new(g->arena);
      emit_implicit_field_initializers(g, sym, inst ? inst : sym->type, obj,
                                       locals);
    }
    emit_eh_tmp_pop(g);
    LLVMBuildStore(g->builder,
                   LLVMBuildPtrToInt(g->builder, raw, i64, "refl.newi"),
                   LLVMGetParam(th, 2));
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));
  }

  LLVMPositionBuilderAtEnd(g->builder, fail_bb);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

  g->current_fn = saved_fn;
  g->current_fn_ret_type = saved_fn_ret;
  g->current_fn_zan_ret_type = saved_fn_zan_ret;
  g->current_fn_body = saved_fn_body;
  g->current_fn_is_main = saved_fn_is_main;
  g->current_fn_is_ctor = saved_fn_is_ctor;
  zan_builder_restore(g, pt);
  g->di_cur_file = saved_file;
  g->di_cur_line = saved_line;
  g->di_cur_col = saved_col;
  g->di_last_loc = saved_last_loc;
  g->di_last_loc_fn = saved_last_fn;
  return LLVMConstBitCast(th, i8ptr);
}

static LLVMValueRef refl_ctor_record(zan_irgen_t *g, zan_symbol_t *sym,
                                     zan_type_t *inst, zan_ast_node_t *decl,
                                     bool late) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMValueRef thunk = LLVMConstNull(i8ptr);
  late = late && refl_ctor_needs_thunk(g);
  zan_type_t *cinst = refl_concrete_inst(sym, inst);
  int rk = refl_ret_kind(cinst ? cinst : (sym ? sym->type : NULL), false);
  if (!decl) {
    if (late && (!is_user_generic_sym(sym) || cinst != NULL)) {
      LLVMValueRef t =
          refl_make_ctor_thunk(g, sym, cinst, NULL, NULL, NULL, NULL);
      if (t)
        thunk = t;
    }
    return LLVMConstNamedStruct(
        refl_method_type(g),
        (LLVMValueRef[]){
            refl_const_string(g, ".ctor", 5),
            refl_const_string(g, sym->name.str, (int)sym->name.len),
            LLVMConstInt(i64, (unsigned long long)rk, 0),
            LLVMConstInt(i64, 0, 0), refl_param_names(g, NULL, NULL), thunk,
            LLVMConstInt(i64, 0, 0), refl_empty_string(g),
            LLVMConstNull(i8ptr)},
        9);
  }
  if (late) {
    if (is_user_generic_sym(sym)) {
      if (cinst) {
        LLVMTypeRef ctor_ty = NULL;
        LLVMValueRef ctor_fn = find_generic_ctor(
            g, sym, decl, cinst->type_args, cinst->type_arg_count, &ctor_ty);
        if (ctor_fn && ctor_ty) {
          LLVMValueRef ctor_gctx =
              resolve_gctx_arg_for_inst(g, false, sym, cinst, NULL, 0);
          if (!ctor_gctx || LLVMIsAConstant(ctor_gctx)) {
            LLVMValueRef t = refl_make_ctor_thunk(
                g, sym, cinst, decl, ctor_fn, ctor_ty, ctor_gctx);
            if (t)
              thunk = t;
          }
        }
      }
    } else {
      for (int i = 0; i < g->ctor_count; i++) {
        if (g->ctors[i].type_sym != sym || g->ctors[i].decl != decl)
          continue;
        LLVMValueRef t = refl_make_ctor_thunk(
            g, sym, NULL, decl, g->ctors[i].fn, g->ctors[i].fn_type, NULL);
        if (t)
          thunk = t;
        break;
      }
    }
  }
  return LLVMConstNamedStruct(
      refl_method_type(g),
      (LLVMValueRef[]){
          refl_const_string(g, ".ctor", 5),
          refl_const_string(g, sym->name.str, (int)sym->name.len),
          LLVMConstInt(i64, (unsigned long long)rk, 0),
          LLVMConstInt(i64, (unsigned long long)decl->method_decl.params.count,
                       0),
          refl_param_names(g, &decl->method_decl.params, cinst), thunk,
          LLVMConstInt(i64, 0, 0), refl_empty_string(g), LLVMConstNull(i8ptr)},
      9);
}

typedef struct {
  zan_builder_point_t point;
  LLVMValueRef fn;
  uint32_t file, line, col;
  LLVMMetadataRef last_loc;
  LLVMValueRef last_loc_fn;
} refl_list_point_t;

static refl_list_point_t refl_list_save(zan_irgen_t *g) {
  return (refl_list_point_t){zan_builder_save(g), g->current_fn,
                             g->di_cur_file,      g->di_cur_line,
                             g->di_cur_col,       g->di_last_loc,
                             g->di_last_loc_fn};
}

static void refl_list_restore(zan_irgen_t *g, refl_list_point_t pt) {
  zan_builder_restore(g, pt.point);
  g->current_fn = pt.fn;
  g->di_cur_file = pt.file;
  g->di_cur_line = pt.line;
  g->di_cur_col = pt.col;
  g->di_last_loc = pt.last_loc;
  g->di_last_loc_fn = pt.last_loc_fn;
}

static bool refl_list_supported_element(zan_irgen_t *g, zan_type_t *elem_type) {
  if (!elem_type)
    return false;
  LLVMTypeRef mty = map_type(g, elem_type);
  LLVMTypeKind kind = LLVMGetTypeKind(mty);
  if (kind == LLVMIntegerTypeKind || kind == LLVMFloatTypeKind ||
      kind == LLVMDoubleTypeKind || kind == LLVMPointerTypeKind)
    return true;
  if (elem_type->kind == TYPE_DECIMAL)
    return true;
  if (elem_type->kind == TYPE_STRUCT)
    return irgen_boxed_eligible_struct(g, elem_type) &&
           llvm_scalar_size(mty) > 0;
  if (elem_type->kind == TYPE_NULLABLE && elem_type->element_type) {
    zan_type_t *et = elem_type->element_type;
    if (et->kind == TYPE_STRUCT && !irgen_boxed_eligible_struct(g, et))
      return false;
    return llvm_is_nullable(mty) && llvm_scalar_size(mty) > 0;
  }
  return false;
}

static void refl_register_mtab(zan_irgen_t *g, LLVMValueRef gv,
                               LLVMTypeRef arr_ty, zan_symbol_t *sym,
                               zan_type_t *inst, zan_type_t *list_type, int n,
                               bool ctors) {
  if (g->refl_mtab_count == g->refl_mtab_cap) {
    int ncap = g->refl_mtab_cap ? g->refl_mtab_cap * 2 : 16;
    g->refl_mtabs = (typeof(g->refl_mtabs))realloc(
        g->refl_mtabs, (size_t)ncap * sizeof(*g->refl_mtabs));
    g->refl_mtab_cap = ncap;
  }
  int i = g->refl_mtab_count++;
  g->refl_mtabs[i].gv = gv;
  g->refl_mtabs[i].arr_ty = arr_ty;
  g->refl_mtabs[i].sym = sym;
  g->refl_mtabs[i].inst = refl_concrete_inst(sym, inst);
  g->refl_mtabs[i].n = n;
  g->refl_mtabs[i].ctors = ctors;
  g->refl_mtabs[i].ctor_ready = false;
  g->refl_mtabs[i].list_type = list_type;
  g->refl_mtabs[i].site = -1;
}

static LLVMValueRef refl_builtin_list_ctor_record(zan_irgen_t *g, zan_type_t *t,
                                                  bool late) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
  LLVMValueRef thunk = LLVMConstNull(i8ptr);
  if (late && refl_ctor_needs_thunk(g)) {
    zan_type_t *elem_type = container_elem_type(t);
    uint32_t saved_file = g->di_cur_file, saved_line = g->di_cur_line;
    g->di_cur_file = g->di_cur_line = 0;
    int site = reserve_arc_site(g, NULL, NULL, 1, elem_type);
    g->di_cur_file = saved_file;
    g->di_cur_line = saved_line;
    char nm[96];
    snprintf(nm, sizeof(nm), "__zan.refl.thunk.list_ctor.%d",
             g->refl_thunk_count++);
    LLVMValueRef th = LLVMAddFunction(
        g->mod, nm,
        LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i64ptr, i64ptr}, 3, 0));
    LLVMSetLinkage(th, LLVMInternalLinkage);
    refl_list_point_t pt = refl_list_save(g);
    g->current_fn = th;
    LLVMPositionBuilderAtEnd(
        g->builder, LLVMAppendBasicBlockInContext(g->ctx, th, "entry"));
    di_clear(g);
    LLVMValueRef raw = zan_call2(
        g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i64, i64, i8ptr}, 3, 0),
        g->rt_alloc,
        (LLVMValueRef[]){LLVMSizeOf(g->list_struct_type), arc_site_arg(g, site),
                         LLVMConstNull(i8ptr)},
        3, "refl.list");
    LLVMValueRef list = LLVMBuildBitCast(
        g->builder, raw, LLVMPointerType(g->list_struct_type, 0), "refl.lp");
    LLVMBuildStore(g->builder, LLVMConstNull(g->list_struct_type), list);
    LLVMBuildStore(g->builder, refl_to_slot(g, raw), LLVMGetParam(th, 2));
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));
    refl_list_restore(g, pt);
    thunk = LLVMConstBitCast(th, i8ptr);
  }

  return LLVMConstNamedStruct(
      refl_method_type(g),
      (LLVMValueRef[]){
          refl_const_string(g, ".ctor", 5), refl_field_type_name(g, t),
          LLVMConstInt(i64, ZAN_REFL_FK_REF, 0), LLVMConstInt(i64, 0, 0),
          LLVMConstNull(i8ptr), thunk, LLVMConstInt(i64, 0, 0),
          refl_empty_string(g), LLVMConstNull(i8ptr)},
      9);
}

static LLVMValueRef refl_emit_ctor_builtin_list(zan_irgen_t *g, zan_type_t *t) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMValueRef rec = refl_builtin_list_ctor_record(g, t, false);
  char nm[96];
  snprintf(nm, sizeof(nm), "__zan.refl.ctor.List.%d", g->refl_thunk_count++);
  LLVMTypeRef arr_ty = LLVMArrayType(refl_method_type(g), 1);
  LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
  LLVMSetInitializer(gv, LLVMConstArray(refl_method_type(g), &rec, 1));
  LLVMSetLinkage(gv, LLVMPrivateLinkage);
  LLVMSetGlobalConstant(gv, 1);
  refl_register_mtab(g, gv, arr_ty, NULL, NULL, t, 1, true);
  return LLVMConstBitCast(gv, i8ptr);
}

static LLVMValueRef refl_make_list_add_thunk(zan_irgen_t *g,
                                             zan_type_t *elem_type) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  if (!refl_list_supported_element(g, elem_type))
    return LLVMConstNull(i8ptr);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
  char nm[96];
  snprintf(nm, sizeof(nm), "__zan.refl.thunk.list_add.%d",
           g->refl_thunk_count++);
  LLVMValueRef th = LLVMAddFunction(
      g->mod, nm,
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i64ptr, i64ptr}, 3, 0));
  LLVMSetLinkage(th, LLVMInternalLinkage);
  refl_list_point_t pt = refl_list_save(g);
  g->current_fn = th;
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, th, "entry");
  LLVMBasicBlockRef grow = LLVMAppendBasicBlockInContext(g->ctx, th, "grow");
  LLVMBasicBlockRef add = LLVMAppendBasicBlockInContext(g->ctx, th, "add");
  LLVMBasicBlockRef fail_bb = LLVMAppendBasicBlockInContext(g->ctx, th, "fail");
  LLVMPositionBuilderAtEnd(g->builder, entry);
  di_clear(g);
  LLVMValueRef arg_slot =
      LLVMBuildLoad2(g->builder, i64, LLVMGetParam(th, 1), "refl.arg");
  LLVMValueRef vk_off = LLVMConstInt(i64, ZAN_REFL_ARG_SLOTS, 0);
  LLVMValueRef vkp = LLVMBuildGEP2(g->builder, i64, LLVMGetParam(th, 1),
                                   &vk_off, 1, "refl.vkp");
  LLVMValueRef vkind = LLVMBuildLoad2(g->builder, i64, vkp, "refl.vk");
  bool ok = true;
  LLVMValueRef value = refl_unpack_arg(
      g, th, elem_type, map_type(g, elem_type), arg_slot, vkind, fail_bb, &ok);
  if (!ok) {
    LLVMDeleteFunction(th);
    refl_list_restore(g, pt);
    return LLVMConstNull(i8ptr);
  }
  LLVMValueRef lp =
      LLVMBuildBitCast(g->builder, LLVMGetParam(th, 0),
                       LLVMPointerType(g->list_struct_type, 0), "refl.lp");
  /* 先过宽度守卫（反射新建的表头 esz=0，首次 Add 时空表直接改标签） */
  (void)list_data_for(g, lp, elem_type, "refl.d0");
  LLVMValueRef cntp =
      LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "refl.cntp");
  LLVMValueRef capp =
      LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 1, "refl.capp");
  LLVMValueRef datap =
      LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "refl.datap");
  LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "refl.cnt");
  LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, capp, "refl.cap");
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntUGE, cnt, cap, "refl.grow"),
                  grow, add);
  LLVMPositionBuilderAtEnd(g->builder, grow);
  LLVMValueRef newcap = LLVMBuildSelect(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntEQ, cap, LLVMConstInt(i64, 0, 0),
                    "refl.empty"),
      LLVMConstInt(i64, 8, 0),
      LLVMBuildMul(g->builder, cap, LLVMConstInt(i64, 2, 0), "refl.double"),
      "refl.newcap");
  LLVMValueRef olddata =
      LLVMBuildLoad2(g->builder, i64ptr, datap, "refl.olddata");
  LLVMValueRef data = zan_call2(
      g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i64}, 2, 0),
      g->fn_realloc,
      (LLVMValueRef[]){
          LLVMBuildBitCast(g->builder, olddata, i8ptr, "refl.oldraw"),
          LLVMBuildMul(g->builder, newcap,
                       LLVMConstInt(i64, (unsigned long long)list_elem_bytes(g, elem_type), 0),
                       "refl.size")},
      2, "refl.data");
  zan_irgen_emit_oom_check(g, th, data);
  LLVMBuildStore(g->builder,
                 LLVMBuildBitCast(g->builder, data, i64ptr, "refl.newdata"),
                 datap);
  LLVMBuildStore(g->builder, newcap, capp);
  LLVMBuildBr(g->builder, add);
  LLVMPositionBuilderAtEnd(g->builder, add);
  LLVMValueRef buffer =
      LLVMBuildLoad2(g->builder, i64ptr, datap, "refl.buffer");
  LLVMValueRef slotp = list_elem_ptr(g, buffer, cnt, elem_type, "refl.slotp");
  /* Invoke borrows arguments. The normal typed store retains references,
   * including nested collections, and preserves null as an occupied slot. */
  emit_collection_slot_store(g, elem_type, list_slot_type(g, elem_type), slotp,
                             value, NULL, NULL, 0);
  LLVMBuildStore(
      g->builder,
      LLVMBuildAdd(g->builder, cnt, LLVMConstInt(i64, 1, 0), "refl.next"),
      cntp);
  LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), LLVMGetParam(th, 2));
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));

  LLVMPositionBuilderAtEnd(g->builder, fail_bb);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));
  refl_list_restore(g, pt);
  return LLVMConstBitCast(th, i8ptr);
}

static LLVMValueRef refl_make_list_count_thunk(zan_irgen_t *g) {
  if (g->fn_list_count_thunk)
    return g->fn_list_count_thunk;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);

  LLVMTypeRef th_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i64ptr, i64ptr}, 3, 0);
  LLVMValueRef th =
      LLVMAddFunction(g->mod, "__zan.refl.thunk.list_count", th_ty);
  LLVMSetLinkage(th, LLVMInternalLinkage);
  g->fn_list_count_thunk = LLVMConstBitCast(th, i8ptr);

  refl_list_point_t pt = refl_list_save(g);
  g->current_fn = th;
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, th, "entry");
  LLVMPositionBuilderAtEnd(g->builder, entry);
  di_clear(g);

  LLVMValueRef self = LLVMGetParam(th, 0);
  LLVMValueRef ret_slot = LLVMGetParam(th, 2);

  LLVMValueRef lp = LLVMBuildBitCast(
      g->builder, self, LLVMPointerType(g->list_struct_type, 0), "refl.lp");
  LLVMValueRef cntp =
      LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "refl.cntp");
  LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "refl.cnt");

  LLVMBuildStore(g->builder, cnt, ret_slot);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));

  refl_list_restore(g, pt);
  return g->fn_list_count_thunk;
}

static LLVMValueRef refl_make_list_get_thunk(zan_irgen_t *g,
                                             zan_type_t *elem_type) {
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  if (!refl_list_supported_element(g, elem_type))
    return LLVMConstNull(i8ptr);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
  LLVMTypeRef vd = LLVMVoidTypeInContext(g->ctx);

  char nm[128];
  snprintf(nm, sizeof(nm), "__zan.refl.thunk.list_get.%d",
           g->refl_thunk_count++);
  LLVMTypeRef th_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i64ptr, i64ptr}, 3, 0);
  LLVMValueRef th = LLVMAddFunction(g->mod, nm, th_ty);
  LLVMSetLinkage(th, LLVMInternalLinkage);

  refl_list_point_t pt = refl_list_save(g);
  g->current_fn = th;
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, th, "entry");
  LLVMBasicBlockRef in_b =
      LLVMAppendBasicBlockInContext(g->ctx, th, "in_bounds");
  LLVMBasicBlockRef out_b =
      LLVMAppendBasicBlockInContext(g->ctx, th, "out_bounds");

  LLVMPositionBuilderAtEnd(g->builder, entry);
  di_clear(g);
  LLVMValueRef self = LLVMGetParam(th, 0);
  LLVMValueRef args = LLVMGetParam(th, 1);
  LLVMValueRef ret_slot = LLVMGetParam(th, 2);

  LLVMValueRef idx = LLVMBuildLoad2(g->builder, i64, args, "refl.idx");

  LLVMValueRef lp = LLVMBuildBitCast(
      g->builder, self, LLVMPointerType(g->list_struct_type, 0), "refl.lp");
  LLVMValueRef cntp =
      LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "refl.cntp");
  LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "refl.cnt");

  LLVMValueRef c1 = LLVMBuildICmp(g->builder, LLVMIntSGE, idx,
                                  LLVMConstInt(i64, 0, 0), "refl.c1");
  LLVMValueRef c2 = LLVMBuildICmp(g->builder, LLVMIntSLT, idx, cnt, "refl.c2");
  LLVMValueRef ok = LLVMBuildAnd(g->builder, c1, c2, "refl.ok");
  LLVMBuildCondBr(g->builder, ok, in_b, out_b);

  LLVMPositionBuilderAtEnd(g->builder, out_b);
  LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), ret_slot);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

  LLVMPositionBuilderAtEnd(g->builder, in_b);
  LLVMValueRef data = list_data_for(g, lp, elem_type, "refl.data");
  LLVMValueRef ep = list_elem_ptr(g, data, idx, elem_type, "refl.ep");
  LLVMTypeRef elem_llvm = map_type(g, elem_type);
  LLVMTypeKind kind = LLVMGetTypeKind(elem_llvm);

  if (kind == LLVMStructTypeKind) {
    LLVMValueRef sv = load_struct_from_slot(g, ep, elem_llvm);
    LLVMValueRef result = refl_to_slot_typed(g, elem_type, sv, false);
    LLVMBuildStore(g->builder, result, ret_slot);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));
    refl_list_restore(g, pt);
    return LLVMConstBitCast(th, i8ptr);
  }

  LLVMValueRef raw = list_slot_load_word(g, ep, elem_type, "refl.raw");

  bool is_ref =
      elem_type &&
      (elem_type->kind == TYPE_CLASS || elem_type->kind == TYPE_STRING ||
       elem_type->kind == TYPE_ARRAY || elem_type->kind == TYPE_INTERFACE ||
       elem_type->kind == TYPE_OBJECT || elem_type->kind == TYPE_DELEGATE ||
       elem_type->kind == TYPE_TASK);

  if (is_ref) {
    LLVMBasicBlockRef do_ret =
        LLVMAppendBasicBlockInContext(g->ctx, th, "do_retain");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, th, "done");
    LLVMValueRef nz = LLVMBuildICmp(g->builder, LLVMIntNE, raw,
                                    LLVMConstInt(i64, 0, 0), "refl.nz");
    LLVMBuildCondBr(g->builder, nz, do_ret, done);

    LLVMPositionBuilderAtEnd(g->builder, do_ret);
    LLVMValueRef obj_ptr = LLVMBuildIntToPtr(g->builder, raw, i8ptr, "refl.op");
    LLVMTypeRef arc_ty = LLVMFunctionType(vd, (LLVMTypeRef[]){i8ptr}, 1, 0);
    zan_call2(g->builder, arc_ty, g->rt_retain, &obj_ptr, 1, "");
    LLVMBuildBr(g->builder, done);

    LLVMPositionBuilderAtEnd(g->builder, done);
  }

  LLVMValueRef result = raw;
  if (kind == LLVMFloatTypeKind) {
    LLVMValueRef bits = LLVMBuildTrunc(
        g->builder, raw, LLVMInt32TypeInContext(g->ctx), "refl.fbits");
    result = refl_to_slot(
        g, LLVMBuildBitCast(g->builder, bits, elem_llvm, "refl.float"));
  } else if (kind == LLVMIntegerTypeKind &&
             LLVMGetIntTypeWidth(elem_llvm) < 64) {
    LLVMValueRef value =
        LLVMBuildTrunc(g->builder, raw, elem_llvm, "refl.narrow");
    result = extend_int_for_slot(g, value, elem_type, i64);
  }
  LLVMBuildStore(g->builder, result, ret_slot);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));

  refl_list_restore(g, pt);
  return LLVMConstBitCast(th, i8ptr);
}

static LLVMValueRef
refl_emit_methods_builtin_list(zan_irgen_t *g, zan_type_t *t, int *out_count) {
  *out_count = 0;
  if (!t)
    return NULL;
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef mrec = refl_method_type(g);

  zan_type_t *elem_type = (t->type_arg_count > 0 && t->type_args)
                              ? t->type_args[0]
                              : g->binder->type_object;

  LLVMValueRef count_thunk = refl_make_list_count_thunk(g);
  LLVMValueRef get_thunk = refl_make_list_get_thunk(g, elem_type);
  LLVMValueRef add_thunk = refl_make_list_add_thunk(g, elem_type);

  /* 参数名 index */
  LLVMValueRef pnames[1] = {refl_const_string(g, "index", 5)};
  LLVMTypeRef pat = LLVMArrayType(i8ptr, 1);
  char pnm[64];
  snprintf(pnm, sizeof(pnm), "__zan.refl.pnames.list.%d", g->refl_str_count++);
  LLVMValueRef pgv = LLVMAddGlobal(g->mod, pat, pnm);
  LLVMSetInitializer(pgv, LLVMConstArray(i8ptr, pnames, 1));
  LLVMSetLinkage(pgv, LLVMPrivateLinkage);
  LLVMSetGlobalConstant(pgv, 1);
  LLVMValueRef pidx[2] = {LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0)};
  LLVMValueRef param_names_ptr = LLVMConstGEP2(pat, pgv, pidx, 2);

  LLVMValueRef recs[4];
  /* 0: Count() -> int */
  recs[0] = LLVMConstNamedStruct(
      mrec,
      (LLVMValueRef[]){
          refl_const_string(g, "Count", 5),
          refl_field_type_name(g, g->binder->type_int),
          LLVMConstInt(
              i64, (unsigned long long)refl_ret_kind(g->binder->type_int, false),
              0),
          LLVMConstInt(i64, 0, 0), LLVMConstNull(i8ptr), count_thunk,
          LLVMConstInt(i64, 0, 0), refl_empty_string(g), LLVMConstNull(i8ptr)},
      9);

  /* 1: Get(int index) -> elem_type */
  recs[1] = LLVMConstNamedStruct(
      mrec,
      (LLVMValueRef[]){
          refl_const_string(g, "Get", 3), refl_field_type_name(g, elem_type),
          LLVMConstInt(i64, (unsigned long long)refl_ret_kind(elem_type, false),
                       0),
          LLVMConstInt(i64, 1, 0), param_names_ptr, get_thunk,
          LLVMConstInt(i64, 0, 0), refl_empty_string(g), LLVMConstNull(i8ptr)},
      9);

  /* 2: At(int index) -> elem_type */
  recs[2] = LLVMConstNamedStruct(
      mrec,
      (LLVMValueRef[]){
          refl_const_string(g, "At", 2), refl_field_type_name(g, elem_type),
          LLVMConstInt(i64, (unsigned long long)refl_ret_kind(elem_type, false),
                       0),
          LLVMConstInt(i64, 1, 0), param_names_ptr, get_thunk,
          LLVMConstInt(i64, 0, 0), refl_empty_string(g), LLVMConstNull(i8ptr)},
      9);

  LLVMValueRef value_name = refl_field_type_name(g, elem_type);
  snprintf(pnm, sizeof(pnm), "__zan.refl.pnames.list.%d", g->refl_str_count++);
  LLVMValueRef vgv = LLVMAddGlobal(g->mod, pat, pnm);
  LLVMSetInitializer(vgv, LLVMConstArray(i8ptr, &value_name, 1));
  LLVMSetLinkage(vgv, LLVMPrivateLinkage);
  LLVMSetGlobalConstant(vgv, 1);
  recs[3] = LLVMConstNamedStruct(
      mrec,
      (LLVMValueRef[]){
          refl_const_string(g, "Add", 3),
          refl_field_type_name(g, g->binder->type_void),
          LLVMConstInt(
              i64, (unsigned long long)refl_field_kind(g->binder->type_void),
              0),
          LLVMConstInt(i64, 1, 0), LLVMConstGEP2(pat, vgv, pidx, 2), add_thunk,
          LLVMConstInt(i64, 0, 0), refl_empty_string(g), LLVMConstNull(i8ptr)},
      9);

  LLVMTypeRef arr_ty = LLVMArrayType(mrec, 4);
  char nm[96];
  snprintf(nm, sizeof(nm), "__zan.refl.mtab.List.%d", g->refl_meta_count);
  LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
  LLVMSetInitializer(gv, LLVMConstArray(mrec, recs, 4));
  LLVMSetLinkage(gv, LLVMPrivateLinkage);
  LLVMSetGlobalConstant(gv, 1);
  *out_count = 4;
  return LLVMConstBitCast(gv, i8ptr);
}

static LLVMValueRef refl_mtab_init(zan_irgen_t *g, zan_symbol_t *sym,
                                   zan_type_t *inst, bool ctors, int n,
                                   bool late) {
  LLVMTypeRef mrec = refl_method_type(g);
  LLVMValueRef *recs = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
  if (!recs)
    return NULL;
  if (ctors) {
    zan_ast_node_t **decls =
        (zan_ast_node_t **)calloc((size_t)n, sizeof(zan_ast_node_t *));
    int got = decls ? refl_collect_ctors(sym, decls, n) : 0;
    for (int i = 0; i < n; i++)
      recs[i] = i < got
                    ? refl_ctor_record(g, sym, inst, decls[i], late)
                    : (got == 0 && i == 0
                           ? refl_ctor_record(g, sym, inst, NULL, late)
                           : LLVMConstNull(mrec));
    free(decls);
  } else {
    zan_symbol_t **msyms =
        (zan_symbol_t **)calloc((size_t)n, sizeof(zan_symbol_t *));
    int got = msyms ? refl_collect_methods(g, sym, msyms, n) : 0;
    if (got > n)
      got = n;
    for (int i = 0; i < n; i++)
      recs[i] = i < got ? refl_method_record(g, sym, inst, msyms[i], late)
                        : LLVMConstNull(mrec);
    free(msyms);
  }
  LLVMValueRef init = LLVMConstArray(mrec, recs, (unsigned)n);
  free(recs);
  return init;
}

static LLVMValueRef refl_shape_mtab(zan_irgen_t *g, zan_symbol_t *sym,
                                    zan_type_t *inst, bool ctors,
                                    int *out_count) {
  *out_count = 0;
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  if (!sym)
    return LLVMConstNull(i8ptr);
  int n = 0;
  if (ctors) {
    if (!sym->decl)
      return LLVMConstNull(i8ptr);
    zan_ast_list_t *ms = &sym->decl->type_decl.members;
    for (int i = 0; i < ms->count; i++)
      if (ms->items[i]->kind == AST_CONSTRUCTOR_DECL &&
          !(ms->items[i]->method_decl.modifiers & MOD_STATIC))
        n++;
    if (n == 0 && sym->type &&
        (sym->type->kind == TYPE_CLASS || sym->type->kind == TYPE_STRUCT))
      n = 1;
  } else {
    n = refl_methods_walk(g, sym, NULL, 0);
  }
  if (n <= 0)
    return LLVMConstNull(i8ptr);

  LLVMTypeRef arr_ty = LLVMArrayType(refl_method_type(g), (unsigned)n);
  char nm[128];
  snprintf(nm, sizeof(nm), "__zan.refl.%s.%.*s.%d", ctors ? "ctors" : "mtab",
           (int)sym->name.len, sym->name.str, g->refl_mtab_count);
  LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
  LLVMSetInitializer(gv, LLVMConstNull(arr_ty));
  LLVMSetLinkage(gv, LLVMPrivateLinkage);
  LLVMSetAlignment(gv, 8);

  refl_register_mtab(g, gv, arr_ty, sym, inst, NULL, n, ctors);
  /* Constructor discovery metadata is valid even when no callable demand
   * ever arrives. Its initializer is upgraded in the pre-freeze fixed point. */
  if (ctors) {
    LLVMValueRef init = refl_mtab_init(g, sym, inst, true, n, false);
    if (init)
      LLVMSetInitializer(gv, init);
  }
  *out_count = n;
  return LLVMConstBitCast(gv,
                          LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
}

static void refl_pack_val_str(zan_ast_node_t *r, char *buf, size_t cap) {
  if (!r || cap <= 1) {
    if (cap > 0)
      buf[0] = 0;
    return;
  }
  if (r->kind == AST_STRING_LITERAL) {
    snprintf(buf, cap, "%s", r->str_val.str ? r->str_val.str : "");
  } else if (r->kind == AST_BOOL_LITERAL) {
    snprintf(buf, cap, "%s", r->bool_val ? "true" : "false");
  } else if (r->kind == AST_INT_LITERAL) {
    snprintf(buf, cap, "%lld", (long long)r->int_val);
  } else if (r->kind == AST_MEMBER_ACCESS && r->member.name.str) {
    if (r->member.object && r->member.object->kind == AST_IDENTIFIER &&
        r->member.object->ident.name.str) {
      snprintf(buf, cap, "%.*s.%.*s", (int)r->member.object->ident.name.len,
               r->member.object->ident.name.str, (int)r->member.name.len,
               r->member.name.str);
    } else {
      snprintf(buf, cap, "%.*s", (int)r->member.name.len, r->member.name.str);
    }
  } else if (r->kind == AST_IDENTIFIER && r->ident.name.str) {
    snprintf(buf, cap, "%.*s", (int)r->ident.name.len, r->ident.name.str);
  } else if (r->kind == AST_BINARY && r->binary.left && r->binary.right) {
    char left[128], right[128];
    refl_pack_val_str(r->binary.left, left, sizeof(left));
    refl_pack_val_str(r->binary.right, right, sizeof(right));
    const char *op = "|";
    if (r->binary.op == TK_PIPE)
      op = "|";
    else if (r->binary.op == TK_AMP)
      op = "&";
    else if (r->binary.op == TK_PLUS)
      op = "+";
    snprintf(buf, cap, "%s%s%s", left, op, right);
  } else {
    buf[0] = 0;
  }
}
