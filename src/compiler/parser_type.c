static zan_ast_node_t *parse_delegate_decl(zan_parser_t *p, uint32_t mods) {
    parser_advance(p);
    zan_loc_t dloc = p->current.loc;
    zan_ast_node_t *ret_type = parse_type_ref(p);
    parser_expect(p, TK_IDENT);
    zan_istr_t dname = p->previous.str_val;
    zan_ast_list_t dtype_params;
    zan_ast_list_init(&dtype_params);
    if (parser_match(p, TK_LESS)) {
        while (!parser_check(p, TK_GREATER) && !parser_check(p, TK_EOF)) {
            int variance = 0;
            if (parser_match(p, TK_OUT)) {
                variance = 1;
            } else if (parser_match(p, TK_IN)) {
                variance = -1;
            }
            if (parser_check(p, TK_IDENT)) {
                parser_advance(p);
                zan_ast_node_t *tp = zan_ast_new(p->arena, AST_IDENTIFIER, p->previous.loc);
                tp->ident.name = p->previous.str_val;
                tp->ident.variance = variance;
                zan_ast_list_push(&dtype_params, tp, p->arena);
            }
            if (!parser_match(p, TK_COMMA)) break;
        }
        parser_expect(p, TK_GREATER);
    }
    zan_ast_list_t dparams = parse_param_list(p);
    parser_expect(p, TK_SEMICOLON);
    zan_ast_node_t *ddecl = zan_ast_new(p->arena, AST_DELEGATE_DECL, dloc);
    ddecl->method_decl.name = dname;
    ddecl->method_decl.return_type = ret_type;
    ddecl->method_decl.params = dparams;
    ddecl->method_decl.type_params = dtype_params;
    ddecl->method_decl.body = NULL;
    ddecl->method_decl.modifiers = mods;
    return ddecl;
}

static bool is_intrinsic_attr_name(zan_istr_t name) {
    if (!name.str) return false;
    if (name.len == 9 && memcmp(name.str, "Intrinsic", 9) == 0) return true;
    if (name.len == 18 && memcmp(name.str, "IntrinsicAttribute", 18) == 0) return true;
    return false;
}

static bool parse_top_level_decl(zan_parser_t *p, zan_ast_node_t *unit) {
    /* 解析类型特性：保留特性元数据，[StructLayout] 触发 C 内存布局对齐 */
    bool has_c_layout = false;
    bool has_explicit_layout = false;
    bool has_intrinsic = false;
    zan_ast_list_t type_attrs;
    zan_ast_list_init(&type_attrs);
    parse_attr_usages(p, &type_attrs, NULL, NULL, NULL);
    for (int _ai = 0; _ai < type_attrs.count; _ai++) {
        zan_istr_t _n = type_attrs.items[_ai]->attribute.name->ident.name;
        if (is_intrinsic_attr_name(_n)) has_intrinsic = true;
        if (_n.str && _n.len == 12 && memcmp(_n.str, "StructLayout", 12) == 0) {
            has_c_layout = true;
            /* `LayoutKind */
            zan_ast_list_t *_args = &type_attrs.items[_ai]->attribute.args;
            for (int _aj = 0; _aj < _args->count; _aj++) {
                zan_ast_node_t *_a = _args->items[_aj];
                zan_istr_t _k = {NULL, 0};
                if (_a->kind == AST_MEMBER_ACCESS) _k = _a->member.name;
                else if (_a->kind == AST_IDENTIFIER) _k = _a->ident.name;
                if (_k.str && _k.len == 8 && memcmp(_k.str, "Explicit", 8) == 0)
                    has_explicit_layout = true;
            }
        }
    }

    uint32_t mods = parse_modifiers(p);
    if (has_intrinsic) mods |= MOD_INTRINSIC;

    if (parser_check(p, TK_IDENT) && p->current.str_val.len == 7 &&
        memcmp(p->current.str_val.str, "partial", 7) == 0) {
        zan_token_kind_t nk = zan_lexer_peek(p->lex).kind;
        if (nk == TK_CLASS || nk == TK_STRUCT || nk == TK_INTERFACE) {
            parser_advance(p);
            mods |= MOD_PARTIAL;
        }
    }

    if (parser_check(p, TK_IDENT) && p->current.str_val.len == 6 &&
        memcmp(p->current.str_val.str, "record", 6) == 0 &&
        zan_lexer_peek(p->lex).kind == TK_IDENT) {
        parser_advance(p);
        parser_expect(p, TK_IDENT);
        zan_istr_t rname = p->previous.str_val;
        zan_ast_list_t rparams = parse_param_list(p);
        parser_expect(p, TK_SEMICOLON);
        gen_record_class(unit, rname, &rparams, p->arena, p->diag);
        return true;
    }

    if (parser_check(p, TK_CLASS) || parser_check(p, TK_STRUCT) ||
        parser_check(p, TK_INTERFACE) || parser_check(p, TK_ENUM)) {
        zan_ast_node_t *decl = parse_type_decl(p, mods);
        decl->type_decl.is_c_layout = has_c_layout;
        decl->type_decl.is_explicit_layout = has_explicit_layout;
        if (type_attrs.count > 0)
            zan_ast_ensure_decl_meta(decl, p->arena)->attributes = type_attrs;
        zan_ast_list_push(&unit->comp_unit.decls, decl, p->arena);
        return true;
    }
    if (parser_check(p, TK_DELEGATE)) {
        zan_ast_node_t *ddecl = parse_delegate_decl(p, mods);
        zan_ast_list_push(&unit->comp_unit.decls, ddecl, p->arena);
        return true;
    }
    zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 35,
                  "expected type declaration (class, struct, interface, enum, or delegate)");
    return false;
}

