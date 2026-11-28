/* Pillar 9: Compile-time metaprogramming — hygienic macros & [Derive(...)] codegen.
 *
 * Runs at the front of nsresolve (before binding, checking, and IR generation),
 * so BOTH zanc and zan-lsp see 100% first-class, fully type-checked Zan AST.
 *
 * Diagnostics:
 *   E2161: macro invocation arity mismatch
 *   E2162: macro expansion recursion depth exceeded
 *   E2163: static_assert! failed at compile time
 *   E2164: macro kind mismatch (e.g., statement macro used in expression context)
 *   E2165: unknown [Derive(...)] trait
 */

#include "parser.h"
#include <stdarg.h>

#define MACRO_MAX_DEFS 256
#define MACRO_MAX_HYGIENE 64
#define MACRO_MAX_DEPTH 32

typedef struct {
  zan_ast_node_t *defs[MACRO_MAX_DEFS];
  int def_count;
  zan_arena_t *arena;
  zan_diag_t *diag;
  int hygiene_seq;
} macro_ctx_t;

typedef struct {
  zan_istr_t orig;
  zan_istr_t renamed;
} macro_hyg_entry_t;

typedef struct {
  zan_ast_node_t *macro_decl;
  zan_ast_list_t *args;
  macro_hyg_entry_t hyg[MACRO_MAX_HYGIENE];
  int hyg_count;
  zan_loc_t call_loc;
} macro_subst_t;

static bool macro_istr_eq(zan_istr_t a, const char *lit) {
  size_t n = strlen(lit);
  return a.len == (uint32_t)n && a.str && memcmp(a.str, lit, n) == 0;
}

static bool macro_istr_same(zan_istr_t a, zan_istr_t b) {
  return a.len == b.len && a.str && b.str && memcmp(a.str, b.str, a.len) == 0;
}

static zan_istr_t macro_arena_istr(zan_arena_t *arena, const char *s,
                                   size_t len) {
  char *buf = (char *)zan_arena_alloc(arena, len + 1);
  if (len > 0)
    memcpy(buf, s, len);
  buf[len] = '\0';
  zan_istr_t r = {buf, (uint32_t)len};
  return r;
}

static void macro_buf_append(char *buf, int cap, int *off, const char *fmt,
                             ...) {
  if (*off >= cap)
    return;
  va_list ap;
  va_start(ap, fmt);
  int w = vsnprintf(buf + *off, (size_t)(cap - *off), fmt, ap);
  va_end(ap);
  if (w > 0) {
    *off += w;
    if (*off > cap)
      *off = cap;
  }
}

/* ---- Part 1: [Derive(Debug, Equatable, Clone, Hash, Default)] ---- */

static bool macro_type_has_member(zan_ast_node_t *decl, const char *name) {
  size_t len = strlen(name);
  for (int i = 0; i < decl->type_decl.members.count; i++) {
    zan_ast_node_t *m = decl->type_decl.members.items[i];
    if (!m)
      continue;
    if (m->kind == AST_METHOD_DECL && m->method_decl.name.len == len &&
        memcmp(m->method_decl.name.str, name, len) == 0)
      return true;
  }
  return false;
}

static bool macro_type_has_default_ctor(zan_ast_node_t *decl) {
  bool any_ctor = false;
  for (int i = 0; i < decl->type_decl.members.count; i++) {
    zan_ast_node_t *m = decl->type_decl.members.items[i];
    if (!m || m->kind != AST_CONSTRUCTOR_DECL ||
        (m->method_decl.modifiers & MOD_STATIC))
      continue;
    any_ctor = true;
    if (m->method_decl.params.count == 0)
      return true;
  }
  return !any_ctor;
}

static bool macro_type_is_primitive_or_string(zan_ast_node_t *tref) {
  if (!tref || tref->kind != AST_TYPE_REF)
    return false;
  if (tref->type_ref.is_array || tref->type_ref.is_nullable)
    return true;
  zan_istr_t n = tref->type_ref.name;
  return macro_istr_eq(n, "int") || macro_istr_eq(n, "uint") ||
         macro_istr_eq(n, "long") || macro_istr_eq(n, "ulong") ||
         macro_istr_eq(n, "short") || macro_istr_eq(n, "ushort") ||
         macro_istr_eq(n, "byte") || macro_istr_eq(n, "sbyte") ||
         macro_istr_eq(n, "float") || macro_istr_eq(n, "double") ||
         macro_istr_eq(n, "bool") || macro_istr_eq(n, "char") ||
         macro_istr_eq(n, "string") || macro_istr_eq(n, "nint") ||
         macro_istr_eq(n, "nuint") || macro_istr_eq(n, "decimal");
}

static bool macro_type_is_int_like(zan_ast_node_t *tref) {
  if (!tref || tref->kind != AST_TYPE_REF || tref->type_ref.is_array ||
      tref->type_ref.is_nullable)
    return false;
  zan_istr_t n = tref->type_ref.name;
  return macro_istr_eq(n, "int") || macro_istr_eq(n, "uint") ||
         macro_istr_eq(n, "short") || macro_istr_eq(n, "ushort") ||
         macro_istr_eq(n, "byte") || macro_istr_eq(n, "sbyte") ||
         macro_istr_eq(n, "char");
}

static bool macro_type_is_long_like(zan_ast_node_t *tref) {
  if (!tref || tref->kind != AST_TYPE_REF || tref->type_ref.is_array ||
      tref->type_ref.is_nullable)
    return false;
  zan_istr_t n = tref->type_ref.name;
  return macro_istr_eq(n, "long") || macro_istr_eq(n, "ulong") ||
         macro_istr_eq(n, "nint") || macro_istr_eq(n, "nuint");
}

