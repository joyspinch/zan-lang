/* 内部辅助实现 */

#include "checker.h"
#include "reflect_api.h"
#include "definite.h"
#include "diag.h"
#include "arena.h"
#include "builtin_api.h"
#include <string.h>
#include <stdio.h>

static const char *type_name(zan_type_t *t);

/* 内部辅助逻辑 */
static zan_type_t *builtin_call_result_type(zan_checker_t *c,
                                            zan_type_t *recv,
                                            zan_istr_t name) {
    const char *bt = NULL;
    if (!recv) return NULL;
    if (recv->kind == TYPE_STRING) bt = "string";
    else if (recv->kind == TYPE_CLASS && recv->name.len == 4 &&
             memcmp(recv->name.str, "List", 4) == 0) bt = "List";
    else if (recv->kind == TYPE_CLASS && recv->name.len == 4 &&
             memcmp(recv->name.str, "Dict", 4) == 0) bt = "Dict";
    else if (recv->kind == TYPE_CLASS && recv->name.len == 13 &&
             memcmp(recv->name.str, "StringBuilder", 13) == 0)
        bt = "StringBuilder";
    if (!bt) return NULL;
    /* Methods only: a property invoked with parentheses (`items */
    if (zan_builtin_member_kind(bt, name.str, (int)name.len) != 'M') return NULL;
    const char *res = zan_builtin_member_result(bt, name.str, (int)name.len);
    if (!res) return NULL;
    if (strcmp(res, "void") == 0) return c->binder->type_void;
    if (strcmp(res, "string") == 0) return c->binder->type_string;
    if (strcmp(res, "int") == 0) return c->binder->type_int;
    if (strcmp(res, "long") == 0) return c->binder->type_long;
    if (strcmp(res, "double") == 0) return c->binder->type_double;
    if (strcmp(res, "bool") == 0) return c->binder->type_bool;
    if (strcmp(res, "List<string>") == 0)
        return zan_binder_make_list_type(c->binder, c->binder->type_string);
    /* 内部辅助逻辑 */
    if (strcmp(res, "T[]") == 0) {
        zan_type_t *elem = recv->type_arg_count > 0 ? recv->type_args[0] : NULL;
        if (!elem) return NULL;
        return zan_binder_make_array_type(c->binder, elem);
    }
    return NULL;
}

/* Depth cap for every walk that follows base_type / interface chains */
#define CHECKER_DERIVES_MAX_DEPTH 8192

void zan_checker_init(zan_checker_t *c, zan_binder_t *binder,
                      zan_arena_t *arena, zan_diag_t *diag) {
    memset(c, 0, sizeof(*c));
    c->binder = binder;
    c->arena = arena;
    c->diag = diag;
    c->current_return_type = NULL;
}

/* 内部辅助实现 */
static void no_runtime_reject(zan_checker_t *c, zan_loc_t loc, const char *what) {
    if (!c->in_no_runtime) return;
    zan_diag_emit(c->diag, DIAG_ERROR, loc,
                  "%s is not allowed in a [NoRuntime] method", what);
}

static bool no_runtime_arc_return_type(zan_type_t *type) {
    return type && (type->kind == TYPE_STRING ||
                    type->kind == TYPE_INTERFACE ||
                    type->kind == TYPE_CLASS);
}

static void no_runtime_warn_arc_return(zan_checker_t *c,
                                       zan_ast_node_t *call,
                                       zan_type_t *type) {
    if (!c->in_no_runtime || !no_runtime_arc_return_type(type)) return;
    zan_diag_emit(c->diag, DIAG_WARNING, call->loc,
                  "call in a [NoRuntime] method returns an ARC-managed value "
                  "with an owned +1 reference, but [NoRuntime] emits no ARC; "
                  "the +1 will not be released and will leak. Move the call "
                  "out of the [NoRuntime] method or use a non-owning return");
}

/* 内部辅助逻辑 */
static zan_type_t *checker_nullable_base(zan_type_t *t) {
    return (t && t->kind == TYPE_NULLABLE && t->element_type)
        ? t->element_type : t;
}

/* 查找a named method (e */
static zan_symbol_t *checker_find_method(zan_symbol_t *type_sym, zan_istr_t name) {
    while (type_sym) {
        for (int i = 0; i < type_sym->member_count; i++) {
            zan_symbol_t *m = type_sym->members[i];
            if (m && m->kind == SYM_METHOD &&
                m->name.len == name.len &&
                memcmp(m->name.str, name.str, (size_t)name.len) == 0)
                return m;
        }
        type_sym = (type_sym->type && type_sym->type->base_type)
            ? type_sym->type->base_type->sym : NULL;
    }
    return NULL;
}

/* 内部辅助实现 */

/* The declared field of `type_sym` named `name`, walking the base chain */
static zan_symbol_t *checker_find_field(zan_symbol_t *type_sym, zan_istr_t name) {
    while (type_sym) {
        zan_symbol_t *found = NULL;
        for (int i = 0; i < type_sym->member_count; i++) {
            zan_symbol_t *m = type_sym->members[i];
            if (m && (m->kind == SYM_FIELD || m->kind == SYM_PROPERTY) &&
                m->name.len == name.len &&
                memcmp(m->name.str, name.str, (size_t)name.len) == 0)
                found = m;
        }
        if (found) return found;
        type_sym = (type_sym->type && type_sym->type->base_type)
            ? type_sym->type->base_type->sym : NULL;
    }
    return NULL;
}

/* 内部辅助实现 */

static bool checker_type_derives_from(zan_type_t *sub, zan_type_t *sup);

/* Length of the module key of dotted namespace `ns`. */
static uint32_t access_module_len(zan_istr_t ns) {
    int dots = 0;
    for (uint32_t i = 0; i < ns.len; i++) {
        if (ns.str[i] == '.') {
            dots++;
            if (dots == 2) return i;
        }
    }
    return ns.len;
}

static bool access_same_module(zan_istr_t a, zan_istr_t b) {
    uint32_t la = access_module_len(a), lb = access_module_len(b);
    return la == lb && memcmp(a.str, b.str, (size_t)la) == 0;
}

/* 内部辅助实现 */
static zan_istr_t access_symbol_ns(zan_symbol_t *s) {
    while (s) {
        if (s->decl && zan_ast_ns_name(s->decl).len)
            return zan_ast_ns_name(s->decl);
        s = s->parent;
    }
    return (zan_istr_t){NULL, 0};
}

static zan_ast_node_t *access_current_decl(zan_checker_t *c) {
    return c->current_type_sym ? c->current_type_sym->decl : NULL;
}

static void report_inaccessible(zan_checker_t *c, zan_symbol_t *m,
                                zan_loc_t loc) {
    zan_istr_t owner_name = m->parent ? m->parent->name : m->name;
    if (!c->current_type_sym) return;
    if (m->modifiers & MOD_PRIVATE)
        zan_diag_emit(c->diag, DIAG_ERROR, loc,
                      "'%.*s.%.*s' is private and cannot be accessed "
                      "from '%.*s'",
                      (int)owner_name.len, owner_name.str,
                      (int)m->name.len, m->name.str,
                      (int)c->current_type_sym->name.len,
                      c->current_type_sym->name.str);
    else if (m->modifiers & MOD_PROTECTED)
        zan_diag_emit(c->diag, DIAG_ERROR, loc,
                      "'%.*s.%.*s' is protected and cannot be accessed "
                      "from '%.*s'",
                      (int)owner_name.len, owner_name.str,
                      (int)m->name.len, m->name.str,
                      (int)c->current_type_sym->name.len,
                      c->current_type_sym->name.str);
    else if (m->modifiers & MOD_INTERNAL) {
        zan_istr_t mod_ns = access_symbol_ns(m);
        zan_ast_node_t *cur = access_current_decl(c);
        zan_istr_t cur_ns = cur ? zan_ast_ns_name(cur) : (zan_istr_t){0};
        zan_istr_t cur_name = cur_ns.len ? cur_ns : c->current_type_sym->name;
        zan_diag_emit(c->diag, DIAG_ERROR, loc,
                      "'%.*s.%.*s' is internal to '%.*s' and cannot be "
                      "accessed from '%.*s'",
                      (int)owner_name.len, owner_name.str,
                      (int)m->name.len, m->name.str,
                      (int)mod_ns.len, mod_ns.str,
                      (int)cur_name.len, cur_name.str);
    }
}

/* 内部辅助实现 */
static bool access_member_allowed(zan_checker_t *c, zan_symbol_t *m) {
    if (!(m->modifiers & (MOD_PRIVATE | MOD_PROTECTED | MOD_INTERNAL)))
        return true;
    if (!c->current_type_sym || !m->parent ||
        !m->parent->type || !c->current_type_sym->type)
        return false;
    /* private: the declaring type's own bodies */
    if (c->current_type_sym == m->parent) return true;
    /* protected: plus bodies of derived types */
    if ((m->modifiers & MOD_PROTECTED) &&
        checker_type_derives_from(c->current_type_sym->type,
                                  m->parent->type))
        return true;
    /* internal: any type whose namespace shares the module prefix */
    if ((m->modifiers & MOD_INTERNAL) &&
        access_same_module(access_symbol_ns(c->current_type_sym),
                           access_symbol_ns(m)))
        return true;
    return false;
}

/* Field lookup for assignment targets etc */
static zan_symbol_t *checker_field_visible(zan_checker_t *c,
                                           zan_symbol_t *ts, zan_loc_t loc,
                                           zan_istr_t name) {
    zan_symbol_t *f = checker_find_field(ts, name);
    if (f && !access_member_allowed(c, f)) {
        report_inaccessible(c, f, loc);
        return NULL;
    }
    return f;
}

/* 内部辅助实现 */
struct checker_local {
    zan_istr_t name;
    zan_type_t *type;
    /* 内部辅助实现 */
    zan_symbol_t *null_src;
    struct checker_local *next;
};

static struct checker_local *checker_find_local_slot(zan_checker_t *c,
                                                     zan_istr_t name) {
    for (struct checker_local *l = c->locals; l; l = l->next) {
        if (l->name.len == name.len &&
            memcmp(l->name.str, name.str, (size_t)name.len) == 0)
            return l;
    }
    return NULL;
}

static zan_type_t *checker_find_local(zan_checker_t *c, zan_istr_t name) {
    for (struct checker_local *l = c->locals; l; l = l->next) {
        if (l->name.len == name.len &&
            memcmp(l->name.str, name.str, (size_t)name.len) == 0)
            return l->type;
    }
    return NULL;
}

static void checker_add_local(zan_checker_t *c, zan_istr_t name,
                              zan_type_t *type) {
    if (!type || type == c->binder->type_error) return;
    struct checker_local *l = (struct checker_local *)
        zan_arena_alloc(c->arena, sizeof(struct checker_local));
    l->name = name;
    l->type = type;
    l->null_src = NULL;
    l->next = c->locals;
    c->locals = l;
}

/* 内部辅助逻辑 */
static void checker_mark_local_null_src(zan_checker_t *c, zan_ast_node_t *decl) {
    zan_ast_node_t *init = decl->var_decl.initializer;
    if (!init || init->kind != AST_CALL || init != c->last_call_node) return;
    if (!c->last_call_method) return;
    struct checker_local *l = checker_find_local_slot(c, decl->var_decl.name);
    if (l) l->null_src = c->last_call_method;
}

static bool field_is_readonly(zan_symbol_t *field) {
    return field && field->decl && field->decl->kind == AST_FIELD_DECL &&
           (field->decl->field_decl.modifiers & MOD_READONLY) != 0;
}

/* A getter-only property (`{ get; }` / `{ get { */
static bool property_is_readonly(zan_symbol_t *prop) {
    if (!prop || prop->kind != SYM_PROPERTY || !prop->decl) return false;
    return !prop->decl->field_decl.has_setter &&
           !prop->decl->field_decl.has_init;
}

/* 内部辅助实现 */
static bool property_is_init_only(zan_symbol_t *prop) {
    if (!prop || prop->kind != SYM_PROPERTY || !prop->decl) return false;
    return prop->decl->field_decl.has_init &&
           !prop->decl->field_decl.has_setter;
}

/* 内部辅助逻辑 */
static zan_symbol_t *assign_target_field(zan_checker_t *c, zan_ast_node_t *lhs,
                                         zan_symbol_t **owner) {
    if (!lhs) return NULL;
    if (lhs->kind == AST_IDENTIFIER) {
        if (!c->current_type_sym) return NULL;
        if (owner) *owner = c->current_type_sym;
        return checker_field_visible(c, c->current_type_sym, lhs->loc,
                                     lhs->ident.name);
    }
    if (lhs->kind == AST_MEMBER_ACCESS) {
        zan_symbol_t *ts = NULL;
        if (lhs->member.object && lhs->member.object->kind == AST_THIS_EXPR) {
            ts = c->current_type_sym;
        } else {
            zan_type_t *ot = zan_checker_check_expr(c, lhs->member.object);
            ts = ot ? ot->sym : NULL;
        }
        if (!ts) return NULL;
        if (owner) *owner = ts;
        return checker_field_visible(c, ts, lhs->loc, lhs->member.name);
    }
    return NULL;
}

static void check_readonly_assignment(zan_checker_t *c, zan_ast_node_t *expr) {
    if (expr && expr->binary.left && expr->binary.left->kind == AST_IDENTIFIER &&
        checker_find_local(c, expr->binary.left->ident.name))
        return;
    zan_symbol_t *owner = NULL;
    zan_symbol_t *field = assign_target_field(c, expr->binary.left, &owner);
    /* a getter-only property has no setter: the write cannot be dispatched */
    if (property_is_readonly(field)) {
        zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                      "property '%.*s' has no setter and cannot be assigned",
                      (int)field->name.len, field->name.str);
        return;
    }
    /* 内部辅助实现 */
    if (property_is_init_only(field)) {
        if (c->in_ctor && field->parent == c->current_type_sym) return;
        zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                      "property '%.*s' has an init accessor and can only be "
                      "assigned in an object initializer or a constructor of "
                      "'%.*s'",
                      (int)field->name.len, field->name.str,
                      (int)(field->parent ? field->parent->name.len : 0),
                      field->parent ? field->parent->name.str : "");
        return;
    }
    if (!field_is_readonly(field)) return;
    /* 派生类构造函数不可初始化基类声明的 readonly 只读字段 */
    if (c->in_ctor && field->parent == c->current_type_sym) return;
    zan_symbol_t *decl_owner = field->parent ? field->parent : owner;
    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                  "cannot assign to readonly field '%.*s' outside a "
                  "constructor of '%.*s'",
                  (int)field->name.len, field->name.str,
                  (int)(decl_owner ? decl_owner->name.len : 0),
                  decl_owner ? decl_owner->name.str : "");
}

/* 内部辅助实现 */
static void check_readonly_incdec(zan_checker_t *c, zan_ast_node_t *expr) {
    if (!expr || !expr->unary.operand) return;
    zan_ast_node_t *operand = expr->unary.operand;
    zan_symbol_t *field = NULL;
    if (operand->kind == AST_IDENTIFIER) {
        if (checker_find_local(c, operand->ident.name)) return;
        if (!c->current_type_sym) return;
        field = checker_field_visible(c, c->current_type_sym, expr->loc,
                                      operand->ident.name);
    } else if (operand->kind == AST_MEMBER_ACCESS) {
        zan_symbol_t *ts = NULL;
        if (operand->member.object &&
            operand->member.object->kind == AST_THIS_EXPR) {
            ts = c->current_type_sym;
        } else {
            zan_type_t *ot = zan_checker_check_expr(c, operand->member.object);
            ts = ot ? ot->sym : NULL;
        }
        if (!ts) return;
        field = checker_field_visible(c, ts, expr->loc, operand->member.name);
    }
    if (!property_is_readonly(field)) return;
    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                  "property '%.*s' has no setter and cannot be assigned",
                  (int)field->name.len, field->name.str);
}

static bool checker_type_assignable(zan_type_t *target, zan_type_t *value);

static zan_type_t *checker_assignment_target_type(zan_checker_t *c,
                                                   zan_ast_node_t *lhs) {
    if (!lhs) return NULL;
    if (lhs->kind == AST_IDENTIFIER) {
        zan_type_t *local = checker_find_local(c, lhs->ident.name);
        if (local) return local;
        zan_symbol_t *field = assign_target_field(c, lhs, NULL);
        return field ? field->type : NULL;
    }
    if (lhs->kind == AST_MEMBER_ACCESS) {
        zan_type_t *obj = zan_checker_check_expr(c, lhs->member.object);
        zan_symbol_t *sym = obj && obj->sym
            ? checker_field_visible(c, obj->sym, lhs->loc, lhs->member.name)
            : NULL;
        return sym ? sym->type : NULL;
    }
    if (lhs->kind == AST_INDEX) {
        zan_type_t *obj = zan_checker_check_expr(c, lhs->index.object);
        zan_checker_check_expr(c, lhs->index.index);
        if (obj && obj->kind == TYPE_ARRAY) return obj->element_type;
        if (obj && obj->kind == TYPE_STRING) return c->binder->type_char;
        if (obj && obj->kind == TYPE_CLASS && obj->type_arg_count == 1 &&
            obj->name.len == 4 && memcmp(obj->name.str, "List", 4) == 0)
            return obj->type_args[0];
    }
    return NULL;
}

/* 内部辅助逻辑 */
static zan_type_t *checker_index_set_target(zan_checker_t *c,
                                            zan_ast_node_t *lhs,
                                            zan_type_t *right) {
    zan_type_t *obj = zan_checker_check_expr(c, lhs->index.object);
    zan_type_t *idx = zan_checker_check_expr(c, lhs->index.index);
    if (!obj || !obj->sym ||
        (obj->kind != TYPE_CLASS && obj->kind != TYPE_STRUCT))
        return NULL;
    zan_istr_t op_name = {(char *)"op_index_set", 12};
    for (int i = 0; i < obj->sym->member_count; i++) {
        zan_symbol_t *m = obj->sym->members[i];
        if (!m) continue;
        if (m->kind != SYM_METHOD || m->name.len != op_name.len ||
            memcmp(m->name.str, op_name.str, op_name.len) != 0) continue;
        if (!m->decl || m->decl->kind != AST_METHOD_DECL)
            continue;
        /* Two declared shapes: parser-synthesized indexers carry (index */
        zan_ast_list_t *ps = &m->decl->method_decl.params;
        int n = ps->count;
        if (n < 2) continue;
        zan_ast_node_t *pidx = ps->items[n - 2];
        zan_ast_node_t *pval = ps->items[n - 1];
        if (n >= 3) {
            zan_ast_node_t *pself = ps->items[0];
            if (pself && pself->kind == AST_PARAM && pself->param.type) {
                zan_type_t *ptself =
                    zan_binder_resolve_type(c->binder, pself->param.type);
                if (ptself && ptself->kind != TYPE_TYPE_PARAM &&
                    !checker_type_assignable(ptself, obj))
                    continue;
            }
        }
        if (pidx->kind != AST_PARAM || pval->kind != AST_PARAM) continue;
        zan_type_t *pt1 = zan_binder_resolve_type(c->binder, pidx->param.type);
        zan_type_t *pt2 = zan_binder_resolve_type(c->binder, pval->param.type);
        if (idx && !checker_type_assignable(pt1, idx)) continue;
        if (right && !checker_type_assignable(pt2, right)) continue;
        return pt2;
    }
    return NULL;
}

