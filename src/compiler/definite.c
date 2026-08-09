/* definite.c -- Definite-assignment analysis.
 *
 * A local read before every path through the method has written it produces a
 * zero/garbage slot at runtime, which is exactly the class of bug that reads
 * as "the IDE just closed": a null control field, a count that was never set.
 * This pass walks each body once as a small flow analysis and reports the read
 * instead.
 *
 * Shape of the analysis: every declared local is a slot that is either
 * definitely assigned or not; statements move that state forward, branches are
 * analysed independently and merged by intersection (a variable is assigned
 * after an `if` only when *both* arms assigned it), and a statement that
 * cannot fall through (return / throw / break / continue) contributes nothing
 * to the merge. Loop bodies may run zero times, so what they assign does not
 * survive the loop unless the loop cannot exit normally.
 *
 * Deliberately conservative, because a false positive here is a program that
 * no longer compiles: a body containing `goto`/labels is skipped entirely, a
 * lambda marks everything it could capture as assigned, and the right operand
 * of `&&` / `||` / `??` / `?:` never counts as a definite write. */

#include <string.h>

#include "definite.h"
#include "diag.h"
#include "token.h"

#define DA_MAX_LOCALS 256

struct da_var {
    zan_istr_t name;
    unsigned char assigned;
    unsigned char is_out;   /* `out` parameter: must be written before return */
    unsigned char reported; /* already diagnosed, so a loop reports once */
};

/* Where the `break`s inside the innermost loop/switch collect their states:
 * the code after the loop sees the intersection of every way out of it. */
struct da_exit {
    struct da_exit *outer;
    unsigned char seen;
    unsigned char state[DA_MAX_LOCALS];
};

struct da_ctx {
    zan_diag_t *diag;
    struct da_var v[DA_MAX_LOCALS];
    int count;
    bool reachable;
    bool bail;      /* a shape the analysis does not model: report nothing */
    bool emit;      /* second pass: diagnostics are real */
    struct da_exit *exits;
};

static void da_stmt(struct da_ctx *c, zan_ast_node_t *n);
static void da_expr(struct da_ctx *c, zan_ast_node_t *n);

static bool da_name_eq(zan_istr_t a, zan_istr_t b) {
    return a.len == b.len && a.str && b.str &&
           memcmp(a.str, b.str, (size_t)a.len) == 0;
}

/* Innermost declaration wins, so a shadowing local is found before the outer
 * one it hides. */
static int da_find(struct da_ctx *c, zan_istr_t name) {
    for (int i = c->count - 1; i >= 0; i--) {
        if (da_name_eq(c->v[i].name, name)) return i;
    }
    return -1;
}

static void da_declare(struct da_ctx *c, zan_istr_t name, bool assigned,
                       bool is_out) {
    if (!name.str || name.len == 0) return;
    if (c->count >= DA_MAX_LOCALS) { c->bail = true; return; }
    struct da_var *v = &c->v[c->count++];
    v->name = name;
    v->assigned = assigned ? 1 : 0;
    v->is_out = is_out ? 1 : 0;
    v->reported = 0;
}

static void da_save(struct da_ctx *c, unsigned char *buf) {
    for (int i = 0; i < c->count; i++) buf[i] = c->v[i].assigned;
}

static void da_restore(struct da_ctx *c, const unsigned char *buf) {
    for (int i = 0; i < c->count; i++) c->v[i].assigned = buf[i];
}

/* Intersect: assigned only where both paths assigned. */
static void da_merge(struct da_ctx *c, const unsigned char *buf) {
    for (int i = 0; i < c->count; i++) {
        if (!buf[i]) c->v[i].assigned = 0;
    }
}

static void da_assign_all(struct da_ctx *c) {
    for (int i = 0; i < c->count; i++) c->v[i].assigned = 1;
}

static void da_mark(struct da_ctx *c, zan_ast_node_t *n) {
    if (!n || n->kind != AST_IDENTIFIER) return;
    int i = da_find(c, n->ident.name);
    if (i >= 0) c->v[i].assigned = 1;
}

static void da_use(struct da_ctx *c, zan_ast_node_t *n) {
    if (!n || n->kind != AST_IDENTIFIER) return;
    int i = da_find(c, n->ident.name);
    if (i < 0 || c->v[i].assigned || c->v[i].reported) return;
    c->v[i].reported = 1;
    if (!c->emit) return;
    zan_diag_emit(c->diag, DIAG_ERROR, n->loc,
                  "use of unassigned local variable '%.*s'",
                  (int)n->ident.name.len, n->ident.name.str);
}

