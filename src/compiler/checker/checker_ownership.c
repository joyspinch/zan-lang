/* Pillar 8: Progressive ownership (~Copyable move-only types) and
 * stack-scoped ref struct / Span<T> escape safety.
 *
 * Diagnostic codes:
 *   E2151: copyable type / static member cannot contain ~Copyable field
 *   E2152: use of moved ~Copyable value (or move inside loop)
 *   E2153: ~Copyable value cannot be captured by an escaping lambda
 *   E2154: ~Copyable value cannot be boxed to object or interface
 *   E2155: ref struct / Span<T> cannot be a field of class/non-ref struct/static
 *   E2156: ref struct / Span<T> cannot be boxed to object or interface
 *   E2157: ref struct / Span<T> cannot be captured by an escaping lambda
 *   E2158: ref struct / Span<T> cannot be used in an async method
 *   E2159: ref struct / Span<T> cannot be array element, nullable, or heap generic arg
 *   E2160: cannot return a Span / ref struct derived from local stack storage
 */

static bool checker_type_is_ref_struct(const zan_type_t *t) {
  if (!t || t->kind != TYPE_STRUCT)
    return false;
  if ((t->name.len == 4 && memcmp(t->name.str, "Span", 4) == 0) ||
      (t->name.len == 12 && memcmp(t->name.str, "ReadOnlySpan", 12) == 0))
    return true;
  if (t->sym) {
    if (t->sym->modifiers & MOD_REF)
      return true;
    if (t->sym->decl && (t->sym->decl->kind == AST_STRUCT_DECL) &&
        (t->sym->decl->type_decl.modifiers & MOD_REF))
      return true;
  }
  return false;
}

static bool checker_type_is_noncopyable(const zan_type_t *t) {
  if (!t || (t->kind != TYPE_STRUCT && t->kind != TYPE_CLASS))
    return false;
  if (t->sym) {
    if (t->sym->modifiers & MOD_NONCOPYABLE)
      return true;
    if (t->sym->decl && (t->sym->decl->kind == AST_STRUCT_DECL ||
                         t->sym->decl->kind == AST_CLASS_DECL) &&
        (t->sym->decl->type_decl.modifiers & MOD_NONCOPYABLE))
      return true;
  }
  return false;
}

static bool checker_type_is_inline_array(const zan_type_t *t) {
  if (!t || t->kind != TYPE_STRUCT || t->sym != NULL)
    return false;
  return t->name.len == 11 && memcmp(t->name.str, "InlineArray", 11) == 0;
}

static void checker_check_ownership_type_ref_depth(zan_checker_t *c,
                                                   zan_type_t *t, zan_loc_t loc,
                                                   int depth) {
  if (!t || depth > 8 || t == c->binder->type_error)
    return;
  if (t->kind == TYPE_ARRAY) {
    if (checker_type_is_ref_struct(t->element_type)) {
      zan_diag_emit_code(
          c->diag, DIAG_ERROR, loc, 2159,
          "stack-only type '%s' cannot be used as an array element type",
          type_name(t->element_type));
      return;
    }
    checker_check_ownership_type_ref_depth(c, t->element_type, loc, depth + 1);
    return;
  }
  if (t->kind == TYPE_NULLABLE) {
    if (checker_type_is_ref_struct(t->element_type)) {
      zan_diag_emit_code(
          c->diag, DIAG_ERROR, loc, 2159,
          "stack-only type '%s' cannot be wrapped in a nullable type",
          type_name(t->element_type));
      return;
    }
    checker_check_ownership_type_ref_depth(c, t->element_type, loc, depth + 1);
    return;
  }
  if (t->kind == TYPE_TASK) {
    for (int i = 0; i < t->type_arg_count; i++) {
      if (checker_type_is_ref_struct(t->type_args[i])) {
        zan_diag_emit_code(
            c->diag, DIAG_ERROR, loc, 2159,
            "stack-only type '%s' cannot be used as a Task type argument",
            type_name(t->type_args[i]));
        return;
      }
      checker_check_ownership_type_ref_depth(c, t->type_args[i], loc,
                                             depth + 1);
    }
    return;
  }
  if (t->type_arg_count > 0) {
    bool allow_ref_arg = checker_type_is_ref_struct(t);
    for (int i = 0; i < t->type_arg_count; i++) {
      zan_type_t *arg = t->type_args[i];
      if (!allow_ref_arg && checker_type_is_ref_struct(arg)) {
        zan_diag_emit_code(
            c->diag, DIAG_ERROR, loc, 2159,
            "stack-only type '%s' cannot be used as a type argument to '%s'",
            type_name(arg), type_name(t));
        return;
      }
      checker_check_ownership_type_ref_depth(c, arg, loc, depth + 1);
    }
  }
}

static void checker_check_ownership_type_ref(zan_checker_t *c, zan_type_t *t,
                                             zan_loc_t loc) {
  checker_check_ownership_type_ref_depth(c, t, loc, 0);
}

