/* 底层系统交互与数据协议契约 */

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
    int expr_depth; /* 底层系统交互与数据协议契约 */
    int expr_tail_depth; /* 底层系统交互与数据协议契约 */
    bool expr_depth_reported; /* 内部辅助逻辑 */
    int stmt_depth; /* 底层系统交互与数据协议契约 */
    int type_depth; /* 底层系统交互与数据协议契约 */
    int type_no_nullable; /* 内部辅助逻辑 */
    int checked_depth; /* 核心系统底层抽象与内存语义契约 */
    int unchecked_depth; /* 核心系统底层抽象与内存语义契约 */
    int synth_counter; /* 底层系统交互与数据协议契约 */
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
/* 核心系统底层抽象与内存语义契约 */
void zan_parser_flatten_nested_types(zan_ast_node_t *unit, zan_arena_t *arena,
                                     zan_diag_t *diag);
/* 内部辅助逻辑 */
void zan_parser_specialize_generic_bases(zan_ast_node_t *unit, zan_arena_t *arena,
                                         zan_diag_t *diag);

#endif /* ZAN_PARSER_H */
