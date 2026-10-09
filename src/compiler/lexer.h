/* lexer.h -- Tokenizer for the Zan language. */

#ifndef ZAN_LEXER_H
#define ZAN_LEXER_H

#include "zan.h"
#include "token.h"

#define ZAN_PP_MAX_DEFINES 2048
#define ZAN_PP_MAX_COND_DEPTH 64

typedef struct {
    char name[64];
    char value[256];
} zan_pp_define_t;

struct zan_token {
    zan_token_kind_t kind;
    zan_loc_t loc;
    /* Integer literal suffix encoding (TK_INT_LIT only): 0=none, 1=L/l
     * (long), 2=U/u (uint), 3=UL/LU in either case (ulong). */
    int lit_suffix;
    /* Integer literal radix (TK_INT_LIT only): 2/8/10/16. Hex/binary/octal
     * literals up to 0xFFFFFFFF type as int with two's-complement wrap
     * (ARGB colors), decimal keeps the value-fit rule (checker). */
    unsigned char lit_radix;
    union {
        int64_t int_val;
        double float_val;
        zan_istr_t str_val; /* for string/char/ident: pointer + length */
    };
};

#define ZAN_MAX_INTERP_DEPTH 512

/* Bracket nesting inside one interpolation hole, so that a `}` is told apart
 * from the one that closes the hole and a `:` from a conditional's colon. */
typedef struct {
    int brace;
    int paren;
    int bracket;
} zan_interp_level_t;

struct zan_lexer {
    const char *source;
    size_t source_len;
    size_t pos;
    uint32_t line;
    uint32_t col;
    uint32_t file_id;
    zan_arena_t *arena;
    zan_diag_t *diag;
    /* Interpolation holes currently open, innermost last. A hole can hold
     * another $"..." with holes of its own, so the bracket counters are per
     * hole. */
    int interp_depth;
    zan_interp_level_t interp_stack[ZAN_MAX_INTERP_DEPTH];

    /* Arena-allocated rather than inline: the parser snapshots whole lexers
     * into stack locals for speculative lookahead, and the 40 KB table inline
     * overflowed the 1 MB stack at modest expression nesting. The snapshot
     * carries `define_count`, so restoring it truncates any #define a
     * speculative pass appended (live entries are always [0, define_count)). */
    zan_pp_define_t *defines;
    int define_count;
    int define_cap;
    /* Conditional compilation stack: 1=active, 0=skipping */
    int cond_stack[ZAN_PP_MAX_COND_DEPTH];
    int cond_depth;
    /* Track whether current #if group had a true branch (for #elif) */
    int cond_seen_true[ZAN_PP_MAX_COND_DEPTH];
    /* Frames pushed past ZAN_PP_MAX_COND_DEPTH are counted here instead of
     * writing cond_stack[MAX] (out of bounds; its address aliases cond_depth).
     * pp_active() treats an overflow frame as inactive. */
    int cond_overflow;
    int at_line_start; /* 1 if next non-ws char is at start of logical line */
};

void zan_lexer_init(zan_lexer_t *lex, const char *source, size_t len,
                    uint32_t file_id, zan_arena_t *arena, zan_diag_t *diag);
zan_token_t zan_lexer_next(zan_lexer_t *lex);
zan_token_t zan_lexer_peek(zan_lexer_t *lex);
/* Two tokens ahead without consuming (state fully restored, like peek). */
zan_token_t zan_lexer_peek2(zan_lexer_t *lex);

/* Preprocessor API: add a define before lexing begins */
void zan_lexer_define(zan_lexer_t *lex, const char *name, const char *value);

#endif /* ZAN_LEXER_H */