static bool checker_check_ownership_boxing(zan_checker_t *c, zan_type_t *target,
                                           zan_type_t *value, zan_loc_t loc) {
  if (!target || !value || target == c->binder->type_error ||
      value == c->binder->type_error)
    return false;
  if (target->kind != TYPE_OBJECT && target->kind != TYPE_INTERFACE)
    return false;
  if (checker_type_is_noncopyable(value)) {
    zan_diag_emit_code(
        c->diag, DIAG_ERROR, loc, 2154,
        "non-copyable type '%s' cannot be boxed or converted to '%s'",
        type_name(value), type_name(target));
    return true;
  }
  if (checker_type_is_ref_struct(value)) {
    zan_diag_emit_code(
        c->diag, DIAG_ERROR, loc, 2156,
        "stack-only ref struct '%s' cannot be boxed or converted to '%s'",
        type_name(value), type_name(target));
    return true;
  }
  return false;
}

static void checker_check_type_ownership_decls(zan_checker_t *c,
                                               zan_ast_node_t *decl) {
  if (!decl)
    return;
  bool decl_is_ref_struct =
      decl->kind == AST_STRUCT_DECL && (decl->type_decl.modifiers & MOD_REF);
  bool decl_is_noncopy = (decl->type_decl.modifiers & MOD_NONCOPYABLE) != 0;

  for (int j = 0; j < decl->type_decl.members.count; j++) {
    zan_ast_node_t *m = decl->type_decl.members.items[j];
    if (!m || (m->kind != AST_FIELD_DECL && m->kind != AST_PROPERTY_DECL))
      continue;
    if (!m->field_decl.type)
      continue;
    zan_type_t *ft = zan_binder_resolve_type(c->binder, m->field_decl.type);
    if (!ft || ft == c->binder->type_error)
      continue;
    checker_check_ownership_type_ref(c, ft, m->loc);
    bool is_static = (m->field_decl.modifiers & MOD_STATIC) != 0;
    if (checker_type_is_noncopyable(ft)) {
      if (is_static || !decl_is_noncopy) {
        zan_diag_emit_code(
            c->diag, DIAG_ERROR, m->loc, 2151,
            "copyable type or static member '%.*s.%.*s' cannot contain "
            "non-copyable (~Copyable) type '%s'",
            (int)decl->type_decl.name.len, decl->type_decl.name.str,
            (int)m->field_decl.name.len, m->field_decl.name.str, type_name(ft));
      }
    }
    if (checker_type_is_ref_struct(ft)) {
      if (is_static || !decl_is_ref_struct) {
        zan_diag_emit_code(
            c->diag, DIAG_ERROR, m->loc, 2155,
            "stack-only type '%s' cannot be declared as field '%.*s.%.*s' "
            "(only instance fields of a 'ref struct' are allowed)",
            type_name(ft), (int)decl->type_decl.name.len,
            decl->type_decl.name.str, (int)m->field_decl.name.len,
            m->field_decl.name.str);
      }
    }
  }
}

/* ---- Method-level control-flow ownership & stack-escape analysis ---- */

#define OWN_MAX_SLOTS 128

typedef struct {
  zan_istr_t name;
  zan_type_t *type;
  int loop_depth;
  bool is_noncopy;
  bool is_ref_struct;
  bool is_local_inline_array;
  bool is_ref_local;
  bool is_byref_param;
  bool is_scoped_param;
} own_slot_t;

typedef struct {
  uint64_t lo;
  uint64_t hi;
} own_bits_t;

typedef struct {
  zan_checker_t *c;
  own_slot_t slots[OWN_MAX_SLOTS];
  int slot_count;
  own_bits_t moved;
  own_bits_t stack_derived;
  own_bits_t crossed_await;
  int loop_depth;
  bool is_async;
  bool await_repass;
  bool is_ref_return;
} own_ctx_t;

static bool own_bit_get(own_bits_t b, int i) {
  if (i < 0 || i >= OWN_MAX_SLOTS)
    return false;
  return i < 64 ? ((b.lo >> i) & 1ULL) != 0 : ((b.hi >> (i - 64)) & 1ULL) != 0;
}

static void own_bit_set(own_bits_t *b, int i) {
  if (i < 0 || i >= OWN_MAX_SLOTS)
    return;
  if (i < 64)
    b->lo |= (1ULL << i);
  else
    b->hi |= (1ULL << (i - 64));
}

static void own_bit_clear(own_bits_t *b, int i) {
  if (i < 0 || i >= OWN_MAX_SLOTS)
    return;
  if (i < 64)
    b->lo &= ~(1ULL << i);
  else
    b->hi &= ~(1ULL << (i - 64));
}

static own_bits_t own_bits_or(own_bits_t a, own_bits_t b) {
  own_bits_t r;
  r.lo = a.lo | b.lo;
  r.hi = a.hi | b.hi;
  return r;
}

static int own_find_slot(own_ctx_t *ctx, zan_istr_t name) {
  for (int i = ctx->slot_count - 1; i >= 0; i--) {
    if (ctx->slots[i].name.len == name.len &&
        memcmp(ctx->slots[i].name.str, name.str, (size_t)name.len) == 0)
      return i;
  }
  return -1;
}

