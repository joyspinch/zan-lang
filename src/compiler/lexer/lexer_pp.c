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

static void pp_end_directive_line(zan_lexer_t *lex, int honor_conditional) {
    if (!honor_conditional) { pp_skip_to_eol(lex); return; }
    size_t stop = lex->pos;
    int found = 0;
    while (stop < lex->source_len && lex->source[stop] != '\n') {
        char c = lex->source[stop];
        if (c == '/' && stop + 1 < lex->source_len
            && lex->source[stop + 1] == '/') {
            break;
        }
        if (c == '/' && stop + 1 < lex->source_len
            && lex->source[stop + 1] == '*') {
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
            zan_diag_emit_code(lex->diag, DIAG_ERROR, lexer_loc(lex), 1,
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
            zan_diag_emit_code(lex->diag, DIAG_ERROR, lexer_loc(lex), 1,
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
            zan_diag_emit_code(lex->diag, DIAG_ERROR, lexer_loc(lex), 1,
                          "conditional-compilation nesting too deep (max %d)",
                          ZAN_PP_MAX_COND_DEPTH);
            lex->cond_overflow++;
        }
    } else if (strcmp(dir, "elif") == 0) {
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
            zan_diag_emit_code(lex->diag, DIAG_ERROR, lexer_loc(lex), 2, "#error %s", msg);
        }
    } else if (strcmp(dir, "warning") == 0) {
        if (pp_active(lex)) {
            pp_skip_hspaces(lex);
            char msg[256]; int mi = 0;
            while (!lexer_at_end(lex) && lexer_peek_ch(lex) != '\n' && mi < 255)
                msg[mi++] = lexer_advance(lex);
            msg[mi] = '\0';
            zan_diag_emit_code(lex->diag, DIAG_WARNING, lexer_loc(lex), 3, "#warning %s", msg);
        }
    } else if (strcmp(dir, "pragma") == 0) {
        /* #pragma warning disable|restore <编号列表>（A382③）；未知 pragma
         * 静默吞行（与 C# 一致）。编号 token 与 --suppress 同形：0003 / W0003 */
        if (!pp_active(lex)) return pp_end_directive_line(lex, 0);
        pp_skip_hspaces(lex);
        char what[32]; pp_read_ident(lex, what, sizeof(what));
        if (strcmp(what, "warning") != 0) return pp_end_directive_line(lex, 0);
        pp_skip_hspaces(lex);
        char act[16]; pp_read_ident(lex, act, sizeof(act));
        int disable = strcmp(act, "disable") == 0;
        int restore = strcmp(act, "restore") == 0;
        if (!disable && !restore) return pp_end_directive_line(lex, 0);
        zan_loc_t dloc = lexer_loc(lex);
        int codes[64]; int nc = 0;
        for (;;) {
            pp_skip_hspaces(lex);
            char c = lexer_peek_ch(lex);
            if (lexer_at_end(lex) || c == '\n') break;
            int code = 0, seen = 0;
            while ((c >= '0' && c <= '9') || c == 'E' || c == 'e' ||
                   c == 'W' || c == 'w') {
                if (c >= '0' && c <= '9') { code = code * 10 + (c - '0'); seen = 1; }
                lexer_advance(lex);
                c = lexer_peek_ch(lex);
            }
            if (seen && nc < 64) codes[nc++] = code;
            pp_skip_hspaces(lex);
            if (lexer_peek_ch(lex) == ',') { lexer_advance(lex); continue; }
            break;
        }
        zan_diag_pragma_warning(lex->diag, dloc, disable, codes, nc);
    }
    int honor_conditional =
        strcmp(dir, "if") == 0 || strcmp(dir, "ifdef") == 0 ||
        strcmp(dir, "ifndef") == 0 || strcmp(dir, "elif") == 0 ||
        strcmp(dir, "else") == 0 || strcmp(dir, "endif") == 0;
    pp_end_directive_line(lex, honor_conditional);
}

