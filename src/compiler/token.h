/* token.h -- Token kinds for the Zan lexer.
 *
 * Generated from SPEC.md Section 2.
 */

#ifndef ZAN_TOKEN_H
#define ZAN_TOKEN_H

typedef enum {
    /* sentinel */
    TK_INVALID = 0,
    TK_EOF,

    /* literals */
    TK_INT_LIT,         /* 42, 0xFF, 0b1010, 0o77 */
    TK_FLOAT_LIT,       /* 3.14, 1.0e10 */
    TK_STRING_LIT,      /* "hello" */
    TK_CHAR_LIT,        /* 'A' */
    TK_INTERP_START,    /* $" ... start of interpolated string */
    TK_INTERP_MID,      /* ... } text { ... middle fragment */
    TK_INTERP_END,      /* ... } text " end fragment */

    /* identifier */
    TK_IDENT,

    /* keywords */
    TK_ABSTRACT,
    TK_AS,
    TK_ASYNC,
    TK_AWAIT,
    TK_BASE,
    TK_BOOL,
    TK_BREAK,
    TK_BYTE,
    TK_CASE,
    TK_CATCH,
    TK_CHAR,
    TK_CLASS,
    TK_CONST,
    TK_CONTINUE,
    TK_DEFAULT,
    TK_DELEGATE,
    TK_DO,
    TK_DOUBLE,
    TK_ELSE,
    TK_ENUM,
    TK_EXTERN,
    TK_FALSE,
    TK_FINALLY,
    TK_FLOAT,
    TK_FOR,
    TK_FOREACH,
    TK_GET,
    TK_IF,
    TK_IN,
    TK_INT,
    TK_INTERFACE,
    TK_INTERNAL,
    TK_IS,
    TK_LET,
    TK_LONG,
    TK_NAMESPACE,
    TK_NEW,
    TK_NINT,
    TK_NULL,
    TK_OPERATOR,
    TK_OBJECT,
    TK_OUT,
    TK_OVERRIDE,
    TK_PRIVATE,
    TK_PROTECTED,
    TK_PUBLIC,
    TK_READONLY,
    TK_REF,
    TK_RETURN,
    TK_SEALED,
    TK_SET,
    TK_SHORT,
    TK_SIZEOF,
    TK_STATIC,
    TK_STRING,
    TK_STRUCT,
    TK_SWITCH,
    TK_THIS,
    TK_THROW,
    TK_TRUE,
    TK_TRY,
    TK_TYPEOF,
    TK_UNSAFE,
    TK_USING,
    TK_VALUE,
    TK_VAR,
    TK_VIRTUAL,
    TK_VOID,
    TK_WEAK,
    TK_WHEN,
    TK_WHERE,
    TK_WHILE,

    /* punctuation & operators */
    TK_LPAREN,          /* ( */
    TK_RPAREN,          /* ) */
    TK_LBRACE,          /* { */
    TK_RBRACE,          /* } */
    TK_LBRACKET,        /* [ */
    TK_RBRACKET,        /* ] */
    TK_SEMICOLON,       /* ; */
    TK_COLON,           /* : */
    TK_COMMA,           /* , */
    TK_DOT,             /* . */
    TK_DOTDOT,          /* .. */
    TK_QUESTION,        /* ? */
    TK_QUESTION_DOT,    /* ?. */
    TK_QUESTION_QUESTION, /* ?? */
    TK_TILDE,           /* ~ */
    TK_ARROW,           /* => */

    /* arithmetic */
    TK_PLUS,            /* + */
    TK_MINUS,           /* - */
    TK_STAR,            /* * */
    TK_SLASH,           /* / */
    TK_PERCENT,         /* % */

    /* increment / decrement */
    TK_PLUS_PLUS,       /* ++ */
    TK_MINUS_MINUS,     /* -- */

    /* comparison */
    TK_LESS,            /* < */
    TK_GREATER,         /* > */
    TK_LESS_EQ,         /* <= */
    TK_GREATER_EQ,      /* >= */
    TK_EQ_EQ,           /* == */
    TK_BANG_EQ,         /* != */

    /* logical */
    TK_BANG,            /* ! */
    TK_AMP_AMP,         /* && */
    TK_PIPE_PIPE,       /* || */

    /* bitwise */
    TK_AMP,             /* & */
    TK_PIPE,            /* | */
    TK_CARET,           /* ^ */
    TK_LESS_LESS,       /* << */
    TK_GREATER_GREATER, /* >> */

    /* assignment */
    TK_EQ,              /* = */
    TK_PLUS_EQ,         /* += */
    TK_MINUS_EQ,        /* -= */
    TK_STAR_EQ,         /* *= */
    TK_SLASH_EQ,        /* /= */
    TK_PERCENT_EQ,      /* %= */
    TK_AMP_EQ,          /* &= */
    TK_PIPE_EQ,         /* |= */
    TK_CARET_EQ,        /* ^= */
    TK_LESS_LESS_EQ,    /* <<= */
    TK_GREATER_GREATER_EQ, /* >>= */

    TK__COUNT
} zan_token_kind_t;

/* human-readable token name (defined in lexer.c) */
const char *zan_token_kind_name(zan_token_kind_t kind);

#endif /* ZAN_TOKEN_H */
