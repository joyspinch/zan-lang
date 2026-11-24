static LLVMValueRef refl_emit_get_type(zan_irgen_t *g, zan_type_t *st,
                                       LLVMValueRef obj) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMValueRef fallback = refl_meta_for(g, st, NULL, 0);
  if (!st || (st->kind != TYPE_CLASS && st->kind != TYPE_INTERFACE &&
              st->kind != TYPE_OBJECT))
    return fallback;
  if (!obj || LLVMGetTypeKind(LLVMTypeOf(obj)) != LLVMPointerTypeKind)
    return fallback;
  obj = LLVMBuildBitCast(g->builder, obj, i8ptr, "refl.obj");
  LLVMValueRef fn = refl_obj_type_fn(g);
  return zan_call2(g->builder,
                   LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
                   fn, (LLVMValueRef[]){obj, fallback}, 2, "refl.ty");
}

static LLVMValueRef refl_receiver_ptr(zan_irgen_t *g, zan_type_t *rt,
                                      LLVMValueRef v) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  if (!v)
    return LLVMConstNull(i8ptr);
  LLVMTypeRef vt = LLVMTypeOf(v);
  if (LLVMGetTypeKind(vt) == LLVMPointerTypeKind)
    return LLVMBuildBitCast(g->builder, v, i8ptr, "refl.recv");
  if (rt && rt->kind == TYPE_STRUCT) {
    LLVMValueRef tmp = LLVMBuildAlloca(g->builder, vt, "refl.spill");
    LLVMBuildStore(g->builder, v, tmp);
    return LLVMBuildBitCast(g->builder, tmp, i8ptr, "refl.recv");
  }
  return LLVMConstNull(i8ptr);
}

static LLVMValueRef refl_mi64(zan_irgen_t *g, LLVMValueRef ti, int tbl,
                              LLVMValueRef idx, int which) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  return zan_call2(
      g->builder,
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i64, i64, i64}, 4, 0),
      refl_mi64_fn(g),
      (LLVMValueRef[]){ti, LLVMConstInt(i64, (unsigned long long)tbl, 0), idx,
                       LLVMConstInt(i64, (unsigned long long)which, 0)},
      4, "refl.mi");
}

static LLVMValueRef refl_mstr(zan_irgen_t *g, LLVMValueRef ti, int tbl,
                              LLVMValueRef idx, int which, LLVMValueRef k) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  return zan_call2(
      g->builder,
      LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i64, i64, i64, i64}, 5, 0),
      refl_mstr_fn(g),
      (LLVMValueRef[]){ti, LLVMConstInt(i64, (unsigned long long)tbl, 0), idx,
                       LLVMConstInt(i64, (unsigned long long)which, 0), k},
      5, "refl.ms");
}

static LLVMValueRef refl_mfind(zan_irgen_t *g, LLVMValueRef ti,
                               LLVMValueRef name, int flags) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  return zan_call2(
      g->builder,
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i64}, 3, 0),
      refl_mfind_fn(g),
      (LLVMValueRef[]){ti, name,
                       LLVMConstInt(i64, (unsigned long long)flags, 0)},
      3, "refl.mfi");
}

static LLVMValueRef refl_arg_index(zan_irgen_t *g, zan_ast_node_t *arg,
                                   local_scope_t *locals) {
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  if (!arg)
    return LLVMConstInt(i64, 0, 0);
  return emit_index_i64(g, emit_expr(g, arg, locals), "refl.i");
}

static LLVMValueRef refl_arg_str(zan_irgen_t *g, zan_ast_node_t *arg,
                                 local_scope_t *locals) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMValueRef v = arg ? emit_expr(g, arg, locals) : refl_empty_string(g);
  return LLVMBuildBitCast(g->builder, v, i8ptr, "refl.nm");
}

typedef struct {
  LLVMValueRef values[ZAN_REFL_ARG_SLOTS];
  LLVMValueRef boxes[ZAN_REFL_ARG_SLOTS];
  int pushed;
} refl_arg_temps_t;

static LLVMValueRef refl_pack_single_val(zan_irgen_t *g, zan_ast_node_t *expr,
                                         LLVMValueRef value,
                                         local_scope_t *locals,
                                         LLVMValueRef *out_vkind,
                                         LLVMValueRef *out_box) {
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  if (out_box)
    *out_box = NULL;
  if (!value || !expr) {
    *out_vkind = LLVMConstInt(i64, ZAN_REFL_VK_INT, 0);
    return LLVMConstInt(i64, 0, 0);
  }
  zan_type_t *vt = infer_expr_type(g, expr, locals);
  LLVMTypeRef lt = LLVMTypeOf(value);
  LLVMTypeKind tk = LLVMGetTypeKind(lt);
  if (vt && vt->kind == TYPE_DECIMAL) {
    LLVMTypeRef dec_ty = get_decimal_type(g);
    LLVMValueRef ds = emit_entry_alloca(g, dec_ty, "refl.darg");
    LLVMBuildStore(g->builder, value, ds);
    *out_vkind = LLVMConstInt(i64, ZAN_REFL_VK_DECIMAL, 0);
    return LLVMBuildPtrToInt(g->builder, ds, i64, "refl.dptr");
  }
  if (vt && vt->kind == TYPE_STRUCT) {
    if (irgen_boxed_eligible_struct(g, vt)) {
      bool owned = expr_yields_owned_rc_value(g, expr, locals);
      LLVMValueRef box = refl_emit_box_struct_val(g, vt, value, owned);
      if (out_box)
        *out_box = box;
      *out_vkind = LLVMConstInt(i64, ZAN_REFL_VK_PTR, 0);
      return LLVMBuildPtrToInt(g->builder, box, i64, "refl.sbox");
    }
    *out_vkind = LLVMConstInt(i64, ZAN_REFL_VK_INT, 0);
    return LLVMConstInt(i64, 0, 0);
  }
  if (vt && vt->kind == TYPE_NULLABLE && vt->element_type &&
      llvm_is_nullable(lt)) {
    zan_type_t *et = vt->element_type;
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef some_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "refl.pns");
    LLVMBasicBlockRef none_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "refl.pnn");
    LLVMBasicBlockRef done_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "refl.pnd");
    LLVMBuildCondBr(g->builder, nullable_has_value(g, value), some_bb, none_bb);

    LLVMPositionBuilderAtEnd(g->builder, some_bb);
    LLVMValueRef pl = nullable_get_payload(g, value);
    LLVMValueRef sv = LLVMConstInt(i64, 0, 0);
    LLVMValueRef sk = LLVMConstInt(i64, ZAN_REFL_VK_INT, 0);
    LLVMValueRef sbox = LLVMConstNull(i8ptr);
    if (et->kind == TYPE_DECIMAL) {
      LLVMTypeRef dec_ty = get_decimal_type(g);
      LLVMValueRef ds = emit_entry_alloca(g, dec_ty, "refl.ndarg");
      LLVMBuildStore(g->builder, pl, ds);
      sv = LLVMBuildPtrToInt(g->builder, ds, i64, "refl.ndptr");
      sk = LLVMConstInt(i64, ZAN_REFL_VK_DECIMAL, 0);
    } else if (et->kind == TYPE_STRUCT && irgen_boxed_eligible_struct(g, et)) {
      bool owned = expr_yields_owned_rc_value(g, expr, locals);
      sbox = refl_emit_box_struct_val(g, et, pl, owned);
      sv = LLVMBuildPtrToInt(g->builder, sbox, i64, "refl.nsbox");
      sk = LLVMConstInt(i64, ZAN_REFL_VK_PTR, 0);
    } else if (et->kind == TYPE_FLOAT || et->kind == TYPE_DOUBLE) {
      sv = refl_to_slot(g, pl);
      sk = LLVMConstInt(i64, ZAN_REFL_VK_DOUBLE, 0);
    } else if (LLVMGetTypeKind(LLVMTypeOf(pl)) == LLVMIntegerTypeKind) {
      sv = extend_int_for_slot(g, pl, et, i64);
      sk = LLVMConstInt(i64, ZAN_REFL_VK_INT, 0);
    }
    LLVMBasicBlockRef some_end = LLVMGetInsertBlock(g->builder);
    LLVMBuildBr(g->builder, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, none_bb);
    LLVMBuildBr(g->builder, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    LLVMValueRef vphi = LLVMBuildPhi(g->builder, i64, "refl.pnv");
    LLVMAddIncoming(vphi, (LLVMValueRef[]){sv, LLVMConstInt(i64, 0, 0)},
                    (LLVMBasicBlockRef[]){some_end, none_bb}, 2);
    LLVMValueRef kphi = LLVMBuildPhi(g->builder, i64, "refl.pnk");
    LLVMAddIncoming(kphi,
                    (LLVMValueRef[]){sk, LLVMConstInt(i64, ZAN_REFL_VK_PTR, 0)},
                    (LLVMBasicBlockRef[]){some_end, none_bb}, 2);
    if (out_box && et->kind == TYPE_STRUCT && irgen_boxed_eligible_struct(g, et)) {
      LLVMValueRef bphi = LLVMBuildPhi(g->builder, i8ptr, "refl.pnb");
      LLVMAddIncoming(bphi, (LLVMValueRef[]){sbox, LLVMConstNull(i8ptr)},
                      (LLVMBasicBlockRef[]){some_end, none_bb}, 2);
      *out_box = bphi;
    }
    *out_vkind = kphi;
    return vphi;
  }
  if (tk == LLVMFloatTypeKind || tk == LLVMDoubleTypeKind) {
    *out_vkind = LLVMConstInt(i64, ZAN_REFL_VK_DOUBLE, 0);
    return refl_to_slot(g, value);
  }
  if (tk == LLVMPointerTypeKind) {
    *out_vkind = LLVMConstInt(i64, ZAN_REFL_VK_PTR, 0);
    return refl_to_slot(g, value);
  }
  if (tk == LLVMIntegerTypeKind) {
    *out_vkind = LLVMConstInt(i64, ZAN_REFL_VK_INT, 0);
    return extend_int_for_slot(g, value, vt, i64);
  }
  *out_vkind = LLVMConstInt(i64, ZAN_REFL_VK_INT, 0);
  return refl_to_slot(g, value);
}