static void macro_expand_derives_on_decl(macro_ctx_t *ctx,
                                         zan_ast_node_t *decl) {
  if (!decl ||
      (decl->kind != AST_STRUCT_DECL && decl->kind != AST_CLASS_DECL) ||
      !decl->meta)
    return;

  bool want_debug = false;
  bool want_eq = false;
  bool want_clone = false;
  bool want_hash = false;
  bool want_default = false;

  for (int i = 0; i < decl->meta->attributes.count; i++) {
    zan_ast_node_t *attr = decl->meta->attributes.items[i];
    if (!attr || attr->kind != AST_ATTRIBUTE || !attr->attribute.name ||
        attr->attribute.name->kind != AST_IDENTIFIER)
      continue;
    if (!macro_istr_eq(attr->attribute.name->ident.name, "Derive"))
      continue;
    for (int j = 0; j < attr->attribute.args.count; j++) {
      zan_ast_node_t *a = attr->attribute.args.items[j];
      zan_istr_t tname = {NULL, 0};
      if (a && a->kind == AST_IDENTIFIER)
        tname = a->ident.name;
      else if (a && a->kind == AST_STRING_LITERAL)
        tname = a->str_val;
      if (macro_istr_eq(tname, "Debug"))
        want_debug = true;
      else if (macro_istr_eq(tname, "Equatable") || macro_istr_eq(tname, "Eq"))
        want_eq = true;
      else if (macro_istr_eq(tname, "Clone"))
        want_clone = true;
      else if (macro_istr_eq(tname, "Hash"))
        want_hash = true;
      else if (macro_istr_eq(tname, "Default"))
        want_default = true;
      else if (tname.len > 0) {
        zan_diag_emit_code(
            ctx->diag, DIAG_ERROR, a ? a->loc : attr->loc, 2165,
            "unknown [Derive] trait '%.*s' (supported: Debug, Equatable, "
            "Clone, Hash, Default)",
            (int)tname.len, tname.str);
      }
    }
  }

  if (!want_debug && !want_eq && !want_clone && !want_hash && !want_default)
    return;

  /* Collect instance fields */
  zan_ast_node_t *fields[128];
  int fcount = 0;
  for (int i = 0; i < decl->type_decl.members.count && fcount < 128; i++) {
    zan_ast_node_t *m = decl->type_decl.members.items[i];
    if (m && m->kind == AST_FIELD_DECL &&
        !(m->field_decl.modifiers & MOD_STATIC)) {
      fields[fcount++] = m;
    }
  }

  bool is_class = decl->kind == AST_CLASS_DECL;
  const char *tname = decl->type_decl.name.str;
  int tlen = (int)decl->type_decl.name.len;

  const int cap = 65536;
  char *buf = (char *)malloc((size_t)cap);
  if (!buf)
    return;
  int off = 0;

  macro_buf_append(buf, cap, &off, "%s %.*s {\n", is_class ? "class" : "struct",
                   tlen, tname);

  if (want_debug && !macro_type_has_member(decl, "ToString")) {
    macro_buf_append(buf, cap, &off, "  public %sstring ToString() {\n",
                     is_class ? "override " : "");
    if (fcount == 0) {
      macro_buf_append(buf, cap, &off, "    return \"%.*s { }\";\n", tlen,
                       tname);
    } else {
      macro_buf_append(buf, cap, &off, "    return $\"%.*s {{ ", tlen, tname);
      for (int i = 0; i < fcount; i++) {
        zan_istr_t fn = fields[i]->field_decl.name;
        if (i > 0)
          macro_buf_append(buf, cap, &off, ", ");
        if (macro_type_is_primitive_or_string(fields[i]->field_decl.type)) {
          macro_buf_append(buf, cap, &off, "%.*s = {this.%.*s}", (int)fn.len,
                           fn.str, (int)fn.len, fn.str);
        } else {
          macro_buf_append(buf, cap, &off, "%.*s = {this.%.*s.ToString()}",
                           (int)fn.len, fn.str, (int)fn.len, fn.str);
        }
      }
      macro_buf_append(buf, cap, &off, " }}\";\n");
    }
    macro_buf_append(buf, cap, &off, "  }\n");
  }

  if (want_eq && !macro_type_has_member(decl, "Equals")) {
    macro_buf_append(buf, cap, &off, "  public bool Equals(%.*s other) {\n",
                     tlen, tname);
    if (is_class)
      macro_buf_append(buf, cap, &off,
                       "    if (other == null) return false;\n");
    if (fcount == 0) {
      macro_buf_append(buf, cap, &off, "    return true;\n");
    } else {
      macro_buf_append(buf, cap, &off, "    return ");
      for (int i = 0; i < fcount; i++) {
        zan_istr_t fn = fields[i]->field_decl.name;
        if (i > 0)
          macro_buf_append(buf, cap, &off, " && ");
        if (macro_type_is_primitive_or_string(fields[i]->field_decl.type)) {
          macro_buf_append(buf, cap, &off, "(this.%.*s == other.%.*s)",
                           (int)fn.len, fn.str, (int)fn.len, fn.str);
        } else {
          macro_buf_append(buf, cap, &off, "this.%.*s.Equals(other.%.*s)",
                           (int)fn.len, fn.str, (int)fn.len, fn.str);
        }
      }
      macro_buf_append(buf, cap, &off, ";\n");
    }
    macro_buf_append(buf, cap, &off, "  }\n");
    if (!macro_type_has_member(decl, "op_eq")) {
      if (is_class) {
        macro_buf_append(
            buf, cap, &off,
            "  public static bool operator ==(%.*s a, %.*s b) {\n"
            "    if ((object)a == null) return (object)b == null;\n"
            "    return a.Equals(b);\n"
            "  }\n",
            tlen, tname, tlen, tname);
      } else {
        macro_buf_append(
            buf, cap, &off,
            "  public static bool operator ==(%.*s a, %.*s b) => a.Equals(b);\n",
            tlen, tname, tlen, tname);
      }
    }
    if (!macro_type_has_member(decl, "op_neq")) {
      macro_buf_append(
          buf, cap, &off,
          "  public static bool operator !=(%.*s a, %.*s b) => !(a == b);\n",
          tlen, tname, tlen, tname);
    }
  }

  if (want_clone && !macro_type_has_member(decl, "Clone")) {
    macro_buf_append(buf, cap, &off, "  public %.*s Clone() {\n", tlen, tname);
    if (!is_class || macro_type_has_default_ctor(decl)) {
      macro_buf_append(buf, cap, &off, "    %.*s __c = new %.*s();\n", tlen,
                       tname, tlen, tname);
      for (int i = 0; i < fcount; i++) {
        zan_istr_t fn = fields[i]->field_decl.name;
        macro_buf_append(buf, cap, &off, "    __c.%.*s = this.%.*s;\n",
                         (int)fn.len, fn.str, (int)fn.len, fn.str);
      }
      macro_buf_append(buf, cap, &off, "    return __c;\n");
    } else {
      macro_buf_append(buf, cap, &off, "    return new %.*s(", tlen, tname);
      for (int i = 0; i < fcount; i++) {
        zan_istr_t fn = fields[i]->field_decl.name;
        if (i > 0)
          macro_buf_append(buf, cap, &off, ", ");
        macro_buf_append(buf, cap, &off, "this.%.*s", (int)fn.len, fn.str);
      }
      macro_buf_append(buf, cap, &off, ");\n");
    }
    macro_buf_append(buf, cap, &off, "  }\n");
  }

  if (want_hash && !macro_type_has_member(decl, "GetHashCode")) {
    macro_buf_append(buf, cap, &off, "  public %sint GetHashCode() {\n",
                     is_class ? "override " : "");
    macro_buf_append(buf, cap, &off, "    int __h = 17;\n");
    for (int i = 0; i < fcount; i++) {
      zan_istr_t fn = fields[i]->field_decl.name;
      zan_ast_node_t *ft = fields[i]->field_decl.type;
      if (macro_type_is_int_like(ft)) {
        macro_buf_append(buf, cap, &off,
                         "    __h = __h * 31 + (int)this.%.*s;\n", (int)fn.len,
                         fn.str);
      } else if (macro_type_is_long_like(ft)) {
        macro_buf_append(buf, cap, &off,
                         "    __h = __h * 31 + (int)(this.%.*s ^ (this.%.*s >> 32));\n",
                         (int)fn.len, fn.str, (int)fn.len, fn.str);
      } else if (ft && ft->kind == AST_TYPE_REF &&
                 macro_istr_eq(ft->type_ref.name, "bool")) {
        macro_buf_append(buf, cap, &off,
                         "    __h = __h * 31 + (this.%.*s ? 1231 : 1237);\n",
                         (int)fn.len, fn.str);
      } else if (ft && ft->kind == AST_TYPE_REF &&
                 (macro_istr_eq(ft->type_ref.name, "float") ||
                  macro_istr_eq(ft->type_ref.name, "double"))) {
        macro_buf_append(buf, cap, &off,
                         "    __h = __h * 31 + (int)(this.%.*s * 1000.0);\n",
                         (int)fn.len, fn.str);
      } else if (ft && ft->kind == AST_TYPE_REF &&
                 macro_istr_eq(ft->type_ref.name, "string")) {
        macro_buf_append(buf, cap, &off,
                         "    __h = __h * 31 + (this.%.*s != null ? this.%.*s.Length : 0);\n",
                         (int)fn.len, fn.str, (int)fn.len, fn.str);
      } else {
        macro_buf_append(buf, cap, &off,
                         "    __h = __h * 31 + this.%.*s.GetHashCode();\n",
                         (int)fn.len, fn.str);
      }
    }
    macro_buf_append(buf, cap, &off, "    return __h;\n  }\n");
  }

  if (want_default && !macro_type_has_member(decl, "Default")) {
    macro_buf_append(buf, cap, &off,
                     "  public static %.*s Default() => new %.*s();\n", tlen,
                     tname, tlen, tname);
  }

  macro_buf_append(buf, cap, &off, "}\n");

  /* Copy source into arena so parsed zan_istr_t slices outlive this function */
  char *arena_src = (char *)zan_arena_alloc(ctx->arena, (size_t)off + 1);
  memcpy(arena_src, buf, (size_t)off);
  arena_src[off] = '\0';
  free(buf);

  zan_lexer_t lex;
  zan_lexer_init(&lex, arena_src, (size_t)off, decl->loc.file_id, ctx->arena,
                 ctx->diag);
  zan_parser_t par;
  zan_parser_init(&par, &lex, ctx->arena, ctx->diag);
  zan_ast_node_t *gen_unit = zan_parser_parse(&par);
  if (gen_unit && gen_unit->kind == AST_COMPILATION_UNIT &&
      gen_unit->comp_unit.decls.count > 0) {
    zan_ast_node_t *gd = gen_unit->comp_unit.decls.items[0];
    if (gd && (gd->kind == AST_STRUCT_DECL || gd->kind == AST_CLASS_DECL)) {
      for (int i = 0; i < gd->type_decl.members.count; i++) {
        zan_ast_node_t *gm = gd->type_decl.members.items[i];
        if (!gm)
          continue;
        gm->loc = decl->loc;
        zan_ast_list_push(&decl->type_decl.members, gm, ctx->arena);
      }
    }
  }
}

