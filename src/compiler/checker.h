/* checker.h -- Basic type checker for the Zan language. */

#ifndef ZAN_CHECKER_H
#define ZAN_CHECKER_H

#include "zan.h"
#include "ast.h"
#include "binder.h"

/* locals of the method body being checked; see checker.c */
struct checker_local;

struct zan_checker {
    zan_binder_t *binder;
    zan_arena_t *arena;
    zan_diag_t *diag;
    zan_type_t *current_return_type;
    /* inside a [NoRuntime] body: anything needing the managed runtime
     * (allocation, ARC, exceptions, the monitor) is an error there */
    bool in_no_runtime;
    /* the type whose member body is being checked, and whether that body is a
     * constructor: a `readonly` field may only be assigned there */
    zan_symbol_t *current_type_sym;
    bool in_ctor;
    /* linked list of the current body's locals/params, so a bare name in an
     * expression resolves local-first, then fields, then scope -- the same
     * precedence irgen uses */
    struct checker_local *locals;
    /* the call expression typed most recently and the method it resolved to,
     * so a member access on a call result can tell whether the callee can
     * hand back null (`o.PathGet("k").AsString(...)`) */
    zan_ast_node_t *last_call_node;
    zan_symbol_t *last_call_method;
    /* body of the method being checked, scanned for a local's null guard */
    zan_ast_node_t *current_body;
};

void zan_checker_init(zan_checker_t *c, zan_binder_t *binder,
                      zan_arena_t *arena, zan_diag_t *diag);
void zan_checker_check(zan_checker_t *c, zan_ast_node_t *unit);

zan_type_t *zan_checker_check_expr(zan_checker_t *c, zan_ast_node_t *expr);
void zan_checker_check_stmt(zan_checker_t *c, zan_ast_node_t *stmt);

#endif /* ZAN_CHECKER_H */
