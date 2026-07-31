/* nsresolve.c -- namespace-aware type resolution.
 *
 * Zan merges every input file's declarations into one flat compilation unit
 * and the binder registers/looks up types by simple name only, which made it
 * impossible for two namespaces to declare the same simple name (e.g. one
 * `class Index` per module).  This pass runs after merge and before the binder.
 *
 *   1. zan_nsresolve_stamp() records each top-level declaration's namespace and
 *      `using` list at parse time (before the flat merge loses that context).
 *   2. zan_nsresolve_run() finds simple names declared in more than one
 *      namespace and renames those declarations to a unique mangled name,
 *      keeping the original simple name in node->orig_name for downstream
 *      passes (route/view generation).  It then rewrites every type reference
 *      to the resolved type's (possibly mangled) name, honoring the referring
 *      declaration's own namespace, its `using` imports, and explicit qualified
 *      names.  Non-conflicting simple names are never touched, so all existing
 *      single-namespace code binds exactly as before.
 *
 * A type name also appears in expression position, as the receiver of a static
 * member access (`Dispatcher.Post(h)`, `Colors.Red`), where it parses as a plain
 * identifier and not as a type reference.  Those receivers are rewritten too --
 * otherwise renaming the declaration leaves every static call to it unresolved.
 * Identifiers that a local, parameter, foreach/catch variable or lambda
 * parameter of the enclosing member shadows are left alone, since there the
 * name means the variable, not the type.
 */

#include "nsresolve.h"
#include "arena.h"
#include "diag.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

/* ---- istr helpers ---- */

static zan_istr_t mk_istr(zan_arena_t *a, const char *s, size_t n) {
    zan_istr_t r;
    r.str = zan_arena_strdup(a, s, n);
    r.len = (uint32_t)n;
    return r;
}

static bool ns_istr_eq(zan_istr_t a, zan_istr_t b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.str, b.str, a.len) == 0);
}

static bool ns_has_dot(zan_istr_t s) {
    for (uint32_t i = 0; i < s.len; i++)
        if (s.str[i] == '.') return true;
    return false;
}

/* build "<a>.<b>"; if a is empty, just b */
static zan_istr_t join_ns(zan_arena_t *ar, zan_istr_t a, zan_istr_t b) {
    if (a.len == 0) return b;
    char buf[512];
    size_t n = 0;
    for (uint32_t i = 0; i < a.len && n < sizeof buf - 2; i++) buf[n++] = a.str[i];
    if (n < sizeof buf - 1) buf[n++] = '.';
    for (uint32_t i = 0; i < b.len && n < sizeof buf - 1; i++) buf[n++] = b.str[i];
    return mk_istr(ar, buf, n);
}

static zan_istr_t flatten_qname(zan_ast_node_t *q, zan_arena_t *ar) {
    zan_istr_t empty = {0};
    if (!q) return empty;
    if (q->kind == AST_IDENTIFIER) return q->ident.name;
    if (q->kind == AST_QUALIFIED_NAME) {
        char buf[512];
        size_t n = 0;
        for (int i = 0; i < q->qualified_name.parts.count; i++) {
            zan_ast_node_t *p = q->qualified_name.parts.items[i];
            if (p->kind != AST_IDENTIFIER) continue;
            if (i > 0 && n < sizeof buf - 1) buf[n++] = '.';
            for (uint32_t j = 0; j < p->ident.name.len && n < sizeof buf - 1; j++)
                buf[n++] = p->ident.name.str[j];
        }
        return mk_istr(ar, buf, n);
    }
    return empty;
}

/* ---- stamping (called per parsed file, before merge) ---- */

void zan_nsresolve_stamp(zan_ast_node_t *unit, zan_arena_t *arena) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    zan_istr_t ns = {0};
    if (unit->comp_unit.ns && unit->comp_unit.ns->kind == AST_NAMESPACE_DECL)
        ns = flatten_qname(unit->comp_unit.ns->namespace_decl.name, arena);
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (!d) continue;
        d->ns_name = ns;
        d->ns_usings = &unit->comp_unit.usings;
    }
}