/* ---- Part 2: Hygienic Declarative Macros & Built-in Macros ---- */

static zan_ast_node_t *macro_find_def(macro_ctx_t *ctx, zan_istr_t name) {
  for (int i = ctx->def_count - 1; i >= 0; i--) {
    if (macro_istr_same(ctx->defs[i]->method_decl.name, name))
      return ctx->defs[i];
  }
  return NULL;
}

static bool macro_is_call_node(zan_ast_node_t *node, zan_istr_t *out_name) {
  if (!node || node->kind != AST_CALL)
    return false;
  zan_ast_node_t *callee = node->call.callee;
  if (!callee || callee->kind != AST_POSTFIX_UNARY ||
      callee->unary.op != TK_BANG || !callee->unary.operand ||
      callee->unary.operand->kind != AST_IDENTIFIER)
    return false;
  if (out_name)
    *out_name = callee->unary.operand->ident.name;
  return true;
}

static void macro_clone_list(zan_arena_t *arena, zan_ast_list_t *dst,
                             const zan_ast_list_t *src) {
  dst->count = src->count;
  dst->capacity = src->count;
  if (src->count > 0) {
    dst->items = (zan_ast_node_t **)zan_arena_alloc(
        arena, sizeof(zan_ast_node_t *) * (size_t)src->count);
    memcpy(dst->items, src->items,
           sizeof(zan_ast_node_t *) * (size_t)src->count);
  } else {
    dst->items = NULL;
  }
}

