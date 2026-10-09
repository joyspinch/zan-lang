/* 模块核心语义抽象与接口调用契约 */

#ifndef ZAN_AST_H
#define ZAN_AST_H

#include "zan.h"
#include "token.h"

typedef enum {
    AST_COMPILATION_UNIT,
    AST_USING_DECL,
    AST_NAMESPACE_DECL,

    AST_CLASS_DECL,
    AST_STRUCT_DECL,
    AST_INTERFACE_DECL,
    AST_ENUM_DECL,
    AST_DELEGATE_DECL,

    AST_FIELD_DECL,
    AST_METHOD_DECL,
    AST_CONSTRUCTOR_DECL,
    AST_DESTRUCTOR_DECL,
    AST_PROPERTY_DECL,
    AST_PARAM,

    AST_BLOCK,
    AST_VAR_DECL,
    AST_EXPR_STMT,
    AST_RETURN_STMT,
    AST_IF_STMT,
    AST_WHILE_STMT,
    AST_DO_WHILE_STMT,
    AST_FOR_STMT,
    AST_FOREACH_STMT,
    AST_BREAK_STMT,
    AST_CONTINUE_STMT,
    AST_THROW_STMT,
    AST_TRY_STMT,
    AST_SWITCH_STMT,

    AST_INT_LITERAL,
    AST_FLOAT_LITERAL,
    AST_STRING_LITERAL,
    AST_CHAR_LITERAL,
    AST_BOOL_LITERAL,
    AST_NULL_LITERAL,
    AST_IDENTIFIER,
    AST_BINARY,
    AST_UNARY,
    AST_CALL,
    AST_MEMBER_ACCESS,
    AST_INDEX,
    AST_ASSIGNMENT,
    AST_NEW_EXPR,
    AST_CAST_EXPR,
    AST_IS_EXPR,
    AST_AS_EXPR,
    AST_THIS_EXPR,
    AST_BASE_EXPR,
    AST_TYPEOF_EXPR,
    AST_SIZEOF_EXPR,
    AST_CONDITIONAL,
    AST_LAMBDA,
    AST_AWAIT_EXPR,
    AST_POSTFIX_UNARY,
    AST_STRING_INTERP,  /* $"text {expr} text" */

    /* 核心系统底层抽象与内存语义契约 */
    AST_TUPLE_EXPR,
    AST_TUPLE_TYPE,

    /* 核心系统底层抽象与内存语义契约 */
    AST_TUPLE_DECON,

    /* 核心系统底层抽象与内存语义契约 */
    AST_TYPE_REF,
    AST_ARRAY_TYPE,
    AST_NULLABLE_TYPE,
    AST_GENERIC_TYPE,
    AST_QUALIFIED_NAME,

    /* 核心系统底层抽象与内存语义契约 */
    AST_REF_ARG,

    /* 核心系统底层抽象与内存语义契约 */
    AST_NAMED_ARG,

    /* 内部辅助实现 */
    AST_COLL_INIT,

    AST_ATTRIBUTE,
    AST_ENUM_MEMBER,
    AST_CATCH_CLAUSE,
    AST_SWITCH_CASE,
    AST_WHERE_CLAUSE, /* 核心系统底层抽象与内存语义契约 */
    AST_YIELD_STMT,   /* 底层系统交互与数据协议契约 */
    AST_LOCK_STMT,    /* lock (expr) body */
    AST_CHECKED_STMT, 
/* 模块核心语义抽象与接口调用契约 */
    AST_GOTO_STMT,    /* 核心系统底层抽象与内存语义契约 */
    AST_LABEL_STMT,   /* label: */
    AST_QUERY_EXPR,   /* 核心系统底层抽象与内存语义契约 */
    AST_QUERY_WHERE,  /* 核心系统底层抽象与内存语义契约 */
    AST_QUERY_LET,    /* 核心系统底层抽象与内存语义契约 */
    AST_QUERY_ORDERBY,/* 核心系统底层抽象与内存语义契约 */
    AST_QUERY_JOIN,   /* 核心系统底层抽象与内存语义契约 */
    AST_SWITCH_EXPR,  /* 核心系统底层抽象与内存语义契约 */
    AST_SWITCH_ARM,   /* 核心系统底层抽象与内存语义契约 */
    AST_WITH_EXPR,    /* 底层系统交互与数据协议契约 */

    AST__COUNT,
} zan_ast_kind_t;

