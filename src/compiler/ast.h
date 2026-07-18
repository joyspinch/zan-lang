/* ast.h -- Abstract Syntax Tree node types for Zan. */

#ifndef ZAN_AST_H
#define ZAN_AST_H

#include "zan.h"
#include "token.h"

/* ---- AST node kinds ---- */

typedef enum {
    /* top-level */
    AST_COMPILATION_UNIT,
    AST_USING_DECL,
    AST_NAMESPACE_DECL,

    /* type declarations */
    AST_CLASS_DECL,
    AST_STRUCT_DECL,
    AST_INTERFACE_DECL,
    AST_ENUM_DECL,
    AST_DELEGATE_DECL,

    /* members */
    AST_FIELD_DECL,
    AST_METHOD_DECL,
    AST_CONSTRUCTOR_DECL,
    AST_DESTRUCTOR_DECL,
    AST_PROPERTY_DECL,
    AST_PARAM,

    /* statements */
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

    /* expressions */
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

    /* type references */
    AST_TYPE_REF,
    AST_ARRAY_TYPE,
    AST_NULLABLE_TYPE,
    AST_GENERIC_TYPE,
    AST_QUALIFIED_NAME,

    /* `ref x` / `out x` / `out T x` call argument */
    AST_REF_ARG,

    /* misc */
    AST_ATTRIBUTE,
    AST_ENUM_MEMBER,
    AST_CATCH_CLAUSE,
    AST_SWITCH_CASE,
    AST_WHERE_CLAUSE, /* generic constraint: where T : C1, C2 */
    AST_YIELD_STMT,   /* yield return expr; / yield break; (desugared in parser) */
    AST_LOCK_STMT,    /* lock (expr) body */
    AST_GOTO_STMT,    /* goto label; */
    AST_LABEL_STMT,   /* label: */

    AST__COUNT,
} zan_ast_kind_t;

/* ---- AST node ---- */

/* dynamic child list */
typedef struct {
    zan_ast_node_t **items;
    int count;
    int capacity;
} zan_ast_list_t;

struct zan_ast_node {
    zan_ast_kind_t kind;
    zan_loc_t loc;

    union {
        /* literals */
        int64_t int_val;
        double float_val;
        bool bool_val;
        zan_istr_t str_val;

        /* identifier / name */
        struct {
            zan_istr_t name;
        } ident;

        /* binary / assignment */
        struct {
            zan_token_kind_t op;
            zan_ast_node_t *left;
            zan_ast_node_t *right;
        } binary;

        /* unary (prefix and postfix) */
        struct {
            zan_token_kind_t op;
            zan_ast_node_t *operand;
        } unary;

        /* call expression */
        struct {
            zan_ast_node_t *callee;
            zan_ast_list_t args;
            zan_ast_list_t type_args; /* explicit generic args: f<int>(...) */
        } call;

        /* member access: expr.name (null_cond: `expr?.name`) */
        struct {
            zan_ast_node_t *object;
            zan_istr_t name;
            int null_cond;
        } member;

        /* index: expr[idx] */
        struct {
            zan_ast_node_t *object;
            zan_ast_node_t *index;
        } index;

        /* conditional: cond ? then : else */
        struct {
            zan_ast_node_t *cond;
            zan_ast_node_t *then_expr;
            zan_ast_node_t *else_expr;
        } conditional;

        /* new expression */
        struct {
            zan_ast_node_t *type;
            zan_ast_list_t args;
            bool is_array;       /* new Type[size] */
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
        } type_test;

        /* var / let declaration */
        struct {
            zan_istr_t name;
            zan_ast_node_t *type;        /* NULL if var (inferred) */
            zan_ast_node_t *initializer; /* NULL if none */
            bool is_const;               /* const */
            bool is_let;                 /* let (immutable) */
        } var_decl;

        /* block: { statements } */
        struct {
            zan_ast_list_t stmts;
        } block;

        /* return */
        struct {
            zan_ast_node_t *value; /* NULL for bare return */
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

        /* catch clause */
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

        /* switch case */
        struct {
            zan_ast_node_t *pattern; /* NULL for default */
            zan_ast_node_t *body;
        } switch_case;

        /* expression statement */
        struct {
            zan_ast_node_t *expr;
        } expr_stmt;

