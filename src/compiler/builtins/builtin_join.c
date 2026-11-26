LLVMValueRef get_str_join_fn(zan_irgen_t *g) {
  LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_join");
  if (fn)
    return fn;
  LLVMContextRef c = g->ctx;
  LLVMBuilderRef b = g->builder;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i1 = LLVMInt1TypeInContext(c);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
  LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
  LLVMTypeRef fnty =
      LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr}, 2, 0);
  fn = LLVMAddFunction(g->mod, "__zan_str_join", fnty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  {
    unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
    if (nw)
      LLVMAddAttributeAtIndex(fn, LLVMAttributeFunctionIndex,
                              LLVMCreateEnumAttribute(c, nw, 0));
  }
  LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr}, 1, 0);
  LLVMTypeRef memcpy_ty =
      LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr, i64}, 3, 0);
  LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);
  LLVMBasicBlockRef saved = LLVMGetInsertBlock(b);
  LLVMValueRef saved_fn = g->current_fn;
  g->current_fn = fn;

  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
  LLVMBasicBlockRef empty_bb = LLVMAppendBasicBlockInContext(c, fn, "empty");
  LLVMBasicBlockRef chk1_bb = LLVMAppendBasicBlockInContext(c, fn, "chk1");
  LLVMBasicBlockRef one_bb = LLVMAppendBasicBlockInContext(c, fn, "one");
  LLVMBasicBlockRef one_nonnull =
      LLVMAppendBasicBlockInContext(c, fn, "one.nn");
  LLVMBasicBlockRef one_ret = LLVMAppendBasicBlockInContext(c, fn, "one.ret");
  LLVMBasicBlockRef one_copy = LLVMAppendBasicBlockInContext(c, fn, "one.cp");
  LLVMBasicBlockRef multi_bb = LLVMAppendBasicBlockInContext(c, fn, "multi");
  LLVMBasicBlockRef m_cond = LLVMAppendBasicBlockInContext(c, fn, "m.cond");
  LLVMBasicBlockRef m_body = LLVMAppendBasicBlockInContext(c, fn, "m.body");
  LLVMBasicBlockRef m_nonnull = LLVMAppendBasicBlockInContext(c, fn, "m.nn");
  LLVMBasicBlockRef m_step = LLVMAppendBasicBlockInContext(c, fn, "m.step");
  LLVMBasicBlockRef c_pre = LLVMAppendBasicBlockInContext(c, fn, "c.pre");
  LLVMBasicBlockRef c_cond = LLVMAppendBasicBlockInContext(c, fn, "c.cond");
  LLVMBasicBlockRef c_body = LLVMAppendBasicBlockInContext(c, fn, "c.body");
  LLVMBasicBlockRef c_len_fast =
      LLVMAppendBasicBlockInContext(c, fn, "c.lfast");
  LLVMBasicBlockRef c_len_slow =
      LLVMAppendBasicBlockInContext(c, fn, "c.lslow");
  LLVMBasicBlockRef c_copy = LLVMAppendBasicBlockInContext(c, fn, "c.copy");
  LLVMBasicBlockRef c_sep = LLVMAppendBasicBlockInContext(c, fn, "c.sep");
  LLVMBasicBlockRef c_next = LLVMAppendBasicBlockInContext(c, fn, "c.next");
  LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(c, fn, "done");

  LLVMPositionBuilderAtEnd(b, entry);
  LLVMValueRef zero64 = LLVMConstInt(i64, 0, 0);
  LLVMValueRef one64 = LLVMConstInt(i64, 1, 0);
  LLVMValueRef sep = emit_str_nonnull(g, LLVMGetParam(fn, 0));
  LLVMValueRef lraw = LLVMGetParam(fn, 1);
  LLVMValueRef lp =
      LLVMBuildBitCast(b, lraw, LLVMPointerType(g->list_struct_type, 0), "lp");
  LLVMValueRef cntp =
      LLVMBuildStructGEP2(b, g->list_struct_type, lp, 0, "cntp");
  LLVMValueRef n = LLVMBuildLoad2(b, i64, cntp, "n");
  LLVMValueRef dpp = LLVMBuildStructGEP2(b, g->list_struct_type, lp, 2, "dpp");
  LLVMValueRef data = LLVMBuildLoad2(b, i64ptr, dpp, "data");
  LLVMValueRef is_le0 = zan_icmp(b, LLVMIntSLE, n, zero64, "isle0");
  LLVMBuildCondBr(b, is_le0, empty_bb, chk1_bb);

  /* n <= 0：直接返回不朽空串，零堆分配 */
  LLVMPositionBuilderAtEnd(b, empty_bb);
  LLVMBuildRet(b, emit_string_literal_rc(g, (zan_istr_t){"", 0}));

  /* n == 1：零分隔符遍历；非空且无内嵌 NUL 直接 retain 返回 */
  LLVMPositionBuilderAtEnd(b, chk1_bb);
  LLVMValueRef is_one = zan_icmp(b, LLVMIntEQ, n, one64, "is1");
  LLVMBuildCondBr(b, is_one, one_bb, multi_bb);

  LLVMPositionBuilderAtEnd(b, one_bb);
  LLVMValueRef raw0 = LLVMBuildIntToPtr(
      b, LLVMBuildLoad2(b, i64, data, "raw0i"), i8ptr, "raw0");
  LLVMValueRef raw0_null =
      zan_icmp(b, LLVMIntEQ, raw0, LLVMConstNull(i8ptr), "r0nul");
  LLVMBuildCondBr(b, raw0_null, empty_bb, one_nonnull);

  LLVMPositionBuilderAtEnd(b, one_nonnull);
  LLVMValueRef l0 = zan_call2(b, strlen_ty, g->fn_strlen, &raw0, 1, "l0");
  LLVMValueRef hl0 = emit_string_length(g, raw0, (zan_loc_t){0});
  LLVMValueRef same0 = zan_icmp(b, LLVMIntEQ, l0, hl0, "same0");
  LLVMBuildCondBr(b, same0, one_ret, one_copy);

  LLVMPositionBuilderAtEnd(b, one_ret);
  emit_string_retain(g, raw0);
  LLVMBuildRet(b, raw0);

  LLVMPositionBuilderAtEnd(b, one_copy);
  LLVMValueRef b1 = emit_string_alloc_rc(g, zan_add(b, l0, one64, "l0p1"));
  zan_call2(b, memcpy_ty, memcpy_fn, (LLVMValueRef[]){b1, raw0, l0}, 3, "");
  LLVMBuildStore(b, LLVMConstInt(i8, 0, 0),
                 LLVMBuildInBoundsGEP2(b, i8, b1, &l0, 1, "b1end"));
  emit_string_len_set(g, b1, l0);
  LLVMBuildRet(b, b1);

  /* n >= 2：首遍量长同时检查是否含内嵌 NUL/null 槽；无内嵌 NUL 时第二遍直接
   * O(1) 读字符串头部长度，彻底消除第二次 strlen 遍历。 */
  LLVMPositionBuilderAtEnd(b, multi_bb);
  LLVMValueRef seplen = zan_call2(b, strlen_ty, g->fn_strlen, &sep, 1, "seplen");
  LLVMValueRef nm1 = zan_sub(b, n, one64, "nm1");
  LLVMValueRef sepsum = zan_mul(b, seplen, nm1, "sepsum");
  LLVMValueRef has_sep = zan_icmp(b, LLVMIntNE, seplen, zero64, "hassep");
  LLVMBuildBr(b, m_cond);

  LLVMPositionBuilderAtEnd(b, m_cond);
  LLVMValueRef mi = LLVMBuildPhi(b, i64, "mi");
  LLVMValueRef tot = LLVMBuildPhi(b, i64, "tot");
  LLVMValueRef any_nul = LLVMBuildPhi(b, i1, "anynul");
  LLVMBuildCondBr(b, zan_icmp(b, LLVMIntSLT, mi, n, "mlt"), m_body, c_pre);

  LLVMPositionBuilderAtEnd(b, m_body);
  LLVMValueRef slot = LLVMBuildInBoundsGEP2(b, i64, data, &mi, 1, "slot");
  LLVMValueRef sv_raw = LLVMBuildIntToPtr(
      b, LLVMBuildLoad2(b, i64, slot, "svi"), i8ptr, "svraw");
  LLVMValueRef sv_is_null =
      zan_icmp(b, LLVMIntEQ, sv_raw, LLVMConstNull(i8ptr), "svnul");
  LLVMBuildCondBr(b, sv_is_null, m_step, m_nonnull);

  LLVMPositionBuilderAtEnd(b, m_nonnull);
  LLVMValueRef sl = zan_call2(b, strlen_ty, g->fn_strlen, &sv_raw, 1, "sl");
  LLVMValueRef hsl = emit_string_length(g, sv_raw, (zan_loc_t){0});
  LLVMValueRef nul_here = zan_icmp(b, LLVMIntNE, sl, hsl, "nulh");
  LLVMBasicBlockRef m_nn_end = LLVMGetInsertBlock(b);
  LLVMBuildBr(b, m_step);

  LLVMPositionBuilderAtEnd(b, m_step);
  LLVMValueRef sl_m = LLVMBuildPhi(b, i64, "slm");
  LLVMAddIncoming(sl_m, (LLVMValueRef[]){zero64, sl},
                  (LLVMBasicBlockRef[]){m_body, m_nn_end}, 2);
  LLVMValueRef nul_m = LLVMBuildPhi(b, i1, "nulm");
  LLVMAddIncoming(nul_m, (LLVMValueRef[]){LLVMConstInt(i1, 0, 0), nul_here},
                  (LLVMBasicBlockRef[]){m_body, m_nn_end}, 2);
  LLVMValueRef tot_next = zan_add(b, tot, sl_m, "totn");
  LLVMValueRef any_nul_next = zan_or(b, any_nul, nul_m, "anynuln");
  LLVMValueRef mi_next = zan_add(b, mi, one64, "min");
  LLVMBuildBr(b, m_cond);
  LLVMAddIncoming(mi, (LLVMValueRef[]){zero64, mi_next},
                  (LLVMBasicBlockRef[]){multi_bb, m_step}, 2);
  LLVMAddIncoming(tot, (LLVMValueRef[]){sepsum, tot_next},
                  (LLVMBasicBlockRef[]){multi_bb, m_step}, 2);
  LLVMAddIncoming(any_nul, (LLVMValueRef[]){LLVMConstInt(i1, 0, 0), any_nul_next},
                  (LLVMBasicBlockRef[]){multi_bb, m_step}, 2);

  LLVMPositionBuilderAtEnd(b, c_pre);
  LLVMValueRef is_tot_zero = zan_icmp(b, LLVMIntEQ, tot, zero64, "totz");
  LLVMBasicBlockRef c_alloc = LLVMAppendBasicBlockInContext(c, fn, "c.alloc");
  LLVMBuildCondBr(b, is_tot_zero, empty_bb, c_alloc);

  LLVMPositionBuilderAtEnd(b, c_alloc);
  LLVMValueRef buf = emit_string_alloc_rc(g, zan_add(b, tot, one64, "tot1"));
  LLVMBasicBlockRef c_alloc_end = LLVMGetInsertBlock(b);
  LLVMBuildBr(b, c_cond);

  LLVMPositionBuilderAtEnd(b, c_cond);
  LLVMValueRef ci = LLVMBuildPhi(b, i64, "ci");
  LLVMValueRef pos = LLVMBuildPhi(b, i64, "pos");
  LLVMBuildCondBr(b, zan_icmp(b, LLVMIntSLT, ci, n, "clt"), c_body, done);

  LLVMPositionBuilderAtEnd(b, c_body);
  LLVMValueRef slot2 = LLVMBuildInBoundsGEP2(b, i64, data, &ci, 1, "slot2");
  LLVMValueRef sv2_raw = LLVMBuildIntToPtr(
      b, LLVMBuildLoad2(b, i64, slot2, "svi2"), i8ptr, "sv2raw");
  LLVMValueRef sv2_null =
      zan_icmp(b, LLVMIntEQ, sv2_raw, LLVMConstNull(i8ptr), "sv2nul");
  LLVMBasicBlockRef c_len_chk = LLVMAppendBasicBlockInContext(c, fn, "c.lchk");
  LLVMBuildCondBr(b, sv2_null, c_copy, c_len_chk);

  LLVMPositionBuilderAtEnd(b, c_len_chk);
  zan_set_branch_weights(g, LLVMBuildCondBr(b, any_nul, c_len_slow, c_len_fast),
                         1, 2000);

  LLVMPositionBuilderAtEnd(b, c_len_fast);
  LLVMValueRef sl2_fast = emit_string_length(g, sv2_raw, (zan_loc_t){0});
  LLVMBasicBlockRef c_lf_end = LLVMGetInsertBlock(b);
  LLVMBuildBr(b, c_copy);

  LLVMPositionBuilderAtEnd(b, c_len_slow);
  LLVMValueRef sl2_slow =
      zan_call2(b, strlen_ty, g->fn_strlen, &sv2_raw, 1, "sl2");
  LLVMBuildBr(b, c_copy);

  LLVMPositionBuilderAtEnd(b, c_copy);
  LLVMValueRef sl2 = LLVMBuildPhi(b, i64, "sl2m");
  LLVMAddIncoming(sl2, (LLVMValueRef[]){zero64, sl2_fast, sl2_slow},
                  (LLVMBasicBlockRef[]){c_body, c_lf_end, c_len_slow}, 3);
  LLVMValueRef dst = LLVMBuildInBoundsGEP2(b, i8, buf, &pos, 1, "dst");
  LLVMBasicBlockRef c_do_cp = LLVMAppendBasicBlockInContext(c, fn, "c.docp");
  LLVMBasicBlockRef c_after_cp =
      LLVMAppendBasicBlockInContext(c, fn, "c.aftercp");
  LLVMBuildCondBr(b, zan_icmp(b, LLVMIntNE, sl2, zero64, "sl2nz"), c_do_cp,
                  c_after_cp);

  LLVMPositionBuilderAtEnd(b, c_do_cp);
  zan_call2(b, memcpy_ty, memcpy_fn, (LLVMValueRef[]){dst, sv2_raw, sl2}, 3,
            "");
  LLVMBuildBr(b, c_after_cp);

  LLVMPositionBuilderAtEnd(b, c_after_cp);
  LLVMValueRef pos2 = zan_add(b, pos, sl2, "pos2");
  LLVMValueRef ci1 = zan_add(b, ci, one64, "ci1");
  LLVMValueRef need_sep =
      zan_and(b, has_sep, zan_icmp(b, LLVMIntSLT, ci1, n, "more"), "needsep");
  LLVMBuildCondBr(b, need_sep, c_sep, c_next);

  LLVMPositionBuilderAtEnd(b, c_sep);
  LLVMValueRef dst2 = LLVMBuildInBoundsGEP2(b, i8, buf, &pos2, 1, "dst2");
  zan_call2(b, memcpy_ty, memcpy_fn, (LLVMValueRef[]){dst2, sep, seplen}, 3,
            "");
  LLVMValueRef pos3 = zan_add(b, pos2, seplen, "pos3");
  LLVMBuildBr(b, c_next);

  LLVMPositionBuilderAtEnd(b, c_next);
  LLVMValueRef pos_next = LLVMBuildPhi(b, i64, "posn");
  LLVMAddIncoming(pos_next, (LLVMValueRef[]){pos2, pos3},
                  (LLVMBasicBlockRef[]){c_after_cp, c_sep}, 2);
  LLVMBuildBr(b, c_cond);
  LLVMAddIncoming(ci, (LLVMValueRef[]){zero64, ci1},
                  (LLVMBasicBlockRef[]){c_alloc_end, c_next}, 2);
  LLVMAddIncoming(pos, (LLVMValueRef[]){zero64, pos_next},
                  (LLVMBasicBlockRef[]){c_alloc_end, c_next}, 2);

  LLVMPositionBuilderAtEnd(b, done);
  LLVMValueRef endptr = LLVMBuildInBoundsGEP2(b, i8, buf, &pos, 1, "endptr");
  LLVMBuildStore(b, LLVMConstInt(i8, 0, 0), endptr);
  emit_string_len_set(g, buf, pos);
  LLVMBuildRet(b, buf);
  g->current_fn = saved_fn;
  if (saved)
    LLVMPositionBuilderAtEnd(b, saved);
  return fn;
}
LLVMValueRef coerce_dict_key(zan_irgen_t *g, LLVMValueRef key, zan_type_t *kt) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  if (LLVMGetTypeKind(LLVMTypeOf(key)) == LLVMIntegerTypeKind) {
    LLVMTypeRef kem = kt ? map_type(g, kt) : NULL;
    if (kem && LLVMGetTypeKind(kem) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(kem) < LLVMGetIntTypeWidth(LLVMTypeOf(key)))
      key = LLVMBuildTrunc(g->builder, key, kem, "k.nw");
    if (LLVMGetIntTypeWidth(LLVMTypeOf(key)) < 64)
      key = LLVMBuildSExt(g->builder, key, i64, "k.sx");
    return LLVMBuildIntToPtr(g->builder, key, i8ptr, "k.ip");
  }
  if (LLVMTypeOf(key) != i8ptr)
    return LLVMBuildBitCast(g->builder, key, i8ptr, "k.bc");
  return key;
}

