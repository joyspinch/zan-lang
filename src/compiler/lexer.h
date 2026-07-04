/* lexer.h -- Tokenizer for the Zan language. */

#ifndef ZAN_LEXER_H
#define ZAN_LEXER_H

#include "zan.h"
#include "token.h"

struct zan_token {
    zan_token_kind_t kind;
    zan_loc_t loc;
    union {
        int64_t int_val;
        double float_val;
        zan_istr_t str_val; /* for string/char/ident: pointer + length */
    };
};

struct zan_lexer {
    const char *source;
    size_t source_len;
    size_t pos;
    uint32_t line;
    uint32_t col;
    uint32_t file_id;
    zan_arena_t *arena;
    zan_diag_t *diag;
    int interp_depth;       /* > 0 when inside $"...{expr}..." */
    int interp_brace_depth; /* tracks nested {} inside interpolation expr */
};

void zan_lexer_init(zan_lexer_t *lex, const char *source, size_t len,
                    uint32_t file_id, zan_arena_t *arena, zan_diag_t *diag);
zan_token_t zan_lexer_next(zan_lexer_t *lex);
zan_token_t zan_lexer_peek(zan_lexer_t *lex);

#endif /* ZAN_LEXER_H */
