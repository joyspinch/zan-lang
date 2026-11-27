/* Phase 2a / 2b: __Canon reference-type generic code sharing & __gctx dictionary */

static bool is_task_or_binding_type(zan_type_t *t) {
  if (!t)
    return false;
  if (t->kind == TYPE_TASK)
    return true;
  if (type_is_binding(t))
    return true;
  if (t->name.str &&
      ((t->name.len >= 4 && memcmp(t->name.str, "Task", 4) == 0 &&
        (t->name.len == 4 || t->name.str[4] == '<')) ||
       (t->name.len >= 9 && memcmp(t->name.str, "ValueTask", 9) == 0 &&
        (t->name.len == 9 || t->name.str[9] == '<'))))
    return true;
  if (t->sym && t->sym->name.str &&
      ((t->sym->name.len == 4 && memcmp(t->sym->name.str, "Task", 4) == 0) ||
       (t->sym->name.len == 9 &&
        memcmp(t->sym->name.str, "ValueTask", 9) == 0)))
    return true;
  return false;
}

static bool class_has_eq_overload(zan_symbol_t *sym) {
  if (!sym)
    return false;
  zan_istr_t eq_istr = {(char *)"op_eq", 5};
  zan_istr_t neq_istr = {(char *)"op_neq", 6};
  return get_method_sym(sym, eq_istr) != NULL ||
         get_method_sym(sym, neq_istr) != NULL;
}

bool type_is_canon_eligible(zan_type_t *t) {
  if (!t)
    return false;
  if (t->is_canon)
    return true;
  if (t->kind == TYPE_OBJECT || t->kind == TYPE_INTERFACE)
    return true;
  if (t->kind == TYPE_CLASS) {
    if (is_task_or_binding_type(t))
      return false;
    if (t->sym && class_has_eq_overload(t->sym))
      return false;
    return true;
  }
  return false;
}

bool type_mentions_canon(zan_type_t *t) {
  if (!t)
    return false;
  if (t->is_canon)
    return true;
  if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE)
    return type_mentions_canon(t->element_type);
  for (int i = 0; i < t->type_arg_count; i++) {
    if (type_mentions_canon(t->type_args[i]))
      return true;
  }
  if (t->kind == TYPE_DELEGATE) {
    if (type_mentions_canon(t->delegate_ret_type))
      return true;
    for (int i = 0; i < t->delegate_param_count; i++) {
      if (type_mentions_canon(t->delegate_param_types[i]))
        return true;
    }
  }
  return false;
}

zan_type_t *get_canon_sentinel_type(zan_irgen_t *g) {
  if (!g)
    return NULL;
  if (!g->canon_sentinel_type) {
    zan_type_t *t =
        (zan_type_t *)zan_arena_alloc(g->binder->arena, sizeof(zan_type_t));
    memset(t, 0, sizeof(*t));
    t->kind = TYPE_OBJECT;
    t->name.str = "__Canon";
    t->name.len = 7;
    t->is_canon = 1;
    t->canon_tp_kind = 0;
    t->canon_tp_idx = 0;
    g->canon_sentinel_type = t;
  }
  return g->canon_sentinel_type;
}

