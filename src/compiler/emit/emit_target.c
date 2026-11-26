static bool reflected_method_has_derived_owner(zan_irgen_t *g,
                                               zan_symbol_t *owner,
                                               zan_ast_node_t *member) {
  if (!g->refl_used || !owner || member->kind != AST_METHOD_DECL ||
      (member->method_decl.modifiers & MOD_STATIC))
    return false;
  /* Method tables include inherited instance hooks. Their declaring type need
   * not have its own metadata or allocation site before late thunk emission. */
  for (int m = 0; m < g->refl_mtab_count; m++) {
    zan_symbol_t *sym = g->refl_mtabs[m].sym;
    if (!refl_owner_can_have_instance(g, sym))
      continue;
    zan_type_t *base = sym && sym->type ? sym->type->base_type : NULL;
    for (int depth = 0; base && base->sym && depth < 32;
         base = base->base_type, depth++) {
      if (base->sym == owner)
        return true;
    }
  }
  return false;
}

static bool extern_type_can_supply_instances(zan_irgen_t *g, zan_type_t *type,
                                             int depth) {
  if (!type || depth >= 32)
    return true;
  switch (type->kind) {
  case TYPE_CLASS:
  case TYPE_INTERFACE:
  case TYPE_OBJECT:
  case TYPE_TYPE_PARAM:
  case TYPE_TASK:
  case TYPE_ERROR:
    return true;
  case TYPE_DELEGATE: {
    /* Native delegates with arguments may invoke nested callbacks that
     * supply objects. Parameterless Action has no such object channel. */
    if (type->delegate_param_count > 0)
      return true;
    zan_type_t *result =
        type->type_arg_count
            ? subst_type_param_deep(g, type->delegate_ret_type, type)
            : type->delegate_ret_type;
    return extern_type_can_supply_instances(g, result, depth + 1);
  }
  case TYPE_ARRAY:
  case TYPE_NULLABLE:
    return extern_type_can_supply_instances(g, type->element_type, depth + 1);
  case TYPE_STRUCT:
    if (!type->sym)
      return true;
    for (int i = 0; i < type->sym->member_count; i++) {
      zan_symbol_t *member = type->sym->members[i];
      if (!member || (member->modifiers & MOD_STATIC) ||
          (member->kind != SYM_FIELD && member->kind != SYM_PROPERTY))
        continue;
      zan_type_t *field = type->type_arg_count
                              ? subst_type_param_deep(g, member->type, type)
                              : member->type;
      if (extern_type_can_supply_instances(g, field, depth + 1))
        return true;
    }
    return false;
  default:
    return false;
  }
}

static bool extern_can_supply_instances(zan_irgen_t *g) {
  for (int i = 0; i < g->function_count; i++) {
    zan_symbol_t *sym = g->functions[i].sym;
    if (!sym_declares_extern(sym))
      continue;
    zan_ast_node_t *decl = sym->decl;
    zan_type_t *ret =
        decl->method_decl.return_type
            ? zan_binder_resolve_type(g->binder, decl->method_decl.return_type)
            : NULL;
    if (decl->method_decl.return_type &&
        extern_type_can_supply_instances(g, ret, 0)) {
      zan_compile_trace(
          "publish instance fallback: extern %.*s result kind=%d type=%.*s",
          (int)sym->name.len, sym->name.str, ret ? (int)ret->kind : -1,
          ret ? (int)ret->name.len : 0, ret ? ret->name.str : "");
      return true;
    }
    for (int p = 0; p < decl->method_decl.params.count; p++) {
      zan_ast_node_t *param = decl->method_decl.params.items[p];
      /* Native code can also fill reference containers or invoke a
       * callback passed by value with objects allocated outside Zan. */
      zan_type_t *type = zan_binder_resolve_type(g->binder, param->param.type);
      if (extern_type_can_supply_instances(g, type, 0)) {
        zan_compile_trace(
            "publish instance fallback: extern %.*s argument kind=%d type=%.*s",
            (int)sym->name.len, sym->name.str, type ? (int)type->kind : -1,
            type ? (int)type->name.len : 0, type ? type->name.str : "");
        return true;
      }
    }
  }
  return false;
}

static bool fn_is_live_used(LLVMModuleRef mod, const char *name) {
  LLVMValueRef fn = LLVMGetNamedFunction(mod, name);
  return fn && LLVMGetFirstUse(fn) != NULL;
}

static bool fn_has_use_outside(LLVMValueRef fn, LLVMValueRef skip1,
                               LLVMValueRef skip2) {
  if (!fn)
    return false;
  for (LLVMUseRef u = LLVMGetFirstUse(fn); u; u = LLVMGetNextUse(u)) {
    LLVMValueRef user = LLVMGetUser(u);
    if (!user || !LLVMIsAInstruction(user))
      return true;
    LLVMValueRef parent =
        LLVMGetBasicBlockParent(LLVMGetInstructionParent(user));
    if (parent != skip1 && parent != skip2)
      return true;
  }
  return false;
}

static void erase_calls_to_fn(LLVMValueRef fn, bool erase_dead_loads) {
  if (!fn)
    return;
  int cap = 16, n = 0;
  LLVMValueRef *calls =
      (LLVMValueRef *)malloc((size_t)cap * sizeof(LLVMValueRef));
  if (!calls)
    return;
  for (LLVMUseRef u = LLVMGetFirstUse(fn); u; u = LLVMGetNextUse(u)) {
    LLVMValueRef user = LLVMGetUser(u);
    if (!user || (!LLVMIsACallInst(user) && !LLVMIsAInvokeInst(user)))
      continue;
    if (LLVMGetCalledValue(user) != fn)
      continue;
    if (n >= cap) {
      cap *= 2;
      LLVMValueRef *nb =
          (LLVMValueRef *)realloc(calls, (size_t)cap * sizeof(LLVMValueRef));
      if (!nb)
        break;
      calls = nb;
    }
    calls[n++] = user;
  }
  for (int i = 0; i < n; i++) {
    LLVMValueRef call = calls[i];
    int lcap = 8, ln = 0;
    LLVMValueRef loads[8];
    if (erase_dead_loads) {
      unsigned argc = LLVMGetNumArgOperands(call);
      for (unsigned a = 0; a < argc && ln < lcap; a++) {
        LLVMValueRef op = LLVMGetOperand(call, a);
        if (op && LLVMIsAInstruction(op) &&
            LLVMGetInstructionOpcode(op) == LLVMLoad) {
          loads[ln++] = op;
        }
      }
    }
    LLVMInstructionEraseFromParent(call);
    for (int li = 0; li < ln; li++) {
      if (LLVMGetFirstUse(loads[li]) == NULL) {
        LLVMInstructionEraseFromParent(loads[li]);
      }
    }
  }
  free(calls);
}

static void delete_fn_if_unused(LLVMModuleRef mod, const char *name) {
  LLVMValueRef fn = LLVMGetNamedFunction(mod, name);
  if (fn && LLVMGetFirstUse(fn) == NULL) {
    LLVMDeleteFunction(fn);
  }
}

static void prune_unused_eh_terminate(zan_irgen_t *g) {
  if (g->emit_lib || g->target_is_wasm)
    return;
  bool eh_used = fn_is_live_used(g->mod, "__cxa_throw") ||
                 fn_is_live_used(g->mod, "__cxa_rethrow") ||
                 fn_is_live_used(g->mod, "__cxa_allocate_exception") ||
                 fn_is_live_used(g->mod, "__cxa_begin_catch") ||
                 fn_is_live_used(g->mod, "__gxx_personality_v0") ||
                 fn_is_live_used(g->mod, "__gxx_personality_seh0");
  if (!eh_used) {
    for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
         fn = LLVMGetNextFunction(fn)) {
      if (!LLVMIsDeclaration(fn) && LLVMHasPersonalityFn(fn)) {
        eh_used = true;
        break;
      }
    }
  }
  if (!eh_used) {
    LLVMValueRef set_term =
        LLVMGetNamedFunction(g->mod, "_ZSt13set_terminatePFvvE");
    if (set_term) {
      erase_calls_to_fn(set_term, false);
      delete_fn_if_unused(g->mod, "_ZSt13set_terminatePFvvE");
    }
    LLVMValueRef term_fn = LLVMGetNamedFunction(g->mod, "__zan_eh_terminate");
    if (term_fn && LLVMGetFirstUse(term_fn) == NULL) {
      LLVMDeleteFunction(term_fn);
    }
    delete_fn_if_unused(g->mod, "__zan_eh_unhandled");
  } else if (g->target_is_windows &&
             !LLVMGetNamedFunction(
                 g->mod, "_ZN9__gnu_cxx27__verbose_terminate_handlerEv")) {
    /* MinGW libsupc++.a references __verbose_terminate_handler from
     * eh_term_handler.o, pulling vterminate.o -> cp-demangle.o (~150 KiB).
     * Zan always overrides std::set_terminate in main when EH is active. */
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(g->ctx);
    LLVMValueRef vterm =
        LLVMAddFunction(g->mod, "_ZN9__gnu_cxx27__verbose_terminate_handlerEv",
                        LLVMFunctionType(void_ty, NULL, 0, 0));
    LLVMBasicBlockRef bb =
        LLVMAppendBasicBlockInContext(g->ctx, vterm, "entry");
    zan_builder_point_t pt = zan_builder_save(g);
    LLVMPositionBuilderAtEnd(g->builder, bb);
    LLVMValueRef abort_fn =
        get_libc_fn(g, "abort", LLVMFunctionType(void_ty, NULL, 0, 0));
    zan_call2(g->builder, LLVMGlobalGetValueType(abort_fn), abort_fn, NULL, 0,
              "");
    LLVMBuildUnreachable(g->builder);
    zan_builder_restore(g, pt);
  }
}

static void prune_unused_co_sched_init(zan_irgen_t *g) {
  if (g->emit_lib || !g->rt_co_sched_init)
    return;
  LLVMValueRef co_fns[] = {
      g->rt_co_ready,         g->rt_co_poll,      g->rt_co_frame_alloc,
      g->rt_co_frame_free,    g->rt_co_sched_run, g->rt_co_sched_run_until,
      g->rt_co_delay,         g->rt_io_wait_co,   g->rt_io_recv_co,
      g->rt_io_recv_to_co,    g->rt_io_accept_co, g->rt_io_resolve_co,
      g->rt_io_resolve_sa_co, g->rt_blocking_co};
  for (size_t i = 0; i < sizeof(co_fns) / sizeof(co_fns[0]); i++) {
    if (fn_has_use_outside(co_fns[i], g->rt_co_sched_init,
                           g->rt_co_sched_run)) {
      return;
    }
  }
  erase_calls_to_fn(g->rt_co_sched_init, false);
  if (LLVMGetFirstUse(g->rt_co_sched_init) == NULL) {
    LLVMDeleteFunction(g->rt_co_sched_init);
    g->rt_co_sched_init = NULL;
  }
}

