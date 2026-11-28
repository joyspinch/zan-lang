/* Compile-Time Function Evaluation (CTFE) & .rodata constant folding.
 * Evaluates `const` methods and `comptime(...)` expressions at compile time
 * into LLVM constants (scalars or private unnamed_addr constant globals). */

#include <math.h>

#define ZAN_CTFE_MAX_STEPS 2000000
#define ZAN_CTFE_MAX_DEPTH 64
#define ZAN_CTFE_MAX_ARRAY_LEN 65536
#define ZAN_CTFE_MAX_LOCALS 256

typedef enum {
  CTFE_VOID = 0,
  CTFE_INT,
  CTFE_FLOAT,
  CTFE_ARRAY,
  CTFE_STRING,
} ctfe_kind_t;

typedef struct ctfe_val ctfe_val_t;

typedef struct ctfe_array {
  int len;
  zan_type_t *elem_type;
  ctfe_val_t *elems;
  struct ctfe_array *next_alloc;
} ctfe_array_t;

struct ctfe_val {
  ctfe_kind_t kind;
  int64_t ival;
  double fval;
  ctfe_array_t *arr;
  zan_istr_t sval;
};

typedef struct {
  zan_istr_t name;
  ctfe_val_t val;
  zan_type_t *type;
  int scope_depth;
} ctfe_local_t;

typedef struct {
  zan_istr_t name;
  int64_t val;
} ctfe_cparam_t;

typedef struct {
  zan_irgen_t *g;
  zan_symbol_t *cur_type;
  ctfe_local_t locals[ZAN_CTFE_MAX_LOCALS];
  int local_count;
  int scope_depth;
  ctfe_cparam_t cparams[16];
  int cparam_count;
  ctfe_array_t *alloc_head;
  int steps;
  int call_depth;
  bool returning;
  bool breaking;
  bool continuing;
  ctfe_val_t ret_val;
  bool failed;
  char err_msg[256];
} ctfe_ctx_t;

static void ctfe_fail(ctfe_ctx_t *ctx, const char *msg) {
  if (!ctx->failed) {
    ctx->failed = true;
    snprintf(ctx->err_msg, sizeof(ctx->err_msg), "%s", msg ? msg : "ctfe error");
  }
}

static bool ctfe_step(ctfe_ctx_t *ctx) {
  if (ctx->failed)
    return false;
  if (++ctx->steps > ZAN_CTFE_MAX_STEPS) {
    ctfe_fail(ctx, "compile-time evaluation exceeded step budget");
    return false;
  }
  return true;
}

static void ctfe_ctx_cleanup(ctfe_ctx_t *ctx) {
  ctfe_array_t *a = ctx->alloc_head;
  while (a) {
    ctfe_array_t *nx = a->next_alloc;
    free(a->elems);
    free(a);
    a = nx;
  }
  ctx->alloc_head = NULL;
}

static ctfe_array_t *ctfe_alloc_array(ctfe_ctx_t *ctx, int len,
                                      zan_type_t *elem_type) {
  if (len <= 0 || len > ZAN_CTFE_MAX_ARRAY_LEN) {
    ctfe_fail(ctx, "compile-time array length out of bounds");
    return NULL;
  }
  ctfe_array_t *a = (ctfe_array_t *)calloc(1, sizeof(ctfe_array_t));
  if (!a) {
    ctfe_fail(ctx, "out of memory in compile-time evaluator");
    return NULL;
  }
  a->elems = (ctfe_val_t *)calloc((size_t)len, sizeof(ctfe_val_t));
  if (!a->elems) {
    free(a);
    ctfe_fail(ctx, "out of memory in compile-time evaluator");
    return NULL;
  }
  a->len = len;
  a->elem_type = elem_type;
  ctfe_kind_t ek = CTFE_INT;
  if (elem_type &&
      (elem_type->kind == TYPE_FLOAT || elem_type->kind == TYPE_DOUBLE))
    ek = CTFE_FLOAT;
  for (int i = 0; i < len; i++) {
    a->elems[i].kind = ek;
  }
  a->next_alloc = ctx->alloc_head;
  ctx->alloc_head = a;
  return a;
}

static ctfe_val_t ctfe_clone_val(ctfe_ctx_t *ctx, ctfe_val_t v) {
  if (v.kind == CTFE_ARRAY && v.arr) {
    ctfe_array_t *na = ctfe_alloc_array(ctx, v.arr->len, v.arr->elem_type);
    if (!na)
      return v;
    for (int i = 0; i < v.arr->len; i++)
      na->elems[i] = ctfe_clone_val(ctx, v.arr->elems[i]);
    v.arr = na;
  }
  return v;
}

static double ctfe_as_double(ctfe_val_t v) {
  return v.kind == CTFE_FLOAT ? v.fval : (double)v.ival;
}

static int64_t ctfe_as_int(ctfe_val_t v) {
  return v.kind == CTFE_FLOAT ? (int64_t)v.fval : v.ival;
}

static bool ctfe_as_bool(ctfe_val_t v) {
  return v.kind == CTFE_FLOAT ? (v.fval != 0.0) : (v.ival != 0);
}

static ctfe_val_t ctfe_make_int(int64_t v) {
  ctfe_val_t r;
  memset(&r, 0, sizeof(r));
  r.kind = CTFE_INT;
  r.ival = v;
  return r;
}

static ctfe_val_t ctfe_make_float(double v) {
  ctfe_val_t r;
  memset(&r, 0, sizeof(r));
  r.kind = CTFE_FLOAT;
  r.fval = v;
  return r;
}

static ctfe_val_t ctfe_cast_to_type(ctfe_val_t v, zan_type_t *t) {
  if (!t)
    return v;
  switch (t->kind) {
  case TYPE_FLOAT:
    return ctfe_make_float((double)(float)ctfe_as_double(v));
  case TYPE_DOUBLE:
    return ctfe_make_float(ctfe_as_double(v));
  case TYPE_BOOL:
    return ctfe_make_int(ctfe_as_bool(v) ? 1 : 0);
  case TYPE_BYTE:
    return ctfe_make_int((int64_t)(uint8_t)ctfe_as_int(v));
  case TYPE_SBYTE:
    return ctfe_make_int((int64_t)(int8_t)ctfe_as_int(v));
  case TYPE_SHORT:
    return ctfe_make_int((int64_t)(int16_t)ctfe_as_int(v));
  case TYPE_USHORT:
  case TYPE_CHAR:
    return ctfe_make_int((int64_t)(uint16_t)ctfe_as_int(v));
  case TYPE_INT:
    return ctfe_make_int((int64_t)(int32_t)ctfe_as_int(v));
  case TYPE_UINT:
    return ctfe_make_int((int64_t)(uint32_t)ctfe_as_int(v));
  case TYPE_LONG:
  case TYPE_ULONG:
  case TYPE_NINT:
  case TYPE_ENUM:
    return ctfe_make_int(ctfe_as_int(v));
  default:
    return v;
  }
}

