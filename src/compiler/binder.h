/* binder.h -- Name resolution and symbol table for the Zan language. */

#ifndef ZAN_BINDER_H
#define ZAN_BINDER_H

#include "zan.h"
#include "ast.h"

/* ---- symbol kinds ---- */
typedef enum {
    SYM_NAMESPACE,
    SYM_CLASS,
    SYM_STRUCT,
    SYM_INTERFACE,
    SYM_ENUM,
    SYM_METHOD,
    SYM_CONSTRUCTOR,
    SYM_FIELD,
    SYM_PROPERTY,
    SYM_PARAM,
    SYM_LOCAL,
    SYM_ENUM_MEMBER,
    SYM_TYPE_PARAM,
    SYM_DELEGATE,
} zan_sym_kind_t;

/* ---- type representation ---- */
typedef enum {
    TYPE_VOID,
    TYPE_BOOL,
    TYPE_BYTE,
    TYPE_SHORT,
    TYPE_INT,
    TYPE_LONG,
    TYPE_SBYTE,
    TYPE_USHORT,
    TYPE_UINT,
    TYPE_ULONG,
    TYPE_FLOAT,
    TYPE_DOUBLE,
    TYPE_CHAR,
    TYPE_STRING,
    TYPE_OBJECT,
    TYPE_NINT,
    TYPE_CLASS,
    TYPE_STRUCT,
    TYPE_INTERFACE,
    TYPE_ENUM,
    TYPE_ARRAY,
    TYPE_NULLABLE,
    TYPE_TASK,       /* Task / Task<T>: a coroutine handle (opaque i64) */
    TYPE_TYPE_PARAM,
    TYPE_DELEGATE,
    TYPE_ERROR,
} zan_type_kind_t;

typedef struct zan_type zan_type_t;
struct zan_type {
    zan_type_kind_t kind;
    zan_istr_t name;
    struct zan_symbol *sym;          /* back-pointer to declaring symbol */
    zan_type_t *element_type;        /* for arrays and nullable */
    int array_rank;                  /* TYPE_ARRAY: 1 = one-dimensional,
                                      * N = rank-N rectangular array */
    zan_type_t *base_type;           /* base class type (for inheritance) */
    int bases_resolved;              /* 0 = not yet, 1 = in progress, 2 = done */
    zan_type_t **interfaces;         /* implemented interfaces (for classes) */
    int interface_count;
    zan_type_t **type_args;          /* for generic instantiation */
    int type_arg_count;
    /* delegate signature (only for TYPE_DELEGATE) */
    zan_type_t *delegate_ret_type;
    zan_type_t **delegate_param_types;
    int delegate_param_count;
    int delegate_is_async;           /* declared `async delegate`: invocation
                                      * yields a task handle (i8*) and is awaited */
};

/* ---- symbol table entry ---- */
typedef struct zan_symbol zan_symbol_t;
struct zan_symbol {
    zan_sym_kind_t kind;
    zan_istr_t name;
    zan_type_t *type;                /* resolved type */
    zan_ast_node_t *decl;            /* back-pointer to AST declaration */
    uint32_t modifiers;
    zan_symbol_t *parent;            /* enclosing scope symbol */

    /* children (for types, namespaces, methods) */
    zan_symbol_t **members;
    int member_count;
    int member_cap;

    /* P2: scope name-index chain, owned by the scope the symbol was added
     * to. name_hash is FNV-1a over `name`, computed once at scope_add;
     * hash_next links the other symbols of the same hash bucket. Arena
     * memory is not zeroed, so both fields must be initialized by
     * make_symbol. */
    uint32_t name_hash;
    zan_symbol_t *hash_next;
};