static bool module_needs_heap_diagnostics(zan_irgen_t *g) {
  if (g->emit_lib || g->check_leaks)
    return true;
  return fn_is_live_used(g->mod, "zan_gc_detect_cycles") ||
         fn_is_live_used(g->mod, "zan_gc_collect_cycles") ||
         fn_is_live_used(g->mod, "zan_gc_write_heap_snapshot") ||
         fn_is_live_used(g->mod, "zan_rt_heap_diagnostics_enable") ||
         fn_is_live_used(g->mod, "zan_rt_heap_mutator_enter") ||
         fn_is_live_used(g->mod, "zan_rt_heap_mutator_leave");
}

static void prune_unused_heap_diag(zan_irgen_t *g, bool need_heap_diag) {
  if (g->hide_diagnostics && g->rt_live_register_named) {
    for (LLVMUseRef use = LLVMGetFirstUse(g->rt_live_register_named); use;
         use = LLVMGetNextUse(use)) {
      LLVMValueRef call = LLVMGetUser(use);
      if (LLVMIsACallInst(call) &&
          LLVMGetCalledValue(call) == g->rt_live_register_named) {
        LLVMSetOperand(call, 2,
                       LLVMConstNull(LLVMTypeOf(LLVMGetOperand(call, 2))));
      }
    }
  }
  if (g->emit_lib)
    return;
  static const char *const diag_weak[] = {
      "zan_gc_detect_cycles", "zan_gc_collect_cycles",
      "zan_gc_write_heap_snapshot", "zan_rt_heap_diagnostics_enable",
      "zan_rt_heap_mutator_enter", "zan_rt_heap_mutator_leave"};
  for (size_t i = 0; i < sizeof(diag_weak) / sizeof(diag_weak[0]); i++) {
    delete_fn_if_unused(g->mod, diag_weak[i]);
  }
  if (!need_heap_diag) {
    if (g->rt_live_register_named) {
      erase_calls_to_fn(g->rt_live_register_named, false);
      if (LLVMGetFirstUse(g->rt_live_register_named) == NULL) {
        LLVMDeleteFunction(g->rt_live_register_named);
        g->rt_live_register_named = NULL;
      }
    }
    if (g->rt_live_register) {
      erase_calls_to_fn(g->rt_live_register, false);
      if (LLVMGetFirstUse(g->rt_live_register) == NULL) {
        LLVMDeleteFunction(g->rt_live_register);
        g->rt_live_register = NULL;
      }
    }
    if (g->rt_live_register_array) {
      erase_calls_to_fn(g->rt_live_register_array, false);
      if (LLVMGetFirstUse(g->rt_live_register_array) == NULL) {
        LLVMDeleteFunction(g->rt_live_register_array);
        g->rt_live_register_array = NULL;
      }
    }
    if (g->rt_live_unregister) {
      erase_calls_to_fn(g->rt_live_unregister, false);
      if (LLVMGetFirstUse(g->rt_live_unregister) == NULL) {
        LLVMDeleteFunction(g->rt_live_unregister);
        g->rt_live_unregister = NULL;
      }
    }
    LLVMValueRef reg_fg =
        LLVMGetNamedFunction(g->mod, "zan_rt_register_site_field_graphs");
    if (reg_fg) {
      erase_calls_to_fn(reg_fg, true);
      delete_fn_if_unused(g->mod, "zan_rt_register_site_field_graphs");
    }
    LLVMValueRef reg_dt =
        LLVMGetNamedFunction(g->mod, "zan_rt_register_site_dtors");
    if (reg_dt) {
      erase_calls_to_fn(reg_dt, true);
      delete_fn_if_unused(g->mod, "zan_rt_register_site_dtors");
    }
  }
  bool request_profile = fn_is_live_used(g->mod, "zan_task_profile_begin");
  if (!request_profile) {
    const char *hooks[] = {"zan_task_profile_wait",
                           "zan_task_profile_checkpoint"};
    for (size_t i = 0; i < sizeof(hooks) / sizeof(hooks[0]); i++) {
      LLVMValueRef hook = LLVMGetNamedFunction(g->mod, hooks[i]);
      if (!hook)
        continue;
      erase_calls_to_fn(hook, false);
      delete_fn_if_unused(g->mod, hooks[i]);
    }
  }
  if (!fn_is_live_used(g->mod, "zan_rt_alloc_bytes_thread") &&
      !fn_is_live_used(g->mod, "zan_rt_alloc_bytes_total")) {
    LLVMValueRef alloc_add = LLVMGetNamedFunction(g->mod, "zan_rt_alloc_add");
    if (alloc_add) {
      if (request_profile) {
        LLVMValueRef hook =
            LLVMAddFunction(g->mod, "zan_task_profile_alloc_add",
                            LLVMGlobalGetValueType(alloc_add));
        LLVMReplaceAllUsesWith(alloc_add, hook);
      } else {
        erase_calls_to_fn(alloc_add, false);
      }
      delete_fn_if_unused(g->mod, "zan_rt_alloc_add");
    }
  }
}

zan_status_t zan_irgen_emit(zan_irgen_t *g, zan_ast_node_t *unit) {
  if (!unit || unit->kind != AST_COMPILATION_UNIT)
    return ZAN_ERROR;
  g_di_emit_ctx = g;
  const char *eager_env = getenv("ZAN_EAGER_CODEGEN");
  bool eager_codegen = (eager_env && strcmp(eager_env, "1") == 0);
  /* Ordinary (non -g) executable builds follow publish's closed-world
   * rules, so reflection that works in development also works after
   * --publish. They used to emit every body of every pulled-in file and a
   * reflection thunk for every method: a small Gui test was 3.4M LLVM
   * instructions, 90 s and 4 GB. ZAN_NO_DEV_PRUNE=1 restores full emission. */
  const char *no_dev_prune = getenv("ZAN_NO_DEV_PRUNE");
  bool dev_prune = !(no_dev_prune && strcmp(no_dev_prune, "1") == 0);
  g->closed_world =
      g->publish_mode || (dev_prune && !g->emit_debug && !g->emit_lib);
  const char *no_instance_prune = getenv("ZAN_NO_PUBLISH_INSTANCE_PRUNE");
  g->publish_instance_prune =
      g->closed_world && !g->emit_debug && !g->emit_lib && !eager_codegen &&
      !(no_instance_prune && strcmp(no_instance_prune, "1") == 0);
  const char *no_reflect_demand = getenv("ZAN_NO_PUBLISH_REFLECT_DEMAND");
  g->publish_reflect_demand =
      g->closed_world && !g->emit_debug && !g->emit_lib && !eager_codegen &&
      !(no_reflect_demand && strcmp(no_reflect_demand, "1") == 0);
  refl_collect_literal_names(g, unit);
  di_debug_types_reset();
  if (g->publish_mode && !g->emit_debug && !g->function_compactor) {
    char error[4096];
    g->function_compactor =
        zan_irgen_compactor_create(g->mod, error, sizeof(error));
    if (!g->function_compactor) {
      zan_diag_emit_code(g->diag, DIAG_ERROR, unit->loc, 3089,
                         "LLVM function compactor initialization failed: %s",
                         error);
      return ZAN_ERROR;
    }
  }

  class_vptr_plan_compute(g, unit);
  discover_generic_insts(g, unit);

  for (int i = 0; i < unit->comp_unit.decls.count; i++) {
    zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
    if (decl->kind == AST_STRUCT_DECL || decl->kind == AST_CLASS_DECL) {
      zan_symbol_t *sym = zan_binder_lookup(g->binder, decl->type_decl.name);
      if (sym)
        register_struct_type(g, sym);
    }
  }

  int work_count = 0;
  method_body_work_t *work = declare_user_methods(g, unit, &work_count);
  if (zan_diag_has_errors(g->diag)) {
    free(work);
    return ZAN_ERROR;
  }
  /* Imported managed results and ref/out slots can supply instances without a
   * local allocation site. Native handles and scalar returns do not do so. */
  if (g->publish_instance_prune && extern_can_supply_instances(g))
    g->publish_instance_prune = false;
  const char *no_vslot_demand = getenv("ZAN_NO_PUBLISH_VSLOT_DEMAND");
  g->publish_vslot_demand =
      g->publish_instance_prune &&
      !(no_vslot_demand && strcmp(no_vslot_demand, "1") == 0);

  bool prune_stdlib_bodies =
      (g->closed_world || g->obfuscate_strings) && !g->emit_debug &&
      !eager_codegen;
  /* Non-publish builds still generate unreached bodies once, for their
   * codegen diagnostics, then drop them before LLVM optimizes. */
  bool diag_unused_bodies = !g->publish_mode && !g->obfuscate_strings;
  unsigned char *live = (unsigned char *)calloc((size_t)work_count + 1, 1);
  for (int w = 0; w < work_count; w++) {
    zan_ast_node_t *m = work[w].member;
    if (sinit_is_cctor(m))
      continue;
    zan_symbol_t *owner = work[w].type_sym;
    bool is_entry =
        m->kind == AST_METHOD_DECL && (m->method_decl.modifiers & MOD_STATIC) &&
        owner && owner->decl && owner->decl->type_decl.type_params.count == 0 &&
        ((m->method_decl.name.len == 4 &&
          memcmp(m->method_decl.name.str, "Main", 4) == 0) ||
         (m->method_decl.name.len == 12 &&
          memcmp(m->method_decl.name.str, "__DesignMain", 12) == 0));
    bool is_user_decl = owner && owner->decl && !owner->decl->from_stdlib &&
                        !owner->decl->from_package;
    bool is_lib_export =
        g->emit_lib && is_user_decl && !work[w].cur_inst &&
        ((m->method_decl.modifiers & MOD_PUBLIC) ||
         m->kind == AST_CONSTRUCTOR_DECL);
    bool is_mod_init = is_module_initializer_member(owner, m);
    if (is_entry || is_lib_export || is_mod_init)
      live[w] = 1;
  }
  emit_demanded_method_bodies(g, work, work_count, live);
  emit_pending_method_specs(g);
  discover_generic_insts(g, unit);
  emit_pending_method_specs(g);
  if (zan_diag_has_errors(g->diag)) {
    free(live);
    free(work);
    return ZAN_ERROR;
  }

  {
    zan_ast_node_t *design_main = NULL;
    /* C# 同步：泛型类中的 static Main 不是合法入口——按开放泛型编译
     * 会让体内对该类类型参数的一切显式引用（调用类型实参等）落回
     * 开放参数（探针实证扩展收到 entity=T cols=0），静默产坏码。 */
    bool generic_main = false;
    zan_loc_t generic_main_loc = zan_loc(0, 0, 0, 0);
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
      zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
      if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL)
        continue;

      for (int j = 0; j < decl->type_decl.members.count; j++) {
        zan_ast_node_t *member = decl->type_decl.members.items[j];
        if (member->kind != AST_METHOD_DECL ||
            !(member->method_decl.modifiers & MOD_STATIC))
          continue;
        if (member->method_decl.name.len == 4 &&
            memcmp(member->method_decl.name.str, "Main", 4) == 0) {
          if (decl->type_decl.type_params.count > 0) {
            if (!generic_main) {
              generic_main = true;
              generic_main_loc = member->loc;
            }
            continue;
          }
          zan_symbol_t *main_type_sym =
              zan_binder_lookup(g->binder, decl->type_decl.name);
          emit_main_method(g, member, main_type_sym, unit);
          goto done;
        }
        if (!design_main && member->method_decl.name.len == 12 &&
            memcmp(member->method_decl.name.str, "__DesignMain", 12) == 0 &&
            decl->type_decl.type_params.count == 0) {
          design_main = member;
        }
      }
    }
    if (design_main) {
      for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL)
          continue;
        bool has = false;
        for (int j = 0; j < decl->type_decl.members.count; j++) {
          if (decl->type_decl.members.items[j] == design_main) {
            has = true;
            break;
          }
        }
        if (has) {
          zan_symbol_t *main_type_sym =
              zan_binder_lookup(g->binder, decl->type_decl.name);
          emit_main_method(g, design_main, main_type_sym, unit);
          break;
        }
      }
    }
    if (!LLVMGetNamedFunction(g->mod, "main") && generic_main) {
      zan_diag_emit_code(
          g->diag, DIAG_ERROR, generic_main_loc, 3102,
          "entry point 'Main' cannot be declared in a generic class"
          " -- move it to a non-generic type (C# parity)");
    }
  }