static ctfe_local_t *ctfe_find_local(ctfe_ctx_t *ctx, zan_istr_t name) {
  for (int i = ctx->local_count - 1; i >= 0; i--) {
    if (ctx->locals[i].name.len == name.len &&
        memcmp(ctx->locals[i].name.str, name.str, (size_t)name.len) == 0)
      return &ctx->locals[i];
  }
  return NULL;
}

static bool ctfe_find_cparam(ctfe_ctx_t *ctx, zan_istr_t name, int64_t *out) {
  for (int i = ctx->cparam_count - 1; i >= 0; i--) {
    if (ctx->cparams[i].name.len == name.len &&
        memcmp(ctx->cparams[i].name.str, name.str, (size_t)name.len) == 0) {
      if (out)
        *out = ctx->cparams[i].val;
      return true;
    }
  }
  return false;
}

static ctfe_val_t ctfe_eval_expr(ctfe_ctx_t *ctx, zan_ast_node_t *e);
static void ctfe_eval_stmt(ctfe_ctx_t *ctx, zan_ast_node_t *s);

static ctfe_val_t ctfe_eval_math(ctfe_ctx_t *ctx, bool is_f32, zan_istr_t mn,
                                 ctfe_val_t *args, int argc) {
  double a0 = argc >= 1 ? ctfe_as_double(args[0]) : 0.0;
  double a1 = argc >= 2 ? ctfe_as_double(args[1]) : 0.0;
  double a2 = argc >= 3 ? ctfe_as_double(args[2]) : 0.0;
  if (is_f32) {
    a0 = (double)(float)a0;
    a1 = (double)(float)a1;
    a2 = (double)(float)a2;
  }
  bool any_float = is_f32 || (argc >= 1 && args[0].kind == CTFE_FLOAT) ||
                   (argc >= 2 && args[1].kind == CTFE_FLOAT) ||
                   (argc >= 3 && args[2].kind == CTFE_FLOAT);

  if (mn.len == 3 && memcmp(mn.str, "Abs", 3) == 0 && argc == 1) {
    if (!any_float) {
      int64_t v = args[0].ival;
      return ctfe_make_int(v < 0 ? -v : v);
    }
    double r = fabs(a0);
    return ctfe_make_float(is_f32 ? (double)(float)r : r);
  }
  if (mn.len == 3 && memcmp(mn.str, "Min", 3) == 0 && argc == 2) {
    if (!any_float)
      return ctfe_make_int(args[0].ival < args[1].ival ? args[0].ival : args[1].ival);
    double r = a0 < a1 ? a0 : a1;
    return ctfe_make_float(is_f32 ? (double)(float)r : r);
  }
  if (mn.len == 3 && memcmp(mn.str, "Max", 3) == 0 && argc == 2) {
    if (!any_float)
      return ctfe_make_int(args[0].ival > args[1].ival ? args[0].ival : args[1].ival);
    double r = a0 > a1 ? a0 : a1;
    return ctfe_make_float(is_f32 ? (double)(float)r : r);
  }
  if (mn.len == 5 && memcmp(mn.str, "Clamp", 5) == 0 && argc == 3) {
    if (!any_float) {
      int64_t v = args[0].ival, lo = args[1].ival, hi = args[2].ival;
      if (v < lo) v = lo;
      if (v > hi) v = hi;
      return ctfe_make_int(v);
    }
    double r = a0 < a1 ? a1 : (a0 > a2 ? a2 : a0);
    return ctfe_make_float(is_f32 ? (double)(float)r : r);
  }
  double r = 0.0;
  if (mn.len == 4 && memcmp(mn.str, "Sqrt", 4) == 0 && argc == 1)
    r = sqrt(a0);
  else if (mn.len == 3 && memcmp(mn.str, "Pow", 3) == 0 && argc == 2)
    r = pow(a0, a1);
  else if (mn.len == 3 && memcmp(mn.str, "Exp", 3) == 0 && argc == 1)
    r = exp(a0);
  else if (mn.len == 3 && memcmp(mn.str, "Log", 3) == 0 && argc == 1)
    r = log(a0);
  else if (mn.len == 4 && memcmp(mn.str, "Log2", 4) == 0 && argc == 1)
    r = log2(a0);
  else if (mn.len == 5 && memcmp(mn.str, "Log10", 5) == 0 && argc == 1)
    r = log10(a0);
  else if (mn.len == 3 && memcmp(mn.str, "Sin", 3) == 0 && argc == 1)
    r = sin(a0);
  else if (mn.len == 3 && memcmp(mn.str, "Cos", 3) == 0 && argc == 1)
    r = cos(a0);
  else if (mn.len == 3 && memcmp(mn.str, "Tan", 3) == 0 && argc == 1)
    r = tan(a0);
  else if (mn.len == 5 && memcmp(mn.str, "Floor", 5) == 0 && argc == 1)
    r = floor(a0);
  else if (mn.len == 7 && memcmp(mn.str, "Ceiling", 7) == 0 && argc == 1)
    r = ceil(a0);
  else if (mn.len == 5 && memcmp(mn.str, "Round", 5) == 0 && argc == 1)
    r = round(a0);
  else if (mn.len == 16 && memcmp(mn.str, "FusedMultiplyAdd", 16) == 0 && argc == 3)
    r = fma(a0, a1, a2);
  else {
    ctfe_fail(ctx, "unsupported Math function in compile-time evaluation");
    return ctfe_make_int(0);
  }
  return ctfe_make_float(is_f32 ? (double)(float)r : r);
}