/* Every `out` parameter has to be written on the path leaving the method. */
static void da_check_out_params(struct da_ctx *c, zan_loc_t loc) {
    for (int i = 0; i < c->count; i++) {
        if (!c->v[i].is_out || c->v[i].assigned || c->v[i].reported) continue;
        c->v[i].reported = 1;
        if (!c->emit) continue;
        zan_diag_emit(c->diag, DIAG_ERROR, loc,
                      "the out parameter '%.*s' must be assigned before "
                      "control leaves the method",
                      (int)c->v[i].name.len, c->v[i].name.str);
    }
}

static void da_record_exit(struct da_ctx *c) {
    struct da_exit *e = c->exits;
    if (!e) return;
    if (!e->seen) {
        da_save(c, e->state);
        e->seen = 1;
        return;
    }
    for (int i = 0; i < c->count; i++) {
        if (!c->v[i].assigned) e->state[i] = 0;
    }
}

static bool da_is_true_literal(zan_ast_node_t *n) {
    return n && n->kind == AST_BOOL_LITERAL && n->bool_val;
}

/* ---- expressions ---- */

static void da_list(struct da_ctx *c, zan_ast_list_t *l) {
    if (!l) return;
    for (int i = 0; i < l->count; i++) da_expr(c, l->items[i]);
}

static void da_call_args(struct da_ctx *c, zan_ast_list_t *args) {
    if (!args) return;
    for (int i = 0; i < args->count; i++) {
        zan_ast_node_t *a = args->items[i];
        if (!a) continue;
        if (a->kind == AST_REF_ARG) {
            /* `out x` writes the slot; an inline `out T x` declares it too.
             * `ref x` reads it first, so it must already be assigned. */
            if (a->ref_arg.is_out) {
                if (a->ref_arg.decl_type && a->ref_arg.expr &&
                    a->ref_arg.expr->kind == AST_IDENTIFIER) {
                    da_declare(c, a->ref_arg.expr->ident.name, true, false);
                } else {
                    da_mark(c, a->ref_arg.expr);
                }
            } else {
                da_expr(c, a->ref_arg.expr);
            }
            continue;
        }
        da_expr(c, a);
    }
}

/* Operands that run conditionally: reads are checked against the state at
 * hand, writes are rolled back because the operand may not run. */
static void da_maybe(struct da_ctx *c, zan_ast_node_t *n) {
    unsigned char snap[DA_MAX_LOCALS];
    da_save(c, snap);
    da_expr(c, n);
    da_restore(c, snap);
}

static void da_ternary(struct da_ctx *c, zan_ast_node_t *n) {
    unsigned char snap[DA_MAX_LOCALS];
    unsigned char branch[DA_MAX_LOCALS];
    da_expr(c, n->conditional.cond);
    da_save(c, snap);
    da_expr(c, n->conditional.then_expr);
    da_save(c, branch);
    da_restore(c, snap);
    da_expr(c, n->conditional.else_expr);
    da_merge(c, branch);
}

static void da_switch_expr(struct da_ctx *c, zan_ast_node_t *n) {
    unsigned char snap[DA_MAX_LOCALS];
    da_expr(c, n->switch_expr.expr);
    da_save(c, snap);
    for (int i = 0; i < n->switch_expr.arms.count; i++) {
        zan_ast_node_t *arm = n->switch_expr.arms.items[i];
        if (!arm) continue;
        da_restore(c, snap);
        int mark = c->count;
        if (arm->switch_arm.var_name.str && arm->switch_arm.var_name.len) {
            da_declare(c, arm->switch_arm.var_name, true, false);
        }
        da_expr(c, arm->switch_arm.when_cond);
        da_expr(c, arm->switch_arm.result);
        c->count = mark;
    }
    da_restore(c, snap);
}

