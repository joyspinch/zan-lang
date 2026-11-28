case AST_RETURN_STMT:
if (stmt->ret.value) {
  zan_type_t *rty = infer_expr_type(g, stmt->ret.value, locals);
  check_implicit_narrowing(g, g->current_fn_zan_ret_type, rty, stmt->ret.value,
                           "return");
  /* lambda 体里的 return：委托返回类型与返回值类型必须兼容。
   * 普通方法由 checker 负责，这里只补 lambda（checker 看不到
   * lambda 的委托返回类型）。 */
  if (g->lambda_depth > 0)
    check_no_boxing_conversion(g, g->current_fn_zan_ret_type, rty,
                               stmt->ret.value, "lambda return");
}
if (g->current_async_frame) {
  LLVMValueRef ri = NULL, rv = NULL;
  zan_type_t *ret_type = g->current_async_ret_type;
  if (stmt->ret.value) {
    rv = emit_expr(g, stmt->ret.value, locals);
    zan_type_t *value_type =
        concretize(g, infer_expr_type(g, stmt->ret.value, locals));
    if (!ret_type)
      ret_type = value_type;
    /* A556-1 隐式装箱：object/接口异步返回类型收值形状——箱是
     * fresh owned，跳过来源 retain（ARC 移交语义）。 */
    LLVMValueRef boxed = g->current_async_ret_type
                             ? emit_implicit_box(g, g->current_async_ret_type,
                                                 rv, stmt->ret.value, locals)
                             : rv;
    if (boxed != rv) {
      rv = boxed;
      ret_type = g->current_async_ret_type;
    } else if (type_contains_collection_rc(g, value_type, 0) &&
               !expr_yields_owned_rc_value(g, stmt->ret.value, locals)) {
      emit_collection_value_retain(g, value_type, rv, 0);
    } else if (value_type &&
               (is_rc_managed_type(value_type) ||
                value_type->kind == TYPE_OBJECT) &&
               !expr_yields_owned_rc_value(g, stmt->ret.value, locals)) {
      if (is_rc_managed_type(value_type)) {
        emit_rc_retain_for_type(g, value_type, rv);
      } else if (LLVMGetTypeKind(LLVMTypeOf(rv)) == LLVMPointerTypeKind) {
        emit_arc_retain(g, rv);
      }
    }
    rv = coerce_async_ret(g, rv);
  }
  if (g->finally_count > 0) {
    if (rv)
      emit_async_pending_return(g, rv);
    emit_pending_finallys(g, locals, 0, rv != NULL);
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
      break;
    if (rv)
      ri = emit_async_pending_take_return(g);
  } else if (rv) {
    ri = coerce_to_frame_result(g, rv, ret_type);
  }
  emit_release_active_catch_excs(g, 0);
  emit_async_complete(g, locals, ri);
  break;
}
if (stmt->ret.value && (g->current_fn_is_ref_return || stmt->ret.is_ref)) {
  LLVMValueRef ref_ptr = emit_ref_lvalue_ptr(g, stmt->ret.value, locals);
  LLVMTypeRef fn_ret = g->current_fn_ret_type;
  if (!ref_ptr)
    ref_ptr = LLVMConstNull(fn_ret);
  else if (LLVMTypeOf(ref_ptr) != fn_ret)
    ref_ptr = LLVMBuildBitCast(g->builder, ref_ptr, fn_ret, "retref.bc");
  if (g->finally_count > 0) {
    LLVMValueRef p_slot = emit_entry_alloca(g, fn_ret, "retref.fin.slot");
    LLVMBuildStore(g->builder, ref_ptr, p_slot);
    emit_pending_finallys(g, locals, 0, false);
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
      break;
    ref_ptr = LLVMBuildLoad2(g->builder, fn_ret, p_slot, "retref.fin");
  }
  emit_release_active_catch_excs(g, 0);
  emit_release_owned_locals(g, locals);
  LLVMBuildRet(g->builder, ref_ptr);
  break;
}
if (stmt->ret.value) {
  LLVMValueRef val = emit_expr(g, stmt->ret.value, locals);
  bool conv_ret = false;
  if (g->current_fn_zan_ret_type) {
    zan_type_t *vty = infer_expr_type(g, stmt->ret.value, locals);
    if (find_user_conversion(g, vty, g->current_fn_zan_ret_type,
                             "op_implicit")) {
      val = emit_user_conversion(g, vty, g->current_fn_zan_ret_type,
                                 "op_implicit", val, stmt->ret.value, locals);
      conv_ret = true;
    }
  }
  /* A556-1 隐式装箱：结构体值流向 object/接口返回类型——箱是
   * fresh owned 堆对象，按 conv_ret 交 ARC 移交契约（不再对
   * 返回值 retain）。 */
  if (g->current_fn_zan_ret_type) {
    LLVMValueRef boxed = emit_implicit_box(g, g->current_fn_zan_ret_type, val,
                                           stmt->ret.value, locals);
    if (boxed != val) {
      val = boxed;
      conv_ret = true;
    }
  }
  /* ARC 返回契约：向调用方移交 +1 强引用所有权，随后释放本作用域局部变量 */
  zan_type_t *ret_type =
      concretize(g, infer_expr_type(g, stmt->ret.value, locals));
  if (conv_ret)
    ret_type = g->current_fn_zan_ret_type;
  if (!ret_type && g->current_fn_zan_ret_type)
    ret_type = g->current_fn_zan_ret_type;
  if (is_rc_managed_type(ret_type) &&
      !expr_yields_owned_rc_value(g, stmt->ret.value, locals) && !conv_ret) {
    emit_rc_retain_for_type(g, ret_type, val);
  } else if (ret_type && ret_type->kind == TYPE_OBJECT &&
             LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind &&
             !expr_yields_owned_rc_value(g, stmt->ret.value, locals) &&
             !conv_ret) {
    emit_arc_retain(g, val);
  } else if (ret_type && ret_type->kind == TYPE_STRUCT &&
             type_contains_collection_rc(g, ret_type, 0) &&
             !expr_yields_owned_rc_value(g, stmt->ret.value, locals) &&
             !conv_ret &&
             !(stmt->ret.value->kind == AST_IDENTIFIER &&
               local_find(locals, stmt->ret.value->ident.name) &&
               local_find(locals, stmt->ret.value->ident.name)->drop_flag)) {
    emit_collection_value_retain(g, ret_type, val, 0);
  }
  if (g->finally_count > 0) {
    LLVMValueRef v_slot = emit_entry_alloca(g, LLVMTypeOf(val), "ret.fin.slot");
    zan_store_fit(g, val, v_slot);
    /* The pending +1 belongs to this return until every finally
     * completes. A replacement return or escaping exception must
     * release it just like an owned local. Inner catches, entered
     * after registration, must leave it alive. */
    int pending_base = locals->count;
    bool pending_owned = is_rc_managed_type(ret_type) ||
                         (ret_type && ret_type->kind == TYPE_OBJECT) ||
                         type_contains_collection_rc(g, ret_type, 0);
    if (pending_owned) {
      local_add(locals, (zan_istr_t){"", 0}, v_slot, ret_type);
      locals->vars[locals->count - 1].struct_rc =
          ret_type && ret_type->kind == TYPE_STRUCT;
      arc_own_local(g, locals);
    }
    emit_pending_finallys(g, locals, 0, false);
    locals->count = pending_base;
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
      break;
    val = LLVMBuildLoad2(g->builder, LLVMTypeOf(val), v_slot, "ret.fin");
    /* Successful completion transfers ownership to the caller. */
    LLVMBuildStore(g->builder, LLVMConstNull(LLVMTypeOf(val)), v_slot);
  }
  emit_move_clear_drop_flag(g, stmt->ret.value, locals);
  /* 退出顺序：先释放 catch 捕获的异常对象，再释放局部变量。
   * 被捕获的异常还压在异常临时栈顶（catch 进入时压入），若先弹局部
   * 变量的 eh_slot，弹出的是异常而不是该局部变量的槽位标记——标记滞留
   * 栈上，栈深与标记错位；后续展开会拿滞留标记再释放一次同一对象，
   * 变成对已释放内存的二次释放（访问冲突）。 */
  emit_release_active_catch_excs(g, 0);
  emit_release_owned_locals(g, locals);
  LLVMTypeRef fn_ret = g->current_fn_ret_type;
  LLVMTypeRef val_t = LLVMTypeOf(val);
  if (val_t != fn_ret) {
    if (llvm_is_nullable(fn_ret)) {
      val = coerce_int_to(g, val, fn_ret);
    } else if (LLVMGetTypeKind(fn_ret) == LLVMFloatTypeKind &&
               LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
      val = LLVMBuildFPTrunc(g->builder, val, fn_ret, "rettrunc");
    } else if (LLVMGetTypeKind(fn_ret) == LLVMDoubleTypeKind &&
               LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
      val = LLVMBuildFPExt(g->builder, val, fn_ret, "retext");
    } else if ((LLVMGetTypeKind(fn_ret) == LLVMDoubleTypeKind ||
                LLVMGetTypeKind(fn_ret) == LLVMFloatTypeKind) &&
               LLVMGetTypeKind(val_t) == LLVMIntegerTypeKind &&
               LLVMGetIntTypeWidth(val_t) > 1) {
      val = LLVMBuildSIToFP(g->builder, val, fn_ret, "retsitofp");
    } else if (LLVMGetTypeKind(fn_ret) == LLVMIntegerTypeKind &&
               LLVMGetTypeKind(val_t) == LLVMIntegerTypeKind) {
      unsigned fn_bits = LLVMGetIntTypeWidth(fn_ret);
      unsigned val_bits = LLVMGetIntTypeWidth(val_t);
      if (fn_bits > val_bits) {
        val = val_bits <= 8 ? LLVMBuildZExt(g->builder, val, fn_ret, "retzext")
                            : LLVMBuildSExt(g->builder, val, fn_ret, "retext");
      } else if (fn_bits < val_bits) {
        val = LLVMBuildTrunc(g->builder, val, fn_ret, "rettrunc");
      }
    } else if (fn_ret == get_decimal_type(g) &&
               LLVMGetTypeKind(val_t) == LLVMIntegerTypeKind) {
      /* int 字面量/变量 → decimal 返回值：隐式转换走运行时
       * 构造，漏掉就是 LLVM 校验失败（ret i64 7 对 %Decimal
       * 返回型，A548）。 */
      val = coerce_to_decimal(g, val);
    } else if (LLVMGetTypeKind(fn_ret) == LLVMStructTypeKind &&
               LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
      val = LLVMBuildLoad2(g->builder, fn_ret, val, "ret.struct");
    } else if (LLVMGetTypeKind(fn_ret) == LLVMPointerTypeKind &&
               LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
      /* e */
      val = LLVMBuildBitCast(g->builder, val, fn_ret, "retcast");
    } else if (LLVMGetTypeKind(fn_ret) == LLVMPointerTypeKind ||
               LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
      val = emit_boundary_coerce(g, val, fn_ret);
    }
  }
  if (g->current_fn_is_main)
    emit_release_static_rc_fields(g, NULL);
  emit_eh_disarm_from(g, 0);
  LLVMBuildRet(g->builder, val);
} else {
  emit_pending_finallys(g, locals, 0, false);
  if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
    break;
  /* 同上：catch 异常先出栈，局部变量后出栈 */
  emit_release_active_catch_excs(g, 0);
  emit_release_owned_locals(g, locals);
  LLVMTypeRef fn_ret = g->current_fn_ret_type;
  if (g->current_fn_is_main)
    emit_release_static_rc_fields(g, NULL);
  emit_eh_disarm_from(g, 0);
  if (fn_ret && LLVMGetTypeKind(fn_ret) != LLVMVoidTypeKind) {
    LLVMBuildRet(g->builder, LLVMConstNull(fn_ret));
  } else {
    LLVMBuildRetVoid(g->builder);
  }
}
break;