static zan_symbol_t *ctfe_find_method(zan_symbol_t *owner, zan_istr_t name,
                                      int argc) {
  if (!owner)
    return NULL;
  zan_symbol_t *fallback = NULL;
  for (int i = 0; i < owner->member_count; i++) {
    zan_symbol_t *m = owner->members[i];
    if (!m || m->kind != SYM_METHOD || !m->decl ||
        m->decl->kind != AST_METHOD_DECL)
      continue;
    if (m->name.len != name.len ||
        memcmp(m->name.str, name.str, (size_t)name.len) != 0)
      continue;
    if (m->decl->method_decl.params.count == argc)
      return m;
    if (!fallback)
      fallback = m;
  }
  return fallback;
}

static int64_t ctfe_resolve_type_arg_const(ctfe_ctx_t *ctx,
                                           zan_ast_node_t *ta) {
  if (!ta)
    return 0;
  if (ta->kind == AST_INT_LITERAL)
    return ta->int_val;
  if (ta->kind == AST_TYPE_REF) {
    if (ta->type_ref.is_const_int)
      return ta->type_ref.const_int_val;
    int64_t v = 0;
    if (ctfe_find_cparam(ctx, ta->type_ref.name, &v))
      return v;
    /* Also check static const field with this name */
    if (ctx->cur_type) {
      zan_symbol_t *fs = get_field_sym(ctx->cur_type, ta->type_ref.name);
      if (fs && fs->decl && fs->decl->field_decl.initializer &&
          (fs->decl->field_decl.modifiers & MOD_READONLY)) {
        ctfe_val_t fv = ctfe_eval_expr(ctx, fs->decl->field_decl.initializer);
        if (!ctx->failed)
          return ctfe_as_int(fv);
      }
    }
  }
  return 0;
}

static ctfe_val_t ctfe_call_method(ctfe_ctx_t *ctx, zan_symbol_t *owner,
                                   zan_symbol_t *msym, zan_ast_node_t *call_expr,
                                   ctfe_val_t *args, int argc) {
  if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL ||
      !msym->decl->method_decl.body) {
    ctfe_fail(ctx, "callee has no compile-time body");
    return ctfe_make_int(0);
  }
  if (!(msym->decl->method_decl.modifiers & MOD_CONST)) {
    ctfe_fail(ctx, "only 'const' methods can be called at compile time");
    return ctfe_make_int(0);
  }
  if (ctx->call_depth >= ZAN_CTFE_MAX_DEPTH) {
    ctfe_fail(ctx, "compile-time call recursion limit exceeded");
    return ctfe_make_int(0);
  }

  /* Save outer call frame state */
  int saved_local_count = ctx->local_count;
  int saved_scope_depth = ctx->scope_depth;
  int saved_cparam_count = ctx->cparam_count;
  zan_symbol_t *saved_type = ctx->cur_type;
  bool saved_ret = ctx->returning;
  bool saved_brk = ctx->breaking;
  bool saved_cnt = ctx->continuing;
  ctfe_val_t saved_ret_val = ctx->ret_val;

  /* Isolate frame locals while keeping recursion clean */
  ctfe_local_t saved_locals_copy[ZAN_CTFE_MAX_LOCALS];
  if (saved_local_count > 0)
    memcpy(saved_locals_copy, ctx->locals,
           (size_t)saved_local_count * sizeof(ctfe_local_t));
  ctx->local_count = 0;
  ctx->scope_depth = 0;
  ctx->returning = false;
  ctx->breaking = false;
  ctx->continuing = false;
  memset(&ctx->ret_val, 0, sizeof(ctx->ret_val));
  ctx->cur_type = owner ? owner : msym->parent;
  ctx->call_depth++;

  /* Bind const generic parameters if present */
  zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
  for (int ti = 0; ti < tps->count && ti < call_expr->call.type_args.count; ti++) {
    zan_ast_node_t *tp = tps->items[ti];
    if (tp && tp->kind == AST_IDENTIFIER && tp->ident.is_const_param &&
        ctx->cparam_count < 16) {
      int64_t cval =
          ctfe_resolve_type_arg_const(ctx, call_expr->call.type_args.items[ti]);
      ctx->cparams[ctx->cparam_count].name = tp->ident.name;
      ctx->cparams[ctx->cparam_count].val = cval;
      ctx->cparam_count++;
    }
  }

  /* Bind parameters */
  zan_ast_list_t *params = &msym->decl->method_decl.params;
  for (int i = 0; i < params->count && i < argc; i++) {
    zan_ast_node_t *p = params->items[i];
    if (!p || p->kind != AST_PARAM)
      continue;
    if (ctx->local_count >= ZAN_CTFE_MAX_LOCALS) {
      ctfe_fail(ctx, "compile-time local variable limit exceeded");
      break;
    }
    zan_type_t *pt =
        p->param.type ? zan_binder_resolve_type(ctx->g->binder, p->param.type)
                      : NULL;
    ctx->locals[ctx->local_count].name = p->param.name;
    ctx->locals[ctx->local_count].val =
        ctfe_clone_val(ctx, pt ? ctfe_cast_to_type(args[i], pt) : args[i]);
    ctx->locals[ctx->local_count].type = pt;
    ctx->locals[ctx->local_count].scope_depth = 0;
    ctx->local_count++;
  }

  if (!ctx->failed)
    ctfe_eval_stmt(ctx, msym->decl->method_decl.body);

  ctfe_val_t result = ctx->ret_val;
  if (msym->decl->method_decl.return_type) {
    zan_type_t *rt = zan_binder_resolve_type(
        ctx->g->binder, msym->decl->method_decl.return_type);
    if (rt && result.kind != CTFE_ARRAY)
      result = ctfe_cast_to_type(result, rt);
  }

  /* Restore outer frame */
  ctx->call_depth--;
  if (saved_local_count > 0)
    memcpy(ctx->locals, saved_locals_copy,
           (size_t)saved_local_count * sizeof(ctfe_local_t));
  ctx->local_count = saved_local_count;
  ctx->scope_depth = saved_scope_depth;
  ctx->cparam_count = saved_cparam_count;
  ctx->cur_type = saved_type;
  ctx->returning = saved_ret;
  ctx->breaking = saved_brk;
  ctx->continuing = saved_cnt;
  ctx->ret_val = saved_ret_val;

  return result;
}