static void da_expr(struct da_ctx *c, zan_ast_node_t *n) {
    if (!n || c->bail) return;

    switch (n->kind) {
    case AST_IDENTIFIER:
        da_use(c, n);
        return;

    case AST_ASSIGNMENT: {
        zan_ast_node_t *lhs = n->binary.left;
        bool compound = n->binary.op != TK_EQ;
        if (lhs && lhs->kind == AST_IDENTIFIER) {
            if (compound) da_use(c, lhs);
            da_expr(c, n->binary.right);
            da_mark(c, lhs);
            return;
        }
        /* `p.x = 1` on a struct local writes the variable rather than reading
         * it: field-by-field initialisation is how a struct without a
         * constructor is filled in, so the root counts as assigned instead of
         * being flagged. */
        zan_ast_node_t *root = lhs;
        while (root && root->kind == AST_MEMBER_ACCESS) root = root->member.object;
        if (root && root->kind == AST_IDENTIFIER && da_find(c, root->ident.name) >= 0) {
            da_expr(c, n->binary.right);
            da_mark(c, root);
            return;
        }
        da_expr(c, lhs);
        da_expr(c, n->binary.right);
        da_mark(c, lhs);
        return;
    }

    case AST_BINARY:
        /* Short-circuit operands run conditionally: whatever they assign is
         * not definite afterwards. */
        if (n->binary.op == TK_AMP_AMP || n->binary.op == TK_PIPE_PIPE ||
            n->binary.op == TK_QUESTION_QUESTION) {
            da_expr(c, n->binary.left);
            da_maybe(c, n->binary.right);
            return;
        }
        da_expr(c, n->binary.left);
        da_expr(c, n->binary.right);
        return;

    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        da_expr(c, n->unary.operand);
        if ((n->unary.op == TK_PLUS_PLUS || n->unary.op == TK_MINUS_MINUS)) {
            da_mark(c, n->unary.operand);
        }
        return;

    case AST_CONDITIONAL:
        da_ternary(c, n);
        return;

    case AST_CALL:
        da_expr(c, n->call.callee);
        da_call_args(c, &n->call.args);
        return;

    case AST_NEW_EXPR:
        da_call_args(c, &n->new_expr.args);
        return;

    case AST_MEMBER_ACCESS:
        da_expr(c, n->member.object);
        return;

    case AST_INDEX:
        da_expr(c, n->index.object);
        da_expr(c, n->index.index);
        da_list(c, &n->index.extra);
        return;

    case AST_CAST_EXPR:
        da_expr(c, n->cast.expr);
        return;

    case AST_IS_EXPR:
    case AST_AS_EXPR:
        da_expr(c, n->type_test.expr);
        /* `is T x` binds x only where the test held; treat it as assigned so
         * the guarded use is never flagged. */
        if (n->type_test.var_name.str && n->type_test.var_name.len) {
            da_declare(c, n->type_test.var_name, true, false);
        }
        return;

    case AST_TUPLE_EXPR:
        da_list(c, &n->tuple_expr.items);
        return;

    case AST_STRING_INTERP:
        da_list(c, &n->string_interp.parts);
        return;

    case AST_AWAIT_EXPR:
        da_expr(c, n->await_expr.expr);
        return;

    case AST_NAMED_ARG:
        da_expr(c, n->named_arg.expr);
        return;

    case AST_SWITCH_EXPR:
        da_switch_expr(c, n);
        return;

    case AST_LAMBDA:
    case AST_QUERY_EXPR:
        /* The body runs somewhere this analysis cannot see and may write any
         * captured local, so nothing it touches can be reported afterwards. */
        da_assign_all(c);
        return;

    default:
        return;
    }
}

/* ---- statements ---- */

static void da_block(struct da_ctx *c, zan_ast_node_t *n) {
    int mark = c->count;
    for (int i = 0; i < n->block.stmts.count && !c->bail; i++) {
        da_stmt(c, n->block.stmts.items[i]);
    }
    /* Locals leave scope with the block. */
    c->count = mark;
}

static void da_if(struct da_ctx *c, zan_ast_node_t *n) {
    unsigned char entry[DA_MAX_LOCALS];
    unsigned char then_state[DA_MAX_LOCALS];
    da_expr(c, n->if_stmt.cond);
    da_save(c, entry);

    da_stmt(c, n->if_stmt.then_body);
    bool then_out = c->reachable;
    da_save(c, then_state);

    da_restore(c, entry);
    c->reachable = true;
    bool else_out = true;
    if (n->if_stmt.else_body) {
        da_stmt(c, n->if_stmt.else_body);
        else_out = c->reachable;
    }

    if (then_out && else_out) {
        da_merge(c, then_state);
        c->reachable = true;
    } else if (then_out) {
        da_restore(c, then_state);
        c->reachable = true;
    } else if (else_out) {
        c->reachable = true;
    } else {
        c->reachable = false;
    }
}