/* 发射`keys[i] == search` for a Dict scan via __zan_dict_key_eq */
static LLVMValueRef emit_dict_key_eq(zan_irgen_t *g, zan_type_t *dict_type,
                                     LLVMValueRef kv, LLVMValueRef search) {
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  zan_type_t *kt = dict_key_type(g, dict_type);
  LLVMValueRef is_str =
      LLVMConstInt(i64, (!kt || kt->kind == TYPE_STRING) ? 1 : 0, 0);
  LLVMValueRef keq = get_dict_key_eq_fn(g);
  return zan_call2(g->builder, LLVMGlobalGetValueType(keq), keq,
                   (LLVMValueRef[]){kv, search, is_str}, 3, "keq");
}

long long dict_key_hash_kind(zan_type_t *kt, int unknown_is_str) {
  if (!kt)
    return unknown_is_str ? 1 : 0;
  switch (kt->kind) {
  case TYPE_STRING:
    return 1;
  case TYPE_BYTE:
  case TYPE_SHORT:
  case TYPE_INT:
  case TYPE_LONG:
  case TYPE_SBYTE:
  case TYPE_USHORT:
  case TYPE_UINT:
  case TYPE_ULONG:
  case TYPE_CHAR:
  case TYPE_ENUM:
    return ZAN_DICT_KEY_INT;
  default:
    return 0;
  }
}