static LLVMValueRef refl_pack_args(zan_irgen_t *g, zan_ast_node_t **args,
                                   int argc, int from, local_scope_t *locals,
                                   refl_arg_temps_t *temps) {
  memset(temps, 0, sizeof(*temps));
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMValueRef slots = LLVMBuildArrayAlloca(
      g->builder, i64, LLVMConstInt(i64, 2 * ZAN_REFL_ARG_SLOTS, 0),
      "refl.args");
  for (int i = 0; i < ZAN_REFL_ARG_SLOTS; i++) {
    LLVMValueRef off = LLVMConstInt(i64, (unsigned long long)i, 0);
    LLVMValueRef p = LLVMBuildGEP2(g->builder, i64, slots, &off, 1, "refl.as");
    LLVMValueRef koff =
        LLVMConstInt(i64, (unsigned long long)(ZAN_REFL_ARG_SLOTS + i), 0);
    LLVMValueRef kp =
        LLVMBuildGEP2(g->builder, i64, slots, &koff, 1, "refl.aks");
    int ai = from + i;
    LLVMValueRef v = LLVMConstInt(i64, 0, 0);
    LLVMValueRef vk = LLVMConstInt(i64, ZAN_REFL_VK_INT, 0);
    if (args && ai < argc) {
      LLVMValueRef value = emit_expr(g, args[ai], locals);
      LLVMValueRef box = NULL;
      v = refl_pack_single_val(g, args[ai], value, locals, &vk, &box);
      if (box) {
        temps->boxes[i] = box;
        emit_eh_tmp_push_obj(g, box);
        temps->pushed++;
      } else {
        temps->values[i] = value;
        /* Reuse normal calls' exception protocol: keep each fresh argument
         * alive across evaluation of later arguments and the thunk call. */
        temps->pushed += emit_call_arg_eh_push(g, args[ai], value, locals);
      }
    }
    LLVMBuildStore(g->builder, v, p);
    LLVMBuildStore(g->builder, vk, kp);
  }
  return slots;
}

static void refl_release_args(zan_irgen_t *g, zan_ast_node_t **args, int argc,
                              int from, local_scope_t *locals,
                              refl_arg_temps_t *temps) {
  for (int i = 0; i < temps->pushed; i++)
    emit_eh_tmp_pop(g);
  for (int i = ZAN_REFL_ARG_SLOTS - 1; i >= 0; i--) {
    if (temps->boxes[i]) {
      emit_release_obj_value(g, temps->boxes[i]);
    } else if (args && from + i < argc && temps->values[i]) {
      emit_release_owned_call_temp(g, args[from + i], temps->values[i], locals);
    }
  }
}

void refl_note_invoke_literal(zan_irgen_t *g, zan_ast_node_t *name) {
  if (!name || name->kind != AST_STRING_LITERAL || !name->str_val.len ||
      memchr(name->str_val.str, '\0', name->str_val.len))
    return;
  zan_istr_t text = name->str_val;
  for (int i = 0; i < g->refl_invoke_name_count; i++) {
    zan_istr_t known = g->refl_invoke_names[i];
    if (known.len == text.len && memcmp(known.str, text.str, text.len) == 0)
      return;
  }
  if (g->refl_invoke_name_count == g->refl_invoke_name_cap) {
    int cap = g->refl_invoke_name_cap ? g->refl_invoke_name_cap * 2 : 8;
    zan_istr_t *names = zan_arena_alloc(g->arena, (size_t)cap * sizeof(*names));
    if (!names) {
      g->refl_invoke_unknown = true;
      return;
    }
    if (g->refl_invoke_name_count)
      memcpy(names, g->refl_invoke_names,
             (size_t)g->refl_invoke_name_count * sizeof(*names));
    g->refl_invoke_names = names;
    g->refl_invoke_name_cap = cap;
  }
  text.str = zan_arena_strdup(g->arena, text.str, text.len);
  if (!text.str) {
    g->refl_invoke_unknown = true;
    return;
  }
  g->refl_invoke_names[g->refl_invoke_name_count++] = text;
}

void refl_note_propspec_accessors(zan_irgen_t *g, zan_symbol_t *method_sym,
                                  zan_ast_node_t *call) {
  if (!g || !method_sym || !method_sym->parent || !call)
    return;
  zan_symbol_t *owner = method_sym->parent;
  if (owner->name.len != 8 || memcmp(owner->name.str, "PropSpec", 8) != 0)
    return;
  if (method_sym->name.len == 8 &&
      memcmp(method_sym->name.str, "Accessor", 8) == 0) {
    if (call->call.args.count >= 1)
      refl_note_invoke_literal(g, call->call.args.items[0]);
    if (call->call.args.count >= 2)
      refl_note_invoke_literal(g, call->call.args.items[1]);
  } else if (method_sym->name.len == 6 &&
             memcmp(method_sym->name.str, "Setter", 6) == 0) {
    if (call->call.args.count >= 1)
      refl_note_invoke_literal(g, call->call.args.items[0]);
  }
}

static void refl_note_invoke_name(zan_irgen_t *g, zan_ast_node_t *name) {
  if (name && (name->kind != AST_STRING_LITERAL ||
               memchr(name->str_val.str, '\0', name->str_val.len))) {
    g->refl_invoke_unknown = true;
    return;
  }
  refl_note_invoke_literal(g, name);
}

/* thunk 把返回引用的所有权放进 ret 槽；不接管所有权的消费臂丢弃前必须
 * 释放，否则每次调用漏一个对象（探针 p2：InvokeLong 取 object 字段，
 * 进程退出漏 payload）。incl_string：数值化臂对 FK_STRING 错配返回的
 * 串一并释放；ISTRING 臂接管 FK_STRING，只丢 FK_REF。 */