static ctfe_val_t ctfe_apply_binary(ctfe_ctx_t *ctx, zan_token_kind_t op,
                                    ctfe_val_t lv, ctfe_val_t rv) {
  bool is_flt = (lv.kind == CTFE_FLOAT || rv.kind == CTFE_FLOAT);
  if (is_flt) {
    double a = ctfe_as_double(lv), b = ctfe_as_double(rv);
    switch (op) {
    case TK_PLUS: return ctfe_make_float(a + b);
    case TK_MINUS: return ctfe_make_float(a - b);
    case TK_STAR: return ctfe_make_float(a * b);
    case TK_SLASH: return ctfe_make_float(a / b);
    case TK_PERCENT: return ctfe_make_float(fmod(a, b));
    case TK_EQ_EQ: return ctfe_make_int(a == b);
    case TK_BANG_EQ: return ctfe_make_int(a != b);
    case TK_LESS: return ctfe_make_int(a < b);
    case TK_LESS_EQ: return ctfe_make_int(a <= b);
    case TK_GREATER: return ctfe_make_int(a > b);
    case TK_GREATER_EQ: return ctfe_make_int(a >= b);
    default:
      ctfe_fail(ctx, "unsupported float operator in compile-time evaluation");
      return ctfe_make_int(0);
    }
  }
  int64_t a = lv.ival, b = rv.ival;
  switch (op) {
  case TK_PLUS: return ctfe_make_int(a + b);
  case TK_MINUS: return ctfe_make_int(a - b);
  case TK_STAR: return ctfe_make_int(a * b);
  case TK_SLASH:
    if (b == 0) {
      ctfe_fail(ctx, "compile-time division by zero");
      return ctfe_make_int(0);
    }
    return ctfe_make_int(a / b);
  case TK_PERCENT:
    if (b == 0) {
      ctfe_fail(ctx, "compile-time modulo by zero");
      return ctfe_make_int(0);
    }
    return ctfe_make_int(a % b);
  case TK_AMP: return ctfe_make_int(a & b);
  case TK_PIPE: return ctfe_make_int(a | b);
  case TK_CARET: return ctfe_make_int(a ^ b);
  case TK_LESS_LESS: return ctfe_make_int(a << (b & 63));
  case TK_GREATER_GREATER: return ctfe_make_int(a >> (b & 63));
  case TK_GREATER_GREATER_GREATER:
    return ctfe_make_int((int64_t)((uint64_t)a >> (b & 63)));
  case TK_EQ_EQ: return ctfe_make_int(a == b);
  case TK_BANG_EQ: return ctfe_make_int(a != b);
  case TK_LESS: return ctfe_make_int(a < b);
  case TK_LESS_EQ: return ctfe_make_int(a <= b);
  case TK_GREATER: return ctfe_make_int(a > b);
  case TK_GREATER_EQ: return ctfe_make_int(a >= b);
  default:
    ctfe_fail(ctx, "unsupported binary operator in compile-time evaluation");
    return ctfe_make_int(0);
  }
}

static ctfe_val_t *ctfe_lvalue_ptr(ctfe_ctx_t *ctx, zan_ast_node_t *lhs,
                                   zan_type_t **out_type) {
  if (!lhs)
    return NULL;
  if (lhs->kind == AST_IDENTIFIER) {
    ctfe_local_t *l = ctfe_find_local(ctx, lhs->ident.name);
    if (!l) {
      ctfe_fail(ctx, "cannot mutate non-local variable in compile-time evaluation");
      return NULL;
    }
    if (out_type)
      *out_type = l->type;
    return &l->val;
  }
  if (lhs->kind == AST_INDEX) {
    ctfe_val_t *arrp = ctfe_lvalue_ptr(ctx, lhs->index.object, NULL);
    if (!arrp || arrp->kind != CTFE_ARRAY || !arrp->arr) {
      ctfe_fail(ctx, "invalid compile-time array target");
      return NULL;
    }
    ctfe_val_t idxv = ctfe_eval_expr(ctx, lhs->index.index);
    if (ctx->failed)
      return NULL;
    int64_t idx = ctfe_as_int(idxv);
    if (idx < 0 || idx >= arrp->arr->len) {
      ctfe_fail(ctx, "compile-time array index out of bounds");
      return NULL;
    }
    if (out_type)
      *out_type = arrp->arr->elem_type;
    return &arrp->arr->elems[idx];
  }
  ctfe_fail(ctx, "unsupported assignment target in compile-time evaluation");
  return NULL;
}

