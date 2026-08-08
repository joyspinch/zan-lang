/* binder.c -- Name resolution and symbol table.
 *
 * Two-pass binding:
 *   Pass 1: Collect all type declarations (classes, structs, interfaces, enums)
 *           and register them as symbols in the global scope.
 *   Pass 2: Bind all member declarations, resolve type references, and build
 *           the full symbol table with resolved types.
 */

#include "binder.h"
#include "arena.h"
#include "diag.h"
#include <stdio.h>
#include <string.h>

/* ---- helpers ---- */

static zan_type_t *make_type(zan_arena_t *arena, zan_type_kind_t kind, const char *name, int len) {
    zan_type_t *t = (zan_type_t *)zan_arena_alloc(arena, sizeof(zan_type_t));
    t->kind = kind;
    t->name.str = name;
    t->name.len = len;
    t->interfaces = NULL;
    t->interface_count = 0;
    return t;
}

static zan_symbol_t *make_symbol(zan_arena_t *arena, zan_sym_kind_t kind,
                                 zan_istr_t name, zan_type_t *type,
                                 zan_ast_node_t *decl, uint32_t modifiers) {
    zan_symbol_t *s = (zan_symbol_t *)zan_arena_alloc(arena, sizeof(zan_symbol_t));
    s->kind = kind;
    s->name = name;
    s->type = type;
    s->decl = decl;
    s->modifiers = modifiers;
    s->parent = NULL;
    s->members = NULL;
    s->member_count = 0;
    s->member_cap = 0;
    return s;
}

static void symbol_add_member(zan_arena_t *arena, zan_symbol_t *parent, zan_symbol_t *child) {
    if (parent->member_count >= parent->member_cap) {
        int new_cap = parent->member_cap < 4 ? 4 : parent->member_cap * 2;
        zan_symbol_t **new_members = (zan_symbol_t **)zan_arena_alloc(
            arena, sizeof(zan_symbol_t *) * (size_t)new_cap);
        if (parent->members) {
            memcpy(new_members, parent->members,
                   sizeof(zan_symbol_t *) * (size_t)parent->member_count);
        }
        parent->members = new_members;
        parent->member_cap = new_cap;
    }
    child->parent = parent;
    parent->members[parent->member_count++] = child;
}

/* ---- scope management ---- */

static zan_scope_t *scope_new(zan_arena_t *arena, zan_scope_t *parent) {
    zan_scope_t *s = (zan_scope_t *)zan_arena_alloc(arena, sizeof(zan_scope_t));
    s->parent = parent;
    s->symbols = NULL;
    s->sym_count = 0;
    s->sym_cap = 0;
    return s;
}

static void scope_add(zan_arena_t *arena, zan_scope_t *scope, zan_symbol_t *sym) {
    if (scope->sym_count >= scope->sym_cap) {
        int new_cap = scope->sym_cap < 8 ? 8 : scope->sym_cap * 2;
        zan_symbol_t **new_syms = (zan_symbol_t **)zan_arena_alloc(
            arena, sizeof(zan_symbol_t *) * (size_t)new_cap);
        if (scope->symbols) {
            memcpy(new_syms, scope->symbols,
                   sizeof(zan_symbol_t *) * (size_t)scope->sym_count);
        }
        scope->symbols = new_syms;
        scope->sym_cap = new_cap;
    }
    scope->symbols[scope->sym_count++] = sym;
}

static zan_symbol_t *scope_find(zan_scope_t *scope, zan_istr_t name) {
    for (zan_scope_t *s = scope; s; s = s->parent) {
        for (int i = 0; i < s->sym_count; i++) {
            zan_symbol_t *sym = s->symbols[i];
            if (sym->name.len == name.len &&
                memcmp(sym->name.str, name.str, (size_t)name.len) == 0) {
                return sym;
            }
        }
    }
    return NULL;
}

/* ---- initialization ---- */

void zan_binder_init(zan_binder_t *b, zan_arena_t *arena, zan_diag_t *diag) {
    memset(b, 0, sizeof(*b));
    b->arena = arena;
    b->diag = diag;
    b->current_scope = scope_new(arena, NULL);

    /* create built-in types */
    b->type_void   = make_type(arena, TYPE_VOID,   "void",   4);
    b->type_bool   = make_type(arena, TYPE_BOOL,   "bool",   4);
    b->type_byte   = make_type(arena, TYPE_BYTE,   "byte",   4);
    b->type_short  = make_type(arena, TYPE_SHORT,  "short",  5);
    b->type_int    = make_type(arena, TYPE_INT,    "int",    3);
    b->type_long   = make_type(arena, TYPE_LONG,   "long",   4);
    b->type_sbyte  = make_type(arena, TYPE_SBYTE,  "sbyte",  5);
    b->type_ushort = make_type(arena, TYPE_USHORT, "ushort", 6);
    b->type_uint   = make_type(arena, TYPE_UINT,   "uint",   4);
    b->type_ulong  = make_type(arena, TYPE_ULONG,  "ulong",  5);
    b->type_float  = make_type(arena, TYPE_FLOAT,  "float",  5);
    b->type_double = make_type(arena, TYPE_DOUBLE, "double", 6);
    b->type_char   = make_type(arena, TYPE_CHAR,   "char",   4);
    b->type_string = make_type(arena, TYPE_STRING, "string", 6);
    b->type_object = make_type(arena, TYPE_OBJECT, "object", 6);
    b->type_nint   = make_type(arena, TYPE_NINT,   "nint",   4);
    b->type_error  = make_type(arena, TYPE_ERROR,  "<error>", 7);
}

/* ---- type resolution ---- */

static bool istr_eq(zan_istr_t a, const char *b, int len) {
    return a.len == len && memcmp(a.str, b, (size_t)len) == 0;
}

zan_type_t *zan_binder_make_list_type(zan_binder_t *b, zan_type_t *elem) {
    zan_type_t *t = make_type(b->arena, TYPE_CLASS, "List", 4);
    t->type_args =
        (zan_type_t **)zan_arena_alloc(b->arena, sizeof(zan_type_t *));
    t->type_args[0] = elem;
    t->type_arg_count = 1;
    return t;
}

/* Span<T> is a non-owning value view; it is a value struct (TYPE_STRUCT), so
 * ARC never retains or releases it. */
zan_type_t *zan_binder_make_span_type(zan_binder_t *b, zan_type_t *elem) {
    zan_type_t *t = make_type(b->arena, TYPE_STRUCT, "Span", 4);
    t->type_args =
        (zan_type_t **)zan_arena_alloc(b->arena, sizeof(zan_type_t *));
    t->type_args[0] = elem;
    t->type_arg_count = 1;
    return t;
}

zan_type_t *zan_binder_make_array_type(zan_binder_t *b, zan_type_t *elem) {
    zan_type_t *t = make_type(b->arena, TYPE_ARRAY, elem->name.str, elem->name.len);
    t->element_type = elem;
    return t;
}