done:;
  if (g->function_compactor) {
    LLVMClearInsertionPosition(g->builder);
    LLVMValueRef main_fn = LLVMGetNamedFunction(g->mod, "main");
    if (main_fn)
      zan_irgen_compact_completed(g, main_fn);
  }
  emit_pending_method_specs(g);
  if (zan_diag_has_errors(g->diag)) {
    free(live);
    free(work);
    return ZAN_ERROR;
  }

  emit_vtables(g);
  work_fn_index_t work_ix = {.g = g};
  work_fn_index_build(&work_ix, work, work_count);
  int pending;
  do {
    pending = 0;
    bool ctor_used_before = g->refl_ctor_used;
    const bool sfield_used_before = g->refl_sfield_used;
    const bool site_meta_before = refl_needs_site_meta(g);
    const bool invoke_unknown_before = g->refl_invoke_unknown;
    const int invoke_names_before = g->refl_invoke_name_count;
    const unsigned query_used_before = g->refl_query_used;
    const int vslots_before = g->vslot_demand_count;
    int metas_before = g->refl_meta_count;
    int mtabs_before = g->refl_mtab_count;
    int sites_before = g->leak_site_count;
    /* Warm metadata and demanded ctor thunks before rooting bodies. Their
     * implicit field initializers can discover further reflected types and
     * callable dependencies; no dead-body stubs or site tables exist yet. */
    refl_reserve_thunk_sites(g);
    emit_pending_method_specs(g);
    if (zan_diag_has_errors(g->diag))
      break;
    if (g->publish_vslot_demand)
      emit_vtables(g);
    sinit_exec_graph_t static_graph;
    sinit_graph_build(&static_graph, g, &work_ix);
    pending += emit_pending_static_initializers(g, unit, work, work_count, live,
                                                &static_graph);
    if (zan_diag_has_errors(g->diag)) {
      sinit_graph_dispose(&static_graph);
      break;
    }
    for (int w = 0; w < work_count; w++) {
      if (live[w])
        continue;
      zan_ast_node_t *member = work[w].member;
      bool is_refl_root = false;
      if (g->refl_used && work[w].type_sym) {
        zan_symbol_t *tsym = work[w].type_sym;
        if (g->refl_mtabs) {
          for (int m = 0; m < g->refl_mtab_count; m++) {
            if (g->refl_mtabs[m].sym == tsym) {
              is_refl_root = true;
              break;
            }
          }
        }
        if (!is_refl_root && g->refl_metas) {
          for (int m = 0; m < g->refl_meta_count; m++) {
            if (g->refl_metas[m].sym == tsym) {
              is_refl_root = true;
              break;
            }
          }
        }
        if (!is_refl_root && refl_needs_site_meta(g) && g->site_syms) {
          for (int m = 0; m < g->leak_site_count; m++) {
            if (g->site_syms[m] == tsym) {
              is_refl_root = true;
              break;
            }
          }
        }
      }
      if (!is_refl_root)
        is_refl_root =
            reflected_method_has_derived_owner(g, work[w].type_sym, member);
      /* Metadata/thunk retention is not executable type demand. Explicit
       * CreateInstance/Invoke and established annotated/HTML callbacks
       * are callable roots; cctors are reached only from activated init. */
      if (sinit_is_cctor(member)) {
        is_refl_root = false;
      } else if (is_refl_root && member->kind == AST_METHOD_DECL) {
        is_refl_root = refl_method_needs_thunk(g, work[w].type_sym, member) &&
                       static_init_body_has_live_use(&static_graph, work[w].fn);
      } else if (is_refl_root && member->kind == AST_CONSTRUCTOR_DECL) {
        bool is_lib_export_owner =
            g->emit_lib && work[w].type_sym && work[w].type_sym->decl &&
            !work[w].type_sym->decl->from_stdlib &&
            !work[w].type_sym->decl->from_package && !work[w].cur_inst;
        is_refl_root = is_lib_export_owner || g->static_init_ctor_demand;
      }
      zan_symbol_t *owner = work[w].type_sym;
      bool is_user_decl = owner && owner->decl && !owner->decl->from_stdlib &&
                          !owner->decl->from_package;
      bool root =
          !sinit_is_cctor(member) &&
          ((g->emit_lib && is_user_decl && !work[w].cur_inst &&
            ((member->method_decl.modifiers & MOD_PUBLIC) ||
             member->kind == AST_CONSTRUCTOR_DECL)) ||
           is_refl_root);
      if (!root)
        root = body_has_live_use(work[w].fn, live, &work_ix) &&
               static_init_body_has_live_use(&static_graph, work[w].fn);
      if (root) {
        live[w] = 1;
        pending++;
      }
    }
    sinit_graph_dispose(&static_graph);
    if (pending) {
      emit_demanded_method_bodies(g, work, work_count, live);
      emit_pending_method_specs(g);
      if (zan_diag_has_errors(g->diag))
        break;
    }
    if (materialize_pending_gctx(g))
      pending++;
    /* Specializations can emit CreateInstance, enable dynamic object
     * reflection or discover metadata after the prewarm, even if no ordinary
     * body became live. Reach the fixed point before freezing dead stubs. */
    if (ctor_used_before != g->refl_ctor_used ||
        sfield_used_before != g->refl_sfield_used ||
        site_meta_before != refl_needs_site_meta(g) ||
        invoke_unknown_before != g->refl_invoke_unknown ||
        invoke_names_before != g->refl_invoke_name_count ||
        query_used_before != g->refl_query_used ||
        vslots_before != g->vslot_demand_count ||
        metas_before != g->refl_meta_count ||
        mtabs_before != g->refl_mtab_count ||
        sites_before != g->leak_site_count) {
      pending++;
    }
  } while (pending);
  if (!zan_diag_has_errors(g->diag)) {
    finalize_static_initializers(g, unit, work, work_count, live, &work_ix);
  }
  free(work_ix.slots);
  /* Preserve ordinary/debug codegen diagnostics on user declarations after
   * freezing executable initialization demand. Stdlib/package bodies are
   * skipped unless debug/eager/ZAN_DIAG_LIB_BODIES=1 is requested. */
  if (diag_unused_bodies && !zan_diag_has_errors(g->diag)) {
    unsigned char *diagnostic_live =
        (unsigned char *)calloc((size_t)work_count + 1, 1);
    const char *diag_lib_env = getenv("ZAN_DIAG_LIB_BODIES");
    bool diag_lib = diag_lib_env && strcmp(diag_lib_env, "1") == 0;
    for (int w = 0; w < work_count; w++) {
      if (live[w]) {
        diagnostic_live[w] = 2;
        continue;
      }
      if (sinit_is_cctor(work[w].member))
        continue;
      zan_symbol_t *owner = work[w].type_sym;
      bool is_lib = owner && owner->decl &&
                    (owner->decl->from_stdlib || owner->decl->from_package);
      if (prune_stdlib_bodies && ((is_lib && !diag_lib) || work[w].cur_inst))
        continue;
      diagnostic_live[w] = 1;
    }
    emit_demanded_method_bodies(g, work, work_count, diagnostic_live);
    emit_pending_method_specs(g);
    while (materialize_pending_gctx(g)) {
    }
    for (int w = 0; w < work_count; w++) {
      if (live[w])
        continue;
      if (prune_stdlib_bodies) {
        /* Root-reachable set is fixed above; this body (and whatever
         * it demanded) existed only for its diagnostics. The dead-stub
         * loop below gives it an unreachable body, as publish does. */
        zan_llvm_delete_body(work[w].fn);
        if (work[w].resume_fn)
          zan_llvm_delete_body(work[w].resume_fn);
        /* emitting the body already released param_types */
        if (diagnostic_live[w] == 2)
          work[w].param_types = NULL;
      } else if (diagnostic_live[w] == 2) {
        live[w] = 3;
      }
    }
    free(diagnostic_live);
  }
  for (int w = 0; w < work_count; w++) {
    if (live[w])
      continue;
    free(work[w].param_types);
    LLVMValueRef fn = work[w].fn;
    if (LLVMIsDeclaration(fn)) {
      LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "dead");
      LLVMPositionBuilderAtEnd(g->builder, bb);
      LLVMBuildUnreachable(g->builder);
    }
    fn = work[w].resume_fn;
    if (fn && LLVMIsDeclaration(fn)) {
      LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "dead");
      LLVMPositionBuilderAtEnd(g->builder, bb);
      LLVMBuildUnreachable(g->builder);
    }
  }
  free(live);
  free(work);
  if (g->publish_instance_prune && !refl_ctor_needs_thunk(g)) {
    LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
    LLVMValueRef false_val = LLVMConstInt(i1, 0, 0);
    LLVMValueRef true_val = LLVMConstInt(i1, 1, 0);
    for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
         fn = LLVMGetNextFunction(fn)) {
      if (LLVMIsDeclaration(fn))
        continue;
      for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn); bb;
           bb = LLVMGetNextBasicBlock(bb)) {
        LLVMValueRef inst = LLVMGetFirstInstruction(bb);
        while (inst) {
          LLVMValueRef next = LLVMGetNextInstruction(inst);
          if (LLVMIsAICmpInst(inst) &&
              interface_candidate_metadata(g, inst) &&
              !interface_candidate_can_exist(g, inst)) {
            LLVMIntPredicate pred = LLVMGetICmpPredicate(inst);
            if (pred == LLVMIntEQ) {
              LLVMReplaceAllUsesWith(inst, false_val);
              LLVMInstructionEraseFromParent(inst);
            } else if (pred == LLVMIntNE) {
              LLVMReplaceAllUsesWith(inst, true_val);
              LLVMInstructionEraseFromParent(inst);
            }
          }
          inst = next;
        }
      }
    }
  }
  if (zan_diag_has_errors(g->diag))
    return ZAN_ERROR;
  di_clear(g);
  prune_unused_eh_terminate(g);
  prune_unused_co_sched_init(g);
  bool need_heap_diag = module_needs_heap_diagnostics(g);
  prune_unused_heap_diag(g, need_heap_diag);
  /* 不变量：全部分配点登记必须先于任何按站点索引的表发射
   * （live/dtor/tyname/meta/desc）。反射 ctor thunk 的分配点原本在
   * finalize 才登记，名表早已冻结——反射造出的对象过不了任何
   * is/下转（MapRow (T)obj 全军覆没）。 */
  if (!prune_stdlib_bodies)
    emit_vtables(g);
  do {
    refl_reserve_thunk_sites(g);
  } while (materialize_pending_gctx(g));
  emit_site_live_tables(g);
  emit_site_dtor_table(g);
  emit_site_tyname_table(g);
  emit_site_meta_table(g);
  if (need_heap_diag)
    emit_site_field_graph_table(g);
  zan_irgen_emit_arc_desc_init(g, need_heap_diag);
  emit_all_class_releases(g);
  refl_finalize_mtabs(g);
  while (materialize_pending_gctx(g)) {
  }
  if (g->emit_lib && g->emit_shared && g->target_is_windows)
    emit_windows_dll_main(g);
  zan_irgen_emit_string_deobf(g);
  if (g->tid_name_reg_global) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    unsigned n = (unsigned)g->tid_name_count;
    LLVMTypeRef reg_ty = LLVMArrayType(g->tid_name_reg_ent_ty, n + 1);
    LLVMValueRef newg =
        LLVMAddGlobal(g->mod, reg_ty, LLVMGetValueName(g->tid_name_reg_global));
    LLVMSetLinkage(newg, LLVMInternalLinkage);
    LLVMReplaceAllUsesWith(g->tid_name_reg_global, newg);
    LLVMDeleteGlobal(g->tid_name_reg_global);
    g->tid_name_reg_global = newg;
    LLVMValueRef *elems =
        zan_arena_alloc(g->arena, (int)(n + 1) * sizeof(LLVMValueRef));
    LLVMValueRef *fields = zan_arena_alloc(g->arena, 2 * sizeof(LLVMValueRef));
    for (unsigned i = 0; i < n; i++) {
      fields[0] = LLVMConstBitCast(g->tid_names[i].tid, i8ptr);
      fields[1] = zan_irgen_intern_string(g, g->tid_names[i].name);
      elems[i] = LLVMConstStruct(fields, 2, 0);
    }
    fields[0] = LLVMConstNull(i8ptr);
    fields[1] = LLVMConstNull(i8ptr);
    elems[n] = LLVMConstStruct(fields, 2, 0); /* terminator */
    LLVMSetInitializer(
        newg, LLVMConstArray(g->tid_name_reg_ent_ty, elems, (unsigned)(n + 1)));
  }
  if (zan_diag_has_errors(g->diag)) {
    return ZAN_ERROR;
  }
  if (g->emit_debug && g->di_builder) {
    LLVMSetCurrentDebugLocation2(g->builder, NULL);
    LLVMDIBuilderFinalize(g->di_builder);
  }
  if (g->function_compactor) {
    LLVMClearInsertionPosition(g->builder);
    for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
         fn = LLVMGetNextFunction(fn)) {
      if (LLVMIsDeclaration(fn))
        continue;
      zan_irgen_compact_completed(g, fn);
      if (zan_diag_has_errors(g->diag))
        break;
    }
    zan_irgen_compactor_destroy((zan_irgen_compactor_t *)g->function_compactor);
    g->function_compactor = NULL;
    if (zan_diag_has_errors(g->diag))
      return ZAN_ERROR;
  }
  sinit_register_library_startup(g, get_static_init_coordinator(g));
  /* 站点号 → 描述符表定型：此后不再登记分配点。须在反射成员表压缩之前——
   * 描述符（及其反射记录）只经这张表被引用，占位表看不到它们会被当成死记录 */
  emit_desc_table(g);
  refl_compact_member_tables(g);
  char *error = NULL;
  if (LLVMVerifyModule(g->mod, LLVMReturnStatusAction, &error)) {
    const char *culprit = NULL;
    for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
         fn = LLVMGetNextFunction(fn)) {
      if (LLVMIsDeclaration(fn))
        continue;
      if (LLVMVerifyFunction(fn, LLVMReturnStatusAction)) {
        culprit = LLVMGetValueName(fn);
        break;
      }
    }
    zan_diag_emit_code(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0), 3090,
                       "LLVM verification failed%s%s: %s",
                       culprit ? " in " : "", culprit ? culprit : "", error);
    if (getenv("ZANC_DUMP_BAD_IR")) {
      for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
           fn = LLVMGetNextFunction(fn)) {
        if (LLVMIsDeclaration(fn))
          continue;
        if (!LLVMVerifyFunction(fn, LLVMReturnStatusAction))
          continue;
        char p[512];
        snprintf(p, sizeof(p), "_scratch/bad_%s.txt", LLVMGetValueName(fn));
        FILE *df = fopen(p, "w");
        if (df) {
          char *txt = LLVMPrintValueToString(fn);
          fputs(txt, df);
          free(txt);
          fclose(df);
          fprintf(stderr, "dumped %s\n", p);
        }
      }
    }
    LLVMDisposeMessage(error);
    return ZAN_ERROR;
  }
  if (error)
    LLVMDisposeMessage(error);

  return ZAN_OK;
}

