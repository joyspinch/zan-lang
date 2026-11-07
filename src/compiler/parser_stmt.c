static zan_ast_node_t *parse_if_stmt(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_IF);
    parser_expect(p, TK_LPAREN);
    zan_ast_node_t *cond = parse_expression(p);
    parser_expect(p, TK_RPAREN);
    zan_ast_node_t *then_body = parse_embedded_stmt(p);

    zan_ast_node_t *else_body = NULL;
    if (parser_match(p, TK_ELSE)) {
        if (parser_check(p, TK_IF)) {
            if (p->stmt_depth >= ZAN_PARSER_MAX_STMT_DEPTH) {
                zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 48,
                              "statement nesting too deep (max %d)",
                              ZAN_PARSER_MAX_STMT_DEPTH);
                parser_advance(p);
            } else {
                p->stmt_depth++;
                else_body = parse_if_stmt(p);
                p->stmt_depth--;
            }
        } else {
            else_body = parse_embedded_stmt(p);
        }
    }

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_IF_STMT, loc);
    n->if_stmt.cond = cond;
    n->if_stmt.then_body = then_body;
    n->if_stmt.else_body = else_body;
    return n;
}

static zan_ast_node_t *parse_while_stmt(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_WHILE);
    parser_expect(p, TK_LPAREN);
    zan_ast_node_t *cond = parse_expression(p);
    parser_expect(p, TK_RPAREN);
    zan_ast_node_t *body = parse_embedded_stmt(p);

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_WHILE_STMT, loc);
    n->while_stmt.cond = cond;
    n->while_stmt.body = body;
    return n;
}

static zan_ast_node_t *parse_for_stmt(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_FOR);
    parser_expect(p, TK_LPAREN);

    zan_ast_node_t *init = NULL;
    zan_ast_list_t head_decls;
    zan_ast_list_init(&head_decls);
    int head_count = 0;
    if (!parser_check(p, TK_SEMICOLON)) {
        if (looks_like_var_decl(p)) {
            bool is_const = parser_match(p, TK_CONST);
            bool is_let = parser_match(p, TK_LET);

            zan_ast_node_t *dtype = NULL;
            if (!is_let || parser_check(p, TK_IDENT)) {
                dtype = parse_type_ref(p);
            }
            if (dtype && dtype->kind == AST_TYPE_REF &&
                dtype->type_ref.name.len == 3 &&
                memcmp(dtype->type_ref.name.str, "var", 3) == 0) {
                dtype = NULL;
            }

            if (dtype == NULL && parser_check(p, TK_LPAREN) && !is_const) {
                init = parse_tuple_decon_body(p, loc, dtype);
            } else {
                for (;;) {
                    zan_istr_t name = {0};
                    if (parser_check(p, TK_IDENT)) {
                        parser_advance(p);
                        name = p->previous.str_val;
                    } else if (!parser_take_reserved_word(p, &name)) {
                        parser_diag_expected_name(p, p->current.loc,
                                                  "expected variable name");
                    }
                    zan_ast_node_t *dinit = NULL;
                    if (parser_match(p, TK_EQ)) {
                        dinit = parse_expression(p);
                    }
                    zan_ast_node_t *decl = zan_ast_new(p->arena,
                        AST_VAR_DECL, loc);
                    decl->var_decl.name = name;
                    decl->var_decl.type = dtype;
                    decl->var_decl.initializer = dinit;
                    decl->var_decl.is_const = is_const;
                    decl->var_decl.is_let = is_let;
                    zan_ast_list_push(&head_decls, decl, p->arena);
                    head_count++;
                    if (!parser_match(p, TK_COMMA)) break;
                }
                parser_expect(p, TK_SEMICOLON);
            }
            goto parse_cond;
        } else {
            /* `for (r = 0; */
            zan_loc_t eloc = p->current.loc;
            zan_ast_node_t *expr = parse_expression(p);
            parser_expect(p, TK_SEMICOLON);
            init = zan_ast_new(p->arena, AST_EXPR_STMT, eloc);
            init->expr_stmt.expr = expr;
        }
    } else {
        parser_advance(p);
    }