static void refl_emit_owned_drop(zan_irgen_t *g, LLVMValueRef v, LLVMValueRef k,
                                 LLVMValueRef called, bool incl_string) {
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMValueRef is_obj = LLVMBuildOr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntEQ, k,
                    LLVMConstInt(i64, ZAN_REFL_FK_REF, 0), "refl.dropref"),
      LLVMBuildICmp(g->builder, LLVMIntEQ, k,
                    LLVMConstInt(i64, ZAN_REFL_FK_STRUCT, 0), "refl.dropst"),
      "refl.dropobj");
  LLVMValueRef is_str = NULL;
  LLVMValueRef owned = is_obj;
  if (incl_string) {
    is_str = LLVMBuildICmp(g->builder, LLVMIntEQ, k,
                           LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0),
                           "refl.dropstr");
    owned = LLVMBuildOr(g->builder, owned, is_str, "refl.dropref2");
  }
  LLVMValueRef drop = LLVMBuildAnd(
      g->builder, LLVMBuildAnd(g->builder, called, owned, "refl.droprise"),
      LLVMBuildICmp(g->builder, LLVMIntNE, v, LLVMConstInt(i64, 0, 0),
                    "refl.dropnz"),
      "refl.drop");
  LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
  LLVMBasicBlockRef rel_b =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "refl.irel");
  LLVMBasicBlockRef cont_b =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "refl.icont");
  LLVMBuildCondBr(g->builder, drop, rel_b, cont_b);
  LLVMPositionBuilderAtEnd(g->builder, rel_b);
  LLVMValueRef p = LLVMBuildIntToPtr(g->builder, v, i8ptr, "refl.dropp");
  LLVMTypeRef rel_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                        (LLVMTypeRef[]){i8ptr}, 1, 0);
  if (incl_string) {
    /* 字符串没有对象头（ZAN_STR_HDR_WORD），必须走 rt_str_release；
     * 对象与装箱结构体走 rt_release_dyn。 */
    LLVMBasicBlockRef str_b =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "refl.irels");
    LLVMBasicBlockRef obj_b =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "refl.irelo");
    LLVMBuildCondBr(g->builder, is_str, str_b, obj_b);
    LLVMPositionBuilderAtEnd(g->builder, str_b);
    zan_call2(g->builder, rel_ty, g->rt_str_release, &p, 1, "");
    LLVMBuildBr(g->builder, cont_b);
    LLVMPositionBuilderAtEnd(g->builder, obj_b);
    zan_call2(g->builder, rel_ty, g->rt_release_dyn, &p, 1, "");
    LLVMBuildBr(g->builder, cont_b);
  } else {
    zan_call2(g->builder, rel_ty, g->rt_release_dyn, &p, 1, "");
    LLVMBuildBr(g->builder, cont_b);
  }
  LLVMPositionBuilderAtEnd(g->builder, cont_b);
}

static LLVMValueRef refl_emit_unpack_invoke_ret(zan_irgen_t *g, int ret_kind,
                                                LLVMValueRef v,
                                                LLVMValueRef kout) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMValueRef kv = LLVMBuildLoad2(g->builder, i64, kout, "refl.ikv");
  LLVMValueRef called = LLVMBuildICmp(
      g->builder, LLVMIntNE, kv, LLVMConstInt(i64, 0, 0), "refl.icalled");
  LLVMValueRef k =
      LLVMBuildSub(g->builder, kv, LLVMConstInt(i64, 1, 0), "refl.ik1");
  if (ret_kind == 0) { /* void -> bool */
    refl_emit_owned_drop(g, v, k, called, true);
    return called;
  }
  if (ret_kind == 5) { /* object */
    LLVMValueRef obj_ret = LLVMBuildIntToPtr(g->builder, v, i8ptr, "refl.iobj");
    return LLVMBuildSelect(g->builder, called, obj_ret, LLVMConstNull(i8ptr),
                           "refl.ires");
  }
  if (ret_kind == 6) /* task */
    return LLVMBuildSelect(g->builder, called, v, LLVMConstInt(i64, 0, 0),
                           "refl.itok");
  if (ret_kind == 4) { /* string */
    LLVMValueRef s = LLVMBuildIntToPtr(g->builder, v, i8ptr, "refl.isv");
    LLVMValueRef isstr = LLVMBuildAnd(
        g->builder, called,
        LLVMBuildICmp(g->builder, LLVMIntEQ, k,
                      LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0), "refl.isk"),
        "refl.isok");
    isstr = LLVMBuildAnd(g->builder, isstr,
                         LLVMBuildIsNotNull(g->builder, s, "refl.isnn"),
                         "refl.isok2");
    LLVMValueRef res = LLVMBuildSelect(g->builder, isstr, s,
                                       refl_empty_string(g), "refl.istr");
    refl_emit_owned_drop(g, v, k, called, false);
    return res;
  }
  if (ret_kind == 3) { /* double */
    LLVMValueRef res = LLVMBuildSelect(g->builder, called,
                                       refl_pget_as_f64(g, v, k),
                                       LLVMConstReal(dbl, 0.0), "refl.idv");
    refl_emit_owned_drop(g, v, k, called, true);
    return res;
  }
  LLVMValueRef iv = LLVMBuildSelect(g->builder, called,
                                    refl_pget_as_i64(g, v, k),
                                    LLVMConstInt(i64, 0, 0), "refl.iiv");
  LLVMValueRef res =
      (ret_kind == 1) ? LLVMBuildTrunc(g->builder, iv, i32, "refl.ii32") : iv;
  refl_emit_owned_drop(g, v, k, called, true);
  return res;
}