/* Grouping<T> is the System.Linq grouping record; a query `group e by k`
 * lowers to List<Grouping<e>>, and `group e by k into g` binds g to one
 * Grouping — both synthesized types need this constructor because there is
 * no syntactic type reference to resolve. */
zan_type_t *zan_binder_make_grouping_type(zan_binder_t *b, zan_type_t *elem) {
    zan_type_t *t = make_type(b->arena, TYPE_CLASS, "Grouping", 8);
    t->type_args =
        (zan_type_t **)zan_arena_alloc(b->arena, sizeof(zan_type_t *));
    t->type_args[0] = elem;
    t->type_arg_count = 1;
    return t;
}

/* Render a type into `buf` for use in a tuple signature: the element's simple
 * name with its generic arguments inlined (`List<int>` -> "List<int>"), so
 * two tuples whose element types are structurally identical -- even ones
 * spelled at different sites -- map to one anonymous struct. */
static void tuple_sig_type(zan_type_t *t, char *buf, size_t cap) {
    if (!t || cap == 0) return;
    size_t used = strlen(buf);
    if (used + 1 >= cap) return;
    if (t->kind == TYPE_ARRAY && t->element_type) {
        tuple_sig_type(t->element_type, buf, cap);
        if (strlen(buf) + 4 < cap) {
            strcat(buf, "[");
            for (int r = 1; r < (t->array_rank > 1 ? t->array_rank : 1); r++)
                if (strlen(buf) + 2 < cap) strcat(buf, ",");
            strcat(buf, "]");
        }
        return;
    }
    if (t->kind == TYPE_NULLABLE && t->element_type) {
        tuple_sig_type(t->element_type, buf, cap);
        if (strlen(buf) + 1 < cap) strcat(buf, "?");
        return;
    }
    const char *nm = t->name.str ? t->name.str : "?";
    int nl = (int)t->name.len;
    if (nl <= 0) nl = (int)strlen(nm);
    if (nl <= 0) nl = 1;
    if (used + (size_t)nl + 8 >= cap) return;
    strncat(buf, nm, (size_t)nl);
    if (t->type_arg_count > 0) {
        if (strlen(buf) + 2 < cap) strcat(buf, "<");
        for (int i = 0; i < t->type_arg_count; i++) {
            if (i > 0 && strlen(buf) + 2 < cap) strcat(buf, ",");
            tuple_sig_type(t->type_args[i], buf, cap);
        }
        if (strlen(buf) + 1 < cap) strcat(buf, ">");
    }
}

/* C# tuples `(T1, T2, ...)` lower to a canonical anonymous struct whose symbol
 * carries Item1..ItemN field members (ItemN for the N-th element, matching C#'s
 * naming). The type is cached per element signature so a tuple return type and
 * the value constructed at the call site are the same struct -- struct identity
 * is what lets `var t = Make();` assign the returned tuple to an inferred
 * variable and `t.Item1` resolve to a real field. */
zan_type_t *zan_binder_make_tuple_type(zan_binder_t *b, zan_type_t **elems,
                                       int count) {
    if (!b || !elems || count <= 0) return b ? b->type_error : NULL;
    /* canonical signature: "__tuple<N>:<sig1>,<sig2>,..." */
    char sig[512];
    snprintf(sig, sizeof sig, "__tuple%d:", count);
    for (int i = 0; i < count; i++) {
        if (i > 0 && strlen(sig) < sizeof sig - 2) strcat(sig, ",");
        tuple_sig_type(elems[i], sig, sizeof sig);
    }
    zan_istr_t sig_istr = { (char *)sig, (uint32_t)strlen(sig) };

    /* cache hit: structurally-identical tuple types are one struct */
    for (int i = 0; i < b->tuple_type_count; i++) {
        zan_type_t *t = b->tuple_types[i];
        if (t && t->name.len == sig_istr.len &&
            memcmp(t->name.str, sig_istr.str, (size_t)sig_istr.len) == 0)
            return t;
    }

    /* synthesize the anonymous struct */
    char *name = zan_arena_strdup(b->arena, sig, (int)strlen(sig));
    zan_type_t *t = make_type(b->arena, TYPE_STRUCT, name, (int)strlen(name));
    zan_symbol_t *sym = make_symbol(b->arena, SYM_STRUCT,
        (zan_istr_t){ name, (uint32_t)strlen(name) }, t, NULL, MOD_PUBLIC);
    t->sym = sym;
    for (int i = 0; i < count; i++) {
        char fname[16];
        if (count > 1) {
            snprintf(fname, sizeof fname, "Item%d", i + 1);
        } else {
            snprintf(fname, sizeof fname, "Item1");
        }
        zan_symbol_t *fs = make_symbol(b->arena, SYM_FIELD,
            (zan_istr_t){ zan_arena_strdup(b->arena, fname, (int)strlen(fname)),
                          (uint32_t)strlen(fname) },
            elems[i], NULL, MOD_PUBLIC);
        symbol_add_member(b->arena, sym, fs);
    }

    if (b->tuple_type_count >= b->tuple_type_cap) {
        int new_cap = b->tuple_type_cap == 0 ? 8 : b->tuple_type_cap * 2;
        zan_type_t **grown = (zan_type_t **)zan_arena_alloc(
            b->arena, sizeof(zan_type_t *) * (size_t)new_cap);
        if (b->tuple_types) {
            memcpy(grown, b->tuple_types,
                   sizeof(zan_type_t *) * (size_t)b->tuple_type_count);
        }
        b->tuple_types = grown;
        b->tuple_type_cap = new_cap;
    }
    b->tuple_types[b->tuple_type_count++] = t;
    return t;
}

/* Substitute type parameters (matched by declared name in `tps`) with `args`
 * throughout `t`, cloning composite types as needed. */
zan_type_t *zan_binder_subst_named(zan_binder_t *b, zan_type_t *t,
                                   zan_ast_list_t *tps, zan_type_t **args) {
    if (!t) return t;
    if (t->kind == TYPE_TYPE_PARAM) {
        for (int i = 0; i < tps->count; i++) {
            zan_istr_t tn = tps->items[i]->ident.name;
            if (args[i] && tn.len == t->name.len &&
                memcmp(tn.str, t->name.str, (size_t)t->name.len) == 0)
                return args[i];
        }
        return t;
    }
    bool mentions = false;
    if ((t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE) && t->element_type)
        mentions = true;
    if (t->kind == TYPE_DELEGATE) mentions = true;
    if (t->type_arg_count > 0) mentions = true;
    if (!mentions) return t;
    zan_type_t *nt = (zan_type_t *)zan_arena_alloc(b->arena, sizeof(zan_type_t));
    *nt = *t;
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE) {
        nt->element_type = zan_binder_subst_named(b, t->element_type, tps, args);
        return nt;
    }
    if (t->kind == TYPE_DELEGATE) {
        nt->delegate_ret_type = zan_binder_subst_named(b, t->delegate_ret_type, tps, args);
        if (t->delegate_param_count > 0) {
            nt->delegate_param_types = (zan_type_t **)zan_arena_alloc(
                b->arena, sizeof(zan_type_t *) * (size_t)t->delegate_param_count);
            for (int i = 0; i < t->delegate_param_count; i++)
                nt->delegate_param_types[i] =
                    zan_binder_subst_named(b, t->delegate_param_types[i], tps, args);
        }
    }
    if (t->type_arg_count > 0) {
        nt->type_args = (zan_type_t **)zan_arena_alloc(
            b->arena, sizeof(zan_type_t *) * (size_t)t->type_arg_count);
        for (int i = 0; i < t->type_arg_count; i++)
            nt->type_args[i] = zan_binder_subst_named(b, t->type_args[i], tps, args);
    }
    return nt;
}

