
#ifndef ZAN_CHECKER_H
#define ZAN_CHECKER_H

#include "zan.h"
#include "ast.h"
#include "binder.h"

struct checker_local;

struct zan_checker {
    zan_binder_t *binder;
    zan_arena_t *arena;
    zan_diag_t *diag;
    zan_type_t *current_return_type;
    bool in_no_runtime;
    zan_symbol_t *current_type_sym;
    bool in_ctor;
    struct checker_local *locals;
    zan_ast_node_t *last_call_node;
    zan_symbol_t *last_call_method;
    zan_ast_node_t *current_body;
    /* 已经报过"null 绑不上值类型形参"的实参位置：同一个实参只报一次
     * （唯一方法路径与重载路径会撞上同一处）。 */
    zan_loc_t null_arg_reported;
    bool has_null_arg_reported;
};

void zan_checker_init(zan_checker_t *c, zan_binder_t *binder,
                      zan_arena_t *arena, zan_diag_t *diag);
void zan_checker_check(zan_checker_t *c, zan_ast_node_t *unit);

zan_type_t *zan_checker_check_expr(zan_checker_t *c, zan_ast_node_t *expr);
void zan_checker_check_stmt(zan_checker_t *c, zan_ast_node_t *stmt);

#endif /* ZAN_CHECKER_H */
