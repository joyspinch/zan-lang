/* 内部辅助实现 */

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
    /* 内部辅助实现 */
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
    /* 编译器代码生成与运行时系统底层调用契约 */
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
    lex->at_line_start = 1;
    /* 内部辅助逻辑 */
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

static inline char lexer_advance(zan_lexer_t *lex) {
    /* 编译器代码生成与运行时系统底层调用契约 */
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

void zan_lexer_define(zan_lexer_t *lex, const char *name, const char *value) {
    if (!name || !lex->arena) return;
    if (lex->define_count >= lex->define_cap) {
        int new_cap = lex->define_cap == 0 ? 16 : lex->define_cap * 2;
        if (new_cap > ZAN_PP_MAX_DEFINES) new_cap = ZAN_PP_MAX_DEFINES;
        if (lex->define_count >= new_cap) return;
        zan_pp_define_t *new_defs = (zan_pp_define_t *)zan_arena_alloc(
            lex->arena, sizeof(zan_pp_define_t) * (size_t)new_cap);
        if (!new_defs) return;
        if (lex->defines && lex->define_count > 0) {
            memcpy(new_defs, lex->defines, sizeof(zan_pp_define_t) * (size_t)lex->define_count);
        }
        lex->defines = new_defs;
        lex->define_cap = new_cap;
    }
    zan_pp_define_t *d = &lex->defines[lex->define_count++];
    strncpy(d->name, name, 63); d->name[63] = '\0';
    if (value) { strncpy(d->value, value, 255); d->value[255] = '\0'; }
    else d->value[0] = '\0';
}

static int pp_is_defined(zan_lexer_t *lex, const char *name) {
    if (!lex->defines || !name) return 0;
    for (int i = 0; i < lex->define_count; i++) {
        if (strcmp(lex->defines[i].name, name) == 0) return 1;
    }
    return 0;
}

static const char *pp_get_value(zan_lexer_t *lex, const char *name) __attribute__((unused));
static const char *pp_get_value(zan_lexer_t *lex, const char *name) {
    if (!lex->defines || !name) return NULL;
    for (int i = 0; i < lex->define_count; i++) {
        if (strcmp(lex->defines[i].name, name) == 0) return lex->defines[i].value;
    }
    return NULL;
}

static void pp_undef(zan_lexer_t *lex, const char *name) {
    if (!lex->defines || !name) return;
    for (int i = 0; i < lex->define_count; i++) {
        if (strcmp(lex->defines[i].name, name) == 0) {
            lex->defines[i] = lex->defines[--lex->define_count];
            return;
        }
    }
}

static int pp_active(zan_lexer_t *lex) {
    /* 模块核心语义抽象与接口调用契约 */
    if (lex->cond_overflow > 0) return 0;
    for (int i = 0; i < lex->cond_depth; i++) {
        if (!lex->cond_stack[i]) return 0;
    }
    return 1;
}

static void pp_skip_to_eol(zan_lexer_t *lex) {
    while (!lexer_at_end(lex) && lexer_peek_ch(lex) != '\n') {
        lexer_advance(lex);
    }
}

static void pp_skip_hspaces(zan_lexer_t *lex) {
    while (!lexer_at_end(lex) && (lexer_peek_ch(lex) == ' ' || lexer_peek_ch(lex) == '\t')) {
        lexer_advance(lex);
    }
}

/* 内部辅助逻辑 */
static int pp_word_is_conditional(const char *src, size_t len, size_t p) {
    static const char *const words[] = { "endif", "else", "elif" };
    for (int w = 0; w < 3; w++) {
        size_t n = strlen(words[w]);
        if (p + n > len) continue;
        if (memcmp(src + p, words[w], n) != 0) continue;
        char after = (p + n < len) ? src[p + n] : '\0';
        if (!(isalnum((unsigned char)after) || after == '_')) return 1;
    }
    return 0;
}

/* 核心系统底层抽象与内存语义契约 */
static void pp_end_directive_line(zan_lexer_t *lex, int honor_conditional) {
    if (!honor_conditional) { pp_skip_to_eol(lex); return; }
    size_t stop = lex->pos;
    int found = 0;
    while (stop < lex->source_len && lex->source[stop] != '\n') {
        char c = lex->source[stop];
        if (c == '/' && stop + 1 < lex->source_len
            && lex->source[stop + 1] == '/') {
            /* 模块核心语义抽象与接口调用契约 */
            break;
        }
        if (c == '/' && stop + 1 < lex->source_len
            && lex->source[stop + 1] == '*') {
            /* 内部辅助逻辑 */
            size_t end = stop + 2;
            while (end < lex->source_len && lex->source[end] != '\n'
                   && !(lex->source[end] == '*' && end + 1 < lex->source_len
                        && lex->source[end + 1] == '/'))
                end++;
            if (end < lex->source_len && lex->source[end] == '*'
                && end + 1 < lex->source_len && lex->source[end + 1] == '/') {
                stop = end + 2;
                continue;
            }
            /* 模块核心语义抽象与接口调用契约 */
            break;
        }
        if (c == '#') {
            size_t p = stop + 1;
            while (p < lex->source_len && (lex->source[p] == ' ' || lex->source[p] == '\t')) p++;
            if (pp_word_is_conditional(lex->source, lex->source_len, p)) { found = 1; break; }
        }
        stop++;
    }
    if (found) {
        while (lex->pos < stop) lexer_advance(lex);
        return;
    }
    pp_skip_to_eol(lex);
}

static void pp_read_ident(zan_lexer_t *lex, char *buf, int maxlen) {
    int i = 0;
    while (!lexer_at_end(lex) && (isalnum((unsigned char)lexer_peek_ch(lex)) || lexer_peek_ch(lex) == '_')) {
        if (i < maxlen - 1) buf[i++] = lexer_peek_ch(lex);
        lexer_advance(lex);
    }
    buf[i] = '\0';
}

/* 内部辅助逻辑 */
static int pp_eval_expr(zan_lexer_t *lex, int depth);
#define ZAN_PP_EVAL_MAX_DEPTH 2048

static int pp_eval_atom(zan_lexer_t *lex, int depth) {
    pp_skip_hspaces(lex);
    char ch = lexer_peek_ch(lex);

    if (ch == '!') {
        lexer_advance(lex);
        if (depth >= ZAN_PP_EVAL_MAX_DEPTH) return 0;
        return !pp_eval_atom(lex, depth + 1);
    }
    if (ch == '(') {
        lexer_advance(lex);
        if (depth >= ZAN_PP_EVAL_MAX_DEPTH) return 0;
        int v = pp_eval_expr(lex, depth + 1);
        pp_skip_hspaces(lex);
        if (lexer_peek_ch(lex) == ')') lexer_advance(lex);
        return v;
    }
    if (isdigit((unsigned char)ch)) {
        /* 内部辅助逻辑 */
        long long v = 0;
        while (!lexer_at_end(lex) && isdigit((unsigned char)lexer_peek_ch(lex))) {
            v = v * 10 + (lexer_advance(lex) - '0');
            if (v > INT_MAX) v = INT_MAX;
        }
        return (int)v;
    }
    if (isalpha((unsigned char)ch) || ch == '_') {
        char name[64];
        pp_read_ident(lex, name, sizeof(name));
        if (strcmp(name, "true") == 0) return 1;
        if (strcmp(name, "false") == 0) return 0;
        if (strcmp(name, "defined") == 0) {
            pp_skip_hspaces(lex);
            int paren = 0;
            if (lexer_peek_ch(lex) == '(') { lexer_advance(lex); paren = 1; }
            pp_skip_hspaces(lex);
            char n2[64]; pp_read_ident(lex, n2, sizeof(n2));
            if (paren) { pp_skip_hspaces(lex); if (lexer_peek_ch(lex)==')') lexer_advance(lex); }
            return pp_is_defined(lex, n2);
        }
        return pp_is_defined(lex, name);
    }
    return 0;
}

static int pp_eval_expr(zan_lexer_t *lex, int depth) {
    int left = pp_eval_atom(lex, depth);
    for (;;) {
        pp_skip_hspaces(lex);
        char c1 = lexer_peek_ch(lex);
        if (c1 == '&' && !lexer_at_end(lex) && lex->pos+1 < lex->source_len && lex->source[lex->pos+1] == '&') {
            lexer_advance(lex); lexer_advance(lex);
            int right = pp_eval_atom(lex, depth);
            left = left && right;
        } else if (c1 == '|' && !lexer_at_end(lex) && lex->pos+1 < lex->source_len && lex->source[lex->pos+1] == '|') {
            lexer_advance(lex); lexer_advance(lex);
            int right = pp_eval_atom(lex, depth);
            left = left || right;
        } else if (c1 == '=' && !lexer_at_end(lex) && lex->pos+1 < lex->source_len && lex->source[lex->pos+1] == '=') {
            lexer_advance(lex); lexer_advance(lex);
            int right = pp_eval_atom(lex, depth);
            left = (left == right);
        } else if (c1 == '!' && !lexer_at_end(lex) && lex->pos+1 < lex->source_len && lex->source[lex->pos+1] == '=') {
            lexer_advance(lex); lexer_advance(lex);
            int right = pp_eval_atom(lex, depth);
            left = (left != right);
        } else {
            break;
        }
    }
    return left;
}

static void pp_handle_directive(zan_lexer_t *lex) {
    pp_skip_hspaces(lex);
    char dir[32];
    pp_read_ident(lex, dir, sizeof(dir));

    if (strcmp(dir, "define") == 0) {
        pp_skip_hspaces(lex);
        char name[64]; pp_read_ident(lex, name, sizeof(name));
        pp_skip_hspaces(lex);
        char val[256]; int vi = 0;
        while (!lexer_at_end(lex) && lexer_peek_ch(lex) != '\n' && vi < 255)
            val[vi++] = lexer_advance(lex);
        val[vi] = '\0';
        while (vi > 0 && (val[vi-1]==' '||val[vi-1]=='\t'||val[vi-1]=='\r')) val[--vi]='\0';
        if (pp_active(lex)) zan_lexer_define(lex, name, val);
    } else if (strcmp(dir, "undef") == 0) {
        pp_skip_hspaces(lex); char name[64]; pp_read_ident(lex, name, sizeof(name));
        if (pp_active(lex)) pp_undef(lex, name);
    } else if (strcmp(dir, "ifdef") == 0) {
        pp_skip_hspaces(lex); char name[64]; pp_read_ident(lex, name, sizeof(name));
        if (lex->cond_depth < ZAN_PP_MAX_COND_DEPTH) {
            int active = pp_active(lex) && pp_is_defined(lex, name);
            lex->cond_stack[lex->cond_depth] = active;
            lex->cond_seen_true[lex->cond_depth] = active;
            lex->cond_depth++;
        } else {
            /* 内部辅助实现 */
            zan_diag_emit(lex->diag, DIAG_ERROR, lexer_loc(lex),
                          "conditional-compilation nesting too deep (max %d)",
                          ZAN_PP_MAX_COND_DEPTH);
            lex->cond_overflow++;
        }
    } else if (strcmp(dir, "ifndef") == 0) {
        pp_skip_hspaces(lex); char name[64]; pp_read_ident(lex, name, sizeof(name));
        if (lex->cond_depth < ZAN_PP_MAX_COND_DEPTH) {
            int active = pp_active(lex) && !pp_is_defined(lex, name);
            lex->cond_stack[lex->cond_depth] = active;
            lex->cond_seen_true[lex->cond_depth] = active;
            lex->cond_depth++;
        } else {
            zan_diag_emit(lex->diag, DIAG_ERROR, lexer_loc(lex),
                          "conditional-compilation nesting too deep (max %d)",
                          ZAN_PP_MAX_COND_DEPTH);
            lex->cond_overflow++;
        }
    } else if (strcmp(dir, "if") == 0) {
        if (lex->cond_depth < ZAN_PP_MAX_COND_DEPTH) {
            int parent_active = pp_active(lex);
            int val = 0;
            if (parent_active) val = pp_eval_expr(lex, 0);
            lex->cond_stack[lex->cond_depth] = parent_active && val;
            lex->cond_seen_true[lex->cond_depth] = parent_active && val;
            lex->cond_depth++;
        } else {
            zan_diag_emit(lex->diag, DIAG_ERROR, lexer_loc(lex),
                          "conditional-compilation nesting too deep (max %d)",
                          ZAN_PP_MAX_COND_DEPTH);
            lex->cond_overflow++;
        }
    } else if (strcmp(dir, "elif") == 0) {
        /* 编译器代码生成与运行时系统底层调用契约 */
        if (lex->cond_overflow > 0) {
            /* nothing to select on */
        } else if (lex->cond_depth > 0) {
            int idx = lex->cond_depth - 1;
            if (lex->cond_seen_true[idx]) {
                lex->cond_stack[idx] = 0;
            } else {
                int parent = 1;
                for (int i = 0; i < idx; i++) { if (!lex->cond_stack[i]) { parent=0; break; } }
                int val = 0;
                if (parent) val = pp_eval_expr(lex, 0);
                lex->cond_stack[idx] = parent && val;
                if (parent && val) lex->cond_seen_true[idx] = 1;
            }
        }
    } else if (strcmp(dir, "else") == 0) {
        /* 底层系统交互与数据协议契约 */
        if (lex->cond_overflow > 0) {
            /* nothing to flip */
        } else if (lex->cond_depth > 0) {
            int idx = lex->cond_depth - 1;
            if (lex->cond_seen_true[idx]) {
                lex->cond_stack[idx] = 0;
            } else {
                int parent = 1;
                for (int i = 0; i < idx; i++) { if (!lex->cond_stack[i]) { parent=0; break; } }
                lex->cond_stack[idx] = parent;
                lex->cond_seen_true[idx] = 1;
            }
        }
    } else if (strcmp(dir, "endif") == 0) {
        if (lex->cond_overflow > 0) lex->cond_overflow--;
        else if (lex->cond_depth > 0) lex->cond_depth--;
    } else if (strcmp(dir, "error") == 0) {
        if (pp_active(lex)) {
            pp_skip_hspaces(lex);
            char msg[256]; int mi = 0;
            while (!lexer_at_end(lex) && lexer_peek_ch(lex) != '\n' && mi < 255)
                msg[mi++] = lexer_advance(lex);
            msg[mi] = '\0';
            zan_diag_emit(lex->diag, DIAG_ERROR, lexer_loc(lex), "#error %s", msg);
        }
    } else if (strcmp(dir, "warning") == 0) {
        if (pp_active(lex)) {
            pp_skip_hspaces(lex);
            char msg[256]; int mi = 0;
            while (!lexer_at_end(lex) && lexer_peek_ch(lex) != '\n' && mi < 255)
                msg[mi++] = lexer_advance(lex);
            msg[mi] = '\0';
            zan_diag_emit(lex->diag, DIAG_WARNING, lexer_loc(lex), "#warning %s", msg);
        }
    }
    /* 内部辅助实现 */
    int honor_conditional =
        strcmp(dir, "if") == 0 || strcmp(dir, "ifdef") == 0 ||
        strcmp(dir, "ifndef") == 0 || strcmp(dir, "elif") == 0 ||
        strcmp(dir, "else") == 0 || strcmp(dir, "endif") == 0;
    pp_end_directive_line(lex, honor_conditional);
}

/* 核心系统底层抽象与内存语义契约 */

static void lexer_skip_whitespace(zan_lexer_t *lex) {
    for (;;) {
        if (lexer_at_end(lex)) return;
        char ch = lexer_peek_ch(lex);

        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            lexer_advance(lex);
            continue;
        }

        if (ch == '/' && lexer_peek_ch2(lex) == '/') {
            while (!lexer_at_end(lex) && lexer_peek_ch(lex) != '\n') {
                lexer_advance(lex);
            }
            continue;
        }

        if (ch == '/' && lexer_peek_ch2(lex) == '*') {
            zan_loc_t start_loc = lexer_loc(lex);
            lexer_advance(lex); /* / */
            lexer_advance(lex); 
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

    for (size_t i = 0; i < KEYWORD_COUNT; i++) {
        if (strlen(s_keywords[i].name) == len &&
            memcmp(s_keywords[i].name, text, len) == 0) {
            return lexer_make(lex, s_keywords[i].kind, loc);
        }
    }

    zan_token_t tok = lexer_make(lex, TK_IDENT, loc);
    tok.str_val.str = zan_arena_strdup(lex->arena, text, len);
    tok.str_val.len = (uint32_t)len;
    return tok;
}

/* 核心系统底层抽象与内存语义契约 */

/* 内部辅助逻辑 */
static int lexer_int_suffix(zan_lexer_t *lex) {
    int enc = 0;
    char c = lexer_peek_ch(lex);
    if (c == 'L' || c == 'l') {
        enc |= 1;
        lexer_advance(lex);
        c = lexer_peek_ch(lex);
        if (c == 'U' || c == 'u') { enc |= 2; lexer_advance(lex); }
    } else if (c == 'U' || c == 'u') {
        enc |= 2;
        lexer_advance(lex);
        c = lexer_peek_ch(lex);
        if (c == 'L' || c == 'l') { enc |= 1; lexer_advance(lex); }
    }
    return enc;
}

/* 内部辅助实现 */
static int64_t lexer_radix_int_value(zan_lexer_t *lex, zan_loc_t loc,
                                     const char *buf, int radix,
                                     int *lit_suffix) {
    errno = 0;
    unsigned long long uv = strtoull(buf, NULL, radix);
    if (errno == ERANGE) {
        zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                      "integer literal is too large for 'ulong'");
        *lit_suffix = 0;
        return 0;
    }
    if (uv <= 0x7FFFFFFFFFFFFFFFULL) return (int64_t)uv;
    if (*lit_suffix == 1) {
        zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                      "integer literal is too large for 'long'");
        return (int64_t)uv;
    }
    if (*lit_suffix == 0) *lit_suffix = 3; /* 核心系统底层抽象与内存语义契约 */
    return (int64_t)uv;
}

