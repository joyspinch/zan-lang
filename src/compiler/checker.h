/* 底层系统交互与数据协议契约 */

#ifndef ZAN_CHECKER_H
#define ZAN_CHECKER_H

#include "zan.h"
#include "ast.h"
#include "binder.h"

/* 底层系统交互与数据协议契约 */
struct checker_local;

struct zan_checker {
    zan_binder_t *binder;
    zan_arena_t *arena;
    zan_diag_t *diag;
    zan_type_t *current_return_type;
    /* 底层系统交互与数据协议契约 */
    bool in_no_runtime;
    /* 底层系统交互与数据协议契约 */
    zan_symbol_t *current_type_sym;
    bool in_ctor;
    /* 底层系统交互与数据协议契约 */
    struct checker_local *locals;
    /* 底层系统交互与数据协议契约 */
    zan_ast_node_t *last_call_node;
    zan_symbol_t *last_call_method;
    /* 底层系统交互与数据协议契约 */
    zan_ast_node_t *current_body;
};

void zan_checker_init(zan_checker_t *c, zan_binder_t *binder,
                      zan_arena_t *arena, zan_diag_t *diag);
void zan_checker_check(zan_checker_t *c, zan_ast_node_t *unit);

zan_type_t *zan_checker_check_expr(zan_checker_t *c, zan_ast_node_t *expr);
void zan_checker_check_stmt(zan_checker_t *c, zan_ast_node_t *stmt);

#endif /* ZAN_CHECKER_H */
