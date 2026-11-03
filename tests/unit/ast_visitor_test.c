#include "ast.h"
#include "lexer.h"
#include "arena.h"
#include "diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define EXPECT(cond, ...) do {                                              \
    if (!(cond)) {                                                          \
        failures++;                                                         \
        fprintf(stderr, "FAIL: ");                                          \
        fprintf(stderr, __VA_ARGS__);                                       \
        fprintf(stderr, "\n");                                              \
    }                                                                       \
} while (0)

static void test_lexer_peek_n(void) {
    const char *src = "int x = 42 + y;";
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);

    zan_lexer_t lex;
    zan_lexer_init(&lex, src, strlen(src), 1, arena, diag);

    /* 1. Test peek_n lookahead */
    zan_token_t t1 = zan_lexer_peek_n(&lex, 1);
    zan_token_t t2 = zan_lexer_peek_n(&lex, 2);
    zan_token_t t3 = zan_lexer_peek_n(&lex, 3);
    zan_token_t t4 = zan_lexer_peek_n(&lex, 4);
    zan_token_t t5 = zan_lexer_peek_n(&lex, 5);
    zan_token_t t6 = zan_lexer_peek_n(&lex, 6);

    EXPECT(t1.kind == TK_INT, "t1 expected TK_INT, got %d", t1.kind);
    EXPECT(t2.kind == TK_IDENT, "t2 expected TK_IDENT, got %d", t2.kind);
    EXPECT(t3.kind == TK_EQ, "t3 expected TK_EQ, got %d", t3.kind);
    EXPECT(t4.kind == TK_INT_LIT, "t4 expected TK_INT_LIT, got %d", t4.kind);
    EXPECT(t5.kind == TK_PLUS, "t5 expected TK_PLUS, got %d", t5.kind);
    EXPECT(t6.kind == TK_IDENT, "t6 expected TK_IDENT, got %d", t6.kind);

    /* 2. Verify state was NOT consumed by peek_n */
    zan_token_t p1 = zan_lexer_peek(&lex);
    zan_token_t p2 = zan_lexer_peek2(&lex);
    EXPECT(p1.kind == t1.kind, "peek should equal peek_n(1)");
    EXPECT(p2.kind == t2.kind, "peek2 should equal peek_n(2)");

    zan_token_t next1 = zan_lexer_next(&lex);
    EXPECT(next1.kind == TK_INT, "next after peeks must be TK_INT, got %d", next1.kind);
    zan_token_t next2 = zan_lexer_next(&lex);
    EXPECT(next2.kind == TK_IDENT, "next after peeks must be TK_IDENT, got %d", next2.kind);

    zan_arena_free(arena);
}

typedef struct {
    int count;
    zan_ast_kind_t kinds[32];
} walk_tracker_t;

static zan_walk_action_t pre_tracker(zan_ast_node_t *node, void *user_data) {
    walk_tracker_t *t = (walk_tracker_t *)user_data;
    if (t->count < 32) {
        t->kinds[t->count++] = node->kind;
    }
    return ZAN_WALK_CONTINUE;
}

static zan_walk_action_t skip_branch_tracker(zan_ast_node_t *node, void *user_data) {
    walk_tracker_t *t = (walk_tracker_t *)user_data;
    if (t->count < 32) {
        t->kinds[t->count++] = node->kind;
    }
    if (node->kind == AST_BINARY) {
        return ZAN_WALK_SKIP_CHILDREN;
    }
    return ZAN_WALK_CONTINUE;
}

static zan_walk_action_t abort_tracker(zan_ast_node_t *node, void *user_data) {
    walk_tracker_t *t = (walk_tracker_t *)user_data;
    if (t->count < 32) {
        t->kinds[t->count++] = node->kind;
    }
    if (node->kind == AST_BINARY) {
        return ZAN_WALK_ABORT;
    }
    return ZAN_WALK_CONTINUE;
}

static bool count_child_cb(zan_ast_node_t *child, void *user_data) {
    (void)child;
    int *c = (int *)user_data;
    (*c)++;
    return true;
}

static bool rewrite_child_cb(zan_ast_node_t **child_ptr, void *user_data) {
    zan_ast_node_t *new_node = (zan_ast_node_t *)user_data;
    if (*child_ptr && (*child_ptr)->kind == AST_IDENTIFIER) {
        *child_ptr = new_node;
    }
    return true;
}