static ctfe_val_t ctfe_eval_expr(ctfe_ctx_t *ctx, zan_ast_node_t *e) {
  if (!e || !ctfe_step(ctx))
    return ctfe_make_int(0);
  switch (e->kind) {
  case AST_INT_LITERAL:
    return ctfe_make_int(e->int_val);
  case AST_FLOAT_LITERAL:
    return ctfe_make_float(e->float_val);
  case AST_BOOL_LITERAL:
    return ctfe_make_int(e->bool_val ? 1 : 0);
  case AST_CHAR_LITERAL:
    return ctfe_make_int(e->int_val);
  case AST_STRING_LITERAL: {
    ctfe_val_t v;
    memset(&v, 0, sizeof(v));
    v.kind = CTFE_STRING;
    v.sval = e->str_val;
    return v;
  }
  case AST_SIZEOF_EXPR: {
    int64_t qv = 0;
    if (zan_irgen_eval_layout_query(ctx->g, e, &qv))
      return ctfe_make_int(qv);
    ctfe_fail(ctx, "cannot evaluate layout query at compile time");
    return ctfe_make_int(0);
  }
  case AST_IDENTIFIER: {
    ctfe_local_t *l = ctfe_find_local(ctx, e->ident.name);
    if (l)
      return l->val;
    int64_t cpv = 0;
    if (ctfe_find_cparam(ctx, e->ident.name, &cpv))
      return ctfe_make_int(cpv);
    if (ctx->cur_type) {
      zan_symbol_t *fs = get_field_sym(ctx->cur_type, e->ident.name);
      if (fs && fs->decl && fs->decl->field_decl.initializer &&
          (fs->decl->field_decl.modifiers & MOD_READONLY)) {
        return ctfe_eval_expr(ctx, fs->decl->field_decl.initializer);
      }
    }
    ctfe_fail(ctx, "non-constant identifier in compile-time evaluation");
    return ctfe_make_int(0);
  }
  case AST_MEMBER_ACCESS: {
    zan_ast_node_t *obj = e->member.object;
    zan_istr_t mn = e->member.name;
    /* Math.PI / Math.E / MathF.PI / MathF.E */
    if (obj && obj->kind == AST_IDENTIFIER) {
      if ((obj->ident.name.len == 4 && memcmp(obj->ident.name.str, "Math", 4) == 0) ||
          (obj->ident.name.len == 5 && memcmp(obj->ident.name.str, "MathF", 5) == 0)) {
        bool is_f = (obj->ident.name.len == 5);
        if (mn.len == 2 && memcmp(mn.str, "PI", 2) == 0)
          return ctfe_make_float(is_f ? (double)3.14159265358979323846f : 3.14159265358979323846);
        if (mn.len == 1 && mn.str[0] == 'E')
          return ctfe_make_float(is_f ? (double)2.71828182845904523536f : 2.71828182845904523536);
      }
      /* Static readonly/const field on another class or enum member */
      zan_symbol_t *ts = zan_binder_lookup(ctx->g->binder, obj->ident.name);
      if (ts && ts->kind == SYM_ENUM) {
        int64_t next_ev = 0;
        for (int i = 0; i < ts->member_count; i++) {
          zan_symbol_t *em = ts->members[i];
          int64_t ev = next_ev;
          if (em && em->decl && em->decl->kind == AST_ENUM_MEMBER &&
              em->decl->enum_member.value) {
            ctfe_val_t vv = ctfe_eval_expr(ctx, em->decl->enum_member.value);
            if (!ctx->failed)
              ev = ctfe_as_int(vv);
          }
          next_ev = ev + 1;
          if (em && em->name.len == mn.len &&
              memcmp(em->name.str, mn.str, (size_t)mn.len) == 0)
            return ctfe_make_int(ev);
        }
      }
      if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT)) {
        zan_symbol_t *fs = get_field_sym(ts, mn);
        if (fs && fs->decl && fs->decl->field_decl.initializer &&
            (fs->decl->field_decl.modifiers & MOD_READONLY)) {
          zan_symbol_t *saved_t = ctx->cur_type;
          ctx->cur_type = ts;
          ctfe_val_t v = ctfe_eval_expr(ctx, fs->decl->field_decl.initializer);
          ctx->cur_type = saved_t;
          return v;
        }
      }
    }
    /* arr.Length */
    if (mn.len == 6 && memcmp(mn.str, "Length", 6) == 0) {
      ctfe_val_t ov = ctfe_eval_expr(ctx, obj);
      if (!ctx->failed && ov.kind == CTFE_ARRAY && ov.arr)
        return ctfe_make_int(ov.arr->len);
    }
    ctfe_fail(ctx, "unsupported member access in compile-time evaluation");
    return ctfe_make_int(0);
  }
  case AST_INDEX: {
    ctfe_val_t ov = ctfe_eval_expr(ctx, e->index.object);
    ctfe_val_t iv = ctfe_eval_expr(ctx, e->index.index);
    if (ctx->failed)
      return ctfe_make_int(0);
    if (ov.kind != CTFE_ARRAY || !ov.arr) {
      ctfe_fail(ctx, "indexing non-array in compile-time evaluation");
      return ctfe_make_int(0);
    }
    int64_t idx = ctfe_as_int(iv);
    if (idx < 0 || idx >= ov.arr->len) {
      ctfe_fail(ctx, "compile-time array index out of bounds");
      return ctfe_make_int(0);
    }
    return ov.arr->elems[idx];
  }
  case AST_UNARY: {
    if (e->unary.op == TK_PLUS_PLUS || e->unary.op == TK_MINUS_MINUS) {
      zan_type_t *lt = NULL;
      ctfe_val_t *p = ctfe_lvalue_ptr(ctx, e->unary.operand, &lt);
      if (!p)
        return ctfe_make_int(0);
      int64_t delta = (e->unary.op == TK_PLUS_PLUS) ? 1 : -1;
      if (p->kind == CTFE_FLOAT) p->fval += (double)delta;
      else p->ival += delta;
      if (lt) *p = ctfe_cast_to_type(*p, lt);
      return *p;
    }
    ctfe_val_t v = ctfe_eval_expr(ctx, e->unary.operand);
    if (ctx->failed)
      return v;
    switch (e->unary.op) {
    case TK_MINUS:
      return v.kind == CTFE_FLOAT ? ctfe_make_float(-v.fval) : ctfe_make_int(-v.ival);
    case TK_PLUS:
      return v;
    case TK_BANG:
      return ctfe_make_int(!ctfe_as_bool(v));
    case TK_TILDE:
      return ctfe_make_int(~v.ival);
    default:
      ctfe_fail(ctx, "unsupported unary operator in compile-time evaluation");
      return ctfe_make_int(0);
    }
  }
  case AST_POSTFIX_UNARY: {
    if (e->unary.op == TK_PLUS_PLUS || e->unary.op == TK_MINUS_MINUS) {
      zan_type_t *lt = NULL;
      ctfe_val_t *p = ctfe_lvalue_ptr(ctx, e->unary.operand, &lt);
      if (!p)
        return ctfe_make_int(0);
      ctfe_val_t old = *p;
      int64_t delta = (e->unary.op == TK_PLUS_PLUS) ? 1 : -1;
      if (p->kind == CTFE_FLOAT) p->fval += (double)delta;
      else p->ival += delta;
      if (lt) *p = ctfe_cast_to_type(*p, lt);
      return old;
    }
    return ctfe_eval_expr(ctx, e->unary.operand);
  }
  case AST_BINARY: {
    if (e->binary.op == TK_AMP_AMP) {
      ctfe_val_t lv = ctfe_eval_expr(ctx, e->binary.left);
      if (ctx->failed || !ctfe_as_bool(lv))
        return ctfe_make_int(0);
      ctfe_val_t rv = ctfe_eval_expr(ctx, e->binary.right);
      return ctfe_make_int(ctfe_as_bool(rv) ? 1 : 0);
    }
    if (e->binary.op == TK_PIPE_PIPE) {
      ctfe_val_t lv = ctfe_eval_expr(ctx, e->binary.left);
      if (ctx->failed)
        return ctfe_make_int(0);
      if (ctfe_as_bool(lv))
        return ctfe_make_int(1);
      ctfe_val_t rv = ctfe_eval_expr(ctx, e->binary.right);
      return ctfe_make_int(ctfe_as_bool(rv) ? 1 : 0);
    }
    ctfe_val_t lv = ctfe_eval_expr(ctx, e->binary.left);
    ctfe_val_t rv = ctfe_eval_expr(ctx, e->binary.right);
    if (ctx->failed)
      return ctfe_make_int(0);
    return ctfe_apply_binary(ctx, e->binary.op, lv, rv);
  }
  case AST_ASSIGNMENT: {
    zan_type_t *lt = NULL;
    ctfe_val_t *lp = ctfe_lvalue_ptr(ctx, e->binary.left, &lt);
    if (!lp)
      return ctfe_make_int(0);
    ctfe_val_t rv = ctfe_eval_expr(ctx, e->binary.right);
    if (ctx->failed)
      return ctfe_make_int(0);
    if (e->binary.compound_base != TK_EOF && e->binary.compound_base != 0) {
      rv = ctfe_apply_binary(ctx, e->binary.compound_base, *lp, rv);
    }
    if (lt && rv.kind != CTFE_ARRAY)
      rv = ctfe_cast_to_type(rv, lt);
    *lp = ctfe_clone_val(ctx, rv);
    return *lp;
  }
  case AST_CONDITIONAL: {
    ctfe_val_t c = ctfe_eval_expr(ctx, e->conditional.cond);
    if (ctx->failed)
      return ctfe_make_int(0);
    return ctfe_as_bool(c) ? ctfe_eval_expr(ctx, e->conditional.then_expr)
                           : ctfe_eval_expr(ctx, e->conditional.else_expr);
  }
  case AST_CAST_EXPR: {
    ctfe_val_t v = ctfe_eval_expr(ctx, e->cast.expr);
    if (ctx->failed)
      return v;
    zan_type_t *tt = zan_binder_resolve_type(ctx->g->binder, e->cast.type);
    return ctfe_cast_to_type(v, tt);
  }
  case AST_NEW_EXPR: {
    if (e->new_expr.type &&
        (e->new_expr.type->kind == AST_TYPE_REF ||
         e->new_expr.type->kind == AST_GENERIC_TYPE) &&
        e->new_expr.type->type_ref.name.len == 11 &&
        memcmp(e->new_expr.type->type_ref.name.str, "InlineArray", 11) == 0 &&
        e->new_expr.type->type_ref.type_args.count == 2) {
      int64_t len = ctfe_resolve_type_arg_const(
          ctx, e->new_expr.type->type_ref.type_args.items[0]);
      zan_type_t *et = zan_binder_resolve_type(
          ctx->g->binder, e->new_expr.type->type_ref.type_args.items[1]);
      ctfe_array_t *arr = ctfe_alloc_array(ctx, (int)len, et);
      ctfe_val_t r;
      memset(&r, 0, sizeof(r));
      r.kind = CTFE_ARRAY;
      r.arr = arr;
      return r;
    }
    ctfe_fail(ctx, "unsupported 'new' expression in compile-time evaluation");
    return ctfe_make_int(0);
  }
  case AST_CALL: {
    /* Nested comptime(inner) */
    if (e->call.callee && e->call.callee->kind == AST_IDENTIFIER &&
        e->call.callee->ident.name.len == 8 &&
        memcmp(e->call.callee->ident.name.str, "comptime", 8) == 0 &&
        e->call.args.count == 1) {
      return ctfe_eval_expr(ctx, e->call.args.items[0]);
    }
    int argc = e->call.args.count;
    ctfe_val_t args[32];
    if (argc > 32) {
      ctfe_fail(ctx, "too many arguments in compile-time call");
      return ctfe_make_int(0);
    }
    for (int i = 0; i < argc; i++) {
      args[i] = ctfe_eval_expr(ctx, e->call.args.items[i]);
      if (ctx->failed)
        return ctfe_make_int(0);
    }
    if (e->call.callee && e->call.callee->kind == AST_MEMBER_ACCESS) {
      zan_ast_node_t *obj = e->call.callee->member.object;
      zan_istr_t mn = e->call.callee->member.name;
      if (obj && obj->kind == AST_IDENTIFIER) {
        if (obj->ident.name.len == 4 && memcmp(obj->ident.name.str, "Math", 4) == 0)
          return ctfe_eval_math(ctx, false, mn, args, argc);
        if (obj->ident.name.len == 5 && memcmp(obj->ident.name.str, "MathF", 5) == 0)
          return ctfe_eval_math(ctx, true, mn, args, argc);
        zan_symbol_t *ts = zan_binder_lookup(ctx->g->binder, obj->ident.name);
        if (ts) {
          zan_symbol_t *ms = ctfe_find_method(ts, mn, argc);
          if (ms)
            return ctfe_call_method(ctx, ts, ms, e, args, argc);
        }
      }
    } else if (e->call.callee && e->call.callee->kind == AST_IDENTIFIER) {
      zan_symbol_t *ms =
          ctfe_find_method(ctx->cur_type, e->call.callee->ident.name, argc);
      if (ms)
        return ctfe_call_method(ctx, ctx->cur_type, ms, e, args, argc);
    }
    ctfe_fail(ctx, "cannot call non-const function at compile time");
    return ctfe_make_int(0);
  }
  default:
    ctfe_fail(ctx, "unsupported expression in compile-time evaluation");
    return ctfe_make_int(0);
  }
}