static void macro_collect_hygiene_locals(zan_ast_node_t *node, macro_ctx_t *ctx,
                                         macro_subst_t *sub, int inv_id) {
  if (!node)
    return;
  zan_istr_t decl_name = {NULL, 0};
  if (node->kind == AST_VAR_DECL)
    decl_name = node->var_decl.name;
  else if (node->kind == AST_FOREACH_STMT)
    decl_name = node->foreach_stmt.var_name;
  if (decl_name.len > 0 && sub->hyg_count < MACRO_MAX_HYGIENE) {
    /* Don't rename if it matches a macro parameter */
    bool is_param = false;
    for (int i = 0; i < sub->macro_decl->method_decl.params.count; i++) {
      zan_ast_node_t *p = sub->macro_decl->method_decl.params.items[i];
      if (p && macro_istr_same(p->param.name, decl_name)) {
        is_param = true;
        break;
      }
    }
    bool already = false;
    for (int i = 0; i < sub->hyg_count; i++) {
      if (macro_istr_same(sub->hyg[i].orig, decl_name)) {
        already = true;
        break;
      }
    }
    if (!is_param && !already) {
      char rbuf[160];
      int rlen = snprintf(rbuf, sizeof(rbuf), "%.*s__m%d", (int)decl_name.len,
                          decl_name.str, inv_id);
      sub->hyg[sub->hyg_count].orig = decl_name;
      sub->hyg[sub->hyg_count].renamed =
          macro_arena_istr(ctx->arena, rbuf, (size_t)rlen);
      sub->hyg_count++;
    }
  }
  int n = zan_ast_child_count(node);
  for (int i = 0; i < n; i++) {
    zan_ast_node_t *ch = zan_ast_child_at(node, i);
    if (ch)
      macro_collect_hygiene_locals(ch, ctx, sub, inv_id);
  }
}