/* ---- declared-type table ---- */

typedef struct {
    zan_ast_node_t *decl;
    zan_istr_t simple;   /* declared simple name */
    zan_istr_t ns;       /* namespace ("" if global) */
    zan_istr_t full;     /* ns.simple, or simple if global */
    zan_istr_t final;    /* rewritten name (== simple unless mangled) */
    bool conflicting;
} nr_type_t;

/* Names bound by the member currently being walked (locals, parameters,
 * foreach/catch variables, lambda parameters): they shadow a type of the same
 * name, so a `name.Member` receiver among them is a value, not a type. */
typedef struct {
    zan_istr_t *items;
    int count;
    int cap;
} nr_shadow_t;

typedef struct {
    nr_type_t *items;
    int count;
    zan_arena_t *arena;
    zan_diag_t *diag;
    nr_shadow_t shadow;
    /* Field names of the class being walked: like locals, a field shadows a
     * type of the same name when it is used as a receiver. */
    nr_shadow_t fields;
} nr_ctx_t;

static void shadow_add(nr_shadow_t *s, zan_istr_t name) {
    if (name.len == 0) return;
    for (int i = 0; i < s->count; i++)
        if (ns_istr_eq(s->items[i], name)) return;
    if (s->count == s->cap) {
        int cap = s->cap ? s->cap * 2 : 16;
        zan_istr_t *items = (zan_istr_t *)realloc(s->items,
                                sizeof(zan_istr_t) * (size_t)cap);
        if (!items) return;
        s->items = items;
        s->cap = cap;
    }
    s->items[s->count++] = name;
}

static bool shadow_has(nr_shadow_t *s, zan_istr_t name) {
    for (int i = 0; i < s->count; i++)
        if (ns_istr_eq(s->items[i], name)) return true;
    return false;
}

static bool is_type_decl_kind(zan_ast_kind_t k) {
    return k == AST_CLASS_DECL || k == AST_STRUCT_DECL ||
           k == AST_INTERFACE_DECL || k == AST_ENUM_DECL ||
           k == AST_DELEGATE_DECL;
}

static zan_istr_t decl_simple_name(zan_ast_node_t *d) {
    if (d->kind == AST_DELEGATE_DECL) return d->method_decl.name;
    return d->type_decl.name;
}

static void decl_set_name(zan_ast_node_t *d, zan_istr_t name) {
    if (d->kind == AST_DELEGATE_DECL) d->method_decl.name = name;
    else d->type_decl.name = name;
}

static nr_type_t *find_full(nr_ctx_t *c, zan_istr_t full) {
    for (int i = 0; i < c->count; i++)
        if (ns_istr_eq(c->items[i].full, full)) return &c->items[i];
    return NULL;
}

static int count_simple(nr_ctx_t *c, zan_istr_t simple) {
    int n = 0;
    for (int i = 0; i < c->count; i++)
        if (ns_istr_eq(c->items[i].simple, simple)) n++;
    return n;
}

static bool name_taken(nr_ctx_t *c, const char *s, size_t n) {
    for (int i = 0; i < c->count; i++) {
        zan_istr_t f = c->items[i].final;
        if (f.len == (uint32_t)n && memcmp(f.str, s, n) == 0) return true;
    }
    return false;
}

/* mangle "A.B.C" -> "A_B_C", uniquified against existing final names */
static zan_istr_t mangle(nr_ctx_t *c, zan_istr_t full) {
    char buf[600];
    size_t n = 0;
    for (uint32_t i = 0; i < full.len && n < sizeof buf - 12; i++)
        buf[n++] = (full.str[i] == '.') ? '_' : full.str[i];
    buf[n] = 0;
    size_t base = n;
    int suffix = 2;
    while (name_taken(c, buf, n)) {
        int w = snprintf(buf + base, sizeof buf - base, "_%d", suffix++);
        n = base + (w > 0 ? (size_t)w : 0);
    }
    return mk_istr(c->arena, buf, n);
}

/* ---- reference resolution ---- */

