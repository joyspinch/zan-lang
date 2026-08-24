/* checker.c -- Basic type checker for the Zan language.
 *
 * For M1 this performs lightweight checks:
 *   - Expression type inference (literals, binary ops, calls)
 *   - Return type validation
 *   - Variable type resolution
 *   - Basic assignment compatibility
 */

#include "checker.h"
#include "reflect_api.h"
#include "definite.h"
#include "diag.h"
#include "arena.h"
#include <string.h>

/* Depth cap for every walk that follows base_type / interface chains. The
 * binder rejects cyclic inheritance outright (binder.c resolve_bases), but a
 * stray ring must never turn a checker pass into an infinite loop. */
#define CHECKER_DERIVES_MAX_DEPTH 512

void zan_checker_init(zan_checker_t *c, zan_binder_t *binder,
                      zan_arena_t *arena, zan_diag_t *diag) {
    memset(c, 0, sizeof(*c));
    c->binder = binder;
    c->arena = arena;
    c->diag = diag;
    c->current_return_type = NULL;
}

/* ---- [NoRuntime] ---------------------------------------------------------
 * A [NoRuntime] method must run with no managed runtime underneath it: crash
 * handlers, thread trampolines and startup code execute where the allocator,
 * the ARC helpers and the exception globals may be unusable or absent. The
 * checker rejects the constructs that would emit calls into that runtime;
 * irgen additionally emits no retain/release inside such a body. */
static void no_runtime_reject(zan_checker_t *c, zan_loc_t loc, const char *what) {
    if (!c->in_no_runtime) return;
    zan_diag_emit(c->diag, DIAG_ERROR, loc,
                  "%s is not allowed in a [NoRuntime] method", what);
}

/* The payload type of a nullable value operand: `int?` -> `int`, anything
 * else unchanged. Lifted operations (`int? + 1`, `int? > 3`) are validated on
 * the payload; irgen lowers them. */
static zan_type_t *checker_nullable_base(zan_type_t *t) {
    return (t && t->kind == TYPE_NULLABLE && t->element_type)
        ? t->element_type : t;
}

/* Look up a named method (e.g. an operator like "op_call"/"op_index") on a
 * class/struct symbol, walking the base chain. Used by the checker to type
 * operator-overloaded call/index expressions. */
static zan_symbol_t *checker_find_method(zan_symbol_t *type_sym, zan_istr_t name) {
    while (type_sym) {
        for (int i = 0; i < type_sym->member_count; i++) {
            zan_symbol_t *m = type_sym->members[i];
            if (m && m->kind == SYM_METHOD &&
                m->name.len == name.len &&
                memcmp(m->name.str, name.str, (size_t)name.len) == 0)
                return m;
        }
        type_sym = (type_sym->type && type_sym->type->base_type)
            ? type_sym->type->base_type->sym : NULL;
    }
    return NULL;
}

/* ---- readonly fields ----------------------------------------------------
 * `readonly` was parsed and stored but never enforced, so a field advertised as
 * immutable could be reassigned from anywhere. A readonly field is writable
 * only from a constructor of the type that declares it (its declaration
 * initializer is lowered inside that constructor too); everywhere else the
 * assignment is an error. */

/* The declared field of `type_sym` named `name`, walking the base chain. A
 * derived field that hides a base one appears twice in the member list
 * (inherited fields are its prefix), so the last match within a type is the
 * declaration that type contributes. */
static zan_symbol_t *checker_find_field(zan_symbol_t *type_sym, zan_istr_t name) {
    while (type_sym) {
        zan_symbol_t *found = NULL;
        for (int i = 0; i < type_sym->member_count; i++) {
            zan_symbol_t *m = type_sym->members[i];
            if (m && (m->kind == SYM_FIELD || m->kind == SYM_PROPERTY) &&
                m->name.len == name.len &&
                memcmp(m->name.str, name.str, (size_t)name.len) == 0)
                found = m;
        }
        if (found) return found;
        type_sym = (type_sym->type && type_sym->type->base_type)
            ? type_sym->type->base_type->sym : NULL;
    }
    return NULL;
}

/* ---- local variable tracking ---------------------------------------------
 * The checker walks a method body without its own scope stack; irgen, in
 * contrast, resolves a bare name in an expression local-first (its
 * local_scope_t), then the enclosing type's fields, then scope lookup. To
 * type an identifier the same way, track the current body's locals/params in
 * a simple name-keyed list: parameters are seeded at method entry, var and
 * foreach declarations register on the way down. One approximation: names
 * are never unregistered at block exit, so an out-of-scope use resolves to
 * the stale local's type rather than error -- a benign widening, consistent
 * with the checker's "defer, don't error" stance for anything it cannot see
 * precisely. */
struct checker_local {
    zan_istr_t name;
    zan_type_t *type;
    /* the method this local was initialised from when that method can hand
     * back null; NULL when the initialiser cannot be null */
    zan_symbol_t *null_src;
    struct checker_local *next;
};

static struct checker_local *checker_find_local_slot(zan_checker_t *c,
                                                     zan_istr_t name) {
    for (struct checker_local *l = c->locals; l; l = l->next) {
        if (l->name.len == name.len &&
            memcmp(l->name.str, name.str, (size_t)name.len) == 0)
            return l;
    }
    return NULL;
}

static zan_type_t *checker_find_local(zan_checker_t *c, zan_istr_t name) {
    for (struct checker_local *l = c->locals; l; l = l->next) {
        if (l->name.len == name.len &&
            memcmp(l->name.str, name.str, (size_t)name.len) == 0)
            return l->type;
    }
    return NULL;
}

static void checker_add_local(zan_checker_t *c, zan_istr_t name,
                              zan_type_t *type) {
    if (!type || type == c->binder->type_error) return;
    struct checker_local *l = (struct checker_local *)
        zan_arena_alloc(c->arena, sizeof(struct checker_local));
    l->name = name;
    l->type = type;
    l->null_src = NULL;
    l->next = c->locals;
    c->locals = l;
}

/* `T v = f();` where `f` can return null: remember which method it came from,
 * so a later unguarded `v.M()` in this body can name it. */
static void checker_mark_local_null_src(zan_checker_t *c, zan_ast_node_t *decl) {
    zan_ast_node_t *init = decl->var_decl.initializer;
    if (!init || init->kind != AST_CALL || init != c->last_call_node) return;
    if (!c->last_call_method) return;
    struct checker_local *l = checker_find_local_slot(c, decl->var_decl.name);
    if (l) l->null_src = c->last_call_method;
}

static bool field_is_readonly(zan_symbol_t *field) {
    return field && field->decl && field->decl->kind == AST_FIELD_DECL &&
           (field->decl->field_decl.modifiers & MOD_READONLY) != 0;
}

/* A getter-only property (`{ get; }` / `{ get { ... } }` with no setter
 * keyword) is read-only: assigning through `obj.Prop = v` has no setter to
 * dispatch to. C# rejects the write; `{ get; set; }` is writable (automatic or
 * custom). `{ get; init; }` is *not* read-only -- its init accessor is a
 * setter restricted to object initialization (see property_is_init_only). */
static bool property_is_readonly(zan_symbol_t *prop) {
    if (!prop || prop->kind != SYM_PROPERTY || !prop->decl) return false;
    return !prop->decl->field_decl.has_setter &&
           !prop->decl->field_decl.has_init;
}

/* `{ get; init; }`: an init accessor is a setter that may only run while the
 * object is being initialized -- from an object initializer, or from a
 * constructor of the declaring type. */
static bool property_is_init_only(zan_symbol_t *prop) {
    if (!prop || prop->kind != SYM_PROPERTY || !prop->decl) return false;
    return prop->decl->field_decl.has_init &&
           !prop->decl->field_decl.has_setter;
}

/* The assignment target's field symbol together with the type that declares
 * it, for `x = ...`, `this.x = ...` and `obj.x = ...`. */
static zan_symbol_t *assign_target_field(zan_checker_t *c, zan_ast_node_t *lhs,
                                         zan_symbol_t **owner) {
    if (!lhs) return NULL;
    if (lhs->kind == AST_IDENTIFIER) {
        if (!c->current_type_sym) return NULL;
        if (owner) *owner = c->current_type_sym;
        return checker_find_field(c->current_type_sym, lhs->ident.name);
    }
    if (lhs->kind == AST_MEMBER_ACCESS) {
        zan_symbol_t *ts = NULL;
        if (lhs->member.object && lhs->member.object->kind == AST_THIS_EXPR) {
            ts = c->current_type_sym;
        } else {
            zan_type_t *ot = zan_checker_check_expr(c, lhs->member.object);
            ts = ot ? ot->sym : NULL;
        }
        if (!ts) return NULL;
        if (owner) *owner = ts;
        return checker_find_field(ts, lhs->member.name);
    }
    return NULL;
}

static void check_readonly_assignment(zan_checker_t *c, zan_ast_node_t *expr) {
    if (expr && expr->binary.left && expr->binary.left->kind == AST_IDENTIFIER &&
        checker_find_local(c, expr->binary.left->ident.name))
        return;
    zan_symbol_t *owner = NULL;
    zan_symbol_t *field = assign_target_field(c, expr->binary.left, &owner);
    /* a getter-only property has no setter: the write cannot be dispatched */
    if (property_is_readonly(field)) {
        zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                      "property '%.*s' has no setter and cannot be assigned",
                      (int)field->name.len, field->name.str);
        return;
    }
    /* an init-only property (`{ get; init; }`) is writable only while the
     * object is being initialized: from an object initializer, or from a
     * constructor of its declaring type. */
    if (property_is_init_only(field)) {
        if (c->in_ctor && field->parent == c->current_type_sym) return;
        zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                      "property '%.*s' has an init accessor and can only be "
                      "assigned in an object initializer or a constructor of "
                      "'%.*s'",
                      (int)field->name.len, field->name.str,
                      (int)(field->parent ? field->parent->name.len : 0),
                      field->parent ? field->parent->name.str : "");
        return;
    }
    if (!field_is_readonly(field)) return;
    /* A derived constructor cannot initialize a readonly field declared by its
     * base. `field->parent` is the declaration owner; `owner` is only the
     * lookup starting point. */
    if (c->in_ctor && field->parent == c->current_type_sym) return;
    zan_symbol_t *decl_owner = field->parent ? field->parent : owner;
    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                  "cannot assign to readonly field '%.*s' outside a "
                  "constructor of '%.*s'",
                  (int)field->name.len, field->name.str,
                  (int)(decl_owner ? decl_owner->name.len : 0),
                  decl_owner ? decl_owner->name.str : "");
}

/* `Prop++` / `Prop--` on a getter-only property must be rejected like any
 * other write to it (the ++ lowers to getter + setter, and there is none). */
static void check_readonly_incdec(zan_checker_t *c, zan_ast_node_t *expr) {
    if (!expr || !expr->unary.operand) return;
    zan_ast_node_t *operand = expr->unary.operand;
    zan_symbol_t *owner = NULL;
    zan_symbol_t *field = NULL;
    if (operand->kind == AST_IDENTIFIER) {
        if (checker_find_local(c, operand->ident.name)) return;
        if (!c->current_type_sym) return;
        field = checker_find_field(c->current_type_sym, operand->ident.name);
    } else if (operand->kind == AST_MEMBER_ACCESS) {
        zan_symbol_t *ts = NULL;
        if (operand->member.object &&
            operand->member.object->kind == AST_THIS_EXPR) {
            ts = c->current_type_sym;
        } else {
            zan_type_t *ot = zan_checker_check_expr(c, operand->member.object);
            ts = ot ? ot->sym : NULL;
        }
        if (!ts) return;
        field = checker_find_field(ts, operand->member.name);
    }
    if (!property_is_readonly(field)) return;
    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                  "property '%.*s' has no setter and cannot be assigned",
                  (int)field->name.len, field->name.str);
}

static bool checker_type_assignable(zan_type_t *target, zan_type_t *value);

static zan_type_t *checker_assignment_target_type(zan_checker_t *c,
                                                   zan_ast_node_t *lhs) {
    if (!lhs) return NULL;
    if (lhs->kind == AST_IDENTIFIER) {
        zan_type_t *local = checker_find_local(c, lhs->ident.name);
        if (local) return local;
        zan_symbol_t *field = assign_target_field(c, lhs, NULL);
        return field ? field->type : NULL;
    }
    if (lhs->kind == AST_MEMBER_ACCESS) {
        zan_type_t *obj = zan_checker_check_expr(c, lhs->member.object);
        zan_symbol_t *sym = obj && obj->sym ? checker_find_field(obj->sym,
                                                                  lhs->member.name)
                                            : NULL;
        return sym ? sym->type : NULL;
    }
    if (lhs->kind == AST_INDEX) {
        zan_type_t *obj = zan_checker_check_expr(c, lhs->index.object);
        zan_checker_check_expr(c, lhs->index.index);
        if (obj && obj->kind == TYPE_ARRAY) return obj->element_type;
        if (obj && obj->kind == TYPE_STRING) return c->binder->type_char;
        if (obj && obj->kind == TYPE_CLASS && obj->type_arg_count == 1 &&
            obj->name.len == 4 && memcmp(obj->name.str, "List", 4) == 0)
            return obj->type_args[0];
    }
    return NULL;
}

/* `obj[i] = v` on a class/struct instance lowers to a static
 * `op_index_set(self, index, value)` call (see irgen_expr.c). The checker
 * otherwise types the assignment through the *read* overload `op_index`,
 * whose return type (e.g. LuaValue) is not the value slot type, so a written
 * string into `person["lang"] = "Zan"` was rejected against LuaValue. Resolve
 * the op_index_set overload whose declared index and value parameters accept
 * the written expressions and return the value parameter's type. */
