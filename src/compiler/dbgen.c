/* dbgen.c -- compile-time typed ORM queries for System.Data.
 *
 * `db.Query<T>()` starts a typed query over table T (class name == table
 * name). The fluent chain is lowered at compile time:
 *
 *   List<UserPoint> l = db.Query<UserPoint>()
 *       .Where(p => p.points > min && p.userId.In(ids))
 *       .Include(p => p.user)
 *       .OrderByDescending(p => p.points)
 *       .Take(10)
 *       .ToList();
 *
 * For every entity class a query class `__DbQ_T` is generated (Zan source,
 * parsed and merged into the unit, like jsongen): it holds the connection
 * (through the `IDbExecutor` interface), WHERE fragments, bound parameters,
 * ORDER BY, LIMIT/OFFSET and include flags, builds the final SQL, executes
 * it and maps rows back into `List<T>` by fixed column position.
 *
 * Lambda conditions are translated into parameterized SQL fragments: column
 * references come from the lambda parameter's fields, every other value is
 * bound as a `?` parameter (never inlined into SQL text). Supported forms:
 * ==, !=, <, <=, >, >= between a column and a value (or two columns), &&,
 * ||, !, bare/negated bool columns, `x == null` (IS NULL), and the methods
 * Like/NotLike/Contains/StartsWith/EndsWith/In/NotIn on a column.
 *
 * `Include(p => p.nav)` LEFT JOINs the navigation entity by convention
 * (`t.<nav>Id = j.<id>`) and maps the joined columns into `o.nav`.
 *
 * Chains must be fluent (rooted at `db.Query<T>()` in the same expression);
 * `In`/`NotIn` list arguments are evaluated twice (count + values), so they
 * must be side-effect-free expressions such as locals.
 */

#include "dbgen.h"
#include "lexer.h"
#include "parser.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- growable source buffer ---- */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} dg_buf_t;

static void dg_putf(dg_buf_t *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) { va_end(ap2); return; }
    if (b->len + (size_t)need + 1 > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 4096;
        while (b->len + (size_t)need + 1 > ncap) ncap *= 2;
        b->buf = (char *)realloc(b->buf, ncap);
        b->cap = ncap;
    }
    vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)need;
}

static bool istr_is(zan_istr_t s, const char *lit) {
    size_t n = strlen(lit);
    return s.len == n && memcmp(s.str, lit, n) == 0;
}

static bool istr_eq(zan_istr_t a, zan_istr_t b) {
    return a.len == b.len && memcmp(a.str, b.str, (size_t)a.len) == 0;
}

static zan_ast_node_t *dg_find_class(zan_ast_node_t *unit, zan_istr_t name) {
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (d->kind != AST_CLASS_DECL) continue;
        if (istr_eq(d->type_decl.name, name)) return d;
    }
    return NULL;
}

static bool dg_find_enum(zan_ast_node_t *unit, zan_istr_t name) {
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (d->kind == AST_ENUM_DECL && istr_eq(d->type_decl.name, name))
            return true;
    }
    return false;
}

/* ---- entity field model ---- */

typedef enum {
    DF_INT,    /* int / long / short / byte / uint / ... */
    DF_DOUBLE, /* double / float / decimal */
    DF_BOOL,
    DF_STRING,
    DF_ENUM,
    DF_NAV,    /* class-typed navigation field */
    DF_SKIP,   /* List<...>, arrays, unsupported */
} dg_fk_t;

typedef struct {
    zan_istr_t name;
    zan_istr_t type_name; /* declared type (enum/nav class name) */
    dg_fk_t kind;
    zan_istr_t col;       /* column name ([Column(Name=)] or the field name) */
    bool is_pk;           /* [Column(IsPrimary = true)], or a field named id */
    bool is_ident;        /* [Column(IsIdentity = true)]; implied by int pk */
    bool not_null;        /* [Column(IsNullable = false)] */
    int str_len;          /* [Column(StringLength = n)], 0 = provider default */
} dg_field_t;

typedef struct {
    dg_field_t items[128];
    int count;
} dg_fields_t;

static bool dg_is_int_name(zan_istr_t n) {
    return istr_is(n, "int") || istr_is(n, "long") || istr_is(n, "short") ||
           istr_is(n, "byte") || istr_is(n, "sbyte") || istr_is(n, "uint") ||
           istr_is(n, "ushort") || istr_is(n, "ulong") || istr_is(n, "char");
}

static bool dg_is_float_name(zan_istr_t n) {
    return istr_is(n, "double") || istr_is(n, "float") || istr_is(n, "decimal");
}

/* ---- [Table] / [Column] attribute readers ---- */

static zan_ast_node_t *dg_attr(const zan_ast_node_t *decl, const char *name) {
    if (!decl) return NULL;
    size_t n = strlen(name);
    for (int i = 0; i < decl->attributes.count; i++) {
        zan_ast_node_t *a = decl->attributes.items[i];
        if (!a || a->kind != AST_ATTRIBUTE || !a->attribute.name) continue;
        if (a->attribute.name->kind != AST_IDENTIFIER) continue;
        zan_istr_t s = a->attribute.name->ident.name;
        if (s.len == (uint32_t)n && memcmp(s.str, name, n) == 0) return a;
    }
    return NULL;
}

/* The value of a named attribute argument (`Name = expr`), or NULL. */
static zan_ast_node_t *dg_attr_arg(zan_ast_node_t *attr, const char *key) {
    if (!attr) return NULL;
    size_t n = strlen(key);
    for (int i = 0; i < attr->attribute.args.count; i++) {
        zan_ast_node_t *a = attr->attribute.args.items[i];
        if (!a || a->kind != AST_ASSIGNMENT) continue;
        zan_ast_node_t *l = a->binary.left;
        if (!l || l->kind != AST_IDENTIFIER) continue;
        if (l->ident.name.len == (uint32_t)n &&
            memcmp(l->ident.name.str, key, n) == 0)
            return a->binary.right;
    }
    return NULL;
}

static zan_istr_t dg_attr_str(zan_ast_node_t *attr, const char *key) {
    zan_ast_node_t *v = dg_attr_arg(attr, key);
    zan_istr_t none = { NULL, 0 };
    if (!v || v->kind != AST_STRING_LITERAL) return none;
    return v->str_val;
}

static zan_istr_t dg_table_of(zan_ast_node_t *unit, zan_istr_t cls) {
    zan_ast_node_t *decl = dg_find_class(unit, cls);
    zan_istr_t n = dg_attr_str(dg_attr(decl, "Table"), "Name");
    return n.len ? n : cls;
}

static bool dg_is_table_entity(zan_ast_node_t *unit, zan_istr_t cls) {
    zan_ast_node_t *decl = dg_find_class(unit, cls);
    return dg_attr(decl, "Table") != NULL;
}


static bool dg_attr_bool(zan_ast_node_t *attr, const char *key, bool dflt) {
    zan_ast_node_t *v = dg_attr_arg(attr, key);
    if (!v || v->kind != AST_BOOL_LITERAL) return dflt;
    return v->bool_val;
}

static int dg_attr_int(zan_ast_node_t *attr, const char *key, int dflt) {
    zan_ast_node_t *v = dg_attr_arg(attr, key);
    if (!v || v->kind != AST_INT_LITERAL) return dflt;
    return (int)v->int_val;
}

static dg_fk_t dg_classify(zan_ast_node_t *unit, zan_ast_node_t *tref) {
    if (!tref || tref->kind != AST_TYPE_REF) return DF_SKIP;
    if (tref->type_ref.is_array || tref->type_ref.type_args.count > 0)
        return DF_SKIP;
    zan_istr_t n = tref->type_ref.name;
    if (dg_is_int_name(n)) return DF_INT;
    if (dg_is_float_name(n)) return DF_DOUBLE;
    if (istr_is(n, "bool")) return DF_BOOL;
    if (istr_is(n, "string")) return DF_STRING;
    if (dg_find_enum(unit, n)) return DF_ENUM;
    if (dg_find_class(unit, n)) return DF_NAV;
    return DF_SKIP;
}

static void dg_collect_fields(zan_ast_node_t *unit, zan_ast_node_t *cls,
                              dg_fields_t *out, int depth) {
    if (!cls || depth > 8) return;
    for (int bx = 0; bx < cls->type_decl.bases.count; bx++) {
        zan_ast_node_t *bref = cls->type_decl.bases.items[bx];
        if (bref->kind != AST_TYPE_REF) continue;
        dg_collect_fields(unit, dg_find_class(unit, bref->type_ref.name), out,
                          depth + 1);
    }
    for (int i = 0; i < cls->type_decl.members.count; i++) {
        zan_ast_node_t *m = cls->type_decl.members.items[i];
        if (m->kind != AST_FIELD_DECL) continue;
        if (m->field_decl.modifiers & MOD_STATIC) continue;
        if (out->count >= 128) return;
        dg_field_t *f = &out->items[out->count];
        memset(f, 0, sizeof(*f));
        f->name = m->field_decl.name;
        f->kind = dg_classify(unit, m->field_decl.type);
        zan_ast_node_t *ca = dg_attr(m, "Column");
        zan_istr_t cn = dg_attr_str(ca, "Name");
        f->col = cn.len ? cn : f->name;
        f->is_pk = dg_attr_bool(ca, "IsPrimary", istr_is(f->name, "id"));
        f->is_ident = dg_attr_bool(ca, "IsIdentity",
                                   f->is_pk && f->kind == DF_INT);
        f->not_null = !dg_attr_bool(ca, "IsNullable", !f->is_pk);
        f->str_len = dg_attr_int(ca, "StringLength", 0);
        f->type_name = (m->field_decl.type &&
                        m->field_decl.type->kind == AST_TYPE_REF)
                           ? m->field_decl.type->type_ref.name
                           : (zan_istr_t){ "?", 1 };
        if (f->kind != DF_SKIP) out->count++;
    }
}

static const dg_field_t *dg_field_find(const dg_fields_t *fs, zan_istr_t name) {
    for (int i = 0; i < fs->count; i++)
        if (istr_eq(fs->items[i].name, name)) return &fs->items[i];
    return NULL;
}

/* ---- needed-entity worklist ---- */

typedef struct {
    zan_istr_t names[64];
    int count;
} dg_need_t;

static int dg_need_add(dg_need_t *need, zan_istr_t name) {
    for (int i = 0; i < need->count; i++)
        if (istr_eq(need->names[i], name)) return i;
    if (need->count >= 64) return -1;
    need->names[need->count] = name;
    return need->count++;
}

/* ---- Expr<T> target-typed lambda slots ----
 *
 * A method whose declared parameter type is `Expr<T>` receives lambdas as
 * inspectable expression trees: dbgen rewrites `M(x => x.f > v)` into
 * `M(Expr<T>.From(DbExpr.Lambda("x", DbExpr.Binary(">", ...))))` so the
 * ORM / Linq / rules engine can walk the tree in pure Zan at runtime.
 * The slots are collected by scanning method declarations (parameter type
 * AST shape `Expr<...>`), which needs no binder — dbgen runs pre-binding. */

typedef struct {
    zan_istr_t cls;       /* owning class name ("" = any receiver) */
    zan_istr_t name;      /* method name */
    int param;            /* parameter index (0-based) */
    zan_istr_t entity;    /* the T of Expr<T> */
} dg_expr_slot_t;

typedef struct {
    zan_ast_node_t *unit;
    zan_arena_t *arena;
    zan_diag_t *diag;
    dg_need_t need;
    dg_expr_slot_t eslots[128];
    int eslot_count;
    bool any;
    bool sync_all;
} dg_ctx_t;

/* ---- AST node builders ---- */

static zan_ast_node_t *nb(dg_ctx_t *c, zan_ast_kind_t k, zan_loc_t loc) {
    return zan_ast_new(c->arena, k, loc);
}