/* Value types have no null representation of their own, so `T?` over one is a
 * real wrapper (irgen lowers it to a payload + has-value pair); over a
 * reference type it is just T. Type parameters are left out -- the substituted
 * type is not known here. */
static bool type_is_value_kind(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BOOL: case TYPE_BYTE: case TYPE_SBYTE:
    case TYPE_SHORT: case TYPE_USHORT: case TYPE_INT: case TYPE_UINT:
    case TYPE_LONG: case TYPE_ULONG: case TYPE_NINT:
    case TYPE_FLOAT: case TYPE_DOUBLE:
    case TYPE_CHAR: case TYPE_ENUM: case TYPE_STRUCT:
        return true;
    default:
        return false;
    }
}

zan_type_t *zan_binder_resolve_type(zan_binder_t *b, zan_ast_node_t *type_ref) {
    if (!type_ref) return b->type_error;
    if (type_ref->kind == AST_TUPLE_TYPE) {
        /* `(int, string)` in type position resolves to a canonical anonymous
         * struct with Item1..ItemN fields (see zan_binder_make_tuple_type). */
        int n = type_ref->tuple_type.elems.count;
        zan_type_t **elems = (zan_type_t **)zan_arena_alloc(
            b->arena, sizeof(zan_type_t *) * (size_t)(n > 0 ? n : 1));
        for (int i = 0; i < n; i++)
            elems[i] = zan_binder_resolve_type(b, type_ref->tuple_type.elems.items[i]);
        return zan_binder_make_tuple_type(b, elems, n);
    }
    if (type_ref->kind != AST_TYPE_REF) {
        return b->type_error;
    }

    /* Resolving is pure for a given (type reference, scope) pair but allocates
     * a fresh instantiation type for every generic reference. Overload
     * resolution re-resolves the same signatures over and over while inferring
     * an expression's type, so without this memo a deeply nested expression
     * grows the arena without bound -- large single-file programs ran the host
     * out of memory. */
    if (b->binding_done && type_ref->rt_type &&
        type_ref->rt_scope == (void *)b->current_scope)
        return (zan_type_t *)type_ref->rt_type;

    zan_istr_t name = type_ref->type_ref.name;

    /* Resolve the base (element) type first, then apply array / nullable
     * wrapping uniformly. Built-in types previously returned early here,
     * which silently dropped the `[]` on parameters/fields such as `int[]`. */
    zan_type_t *base = NULL;

    /* built-in types */
    if (istr_eq(name, "void",   4)) base = b->type_void;
    else if (istr_eq(name, "bool",   4)) base = b->type_bool;
    else if (istr_eq(name, "byte",   4)) base = b->type_byte;
    else if (istr_eq(name, "short",  5)) base = b->type_short;
    else if (istr_eq(name, "int",    3)) base = b->type_int;
    else if (istr_eq(name, "long",   4)) base = b->type_long;
    else if (istr_eq(name, "sbyte",  5)) base = b->type_sbyte;
    else if (istr_eq(name, "ushort", 6)) base = b->type_ushort;
    else if (istr_eq(name, "uint",   4)) base = b->type_uint;
    else if (istr_eq(name, "ulong",  5)) base = b->type_ulong;
    else if (istr_eq(name, "float",  5)) base = b->type_float;
    else if (istr_eq(name, "double", 6)) base = b->type_double;
    /* decimal: mapped to double (no 128-bit decimal representation yet) */
    else if (istr_eq(name, "decimal", 7)) base = b->type_double;
    else if (istr_eq(name, "char",   4)) base = b->type_char;
    else if (istr_eq(name, "string", 6)) base = b->type_string;
    else if (istr_eq(name, "object", 6)) base = b->type_object;
    else if (istr_eq(name, "nint",   4)) base = b->type_nint;
    /* built-in generic types */
    else if (istr_eq(name, "List", 4)) base = make_type(b->arena, TYPE_CLASS, "List", 4);
    else if (istr_eq(name, "Dict", 4) || istr_eq(name, "Dictionary", 10))
        base = make_type(b->arena, TYPE_CLASS, "Dict", 4);
    else if (istr_eq(name, "StringBuilder", 13))
        base = make_type(b->arena, TYPE_CLASS, "StringBuilder", 13);
    else if (istr_eq(name, "Span", 4))
        base = make_type(b->arena, TYPE_STRUCT, "Span", 4);
    /* Task / Task<T>: a coroutine handle (opaque i64 at codegen). The static
     * `Task.Spawn/Run/IsDone/...` call surface stays a compiler-intercepted
     * builtin (builtin_api.c); this makes the *type* usable as a value:
     * `Task t = Task.Run(...)`, `Task<int> t`, `t.Wait()`, `t.Result`. */
    else if (istr_eq(name, "Task", 4))
        base = make_type(b->arena, TYPE_TASK, "Task", 4);
    /* .NET Func<...>/Action<...> delegate families are synthesized from their
     * type arguments (Func<T1..Tn, TResult> / Action<T1..Tn>); see below. */
    else if (istr_eq(name, "Func", 4) || istr_eq(name, "Action", 6))
        base = make_type(b->arena, TYPE_DELEGATE, name.str, name.len);
    else {
        /* user-defined type: look up in scope */
        zan_symbol_t *sym = scope_find(b->current_scope, name);
        if (sym) base = sym->type;
    }

    if (!base) {
        zan_diag_emit(b->diag, DIAG_ERROR, type_ref->loc,
                      "undefined type '%.*s'", name.len, name.str);
        return b->type_error;
    }

    /* carry generic arguments (e.g. List<Node>, Dict<K,V>, Box<int>) so codegen
     * and the checker can recover concrete element/argument types. Built-in
     * List/Dict use a fresh `make_type` above, but a user generic type resolves
     * to the *shared* class-symbol type; mutating that would let one
     * instantiation (Box<int>) clobber another (Box<string>), so copy it into a
     * fresh instantiation type first. */
    /* A generic delegate instantiation (e.g. Selector<T, List<R>>) must
     * substitute the delegate's own type parameters in its signature: the
     * shared delegate type's ret/param types are the delegate's bare type
     * params, which only coincide with the use-site's meaning when the
     * argument is that same bare name. Composite arguments (List<R>) would
     * otherwise be silently flattened to the bare param. */
    if (type_ref->type_ref.type_args.count > 0 && base->kind == TYPE_DELEGATE &&
        base->sym && base->sym->decl &&
        base->sym->decl->method_decl.type_params.count ==
            type_ref->type_ref.type_args.count) {
        int nargs = type_ref->type_ref.type_args.count;
        zan_type_t **args = (zan_type_t **)zan_arena_alloc(
            b->arena, sizeof(zan_type_t *) * (size_t)nargs);
        for (int i = 0; i < nargs; i++)
            args[i] = zan_binder_resolve_type(b, type_ref->type_ref.type_args.items[i]);
        zan_type_t *inst = make_type(b->arena, TYPE_DELEGATE,
                                     base->name.str, base->name.len);
        *inst = *base;
        inst->type_args = args;
        inst->type_arg_count = nargs;
        zan_ast_list_t *dtps = &base->sym->decl->method_decl.type_params;
        inst->delegate_ret_type =
            zan_binder_subst_named(b, base->delegate_ret_type, dtps, args);
        if (base->delegate_param_count > 0) {
            inst->delegate_param_types = (zan_type_t **)zan_arena_alloc(
                b->arena, sizeof(zan_type_t *) * (size_t)base->delegate_param_count);
            for (int i = 0; i < base->delegate_param_count; i++)
                inst->delegate_param_types[i] =
                    zan_binder_subst_named(b, base->delegate_param_types[i], dtps, args);
        }
        base = inst;
    } else
    if (type_ref->type_ref.type_args.count > 0) {
        bool builtin_generic = istr_eq(name, "List", 4) || istr_eq(name, "Dict", 4) ||
                               istr_eq(name, "Dictionary", 10) || istr_eq(name, "Span", 4) ||
                               istr_eq(name, "Task", 4);
        bool user_generic = !builtin_generic &&
            (base->kind == TYPE_CLASS || base->kind == TYPE_STRUCT ||
             base->kind == TYPE_INTERFACE);
        if (builtin_generic || user_generic) {
            int nargs = type_ref->type_ref.type_args.count;
            zan_type_t *inst = base;
            if (user_generic) {
                inst = make_type(b->arena, base->kind, base->name.str, base->name.len);
                inst->sym = base->sym;
                inst->base_type = base->base_type;
                inst->element_type = base->element_type;
            }
            inst->type_args = (zan_type_t **)zan_arena_alloc(
                b->arena, sizeof(zan_type_t *) * (size_t)nargs);
            inst->type_arg_count = nargs;
            for (int i = 0; i < nargs; i++) {
                inst->type_args[i] = zan_binder_resolve_type(
                    b, type_ref->type_ref.type_args.items[i]);
            }
            base = inst;
        }
    }

    /* Synthesize a Func<...>/Action<...> delegate from the type arguments:
     *   Func<T1..Tn, TResult>  -> parameters T1..Tn, result TResult
     *   Action<T1..Tn>         -> parameters T1..Tn, result void
     * A bare `Func`/`Action` (no type args) is not a valid type -- it has no
     * signature to derive, so it stays an empty placeholder and fails later
     * when the signature is read. */
    if (base && base->kind == TYPE_DELEGATE && base->name.len == 4 &&
        memcmp(base->name.str, "Func", 4) == 0) {
        if (type_ref->type_ref.type_args.count < 1) {
            zan_diag_emit(b->diag, DIAG_ERROR, type_ref->loc,
                          "'Func' requires at least one type argument "
                          "(the return type)");
            return b->type_error;
        }
        int nargs = type_ref->type_ref.type_args.count;
        zan_type_t *fn = make_type(b->arena, TYPE_DELEGATE, "Func", 4);
        fn->type_args = (zan_type_t **)zan_arena_alloc(
            b->arena, sizeof(zan_type_t *) * (size_t)nargs);
        fn->type_arg_count = nargs;
        for (int i = 0; i < nargs; i++)
            fn->type_args[i] = zan_binder_resolve_type(
                b, type_ref->type_ref.type_args.items[i]);
        /* Func<TResult> is a zero-parameter delegate */
        fn->delegate_param_count = nargs - 1;
        if (fn->delegate_param_count > 0) {
            fn->delegate_param_types = (zan_type_t **)zan_arena_alloc(
                b->arena, sizeof(zan_type_t *) * (size_t)fn->delegate_param_count);
            for (int i = 0; i < fn->delegate_param_count; i++)
                fn->delegate_param_types[i] = fn->type_args[i];
        }
        fn->delegate_ret_type = fn->type_args[nargs - 1];
        base = fn;
    } else if (base && base->kind == TYPE_DELEGATE && base->name.len == 6 &&
        memcmp(base->name.str, "Action", 6) == 0) {
        int nargs = type_ref->type_ref.type_args.count;
        zan_type_t *fn = make_type(b->arena, TYPE_DELEGATE, "Action", 6);
        if (nargs > 0) {
            fn->type_args = (zan_type_t **)zan_arena_alloc(
                b->arena, sizeof(zan_type_t *) * (size_t)nargs);
            fn->type_arg_count = nargs;
            for (int i = 0; i < nargs; i++)
                fn->type_args[i] = zan_binder_resolve_type(
                    b, type_ref->type_ref.type_args.items[i]);
        }
        fn->delegate_param_count = nargs;
        if (nargs > 0) {
            fn->delegate_param_types = (zan_type_t **)zan_arena_alloc(
                b->arena, sizeof(zan_type_t *) * (size_t)nargs);
            for (int i = 0; i < nargs; i++)
                fn->delegate_param_types[i] = fn->type_args[i];
        }
        fn->delegate_ret_type = b->type_void;
        base = fn;
    }

    zan_type_t *resolved = base;
    /* wrap in nullable if needed */
    if (type_ref->type_ref.is_nullable) {
        /* `T?` over a reference type is just T: the reference already carries
         * null. Over a value type it becomes a TYPE_NULLABLE wrapper, which
         * irgen lowers to `{ payload, i1 }` -- the value plus a has-value flag.
         * (Mapping it to a bare pointer stored `int? v = 5` as the address 5.) */
        if (resolved && resolved != b->type_error &&
            type_is_value_kind(resolved)) {
            zan_type_t *nullable = make_type(b->arena, TYPE_NULLABLE, name.str, name.len);
            nullable->element_type = resolved;
            resolved = nullable;
        }
        /* Reference types are left unwrapped: `A?` is A, which already admits
         * null. Wrapping lost the class identity -- TYPE_NULLABLE maps to a
         * bare i8*, so `A? a = new A(); a.x` read a field off an untyped
         * pointer and produced 0, and a call through `I? i` dispatched
         * nowhere. */
    }
    /* wrap in array if needed -- after the nullable wrap, so `int?[]` is an
     * array whose element type is the nullable `int?`, not a plain int.
     * A jagged declaration nests: the node's array_element is the inner
     * declaration chain, so `int[][]` resolves as array-of-int[] and
     * `int[][,]` as a 1D array of rank-2 `int[,]` (leftmost specifier is
     * the outermost array). A plain node without array_element is its own
     * element (`int[]` / `int[,]` wrap the base type directly). */
    if (type_ref->type_ref.is_array) {
        zan_type_t *elem = resolved;
        if (type_ref->type_ref.array_element)
            elem = zan_binder_resolve_type(b, type_ref->type_ref.array_element);
        if (!elem) elem = b->type_error;
        zan_type_t *arr = make_type(b->arena, TYPE_ARRAY, elem->name.str, elem->name.len);
        arr->element_type = elem;
        arr->array_rank = type_ref->type_ref.array_rank > 0
            ? type_ref->type_ref.array_rank : 1;
        resolved = arr;
    }
    if (b->binding_done && resolved && resolved != b->type_error) {
        type_ref->rt_type = resolved;
        type_ref->rt_scope = (void *)b->current_scope;
    }
    return resolved;
}