static void resolve_ref(nr_ctx_t *c, zan_ast_node_t *tr,
                        zan_istr_t ctx_ns, zan_ast_list_t *usings) {
    zan_istr_t R = tr->type_ref.name;
    if (R.len == 0) return;
    nr_type_t *t = NULL;

    if (ns_has_dot(R)) {
        /* explicit qualified reference: try full, then using-prefixed */
        t = find_full(c, R);
        if (!t && usings) {
            for (int i = 0; i < usings->count && !t; i++) {
                zan_ast_node_t *u = usings->items[i];
                if (!u || u->kind != AST_USING_DECL || u->using_decl.is_static) continue;
                zan_istr_t up = flatten_qname(u->using_decl.name, c->arena);
                t = find_full(c, join_ns(c->arena, up, R));
            }
        }
        if (t) { tr->type_ref.name = t->final; return; }
        /* qualified reference to a non-declared (e.g. stdlib) type: reduce to
         * the last segment so the simple-name binder can resolve it. */
        uint32_t last = 0;
        for (uint32_t i = 0; i < R.len; i++)
            if (R.str[i] == '.') last = i + 1;
        tr->type_ref.name = mk_istr(c->arena, R.str + last, R.len - last);
        return;
    }

    /* simple name: same-namespace first, then usings */
    if (ctx_ns.len)
        t = find_full(c, join_ns(c->arena, ctx_ns, R));
    if (!t && usings) {
        for (int i = 0; i < usings->count && !t; i++) {
            zan_ast_node_t *u = usings->items[i];
            if (!u || u->kind != AST_USING_DECL || u->using_decl.is_static) continue;
            zan_istr_t up = flatten_qname(u->using_decl.name, c->arena);
            t = find_full(c, join_ns(c->arena, up, R));
        }
    }
    if (t) { tr->type_ref.name = t->final; return; }

    /* Unresolved simple name.  If it names a type that only exists inside some
     * namespace(s) and is ambiguous, warn the user to qualify it; otherwise
     * leave it unchanged (builtin, generic type parameter, or a unique
     * non-conflicting global/simple type the binder resolves as before). */
    if (count_simple(c, R) >= 2 && !find_full(c, R)) {
        zan_diag_emit(c->diag, DIAG_ERROR, tr->loc,
                      "ambiguous type '%.*s'; qualify it with its namespace",
                      (int)R.len, R.str);
    }
}

/* Resolves a simple name in expression position to a declared type, honoring
 * the referring declaration's namespace and `using` imports, and rewrites it to
 * that type's (mangled) name.  Anything that does not resolve to a renamed
 * declaration -- a variable, a builtin, a type whose name never collided -- is
 * left exactly as it was. */
static void resolve_static_receiver(nr_ctx_t *c, zan_ast_node_t *id,
                                    zan_istr_t ctx_ns, zan_ast_list_t *usings) {
    zan_istr_t R = id->ident.name;
    if (R.len == 0 || shadow_has(&c->shadow, R)) return;

    nr_type_t *t = NULL;
    if (ctx_ns.len) t = find_full(c, join_ns(c->arena, ctx_ns, R));
    if (!t && usings) {
        for (int i = 0; i < usings->count && !t; i++) {
            zan_ast_node_t *u = usings->items[i];
            if (!u || u->kind != AST_USING_DECL || u->using_decl.is_static) continue;
            zan_istr_t up = flatten_qname(u->using_decl.name, c->arena);
            t = find_full(c, join_ns(c->arena, up, R));
        }
    }
    if (!t) t = find_full(c, R);
    if (t && !ns_istr_eq(t->final, R)) id->ident.name = t->final;
}

/* Flattens a receiver made of identifiers and member accesses (`Gui.Reactive`)
 * into a dotted name; an empty name means it is something else. */
static zan_istr_t flatten_receiver(nr_ctx_t *c, zan_ast_node_t *n) {
    zan_istr_t empty = {0};
    if (!n) return empty;
    if (n->kind == AST_IDENTIFIER) return n->ident.name;
    if (n->kind == AST_MEMBER_ACCESS) {
        zan_istr_t head = flatten_receiver(c, n->member.object);
        if (head.len == 0) return empty;
        return join_ns(c->arena, head, n->member.name);
    }
    return empty;
}