static zan_type_t *checker_index_set_target(zan_checker_t *c,
                                            zan_ast_node_t *lhs,
                                            zan_type_t *right) {
    zan_type_t *obj = zan_checker_check_expr(c, lhs->index.object);
    zan_type_t *idx = zan_checker_check_expr(c, lhs->index.index);
    if (!obj || !obj->sym ||
        (obj->kind != TYPE_CLASS && obj->kind != TYPE_STRUCT))
        return NULL;
    zan_istr_t op_name = {(char *)"op_index_set", 12};
    for (int i = 0; i < obj->sym->member_count; i++) {
        zan_symbol_t *m = obj->sym->members[i];
        if (m->kind != SYM_METHOD || m->name.len != op_name.len ||
            memcmp(m->name.str, op_name.str, op_name.len) != 0) continue;
        if (!m->decl || m->decl->kind != AST_METHOD_DECL ||
            m->decl->method_decl.params.count != 3) continue;
        zan_ast_node_t *p0 = m->decl->method_decl.params.items[0];
        zan_ast_node_t *p1 = m->decl->method_decl.params.items[1];
        zan_ast_node_t *p2 = m->decl->method_decl.params.items[2];
        if (p0->kind != AST_PARAM || p1->kind != AST_PARAM ||
            p2->kind != AST_PARAM) continue;
        zan_type_t *pt0 = zan_binder_resolve_type(c->binder, p0->param.type);
        zan_type_t *pt1 = zan_binder_resolve_type(c->binder, p1->param.type);
        zan_type_t *pt2 = zan_binder_resolve_type(c->binder, p2->param.type);
        if (!checker_type_assignable(pt0, obj)) continue;
        if (idx && !checker_type_assignable(pt1, idx)) continue;
        if (right && !checker_type_assignable(pt2, right)) continue;
        return pt2;
    }
    return NULL;
}

/* ---- generic constraint checking ---- */

/* True when `arg` is, implements, or derives from `cons`. Depth-guarded so a
 * (defensively) cyclic hierarchy cannot recurse forever. */
static bool type_satisfies_constraint_depth(zan_type_t *arg, zan_type_t *cons,
                                            int depth) {
    if (!arg || !cons) return true;
    if (depth > CHECKER_DERIVES_MAX_DEPTH) return false;
    if (arg == cons) return true;
    if (arg->sym && cons->sym && arg->sym == cons->sym) return true;
    for (int i = 0; i < arg->interface_count; i++) {
        if (type_satisfies_constraint_depth(arg->interfaces[i], cons, depth + 1))
            return true;
    }
    if (arg->base_type && arg->base_type != arg)
        return type_satisfies_constraint_depth(arg->base_type, cons, depth + 1);
    return false;
}

static bool type_satisfies_constraint(zan_type_t *arg, zan_type_t *cons) {
    return type_satisfies_constraint_depth(arg, cons, 0);
}

static const char *type_name(zan_type_t *t);

/* Validate each `where T : C` clause of a generic type's declaration against
 * the instantiation's type arguments. */
static void check_generic_constraints(zan_checker_t *c, zan_type_t *t,
                                      zan_loc_t loc) {
    if (!t || !t->sym || !t->sym->decl) return;
    zan_ast_node_t *d = t->sym->decl;
    if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL &&
        d->kind != AST_INTERFACE_DECL)
        return;
    if (!t->type_args || t->type_arg_count == 0) return;
    for (int w = 0; w < d->type_decl.where_clauses.count; w++) {
        zan_ast_node_t *wc = d->type_decl.where_clauses.items[w];
        int idx = -1;
        for (int i = 0; i < d->type_decl.type_params.count; i++) {
            zan_ast_node_t *tp = d->type_decl.type_params.items[i];
            if (tp->ident.name.len == wc->where_clause.param_name.len &&
                memcmp(tp->ident.name.str, wc->where_clause.param_name.str,
                       (size_t)tp->ident.name.len) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0 || idx >= t->type_arg_count) continue;
        zan_type_t *arg = t->type_args[idx];
        /* Box<T> written inside a generic that owns T: nothing is known about
         * T here, and the constraint is checked where that outer generic is
         * instantiated with a real type. */
        if (arg && arg->kind == TYPE_TYPE_PARAM) continue;
        for (int k = 0; k < wc->where_clause.constraints.count; k++) {
            zan_ast_node_t *cons_ref = wc->where_clause.constraints.items[k];
            zan_type_t *cons = zan_binder_resolve_type(c->binder, cons_ref);
            if (!cons || cons->kind == TYPE_ERROR) {
                zan_diag_emit(c->diag, DIAG_ERROR, loc,
                    "unknown constraint '%.*s' in 'where %.*s : ...' for '%s'",
                    cons_ref->type_ref.name.len, cons_ref->type_ref.name.str,
                    wc->where_clause.param_name.len,
                    wc->where_clause.param_name.str,
                    type_name(t));
                continue;
            }
            if (!type_satisfies_constraint(arg, cons)) {
                zan_diag_emit(c->diag, DIAG_ERROR, loc,
                    "type '%s' does not satisfy constraint '%s' for type parameter '%.*s' of '%s'",
                    type_name(arg), type_name(cons),
                    wc->where_clause.param_name.len,
                    wc->where_clause.param_name.str,
                    type_name(t));
            }
        }
    }
}

/* ---- expression type checking ---- */

static const char *type_name(zan_type_t *t) {
    if (!t) return "<unknown>";
    return t->name.str;
}

/* A weak slot must point at an ARC-managed object whose final release reaches
 * zan_rt_weak_nil_all. Intrinsic collections share TYPE_CLASS with user
 * classes but have no class symbol and use their own layouts/destructors.
 * Unresolved type parameters are deferred: their eventual argument is not
 * known while checking the generic declaration. */
static bool checker_weak_target_is_arc_ref(zan_type_t *t) {
    if (!t) return false;
    if (t->kind == TYPE_TYPE_PARAM) return true;
    if (t->kind == TYPE_INTERFACE) return true;
    return t->kind == TYPE_CLASS && t->sym != NULL;
}

static void check_weak_member(zan_checker_t *c, zan_ast_node_t *owner,
                              zan_ast_node_t *member) {
    if (!c || !owner || !member ||
        (member->kind != AST_FIELD_DECL && member->kind != AST_PROPERTY_DECL) ||
        !(member->field_decl.modifiers & MOD_WEAK))
        return;

    if (owner->kind == AST_STRUCT_DECL) {
        zan_diag_emit(c->diag, DIAG_ERROR, member->loc,
                      "weak field '%.*s' inside value-type struct '%.*s' "
                      "cannot be tracked safely; move it to a class or remove "
                      "'weak'",
                      (int)member->field_decl.name.len,
                      member->field_decl.name.str,
                      (int)owner->type_decl.name.len,
                      owner->type_decl.name.str);
        return;
    }

    zan_type_t *type = zan_binder_resolve_type(c->binder,
                                                member->field_decl.type);
    if (!type || type == c->binder->type_error) return;
    if (checker_weak_target_is_arc_ref(type)) return;

    zan_diag_emit(c->diag, DIAG_ERROR, member->loc,
                  "weak field '%.*s' has type '%s'; weak requires an "
                  "ARC-managed class or interface reference, so use a "
                  "class/interface type or remove 'weak'",
                  (int)member->field_decl.name.len,
                  member->field_decl.name.str,
                  type_name(type));
}

/* ---- constructor availability -------------------------------------------
 * A class that declares constructors has no implicit parameterless one, so
 * `new T()` on it has nothing to run: irgen zero-fills the object and calls no
 * constructor, and the first method call on that half-built instance reads a
 * null field (`List` members are null, not empty). That crash surfaced far
 * from the `new`, so the arity mismatch is diagnosed here instead. Only
 * constructors the class itself declares count -- a derived class that
 * declares none keeps its implicit parameterless constructor. */

/* Number of arguments the constructor `decl` accepts at minimum / at most;
 * `max` is -1 for a `params T[]` tail (unbounded). */
static void ctor_arity(zan_ast_node_t *decl, int *min, int *max) {
    int total = decl->method_decl.params.count;
    int required = 0;
    bool variadic = false;
    for (int i = 0; i < total; i++) {
        zan_ast_node_t *p = decl->method_decl.params.items[i];
        if (!p || p->kind != AST_PARAM) { required++; continue; }
        if (p->param.is_params) { variadic = true; continue; }
        if (!p->param.default_val) required++;
    }
    *min = required;
    *max = variadic ? -1 : total;
}

/* Arguments that go to the constructor: an object initializer
 * (`new T(a) { Field = v }`) is parsed with the field writes appended to the
 * argument list, so drop that tail (mirrors irgen's init_start). */
static int ctor_arg_count(zan_checker_t *c, zan_ast_node_t *expr,
                          zan_symbol_t *type_sym) {
    int n = expr->new_expr.args.count;
    while (n > 0) {
        zan_ast_node_t *a = expr->new_expr.args.items[n - 1];
        if (!a || a->kind != AST_ASSIGNMENT || !a->binary.left ||
            a->binary.left->kind != AST_IDENTIFIER ||
            !checker_find_field(type_sym, a->binary.left->ident.name))
            break;
        n--;
    }
    (void)c;
    return n;
}

/* True when `expr`'s object initializer writes every instance field and
 * property the type (and its bases) declare -- the one case where running no
 * constructor still leaves a whole object, so `new T { A = 1, B = 2 }` needs
 * no parameterless constructor to stand in. A partial initializer does not
 * qualify: the members it skips would stay zero-filled, which is exactly the
 * half-built instance the diagnostic below exists to prevent. */
static bool initializer_covers_all_members(zan_ast_node_t *expr,
                                           zan_symbol_t *type_sym,
                                           int init_start) {
    int covered = 0;
    int members = 0;
    for (zan_symbol_t *t = type_sym; t; ) {
        for (int i = 0; i < t->member_count; i++) {
            zan_symbol_t *m = t->members[i];
            if (!m || (m->kind != SYM_FIELD && m->kind != SYM_PROPERTY))
                continue;
            if (m->modifiers & MOD_STATIC) continue;
            members++;
            for (int a = init_start; a < expr->new_expr.args.count; a++) {
                zan_ast_node_t *as = expr->new_expr.args.items[a];
                zan_istr_t n = as->binary.left->ident.name;
                if (n.len == m->name.len &&
                    memcmp(n.str, m->name.str, (size_t)n.len) == 0) {
                    covered++;
                    break;
                }
            }
        }
        t = (t->type && t->type->base_type) ? t->type->base_type->sym : NULL;
    }
    return members > 0 && covered == members;
}

static void check_ctor_available(zan_checker_t *c, zan_type_t *type,
                                 zan_ast_node_t *expr) {
    if (!type || type->kind != TYPE_CLASS || !type->sym) return;
    if (expr->new_expr.is_array) return;
    zan_symbol_t *sym = type->sym;
    int declared = 0;
    int argc = ctor_arg_count(c, expr, sym);
    /* `new T { ... }` with no constructor arguments but a complete object
     * initializer builds the object itself; see above. */
    if (argc == 0 && argc < expr->new_expr.args.count &&
        initializer_covers_all_members(expr, sym, argc)) {
        return;
    }
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (!m || m->kind != SYM_CONSTRUCTOR || !m->decl) continue;
        if (m->decl->kind != AST_CONSTRUCTOR_DECL) continue;
        declared++;
        int lo = 0;
        int hi = 0;
        ctor_arity(m->decl, &lo, &hi);
        if (argc >= lo && (hi < 0 || argc <= hi)) return;
    }
    if (declared == 0) return;
    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
        "no constructor of '%s' takes %d argument(s); a class that declares "
        "constructors has no implicit parameterless one",
        type_name(type), argc);
}

static bool type_is_numeric(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BYTE: case TYPE_SHORT: case TYPE_INT: case TYPE_LONG:
    case TYPE_SBYTE: case TYPE_USHORT: case TYPE_UINT: case TYPE_ULONG:
    case TYPE_FLOAT: case TYPE_DOUBLE: case TYPE_NINT:
        return true;
    default:
        return false;
    }
}

/* True for the builtin scalar types that declare no members of their own:
 * string, the numeric types, bool, char. Their only resolvable member is the
 * string.Length property (lowered by irgen); their instance methods
 * (ToString, Substring, ...) are also lowered by irgen from the member name
 * alone. Any other member access on such a type is a typo. */
static bool type_is_scalar_primitive(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BOOL: case TYPE_BYTE: case TYPE_SHORT: case TYPE_INT:
    case TYPE_LONG: case TYPE_SBYTE: case TYPE_USHORT: case TYPE_UINT:
    case TYPE_ULONG: case TYPE_FLOAT: case TYPE_DOUBLE: case TYPE_CHAR:
    case TYPE_STRING: case TYPE_NINT:
        return true;
    default:
        return false;
    }
}

static bool type_is_integral(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BYTE: case TYPE_SHORT: case TYPE_INT: case TYPE_LONG:
    case TYPE_SBYTE: case TYPE_USHORT: case TYPE_UINT: case TYPE_ULONG:
    case TYPE_NINT:
        return true;
    default:
        return false;
    }
}

static zan_type_t *promote_numeric(zan_binder_t *b, zan_type_t *a, zan_type_t *b_type) {
    if (!a || !b_type) return b->type_error;
    if (a->kind == TYPE_DOUBLE || b_type->kind == TYPE_DOUBLE) return b->type_double;
    if (a->kind == TYPE_FLOAT  || b_type->kind == TYPE_FLOAT)  return b->type_float;
    if (a->kind == TYPE_ULONG  || b_type->kind == TYPE_ULONG)  return b->type_ulong;
    if (a->kind == TYPE_LONG   || b_type->kind == TYPE_LONG)   return b->type_long;
    return b->type_int;
}

/* ---- conditional branch type merging (D1) ---------------------------------
 * `cond ? then : else` used to be typed as the then branch alone, so a class
 * and a primitive could mix (`cond ? new A() : 123`) and only fail later as
 * an LLVM verification error naming no source line. Merge the branch types
 * like C#: identical types win, numerics promote, a subtype converts to its
 * base, a common base class is used, otherwise the conditional is an error. */

/* Structural equality at the level the checker can see: same kind, same name
 * (generic arguments included). Arrays and nullable compare their element
 * type instead of the name, which is built inconsistently across construction
 * paths (resolve_type keeps "byte[]", make_array_type keeps "byte"). */
