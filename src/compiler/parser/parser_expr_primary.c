static bool is_type_kw(zan_token_kind_t k);

static zan_ast_node_t *parse_call_arg(zan_parser_t *p);

static zan_ast_node_t *parse_primary(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;

    /* checked( */
    if (is_checked_use(p)) {
        bool is_checked = p->current.str_val.len == 7;
        zan_loc_t cloc = p->current.loc;
        parser_advance(p);
        if (is_checked) p->checked_depth++; else p->unchecked_depth++;
        if (parser_check(p, TK_LPAREN)) {
            parser_advance(p);
            zan_ast_node_t *inner = parse_expression(p);
            parser_expect(p, TK_RPAREN);
            if (is_checked) p->checked_depth--; else p->unchecked_depth--;
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_CHECKED_STMT, cloc);
            n->checked_stmt.checked = is_checked;
            n->checked_stmt.body = inner;
            return n;
        }
        /* checked { */
        zan_ast_node_t *blk = parse_block(p);
        if (is_checked) p->checked_depth--; else p->unchecked_depth--;
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_CHECKED_STMT, cloc);
        n->checked_stmt.checked = is_checked;
        n->checked_stmt.body = blk;
        return n;
    }

    if (is_type_kw(p->current.kind) && zan_lexer_peek(p->lex).kind == TK_DOT) {
        const char *nm = zan_token_kind_name(p->current.kind);
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_IDENTIFIER, loc);
        n->ident.name = (zan_istr_t){ nm, (int)strlen(nm) };
        return n;
    }

    if (p->current.kind == TK_IDENT && p->current.str_val.len == 4 &&
        memcmp(p->current.str_val.str, "from", 4) == 0 &&
        zan_lexer_peek(p->lex).kind == TK_IDENT) {
        zan_lexer_mark_t saved_lex; zan_lexer_mark(p->lex, &saved_lex, p->arena);
        zan_token_t saved_cur = p->current;
        zan_token_t saved_prev = p->previous;
        parser_advance(p);
        parser_advance(p);
        if (p->current.kind == TK_IN) {
            zan_istr_t qvar = p->previous.str_val;
            parser_advance(p);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_QUERY_EXPR, loc);
            n->query.var = qvar;
            n->query.source = parse_expression(p);
            zan_ast_list_init(&n->query.clauses);
            n->query.group_expr = NULL;
            n->query.group_key = NULL;
            n->query.group_into = (zan_istr_t){ NULL, 0 };
            n->query.select = NULL;
            for (;;) {
                if (parser_match(p, TK_WHERE)) {
                    zan_ast_node_t *wc = zan_ast_new(p->arena, AST_QUERY_WHERE,
                                                     p->previous.loc);
                    wc->query_clause.expr = parse_expression(p);
                    zan_ast_list_push(&n->query.clauses, wc, p->arena);
                    continue;
                }
                if (p->current.kind == TK_LET) {
                    parser_advance(p);
                    zan_ast_node_t *lc = zan_ast_new(p->arena, AST_QUERY_LET,
                                                     p->previous.loc);
                    lc->query_clause.name = p->current.str_val;
                    parser_expect(p, TK_IDENT);
                    parser_expect(p, TK_EQ);
                    lc->query_clause.expr = parse_expression(p);
                    zan_ast_list_push(&n->query.clauses, lc, p->arena);
                    continue;
                }
                if (p->current.kind == TK_IDENT &&
                    p->current.str_val.len == 7 &&
                    memcmp(p->current.str_val.str, "orderby", 7) == 0) {
                    parser_advance(p);
                    for (;;) {
                        zan_ast_node_t *oc = zan_ast_new(
                            p->arena, AST_QUERY_ORDERBY, p->previous.loc);
                        oc->query_clause.expr = parse_expression(p);
                        oc->query_clause.descending = 0;
                        if (p->current.kind == TK_IDENT &&
                            p->current.str_val.len == 10 &&
                            memcmp(p->current.str_val.str, "descending", 10)
                                == 0) {
                            parser_advance(p);
                            oc->query_clause.descending = 1;
                        } else if (p->current.kind == TK_IDENT &&
                                   p->current.str_val.len == 9 &&
                                   memcmp(p->current.str_val.str,
                                          "ascending", 9) == 0) {
                            parser_advance(p);
                        }
                        zan_ast_list_push(&n->query.clauses, oc, p->arena);
                        if (!parser_match(p, TK_COMMA)) break;
                    }
                    continue;
                }
                if (p->current.kind == TK_IDENT &&
                    p->current.str_val.len == 4 &&
                    memcmp(p->current.str_val.str, "join", 4) == 0) {
                    parser_advance(p);
                    zan_ast_node_t *jc = zan_ast_new(p->arena, AST_QUERY_JOIN,
                                                     p->previous.loc);
                    jc->query_clause.name = p->current.str_val;
                    parser_expect(p, TK_IDENT);
                    parser_expect(p, TK_IN);
                    jc->query_clause.source = parse_expression(p);
                    if (!(p->current.kind == TK_IDENT &&
                          p->current.str_val.len == 2 &&
                          memcmp(p->current.str_val.str, "on", 2) == 0)) {
                        zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 39,
                                      "expected 'on' in join clause");
                    } else {
                        parser_advance(p);
                    }
                    jc->query_clause.left_key = parse_expression(p);
                    if (!(p->current.kind == TK_IDENT &&
                          p->current.str_val.len == 6 &&
                          memcmp(p->current.str_val.str, "equals", 6) == 0)) {
                        zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 40,
                                      "expected 'equals' in join clause");
                    } else {
                        parser_advance(p);
                    }
                    jc->query_clause.right_key = parse_expression(p);
                    jc->query_clause.into = (zan_istr_t){ NULL, 0 };
                    if (p->current.kind == TK_IDENT &&
                        p->current.str_val.len == 4 &&
                        memcmp(p->current.str_val.str, "into", 4) == 0) {
                        parser_advance(p);
                        jc->query_clause.into = p->current.str_val;
                        parser_expect(p, TK_IDENT);
                    }
                    zan_ast_list_push(&n->query.clauses, jc, p->arena);
                    continue;
                }
                break;
            }
            if (p->current.kind == TK_IDENT &&
                p->current.str_val.len == 5 &&
                memcmp(p->current.str_val.str, "group", 5) == 0) {
                parser_advance(p);
                n->query.group_expr = parse_expression(p);
                if (!(p->current.kind == TK_IDENT &&
                      p->current.str_val.len == 2 &&
                      memcmp(p->current.str_val.str, "by", 2) == 0)) {
                    zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 41,
                                  "expected 'by' in group clause");
                } else {
                    parser_advance(p);
                }
                n->query.group_key = parse_expression(p);
                if (p->current.kind == TK_IDENT &&
                    p->current.str_val.len == 4 &&
                    memcmp(p->current.str_val.str, "into", 4) == 0) {
                    parser_advance(p);
                    n->query.group_into = p->current.str_val;
                    parser_expect(p, TK_IDENT);
                }
            }
            if (p->current.kind == TK_IDENT && p->current.str_val.len == 6 &&
                memcmp(p->current.str_val.str, "select", 6) == 0) {
                if (n->query.group_expr && n->query.group_into.len == 0) {
                    /* `group e by k` without `into` is terminal. */
                    zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 42,
                                  "a group clause without 'into' must be the "
                                  "final clause of the query");
                } else {
                    parser_advance(p);
                    n->query.select = parse_expression(p);
                }
            } else if (!n->query.group_expr) {
                zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 43,
                              "expected 'select' in query expression");
                n->query.select = parser_error_node(p);
            }
            return n;
        }
        zan_lexer_reset(p->lex, &saved_lex);
        p->current = saved_cur;
        p->previous = saved_prev;
    }

    switch (p->current.kind) {
    case TK_INT_LIT: {
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_INT_LITERAL, loc);
        n->int_val = p->previous.int_val;
        n->lit_suffix = p->previous.lit_suffix;
        n->lit_radix = p->previous.lit_radix;
        return n;
    }
    case TK_FLOAT_LIT: {
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_FLOAT_LITERAL, loc);
        n->float_val = p->previous.float_val;
        n->lit_suffix = p->previous.lit_suffix;
        return n;
    }
    case TK_DECIMAL_LIT: {
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_DECIMAL_LITERAL, loc);
        n->str_val = p->previous.str_val;
        return n;
    }
    case TK_STRING_LIT: {
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_STRING_LITERAL, loc);
        n->str_val = p->previous.str_val;
        return n;
    }
    case TK_INTERP_START: {
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_STRING_INTERP, loc);
        zan_ast_list_init(&n->string_interp.parts);
        zan_ast_list_init(&n->string_interp.formats);
        parser_advance(p);
        zan_ast_node_t *seg = zan_ast_new(p->arena, AST_STRING_LITERAL, loc);
        seg->str_val = p->previous.str_val;
        zan_ast_list_push(&n->string_interp.parts, seg, p->arena);
        while (true) {
            zan_ast_node_t *expr = parse_expression(p);
            zan_ast_list_push(&n->string_interp.parts, expr, p->arena);
            zan_ast_node_t *fmt = NULL;
            if (p->current.kind == TK_INTERP_FMT) {
                parser_advance(p);
                fmt = zan_ast_new(p->arena, AST_STRING_LITERAL, p->previous.loc);
                fmt->str_val = p->previous.str_val;
            }
            zan_ast_list_push(&n->string_interp.formats, fmt, p->arena);
            if (p->current.kind == TK_INTERP_MID) {
                parser_advance(p);
                zan_ast_node_t *mid = zan_ast_new(p->arena, AST_STRING_LITERAL, p->previous.loc);
                mid->str_val = p->previous.str_val;
                zan_ast_list_push(&n->string_interp.parts, mid, p->arena);
            } else if (p->current.kind == TK_INTERP_END) {
                parser_advance(p);
                zan_ast_node_t *end = zan_ast_new(p->arena, AST_STRING_LITERAL, p->previous.loc);
                end->str_val = p->previous.str_val;
                zan_ast_list_push(&n->string_interp.parts, end, p->arena);
                break;
            } else {
                break;
            }
        }
        return n;
    }
    case TK_CHAR_LIT: {
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_CHAR_LITERAL, loc);
        n->int_val = p->previous.int_val;
        return n;
    }
    case TK_TRUE: {
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_BOOL_LITERAL, loc);
        n->bool_val = true;
        return n;
    }
    case TK_FALSE: {
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_BOOL_LITERAL, loc);
        n->bool_val = false;
        return n;
    }
    case TK_NULL: {
        parser_advance(p);
        return zan_ast_new(p->arena, AST_NULL_LITERAL, loc);
    }
    case TK_THIS: {
        parser_advance(p);
        return zan_ast_new(p->arena, AST_THIS_EXPR, loc);
    }
    case TK_BASE: {
        parser_advance(p);
        return zan_ast_new(p->arena, AST_BASE_EXPR, loc);
    }
    case TK_IDENT: {
        if (p->current.str_val.len == 6 &&
            memcmp(p->current.str_val.str, "nameof", 6) == 0 &&
            zan_lexer_peek(p->lex).kind == TK_LPAREN) {
            parser_advance(p);
            parser_expect(p, TK_LPAREN);
            zan_ast_node_t *arg = parse_expression(p);
            parser_expect(p, TK_RPAREN);
            zan_istr_t name = {NULL, 0};
            if (arg->kind == AST_IDENTIFIER) {
                name = arg->ident.name;
            } else if (arg->kind == AST_MEMBER_ACCESS) {
                name = arg->member.name;
            }
            if (!name.str) {
                zan_diag_emit_code(p->diag, DIAG_ERROR, loc, 44,
                              "nameof argument must be an identifier or member access");
                name.str = "";
                name.len = 0;
            }
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_STRING_LITERAL, loc);
            n->str_val = name;
            return n;
        }
        zan_token_t peek = zan_lexer_peek(p->lex);
        if (peek.kind == TK_ARROW) {
            parser_advance(p);
            zan_istr_t param_name = p->previous.str_val;
            parser_advance(p);
            zan_ast_node_t *body = parser_check(p, TK_LBRACE)
                ? parse_block(p) : parse_expression(p);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_LAMBDA, loc);
            zan_ast_list_init(&n->lambda.params);
            zan_ast_node_t *param = zan_ast_new(p->arena, AST_PARAM, loc);
            param->param.name = param_name;
            param->param.type = NULL;
            param->param.default_val = NULL;
            zan_ast_list_push(&n->lambda.params, param, p->arena);
            n->lambda.body = body;
            return n;
        }
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_IDENTIFIER, loc);
        n->ident.name = p->previous.str_val;
        return n;
    }
    case TK_LPAREN: {
        if (paren_is_lambda(p)) {
            return parse_lambda_paren(p, loc);
        }
        switch (zan_lexer_peek(p->lex).kind) {
        case TK_INT: case TK_LONG: case TK_SHORT: case TK_BYTE:
        case TK_UINT: case TK_ULONG: case TK_USHORT: case TK_SBYTE:
        case TK_DOUBLE: case TK_FLOAT: case TK_DECIMAL:
        case TK_BOOL: case TK_CHAR: case TK_NINT:
        case TK_STRING: case TK_OBJECT: {
            parser_advance(p);
            zan_ast_node_t *ctype = parse_type_ref(p);
            parser_expect(p, TK_RPAREN);
            zan_ast_node_t *coperand = parse_unary(p);
            zan_ast_node_t *cn = zan_ast_new(p->arena, AST_CAST_EXPR, loc);
            cn->cast.type = ctype;
            cn->cast.expr = coperand;
            return cn;
        }
        default: break;
        }
        if (paren_is_named_cast(p)) {
            parser_advance(p);
            zan_ast_node_t *ctype = parse_type_ref(p);
            parser_expect(p, TK_RPAREN);
            zan_ast_node_t *coperand = parse_unary(p);
            zan_ast_node_t *cn = zan_ast_new(p->arena, AST_CAST_EXPR, loc);
            cn->cast.type = ctype;
            cn->cast.expr = coperand;
            return cn;
        }
        parser_advance(p);
        zan_ast_node_t *expr = parse_expression(p);
        if (parser_check(p, TK_COMMA)) {
            zan_ast_node_t *tup = zan_ast_new(p->arena, AST_TUPLE_EXPR, loc);
            zan_ast_list_init(&tup->tuple_expr.items);
            zan_ast_list_push(&tup->tuple_expr.items, expr, p->arena);
            while (parser_match(p, TK_COMMA)) {
                if (parser_check(p, TK_RPAREN) || parser_check(p, TK_EOF)) break;
                if (p->current.kind == TK_IDENT &&
                    zan_lexer_peek(p->lex).kind == TK_COLON) {
                    parser_advance(p);
                    parser_advance(p);
                }
                zan_ast_node_t *item = parse_expression(p);
                zan_ast_list_push(&tup->tuple_expr.items, item, p->arena);
            }
            parser_expect(p, TK_RPAREN);
            return tup;
        }
        parser_expect(p, TK_RPAREN);
        if (parser_check(p, TK_ARROW)) {
            parser_advance(p);
            zan_ast_node_t *body = parse_expression(p);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_LAMBDA, loc);
            zan_ast_list_init(&n->lambda.params);
            if (expr->kind == AST_IDENTIFIER) {
                zan_ast_node_t *param = zan_ast_new(p->arena, AST_PARAM, loc);
                param->param.name = expr->ident.name;
                param->param.type = NULL;
                param->param.default_val = NULL;
                zan_ast_list_push(&n->lambda.params, param, p->arena);
            }
            n->lambda.body = body;
            return n;
        }
        return expr;
    }
    case TK_DELEGATE: {
        zan_loc_t dloc = p->current.loc;
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_LAMBDA, dloc);
        zan_ast_list_init(&n->lambda.params);
        if (parser_match(p, TK_LPAREN)) {
            while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *param = parse_parameter(p);
                zan_ast_list_push(&n->lambda.params, param, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RPAREN);
        }
        n->lambda.body = parse_block(p);
        return n;
    }
    case TK_NEW: {
        parser_advance(p);
        zan_loc_t newloc = p->previous.loc;
        zan_ast_node_t *n = NULL;

        if (parser_check(p, TK_LBRACE)) {
            parser_advance(p);
            zan_loc_t loc = newloc;
            zan_ast_node_t *res = NULL;
            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *arg = parse_expression(p);
                if (arg->kind == AST_ASSIGNMENT) {
                    arg = arg->binary.right;
                }
                /* Convert.ToString(arg) */
                zan_ast_node_t *cid = zan_ast_new(p->arena, AST_IDENTIFIER, loc);
                cid->ident.name = (zan_istr_t){"Convert", 7};
                cid->ident.inst_type_ref = NULL;
                zan_ast_node_t *cmem = zan_ast_new(p->arena, AST_MEMBER_ACCESS, loc);
                cmem->member.object = cid;
                cmem->member.name = (zan_istr_t){"ToString", 8};
                cmem->member.null_cond = 0;
                zan_ast_node_t *call = zan_ast_new(p->arena, AST_CALL, loc);
                call->call.callee = cmem;
                zan_ast_list_init(&call->call.args);
                zan_ast_list_init(&call->call.type_args);
                zan_ast_list_push(&call->call.args, arg, p->arena);

                if (!res) {
                    res = call;
                } else {
                    zan_ast_node_t *tab = zan_ast_new(p->arena, AST_STRING_LITERAL, loc);
                    tab->str_val = (zan_istr_t){"\t", 1};
                    zan_ast_node_t *add1 = zan_ast_new(p->arena, AST_BINARY, loc);
                    add1->binary.op = TK_PLUS;
                    add1->binary.left = res;
                    add1->binary.right = tab;
                    zan_ast_node_t *add2 = zan_ast_new(p->arena, AST_BINARY, loc);
                    add2->binary.op = TK_PLUS;
                    add2->binary.left = add1;
                    add2->binary.right = call;
                    res = add2;
                }
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RBRACE);
            if (!res) {
                res = zan_ast_new(p->arena, AST_STRING_LITERAL, loc);
                res->str_val = (zan_istr_t){"", 0};
            }
            return res;
        }

        zan_ast_node_t *type = parse_type_ref(p);
        n = zan_ast_new(p->arena, AST_NEW_EXPR, loc);
        n->new_expr.type = type;
        zan_ast_list_init(&n->new_expr.args);
        n->new_expr.is_array = false;
        n->new_expr.array_init = false;

        if (parser_check(p, TK_LBRACKET) && !type->type_ref.is_array) {
            parser_advance(p);
            n->new_expr.is_array = true;
            while (!parser_check(p, TK_RBRACKET) && !parser_check(p, TK_EOF)) {
                if (n->new_expr.array_rank >= 16) {
                    /* 多维数组维度上限校验 (最大支持 16 维) */
                    zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 38,
                                  "array rank specifier is too deep (max 16)");
                }
                zan_ast_node_t *dim = parse_expression(p);
                zan_ast_list_push(&n->new_expr.args, dim, p->arena);
                n->new_expr.array_rank++;
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RBRACKET);
            while (parser_check(p, TK_LBRACKET)) {
                int rank = array_suffix_rank(p);
                if (rank <= 0) break;
                parser_advance(p);
                for (int c = 1; c < rank; c++) parser_advance(p);
                parser_advance(p);
                if (rank > 16) {
                    zan_diag_emit_code(p->diag, DIAG_ERROR, loc, 38,
                                  "array rank specifier is too deep (max 16)");
                    continue;
                }
                zan_ast_node_t *w = zan_ast_new(p->arena, AST_TYPE_REF, loc);
                w->type_ref.name = type->type_ref.name;
                w->type_ref.is_array = true;
                w->type_ref.array_rank = rank;
                w->type_ref.array_element = type;
                zan_ast_list_init(&w->type_ref.type_args);
                type = w;
            }
            if (n->new_expr.array_rank > 0) {
                zan_ast_node_t *w = zan_ast_new(p->arena, AST_TYPE_REF, loc);
                w->type_ref.name = type->type_ref.name;
                w->type_ref.is_array = true;
                w->type_ref.array_rank = n->new_expr.array_rank;
                w->type_ref.array_element = type;
                zan_ast_list_init(&w->type_ref.type_args);
                type = w;
            }
            n->new_expr.type = type;
        } else if (parser_match(p, TK_LPAREN)) {
            while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *arg = parse_call_arg(p);
                zan_ast_list_push(&n->new_expr.args, arg, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RPAREN);
        }
        if (parser_check(p, TK_LBRACE) && type->kind == AST_TYPE_REF &&
            type->type_ref.is_array && !n->new_expr.is_array) {
            /* new T[] { */
            n->new_expr.is_array = true;
            n->new_expr.array_init = true;
        }
        if (parser_match(p, TK_LBRACE)) {
            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                if (parser_match(p, TK_LBRACE)) {
                    while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                        zan_ast_node_t *sub = parse_expression(p);
                        zan_ast_list_push(&n->new_expr.args, sub, p->arena);
                        if (!parser_match(p, TK_COMMA)) break;
                    }
                    parser_expect(p, TK_RBRACE);
                } else {
                    zan_ast_node_t *item = parse_expression(p);
                    zan_ast_list_push(&n->new_expr.args, item, p->arena);
                }
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RBRACE);
        }
        return n;
    }
    case TK_TYPEOF: {
        parser_advance(p);
        parser_expect(p, TK_LPAREN);
        zan_ast_node_t *type = parse_type_ref(p);
        parser_expect(p, TK_RPAREN);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_TYPEOF_EXPR, loc);
        n->cast.type = type;
        return n;
    }
    case TK_SIZEOF: {
        parser_advance(p);
        parser_expect(p, TK_LPAREN);
        zan_ast_node_t *type = parse_type_ref(p);
        parser_expect(p, TK_RPAREN);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_SIZEOF_EXPR, loc);
        n->cast.type = type;
        return n;
    }
    default:
        zan_diag_emit_code(p->diag, DIAG_ERROR, loc, 45,
                      "unexpected token '%s' in expression",
                      zan_token_kind_name(p->current.kind));
        /* 结构性收尾符（; ) ] } ,）与 EOF 留给外层构造去消费：吃掉它们会让
         * 外层紧接着再报 "expected ';'"，把真正的原因盖住（B-ID91 级联）。 */
        switch (p->current.kind) {
        case TK_SEMICOLON: case TK_RPAREN: case TK_RBRACKET:
        case TK_RBRACE: case TK_COMMA: case TK_EOF:
            break;
        default:
            parser_advance(p);
            break;
        }
        return parser_error_node(p);
    }
}