/* ---- scope ---- */
typedef struct zan_scope zan_scope_t;
struct zan_scope {
    zan_scope_t *parent;
    zan_symbol_t **symbols;
    int sym_count;
    int sym_cap;
    /* P2: O(1) name index over `symbols`. The array stays the authoritative
     * storage in insertion order; `buckets` points at the same symbols,
     * chained via hash_next with every bucket also kept in insertion order,
     * so the first same-name symbol per scope is still the first one added
     * and overload / redeclaration diagnostics see exactly what the old
     * linear scan returned. */
    zan_symbol_t **buckets;          /* NULL until the first symbol is added */
    int bucket_count;                /* power of two, or 0 when empty */
};

/* ---- binder context ---- */
struct zan_binder {
    zan_arena_t *arena;
    zan_diag_t *diag;
    zan_scope_t *current_scope;

    /* built-in types */
    zan_type_t *type_void;
    zan_type_t *type_bool;
    zan_type_t *type_byte;
    zan_type_t *type_short;
    zan_type_t *type_int;
    zan_type_t *type_long;
    zan_type_t *type_sbyte;
    zan_type_t *type_ushort;
    zan_type_t *type_uint;
    zan_type_t *type_ulong;
    zan_type_t *type_float;
    zan_type_t *type_double;
    zan_type_t *type_char;
    zan_type_t *type_string;
    zan_type_t *type_object;
    zan_type_t *type_nint;
    zan_type_t *type_error;
    /* `TypeInfo`, the static type of `typeof(T)` and `obj.GetType()`. A value
     * of it is a pointer to a compiler-emitted type record whose payload
     * starts at the type's display name, so a TypeInfo IS a valid (immortal)
     * Zan string carrying that name: printing or concatenating one keeps
     * working, and the record's reflection slots sit at negative offsets in
     * front of it (see irgen_reflect.c). */
    zan_type_t *type_typeinfo;

    /* Set once every declaration is bound: a type reference's meaning is only
     * stable after that, so zan_binder_resolve_type memoizes from here on. */
    bool binding_done;

    /* Canonical tuple structs, one per distinct element-type signature. Each
     * entry is a TYPE_STRUCT whose symbol carries Item1..ItemN field members;
     * see zan_binder_make_tuple_type. */
    zan_type_t **tuple_types;
    int tuple_type_count;
    int tuple_type_cap;
};

void zan_binder_init(zan_binder_t *b, zan_arena_t *arena, zan_diag_t *diag);
void zan_binder_bind(zan_binder_t *b, zan_ast_node_t *unit);

zan_type_t *zan_binder_resolve_type(zan_binder_t *b, zan_ast_node_t *type_ref);

/* Construct a List<elem> instantiation type (used by query-expression
 * lowering, which has no syntactic type reference to resolve). */
zan_type_t *zan_binder_make_list_type(zan_binder_t *b, zan_type_t *elem);
zan_type_t *zan_binder_make_span_type(zan_binder_t *b, zan_type_t *elem);
zan_type_t *zan_binder_make_array_type(zan_binder_t *b, zan_type_t *elem);
zan_type_t *zan_binder_make_nullable_type(zan_binder_t *b, zan_type_t *elem);
/* Construct a Grouping<elem> instantiation (query `group e by k` lowering). */
zan_type_t *zan_binder_make_grouping_type(zan_binder_t *b, zan_type_t *elem);

/* C# tuples `(T1, T2, ...)` lower to a canonical anonymous struct with
 * Item1..ItemN fields. Two tuple types with the same element signature are the
 * same struct (structural identity), so a `(int, string)` return type matches
 * the `(int, string)` a caller infers for the value it hands back. */
zan_type_t *zan_binder_make_tuple_type(zan_binder_t *b, zan_type_t **elems,
                                       int count);

/* Substitute the type parameters named by `tps` with `args` throughout `t`,
 * cloning composite types on the way (List<T> -> List<Square>). */
zan_type_t *zan_binder_subst_named(zan_binder_t *b, zan_type_t *t,
                                   zan_ast_list_t *tps, zan_type_t **args);
zan_symbol_t *zan_binder_lookup(zan_binder_t *b, zan_istr_t name);

#endif /* ZAN_BINDER_H */