/* 发射特化版 `__zan_dict_find_{str,int,raw}(dict, key)` */
LLVMValueRef emit_dict_find(zan_irgen_t *g, zan_type_t *dict_type,
                            LLVMValueRef draw, LLVMValueRef key) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  zan_type_t *kt = dict_key_type(g, dict_type);
  int key_kind = (int)dict_key_hash_kind(kt, 1);
  LLVMValueRef fn = get_dict_find_spec_fn(g, key_kind);
  if (LLVMTypeOf(draw) != i8ptr)
    draw = LLVMBuildBitCast(g->builder, draw, i8ptr, "d8");
  return zan_call2(g->builder, LLVMGlobalGetValueType(fn), fn,
                   (LLVMValueRef[]){draw, key}, 2, "dfind");
}
zan_type_t *static_access_inst(zan_irgen_t *g, zan_ast_node_t *obj_expr) {
  if (!obj_expr || !obj_expr->ident.inst_type_ref)
    return NULL;
  zan_type_t *t = resolve_type_ctx(g, obj_expr->ident.inst_type_ref);
  return (t && t->type_arg_count > 0) ? t : NULL;
}
LLVMValueRef get_static_field_global(zan_irgen_t *g, zan_symbol_t *class_sym,
                                     zan_symbol_t *fsym, zan_type_t *inst) {
  if (!class_sym || !fsym || !fsym->decl ||
      (fsym->decl->kind != AST_FIELD_DECL &&
       fsym->decl->kind != AST_PROPERTY_DECL))
    return NULL;
  if (!((fsym->modifiers & MOD_STATIC) ||
        (fsym->decl->field_decl.modifiers & MOD_STATIC)))
    return NULL;
  if (fsym->parent &&
      (fsym->parent->kind == SYM_CLASS || fsym->parent->kind == SYM_STRUCT)) {
    class_sym = fsym->parent;
  }
  /* 泛型类的静态字段按闭合特化类型独立隔离 (如 Stat<int> 与 Stat<string>
   * 拥有独立静态槽) */
  if (!inst && g->cur_inst && g->cur_inst->sym == class_sym)
    inst = g->cur_inst;
  if (inst && inst->sym != class_sym)
    inst = NULL;
  if (!is_user_generic_sym(class_sym))
    inst = NULL;
  if (g->cur_canon_body && g->cur_gctx_val && inst &&
      inst->sym == class_sym && type_mentions_canon(inst)) {
    LLVMValueRef slot = emit_gctx_load_slot(g, GCTX_SLOT_STATIC_FIELD, fsym,
                                            inst, NULL, 0, 0, 0);
    if (slot) {
      zan_type_t *ft_z = field_store_type(g, fsym, inst);
      LLVMTypeRef ft = ft_z ? map_type(g, ft_z) : LLVMInt64TypeInContext(g->ctx);
      return LLVMBuildBitCast(g->builder, slot, LLVMPointerType(ft, 0),
                              "sf.gctx");
    }
  }
  irgen_record_static_type_use(g, class_sym, inst);
  char suffix[256];
  suffix[0] = '\0';
  if (inst && inst->sym == class_sym && inst->type_arg_count > 0)
    mangle_inst_suffix(suffix, sizeof(suffix), inst);
  char name[512];
  snprintf(name, sizeof(name), "__zan_sf_%.*s_%.*s%s", (int)class_sym->name.len,
           class_sym->name.str, (int)fsym->name.len, fsym->name.str, suffix);
  LLVMValueRef gv = LLVMGetNamedGlobal(g->mod, name);
  if (gv)
    return gv;
  zan_type_t *field_type = field_store_type(g, fsym, inst);
  LLVMTypeRef ft =
      field_type ? map_type(g, field_type) : LLVMInt64TypeInContext(g->ctx);
  gv = LLVMAddGlobal(g->mod, ft, name);
  LLVMSetLinkage(gv, LLVMInternalLinkage);
  LLVMSetInitializer(gv, LLVMConstNull(ft));
  if (field_type &&
      (is_rc_managed_type(field_type) || field_type->kind == TYPE_OBJECT)) {
    if (g->static_field_count >= g->static_field_cap) {
      g->static_field_cap = g->static_field_cap ? g->static_field_cap * 2 : 16;
      g->static_fields = (struct zan_static_field_ref *)realloc(
          g->static_fields,
          (size_t)g->static_field_cap * sizeof(*g->static_fields));
    }
    g->static_fields[g->static_field_count].type = field_type;
    g->static_fields[g->static_field_count].gv = gv;
    g->static_fields[g->static_field_count].owner = class_sym;
    g->static_fields[g->static_field_count].inst = inst;
    g->static_field_count++;
  }
  return gv;
}

