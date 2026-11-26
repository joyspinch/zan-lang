}
void build_class_release_body(zan_irgen_t *g, zan_symbol_t *sym,
                              zan_type_t *inst, LLVMValueRef fn) {
  di_clear(g);
  LLVMContextRef c = g->ctx;
  LLVMBuilderRef b = g->builder;
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
  LLVMTypeRef rc_ty = LLVMInt32TypeInContext(c);
  LLVMTypeRef structT = get_struct_llvm_type(g, sym);
  LLVMValueRef obj = LLVMGetParam(fn, 0);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
  LLVMBasicBlockRef cont = LLVMAppendBasicBlockInContext(c, fn, "cont");
  LLVMBasicBlockRef relf = LLVMAppendBasicBlockInContext(c, fn, "relf");
  LLVMBasicBlockRef dorel = LLVMAppendBasicBlockInContext(c, fn, "dorel");
  LLVMBasicBlockRef ret = LLVMAppendBasicBlockInContext(c, fn, "ret");
  LLVMPositionBuilderAtEnd(b, entry);
  LLVMValueRef isnull =
      zan_icmp(b, LLVMIntEQ, obj, LLVMConstNull(i8ptr), "isnull");
  zan_set_branch_weights(g, LLVMBuildCondBr(b, isnull, ret, cont), 1, 2000);
  LLVMPositionBuilderAtEnd(b, cont);
  LLVMValueRef rcip = zan_rc_ptr(g, b, obj, "rcp");
  LLVMValueRef zan_emit_weak_any(zan_irgen_t *);
  if (!g->arc_guard) {
    /* 独占快路径：读到 rc == 1 且全局无活跃弱引用时，本线程是唯一持有者，
     * 直接写 0 + acquire fence 进 relf，免一次硬件 LOCK XADD 与二次 weak 判空。 */
    LLVMValueRef rc_now = LLVMBuildLoad2(b, rc_ty, rcip, "rcnow");
    LLVMSetOrdering(rc_now, LLVMAtomicOrderingMonotonic);
    LLVMSetAlignment(rc_now, 4);
    LLVMBasicBlockRef uniq_chk = LLVMAppendBasicBlockInContext(c, fn, "uniq.chk");
    LLVMBasicBlockRef uniq_go = LLVMAppendBasicBlockInContext(c, fn, "uniq.go");
    LLVMBasicBlockRef atomic_bb = LLVMAppendBasicBlockInContext(c, fn, "sub.atomic");
    LLVMValueRef is_uniq =
        zan_icmp(b, LLVMIntEQ, rc_now, LLVMConstInt(rc_ty, 1, 0), "isuniq");
    zan_set_branch_weights(g, LLVMBuildCondBr(b, is_uniq, uniq_chk, atomic_bb),
                           2000, 1);
    LLVMPositionBuilderAtEnd(b, uniq_chk);
    zan_set_branch_weights(
        g, LLVMBuildCondBr(b, zan_emit_weak_any(g), atomic_bb, uniq_go), 1, 2000);
    LLVMPositionBuilderAtEnd(b, uniq_go);
    LLVMValueRef st0 = LLVMBuildStore(b, LLVMConstInt(rc_ty, 0, 0), rcip);
    LLVMSetOrdering(st0, LLVMAtomicOrderingMonotonic);
    LLVMSetAlignment(st0, 4);
    LLVMBuildFence(b, LLVMAtomicOrderingAcquire, 0, "");
    LLVMBuildBr(b, relf);
    LLVMPositionBuilderAtEnd(b, atomic_bb);
  }
  LLVMValueRef rc_old = LLVMBuildAtomicRMW(b, LLVMAtomicRMWBinOpSub, rcip,
                                           LLVMConstInt(rc_ty, 1, 0),
                                           LLVMAtomicOrderingRelease, 0);
  LLVMValueRef is1 =
      zan_icmp(b, LLVMIntEQ, rc_old, LLVMConstInt(rc_ty, 1, 0), "is1");
  LLVMValueRef over =
      zan_icmp(b, LLVMIntSLE, rc_old, LLVMConstInt(rc_ty, 0, 0), "over");
  LLVMBasicBlockRef last_bb = LLVMAppendBasicBlockInContext(c, fn, "last");
  zan_set_branch_weights(g, LLVMBuildCondBr(b, over, dorel, last_bb), 1, 2000);
  LLVMPositionBuilderAtEnd(b, last_bb);
  LLVMBasicBlockRef begin_bb = LLVMAppendBasicBlockInContext(c, fn, "begin");
  LLVMBuildCondBr(b, is1, begin_bb, ret);
  LLVMPositionBuilderAtEnd(b, begin_bb);
  LLVMBuildFence(b, LLVMAtomicOrderingAcquire, 0, "");
  /* 弱引用表为空时 destroy_begin 恒返回真：内联判空，不付调用 */
  LLVMBasicBlockRef wcall_bb =
      LLVMAppendBasicBlockInContext(c, fn, "weak.begin.call");
  zan_set_branch_weights(g, LLVMBuildCondBr(b, zan_emit_weak_any(g), wcall_bb, relf),
                         1, 2000);
  LLVMPositionBuilderAtEnd(b, wcall_bb);
  LLVMValueRef weak_ok =
      zan_call2(b, LLVMGlobalGetValueType(g->rt_weak_destroy_begin),
                g->rt_weak_destroy_begin, &obj, 1, "weak.begin");
  LLVMBuildCondBr(b, weak_ok, relf, ret);
  LLVMPositionBuilderAtEnd(b, relf);
  LLVMValueRef self =
      LLVMBuildBitCast(b, obj, LLVMPointerType(structT, 0), "self");
  if (sym->kind == SYM_STRUCT) {
    /* A556-2：箱首字是 vtable tag（emit_box_struct 布局契约），载荷
     * 在 +8——字段 walk 先跳过 tag 字。类不走这里（class_vptr_offset
     * 已把 vptr 计为 0 号字段）。 */
    LLVMValueRef p8 = LLVMBuildGEP2(
        b, LLVMInt8TypeInContext(c), obj,
        (LLVMValueRef[]){LLVMConstInt(LLVMInt64TypeInContext(c), 8, 0)}, 1,
        "box.p8");
    self = LLVMBuildBitCast(b, p8, LLVMPointerType(structT, 0), "self.p8");
  }
  int fi = class_vptr_offset(sym);
  for (int i = 0; i < sym->member_count; i++) {
    zan_symbol_t *m = sym->members[i];
    if (m->kind != SYM_FIELD && m->kind != SYM_PROPERTY)
      continue;
    if (field_member_is_static(m))
      continue;
    int idx = fi++;
    zan_type_t *ft = m->type;
    if (!ft)
      continue;
    if (inst)
      ft = subst_type_param_deep(g, ft, inst);
    if (!ft || ft->kind == TYPE_TYPE_PARAM)
      continue;
    if (type_is_binding(inst ? inst : sym->type) && m->name.len == 6 &&
        memcmp(m->name.str, "target", 6) == 0)
      continue;
    if ((m->modifiers & MOD_WEAK) &&
        (is_arc_managed_type(ft) || ft->is_canon) &&
        (ft->kind == TYPE_INTERFACE || ft->is_canon ||
         (ft->kind == TYPE_CLASS && ft->sym != NULL))) {
      if (LLVMGetTypeKind(map_type(g, ft)) == LLVMPointerTypeKind) {
        LLVMValueRef fp =
            LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "weak.fp");
        emit_weak_store(
            g, fp,
            LLVMConstPointerNull(LLVMPointerType(LLVMInt8TypeInContext(c), 0)));
      }
      continue;
    }
    if (ft->kind == TYPE_STRING) {
      LLVMValueRef fp =
          LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
      LLVMValueRef s = LLVMBuildLoad2(b, i8ptr, fp, "fs");
      emit_string_release(g, s);
    } else if (ft->kind == TYPE_DELEGATE) {
      LLVMValueRef fp =
          LLVMBuildStructGEP2(g->builder, structT, self, (unsigned)idx, "fp");
      LLVMValueRef dv = LLVMBuildLoad2(g->builder, i8ptr, fp, "fd");
      emit_closure_release(g, dv);
    } else if (ft->kind == TYPE_ARRAY) {
      LLVMValueRef fp =
          LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
      LLVMValueRef av = LLVMBuildLoad2(b, i8ptr, fp, "fa");
      emit_array_release(g, ft, av);
    } else if (is_arc_managed_type(ft)) {
      LLVMValueRef fp =
          LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
      LLVMValueRef cv = LLVMBuildLoad2(b, map_type(g, ft), fp, "fc");
      emit_arc_release_typed(g, ft, cv);
    } else if (ft->kind == TYPE_STRUCT) {
      /* 装箱结构体的嵌套结构体字段（A556-3）：内层托管叶子按值
       * 递归释放（emit_collection_value_release 对聚合值逐字段
       * 下钻），GEP 位移与载荷布局一致。 */
      LLVMValueRef fp =
          LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
      LLVMValueRef nv = LLVMBuildLoad2(b, map_type(g, ft), fp, "fn");
      emit_collection_value_release(g, ft, nv, 0);
    } else if (ft->kind == TYPE_OBJECT) {
      LLVMValueRef fp =
          LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
      LLVMValueRef cv = LLVMBuildLoad2(b, i8ptr, fp, "fc");
      emit_release_obj_value(g, cv);
    }
  }
  LLVMBasicBlockRef freebb = LLVMAppendBasicBlockInContext(c, fn, "freebb");
  LLVMBuildBr(b, freebb);
  LLVMPositionBuilderAtEnd(b, freebb);
  LLVMValueRef freefn =
      ((sym->kind == SYM_CLASS && zan_class_needs_pad16(structT))
           ? get_arc_free_obj16_decl(g)
           : get_arc_free_obj_decl(g));
  zan_call2(b, LLVMGlobalGetValueType(freefn), freefn, &obj, 1, "");
  LLVMBuildBr(b, ret);
  LLVMPositionBuilderAtEnd(b, dorel);
  zan_call2(b, LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0),
            g->rt_release, &obj, 1, "");
  LLVMBuildBr(b, ret);
  LLVMPositionBuilderAtEnd(b, ret);
  LLVMBuildRetVoid(b);
}