/* 内部辅助逻辑 */
static bool expr_captures_this(zan_checker_t *c, zan_ast_node_t *node) {
    if (!node) return false;
    switch (node->kind) {
    case AST_THIS_EXPR:
        return true;
    case AST_IDENTIFIER: {
        zan_istr_t name = node->ident.name;
        /* If resolved to a local or parameter, it is not 'this' */
        if (checker_find_local(c, name)) return false;
        /* 内部辅助逻辑 */
        if (c->current_type_sym) {
            zan_symbol_t *f = checker_find_field(c->current_type_sym, name);
            if (f && !(f->modifiers & MOD_STATIC)) return true;
            zan_symbol_t *m = checker_find_method(c->current_type_sym, name);
            if (m && !(m->modifiers & MOD_STATIC)) return true;
        }
        return false;
    }
    case AST_MEMBER_ACCESS:
        if (node->member.object && node->member.object->kind == AST_THIS_EXPR)
            return true;
        return expr_captures_this(c, node->member.object);
    case AST_CALL:
        if (expr_captures_this(c, node->call.callee)) return true;
        for (int i = 0; i < node->call.args.count; i++) {
            if (expr_captures_this(c, node->call.args.items[i])) return true;
        }
        return false;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        return expr_captures_this(c, node->binary.left) ||
               expr_captures_this(c, node->binary.right);
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        return expr_captures_this(c, node->unary.operand);
    case AST_INDEX:
        if (expr_captures_this(c, node->index.object)) return true;
        if (expr_captures_this(c, node->index.index)) return true;
        for (int i = 0; i < node->index.extra.count; i++) {
            if (expr_captures_this(c, node->index.extra.items[i])) return true;
        }
        return false;
    case AST_CONDITIONAL:
        return expr_captures_this(c, node->conditional.cond) ||
               expr_captures_this(c, node->conditional.then_expr) ||
               expr_captures_this(c, node->conditional.else_expr);
    case AST_NEW_EXPR:
        if (node->new_expr.call_init && expr_captures_this(c, node->new_expr.call_init))
            return true;
        for (int i = 0; i < node->new_expr.args.count; i++) {
            if (expr_captures_this(c, node->new_expr.args.items[i])) return true;
        }
        for (int i = 0; i < node->new_expr.arg_inits.count; i++) {
            if (expr_captures_this(c, node->new_expr.arg_inits.items[i])) return true;
        }
        return false;
    case AST_LAMBDA:
        /* Nested lambda inside lambda */
        return expr_captures_this(c, node->lambda.body);
    case AST_BLOCK:
        for (int i = 0; i < node->block.stmts.count; i++) {
            if (expr_captures_this(c, node->block.stmts.items[i])) return true;
        }
        return false;
    case AST_RETURN_STMT:
        return expr_captures_this(c, node->ret.value);
    case AST_IF_STMT:
        return expr_captures_this(c, node->if_stmt.cond) ||
               expr_captures_this(c, node->if_stmt.then_body) ||
               expr_captures_this(c, node->if_stmt.else_body);
    case AST_WHILE_STMT:
        return expr_captures_this(c, node->while_stmt.cond) ||
               expr_captures_this(c, node->while_stmt.body);
    case AST_FOR_STMT:
        return expr_captures_this(c, node->for_stmt.init) ||
               expr_captures_this(c, node->for_stmt.cond) ||
               expr_captures_this(c, node->for_stmt.step) ||
               expr_captures_this(c, node->for_stmt.body);
    case AST_EXPR_STMT:
        return expr_captures_this(c, node->expr_stmt.expr);
    case AST_CAST_EXPR:
        return expr_captures_this(c, node->cast.expr);
    case AST_AWAIT_EXPR:
        return expr_captures_this(c, node->await_expr.expr);
    case AST_VAR_DECL:
        return expr_captures_this(c, node->var_decl.initializer);
    case AST_TUPLE_EXPR:
        for (int i = 0; i < node->tuple_expr.items.count; i++) {
            if (expr_captures_this(c, node->tuple_expr.items.items[i])) return true;
        }
        return false;
    default:
        return false;
    }
}

static bool has_closure_capturing_this(zan_checker_t *c, zan_ast_node_t *node) {
    if (!node) return false;
    while (node && node->kind == AST_CAST_EXPR) node = node->cast.expr;
    if (!node) return false;
    if (node->kind == AST_LAMBDA) {
        return expr_captures_this(c, node->lambda.body);
    }
    if (node->kind == AST_ASSIGNMENT) {
        return has_closure_capturing_this(c, node->binary.right);
    }
    if (node->kind == AST_NEW_EXPR) {
        for (int i = 0; i < node->new_expr.args.count; i++) {
            if (has_closure_capturing_this(c, node->new_expr.args.items[i])) return true;
        }
        for (int i = 0; i < node->new_expr.arg_inits.count; i++) {
            if (has_closure_capturing_this(c, node->new_expr.arg_inits.items[i])) return true;
        }
    }
    return false;
}

/* 内部辅助实现 */
static void check_closure_cycle_warning(zan_checker_t *c, zan_ast_node_t *lhs,
                                       zan_ast_node_t *rhs, zan_loc_t loc) {
    if (!c->current_type_sym || !lhs || !rhs) return;
    /* Only classes participate in ARC cycles */
    if (c->current_type_sym->kind != SYM_CLASS) return;

    /* 检查if lhs is a member of 'this', e */
    bool targets_this = false;
    if (lhs->kind == AST_MEMBER_ACCESS) {
        zan_ast_node_t *obj = lhs->member.object;
        while (obj && obj->kind == AST_MEMBER_ACCESS) {
            obj = obj->member.object;
        }
        if (obj && obj->kind == AST_THIS_EXPR) {
            targets_this = true;
        } else if (obj && obj->kind == AST_IDENTIFIER) {
            zan_istr_t name = obj->ident.name;
            if (!checker_find_local(c, name)) {
                zan_symbol_t *f = checker_find_field(c->current_type_sym, name);
                if (f && !(f->modifiers & MOD_STATIC)) {
                    targets_this = true;
                }
            }
        }
    } else if (lhs->kind == AST_IDENTIFIER) {
        zan_istr_t name = lhs->ident.name;
        if (!checker_find_local(c, name)) {
            zan_symbol_t *f = checker_find_field(c->current_type_sym, name);
            if (f && !(f->modifiers & MOD_STATIC)) {
                targets_this = true;
            }
        }
    }

    if (targets_this && has_closure_capturing_this(c, rhs)) {
        zan_diag_emit(c->diag, DIAG_WARNING, loc,
            "closure captures 'this' while assigned to a member of 'this'; "
            "this creates a potential strong reference cycle under ARC");
    }
}

/* ---- generic constraint checking ---- */

/* True when `arg` is, implements, or derives from `cons` */
static bool type_satisfies_constraint_depth(zan_type_t *arg, zan_type_t *cons,
                                            int depth) {
    if (!arg || !cons) return true;
    if (depth > CHECKER_DERIVES_MAX_DEPTH) return false;
    if (arg == cons) return true;
    if (arg->sym && cons->sym && arg->sym == cons->sym) return true;
    for (int i = 0; i < arg->interface_count; i++) {
        if (type_satisfies_constraint_depth(arg->interfaces[i], cons, depth + 1))
            return true;
    }
    if (arg->base_type && arg->base_type != arg)
        return type_satisfies_constraint_depth(arg->base_type, cons, depth + 1);
    return false;
}

static bool type_satisfies_constraint(zan_type_t *arg, zan_type_t *cons) {
    return type_satisfies_constraint_depth(arg, cons, 0);
}

static const char *type_name(zan_type_t *t);

/* 内部辅助逻辑 */
static void check_generic_constraints(zan_checker_t *c, zan_type_t *t,
                                      zan_loc_t loc) {
    if (!t || !t->sym || !t->sym->decl) return;
    zan_ast_node_t *d = t->sym->decl;
    if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL &&
        d->kind != AST_INTERFACE_DECL)
        return;
    if (!t->type_args || t->type_arg_count == 0) return;
    if (!d->type_decl.where_clauses) return;
    for (int w = 0; w < d->type_decl.where_clauses->count; w++) {
        zan_ast_node_t *wc = d->type_decl.where_clauses->items[w];
        int idx = -1;
        for (int i = 0; i < d->type_decl.type_params.count; i++) {
            zan_ast_node_t *tp = d->type_decl.type_params.items[i];
            if (tp->ident.name.len == wc->where_clause.param_name.len &&
                memcmp(tp->ident.name.str, wc->where_clause.param_name.str,
                       (size_t)tp->ident.name.len) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0 || idx >= t->type_arg_count) continue;
        zan_type_t *arg = t->type_args[idx];
        /* 内部辅助实现 */
        if (arg && arg->kind == TYPE_TYPE_PARAM) continue;
        for (int k = 0; k < wc->where_clause.constraints.count; k++) {
            zan_ast_node_t *cons_ref = wc->where_clause.constraints.items[k];
            zan_type_t *cons = zan_binder_resolve_type(c->binder, cons_ref);
            if (!cons || cons->kind == TYPE_ERROR) {
                zan_diag_emit(c->diag, DIAG_ERROR, loc,
                    "unknown constraint '%.*s' in 'where %.*s : ...' for '%s'",
                    cons_ref->type_ref.name.len, cons_ref->type_ref.name.str,
                    wc->where_clause.param_name.len,
                    wc->where_clause.param_name.str,
                    type_name(t));
                continue;
            }
            if (!type_satisfies_constraint(arg, cons)) {
                zan_diag_emit(c->diag, DIAG_ERROR, loc,
                    "type '%s' does not satisfy constraint '%s' for type parameter '%.*s' of '%s'",
                    type_name(arg), type_name(cons),
                    wc->where_clause.param_name.len,
                    wc->where_clause.param_name.str,
                    type_name(t));
            }
        }
    }
}

/* ---- expression type checking ---- */

static const char *type_name(zan_type_t *t) {
    if (!t) return "<unknown>";
    return t->name.str;
}

/* 内部辅助逻辑 */
static bool checker_weak_target_is_arc_ref(zan_type_t *t) {
    if (!t) return false;
    if (t->kind == TYPE_TYPE_PARAM) return true;
    if (t->kind == TYPE_INTERFACE) return true;
    return t->kind == TYPE_CLASS && t->sym != NULL;
}

/* 内部辅助实现 */
static void check_extern_static(zan_checker_t *c, zan_ast_node_t *member) {
    if (!c || !member || member->kind != AST_METHOD_DECL) return;
    bool is_extern = zan_ast_method_extern_lib(member).str != NULL ||
                     (member->method_decl.modifiers & MOD_EXTERN) != 0;
    if (!is_extern) return;
    if (member->method_decl.modifiers & MOD_STATIC) return;
    zan_diag_emit(c->diag, DIAG_ERROR, member->loc,
                  "extern method '%.*s' must be static; a native function has "
                  "no receiver, so drop 'extern' or mark it 'static'",
                  (int)member->method_decl.name.len,
                  member->method_decl.name.str);
}
static void check_weak_member(zan_checker_t *c, zan_ast_node_t *owner,
                              zan_ast_node_t *member) {
    if (!c || !owner || !member ||
        (member->kind != AST_FIELD_DECL && member->kind != AST_PROPERTY_DECL) ||
        !(member->field_decl.modifiers & MOD_WEAK))
        return;

    if (owner->kind == AST_STRUCT_DECL) {
        zan_diag_emit(c->diag, DIAG_ERROR, member->loc,
                      "weak field '%.*s' inside value-type struct '%.*s' "
                      "cannot be tracked safely; move it to a class or remove "
                      "'weak'",
                      (int)member->field_decl.name.len,
                      member->field_decl.name.str,
                      (int)owner->type_decl.name.len,
                      owner->type_decl.name.str);
        return;
    }

    zan_type_t *type = zan_binder_resolve_type(c->binder,
                                                member->field_decl.type);
    if (!type || type == c->binder->type_error) return;
    if (checker_weak_target_is_arc_ref(type)) return;

    zan_diag_emit(c->diag, DIAG_ERROR, member->loc,
                  "weak field '%.*s' has type '%s'; weak requires an "
                  "ARC-managed class or interface reference, so use a "
                  "class/interface type or remove 'weak'",
                  (int)member->field_decl.name.len,
                  member->field_decl.name.str,
                  type_name(type));
}

/* 内部辅助实现 */

/* 内部辅助实现 */
static void ctor_arity(zan_ast_node_t *decl, int *min, int *max) {
    int total = decl->method_decl.params.count;
    int required = 0;
    bool variadic = false;
    for (int i = 0; i < total; i++) {
        zan_ast_node_t *p = decl->method_decl.params.items[i];
        if (!p || p->kind != AST_PARAM) { required++; continue; }
        if (p->param.is_params) { variadic = true; continue; }
        if (!p->param.default_val) required++;
    }
    *min = required;
    *max = variadic ? -1 : total;
}

/* 内部辅助实现 */
static int ctor_arg_count(zan_checker_t *c, zan_ast_node_t *expr,
                          zan_symbol_t *type_sym) {
    int n = expr->new_expr.args.count;
    while (n > 0) {
        zan_ast_node_t *a = expr->new_expr.args.items[n - 1];
        if (a && a->kind == AST_COLL_INIT) {
            if (!checker_find_field(type_sym, a->coll_init.name)) break;
            n--;
            continue;
        }
        if (!a || a->kind != AST_ASSIGNMENT || !a->binary.left ||
            a->binary.left->kind != AST_IDENTIFIER ||
            !checker_find_field(type_sym, a->binary.left->ident.name))
            break;
        n--;
    }
    (void)c;
    return n;
}

/* 内部辅助实现 */
static bool arg_is_initializer_entry(zan_ast_node_t *a) {
    if (!a) return false;
    if (a->kind == AST_COLL_INIT) return true;
    return a->kind == AST_ASSIGNMENT && a->binary.left &&
           a->binary.left->kind == AST_IDENTIFIER;
}

/* 内部辅助实现 */
static bool initializer_covers_all_members(zan_ast_node_t *expr,
                                           zan_symbol_t *type_sym,
                                           int init_start) {
    int covered = 0;
    int members = 0;
    for (zan_symbol_t *t = type_sym; t; ) {
        for (int i = 0; i < t->member_count; i++) {
            zan_symbol_t *m = t->members[i];
            if (!m || (m->kind != SYM_FIELD && m->kind != SYM_PROPERTY))
                continue;
            if (m->modifiers & MOD_STATIC) continue;
            members++;
            for (int a = init_start; a < expr->new_expr.args.count; a++) {
                zan_ast_node_t *as = expr->new_expr.args.items[a];
                if (!arg_is_initializer_entry(as)) continue;
                zan_istr_t n = as->kind == AST_COLL_INIT
                    ? as->coll_init.name
                    : as->binary.left->ident.name;
                if (n.len == m->name.len &&
                    memcmp(n.str, m->name.str, (size_t)n.len) == 0) {
                    covered++;
                    break;
                }
            }
        }
        t = (t->type && t->type->base_type) ? t->type->base_type->sym : NULL;
    }
    return members > 0 && covered == members;
}

static bool type_is_scalar_primitive(zan_type_t *t);

static void check_ctor_available(zan_checker_t *c, zan_type_t *type,
                                 zan_ast_node_t *expr) {
    if (!type || expr->new_expr.is_array) return;
    /* 内部辅助逻辑 */
    if (type_is_scalar_primitive(type)) {
        zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
            "the builtin type '%s' has no constructor -- use literals, casts or the conversion functions",
            type_name(type));
        return;
    }
    if (type->kind != TYPE_CLASS || !type->sym) return;
    zan_symbol_t *sym = type->sym;
    int declared = 0;
    int argc = ctor_arg_count(c, expr, sym);
    /* 内部辅助实现 */
    if (argc == 0 && argc < expr->new_expr.args.count &&
        initializer_covers_all_members(expr, sym, argc)) {
        return;
    }
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (!m || m->kind != SYM_CONSTRUCTOR || !m->decl) continue;
        if (m->decl->kind != AST_CONSTRUCTOR_DECL) continue;
        declared++;
        int lo = 0;
        int hi = 0;
        ctor_arity(m->decl, &lo, &hi);
        if (argc >= lo && (hi < 0 || argc <= hi)) {
            if (!access_member_allowed(c, m))
                report_inaccessible(c, m, expr->loc);
            return;
        }
    }
    if (declared == 0) {
        if (argc > 0) {
            zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                "no constructor of '%s' accepts %d arguments",
                type_name(type), argc);
        }
        return;
    }
    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
        "no constructor of '%s' takes %d argument(s); a class that declares "
        "constructors has no implicit parameterless one",
        type_name(type), argc);
}

static bool type_is_numeric(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BYTE: case TYPE_SHORT: case TYPE_INT: case TYPE_LONG:
    case TYPE_SBYTE: case TYPE_USHORT: case TYPE_UINT: case TYPE_ULONG:
    case TYPE_FLOAT: case TYPE_DOUBLE: case TYPE_NINT:
        return true;
    default:
        return false;
    }
}

/* 内部辅助逻辑 */
static bool type_is_scalar_primitive(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BOOL: case TYPE_BYTE: case TYPE_SHORT: case TYPE_INT:
    case TYPE_LONG: case TYPE_SBYTE: case TYPE_USHORT: case TYPE_UINT:
    case TYPE_ULONG: case TYPE_FLOAT: case TYPE_DOUBLE: case TYPE_CHAR:
    case TYPE_STRING: case TYPE_NINT:
        return true;
    default:
        return false;
    }
}

static bool type_is_integral(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BYTE: case TYPE_SHORT: case TYPE_INT: case TYPE_LONG:
    case TYPE_SBYTE: case TYPE_USHORT: case TYPE_UINT: case TYPE_ULONG:
    case TYPE_NINT:
        return true;
    default:
        return false;
    }
}

static zan_type_t *promote_numeric(zan_binder_t *b, zan_type_t *a, zan_type_t *b_type) {
    if (!a || !b_type) return b->type_error;
    if (a->kind == TYPE_DOUBLE || b_type->kind == TYPE_DOUBLE) return b->type_double;
    if (a->kind == TYPE_FLOAT  || b_type->kind == TYPE_FLOAT)  return b->type_float;
    if (a->kind == TYPE_ULONG  || b_type->kind == TYPE_ULONG)  return b->type_ulong;
    if (a->kind == TYPE_LONG   || b_type->kind == TYPE_LONG)   return b->type_long;
    return b->type_int;
}

/* 内部辅助实现 */

/* 内部辅助逻辑 */
static bool checker_type_equal(zan_type_t *a, zan_type_t *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_NULLABLE) {
        if (a->kind == TYPE_ARRAY && a->array_rank != b->array_rank)
            return false; /* int[,] is not int[] */
        return checker_type_equal(a->element_type, b->element_type);
    }
    if (a->name.len != b->name.len ||
        (a->name.len && memcmp(a->name.str, b->name.str, (size_t)a->name.len) != 0))
        return false;
    if (a->type_arg_count != b->type_arg_count) return false;
    for (int i = 0; i < a->type_arg_count; i++)
        if (!checker_type_equal(a->type_args[i], b->type_args[i])) return false;
    return true;
}

/* 检查是否one declared base edge proves a derivation */
static bool iface_edge_derives_params_ast(zan_ast_node_t *edge_ref,
                                          zan_ast_list_t *tps,
                                          zan_type_t **args,
                                          zan_type_t *sup) {
    if (!edge_ref || edge_ref->kind != AST_TYPE_REF || !sup || !sup->sym)
        return false;
    zan_istr_t en = edge_ref->type_ref.name;
    if (en.len != sup->sym->name.len ||
        memcmp(en.str, sup->sym->name.str, (size_t)en.len) != 0)
        return false;
    zan_ast_list_t *eargs = &edge_ref->type_ref.type_args;
    int want = sup->type_arg_count;
    if (eargs->count != want) return false;
    for (int j = 0; j < want; j++) {
        zan_ast_node_t *ea = eargs->items[j];
        if (!ea) return false;
        if (ea->kind == AST_TYPE_REF && ea->type_ref.type_args.count > 0)
            return false; /* concrete generic argument: conservative reject */
        /* A bare name in type position parses as an AST_TYPE_REF (parser */
        zan_istr_t ename;
        if (ea->kind == AST_IDENTIFIER) ename = ea->ident.name;
        else if (ea->kind == AST_TYPE_REF) ename = ea->type_ref.name;
        else return false;
        int k = 0;
        for (; k < tps->count; k++) {
            zan_istr_t tn = tps->items[k]->ident.name;
            if (tn.len == ename.len &&
                memcmp(tn.str, ename.str, (size_t)tn.len) == 0)
                break;
        }
        if (k < tps->count) {
            if (args == NULL || args[k] == NULL ||
                !checker_type_equal(args[k], sup->type_args[j]))
                return false;
        } else {
            zan_type_t *sa = sup->type_args[j];
            if (!sa || ename.len != sa->name.len ||
                memcmp(ename.str, sa->name.str, (size_t)sa->name.len) != 0)
                return false;
        }
    }
    return true;
}

/* 内部辅助实现 */
static bool checker_type_derives_from_depth(zan_type_t *sub, zan_type_t *sup,
                                            int depth) {
    if (!sub || !sup) return false;
    if (depth > CHECKER_DERIVES_MAX_DEPTH) return false;
    if (sub == sup) return true;
    if (sub->sym && sup->sym && sub->sym == sup->sym) {
        if (sup->type_arg_count == 0 && sup->sym->decl &&
            (sup->sym->decl->kind == AST_CLASS_DECL || sup->sym->decl->kind == AST_STRUCT_DECL) &&
            sup->sym->decl->type_decl.type_params.count > 0)
            return true;
        return checker_type_equal(sub, sup);
    }
    if (sub->kind == sup->kind && checker_type_equal(sub, sup)) return true;
    for (int i = 0; i < sub->interface_count; i++)
        if (checker_type_derives_from_depth(sub->interfaces[i], sup, depth + 1))
            return true;
    zan_type_t *base = sub->base_type;
    /* 内部辅助逻辑 */
    if (!base && sub->sym && sub->sym->type && sub->sym->type != sub)
        base = sub->sym->type->base_type;
    if (base && base != sub)
        return checker_type_derives_from_depth(base, sup, depth + 1);
    /* 内部辅助实现 */
    if (sub->type_arg_count > 0 && sub->sym && sub->sym->type &&
        sub->sym->type != sub && sub->sym->decl &&
        (sub->sym->decl->kind == AST_CLASS_DECL ||
         sub->sym->decl->kind == AST_STRUCT_DECL ||
         sub->sym->decl->kind == AST_INTERFACE_DECL)) {
        zan_ast_list_t *tps = &sub->sym->decl->type_decl.type_params;
        zan_ast_list_t *bases = &sub->sym->decl->type_decl.bases;

        for (int i = 0; i < bases->count; i++)
            if (iface_edge_derives_params_ast(bases->items[i], tps,
                                              sub->type_args, sup))
                return true;
    }
    return false;
}

static bool checker_type_derives_from(zan_type_t *sub, zan_type_t *sup) {
    return checker_type_derives_from_depth(sub, sup, 0);
}

