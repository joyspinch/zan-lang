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
#include <string.h>

/* ---- helpers ---- */

static zan_type_t *make_type(zan_arena_t *arena, zan_type_kind_t kind, const char *name, int len) {
    zan_type_t *t = (zan_type_t *)zan_arena_alloc(arena, sizeof(zan_type_t));
    t->kind = kind;
    t->name.str = name;
    t->name.len = len;
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

zan_type_t *zan_binder_resolve_type(zan_binder_t *b, zan_ast_node_t *type_ref) {
    if (!type_ref || type_ref->kind != AST_TYPE_REF) {
        return b->type_error;
    }

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
    else if (istr_eq(name, "float",  5)) base = b->type_float;
    else if (istr_eq(name, "double", 6)) base = b->type_double;
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

    /* carry generic arguments (e.g. List<Node>, Dict<K,V>) onto the fresh
     * built-in generic type so codegen can recover the element type. */
    if ((istr_eq(name, "List", 4) || istr_eq(name, "Dict", 4) ||
         istr_eq(name, "Dictionary", 10)) &&
        type_ref->type_ref.type_args.count > 0) {
        int nargs = type_ref->type_ref.type_args.count;
        base->type_args = (zan_type_t **)zan_arena_alloc(
            b->arena, sizeof(zan_type_t *) * (size_t)nargs);
        base->type_arg_count = nargs;
        for (int i = 0; i < nargs; i++) {
            base->type_args[i] = zan_binder_resolve_type(
                b, type_ref->type_ref.type_args.items[i]);
        }
    }

    zan_type_t *resolved = base;
    /* wrap in array if needed */
    if (type_ref->type_ref.is_array) {
        zan_type_t *arr = make_type(b->arena, TYPE_ARRAY, name.str, name.len);
        arr->element_type = resolved;
        resolved = arr;
    }
    /* wrap in nullable if needed */
    if (type_ref->type_ref.is_nullable) {
        zan_type_t *nullable = make_type(b->arena, TYPE_NULLABLE, name.str, name.len);
        nullable->element_type = resolved;
        resolved = nullable;
    }
    return resolved;
}

/* ---- symbol lookup ---- */

zan_symbol_t *zan_binder_lookup(zan_binder_t *b, zan_istr_t name) {
    return scope_find(b->current_scope, name);
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
            /* resolve delegate return type */
            type->delegate_ret_type = node->method_decl.return_type
                ? zan_binder_resolve_type(b, node->method_decl.return_type)
                : b->type_void;
            /* resolve delegate parameter types */
            int pc = node->method_decl.params.count;
            type->delegate_param_count = pc;
            if (pc > 0) {
                type->delegate_param_types = (zan_type_t **)zan_arena_alloc(
                    b->arena, sizeof(zan_type_t *) * (size_t)pc);
                for (int j = 0; j < pc; j++) {
                    zan_ast_node_t *param = node->method_decl.params.items[j];
                    type->delegate_param_types[j] = zan_binder_resolve_type(b, param->param.type);
                }
            } else {
                type->delegate_param_types = NULL;
            }
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
}

/* Pass 2: bind member declarations */
static void bind_members(zan_binder_t *b, zan_ast_node_t *type_node) {
    zan_istr_t type_name = type_node->type_decl.name;
    zan_symbol_t *type_sym = scope_find(b->current_scope, type_name);
    if (!type_sym) return;

    /* resolve base types and inherit members */
    if (type_node->type_decl.bases.count > 0) {
        zan_ast_node_t *base_ref = type_node->type_decl.bases.items[0];
        zan_istr_t base_name = base_ref->type_ref.name;
        zan_symbol_t *base_sym = scope_find(b->current_scope, base_name);
        if (base_sym && (base_sym->kind == SYM_CLASS || base_sym->kind == SYM_STRUCT)) {
            type_sym->type->base_type = base_sym->type;
            /* inherit base class fields and properties */
            for (int bi = 0; bi < base_sym->member_count; bi++) {
                if (base_sym->members[bi]->kind == SYM_FIELD ||
                    base_sym->members[bi]->kind == SYM_PROPERTY) {
                    symbol_add_member(b->arena, type_sym, base_sym->members[bi]);
                }
            }
        }
    }

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

    /* pass 2: bind members of each type */
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind == AST_CLASS_DECL || decl->kind == AST_STRUCT_DECL ||
            decl->kind == AST_INTERFACE_DECL || decl->kind == AST_ENUM_DECL) {
            bind_members(b, decl);
        }
    }
}