static zan_ast_node_t *parse_qualified_name(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    zan_ast_node_t *node = zan_ast_new(p->arena, AST_QUALIFIED_NAME, loc);
    zan_ast_list_init(&node->qualified_name.parts);

    parser_expect(p, TK_IDENT);
    zan_ast_node_t *part = zan_ast_new(p->arena, AST_IDENTIFIER, p->previous.loc);
    part->ident.name = p->previous.str_val;
    zan_ast_list_push(&node->qualified_name.parts, part, p->arena);

    while (parser_match(p, TK_DOT)) {
        if (!parser_check(p, TK_IDENT)) break;
        parser_advance(p);
        part = zan_ast_new(p->arena, AST_IDENTIFIER, p->previous.loc);
        part->ident.name = p->previous.str_val;
        zan_ast_list_push(&node->qualified_name.parts, part, p->arena);
    }

    return node;
}

static int array_suffix_rank(zan_parser_t *p) {
    zan_lexer_t *lx = p->lex;
    const char *s = lx->source;
    size_t i = lx->pos;
    size_t n = lx->source_len;
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' ||
                     s[i] == '\n'))
        i++;
    if (i >= n) return 0;
    if (s[i] == ']') return 1;
    if (s[i] != ',') return 0;
    int rank = 1;
    while (i < n) {
        if (s[i] == ',') { rank++; i++; continue; }
        if (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') {
            i++; continue;
        }
        if (s[i] == ']') return rank;
        return 0;
    }
    return 0;
}

