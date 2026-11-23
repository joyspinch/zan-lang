/* Static initialization is executable work, not a root of the loaded type set.
 * Keep demand separate from body emission: non-publish builds still emit dead
 * bodies for diagnostics, and reflection can retain metadata without an object.
 */

static bool sinit_same_inst(zan_type_t *a, zan_type_t *b) {
  return (!a && !b) || (a && b && types_equal(a, b));
}

void irgen_record_static_type_use(zan_irgen_t *g, zan_symbol_t *sym,
                                  zan_type_t *inst) {
  if (!sym || !sym->decl || (sym->kind != SYM_CLASS && sym->kind != SYM_STRUCT))
    return;
  if (inst && inst->sym != sym)
    inst = NULL;
  if (is_user_generic_sym(sym) && (!inst || !type_is_concrete(inst)))
    return;
  if (!is_user_generic_sym(sym))
    inst = NULL;
  LLVMBasicBlockRef bb = LLVMGetInsertBlock(g->builder);
  LLVMValueRef caller = bb ? LLVMGetBasicBlockParent(bb) : NULL;
  if (!caller)
    return;
  for (int i = 0; i < g->static_init_use_count; i++) {
    struct zan_static_init_use *use = &g->static_init_uses[i];
    if (use->sym == sym && use->caller == caller &&
        sinit_same_inst(use->inst, inst))
      return;
  }
  if (!ZAN_TAB_ENSURE(g->static_init_uses, g->static_init_use_count,
                      g->static_init_use_cap, 32))
    return;
  g->static_init_uses[g->static_init_use_count++] =
      (struct zan_static_init_use){sym, inst, caller};
}

LLVMValueRef get_static_init_coordinator(zan_irgen_t *g) {
  if (!g->static_init_coordinator) {
    LLVMTypeRef ty =
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    g->static_init_coordinator =
        LLVMAddFunction(g->mod, "__zan_static_init", ty);
    zan_set_module_local(g->static_init_coordinator);
  }
  return g->static_init_coordinator;
}

static bool sinit_is_cctor(zan_ast_node_t *member) {
  return member->kind == AST_CONSTRUCTOR_DECL &&
         (member->method_decl.modifiers & MOD_STATIC) != 0;
}

static bool is_module_initializer_member(zan_symbol_t *owner,
                                         zan_ast_node_t *member) {
  if (!member || member->kind != AST_METHOD_DECL)
    return false;
  uint32_t mods = member->method_decl.modifiers;
  if (!(mods & MOD_STATIC) ||
      (mods & (MOD_ABSTRACT | MOD_EXTERN | MOD_ASYNC)) != 0 ||
      zan_ast_method_extern_lib(member).str != NULL ||
      member->method_decl.params.count != 0 ||
      member->method_decl.type_params.count > 0 ||
      (owner && owner->decl && owner->decl->type_decl.type_params.count > 0))
    return false;
  return zan_ast_has_module_initializer(member);
}

/* The executable graph is rebuilt once per discovery round. One closure serves
 * all type/method queries; final ordering computes one closure per initializer.
 * Metadata globals stop edges. Actual virtual/property/reflection execution
 * shares the same graph for demand discovery and initialization ordering. */
typedef struct {
  LLVMValueRef fn;
  int first;
} sinit_fn_t;
typedef struct {
  int to, next;
} sinit_edge_t;
typedef struct {
  zan_irgen_t *g;
  const work_fn_index_t *work;
  sinit_fn_t *fns;
  int count;
  int *slots;
  int slot_cap;
  sinit_edge_t *edges;
  int edge_count, edge_cap;
  unsigned char *reachable;
  int *queue;
} sinit_exec_graph_t;

static int sinit_fn_index(const sinit_exec_graph_t *gr, LLVMValueRef fn) {
  if (!fn || !gr->slot_cap)
    return -1;
  size_t slot = (size_t)work_fn_hash(fn) & (size_t)(gr->slot_cap - 1);
  while (gr->slots[slot]) {
    int i = gr->slots[slot] - 1;
    if (gr->fns[i].fn == fn)
      return i;
    slot = (slot + 1) & (size_t)(gr->slot_cap - 1);
  }
  return -1;
}

static void sinit_add_edge(sinit_exec_graph_t *gr, int from, int to) {
  if (from < 0 || to < 0 || from == to)
    return;
  if (!ZAN_TAB_ENSURE(gr->edges, gr->edge_count, gr->edge_cap, 128))
    return;
  gr->edges[gr->edge_count] = (sinit_edge_t){to, gr->fns[from].first};
  gr->fns[from].first = gr->edge_count++;
}

typedef struct {
  LLVMValueRef *slots;
  int count, cap;
} sinit_seen_t;
static bool sinit_seen_add(sinit_seen_t *seen, LLVMValueRef value) {
  if (!seen->cap || (seen->count + 1) * 2 >= seen->cap) {
    int cap = seen->cap ? seen->cap * 2 : 32;
    LLVMValueRef *slots = (LLVMValueRef *)calloc((size_t)cap, sizeof(*slots));
    if (!slots)
      return false;
    for (int i = 0; i < seen->cap; i++) {
      LLVMValueRef old = seen->slots[i];
      if (!old)
        continue;
      size_t j = (size_t)work_fn_hash(old) & (size_t)(cap - 1);
      while (slots[j])
        j = (j + 1) & (size_t)(cap - 1);
      slots[j] = old;
    }
    free(seen->slots);
    seen->slots = slots;
    seen->cap = cap;
  }
  size_t j = (size_t)work_fn_hash(value) & (size_t)(seen->cap - 1);
  while (seen->slots[j]) {
    if (seen->slots[j] == value)
      return false;
    j = (j + 1) & (size_t)(seen->cap - 1);
  }
  seen->slots[j] = value;
  seen->count++;
  return true;
}