static bool is_type_kw(zan_token_kind_t k);

static zan_ast_node_t *parse_call_arg(zan_parser_t *p) {
    if (parser_check(p, TK_IDENT)) {
        zan_token_t peek = zan_lexer_peek(p->lex);
        if (peek.kind == TK_COLON) {
            zan_loc_t loc = p->current.loc;
            zan_istr_t name = p->current.str_val;
            parser_advance(p);
            parser_advance(p);
            zan_ast_node_t *expr = parse_expression(p);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_NAMED_ARG, loc);
            n->named_arg.name = name;
            n->named_arg.expr = expr;
            return n;
        }
    }
    if (!parser_check(p, TK_REF) && !parser_check(p, TK_OUT)) {
        return parse_expression(p);
    }
    zan_loc_t loc = p->current.loc;
    int is_out = parser_check(p, TK_OUT);
    parser_advance(p);
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_REF_ARG, loc);
    n->ref_arg.is_out = is_out;
    n->ref_arg.decl_type = NULL;
    if (is_out && is_type_kw(p->current.kind)) {
        n->ref_arg.decl_type = parse_type_ref(p);
        zan_ast_node_t *id = zan_ast_new(p->arena, AST_IDENTIFIER, p->current.loc);
        id->ident.name = p->current.str_val;
        parser_expect(p, TK_IDENT);
        n->ref_arg.expr = id;
        return n;
    }
    zan_ast_node_t *e = parse_expression(p);
    if (is_out && e->kind == AST_IDENTIFIER && parser_check(p, TK_IDENT)) {
        zan_ast_node_t *ty = zan_ast_new(p->arena, AST_TYPE_REF, e->loc);
        ty->type_ref.name = e->ident.name;
        zan_ast_list_init(&ty->type_ref.type_args);
        n->ref_arg.decl_type = ty;
        zan_ast_node_t *id = zan_ast_new(p->arena, AST_IDENTIFIER, p->current.loc);
        id->ident.name = p->current.str_val;
        parser_advance(p);
        e = id;
    }
    n->ref_arg.expr = e;
    return n;
}

