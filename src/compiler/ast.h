
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
    AST_DECIMAL_LITERAL,
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

    AST_TUPLE_EXPR,
    AST_TUPLE_TYPE,

    AST_TUPLE_DECON,

    AST_TYPE_REF,
    AST_ARRAY_TYPE,
    AST_NULLABLE_TYPE,
    AST_GENERIC_TYPE,
    AST_QUALIFIED_NAME,

    AST_REF_ARG,

    AST_NAMED_ARG,

    AST_COLL_INIT,

    AST_ATTRIBUTE,
    AST_ENUM_MEMBER,
    AST_CATCH_CLAUSE,
    AST_SWITCH_CASE,
    AST_WHERE_CLAUSE,
    AST_YIELD_STMT,
    AST_LOCK_STMT,    /* lock (expr) body */
    AST_CHECKED_STMT, 
    AST_GOTO_STMT,
    AST_LABEL_STMT,   /* label: */
    AST_QUERY_EXPR,
    AST_QUERY_WHERE,
    AST_QUERY_LET,
    AST_QUERY_ORDERBY,
    AST_QUERY_JOIN,
    AST_SWITCH_EXPR,
    AST_SWITCH_ARM,
    AST_WITH_EXPR,

    AST__COUNT,
} zan_ast_kind_t;

typedef struct {
    zan_ast_node_t **items;
    int count;
    int capacity;
} zan_ast_list_t;

typedef struct {
    zan_istr_t extern_lib;
    zan_istr_t *entry_point;
    zan_ast_list_t where_clauses;
    zan_ast_list_t base_args;
} zan_method_ext_t;

typedef struct {
    zan_ast_list_t attributes;
    zan_istr_t ns_name;
    zan_istr_t orig_name;
    zan_ast_list_t *ns_usings;
} zan_decl_meta_t;

struct zan_ast_node {
    zan_ast_kind_t kind;
    zan_loc_t loc;

    uint8_t lit_suffix;

