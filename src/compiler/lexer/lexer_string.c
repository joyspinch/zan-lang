static int32_t lexer_hex_escape(zan_lexer_t *lex, zan_loc_t loc, char kind,
                                int ndigits) {
    uint32_t val = 0;
    int got = 0;
    if (kind == 'x') {
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
            zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 14,
                          "'\\x' escape requires at least one hex digit");
            return -1;
        }
    } else {
        for (int i = 0; i < ndigits; i++) {
            char ch = lexer_peek_ch(lex);
            if (!isxdigit((unsigned char)ch)) {
                zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 15,
                              "'\\u' escape requires %d hex digits", ndigits);
                return -1;
            }
            lexer_advance(lex);
            int d = (ch <= '9') ? ch - '0'
                  : (ch <= 'F') ? ch - 'A' + 10 : ch - 'a' + 10;
            val = val * 16 + (uint32_t)d;
        }
    }
    if (val >= 0xD800 && val <= 0xDFFF) {
        zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 16,
                      "'\\%c' escape value 0x%X is a surrogate code point",
                      kind, val);
        return -1;
    }
    return (int32_t)val;
}

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
        zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 17,
                      "invalid escape sequence '\\%c'", ch);
        out->bytes[0] = ch;
        return 1;
    }
}

/* A character literal holds a single Unicode scalar (docs/ABI.md §3.1), so the
 * escape must yield the code point itself -- not the first byte of its UTF-8
 * form.  String literals keep using lexer_escape_seq(), which does encode
 * escapes to UTF-8 bytes. */
static int64_t lexer_escape_char_cp(zan_lexer_t *lex) {
    zan_loc_t loc = lexer_loc(lex);
    char ch = lexer_advance(lex);
    switch (ch) {
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    case '\\': return '\\';
    case '"': return '"';
    case '\'': return '\'';
    case '0': return '\0';
    case 'x': {
        int32_t cp = lexer_hex_escape(lex, loc, 'x', 4);
        if (cp < 0) return 'x';
        return (int64_t)cp;
    }
    case 'u': {
        int32_t cp = lexer_hex_escape(lex, loc, 'u', 4);
        if (cp < 0) return 'u';
        return (int64_t)cp;
    }
    default:
        zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 17,
                      "invalid escape sequence '\\%c'", ch);
        return (int64_t)(unsigned char)ch;
    }
}

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
        if (sb->cap > SIZE_MAX / 2) { sb->oom = true; return; }
        size_t ncap = sb->cap * 2;
        char *nbuf = (char *)realloc(sb->buf, ncap);
        if (!nbuf) { sb->oom = true; return; }
        sb->buf = nbuf;
        sb->cap = ncap;
    }
    sb->buf[sb->len++] = ch;
}

static char *zan_lex_strbuf_take(zan_lexer_t *lex, zan_lex_strbuf_t *sb,
                                 size_t *out_len) {
    if (sb->oom) {
        zan_diag_emit_code(lex->diag, DIAG_ERROR, lexer_loc(lex), 18,
                      "out of memory while lexing string literal");
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
            zan_esc_out_t esc;
            int en = lexer_escape_seq(lex, loc, &esc);
            for (int i = 0; i < en; i++) {
                zan_lex_strbuf_push(&sb, esc.bytes[i]);
            }
        } else if (lexer_peek_ch(lex) == '\n') {
            zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 19, "unterminated string literal");
            break;
        } else {
            zan_lex_strbuf_push(&sb, lexer_advance(lex));
        }
    }

    if (!lexer_at_end(lex)) {
        lexer_advance(lex); /* closing " */
    } else {
        zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 19, "unterminated string literal");
    }

    size_t bi = 0;
    char *text = zan_lex_strbuf_take(lex, &sb, &bi);
    zan_token_t tok = lexer_make(lex, TK_STRING_LIT, loc);
    tok.str_val.str = text;
    tok.str_val.len = (uint32_t)bi;
    return tok;
}