static void build_collection_release_body(zan_irgen_t *g, int coll_kind,
                                          zan_type_t *elem_type,
                                          LLVMValueRef fn) {
  di_clear(g);
  LLVMContextRef c = g->ctx;
  LLVMBuilderRef b = g->builder;
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
  LLVMTypeRef rc_ty = LLVMInt32TypeInContext(c);
  LLVMValueRef obj = LLVMGetParam(fn, 0);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
  LLVMBasicBlockRef cont = LLVMAppendBasicBlockInContext(c, fn, "cont");
  LLVMBasicBlockRef relf = LLVMAppendBasicBlockInContext(c, fn, "relf");
  LLVMBasicBlockRef dorel = LLVMAppendBasicBlockInContext(c, fn, "dorel");
  LLVMBasicBlockRef ret = LLVMAppendBasicBlockInContext(c, fn, "ret");
  LLVMPositionBuilderAtEnd(b, entry);
  LLVMValueRef isnull =
      zan_icmp(b, LLVMIntEQ, obj, LLVMConstNull(i8ptr), "isnull");
  zan_set_branch_weights(g, LLVMBuildCondBr(b, isnull, ret, cont), 1, 2000);
  LLVMPositionBuilderAtEnd(b, cont);
  LLVMValueRef rcip = zan_rc_ptr(g, b, obj, "rcp");
  if (!g->arc_guard) {
    /* 集合对象不进弱引用表：rc == 1 时本线程独占，写 0 直接进 relf（acquire fence）。 */
    LLVMValueRef rc_now = LLVMBuildLoad2(b, rc_ty, rcip, "rcnow");
    LLVMSetOrdering(rc_now, LLVMAtomicOrderingMonotonic);
    LLVMSetAlignment(rc_now, 4);
    LLVMBasicBlockRef uniq_go = LLVMAppendBasicBlockInContext(c, fn, "uniq.go");
    LLVMBasicBlockRef atomic_bb =
        LLVMAppendBasicBlockInContext(c, fn, "sub.atomic");
    LLVMValueRef is_uniq =
        zan_icmp(b, LLVMIntEQ, rc_now, LLVMConstInt(rc_ty, 1, 0), "isuniq");
    zan_set_branch_weights(g, LLVMBuildCondBr(b, is_uniq, uniq_go, atomic_bb),
                           2000, 1);
    LLVMPositionBuilderAtEnd(b, uniq_go);
    LLVMValueRef st0 = LLVMBuildStore(b, LLVMConstInt(rc_ty, 0, 0), rcip);
    LLVMSetOrdering(st0, LLVMAtomicOrderingMonotonic);
    LLVMSetAlignment(st0, 4);
    LLVMBuildBr(b, relf);
    LLVMPositionBuilderAtEnd(b, atomic_bb);
  }
  LLVMValueRef rc_old = LLVMBuildAtomicRMW(b, LLVMAtomicRMWBinOpSub, rcip,
                                           LLVMConstInt(rc_ty, 1, 0),
                                           LLVMAtomicOrderingRelease, 0);
  LLVMValueRef is1 =
      zan_icmp(b, LLVMIntEQ, rc_old, LLVMConstInt(rc_ty, 1, 0), "is1");
  LLVMValueRef over =
      zan_icmp(b, LLVMIntSLE, rc_old, LLVMConstInt(rc_ty, 0, 0), "over");
  LLVMBasicBlockRef last_bb = LLVMAppendBasicBlockInContext(c, fn, "last");
  zan_set_branch_weights(g, LLVMBuildCondBr(b, over, dorel, last_bb), 1, 2000);
  LLVMPositionBuilderAtEnd(b, last_bb);
  LLVMBuildCondBr(b, is1, relf, ret);
  LLVMPositionBuilderAtEnd(b, relf);
  LLVMBuildFence(b, LLVMAtomicOrderingAcquire, 0, "");
  LLVMTypeRef free_ty =
      LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0);
  if (coll_kind == 1) {
    LLVMValueRef lp =
        LLVMBuildBitCast(b, obj, LLVMPointerType(g->list_struct_type, 0), "lp");
    LLVMValueRef dp = LLVMBuildStructGEP2(b, g->list_struct_type, lp, 2, "dp");
    LLVMValueRef data = LLVMBuildLoad2(b, LLVMPointerType(i64, 0), dp, "data");
    emit_list_release_elems(g, elem_type, obj);
    LLVMValueRef d8 = LLVMBuildBitCast(b, data, i8ptr, "d8");
    zan_call2(b, free_ty, g->fn_free, &d8, 1, "");
  } else if (coll_kind == 3) {
    LLVMValueRef dp =
        LLVMBuildBitCast(b, obj, LLVMPointerType(g->dict_struct_type, 0), "dp");
    LLVMValueRef ks = LLVMBuildLoad2(
        b, i8ptr, LLVMBuildStructGEP2(b, g->dict_struct_type, dp, 2, "kp"),
        "ks8");
    LLVMValueRef vs = LLVMBuildLoad2(
        b, i8ptr, LLVMBuildStructGEP2(b, g->dict_struct_type, dp, 3, "vp"),
        "vs8");
    LLVMValueRef ix = LLVMBuildLoad2(
        b, i8ptr, LLVMBuildStructGEP2(b, g->dict_struct_type, dp, 4, "ip"),
        "ix8");
    LLVMValueRef dead = LLVMBuildLoad2(
        b, i8ptr, LLVMBuildStructGEP2(b, g->dict_struct_type, dp, 9, "deadp"),
        "dead8");
    emit_dict_release_elems(g, elem_type, obj);
    zan_call2(b, free_ty, g->fn_free, &ks, 1, "");
    zan_call2(b, free_ty, g->fn_free, &vs, 1, "");
    zan_call2(b, free_ty, g->fn_free, &ix, 1, "");
    zan_call2(b, free_ty, g->fn_free, &dead, 1, "");
  } else if (coll_kind == 2) {
    LLVMValueRef sp =
        LLVMBuildBitCast(b, obj, LLVMPointerType(g->sb_struct_type, 0), "sp");
    LLVMValueRef dp = LLVMBuildStructGEP2(b, g->sb_struct_type, sp, 2, "dp");
    LLVMValueRef data = LLVMBuildLoad2(b, i8ptr, dp, "data");
    zan_call2(b, free_ty, g->fn_free, &data, 1, "");
  }
  LLVMBasicBlockRef freebb = LLVMAppendBasicBlockInContext(c, fn, "freebb");
  LLVMBuildBr(b, freebb);
  LLVMPositionBuilderAtEnd(b, freebb);
  LLVMValueRef freefn = get_arc_free_obj_decl(g);
  zan_call2(b, LLVMGlobalGetValueType(freefn), freefn, &obj, 1, "");
  LLVMBuildBr(b, ret);
  LLVMPositionBuilderAtEnd(b, dorel);
  zan_call2(b, LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0),
            g->rt_release, &obj, 1, "");
  LLVMBuildBr(b, ret);
  LLVMPositionBuilderAtEnd(b, ret);
  LLVMBuildRetVoid(b);
}
LLVMValueRef get_collection_release_decl(zan_irgen_t *g, int site) {
  int ck = g->site_coll ? g->site_coll[site] : 0;
  zan_type_t *ce = g->site_coll_elem ? g->site_coll_elem[site] : NULL;
  char name[64];
  snprintf(name, sizeof(name), "__zan_release_coll_%d", site);
  LLVMValueRef existing = LLVMGetNamedFunction(g->mod, name);
  if (existing)
    return existing;
  if (ck != 0) {
    for (int prev = 1; prev < g->leak_site_count; prev++) {
      if (!g->site_coll || g->site_coll[prev] != ck)
        continue;
      zan_type_t *pe = g->site_coll_elem ? g->site_coll_elem[prev] : NULL;
      if (pe != ce && (!pe || !ce || !types_equal(pe, ce)))
        continue;
      char pname[64];
      snprintf(pname, sizeof(pname), "__zan_release_coll_%d", prev);
      LLVMValueRef pfn = LLVMGetNamedFunction(g->mod, pname);
      if (pfn)
        return pfn;
    }
  }
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef ft =
      LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, name, ft);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  zan_mark_nounwind(g, fn);
  build_collection_release_body(g, ck, ce, fn);
  return fn;
}
void emit_arc_release_typed(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v) {
  (void)type;
  if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind)
    return;
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  if (LLVMTypeOf(v) != i8ptr)
    v = LLVMBuildBitCast(g->builder, v, i8ptr, "arc.rlt");
  zan_call2(g->builder,
            LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
            g->rt_release_dyn, &v, 1, "");
}

