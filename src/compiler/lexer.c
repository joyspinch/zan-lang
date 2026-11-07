
#include "lexer.h"
#include "arena.h"
#include "diag.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

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
    {"defer",     TK_DEFER},
    {"delegate",  TK_DELEGATE},
    {"do",        TK_DO},
    {"decimal",   TK_DECIMAL},
    {"double",    TK_DOUBLE},
    {"else",      TK_ELSE},
    {"enum",      TK_ENUM},
    {"extern",    TK_EXTERN},
    {"false",     TK_FALSE},
    {"finally",   TK_FINALLY},
    {"fixed",     TK_FIXED},
    {"float",     TK_FLOAT},
    {"goto",      TK_GOTO},
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
    {"lock",      TK_LOCK},
    {"long",      TK_LONG},
    {"namespace", TK_NAMESPACE},
    {"new",       TK_NEW},
    {"not",       TK_NOT},
    {"nint",      TK_NINT},
    {"null",      TK_NULL},
    {"operator",  TK_OPERATOR},
    {"object",    TK_OBJECT},
    {"out",       TK_OUT},
    {"override",  TK_OVERRIDE},
    {"private",   TK_PRIVATE},
    {"protected", TK_PROTECTED},
    {"public",    TK_PUBLIC},
    {"readonly",  TK_READONLY},
    {"ref",       TK_REF},
    {"return",    TK_RETURN},
    {"sbyte",     TK_SBYTE},
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
    {"uint",      TK_UINT},
    {"ulong",     TK_ULONG},
    {"unsafe",    TK_UNSAFE},
    {"ushort",    TK_USHORT},
    {"using",     TK_USING},
    {"var",       TK_VAR},
    {"virtual",   TK_VIRTUAL},
    {"void",      TK_VOID},
    {"weak",      TK_WEAK},
    {"when",      TK_WHEN},
    {"where",     TK_WHERE},
    {"while",     TK_WHILE},
};

#define KEYWORD_COUNT (sizeof(s_keywords) / sizeof(s_keywords[0]))

static const char *s_token_names[TK__COUNT] = {
    [TK_INVALID]     = "INVALID",
    [TK_EOF]         = "EOF",
    [TK_INT_LIT]     = "INT_LIT",
    [TK_FLOAT_LIT]   = "FLOAT_LIT",
    [TK_DECIMAL_LIT] = "DECIMAL_LIT",
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
    [TK_GREATER_GREATER_GREATER] = ">>>",
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
    [TK_GREATER_GREATER_GREATER_EQ] = ">>>=",
};

const char *zan_token_kind_name(zan_token_kind_t kind) {
    if (kind >= 0 && kind < TK__COUNT && s_token_names[kind]) {
        return s_token_names[kind];
    }
    for (size_t i = 0; i < KEYWORD_COUNT; i++) {
        if (s_keywords[i].kind == kind) return s_keywords[i].name;
    }
    return "???";
}

/* 关键字 token → 词面文本；不是关键字返回 NULL。
 * 解析器在"这里应该是个名字"的位置撞上关键字时靠它拼诊断——
 * zan_token_kind_name 也能反查词面，但它对未知 kind 返回 "???"，
 * 没法用来判断"当前 token 是不是保留字"。 */
const char *zan_lexer_keyword_text(zan_token_kind_t kind) {
    for (size_t i = 0; i < KEYWORD_COUNT; i++) {
        if (s_keywords[i].kind == kind) return s_keywords[i].name;
    }
    return NULL;
}

bool zan_is_keyword(const char *name) {
    if (!name) return false;
    for (size_t i = 0; i < KEYWORD_COUNT; i++) {
        if (strcmp(s_keywords[i].name, name) == 0) return true;
    }
    return false;
}

void zan_lexer_init(zan_lexer_t *lex, const char *source, size_t len,
                    uint32_t file_id, zan_arena_t *arena, zan_diag_t *diag) {
    memset(lex, 0, sizeof(*lex));
    if (source && len >= 3 &&
        (unsigned char)source[0] == 0xEF &&
        (unsigned char)source[1] == 0xBB &&
        (unsigned char)source[2] == 0xBF) {
        source += 3;
        len -= 3;
    }
    lex->source = source;
    lex->source_len = len;
    lex->pos = 0;
    lex->line = 1;
    lex->col = 1;
    lex->file_id = file_id;
    lex->arena = arena;
    lex->diag = diag;
    lex->defines = NULL;
    lex->define_count = 0;
    lex->define_cap = 0;
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

static inline char lexer_peek_ch3(zan_lexer_t *lex) {
    if (lex->pos + 2 >= lex->source_len) return '\0';
    return lex->source[lex->pos + 2];
}

static inline char lexer_peek_ch4(zan_lexer_t *lex) {
    if (lex->pos + 3 >= lex->source_len) return '\0';
    return lex->source[lex->pos + 3];
}

static inline char lexer_peek_ch5(zan_lexer_t *lex) {
    if (lex->pos + 4 >= lex->source_len) return '\0';
    return lex->source[lex->pos + 4];
}

static inline char lexer_advance(zan_lexer_t *lex) {
    if (lexer_at_end(lex)) return '\0';
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


/* Composite Dispatcher Submodules */
#include "lexer/lexer_pp.c"
#include "lexer/lexer_tokens.c"
#include "lexer/lexer_string.c"
#include "lexer/lexer_scan.c"