/* ---- output ---- */

static zan_status_t prepare_target_output(zan_irgen_t *g, const char *stage,
                                          bool verify);

zan_status_t zan_irgen_write_ir(zan_irgen_t *g, const char *path) {
  if (zan_bind_target_layout(g, NULL) != ZAN_OK ||
      prepare_target_output(g, "before IR output", true) != ZAN_OK)
    return ZAN_ERROR;
  char *ir = LLVMPrintModuleToString(g->mod);
  char *owned = NULL; /* 自述头 + IR 的拼接缓冲（malloc 释放） */
  /* 产物自述目标特性：编译器自己用 AVX2 档后端出码，但 .ll 文本里没有这层
     信息，消费方（llc 等）按 generic CPU 处理时 256 位向量路径拆不开
     （SplitVectorOperand fatal error，见项目台账 A388）。把 CPU/特性写进
     IR 头部，消费方照抄即可复现编译器的选择。 */
  char header[768];
  int hn = 0;
  if (g->target_cpu[0]) {
    hn = snprintf(
        header, sizeof(header),
        "; zanc-target-cpu: %s\n"
        "; zanc-target-features: %s\n"
        "; zanc: 消费本 IR 必须用同样的目标特性，例如\n"
        "; zanc:   llc -mcpu=%s -mattr=%s -filetype=obj out.ll\n"
        "; zanc: generic CPU 下 256 位向量路径无法合法化（台账 A388）。\n",
        g->target_cpu, g->target_features[0] ? g->target_features : "(default)",
        g->target_cpu, g->target_features[0] ? g->target_features : "+sse2");
    if (hn < 0)
      hn = 0;
    if ((size_t)hn > sizeof(header))
      hn = (int)sizeof(header);
  }
  if (hn > 0) {
    owned = (char *)malloc((size_t)hn + strlen(ir) + 1);
    if (owned) {
      memcpy(owned, header, (size_t)hn);
      strcpy(owned + hn, ir);
    }
  }
  const char *text = owned ? owned : ir;
  zan_status_t rc = ZAN_OK;
  if (!path) {
    if (fputs(text, stdout) < 0)
      rc = ZAN_ERROR;
  } else {
    FILE *f = fopen(path, "w");
    if (!f) {
      rc = ZAN_ERROR;
    } else {
      bool ok = (fputs(text, f) >= 0);
      if (fclose(f) != 0)
        ok = false;
      if (!ok)
        rc = ZAN_ERROR;
    }
  }
  if (owned)
    free(owned);
  LLVMDisposeMessage(ir);
  return rc;
}

int zan_irgen_stub_extern_lib(zan_irgen_t *g, const char *lib, int lib_len) {
  int stubbed = 0;
  for (int i = 0; i < g->extern_fn_count; i++) {
    if ((int)g->extern_fns[i].lib.len != lib_len ||
        memcmp(g->extern_fns[i].lib.str, lib, (size_t)lib_len) != 0)
      continue;
    char nm[256];
    snprintf(nm, sizeof(nm), "%.*s", (int)g->extern_fns[i].name.len,
             g->extern_fns[i].name.str);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, nm);
    if (!fn)
      continue;
    if (LLVMCountBasicBlocks(fn) > 0)
      continue;
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);
    LLVMPositionBuilderAtEnd(b, bb);
    LLVMTypeRef ft = LLVMGlobalGetValueType(fn);
    LLVMTypeRef rt = LLVMGetReturnType(ft);
    switch (LLVMGetTypeKind(rt)) {
    case LLVMVoidTypeKind:
      LLVMBuildRetVoid(b);
      break;
    case LLVMIntegerTypeKind:
      LLVMBuildRet(
          b, LLVMConstInt(
                 rt, LLVMGetIntTypeWidth(rt) >= 64 ? 0 : (unsigned long long)-1,
                 1));
      break;
    case LLVMFloatTypeKind:
    case LLVMDoubleTypeKind:
      LLVMBuildRet(b, LLVMConstReal(rt, 0.0));
      break;
    default:
      LLVMBuildRet(b, LLVMConstNull(rt));
      break;
    }
    LLVMDisposeBuilder(b);
    stubbed++;
  }
  return stubbed;
}

int zan_irgen_drop_extern_lib(zan_irgen_t *g, const char *lib, int lib_len) {
  int kept = 0, dropped = 0;
  for (int li = 0; li < g->extern_lib_count; li++) {
    if ((int)g->extern_libs[li].len == lib_len && lib_len > 0 &&
        memcmp(g->extern_libs[li].str, lib, (size_t)lib_len) == 0) {
      dropped++;
      continue;
    }
    g->extern_libs[kept++] = g->extern_libs[li];
  }
  g->extern_lib_count = kept;
  return dropped;
}