/* 构建已注册释放声明的类释放函数体（仅限实际引用/分配的类） */
void emit_all_class_releases(zan_irgen_t *g) {
  for (int i = 0; i < g->class_release_count; i++) {
    zan_symbol_t *sym = g->class_release[i].sym;
    zan_type_t *inst = g->class_release[i].inst;
    LLVMValueRef fn = g->class_release[i].fn;
    if (!sym || !fn || LLVMCountBasicBlocks(fn) > 0)
      continue;
    build_class_release_body(g, sym, inst, fn);
  }
}
/* ---- 站点号 → 描述符表（默认模式）-------------------------------------
 * 对象头字的高 32 位是 tw = 对象标记 | 站点号（docs/OBJECT_LAYOUT.md）。
 * 默认模式下按站点号查 __zan_desc_tbl 得描述符；表在 irgen 期间是占位，
 * 收尾时 emit_desc_table 按 desc_gv 定型。没有对象标记（字符串、数组描述符
 * 指针、soft scratch 前缀）或站点号越界的头字一律查出 null。 */
static void desc_tbl_ensure(zan_irgen_t *g) {
    if (g->g_desc_tbl) return;
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    /* 占位是外部声明（无初值）：发布模式的逐函数压缩器会提前优化已完成的
     * 函数，占位若是带初值的常量，表长/表项会被折成占位值（表长 1 → 全部
     * 越界）。定型时换成内部常量并 RAUW。 */
    g->g_desc_tbl = LLVMAddGlobal(g->mod, LLVMArrayType(i8p, 1), "__zan_desc_tbl.ph");
    g->g_desc_n = LLVMAddGlobal(g->mod, i64t, "__zan_desc_n.ph");
}
LLVMValueRef zan_desc_tbl_global(zan_irgen_t *g, LLVMValueRef *count_out) {
    desc_tbl_ensure(g);
    if (count_out) *count_out = g->g_desc_n;
    return g->g_desc_tbl;
}
LLVMValueRef zan_hdr_desc(zan_irgen_t *g, LLVMBuilderRef b, LLVMValueRef word, const char *name) {
    desc_tbl_ensure(g);
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef tw = LLVMBuildLShr(b, word, LLVMConstInt(i64t, 32, 0), "desc.tw");
    LLVMValueRef site =
        LLVMBuildAnd(b, tw, LLVMConstInt(i64t, ZAN_OBJ_TW_SITE_MASK, 0), "desc.site");
    LLVMValueRef is_obj = LLVMBuildICmp(
        b, LLVMIntNE, LLVMBuildAnd(b, tw, LLVMConstInt(i64t, ZAN_OBJ_TW_MARK, 0), "desc.mark"),
        LLVMConstInt(i64t, 0, 0), "desc.isobj");
    LLVMValueRef n = LLVMBuildLoad2(b, i64t, g->g_desc_n, "desc.n");
    LLVMValueRef ok =
        LLVMBuildAnd(b, is_obj, LLVMBuildICmp(b, LLVMIntULT, site, n, "desc.inrange"), "desc.ok");
    LLVMValueRef idx = LLVMBuildSelect(b, ok, site, LLVMConstInt(i64t, 0, 0), "desc.idx");
    LLVMValueRef slot = LLVMBuildGEP2(b, i8p, g->g_desc_tbl, &idx, 1, "desc.slot");
    return LLVMBuildLoad2(b, i8p, slot, name);
}
void emit_desc_table(zan_irgen_t *g) {
    if (!g->desc_hdr) return;
    desc_tbl_ensure(g);
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    unsigned n = g->leak_site_count > 0 ? (unsigned)g->leak_site_count : 1u;
    LLVMValueRef *elems = (LLVMValueRef *)calloc(n, sizeof(LLVMValueRef));
    if (!elems) {
        fprintf(stderr, "zanc: out of memory building the ARC descriptor table\n");
        exit(1);
    }
    for (unsigned i = 0; i < n; i++) {
        LLVMValueRef dg = (g->desc_gv && (int)i < g->leak_site_count) ? g->desc_gv[i] : NULL;
        elems[i] = dg ? (LLVMTypeOf(dg) == i8p ? dg : LLVMConstBitCast(dg, i8p)) : LLVMConstNull(i8p);
    }
    elems[0] = LLVMConstNull(i8p); /* 0 号站点是哨兵：未跟踪对象 */
    LLVMTypeRef at = LLVMArrayType(i8p, n);
    LLVMValueRef newg = LLVMAddGlobal(g->mod, at, "__zan_desc_tbl.final");
    LLVMSetLinkage(newg, LLVMInternalLinkage);
    LLVMSetGlobalConstant(newg, 1);
    LLVMSetInitializer(newg, LLVMConstArray(i8p, elems, n));
    free(elems);
    LLVMReplaceAllUsesWith(g->g_desc_tbl, newg);
    LLVMDeleteGlobal(g->g_desc_tbl);
    LLVMSetValueName2(newg, "__zan_desc_tbl", strlen("__zan_desc_tbl"));
    g->g_desc_tbl = newg;
    LLVMValueRef newn = LLVMAddGlobal(g->mod, i64t, "__zan_desc_n");
    LLVMSetLinkage(newn, LLVMInternalLinkage);
    LLVMSetGlobalConstant(newn, 1);
    LLVMSetInitializer(newn, LLVMConstInt(i64t, n, 0));
    LLVMReplaceAllUsesWith(g->g_desc_n, newn);
    LLVMDeleteGlobal(g->g_desc_n);
    g->g_desc_n = newn;
    g->desc_tbl_frozen = true;
}