static void sinit_collect_callers(sinit_exec_graph_t *gr, int target,
                                  LLVMValueRef value, sinit_seen_t *seen) {
  if (!sinit_seen_add(seen, value))
    return;
  for (LLVMUseRef use = LLVMGetFirstUse(value); use;
       use = LLVMGetNextUse(use)) {
    LLVMValueRef user = LLVMGetUser(use);
    if (LLVMIsAInstruction(user)) {
      if (LLVMIsAICmpInst(user) && interface_candidate_metadata(gr->g, user))
        continue;
      if ((LLVMIsACallInst(user) || LLVMIsAInvokeInst(user)) &&
          !interface_candidate_can_exist(gr->g, user))
        continue;
      LLVMValueRef parent =
          LLVMGetBasicBlockParent(LLVMGetInstructionParent(user));
      if (parent != gr->g->static_init_cleanup)
        sinit_add_edge(gr, sinit_fn_index(gr, parent), target);
    } else if (LLVMIsAGlobalVariable(user)) {
      const char *name = LLVMGetValueName(user);
      if (name && (strncmp(name, "__zan_vtable_", 13) == 0 ||
                   strncmp(name, "__zan_gctx_", 11) == 0))
        sinit_collect_callers(gr, target, user, seen);
    } else if (LLVMIsAConstantExpr(user) || LLVMIsAConstantArray(user) ||
               LLVMIsAConstantStruct(user)) {
      sinit_collect_callers(gr, target, user, seen);
    }
  }
}

static int sinit_literal_name_at(zan_irgen_t *g, LLVMValueRef value,
                                 zan_istr_t name, uint64_t offset, int depth) {
  if (!value || depth > 8)
    return -1;
  LLVMTargetDataRef layout = LLVMGetModuleDataLayout(g->mod);
  if (LLVMIsAConstantExpr(value)) {
    LLVMOpcode op = LLVMGetConstOpcode(value);
    if (op == LLVMGetElementPtr) {
      LLVMTypeRef type = LLVMGetGEPSourceElementType(value);
      int operands = LLVMGetNumOperands(value);
      for (int i = 1; i < operands; i++) {
        LLVMValueRef index = LLVMGetOperand(value, (unsigned)i);
        if (!LLVMIsAConstantInt(index))
          return -1;
        int64_t n = LLVMConstIntGetSExtValue(index);
        if (n < 0)
          return -1;
        uint64_t delta;
        if (i > 1 && LLVMGetTypeKind(type) == LLVMStructTypeKind) {
          if ((uint64_t)n >= LLVMCountStructElementTypes(type))
            return -1;
          delta = LLVMOffsetOfElement(layout, type, (unsigned)n);
          type = LLVMStructGetTypeAtIndex(type, (unsigned)n);
        } else {
          if (i > 1) {
            LLVMTypeKind kind = LLVMGetTypeKind(type);
            if (kind != LLVMArrayTypeKind && kind != LLVMVectorTypeKind)
              return -1;
            type = LLVMGetElementType(type);
          }
          uint64_t stride = LLVMABISizeOfType(layout, type);
          if (stride && (uint64_t)n > UINT64_MAX / stride)
            return -1;
          delta = (uint64_t)n * stride;
        }
        if (offset > UINT64_MAX - delta)
          return -1;
        offset += delta;
      }
    } else if (op != LLVMBitCast && op != LLVMAddrSpaceCast) {
      return -1;
    }
    return sinit_literal_name_at(g, LLVMGetOperand(value, 0), name, offset,
                                 depth + 1);
  }
  if (LLVMIsAGlobalVariable(value))
    return sinit_literal_name_at(g, LLVMGetInitializer(value), name, offset,
                                 depth + 1);
  if (LLVMIsAConstantStruct(value)) {
    LLVMTypeRef type = LLVMTypeOf(value);
    unsigned count = LLVMCountStructElementTypes(type);
    for (unsigned i = 0; i < count; i++) {
      uint64_t start = LLVMOffsetOfElement(layout, type, i);
      uint64_t size =
          LLVMABISizeOfType(layout, LLVMStructGetTypeAtIndex(type, i));
      if (offset >= start && offset - start < size)
        return sinit_literal_name_at(g, LLVMGetAggregateElement(value, i), name,
                                     offset - start, depth + 1);
    }
    return -1;
  }
  if (LLVMIsAConstantDataArray(value) && LLVMIsConstantString(value)) {
    size_t size = 0;
    const char *bytes = LLVMGetAsString(value, &size);
    if (offset >= size)
      return -1;
    const char *text = bytes + (size_t)offset;
    const char *end = (const char *)memchr(text, 0, size - (size_t)offset);
    if (!end)
      return -1;
    size_t len = (size_t)(end - text);
    return len == name.len && memcmp(text, name.str, len) == 0;
  }
  return -1;
}