static int64_t lexer_next_utf8_cp(zan_lexer_t *lex) {
    int b0 = (unsigned char)lexer_advance(lex);
    if (b0 < 0x80) return b0;
    if ((b0 & 0xE0) == 0xC0) {
        int b1 = (unsigned char)lexer_peek_ch(lex);
        if ((b1 & 0xC0) == 0x80) {
            lexer_advance(lex);
            return ((int64_t)(b0 & 0x1F) << 6) | (b1 & 0x3F);
        }
    } else if ((b0 & 0xF0) == 0xE0) {
        int b1 = (unsigned char)lexer_peek_ch(lex);
        if ((b1 & 0xC0) == 0x80) {
            lexer_advance(lex);
            int b2 = (unsigned char)lexer_peek_ch(lex);
            if ((b2 & 0xC0) == 0x80) {
                lexer_advance(lex);
                return ((int64_t)(b0 & 0x0F) << 12) | ((int64_t)(b1 & 0x3F) << 6) | (b2 & 0x3F);
            }
        }
    } else if ((b0 & 0xF8) == 0xF0) {
        int b1 = (unsigned char)lexer_peek_ch(lex);
        if ((b1 & 0xC0) == 0x80) {
            lexer_advance(lex);
            int b2 = (unsigned char)lexer_peek_ch(lex);
            if ((b2 & 0xC0) == 0x80) {
                lexer_advance(lex);
                int b3 = (unsigned char)lexer_peek_ch(lex);
                if ((b3 & 0xC0) == 0x80) {
                    lexer_advance(lex);
                    return ((int64_t)(b0 & 0x07) << 18) | ((int64_t)(b1 & 0x3F) << 12) | ((int64_t)(b2 & 0x3F) << 6) | (b3 & 0x3F);
                }
            }
        }
    }
    return b0;
}

static zan_token_t lexer_char(zan_lexer_t *lex) {
    zan_loc_t loc = lexer_loc(lex);
    lexer_advance(lex); /* opening ' */

    int64_t cp;
    if (lexer_peek_ch(lex) == '\\') {
        lexer_advance(lex); /* \ */
        cp = lexer_escape_char_cp(lex);
    } else {
        cp = lexer_next_utf8_cp(lex);
    }

    if (lexer_peek_ch(lex) != '\'') {
        zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 20, "unterminated character literal");
    } else {
        lexer_advance(lex); /* closing ' */
    }

    zan_token_t tok = lexer_make(lex, TK_CHAR_LIT, loc);
    tok.int_val = cp;
    return tok;
}

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
            lexer_advance(lex);
            lexer_advance(lex);
            zan_lex_strbuf_push(&sb, ch);
        } else if (ch == '{') {
            break;
        } else if (ch == '\\') {
            lexer_advance(lex); /* \ */
            zan_esc_out_t esc;
            int en = lexer_escape_seq(lex, loc, &esc);
            for (int i = 0; i < en; i++) {
                zan_lex_strbuf_push(&sb, esc.bytes[i]);
            }
        } else if (lexer_peek_ch(lex) == '\n') {
            zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 21, "unterminated interpolated string");
            break;
        } else {
            zan_lex_strbuf_push(&sb, lexer_advance(lex));
        }
    }

    zan_token_kind_t kind;
    if (lexer_peek_ch(lex) == '{') {
        lexer_advance(lex); /* { */
        if (lex->interp_depth >= ZAN_MAX_INTERP_DEPTH) {
            zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 22,
                          "interpolated strings nested more than %d deep",
                          ZAN_MAX_INTERP_DEPTH);
        } else {
            lex->interp_stack[lex->interp_depth].brace = 0;
            lex->interp_stack[lex->interp_depth].paren = 0;
            lex->interp_stack[lex->interp_depth].bracket = 0;
            lex->interp_stack[lex->interp_depth].verbatim = false;
            lex->interp_stack[lex->interp_depth].raw = false;
            lex->interp_stack[lex->interp_depth].strip = 0;
            lex->interp_depth++;
        }
        kind = start_kind;
    } else {
        if (!lexer_at_end(lex)) {
            lexer_advance(lex); /* " */
        } else {
            zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 21,
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

static zan_token_t lexer_interp_format(zan_lexer_t *lex, zan_loc_t loc) {
    char buf[256];
    size_t bi = 0;
    while (!lexer_at_end(lex) && lexer_peek_ch(lex) != '}') {
        if (bi < sizeof(buf) - 1) buf[bi++] = lexer_advance(lex);
        else lexer_advance(lex);
    }
    if (bi >= sizeof(buf) - 1) {
        zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 23,
                      "interpolation format specifier exceeds %d characters and was truncated",
                      (int)sizeof(buf) - 1);
    }
    if (lexer_at_end(lex)) {
        zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 21,
                      "unterminated interpolated string");
    }
    zan_token_t tok = lexer_make(lex, TK_INTERP_FMT, loc);
    tok.str_val.str = zan_arena_strdup(lex->arena, buf, bi);
    tok.str_val.len = (uint32_t)bi;
    return tok;
}

