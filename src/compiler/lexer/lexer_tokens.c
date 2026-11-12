
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
                zan_diag_emit_code(lex->diag, DIAG_ERROR, start_loc, 4,
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

static int64_t lexer_radix_int_value(zan_lexer_t *lex, zan_loc_t loc,
                                     const char *buf, int radix,
                                     int *lit_suffix) {
    errno = 0;
    unsigned long long uv = strtoull(buf, NULL, radix);
    if (errno == ERANGE) {
        zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 5,
                      "integer literal is too large for 'ulong'");
        *lit_suffix = 0;
        return 0;
    }
    if (uv <= 0x7FFFFFFFFFFFFFFFULL) return (int64_t)uv;
    if (*lit_suffix == 1) {
        zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 6,
                      "integer literal is too large for 'long'");
        return (int64_t)uv;
    }
    if (*lit_suffix == 0) *lit_suffix = 3;
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
            if (!digit_seen) {
                zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 7,
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
                zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 8,
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
                zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 9,
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
                zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 8,
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
            if (!digit_seen) {
                char bad = lexer_peek_ch(lex);
                if (bad >= '8' && bad <= '9')
                    zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 10,
                                  "octal literal requires digits 0-7 after '0o'");
                else
                    zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 11,
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
                zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 8,
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

    /* A decimal point before a member name belongs to postfix parsing.
     * Keep trailing-dot floats, exponents and the existing f/m suffixes;
     * checking the full suffix boundary keeps 9.Format() an integer member. */
    char after_dot = lexer_peek_ch2(lex);
    char after_suffix = lexer_peek_ch3(lex);
    bool dot_member = isalpha((unsigned char)after_dot) || after_dot == '_';
    if ((after_dot == 'e' || after_dot == 'E') &&
        (isdigit((unsigned char)after_suffix) ||
         after_suffix == '+' || after_suffix == '-'))
        dot_member = false;
    if ((after_dot == 'f' || after_dot == 'F' ||
         after_dot == 'm' || after_dot == 'M') &&
        !(isalnum((unsigned char)after_suffix) || after_suffix == '_'))
        dot_member = false;
    if (lexer_peek_ch(lex) == '.' && after_dot != '.' && !dot_member) {
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
        if (!exp_digit_seen) {
            zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 12,
                          "exponent requires at least one digit after 'e'");
            return lexer_make(lex, TK_INVALID, loc);
        }
    }

    bool is_decimal = false;
    int float_suffix = 0;
    /* suffix: f/F (float), m/M (decimal) */
    if (lexer_peek_ch(lex) == 'f' || lexer_peek_ch(lex) == 'F') {
        is_float = true;
        float_suffix = 1;
        lexer_advance(lex);
    } else if (lexer_peek_ch(lex) == 'm' || lexer_peek_ch(lex) == 'M') {
        is_decimal = true;
        lexer_advance(lex);
    }

    int lit_suffix = 0;
    if (!is_float && !is_decimal) {
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
        zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 13, is_float || is_decimal
                      ? "floating-point/decimal literal is too long"
                      : "integer literal is too long");
    }

    if (is_decimal) {
        zan_token_t tok = lexer_make(lex, TK_DECIMAL_LIT, loc);
        tok.str_val.str = zan_arena_strdup(lex->arena, buf, bi);
        tok.str_val.len = (uint32_t)bi;
        return tok;
    } else if (is_float) {
        zan_token_t tok = lexer_make(lex, TK_FLOAT_LIT, loc);
        tok.float_val = strtod(buf, NULL);
        /* f/F 后缀标记:字面量按 float(f32) 语义参与类型检查与发射 */
        tok.lit_suffix = float_suffix;
        return tok;
    } else {
        zan_token_t tok = lexer_make(lex, TK_INT_LIT, loc);
        tok.lit_suffix = lit_suffix;
        tok.lit_radix = 10;
        errno = 0;
        long long sv = strtoll(buf, NULL, 10);
        if (errno == ERANGE) {
            errno = 0;
            unsigned long long uv = strtoull(buf, NULL, 10);
            if (errno == ERANGE) {
                zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 5,
                              "integer literal is too large for 'ulong'");
                tok.int_val = 0;
            } else if (lit_suffix == 1) {
                zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 6,
                              "integer literal is too large for 'long'");
                tok.int_val = (int64_t)uv;
            } else {
                tok.lit_suffix = 3;
                tok.int_val = (int64_t)uv;
            }
        } else {
            tok.int_val = (int64_t)sv;
        }
        return tok;
    }
}

typedef struct {
    char bytes[4];
    int len;
} zan_esc_out_t;

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