/* 内部辅助实现 */
static bool checker_type_is_ref(zan_type_t *t) {
    if (!t) return false;
    return t->kind == TYPE_OBJECT || t->kind == TYPE_INTERFACE ||
           t->kind == TYPE_STRING || t->kind == TYPE_CLASS ||
           t->kind == TYPE_ARRAY || t->kind == TYPE_DELEGATE ||
           t->kind == TYPE_TASK; /* Task is a reference type in C#; null is legal */
}

/* Merge the then/else branch types of a conditional expression */
static zan_type_t *merge_conditional_types(zan_checker_t *c,
                                           zan_type_t *a, zan_type_t *b);
static bool expr_is_null_literal(zan_ast_node_t *e);
static zan_type_t *merge_conditional_types(zan_checker_t *c,
                                           zan_type_t *a, zan_type_t *b) {
    if (!a) return b;
    if (!b) return a;
    if (a == c->binder->type_error) return b;
    if (b == c->binder->type_error) return a;
    if (a == b) return a;
    /* 内部辅助逻辑 */
    if (a->kind == TYPE_TYPE_PARAM || b->kind == TYPE_TYPE_PARAM) return a;

    /* identical types (same kind, name and generic arguments) */
    if (a->kind == b->kind && checker_type_equal(a, b)) return a;

    /* numeric promotion: int widens to long, float to double, ... */
    if ((type_is_numeric(a) || a->kind == TYPE_CHAR) &&
        (type_is_numeric(b) || b->kind == TYPE_CHAR))
        return promote_numeric(c->binder, a, b);

    if (checker_type_is_ref(a) && checker_type_is_ref(b)) {
        if (checker_type_derives_from(b, a)) return a;  /* b is-a a */
        if (checker_type_derives_from(a, b)) return b;  /* a is-a b */
        /* 内部辅助逻辑 */
        for (zan_type_t *t = a->base_type; t && t != a; t = t->base_type) {
            if (checker_type_derives_from(b, t)) return t;
            if (!t->base_type) break;
        }
        return NULL; /* unrelated references have no implicit common type */
    }

    /* 内部辅助实现 */
    if (a->kind == TYPE_ENUM || b->kind == TYPE_ENUM) return a;

    return NULL; /* unrelated value types, or a value mixed with a reference */
}

/* True when `a` and `b` are the same generic container class (List< */
static bool same_generic_container(zan_type_t *a, zan_type_t *b) {
    if (!a || !b || a->kind != b->kind) return false;
    if (a->type_arg_count != b->type_arg_count || a->type_arg_count == 0)
        return false;
    if (a->sym && b->sym) return a->sym == b->sym;
    return a->name.len == b->name.len &&
           memcmp(a->name.str, b->name.str, (size_t)a->name.len) == 0;
}

static bool checker_is_byte_buffer(zan_type_t *t) {
    if (!t || t->kind != TYPE_ARRAY || !t->element_type) return false;
    return t->element_type->kind == TYPE_BYTE ||
           t->element_type->kind == TYPE_SBYTE ||
           t->element_type->kind == TYPE_CHAR;
}

/* 内部辅助实现 */

/* 内部辅助逻辑 */
static bool checker_type_assignable(zan_type_t *target, zan_type_t *value) {
    if (!target || !value) return true;
    if (target == value) return true;
    if (target->kind == TYPE_ERROR || value->kind == TYPE_ERROR) return true;
    if (target->kind == TYPE_TYPE_PARAM || value->kind == TYPE_TYPE_PARAM)
        return true;
    if (target->kind == value->kind && checker_type_equal(target, value))
        return true;

    /* 内部辅助实现 */
    if (target->kind == TYPE_CLASS && target->name.len == 7 &&
        memcmp(target->name.str, "Binding", 7) == 0 &&
        target->type_arg_count == 1 && target->type_args[0]) {
        if (value->kind == TYPE_OBJECT) return true;
        return checker_type_assignable(target->type_args[0], value);
    }

    /* Nullable value types accept null and the underlying non-nullable value */
    if (target->kind == TYPE_NULLABLE && value->kind == TYPE_OBJECT)
        return true; /* null -> T? */
    if (target->kind == TYPE_NULLABLE && value->kind != TYPE_NULLABLE &&
        value->kind != TYPE_OBJECT && target->element_type)
        return checker_type_assignable(target->element_type, value); /* T -> T? */

    /* 内部辅助实现 */
    if (target->kind == TYPE_DELEGATE)
        return value->kind == TYPE_OBJECT ||
               (value->kind == TYPE_DELEGATE && checker_type_equal(target, value));

    /* 内部辅助实现 */
    if (target->kind == TYPE_TASK && value->kind == TYPE_TASK)
        return value->type_arg_count >= target->type_arg_count;
    bool tnum = type_is_numeric(target), vnum = type_is_numeric(value);
    /* 内部辅助逻辑 */
    if (target->kind == TYPE_ENUM)
        return value->kind == TYPE_ENUM || vnum || value->kind == TYPE_CHAR;
    if (value->kind == TYPE_ENUM)
        return tnum || target->kind == TYPE_CHAR;
    if (tnum && (vnum || value->kind == TYPE_CHAR)) return true;
    if (target->kind == TYPE_CHAR && (vnum || value->kind == TYPE_CHAR))
        return true;

    /* 内部辅助实现 */
    if (value->kind == TYPE_OBJECT) {
        return checker_type_is_ref(target); /* null literal */
    }
    switch (target->kind) {
    case TYPE_STRING:
        return value->kind == TYPE_STRING || checker_is_byte_buffer(value);
    case TYPE_CLASS:
        if (value->kind == TYPE_CLASS) {
            /* 内部辅助实现 */
            if (target->sym && value->sym && target->sym == value->sym &&
                (target->type_arg_count == 0 || value->type_arg_count == 0))
                return true;
            /* 内部辅助实现 */
            if (same_generic_container(target, value) &&
                !checker_type_equal(target, value))
                return true;
            return checker_type_derives_from(value, target);
        }
        return false;
    case TYPE_INTERFACE:
        if (value->kind == TYPE_INTERFACE || value->kind == TYPE_CLASS ||
            value->kind == TYPE_STRUCT) {
            if (same_generic_container(target, value) &&
                !checker_type_equal(target, value))
                return true;
            return checker_type_derives_from(value, target);
        }
        return false;
    case TYPE_ARRAY:
        return value->kind == TYPE_ARRAY &&
               checker_type_equal(target->element_type, value->element_type);
    case TYPE_OBJECT:
        return checker_type_is_ref(value);
    default:
        return false;
    }

    return false;
}

static bool integral_range(zan_type_t *t, int64_t *min_value,
                           uint64_t *max_value, bool *is_unsigned) {
    if (!t) return false;
    *is_unsigned = false;
    switch (t->kind) {
    case TYPE_SBYTE:  *min_value = -128; *max_value = 127; return true;
    case TYPE_BYTE:   *min_value = 0; *max_value = 255; *is_unsigned = true; return true;
    case TYPE_SHORT:  *min_value = -32768; *max_value = 32767; return true;
    case TYPE_USHORT: *min_value = 0; *max_value = 65535; *is_unsigned = true; return true;
    case TYPE_CHAR:   *min_value = 0; *max_value = 65535; *is_unsigned = true; return true;
    case TYPE_INT:    *min_value = INT32_MIN; *max_value = INT32_MAX; return true;
    case TYPE_UINT:   *min_value = 0; *max_value = UINT32_MAX; *is_unsigned = true; return true;
    case TYPE_LONG:
    case TYPE_NINT:   *min_value = INT64_MIN; *max_value = INT64_MAX; return true;
    case TYPE_ULONG:  *min_value = 0; *max_value = UINT64_MAX; *is_unsigned = true; return true;
    case TYPE_ENUM:   *min_value = INT32_MIN; *max_value = INT32_MAX; return true;
    default: return false;
    }
}

static bool const_integral_value(zan_ast_node_t *expr, int64_t *value) {
    if (!expr) return false;
    if (expr->kind == AST_INT_LITERAL) {
        *value = expr->int_val;
        return true;
    }
    if (expr->kind == AST_CHAR_LITERAL) {
        *value = expr->int_val;
        return true;
    }
    if (expr->kind == AST_UNARY && expr->unary.op == TK_MINUS &&
        expr->unary.operand && expr->unary.operand->kind == AST_INT_LITERAL) {
        /* 内部辅助实现 */
        *value = (int64_t)(0ULL - (uint64_t)expr->unary.operand->int_val);
        return true;
    }
    return false;
}

/* 内部辅助实现 */
static bool mixed_sign_compare_error(zan_binder_t *b, zan_diag_t *diag,
                                     zan_ast_node_t *expr,
                                     zan_type_t *left, zan_type_t *right) {
    bool lu = left->kind == TYPE_ULONG, ru = right->kind == TYPE_ULONG;
    if (!lu && !ru) return false;
    zan_type_t *s = lu ? right : left;
    if (s->kind != TYPE_LONG && s->kind != TYPE_INT &&
        s->kind != TYPE_SHORT && s->kind != TYPE_SBYTE &&
        s->kind != TYPE_NINT)
        return false;
    zan_ast_node_t *se = lu ? expr->binary.right : expr->binary.left;
    int64_t constant = 0;
    if (const_integral_value(se, &constant) && constant >= 0) return false;
    zan_diag_emit(diag, DIAG_ERROR, expr->loc,
                  "operator cannot be applied to operands of type '%s' and "
                  "'%s': mixed signed/unsigned comparison needs an explicit "
                  "cast (C# rules)",
                  type_name(left), type_name(right));
    (void)b;
    return true;
}

static bool integral_conversion_is_safe(zan_type_t *target, zan_type_t *value,
                                        zan_ast_node_t *expr) {
    int64_t tmin, vmin, constant;
    uint64_t tmax, vmax;
    bool tu, vu;
    if (!integral_range(target, &tmin, &tmax, &tu) ||
        !integral_range(value, &vmin, &vmax, &vu))
        return true;
    if ((!vu || tu) && vmin >= tmin && vmax <= tmax) return true;
    if (!const_integral_value(expr, &constant)) return false;
    /* 内部辅助实现 */
    if (vu && !tu) {
        /* 内部辅助实现 */
        if (expr && expr->kind == AST_UNARY && expr->unary.op == TK_MINUS &&
            expr->unary.operand && expr->unary.operand->kind == AST_INT_LITERAL &&
            (uint64_t)expr->unary.operand->int_val == 0x8000000000000000ULL)
            return constant >= tmin;
        if (constant < 0) return false;
        if (expr && expr->kind == AST_INT_LITERAL)
            return (uint64_t)expr->int_val <= (uint64_t)tmax;
        return (uint64_t)constant <= (uint64_t)tmax;
    }
    if (constant < tmin) return false;
    if (tu) {
        if (constant < 0) return false;
        return (uint64_t)constant <= tmax;
    }
    return constant <= (int64_t)tmax;
}

/* 发射the diagnostic when `value` is not assignable to `target` */
/* 内部辅助逻辑 */
static bool checker_conversion_method(zan_checker_t *c, zan_symbol_t *sym,
                                      zan_type_t *from, zan_type_t *to,
                                      const char *op_name) {
    if (!sym) return false;
    zan_istr_t op = { (char *)op_name, (int)strlen(op_name) };
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (!m || m->kind != SYM_METHOD || !(m->modifiers & MOD_STATIC)) continue;
        if (!m->decl || m->decl->kind != AST_METHOD_DECL) continue;
        if (m->name.len != op.len ||
            memcmp(m->name.str, op.str, (size_t)op.len) != 0) continue;
        zan_type_t *rt = zan_binder_resolve_type(c->binder,
            m->decl->method_decl.return_type);
        if (!rt || !checker_type_equal(rt, to)) continue;
        zan_ast_list_t *ps = &m->decl->method_decl.params;
        if (ps->count != 1 || !ps->items[0] || ps->items[0]->kind != AST_PARAM)
            continue;
        zan_type_t *pt = zan_binder_resolve_type(c->binder,
            ps->items[0]->param.type);
        if (pt && checker_type_equal(pt, from)) return true;
    }
    return false;
}

static bool checker_user_conversion(zan_checker_t *c, zan_type_t *target,
                                    zan_type_t *value, const char *op_name) {
    if (!value || !target) return false;
    if (value->kind == TYPE_CLASS || value->kind == TYPE_STRUCT) {
        if (checker_conversion_method(c, value->sym, value, target, op_name))
            return true;
    }
    if (target->kind == TYPE_CLASS || target->kind == TYPE_STRUCT) {
        if (checker_conversion_method(c, target->sym, value, target, op_name))
            return true;
    }
    return false;
}

static bool checker_has_user_conversion(zan_checker_t *c, zan_type_t *target,
                                        zan_type_t *value) {
    return checker_user_conversion(c, target, value, "op_implicit");
}

/* 内部辅助逻辑 */
static bool cast_is_scalar(zan_type_t *t) {
    return t && (type_is_numeric(t) || t->kind == TYPE_CHAR ||
                 t->kind == TYPE_ENUM || t->kind == TYPE_NINT);
}

/* 检查是否`(dst)src` is a conversion the language has */
static bool checker_cast_is_valid(zan_checker_t *c, zan_type_t *dst,
                                  zan_type_t *src) {
    if (!dst || !src) return true;
    if (dst == c->binder->type_error || src == c->binder->type_error ||
        src == c->binder->type_void)
        return true;
    if (dst->kind == TYPE_TYPE_PARAM || src->kind == TYPE_TYPE_PARAM) return true;
    /* object is the boxed carrier (and the null literal's type). */
    if (dst->kind == TYPE_OBJECT || src->kind == TYPE_OBJECT) return true;
    /* 内部辅助实现 */
    if (dst->kind == TYPE_NINT || src->kind == TYPE_NINT) return true;
    if (dst->kind == TYPE_DELEGATE || src->kind == TYPE_DELEGATE) return true;
    if (checker_type_equal(dst, src)) return true;
    if (dst->kind == TYPE_NULLABLE || src->kind == TYPE_NULLABLE)
        return checker_cast_is_valid(c, checker_nullable_base(dst),
                                     checker_nullable_base(src));
    if (cast_is_scalar(dst) && cast_is_scalar(src)) return true;
    /* 内部辅助实现 */
    if (checker_type_is_ref(dst) && checker_type_is_ref(src)) return true;
    if (checker_user_conversion(c, dst, src, "op_explicit")) return true;
    if (checker_user_conversion(c, dst, src, "op_implicit")) return true;
    return checker_type_assignable(dst, src);
}

static zan_symbol_t *expr_method_group(zan_checker_t *c, zan_ast_node_t *e);
static void reject_method_group(zan_checker_t *c, zan_ast_node_t *e);

static void checker_check_assignable(zan_checker_t *c, zan_type_t *target,
                                     zan_type_t *value, zan_ast_node_t *expr,
                                     zan_loc_t loc, const char *what) {
    if (!target || !value) return;
    if (expr && target->kind != TYPE_DELEGATE) {
        zan_symbol_t *mg = expr_method_group(c, expr);
        if (mg) {
            reject_method_group(c, expr);
            return;
        }
    }
    if (target == c->binder->type_error || value == c->binder->type_error)
        return;
    /* A void call used as a value is invalid; reject with diagnostic */
    if (value == c->binder->type_void && target != c->binder->type_void) {
        zan_diag_emit(c->diag, DIAG_ERROR, loc,
                      "cannot convert 'void' to '%s' in %s: no implicit "
                      "conversion",
                      type_name(target), what);
        return;
    }
    bool target_integral = type_is_integral(target) || target->kind == TYPE_CHAR ||
                           target->kind == TYPE_ENUM;
    bool value_integral = type_is_integral(value) || value->kind == TYPE_CHAR ||
                          value->kind == TYPE_ENUM;
    if (target_integral &&
        (value->kind == TYPE_FLOAT || value->kind == TYPE_DOUBLE)) {
        zan_diag_emit(c->diag, DIAG_ERROR, loc,
                      "narrowing conversion from '%s' to '%s' in %s needs an "
                      "explicit cast (C# rules)",
                      type_name(value), type_name(target), what);
        return;
    }
    if (target_integral && value_integral) {
        int64_t constant = 0;
        bool has_constant = const_integral_value(expr, &constant);
        bool native_handle_narrowing = value->kind == TYPE_NINT &&
            target->kind != TYPE_LONG && target->kind != TYPE_NINT;
        /* 内部辅助实现 */
        bool decimal_radix = expr && expr->kind == AST_INT_LITERAL &&
            expr->lit_radix == 10;
        bool uint32_bit_pattern = value->kind == TYPE_LONG &&
            target->kind == TYPE_INT && has_constant && constant >= 0 &&
            (uint64_t)constant <= UINT32_MAX && !decimal_radix;
        /* 内部辅助实现 */
        bool ulong_literal = target->kind == TYPE_ULONG && expr &&
            expr->kind == AST_INT_LITERAL;
        if (native_handle_narrowing ||
            (!uint32_bit_pattern && !ulong_literal && has_constant &&
             !integral_conversion_is_safe(target, value, expr))) {
            zan_diag_emit(c->diag, DIAG_ERROR, loc,
                          "narrowing conversion from '%s' to '%s' in %s needs an "
                          "explicit cast (C# rules)",
                          type_name(value), type_name(target), what);
            return;
        }
    }
    if (checker_type_assignable(target, value)) return;
    if (checker_has_user_conversion(c, target, value)) return;
    zan_diag_emit(c->diag, DIAG_ERROR, loc,
                  "cannot convert '%s' to '%s' in %s: no implicit conversion",
                  type_name(value), type_name(target), what);
}

/* 内部辅助逻辑 */
static zan_type_t *check_member_access(zan_checker_t *c, zan_ast_node_t *expr,
                                       zan_type_t *obj_type, bool in_call_callee);
static void checker_reject_null_receiver(zan_checker_t *c, zan_ast_node_t *expr);

/* How many arguments `m` accepts: [min */
static bool method_arity(zan_symbol_t *m, int *min, int *max) {
    if (!m->decl || m->decl->kind != AST_METHOD_DECL) return false;
    /* a [DllImport( */
    if (m->decl->method_decl.is_variadic) return false;
    zan_ast_list_t *ps = &m->decl->method_decl.params;
    int lo = 0;
    for (int i = 0; i < ps->count; i++) {
        zan_ast_node_t *p = ps->items[i];
        if (!p || p->kind != AST_PARAM) return false;
        if (p->param.is_params || p->param.is_this) return false;
        if (!p->param.default_val) lo++;
    }
    *min = lo;
    *max = ps->count;
    return true;
}

/* Same test irgen's resolve_overload applies (irgen */
static bool method_accepts_argc(zan_symbol_t *m, int argc) {
    int lo, hi;
    if (method_arity(m, &lo, &hi)) return argc >= lo && argc <= hi;
    if (!m->decl || m->decl->kind != AST_METHOD_DECL) return false;
    zan_ast_list_t *ps = &m->decl->method_decl.params;
    /* a Variadic DllImport takes every declared parameter and any tail */
    if (m->decl->method_decl.is_variadic) return argc >= ps->count;
    if (ps->count == 0) return false;
    zan_ast_node_t *last = ps->items[ps->count - 1];
    if (!last || last->kind != AST_PARAM || !last->param.is_params) return false;
    return argc >= ps->count - 1;
}

static bool method_is_params_tail(zan_symbol_t *m) {
    if (!m->decl || m->decl->kind != AST_METHOD_DECL) return false;
    zan_ast_list_t *ps = &m->decl->method_decl.params;
    if (ps->count == 0) return false;
    zan_ast_node_t *last = ps->items[ps->count - 1];
    return last && last->kind == AST_PARAM && last->param.is_params;
}

/* The same-named method a call with `argc` arguments can actually invoke */
static zan_symbol_t *checker_find_method_call(zan_symbol_t *type_sym,
                                               zan_istr_t name, int argc,
                                               int type_arg_count) {
    zan_symbol_t *fallback = NULL;
    for (zan_symbol_t *s = type_sym; s;
         s = (s->type && s->type->base_type) ? s->type->base_type->sym : NULL) {
        zan_symbol_t *variadic = NULL;
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            if (!m || m->kind != SYM_METHOD || m->name.len != name.len ||
                memcmp(m->name.str, name.str, (size_t)name.len) != 0)
                continue;
            if (!fallback) fallback = m;
            if (!method_accepts_argc(m, argc)) continue;
            int mtps = (m->decl && m->decl->kind == AST_METHOD_DECL)
                ? m->decl->method_decl.type_params.count : 0;
            if (type_arg_count > 0 && mtps != type_arg_count) continue;
            if (method_is_params_tail(m)) {
                if (!variadic) variadic = m;
                continue;
            }
            return m;
        }
        if (variadic) return variadic;
    }
    return fallback;
}