void emit_site_live_tables(zan_irgen_t *g) {
  if (g->desc_hdr || !g->g_site_live || !g->g_site_count)
    return;
  LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  unsigned n = (unsigned)g->leak_site_count;
  LLVMTypeRef lt = LLVMArrayType(i64t, n);
  LLVMValueRef live = LLVMAddGlobal(g->mod, lt, "__zan_site_live_tbl");
  LLVMSetInitializer(live, LLVMConstNull(lt));
  LLVMSetLinkage(live, LLVMInternalLinkage);
  LLVMSetInitializer(g->g_site_live,
                     LLVMConstBitCast(live, LLVMPointerType(i64t, 0)));
  LLVMTypeRef nt = LLVMArrayType(i8p, n);
  LLVMValueRef names = LLVMAddGlobal(g->mod, nt, "__zan_site_names_tbl");
  LLVMSetInitializer(names, LLVMConstNull(nt));
  LLVMSetLinkage(names, LLVMInternalLinkage);
  LLVMSetInitializer(g->g_site_names,
                     LLVMConstBitCast(names, LLVMPointerType(i8p, 0)));
  LLVMSetInitializer(g->g_site_count, LLVMConstInt(i64t, n, 0));
}
void emit_site_dtor_table(zan_irgen_t *g) {
  if (!g->site_syms || g->desc_hdr)
    return;
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  int n = g->leak_site_count;
  if (n <= 0)
    return;
  LLVMValueRef *elems = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
  for (int i = 0; i < n; i++) {
    LLVMValueRef e = LLVMConstNull(i8ptr);
    if (g->site_coll && g->site_coll[i]) {
      LLVMValueRef fn = get_collection_release_decl(g, i);
      if (fn)
        e = LLVMConstBitCast(fn, i8ptr);
    } else if (g->site_syms[i]) {
      LLVMValueRef fn = get_class_release_decl(
          g, g->site_syms[i], g->site_inst ? g->site_inst[i] : NULL);
      if (fn)
        e = LLVMConstBitCast(fn, i8ptr);
    }
    elems[i] = e;
  }
  LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)n);
  LLVMValueRef arr = LLVMAddGlobal(g->mod, at, "__zan_site_dtor_tbl");
  LLVMSetInitializer(arr, LLVMConstArray(i8ptr, elems, (unsigned)n));
  LLVMSetLinkage(arr, LLVMInternalLinkage);
  LLVMSetInitializer(g->g_site_dtors,
                     LLVMConstBitCast(arr, LLVMPointerType(i8ptr, 0)));
  free(elems);
}
void emit_site_tyname_table(zan_irgen_t *g) {
  if (!g->site_syms || g->desc_hdr)
    return;
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  int n = g->leak_site_count;
  if (n <= 0)
    return;
  LLVMValueRef *elems = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
  typedef struct {
    LLVMValueRef gv;
    LLVMTypeRef at;
    LLVMValueRef *ptrs;
    int k;
  } tbl_tn_ent_t;
  tbl_tn_ent_t *tn_cache = NULL;
  int tn_cnt = 0, tn_cap = 0;
  for (int i = 0; i < n; i++) {
    elems[i] = LLVMConstNull(i8ptr);
    const char *names[64];
    int k = 0;
    if (g->site_coll && g->site_coll[i]) {
      int ck = g->site_coll[i];
      names[k++] = arc_collection_type_name(
          g, ck, g->site_coll_elem ? g->site_coll_elem[i] : NULL);
    } else if (g->site_syms[i]) {
      zan_symbol_t *cur = g->site_syms[i];
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
    }
    if (k == 0)
      continue;
    LLVMValueRef *nptr =
        (LLVMValueRef *)calloc((size_t)k + 1, sizeof(LLVMValueRef));
    for (int j = 0; j < k; j++)
      nptr[j] = zan_irgen_intern_type_name(g, names[j]);
    nptr[k] = LLVMConstNull(i8ptr);
    int hit = -1;
    for (int c = 0; c < tn_cnt; c++) {
      if (tn_cache[c].k == k && memcmp(tn_cache[c].ptrs, nptr,
                                       (size_t)k * sizeof(LLVMValueRef)) == 0) {
        hit = c;
        break;
      }
    }
    LLVMValueRef idxs[] = {LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0)};
    if (hit >= 0) {
      elems[i] = LLVMBuildGEP2(g->builder, tn_cache[hit].at, tn_cache[hit].gv,
                               idxs, 2, "tn.ptr");
      free(nptr);
      continue;
    }
    LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)k + 1);
    char gname[64];
    snprintf(gname, sizeof(gname), "__zan_tynames_%d", i);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, at, gname);
    LLVMSetInitializer(gv, LLVMConstArray(i8ptr, nptr, (unsigned)k + 1));
    LLVMSetLinkage(gv, LLVMInternalLinkage);
    LLVMSetGlobalConstant(gv, 1);
    LLVMSetUnnamedAddr(gv, LLVMGlobalUnnamedAddr);
    if (tn_cnt >= tn_cap) {
      int ncap = tn_cap ? (tn_cap * 2) : 32;
      tbl_tn_ent_t *nb =
          (tbl_tn_ent_t *)realloc(tn_cache, (size_t)ncap * sizeof(*nb));
      if (nb) {
        tn_cache = nb;
        tn_cap = ncap;
      }
    }
    if (tn_cnt < tn_cap) {
      LLVMValueRef *copy =
          (LLVMValueRef *)malloc((size_t)k * sizeof(LLVMValueRef));
      if (copy) {
        memcpy(copy, nptr, (size_t)k * sizeof(LLVMValueRef));
        tn_cache[tn_cnt].gv = gv;
        tn_cache[tn_cnt].at = at;
        tn_cache[tn_cnt].ptrs = copy;
        tn_cache[tn_cnt].k = k;
        tn_cnt++;
      }
    }
    free(nptr);
    elems[i] = LLVMBuildGEP2(g->builder, at, gv, idxs, 2, "tn.ptr");
  }
  for (int c = 0; c < tn_cnt; c++)
    free(tn_cache[c].ptrs);
  free(tn_cache);
  LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)n);
  LLVMValueRef arr = LLVMAddGlobal(g->mod, at, "__zan_site_tyname_tbl");
  LLVMSetInitializer(arr, LLVMConstArray(i8ptr, elems, (unsigned)n));
  LLVMSetLinkage(arr, LLVMInternalLinkage);
  LLVMSetInitializer(g->g_site_tynames,
                     LLVMConstBitCast(arr, LLVMPointerType(i8ptr, 0)));
  free(elems);
}
LLVMValueRef build_site_field_graph(zan_irgen_t *g, int site_idx) {
  char fg_name[64];
  snprintf(fg_name, sizeof(fg_name), "__zan_fg_%d", site_idx);
  LLVMValueRef existing = LLVMGetNamedGlobal(g->mod, fg_name);
  LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  if (existing)
    return LLVMConstBitCast(existing, i8p);

  LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
  uint32_t kind = 3;
  uint32_t ref_count = 0;
  uint32_t instance_size = 0;
  uint32_t offsets[256];
  memset(offsets, 0, sizeof(offsets));

  if (g->site_coll && g->site_coll[site_idx]) {
    int ck = g->site_coll[site_idx];
    if (ck == 1) { /* List */
      zan_type_t *elem_type =
          g->site_coll_elem ? g->site_coll_elem[site_idx] : NULL;
      if (elem_type &&
          (is_arc_managed_type(elem_type) || elem_type->kind == TYPE_STRING ||
           elem_type->kind == TYPE_DELEGATE || elem_type->kind == TYPE_ARRAY ||
           elem_type->kind == TYPE_OBJECT)) {
        kind = 1;
        ref_count = 1;
        offsets[0] = 16;
      } else {
        kind = 3;
        ref_count = 0;
      }
    } else if (ck == 3) { /* Dict */
      kind = 2;
      ref_count = 3;
      offsets[0] = 16;
      offsets[1] = 24;
      offsets[2] = 0;
    } else {
      kind = 3;
      ref_count = 0;
    }
  } else if (g->site_syms && g->site_syms[site_idx]) {
    zan_symbol_t *sym = g->site_syms[site_idx];
    zan_type_t *inst = g->site_inst ? g->site_inst[site_idx] : NULL;
    struct zan_struct_type_entry *e = get_struct_entry(g, sym);
    if (e && e->field_llvm) {
      int vptr = class_vptr_offset(sym);
      unsigned long cursor = vptr ? 8 : 0;
      int slot = vptr;
      for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if ((m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) ||
            field_member_is_static(m))
          continue;
        unsigned long fa = abi_align_of(e->field_llvm[slot]);
        unsigned long off = e->explicit_layout ? e->field_offsets[slot]
                                               : di_align_up(cursor, fa);
        unsigned long fsize = abi_size_of(e->field_llvm[slot]);
        if (!e->explicit_layout)
          cursor = off + fsize;
        slot++;

        zan_type_t *ft = m->type;
        if (inst)
          ft = subst_type_param_deep(g, ft, inst);
        if (!ft)
          continue;
        if (!(m->modifiers & MOD_WEAK)) {
          bool is_rc = (ft->kind == TYPE_STRING || ft->kind == TYPE_DELEGATE ||
                        ft->kind == TYPE_ARRAY || is_arc_managed_type(ft) ||
                        ft->kind == TYPE_OBJECT);
          if (is_rc && ref_count < 256) {
            offsets[ref_count++] = (uint32_t)off;
          }
        }
      }
      instance_size = (uint32_t)cursor;
      kind = (ref_count > 0) ? 0 : 3;
    }
  } else if (g->site_clo && site_idx < g->site_clo_cap &&
             g->site_clo[site_idx]) {
    /* 闭包记录 / 捕获盒：登记过的强引用槽（target、捕获变量、强捕获 this） */
    zan_clo_layout_t *cl = g->site_clo[site_idx];
    kind = cl->n > 0 ? 0 : 3;
    ref_count = (uint32_t)cl->n;
    for (int k = 0; k < cl->n; k++)
      offsets[k] = cl->offsets[k];
    instance_size = 24;
  } else {
    /* Closure or unknown site */
    kind = 0;
    ref_count = 1;
    offsets[0] = 16;
    instance_size = 24;
  }

  LLVMTypeRef ref_ty =
      LLVMStructTypeInContext(g->ctx, (LLVMTypeRef[]){i32t, i32t}, 2, 0);
  LLVMTypeRef refs_arr_ty = LLVMArrayType(ref_ty, (unsigned)ref_count);
  LLVMTypeRef fg_fields_ty[5] = {i32t, i32t, i32t, i32t, refs_arr_ty};
  LLVMTypeRef fg_ty = LLVMStructTypeInContext(g->ctx, fg_fields_ty, 5, 0);

  LLVMValueRef fg_gv = LLVMAddGlobal(g->mod, fg_ty, fg_name);
  LLVMSetLinkage(fg_gv, LLVMInternalLinkage);
  LLVMSetGlobalConstant(fg_gv, 1);

  LLVMValueRef *ref_elems = NULL;
  if (ref_count > 0) {
    ref_elems = (LLVMValueRef *)calloc((size_t)ref_count, sizeof(LLVMValueRef));
    for (uint32_t k = 0; k < ref_count; k++) {
      LLVMValueRef rvals[2] = {LLVMConstInt(i32t, offsets[k], 0),
                               LLVMConstInt(i32t, 1, 0)};
      ref_elems[k] = LLVMConstStructInContext(g->ctx, rvals, 2, 0);
    }
  }
  LLVMValueRef refs_val =
      LLVMConstArray(ref_ty, ref_elems, (unsigned)ref_count);
  if (ref_elems)
    free(ref_elems);

  LLVMValueRef fg_init[5] = {
      LLVMConstInt(i32t, kind, 0), LLVMConstInt(i32t, ref_count, 0),
      LLVMConstInt(i32t, instance_size, 0), LLVMConstInt(i32t, 0, 0), refs_val};
  LLVMSetInitializer(fg_gv, LLVMConstStructInContext(g->ctx, fg_init, 5, 0));
  return LLVMTypeOf(fg_gv) == i8p ? fg_gv : LLVMConstBitCast(fg_gv, i8p);
}
/* S13（docs/ZAN_STANDARD.md §3）：--check-leaks 退出环路诊断的字段名表。
 * 每个类站点一个以 NUL 结尾的 C 串数组，与 build_site_field_graph 的 refs[]
 * 同序——两边用同一判据（非静态、非 weak、引用计数类型）筛字段，运行时按
 * 边的 refs 下标取名，打印 "Node.Next -> ..."。非类站点为空指针。 */
