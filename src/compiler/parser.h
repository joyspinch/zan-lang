
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
    int expr_depth;
    int expr_tail_depth;
    bool expr_depth_reported;
    int stmt_depth;
    int type_depth;
    int type_no_nullable;
    int checked_depth;
    int unchecked_depth;
    int synth_counter;
    bool chain_cap_reported;
    zan_ast_list_t pending_members;
    zan_ast_list_t pending_stmts;
    zan_ast_node_t *cur_type_decl; /* 正在解析成员的外层类（yield 迭代器合成用） */
};

void zan_parser_init(zan_parser_t *p, zan_lexer_t *lex, zan_arena_t *arena,
                     zan_diag_t *diag);
zan_ast_node_t *zan_parser_parse(zan_parser_t *p);

/* 降级`event D E;` fields into generated multicast holder classes */
void zan_parser_merge_partials(zan_ast_node_t *unit, zan_arena_t *arena,
                               zan_diag_t *diag);
void zan_parser_desugar_events(zan_ast_node_t *unit, zan_arena_t *arena,
                               zan_diag_t *diag);
void zan_parser_flatten_nested_types(zan_ast_node_t *unit, zan_arena_t *arena,
                                     zan_diag_t *diag);
void zan_parser_specialize_generic_bases(zan_ast_node_t *unit, zan_arena_t *arena,
                                         zan_diag_t *diag);

#endif /* ZAN_PARSER_H */