static void test_ast_child_iteration_and_visitor(void) {
    /* Construct a test AST:
     * if (x > 0) {
     *     foo(1, y);
     * }
     */
    zan_ast_node_t lit0 = {0};
    lit0.kind = AST_INT_LITERAL;

    zan_ast_node_t var_x = {0};
    var_x.kind = AST_IDENTIFIER;

    zan_ast_node_t cond_bin = {0};
    cond_bin.kind = AST_BINARY;
    cond_bin.binary.left = &var_x;
    cond_bin.binary.right = &lit0;

    zan_ast_node_t callee = {0};
    callee.kind = AST_IDENTIFIER;

    zan_ast_node_t arg1 = {0};
    arg1.kind = AST_INT_LITERAL;

    zan_ast_node_t arg2 = {0};
    arg2.kind = AST_IDENTIFIER;

    zan_ast_node_t *call_args[2] = { &arg1, &arg2 };
    zan_ast_node_t call = {0};
    call.kind = AST_CALL;
    call.call.callee = &callee;
    call.call.args.items = call_args;
    call.call.args.count = 2;

    zan_ast_node_t expr_stmt = {0};
    expr_stmt.kind = AST_EXPR_STMT;
    expr_stmt.expr_stmt.expr = &call;

    zan_ast_node_t *block_stmts[1] = { &expr_stmt };
    zan_ast_node_t block = {0};
    block.kind = AST_BLOCK;
    block.block.stmts.items = block_stmts;
    block.block.stmts.count = 1;

    zan_ast_node_t if_node = {0};
    if_node.kind = AST_IF_STMT;
    if_node.if_stmt.cond = &cond_bin;
    if_node.if_stmt.then_body = &block;
    if_node.if_stmt.else_body = NULL;

    /* 1. Test child count and child_at */
    EXPECT(zan_ast_child_count(&cond_bin) == 2, "cond_bin child count must be 2");
    EXPECT(zan_ast_child_at(&cond_bin, 0) == &var_x, "cond_bin child 0 must be var_x");
    EXPECT(zan_ast_child_at(&cond_bin, 1) == &lit0, "cond_bin child 1 must be lit0");

    EXPECT(zan_ast_child_count(&call) == 3, "call child count must be 3 (callee + 2 args)");
    EXPECT(zan_ast_child_at(&call, 0) == &callee, "call child 0 must be callee");
    EXPECT(zan_ast_child_at(&call, 1) == &arg1, "call child 1 must be arg1");
    EXPECT(zan_ast_child_at(&call, 2) == &arg2, "call child 2 must be arg2");

    /* 2. Test zan_ast_for_each_child (NULL else_body skipped) */
    int if_child_count = 0;
    zan_ast_for_each_child(&if_node, count_child_cb, &if_child_count);
    EXPECT(if_child_count == 2, "if_node should have 2 non-null children (cond, then), got %d", if_child_count);

    /* 3. Test zan_ast_subtree_count */
    /* if_node(1) + cond_bin(1) + var_x(1) + lit0(1) + block(1) + expr_stmt(1) + call(1) + callee(1) + arg1(1) + arg2(1) = 10 */
    size_t total = zan_ast_subtree_count(&if_node);
    EXPECT(total == 10, "subtree total expected 10, got %zu", total);

    /* 4. Test pre-order walk */
    walk_tracker_t tracker = {0};
    bool walk_ok = zan_ast_walk(&if_node, pre_tracker, NULL, &tracker);
    EXPECT(walk_ok, "walk should succeed");
    EXPECT(tracker.count == 10, "walk expected 10 nodes visited, got %d", tracker.count);
    EXPECT(tracker.kinds[0] == AST_IF_STMT, "first visited should be IF");
    EXPECT(tracker.kinds[1] == AST_BINARY, "second visited should be BINARY");

    /* 5. Test walk with ZAN_WALK_SKIP_CHILDREN (skipping cond_bin children) */
    walk_tracker_t skip_tracker = {0};
    walk_ok = zan_ast_walk(&if_node, skip_branch_tracker, NULL, &skip_tracker);
    EXPECT(walk_ok, "skip walk should succeed");
    /* var_x and lit0 are skipped -> 10 - 2 = 8 nodes */
    EXPECT(skip_tracker.count == 8, "skip walk expected 8 nodes, got %d", skip_tracker.count);

    /* 6. Test walk with ZAN_WALK_ABORT */
    walk_tracker_t abort_track = {0};
    walk_ok = zan_ast_walk(&if_node, abort_tracker, NULL, &abort_track);
    EXPECT(!walk_ok, "abort walk should return false");
    EXPECT(abort_track.count == 2, "abort walk should visit 2 nodes before aborting, got %d", abort_track.count);

    /* 7. Test in-place child rewriting via zan_ast_child_ptr_at / zan_ast_for_each_child_ptr */
    zan_ast_node_t replacement_id = {0};
    replacement_id.kind = AST_STRING_LITERAL;
    zan_ast_for_each_child_ptr(&cond_bin, rewrite_child_cb, &replacement_id);
    EXPECT(cond_bin.binary.left == &replacement_id, "binary.left should be rewritten in-place to replacement_id");
}

int main(void) {
    test_lexer_peek_n();
    test_ast_child_iteration_and_visitor();

    if (failures == 0) {
        printf("ALL AST VISITOR AND LEXER TESTS PASSED\n");
        return 0;
    } else {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
}