int zan_irgen_prune_extern_libs(zan_irgen_t *g) {
  /* Recompute runtime object demand from post-DCE live uses in the module so
   * unused stdlib/type-map AST declarations do not force dead .o links. */
  g->uses_file_runtime = false;
  g->uses_sync_runtime = false;
  g->uses_socket_async = false;
  g->uses_timer_runtime = false;
  g->uses_embed_api = false;
  g->uses_inflate = false;
  g->uses_decimal_runtime = false;
  g->has_async_work = false;
  LLVMValueRef legacy_pump = LLVMGetNamedFunction(g->mod, "zan_io_pump");
  bool legacy_pump_dead = legacy_pump && LLVMGetFirstUse(legacy_pump) == NULL;
  if (legacy_pump_dead && (!g->emit_lib || g->emit_shared)) {
    LLVMDeleteFunction(legacy_pump);
    legacy_pump = NULL;
    legacy_pump_dead = false;
  }
  for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
       fn = LLVMGetNextFunction(fn)) {
    size_t nlen = 0;
    const char *nm = LLVMGetValueName2(fn, &nlen);
    if (!nm || !nlen)
      continue;
    bool used = (fn == legacy_pump) ? !legacy_pump_dead
                : (legacy_pump_dead && strcmp(nm, "zan_io_pump_timeout") == 0)
                    ? fn_has_use_outside(fn, legacy_pump, NULL)
                    : (LLVMGetFirstUse(fn) != NULL);
    if (!used)
      continue;
    if (strncmp(nm, "zan_file_", 9) == 0 || strncmp(nm, "zan_pkg_", 8) == 0) {
      g->uses_file_runtime = true;
    } else if (strncmp(nm, "zan_atomic_int_", 15) == 0 ||
               strncmp(nm, "zan_dir_list_", 13) == 0 ||
               strncmp(nm, "zan_dispatch_", 13) == 0 ||
               strncmp(nm, "zan_exe_dir_", 12) == 0 ||
               strncmp(nm, "zan_mmap_", 9) == 0 ||
               strncmp(nm, "zan_monitor_", 12) == 0 ||
               strncmp(nm, "zan_monotonic_", 14) == 0 ||
               strncmp(nm, "zan_plat_", 9) == 0 ||
               strncmp(nm, "zan_proc_", 9) == 0 ||
               strncmp(nm, "zan_shared_", 11) == 0 ||
               strncmp(nm, "zan_stopwatch_", 14) == 0 ||
               strncmp(nm, "zan_thread_", 11) == 0 ||
               strncmp(nm, "zan_ui_thread_", 14) == 0 ||
               strcmp(nm, "zan_obj_slot_is_string") == 0) {
      g->uses_sync_runtime = true;
    } else if (strncmp(nm, "zan_io_", 7) == 0 ||
               strncmp(nm, "zan_gate_", 9) == 0 ||
               strcmp(nm, "zan_cpu_count") == 0 ||
               strcmp(nm, "zan_crypto_cert_encoded") == 0 ||
               strcmp(nm, "zan_rt_eprint_line") == 0) {
      g->uses_socket_async = true;
    } else if (strncmp(nm, "zan_co_", 7) == 0 ||
               strncmp(nm, "__zan_co_", 9) == 0 ||
               strncmp(nm, "zan_rt_blocking_", 16) == 0) {
      g->has_async_work = true;
    } else if (strncmp(nm, "zan_timer_", 10) == 0) {
      g->uses_timer_runtime = true;
    } else if (strcmp(nm, "zan_embed_decode") == 0 ||
               strcmp(nm, "zan_embed_rawlen") == 0 ||
               strcmp(nm, "zan_embed_uncrypt") == 0 ||
               strncmp(nm, "tinfl_", 6) == 0) {
      g->uses_inflate = true;
    } else if (strncmp(nm, "zan_embed_", 10) == 0) {
      g->uses_embed_api = true;
    } else if (strncmp(nm, "__zan_decimal_", 14) == 0 ||
               strncmp(nm, "zan_decimal_", 12) == 0) {
      g->uses_decimal_runtime = true;
    }
  }

  int kept = 0, dropped = 0;
  for (int li = 0; li < g->extern_lib_count; li++) {
    const char *lib = g->extern_libs[li].str;
    int lib_len = (int)g->extern_libs[li].len;
    int live = 0;
    for (int i = 0; i < g->extern_fn_count && !live; i++) {
      if ((int)g->extern_fns[i].lib.len != lib_len ||
          memcmp(g->extern_fns[i].lib.str, lib, (size_t)lib_len) != 0)
        continue;
      char nm[256];
      snprintf(nm, sizeof(nm), "%.*s", (int)g->extern_fns[i].name.len,
               g->extern_fns[i].name.str);
      if (LLVMGetNamedFunction(g->mod, nm))
        live = 1;
    }
    if (!live) {
      int has_fns = 0;
      for (int i = 0; i < g->extern_fn_count && !has_fns; i++) {
        if ((int)g->extern_fns[i].lib.len == lib_len &&
            memcmp(g->extern_fns[i].lib.str, lib, (size_t)lib_len) == 0)
          has_fns = 1;
      }
      if (!has_fns)
        live = 1;
    }
    if (live) {
      g->extern_libs[kept++] = g->extern_libs[li];
    } else {
      dropped++;
    }
  }
  g->extern_lib_count = kept;
  return dropped;
}

bool zan_irgen_imports_ios_driver(const zan_irgen_t *g, const char *driver) {
  if (!g->target_is_ios)
    return false;
  size_t driver_len = strlen(driver);
  for (int li = 0; li < g->extern_lib_count; li++) {
    zan_istr_t lib = g->extern_libs[li];
    if (lib.len > 3 && memcmp(lib.str, "lib", 3) == 0) {
      lib.str += 3;
      lib.len -= 3;
    }
    if (lib.len == driver_len && memcmp(lib.str, driver, driver_len) == 0)
      return true;
  }
  return false;
}

bool zan_irgen_defines_prefix(zan_irgen_t *g, const char *prefix) {
  if (!g || !prefix)
    return false;
  for (int i = 0; i < g->prefix_cache_count; i++) {
    if (strcmp(g->prefix_cache[i], prefix) == 0)
      return g->prefix_cache_val[i];
  }
  if (!g->mod)
    return false;
  size_t plen = strlen(prefix);
  if (!plen)
    return false;
  bool found = false;
  for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
       fn = LLVMGetNextFunction(fn)) {
    if (LLVMCountBasicBlocks(fn) == 0)
      continue;
    size_t nlen = 0;
    const char *nm = LLVMGetValueName2(fn, &nlen);
    if (nm && nlen >= plen && memcmp(nm, prefix, plen) == 0) {
      found = true;
      break;
    }
  }
  if (g->prefix_cache_count < 128) {
    snprintf(g->prefix_cache[g->prefix_cache_count], sizeof(g->prefix_cache[0]),
             "%s", prefix);
    g->prefix_cache_val[g->prefix_cache_count] = found;
    g->prefix_cache_count++;
  }
  return found;
}

static void w32_build_adapter_into(zan_irgen_t *g, LLVMValueRef fn,
                                   LLVMValueRef real, LLVMTypeRef lft) {
  LLVMTypeRef src_ft = LLVMGlobalGetValueType(fn);
  unsigned nparams = LLVMCountParamTypes(src_ft);
  if (LLVMCountParamTypes(lft) != nparams)
    return;
  size_t slots = nparams ? nparams : 1;
  LLVMTypeRef *sps = zan_arena_alloc(g->arena, slots * sizeof(*sps));
  LLVMTypeRef *lps = zan_arena_alloc(g->arena, slots * sizeof(*lps));
  LLVMGetParamTypes(src_ft, sps);
  LLVMGetParamTypes(lft, lps);
  LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);
  LLVMPositionBuilderAtEnd(b, bb);
  LLVMValueRef *args = zan_arena_alloc(g->arena, slots * sizeof(*args));
  for (unsigned p = 0; p < nparams; p++) {
    LLVMValueRef a = LLVMGetParam(fn, p);
    LLVMTypeKind sk = LLVMGetTypeKind(sps[p]);
    LLVMTypeKind lk = LLVMGetTypeKind(lps[p]);
    if (sps[p] == lps[p]) {
      args[p] = a;
    } else if (sk == LLVMIntegerTypeKind && lk == LLVMPointerTypeKind) {
      args[p] = LLVMBuildIntToPtr(b, a, lps[p], "");
    } else if (sk == LLVMPointerTypeKind && lk == LLVMIntegerTypeKind) {
      args[p] = LLVMBuildPtrToInt(b, a, lps[p], "");
    } else if (sk == LLVMIntegerTypeKind && lk == LLVMIntegerTypeKind) {
      args[p] = LLVMBuildIntCast2(b, a, lps[p], 1, "");
    } else if (sk == LLVMPointerTypeKind && lk == LLVMPointerTypeKind) {
      args[p] = LLVMBuildPointerCast(b, a, lps[p], "");
    } else {
      args[p] = a;
    }
  }
  LLVMValueRef rv = zan_call2(b, lft, real, args, nparams, "");
  LLVMTypeRef srt = LLVMGetReturnType(src_ft);
  LLVMTypeRef lrt = LLVMGetReturnType(lft);
  if (LLVMGetTypeKind(srt) == LLVMVoidTypeKind) {
    LLVMBuildRetVoid(b);
  } else if (srt == lrt) {
    LLVMBuildRet(b, rv);
  } else if (LLVMGetTypeKind(lrt) == LLVMVoidTypeKind) {
    LLVMBuildRet(b, LLVMConstNull(srt));
  } else if (LLVMGetTypeKind(lrt) == LLVMPointerTypeKind &&
             LLVMGetTypeKind(srt) == LLVMIntegerTypeKind) {
    LLVMBuildRet(b, LLVMBuildPtrToInt(b, rv, srt, ""));
  } else if (LLVMGetTypeKind(lrt) == LLVMIntegerTypeKind &&
             LLVMGetTypeKind(srt) == LLVMIntegerTypeKind) {
    LLVMBuildRet(b, LLVMBuildIntCast2(b, rv, srt, 1, ""));
  } else if (LLVMGetTypeKind(lrt) == LLVMIntegerTypeKind &&
             LLVMGetTypeKind(srt) == LLVMPointerTypeKind) {
    LLVMBuildRet(b, LLVMBuildIntToPtr(b, rv, srt, ""));
  } else {
    LLVMBuildRet(b, rv);
  }
  LLVMDisposeBuilder(b);
}

static LLVMValueRef w32_build_adapter(zan_irgen_t *g, const char *name,
                                      LLVMTypeRef src_ft, LLVMValueRef real,
                                      LLVMTypeRef lft) {
  if (LLVMCountParamTypes(src_ft) != LLVMCountParamTypes(lft))
    return NULL;
  LLVMValueRef fn = LLVMAddFunction(g->mod, name, src_ft);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  w32_build_adapter_into(g, fn, real, lft);
  return fn;
}