static bool checker_type_equal(zan_type_t *a, zan_type_t *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_NULLABLE) {
        if (a->kind == TYPE_ARRAY && a->array_rank != b->array_rank)
            return false; /* int[,] is not int[] */
        return checker_type_equal(a->element_type, b->element_type);
    }
    if (a->name.len != b->name.len ||
        (a->name.len && memcmp(a->name.str, b->name.str, (size_t)a->name.len) != 0))
        return false;
    if (a->type_arg_count != b->type_arg_count) return false;
    for (int i = 0; i < a->type_arg_count; i++)
        if (!checker_type_equal(a->type_args[i], b->type_args[i])) return false;
    return true;
}

/* True when `sub` is, implements, or derives from `sup` -- the same relation
 * type_satisfies_constraint computes for `where T : C` clauses. Depth-guarded
 * so a (defensively) cyclic class hierarchy can never loop forever. */
static bool checker_type_derives_from_depth(zan_type_t *sub, zan_type_t *sup,
                                            int depth) {
    if (!sub || !sup) return false;
    if (depth > CHECKER_DERIVES_MAX_DEPTH) return false;
    if (sub == sup) return true;
    if (sub->sym && sup->sym && sub->sym == sup->sym)
        return checker_type_equal(sub, sup);
    if (sub->kind == sup->kind && checker_type_equal(sub, sup)) return true;
    for (int i = 0; i < sub->interface_count; i++)
        if (checker_type_derives_from_depth(sub->interfaces[i], sup, depth + 1))
            return true;
    if (sub->base_type && sub->base_type != sub)
        return checker_type_derives_from_depth(sub->base_type, sup, depth + 1);
    return false;
}

static bool checker_type_derives_from(zan_type_t *sub, zan_type_t *sup) {
    return checker_type_derives_from_depth(sub, sup, 0);
}

/* Reference kinds: the IR carries them all as one object pointer, so any two
 * of them can share a single PHI; the merged type picks the more general side.
 * Delegates are reference kinds too -- an empty delegate is `null` in Zan, so
 * `del = null` and `return null` for a delegate slot must stay legal. */
static bool checker_type_is_ref(zan_type_t *t) {
    if (!t) return false;
    return t->kind == TYPE_OBJECT || t->kind == TYPE_INTERFACE ||
           t->kind == TYPE_STRING || t->kind == TYPE_CLASS ||
           t->kind == TYPE_ARRAY || t->kind == TYPE_DELEGATE ||
           t->kind == TYPE_TASK; /* Task is a reference type in C#; null is legal */
}

/* Merge the then/else branch types of a conditional expression. NULL means
 * the two are unrelated and the conditional is an error. */
static zan_type_t *merge_conditional_types(zan_checker_t *c,
                                           zan_type_t *a, zan_type_t *b) {
    if (!a) return b;
    if (!b) return a;
    if (a == c->binder->type_error) return b;
    if (b == c->binder->type_error) return a;
    if (a == b) return a;
    /* An unresolved generic parameter: nothing concrete to compare yet; the
     * instantiation site re-checks with real arguments. */
    if (a->kind == TYPE_TYPE_PARAM || b->kind == TYPE_TYPE_PARAM) return a;

    /* identical types (same kind, name and generic arguments) */
    if (a->kind == b->kind && checker_type_equal(a, b)) return a;

    /* numeric promotion: int widens to long, float to double, ... */
    if ((type_is_numeric(a) || a->kind == TYPE_CHAR) &&
        (type_is_numeric(b) || b->kind == TYPE_CHAR))
        return promote_numeric(c->binder, a, b);

    if (checker_type_is_ref(a) && checker_type_is_ref(b)) {
        if (checker_type_derives_from(b, a)) return a;  /* b is-a a */
        if (checker_type_derives_from(a, b)) return b;  /* a is-a b */
        /* common base class: walk a's strict base chain and return the first
         * (nearest) type b also derives from. Depth-guarded so a cyclic class
         * hierarchy cannot loop forever. */
        for (zan_type_t *t = a->base_type; t && t != a; t = t->base_type) {
            if (checker_type_derives_from(b, t)) return t;
            if (!t->base_type) break;
        }
        return NULL; /* unrelated references have no implicit common type */
    }

    /* enums ride an integer carrier at the IR level; enum vs enum was handled
     * above, enum vs int (or int vs enum) matches that carrier. */
    if (a->kind == TYPE_ENUM || b->kind == TYPE_ENUM) return a;

    return NULL; /* unrelated value types, or a value mixed with a reference */
}

/* True when `a` and `b` are the same generic container class (List<...>,
 * Dict<...>, a user Box<...>) regardless of their type arguments. User
 * generics share the class symbol; builtin containers (List/Dict) are created
 * by resolve_type as fresh types with no symbol, so those match by name. */
static bool same_generic_container(zan_type_t *a, zan_type_t *b) {
    if (!a || !b || a->kind != b->kind) return false;
    if (a->type_arg_count != b->type_arg_count || a->type_arg_count == 0)
        return false;
    if (a->sym && b->sym) return a->sym == b->sym;
    return a->name.len == b->name.len &&
           memcmp(a->name.str, b->name.str, (size_t)a->name.len) == 0;
}

static bool checker_is_byte_buffer(zan_type_t *t) {
    if (!t || t->kind != TYPE_ARRAY || !t->element_type) return false;
    return t->element_type->kind == TYPE_BYTE ||
           t->element_type->kind == TYPE_SBYTE ||
           t->element_type->kind == TYPE_CHAR;
}

/* ---- return/assignment compatibility (D2) --------------------------------
 * A return value and an assignment RHS were checked for syntax only, never
 * compared against the declared target type, so `string F() { return 123; }`
 * and `intField = "abc"` compiled and either failed later as a sourceless
 * LLVM verification error or -- worse -- misread the value at runtime (123
 * dereferenced as a string pointer). Compare the value's type against the
 * target; the one-way direction of the conditional merge above. */

/* True when a value of type `value` may be assigned to a target of type
 * `target` (a return statement or an assignment). Numeric widths convert
 * freely (narrowing stays an irgen warning), references share one pointer
 * carrier, a derived class converts to its base. Anything the checker cannot
 * resolve yet -- an error type, an unbound generic parameter -- is deferred
 * rather than rejected. */
static bool checker_type_assignable(zan_type_t *target, zan_type_t *value) {
    if (!target || !value) return true;
    if (target == value) return true;
    if (target->kind == TYPE_ERROR || value->kind == TYPE_ERROR) return true;
    if (target->kind == TYPE_TYPE_PARAM || value->kind == TYPE_TYPE_PARAM)
        return true;
    if (target->kind == value->kind && checker_type_equal(target, value))
        return true;

    /* A Binding<T>-typed slot assigned a raw T value is not a plain
     * assignment: irgen lowers it into a binding construction (const value
     * or live model binding), so the value only needs to fit T. A null
     * literal also fits because the backing Binding<T> is a class object. */
    if (target->kind == TYPE_CLASS && target->name.len == 7 &&
        memcmp(target->name.str, "Binding", 7) == 0 &&
        target->type_arg_count == 1 && target->type_args[0]) {
        if (value->kind == TYPE_OBJECT) return true;
        return checker_type_assignable(target->type_args[0], value);
    }

    /* Nullable value types accept null and the underlying non-nullable value. */
    if (target->kind == TYPE_NULLABLE && value->kind == TYPE_OBJECT)
        return true; /* null -> T? */
    if (target->kind == TYPE_NULLABLE && value->kind != TYPE_NULLABLE &&
        value->kind != TYPE_OBJECT && target->element_type)
        return checker_type_assignable(target->element_type, value); /* T -> T? */

    /* Delegates accept method groups and lambdas (both typed as error here),
     * null (typed as object), and the identical delegate type (already
     * accepted above). Anything with a concrete non-delegate type -- an int,
     * a string, an unrelated class -- is rejected: irgen would call it as a
     * function pointer. */
    if (target->kind == TYPE_DELEGATE)
        return value->kind == TYPE_OBJECT ||
               (value->kind == TYPE_DELEGATE && checker_type_equal(target, value));

    /* Task<T> converts to Task (C# variance on the result type): both are the
     * same coroutine-handle carrier at codegen, and a `Task<int>` read as a
     * plain `Task` loses only the result typing. A bare Task does not convert
     * up to Task<T> -- the result would be missing. */
    if (target->kind == TYPE_TASK && value->kind == TYPE_TASK)
        return value->type_arg_count >= target->type_arg_count;
    bool tnum = type_is_numeric(target), vnum = type_is_numeric(value);
    /* Enums use the integer carrier in IR and retain the language's existing
     * explicit numeric interoperability. Keep these cases before the
     * reference-kind switch below. */
    if (target->kind == TYPE_ENUM)
        return value->kind == TYPE_ENUM || vnum || value->kind == TYPE_CHAR;
    if (value->kind == TYPE_ENUM)
        return tnum || target->kind == TYPE_CHAR;
    if (tnum && (vnum || value->kind == TYPE_CHAR)) return true;
    if (target->kind == TYPE_CHAR && (vnum || value->kind == TYPE_CHAR))
        return true;

    /* Reference kinds: only sound implicit conversions -- null into any
     * reference, a derived class into its base, an implementing class into
     * its interface, and arrays with the same element type. Everything else
     * (an unrelated class, a string into a class, an array of the wrong
     * element type) shares a pointer carrier but not a layout, and must be an
     * explicit cast. */
    if (value->kind == TYPE_OBJECT) {
        return checker_type_is_ref(target); /* null literal */
    }
    switch (target->kind) {
    case TYPE_STRING:
        return value->kind == TYPE_STRING || checker_is_byte_buffer(value);
    case TYPE_CLASS:
        if (value->kind == TYPE_CLASS) {
            /* Inside a generic class body, `this` is represented by the
             * uninstantiated class declaration while a fluent return type is
             * represented as the matching class with its type parameters.
             * They are the same definition; the call-site instantiation fills
             * the arguments later. */
            if (target->sym && value->sym && target->sym == value->sym &&
                (target->type_arg_count == 0 || value->type_arg_count == 0))
                return true;
            /* The same generic class with a mismatched instantiation
             * (List<int> vs List<Box>) is the generic-invariance rule, which
             * irgen reports with its precise diagnostic. Defer so that
             * message surfaces instead of a generic "no implicit conversion". */
            if (same_generic_container(target, value) &&
                !checker_type_equal(target, value))
                return true;
            return checker_type_derives_from(value, target);
        }
        return false;
    case TYPE_INTERFACE:
        if (value->kind == TYPE_INTERFACE || value->kind == TYPE_CLASS ||
            value->kind == TYPE_STRUCT) {
            if (same_generic_container(target, value) &&
                !checker_type_equal(target, value))
                return true;
            return checker_type_derives_from(value, target);
        }
        return false;
    case TYPE_ARRAY:
        return value->kind == TYPE_ARRAY &&
               checker_type_equal(target->element_type, value->element_type);
    case TYPE_OBJECT:
        return checker_type_is_ref(value);
    default:
        return false;
    }

    return false;
}

static bool integral_range(zan_type_t *t, int64_t *min_value,
                           uint64_t *max_value, bool *is_unsigned) {
    if (!t) return false;
    *is_unsigned = false;
    switch (t->kind) {
    case TYPE_SBYTE:  *min_value = -128; *max_value = 127; return true;
    case TYPE_BYTE:   *min_value = 0; *max_value = 255; *is_unsigned = true; return true;
    case TYPE_SHORT:  *min_value = -32768; *max_value = 32767; return true;
    case TYPE_USHORT: *min_value = 0; *max_value = 65535; *is_unsigned = true; return true;
    case TYPE_CHAR:   *min_value = 0; *max_value = 65535; *is_unsigned = true; return true;
    case TYPE_INT:    *min_value = INT32_MIN; *max_value = INT32_MAX; return true;
    case TYPE_UINT:   *min_value = 0; *max_value = UINT32_MAX; *is_unsigned = true; return true;
    case TYPE_LONG:
    case TYPE_NINT:   *min_value = INT64_MIN; *max_value = INT64_MAX; return true;
    case TYPE_ULONG:  *min_value = 0; *max_value = UINT64_MAX; *is_unsigned = true; return true;
    case TYPE_ENUM:   *min_value = INT32_MIN; *max_value = INT32_MAX; return true;
    default: return false;
    }
}

static bool const_integral_value(zan_ast_node_t *expr, int64_t *value) {
    if (!expr) return false;
    if (expr->kind == AST_INT_LITERAL) {
        *value = expr->int_val;
        return true;
    }
    if (expr->kind == AST_CHAR_LITERAL) {
        *value = expr->int_val;
        return true;
    }
    if (expr->kind == AST_UNARY && expr->unary.op == TK_MINUS &&
        expr->unary.operand && expr->unary.operand->kind == AST_INT_LITERAL) {
        *value = -expr->unary.operand->int_val;
        return true;
    }
    return false;
}

static bool integral_conversion_is_safe(zan_type_t *target, zan_type_t *value,
                                        zan_ast_node_t *expr) {
    int64_t tmin, vmin, constant;
    uint64_t tmax, vmax;
    bool tu, vu;
    if (!integral_range(target, &tmin, &tmax, &tu) ||
        !integral_range(value, &vmin, &vmax, &vu))
        return true;
    if ((!vu || tu) && vmin >= tmin && vmax <= tmax) return true;
    if (!const_integral_value(expr, &constant)) return false;
    if (constant < tmin) return false;
    if (tu) {
        if (constant < 0) return false;
        return (uint64_t)constant <= tmax;
    }
    return constant <= (int64_t)tmax;
}

/* Emit the D2 diagnostic when `value` is not assignable to `target`. Numeric
 * conversions follow C#-style implicit rules: integral widening is allowed,
 * fitting constants may narrow, and every float-to-integral conversion needs
 * an explicit cast. */
/* A user-defined implicit conversion (`static implicit operator T2(T1 v)`)
 * makes a T1 value assignable to a T2 slot. The method is lowered by the
 * parser as a static `op_implicit` member; like C#, it may be declared on
 * either the source type (T1) or the target type (T2). */
