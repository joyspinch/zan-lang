/* Phase 2a / 2b: __Canon instance canonicalization & __gctx dictionary */

bool inst_uses_canon(zan_irgen_t *g, zan_type_t *inst) {
  if (!inst || inst->type_arg_count <= 0)
    return false;
  if (is_task_or_binding_type(inst))
    return false;
  if (inst->sym && !sym_supports_canon(g, inst->sym))
    return false;
  for (int i = 0; i < inst->type_arg_count; i++) {
    if (type_is_canon_eligible(inst->type_args[i]))
      return true;
  }
  return false;
}

bool args_use_canon(zan_irgen_t *g, zan_symbol_t *owner_sym, zan_type_t **args,
                    int argc) {
  if (!args || argc <= 0)
    return false;
  if (owner_sym && owner_sym->type && is_task_or_binding_type(owner_sym->type))
    return false;
  if (owner_sym && owner_sym->name.str &&
      ((owner_sym->name.len == 7 &&
        memcmp(owner_sym->name.str, "Binding", 7) == 0) ||
       (owner_sym->name.len == 4 &&
        memcmp(owner_sym->name.str, "Task", 4) == 0) ||
       (owner_sym->name.len == 9 &&
        memcmp(owner_sym->name.str, "ValueTask", 9) == 0)))
    return false;
  if (owner_sym && !sym_supports_canon(g, owner_sym))
    return false;
  for (int i = 0; i < argc; i++) {
    if (type_is_canon_eligible(args[i]))
      return true;
  }
  return false;
}

zan_type_t *canonicalize_inst(zan_irgen_t *g, zan_type_t *inst) {
  if (!g || !inst || inst->type_arg_count <= 0)
    return inst;
  if (!inst_uses_canon(g, inst))
    return inst;
  bool chg = false;
  zan_type_t **na = (zan_type_t **)zan_arena_alloc(
      g->binder->arena, (size_t)inst->type_arg_count * sizeof(zan_type_t *));
  for (int i = 0; i < inst->type_arg_count; i++) {
    if (type_is_canon_eligible(inst->type_args[i])) {
      na[i] = get_canon_sentinel_type(g);
      if (na[i] != inst->type_args[i])
        chg = true;
    } else {
      na[i] = inst->type_args[i];
    }
  }
  if (!chg)
    return inst;
  zan_type_t *nt =
      (zan_type_t *)zan_arena_alloc(g->binder->arena, sizeof(zan_type_t));
  *nt = *inst;
  nt->type_args = na;
  return nt;
}

zan_type_t **canonicalize_args(zan_irgen_t *g, zan_symbol_t *owner_sym,
                               zan_type_t **args, int argc,
                               zan_type_t **scratch, int scratch_cap) {
  if (!g || !args || argc <= 0)
    return args;
  if (!args_use_canon(g, owner_sym, args, argc))
    return args;
  zan_type_t **out =
      (argc <= scratch_cap && scratch)
          ? scratch
          : (zan_type_t **)zan_arena_alloc(g->binder->arena,
                                           (size_t)argc * sizeof(zan_type_t *));
  for (int i = 0; i < argc; i++) {
    out[i] = type_is_canon_eligible(args[i]) ? get_canon_sentinel_type(g)
                                             : args[i];
  }
  return out;
}

/* Construct a constraint-preserving handle for a canonicalized type parameter
 * inside a shared __Canon body. The handle's kind/sym mirrors any `where`
 * constraint (class base or interface), while is_canon=1 marks it as an
 * erased reference handle backed by __gctx[canon_tp_kind, canon_tp_idx]. */
static zan_type_t *make_canon_tp_handle(zan_irgen_t *g, zan_ast_node_t *decl,
                                        zan_istr_t tp_name, uint8_t tp_kind,
                                        uint16_t tp_idx) {
  zan_type_t *h =
      (zan_type_t *)zan_arena_alloc(g->binder->arena, sizeof(zan_type_t));
  memset(h, 0, sizeof(*h));
  h->kind = TYPE_OBJECT;
  h->name = tp_name;
  h->is_canon = 1;
  h->canon_tp_kind = tp_kind;
  h->canon_tp_idx = tp_idx;

  if (decl && g && g->binder) {
    zan_ast_list_t *constraints = NULL;
    if (decl->kind == AST_CLASS_DECL || decl->kind == AST_STRUCT_DECL ||
        decl->kind == AST_INTERFACE_DECL) {
      constraints = zan_ast_type_where_clauses(decl);
    } else if (decl->kind == AST_METHOD_DECL) {
      constraints = zan_ast_method_where_clauses(decl);
    }
    if (constraints) {
      zan_type_t *base_cls = NULL;
      zan_type_t *first_iface = NULL;
      for (int ci = 0; ci < constraints->count; ci++) {
        zan_ast_node_t *wc = constraints->items[ci];
        if (!wc || wc->kind != AST_WHERE_CLAUSE)
          continue;
        if (wc->where_clause.param_name.len != tp_name.len ||
            (tp_name.len &&
             memcmp(wc->where_clause.param_name.str, tp_name.str,
                    (size_t)tp_name.len) != 0))
          continue;
        for (int bi = 0; bi < wc->where_clause.constraints.count; bi++) {
          zan_ast_node_t *bnode = wc->where_clause.constraints.items[bi];
          if (!bnode || bnode->kind != AST_TYPE_REF)
            continue;
          zan_type_t *bt = zan_binder_resolve_type(g->binder, bnode);
          if (!bt || bt == g->binder->type_error)
            continue;
          if (bt->kind == TYPE_CLASS && !base_cls)
            base_cls = bt;
          else if (bt->kind == TYPE_INTERFACE && !first_iface)
            first_iface = bt;
        }
      }
      zan_type_t *chosen = base_cls ? base_cls : first_iface;
      if (chosen) {
        h->kind = chosen->kind;
        h->sym = chosen->sym;
        h->name = chosen->name;
        h->base_type = chosen->base_type;
        h->bases_resolved = chosen->bases_resolved;
        h->interfaces = chosen->interfaces;
        h->interface_count = chosen->interface_count;
        h->type_args = chosen->type_args;
        h->type_arg_count = chosen->type_arg_count;
        h->is_canon = 1;
        h->canon_tp_kind = tp_kind;
        h->canon_tp_idx = tp_idx;
      }
    }
  }
  return h;
}

