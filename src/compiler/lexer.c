/* lexer.c -- Tokenizer for the Zan language.
 *
 * Handles all token types from SPEC.md Section 2: keywords, identifiers,
 * integer/float/string/char literals, operators, and punctuation.
 */

#include "lexer.h"
#include "arena.h"
#include "diag.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* ---- keyword table ---- */

typedef struct {
    const char *name;
    zan_token_kind_t kind;
} keyword_entry_t;

static const keyword_entry_t s_keywords[] = {
    {"abstract",  TK_ABSTRACT},
    {"as",        TK_AS},
    {"async",     TK_ASYNC},
    {"await",     TK_AWAIT},
    {"base",      TK_BASE},
    {"bool",      TK_BOOL},
    {"break",     TK_BREAK},
    {"byte",      TK_BYTE},
    {"case",      TK_CASE},
    {"catch",     TK_CATCH},
    {"char",      TK_CHAR},
    {"class",     TK_CLASS},
    {"const",     TK_CONST},
    {"continue",  TK_CONTINUE},
    {"default",   TK_DEFAULT},
    {"do",        TK_DO},
    {"double",    TK_DOUBLE},
    {"else",      TK_ELSE},
    {"enum",      TK_ENUM},
    {"extern",    TK_EXTERN},
    {"false",     TK_FALSE},
    {"finally",   TK_FINALLY},
    {"float",     TK_FLOAT},
    {"for",       TK_FOR},
    {"foreach",   TK_FOREACH},
    {"get",       TK_GET},
    {"if",        TK_IF},
    {"in",        TK_IN},
    {"int",       TK_INT},
    {"interface", TK_INTERFACE},
    {"internal",  TK_INTERNAL},
    {"is",        TK_IS},
    {"let",       TK_LET},
    {"long",      TK_LONG},
    {"namespace", TK_NAMESPACE},
    {"new",       TK_NEW},
    {"nint",      TK_NINT},
    {"null",      TK_NULL},
    {"object",    TK_OBJECT},
    {"out",       TK_OUT},
    {"override",  TK_OVERRIDE},
    {"private",   TK_PRIVATE},
    {"protected", TK_PROTECTED},
    {"public",    TK_PUBLIC},
    {"readonly",  TK_READONLY},
    {"ref",       TK_REF},
    {"return",    TK_RETURN},
    {"sealed",    TK_SEALED},
    {"set",       TK_SET},
    {"short",     TK_SHORT},
    {"sizeof",    TK_SIZEOF},
    {"static",    TK_STATIC},
    {"string",    TK_STRING},
    {"struct",    TK_STRUCT},
    {"switch",    TK_SWITCH},
    {"this",      TK_THIS},
    {"throw",     TK_THROW},
    {"true",      TK_TRUE},
    {"try",       TK_TRY},
    {"typeof",    TK_TYPEOF},
    {"unsafe",    TK_UNSAFE},
    {"using",     TK_USING},
    {"value",     TK_VALUE},
    {"var",       TK_VAR},
    {"virtual",   TK_VIRTUAL},
    {"void",      TK_VOID},
    {"weak",      TK_WEAK},
    {"when",      TK_WHEN},
    {"where",     TK_WHERE},
    {"while",     TK_WHILE},
};

#define KEYWORD_COUNT (sizeof(s_keywords) / sizeof(s_keywords[0]))

/* ---- token kind names ---- */

