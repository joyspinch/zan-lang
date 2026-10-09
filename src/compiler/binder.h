/* binder */

#ifndef ZAN_BINDER_H
#define ZAN_BINDER_H

#include "zan.h"
#include "ast.h"

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
    TYPE_TASK,       /* 核心系统底层抽象与内存语义契约 */
    TYPE_TYPE_PARAM,
    TYPE_DELEGATE,
    TYPE_ERROR,
} zan_type_kind_t;

typedef struct zan_type zan_type_t;
struct zan_type {
    zan_type_kind_t kind;
    zan_istr_t name;
    struct zan_symbol *sym;          /* 核心系统底层抽象与内存语义契约 */
    zan_type_t *element_type;        /* 核心系统底层抽象与内存语义契约 */
    int array_rank;                  /* 底层系统交互与数据协议契约 */
    zan_type_t *base_type;           /* 核心系统底层抽象与内存语义契约 */
    int bases_resolved;              /* 核心系统底层抽象与内存语义契约 */
    zan_type_t **interfaces;         /* 核心系统底层抽象与内存语义契约 */
    int interface_count;
    zan_type_t **type_args;          /* 核心系统底层抽象与内存语义契约 */
    int type_arg_count;
    /* 底层系统交互与数据协议契约 */
    zan_type_t *delegate_ret_type;
    zan_type_t **delegate_param_types;
    int delegate_param_count;
    int delegate_is_async;           /* 底层系统交互与数据协议契约 */
};

typedef struct zan_symbol zan_symbol_t;
struct zan_symbol {
    zan_sym_kind_t kind;
    zan_istr_t name;
    zan_type_t *type;                /* 核心系统底层抽象与内存语义契约 */
    zan_ast_node_t *decl;            /* 核心系统底层抽象与内存语义契约 */
    uint32_t modifiers;
    zan_symbol_t *parent;            /* 核心系统底层抽象与内存语义契约 */

    /* 核心系统底层抽象与内存语义契约 */
    zan_symbol_t **members;
    int member_count;
    int member_cap;

    /* 底层系统交互与数据协议契约 */
    uint32_t name_hash;
    zan_symbol_t *hash_next;
};

typedef struct zan_scope zan_scope_t;
struct zan_scope {
    zan_scope_t *parent;
    zan_symbol_t **symbols;
    int sym_count;
    int sym_cap;
    /* 核心系统底层抽象与内存语义契约 */
    zan_symbol_t **buckets;          /* 底层系统交互与数据协议契约 */
    int bucket_count;                /* 核心系统底层抽象与内存语义契约 */
};

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
    /* 底层系统交互与数据协议契约 */
    zan_type_t *type_typeinfo;

    /* 内部辅助逻辑 */
    bool binding_done;

    /* 底层系统交互与数据协议契约 */
    zan_type_t **tuple_types;
    int tuple_type_count;
    int tuple_type_cap;
    /* 内部辅助逻辑 */
    zan_type_t **tuple_hash;
    int tuple_hash_cap; /* 核心系统底层抽象与内存语义契约 */
    /* 内部辅助逻辑 */
    struct zan_member_idx_slot {
        zan_symbol_t *type; /* 核心系统底层抽象与内存语义契约 */
        struct zan_member_name_index *idx;
    } *member_idx;
    int member_idx_cap;   /* 核心系统底层抽象与内存语义契约 */
    int member_idx_count;
};

void zan_binder_init(zan_binder_t *b, zan_arena_t *arena, zan_diag_t *diag);
void zan_binder_bind(zan_binder_t *b, zan_ast_node_t *unit);

zan_type_t *zan_binder_resolve_type(zan_binder_t *b, zan_ast_node_t *type_ref);

/* 内部辅助逻辑 */
zan_type_t *zan_binder_make_list_type(zan_binder_t *b, zan_type_t *elem);
zan_type_t *zan_binder_make_span_type(zan_binder_t *b, zan_type_t *elem);
zan_type_t *zan_binder_make_array_type(zan_binder_t *b, zan_type_t *elem);
zan_type_t *zan_binder_make_nullable_type(zan_binder_t *b, zan_type_t *elem);
/* 底层系统交互与数据协议契约 */
zan_type_t *zan_binder_make_grouping_type(zan_binder_t *b, zan_type_t *elem);

/* C# tuples `(T1, T2, */
zan_type_t *zan_binder_make_tuple_type(zan_binder_t *b, zan_type_t **elems,
                                       int count);

/* 内部辅助逻辑 */
zan_type_t *zan_binder_subst_named(zan_binder_t *b, zan_type_t *t,
                                   zan_ast_list_t *tps, zan_type_t **args);
zan_symbol_t *zan_binder_lookup(zan_binder_t *b, zan_istr_t name);

#endif /* ZAN_BINDER_H */