parse_cond:;
    zan_ast_node_t *cond = NULL;
    if (!parser_check(p, TK_SEMICOLON)) {
        cond = parse_expression(p);
    }
    parser_expect(p, TK_SEMICOLON);

    zan_ast_node_t *step = NULL;
    if (!parser_check(p, TK_RPAREN)) {
        step = parse_expression(p);
    }
    parser_expect(p, TK_RPAREN);

    zan_ast_node_t *body = parse_embedded_stmt(p);

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_FOR_STMT, loc);
    n->for_stmt.init = init;
    n->for_stmt.cond = cond;
    n->for_stmt.step = step;
    n->for_stmt.body = body;
    if (head_count == 1) {
        n->for_stmt.init = head_decls.items[0];
    } else if (head_count > 1) {
        zan_ast_node_t *wrap = zan_ast_new(p->arena, AST_BLOCK, loc);
        zan_ast_list_init(&wrap->block.stmts);
        for (int i = 0; i < head_decls.count; i++) {
            zan_ast_list_push(&wrap->block.stmts, head_decls.items[i],
                              p->arena);
        }
        zan_ast_list_push(&wrap->block.stmts, n, p->arena);
        return wrap;
    }
    return n;
}

static zan_ast_node_t *parse_foreach_stmt(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_FOREACH);
    parser_expect(p, TK_LPAREN);

    zan_ast_node_t *var_type = NULL;
    if (!parser_check(p, TK_VAR)) {
        var_type = parse_type_ref(p);
    } else {
        parser_advance(p);
    }

    zan_istr_t var_name = {0};
    if (parser_check(p, TK_IDENT)) {
        parser_advance(p);
        var_name = p->previous.str_val;
    }

    parser_expect(p, TK_IN);
    zan_ast_node_t *collection = parse_expression(p);
    parser_expect(p, TK_RPAREN);
    zan_ast_node_t *body = parse_embedded_stmt(p);

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_FOREACH_STMT, loc);
    n->foreach_stmt.var_name = var_name;
    n->foreach_stmt.var_type = var_type;
    n->foreach_stmt.collection = collection;
    n->foreach_stmt.body = body;
    return n;
}

static zan_ast_node_t *parse_return_stmt(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_RETURN);

    zan_ast_node_t *value = NULL;
    if (!parser_check(p, TK_SEMICOLON)) {
        value = parse_expression(p);
    }
    parser_expect(p, TK_SEMICOLON);

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_RETURN_STMT, loc);
    n->ret.value = value;
    return n;
}

static bool is_case_type_pattern(zan_parser_t *p) {
    switch (p->current.kind) {
    case TK_INT: case TK_LONG: case TK_SHORT: case TK_BYTE:
    case TK_UINT: case TK_ULONG: case TK_USHORT: case TK_SBYTE:
    case TK_FLOAT: case TK_DOUBLE: case TK_DECIMAL: case TK_BOOL: case TK_CHAR:
    case TK_STRING: case TK_OBJECT: case TK_NINT:
        return true;
    case TK_IDENT: {
        zan_token_t nxt = zan_lexer_peek(p->lex);
        return nxt.kind == TK_IDENT || nxt.kind == TK_LESS ||
               nxt.kind == TK_QUESTION || nxt.kind == TK_LBRACKET;
    }
    default:
        return false;
    }
}

