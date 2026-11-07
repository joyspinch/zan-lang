#define ZAN_PARSER_MAX_EXPR_DEPTH 512

static zan_ast_node_t *parse_unary_inner(zan_parser_t *p);

static zan_ast_node_t *parser_expr_too_deep(zan_parser_t *p) {
    if (!p->expr_depth_reported) {
        p->expr_depth_reported = true;
        zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 46,
                      "expression nesting too deep (max %d)",
                      ZAN_PARSER_MAX_EXPR_DEPTH);
    }
    return parser_error_node(p);
}

static zan_ast_node_t *parse_unary(zan_parser_t *p) {
    if (p->expr_depth >= ZAN_PARSER_MAX_EXPR_DEPTH)
        return parser_expr_too_deep(p);
    p->expr_depth++;
    zan_ast_node_t *n = parse_unary_inner(p);
    p->expr_depth--;
    return n;
}

static zan_ast_node_t *parse_unary_inner(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;

    if (parser_check(p, TK_AWAIT)) {
        parser_advance(p);
        zan_ast_node_t *expr = parse_unary(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_AWAIT_EXPR, loc);
        n->await_expr.expr = expr;
        return n;
    }

    /* 一元正号 `+x`：数值上是恒等（irgen 的 `case TK_PLUS: return operand`），
       类/结构体上走 static `operator +` 的重载路径（emit_unary_op_overload）。
       自举后端的前缀表本来就有 `+`（src/selfhost/parser.zan 的 ParseUnaryInner），
       宿主侧漏了它 → 两个后端在语法层就已经分叉（宿主报 "unexpected token
       '+' in expression"，自举照常编译）。`+=` / `++` 是各自独立的 token，
       前缀位置上的 `+` 只可能是一元正号。 */
    if (parser_check(p, TK_BANG) || parser_check(p, TK_MINUS) ||
        parser_check(p, TK_PLUS) || parser_check(p, TK_TILDE) ||
        parser_check(p, TK_PLUS_PLUS) || parser_check(p, TK_MINUS_MINUS)) {
        zan_token_kind_t op = p->current.kind;
        parser_advance(p);
        zan_ast_node_t *operand = parse_unary(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_UNARY, loc);
        n->unary.op = op;
        n->unary.operand = operand;
        return n;
    }

    return parse_postfix(p);
}

static int get_precedence(zan_token_kind_t kind) {
    switch (kind) {
    case TK_STAR: case TK_SLASH: case TK_PERCENT: return 12;
    case TK_PLUS: case TK_MINUS: return 11;
    case TK_LESS_LESS: case TK_GREATER_GREATER: case TK_GREATER_GREATER_GREATER: return 10;
    case TK_LESS: case TK_GREATER: case TK_LESS_EQ: case TK_GREATER_EQ:
    case TK_IS: case TK_AS: return 9;
    case TK_EQ_EQ: case TK_BANG_EQ: return 8;
    case TK_AMP: return 7;
    case TK_CARET: return 6;
    case TK_PIPE: return 5;
    case TK_AMP_AMP: return 4;
    case TK_PIPE_PIPE: return 3;
    case TK_QUESTION_QUESTION: return 2;
    default: return 0;
    }
}

static bool is_binary_op(zan_token_kind_t kind) {
    return get_precedence(kind) > 0;
}

static bool token_starts_expr(zan_token_kind_t k) {
    switch (k) {
    case TK_IDENT: case TK_INT_LIT: case TK_FLOAT_LIT: case TK_DECIMAL_LIT: case TK_STRING_LIT:
    case TK_CHAR_LIT: case TK_INTERP_START: case TK_LPAREN: case TK_MINUS:
    case TK_PLUS: case TK_BANG: case TK_TILDE: case TK_THIS: case TK_BASE:
    case TK_NEW: case TK_NULL: case TK_TRUE: case TK_FALSE:
        return true;
    default:
        return false;
    }
}

