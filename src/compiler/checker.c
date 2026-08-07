/* checker.c -- Basic type checker for the Zan language.
 *
 * For M1 this performs lightweight checks:
 *   - Expression type inference (literals, binary ops, calls)
 *   - Return type validation
 *   - Variable type resolution
 *   - Basic assignment compatibility
 */

#include "checker.h"
#include "diag.h"
#include <string.h>

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

/* The declared field of `type_sym` named `name`, walking the base chain. */
static zan_symbol_t *checker_find_field(zan_symbol_t *type_sym, zan_istr_t name) {
    while (type_sym) {
        for (int i = 0; i < type_sym->member_count; i++) {
            zan_symbol_t *m = type_sym->members[i];
            if (m && m->kind == SYM_FIELD &&
                m->name.len == name.len &&
                memcmp(m->name.str, name.str, (size_t)name.len) == 0)
                return m;
        }
        type_sym = (type_sym->type && type_sym->type->base_type)
            ? type_sym->type->base_type->sym : NULL;
    }
    return NULL;
}

static bool field_is_readonly(zan_symbol_t *field) {
    return field && field->decl && field->decl->kind == AST_FIELD_DECL &&
           (field->decl->field_decl.modifiers & MOD_READONLY) != 0;
}

/* The assignment target's field symbol together with the type that declares
 * it, for `x = ...`, `this.x = ...` and `obj.x = ...`. */
static zan_symbol_t *assign_target_field(zan_checker_t *c, zan_ast_node_t *lhs,
                                         zan_symbol_t **owner) {
    if (!lhs) return NULL;
    if (lhs->kind == AST_IDENTIFIER) {
        if (!c->current_type_sym) return NULL;
        *owner = c->current_type_sym;
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
        *owner = ts;
        return checker_find_field(ts, lhs->member.name);
    }
    return NULL;
}

static void check_readonly_assignment(zan_checker_t *c, zan_ast_node_t *expr) {
    zan_symbol_t *owner = NULL;
    zan_symbol_t *field = assign_target_field(c, expr->binary.left, &owner);
    if (!field_is_readonly(field)) return;
    if (c->in_ctor && owner == c->current_type_sym) return;
    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                  "cannot assign to readonly field '%.*s' outside a "
                  "constructor of '%.*s'",
                  (int)field->name.len, field->name.str,
                  (int)(owner ? owner->name.len : 0),
                  owner ? owner->name.str : "");
}

/* ---- generic constraint checking ---- */

/* True when `arg` is, implements, or derives from `cons`. */
static bool type_satisfies_constraint(zan_type_t *arg, zan_type_t *cons) {
    if (!arg || !cons) return true;
    if (arg == cons) return true;
    if (arg->sym && cons->sym && arg->sym == cons->sym) return true;
    for (int i = 0; i < arg->interface_count; i++) {
        if (type_satisfies_constraint(arg->interfaces[i], cons)) return true;
    }
    if (arg->base_type && arg->base_type != arg)
        return type_satisfies_constraint(arg->base_type, cons);
    return false;
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
            zan_type_t *cons = zan_binder_resolve_type(
                c->binder, wc->where_clause.constraints.items[k]);
            if (!cons || cons->kind == TYPE_ERROR) continue;
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
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_NULLABLE)
        return checker_type_equal(a->element_type, b->element_type);
    if (a->name.len != b->name.len ||
        (a->name.len && memcmp(a->name.str, b->name.str, (size_t)a->name.len) != 0))
        return false;
    if (a->type_arg_count != b->type_arg_count) return false;
    for (int i = 0; i < a->type_arg_count; i++)
        if (!checker_type_equal(a->type_args[i], b->type_args[i])) return false;
    return true;
}

/* True when `sub` is, implements, or derives from `sup` -- the same relation
 * type_satisfies_constraint computes for `where T : C` clauses. */
static bool checker_type_derives_from(zan_type_t *sub, zan_type_t *sup) {
    if (!sub || !sup) return false;
    if (sub == sup) return true;
    if (sub->sym && sup->sym && sub->sym == sup->sym) return true;
    if (sub->kind == sup->kind && checker_type_equal(sub, sup)) return true;
    for (int i = 0; i < sub->interface_count; i++)
        if (checker_type_derives_from(sub->interfaces[i], sup)) return true;
    if (sub->base_type && sub->base_type != sub)
        return checker_type_derives_from(sub->base_type, sup);
    return false;
}

/* Reference kinds: the IR carries them all as one object pointer, so any two
 * of them can share a single PHI; the merged type picks the more general side.
 * Delegates are reference kinds too -- an empty delegate is `null` in Zan, so
 * `del = null` and `return null` for a delegate slot must stay legal. */