static bool is_type_kw(zan_token_kind_t k) {
    switch (k) {
    case TK_INT: case TK_LONG: case TK_SHORT: case TK_BYTE:
    case TK_UINT: case TK_ULONG: case TK_USHORT: case TK_SBYTE:
    case TK_FLOAT: case TK_DOUBLE: case TK_DECIMAL: case TK_BOOL: case TK_CHAR:
    case TK_STRING: case TK_VOID: case TK_OBJECT: case TK_NINT:
        return true;
    default:
        return false;
    }
}

/* Disambiguate `name< */
static bool looks_like_call_type_args(zan_parser_t *p) {
    zan_lexer_mark_t saved_lex; zan_lexer_mark(p->lex, &saved_lex, p->arena);
    zan_token_t saved_cur = p->current;
    zan_token_t saved_prev = p->previous;
    bool ok = false;
    int depth = 0;
    while (p->current.kind != TK_EOF) {
        zan_token_kind_t k = p->current.kind;
        if (k == TK_LESS) {
            depth++;
        } else if (k == TK_GREATER || k == TK_GREATER_GREATER ||
                   k == TK_GREATER_GREATER_GREATER) {
            depth -= (k == TK_GREATER_GREATER_GREATER) ? 3
                   : (k == TK_GREATER_GREATER) ? 2 : 1;
            if (depth <= 0) {
                ok = (zan_lexer_peek(p->lex).kind == TK_LPAREN);
                break;
            }
        } else if (k == TK_IDENT || k == TK_COMMA || k == TK_DOT ||
                   k == TK_LBRACKET || k == TK_RBRACKET || k == TK_QUESTION ||
                   is_type_kw(k)) {
        } else {
            break;
        }
        parser_advance(p);
    }
    zan_lexer_reset(p->lex, &saved_lex);
    p->current = saved_cur;
    p->previous = saved_prev;
    return ok;
}