static void ctfe_eval_stmt(ctfe_ctx_t *ctx, zan_ast_node_t *s) {
  if (!s || ctx->failed || ctx->returning || ctx->breaking || ctx->continuing)
    return;
  if (!ctfe_step(ctx))
    return;
  switch (s->kind) {
  case AST_BLOCK: {
    int saved_lc = ctx->local_count;
    ctx->scope_depth++;
    for (int i = 0; i < s->block.stmts.count; i++) {
      ctfe_eval_stmt(ctx, s->block.stmts.items[i]);
      if (ctx->failed || ctx->returning || ctx->breaking || ctx->continuing)
        break;
    }
    ctx->scope_depth--;
    ctx->local_count = saved_lc;
    break;
  }
  case AST_VAR_DECL: {
    if (ctx->local_count >= ZAN_CTFE_MAX_LOCALS) {
      ctfe_fail(ctx, "compile-time local variable limit exceeded");
      return;
    }
    zan_type_t *vt =
        s->var_decl.type
            ? zan_binder_resolve_type(ctx->g->binder, s->var_decl.type)
            : NULL;
    ctfe_val_t init = ctfe_make_int(0);
    if (s->var_decl.initializer) {
      init = ctfe_eval_expr(ctx, s->var_decl.initializer);
      if (ctx->failed)
        return;
      if (vt && init.kind != CTFE_ARRAY)
        init = ctfe_cast_to_type(init, vt);
    } else if (vt && is_inline_array_type(vt)) {
      int64_t alen = inline_array_len(vt);
      if (alen <= 0 && s->var_decl.type &&
          (s->var_decl.type->kind == AST_TYPE_REF ||
           s->var_decl.type->kind == AST_GENERIC_TYPE) &&
          s->var_decl.type->type_ref.type_args.count == 2) {
        alen = ctfe_resolve_type_arg_const(
            ctx, s->var_decl.type->type_ref.type_args.items[0]);
      }
      init.kind = CTFE_ARRAY;
      init.arr = ctfe_alloc_array(ctx, (int)alen, inline_array_elem(vt));
    } else if (vt && (vt->kind == TYPE_FLOAT || vt->kind == TYPE_DOUBLE)) {
      init = ctfe_make_float(0.0);
    }
    ctx->locals[ctx->local_count].name = s->var_decl.name;
    ctx->locals[ctx->local_count].val = ctfe_clone_val(ctx, init);
    ctx->locals[ctx->local_count].type = vt;
    ctx->locals[ctx->local_count].scope_depth = ctx->scope_depth;
    ctx->local_count++;
    break;
  }
  case AST_EXPR_STMT:
    (void)ctfe_eval_expr(ctx, s->expr_stmt.expr);
    break;
  case AST_RETURN_STMT:
    if (s->ret.value) {
      ctx->ret_val = ctfe_eval_expr(ctx, s->ret.value);
    } else {
      memset(&ctx->ret_val, 0, sizeof(ctx->ret_val));
    }
    ctx->returning = true;
    break;
  case AST_IF_STMT: {
    ctfe_val_t c = ctfe_eval_expr(ctx, s->if_stmt.cond);
    if (ctx->failed)
      return;
    if (ctfe_as_bool(c))
      ctfe_eval_stmt(ctx, s->if_stmt.then_body);
    else if (s->if_stmt.else_body)
      ctfe_eval_stmt(ctx, s->if_stmt.else_body);
    break;
  }
  case AST_FOR_STMT: {
    int saved_lc = ctx->local_count;
    ctx->scope_depth++;
    if (s->for_stmt.init)
      ctfe_eval_stmt(ctx, s->for_stmt.init);
    while (!ctx->failed && !ctx->returning) {
      if (s->for_stmt.cond) {
        ctfe_val_t c = ctfe_eval_expr(ctx, s->for_stmt.cond);
        if (ctx->failed || !ctfe_as_bool(c))
          break;
      }
      ctfe_eval_stmt(ctx, s->for_stmt.body);
      if (ctx->failed || ctx->returning)
        break;
      if (ctx->breaking) {
        ctx->breaking = false;
        break;
      }
      ctx->continuing = false;
      if (s->for_stmt.step)
        (void)ctfe_eval_expr(ctx, s->for_stmt.step);
    }
    ctx->scope_depth--;
    ctx->local_count = saved_lc;
    break;
  }
  case AST_WHILE_STMT: {
    while (!ctx->failed && !ctx->returning) {
      ctfe_val_t c = ctfe_eval_expr(ctx, s->while_stmt.cond);
      if (ctx->failed || !ctfe_as_bool(c))
        break;
      ctfe_eval_stmt(ctx, s->while_stmt.body);
      if (ctx->failed || ctx->returning)
        break;
      if (ctx->breaking) {
        ctx->breaking = false;
        break;
      }
      ctx->continuing = false;
    }
    break;
  }
  case AST_DO_WHILE_STMT: {
    do {
      ctfe_eval_stmt(ctx, s->while_stmt.body);
      if (ctx->failed || ctx->returning)
        break;
      if (ctx->breaking) {
        ctx->breaking = false;
        break;
      }
      ctx->continuing = false;
      ctfe_val_t c = ctfe_eval_expr(ctx, s->while_stmt.cond);
      if (ctx->failed || !ctfe_as_bool(c))
        break;
    } while (1);
    break;
  }
  case AST_BREAK_STMT:
    ctx->breaking = true;
    break;
  case AST_CONTINUE_STMT:
    ctx->continuing = true;
    break;
  default:
    ctfe_fail(ctx, "unsupported statement in compile-time evaluation");
    break;
  }
}