static bool checker_conversion_method(zan_checker_t *c, zan_symbol_t *sym,
                                      zan_type_t *from, zan_type_t *to,
                                      const char *op_name) {
    if (!sym) return false;
    zan_istr_t op = { (char *)op_name, (int)strlen(op_name) };
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (!m || m->kind != SYM_METHOD || !(m->modifiers & MOD_STATIC)) continue;
        if (!m->decl || m->decl->kind != AST_METHOD_DECL) continue;
        if (m->name.len != op.len ||
            memcmp(m->name.str, op.str, (size_t)op.len) != 0) continue;
        zan_type_t *rt = zan_binder_resolve_type(c->binder,
            m->decl->method_decl.return_type);
        if (!rt || !checker_type_equal(rt, to)) continue;
        zan_ast_list_t *ps = &m->decl->method_decl.params;
        if (ps->count != 1 || !ps->items[0] || ps->items[0]->kind != AST_PARAM)
            continue;
        zan_type_t *pt = zan_binder_resolve_type(c->binder,
            ps->items[0]->param.type);
        if (pt && checker_type_equal(pt, from)) return true;
    }
    return false;
}

static bool checker_user_conversion(zan_checker_t *c, zan_type_t *target,
                                    zan_type_t *value, const char *op_name) {
    if (!value || !target) return false;
    if (value->kind == TYPE_CLASS || value->kind == TYPE_STRUCT) {
        if (checker_conversion_method(c, value->sym, value, target, op_name))
            return true;
    }
    if (target->kind == TYPE_CLASS || target->kind == TYPE_STRUCT) {
        if (checker_conversion_method(c, target->sym, value, target, op_name))
            return true;
    }
    return false;
}

static bool checker_has_user_conversion(zan_checker_t *c, zan_type_t *target,
                                        zan_type_t *value) {
    return checker_user_conversion(c, target, value, "op_implicit");
}

/* Everything that lowers to a numeric register and so converts to any other
 * such type by a value conversion. bool is deliberately absent: C# has no
 * conversion between bool and a number in either direction. */
static bool cast_is_scalar(zan_type_t *t) {
    return t && (type_is_numeric(t) || t->kind == TYPE_CHAR ||
                 t->kind == TYPE_ENUM || t->kind == TYPE_NINT);
}

/* Whether `(dst)src` is a conversion the language has. Explicit casts share a
 * carrier with the target for most kinds, so an unchecked cast reinterprets
 * whatever bits it is given -- `(int)"abc"` handed back the string's pointer
 * and `(double)obj` a denormal. Anything the checker cannot judge (an error
 * type, an unsubstituted type parameter, object, nint) is left alone. */
static bool checker_cast_is_valid(zan_checker_t *c, zan_type_t *dst,
                                  zan_type_t *src) {
    if (!dst || !src) return true;
    if (dst == c->binder->type_error || src == c->binder->type_error ||
        src == c->binder->type_void)
        return true;
    if (dst->kind == TYPE_TYPE_PARAM || src->kind == TYPE_TYPE_PARAM) return true;
    /* object is the boxed carrier (and the null literal's type). */
    if (dst->kind == TYPE_OBJECT || src->kind == TYPE_OBJECT) return true;
    /* nint is the native pointer/handle carrier of the interop surface, and a
     * delegate is built from one; both convert to and from anything. */
    if (dst->kind == TYPE_NINT || src->kind == TYPE_NINT) return true;
    if (dst->kind == TYPE_DELEGATE || src->kind == TYPE_DELEGATE) return true;
    if (checker_type_equal(dst, src)) return true;
    if (dst->kind == TYPE_NULLABLE || src->kind == TYPE_NULLABLE)
        return checker_cast_is_valid(c, checker_nullable_base(dst),
                                     checker_nullable_base(src));
    if (cast_is_scalar(dst) && cast_is_scalar(src)) return true;
    /* A reference conversion (up, down or to an interface) is checked at run
     * time, so any pair of reference types is a legal cast to write. */
    if (checker_type_is_ref(dst) && checker_type_is_ref(src)) return true;
    if (checker_user_conversion(c, dst, src, "op_explicit")) return true;
    if (checker_user_conversion(c, dst, src, "op_implicit")) return true;
    return checker_type_assignable(dst, src);
}

static void checker_check_assignable(zan_checker_t *c, zan_type_t *target,
                                     zan_type_t *value, zan_ast_node_t *expr,
                                     zan_loc_t loc, const char *what) {
    if (!target || !value || target == c->binder->type_error ||
        value == c->binder->type_error || value == c->binder->type_void)
        return;
    bool target_integral = type_is_integral(target) || target->kind == TYPE_CHAR ||
                           target->kind == TYPE_ENUM;
    bool value_integral = type_is_integral(value) || value->kind == TYPE_CHAR ||
                          value->kind == TYPE_ENUM;
    if (target_integral &&
        (value->kind == TYPE_FLOAT || value->kind == TYPE_DOUBLE)) {
        zan_diag_emit(c->diag, DIAG_ERROR, loc,
                      "narrowing conversion from '%s' to '%s' in %s needs an "
                      "explicit cast (C# rules)",
                      type_name(value), type_name(target), what);
        return;
    }
    if (target_integral && value_integral) {
        int64_t constant = 0;
        bool has_constant = const_integral_value(expr, &constant);
        bool native_handle_narrowing = value->kind == TYPE_NINT &&
            target->kind != TYPE_LONG && target->kind != TYPE_NINT;
        bool uint32_bit_pattern = value->kind == TYPE_LONG &&
            target->kind == TYPE_INT && has_constant && constant >= 0 &&
            (uint64_t)constant <= UINT32_MAX;
        /* The lexer preserves an unsuffixed/hex literal's full uint64 bit
         * pattern in int_val, so direct ulong literals above INT64_MAX appear
         * negative here. They are still exact ulong constants; a unary minus
         * remains a real negative expression and is rejected below. */
        bool ulong_literal = target->kind == TYPE_ULONG && expr &&
            expr->kind == AST_INT_LITERAL;
        if (native_handle_narrowing ||
            (!uint32_bit_pattern && !ulong_literal && has_constant &&
             !integral_conversion_is_safe(target, value, expr))) {
            zan_diag_emit(c->diag, DIAG_ERROR, loc,
                          "narrowing conversion from '%s' to '%s' in %s needs an "
                          "explicit cast (C# rules)",
                          type_name(value), type_name(target), what);
            return;
        }
    }
    if (checker_type_assignable(target, value)) return;
    if (checker_has_user_conversion(c, target, value)) return;
    zan_diag_emit(c->diag, DIAG_ERROR, loc,
                  "cannot convert '%s' to '%s' in %s: no implicit conversion",
                  type_name(value), type_name(target), what);
}

/* The member-access rules, taking the object's type as an argument instead of
 * inferring it. A call on a chain (`app.Map(..).Named(..).Auth()`) needs the
 * receiver's type twice -- once to decide whether it is a builtin scalar, once
 * to resolve the member -- and computing it twice made every link double the
 * work below it, so a fluent chain cost 2^links and a generated route table
 * never finished checking at all. */
static zan_type_t *check_member_access(zan_checker_t *c, zan_ast_node_t *expr,
                                       zan_type_t *obj_type);
static void checker_reject_null_receiver(zan_checker_t *c, zan_ast_node_t *expr);

/* How many arguments `m` accepts: [min..max], min counting the parameters
 * without a default. Returns false when the count cannot be decided here --
 * a variadic `params T[]` tail, or an extension method, whose receiver is a
 * parameter of the declaration but not an argument of the call. */
static bool method_arity(zan_symbol_t *m, int *min, int *max) {
    if (!m->decl || m->decl->kind != AST_METHOD_DECL) return false;
    zan_ast_list_t *ps = &m->decl->method_decl.params;
    int lo = 0;
    for (int i = 0; i < ps->count; i++) {
        zan_ast_node_t *p = ps->items[i];
        if (!p || p->kind != AST_PARAM) return false;
        if (p->param.is_params || p->param.is_this) return false;
        if (!p->param.default_val) lo++;
    }
    *min = lo;
    *max = ps->count;
    return true;
}

/* Reject a call that passes the wrong number of arguments. Without this the
 * mismatch survived every source-level phase and only turned up as an LLVM
 * verifier failure ("Incorrect number of arguments passed to called
 * function"), which names a mangled symbol and no source line. Only method
 * calls on a known type are judged, and only when the count fits no declared
 * overload, so anything the checker cannot see (builtin scalar methods,
 * delegates, compiler-lowered members) is left alone. */
static void check_call_arity(zan_checker_t *c, zan_ast_node_t *call,
                             zan_type_t *recv) {
    zan_ast_node_t *callee = call->call.callee;
    if (!callee || callee->kind != AST_MEMBER_ACCESS) return;
    if (!recv || !recv->sym) return;
    zan_istr_t name = callee->member.name;
    int argc = call->call.args.count;
    zan_compile_trace("arity? %.*s.%.*s argc=%d members=%d",
                    (int)recv->sym->name.len, recv->sym->name.str,
                    (int)name.len, name.str, argc, recv->sym->member_count);
    int candidates = 0, want_min = 0, want_max = 0;
    for (zan_symbol_t *s = recv->sym; s;
         s = (s->type && s->type->base_type) ? s->type->base_type->sym : NULL) {
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            int lo, hi;
            if (!m || m->kind != SYM_METHOD || m->name.len != name.len ||
                memcmp(m->name.str, name.str, (size_t)name.len) != 0)
                continue;
            if (!method_arity(m, &lo, &hi)) return;
            if (argc >= lo && argc <= hi) return;
            if (!candidates) { want_min = lo; want_max = hi; }
            candidates++;
        }
    }
    if (!candidates) return;
    if (candidates > 1) {
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "no overload of '%.*s.%.*s' takes %d argument%s",
                      (int)recv->sym->name.len, recv->sym->name.str,
                      (int)name.len, name.str, argc, argc == 1 ? "" : "s");
        return;
    }
    if (want_min == want_max) {
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "'%.*s.%.*s' takes %d argument%s, but %d given",
                      (int)recv->sym->name.len, recv->sym->name.str,
                      (int)name.len, name.str, want_min,
                      want_min == 1 ? "" : "s", argc);
        return;
    }
    zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                  "'%.*s.%.*s' takes %d to %d arguments, but %d given",
                  (int)recv->sym->name.len, recv->sym->name.str,
                  (int)name.len, name.str, want_min, want_max, argc);
}

/* Whether an argument of type `value` cannot reach a parameter of type
 * `target`. Only class/struct types are judged, and only when neither side is
 * a generic instantiation: passing an unrelated class (Theme where App is
 * declared) shares the pointer carrier but not the layout, so it survived
 * every source-level phase and read foreign fields at runtime. Numerics,
 * interfaces, delegates, arrays and generics are left to the existing
 * assignment/irgen paths. */
static bool checker_arg_type_mismatch(zan_checker_t *c, zan_type_t *target,
                                      zan_type_t *value) {
    if (!target || !value) return false;
    if (target->kind != TYPE_CLASS && target->kind != TYPE_STRUCT) return false;
    if (value->kind != TYPE_CLASS && value->kind != TYPE_STRUCT) return false;
    if (!target->sym || !value->sym) return false;
    if (target->type_arg_count || value->type_arg_count) return false;
    if (checker_type_assignable(target, value)) return false;
    return !checker_has_user_conversion(c, target, value);
}

/* The declaration whose parameters line up one-for-one with this call's
 * arguments, or NULL when the checker cannot match them by position: an
 * overloaded name (irgen picks the overload), a generic method, a `params`
 * tail or an extension receiver (the declaration has a parameter the call has
 * no argument for), and named or `ref`/`out` arguments (reordered or passed by
 * reference). */
static zan_symbol_t *call_arg_signature(zan_ast_node_t *call,
                                        zan_type_t *recv) {
    zan_ast_node_t *callee = call->call.callee;
    if (!callee || callee->kind != AST_MEMBER_ACCESS) return NULL;
    if (!recv || !recv->sym || recv->type_arg_count) return NULL;
    zan_istr_t name = callee->member.name;
    zan_symbol_t *only = NULL;
    for (zan_symbol_t *s = recv->sym; s;
         s = (s->type && s->type->base_type) ? s->type->base_type->sym : NULL) {
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            if (!m || m->kind != SYM_METHOD || m->name.len != name.len ||
                memcmp(m->name.str, name.str, (size_t)name.len) != 0)
                continue;
            if (only && only != m) return NULL;
            only = m;
        }
    }
    if (!only || !only->decl || only->decl->kind != AST_METHOD_DECL) return NULL;
    if (only->decl->method_decl.type_params.count) return NULL;
    zan_ast_list_t *ps = &only->decl->method_decl.params;
    if (call->call.args.count > ps->count) return NULL;
    for (int i = 0; i < ps->count; i++) {
        zan_ast_node_t *p = ps->items[i];
        if (!p || p->kind != AST_PARAM || p->param.is_params || p->param.is_this)
            return NULL;
    }
    for (int i = 0; i < call->call.args.count; i++) {
        zan_ast_node_t *a = call->call.args.items[i];
        if (!a || a->kind == AST_NAMED_ARG || a->kind == AST_REF_ARG) return NULL;
    }
    return only;
}

static void check_call_arg_type(zan_checker_t *c, zan_symbol_t *sig, int index,
                                zan_ast_node_t *arg, zan_type_t *arg_type) {
    zan_ast_list_t *ps = &sig->decl->method_decl.params;
    if (index >= ps->count) return;
    zan_type_t *pt = zan_binder_resolve_type(c->binder,
                                             ps->items[index]->param.type);
    if (!checker_arg_type_mismatch(c, pt, arg_type)) return;
    zan_diag_emit(c->diag, DIAG_ERROR, arg->loc,
                  "cannot convert '%s' to '%s' in argument %d of '%.*s': "
                  "no implicit conversion",
                  type_name(arg_type), type_name(pt), index + 1,
                  (int)sig->name.len, sig->name.str);
}

/* Comparisons, which consume the *value* of both operands. `+`/`-` are left
 * out: they combine delegates and register event handlers, both of which take
 * a method group. */
static bool binary_op_compares(zan_token_kind_t op) {
    switch (op) {
    case TK_EQ_EQ: case TK_BANG_EQ:
    case TK_LESS: case TK_GREATER: case TK_LESS_EQ: case TK_GREATER_EQ:
        return true;
    default:
        return false;
    }
}

