#define ZAN_PARSER_MAX_STMT_DEPTH 4096

static void parse_block_stmts(zan_parser_t *p, zan_ast_list_t *stmts_list) {
    while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
        if (parser_check(p, TK_DEFER)) {
            zan_loc_t defer_loc = p->current.loc;
            parser_advance(p);
            if (p->stmt_depth >= ZAN_PARSER_MAX_STMT_DEPTH) {
                zan_diag_emit_code(p->diag, DIAG_ERROR, defer_loc, 48,
                              "statement nesting too deep (max %d)",
                              ZAN_PARSER_MAX_STMT_DEPTH);
                if (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF))
                    parser_advance(p);
                break;
            }
            p->stmt_depth++;
            zan_ast_node_t *defer_stmt = parse_statement(p);
            p->stmt_depth--;
            zan_ast_node_t *fin_body = defer_stmt;
            if (!fin_body || fin_body->kind != AST_BLOCK) {
                zan_ast_node_t *fb = zan_ast_new(p->arena, AST_BLOCK, defer_loc);
                zan_ast_list_init(&fb->block.stmts);
                if (defer_stmt) {
                    zan_ast_list_push(&fb->block.stmts, defer_stmt, p->arena);
                    splice_pending_stmts(p, &fb->block.stmts);
                }
                fin_body = fb;
            }

            zan_ast_node_t *tail_block = zan_ast_new(p->arena, AST_BLOCK, defer_loc);
            zan_ast_list_init(&tail_block->block.stmts);
            parse_block_stmts(p, &tail_block->block.stmts);

            zan_ast_node_t *try_node = zan_ast_new(p->arena, AST_TRY_STMT, defer_loc);
            try_node->try_stmt.try_body = tail_block;
            zan_ast_list_init(&try_node->try_stmt.catches);
            try_node->try_stmt.finally_body = fin_body;

            zan_ast_list_push(stmts_list, try_node, p->arena);
            break;
        }

        uint32_t before = p->current.loc.offset;
        zan_ast_node_t *stmt = parse_statement(p);
        if (stmt) {
            zan_ast_list_push(stmts_list, stmt, p->arena);
            splice_pending_stmts(p, stmts_list);
        }
        if (p->current.loc.offset == before && !parser_check(p, TK_EOF)) {
            parser_advance(p);
        }
    }
}

static zan_ast_node_t *parse_block(zan_parser_t *p) {
    if (p->stmt_depth >= ZAN_PARSER_MAX_STMT_DEPTH) {
        zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 48,
                      "statement nesting too deep (max %d)",
                      ZAN_PARSER_MAX_STMT_DEPTH);
        while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF))
            parser_advance(p);
        zan_ast_node_t *empty = zan_ast_new(p->arena, AST_BLOCK,
                                            p->current.loc);
        zan_ast_list_init(&empty->block.stmts);
        return empty;
    }
    p->stmt_depth++;
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_LBRACE);

    zan_ast_node_t *block = zan_ast_new(p->arena, AST_BLOCK, loc);
    zan_ast_list_init(&block->block.stmts);

    parse_block_stmts(p, &block->block.stmts);

    p->stmt_depth--;
    parser_expect(p, TK_RBRACE);
    return block;
}


static inline zan_token_t lexer_peek_n(zan_parser_t *p, int k) {
    return zan_lexer_peek_n(p->lex, k);
}

static bool looks_like_decon_decl(zan_parser_t *p) {
    if (!parser_check(p, TK_LPAREN)) return false;
    zan_token_t a = lexer_peek_n(p, 1);
    bool type_kw = a.kind == TK_INT || a.kind == TK_LONG || a.kind == TK_SHORT ||
                   a.kind == TK_BYTE || a.kind == TK_UINT || a.kind == TK_ULONG ||
                   a.kind == TK_USHORT || a.kind == TK_SBYTE || a.kind == TK_FLOAT ||
                   a.kind == TK_DOUBLE || a.kind == TK_DECIMAL || a.kind == TK_BOOL ||
                   a.kind == TK_CHAR || a.kind == TK_STRING || a.kind == TK_OBJECT ||
                   a.kind == TK_NINT || a.kind == TK_VAR;
    if (a.kind != TK_IDENT && !type_kw) return false;
    zan_token_t b = lexer_peek_n(p, 2);
    if (b.kind != TK_IDENT) return false;
    zan_token_t c = lexer_peek_n(p, 3);
    if (c.kind != TK_COMMA && c.kind != TK_RPAREN) return false;

    int depth = 0;
    for (int k = 1; k < 64; k++) {
        zan_token_t tk = lexer_peek_n(p, k);
        if (tk.kind == TK_LPAREN) {
            depth++;
        } else if (tk.kind == TK_RPAREN) {
            if (depth == 0) {
                zan_token_t after = lexer_peek_n(p, k + 1);
                return after.kind == TK_EQ;
            }
            depth--;
        } else if (tk.kind == TK_SEMICOLON || tk.kind == TK_EOF) {
            return false;
        }
    }
    return false;
}