/* 初始化every target family the build links (see CMakeLists */
void zan_irgen_init_llvm_targets(void) {
  LLVMInitializeX86TargetInfo();
  LLVMInitializeX86Target();
  LLVMInitializeX86TargetMC();
  LLVMInitializeX86AsmParser();
  LLVMInitializeX86AsmPrinter();

  LLVMInitializeAArch64TargetInfo();
  LLVMInitializeAArch64Target();
  LLVMInitializeAArch64TargetMC();
  LLVMInitializeAArch64AsmParser();
  LLVMInitializeAArch64AsmPrinter();

#ifdef ZAN_HAVE_LLVM_ARM
  LLVMInitializeARMTargetInfo();
  LLVMInitializeARMTarget();
  LLVMInitializeARMTargetMC();
  LLVMInitializeARMAsmParser();
  LLVMInitializeARMAsmPrinter();
#endif

#ifdef ZAN_HAVE_LLVM_WEBASSEMBLY
  LLVMInitializeWebAssemblyTargetInfo();
  LLVMInitializeWebAssemblyTarget();
  LLVMInitializeWebAssemblyTargetMC();
  LLVMInitializeWebAssemblyAsmParser();
  LLVMInitializeWebAssemblyAsmPrinter();
#endif

#ifdef ZAN_HAVE_LLVM_RISCV
  LLVMInitializeRISCVTargetInfo();
  LLVMInitializeRISCVTarget();
  LLVMInitializeRISCVTargetMC();
  LLVMInitializeRISCVAsmParser();
  LLVMInitializeRISCVAsmPrinter();
#endif
}
zan_status_t zan_bind_target_layout(zan_irgen_t *g,
                                    LLVMTargetMachineRef *out_tm) {
  char *triple;
  if (g->target_triple[0]) {
    triple = LLVMCreateMessage(g->target_triple);
  } else {
    triple = LLVMGetDefaultTargetTriple();
#ifdef _WIN32
    /* 发射GNU-ABI (MinGW) objects so the produced code links against the
     * bundled ld */
    {
      const char *dash = strchr(triple, '-');
      size_t archlen = dash ? (size_t)(dash - triple) : strlen(triple);
      char gnu[128];
      if (archlen > sizeof(gnu) - 20)
        archlen = sizeof(gnu) - 20;
      memcpy(gnu, triple, archlen);
      snprintf(gnu + archlen, sizeof(gnu) - archlen, "-w64-windows-gnu");
      LLVMDisposeMessage(triple);
      triple = LLVMCreateMessage(gnu);
    }
#endif
  }

  LLVMTargetRef target;
  char *error = NULL;

  if (LLVMGetTargetFromTriple(triple, &target, &error)) {
    zan_diag_emit_code(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0), 3091,
                       "failed to get target: %s", error);
    LLVMDisposeMessage(error);
    LLVMDisposeMessage(triple);
    return ZAN_ERROR;
  }

  const char *tm_cpu = "generic";
  const char *tm_features = "";
  bool wasm = strncmp(triple, "wasm", 4) == 0;
  if (wasm) {
    tm_features = "+exception-handling,+reference-types";
  } else if (strncmp(triple, "riscv64", 7) == 0) {
    tm_cpu = "generic-rv64";
    tm_features = "+m,+a,+f,+d,+c";
    if (!LLVMGetModuleFlag(g->mod, "target-abi", 10))
      LLVMAddModuleFlag(
          g->mod, LLVMModuleFlagBehaviorError, "target-abi",
          strlen("target-abi"),
          LLVMValueAsMetadata(LLVMMDStringInContext(g->ctx, "lp64d", 5)));
  } else if (strncmp(triple, "riscv32", 7) == 0) {
    tm_cpu = "generic-rv32";
    tm_features = "+m,+c";
    if (!LLVMGetModuleFlag(g->mod, "target-abi", 10))
      LLVMAddModuleFlag(
          g->mod, LLVMModuleFlagBehaviorError, "target-abi",
          strlen("target-abi"),
          LLVMValueAsMetadata(LLVMMDStringInContext(g->ctx, "ilp32", 5)));
  } else if (strncmp(triple, "x86_64", 6) == 0) {
    tm_cpu = "x86-64";
    /* Android's x86_64 ABI matches the NDK baseline: SSE4.2, POPCNT,
       CX16, but no AVX/BMI/FMA/AES guarantee. Enabling AVX globally
       vectorizes ordinary initialization before any CPU feature check. */
    if (g->cpu_profile && strcmp(g->cpu_profile, "baseline") == 0)
      tm_features = "+sse2,-sse3,-ssse3,-sse4.1,-sse4.2,-crc32,-aes,-avx,-avx2,"
                    "-fma,-bmi,-bmi2,-popcnt";
    else if (g->cpu_profile && strcmp(g->cpu_profile, "avx2") == 0)
      tm_features =
          "+sse3,+ssse3,+sse4.1,+sse4.2,+crc32,+aes,+avx,+avx2,+fma,+bmi";
    else
      tm_features =
          strstr(triple, "android")
              ? "+sse4.2,+crc32,+popcnt,+cx16"
              : "+sse3,+ssse3,+sse4.1,+sse4.2,+crc32,+aes,+avx,+avx2,+fma,+bmi";
  } else if (strncmp(triple, "aarch64", 7) == 0 ||
             strncmp(triple, "arm64", 5) == 0) {
    tm_features = "+aes";
  }
  LLVMCodeGenOptLevel cg =
      g->fast_codegen
          ? LLVMCodeGenLevelNone
          : (g->publish_mode ? LLVMCodeGenLevelAggressive
                             : LLVMCodeGenLevelDefault);
  /* 后端优化档：--codegen-level 显式优先，未给时回退 ZAN_CODEGEN_LEVEL env。 */
  int clvl = g->codegen_level;
  if (clvl < 0) {
    const char *cgenv = getenv("ZAN_CODEGEN_LEVEL");
    if (cgenv && *cgenv)
      clvl = atoi(cgenv);
    if (clvl < 0 || clvl > 3)
      clvl = -1;
  }
  if (clvl == 0)
    cg = LLVMCodeGenLevelNone;
  else if (clvl == 1)
    cg = LLVMCodeGenLevelLess;
  else if (clvl == 2)
    cg = LLVMCodeGenLevelDefault;
  else if (clvl == 3)
    cg = LLVMCodeGenLevelAggressive;
  /* 记下后端选用的 CPU/特性：--emit-ir 的产物要靠它自述目标特性，
     否则消费方（llc 等）用 generic CPU 拿到 256 位向量路径会合法化失败。 */
  snprintf(g->target_cpu, sizeof(g->target_cpu), "%s", tm_cpu);
  snprintf(g->target_features, sizeof(g->target_features), "%s", tm_features);
  /* Android's API 28 floor predates ELF TLS. Match the NDK's codegen.
   * Wasm catchpads require the target's native exception model. */
  bool emulated_tls = strstr(triple, "android") != NULL;
  LLVMTargetMachineRef tm =
      (emulated_tls || wasm)
          ? zan_create_target_machine_with_options(
                target, triple, tm_cpu, tm_features, cg, emulated_tls, wasm)
          : LLVMCreateTargetMachine(target, triple, tm_cpu, tm_features, cg,
                                    LLVMRelocPIC, LLVMCodeModelDefault);
  if (!tm) {
    zan_diag_emit_code(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0), 3091,
                       "failed to create target machine for '%s'", triple);
    LLVMDisposeMessage(triple);
    return ZAN_ERROR;
  }

  LLVMSetTarget(g->mod, triple);
  LLVMTargetDataRef dl = LLVMCreateTargetDataLayout(tm);
  char *dl_str = LLVMCopyStringRepOfTargetData(dl);
  LLVMSetDataLayout(g->mod, dl_str);
  LLVMDisposeMessage(dl_str);
  LLVMDisposeTargetData(dl);
  LLVMDisposeMessage(triple);

  if (out_tm)
    *out_tm = tm;
  else
    LLVMDisposeTargetMachine(tm);
  return ZAN_OK;
}

void zan_irgen_bind_target(zan_irgen_t *g) {
  zan_irgen_init_llvm_targets();
  zan_bind_target_layout(g, NULL);
}

zan_status_t zan_irgen_create_target_machine(zan_irgen_t *g,
                                             LLVMTargetMachineRef *out_tm) {
  zan_irgen_init_llvm_targets();
  return zan_bind_target_layout(g, out_tm);
}

/* Apple --fast-alloc: point every allocation call this module makes at the
 * small-object allocator. Runs on the main and every shard module right
 * before object emission, i.e. after the LLVM pipeline has had its look at
 * the libc names (heap-to-stack, malloc/free pair elision). Only bodiless
 * declarations are renamed; rt_mem.c's zone makes any pointer that still
 * crosses to libc (C runtime, drivers, cached objects) safe either way. */
static void zan_irgen_route_fast_alloc(zan_irgen_t *g, LLVMModuleRef mod) {
    if (!g->route_fast_alloc) return;
    static const char *const names[][2] = {
        { "malloc", "zan_mem_malloc" }, { "free", "zan_mem_free" },
        { "calloc", "zan_mem_calloc" }, { "realloc", "zan_mem_realloc" },
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        LLVMValueRef fn = LLVMGetNamedFunction(mod, names[i][0]);
        if (!fn || !LLVMIsDeclaration(fn)) continue;
        LLVMValueRef have = LLVMGetNamedFunction(mod, names[i][1]);
        if (have) {
            /* Both spellings declared (a DllImport of zan_mem_*): fold. */
            LLVMReplaceAllUsesWith(fn, have);
            LLVMDeleteFunction(fn);
            continue;
        }
        LLVMSetValueName2(fn, names[i][1], strlen(names[i][1]));
    }
}