/* ---- symbol lookup ---- */

zan_symbol_t *zan_binder_lookup(zan_binder_t *b, zan_istr_t name) {
    return scope_find(b->current_scope, name);
}

/* Register every declared generic type parameter (class Box<T> -> T) in the
 * root scope so that it stays resolvable after per-type binding scopes are torn
 * down. The later checker and irgen passes re-resolve method signatures / local
 * declarations against the root scope; without this they would report a bogus
 * "undefined type 'T'". Type parameters all erase to the same representation
 * (an opaque pointer), so a name shared by two classes' <T> is harmless. */
static void register_type_param_list(zan_binder_t *b, zan_ast_list_t *tps) {
    for (int j = 0; j < tps->count; j++) {
        zan_ast_node_t *tp = tps->items[j];
        if (tp->kind != AST_IDENTIFIER) continue;
        if (scope_find(b->current_scope, tp->ident.name)) continue;
        zan_type_t *tp_type = make_type(b->arena, TYPE_TYPE_PARAM,
                                        tp->ident.name.str, tp->ident.name.len);
        zan_symbol_t *tp_sym = make_symbol(b->arena, SYM_TYPE_PARAM,
                                           tp->ident.name, tp_type, tp, 0);
        scope_add(b->arena, b->current_scope, tp_sym);
    }
}