/* Disambiguate `Name< */
static bool looks_like_type_args_before_dot(zan_parser_t *p) {
    zan_lexer_mark_t saved_lex; zan_lexer_mark(p->lex, &saved_lex, p->arena);
    zan_token_t saved_cur = p->current;
    zan_token_t saved_prev = p->previous;
    bool ok = false;
    int depth = 0;
    while (p->current.kind != TK_EOF) {
        zan_token_kind_t k = p->current.kind;
        if (k == TK_LESS) {
            depth++;
        } else if (k == TK_GREATER || k == TK_GREATER_GREATER ||
                   k == TK_GREATER_GREATER_GREATER) {
            depth -= (k == TK_GREATER_GREATER_GREATER) ? 3
                   : (k == TK_GREATER_GREATER) ? 2 : 1;
            if (depth <= 0) {
                ok = (zan_lexer_peek(p->lex).kind == TK_DOT);
                break;
            }
        } else if (k == TK_IDENT || k == TK_COMMA || k == TK_DOT ||
                   k == TK_LBRACKET || k == TK_RBRACKET || k == TK_QUESTION ||
                   is_type_kw(k)) {
        } else {
            break;
        }
        parser_advance(p);
    }
    zan_lexer_reset(p->lex, &saved_lex);
    p->current = saved_cur;
    p->previous = saved_prev;
    return ok;
}