void zan_irgen_prepare_object_sections(zan_irgen_t *g, LLVMModuleRef mod) {
  zan_irgen_route_fast_alloc(g, mod);
  /* Main and shard objects need the same GC granularity. COFF '$' suffixes
   * merge into ordinary output sections. Preserve special runtime sections,
   * TLS and weak/ODR groups when making strong definitions discardable. */
  bool need_fn_sec = (g->obfuscate_strings || g->publish_mode) &&
                     !g->target_is_macos && !g->target_is_ios &&
                     !g->target_is_wasm;
  bool coff_publish_data = g->publish_mode && g->target_is_windows;
  bool need_gv_sec = (g->obfuscate_strings || g->publish_mode) &&
                     !g->target_is_macos && !g->target_is_ios &&
                     !g->target_is_wasm;
  if (need_fn_sec) {
    for (LLVMValueRef fn = LLVMGetFirstFunction(mod); fn;
         fn = LLVMGetNextFunction(fn)) {
      if (LLVMIsDeclaration(fn) || LLVMGetSection(fn))
        continue;
      size_t nlen = 0;
      const char *nm = LLVMGetValueName2(fn, &nlen);
      if (!nm || !nlen || nlen > 200)
        continue;
      LLVMLinkage linkage = LLVMGetLinkage(fn);
      if (g->target_is_windows && !LLVMGetComdat(fn) &&
          (linkage == LLVMExternalLinkage || linkage == LLVMInternalLinkage ||
           linkage == LLVMPrivateLinkage)) {
        /* COFF GC needs COMDAT and '$' names for MinGW unwind sections.
         * NODUPLICATES preserves strong-definition errors. Keep weak
         * fallbacks ungrouped so the native runtime can override them.
         * Explicit COFF sections need internal rather than private keys. */
        if (LLVMGetLinkage(fn) == LLVMPrivateLinkage)
          LLVMSetLinkage(fn, LLVMInternalLinkage);
        LLVMComdatRef group = LLVMGetOrInsertComdat(mod, nm);
        LLVMSetComdatSelectionKind(group, LLVMNoDeduplicateComdatSelectionKind);
        LLVMSetComdat(fn, group);
      }
      char sec[256];
      snprintf(sec, sizeof(sec), ".text%s%s", g->target_is_windows ? "$" : ".",
               nm);
      LLVMSetSection(fn, sec);
    }
  }
  if (need_gv_sec) {
    int coff_obj =
        g->target_triple[0] ? strstr(g->target_triple, "windows-gnu") != NULL :
#ifdef _WIN32
                            1;
#else
                            0;
#endif
    const char *dsep = coff_obj ? "$" : ".";
    for (LLVMValueRef gv = LLVMGetFirstGlobal(mod); gv;
         gv = LLVMGetNextGlobal(gv)) {
      if (LLVMGetSection(gv))
        continue;
      if (!LLVMGetInitializer(gv))
        continue;
      if (LLVMIsThreadLocal(gv))
        continue;
      size_t nlen = 0;
      const char *nm = LLVMGetValueName2(gv, &nlen);
      if (!nm || !nlen || nlen > 200)
        continue;
      if (strncmp(nm, "llvm.", 5) == 0)
        continue;
      if (strncmp(nm, "rterr", 5) == 0)
        continue;
      if (coff_publish_data) {
        LLVMLinkage linkage = LLVMGetLinkage(gv);
        if (LLVMGetComdat(gv) ||
            (linkage != LLVMExternalLinkage && linkage != LLVMInternalLinkage &&
             linkage != LLVMPrivateLinkage))
          continue;
        if (linkage == LLVMPrivateLinkage)
          LLVMSetLinkage(gv, LLVMInternalLinkage);
        LLVMComdatRef group = LLVMGetOrInsertComdat(mod, nm);
        LLVMSetComdatSelectionKind(group, LLVMNoDeduplicateComdatSelectionKind);
        LLVMSetComdat(gv, group);
      }
      char sec[260];
      snprintf(sec, sizeof(sec), "%s%s%s",
               LLVMIsGlobalConstant(gv) ? (coff_obj ? ".rdata" : ".rodata")
                                        : ".data",
               dsep, nm);
      LLVMSetSection(gv, sec);
    }
  }
}

static zan_status_t prepare_target_output(zan_irgen_t *g, const char *stage,
                                          bool verify) {
  abi_pending_report(g);
  if (zan_diag_has_errors(g->diag))
    return ZAN_ERROR;
  bool w32_triple = strncmp(g->target_triple, "wasm32", 6) == 0;
  bool rv32_triple = strncmp(g->target_triple, "riscv32", 7) == 0;
  if (w32_triple || rv32_triple) {
    bool wasi = w32_triple;
    if (wasi) {
      LLVMValueRef mainf = LLVMGetNamedFunction(g->mod, "main");
      if (mainf && LLVMCountBasicBlocks(mainf) > 0)
        LLVMSetValueName2(mainf, "__main_argc_argv",
                          strlen("__main_argc_argv"));
    }
    {
      LLVMValueRef f = LLVMGetNamedFunction(g->mod, "snprintf");
      if (f && LLVMCountBasicBlocks(f) == 0)
        LLVMSetValueName2(f, "zan_w32_snprintf", strlen("zan_w32_snprintf"));
    }
    static const struct {
      const char *name;
      const char *sig;
    } w32adapt[] = {{"malloc", "ps"},
                    {"calloc", "pss"},
                    {"realloc", "pps"},
                    {"free", "vp"},
                    {"strlen", "sp"},
                    {"memcpy", "ppps"},
                    {"memset", "ppis"},
                    {"memcmp", "ipps"},
                    {"strcat", "ppp"},
                    {"strcpy", "ppp"},
                    {"strncpy", "ppps"},
                    {"strcmp", "ipp"},
                    {"strncmp", "ipps"},
                    {"strrchr", "ppi"},
                    {"strstr", "ppp"},
                    {"atoi", "ip"},
                    {"fopen", "ppp"},
                    {"fclose", "ip"},
                    {"fgetc", "ip"},
                    {"fputc", "iip"},
                    {"fputs", "ipp"},
                    {"fgets", "ppip"},
                    {"fread", "spssp"},
                    {"fwrite", "spssp"},
                    {"fseek", "ipii"},
                    {"ftell", "ip"},
                    {"ftello", "jp"},
                    {"__zan_co_frame_alloc", "ps"},
                    {"fflush", "ip"},
                    {"remove", "ip"},
                    {"rename", "ipp"},
                    {"chdir", "ip"},
                    {"getcwd", "pps"},
                    {"mkdir", "ipi"},
                    {"rmdir", "ip"},
                    {"opendir", "pp"},
                    {"readdir", "pp"},
                    {"closedir", "ip"},
                    {"time", "jp"},
                    {"poll", "ipii"},
                    /* NativeMemory */
                    {"strcspn", "ipp"},
                    {"pthread_mutex_init", "iii"},
                    {"pthread_mutex_lock", "ii"},
                    {"pthread_mutex_unlock", "ii"},
                    {"pthread_mutex_destroy", "ii"},
                    {"setvbuf", "ipipi"},
                    {"dlopen", "pip"},
                    {"dlsym", "ppp"},
                    {"dlclose", "ip"},
                    {"zan_file_fopen", "ppp"},
                    {"zan_pkg_fopen", "ppp"},
                    {"zan_gui_draw_polyline", "vppiii"},
                    {"zan_gui_draw_polyline_fx", "vppiii"},
                    {"zan_gui_draw_polybatch", "vippiii"},
                    {"zan_gui_draw_polybatch_fx", "vippiii"},
                    {"zan_gui_fill_rects", "vipi"},
                    {"zan_gui_fill_circles", "vipi"},
                    {"zan_gui_fill_radials", "vipi"},
                    {"zan_gui_get_pixels", "pi"},
                    {"zan_gui_blit_pixels", "vipiiiiiiii"},
                    {"zan_image_get", "pp"},
                    {"zan_image_register_argb", "ippiii"},
                    {"zan_image_copy_argb", "ippi"},
                    {"zan_image_encode_argb", "ppiiiiip"},
                    {"zan_image_free_bytes", "vp"},
                    {"zan_image_resize_argb", "ipiipiii"},
                    {"zan_image_rotate_argb", "ipiipi"},
                    {"zan_image_color_adjust_argb", "vpiiii"},
                    {"zan_image_box_blur_argb", "ipiiii"},
                    {"zan_image_sharpen_argb", "ipiii"},
                    {"zan_image_blit_argb", "ipiipiiiii"},
                    {"zan_image_anim_open_mem", "ppi"},
                    {"zan_image_anim_open_file", "pp"},
                    {"zan_image_anim_width", "ip"},
                    {"zan_image_anim_height", "ip"},
                    {"zan_image_anim_frame_count", "ip"},
                    {"zan_image_anim_frame_delay_ms", "ipi"},
                    {"zan_image_anim_copy_frame", "ipipi"},
                    {"zan_image_anim_close", "vp"},
                    {"zan_audio_stream_write", "ijpi"},
                    {"zan_video_open", "pp"},
                    {"zan_video_close", "vp"},
                    {"zan_video_width", "ip"},
                    {"zan_video_height", "ip"},
                    {"zan_video_duration_ms", "jp"},
                    {"zan_video_fps_num", "ip"},
                    {"zan_video_fps_den", "ip"},
                    {"zan_video_bitrate", "jp"},
                    {"zan_video_frame_count", "jp"},
                    {"zan_video_has_video", "ip"},
                    {"zan_video_has_audio", "ip"},
                    {"zan_video_audio_sample_rate", "ip"},
                    {"zan_video_audio_channels", "ip"},
                    {"zan_video_video_codec", "pp"},
                    {"zan_video_audio_codec", "pp"},
                    {"zan_video_step", "ip"},
                    {"zan_video_copy_video_argb", "ippiii"},
                    {"zan_video_video_pts_ms", "jp"},
                    {"zan_video_audio_pcm_ptr", "pp"},
                    {"zan_video_audio_samples", "ip"},
                    {"zan_video_audio_pts_ms", "jp"},
                    {"zan_video_seek_ms", "ipj"},
                    {"zan_video_extract_frame_argb", "ipjpii"},
                    {"zan_game_sprite_batch", "viipi"},
                    {"zan_game_mesh_create", "iipipi"},
                    {"zan_game_mesh_create_ex", "iipipii"},
                    {"zan_game_draw3d", "iiipip"},
                    {"zan_game_draw3d_batch_begin", "ii"},
                    {"zan_game_draw3d_batch", "iiippi"},
                    {"zan_game_draw3d_batch_end", "ii"},
                    {"zan_gui_clear_hit_guards", "ii"},
                    {"zan_gui_add_hit_guard", "iiiiii"},
                    {"zan_gui_text_stat_read", "ji"},
                    {"zan_gui_guard_call", "iii"},
                    /* NativeMemory */
                    {"memmove", "ppps"},
                    {"memchr", "ppis"},
                    {NULL, NULL}};
    LLVMTypeRef w_i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef w_i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef w_ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = 0; w32adapt[i].name; i++) {
      LLVMValueRef decl = LLVMGetNamedFunction(g->mod, w32adapt[i].name);
      if (!decl || LLVMCountBasicBlocks(decl) > 0)
        continue;
      LLVMTypeRef dft = LLVMGlobalGetValueType(decl);
      if (LLVMIsFunctionVarArg(dft))
        continue;
      const char *sig = w32adapt[i].sig;
      int nparams = (int)strlen(sig) - 1;
      if ((int)LLVMCountParamTypes(dft) != nparams)
        continue;
      size_t slots = nparams ? (size_t)nparams : 1;
      LLVMTypeRef *lps = zan_arena_alloc(g->arena, slots * sizeof(*lps));
      for (int p = 0; p < nparams; p++) {
        char c = sig[p + 1];
        lps[p] = (c == 'p') ? w_ptr : (c == 'j') ? w_i64 : w_i32;
      }
      char rc = sig[0];
      LLVMTypeRef lrt = (rc == 'v')   ? LLVMVoidTypeInContext(g->ctx)
                        : (rc == 'p') ? w_ptr
                        : (rc == 'j') ? w_i64
                                      : w_i32;
      LLVMTypeRef lft = LLVMFunctionType(lrt, lps, (unsigned)nparams, 0);
      char an[80];
      snprintf(an, sizeof(an), "__zan_w32ir_%s", w32adapt[i].name);
      LLVMSetValueName2(decl, an, strlen(an));
      LLVMValueRef real = LLVMAddFunction(g->mod, w32adapt[i].name, lft);
      LLVMTypeRef cts[8];
      LLVMValueRef cad[8];
      int ncts = 0;
      LLVMUseRef use = LLVMGetFirstUse(decl);
      while (use) {
        LLVMUseRef next = LLVMGetNextUse(use);
        LLVMValueRef user = LLVMGetUser(use);
        if ((LLVMIsACallInst(user) || LLVMIsAInvokeInst(user)) &&
            LLVMGetCalledValue(user) == decl) {
          LLVMTypeRef cft = LLVMGetCalledFunctionType(user);
          if (cft == lft) {
            LLVMSetOperand(user, LLVMGetNumOperands(user) - 1, real);
          } else if (!LLVMIsFunctionVarArg(cft) &&
                     (int)LLVMCountParamTypes(cft) == nparams) {
            LLVMValueRef ad = NULL;
            for (int k = 0; k < ncts; k++) {
              if (cts[k] == cft) {
                ad = cad[k];
                break;
              }
            }
            if (!ad) {
              char nm[96];
              snprintf(nm, sizeof(nm), "%s.v%d", an, ncts);
              ad = w32_build_adapter(g, nm, cft, real, lft);
              if (ad && ncts < 8) {
                cts[ncts] = cft;
                cad[ncts] = ad;
                ncts++;
              }
            }
            if (ad)
              LLVMSetOperand(user, LLVMGetNumOperands(user) - 1, ad);
          }
        }
        use = next;
      }
      if (LLVMGetFirstUse(decl) && dft != lft) {
        w32_build_adapter_into(g, decl, real, lft);
        LLVMSetLinkage(decl, LLVMInternalLinkage);
      }
    }
  }
  /* Only the UIKit driver supplies its own main and calls Zan's entry. */
  if (zan_irgen_imports_ios_driver(g, "zan_gui") ||
      zan_irgen_imports_ios_driver(g, "zan_game")) {
    LLVMValueRef mainf = LLVMGetNamedFunction(g->mod, "main");
    if (mainf && LLVMCountBasicBlocks(mainf) > 0) {
      LLVMSetValueName2(mainf, "zan_user_main", strlen("zan_user_main"));
    }
  }
  if (zan_opt_validate_cpu(g, g->mod) != ZAN_OK ||
      (verify && zan_opt_verify_module(g->mod, stage) != ZAN_OK))
    return ZAN_ERROR;
  return ZAN_OK;
}