static zan_istr_t dg_istr(dg_ctx_t *c, const char *s, size_t n) {
    char *p = (char *)zan_arena_alloc(c->arena, n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return (zan_istr_t){ p, (uint32_t)n };
}

static zan_ast_node_t *nb_strlit(dg_ctx_t *c, zan_loc_t loc, const char *s,
                                 size_t n) {
    zan_ast_node_t *e = nb(c, AST_STRING_LITERAL, loc);
    e->str_val = dg_istr(c, s, n);
    return e;
}

static zan_ast_node_t *nb_ident(dg_ctx_t *c, zan_loc_t loc, const char *name) {
    zan_ast_node_t *e = nb(c, AST_IDENTIFIER, loc);
    e->ident.name = dg_istr(c, name, strlen(name));
    return e;
}

static zan_ast_node_t *nb_null(dg_ctx_t *c, zan_loc_t loc) {
    return nb(c, AST_NULL_LITERAL, loc);
}

static zan_ast_node_t *nb_member(dg_ctx_t *c, zan_loc_t loc,
                                 zan_ast_node_t *obj, zan_istr_t name) {
    zan_ast_node_t *e = nb(c, AST_MEMBER_ACCESS, loc);
    e->member.object = obj;
    e->member.name = name;
    e->member.null_cond = 0;
    return e;
}

static zan_ast_node_t *nb_call1(dg_ctx_t *c, zan_loc_t loc,
                                zan_ast_node_t *callee, zan_ast_node_t *arg) {
    zan_ast_node_t *e = nb(c, AST_CALL, loc);
    e->call.callee = callee;
    zan_ast_list_init(&e->call.args);
    zan_ast_list_init(&e->call.type_args);
    if (arg) zan_ast_list_push(&e->call.args, arg, c->arena);
    return e;
}

static zan_ast_node_t *nb_concat(dg_ctx_t *c, zan_loc_t loc,
                                 zan_ast_node_t *l, zan_ast_node_t *r) {
    zan_ast_node_t *e = nb(c, AST_BINARY, loc);
    e->binary.op = TK_PLUS;
    e->binary.left = l;
    e->binary.right = r;
    return e;
}

static zan_ast_node_t *nb_cast_int(dg_ctx_t *c, zan_loc_t loc,
                                   zan_ast_node_t *expr) {
    zan_ast_node_t *t = nb(c, AST_TYPE_REF, loc);
    t->type_ref.name = dg_istr(c, "int", 3);
    zan_ast_list_init(&t->type_ref.type_args);
    t->type_ref.is_nullable = false;
    t->type_ref.is_array = false;
    zan_ast_node_t *e = nb(c, AST_CAST_EXPR, loc);
    e->cast.type = t;
    e->cast.expr = expr;
    return e;
}

static zan_ast_node_t *nb_cast_double(dg_ctx_t *c, zan_loc_t loc,
                                      zan_ast_node_t *expr) {
    zan_ast_node_t *t = nb(c, AST_TYPE_REF, loc);
    t->type_ref.name = dg_istr(c, "double", 6);
    zan_ast_list_init(&t->type_ref.type_args);
    t->type_ref.is_nullable = false;
    t->type_ref.is_array = false;
    zan_ast_node_t *e = nb(c, AST_CAST_EXPR, loc);
    e->cast.type = t;
    e->cast.expr = expr;
    return e;
}

/* ---- WHERE fragment builder ---- */

/* The fragment is accumulated as literal text plus runtime expression
 * segments (for dynamic IN mark counts), later folded into a string-concat
 * expression. Bind calls are recorded in fragment order and chained after
 * `.W(frag)` so parameters line up with their `?` marks. */

typedef struct {
    dg_buf_t lit;             /* pending literal run */
    zan_ast_node_t *expr;     /* folded expression so far (NULL = none) */
    zan_loc_t loc;
} dg_frag_t;

typedef enum { BK_STR, BK_INT, BK_DBL, BK_INI, BK_INS, BK_IND } dg_bk_t;

typedef struct {
    dg_bk_t kind;
    zan_ast_node_t *expr;
} dg_bind_t;

typedef struct {
    dg_ctx_t *c;
    zan_istr_t pname;         /* lambda parameter name */
    const dg_fields_t *fields;
    dg_frag_t frag;
    dg_bind_t binds[64];
    int bind_count;
    bool ok;
} dg_where_t;

static void wf_text(dg_where_t *w, const char *s) {
    dg_putf(&w->frag.lit, "%s", s);
}

static void wf_textn(dg_where_t *w, const char *s, size_t n) {
    dg_putf(&w->frag.lit, "%.*s", (int)n, s);
}

static void wf_flush(dg_where_t *w) {
    if (w->frag.lit.len == 0) return;
    zan_ast_node_t *lit =
        nb_strlit(w->c, w->frag.loc, w->frag.lit.buf, w->frag.lit.len);
    w->frag.expr = w->frag.expr
                       ? nb_concat(w->c, w->frag.loc, w->frag.expr, lit)
                       : lit;
    w->frag.lit.len = 0;
}

static void wf_expr(dg_where_t *w, zan_ast_node_t *e) {
    wf_flush(w);
    w->frag.expr = w->frag.expr ? nb_concat(w->c, w->frag.loc, w->frag.expr, e)
                                : e;
}

static void wf_bind(dg_where_t *w, dg_bk_t kind, zan_ast_node_t *expr) {
    if (w->bind_count >= 64) { w->ok = false; return; }
    w->binds[w->bind_count].kind = kind;
    w->binds[w->bind_count].expr = expr;
    w->bind_count++;
}

static void wf_err(dg_where_t *w, zan_loc_t loc, const char *msg) {
    zan_diag_emit(w->c->diag, DIAG_ERROR, loc,
                  "db.Query<T>().Where: %s", msg);
    w->ok = false;
}

static bool dg_mentions_param(zan_ast_node_t *e, zan_istr_t pname) {
    if (!e) return false;
    switch (e->kind) {
    case AST_IDENTIFIER: return istr_eq(e->ident.name, pname);
    case AST_MEMBER_ACCESS: return dg_mentions_param(e->member.object, pname);
    case AST_BINARY:
        return dg_mentions_param(e->binary.left, pname) ||
               dg_mentions_param(e->binary.right, pname);
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        return dg_mentions_param(e->unary.operand, pname);
    case AST_CALL: {
        if (dg_mentions_param(e->call.callee, pname)) return true;
        for (int i = 0; i < e->call.args.count; i++)
            if (dg_mentions_param(e->call.args.items[i], pname)) return true;
        return false;
    }
    case AST_INDEX:
        return dg_mentions_param(e->index.object, pname) ||
               dg_mentions_param(e->index.index, pname);
    case AST_CAST_EXPR: return dg_mentions_param(e->cast.expr, pname);
    case AST_CONDITIONAL:
        return dg_mentions_param(e->conditional.cond, pname) ||
               dg_mentions_param(e->conditional.then_expr, pname) ||
               dg_mentions_param(e->conditional.else_expr, pname);
    default: return false;
    }
}

/* `p.field` -> field entry, else NULL. */
static const dg_field_t *dg_as_column(dg_where_t *w, zan_ast_node_t *e) {
    if (!e || e->kind != AST_MEMBER_ACCESS) return NULL;
    zan_ast_node_t *obj = e->member.object;
    if (!obj || obj->kind != AST_IDENTIFIER ||
        !istr_eq(obj->ident.name, w->pname))
        return NULL;
    return dg_field_find(w->fields, e->member.name);
}

static void wf_col(dg_where_t *w, const dg_field_t *f) {
    wf_text(w, "t.");
    wf_textn(w, f->name.str, f->name.len);
}

/* Compile-time literal/field type matching. The dbgen pass runs before type
 * checking, so only literal shapes can be validated here; anything else
 * (variables, calls) is left to the generated code's parameter types. The
 * point is to catch the common `p.name == 123` / `p.num == "abc"` mistakes
 * with a clear diagnostic instead of a silent wrong result at runtime.
 * Returns NULL when the literal shape matches the column, else a message. */
static const char *dg_literal_mismatch(const dg_field_t *f, zan_ast_node_t *e) {
    zan_ast_node_t *lit = e;
    if (e->kind == AST_UNARY &&
        (e->unary.op == TK_MINUS || e->unary.op == TK_PLUS))
        lit = e->unary.operand;
    if (lit->kind == AST_INT_LITERAL || lit->kind == AST_CHAR_LITERAL) {
        if (f->kind == DF_INT || f->kind == DF_ENUM || f->kind == DF_DOUBLE)
            return NULL;
        return f->kind == DF_STRING
                   ? "type mismatch: string column compared to an int value"
                   : "type mismatch: numeric value for a non-numeric column";
    }
    if (lit->kind == AST_FLOAT_LITERAL) {
        if (f->kind == DF_DOUBLE) return NULL;
        return "type mismatch: double value compared to a non-double column";
    }
    if (lit->kind == AST_STRING_LITERAL) {
        if (f->kind == DF_STRING) return NULL;
        return "type mismatch: string value compared to a numeric column";
    }
    if (lit->kind == AST_BOOL_LITERAL) {
        if (f->kind == DF_BOOL) return NULL;
        return "type mismatch: bool value compared to a non-bool column";
    }
    return NULL; /* unknown shapes (identifiers, calls): trust the generated code */
}

static bool dg_check_value_type(dg_where_t *w, const dg_field_t *f,
                                zan_ast_node_t *e) {
    const char *msg = dg_literal_mismatch(f, e);
    if (msg) {
        wf_err(w, e->loc, msg);
        return false;
    }
    return true;
}

/* Emits `?` and records the bind for a scalar comparison value. */
static void wf_value(dg_where_t *w, const dg_field_t *f, zan_ast_node_t *e) {
    if (!dg_check_value_type(w, f, e)) return;
    wf_text(w, "?");
    switch (f->kind) {
    case DF_INT:
        wf_bind(w, BK_INT, e);
        break;
    case DF_ENUM:
        wf_bind(w, BK_INT, nb_cast_int(w->c, e->loc, e));
        break;
    case DF_DOUBLE:
        /* int expressions are promoted to double: a bare `Pd(intExpr)` would
         * fail LLVM verification (call parameter type mismatch), so the cast
         * is always applied except for a plain double literal. */
        if (e->kind == AST_FLOAT_LITERAL)
            wf_bind(w, BK_DBL, e);
        else
            wf_bind(w, BK_DBL, nb_cast_double(w->c, e->loc, e));
        break;
    case DF_STRING:
        wf_bind(w, BK_STR, e);
        break;
    default:
        wf_err(w, e->loc, "unsupported column type in comparison");
        break;
    }
}

static const char *dg_sql_op(zan_token_kind_t op, bool flip) {
    switch (op) {
    case TK_EQ_EQ: return " = ";
    case TK_BANG_EQ: return " <> ";
    case TK_LESS: return flip ? " > " : " < ";
    case TK_GREATER: return flip ? " < " : " > ";
    case TK_LESS_EQ: return flip ? " >= " : " <= ";
    case TK_GREATER_EQ: return flip ? " <= " : " >= ";
    default: return NULL;
    }
}

/* FreeSQL-style aggregate in a condition: `p.Count()`, `p.Sum(x => x.col)`,
 * `p.Avg/Max/Min(x => x.col)`. Emits the SQL text (COUNT(*) / SUM(t.col))
 * and reports the comparison value kind (DF_INT for COUNT, the column kind
 * otherwise; AVG is always double). Returns the aggregate name, or NULL when
 * `e` is not an aggregate call on the lambda parameter. On a malformed
 * aggregate it emits a diagnostic and returns non-NULL with w->ok cleared. */
static const char *dg_agg_call(dg_where_t *w, zan_ast_node_t *e,
                               int *out_kind) {
    if (!e || e->kind != AST_CALL) return NULL;
    zan_ast_node_t *callee = e->call.callee;
    if (!callee || callee->kind != AST_MEMBER_ACCESS) return NULL;
    zan_ast_node_t *recv = callee->member.object;
    if (!recv || recv->kind != AST_IDENTIFIER ||
        !istr_eq(recv->ident.name, w->pname))
        return NULL;
    zan_istr_t mn = callee->member.name;
    const char *fn = istr_is(mn, "Count") ? "COUNT"
                     : istr_is(mn, "Sum")   ? "SUM"
                     : istr_is(mn, "Avg")   ? "AVG"
                     : istr_is(mn, "Max")   ? "MAX"
                     : istr_is(mn, "Min")   ? "MIN" : NULL;
    if (!fn) return NULL;
    if (istr_is(mn, "Count")) {
        if (e->call.args.count != 0) {
            wf_err(w, e->loc, "Count() takes no arguments");
            return fn;
        }
        wf_text(w, "COUNT(*)");
        *out_kind = DF_INT;
        return fn;
    }
    if (e->call.args.count != 1) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s requires x => x.<column>", fn);
        wf_err(w, e->loc, msg);
        return fn;
    }
    zan_ast_node_t *lam = e->call.args.items[0];
    if (lam->kind != AST_LAMBDA || lam->lambda.params.count != 1) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s expects x => x.<column>", fn);
        wf_err(w, e->loc, msg);
        return fn;
    }
    zan_ast_node_t *body = lam->lambda.body;
    if (body->kind != AST_MEMBER_ACCESS || !body->member.object ||
        body->member.object->kind != AST_IDENTIFIER ||
        !istr_eq(body->member.object->ident.name,
                 lam->lambda.params.items[0]->param.name)) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s expects x => x.<column>", fn);
        wf_err(w, e->loc, msg);
        return fn;
    }
    const dg_field_t *col = dg_field_find(w->fields, body->member.name);
    if (!col || col->kind == DF_NAV) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s expects a scalar column", fn);
        wf_err(w, e->loc, msg);
        return fn;
    }
    char buf[160];
    snprintf(buf, sizeof(buf), "%s(t.%.*s)", fn, (int)col->col.len,
             col->col.str);
    wf_text(w, buf);
    *out_kind = istr_is(mn, "Avg") ? DF_DOUBLE : col->kind;
    return fn;
}

static void dg_where_cond(dg_where_t *w, zan_ast_node_t *e);

/* p.f.Method(arg...) conditions. Null tests (0 args), range tests
 * (Between/NotBetween, 2 args) and comparison methods (Eq/Ne/Gt/Ge/Lt/Le,
 * 1 arg) join the string-matching methods Like/NotLike/Contains/
 * StartsWith/EndsWith/In/NotIn. */
static bool dg_where_method(dg_where_t *w, zan_ast_node_t *e) {
    if (e->kind != AST_CALL) return false;
    zan_ast_node_t *callee = e->call.callee;
    if (!callee || callee->kind != AST_MEMBER_ACCESS) return false;
    const dg_field_t *col = dg_as_column(w, callee->member.object);
    zan_istr_t mn = callee->member.name;
    dg_ctx_t *c = w->c;

    /* FreeSQL-style `list.Contains(p.col)` -> `p.col IN (?, ...)`. The
     * receiver is a List (not a column) and the sole argument is a column;
     * `!list.Contains(p.col)` is handled by the NOT wrapper in
     * dg_where_cond. This mirrors `new[]{1,2,3}.Contains(a.Id)` from the
     * FreeSQL docs. */
    if (!col && istr_is(mn, "Contains") && e->call.args.count == 1) {
        const dg_field_t *inner = dg_as_column(w, e->call.args.items[0]);
        if (inner && inner->kind != DF_BOOL) {
            zan_ast_node_t *list = callee->member.object;
            if (dg_mentions_param(list, w->pname)) {
                wf_err(w, list->loc,
                       "the list receiver must not reference the lambda "
                       "parameter");
                return true;
            }
            wf_col(w, inner);
            wf_text(w, " IN (");
            zan_ast_node_t *cnt =
                nb_member(c, list->loc, list, dg_istr(c, "Count", 5));
            zan_ast_node_t *m = nb_call1(
                c, list->loc,
                nb_member(c, list->loc, nb_ident(c, list->loc, "__DbBind"),
                          dg_istr(c, "M", 1)),
                cnt);
            wf_expr(w, m);
            wf_text(w, ")");
            switch (inner->kind) {
            case DF_INT:
            case DF_ENUM: wf_bind(w, BK_INI, list); break;
            case DF_STRING: wf_bind(w, BK_INS, list); break;
            case DF_DOUBLE: wf_bind(w, BK_IND, list); break;
            default:
                wf_err(w, e->loc, "Contains requires an int/string/double "
                                  "column");
                break;
            }
            return true;
        }
        return false;
    }

    if (!col) return false;

    /* p.x.IsNull() / p.x.IsNotNull() */
    if (istr_is(mn, "IsNull") || istr_is(mn, "IsNotNull")) {
        if (e->call.args.count != 0) {
            wf_err(w, e->loc, "IsNull/IsNotNull take no arguments");
            return true;
        }
        wf_col(w, col);
        wf_text(w, istr_is(mn, "IsNull") ? " IS NULL" : " IS NOT NULL");
        return true;
    }

    /* p.age.Between(lo, hi) / p.age.NotBetween(lo, hi) */
    if (istr_is(mn, "Between") || istr_is(mn, "NotBetween")) {
        if (e->call.args.count != 2) {
            wf_err(w, e->loc,
                   "Between/NotBetween take exactly two arguments");
            return true;
        }
        if (col->kind != DF_INT && col->kind != DF_ENUM &&
            col->kind != DF_DOUBLE) {
            wf_err(w, e->loc, "Between requires an int/double/enum column");
            return true;
        }
        for (int i = 0; i < 2; i++) {
            if (dg_mentions_param(e->call.args.items[i], w->pname)) {
                wf_err(w, e->call.args.items[i]->loc,
                       "argument must not reference the lambda parameter");
                return true;
            }
        }
        wf_col(w, col);
        wf_text(w, istr_is(mn, "Between") ? " BETWEEN "
                                          : " NOT BETWEEN ");
        wf_value(w, col, e->call.args.items[0]);
        wf_text(w, " AND ");
        wf_value(w, col, e->call.args.items[1]);
        return true;
    }

    /* p.age.Eq(x) / Ne / Gt / Ge / Lt / Le — same shape as the binary
     * comparison operators, spelled as a method like FreeSQL. */
    const char *cmp = NULL;
    if (istr_is(mn, "Eq")) cmp = " = ";
    else if (istr_is(mn, "Ne")) cmp = " <> ";
    else if (istr_is(mn, "Gt")) cmp = " > ";
    else if (istr_is(mn, "Ge")) cmp = " >= ";
    else if (istr_is(mn, "Lt")) cmp = " < ";
    else if (istr_is(mn, "Le")) cmp = " <= ";
    if (cmp) {
        if (e->call.args.count != 1) {
            wf_err(w, e->loc, "comparison method takes exactly one argument");
            return true;
        }
        if (col->kind == DF_BOOL) {
            wf_err(w, e->loc, "bool columns compare with == / !=");
            return true;
        }
        zan_ast_node_t *arg = e->call.args.items[0];
        if (dg_mentions_param(arg, w->pname)) {
            wf_err(w, arg->loc,
                   "argument must not reference the lambda parameter");
            return true;
        }
        wf_col(w, col);
        wf_text(w, cmp);
        wf_value(w, col, arg);
        return true;
    }

    /* Remaining methods take exactly one argument. */
    if (e->call.args.count != 1) {
        wf_err(w, e->loc, "column method takes exactly one argument");
        return true;
    }
    zan_ast_node_t *arg = e->call.args.items[0];
    if (dg_mentions_param(arg, w->pname)) {
        wf_err(w, arg->loc,
               "argument must not reference the lambda parameter");
        return true;
    }
    if (istr_is(mn, "Like") || istr_is(mn, "NotLike")) {
        if (col->kind != DF_STRING) {
            wf_err(w, e->loc, "Like requires a string column");
            return true;
        }
        wf_col(w, col);
        wf_text(w, istr_is(mn, "Like") ? " LIKE ?" : " NOT LIKE ?");
        wf_bind(w, BK_STR, arg);
        return true;
    }
    if (istr_is(mn, "Contains") || istr_is(mn, "StartsWith") ||
        istr_is(mn, "EndsWith")) {
        if (col->kind != DF_STRING) {
            wf_err(w, e->loc, "string matching requires a string column");
            return true;
        }
        wf_col(w, col);
        wf_text(w, " LIKE ?");
        zan_ast_node_t *pat = arg;
        if (!istr_is(mn, "StartsWith"))
            pat = nb_concat(c, arg->loc, nb_strlit(c, arg->loc, "%", 1), pat);
        if (!istr_is(mn, "EndsWith"))
            pat = nb_concat(c, arg->loc, pat, nb_strlit(c, arg->loc, "%", 1));
        wf_bind(w, BK_STR, pat);
        return true;
    }
    if (istr_is(mn, "In") || istr_is(mn, "NotIn")) {
        wf_col(w, col);
        wf_text(w, istr_is(mn, "In") ? " IN (" : " NOT IN (");
        /* __DbBind.M(arg.Count) expands to the right number of marks. NOTE:
         * `arg` is referenced twice (count + values); it must be side-effect
         * free. */
        zan_ast_node_t *cnt =
            nb_member(c, arg->loc, arg, dg_istr(c, "Count", 5));
        zan_ast_node_t *m = nb_call1(
            c, arg->loc,
            nb_member(c, arg->loc, nb_ident(c, arg->loc, "__DbBind"),
                      dg_istr(c, "M", 1)),
            cnt);
        wf_expr(w, m);
        wf_text(w, ")");
        switch (col->kind) {
        case DF_INT:
        case DF_ENUM: wf_bind(w, BK_INI, arg); break;
        case DF_STRING: wf_bind(w, BK_INS, arg); break;
        case DF_DOUBLE: wf_bind(w, BK_IND, arg); break;
        default:
            wf_err(w, e->loc, "In requires an int/string/double column");
            break;
        }
        return true;
    }
    wf_err(w, e->loc, "unsupported column method (expected Eq/Ne/Gt/Ge/Lt/Le/"
                      "Between/NotBetween/IsNull/IsNotNull/Like/NotLike/"
                      "Contains/StartsWith/EndsWith/In/NotIn)");
    return true;
}

static void dg_where_cond(dg_where_t *w, zan_ast_node_t *e) {
    if (!w->ok || !e) return;
    if (e->kind == AST_BINARY &&
        (e->binary.op == TK_AMP_AMP || e->binary.op == TK_PIPE_PIPE)) {
        wf_text(w, "(");
        dg_where_cond(w, e->binary.left);
        wf_text(w, e->binary.op == TK_AMP_AMP ? " AND " : " OR ");
        dg_where_cond(w, e->binary.right);
        wf_text(w, ")");
        return;
    }
    if (e->kind == AST_UNARY && e->unary.op == TK_BANG) {
        const dg_field_t *col = dg_as_column(w, e->unary.operand);
        if (col && col->kind == DF_BOOL) {
            wf_col(w, col);
            wf_text(w, " = 0");
            return;
        }
        wf_text(w, "NOT (");
        dg_where_cond(w, e->unary.operand);
        wf_text(w, ")");
        return;
    }
    if (e->kind == AST_MEMBER_ACCESS) {
        const dg_field_t *col = dg_as_column(w, e);
        if (col && col->kind == DF_BOOL) {
            wf_col(w, col);
            wf_text(w, " = 1");
            return;
        }
        wf_err(w, e->loc, "expected a boolean condition");
        return;
    }
    if (dg_where_method(w, e)) return;
    if (e->kind == AST_BINARY) {
        const char *op = dg_sql_op(e->binary.op, false);
        if (!op) {
            wf_err(w, e->loc, "unsupported operator");
            return;
        }
        zan_ast_node_t *l = e->binary.left;
        zan_ast_node_t *r = e->binary.right;
        const dg_field_t *lc = dg_as_column(w, l);
        const dg_field_t *rc = dg_as_column(w, r);
        if (lc && rc) {
            wf_col(w, lc);
            wf_text(w, op);
            wf_col(w, rc);
            return;
        }
        bool flip = false;
        const dg_field_t *col = lc;
        zan_ast_node_t *val = r;
        if (!col && rc) {
            col = rc;
            val = l;
            flip = true;
            op = dg_sql_op(e->binary.op, true);
        }
        if (!col && !rc) {
            /* FreeSQL-style aggregate comparison: `p.Count() > n`,
             * `p.Sum(x => x.amount) >= n`, ... The aggregate emits its own
             * SQL text; the scalar side binds like a normal column value. */
            int ak = 0;
            const char *af = dg_agg_call(w, l, &ak);
            if (af) {
                if (!w->ok) return;
                if (dg_mentions_param(r, w->pname)) {
                    wf_err(w, r->loc,
                           "value side must not reference the lambda "
                           "parameter");
                    return;
                }
                dg_field_t fake;
                memset(&fake, 0, sizeof(fake));
                fake.kind = ak;
                wf_text(w, op);
                wf_value(w, &fake, r);
                return;
            }
            af = dg_agg_call(w, r, &ak);
            if (af) {
                if (!w->ok) return;
                if (dg_mentions_param(l, w->pname)) {
                    wf_err(w, l->loc,
                           "value side must not reference the lambda "
                           "parameter");
                    return;
                }
                dg_field_t fake;
                memset(&fake, 0, sizeof(fake));
                fake.kind = ak;
                wf_value(w, &fake, l);
                wf_text(w, op);
                return;
            }
        }
        if (!col) {
            wf_err(w, e->loc,
                   "one side of a comparison must be a lambda-parameter "
                   "field (e.g. p.age > x)");
            return;
        }
        if (dg_mentions_param(val, w->pname)) {
            wf_err(w, val->loc,
                   "value side must not reference the lambda parameter");
            return;
        }
        /* x == null / x != null */
        if (val->kind == AST_NULL_LITERAL) {
            if (e->binary.op == TK_EQ_EQ) {
                wf_col(w, col);
                wf_text(w, " IS NULL");
            } else if (e->binary.op == TK_BANG_EQ) {
                wf_col(w, col);
                wf_text(w, " IS NOT NULL");
            } else {
                wf_err(w, e->loc, "null only supports == / !=");
            }
            return;
        }
        if (col->kind == DF_BOOL) {
            if (val->kind != AST_BOOL_LITERAL ||
                (e->binary.op != TK_EQ_EQ && e->binary.op != TK_BANG_EQ)) {
                wf_err(w, e->loc,
                       "bool columns only compare to true/false literals");
                return;
            }
            bool truth = val->bool_val == (e->binary.op == TK_EQ_EQ);
            wf_col(w, col);
            wf_text(w, truth ? " = 1" : " = 0");
            return;
        }
        wf_col(w, col);
        wf_text(w, op);
        wf_value(w, col, val);
        return;
    }
    wf_err(w, e->loc, "unsupported condition");
}