/* The method a value-position member access names, if any: `set.Count`
 * without an argument list is a method group. Only simple receivers are
 * resolved, so no expression is type-checked (and no diagnostic duplicated)
 * on this path. */
static zan_symbol_t *expr_method_group(zan_checker_t *c, zan_ast_node_t *e) {
    if (!e || e->kind != AST_MEMBER_ACCESS) return NULL;
    zan_ast_node_t *obj = e->member.object;
    if (!obj || (obj->kind != AST_IDENTIFIER && obj->kind != AST_THIS_EXPR))
        return NULL;
    zan_type_t *ot = zan_checker_check_expr(c, obj);
    if (!ot || !ot->sym) return NULL;
    for (int i = ot->sym->member_count - 1; i >= 0; i--) {
        zan_symbol_t *m = ot->sym->members[i];
        if (m->name.len == e->member.name.len &&
            memcmp(m->name.str, e->member.name.str, (size_t)m->name.len) == 0)
            return m->kind == SYM_METHOD ? m : NULL;
    }
    return NULL;
}

static void reject_method_group(zan_checker_t *c, zan_ast_node_t *e) {
    zan_symbol_t *m = expr_method_group(c, e);
    if (!m) return;
    zan_diag_emit(c->diag, DIAG_ERROR, e->loc,
                  "'%.*s' is a method, not a value; call it as '%.*s()'",
                  (int)m->name.len, m->name.str,
                  (int)m->name.len, m->name.str);
}