/* Split object emission when the optimized module is large enough that a
 * single-threaded codegen pass dominates time and memory. ZAN_CODEGEN_SPLIT=0
 * disables it; =N forces N partitions. */
static bool zan_codegen_split_plan(zan_irgen_t *g, int *parts, int *threads) {
    const char *env = getenv("ZAN_CODEGEN_SPLIT");
    long forced = env && *env ? strtol(env, NULL, 10) : -1;
    if (forced == 0 || !g->split_codegen_ok || g->emit_debug || g->emit_lib) return false;
    long insns = 0;
    for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn; fn = LLVMGetNextFunction(fn))
        for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn); bb; bb = LLVMGetNextBasicBlock(bb))
            for (LLVMValueRef in = LLVMGetFirstInstruction(bb); in; in = LLVMGetNextInstruction(in))
                insns++;
    long cpus = 1;
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    cpus = (long)si.dwNumberOfProcessors;
#else
    cpus = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    if (cpus < 1) cpus = 1;
    if (cpus > 16) cpus = 16;
    if (forced < 0 && (insns < 200000 || cpus < 2)) return false;
    long n = forced > 0 ? forced : insns / 100000;
    if (n < 2) n = 2;
    if (n > 16) n = 16; /* keeps the link line short (cmd.exe caps at 8191) */
    *parts = (int)n;
    *threads = (int)(cpus < n ? cpus : n);
    return true;
}

static void prepare_sections_cb(void *ctx, LLVMModuleRef part) {
    zan_irgen_prepare_object_sections((zan_irgen_t *)ctx, part);
}

static int zan_online_cpus(void) {
    long cpus = 1;
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    cpus = (long)si.dwNumberOfProcessors;
#else
    cpus = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    if (cpus < 1) cpus = 1;
    if (cpus > 16) cpus = 16;
    return (int)cpus;
}

int zan_irgen_partition(zan_irgen_t *g, int opt_level, const char *obj_base) {
    if (zan_diag_has_errors(g->diag)) return -1;
    LLVMTargetMachineRef tm;
    if (zan_bind_target_layout(g, &tm) != ZAN_OK) return -1;
    zan_part_opts_t o;
    memset(&o, 0, sizeof(o));
    o.fn_names = (const char *const *)g->origin_fn_names;
    o.fn_units = g->origin_fn_unit;
    o.fn_count = g->origin_count;
    o.units = (const char *const *)g->origin_units;
    o.unit_count = g->origin_unit_count;
    o.optimize = zan_opt_run_passes_on_unit;
    o.prelude = zan_opt_unit_prelude;
    o.level = opt_level;
    o.prepare = prepare_sections_cb;
    o.prepare_ctx = g;
    o.threads = zan_online_cpus();
    o.obj_base = obj_base;
    zan_part_result_t r;
    char *error = NULL;
    int rc = zan_part_run(g->mod, tm, &o, &r, &error);
    LLVMDisposeTargetMachine(tm);
    g->part_stats.units = r.count;
    g->part_stats.hit = r.units_hit;
    g->part_stats.miss = r.units_miss;
    g->part_stats.moved_insns = r.moved_insns;
    g->part_stats.residual_insns = r.residual_insns;
    g->part_stats.ms_split = r.ms_split;
    g->part_stats.ms_compile = r.ms_compile;
    if (rc < 0) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0), 3092,
                           "partitioned backend failed: %s", error ? error : "?");
        free(error);
        return -1;
    }
    if (rc > 0) {
        g->part_objs = r.objs;
        g->part_obj_count = r.count;
    }
    return rc;
}

zan_status_t zan_irgen_write_obj(zan_irgen_t *g, const char *path) {
  LLVMTargetMachineRef tm;
  if (zan_irgen_create_target_machine(g, &tm) != ZAN_OK)
    return ZAN_ERROR;

  if (prepare_target_output(g, "before object output", false) != ZAN_OK) {
    LLVMDisposeTargetMachine(tm);
    return ZAN_ERROR;
  }
  /* The finish half of the optimizer, if zan_optimize left it pending, and
   * section preparation (which must see the final IR: fast-alloc routing
   * renames libc allocators the pipeline reasons about) run per partition
   * when the module splits, else here on the whole module. */
  zan_split_opts_t sopts;
  memset(&sopts, 0, sizeof(sopts));
  sopts.finish_level = g->opt_finish_level;
  sopts.vectorize = g->opt_finish_vectorize;
  sopts.prepare = prepare_sections_cb;
  sopts.prepare_ctx = g;
  g->opt_finish_level = 0;
  char *error = NULL;
  int parts = 0, threads = 1;
  /* Build cache (docs/BUILD_CACHE.md): the key is this exact module plus
   * the target machine and partition count, so a hit is the object(s) the
   * same module produced before. */
  zan_bcache_key_t obj_key = {{0}, false};
  if (zan_codegen_split_plan(g, &parts, &threads)) {
    size_t plen = strlen(path);
    char **paths = (char **)calloc((size_t)parts, sizeof(char *));
    bool ok = paths != NULL;
    for (int i = 0; ok && i < parts; i++) {
      paths[i] = (char *)malloc(plen + 24);
      if (!paths[i]) {
        ok = false;
        break;
      }
      if (i == 0)
        memcpy(paths[i], path, plen + 1);
      else
        snprintf(paths[i], plen + 24, "%s.p%d.o", path, i);
    }
    if (ok && zan_bcache_obj_key(g->mod, tm, parts, &obj_key) &&
        zan_bcache_obj_load(&obj_key, parts, (const char *const *)paths)) {
      LLVMDisposeTargetMachine(tm);
      g->split_objs = paths;
      g->split_obj_count = parts;
      return ZAN_OK;
    }
    if (ok) {
      LLVMModuleRef mod = g->mod;
      g->mod = NULL; /* owned and disposed by the splitter */
      int rc = zan_codegen_split_emit(mod, tm, parts, threads, &sopts,
                                      (const char *const *)paths, &error);
      LLVMDisposeTargetMachine(tm);
      if (rc != 0) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0), 3092,
                           "failed to emit object file: %s",
                           error ? error : "?");
        free(error);
        for (int i = 0; i < parts; i++) {
          remove(paths[i]);
          free(paths[i]);
        }
        free(paths);
        return ZAN_ERROR;
      }
      zan_bcache_obj_store(&obj_key, parts, (const char *const *)paths);
      g->split_objs = paths;
      g->split_obj_count = parts;
      return ZAN_OK;
    }
    if (paths) {
      for (int i = 0; i < parts; i++)
        free(paths[i]);
      free(paths);
    }
  }
  const char *one_path[1] = {path};
  if (zan_bcache_obj_key(g->mod, tm, 1, &obj_key) &&
      zan_bcache_obj_load(&obj_key, 1, one_path)) {
    LLVMDisposeTargetMachine(tm);
    return ZAN_OK;
  }
  if (sopts.finish_level > 0) {
    zan_opt_finish_module(g->mod, tm, sopts.finish_level, sopts.vectorize);
    if (zan_opt_verify_module(g->mod, "after optimization pipeline") != ZAN_OK) {
      LLVMDisposeTargetMachine(tm);
      return ZAN_ERROR;
    }
  }
  zan_irgen_prepare_object_sections(g, g->mod);
  if (zan_opt_verify_module(g->mod, "after object section preparation") !=
      ZAN_OK) {
    LLVMDisposeTargetMachine(tm);
    return ZAN_ERROR;
  }
  if (LLVMTargetMachineEmitToFile(tm, g->mod, (char *)path, LLVMObjectFile,
                                  &error)) {
    zan_diag_emit_code(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0), 3092,
                       "failed to emit object file: %s", error);
    remove(path); /* the backend may have created a partial object */
    LLVMDisposeMessage(error);
    LLVMDisposeTargetMachine(tm);
    return ZAN_ERROR;
  }

  LLVMDisposeTargetMachine(tm);
  zan_bcache_obj_store(&obj_key, 1, one_path);
  return ZAN_OK;
}