static const char *s_token_names[TK__COUNT] = {
    [TK_INVALID]     = "INVALID",
    [TK_EOF]         = "EOF",
    [TK_INT_LIT]     = "INT_LIT",
    [TK_FLOAT_LIT]   = "FLOAT_LIT",
    [TK_STRING_LIT]  = "STRING_LIT",
    [TK_CHAR_LIT]    = "CHAR_LIT",
    [TK_IDENT]       = "IDENT",
    [TK_LPAREN]      = "(",
    [TK_RPAREN]      = ")",
    [TK_LBRACE]      = "{",
    [TK_RBRACE]      = "}",
    [TK_LBRACKET]    = "[",
    [TK_RBRACKET]    = "]",
    [TK_SEMICOLON]   = ";",
    [TK_COLON]       = ":",
    [TK_COMMA]       = ",",
    [TK_DOT]         = ".",
    [TK_DOTDOT]      = "..",
    [TK_QUESTION]    = "?",
    [TK_QUESTION_DOT]= "?.",
    [TK_QUESTION_QUESTION] = "??",
    [TK_TILDE]       = "~",
    [TK_ARROW]       = "=>",
    [TK_PLUS]        = "+",
    [TK_MINUS]       = "-",
    [TK_STAR]        = "*",
    [TK_SLASH]       = "/",
    [TK_PERCENT]     = "%",
    [TK_PLUS_PLUS]   = "++",
    [TK_MINUS_MINUS] = "--",
    [TK_LESS]        = "<",
    [TK_GREATER]     = ">",
    [TK_LESS_EQ]     = "<=",
    [TK_GREATER_EQ]  = ">=",
    [TK_EQ_EQ]       = "==",
    [TK_BANG_EQ]     = "!=",
    [TK_BANG]        = "!",
    [TK_AMP_AMP]     = "&&",
    [TK_PIPE_PIPE]   = "||",
    [TK_AMP]         = "&",
    [TK_PIPE]        = "|",
    [TK_CARET]       = "^",
    [TK_LESS_LESS]   = "<<",
    [TK_GREATER_GREATER] = ">>",
    [TK_EQ]          = "=",
    [TK_PLUS_EQ]     = "+=",
    [TK_MINUS_EQ]    = "-=",
    [TK_STAR_EQ]     = "*=",
    [TK_SLASH_EQ]    = "/=",
    [TK_PERCENT_EQ]  = "%=",
    [TK_AMP_EQ]      = "&=",
    [TK_PIPE_EQ]     = "|=",
    [TK_CARET_EQ]    = "^=",
    [TK_LESS_LESS_EQ]= "<<=",
    [TK_GREATER_GREATER_EQ] = ">>=",
};

const char *zan_token_kind_name(zan_token_kind_t kind) {
    if (kind >= 0 && kind < TK__COUNT && s_token_names[kind]) {
        return s_token_names[kind];
    }
    /* keywords: use the keyword table */
    for (size_t i = 0; i < KEYWORD_COUNT; i++) {
        if (s_keywords[i].kind == kind) return s_keywords[i].name;
    }
    return "???";
}

/* ---- lexer helpers ---- */

void zan_lexer_init(zan_lexer_t *lex, const char *source, size_t len,
                    uint32_t file_id, zan_arena_t *arena, zan_diag_t *diag) {
    memset(lex, 0, sizeof(*lex));
    lex->source = source;
    lex->source_len = len;
    lex->pos = 0;
    lex->line = 1;
    lex->col = 1;
    lex->file_id = file_id;
    lex->arena = arena;
    lex->diag = diag;
}

static inline bool lexer_at_end(zan_lexer_t *lex) {
    return lex->pos >= lex->source_len;
}

static inline char lexer_peek_ch(zan_lexer_t *lex) {
    if (lexer_at_end(lex)) return '\0';
    return lex->source[lex->pos];
}

static inline char lexer_peek_ch2(zan_lexer_t *lex) {
    if (lex->pos + 1 >= lex->source_len) return '\0';
    return lex->source[lex->pos + 1];
}

static inline char lexer_advance(zan_lexer_t *lex) {
    char ch = lex->source[lex->pos++];
    if (ch == '\n') {
        lex->line++;
        lex->col = 1;
    } else {
        lex->col++;
    }
    return ch;
}

static inline zan_loc_t lexer_loc(zan_lexer_t *lex) {
    return zan_loc(lex->file_id, lex->line, lex->col, (uint32_t)lex->pos);
}

static inline zan_token_t lexer_make(zan_lexer_t *lex, zan_token_kind_t kind,
                                     zan_loc_t loc) {
    (void)lex;
    zan_token_t tok;
    memset(&tok, 0, sizeof(tok));
    tok.kind = kind;
    tok.loc = loc;
    return tok;
}