static zan_ast_node_t *parse_switch_stmt(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_SWITCH);
    parser_expect(p, TK_LPAREN);
    zan_ast_node_t *expr = parse_expression(p);
    parser_expect(p, TK_RPAREN);
    parser_expect(p, TK_LBRACE);

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_SWITCH_STMT, loc);
    n->switch_stmt.expr = expr;
    zan_ast_list_init(&n->switch_stmt.cases);

    while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
        zan_loc_t case_loc = p->current.loc;
        zan_ast_node_t *pattern = NULL;
        zan_ast_node_t *type_pattern = NULL;
        zan_ast_node_t *when_cond = NULL;
        zan_istr_t var_name = {0};

        if (parser_check(p, TK_CASE)) {
            parser_advance(p);
            if (is_case_type_pattern(p)) {
                type_pattern = parse_type_ref(p);
                if (parser_check(p, TK_IDENT)) {
                    parser_advance(p);
                    var_name = p->previous.str_val;
                }
            } else {
                pattern = parse_expression(p);
            }
            /* `case */
            if (parser_match(p, TK_WHEN)) {
                when_cond = parse_expression(p);
            }
            parser_expect(p, TK_COLON);
        } else if (parser_check(p, TK_DEFAULT)) {
            parser_advance(p);
            if (parser_match(p, TK_WHEN)) {
                when_cond = parse_expression(p);
            }
            parser_expect(p, TK_COLON);
        } else {
            break;
        }

        zan_ast_node_t *body_block = zan_ast_new(p->arena, AST_BLOCK, case_loc);
        zan_ast_list_init(&body_block->block.stmts);
        while (!parser_check(p, TK_CASE) && !parser_check(p, TK_DEFAULT) &&
               !parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
            uint32_t before = p->current.loc.offset;
            zan_ast_node_t *stmt = parse_statement(p);
            zan_ast_list_push(&body_block->block.stmts, stmt, p->arena);
            splice_pending_stmts(p, &body_block->block.stmts);
            if (p->current.loc.offset == before && !parser_check(p, TK_EOF)) {
                parser_advance(p);
            }
        }

        zan_ast_node_t *sc = zan_ast_new(p->arena, AST_SWITCH_CASE, case_loc);
        sc->switch_case.pattern = pattern;
        sc->switch_case.type_pattern = type_pattern;
        sc->switch_case.when_cond = when_cond;
        sc->switch_case.var_name = var_name;
        sc->switch_case.body = body_block;
        zan_ast_list_push(&n->switch_stmt.cases, sc, p->arena);
    }

    parser_expect(p, TK_RBRACE);
    return n;
}

static zan_ast_node_t *parse_try_stmt(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_TRY);
    zan_ast_node_t *try_body = parse_block(p);

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_TRY_STMT, loc);
    n->try_stmt.try_body = try_body;
    zan_ast_list_init(&n->try_stmt.catches);
    n->try_stmt.finally_body = NULL;

    while (parser_check(p, TK_CATCH)) {
        zan_loc_t catch_loc = p->current.loc;
        parser_advance(p);

        zan_ast_node_t *catch_type = NULL;
        zan_istr_t catch_var = {0};

        if (parser_check(p, TK_LPAREN)) {
            parser_advance(p);
            catch_type = parse_type_ref(p);
            if (parser_check(p, TK_IDENT)) {
                parser_advance(p);
                catch_var = p->previous.str_val;
            }
            parser_expect(p, TK_RPAREN);
        }

        zan_ast_node_t *catch_body = parse_block(p);

        zan_ast_node_t *cc = zan_ast_new(p->arena, AST_CATCH_CLAUSE, catch_loc);
        cc->catch_clause.type = catch_type;
        cc->catch_clause.var_name = catch_var;
        cc->catch_clause.body = catch_body;
        zan_ast_list_push(&n->try_stmt.catches, cc, p->arena);
    }

    if (parser_check(p, TK_FINALLY)) {
        parser_advance(p);
        n->try_stmt.finally_body = parse_block(p);
    }

    return n;
}

/* Skip a balanced `< */
static bool skip_angle_group(zan_parser_t *p) {
    int depth = 0;
    for (;;) {
        zan_token_t gt = zan_lexer_next(p->lex);
        if (gt.kind == TK_LESS) depth++;
        else if (gt.kind == TK_GREATER) depth--;
        else if (gt.kind == TK_GREATER_GREATER) depth -= 2;
        else if (gt.kind == TK_GREATER_GREATER_GREATER) depth -= 3;
        else if (gt.kind == TK_EOF || gt.kind == TK_SEMICOLON) return false;
        if (depth <= 0) return true;
    }
}