static bool checker_type_is_ref(zan_type_t *t) {
    if (!t) return false;
    return t->kind == TYPE_OBJECT || t->kind == TYPE_INTERFACE ||
           t->kind == TYPE_STRING || t->kind == TYPE_CLASS ||
           t->kind == TYPE_ARRAY || t->kind == TYPE_DELEGATE;
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
        return a; /* unrelated references still share one pointer carrier */
    }

    /* enums ride an integer carrier at the IR level; enum vs enum was handled
     * above, enum vs int (or int vs enum) matches that carrier. */
    if (a->kind == TYPE_ENUM || b->kind == TYPE_ENUM) return a;

    return NULL; /* unrelated value types, or a value mixed with a reference */
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

    /* Delegates accept method groups, lambdas and null; the checker doesn't
     * model method-group types, so defer to irgen's delegate lowering. */
    if (target->kind == TYPE_DELEGATE) return true;
    bool tnum = type_is_numeric(target), vnum = type_is_numeric(value);
    if (tnum && (vnum || value->kind == TYPE_CHAR)) return true;
    if (target->kind == TYPE_CHAR && (vnum || value->kind == TYPE_CHAR))
        return true;

    /* reference kinds (and a null literal, typed as object) share one carrier */
    if (checker_type_is_ref(target) &&
        (checker_type_is_ref(value) || value->kind == TYPE_OBJECT))
        return true;

    /* enums ride an integer carrier */
    if (target->kind == TYPE_ENUM && value->kind == TYPE_ENUM) return true;
    if (target->kind == TYPE_ENUM && (vnum || value->kind == TYPE_CHAR)) return true;
    if (value->kind == TYPE_ENUM && (tnum || target->kind == TYPE_CHAR)) return true;

    return false;
}

/* Emit the D2 diagnostic when `value` is not assignable to `target`. */
static void checker_check_assignable(zan_checker_t *c, zan_type_t *target,
                                     zan_type_t *value, zan_loc_t loc,
                                     const char *what) {
    if (!target || !value || target == c->binder->type_error ||
        value == c->binder->type_error || value == c->binder->type_void)
        return;
    if (checker_type_assignable(target, value)) return;
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
            if (type_is_integral(left) && type_is_integral(right)) {
                return promote_numeric(c->binder, left, right);
            }
            return c->binder->type_error;

        case TK_EQ_EQ: case TK_BANG_EQ:
        case TK_LESS: case TK_GREATER:
        case TK_LESS_EQ: case TK_GREATER_EQ:
            return c->binder->type_bool;

        case TK_AMP_AMP: case TK_PIPE_PIPE:
            return c->binder->type_bool;

        case TK_QUESTION_QUESTION:
            return left;

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
            if (type_is_numeric(operand)) return operand;
            return c->binder->type_error;
        default:
            return c->binder->type_error;
        }
    }

    case AST_POSTFIX_UNARY: {
        zan_type_t *operand = zan_checker_check_expr(c, expr->unary.operand);
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
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            recv = zan_checker_check_expr(c, expr->call.callee->member.object);
            callee_type = type_is_scalar_primitive(recv)
                ? c->binder->type_error
                : check_member_access(c, expr->call.callee, recv);
        } else {
            callee_type = zan_checker_check_expr(c, expr->call.callee);
        }
        for (int i = 0; i < expr->call.args.count; i++) {
            zan_checker_check_expr(c, expr->call.args.items[i]);
        }
        check_call_arity(c, expr, recv);
        if (callee_type && callee_type->kind == TYPE_DELEGATE) {
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
            return obj->element_type;
        }
        return c->binder->type_error;
    }

    case AST_ASSIGNMENT: {
        zan_checker_check_expr(c, expr->binary.left);
        zan_type_t *right = zan_checker_check_expr(c, expr->binary.right);
        check_readonly_assignment(c, expr);
        /* Only compare explicit member-access LHS (`this.f`, `obj.f`,
         * `Type.f`). A bare identifier may be a local variable shadowing a
         * field, and the checker does not track locals. */
        if (expr->binary.left->kind == AST_MEMBER_ACCESS) {
            zan_symbol_t *owner = NULL;
            zan_symbol_t *field = assign_target_field(
                c, expr->binary.left, &owner);
            if (field && field->type)
                checker_check_assignable(c, field->type, right, expr->loc,
                                         "assignment");
        }
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
        for (int i = 0; i < expr->new_expr.args.count; i++) {
            zan_checker_check_expr(c, expr->new_expr.args.items[i]);
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
    case AST_BLOCK:
        for (int i = 0; i < stmt->block.stmts.count; i++) {
            zan_checker_check_stmt(c, stmt->block.stmts.items[i]);
        }
        break;

    case AST_VAR_DECL:
        if (stmt->var_decl.initializer) {
            zan_checker_check_expr(c, stmt->var_decl.initializer);
        }
        if (stmt->var_decl.type) {
            zan_type_t *dt = zan_binder_resolve_type(c->binder, stmt->var_decl.type);
            check_generic_constraints(c, dt, stmt->loc);
        }
        break;

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
                                         stmt->loc, "return statement");
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
        zan_checker_check_expr(c, stmt->foreach_stmt.collection);
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
        if (!type_is_numeric(sw_type) && sw_type->kind != TYPE_STRING &&
            sw_type->kind != TYPE_CHAR && sw_type->kind != TYPE_ERROR) {
            zan_diag_emit(c->diag, DIAG_WARNING, stmt->loc,
                          "switch expression has non-switchable type '%s'", type_name(sw_type));
        }
        for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            if (sc->switch_case.pattern) {
                zan_checker_check_expr(c, sc->switch_case.pattern);
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
static zan_type_t *check_member_access(zan_checker_t *c, zan_ast_node_t *expr,
                                       zan_type_t *obj_type) {
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
    /* resolve field/method on known struct/class types */
    if (obj_type && obj_type->sym) {
        for (int i = 0; i < obj_type->sym->member_count; i++) {
            zan_symbol_t *m = obj_type->sym->members[i];
            if (m->name.len == expr->member.name.len &&
                memcmp(m->name.str, expr->member.name.str, m->name.len) == 0) {
                return m->type ? m->type : c->binder->type_error;
            }
        }
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
    zan_checker_check_stmt(c, method->method_decl.body);
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