static inline bool lexer_match(zan_lexer_t *lex, char expected) {
    if (lexer_at_end(lex) || lex->source[lex->pos] != expected) return false;
    lexer_advance(lex);
    return true;
}

/* ---- skip whitespace and comments ---- */

static void lexer_skip_whitespace(zan_lexer_t *lex) {
    for (;;) {
        if (lexer_at_end(lex)) return;
        char ch = lexer_peek_ch(lex);

        /* whitespace */
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            lexer_advance(lex);
            continue;
        }

        /* single-line comment */
        if (ch == '/' && lexer_peek_ch2(lex) == '/') {
            while (!lexer_at_end(lex) && lexer_peek_ch(lex) != '\n') {
                lexer_advance(lex);
            }
            continue;
        }

        /* multi-line comment */
        if (ch == '/' && lexer_peek_ch2(lex) == '*') {
            zan_loc_t start_loc = lexer_loc(lex);
            lexer_advance(lex); /* / */
            lexer_advance(lex); /* * */
            int depth = 1;
            while (!lexer_at_end(lex) && depth > 0) {
                if (lexer_peek_ch(lex) == '/' && lexer_peek_ch2(lex) == '*') {
                    lexer_advance(lex);
                    lexer_advance(lex);
                    depth++;
                } else if (lexer_peek_ch(lex) == '*' && lexer_peek_ch2(lex) == '/') {
                    lexer_advance(lex);
                    lexer_advance(lex);
                    depth--;
                } else {
                    lexer_advance(lex);
                }
            }
            if (depth > 0) {
                zan_diag_emit(lex->diag, DIAG_ERROR, start_loc,
                              "unterminated multi-line comment");
            }
            continue;
        }

        break;
    }
}

/* ---- identifier / keyword ---- */

static zan_token_t lexer_ident_or_keyword(zan_lexer_t *lex) {
    zan_loc_t loc = lexer_loc(lex);
    size_t start = lex->pos;

    while (!lexer_at_end(lex)) {
        char ch = lexer_peek_ch(lex);
        if (isalnum((unsigned char)ch) || ch == '_') {
            lexer_advance(lex);
        } else {
            break;
        }
    }

    size_t len = lex->pos - start;
    const char *text = lex->source + start;

    /* check keywords */
    for (size_t i = 0; i < KEYWORD_COUNT; i++) {
        if (strlen(s_keywords[i].name) == len &&
            memcmp(s_keywords[i].name, text, len) == 0) {
            return lexer_make(lex, s_keywords[i].kind, loc);
        }
    }

    /* identifier */
    zan_token_t tok = lexer_make(lex, TK_IDENT, loc);
    tok.str_val.str = zan_arena_strdup(lex->arena, text, len);
    tok.str_val.len = (uint32_t)len;
    return tok;
}

/* ---- number literal ---- */