static int own_add_slot(own_ctx_t *ctx, zan_istr_t name, zan_type_t *type,
                        bool is_local_inline_array, bool is_stack_derived) {
  if (ctx->slot_count >= OWN_MAX_SLOTS)
    return -1;
  int idx = ctx->slot_count++;
  own_slot_t *s = &ctx->slots[idx];
  s->name = name;
  s->type = type;
  s->loop_depth = ctx->loop_depth;
  s->is_noncopy = checker_type_is_noncopyable(type);
  s->is_ref_struct = checker_type_is_ref_struct(type);
  s->is_local_inline_array = is_local_inline_array;
  s->is_ref_local = false;
  s->is_byref_param = false;
  s->is_scoped_param = false;
  own_bit_clear(&ctx->moved, idx);
  own_bit_clear(&ctx->crossed_await, idx);
  if (is_stack_derived)
    own_bit_set(&ctx->stack_derived, idx);
  else
    own_bit_clear(&ctx->stack_derived, idx);
  return idx;
}

static zan_type_t *own_infer_expr_type(own_ctx_t *ctx, zan_ast_node_t *expr);

static zan_type_t *own_lookup_member_type(own_ctx_t *ctx, zan_symbol_t *tsym,
                                          zan_istr_t mname) {
  if (!tsym)
    return NULL;
  for (int i = 0; i < tsym->member_count; i++) {
    zan_symbol_t *m = tsym->members[i];
    if (!m || m->name.len != mname.len ||
        memcmp(m->name.str, mname.str, (size_t)mname.len) != 0)
      continue;
    if (m->kind == SYM_METHOD) {
      if (m->decl && m->decl->method_decl.return_type)
        return zan_binder_resolve_type(ctx->c->binder,
                                       m->decl->method_decl.return_type);
      return m->type;
    }
    if (m->kind == SYM_FIELD || m->kind == SYM_PROPERTY)
      return m->type;
  }
  return NULL;
}

static zan_type_t *own_infer_expr_type(own_ctx_t *ctx, zan_ast_node_t *expr) {
  if (!expr)
    return NULL;
  switch (expr->kind) {
  case AST_IDENTIFIER: {
    int idx = own_find_slot(ctx, expr->ident.name);
    if (idx >= 0)
      return ctx->slots[idx].type;
    if (ctx->c->current_type_sym) {
      zan_symbol_t *f =
          checker_find_field(ctx->c->current_type_sym, expr->ident.name);
      if (f)
        return f->type;
    }
    return NULL;
  }
  case AST_NEW_EXPR: {
    if (!expr->new_expr.type)
      return NULL;
    zan_type_t *t =
        zan_binder_resolve_type(ctx->c->binder, expr->new_expr.type);
    if (expr->new_expr.is_array && t && t->kind != TYPE_ARRAY)
      t = zan_binder_make_array_type(ctx->c->binder, t);
    return t;
  }
  case AST_CAST_EXPR:
    return expr->cast.type
               ? zan_binder_resolve_type(ctx->c->binder, expr->cast.type)
               : NULL;
  case AST_STACKALLOC_EXPR: {
    zan_type_t *et = expr->cast.type
                         ? zan_binder_resolve_type(ctx->c->binder,
                                                   expr->cast.type)
                         : NULL;
    return et ? zan_binder_make_span_type(ctx->c->binder, et) : NULL;
  }
  case AST_MEMBER_ACCESS: {
    zan_type_t *ot = own_infer_expr_type(ctx, expr->member.object);
    if (ot && ot->sym)
      return own_lookup_member_type(ctx, ot->sym, expr->member.name);
    if (expr->member.object && expr->member.object->kind == AST_IDENTIFIER) {
      zan_symbol_t *ts =
          zan_binder_lookup(ctx->c->binder, expr->member.object->ident.name);
      if (ts)
        return own_lookup_member_type(ctx, ts, expr->member.name);
    }
    return NULL;
  }
  case AST_CALL: {
    zan_ast_node_t *callee = expr->call.callee;
    if (!callee)
      return NULL;
    if (callee->kind == AST_IDENTIFIER) {
      if (ctx->c->current_type_sym)
        return own_lookup_member_type(ctx, ctx->c->current_type_sym,
                                      callee->ident.name);
      return NULL;
    }
    if (callee->kind == AST_MEMBER_ACCESS) {
      zan_istr_t mn = callee->member.name;
      zan_type_t *ot = own_infer_expr_type(ctx, callee->member.object);
      if (mn.len == 6 && memcmp(mn.str, "AsSpan", 6) == 0) {
        zan_type_t *elem =
            (ot && ot->kind == TYPE_ARRAY)
                ? ot->element_type
                : (ot && checker_type_is_inline_array(ot) &&
                   ot->type_arg_count >= 2)
                      ? ot->type_args[1]
                      : ctx->c->binder->type_int;
        return zan_binder_make_span_type(ctx->c->binder, elem);
      }
      if (mn.len == 14 && memcmp(mn.str, "AsReadOnlySpan", 14) == 0) {
        zan_type_t *elem =
            (ot && ot->kind == TYPE_ARRAY)
                ? ot->element_type
                : (ot && checker_type_is_inline_array(ot) &&
                   ot->type_arg_count >= 2)
                      ? ot->type_args[1]
                      : ctx->c->binder->type_int;
        return zan_binder_make_readonly_span_type(ctx->c->binder, elem);
      }
      if (mn.len == 5 && memcmp(mn.str, "Slice", 5) == 0 && ot &&
          checker_type_is_ref_struct(ot))
        return ot;
      if (ot && ot->sym)
        return own_lookup_member_type(ctx, ot->sym, mn);
      if (callee->member.object &&
          callee->member.object->kind == AST_IDENTIFIER) {
        zan_symbol_t *ts =
            zan_binder_lookup(ctx->c->binder, callee->member.object->ident.name);
        if (ts)
          return own_lookup_member_type(ctx, ts, mn);
      }
    }
    return NULL;
  }
  case AST_CONDITIONAL: {
    zan_type_t *t = own_infer_expr_type(ctx, expr->conditional.then_expr);
    return t ? t : own_infer_expr_type(ctx, expr->conditional.else_expr);
  }
  default:
    return NULL;
  }
}