/* 预扫闭合 """：从当前位置向后找第一个 3 连引号；若引号所在行首到
 * 引号之间全是空白，返回该空白列数（多行缩进剥离量），否则 0。
 * 洞内的嵌套 3 连引号会提前命中——已记为限制（C# 同类约束）。 */
static int lexer_raw_interp_strip(const zan_lexer_t *lex) {
    size_t i = lex->pos;
    while (i < lex->source_len) {
        if (lex->source[i] != '"') { i++; continue; }
        size_t q = 0;
        while (i + q < lex->source_len && lex->source[i + q] == '"') q++;
        if (q >= 3) {
            size_t j = i;
            while (j > 0 && lex->source[j - 1] != '\n' &&
                   (lex->source[j - 1] == ' ' || lex->source[j - 1] == '\t'))
                j--;
            if (j == 0 || lex->source[j - 1] == '\n') {
                return (int)(i - j);
            }
            return 0;
        }
        i += q;
    }
    return 0;
}

/* $""" 开启：吃掉三引号，预扫剥离量，丢多行首换行。 */
static zan_token_t lexer_interp_raw_open(zan_lexer_t *lex);

static zan_token_t lexer_interp_raw_segment(zan_lexer_t *lex,
                                            zan_token_kind_t start_kind,
                                            int strip) {
    zan_loc_t loc = lexer_loc(lex);
    zan_lex_strbuf_t sb;
    zan_lex_strbuf_init(&sb);

    int qrun = 0;
    while (!lexer_at_end(lex)) {
        char ch = lexer_peek_ch(lex);
        if (ch == '"') {
            qrun++;
            lexer_advance(lex);
            if (qrun >= 3) {
                while (lexer_peek_ch(lex) == '"') {
                    qrun++;
                    lexer_advance(lex);
                }
                if (qrun > 3) {
                    zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 24,
                                  "too many closing quotes in raw string literal");
                }
                goto closed;
            }
            continue; /* 持有：等下一个字符判定是内容还是闭合 */
        }
        while (qrun > 0) { zan_lex_strbuf_push(&sb, '"'); qrun--; }
        if ((ch == '{' || ch == '}') && lexer_peek_ch2(lex) == ch) {
            lexer_advance(lex);
            lexer_advance(lex);
            zan_lex_strbuf_push(&sb, ch);
        } else if (ch == '{') {
            break; /* 洞 */
        } else if (ch == '\n' && strip > 0) {
            zan_lex_strbuf_push(&sb, lexer_advance(lex));
            int k = 0;
            while (k < strip && (lexer_peek_ch(lex) == ' ' ||
                                 lexer_peek_ch(lex) == '\t')) {
                lexer_advance(lex);
                k++;
            }
            if (k < strip && lexer_peek_ch(lex) != '\n' && !lexer_at_end(lex)) {
                zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 25,
                              "insufficient indentation for raw string literal");
            }
        } else {
            zan_lex_strbuf_push(&sb, lexer_advance(lex));
        }
    }
    if (lexer_at_end(lex)) {
        zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 26,
                      "unterminated raw interpolated string");
        goto closed;
    }

    /* 洞路径：停在 '{' */
    lexer_advance(lex); /* { */
    {
        if (lex->interp_depth >= ZAN_MAX_INTERP_DEPTH) {
            zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 22,
                          "interpolated strings nested more than %d deep",
                          ZAN_MAX_INTERP_DEPTH);
        } else {
            lex->interp_stack[lex->interp_depth].brace = 0;
            lex->interp_stack[lex->interp_depth].paren = 0;
            lex->interp_stack[lex->interp_depth].bracket = 0;
            lex->interp_stack[lex->interp_depth].verbatim = false;
            lex->interp_stack[lex->interp_depth].raw = true;
            lex->interp_stack[lex->interp_depth].strip = strip;
            lex->interp_depth++;
        }
        size_t bi = 0;
        char *text = zan_lex_strbuf_take(lex, &sb, &bi);
        zan_token_t tok = lexer_make(lex, start_kind, loc);
        tok.str_val.str = text;
        tok.str_val.len = (uint32_t)bi;
        return tok;
    }