static LLVMValueRef build_site_field_names(zan_irgen_t *g, int site_idx) {
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (g->site_clo && site_idx < g->site_clo_cap && g->site_clo[site_idx] &&
        g->site_clo[site_idx]->n > 0) {
        zan_clo_layout_t *cl = g->site_clo[site_idx];
        LLVMValueRef cnames[ZAN_CLO_LAYOUT_MAX];
        for (int k = 0; k < cl->n; k++)
            cnames[k] = zan_irgen_intern_string(g, cl->names[k] ? cl->names[k] : "?");
        char cgname[64];
        snprintf(cgname, sizeof(cgname), "__zan_fnames_%d", site_idx);
        LLVMTypeRef cat = LLVMArrayType(i8p, (unsigned)cl->n);
        LLVMValueRef cgv = LLVMAddGlobal(g->mod, cat, cgname);
        LLVMSetLinkage(cgv, LLVMInternalLinkage);
        LLVMSetGlobalConstant(cgv, 1);
        LLVMSetInitializer(cgv, LLVMConstArray(i8p, cnames, (unsigned)cl->n));
        return LLVMConstBitCast(cgv, i8p);
    }
    if ((g->site_coll && g->site_coll[site_idx]) || !g->site_syms ||
        !g->site_syms[site_idx])
        return LLVMConstNull(i8p);
    zan_symbol_t *sym = g->site_syms[site_idx];
    zan_type_t *inst = g->site_inst ? g->site_inst[site_idx] : NULL;
    struct zan_struct_type_entry *e = get_struct_entry(g, sym);
    if (!e || !e->field_llvm) return LLVMConstNull(i8p);
    LLVMValueRef names[256];
    unsigned n = 0;
    for (int i = 0; i < sym->member_count && n < 256; i++) {
        zan_symbol_t *m = sym->members[i];
        if ((m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) || field_member_is_static(m))
            continue;
        zan_type_t *ft = m->type;
        if (inst) ft = subst_type_param_deep(g, ft, inst);
        if (!ft || (m->modifiers & MOD_WEAK)) continue;
        if (!(ft->kind == TYPE_STRING || ft->kind == TYPE_DELEGATE ||
              ft->kind == TYPE_ARRAY || is_arc_managed_type(ft) ||
              ft->kind == TYPE_OBJECT))
            continue;
        char buf[128];
        size_t len = m->name.len < sizeof(buf) - 1 ? m->name.len : sizeof(buf) - 1;
        memcpy(buf, m->name.str, len);
        buf[len] = '\0';
        names[n++] = zan_irgen_intern_string(g, buf);
    }
    if (n == 0) return LLVMConstNull(i8p);
    char gname[64];
    snprintf(gname, sizeof(gname), "__zan_fnames_%d", site_idx);
    LLVMTypeRef at = LLVMArrayType(i8p, n);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, at, gname);
    LLVMSetLinkage(gv, LLVMInternalLinkage);
    LLVMSetGlobalConstant(gv, 1);
    LLVMSetInitializer(gv, LLVMConstArray(i8p, names, n));
    return LLVMConstBitCast(gv, i8p);
}