static int sinit_literal_name_matches(zan_irgen_t *g, LLVMValueRef value,
                                      zan_istr_t name) {
  /* Preserve the data pointer's GEP offset: managed string literals include
   * a 16-byte header before the actual name, and may include padding after it.
   */
  return sinit_literal_name_at(g, value, name, 0, 0);
}

static bool sinit_reflected_owner(zan_irgen_t *g, zan_symbol_t *sym,
                                  bool ctor) {
  for (int i = 0; i < g->refl_mtab_count; i++)
    if (g->refl_mtabs[i].sym == sym && g->refl_mtabs[i].ctors == ctor)
      return true;
  return false;
}

static void sinit_add_reflection_edges(sinit_exec_graph_t *gr) {
  zan_irgen_t *g = gr->g;
  const method_body_work_t *work = gr->work->work;
  for (int w = 0; w < gr->work->work_count; w++) {
    zan_ast_node_t *m = work[w].member;
    if (m->kind != AST_METHOD_DECL)
      continue;
    zan_istr_t name = m->method_decl.name;
    int target = sinit_fn_index(gr, work[w].fn);
    if (!refl_method_needs_thunk(g, work[w].type_sym, m))
      continue;
    if (name.len > 4 && memcmp(name.str, "get_", 4) == 0) {
      sinit_add_edge(gr, sinit_fn_index(gr, g->fn_refl_pget), target);
      sinit_add_edge(gr, sinit_fn_index(gr, g->fn_refl_spget), target);
    }
    if (name.len > 4 && memcmp(name.str, "set_", 4) == 0) {
      sinit_add_edge(gr, sinit_fn_index(gr, g->fn_refl_set), target);
      sinit_add_edge(gr, sinit_fn_index(gr, g->fn_refl_set_at), target);
      sinit_add_edge(gr, sinit_fn_index(gr, g->fn_refl_sset), target);
    }
  }
  LLVMValueRef invoke = g->fn_refl_invoke;
  if (!invoke)
    return;
  for (LLVMUseRef use = LLVMGetFirstUse(invoke); use;
       use = LLVMGetNextUse(use)) {
    LLVMValueRef call = LLVMGetUser(use);
    if ((!LLVMIsACallInst(call) && !LLVMIsAInvokeInst(call)) ||
        LLVMGetCalledValue(call) != invoke || LLVMGetNumArgOperands(call) < 3)
      continue;
    LLVMValueRef table = LLVMGetOperand(call, 1);
    if (!LLVMIsAConstantInt(table))
      continue;
    LLVMValueRef parent =
        LLVMGetBasicBlockParent(LLVMGetInstructionParent(call));
    int from = sinit_fn_index(gr, parent);
    if (LLVMConstIntGetZExtValue(table) == 1) {
      for (int i = 0; i < gr->count; i++) {
        const char *name = LLVMGetValueName(gr->fns[i].fn);
        if (name && strncmp(name, "__zan.refl.thunk.new.",
                            sizeof("__zan.refl.thunk.new.") - 1) == 0)
          sinit_add_edge(gr, from, i);
      }
      for (int w = 0; w < gr->work->work_count; w++) {
        if (work[w].member->kind == AST_CONSTRUCTOR_DECL &&
            !sinit_is_cctor(work[w].member) &&
            sinit_reflected_owner(g, work[w].type_sym, true))
          sinit_add_edge(gr, from, sinit_fn_index(gr, work[w].fn));
      }
      continue;
    }
    if (parent == g->fn_refl_pget || parent == g->fn_refl_spget ||
        parent == g->fn_refl_set || parent == g->fn_refl_set_at ||
        parent == g->fn_refl_sset)
      continue;
    LLVMValueRef index = LLVMGetOperand(call, 2);
    LLVMValueRef literal = NULL;
    if ((LLVMIsACallInst(index) || LLVMIsAInvokeInst(index)) &&
        LLVMGetCalledValue(index) == g->fn_refl_mfind &&
        LLVMGetNumArgOperands(index) >= 2)
      literal = LLVMGetOperand(index, 1);
    for (int w = 0; w < gr->work->work_count; w++) {
      zan_ast_node_t *m = work[w].member;
      if (m->kind != AST_METHOD_DECL ||
          !refl_method_needs_thunk(g, work[w].type_sym, m) ||
          sinit_literal_name_matches(g, literal, m->method_decl.name) == 0)
        continue;
      sinit_add_edge(gr, from, sinit_fn_index(gr, work[w].fn));
    }
  }
}

static void sinit_mark_from(sinit_exec_graph_t *gr, LLVMValueRef fn, int *end) {
  int i = sinit_fn_index(gr, fn);
  if (i < 0 || gr->reachable[i])
    return;
  gr->reachable[i] = 1;
  gr->queue[(*end)++] = i;
}