/* Match concrete argument types after checking each argument once */
static zan_symbol_t *checker_find_method_typed(zan_checker_t *c,
                                                zan_symbol_t *type_sym,
                                                zan_istr_t name,
                                                zan_ast_node_t *call,
                                                zan_type_t **args) {
    zan_symbol_t *best = NULL;
    int best_score = -1;
    int argc = call->call.args.count;
    int type_arg_count = call->call.type_args.count;
    for (zan_symbol_t *s = type_sym; s;
         s = (s->type && s->type->base_type) ? s->type->base_type->sym : NULL) {
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            if (!m || m->kind != SYM_METHOD || m->name.len != name.len ||
                memcmp(m->name.str, name.str, (size_t)name.len) != 0 ||
                !method_accepts_argc(m, argc)) continue;
            int mtps = (m->decl && m->decl->kind == AST_METHOD_DECL)
                ? m->decl->method_decl.type_params.count : 0;
            if (type_arg_count > 0 && mtps != type_arg_count) continue;
            zan_ast_list_t *ps = &m->decl->method_decl.params;
            int score = 0;
            bool compatible = true;
            for (int j = 0; j < argc && j < ps->count; j++) {
                zan_ast_node_t *arg = call->call.args.items[j];
                if (!arg || arg->kind == AST_NAMED_ARG ||
                    arg->kind == AST_REF_ARG) continue;
                zan_ast_node_t *param = ps->items[j];
                if (!param || param->kind != AST_PARAM) continue;
                zan_type_t *pt = zan_binder_resolve_type(c->binder, param->param.type);
                if (param->param.is_params && pt && pt->kind == TYPE_ARRAY)
                    pt = pt->element_type;
                zan_type_t *at = args[j];
                if (!pt || !at || pt->kind == TYPE_ERROR ||
                    at->kind == TYPE_ERROR || pt->kind == TYPE_TYPE_PARAM ||
                    at->kind == TYPE_TYPE_PARAM) continue;
                if (checker_type_equal(pt, at)) { score += 4; continue; }
                if (checker_type_assignable(pt, at)) { score += 1; continue; }
                if (checker_has_user_conversion(c, pt, at)) continue;
                compatible = false;
                break;
            }
            if (!compatible) continue;
            if (score > best_score ||
                (score == best_score && best &&
                 method_is_params_tail(best) && !method_is_params_tail(m))) {
                best = m;
                best_score = score;
            }
        }
    }
    return best ? best : checker_find_method_call(type_sym, name, argc, type_arg_count);
}

/* Reject a call that passes the wrong number of arguments */
static void check_call_arity(zan_checker_t *c, zan_ast_node_t *call,
                             zan_type_t *recv) {
    zan_ast_node_t *callee = call->call.callee;
    if (!callee || callee->kind != AST_MEMBER_ACCESS) return;
    if (!recv || !recv->sym) return;
    zan_istr_t name = callee->member.name;
    int argc = call->call.args.count;
    zan_compile_trace("arity? %.*s.%.*s argc=%d members=%d",
                    (int)recv->sym->name.len, recv->sym->name.str,
                    (int)name.len, name.str, argc, recv->sym->member_count);
    int candidates = 0, want_min = 0, want_max = 0;
    for (zan_symbol_t *s = recv->sym; s;
         s = (s->type && s->type->base_type) ? s->type->base_type->sym : NULL) {
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            int lo, hi;
            if (!m || m->kind != SYM_METHOD || m->name.len != name.len ||
                memcmp(m->name.str, name.str, (size_t)name.len) != 0)
                continue;
            if (!method_arity(m, &lo, &hi)) {
                /* 内部辅助逻辑 */
                if (candidates == 0 && m->decl->method_decl.is_variadic &&
                    argc < m->decl->method_decl.params.count) {
                    int floor = m->decl->method_decl.params.count;
                    zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                                  "'%.*s.%.*s' takes at least %d argument%s, "
                                  "but %d given",
                                  (int)recv->sym->name.len, recv->sym->name.str,
                                  (int)name.len, name.str, floor,
                                  floor == 1 ? "" : "s", argc);
                }
                return;
            }
            if (argc >= lo && argc <= hi) return;
            if (!candidates) { want_min = lo; want_max = hi; }
            candidates++;
        }
    }
    if (!candidates) return;
    if (candidates > 1) {
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "no overload of '%.*s.%.*s' takes %d argument%s",
                      (int)recv->sym->name.len, recv->sym->name.str,
                      (int)name.len, name.str, argc, argc == 1 ? "" : "s");
        return;
    }
    if (want_min == want_max) {
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "'%.*s.%.*s' takes %d argument%s, but %d given",
                      (int)recv->sym->name.len, recv->sym->name.str,
                      (int)name.len, name.str, want_min,
                      want_min == 1 ? "" : "s", argc);
        return;
    }
    zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                  "'%.*s.%.*s' takes %d to %d arguments, but %d given",
                  (int)recv->sym->name.len, recv->sym->name.str,
                  (int)name.len, name.str, want_min, want_max, argc);
}

/* 内部辅助逻辑 */
static bool type_is_scalar_register(zan_type_t *t) {
    return t && (type_is_numeric(t) || t->kind == TYPE_BOOL ||
                 t->kind == TYPE_CHAR || t->kind == TYPE_ENUM ||
                 t->kind == TYPE_NINT);
}

/* 检查是否an argument of type `value` cannot reach a parameter of type `target` */
/* 内部辅助实现 */
static bool type_refs_type_param_d(zan_type_t *t, int depth) {
    if (!t || depth > 4) return false;
    if (t->kind == TYPE_TYPE_PARAM) return true;
    if (t->element_type && type_refs_type_param_d(t->element_type, depth + 1))
        return true;
    if (t->delegate_ret_type && type_refs_type_param_d(t->delegate_ret_type, depth + 1))
        return true;
    for (int i = 0; i < t->type_arg_count && t->type_args; i++)
        if (type_refs_type_param_d(t->type_args[i], depth + 1)) return true;
    return false;
}
static bool type_refs_type_param(zan_type_t *t) { return type_refs_type_param_d(t, 0); }

static bool checker_arg_type_mismatch(zan_checker_t *c, zan_type_t *target,
                                      zan_type_t *value) {
    if (!target || !value) return false;
    if (target->kind == TYPE_ERROR || value->kind == TYPE_ERROR) return false;
    /* 内部辅助实现 */
    if (type_refs_type_param(target) || type_refs_type_param(value) ||
        (target->type_arg_count && value->type_arg_count &&
         same_generic_container(target, value) &&
         !checker_type_equal(target, value))) return false;
    if (type_is_scalar_register(value)) {
        bool ref_target = target->kind == TYPE_OBJECT ||
                          target->kind == TYPE_INTERFACE ||
                          target->kind == TYPE_DELEGATE ||
                          target->kind == TYPE_ARRAY;
        if (!ref_target) return false;
        if (checker_type_assignable(target, value)) return false;
        return !checker_has_user_conversion(c, target, value);
    }
    bool ref_target = checker_type_is_ref(target) || target->kind == TYPE_STRUCT;
    bool ref_value = checker_type_is_ref(value) || value->kind == TYPE_STRUCT;
    if (ref_target && ref_value) {
        if (checker_type_assignable(target, value)) return false;
        return !checker_has_user_conversion(c, target, value);
    }
    return false;
}

/* 内部辅助实现 */
static zan_symbol_t *unique_named_method(zan_symbol_t *type_sym,
                                         zan_istr_t name) {
    zan_symbol_t *only = NULL;
    for (zan_symbol_t *s = type_sym; s;
         s = (s->type && s->type->base_type) ? s->type->base_type->sym : NULL) {
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            if (!m || m->kind != SYM_METHOD || m->name.len != name.len ||
                memcmp(m->name.str, name.str, (size_t)name.len) != 0)
                continue;
            if (only && only != m) return NULL;
            only = m;
        }
    }
    return only;
}

/* 内部辅助实现 */
static zan_symbol_t *call_arg_signature(zan_checker_t *c, zan_ast_node_t *call,
                                        zan_type_t *recv) {
    zan_ast_node_t *callee = call->call.callee;
    if (!callee) return NULL;
    zan_symbol_t *only = NULL;
    if (callee->kind == AST_MEMBER_ACCESS) {
        if (!recv || !recv->sym || recv->type_arg_count) return NULL;
        only = unique_named_method(recv->sym, callee->member.name);
    } else if (callee->kind == AST_IDENTIFIER) {
        if (!c->current_type_sym) return NULL;
        if (c->current_type_sym->type &&
            c->current_type_sym->type->type_arg_count) return NULL;
        /* 内部辅助逻辑 */
        zan_symbol_t *shadow = zan_binder_lookup(c->binder, callee->ident.name);
        if (shadow && shadow->kind != SYM_METHOD) return NULL;
        only = unique_named_method(c->current_type_sym, callee->ident.name);
    } else {
        return NULL;
    }
    if (!only || !only->decl || only->decl->kind != AST_METHOD_DECL) return NULL;
    if (only->decl->method_decl.type_params.count) return NULL;
    zan_ast_list_t *ps = &only->decl->method_decl.params;
    if (call->call.args.count > ps->count) return NULL;
    for (int i = 0; i < ps->count; i++) {
        zan_ast_node_t *p = ps->items[i];
        if (!p || p->kind != AST_PARAM || p->param.is_params || p->param.is_this)
            return NULL;
    }
    for (int i = 0; i < call->call.args.count; i++) {
        zan_ast_node_t *a = call->call.args.items[i];
        if (!a || a->kind == AST_NAMED_ARG || a->kind == AST_REF_ARG) return NULL;
    }
    return only;
}

/* True for `Task */
static bool arg_is_spawn_callee(zan_ast_node_t *call) {
    if (!call || call->call.callee == NULL ||
        call->call.callee->kind != AST_MEMBER_ACCESS ||
        call->call.args.count != 1) return false;
    zan_istr_t n = call->call.callee->member.name;
    if (n.len == 5 && memcmp(n.str, "Spawn", 5) == 0) return true;
    if (n.len == 3 && memcmp(n.str, "Run", 3) == 0) return true;
    return false;
}

static void check_call_arg_type(zan_checker_t *c, zan_symbol_t *sig, int index,
                                zan_ast_node_t *arg, zan_type_t *arg_type) {
    zan_ast_list_t *ps = &sig->decl->method_decl.params;
    if (index >= ps->count) return;
    zan_type_t *pt = zan_binder_resolve_type(c->binder,
                                             ps->items[index]->param.type);
    if (arg_type == c->binder->type_void) {
        if (!pt || pt->kind != TYPE_DELEGATE) {
            zan_diag_emit(c->diag, DIAG_ERROR, arg->loc,
                          "cannot use a 'void' value in argument %d of "
                          "'%.*s': a void call has no result",
                          index + 1, (int)sig->name.len, sig->name.str);
        }
        return;
    }
    if (!checker_arg_type_mismatch(c, pt, arg_type)) return;
    zan_diag_emit(c->diag, DIAG_ERROR, arg->loc,
                  "cannot convert '%s' to '%s' in argument %d of '%.*s': "
                  "no implicit conversion",
                  type_name(arg_type), type_name(pt), index + 1,
                  (int)sig->name.len, sig->name.str);
}

static void check_ctor_call_arguments(zan_checker_t *c, zan_type_t *type,
                                      zan_ast_node_t *expr, int argc,
                                      zan_type_t **arg_types) {
    if (!type || type->kind != TYPE_CLASS || !type->sym || argc <= 0) return;
    zan_symbol_t *sym = type->sym;
    int declared = 0;
    int arity_matches = 0;
    int mismatch_first_arg = -1;
    zan_type_t *mismatch_target = NULL;
    zan_type_t *mismatch_source = NULL;
    zan_ast_node_t *mismatch_node = NULL;

    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (!m || m->kind != SYM_CONSTRUCTOR || !m->decl) continue;
        if (m->decl->kind != AST_CONSTRUCTOR_DECL) continue;
        declared++;
        int lo = 0, hi = 0;
        ctor_arity(m->decl, &lo, &hi);
        if (argc < lo || (hi >= 0 && argc > hi)) continue;
        arity_matches++;

        zan_ast_list_t *ps = &m->decl->method_decl.params;
        bool match = true;
        int cand_bad_arg = -1;
        zan_type_t *cand_bad_tgt = NULL;
        zan_type_t *cand_bad_src = NULL;
        zan_ast_node_t *cand_bad_node = NULL;

        for (int j = 0; j < argc; j++) {
            zan_ast_node_t *arg_node = (j < expr->new_expr.args.count)
                ? expr->new_expr.args.items[j] : NULL;
            zan_type_t *at = arg_types[j];
            zan_type_t *pt = NULL;

            if (arg_node && arg_node->kind == AST_NAMED_ARG) {
                for (int q = 0; q < ps->count; q++) {
                    zan_ast_node_t *pp = ps->items[q];
                    if (pp && pp->kind == AST_PARAM &&
                        pp->param.name.len == arg_node->named_arg.name.len &&
                        memcmp(pp->param.name.str, arg_node->named_arg.name.str,
                               (size_t)arg_node->named_arg.name.len) == 0) {
                        pt = zan_binder_resolve_type(c->binder, pp->param.type);
                        break;
                    }
                }
            } else if (j < ps->count) {
                zan_ast_node_t *pp = ps->items[j];
                if (pp && pp->kind == AST_PARAM) {
                    pt = zan_binder_resolve_type(c->binder, pp->param.type);
                    if (pp->param.is_params && pt && pt->kind == TYPE_ARRAY)
                        pt = pt->element_type;
                }
            } else if (ps->count > 0) {
                zan_ast_node_t *pp = ps->items[ps->count - 1];
                if (pp && pp->kind == AST_PARAM && pp->param.is_params) {
                    pt = zan_binder_resolve_type(c->binder, pp->param.type);
                    if (pt && pt->kind == TYPE_ARRAY)
                        pt = pt->element_type;
                }
            }

            if (!pt || !at) continue;
            if (at == c->binder->type_void) {
                if (pt->kind != TYPE_DELEGATE) {
                    match = false;
                    if (cand_bad_arg < 0) {
                        cand_bad_arg = j;
                        cand_bad_tgt = pt;
                        cand_bad_src = at;
                        cand_bad_node = arg_node;
                    }
                }
                continue;
            }
            if (checker_arg_type_mismatch(c, pt, at)) {
                match = false;
                if (cand_bad_arg < 0) {
                    cand_bad_arg = j;
                    cand_bad_tgt = pt;
                    cand_bad_src = at;
                    cand_bad_node = arg_node;
                }
                break;
            }
        }

        if (match) {
            return;
        }

        if (arity_matches == 1) {
            mismatch_first_arg = cand_bad_arg;
            mismatch_target = cand_bad_tgt;
            mismatch_source = cand_bad_src;
            mismatch_node = cand_bad_node;
        }
    }

    if (declared == 0 || arity_matches == 0) {
        return;
    }

    if (arity_matches == 1 && mismatch_first_arg >= 0 && mismatch_target && mismatch_source) {
        zan_loc_t eloc = mismatch_node ? mismatch_node->loc : expr->loc;
        if (mismatch_source == c->binder->type_void) {
            zan_diag_emit(c->diag, DIAG_ERROR, eloc,
                          "cannot use a 'void' value in argument %d of '%s' "
                          "constructor: a void call has no result",
                          mismatch_first_arg + 1, type_name(type));
        } else {
            zan_diag_emit(c->diag, DIAG_ERROR, eloc,
                          "cannot convert '%s' to '%s' in argument %d of '%s' "
                          "constructor: no implicit conversion",
                          type_name(mismatch_source), type_name(mismatch_target),
                          mismatch_first_arg + 1, type_name(type));
        }
        return;
    }

    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                  "no overload of constructor '%s' matches the given argument types",
                  type_name(type));
}

/* Comparisons, which consume the *value* of both operands */
/* 内部辅助实现 */
static bool type_is_concatable(zan_type_t *t) {
    /* 内部辅助实现 */
    if (t->kind == TYPE_NULLABLE)
        return t->element_type && type_is_concatable(t->element_type);
    return t->kind == TYPE_STRING || t->kind == TYPE_CHAR ||
           t->kind == TYPE_ENUM ||
           t->kind == TYPE_BOOL || t->kind == TYPE_ERROR ||
           type_is_numeric(t);
}

static bool binary_op_compares(zan_token_kind_t op) {
    switch (op) {
    case TK_EQ_EQ: case TK_BANG_EQ:
    case TK_LESS: case TK_GREATER: case TK_LESS_EQ: case TK_GREATER_EQ:
        return true;
    default:
        return false;
    }
}

/* The method a value-position member access names, if any: `set */
static zan_symbol_t *expr_method_group(zan_checker_t *c, zan_ast_node_t *e) {
    if (!e || e->kind != AST_MEMBER_ACCESS) return NULL;
    zan_ast_node_t *obj = e->member.object;
    if (!obj || (obj->kind != AST_IDENTIFIER && obj->kind != AST_THIS_EXPR))
        return NULL;
    zan_type_t *ot = zan_checker_check_expr(c, obj);
    if (!ot || !ot->sym) return NULL;
    for (zan_symbol_t *s = ot->sym; s;
         s = (s->type && s->type->base_type) ? s->type->base_type->sym : NULL) {
        for (int i = s->member_count - 1; i >= 0; i--) {
            zan_symbol_t *m = s->members[i];
            if (!m) continue;
            if (m->name.len == e->member.name.len &&
                memcmp(m->name.str, e->member.name.str, (size_t)m->name.len) == 0)
                return m->kind == SYM_METHOD ? m : NULL;
        }
    }
    return NULL;
}

/* 内部辅助逻辑 */
static bool body_returns_this(zan_ast_node_t *n, int depth, int *count) {
    if (!n || depth > CHECKER_DERIVES_MAX_DEPTH) return true;
    switch (n->kind) {
    case AST_RETURN_STMT:
        if (n->ret.value && n->ret.value->kind == AST_THIS_EXPR) {
            (*count)++;
            return true;
        }
        if (n->ret.value && n->ret.value->kind == AST_NULL_LITERAL)
            return true;
        if (!n->ret.value) return true; /* bare `return;` (void) */
        return false;
    case AST_BLOCK:
        for (int i = 0; i < n->block.stmts.count; i++)
            if (!body_returns_this(n->block.stmts.items[i], depth + 1, count))
                return false;
        return true;
    case AST_IF_STMT:
        return body_returns_this(n->if_stmt.then_body, depth + 1, count) &&
               body_returns_this(n->if_stmt.else_body, depth + 1, count);
    case AST_WHILE_STMT: case AST_DO_WHILE_STMT:
        return body_returns_this(n->while_stmt.body, depth + 1, count);
    case AST_FOR_STMT:
        return body_returns_this(n->for_stmt.body, depth + 1, count);
    case AST_FOREACH_STMT:
        return body_returns_this(n->foreach_stmt.body, depth + 1, count);
    case AST_SWITCH_STMT: {
        for (int i = 0; i < n->switch_stmt.cases.count; i++) {
            zan_ast_node_t *sec = n->switch_stmt.cases.items[i];
            if (sec && !body_returns_this(sec->switch_case.body, depth + 1, count))
                return false;
        }
        return true;
    }
    case AST_TRY_STMT: {
        if (!body_returns_this(n->try_stmt.try_body, depth + 1, count))
            return false;
        for (int i = 0; i < n->try_stmt.catches.count; i++) {
            zan_ast_node_t *cat = n->try_stmt.catches.items[i];
            if (cat && !body_returns_this(cat->catch_clause.body, depth + 1, count))
                return false;
        }
        return body_returns_this(n->try_stmt.finally_body, depth + 1, count);
    }
    default:
        return true;
    }
}

/* 内部辅助实现 */
static bool expr_is_this_call(zan_checker_t *c, zan_symbol_t *method,
                              zan_type_t *recv) {
    if (!method || !method->decl) return false;
    zan_ast_node_t *m = method->decl;
    if (m->kind != AST_METHOD_DECL) return false;
    zan_ast_node_t *rt = m->method_decl.return_type;
    if (!rt) return false;
    zan_type_t *ret = zan_binder_resolve_type(c->binder, rt);
    if (!ret || !checker_type_is_ref(ret)) return false;
    if (!recv || recv == ret || !checker_type_derives_from(recv, ret))
        return false;
    int count = 0;
    if (!body_returns_this(m->method_decl.body, 0, &count)) return false;
    return count > 0;
}

static void reject_method_group(zan_checker_t *c, zan_ast_node_t *e) {
    zan_symbol_t *m = expr_method_group(c, e);
    if (!m) return;
    zan_diag_emit(c->diag, DIAG_ERROR, e->loc,
                  "'%.*s' is a method, not a value; call it as '%.*s()'",
                  (int)m->name.len, m->name.str,
                  (int)m->name.len, m->name.str);
}

static bool patterns_match_same(zan_ast_node_t *a, zan_ast_node_t *b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    while (a && a->kind == AST_CAST_EXPR) a = a->cast.expr;
    while (b && b->kind == AST_CAST_EXPR) b = b->cast.expr;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case AST_INT_LITERAL:
    case AST_CHAR_LITERAL:
        return a->int_val == b->int_val;
    case AST_STRING_LITERAL:
        return a->str_val.len == b->str_val.len &&
               memcmp(a->str_val.str, b->str_val.str, (size_t)a->str_val.len) == 0;
    case AST_BOOL_LITERAL:
        return a->bool_val == b->bool_val;
    case AST_NULL_LITERAL:
        return true;
    case AST_MEMBER_ACCESS:
        if (a->member.name.len != b->member.name.len ||
            memcmp(a->member.name.str, b->member.name.str, (size_t)a->member.name.len) != 0)
            return false;
        if (a->member.object && b->member.object &&
            a->member.object->kind == AST_IDENTIFIER &&
            b->member.object->kind == AST_IDENTIFIER) {
            return a->member.object->ident.name.len == b->member.object->ident.name.len &&
                   memcmp(a->member.object->ident.name.str,
                          b->member.object->ident.name.str,
                          (size_t)a->member.object->ident.name.len) == 0;
        }
        return false;
    case AST_IDENTIFIER:
        return a->ident.name.len == b->ident.name.len &&
               memcmp(a->ident.name.str, b->ident.name.str, (size_t)a->ident.name.len) == 0;
    default:
        return false;
    }
}