static bool own_expr_is_stack_derived(own_ctx_t *ctx, zan_ast_node_t *expr) {
  if (!expr)
    return false;
  switch (expr->kind) {
  case AST_STACKALLOC_EXPR:
    return true;
  case AST_IDENTIFIER: {
    int idx = own_find_slot(ctx, expr->ident.name);
    if (idx < 0)
      return false;
    return own_bit_get(ctx->stack_derived, idx) ||
           ctx->slots[idx].is_local_inline_array;
  }
  case AST_CALL: {
    zan_ast_node_t *callee = expr->call.callee;
    if (callee && callee->kind == AST_MEMBER_ACCESS) {
      zan_istr_t mn = callee->member.name;
      if ((mn.len == 6 && memcmp(mn.str, "AsSpan", 6) == 0) ||
          (mn.len == 14 && memcmp(mn.str, "AsReadOnlySpan", 14) == 0) ||
          (mn.len == 5 && memcmp(mn.str, "Slice", 5) == 0)) {
        return own_expr_is_stack_derived(ctx, callee->member.object);
      }
    }
    return false;
  }
  case AST_NEW_EXPR: {
    zan_type_t *nt = own_infer_expr_type(ctx, expr);
    if (!checker_type_is_ref_struct(nt))
      return false;
    for (int i = 0; i < expr->new_expr.args.count; i++) {
      if (own_expr_is_stack_derived(ctx, expr->new_expr.args.items[i]))
        return true;
    }
    return false;
  }
  case AST_MEMBER_ACCESS:
    return own_expr_is_stack_derived(ctx, expr->member.object);
  case AST_CONDITIONAL:
    return own_expr_is_stack_derived(ctx, expr->conditional.then_expr) ||
           own_expr_is_stack_derived(ctx, expr->conditional.else_expr);
  default:
    return false;
  }
}

/* Returns true if taking a `ref` to `expr` would alias local stack storage
 * (local variable, by-value param, scoped ref param, stackalloc Span, local
 * InlineArray, or instance field of a struct `this`), which cannot escape via
 * `return ref`. */
static bool own_ref_expr_is_stack_local(own_ctx_t *ctx, zan_ast_node_t *expr) {
  if (!expr)
    return false;
  switch (expr->kind) {
  case AST_IDENTIFIER: {
    int idx = own_find_slot(ctx, expr->ident.name);
    if (idx >= 0) {
      own_slot_t *s = &ctx->slots[idx];
      if (s->is_ref_local)
        return own_bit_get(ctx->stack_derived, idx);
      if (s->is_byref_param)
        return s->is_scoped_param;
      return true;
    }
    if (ctx->c->current_type_sym &&
        ctx->c->current_type_sym->kind == SYM_STRUCT) {
      zan_symbol_t *f =
          checker_find_field(ctx->c->current_type_sym, expr->ident.name);
      if (f && !(f->modifiers & MOD_STATIC))
        return true;
    }
    return false;
  }
  case AST_THIS_EXPR:
    return ctx->c->current_type_sym &&
           ctx->c->current_type_sym->kind == SYM_STRUCT;
  case AST_INDEX: {
    if (own_expr_is_stack_derived(ctx, expr->index.object))
      return true;
    zan_type_t *bt = own_infer_expr_type(ctx, expr->index.object);
    if (bt && checker_type_is_inline_array(bt))
      return own_ref_expr_is_stack_local(ctx, expr->index.object);
    return false;
  }
  case AST_MEMBER_ACCESS: {
    zan_type_t *ot = own_infer_expr_type(ctx, expr->member.object);
    if (ot && ot->kind == TYPE_STRUCT)
      return own_ref_expr_is_stack_local(ctx, expr->member.object);
    return false;
  }
  case AST_CALL: {
    if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
      zan_type_t *ot =
          own_infer_expr_type(ctx, expr->call.callee->member.object);
      if (ot && ot->kind == TYPE_STRUCT &&
          own_ref_expr_is_stack_local(ctx, expr->call.callee->member.object))
        return true;
    }
    for (int i = 0; i < expr->call.args.count; i++) {
      zan_ast_node_t *a = expr->call.args.items[i];
      if (a && a->kind == AST_REF_ARG &&
          own_ref_expr_is_stack_local(ctx, a->ref_arg.expr))
        return true;
      if (a && own_expr_is_stack_derived(ctx, a))
        return true;
    }
    return false;
  }
  case AST_CONDITIONAL:
    return own_ref_expr_is_stack_local(ctx, expr->conditional.then_expr) ||
           own_ref_expr_is_stack_local(ctx, expr->conditional.else_expr);
  default:
    return false;
  }
}