static void sinit_compute_closure(sinit_exec_graph_t *gr, LLVMValueRef root) {
  memset(gr->reachable, 0, (size_t)gr->count);
  int end = 0;
  if (root) {
    sinit_mark_from(gr, root, &end);
  } else {
    sinit_mark_from(gr, LLVMGetNamedFunction(gr->g->mod, "main"), &end);
    for (int t = 0; t < gr->g->static_init_type_count; t++)
      sinit_mark_from(gr, gr->g->static_init_types[t].fn, &end);
    for (int w = 0; w < gr->work->work_count; w++) {
      const method_body_work_t *work = &gr->work->work[w];
      zan_ast_node_t *m = work->member;
      if (sinit_is_cctor(m))
        continue;
      if ((gr->g->emit_lib && ((m->method_decl.modifiers & MOD_PUBLIC) ||
                               m->kind == AST_CONSTRUCTOR_DECL)) ||
          is_module_initializer_member(work->type_sym, m))
        sinit_mark_from(gr, work->fn, &end);
    }
  }
  for (int start = 0; start < end; start++) {
    int from = gr->queue[start];
    for (int e = gr->fns[from].first; e >= 0; e = gr->edges[e].next) {
      int to = gr->edges[e].to;
      if (!gr->reachable[to]) {
        gr->reachable[to] = 1;
        gr->queue[end++] = to;
      }
    }
  }
}

static void sinit_graph_build(sinit_exec_graph_t *gr, zan_irgen_t *g,
                              const work_fn_index_t *work) {
  memset(gr, 0, sizeof(*gr));
  gr->g = g;
  gr->work = work;
  for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
       fn = LLVMGetNextFunction(fn))
    gr->count++;
  gr->slot_cap = 16;
  while (gr->slot_cap < gr->count * 2)
    gr->slot_cap *= 2;
  gr->slots = (int *)calloc((size_t)gr->slot_cap, sizeof(*gr->slots));
  gr->fns = (sinit_fn_t *)calloc((size_t)gr->count + 1, sizeof(*gr->fns));
  gr->reachable = (unsigned char *)calloc((size_t)gr->count + 1, 1);
  gr->queue = (int *)malloc(((size_t)gr->count + 1) * sizeof(*gr->queue));
  int i = 0;
  for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
       fn = LLVMGetNextFunction(fn), i++) {
    gr->fns[i] = (sinit_fn_t){fn, -1};
    size_t slot = (size_t)work_fn_hash(fn) & (size_t)(gr->slot_cap - 1);
    while (gr->slots[slot])
      slot = (slot + 1) & (size_t)(gr->slot_cap - 1);
    gr->slots[slot] = i + 1;
  }
  sinit_seen_t seen = {0};
  for (i = 0; i < gr->count; i++) {
    if (seen.slots)
      memset(seen.slots, 0, (size_t)seen.cap * sizeof(*seen.slots));
    seen.count = 0;
    sinit_collect_callers(gr, i, gr->fns[i].fn, &seen);
  }
  free(seen.slots);
  sinit_add_reflection_edges(gr);
  sinit_compute_closure(gr, NULL);
}

static void sinit_graph_dispose(sinit_exec_graph_t *gr) {
  free(gr->fns);
  free(gr->slots);
  free(gr->edges);
  free(gr->reachable);
  free(gr->queue);
}

static bool static_init_body_has_live_use(const sinit_exec_graph_t *gr,
                                          LLVMValueRef fn) {
  int i = sinit_fn_index(gr, fn);
  return i >= 0 && gr->reachable[i];
}

static bool sinit_value_has_live_use(const sinit_exec_graph_t *gr,
                                     LLVMValueRef val, int depth) {
  if (!val || depth > 8)
    return false;
  for (LLVMUseRef use = LLVMGetFirstUse(val); use; use = LLVMGetNextUse(use)) {
    LLVMValueRef user = LLVMGetUser(use);
    if (LLVMIsAInstruction(user)) {
      if (LLVMIsAICmpInst(user) && interface_candidate_metadata(gr->g, user))
        continue;
      if ((LLVMIsACallInst(user) || LLVMIsAInvokeInst(user)) &&
          !interface_candidate_can_exist(gr->g, user))
        continue;
      LLVMValueRef parent =
          LLVMGetBasicBlockParent(LLVMGetInstructionParent(user));
      if (parent != gr->g->static_init_cleanup &&
          static_init_body_has_live_use(gr, parent))
        return true;
    } else if (LLVMIsAGlobalVariable(user)) {
      const char *name = LLVMGetValueName(user);
      if (name &&
          (strncmp(name, "__zan_vtable_", 13) == 0 ||
           strncmp(name, "__zan_gctx_", 11) == 0) &&
          sinit_value_has_live_use(gr, user, depth + 1))
        return true;
    } else if ((LLVMIsAConstantExpr(user) || LLVMIsAConstantArray(user) ||
                LLVMIsAConstantStruct(user)) &&
               sinit_value_has_live_use(gr, user, depth + 1)) {
      return true;
    }
  }
  return false;
}

static bool sinit_type_has_init_work(zan_symbol_t *sym) {
  if (!sym || !sym->decl)
    return false;
  zan_ast_node_t *decl = sym->decl;
  for (int mi = 0; mi < decl->type_decl.members.count; mi++) {
    zan_ast_node_t *m = decl->type_decl.members.items[mi];
    if (sinit_is_cctor(m))
      return true;
    if ((m->kind != AST_FIELD_DECL && m->kind != AST_PROPERTY_DECL) ||
        !(m->field_decl.modifiers & MOD_STATIC) || !m->field_decl.initializer)
      continue;
    if (m->kind == AST_PROPERTY_DECL &&
        (m->field_decl.getter_body || m->field_decl.setter_body))
      continue;
    return true;
  }
  return false;
}