static zan_token_t lexer_number(zan_lexer_t *lex) {
    zan_loc_t loc = lexer_loc(lex);
    size_t start = lex->pos;
    bool is_float = false;

    /* check for 0x, 0b, 0o prefixes */
    if (lexer_peek_ch(lex) == '0' && lex->pos + 1 < lex->source_len) {
        char next = lex->source[lex->pos + 1];
        if (next == 'x' || next == 'X') {
            lexer_advance(lex); /* 0 */
            lexer_advance(lex); /* x */
            while (!lexer_at_end(lex)) {
                char ch = lexer_peek_ch(lex);
                if (isxdigit((unsigned char)ch) || ch == '_') {
                    lexer_advance(lex);
                } else {
                    break;
                }
            }
            zan_token_t tok = lexer_make(lex, TK_INT_LIT, loc);
            /* parse hex value, ignoring underscores */
            char buf[64];
            size_t bi = 0;
            for (size_t i = start + 2; i < lex->pos && bi < 63; i++) {
                if (lex->source[i] != '_') buf[bi++] = lex->source[i];
            }
            buf[bi] = '\0';
            tok.int_val = (int64_t)strtoull(buf, NULL, 16);
            return tok;
        }
        if (next == 'b' || next == 'B') {
            lexer_advance(lex); /* 0 */
            lexer_advance(lex); /* b */
            while (!lexer_at_end(lex)) {
                char ch = lexer_peek_ch(lex);
                if (ch == '0' || ch == '1' || ch == '_') {
                    lexer_advance(lex);
                } else {
                    break;
                }
            }
            zan_token_t tok = lexer_make(lex, TK_INT_LIT, loc);
            char buf[128];
            size_t bi = 0;
            for (size_t i = start + 2; i < lex->pos && bi < 127; i++) {
                if (lex->source[i] != '_') buf[bi++] = lex->source[i];
            }
            buf[bi] = '\0';
            tok.int_val = (int64_t)strtoull(buf, NULL, 2);
            return tok;
        }
        if (next == 'o' || next == 'O') {
            lexer_advance(lex); /* 0 */
            lexer_advance(lex); /* o */
            while (!lexer_at_end(lex)) {
                char ch = lexer_peek_ch(lex);
                if ((ch >= '0' && ch <= '7') || ch == '_') {
                    lexer_advance(lex);
                } else {
                    break;
                }
            }
            zan_token_t tok = lexer_make(lex, TK_INT_LIT, loc);
            char buf[64];
            size_t bi = 0;
            for (size_t i = start + 2; i < lex->pos && bi < 63; i++) {
                if (lex->source[i] != '_') buf[bi++] = lex->source[i];
            }
            buf[bi] = '\0';
            tok.int_val = (int64_t)strtoull(buf, NULL, 8);
            return tok;
        }
    }

    /* decimal digits */
    while (!lexer_at_end(lex)) {
        char ch = lexer_peek_ch(lex);
        if (isdigit((unsigned char)ch) || ch == '_') {
            lexer_advance(lex);
        } else {
            break;
        }
    }

    /* fractional part */
    if (lexer_peek_ch(lex) == '.' && lexer_peek_ch2(lex) != '.') {
        is_float = true;
        lexer_advance(lex); /* . */
        while (!lexer_at_end(lex)) {
            char ch = lexer_peek_ch(lex);
            if (isdigit((unsigned char)ch) || ch == '_') {
                lexer_advance(lex);
            } else {
                break;
            }
        }
    }

    /* exponent */
    if (lexer_peek_ch(lex) == 'e' || lexer_peek_ch(lex) == 'E') {
        is_float = true;
        lexer_advance(lex); /* e */
        if (lexer_peek_ch(lex) == '+' || lexer_peek_ch(lex) == '-') {
            lexer_advance(lex);
        }
        while (!lexer_at_end(lex) && isdigit((unsigned char)lexer_peek_ch(lex))) {
            lexer_advance(lex);
        }
    }

    /* suffix */
    if (lexer_peek_ch(lex) == 'f' || lexer_peek_ch(lex) == 'F') {
        is_float = true;
        lexer_advance(lex);
    }

    /* build clean number string (no underscores) */
    char buf[128];
    size_t bi = 0;
    for (size_t i = start; i < lex->pos && bi < 127; i++) {
        char ch = lex->source[i];
        if (ch != '_' && ch != 'f' && ch != 'F') {
            buf[bi++] = ch;
        }
    }
    buf[bi] = '\0';

    if (is_float) {
        zan_token_t tok = lexer_make(lex, TK_FLOAT_LIT, loc);
        tok.float_val = strtod(buf, NULL);
        return tok;
    } else {
        zan_token_t tok = lexer_make(lex, TK_INT_LIT, loc);
        tok.int_val = (int64_t)strtoll(buf, NULL, 10);
        return tok;
    }
}

/* ---- string literal ---- */

static char lexer_escape_char(zan_lexer_t *lex) {
    char ch = lexer_advance(lex);
    switch (ch) {
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    case '\\': return '\\';
    case '"': return '"';
    case '\'': return '\'';
    case '0': return '\0';
    default:
        zan_diag_emit(lex->diag, DIAG_ERROR, lexer_loc(lex),
                      "invalid escape sequence '\\%c'", ch);
        return ch;
    }
}