case AST_IF_STMT: {
  int condition_start = locals->count;
  LLVMValueRef cond = emit_expr(g, stmt->if_stmt.cond, locals);
  cond = zan_tobool(g->builder, cond, "tobool");
  int body_start = locals->count;
  int condition_count = body_start - condition_start;
  /* Both branches start with the original condition bindings. */
  local_var_t *condition_vars = NULL;
  if (condition_count > 0) {
    condition_vars = zan_arena_alloc(g->arena, sizeof(local_var_t) *
                                                   (size_t)condition_count);
    memcpy(condition_vars, locals->vars + condition_start,
           sizeof(local_var_t) * (size_t)condition_count);
  }

  LLVMBasicBlockRef then_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "then");
  LLVMBasicBlockRef else_bb = NULL;
  if (stmt->if_stmt.else_body || condition_count > 0)
    else_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "else");
  LLVMBasicBlockRef merge_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "merge");
  LLVMBuildCondBr(g->builder, cond, then_bb, else_bb ? else_bb : merge_bb);

  LLVMBasicBlockRef branches[] = {then_bb, else_bb};
  zan_ast_node_t *bodies[] = {stmt->if_stmt.then_body, stmt->if_stmt.else_body};
  for (int branch = 0; branch < 2 && branches[branch]; branch++) {
    if (branch == 1 && condition_count > 0) {
      memcpy(locals->vars + condition_start, condition_vars,
             sizeof(local_var_t) * (size_t)condition_count);
      locals->count = body_start;
    }
    LLVMPositionBuilderAtEnd(g->builder, branches[branch]);
    emit_stmt(g, bodies[branch], locals);
    emit_release_owned_locals_from(g, locals, body_start);
    while (locals->count > condition_start) {
      int i = locals->count - 1;
      zan_ast_node_t *decl = locals->vars[i].binding_decl;
      /* The enclosing block owns inline out bindings. */
      if (decl && decl->kind == AST_REF_ARG)
        locals->count = i;
      else
        emit_release_owned_locals_from(g, locals, i);
    }
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
      LLVMBuildBr(g->builder, merge_bb);
  }

  LLVMPositionBuilderAtEnd(g->builder, merge_bb);
  for (int i = 0; i < condition_count; i++) {
    zan_ast_node_t *decl = condition_vars[i].binding_decl;
    if (decl && decl->kind == AST_REF_ARG)
      locals->vars[locals->count++] = condition_vars[i];
  }
  break;
}