static void own_check_expr(own_ctx_t *ctx, zan_ast_node_t *expr,
                           bool consume_value);
static void own_check_stmt(own_ctx_t *ctx, zan_ast_node_t *stmt);

static void own_check_expr(own_ctx_t *ctx, zan_ast_node_t *expr,
                           bool consume_value) {
  if (!expr)
    return;
  switch (expr->kind) {
  case AST_IDENTIFIER: {
    int idx = own_find_slot(ctx, expr->ident.name);
    if (idx < 0)
      return;
    if (ctx->is_async && ctx->slots[idx].is_ref_struct &&
        own_bit_get(ctx->crossed_await, idx)) {
      zan_diag_emit_code(
          ctx->c->diag, DIAG_ERROR, expr->loc, 2158,
          "stack-only type '%s' ('%.*s') cannot be preserved across an "
          "'await' boundary",
          type_name(ctx->slots[idx].type), (int)expr->ident.name.len,
          expr->ident.name.str);
      /* Avoid duplicate diagnostics on repeated uses of the same local */
      own_bit_clear(&ctx->crossed_await, idx);
    }
    if (!ctx->slots[idx].is_noncopy || ctx->await_repass)
      return;
    if (own_bit_get(ctx->moved, idx)) {
      zan_diag_emit_code(
          ctx->c->diag, DIAG_ERROR, expr->loc, 2152,
          "use of moved non-copyable (~Copyable) value '%.*s'",
          (int)expr->ident.name.len, expr->ident.name.str);
      return;
    }
    if (consume_value) {
      if (ctx->loop_depth > ctx->slots[idx].loop_depth) {
        zan_diag_emit_code(
            ctx->c->diag, DIAG_ERROR, expr->loc, 2152,
            "cannot move non-copyable (~Copyable) value '%.*s' inside a loop "
            "(declared outside the loop)",
            (int)expr->ident.name.len, expr->ident.name.str);
      }
      own_bit_set(&ctx->moved, idx);
    }
    return;
  }
  case AST_AWAIT_EXPR: {
    own_check_expr(ctx, expr->await_expr.expr, true);
    if (ctx->is_async) {
      for (int i = 0; i < ctx->slot_count; i++) {
        if (ctx->slots[i].is_ref_struct)
          own_bit_set(&ctx->crossed_await, i);
      }
    }
    return;
  }
  case AST_REF_ARG: {
    zan_ast_node_t *inner = expr->ref_arg.expr;
    if (expr->ref_arg.is_out) {
      if (inner && inner->kind == AST_IDENTIFIER) {
        int idx = own_find_slot(ctx, inner->ident.name);
        if (idx >= 0) {
          own_bit_clear(&ctx->moved, idx);
          own_bit_clear(&ctx->stack_derived, idx);
          own_bit_clear(&ctx->crossed_await, idx);
        }
      }
    } else {
      /* `ref x` borrows x without moving it, but x must not already be moved */
      own_check_expr(ctx, inner, false);
    }
    return;
  }
  case AST_NAMED_ARG:
    own_check_expr(ctx, expr->named_arg.expr, consume_value);
    return;
  case AST_MEMBER_ACCESS:
    /* Reading a field or calling a method on `x` borrows `x` (does not move) */
    own_check_expr(ctx, expr->member.object, false);
    return;
  case AST_INDEX:
    own_check_expr(ctx, expr->index.object, false);
    own_check_expr(ctx, expr->index.index, true);
    return;
  case AST_CALL:
    own_check_expr(ctx, expr->call.callee, false);
    for (int i = 0; i < expr->call.args.count; i++) {
      zan_ast_node_t *arg = expr->call.args.items[i];
      if (arg && arg->kind == AST_REF_ARG)
        own_check_expr(ctx, arg, false);
      else
        own_check_expr(ctx, arg, true);
    }
    return;
  case AST_NEW_EXPR:
    if (expr->new_expr.call_init)
      own_check_expr(ctx, expr->new_expr.call_init, true);
    for (int i = 0; i < expr->new_expr.args.count; i++) {
      zan_ast_node_t *arg = expr->new_expr.args.items[i];
      if (arg && arg->kind == AST_REF_ARG)
        own_check_expr(ctx, arg, false);
      else
        own_check_expr(ctx, arg, true);
    }
    for (int i = 0; i < expr->new_expr.arg_inits.count; i++)
      own_check_expr(ctx, expr->new_expr.arg_inits.items[i], true);
    return;
  case AST_ASSIGNMENT: {
    /* Evaluate RHS first so `h = Transform(h)` moves old h then re-inits h */
    own_check_expr(ctx, expr->binary.right, true);
    zan_ast_node_t *lhs = expr->binary.left;
    if (lhs && lhs->kind == AST_IDENTIFIER) {
      int idx = own_find_slot(ctx, lhs->ident.name);
      if (idx >= 0) {
        if (ctx->slots[idx].is_ref_local) {
          own_check_expr(ctx, lhs, false);
        } else if (expr->binary.op == TK_EQ) {
          own_bit_clear(&ctx->moved, idx);
          own_bit_clear(&ctx->crossed_await, idx);
          if (own_expr_is_stack_derived(ctx, expr->binary.right))
            own_bit_set(&ctx->stack_derived, idx);
          else
            own_bit_clear(&ctx->stack_derived, idx);
        } else {
          own_check_expr(ctx, lhs, false);
        }
        return;
      }
    }
    own_check_expr(ctx, lhs, false);
    return;
  }
  case AST_BINARY:
    own_check_expr(ctx, expr->binary.left, false);
    own_check_expr(ctx, expr->binary.right, false);
    return;
  case AST_UNARY:
    own_check_expr(ctx, expr->unary.operand, false);
    return;
  case AST_POSTFIX_UNARY:
    own_check_expr(ctx, expr->unary.operand, consume_value);
    return;
  case AST_CAST_EXPR:
    own_check_expr(ctx, expr->cast.expr, consume_value);
    return;
  case AST_STACKALLOC_EXPR:
    own_check_expr(ctx, expr->cast.expr, false);
    return;
  case AST_CONDITIONAL: {
    own_check_expr(ctx, expr->conditional.cond, false);
    own_bits_t base_moved = ctx->moved;
    own_bits_t base_stack = ctx->stack_derived;
    own_bits_t base_await = ctx->crossed_await;
    own_check_expr(ctx, expr->conditional.then_expr, consume_value);
    own_bits_t then_moved = ctx->moved;
    own_bits_t then_stack = ctx->stack_derived;
    own_bits_t then_await = ctx->crossed_await;
    ctx->moved = base_moved;
    ctx->stack_derived = base_stack;
    ctx->crossed_await = base_await;
    own_check_expr(ctx, expr->conditional.else_expr, consume_value);
    ctx->moved = own_bits_or(then_moved, ctx->moved);
    ctx->stack_derived = own_bits_or(then_stack, ctx->stack_derived);
    ctx->crossed_await = own_bits_or(then_await, ctx->crossed_await);
    return;
  }
  case AST_LAMBDA: {
    /* For non-escaping lambdas, inspect body in nested scope (no moves of outer
     * non-copyable bindings allowed without consuming; borrow is fine).
     * Escaping lambda captures of ~Copyable / ref struct are rejected in
     * checker_expr_core.c (E2153 / E2157). */
    int saved_count = ctx->slot_count;
    int saved_loop = ctx->loop_depth;
    /* Treat outer ~Copyable as borrowed-only inside lambda body (loop_depth+1
     * prevents moving outer ~Copyable bindings out of a closure). */
    ctx->loop_depth++;
    for (int i = 0; i < expr->lambda.params.count; i++) {
      zan_ast_node_t *p = expr->lambda.params.items[i];
      if (p && p->kind == AST_PARAM) {
        zan_type_t *pt = p->param.type
                             ? zan_binder_resolve_type(ctx->c->binder, p->param.type)
                             : NULL;
        own_add_slot(ctx, p->param.name, pt, false, p->param.is_scoped != 0);
      }
    }
    if (expr->lambda.body) {
      if (expr->lambda.body->kind == AST_BLOCK)
        own_check_stmt(ctx, expr->lambda.body);
      else
        own_check_expr(ctx, expr->lambda.body, false);
    }
    ctx->loop_depth = saved_loop;
    ctx->slot_count = saved_count;
    return;
  }
  default: {
    int n = zan_ast_child_count(expr);
    for (int i = 0; i < n; i++) {
      zan_ast_node_t *ch = zan_ast_child_at(expr, i);
      if (ch)
        own_check_expr(ctx, ch, false);
    }
    return;
  }
  }
}