static zan_ast_node_t *parse_tuple_decon_body(zan_parser_t *p, zan_loc_t loc,
                                              zan_ast_node_t *type_prefix) {
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_TUPLE_DECON, loc);
    zan_ast_list_init(&n->tuple_decon.names);
    zan_ast_list_init(&n->tuple_decon.types);

    if (type_prefix) {
        bool is_var = type_prefix->kind == AST_TYPE_REF &&
                      type_prefix->type_ref.name.len == 3 &&
                      memcmp(type_prefix->type_ref.name.str, "var", 3) == 0;
        if (!is_var) {
            zan_diag_emit_code(p->diag, DIAG_ERROR, loc, 49,
                          "unsupported tuple deconstruction form");
            return n;
        }
    }

    if (parser_check(p, TK_LPAREN)) parser_advance(p);

    while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
        zan_ast_node_t *elem_type = NULL;
        bool is_kw_type =
            parser_check(p, TK_INT) || parser_check(p, TK_LONG) ||
            parser_check(p, TK_SHORT) || parser_check(p, TK_BYTE) ||
            parser_check(p, TK_UINT) || parser_check(p, TK_ULONG) ||
            parser_check(p, TK_USHORT) || parser_check(p, TK_SBYTE) ||
            parser_check(p, TK_FLOAT) || parser_check(p, TK_DOUBLE) ||
            parser_check(p, TK_DECIMAL) || parser_check(p, TK_BOOL) ||
            parser_check(p, TK_CHAR) || parser_check(p, TK_STRING) ||
            parser_check(p, TK_OBJECT) || parser_check(p, TK_NINT) ||
            parser_check(p, TK_VAR);
        if (is_kw_type) {
            elem_type = parse_type_ref(p);
        } else if (parser_check(p, TK_IDENT)) {
            zan_token_t nxt = zan_lexer_peek(p->lex);
            if (nxt.kind != TK_COMMA && nxt.kind != TK_RPAREN) {
                elem_type = parse_type_ref(p);
            }
        }
        zan_ast_node_t *name = zan_ast_new(p->arena, AST_IDENTIFIER, p->current.loc);
        if (parser_check(p, TK_IDENT)) {
            parser_advance(p);
            name->ident.name = p->previous.str_val;
        } else {
            zan_istr_t bad = {0};
            if (!parser_take_reserved_word(p, &bad)) {
                parser_diag_expected_name(p, p->current.loc,
                                          "expected variable name in deconstruction");
                bad = (zan_istr_t){ (char *)"__bad", 5 };
            }
            name->ident.name = bad;
        }
        zan_ast_list_push(&n->tuple_decon.names, name, p->arena);
        zan_ast_list_push(&n->tuple_decon.types, elem_type, p->arena);
        if (!parser_match(p, TK_COMMA)) break;
    }
    parser_expect(p, TK_RPAREN);
    parser_expect(p, TK_EQ);
    n->tuple_decon.initializer = parse_expression(p);
    parser_expect(p, TK_SEMICOLON);
    return n;
}

static zan_ast_node_t *parse_embedded_stmt(zan_parser_t *p) {
    if (parser_check(p, TK_LBRACE)) {
        return parse_block(p);
    }

    if (p->stmt_depth >= ZAN_PARSER_MAX_STMT_DEPTH) {
        zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 48,
                      "statement nesting too deep (max %d)",
                      ZAN_PARSER_MAX_STMT_DEPTH);
        parser_advance(p);
        zan_ast_node_t *empty = zan_ast_new(p->arena, AST_BLOCK,
                                            p->current.loc);
        zan_ast_list_init(&empty->block.stmts);
        return empty;
    }
    p->stmt_depth++;

    zan_loc_t loc = p->current.loc;
    if (looks_like_var_decl(p)) {
        zan_diag_emit_code(p->diag, DIAG_ERROR, loc, 50,
                      "a declaration cannot be used as a single-statement body; "
                      "enclose it in braces");
    }

    zan_ast_node_t *block = zan_ast_new(p->arena, AST_BLOCK, loc);
    zan_ast_list_init(&block->block.stmts);
    uint32_t before = p->current.loc.offset;
    zan_ast_node_t *stmt = parse_statement(p);
    if (stmt) {
        zan_ast_list_push(&block->block.stmts, stmt, p->arena);
        splice_pending_stmts(p, &block->block.stmts);
    }
    if (p->current.loc.offset == before && !parser_check(p, TK_EOF)) {
        parser_advance(p);
    }
    p->stmt_depth--;
    return block;
}