static int sinit_activate(zan_irgen_t *g, zan_symbol_t *sym, zan_type_t *inst) {
  if (!sym || !sym->decl || (sym->kind != SYM_CLASS && sym->kind != SYM_STRUCT))
    return 0;
  if (inst && inst->sym != sym)
    inst = NULL;
  if (is_user_generic_sym(sym) &&
      (!inst || !type_is_concrete(inst) || type_mentions_canon(inst)))
    return 0;
  if (!is_user_generic_sym(sym))
    inst = NULL;
  for (int t = 0; t < g->static_init_type_count; t++) {
    if (g->static_init_types[t].sym == sym &&
        sinit_same_inst(g->static_init_types[t].inst, inst))
      return 0;
  }
  if (!ZAN_TAB_ENSURE(g->static_init_types, g->static_init_type_count,
                      g->static_init_type_cap, 32))
    return 0;
  LLVMValueRef fn = NULL;
  if (sinit_type_has_init_work(sym)) {
    char suffix[256] = "", name[768];
    if (inst)
      mangle_inst_suffix(suffix, sizeof(suffix), inst);
    snprintf(name, sizeof(name), "__zan_sinit_%.*s%s", (int)sym->name.len,
             sym->name.str, suffix);
    LLVMTypeRef ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    fn = LLVMAddFunction(g->mod, name, ty);
    zan_set_module_local(fn);
  }
  g->static_init_types[g->static_init_type_count++] =
      (struct zan_static_init_type){
          .sym = sym, .inst = inst, .fn = fn, .emitted = (fn == NULL)};
  return fn ? 1 : 0;
}

LLVMValueRef get_static_init_fn_ptr(zan_irgen_t *g, zan_symbol_t *sym,
                                    zan_type_t *inst) {
  if (!g || !sym)
    return NULL;
  if (!is_user_generic_sym(sym))
    inst = NULL;
  sinit_activate(g, sym, inst);
  for (int t = 0; t < g->static_init_type_count; t++) {
    if (g->static_init_types[t].sym == sym &&
        sinit_same_inst(g->static_init_types[t].inst, inst))
      return g->static_init_types[t].fn;
  }
  return NULL;
}

/* Eager diagnostics must not mark every declared type as executable demand. */
static void emit_demanded_method_bodies(zan_irgen_t *g,
                                        method_body_work_t *work, int count,
                                        unsigned char *live) {
  for (int w = 0; w < count; w++) {
    if (live[w] == 1 && !LLVMIsDeclaration(work[w].fn))
      live[w] = 2;
  }
  emit_user_method_bodies(g, work, count, live);
}