static int is_stable_field_base(zan_ast_node_t *expr) {
  if (!expr)
    return 0;
  if (expr->kind == AST_IDENTIFIER || expr->kind == AST_THIS_EXPR ||
      expr->kind == AST_BASE_EXPR)
    return 1;
  return expr->kind == AST_MEMBER_ACCESS &&
         is_stable_field_base(expr->member.object);
}
void emit_invalidate_freed_string(zan_irgen_t *g, zan_ast_node_t *arg,
                                  local_scope_t *locals) {
  if (!arg || !locals)
    return;
  if (arg->kind == AST_IDENTIFIER) {
    local_var_t *local = local_find(locals, arg->ident.name);
    if (local && local->type && local->type->kind == TYPE_STRING) {
      LLVMTypeRef slot_type = local_slot_type(g, local);
      if (LLVMGetTypeKind(slot_type) == LLVMPointerTypeKind) {
        LLVMBuildStore(g->builder, LLVMConstNull(slot_type), local->alloca);
      }
      return;
    }
    if (g->current_type_sym) {
      zan_symbol_t *field = get_field_sym(g->current_type_sym, arg->ident.name);
      if (!field || !field->type || field->type->kind != TYPE_STRING)
        return;
      LLVMValueRef global =
          get_static_field_global(g, g->current_type_sym, field, NULL);
      zan_type_t *fst = field_store_type(g, field, g->cur_inst);
      LLVMTypeRef field_type = map_type(g, fst ? fst : field->type);
      if (global) {
        LLVMBuildStore(g->builder, LLVMConstNull(field_type), global);
      } else if (g->current_this) {
        int fi = get_field_index(g->current_type_sym, arg->ident.name);
        LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
        if (fi >= 0 && st) {
          LLVMValueRef this_ptr = LLVMBuildLoad2(
              g->builder, LLVMPointerType(st, 0), g->current_this, "this");
          LLVMValueRef field_ptr = emit_field_ptr(g, g->current_type_sym, st,
                                                  this_ptr, fi, "freed.fld");
          LLVMBuildStore(g->builder, LLVMConstNull(field_type), field_ptr);
        }
      }
    }
    return;
  }
  if (arg->kind != AST_MEMBER_ACCESS)
    return;
  zan_type_t *field_type = infer_expr_type(g, arg, locals);
  if (!field_type || field_type->kind != TYPE_STRING)
    return;
  zan_ast_node_t *obj = arg->member.object;
  zan_symbol_t *class_sym = NULL;
  LLVMValueRef object_ptr = NULL;
  if (obj->kind == AST_IDENTIFIER) {
    local_var_t *local = local_find(locals, obj->ident.name);
    if (local && local->type && local->type->sym) {
      class_sym = local->type->sym;
      LLVMTypeRef st = get_struct_llvm_type(g, class_sym);
      if (st)
        object_ptr = struct_base_ptr(g, local, st);
    } else {
      class_sym = zan_binder_lookup(g->binder, obj->ident.name);
      if (class_sym &&
          (class_sym->kind == SYM_CLASS || class_sym->kind == SYM_STRUCT)) {
        zan_symbol_t *field = get_field_sym(class_sym, arg->member.name);
        LLVMValueRef global = get_static_field_global(
            g, class_sym, field, static_access_inst(g, obj));
        if (global) {
          LLVMBuildStore(g->builder, LLVMConstNull(map_type(g, field_type)),
                         global);
          return;
        }
      }
      class_sym = NULL;
    }
  } else if ((obj->kind == AST_THIS_EXPR || obj->kind == AST_BASE_EXPR) &&
             g->current_type_sym && g->current_this) {
    class_sym = g->current_type_sym;
    LLVMTypeRef st = get_struct_llvm_type(g, class_sym);
    if (st) {
      object_ptr = LLVMBuildLoad2(g->builder, LLVMPointerType(st, 0),
                                  g->current_this, "this");
    }
  } else if (is_stable_field_base(obj)) {
    class_sym = expr_class_sym(g, obj, locals);
    if (class_sym)
      object_ptr = emit_expr(g, obj, locals);
  }
  if (!class_sym || !object_ptr ||
      LLVMGetTypeKind(LLVMTypeOf(object_ptr)) != LLVMPointerTypeKind)
    return;
  int fi = get_field_index(class_sym, arg->member.name);
  LLVMTypeRef st = get_struct_llvm_type(g, class_sym);
  if (fi < 0 || !st)
    return;
  LLVMValueRef field_ptr =
      emit_field_ptr(g, class_sym, st, object_ptr, fi, "freed.fld");
  LLVMBuildStore(g->builder, LLVMConstNull(map_type(g, field_type)), field_ptr);
}
LLVMValueRef emit_null_cond(zan_irgen_t *g, zan_ast_node_t *expr,
                            zan_ast_node_t *qmem, local_scope_t *locals) {
  LLVMValueRef obj = emit_expr(g, qmem->member.object, locals);
  if (LLVMGetTypeKind(LLVMTypeOf(obj)) != LLVMPointerTypeKind) {
    qmem->member.null_cond = 0;
    LLVMValueRef v = emit_expr(g, expr, locals);
    qmem->member.null_cond = 1;
    return v;
  }
  zan_type_t *oty = infer_expr_type(g, qmem->member.object, locals);
  char nm[32];
  snprintf(nm, sizeof(nm), "__qdot%d", g->qdot_counter++);
  zan_istr_t iname = {zan_arena_strdup(g->arena, nm, strlen(nm)),
                      (int)strlen(nm)};
  LLVMValueRef slot = emit_entry_alloca(g, LLVMTypeOf(obj), nm);
  LLVMBuildStore(g->builder, obj, slot);
  local_add(locals, iname, slot, oty);

  zan_ast_node_t *ident = zan_ast_new(g->arena, AST_IDENTIFIER, qmem->loc);
  ident->ident.name = iname;
  zan_ast_node_t *saved_obj = qmem->member.object;
  qmem->member.object = ident;
  qmem->member.null_cond = 0;

  LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
  LLVMBasicBlockRef then_bb =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "qdot.then");
  LLVMBasicBlockRef merge_bb =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "qdot.end");
  LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, obj,
                                 LLVMConstNull(LLVMTypeOf(obj)), "qdot.isnull");
  LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->builder);
  LLVMBuildCondBr(g->builder, isnull, merge_bb, then_bb);

  LLVMPositionBuilderAtEnd(g->builder, then_bb);
  LLVMValueRef v = emit_expr(g, expr, locals);
  LLVMTypeRef rt = LLVMTypeOf(v);
  if (LLVMGetTypeKind(rt) != LLVMVoidTypeKind &&
      LLVMGetTypeKind(rt) != LLVMPointerTypeKind && !llvm_is_nullable(rt)) {
    LLVMValueRef some = nullable_some(g, nullable_type_of(g, rt), v);
    if (some)
      v = some;
  }
  LLVMBasicBlockRef then_end = LLVMGetInsertBlock(g->builder);
  LLVMBuildBr(g->builder, merge_bb);

  qmem->member.object = saved_obj;
  qmem->member.null_cond = 1;

  LLVMPositionBuilderAtEnd(g->builder, merge_bb);
  LLVMTypeRef vt = LLVMTypeOf(v);
  if (LLVMGetTypeKind(vt) == LLVMVoidTypeKind) {
    emit_release_owned_call_temp(g, saved_obj, obj, locals);
    return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
  }
  LLVMValueRef phi = LLVMBuildPhi(g->builder, vt, "qdot");
  LLVMValueRef dflt = LLVMConstNull(vt);
  LLVMValueRef vals[] = {dflt, v};
  LLVMBasicBlockRef bbs[] = {entry_bb, then_end};
  LLVMAddIncoming(phi, vals, bbs, 2);
  emit_release_owned_call_temp(g, saved_obj, obj, locals);
  return phi;
}
void reorder_named_args_impl(zan_irgen_t *g, zan_ast_list_t *args,
                             zan_loc_t loc, zan_ast_node_t *decl) {
  if (!args || !decl)
    return;
  if (decl->kind != AST_METHOD_DECL && decl->kind != AST_CONSTRUCTOR_DECL)
    return;
  int n = args->count;
  int has_named = 0;
  for (int i = 0; i < n; i++) {
    if (args->items[i] && args->items[i]->kind == AST_NAMED_ARG) {
      has_named = 1;
      break;
    }
  }
  if (!has_named)
    return;

  zan_ast_list_t *ps = &decl->method_decl.params;
  int m = ps->count;
  int *slot = (int *)malloc(sizeof(int) * (size_t)(m > n ? m : n));
  for (int i = 0; i < (m > n ? m : n); i++)
    slot[i] = -1;
  int *used_name = (int *)malloc(sizeof(int) * (size_t)n);
  for (int i = 0; i < n; i++)
    used_name[i] = 0;

  for (int i = 0; i < n; i++) {
    zan_ast_node_t *arg = args->items[i];
    if (!arg || arg->kind != AST_NAMED_ARG)
      continue;
    int found = -1;
    for (int j = 0; j < m; j++) {
      zan_ast_node_t *p = ps->items[j];
      if (!p || p->kind != AST_PARAM)
        continue;
      if (p->param.is_this)
        continue;
      if (p->param.name.len == arg->named_arg.name.len &&
          memcmp(p->param.name.str, arg->named_arg.name.str,
                 (size_t)p->param.name.len) == 0) {
        found = j;
        break;
      }
    }
    if (found < 0) {
      zan_diag_emit_code(g->diag, DIAG_ERROR, arg->loc, 2100,
                         "no parameter named '%.*s'",
                         (int)arg->named_arg.name.len, arg->named_arg.name.str);
    } else if (slot[found] >= 0) {
      zan_diag_emit_code(g->diag, DIAG_ERROR, arg->loc, 2101,
                         "parameter '%.*s' is already assigned",
                         (int)arg->named_arg.name.len, arg->named_arg.name.str);
    } else {
      slot[found] = i;
      used_name[i] = 1;
    }
  }

  int j = 0;
  for (int i = 0; i < n; i++) {
    if (used_name[i])
      continue;
    while (j < m && (slot[j] >= 0 || ps->items[j]->param.is_this))
      j++;
    if (j >= m)
      break;
    slot[j] = i;
    j++;
  }

  zan_ast_node_t **reb =
      (zan_ast_node_t **)malloc(sizeof(zan_ast_node_t *) * (size_t)n);
  if (!reb) {
    free(slot);
    free(used_name);
    return;
  }
  int ok = 1;
  for (int p = 0; p < n; p++) {
    if (slot[p] < 0 || slot[p] >= n) {
      ok = 0;
      zan_diag_emit_code(g->diag, DIAG_ERROR, loc, 2102,
                         "named arguments must bind to the leading "
                         "parameters in order");
      break;
    }
    zan_ast_node_t *arg = args->items[slot[p]];
    if (arg && arg->kind == AST_NAMED_ARG)
      arg = arg->named_arg.expr;
    reb[p] = arg;
  }
  if (ok)
    for (int i = 0; i < n; i++)
      args->items[i] = reb[i];
  free(reb);
  free(slot);
  free(used_name);
}