case AST_WHILE_STMT: {
  int body_start = locals->count;
  LLVMBasicBlockRef cond_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.cond");
  LLVMBasicBlockRef body_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.body");
  LLVMBasicBlockRef end_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.end");

  zan_irgen_pending_context_t saved_pending = g->pending;
  LLVMBasicBlockRef saved_break = g->break_target;
  LLVMBasicBlockRef saved_cont = g->continue_target;
  int saved_loop_base = g->loop_locals_base;
  int saved_break_base = g->break_locals_base;
  int saved_loop_cbase = g->loop_catch_base;
  int saved_loop_fbase = g->finally_loop_base;
  g->pending.break_scope = g->pending.scope;
  g->pending.continue_scope = g->pending.scope;
  g->break_target = end_bb;
  g->continue_target = cond_bb;
  g->loop_locals_base = body_start;
  g->break_locals_base = body_start;
  g->loop_catch_base = g->catch_cleanup_count;
  g->finally_loop_base = g->finally_count;
  int saved_loop_ehbase = g->eh_armed_loop_base;
  g->eh_armed_loop_base = g->eh_armed_count;
  int saved_guarded_lists = g->loop_guarded_list_count;

  emit_loop_preheader_list_guards(g, stmt->while_stmt.cond, NULL,
                                  stmt->while_stmt.body, locals);
  LLVMBuildBr(g->builder, cond_bb);
  LLVMPositionBuilderAtEnd(g->builder, cond_bb);
  LLVMValueRef cond = emit_expr(g, stmt->while_stmt.cond, locals);
  cond = zan_tobool(g->builder, cond, "tobool");
  if (locals->count > body_start) {
    LLVMBasicBlockRef false_bb =
        LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.false");
    LLVMBuildCondBr(g->builder, cond, body_bb, false_bb);
    LLVMPositionBuilderAtEnd(g->builder, false_bb);
    /* Emit this edge before body cleanup truncates condition locals. */
    emit_release_owned_locals_range(g, locals, body_start);
    LLVMBuildBr(g->builder, end_bb);
  } else {
    LLVMBuildCondBr(g->builder, cond, body_bb, end_bb);
  }

  LLVMPositionBuilderAtEnd(g->builder, body_bb);
  emit_stmt(g, stmt->while_stmt.body, locals);
  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
    emit_release_owned_locals_from(g, locals, body_start);
    if (!emit_async_preempt_site(g, cond_bb))
      LLVMBuildBr(g->builder, cond_bb);
  } else {
    emit_release_owned_locals_from(g, locals, body_start);
  }

  g->pending.break_scope = saved_pending.break_scope;
  g->pending.continue_scope = saved_pending.continue_scope;
  g->break_target = saved_break;
  g->continue_target = saved_cont;
  g->loop_locals_base = saved_loop_base;
  g->break_locals_base = saved_break_base;
  g->loop_catch_base = saved_loop_cbase;
  g->finally_loop_base = saved_loop_fbase;
  g->eh_armed_loop_base = saved_loop_ehbase;
  g->loop_guarded_list_count = saved_guarded_lists;

  LLVMPositionBuilderAtEnd(g->builder, end_bb);
  break;
}