static zan_ast_node_t *macro_clone_node(macro_ctx_t *ctx, zan_ast_node_t *node,
                                        macro_subst_t *sub) {
  if (!node)
    return NULL;

  /* Identifier substitution (parameter or hygienic α-renamed local) */
  if (sub && node->kind == AST_IDENTIFIER) {
    for (int i = 0; i < sub->macro_decl->method_decl.params.count; i++) {
      zan_ast_node_t *p = sub->macro_decl->method_decl.params.items[i];
      if (p && macro_istr_same(p->param.name, node->ident.name) &&
          i < sub->args->count) {
        return macro_clone_node(ctx, sub->args->items[i], NULL);
      }
    }
    for (int i = 0; i < sub->hyg_count; i++) {
      if (macro_istr_same(sub->hyg[i].orig, node->ident.name)) {
        zan_ast_node_t *copy =
            (zan_ast_node_t *)zan_arena_alloc(ctx->arena, sizeof(*copy));
        *copy = *node;
        copy->loc = sub->call_loc;
        copy->ident.name = sub->hyg[i].renamed;
        return copy;
      }
    }
  }

  zan_ast_node_t *copy =
      (zan_ast_node_t *)zan_arena_alloc(ctx->arena, sizeof(*copy));
  *copy = *node;
  if (sub)
    copy->loc = sub->call_loc;

  /* Clone any owned AST lists before recursing through child slots */
  switch (copy->kind) {
  case AST_BLOCK:
    macro_clone_list(ctx->arena, &copy->block.stmts, &node->block.stmts);
    break;
  case AST_CALL:
    macro_clone_list(ctx->arena, &copy->call.args, &node->call.args);
    macro_clone_list(ctx->arena, &copy->call.type_args, &node->call.type_args);
    break;
  case AST_NEW_EXPR:
    macro_clone_list(ctx->arena, &copy->new_expr.args, &node->new_expr.args);
    macro_clone_list(ctx->arena, &copy->new_expr.arg_inits,
                     &node->new_expr.arg_inits);
    break;
  case AST_TUPLE_EXPR:
    macro_clone_list(ctx->arena, &copy->tuple_expr.items,
                     &node->tuple_expr.items);
    break;
  case AST_TUPLE_TYPE:
    macro_clone_list(ctx->arena, &copy->tuple_type.elems,
                     &node->tuple_type.elems);
    break;
  case AST_STRING_INTERP:
    macro_clone_list(ctx->arena, &copy->string_interp.parts,
                     &node->string_interp.parts);
    break;
  case AST_LAMBDA:
    macro_clone_list(ctx->arena, &copy->lambda.params, &node->lambda.params);
    break;
  case AST_SWITCH_STMT:
    macro_clone_list(ctx->arena, &copy->switch_stmt.cases,
                     &node->switch_stmt.cases);
    break;
  case AST_SWITCH_EXPR:
    macro_clone_list(ctx->arena, &copy->switch_expr.arms,
                     &node->switch_expr.arms);
    break;
  case AST_TRY_STMT:
    macro_clone_list(ctx->arena, &copy->try_stmt.catches,
                     &node->try_stmt.catches);
    break;
  case AST_TYPE_REF:
    macro_clone_list(ctx->arena, &copy->type_ref.type_args,
                     &node->type_ref.type_args);
    break;
  case AST_COLL_INIT:
    macro_clone_list(ctx->arena, &copy->coll_init.items,
                     &node->coll_init.items);
    break;
  case AST_WITH_EXPR:
    macro_clone_list(ctx->arena, &copy->with_expr.assigns,
                     &node->with_expr.assigns);
    break;
  case AST_VAR_DECL:
    if (sub) {
      for (int i = 0; i < sub->hyg_count; i++) {
        if (macro_istr_same(sub->hyg[i].orig, copy->var_decl.name)) {
          copy->var_decl.name = sub->hyg[i].renamed;
          break;
        }
      }
    }
    break;
  case AST_FOREACH_STMT:
    if (sub) {
      for (int i = 0; i < sub->hyg_count; i++) {
        if (macro_istr_same(sub->hyg[i].orig, copy->foreach_stmt.var_name)) {
          copy->foreach_stmt.var_name = sub->hyg[i].renamed;
          break;
        }
      }
    }
    break;
  default:
    break;
  }

  int n = zan_ast_child_count(copy);
  for (int i = 0; i < n; i++) {
    zan_ast_node_t **slot = zan_ast_child_ptr_at(copy, i);
    if (slot && *slot)
      *slot = macro_clone_node(ctx, *slot, sub);
  }
  return copy;
}

/* Pretty-print an AST expression for `stringify!(expr)` */
static void macro_stringify_expr(zan_ast_node_t *e, char *buf, int cap,
                                 int *off) {
  if (!e)
    return;
  switch (e->kind) {
  case AST_INT_LITERAL:
    macro_buf_append(buf, cap, off, "%lld", (long long)e->int_val);
    return;
  case AST_FLOAT_LITERAL:
    macro_buf_append(buf, cap, off, "%g", e->float_val);
    return;
  case AST_BOOL_LITERAL:
    macro_buf_append(buf, cap, off, "%s", e->bool_val ? "true" : "false");
    return;
  case AST_STRING_LITERAL:
    macro_buf_append(buf, cap, off, "\"%.*s\"", (int)e->str_val.len,
                     e->str_val.str);
    return;
  case AST_NULL_LITERAL:
    macro_buf_append(buf, cap, off, "null");
    return;
  case AST_IDENTIFIER:
    macro_buf_append(buf, cap, off, "%.*s", (int)e->ident.name.len,
                     e->ident.name.str);
    return;
  case AST_MEMBER_ACCESS:
    macro_stringify_expr(e->member.object, buf, cap, off);
    macro_buf_append(buf, cap, off, ".%.*s", (int)e->member.name.len,
                     e->member.name.str);
    return;
  case AST_INDEX:
    macro_stringify_expr(e->index.object, buf, cap, off);
    macro_buf_append(buf, cap, off, "[");
    macro_stringify_expr(e->index.index, buf, cap, off);
    macro_buf_append(buf, cap, off, "]");
    return;
  case AST_BINARY: {
    const char *op = "?";
    switch (e->binary.op) {
    case TK_PLUS: op = "+"; break;
    case TK_MINUS: op = "-"; break;
    case TK_STAR: op = "*"; break;
    case TK_SLASH: op = "/"; break;
    case TK_PERCENT: op = "%"; break;
    case TK_EQ_EQ: op = "=="; break;
    case TK_BANG_EQ: op = "!="; break;
    case TK_LESS: op = "<"; break;
    case TK_LESS_EQ: op = "<="; break;
    case TK_GREATER: op = ">"; break;
    case TK_GREATER_EQ: op = ">="; break;
    case TK_AMP_AMP: op = "&&"; break;
    case TK_PIPE_PIPE: op = "||"; break;
    case TK_AMP: op = "&"; break;
    case TK_PIPE: op = "|"; break;
    case TK_CARET: op = "^"; break;
    case TK_LESS_LESS: op = "<<"; break;
    case TK_GREATER_GREATER: op = ">>"; break;
    default: break;
    }
    macro_stringify_expr(e->binary.left, buf, cap, off);
    macro_buf_append(buf, cap, off, " %s ", op);
    macro_stringify_expr(e->binary.right, buf, cap, off);
    return;
  }
  case AST_UNARY: {
    const char *op = e->unary.op == TK_MINUS   ? "-"
                     : e->unary.op == TK_BANG  ? "!"
                     : e->unary.op == TK_TILDE ? "~"
                                               : "+";
    macro_buf_append(buf, cap, off, "%s", op);
    macro_stringify_expr(e->unary.operand, buf, cap, off);
    return;
  }
  case AST_CALL:
    macro_stringify_expr(e->call.callee, buf, cap, off);
    macro_buf_append(buf, cap, off, "(");
    for (int i = 0; i < e->call.args.count; i++) {
      if (i > 0)
        macro_buf_append(buf, cap, off, ", ");
      macro_stringify_expr(e->call.args.items[i], buf, cap, off);
    }
    macro_buf_append(buf, cap, off, ")");
    return;
  case AST_CONDITIONAL:
    macro_stringify_expr(e->conditional.cond, buf, cap, off);
    macro_buf_append(buf, cap, off, " ? ");
    macro_stringify_expr(e->conditional.then_expr, buf, cap, off);
    macro_buf_append(buf, cap, off, " : ");
    macro_stringify_expr(e->conditional.else_expr, buf, cap, off);
    return;
  default:
    macro_buf_append(buf, cap, off, "<expr>");
    return;
  }
}