zan_type_t *zan_checker_check_expr(zan_checker_t *c, zan_ast_node_t *expr) {
    if (!expr) return c->binder->type_error;

    switch (expr->kind) {
    case AST_INT_LITERAL:
        /* 内部辅助实现 */
        switch (expr->lit_suffix) {
        case 1:
            return c->binder->type_long;
        case 2:
            if (expr->int_val < 0 || expr->int_val > 4294967295LL)
                return c->binder->type_ulong;
            return c->binder->type_uint;
        case 3:
            return c->binder->type_ulong;
        default:
            if ((expr->lit_radix == 2 || expr->lit_radix == 8
                 || expr->lit_radix == 16)
                && expr->int_val >= 0 && expr->int_val <= 4294967295LL) {
                return c->binder->type_int;
            }
            if (expr->int_val < -2147483648LL || expr->int_val > 2147483647LL)
                return c->binder->type_long;
            return c->binder->type_int;
        }
    case AST_FLOAT_LITERAL:
        return c->binder->type_double;
    case AST_STRING_LITERAL:
        return c->binder->type_string;
    case AST_CHAR_LITERAL:
        return c->binder->type_char;
    case AST_BOOL_LITERAL:
        return c->binder->type_bool;
    case AST_NULL_LITERAL:
        return c->binder->type_object;

    case AST_IDENTIFIER: {
        /* 内部辅助实现 */
        zan_type_t *lt = checker_find_local(c, expr->ident.name);
        if (lt) return lt;
        if (c->current_type_sym) {
            zan_symbol_t *fsym = checker_find_field(c->current_type_sym,
                                                    expr->ident.name);
            if (fsym) {
                return fsym->type ? fsym->type : c->binder->type_error;
            }
        }
        zan_symbol_t *sym = zan_binder_lookup(c->binder, expr->ident.name);
        if (!sym) {
            /* don't error — may be resolved in later phases */
            return c->binder->type_error;
        }
        return sym->type;
    }

    case AST_BINARY: {
        zan_type_t *left = zan_checker_check_expr(c, expr->binary.left);
        zan_type_t *right = zan_checker_check_expr(c, expr->binary.right);
        /* A method group as an operand is a forgotten '()' */
        if (binary_op_compares(expr->binary.op)) {
            if (right->kind != TYPE_DELEGATE)
                reject_method_group(c, expr->binary.left);
            if (left->kind != TYPE_DELEGATE)
                reject_method_group(c, expr->binary.right);
        }

        switch (expr->binary.op) {
        case TK_PLUS:
            /* string concatenation */
            if (left->kind == TYPE_STRING || right->kind == TYPE_STRING) {
                zan_type_t *other = left->kind == TYPE_STRING ? right : left;
                if (!type_is_concatable(other)) {
                    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                                  "cannot concatenate 'string' and '%s' -- convert the operand explicitly (e.g. call .ToStr()/read the field) instead of concatenating the reference",
                                  type_name(other));
                    return c->binder->type_error;
                }
                no_runtime_reject(c, expr->loc, "string concatenation");
                return c->binder->type_string;
            }
            /* fall through */
        case TK_MINUS: case TK_STAR: case TK_SLASH: case TK_PERCENT:
            /* A delegate operand reached the arithmetic path */
            if (left->kind == TYPE_DELEGATE || right->kind == TYPE_DELEGATE) {
                if (expr->binary.op == TK_PLUS || expr->binary.op == TK_MINUS)
                    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                                  "delegates are single-cast: '+='/'-=' combination is not supported -- assign directly (`d = h`) or subscribe on a multicast event type (`UiEvent`, `e += h`)");
                else
                    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                                  "cannot apply operator to '%s' and '%s'",
                                  left->kind == TYPE_ERROR ? "lambda"
                                                           : type_name(left),
                                  right->kind == TYPE_ERROR ? "lambda"
                                                            : type_name(right));
                return c->binder->type_error;
            }
            if (type_is_numeric(left) && type_is_numeric(right)) {
                return promote_numeric(c->binder, left, right);
            }
            /* 内部辅助逻辑 */
            if (left->kind == TYPE_NULLABLE || right->kind == TYPE_NULLABLE) {
                zan_type_t *lb = checker_nullable_base(left);
                zan_type_t *rb = checker_nullable_base(right);
                if (type_is_numeric(lb) && type_is_numeric(rb))
                    return left->kind == TYPE_NULLABLE ? left : right;
            }
            /* 内部辅助实现 */
            /* 内部辅助实现 */
            if ((type_is_numeric(left) || left->kind == TYPE_ENUM ||
                 left->kind == TYPE_CHAR) &&
                (type_is_numeric(right) || right->kind == TYPE_ENUM ||
                 right->kind == TYPE_CHAR)) {
                return promote_numeric(c->binder, left, right);
            }
            /* 内部辅助逻辑 */
            if (left->kind == TYPE_CLASS || left->kind == TYPE_STRUCT) {
                return left;
            }
            if (left->kind != TYPE_ERROR && right->kind != TYPE_ERROR) {
                zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                              "cannot apply operator to '%s' and '%s'",
                              type_name(left), type_name(right));
            }
            return c->binder->type_error;

        case TK_AMP: case TK_PIPE: case TK_CARET:
        case TK_LESS_LESS: case TK_GREATER_GREATER:
        case TK_GREATER_GREATER_GREATER:
            if ((type_is_integral(left) || left->kind == TYPE_CHAR ||
                 left->kind == TYPE_ENUM) &&
                (type_is_integral(right) || right->kind == TYPE_CHAR ||
                 right->kind == TYPE_ENUM)) {
                return promote_numeric(c->binder, left, right);
            }
            return c->binder->type_error;

        case TK_EQ_EQ: case TK_BANG_EQ: {
            bool ln = type_is_numeric(left) || left->kind == TYPE_CHAR ||
                      left->kind == TYPE_ENUM;
            bool rn = type_is_numeric(right) || right->kind == TYPE_CHAR ||
                      right->kind == TYPE_ENUM;
            if (ln && rn) {
                if (mixed_sign_compare_error(c->binder, c->diag, expr, left, right))
                    return c->binder->type_error;
                return c->binder->type_bool;
            }
            /* 内部辅助实现 */
            if (left->kind == TYPE_NULLABLE || right->kind == TYPE_NULLABLE) {
                zan_type_t *lb = checker_nullable_base(left);
                zan_type_t *rb = checker_nullable_base(right);
                if (left->kind == TYPE_OBJECT || right->kind == TYPE_OBJECT)
                    return c->binder->type_bool;
                bool l2 = type_is_numeric(lb) || lb->kind == TYPE_CHAR ||
                          lb->kind == TYPE_ENUM;
                bool r2 = type_is_numeric(rb) || rb->kind == TYPE_CHAR ||
                          rb->kind == TYPE_ENUM;
                if (l2 && r2) return c->binder->type_bool;
                if (lb->kind == TYPE_BOOL && rb->kind == TYPE_BOOL)
                    return c->binder->type_bool;
                if (checker_type_assignable(lb, rb) ||
                    checker_type_assignable(rb, lb))
                    return c->binder->type_bool;
            }
            /* 内部辅助逻辑 */
            if (left->kind == TYPE_STRUCT && right->kind == TYPE_STRUCT &&
                left->sym && left->sym == right->sym)
                return c->binder->type_bool;
            /* 内部辅助逻辑 */
            if (left->kind == TYPE_TYPE_PARAM && right->kind == TYPE_TYPE_PARAM &&
                checker_type_equal(left, right))
                return c->binder->type_bool;
            /* Comparing a generic type parameter against null or object (`T a == null`) */
            if ((left->kind == TYPE_TYPE_PARAM && (right->kind == TYPE_OBJECT || expr_is_null_literal(expr->binary.right))) ||
                (right->kind == TYPE_TYPE_PARAM && (left->kind == TYPE_OBJECT || expr_is_null_literal(expr->binary.left))))
                return c->binder->type_bool;
            /* 内部辅助逻辑 */
            if (checker_type_is_ref(left) && checker_type_is_ref(right) &&
                (left->kind == TYPE_OBJECT || right->kind == TYPE_OBJECT ||
                 checker_type_assignable(left, right) ||
                 checker_type_assignable(right, left)))
                return c->binder->type_bool;
            if (left->kind == TYPE_BOOL && right->kind == TYPE_BOOL)
                return c->binder->type_bool;
            if (left->kind != TYPE_ERROR && right->kind != TYPE_ERROR)
                zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                              "cannot compare '%s' and '%s'",
                              type_name(left), type_name(right));
            return c->binder->type_error;
        }
        case TK_LESS: case TK_GREATER:
        case TK_LESS_EQ: case TK_GREATER_EQ: {
            bool ln = type_is_numeric(left) || left->kind == TYPE_CHAR ||
                      left->kind == TYPE_ENUM;
            bool rn = type_is_numeric(right) || right->kind == TYPE_CHAR ||
                      right->kind == TYPE_ENUM;
            if (ln && rn) {
                if (mixed_sign_compare_error(c->binder, c->diag, expr, left, right))
                    return c->binder->type_error;
                return c->binder->type_bool;
            }
            /* 内部辅助逻辑 */
            if (left->kind == TYPE_NULLABLE || right->kind == TYPE_NULLABLE) {
                zan_type_t *lb = checker_nullable_base(left);
                zan_type_t *rb = checker_nullable_base(right);
                bool l2 = type_is_numeric(lb) || lb->kind == TYPE_CHAR ||
                          lb->kind == TYPE_ENUM;
                bool r2 = type_is_numeric(rb) || rb->kind == TYPE_CHAR ||
                          rb->kind == TYPE_ENUM;
                if (l2 && r2) return c->binder->type_bool;
            }
            /* 内部辅助逻辑 */
            if (left->kind == TYPE_TYPE_PARAM && right->kind == TYPE_TYPE_PARAM &&
                checker_type_equal(left, right))
                return c->binder->type_bool;
            /* 内部辅助实现 */
            if (left->kind == TYPE_STRING && right->kind == TYPE_STRING)
                return c->binder->type_bool;
            /* 内部辅助实现 */
            if ((left->kind == TYPE_CLASS || left->kind == TYPE_STRUCT) &&
                left->sym) {
                const char *rel_name = NULL;
                switch (expr->binary.op) {
                case TK_LESS:       rel_name = "op_lt"; break;
                case TK_GREATER:    rel_name = "op_gt"; break;
                case TK_LESS_EQ:    rel_name = "op_le"; break;
                case TK_GREATER_EQ: rel_name = "op_ge"; break;
                default: break;
                }
                if (rel_name) {
                    zan_istr_t rn = {(char *)rel_name, (int)strlen(rel_name)};
                    if (checker_find_method(left->sym, rn))
                        return c->binder->type_bool;
                }
            }
            if (left->kind != TYPE_ERROR && right->kind != TYPE_ERROR)
                zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                              "cannot compare '%s' and '%s' with a relational "
                              "operator: no implicit numeric conversion",
                              type_name(left), type_name(right));
            return c->binder->type_error;
        }

        case TK_AMP_AMP: case TK_PIPE_PIPE:
            if ((left->kind == TYPE_BOOL && right->kind == TYPE_BOOL) ||
                left->kind == TYPE_ERROR || right->kind == TYPE_ERROR)
                return c->binder->type_bool;
            zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                          "cannot apply logical operator to '%s' and '%s': "
                          "both operands must be bool",
                          type_name(left), type_name(right));
            return c->binder->type_error;

        case TK_QUESTION_QUESTION: {
            /* 内部辅助实现 */
            if (left->kind == TYPE_NULLABLE && left->element_type) {
                zan_type_t *merged = merge_conditional_types(c,
                                                              left->element_type,
                                                              right);
                if (merged) return merged;
            } else if (checker_type_is_ref(left) || checker_type_is_ref(right) ||
                       right->kind == TYPE_OBJECT ||
                       right->kind == TYPE_NULLABLE) {
                zan_type_t *merged = merge_conditional_types(c, left, right);
                if (merged) return merged;
            }
            if (left->kind != TYPE_ERROR && right->kind != TYPE_ERROR)
                zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                              "cannot apply '?' '?' to '%s' and '%s'",
                              type_name(left), type_name(right));
            return c->binder->type_error;
        }

        default:
            return c->binder->type_error;
        }
    }

    case AST_UNARY: {
        zan_type_t *operand = zan_checker_check_expr(c, expr->unary.operand);
        switch (expr->unary.op) {
        case TK_MINUS:
            if (type_is_numeric(operand)) return operand;
            return c->binder->type_error;
        case TK_BANG:
            return c->binder->type_bool;
        case TK_TILDE:
            if (type_is_integral(operand)) return operand;
            return c->binder->type_error;
        case TK_PLUS_PLUS: case TK_MINUS_MINUS:
            check_readonly_incdec(c, expr);
            if (type_is_numeric(operand)) return operand;
            return c->binder->type_error;
        default:
            return c->binder->type_error;
        }
    }

    case AST_POSTFIX_UNARY: {
        zan_type_t *operand = zan_checker_check_expr(c, expr->unary.operand);
        /* 内部辅助逻辑 */
        if (expr->unary.op == TK_BANG)
            return operand ? operand : c->binder->type_error;
        check_readonly_incdec(c, expr);
        if (type_is_numeric(operand)) return operand;
        return c->binder->type_error;
    }

    case AST_CALL: {
        /* nameof(expr) 编译期常量折叠：取标识符末段拼写为 string */
        if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER &&
            expr->call.callee->ident.name.len == 6 &&
            memcmp(expr->call.callee->ident.name.str, "nameof", 6) == 0 &&
            expr->call.args.count == 1) {
            zan_ast_node_t *na = expr->call.args.items[0];
            if (na && (na->kind == AST_IDENTIFIER ||
                       na->kind == AST_MEMBER_ACCESS)) {
                zan_checker_check_expr(c, na);
                return c->binder->type_string;
            }
            zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                "nameof requires a single identifier or member access");
            return c->binder->type_error;
        }
        /* Builtin scalar instance methods (string */
        zan_type_t *callee_type;
        zan_type_t *recv = NULL;
        /* 内部辅助实现 */
        int callee_is_method = 0;
        bool typed_overload_changed = false;
        zan_symbol_t *called_sym = NULL;
        int argc = expr->call.args.count;
        zan_type_t **arg_types = argc > 0 ? (zan_type_t **)zan_arena_alloc(
            c->arena, (size_t)argc * sizeof(zan_type_t *)) : NULL;
        /* 内部辅助实现 */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_IDENTIFIER &&
            expr->call.args.count == 2 &&
            expr->call.callee->member.name.len == 8 &&
            memcmp(expr->call.callee->member.name.str, "TryParse", 8) == 0) {
            zan_symbol_t *es = zan_binder_lookup(c->binder,
                expr->call.callee->member.object->ident.name);
            if (es && es->kind == SYM_ENUM) {
                for (int ai = 0; ai < 2; ai++)
                    zan_checker_check_expr(c, expr->call.args.items[ai]);
                return c->binder->type_bool;
            }
        }
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            recv = zan_checker_check_expr(c, expr->call.callee->member.object);
            if (recv && recv->sym) {
                zan_symbol_t *m = checker_find_method_call(
                    recv->sym, expr->call.callee->member.name,
                    expr->call.args.count, expr->call.type_args.count);
                if (m && m->kind == SYM_METHOD) {
                    /* 内部辅助逻辑 */
                    if (!access_member_allowed(c, m)) {
                        report_inaccessible(c, m, expr->call.callee->loc);
                        return c->binder->type_error;
                    }
                    checker_reject_null_receiver(c, expr->call.callee);
                    called_sym = m;
                    callee_type = m->type ? m->type : c->binder->type_error;
                    callee_is_method = 1;
                }
            }
            if (!callee_is_method) {
                callee_type = type_is_scalar_primitive(recv)
                    ? c->binder->type_error
                    : check_member_access(c, expr->call.callee, recv, true);
            }
        } else {
            callee_type = zan_checker_check_expr(c, expr->call.callee);
        }
        zan_symbol_t *arg_sig = call_arg_signature(c, expr, recv);
        for (int i = 0; i < expr->call.args.count; i++) {
            zan_ast_node_t *arg = expr->call.args.items[i];
            if (arg && arg->kind == AST_NAMED_ARG) arg = arg->named_arg.expr;
            zan_type_t *arg_type = zan_checker_check_expr(c, arg);
            arg_types[i] = arg_type;
            /* An async call passed as an argument is the spawn idiom (Task */
            if (!arg_sig && arg && arg_type == c->binder->type_void &&
                arg->kind != AST_REF_ARG && !arg_is_spawn_callee(expr)) {
                zan_diag_emit(c->diag, DIAG_ERROR, arg->loc,
                              "cannot use a 'void' value in argument %d: a "
                              "void call has no result",
                              i + 1);
            }
            if (arg_sig && arg)
                check_call_arg_type(c, arg_sig, i, arg, arg_type);
        }
        /* 内部辅助实现 */
        if (callee_is_method && recv && recv->sym) {
            zan_symbol_t *typed = checker_find_method_typed(c, recv->sym,
                expr->call.callee->member.name, expr, arg_types);
            if (typed && typed != called_sym) {
                if (!access_member_allowed(c, typed)) {
                    report_inaccessible(c, typed, expr->call.callee->loc);
                    return c->binder->type_error;
                }
                called_sym = typed;
                typed_overload_changed = true;
                callee_type = typed->type ? typed->type : c->binder->type_error;
            }
        }
        /* 内部辅助实现 */
        if (!called_sym && expr->call.callee &&
            expr->call.callee->kind == AST_IDENTIFIER)
            called_sym = zan_binder_lookup(c->binder, expr->call.callee->ident.name);
        /* 内部辅助逻辑 */
        if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER &&
            c->current_type_sym &&
            (!called_sym || called_sym->kind == SYM_METHOD)) {
            zan_symbol_t *resolved = checker_find_method_typed(c,
                c->current_type_sym, expr->call.callee->ident.name,
                expr, arg_types);
            if (resolved) called_sym = resolved;
        }
        c->last_call_node = expr;
        c->last_call_method = called_sym;
        check_call_arity(c, expr, recv);
        /* 内部辅助实现 */
        if (!callee_is_method && recv) {
            zan_type_t *br = builtin_call_result_type(c, recv,
                                                      expr->call.callee->member.name);
            if (br) return br;
        }
        if (!callee_is_method && callee_type &&
            callee_type->kind == TYPE_DELEGATE) {
            zan_type_t *ret = callee_type->delegate_ret_type
                ? callee_type->delegate_ret_type
                : c->binder->type_void;
            no_runtime_warn_arc_return(c, expr, ret);
            return ret;
        }
        /* 内部辅助逻辑 */
        if (callee_type && (callee_type->kind == TYPE_CLASS ||
                            callee_type->kind == TYPE_STRUCT) && callee_type->sym) {
            zan_istr_t op_name = {(char *)"op_call", 7};
            zan_symbol_t *op = checker_find_method(callee_type->sym, op_name);
            if (op && op->decl && op->decl->kind == AST_METHOD_DECL &&
                op->decl->method_decl.return_type) {
                zan_type_t *ret = zan_binder_resolve_type(c->binder,
                    op->decl->method_decl.return_type);
                no_runtime_warn_arc_return(c, expr, ret);
                return ret;
            }
            if (op && op->type) {
                no_runtime_warn_arc_return(c, expr, op->type);
                return op->type;
            }
        }
        /* 解析作用域内被调函数/方法的返回类型 */
        if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER) {
            zan_symbol_t *fsym = called_sym
                ? called_sym
                : zan_binder_lookup(c->binder, expr->call.callee->ident.name);
            if (fsym && (fsym->kind == SYM_METHOD) && fsym->decl) {
                zan_ast_node_t *m = fsym->decl;
                if (m->method_decl.return_type) {
                    zan_type_t *ret = zan_binder_resolve_type(
                        c->binder, m->method_decl.return_type);
                    no_runtime_warn_arc_return(c, expr, ret);
                    return ret;
                }
                return c->binder->type_void; /* explicit void return */
            }
        }
        /* 内部辅助逻辑 */
        if (called_sym && called_sym->decl &&
            called_sym->decl->kind == AST_METHOD_DECL &&
            expr->call.type_args.count > 0) {
            zan_ast_node_t *m = called_sym->decl;
            zan_ast_list_t *tps = &m->method_decl.type_params;
            if (tps->count == expr->call.type_args.count && m->method_decl.return_type) {
                zan_type_t *ret = zan_binder_resolve_type(c->binder, m->method_decl.return_type);
                zan_type_t *targs[8] = { NULL };
                int n = expr->call.type_args.count < 8 ? expr->call.type_args.count : 8;
                for (int i = 0; i < n; i++) {
                    targs[i] = zan_binder_resolve_type(c->binder, expr->call.type_args.items[i]);
                }
                ret = zan_binder_subst_named(c->binder, ret, tps, targs);
                no_runtime_warn_arc_return(c, expr, ret);
                return ret;
            }
        }

        /* 内部辅助实现 */
        if (typed_overload_changed && called_sym && called_sym->decl &&
            called_sym->decl->kind == AST_METHOD_DECL) {
            zan_ast_node_t *m = called_sym->decl;
            zan_type_t *ret = m->method_decl.return_type
                ? zan_binder_resolve_type(c->binder, m->method_decl.return_type)
                : c->binder->type_void;
            no_runtime_warn_arc_return(c, expr, ret);
            return ret;
        }
        /* Resolved call of a uniquely-named method (`obj */
        if (arg_sig && arg_sig->decl &&
            arg_sig->decl->kind == AST_METHOD_DECL) {
            if (arg_sig->decl->method_decl.return_type) {
                /* Fluent `return this` methods (`Control Gap( */
                if (expr_is_this_call(c, arg_sig, recv)) {
                    no_runtime_warn_arc_return(c, expr, recv);
                    return recv;
                }
                zan_type_t *ret = zan_binder_resolve_type(
                    c->binder, arg_sig->decl->method_decl.return_type);
                no_runtime_warn_arc_return(c, expr, ret);
                return ret;
            }
            no_runtime_warn_arc_return(c, expr, c->binder->type_void);
            return c->binder->type_void; /* explicit void return */
        }
        no_runtime_warn_arc_return(c, expr, callee_is_method ? callee_type : NULL);
        return c->binder->type_error;
    }

    case AST_STRING_INTERP: {
        no_runtime_reject(c, expr->loc, "string interpolation");
        for (int i = 0; i < expr->string_interp.parts.count; i++) {
            zan_type_t *pt = zan_checker_check_expr(c, expr->string_interp.parts.items[i]);
            /* 内部辅助实现 */
            if (!type_is_concatable(pt)) {
                zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                              "cannot interpolate '%s' -- convert the operand explicitly instead of formatting the reference",
                              type_name(pt));
            }
        }
        return c->binder->type_string;
    }

    case AST_MEMBER_ACCESS:
        return check_member_access(c, expr,
                                   zan_checker_check_expr(c, expr->member.object),
                                   false);

    case AST_INDEX: {
        zan_type_t *obj = zan_checker_check_expr(c, expr->index.object);
        zan_type_t *idx = zan_checker_check_expr(c, expr->index.index);
        /* 内部辅助逻辑 */
        if ((obj->kind == TYPE_CLASS || obj->kind == TYPE_STRUCT) && obj->sym) {
            zan_istr_t op_name = {(char *)"op_index", 8};
            /* 内部辅助实现 */
            zan_symbol_t *op = NULL;
            int op_count = 0;
            for (zan_symbol_t *ts = obj->sym; ts;
                 ts = (ts->type && ts->type->base_type)
                     ? ts->type->base_type->sym : NULL) {
                for (int mi = 0; mi < ts->member_count; mi++) {
                    zan_symbol_t *mm = ts->members[mi];
                    if (mm && mm->kind == SYM_METHOD &&
                        mm->name.len == op_name.len &&
                        memcmp(mm->name.str, op_name.str,
                               (size_t)op_name.len) == 0) {
                        if (!op) op = mm;
                        op_count++;
                    }
                }
            }
            if (op_count == 1 && op && op->decl &&
                op->decl->kind == AST_METHOD_DECL &&
                op->decl->method_decl.return_type) {
                /* 索引器形参与实参类型匹配校验 */
                zan_ast_list_t *ps = &op->decl->method_decl.params;
                /* 内部辅助实现 */
                bool is_static = (op->decl->method_decl.modifiers &
                                  MOD_STATIC) != 0;
                int self_off = is_static ? 1 : 0;
                int given = 1 + expr->index.extra.count;
                int want = ps->count - self_off;
                bool any_generic = false;
                for (int pi = self_off; pi < ps->count; pi++) {
                    zan_ast_node_t *pp = ps->items[pi];
                    if (pp && pp->kind == AST_PARAM && pp->param.type) {
                        zan_type_t *pt =
                            zan_binder_resolve_type(c->binder, pp->param.type);
                        if (pt && type_refs_type_param(pt)) any_generic = true;
                    }
                }
                if (!any_generic && ps->count >= self_off && want != given) {
                    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                        "'%.*s' indexer takes %d index%s, but %d given",
                        (int)obj->sym->name.len, obj->sym->name.str,
                        want, want == 1 ? "" : "es", given);
                    return c->binder->type_error;
                }
                if (!any_generic && idx && idx->kind != TYPE_ERROR &&
                    ps->count > self_off) {
                    zan_ast_node_t *p0 = ps->items[self_off];
                    if (p0 && p0->kind == AST_PARAM && p0->param.type) {
                        zan_type_t *pt0 =
                            zan_binder_resolve_type(c->binder, p0->param.type);
                        /* 内部辅助实现 */
                        if (pt0 && !type_refs_type_param(pt0) &&
                            !checker_type_assignable(pt0, idx)) {
                            zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                                "no '%.*s' indexer takes an index of type "
                                "'%.*s'",
                                (int)obj->sym->name.len, obj->sym->name.str,
                                (int)idx->name.len, idx->name.str);
                            return c->binder->type_error;
                        }
                    }
                }
                return zan_binder_resolve_type(c->binder,
                    op->decl->method_decl.return_type);
            }
            if (op && op_count > 0 && op->decl &&
                op->decl->kind == AST_METHOD_DECL &&
                op->decl->method_decl.return_type) {
                return zan_binder_resolve_type(c->binder,
                    op->decl->method_decl.return_type);
            }
            if (op && op->type) return op->type;
        }
        if (obj->kind == TYPE_ARRAY && obj->element_type) {
            /* 内部辅助逻辑 */
            int rank = obj->array_rank > 1 ? obj->array_rank : 1;
            int given = 1 + expr->index.extra.count;
            if (given != rank) {
                zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                    "rank-%d array indexed with %d indices", rank, given);
            }
            for (int i = 0; i < expr->index.extra.count; i++)
                zan_checker_check_expr(c, expr->index.extra.items[i]);
            return obj->element_type;
        }
        if (obj->kind == TYPE_STRING) {
            for (int i = 0; i < expr->index.extra.count; i++)
                zan_checker_check_expr(c, expr->index.extra.items[i]);
            return c->binder->type_char;
        }
        return c->binder->type_error;
    }

    case AST_ASSIGNMENT: {
        zan_type_t *left = zan_checker_check_expr(c, expr->binary.left);
        zan_type_t *right = zan_checker_check_expr(c, expr->binary.right);
        check_readonly_assignment(c, expr);
        zan_type_t *target = checker_assignment_target_type(c, expr->binary.left);
        if (!target && expr->binary.left->kind == AST_INDEX) {
            target = checker_index_set_target(c, expr->binary.left, right);
            /* No op_index_set overload accepted the written index/value */
            if (!target) {
                zan_type_t *iobj = zan_checker_check_expr(
                    c, expr->binary.left->index.object);
                if (iobj && iobj->sym &&
                    (iobj->kind == TYPE_CLASS || iobj->kind == TYPE_STRUCT)) {
                    zan_istr_t set_name = {(char *)"op_index_set", 12};
                    bool has_set = false;
                    for (int mi = 0; mi < iobj->sym->member_count; mi++) {
                        zan_symbol_t *mm = iobj->sym->members[mi];
                        if (mm && mm->kind == SYM_METHOD &&
                            mm->name.len == set_name.len &&
                            memcmp(mm->name.str, set_name.str,
                                   (size_t)set_name.len) == 0) {
                            has_set = true;
                            break;
                        }
                    }
                    if (has_set) {
                        zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                            "no '%.*s' indexer accepts this assignment",
                            (int)iobj->sym->name.len, iobj->sym->name.str);
                    }
                }
            }
        }
        if (!target) target = left;
        if (target)
            checker_check_assignable(c, target, right, expr->binary.right,
                                     expr->loc, "assignment");
        check_closure_cycle_warning(c, expr->binary.left, expr->binary.right, expr->loc);
        return right;
    }

    case AST_NEW_EXPR: {
        no_runtime_reject(c, expr->loc, "allocation (`new`)");
        zan_type_t *type;
        if (expr->new_expr.call_init) {
            /* `FactoryCall( */
            type = zan_checker_check_expr(c, expr->new_expr.call_init);
        } else if (!expr->new_expr.type) {
            /* Anonymous object literal `new { a */
            for (int i = 0; i < expr->new_expr.args.count; i++) {
                zan_checker_check_expr(c, expr->new_expr.args.items[i]);
            }
            return c->binder->type_error;
        }
        /* 内部辅助实现 */
        bool factory_init = expr->new_expr.call_init != NULL;
        if (!factory_init) {
            type = zan_binder_resolve_type(c->binder, expr->new_expr.type);
            /* 内部辅助实现 */
            if (expr->new_expr.is_array && type && type->kind != TYPE_ERROR &&
                type->kind != TYPE_ARRAY)
                type = zan_binder_make_array_type(c->binder, type);
            check_generic_constraints(c, type, expr->loc);
            /* 内部辅助逻辑 */
            check_ctor_available(c, type, expr);
        }
        /* new List<T>(src) 单参数拷贝构造函数类型推导 */
        bool list_copy_candidate = !expr->new_expr.is_array && type &&
            type->kind == TYPE_CLASS && type->type_arg_count == 1 &&
            type->name.len == 4 && memcmp(type->name.str, "List", 4) == 0 &&
            expr->new_expr.args.count == 1 &&
            expr->new_expr.args.items[0] &&
            expr->new_expr.args.items[0]->kind != AST_ASSIGNMENT;
        expr->new_expr.list_copy = false;
        /* 内部辅助实现 */
        if (expr->new_expr.is_array && !expr->new_expr.array_init &&
            expr->new_expr.args.count > 0) {
            int rank = expr->new_expr.array_rank > 0
                ? expr->new_expr.array_rank : 1;
            int nd = rank > 16 ? 16 : rank;
            int ninit = expr->new_expr.args.count - nd;
            if (ninit > 0) {
                int64_t prod = 1;
                bool const_dims = true;
                for (int d = 0; d < nd && const_dims; d++) {
                    int64_t dv = 0;
                    if (!const_integral_value(expr->new_expr.args.items[d], &dv))
                        const_dims = false;
                    else
                        prod *= dv;
                }
                if (!const_dims)
                    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                                  "an array creation with an initializer "
                                  "requires constant dimensions");
                else if (prod != ninit)
                    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                                  "array initializer has %d element%s but the "
                                  "dimensions describe %lld",
                                  ninit, ninit == 1 ? "" : "s",
                                  (long long)prod);
            }
        }
        /* A postfix generic-type initializer (`List<int> { */
        int ctor_argc = (!factory_init && !expr->new_expr.is_array && type && type->sym)
            ? ctor_arg_count(c, expr, type->sym) : 0;
        zan_type_t *ctor_arg_types[256];
        if (ctor_argc > 256) ctor_argc = 256;
        for (int k = 0; k < ctor_argc; k++) ctor_arg_types[k] = NULL;

        zan_ast_list_t *init_lists[2] = { &expr->new_expr.args,
                                          &expr->new_expr.arg_inits };
        for (int li = 0; li < 2; li++) {
        zan_ast_list_t *init_args = init_lists[li];
        for (int i = 0; i < init_args->count; i++) {
            zan_ast_node_t *arg = init_args->items[i];
            /* 内部辅助实现 */
            if (arg && arg_is_initializer_entry(arg) && type && type->sym) {
                zan_istr_t mname = arg->kind == AST_COLL_INIT
                    ? arg->coll_init.name
                    : arg->binary.left->ident.name;
                zan_symbol_t *field = checker_find_field(type->sym, mname);
                /* 内部辅助实现 */
                if (!field && factory_init) {
                    zan_diag_emit(c->diag, DIAG_ERROR, arg->loc,
                                  "'%s' has no member '%.*s'",
                                  type_name(type), (int)mname.len, mname.str);
                    continue;
                }
                if (field) {
                    /* 内部辅助实现 */
                    if (property_is_readonly(field) &&
                        arg->kind != AST_COLL_INIT) {
                        zan_diag_emit(c->diag, DIAG_ERROR, arg->loc,
                                      "property '%.*s' has no setter and "
                                      "cannot be assigned",
                                      (int)field->name.len, field->name.str);
                    }
                }
                if (arg->kind == AST_COLL_INIT) {
                    /* 内部辅助逻辑 */
                    if (!field || !field->type) {
                        zan_diag_emit(c->diag, DIAG_ERROR, arg->loc,
                                      "'%.*s' has no member '%.*s'",
                                      (int)type->name.len, type->name.str,
                                      (int)mname.len, mname.str);
                    } else {
                        /* 内部辅助实现 */
                        zan_istr_t add_istr = {(char *)"Add", 3};
                        bool builtin_list = false;
                        zan_type_t *coll = field->type;
                        if (coll && ((coll->name.len == 4 &&
                                      memcmp(coll->name.str, "List", 4) == 0) ||
                                     (coll->name.len == 4 &&
                                      memcmp(coll->name.str, "Dict", 4) == 0)))
                            builtin_list = true;
                        zan_symbol_t *coll_sym =
                            coll && (coll->kind == TYPE_CLASS ||
                                     coll->kind == TYPE_STRUCT)
                                ? coll->sym : NULL;
                        zan_symbol_t *add = builtin_list
                            ? (zan_symbol_t *)1
                            : (coll_sym ? checker_find_method(coll_sym, add_istr)
                                        : NULL);
                        /* 内部辅助实现 */
                        bool dict_kv = coll && coll->name.len == 4 &&
                                       memcmp(coll->name.str, "Dict", 4) == 0;
                        zan_type_t *elem =
                            coll && coll->type_arg_count >= 1
                                ? coll->type_args[0]
                                : (coll && coll->kind == TYPE_ARRAY
                                   ? coll->element_type : NULL);
                        zan_type_t *elem2 = coll && coll->type_arg_count >= 2
                            ? coll->type_args[1] : NULL;
                        if (!add) {
                            zan_diag_emit(c->diag, DIAG_ERROR, arg->loc,
                                          "type '%s' has no Add method for a "
                                          "collection initializer",
                                          type_name(coll));
                        }
                        for (int k = 0; k < arg->coll_init.items.count; k++) {
                            zan_ast_node_t *item =
                                arg->coll_init.items.items[k];
                            zan_type_t *it = zan_checker_check_expr(c, item);
                            if (elem && it && add && !dict_kv)
                                checker_check_assignable(c, elem, it, item,
                                                         item->loc,
                                                         "collection initializer");
                            if (dict_kv && add) {
                                zan_ast_node_t *vnode =
                                    k + 1 < arg->coll_init.items.count
                                        ? arg->coll_init.items.items[k + 1]
                                        : NULL;
                                if (vnode) {
                                    zan_type_t *vt =
                                        zan_checker_check_expr(c, vnode);
                                    if (elem && it)
                                        checker_check_assignable(c, elem, it,
                                                                 item, item->loc,
                                                                 "collection initializer");
                                    if (elem2 && vt)
                                        checker_check_assignable(c, elem2, vt,
                                                                 vnode, vnode->loc,
                                                                 "collection initializer");
                                    k++;
                                } else if (elem && it) {
                                    checker_check_assignable(c, elem, it, item,
                                                             item->loc,
                                                             "collection initializer");
                                }
                            }
                        }
                    }
                    continue;
                }
                if (field && field->type) {
                    zan_type_t *right = zan_checker_check_expr(
                        c, arg->binary.right);
                    checker_check_assignable(c, field->type, right,
                                             arg->binary.right, arg->loc,
                                             "object initializer");
                    continue;
                }
            }
            zan_type_t *arg_type = zan_checker_check_expr(c, arg);
            if (li == 0 && i < ctor_argc) {
                ctor_arg_types[i] = arg_type;
            }
            if (list_copy_candidate &&
                arg_type && arg_type->kind == TYPE_CLASS &&
                arg_type->type_arg_count == 1 && arg_type->name.len == 4 &&
                memcmp(arg_type->name.str, "List", 4) == 0) {
                expr->new_expr.list_copy = true;
            }
        }
        }
        if (ctor_argc > 0) {
            check_ctor_call_arguments(c, type, expr, ctor_argc, ctor_arg_types);
        }
        return type;
    }

    case AST_CONDITIONAL: {
        zan_checker_check_expr(c, expr->conditional.cond);
        zan_type_t *then_type = zan_checker_check_expr(c, expr->conditional.then_expr);
        zan_type_t *else_type = zan_checker_check_expr(c, expr->conditional.else_expr);
        zan_type_t *merged = merge_conditional_types(c, then_type, else_type);
        if (!merged) {
            /* 三元条件表达式中引用类型与 null 字面量类型推导 */
            if (expr_is_null_literal(expr->conditional.else_expr) &&
                checker_type_is_ref(then_type))
                merged = then_type;
            else if (expr_is_null_literal(expr->conditional.then_expr) &&
                     checker_type_is_ref(else_type))
                merged = else_type;
        }
        if (!merged) {
            zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                "cannot apply '?:' operator to operands of type '%s' and '%s': "
                "neither converts to the other and they share no common type",
                type_name(then_type), type_name(else_type));
            return c->binder->type_error;
        }
        return merged;
    }

    case AST_LAMBDA:
        no_runtime_reject(c, expr->loc, "a lambda");
        for (int i = 0; i < expr->lambda.params.count; i++) {
            /* params type-checked later */
        }
        zan_checker_check_expr(c, expr->lambda.body);
        return c->binder->type_error; /* lambda type resolved in */

    case AST_THIS_EXPR:
        /* `this` is the type whose body is being checked */
        if (c->current_type_sym && c->current_type_sym->type) {
            return c->current_type_sym->type;
        }
        return c->binder->type_error;

    case AST_QUERY_EXPR: {
        /* 内部辅助实现 */
        zan_type_t *src_ty = zan_checker_check_expr(c, expr->query.source);
        zan_type_t *elem = (src_ty && src_ty->element_type)
            ? src_ty->element_type : NULL;
        if (!elem && src_ty && src_ty->type_args && src_ty->type_arg_count > 0)
            elem = src_ty->type_args[0];
        if (!elem) elem = c->binder->type_int;
        checker_add_local(c, expr->query.var, elem);
        for (int ci = 0; ci < expr->query.clauses.count; ci++) {
            zan_ast_node_t *cl = expr->query.clauses.items[ci];
            if (cl->kind == AST_QUERY_JOIN) {
                zan_type_t *jty = zan_checker_check_expr(
                    c, cl->query_clause.source);
                zan_type_t *je = (jty && jty->element_type)
                    ? jty->element_type : NULL;
                if (!je && jty && jty->type_args && jty->type_arg_count > 0)
                    je = jty->type_args[0];
                if (!je) je = c->binder->type_int;
                zan_checker_check_expr(c, cl->query_clause.left_key);
                checker_add_local(c, cl->query_clause.name, je);
                zan_checker_check_expr(c, cl->query_clause.right_key);
                if (cl->query_clause.into.len > 0)
                    checker_add_local(c, cl->query_clause.into,
                        zan_binder_make_list_type(c->binder, je));
            } else {
                zan_type_t *ct = zan_checker_check_expr(c,
                    cl->query_clause.expr);
                if (cl->kind == AST_QUERY_LET)
                    checker_add_local(c, cl->query_clause.name, ct);
            }
        }
        if (expr->query.group_expr) {
            zan_type_t *ge = zan_checker_check_expr(c, expr->query.group_expr);
            if (!ge) ge = elem;
            zan_checker_check_expr(c, expr->query.group_key);
            zan_type_t *grp = zan_binder_make_grouping_type(c->binder, ge);
            if (expr->query.group_into.len > 0) {
                checker_add_local(c, expr->query.group_into, grp);
                zan_type_t *sel =
                    zan_checker_check_expr(c, expr->query.select);
                if (!sel) sel = ge;
                return zan_binder_make_list_type(c->binder, sel);
            }
            return zan_binder_make_list_type(c->binder, grp);
        }
        zan_type_t *sel = zan_checker_check_expr(c, expr->query.select);
        if (!sel) sel = elem;
        return zan_binder_make_list_type(c->binder, sel);
    }

    case AST_TUPLE_EXPR: {
        /* 内部辅助实现 */
        int n = expr->tuple_expr.items.count;
        zan_type_t **elems = (zan_type_t **)zan_arena_alloc(
            c->binder->arena, sizeof(zan_type_t *) * (size_t)(n > 0 ? n : 1));
        for (int i = 0; i < n; i++) {
            elems[i] = zan_checker_check_expr(c, expr->tuple_expr.items.items[i]);
        }
        return zan_binder_make_tuple_type(c->binder, elems, n);
    }

    case AST_SWITCH_EXPR: {
        /* `expr switch { arm, */
        zan_type_t *disc_type = zan_checker_check_expr(c, expr->switch_expr.expr);
        zan_type_t *merged = NULL;
        bool has_result = false;
        bool seen_wildcard = false;

        for (int i = 0; i < expr->switch_expr.arms.count; i++) {
            zan_ast_node_t *arm = expr->switch_expr.arms.items[i];
            struct checker_local *saved_locals = c->locals;

            if (seen_wildcard) {
                zan_diag_emit(c->diag, DIAG_WARNING, arm->loc,
                              "unreachable switch arm: previous discard arm '_' matches all remaining values");
            }

            if (arm->switch_arm.pattern) {
                zan_checker_check_expr(c, arm->switch_arm.pattern);
                if (arm->switch_arm.when_cond == NULL) {
                    for (int prev = 0; prev < i; prev++) {
                        zan_ast_node_t *prev_arm = expr->switch_expr.arms.items[prev];
                        if (prev_arm->switch_arm.when_cond == NULL &&
                            prev_arm->switch_arm.pattern != NULL &&
                            patterns_match_same(arm->switch_arm.pattern, prev_arm->switch_arm.pattern)) {
                            zan_diag_emit(c->diag, DIAG_ERROR, arm->loc,
                                          "duplicate pattern in switch expression");
                            break;
                        }
                    }
                }
            } else if (arm->switch_arm.is_default) {
                for (int prev = 0; prev < i; prev++) {
                    zan_ast_node_t *prev_arm = expr->switch_expr.arms.items[prev];
                    if (prev_arm->switch_arm.is_default) {
                        zan_diag_emit(c->diag, DIAG_ERROR, arm->loc,
                                      "duplicate discard arm '_' in switch expression");
                        break;
                    }
                }
                seen_wildcard = true;
            }

            if (arm->switch_arm.type_pattern && arm->switch_arm.var_name.len > 0) {
                zan_type_t *pt = zan_binder_resolve_type(
                    c->binder, arm->switch_arm.type_pattern);
                checker_add_local(c, arm->switch_arm.var_name, pt);
                if (arm->switch_arm.when_cond == NULL &&
                    (pt && (pt->kind == TYPE_OBJECT || pt == disc_type))) {
                    seen_wildcard = true;
                }
            }
            if (arm->switch_arm.when_cond) {
                zan_checker_check_expr(c, arm->switch_arm.when_cond);
            }
            zan_type_t *rt = zan_checker_check_expr(c, arm->switch_arm.result);
            c->locals = saved_locals;
            if (rt && rt->kind != TYPE_ERROR) {
                if (!has_result) {
                    merged = rt;
                    has_result = true;
                } else {
                    zan_type_t *m2 = merge_conditional_types(c, merged, rt);
                    if (m2) merged = m2;
                }
            }
        }

        /* Enum exhaustiveness check: if disc_type is enum and no wildcard arm exists */
        if (disc_type && disc_type->kind == TYPE_ENUM && disc_type->sym && !seen_wildcard) {
            zan_symbol_t *esym = disc_type->sym;
            for (int mi = 0; mi < esym->member_count; mi++) {
                zan_symbol_t *em = esym->members[mi];
                if (em->kind != SYM_ENUM_MEMBER) continue;
                bool handled = false;
                for (int ai = 0; ai < expr->switch_expr.arms.count; ai++) {
                    zan_ast_node_t *arm = expr->switch_expr.arms.items[ai];
                    if (arm->switch_arm.pattern) {
                        zan_ast_node_t *pat = arm->switch_arm.pattern;
                        while (pat && pat->kind == AST_CAST_EXPR) pat = pat->cast.expr;
                        if (pat && pat->kind == AST_MEMBER_ACCESS &&
                            pat->member.name.len == em->name.len &&
                            memcmp(pat->member.name.str, em->name.str, em->name.len) == 0) {
                            handled = true;
                            break;
                        }
                        if (pat && pat->kind == AST_IDENTIFIER &&
                            pat->ident.name.len == em->name.len &&
                            memcmp(pat->ident.name.str, em->name.str, em->name.len) == 0) {
                            handled = true;
                            break;
                        }
                    }
                }
                if (!handled) {
                    zan_diag_emit(c->diag, DIAG_WARNING, expr->loc,
                                  "switch expression does not handle enum member '%.*s' of '%.*s'; add missing case or '_' discard arm",
                                  (int)em->name.len, em->name.str,
                                  (int)esym->name.len, esym->name.str);
                }
            }
        }
        return has_result ? merged : c->binder->type_error;
    }

    case AST_WITH_EXPR: {
        /* `recv with { field = value, */
        zan_type_t *rt = zan_checker_check_expr(c, expr->with_expr.expr);
        if (!rt || (rt->kind != TYPE_CLASS && rt->kind != TYPE_STRUCT)) {
            if (rt && rt->kind != TYPE_ERROR)
                zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                    "'with' requires a record (class/struct) receiver, "
                    "not '%s'", type_name(rt));
            return c->binder->type_error;
        }
        for (int i = 0; i < expr->with_expr.assigns.count; i++) {
            zan_ast_node_t *asg = expr->with_expr.assigns.items[i];
            if (!asg || asg->kind != AST_ASSIGNMENT) continue;
            zan_istr_t fname = asg->binary.left->ident.name;
            zan_symbol_t *field = rt->sym
                ? checker_find_field(rt->sym, fname) : NULL;
            if (!field || !field->type) {
                zan_diag_emit(c->diag, DIAG_ERROR, asg->loc,
                    "'%.*s' has no field '%.*s' for 'with'",
                    (int)rt->name.len, rt->name.str,
                    (int)fname.len, fname.str);
                zan_checker_check_expr(c, asg->binary.right);
                continue;
            }
            zan_type_t *val = zan_checker_check_expr(c, asg->binary.right);
            checker_check_assignable(c, field->type, val,
                                     asg->binary.right, asg->loc, "with");
        }
        return rt;
    }

    case AST_CAST_EXPR: {
        zan_type_t *src = zan_checker_check_expr(c, expr->cast.expr);
        zan_type_t *dst = zan_binder_resolve_type(c->binder, expr->cast.type);
        if (!dst) return c->binder->type_error;
        if (!checker_cast_is_valid(c, dst, src))
            zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                          "cannot convert '%s' to '%s': no such conversion",
                          type_name(src), type_name(dst));
        return dst;
    }

    case AST_AWAIT_EXPR:
        /* 内部辅助逻辑 */
        {
            zan_type_t *inner = zan_checker_check_expr(c, expr->await_expr.expr);
            if (inner && inner->kind == TYPE_TASK) {
                return inner->type_arg_count == 1 ? inner->type_args[0]
                                                  : c->binder->type_void;
            }
            /* 内部辅助逻辑 */
            return inner ? inner : c->binder->type_error;
        }

    case AST_BASE_EXPR:
        return c->binder->type_error; /* resolved in */

    default:
        return c->binder->type_error;
    }
}