static void register_type_params(zan_binder_t *b, zan_ast_list_t *decls) {
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *decl = decls->items[i];
        /* delegate declarations may themselves be generic: delegate R F<T>(...) */
        if (decl->kind == AST_DELEGATE_DECL) {
            register_type_param_list(b, &decl->method_decl.type_params);
            continue;
        }
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL &&
            decl->kind != AST_INTERFACE_DECL && decl->kind != AST_ENUM_DECL) {
            continue;
        }
        register_type_param_list(b, &decl->type_decl.type_params);
        /* method-level type parameters (static T Id<T>(...)) share the same
         * erased-pointer representation as class type parameters, so registering
         * their names globally keeps them resolvable in signatures and bodies. */
        for (int m = 0; m < decl->type_decl.members.count; m++) {
            zan_ast_node_t *member = decl->type_decl.members.items[m];
            if (member->kind == AST_METHOD_DECL)
                register_type_param_list(b, &member->method_decl.type_params);
        }
    }
}

/* ---- binding passes ---- */

static zan_sym_kind_t ast_kind_to_sym_kind(zan_ast_kind_t kind) {
    switch (kind) {
    case AST_CLASS_DECL:     return SYM_CLASS;
    case AST_STRUCT_DECL:    return SYM_STRUCT;
    case AST_INTERFACE_DECL: return SYM_INTERFACE;
    case AST_ENUM_DECL:      return SYM_ENUM;
    case AST_DELEGATE_DECL:  return SYM_DELEGATE;
    default:                 return SYM_CLASS;
    }
}

static zan_type_kind_t ast_kind_to_type_kind(zan_ast_kind_t kind) {
    switch (kind) {
    case AST_CLASS_DECL:     return TYPE_CLASS;
    case AST_STRUCT_DECL:    return TYPE_STRUCT;
    case AST_INTERFACE_DECL: return TYPE_INTERFACE;
    case AST_ENUM_DECL:      return TYPE_ENUM;
    case AST_DELEGATE_DECL:  return TYPE_DELEGATE;
    default:                 return TYPE_CLASS;
    }
}

/* Pass 1: register type declarations */
static void bind_type_decls(zan_binder_t *b, zan_ast_list_t *decls) {
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *node = decls->items[i];

        /* delegate declarations use method_decl union */
        if (node->kind == AST_DELEGATE_DECL) {
            zan_istr_t name = node->method_decl.name;
            zan_symbol_t *existing = scope_find(b->current_scope, name);
            if (existing) {
                zan_diag_emit(b->diag, DIAG_ERROR, node->loc,
                              "duplicate type declaration '%.*s'", name.len, name.str);
                continue;
            }
            zan_type_t *type = make_type(b->arena, TYPE_DELEGATE, name.str, name.len);
            zan_symbol_t *sym = make_symbol(b->arena, SYM_DELEGATE,
                name, type, node, node->method_decl.modifiers);
            type->sym = sym;
            scope_add(b->arena, b->current_scope, sym);
            continue;
        }

        if (node->kind == AST_CLASS_DECL || node->kind == AST_STRUCT_DECL ||
            node->kind == AST_INTERFACE_DECL || node->kind == AST_ENUM_DECL) {

            zan_istr_t name = node->type_decl.name;

            /* check for duplicates */
            zan_symbol_t *existing = scope_find(b->current_scope, name);
            if (existing) {
                zan_diag_emit(b->diag, DIAG_ERROR, node->loc,
                              "duplicate type declaration '%.*s'", name.len, name.str);
                continue;
            }

            zan_type_t *type = make_type(b->arena,
                ast_kind_to_type_kind(node->kind), name.str, name.len);
            zan_symbol_t *sym = make_symbol(b->arena,
                ast_kind_to_sym_kind(node->kind),
                name, type, node, node->type_decl.modifiers);
            type->sym = sym;
            type->base_type = NULL;

            scope_add(b->arena, b->current_scope, sym);
        }
    }

    /* Resolve delegate signatures only after every named type is registered.
     * This permits delegates to reference classes declared later or in another
     * source file within the same compilation. */
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *node = decls->items[i];
        if (node->kind != AST_DELEGATE_DECL) continue;

        zan_symbol_t *sym = scope_find(b->current_scope, node->method_decl.name);
        if (!sym || sym->decl != node) continue;
        zan_type_t *type = sym->type;

        register_type_param_list(b, &node->method_decl.type_params);
        type->delegate_is_async = (node->method_decl.modifiers & MOD_ASYNC) != 0;
        type->delegate_ret_type = node->method_decl.return_type
            ? zan_binder_resolve_type(b, node->method_decl.return_type)
            : b->type_void;
        int pc = node->method_decl.params.count;
        type->delegate_param_count = pc;
        if (pc > 0) {
            type->delegate_param_types = (zan_type_t **)zan_arena_alloc(
                b->arena, sizeof(zan_type_t *) * (size_t)pc);
            for (int j = 0; j < pc; j++) {
                zan_ast_node_t *param = node->method_decl.params.items[j];
                type->delegate_param_types[j] =
                    zan_binder_resolve_type(b, param->param.type);
            }
        } else {
            type->delegate_param_types = NULL;
        }
    }
}