/* 核心系统底层抽象与内存语义契约 */
typedef struct {
    zan_ast_node_t **items;
    int count;
    int capacity;
} zan_ast_list_t;

/* 内部辅助实现 */
typedef struct {
    zan_istr_t extern_lib;        /* 核心系统底层抽象与内存语义契约 */
    zan_istr_t *entry_point;      /* 底层系统交互与数据协议契约 */
    zan_ast_list_t where_clauses; /* 核心系统底层抽象与内存语义契约 */
    zan_ast_list_t base_args;     /* 核心系统底层抽象与内存语义契约 */
} zan_method_ext_t;

/* 内部辅助实现 */
typedef struct {
    zan_ast_list_t attributes;
    zan_istr_t ns_name;
    zan_istr_t orig_name;
    zan_ast_list_t *ns_usings;
} zan_decl_meta_t;

struct zan_ast_node {
    zan_ast_kind_t kind;
    zan_loc_t loc;

    /* 模块核心语义抽象与接口调用契约 */
    uint8_t lit_suffix;

    /* 底层系统交互与数据协议契约 */
    uint8_t lit_radix;
    /* 模块核心语义抽象与接口调用契约 */
    unsigned char from_stdlib;
    uint8_t _pad;

    /* 模块核心语义抽象与接口调用契约 */
    zan_decl_meta_t *meta;

    union {
        /* literals */
        int64_t int_val;
        double float_val;
        bool bool_val;
        zan_istr_t str_val;

        /* identifier / name */
        struct {
            zan_istr_t name;
            /* 模块核心语义抽象与接口调用契约 */
            zan_ast_node_t *inst_type_ref;
        } ident;

        /* binary / assignment */
        struct {
            zan_token_kind_t op;
            /* 模块核心语义抽象与接口调用契约 */
            zan_token_kind_t compound_base;
            /* 模块核心语义抽象与接口调用契约 */
            int checked;
            zan_ast_node_t *left;
            zan_ast_node_t *right;
        } binary;

        /* 核心系统底层抽象与内存语义契约 */
        struct {
            zan_token_kind_t op;
            zan_ast_node_t *operand;
        } unary;

        /* 核心系统底层抽象与内存语义契约 */
        struct {
            zan_ast_node_t *callee;
            zan_ast_list_t args;
            zan_ast_list_t type_args; /* 核心系统底层抽象与内存语义契约 */
        } call;

        /* 模块核心语义抽象与接口调用契约 */
        struct {
            zan_ast_node_t *object;
            zan_istr_t name;
            int null_cond;
        } member;

        /* index: obj[i] or obj[i,j] (multi-dimensional) */
        struct {
            zan_ast_node_t *object;
            zan_ast_node_t *index;   /* 核心系统底层抽象与内存语义契约 */
            zan_ast_list_t extra;    /* 核心系统底层抽象与内存语义契约 */
        } index;

        /* conditional: cond ? then : else */
        struct {
            zan_ast_node_t *cond;
            zan_ast_node_t *then_expr;
            zan_ast_node_t *else_expr;
        } conditional;

        /* 核心系统底层抽象与内存语义契约 */
        struct {
            zan_ast_node_t *type;
            /* `FactoryCall( */
            zan_ast_node_t *call_init;
            zan_ast_list_t args;
            /* 模块核心语义抽象与接口调用契约 */
            zan_ast_list_t arg_inits;
            bool is_array;       /* 核心系统底层抽象与内存语义契约 */
            bool array_init;     /* 核心系统底层抽象与内存语义契约 */
            int array_rank;      
/* 模块核心语义抽象与接口调用契约 */
            bool list_copy;      
/* 模块核心语义抽象与接口调用契约 */
        } new_expr;