static LLVMValueRef ctfe_val_to_llvm_const(zan_irgen_t *g, ctfe_val_t v,
                                           zan_type_t *target_type) {
  if (v.kind == CTFE_ARRAY && v.arr) {
    zan_type_t *et = v.arr->elem_type;
    if (!et && target_type && is_inline_array_type(target_type))
      et = inline_array_elem(target_type);
    LLVMTypeRef elt = et ? map_type(g, et) : LLVMInt32TypeInContext(g->ctx);
    int n = v.arr->len;
    LLVMValueRef *vals = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    for (int i = 0; i < n; i++) {
      vals[i] = ctfe_val_to_llvm_const(g, v.arr->elems[i], et);
    }
    LLVMValueRef carr = LLVMConstArray2(elt, vals, (uint64_t)n);
    free(vals);
    return carr;
  }
  if (v.kind == CTFE_STRING) {
    return NULL; /* handled by emit_string_literal_rc at call sites */
  }
  if (target_type) {
    LLVMTypeRef lt = map_type(g, target_type);
    if (target_type->kind == TYPE_FLOAT)
      return LLVMConstReal(LLVMFloatTypeInContext(g->ctx),
                           (double)(float)ctfe_as_double(v));
    if (target_type->kind == TYPE_DOUBLE)
      return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), ctfe_as_double(v));
    if (target_type->kind == TYPE_BOOL)
      return LLVMConstInt(LLVMInt1TypeInContext(g->ctx),
                          ctfe_as_bool(v) ? 1 : 0, 0);
    if (LLVMGetTypeKind(lt) == LLVMIntegerTypeKind)
      return LLVMConstInt(lt, (unsigned long long)ctfe_as_int(v), 1);
  }
  if (v.kind == CTFE_FLOAT)
    return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), v.fval);
  return LLVMConstInt(LLVMInt32TypeInContext(g->ctx),
                      (unsigned long long)v.ival, 1);
}