/* Pass 3: resolve base types and inherit members. Runs only after every
 * type's own members are bound (pass 2), so a base class declared later in
 * the merged compilation unit -- e.g. a stdlib class pulled in by
 * --auto-stdlib after the user's sources -- contributes its fields all the
 * same. Multi-level chains are handled by resolving the base first
 * (recursively); inherited fields are PREPENDED so base fields stay a prefix
 * of the derived layout (upcasts are plain bitcasts). */
static bool type_has_own_member(zan_symbol_t *type_sym, zan_istr_t name) {
    for (int i = 0; i < type_sym->member_count; i++) {
        if ((type_sym->members[i]->kind == SYM_FIELD ||
             type_sym->members[i]->kind == SYM_PROPERTY) &&
            type_sym->members[i]->name.len == name.len &&
            memcmp(type_sym->members[i]->name.str, name.str, name.len) == 0) {
            return true;
        }
    }
    return false;
}

static void resolve_bases(zan_binder_t *b, zan_ast_node_t *type_node) {
    zan_symbol_t *type_sym = scope_find(b->current_scope, type_node->type_decl.name);
    if (!type_sym || !type_sym->type || type_sym->decl != type_node) return;
    if (type_sym->type->bases_resolved == 2) return;   /* done */
    if (type_sym->type->bases_resolved == 1) {
        /* Re-entered while still on the inheritance stack: the base edge we
         * are resolving closes a cycle (`class A : B {} class B : A {}`).
         * Report it and leave bases_resolved at 1 so the caller that recursed
         * into us can see the resolution failed and skip this edge. */
        zan_diag_emit(b->diag, DIAG_ERROR, type_node->loc,
                      "cyclic inheritance involving '%.*s'",
                      (int)type_node->type_decl.name.len,
                      type_node->type_decl.name.str);
        return;
    }
    type_sym->type->bases_resolved = 1;

    if (type_node->type_decl.bases.count > 0) {
        int nbases = type_node->type_decl.bases.count;
        zan_type_t **ifaces = (zan_type_t **)zan_arena_alloc(
            b->arena, sizeof(zan_type_t *) * (size_t)nbases);
        int nif = 0;
        for (int bx = 0; bx < nbases; bx++) {
            zan_ast_node_t *base_ref = type_node->type_decl.bases.items[bx];
            zan_istr_t base_name = base_ref->type_ref.name;
            zan_symbol_t *base_sym = scope_find(b->current_scope, base_name);
            if (!base_sym) {
                zan_diag_emit(b->diag, DIAG_ERROR, base_ref->loc,
                              "undefined base type '%.*s'",
                              (int)base_name.len, base_name.str);
                continue;
            }
            if ((base_sym->kind == SYM_CLASS || base_sym->kind == SYM_STRUCT) &&
                !type_sym->type->base_type) {
                /* make sure the base has its own inherited fields first */
                if (base_sym->decl &&
                    (base_sym->decl->kind == AST_CLASS_DECL ||
                     base_sym->decl->kind == AST_STRUCT_DECL)) {
                    resolve_bases(b, base_sym->decl);
                }
                /* A base whose own resolution was interrupted hit a cycle:
                 * adopting it would build a base_type ring that later passes
                 * walk forever. Skip the edge (the cycle was already
                 * diagnosed at the re-entry site). */
                if (base_sym->type->bases_resolved != 2) continue;
                type_sym->type->base_type = base_sym->type;
                /* inherit base class fields and properties (prefix layout):
                 * collect the base's non-shadowed fields/properties, then
                 * prepend them to the derived member list */
                int ninherit = 0;
                for (int bi = 0; bi < base_sym->member_count; bi++) {
                    if ((base_sym->members[bi]->kind == SYM_FIELD ||
                         base_sym->members[bi]->kind == SYM_PROPERTY) &&
                        !type_has_own_member(type_sym, base_sym->members[bi]->name)) {
                        ninherit++;
                    }
                }
                if (ninherit > 0) {
                    int total = ninherit + type_sym->member_count;
                    zan_symbol_t **merged = (zan_symbol_t **)zan_arena_alloc(
                        b->arena, sizeof(zan_symbol_t *) * (size_t)total);
                    int mi = 0;
                    for (int bi = 0; bi < base_sym->member_count; bi++) {
                        if ((base_sym->members[bi]->kind == SYM_FIELD ||
                             base_sym->members[bi]->kind == SYM_PROPERTY) &&
                            !type_has_own_member(type_sym, base_sym->members[bi]->name)) {
                            merged[mi++] = base_sym->members[bi];
                        }
                    }
                    for (int oi = 0; oi < type_sym->member_count; oi++) {
                        merged[mi++] = type_sym->members[oi];
                    }
                    type_sym->members = merged;
                    type_sym->member_count = total;
                    type_sym->member_cap = total;
                }
            } else if (base_sym->kind == SYM_INTERFACE) {
                if (base_sym->decl) resolve_bases(b, base_sym->decl);
                if (base_sym->type->bases_resolved != 2) continue;
                ifaces[nif++] = base_sym->type;
            }
        }
        if (nif > 0) {
            type_sym->type->interfaces = ifaces;
            type_sym->type->interface_count = nif;
        }
    }
    type_sym->type->bases_resolved = 2;
}

/* Pass 1.5: copy default interface methods into the implementing types.
 *
 * An interface method with a body (`interface I { int G() { return 42; } }`) was
 * bound as a member of the interface and then dropped: irgen never walks an
 * AST_INTERFACE_DECL, so no body was emitted and a call through either the
 * interface or the class returned 0 (or failed with "no member 'G'"). Copying
 * the declaration into every implementing type that does not provide its own
 * override makes it an ordinary method of that type, so it is bound, emitted
 * and dispatched like any other -- and a `this.F()` inside the default body
 * resolves to the implementing type's F.
 *
 * Runs between pass 1 (types registered) and pass 2 (members bound), so the
 * copies are bound as members of the implementing type. */