static zan_token_t lexer_number(zan_lexer_t *lex) {
    zan_loc_t loc = lexer_loc(lex);
    size_t start = lex->pos;
    bool is_float = false;

    if (lexer_peek_ch(lex) == '0' && lex->pos + 1 < lex->source_len) {
        char next = lex->source[lex->pos + 1];
        if (next == 'x' || next == 'X') {
            lexer_advance(lex); /* 0 */
            lexer_advance(lex); /* x */
            bool digit_seen = false;
            while (!lexer_at_end(lex)) {
                char ch = lexer_peek_ch(lex);
                if (isxdigit((unsigned char)ch) || ch == '_') {
                    if (ch != '_') digit_seen = true;
                    lexer_advance(lex);
                } else {
                    break;
                }
            }
            /* 底层系统交互与数据协议契约 */
            if (!digit_seen) {
                zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                              "hexadecimal literal requires at least one digit after '0x'");
                return lexer_make(lex, TK_INVALID, loc);
            }
            zan_token_t tok = lexer_make(lex, TK_INT_LIT, loc);
            tok.lit_suffix = lexer_int_suffix(lex);
            tok.lit_radix = 16;
            char buf[64];
            size_t bi = 0;
            int lit_truncated = 0;
            for (size_t i = start + 2; i < lex->pos; i++) {
                if (lex->source[i] != '_' && lex->source[i] != 'L'
                    && lex->source[i] != 'l' && lex->source[i] != 'U'
                    && lex->source[i] != 'u') {
                    if (bi >= sizeof(buf) - 1) { lit_truncated = 1; break; }
                    buf[bi++] = lex->source[i];
                }
            }
            buf[bi] = '\0';
            if (lit_truncated)
                zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                              "integer literal is too long");
            tok.int_val = lexer_radix_int_value(lex, loc, buf, 16,
                                                &tok.lit_suffix);
            return tok;
        }
        if (next == 'b' || next == 'B') {
            lexer_advance(lex); /* 0 */
            lexer_advance(lex); /* b */
            bool digit_seen = false;
            while (!lexer_at_end(lex)) {
                char ch = lexer_peek_ch(lex);
                if (ch == '0' || ch == '1' || ch == '_') {
                    if (ch != '_') digit_seen = true;
                    lexer_advance(lex);
                } else {
                    break;
                }
            }
            if (!digit_seen) {
                zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                              "binary literal requires at least one digit after '0b'");
                return lexer_make(lex, TK_INVALID, loc);
            }
            zan_token_t tok = lexer_make(lex, TK_INT_LIT, loc);
            tok.lit_suffix = lexer_int_suffix(lex);
            tok.lit_radix = 2;
            char buf[128];
            size_t bi = 0;
            int lit_truncated = 0;
            for (size_t i = start + 2; i < lex->pos; i++) {
                if (lex->source[i] != '_' && lex->source[i] != 'L'
                    && lex->source[i] != 'l' && lex->source[i] != 'U'
                    && lex->source[i] != 'u') {
                    if (bi >= sizeof(buf) - 1) { lit_truncated = 1; break; }
                    buf[bi++] = lex->source[i];
                }
            }
            buf[bi] = '\0';
            if (lit_truncated)
                zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                              "integer literal is too long");
            tok.int_val = lexer_radix_int_value(lex, loc, buf, 2,
                                                &tok.lit_suffix);
            return tok;
        }
        if (next == 'o' || next == 'O') {
            lexer_advance(lex); /* 0 */
            lexer_advance(lex); /* o */
            bool digit_seen = false;
            while (!lexer_at_end(lex)) {
                char ch = lexer_peek_ch(lex);
                if ((ch >= '0' && ch <= '7') || ch == '_') {
                    if (ch != '_') digit_seen = true;
                    lexer_advance(lex);
                } else {
                    break;
                }
            }
            /* 底层系统交互与数据协议契约 */
            if (!digit_seen) {
                char bad = lexer_peek_ch(lex);
                if (bad >= '8' && bad <= '9')
                    zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                                  "octal literal requires digits 0-7 after '0o'");
                else
                    zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                                  "octal literal requires at least one digit after '0o'");
                return lexer_make(lex, TK_INVALID, loc);
            }
            zan_token_t tok = lexer_make(lex, TK_INT_LIT, loc);
            tok.lit_suffix = lexer_int_suffix(lex);
            tok.lit_radix = 8;
            char buf[64];
            size_t bi = 0;
            int lit_truncated = 0;
            for (size_t i = start + 2; i < lex->pos; i++) {
                if (lex->source[i] != '_' && lex->source[i] != 'L'
                    && lex->source[i] != 'l' && lex->source[i] != 'U'
                    && lex->source[i] != 'u') {
                    if (bi >= sizeof(buf) - 1) { lit_truncated = 1; break; }
                    buf[bi++] = lex->source[i];
                }
            }
            buf[bi] = '\0';
            if (lit_truncated)
                zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                              "integer literal is too long");
            tok.int_val = lexer_radix_int_value(lex, loc, buf, 8,
                                                &tok.lit_suffix);
            return tok;
        }
    }

    while (!lexer_at_end(lex)) {
        char ch = lexer_peek_ch(lex);
        if (isdigit((unsigned char)ch) || ch == '_') {
            lexer_advance(lex);
        } else {
            break;
        }
    }

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

    if (lexer_peek_ch(lex) == 'e' || lexer_peek_ch(lex) == 'E') {
        is_float = true;
        lexer_advance(lex); /* e */
        if (lexer_peek_ch(lex) == '+' || lexer_peek_ch(lex) == '-') {
            lexer_advance(lex);
        }
        bool exp_digit_seen = false;
        while (!lexer_at_end(lex) && isdigit((unsigned char)lexer_peek_ch(lex))) {
            lexer_advance(lex);
            exp_digit_seen = true;
        }
        /* 核心系统底层抽象与内存语义契约 */
        if (!exp_digit_seen) {
            zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                          "exponent requires at least one digit after 'e'");
            return lexer_make(lex, TK_INVALID, loc);
        }
    }

    /* suffix: f/F (float), m/M (decimal, mapped to double) */
    if (lexer_peek_ch(lex) == 'f' || lexer_peek_ch(lex) == 'F' ||
        lexer_peek_ch(lex) == 'm' || lexer_peek_ch(lex) == 'M') {
        is_float = true;
        lexer_advance(lex);
    }

    /* 底层系统交互与数据协议契约 */
    int lit_suffix = 0;
    if (!is_float) {
        lit_suffix = lexer_int_suffix(lex);
    }

    char buf[128];
    size_t bi = 0;
    int lit_truncated = 0;
    for (size_t i = start; i < lex->pos; i++) {
        char ch = lex->source[i];
        if (ch != '_' && ch != 'f' && ch != 'F' && ch != 'm' && ch != 'M'
            && ch != 'L' && ch != 'l' && ch != 'U' && ch != 'u') {
            if (bi >= sizeof(buf) - 1) { lit_truncated = 1; break; }
            buf[bi++] = ch;
        }
    }
    buf[bi] = '\0';
    if (lit_truncated) {
        /* 内部辅助逻辑 */
        zan_diag_emit(lex->diag, DIAG_ERROR, loc, is_float
                      ? "floating-point literal is too long"
                      : "integer literal is too long");
    }

    if (is_float) {
        zan_token_t tok = lexer_make(lex, TK_FLOAT_LIT, loc);
        tok.float_val = strtod(buf, NULL);
        return tok;
    } else {
        zan_token_t tok = lexer_make(lex, TK_INT_LIT, loc);
        tok.lit_suffix = lit_suffix;
        tok.lit_radix = 10;
        /* 核心系统底层抽象与内存语义契约 */
        errno = 0;
        long long sv = strtoll(buf, NULL, 10);
        if (errno == ERANGE) {
            /* 内部辅助实现 */
            errno = 0;
            unsigned long long uv = strtoull(buf, NULL, 10);
            if (errno == ERANGE) {
                zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                              "integer literal is too large for 'ulong'");
                tok.int_val = 0;
            } else if (lit_suffix == 1) {
                zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                              "integer literal is too large for 'long'");
                tok.int_val = (int64_t)uv;
            } else {
                tok.lit_suffix = 3;   /* 底层系统交互与数据协议契约 */
                tok.int_val = (int64_t)uv;
            }
        } else {
            tok.int_val = (int64_t)sv;
        }
        return tok;
    }
}

