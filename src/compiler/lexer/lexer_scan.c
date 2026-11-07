zan_token_t zan_lexer_next(zan_lexer_t *lex) {
pp_retry:
    lexer_skip_whitespace(lex);

    if (lexer_at_end(lex)) {
        if (lex->cond_depth > 0) {
            zan_diag_emit_code(lex->diag, DIAG_WARNING, lexer_loc(lex), 30,
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
        if (lexer_peek_ch2(lex) == '"' && lexer_peek_ch3(lex) == '"') {
            return lexer_raw_string(lex);
        }
        return lexer_string(lex);
    }

    if (ch == '\'') {
        return lexer_char(lex);
    }

    if (ch == '$' && lexer_peek_ch2(lex) == '"' &&
        lexer_peek_ch3(lex) == '"') {
        lexer_advance(lex); /* $ */
        return lexer_interp_raw_open(lex);
    }
    if (((ch == '$' && lexer_peek_ch2(lex) == '@') ||
         (ch == '@' && lexer_peek_ch2(lex) == '$')) &&
        lexer_peek_ch3(lex) == '"' && lexer_peek_ch4(lex) == '"' &&
        lexer_peek_ch5(lex) == '"') {
        lexer_advance(lex); /* $ 或 @ */
        lexer_advance(lex); /* @ 或 $ */
        return lexer_interp_raw_open(lex);
    }
    if ((ch == '$' && lexer_peek_ch2(lex) == '@' && lexer_peek_ch3(lex) == '"') ||
        (ch == '@' && lexer_peek_ch2(lex) == '$' && lexer_peek_ch3(lex) == '"')) {
        lexer_advance(lex); /* $ or @ */
        lexer_advance(lex); /* @ or $ */
        lexer_advance(lex); /* " */
        return lexer_interp_verbatim_segment(lex, TK_INTERP_START);
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
            bool is_verb = lexer_interp_top(lex)->verbatim;
            bool is_raw = lexer_interp_top(lex)->raw;
            int rstrip = lexer_interp_top(lex)->strip;
            lex->interp_depth--;
            if (is_raw)
                return lexer_interp_raw_segment(lex, TK_INTERP_MID, rstrip);
            if (is_verb)
                return lexer_interp_verbatim_segment(lex, TK_INTERP_MID);
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
        zan_diag_emit_code(lex->diag, DIAG_ERROR, loc, 31,
                      "unexpected character '%c' (0x%02x)", ch, (unsigned char)ch);
        return lexer_make(lex, TK_INVALID, loc);
    }
}

zan_token_t zan_lexer_peek_n(zan_lexer_t *lex, int n) {
    size_t pos = lex->pos;
    uint32_t line = lex->line;
    uint32_t col = lex->col;
    int idepth = lex->interp_depth;
    int nsave = idepth < ZAN_MAX_INTERP_DEPTH ? idepth : ZAN_MAX_INTERP_DEPTH;
    zan_interp_level_t istack[ZAN_MAX_INTERP_DEPTH];
    if (nsave > 0)
        memcpy(istack, lex->interp_stack, sizeof(istack[0]) * (size_t)nsave);
    int dcount = lex->define_count;
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
    return zan_lexer_peek_n(lex, 1);
}

zan_token_t zan_lexer_peek2(zan_lexer_t *lex) {
    return zan_lexer_peek_n(lex, 2);
}

void zan_lexer_mark(zan_lexer_t *lex, zan_lexer_mark_t *out, zan_arena_t *arena) {
    out->pos = lex->pos;
    out->line = lex->line;
    out->col = lex->col;
    out->interp_depth = lex->interp_depth;
    out->interp_tail = NULL;
    if (lex->interp_depth > 0) {
        out->interp_tail = (zan_interp_level_t *)zan_arena_alloc(
            arena, sizeof(zan_interp_level_t) * (size_t)lex->interp_depth);
        memcpy(out->interp_tail, lex->interp_stack,
               sizeof(zan_interp_level_t) * (size_t)lex->interp_depth);
    }
    out->define_count = lex->define_count;
    out->cond_depth = lex->cond_depth;
    out->cond_overflow = lex->cond_overflow;
}

void zan_lexer_reset(zan_lexer_t *lex, const zan_lexer_mark_t *mark) {
    lex->pos = mark->pos;
    lex->line = mark->line;
    lex->col = mark->col;
    lex->interp_depth = mark->interp_depth;
    if (mark->interp_tail) {
        memcpy(lex->interp_stack, mark->interp_tail,
               sizeof(zan_interp_level_t) * (size_t)mark->interp_depth);
    }
    lex->define_count = mark->define_count;
    lex->cond_depth = mark->cond_depth;
    lex->cond_overflow = mark->cond_overflow;
}
