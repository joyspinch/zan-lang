/* checker.c -- Basic type checker for the Zan language.
 *
 * For M1 this performs lightweight checks:
 *   - Expression type inference (literals, binary ops, calls)
 *   - Return type validation
 *   - Variable type resolution
 *   - Basic assignment compatibility
 */

#include "checker.h"
#include "diag.h"
#include <string.h>

void zan_checker_init(zan_checker_t *c, zan_binder_t *binder,
                      zan_arena_t *arena, zan_diag_t *diag) {
    memset(c, 0, sizeof(*c));
    c->binder = binder;
    c->arena = arena;
    c->diag = diag;
    c->current_return_type = NULL;
}

/* ---- expression type checking ---- */

static const char *type_name(zan_type_t *t) {
    if (!t) return "<unknown>";
    return t->name.str;
}

static bool type_is_numeric(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BYTE: case TYPE_SHORT: case TYPE_INT: case TYPE_LONG:
    case TYPE_FLOAT: case TYPE_DOUBLE: case TYPE_NINT:
        return true;
    default:
        return false;
    }
}

static bool type_is_integral(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BYTE: case TYPE_SHORT: case TYPE_INT: case TYPE_LONG: case TYPE_NINT:
        return true;
    default:
        return false;
    }
}

static zan_type_t *promote_numeric(zan_binder_t *b, zan_type_t *a, zan_type_t *b_type) {
    if (!a || !b_type) return b->type_error;
    if (a->kind == TYPE_DOUBLE || b_type->kind == TYPE_DOUBLE) return b->type_double;
    if (a->kind == TYPE_FLOAT  || b_type->kind == TYPE_FLOAT)  return b->type_float;
    if (a->kind == TYPE_LONG   || b_type->kind == TYPE_LONG)   return b->type_long;
    return b->type_int;
}

zan_type_t *zan_checker_check_expr(zan_checker_t *c, zan_ast_node_t *expr) {
    if (!expr) return c->binder->type_error;

    switch (expr->kind) {
    case AST_INT_LITERAL:
        return c->binder->type_int;
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

        switch (expr->binary.op) {
        case TK_PLUS:
            /* string concatenation */
            if (left->kind == TYPE_STRING || right->kind == TYPE_STRING) {
                return c->binder->type_string;
            }
            /* fall through */
        case TK_MINUS: case TK_STAR: case TK_SLASH: case TK_PERCENT:
            if (type_is_numeric(left) && type_is_numeric(right)) {
                return promote_numeric(c->binder, left, right);
            }
            if (left->kind != TYPE_ERROR && right->kind != TYPE_ERROR) {
                zan_diag_emit(c->diag, DIAG_ERROR, expr->loc,
                              "cannot apply operator to '%s' and '%s'",
                              type_name(left), type_name(right));
            }
            return c->binder->type_error;

        case TK_AMP: case TK_PIPE: case TK_CARET:
        case TK_LESS_LESS: case TK_GREATER_GREATER:
            if (type_is_integral(left) && type_is_integral(right)) {
                return promote_numeric(c->binder, left, right);
            }
            return c->binder->type_error;

        case TK_EQ_EQ: case TK_BANG_EQ:
        case TK_LESS: case TK_GREATER:
        case TK_LESS_EQ: case TK_GREATER_EQ:
            return c->binder->type_bool;

        case TK_AMP_AMP: case TK_PIPE_PIPE:
            return c->binder->type_bool;

        case TK_QUESTION_QUESTION:
            return left;

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
            if (type_is_numeric(operand)) return operand;
            return c->binder->type_error;
        default:
            return c->binder->type_error;
        }
    }

    case AST_POSTFIX_UNARY: {
        zan_type_t *operand = zan_checker_check_expr(c, expr->unary.operand);
        if (type_is_numeric(operand)) return operand;
        return c->binder->type_error;
    }

    case AST_CALL: {
        zan_checker_check_expr(c, expr->call.callee);
        for (int i = 0; i < expr->call.args.count; i++) {
            zan_checker_check_expr(c, expr->call.args.items[i]);
        }
        /* for M1 return void — full call resolution in M2 */
        return c->binder->type_void;
    }

    case AST_MEMBER_ACCESS: {
        zan_checker_check_expr(c, expr->member.object);
        /* for M1 return error — full member resolution in M2 */
        return c->binder->type_error;
    }

    case AST_INDEX: {
        zan_type_t *obj = zan_checker_check_expr(c, expr->index.object);
        zan_checker_check_expr(c, expr->index.index);
        if (obj->kind == TYPE_ARRAY && obj->element_type) {
            return obj->element_type;
        }
        return c->binder->type_error;
    }

    case AST_ASSIGNMENT: {
        zan_checker_check_expr(c, expr->binary.left);
        zan_type_t *right = zan_checker_check_expr(c, expr->binary.right);
        return right;
    }

    case AST_NEW_EXPR: {
        zan_type_t *type = zan_binder_resolve_type(c->binder, expr->new_expr.type);
        for (int i = 0; i < expr->new_expr.args.count; i++) {
            zan_checker_check_expr(c, expr->new_expr.args.items[i]);
        }
        return type;
    }

    case AST_CONDITIONAL: {
        zan_checker_check_expr(c, expr->conditional.cond);
        zan_type_t *then_type = zan_checker_check_expr(c, expr->conditional.then_expr);
        zan_checker_check_expr(c, expr->conditional.else_expr);
        return then_type;
    }

    case AST_LAMBDA:
        for (int i = 0; i < expr->lambda.params.count; i++) {
            /* params type-checked later */
        }
        zan_checker_check_expr(c, expr->lambda.body);
        return c->binder->type_error; /* lambda type resolved in M2 */

    case AST_THIS_EXPR:
    case AST_BASE_EXPR:
        return c->binder->type_error; /* resolved in M2 */

    default:
        return c->binder->type_error;
    }
}