zan_type_t *make_canon_body_inst(zan_irgen_t *g, zan_symbol_t *type_sym,
                                 zan_type_t *canon_inst) {
  if (!g || !canon_inst || canon_inst->type_arg_count <= 0)
    return canon_inst;
  zan_ast_list_t *tps = NULL;
  if (type_sym && type_sym->decl &&
      (type_sym->decl->kind == AST_CLASS_DECL ||
       type_sym->decl->kind == AST_STRUCT_DECL))
    tps = &type_sym->decl->type_decl.type_params;
  zan_type_t **args = (zan_type_t **)zan_arena_alloc(
      g->binder->arena,
      (size_t)canon_inst->type_arg_count * sizeof(zan_type_t *));
  for (int i = 0; i < canon_inst->type_arg_count; i++) {
    if (canon_inst->type_args[i] && canon_inst->type_args[i]->is_canon) {
      zan_istr_t tpn = canon_inst->type_args[i]->name;
      if (tps && i < tps->count && tps->items[i] &&
          tps->items[i]->kind == AST_IDENTIFIER)
        tpn = tps->items[i]->ident.name;
      args[i] = make_canon_tp_handle(
          g, type_sym ? type_sym->decl : NULL, tpn, 1, (uint16_t)i);
    } else {
      args[i] = canon_inst->type_args[i];
    }
  }
  zan_type_t *nt =
      (zan_type_t *)zan_arena_alloc(g->binder->arena, sizeof(zan_type_t));
  *nt = *canon_inst;
  nt->type_args = args;
  return nt;
}

zan_type_t **make_canon_body_bind(zan_irgen_t *g, zan_ast_node_t *member,
                                  zan_type_t **canon_bind, int bindc) {
  if (!g || !canon_bind || bindc <= 0)
    return canon_bind;
  zan_ast_list_t *tps =
      (member && member->kind == AST_METHOD_DECL)
          ? &member->method_decl.type_params
          : NULL;
  zan_type_t **out = (zan_type_t **)zan_arena_alloc(
      g->binder->arena, (size_t)bindc * sizeof(zan_type_t *));
  for (int i = 0; i < bindc; i++) {
    if (canon_bind[i] && canon_bind[i]->is_canon) {
      zan_istr_t tpn = canon_bind[i]->name;
      if (tps && i < tps->count && tps->items[i] &&
          tps->items[i]->kind == AST_IDENTIFIER)
        tpn = tps->items[i]->ident.name;
      out[i] = make_canon_tp_handle(g, member, tpn, 2, (uint16_t)i);
    } else {
      out[i] = canon_bind[i];
    }
  }
  return out;
}

static bool gctx_entry_matches(zan_gctx_entry_t *e, bool is_mspec,
                               zan_symbol_t *sym, zan_type_t *inst,
                               zan_type_t **bind, int bindc) {
  if (e->is_method_spec != is_mspec || e->sym != sym)
    return false;
  if (!types_equal(e->concrete_inst, inst))
    return false;
  if (e->bindc != bindc)
    return false;
  if (bindc > 0 && !type_arglists_equal(e->concrete_bind, e->bindc, bind, bindc))
    return false;
  return true;
}

static bool gctx_is_owner_key(bool is_mspec, zan_type_t *inst,
                              zan_type_t *canon_inst, zan_type_t **bind,
                              zan_type_t **canon_bind, int bindc) {
  if (!types_equal(inst, canon_inst))
    return false;
  if (is_mspec && bindc > 0 && !type_arglists_equal(bind, bindc, canon_bind, bindc))
    return false;
  return true;
}