static void reorder_named_args(zan_irgen_t *g, zan_ast_node_t *call,
                               zan_symbol_t *method_sym) {
  if (!call || call->kind != AST_CALL)
    return;
  if (!method_sym || !method_sym->decl)
    return;
  reorder_named_args_impl(g, &call->call.args, call->loc, method_sym->decl);
}
void fill_default_args(zan_irgen_t *g, zan_ast_node_t *call,
                       zan_symbol_t *method_sym) {
  if (!call || call->kind != AST_CALL)
    return;
  if (!method_sym || !method_sym->decl)
    return;
  if (method_sym->decl->kind != AST_METHOD_DECL &&
      method_sym->decl->kind != AST_CONSTRUCTOR_DECL)
    return;
  reorder_named_args(g, call, method_sym);
  if (method_is_params_variadic(method_sym)) {
    /* params 方法也填充尾参之前的可选形参（A381 params 组合）：
     * "(int a, int b = 10, params int[] rest)" 的 b 在调用缺省时要填，
     * params 数组本身留给 pack_params_args 收（0 个也收空数组）。 */
    zan_ast_list_t *pps = &method_sym->decl->method_decl.params;
    int fill_to = pps->count - 1;
    for (int i = call->call.args.count; i < fill_to; i++) {
      zan_ast_node_t *pp = pps->items[i];
      if (!pp || pp->kind != AST_PARAM || !pp->param.default_val)
        return;
      zan_ast_list_push(&call->call.args, pp->param.default_val, g->arena);
    }
    return;
  }
  if (method_sym->name.len > 3 && memcmp(method_sym->name.str, "op_", 3) == 0)
    return;
  zan_ast_list_t *ps = &method_sym->decl->method_decl.params;
  for (int i = call->call.args.count; i < ps->count; i++) {
    zan_ast_node_t *p = ps->items[i];
    if (!p || p->kind != AST_PARAM || !p->param.default_val)
      return;
    zan_ast_list_push(&call->call.args, p->param.default_val, g->arena);
  }
}
void pack_params_args(zan_irgen_t *g, zan_ast_node_t *call,
                      zan_symbol_t *method_sym, local_scope_t *locals) {
  if (!method_sym || !method_sym->decl ||
      method_sym->decl->kind != AST_METHOD_DECL)
    return;
  if (!method_is_params_variadic(method_sym))
    return;
  zan_ast_list_t *ps = &method_sym->decl->method_decl.params;
  int injected = method_sym->name.len == 7 &&
                 memcmp(method_sym->name.str, "op_call", 7) == 0 &&
                 (method_sym->modifiers & MOD_STATIC) != 0;
  int visible = ps->count - injected;
  int fixed = visible - 1;
  int argc = call->call.args.count;
  if (argc < fixed)
    return;
  if (argc == visible) {
    zan_ast_node_t *la = call->call.args.items[argc - 1];
    if (la->kind == AST_NEW_EXPR)
      return;
    zan_type_t *lt = infer_expr_type(g, la, locals);
    if (lt && lt->kind == TYPE_ARRAY)
      return;
  }
  zan_ast_node_t *last_p = ps->items[ps->count - 1];
  zan_ast_node_t *arr = zan_ast_new(g->arena, AST_NEW_EXPR, call->loc);
  arr->new_expr.type = last_p->param.type; /* T[] */
  arr->new_expr.is_array = true;
  arr->new_expr.array_init = true;
  zan_ast_list_init(&arr->new_expr.args);
  for (int i = fixed; i < argc; i++)
    zan_ast_list_push(&arr->new_expr.args, call->call.args.items[i], g->arena);
  call->call.args.count = fixed;
  zan_ast_list_push(&call->call.args, arr, g->arena);
}
LLVMValueRef emit_ref_arg(zan_irgen_t *g, zan_ast_node_t *arg,
                          local_scope_t *locals);