static zan_ast_node_t *parse_type_ref(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;

    int nn_top = p->type_no_nullable;
    p->type_no_nullable = 0;

    if (++p->type_depth > ZAN_PARSER_MAX_TYPE_DEPTH) {
        zan_diag_emit_code(p->diag, DIAG_ERROR, loc, 36,
                      "type nesting too deep (max %d)",
                      ZAN_PARSER_MAX_TYPE_DEPTH);
        p->type_depth--;
        return parser_error_node(p);
    }

    if (parser_check(p, TK_LPAREN)) {
        parser_advance(p);
        zan_ast_node_t *tn = zan_ast_new(p->arena, AST_TUPLE_TYPE, loc);
        zan_ast_list_init(&tn->tuple_type.elems);
        while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
            zan_ast_node_t *et = parse_type_ref(p);
            zan_ast_list_push(&tn->tuple_type.elems, et, p->arena);
            if (p->current.kind == TK_IDENT &&
                !(et->kind == AST_TYPE_REF && et->type_ref.is_array)) {
                parser_advance(p);
            }
            if (!parser_match(p, TK_COMMA)) break;
        }
        parser_expect(p, TK_RPAREN);
        p->type_depth--;
        return tn;
    }

    zan_istr_t name = {0};
    bool is_builtin = true;
    switch (p->current.kind) {
    case TK_INT:    name.str = "int";    name.len = 3; break;
    case TK_LONG:   name.str = "long";   name.len = 4; break;
    case TK_SHORT:  name.str = "short";  name.len = 5; break;
    case TK_BYTE:   name.str = "byte";   name.len = 4; break;
    case TK_UINT:   name.str = "uint";   name.len = 4; break;
    case TK_ULONG:  name.str = "ulong";  name.len = 5; break;
    case TK_USHORT: name.str = "ushort"; name.len = 6; break;
    case TK_SBYTE:  name.str = "sbyte";  name.len = 5; break;
    case TK_FLOAT:  name.str = "float";  name.len = 5; break;
    case TK_DOUBLE: name.str = "double"; name.len = 6; break;
    case TK_DECIMAL: name.str = "decimal"; name.len = 7; break;
    case TK_BOOL:   name.str = "bool";   name.len = 4; break;
    case TK_CHAR:   name.str = "char";   name.len = 4; break;
    case TK_STRING: name.str = "string"; name.len = 6; break;
    case TK_VOID:   name.str = "void";   name.len = 4; break;
    case TK_OBJECT: name.str = "object"; name.len = 6; break;
    case TK_NINT:   name.str = "nint";   name.len = 4; break;
    case TK_VAR:    name.str = "var";    name.len = 3; break;
    default: is_builtin = false; break;
    }

    if (is_builtin) {
        parser_advance(p);
    } else if (parser_check(p, TK_IDENT)) {
        parser_advance(p);
        name = p->previous.str_val;
        if (parser_check(p, TK_DOT) &&
            zan_lexer_peek(p->lex).kind == TK_IDENT) {
            char qbuf[512];
            size_t qn = 0;
            for (uint32_t qi = 0; qi < name.len && qn < sizeof qbuf; qi++)
                qbuf[qn++] = name.str[qi];
            while (parser_check(p, TK_DOT) &&
                   zan_lexer_peek(p->lex).kind == TK_IDENT) {
                parser_advance(p);
                parser_advance(p);
                if (qn < sizeof qbuf - 1) qbuf[qn++] = '.';
                for (uint32_t qi = 0; qi < p->previous.str_val.len &&
                     qn < sizeof qbuf; qi++)
                    qbuf[qn++] = p->previous.str_val.str[qi];
            }
            name.str = zan_arena_strdup(p->arena, qbuf, qn);
            name.len = (uint32_t)qn;
        }
    } else {
        zan_diag_emit_code(p->diag, DIAG_ERROR, loc, 37, "expected type");
        p->type_depth--;
        return parser_error_node(p);
    }

    zan_ast_node_t *type_node = zan_ast_new(p->arena, AST_TYPE_REF, loc);
    type_node->type_ref.name = name;
    type_node->type_ref.is_nullable = false;
    type_node->type_ref.is_array = false;
    zan_ast_list_init(&type_node->type_ref.type_args);

    if (parser_check(p, TK_LESS)) {
        parser_advance(p);
        while (!parser_check(p, TK_GREATER) && !parser_check(p, TK_EOF)) {
            zan_ast_node_t *arg = parse_type_ref(p);
            zan_ast_list_push(&type_node->type_ref.type_args, arg, p->arena);
            if (!parser_match(p, TK_COMMA)) break;
        }
        parser_expect_gt(p);
    }

    int ranks[16];
    int nranks = 0;
    bool seen_array = false;
    bool array_nullable = false;
    for (;;) {
        if (nn_top && parser_check(p, TK_QUESTION)) {
            break;
        }
        if (parser_match(p, TK_QUESTION)) {
            if (seen_array) {
                array_nullable = true;
            } else {
                type_node->type_ref.is_nullable = true;
            }
        } else if (parser_check(p, TK_LBRACKET)) {
            int rank = array_suffix_rank(p);
            if (rank <= 0) break;
            parser_advance(p);
            for (int c = 1; c < rank; c++) parser_advance(p);
            parser_advance(p);
            if (rank <= 16 && nranks < 16) {
                ranks[nranks++] = rank;
            } else {
                zan_diag_emit_code(p->diag, DIAG_ERROR, loc, 38,
                              "array rank specifier is too deep (max 16)");
            }
            seen_array = true;
        } else {
            break;
        }
    }
    if (nranks > 0) {
        zan_ast_node_t *cur = type_node;
        for (int i = nranks - 1; i >= 0; i--) {
            zan_ast_node_t *w = zan_ast_new(p->arena, AST_TYPE_REF, loc);
            w->type_ref.name = cur->type_ref.name;
            w->type_ref.is_array = true;
            w->type_ref.array_rank = ranks[i];
            w->type_ref.array_element = cur;
            zan_ast_list_init(&w->type_ref.type_args);
            cur = w;
        }
        cur->type_ref.is_nullable = array_nullable;
        p->type_depth--;
        return cur;
    }

    p->type_depth--;
    return type_node;
}