        /* cast: (Type)expr */
        struct {
            zan_ast_node_t *type;
            zan_ast_node_t *expr;
        } cast;

        /* is / as */
        struct {
            zan_ast_node_t *expr;
            zan_ast_node_t *type;
            zan_istr_t var_name; /* 核心系统底层抽象与内存语义契约 */
            bool is_not;         /* 核心系统底层抽象与内存语义契约 */
        } type_test;

    /* 核心系统底层抽象与内存语义契约 */
    struct {
        zan_istr_t name;
        zan_ast_node_t *type;        /* NULL if var (inferred) */
        zan_ast_node_t *initializer; /* NULL if none */
        bool is_const;               /* const */
        bool is_let;                 /* let (immutable) */
    } var_decl;

    /* 核心系统底层抽象与内存语义契约 */
    struct {
        zan_ast_list_t items;
    } tuple_expr;

    /* 核心系统底层抽象与内存语义契约 */
    struct {
        zan_ast_list_t elems;
    } tuple_type;

    /* 内部辅助实现 */
    struct {
        zan_ast_list_t names;
        zan_ast_list_t types;
        zan_ast_node_t *initializer;
    } tuple_decon;

        /* block: { statements } */
        struct {
            zan_ast_list_t stmts;
        } block;

        /* return */
        struct {
            zan_ast_node_t *value; /* 核心系统底层抽象与内存语义契约 */
        } ret;

        /* if */
        struct {
            zan_ast_node_t *cond;
            zan_ast_node_t *then_body;
            zan_ast_node_t *else_body; /* NULL or else/else-if */
        } if_stmt;

        /* while / do-while */
        struct {
            zan_ast_node_t *cond;
            zan_ast_node_t *body;
        } while_stmt;

        /* for (init; cond; step) body */
        struct {
            zan_ast_node_t *init;
            zan_ast_node_t *cond;
            zan_ast_node_t *step;
            zan_ast_node_t *body;
        } for_stmt;

        /* foreach (var x in collection) body */
        struct {
            zan_istr_t var_name;
            zan_ast_node_t *var_type; /* NULL if var */
            zan_ast_node_t *collection;
            zan_ast_node_t *body;
        } foreach_stmt;

        /* throw */
        struct {
            zan_ast_node_t *value;
        } throw_stmt;

        /* try-catch-finally */
        struct {
            zan_ast_node_t *try_body;
            zan_ast_list_t catches;
            zan_ast_node_t *finally_body; /* NULL if none */
        } try_stmt;

        /* 核心系统底层抽象与内存语义契约 */
        struct {
            zan_ast_node_t *type;
            zan_istr_t var_name;
            zan_ast_node_t *body;
        } catch_clause;

        /* switch */
        struct {
            zan_ast_node_t *expr;
            zan_ast_list_t cases;
        } switch_stmt;

        /* 核心系统底层抽象与内存语义契约 */
        struct {
            zan_ast_node_t *pattern; /* 核心系统底层抽象与内存语义契约 */
            zan_ast_node_t *type_pattern; /* 核心系统底层抽象与内存语义契约 */
            zan_ast_node_t *when_cond;    /* 底层系统交互与数据协议契约 */
            zan_ast_node_t *body;
            zan_istr_t var_name; /* 核心系统底层抽象与内存语义契约 */
        } switch_case;

        /* 核心系统底层抽象与内存语义契约 */
        struct {
            zan_ast_node_t *expr;
            zan_ast_list_t arms;
        } switch_expr;

        /* 底层系统交互与数据协议契约 */
        struct {
            zan_ast_node_t *expr;
            zan_ast_list_t assigns;
        } with_expr;