void zan_set_closure_site_layout(zan_irgen_t *g, int site_idx, LLVMTypeRef rec_ty,
                                 const unsigned *fields, const char *const *names, int n) {
    if (site_idx < 0 || n <= 0) return;
    if (site_idx >= g->site_clo_cap) {
        int ncap = g->site_clo_cap ? g->site_clo_cap : 256;
        while (ncap <= site_idx) ncap *= 2;
        zan_clo_layout_t **na = (zan_clo_layout_t **)realloc(g->site_clo,
            (size_t)ncap * sizeof(*na));
        if (!na) return;
        memset(na + g->site_clo_cap, 0, (size_t)(ncap - g->site_clo_cap) * sizeof(*na));
        g->site_clo = na;
        g->site_clo_cap = ncap;
    }
    zan_clo_layout_t *cl = zan_arena_alloc(g->arena, sizeof(*cl));
    if (!cl) return;
    LLVMTargetDataRef layout = LLVMGetModuleDataLayout(g->mod);
    cl->n = 0;
    for (int k = 0; k < n && cl->n < ZAN_CLO_LAYOUT_MAX; k++) {
        cl->offsets[cl->n] = (uint32_t)LLVMOffsetOfElement(layout, rec_ty, fields[k]);
        cl->names[cl->n] = names[k];
        cl->n++;
    }
    g->site_clo[site_idx] = cl;
}