/* Compile-time constant evaluator for `static_assert!(cond, msg)` */
static bool macro_eval_const_int(zan_ast_node_t *e, int64_t *out) {
  if (!e)
    return false;
  switch (e->kind) {
  case AST_INT_LITERAL:
    *out = e->int_val;
    return true;
  case AST_BOOL_LITERAL:
    *out = e->bool_val ? 1 : 0;
    return true;
  case AST_UNARY: {
    int64_t v = 0;
    if (!macro_eval_const_int(e->unary.operand, &v))
      return false;
    if (e->unary.op == TK_MINUS) {
      *out = -v;
      return true;
    }
    if (e->unary.op == TK_BANG) {
      *out = !v;
      return true;
    }
    if (e->unary.op == TK_TILDE) {
      *out = ~v;
      return true;
    }
    return false;
  }
  case AST_BINARY: {
    int64_t a = 0, b = 0;
    if (!macro_eval_const_int(e->binary.left, &a) ||
        !macro_eval_const_int(e->binary.right, &b))
      return false;
    switch (e->binary.op) {
    case TK_PLUS: *out = a + b; return true;
    case TK_MINUS: *out = a - b; return true;
    case TK_STAR: *out = a * b; return true;
    case TK_SLASH: if (b == 0) return false; *out = a / b; return true;
    case TK_PERCENT: if (b == 0) return false; *out = a % b; return true;
    case TK_EQ_EQ: *out = (a == b); return true;
    case TK_BANG_EQ: *out = (a != b); return true;
    case TK_LESS: *out = (a < b); return true;
    case TK_LESS_EQ: *out = (a <= b); return true;
    case TK_GREATER: *out = (a > b); return true;
    case TK_GREATER_EQ: *out = (a >= b); return true;
    case TK_AMP_AMP: *out = (a && b); return true;
    case TK_PIPE_PIPE: *out = (a || b); return true;
    case TK_AMP: *out = (a & b); return true;
    case TK_PIPE: *out = (a | b); return true;
    case TK_CARET: *out = (a ^ b); return true;
    case TK_LESS_LESS: *out = (a << b); return true;
    case TK_GREATER_GREATER: *out = (a >> b); return true;
    default: return false;
    }
  }
  case AST_SIZEOF_EXPR: {
    zan_ast_node_t *t = e->cast.type;
    if (t && t->kind == AST_TYPE_REF) {
      zan_istr_t n = t->type_ref.name;
      if (macro_istr_eq(n, "byte") || macro_istr_eq(n, "sbyte") ||
          macro_istr_eq(n, "bool")) { *out = 1; return true; }
      if (macro_istr_eq(n, "short") || macro_istr_eq(n, "ushort") ||
          macro_istr_eq(n, "char")) { *out = 2; return true; }
      if (macro_istr_eq(n, "int") || macro_istr_eq(n, "uint") ||
          macro_istr_eq(n, "float")) { *out = 4; return true; }
      if (macro_istr_eq(n, "long") || macro_istr_eq(n, "ulong") ||
          macro_istr_eq(n, "double") || macro_istr_eq(n, "nint") ||
          macro_istr_eq(n, "nuint")) { *out = 8; return true; }
    }
    return false;
  }
  default:
    return false;
  }
}

static zan_ast_node_t *macro_expand_node(macro_ctx_t *ctx, zan_ast_node_t *node,
                                         bool stmt_context, int depth);

