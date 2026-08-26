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

    /* tuple: (e1, e2, ...) literal and its type (T1, T2) */
    AST_TUPLE_EXPR,
    AST_TUPLE_TYPE,

    /* deconstruction statement: var (a, b) = rhs; */
    AST_TUPLE_DECON,

    /* type references */
    AST_TYPE_REF,
    AST_ARRAY_TYPE,
    AST_NULLABLE_TYPE,
    AST_GENERIC_TYPE,
    AST_QUALIFIED_NAME,

    /* `ref x` / `out x` / `out T x` call argument */
    AST_REF_ARG,

    /* named call argument: `F(b: 2)` */
    AST_NAMED_ARG,

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
    AST_QUERY_EXPR,   /* from x in src where c ... select e */
    AST_QUERY_WHERE,  /* query clause: where c */
    AST_QUERY_LET,    /* query clause: let v = e */
    AST_QUERY_ORDERBY,/* query clause: orderby k [ascending|descending] */
    AST_QUERY_JOIN,   /* query clause: join y in s on k1 equals k2 [into g] */
    AST_SWITCH_EXPR,  /* `expr switch { arm, ... }` (expression form) */
    AST_SWITCH_ARM,   /* one arm of a switch expression */

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

    /* Integer literal suffix (AST_INT_LITERAL only): 0=none, 1=L/l (long),
     * 2=U/u (uint), 3=UL/LU in either case (ulong). */
    int lit_suffix;

    /* [Attr(...)] usages attached to a declaration; empty list if none. */
    zan_ast_list_t attributes;

    /* Namespace context, stamped per top-level declaration before binding
     * so namespace-aware resolution can tell apart same-named types in
     * different namespaces (see nsresolve.c).  Empty/NULL for global. */
    zan_istr_t ns_name;         /* enclosing namespace, dotted ("A.B") */
    zan_istr_t orig_name;       /* pre-mangling simple name, if renamed */
    zan_ast_list_t *ns_usings;  /* the file's `using` decls; NULL if none */

    /* On an AST_IDENTIFIER naming a generic type in expression position
     * (`Box<int>.Create(x)`): the `Box<int>` type reference. The identifier
     * keeps the simple name, so every consumer that only reads the name is
     * unaffected, while a static call can route to the instantiation's
     * specialization. NULL on every other node. */
    zan_ast_node_t *inst_type_ref;

    /* Memo for zan_binder_resolve_type on AST_TYPE_REF nodes: the type it
     * resolved to and the scope that resolution ran in (a type reference can
     * mean different things in different scopes, e.g. a type parameter). */
    void *rt_type;
    void *rt_scope;

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
            /* AST_ASSIGNMENT only: base operator of a desugared `lhs op= rhs`
             * (TK_EOF for a plain assignment). The parser reuses the target
             * subtree as the value-side operand; irgen keys single-evaluation
             * rewriting on this. */
            zan_token_kind_t compound_base;
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
        /* index: obj[i] or obj[i,j] (multi-dimensional) */
        struct {
            zan_ast_node_t *object;
            zan_ast_node_t *index;   /* first index */
            zan_ast_list_t extra;    /* remaining indices for rank > 1 */
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
            bool array_init;     /* new Type[] { a, b } -- args are elements */
            int array_rank;      /* number of dimension sizes at args start
                                  * (0 = no dims: plain object or unsized init) */
            bool list_copy;      /* new List<T>(src): copy-construct from
                                  * another List (checker-flagged; without it
                                  * args were silently initializer items) */
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
            zan_istr_t var_name; /* pattern variable: `is T x` (empty if none) */
            bool is_not;         /* `is not T` / `is not null` */
        } type_test;

    /* var / let declaration */
    struct {
        zan_istr_t name;
        zan_ast_node_t *type;        /* NULL if var (inferred) */
        zan_ast_node_t *initializer; /* NULL if none */
        bool is_const;               /* const */
        bool is_let;                 /* let (immutable) */
    } var_decl;

    /* tuple expression: (e1, e2, ...) */
    struct {
        zan_ast_list_t items;
    } tuple_expr;

    /* tuple type: (T1, T2, ...) in type position */
    struct {
        zan_ast_list_t elems;
    } tuple_type;

    /* deconstruction: var (a, b) = rhs;  /  (int a, string b) = rhs;
     * names[i] is the variable name, types[i] its declared type (NULL for a
     * `var` element). Lowered to field reads of the initializer's ItemN. */
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
            zan_ast_node_t *pattern; /* constant expr, or NULL for default */
            zan_ast_node_t *type_pattern; /* `case T x:` type node (else NULL) */
            zan_ast_node_t *when_cond;    /* `case ... when guard:` guard (else NULL) */
            zan_ast_node_t *body;
            zan_istr_t var_name; /* pattern variable for `case T x:` */
        } switch_case;

        /* switch expression: `expr switch { arm, ... }` */
        struct {
            zan_ast_node_t *expr;
            zan_ast_list_t arms;
        } switch_expr;

        /* switch expression arm: `pattern => result` / `pattern when g => result` */
        struct {
            zan_ast_node_t *pattern;     /* constant expr, NULL for default/discard */
            zan_ast_node_t *type_pattern; /* `int i =>` type node (else NULL) */
            zan_ast_node_t *when_cond;    /* `... when g =>` guard (else NULL) */
            zan_ast_node_t *result;
            zan_istr_t var_name;          /* pattern variable for `int i =>` */
            bool is_default;              /* `_ =>` / `default =>` */
        } switch_arm;

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
            bool is_explicit_layout; /* [StructLayout(LayoutKind.Explicit)]:
                                      * every field carries [FieldOffset(n)] */
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
            zan_ast_list_t base_args;  /* constructor `: base(...)` argument exprs */
            bool has_base_init;        /* constructor declared a `: base(...)` initializer */
            bool has_this_init;
        } method_decl;

        /* field */
        struct {
            zan_istr_t name;
            zan_ast_node_t *type;
            zan_ast_node_t *initializer;
            uint32_t modifiers;
            /* Property accessor bodies (AST_PROPERTY_DECL only). NULL means an
             * automatic accessor (`{ get; }` / `{ set; }`) backed by the field
             * slot; a block/expression body means the read/write lowers to the
             * synthesized get_<name>/set_<name> method. Plain fields keep both
             * NULL. */
            zan_ast_node_t *getter_body;
            zan_ast_node_t *setter_body;
            /* Whether the corresponding accessor keyword was present at all
             * (`get`/`set` in the `{ ... }` list). `{ get; }` has has_setter
             * false and is read-only; `{ get; set; }` has both true even though
             * the automatic setter's body is NULL. `has_init` marks an `init`
             * accessor (`{ get; init; }`): a setter that is only writable while
             * the object is being initialized (object initializer / ctor). Its
             * body (if any) is stored in setter_body and lowers to set_<name>,
             * same as `set`. */
            bool has_getter;
            bool has_setter;
            bool has_init;
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

        /* from var in source [clauses]* [group e by k [into g]]? [select p]
         * -- sub-clauses (where/let/orderby/join) sit in `clauses` in source
         * order; group is a single trailing clause (its `into` makes the
         * range variable that `select` sees a Grouping). */
        struct {
            zan_istr_t var;
            zan_ast_node_t *source;
            zan_ast_list_t clauses; /* AST_QUERY_WHERE/LET/ORDERBY/JOIN, in
                                     * source order */
            zan_ast_node_t *group_expr; /* `group <expr> by <key>` element */
            zan_ast_node_t *group_key;  /* group key expression */
            zan_istr_t group_into;      /* `into <name>` var (empty = none) */
            zan_ast_node_t *select;
        } query;

        /* sub-clause payloads (AST_QUERY_WHERE, AST_QUERY_LET,
         * AST_QUERY_ORDERBY, AST_QUERY_JOIN) */
        struct {
            zan_istr_t name;        /* let v / join y / into g var name */
            zan_ast_node_t *expr;   /* where cond, let value or orderby key */
            int descending;         /* orderby: 1 = descending */
            zan_ast_node_t *source;     /* join source collection */
            zan_ast_node_t *left_key;   /* join left key (outer range var) */
            zan_ast_node_t *right_key;  /* join right key (inner range var) */
            zan_istr_t into;            /* join ... into g (group join) */
        } query_clause;

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

        /* named call argument: `F(b: 2)` -- the arg name and its expression.
         * Stored in the call's args list; irgen reorders it to the parameter
         * position once the callee symbol is known. */
        struct {
            zan_istr_t name;
            zan_ast_node_t *expr;
        } named_arg;

        /* type reference */
        struct {
            zan_istr_t name;
            zan_ast_list_t type_args;
            bool is_nullable;
            bool is_array;
            /* Array shape beyond the boolean: `array_rank` is 1 for `[]`,
             * 2 for `[,]`, 3 for `[,,]` ... A *jagged* declaration wraps the
             * inner declaration: `int[][]` parses as an outer node whose
             * array_element points at the `int[]` node. C# folds the
             * leftmost rank specifier as the OUTERMOST array, so `int[][,]`
             * is a 1D array of `int[,]`. NULL array_element means the node
             * itself is the element (plain `int[]` / `int[,]`). */
            int array_rank;
            zan_ast_node_t *array_element;
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
            /* Parallel to the expr parts: the format specifier of each hole
             * (e.g. "D4" in {v:D4}) as an AST_STRING_LITERAL, or NULL when the
             * hole has none. Index i corresponds to the i-th expr part. */
            zan_ast_list_t formats;
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
#define MOD_REF       0x10000  /* `ref struct` / `ref` return-annotated member */

/* ---- utility functions ---- */

zan_ast_node_t *zan_ast_new(zan_arena_t *arena, zan_ast_kind_t kind, zan_loc_t loc);
bool zan_ast_has_attr(const zan_ast_node_t *decl, const char *name);
void zan_ast_list_init(zan_ast_list_t *list);
void zan_ast_list_push(zan_ast_list_t *list, zan_ast_node_t *node, zan_arena_t *arena);

#endif /* ZAN_AST_H */