/* ---- chain recognition & rewriting ---- */

/* Which statement a fluent chain builds; the root method name encodes it
 * (`__DbBind.Q_T` / `I_T` / `U_T` / `D_T`). */
typedef enum { CK_NONE, CK_SELECT, CK_INSERT, CK_UPDATE, CK_DELETE } dg_ck_t;

static dg_ck_t dg_prefix_kind(zan_istr_t m) {
    if (m.len < 3 || m.str[1] != '_') return CK_NONE;
    switch (m.str[0]) {
    case 'Q': return CK_SELECT;
    case 'I': return CK_INSERT;
    case 'U': return CK_UPDATE;
    case 'D': return CK_DELETE;
    default: return CK_NONE;
    }
}

/* Walks a fluent chain receiver down to a rewritten `__DbBind.X_<T>(db)`
 * root and returns the entity name (len 0 when not a db chain). */
static zan_istr_t dg_chain_entity2(zan_ast_node_t *recv, dg_ck_t *kind) {
    zan_istr_t none = { NULL, 0 };
    if (kind) *kind = CK_NONE;
    while (recv && recv->kind == AST_CALL) {
        zan_ast_node_t *callee = recv->call.callee;
        if (!callee || callee->kind != AST_MEMBER_ACCESS) return none;
        zan_ast_node_t *obj = callee->member.object;
        if (obj && obj->kind == AST_IDENTIFIER &&
            istr_is(obj->ident.name, "__DbBind")) {
            dg_ck_t k = dg_prefix_kind(callee->member.name);
            if (k == CK_NONE) return none;
            if (kind) *kind = k;
            zan_istr_t r = { callee->member.name.str + 2,
                             callee->member.name.len - 2 };
            return r;
        }
        recv = obj;
    }
    return none;
}

static zan_istr_t dg_chain_entity(zan_ast_node_t *recv) {
    return dg_chain_entity2(recv, NULL);
}

/* `<recv>.Query<T>()` / `Select<T>()` / `Insert<T>(..)` / `Update<T>()` /
 * `Delete<T>()` / `SyncStructure<T>()` with a known entity class T. The
 * out params carry the entity and which statement the chain builds;
 * `SyncStructure` is reported through *sync (it is not a chain). */
static bool dg_is_root(dg_ctx_t *c, zan_ast_node_t *call, zan_istr_t *cls_out,
                       dg_ck_t *kind_out, bool *sync_out) {
    if (call->kind != AST_CALL) return false;
    zan_ast_node_t *callee = call->call.callee;
    if (!callee || callee->kind != AST_MEMBER_ACCESS) return false;
    zan_istr_t m = callee->member.name;
    dg_ck_t k = CK_NONE;
    bool sync = false;
    int maxargs = 0;
    if (istr_is(m, "Query") || istr_is(m, "Select")) k = CK_SELECT;
    else if (istr_is(m, "Insert")) { k = CK_INSERT; maxargs = 1; }
    else if (istr_is(m, "Update")) k = CK_UPDATE;
    else if (istr_is(m, "Delete")) k = CK_DELETE;
    else if (istr_is(m, "SyncStructure")) sync = true;
    else return false;
    if (call->call.type_args.count != 1 || call->call.args.count > maxargs)
        return false;
    zan_ast_node_t *targ = call->call.type_args.items[0];
    if (!targ || targ->kind != AST_TYPE_REF || targ->type_ref.is_array ||
        targ->type_ref.type_args.count > 0)
        return false;
    if (!dg_find_class(c->unit, targ->type_ref.name)) return false;
    *cls_out = targ->type_ref.name;
    *kind_out = k;
    *sync_out = sync;
    return true;
}

/* `db.Xxx<T>(args)` -> `__DbBind.<P>_T(db, args)`. */
static void dg_rewrite_root(dg_ctx_t *c, zan_ast_node_t *call, zan_istr_t cls,
                            char prefix) {
    dg_need_add(&c->need, cls);
    c->any = true;
    zan_ast_node_t *callee = call->call.callee;
    zan_ast_node_t *recv = callee->member.object;
    zan_ast_node_t *extra =
        call->call.args.count > 0 ? call->call.args.items[0] : NULL;
    char mname[160];
    snprintf(mname, sizeof(mname), "%c_%.*s", prefix, (int)cls.len, cls.str);
    callee->member.object = nb_ident(c, call->loc, "__DbBind");
    callee->member.name = dg_istr(c, mname, strlen(mname));
    call->call.type_args.count = 0;
    zan_ast_list_init(&call->call.args);
    zan_ast_list_push(&call->call.args, recv, c->arena);
    if (extra) zan_ast_list_push(&call->call.args, extra, c->arena);
}

static const char *dg_bind_method(dg_bk_t k) {
    switch (k) {
    case BK_STR: return "P";
    case BK_INT: return "Pi";
    case BK_DBL: return "Pd";
    case BK_INI: return "InI";
    case BK_INS: return "InS";
    case BK_IND: return "InD";
    }
    return "P";
}

/* Builds the SQL fragment + bind list for a Where/WhereIf lambda. Returns
 * NULL when the lambda could not be translated (a diagnostic was emitted). */
static zan_ast_node_t *dg_where_frag(dg_ctx_t *c, zan_ast_node_t *call,
                                     zan_istr_t cls, zan_ast_node_t *lambda,
                                     dg_where_t *w) {
    if (lambda->kind != AST_LAMBDA || lambda->lambda.params.count != 1) {
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "Where expects a one-parameter lambda");
        return NULL;
    }
    zan_ast_node_t *body = lambda->lambda.body;
    if (body && body->kind == AST_BLOCK) {
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "Where lambda must be an expression (no block body)");
        return NULL;
    }
    static dg_fields_t fields;
    memset(&fields, 0, sizeof(fields));
    dg_collect_fields(c->unit, dg_find_class(c->unit, cls), &fields, 0);

    memset(w, 0, sizeof(*w));
    w->c = c;
    w->pname = lambda->lambda.params.items[0]->param.name;
    w->fields = &fields;
    w->frag.loc = call->loc;
    w->ok = true;
    dg_where_cond(w, body);
    if (!w->ok) {
        free(w->frag.lit.buf);
        return NULL;
    }
    wf_flush(w);
    zan_ast_node_t *frag = w->frag.expr;
    free(w->frag.lit.buf);
    if (!frag) frag = nb_strlit(c, call->loc, "1 = 1", 5);
    return frag;
}

/* Chains `.W(frag).P(v)...` onto the receiver, replacing the call node. */
static void dg_emit_where(dg_ctx_t *c, zan_ast_node_t *call,
                          zan_ast_node_t *frag, dg_where_t *w) {
    zan_ast_node_t *recv = call->call.callee->member.object;
    zan_ast_node_t *node = nb_call1(
        c, call->loc, nb_member(c, call->loc, recv, dg_istr(c, "W", 1)), frag);
    for (int i = 0; i < w->bind_count; i++) {
        const char *mn = dg_bind_method(w->binds[i].kind);
        node = nb_call1(c, call->loc,
                        nb_member(c, call->loc, node,
                                  dg_istr(c, mn, strlen(mn))),
                        w->binds[i].expr);
    }
    *call = *node;
}

/* ---- Expr<T> lowering forward decls (defined later in this file) ---- */
static zan_ast_node_t *dg_expr_tree(dg_ctx_t *c, zan_ast_node_t *e,
                                    zan_istr_t pname);
static zan_ast_node_t *dg_expr_wrap(dg_ctx_t *c, zan_loc_t loc,
                                    zan_istr_t entity, zan_istr_t pname,
                                    zan_ast_node_t *tree);
static bool dg_expr_compatible(dg_ctx_t *c, const dg_fields_t *fs,
                               zan_ast_node_t *e, zan_istr_t pname);

static void dg_rewrite_where(dg_ctx_t *c, zan_ast_node_t *call,
                             zan_istr_t cls) {
    /* Expr path: simple predicates (comparisons / logic / string methods)
     * lower to an Expr<cls> tree and let stdlib ExprSql build the SQL in
     * pure Zan. Complex shapes (IN, aggregates, Between, ...) fall back to
     * the SQL-fragment path below. */
    dg_fields_t fields;
    memset(&fields, 0, sizeof(fields));
    dg_collect_fields(c->unit, dg_find_class(c->unit, cls), &fields, 0);
    zan_ast_node_t *lambda = call->call.args.items[0];
    if (lambda->kind == AST_LAMBDA && lambda->lambda.params.count == 1) {
        zan_istr_t pname = lambda->lambda.params.items[0]->param.name;
        if (dg_expr_compatible(c, &fields, lambda->lambda.body, pname)) {
            zan_ast_node_t *tree =
                dg_expr_tree(c, lambda->lambda.body, pname);
            if (tree) {
                call->call.args.items[0] =
                    dg_expr_wrap(c, call->loc, cls, pname, tree);
                return; /* method name stays `Where`; the generated class
                         * has a Where(Expr<cls>) overload */
            }
        }
    }
    dg_where_t w;
    zan_ast_node_t *frag =
        dg_where_frag(c, call, cls, call->call.args.items[0], &w);
    if (!frag) return;
    dg_emit_where(c, call, frag, &w);
}

/* `WhereIf(cond, p => ...)`: the fragment and its parameters are applied
 * only when `cond` holds, so an optional filter costs one `if` at runtime
 * instead of a hand-built SQL string. */
static void dg_rewrite_where_if(dg_ctx_t *c, zan_ast_node_t *call,
                                zan_istr_t cls) {
    zan_ast_node_t *cond = call->call.args.items[0];
    dg_where_t w;
    zan_ast_node_t *frag =
        dg_where_frag(c, call, cls, call->call.args.items[1], &w);
    if (!frag) return;
    zan_ast_node_t *recv = call->call.callee->member.object;
    zan_ast_node_t *node =
        nb_call1(c, call->loc,
                 nb_member(c, call->loc, recv, dg_istr(c, "CondBegin", 9)),
                 cond);
    node = nb_call1(c, call->loc,
                    nb_member(c, call->loc, node, dg_istr(c, "W", 1)), frag);
    for (int i = 0; i < w.bind_count; i++) {
        const char *mn = dg_bind_method(w.binds[i].kind);
        node = nb_call1(c, call->loc,
                        nb_member(c, call->loc, node,
                                  dg_istr(c, mn, strlen(mn))),
                        w.binds[i].expr);
    }
    node = nb_call1(c, call->loc,
                    nb_member(c, call->loc, node, dg_istr(c, "CondEnd", 7)),
                    NULL);
    *call = *node;
}

/* `Having(p => ...)`: like Where but emitted on the HAVING clause. The
 * fragment and its bound parameters chain `.H(frag).P(v)...` the same way
 * Where uses `.W(frag).P(v)...`. */
static const dg_field_t *dg_sel_column(dg_ctx_t *c, zan_ast_node_t *call,
                                       zan_istr_t cls, dg_fields_t *fields,
                                       const char *what);
static void dg_rewrite_having(dg_ctx_t *c, zan_ast_node_t *call,
                              zan_istr_t cls) {
    dg_where_t w;
    zan_ast_node_t *frag =
        dg_where_frag(c, call, cls, call->call.args.items[0], &w);
    if (!frag) return;
    zan_ast_node_t *recv = call->call.callee->member.object;
    zan_ast_node_t *node = nb_call1(
        c, call->loc, nb_member(c, call->loc, recv, dg_istr(c, "H", 1)), frag);
    for (int i = 0; i < w.bind_count; i++) {
        const char *mn = dg_bind_method(w.binds[i].kind);
        node = nb_call1(c, call->loc,
                        nb_member(c, call->loc, node,
                                  dg_istr(c, mn, strlen(mn))),
                        w.binds[i].expr);
    }
    *call = *node;
}

/* `GroupBy(p => p.col)` -> `.GB("t.col")`. */
static void dg_rewrite_groupby(dg_ctx_t *c, zan_ast_node_t *call,
                               zan_istr_t cls) {
    zan_ast_node_t *lambda = call->call.args.items[0];
    if (lambda->kind != AST_LAMBDA || lambda->lambda.params.count != 1) {
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "GroupBy expects p => p.<scalar field> or p => new { ... }");
        return;
    }
    zan_ast_node_t *body = lambda->lambda.body;
    zan_istr_t pname = lambda->lambda.params.items[0]->param.name;
    dg_fields_t fields;
    memset(&fields, 0, sizeof(fields));
    dg_collect_fields(c->unit, dg_find_class(c->unit, cls), &fields, 0);

    dg_buf_t gb;
    memset(&gb, 0, sizeof(gb));
    bool first = true;
    if (body && body->kind == AST_NEW_EXPR) {
        /* p => new { p.col1, p.col2, ... } -- multi-column grouping. */
        for (int i = 0; i < body->new_expr.args.count; i++) {
            zan_ast_node_t *arg = body->new_expr.args.items[i];
            const dg_field_t *col = NULL;
            if (arg->kind == AST_MEMBER_ACCESS &&
                arg->member.object->kind == AST_IDENTIFIER &&
                istr_eq(arg->member.object->ident.name, pname))
                col = dg_field_find(&fields, arg->member.name);
            if (!col || col->kind == DF_NAV) {
                zan_diag_emit(c->diag, DIAG_ERROR, arg->loc,
                              "GroupBy member must be p => p.<scalar field>");
                return;
            }
            dg_putf(&gb, "%st.%.*s", first ? "" : ", ", (int)col->name.len,
                    col->name.str);
            first = false;
        }
        if (first) {
            zan_diag_emit(c->diag, DIAG_ERROR, body->loc,
                          "GroupBy new { } must name at least one field");
            return;
        }
    } else {
        const dg_field_t *col = NULL;
        if (body && body->kind == AST_MEMBER_ACCESS &&
            body->member.object->kind == AST_IDENTIFIER &&
            istr_eq(body->member.object->ident.name, pname))
            col = dg_field_find(&fields, body->member.name);
        if (!col || col->kind == DF_NAV) {
            zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                          "GroupBy expects p => p.<scalar field>");
            return;
        }
        dg_putf(&gb, "t.%.*s", (int)col->name.len, col->name.str);
    }
    zan_ast_node_t *arg = nb_strlit(c, call->loc, gb.buf, strlen(gb.buf));
    free(gb.buf);
    call->call.callee->member.name = dg_istr(c, "GB", 2);
    zan_ast_list_init(&call->call.args);
    zan_ast_list_push(&call->call.args, arg, c->arena);
}

/* `Distinct()` -> `.DISTINCT()`. */
static void dg_rewrite_distinct(dg_ctx_t *c, zan_ast_node_t *call) {
    call->call.callee->member.name = dg_istr(c, "DISTINCT", 8);
    zan_ast_list_init(&call->call.args);
}

/* The column a `p => p.field` selector names, or NULL (diagnostic emitted). */
static const dg_field_t *dg_sel_column(dg_ctx_t *c, zan_ast_node_t *call,
                                       zan_istr_t cls, dg_fields_t *fields,
                                       const char *what) {
    zan_ast_node_t *lambda = call->call.args.items[0];
    memset(fields, 0, sizeof(*fields));
    dg_collect_fields(c->unit, dg_find_class(c->unit, cls), fields, 0);
    zan_ast_node_t *body =
        (lambda->kind == AST_LAMBDA && lambda->lambda.params.count == 1)
            ? lambda->lambda.body
            : NULL;
    const dg_field_t *col = NULL;
    if (body && body->kind == AST_MEMBER_ACCESS &&
        body->member.object->kind == AST_IDENTIFIER &&
        istr_eq(body->member.object->ident.name,
                lambda->lambda.params.items[0]->param.name))
        col = dg_field_find(fields, body->member.name);
    if (!col || col->kind == DF_NAV)
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "%s expects p => p.<scalar field>", what);
    return col;
}

/* `Set(a => a.col, value)` / `SetIncr(a => a.col, delta)`. */
static void dg_rewrite_set(dg_ctx_t *c, zan_ast_node_t *call, zan_istr_t cls,
                           bool incr) {
    dg_fields_t fields;
    const dg_field_t *col =
        dg_sel_column(c, call, cls, &fields, incr ? "SetIncr" : "Set");
    if (!col) return;
    zan_ast_node_t *val = call->call.args.items[1];
    const char *msg = dg_literal_mismatch(col, val);
    if (msg) {
        zan_diag_emit(c->diag, DIAG_ERROR, val->loc,
                      "db.Update<T>().Set: %s", msg);
        return;
    }
    const char *m = NULL;
    zan_ast_node_t *arg = val;
    switch (col->kind) {
    case DF_INT: m = incr ? "SetIncrI" : "SetI"; break;
    case DF_ENUM:
        m = incr ? "SetIncrI" : "SetI";
        arg = nb_cast_int(c, val->loc, val);
        break;
    case DF_DOUBLE:
        m = incr ? "SetIncrD" : "SetD";
        if (val->kind != AST_FLOAT_LITERAL)
            arg = nb_cast_double(c, val->loc, val);
        break;
    case DF_STRING: m = "SetS"; break;
    case DF_BOOL: m = "SetB"; break;
    default: break;
    }
    if (!m || (incr && (col->kind == DF_STRING || col->kind == DF_BOOL))) {
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "unsupported column type for Set/SetIncr");
        return;
    }
    zan_ast_node_t *recv = call->call.callee->member.object;
    zan_ast_node_t *node =
        nb_call1(c, call->loc,
                 nb_member(c, call->loc, recv, dg_istr(c, m, strlen(m))),
                 nb_strlit(c, call->loc, col->col.str, col->col.len));
    zan_ast_list_push(&node->call.args, arg, c->arena);
    *call = *node;
}