closed: ;
    /* 闭合路径：多行形态闭合行残留的换行随缓冲判落（单行内容不含换行） */
    {
        size_t bi0 = 0;
        char *probe = zan_lex_strbuf_take(lex, &sb, &bi0);
        if (bi0 > 0 && probe[bi0 - 1] == '\n') {
            bi0--;
        }
        zan_token_kind_t kind = (start_kind == TK_INTERP_START) ? TK_STRING_LIT
                                                                : TK_INTERP_END;
        zan_token_t tok = lexer_make(lex, kind, loc);
        tok.str_val.str = probe;
        tok.str_val.len = (uint32_t)bi0;
        return tok;
    }
}

static zan_token_t lexer_interp_raw_open(zan_lexer_t *lex) {
    lexer_advance(lex); /* " */
    lexer_advance(lex); /* " */
    lexer_advance(lex); /* " */
    int strip = lexer_raw_interp_strip(lex);
    if (lexer_peek_ch(lex) == '\n') {
        lexer_advance(lex); /* 多行形态：丢首换行 */
        /* 首行缩进同样剥 strip 列（不足报错，空行不报） */
        int k = 0;
        while (k < strip && (lexer_peek_ch(lex) == ' ' ||
                             lexer_peek_ch(lex) == '\t')) {
            lexer_advance(lex);
            k++;
        }
        if (k < strip && lexer_peek_ch(lex) != '\n' && !lexer_at_end(lex)) {
            zan_diag_emit_code(lex->diag, DIAG_ERROR, lexer_loc(lex), 25,
                          "insufficient indentation for raw string literal");
        }
    }
    return lexer_interp_raw_segment(lex, TK_INTERP_START, strip);
}

static zan_token_t lexer_interp_verbatim_segment(zan_lexer_t *lex, zan_token_kind_t start_kind) {
    zan_loc_t loc = lexer_loc(lex);
    zan_lex_strbuf_t sb;
    zan_lex_strbuf_init(&sb);

    while (!lexer_at_end(lex)) {
        char ch = lexer_peek_ch(lex);
        if (ch == '"') {
            if (lexer_peek_ch2(lex) == '"') {
                lexer_advance(lex);
                lexer_advance(lex);
                zan_lex_strbuf_push(&sb, '"');
            } else {
                break;
            }
        } else if ((ch == '{' || ch == '}') && lexer_peek_ch2(lex) == ch) {
            lexer_advance(lex);
            lexer_advance(lex);
            zan_lex_strbuf_push(&sb, ch);
        } else if (ch == '{') {
            break;
        } else {
            zan_lex_strbuf_push(&sb, lexer_advance(lex));
        }
    }

    zan_token_kind_t kind;
    if (lexer_peek_ch(lex) == '{') {
        lexer_advance(lex); /* { */
        if (lex->interp_depth >= ZAN_MAX_INTERP_DEPTH) {
            zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 22,
                          "interpolated strings nested more than %d deep",
                          ZAN_MAX_INTERP_DEPTH);
        } else {
            lex->interp_stack[lex->interp_depth].brace = 0;
            lex->interp_stack[lex->interp_depth].paren = 0;
            lex->interp_stack[lex->interp_depth].bracket = 0;
            lex->interp_stack[lex->interp_depth].verbatim = true;
            lex->interp_stack[lex->interp_depth].raw = false;
            lex->interp_stack[lex->interp_depth].strip = 0;
            lex->interp_depth++;
        }
        kind = start_kind;
    } else {
        if (!lexer_at_end(lex)) {
            lexer_advance(lex); /* closing " */
        } else {
            zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 27,
                          "unterminated verbatim interpolated string");
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
        zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 28,
                      "unterminated verbatim string literal");
    }

    size_t bi = 0;
    char *text = zan_lex_strbuf_take(lex, &sb, &bi);
    zan_token_t tok = lexer_make(lex, TK_STRING_LIT, loc);
    tok.str_val.str = text;
    tok.str_val.len = (uint32_t)bi;
    return tok;
}