static void own_check_stmt(own_ctx_t *ctx, zan_ast_node_t *stmt) {
  if (!stmt)
    return;
  switch (stmt->kind) {
  case AST_BLOCK: {
    int saved_count = ctx->slot_count;
    for (int i = 0; i < stmt->block.stmts.count; i++)
      own_check_stmt(ctx, stmt->block.stmts.items[i]);
    ctx->slot_count = saved_count;
    return;
  }
  case AST_VAR_DECL: {
    zan_type_t *vt = stmt->var_decl.type
                         ? zan_binder_resolve_type(ctx->c->binder,
                                                   stmt->var_decl.type)
                         : own_infer_expr_type(ctx, stmt->var_decl.initializer);
    if (vt) {
      checker_check_ownership_type_ref(ctx->c, vt, stmt->loc);
      /* C# 13 semantics: stack-only locals (Span<T> / ref struct) are allowed
       * in async methods as long as they are NOT preserved across an `await`
       * suspension boundary (checked via ctx->crossed_await -> E2158). */
    }
    bool stack_src =
        stmt->var_decl.initializer &&
        (stmt->var_decl.is_ref
             ? own_ref_expr_is_stack_local(ctx, stmt->var_decl.initializer)
             : own_expr_is_stack_derived(ctx, stmt->var_decl.initializer));
    if (stmt->var_decl.initializer)
      own_check_expr(ctx, stmt->var_decl.initializer, !stmt->var_decl.is_ref);
    int sidx = own_add_slot(ctx, stmt->var_decl.name, vt,
                            !stmt->var_decl.is_ref &&
                                checker_type_is_inline_array(vt),
                            stack_src);
    if (sidx >= 0 && stmt->var_decl.is_ref) {
      ctx->slots[sidx].is_ref_local = true;
      ctx->slots[sidx].is_ref_struct = true;
    }
    return;
  }
  case AST_EXPR_STMT:
    own_check_expr(ctx, stmt->expr_stmt.expr, false);
    return;
  case AST_RETURN_STMT:
    if (stmt->ret.value) {
      if (stmt->ret.is_ref || ctx->is_ref_return) {
        if (own_ref_expr_is_stack_local(ctx, stmt->ret.value)) {
          zan_diag_emit_code(
              ctx->c->diag, DIAG_ERROR, stmt->loc, 2161,
              "cannot return a reference to local stack storage");
        }
        own_check_expr(ctx, stmt->ret.value, false);
      } else {
        if (checker_type_is_ref_struct(ctx->c->current_return_type) &&
            own_expr_is_stack_derived(ctx, stmt->ret.value)) {
          zan_diag_emit_code(
              ctx->c->diag, DIAG_ERROR, stmt->loc, 2160,
              "cannot return a Span or ref struct derived from local stack "
              "storage or a scoped parameter");
        }
        own_check_expr(ctx, stmt->ret.value, true);
      }
    }
    return;
  case AST_IF_STMT: {
    own_check_expr(ctx, stmt->if_stmt.cond, false);
    own_bits_t base_moved = ctx->moved;
    own_bits_t base_stack = ctx->stack_derived;
    own_bits_t base_await = ctx->crossed_await;
    own_check_stmt(ctx, stmt->if_stmt.then_body);
    own_bits_t then_moved = ctx->moved;
    own_bits_t then_stack = ctx->stack_derived;
    own_bits_t then_await = ctx->crossed_await;
    if (stmt->if_stmt.else_body) {
      ctx->moved = base_moved;
      ctx->stack_derived = base_stack;
      ctx->crossed_await = base_await;
      own_check_stmt(ctx, stmt->if_stmt.else_body);
      ctx->moved = own_bits_or(then_moved, ctx->moved);
      ctx->stack_derived = own_bits_or(then_stack, ctx->stack_derived);
      ctx->crossed_await = own_bits_or(then_await, ctx->crossed_await);
    } else {
      ctx->moved = own_bits_or(base_moved, then_moved);
      ctx->stack_derived = own_bits_or(base_stack, then_stack);
      ctx->crossed_await = own_bits_or(base_await, then_await);
    }
    return;
  }
  case AST_WHILE_STMT:
  case AST_DO_WHILE_STMT: {
    own_bits_t before_await = ctx->crossed_await;
    ctx->loop_depth++;
    own_check_expr(ctx, stmt->while_stmt.cond, false);
    own_check_stmt(ctx, stmt->while_stmt.body);
    if (ctx->is_async && !ctx->await_repass &&
        memcmp(&before_await, &ctx->crossed_await, sizeof(before_await)) != 0) {
      bool prev_repass = ctx->await_repass;
      ctx->await_repass = true;
      own_check_expr(ctx, stmt->while_stmt.cond, false);
      own_check_stmt(ctx, stmt->while_stmt.body);
      ctx->await_repass = prev_repass;
    }
    ctx->loop_depth--;
    return;
  }
  case AST_FOR_STMT: {
    int saved_count = ctx->slot_count;
    if (stmt->for_stmt.init) {
      if (stmt->for_stmt.init->kind == AST_VAR_DECL)
        own_check_stmt(ctx, stmt->for_stmt.init);
      else
        own_check_expr(ctx, stmt->for_stmt.init, false);
    }
    own_bits_t before_await = ctx->crossed_await;
    ctx->loop_depth++;
    own_check_expr(ctx, stmt->for_stmt.cond, false);
    own_check_stmt(ctx, stmt->for_stmt.body);
    own_check_expr(ctx, stmt->for_stmt.step, false);
    if (ctx->is_async && !ctx->await_repass &&
        memcmp(&before_await, &ctx->crossed_await, sizeof(before_await)) != 0) {
      bool prev_repass = ctx->await_repass;
      ctx->await_repass = true;
      own_check_expr(ctx, stmt->for_stmt.cond, false);
      own_check_stmt(ctx, stmt->for_stmt.body);
      own_check_expr(ctx, stmt->for_stmt.step, false);
      ctx->await_repass = prev_repass;
    }
    ctx->loop_depth--;
    ctx->slot_count = saved_count;
    return;
  }
  case AST_FOREACH_STMT: {
    int saved_count = ctx->slot_count;
    own_check_expr(ctx, stmt->foreach_stmt.collection, false);
    ctx->loop_depth++;
    zan_type_t *vt = stmt->foreach_stmt.var_type
                         ? zan_binder_resolve_type(ctx->c->binder,
                                                   stmt->foreach_stmt.var_type)
                         : NULL;
    own_add_slot(ctx, stmt->foreach_stmt.var_name, vt, false, false);
    own_bits_t before_await = ctx->crossed_await;
    own_check_stmt(ctx, stmt->foreach_stmt.body);
    if (ctx->is_async && !ctx->await_repass &&
        memcmp(&before_await, &ctx->crossed_await, sizeof(before_await)) != 0) {
      bool prev_repass = ctx->await_repass;
      ctx->await_repass = true;
      own_check_stmt(ctx, stmt->foreach_stmt.body);
      ctx->await_repass = prev_repass;
    }
    ctx->loop_depth--;
    ctx->slot_count = saved_count;
    return;
  }
  case AST_SWITCH_STMT: {
    own_check_expr(ctx, stmt->switch_stmt.expr, false);
    own_bits_t base_moved = ctx->moved;
    own_bits_t base_stack = ctx->stack_derived;
    own_bits_t base_await = ctx->crossed_await;
    own_bits_t merged_moved = base_moved;
    own_bits_t merged_stack = base_stack;
    own_bits_t merged_await = base_await;
    for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
      zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
      if (!sc)
        continue;
      ctx->moved = base_moved;
      ctx->stack_derived = base_stack;
      ctx->crossed_await = base_await;
      own_check_stmt(ctx, sc->switch_case.body);
      merged_moved = own_bits_or(merged_moved, ctx->moved);
      merged_stack = own_bits_or(merged_stack, ctx->stack_derived);
      merged_await = own_bits_or(merged_await, ctx->crossed_await);
    }
    ctx->moved = merged_moved;
    ctx->stack_derived = merged_stack;
    ctx->crossed_await = merged_await;
    return;
  }
  case AST_TRY_STMT: {
    own_check_stmt(ctx, stmt->try_stmt.try_body);
    for (int i = 0; i < stmt->try_stmt.catches.count; i++) {
      zan_ast_node_t *cc = stmt->try_stmt.catches.items[i];
      if (cc)
        own_check_stmt(ctx, cc->catch_clause.body);
    }
    if (stmt->try_stmt.finally_body)
      own_check_stmt(ctx, stmt->try_stmt.finally_body);
    return;
  }
  case AST_THROW_STMT:
    own_check_expr(ctx, stmt->throw_stmt.value, true);
    return;
  default:
    return;
  }
}