        /* using declaration */
        struct {
            zan_ast_node_t *name;     /* qualified name */
            bool is_static;
        } using_decl;

        /* namespace */
        struct {
            zan_ast_node_t *name;     /* qualified name */
            zan_ast_list_t members;   /* type declarations */
            bool is_file_scoped;
        } namespace_decl;

        /* compilation unit */
        struct {
            zan_ast_list_t usings;
            zan_ast_node_t *ns;       /* namespace */
            zan_ast_list_t decls;     /* type declarations */
        } comp_unit;

        /* class / struct / interface */
        struct {
            zan_istr_t name;
            zan_ast_list_t type_params;
            zan_ast_list_t bases;      /* base types */
            zan_ast_list_t members;
            uint32_t modifiers;
            bool is_c_layout;  /* [StructLayout(LayoutKind.Sequential)] for C ABI */
            zan_ast_list_t where_clauses; /* AST_WHERE_CLAUSE generic constraints */
        } type_decl;

        /* method / constructor */
        struct {
            zan_istr_t name;
            zan_ast_node_t *return_type;
            zan_ast_list_t params;
            zan_ast_list_t type_params;
            zan_ast_node_t *body;
            uint32_t modifiers;
            zan_istr_t extern_lib;   /* DllImport library name, {NULL,0} if none */
            zan_istr_t entry_point;  /* DllImport entry point override, {NULL,0} if none */
            zan_ast_list_t where_clauses; /* AST_WHERE_CLAUSE generic constraints */
        } method_decl;

        /* field */
        struct {
            zan_istr_t name;
            zan_ast_node_t *type;
            zan_ast_node_t *initializer;
            uint32_t modifiers;
        } field_decl;

        /* generic constraint clause: where T : C1, C2 */
        struct {
            zan_istr_t param_name;
            zan_ast_list_t constraints; /* AST_TYPE_REF list */
        } where_clause;

        /* yield return expr; (value set) or yield break; (value NULL) */
        struct {
            zan_ast_node_t *value;
        } yield_stmt;

        /* lock (expr) body */
        struct {
            zan_ast_node_t *expr;
            zan_ast_node_t *body;
        } lock_stmt;

        /* parameter */
        struct {
            zan_istr_t name;
            zan_ast_node_t *type;
            zan_ast_node_t *default_val;
            int is_params; /* trailing `params T[]` variadic parameter */
            int by_ref;    /* 0 = by value, 1 = `ref`, 2 = `out` */
            int is_this;   /* leading `this T recv` extension-method receiver */
        } param;

        /* by-reference call argument: `ref x`, `out x`, `out T x` */
        struct {
            zan_ast_node_t *expr;      /* the referenced lvalue (identifier) */
            zan_ast_node_t *decl_type; /* non-NULL for inline `out T x` decl */
            int is_out;
        } ref_arg;

        /* type reference */
        struct {
            zan_istr_t name;
            zan_ast_list_t type_args;
            bool is_nullable;
            bool is_array;
        } type_ref;

        /* qualified name: a.b.c */
        struct {
            zan_ast_list_t parts; /* list of AST_IDENTIFIER nodes */
        } qualified_name;

        /* attribute */
        struct {
            zan_ast_node_t *name;
            zan_ast_list_t args;
        } attribute;

        /* enum member */
        struct {
            zan_istr_t name;
            zan_ast_node_t *value; /* NULL for auto */
        } enum_member;

        /* await expression */
        struct {
            zan_ast_node_t *expr;
        } await_expr;

        /* lambda: (params) => body */
        struct {
            zan_ast_list_t params;
            zan_ast_node_t *body; /* block or expr */
        } lambda;

        /* string interpolation: $"text {expr} text" */
        struct {
            zan_ast_list_t parts; /* alternating STRING_LITERAL and expr nodes */
        } string_interp;
    };
};

/* ---- modifier flags ---- */

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

/* ---- utility functions ---- */

zan_ast_node_t *zan_ast_new(zan_arena_t *arena, zan_ast_kind_t kind, zan_loc_t loc);
void zan_ast_list_init(zan_ast_list_t *list);
void zan_ast_list_push(zan_ast_list_t *list, zan_ast_node_t *node, zan_arena_t *arena);

#endif /* ZAN_AST_H */