static int find_or_add_gctx_raw(zan_irgen_t *g, bool is_mspec, zan_symbol_t *sym,
                                zan_type_t *inst, zan_type_t *canon_inst,
                                zan_type_t **bind, zan_type_t **canon_bind,
                                int bindc, int owner_idx) {
  for (int i = 0; i < g->gctx_entry_count; i++) {
    if (gctx_entry_matches(&g->gctx_entries[i], is_mspec, sym, inst, bind,
                           bindc))
      return i;
  }
  if (g->gctx_entry_count >= g->gctx_entry_cap) {
    int ncap = g->gctx_entry_cap ? g->gctx_entry_cap * 2 : 64;
    g->gctx_entries = (zan_gctx_entry_t *)realloc(
        g->gctx_entries, (size_t)ncap * sizeof(zan_gctx_entry_t));
    g->gctx_entry_cap = ncap;
  }
  int idx = g->gctx_entry_count++;
  zan_gctx_entry_t *e = &g->gctx_entries[idx];
  memset(e, 0, sizeof(*e));
  e->is_method_spec = is_mspec;
  e->sym = sym;
  e->concrete_inst = inst;
  e->canon_inst = canon_inst;
  e->bindc = bindc;
  e->owner_idx = owner_idx;
  if (bindc > 0) {
    e->concrete_bind = (zan_type_t **)malloc((size_t)bindc * sizeof(zan_type_t *));
    e->canon_bind = (zan_type_t **)malloc((size_t)bindc * sizeof(zan_type_t *));
    memcpy(e->concrete_bind, bind, (size_t)bindc * sizeof(zan_type_t *));
    memcpy(e->canon_bind, canon_bind, (size_t)bindc * sizeof(zan_type_t *));
  }

  /* Create opaque/placeholder global pointer if not owner-only template.
   * Note: every concrete instantiation gets its own global `@__zan_gctx_<mangled>`.
   * Even if owner itself is never instantiated concretely at runtime, we still
   * allocate an entry for owner at owner_idx == -1 to hold slot recipes. */
  if (!gctx_is_owner_key(is_mspec, inst, canon_inst, bind, canon_bind, bindc)) {
    char name[512];
    char suf[256];
    suf[0] = '\0';
    if (inst && inst->type_arg_count > 0)
      mangle_inst_suffix(suf, sizeof(suf), inst);
    if (is_mspec) {
      size_t off = 0;
      char msuf[256];
      msuf[0] = '\0';
      for (int bi = 0; bi < bindc; bi++) {
        int w = snprintf(msuf + off, sizeof(msuf) - off, "$");
        if (w > 0)
          off += (size_t)w;
        mangle_type_token(msuf, sizeof(msuf), &off, bind[bi]);
      }
      snprintf(name, sizeof(name), "__zan_gctx_%s_%.*s%s%s_%d",
               sym && sym->parent ? sym->parent->name.str : "G",
               sym ? (int)sym->name.len : 1, sym ? sym->name.str : "m", suf,
               msuf, idx);
    } else {
      snprintf(name, sizeof(name), "__zan_gctx_%.*s%s_%d",
               sym ? (int)sym->name.len : 1, sym ? sym->name.str : "C", suf,
               idx);
    }
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    /* Start with [1 x i8*] if materialized later, or resize in materialize.
     * Wait: in LLVM IR global type must match initializer type! So we defer
     * creating the LLVMGlobal until materialize_pending_gctx? No! Callers need
     * an i8* LLVMValueRef NOW before owner finishes emitting all slots!
     * Solution: create an internal alias or fixed-name external/private
     * constant global? In LLVM C API, LLVMConstBitCast on a forward-declared
     * [0 x i8*] global works? No, LLVMGetGlobalValueType cannot change after
     * creation. Instead: create `[1 x i8*]`? No, if N > 1 we'd need replacement.
     * Wait: create an internal `[1 x i8*]*`? Even cleaner: create
     * `@__zan_gctx_ptr_<idx>` as an `internal global i8*`? No, constant i8*
     * table needs `[N x i8*]`! How do we reference `[N x i8*]` before N is
     * known? Use a forward `global [0 x i8*]`? No, LLVM bitcast of a forward
     * global then `LLVMReplaceAllUsesWith` on the final `[N x i8*]` global!
     * In LLVM IR, `LLVMReplaceAllUsesWith(old_gv, LLVMConstBitCast(new_gv, i8ptr))`
     * + `LLVMDeleteGlobal(old_gv)` is trivial, O(uses), and keeps the final
     * IR 100% clean constant `[N x i8*]` globals with zero extra indirection! */
    LLVMTypeRef arr0 = LLVMArrayType(i8ptr, 0);
    LLVMValueRef placeholder = LLVMAddGlobal(g->mod, arr0, name);
    LLVMSetLinkage(placeholder, LLVMInternalLinkage);
    e->gv = placeholder;
    e->gv_ty = arr0;
  }
  return idx;
}

int get_or_register_gctx(zan_irgen_t *g, bool is_mspec, zan_symbol_t *sym,
                         zan_type_t *inst, zan_type_t **bind, int bindc) {
  if (!g || !sym)
    return -1;
  zan_type_t *canon_inst = inst ? canonicalize_inst(g, inst) : NULL;
  zan_type_t *scratch[8];
  zan_type_t **canon_bind =
      (bind && bindc > 0)
          ? canonicalize_args(g, is_mspec ? sym : NULL, bind, bindc, scratch, 8)
          : NULL;
  int owner_idx = find_or_add_gctx_raw(g, is_mspec, sym, canon_inst, canon_inst,
                                       canon_bind, canon_bind, bindc, -1);
  if (gctx_is_owner_key(is_mspec, inst, canon_inst, bind, canon_bind, bindc))
    return owner_idx;
  return find_or_add_gctx_raw(g, is_mspec, sym, inst, canon_inst, bind,
                              canon_bind, bindc, owner_idx);
}

LLVMValueRef get_gctx_global_ptr(zan_irgen_t *g, int gctx_idx) {
  if (!g || gctx_idx < 0 || gctx_idx >= g->gctx_entry_count)
    return NULL;
  LLVMValueRef gv = g->gctx_entries[gctx_idx].gv;
  if (!gv)
    return NULL;
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  return LLVMConstBitCast(gv, i8ptr);
}

/* Record a slot on the current canonical owner and emit a load from cur_gctx_val. */
static bool gctx_slot_equal(const zan_gctx_slot_t *a, const zan_gctx_slot_t *b) {
  if (a->kind != b->kind || a->sym != b->sym || a->site_id != b->site_id ||
      a->coll_kind != b->coll_kind || a->bindc != b->bindc)
    return false;
  if (!types_equal(a->template_ty, b->template_ty))
    return false;
  if (a->bindc > 0 && !type_arglists_equal(a->bind, a->bindc, b->bind, b->bindc))
    return false;
  return true;
}

int canon_record_slot(zan_irgen_t *g, zan_gctx_slot_kind_t kind,
                      zan_symbol_t *sym, zan_type_t *template_ty,
                      zan_type_t **bind, int bindc, int site_id,
                      int coll_kind) {
  if (!g || g->cur_gctx_owner_idx < 0 ||
      g->cur_gctx_owner_idx >= g->gctx_entry_count)
    return -1;
  zan_gctx_entry_t *owner = &g->gctx_entries[g->cur_gctx_owner_idx];
  zan_gctx_slot_t key;
  memset(&key, 0, sizeof(key));
  key.kind = kind;
  key.sym = sym;
  key.template_ty = template_ty;
  key.bind = bind;
  key.bindc = bindc;
  key.site_id = site_id;
  key.coll_kind = coll_kind;
  for (int i = 0; i < owner->slot_count; i++) {
    if (gctx_slot_equal(&owner->slots[i], &key))
      return i;
  }
  if (owner->slot_count >= owner->slot_cap) {
    int ncap = owner->slot_cap ? owner->slot_cap * 2 : 16;
    owner->slots = (zan_gctx_slot_t *)realloc(
        owner->slots, (size_t)ncap * sizeof(zan_gctx_slot_t));
    owner->slot_cap = ncap;
  }
  int sidx = owner->slot_count++;
  owner->slots[sidx] = key;
  if (bindc > 0 && bind) {
    owner->slots[sidx].bind =
        (zan_type_t **)zan_arena_alloc(g->binder->arena,
                                       (size_t)bindc * sizeof(zan_type_t *));
    memcpy(owner->slots[sidx].bind, bind, (size_t)bindc * sizeof(zan_type_t *));
  }
  return sidx;
}