/* `ti */
bool refl_emit_typeinfo_member(zan_irgen_t *g, LLVMValueRef ti, zan_istr_t name,
                               zan_ast_node_t **args, int argc,
                               local_scope_t *locals, LLVMValueRef *out) {
  int code = 0;
  if (!zan_refl_typeinfo_member(name, &code))
    return false;
  switch (code) {
  case ZAN_REFL_M_MRET:
    g->refl_query_used |= ZAN_REFL_QUERY_METHOD_RETURN;
    break;
  case ZAN_REFL_M_MPTYPE:
    g->refl_query_used |= ZAN_REFL_QUERY_METHOD_PARAMS;
    break;
  case ZAN_REFL_M_MATTRS:
    g->refl_query_used |= ZAN_REFL_QUERY_METHOD_ATTRS;
    break;
  case ZAN_REFL_M_MDECLATTRS:
    g->refl_query_used |= ZAN_REFL_QUERY_METHOD_DECLATTRS;
    break;
  case ZAN_REFL_M_CCOUNT:
  case ZAN_REFL_M_CPCOUNT:
    g->refl_query_used |= ZAN_REFL_QUERY_CTORS;
    break;
  case ZAN_REFL_M_CPTYPE:
    g->refl_query_used |= ZAN_REFL_QUERY_CTORS | ZAN_REFL_QUERY_CTOR_PARAMS;
    break;
  case ZAN_REFL_M_FTYPE:
    g->refl_query_used |= ZAN_REFL_QUERY_FIELD_TYPE;
    break;
  case ZAN_REFL_M_FDECLATTRS:
    g->refl_query_used |= ZAN_REFL_QUERY_FIELD_DECLATTRS;
    break;
  case ZAN_REFL_M_FTYPEINFO:
    g->refl_query_used |= ZAN_REFL_QUERY_FIELD_TYPEINFO;
    break;
  case ZAN_REFL_M_FATTRS:
    g->refl_query_used |= ZAN_REFL_QUERY_FIELD_ATTRS;
    break;
  default:
    break;
  }
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
  zan_ast_node_t *arg0 = argc > 0 ? args[0] : NULL;
  zan_ast_node_t *arg1 = argc > 1 ? args[1] : NULL;
  ti = LLVMBuildBitCast(g->builder, ti, i8ptr, "refl.ti");
  switch (code) {
  case ZAN_REFL_M_MCOUNT: /* MethodCount / ConstructorCount */
  case ZAN_REFL_M_CCOUNT: {
    LLVMValueRef c = refl_mi64(g, ti,
                               code == ZAN_REFL_M_MCOUNT ? ZAN_REFL_TBL_METHOD
                                                         : ZAN_REFL_TBL_CTOR,
                               LLVMConstInt(i64, 0, 0), ZAN_REFL_MI_COUNT);
    *out = LLVMBuildTrunc(g->builder, c, i32, "refl.mc32");
    return true;
  }
  case ZAN_REFL_M_MNAME: /* GetMethodName / GetMethodReturnType /
                            GetMethodCustomAttributes */
  case ZAN_REFL_M_MRET:
  case ZAN_REFL_M_MATTRS:
  case ZAN_REFL_M_MDECLATTRS: {
    LLVMValueRef idx = refl_arg_index(g, arg0, locals);
    int mw = ZAN_REFL_MW_NAME;
    if (code == ZAN_REFL_M_MRET)
      mw = ZAN_REFL_MW_RET;
    if (code == ZAN_REFL_M_MATTRS)
      mw = ZAN_REFL_MW_ATTRS;
    if (code == ZAN_REFL_M_MDECLATTRS)
      mw = ZAN_REFL_MW_DECLATTRS;
    *out =
        refl_mstr(g, ti, ZAN_REFL_TBL_METHOD, idx, mw, LLVMConstInt(i64, 0, 0));
    return true;
  }
  case ZAN_REFL_M_MPCOUNT: /* GetMethodParamCount / GetCtorParamCount */
  case ZAN_REFL_M_CPCOUNT: {
    LLVMValueRef idx = refl_arg_index(g, arg0, locals);
    LLVMValueRef c = refl_mi64(g, ti,
                               code == ZAN_REFL_M_MPCOUNT ? ZAN_REFL_TBL_METHOD
                                                          : ZAN_REFL_TBL_CTOR,
                               idx, ZAN_REFL_MI_PCOUNT);
    *out = LLVMBuildTrunc(g->builder, c, i32, "refl.pc32");
    return true;
  }
  case ZAN_REFL_M_MPTYPE: /* GetMethodParamType / GetCtorParamType */
  case ZAN_REFL_M_CPTYPE: {
    LLVMValueRef idx = refl_arg_index(g, arg0, locals);
    LLVMValueRef k = refl_arg_index(g, arg1, locals);
    *out = refl_mstr(g, ti,
                     code == ZAN_REFL_M_MPTYPE ? ZAN_REFL_TBL_METHOD
                                               : ZAN_REFL_TBL_CTOR,
                     idx, ZAN_REFL_MW_PARAM, k);
    return true;
  }
  case ZAN_REFL_M_MHAS: { /* HasMethod(name) */
    LLVMValueRef nm = refl_arg_str(g, arg0, locals);
    *out = LLVMBuildICmp(g->builder, LLVMIntSGE, refl_mfind(g, ti, nm, 0),
                         LLVMConstInt(i64, 0, 0), "refl.mhas");
    /* 实参 temp 归属：拼接/调用产出的 fresh owned 名串在 mfind 消费完
     * 释放；局部变量/字段/字面量是借用，不得动（否则双重释放）。
     * 曾经不管不问——ORM 的 `HasField(a + b)` 每次调用漏一个串
     * （Include 循环里每轮 3 个，probe pQL5/pQL8）。 */
    if (arg0)
      emit_release_owned_call_temp(g, arg0, nm, locals);
    return true;
  }
  case ZAN_REFL_M_MSTATIC: { /* IsMethodStatic(i) */
    LLVMValueRef idx = refl_arg_index(g, arg0, locals);
    LLVMValueRef fl =
        refl_mi64(g, ti, ZAN_REFL_TBL_METHOD, idx, ZAN_REFL_MI_FLAGS);
    *out = LLVMBuildICmp(g->builder, LLVMIntNE,
                         LLVMBuildAnd(g->builder, fl,
                                      LLVMConstInt(i64, ZAN_REFL_MF_STATIC, 0),
                                      "refl.stand"),
                         LLVMConstInt(i64, 0, 0), "refl.isst");
    return true;
  }
  case ZAN_REFL_M_CNEW: { /* CreateInstance(...) */
    /* Record demand at emission, never by scanning dead user bodies. Do not
     * narrow by the receiver's apparent type: TypeInfo flows are dynamic. */
    g->refl_ctor_used = true;
    refl_arg_temps_t temps;
    LLVMValueRef slots = refl_pack_args(g, args, argc, 0, locals, &temps);
    LLVMValueRef idx = zan_call2(
        g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i64}, 2, 0),
        refl_cfind_fn(g),
        (LLVMValueRef[]){ti, LLVMConstInt(i64, (unsigned long long)argc, 0)}, 2,
        "refl.ci");
    LLVMValueRef kout = LLVMBuildAlloca(g->builder, i64, "refl.nk");
    LLVMValueRef v = zan_call_or_invoke(
        g, g->builder,
        LLVMFunctionType(
            i64, (LLVMTypeRef[]){i8ptr, i64, i64, i8ptr, i64ptr, i64ptr}, 6, 0),
        refl_invoke_fn(g),
        (LLVMValueRef[]){ti, LLVMConstInt(i64, ZAN_REFL_TBL_CTOR, 0), idx,
                         LLVMConstNull(i8ptr), slots, kout},
        6, "refl.new");
    refl_release_args(g, args, argc, 0, locals, &temps);
    *out = LLVMBuildIntToPtr(
        g->builder,
        LLVMBuildSelect(
            g->builder,
            LLVMBuildICmp(g->builder, LLVMIntEQ,
                          LLVMBuildLoad2(g->builder, i64, kout, "refl.nkv"),
                          LLVMConstInt(i64, 0, 0), "refl.nofail"),
            LLVMConstInt(i64, 0, 0), v, "refl.nsel"),
        i8ptr, "refl.nobj");
    return true;
  }
  case ZAN_REFL_M_ISARRAY: /* IsArray / IsNullable / IsInterface */
  case ZAN_REFL_M_ISNULLABLE:
  case ZAN_REFL_M_ISIFACE: {
    int want = code == ZAN_REFL_M_ISARRAY      ? ZAN_REFL_TK_ARRAY
               : code == ZAN_REFL_M_ISNULLABLE ? ZAN_REFL_TK_NULLABLE
                                               : ZAN_REFL_TK_INTERFACE;
    *out = LLVMBuildICmp(g->builder, LLVMIntEQ,
                         refl_hdr_i64(g, ti, ZAN_REFL_KIND_OFF, "refl.tk"),
                         LLVMConstInt(i64, (unsigned long long)want, 0),
                         "refl.iskind");
    return true;
  }
  case ZAN_REFL_M_ISGENERIC: /* IsGeneric */
    *out = LLVMBuildIsNotNull(
        g->builder, refl_hdr_ptr(g, ti, ZAN_REFL_TARGS_OFF, "refl.ta"),
        "refl.isgen");
    return true;
  case ZAN_REFL_M_ELEMTYPE: { /* ElementType */
    LLVMValueRef e = refl_hdr_ptr(g, ti, ZAN_REFL_ELEM_OFF, "refl.el");
    *out = LLVMBuildSelect(g->builder,
                           LLVMBuildIsNull(g->builder, e, "refl.elnull"),
                           refl_meta_for(g, NULL, NULL, 0), e, "refl.elt");
    return true;
  }
  case ZAN_REFL_M_TACOUNT: { /* TypeArgCount */
    LLVMValueRef c = zan_call2(
        g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i64, i64}, 3, 0),
        refl_tainfo_fn(g),
        (LLVMValueRef[]){ti, LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0)},
        3, "refl.tac");
    *out = LLVMBuildTrunc(g->builder, c, i32, "refl.tac32");
    return true;
  }
  case ZAN_REFL_M_TARG: { /* GetTypeArg(i) */
    LLVMValueRef idx = refl_arg_index(g, arg0, locals);
    LLVMValueRef v = zan_call2(
        g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i64, i64}, 3, 0),
        refl_tainfo_fn(g), (LLVMValueRef[]){ti, idx, LLVMConstInt(i64, 1, 0)},
        3, "refl.tav");
    *out = LLVMBuildSelect(
        g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, v, LLVMConstInt(i64, 0, 0),
                      "refl.tanull"),
        refl_meta_for(g, NULL, NULL, 0),
        LLVMBuildIntToPtr(g->builder, v, i8ptr, "refl.tap"), "refl.targ");
    return true;
  }
  }
  switch (code) {
  case ZAN_REFL_M_NAME:
    *out = ti;
    return true;
  case ZAN_REFL_M_FULLNAME: /* B-ID107：全限定名，-112 槽 */
    *out = refl_hdr_ptr(g, ti, ZAN_REFL_FULLNAME_OFF, "refl.full");
    return true;
  case ZAN_REFL_M_DECLATTRS:
    *out = refl_hdr_ptr(g, ti, ZAN_REFL_DECLATTRS_OFF, "refl.declattrs");
    return true;
  case ZAN_REFL_M_TATTRS:
    *out = refl_hdr_ptr(g, ti, ZAN_REFL_ATTRS_OFF, "refl.tattrs");
    return true;
  case ZAN_REFL_M_KIND: { /* Kind */
    LLVMValueRef k = refl_hdr_i64(g, ti, ZAN_REFL_KIND_OFF, "refl.tk");
    const char *names[] = {"other",     "class",     "struct", "enum",
                           "interface", "primitive", "other",  "array",
                           "nullable",  "delegate"};
    LLVMValueRef v = refl_const_string(g, names[0], 5);
    for (int i = 1; i <= ZAN_REFL_TK_LAST; i++) {
      LLVMValueRef s = refl_const_string(g, names[i], (int)strlen(names[i]));
      v = LLVMBuildSelect(
          g->builder,
          LLVMBuildICmp(g->builder, LLVMIntEQ, k,
                        LLVMConstInt(i64, (unsigned long long)i, 0), "refl.kq"),
          s, v, "refl.kn");
    }
    *out = v;
    return true;
  }
  case ZAN_REFL_M_FCOUNT: { /* FieldCount */
    LLVMValueRef c = refl_hdr_i64(g, ti, ZAN_REFL_COUNT_OFF, "refl.fc");
    *out = LLVMBuildTrunc(g->builder, c, i32, "refl.fc32");
    return true;
  }
  case ZAN_REFL_M_FNAME:
  case ZAN_REFL_M_FTYPE:
  case ZAN_REFL_M_FATTRS:
  case ZAN_REFL_M_FDECLATTRS: {
    LLVMValueRef idx =
        arg0 ? emit_expr(g, arg0, locals) : LLVMConstInt(i64, 0, 0);
    idx = emit_index_i64(g, idx, "refl.i");
    int which = ZAN_REFL_WHICH_NAME;
    if (code == ZAN_REFL_M_FTYPE)
      which = ZAN_REFL_WHICH_TYPE;
    else if (code == ZAN_REFL_M_FDECLATTRS)
      which = ZAN_REFL_WHICH_DECLATTRS;
    else if (code == ZAN_REFL_M_FATTRS)
      which = ZAN_REFL_WHICH_ATTRS;
    *out = zan_call2(
        g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i64, i64}, 3, 0),
        refl_fname_fn(g),
        (LLVMValueRef[]){ti, idx,
                         LLVMConstInt(i64, (unsigned long long)which, 0)},
        3, "refl.fname");
    return true;
  }
  case ZAN_REFL_M_FTYPEINFO: { /* GetFieldTypeInfo(idx) -> TypeInfo */
    LLVMValueRef idx =
        arg0 ? emit_expr(g, arg0, locals) : LLVMConstInt(i64, 0, 0);
    idx = emit_index_i64(g, idx, "refl.i");
    LLVMValueRef value = zan_call2(
        g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i64, i64}, 3, 0),
        refl_field_scalar_fn(g),
        (LLVMValueRef[]){ti, idx, LLVMConstInt(i64, 1, 0)}, 3, "refl.typeinfo");
    *out = LLVMBuildIntToPtr(g->builder, value, i8ptr, "refl.fti");
    return true;
  }
  case ZAN_REFL_M_HAS: { /* HasField(name) */
    LLVMValueRef nm = arg0 ? emit_expr(g, arg0, locals) : refl_empty_string(g);
    LLVMValueRef found = zan_call2(
        g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
        refl_find_fn(g),
        (LLVMValueRef[]){ti,
                         LLVMBuildBitCast(g->builder, nm, i8ptr, "refl.nm")},
        2, "refl.f");
    /* 实参 temp 归属：同 M_MHAS——fresh owned 名串消费完释放，
     * 借用实参不动。 */
    if (arg0)
      emit_release_owned_call_temp(g, arg0, nm, locals);
    *out = LLVMBuildICmp(g->builder, LLVMIntSGE, found, LLVMConstInt(i64, 0, 0),
                         "refl.has");
    return true;
  }
  case ZAN_REFL_M_FFIND: { /* FindField(name) -> int */
    LLVMValueRef nm = refl_arg_str(g, arg0, locals);
    LLVMValueRef found = zan_call2(
        g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
        refl_find_fn(g), (LLVMValueRef[]){ti, nm}, 2, "refl.ff");
    if (arg0)
      emit_release_owned_call_temp(g, arg0, nm, locals);
    *out = LLVMBuildTrunc(g->builder, found, i32, "refl.ff32");
    return true;
  }
  case ZAN_REFL_M_MFIND: { /* FindMethod(name) -> int */
    LLVMValueRef nm = refl_arg_str(g, arg0, locals);
    LLVMValueRef found = refl_mfind(g, ti, nm, 0);
    if (arg0)
      emit_release_owned_call_temp(g, arg0, nm, locals);
    *out = LLVMBuildTrunc(g->builder, found, i32, "refl.mf32");
    return true;
  }
  case ZAN_REFL_M_SIVOID:
  case ZAN_REFL_M_SIINT:
  case ZAN_REFL_M_SILONG:
  case ZAN_REFL_M_SIDOUBLE:
  case ZAN_REFL_M_SISTRING:
  case ZAN_REFL_M_SIOBJECT:
  case ZAN_REFL_M_SITASK: {
    LLVMValueRef nm = refl_arg_str(g, arg0, locals);
    refl_note_invoke_name(g, arg0);
    LLVMValueRef idx = refl_mfind(g, ti, nm, ZAN_REFL_MF_STATIC);
    if (arg0)
      emit_release_owned_call_temp(g, arg0, nm, locals);
    refl_arg_temps_t temps;
    LLVMValueRef slots = refl_pack_args(g, args, argc, 1, locals, &temps);
    LLVMValueRef kout = LLVMBuildAlloca(g->builder, i64, "refl.sik");
    LLVMValueRef v = zan_call_or_invoke(
        g, g->builder,
        LLVMFunctionType(
            i64, (LLVMTypeRef[]){i8ptr, i64, i64, i8ptr, i64ptr, i64ptr}, 6, 0),
        refl_invoke_fn(g),
        (LLVMValueRef[]){ti, LLVMConstInt(i64, ZAN_REFL_TBL_METHOD, 0), idx,
                         LLVMConstNull(i8ptr), slots, kout},
        6, "refl.siv");
    refl_release_args(g, args, argc, 1, locals, &temps);
    int rk = code == ZAN_REFL_M_SIVOID     ? 0
             : code == ZAN_REFL_M_SIINT    ? 1
             : code == ZAN_REFL_M_SILONG   ? 2
             : code == ZAN_REFL_M_SIDOUBLE ? 3
             : code == ZAN_REFL_M_SISTRING ? 4
             : code == ZAN_REFL_M_SIOBJECT ? 5
                                           : 6;
    *out = refl_emit_unpack_invoke_ret(g, rk, v, kout);
    return true;
  }
  case ZAN_REFL_M_SGINT:
  case ZAN_REFL_M_SGLONG:
  case ZAN_REFL_M_SGBOOL: {
    g->refl_sfield_used = true;
    LLVMValueRef nm = refl_arg_str(g, arg0, locals);
    LLVMValueRef v = zan_call2(
        g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
        refl_sget_i64_fn(g), (LLVMValueRef[]){ti, nm}, 2, "refl.sg64");
    if (arg0)
      emit_release_owned_call_temp(g, arg0, nm, locals);
    if (code == ZAN_REFL_M_SGBOOL)
      *out = LLVMBuildICmp(g->builder, LLVMIntNE, v, LLVMConstInt(i64, 0, 0),
                           "refl.sgb");
    else if (code == ZAN_REFL_M_SGINT)
      *out = LLVMBuildTrunc(g->builder, v, i32, "refl.sgi32");
    else
      *out = v;
    return true;
  }
  case ZAN_REFL_M_SGDOUBLE: {
    g->refl_sfield_used = true;
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    LLVMValueRef nm = refl_arg_str(g, arg0, locals);
    *out = zan_call2(
        g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
        refl_sget_f64_fn(g), (LLVMValueRef[]){ti, nm}, 2, "refl.sgf64");
    if (arg0)
      emit_release_owned_call_temp(g, arg0, nm, locals);
    return true;
  }
  case ZAN_REFL_M_SGSTRING: {
    g->refl_sfield_used = true;
    LLVMValueRef nm = refl_arg_str(g, arg0, locals);
    *out = zan_call2(
        g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
        refl_sget_str_fn(g), (LLVMValueRef[]){ti, nm}, 2, "refl.sgstr");
    if (arg0)
      emit_release_owned_call_temp(g, arg0, nm, locals);
    return true;
  }
  case ZAN_REFL_M_SGOBJECT: {
    g->refl_sfield_used = true;
    LLVMValueRef nm = refl_arg_str(g, arg0, locals);
    *out = zan_call2(
        g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
        refl_sget_obj_fn(g), (LLVMValueRef[]){ti, nm}, 2, "refl.sgobj");
    if (arg0)
      emit_release_owned_call_temp(g, arg0, nm, locals);
    return true;
  }
  case ZAN_REFL_M_SSINT:
  case ZAN_REFL_M_SSLONG:
  case ZAN_REFL_M_SSDOUBLE:
  case ZAN_REFL_M_SSSTRING:
  case ZAN_REFL_M_SSBOOL:
  case ZAN_REFL_M_SSOBJECT: {
    g->refl_sfield_used = true;
    LLVMValueRef nm = refl_arg_str(g, arg0, locals);
    LLVMValueRef v = arg1 ? emit_expr(g, arg1, locals) : NULL;
    LLVMValueRef vk = NULL, box = NULL;
    LLVMValueRef slot = refl_pack_single_val(g, arg1, v, locals, &vk, &box);
    LLVMValueRef ok = zan_call2(
        g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i64, i64}, 4, 0),
        refl_sset_fn(g), (LLVMValueRef[]){ti, nm, slot, vk}, 4, "refl.sset");
    if (arg0)
      emit_release_owned_call_temp(g, arg0, nm, locals);
    if (box)
      emit_release_obj_value(g, box);
    else if (arg1 && v)
      emit_release_owned_call_temp(g, arg1, v, locals);
    *out = LLVMBuildICmp(g->builder, LLVMIntNE, ok, LLVMConstInt(i64, 0, 0),
                         "refl.ssetok");
    return true;
  }
  default: { /* GetMemberValue(i) */
    LLVMValueRef idx =
        arg0 ? emit_expr(g, arg0, locals) : LLVMConstInt(i64, 0, 0);
    idx = emit_index_i64(g, idx, "refl.i");
    *out =
        zan_call2(g->builder,
                  LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i64, i64}, 3, 0),
                  refl_field_scalar_fn(g),
                  (LLVMValueRef[]){ti, idx, LLVMConstInt(i64, 2, 0)}, 3,
                  "refl.membervalue");
    return true;
  }
  }
}