    uint8_t lit_radix;
    unsigned char from_stdlib;
    uint8_t _pad;

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
            zan_ast_node_t *inst_type_ref;
        } ident;

        /* binary / assignment */
        struct {
            zan_token_kind_t op;
            zan_token_kind_t compound_base;
            int checked;
            zan_ast_node_t *left;
            zan_ast_node_t *right;
        } binary;

        struct {
            zan_token_kind_t op;
            zan_ast_node_t *operand;
        } unary;

        struct {
            zan_ast_node_t *callee;
            zan_ast_list_t args;
            zan_ast_list_t type_args;
        } call;

        struct {
            zan_ast_node_t *object;
            zan_istr_t name;
            int null_cond;
        } member;

        /* index: obj[i] or obj[i,j] (multi-dimensional) */
        struct {
            zan_ast_node_t *object;
            zan_ast_node_t *index;
            zan_ast_list_t extra;
        } index;

        /* conditional: cond ? then : else */
        struct {
            zan_ast_node_t *cond;
            zan_ast_node_t *then_expr;
            zan_ast_node_t *else_expr;
        } conditional;

        struct {
            zan_ast_node_t *type;
            /* `FactoryCall( */
            zan_ast_node_t *call_init;
            zan_ast_list_t args;
            zan_ast_list_t arg_inits;
            bool is_array;
            bool array_init;
            int array_rank;      
            bool list_copy;      
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
            zan_istr_t var_name;
            bool is_not;
        } type_test;

    struct {
        zan_istr_t name;
        zan_ast_node_t *type;        /* NULL if var (inferred) */
        zan_ast_node_t *initializer; /* NULL if none */
        bool is_const;               /* const */
        bool is_let;                 /* let (immutable) */
    } var_decl;

    struct {
        zan_ast_list_t items;
    } tuple_expr;

    struct {
        zan_ast_list_t elems;
    } tuple_type;

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
            zan_ast_node_t *value;
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

        struct {
            zan_ast_node_t *pattern;
            zan_ast_node_t *type_pattern;
            zan_ast_node_t *when_cond;
            zan_ast_node_t *body;
            zan_istr_t var_name;
        } switch_case;

        struct {
            zan_ast_node_t *expr;
            zan_ast_list_t arms;
        } switch_expr;

        struct {
            zan_ast_node_t *expr;
            zan_ast_list_t assigns;
        } with_expr;

        struct {
            zan_ast_node_t *pattern;
            zan_ast_node_t *type_pattern;
            zan_ast_node_t *when_cond;
            zan_ast_node_t *result;
            zan_istr_t var_name;
            bool is_default;              /* `_ =>` / `default =>` */
        } switch_arm;

        struct {
            zan_ast_node_t *expr;
        } expr_stmt;

        struct {
            zan_ast_node_t *name;
            bool is_static;
        } using_decl;

        /* namespace */
        struct {
            zan_ast_node_t *name;
            zan_ast_list_t members;
            bool is_file_scoped;
        } namespace_decl;

        struct {
            zan_ast_list_t usings;
            zan_ast_node_t *ns;       /* namespace */
            zan_ast_list_t decls;
        } comp_unit;

        /* class / struct / interface */
        struct {
            zan_istr_t name;
            zan_ast_list_t type_params;
            zan_ast_list_t bases;
            zan_ast_list_t members;
            uint32_t modifiers;
            bool is_c_layout;  /* [StructLayout(LayoutKind.Sequential)] for C ABI */
            bool is_explicit_layout; 
            zan_ast_list_t *where_clauses;
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
            bool has_base_init;
            bool has_this_init;
            bool is_task_return;
            zan_method_ext_t *ext;
        } method_decl;

        /* field */
        struct {
            zan_istr_t name;
            zan_ast_node_t *type;
            zan_ast_node_t *initializer;
            uint32_t modifiers;
            zan_ast_node_t *getter_body;
            zan_ast_node_t *setter_body;
            /* 检查是否the corresponding accessor keyword was present at all (`get`/`set` in the `{ */
            bool has_getter;
            bool has_setter;
            bool has_init;
            zan_ast_list_t *indexer_params;
        } field_decl;

        struct {
            zan_istr_t param_name;
            zan_ast_list_t constraints;
            bool has_new;
            bool has_class;
            bool has_struct;
        } where_clause;

        struct {
            zan_ast_node_t *value;
        } yield_stmt;

        /* lock (expr) body */
        struct {
            zan_ast_node_t *expr;
            zan_ast_node_t *body;
        } lock_stmt;

        struct {
            zan_ast_node_t *body;
            bool checked;
        } checked_stmt;

        struct {
            zan_istr_t var;
            zan_ast_node_t *source;
            zan_ast_list_t clauses; 
            zan_ast_node_t *group_expr; /* `group <expr> by <key>` element */
            zan_ast_node_t *group_key;
            zan_istr_t group_into;      /* `into <name>` var (empty = none) */
            zan_ast_node_t *select;
        } query;

        struct {
            zan_istr_t name;
            zan_ast_node_t *expr;
            int descending;         /* orderby: 1 = descending */
            zan_ast_node_t *source;
            zan_ast_node_t *left_key;
            zan_ast_node_t *right_key;
            zan_istr_t into;
        } query_clause;

        /* parameter */
        struct {
            zan_istr_t name;
            zan_ast_node_t *type;
            zan_ast_node_t *default_val;
            int is_params;
            int by_ref;    /* 0 = by value, 1 = `ref`, 2 = `out` */
            int is_this;
        } param;

        struct {
            zan_ast_node_t *expr;
            zan_ast_node_t *decl_type;
            int is_out;
        } ref_arg;

        struct {
            zan_istr_t name;
            zan_ast_node_t *expr;
        } named_arg;

        struct {
            zan_istr_t name;
            zan_ast_list_t items;
        } coll_init;

        struct {
            zan_istr_t name;
            zan_ast_list_t type_args;
            bool is_nullable;
            bool is_array;
            int array_rank;
            zan_ast_node_t *array_element;
            void *rt_type;
            void *rt_scope;
        } type_ref;

        struct {
            zan_ast_list_t parts;
        } qualified_name;

        /* attribute */
        struct {
            zan_ast_node_t *name;
            zan_ast_list_t args;
        } attribute;

        struct {
            zan_istr_t name;
            zan_ast_node_t *value;
        } enum_member;

        struct {
            zan_ast_node_t *expr;
        } await_expr;

        /* lambda: (params) => body */
        struct {
            zan_ast_list_t params;
            zan_ast_node_t *body; /* block or expr */
        } lambda;

        struct {
            zan_ast_list_t parts;
            zan_ast_list_t formats;
        } string_interp;
    };
};


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
#define MOD_REF       0x10000

zan_ast_node_t *zan_ast_new(zan_arena_t *arena, zan_ast_kind_t kind, zan_loc_t loc);
size_t zan_ast_node_count(void);
bool zan_ast_has_attr(const zan_ast_node_t *decl, const char *name);
void zan_ast_list_init(zan_ast_list_t *list);
void zan_ast_list_push(zan_ast_list_t *list, zan_ast_node_t *node, zan_arena_t *arena);

/* 实参里藏着的 lambda：裸 lambda，或三元表达式 / 强制转换里的一支是
 * lambda。没有 lambda 返回 NULL。
 *
 * 为什么要有这个：lambda 没有自己的类型（类型由目标委托形参决定），
 * 所以判「实参接不接得住形参」时必须先找到这个 lambda，再拿形参的
 * 委托类型去比——拿实参自身的类型比是比不出来的（未知类型）。
 *
 * 踩过的坑：只认裸 lambda 的话，`f(cond ? (Action a) => ... : null)`
 * 这种三元表达式就没人管，两个重载同分 → 谁在成员表里靠前谁赢。
 * IDE 里 `LspSession.StartAsync(app != null ? (Action a) => ... : null)`
 * 就这样绑到了 App 那个重载上，无限自我递归，一开工程就 exit(1)。 */
zan_ast_node_t *zan_ast_lambda_source(zan_ast_node_t *arg);

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