/* while / for / foreach: the body may run zero times, so only the state at
 * loop entry (intersected with every `break`) survives. */
static void da_loop(struct da_ctx *c, zan_ast_node_t *body,
                    zan_ast_node_t *step, bool endless) {
    unsigned char entry[DA_MAX_LOCALS];
    da_save(c, entry);

    struct da_exit exit;
    exit.outer = c->exits;
    exit.seen = 0;
    c->exits = &exit;

    c->reachable = true;
    da_stmt(c, body);
    if (step) da_expr(c, step);

    c->exits = exit.outer;

    if (endless) {
        if (exit.seen) {
            da_restore(c, exit.state);
            c->reachable = true;
        } else {
            /* `while (true)` with no break: the code after it is dead. */
            da_restore(c, entry);
            c->reachable = false;
        }
        return;
    }
    da_restore(c, entry);
    if (exit.seen) da_merge(c, exit.state);
    c->reachable = true;
}

static void da_try(struct da_ctx *c, zan_ast_node_t *n) {
    unsigned char entry[DA_MAX_LOCALS];
    unsigned char merged[DA_MAX_LOCALS];
    da_save(c, entry);

    da_stmt(c, n->try_stmt.try_body);
    bool any = c->reachable;
    if (any) da_save(c, merged);

    for (int i = 0; i < n->try_stmt.catches.count; i++) {
        zan_ast_node_t *cc = n->try_stmt.catches.items[i];
        if (!cc) continue;
        /* A catch runs from anywhere in the try, so it starts from the state
         * the try began with. */
        da_restore(c, entry);
        c->reachable = true;
        int mark = c->count;
        if (cc->catch_clause.var_name.str && cc->catch_clause.var_name.len) {
            da_declare(c, cc->catch_clause.var_name, true, false);
        }
        da_stmt(c, cc->catch_clause.body);
        c->count = mark;
        if (!c->reachable) continue;
        if (!any) {
            da_save(c, merged);
            any = true;
        } else {
            for (int k = 0; k < c->count; k++) {
                if (!c->v[k].assigned) merged[k] = 0;
            }
        }
    }

    if (any) {
        da_restore(c, merged);
    } else {
        da_restore(c, entry);
    }
    c->reachable = any;

    if (n->try_stmt.finally_body) {
        /* The finally block runs on every path, so its writes are definite --
         * even when the protected code left the method. */
        bool saved = c->reachable;
        c->reachable = true;
        da_stmt(c, n->try_stmt.finally_body);
        c->reachable = saved && c->reachable;
    }
}

static void da_switch(struct da_ctx *c, zan_ast_node_t *n) {
    unsigned char entry[DA_MAX_LOCALS];
    da_expr(c, n->switch_stmt.expr);
    da_save(c, entry);

    struct da_exit exit;
    exit.outer = c->exits;
    exit.seen = 0;
    c->exits = &exit;

    bool has_default = false;
    for (int i = 0; i < n->switch_stmt.cases.count; i++) {
        zan_ast_node_t *sc = n->switch_stmt.cases.items[i];
        if (!sc) continue;
        if (!sc->switch_case.pattern && !sc->switch_case.type_pattern) {
            has_default = true;
        }
        da_restore(c, entry);
        c->reachable = true;
        int mark = c->count;
        if (sc->switch_case.var_name.str && sc->switch_case.var_name.len) {
            da_declare(c, sc->switch_case.var_name, true, false);
        }
        da_expr(c, sc->switch_case.when_cond);
        da_stmt(c, sc->switch_case.body);
        c->count = mark;
        /* A case that falls out of the switch leaves through the same edge a
         * break does. */
        if (c->reachable) da_record_exit(c);
    }

    c->exits = exit.outer;

    da_restore(c, entry);
    if (exit.seen && has_default) {
        da_merge(c, exit.state);
    }
    c->reachable = true;
}