/* ---- statement checking ---- */

void zan_checker_check_stmt(zan_checker_t *c, zan_ast_node_t *stmt) {
    if (!stmt) return;

    switch (stmt->kind) {
    case AST_BLOCK: {
        struct checker_local *saved_locals = c->locals;
        for (int i = 0; i < stmt->block.stmts.count; i++) {
            zan_checker_check_stmt(c, stmt->block.stmts.items[i]);
        }
        c->locals = saved_locals;
        break;
    }

    case AST_VAR_DECL: {
        zan_type_t *dt = stmt->var_decl.type
            ? zan_binder_resolve_type(c->binder, stmt->var_decl.type)
            : NULL;
        zan_type_t *iv = NULL;
        if (stmt->var_decl.initializer) {
            iv = zan_checker_check_expr(c, stmt->var_decl.initializer);
        }
        if (dt) {
            check_generic_constraints(c, dt, stmt->loc);
            if (iv) {
                checker_check_assignable(c, dt, iv, stmt->var_decl.initializer,
                                         stmt->loc, "initializer");
            }
            checker_add_local(c, stmt->var_decl.name, dt);
            checker_mark_local_null_src(c, stmt);
        } else {
            /* 内部辅助实现 */
            if (stmt->var_decl.initializer) {
                zan_symbol_t *mg = expr_method_group(c, stmt->var_decl.initializer);
                if (mg) {
                    reject_method_group(c, stmt->var_decl.initializer);
                }
            }
            checker_add_local(c, stmt->var_decl.name, iv);
            checker_mark_local_null_src(c, stmt);
        }
        break;
    }

    case AST_EXPR_STMT: {
        zan_symbol_t *mg = expr_method_group(c, stmt->expr_stmt.expr);
        if (mg) {
            reject_method_group(c, stmt->expr_stmt.expr);
        } else {
            zan_checker_check_expr(c, stmt->expr_stmt.expr);
        }
        break;
    }

    case AST_RETURN_STMT:
        if (stmt->ret.value) {
            zan_type_t *rv = zan_checker_check_expr(c, stmt->ret.value);
            /* 内部辅助实现 */
            if (c->current_return_type &&
                c->current_return_type->kind != TYPE_VOID)
                checker_check_assignable(c, c->current_return_type, rv,
                                         stmt->ret.value, stmt->loc,
                                         "return statement");
        }
        break;

    case AST_IF_STMT: {
        zan_type_t *cond_type = zan_checker_check_expr(c, stmt->if_stmt.cond);
        if (cond_type->kind != TYPE_BOOL && cond_type->kind != TYPE_ERROR) {
            zan_diag_emit(c->diag, DIAG_WARNING, stmt->if_stmt.cond->loc,
                          "condition should be bool, got '%s'", type_name(cond_type));
        }
        zan_checker_check_stmt(c, stmt->if_stmt.then_body);
        if (stmt->if_stmt.else_body) {
            zan_checker_check_stmt(c, stmt->if_stmt.else_body);
        }
        break;
    }

    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        zan_checker_check_expr(c, stmt->while_stmt.cond);
        zan_checker_check_stmt(c, stmt->while_stmt.body);
        break;

    case AST_FOR_STMT:
        if (stmt->for_stmt.init) zan_checker_check_stmt(c, stmt->for_stmt.init);
        if (stmt->for_stmt.cond) zan_checker_check_expr(c, stmt->for_stmt.cond);
        if (stmt->for_stmt.step) zan_checker_check_expr(c, stmt->for_stmt.step);
        zan_checker_check_stmt(c, stmt->for_stmt.body);
        break;

    case AST_FOREACH_STMT:
        /* iterating a managed collection walks rc-managed elements */
        no_runtime_reject(c, stmt->loc, "`foreach`");
        {
            zan_type_t *col = zan_checker_check_expr(c, stmt->foreach_stmt.collection);
            zan_type_t *elem = NULL;
            if (col) {
                if (col->kind == TYPE_ARRAY) elem = col->element_type;
                else if (col->kind == TYPE_CLASS && col->type_arg_count == 1)
                    elem = col->type_args[0];
            }
            if (stmt->foreach_stmt.var_type) {
                elem = zan_binder_resolve_type(c->binder,
                                               stmt->foreach_stmt.var_type);
            }
            checker_add_local(c, stmt->foreach_stmt.var_name, elem);
        }
        zan_checker_check_stmt(c, stmt->foreach_stmt.body);
        break;

    case AST_THROW_STMT:
        no_runtime_reject(c, stmt->loc, "`throw`");
        zan_checker_check_expr(c, stmt->throw_stmt.value);
        break;

    case AST_BREAK_STMT:
    case AST_CONTINUE_STMT:
    case AST_GOTO_STMT:
    case AST_LABEL_STMT:
        break;

    case AST_CHECKED_STMT:
        /* overflow-checking context wrapper: only the body needs checking */
        zan_checker_check_stmt(c, stmt->checked_stmt.body);
        break;

    case AST_LOCK_STMT:
        no_runtime_reject(c, stmt->loc, "`lock`");
        zan_checker_check_expr(c, stmt->lock_stmt.expr);
        zan_checker_check_stmt(c, stmt->lock_stmt.body);
        break;

    case AST_SWITCH_STMT: {
        zan_type_t *sw_type = zan_checker_check_expr(c, stmt->switch_stmt.expr);
        /* 内部辅助实现 */
        bool has_patterns = false;
        for (int i = 0; i < stmt->switch_stmt.cases.count && !has_patterns; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            if (sc->switch_case.type_pattern ||
                (sc->switch_case.pattern &&
                 sc->switch_case.pattern->kind == AST_NULL_LITERAL))
                has_patterns = true;
        }
        if (!has_patterns && !type_is_numeric(sw_type) &&
            sw_type->kind != TYPE_STRING && sw_type->kind != TYPE_CHAR &&
            sw_type->kind != TYPE_ENUM && sw_type->kind != TYPE_BOOL &&
            sw_type->kind != TYPE_ERROR) {
            zan_diag_emit(c->diag, DIAG_WARNING, stmt->loc,
                          "switch expression has non-switchable type '%s'", type_name(sw_type));
        }

        bool seen_wildcard = false;
        for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            bool is_default = (sc->switch_case.pattern == NULL &&
                               sc->switch_case.type_pattern == NULL &&
                               sc->switch_case.var_name.len == 0);

            if (seen_wildcard) {
                zan_diag_emit(c->diag, DIAG_WARNING, sc->loc,
                              "unreachable switch case: previous default/catch-all case matches all remaining values");
            }

            if (sc->switch_case.pattern) {
                zan_checker_check_expr(c, sc->switch_case.pattern);
                if (sc->switch_case.when_cond == NULL) {
                    for (int prev = 0; prev < i; prev++) {
                        zan_ast_node_t *prev_sc = stmt->switch_stmt.cases.items[prev];
                        if (prev_sc->switch_case.when_cond == NULL &&
                            prev_sc->switch_case.pattern != NULL &&
                            patterns_match_same(sc->switch_case.pattern, prev_sc->switch_case.pattern)) {
                            zan_diag_emit(c->diag, DIAG_ERROR, sc->loc,
                                          "duplicate case label in switch statement");
                            break;
                        }
                    }
                }
            } else if (is_default) {
                for (int prev = 0; prev < i; prev++) {
                    zan_ast_node_t *prev_sc = stmt->switch_stmt.cases.items[prev];
                    bool prev_is_default = (prev_sc->switch_case.pattern == NULL &&
                                            prev_sc->switch_case.type_pattern == NULL &&
                                            prev_sc->switch_case.var_name.len == 0);
                    if (prev_is_default) {
                        zan_diag_emit(c->diag, DIAG_ERROR, sc->loc,
                                      "duplicate default label in switch statement");
                        break;
                    }
                }
                seen_wildcard = true;
            }

            /* 模式匹配变量引入：case T x 将 x 绑定入守卫与代码块作用域 */
            if (sc->switch_case.type_pattern && sc->switch_case.var_name.len > 0) {
                zan_type_t *pt = zan_binder_resolve_type(
                    c->binder, sc->switch_case.type_pattern);
                checker_add_local(c, sc->switch_case.var_name, pt);
                if (sc->switch_case.when_cond == NULL &&
                    (pt->kind == TYPE_OBJECT || pt == sw_type)) {
                    seen_wildcard = true;
                }
            }
            if (sc->switch_case.when_cond) {
                zan_checker_check_expr(c, sc->switch_case.when_cond);
            }
            zan_checker_check_stmt(c, sc->switch_case.body);
        }
        break;
    }

    case AST_TRY_STMT:
        no_runtime_reject(c, stmt->loc, "`try`");
        zan_checker_check_stmt(c, stmt->try_stmt.try_body);
        for (int i = 0; i < stmt->try_stmt.catches.count; i++) {
            zan_ast_node_t *cc = stmt->try_stmt.catches.items[i];
            zan_checker_check_stmt(c, cc->catch_clause.body);
        }
        if (stmt->try_stmt.finally_body) {
            zan_checker_check_stmt(c, stmt->try_stmt.finally_body);
        }
        break;

    default:
        break;
    }
}