static bool looks_like_local_func(zan_parser_t *p) {
    zan_lexer_mark_t saved; zan_lexer_mark(p->lex, &saved, p->arena);

    zan_token_t t = p->current;

    switch (t.kind) {
    case TK_INT: case TK_LONG: case TK_SHORT: case TK_BYTE:
    case TK_UINT: case TK_ULONG: case TK_USHORT: case TK_SBYTE:
    case TK_FLOAT: case TK_DOUBLE: case TK_DECIMAL: case TK_BOOL: case TK_CHAR:
    case TK_STRING: case TK_VOID: case TK_OBJECT: case TK_NINT:
        break;
    case TK_IDENT: {
        for (;;) {
            zan_token_t after = zan_lexer_peek(p->lex);
            if (after.kind == TK_DOT) {
                zan_lexer_next(p->lex);
                if (zan_lexer_next(p->lex).kind != TK_IDENT) {
                    zan_lexer_reset(p->lex, &saved); return false;
                }
                continue;
            }
            if (after.kind == TK_LESS) {
                if (!skip_angle_group(p)) { zan_lexer_reset(p->lex, &saved); return false; }
                continue;
            }
            break;
        }
        break;
    }
    default:
        return false;
    }

    for (;;) {
        zan_token_t after = zan_lexer_peek(p->lex);
        if (after.kind != TK_LBRACKET) break;
        int depth = 0;
        for (;;) {
            zan_token_t bt = zan_lexer_next(p->lex);
            if (bt.kind == TK_LBRACKET) depth++;
            else if (bt.kind == TK_RBRACKET) depth--;
            else if (bt.kind == TK_EOF || bt.kind == TK_SEMICOLON) {
                zan_lexer_reset(p->lex, &saved); return false;
            }
            if (depth <= 0) break;
        }
    }

    if (zan_lexer_next(p->lex).kind != TK_IDENT) { zan_lexer_reset(p->lex, &saved); return false; }

    if (zan_lexer_peek(p->lex).kind == TK_LESS) {
        if (!skip_angle_group(p)) { zan_lexer_reset(p->lex, &saved); return false; }
    }

    if (zan_lexer_next(p->lex).kind != TK_LPAREN) { zan_lexer_reset(p->lex, &saved); return false; }

    {
        int depth = 1;
        while (depth > 0) {
            zan_token_t pt = zan_lexer_next(p->lex);
            if (pt.kind == TK_LPAREN || pt.kind == TK_LBRACKET) depth++;
            else if (pt.kind == TK_RPAREN || pt.kind == TK_RBRACKET) depth--;
            else if (pt.kind == TK_SEMICOLON || pt.kind == TK_EOF) {
                zan_lexer_reset(p->lex, &saved); return false;
            }
        }
    }

    zan_token_t tail = zan_lexer_next(p->lex);
    zan_lexer_reset(p->lex, &saved);
    return tail.kind == TK_LBRACE || tail.kind == TK_ARROW;
}

static zan_ast_node_t *parse_local_func(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;

    zan_ast_node_t *ret_type = parse_type_ref(p);

    zan_istr_t name = {0};
    if (parser_check(p, TK_IDENT)) {
        parser_advance(p);
        name = p->previous.str_val;
    } else if (!parser_take_reserved_word(p, &name)) {
        parser_diag_expected_name(p, p->current.loc,
                                  "expected local function name");
        return parser_error_node(p);
    }

    zan_ast_list_t type_params;
    zan_ast_list_init(&type_params);
    if (parser_match(p, TK_LESS)) {
        while (!parser_check(p, TK_GREATER) && !parser_check(p, TK_EOF)) {
            if (parser_check(p, TK_IDENT)) {
                parser_advance(p);
                zan_ast_node_t *tp = zan_ast_new(p->arena, AST_IDENTIFIER, p->previous.loc);
                tp->ident.name = p->previous.str_val;
                zan_ast_list_push(&type_params, tp, p->arena);
            }
            if (!parser_match(p, TK_COMMA)) break;
        }
        parser_expect(p, TK_GREATER);
    }

    zan_ast_list_t params;
    zan_ast_list_init(&params);
    parser_expect(p, TK_LPAREN);
    if (!parser_check(p, TK_RPAREN)) {
        for (;;) {
            zan_ast_node_t *param = parse_parameter(p);
            zan_ast_list_push(&params, param, p->arena);
            if (!parser_match(p, TK_COMMA)) break;
        }
    }
    parser_expect(p, TK_RPAREN);

    zan_ast_node_t *body = NULL;
    if (parser_check(p, TK_LBRACE)) {
        body = parse_block(p);
    } else if (parser_match(p, TK_ARROW)) {
        zan_ast_node_t *expr = parse_expression(p);
        parser_expect(p, TK_SEMICOLON);
        body = zan_ast_new(p->arena, AST_BLOCK, expr->loc);
        zan_ast_list_init(&body->block.stmts);
        zan_ast_node_t *ret = zan_ast_new(p->arena, AST_RETURN_STMT, expr->loc);
        ret->ret.value = expr;
        zan_ast_list_push(&body->block.stmts, ret, p->arena);
    } else {
        zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 51,
                      "expected local function body");
        return parser_error_node(p);
    }

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_METHOD_DECL, loc);
    n->method_decl.name = name;
    n->method_decl.return_type = ret_type;
    n->method_decl.params = params;
    n->method_decl.type_params = type_params;
    n->method_decl.body = body;
    n->method_decl.modifiers = MOD_PRIVATE | MOD_STATIC;
    n->method_decl.has_base_init = false;
    n->method_decl.has_this_init = false;
    zan_ast_list_push(&p->pending_members, n, p->arena);

    zan_ast_node_t *noop = zan_ast_new(p->arena, AST_BLOCK, loc);
    zan_ast_list_init(&noop->block.stmts);
    return noop;
}