LLVMValueRef emit_gctx_load_slot(zan_irgen_t *g, zan_gctx_slot_kind_t kind,
                                 zan_symbol_t *sym, zan_type_t *template_ty,
                                 zan_type_t **bind, int bindc, int site_id,
                                 int coll_kind) {
  if (!g || !g->cur_canon_body || !g->cur_gctx_val)
    return NULL;
  int slot = canon_record_slot(g, kind, sym, template_ty, bind, bindc, site_id,
                               coll_kind);
  if (slot < 0)
    return NULL;
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMValueRef base = g->cur_gctx_val;
  if (LLVMTypeOf(base) != LLVMPointerType(i8ptr, 0))
    base = LLVMBuildBitCast(g->builder, base, LLVMPointerType(i8ptr, 0),
                            "gctx.p");
  LLVMValueRef idx = LLVMConstInt(i64, (unsigned long long)slot, 0);
  LLVMValueRef gep = LLVMBuildInBoundsGEP2(g->builder, i8ptr, base, &idx, 1,
                                           "gctx.slot");
  return LLVMBuildLoad2(g->builder, i8ptr, gep, "gctx.val");
}

/* Substitute canonical tp handles (canon_tp_kind 1=class, 2=method) using
 * the concrete owner entry's concrete_inst / concrete_bind. */
static zan_type_t *subst_canon_handles(zan_irgen_t *g, zan_type_t *t,
                                       zan_gctx_entry_t *concrete) {
  if (!g || !t || !concrete)
    return t;
  if (t->is_canon) {
    if (t->canon_tp_kind == 1 && concrete->concrete_inst &&
        t->canon_tp_idx < (uint16_t)concrete->concrete_inst->type_arg_count) {
      return concrete->concrete_inst->type_args[t->canon_tp_idx];
    }
    if (t->canon_tp_kind == 2 && concrete->concrete_bind &&
        t->canon_tp_idx < (uint16_t)concrete->bindc) {
      return concrete->concrete_bind[t->canon_tp_idx];
    }
    return t;
  }
  if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE) {
    zan_type_t *ne = subst_canon_handles(g, t->element_type, concrete);
    if (ne == t->element_type)
      return t;
    zan_type_t *nt =
        (zan_type_t *)zan_arena_alloc(g->binder->arena, sizeof(zan_type_t));
    *nt = *t;
    nt->element_type = ne;
    return nt;
  }
  if (t->type_arg_count > 0) {
    bool chg = false;
    zan_type_t **na = (zan_type_t **)zan_arena_alloc(
        g->binder->arena, (size_t)t->type_arg_count * sizeof(zan_type_t *));
    for (int i = 0; i < t->type_arg_count; i++) {
      na[i] = subst_canon_handles(g, t->type_args[i], concrete);
      if (na[i] != t->type_args[i])
        chg = true;
    }
    if (!chg)
      return t;
    zan_type_t *nt =
        (zan_type_t *)zan_arena_alloc(g->binder->arena, sizeof(zan_type_t));
    *nt = *t;
    nt->type_args = na;
    return nt;
  }
  return t;
}