static zan_token_t lexer_raw_string(zan_lexer_t *lex) {
    zan_loc_t loc = lexer_loc(lex);
    lexer_advance(lex); /* " */
    lexer_advance(lex); /* " */
    lexer_advance(lex); /* " */

    zan_lex_strbuf_t sb;
    zan_lex_strbuf_init(&sb);

    /* 内容零转义：反斜杠照抄；连续引号先持有——满 3 个是闭合定界
     * （丢弃持有），不足 3 个遇非引号字符时落袋为内容。
     * 多行形态首换行丢弃，闭合后按闭合行的缩进逐行剥离。 */
    int qrun = 0;
    while (!lexer_at_end(lex)) {
        char c = lexer_peek_ch(lex);
        if (c == '"') {
            qrun++;
            lexer_advance(lex);
            if (qrun >= 3) {
                while (lexer_peek_ch(lex) == '"') {
                    qrun++;
                    lexer_advance(lex);
                }
                if (qrun > 3) {
                    zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 24,
                                  "too many closing quotes in raw string literal");
                }
                goto closed;
            }
            continue; /* 持有：等下一个字符判定是内容还是闭合 */
        }
        while (qrun > 0) { zan_lex_strbuf_push(&sb, '"'); qrun--; }
        zan_lex_strbuf_push(&sb, lexer_advance(lex));
    }
    zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 29,
                  "unterminated raw string literal");
closed: ;

    size_t bi = 0;
    char *text = zan_lex_strbuf_take(lex, &sb, &bi);

    /* 多行形态：首字符是换行。末行若全为空白，视为闭合行的缩进 I，
     * 丢掉首换行与闭合行，再逐行剥掉 I 列前导空白；非空行缩进不足
     * I 直接报错（C# 口径，空行不剥不报）。 */
    if (bi > 0 && text[0] == '\n') {
        uint32_t strip = 0;
        size_t end = bi - 1;
        while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '\t'))
            end--;
        if (end > 0 && text[end - 1] == '\n') {
            strip = (uint32_t)(bi - end);
            bi = end >= 2 ? end - 2 : 0; /* 丢首换行与闭合行 */
            memmove(text, text + 1, bi);
            char *r = text, *w = text;
            size_t remain = bi;
            while (remain > 0) {
                uint32_t lead = 0;
                while (lead < remain && lead < strip &&
                       (r[lead] == ' ' || r[lead] == '\t'))
                    lead++;
                if (lead < strip && r[lead] != '\n') {
                    zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 25,
                                  "insufficient indentation for raw string literal");
                }
                r += lead;
                remain -= lead;
                while (remain > 0 && *r != '\n') { *w++ = *r++; remain--; }
                if (remain > 0 && *r == '\n') { *w++ = '\n'; r++; remain--; }
            }
            bi = (size_t)(w - text);
        } else {
            text++;       /* 闭合与内容同行：仅丢首换行 */
            bi--;
            if (bi > 0 && text[bi - 1] == '\n') bi--;
        }
    }

    zan_token_t tok = lexer_make(lex, TK_STRING_LIT, loc);
    tok.str_val.str = text;
    tok.str_val.len = (uint32_t)bi;
    return tok;
}