/* `Sum/Avg/Max/Min(a => a.col)`: the terminal returns the column's own type
 * (AVG always a double, like SQL). */
static void dg_rewrite_agg(dg_ctx_t *c, zan_ast_node_t *call, zan_istr_t cls,
                           const char *fn) {
    dg_fields_t fields;
    const dg_field_t *col = dg_sel_column(c, call, cls, &fields, fn);
    if (!col) return;
    if (col->kind != DF_INT && col->kind != DF_DOUBLE) {
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "%s requires an int or double column", fn);
        return;
    }
    char buf[200];
    snprintf(buf, sizeof(buf), "%s(t.%.*s)", fn, (int)col->col.len,
             col->col.str);
    bool as_int = col->kind == DF_INT && strcmp(fn, "AVG") != 0;
    const char *m = as_int ? "AggI" : "AggD";
    zan_ast_node_t *recv = call->call.callee->member.object;
    zan_ast_node_t *node =
        nb_call1(c, call->loc,
                 nb_member(c, call->loc, recv, dg_istr(c, m, strlen(m))),
                 nb_strlit(c, call->loc, buf, strlen(buf)));
    *call = *node;
}

/* `InsertColumns(a => a.col)` / `IgnoreColumns(a => a.col)`. */
static void dg_rewrite_cols(dg_ctx_t *c, zan_ast_node_t *call, zan_istr_t cls,
                            bool only) {
    dg_fields_t fields;
    const dg_field_t *col = dg_sel_column(
        c, call, cls, &fields,
        only ? "InsertColumns/UpdateColumns" : "IgnoreColumns");
    if (!col) return;
    const char *m = only ? "Only" : "Skip";
    zan_ast_node_t *recv = call->call.callee->member.object;
    zan_ast_node_t *node =
        nb_call1(c, call->loc,
                 nb_member(c, call->loc, recv, dg_istr(c, m, strlen(m))),
                 nb_strlit(c, call->loc, col->col.str, col->col.len));
    *call = *node;
}

/* `db.SyncStructure<T>()` -> `__DbBind.CF_T(db)` (CodeFirst DDL). */
static void dg_rewrite_sync(dg_ctx_t *c, zan_ast_node_t *call,
                            zan_istr_t cls) {
    dg_need_add(&c->need, cls);
    c->any = true;
    zan_ast_node_t *callee = call->call.callee;
    zan_ast_node_t *recv = callee->member.object;
    char mname[160];
    snprintf(mname, sizeof(mname), "CF_%.*s", (int)cls.len, cls.str);
    callee->member.object = nb_ident(c, call->loc, "__DbBind");
    callee->member.name = dg_istr(c, mname, strlen(mname));
    call->call.type_args.count = 0;
    zan_ast_list_init(&call->call.args);
    zan_ast_list_push(&call->call.args, recv, c->arena);
}

/* `db.SyncStructureAll()` -> `__DbBind.SyncAll(db)`. Every class carrying a
 * [Table] attribute is included by the generated binder. */
static void dg_rewrite_sync_all(dg_ctx_t *c, zan_ast_node_t *call) {
    c->any = true;
    zan_ast_node_t *callee = call->call.callee;
    zan_ast_node_t *recv = callee->member.object;
    callee->member.object = nb_ident(c, call->loc, "__DbBind");
    callee->member.name = dg_istr(c, "SyncAll", 7);
    call->call.type_args.count = 0;
    zan_ast_list_init(&call->call.args);
    zan_ast_list_push(&call->call.args, recv, c->arena);
}

static void dg_rewrite_orderby(dg_ctx_t *c, zan_ast_node_t *call,
                               zan_istr_t cls, bool desc) {
    zan_ast_node_t *lambda = call->call.args.items[0];
    zan_ast_node_t *body =
        (lambda->kind == AST_LAMBDA && lambda->lambda.params.count == 1)
            ? lambda->lambda.body
            : NULL;
    dg_fields_t fields;
    memset(&fields, 0, sizeof(fields));
    dg_collect_fields(c->unit, dg_find_class(c->unit, cls), &fields, 0);
    const dg_field_t *col = NULL;
    if (body && body->kind == AST_MEMBER_ACCESS &&
        body->member.object->kind == AST_IDENTIFIER &&
        istr_eq(body->member.object->ident.name,
                lambda->lambda.params.items[0]->param.name))
        col = dg_field_find(&fields, body->member.name);
    if (!col || col->kind == DF_NAV) {
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "OrderBy expects p => p.<scalar field>");
        return;
    }
    char buf[160];
    snprintf(buf, sizeof(buf), "t.%.*s", (int)col->name.len, col->name.str);
    zan_ast_node_t *arg = nb_strlit(c, call->loc, buf, strlen(buf));
    call->call.callee->member.name =
        dg_istr(c, desc ? "OBD" : "OB", desc ? 3 : 2);
    zan_ast_list_init(&call->call.args);
    zan_ast_list_push(&call->call.args, arg, c->arena);
}

/* `ToList(a => a.field)` -> `.ToListCol("t.field")` returning a scalar
 * list, and `ToList<TDto>(a => new Dto { ... })` -> `.ToListDto(...)` mapping
 * columns into the DTO. */

/* ---- Expr<T> target-typed lambda lowering ----
 *
 * A method whose declared parameter type is `Expr<T>` receives lambdas as
 * inspectable expression trees: dbgen rewrites `M(x => x.f > v)` into
 * `M(Expr<T>.From(DbExpr.Lambda("x", DbExpr.Binary(">", ...))))` so the
 * ORM / Linq / rules engine can walk the tree in pure Zan at runtime. */

static zan_ast_node_t *nb_type_ref(dg_ctx_t *c, zan_loc_t loc,
                                   const char *name, size_t n) {
    zan_ast_node_t *t = nb(c, AST_TYPE_REF, loc);
    t->type_ref.name = dg_istr(c, name, n);
    zan_ast_list_init(&t->type_ref.type_args);
    t->type_ref.is_nullable = false;
    t->type_ref.is_array = false;
    return t;
}

static zan_ast_node_t *nb_call_multi(dg_ctx_t *c, zan_loc_t loc,
                                     zan_ast_node_t *callee,
                                     zan_ast_node_t **args, int nargs) {
    zan_ast_node_t *e = nb(c, AST_CALL, loc);
    e->call.callee = callee;
    zan_ast_list_init(&e->call.args);
    zan_ast_list_init(&e->call.type_args);
    for (int i = 0; i < nargs; i++)
        zan_ast_list_push(&e->call.args, args[i], c->arena);
    return e;
}

static const char *dg_expr_op(zan_token_kind_t op) {
    switch (op) {
    case TK_EQ_EQ: return "==";
    case TK_BANG_EQ: return "!=";
    case TK_LESS: return "<";
    case TK_GREATER: return ">";
    case TK_LESS_EQ: return "<=";
    case TK_GREATER_EQ: return ">=";
    case TK_PLUS: return "+";
    case TK_MINUS: return "-";
    case TK_STAR: return "*";
    case TK_SLASH: return "/";
    case TK_PERCENT: return "%";
    case TK_AMP_AMP: return "&&";
    case TK_PIPE_PIPE: return "||";
    default: return NULL;
    }
}

static zan_ast_node_t *dg_expr_call(dg_ctx_t *c, zan_loc_t loc,
                                    const char *fn, size_t fn_len,
                                    zan_ast_node_t **args, int nargs) {
    zan_ast_node_t *callee =
        nb_member(c, loc, nb_ident(c, loc, "ExprNode"),
                  dg_istr(c, fn, fn_len));
    return nb_call_multi(c, loc, callee, args, nargs);
}

/* Build a DbExpr construction AST for an expression tree node. `pname` is the
 * lambda parameter name: `p.field` becomes a Member node and the bare `p`
 * identifier is an error (the parameter itself has no node). */
static zan_ast_node_t *dg_expr_tree(dg_ctx_t *c, zan_ast_node_t *e,
                                    zan_istr_t pname) {
    if (!e) return NULL;
    zan_loc_t loc = e->loc;
    if (e->kind == AST_MEMBER_ACCESS) {
        if (e->member.object && e->member.object->kind == AST_IDENTIFIER &&
            istr_eq(e->member.object->ident.name, pname)) {
            zan_ast_node_t *args[1];
            args[0] = nb_strlit(c, loc, e->member.name.str,
                                (size_t)e->member.name.len);
            return dg_expr_call(c, loc, "Member", 6, args, 1);
        }
        zan_diag_emit(c->diag, DIAG_ERROR, loc,
                      "expression-tree member access must read the lambda "
                      "parameter (p => p.<field>)");
        return NULL;
    }
    if (e->kind == AST_INT_LITERAL) {
        zan_ast_node_t *v = nb(c, AST_INT_LITERAL, loc);
        v->int_val = e->int_val;
        zan_ast_node_t *args[1];
        args[0] = v;
        return dg_expr_call(c, loc, "Const", 5, args, 1);
    }
    if (e->kind == AST_FLOAT_LITERAL) {
        zan_ast_node_t *v = nb(c, AST_FLOAT_LITERAL, loc);
        v->float_val = e->float_val;
        zan_ast_node_t *args[1];
        args[0] = v;
        return dg_expr_call(c, loc, "Const", 5, args, 1);
    }
    if (e->kind == AST_STRING_LITERAL) {
        zan_ast_node_t *args[1];
        args[0] = nb_strlit(c, loc, e->str_val.str, (size_t)e->str_val.len);
        return dg_expr_call(c, loc, "Const", 5, args, 1);
    }
    if (e->kind == AST_BOOL_LITERAL) {
        zan_ast_node_t *v = nb(c, AST_BOOL_LITERAL, loc);
        v->bool_val = e->bool_val;
        zan_ast_node_t *args[1];
        args[0] = v;
        return dg_expr_call(c, loc, "Const", 5, args, 1);
    }
    if (e->kind == AST_BINARY) {
        const char *op = dg_expr_op(e->binary.op);
        if (!op) {
            zan_diag_emit(c->diag, DIAG_ERROR, loc,
                          "unsupported operator in expression tree");
            return NULL;
        }
        zan_ast_node_t *l = dg_expr_tree(c, e->binary.left, pname);
        zan_ast_node_t *r = dg_expr_tree(c, e->binary.right, pname);
        if (!l || !r) return NULL;
        zan_ast_node_t *args[3];
        args[0] = nb_strlit(c, loc, op, strlen(op));
        args[1] = l;
        args[2] = r;
        return dg_expr_call(c, loc, "Binary", 6, args, 3);
    }
    if (e->kind == AST_UNARY && e->unary.op == TK_BANG) {
        zan_ast_node_t *x = dg_expr_tree(c, e->unary.operand, pname);
        if (!x) return NULL;
        zan_ast_node_t *args[1];
        args[0] = x;
        return dg_expr_call(c, loc, "Not", 3, args, 1);
    }
    if (e->kind == AST_CALL) {
        zan_ast_node_t *callee = e->call.callee;
        if (!callee || callee->kind != AST_MEMBER_ACCESS) {
            zan_diag_emit(c->diag, DIAG_ERROR, loc,
                          "expression-tree calls must be p.chain.Method(...)");
            return NULL;
        }
        zan_istr_t mn = callee->member.name;
        if (!istr_is(mn, "StartsWith") && !istr_is(mn, "EndsWith") &&
            !istr_is(mn, "Contains")) {
            zan_diag_emit(c->diag, DIAG_ERROR, loc,
                          "unsupported method '%.*s' in expression tree",
                          (int)mn.len, mn.str);
            return NULL;
        }
        if (e->call.args.count > 1) {
            zan_diag_emit(c->diag, DIAG_ERROR, loc,
                          "expression-tree calls take at most one argument");
            return NULL;
        }
        /* The receiver is the member chain up to the method name
         * (`o.region` in `o.region.StartsWith("A")`); a bare `o` receiver
         * is represented as null (the lambda parameter itself). */
        zan_ast_node_t *obj = callee->member.object;
        zan_ast_node_t *recv = NULL;
        if (obj->kind == AST_IDENTIFIER && istr_eq(obj->ident.name, pname)) {
            recv = NULL;
        } else {
            recv = dg_expr_tree(c, obj, pname);
            if (!recv) return NULL;
        }
        zan_ast_node_t *arg = NULL;
        if (e->call.args.count == 1) {
            arg = dg_expr_tree(c, e->call.args.items[0], pname);
            if (!arg) return NULL;
        }
        zan_ast_node_t *args[3];
        args[0] = nb_strlit(c, loc, callee->member.name.str,
                            (size_t)callee->member.name.len);
        args[1] = recv ? recv : nb_null(c, loc);
        args[2] = arg ? arg : nb_null(c, loc);
        return dg_expr_call(c, loc, "Call", 4, args, 3);
    }
    zan_diag_emit(c->diag, DIAG_ERROR, loc,
                  "unsupported expression in lambda tree");
    return NULL;
}

/* Wrap a lowered tree as Expr<T>.From(ExprNode.Lambda(pname, tree)). */
static zan_ast_node_t *dg_expr_wrap(dg_ctx_t *c, zan_loc_t loc,
                                    zan_istr_t entity, zan_istr_t pname,
                                    zan_ast_node_t *tree) {
    zan_ast_node_t *largs[2];
    largs[0] = nb_strlit(c, loc, pname.str, (size_t)pname.len);
    largs[1] = tree;
    zan_ast_node_t *lam = dg_expr_call(c, loc, "Lambda", 6, largs, 2);
    zan_ast_node_t *expr_id = nb_ident(c, loc, "Expr");
    zan_ast_node_t *ta = nb_type_ref(c, loc, entity.str, (size_t)entity.len);
    expr_id->inst_type_ref = nb_type_ref(c, loc, "Expr", 4);
    zan_ast_list_push(&expr_id->inst_type_ref->type_ref.type_args, ta,
                      c->arena);
    zan_ast_node_t *from_callee =
        nb_member(c, loc, expr_id, dg_istr(c, "From", 4));
    return nb_call1(c, loc, from_callee, lam);
}

/* True when the receiver chain bottoms out at the lambda parameter
 * (`o` in `o.region` / `o.region.Sub` / ...). */
static bool dg_expr_pname_chain(zan_ast_node_t *e, zan_istr_t pname) {
    if (!e) return false;
    if (e->kind == AST_IDENTIFIER) return istr_eq(e->ident.name, pname);
    if (e->kind == AST_MEMBER_ACCESS)
        return dg_expr_pname_chain(e->member.object, pname);
    return false;
}

/* The direct operand of a comparison must be a column read or a literal; a
 * captured variable cannot be lowered into an Expr tree (its value lives at
 * runtime) and falls back to the classic fragment path which binds it. */
static bool dg_expr_is_value(zan_ast_node_t *e, zan_istr_t pname) {
    if (e->kind == AST_MEMBER_ACCESS)
        return dg_expr_pname_chain(e->member.object, pname);
    if (e->kind == AST_INT_LITERAL || e->kind == AST_FLOAT_LITERAL ||
        e->kind == AST_STRING_LITERAL || e->kind == AST_BOOL_LITERAL)
        return true;
    return false;
}

static const dg_field_t *dg_member_field(const dg_fields_t *fs,
                                         zan_ast_node_t *e,
                                         zan_istr_t pname) {
    if (!fs || e->kind != AST_MEMBER_ACCESS) return NULL;
    if (!e->member.object || e->member.object->kind != AST_IDENTIFIER ||
        !istr_eq(e->member.object->ident.name, pname))
        return NULL;
    return dg_field_find(fs, e->member.name);
}

static int dg_lit_kind(zan_ast_node_t *e) {
    if (e->kind == AST_INT_LITERAL) return DF_INT;
    if (e->kind == AST_FLOAT_LITERAL) return DF_DOUBLE;
    if (e->kind == AST_STRING_LITERAL) return DF_STRING;
    if (e->kind == AST_BOOL_LITERAL) return DF_BOOL;
    return -1;
}

/* A literal of kind lk may be compared against a column of kind fk. Keeps the
 * negative diagnostics (int column vs string literal and vice versa) alive:
 * those predicates stay on the classic path, which reports the compile-time
 * type error the test expects. */
static bool dg_kind_compat(int fk, int lk) {
    switch (fk) {
    case DF_INT:
    case DF_ENUM:
        return lk == DF_INT || lk == DF_BOOL;
    case DF_DOUBLE:
        return lk == DF_INT || lk == DF_DOUBLE;
    case DF_BOOL:
        return lk == DF_BOOL || lk == DF_INT;
    case DF_STRING:
        return lk == DF_STRING;
    default:
        return false;
    }
}

static bool dg_expr_type_ok(const dg_fields_t *fs, zan_ast_node_t *l,
                            zan_ast_node_t *r, zan_istr_t pname) {
    const dg_field_t *lf = dg_member_field(fs, l, pname);
    const dg_field_t *rf = dg_member_field(fs, r, pname);
    int lk = dg_lit_kind(l);
    int rk = dg_lit_kind(r);
    if (lf && rk >= 0) return dg_kind_compat(lf->kind, rk);
    if (rf && lk >= 0) return dg_kind_compat(rf->kind, lk);
    return true; /* column-vs-column and constant-vs-constant: let SQL decide */
}

/* True when the lambda body can be lowered to an Expr tree that ExprSql can
 * build SQL for: member reads, literals, supported binary ops (with operand
 * type checks), not, and p-chain StartsWith/EndsWith/Contains calls. Anything
 * else (Eq/Gt/Between/IsNull/Like/In method calls, captured variables,
 * aggregates, ...) falls back to the classic SQL-fragment path so existing
 * behavior — including negative type diagnostics — is preserved. */
