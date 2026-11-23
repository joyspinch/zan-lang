/* gui_kind_manifest.c - Compile-time [GuiKind] / [GuiKindManifest] synthesizer */

typedef struct {
  zan_istr_t kind;
  zan_ast_node_t *decl;
  zan_ast_node_t *factory_method;
  zan_ast_node_t *target_type;
  zan_loc_t loc;
} gui_kind_entry_t;

static bool gkm_istr_eq(zan_istr_t a, const char *b) {
  size_t blen = strlen(b);
  return a.str && a.len == (uint32_t)blen && memcmp(a.str, b, blen) == 0;
}

static bool gkm_istr_eq_istr(zan_istr_t a, zan_istr_t b) {
  return a.len == b.len &&
         (a.len == 0 || (a.str && b.str && memcmp(a.str, b.str, a.len) == 0));
}

static bool gkm_istr_eq_lower(zan_istr_t a, zan_istr_t b) {
  if (a.len != b.len)
    return false;
  for (uint32_t i = 0; i < a.len; i++) {
    char ca = a.str[i];
    char cb = b.str[i];
    if (ca >= 'A' && ca <= 'Z')
      ca = (char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z')
      cb = (char)(cb + 32);
    if (ca != cb)
      return false;
  }
  return true;
}

static zan_istr_t gkm_intern_str(zan_arena_t *arena, const char *s,
                                 size_t len) {
  char *buf = (char *)zan_arena_alloc(arena, len + 1);
  if (buf && len > 0)
    memcpy(buf, s, len);
  if (buf)
    buf[len] = 0;
  return (zan_istr_t){buf, (uint32_t)len};
}

static zan_istr_t gkm_lower_istr(zan_arena_t *arena, zan_istr_t s) {
  char *buf = (char *)zan_arena_alloc(arena, (size_t)s.len + 1);
  if (!buf)
    return (zan_istr_t){0};
  for (uint32_t i = 0; i < s.len; i++) {
    char ch = s.str[i];
    if (ch >= 'A' && ch <= 'Z')
      ch = (char)(ch + 32);
    buf[i] = ch;
  }
  buf[s.len] = 0;
  return (zan_istr_t){buf, s.len};
}

static bool gkm_is_attr(const zan_ast_node_t *a, const char *short_name,
                        const char *full_name) {
  if (!a || a->kind != AST_ATTRIBUTE || !a->attribute.name ||
      a->attribute.name->kind != AST_IDENTIFIER)
    return false;
  zan_istr_t name = a->attribute.name->ident.name;
  return gkm_istr_eq(name, short_name) || gkm_istr_eq(name, full_name);
}

static bool gkm_extract_gui_kind(const zan_ast_node_t *attr, zan_diag_t *diag,
                                 zan_istr_t *out_kind,
                                 zan_ast_node_t **out_type) {
  *out_kind = (zan_istr_t){0};
  *out_type = NULL;
  int pos = 0;
  for (int i = 0; i < attr->attribute.args.count; i++) {
    zan_ast_node_t *arg = attr->attribute.args.items[i];
    if (!arg)
      continue;
    if (arg->kind == AST_ASSIGNMENT && arg->binary.left &&
        arg->binary.left->kind == AST_IDENTIFIER) {
      zan_istr_t pname = arg->binary.left->ident.name;
      zan_ast_node_t *rhs = arg->binary.right;
      if (gkm_istr_eq(pname, "Kind") || gkm_istr_eq(pname, "kind")) {
        if (rhs && rhs->kind == AST_STRING_LITERAL)
          *out_kind = rhs->str_val;
      } else if (gkm_istr_eq(pname, "TargetType") ||
                 gkm_istr_eq(pname, "targetType")) {
        if (rhs && rhs->kind == AST_TYPEOF_EXPR)
          *out_type = rhs->cast.type;
      }
      continue;
    }
    if (pos == 0) {
      if (arg->kind == AST_STRING_LITERAL)
        *out_kind = arg->str_val;
    } else if (pos == 1) {
      if (arg->kind == AST_TYPEOF_EXPR)
        *out_type = arg->cast.type;
    }
    pos++;
  }
  if (!out_kind->str || out_kind->len == 0) {
    zan_diag_emit(diag, DIAG_ERROR, attr->loc,
                  "[GuiKind] requires a non-empty string literal kind");
    return false;
  }
  return true;
}

static bool gkm_extract_manifest_mode(const zan_ast_node_t *attr,
                                      zan_diag_t *diag, zan_istr_t *out_mode) {
  *out_mode = (zan_istr_t){0};
  for (int i = 0; i < attr->attribute.args.count; i++) {
    zan_ast_node_t *arg = attr->attribute.args.items[i];
    if (!arg)
      continue;
    if (arg->kind == AST_ASSIGNMENT && arg->binary.left &&
        arg->binary.left->kind == AST_IDENTIFIER) {
      zan_istr_t pname = arg->binary.left->ident.name;
      if ((gkm_istr_eq(pname, "Mode") || gkm_istr_eq(pname, "mode")) &&
          arg->binary.right && arg->binary.right->kind == AST_STRING_LITERAL) {
        *out_mode = arg->binary.right->str_val;
        break;
      }
      continue;
    }
    if (arg->kind == AST_STRING_LITERAL) {
      *out_mode = arg->str_val;
      break;
    }
  }
  if (!out_mode->str || out_mode->len == 0) {
    zan_diag_emit(diag, DIAG_ERROR, attr->loc,
                  "[GuiKindManifest] requires a mode string literal");
    return false;
  }
  return true;
}

static zan_ast_node_t *gkm_clone_type_ref(zan_ast_node_t *src,
                                          zan_arena_t *arena, zan_loc_t loc) {
  if (!src || src->kind != AST_TYPE_REF)
    return src;
  zan_ast_node_t *copy = zan_ast_new(arena, AST_TYPE_REF, loc);
  copy->type_ref = src->type_ref;
  copy->type_ref.rt_type = NULL;
  copy->type_ref.rt_scope = NULL;
  zan_ast_list_init(&copy->type_ref.type_args);
  for (int i = 0; i < src->type_ref.type_args.count; i++) {
    zan_ast_list_push(&copy->type_ref.type_args,
                      gkm_clone_type_ref(src->type_ref.type_args.items[i],
                                         arena, loc),
                      arena);
  }
  if (src->type_ref.array_element) {
    copy->type_ref.array_element =
        gkm_clone_type_ref(src->type_ref.array_element, arena, loc);
  }
  return copy;
}

static zan_ast_node_t *gkm_make_ident(zan_arena_t *arena, zan_loc_t loc,
                                      zan_istr_t name) {
  zan_ast_node_t *id = zan_ast_new(arena, AST_IDENTIFIER, loc);
  id->ident.name = name;
  id->ident.source_ident = true;
  return id;
}

static zan_ast_node_t *gkm_make_str(zan_arena_t *arena, zan_loc_t loc,
                                    zan_istr_t val) {
  zan_ast_node_t *lit = zan_ast_new(arena, AST_STRING_LITERAL, loc);
  lit->str_val = val;
  return lit;
}

static zan_ast_node_t *gkm_make_null(zan_arena_t *arena, zan_loc_t loc) {
  return zan_ast_new(arena, AST_NULL_LITERAL, loc);
}

static zan_ast_node_t *gkm_make_bool(zan_arena_t *arena, zan_loc_t loc,
                                     bool v) {
  zan_ast_node_t *lit = zan_ast_new(arena, AST_BOOL_LITERAL, loc);
  lit->bool_val = v;
  return lit;
}

static zan_ast_node_t *gkm_make_return(zan_arena_t *arena, zan_loc_t loc,
                                       zan_ast_node_t *val) {
  zan_ast_node_t *ret = zan_ast_new(arena, AST_RETURN_STMT, loc);
  ret->ret.value = val;
  return ret;
}

static zan_ast_node_t *gkm_make_null_guard(zan_arena_t *arena, zan_loc_t loc,
                                           zan_istr_t param_name,
                                           zan_ast_node_t *ret_val) {
  zan_ast_node_t *cmp = zan_ast_new(arena, AST_BINARY, loc);
  cmp->binary.op = TK_EQ_EQ;
  cmp->binary.left = gkm_make_ident(arena, loc, param_name);
  cmp->binary.right = gkm_make_null(arena, loc);

  zan_ast_node_t *then_blk = zan_ast_new(arena, AST_BLOCK, loc);
  zan_ast_list_init(&then_blk->block.stmts);
  zan_ast_list_push(&then_blk->block.stmts,
                    gkm_make_return(arena, loc, ret_val), arena);

  zan_ast_node_t *ifs = zan_ast_new(arena, AST_IF_STMT, loc);
  ifs->if_stmt.cond = cmp;
  ifs->if_stmt.then_body = then_blk;
  ifs->if_stmt.else_body = NULL;
  return ifs;
}

static zan_ast_node_t *gkm_make_create_expr(const gui_kind_entry_t *e,
                                            zan_arena_t *arena, zan_loc_t loc) {
  if (e->factory_method) {
    zan_ast_node_t *recv =
        gkm_make_ident(arena, loc, e->decl->type_decl.name);
    recv->ident.is_type_receiver = true;
    recv->ident.ns_decl = e->decl;

    zan_ast_node_t *mem = zan_ast_new(arena, AST_MEMBER_ACCESS, loc);
    mem->member.object = recv;
    mem->member.name = e->factory_method->method_decl.name;

    zan_ast_node_t *call = zan_ast_new(arena, AST_CALL, loc);
    call->call.callee = mem;
    zan_ast_list_init(&call->call.args);
    zan_ast_list_init(&call->call.type_args);
    return call;
  }

  zan_ast_node_t *tref = NULL;
  if (e->target_type) {
    tref = gkm_clone_type_ref(e->target_type, arena, loc);
  } else {
    tref = zan_ast_new(arena, AST_TYPE_REF, loc);
    tref->type_ref.name = e->decl->type_decl.name;
    tref->type_ref.ns_decl = e->decl;
    zan_ast_list_init(&tref->type_ref.type_args);
  }
  zan_ast_node_t *ne = zan_ast_new(arena, AST_NEW_EXPR, loc);
  ne->new_expr.type = tref;
  zan_ast_list_init(&ne->new_expr.args);
  zan_ast_list_init(&ne->new_expr.arg_inits);
  return ne;
}

static zan_ast_node_t *gkm_build_switch_body(const gui_kind_entry_t *entries,
                                             int count, zan_istr_t param_name,
                                             bool lower_mode, bool has_mode,
                                             zan_arena_t *arena,
                                             zan_loc_t loc) {
  zan_ast_node_t *body = zan_ast_new(arena, AST_BLOCK, loc);
  zan_ast_list_init(&body->block.stmts);

  zan_ast_node_t *guard_ret =
      has_mode ? gkm_make_bool(arena, loc, false) : gkm_make_null(arena, loc);
  zan_ast_list_push(&body->block.stmts,
                    gkm_make_null_guard(arena, loc, param_name, guard_ret),
                    arena);

  if (count == 0) {
    zan_ast_node_t *ret_val =
        has_mode ? gkm_make_bool(arena, loc, false) : gkm_make_null(arena, loc);
    zan_ast_list_push(&body->block.stmts,
                      gkm_make_return(arena, loc, ret_val), arena);
    return body;
  }

  zan_ast_node_t *sw = zan_ast_new(arena, AST_SWITCH_STMT, loc);
  sw->switch_stmt.expr = gkm_make_ident(arena, loc, param_name);
  zan_ast_list_init(&sw->switch_stmt.cases);

  for (int i = 0; i < count; i++) {
    if (lower_mode) {
      bool dup_lower = false;
      for (int p = 0; p < i; p++) {
        if (gkm_istr_eq_lower(entries[p].kind, entries[i].kind)) {
          dup_lower = true;
          break;
        }
      }
      if (dup_lower)
        continue;
    }

    zan_istr_t pat =
        lower_mode ? gkm_lower_istr(arena, entries[i].kind) : entries[i].kind;
    zan_ast_node_t *cblk = zan_ast_new(arena, AST_BLOCK, loc);
    zan_ast_list_init(&cblk->block.stmts);
    zan_ast_node_t *ret_val =
        has_mode ? gkm_make_bool(arena, loc, true)
                 : gkm_make_create_expr(&entries[i], arena, loc);
    zan_ast_list_push(&cblk->block.stmts, gkm_make_return(arena, loc, ret_val),
                      arena);

    zan_ast_node_t *sc = zan_ast_new(arena, AST_SWITCH_CASE, loc);
    sc->switch_case.pattern = gkm_make_str(arena, loc, pat);
    sc->switch_case.body = cblk;
    zan_ast_list_push(&sw->switch_stmt.cases, sc, arena);
  }

  zan_ast_node_t *dblk = zan_ast_new(arena, AST_BLOCK, loc);
  zan_ast_list_init(&dblk->block.stmts);
  zan_ast_node_t *dret =
      has_mode ? gkm_make_bool(arena, loc, false) : gkm_make_null(arena, loc);
  zan_ast_list_push(&dblk->block.stmts, gkm_make_return(arena, loc, dret),
                    arena);

  zan_ast_node_t *dsc = zan_ast_new(arena, AST_SWITCH_CASE, loc);
  dsc->switch_case.body = dblk;
  zan_ast_list_push(&sw->switch_stmt.cases, dsc, arena);

  zan_ast_list_push(&body->block.stmts, sw, arena);
  return body;
}

static zan_ast_node_t *gkm_build_kinds_body(const gui_kind_entry_t *entries,
                                            int count, zan_istr_t param_name,
                                            zan_arena_t *arena, zan_loc_t loc) {
  zan_ast_node_t *body = zan_ast_new(arena, AST_BLOCK, loc);
  zan_ast_list_init(&body->block.stmts);
  zan_ast_list_push(&body->block.stmts,
                    gkm_make_null_guard(arena, loc, param_name, NULL), arena);

  zan_istr_t add_name = gkm_intern_str(arena, "Add", 3);
  for (int i = 0; i < count; i++) {
    zan_ast_node_t *mem = zan_ast_new(arena, AST_MEMBER_ACCESS, loc);
    mem->member.object = gkm_make_ident(arena, loc, param_name);
    mem->member.name = add_name;

    zan_ast_node_t *call = zan_ast_new(arena, AST_CALL, loc);
    call->call.callee = mem;
    zan_ast_list_init(&call->call.args);
    zan_ast_list_init(&call->call.type_args);
    zan_ast_list_push(&call->call.args,
                      gkm_make_str(arena, loc, entries[i].kind), arena);

    zan_ast_node_t *es = zan_ast_new(arena, AST_EXPR_STMT, loc);
    es->expr_stmt.expr = call;
    zan_ast_list_push(&body->block.stmts, es, arena);
  }
  return body;
}

static void zan_synthesize_gui_kind_manifests(zan_ast_node_t *unit,
                                              zan_arena_t *arena,
                                              zan_diag_t *diag) {
  if (!unit || unit->kind != AST_COMPILATION_UNIT)
    return;

  zan_ast_list_t *decls = &unit->comp_unit.decls;
  bool has_any_manifest = false;
  for (int i = 0; i < decls->count && !has_any_manifest; i++) {
    zan_ast_node_t *d = decls->items[i];
    if (!d || (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL))
      continue;
    for (int m = 0; m < d->type_decl.members.count; m++) {
      zan_ast_node_t *mem = d->type_decl.members.items[m];
      if (!mem || mem->kind != AST_METHOD_DECL || !mem->meta)
        continue;
      for (int ai = 0; ai < mem->meta->attributes.count; ai++) {
        if (gkm_is_attr(mem->meta->attributes.items[ai], "GuiKindManifest",
                        "GuiKindManifestAttribute")) {
          has_any_manifest = true;
          break;
        }
      }
      if (has_any_manifest)
        break;
    }
  }
  if (!has_any_manifest)
    return;

  int cap = 64;
  int count = 0;
  gui_kind_entry_t *entries =
      (gui_kind_entry_t *)malloc((size_t)cap * sizeof(gui_kind_entry_t));
  if (!entries)
    return;

  for (int i = 0; i < decls->count; i++) {
    zan_ast_node_t *d = decls->items[i];
    if (!d || (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL))
      continue;

    if (d->meta) {
      for (int ai = 0; ai < d->meta->attributes.count; ai++) {
        zan_ast_node_t *a = d->meta->attributes.items[ai];
        if (!gkm_is_attr(a, "GuiKind", "GuiKindAttribute"))
          continue;
        zan_istr_t kind = {0};
        zan_ast_node_t *tt = NULL;
        if (!gkm_extract_gui_kind(a, diag, &kind, &tt))
          continue;
        if (!tt && d->type_decl.type_params.count > 0) {
          zan_diag_emit(diag, DIAG_ERROR, a->loc,
                        "[GuiKind] on generic type '%.*s' requires explicit "
                        "typeof(...) target type",
                        (int)d->type_decl.name.len, d->type_decl.name.str);
          continue;
        }
        bool dup = false;
        for (int k = 0; k < count; k++) {
          if (gkm_istr_eq_istr(entries[k].kind, kind)) {
            zan_diag_emit(diag, DIAG_ERROR, a->loc,
                          "duplicate [GuiKind] key '%.*s'", (int)kind.len,
                          kind.str);
            dup = true;
            break;
          }
        }
        if (dup)
          continue;
        if (count >= cap) {
          int ncap = cap * 2;
          gui_kind_entry_t *grown = (gui_kind_entry_t *)realloc(
              entries, (size_t)ncap * sizeof(gui_kind_entry_t));
          if (!grown)
            break;
          entries = grown;
          cap = ncap;
        }
        entries[count++] = (gui_kind_entry_t){
            .kind = kind,
            .decl = d,
            .factory_method = NULL,
            .target_type = tt,
            .loc = a->loc,
        };
      }
    }

    for (int m = 0; m < d->type_decl.members.count; m++) {
      zan_ast_node_t *mem = d->type_decl.members.items[m];
      if (!mem || mem->kind != AST_METHOD_DECL || !mem->meta)
        continue;
      for (int ai = 0; ai < mem->meta->attributes.count; ai++) {
        zan_ast_node_t *a = mem->meta->attributes.items[ai];
        if (!gkm_is_attr(a, "GuiKind", "GuiKindAttribute"))
          continue;
        zan_istr_t kind = {0};
        zan_ast_node_t *tt = NULL;
        if (!gkm_extract_gui_kind(a, diag, &kind, &tt))
          continue;
        if ((mem->method_decl.modifiers & MOD_STATIC) == 0 ||
            mem->method_decl.params.count != 0 ||
            mem->method_decl.type_params.count != 0 ||
            d->type_decl.type_params.count != 0) {
          zan_diag_emit(diag, DIAG_ERROR, a->loc,
                        "[GuiKind] on method '%.*s' requires a non-generic "
                        "parameterless static method on a non-generic type",
                        (int)mem->method_decl.name.len,
                        mem->method_decl.name.str);
          continue;
        }
        bool dup = false;
        for (int k = 0; k < count; k++) {
          if (gkm_istr_eq_istr(entries[k].kind, kind)) {
            zan_diag_emit(diag, DIAG_ERROR, a->loc,
                          "duplicate [GuiKind] key '%.*s'", (int)kind.len,
                          kind.str);
            dup = true;
            break;
          }
        }
        if (dup)
          continue;
        if (count >= cap) {
          int ncap = cap * 2;
          gui_kind_entry_t *grown = (gui_kind_entry_t *)realloc(
              entries, (size_t)ncap * sizeof(gui_kind_entry_t));
          if (!grown)
            break;
          entries = grown;
          cap = ncap;
        }
        entries[count++] = (gui_kind_entry_t){
            .kind = kind,
            .decl = d,
            .factory_method = mem,
            .target_type = tt,
            .loc = a->loc,
        };
      }
    }
  }

  for (int i = 0; i < decls->count; i++) {
    zan_ast_node_t *d = decls->items[i];
    if (!d || (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL))
      continue;
    for (int m = 0; m < d->type_decl.members.count; m++) {
      zan_ast_node_t *mem = d->type_decl.members.items[m];
      if (!mem || mem->kind != AST_METHOD_DECL || !mem->meta)
        continue;
      for (int ai = 0; ai < mem->meta->attributes.count; ai++) {
        zan_ast_node_t *a = mem->meta->attributes.items[ai];
        if (!gkm_is_attr(a, "GuiKindManifest", "GuiKindManifestAttribute"))
          continue;
        zan_istr_t mode = {0};
        if (!gkm_extract_manifest_mode(a, diag, &mode))
          continue;
        if (mem->method_decl.params.count != 1 ||
            !mem->method_decl.params.items[0] ||
            mem->method_decl.params.items[0]->kind != AST_PARAM) {
          zan_diag_emit(diag, DIAG_ERROR, mem->loc,
                        "[GuiKindManifest] method '%.*s' must accept "
                        "exactly 1 parameter",
                        (int)mem->method_decl.name.len,
                        mem->method_decl.name.str);
          continue;
        }
        bool synthesized = false;
        zan_istr_t param_name = mem->method_decl.params.items[0]->param.name;
        if (gkm_istr_eq(mode, "create")) {
          mem->method_decl.body = gkm_build_switch_body(
              entries, count, param_name, false, false, arena, mem->loc);
          synthesized = true;
        } else if (gkm_istr_eq(mode, "createLower")) {
          mem->method_decl.body = gkm_build_switch_body(
              entries, count, param_name, true, false, arena, mem->loc);
          synthesized = true;
        } else if (gkm_istr_eq(mode, "has")) {
          mem->method_decl.body = gkm_build_switch_body(
              entries, count, param_name, false, true, arena, mem->loc);
          synthesized = true;
        } else if (gkm_istr_eq(mode, "kinds")) {
          mem->method_decl.body = gkm_build_kinds_body(
              entries, count, param_name, arena, mem->loc);
          synthesized = true;
        } else {
          zan_diag_emit(diag, DIAG_ERROR, a->loc,
                        "unknown [GuiKindManifest] mode '%.*s'",
                        (int)mode.len, mode.str);
        }
        if (synthesized) {
          mem->method_decl.modifiers &= ~(uint32_t)MOD_EXTERN;
        }
      }
    }
  }

  free(entries);
}