static zan_ast_node_t *parse_statement(zan_parser_t *p) {
    if (p->current.kind == TK_IDENT && p->current.str_val.len == 5 &&
        memcmp(p->current.str_val.str, "yield", 5) == 0) {
        zan_token_kind_t nk = zan_lexer_peek(p->lex).kind;
        if (nk == TK_RETURN || nk == TK_BREAK) {
            zan_loc_t loc = p->current.loc;
            parser_advance(p);
            parser_advance(p);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_YIELD_STMT, loc);
            n->yield_stmt.value =
                (nk == TK_RETURN) ? parse_expression(p) : NULL;
            parser_expect(p, TK_SEMICOLON);
            return n;
        }
    }

    if (p->current.kind == TK_IDENT && zan_lexer_peek(p->lex).kind == TK_COLON) {
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        zan_istr_t lname = p->previous.str_val;
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_LABEL_STMT, loc);
        n->ident.name = lname;
        return n;
    }

    /* `checked { */
    if (is_checked_use(p) && zan_lexer_peek(p->lex).kind == TK_LBRACE) {
        zan_loc_t loc = p->current.loc;
        bool is_checked = p->current.str_val.len == 7;
        parser_advance(p);
        if (is_checked) p->checked_depth++; else p->unchecked_depth++;
        zan_ast_node_t *blk = parse_block(p);
        if (is_checked) p->checked_depth--; else p->unchecked_depth--;
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_CHECKED_STMT, loc);
        n->checked_stmt.checked = is_checked;
        n->checked_stmt.body = blk;
        return n;
    }

    switch (p->current.kind) {
    case TK_LBRACE:
        return parse_block(p);
    case TK_IF:
        return parse_if_stmt(p);
    case TK_WHILE:
        return parse_while_stmt(p);
    case TK_FOR:
        return parse_for_stmt(p);
    case TK_FOREACH:
        return parse_foreach_stmt(p);
    case TK_RETURN:
        return parse_return_stmt(p);
    case TK_BREAK: {
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        parser_expect(p, TK_SEMICOLON);
        return zan_ast_new(p->arena, AST_BREAK_STMT, loc);
    }
    case TK_CONTINUE: {
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        parser_expect(p, TK_SEMICOLON);
        return zan_ast_new(p->arena, AST_CONTINUE_STMT, loc);
    }
    case TK_THROW: {
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        zan_ast_node_t *value = NULL;
        if (p->current.kind != TK_SEMICOLON)
            value = parse_expression(p);
        parser_expect(p, TK_SEMICOLON);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_THROW_STMT, loc);
        n->throw_stmt.value = value;
        return n;
    }
    case TK_SWITCH:
        return parse_switch_stmt(p);
    case TK_TRY:
        return parse_try_stmt(p);
    case TK_DEFER: {
        zan_loc_t loc = p->current.loc;
        if (p->stmt_depth >= ZAN_PARSER_MAX_STMT_DEPTH) {
            zan_diag_emit_code(p->diag, DIAG_ERROR, loc, 48,
                          "statement nesting too deep (max %d)",
                          ZAN_PARSER_MAX_STMT_DEPTH);
            parser_advance(p);
            zan_ast_node_t *fb = zan_ast_new(p->arena, AST_BLOCK, loc);
            zan_ast_list_init(&fb->block.stmts);
            return fb;
        }
        p->stmt_depth++;
        parser_advance(p);
        zan_ast_node_t *stmt = parse_statement(p);
        p->stmt_depth--;
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_BLOCK, loc);
        zan_ast_list_init(&n->block.stmts);
        zan_ast_node_t *try_node = zan_ast_new(p->arena, AST_TRY_STMT, loc);
        zan_ast_node_t *empty_try = zan_ast_new(p->arena, AST_BLOCK, loc);
        zan_ast_list_init(&empty_try->block.stmts);
        try_node->try_stmt.try_body = empty_try;
        zan_ast_list_init(&try_node->try_stmt.catches);
        zan_ast_node_t *fin = stmt;
        if (!fin || fin->kind != AST_BLOCK) {
            zan_ast_node_t *fb = zan_ast_new(p->arena, AST_BLOCK, loc);
            zan_ast_list_init(&fb->block.stmts);
            if (stmt) zan_ast_list_push(&fb->block.stmts, stmt, p->arena);
            fin = fb;
        }
        try_node->try_stmt.finally_body = fin;
        zan_ast_list_push(&n->block.stmts, try_node, p->arena);
        return n;
    }
    case TK_USING: {
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        parser_expect(p, TK_LPAREN);

        zan_istr_t name = {NULL, 0};
        zan_ast_node_t *type = NULL;
        zan_ast_node_t *init = NULL;

        if (looks_like_var_decl(p)) {
            zan_ast_node_t *tref = parse_type_ref(p);
            type = tref;
            if (parser_check(p, TK_IDENT)) {
                parser_advance(p);
                name = p->previous.str_val;
            } else if (!parser_take_reserved_word(p, &name)) {
                parser_diag_expected_name(p, p->current.loc,
                                          "expected variable name in using declaration");
            }
            if (parser_match(p, TK_EQ)) {
                init = parse_expression(p);
            }
        } else {
            init = parse_expression(p);
            char buf[32];
            snprintf(buf, sizeof buf, "__using%d", p->synth_counter++);
            name = (zan_istr_t){ zan_arena_strdup(p->arena, buf, (int)strlen(buf)),
                                 (uint32_t)strlen(buf) };
        }
        parser_expect(p, TK_RPAREN);
        zan_ast_node_t *body = parse_embedded_stmt(p);

        zan_ast_node_t *outer = zan_ast_new(p->arena, AST_BLOCK, loc);
        zan_ast_list_init(&outer->block.stmts);

        if (type || !init) {
            zan_ast_node_t *decl = zan_ast_new(p->arena, AST_VAR_DECL, loc);
            decl->var_decl.name = name;
            decl->var_decl.type = type;
            decl->var_decl.initializer = init;
            decl->var_decl.is_const = false;
            decl->var_decl.is_let = false;
            zan_ast_list_push(&outer->block.stmts, decl, p->arena);
        } else if (init) {
            zan_ast_node_t *decl = zan_ast_new(p->arena, AST_VAR_DECL, loc);
            decl->var_decl.name = name;
            decl->var_decl.type = NULL; /* var (inferred) */
            decl->var_decl.initializer = init;
            decl->var_decl.is_const = false;
            decl->var_decl.is_let = false;
            zan_ast_list_push(&outer->block.stmts, decl, p->arena);
        }

        zan_ast_node_t *try_node = zan_ast_new(p->arena, AST_TRY_STMT, loc);
        try_node->try_stmt.try_body = body;
        zan_ast_list_init(&try_node->try_stmt.catches);
        try_node->try_stmt.finally_body = NULL;

        /* finally { name.Dispose(); } */
        zan_ast_node_t *fin = zan_ast_new(p->arena, AST_BLOCK, loc);
        zan_ast_list_init(&fin->block.stmts);
        zan_ast_node_t *callee = zan_ast_new(p->arena, AST_MEMBER_ACCESS, loc);
        zan_ast_node_t *res_ref = zan_ast_new(p->arena, AST_IDENTIFIER, loc);
        res_ref->ident.name = name;
        callee->member.object = res_ref;
        callee->member.name = (zan_istr_t){ (char *)"Dispose", 7 };
        callee->member.null_cond = false;
        zan_ast_node_t *dispose_call = zan_ast_new(p->arena, AST_CALL, loc);
        dispose_call->call.callee = callee;
        zan_ast_list_init(&dispose_call->call.args);
        zan_ast_list_init(&dispose_call->call.type_args);
        zan_ast_node_t *estmt = zan_ast_new(p->arena, AST_EXPR_STMT, loc);
        estmt->expr_stmt.expr = dispose_call;
        zan_ast_list_push(&fin->block.stmts, estmt, p->arena);
        try_node->try_stmt.finally_body = fin;

        zan_ast_list_push(&outer->block.stmts, try_node, p->arena);
        return outer;
    }
    case TK_LOCK: {
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        parser_expect(p, TK_LPAREN);
        zan_ast_node_t *expr = parse_expression(p);
        parser_expect(p, TK_RPAREN);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_LOCK_STMT, loc);
        n->lock_stmt.expr = expr;
        n->lock_stmt.body = parse_embedded_stmt(p);
        return n;
    }
    case TK_GOTO: {
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        parser_expect(p, TK_IDENT);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_GOTO_STMT, loc);
        n->ident.name = p->previous.str_val;
        parser_expect(p, TK_SEMICOLON);
        return n;
    }
    case TK_UNSAFE:
        /* unsafe { */
        parser_advance(p);
        return parse_block(p);
    case TK_FIXED: {
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        parser_expect(p, TK_LPAREN);
        zan_ast_node_t *type = parse_type_ref(p);
        parser_match(p, TK_STAR);
        parser_expect(p, TK_IDENT);
        zan_istr_t vname = p->previous.str_val;
        parser_expect(p, TK_EQ);
        zan_ast_node_t *init = parse_expression(p);
        parser_expect(p, TK_RPAREN);
        zan_ast_node_t *decl = zan_ast_new(p->arena, AST_VAR_DECL, loc);
        decl->var_decl.name = vname;
        decl->var_decl.type = type;
        decl->var_decl.initializer = init;
        decl->var_decl.is_const = false;
        decl->var_decl.is_let = false;
        zan_ast_node_t *body = parse_embedded_stmt(p);
        zan_ast_node_t *blk = zan_ast_new(p->arena, AST_BLOCK, loc);
        zan_ast_list_init(&blk->block.stmts);
        zan_ast_list_push(&blk->block.stmts, decl, p->arena);
        zan_ast_list_push(&blk->block.stmts, body, p->arena);
        return blk;
    }
    case TK_DO: {
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        zan_ast_node_t *body = parse_embedded_stmt(p);
        parser_expect(p, TK_WHILE);
        parser_expect(p, TK_LPAREN);
        zan_ast_node_t *cond = parse_expression(p);
        parser_expect(p, TK_RPAREN);
        parser_expect(p, TK_SEMICOLON);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_DO_WHILE_STMT, loc);
        n->while_stmt.cond = cond;
        n->while_stmt.body = body;
        return n;
    }
    default:
        break;
    }

    if (looks_like_local_func(p)) {
        return parse_local_func(p);
    }

    if (looks_like_decon_decl(p)) {
        zan_loc_t dloc = p->current.loc;
        return parse_tuple_decon_body(p, dloc, NULL);
    }

    if (looks_like_var_decl(p)) {
        return parse_var_decl(p);
    }

    zan_loc_t loc = p->current.loc;
    zan_ast_node_t *expr = parse_expression(p);
    parser_expect(p, TK_SEMICOLON);
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_EXPR_STMT, loc);
    n->expr_stmt.expr = expr;
    return n;
}