static LLVMValueRef materialize_gctx_slot_val(zan_irgen_t *g,
                                              zan_gctx_entry_t *concrete,
                                              zan_gctx_slot_t *s) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  zan_type_t *cty =
      s->template_ty ? subst_canon_handles(g, s->template_ty, concrete) : NULL;

  switch (s->kind) {
  case GCTX_SLOT_TYPEINFO: {
    if (!cty)
      return LLVMConstNull(i8ptr);
    char buf[256];
    int len = render_type_full(cty, buf, (int)sizeof(buf));
    LLVMValueRef ti = refl_meta_for(g, cty, buf, len);
    if (!ti)
      return LLVMConstNull(i8ptr);
    if (LLVMTypeOf(ti) != i8ptr)
      ti = LLVMConstBitCast(ti, i8ptr);
    return ti;
  }
  case GCTX_SLOT_TYPENAME: {
    if (!cty)
      return LLVMConstNull(i8ptr);
    const char *nm = arc_runtime_type_name(g, cty);
    if (!nm || !nm[0])
      nm = "object";
    return zan_irgen_intern_string(g, nm);
  }
  case GCTX_SLOT_NEW_TP: {
    if (!cty)
      return LLVMConstNull(i8ptr);
    if (cty->kind == TYPE_OBJECT) {
      const char *wname = "__zan_new_tp_object";
      LLVMValueRef wfn = LLVMGetNamedFunction(g->mod, wname);
      if (!wfn) {
        LLVMTypeRef wty = LLVMFunctionType(i8ptr, NULL, 0, 0);
        wfn = LLVMAddFunction(g->mod, wname, wty);
        zan_set_module_local(wfn);
        zan_builder_point_t pt = zan_builder_save(g);
        uint32_t saved_file = g->di_cur_file;
        uint32_t saved_line = g->di_cur_line;
        uint32_t saved_col = g->di_cur_col;
        LLVMMetadataRef saved_last_loc = g->di_last_loc;
        LLVMValueRef saved_last_fn = g->di_last_loc_fn;
        g->current_fn = wfn;
        LLVMBasicBlockRef entry =
            LLVMAppendBasicBlockInContext(g->ctx, wfn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, entry);
        di_clear(g);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        int site_idx = reserve_arc_site(g, NULL, NULL, 0, NULL);
        LLVMTypeRef alloc_ty =
            LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i64, i64, i8ptr}, 3, 0);
        LLVMValueRef raw = zan_call2(
            g->builder, alloc_ty, g->rt_alloc,
            (LLVMValueRef[]){LLVMConstInt(i64, 8, 0), arc_site_arg(g, site_idx),
                             LLVMConstNull(i8ptr)},
            3, "ntp.obj");
        LLVMValueRef p64 =
            LLVMBuildBitCast(g->builder, raw, LLVMPointerType(i64, 0), "objp");
        zan_store_fit(g, LLVMConstInt(i64, 0, 0), p64);
        LLVMBuildRet(g->builder, raw);
        zan_builder_restore(g, pt);
        g->di_cur_file = saved_file;
        g->di_cur_line = saved_line;
        g->di_cur_col = saved_col;
        g->di_last_loc = saved_last_loc;
        g->di_last_loc_fn = saved_last_fn;
      }
      return LLVMConstBitCast(wfn, i8ptr);
    }
    if (!cty->sym)
      return LLVMConstNull(i8ptr);
    /* Build or reuse a zero-arg wrapper `i8* __zan_new_tp_<...>(void)` so
     * `new T()` in __Canon always has a uniform `i8* (*)()` call signature. */
    char wname[512];
    char suf[256];
    suf[0] = '\0';
    if (cty->type_arg_count > 0)
      mangle_inst_suffix(suf, sizeof(suf), cty);
    snprintf(wname, sizeof(wname), "__zan_new_tp_%.*s%s",
             (int)cty->sym->name.len, cty->sym->name.str, suf);
    LLVMValueRef wfn = LLVMGetNamedFunction(g->mod, wname);
    if (!wfn) {
      LLVMTypeRef wty = LLVMFunctionType(i8ptr, NULL, 0, 0);
      wfn = LLVMAddFunction(g->mod, wname, wty);
      zan_set_module_local(wfn);
      zan_builder_point_t pt = zan_builder_save(g);
      uint32_t saved_file = g->di_cur_file;
      uint32_t saved_line = g->di_cur_line;
      uint32_t saved_col = g->di_cur_col;
      LLVMMetadataRef saved_last_loc = g->di_last_loc;
      LLVMValueRef saved_last_fn = g->di_last_loc_fn;
      zan_symbol_t *save_type = g->current_type_sym;
      zan_type_t *save_inst = g->cur_inst;
      bool save_canon = g->cur_canon_body;
      LLVMValueRef save_gctx = g->cur_gctx_val;
      int save_owner = g->cur_gctx_owner_idx;
      LLVMValueRef save_this = g->current_this;
      void *save_locals = g->current_locals;

      zan_symbol_t *csym = cty->sym;
      g->current_fn = wfn;
      g->current_type_sym = csym;
      g->cur_inst = (cty->type_arg_count > 0) ? cty : NULL;
      g->cur_canon_body = false;
      g->cur_gctx_val = NULL;
      g->cur_gctx_owner_idx = -1;
      g->current_this = NULL;

      LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, wfn, "entry");
      LLVMPositionBuilderAtEnd(g->builder, entry);
      di_clear(g);
      local_scope_t *tmp_locals = local_scope_new(g->arena);
      g->current_locals = tmp_locals;
      LLVMValueRef obj = NULL;
      if (csym->kind == SYM_CLASS) {
        LLVMTypeRef st = get_struct_llvm_type(g, csym);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        int site_idx = reserve_arc_site(g, csym, g->cur_inst, 0, NULL);
        LLVMValueRef sz = st ? LLVMSizeOf(st) : LLVMConstInt(i64, 16, 0);
        LLVMTypeRef alloc_ty =
            LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i64, i64, i8ptr}, 3, 0);
        LLVMValueRef raw = zan_call2(
            g->builder, alloc_ty, g->rt_alloc,
            (LLVMValueRef[]){sz, arc_site_arg(g, site_idx), LLVMConstNull(i8ptr)},
            3, "ntp.raw");
        if (st && csym->decl && csym->decl->kind == AST_CLASS_DECL) {
          LLVMValueRef typed =
              LLVMBuildBitCast(g->builder, raw, LLVMPointerType(st, 0), "ntp.p");
          zan_store_fit(g, LLVMConstNull(st), typed);
          if (class_has_virtual_methods(csym)) {
            LLVMValueRef vt =
                (g->cur_inst && is_user_generic_sym(csym))
                    ? get_vtable_global_inst(g, csym, g->cur_inst)
                    : get_vtable_global(g, csym);
            if (vt && LLVMCountStructElementTypes(st) > 0) {
              LLVMValueRef vt_slot =
                  LLVMBuildStructGEP2(g->builder, st, typed, 0, "ntp.vt");
              zan_store_fit(g, LLVMConstBitCast(vt, i8ptr), vt_slot);
            }
          }
          /* Call 0-arg (or all-default-arg) constructor if present */
          zan_ast_list_t empty_args;
          zan_ast_list_init(&empty_args);
          zan_ast_list_t filled_args;
          zan_ast_list_init(&filled_args);
          zan_ast_list_t *cargs = &empty_args;
          struct zan_ctor_entry *ctor =
              find_ctor_inst(g, csym, g->cur_inst, &empty_args, NULL, NULL);
          if (!ctor &&
              fill_ctor_default_args(g, csym, &empty_args, &filled_args)) {
            ctor =
                find_ctor_inst(g, csym, g->cur_inst, &filled_args, NULL, NULL);
            if (ctor)
              cargs = &filled_args;
          }
          LLVMTypeRef cty_fn = NULL;
          LLVMValueRef cfn = NULL;
          LLVMValueRef cgctx = NULL;
          if (ctor) {
            if (g->cur_inst && is_user_generic_sym(csym)) {
              cfn = find_generic_ctor(g, csym, ctor->decl, g->cur_inst->type_args,
                                      g->cur_inst->type_arg_count, &cty_fn);
              cgctx = resolve_gctx_arg_for_inst(g, false, csym, g->cur_inst,
                                                NULL, 0);
            }
            if (!cfn) {
              cfn = ctor->fn;
              cty_fn = ctor->fn_type;
            }
          }
          if (cfn && cty_fn) {
            int argc = cargs->count + 1;
            LLVMValueRef *call_args =
                (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
            call_args[0] = typed;
            for (int k = 0; k < cargs->count; k++) {
              zan_ast_node_t *param = ctor->decl->method_decl.params.items[k];
              zan_type_t *pt =
                  zan_binder_resolve_type(g->binder, param->param.type);
              if (g->cur_inst && g->cur_inst->type_arg_count > 0)
                pt = subst_delegate_sig(g, pt, g->cur_inst);
              call_args[k + 1] =
                  emit_arg_typed(g, cargs->items[k], pt, tmp_locals);
            }
            zan_call_with_gctx(g, cty_fn, cfn, call_args, argc, cgctx, "");
            for (int k = 0; k < cargs->count; k++)
              emit_release_owned_call_temp(g, cargs->items[k], call_args[k + 1],
                                           tmp_locals);
            free(call_args);
          } else {
            /* No explicit 0-arg ctor on class: run implicit field initializers
             * and any nearest base 0-arg constructor. */
            struct zan_ctor_entry *bctor = NULL;
            zan_symbol_t *base_sym = (csym->type && csym->type->base_type)
                                         ? csym->type->base_type->sym
                                         : NULL;
            zan_ast_list_t bfilled;
            zan_ast_list_init(&bfilled);
            zan_ast_list_t *bargs = &empty_args;
            while (base_sym) {
              bctor = find_ctor(g, base_sym, &empty_args, NULL, NULL);
              if (!bctor &&
                  fill_ctor_default_args(g, base_sym, &empty_args, &bfilled)) {
                bctor = find_ctor(g, base_sym, &bfilled, NULL, NULL);
                if (bctor)
                  bargs = &bfilled;
              }
              if (bctor || has_declared_ctor(g, base_sym))
                break;
              base_sym = (base_sym->type && base_sym->type->base_type)
                             ? base_sym->type->base_type->sym
                             : NULL;
            }
            if (bctor && bctor->fn) {
              zan_symbol_t *cur_init = csym;
              while (cur_init && cur_init != bctor->type_sym) {
                zan_type_t *it = (cur_init == csym && g->cur_inst)
                                     ? g->cur_inst
                                     : cur_init->type;
                emit_decl_field_initializers(g, cur_init, it, typed,
                                             tmp_locals);
                cur_init = (cur_init->type && cur_init->type->base_type)
                               ? cur_init->type->base_type->sym
                               : NULL;
              }
              LLVMTypeRef bst = get_struct_llvm_type(g, bctor->type_sym);
              LLVMValueRef bthis =
                  bst ? LLVMBuildBitCast(g->builder, typed,
                                         LLVMPointerType(bst, 0), "ntp.bthis")
                      : typed;
              int bc = bargs->count + 1;
              LLVMValueRef *ba =
                  (LLVMValueRef *)calloc((size_t)bc, sizeof(LLVMValueRef));
              ba[0] = bthis;
              for (int k = 0; k < bargs->count; k++) {
                zan_ast_node_t *param =
                    bctor->decl->method_decl.params.items[k];
                zan_type_t *pt =
                    zan_binder_resolve_type(g->binder, param->param.type);
                ba[k + 1] =
                    emit_arg_typed(g, bargs->items[k], pt, tmp_locals);
              }
              coerce_args_to_params(g, bctor->fn_type, ba, bc);
              zan_call_or_invoke(g, g->builder, bctor->fn_type, bctor->fn, ba,
                                 (unsigned)bc, "");
              for (int k = 0; k < bargs->count; k++)
                emit_release_owned_call_temp(g, bargs->items[k], ba[k + 1],
                                             tmp_locals);
              free(ba);
            } else {
              emit_implicit_field_initializers(
                  g, csym, g->cur_inst ? g->cur_inst : csym->type, typed,
                  tmp_locals);
            }
          }
        }
        obj = raw;
      } else {
        obj = LLVMConstNull(i8ptr);
      }
      emit_release_owned_locals(g, tmp_locals);
      LLVMBuildRet(g->builder, obj ? obj : LLVMConstNull(i8ptr));

      g->current_type_sym = save_type;
      g->cur_inst = save_inst;
      g->cur_canon_body = save_canon;
      g->cur_gctx_val = save_gctx;
      g->cur_gctx_owner_idx = save_owner;
      g->current_this = save_this;
      g->current_locals = save_locals;
      zan_builder_restore(g, pt);
      g->di_cur_file = saved_file;
      g->di_cur_line = saved_line;
      g->di_cur_col = saved_col;
      g->di_last_loc = saved_last_loc;
      g->di_last_loc_fn = saved_last_fn;
    }
    return LLVMConstBitCast(wfn, i8ptr);
  }
  case GCTX_SLOT_STATIC_FIELD: {
    zan_symbol_t *owner_sym =
        s->sym && s->sym->parent ? s->sym->parent : concrete->sym;
    zan_type_t *owner_inst =
        cty ? cty : concrete->concrete_inst;
    if (!owner_sym || !s->sym)
      return LLVMConstNull(i8ptr);
    LLVMValueRef gv =
        get_static_field_global(g, owner_sym, s->sym, owner_inst);
    return gv ? LLVMConstBitCast(gv, i8ptr) : LLVMConstNull(i8ptr);
  }
  case GCTX_SLOT_SINIT_FN: {
    zan_symbol_t *owner_sym = s->sym ? s->sym : concrete->sym;
    zan_type_t *owner_inst = cty ? cty : concrete->concrete_inst;
    if (!owner_sym)
      return LLVMConstNull(i8ptr);
    LLVMValueRef fn = get_static_init_fn_ptr(g, owner_sym, owner_inst);
    return fn ? LLVMConstBitCast(fn, i8ptr) : LLVMConstNull(i8ptr);
  }
  case GCTX_SLOT_VTABLE: {
    zan_symbol_t *csym = s->sym ? s->sym : (cty ? cty->sym : NULL);
    if (!csym)
      return LLVMConstNull(i8ptr);
    LLVMValueRef vt =
        (cty && cty->type_arg_count > 0 && is_user_generic_sym(csym))
            ? get_vtable_global_inst(g, csym, cty)
            : get_vtable_global(g, csym);
    return vt ? LLVMConstBitCast(vt, i8ptr) : LLVMConstNull(i8ptr);
  }
  case GCTX_SLOT_ARC_DESC: {
    int site = -1;
    if (s->coll_kind > 0) {
      site = reserve_arc_site(g, NULL, NULL, s->coll_kind, cty);
    } else if (s->sym) {
      site = reserve_arc_site(g, s->sym, cty, 0, NULL);
    }
    if (site < 0)
      return LLVMConstNull(i8ptr);
    return LLVMConstIntToPtr(arc_site_arg(g, site), i8ptr);
  }
  case GCTX_SLOT_CHILD_GCTX: {
    zan_type_t **c_bind = NULL;
    if (s->bindc > 0 && s->bind) {
      c_bind = (zan_type_t **)zan_arena_alloc(
          g->binder->arena, (size_t)s->bindc * sizeof(zan_type_t *));
      for (int bi = 0; bi < s->bindc; bi++)
        c_bind[bi] = subst_canon_handles(g, s->bind[bi], concrete);
    }
    int child_idx = get_or_register_gctx(g, (s->bindc > 0), s->sym, cty,
                                         c_bind, s->bindc);
    LLVMValueRef cgv = get_gctx_global_ptr(g, child_idx);
    return cgv ? cgv : LLVMConstNull(i8ptr);
  }
  default:
    return LLVMConstNull(i8ptr);
  }
}

