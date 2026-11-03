
#include "ast.h"
#include "arena.h"
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(zan_ast_node_t) <= 120, "AST node layout regressed");

static size_t g_ast_node_count;

size_t zan_ast_node_count(void) { return g_ast_node_count; }

zan_method_ext_t *zan_ast_ensure_method_ext(zan_ast_node_t *n, zan_arena_t *arena) {
    if (!n) return NULL;
    if (!n->method_decl.ext) {
        n->method_decl.ext = (zan_method_ext_t *)zan_arena_alloc(arena, sizeof(zan_method_ext_t));
        memset(n->method_decl.ext, 0, sizeof(zan_method_ext_t));
    }
    return n->method_decl.ext;
}

zan_decl_meta_t *zan_ast_ensure_decl_meta(zan_ast_node_t *n, zan_arena_t *arena) {
    if (!n) return NULL;
    if (!n->meta) {
        n->meta = (zan_decl_meta_t *)zan_arena_alloc(arena, sizeof(zan_decl_meta_t));
        memset(n->meta, 0, sizeof(zan_decl_meta_t));
    }
    return n->meta;
}

zan_ast_node_t *zan_ast_new(zan_arena_t *arena, zan_ast_kind_t kind, zan_loc_t loc) {
    zan_ast_node_t *node = (zan_ast_node_t *)zan_arena_alloc(arena, sizeof(zan_ast_node_t));
    g_ast_node_count++;
    node->kind = kind;
    node->loc = loc;
    node->lit_suffix = 0;
    node->meta = NULL;
    if (kind == AST_ASSIGNMENT || kind == AST_BINARY) {
        node->binary.compound_base = TK_EOF;
    }
    if (kind == AST_METHOD_DECL || kind == AST_CONSTRUCTOR_DECL) {
        node->method_decl.is_task_return = false;
        node->method_decl.ext = NULL;
    } else if (kind == AST_CLASS_DECL || kind == AST_STRUCT_DECL ||
               kind == AST_INTERFACE_DECL || kind == AST_ENUM_DECL) {
        node->type_decl.where_clauses = NULL;
    }
    return node;
}

bool zan_ast_has_attr(const zan_ast_node_t *decl, const char *name) {
    if (!decl || !decl->meta) return false;
    size_t n = strlen(name);
    for (int i = 0; i < decl->meta->attributes.count; i++) {
        zan_ast_node_t *a = decl->meta->attributes.items[i];
        if (!a || a->kind != AST_ATTRIBUTE || !a->attribute.name) continue;
        if (a->attribute.name->kind != AST_IDENTIFIER) continue;
        zan_istr_t s = a->attribute.name->ident.name;
        if (s.len == (uint32_t)n && memcmp(s.str, name, n) == 0) return true;
    }
    return false;
}