/* 退出报告引用的字段名表全局（i8**）。报告函数在 main 发射时就要引用它，
 * 表本身要等全部站点确定后才填（emit_site_field_graph_table）。 */
LLVMValueRef zan_site_field_names_global(zan_irgen_t *g) {
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef gv = LLVMGetNamedGlobal(g->mod, "__zan_site_field_names");
    if (gv) return gv;
    gv = LLVMAddGlobal(g->mod, LLVMPointerType(i8p, 0), "__zan_site_field_names");
    LLVMSetLinkage(gv, LLVMInternalLinkage);
    LLVMSetInitializer(gv, LLVMConstNull(LLVMPointerType(i8p, 0)));
    return gv;
}

void emit_site_field_graph_table(zan_irgen_t *g) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  int n = g->leak_site_count;
  if (n <= 0)
    return;
  LLVMValueRef *elems = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
  for (int i = 0; i < n; i++) {
    elems[i] = build_site_field_graph(g, i);
  }
  LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)n);
  LLVMValueRef arr = LLVMAddGlobal(g->mod, at, "__zan_site_field_graph_tbl");
  LLVMSetInitializer(arr, LLVMConstArray(i8ptr, elems, (unsigned)n));
  LLVMSetLinkage(arr, LLVMInternalLinkage);
  if (g->g_site_field_graphs) {
    LLVMSetInitializer(g->g_site_field_graphs,
                       LLVMConstBitCast(arr, LLVMPointerType(i8ptr, 0)));
  }
  LLVMValueRef fnames = LLVMGetNamedGlobal(g->mod, "__zan_site_field_names");
  if (fnames) {
    for (int i = 0; i < n; i++)
      elems[i] = build_site_field_names(g, i);
    LLVMValueRef ntbl = LLVMAddGlobal(g->mod, at, "__zan_site_field_names_tbl");
    LLVMSetInitializer(ntbl, LLVMConstArray(i8ptr, elems, (unsigned)n));
    LLVMSetLinkage(ntbl, LLVMInternalLinkage);
    LLVMSetGlobalConstant(ntbl, 1);
    LLVMSetInitializer(fnames,
                       LLVMConstBitCast(ntbl, LLVMPointerType(i8ptr, 0)));
  }
  free(elems);
}

void zan_irgen_emit_arc_desc_init(zan_irgen_t *g, bool need_field_graph);