bool materialize_pending_gctx(zan_irgen_t *g) {
  if (!g || g->gctx_entry_count == 0)
    return false;
  bool changed = false;
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);

  for (int i = 0; i < g->gctx_entry_count; i++) {
    int owner_idx = g->gctx_entries[i].owner_idx;
    if (owner_idx < 0 || owner_idx >= g->gctx_entry_count)
      continue;
    int slot_count = g->gctx_entries[owner_idx].slot_count;
    int mat_slots = g->gctx_entries[i].materialized_slots;
    LLVMTypeRef cur_gv_ty = g->gctx_entries[i].gv_ty;
    if (slot_count == 0 && mat_slots == -1 && cur_gv_ty &&
        LLVMGetArrayLength(cur_gv_ty) > 0)
      continue;
    if (slot_count > 0 && mat_slots == slot_count && cur_gv_ty &&
        LLVMGetArrayLength(cur_gv_ty) > 0)
      continue;

    int n = slot_count > 0 ? slot_count : 1;
    LLVMValueRef *vals = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    if (slot_count == 0) {
      vals[0] = LLVMConstNull(i8ptr);
    } else {
      for (int si = 0; si < slot_count; si++) {
        zan_gctx_slot_t slot_copy = g->gctx_entries[owner_idx].slots[si];
        zan_gctx_entry_t concrete_copy = g->gctx_entries[i];
        vals[si] = materialize_gctx_slot_val(g, &concrete_copy, &slot_copy);
        if (!vals[si])
          vals[si] = LLVMConstNull(i8ptr);
      }
    }

    LLVMTypeRef arr_ty = LLVMArrayType(i8ptr, (unsigned)n);
    LLVMValueRef init = LLVMConstArray(i8ptr, vals, (unsigned)n);
    free(vals);

    zan_gctx_entry_t *e = &g->gctx_entries[i];
    if (e->gv_ty == arr_ty) {
      LLVMSetInitializer(e->gv, init);
      LLVMSetGlobalConstant(e->gv, 1);
    } else {
      char old_name[512];
      size_t nlen = 0;
      const char *onm = LLVMGetValueName2(e->gv, &nlen);
      snprintf(old_name, sizeof(old_name), "%.*s", (int)nlen, onm ? onm : "");
      LLVMValueRef old_gv = e->gv;
      LLVMSetValueName2(old_gv, "", 0);
      LLVMValueRef new_gv = LLVMAddGlobal(g->mod, arr_ty, old_name);
      LLVMSetLinkage(new_gv, LLVMInternalLinkage);
      LLVMSetGlobalConstant(new_gv, 1);
      LLVMSetInitializer(new_gv, init);
      LLVMReplaceAllUsesWith(
          old_gv, LLVMConstBitCast(new_gv, LLVMTypeOf(old_gv)));
      LLVMDeleteGlobal(old_gv);
      e->gv = new_gv;
      e->gv_ty = arr_ty;
    }
    e->materialized_slots = slot_count > 0 ? slot_count : -1;
    changed = true;
  }
  return changed;
}