static bool is_question_is_conditional(zan_parser_t *p) {
    zan_lexer_mark_t saved_lex; zan_lexer_mark(p->lex, &saved_lex, p->arena);
    zan_token_t saved_cur = p->current;
    zan_token_t saved_prev = p->previous;
    int depth = 0;
    bool in_arm = false;
    bool result = false;
    while (p->current.kind != TK_EOF) {
        zan_token_kind_t k = p->current.kind;
        if (k == TK_SEMICOLON || k == TK_COMMA || k == TK_LBRACE
            || k == TK_RBRACE || k == TK_ARROW) {
            break;
        }
        if (k == TK_LPAREN || k == TK_LBRACKET
            || (!in_arm && k == TK_LESS)) {
            depth++;
        } else if (k == TK_RPAREN || k == TK_RBRACKET || k == TK_GREATER) {
            if (depth == 0) { break; }
            depth--;
        } else if (k == TK_GREATER_GREATER) {
            if (depth == 0) { break; }
            depth -= depth >= 2 ? 2 : depth;
        } else if (k == TK_GREATER_GREATER_GREATER) {
            if (depth == 0) { break; }
            depth -= depth >= 3 ? 3 : depth;
        } else if (!in_arm && k == TK_QUESTION && depth == 0) {
            if (!token_starts_expr(zan_lexer_peek(p->lex).kind)) {
                break;
            }
            in_arm = true;
        } else if (in_arm && k == TK_COLON && depth == 0) {
            result = true;
            break;
        }
        parser_advance(p);
    }
    zan_lexer_reset(p->lex, &saved_lex);
    p->current = saved_cur;
    p->previous = saved_prev;
    return result;
}

#define ZAN_PARSER_MAX_BINOP_CHAIN 16384

static zan_ast_node_t *parse_binary(zan_parser_t *p, int min_prec) {
    zan_ast_node_t *left = parse_unary(p);
    int chain = 0;

    while (is_binary_op(p->current.kind)) {
        int prec = get_precedence(p->current.kind);
        if (prec < min_prec) break;

        if (++chain > ZAN_PARSER_MAX_BINOP_CHAIN) {
            if (!p->chain_cap_reported) {
                zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 47,
                              "expression chain too long (max %d operators); "
                              "split it into statements",
                              ZAN_PARSER_MAX_BINOP_CHAIN);
                p->chain_cap_reported = true;
            }
            break;
        }

        zan_token_kind_t op = p->current.kind;
        zan_loc_t loc = p->current.loc;
        parser_advance(p);

        if (op == TK_IS || op == TK_AS) {
            zan_ast_node_t *n = zan_ast_new(p->arena,
                op == TK_IS ? AST_IS_EXPR : AST_AS_EXPR, loc);
            n->type_test.expr = left;
            n->type_test.type = NULL;
            n->type_test.var_name = (zan_istr_t){NULL, 0};
            n->type_test.is_not = false;
            if (op == TK_AS) {
                p->type_no_nullable =
                    is_question_is_conditional(p) ? 1 : 0;
                n->type_test.type = parse_type_ref(p);
                p->type_no_nullable = 0;
                left = n;
                continue;
            }
            if (parser_match(p, TK_NOT)) {
                n->type_test.is_not = true;
            }
            if (parser_check(p, TK_NULL)) {
                parser_advance(p);
            } else {
                p->type_no_nullable =
                    is_question_is_conditional(p) ? 1 : 0;
                n->type_test.type = parse_type_ref(p);
                p->type_no_nullable = 0;
                if (parser_check(p, TK_IDENT)) {
                    zan_token_t after = zan_lexer_peek(p->lex);
                    if (after.kind != TK_IS && after.kind != TK_AS) {
                        parser_advance(p);
                        n->type_test.var_name = p->previous.str_val;
                    }
                }
            }
            left = n;
            continue;
        }

        zan_ast_node_t *right = parse_binary(p, prec + 1);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_BINARY, loc);
        n->binary.op = op;
        if (op == TK_PLUS || op == TK_MINUS || op == TK_STAR) {
            if (p->checked_depth > 0) n->binary.checked = 1;
            else if (p->unchecked_depth > 0) n->binary.checked = -1;
        }
        n->binary.left = left;
        n->binary.right = right;
        left = n;
    }

    return left;
}