static bool binder_type_equal(zan_type_t *a, zan_type_t *b, int depth) {
    if (!a || !b) return a == b;
    if (a == b) return true;
    if (depth > 64 || a->kind != b->kind) return false;
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_NULLABLE) {
        if (a->kind == TYPE_ARRAY && a->array_rank != b->array_rank)
            return false; /* int[,] is not int[] */
        return binder_type_equal(a->element_type, b->element_type, depth + 1);
    }
    if (a->kind == TYPE_DELEGATE) {
        if (a->delegate_is_async != b->delegate_is_async ||
            a->delegate_param_count != b->delegate_param_count ||
            !binder_type_equal(a->delegate_ret_type, b->delegate_ret_type, depth + 1))
            return false;
        for (int i = 0; i < a->delegate_param_count; i++)
            if (!binder_type_equal(a->delegate_param_types[i],
                                   b->delegate_param_types[i], depth + 1))
                return false;
        return true;
    }
    if (a->type_arg_count != b->type_arg_count) return false;
    if (a->sym && b->sym && a->sym != b->sym) return false;
    if ((!a->sym || !b->sym) &&
        (a->name.len != b->name.len ||
         memcmp(a->name.str, b->name.str, (size_t)a->name.len) != 0))
        return false;
    for (int i = 0; i < a->type_arg_count; i++)
        if (!binder_type_equal(a->type_args[i], b->type_args[i], depth + 1))
            return false;
    return true;
}

static bool method_signature_equal(zan_binder_t *b, zan_ast_node_t *a,
                                   zan_ast_node_t *other) {
    if (!a || !other || a->kind != AST_METHOD_DECL ||
        other->kind != AST_METHOD_DECL ||
        a->method_decl.name.len != other->method_decl.name.len ||
        memcmp(a->method_decl.name.str, other->method_decl.name.str,
               (size_t)a->method_decl.name.len) != 0)
        return false;
    if (((a->method_decl.modifiers ^ other->method_decl.modifiers) &
         (MOD_STATIC | MOD_ASYNC)) != 0)
        return false;
    if (a->method_decl.params.count != other->method_decl.params.count)
        return false;
    zan_type_t *ar = a->method_decl.return_type
        ? zan_binder_resolve_type(b, a->method_decl.return_type) : b->type_void;
    zan_type_t *br = other->method_decl.return_type
        ? zan_binder_resolve_type(b, other->method_decl.return_type) : b->type_void;
    /* An interface method may be phrased in the interface's own type
     * parameters (`interface I<T> { T Get(); }`). Against an implementing
     * type's concrete signature the parameter is a wildcard -- `int Get()`
     * implements `T Get()` for the binding `T := int`. */
    bool a_tp = ar && ar->kind == TYPE_TYPE_PARAM;
    bool b_tp = br && br->kind == TYPE_TYPE_PARAM;
    if (!(a_tp || b_tp) && !binder_type_equal(ar, br, 0)) return false;
    for (int i = 0; i < a->method_decl.params.count; i++) {
        zan_ast_node_t *ap = a->method_decl.params.items[i];
        zan_ast_node_t *bp = other->method_decl.params.items[i];
        if (!ap || !bp || ap->kind != AST_PARAM || bp->kind != AST_PARAM ||
            ap->param.by_ref != bp->param.by_ref ||
            ap->param.is_params != bp->param.is_params ||
            ap->param.is_this != bp->param.is_this)
            return false;
        zan_type_t *at = zan_binder_resolve_type(b, ap->param.type);
        zan_type_t *bt = zan_binder_resolve_type(b, bp->param.type);
        bool at_tp = at && at->kind == TYPE_TYPE_PARAM;
        bool bt_tp = bt && bt->kind == TYPE_TYPE_PARAM;
        if (!(at_tp || bt_tp) && !binder_type_equal(at, bt, 0)) return false;
    }
    return true;
}

static bool type_declares_method(zan_binder_t *b, zan_ast_node_t *type_node,
                                 zan_ast_node_t *wanted) {
    for (int i = 0; i < type_node->type_decl.members.count; i++) {
        zan_ast_node_t *m = type_node->type_decl.members.items[i];
        if (method_signature_equal(b, m, wanted)) return true;
    }
    return false;
}

static void inherit_default_methods(zan_binder_t *b, zan_ast_node_t *type_node,
                                    zan_ast_node_t *iface_node, int depth) {
    if (!iface_node || iface_node->kind != AST_INTERFACE_DECL || depth > 8) return;
    for (int i = 0; i < iface_node->type_decl.members.count; i++) {
        zan_ast_node_t *m = iface_node->type_decl.members.items[i];
        if (m->kind != AST_METHOD_DECL || !m->method_decl.body) continue;
        if ((m->method_decl.modifiers & MOD_STATIC) != 0) continue;
        if (type_declares_method(b, type_node, m)) continue;
        /* Shallow copy: the body and parameter list are read-only here, but the
         * node identity must differ per implementing type, since irgen keys a
         * method's symbol and its emitted function on the declaration node. */
        zan_ast_node_t *copy = zan_ast_new(b->arena, AST_METHOD_DECL, m->loc);
        *copy = *m;
        zan_ast_list_push(&type_node->type_decl.members, copy, b->arena);
    }
    /* an interface may itself extend interfaces carrying defaults */
    for (int bx = 0; bx < iface_node->type_decl.bases.count; bx++) {
        zan_symbol_t *bs = scope_find(b->current_scope,
            iface_node->type_decl.bases.items[bx]->type_ref.name);
        if (bs && bs->kind == SYM_INTERFACE && bs->decl)
            inherit_default_methods(b, type_node, bs->decl, depth + 1);
    }
}

static void bind_default_interface_methods(zan_binder_t *b, zan_ast_list_t *decls) {
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *decl = decls->items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL) continue;
        for (int bx = 0; bx < decl->type_decl.bases.count; bx++) {
            zan_symbol_t *bs = scope_find(b->current_scope,
                decl->type_decl.bases.items[bx]->type_ref.name);
            if (bs && bs->kind == SYM_INTERFACE && bs->decl)
                inherit_default_methods(b, decl, bs->decl, 0);
        }
    }
}

static bool class_has_interface_method(zan_binder_t *b, zan_symbol_t *cls,
                                       zan_ast_node_t *wanted) {
    for (zan_symbol_t *s = cls; s; s = (s->type && s->type->base_type)
             ? s->type->base_type->sym : NULL) {
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            if (m && m->kind == SYM_METHOD && m->decl &&
                method_signature_equal(b, m->decl, wanted))
                return true;
        }
    }
    return false;
}