bool zan_ctfe_expr_wants_rodata(zan_ast_node_t *init) {
  if (!init)
    return false;
  if (init->kind == AST_CALL && init->call.callee &&
      init->call.callee->kind == AST_IDENTIFIER &&
      init->call.callee->ident.name.len == 8 &&
      memcmp(init->call.callee->ident.name.str, "comptime", 8) == 0)
    return true;
  if (init->kind == AST_CALL)
    return true;
  return false;
}

bool zan_ctfe_eval_to_llvm_const(zan_irgen_t *g, zan_symbol_t *owner_type,
                                 zan_ast_node_t *expr, zan_type_t *target_type,
                                 LLVMValueRef *out_const) {
  if (!g || !expr || !out_const)
    return false;
  ctfe_ctx_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.g = g;
  ctx.cur_type = owner_type ? owner_type : g->current_type_sym;
  ctfe_val_t v = ctfe_eval_expr(&ctx, expr);
  if (ctx.failed) {
    ctfe_ctx_cleanup(&ctx);
    return false;
  }
  LLVMValueRef c = ctfe_val_to_llvm_const(g, v, target_type);
  ctfe_ctx_cleanup(&ctx);
  if (!c)
    return false;
  *out_const = c;
  return true;
}

LLVMValueRef zan_ctfe_emit_comptime(zan_irgen_t *g, zan_ast_node_t *call_expr,
                                    local_scope_t *locals) {
  zan_ast_node_t *inner = call_expr->call.args.items[0];
  zan_type_t *ty = infer_expr_type(g, inner, locals);
  ctfe_ctx_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.g = g;
  ctx.cur_type = g->current_type_sym;
  /* Pass enclosing const generic params if inside a const-generic method */
  if (g->cur_mtps && g->cur_mbind) {
    for (int i = 0; i < g->cur_mtps->count && ctx.cparam_count < 16; i++) {
      zan_ast_node_t *tp = g->cur_mtps->items[i];
      if (tp && tp->kind == AST_IDENTIFIER && tp->ident.is_const_param &&
          g->cur_mbind[i] && g->cur_mbind[i]->kind == TYPE_CONST_INT) {
        ctx.cparams[ctx.cparam_count].name = tp->ident.name;
        ctx.cparams[ctx.cparam_count].val = g->cur_mbind[i]->const_int_val;
        ctx.cparam_count++;
      }
    }
  }
  ctfe_val_t v = ctfe_eval_expr(&ctx, inner);
  if (ctx.failed) {
    zan_diag_emit_code(g->diag, DIAG_ERROR, call_expr->loc, 3145,
                       "comptime(...) evaluation failed: %s", ctx.err_msg);
    ctfe_ctx_cleanup(&ctx);
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
  }
  if (v.kind == CTFE_STRING) {
    LLVMValueRef sv = emit_string_literal_rc(g, v.sval);
    ctfe_ctx_cleanup(&ctx);
    return sv;
  }
  LLVMValueRef c = ctfe_val_to_llvm_const(g, v, ty);
  ctfe_ctx_cleanup(&ctx);
  if (!c) {
    zan_diag_emit_code(g->diag, DIAG_ERROR, call_expr->loc, 3145,
                       "comptime(...) could not lower result to constant");
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
  }
  if (v.kind == CTFE_ARRAY) {
    /* Place compile-time array constant into .rodata */
    static int rodata_id = 0;
    char gname[64];
    snprintf(gname, sizeof(gname), "__zan_rodata_%d",
             (int)__atomic_fetch_add(&rodata_id, 1, __ATOMIC_SEQ_CST));
    LLVMTypeRef cty = LLVMTypeOf(c);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, cty, gname);
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv, 1);
    LLVMSetUnnamedAddr(gv, LLVMGlobalUnnamedAddr);
    LLVMSetInitializer(gv, c);
    return LLVMBuildLoad2(g->builder, cty, gv, "ctfe.ro");
  }
  return c;
}

void zan_ctfe_eval_static_assert(zan_irgen_t *g, zan_ast_node_t *call_expr) {
  if (!g || !call_expr || call_expr->call.args.count < 1)
    return;
  zan_ast_node_t *cond = call_expr->call.args.items[0];
  ctfe_ctx_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.g = g;
  ctx.cur_type = g->current_type_sym;
  if (g->cur_mtps && g->cur_mbind) {
    for (int i = 0; i < g->cur_mtps->count && ctx.cparam_count < 16; i++) {
      zan_ast_node_t *tp = g->cur_mtps->items[i];
      if (tp && tp->kind == AST_IDENTIFIER && tp->ident.is_const_param &&
          g->cur_mbind[i] && g->cur_mbind[i]->kind == TYPE_CONST_INT) {
        ctx.cparams[ctx.cparam_count].name = tp->ident.name;
        ctx.cparams[ctx.cparam_count].val = g->cur_mbind[i]->const_int_val;
        ctx.cparam_count++;
      }
    }
  }
  ctfe_val_t v = ctfe_eval_expr(&ctx, cond);
  if (ctx.failed) {
    zan_diag_emit_code(g->diag, DIAG_ERROR, call_expr->loc, 3097,
                       "static_assert condition is not a compile-time constant: %s",
                       ctx.err_msg);
    ctfe_ctx_cleanup(&ctx);
    return;
  }
  bool ok = ctfe_as_bool(v);
  ctfe_ctx_cleanup(&ctx);
  if (!ok) {
    if (call_expr->call.args.count >= 2 &&
        call_expr->call.args.items[1]->kind == AST_STRING_LITERAL) {
      zan_istr_t msg = call_expr->call.args.items[1]->str_val;
      zan_diag_emit_code(g->diag, DIAG_ERROR, call_expr->loc, 3097,
                         "static_assert failed: %.*s",
                         (int)msg.len, msg.str ? msg.str : "");
    } else {
      zan_diag_emit_code(g->diag, DIAG_ERROR, call_expr->loc, 3097,
                         "static_assert failed");
    }
  }
}