case AST_FOR_STMT: {
  int for_start = locals->count;
  if (stmt->for_stmt.init)
    emit_stmt(g, stmt->for_stmt.init, locals);
  /* C# 的 for 头变量是【共享单变量】：闭包捕获共享同一个存储，循环里
   * 的写入对先前创建的闭包可见（TASKS.md A519）。曾给捕获打
   * per_iteration=1（每次迭代快照）——那是 foreach 的 C#5 语义，
   * 用在 for 上让经典"共享循环计数"模式迁移即错。 */
  int for_body_start = locals->count;

  LLVMBasicBlockRef cond_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.cond");
  LLVMBasicBlockRef body_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.body");
  LLVMBasicBlockRef step_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.step");
  LLVMBasicBlockRef end_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.end");

  zan_irgen_pending_context_t saved_pending = g->pending;
  LLVMBasicBlockRef saved_break = g->break_target;
  LLVMBasicBlockRef saved_cont = g->continue_target;
  int saved_loop_base = g->loop_locals_base;
  int saved_break_base = g->break_locals_base;
  int saved_loop_cbase = g->loop_catch_base;
  int saved_loop_fbase = g->finally_loop_base;
  g->pending.break_scope = g->pending.scope;
  g->pending.continue_scope = g->pending.scope;
  g->break_target = end_bb;
  g->continue_target = step_bb;
  g->loop_locals_base = for_body_start;
  g->break_locals_base = for_body_start;
  g->loop_catch_base = g->catch_cleanup_count;
  g->finally_loop_base = g->finally_count;
  int saved_loop_ehbase = g->eh_armed_loop_base;
  g->eh_armed_loop_base = g->eh_armed_count;
  int saved_guarded_lists = g->loop_guarded_list_count;
  int saved_bce_count = g->loop_bce_count;

  emit_loop_preheader_list_guards(g, stmt->for_stmt.cond, stmt->for_stmt.step,
                                  stmt->for_stmt.body, locals);

  bce_for_plan_t bce_plan;
  bce_plan_for_loop(g, stmt, locals, &bce_plan);

  if (bce_plan.can_version) {
    /* Fast-path + Slow-path versioned loop: 0 bounds checks + scoped noalias on fast path,
     * 100% identical checked/overlapping semantics on slow path. */
    LLVMValueRef fast_ok = bce_emit_preheader_fast_cond(g, &bce_plan, locals);
    LLVMBasicBlockRef fast_cond_bb =
        LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.fcond");
    LLVMBasicBlockRef fast_body_bb =
        LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.fbody");
    LLVMBasicBlockRef fast_step_bb =
        LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.fstep");
    LLVMValueRef vbr = LLVMBuildCondBr(g->builder, fast_ok, fast_cond_bb, cond_bb);
    zan_set_branch_weights(g, vbr, 2000, 1);

    /* Push BCE entries + disjoint alias scopes for the fast loop */
    int saved_alias_cnt = g->fn_alias_scope_count;
    for (int bi = 0; bi < bce_plan.ver_count && g->loop_bce_count < 32; bi++) {
      int sid = bce_plan.ver_spans[bi]->noalias_scope_id;
      if (sid <= 0 && bce_plan.ver_count >= 2)
        sid = zan_fn_alloc_alias_scope(g);
      g->loop_bce[g->loop_bce_count].iv_local = bce_plan.iv_local;
      g->loop_bce[g->loop_bce_count].container_local = bce_plan.ver_spans[bi];
      g->loop_bce[g->loop_bce_count].max_offset = bce_plan.ver_max_off[bi];
      g->loop_bce[g->loop_bce_count].alias_scope_id = sid;
      g->loop_bce_count++;
    }
    g->continue_target = fast_step_bb;
    LLVMPositionBuilderAtEnd(g->builder, fast_cond_bb);
    {
      LLVMValueRef cond = emit_expr(g, stmt->for_stmt.cond, locals);
      cond = zan_tobool(g->builder, cond, "tobool");
      LLVMBuildCondBr(g->builder, cond, fast_body_bb, end_bb);
    }
    LLVMPositionBuilderAtEnd(g->builder, fast_body_bb);
    emit_stmt(g, stmt->for_stmt.body, locals);
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
      emit_release_owned_locals_from(g, locals, for_body_start);
      LLVMBuildBr(g->builder, fast_step_bb);
    } else {
      emit_release_owned_locals_from(g, locals, for_body_start);
    }
    LLVMPositionBuilderAtEnd(g->builder, fast_step_bb);
    if (stmt->for_stmt.step) {
      if (stmt->for_stmt.step->kind == AST_BLOCK ||
          stmt->for_stmt.step->kind == AST_EXPR_STMT)
        emit_stmt(g, stmt->for_stmt.step, locals);
      else
        emit_expr(g, stmt->for_stmt.step, locals);
    }
    LLVMBuildBr(g->builder, fast_cond_bb);

    /* Restore BCE and alias scope count before emitting the slow checked fallback loop */
    g->loop_bce_count = saved_bce_count;
    g->fn_alias_scope_count = saved_alias_cnt;
    g->continue_target = step_bb;
    if (bce_plan.static_span && g->loop_bce_count < 32) {
      g->loop_bce[g->loop_bce_count].iv_local = bce_plan.iv_local;
      g->loop_bce[g->loop_bce_count].container_local = bce_plan.static_span;
      g->loop_bce[g->loop_bce_count].max_offset = bce_plan.static_max_off;
      g->loop_bce[g->loop_bce_count].alias_scope_id =
          bce_plan.static_span->noalias_scope_id;
      g->loop_bce_count++;
    }
  } else {
    if (bce_plan.static_span && g->loop_bce_count < 32) {
      g->loop_bce[g->loop_bce_count].iv_local = bce_plan.iv_local;
      g->loop_bce[g->loop_bce_count].container_local = bce_plan.static_span;
      g->loop_bce[g->loop_bce_count].max_offset = bce_plan.static_max_off;
      g->loop_bce[g->loop_bce_count].alias_scope_id =
          bce_plan.static_span->noalias_scope_id;
      g->loop_bce_count++;
    }
    LLVMBuildBr(g->builder, cond_bb);
  }
  LLVMPositionBuilderAtEnd(g->builder, cond_bb);
  if (stmt->for_stmt.cond) {
    LLVMValueRef cond = emit_expr(g, stmt->for_stmt.cond, locals);
    cond = zan_tobool(g->builder, cond, "tobool");
    if (locals->count > for_body_start) {
      LLVMBasicBlockRef false_bb =
          LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.false");
      LLVMBuildCondBr(g->builder, cond, body_bb, false_bb);
      LLVMPositionBuilderAtEnd(g->builder, false_bb);
      emit_release_owned_locals_range(g, locals, for_body_start);
      LLVMBuildBr(g->builder, end_bb);
    } else {
      LLVMBuildCondBr(g->builder, cond, body_bb, end_bb);
    }
  } else {
    LLVMBuildBr(g->builder, body_bb);
  }

  LLVMPositionBuilderAtEnd(g->builder, body_bb);
  emit_stmt(g, stmt->for_stmt.body, locals);
  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
    emit_release_owned_locals_from(g, locals, for_body_start);
    LLVMBuildBr(g->builder, step_bb);
  } else {
    emit_release_owned_locals_from(g, locals, for_body_start);
  }

  LLVMPositionBuilderAtEnd(g->builder, step_bb);
  if (stmt->for_stmt.step) {
    if (stmt->for_stmt.step->kind == AST_BLOCK ||
        stmt->for_stmt.step->kind == AST_EXPR_STMT) {
      emit_stmt(g, stmt->for_stmt.step, locals);
    } else {
      emit_expr(g, stmt->for_stmt.step, locals);
    }
  }
  if (!emit_async_preempt_site(g, cond_bb))
    LLVMBuildBr(g->builder, cond_bb);

  g->pending.break_scope = saved_pending.break_scope;
  g->pending.continue_scope = saved_pending.continue_scope;
  g->break_target = saved_break;
  g->continue_target = saved_cont;
  g->loop_locals_base = saved_loop_base;
  g->break_locals_base = saved_break_base;
  g->loop_catch_base = saved_loop_cbase;
  g->finally_loop_base = saved_loop_fbase;
  g->eh_armed_loop_base = saved_loop_ehbase;
  g->loop_guarded_list_count = saved_guarded_lists;
  g->loop_bce_count = saved_bce_count;

  LLVMPositionBuilderAtEnd(g->builder, end_bb);
  emit_release_owned_locals_from(g, locals, for_start);
  break;
}