/* ---- statement checking ---- */

void zan_checker_check_stmt(zan_checker_t *c, zan_ast_node_t *stmt) {
    if (!stmt) return;

    switch (stmt->kind) {
    case AST_BLOCK:
        for (int i = 0; i < stmt->block.stmts.count; i++) {
            zan_checker_check_stmt(c, stmt->block.stmts.items[i]);
        }
        break;

    case AST_VAR_DECL:
        if (stmt->var_decl.initializer) {
            zan_checker_check_expr(c, stmt->var_decl.initializer);
        }
        if (stmt->var_decl.type) {
            zan_binder_resolve_type(c->binder, stmt->var_decl.type);
        }
        break;

    case AST_EXPR_STMT:
        zan_checker_check_expr(c, stmt->expr_stmt.expr);
        break;

    case AST_RETURN_STMT:
        if (stmt->ret.value) {
            zan_checker_check_expr(c, stmt->ret.value);
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
        zan_checker_check_expr(c, stmt->foreach_stmt.collection);
        zan_checker_check_stmt(c, stmt->foreach_stmt.body);
        break;

    case AST_THROW_STMT:
        zan_checker_check_expr(c, stmt->throw_stmt.value);
        break;

    case AST_BREAK_STMT:
    case AST_CONTINUE_STMT:
        break;

    default:
        break;
    }
}

/* ---- check entire compilation unit ---- */

static void check_method_body(zan_checker_t *c, zan_ast_node_t *method) {
    if (!method->method_decl.body) return;

    zan_type_t *saved = c->current_return_type;
    c->current_return_type = zan_binder_resolve_type(c->binder,
                                                      method->method_decl.return_type);
    zan_checker_check_stmt(c, method->method_decl.body);
    c->current_return_type = saved;
}

void zan_checker_check(zan_checker_t *c, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;

    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL &&
            decl->kind != AST_INTERFACE_DECL && decl->kind != AST_ENUM_DECL) {
            continue;
        }

        for (int j = 0; j < decl->type_decl.members.count; j++) {
            zan_ast_node_t *member = decl->type_decl.members.items[j];
            if (member->kind == AST_METHOD_DECL ||
                member->kind == AST_CONSTRUCTOR_DECL ||
                member->kind == AST_DESTRUCTOR_DECL) {
                check_method_body(c, member);
            }
        }
    }
}