/* `obj */
static bool refl_emit_instance_call_1(zan_irgen_t *g, zan_type_t *rt,
                                      LLVMValueRef recv, zan_istr_t name,
                                      zan_ast_node_t **args, int argc,
                                      local_scope_t *locals, LLVMValueRef *out,
                                      LLVMValueRef *nm_tmp,
                                      LLVMValueRef *val_tmp,
                                      LLVMValueRef *box_tmp) {
  int code = 0;
  if (!zan_refl_instance_method(name, &code))
    return false;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  zan_ast_node_t *arg0 = argc > 0 ? args[0] : NULL;
  zan_ast_node_t *arg1 = argc > 1 ? args[1] : NULL;
  if (code == ZAN_REFL_M_GETTYPE) {
    *out = refl_emit_get_type(g, rt, recv);
    return true;
  }
  LLVMValueRef ti = refl_emit_get_type(g, rt, recv);
  LLVMValueRef obj = refl_receiver_ptr(g, rt, recv);
  if (!rt || rt->kind != TYPE_STRUCT) {
    LLVMValueRef is_boxed = LLVMBuildAnd(
        g->builder, LLVMBuildIsNotNull(g->builder, obj, "refl.objnn"),
        LLVMBuildICmp(g->builder, LLVMIntEQ,
                      refl_hdr_i64(g, ti, ZAN_REFL_KIND_OFF, "refl.otk"),
                      LLVMConstInt(i64, ZAN_REFL_TK_STRUCT, 0), "refl.isbox"),
        "refl.shift8");
    LLVMValueRef eight = LLVMConstInt(i64, 8, 0);
    LLVMValueRef obj8 =
        LLVMBuildGEP2(g->builder, i8, obj, &eight, 1, "refl.obj8");
    obj = LLVMBuildSelect(g->builder, is_boxed, obj8, obj, "refl.objadj");
  }
  LLVMTypeRef three_at[3] = {i8ptr, i8ptr, i64};
  if (code == ZAN_REFL_M_OINT_AT || code == ZAN_REFL_M_OLONG_AT ||
      code == ZAN_REFL_M_OBOOL_AT) {
    g->refl_field_rw_used = true;
    LLVMValueRef idx = refl_arg_index(g, arg0, locals);
    LLVMValueRef v =
        zan_call2(g->builder, LLVMFunctionType(i64, three_at, 3, 0),
                  refl_get_i64_at_fn(g), (LLVMValueRef[]){ti, obj, idx}, 3,
                  "refl.i64at");
    if (code == ZAN_REFL_M_OBOOL_AT)
      *out = LLVMBuildICmp(g->builder, LLVMIntNE, v, LLVMConstInt(i64, 0, 0),
                           "refl.boolat");
    else if (code == ZAN_REFL_M_OINT_AT)
      *out = LLVMBuildTrunc(g->builder, v, i32, "refl.i32at");
    else
      *out = v;
    return true;
  }
  if (code == ZAN_REFL_M_ODOUBLE_AT) {
    g->refl_field_rw_used = true;
    LLVMValueRef idx = refl_arg_index(g, arg0, locals);
    *out = zan_call2(g->builder, LLVMFunctionType(dbl, three_at, 3, 0),
                     refl_get_f64_at_fn(g), (LLVMValueRef[]){ti, obj, idx}, 3,
                     "refl.f64at");
    return true;
  }
  if (code == ZAN_REFL_M_OSTRING_AT) {
    g->refl_field_rw_used = true;
    LLVMValueRef idx = refl_arg_index(g, arg0, locals);
    *out = zan_call2(g->builder, LLVMFunctionType(i8ptr, three_at, 3, 0),
                     refl_get_str_at_fn(g), (LLVMValueRef[]){ti, obj, idx}, 3,
                     "refl.strat");
    return true;
  }
  if (code == ZAN_REFL_M_OOBJECT_AT) {
    g->refl_field_rw_used = true;
    LLVMValueRef idx = refl_arg_index(g, arg0, locals);
    *out = zan_call2(g->builder, LLVMFunctionType(i8ptr, three_at, 3, 0),
                     refl_get_obj_at_fn(g), (LLVMValueRef[]){ti, obj, idx}, 3,
                     "refl.objat");
    return true;
  }
  if (code == ZAN_REFL_M_SETINT_AT || code == ZAN_REFL_M_SETLONG_AT ||
      code == ZAN_REFL_M_SETBOOL_AT || code == ZAN_REFL_M_SETDOUBLE_AT ||
      code == ZAN_REFL_M_SETSTRING_AT || code == ZAN_REFL_M_SETOBJECT_AT) {
    g->refl_field_rw_used = true;
    LLVMValueRef idx = refl_arg_index(g, arg0, locals);
    LLVMValueRef v = arg1 ? emit_expr(g, arg1, locals) : NULL;
    LLVMValueRef vk = NULL, box = NULL;
    LLVMValueRef slot = refl_pack_single_val(g, arg1, v, locals, &vk, &box);
    if (box)
      *box_tmp = box;
    else if (arg1)
      *val_tmp = v;
    LLVMValueRef ok = zan_call2(
        g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i64, i64, i64}, 5,
                         0),
        refl_set_at_fn(g), (LLVMValueRef[]){ti, obj, idx, slot, vk}, 5,
        "refl.setat");
    *out = LLVMBuildICmp(g->builder, LLVMIntNE, ok, LLVMConstInt(i64, 0, 0),
                         "refl.setok");
    return true;
  }
  bool is_invoke_at =
      (code == ZAN_REFL_M_IVOID_AT || code == ZAN_REFL_M_IINT_AT ||
       code == ZAN_REFL_M_ILONG_AT || code == ZAN_REFL_M_IDOUBLE_AT ||
       code == ZAN_REFL_M_ISTRING_AT || code == ZAN_REFL_M_IOBJECT_AT ||
       code == ZAN_REFL_M_ITASK_AT);
  bool is_invoke = is_invoke_at ||
                   (code == ZAN_REFL_M_IVOID || code == ZAN_REFL_M_IINT ||
                    code == ZAN_REFL_M_ILONG || code == ZAN_REFL_M_IDOUBLE ||
                    code == ZAN_REFL_M_ISTRING || code == ZAN_REFL_M_IOBJECT ||
                    code == ZAN_REFL_M_ITASK);
  if (is_invoke) {
    LLVMValueRef idx = NULL;
    if (is_invoke_at) {
      g->refl_invoke_unknown = true;
      idx = refl_arg_index(g, arg0, locals);
    } else {
      LLVMValueRef nm =
          arg0 ? emit_expr(g, arg0, locals) : refl_empty_string(g);
      if (arg0)
        *nm_tmp = nm;
      nm = LLVMBuildBitCast(g->builder, nm, i8ptr, "refl.nm");
      refl_note_invoke_name(g, arg0);
      idx = refl_mfind(g, ti, nm, 0);
    }
    refl_arg_temps_t temps;
    LLVMValueRef slots = refl_pack_args(g, args, argc, 1, locals, &temps);
    LLVMValueRef kout = LLVMBuildAlloca(g->builder, i64, "refl.ik");
    LLVMValueRef v = zan_call_or_invoke(
        g, g->builder,
        LLVMFunctionType(
            i64, (LLVMTypeRef[]){i8ptr, i64, i64, i8ptr, i64ptr, i64ptr}, 6, 0),
        refl_invoke_fn(g),
        (LLVMValueRef[]){ti, LLVMConstInt(i64, ZAN_REFL_TBL_METHOD, 0), idx,
                         obj, slots, kout},
        6, "refl.iv");
    refl_release_args(g, args, argc, 1, locals, &temps);
    int rk = (code == ZAN_REFL_M_IVOID || code == ZAN_REFL_M_IVOID_AT)     ? 0
             : (code == ZAN_REFL_M_IINT || code == ZAN_REFL_M_IINT_AT)     ? 1
             : (code == ZAN_REFL_M_ILONG || code == ZAN_REFL_M_ILONG_AT)   ? 2
             : (code == ZAN_REFL_M_IDOUBLE || code == ZAN_REFL_M_IDOUBLE_AT) ? 3
             : (code == ZAN_REFL_M_ISTRING || code == ZAN_REFL_M_ISTRING_AT) ? 4
             : (code == ZAN_REFL_M_IOBJECT || code == ZAN_REFL_M_IOBJECT_AT) ? 5
                                                                           : 6;
    *out = refl_emit_unpack_invoke_ret(g, rk, v, kout);
    return true;
  }
  LLVMValueRef nm = arg0 ? emit_expr(g, arg0, locals) : refl_empty_string(g);
  if (arg0)
    *nm_tmp = nm;
  nm = LLVMBuildBitCast(g->builder, nm, i8ptr, "refl.nm");
  LLVMTypeRef three[3] = {i8ptr, i8ptr, i8ptr};
  if (code == ZAN_REFL_M_SETINT || code == ZAN_REFL_M_SETLONG ||
      code == ZAN_REFL_M_SETBOOL || code == ZAN_REFL_M_SETDOUBLE ||
      code == ZAN_REFL_M_SETSTRING || code == ZAN_REFL_M_SETOBJECT) {
    g->refl_field_rw_used = true;
    LLVMValueRef v = arg1 ? emit_expr(g, arg1, locals) : NULL;
    LLVMValueRef vk = NULL, box = NULL;
    LLVMValueRef slot = refl_pack_single_val(g, arg1, v, locals, &vk, &box);
    if (box)
      *box_tmp = box;
    else if (arg1)
      *val_tmp = v;
    LLVMValueRef ok = zan_call2(
        g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr, i8ptr, i64, i64}, 5,
                         0),
        refl_set_fn(g), (LLVMValueRef[]){ti, obj, nm, slot, vk}, 5, "refl.set");
    *out = LLVMBuildICmp(g->builder, LLVMIntNE, ok, LLVMConstInt(i64, 0, 0),
                         "refl.setok");
    return true;
  }
  if (code == ZAN_REFL_M_OMHAS) { /* HasMethod(name) */
    *out = LLVMBuildICmp(g->builder, LLVMIntSGE, refl_mfind(g, ti, nm, 0),
                         LLVMConstInt(i64, 0, 0), "refl.omhas");
    return true;
  }
  if (code == ZAN_REFL_M_OBOOL) { /* GetFieldBool(name) */
    g->refl_field_rw_used = true;
    LLVMValueRef v = zan_call2(g->builder, LLVMFunctionType(i64, three, 3, 0),
                               refl_get_i64_fn(g),
                               (LLVMValueRef[]){ti, obj, nm}, 3, "refl.b64");
    *out = LLVMBuildICmp(g->builder, LLVMIntNE, v, LLVMConstInt(i64, 0, 0),
                         "refl.bool");
    return true;
  }
  if (code == ZAN_REFL_M_OHAS) {
    LLVMValueRef found = zan_call2(
        g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0),
        refl_find_fn(g), (LLVMValueRef[]){ti, nm}, 2, "refl.f");
    *out = LLVMBuildICmp(g->builder, LLVMIntSGE, found, LLVMConstInt(i64, 0, 0),
                         "refl.has");
    return true;
  }
  if (code == ZAN_REFL_M_ODOUBLE) {
    g->refl_field_rw_used = true;
    *out = zan_call2(g->builder, LLVMFunctionType(dbl, three, 3, 0),
                     refl_get_f64_fn(g), (LLVMValueRef[]){ti, obj, nm}, 3,
                     "refl.f64");
    return true;
  }
  if (code == ZAN_REFL_M_OSTRING) {
    g->refl_field_rw_used = true;
    *out = zan_call2(g->builder, LLVMFunctionType(i8ptr, three, 3, 0),
                     refl_get_str_fn(g), (LLVMValueRef[]){ti, obj, nm}, 3,
                     "refl.str");
    return true;
  }
  if (code == ZAN_REFL_M_OOBJECT) {
    g->refl_field_rw_used = true;
    *out = zan_call2(g->builder, LLVMFunctionType(i8ptr, three, 3, 0),
                     refl_get_obj_fn(g), (LLVMValueRef[]){ti, obj, nm}, 3,
                     "refl.obj");
    return true;
  }
  g->refl_field_rw_used = true;
  LLVMValueRef v = zan_call2(g->builder, LLVMFunctionType(i64, three, 3, 0),
                             refl_get_i64_fn(g), (LLVMValueRef[]){ti, obj, nm},
                             3, "refl.i64");
  *out = code == ZAN_REFL_M_OINT
             ? LLVMBuildTrunc(g->builder, v, i32, "refl.i32")
             : v;
  return true;
}
bool refl_emit_instance_call(zan_irgen_t *g, zan_type_t *rt, LLVMValueRef recv,
                             zan_istr_t name, zan_ast_node_t **args, int argc,
                             local_scope_t *locals, LLVMValueRef *out) {
  LLVMValueRef nm_tmp = NULL, val_tmp = NULL, box_tmp = NULL;
  if (!refl_emit_instance_call_1(g, rt, recv, name, args, argc, locals, out,
                                 &nm_tmp, &val_tmp, &box_tmp))
    return false;
  if (nm_tmp)
    emit_release_owned_call_temp(g, args[0], nm_tmp, locals);
  if (box_tmp)
    emit_release_obj_value(g, box_tmp);
  else if (val_tmp)
    emit_release_owned_call_temp(g, args[1], val_tmp, locals);
  return true;
}
bool refl_needs_site_meta(zan_irgen_t *g) {
  /* typeof metadata has its own recursive roots. Only emitted object
   * reflection needs every allocation site's concrete type; the helper is
   * shared by GetType and object Get/Set/Invoke, including unknown receivers.
   * Once emitted, demand stays conservative even if optimization removes it. */
  return g->refl_used &&
         (!g->publish_reflect_demand || g->emit_lib ||
          g->fn_refl_obj_type != NULL);
}