static zan_ast_node_t *parse_conditional(zan_parser_t *p) {
    zan_ast_node_t *expr = parse_binary(p, 1);

    if (parser_match(p, TK_QUESTION)) {
        zan_loc_t loc = p->previous.loc;
        zan_ast_node_t *then_expr = parse_expression(p);
        parser_expect(p, TK_COLON);
        zan_ast_node_t *else_expr = parse_expression(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_CONDITIONAL, loc);
        n->conditional.cond = expr;
        n->conditional.then_expr = then_expr;
        n->conditional.else_expr = else_expr;
        return n;
    }

    return expr;
}

static bool is_assign_op(zan_token_kind_t kind) {
    switch (kind) {
    case TK_EQ: case TK_PLUS_EQ: case TK_MINUS_EQ: case TK_STAR_EQ:
    case TK_SLASH_EQ: case TK_PERCENT_EQ: case TK_AMP_EQ: case TK_PIPE_EQ:
    case TK_CARET_EQ: case TK_LESS_LESS_EQ: case TK_GREATER_GREATER_EQ:
    case TK_GREATER_GREATER_GREATER_EQ:
        return true;
    default:
        return false;
    }
}

static zan_ast_node_t *parse_expression_inner(zan_parser_t *p);

static zan_ast_node_t *parse_expression(zan_parser_t *p) {
    if (p->expr_tail_depth >= ZAN_PARSER_MAX_EXPR_DEPTH)
        return parser_expr_too_deep(p);
    p->expr_tail_depth++;
    zan_ast_node_t *n = parse_expression_inner(p);
    p->expr_tail_depth--;
    return n;
}

static zan_ast_node_t *parse_expression_inner(zan_parser_t *p) {
    zan_ast_node_t *expr = parse_conditional(p);

    if (is_assign_op(p->current.kind)) {
        zan_token_kind_t op = p->current.kind;
        zan_loc_t loc = p->current.loc;
        parser_advance(p);

        if (op == TK_EQ && parser_check(p, TK_LBRACE) &&
            (expr->kind == AST_IDENTIFIER || expr->kind == AST_MEMBER_ACCESS)) {
            zan_istr_t name = (expr->kind == AST_IDENTIFIER)
                                  ? expr->ident.name
                                  : expr->member.name;
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_COLL_INIT, loc);
            n->coll_init.name = name;
            zan_ast_list_init(&n->coll_init.items);
            parser_advance(p);
            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *item = parse_expression(p);
                zan_ast_list_push(&n->coll_init.items, item, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RBRACE);
            return n;
        }

        zan_ast_node_t *right = parse_expression(p);

        zan_token_kind_t base = TK_EOF;
        switch (op) {
        case TK_PLUS_EQ:            base = TK_PLUS; break;
        case TK_MINUS_EQ:           base = TK_MINUS; break;
        case TK_STAR_EQ:            base = TK_STAR; break;
        case TK_SLASH_EQ:           base = TK_SLASH; break;
        case TK_PERCENT_EQ:         base = TK_PERCENT; break;
        case TK_AMP_EQ:             base = TK_AMP; break;
        case TK_PIPE_EQ:            base = TK_PIPE; break;
        case TK_CARET_EQ:           base = TK_CARET; break;
        case TK_LESS_LESS_EQ:       base = TK_LESS_LESS; break;
        case TK_GREATER_GREATER_EQ: base = TK_GREATER_GREATER; break;
        case TK_GREATER_GREATER_GREATER_EQ: base = TK_GREATER_GREATER_GREATER; break;
        default: break;
        }
        if (base != TK_EOF) {
            zan_ast_node_t *bin = zan_ast_new(p->arena, AST_BINARY, loc);
            bin->binary.op = base;
            bin->binary.left = expr;
            bin->binary.right = right;
            right = bin;
            op = TK_EQ;
        }

        zan_ast_node_t *n = zan_ast_new(p->arena, AST_ASSIGNMENT, loc);
        n->binary.op = op;
        n->binary.compound_base = base;
        n->binary.left = expr;
        n->binary.right = right;
        return n;
    }

    return expr;
}