static void emit_static_type_initializer(zan_irgen_t *g, int index) {
  struct zan_static_init_type entry = g->static_init_types[index];
  g->static_init_types[index].emitted = true;
  zan_ast_node_t *decl = entry.sym->decl;
  LLVMValueRef saved_fn = g->current_fn;
  LLVMTypeRef saved_ret = g->current_fn_ret_type;
  zan_type_t *saved_zan_ret = g->current_fn_zan_ret_type;
  zan_symbol_t *saved_type = g->current_type_sym;
  zan_type_t *saved_inst = g->cur_inst;
  bool saved_canon = g->cur_canon_body;
  LLVMValueRef saved_gctx = g->cur_gctx_val;
  int saved_owner_idx = g->cur_gctx_owner_idx;
  LLVMValueRef saved_this = g->current_this;
  zan_ast_node_t *saved_body = g->current_fn_body;
  bool saved_ctor = g->current_fn_is_ctor;
  bool saved_main = g->current_fn_is_main;
  void *saved_locals = g->current_locals;
  g->current_fn = entry.fn;
  g->current_fn_ret_type = LLVMVoidTypeInContext(g->ctx);
  g->current_fn_zan_ret_type = g->binder->type_void;
  g->current_type_sym = entry.sym;
  g->cur_inst = entry.inst;
  g->cur_canon_body = false;
  g->cur_gctx_val = NULL;
  g->cur_gctx_owner_idx = -1;
  g->current_this = NULL;
  g->current_fn_body = NULL;
  g->current_fn_is_ctor = false;
  g->current_fn_is_main = false;
  LLVMPositionBuilderAtEnd(
      g->builder, LLVMAppendBasicBlockInContext(g->ctx, entry.fn, "entry"));
  di_clear(g);
  local_scope_t *locals = local_scope_new(g->arena);
  zan_ast_node_t *cctor_decl = NULL;
  for (int mi = 0; mi < decl->type_decl.members.count; mi++) {
    zan_ast_node_t *m = decl->type_decl.members.items[mi];
    if (sinit_is_cctor(m)) {
      cctor_decl = m;
      continue;
    }
    if ((m->kind != AST_FIELD_DECL && m->kind != AST_PROPERTY_DECL) ||
        !(m->field_decl.modifiers & MOD_STATIC) || !m->field_decl.initializer)
      continue;
    if (m->kind == AST_PROPERTY_DECL &&
        (m->field_decl.getter_body || m->field_decl.setter_body))
      continue;
    zan_symbol_t *field = get_field_sym(entry.sym, m->field_decl.name);
    LLVMValueRef gv = get_static_field_global(g, entry.sym, field, entry.inst);
    if (!gv)
      continue;
    zan_type_t *field_type = field_store_type(g, field, entry.inst);
    zan_type_t *source = infer_expr_type(g, m->field_decl.initializer, locals);
    check_implicit_narrowing(g, field_type, source, m->field_decl.initializer,
                             "field initializer");
    check_value_type_mismatch(g, field_type, source, m->field_decl.initializer,
                              "field initializer");
    LLVMValueRef value =
        field_type && field_type->kind == TYPE_DELEGATE
            ? emit_arg_typed(g, m->field_decl.initializer, field_type, locals)
            : emit_expr(g, m->field_decl.initializer, locals);
    if (field_type &&
        (is_rc_managed_type(field_type) || field_type->kind == TYPE_OBJECT)) {
      emit_rc_store_field(g, field_type, gv, value, m->field_decl.initializer,
                          locals, (field->modifiers & MOD_WEAK) ? 1 : 0);
    } else {
      LLVMTypeRef ty = LLVMGlobalGetValueType(gv);
      value = coerce_struct_value_arg(g, value, ty);
      LLVMBuildStore(g->builder, coerce_int_to(g, value, ty), gv);
    }
  }
  if (cctor_decl) {
    char suffix[256] = "", name[768];
    if (entry.inst)
      mangle_inst_suffix(suffix, sizeof(suffix), entry.inst);
    snprintf(name, sizeof(name), "%.*s_cctor%s", (int)entry.sym->name.len,
             entry.sym->name.str, suffix);
    LLVMValueRef cctor = LLVMGetNamedFunction(g->mod, name);
    if (entry.inst && !cctor) {
      LLVMTypeRef ty =
          LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
      cctor = LLVMAddFunction(g->mod, name, ty);
      zan_set_module_local(cctor);
    }
    g->static_init_types[index].cctor_fn = cctor;
    if (cctor) {
      zan_call2(g->builder, LLVMGlobalGetValueType(cctor), cctor, NULL, 0, "");
    }
  }
  emit_release_owned_locals(g, locals);
  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
    LLVMBuildRetVoid(g->builder);
  if (entry.inst && cctor_decl && g->static_init_types[index].cctor_fn &&
      LLVMIsDeclaration(g->static_init_types[index].cctor_fn)) {
    /* Static storage belongs to a closed type, including accesses in cctors.
     * The declaration queue intentionally contains only the erased cctor. */
    method_body_work_t cwork = {
        .member = cctor_decl,
        .type_sym = entry.sym,
        .fn = g->static_init_types[index].cctor_fn,
        .param_types = (LLVMTypeRef *)calloc(1, sizeof(LLVMTypeRef)),
        .is_static = true,
        .llvm_ret = LLVMVoidTypeInContext(g->ctx),
        .ret_type = g->binder->type_void,
        .cur_inst = entry.inst};
    unsigned char clive = 1;
    emit_user_method_bodies(g, &cwork, 1, &clive);
  }
  g->current_fn = saved_fn;
  g->current_fn_ret_type = saved_ret;
  g->current_fn_zan_ret_type = saved_zan_ret;
  g->current_type_sym = saved_type;
  g->cur_inst = saved_inst;
  g->cur_canon_body = saved_canon;
  g->cur_gctx_val = saved_gctx;
  g->cur_gctx_owner_idx = saved_owner_idx;
  g->current_this = saved_this;
  g->current_fn_body = saved_body;
  g->current_fn_is_ctor = saved_ctor;
  g->current_fn_is_main = saved_main;
  g->current_locals = saved_locals;
  LLVMClearInsertionPosition(g->builder);
  zan_irgen_compact_completed(g, entry.fn);
}

/* Called inside the same fixed point as callable/reflection/vtable demand. A
 * newly emitted initializer can discover methods, objects, closed generic types
 * and reflection capabilities; none is allowed to arrive after dead stubs. */
