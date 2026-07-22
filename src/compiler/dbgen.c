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
        f->name = m->field_decl.name;
        f->kind = dg_classify(unit, m->field_decl.type);
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

typedef struct {
    zan_ast_node_t *unit;
    zan_arena_t *arena;
    zan_diag_t *diag;
    dg_need_t need;
    bool any;
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

/* Emits `?` and records the bind for a scalar comparison value. */
static void wf_value(dg_where_t *w, const dg_field_t *f, zan_ast_node_t *e) {
    wf_text(w, "?");
    switch (f->kind) {
    case DF_INT:
        wf_bind(w, BK_INT, e);
        break;
    case DF_ENUM:
        wf_bind(w, BK_INT, nb_cast_int(w->c, e->loc, e));
        break;
    case DF_DOUBLE:
        wf_bind(w, BK_DBL, e);
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

static void dg_where_cond(dg_where_t *w, zan_ast_node_t *e);

/* p.f.Method(arg) conditions: Like/NotLike/Contains/StartsWith/EndsWith/
 * In/NotIn. */
static bool dg_where_method(dg_where_t *w, zan_ast_node_t *e) {
    if (e->kind != AST_CALL) return false;
    zan_ast_node_t *callee = e->call.callee;
    if (!callee || callee->kind != AST_MEMBER_ACCESS) return false;
    const dg_field_t *col = dg_as_column(w, callee->member.object);
    if (!col) return false;
    zan_istr_t mn = callee->member.name;
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
    dg_ctx_t *c = w->c;
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
    wf_err(w, e->loc, "unsupported column method (expected Like/NotLike/"
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

/* Walks a fluent chain receiver down to a rewritten `__DbBind.Q_<T>(db)`
 * root and returns the entity name (len 0 when not a db query chain). */
static zan_istr_t dg_chain_entity(zan_ast_node_t *recv) {
    zan_istr_t none = { NULL, 0 };
    while (recv && recv->kind == AST_CALL) {
        zan_ast_node_t *callee = recv->call.callee;
        if (!callee || callee->kind != AST_MEMBER_ACCESS) return none;
        zan_ast_node_t *obj = callee->member.object;
        if (obj && obj->kind == AST_IDENTIFIER &&
            istr_is(obj->ident.name, "__DbBind") &&
            callee->member.name.len > 2 &&
            memcmp(callee->member.name.str, "Q_", 2) == 0) {
            zan_istr_t r = { callee->member.name.str + 2,
                             callee->member.name.len - 2 };
            return r;
        }
        recv = obj;
    }
    return none;
}

/* True when `call` is `<recv>.Query<T>()` with a known entity class T. */
static bool dg_is_query_root(dg_ctx_t *c, zan_ast_node_t *call,
                             zan_istr_t *cls_out) {
    if (call->kind != AST_CALL) return false;
    zan_ast_node_t *callee = call->call.callee;
    if (!callee || callee->kind != AST_MEMBER_ACCESS) return false;
    if (!istr_is(callee->member.name, "Query")) return false;
    if (call->call.type_args.count != 1 || call->call.args.count != 0)
        return false;
    zan_ast_node_t *targ = call->call.type_args.items[0];
    if (!targ || targ->kind != AST_TYPE_REF || targ->type_ref.is_array ||
        targ->type_ref.type_args.count > 0)
        return false;
    if (!dg_find_class(c->unit, targ->type_ref.name)) return false;
    *cls_out = targ->type_ref.name;
    return true;
}

static void dg_rewrite_query_root(dg_ctx_t *c, zan_ast_node_t *call,
                                  zan_istr_t cls) {
    dg_need_add(&c->need, cls);
    c->any = true;
    zan_ast_node_t *callee = call->call.callee;
    zan_ast_node_t *recv = callee->member.object;
    char mname[160];
    snprintf(mname, sizeof(mname), "Q_%.*s", (int)cls.len, cls.str);
    callee->member.object = nb_ident(c, call->loc, "__DbBind");
    callee->member.name = dg_istr(c, mname, strlen(mname));
    call->call.type_args.count = 0;
    zan_ast_list_init(&call->call.args);
    zan_ast_list_push(&call->call.args, recv, c->arena);
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

static void dg_rewrite_where(dg_ctx_t *c, zan_ast_node_t *call,
                             zan_istr_t cls) {
    zan_ast_node_t *lambda = call->call.args.items[0];
    if (lambda->kind != AST_LAMBDA || lambda->lambda.params.count != 1) {
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "db.Query<T>().Where expects a one-parameter lambda");
        return;
    }
    zan_ast_node_t *body = lambda->lambda.body;
    if (body && body->kind == AST_BLOCK) {
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "Where lambda must be an expression (no block body)");
        return;
    }
    dg_fields_t fields;
    memset(&fields, 0, sizeof(fields));
    dg_collect_fields(c->unit, dg_find_class(c->unit, cls), &fields, 0);

    dg_where_t w;
    memset(&w, 0, sizeof(w));
    w.c = c;
    w.pname = lambda->lambda.params.items[0]->param.name;
    w.fields = &fields;
    w.frag.loc = call->loc;
    w.ok = true;
    dg_where_cond(&w, body);
    if (!w.ok) {
        free(w.frag.lit.buf);
        return;
    }
    wf_flush(&w);
    zan_ast_node_t *frag = w.frag.expr;
    free(w.frag.lit.buf);
    if (!frag) frag = nb_strlit(c, call->loc, "1 = 1", 5);

    zan_ast_node_t *recv = call->call.callee->member.object;
    zan_ast_node_t *node = nb_call1(
        c, call->loc, nb_member(c, call->loc, recv, dg_istr(c, "W", 1)), frag);
    for (int i = 0; i < w.bind_count; i++) {
        const char *mn = dg_bind_method(w.binds[i].kind);
        node = nb_call1(c, call->loc,
                        nb_member(c, call->loc, node,
                                  dg_istr(c, mn, strlen(mn))),
                        w.binds[i].expr);
    }
    *call = *node;
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
    if (dg_is_query_root(c, e, &cls)) {
        dg_rewrite_query_root(c, e, cls);
        return;
    }
    zan_ast_node_t *callee = e->call.callee;
    if (!callee || callee->kind != AST_MEMBER_ACCESS) return;
    zan_istr_t mn = callee->member.name;
    bool is_where = istr_is(mn, "Where");
    bool is_ob = istr_is(mn, "OrderBy");
    bool is_obd = istr_is(mn, "OrderByDescending");
    bool is_inc = istr_is(mn, "Include");
    if (!is_where && !is_ob && !is_obd && !is_inc) return;
    if (e->call.args.count != 1 ||
        e->call.args.items[0]->kind != AST_LAMBDA)
        return;
    zan_istr_t entity = dg_chain_entity(callee->member.object);
    if (entity.len == 0) return;
    if (is_where) dg_rewrite_where(c, e, entity);
    else if (is_inc) dg_rewrite_include(c, e, entity);
    else dg_rewrite_orderby(c, e, entity, is_obd);
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

    int nbase = 0;
    for (int i = 0; i < fs.count; i++)
        if (fs.items[i].kind != DF_NAV) nbase++;

    dg_putf(out, "class __DbQ_%.*s {\n", CL, CS);
    dg_putf(out,
        "    IDbExecutor db;\n"
        "    string w;\n"
        "    DbParams ps;\n"
        "    string ob;\n"
        "    int limN;\n"
        "    int offN;\n");
    for (int i = 0; i < fs.count; i++)
        if (fs.items[i].kind == DF_NAV)
            dg_putf(out, "    bool j_%.*s;\n", (int)fs.items[i].name.len,
                    fs.items[i].name.str);
    dg_putf(out,
        "    static __DbQ_%.*s Create(IDbExecutor db) {\n"
        "        __DbQ_%.*s q = new __DbQ_%.*s();\n"
        "        q.db = db;\n"
        "        q.w = \"\";\n"
        "        q.ps = DbParams.Create();\n"
        "        q.ob = \"\";\n"
        "        q.limN = -1;\n"
        "        q.offN = -1;\n"
        "        return q;\n"
        "    }\n",
        CL, CS, CL, CS, CL, CS);
    dg_putf(out,
        "    __DbQ_%.*s W(string f) {\n"
        "        if (this.w.Length == 0) { this.w = f; }\n"
        "        else { this.w = this.w + \" AND \" + f; }\n"
        "        return this;\n"
        "    }\n"
        "    __DbQ_%.*s P(string v) { this.ps.Add(v); return this; }\n"
        "    __DbQ_%.*s Pi(int v) { this.ps.AddInt(v); return this; }\n"
        "    __DbQ_%.*s Pd(double v) { this.ps.AddDouble(v); return this; }\n"
        "    __DbQ_%.*s InI(List<int> vs) {\n"
        "        int i = 0;\n"
        "        while (i < vs.Count) { this.ps.AddInt(vs[i]); i = i + 1; }\n"
        "        return this;\n"
        "    }\n"
        "    __DbQ_%.*s InS(List<string> vs) {\n"
        "        int i = 0;\n"
        "        while (i < vs.Count) { this.ps.Add(vs[i]); i = i + 1; }\n"
        "        return this;\n"
        "    }\n"
        "    __DbQ_%.*s InD(List<double> vs) {\n"
        "        int i = 0;\n"
        "        while (i < vs.Count) { this.ps.AddDouble(vs[i]); i = i + 1; }\n"
        "        return this;\n"
        "    }\n"
        "    __DbQ_%.*s OB(string col) {\n"
        "        if (this.ob.Length == 0) { this.ob = col; }\n"
        "        else { this.ob = this.ob + \", \" + col; }\n"
        "        return this;\n"
        "    }\n"
        "    __DbQ_%.*s OBD(string col) { return OB(col + \" DESC\"); }\n"
        "    __DbQ_%.*s Take(int n) { this.limN = n; return this; }\n"
        "    __DbQ_%.*s Skip(int n) { this.offN = n; return this; }\n",
        CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS,
        CL, CS, CL, CS, CL, CS);
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
        "        sb.Append(\"SELECT \");\n");
    {
        dg_buf_t cols;
        memset(&cols, 0, sizeof(cols));
        bool first = true;
        for (int i = 0; i < fs.count; i++) {
            if (fs.items[i].kind == DF_NAV) continue;
            dg_putf(&cols, "%st.%.*s", first ? "" : ", ",
                    (int)fs.items[i].name.len, fs.items[i].name.str);
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
                    (int)nfs.items[k].name.len, nfs.items[k].name.str);
        }
        dg_putf(out,
            "        if (this.j_%.*s) { sb.Append(\"%s\"); }\n",
            FL, FS, cols.buf ? cols.buf : "");
        free(cols.buf);
    }
    dg_putf(out, "        sb.Append(\" FROM %.*s t\");\n", CL, CS);
    for (int i = 0; i < fs.count; i++) {
        if (fs.items[i].kind != DF_NAV) continue;
        int FL = (int)fs.items[i].name.len;
        const char *FS = fs.items[i].name.str;
        int TL = (int)fs.items[i].type_name.len;
        const char *TS = fs.items[i].type_name.str;
        dg_putf(out,
            "        if (this.j_%.*s) { sb.Append(\" LEFT JOIN %.*s j_%.*s "
            "ON t.%.*sId = j_%.*s.id\"); }\n",
            FL, FS, TL, TS, FL, FS, FL, FS, FL, FS);
    }
    dg_putf(out,
        "        if (this.w.Length > 0) { sb.Append(\" WHERE \"); "
        "sb.Append(this.w); }\n"
        "        if (this.ob.Length > 0) { sb.Append(\" ORDER BY \"); "
        "sb.Append(this.ob); }\n"
        "        if (this.limN >= 0) { sb.Append(\" LIMIT \"); "
        "sb.Append(Convert.ToString(this.limN)); }\n"
        "        if (this.offN >= 0) { sb.Append(\" OFFSET \"); "
        "sb.Append(Convert.ToString(this.offN)); }\n"
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
                dg_putf(out,
                    "            o.%.*s = (%.*s)r.GetInt(row, %d);\n",
                    (int)fs.items[i].name.len, fs.items[i].name.str,
                    (int)fs.items[i].type_name.len, fs.items[i].type_name.str,
                    ci);
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
                dg_putf(out,
                    "                    nv.%.*s = (%.*s)r.GetInt(row, "
                    "ci + %d);\n",
                    (int)nfs.items[k].name.len, nfs.items[k].name.str,
                    (int)nfs.items[k].type_name.len,
                    nfs.items[k].type_name.str, nk);
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
        "    %.*s First() {\n"
        "        this.limN = 1;\n"
        "        List<%.*s> l = ToList();\n"
        "        if (l.Count > 0) { return l[0]; }\n"
        "        return null;\n"
        "    }\n"
        "    %.*s FirstOrDefault() { return First(); }\n"
        "    int Count() {\n"
        "        StringBuilder sb = new StringBuilder();\n"
        "        sb.Append(\"SELECT COUNT(*) FROM %.*s t\");\n"
        "        if (this.w.Length > 0) { sb.Append(\" WHERE \"); "
        "sb.Append(this.w); }\n"
        "        DbResult r = this.db.Query(sb.ToString(), this.ps);\n"
        "        if (r.RowCount() == 0) { return 0; }\n"
        "        return r.GetInt(0, 0);\n"
        "    }\n"
        "}\n",
        CL, CS, CL, CS, CL, CS, CL, CS);
}

/* ---- entry point ---- */

void zan_dbgen_run(zan_ast_node_t *unit, zan_arena_t *arena,
                   zan_diag_t *diag) {
    dg_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.unit = unit;
    c.arena = arena;
    c.diag = diag;

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

    dg_buf_t out;
    memset(&out, 0, sizeof(out));
    dg_putf(&out, "class __DbBind {\n");
    for (int i = 0; i < c.need.count; i++) {
        zan_istr_t cls = c.need.names[i];
        dg_putf(&out,
            "    static __DbQ_%.*s Q_%.*s(IDbExecutor db) { "
            "return __DbQ_%.*s.Create(db); }\n",
            (int)cls.len, cls.str, (int)cls.len, cls.str, (int)cls.len,
            cls.str);
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
        "        return sb.ToString();\n"
        "    }\n"
        "}\n");
    /* dg_gen_entity may add nav classes to the worklist while generating */
    for (int i = 0; i < c.need.count; i++)
        dg_gen_entity(&c, &out, c.need.names[i]);

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