case AST_BREAK_STMT:
if (g->break_target) {
  emit_pending_finallys(g, locals, g->finally_loop_base, false);
  if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
    break;
  /* 同上：catch 异常先出栈，局部变量后出栈 */
  emit_release_active_catch_excs(g, g->loop_catch_base);
  emit_release_owned_locals_range(g, locals, g->break_locals_base);
  emit_eh_disarm_from(g, g->eh_armed_loop_base);
  emit_pending_scope_exit(g, g->pending.break_scope, false);
  LLVMBuildBr(g->builder, g->break_target);
}
break;

case AST_CONTINUE_STMT:
if (g->continue_target) {
  emit_pending_finallys(g, locals, g->finally_loop_base, false);
  if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
    break;
  /* 同上：catch 异常先出栈，局部变量后出栈 */
  emit_release_active_catch_excs(g, g->loop_catch_base);
  emit_release_owned_locals_range(g, locals, g->loop_locals_base);
  emit_eh_disarm_from(g, g->eh_armed_loop_base);
  emit_pending_scope_exit(g, g->pending.continue_scope, false);
  LLVMBuildBr(g->builder, g->continue_target);
}
break;

case AST_SWITCH_STMT: {
  int switch_start = locals->count;
  zan_type_t *sw_type =
      concretize(g, infer_expr_type(g, stmt->switch_stmt.expr, locals));
  LLVMValueRef switch_val = emit_expr(g, stmt->switch_stmt.expr, locals);
  LLVMBasicBlockRef end_bb =
      LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.end");

  int num_cases = 0;
  zan_ast_node_t *default_case = NULL;
  for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
    zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
    if (sc->switch_case.pattern || sc->switch_case.type_pattern ||
        sc->switch_case.adt_pat || sc->switch_case.when_cond ||
        sc->switch_case.var_name.len > 0) {
      num_cases++;
    } else {
      default_case = sc;
    }
  }

  LLVMBasicBlockRef default_bb =
      default_case
          ? LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.default")
          : end_bb;

  zan_irgen_pending_scope_t *sw_saved_pending_break = g->pending.break_scope;
  g->pending.break_scope = g->pending.scope;
  LLVMBasicBlockRef sw_saved_break = g->break_target;
  g->break_target = end_bb;
  int sw_saved_break_base = g->break_locals_base;
  g->break_locals_base = switch_start;

  /* Tagged Union ADT switch lowering (value-type struct with __adt_tag + payload fields) */
  zan_adt_info_t *adt_info =
      (sw_type && sw_type->kind == TYPE_STRUCT && sw_type->sym &&
       sw_type->sym->decl && sw_type->sym->decl->kind == AST_STRUCT_DECL &&
       sw_type->sym->decl->meta)
          ? sw_type->sym->decl->meta->adt_info
          : NULL;
  if (adt_info) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef sw_st = get_struct_llvm_type(g, sw_type->sym);
    if (!sw_st)
      sw_st = LLVMTypeOf(switch_val);
    LLVMValueRef sw_slot = emit_entry_alloca(g, sw_st, "sw.adt");
    zan_store_fit(g, LLVMConstNull(sw_st), sw_slot);
    zan_store_fit(g, switch_val, sw_slot);

    /* If scrutinee is an owned temporary RC struct (e.g. returned from a call/ctor),
     * register cleanup at switch_start so it is released on break/return/exit. */
    bool adt_is_owned_temp =
        stmt->switch_stmt.expr &&
        (stmt->switch_stmt.expr->kind == AST_CALL ||
         stmt->switch_stmt.expr->kind == AST_NEW_EXPR ||
         stmt->switch_stmt.expr->kind == AST_SWITCH_EXPR ||
         stmt->switch_stmt.expr->kind == AST_CONDITIONAL ||
         stmt->switch_stmt.expr->kind == AST_WITH_EXPR);
    if (adt_is_owned_temp && type_contains_collection_rc(g, sw_type, 0)) {
      static const char kAdtTmp[] = {1, 'a', 'd', 't'};
      int tidx = locals->count;
      local_add(locals, (zan_istr_t){(char *)kAdtTmp, 4}, sw_slot, sw_type);
      locals->vars[tidx].struct_rc = 1;
      lpad_register_local(g, &locals->vars[tidx]);
    }
    int case_locals_base = locals->count;
    g->break_locals_base = case_locals_base;

    int tag_fi = get_field_index(sw_type->sym, (zan_istr_t){(char *)"__adt_tag", 9});
    LLVMValueRef tag_ptr =
        emit_field_ptr(g, sw_type->sym, sw_st, sw_slot, tag_fi >= 0 ? tag_fi : 0, "adt.tagp");
    LLVMValueRef tag_val = LLVMBuildLoad2(g->builder, i32, tag_ptr, "adt.tag");

    LLVMBasicBlockRef test_bb =
        LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.adttest");
    LLVMBuildBr(g->builder, test_bb);

    for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
      zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
      if (!sc->switch_case.adt_pat && !sc->switch_case.pattern)
        continue;

      zan_istr_t vname = {0};
      zan_ast_list_t *binds = NULL;
      if (sc->switch_case.adt_pat) {
        vname = sc->switch_case.adt_pat->variant;
        binds = &sc->switch_case.adt_pat->bindings;
      } else if (sc->switch_case.pattern->kind == AST_CALL) {
        zan_ast_node_t *cal = sc->switch_case.pattern->call.callee;
        if (cal && cal->kind == AST_MEMBER_ACCESS)
          vname = cal->member.name;
        else if (cal && cal->kind == AST_IDENTIFIER)
          vname = cal->ident.name;
        binds = &sc->switch_case.pattern->call.args;
      } else if (sc->switch_case.pattern->kind == AST_MEMBER_ACCESS) {
        vname = sc->switch_case.pattern->member.name;
      } else if (sc->switch_case.pattern->kind == AST_IDENTIFIER) {
        vname = sc->switch_case.pattern->ident.name;
      }

      zan_adt_variant_t *av = NULL;
      for (int vi = 0; vi < adt_info->variant_count; vi++) {
        if (adt_info->variants[vi].name.len == vname.len &&
            memcmp(adt_info->variants[vi].name.str, vname.str, (size_t)vname.len) == 0) {
          av = &adt_info->variants[vi];
          break;
        }
      }
      if (!av)
        continue;

      LLVMPositionBuilderAtEnd(g->builder, test_bb);
      LLVMBasicBlockRef bind_bb =
          LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.adtbind");
      LLVMBasicBlockRef fail_bb =
          LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.adtnext");

      LLVMValueRef cond = zan_icmp(g->builder, LLVMIntEQ, tag_val,
                                   LLVMConstInt(i32, (uint64_t)av->tag, 0),
                                   "adt.eq");
      LLVMBuildCondBr(g->builder, cond, bind_bb, fail_bb);

      LLVMPositionBuilderAtEnd(g->builder, bind_bb);
      int bc = binds ? binds->count : 0;
      for (int bi = 0; bi < bc && bi < av->params.count; bi++) {
        zan_ast_node_t *bn = binds->items[bi];
        if (!bn || bn->kind != AST_IDENTIFIER)
          continue;
        zan_istr_t bname = bn->ident.name;
        if (bname.len == 1 && bname.str[0] == '_')
          continue;
        int fi = get_field_index(sw_type->sym, av->struct_field_names[bi]);
        if (fi < 0)
          continue;
        zan_symbol_t *fsym = get_field_sym(sw_type->sym, av->struct_field_names[bi]);
        zan_type_t *fty = fsym ? field_type_at(g, fsym, sw_type) : NULL;
        if (!fty)
          continue;
        LLVMTypeRef fll = map_type(g, fty);
        LLVMValueRef fptr = emit_field_ptr(g, sw_type->sym, sw_st, sw_slot, fi, "adt.fp");
        LLVMValueRef fv0 = LLVMBuildLoad2(g->builder, fll, fptr, "adt.fv");
        LLVMValueRef fv = promote_loaded(g, fv0, fty);
        LLVMTypeRef bslot_ty = LLVMTypeOf(fv);
        LLVMValueRef bslot = emit_entry_alloca(g, bslot_ty, "adt.bind");
        zan_store_fit(g, LLVMConstNull(bslot_ty), bslot);
        /* Retain payload so destructured variable owns its reference cleanly */
        int is_struct_rc = (fty->kind == TYPE_STRUCT &&
                            type_contains_collection_rc(g, fty, 0));
        int is_ptr_rc = (!is_struct_rc && is_rc_managed_type(fty) &&
                         LLVMGetTypeKind(bslot_ty) == LLVMPointerTypeKind);
        if (is_struct_rc)
          emit_collection_value_retain(g, fty, fv, 0);
        else if (is_ptr_rc)
          emit_rc_retain_for_type(g, fty, fv);
        zan_store_fit(g, fv, bslot);
        int bidx = locals->count;
        local_add(locals, bname, bslot, fty);
        locals->vars[bidx].binding_decl = bn;
        if (is_ptr_rc) {
          locals->vars[bidx].arc_owned = 1;
          lpad_register_local(g, &locals->vars[bidx]);
        } else if (is_struct_rc) {
          locals->vars[bidx].struct_rc = 1;
          lpad_register_local(g, &locals->vars[bidx]);
        }
      }

      if (sc->switch_case.when_cond) {
        LLVMBasicBlockRef match_bb =
            LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.adtmatch");
        LLVMBasicBlockRef gfail_bb =
            LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.adtgfail");
        LLVMValueRef gv = emit_expr(g, sc->switch_case.when_cond, locals);
        if (LLVMGetTypeKind(LLVMTypeOf(gv)) != LLVMIntegerTypeKind ||
            LLVMGetIntTypeWidth(LLVMTypeOf(gv)) != 1)
          gv = LLVMBuildICmp(g->builder, LLVMIntNE, gv,
                             LLVMConstInt(LLVMTypeOf(gv), 0, 0), "sw.guard");
        LLVMBuildCondBr(g->builder, gv, match_bb, gfail_bb);
        LLVMPositionBuilderAtEnd(g->builder, gfail_bb);
        int saved_bind_count = locals->count;
        emit_release_owned_locals_from(g, locals, case_locals_base);
        locals->count = saved_bind_count;
        LLVMBuildBr(g->builder, fail_bb);
        LLVMPositionBuilderAtEnd(g->builder, match_bb);
      }

      emit_stmt(g, sc->switch_case.body, locals);
      if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
        emit_release_owned_locals_from(g, locals, case_locals_base);
        LLVMBuildBr(g->builder, end_bb);
      }
      locals->count = case_locals_base;
      test_bb = fail_bb;
    }

    LLVMPositionBuilderAtEnd(g->builder, test_bb);
    if (default_case)
      LLVMBuildBr(g->builder, default_bb);
    else
      LLVMBuildBr(g->builder, end_bb);

    if (default_case) {
      LLVMPositionBuilderAtEnd(g->builder, default_bb);
      emit_stmt(g, default_case->switch_case.body, locals);
      if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
        emit_release_owned_locals_from(g, locals, case_locals_base);
        LLVMBuildBr(g->builder, end_bb);
      }
      locals->count = case_locals_base;
    }

    LLVMPositionBuilderAtEnd(g->builder, end_bb);
    emit_release_owned_locals_from(g, locals, switch_start);
    locals->count = switch_start;
    g->pending.break_scope = sw_saved_pending_break;
    g->break_target = sw_saved_break;
    g->break_locals_base = sw_saved_break_base;
    break;
  }

  bool has_patterns = false;
  for (int i = 0; i < stmt->switch_stmt.cases.count && !has_patterns; i++) {
    zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
    if (sc->switch_case.type_pattern || sc->switch_case.when_cond ||
        sc->switch_case.var_name.len > 0 ||
        (sc->switch_case.pattern &&
         sc->switch_case.pattern->kind == AST_NULL_LITERAL))
      has_patterns = true;
  }
  if (has_patterns) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    (void)LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    bool sw_string = is_string_expr(g, stmt->switch_stmt.expr, locals);
    LLVMValueRef strcmp_fn = NULL;
    LLVMTypeRef strcmp_ty = NULL;
    if (sw_string) {
      strcmp_fn = LLVMGetNamedFunction(g->mod, "strcmp");
      strcmp_ty = LLVMFunctionType(i32, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0);
      if (!strcmp_fn)
        strcmp_fn = LLVMAddFunction(g->mod, "strcmp", strcmp_ty);
    }

    int nc = stmt->switch_stmt.cases.count;
    LLVMBasicBlockRef *pmatch = (LLVMBasicBlockRef *)calloc(
        (size_t)(nc > 0 ? nc : 1), sizeof(LLVMBasicBlockRef));
    int *psrc = (int *)calloc((size_t)(nc > 0 ? nc : 1), sizeof(int));
    int ci = 0;

    LLVMBasicBlockRef test_bb =
        LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.ptest");
    LLVMBuildBr(g->builder, test_bb);

    for (int i = 0; i < nc; i++) {
      zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
      if (!sc->switch_case.pattern && !sc->switch_case.type_pattern)
        continue;
      LLVMPositionBuilderAtEnd(g->builder, test_bb);
      LLVMBasicBlockRef match_bb =
          LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.pmatch");
      LLVMBasicBlockRef fail_bb =
          LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.pnext");

      emit_switch_pattern_bind(g, locals, sc, switch_val);

      LLVMValueRef cond;
      if (sc->switch_case.type_pattern) {
        zan_type_t *pt = resolve_type_ctx(g, sc->switch_case.type_pattern);
        if (pt && (pt->kind == TYPE_CLASS || pt->kind == TYPE_STRING ||
                   pt->kind == TYPE_OBJECT || pt->kind == TYPE_INTERFACE))
          cond = emit_runtime_is_check_name(g, switch_val, pt);
        else {
          zan_type_t *dt = infer_expr_type(g, stmt->switch_stmt.expr, locals);
          cond = LLVMConstInt(LLVMInt1TypeInContext(g->ctx),
                              (pt && dt && types_equal(dt, pt)) ? 1 : 0, 0);
        }
      } else if (sc->switch_case.pattern->kind == AST_NULL_LITERAL) {
        cond = zan_icmp(g->builder, LLVMIntEQ, switch_val,
                        LLVMConstNull(LLVMTypeOf(switch_val)), "sw.null");
      } else if (sw_string) {
        LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
        LLVMValueRef cmp =
            zan_call2(g->builder, strcmp_ty, strcmp_fn,
                      (LLVMValueRef[]){switch_val, case_val}, 2, "swcmp");
        cond = zan_icmp(g->builder, LLVMIntEQ, cmp, LLVMConstInt(i32, 0, 0),
                        "sweq");
      } else {
        LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
        cond = zan_icmp(g->builder, LLVMIntEQ, switch_val, case_val, "sw.eq");
      }
      if (sc->switch_case.when_cond) {
        LLVMBasicBlockRef guard_bb =
            LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.pguard");
        LLVMBuildCondBr(g->builder, cond, guard_bb, fail_bb);
        LLVMPositionBuilderAtEnd(g->builder, guard_bb);
        LLVMValueRef gv = emit_expr(g, sc->switch_case.when_cond, locals);
        if (LLVMGetTypeKind(LLVMTypeOf(gv)) != LLVMIntegerTypeKind ||
            LLVMGetIntTypeWidth(LLVMTypeOf(gv)) != 1)
          gv = LLVMBuildICmp(g->builder, LLVMIntNE, gv,
                             LLVMConstInt(LLVMTypeOf(gv), 0, 0), "sw.guard");
        LLVMBuildCondBr(g->builder, gv, match_bb, fail_bb);
      } else {
        LLVMBuildCondBr(g->builder, cond, match_bb, fail_bb);
      }
      LLVMPositionBuilderAtEnd(g->builder, fail_bb);
      emit_switch_pattern_fail_release(g, locals, sc);
      pmatch[ci] = match_bb;
      psrc[ci] = i;
      ci++;
      test_bb = LLVMGetInsertBlock(g->builder);
    }

    LLVMPositionBuilderAtEnd(g->builder, test_bb);
    if (default_case)
      LLVMBuildBr(g->builder, default_bb);
    else
      LLVMBuildBr(g->builder, end_bb);

    for (int k = 0; k < ci; k++) {
      int i = psrc[k];
      zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
      LLVMPositionBuilderAtEnd(g->builder, pmatch[k]);
      bool case_empty = (sc->switch_case.body &&
                         sc->switch_case.body->block.stmts.count == 0);
      if (case_empty) {
        LLVMBasicBlockRef fall = NULL;
        for (int k2 = k + 1; k2 < ci; k2++) {
          zan_ast_node_t *nxt = stmt->switch_stmt.cases.items[psrc[k2]];
          if (nxt->switch_case.body &&
              nxt->switch_case.body->block.stmts.count > 0) {
            fall = pmatch[k2];
            break;
          }
        }
        if (!fall)
          fall = default_bb;
        LLVMBuildBr(g->builder, fall);
        continue;
      }
      emit_switch_pattern_bind(g, locals, sc, switch_val);
      emit_stmt(g, sc->switch_case.body, locals);
      if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
        emit_release_owned_locals_from(g, locals, switch_start);
        LLVMBuildBr(g->builder, end_bb);
      } else {
        emit_release_owned_locals_from(g, locals, switch_start);
      }
    }
    free(pmatch);
    free(psrc);

    if (default_case) {
      LLVMPositionBuilderAtEnd(g->builder, default_bb);
      emit_stmt(g, default_case->switch_case.body, locals);
      if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
        emit_release_owned_locals_from(g, locals, switch_start);
        LLVMBuildBr(g->builder, end_bb);
      } else {
        emit_release_owned_locals_from(g, locals, switch_start);
      }
    }

    g->pending.break_scope = sw_saved_pending_break;
    g->break_target = sw_saved_break;
    g->break_locals_base = sw_saved_break_base;
    LLVMPositionBuilderAtEnd(g->builder, end_bb);
    break;
  }

  if (is_string_expr(g, stmt->switch_stmt.expr, locals)) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef strcmp_fn = LLVMGetNamedFunction(g->mod, "strcmp");
    LLVMTypeRef strcmp_ty =
        LLVMFunctionType(i32, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0);
    if (!strcmp_fn)
      strcmp_fn = LLVMAddFunction(g->mod, "strcmp", strcmp_ty);

    LLVMBasicBlockRef *case_bbs = (LLVMBasicBlockRef *)calloc(
        (size_t)(num_cases > 0 ? num_cases : 1), sizeof(LLVMBasicBlockRef));
    int *case_src =
        (int *)calloc((size_t)(num_cases > 0 ? num_cases : 1), sizeof(int));
    int ci = 0;
    for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
      zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
      if (!sc->switch_case.pattern)
        continue;
      LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
      LLVMBasicBlockRef case_bb =
          LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.case");
      LLVMBasicBlockRef next_bb =
          LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.next");
      LLVMValueRef cmp =
          zan_call2(g->builder, strcmp_ty, strcmp_fn,
                    (LLVMValueRef[]){switch_val, case_val}, 2, "swcmp");
      LLVMValueRef eq =
          zan_icmp(g->builder, LLVMIntEQ, cmp, LLVMConstInt(i32, 0, 0), "sweq");
      LLVMBuildCondBr(g->builder, eq, case_bb, next_bb);
      LLVMPositionBuilderAtEnd(g->builder, next_bb);
      case_bbs[ci] = case_bb;
      case_src[ci] = i;
      ci++;
    }
    LLVMBuildBr(g->builder, default_bb);

    for (int k = 0; k < ci; k++) {
      int i = case_src[k];
      zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
      LLVMPositionBuilderAtEnd(g->builder, case_bbs[k]);
      bool case_empty = (sc->switch_case.body &&
                         sc->switch_case.body->block.stmts.count == 0);
      if (case_empty) {
        LLVMBasicBlockRef fall_bb = NULL;
        for (int k2 = k + 1; k2 < ci; k2++) {
          zan_ast_node_t *nxt = stmt->switch_stmt.cases.items[case_src[k2]];
          if (nxt->switch_case.body &&
              nxt->switch_case.body->block.stmts.count > 0) {
            fall_bb = case_bbs[k2];
            break;
          }
        }
        if (!fall_bb)
          fall_bb = default_bb;
        LLVMBuildBr(g->builder, fall_bb);
        continue;
      }
      emit_stmt(g, sc->switch_case.body, locals);
      if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
        emit_release_owned_locals_from(g, locals, switch_start);
        LLVMBuildBr(g->builder, end_bb);
      } else {
        emit_release_owned_locals_from(g, locals, switch_start);
      }
    }
    free(case_bbs);
    free(case_src);

    if (default_case) {
      LLVMPositionBuilderAtEnd(g->builder, default_bb);
      emit_stmt(g, default_case->switch_case.body, locals);
      if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
        emit_release_owned_locals_from(g, locals, switch_start);
        LLVMBuildBr(g->builder, end_bb);
      } else {
        emit_release_owned_locals_from(g, locals, switch_start);
      }
    }

    g->pending.break_scope = sw_saved_pending_break;
    g->break_target = sw_saved_break;
    g->break_locals_base = sw_saved_break_base;
    LLVMPositionBuilderAtEnd(g->builder, end_bb);
    break;
  }

  LLVMValueRef sw =
      LLVMBuildSwitch(g->builder, switch_val, default_bb, (unsigned)num_cases);

  LLVMBasicBlockRef *case_bbs = (LLVMBasicBlockRef *)calloc(
      (size_t)(num_cases > 0 ? num_cases : 1), sizeof(LLVMBasicBlockRef));
  int *case_src =
      (int *)calloc((size_t)(num_cases > 0 ? num_cases : 1), sizeof(int));
  int nci = 0;
  for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
    zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
    if (!sc->switch_case.pattern)
      continue;

    LLVMBasicBlockRef case_bb =
        LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.case");
    LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->builder);
    LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
    if (!case_val || !LLVMIsAConstantInt(case_val)) {
      zan_diag_emit_code(g->diag, DIAG_ERROR, sc->loc, 3074,
                         "case label must be a compile-time constant "
                         "in an integer switch");
      LLVMPositionBuilderAtEnd(g->builder, case_bb);
      emit_release_owned_locals_from(g, locals, switch_start);
      LLVMBuildBr(g->builder, end_bb);
      LLVMPositionBuilderAtEnd(g->builder, entry_bb);
      continue;
    }
    LLVMTypeRef swty = LLVMTypeOf(switch_val);
    if (LLVMGetTypeKind(LLVMTypeOf(case_val)) == LLVMIntegerTypeKind &&
        LLVMGetTypeKind(swty) == LLVMIntegerTypeKind &&
        LLVMTypeOf(case_val) != swty) {
      long long cv = LLVMConstIntGetSExtValue(case_val);
      case_val = LLVMConstInt(swty, (unsigned long long)cv, 1);
    }
    LLVMAddCase(sw, case_val, case_bb);
    case_bbs[nci] = case_bb;
    case_src[nci] = i;
    nci++;
  }

  for (int k = 0; k < nci; k++) {
    int i = case_src[k];
    zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
    LLVMBasicBlockRef case_bb = case_bbs[k];
    bool case_empty =
        (sc->switch_case.body && sc->switch_case.body->block.stmts.count == 0);
    LLVMBasicBlockRef fall_bb = NULL;
    if (case_empty) {
      for (int k2 = k + 1; k2 < nci; k2++) {
        zan_ast_node_t *nxt = stmt->switch_stmt.cases.items[case_src[k2]];
        if (nxt->switch_case.body &&
            nxt->switch_case.body->block.stmts.count > 0) {
          fall_bb = case_bbs[k2];
          break;
        }
      }
      if (!fall_bb)
        fall_bb = default_bb;
    }

    LLVMPositionBuilderAtEnd(g->builder, case_bb);
    if (case_empty) {
      LLVMBuildBr(g->builder, fall_bb);
      continue;
    }
    emit_stmt(g, sc->switch_case.body, locals);
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
      emit_release_owned_locals_from(g, locals, switch_start);
      LLVMBuildBr(g->builder, end_bb);
    } else {
      emit_release_owned_locals_from(g, locals, switch_start);
    }
  }
  free(case_bbs);
  free(case_src);

  if (default_case) {
    LLVMPositionBuilderAtEnd(g->builder, default_bb);
    emit_stmt(g, default_case->switch_case.body, locals);
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
      emit_release_owned_locals_from(g, locals, switch_start);
      LLVMBuildBr(g->builder, end_bb);
    } else {
      emit_release_owned_locals_from(g, locals, switch_start);
    }
  }

  g->pending.break_scope = sw_saved_pending_break;
  g->break_target = sw_saved_break;
  g->break_locals_base = sw_saved_break_base;
  LLVMPositionBuilderAtEnd(g->builder, end_bb);
  break;
}