static int emit_pending_static_initializers(zan_irgen_t *g,
                                            zan_ast_node_t *unit,
                                            method_body_work_t *work, int count,
                                            const unsigned char *live,
                                            const sinit_exec_graph_t *graph) {
  (void)unit;
  int pending = 0;
  if (!g->static_init_ctor_demand && g->fn_refl_invoke) {
    for (LLVMUseRef use = LLVMGetFirstUse(g->fn_refl_invoke); use;
         use = LLVMGetNextUse(use)) {
      LLVMValueRef call = LLVMGetUser(use);
      if ((!LLVMIsACallInst(call) && !LLVMIsAInvokeInst(call)) ||
          LLVMGetCalledValue(call) != g->fn_refl_invoke ||
          LLVMGetNumArgOperands(call) < 2)
        continue;
      LLVMValueRef table = LLVMGetOperand(call, 1);
      if (!LLVMIsAConstantInt(table) ||
          LLVMConstIntGetZExtValue(table) !=
              1 /* reflection constructor table */)
        continue;
      LLVMValueRef parent =
          LLVMGetBasicBlockParent(LLVMGetInstructionParent(call));
      if (static_init_body_has_live_use(graph, parent)) {
        g->static_init_ctor_demand = true;
        pending++;
        break;
      }
    }
  }
  if (!g->static_init_sfield_demand && g->refl_sfield_used) {
    LLVMValueRef sfns[] = {g->fn_refl_sget_i64, g->fn_refl_sget_f64,
                           g->fn_refl_sget_str, g->fn_refl_sget_obj,
                           g->fn_refl_sset};
    for (int fi = 0; fi < 5 && !g->static_init_sfield_demand; fi++) {
      LLVMValueRef sfn = sfns[fi];
      if (!sfn)
        continue;
      for (LLVMUseRef use = LLVMGetFirstUse(sfn); use;
           use = LLVMGetNextUse(use)) {
        LLVMValueRef call = LLVMGetUser(use);
        if ((!LLVMIsACallInst(call) && !LLVMIsAInvokeInst(call)) ||
            LLVMGetCalledValue(call) != sfn)
          continue;
        LLVMValueRef parent =
            LLVMGetBasicBlockParent(LLVMGetInstructionParent(call));
        if (static_init_body_has_live_use(graph, parent)) {
          g->static_init_sfield_demand = true;
          pending++;
          break;
        }
      }
    }
  }
  if (g->static_init_sfield_demand) {
    for (int i = 0; i < g->refl_meta_count; i++) {
      if (g->refl_metas[i].sym)
        pending += sinit_activate(g, g->refl_metas[i].sym, g->refl_metas[i].type);
    }
  }
  for (int w = 0; w < count; w++) {
    if (live[w] && !sinit_is_cctor(work[w].member) &&
        static_init_body_has_live_use(graph, work[w].fn))
      pending += sinit_activate(g, work[w].type_sym, work[w].cur_inst);
  }
  for (int i = 0; i < g->static_init_use_count; i++) {
    struct zan_static_init_use use = g->static_init_uses[i];
    if (static_init_body_has_live_use(graph, use.caller))
      pending += sinit_activate(g, use.sym, use.inst);
  }
  if (g->static_init_ctor_demand) {
    for (int m = 0; m < g->refl_mtab_count; m++) {
      if (g->refl_mtabs[m].ctors)
        pending += sinit_activate(g, g->refl_mtabs[m].sym, NULL);
    }
  }
  for (int i = 0; i < g->method_spec_count; i++) {
    struct zan_method_spec *sp = &g->method_specs[i];
    if (static_init_body_has_live_use(graph, sp->fn))
      pending += sinit_activate(g, sp->type_sym, sp->owner_inst);
  }
  for (int i = 0; i < g->gctx_entry_count; i++) {
    zan_gctx_entry_t *e = &g->gctx_entries[i];
    if (!e->gv)
      continue;
    zan_symbol_t *owner_sym =
        e->is_method_spec ? (e->sym ? e->sym->parent : NULL) : e->sym;
    if (!owner_sym)
      continue;
    if (is_user_generic_sym(owner_sym) &&
        (!e->concrete_inst || !type_is_concrete(e->concrete_inst)))
      continue;
    if (sinit_value_has_live_use(graph, e->gv, 0))
      pending += sinit_activate(g, owner_sym, e->concrete_inst);
  }
  for (int i = 0; i < g->function_count; i++) {
    zan_symbol_t *sym = g->functions[i].sym;
    if (sym_declares_extern(sym) &&
        static_init_body_has_live_use(graph, g->functions[i].fn))
      pending += sinit_activate(g, sym->parent, NULL);
  }
  for (int t = 0; t < g->static_init_type_count; t++) {
    if (!g->static_init_types[t].emitted) {
      emit_static_type_initializer(g, t);
      pending++;
      if (zan_diag_has_errors(g->diag))
        break;
    }
  }
  return pending;
}

static void sinit_emit_ordered(zan_irgen_t *g, int t, const uint8_t *deps,
                               uint8_t *color, int count) {
  if (color[t])
    return; /* Existing deterministic cycle break: no recursion/retry. */
  color[t] = 1;
  for (int d = 0; d < count; d++) {
    if (deps[t * count + d] && color[d] != 1)
      sinit_emit_ordered(g, d, deps, color, count);
  }
  color[t] = 2;
  LLVMValueRef fn = g->static_init_types[t].fn;
  if (fn)
    zan_call2(g->builder, LLVMGlobalGetValueType(fn), fn, NULL, 0, "");
}