void emit_site_meta_table(zan_irgen_t *g) {
  if (!refl_needs_site_meta(g) || !g->g_site_meta || !g->site_syms)
    return;
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  int n = g->leak_site_count;
  if (n <= 0)
    return;
  LLVMValueRef *elems = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
  if (!elems)
    return;
  for (int i = 0; i < n; i++) {
    elems[i] = LLVMConstNull(i8ptr);
    if (g->site_coll && g->site_coll[i]) {
      if (g->site_coll[i] == 1) {
        zan_type_t *elem_type = g->site_coll_elem ? g->site_coll_elem[i] : NULL;
        zan_type_t *lt = zan_binder_make_list_type(g->binder, elem_type);
        elems[i] = refl_meta_for(g, lt, NULL, 0);
      }
      continue;
    }
    zan_symbol_t *sym = g->site_syms[i];
    if (!sym || !sym->type)
      continue;
    zan_type_t *t =
        (g->site_inst && g->site_inst[i]) ? g->site_inst[i] : sym->type;
    elems[i] = refl_meta_for(g, t, NULL, 0);
  }
  LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)n);
  LLVMValueRef arr = LLVMAddGlobal(g->mod, at, "__zan_site_meta_tbl");
  LLVMSetInitializer(arr, LLVMConstArray(i8ptr, elems, (unsigned)n));
  LLVMSetLinkage(arr, LLVMInternalLinkage);
  LLVMSetInitializer(g->g_site_meta,
                     LLVMConstBitCast(arr, LLVMPointerType(i8ptr, 0)));
  free(elems);
}
typedef struct {
  LLVMValueRef gv;
  LLVMTypeRef at;
  LLVMValueRef *ptrs;
  int k;
} zan_tynames_cache_ent_t;