static zan_token_t lexer_string(zan_lexer_t *lex) {
    zan_loc_t loc = lexer_loc(lex);
    lexer_advance(lex); /* opening " */

    /* collect into temporary buffer */
    char buf[4096];
    size_t bi = 0;

    while (!lexer_at_end(lex) && lexer_peek_ch(lex) != '"') {
        if (lexer_peek_ch(lex) == '\\') {
            lexer_advance(lex); /* \ */
            if (bi < sizeof(buf) - 1) {
                buf[bi++] = lexer_escape_char(lex);
            }
        } else if (lexer_peek_ch(lex) == '\n') {
            zan_diag_emit(lex->diag, DIAG_ERROR, loc, "unterminated string literal");
            break;
        } else {
            if (bi < sizeof(buf) - 1) {
                buf[bi++] = lexer_advance(lex);
            } else {
                lexer_advance(lex);
            }
        }
    }

    if (!lexer_at_end(lex)) {
        lexer_advance(lex); /* closing " */
    } else {
        zan_diag_emit(lex->diag, DIAG_ERROR, loc, "unterminated string literal");
    }

    zan_token_t tok = lexer_make(lex, TK_STRING_LIT, loc);
    tok.str_val.str = zan_arena_strdup(lex->arena, buf, bi);
    tok.str_val.len = (uint32_t)bi;
    return tok;
}

/* ---- char literal ---- */

static zan_token_t lexer_char(zan_lexer_t *lex) {
    zan_loc_t loc = lexer_loc(lex);
    lexer_advance(lex); /* opening ' */

    char ch;
    if (lexer_peek_ch(lex) == '\\') {
        lexer_advance(lex); /* \ */
        ch = lexer_escape_char(lex);
    } else {
        ch = lexer_advance(lex);
    }

    if (lexer_peek_ch(lex) != '\'') {
        zan_diag_emit(lex->diag, DIAG_ERROR, loc, "unterminated character literal");
    } else {
        lexer_advance(lex); /* closing ' */
    }

    zan_token_t tok = lexer_make(lex, TK_CHAR_LIT, loc);
    tok.int_val = (int64_t)(unsigned char)ch;
    return tok;
}

/* ---- interpolated string $"..." ---- */

static zan_token_t lexer_interp_string_segment(zan_lexer_t *lex, zan_token_kind_t start_kind) {
    zan_loc_t loc = lexer_loc(lex);
    char buf[4096];
    size_t bi = 0;

    while (!lexer_at_end(lex) && lexer_peek_ch(lex) != '"' && lexer_peek_ch(lex) != '{') {
        if (lexer_peek_ch(lex) == '\\') {
            lexer_advance(lex); /* \ */
            if (bi < sizeof(buf) - 1) {
                buf[bi++] = lexer_escape_char(lex);
            }
        } else if (lexer_peek_ch(lex) == '\n') {
            zan_diag_emit(lex->diag, DIAG_ERROR, loc, "unterminated interpolated string");
            break;
        } else {
            if (bi < sizeof(buf) - 1) {
                buf[bi++] = lexer_advance(lex);
            } else {
                lexer_advance(lex);
            }
        }
    }

    zan_token_kind_t kind;
    if (lexer_peek_ch(lex) == '{') {
        lexer_advance(lex); /* { */
        lex->interp_depth++;
        lex->interp_brace_depth = 0;
        kind = start_kind; /* INTERP_START or INTERP_MID */
    } else {
        /* closing " or EOF */
        if (!lexer_at_end(lex)) lexer_advance(lex); /* " */
        kind = (start_kind == TK_INTERP_START) ? TK_STRING_LIT : TK_INTERP_END;
        lex->interp_depth = 0;
    }

    zan_token_t tok = lexer_make(lex, kind, loc);
    tok.str_val.str = zan_arena_strdup(lex->arena, buf, bi);
    tok.str_val.len = (uint32_t)bi;
    return tok;
}

