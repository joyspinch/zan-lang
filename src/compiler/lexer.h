/* lexer.h -- Tokenizer for the Zan language. */

#ifndef ZAN_LEXER_H
#define ZAN_LEXER_H

#include "zan.h"
#include "token.h"

/* ---- Preprocessor defines ---- */
#define ZAN_PP_MAX_DEFINES 128
#define ZAN_PP_MAX_COND_DEPTH 32

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
    union {
        int64_t int_val;
        double float_val;
        zan_istr_t str_val; /* for string/char/ident: pointer + length */
    };
};

#define ZAN_MAX_INTERP_DEPTH 16

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
     * hole: with one shared set the inner string's closing quote ended
     * interpolation for the outer one too. */
    int interp_depth;
    zan_interp_level_t interp_stack[ZAN_MAX_INTERP_DEPTH];

    /* ---- Preprocessor state ---- */
    /* Arena-allocated rather than inline: the table is 40 KB, and the parser
     * backtracks speculative lookahead by snapshotting a whole lexer into a
     * stack local (`zan_lexer_t saved = *p->lex;`, six sites in parser.c). With
     * the table inline every one of those frames was 41 KB, and one of them
     * sits in the expression recursion cycle (parse_postfix) -- so nesting
     * overflowed the 1 MB stack at ~25 levels, long before the parser's own
     * 256-deep guard could turn it into a diagnostic.
     * Behind a pointer a snapshot copies ~520 bytes and shares the table.
     * Semantics are unchanged: the snapshot carries `define_count`, so
     * restoring it truncates any #define a speculative pass appended (live
     * entries are always [0, define_count)). */
    zan_pp_define_t *defines;
    int define_count;
    /* Conditional compilation stack: 1=active, 0=skipping */
    int cond_stack[ZAN_PP_MAX_COND_DEPTH];
    int cond_depth;
    /* Track whether current #if group had a true branch (for #elif) */
    int cond_seen_true[ZAN_PP_MAX_COND_DEPTH];
    int at_line_start; /* 1 if next non-ws char is at start of logical line */
};

void zan_lexer_init(zan_lexer_t *lex, const char *source, size_t len,
                    uint32_t file_id, zan_arena_t *arena, zan_diag_t *diag);
zan_token_t zan_lexer_next(zan_lexer_t *lex);
zan_token_t zan_lexer_peek(zan_lexer_t *lex);

/* Preprocessor API: add a define before lexing begins */
void zan_lexer_define(zan_lexer_t *lex, const char *name, const char *value);

#endif /* ZAN_LEXER_H */