static bool looks_like_var_decl(zan_parser_t *p) {
    if (parser_check(p, TK_VAR) || parser_check(p, TK_LET) || parser_check(p, TK_CONST)) {
        return true;
    }
    switch (p->current.kind) {
    case TK_INT: case TK_LONG: case TK_SHORT: case TK_BYTE:
    case TK_UINT: case TK_ULONG: case TK_USHORT: case TK_SBYTE:
    case TK_FLOAT: case TK_DOUBLE: case TK_DECIMAL: case TK_BOOL: case TK_CHAR:
    case TK_STRING: case TK_VOID: case TK_OBJECT: case TK_NINT: {
        /* `int x = 3;` declares, but `int */
        if (p->current.kind == TK_VOID) return true;
        const char *s = p->lex->source;
        size_t q = p->lex->pos, n = p->lex->source_len;
        #define ZAN_TKW_WS(ch) ((ch)==' '||(ch)=='\t'||(ch)=='\r'||(ch)=='\n')
        while (q < n && ZAN_TKW_WS(s[q])) q++;
        while (q < n && s[q] == '[') {
            size_t r = q + 1;
            /* 数组秩修饰符解析：方括号间仅允许逗号与空白 */
            while (r < n && (ZAN_TKW_WS(s[r]) || s[r] == ',')) r++;
            if (r >= n || s[r] != ']') return false;
            q = r + 1;
            while (q < n && ZAN_TKW_WS(s[q])) q++;
        }
        if (q < n && s[q] == '.') return false;
        #undef ZAN_TKW_WS
        return true;
    }
    case TK_IDENT: {
        zan_token_t peek = zan_lexer_peek(p->lex);
        if (peek.kind == TK_IDENT) {
            return true;
        }
        if (peek.kind == TK_LESS) {
            const char *s = p->lex->source;
            size_t q = p->lex->pos, n = p->lex->source_len;
            #define ZAN_GA_WS(ch) ((ch)==' '||(ch)=='\t'||(ch)=='\r'||(ch)=='\n')
            #define ZAN_GA_IDSTART(ch) (((ch)>='a'&&(ch)<='z')||((ch)>='A'&&(ch)<='Z')||(ch)=='_')
            while (q < n && ZAN_GA_WS(s[q])) q++;
            if (q >= n || s[q] != '<') return false;
            int gd = 0;
            while (q < n) {
                char gc = s[q];
                if (gc == '<') { gd++; }
                else if (gc == '>') { gd--; if (gd == 0) { q++; break; } }
                else if (gc == ';' || gc == '(' || gc == ')' ||
                         gc == '{' || gc == '}' || gc == '=') break;
                q++;
            }
            if (gd != 0) return false;
            while (q < n && ZAN_GA_WS(s[q])) q++;
            while (q < n && s[q] == '[') {
                size_t r = q + 1;
                while (r < n && (ZAN_GA_WS(s[r]) || s[r] == ',')) r++;
                if (r >= n || s[r] != ']') return false;
                q = r + 1;
                while (q < n && ZAN_GA_WS(s[q])) q++;
            }
            return q < n && ZAN_GA_IDSTART(s[q]);
            #undef ZAN_GA_WS
            #undef ZAN_GA_IDSTART
        }
        if (peek.kind == TK_DOT) {
            const char *s = p->lex->source;
            size_t q = p->lex->pos, n = p->lex->source_len;
            #define ZAN_WS(ch) ((ch)==' '||(ch)=='\t'||(ch)=='\r'||(ch)=='\n')
            #define ZAN_IDSTART(ch) (((ch)>='a'&&(ch)<='z')||((ch)>='A'&&(ch)<='Z')||(ch)=='_')
            #define ZAN_IDCONT(ch) (ZAN_IDSTART(ch)||((ch)>='0'&&(ch)<='9'))
            for (;;) {
                while (q < n && ZAN_WS(s[q])) q++;
                if (q >= n || s[q] != '.') break;
                q++;
                while (q < n && ZAN_WS(s[q])) q++;
                if (q >= n || !ZAN_IDSTART(s[q])) return false;
                while (q < n && ZAN_IDCONT(s[q])) q++;
            }
            while (q < n && ZAN_WS(s[q])) q++;
            if (q < n && s[q] == '<') {
                /* `A */
                int gd = 0; size_t r = q;
                while (r < n) {
                    char gc = s[r];
                    if (gc == '<') gd++;
                    else if (gc == '>') { gd--; if (gd == 0) { r++; break; } }
                    else if (gc == ';' || gc == '(' || gc == ')' ||
                             gc == '{' || gc == '}') break;
                    r++;
                }
                if (gd != 0) return false;
                while (r < n && ZAN_WS(s[r])) r++;
                return r < n && ZAN_IDSTART(s[r]);
            }
            if (q < n && s[q] == '[') {
                size_t r = q + 1;
                while (r < n && (ZAN_WS(s[r]) || s[r] == ',')) r++;
                if (r < n && s[r] == ']') return true;
                return false;
            }
            if (q < n && s[q] == '?') {
                /* 限定名的可空标记：`A.B? x` / `A.B?[] x`。没有这一段时，
                 * `System.Text.Encoding? e = null;` 会被当成三元表达式，
                 * 解析器一路报 "expected ':'"。
                 * 收紧到「标识符 + ';' 或 '='」：`A.B ? c : d` 这种**限定名开头
                 * 的三元表达式**不能被当成声明。 */
                q++;
                while (q < n && ZAN_WS(s[q])) q++;
                if (q < n && s[q] == '[') {
                    /* 可空数组：秩后缀（方括号里只允许空白与逗号） */
                    size_t r = q + 1;
                    while (r < n && (ZAN_WS(s[r]) || s[r] == ',')) r++;
                    if (r >= n || s[r] != ']') return false;
                    q = r + 1;
                    while (q < n && ZAN_WS(s[q])) q++;
                }
                if (q < n && ZAN_IDSTART(s[q])) {
                    while (q < n && ZAN_IDCONT(s[q])) q++;
                    while (q < n && ZAN_WS(s[q])) q++;
                    return q < n && (s[q] == ';' ||
                        (s[q] == '=' && (q + 1 >= n || s[q + 1] != '=')));
                }
                return false;
            }
            if (q < n && ZAN_IDSTART(s[q])) {
                char w[8]; size_t wl = 0; size_t r = q;
                while (r < n && ZAN_IDCONT(s[r]) && wl < sizeof w - 1) w[wl++] = s[r++];
                w[wl] = 0;
                if ((wl==2 && (w[0]=='i'&&w[1]=='s')) ||
                    (wl==2 && (w[0]=='a'&&w[1]=='s')) ||
                    (wl==2 && (w[0]=='i'&&w[1]=='n')))
                    return false;
                return true;
            }
            #undef ZAN_WS
            #undef ZAN_IDSTART
            #undef ZAN_IDCONT
            return false;
        }
        if (peek.kind == TK_QUESTION) {
            const char *s = p->lex->source;
            size_t q = p->lex->pos, n = p->lex->source_len;
            #define ZAN_NQ_WS(ch) ((ch)==' '||(ch)=='\t'||(ch)=='\r'||(ch)=='\n')
            #define ZAN_NQ_IDSTART(ch) (((ch)>='a'&&(ch)<='z')||((ch)>='A'&&(ch)<='Z')||(ch)=='_')
            #define ZAN_NQ_IDCONT(ch) (ZAN_NQ_IDSTART(ch)||((ch)>='0'&&(ch)<='9'))
            bool is_decl = false;
            while (q < n && ZAN_NQ_WS(s[q])) q++;
            if (q < n && s[q] == '?') {
                q++;
                if (q < n && s[q] != '?' && s[q] != '.') {
                    while (q < n && ZAN_NQ_WS(s[q])) q++;
                    /* 可空数组：`T?[]` 的秩后缀。方括号里只允许空白与逗号，
                     * 所以 `cond ? [1,2] : x`（数组字面量）不会被误判成声明。 */
                    if (q < n && s[q] == '[') {
                        size_t r = q + 1;
                        while (r < n && (ZAN_NQ_WS(s[r]) || s[r] == ',')) r++;
                        if (r < n && s[r] == ']') {
                            q = r + 1;
                            while (q < n && ZAN_NQ_WS(s[q])) q++;
                        }
                        /* 不是秩后缀（`cond ? [1,2] : x` 里的数组字面量）→
                         * q 停在 '[' 上，后面的标识符检查自然不成立。 */
                    }
                    if (q < n && ZAN_NQ_IDSTART(s[q])) {
                        while (q < n && ZAN_NQ_IDCONT(s[q])) q++;
                        while (q < n && ZAN_NQ_WS(s[q])) q++;
                        is_decl = q < n && (s[q] == ';' ||
                            (s[q] == '=' && (q + 1 >= n || s[q + 1] != '=')));
                    }
                }
            }
            #undef ZAN_NQ_WS
            #undef ZAN_NQ_IDSTART
            #undef ZAN_NQ_IDCONT
            return is_decl;
        }
        if (peek.kind == TK_LBRACKET) {
            const char *s = p->lex->source;
            size_t q = p->lex->pos, n = p->lex->source_len;
            while (q < n && (s[q] == ' ' || s[q] == '\t' ||
                             s[q] == '\r' || s[q] == '\n')) q++;
            if (q < n && s[q] == '[') {
                q++;
                while (q < n && (s[q] == ' ' || s[q] == '\t' ||
                                 s[q] == '\r' || s[q] == '\n' || s[q] == ',')) q++;
                if (q < n && s[q] == ']') return true;
            }
        }
        return false;
    }
    case TK_LPAREN: {
        int depth = 0;
        for (int k = 1; k < 64; k++) {
            zan_token_t tk = lexer_peek_n(p, k);
            if (tk.kind == TK_LPAREN) {
                depth++;
            } else if (tk.kind == TK_RPAREN) {
                if (depth == 0) {
                    zan_token_t nm = lexer_peek_n(p, k + 1);
                    return nm.kind == TK_IDENT;
                }
                depth--;
            } else if (tk.kind == TK_SEMICOLON || tk.kind == TK_EOF ||
                       tk.kind == TK_EQ_EQ) {
                return false;
            }
        }
        return false;
    }
    default:
        return false;
    }
}