/* Build or retrieve a cached wrapper thunk that drops trailing `i8* __gctx`
 * from a __Canon function signature and supplies the concrete `@__zan_gctx`. */
LLVMValueRef build_gctx_vtable_thunk(zan_irgen_t *g, LLVMValueRef target_fn,
                                     LLVMTypeRef target_fnty,
                                     zan_type_t *owner_inst,
                                     zan_type_t **bind, int bindc,
                                     LLVMTypeRef *out_thunk_ty) {
  if (!g || !target_fn || !target_fnty)
    return target_fn;
  unsigned npar = LLVMCountParamTypes(target_fnty);
  if (npar < 1)
    return target_fn;
  zan_symbol_t *sym = owner_inst ? owner_inst->sym : NULL;
  int gidx = get_or_register_gctx(g, (bindc > 0), sym, owner_inst, bind, bindc);
  LLVMValueRef gctx_ptr = get_gctx_global_ptr(g, gidx);
  if (!gctx_ptr)
    return target_fn;

  for (int i = 0; i < g->gctx_thunk_count; i++) {
    if (g->gctx_thunks[i].target_fn == target_fn &&
        g->gctx_thunks[i].gctx_gv == gctx_ptr) {
      if (out_thunk_ty)
        *out_thunk_ty = LLVMGlobalGetValueType(g->gctx_thunks[i].thunk_fn);
      return g->gctx_thunks[i].thunk_fn;
    }
  }

  LLVMTypeRef *pts = (LLVMTypeRef *)calloc(npar, sizeof(LLVMTypeRef));
  LLVMGetParamTypes(target_fnty, pts);
  LLVMTypeRef ret_ty = LLVMGetReturnType(target_fnty);
  unsigned user_n = npar - 1;
  LLVMTypeRef thunk_ty = LLVMFunctionType(ret_ty, pts, user_n, 0);

  size_t tlen = 0;
  const char *tnm = LLVMGetValueName2(target_fn, &tlen);
  char name[512];
  snprintf(name, sizeof(name), "__zan_gctx_thunk_%.*s_%d", (int)tlen,
           tnm ? tnm : "fn", g->gctx_thunk_count);
  LLVMValueRef thunk = LLVMAddFunction(g->mod, name, thunk_ty);
  zan_set_module_local(thunk);

  zan_builder_point_t pt = zan_builder_save(g);
  uint32_t saved_file = g->di_cur_file;
  uint32_t saved_line = g->di_cur_line;
  uint32_t saved_col = g->di_cur_col;
  LLVMMetadataRef saved_last_loc = g->di_last_loc;
  LLVMValueRef saved_last_fn = g->di_last_loc_fn;
  g->current_fn = thunk;
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, thunk, "entry");
  LLVMPositionBuilderAtEnd(g->builder, entry);
  di_clear(g);
  LLVMValueRef *args = (LLVMValueRef *)calloc(npar, sizeof(LLVMValueRef));
  for (unsigned i = 0; i < user_n; i++)
    args[i] = LLVMGetParam(thunk, i);
  args[user_n] = (LLVMTypeOf(gctx_ptr) != pts[user_n])
                     ? LLVMConstBitCast(gctx_ptr, pts[user_n])
                     : gctx_ptr;
  LLVMValueRef res = LLVMBuildCall2(g->builder, target_fnty, target_fn, args,
                                    npar,
                                    LLVMGetTypeKind(ret_ty) == LLVMVoidTypeKind
                                        ? ""
                                        : "thk.r");
  free(args);
  free(pts);
  if (LLVMGetTypeKind(ret_ty) == LLVMVoidTypeKind)
    LLVMBuildRetVoid(g->builder);
  else
    LLVMBuildRet(g->builder, res);

  zan_builder_restore(g, pt);
  g->di_cur_file = saved_file;
  g->di_cur_line = saved_line;
  g->di_cur_col = saved_col;
  g->di_last_loc = saved_last_loc;
  g->di_last_loc_fn = saved_last_fn;

  if (g->gctx_thunk_count >= g->gctx_thunk_cap) {
    int ncap = g->gctx_thunk_cap ? g->gctx_thunk_cap * 2 : 32;
    g->gctx_thunks = (struct zan_gctx_thunk *)realloc(
        g->gctx_thunks, (size_t)ncap * sizeof(*g->gctx_thunks));
    g->gctx_thunk_cap = ncap;
  }
  g->gctx_thunks[g->gctx_thunk_count].target_fn = target_fn;
  g->gctx_thunks[g->gctx_thunk_count].gctx_gv = gctx_ptr;
  g->gctx_thunks[g->gctx_thunk_count].thunk_fn = thunk;
  g->gctx_thunk_count++;

  if (out_thunk_ty)
    *out_thunk_ty = thunk_ty;
  return thunk;
}