/* ---- verbatim string @"..." ---- */

static zan_token_t lexer_verbatim_string(zan_lexer_t *lex) {
    zan_loc_t loc = lexer_loc(lex);
    lexer_advance(lex); /* @ */
    lexer_advance(lex); /* " */

    char buf[4096];
    size_t bi = 0;

    while (!lexer_at_end(lex)) {
        if (lexer_peek_ch(lex) == '"') {
            if (lexer_peek_ch2(lex) == '"') {
                /* escaped quote "" → " */
                lexer_advance(lex);
                lexer_advance(lex);
                if (bi < sizeof(buf) - 1) buf[bi++] = '"';
            } else {
                lexer_advance(lex); /* closing " */
                break;
            }
        } else {
            if (bi < sizeof(buf) - 1) {
                buf[bi++] = lexer_advance(lex);
            } else {
                lexer_advance(lex);
            }
        }
    }

    zan_token_t tok = lexer_make(lex, TK_STRING_LIT, loc);
    tok.str_val.str = zan_arena_strdup(lex->arena, buf, bi);
    tok.str_val.len = (uint32_t)bi;
    return tok;
}

/* ---- main tokenizer ---- */

zan_token_t zan_lexer_next(zan_lexer_t *lex) {
    lexer_skip_whitespace(lex);

    if (lexer_at_end(lex)) {
        return lexer_make(lex, TK_EOF, lexer_loc(lex));
    }

    zan_loc_t loc = lexer_loc(lex);
    char ch = lexer_peek_ch(lex);

    /* identifiers and keywords */
    if (isalpha((unsigned char)ch) || ch == '_') {
        /* check for @"..." verbatim string */
        if (ch == '@' && lexer_peek_ch2(lex) == '"') {
            /* handled below */
        }
        return lexer_ident_or_keyword(lex);
    }

    /* number literals */
    if (isdigit((unsigned char)ch)) {
        return lexer_number(lex);
    }

    /* string literal */
    if (ch == '"') {
        return lexer_string(lex);
    }

    /* char literal */
    if (ch == '\'') {
        return lexer_char(lex);
    }

    /* verbatim string @"..." */
    if (ch == '@' && lexer_peek_ch2(lex) == '"') {
        return lexer_verbatim_string(lex);
    }

    /* interpolated string $"..." */
    if (ch == '$' && lexer_peek_ch2(lex) == '"') {
        lexer_advance(lex); /* $ */
        lexer_advance(lex); /* " */
        return lexer_interp_string_segment(lex, TK_INTERP_START);
    }

    /* operators and punctuation */
    lexer_advance(lex);

    switch (ch) {
    case '(': return lexer_make(lex, TK_LPAREN, loc);
    case ')': return lexer_make(lex, TK_RPAREN, loc);
    case '{':
        if (lex->interp_depth > 0) lex->interp_brace_depth++;
        return lexer_make(lex, TK_LBRACE, loc);
    case '}':
        if (lex->interp_depth > 0 && lex->interp_brace_depth == 0) {
            /* end of interpolation expression — scan next text segment */
            return lexer_interp_string_segment(lex, TK_INTERP_MID);
        }
        if (lex->interp_depth > 0) lex->interp_brace_depth--;
        return lexer_make(lex, TK_RBRACE, loc);
    case '[': return lexer_make(lex, TK_LBRACKET, loc);
    case ']': return lexer_make(lex, TK_RBRACKET, loc);
    case ';': return lexer_make(lex, TK_SEMICOLON, loc);
    case ':': return lexer_make(lex, TK_COLON, loc);
    case ',': return lexer_make(lex, TK_COMMA, loc);
    case '~': return lexer_make(lex, TK_TILDE, loc);

    case '.':
        if (lexer_match(lex, '.')) return lexer_make(lex, TK_DOTDOT, loc);
        return lexer_make(lex, TK_DOT, loc);

    case '?':
        if (lexer_match(lex, '.')) return lexer_make(lex, TK_QUESTION_DOT, loc);
        if (lexer_match(lex, '?')) return lexer_make(lex, TK_QUESTION_QUESTION, loc);
        return lexer_make(lex, TK_QUESTION, loc);

    case '+':
        if (lexer_match(lex, '+')) return lexer_make(lex, TK_PLUS_PLUS, loc);
        if (lexer_match(lex, '=')) return lexer_make(lex, TK_PLUS_EQ, loc);
        return lexer_make(lex, TK_PLUS, loc);

    case '-':
        if (lexer_match(lex, '-')) return lexer_make(lex, TK_MINUS_MINUS, loc);
        if (lexer_match(lex, '=')) return lexer_make(lex, TK_MINUS_EQ, loc);
        return lexer_make(lex, TK_MINUS, loc);

    case '*':
        if (lexer_match(lex, '=')) return lexer_make(lex, TK_STAR_EQ, loc);
        return lexer_make(lex, TK_STAR, loc);

    case '/':
        if (lexer_match(lex, '=')) return lexer_make(lex, TK_SLASH_EQ, loc);
        return lexer_make(lex, TK_SLASH, loc);

    case '%':
        if (lexer_match(lex, '=')) return lexer_make(lex, TK_PERCENT_EQ, loc);
        return lexer_make(lex, TK_PERCENT, loc);

    case '<':
        if (lexer_match(lex, '<')) {
            if (lexer_match(lex, '=')) return lexer_make(lex, TK_LESS_LESS_EQ, loc);
            return lexer_make(lex, TK_LESS_LESS, loc);
        }
        if (lexer_match(lex, '=')) return lexer_make(lex, TK_LESS_EQ, loc);
        return lexer_make(lex, TK_LESS, loc);

    case '>':
        if (lexer_match(lex, '>')) {
            if (lexer_match(lex, '=')) return lexer_make(lex, TK_GREATER_GREATER_EQ, loc);
            return lexer_make(lex, TK_GREATER_GREATER, loc);
        }
        if (lexer_match(lex, '=')) return lexer_make(lex, TK_GREATER_EQ, loc);
        return lexer_make(lex, TK_GREATER, loc);

    case '=':
        if (lexer_match(lex, '=')) return lexer_make(lex, TK_EQ_EQ, loc);
        if (lexer_match(lex, '>')) return lexer_make(lex, TK_ARROW, loc);
        return lexer_make(lex, TK_EQ, loc);

    case '!':
        if (lexer_match(lex, '=')) return lexer_make(lex, TK_BANG_EQ, loc);
        return lexer_make(lex, TK_BANG, loc);

    case '&':
        if (lexer_match(lex, '&')) return lexer_make(lex, TK_AMP_AMP, loc);
        if (lexer_match(lex, '=')) return lexer_make(lex, TK_AMP_EQ, loc);
        return lexer_make(lex, TK_AMP, loc);

    case '|':
        if (lexer_match(lex, '|')) return lexer_make(lex, TK_PIPE_PIPE, loc);
        if (lexer_match(lex, '=')) return lexer_make(lex, TK_PIPE_EQ, loc);
        return lexer_make(lex, TK_PIPE, loc);

    case '^':
        if (lexer_match(lex, '=')) return lexer_make(lex, TK_CARET_EQ, loc);
        return lexer_make(lex, TK_CARET, loc);

    default:
        zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                      "unexpected character '%c' (0x%02x)", ch, (unsigned char)ch);
        return lexer_make(lex, TK_INVALID, loc);
    }
}

zan_token_t zan_lexer_peek(zan_lexer_t *lex) {
    /* save state */
    size_t pos = lex->pos;
    uint32_t line = lex->line;
    uint32_t col = lex->col;
    int idepth = lex->interp_depth;
    int ibrace = lex->interp_brace_depth;

    zan_token_t tok = zan_lexer_next(lex);

    /* restore state */
    lex->pos = pos;
    lex->line = line;
    lex->col = col;
    lex->interp_depth = idepth;
    lex->interp_brace_depth = ibrace;

    return tok;
}
