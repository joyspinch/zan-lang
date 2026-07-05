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
} zan_sym_kind_t;

/* ---- type representation ---- */
typedef enum {
    TYPE_VOID,
    TYPE_BOOL,
    TYPE_BYTE,
    TYPE_SHORT,
    TYPE_INT,
    TYPE_LONG,
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
    TYPE_TYPE_PARAM,
    TYPE_ERROR,
} zan_type_kind_t;

typedef struct zan_type zan_type_t;
struct zan_type {
    zan_type_kind_t kind;
    zan_istr_t name;
    struct zan_symbol *sym;          /* back-pointer to declaring symbol */
    zan_type_t *element_type;        /* for arrays and nullable */
    zan_type_t *base_type;           /* base class type (for inheritance) */
    zan_type_t **type_args;          /* for generic instantiation */
    int type_arg_count;
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
};

/* ---- scope ---- */
typedef struct zan_scope zan_scope_t;
struct zan_scope {
    zan_scope_t *parent;
    zan_symbol_t **symbols;
    int sym_count;
    int sym_cap;
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
    zan_type_t *type_float;
    zan_type_t *type_double;
    zan_type_t *type_char;
    zan_type_t *type_string;
    zan_type_t *type_object;
    zan_type_t *type_nint;
    zan_type_t *type_error;
};

void zan_binder_init(zan_binder_t *b, zan_arena_t *arena, zan_diag_t *diag);
void zan_binder_bind(zan_binder_t *b, zan_ast_node_t *unit);

zan_type_t *zan_binder_resolve_type(zan_binder_t *b, zan_ast_node_t *type_ref);
zan_symbol_t *zan_binder_lookup(zan_binder_t *b, zan_istr_t name);

#endif /* ZAN_BINDER_H */