/* Rewrites a namespace-qualified static receiver (`Beta.Log.Line(...)`) to the
 * renamed declaration: the whole `Beta.Log` chain collapses into the single
 * identifier the binder knows. Only an exact namespace+type match collapses, so
 * a field chain (`this.a.b`) or a value's member is never touched. */
static void resolve_qualified_receiver(nr_ctx_t *c, zan_ast_node_t *recv) {
    zan_istr_t full = flatten_receiver(c, recv);
    if (full.len == 0 || !ns_has_dot(full)) return;
    nr_type_t *t = find_full(c, full);
    if (!t) return;
    recv->kind = AST_IDENTIFIER;
    recv->ident.name = t->final;
}

/* ---- shadow collection ---- */

static void collect_shadows(nr_ctx_t *c, zan_ast_node_t *n);

static void collect_shadows_list(nr_ctx_t *c, zan_ast_list_t *l) {
    if (!l) return;
    for (int i = 0; i < l->count; i++) collect_shadows(c, l->items[i]);
}

/* Gathers every name the member binds, anywhere in its body: nsresolve has no
 * scopes, so one flat set per member is what keeps a variable from being
 * mistaken for a type of the same name. */
static void collect_shadows(nr_ctx_t *c, zan_ast_node_t *n) {
    if (!n) return;
    switch (n->kind) {
    case AST_PARAM:
        shadow_add(&c->shadow, n->param.name);
        collect_shadows(c, n->param.default_val);
        break;
    case AST_VAR_DECL:
        shadow_add(&c->shadow, n->var_decl.name);
        collect_shadows(c, n->var_decl.initializer);
        break;
    case AST_FOREACH_STMT:
        shadow_add(&c->shadow, n->foreach_stmt.var_name);
        collect_shadows(c, n->foreach_stmt.collection);
        collect_shadows(c, n->foreach_stmt.body);
        break;
    case AST_CATCH_CLAUSE:
        shadow_add(&c->shadow, n->catch_clause.var_name);
        collect_shadows(c, n->catch_clause.body);
        break;
    case AST_LAMBDA:
        collect_shadows_list(c, &n->lambda.params);
        collect_shadows(c, n->lambda.body);
        break;
    case AST_BLOCK:
        collect_shadows_list(c, &n->block.stmts);
        break;
    case AST_EXPR_STMT:  collect_shadows(c, n->expr_stmt.expr); break;
    case AST_RETURN_STMT: collect_shadows(c, n->ret.value); break;
    case AST_IF_STMT:
        collect_shadows(c, n->if_stmt.cond);
        collect_shadows(c, n->if_stmt.then_body);
        collect_shadows(c, n->if_stmt.else_body);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        collect_shadows(c, n->while_stmt.cond);
        collect_shadows(c, n->while_stmt.body);
        break;
    case AST_FOR_STMT:
        collect_shadows(c, n->for_stmt.init);
        collect_shadows(c, n->for_stmt.cond);
        collect_shadows(c, n->for_stmt.step);
        collect_shadows(c, n->for_stmt.body);
        break;
    case AST_TRY_STMT:
        collect_shadows(c, n->try_stmt.try_body);
        collect_shadows_list(c, &n->try_stmt.catches);
        collect_shadows(c, n->try_stmt.finally_body);
        break;
    case AST_SWITCH_STMT:
        collect_shadows(c, n->switch_stmt.expr);
        collect_shadows_list(c, &n->switch_stmt.cases);
        break;
    case AST_SWITCH_CASE:
        collect_shadows(c, n->switch_case.body);
        break;
    case AST_LOCK_STMT:
        collect_shadows(c, n->lock_stmt.expr);
        collect_shadows(c, n->lock_stmt.body);
        break;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        collect_shadows(c, n->binary.left);
        collect_shadows(c, n->binary.right);
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        collect_shadows(c, n->unary.operand);
        break;
    case AST_CALL:
        collect_shadows(c, n->call.callee);
        collect_shadows_list(c, &n->call.args);
        break;
    case AST_MEMBER_ACCESS:
        collect_shadows(c, n->member.object);
        break;
    case AST_INDEX:
        collect_shadows(c, n->index.object);
        collect_shadows(c, n->index.index);
        break;
    case AST_NEW_EXPR:
        collect_shadows_list(c, &n->new_expr.args);
        break;
    case AST_CONDITIONAL:
        collect_shadows(c, n->conditional.cond);
        collect_shadows(c, n->conditional.then_expr);
        collect_shadows(c, n->conditional.else_expr);
        break;
    case AST_CAST_EXPR:
    case AST_TYPEOF_EXPR:
    case AST_SIZEOF_EXPR:
        collect_shadows(c, n->cast.expr);
        break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        collect_shadows(c, n->type_test.expr);
        break;
    case AST_AWAIT_EXPR:
        collect_shadows(c, n->await_expr.expr);
        break;
    case AST_STRING_INTERP:
        collect_shadows_list(c, &n->string_interp.parts);
        break;
    case AST_REF_ARG:
        collect_shadows(c, n->ref_arg.expr);
        break;
    case AST_THROW_STMT:
        collect_shadows(c, n->throw_stmt.value);
        break;
    case AST_YIELD_STMT:
        collect_shadows(c, n->yield_stmt.value);
        break;
    default:
        break;
    }
}