static bool is_init_accessor_kw(zan_parser_t *p) {
    return p->current.kind == TK_IDENT && p->current.str_val.len == 4 &&
           memcmp(p->current.str_val.str, "init", 4) == 0;
}

static bool is_checked_use(zan_parser_t *p) {
    if (p->current.kind != TK_IDENT) return false;
    const char *s = p->current.str_val.str;
    int n = p->current.str_val.len;
    bool kw = (n == 7 && memcmp(s, "checked", 7) == 0) ||
              (n == 9 && memcmp(s, "unchecked", 9) == 0);
    if (!kw) return false;
    zan_token_kind_t nxt = zan_lexer_peek(p->lex).kind;
    return nxt == TK_LPAREN || nxt == TK_LBRACE;
}

static uint32_t parse_modifiers(zan_parser_t *p) {
    uint32_t mods = 0;
    for (;;) {
        switch (p->current.kind) {
        case TK_PUBLIC:    parser_advance(p); mods |= MOD_PUBLIC;    break;
        case TK_PRIVATE:   parser_advance(p); mods |= MOD_PRIVATE;   break;
        case TK_PROTECTED: parser_advance(p); mods |= MOD_PROTECTED; break;
        case TK_INTERNAL:  parser_advance(p); mods |= MOD_INTERNAL;  break;
        case TK_STATIC:    parser_advance(p); mods |= MOD_STATIC;    break;
        case TK_VIRTUAL:   parser_advance(p); mods |= MOD_VIRTUAL;   break;
        case TK_OVERRIDE:  parser_advance(p); mods |= MOD_OVERRIDE;  break;
        case TK_ABSTRACT:  parser_advance(p); mods |= MOD_ABSTRACT;  break;
        case TK_SEALED:    parser_advance(p); mods |= MOD_SEALED;    break;
        case TK_READONLY:  parser_advance(p); mods |= MOD_READONLY;  break;
        case TK_CONST:     parser_advance(p);
                           mods |= MOD_STATIC | MOD_READONLY; break;
        case TK_EXTERN:    parser_advance(p); mods |= MOD_EXTERN;    break;
        case TK_ASYNC:     parser_advance(p); mods |= MOD_ASYNC;     break;
        case TK_UNSAFE:    parser_advance(p); mods |= MOD_UNSAFE;    break;
        case TK_WEAK:      parser_advance(p); mods |= MOD_WEAK;      break;
        case TK_REF:       parser_advance(p); mods |= MOD_REF;       break;
        default: return mods;
        }
    }
}

static bool paren_is_lambda(zan_parser_t *p) {
    zan_lexer_mark_t saved_lex; zan_lexer_mark(p->lex, &saved_lex, p->arena);
    zan_token_t saved_cur = p->current;
    zan_token_t saved_prev = p->previous;
    int depth = 0;
    bool result = false;
    while (p->current.kind != TK_EOF) {
        if (p->current.kind == TK_LPAREN) {
            depth++;
        } else if (p->current.kind == TK_RPAREN) {
            depth--;
            if (depth == 0) {
                result = (zan_lexer_peek(p->lex).kind == TK_ARROW);
                break;
            }
        }
        parser_advance(p);
    }
    zan_lexer_reset(p->lex, &saved_lex);
    p->current = saved_cur;
    p->previous = saved_prev;
    return result;
}

