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
};

void zan_parser_init(zan_parser_t *p, zan_lexer_t *lex, zan_arena_t *arena,
                     zan_diag_t *diag);
zan_ast_node_t *zan_parser_parse(zan_parser_t *p);

#endif /* ZAN_PARSER_H */