        /* 模块核心语义抽象与接口调用契约 */
        struct {
            zan_ast_node_t *pattern;     /* 底层系统交互与数据协议契约 */
            zan_ast_node_t *type_pattern; /* 核心系统底层抽象与内存语义契约 */
            zan_ast_node_t *when_cond;    /* 核心系统底层抽象与内存语义契约 */
            zan_ast_node_t *result;
            zan_istr_t var_name;          /* 核心系统底层抽象与内存语义契约 */
            bool is_default;              /* `_ =>` / `default =>` */
        } switch_arm;

        /* 核心系统底层抽象与内存语义契约 */
        struct {
            zan_ast_node_t *expr;
        } expr_stmt;

        /* 核心系统底层抽象与内存语义契约 */
        struct {
            zan_ast_node_t *name;     /* 核心系统底层抽象与内存语义契约 */
            bool is_static;
        } using_decl;

        /* namespace */
        struct {
            zan_ast_node_t *name;     /* 核心系统底层抽象与内存语义契约 */
            zan_ast_list_t members;   /* 核心系统底层抽象与内存语义契约 */
            bool is_file_scoped;
        } namespace_decl;

        /* 核心系统底层抽象与内存语义契约 */
        struct {
            zan_ast_list_t usings;
            zan_ast_node_t *ns;       /* namespace */
            zan_ast_list_t decls;     /* 核心系统底层抽象与内存语义契约 */
        } comp_unit;

        /* class / struct / interface */
        struct {
            zan_istr_t name;
            zan_ast_list_t type_params;
            zan_ast_list_t bases;      /* 核心系统底层抽象与内存语义契约 */
            zan_ast_list_t members;
            uint32_t modifiers;
            bool is_c_layout;  /* [StructLayout(LayoutKind.Sequential)] for C ABI */
            bool is_explicit_layout; 
/* 底层系统交互与数据协议契约 */
            zan_ast_list_t *where_clauses; /* 底层系统交互与数据协议契约 */
            /* 模块核心语义抽象与接口调用契约 */
            zan_ast_node_t *nested_host;
        } type_decl;

        /* method / constructor */
        struct {
            zan_istr_t name;
            zan_ast_node_t *return_type;
            zan_ast_list_t params;
            zan_ast_list_t type_params;
            zan_ast_node_t *body;
            uint32_t modifiers;
            bool is_variadic;        
/* [DllImport( */
            bool has_base_init;     /* 核心系统底层抽象与内存语义契约 */
            bool has_this_init;
            bool is_task_return;    /* 底层系统交互与数据协议契约 */
            zan_method_ext_t *ext;   /* 底层系统交互与数据协议契约 */
        } method_decl;

        /* field */
        struct {
            zan_istr_t name;
            zan_ast_node_t *type;
            zan_ast_node_t *initializer;
            uint32_t modifiers;
            /* 底层系统交互与数据协议契约 */
            zan_ast_node_t *getter_body;
            zan_ast_node_t *setter_body;
            /* 检查是否the corresponding accessor keyword was present at all (`get`/`set` in the `{ */
            bool has_getter;
            bool has_setter;
            bool has_init;
            /* 内部辅助实现 */
            zan_ast_list_t *indexer_params;
        } field_decl;

        /* 核心系统底层抽象与内存语义契约 */
        struct {
            zan_istr_t param_name;
            zan_ast_list_t constraints; /* 核心系统底层抽象与内存语义契约 */
        } where_clause;

        /* 模块核心语义抽象与接口调用契约 */
        struct {
            zan_ast_node_t *value;
        } yield_stmt;

        /* lock (expr) body */
        struct {
            zan_ast_node_t *expr;
            zan_ast_node_t *body;
        } lock_stmt;

        /* 内部辅助实现 */
        struct {
            zan_ast_node_t *body;
            bool checked;
        } checked_stmt;