/* 内部辅助实现 */
typedef struct {
    char bytes[4];
    int len;
} zan_esc_out_t;

/* 内部辅助实现 */
static int zan_utf8_encode(uint32_t cp, zan_esc_out_t *out) {
    if (cp < 0x80) {
        out->bytes[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out->bytes[0] = (char)(0xC0 | (cp >> 6));
        out->bytes[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out->bytes[0] = (char)(0xE0 | (cp >> 12));
        out->bytes[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out->bytes[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out->bytes[0] = (char)(0xF0 | (cp >> 18));
    out->bytes[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out->bytes[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out->bytes[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* 内部辅助逻辑 */
static int32_t lexer_hex_escape(zan_lexer_t *lex, zan_loc_t loc, char kind,
                                int ndigits) {
    uint32_t val = 0;
    int got = 0;
    if (kind == 'x') {
        /* 核心系统底层抽象与内存语义契约 */
        while (got < 4) {
            char ch = lexer_peek_ch(lex);
            if (!isxdigit((unsigned char)ch)) break;
            lexer_advance(lex);
            int d = (ch <= '9') ? ch - '0'
                  : (ch <= 'F') ? ch - 'A' + 10 : ch - 'a' + 10;
            val = val * 16 + (uint32_t)d;
            got++;
        }
        if (got == 0) {
            zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                          "'\\x' escape requires at least one hex digit");
            return -1;
        }
    } else {
        /* 核心系统底层抽象与内存语义契约 */
        for (int i = 0; i < ndigits; i++) {
            char ch = lexer_peek_ch(lex);
            if (!isxdigit((unsigned char)ch)) {
                zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                              "'\\u' escape requires %d hex digits", ndigits);
                return -1;
            }
            lexer_advance(lex);
            int d = (ch <= '9') ? ch - '0'
                  : (ch <= 'F') ? ch - 'A' + 10 : ch - 'a' + 10;
            val = val * 16 + (uint32_t)d;
        }
    }
    /* 模块核心语义抽象与接口调用契约 */
    if (val >= 0xD800 && val <= 0xDFFF) {
        zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                      "'\\%c' escape value 0x%X is a surrogate code point",
                      kind, val);
        return -1;
    }
    return (int32_t)val;
}

/* 内部辅助实现 */
static int lexer_escape_seq(zan_lexer_t *lex, zan_loc_t loc, zan_esc_out_t *out) {
    char ch = lexer_advance(lex);
    switch (ch) {
    case 'n': out->bytes[0] = '\n'; return 1;
    case 'r': out->bytes[0] = '\r'; return 1;
    case 't': out->bytes[0] = '\t'; return 1;
    case '\\': out->bytes[0] = '\\'; return 1;
    case '"': out->bytes[0] = '"'; return 1;
    case '\'': out->bytes[0] = '\''; return 1;
    case '0': out->bytes[0] = '\0'; return 1;
    case 'x': {
        int32_t cp = lexer_hex_escape(lex, loc, 'x', 4);
        if (cp < 0) { out->bytes[0] = 'x'; return 1; }
        return zan_utf8_encode((uint32_t)cp, out);
    }
    case 'u': {
        int32_t cp = lexer_hex_escape(lex, loc, 'u', 4);
        if (cp < 0) { out->bytes[0] = 'u'; return 1; }
        return zan_utf8_encode((uint32_t)cp, out);
    }
    default:
        zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                      "invalid escape sequence '\\%c'", ch);
        out->bytes[0] = ch;
        return 1;
    }
}

/* 内部辅助实现 */
static char lexer_escape_char(zan_lexer_t *lex) {
    zan_esc_out_t out;
    int n = lexer_escape_seq(lex, lexer_loc(lex), &out);
    return out.bytes[0]; /* 底层系统交互与数据协议契约 */
    (void)n;
}

/* 内部辅助逻辑 */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    bool oom;
} zan_lex_strbuf_t;

static void zan_lex_strbuf_init(zan_lex_strbuf_t *sb) {
    sb->cap = 4096;
    sb->len = 0;
    sb->buf = (char *)malloc(sb->cap);
    sb->oom = sb->buf == NULL;
}

static void zan_lex_strbuf_push(zan_lex_strbuf_t *sb, char ch) {
    if (sb->oom) return;
    if (sb->len + 1 > sb->cap) {
        /* 内部辅助逻辑 */
        if (sb->cap > SIZE_MAX / 2) { sb->oom = true; return; }
        size_t ncap = sb->cap * 2;
        char *nbuf = (char *)realloc(sb->buf, ncap);
        if (!nbuf) { sb->oom = true; return; }
        sb->buf = nbuf;
        sb->cap = ncap;
    }
    sb->buf[sb->len++] = ch;
}

/* 模块核心语义抽象与接口调用契约 */
static char *zan_lex_strbuf_take(zan_lexer_t *lex, zan_lex_strbuf_t *sb,
                                 size_t *out_len) {
    if (sb->oom) {
        zan_diag_emit(lex->diag, DIAG_ERROR, lexer_loc(lex),
                      "out of memory while lexing string literal");
        /* 内部辅助逻辑 */
        free(sb->buf);
        sb->buf = NULL;
        *out_len = 0;
        return zan_arena_strdup(lex->arena, "", 0);
    }
    *out_len = sb->len;
    char *out = zan_arena_strdup(lex->arena, sb->buf, sb->len);
    free(sb->buf);
    sb->buf = NULL;
    return out;
}

static zan_token_t lexer_string(zan_lexer_t *lex) {
    zan_loc_t loc = lexer_loc(lex);
    lexer_advance(lex); /* opening " */

    zan_lex_strbuf_t sb;
    zan_lex_strbuf_init(&sb);

    while (!lexer_at_end(lex) && lexer_peek_ch(lex) != '"') {
        if (lexer_peek_ch(lex) == '\\') {
            lexer_advance(lex); /* \ */
            /* 内部辅助实现 */
            zan_esc_out_t esc;
            int en = lexer_escape_seq(lex, loc, &esc);
            for (int i = 0; i < en; i++) {
                zan_lex_strbuf_push(&sb, esc.bytes[i]);
            }
        } else if (lexer_peek_ch(lex) == '\n') {
            zan_diag_emit(lex->diag, DIAG_ERROR, loc, "unterminated string literal");
            break;
        } else {
            zan_lex_strbuf_push(&sb, lexer_advance(lex));
        }
    }

    if (!lexer_at_end(lex)) {
        lexer_advance(lex); /* closing " */
    } else {
        zan_diag_emit(lex->diag, DIAG_ERROR, loc, "unterminated string literal");
    }

    size_t bi = 0;
    char *text = zan_lex_strbuf_take(lex, &sb, &bi);
    zan_token_t tok = lexer_make(lex, TK_STRING_LIT, loc);
    tok.str_val.str = text;
    tok.str_val.len = (uint32_t)bi;
    return tok;
}

/* 核心系统底层抽象与内存语义契约 */

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

/* 内部辅助逻辑 */
static zan_interp_level_t *lexer_interp_top(zan_lexer_t *lex) {
    if (lex->interp_depth <= 0) return NULL;
    int i = lex->interp_depth - 1;
    if (i >= ZAN_MAX_INTERP_DEPTH) i = ZAN_MAX_INTERP_DEPTH - 1;
    return &lex->interp_stack[i];
}

static zan_token_t lexer_interp_string_segment(zan_lexer_t *lex, zan_token_kind_t start_kind) {
    zan_loc_t loc = lexer_loc(lex);
    zan_lex_strbuf_t sb;
    zan_lex_strbuf_init(&sb);

    while (!lexer_at_end(lex) && lexer_peek_ch(lex) != '"') {
        char ch = lexer_peek_ch(lex);
        if ((ch == '{' || ch == '}') && lexer_peek_ch2(lex) == ch) {
            /* 内部辅助逻辑 */
            lexer_advance(lex);
            lexer_advance(lex);
            zan_lex_strbuf_push(&sb, ch);
        } else if (ch == '{') {
            break; /* 底层系统交互与数据协议契约 */
        } else if (ch == '\\') {
            lexer_advance(lex); /* \ */
            /* 内部辅助逻辑 */
            zan_esc_out_t esc;
            int en = lexer_escape_seq(lex, loc, &esc);
            for (int i = 0; i < en; i++) {
                zan_lex_strbuf_push(&sb, esc.bytes[i]);
            }
        } else if (lexer_peek_ch(lex) == '\n') {
            zan_diag_emit(lex->diag, DIAG_ERROR, loc, "unterminated interpolated string");
            break;
        } else {
            zan_lex_strbuf_push(&sb, lexer_advance(lex));
        }
    }

    zan_token_kind_t kind;
    if (lexer_peek_ch(lex) == '{') {
        lexer_advance(lex); /* { */
        if (lex->interp_depth >= ZAN_MAX_INTERP_DEPTH) {
            zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                          "interpolated strings nested more than %d deep",
                          ZAN_MAX_INTERP_DEPTH);
            /* 内部辅助实现 */
        } else {
            lex->interp_stack[lex->interp_depth].brace = 0;
            lex->interp_stack[lex->interp_depth].paren = 0;
            lex->interp_stack[lex->interp_depth].bracket = 0;
            lex->interp_depth++;
        }
        kind = start_kind;
    } else {
        /* 编译器代码生成与运行时系统底层调用契约 */
        if (!lexer_at_end(lex)) {
            lexer_advance(lex); /* " */
        } else {
            /* 内部辅助逻辑 */
            zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                          "unterminated interpolated string");
        }
        kind = (start_kind == TK_INTERP_START) ? TK_STRING_LIT : TK_INTERP_END;
    }

    size_t bi = 0;
    char *text = zan_lex_strbuf_take(lex, &sb, &bi);
    zan_token_t tok = lexer_make(lex, kind, loc);
    tok.str_val.str = text;
    tok.str_val.len = (uint32_t)bi;
    return tok;
}

/* 内部辅助实现 */
static zan_token_t lexer_interp_format(zan_lexer_t *lex, zan_loc_t loc) {
    char buf[256];
    size_t bi = 0;
    while (!lexer_at_end(lex) && lexer_peek_ch(lex) != '}') {
        if (bi < sizeof(buf) - 1) buf[bi++] = lexer_advance(lex);
        else lexer_advance(lex); /* 底层系统交互与数据协议契约 */
    }
    if (bi >= sizeof(buf) - 1) {
        zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                      "interpolation format specifier exceeds %d characters and was truncated",
                      (int)sizeof(buf) - 1);
    }
    if (lexer_at_end(lex)) {
        zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                      "unterminated interpolated string");
    }
    zan_token_t tok = lexer_make(lex, TK_INTERP_FMT, loc);
    tok.str_val.str = zan_arena_strdup(lex->arena, buf, bi);
    tok.str_val.len = (uint32_t)bi;
    return tok;
}