/* Disambiguate `Name< */
static bool looks_like_type_args_before_brace(zan_parser_t *p) {
    zan_lexer_mark_t saved_lex; zan_lexer_mark(p->lex, &saved_lex, p->arena);
    zan_token_t saved_cur = p->current;
    zan_token_t saved_prev = p->previous;
    bool ok = false;
    int depth = 0;
    while (p->current.kind != TK_EOF) {
        zan_token_kind_t k = p->current.kind;
        if (k == TK_LESS) {
            depth++;
        } else if (k == TK_GREATER || k == TK_GREATER_GREATER ||
                   k == TK_GREATER_GREATER_GREATER) {
            depth -= (k == TK_GREATER_GREATER_GREATER) ? 3
                   : (k == TK_GREATER_GREATER) ? 2 : 1;
            if (depth <= 0) {
                ok = (zan_lexer_peek(p->lex).kind == TK_LBRACE);
                break;
            }
        } else if (k == TK_IDENT || k == TK_COMMA || k == TK_DOT ||
                   k == TK_LBRACKET || k == TK_RBRACKET || k == TK_QUESTION ||
                   is_type_kw(k)) {
        } else {
            break;
        }
        parser_advance(p);
    }
    zan_lexer_reset(p->lex, &saved_lex);
    p->current = saved_cur;
    p->previous = saved_prev;
    return ok;
}