zan_type_t *zan_checker_check_expr(zan_checker_t *c, zan_ast_node_t *expr) {
    if (!expr) return c->binder->type_error;

    switch (expr->kind) {
    case AST_INT_LITERAL:
        /* Type an integer literal by its value (and suffix) so it agrees with
         * IRGen and so a narrowing target (`int x = 0xFFFFFFFF`) is
         * diagnosable: a value outside i32 is `long`. A suffix pins the type:
         * L/l -> long, U/u -> uint (ulong when the value overflows uint),
         * UL/LU -> ulong. */
        switch (expr->lit_suffix) {
        case 1:
            return c->binder->type_long;
        case 2:
            if (expr->int_val < 0 || expr->int_val > 4294967295LL)
                return c->binder->type_ulong;
            return c->binder->type_uint;
        case 3:
            return c->binder->type_ulong;
        default:
            if (expr->int_val < -2147483648LL || expr->int_val > 2147483647LL)
                return c->binder->type_long;
            return c->binder->type_int;
        }
    case AST_FLOAT_LITERAL:
        return c->binder->type_double;
    case AST_STRING_LITERAL:
        return c->binder->type_string;
    case AST_CHAR_LITERAL:
        return c->binder->type_char;
    case AST_BOOL_LITERAL:
        return c->binder->type_bool;
    case AST_NULL_LITERAL:
        return c->binder->type_object;

    case AST_IDENTIFIER: {
        /* A bare name in an expression resolves local-first, exactly like
         * irgen (emit_expr_identifier): a parameter/local shadows a field of
         * the same name, and the enclosing type's field shadows an in-scope
         * type of the same name (`string Icon;` beats `using Gui;`'s
         * `class Icon`). The old ref-carrier rules masked these mismatches;
         * the strict assignability/initializer checks surfaced them as bogus
         * "cannot convert 'Icon' to 'string'" / "type 'int' has no member"
         * errors. Only when no local or field matches do we fall back to
         * scope lookup, which is what finds types, enum members and method
         * names. */
        zan_type_t *lt = checker_find_local(c, expr->ident.name);
        if (lt) return lt;
        if (c->current_type_sym) {
            zan_symbol_t *fsym = checker_find_field(c->current_type_sym,
                                                    expr->ident.name);
            if (fsym) {
                return fsym->type ? fsym->type : c->binder->type_error;
            }
        }
        zan_symbol_t *sym = zan_binder_lookup(c->binder, expr->ident.name);
        if (!sym) {
            /* don't error — may be resolved in later phases */
            return c->binder->type_error;
        }
        return sym->type;
    }

    case AST_BINARY: {
        zan_type_t *left = zan_checker_check_expr(c, expr->binary.left);
        zan_type_t *right = zan_checker_check_expr(c, expr->binary.right);
        /* A method group as an operand is a forgotten '()'. It types as
         * type_error, which every operand rule below waves through, and irgen
         * then lowers the group to a closure pointer -- `set.Count == 2`
         * reached LLVM as `icmp eq ptr, i64`. Delegate operands (event
         * `+=`/`-=`, delegate combination) legitimately take one, so the
         * report is limited to comparisons with a non-delegate other side. */
        if (binary_op_compares(expr->binary.op)) {
            if (right->kind != TYPE_DELEGATE)
                reject_method_group(c, expr->binary.left);
            if (left->kind != TYPE_DELEGATE)
                reject_method_group(c, expr->binary.right);
        }

        switch (expr->binary.op) {
        case TK_PLUS:
            /* string concatenation */
            if (left->kind == TYPE_STRING || right->kind == TYPE_STRING) {
                no_runtime_reject(c, expr->loc, "string concatenation");
                return c->binder->type_string;
            }
            /* fall through */
        case TK_MINUS: case TK_STAR: case TK_SLASH: case TK_PERCENT:
            if (type_is_numeric(left) && type_is_numeric(right)) {
                return promote_numeric(c->binder, left, right);
            }
            /* Lifted nullable arithmetic: `int? + 1` and `int? + int?` operate
             * on the payload and yield the nullable type again. Non-numeric
             * payloads fall through to the class/struct overload path. */
            if (left->kind == TYPE_NULLABLE || right->kind == TYPE_NULLABLE) {
                zan_type_t *lb = checker_nullable_base(left);
                zan_type_t *rb = checker_nullable_base(right);
                if (type_is_numeric(lb) && type_is_numeric(rb))
                    return left->kind == TYPE_NULLABLE ? left : right;
            }
            /* Enums ride an integer carrier: irgen compiles `int + enum` as
             * plain integer arithmetic (the stdlib folds enum flags into
             * hash sums), so accept them as integers here too. promote_numeric
             * already defaults an enum operand to int. */
            /* char is an integer in arithmetic (C#: `c1 + c2` is an int),
             * which is why `(char)(c1 + c2)` is the way to add two of them. */
            if ((type_is_numeric(left) || left->kind == TYPE_ENUM ||
                 left->kind == TYPE_CHAR) &&
                (type_is_numeric(right) || right->kind == TYPE_ENUM ||
                 right->kind == TYPE_CHAR)) {
                return promote_numeric(c->binder, left, right);
            }
            /* User-defined operator overloading: `a + b` on a class/struct left
             * operand dispatches to a static op_add/op_sub/... method resolved
             * in irgen. Assume the result is the left operand's type (e.g. an
             * event `+= handler` yields the event) rather than rejecting it. */
            if (left->kind == TYPE_CLASS || left->kind == TYPE_STRUCT) {
                return left;
            }
            if (left->kind != TYPE_ERROR && right->kind != TYPE_ERROR) {
                zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                              "cannot apply operator to '%s' and '%s'",
                              type_name(left), type_name(right));
            }
            return c->binder->type_error;

        case TK_AMP: case TK_PIPE: case TK_CARET:
        case TK_LESS_LESS: case TK_GREATER_GREATER:
            if ((type_is_integral(left) || left->kind == TYPE_CHAR ||
                 left->kind == TYPE_ENUM) &&
                (type_is_integral(right) || right->kind == TYPE_CHAR ||
                 right->kind == TYPE_ENUM)) {
                return promote_numeric(c->binder, left, right);
            }
            return c->binder->type_error;

        case TK_EQ_EQ: case TK_BANG_EQ: {
            bool ln = type_is_numeric(left) || left->kind == TYPE_CHAR ||
                      left->kind == TYPE_ENUM;
            bool rn = type_is_numeric(right) || right->kind == TYPE_CHAR ||
                      right->kind == TYPE_ENUM;
            if (ln && rn) return c->binder->type_bool;
            /* Nullable value equality: `int? == null`, `int? == int` and
             * `int? == int?` are compared on the payload (irgen lowers the
             * lifted comparison). */
            if (left->kind == TYPE_NULLABLE || right->kind == TYPE_NULLABLE) {
                zan_type_t *lb = checker_nullable_base(left);
                zan_type_t *rb = checker_nullable_base(right);
                if (left->kind == TYPE_OBJECT || right->kind == TYPE_OBJECT)
                    return c->binder->type_bool;
                bool l2 = type_is_numeric(lb) || lb->kind == TYPE_CHAR ||
                          lb->kind == TYPE_ENUM;
                bool r2 = type_is_numeric(rb) || rb->kind == TYPE_CHAR ||
                          rb->kind == TYPE_ENUM;
                if (l2 && r2) return c->binder->type_bool;
                if (lb->kind == TYPE_BOOL && rb->kind == TYPE_BOOL)
                    return c->binder->type_bool;
                if (checker_type_assignable(lb, rb) ||
                    checker_type_assignable(rb, lb))
                    return c->binder->type_bool;
            }
            /* The same value struct compares field-by-field (lowered in
             * irgen); unrelated structs do not. */
            if (left->kind == TYPE_STRUCT && right->kind == TYPE_STRUCT &&
                left->sym && left->sym == right->sym)
                return c->binder->type_bool;
            /* A generic body compares its own type parameter (`T a == T b`);
             * the comparison is instantiated per use site. */
            if (left->kind == TYPE_TYPE_PARAM && right->kind == TYPE_TYPE_PARAM &&
                checker_type_equal(left, right))
                return c->binder->type_bool;
            /* Reference equality is valid for null and types connected by an
             * implicit reference conversion. Sharing the LLVM pointer carrier
             * alone is not enough: unrelated object layouts must not compare
             * as though they were compatible source types. */
            if (checker_type_is_ref(left) && checker_type_is_ref(right) &&
                (left->kind == TYPE_OBJECT || right->kind == TYPE_OBJECT ||
                 checker_type_assignable(left, right) ||
                 checker_type_assignable(right, left)))
                return c->binder->type_bool;
            if (left->kind == TYPE_BOOL && right->kind == TYPE_BOOL)
                return c->binder->type_bool;
            if (left->kind != TYPE_ERROR && right->kind != TYPE_ERROR)
                zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                              "cannot compare '%s' and '%s'",
                              type_name(left), type_name(right));
            return c->binder->type_error;
        }
        case TK_LESS: case TK_GREATER:
        case TK_LESS_EQ: case TK_GREATER_EQ: {
            bool ln = type_is_numeric(left) || left->kind == TYPE_CHAR ||
                      left->kind == TYPE_ENUM;
            bool rn = type_is_numeric(right) || right->kind == TYPE_CHAR ||
                      right->kind == TYPE_ENUM;
            if (ln && rn) return c->binder->type_bool;
            /* Lifted nullable relational: `int? > 3` compares the payloads
             * (null yields false in irgen). */
            if (left->kind == TYPE_NULLABLE || right->kind == TYPE_NULLABLE) {
                zan_type_t *lb = checker_nullable_base(left);
                zan_type_t *rb = checker_nullable_base(right);
                bool l2 = type_is_numeric(lb) || lb->kind == TYPE_CHAR ||
                          lb->kind == TYPE_ENUM;
                bool r2 = type_is_numeric(rb) || rb->kind == TYPE_CHAR ||
                          rb->kind == TYPE_ENUM;
                if (l2 && r2) return c->binder->type_bool;
            }
            /* A generic body orders its own type parameter (`R a < R b`);
             * the comparison is instantiated per use site. */
            if (left->kind == TYPE_TYPE_PARAM && right->kind == TYPE_TYPE_PARAM &&
                checker_type_equal(left, right))
                return c->binder->type_bool;
            /* irgen routes string ordering through strcmp; the stdlib
             * compares single-char substrings with `<`/`<=`/`>`/`>=` (URL
             * encoding, markdown list detection), so accept it here too */
            if (left->kind == TYPE_STRING && right->kind == TYPE_STRING)
                return c->binder->type_bool;
            /* User-defined relational overload: `a < b` on a class/struct left
             * operand dispatches to a static op_lt/op_gt/op_le/op_ge method
             * resolved in irgen (mirrors the arithmetic operator path above).
             * Assume the result is bool rather than rejecting it. */
            if ((left->kind == TYPE_CLASS || left->kind == TYPE_STRUCT) &&
                left->sym) {
                const char *rel_name = NULL;
                switch (expr->binary.op) {
                case TK_LESS:       rel_name = "op_lt"; break;
                case TK_GREATER:    rel_name = "op_gt"; break;
                case TK_LESS_EQ:    rel_name = "op_le"; break;
                case TK_GREATER_EQ: rel_name = "op_ge"; break;
                default: break;
                }
                if (rel_name) {
                    zan_istr_t rn = {(char *)rel_name, (int)strlen(rel_name)};
                    if (checker_find_method(left->sym, rn))
                        return c->binder->type_bool;
                }
            }
            if (left->kind != TYPE_ERROR && right->kind != TYPE_ERROR)
                zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                              "cannot compare '%s' and '%s' with a relational "
                              "operator: no implicit numeric conversion",
                              type_name(left), type_name(right));
            return c->binder->type_error;
        }

        case TK_AMP_AMP: case TK_PIPE_PIPE:
            if ((left->kind == TYPE_BOOL && right->kind == TYPE_BOOL) ||
                left->kind == TYPE_ERROR || right->kind == TYPE_ERROR)
                return c->binder->type_bool;
            zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                          "cannot apply logical operator to '%s' and '%s': "
                          "both operands must be bool",
                          type_name(left), type_name(right));
            return c->binder->type_error;

        case TK_QUESTION_QUESTION: {
            /* `a ?? b`: nullish types (nullable value types and reference
             * kinds) on either side merge to the most specific of the two,
             * like a conditional branch -- `x ?? "fallback"` with x:object
             * yields string, and `int? x; x ?? 0` yields int. */
            if (left->kind == TYPE_NULLABLE && left->element_type) {
                zan_type_t *merged = merge_conditional_types(c,
                                                              left->element_type,
                                                              right);
                if (merged) return merged;
            } else if (checker_type_is_ref(left) || checker_type_is_ref(right) ||
                       right->kind == TYPE_OBJECT ||
                       right->kind == TYPE_NULLABLE) {
                zan_type_t *merged = merge_conditional_types(c, left, right);
                if (merged) return merged;
            }
            if (left->kind != TYPE_ERROR && right->kind != TYPE_ERROR)
                zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                              "cannot apply '??' to '%s' and '%s'",
                              type_name(left), type_name(right));
            return c->binder->type_error;
        }

        default:
            return c->binder->type_error;
        }
    }

    case AST_UNARY: {
        zan_type_t *operand = zan_checker_check_expr(c, expr->unary.operand);
        switch (expr->unary.op) {
        case TK_MINUS:
            if (type_is_numeric(operand)) return operand;
            return c->binder->type_error;
        case TK_BANG:
            return c->binder->type_bool;
        case TK_TILDE:
            if (type_is_integral(operand)) return operand;
            return c->binder->type_error;
        case TK_PLUS_PLUS: case TK_MINUS_MINUS:
            check_readonly_incdec(c, expr);
            if (type_is_numeric(operand)) return operand;
            return c->binder->type_error;
        default:
            return c->binder->type_error;
        }
    }

    case AST_POSTFIX_UNARY: {
        zan_type_t *operand = zan_checker_check_expr(c, expr->unary.operand);
        check_readonly_incdec(c, expr);
        if (type_is_numeric(operand)) return operand;
        return c->binder->type_error;
    }

    case AST_CALL: {
        /* Builtin scalar instance methods (string.Trim/Substring/EndsWith/...)
         * are lowered by irgen from the member name alone; they have no symbol
         * for the checker to validate. A method group on a scalar type is
         * therefore skipped here — a bare (non-call) member access on a scalar
         * is still rejected in the AST_MEMBER_ACCESS case. */
        zan_type_t *callee_type;
        zan_type_t *recv = NULL;
        /* A call to a *method* is typed by the method's declared return type
         * (which may itself be a delegate — `MessageHandlerOf()` returns the
         * delegate, not the delegate's result). Only an invocation of a
         * *delegate value* (a field/param/local of delegate type) collapses to
         * the delegate's return type. The member-access case is resolved here
         * so the two are not conflated. */
        int callee_is_method = 0;
        zan_symbol_t *called_sym = NULL;
        /* EnumType.TryParse(text, out value): a compiler-lowered static over
         * the enum's declaration-order name table. The enum carries no method
         * symbol for the generic member path to find, so resolve it here and
         * type the call bool; irgen emits the strcmp probe chain. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_IDENTIFIER &&
            expr->call.args.count == 2 &&
            expr->call.callee->member.name.len == 8 &&
            memcmp(expr->call.callee->member.name.str, "TryParse", 8) == 0) {
            zan_symbol_t *es = zan_binder_lookup(c->binder,
                expr->call.callee->member.object->ident.name);
            if (es && es->kind == SYM_ENUM) {
                for (int ai = 0; ai < 2; ai++)
                    zan_checker_check_expr(c, expr->call.args.items[ai]);
                return c->binder->type_bool;
            }
        }
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            recv = zan_checker_check_expr(c, expr->call.callee->member.object);
            if (recv && recv->sym) {
                zan_symbol_t *m = checker_find_method(recv->sym,
                                                      expr->call.callee->member.name);
                if (m && m->kind == SYM_METHOD) {
                    /* the unresolved path below reports through
                     * check_member_access, so report here only for the
                     * resolved-method fast path */
                    checker_reject_null_receiver(c, expr->call.callee);
                    called_sym = m;
                    callee_type = m->type ? m->type : c->binder->type_error;
                    callee_is_method = 1;
                }
            }
            if (!callee_is_method) {
                callee_type = type_is_scalar_primitive(recv)
                    ? c->binder->type_error
                    : check_member_access(c, expr->call.callee, recv);
            }
        } else {
            callee_type = zan_checker_check_expr(c, expr->call.callee);
        }
        zan_symbol_t *arg_sig = call_arg_signature(expr, recv);
        for (int i = 0; i < expr->call.args.count; i++) {
            zan_ast_node_t *arg = expr->call.args.items[i];
            if (arg && arg->kind == AST_NAMED_ARG) arg = arg->named_arg.expr;
            zan_type_t *arg_type = zan_checker_check_expr(c, arg);
            if (arg_sig && arg)
                check_call_arg_type(c, arg_sig, i, arg, arg_type);
        }
        /* Published after the arguments are checked (they are calls too), so a
         * member access on this call reads *this* call's callee. */
        if (!called_sym && expr->call.callee &&
            expr->call.callee->kind == AST_IDENTIFIER)
            called_sym = zan_binder_lookup(c->binder, expr->call.callee->ident.name);
        c->last_call_node = expr;
        c->last_call_method = called_sym;
        check_call_arity(c, expr, recv);
        if (!callee_is_method && callee_type &&
            callee_type->kind == TYPE_DELEGATE) {
            return callee_type->delegate_ret_type
                ? callee_type->delegate_ret_type
                : c->binder->type_void;
        }
        /* op_call operator: `<class instance>(args)` has the op_call method's
         * declared return type. */
        if (callee_type && (callee_type->kind == TYPE_CLASS ||
                            callee_type->kind == TYPE_STRUCT) && callee_type->sym) {
            zan_istr_t op_name = {(char *)"op_call", 7};
            zan_symbol_t *op = checker_find_method(callee_type->sym, op_name);
            if (op && op->decl && op->decl->kind == AST_METHOD_DECL &&
                op->decl->method_decl.return_type) {
                return zan_binder_resolve_type(c->binder,
                    op->decl->method_decl.return_type);
            }
            if (op && op->type) return op->type;
        }
        /* Resolve the callee's return type when the function/method symbol
         * is in scope. Fall back to type_error (NOT void) for unresolved
         * calls so that using a call result in an expression — e.g. the
         * recursive `Fib(n-1) + Fib(n-2)` — does not raise a spurious
         * "cannot apply operator to 'void'..." error. */
        if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER) {
            zan_symbol_t *fsym = zan_binder_lookup(c->binder, expr->call.callee->ident.name);
            if (fsym && (fsym->kind == SYM_METHOD) && fsym->decl) {
                zan_ast_node_t *m = fsym->decl;
                if (m->method_decl.return_type) {
                    return zan_binder_resolve_type(c->binder, m->method_decl.return_type);
                }
                return c->binder->type_void; /* explicit void return */
            }
        }
        return c->binder->type_error;
    }

    case AST_STRING_INTERP: {
        no_runtime_reject(c, expr->loc, "string interpolation");
        for (int i = 0; i < expr->string_interp.parts.count; i++) {
            zan_checker_check_expr(c, expr->string_interp.parts.items[i]);
        }
        return c->binder->type_string;
    }

    case AST_MEMBER_ACCESS:
        return check_member_access(c, expr,
                                   zan_checker_check_expr(c, expr->member.object));

    case AST_INDEX: {
        zan_type_t *obj = zan_checker_check_expr(c, expr->index.object);
        zan_checker_check_expr(c, expr->index.index);
        /* op_index operator: `<class instance>[i]` has the op_index method's
         * declared return type. */
        if ((obj->kind == TYPE_CLASS || obj->kind == TYPE_STRUCT) && obj->sym) {
            zan_istr_t op_name = {(char *)"op_index", 8};
            zan_symbol_t *op = checker_find_method(obj->sym, op_name);
            if (op && op->decl && op->decl->kind == AST_METHOD_DECL &&
                op->decl->method_decl.return_type) {
                return zan_binder_resolve_type(c->binder,
                    op->decl->method_decl.return_type);
            }
            if (op && op->type) return op->type;
        }
        if (obj->kind == TYPE_ARRAY && obj->element_type) {
            /* a rank-N array takes exactly N indices (jagged `a[][]` is
             * nested rank-1 arrays, so each level still takes one). */
            int rank = obj->array_rank > 1 ? obj->array_rank : 1;
            int given = 1 + expr->index.extra.count;
            if (given != rank) {
                zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                    "rank-%d array indexed with %d indices", rank, given);
            }
            for (int i = 0; i < expr->index.extra.count; i++)
                zan_checker_check_expr(c, expr->index.extra.items[i]);
            return obj->element_type;
        }
        return c->binder->type_error;
    }

    case AST_ASSIGNMENT: {
        zan_type_t *left = zan_checker_check_expr(c, expr->binary.left);
        zan_type_t *right = zan_checker_check_expr(c, expr->binary.right);
        check_readonly_assignment(c, expr);
        zan_type_t *target = checker_assignment_target_type(c, expr->binary.left);
        if (!target && expr->binary.left->kind == AST_INDEX)
            target = checker_index_set_target(c, expr->binary.left, right);
        if (!target) target = left;
        if (target)
            checker_check_assignable(c, target, right, expr->binary.right,
                                     expr->loc, "assignment");
        return right;
    }

    case AST_NEW_EXPR: {
        no_runtime_reject(c, expr->loc, "allocation (`new`)");
        zan_type_t *type;
        if (!expr->new_expr.type) {
            /* Anonymous object literal `new { a.x, b.y }`. Only meaningful as
             * a dbgen query projection (GroupBy / ToList / ToAggregate); the
             * checker validates the members and returns a placeholder type. */
            for (int i = 0; i < expr->new_expr.args.count; i++) {
                zan_checker_check_expr(c, expr->new_expr.args.items[i]);
            }
            return c->binder->type_error;
        }
        type = zan_binder_resolve_type(c->binder, expr->new_expr.type);
        /* `new byte[256]` parses the element type with is_array set, so the
         * expression's type is the array of it, not the scalar element. */
        if (expr->new_expr.is_array && type && type->kind != TYPE_ERROR &&
            type->kind != TYPE_ARRAY)
            type = zan_binder_make_array_type(c->binder, type);
        check_generic_constraints(c, type, expr->loc);
        check_ctor_available(c, type, expr);
        for (int i = 0; i < expr->new_expr.args.count; i++) {
            zan_ast_node_t *arg = expr->new_expr.args.items[i];
            /* Object-initializer assignments are field writes on the newly
             * allocated type, not ordinary assignments in the surrounding
             * scope. Resolve the bare field name against that type first so a
             * same-named namespace type (for example Gui.Icon) cannot shadow
             * the actual field (Button.Icon). */
            if (arg && arg->kind == AST_ASSIGNMENT &&
                arg->binary.left &&
                arg->binary.left->kind == AST_IDENTIFIER &&
                type && type->sym) {
                zan_symbol_t *field = checker_find_field(
                    type->sym, arg->binary.left->ident.name);
                if (field) {
                    /* a getter-only property has no setter to dispatch to */
                    if (property_is_readonly(field)) {
                        zan_diag_emit(c->diag, DIAG_ERROR, arg->loc,
                                      "property '%.*s' has no setter and "
                                      "cannot be assigned",
                                      (int)field->name.len, field->name.str);
                    }
                }
                if (field && field->type) {
                    zan_type_t *right = zan_checker_check_expr(
                        c, arg->binary.right);
                    checker_check_assignable(c, field->type, right,
                                             arg->binary.right, arg->loc,
                                             "object initializer");
                    continue;
                }
            }
            zan_checker_check_expr(c, arg);
        }
        return type;
    }

    case AST_CONDITIONAL: {
        zan_checker_check_expr(c, expr->conditional.cond);
        zan_type_t *then_type = zan_checker_check_expr(c, expr->conditional.then_expr);
        zan_type_t *else_type = zan_checker_check_expr(c, expr->conditional.else_expr);
        zan_type_t *merged = merge_conditional_types(c, then_type, else_type);
        if (!merged) {
            zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                "cannot apply '?:' operator to operands of type '%s' and '%s': "
                "neither converts to the other and they share no common type",
                type_name(then_type), type_name(else_type));
            return c->binder->type_error;
        }
        return merged;
    }

    case AST_LAMBDA:
        no_runtime_reject(c, expr->loc, "a lambda");
        for (int i = 0; i < expr->lambda.params.count; i++) {
            /* params type-checked later */
        }
        zan_checker_check_expr(c, expr->lambda.body);
        return c->binder->type_error; /* lambda type resolved in M2 */

    case AST_THIS_EXPR:
        /* `this` is the type whose body is being checked. Typing it here is
         * what lets a call on it be validated (arity, and the member rules
         * above); the member itself is still resolved in M2. */
        if (c->current_type_sym && c->current_type_sym->type) {
            return c->current_type_sym->type;
        }
        return c->binder->type_error;

    case AST_QUERY_EXPR: {
        /* from x in src [clauses] [group e by k [into g]] [select p] — check
         * the source, then walk the clauses in order registering the range
         * var, `let` vars and join vars as locals so later clause
         * expressions and the projection type-check against them. The query
         * itself types as List<select-type> (or List<Grouping<e>> for a
         * terminal group clause). */
        zan_type_t *src_ty = zan_checker_check_expr(c, expr->query.source);
        zan_type_t *elem = (src_ty && src_ty->element_type)
            ? src_ty->element_type : NULL;
        if (!elem && src_ty && src_ty->type_args && src_ty->type_arg_count > 0)
            elem = src_ty->type_args[0];
        if (!elem) elem = c->binder->type_int;
        checker_add_local(c, expr->query.var, elem);
        for (int ci = 0; ci < expr->query.clauses.count; ci++) {
            zan_ast_node_t *cl = expr->query.clauses.items[ci];
            if (cl->kind == AST_QUERY_JOIN) {
                zan_type_t *jty = zan_checker_check_expr(
                    c, cl->query_clause.source);
                zan_type_t *je = (jty && jty->element_type)
                    ? jty->element_type : NULL;
                if (!je && jty && jty->type_args && jty->type_arg_count > 0)
                    je = jty->type_args[0];
                if (!je) je = c->binder->type_int;
                zan_checker_check_expr(c, cl->query_clause.left_key);
                checker_add_local(c, cl->query_clause.name, je);
                zan_checker_check_expr(c, cl->query_clause.right_key);
                if (cl->query_clause.into.len > 0)
                    checker_add_local(c, cl->query_clause.into,
                        zan_binder_make_list_type(c->binder, je));
            } else {
                zan_type_t *ct = zan_checker_check_expr(c,
                    cl->query_clause.expr);
                if (cl->kind == AST_QUERY_LET)
                    checker_add_local(c, cl->query_clause.name, ct);
            }
        }
        if (expr->query.group_expr) {
            zan_type_t *ge = zan_checker_check_expr(c, expr->query.group_expr);
            if (!ge) ge = elem;
            zan_checker_check_expr(c, expr->query.group_key);
            zan_type_t *grp = zan_binder_make_grouping_type(c->binder, ge);
            if (expr->query.group_into.len > 0) {
                checker_add_local(c, expr->query.group_into, grp);
                zan_type_t *sel =
                    zan_checker_check_expr(c, expr->query.select);
                if (!sel) sel = ge;
                return zan_binder_make_list_type(c->binder, sel);
            }
            return zan_binder_make_list_type(c->binder, grp);
        }
        zan_type_t *sel = zan_checker_check_expr(c, expr->query.select);
        if (!sel) sel = elem;
        return zan_binder_make_list_type(c->binder, sel);
    }

    case AST_TUPLE_EXPR: {
        /* (a, b, ...) has the synthesized anonymous struct type with
         * Item1..ItemN fields. Each element is checked and its type folded
         * into the tuple so `var t = (1, "x")` types as the tuple struct. */
        int n = expr->tuple_expr.items.count;
        zan_type_t **elems = (zan_type_t **)zan_arena_alloc(
            c->binder->arena, sizeof(zan_type_t *) * (size_t)(n > 0 ? n : 1));
        for (int i = 0; i < n; i++) {
            elems[i] = zan_checker_check_expr(c, expr->tuple_expr.items.items[i]);
        }
        return zan_binder_make_tuple_type(c->binder, elems, n);
    }

    case AST_SWITCH_EXPR: {
        /* `expr switch { arm, ... }` — the discriminant is checked, then each
         * arm's result is checked and the common type is returned (C# requires
         * every arm to convert to the same type). A pattern variable (`T x =>
         * ...`) is in scope only for its own arm's guard and result. */
        zan_checker_check_expr(c, expr->switch_expr.expr);
        zan_type_t *merged = NULL;
        bool has_result = false;
        for (int i = 0; i < expr->switch_expr.arms.count; i++) {
            zan_ast_node_t *arm = expr->switch_expr.arms.items[i];
            struct checker_local *saved_locals = c->locals;
            if (arm->switch_arm.type_pattern && arm->switch_arm.var_name.len > 0) {
                zan_type_t *pt = zan_binder_resolve_type(
                    c->binder, arm->switch_arm.type_pattern);
                checker_add_local(c, arm->switch_arm.var_name, pt);
            }
            if (arm->switch_arm.when_cond) {
                zan_checker_check_expr(c, arm->switch_arm.when_cond);
            }
            zan_type_t *rt = zan_checker_check_expr(c, arm->switch_arm.result);
            c->locals = saved_locals;
            if (rt && rt->kind != TYPE_ERROR) {
                if (!has_result) {
                    merged = rt;
                    has_result = true;
                } else {
                    zan_type_t *m2 = merge_conditional_types(c, merged, rt);
                    if (m2) merged = m2;
                }
            }
        }
        return has_result ? merged : c->binder->type_error;
    }

    case AST_CAST_EXPR: {
        zan_type_t *src = zan_checker_check_expr(c, expr->cast.expr);
        zan_type_t *dst = zan_binder_resolve_type(c->binder, expr->cast.type);
        if (!dst) return c->binder->type_error;
        if (!checker_cast_is_valid(c, dst, src))
            zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                          "cannot convert '%s' to '%s': no such conversion",
                          type_name(src), type_name(dst));
        return dst;
    }

    case AST_BASE_EXPR:
        return c->binder->type_error; /* resolved in M2 */

    default:
        return c->binder->type_error;
    }
}