static zan_token_t lexer_verbatim_string(zan_lexer_t *lex) {
    zan_loc_t loc = lexer_loc(lex);
    lexer_advance(lex); /* @ */
    lexer_advance(lex); /* " */

    zan_lex_strbuf_t sb;
    zan_lex_strbuf_init(&sb);

    while (!lexer_at_end(lex)) {
        if (lexer_peek_ch(lex) == '"') {
            if (lexer_peek_ch2(lex) == '"') {
                lexer_advance(lex);
                lexer_advance(lex);
                zan_lex_strbuf_push(&sb, '"');
            } else {
                lexer_advance(lex); /* closing " */
                break;
            }
        } else {
            zan_lex_strbuf_push(&sb, lexer_advance(lex));
        }
    }
    if (lexer_at_end(lex)) {
        /* 内部辅助逻辑 */
        zan_diag_emit(lex->diag, DIAG_ERROR, loc,
                      "unterminated verbatim string literal");
    }

    size_t bi = 0;
    char *text = zan_lex_strbuf_take(lex, &sb, &bi);
    zan_token_t tok = lexer_make(lex, TK_STRING_LIT, loc);
    tok.str_val.str = text;
    tok.str_val.len = (uint32_t)bi;
    return tok;
}

zan_token_t zan_lexer_next(zan_lexer_t *lex) {
pp_retry:
    lexer_skip_whitespace(lex);

    if (lexer_at_end(lex)) {
        if (lex->cond_depth > 0) {
            zan_diag_emit(lex->diag, DIAG_WARNING, lexer_loc(lex),
                          "unterminated #if/#ifdef (missing #endif)");
        }
        return lexer_make(lex, TK_EOF, lexer_loc(lex));
    }

    if (lexer_peek_ch(lex) == '#') {
        lexer_advance(lex); /* consume # */
        pp_handle_directive(lex);
        goto pp_retry;
    }

    if (!pp_active(lex)) {
        while (!lexer_at_end(lex) && lexer_peek_ch(lex) != '\n') {
            lexer_advance(lex);
        }
        goto pp_retry;
    }

    zan_loc_t loc = lexer_loc(lex);
    char ch = lexer_peek_ch(lex);

    if (isalpha((unsigned char)ch) || ch == '_') {
        return lexer_ident_or_keyword(lex);
    }

    if (isdigit((unsigned char)ch)) {
        return lexer_number(lex);
    }

    if (ch == '"') {
        return lexer_string(lex);
    }

    if (ch == '\'') {
        return lexer_char(lex);
    }

    if (ch == '@' && lexer_peek_ch2(lex) == '"') {
        return lexer_verbatim_string(lex);
    }

    if (ch == '$' && lexer_peek_ch2(lex) == '"') {
        lexer_advance(lex); /* $ */
        lexer_advance(lex); /* " */
        return lexer_interp_string_segment(lex, TK_INTERP_START);
    }

    lexer_advance(lex);

    switch (ch) {
    case '(':
        if (lexer_interp_top(lex)) lexer_interp_top(lex)->paren++;
        return lexer_make(lex, TK_LPAREN, loc);
    case ')':
        if (lexer_interp_top(lex) && lexer_interp_top(lex)->paren > 0)
            lexer_interp_top(lex)->paren--;
        return lexer_make(lex, TK_RPAREN, loc);
    case '{':
        if (lexer_interp_top(lex)) lexer_interp_top(lex)->brace++;
        return lexer_make(lex, TK_LBRACE, loc);
    case '}':
        if (lexer_interp_top(lex) && lexer_interp_top(lex)->brace == 0) {
            /* 内部辅助逻辑 */
            lex->interp_depth--;
            return lexer_interp_string_segment(lex, TK_INTERP_MID);
        }
        if (lexer_interp_top(lex)) lexer_interp_top(lex)->brace--;
        return lexer_make(lex, TK_RBRACE, loc);
    case '[':
        if (lexer_interp_top(lex)) lexer_interp_top(lex)->bracket++;
        return lexer_make(lex, TK_LBRACKET, loc);
    case ']':
        if (lexer_interp_top(lex) && lexer_interp_top(lex)->bracket > 0)
            lexer_interp_top(lex)->bracket--;
        return lexer_make(lex, TK_RBRACKET, loc);
    case ';': return lexer_make(lex, TK_SEMICOLON, loc);
    case ':':
        /* Inside a $" */
        {
            zan_interp_level_t *lv = lexer_interp_top(lex);
            if (lv && lv->brace == 0 && lv->paren == 0 && lv->bracket == 0)
                return lexer_interp_format(lex, loc);
        }
        return lexer_make(lex, TK_COLON, loc);
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
            if (lexer_match(lex, '>')) {
                if (lexer_match(lex, '=')) return lexer_make(lex, TK_GREATER_GREATER_GREATER_EQ, loc);
                return lexer_make(lex, TK_GREATER_GREATER_GREATER, loc);
            }
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

static zan_token_t lexer_peek_n(zan_lexer_t *lex, int n) {
    size_t pos = lex->pos;
    uint32_t line = lex->line;
    uint32_t col = lex->col;
    int idepth = lex->interp_depth;
    int nsave = idepth < ZAN_MAX_INTERP_DEPTH ? idepth : ZAN_MAX_INTERP_DEPTH;
    zan_interp_level_t istack[ZAN_MAX_INTERP_DEPTH];
    if (nsave > 0)
        memcpy(istack, lex->interp_stack, sizeof(istack[0]) * (size_t)nsave);
    /* 内部辅助实现 */
    int dcount = lex->define_count;
    /* 内部辅助逻辑 */
    int cdep = lex->cond_depth;
    int cover = lex->cond_overflow;
    int cstack[ZAN_PP_MAX_COND_DEPTH];
    int cseen[ZAN_PP_MAX_COND_DEPTH];
    int csave = cdep < ZAN_PP_MAX_COND_DEPTH ? cdep : ZAN_PP_MAX_COND_DEPTH;
    if (csave > 0) {
        memcpy(cstack, lex->cond_stack, sizeof(cstack[0]) * (size_t)csave);
        memcpy(cseen, lex->cond_seen_true, sizeof(cseen[0]) * (size_t)csave);
    }

    zan_token_t tok;
    for (int i = 0; i < n; i++)
        tok = zan_lexer_next(lex);

    lex->pos = pos;
    lex->line = line;
    lex->col = col;
    lex->interp_depth = idepth;
    if (nsave > 0)
        memcpy(lex->interp_stack, istack, sizeof(istack[0]) * (size_t)nsave);
    lex->define_count = dcount;
    lex->cond_depth = cdep;
    lex->cond_overflow = cover;
    if (csave > 0) {
        memcpy(lex->cond_stack, cstack, sizeof(cstack[0]) * (size_t)csave);
        memcpy(lex->cond_seen_true, cseen, sizeof(cseen[0]) * (size_t)csave);
    }

    return tok;
}
zan_token_t zan_lexer_peek(zan_lexer_t *lex) {
    return lexer_peek_n(lex, 1);
}

zan_token_t zan_lexer_peek2(zan_lexer_t *lex) {
    return lexer_peek_n(lex, 2);
}