/* (Name)operand 显式类型转换表达式语法消歧 */
static bool paren_is_named_cast(zan_parser_t *p) {
    if (zan_lexer_peek(p->lex).kind != TK_IDENT) return false;
    zan_lexer_mark_t saved_lex; zan_lexer_mark(p->lex, &saved_lex, p->arena);
    zan_token_t saved_cur = p->current;
    zan_token_t saved_prev = p->previous;
    bool result = false;
    parser_advance(p);
    parser_advance(p);
    while (p->current.kind == TK_DOT) {
        if (zan_lexer_peek(p->lex).kind != TK_IDENT) break;
        parser_advance(p);
        parser_advance(p);
    }
    if (p->current.kind == TK_LESS) {
        int depth = 1;
        parser_advance(p);
        while (depth > 0 && p->current.kind != TK_EOF && p->current.kind != TK_SEMICOLON) {
            if (p->current.kind == TK_LESS) {
                depth++;
            } else if (p->current.kind == TK_GREATER) {
                depth--;
            } else if (p->current.kind == TK_GREATER_GREATER) {
                depth -= 2;
            } else if (p->current.kind == TK_GREATER_GREATER_GREATER) {
                depth -= 3;
            }
            if (depth <= 0) {
                parser_advance(p);
                break;
            }
            parser_advance(p);
        }
        if (depth > 0) {
            zan_lexer_reset(p->lex, &saved_lex);
            p->current = saved_cur;
            p->previous = saved_prev;
            return false;
        }
    }
    if (p->current.kind == TK_QUESTION) {
        parser_advance(p);
    }
    while (p->current.kind == TK_LBRACKET && zan_lexer_peek(p->lex).kind == TK_RBRACKET) {
        parser_advance(p);
        parser_advance(p);
    }
    if (p->current.kind == TK_RPAREN) {
        switch (zan_lexer_peek(p->lex).kind) {
        case TK_IDENT: case TK_INT_LIT: case TK_FLOAT_LIT: case TK_DECIMAL_LIT:
        case TK_STRING_LIT: case TK_CHAR_LIT:
        case TK_THIS: case TK_NEW: case TK_LPAREN:
            result = true;
            break;
        default:
            break;
        }
    }
    zan_lexer_reset(p->lex, &saved_lex);
    p->current = saved_cur;
    p->previous = saved_prev;
    return result;
}

static zan_ast_node_t *parse_lambda_param(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    if (parser_check(p, TK_IDENT)) {
        zan_token_kind_t after = zan_lexer_peek(p->lex).kind;
        if (after == TK_COMMA || after == TK_RPAREN) {
            parser_advance(p);
            zan_ast_node_t *pn = zan_ast_new(p->arena, AST_PARAM, loc);
            pn->param.name = p->previous.str_val;
            pn->param.type = NULL;
            pn->param.default_val = NULL;
            return pn;
        }
    }
    zan_ast_node_t *type = parse_type_ref(p);
    zan_istr_t name = {0};
    if (parser_check(p, TK_IDENT)) {
        parser_advance(p);
        name = p->previous.str_val;
    }
    zan_ast_node_t *pn = zan_ast_new(p->arena, AST_PARAM, loc);
    pn->param.name = name;
    pn->param.type = type;
    pn->param.default_val = NULL;
    return pn;
}

static zan_ast_node_t *parse_lambda_paren(zan_parser_t *p, zan_loc_t loc) {
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_LAMBDA, loc);
    zan_ast_list_init(&n->lambda.params);
    parser_expect(p, TK_LPAREN);
    while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
        zan_ast_node_t *param = parse_lambda_param(p);
        zan_ast_list_push(&n->lambda.params, param, p->arena);
        if (!parser_match(p, TK_COMMA)) break;
    }
    parser_expect(p, TK_RPAREN);
    parser_expect(p, TK_ARROW);
    if (parser_check(p, TK_LBRACE)) {
        n->lambda.body = parse_block(p);
    } else {
        n->lambda.body = parse_expression(p);
    }
    return n;
}