/* ---- statement checking ---- */

void zan_checker_check_stmt(zan_checker_t *c, zan_ast_node_t *stmt) {
    if (!stmt) return;

    switch (stmt->kind) {
    case AST_BLOCK: {
        struct checker_local *saved_locals = c->locals;
        for (int i = 0; i < stmt->block.stmts.count; i++) {
            zan_checker_check_stmt(c, stmt->block.stmts.items[i]);
        }
        c->locals = saved_locals;
        break;
    }

    case AST_VAR_DECL: {
        zan_type_t *dt = stmt->var_decl.type
            ? zan_binder_resolve_type(c->binder, stmt->var_decl.type)
            : NULL;
        zan_type_t *iv = NULL;
        if (stmt->var_decl.initializer) {
            iv = zan_checker_check_expr(c, stmt->var_decl.initializer);
        }
        if (dt) {
            check_generic_constraints(c, dt, stmt->loc);
            if (iv) {
                checker_check_assignable(c, dt, iv, stmt->var_decl.initializer,
                                         stmt->loc, "initializer");
            }
            checker_add_local(c, stmt->var_decl.name, dt);
            checker_mark_local_null_src(c, stmt);
        } else {
            /* `var x = ...`: register with the inferred type so later bare
             * uses in the body resolve to it, not to a field/type of the
             * same name */
            checker_add_local(c, stmt->var_decl.name, iv);
            checker_mark_local_null_src(c, stmt);
        }
        break;
    }

    case AST_EXPR_STMT:
        zan_checker_check_expr(c, stmt->expr_stmt.expr);
        break;

    case AST_RETURN_STMT:
        if (stmt->ret.value) {
            zan_type_t *rv = zan_checker_check_expr(c, stmt->ret.value);
            /* Constructors and void methods have no meaningful return target;
             * current_return_type is the resolved declared return type. */
            if (c->current_return_type &&
                c->current_return_type->kind != TYPE_VOID)
                checker_check_assignable(c, c->current_return_type, rv,
                                         stmt->ret.value, stmt->loc,
                                         "return statement");
        }
        break;

    case AST_IF_STMT: {
        zan_type_t *cond_type = zan_checker_check_expr(c, stmt->if_stmt.cond);
        if (cond_type->kind != TYPE_BOOL && cond_type->kind != TYPE_ERROR) {
            zan_diag_emit(c->diag, DIAG_WARNING, stmt->if_stmt.cond->loc,
                          "condition should be bool, got '%s'", type_name(cond_type));
        }
        zan_checker_check_stmt(c, stmt->if_stmt.then_body);
        if (stmt->if_stmt.else_body) {
            zan_checker_check_stmt(c, stmt->if_stmt.else_body);
        }
        break;
    }

    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        zan_checker_check_expr(c, stmt->while_stmt.cond);
        zan_checker_check_stmt(c, stmt->while_stmt.body);
        break;

    case AST_FOR_STMT:
        if (stmt->for_stmt.init) zan_checker_check_stmt(c, stmt->for_stmt.init);
        if (stmt->for_stmt.cond) zan_checker_check_expr(c, stmt->for_stmt.cond);
        if (stmt->for_stmt.step) zan_checker_check_expr(c, stmt->for_stmt.step);
        zan_checker_check_stmt(c, stmt->for_stmt.body);
        break;

    case AST_FOREACH_STMT:
        /* iterating a managed collection walks rc-managed elements */
        no_runtime_reject(c, stmt->loc, "`foreach`");
        {
            zan_type_t *col = zan_checker_check_expr(c, stmt->foreach_stmt.collection);
            zan_type_t *elem = NULL;
            if (col) {
                if (col->kind == TYPE_ARRAY) elem = col->element_type;
                else if (col->kind == TYPE_CLASS && col->type_arg_count == 1)
                    elem = col->type_args[0];
            }
            if (stmt->foreach_stmt.var_type) {
                elem = zan_binder_resolve_type(c->binder,
                                               stmt->foreach_stmt.var_type);
            }
            checker_add_local(c, stmt->foreach_stmt.var_name, elem);
        }
        zan_checker_check_stmt(c, stmt->foreach_stmt.body);
        break;

    case AST_THROW_STMT:
        no_runtime_reject(c, stmt->loc, "`throw`");
        zan_checker_check_expr(c, stmt->throw_stmt.value);
        break;

    case AST_BREAK_STMT:
    case AST_CONTINUE_STMT:
    case AST_GOTO_STMT:
    case AST_LABEL_STMT:
        break;

    case AST_LOCK_STMT:
        no_runtime_reject(c, stmt->loc, "`lock`");
        zan_checker_check_expr(c, stmt->lock_stmt.expr);
        zan_checker_check_stmt(c, stmt->lock_stmt.body);
        break;

    case AST_SWITCH_STMT: {
        zan_type_t *sw_type = zan_checker_check_expr(c, stmt->switch_stmt.expr);
        /* B5 pattern switches (`case T x:` / `case null:`) match on the
         * runtime type, so a class/object discriminant is fine; only a plain
         * constant switch on a non-switchable type is suspect. */
        bool has_patterns = false;
        for (int i = 0; i < stmt->switch_stmt.cases.count && !has_patterns; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            if (sc->switch_case.type_pattern ||
                (sc->switch_case.pattern &&
                 sc->switch_case.pattern->kind == AST_NULL_LITERAL))
                has_patterns = true;
        }
        if (!has_patterns && !type_is_numeric(sw_type) &&
            sw_type->kind != TYPE_STRING && sw_type->kind != TYPE_CHAR &&
            sw_type->kind != TYPE_ERROR) {
            zan_diag_emit(c->diag, DIAG_WARNING, stmt->loc,
                          "switch expression has non-switchable type '%s'", type_name(sw_type));
        }
        for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            if (sc->switch_case.pattern) {
                zan_checker_check_expr(c, sc->switch_case.pattern);
            }
            /* B5 pattern variable: `case T x:` brings x into scope for the
             * guard and the body. */
            if (sc->switch_case.type_pattern && sc->switch_case.var_name.len > 0) {
                zan_type_t *pt = zan_binder_resolve_type(
                    c->binder, sc->switch_case.type_pattern);
                checker_add_local(c, sc->switch_case.var_name, pt);
            }
            if (sc->switch_case.when_cond) {
                zan_checker_check_expr(c, sc->switch_case.when_cond);
            }
            zan_checker_check_stmt(c, sc->switch_case.body);
        }
        break;
    }

    case AST_TRY_STMT:
        no_runtime_reject(c, stmt->loc, "`try`");
        zan_checker_check_stmt(c, stmt->try_stmt.try_body);
        for (int i = 0; i < stmt->try_stmt.catches.count; i++) {
            zan_ast_node_t *cc = stmt->try_stmt.catches.items[i];
            zan_checker_check_stmt(c, cc->catch_clause.body);
        }
        if (stmt->try_stmt.finally_body) {
            zan_checker_check_stmt(c, stmt->try_stmt.finally_body);
        }
        break;

    default:
        break;
    }
}

/* ---- check entire compilation unit ---- */


/* See the forward declaration above: the receiver's type is computed by the
 * caller so a fluent chain is checked once per link, not once per subchain. */
/* ---- "member access on a call that can return null" -----------------------
 * A member access lowers to a load/call off the receiver pointer with no null
 * test, so `o.PathGet("k").AsString(d)` faults (read at 0x8) whenever the
 * lookup misses. The receiver's *declared* type says nothing -- every
 * reference is implicitly nullable -- so the callee's body is what is
 * consulted: a method with a literal `return null;` is treated as nullable and
 * a direct member access on its result is rejected. `?.` and storing the
 * result in a local first (where the flow can be guarded) both stay legal. */
static bool expr_is_null_literal(zan_ast_node_t *e) {
    if (!e) return false;
    if (e->kind == AST_NULL_LITERAL) return true;
    if (e->kind == AST_CONDITIONAL)
        return expr_is_null_literal(e->conditional.then_expr) ||
               expr_is_null_literal(e->conditional.else_expr);
    return false;
}

static bool stmt_returns_null(zan_ast_node_t *n, int depth) {
    if (!n || depth > CHECKER_DERIVES_MAX_DEPTH) return false;
    switch (n->kind) {
    case AST_RETURN_STMT:
        return expr_is_null_literal(n->ret.value);
    case AST_BLOCK:
        for (int i = 0; i < n->block.stmts.count; i++)
            if (stmt_returns_null(n->block.stmts.items[i], depth + 1)) return true;
        return false;
    case AST_IF_STMT:
        return stmt_returns_null(n->if_stmt.then_body, depth + 1) ||
               stmt_returns_null(n->if_stmt.else_body, depth + 1);
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        return stmt_returns_null(n->while_stmt.body, depth + 1);
    case AST_FOR_STMT:
        return stmt_returns_null(n->for_stmt.body, depth + 1);
    case AST_FOREACH_STMT:
        return stmt_returns_null(n->foreach_stmt.body, depth + 1);
    case AST_TRY_STMT: {
        if (stmt_returns_null(n->try_stmt.try_body, depth + 1)) return true;
        for (int i = 0; i < n->try_stmt.catches.count; i++) {
            zan_ast_node_t *cc = n->try_stmt.catches.items[i];
            if (cc && stmt_returns_null(cc->catch_clause.body, depth + 1)) return true;
        }
        return stmt_returns_null(n->try_stmt.finally_body, depth + 1);
    }
    case AST_SWITCH_STMT:
        for (int i = 0; i < n->switch_stmt.cases.count; i++) {
            zan_ast_node_t *cs = n->switch_stmt.cases.items[i];
            if (cs && stmt_returns_null(cs->switch_case.body, depth + 1)) return true;
        }
        return false;
    default:
        return false;
    }
}

static bool method_can_return_null(zan_symbol_t *m) {
    if (!m || m->kind != SYM_METHOD || !m->decl) return false;
    if (m->decl->kind != AST_METHOD_DECL) return false;
    if (!m->decl->method_decl.return_type) return false;
    return stmt_returns_null(m->decl->method_decl.body, 0);
}

/* Does the body ever compare `name` against null (`x == null`, `x != null`,
 * `x is null`, `x?.y`, `x ?? y`)? A local that is tested anywhere in its method
 * is assumed guarded: the checker has no flow graph, and a guard in a sibling
 * branch is far more often correct code than a bug. */