static void checker_check_method_ownership(zan_checker_t *c,
                                           zan_ast_node_t *method) {
  if (!method)
    return;
  own_ctx_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.c = c;
  ctx.is_async = (method->method_decl.modifiers & MOD_ASYNC) != 0;
  ctx.is_ref_return = (method->kind == AST_METHOD_DECL) &&
                      method->method_decl.is_ref_return;

  if (c->current_return_type)
    checker_check_ownership_type_ref(c, c->current_return_type, method->loc);

  for (int j = 0; j < method->method_decl.params.count; j++) {
    zan_ast_node_t *p = method->method_decl.params.items[j];
    if (!p || p->kind != AST_PARAM)
      continue;
    zan_type_t *pt =
        p->param.type ? zan_binder_resolve_type(c->binder, p->param.type) : NULL;
    if (pt) {
      checker_check_ownership_type_ref(c, pt, p->loc);
      if (ctx.is_async && checker_type_is_ref_struct(pt)) {
        zan_diag_emit_code(
            c->diag, DIAG_ERROR, p->loc, 2158,
            "stack-only type '%s' cannot be used as parameter '%.*s' in an "
            "'async' method",
            type_name(pt), (int)p->param.name.len, p->param.name.str);
      }
    }
    /* Parameters of type InlineArray are only local stack if passed by value;
     * `ref InlineArray` is caller-owned. `scoped` params cannot escape return. */
    bool local_arr = (p->param.by_ref == 0) && checker_type_is_inline_array(pt);
    int pidx =
        own_add_slot(&ctx, p->param.name, pt, local_arr, p->param.is_scoped != 0);
    if (pidx >= 0) {
      ctx.slots[pidx].is_byref_param = p->param.by_ref != 0;
      ctx.slots[pidx].is_scoped_param = p->param.is_scoped != 0;
    }
  }

  if (method->method_decl.body)
    own_check_stmt(&ctx, method->method_decl.body);
}