        /* 内部辅助实现 */
        struct {
            zan_istr_t var;
            zan_ast_node_t *source;
            zan_ast_list_t clauses; 
/* 模块核心语义抽象与接口调用契约 */
            zan_ast_node_t *group_expr; /* `group <expr> by <key>` element */
            zan_ast_node_t *group_key;  /* 核心系统底层抽象与内存语义契约 */
            zan_istr_t group_into;      /* `into <name>` var (empty = none) */
            zan_ast_node_t *select;
        } query;

        /* 模块核心语义抽象与接口调用契约 */
        struct {
            zan_istr_t name;        /* 核心系统底层抽象与内存语义契约 */
            zan_ast_node_t *expr;   /* 底层系统交互与数据协议契约 */
            int descending;         /* orderby: 1 = descending */
            zan_ast_node_t *source;     /* 核心系统底层抽象与内存语义契约 */
            zan_ast_node_t *left_key;   /* 底层系统交互与数据协议契约 */
            zan_ast_node_t *right_key;  /* 底层系统交互与数据协议契约 */
            zan_istr_t into;            /* 核心系统底层抽象与内存语义契约 */
        } query_clause;

        /* parameter */
        struct {
            zan_istr_t name;
            zan_ast_node_t *type;
            zan_ast_node_t *default_val;
            int is_params; /* 核心系统底层抽象与内存语义契约 */
            int by_ref;    /* 0 = by value, 1 = `ref`, 2 = `out` */
            int is_this;   /* 底层系统交互与数据协议契约 */
        } param;

        /* 底层系统交互与数据协议契约 */
        struct {
            zan_ast_node_t *expr;      /* 核心系统底层抽象与内存语义契约 */
            zan_ast_node_t *decl_type; /* 底层系统交互与数据协议契约 */
            int is_out;
        } ref_arg;

        /* 模块核心语义抽象与接口调用契约 */
        struct {
            zan_istr_t name;
            zan_ast_node_t *expr;
        } named_arg;

        /* 内部辅助实现 */
        struct {
            zan_istr_t name;
            zan_ast_list_t items;
        } coll_init;

        /* 核心系统底层抽象与内存语义契约 */
        struct {
            zan_istr_t name;
            zan_ast_list_t type_args;
            bool is_nullable;
            bool is_array;
            /* 模块核心语义抽象与接口调用契约 */
            int array_rank;
            zan_ast_node_t *array_element;
            /* 模块核心语义抽象与接口调用契约 */
            void *rt_type;
            void *rt_scope;
        } type_ref;

        /* 核心系统底层抽象与内存语义契约 */
        struct {
            zan_ast_list_t parts; /* 核心系统底层抽象与内存语义契约 */
        } qualified_name;

        /* attribute */
        struct {
            zan_ast_node_t *name;
            zan_ast_list_t args;
        } attribute;

        /* 核心系统底层抽象与内存语义契约 */
        struct {
            zan_istr_t name;
            zan_ast_node_t *value; /* 核心系统底层抽象与内存语义契约 */
        } enum_member;

        /* 核心系统底层抽象与内存语义契约 */
        struct {
            zan_ast_node_t *expr;
        } await_expr;

        /* lambda: (params) => body */
        struct {
            zan_ast_list_t params;
            zan_ast_node_t *body; /* block or expr */
        } lambda;

        /* 核心系统底层抽象与内存语义契约 */
        struct {
            zan_ast_list_t parts; /* 底层系统交互与数据协议契约 */
            /* 模块核心语义抽象与接口调用契约 */
            zan_ast_list_t formats;
        } string_interp;
    };
};

/* 核心系统底层抽象与内存语义契约 */

#define MOD_PUBLIC    0x0001
#define MOD_PRIVATE   0x0002
#define MOD_PROTECTED 0x0004
#define MOD_INTERNAL  0x0008
#define MOD_STATIC    0x0010
#define MOD_VIRTUAL   0x0020
#define MOD_OVERRIDE  0x0040
#define MOD_ABSTRACT  0x0080
#define MOD_SEALED    0x0100
#define MOD_READONLY  0x0200
#define MOD_EXTERN    0x0400
#define MOD_ASYNC     0x0800
#define MOD_UNSAFE    0x1000
#define MOD_WEAK      0x2000
#define MOD_EVENT     0x4000
#define MOD_PARTIAL   0x8000
#define MOD_REF       0x10000  /* 底层系统交互与数据协议契约 */