static bool dg_expr_compatible(dg_ctx_t *c, const dg_fields_t *fs,
                               zan_ast_node_t *e, zan_istr_t pname) {
    (void)c;
    if (!e) return false;
    if (e->kind == AST_MEMBER_ACCESS)
        return dg_expr_pname_chain(e->member.object, pname);
    if (e->kind == AST_INT_LITERAL || e->kind == AST_FLOAT_LITERAL ||
        e->kind == AST_STRING_LITERAL || e->kind == AST_BOOL_LITERAL)
        return true;
    if (e->kind == AST_BINARY) {
        const char *op = dg_expr_op(e->binary.op);
        if (!op) return false;
        if (!dg_expr_compatible(c, fs, e->binary.left, pname) ||
            !dg_expr_compatible(c, fs, e->binary.right, pname))
            return false;
        if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0)
            return true; /* boolean combination: operands already checked */
        if (!dg_expr_is_value(e->binary.left, pname) ||
            !dg_expr_is_value(e->binary.right, pname))
            return false; /* captured variable: classic path binds it */
        return dg_expr_type_ok(fs, e->binary.left, e->binary.right, pname);
    }
    if (e->kind == AST_UNARY && e->unary.op == TK_BANG)
        return dg_expr_compatible(c, fs, e->unary.operand, pname);
    if (e->kind == AST_CALL) {
        zan_ast_node_t *callee = e->call.callee;
        if (!callee || callee->kind != AST_MEMBER_ACCESS) return false;
        zan_istr_t mn = callee->member.name;
        if (!istr_is(mn, "StartsWith") && !istr_is(mn, "EndsWith") &&
            !istr_is(mn, "Contains"))
            return false; /* Eq/Gt/Like/In/... stay on the classic path */
        if (!dg_expr_pname_chain(callee->member.object, pname)) return false;
        if (e->call.args.count > 1) return false;
        if (e->call.args.count == 1 &&
            !dg_expr_compatible(c, fs, e->call.args.items[0], pname))
            return false;
        return true;
    }
    return false;
}

/* Scan every type declaration for methods whose parameter type AST is
 * `Expr<...>` (shape check only — no binder needed) and record a slot so
 * call sites can lower lambdas into trees. */
static void dg_collect_expr_slots(dg_ctx_t *c) {
    zan_ast_node_t *unit = c->unit;
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (!d || (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL)) continue;
        zan_istr_t cls = d->type_decl.name;
        for (int m = 0; m < d->type_decl.members.count; m++) {
            zan_ast_node_t *mem = d->type_decl.members.items[m];
            if (!mem || mem->kind != AST_METHOD_DECL) continue;
            for (int p = 0; p < mem->method_decl.params.count; p++) {
                zan_ast_node_t *pt = mem->method_decl.params.items[p]->param.type;
                if (!pt || pt->kind != AST_TYPE_REF ||
                    !istr_is(pt->type_ref.name, "Expr") ||
                    pt->type_ref.type_args.count != 1)
                    continue;
                zan_ast_node_t *ta = pt->type_ref.type_args.items[0];
                if (c->eslot_count >= 128) return;
                dg_expr_slot_t *s = &c->eslots[c->eslot_count++];
                s->cls = cls;
                s->name = mem->method_decl.name;
                s->param = p;
                s->entity = (ta && ta->kind == AST_TYPE_REF)
                                ? ta->type_ref.name
                                : (zan_istr_t){NULL, 0};
            }
        }
    }
}

/* Rewrite `recv.Method(args)` (or bare `Method(args)`) when the argument at
 * the Expr slot is a lambda: `M(p => body)` becomes
 * `M(Expr<T>.From(DbExpr.Lambda("p", <tree>)))`. */
static bool dg_expr_rewrite_call(dg_ctx_t *c, zan_ast_node_t *e) {
    zan_ast_node_t *callee = e->call.callee;
    if (!callee) return false;
    zan_istr_t cls = {NULL, 0};
    zan_istr_t name;
    bool direct = false;
    if (callee->kind == AST_IDENTIFIER) {
        name = callee->ident.name;
        direct = true; /* unqualified call: match any class of that name */
    } else if (callee->kind == AST_MEMBER_ACCESS &&
               callee->member.object &&
               callee->member.object->kind == AST_IDENTIFIER) {
        cls = callee->member.object->ident.name;
        name = callee->member.name;
    } else {
        return false;
    }
    for (int i = 0; i < c->eslot_count; i++) {
        dg_expr_slot_t *s = &c->eslots[i];
        if (!istr_eq(s->name, name)) continue;
        if (!direct && !istr_eq(s->cls, cls)) continue;
        if (s->param >= e->call.args.count) continue;
        zan_ast_node_t *arg = e->call.args.items[s->param];
        if (!arg || arg->kind != AST_LAMBDA ||
            arg->lambda.params.count != 1)
            continue;
        zan_istr_t pname = arg->lambda.params.items[0]->param.name;
        zan_ast_node_t *tree = dg_expr_tree(c, arg->lambda.body, pname);
        if (!tree) return true; /* diagnostic already emitted */
        e->call.args.items[s->param] =
            dg_expr_wrap(c, e->loc, s->entity, pname, tree);
        return true;
    }
    return false;
}

static void dg_rewrite_tolist(dg_ctx_t *c, zan_ast_node_t *call,
                              zan_istr_t cls) {
    zan_ast_node_t *lambda = call->call.args.items[0];
    if (lambda->kind != AST_LAMBDA || lambda->lambda.params.count != 1) {
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "ToList expects a one-parameter lambda");
        return;
    }
    zan_ast_node_t *body = lambda->lambda.body;
    zan_istr_t pname = lambda->lambda.params.items[0]->param.name;
    dg_fields_t fields;
    memset(&fields, 0, sizeof(fields));
    dg_collect_fields(c->unit, dg_find_class(c->unit, cls), &fields, 0);

    /* DTO mapping: ToList<TDto>(a => new Dto { f1 = a.x, f2 = a.y }) */
    if (call->call.type_args.count == 1) {
        zan_ast_node_t *targ = call->call.type_args.items[0];
        zan_istr_t dtc = targ->type_ref.name;
        if (body->kind != AST_NEW_EXPR) {
            zan_diag_emit(c->diag, DIAG_ERROR, body->loc,
                          "ToList<Dto> expects a => new Dto { f = a.col, ... }");
            return;
        }
        dg_buf_t cols;
        memset(&cols, 0, sizeof(cols));
        dg_fields_t df;
        memset(&df, 0, sizeof(df));
        zan_ast_node_t *ddecl = dg_find_class(c->unit, dtc);
        if (ddecl) dg_collect_fields(c->unit, ddecl, &df, 0);
        bool first = true;
        for (int i = 0; i < body->new_expr.args.count; i++) {
            zan_ast_node_t *arg = body->new_expr.args.items[i];
            zan_istr_t dname = {NULL, 0};
            zan_ast_node_t *src = arg;
            if (arg->kind == AST_ASSIGNMENT &&
                arg->binary.left->kind == AST_IDENTIFIER) {
                dname = arg->binary.left->ident.name;
                src = arg->binary.right;
            }
            const dg_field_t *col = NULL;
            if (src->kind == AST_MEMBER_ACCESS &&
                src->member.object->kind == AST_IDENTIFIER &&
                istr_eq(src->member.object->ident.name, pname))
                col = dg_field_find(&fields, src->member.name);
            if (!col || col->kind == DF_NAV) {
                zan_diag_emit(c->diag, DIAG_ERROR, src->loc,
                              "ToList<Dto> member must read p => p.<scalar field>");
                return;
            }
            dg_putf(&cols, "%st.%.*s", first ? "" : ", ", (int)col->name.len,
                    col->name.str);
            first = false;
            if (dname.len == 0) dname = src->member.name;
            (void)dname;
        }
        if (first) {
            zan_diag_emit(c->diag, DIAG_ERROR, body->loc,
                          "ToList<Dto> must name at least one member");
            return;
        }
        zan_ast_node_t *a1 = nb_strlit(c, call->loc, cols.buf, strlen(cols.buf));
        free(cols.buf);
        zan_ast_node_t *a2 = nb_strlit(c, call->loc, dtc.str, dtc.len);
        call->call.callee->member.name = dg_istr(c, "ToListDto", 9);
        call->call.type_args.count = 0;
        zan_ast_list_init(&call->call.args);
        zan_ast_list_push(&call->call.args, a1, c->arena);
        zan_ast_list_push(&call->call.args, a2, c->arena);
        return;
    }

    /* scalar projection: ToList(a => a.field) */
    const dg_field_t *col = NULL;
    if (body->kind == AST_MEMBER_ACCESS &&
        body->member.object->kind == AST_IDENTIFIER &&
        istr_eq(body->member.object->ident.name, pname))
        col = dg_field_find(&fields, body->member.name);
    if (!col || col->kind == DF_NAV) {
        zan_diag_emit(c->diag, DIAG_ERROR, body->loc,
                      "ToList expects a => a.<scalar field>");
        return;
    }
    char buf[160];
    snprintf(buf, sizeof(buf), "t.%.*s", (int)col->name.len, col->name.str);
    zan_ast_node_t *arg = nb_strlit(c, call->loc, buf, strlen(buf));
    call->call.callee->member.name = dg_istr(c, "ToListCol", 9);
    call->call.type_args.count = 0;
    zan_ast_list_init(&call->call.args);
    zan_ast_list_push(&call->call.args, arg, c->arena);
}

static void dg_rewrite_include(dg_ctx_t *c, zan_ast_node_t *call,
                               zan_istr_t cls) {
    zan_ast_node_t *lambda = call->call.args.items[0];
    zan_ast_node_t *body =
        (lambda->kind == AST_LAMBDA && lambda->lambda.params.count == 1)
            ? lambda->lambda.body
            : NULL;
    dg_fields_t fields;
    memset(&fields, 0, sizeof(fields));
    dg_collect_fields(c->unit, dg_find_class(c->unit, cls), &fields, 0);
    const dg_field_t *col = NULL;
    if (body && body->kind == AST_MEMBER_ACCESS &&
        body->member.object->kind == AST_IDENTIFIER &&
        istr_eq(body->member.object->ident.name,
                lambda->lambda.params.items[0]->param.name))
        col = dg_field_find(&fields, body->member.name);
    if (!col || col->kind != DF_NAV) {
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "Include expects p => p.<navigation field>");
        return;
    }
    dg_need_add(&c->need, col->type_name);
    char buf[160];
    snprintf(buf, sizeof(buf), "Inc_%.*s", (int)col->name.len, col->name.str);
    call->call.callee->member.name = dg_istr(c, buf, strlen(buf));
    zan_ast_list_init(&call->call.args);
}

/* ---- AST walk ---- */

static void dg_visit_expr(dg_ctx_t *c, zan_ast_node_t *e);

static void dg_visit_list(dg_ctx_t *c, zan_ast_list_t *l) {
    for (int i = 0; i < l->count; i++) dg_visit_expr(c, l->items[i]);
}

static void dg_visit_call(dg_ctx_t *c, zan_ast_node_t *e) {
    /* children first: the Query<T>() root is rewritten before the chain
     * methods above it look for it */
    dg_visit_expr(c, e->call.callee);
    dg_visit_list(c, &e->call.args);

    zan_istr_t cls;
    dg_ck_t rk = CK_NONE;
    bool sync = false;
    if (e->call.callee && e->call.callee->kind == AST_MEMBER_ACCESS
        && istr_is(e->call.callee->member.name, "SyncStructureAll")
        && e->call.type_args.count == 0 && e->call.args.count == 0) {
        c->sync_all = true;
        dg_rewrite_sync_all(c, e);
        return;
    }
    /* Expr<T> target-typed lambda: rewrite before ORM chain handling so the
     * tree lowering applies to any method whose parameter is Expr<T>. */
    if (dg_expr_rewrite_call(c, e)) return;
    if (dg_is_root(c, e, &cls, &rk, &sync)) {
        if (sync) dg_rewrite_sync(c, e, cls);
        else dg_rewrite_root(c, e, cls,
                             rk == CK_SELECT   ? 'Q'
                             : rk == CK_INSERT ? 'I'
                             : rk == CK_UPDATE ? 'U'
                                               : 'D');
        return;
    }
    zan_ast_node_t *callee = e->call.callee;
    if (!callee || callee->kind != AST_MEMBER_ACCESS) return;
    zan_istr_t mn = callee->member.name;
    dg_ck_t ck = CK_NONE;
    zan_istr_t entity = dg_chain_entity2(callee->member.object, &ck);
    if (entity.len == 0) return;

    bool is_where = istr_is(mn, "Where");
    bool is_whereif = istr_is(mn, "WhereIf");
    bool is_ob = istr_is(mn, "OrderBy");
    bool is_obd = istr_is(mn, "OrderByDescending");
    bool is_inc = istr_is(mn, "Include");
    bool is_set = istr_is(mn, "Set");
    bool is_incr = istr_is(mn, "SetIncr");
    bool is_only = istr_is(mn, "InsertColumns") || istr_is(mn, "UpdateColumns");
    bool is_skip = istr_is(mn, "IgnoreColumns");
    bool is_groupby = istr_is(mn, "GroupBy");
    bool is_having = istr_is(mn, "Having");
    bool is_distinct = istr_is(mn, "Distinct");
    const char *agg = istr_is(mn, "Sum")   ? "SUM"
                      : istr_is(mn, "Avg") ? "AVG"
                      : istr_is(mn, "Max") ? "MAX"
                      : istr_is(mn, "Min") ? "MIN"
                                           : NULL;

    if (is_distinct && ck == CK_SELECT && e->call.args.count == 0) {
        dg_rewrite_distinct(c, e);
        return;
    }
    if (is_whereif) {
        if (e->call.args.count != 2 ||
            e->call.args.items[1]->kind != AST_LAMBDA)
            return;
        dg_rewrite_where_if(c, e, entity);
        return;
    }
    if ((is_set || is_incr) && ck == CK_UPDATE) {
        if (e->call.args.count != 2 ||
            e->call.args.items[0]->kind != AST_LAMBDA)
            return;
        dg_rewrite_set(c, e, entity, is_incr);
        return;
    }
    if (e->call.args.count != 1 || e->call.args.items[0]->kind != AST_LAMBDA)
        return;
    if (is_where) dg_rewrite_where(c, e, entity);
    else if (is_inc && ck == CK_SELECT) dg_rewrite_include(c, e, entity);
    else if ((is_ob || is_obd) && ck == CK_SELECT)
        dg_rewrite_orderby(c, e, entity, is_obd);
    else if (agg && ck == CK_SELECT) dg_rewrite_agg(c, e, entity, agg);
    else if (is_groupby && ck == CK_SELECT) dg_rewrite_groupby(c, e, entity);
    else if (is_having && ck == CK_SELECT) dg_rewrite_having(c, e, entity);
    else if (istr_is(mn, "ToList") && ck == CK_SELECT)
        dg_rewrite_tolist(c, e, entity);
    else if ((is_only || is_skip) && (ck == CK_INSERT || ck == CK_UPDATE))
        dg_rewrite_cols(c, e, entity, is_only);
}

static void dg_visit_expr(dg_ctx_t *c, zan_ast_node_t *e) {
    if (!e) return;
    switch (e->kind) {
    case AST_BLOCK: dg_visit_list(c, &e->block.stmts); break;
    case AST_VAR_DECL: dg_visit_expr(c, e->var_decl.initializer); break;
    case AST_EXPR_STMT: dg_visit_expr(c, e->expr_stmt.expr); break;
    case AST_RETURN_STMT: dg_visit_expr(c, e->ret.value); break;
    case AST_IF_STMT:
        dg_visit_expr(c, e->if_stmt.cond);
        dg_visit_expr(c, e->if_stmt.then_body);
        dg_visit_expr(c, e->if_stmt.else_body);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        dg_visit_expr(c, e->while_stmt.cond);
        dg_visit_expr(c, e->while_stmt.body);
        break;
    case AST_FOR_STMT:
        dg_visit_expr(c, e->for_stmt.init);
        dg_visit_expr(c, e->for_stmt.cond);
        dg_visit_expr(c, e->for_stmt.step);
        dg_visit_expr(c, e->for_stmt.body);
        break;
    case AST_FOREACH_STMT:
        dg_visit_expr(c, e->foreach_stmt.collection);
        dg_visit_expr(c, e->foreach_stmt.body);
        break;
    case AST_THROW_STMT: dg_visit_expr(c, e->throw_stmt.value); break;
    case AST_TRY_STMT:
        dg_visit_expr(c, e->try_stmt.try_body);
        for (int i = 0; i < e->try_stmt.catches.count; i++)
            dg_visit_expr(c, e->try_stmt.catches.items[i]->catch_clause.body);
        dg_visit_expr(c, e->try_stmt.finally_body);
        break;
    case AST_SWITCH_STMT:
        dg_visit_expr(c, e->switch_stmt.expr);
        for (int i = 0; i < e->switch_stmt.cases.count; i++) {
            dg_visit_expr(c, e->switch_stmt.cases.items[i]->switch_case.pattern);
            dg_visit_expr(c, e->switch_stmt.cases.items[i]->switch_case.body);
        }
        break;
    case AST_YIELD_STMT: dg_visit_expr(c, e->yield_stmt.value); break;
    case AST_LOCK_STMT:
        dg_visit_expr(c, e->lock_stmt.expr);
        dg_visit_expr(c, e->lock_stmt.body);
        break;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        dg_visit_expr(c, e->binary.left);
        dg_visit_expr(c, e->binary.right);
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        dg_visit_expr(c, e->unary.operand);
        break;
    case AST_CALL: dg_visit_call(c, e); break;
    case AST_MEMBER_ACCESS: dg_visit_expr(c, e->member.object); break;
    case AST_INDEX:
        dg_visit_expr(c, e->index.object);
        dg_visit_expr(c, e->index.index);
        break;
    case AST_NEW_EXPR: dg_visit_list(c, &e->new_expr.args); break;
    case AST_CAST_EXPR: dg_visit_expr(c, e->cast.expr); break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        dg_visit_expr(c, e->type_test.expr);
        break;
    case AST_CONDITIONAL:
        dg_visit_expr(c, e->conditional.cond);
        dg_visit_expr(c, e->conditional.then_expr);
        dg_visit_expr(c, e->conditional.else_expr);
        break;
    case AST_LAMBDA: dg_visit_expr(c, e->lambda.body); break;
    case AST_AWAIT_EXPR: dg_visit_expr(c, e->await_expr.expr); break;
    case AST_STRING_INTERP: dg_visit_list(c, &e->string_interp.parts); break;
    case AST_REF_ARG: dg_visit_expr(c, e->ref_arg.expr); break;
    case AST_QUERY_EXPR:
        dg_visit_expr(c, e->query.source);
        dg_visit_list(c, &e->query.wheres);
        dg_visit_expr(c, e->query.select);
        break;
    default: break;
    }
}

/* ---- per-entity code generation ---- */