static void validate_interface_contract(zan_binder_t *b, zan_symbol_t *cls,
                                        zan_ast_node_t *iface, int depth) {
    if (!iface || depth > 64) return;
    /* An interface method may be phrased in the interface's own type
     * parameters (`interface I<T> { T Get(); }`). The signature comparison
     * below re-resolves the method's declared types, which are only
     * resolvable inside the interface's scope, so register those parameters
     * here and pop them afterwards. */
    zan_scope_t *saved = b->current_scope;
    int tp_count = iface->type_decl.type_params.count;
    if (tp_count > 0) {
        b->current_scope = scope_new(b->arena, saved);
        register_type_param_list(b, &iface->type_decl.type_params);
    }
    for (int i = 0; i < iface->type_decl.members.count; i++) {
        zan_ast_node_t *im = iface->type_decl.members.items[i];
        if (im->kind != AST_METHOD_DECL ||
            (im->method_decl.modifiers & MOD_STATIC) != 0 ||
            im->method_decl.body)
            continue;
        if (!class_has_interface_method(b, cls, im)) {
            zan_diag_emit(b->diag, DIAG_ERROR, im->loc,
                          "type '%.*s' does not implement interface method '%.*s'",
                          (int)cls->name.len, cls->name.str,
                          (int)im->method_decl.name.len,
                          im->method_decl.name.str);
        }
    }
    for (int i = 0; i < iface->type_decl.bases.count; i++) {
        zan_symbol_t *base = scope_find(b->current_scope,
            iface->type_decl.bases.items[i]->type_ref.name);
        if (base && base->kind == SYM_INTERFACE && base->decl)
            validate_interface_contract(b, cls, base->decl, depth + 1);
    }
    b->current_scope = saved;
}

static void validate_interface_contracts(zan_binder_t *b, zan_ast_list_t *decls) {
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *decl = decls->items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL) continue;
        zan_symbol_t *cls = scope_find(b->current_scope, decl->type_decl.name);
        if (!cls || !cls->type) continue;
        for (int j = 0; j < cls->type->interface_count; j++) {
            zan_type_t *iface = cls->type->interfaces[j];
            if (iface && iface->sym && iface->sym->decl)
                validate_interface_contract(b, cls, iface->sym->decl, 0);
        }
    }
}

/* Pass 2: bind member declarations */
static void bind_members(zan_binder_t *b, zan_ast_node_t *type_node) {
    zan_istr_t type_name = type_node->type_decl.name;
    zan_symbol_t *type_sym = scope_find(b->current_scope, type_name);
    if (!type_sym) return;

    zan_scope_t *saved = b->current_scope;
    b->current_scope = scope_new(b->arena, saved);

    /* register type parameters */
    for (int i = 0; i < type_node->type_decl.type_params.count; i++) {
        zan_ast_node_t *tp = type_node->type_decl.type_params.items[i];
        zan_type_t *tp_type = make_type(b->arena, TYPE_TYPE_PARAM,
                                        tp->ident.name.str, tp->ident.name.len);
        zan_symbol_t *tp_sym = make_symbol(b->arena, SYM_TYPE_PARAM,
                                           tp->ident.name, tp_type, tp, 0);
        scope_add(b->arena, b->current_scope, tp_sym);
        symbol_add_member(b->arena, type_sym, tp_sym);
    }

    /* bind members */
    for (int i = 0; i < type_node->type_decl.members.count; i++) {
        zan_ast_node_t *member = type_node->type_decl.members.items[i];

        switch (member->kind) {
        case AST_FIELD_DECL:
        case AST_PROPERTY_DECL: {
            zan_type_t *field_type = zan_binder_resolve_type(b, member->field_decl.type);
            zan_symbol_t *field_sym = make_symbol(b->arena,
                member->kind == AST_PROPERTY_DECL ? SYM_PROPERTY : SYM_FIELD,
                member->field_decl.name, field_type, member,
                member->field_decl.modifiers);
            scope_add(b->arena, b->current_scope, field_sym);
            symbol_add_member(b->arena, type_sym, field_sym);
            break;
        }
        case AST_METHOD_DECL: {
            /* generic method: make its type params resolvable in the signature */
            register_type_param_list(b, &member->method_decl.type_params);
            zan_type_t *ret_type = zan_binder_resolve_type(b, member->method_decl.return_type);
            zan_symbol_t *method_sym = make_symbol(b->arena, SYM_METHOD,
                member->method_decl.name, ret_type, member,
                member->method_decl.modifiers);
            scope_add(b->arena, b->current_scope, method_sym);
            symbol_add_member(b->arena, type_sym, method_sym);

            /* bind parameters */
            for (int j = 0; j < member->method_decl.params.count; j++) {
                zan_ast_node_t *param = member->method_decl.params.items[j];
                zan_type_t *param_type = zan_binder_resolve_type(b, param->param.type);
                zan_symbol_t *param_sym = make_symbol(b->arena, SYM_PARAM,
                    param->param.name, param_type, param, 0);
                symbol_add_member(b->arena, method_sym, param_sym);
            }
            break;
        }
        case AST_CONSTRUCTOR_DECL: {
            zan_symbol_t *ctor_sym = make_symbol(b->arena, SYM_CONSTRUCTOR,
                member->method_decl.name, type_sym->type, member,
                member->method_decl.modifiers);
            scope_add(b->arena, b->current_scope, ctor_sym);
            symbol_add_member(b->arena, type_sym, ctor_sym);

            for (int j = 0; j < member->method_decl.params.count; j++) {
                zan_ast_node_t *param = member->method_decl.params.items[j];
                zan_type_t *param_type = zan_binder_resolve_type(b, param->param.type);
                zan_symbol_t *param_sym = make_symbol(b->arena, SYM_PARAM,
                    param->param.name, param_type, param, 0);
                symbol_add_member(b->arena, ctor_sym, param_sym);
            }
            break;
        }
        case AST_ENUM_MEMBER: {
            zan_symbol_t *em_sym = make_symbol(b->arena, SYM_ENUM_MEMBER,
                member->enum_member.name, type_sym->type, member, MOD_PUBLIC);
            scope_add(b->arena, b->current_scope, em_sym);
            symbol_add_member(b->arena, type_sym, em_sym);
            break;
        }
        default:
            break;
        }
    }

    b->current_scope = saved;
}

/* ---- main entry ---- */

void zan_binder_bind(zan_binder_t *b, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;

    /* pass 1: collect type declarations */
    bind_type_decls(b, &unit->comp_unit.decls);

    /* pass 1.5: give every implementing type a copy of the default interface
     * methods it does not override, before members are bound */
    bind_default_interface_methods(b, &unit->comp_unit.decls);

    /* pass 2: bind members of each type */
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind == AST_CLASS_DECL || decl->kind == AST_STRUCT_DECL ||
            decl->kind == AST_INTERFACE_DECL || decl->kind == AST_ENUM_DECL) {
            bind_members(b, decl);
        }
    }

    /* pass 3: resolve bases + inherit members, after every type's own
     * members exist (declaration order across files no longer matters) */
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind == AST_CLASS_DECL || decl->kind == AST_STRUCT_DECL ||
            decl->kind == AST_INTERFACE_DECL) {
            resolve_bases(b, decl);
        }
    }

    /* Every interface contract is checked after inherited members and default
     * methods are known, so a class method with the right name but the wrong
     * signature cannot silently satisfy the interface. */
    validate_interface_contracts(b, &unit->comp_unit.decls);

    /* keep generic type parameters resolvable for the checker / irgen passes */
    register_type_params(b, &unit->comp_unit.decls);

    b->binding_done = true;
}