static void sinit_register_library_startup(zan_irgen_t *g, LLVMValueRef fn) {
  if (!g->emit_lib || LLVMGetNamedFunction(g->mod, "main"))
    return;
  LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMValueRef record = LLVMConstStructInContext(
      g->ctx,
      (LLVMValueRef[]){LLVMConstInt(i32, 65535, 0), fn, LLVMConstNull(ptr)}, 3,
      0);
  LLVMValueRef *records = NULL;
  LLVMValueRef *old_globals = NULL;
  int count = 0, cap = 0, old_count = 0, old_cap = 0;
  for (LLVMValueRef gv = LLVMGetFirstGlobal(g->mod); gv;
       gv = LLVMGetNextGlobal(gv)) {
    const char *name = LLVMGetValueName(gv);
    if (!name || (strcmp(name, "llvm.global_ctors") != 0 &&
                  strncmp(name, "llvm.global_ctors.", 18) != 0))
      continue;
    LLVMValueRef initializer = LLVMGetInitializer(gv);
    unsigned n = initializer ? LLVMGetArrayLength(LLVMTypeOf(initializer)) : 0;
    for (unsigned i = 0; i < n; i++) {
      if (ZAN_TAB_ENSURE(records, count, cap, 8))
        records[count++] = LLVMGetAggregateElement(initializer, i);
    }
    if (ZAN_TAB_ENSURE(old_globals, old_count, old_cap, 4))
      old_globals[old_count++] = gv;
  }
  if (ZAN_TAB_ENSURE(records, count, cap, 8))
    records[count++] = record;
  LLVMTypeRef ty = LLVMTypeOf(record);
  for (int i = 0; i < old_count; i++)
    LLVMSetValueName2(old_globals[i], "", 0);
  LLVMValueRef ctors = LLVMAddGlobal(g->mod, LLVMArrayType(ty, (unsigned)count),
                                     "llvm.global_ctors");
  LLVMSetLinkage(ctors, LLVMAppendingLinkage);
  LLVMSetInitializer(ctors, LLVMConstArray(ty, records, (unsigned)count));
  for (int i = 0; i < old_count; i++) {
    LLVMReplaceAllUsesWith(old_globals[i], ctors);
    LLVMDeleteGlobal(old_globals[i]);
  }
  free(records);
  free(old_globals);
}

static void finalize_static_initializers(zan_irgen_t *g, zan_ast_node_t *unit,
                                         method_body_work_t *work,
                                         int work_count,
                                         const unsigned char *live,
                                         const work_fn_index_t *ix) {
  LLVMValueRef coordinator = get_static_init_coordinator(g);
  LLVMPositionBuilderAtEnd(
      g->builder, LLVMAppendBasicBlockInContext(g->ctx, coordinator, "entry"));
  di_clear(g);
  int count = g->static_init_type_count;
  uint8_t *deps = (uint8_t *)calloc((size_t)count * (size_t)count + 1, 1);
  uint8_t *color = (uint8_t *)calloc((size_t)count + 1, 1);
  int *use_types =
      (int *)malloc(((size_t)g->static_init_use_count + 1) * sizeof(int));
  int *work_types = (int *)malloc(((size_t)work_count + 1) * sizeof(int));
  for (int i = 0; i < g->static_init_use_count; i++) {
    use_types[i] = -1;
    for (int d = 0; d < count; d++) {
      if (g->static_init_uses[i].sym == g->static_init_types[d].sym &&
          sinit_same_inst(g->static_init_uses[i].inst,
                          g->static_init_types[d].inst)) {
        use_types[i] = d;
        break;
      }
    }
  }
  for (int w = 0; w < work_count; w++) {
    work_types[w] = -1;
    if (!live[w] || sinit_is_cctor(work[w].member))
      continue;
    for (int d = 0; d < count; d++) {
      if (work[w].type_sym == g->static_init_types[d].sym &&
          sinit_same_inst(work[w].cur_inst, g->static_init_types[d].inst)) {
        work_types[w] = d;
        break;
      }
    }
  }
  sinit_exec_graph_t graph;
  sinit_graph_build(&graph, g, ix);
  for (int t = 0; t < count; t++) {
    if (!g->static_init_types[t].fn)
      continue;
    sinit_compute_closure(&graph, g->static_init_types[t].fn);
    for (int i = 0; i < g->static_init_use_count; i++) {
      int d = use_types[i];
      if (d >= 0 && d != t &&
          static_init_body_has_live_use(&graph, g->static_init_uses[i].caller))
        deps[t * count + d] = 1;
    }
    for (int w = 0; w < work_count; w++) {
      int d = work_types[w];
      if (d >= 0 && d != t && static_init_body_has_live_use(&graph, work[w].fn))
        deps[t * count + d] = 1;
    }
  }
  free(use_types);
  free(work_types);
  /* Start DFS in declaration order, matching the existing cycle break. The
   * executable dependency graph also covers dependencies behind helpers. */
  for (int oi = 0; oi < unit->comp_unit.decls.count; oi++) {
    zan_ast_node_t *decl = unit->comp_unit.decls.items[oi];
    for (int t = 0; t < count; t++) {
      if (g->static_init_types[t].sym->decl == decl)
        sinit_emit_ordered(g, t, deps, color, count);
    }
  }
  for (int t = 0; t < count; t++)
    sinit_emit_ordered(g, t, deps, color, count);
  for (int w = 0; w < work_count; w++) {
    if (live[w] && !work[w].cur_inst &&
        is_module_initializer_member(work[w].type_sym, work[w].member)) {
      LLVMValueRef fn = work[w].fn;
      zan_call2(g->builder, LLVMGlobalGetValueType(fn), fn, NULL, 0, "");
    }
  }
  sinit_graph_dispose(&graph);
  free(deps);
  free(color);
  LLVMBuildRetVoid(g->builder);
  LLVMClearInsertionPosition(g->builder);
  zan_irgen_compact_completed(g, coordinator);
  finalize_static_rc_cleanup(g);
}