static LLVMValueRef intern_desc_tynames_ptr(zan_irgen_t *g, LLVMValueRef *nptr,
                                            int k, int site_idx,
                                            LLVMTypeRef i8ptr, LLVMTypeRef i64,
                                            zan_tynames_cache_ent_t **cache,
                                            int *cnt, int *cap) {
  for (int c = 0; c < *cnt; c++) {
    if ((*cache)[c].k != k)
      continue;
    if (memcmp((*cache)[c].ptrs, nptr, (size_t)k * sizeof(LLVMValueRef)) == 0) {
      LLVMValueRef idxs[] = {LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0)};
      return LLVMBuildGEP2(g->builder, (*cache)[c].at, (*cache)[c].gv, idxs, 2,
                           "tn.ptr");
    }
  }
  LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)k + 1);
  char gname[64];
  snprintf(gname, sizeof(gname), "__zan_tynames_%d", site_idx);
  LLVMValueRef gv = LLVMAddGlobal(g->mod, at, gname);
  LLVMSetInitializer(gv, LLVMConstArray(i8ptr, nptr, (unsigned)k + 1));
  LLVMSetLinkage(gv, LLVMInternalLinkage);
  LLVMSetGlobalConstant(gv, 1);
  LLVMSetUnnamedAddr(gv, LLVMGlobalUnnamedAddr);
  if (*cnt >= *cap) {
    int ncap = *cap ? (*cap * 2) : 32;
    zan_tynames_cache_ent_t *nb =
        (zan_tynames_cache_ent_t *)realloc(*cache, (size_t)ncap * sizeof(*nb));
    if (nb) {
      *cache = nb;
      *cap = ncap;
    }
  }
  if (*cnt < *cap) {
    LLVMValueRef *copy =
        (LLVMValueRef *)malloc((size_t)k * sizeof(LLVMValueRef));
    if (copy) {
      memcpy(copy, nptr, (size_t)k * sizeof(LLVMValueRef));
      (*cache)[*cnt].gv = gv;
      (*cache)[*cnt].at = at;
      (*cache)[*cnt].ptrs = copy;
      (*cache)[*cnt].k = k;
      (*cnt)++;
    }
  }
  LLVMValueRef idxs[] = {LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0)};
  return LLVMBuildGEP2(g->builder, at, gv, idxs, 2, "tn.ptr");
}