zan_ast_node_t *zan_ast_new(zan_arena_t *arena, zan_ast_kind_t kind, zan_loc_t loc);
size_t zan_ast_node_count(void);
bool zan_ast_has_attr(const zan_ast_node_t *decl, const char *name);
void zan_ast_list_init(zan_ast_list_t *list);
void zan_ast_list_push(zan_ast_list_t *list, zan_ast_node_t *node, zan_arena_t *arena);

zan_method_ext_t *zan_ast_ensure_method_ext(zan_ast_node_t *n, zan_arena_t *arena);

static inline zan_istr_t zan_ast_method_extern_lib(const zan_ast_node_t *n) {
    static const zan_istr_t empty = {0};
    return (n && (n->kind == AST_METHOD_DECL || n->kind == AST_CONSTRUCTOR_DECL) &&
            n->method_decl.ext) ? n->method_decl.ext->extern_lib : empty;
}

static inline zan_istr_t *zan_ast_method_entry_point(const zan_ast_node_t *n) {
    return (n && (n->kind == AST_METHOD_DECL || n->kind == AST_CONSTRUCTOR_DECL) &&
            n->method_decl.ext) ? n->method_decl.ext->entry_point : NULL;
}

static inline zan_ast_list_t *zan_ast_method_where_clauses(zan_ast_node_t *n) {
    static zan_ast_list_t empty = {0};
    return (n && (n->kind == AST_METHOD_DECL || n->kind == AST_CONSTRUCTOR_DECL) &&
            n->method_decl.ext) ? &n->method_decl.ext->where_clauses : &empty;
}

static inline zan_ast_list_t *zan_ast_method_base_args(zan_ast_node_t *n) {
    static zan_ast_list_t empty = {0};
    return (n && (n->kind == AST_METHOD_DECL || n->kind == AST_CONSTRUCTOR_DECL) &&
            n->method_decl.ext) ? &n->method_decl.ext->base_args : &empty;
}

static inline zan_ast_list_t *zan_ast_type_where_clauses(zan_ast_node_t *n) {
    static zan_ast_list_t empty = {0};
    return (n && (n->kind == AST_CLASS_DECL || n->kind == AST_STRUCT_DECL ||
                  n->kind == AST_INTERFACE_DECL) &&
            n->type_decl.where_clauses) ? n->type_decl.where_clauses : &empty;
}

zan_decl_meta_t *zan_ast_ensure_decl_meta(zan_ast_node_t *n, zan_arena_t *arena);

static inline zan_ast_list_t *zan_ast_attributes(zan_ast_node_t *n) {
    static zan_ast_list_t empty = {0};
    return (n && n->meta) ? &n->meta->attributes : &empty;
}

static inline const zan_ast_list_t *zan_ast_attributes_const(const zan_ast_node_t *n) {
    static const zan_ast_list_t empty = {0};
    return (n && n->meta) ? &n->meta->attributes : &empty;
}

static inline zan_istr_t zan_ast_ns_name(const zan_ast_node_t *n) {
    static const zan_istr_t empty = {0};
    return (n && n->meta) ? n->meta->ns_name : empty;
}

static inline zan_istr_t zan_ast_orig_name(const zan_ast_node_t *n) {
    static const zan_istr_t empty = {0};
    return (n && n->meta) ? n->meta->orig_name : empty;
}

static inline zan_ast_list_t *zan_ast_ns_usings(const zan_ast_node_t *n) {
    return (n && n->meta) ? n->meta->ns_usings : NULL;
}

#endif /* ZAN_AST_H */