static zan_ast_node_t *macro_expand_builtin(macro_ctx_t *ctx,
                                            zan_ast_node_t *call,
                                            zan_istr_t name, bool stmt_context,
                                            int depth) {
  if (macro_istr_eq(name, "static_assert")) {
    if (call->call.args.count < 1 || call->call.args.count > 2) {
      zan_diag_emit_code(ctx->diag, DIAG_ERROR, call->loc, 2161,
                         "static_assert! expects 1 or 2 arguments (got %d)",
                         call->call.args.count);
    } else {
      zan_ast_node_t *cond =
          macro_expand_node(ctx, call->call.args.items[0], false, depth + 1);
      int64_t val = 0;
      if (!macro_eval_const_int(cond, &val)) {
        zan_diag_emit_code(
            ctx->diag, DIAG_ERROR, call->loc, 2163,
            "static_assert! condition must be a compile-time constant");
      } else if (val == 0) {
        const char *msg = "compile-time assertion failed";
        int mlen = (int)strlen(msg);
        if (call->call.args.count == 2 && call->call.args.items[1] &&
            call->call.args.items[1]->kind == AST_STRING_LITERAL) {
          msg = call->call.args.items[1]->str_val.str;
          mlen = (int)call->call.args.items[1]->str_val.len;
        }
        zan_diag_emit_code(ctx->diag, DIAG_ERROR, call->loc, 2163,
                           "static_assert! failed: %.*s", mlen, msg);
      }
    }
    if (stmt_context) {
      zan_ast_node_t *empty = zan_ast_new(ctx->arena, AST_BLOCK, call->loc);
      zan_ast_list_init(&empty->block.stmts);
      return empty;
    }
    zan_ast_node_t *t = zan_ast_new(ctx->arena, AST_BOOL_LITERAL, call->loc);
    t->bool_val = true;
    return t;
  }

  if (macro_istr_eq(name, "line")) {
    if (call->call.args.count != 0) {
      zan_diag_emit_code(ctx->diag, DIAG_ERROR, call->loc, 2161,
                         "line! expects 0 arguments (got %d)",
                         call->call.args.count);
    }
    zan_ast_node_t *lit = zan_ast_new(ctx->arena, AST_INT_LITERAL, call->loc);
    lit->int_val = (int64_t)call->loc.line;
    lit->lit_radix = 10;
    return lit;
  }

  if (macro_istr_eq(name, "file")) {
    if (call->call.args.count != 0) {
      zan_diag_emit_code(ctx->diag, DIAG_ERROR, call->loc, 2161,
                         "file! expects 0 arguments (got %d)",
                         call->call.args.count);
    }
    const char *fname = "<unknown>";
    if (ctx->diag && ctx->diag->file_names &&
        (int)call->loc.file_id < ctx->diag->file_count &&
        ctx->diag->file_names[call->loc.file_id]) {
      fname = ctx->diag->file_names[call->loc.file_id];
      /* Strip directory prefix for clean deterministic output */
      const char *slash = strrchr(fname, '/');
      const char *bslash = strrchr(fname, '\\');
      if (bslash && (!slash || bslash > slash))
        slash = bslash;
      if (slash)
        fname = slash + 1;
    }
    zan_ast_node_t *lit =
        zan_ast_new(ctx->arena, AST_STRING_LITERAL, call->loc);
    lit->str_val = macro_arena_istr(ctx->arena, fname, strlen(fname));
    return lit;
  }

  if (macro_istr_eq(name, "stringify")) {
    if (call->call.args.count != 1) {
      zan_diag_emit_code(ctx->diag, DIAG_ERROR, call->loc, 2161,
                         "stringify! expects 1 argument (got %d)",
                         call->call.args.count);
      zan_ast_node_t *lit =
          zan_ast_new(ctx->arena, AST_STRING_LITERAL, call->loc);
      lit->str_val = macro_arena_istr(ctx->arena, "", 0);
      return lit;
    }
    char sbuf[512];
    int soff = 0;
    sbuf[0] = '\0';
    macro_stringify_expr(call->call.args.items[0], sbuf, (int)sizeof(sbuf),
                         &soff);
    zan_ast_node_t *lit =
        zan_ast_new(ctx->arena, AST_STRING_LITERAL, call->loc);
    lit->str_val = macro_arena_istr(ctx->arena, sbuf, (size_t)soff);
    return lit;
  }

  if (macro_istr_eq(name, "concat")) {
    char sbuf[1024];
    int soff = 0;
    sbuf[0] = '\0';
    for (int i = 0; i < call->call.args.count; i++) {
      zan_ast_node_t *a =
          macro_expand_node(ctx, call->call.args.items[i], false, depth + 1);
      if (!a)
        continue;
      if (a->kind == AST_STRING_LITERAL)
        macro_buf_append(sbuf, (int)sizeof(sbuf), &soff, "%.*s",
                         (int)a->str_val.len, a->str_val.str);
      else if (a->kind == AST_INT_LITERAL)
        macro_buf_append(sbuf, (int)sizeof(sbuf), &soff, "%lld",
                         (long long)a->int_val);
      else if (a->kind == AST_BOOL_LITERAL)
        macro_buf_append(sbuf, (int)sizeof(sbuf), &soff, "%s",
                         a->bool_val ? "true" : "false");
      else
        macro_stringify_expr(a, sbuf, (int)sizeof(sbuf), &soff);
    }
    zan_ast_node_t *lit =
        zan_ast_new(ctx->arena, AST_STRING_LITERAL, call->loc);
    lit->str_val = macro_arena_istr(ctx->arena, sbuf, (size_t)soff);
    return lit;
  }

  return NULL;
}

static zan_ast_node_t *macro_expand_invocation(macro_ctx_t *ctx,
                                               zan_ast_node_t *call,
                                               bool stmt_context, int depth) {
  zan_istr_t name = {NULL, 0};
  if (!macro_is_call_node(call, &name))
    return NULL;

  /* Built-in compile-time macros */
  zan_ast_node_t *builtin =
      macro_expand_builtin(ctx, call, name, stmt_context, depth);
  if (builtin)
    return builtin;

  /* User-declared hygienic macro */
  zan_ast_node_t *def = macro_find_def(ctx, name);
  if (!def)
    return NULL;

  if (depth >= MACRO_MAX_DEPTH) {
    zan_diag_emit_code(ctx->diag, DIAG_ERROR, call->loc, 2162,
                       "macro expansion depth exceeded for '%.*s!' (possible "
                       "recursive macro)",
                       (int)name.len, name.str);
    return call;
  }

  int want = def->method_decl.params.count;
  int got = call->call.args.count;
  if (want != got) {
    zan_diag_emit_code(ctx->diag, DIAG_ERROR, call->loc, 2161,
                       "macro '%.*s!' expects %d argument(s), got %d",
                       (int)name.len, name.str, want, got);
    return call;
  }

  if (!def->method_decl.body)
    return call;
  if (!stmt_context && def->method_decl.body->kind == AST_BLOCK) {
    zan_diag_emit_code(
        ctx->diag, DIAG_ERROR, call->loc, 2164,
        "statement macro '%.*s!' cannot be used in an expression position",
        (int)name.len, name.str);
    return call;
  }

  /* Expand macro arguments first so nested macros in args resolve cleanly */
  for (int i = 0; i < call->call.args.count; i++) {
    if (call->call.args.items[i])
      call->call.args.items[i] =
          macro_expand_node(ctx, call->call.args.items[i], false, depth + 1);
  }

  macro_subst_t sub;
  memset(&sub, 0, sizeof(sub));
  sub.macro_decl = def;
  sub.args = &call->call.args;
  sub.call_loc = call->loc;
  int inv_id = ++ctx->hygiene_seq;
  macro_collect_hygiene_locals(def->method_decl.body, ctx, &sub, inv_id);

  zan_ast_node_t *expanded = macro_clone_node(ctx, def->method_decl.body, &sub);
  return macro_expand_node(ctx, expanded, stmt_context, depth + 1);
}