static void desugar_task_join(zan_ast_node_t *member) {
    zan_istr_t obj_name;
    zan_istr_t m = member->member.name;
    if (!member->member.object ||
        member->member.object->kind != AST_IDENTIFIER) return;
    obj_name = member->member.object->ident.name;
    if (obj_name.len != 4 || memcmp(obj_name.str, "Task", 4) != 0) return;
    if (!((m.len == 7 && memcmp(m.str, "WhenAll", 7) == 0) ||
          (m.len == 7 && memcmp(m.str, "WhenAny", 7) == 0))) return;
    member->member.object->ident.name = (zan_istr_t){"TaskJoin", 8};
}

static bool is_case_type_pattern(zan_parser_t *p);

static zan_ast_node_t *parse_postfix(zan_parser_t *p) {
    zan_ast_node_t *expr = parse_primary(p);

    for (;;) {
        zan_loc_t loc = p->current.loc;

        if (parser_check(p, TK_LBRACE) &&
            (expr->kind == AST_IDENTIFIER || expr->kind == AST_MEMBER_ACCESS ||
             expr->kind == AST_CALL)) {
            if (expr->kind == AST_IDENTIFIER && expr->ident.inst_type_ref) {
                zan_ast_node_t *n = zan_ast_new(p->arena, AST_NEW_EXPR, loc);
                n->new_expr.type = expr->ident.inst_type_ref;
                zan_ast_list_init(&n->new_expr.args);
                zan_ast_list_init(&n->new_expr.arg_inits);
                parser_advance(p);
                while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                    zan_ast_node_t *field_init;
                    if (parser_check(p, TK_IDENT) &&
                        zan_lexer_peek(p->lex).kind == TK_EQ) {
                        zan_loc_t nloc = p->current.loc;
                        zan_istr_t name = p->current.str_val;
                        parser_advance(p);
                        parser_advance(p);
                        if (parser_match(p, TK_LBRACE)) {
                            field_init = zan_ast_new(p->arena, AST_COLL_INIT, nloc);
                            field_init->coll_init.name = name;
                            zan_ast_list_init(&field_init->coll_init.items);
                            while (!parser_check(p, TK_RBRACE) &&
                                   !parser_check(p, TK_EOF)) {
                                zan_ast_node_t *item = parse_expression(p);
                                zan_ast_list_push(&field_init->coll_init.items,
                                                  item, p->arena);
                                if (!parser_match(p, TK_COMMA)) break;
                            }
                            parser_expect(p, TK_RBRACE);
                        } else {
                            field_init = zan_ast_new(p->arena, AST_ASSIGNMENT, nloc);
                            zan_ast_node_t *lhs =
                                zan_ast_new(p->arena, AST_IDENTIFIER, nloc);
                            lhs->ident.name = name;
                            field_init->binary.op = TK_EQ;
                            field_init->binary.left = lhs;
                            field_init->binary.right = parse_expression(p);
                        }
                    } else {
                        field_init = parse_expression(p);
                    }
                    zan_ast_list_push(&n->new_expr.arg_inits, field_init,
                                      p->arena);
                    if (!parser_match(p, TK_COMMA)) break;
                }
                parser_expect(p, TK_RBRACE);
                expr = n;
                continue;
            }

            if (expr->kind == AST_CALL) {
                zan_ast_node_t *n = zan_ast_new(p->arena, AST_NEW_EXPR, loc);
                n->new_expr.call_init = expr;
                zan_ast_list_init(&n->new_expr.args);
                zan_ast_list_init(&n->new_expr.arg_inits);
                parser_advance(p);
                while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                    zan_ast_node_t *field_init;
                    if (parser_check(p, TK_IDENT) &&
                        zan_lexer_peek(p->lex).kind == TK_EQ) {
                        zan_loc_t nloc = p->current.loc;
                        zan_istr_t name = p->current.str_val;
                        parser_advance(p);
                        parser_advance(p);
                        if (parser_match(p, TK_LBRACE)) {
                            field_init = zan_ast_new(p->arena, AST_COLL_INIT, nloc);
                            field_init->coll_init.name = name;
                            zan_ast_list_init(&field_init->coll_init.items);
                            while (!parser_check(p, TK_RBRACE) &&
                                   !parser_check(p, TK_EOF)) {
                                zan_ast_node_t *item = parse_expression(p);
                                zan_ast_list_push(&field_init->coll_init.items,
                                                  item, p->arena);
                                if (!parser_match(p, TK_COMMA)) break;
                            }
                            parser_expect(p, TK_RBRACE);
                        } else {
                            field_init = zan_ast_new(p->arena, AST_ASSIGNMENT, nloc);
                            zan_ast_node_t *lhs =
                                zan_ast_new(p->arena, AST_IDENTIFIER, nloc);
                            lhs->ident.name = name;
                            field_init->binary.op = TK_EQ;
                            field_init->binary.left = lhs;
                            field_init->binary.right = parse_expression(p);
                        }
                    } else {
                        field_init = parse_expression(p);
                    }
                    zan_ast_list_push(&n->new_expr.arg_inits, field_init,
                                      p->arena);
                    if (!parser_match(p, TK_COMMA)) break;
                }
                parser_expect(p, TK_RBRACE);
                expr = n;
                continue;
            }
            zan_ast_node_t *type = zan_ast_new(p->arena, AST_TYPE_REF, expr->loc);
            if (expr->kind == AST_IDENTIFIER) {
                type->type_ref.name = expr->ident.name;
            } else {
                type->type_ref.name = expr->member.name;
            }
            zan_ast_list_init(&type->type_ref.type_args);

            zan_ast_node_t *n = zan_ast_new(p->arena, AST_NEW_EXPR, loc);
            n->new_expr.type = type;
            zan_ast_list_init(&n->new_expr.args);

            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *field_init;
                if (parser_check(p, TK_IDENT) &&
                    zan_lexer_peek(p->lex).kind == TK_EQ) {
                    zan_loc_t nloc = p->current.loc;
                    zan_istr_t name = p->current.str_val;
                    parser_advance(p);
                    parser_advance(p);
                    if (parser_match(p, TK_LBRACE)) {
                        field_init = zan_ast_new(p->arena, AST_COLL_INIT, nloc);
                        field_init->coll_init.name = name;
                        zan_ast_list_init(&field_init->coll_init.items);
                        while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                            zan_ast_node_t *item = parse_expression(p);
                            zan_ast_list_push(&field_init->coll_init.items, item,
                                              p->arena);
                            if (!parser_match(p, TK_COMMA)) break;
                        }
                        parser_expect(p, TK_RBRACE);
                    } else {
                        field_init = zan_ast_new(p->arena, AST_ASSIGNMENT, nloc);
                        zan_ast_node_t *lhs =
                            zan_ast_new(p->arena, AST_IDENTIFIER, nloc);
                        lhs->ident.name = name;
                        field_init->binary.op = TK_EQ;
                        field_init->binary.left = lhs;
                        field_init->binary.right = parse_expression(p);
                    }
                } else {
                    field_init = parse_expression(p);
                }
                zan_ast_list_push(&n->new_expr.args, field_init, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RBRACE);
            expr = n;
            continue;
        }

        if (parser_check(p, TK_DOT) || parser_check(p, TK_QUESTION_DOT)) {
            int null_cond = parser_check(p, TK_QUESTION_DOT);
            parser_advance(p);
            zan_istr_t mname = {0};
            if (parser_check(p, TK_IDENT)) {
                parser_advance(p);
                mname = p->previous.str_val;
            } else if (!parser_take_reserved_word(p, &mname)) {
                parser_diag_expected_name(p, loc, "expected member name after '.'");
                break;
            }
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_MEMBER_ACCESS, loc);
            n->member.object = expr;
            n->member.name = mname;
            n->member.null_cond = null_cond;
            desugar_task_join(n);
            expr = n;
        } else if (parser_match(p, TK_LPAREN)) {
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_CALL, loc);
            n->call.callee = expr;
            zan_ast_list_init(&n->call.args);
            zan_ast_list_init(&n->call.type_args);
            while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *arg = parse_call_arg(p);
                zan_ast_list_push(&n->call.args, arg, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RPAREN);
            expr = n;
        } else if (parser_check(p, TK_LESS) &&
                   (expr->kind == AST_IDENTIFIER || expr->kind == AST_MEMBER_ACCESS) &&
                   looks_like_call_type_args(p)) {
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_CALL, loc);
            n->call.callee = expr;
            zan_ast_list_init(&n->call.args);
            zan_ast_list_init(&n->call.type_args);
            parser_advance(p);
            while (!parser_check(p, TK_GREATER) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *ta = parse_type_ref(p);
                zan_ast_list_push(&n->call.type_args, ta, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect_gt(p);
            parser_expect(p, TK_LPAREN);
            while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *arg = parse_call_arg(p);
                zan_ast_list_push(&n->call.args, arg, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RPAREN);
            expr = n;
        } else if (parser_check(p, TK_LESS) && expr->kind == AST_IDENTIFIER &&
                   !expr->ident.inst_type_ref &&
                   (looks_like_type_args_before_dot(p) ||
                    looks_like_type_args_before_brace(p))) {
            zan_ast_node_t *tref = zan_ast_new(p->arena, AST_TYPE_REF, expr->loc);
            tref->type_ref.name = expr->ident.name;
            tref->type_ref.is_nullable = false;
            tref->type_ref.is_array = false;
            zan_ast_list_init(&tref->type_ref.type_args);
            parser_advance(p);
            while (!parser_check(p, TK_GREATER) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *ta = parse_type_ref(p);
                zan_ast_list_push(&tref->type_ref.type_args, ta, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect_gt(p);
            expr->ident.inst_type_ref = tref;
        } else if (parser_match(p, TK_LBRACKET)) {
            zan_ast_node_t *idx = parse_expression(p);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_INDEX, loc);
            n->index.object = expr;
            n->index.index = idx;
            zan_ast_list_init(&n->index.extra);
            while (parser_match(p, TK_COMMA))
                zan_ast_list_push(&n->index.extra, parse_expression(p),
                                  p->arena);
            parser_expect(p, TK_RBRACKET);
            expr = n;
        } else if (parser_check(p, TK_SWITCH)) {
            parser_advance(p);
            parser_expect(p, TK_LBRACE);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_SWITCH_EXPR, loc);
            n->switch_expr.expr = expr;
            zan_ast_list_init(&n->switch_expr.arms);
            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                zan_loc_t arm_loc = p->current.loc;
                zan_ast_node_t *arm = zan_ast_new(p->arena, AST_SWITCH_ARM, arm_loc);
                zan_ast_node_t *pattern = NULL;
                zan_ast_node_t *type_pattern = NULL;
                zan_ast_node_t *when_cond = NULL;
                zan_istr_t var_name = {0};
                bool is_default = false;

                if (parser_check(p, TK_DEFAULT) ||
                    (parser_check(p, TK_IDENT) && p->current.str_val.len == 1 &&
                     p->current.str_val.str[0] == '_')) {
                    parser_advance(p);
                    is_default = true;
                } else if (is_case_type_pattern(p)) {
                    type_pattern = parse_type_ref(p);
                    if (parser_check(p, TK_IDENT)) {
                        parser_advance(p);
                        var_name = p->previous.str_val;
                    }
                } else {
                    pattern = parse_expression(p);
                }
                if (parser_match(p, TK_WHEN)) {
                    when_cond = parse_expression(p);
                }
                parser_expect(p, TK_ARROW);
                zan_ast_node_t *result = parse_expression(p);

                arm->switch_arm.pattern = pattern;
                arm->switch_arm.type_pattern = type_pattern;
                arm->switch_arm.when_cond = when_cond;
                arm->switch_arm.result = result;
                arm->switch_arm.var_name = var_name;
                arm->switch_arm.is_default = is_default;
                zan_ast_list_push(&n->switch_expr.arms, arm, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RBRACE);
            expr = n;
        } else if (parser_check(p, TK_IDENT) &&
                   p->current.str_val.len == 4 &&
                   memcmp(p->current.str_val.str, "with", 4) == 0 &&
                   zan_lexer_peek(p->lex).kind == TK_LBRACE) {
            parser_advance(p);
            parser_expect(p, TK_LBRACE);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_WITH_EXPR, loc);
            n->with_expr.expr = expr;
            zan_ast_list_init(&n->with_expr.assigns);
            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                parser_expect(p, TK_IDENT);
                zan_istr_t fname = p->previous.str_val;
                parser_expect(p, TK_EQ);
                zan_ast_node_t *asg =
                    zan_ast_new(p->arena, AST_ASSIGNMENT, loc);
                asg->binary.op = TK_EQ;
                zan_ast_node_t *lhs =
                    zan_ast_new(p->arena, AST_IDENTIFIER, loc);
                lhs->ident.name = fname;
                asg->binary.left = lhs;
                asg->binary.right = parse_expression(p);
                zan_ast_list_push(&n->with_expr.assigns, asg, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RBRACE);
            expr = n;
        } else if (parser_check(p, TK_BANG)) {
            parser_advance(p);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_POSTFIX_UNARY, loc);
            n->unary.op = TK_BANG;
            n->unary.operand = expr;
            expr = n;
            continue;
        } else if (parser_check(p, TK_PLUS_PLUS) || parser_check(p, TK_MINUS_MINUS)) {
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_POSTFIX_UNARY, loc);
            n->unary.op = p->current.kind;
            parser_advance(p);
            n->unary.operand = expr;
            expr = n;
            break;
        } else {
            break;
        }
    }

    return expr;
}