void zan_irgen_emit_arc_desc_init(zan_irgen_t *g, bool need_field_graph) {
  if (!g->desc_hdr || !g->desc_gv)
    return;
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  zan_tynames_cache_ent_t *tn_cache = NULL;
  int tn_cnt = 0, tn_cap = 0;
  for (int i = 0; i < g->leak_site_count; i++) {
    LLVMValueRef dg = g->desc_gv[i];
    if (!dg)
      continue;
    LLVMTypeRef dt = LLVMGlobalGetValueType(dg);
    LLVMValueRef dtor = LLVMConstNull(i8ptr);
    LLVMValueRef tynames = LLVMConstNull(i8ptr);
    LLVMValueRef meta = LLVMConstNull(i8ptr);
    if (g->site_coll && g->site_coll[i]) {
      LLVMValueRef fn = get_collection_release_decl(g, i);
      if (fn)
        dtor = LLVMConstBitCast(fn, i8ptr);
      const char *cn = arc_collection_type_name(
          g, g->site_coll[i], g->site_coll_elem ? g->site_coll_elem[i] : NULL);
      LLVMValueRef nptr[2] = {zan_irgen_intern_string(g, cn),
                              LLVMConstNull(i8ptr)};
      tynames = intern_desc_tynames_ptr(g, nptr, 1, i, i8ptr, i64, &tn_cache,
                                        &tn_cnt, &tn_cap);
      if (refl_needs_site_meta(g) && g->site_coll[i] == 1) {
        zan_type_t *elem_type = g->site_coll_elem ? g->site_coll_elem[i] : NULL;
        zan_type_t *lt = zan_binder_make_list_type(g->binder, elem_type);
        meta = refl_meta_for(g, lt, NULL, 0);
      }
    } else if (g->site_syms[i]) {
      zan_symbol_t *sym = g->site_syms[i];
      LLVMValueRef fn =
          get_class_release_decl(g, sym, g->site_inst ? g->site_inst[i] : NULL);
      if (fn)
        dtor = LLVMConstBitCast(fn, i8ptr);
      const char *names[64];
      int k = 0;
      zan_symbol_t *cur = sym;
      while (cur && k < 63) {
        char buf[320];
        int len = (int)cur->name.len;
        if (len > 319)
          len = 319;
        memcpy(buf, cur->name.str, (size_t)len);
        buf[len] = 0;
        names[k++] = zan_arena_strdup(g->arena, buf, (size_t)len);
        if (cur->type) {
          for (int ifi = 0; ifi < cur->type->interface_count && k < 63; ifi++) {
            zan_type_t *iface = cur->type->interfaces[ifi];
            if (iface && iface->sym) {
              int ilen = (int)iface->sym->name.len;
              if (ilen > 319)
                ilen = 319;
              char ibuf[320];
              memcpy(ibuf, iface->sym->name.str, (size_t)ilen);
              ibuf[ilen] = 0;
              names[k++] = zan_arena_strdup(g->arena, ibuf, (size_t)ilen);
            }
          }
        }
        cur = (cur->type && cur->type->base_type) ? cur->type->base_type->sym
                                                  : NULL;
      }
      if (k > 0) {
        LLVMValueRef *nptr =
            (LLVMValueRef *)calloc((size_t)k + 1, sizeof(LLVMValueRef));
        for (int j = 0; j < k; j++)
          nptr[j] = zan_irgen_intern_string(g, names[j]);
        nptr[k] = LLVMConstNull(i8ptr);
        tynames = intern_desc_tynames_ptr(g, nptr, k, i, i8ptr, i64, &tn_cache,
                                          &tn_cnt, &tn_cap);
        free(nptr);
      }
      if (refl_needs_site_meta(g) && sym->type) {
        zan_type_t *t =
            (g->site_inst && g->site_inst[i]) ? g->site_inst[i] : sym->type;
        meta = refl_meta_for(g, t, NULL, 0);
      }
    }
    LLVMValueRef fg =
        need_field_graph ? build_site_field_graph(g, i) : LLVMConstNull(i8ptr);
    LLVMValueRef fields[4] = {dtor, tynames, meta, fg};
    LLVMSetInitializer(dg, LLVMConstNamedStruct(dt, fields, 4));
  }
  for (int c = 0; c < tn_cnt; c++)
    free(tn_cache[c].ptrs);
  free(tn_cache);
}