static zan_ast_node_t *macro_expand_node(macro_ctx_t *ctx, zan_ast_node_t *node,
                                         bool stmt_context, int depth) {
  if (!node)
    return NULL;

  /* Statement-level macro call: `Swap!(a, b);` or `static_assert!(...);` */
  if (node->kind == AST_EXPR_STMT && node->expr_stmt.expr) {
    zan_ast_node_t *rep =
        macro_expand_invocation(ctx, node->expr_stmt.expr, true, depth);
    if (rep) {
      if (rep->kind == AST_BLOCK)
        return rep;
      node->expr_stmt.expr = rep;
      return node;
    }
  }

  /* Expression-level macro call: `Clamp!(x, 0, 10)`, `stringify!(a + b)`, etc. */
  if (node->kind == AST_CALL) {
    zan_ast_node_t *rep = macro_expand_invocation(ctx, node, stmt_context, depth);
    if (rep)
      return rep;
  }

  /* Recurse into all child slots generically via AST topology table */
  int n = zan_ast_child_count(node);
  for (int i = 0; i < n; i++) {
    zan_ast_node_t **slot = zan_ast_child_ptr_at(node, i);
    if (slot && *slot) {
      bool child_stmt =
          (node->kind == AST_BLOCK || node->kind == AST_IF_STMT ||
           node->kind == AST_WHILE_STMT || node->kind == AST_DO_WHILE_STMT ||
           node->kind == AST_FOR_STMT || node->kind == AST_FOREACH_STMT ||
           node->kind == AST_SWITCH_CASE || node->kind == AST_TRY_STMT ||
           node->kind == AST_CATCH_CLAUSE);
      *slot = macro_expand_node(ctx, *slot, child_stmt, depth);
    }
  }
  return node;
}

static void zan_macro_expand_unit(zan_ast_node_t *unit, zan_arena_t *arena,
                                  zan_diag_t *diag) {
  if (!unit || unit->kind != AST_COMPILATION_UNIT)
    return;

  macro_ctx_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.arena = arena;
  ctx.diag = diag;

  zan_ast_list_t *decls = &unit->comp_unit.decls;

  /* Pass 1: Collect all macro declarations (top-level and member-level) */
  for (int i = 0; i < decls->count; i++) {
    zan_ast_node_t *d = decls->items[i];
    if (!d)
      continue;
    if (d->kind == AST_MACRO_DECL) {
      if (ctx.def_count < MACRO_MAX_DEFS)
        ctx.defs[ctx.def_count++] = d;
    } else if (d->kind == AST_CLASS_DECL || d->kind == AST_STRUCT_DECL) {
      for (int m = 0; m < d->type_decl.members.count; m++) {
        zan_ast_node_t *mem = d->type_decl.members.items[m];
        if (mem && mem->kind == AST_MACRO_DECL &&
            ctx.def_count < MACRO_MAX_DEFS) {
          ctx.defs[ctx.def_count++] = mem;
        }
      }
    }
  }

  /* Pass 2: Expand [Derive(...)] attributes into first-class typed AST members */
  for (int i = 0; i < decls->count; i++) {
    zan_ast_node_t *d = decls->items[i];
    if (d && (d->kind == AST_CLASS_DECL || d->kind == AST_STRUCT_DECL))
      macro_expand_derives_on_decl(&ctx, d);
  }

  /* Pass 3: Expand all hygienic macro & built-in macro invocations across unit */
  for (int i = 0; i < decls->count; i++) {
    zan_ast_node_t *d = decls->items[i];
    if (!d || d->kind == AST_MACRO_DECL)
      continue;
    if (d->kind == AST_CLASS_DECL || d->kind == AST_STRUCT_DECL ||
        d->kind == AST_INTERFACE_DECL) {
      for (int m = 0; m < d->type_decl.members.count; m++) {
        zan_ast_node_t *mem = d->type_decl.members.items[m];
        if (!mem || mem->kind == AST_MACRO_DECL)
          continue;
        macro_expand_node(&ctx, mem, false, 0);
      }
    }
  }

  /* Pass 4: Strip AST_MACRO_DECL templates so downstream passes never see them */
  int out_d = 0;
  for (int i = 0; i < decls->count; i++) {
    zan_ast_node_t *d = decls->items[i];
    if (!d || d->kind == AST_MACRO_DECL)
      continue;
    if (d->kind == AST_CLASS_DECL || d->kind == AST_STRUCT_DECL) {
      int out_m = 0;
      for (int m = 0; m < d->type_decl.members.count; m++) {
        zan_ast_node_t *mem = d->type_decl.members.items[m];
        if (mem && mem->kind != AST_MACRO_DECL)
          d->type_decl.members.items[out_m++] = mem;
      }
      d->type_decl.members.count = out_m;
    }
    decls->items[out_d++] = d;
  }
  decls->count = out_d;
}