static zan_ast_node_t *parse_var_decl(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    bool is_const = parser_match(p, TK_CONST);
    bool is_let = parser_match(p, TK_LET);

    zan_ast_node_t *type = NULL;
    if (!is_let || parser_check(p, TK_IDENT)) {
        type = parse_type_ref(p);
    }

    if (type && type->kind == AST_TYPE_REF &&
        type->type_ref.name.len == 3 &&
        memcmp(type->type_ref.name.str, "var", 3) == 0) {
        type = NULL;
    }

    if (type == NULL && parser_check(p, TK_LPAREN) && !is_const) {
        return parse_tuple_decon_body(p, loc, type);
    }

    zan_istr_t name = {0};
    if (parser_check(p, TK_IDENT)) {
        parser_advance(p);
        name = p->previous.str_val;
    } else if (!parser_take_reserved_word(p, &name)) {
        parser_diag_expected_name(p, p->current.loc, "expected variable name");
    }

    zan_ast_node_t *init = NULL;
    if (parser_match(p, TK_EQ)) {
        init = parse_expression(p);
    }

    zan_ast_node_t *decl = zan_ast_new(p->arena, AST_VAR_DECL, loc);
    decl->var_decl.name = name;
    decl->var_decl.type = type;
    decl->var_decl.initializer = init;
    decl->var_decl.is_const = is_const;
    decl->var_decl.is_let = is_let;

    zan_ast_list_init(&p->pending_stmts);
    while (parser_match(p, TK_COMMA)) {
        zan_istr_t more = {0};
        if (parser_check(p, TK_IDENT)) {
            parser_advance(p);
            more = p->previous.str_val;
        } else if (!parser_take_reserved_word(p, &more)) {
            parser_diag_expected_name(p, p->current.loc,
                                      "expected variable name after ','");
            break;
        }
        zan_ast_node_t *more_init = NULL;
        if (parser_match(p, TK_EQ)) {
            more_init = parse_expression(p);
        }
        zan_ast_node_t *md = zan_ast_new(p->arena, AST_VAR_DECL, loc);
        md->var_decl.name = more;
        md->var_decl.type = type;
        md->var_decl.initializer = more_init;
        md->var_decl.is_const = is_const;
        md->var_decl.is_let = is_let;
        zan_ast_list_push(&p->pending_stmts, md, p->arena);
    }

    parser_expect(p, TK_SEMICOLON);
    return decl;
}