/* ---- check entire compilation unit ---- */

/* 内部辅助实现 */
/* 内部辅助实现 */
static bool expr_is_null_literal(zan_ast_node_t *e) {
    if (!e) return false;
    if (e->kind == AST_NULL_LITERAL) return true;
    if (e->kind == AST_CONDITIONAL)
        return expr_is_null_literal(e->conditional.then_expr) ||
               expr_is_null_literal(e->conditional.else_expr);
    return false;
}

static bool stmt_returns_null(zan_ast_node_t *n, int depth) {
    if (!n || depth > CHECKER_DERIVES_MAX_DEPTH) return false;
    switch (n->kind) {
    case AST_RETURN_STMT:
        return expr_is_null_literal(n->ret.value);
    case AST_BLOCK:
        for (int i = 0; i < n->block.stmts.count; i++)
            if (stmt_returns_null(n->block.stmts.items[i], depth + 1)) return true;
        return false;
    case AST_IF_STMT:
        return stmt_returns_null(n->if_stmt.then_body, depth + 1) ||
               stmt_returns_null(n->if_stmt.else_body, depth + 1);
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        return stmt_returns_null(n->while_stmt.body, depth + 1);
    case AST_FOR_STMT:
        return stmt_returns_null(n->for_stmt.body, depth + 1);
    case AST_FOREACH_STMT:
        return stmt_returns_null(n->foreach_stmt.body, depth + 1);
    case AST_LOCK_STMT:
        return stmt_returns_null(n->lock_stmt.body, depth + 1);
    case AST_TRY_STMT: {
        if (stmt_returns_null(n->try_stmt.try_body, depth + 1)) return true;
        for (int i = 0; i < n->try_stmt.catches.count; i++) {
            zan_ast_node_t *cc = n->try_stmt.catches.items[i];
            if (cc && stmt_returns_null(cc->catch_clause.body, depth + 1)) return true;
        }
        return stmt_returns_null(n->try_stmt.finally_body, depth + 1);
    }
    case AST_SWITCH_STMT:
        for (int i = 0; i < n->switch_stmt.cases.count; i++) {
            zan_ast_node_t *cs = n->switch_stmt.cases.items[i];
            if (cs && stmt_returns_null(cs->switch_case.body, depth + 1)) return true;
        }
        return false;
    default:
        return false;
    }
}

static bool method_can_return_null(zan_symbol_t *m) {
    if (!m || m->kind != SYM_METHOD || !m->decl) return false;
    if (m->decl->kind != AST_METHOD_DECL) return false;
    if (!m->decl->method_decl.return_type) return false;
    return stmt_returns_null(m->decl->method_decl.body, 0);
}

/* 内部辅助逻辑 */
static bool loc_at_or_after(zan_loc_t a, zan_loc_t b) {
    if (a.file_id != b.file_id)
        return a.line > b.line || (a.line == b.line && a.col >= b.col);
    return a.offset >= b.offset;
}

/* 内部辅助实现 */
static bool node_loc_prunable(zan_ast_kind_t k) {
    switch (k) {
    case AST_BLOCK: case AST_EXPR_STMT: case AST_RETURN_STMT:
    case AST_THROW_STMT: case AST_VAR_DECL: case AST_IF_STMT:
    case AST_WHILE_STMT: case AST_DO_WHILE_STMT: case AST_FOR_STMT:
    case AST_FOREACH_STMT: case AST_LOCK_STMT:
    case AST_TRY_STMT: case AST_SWITCH_STMT:
        return true;
    default:
        return false;
    }
}

static bool node_guards_null(zan_ast_node_t *n, zan_istr_t name, int depth,
                             zan_loc_t use);

static bool list_guards_null(zan_ast_list_t *l, zan_istr_t name, int depth,
                             zan_loc_t use) {
    for (int i = 0; i < l->count; i++)
        if (node_guards_null(l->items[i], name, depth + 1, use)) return true;
    return false;
}

static bool is_named_ident(zan_ast_node_t *n, zan_istr_t name) {
    return n && n->kind == AST_IDENTIFIER &&
           n->ident.name.len == name.len &&
           memcmp(n->ident.name.str, name.str, (size_t)name.len) == 0;
}