static bool node_guards_null(zan_ast_node_t *n, zan_istr_t name, int depth);

static bool list_guards_null(zan_ast_list_t *l, zan_istr_t name, int depth) {
    for (int i = 0; i < l->count; i++)
        if (node_guards_null(l->items[i], name, depth + 1)) return true;
    return false;
}

static bool is_named_ident(zan_ast_node_t *n, zan_istr_t name) {
    return n && n->kind == AST_IDENTIFIER &&
           n->ident.name.len == name.len &&
           memcmp(n->ident.name.str, name.str, (size_t)name.len) == 0;
}

static bool node_guards_null(zan_ast_node_t *n, zan_istr_t name, int depth) {
    if (!n || depth > CHECKER_DERIVES_MAX_DEPTH) return false;
    switch (n->kind) {
    case AST_BINARY:
        /* `x == null` / `x != null` / `x ?? fallback`, either operand order */
        if ((is_named_ident(n->binary.left, name) &&
             (n->binary.right && n->binary.right->kind == AST_NULL_LITERAL)) ||
            (is_named_ident(n->binary.right, name) &&
             (n->binary.left && n->binary.left->kind == AST_NULL_LITERAL)) ||
            (n->binary.op == TK_QUESTION_QUESTION &&
             is_named_ident(n->binary.left, name)))
            return true;
        return node_guards_null(n->binary.left, name, depth + 1) ||
               node_guards_null(n->binary.right, name, depth + 1);
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        if (is_named_ident(n->type_test.expr, name)) return true;
        return node_guards_null(n->type_test.expr, name, depth + 1);
    case AST_MEMBER_ACCESS:
        if (n->member.null_cond && is_named_ident(n->member.object, name))
            return true;
        return node_guards_null(n->member.object, name, depth + 1);
    case AST_UNARY:
        return node_guards_null(n->unary.operand, name, depth + 1);
    case AST_CAST_EXPR:
        return node_guards_null(n->cast.expr, name, depth + 1);
    case AST_CALL:
        return node_guards_null(n->call.callee, name, depth + 1) ||
               list_guards_null(&n->call.args, name, depth + 1);
    case AST_INDEX:
        return node_guards_null(n->index.object, name, depth + 1) ||
               node_guards_null(n->index.index, name, depth + 1);
    case AST_CONDITIONAL:
        return node_guards_null(n->conditional.cond, name, depth + 1) ||
               node_guards_null(n->conditional.then_expr, name, depth + 1) ||
               node_guards_null(n->conditional.else_expr, name, depth + 1);
    case AST_ASSIGNMENT:
        return node_guards_null(n->binary.left, name, depth + 1) ||
               node_guards_null(n->binary.right, name, depth + 1);
    case AST_BLOCK:
        return list_guards_null(&n->block.stmts, name, depth + 1);
    case AST_EXPR_STMT:
        return node_guards_null(n->expr_stmt.expr, name, depth + 1);
    case AST_RETURN_STMT:
        return node_guards_null(n->ret.value, name, depth + 1);
    case AST_THROW_STMT:
        return node_guards_null(n->throw_stmt.value, name, depth + 1);
    case AST_VAR_DECL:
        return node_guards_null(n->var_decl.initializer, name, depth + 1);
    case AST_IF_STMT:
        return node_guards_null(n->if_stmt.cond, name, depth + 1) ||
               node_guards_null(n->if_stmt.then_body, name, depth + 1) ||
               node_guards_null(n->if_stmt.else_body, name, depth + 1);
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        return node_guards_null(n->while_stmt.cond, name, depth + 1) ||
               node_guards_null(n->while_stmt.body, name, depth + 1);
    case AST_FOR_STMT:
        return node_guards_null(n->for_stmt.init, name, depth + 1) ||
               node_guards_null(n->for_stmt.cond, name, depth + 1) ||
               node_guards_null(n->for_stmt.step, name, depth + 1) ||
               node_guards_null(n->for_stmt.body, name, depth + 1);
    case AST_FOREACH_STMT:
        return node_guards_null(n->foreach_stmt.collection, name, depth + 1) ||
               node_guards_null(n->foreach_stmt.body, name, depth + 1);
    case AST_TRY_STMT: {
        if (node_guards_null(n->try_stmt.try_body, name, depth + 1)) return true;
        for (int i = 0; i < n->try_stmt.catches.count; i++) {
            zan_ast_node_t *cc = n->try_stmt.catches.items[i];
            if (cc && node_guards_null(cc->catch_clause.body, name, depth + 1))
                return true;
        }
        return node_guards_null(n->try_stmt.finally_body, name, depth + 1);
    }
    case AST_SWITCH_STMT: {
        if (node_guards_null(n->switch_stmt.expr, name, depth + 1)) return true;
        for (int i = 0; i < n->switch_stmt.cases.count; i++) {
            zan_ast_node_t *cs = n->switch_stmt.cases.items[i];
            if (cs && node_guards_null(cs->switch_case.body, name, depth + 1))
                return true;
        }
        return false;
    }
    default:
        return false;
    }
}

static void checker_reject_null_receiver(zan_checker_t *c, zan_ast_node_t *expr) {
    if (!expr || expr->kind != AST_MEMBER_ACCESS) return;
    if (expr->member.null_cond) return;
    zan_ast_node_t *obj = expr->member.object;
    if (!obj) return;
    zan_symbol_t *m = NULL;
    if (obj->kind == AST_CALL) {
        if (obj != c->last_call_node) return;
        m = c->last_call_method;
    } else if (obj->kind == AST_IDENTIFIER) {
        /* a local holding a nullable call result, never tested in this body */
        struct checker_local *l = checker_find_local_slot(c, obj->ident.name);
        if (!l || !l->null_src) return;
        if (!c->current_body) return;
        if (node_guards_null(c->current_body, obj->ident.name, 0)) return;
        m = l->null_src;
    } else {
        return;
    }
    if (!method_can_return_null(m)) return;
    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                  "'%.*s' can return null; accessing '%.*s' on its result "
                  "faults at runtime -- use '?.', or store it and check for "
                  "null first",
                  (int)m->name.len, m->name.str,
                  (int)expr->member.name.len, expr->member.name.str);
}

/* True when the receiver of a member access is a value (a local, parameter,
 * field, call result, ...) rather than a type name. `Type.StaticField` is the
 * only valid way to reach a static field; through a value it used to lower to
 * the constant 0, which then either compared an int against a pointer (invalid
 * IR) or was dereferenced as an object. */
static bool receiver_is_value_expr(zan_checker_t *c, zan_ast_node_t *obj) {
    switch (obj->kind) {
    case AST_IDENTIFIER: {
        if (checker_find_local_slot(c, obj->ident.name)) return true;
        zan_symbol_t *s = zan_binder_lookup(c->binder, obj->ident.name);
        if (!s) return false;
        return s->kind == SYM_FIELD || s->kind == SYM_PROPERTY ||
               s->kind == SYM_PARAM || s->kind == SYM_LOCAL;
    }
    case AST_CALL:
    case AST_INDEX:
        return true;
    default:
        return false;
    }
}

static zan_type_t *check_member_access(zan_checker_t *c, zan_ast_node_t *expr,
                                       zan_type_t *obj_type) {
    checker_reject_null_receiver(c, expr);
    /* Static enum member access: EnumType.Member must be a declared member.
     * A miss used to fall through to irgen, which silently folded the
     * access to the constant 0 (masking stale references, e.g. a removed
     * enum member). A member access on an enum *value* (`t.ToString()`) is
     * a compiler-lowered method call resolved in irgen, so it is not
     * validated here. */
    if (obj_type && obj_type->kind == TYPE_ENUM && obj_type->sym &&
        expr->member.object->kind == AST_IDENTIFIER) {
        zan_symbol_t *os = zan_binder_lookup(c->binder,
                                             expr->member.object->ident.name);
        if (os && os->kind == SYM_ENUM) {
            int ei;
            for (ei = 0; ei < obj_type->sym->member_count; ei++) {
                zan_symbol_t *m = obj_type->sym->members[ei];
                if (m->kind == SYM_ENUM_MEMBER &&
                    m->name.len == expr->member.name.len &&
                    memcmp(m->name.str, expr->member.name.str,
                           (size_t)expr->member.name.len) == 0)
                    break;
            }
            if (ei == obj_type->sym->member_count) {
                /* Compiler-provided static pseudo-members on an enum type:
                 * TryParse(string, out T) is lowered by irgen over the
                 * declaration-order name table, so it is not a declared
                 * member. */
                zan_istr_t mn = expr->member.name;
                bool pseudo = mn.len == 8 &&
                    memcmp(mn.str, "TryParse", 8) == 0;
                if (!pseudo) {
                    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                                  "enum type '%.*s' has no member '%.*s'",
                                  (int)obj_type->sym->name.len,
                                  obj_type->sym->name.str,
                                  (int)expr->member.name.len,
                                  expr->member.name.str);
                    return c->binder->type_error;
                }
            }
        }
    }
    /* resolve field/method on known struct/class types. Inherited fields are the
     * layout prefix, so a field that hides a base one of the same name appears
     * twice: the search runs back to front, which picks the declaration this
     * static type contributes (C# hiding). */
    if (obj_type && obj_type->sym) {
        for (int i = obj_type->sym->member_count - 1; i >= 0; i--) {
            zan_symbol_t *m = obj_type->sym->members[i];
            if (m->name.len == expr->member.name.len &&
                memcmp(m->name.str, expr->member.name.str, m->name.len) == 0) {
                /* A method in value position is a method group: only a
                 * delegate-typed slot (or a call, typed in the AST_CALL case)
                 * consumes it. Returning the method's *return* type here would
                 * let `int x = obj.Method;` type as the return value and then
                 * be rejected against a delegate target; type_error is what the
                 * delegate assignability rule expects for method groups. */
                if (m->kind == SYM_METHOD)
                    return c->binder->type_error;
                if (m->kind == SYM_FIELD && (m->modifiers & MOD_STATIC) &&
                    expr->member.object &&
                    receiver_is_value_expr(c, expr->member.object)) {
                    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                                  "'%.*s' is a static field of '%.*s'; "
                                  "qualify it with the type name "
                                  "('%.*s.%.*s'), not an instance",
                                  (int)expr->member.name.len,
                                  expr->member.name.str,
                                  (int)obj_type->sym->name.len,
                                  obj_type->sym->name.str,
                                  (int)obj_type->sym->name.len,
                                  obj_type->sym->name.str,
                                  (int)expr->member.name.len,
                                  expr->member.name.str);
                    return c->binder->type_error;
                }
                return m->type ? m->type : c->binder->type_error;
            }
        }
    }
    /* Reflection members (`ti.Name`, `obj.GetType`, ...): checked after the
     * declared members above, so a user member of the same name wins. */
    {
        zan_type_t *rt = zan_refl_member_type(c->binder, obj_type,
                                              expr->member.name);
        if (rt) return rt;
    }
    /* Builtin scalar types (string, int, ...) declare no fields; the only
     * member they carry is the string.Length property (lowered by irgen).
     * Anything else is a typo: it used to fall through to irgen, which
     * silently lowered it to the constant 0 — a crash at runtime when the
     * zero was used as a pointer (e.g. `tabs[i].path.path`). */
    if (obj_type && type_is_scalar_primitive(obj_type)) {
        if (obj_type->kind == TYPE_STRING && expr->member.name.len == 6 &&
            memcmp(expr->member.name.str, "Length", 6) == 0) {
            return c->binder->type_int;
        }
        zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                      "type '%.*s' has no member '%.*s'",
                      (int)obj_type->name.len, obj_type->name.str,
                      (int)expr->member.name.len, expr->member.name.str);
        return c->binder->type_error;
    }
    return c->binder->type_error;
}

static void check_method_body(zan_checker_t *c, zan_ast_node_t *method) {
    if (!method->method_decl.body) return;

    bool saved_ctor = c->in_ctor;
    c->in_ctor = method->kind == AST_CONSTRUCTOR_DECL;
    bool saved_nrt = c->in_no_runtime;
    c->in_no_runtime = zan_ast_has_attr(method, "NoRuntime");
    zan_type_t *saved = c->current_return_type;
    c->current_return_type = zan_binder_resolve_type(c->binder,
                                                      method->method_decl.return_type);
    check_generic_constraints(c, c->current_return_type, method->loc);
    /* seed the local list with the parameters: a param that shadows a field
     * (`static int Val(ChartData d)` in a class with `int d;`) must type as
     * the parameter, not the field */
    struct checker_local *saved_locals = c->locals;
    c->locals = NULL;
    for (int j = 0; j < method->method_decl.params.count; j++) {
        zan_ast_node_t *p = method->method_decl.params.items[j];
        if (p && p->kind == AST_PARAM) {
            zan_type_t *pt = zan_binder_resolve_type(c->binder, p->param.type);
            checker_add_local(c, p->param.name, pt);
        }
    }
    zan_ast_node_t *saved_body = c->current_body;
    c->current_body = method->method_decl.body;
    zan_checker_check_stmt(c, method->method_decl.body);
    zan_definite_check(c->diag, method);
    c->current_body = saved_body;
    c->locals = saved_locals;
    c->current_return_type = saved;
    c->in_no_runtime = saved_nrt;
    c->in_ctor = saved_ctor;
}

void zan_checker_check(zan_checker_t *c, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;

    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL &&
            decl->kind != AST_INTERFACE_DECL && decl->kind != AST_ENUM_DECL) {
            continue;
        }

        c->current_type_sym = zan_binder_lookup(c->binder, decl->type_decl.name);
        for (int j = 0; j < decl->type_decl.members.count; j++) {
            zan_ast_node_t *member = decl->type_decl.members.items[j];
            check_weak_member(c, decl, member);
            if (member->kind == AST_METHOD_DECL ||
                member->kind == AST_CONSTRUCTOR_DECL ||
                member->kind == AST_DESTRUCTOR_DECL) {
                zan_compile_trace("check %.*s.%.*s",
                                (int)decl->type_decl.name.len,
                                decl->type_decl.name.str,
                                (int)member->method_decl.name.len,
                                member->method_decl.name.str);
                check_method_body(c, member);
            }
        }
    }
}