/* ---- AST walk ---- */

static void nr_walk(nr_ctx_t *c, zan_ast_node_t *n,
                    zan_istr_t ns, zan_ast_list_t *usings);

static void nr_walk_list(nr_ctx_t *c, zan_ast_list_t *l,
                         zan_istr_t ns, zan_ast_list_t *usings) {
    if (!l) return;
    for (int i = 0; i < l->count; i++)
        nr_walk(c, l->items[i], ns, usings);
}

static void nr_walk(nr_ctx_t *c, zan_ast_node_t *n,
                    zan_istr_t ns, zan_ast_list_t *usings) {
    if (!n) return;
    switch (n->kind) {
    case AST_TYPE_REF:
        resolve_ref(c, n, ns, usings);
        nr_walk_list(c, &n->type_ref.type_args, ns, usings);
        break;

    case AST_CLASS_DECL:
    case AST_STRUCT_DECL:
    case AST_INTERFACE_DECL:
    case AST_ENUM_DECL: {
        int saved_fields = c->fields.count;
        for (int i = 0; i < n->type_decl.members.count; i++) {
            zan_ast_node_t *m = n->type_decl.members.items[i];
            if (m && (m->kind == AST_FIELD_DECL || m->kind == AST_PROPERTY_DECL))
                shadow_add(&c->fields, m->field_decl.name);
        }
        nr_walk_list(c, &n->type_decl.bases, ns, usings);
        nr_walk_list(c, &n->type_decl.where_clauses, ns, usings);
        nr_walk_list(c, &n->type_decl.members, ns, usings);
        c->fields.count = saved_fields;
        break;
    }

    case AST_DELEGATE_DECL:
    case AST_METHOD_DECL:
    case AST_CONSTRUCTOR_DECL:
    case AST_DESTRUCTOR_DECL:
        c->shadow.count = 0;
        for (int i = 0; i < c->fields.count; i++)
            shadow_add(&c->shadow, c->fields.items[i]);
        collect_shadows_list(c, &n->method_decl.params);
        collect_shadows(c, n->method_decl.body);
        nr_walk(c, n->method_decl.return_type, ns, usings);
        nr_walk_list(c, &n->method_decl.params, ns, usings);
        nr_walk_list(c, &n->method_decl.where_clauses, ns, usings);
        nr_walk_list(c, &n->method_decl.base_args, ns, usings);
        nr_walk(c, n->method_decl.body, ns, usings);
        break;

    case AST_FIELD_DECL:
    case AST_PROPERTY_DECL:
        nr_walk(c, n->field_decl.type, ns, usings);
        nr_walk(c, n->field_decl.initializer, ns, usings);
        break;

    case AST_PARAM:
        nr_walk(c, n->param.type, ns, usings);
        nr_walk(c, n->param.default_val, ns, usings);
        break;

    case AST_WHERE_CLAUSE:
        nr_walk_list(c, &n->where_clause.constraints, ns, usings);
        break;

    case AST_BLOCK:
        nr_walk_list(c, &n->block.stmts, ns, usings);
        break;

    case AST_VAR_DECL:
        nr_walk(c, n->var_decl.type, ns, usings);
        nr_walk(c, n->var_decl.initializer, ns, usings);
        break;

    case AST_EXPR_STMT:
        nr_walk(c, n->expr_stmt.expr, ns, usings);
        break;

    case AST_RETURN_STMT:
        nr_walk(c, n->ret.value, ns, usings);
        break;

    case AST_IF_STMT:
        nr_walk(c, n->if_stmt.cond, ns, usings);
        nr_walk(c, n->if_stmt.then_body, ns, usings);
        nr_walk(c, n->if_stmt.else_body, ns, usings);
        break;

    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        nr_walk(c, n->while_stmt.cond, ns, usings);
        nr_walk(c, n->while_stmt.body, ns, usings);
        break;

    case AST_FOR_STMT:
        nr_walk(c, n->for_stmt.init, ns, usings);
        nr_walk(c, n->for_stmt.cond, ns, usings);
        nr_walk(c, n->for_stmt.step, ns, usings);
        nr_walk(c, n->for_stmt.body, ns, usings);
        break;

    case AST_FOREACH_STMT:
        nr_walk(c, n->foreach_stmt.var_type, ns, usings);
        nr_walk(c, n->foreach_stmt.collection, ns, usings);
        nr_walk(c, n->foreach_stmt.body, ns, usings);
        break;

    case AST_THROW_STMT:
        nr_walk(c, n->throw_stmt.value, ns, usings);
        break;

    case AST_TRY_STMT:
        nr_walk(c, n->try_stmt.try_body, ns, usings);
        nr_walk_list(c, &n->try_stmt.catches, ns, usings);
        nr_walk(c, n->try_stmt.finally_body, ns, usings);
        break;

    case AST_CATCH_CLAUSE:
        nr_walk(c, n->catch_clause.type, ns, usings);
        nr_walk(c, n->catch_clause.body, ns, usings);
        break;

    case AST_SWITCH_STMT:
        nr_walk(c, n->switch_stmt.expr, ns, usings);
        nr_walk_list(c, &n->switch_stmt.cases, ns, usings);
        break;

    case AST_SWITCH_CASE:
        nr_walk(c, n->switch_case.pattern, ns, usings);
        nr_walk(c, n->switch_case.body, ns, usings);
        break;

    case AST_LOCK_STMT:
        nr_walk(c, n->lock_stmt.expr, ns, usings);
        nr_walk(c, n->lock_stmt.body, ns, usings);
        break;

    case AST_YIELD_STMT:
        nr_walk(c, n->yield_stmt.value, ns, usings);
        break;

    case AST_BINARY:
    case AST_ASSIGNMENT:
        nr_walk(c, n->binary.left, ns, usings);
        nr_walk(c, n->binary.right, ns, usings);
        break;

    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        nr_walk(c, n->unary.operand, ns, usings);
        break;

    case AST_CALL:
        nr_walk(c, n->call.callee, ns, usings);
        nr_walk_list(c, &n->call.type_args, ns, usings);
        nr_walk_list(c, &n->call.args, ns, usings);
        break;

    case AST_MEMBER_ACCESS:
        if (n->member.object && n->member.object->kind == AST_IDENTIFIER)
            resolve_static_receiver(c, n->member.object, ns, usings);
        else if (n->member.object && n->member.object->kind == AST_MEMBER_ACCESS)
            resolve_qualified_receiver(c, n->member.object);
        nr_walk(c, n->member.object, ns, usings);
        break;

    case AST_INDEX:
        nr_walk(c, n->index.object, ns, usings);
        nr_walk(c, n->index.index, ns, usings);
        break;

    case AST_NEW_EXPR:
        nr_walk(c, n->new_expr.type, ns, usings);
        nr_walk_list(c, &n->new_expr.args, ns, usings);
        break;

    case AST_CAST_EXPR:
    case AST_TYPEOF_EXPR:
    case AST_SIZEOF_EXPR:
        nr_walk(c, n->cast.type, ns, usings);
        nr_walk(c, n->cast.expr, ns, usings);
        break;

    case AST_IS_EXPR:
    case AST_AS_EXPR:
        nr_walk(c, n->type_test.expr, ns, usings);
        nr_walk(c, n->type_test.type, ns, usings);
        break;

    case AST_CONDITIONAL:
        nr_walk(c, n->conditional.cond, ns, usings);
        nr_walk(c, n->conditional.then_expr, ns, usings);
        nr_walk(c, n->conditional.else_expr, ns, usings);
        break;

    case AST_LAMBDA:
        nr_walk_list(c, &n->lambda.params, ns, usings);
        nr_walk(c, n->lambda.body, ns, usings);
        break;

    case AST_AWAIT_EXPR:
        nr_walk(c, n->await_expr.expr, ns, usings);
        break;

    case AST_STRING_INTERP:
        nr_walk_list(c, &n->string_interp.parts, ns, usings);
        break;

    case AST_REF_ARG:
        nr_walk(c, n->ref_arg.expr, ns, usings);
        nr_walk(c, n->ref_arg.decl_type, ns, usings);
        break;

    case AST_QUERY_EXPR:
        nr_walk(c, n->query.source, ns, usings);
        nr_walk_list(c, &n->query.wheres, ns, usings);
        nr_walk(c, n->query.select, ns, usings);
        break;

    case AST_ENUM_MEMBER:
        nr_walk(c, n->enum_member.value, ns, usings);
        break;

    default:
        /* literals, identifiers, this/base, labels, goto: no nested types */
        break;
    }
}