static bool node_guards_null(zan_ast_node_t *n, zan_istr_t name, int depth,
                             zan_loc_t use) {
    if (!n || depth > CHECKER_DERIVES_MAX_DEPTH) return false;
    /* See node_loc_prunable: the loc prune is only sound on statements */
    if (node_loc_prunable(n->kind) && loc_at_or_after(n->loc, use)) {
        return false;
    }
    switch (n->kind) {
    case AST_BINARY:
        /* `x == null` / `x != null` / `x ?? fallback`, either operand order */
        if ((is_named_ident(n->binary.left, name) &&
             (n->binary.right && n->binary.right->kind == AST_NULL_LITERAL)) ||
            (is_named_ident(n->binary.right, name) &&
             (n->binary.left && n->binary.left->kind == AST_NULL_LITERAL)) ||
            (n->binary.op == TK_QUESTION_QUESTION &&
             is_named_ident(n->binary.left, name)))
            return true;
        return node_guards_null(n->binary.left, name, depth + 1, use) ||
               node_guards_null(n->binary.right, name, depth + 1, use);
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        if (is_named_ident(n->type_test.expr, name)) return true;
        return node_guards_null(n->type_test.expr, name, depth + 1, use);
    case AST_MEMBER_ACCESS:
        if (n->member.null_cond && is_named_ident(n->member.object, name))
            return true;
        return node_guards_null(n->member.object, name, depth + 1, use);
    case AST_UNARY:
        return node_guards_null(n->unary.operand, name, depth + 1, use);
    case AST_CAST_EXPR:
        return node_guards_null(n->cast.expr, name, depth + 1, use);
    case AST_CALL:
        return node_guards_null(n->call.callee, name, depth + 1, use) ||
               list_guards_null(&n->call.args, name, depth + 1, use);
    case AST_INDEX:
        return node_guards_null(n->index.object, name, depth + 1, use) ||
               node_guards_null(n->index.index, name, depth + 1, use);
    case AST_CONDITIONAL:
        return node_guards_null(n->conditional.cond, name, depth + 1, use) ||
               node_guards_null(n->conditional.then_expr, name, depth + 1, use) ||
               node_guards_null(n->conditional.else_expr, name, depth + 1, use);
    case AST_ASSIGNMENT:
        return node_guards_null(n->binary.left, name, depth + 1, use) ||
               node_guards_null(n->binary.right, name, depth + 1, use);
    case AST_BLOCK:
        return list_guards_null(&n->block.stmts, name, depth + 1, use);
    case AST_EXPR_STMT:
        return node_guards_null(n->expr_stmt.expr, name, depth + 1, use);
    case AST_RETURN_STMT:
        return node_guards_null(n->ret.value, name, depth + 1, use);
    case AST_THROW_STMT:
        return node_guards_null(n->throw_stmt.value, name, depth + 1, use);
    case AST_VAR_DECL:
        return node_guards_null(n->var_decl.initializer, name, depth + 1, use);
    case AST_IF_STMT:
        return node_guards_null(n->if_stmt.cond, name, depth + 1, use) ||
               node_guards_null(n->if_stmt.then_body, name, depth + 1, use) ||
               node_guards_null(n->if_stmt.else_body, name, depth + 1, use);
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        return node_guards_null(n->while_stmt.cond, name, depth + 1, use) ||
               node_guards_null(n->while_stmt.body, name, depth + 1, use);
    case AST_FOR_STMT:
        return node_guards_null(n->for_stmt.init, name, depth + 1, use) ||
               node_guards_null(n->for_stmt.cond, name, depth + 1, use) ||
               node_guards_null(n->for_stmt.step, name, depth + 1, use) ||
               node_guards_null(n->for_stmt.body, name, depth + 1, use);
    case AST_FOREACH_STMT:
        return node_guards_null(n->foreach_stmt.collection, name, depth + 1, use) ||
               node_guards_null(n->foreach_stmt.body, name, depth + 1, use);
    case AST_LOCK_STMT:
        return node_guards_null(n->lock_stmt.expr, name, depth + 1, use) ||
               node_guards_null(n->lock_stmt.body, name, depth + 1, use);
    case AST_TRY_STMT: {
        if (node_guards_null(n->try_stmt.try_body, name, depth + 1, use)) return true;
        for (int i = 0; i < n->try_stmt.catches.count; i++) {
            zan_ast_node_t *cc = n->try_stmt.catches.items[i];
            if (cc && node_guards_null(cc->catch_clause.body, name, depth + 1, use))
                return true;
        }
        return node_guards_null(n->try_stmt.finally_body, name, depth + 1, use);
    }
    case AST_SWITCH_STMT: {
        if (node_guards_null(n->switch_stmt.expr, name, depth + 1, use)) return true;
        for (int i = 0; i < n->switch_stmt.cases.count; i++) {
            zan_ast_node_t *cs = n->switch_stmt.cases.items[i];
            if (cs && node_guards_null(cs->switch_case.body, name, depth + 1, use))
                return true;
        }
        return false;
    }
    default:
        return false;
    }
}

static void checker_reject_null_receiver(zan_checker_t *c, zan_ast_node_t *expr) {
    if (!expr || expr->kind != AST_MEMBER_ACCESS) return;
    if (expr->member.null_cond) return;
    zan_ast_node_t *obj = expr->member.object;
    if (!obj) return;

    /* 1 */
    zan_ast_node_t *core_obj = obj;
    while (core_obj && core_obj->kind == AST_CAST_EXPR) core_obj = core_obj->cast.expr;
    if (core_obj && core_obj->kind == AST_NULL_LITERAL) {
        zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                      "cannot access member '%.*s' on 'null'; null reference dereference faults at runtime",
                      (int)expr->member.name.len, expr->member.name.str);
        return;
    }

    zan_symbol_t *m = NULL;
    if (obj->kind == AST_CALL) {
        if (obj != c->last_call_node) return;
        m = c->last_call_method;
    } else if (obj->kind == AST_IDENTIFIER) {
        /* a local holding a nullable call result, tested before this access */
        struct checker_local *l = checker_find_local_slot(c, obj->ident.name);
        if (!l || !l->null_src) return;
        if (!c->current_body) return;
        if (node_guards_null(c->current_body, obj->ident.name, 0, expr->loc))
            return;
        m = l->null_src;
    } else {
        return;
    }
    if (!method_can_return_null(m)) return;
    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                  "'%.*s' can return null; accessing '%.*s' on its result "
                  "faults at runtime -- use '?.', or store it and check for "
                  "null first",
                  (int)m->name.len, m->name.str,
                  (int)expr->member.name.len, expr->member.name.str);
}

/* 内部辅助逻辑 */
static bool receiver_is_value_expr(zan_checker_t *c, zan_ast_node_t *obj) {
    switch (obj->kind) {
    case AST_IDENTIFIER: {
        if (checker_find_local_slot(c, obj->ident.name)) return true;
        zan_symbol_t *s = zan_binder_lookup(c->binder, obj->ident.name);
        if (!s) return false;
        return s->kind == SYM_FIELD || s->kind == SYM_PROPERTY ||
               s->kind == SYM_PARAM || s->kind == SYM_LOCAL;
    }
    case AST_CALL:
    case AST_INDEX:
        return true;
    default:
        return false;
    }
}

static zan_type_t *check_member_access(zan_checker_t *c, zan_ast_node_t *expr,
                                       zan_type_t *obj_type, bool in_call_callee) {
    checker_reject_null_receiver(c, expr);
    if (expr->member.object) {
        zan_symbol_t *mg = expr_method_group(c, expr->member.object);
        if (mg) {
            reject_method_group(c, expr->member.object);
            return c->binder->type_error;
        }
    }
    /* Static enum member access: EnumType */
    if (obj_type && obj_type->kind == TYPE_ENUM && obj_type->sym &&
        expr->member.object->kind == AST_IDENTIFIER) {
        zan_symbol_t *os = zan_binder_lookup(c->binder,
                                             expr->member.object->ident.name);
        if (os && os->kind == SYM_ENUM) {
            int ei;
            for (ei = 0; ei < obj_type->sym->member_count; ei++) {
                zan_symbol_t *m = obj_type->sym->members[ei];
                if (m->kind == SYM_ENUM_MEMBER &&
                    m->name.len == expr->member.name.len &&
                    memcmp(m->name.str, expr->member.name.str,
                           (size_t)expr->member.name.len) == 0)
                    break;
            }
            if (ei == obj_type->sym->member_count) {
                /* 内部辅助实现 */
                zan_istr_t mn = expr->member.name;
                bool pseudo = mn.len == 8 &&
                    memcmp(mn.str, "TryParse", 8) == 0;
                if (!pseudo) {
                    zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                                  "enum type '%.*s' has no member '%.*s'",
                                  (int)obj_type->sym->name.len,
                                  obj_type->sym->name.str,
                                  (int)expr->member.name.len,
                                  expr->member.name.str);
                    return c->binder->type_error;
                }
            }
        }
    }
    /* Nullable value types (T?): only */
    if (obj_type && obj_type->kind == TYPE_NULLABLE) {
        zan_istr_t mn = expr->member.name;
        if (mn.len == 8 && memcmp(mn.str, "HasValue", 8) == 0) {
            return c->binder->type_bool;
        }
        if (mn.len == 5 && memcmp(mn.str, "Value", 5) == 0) {
            return obj_type->element_type ? obj_type->element_type : c->binder->type_error;
        }
        if (mn.len == 17 && memcmp(mn.str, "GetValueOrDefault", 17) == 0) {
            return obj_type->element_type ? obj_type->element_type : c->binder->type_error;
        }
        const char *elem_name = obj_type->element_type ? type_name(obj_type->element_type) : "T";
        zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                      "cannot access member '%.*s' directly on nullable type '%s?'; access via '.Value' or check '.HasValue' first",
                      (int)mn.len, mn.str, elem_name);
        return c->binder->type_error;
    }
    /* resolve field/method on known struct/class types */
    if (obj_type && obj_type->sym) {
        for (zan_symbol_t *s = obj_type->sym; s;
             s = (s->type && s->type->base_type) ? s->type->base_type->sym : NULL) {
            for (int i = s->member_count - 1; i >= 0; i--) {
                zan_symbol_t *m = s->members[i];
                if (!m) continue;
                if (m->name.len == expr->member.name.len &&
                    memcmp(m->name.str, expr->member.name.str, m->name.len) == 0) {
                    if (!access_member_allowed(c, m)) {
                        report_inaccessible(c, m, expr->loc);
                        return c->binder->type_error;
                    }
                    /* 内部辅助实现 */
                    if (m->kind == SYM_METHOD)
                        return c->binder->type_error;
                    if (m->kind == SYM_FIELD && (m->modifiers & MOD_STATIC) &&
                        expr->member.object &&
                        receiver_is_value_expr(c, expr->member.object)) {
                        zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                                      "'%.*s' is a static field of '%.*s'; "
                                      "qualify it with the type name "
                                      "('%.*s.%.*s'), not an instance",
                                      (int)expr->member.name.len,
                                      expr->member.name.str,
                                      (int)obj_type->sym->name.len,
                                      obj_type->sym->name.str,
                                      (int)obj_type->sym->name.len,
                                      obj_type->sym->name.str,
                                      (int)expr->member.name.len,
                                      expr->member.name.str);
                        return c->binder->type_error;
                    }
                    return m->type ? m->type : c->binder->type_error;
                }
            }
        }
    }
    /* Reflection members (`ti */
    {
        zan_type_t *rt = zan_refl_member_type(c->binder, obj_type,
                                              expr->member.name);
        if (rt) return rt;
    }
    /* 内部辅助实现 */
    if (obj_type && obj_type->kind == TYPE_ARRAY) {
        bool is_len = expr->member.name.len == 6 &&
            memcmp(expr->member.name.str, "Length", 6) == 0;
        bool is_cnt = expr->member.name.len == 5 &&
            memcmp(expr->member.name.str, "Count", 5) == 0;
        if (is_len || is_cnt) return c->binder->type_int;
    }
    /* Builtin scalar types (string, int, */
    if (obj_type && type_is_scalar_primitive(obj_type)) {
        if (obj_type->kind == TYPE_STRING && expr->member.name.len == 6 &&
            memcmp(expr->member.name.str, "Length", 6) == 0) {
            return c->binder->type_int;
        }
        zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                      "type '%.*s' has no member '%.*s'",
                      (int)obj_type->name.len, obj_type->name.str,
                      (int)expr->member.name.len, expr->member.name.str);
        return c->binder->type_error;
    }
    if (!in_call_callee && obj_type &&
        (obj_type->kind == TYPE_CLASS || obj_type->kind == TYPE_STRUCT ||
         obj_type->kind == TYPE_INTERFACE) &&
        obj_type != c->binder->type_error && obj_type->sym) {
        /* 内部辅助实现 */
        zan_istr_t ns = access_symbol_ns(obj_type->sym);
        if (ns.len)
            zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                          "type '%.*s.%.*s' has no member '%.*s'",
                          (int)ns.len, ns.str,
                          (int)obj_type->sym->name.len, obj_type->sym->name.str,
                          (int)expr->member.name.len, expr->member.name.str);
        else
            zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                          "'%.*s' has no member '%.*s'",
                          (int)obj_type->sym->name.len, obj_type->sym->name.str,
                          (int)expr->member.name.len, expr->member.name.str);
        return c->binder->type_error;
    }
    return c->binder->type_error;
}

static void check_method_body(zan_checker_t *c, zan_ast_node_t *method) {
    if (!method->method_decl.body) return;

    bool saved_ctor = c->in_ctor;
    c->in_ctor = method->kind == AST_CONSTRUCTOR_DECL;
    bool saved_nrt = c->in_no_runtime;
    c->in_no_runtime = zan_ast_has_attr(method, "NoRuntime");
    zan_type_t *saved = c->current_return_type;
    c->current_return_type = zan_binder_resolve_type(c->binder,
                                                      method->method_decl.return_type);
    check_generic_constraints(c, c->current_return_type, method->loc);
    /* 内部辅助实现 */
    struct checker_local *saved_locals = c->locals;
    c->locals = NULL;
    for (int j = 0; j < method->method_decl.params.count; j++) {
        zan_ast_node_t *p = method->method_decl.params.items[j];
        if (p && p->kind == AST_PARAM) {
            zan_type_t *pt = zan_binder_resolve_type(c->binder, p->param.type);
            checker_add_local(c, p->param.name, pt);
        }
    }
    zan_ast_node_t *saved_body = c->current_body;
    c->current_body = method->method_decl.body;
    zan_checker_check_stmt(c, method->method_decl.body);
    zan_definite_check(c->diag, method);
    c->current_body = saved_body;
    c->locals = saved_locals;
    c->current_return_type = saved;
    c->in_no_runtime = saved_nrt;
    c->in_ctor = saved_ctor;
}

/* Completed-subtree set for the struct-cycle walk */
typedef struct {
    zan_ast_node_t **slots;      /* open addressing, NULL = empty */
    int cap;                     /* power of two, 0 = unset */
    int count;
} zan_struct_visited_t;

static size_t struct_visit_hash(zan_ast_node_t *n) {
    uintptr_t h = (uintptr_t)n;
    h ^= h >> 33;
    h *= (uintptr_t)0xff51afd7ed558ccdULL;
    h ^= h >> 29;
    return (size_t)h;
}

static bool struct_visit_seen(const zan_struct_visited_t *v,
                              zan_ast_node_t *n) {
    if (!v->cap) return false;
    size_t mask = (size_t)v->cap - 1;
    for (size_t i = struct_visit_hash(n) & mask; v->slots[i];
         i = (i + 1) & mask) {
        if (v->slots[i] == n) return true;
    }
    return false;
}

static void struct_visit_add(zan_checker_t *c, zan_struct_visited_t *v,
                             zan_ast_node_t *n) {
    if (v->count * 2 + 1 >= v->cap) {
        int ncap = v->cap ? v->cap * 2 : 64;
        zan_ast_node_t **nslots = (zan_ast_node_t **)zan_arena_alloc(
            c->arena, sizeof(zan_ast_node_t *) * (size_t)ncap);
        memset(nslots, 0, sizeof(zan_ast_node_t *) * (size_t)ncap);
        int ocap = v->cap;
        zan_ast_node_t **oslots = v->slots;
        v->slots = nslots;
        v->cap = ncap;
        v->count = 0;
        for (int i = 0; i < ocap; i++) {
            if (!oslots[i]) continue;
            size_t mask = (size_t)ncap - 1;
            size_t j = struct_visit_hash(oslots[i]) & mask;
            while (nslots[j]) j = (j + 1) & mask;
            nslots[j] = oslots[i];
            v->count++;
        }
    }
    size_t mask = (size_t)v->cap - 1;
    size_t i = struct_visit_hash(n) & mask;
    while (v->slots[i]) {
        if (v->slots[i] == n) return;
        i = (i + 1) & mask;
    }
    v->slots[i] = n;
    v->count++;
}

static bool check_struct_cycle_dfs(zan_checker_t *c, zan_ast_node_t *struct_decl,
                                   zan_ast_node_t **stack, int depth,
                                   zan_struct_visited_t *visited) {
    if (!struct_decl || struct_decl->kind != AST_STRUCT_DECL) return false;
    if (depth >= 512) {
        zan_diag_emit(c->diag, DIAG_ERROR, struct_decl->loc,
                      "struct '%.*s' exceeds maximum nesting depth",
                      (int)struct_decl->type_decl.name.len,
                      struct_decl->type_decl.name.str);
        return false;
    }
    /* already walked to completion without a cycle below: skip */
    if (struct_visit_seen(visited, struct_decl)) return false;
    stack[depth] = struct_decl;

    for (int j = 0; j < struct_decl->type_decl.members.count; j++) {
        zan_ast_node_t *member = struct_decl->type_decl.members.items[j];
        if (!member) continue;
        if (member->kind != AST_FIELD_DECL && member->kind != AST_PROPERTY_DECL) continue;
        if (member->field_decl.modifiers & MOD_STATIC) continue;
        if (member->kind == AST_PROPERTY_DECL &&
            (member->field_decl.getter_body || member->field_decl.setter_body)) continue;

        zan_type_t *ftype = zan_binder_resolve_type(c->binder, member->field_decl.type);
        if (!ftype || ftype->kind != TYPE_STRUCT) continue;
        zan_symbol_t *fsym = ftype->sym;
        if (!fsym && c->binder) fsym = zan_binder_lookup(c->binder, ftype->name);
        if (fsym && fsym->decl && fsym->decl->kind == AST_STRUCT_DECL) {
            for (int k = 0; k <= depth; k++) {
                if (stack[k] == fsym->decl) {
                    zan_diag_emit(c->diag, DIAG_ERROR, member->loc,
                                  "struct member '%.*s' of type '%.*s' causes a cycle in the struct layout (recursive struct has infinite size)",
                                  (int)member->field_decl.name.len,
                                  member->field_decl.name.str,
                                  (int)fsym->decl->type_decl.name.len,
                                  fsym->decl->type_decl.name.str);
                    return true;
                }
            }
            if (check_struct_cycle_dfs(c, fsym->decl, stack, depth + 1,
                                       visited)) {
                return true;
            }
        }
    }
    struct_visit_add(c, visited, struct_decl);
    return false;
}

static void check_all_struct_cycles(zan_checker_t *c, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    zan_struct_visited_t visited = { NULL, 0, 0 };
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl && decl->kind == AST_STRUCT_DECL) {
            zan_ast_node_t *stack[512];
            check_struct_cycle_dfs(c, decl, stack, 0, &visited);
        }
    }
}

/* 内部辅助实现 */

/* Declared parameter count of a method symbol, -1 for non-method decls */
static int checker_method_param_count(zan_symbol_t *m) {
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) return -1;
    return m->decl->method_decl.params.count;
}

static bool checker_base_has_virtual_slot(zan_symbol_t *base_sym,
                                          zan_istr_t name, int arity,
                                          int depth) {
    if (!base_sym || depth > CHECKER_DERIVES_MAX_DEPTH) return false;
    for (int i = 0; i < base_sym->member_count; i++) {
        zan_symbol_t *m = base_sym->members[i];
        if (m && m->kind == SYM_METHOD &&
            (m->modifiers & MOD_VIRTUAL) &&
            !(m->modifiers & MOD_OVERRIDE) &&
            m->name.len == name.len &&
            memcmp(m->name.str, name.str, (size_t)name.len) == 0 &&
            checker_method_param_count(m) == arity)
            return true;
    }
    return checker_base_has_virtual_slot(
        (base_sym->type && base_sym->type->base_type)
            ? base_sym->type->base_type->sym : NULL,
        name, arity, depth + 1);
}

static void check_virtual_shadows_base(zan_checker_t *c, zan_ast_node_t *decl) {
    zan_symbol_t *ts = c->current_type_sym;
    if (!ts || !ts->type || !ts->type->base_type || !ts->type->base_type->sym)
        return;
    for (int j = 0; j < decl->type_decl.members.count; j++) {
        zan_ast_node_t *member = decl->type_decl.members.items[j];
        if (member->kind != AST_METHOD_DECL) continue;
        int want = member->method_decl.params.count;
        /* 内部辅助实现 */
        zan_symbol_t *sym = NULL;
        for (int i = 0; i < ts->member_count; i++) {
            zan_symbol_t *m = ts->members[i];
            if (m && m->kind == SYM_METHOD &&
                m->name.len == member->method_decl.name.len &&
                memcmp(m->name.str, member->method_decl.name.str,
                       (size_t)member->method_decl.name.len) == 0 &&
                checker_method_param_count(m) == want)
                sym = m;
        }
        if (!sym || !(sym->modifiers & MOD_VIRTUAL) ||
            (sym->modifiers & MOD_OVERRIDE))
            continue;
        if (checker_base_has_virtual_slot(ts->type->base_type->sym,
                                          sym->name, want, 0)) {
            zan_diag_emit(c->diag, DIAG_WARNING, member->loc,
                "'%.*s' is declared 'virtual' but hides an inherited virtual "
                "with the same name and arity; calls through base-class "
                "references will run the base implementation -- "
                "use 'override' to replace it",
                (int)member->method_decl.name.len,
                member->method_decl.name.str);
        }
    }
}

void zan_checker_check(zan_checker_t *c, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;

    check_all_struct_cycles(c, unit);

    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL &&
            decl->kind != AST_INTERFACE_DECL && decl->kind != AST_ENUM_DECL) {
            continue;
        }

        c->current_type_sym = zan_binder_lookup(c->binder, decl->type_decl.name);
        if (decl->kind == AST_CLASS_DECL) {
            check_virtual_shadows_base(c, decl);
        }
        for (int j = 0; j < decl->type_decl.members.count; j++) {
            zan_ast_node_t *member = decl->type_decl.members.items[j];
            check_weak_member(c, decl, member);
            check_extern_static(c, member);
            if (member->kind == AST_METHOD_DECL ||
                member->kind == AST_CONSTRUCTOR_DECL ||
                member->kind == AST_DESTRUCTOR_DECL) {
                zan_compile_trace("check %.*s.%.*s",
                                (int)decl->type_decl.name.len,
                                decl->type_decl.name.str,
                                (int)member->method_decl.name.len,
                                member->method_decl.name.str);
                check_method_body(c, member);
            }
        }
    }
}