zan_type_t *canonicalize_type(zan_irgen_t *g, zan_type_t *t) {
  if (!g || !t)
    return t;
  if (t->is_canon)
    return get_canon_sentinel_type(g);
  if (type_is_canon_eligible(t))
    return get_canon_sentinel_type(g);
  if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE) {
    zan_type_t *ne = canonicalize_type(g, t->element_type);
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
      na[i] = canonicalize_type(g, t->type_args[i]);
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

/* Determine whether a generic class/struct/method `sym` can safely share
 * `$__Canon` code (no unconstrained duck-typed member/operator/index access on
 * its type parameters, and no forwarding of its type parameters to another
 * generic class/method that is ineligible). Cached on `sym->canon_eligible_cache`. */
bool sym_supports_canon(zan_irgen_t *g, zan_symbol_t *sym);

typedef struct {
  zan_istr_t name;
  int tp_idx;
  zan_symbol_t *cls_sym;
  bool is_string;
  int scope_depth;
} canon_chk_var_t;

typedef struct {
  zan_irgen_t *g;
  zan_symbol_t *owner_cls;
  zan_istr_t tp_names[16];
  int tp_count;
  zan_type_t *tp_bases[16][4];
  int tp_base_count[16];
  zan_istr_t shadow_names[16];
  int shadow_count;
  canon_chk_var_t vars[128];
  int var_count;
  int scope_depth;
  bool ok;
} canon_chk_ctx_t;

static bool istr_eq(zan_istr_t a, zan_istr_t b) {
  return a.len == b.len && (a.len == 0 || memcmp(a.str, b.str, (size_t)a.len) == 0);
}

static bool istr_eq_cstr(zan_istr_t a, const char *s) {
  size_t len = strlen(s);
  return (size_t)a.len == len && (len == 0 || memcmp(a.str, s, len) == 0);
}

static bool is_object_method_name(zan_istr_t nm) {
  return istr_eq_cstr(nm, "ToString") || istr_eq_cstr(nm, "Equals") ||
         istr_eq_cstr(nm, "GetHashCode") || istr_eq_cstr(nm, "GetType");
}

static int canon_chk_find_tp_name(const canon_chk_ctx_t *ctx, zan_istr_t nm) {
  for (int i = 0; i < ctx->shadow_count; i++) {
    if (istr_eq(ctx->shadow_names[i], nm))
      return -1;
  }
  for (int i = 0; i < ctx->tp_count; i++) {
    if (istr_eq(ctx->tp_names[i], nm))
      return i;
  }
  return -1;
}

static int canon_chk_typeref_bare_tp(const canon_chk_ctx_t *ctx,
                                     zan_ast_node_t *tref) {
  if (!tref || tref->kind != AST_TYPE_REF)
    return -1;
  if (tref->type_ref.is_array || tref->type_ref.type_args.count > 0)
    return -1;
  return canon_chk_find_tp_name(ctx, tref->type_ref.name);
}

static bool canon_chk_typeref_mentions_tp(const canon_chk_ctx_t *ctx,
                                          zan_ast_node_t *tref) {
  if (!tref)
    return false;
  if (tref->kind == AST_ARRAY_TYPE)
    return canon_chk_typeref_mentions_tp(ctx, tref->type_ref.array_element);
  if (tref->kind == AST_NULLABLE_TYPE)
    return canon_chk_typeref_mentions_tp(ctx, tref->type_ref.array_element);
  if (tref->kind == AST_TUPLE_TYPE) {
    for (int i = 0; i < tref->tuple_type.elems.count; i++) {
      if (canon_chk_typeref_mentions_tp(ctx, tref->tuple_type.elems.items[i]))
        return true;
    }
    return false;
  }
  if (tref->kind != AST_TYPE_REF)
    return false;
  if (tref->type_ref.is_array && tref->type_ref.array_element)
    return canon_chk_typeref_mentions_tp(ctx, tref->type_ref.array_element);
  if (tref->type_ref.type_args.count == 0 &&
      canon_chk_find_tp_name(ctx, tref->type_ref.name) >= 0)
    return true;
  for (int i = 0; i < tref->type_ref.type_args.count; i++) {
    if (canon_chk_typeref_mentions_tp(ctx, tref->type_ref.type_args.items[i]))
      return true;
  }
  return false;
}

static int canon_chk_type_bare_tp(const canon_chk_ctx_t *ctx, zan_type_t *t) {
  if (!t || t->kind != TYPE_TYPE_PARAM)
    return -1;
  return canon_chk_find_tp_name(ctx, t->name);
}

static zan_symbol_t *canon_chk_resolve_type_sym(canon_chk_ctx_t *ctx,
                                                zan_ast_node_t *tref) {
  if (!tref || tref->kind != AST_TYPE_REF || !ctx->g || !ctx->g->binder)
    return NULL;
  if (tref->type_ref.ns_decl &&
      (tref->type_ref.ns_decl->kind == AST_CLASS_DECL ||
       tref->type_ref.ns_decl->kind == AST_STRUCT_DECL ||
       tref->type_ref.ns_decl->kind == AST_INTERFACE_DECL)) {
    zan_symbol_t *s =
        zan_binder_lookup(ctx->g->binder, tref->type_ref.ns_decl->type_decl.name);
    if (s && s->decl == tref->type_ref.ns_decl)
      return s;
  }
  zan_symbol_t *s = zan_binder_lookup(ctx->g->binder, tref->type_ref.name);
  if (s && (s->kind == SYM_CLASS || s->kind == SYM_STRUCT ||
            s->kind == SYM_INTERFACE))
    return s;
  return NULL;
}

static bool canon_chk_typeref_is_string(zan_ast_node_t *tref) {
  if (!tref || tref->kind != AST_TYPE_REF)
    return false;
  return !tref->type_ref.is_array && tref->type_ref.type_args.count == 0 &&
         (istr_eq_cstr(tref->type_ref.name, "string") ||
          istr_eq_cstr(tref->type_ref.name, "String"));
}

static void canon_chk_push_var(canon_chk_ctx_t *ctx, zan_istr_t name,
                               int tp_idx, zan_symbol_t *cls_sym,
                               bool is_string) {
  if (ctx->var_count >= 128)
    return;
  canon_chk_var_t *v = &ctx->vars[ctx->var_count++];
  v->name = name;
  v->tp_idx = tp_idx;
  v->cls_sym = cls_sym;
  v->is_string = is_string;
  v->scope_depth = ctx->scope_depth;
}

static void canon_chk_pop_scope(canon_chk_ctx_t *ctx, int depth) {
  while (ctx->var_count > 0 && ctx->vars[ctx->var_count - 1].scope_depth >= depth)
    ctx->var_count--;
}

static const canon_chk_var_t *canon_chk_find_var(const canon_chk_ctx_t *ctx,
                                                 zan_istr_t name) {
  for (int i = ctx->var_count - 1; i >= 0; i--) {
    if (istr_eq(ctx->vars[i].name, name))
      return &ctx->vars[i];
  }
  return NULL;
}

static bool canon_chk_tp_has_member(const canon_chk_ctx_t *ctx, int tp_idx,
                                    zan_istr_t mname, bool want_method) {
  if (tp_idx < 0 || tp_idx >= ctx->tp_count)
    return false;
  if (want_method && is_object_method_name(mname))
    return true;
  for (int bi = 0; bi < ctx->tp_base_count[tp_idx]; bi++) {
    zan_type_t *bt = ctx->tp_bases[tp_idx][bi];
    if (!bt || !bt->sym)
      continue;
    if (want_method) {
      if (get_method_sym(bt->sym, mname) != NULL)
        return true;
    } else {
      if (get_field_sym(bt->sym, mname) != NULL ||
          get_method_sym(bt->sym, mname) != NULL)
        return true;
    }
  }
  return false;
}

static bool canon_chk_ext_method_matches(canon_chk_ctx_t *ctx, int tp_idx,
                                         zan_istr_t mname, int argc,
                                         bool *out_eligible) {
  if (!ctx->g || !ctx->g->binder || !ctx->g->binder->ext_methods)
    return false;
  for (int i = 0; i < ctx->g->binder->ext_method_count; i++) {
    zan_ext_method_t *em = &ctx->g->binder->ext_methods[i];
    if (!em->method || !em->method->decl || !istr_eq(em->method->name, mname))
      continue;
    int pcnt = em->method->decl->method_decl.params.count;
    if (pcnt < 1 || argc > pcnt - 1)
      continue;
    zan_type_t *rt = em->recv_type;
    bool match = false;
    if (rt && (rt->kind == TYPE_TYPE_PARAM || rt->kind == TYPE_OBJECT)) {
      match = true;
    } else if (rt && rt->sym) {
      for (int bi = 0; bi < ctx->tp_base_count[tp_idx]; bi++) {
        if (ctx->tp_bases[tp_idx][bi] &&
            ctx->tp_bases[tp_idx][bi]->sym == rt->sym) {
          match = true;
          break;
        }
      }
    }
    if (match) {
      if (out_eligible)
        *out_eligible = sym_supports_canon(ctx->g, em->method);
      return true;
    }
  }
  return false;
}

static void canon_chk_typeref(canon_chk_ctx_t *ctx, zan_ast_node_t *tref) {
  if (!ctx->ok || !tref)
    return;
  if (tref->kind == AST_ARRAY_TYPE || tref->kind == AST_NULLABLE_TYPE) {
    canon_chk_typeref(ctx, tref->type_ref.array_element);
    return;
  }
  if (tref->kind == AST_TUPLE_TYPE) {
    for (int i = 0; i < tref->tuple_type.elems.count; i++)
      canon_chk_typeref(ctx, tref->tuple_type.elems.items[i]);
    return;
  }
  if (tref->kind != AST_TYPE_REF)
    return;
  if (tref->type_ref.is_array && tref->type_ref.array_element)
    canon_chk_typeref(ctx, tref->type_ref.array_element);
  for (int i = 0; i < tref->type_ref.type_args.count; i++)
    canon_chk_typeref(ctx, tref->type_ref.type_args.items[i]);
  if (tref->type_ref.type_args.count > 0 &&
      canon_chk_typeref_mentions_tp(ctx, tref)) {
    zan_symbol_t *ts = canon_chk_resolve_type_sym(ctx, tref);
    if (ts && !sym_supports_canon(ctx->g, ts))
      ctx->ok = false;
  }
}

static void canon_chk_expr(canon_chk_ctx_t *ctx, zan_ast_node_t *e,
                           int *out_tp, zan_symbol_t **out_cls,
                           bool *out_str);
static void canon_chk_stmt(canon_chk_ctx_t *ctx, zan_ast_node_t *st);

static int canon_chk_field_tp(canon_chk_ctx_t *ctx, zan_symbol_t *cs,
                              zan_istr_t fname, zan_symbol_t **out_cls,
                              bool *out_str) {
  if (!cs)
    return -1;
  zan_symbol_t *fs = get_field_sym(cs, fname);
  if (!fs)
    return -1;
  if (fs->decl && fs->decl->kind == AST_FIELD_DECL && fs->decl->field_decl.type) {
    if (cs == ctx->owner_cls) {
      int tp = canon_chk_typeref_bare_tp(ctx, fs->decl->field_decl.type);
      if (tp >= 0)
        return tp;
    }
    if (out_str && canon_chk_typeref_is_string(fs->decl->field_decl.type))
      *out_str = true;
    if (out_cls)
      *out_cls = canon_chk_resolve_type_sym(ctx, fs->decl->field_decl.type);
  } else if (fs->type) {
    if (cs == ctx->owner_cls) {
      int tp = canon_chk_type_bare_tp(ctx, fs->type);
      if (tp >= 0)
        return tp;
    }
    if (out_str && fs->type->kind == TYPE_STRING)
      *out_str = true;
    if (out_cls && fs->type->sym)
      *out_cls = fs->type->sym;
  }
  return -1;
}

static zan_symbol_t *canon_chk_resolve_callee_sym(canon_chk_ctx_t *ctx,
                                                  zan_ast_node_t *callee,
                                                  zan_symbol_t *call_resolved,
                                                  zan_symbol_t *recv_cls,
                                                  int recv_tp, int argc) {
  if (call_resolved)
    return call_resolved;
  if (!callee)
    return NULL;
  if (callee->kind == AST_IDENTIFIER) {
    if (ctx->owner_cls) {
      zan_symbol_t *m = get_method_sym(ctx->owner_cls, callee->ident.name);
      if (m)
        return m;
    }
    return zan_binder_lookup(ctx->g->binder, callee->ident.name);
  }
  if (callee->kind == AST_MEMBER_ACCESS) {
    zan_ast_node_t *obj = callee->member.object;
    zan_symbol_t *cs = recv_cls;
    if (!cs && obj) {
      if (obj->kind == AST_THIS_EXPR)
        cs = ctx->owner_cls;
      else if (obj->kind == AST_IDENTIFIER) {
        const canon_chk_var_t *v = canon_chk_find_var(ctx, obj->ident.name);
        if (v)
          cs = v->cls_sym;
        else {
          zan_symbol_t *ls = zan_binder_lookup(ctx->g->binder, obj->ident.name);
          if (ls && (ls->kind == SYM_CLASS || ls->kind == SYM_STRUCT ||
                     ls->kind == SYM_INTERFACE))
            cs = ls;
        }
      }
    }
    if (cs) {
      zan_symbol_t *m = get_method_sym(cs, callee->member.name);
      if (m)
        return m;
    }
    if (recv_tp < 0 && ctx->g && ctx->g->binder && ctx->g->binder->ext_methods) {
      for (int i = 0; i < ctx->g->binder->ext_method_count; i++) {
        zan_ext_method_t *em = &ctx->g->binder->ext_methods[i];
        if (!em->method || !em->method->decl ||
            !istr_eq(em->method->name, callee->member.name))
          continue;
        int pcnt = em->method->decl->method_decl.params.count;
        if (pcnt >= 1 && argc <= pcnt - 1) {
          if (!em->recv_type || !em->recv_type->sym || !cs ||
              em->recv_type->sym == cs ||
              (em->recv_type->kind == TYPE_INTERFACE &&
               em->recv_type->type_arg_count == 0 &&
               zan_binder_class_implements_iface(cs, em->recv_type->sym)))
            return em->method;
        }
      }
    }
  }
  return NULL;
}

static void canon_chk_expr(canon_chk_ctx_t *ctx, zan_ast_node_t *e,
                           int *out_tp, zan_symbol_t **out_cls,
                           bool *out_str) {
  if (out_tp)
    *out_tp = -1;
  if (out_cls)
    *out_cls = NULL;
  if (out_str)
    *out_str = false;
  if (!ctx->ok || !e)
    return;

  switch (e->kind) {
  case AST_STRING_LITERAL:
  case AST_STRING_INTERP:
    if (e->kind == AST_STRING_INTERP) {
      for (int i = 0; i < e->string_interp.parts.count; i++)
        canon_chk_expr(ctx, e->string_interp.parts.items[i], NULL, NULL, NULL);
    }
    if (out_str)
      *out_str = true;
    break;
  case AST_THIS_EXPR:
    if (out_cls)
      *out_cls = ctx->owner_cls;
    break;
  case AST_IDENTIFIER: {
    const canon_chk_var_t *v = canon_chk_find_var(ctx, e->ident.name);
    if (v) {
      if (out_tp)
        *out_tp = v->tp_idx;
      if (out_cls)
        *out_cls = v->cls_sym;
      if (out_str)
        *out_str = v->is_string;
      break;
    }
    if (ctx->owner_cls) {
      int tp = canon_chk_field_tp(ctx, ctx->owner_cls, e->ident.name, out_cls,
                                  out_str);
      if (out_tp)
        *out_tp = tp;
    }
    break;
  }
  case AST_MEMBER_ACCESS: {
    int otp = -1;
    zan_symbol_t *ocls = NULL;
    canon_chk_expr(ctx, e->member.object, &otp, &ocls, NULL);
    if (!ctx->ok)
      break;
    if (otp >= 0) {
      if (!canon_chk_tp_has_member(ctx, otp, e->member.name, false))
        ctx->ok = false;
      break;
    }
    if (ocls) {
      int ftp = canon_chk_field_tp(ctx, ocls, e->member.name, out_cls, out_str);
      if (out_tp)
        *out_tp = ftp;
    }
    break;
  }
  case AST_CALL: {
    bool any_arg_tp = false;
    for (int i = 0; i < e->call.type_args.count; i++) {
      canon_chk_typeref(ctx, e->call.type_args.items[i]);
      if (canon_chk_typeref_mentions_tp(ctx, e->call.type_args.items[i]))
        any_arg_tp = true;
    }
    for (int i = 0; i < e->call.args.count; i++) {
      int atp = -1;
      zan_ast_node_t *arg = e->call.args.items[i];
      if (arg && arg->kind == AST_NAMED_ARG)
        arg = arg->named_arg.expr;
      if (arg && arg->kind == AST_REF_ARG)
        arg = arg->ref_arg.expr;
      canon_chk_expr(ctx, arg, &atp, NULL, NULL);
      if (atp >= 0)
        any_arg_tp = true;
    }
    if (!ctx->ok)
      break;
    zan_ast_node_t *callee = e->call.callee;
    if (!callee)
      break;
    if (callee->kind == AST_MEMBER_ACCESS) {
      int otp = -1;
      zan_symbol_t *ocls = NULL;
      canon_chk_expr(ctx, callee->member.object, &otp, &ocls, NULL);
      if (!ctx->ok)
        break;
      if (otp >= 0) {
        if (!canon_chk_tp_has_member(ctx, otp, callee->member.name, true)) {
          bool ext_ok = false;
          if (!canon_chk_ext_method_matches(ctx, otp, callee->member.name,
                                            e->call.args.count, &ext_ok) ||
              !ext_ok) {
            ctx->ok = false;
          }
        }
        break;
      }
      zan_symbol_t *tsym = canon_chk_resolve_callee_sym(
          ctx, callee, e->call.resolved_sym, ocls, otp, e->call.args.count);
      if (tsym && tsym->decl && tsym->decl->kind == AST_METHOD_DECL) {
        if ((any_arg_tp || e->call.type_args.count > 0) &&
            tsym->decl->method_decl.type_params.count > 0) {
          bool mentions = any_arg_tp;
          if (!mentions) {
            for (int ti = 0; ti < e->call.type_args.count; ti++) {
              if (canon_chk_typeref_mentions_tp(ctx, e->call.type_args.items[ti]))
                mentions = true;
            }
          }
          if (mentions && !sym_supports_canon(ctx->g, tsym))
            ctx->ok = false;
        }
        if (tsym->parent == ctx->owner_cls && tsym->decl->method_decl.return_type) {
          int rtp = canon_chk_typeref_bare_tp(ctx, tsym->decl->method_decl.return_type);
          if (out_tp)
            *out_tp = rtp;
        }
        if (tsym->decl->method_decl.return_type &&
            canon_chk_typeref_is_string(tsym->decl->method_decl.return_type) &&
            out_str)
          *out_str = true;
      }
    } else {
      canon_chk_expr(ctx, callee, NULL, NULL, NULL);
      if (!ctx->ok)
        break;
      zan_symbol_t *tsym = canon_chk_resolve_callee_sym(
          ctx, callee, e->call.resolved_sym, NULL, -1, e->call.args.count);
      if (tsym && tsym->decl && tsym->decl->kind == AST_METHOD_DECL) {
        if (any_arg_tp && tsym->decl->method_decl.type_params.count > 0 &&
            !sym_supports_canon(ctx->g, tsym))
          ctx->ok = false;
        if (tsym->parent == ctx->owner_cls && tsym->decl->method_decl.return_type) {
          int rtp = canon_chk_typeref_bare_tp(ctx, tsym->decl->method_decl.return_type);
          if (out_tp)
            *out_tp = rtp;
        }
        if (tsym->decl->method_decl.return_type &&
            canon_chk_typeref_is_string(tsym->decl->method_decl.return_type) &&
            out_str)
          *out_str = true;
      }
    }
    break;
  }
  case AST_BINARY:
  case AST_ASSIGNMENT: {
    int ltp = -1, rtp = -1;
    bool lstr = false, rstr = false;
    canon_chk_expr(ctx, e->binary.left, &ltp, NULL, &lstr);
    canon_chk_expr(ctx, e->binary.right, &rtp, NULL, &rstr);
    if (!ctx->ok)
      break;
    if (e->kind == AST_BINARY && (ltp >= 0 || rtp >= 0)) {
      zan_token_kind_t op = e->binary.op;
      if (op == TK_EQ_EQ || op == TK_BANG_EQ || op == TK_QUESTION_QUESTION) {
        /* pointer equality / null coalescing on __Canon */
      } else if (op == TK_PLUS && (lstr || rstr) && !(ltp >= 0 && rtp >= 0)) {
        /* string concatenation with __Canon object */
        if (out_str)
          *out_str = true;
      } else {
        ctx->ok = false;
      }
    } else if (e->kind == AST_BINARY && e->binary.op == TK_PLUS && (lstr || rstr)) {
      if (out_str)
        *out_str = true;
    }
    if (e->kind == AST_ASSIGNMENT && out_tp)
      *out_tp = ltp;
    break;
  }
  case AST_UNARY:
  case AST_POSTFIX_UNARY: {
    int otp = -1;
    canon_chk_expr(ctx, e->unary.operand, &otp, NULL, NULL);
    if (otp >= 0 && !(e->kind == AST_POSTFIX_UNARY && e->unary.op == TK_BANG))
      ctx->ok = false;
    if (out_tp && e->kind == AST_POSTFIX_UNARY && e->unary.op == TK_BANG)
      *out_tp = otp;
    break;
  }
  case AST_INDEX: {
    int otp = -1;
    canon_chk_expr(ctx, e->index.object, &otp, NULL, NULL);
    canon_chk_expr(ctx, e->index.index, NULL, NULL, NULL);
    for (int i = 0; i < e->index.extra.count; i++)
      canon_chk_expr(ctx, e->index.extra.items[i], NULL, NULL, NULL);
    if (otp >= 0) {
      zan_istr_t idx_istr = {(char *)"op_index", 8};
      if (!canon_chk_tp_has_member(ctx, otp, idx_istr, true))
        ctx->ok = false;
    }
    break;
  }
  case AST_NEW_EXPR:
    canon_chk_typeref(ctx, e->new_expr.type);
    canon_chk_expr(ctx, e->new_expr.call_init, NULL, NULL, NULL);
    for (int i = 0; i < e->new_expr.args.count; i++)
      canon_chk_expr(ctx, e->new_expr.args.items[i], NULL, NULL, NULL);
    for (int i = 0; i < e->new_expr.arg_inits.count; i++)
      canon_chk_expr(ctx, e->new_expr.arg_inits.items[i], NULL, NULL, NULL);
    if (e->new_expr.type && e->new_expr.type->kind == AST_TYPE_REF &&
        !e->new_expr.is_array) {
      int ntp = canon_chk_typeref_bare_tp(ctx, e->new_expr.type);
      if (ntp >= 0) {
        if (out_tp)
          *out_tp = ntp;
      } else if (out_cls) {
        *out_cls = canon_chk_resolve_type_sym(ctx, e->new_expr.type);
      }
    }
    break;
  case AST_CAST_EXPR:
    canon_chk_typeref(ctx, e->cast.type);
    canon_chk_expr(ctx, e->cast.expr, NULL, NULL, NULL);
    if (out_tp)
      *out_tp = canon_chk_typeref_bare_tp(ctx, e->cast.type);
    if (out_str && canon_chk_typeref_is_string(e->cast.type))
      *out_str = true;
    break;
  case AST_IS_EXPR:
  case AST_AS_EXPR:
    canon_chk_typeref(ctx, e->type_test.type);
    canon_chk_expr(ctx, e->type_test.expr, NULL, NULL, NULL);
    if (e->kind == AST_AS_EXPR && out_tp)
      *out_tp = canon_chk_typeref_bare_tp(ctx, e->type_test.type);
    break;
  case AST_AWAIT_EXPR: {
    int otp = -1;
    canon_chk_expr(ctx, e->await_expr.expr, &otp, out_cls, out_str);
    break;
  }
  case AST_CONDITIONAL: {
    int ttp = -1, etp = -1;
    bool tstr = false, estr = false;
    canon_chk_expr(ctx, e->conditional.cond, NULL, NULL, NULL);
    canon_chk_expr(ctx, e->conditional.then_expr, &ttp, NULL, &tstr);
    canon_chk_expr(ctx, e->conditional.else_expr, &etp, NULL, &estr);
    if (out_tp && ttp >= 0 && (ttp == etp || etp < 0))
      *out_tp = ttp;
    if (out_str && (tstr || estr))
      *out_str = true;
    break;
  }
  case AST_LAMBDA: {
    int d = ++ctx->scope_depth;
    for (int i = 0; i < e->lambda.params.count; i++) {
      zan_ast_node_t *p = e->lambda.params.items[i];
      if (!p || p->kind != AST_PARAM)
        continue;
      canon_chk_typeref(ctx, p->param.type);
      int ptp = canon_chk_typeref_bare_tp(ctx, p->param.type);
      zan_symbol_t *pcls = canon_chk_resolve_type_sym(ctx, p->param.type);
      canon_chk_push_var(ctx, p->param.name, ptp, pcls,
                         canon_chk_typeref_is_string(p->param.type));
    }
    if (e->lambda.body) {
      if (e->lambda.body->kind == AST_BLOCK)
        canon_chk_stmt(ctx, e->lambda.body);
      else
        canon_chk_expr(ctx, e->lambda.body, NULL, NULL, NULL);
    }
    canon_chk_pop_scope(ctx, d);
    ctx->scope_depth--;
    break;
  }
  case AST_TUPLE_EXPR:
    for (int i = 0; i < e->tuple_expr.items.count; i++)
      canon_chk_expr(ctx, e->tuple_expr.items.items[i], NULL, NULL, NULL);
    break;
  case AST_COLL_INIT:
    for (int i = 0; i < e->coll_init.items.count; i++)
      canon_chk_expr(ctx, e->coll_init.items.items[i], NULL, NULL, NULL);
    break;
  case AST_SWITCH_EXPR:
    canon_chk_expr(ctx, e->switch_expr.expr, NULL, NULL, NULL);
    for (int i = 0; i < e->switch_expr.arms.count; i++) {
      zan_ast_node_t *arm = e->switch_expr.arms.items[i];
      if (!arm || arm->kind != AST_SWITCH_ARM)
        continue;
      int d = ++ctx->scope_depth;
      canon_chk_typeref(ctx, arm->switch_arm.type_pattern);
      if (arm->switch_arm.var_name.len > 0 && arm->switch_arm.type_pattern) {
        int tp = canon_chk_typeref_bare_tp(ctx, arm->switch_arm.type_pattern);
        zan_symbol_t *cs = canon_chk_resolve_type_sym(ctx, arm->switch_arm.type_pattern);
        canon_chk_push_var(ctx, arm->switch_arm.var_name, tp, cs,
                           canon_chk_typeref_is_string(arm->switch_arm.type_pattern));
      }
      canon_chk_expr(ctx, arm->switch_arm.pattern, NULL, NULL, NULL);
      canon_chk_expr(ctx, arm->switch_arm.when_cond, NULL, NULL, NULL);
      canon_chk_expr(ctx, arm->switch_arm.result, NULL, NULL, NULL);
      canon_chk_pop_scope(ctx, d);
      ctx->scope_depth--;
    }
    break;
  default:
    break;
  }
}

static void canon_chk_stmt(canon_chk_ctx_t *ctx, zan_ast_node_t *st) {
  if (!ctx->ok || !st)
    return;
  switch (st->kind) {
  case AST_BLOCK: {
    int d = ++ctx->scope_depth;
    for (int i = 0; i < st->block.stmts.count && ctx->ok; i++)
      canon_chk_stmt(ctx, st->block.stmts.items[i]);
    canon_chk_pop_scope(ctx, d);
    ctx->scope_depth--;
    break;
  }
  case AST_VAR_DECL: {
    canon_chk_typeref(ctx, st->var_decl.type);
    int itp = -1;
    zan_symbol_t *icls = NULL;
    bool istr = false;
    canon_chk_expr(ctx, st->var_decl.initializer, &itp, &icls, &istr);
    int vtp = st->var_decl.type ? canon_chk_typeref_bare_tp(ctx, st->var_decl.type)
                                : itp;
    zan_symbol_t *vcls =
        st->var_decl.type ? canon_chk_resolve_type_sym(ctx, st->var_decl.type) : icls;
    bool vstr =
        st->var_decl.type ? canon_chk_typeref_is_string(st->var_decl.type) : istr;
    canon_chk_push_var(ctx, st->var_decl.name, vtp, vcls, vstr);
    break;
  }
  case AST_EXPR_STMT:
    canon_chk_expr(ctx, st->expr_stmt.expr, NULL, NULL, NULL);
    break;
  case AST_RETURN_STMT:
    canon_chk_expr(ctx, st->ret.value, NULL, NULL, NULL);
    break;
  case AST_IF_STMT: {
    int d = ++ctx->scope_depth;
    canon_chk_expr(ctx, st->if_stmt.cond, NULL, NULL, NULL);
    if (st->if_stmt.cond && st->if_stmt.cond->kind == AST_IS_EXPR &&
        !st->if_stmt.cond->type_test.is_not &&
        st->if_stmt.cond->type_test.var_name.len > 0) {
      int tp = canon_chk_typeref_bare_tp(ctx, st->if_stmt.cond->type_test.type);
      zan_symbol_t *cs =
          canon_chk_resolve_type_sym(ctx, st->if_stmt.cond->type_test.type);
      canon_chk_push_var(ctx, st->if_stmt.cond->type_test.var_name, tp, cs,
                         canon_chk_typeref_is_string(st->if_stmt.cond->type_test.type));
    }
    canon_chk_stmt(ctx, st->if_stmt.then_body);
    canon_chk_pop_scope(ctx, d);
    ctx->scope_depth--;
    canon_chk_stmt(ctx, st->if_stmt.else_body);
    break;
  }
  case AST_WHILE_STMT:
  case AST_DO_WHILE_STMT:
    canon_chk_expr(ctx, st->while_stmt.cond, NULL, NULL, NULL);
    canon_chk_stmt(ctx, st->while_stmt.body);
    break;
  case AST_FOR_STMT: {
    int d = ++ctx->scope_depth;
    canon_chk_stmt(ctx, st->for_stmt.init);
    canon_chk_expr(ctx, st->for_stmt.cond, NULL, NULL, NULL);
    canon_chk_expr(ctx, st->for_stmt.step, NULL, NULL, NULL);
    canon_chk_stmt(ctx, st->for_stmt.body);
    canon_chk_pop_scope(ctx, d);
    ctx->scope_depth--;
    break;
  }
  case AST_FOREACH_STMT: {
    int d = ++ctx->scope_depth;
    canon_chk_typeref(ctx, st->foreach_stmt.var_type);
    canon_chk_expr(ctx, st->foreach_stmt.collection, NULL, NULL, NULL);
    int vtp = canon_chk_typeref_bare_tp(ctx, st->foreach_stmt.var_type);
    zan_symbol_t *vcls =
        canon_chk_resolve_type_sym(ctx, st->foreach_stmt.var_type);
    canon_chk_push_var(ctx, st->foreach_stmt.var_name, vtp, vcls,
                       canon_chk_typeref_is_string(st->foreach_stmt.var_type));
    canon_chk_stmt(ctx, st->foreach_stmt.body);
    canon_chk_pop_scope(ctx, d);
    ctx->scope_depth--;
    break;
  }
  case AST_THROW_STMT:
    canon_chk_expr(ctx, st->throw_stmt.value, NULL, NULL, NULL);
    break;
  case AST_TRY_STMT:
    canon_chk_stmt(ctx, st->try_stmt.try_body);
    for (int i = 0; i < st->try_stmt.catches.count && ctx->ok; i++) {
      zan_ast_node_t *cc = st->try_stmt.catches.items[i];
      if (!cc || cc->kind != AST_CATCH_CLAUSE)
        continue;
      int d = ++ctx->scope_depth;
      canon_chk_typeref(ctx, cc->catch_clause.type);
      canon_chk_push_var(ctx, cc->catch_clause.var_name, -1,
                         canon_chk_resolve_type_sym(ctx, cc->catch_clause.type),
                         false);
      canon_chk_stmt(ctx, cc->catch_clause.body);
      canon_chk_pop_scope(ctx, d);
      ctx->scope_depth--;
    }
    canon_chk_stmt(ctx, st->try_stmt.finally_body);
    break;
  case AST_SWITCH_STMT:
    canon_chk_expr(ctx, st->switch_stmt.expr, NULL, NULL, NULL);
    for (int i = 0; i < st->switch_stmt.cases.count && ctx->ok; i++) {
      zan_ast_node_t *sc = st->switch_stmt.cases.items[i];
      if (!sc || sc->kind != AST_SWITCH_CASE)
        continue;
      int d = ++ctx->scope_depth;
      canon_chk_typeref(ctx, sc->switch_case.type_pattern);
      if (sc->switch_case.var_name.len > 0 && sc->switch_case.type_pattern) {
        int tp = canon_chk_typeref_bare_tp(ctx, sc->switch_case.type_pattern);
        zan_symbol_t *cs =
            canon_chk_resolve_type_sym(ctx, sc->switch_case.type_pattern);
        canon_chk_push_var(ctx, sc->switch_case.var_name, tp, cs,
                           canon_chk_typeref_is_string(sc->switch_case.type_pattern));
      }
      canon_chk_expr(ctx, sc->switch_case.pattern, NULL, NULL, NULL);
      canon_chk_expr(ctx, sc->switch_case.when_cond, NULL, NULL, NULL);
      canon_chk_stmt(ctx, sc->switch_case.body);
      canon_chk_pop_scope(ctx, d);
      ctx->scope_depth--;
    }
    break;
  case AST_LOCK_STMT:
    canon_chk_expr(ctx, st->lock_stmt.expr, NULL, NULL, NULL);
    canon_chk_stmt(ctx, st->lock_stmt.body);
    break;
  case AST_CHECKED_STMT:
    canon_chk_stmt(ctx, st->checked_stmt.body);
    break;
  case AST_YIELD_STMT:
    canon_chk_expr(ctx, st->yield_stmt.value, NULL, NULL, NULL);
    break;
  default:
    break;
  }
}

static void canon_chk_load_where(canon_chk_ctx_t *ctx, zan_ast_list_t *wcs) {
  if (!wcs || !ctx->g || !ctx->g->binder)
    return;
  for (int i = 0; i < wcs->count; i++) {
    zan_ast_node_t *wc = wcs->items[i];
    if (!wc || wc->kind != AST_WHERE_CLAUSE)
      continue;
    int idx = canon_chk_find_tp_name(ctx, wc->where_clause.param_name);
    if (idx < 0)
      continue;
    for (int bi = 0; bi < wc->where_clause.constraints.count; bi++) {
      zan_ast_node_t *bn = wc->where_clause.constraints.items[bi];
      if (!bn || bn->kind != AST_TYPE_REF)
        continue;
      zan_type_t *bt = zan_binder_resolve_type(ctx->g->binder, bn);
      if (bt && (bt->kind == TYPE_CLASS || bt->kind == TYPE_INTERFACE) &&
          ctx->tp_base_count[idx] < 4) {
        ctx->tp_bases[idx][ctx->tp_base_count[idx]++] = bt;
      }
    }
  }
}

static void canon_chk_method(canon_chk_ctx_t *ctx, zan_ast_node_t *m,
                             bool shadow_method_tps) {
  if (!ctx->ok || !m)
    return;
  int saved_shadow = ctx->shadow_count;
  if (shadow_method_tps && m->kind == AST_METHOD_DECL) {
    for (int i = 0; i < m->method_decl.type_params.count; i++) {
      zan_ast_node_t *tp = m->method_decl.type_params.items[i];
      if (tp && tp->kind == AST_IDENTIFIER && ctx->shadow_count < 16)
        ctx->shadow_names[ctx->shadow_count++] = tp->ident.name;
    }
  }
  int d = ++ctx->scope_depth;
  canon_chk_typeref(ctx, m->method_decl.return_type);
  for (int i = 0; i < m->method_decl.params.count; i++) {
    zan_ast_node_t *p = m->method_decl.params.items[i];
    if (!p || p->kind != AST_PARAM)
      continue;
    canon_chk_typeref(ctx, p->param.type);
    int ptp = canon_chk_typeref_bare_tp(ctx, p->param.type);
    zan_symbol_t *pcls = canon_chk_resolve_type_sym(ctx, p->param.type);
    canon_chk_push_var(ctx, p->param.name, ptp, pcls,
                       canon_chk_typeref_is_string(p->param.type));
  }
  if (m->method_decl.body) {
    if (m->method_decl.body->kind == AST_BLOCK)
      canon_chk_stmt(ctx, m->method_decl.body);
    else
      canon_chk_expr(ctx, m->method_decl.body, NULL, NULL, NULL);
  }
  canon_chk_pop_scope(ctx, d);
  ctx->scope_depth--;
  ctx->shadow_count = saved_shadow;
}

bool sym_supports_canon(zan_irgen_t *g, zan_symbol_t *sym) {
  if (!g || !sym || !sym->decl)
    return true;
  if (sym->canon_eligible_cache != 0)
    return sym->canon_eligible_cache > 0;
  if (sym->canon_check_visiting)
    return true;

  zan_ast_node_t *d = sym->decl;
  if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL &&
      d->kind != AST_INTERFACE_DECL && d->kind != AST_METHOD_DECL) {
    sym->canon_eligible_cache = 1;
    return true;
  }
  if (d->kind == AST_METHOD_DECL && d->method_decl.type_params.count == 0) {
    sym->canon_eligible_cache = 1;
    return true;
  }
  if ((d->kind == AST_CLASS_DECL || d->kind == AST_STRUCT_DECL ||
       d->kind == AST_INTERFACE_DECL) &&
      d->type_decl.type_params.count == 0) {
    sym->canon_eligible_cache = 1;
    return true;
  }

  sym->canon_check_visiting = 1;
  canon_chk_ctx_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.g = g;
  ctx.ok = true;

  if (d->kind == AST_METHOD_DECL) {
    ctx.owner_cls = sym->parent;
    for (int i = 0; i < d->method_decl.type_params.count && ctx.tp_count < 16; i++) {
      zan_ast_node_t *tp = d->method_decl.type_params.items[i];
      if (tp && tp->kind == AST_IDENTIFIER)
        ctx.tp_names[ctx.tp_count++] = tp->ident.name;
    }
    canon_chk_load_where(&ctx, zan_ast_method_where_clauses(d));
    canon_chk_method(&ctx, d, false);
  } else {
    ctx.owner_cls = sym;
    for (int i = 0; i < d->type_decl.type_params.count && ctx.tp_count < 16; i++) {
      zan_ast_node_t *tp = d->type_decl.type_params.items[i];
      if (tp && tp->kind == AST_IDENTIFIER)
        ctx.tp_names[ctx.tp_count++] = tp->ident.name;
    }
    canon_chk_load_where(&ctx, zan_ast_type_where_clauses(d));
    for (int i = 0; i < d->type_decl.bases.count && ctx.ok; i++)
      canon_chk_typeref(&ctx, d->type_decl.bases.items[i]);
    for (int i = 0; i < d->type_decl.members.count && ctx.ok; i++) {
      zan_ast_node_t *m = d->type_decl.members.items[i];
      if (!m)
        continue;
      if (m->kind == AST_FIELD_DECL || m->kind == AST_PROPERTY_DECL) {
        canon_chk_typeref(&ctx, m->field_decl.type);
        canon_chk_expr(&ctx, m->field_decl.initializer, NULL, NULL, NULL);
        if (m->field_decl.getter_body) {
          int gd = ++ctx.scope_depth;
          canon_chk_stmt(&ctx, m->field_decl.getter_body);
          canon_chk_pop_scope(&ctx, gd);
          ctx.scope_depth--;
        }
        if (m->field_decl.setter_body) {
          int sd = ++ctx.scope_depth;
          zan_istr_t val_istr = {(char *)"value", 5};
          int vtp = canon_chk_typeref_bare_tp(&ctx, m->field_decl.type);
          zan_symbol_t *vcls = canon_chk_resolve_type_sym(&ctx, m->field_decl.type);
          canon_chk_push_var(&ctx, val_istr, vtp, vcls,
                             canon_chk_typeref_is_string(m->field_decl.type));
          canon_chk_stmt(&ctx, m->field_decl.setter_body);
          canon_chk_pop_scope(&ctx, sd);
          ctx.scope_depth--;
        }
      } else if (m->kind == AST_METHOD_DECL || m->kind == AST_CONSTRUCTOR_DECL) {
        canon_chk_method(&ctx, m, true);
      }
    }
  }

  sym->canon_check_visiting = 0;
  sym->canon_eligible_cache = ctx.ok ? 1 : -1;
  return ctx.ok;
}