/* ---- entry point ---- */

void zan_nsresolve_run(zan_ast_node_t *unit, zan_arena_t *arena, zan_diag_t *diag) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    zan_ast_list_t *decls = &unit->comp_unit.decls;
    if (decls->count == 0) return;

    nr_ctx_t c;
    c.arena = arena;
    c.diag = diag;
    c.count = 0;
    c.shadow.items = NULL;
    c.shadow.count = 0;
    c.shadow.cap = 0;
    c.fields.items = NULL;
    c.fields.count = 0;
    c.fields.cap = 0;
    c.items = (nr_type_t *)zan_arena_alloc(arena,
                    sizeof(nr_type_t) * (size_t)(decls->count + 1));

    /* 1. collect declared types */
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *d = decls->items[i];
        if (!d || !is_type_decl_kind(d->kind)) continue;
        nr_type_t *t = &c.items[c.count++];
        t->decl = d;
        t->simple = decl_simple_name(d);
        t->ns = d->ns_name;
        t->full = join_ns(arena, d->ns_name, t->simple);
        t->final = t->simple;
        t->conflicting = false;
    }

    /* 2. detect cross-namespace simple-name collisions */
    for (int i = 0; i < c.count; i++) {
        for (int j = i + 1; j < c.count; j++) {
            if (ns_istr_eq(c.items[i].simple, c.items[j].simple) &&
                !ns_istr_eq(c.items[i].full, c.items[j].full)) {
                c.items[i].conflicting = true;
                c.items[j].conflicting = true;
            }
        }
    }

    /* 3. give each conflicting declaration a unique mangled name */
    for (int i = 0; i < c.count; i++) {
        if (!c.items[i].conflicting) continue;
        zan_istr_t m = mangle(&c, c.items[i].full);
        c.items[i].final = m;
        if (c.items[i].decl->orig_name.len == 0)
            c.items[i].decl->orig_name = c.items[i].simple;
        decl_set_name(c.items[i].decl, m);
    }

    /* 4. rewrite every type reference against its declaration's ns/usings */
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *d = decls->items[i];
        if (!d) continue;
        c.shadow.count = 0;
        nr_walk(&c, d, d->ns_name, d->ns_usings);
    }
    free(c.shadow.items);
}
