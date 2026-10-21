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
    TYPE_DECIMAL,
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
    TYPE_TASK,
    TYPE_TYPE_PARAM,
    TYPE_DELEGATE,
    TYPE_ERROR,
} zan_type_kind_t;

typedef struct zan_type zan_type_t;
struct zan_type {
    zan_type_kind_t kind;
    zan_istr_t name;
    struct zan_symbol *sym;
    zan_type_t *element_type;
    int array_rank;
    zan_type_t *base_type;
    int bases_resolved;
    zan_type_t **interfaces;
    int interface_count;
    zan_type_t **type_args;
    int type_arg_count;
    zan_type_t *delegate_ret_type;
    zan_type_t **delegate_param_types;
    int delegate_param_count;
    int delegate_is_async;
};

typedef struct zan_symbol zan_symbol_t;
struct zan_symbol {
    zan_sym_kind_t kind;
    zan_istr_t name;
    zan_type_t *type;
    zan_ast_node_t *decl;
    uint32_t modifiers;
    zan_symbol_t *parent;

    zan_symbol_t **members;
    int member_count;
    int member_cap;

    uint32_t name_hash;
    zan_symbol_t *hash_next;
};

typedef struct zan_scope zan_scope_t;
struct zan_scope {
    zan_scope_t *parent;
    zan_symbol_t **symbols;
    int sym_count;
    int sym_cap;
    zan_symbol_t **buckets;
    int bucket_count;
};

/* 扩展方法登记项：首参带 `this` 的方法。
 *
 * C# 风格的扩展方法在接收者类型上**查不到成员**，但 `recv.Foo()` 合法。
 * checker 判定"调用目标不存在"时必须把它们算进去，否则会把
 * `items.Remove(x)` / `s.PadLeft(10)` 误报成"没有这个方法"。
 *
 * 登记口径与 irgen 的 find_extension_method 逐字对齐：只看首参是不是
 * `this` 标记，**不额外要求 static**——两边口径不同就会一边能发、
 * 一边判成"没有这个方法"。 */
typedef struct zan_ext_method {
    zan_symbol_t *method;    /* 方法符号 */
    zan_type_t *recv_type;   /* 首参（this）的类型——绑定时解析，
                               * 那时类型参还在作用域里 */
} zan_ext_method_t;

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
    zan_type_t *type_decimal;
    zan_type_t *type_char;
    zan_type_t *type_string;
    zan_type_t *type_object;
    zan_type_t *type_nint;
    zan_type_t *type_error;
    zan_type_t *type_typeinfo;

    bool binding_done;

    /* 扩展方法登记表（static + 首参 this） */
    zan_ext_method_t *ext_methods;
    int ext_method_count;
    int ext_method_cap;

    zan_type_t **tuple_types;
    int tuple_type_count;
    int tuple_type_cap;
    zan_type_t **tuple_hash;
    int tuple_hash_cap;
    struct zan_member_idx_slot {
        zan_symbol_t *type;
        struct zan_member_name_index *idx;
    } *member_idx;
    int member_idx_cap;
    int member_idx_count;
};

void zan_binder_init(zan_binder_t *b, zan_arena_t *arena, zan_diag_t *diag);
void zan_binder_bind(zan_binder_t *b, zan_ast_node_t *unit);

zan_type_t *zan_binder_resolve_type(zan_binder_t *b, zan_ast_node_t *type_ref);

zan_type_t *zan_binder_make_list_type(zan_binder_t *b, zan_type_t *elem);
zan_type_t *zan_binder_make_span_type(zan_binder_t *b, zan_type_t *elem);
zan_type_t *zan_binder_make_readonly_span_type(zan_binder_t *b, zan_type_t *elem);
zan_type_t *zan_binder_make_array_type(zan_binder_t *b, zan_type_t *elem);
zan_type_t *zan_binder_make_nullable_type(zan_binder_t *b, zan_type_t *elem);
zan_type_t *zan_binder_make_grouping_type(zan_binder_t *b, zan_type_t *elem);
zan_type_t *zan_binder_make_task_type(zan_binder_t *b, zan_type_t *elem);

/* C# tuples `(T1, T2, */
zan_type_t *zan_binder_make_tuple_type(zan_binder_t *b, zan_type_t **elems,
                                       int count);

zan_type_t *zan_binder_subst_named(zan_binder_t *b, zan_type_t *t,
                                   zan_ast_list_t *tps, zan_type_t **args);
zan_symbol_t *zan_binder_lookup(zan_binder_t *b, zan_istr_t name);

/* 类（含其基类链）是否实现了接口 iface。checker 与 irgen 共用这一份判定，
 * 免得两边对"接口方法算不算这个类型的成员"口径分叉。 */
bool zan_binder_class_implements_iface(zan_symbol_t *cls, zan_symbol_t *iface);

/* 名为 name 的扩展方法能否作用于 recv_ty。判定口径与 irgen 的
 * find_extension_method 一致（src/compiler/irgen_expr_core.c）。 */
bool zan_binder_has_extension_method(zan_binder_t *b, zan_type_t *recv_ty,
                                     zan_istr_t name);

/* 面向用户的类型名：按类型的"形状"渲染 —— `T?` / `T[]` / `List<T>`。
 *
 * 为什么不能直接拿 t->name：可空类型与数组类型的 name 就是元素类型的
 * 名字（`bool?` 的 name 是 "bool"），诊断里会把 `bool?` 说成 `bool`
 * （A463）。而 name 字段是类型身份的一部分（列表特判、ARC 散列、
 * mangle 都靠它），不能为了好看去改写 —— 渲染与身份彻底分开。
 *
 * 形状信息本来就全在类型里（kind / element_type / array_rank /
 * type_args），不需要新增字段，也不会与类型同一性分叉。
 *
 * 返回写入的字节数（不含结尾 '\0'）；buf 装不下时截断并保证结尾 '\0'。 */
size_t zan_type_display(const zan_type_t *t, char *buf, size_t cap);

/* null 字面量实参 → 形参类型的匹配分。<0 表示不兼容（null 绑不上去）。
 * checker 与 irgen 两侧的重载分解共用这一条规则。 */
int zan_null_arg_score(zan_type_t *pt);

#endif /* ZAN_BINDER_H */