static const char *dg_getter(dg_fk_t k) {
    switch (k) {
    case DF_INT:
    case DF_ENUM: return "GetInt";
    case DF_DOUBLE: return "GetDouble";
    case DF_BOOL: return "GetBool";
    case DF_STRING: return "GetString";
    default: return "GetString";
    }
}

static void dg_gen_entity(dg_ctx_t *c, dg_buf_t *out, zan_istr_t cls) {
    int CL = (int)cls.len;
    const char *CS = cls.str;
    zan_ast_node_t *decl = dg_find_class(c->unit, cls);
    if (!decl) return;
    dg_fields_t fs;
    memset(&fs, 0, sizeof(fs));
    dg_collect_fields(c->unit, decl, &fs, 0);
    zan_istr_t tbl = dg_table_of(c->unit, cls);
    int TBL = (int)tbl.len;
    const char *TBS = tbl.str;

    int nbase = 0;
    for (int i = 0; i < fs.count; i++)
        if (fs.items[i].kind != DF_NAV) nbase++;

    dg_putf(out, "class __DbQ_%.*s {\n", CL, CS);
    dg_putf(out,
        "    IDbExecutor db;\n"
        "    string tbl;\n"
        "    string w;\n"
        "    DbParams ps;\n"
        "    string ob;\n"
        "    string gb;\n"
        "    string h;\n"
        "    bool distinct;\n"
        "    int limN;\n"
        "    int offN;\n"
        "    bool on;\n");
    for (int i = 0; i < fs.count; i++)
        if (fs.items[i].kind == DF_NAV)
            dg_putf(out, "    bool j_%.*s;\n", (int)fs.items[i].name.len,
                    fs.items[i].name.str);
    dg_putf(out,
        "    static __DbQ_%.*s Create(IDbExecutor db) {\n"
        "        __DbQ_%.*s q = new __DbQ_%.*s();\n"
        "        q.db = db;\n"
        "        q.tbl = \"%.*s\";\n"
        "        q.on = true;\n"
        "        q.w = \"\";\n"
        "        q.ps = DbParams.Create();\n"
        "        q.ob = \"\";\n"
        "        q.gb = \"\";\n"
        "        q.h = \"\";\n"
        "        q.distinct = false;\n"
        "        q.limN = -1;\n"
        "        q.offN = -1;\n"
        "        return q;\n"
        "    }\n",
        CL, CS, CL, CS, CL, CS, TBL, TBS);
    dg_putf(out,
        "    __DbQ_%.*s W(string f) {\n"
        "        if (!this.on) { return this; }\n"
        "        if (this.w.Length == 0) { this.w = f; }\n"
        "        else { this.w = this.w + \" AND \" + f; }\n"
        "        return this;\n"
        "    }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s Where(Expr<%.*s> e) {\n"
        "        if (!this.on) { return this; }\n"
        "        if (e == null || e.Root == null) { return this; }\n"
        "        StringBuilder sb = new StringBuilder();\n"
        "        ExprSql.Build(e.Root, sb, this.ps);\n"
        "        return this.W(sb.ToString());\n"
        "    }\n",
        CL, CS, CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s WhereDict(DbValues v) {\n"
        "        if (!this.on) { return this; }\n"
        "        int i = 0;\n"
        "        while (i < v.Count()) {\n"
        "            string c = %.*sCols.Require(v.NameAt(i));\n"
        "            W(\"t.\" + c + \" = ?\");\n"
        "            v.BindAs(this.ps, i, %.*sCols.Kind(c));\n"
        "            i = i + 1;\n"
        "        }\n"
        "        return this;\n"
        "    }\n",
        CL, CS, CL, CS, CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s CondBegin(bool c) { this.on = c; return this; }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s CondEnd() { this.on = true; return this; }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s AsTable(string t) { this.tbl = t; return this; }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s P(string v) {\n"
        "        if (this.on) { this.ps.Add(v); }\n"
        "        return this;\n"
        "    }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s Pi(int v) {\n"
        "        if (this.on) { this.ps.AddInt(v); }\n"
        "        return this;\n"
        "    }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s Pd(double v) {\n"
        "        if (this.on) { this.ps.AddDouble(v); }\n"
        "        return this;\n"
        "    }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s InI(List<int> vs) {\n"
        "        if (!this.on) { return this; }\n"
        "        int i = 0;\n"
        "        while (i < vs.Count) { this.ps.AddInt(vs[i]); i = i + 1; }\n"
        "        return this;\n"
        "    }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s InS(List<string> vs) {\n"
        "        if (!this.on) { return this; }\n"
        "        int i = 0;\n"
        "        while (i < vs.Count) { this.ps.Add(vs[i]); i = i + 1; }\n"
        "        return this;\n"
        "    }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s InD(List<double> vs) {\n"
        "        if (!this.on) { return this; }\n"
        "        int i = 0;\n"
        "        while (i < vs.Count) { this.ps.AddDouble(vs[i]); i = i + 1; }\n"
        "        return this;\n"
        "    }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s OB(string col) {\n"
        "        if (this.ob.Length == 0) { this.ob = col; }\n"
        "        else { this.ob = this.ob + \", \" + col; }\n"
        "        return this;\n"
        "    }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s OBD(string col) { return OB(col + \" DESC\"); }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s GB(string col) { this.gb = col; return this; }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s H(string f) {\n"
        "        if (this.h.Length == 0) { this.h = f; }\n"
        "        else { this.h = this.h + \" AND \" + f; }\n"
        "        return this;\n"
        "    }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s DISTINCT() { this.distinct = true; return this; }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s Take(int n) { this.limN = n; return this; }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s Skip(int n) { this.offN = n; return this; }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s Limit(int n) { this.limN = n; return this; }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s Offset(int n) { this.offN = n; return this; }\n",
        CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s Page(int index, int size) {\n"
        "        if (index < 1) { index = 1; }\n"
        "        this.limN = size;\n"
        "        this.offN = (index - 1) * size;\n"
        "        return this;\n"
        "    }\n",
        CL, CS);
    for (int i = 0; i < fs.count; i++)
        if (fs.items[i].kind == DF_NAV)
            dg_putf(out,
                "    __DbQ_%.*s Inc_%.*s() { this.j_%.*s = true; "
                "return this; }\n",
                CL, CS, (int)fs.items[i].name.len, fs.items[i].name.str,
                (int)fs.items[i].name.len, fs.items[i].name.str);

    /* SELECT builder */
    dg_putf(out,
        "    string BuildSelect() {\n"
        "        StringBuilder sb = new StringBuilder();\n"
        "        sb.Append(\"SELECT \");\n"
        "        if (this.distinct) { sb.Append(\"DISTINCT \"); }\n");
    {
        dg_buf_t cols;
        memset(&cols, 0, sizeof(cols));
        bool first = true;
        for (int i = 0; i < fs.count; i++) {
            if (fs.items[i].kind == DF_NAV) continue;
            dg_putf(&cols, "%st.%.*s", first ? "" : ", ",
                    (int)fs.items[i].col.len, fs.items[i].col.str);
            first = false;
        }
        dg_putf(out, "        sb.Append(\"%s\");\n", cols.buf ? cols.buf : "");
        free(cols.buf);
    }
    for (int i = 0; i < fs.count; i++) {
        if (fs.items[i].kind != DF_NAV) continue;
        int FL = (int)fs.items[i].name.len;
        const char *FS = fs.items[i].name.str;
        zan_ast_node_t *ndecl = dg_find_class(c->unit, fs.items[i].type_name);
        dg_fields_t nfs;
        memset(&nfs, 0, sizeof(nfs));
        dg_collect_fields(c->unit, ndecl, &nfs, 0);
        dg_buf_t cols;
        memset(&cols, 0, sizeof(cols));
        for (int k = 0; k < nfs.count; k++) {
            if (nfs.items[k].kind == DF_NAV) continue;
            dg_putf(&cols, ", j_%.*s.%.*s", FL, FS,
                    (int)nfs.items[k].col.len, nfs.items[k].col.str);
        }
        dg_putf(out,
            "        if (this.j_%.*s) { sb.Append(\"%s\"); }\n",
            FL, FS, cols.buf ? cols.buf : "");
        free(cols.buf);
    }
    {
        zan_istr_t tb = dg_table_of(c->unit, cls);
        dg_putf(out,
                "        sb.Append(\" FROM \");\n"
                "        sb.Append(this.tbl);\n"
                "        sb.Append(\" t\");\n");
        (void)tb;
    }
    for (int i = 0; i < fs.count; i++) {
        if (fs.items[i].kind != DF_NAV) continue;
        int FL = (int)fs.items[i].name.len;
        const char *FS = fs.items[i].name.str;
        zan_istr_t ntb = dg_table_of(c->unit, fs.items[i].type_name);
        int TL = (int)ntb.len;
        const char *TS = ntb.str;
        dg_putf(out,
            "        if (this.j_%.*s) { sb.Append(\" LEFT JOIN %.*s j_%.*s "
            "ON t.%.*sId = j_%.*s.id\"); }\n",
            FL, FS, TL, TS, FL, FS, FL, FS, FL, FS);
    }
    dg_putf(out,
        "        if (this.w.Length > 0) { sb.Append(\" WHERE \"); "
        "sb.Append(this.w); }\n"
        "        if (this.gb.Length > 0) { sb.Append(\" GROUP BY \"); "
        "sb.Append(this.gb); }\n"
        "        if (this.h.Length > 0) { sb.Append(\" HAVING \"); "
        "sb.Append(this.h); }\n"
        "        if (this.ob.Length > 0) { sb.Append(\" ORDER BY \"); "
        "sb.Append(this.ob); }\n"
        "        int pv = this.db.GetProvider();\n"
        "        if (pv == DbProvider.SqlServer || pv == DbProvider.Oracle) {\n"
        "            int off = this.offN;\n"
        "            if (off < 0) { off = 0; }\n"
        "            if (this.ob.Length == 0) {\n"
        "                if (pv == DbProvider.SqlServer) { "
        "sb.Append(\" ORDER BY (SELECT 0)\"); }\n"
        "                else { sb.Append(\" ORDER BY (SELECT 0 FROM DUAL)\"); }\n"
        "            }\n"
        "            sb.Append(\" OFFSET \"); "
        "sb.Append(Convert.ToString(off)); sb.Append(\" ROWS\");\n"
        "            if (this.limN >= 0) { sb.Append(\" FETCH NEXT \"); "
        "sb.Append(Convert.ToString(this.limN)); sb.Append(\" ROWS ONLY\"); }\n"
        "        } else {\n"
        "            if (this.limN >= 0) { sb.Append(\" LIMIT \"); "
        "sb.Append(Convert.ToString(this.limN)); }\n"
        "            if (this.offN >= 0) { sb.Append(\" OFFSET \"); "
        "sb.Append(Convert.ToString(this.offN)); }\n"
        "        }\n"
        "        return sb.ToString();\n"
        "    }\n");

    /* row mapper + terminals */
    dg_putf(out,
        "    List<%.*s> ToList() {\n"
        "        DbResult r = this.db.Query(BuildSelect(), this.ps);\n"
        "        List<%.*s> outp = new List<%.*s>();\n"
        "        int row = 0;\n"
        "        while (row < r.RowCount()) {\n"
        "            %.*s o = new %.*s();\n",
        CL, CS, CL, CS, CL, CS, CL, CS, CL, CS);
    {
        int ci = 0;
        for (int i = 0; i < fs.count; i++) {
            if (fs.items[i].kind == DF_NAV) continue;
            if (fs.items[i].kind == DF_ENUM)
                /* enum casts are not parseable; int -> enum is implicit */
                dg_putf(out,
                    "            o.%.*s = r.GetInt(row, %d);\n",
                    (int)fs.items[i].name.len, fs.items[i].name.str, ci);
            else
                dg_putf(out, "            o.%.*s = r.%s(row, %d);\n",
                        (int)fs.items[i].name.len, fs.items[i].name.str,
                        dg_getter(fs.items[i].kind), ci);
            ci++;
        }
    }
    dg_putf(out, "            int ci = %d;\n", nbase);
    for (int i = 0; i < fs.count; i++) {
        if (fs.items[i].kind != DF_NAV) continue;
        int FL = (int)fs.items[i].name.len;
        const char *FS = fs.items[i].name.str;
        int TL = (int)fs.items[i].type_name.len;
        const char *TS = fs.items[i].type_name.str;
        zan_ast_node_t *ndecl = dg_find_class(c->unit, fs.items[i].type_name);
        dg_fields_t nfs;
        memset(&nfs, 0, sizeof(nfs));
        dg_collect_fields(c->unit, ndecl, &nfs, 0);
        int nn = 0, pk = -1;
        for (int k = 0; k < nfs.count; k++) {
            if (nfs.items[k].kind == DF_NAV) continue;
            if (istr_is(nfs.items[k].name, "id")) pk = nn;
            nn++;
        }
        if (pk < 0) pk = 0;
        dg_putf(out,
            "            if (this.j_%.*s) {\n"
            "                if (!r.IsNull(row, ci + %d)) {\n"
            "                    %.*s nv = new %.*s();\n",
            FL, FS, pk, TL, TS, TL, TS);
        int nk = 0;
        for (int k = 0; k < nfs.count; k++) {
            if (nfs.items[k].kind == DF_NAV) continue;
            if (nfs.items[k].kind == DF_ENUM)
                /* enum casts are not parseable; int -> enum is implicit */
                dg_putf(out,
                    "                    nv.%.*s = r.GetInt(row, ci + %d);\n",
                    (int)nfs.items[k].name.len, nfs.items[k].name.str, nk);
            else
                dg_putf(out,
                    "                    nv.%.*s = r.%s(row, ci + %d);\n",
                    (int)nfs.items[k].name.len, nfs.items[k].name.str,
                    dg_getter(nfs.items[k].kind), nk);
            nk++;
        }
        dg_putf(out,
            "                    o.%.*s = nv;\n"
            "                }\n"
            "                ci = ci + %d;\n"
            "            }\n",
            FL, FS, nn);
    }
    dg_putf(out,
        "            outp.Add(o);\n"
        "            row = row + 1;\n"
        "        }\n"
        "        return outp;\n"
        "    }\n");
    dg_putf(out,
        "    List<string> ToListCol(string col) {\n"
        "        StringBuilder sb = new StringBuilder();\n"
        "        sb.Append(\"SELECT \" + col + \" FROM \" + this.tbl + \" t\");\n"
        "        if (this.w.Length > 0) { sb.Append(\" WHERE \" + this.w); }\n"
        "        if (this.gb.Length > 0) { sb.Append(\" GROUP BY \" + this.gb); }\n"
        "        if (this.h.Length > 0) { sb.Append(\" HAVING \" + this.h); }\n"
        "        if (this.ob.Length > 0) { sb.Append(\" ORDER BY \" + this.ob); }\n"
        "        DbResult r = this.db.Query(sb.ToString(), this.ps);\n"
        "        List<string> outp = new List<string>();\n"
        "        int row = 0;\n"
        "        while (row < r.RowCount()) { outp.Add(r.GetString(row, 0)); row = row + 1; }\n"
        "        return outp;\n"
        "    }\n");
    dg_putf(out,
        "    DbResult ToListDtoR(string cols) {\n"
        "        StringBuilder sb = new StringBuilder();\n"
        "        sb.Append(\"SELECT \" + cols + \" FROM \" + this.tbl + \" t\");\n"
        "        if (this.w.Length > 0) { sb.Append(\" WHERE \" + this.w); }\n"
        "        if (this.gb.Length > 0) { sb.Append(\" GROUP BY \" + this.gb); }\n"
        "        if (this.h.Length > 0) { sb.Append(\" HAVING \" + this.h); }\n"
        "        if (this.ob.Length > 0) { sb.Append(\" ORDER BY \" + this.ob); }\n"
        "        return this.db.Query(sb.ToString(), this.ps);\n"
        "    }\n");
    dg_putf(out,
        "    %.*s First() {\n"
        "        this.limN = 1;\n"
        "        List<%.*s> l = ToList();\n"
        "        if (l.Count > 0) { return l[0]; }\n"
        "        return null;\n"
        "    }\n"
        "    %.*s FirstOrDefault() { return First(); }\n"
        "    %.*s ToOne() { return First(); }\n"
        "    int Count() {\n"
        "        if (this.gb.Length > 0) {\n"
        "            StringBuilder sb = new StringBuilder();\n"
        "            sb.Append(\"SELECT COUNT(*) FROM (SELECT 1 FROM \" + "
        "this.tbl + \" t\");\n"
        "            if (this.w.Length > 0) { sb.Append(\" WHERE \" + "
        "this.w); }\n"
        "            sb.Append(\" GROUP BY \" + this.gb);\n"
        "            if (this.h.Length > 0) { sb.Append(\" HAVING \" + "
        "this.h); }\n"
        "            sb.Append(\")\");\n"
        "            DbResult r = this.db.Query(sb.ToString(), this.ps);\n"
        "            if (r.RowCount() == 0) { return 0; }\n"
        "            return r.GetInt(0, 0);\n"
        "        }\n"
        "        return AggI(\"COUNT(*)\");\n"
        "    }\n"
        "    bool Any() { return Count() > 0; }\n"
        "    DbResult AggR(string expr) {\n"
        "        StringBuilder sb = new StringBuilder();\n"
        "        sb.Append(\"SELECT \" + expr + \" FROM \" + this.tbl + \" t\");\n"
        "        if (this.w.Length > 0) { sb.Append(\" WHERE \"); "
        "sb.Append(this.w); }\n"
        "        if (this.gb.Length > 0) { sb.Append(\" GROUP BY \"); "
        "sb.Append(this.gb); }\n"
        "        if (this.h.Length > 0) { sb.Append(\" HAVING \"); "
        "sb.Append(this.h); }\n"
        "        return this.db.Query(sb.ToString(), this.ps);\n"
        "    }\n"
        "    int AggI(string expr) {\n"
        "        DbResult r = AggR(expr);\n"
        "        if (r.RowCount() == 0) { return 0; }\n"
        "        return r.GetInt(0, 0);\n"
        "    }\n"
        "    double AggD(string expr) {\n"
        "        DbResult r = AggR(expr);\n"
        "        if (r.RowCount() == 0) { return 0.0; }\n"
        "        return r.GetDouble(0, 0);\n"
        "    }\n"
        "}\n",
        CL, CS, CL, CS, CL, CS, CL, CS);
}


/* ---- write-side code generation (Insert / Update / Delete / CodeFirst) ---- */

/* Column kind code understood by `__DbBind.Ty` (DDL type mapping). */
static int dg_ty_code(dg_fk_t k) {
    switch (k) {
    case DF_DOUBLE: return 1;
    case DF_BOOL: return 2;
    case DF_STRING: return 3;
    default: return 0; /* int, enum */
    }
}

/* Appends `o.<field>` as a bound parameter of the right type. */
static void dg_bind_field(dg_buf_t *out, const char *ind, const dg_field_t *f,
                          const char *obj) {
    int FL = (int)f->name.len;
    const char *FS = f->name.str;
    switch (f->kind) {
    case DF_INT:
        dg_putf(out, "%sps.AddInt(%s.%.*s);\n", ind, obj, FL, FS);
        break;
    case DF_ENUM:
        /* enum -> int is implicit; a temp keeps it out of an expression */
        dg_putf(out, "%sint ev_%.*s = %s.%.*s;\n%sps.AddInt(ev_%.*s);\n", ind,
                FL, FS, obj, FL, FS, ind, FL, FS);
        break;
    case DF_DOUBLE:
        dg_putf(out, "%sps.AddDouble(%s.%.*s);\n", ind, obj, FL, FS);
        break;
    case DF_BOOL:
        dg_putf(out,
                "%sif (%s.%.*s) { ps.AddInt(1); } else { ps.AddInt(0); }\n",
                ind, obj, FL, FS);
        break;
    default:
        dg_putf(out, "%sps.Add(%s.%.*s);\n", ind, obj, FL, FS);
        break;
    }
}

/* `<Entity>Cols`: the column names as constants, so dynamic (DbValues) code
 * gets autocompletion and a compile-time check instead of bare strings. */
static void dg_gen_cols(dg_ctx_t *c, dg_buf_t *out, zan_istr_t cls,
                        const dg_fields_t *fs) {
    (void)c;
    int CL = (int)cls.len;
    const char *CS = cls.str;
    dg_putf(out, "class %.*sCols {\n", CL, CS);
    for (int i = 0; i < fs->count; i++) {
        const dg_field_t *f = &fs->items[i];
        if (f->kind == DF_NAV) continue;
        dg_putf(out, "    static string %.*s = \"%.*s\";\n", (int)f->name.len,
                f->name.str, (int)f->col.len, f->col.str);
    }
    dg_putf(out, "    static bool Has(string c) {\n        return false");
    for (int i = 0; i < fs->count; i++) {
        const dg_field_t *f = &fs->items[i];
        if (f->kind == DF_NAV) continue;
        dg_putf(out, "\n            || c == \"%.*s\"", (int)f->col.len,
                f->col.str);
    }
    dg_putf(out, ";\n    }\n");
    /* the column's declared kind, so dynamic values are converted to it */
    dg_putf(out, "    static int Kind(string c) {\n");
    for (int i = 0; i < fs->count; i++) {
        const dg_field_t *f = &fs->items[i];
        if (f->kind == DF_NAV) continue;
        const char *k = (f->kind == DF_DOUBLE) ? "KindDouble"
                        : (f->kind == DF_STRING) ? "KindText"
                                                 : "KindInt";
        dg_putf(out, "        if (c == \"%.*s\") { return DbParams.%s; }\n",
                (int)f->col.len, f->col.str, k);
    }
    dg_putf(out, "        return DbParams.KindNull;\n    }\n");
    dg_putf(out,
        "    static string Require(string c) {\n"
        "        if (!%.*sCols.Has(c)) {\n"
        "            throw new Exception(\"%.*s has no column: \" + c);\n"
        "        }\n"
        "        return c;\n"
        "    }\n"
        "}\n",
        CL, CS, CL, CS);
}

static void dg_gen_insert(dg_ctx_t *c, dg_buf_t *out, zan_istr_t cls,
                          const dg_fields_t *fs, zan_istr_t tbl) {
    (void)c;
    int CL = (int)cls.len;
    const char *CS = cls.str;
    int TBL = (int)tbl.len;
    const char *TBS = tbl.str;
    dg_putf(out,
        "class __DbI_%.*s {\n"
        "    IDbExecutor db;\n"
        "    string tbl;\n"
        "    List<%.*s> rows;\n"
        "    List<DbValues> dicts;\n"
        "    List<string> only;\n"
        "    List<string> skip;\n"
        "    static __DbI_%.*s Create(IDbExecutor db) {\n"
        "        __DbI_%.*s q = new __DbI_%.*s();\n"
        "        q.db = db;\n"
        "        q.tbl = \"%.*s\";\n"
        "        q.rows = new List<%.*s>();\n"
        "        q.dicts = new List<DbValues>();\n"
        "        q.only = new List<string>();\n"
        "        q.skip = new List<string>();\n"
        "        return q;\n"
        "    }\n"
        "    static __DbI_%.*s Create(IDbExecutor db, %.*s o) {\n"
        "        return __DbI_%.*s.Create(db).AppendData(o);\n"
        "    }\n"
        "    static __DbI_%.*s Create(IDbExecutor db, List<%.*s> l) {\n"
        "        return __DbI_%.*s.Create(db).AppendData(l);\n"
        "    }\n"
        "    __DbI_%.*s AppendData(%.*s o) { this.rows.Add(o); return this; }\n"
        "    __DbI_%.*s AppendData(List<%.*s> l) {\n"
        "        int i = 0;\n"
        "        while (i < l.Count) { this.rows.Add(l[i]); i = i + 1; }\n"
        "        return this;\n"
        "    }\n"
        "    __DbI_%.*s AsTable(string t) { this.tbl = t; return this; }\n"
        "    __DbI_%.*s AppendDict(DbValues v) {\n"
        "        int i = 0;\n"
        "        while (i < v.Count()) { %.*sCols.Require(v.NameAt(i)); i = i + 1; }\n"
        "        this.dicts.Add(v);\n"
        "        return this;\n"
        "    }\n"
        "    __DbI_%.*s AppendDict(List<DbValues> l) {\n"
        "        int i = 0;\n"
        "        while (i < l.Count) { AppendDict(l[i]); i = i + 1; }\n"
        "        return this;\n"
        "    }\n"
        "    __DbI_%.*s Only(string c) { this.only.Add(c); return this; }\n"
        "    __DbI_%.*s Skip(string c) { this.skip.Add(c); return this; }\n"
        "    bool Use(string c, bool identity) {\n"
        "        if (this.only.Count > 0) { return __DbBind.Has(this.only, c); }\n"
        "        if (identity) { return false; }\n"
        "        return !__DbBind.Has(this.skip, c);\n"
        "    }\n",
        CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, TBL, TBS, CL, CS,
        CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS,
        CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS,
        CL, CS);

    dg_putf(out,
        "    int ExecuteDicts() {\n"
        "        DbParams ps = DbParams.Create();\n"
        "        DbValues head = this.dicts[0];\n"
        "        StringBuilder sb = new StringBuilder();\n"
        "        sb.Append(\"INSERT INTO \");\n"
        "        sb.Append(this.tbl);\n"
        "        sb.Append(\" (\");\n"
        "        int c = 0;\n"
        "        while (c < head.Count()) {\n"
        "            if (c > 0) { sb.Append(\", \"); }\n"
        "            sb.Append(head.NameAt(c));\n"
        "            c = c + 1;\n"
        "        }\n"
        "        sb.Append(\") VALUES \");\n"
        "        int i = 0;\n"
        "        while (i < this.dicts.Count) {\n"
        "            DbValues v = this.dicts[i];\n"
        "            if (i > 0) { sb.Append(\", \"); }\n"
        "            sb.Append(\"(\");\n"
        "            int k = 0;\n"
        "            while (k < head.Count()) {\n"
        "                if (k > 0) { sb.Append(\", \"); }\n"
        "                sb.Append(\"?\");\n"
        "                int at = v.IndexOf(head.NameAt(k));\n"
        "                if (at < 0) { ps.AddNull(); } else {\n"
        "                    v.BindAs(ps, at, %.*sCols.Kind(head.NameAt(k)));\n"
        "                }\n"
        "                k = k + 1;\n"
        "            }\n"
        "            sb.Append(\")\");\n"
        "            i = i + 1;\n"
        "        }\n"
        "        return this.db.Execute(sb.ToString(), ps);\n"
        "    }\n"
        "    int ExecuteAffrows() {\n"
        "        if (this.dicts.Count > 0) { return ExecuteDicts(); }\n"
        "        if (this.rows.Count == 0) { return 0; }\n"
        "        DbParams ps = DbParams.Create();\n"
        "        string cols = \"\";\n", CL, CS);
    for (int i = 0; i < fs->count; i++) {
        const dg_field_t *f = &fs->items[i];
        if (f->kind == DF_NAV) continue;
        dg_putf(out,
            "        if (this.Use(\"%.*s\", %s)) {\n"
            "            if (cols.Length > 0) { cols = cols + \", \"; }\n"
            "            cols = cols + \"%.*s\";\n"
            "        }\n",
            (int)f->col.len, f->col.str, f->is_ident ? "true" : "false",
            (int)f->col.len, f->col.str);
    }
    dg_putf(out,
        "        if (cols.Length == 0) { return 0; }\n"
        "        StringBuilder sb = new StringBuilder();\n"
        "        sb.Append(\"INSERT INTO \");\n"
        "        sb.Append(this.tbl);\n"
        "        sb.Append(\" (\");\n"
        "        sb.Append(cols);\n"
        "        sb.Append(\") VALUES \");\n"
        "        int i = 0;\n"
        "        while (i < this.rows.Count) {\n"
        "            %.*s o = this.rows[i];\n"
        "            if (i > 0) { sb.Append(\", \"); }\n"
        "            string marks = \"\";\n",
        CL, CS);
    for (int i = 0; i < fs->count; i++) {
        const dg_field_t *f = &fs->items[i];
        if (f->kind == DF_NAV) continue;
        dg_putf(out,
            "            if (this.Use(\"%.*s\", %s)) {\n"
            "                if (marks.Length > 0) { marks = marks + \", \"; }\n"
            "                marks = marks + \"?\";\n",
            (int)f->col.len, f->col.str, f->is_ident ? "true" : "false");
        dg_bind_field(out, "                ", f, "o");
        dg_putf(out, "            }\n");
    }
    dg_putf(out,
        "            sb.Append(\"(\");\n"
        "            sb.Append(marks);\n"
        "            sb.Append(\")\");\n"
        "            i = i + 1;\n"
        "        }\n"
        "        return this.db.Execute(sb.ToString(), ps);\n"
        "    }\n"
        "    int ExecuteIdentity() {\n"
        "        if (ExecuteAffrows() == 0) { return 0; }\n"
        "        return __DbBind.LastId(this.db);\n"
        "    }\n"
        "}\n");
}

static void dg_gen_update(dg_ctx_t *c, dg_buf_t *out, zan_istr_t cls,
                          const dg_fields_t *fs, zan_istr_t tbl) {
    (void)c;
    int CL = (int)cls.len;
    const char *CS = cls.str;
    int TBL = (int)tbl.len;
    const char *TBS = tbl.str;
    const dg_field_t *pk = NULL;
    for (int i = 0; i < fs->count; i++)
        if (fs->items[i].is_pk && fs->items[i].kind != DF_NAV)
            pk = &fs->items[i];
    dg_putf(out,
        "class __DbU_%.*s {\n"
        "    IDbExecutor db;\n"
        "    string tbl;\n"
        "    %.*s src;\n"
        "    List<string> only;\n"
        "    List<string> skip;\n"
        "    string sets;\n"
        "    DbParams sp;\n"
        "    string w;\n"
        "    DbParams wp;\n"
        "    bool on;\n"
        "    static __DbU_%.*s Create(IDbExecutor db) {\n"
        "        __DbU_%.*s q = new __DbU_%.*s();\n"
        "        q.db = db;\n"
        "        q.tbl = \"%.*s\";\n"
        "        q.only = new List<string>();\n"
        "        q.skip = new List<string>();\n"
        "        q.sets = \"\";\n"
        "        q.sp = DbParams.Create();\n"
        "        q.w = \"\";\n"
        "        q.wp = DbParams.Create();\n"
        "        q.on = true;\n"
        "        return q;\n"
        "    }\n"
        "    __DbU_%.*s AsTable(string t) { this.tbl = t; return this; }\n"
        "    __DbU_%.*s Only(string c) { this.only.Add(c); return this; }\n"
        "    __DbU_%.*s Skip(string c) { this.skip.Add(c); return this; }\n"
        "    bool Use(string c) {\n"
        "        if (this.only.Count > 0) { return __DbBind.Has(this.only, c); }\n"
        "        return !__DbBind.Has(this.skip, c);\n"
        "    }\n"
        "    __DbU_%.*s SetDict(DbValues v) {\n"
        "        int i = 0;\n"
        "        while (i < v.Count()) {\n"
        "            string c = %.*sCols.Require(v.NameAt(i));\n"
        "            if (Use(c)) {\n"
        "                v.BindAs(this.sp, i, %.*sCols.Kind(c));\n"
        "                Frag(c + \" = ?\");\n"
        "            }\n"
        "            i = i + 1;\n"
        "        }\n"
        "        return this;\n"
        "    }\n"
        "    __DbU_%.*s WhereDict(DbValues v) {\n"
        "        if (!this.on) { return this; }\n"
        "        int i = 0;\n"
        "        while (i < v.Count()) {\n"
        "            string c = %.*sCols.Require(v.NameAt(i));\n"
        "            W(c + \" = ?\");\n"
        "            v.BindAs(this.wp, i, %.*sCols.Kind(c));\n"
        "            i = i + 1;\n"
        "        }\n"
        "        return this;\n"
        "    }\n"
        "    __DbU_%.*s CondBegin(bool c) { this.on = c; return this; }\n"
        "    __DbU_%.*s CondEnd() { this.on = true; return this; }\n"
        "    __DbU_%.*s W(string f) {\n"
        "        if (!this.on) { return this; }\n"
        "        string u = __DbBind.Unalias(f);\n"
        "        if (this.w.Length == 0) { this.w = u; }\n"
        "        else { this.w = this.w + \" AND \" + u; }\n"
        "        return this;\n"
        "    }\n"
        "    __DbU_%.*s P(string v) {\n"
        "        if (this.on) { this.wp.Add(v); }\n"
        "        return this;\n"
        "    }\n"
        "    __DbU_%.*s Pi(int v) {\n"
        "        if (this.on) { this.wp.AddInt(v); }\n"
        "        return this;\n"
        "    }\n"
        "    __DbU_%.*s Pd(double v) {\n"
        "        if (this.on) { this.wp.AddDouble(v); }\n"
        "        return this;\n"
        "    }\n"
        "    __DbU_%.*s InI(List<int> vs) {\n"
        "        if (!this.on) { return this; }\n"
        "        int i = 0;\n"
        "        while (i < vs.Count) { this.wp.AddInt(vs[i]); i = i + 1; }\n"
        "        return this;\n"
        "    }\n"
        "    __DbU_%.*s InS(List<string> vs) {\n"
        "        if (!this.on) { return this; }\n"
        "        int i = 0;\n"
        "        while (i < vs.Count) { this.wp.Add(vs[i]); i = i + 1; }\n"
        "        return this;\n"
        "    }\n"
        "    __DbU_%.*s InD(List<double> vs) {\n"
        "        if (!this.on) { return this; }\n"
        "        int i = 0;\n"
        "        while (i < vs.Count) { this.wp.AddDouble(vs[i]); i = i + 1; }\n"
        "        return this;\n"
        "    }\n"
        "    __DbU_%.*s Frag(string f) {\n"
        "        if (this.sets.Length > 0) { this.sets = this.sets + \", \"; }\n"
        "        this.sets = this.sets + f;\n"
        "        return this;\n"
        "    }\n"
        "    __DbU_%.*s SetI(string c, int v) {\n"
        "        this.sp.AddInt(v);\n"
        "        return Frag(c + \" = ?\");\n"
        "    }\n"
        "    __DbU_%.*s SetD(string c, double v) {\n"
        "        this.sp.AddDouble(v);\n"
        "        return Frag(c + \" = ?\");\n"
        "    }\n"
        "    __DbU_%.*s SetS(string c, string v) {\n"
        "        this.sp.Add(v);\n"
        "        return Frag(c + \" = ?\");\n"
        "    }\n"
        "    __DbU_%.*s SetB(string c, bool v) {\n"
        "        if (v) { this.sp.AddInt(1); } else { this.sp.AddInt(0); }\n"
        "        return Frag(c + \" = ?\");\n"
        "    }\n"
        "    __DbU_%.*s SetIncrI(string c, int v) {\n"
        "        this.sp.AddInt(v);\n"
        "        return Frag(c + \" = \" + c + \" + ?\");\n"
        "    }\n"
        "    __DbU_%.*s SetIncrD(string c, double v) {\n"
        "        this.sp.AddDouble(v);\n"
        "        return Frag(c + \" = \" + c + \" + ?\");\n"
        "    }\n",
        CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, TBL, TBS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS,
        CL, CS, CL, CS);

    /* SetSource records the entity; the columns are expanded at execute
     * time so UpdateColumns/IgnoreColumns work in any chain order. */
    dg_putf(out,
        "    __DbU_%.*s SetSource(%.*s o) { this.src = o; return this; }\n"
        "    void ApplySource() {\n"
        "        if (this.src == null) { return; }\n"
        "        %.*s o = this.src;\n"
        "        this.src = null;\n",
        CL, CS, CL, CS, CL, CS);
    if (!pk) {
        /* no primary key: nothing to key the row on */
        dg_putf(out, "    }\n");
    } else {
        for (int i = 0; i < fs->count; i++) {
            const dg_field_t *f = &fs->items[i];
            if (f->kind == DF_NAV || f->is_pk) continue;
            int L = (int)f->col.len;
            const char *S = f->col.str;
            int NL = (int)f->name.len;
            const char *NS = f->name.str;
            dg_putf(out, "        if (Use(\"%.*s\")) {\n", L, S);
            switch (f->kind) {
            case DF_INT:
                dg_putf(out, "            SetI(\"%.*s\", o.%.*s);\n", L, S, NL, NS);
                break;
            case DF_ENUM:
                dg_putf(out,
                        "            int ev_%.*s = o.%.*s;\n"
                        "            SetI(\"%.*s\", ev_%.*s);\n",
                        NL, NS, NL, NS, L, S, NL, NS);
                break;
            case DF_DOUBLE:
                dg_putf(out, "            SetD(\"%.*s\", o.%.*s);\n", L, S, NL, NS);
                break;
            case DF_BOOL:
                dg_putf(out, "            SetB(\"%.*s\", o.%.*s);\n", L, S, NL, NS);
                break;
            default:
                dg_putf(out, "            SetS(\"%.*s\", o.%.*s);\n", L, S, NL, NS);
                break;
            }
            dg_putf(out, "        }\n");
        }
        const char *pm = pk->kind == DF_STRING   ? "P"
                         : pk->kind == DF_DOUBLE ? "Pd"
                                                 : "Pi";
        dg_putf(out,
            "        W(\"%.*s = ?\");\n"
            "        %s(o.%.*s);\n"
            "    }\n",
            (int)pk->col.len, pk->col.str, pm, (int)pk->name.len,
            pk->name.str);
    }

    dg_putf(out,
        "    int ExecuteAffrows() {\n"
        "        ApplySource();\n"
        "        if (this.sets.Length == 0) { return 0; }\n"
        "        if (this.w.Length == 0) { return 0; }\n"
        "        string sql = \"UPDATE \" + this.tbl + \" SET \" + this.sets\n"
        "            + \" WHERE \" + this.w;\n"
        "        return this.db.Execute(sql, __DbBind.Cat(this.sp, this.wp));\n"
        "    }\n"
        "}\n");
}

static void dg_gen_delete(dg_ctx_t *c, dg_buf_t *out, zan_istr_t cls,
                          zan_istr_t tbl) {
    (void)c;
    int CL = (int)cls.len;
    const char *CS = cls.str;
    int TBL = (int)tbl.len;
    const char *TBS = tbl.str;
    dg_putf(out,
        "class __DbD_%.*s {\n"
        "    IDbExecutor db;\n"
        "    string tbl;\n"
        "    string w;\n"
        "    DbParams ps;\n"
        "    bool on;\n"
        "    static __DbD_%.*s Create(IDbExecutor db) {\n"
        "        __DbD_%.*s q = new __DbD_%.*s();\n"
        "        q.db = db;\n"
        "        q.tbl = \"%.*s\";\n"
        "        q.w = \"\";\n"
        "        q.ps = DbParams.Create();\n"
        "        q.on = true;\n"
        "        return q;\n"
        "    }\n"
        "    __DbD_%.*s AsTable(string t) { this.tbl = t; return this; }\n"
        "    __DbD_%.*s CondBegin(bool c) { this.on = c; return this; }\n"
        "    __DbD_%.*s CondEnd() { this.on = true; return this; }\n"
        "    __DbD_%.*s W(string f) {\n"
        "        if (!this.on) { return this; }\n"
        "        string u = __DbBind.Unalias(f);\n"
        "        if (this.w.Length == 0) { this.w = u; }\n"
        "        else { this.w = this.w + \" AND \" + u; }\n"
        "        return this;\n"
        "    }\n"
        "    __DbD_%.*s WhereDict(DbValues v) {\n"
        "        if (!this.on) { return this; }\n"
        "        int i = 0;\n"
        "        while (i < v.Count()) {\n"
        "            string c = %.*sCols.Require(v.NameAt(i));\n"
        "            W(c + \" = ?\");\n"
        "            v.BindAs(this.ps, i, %.*sCols.Kind(c));\n"
        "            i = i + 1;\n"
        "        }\n"
        "        return this;\n"
        "    }\n"
        "    __DbD_%.*s P(string v) {\n"
        "        if (this.on) { this.ps.Add(v); }\n"
        "        return this;\n"
        "    }\n"
        "    __DbD_%.*s Pi(int v) {\n"
        "        if (this.on) { this.ps.AddInt(v); }\n"
        "        return this;\n"
        "    }\n"
        "    __DbD_%.*s Pd(double v) {\n"
        "        if (this.on) { this.ps.AddDouble(v); }\n"
        "        return this;\n"
        "    }\n"
        "    __DbD_%.*s InI(List<int> vs) {\n"
        "        if (!this.on) { return this; }\n"
        "        int i = 0;\n"
        "        while (i < vs.Count) { this.ps.AddInt(vs[i]); i = i + 1; }\n"
        "        return this;\n"
        "    }\n"
        "    __DbD_%.*s InS(List<string> vs) {\n"
        "        if (!this.on) { return this; }\n"
        "        int i = 0;\n"
        "        while (i < vs.Count) { this.ps.Add(vs[i]); i = i + 1; }\n"
        "        return this;\n"
        "    }\n"
        "    __DbD_%.*s InD(List<double> vs) {\n"
        "        if (!this.on) { return this; }\n"
        "        int i = 0;\n"
        "        while (i < vs.Count) { this.ps.AddDouble(vs[i]); i = i + 1; }\n"
        "        return this;\n"
        "    }\n"
        "    int ExecuteAffrows() {\n"
        "        if (this.w.Length == 0) { return 0; }\n"
        "        string sql = \"DELETE FROM \" + this.tbl + \" WHERE \" + this.w;\n"
        "        return this.db.Execute(sql, this.ps);\n"
        "    }\n"
        "}\n",
        CL, CS, CL, CS, CL, CS, CL, CS, TBL, TBS, CL, CS, CL, CS, CL, CS,
        CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS,
        CL, CS, CL, CS,
        CL, CS);
}

/* CodeFirst: creates the table when missing, otherwise adds columns the
 * entity gained since the table was created (never drops or retypes). */
static void dg_gen_codefirst(dg_ctx_t *c, dg_buf_t *out, zan_istr_t cls,
                             const dg_fields_t *fs, zan_istr_t tbl) {
    (void)c;
    int CL = (int)cls.len;
    const char *CS = cls.str;
    int TBL = (int)tbl.len;
    const char *TBS = tbl.str;
    dg_putf(out,
        "class __DbCF_%.*s {\n"
        "    static void Sync(IDbExecutor db) {\n"
        "        int p = db.GetProvider();\n"
        "        string t = \"%.*s\";\n"
        "        if (!__DbBind.HasTable(db, t)) {\n"
        "            string ddl = \"CREATE TABLE \" + t + \" (\";\n",
        CL, CS, TBL, TBS);
    bool first = true;
    for (int i = 0; i < fs->count; i++) {
        const dg_field_t *f = &fs->items[i];
        if (f->kind == DF_NAV) continue;
        dg_putf(out,
            "            ddl = ddl + \"%s%.*s \" + __DbBind.Ty(p, %d, %s, %s, %d);\n",
            first ? "" : ", ", (int)f->col.len, f->col.str,
            dg_ty_code(f->kind), f->is_pk ? "true" : "false",
            f->is_ident ? "true" : "false", f->str_len);
        first = false;
    }
    dg_putf(out,
        "            ddl = ddl + \")\";\n"
        "            db.Execute(ddl);\n"
        "            return;\n"
        "        }\n"
        "        List<string> have = __DbBind.Cols(db, t);\n");
    for (int i = 0; i < fs->count; i++) {
        const dg_field_t *f = &fs->items[i];
        if (f->kind == DF_NAV) continue;
        dg_putf(out,
            "        if (!__DbBind.Has(have, \"%.*s\")) {\n"
            "            db.Execute(\"ALTER TABLE \" + t + \" ADD COLUMN %.*s \"\n"
            "                + __DbBind.Ty(p, %d, false, false, %d));\n"
            "        }\n",
            (int)f->col.len, f->col.str, (int)f->col.len, f->col.str,
            dg_ty_code(f->kind), f->str_len);
    }
    dg_putf(out, "    }\n}\n");
}

/* ---- entry point ---- */

void zan_dbgen_run(zan_ast_node_t *unit, zan_arena_t *arena,
                   zan_diag_t *diag) {
    dg_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.unit = unit;
    c.arena = arena;
    c.diag = diag;

    dg_collect_expr_slots(&c);

    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL) continue;
        for (int m = 0; m < d->type_decl.members.count; m++) {
            zan_ast_node_t *mem = d->type_decl.members.items[m];
            if (mem->kind == AST_METHOD_DECL ||
                mem->kind == AST_CONSTRUCTOR_DECL)
                dg_visit_expr(&c, mem->method_decl.body);
            else if (mem->kind == AST_FIELD_DECL)
                dg_visit_expr(&c, mem->field_decl.initializer);
        }
    }

    if (!c.any || zan_diag_has_errors(diag)) return;

    if (c.sync_all) {
        for (int i = 0; i < unit->comp_unit.decls.count; i++) {
            zan_ast_node_t *d = unit->comp_unit.decls.items[i];
            if (d->kind == AST_CLASS_DECL && dg_is_table_entity(unit, d->type_decl.name)) {
                dg_need_add(&c.need, d->type_decl.name);
            }
        }
    }

    /* Entities first: generating one may pull navigation entities into the
     * worklist, and __DbBind needs an entry for every entity in it. */
    dg_buf_t ents;
    memset(&ents, 0, sizeof(ents));
    for (int i = 0; i < c.need.count; i++) {
        zan_istr_t cls = c.need.names[i];
        dg_fields_t fs;
        memset(&fs, 0, sizeof(fs));
        dg_collect_fields(unit, dg_find_class(unit, cls), &fs, 0);
        zan_istr_t tbl = dg_table_of(unit, cls);
        dg_gen_cols(&c, &ents, cls, &fs);
        dg_gen_entity(&c, &ents, cls);
        dg_gen_insert(&c, &ents, cls, &fs, tbl);
        dg_gen_update(&c, &ents, cls, &fs, tbl);
        dg_gen_delete(&c, &ents, cls, tbl);
        dg_gen_codefirst(&c, &ents, cls, &fs, tbl);
    }

    dg_buf_t out;
    memset(&out, 0, sizeof(out));
    dg_putf(&out, "class __DbBind {\n");
    for (int i = 0; i < c.need.count; i++) {
        zan_istr_t cls = c.need.names[i];
        int L = (int)cls.len;
        const char *S = cls.str;
        dg_putf(&out,
            "    static __DbQ_%.*s Q_%.*s(IDbExecutor db) { "
            "return __DbQ_%.*s.Create(db); }\n"
            "    static __DbI_%.*s I_%.*s(IDbExecutor db) { "
            "return __DbI_%.*s.Create(db); }\n"
            "    static __DbI_%.*s I_%.*s(IDbExecutor db, %.*s o) { "
            "return __DbI_%.*s.Create(db, o); }\n"
            "    static __DbI_%.*s I_%.*s(IDbExecutor db, List<%.*s> l) { "
            "return __DbI_%.*s.Create(db, l); }\n"
            "    static __DbU_%.*s U_%.*s(IDbExecutor db) { "
            "return __DbU_%.*s.Create(db); }\n"
            "    static __DbD_%.*s D_%.*s(IDbExecutor db) { "
            "return __DbD_%.*s.Create(db); }\n"
            "    static void CF_%.*s(IDbExecutor db) { "
            "__DbCF_%.*s.Sync(db); }\n",
            L, S, L, S, L, S, L, S, L, S, L, S, L, S, L, S, L, S, L, S,
            L, S, L, S, L, S, L, S, L, S, L, S, L, S, L, S, L, S, L, S,
            L, S, L, S);
    }
    if (c.sync_all) {
        dg_putf(&out, "    static void SyncAll(IDbExecutor db) {\n");
        for (int i = 0; i < c.need.count; i++) {
            zan_istr_t cls = c.need.names[i];
            dg_putf(&out, "        CF_%.*s(db);\n", (int)cls.len, cls.str);
        }
        dg_putf(&out, "    }\n");
    }
    dg_putf(&out,
        "    static string M(int n) {\n"
        "        StringBuilder sb = new StringBuilder();\n"
        "        int i = 0;\n"
        "        while (i < n) {\n"
        "            if (i > 0) { sb.Append(\", \"); }\n"
        "            sb.Append(\"?\");\n"
        "            i = i + 1;\n"
        "        }\n"
        "        if (sb.Length == 0) { sb.Append(\"NULL\"); }\n"
        "        return sb.ToString();\n"
        "    }\n"
        "    static bool Has(List<string> l, string v) {\n"
        "        int i = 0;\n"
        "        while (i < l.Count) {\n"
        "            if (l[i] == v) { return true; }\n"
        "            i = i + 1;\n"
        "        }\n"
        "        return false;\n"
        "    }\n"
        /* UPDATE/DELETE take no table alias in SQLite and friends, so the
         * `t.` the WHERE builder emits is dropped there. */
        "    static string Unalias(string f) { return f.Replace(\"t.\", \"\"); }\n"
        "    static DbParams Cat(DbParams a, DbParams b) {\n"
        "        DbParams r = DbParams.Create();\n"
        "        __DbBind.CopyInto(a, r);\n"
        "        __DbBind.CopyInto(b, r);\n"
        "        return r;\n"
        "    }\n"
        "    static void CopyInto(DbParams src, DbParams dst) {\n"
        "        int i = 0;\n"
        "        while (i < src.Count()) {\n"
        "            int k = src.KindAt(i);\n"
        "            if (k == DbParams.KindInt) { dst.AddInt(src.IntAt(i)); }\n"
        "            else if (k == DbParams.KindDouble) { "
        "dst.AddDouble(src.DoubleAt(i)); }\n"
        "            else if (k == DbParams.KindText) { dst.Add(src.TextAt(i)); }\n"
        "            else { dst.AddNull(); }\n"
        "            i = i + 1;\n"
        "        }\n"
        "    }\n"
        "    static int LastId(IDbExecutor db) {\n"
        "        int p = db.GetProvider();\n"
        "        string sql = \"SELECT last_insert_rowid()\";\n"
        "        if (p == DbProvider.MySQL || p == DbProvider.MariaDB) {\n"
        "            sql = \"SELECT LAST_INSERT_ID()\";\n"
        "        } else if (p == DbProvider.PostgreSQL "
        "|| p == DbProvider.OpenGauss || p == DbProvider.Kingbase) {\n"
        "            sql = \"SELECT lastval()\";\n"
        "        } else if (p == DbProvider.SqlServer || p == DbProvider.DM) {\n"
        "            sql = \"SELECT SCOPE_IDENTITY()\";\n"
        "        }\n"
        "        DbResult r = db.Query(sql);\n"
        "        if (r.RowCount() == 0) { return 0; }\n"
        "        return r.GetInt(0, 0);\n"
        "    }\n"
        "    static bool HasTable(IDbExecutor db, string t) {\n"
        "        int p = db.GetProvider();\n"
        "        string sql = \"SELECT COUNT(*) FROM information_schema.tables"
        " WHERE table_name = ?\";\n"
        "        if (p == DbProvider.SQLite) {\n"
        "            sql = \"SELECT COUNT(*) FROM sqlite_master WHERE"
        " type = 'table' AND name = ?\";\n"
        "        }\n"
        "        DbResult r = db.Query(sql, DbParams.Create().Add(t));\n"
        "        if (r.RowCount() == 0) { return false; }\n"
        "        return r.GetInt(0, 0) > 0;\n"
        "    }\n"
        "    static List<string> Cols(IDbExecutor db, string t) {\n"
        "        List<string> cols = new List<string>();\n"
        "        int p = db.GetProvider();\n"
        "        DbResult r;\n"
        "        int nameCol = 0;\n"
        "        if (p == DbProvider.SQLite) {\n"
        "            r = db.Query(\"PRAGMA table_info(\" + t + \")\");\n"
        "            nameCol = 1;\n"
        "        } else {\n"
        "            r = db.Query(\"SELECT column_name FROM"
        " information_schema.columns WHERE table_name = ?\",\n"
        "                DbParams.Create().Add(t));\n"
        "        }\n"
        "        int i = 0;\n"
        "        while (i < r.RowCount()) {\n"
        "            cols.Add(r.GetString(i, nameCol));\n"
        "            i = i + 1;\n"
        "        }\n"
        "        return cols;\n"
        "    }\n"
        /* kind: 0 int/enum, 1 double, 2 bool, 3 string */
        "    static string Ty(int p, int kind, bool pk, bool ident, int len) {\n"
        "        bool my = p == DbProvider.MySQL || p == DbProvider.MariaDB;\n"
        "        bool pg = p == DbProvider.PostgreSQL"
        " || p == DbProvider.OpenGauss || p == DbProvider.Kingbase;\n"
        "        bool ms = p == DbProvider.SqlServer || p == DbProvider.DM;\n"
        "        if (len <= 0) { len = 255; }\n"
        "        if (ident) {\n"
        "            if (my) { return \"INT NOT NULL AUTO_INCREMENT PRIMARY KEY\"; }\n"
        "            if (pg) { return \"SERIAL PRIMARY KEY\"; }\n"
        "            if (ms) { return \"INT IDENTITY(1,1) PRIMARY KEY\"; }\n"
        "            return \"INTEGER PRIMARY KEY AUTOINCREMENT\";\n"
        "        }\n"
        "        string t = \"INTEGER\";\n"
        "        if (kind == 1) {\n"
        "            t = \"DOUBLE PRECISION\";\n"
        "            if (my) { t = \"DOUBLE\"; }\n"
        "            if (ms) { t = \"FLOAT\"; }\n"
        "            if (p == DbProvider.SQLite) { t = \"REAL\"; }\n"
        "        } else if (kind == 2) {\n"
        "            t = \"BOOLEAN\";\n"
        "            if (my) { t = \"TINYINT(1)\"; }\n"
        "            if (ms) { t = \"BIT\"; }\n"
        "            if (p == DbProvider.SQLite) { t = \"INTEGER\"; }\n"
        "        } else if (kind == 3) {\n"
        "            t = \"VARCHAR(\" + Convert.ToString(len) + \")\";\n"
        "            if (p == DbProvider.SQLite) { t = \"TEXT\"; }\n"
        "            if (ms) { t = \"NVARCHAR(\" + Convert.ToString(len) + \")\"; }\n"
        "        } else {\n"
        "            if (my || ms) { t = \"INT\"; }\n"
        "        }\n"
        "        if (pk) { t = t + \" PRIMARY KEY\"; }\n"
        "        return t;\n"
        "    }\n"
        "}\n");
    dg_putf(&out, "%.*s", (int)ents.len, ents.buf);
    free(ents.buf);

    /* ZAN_DBGEN_DUMP=1 prints the generated ORM source (debugging aid). */
    if (getenv("ZAN_DBGEN_DUMP")) fprintf(stderr, "%.*s\n", (int)out.len, out.buf);

    char *gsrc = zan_arena_strdup(arena, out.buf, out.len);
    size_t glen = out.len;
    free(out.buf);

    zan_lexer_t lex;
    zan_lexer_init(&lex, gsrc, glen, 0, arena, diag);
    zan_parser_t gp;
    zan_parser_init(&gp, &lex, arena, diag);
    zan_ast_node_t *gu = zan_parser_parse(&gp);
    if (!gu) return;
    for (int i = 0; i < gu->comp_unit.decls.count; i++)
        zan_ast_list_push(&unit->comp_unit.decls, gu->comp_unit.decls.items[i],
                          arena);
}
