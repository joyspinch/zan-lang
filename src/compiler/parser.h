/* parser.h -- Recursive descent parser for the Zan language. */

#ifndef ZAN_PARSER_H
#define ZAN_PARSER_H

#include "zan.h"
#include "ast.h"
#include "lexer.h"

struct zan_parser {
    zan_lexer_t *lex;
    zan_arena_t *arena;
    zan_diag_t *diag;
    zan_token_t current;
    zan_token_t previous;
    int expr_depth; /* current expression recursion depth (stack-overflow guard) */
    int expr_tail_depth; /* depth of the low-precedence right recursion (assignment `a = a */
    bool expr_depth_reported; /* 内部辅助逻辑 */
    int stmt_depth; /* current statement/block recursion depth (stack-overflow guard) */
    int type_depth; /* current type-reference recursion depth (stack-overflow guard) */
    int type_no_nullable; /* 内部辅助逻辑 */
    int checked_depth; /* >0 while inside checked( */
    int unchecked_depth; /* >0 while inside unchecked( */
    int synth_counter; /* unique-id seed for synthesized locals (using temp) */
    bool chain_cap_reported; /* 内部辅助逻辑 */
    /* 内部辅助逻辑 */
    zan_ast_list_t pending_members;
    /* 内部辅助逻辑 */
    zan_ast_list_t pending_stmts;
};

void zan_parser_init(zan_parser_t *p, zan_lexer_t *lex, zan_arena_t *arena,
                     zan_diag_t *diag);
zan_ast_node_t *zan_parser_parse(zan_parser_t *p);

/* 降级`event D E;` fields into generated multicast holder classes */
void zan_parser_merge_partials(zan_ast_node_t *unit, zan_arena_t *arena,
                               zan_diag_t *diag);
void zan_parser_desugar_events(zan_ast_node_t *unit, zan_arena_t *arena,
                               zan_diag_t *diag);
/* Hoist nested type declarations (e */
void zan_parser_flatten_nested_types(zan_ast_node_t *unit, zan_arena_t *arena,
                                     zan_diag_t *diag);
/* 内部辅助逻辑 */
void zan_parser_specialize_generic_bases(zan_ast_node_t *unit, zan_arena_t *arena,
                                         zan_diag_t *diag);

#endif /* ZAN_PARSER_H */