void zan_ast_list_init(zan_ast_list_t *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void zan_ast_list_push(zan_ast_list_t *list, zan_ast_node_t *node, zan_arena_t *arena) {
    if (list->count >= list->capacity) {
        int new_cap = list->capacity < 4 ? 4 : list->capacity * 2;
        zan_ast_node_t **new_items = (zan_ast_node_t **)zan_arena_alloc(
            arena, sizeof(zan_ast_node_t *) * (size_t)new_cap);
        if (list->items && list->count > 0) {
            memcpy(new_items, list->items, sizeof(zan_ast_node_t *) * (size_t)list->count);
        }
        list->items = new_items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = node;
}

/* 表达式是不是「lambda 或 null」的形状。 */
static bool ast_expr_is_lambda_or_null(zan_ast_node_t *e) {
    if (!e) return true;
    switch (e->kind) {
    case AST_LAMBDA:
    case AST_NULL_LITERAL:
        return true;
    case AST_CAST_EXPR:
        return ast_expr_is_lambda_or_null(e->cast.expr);
    case AST_CONDITIONAL:
        return ast_expr_is_lambda_or_null(e->conditional.then_expr) &&
               ast_expr_is_lambda_or_null(e->conditional.else_expr);
    default:
        return false;
    }
}

zan_ast_node_t *zan_ast_lambda_source(zan_ast_node_t *arg) {
    if (!arg) return NULL;
    switch (arg->kind) {
    case AST_LAMBDA:
        return arg;
    case AST_CAST_EXPR: {
        zan_ast_node_t *inner = zan_ast_lambda_source(arg->cast.expr);
        return inner ? inner : NULL;
    }
    case AST_CONDITIONAL: {
        if (!ast_expr_is_lambda_or_null(arg)) return NULL;
        zan_ast_node_t *t = zan_ast_lambda_source(arg->conditional.then_expr);
        if (t) return t;
        return zan_ast_lambda_source(arg->conditional.else_expr);
    }
    default:
        return NULL;
    }
}

/* ===== B-ID102：AST 子节点拓扑单一事实源 =====
 * 每个 kind 的子节点扁平枚举：列表按元素长度展开，顺序与 union 分支内字段
 * 声明序一致；NULL 子槽合法（如无 else 的 if），计入 count、child_at 返回
 * NULL。type_decl.where_clauses 与 method ext 的 where/base_args 挂在堆上，
 * 非空才计入。AST_ARRAY_TYPE/AST_NULLABLE_TYPE/AST_GENERIC_TYPE 为死 kind
 * （parser 从不创建，type_ref 的 is_array/is_nullable 旗标承担语义），记 0。
 * 消费方：nr_walk/collect_shadows 的 default 覆盖审计（ZANC_TRACE 门控）；
 * 后续遍历器迁统一遍历亦以本表为准。新加 AST kind 必须同步本表——漏登的
 * kind 会在 default 审计里现形（B-ID101 即此型漏改）。 */

int zan_ast_child_count(const zan_ast_node_t *n) {
    if (!n) return 0;
    switch (n->kind) {
    case AST_COMPILATION_UNIT:
        return n->comp_unit.usings.count + 1 + n->comp_unit.decls.count;
    case AST_USING_DECL:
        return 1;
    case AST_NAMESPACE_DECL:
        return 1 + n->namespace_decl.members.count;
    case AST_CLASS_DECL:
    case AST_STRUCT_DECL:
    case AST_INTERFACE_DECL:
    case AST_ENUM_DECL: {
        int c = n->type_decl.type_params.count + n->type_decl.bases.count +
                n->type_decl.members.count; /* nested_host 为父指针（反向边），不计 */
        if (n->type_decl.where_clauses) c += n->type_decl.where_clauses->count;
        return c;
    }
    case AST_DELEGATE_DECL:
        return 1 + n->method_decl.params.count + n->method_decl.type_params.count;
    case AST_METHOD_DECL:
    case AST_CONSTRUCTOR_DECL:
    case AST_DESTRUCTOR_DECL: {
        int c = 1 + n->method_decl.params.count + n->method_decl.type_params.count + 1;
        if (n->method_decl.ext) {
            c += n->method_decl.ext->where_clauses.count;
            c += n->method_decl.ext->base_args.count;
        }
        return c;
    }
    case AST_FIELD_DECL:
    case AST_PROPERTY_DECL:
        return 4; /* type/initializer/getter_body/setter_body */
    case AST_PARAM:
        return 2;
    case AST_BLOCK:
        return n->block.stmts.count;
    case AST_VAR_DECL:
        return 2;
    case AST_EXPR_STMT:
    case AST_NAMED_ARG:
    case AST_AWAIT_EXPR:
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
    case AST_MEMBER_ACCESS:
    case AST_TYPEOF_EXPR:
    case AST_SIZEOF_EXPR:
        return 1;
    case AST_RETURN_STMT:
    case AST_THROW_STMT:
    case AST_YIELD_STMT:
    case AST_ENUM_MEMBER:
        return 1;
    case AST_IF_STMT:
        return 3;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
    case AST_LOCK_STMT:
    case AST_CHECKED_STMT:
        return 2;
    case AST_FOR_STMT:
        return 4;
    case AST_FOREACH_STMT:
        return 3;
    case AST_TRY_STMT:
        return 1 + n->try_stmt.catches.count + 1;
    case AST_SWITCH_STMT:
        return 1 + n->switch_stmt.cases.count;
    case AST_INT_LITERAL:
    case AST_FLOAT_LITERAL:
    case AST_DECIMAL_LITERAL:
    case AST_STRING_LITERAL:
    case AST_CHAR_LITERAL:
    case AST_BOOL_LITERAL:
    case AST_NULL_LITERAL:
    case AST_BREAK_STMT:
    case AST_CONTINUE_STMT:
    case AST_THIS_EXPR:
    case AST_BASE_EXPR:
    case AST_GOTO_STMT:
    case AST_LABEL_STMT:
    case AST_ERROR_EXPR:
        return 0;
    case AST_IDENTIFIER:
        return 1; /* inst_type_ref，常 NULL */
    case AST_BINARY:
    case AST_ASSIGNMENT:
        return 2;
    case AST_CALL:
        return 1 + n->call.args.count + n->call.type_args.count;
    case AST_INDEX:
        return 2 + n->index.extra.count;
    case AST_NEW_EXPR:
        return 2 + n->new_expr.args.count + n->new_expr.arg_inits.count;
    case AST_CAST_EXPR:
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        return 2;
    case AST_CONDITIONAL:
        return 3;
    case AST_LAMBDA:
        return n->lambda.params.count + 1;
    case AST_STRING_INTERP:
        return n->string_interp.parts.count + n->string_interp.formats.count;
    case AST_TUPLE_EXPR:
        return n->tuple_expr.items.count;
    case AST_TUPLE_TYPE:
        return n->tuple_type.elems.count;
    case AST_TUPLE_DECON:
        return n->tuple_decon.names.count + n->tuple_decon.types.count + 1;
    case AST_TYPE_REF:
        return n->type_ref.type_args.count + 1;
    case AST_ARRAY_TYPE:
    case AST_NULLABLE_TYPE:
    case AST_GENERIC_TYPE:
        return 0; /* 死 kind */
    case AST_QUALIFIED_NAME:
        return n->qualified_name.parts.count;
    case AST_REF_ARG:
        return 2;
    case AST_COLL_INIT:
        return n->coll_init.items.count;
    case AST_ATTRIBUTE:
        return 1 + n->attribute.args.count;
    case AST_CATCH_CLAUSE:
        return 2;
    case AST_SWITCH_CASE:
        return 4;
    case AST_WHERE_CLAUSE:
        return n->where_clause.constraints.count;
    case AST_QUERY_EXPR:
        return 1 + n->query.clauses.count + 3;
    case AST_QUERY_WHERE:
    case AST_QUERY_LET:
    case AST_QUERY_ORDERBY:
    case AST_QUERY_JOIN:
        return 4; /* expr/source/left_key/right_key，按 kind 取子集，其余 NULL */
    case AST_SWITCH_EXPR:
        return 1 + n->switch_expr.arms.count;
    case AST_SWITCH_ARM:
        return 4;
    case AST_WITH_EXPR:
        return 1 + n->with_expr.assigns.count;
    default:
        return 0;
    }
}

zan_ast_node_t **zan_ast_child_ptr_at(zan_ast_node_t *n, int i) {
    if (!n || i < 0) return NULL;
    switch (n->kind) {
    case AST_COMPILATION_UNIT: {
        int b = 0;
        if (i < b + n->comp_unit.usings.count) return &n->comp_unit.usings.items[i - b];
        b += n->comp_unit.usings.count;
        if (i == b) return &n->comp_unit.ns;
        b += 1;
        if (i < b + n->comp_unit.decls.count) return &n->comp_unit.decls.items[i - b];
        return NULL;
    }
    case AST_USING_DECL:
        return i == 0 ? &n->using_decl.name : NULL;
    case AST_NAMESPACE_DECL: {
        if (i == 0) return &n->namespace_decl.name;
        int b = 1;
        if (i < b + n->namespace_decl.members.count) return &n->namespace_decl.members.items[i - b];
        return NULL;
    }
    case AST_CLASS_DECL:
    case AST_STRUCT_DECL:
    case AST_INTERFACE_DECL:
    case AST_ENUM_DECL: {
        int b = 0;
        if (i < b + n->type_decl.type_params.count) return &n->type_decl.type_params.items[i - b];
        b += n->type_decl.type_params.count;
        if (i < b + n->type_decl.bases.count) return &n->type_decl.bases.items[i - b];
        b += n->type_decl.bases.count;
        if (i < b + n->type_decl.members.count) return &n->type_decl.members.items[i - b];
        b += n->type_decl.members.count;
        if (n->type_decl.where_clauses && i < b + n->type_decl.where_clauses->count)
            return &n->type_decl.where_clauses->items[i - b];
        return NULL;
    }
    case AST_DELEGATE_DECL: {
        if (i == 0) return &n->method_decl.return_type;
        int b = 1;
        if (i < b + n->method_decl.params.count) return &n->method_decl.params.items[i - b];
        b += n->method_decl.params.count;
        if (i < b + n->method_decl.type_params.count) return &n->method_decl.type_params.items[i - b];
        return NULL;
    }
    case AST_METHOD_DECL:
    case AST_CONSTRUCTOR_DECL:
    case AST_DESTRUCTOR_DECL: {
        if (i == 0) return &n->method_decl.return_type;
        int b = 1;
        if (i < b + n->method_decl.params.count) return &n->method_decl.params.items[i - b];
        b += n->method_decl.params.count;
        if (i < b + n->method_decl.type_params.count) return &n->method_decl.type_params.items[i - b];
        b += n->method_decl.type_params.count;
        if (i == b) return &n->method_decl.body;
        b += 1;
        if (n->method_decl.ext) {
            if (i < b + n->method_decl.ext->where_clauses.count)
                return &n->method_decl.ext->where_clauses.items[i - b];
            b += n->method_decl.ext->where_clauses.count;
            if (i < b + n->method_decl.ext->base_args.count)
                return &n->method_decl.ext->base_args.items[i - b];
        }
        return NULL;
    }
    case AST_FIELD_DECL:
    case AST_PROPERTY_DECL:
        switch (i) {
        case 0: return &n->field_decl.type;
        case 1: return &n->field_decl.initializer;
        case 2: return &n->field_decl.getter_body;
        case 3: return &n->field_decl.setter_body;
        }
        return NULL;
    case AST_PARAM:
        return i == 0 ? &n->param.type : (i == 1 ? &n->param.default_val : NULL);
    case AST_BLOCK:
        return i < n->block.stmts.count ? &n->block.stmts.items[i] : NULL;
    case AST_VAR_DECL:
        return i == 0 ? &n->var_decl.type : (i == 1 ? &n->var_decl.initializer : NULL);
    case AST_EXPR_STMT:
        return i == 0 ? &n->expr_stmt.expr : NULL;
    case AST_RETURN_STMT:
        return i == 0 ? &n->ret.value : NULL;
    case AST_THROW_STMT:
        return i == 0 ? &n->throw_stmt.value : NULL;
    case AST_YIELD_STMT:
        return i == 0 ? &n->yield_stmt.value : NULL;
    case AST_ENUM_MEMBER:
        return i == 0 ? &n->enum_member.value : NULL;
    case AST_NAMED_ARG:
        return i == 0 ? &n->named_arg.expr : NULL;
    case AST_AWAIT_EXPR:
        return i == 0 ? &n->await_expr.expr : NULL;
    case AST_IF_STMT:
        switch (i) {
        case 0: return &n->if_stmt.cond;
        case 1: return &n->if_stmt.then_body;
        case 2: return &n->if_stmt.else_body;
        }
        return NULL;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        return i == 0 ? &n->while_stmt.cond : (i == 1 ? &n->while_stmt.body : NULL);
    case AST_LOCK_STMT:
        return i == 0 ? &n->lock_stmt.expr : (i == 1 ? &n->lock_stmt.body : NULL);
    case AST_CHECKED_STMT:
        return i == 0 ? &n->checked_stmt.body : NULL;
    case AST_FOR_STMT:
        switch (i) {
        case 0: return &n->for_stmt.init;
        case 1: return &n->for_stmt.cond;
        case 2: return &n->for_stmt.step;
        case 3: return &n->for_stmt.body;
        }
        return NULL;
    case AST_FOREACH_STMT:
        switch (i) {
        case 0: return &n->foreach_stmt.var_type;
        case 1: return &n->foreach_stmt.collection;
        case 2: return &n->foreach_stmt.body;
        }
        return NULL;
    case AST_TRY_STMT: {
        if (i == 0) return &n->try_stmt.try_body;
        int b = 1;
        if (i < b + n->try_stmt.catches.count) return &n->try_stmt.catches.items[i - b];
        b += n->try_stmt.catches.count;
        if (i == b) return &n->try_stmt.finally_body;
        return NULL;
    }
    case AST_SWITCH_STMT: {
        if (i == 0) return &n->switch_stmt.expr;
        int b = 1;
        if (i < b + n->switch_stmt.cases.count) return &n->switch_stmt.cases.items[i - b];
        return NULL;
    }
    case AST_IDENTIFIER:
        return i == 0 ? &n->ident.inst_type_ref : NULL;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        return i == 0 ? &n->binary.left : (i == 1 ? &n->binary.right : NULL);
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        return i == 0 ? &n->unary.operand : NULL;
    case AST_CALL: {
        if (i == 0) return &n->call.callee;
        int b = 1;
        if (i < b + n->call.args.count) return &n->call.args.items[i - b];
        b += n->call.args.count;
        if (i < b + n->call.type_args.count) return &n->call.type_args.items[i - b];
        return NULL;
    }
    case AST_MEMBER_ACCESS:
        return i == 0 ? &n->member.object : NULL;
    case AST_INDEX: {
        if (i == 0) return &n->index.object;
        if (i == 1) return &n->index.index;
        int b = 2;
        if (i < b + n->index.extra.count) return &n->index.extra.items[i - b];
        return NULL;
    }
    case AST_NEW_EXPR: {
        if (i == 0) return &n->new_expr.type;
        if (i == 1) return &n->new_expr.call_init;
        int b = 2;
        if (i < b + n->new_expr.args.count) return &n->new_expr.args.items[i - b];
        b += n->new_expr.args.count;
        if (i < b + n->new_expr.arg_inits.count) return &n->new_expr.arg_inits.items[i - b];
        return NULL;
    }
    case AST_CAST_EXPR:
        return i == 0 ? &n->cast.type : (i == 1 ? &n->cast.expr : NULL);
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        return i == 0 ? &n->type_test.expr : (i == 1 ? &n->type_test.type : NULL);
    case AST_TYPEOF_EXPR:
    case AST_SIZEOF_EXPR:
        return i == 0 ? &n->cast.type : NULL;
    case AST_CONDITIONAL:
        switch (i) {
        case 0: return &n->conditional.cond;
        case 1: return &n->conditional.then_expr;
        case 2: return &n->conditional.else_expr;
        }
        return NULL;
    case AST_LAMBDA: {
        if (i < n->lambda.params.count) return &n->lambda.params.items[i];
        if (i == n->lambda.params.count) return &n->lambda.body;
        return NULL;
    }
    case AST_STRING_INTERP: {
        if (i < n->string_interp.parts.count) return &n->string_interp.parts.items[i];
        int b = n->string_interp.parts.count;
        if (i < b + n->string_interp.formats.count) return &n->string_interp.formats.items[i - b];
        return NULL;
    }
    case AST_TUPLE_EXPR:
        return i < n->tuple_expr.items.count ? &n->tuple_expr.items.items[i] : NULL;
    case AST_TUPLE_TYPE:
        return i < n->tuple_type.elems.count ? &n->tuple_type.elems.items[i] : NULL;
    case AST_TUPLE_DECON: {
        if (i < n->tuple_decon.names.count) return &n->tuple_decon.names.items[i];
        int b = n->tuple_decon.names.count;
        if (i < b + n->tuple_decon.types.count) return &n->tuple_decon.types.items[i - b];
        if (i == b + n->tuple_decon.types.count) return &n->tuple_decon.initializer;
        return NULL;
    }
    case AST_TYPE_REF: {
        if (i < n->type_ref.type_args.count) return &n->type_ref.type_args.items[i];
        if (i == n->type_ref.type_args.count) return &n->type_ref.array_element;
        return NULL;
    }
    case AST_QUALIFIED_NAME:
        return i < n->qualified_name.parts.count ? &n->qualified_name.parts.items[i] : NULL;
    case AST_REF_ARG:
        return i == 0 ? &n->ref_arg.decl_type : (i == 1 ? &n->ref_arg.expr : NULL);
    case AST_COLL_INIT:
        return i < n->coll_init.items.count ? &n->coll_init.items.items[i] : NULL;
    case AST_ATTRIBUTE: {
        if (i == 0) return &n->attribute.name;
        int b = 1;
        if (i < b + n->attribute.args.count) return &n->attribute.args.items[i - b];
        return NULL;
    }
    case AST_CATCH_CLAUSE:
        return i == 0 ? &n->catch_clause.type : (i == 1 ? &n->catch_clause.body : NULL);
    case AST_SWITCH_CASE:
        switch (i) {
        case 0: return &n->switch_case.pattern;
        case 1: return &n->switch_case.type_pattern;
        case 2: return &n->switch_case.when_cond;
        case 3: return &n->switch_case.body;
        }
        return NULL;
    case AST_WHERE_CLAUSE:
        return i < n->where_clause.constraints.count ? &n->where_clause.constraints.items[i] : NULL;
    case AST_QUERY_EXPR: {
        if (i == 0) return &n->query.source;
        int b = 1;
        if (i < b + n->query.clauses.count) return &n->query.clauses.items[i - b];
        b += n->query.clauses.count;
        if (i == b) return &n->query.group_expr;
        if (i == b + 1) return &n->query.group_key;
        if (i == b + 2) return &n->query.select;
        return NULL;
    }
    case AST_QUERY_WHERE:
    case AST_QUERY_LET:
    case AST_QUERY_ORDERBY:
    case AST_QUERY_JOIN:
        switch (i) {
        case 0: return &n->query_clause.expr;
        case 1: return &n->query_clause.source;
        case 2: return &n->query_clause.left_key;
        case 3: return &n->query_clause.right_key;
        }
        return NULL;
    case AST_SWITCH_EXPR: {
        if (i == 0) return &n->switch_expr.expr;
        int b = 1;
        if (i < b + n->switch_expr.arms.count) return &n->switch_expr.arms.items[i - b];
        return NULL;
    }
    case AST_SWITCH_ARM:
        switch (i) {
        case 0: return &n->switch_arm.pattern;
        case 1: return &n->switch_arm.type_pattern;
        case 2: return &n->switch_arm.when_cond;
        case 3: return &n->switch_arm.result;
        }
        return NULL;
    case AST_WITH_EXPR: {
        if (i == 0) return &n->with_expr.expr;
        int b = 1;
        if (i < b + n->with_expr.assigns.count) return &n->with_expr.assigns.items[i - b];
        return NULL;
    }
    default:
        return NULL;
    }
}

/* 经 child_count/child_at 递归数子树节点（含 root；NULL 槽跳过）。
 * 拓扑表自检与遍历器迁移的对照基线；共享子树按可达次数重复计数。 */
zan_ast_node_t *zan_ast_child_at(const zan_ast_node_t *n, int i) {
    zan_ast_node_t **p = zan_ast_child_ptr_at((zan_ast_node_t *)n, i);
    return p ? *p : NULL;
}

bool zan_ast_for_each_child(zan_ast_node_t *n, zan_ast_child_fn fn, void *user_data) {
    if (!n || !fn) return true;
    int c = zan_ast_child_count(n);
    for (int i = 0; i < c; i++) {
        zan_ast_node_t *ch = zan_ast_child_at(n, i);
        if (ch) {
            if (!fn(ch, user_data)) return false;
        }
    }
    return true;
}

bool zan_ast_for_each_child_const(const zan_ast_node_t *n, zan_ast_child_const_fn fn, void *user_data) {
    if (!n || !fn) return true;
    int c = zan_ast_child_count(n);
    for (int i = 0; i < c; i++) {
        const zan_ast_node_t *ch = zan_ast_child_at(n, i);
        if (ch) {
            if (!fn(ch, user_data)) return false;
        }
    }
    return true;
}

bool zan_ast_for_each_child_ptr(zan_ast_node_t *n, zan_ast_child_ptr_fn fn, void *user_data) {
    if (!n || !fn) return true;
    int c = zan_ast_child_count(n);
    for (int i = 0; i < c; i++) {
        zan_ast_node_t **p = zan_ast_child_ptr_at(n, i);
        if (p && *p) {
            if (!fn(p, user_data)) return false;
        }
    }
    return true;
}

bool zan_ast_walk(zan_ast_node_t *root,
                  zan_ast_visitor_fn pre_visit,
                  zan_ast_visitor_fn post_visit,
                  void *user_data) {
    if (!root) return true;
    if (pre_visit) {
        zan_walk_action_t act = pre_visit(root, user_data);
        if (act == ZAN_WALK_ABORT) return false;
        if (act == ZAN_WALK_SKIP_CHILDREN) {
            if (post_visit) {
                zan_walk_action_t post_act = post_visit(root, user_data);
                if (post_act == ZAN_WALK_ABORT) return false;
            }
            return true;
        }
    }
    int count = zan_ast_child_count(root);
    for (int i = 0; i < count; i++) {
        zan_ast_node_t *child = zan_ast_child_at(root, i);
        if (child) {
            if (!zan_ast_walk(child, pre_visit, post_visit, user_data))
                return false;
        }
    }
    if (post_visit) {
        zan_walk_action_t post_act = post_visit(root, user_data);
        if (post_act == ZAN_WALK_ABORT) return false;
    }
    return true;
}

bool zan_ast_walk_const(const zan_ast_node_t *root,
                        zan_ast_visitor_const_fn pre_visit,
                        zan_ast_visitor_const_fn post_visit,
                        void *user_data) {
    if (!root) return true;
    if (pre_visit) {
        zan_walk_action_t act = pre_visit(root, user_data);
        if (act == ZAN_WALK_ABORT) return false;
        if (act == ZAN_WALK_SKIP_CHILDREN) {
            if (post_visit) {
                zan_walk_action_t post_act = post_visit(root, user_data);
                if (post_act == ZAN_WALK_ABORT) return false;
            }
            return true;
        }
    }
    int count = zan_ast_child_count(root);
    for (int i = 0; i < count; i++) {
        const zan_ast_node_t *child = zan_ast_child_at(root, i);
        if (child) {
            if (!zan_ast_walk_const(child, pre_visit, post_visit, user_data))
                return false;
        }
    }
    if (post_visit) {
        zan_walk_action_t post_act = post_visit(root, user_data);
        if (post_act == ZAN_WALK_ABORT) return false;
    }
    return true;
}

size_t zan_ast_subtree_count(const zan_ast_node_t *root) {
    if (!root) return 0;
    size_t total = 1;
    int nc = zan_ast_child_count(root);
    for (int i = 0; i < nc; i++)
        total += zan_ast_subtree_count(zan_ast_child_at(root, i));
    return total;
}