LLVMTypeRef strip_last_param_fn_type(zan_irgen_t *g, LLVMTypeRef fn_type) {
  (void)g;
  if (!fn_type)
    return fn_type;
  unsigned npar = LLVMCountParamTypes(fn_type);
  if (npar < 1)
    return fn_type;
  LLVMTypeRef *pts = (LLVMTypeRef *)calloc(npar, sizeof(LLVMTypeRef));
  LLVMGetParamTypes(fn_type, pts);
  LLVMTypeRef res =
      LLVMFunctionType(LLVMGetReturnType(fn_type), pts, npar - 1, 0);
  free(pts);
  return res;
}

static bool bind_mentions_canon(zan_type_t **bind, int bindc) {
  for (int i = 0; i < bindc; i++) {
    if (bind[i] && type_mentions_canon(bind[i]))
      return true;
  }
  return false;
}

LLVMValueRef resolve_gctx_arg_for_inst(zan_irgen_t *g, bool is_mspec,
                                       zan_symbol_t *sym, zan_type_t *inst,
                                       zan_type_t **bind, int bindc) {
  if (!g)
    return NULL;
  bool uses_canon =
      (inst && inst_uses_canon(g, inst)) ||
      (bind && bindc > 0 && args_use_canon(g, is_mspec ? sym : NULL, bind, bindc));
  if (!uses_canon)
    return NULL;
  if (g->cur_canon_body && g->cur_gctx_val) {
    if (!is_mspec && g->cur_inst && inst && inst->sym == g->cur_inst->sym &&
        types_equal(inst, g->cur_inst) && !g->cur_mbind) {
      return g->cur_gctx_val;
    }
    if ((inst && type_mentions_canon(inst)) ||
        (bind && bindc > 0 && bind_mentions_canon(bind, bindc))) {
      return emit_gctx_load_slot(g, GCTX_SLOT_CHILD_GCTX, sym, inst, bind,
                                 bindc, 0, 0);
    }
  }
  if ((inst && (!type_is_concrete(inst) || type_mentions_canon(inst)))) {
    return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
  }
  for (int i = 0; i < bindc; i++) {
    if (!bind[i] || !type_is_concrete(bind[i]) || type_mentions_canon(bind[i])) {
      return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
    }
  }
  zan_symbol_t *use_sym =
      (is_mspec && sym && sym->parent) ? sym->parent : sym;
  if (use_sym && inst && inst->sym == use_sym && type_is_concrete(inst))
    irgen_record_static_type_use(g, use_sym, inst);
  int idx = get_or_register_gctx(g, is_mspec, sym, inst, bind, bindc);
  return get_gctx_global_ptr(g, idx);
}

LLVMValueRef zan_call_with_gctx(zan_irgen_t *g, LLVMTypeRef fn_type,
                                LLVMValueRef fn, LLVMValueRef *args, int argc,
                                LLVMValueRef gctx_arg, const char *cn) {
  unsigned npar = fn_type ? LLVMCountParamTypes(fn_type) : (unsigned)argc;
  if (gctx_arg && npar == (unsigned)argc + 1) {
    LLVMValueRef *ext =
        (LLVMValueRef *)calloc((size_t)(argc + 1), sizeof(LLVMValueRef));
    if (argc > 0 && args)
      memcpy(ext, args, (size_t)argc * sizeof(LLVMValueRef));
    ext[argc] = gctx_arg;
    coerce_args_to_params(g, fn_type, ext, argc + 1);
    LLVMValueRef res = zan_call_or_invoke(g, g->builder, fn_type, fn, ext,
                                          (unsigned)(argc + 1), cn);
    free(ext);
    return res;
  }
  coerce_args_to_params(g, fn_type, args, argc);
  return zan_call_or_invoke(g, g->builder, fn_type, fn, args, (unsigned)argc,
                            cn);
}