static void da_stmt(struct da_ctx *c, zan_ast_node_t *n) {
    if (!n || c->bail) return;

    switch (n->kind) {
    case AST_BLOCK:
        da_block(c, n);
        return;

    case AST_VAR_DECL:
        da_expr(c, n->var_decl.initializer);
        da_declare(c, n->var_decl.name, n->var_decl.initializer != NULL, false);
        return;

    case AST_TUPLE_DECON:
        da_expr(c, n->tuple_decon.initializer);
        for (int i = 0; i < n->tuple_decon.names.count; i++) {
            zan_ast_node_t *nm = n->tuple_decon.names.items[i];
            if (nm && nm->kind == AST_IDENTIFIER) {
                da_declare(c, nm->ident.name, true, false);
            }
        }
        return;

    case AST_EXPR_STMT:
        da_expr(c, n->expr_stmt.expr);
        return;

    case AST_RETURN_STMT:
        da_expr(c, n->ret.value);
        da_check_out_params(c, n->loc);
        c->reachable = false;
        return;

    case AST_YIELD_STMT:
        da_expr(c, n->yield_stmt.value);
        return;

    case AST_THROW_STMT:
        da_expr(c, n->throw_stmt.value);
        c->reachable = false;
        return;

    case AST_BREAK_STMT:
        da_record_exit(c);
        c->reachable = false;
        return;

    case AST_CONTINUE_STMT:
        c->reachable = false;
        return;

    case AST_IF_STMT:
        da_if(c, n);
        return;

    case AST_WHILE_STMT:
        da_expr(c, n->while_stmt.cond);
        da_loop(c, n->while_stmt.body, NULL,
                da_is_true_literal(n->while_stmt.cond));
        return;

    case AST_DO_WHILE_STMT: {
        /* The body always runs once, so what it assigns holds afterwards. */
        struct da_exit exit;
        exit.outer = c->exits;
        exit.seen = 0;
        c->exits = &exit;
        da_stmt(c, n->while_stmt.body);
        bool fell = c->reachable;
        c->reachable = true;
        da_expr(c, n->while_stmt.cond);
        c->exits = exit.outer;
        if (exit.seen) {
            if (fell) {
                da_merge(c, exit.state);
            } else {
                da_restore(c, exit.state);
            }
        }
        c->reachable = true;
        return;
    }

    case AST_FOR_STMT: {
        int mark = c->count;
        da_stmt(c, n->for_stmt.init);
        da_expr(c, n->for_stmt.cond);
        da_loop(c, n->for_stmt.body, n->for_stmt.step,
                n->for_stmt.cond == NULL || da_is_true_literal(n->for_stmt.cond));
        c->count = mark;
        return;
    }

    case AST_FOREACH_STMT: {
        da_expr(c, n->foreach_stmt.collection);
        int mark = c->count;
        da_declare(c, n->foreach_stmt.var_name, true, false);
        da_loop(c, n->foreach_stmt.body, NULL, false);
        c->count = mark;
        return;
    }

    case AST_SWITCH_STMT:
        da_switch(c, n);
        return;

    case AST_TRY_STMT:
        da_try(c, n);
        return;

    case AST_LOCK_STMT:
        da_expr(c, n->lock_stmt.expr);
        da_stmt(c, n->lock_stmt.body);
        return;

    case AST_GOTO_STMT:
    case AST_LABEL_STMT:
        /* Arbitrary jumps: the flow this pass models no longer describes the
         * body, so it says nothing about it. */
        c->bail = true;
        return;

    default:
        /* Any other node in statement position is an expression. */
        da_expr(c, n);
        return;
    }
}

static void da_run(struct da_ctx *c, zan_ast_node_t *method, bool emit) {
    c->count = 0;
    c->reachable = true;
    c->bail = false;
    c->emit = emit;
    c->exits = NULL;

    for (int i = 0; i < method->method_decl.params.count; i++) {
        zan_ast_node_t *p = method->method_decl.params.items[i];
        if (!p || p->kind != AST_PARAM) continue;
        da_declare(c, p->param.name, p->param.by_ref != 2, p->param.by_ref == 2);
    }

    da_stmt(c, method->method_decl.body);
    if (c->reachable) da_check_out_params(c, method->loc);
}

void zan_definite_check(zan_diag_t *diag, zan_ast_node_t *method) {
    if (!method || !method->method_decl.body) return;
    if (method->kind != AST_METHOD_DECL &&
        method->kind != AST_CONSTRUCTOR_DECL &&
        method->kind != AST_DESTRUCTOR_DECL) {
        return;
    }

    static struct da_ctx ctx;
    ctx.diag = diag;

    /* First pass finds the shapes the analysis refuses to model (a `goto`
     * anywhere in the body); only a clean pass gets to report. */
    da_run(&ctx, method, false);
    if (ctx.bail) return;
    da_run(&ctx, method, true);
}
