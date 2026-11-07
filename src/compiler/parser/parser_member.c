
/* `where T : C1, C2 */
static void parse_where_clauses(zan_parser_t *p, zan_ast_list_t *out) {
    while (parser_check(p, TK_WHERE)) {
        parser_advance(p);
        zan_loc_t loc = p->current.loc;
        zan_istr_t pname = {NULL, 0};
        if (parser_check(p, TK_IDENT)) {
            parser_advance(p);
            pname = p->previous.str_val;
        } else if (!parser_take_reserved_word(p, &pname)) {
            parser_diag_expected_name(p, loc,
                                      "expected type parameter name after 'where'");
        }
        parser_expect(p, TK_COLON);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_WHERE_CLAUSE, loc);
        n->where_clause.param_name = pname;
        zan_ast_list_init(&n->where_clause.constraints);
        n->where_clause.has_new = false;
        n->where_clause.has_class = false;
        n->where_clause.has_struct = false;
        do {
            if (parser_check(p, TK_CLASS)) {
                parser_advance(p);
                n->where_clause.has_class = true;
                continue;
            }
            if (parser_check(p, TK_STRUCT)) {
                parser_advance(p);
                n->where_clause.has_struct = true;
                continue;
            }
            if (parser_check(p, TK_NEW)) {
                parser_advance(p);
                parser_expect(p, TK_LPAREN);
                parser_expect(p, TK_RPAREN);
                n->where_clause.has_new = true;
                continue;
            }
            zan_ast_node_t *cons = parse_type_ref(p);
            zan_ast_list_push(&n->where_clause.constraints, cons, p->arena);
        } while (parser_match(p, TK_COMMA));
        zan_ast_list_push(out, n, p->arena);
    }
}

static zan_ast_node_t *parse_parameter(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;

    int is_this = 0;
    if (parser_check(p, TK_THIS)) {
        parser_advance(p);
        is_this = 1;
    }

    /* Contextual `params` modifier: `params T[] rest` */
    int is_params = 0;
    if (parser_check(p, TK_IDENT) && p->current.str_val.len == 6 &&
        memcmp(p->current.str_val.str, "params", 6) == 0) {
        parser_advance(p);
        is_params = 1;
    }

    int by_ref = 0;
    if (parser_check(p, TK_REF)) { parser_advance(p); by_ref = 1; }
    else if (parser_check(p, TK_OUT)) { parser_advance(p); by_ref = 2; }

    zan_ast_node_t *type = parse_type_ref(p);

    zan_istr_t name = {0};
    if (parser_check(p, TK_IDENT)) {
        parser_advance(p);
        name = p->previous.str_val;
    } else if (parser_take_reserved_word(p, &name)) {
        /* 保留字当参数名（`F(int out)`）：点名道姓，并把它吃掉——
         * 留着的话外层还会补一条 "expected ')'"，把真正的错盖住。 */
    }

    zan_ast_node_t *default_val = NULL;
    if (parser_match(p, TK_EQ)) {
        default_val = parse_expression(p);
    }

    zan_ast_node_t *param = zan_ast_new(p->arena, AST_PARAM, loc);
    param->param.name = name;
    param->param.type = type;
    param->param.default_val = default_val;
    param->param.is_params = is_params;
    param->param.by_ref = by_ref;
    param->param.is_this = is_this;
    return param;
}

static zan_ast_list_t parse_param_list(zan_parser_t *p) {
    zan_ast_list_t params;
    zan_ast_list_init(&params);

    parser_expect(p, TK_LPAREN);
    while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
        zan_ast_node_t *param = parse_parameter(p);
        zan_ast_list_push(&params, param, p->arena);
        if (!parser_match(p, TK_COMMA)) break;
    }
    parser_expect(p, TK_RPAREN);

    return params;
}


static bool stmt_contains_yield(zan_ast_node_t *s);

static bool list_contains_yield(zan_ast_list_t *l) {
    for (int i = 0; i < l->count; i++)
        if (stmt_contains_yield(l->items[i])) return true;
    return false;
}

static bool stmt_contains_yield(zan_ast_node_t *s) {
    if (!s) return false;
    switch (s->kind) {
    case AST_YIELD_STMT:   return true;
    case AST_BLOCK:        return list_contains_yield(&s->block.stmts);
    case AST_IF_STMT:      return stmt_contains_yield(s->if_stmt.then_body) ||
                                  stmt_contains_yield(s->if_stmt.else_body);
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT: return stmt_contains_yield(s->while_stmt.body);
    case AST_FOR_STMT:     return stmt_contains_yield(s->for_stmt.body);
    case AST_FOREACH_STMT: return stmt_contains_yield(s->foreach_stmt.body);
    case AST_TRY_STMT:
        if (stmt_contains_yield(s->try_stmt.try_body)) return true;
        for (int i = 0; i < s->try_stmt.catches.count; i++)
            if (stmt_contains_yield(s->try_stmt.catches.items[i]->catch_clause.body))
                return true;
        return stmt_contains_yield(s->try_stmt.finally_body);
    case AST_SWITCH_STMT:
        for (int i = 0; i < s->switch_stmt.cases.count; i++)
            if (stmt_contains_yield(s->switch_stmt.cases.items[i]->switch_case.body))
                return true;
        return false;
    default: return false;
    }
}

static zan_ast_node_t *make_yield_ident(zan_parser_t *p, zan_loc_t loc) {
    zan_ast_node_t *id = zan_ast_new(p->arena, AST_IDENTIFIER, loc);
    id->ident.name = (zan_istr_t){"__yield", 7};
    return id;
}

static void rewrite_yield_stmt(zan_parser_t *p, zan_ast_node_t **slot) {
    zan_ast_node_t *s = *slot;
    if (!s) return;
    switch (s->kind) {
    case AST_YIELD_STMT: {
        if (s->yield_stmt.value) {
            /* __yield.Add(value); */
            zan_ast_node_t *mem = zan_ast_new(p->arena, AST_MEMBER_ACCESS, s->loc);
            mem->member.object = make_yield_ident(p, s->loc);
            mem->member.name = (zan_istr_t){"Add", 3};
            mem->member.null_cond = 0;
            zan_ast_node_t *call = zan_ast_new(p->arena, AST_CALL, s->loc);
            call->call.callee = mem;
            zan_ast_list_init(&call->call.args);
            zan_ast_list_init(&call->call.type_args);
            zan_ast_list_push(&call->call.args, s->yield_stmt.value, p->arena);
            zan_ast_node_t *es = zan_ast_new(p->arena, AST_EXPR_STMT, s->loc);
            es->expr_stmt.expr = call;
            *slot = es;
        } else {
            zan_ast_node_t *ret = zan_ast_new(p->arena, AST_RETURN_STMT, s->loc);
            ret->ret.value = make_yield_ident(p, s->loc);
            *slot = ret;
        }
        return;
    }
    case AST_BLOCK:
        for (int i = 0; i < s->block.stmts.count; i++)
            rewrite_yield_stmt(p, &s->block.stmts.items[i]);
        return;
    case AST_IF_STMT:
        rewrite_yield_stmt(p, &s->if_stmt.then_body);
        rewrite_yield_stmt(p, &s->if_stmt.else_body);
        return;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        rewrite_yield_stmt(p, &s->while_stmt.body);
        return;
    case AST_FOR_STMT:
        rewrite_yield_stmt(p, &s->for_stmt.body);
        return;
    case AST_FOREACH_STMT:
        rewrite_yield_stmt(p, &s->foreach_stmt.body);
        return;
    case AST_TRY_STMT:
        rewrite_yield_stmt(p, &s->try_stmt.try_body);
        for (int i = 0; i < s->try_stmt.catches.count; i++)
            rewrite_yield_stmt(p, &s->try_stmt.catches.items[i]->catch_clause.body);
        rewrite_yield_stmt(p, &s->try_stmt.finally_body);
        return;
    case AST_SWITCH_STMT:
        for (int i = 0; i < s->switch_stmt.cases.count; i++)
            rewrite_yield_stmt(p, &s->switch_stmt.cases.items[i]->switch_case.body);
        return;
    default: return;
    }
}

static void desugar_yield_method(zan_parser_t *p, zan_ast_node_t *m) {
    zan_ast_node_t *body = m->method_decl.body;
    if (!body || !stmt_contains_yield(body)) return;
    zan_ast_node_t *rt = m->method_decl.return_type;
    if (!rt || rt->kind != AST_TYPE_REF || rt->type_ref.type_args.count != 1) {
        zan_diag_emit_code(p->diag, DIAG_ERROR, m->loc, 52,
                      "iterator method '%.*s' must return IEnumerable<T> or List<T>",
                      (int)m->method_decl.name.len, m->method_decl.name.str);
        return;
    }
    /* A381 lazy iterators：IEnumerable<T> 返回 + yield 走 parse 后的
     * 迭代器类合成（iter_desugar_unit），真惰性；这里只保留 List<T>
     * 返回的急切改写（现网语义不变）。 */
    if (rt->type_ref.name.len == 11 &&
        memcmp(rt->type_ref.name.str, "IEnumerable", 11) == 0) {
        return;
    }
    rt->type_ref.name = (zan_istr_t){"List", 4};

    zan_ast_node_t *list_type = zan_ast_new(p->arena, AST_TYPE_REF, m->loc);
    list_type->type_ref.name = (zan_istr_t){"List", 4};
    list_type->type_ref.type_args = rt->type_ref.type_args;
    list_type->type_ref.is_nullable = false;
    list_type->type_ref.is_array = false;

    zan_ast_node_t *nw = zan_ast_new(p->arena, AST_NEW_EXPR, m->loc);
    nw->new_expr.type = list_type;
    zan_ast_list_init(&nw->new_expr.args);
    nw->new_expr.is_array = false;

    zan_ast_node_t *decl = zan_ast_new(p->arena, AST_VAR_DECL, m->loc);
    decl->var_decl.name = (zan_istr_t){"__yield", 7};
    decl->var_decl.type = NULL;
    decl->var_decl.initializer = nw;
    decl->var_decl.is_const = false;
    decl->var_decl.is_let = false;

    rewrite_yield_stmt(p, &m->method_decl.body);
    body = m->method_decl.body;

    zan_ast_node_t *ret = zan_ast_new(p->arena, AST_RETURN_STMT, m->loc);
    ret->ret.value = make_yield_ident(p, m->loc);

    zan_ast_list_t old = body->block.stmts;
    zan_ast_list_init(&body->block.stmts);
    zan_ast_list_push(&body->block.stmts, decl, p->arena);
    for (int i = 0; i < old.count; i++)
        zan_ast_list_push(&body->block.stmts, old.items[i], p->arena);
    zan_ast_list_push(&body->block.stmts, ret, p->arena);
}

static void desugar_async_task_method(zan_parser_t *p, zan_ast_node_t *m) {
    if (!m || m->kind != AST_METHOD_DECL) return;
    zan_ast_node_t *rt = m->method_decl.return_type;
    if (!rt || rt->kind != AST_TYPE_REF) return;
    bool is_task = (rt->type_ref.name.len == 4 && memcmp(rt->type_ref.name.str, "Task", 4) == 0);
    bool is_valuetask = (rt->type_ref.name.len == 9 && memcmp(rt->type_ref.name.str, "ValueTask", 9) == 0);
    if (!is_task && !is_valuetask) return;

    if ((m->method_decl.modifiers & MOD_ASYNC) != 0 || m->method_decl.body == NULL) {
        if ((m->method_decl.modifiers & MOD_ASYNC) == 0) {
            m->method_decl.modifiers |= MOD_ASYNC;
        }
        m->method_decl.is_task_return = true;
        if (rt->type_ref.type_args.count == 1) {
            m->method_decl.return_type = rt->type_ref.type_args.items[0];
        } else if (rt->type_ref.type_args.count == 0) {
            zan_ast_node_t *v = zan_ast_new(p->arena, AST_TYPE_REF, rt->loc);
            v->type_ref.name = (zan_istr_t){"void", 4};
            v->type_ref.is_nullable = false;
            v->type_ref.is_array = false;
            m->method_decl.return_type = v;
        }
    }
}

static void parse_attr_usages(zan_parser_t *p, zan_ast_list_t *out,
                              zan_istr_t *out_lib, zan_istr_t *out_entry,
                              bool *out_variadic) {
    while (parser_check(p, TK_LBRACKET)) {
        parser_advance(p);
        for (;;) {
            zan_loc_t aloc = p->current.loc;
            zan_istr_t aname = {NULL, 0};
            if (parser_check(p, TK_IDENT)) {
                aname = p->current.str_val;
                parser_advance(p);
                while (parser_check(p, TK_DOT)) {
                    parser_advance(p);
                    if (parser_check(p, TK_IDENT)) { aname = p->current.str_val; parser_advance(p); }
                    else break;
                }
            }
            zan_ast_node_t *attr = zan_ast_new(p->arena, AST_ATTRIBUTE, aloc);
            zan_ast_node_t *nameNode = zan_ast_new(p->arena, AST_IDENTIFIER, aloc);
            nameNode->ident.name = aname;
            attr->attribute.name = nameNode;
            zan_ast_list_init(&attr->attribute.args);
            bool is_dll = (aname.str && aname.len == 9 &&
                           memcmp(aname.str, "DllImport", 9) == 0);
            if (parser_match(p, TK_LPAREN)) {
                while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
                    if (parser_check(p, TK_IDENT) && zan_lexer_peek(p->lex).kind == TK_EQ) {
                        zan_loc_t nloc = p->current.loc;
                        zan_istr_t argname = p->current.str_val;
                        parser_advance(p);
                        parser_advance(p);
                        zan_ast_node_t *val = parse_expression(p);
                        zan_ast_node_t *asn = zan_ast_new(p->arena, AST_ASSIGNMENT, nloc);
                        zan_ast_node_t *lhs = zan_ast_new(p->arena, AST_IDENTIFIER, nloc);
                        lhs->ident.name = argname;
                        asn->binary.op = TK_EQ;
                        asn->binary.left = lhs;
                        asn->binary.right = val;
                        zan_ast_list_push(&attr->attribute.args, asn, p->arena);
                        if (is_dll && out_entry && argname.str && argname.len == 10 &&
                            memcmp(argname.str, "EntryPoint", 10) == 0 &&
                            val->kind == AST_STRING_LITERAL) {
                            *out_entry = val->str_val;
                        }
                        if (is_dll && out_variadic && argname.str &&
                            argname.len == 8 &&
                            memcmp(argname.str, "Variadic", 8) == 0 &&
                            val->kind == AST_BOOL_LITERAL && val->bool_val) {
                            *out_variadic = true;
                        }
                    } else {
                        zan_ast_node_t *val = parse_expression(p);
                        zan_ast_list_push(&attr->attribute.args, val, p->arena);
                        if (is_dll && out_lib && !out_lib->str &&
                            val->kind == AST_STRING_LITERAL) {
                            *out_lib = val->str_val;
                        }
                    }
                    if (!parser_match(p, TK_COMMA)) break;
                }
                parser_expect(p, TK_RPAREN);
            }
            if (out) zan_ast_list_push(out, attr, p->arena);
            if (!parser_match(p, TK_COMMA)) break;
        }
        parser_expect(p, TK_RBRACKET);
    }
}

static zan_ast_node_t *parse_member_decl_inner(zan_parser_t *p,
                                               zan_istr_t dll_import_lib,
                                               zan_istr_t dll_entry_point,
                                               bool dll_variadic);

static zan_ast_node_t *synth_property_accessor(zan_parser_t *p, zan_istr_t name,
                                               zan_ast_node_t *ret_type,
                                               zan_ast_node_t *value_type,
                                               zan_ast_node_t *body,
                                               uint32_t mods, zan_loc_t loc) {
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_METHOD_DECL, loc);
    n->method_decl.name = name;
    n->method_decl.return_type = ret_type;
    zan_ast_list_init(&n->method_decl.params);
    zan_ast_list_init(&n->method_decl.type_params);
    if (value_type) {
        zan_ast_node_t *param = zan_ast_new(p->arena, AST_PARAM, loc);
        zan_istr_t vn = {(char *)"value", 5};
        param->param.name = vn;
        param->param.type = value_type;
        param->param.is_this = false;
        zan_ast_list_push(&n->method_decl.params, param, p->arena);
    }
    n->method_decl.body = body;
    n->method_decl.modifiers = mods;
    return n;
}

static zan_ast_node_t *parse_member_decl(zan_parser_t *p) {
    zan_ast_list_t attrs;
    zan_ast_list_init(&attrs);
    zan_istr_t dll_import_lib = {NULL, 0};
    zan_istr_t dll_entry_point = {NULL, 0};
    bool dll_variadic = false;
    parse_attr_usages(p, &attrs, &dll_import_lib, &dll_entry_point, &dll_variadic);
    zan_ast_node_t *n = parse_member_decl_inner(p, dll_import_lib, dll_entry_point,
                                                dll_variadic);
    if (n && attrs.count > 0) {
        zan_ast_ensure_decl_meta(n, p->arena)->attributes = attrs;
        for (int _ai = 0; _ai < attrs.count; _ai++) {
            zan_ast_node_t *a = attrs.items[_ai];
            if (a && a->kind == AST_ATTRIBUTE && a->attribute.name) {
                if (is_intrinsic_attr_name(a->attribute.name->ident.name)) {
                    if (n->kind == AST_METHOD_DECL || n->kind == AST_CONSTRUCTOR_DECL ||
                        n->kind == AST_DESTRUCTOR_DECL) {
                        n->method_decl.modifiers |= MOD_INTRINSIC;
                    } else if (n->kind == AST_FIELD_DECL || n->kind == AST_PROPERTY_DECL) {
                        n->field_decl.modifiers |= MOD_INTRINSIC;
                    } else if (n->kind == AST_CLASS_DECL || n->kind == AST_STRUCT_DECL ||
                               n->kind == AST_INTERFACE_DECL || n->kind == AST_ENUM_DECL) {
                        n->type_decl.modifiers |= MOD_INTRINSIC;
                    }
                    break;
                }
            }
        }
    }
    return n;
}

static zan_ast_node_t *parse_member_decl_inner(zan_parser_t *p,
                                               zan_istr_t dll_import_lib,
                                               zan_istr_t dll_entry_point,
                                               bool dll_variadic) {
    uint32_t mods = parse_modifiers(p);
    if (dll_import_lib.str) mods |= MOD_EXTERN;
    if (parser_check(p, TK_IDENT) && p->current.str_val.len == 5 &&
        memcmp(p->current.str_val.str, "event", 5) == 0 &&
        zan_lexer_peek(p->lex).kind == TK_IDENT) {
        parser_advance(p);
        mods |= MOD_EVENT;
    }

    if (parser_check(p, TK_IDENT) && p->current.str_val.len == 7 &&
        memcmp(p->current.str_val.str, "partial", 7) == 0) {
        zan_token_kind_t nk = zan_lexer_peek(p->lex).kind;
        if (nk == TK_CLASS || nk == TK_STRUCT || nk == TK_INTERFACE) {
            parser_advance(p);
            mods |= MOD_PARTIAL;
        }
    }

    if (parser_check(p, TK_CLASS) || parser_check(p, TK_STRUCT) ||
        parser_check(p, TK_INTERFACE) || parser_check(p, TK_ENUM)) {
        return parse_type_decl(p, mods);
    }

    if (parser_check(p, TK_DELEGATE)) {
        return parse_delegate_decl(p, mods);
    }

    zan_loc_t loc = p->current.loc;

    /* destructor: ~ClassName() { } */
    if (parser_check(p, TK_TILDE)) {
        parser_advance(p);
        parser_expect(p, TK_IDENT);
        zan_istr_t name = p->previous.str_val;
        parser_expect(p, TK_LPAREN);
        parser_expect(p, TK_RPAREN);
        zan_ast_node_t *body = parse_block(p);

        zan_ast_node_t *n = zan_ast_new(p->arena, AST_DESTRUCTOR_DECL, loc);
        n->method_decl.name = name;
        n->method_decl.body = body;
        n->method_decl.modifiers = mods;
        n->method_decl.return_type = NULL;
        zan_ast_list_init(&n->method_decl.params);
        zan_ast_list_init(&n->method_decl.type_params);
        return n;
    }

    if (parser_check(p, TK_IDENT) &&
        zan_lexer_peek(p->lex).kind == TK_OPERATOR) {
        zan_istr_t kw = p->current.str_val;
        bool is_explicit;
        if (kw.len == 8 && memcmp(kw.str, "implicit", 8) == 0)
            is_explicit = false;
        else if (kw.len == 8 && memcmp(kw.str, "explicit", 8) == 0)
            is_explicit = true;
        else
            goto ordinary_member;
        parser_advance(p);
        parser_advance(p);
        zan_ast_node_t *conv_ret = parse_type_ref(p);
        zan_ast_list_t conv_params = parse_param_list(p);
        zan_ast_node_t *conv_body = NULL;
        if (parser_check(p, TK_LBRACE)) {
            conv_body = parse_block(p);
        } else {
            parser_expect(p, TK_SEMICOLON);
        }
        const char *conv_name = is_explicit ? "op_explicit" : "op_implicit";
        zan_ast_node_t *cn = zan_ast_new(p->arena, AST_METHOD_DECL, loc);
        cn->method_decl.name =
            (zan_istr_t){ (char *)conv_name, 11 };
        cn->method_decl.return_type = conv_ret;
        cn->method_decl.params = conv_params;
        zan_ast_list_init(&cn->method_decl.type_params);
        cn->method_decl.body = conv_body;
        cn->method_decl.modifiers = mods | MOD_STATIC;
        return cn;
    }
ordinary_member:
    ;
    zan_ast_node_t *type = parse_type_ref(p);

    /* constructor: ClassName(params) [: base(args)] { } */
    if (parser_check(p, TK_LPAREN)) {
        zan_istr_t name = type->type_ref.name;
        zan_ast_list_t params = parse_param_list(p);

        zan_ast_list_t base_args;
        zan_ast_list_init(&base_args);
        bool has_base_init = false;
        bool has_this_init = false;
        if (parser_match(p, TK_COLON)) {
            if (parser_check(p, TK_BASE) || parser_check(p, TK_THIS)) {
                bool is_base = parser_check(p, TK_BASE);
                parser_advance(p);
                if (parser_match(p, TK_LPAREN)) {
                    while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
                        zan_ast_node_t *arg = parse_expression(p);
                        if (arg)
                            zan_ast_list_push(&base_args, arg, p->arena);
                        if (!parser_match(p, TK_COMMA)) break;
                    }
                    parser_expect(p, TK_RPAREN);
                }
                has_base_init = is_base;
                has_this_init = !is_base;
            }
        }

        zan_ast_node_t *body = NULL;
        if (parser_check(p, TK_LBRACE)) {
            body = parse_block(p);
        } else {
            parser_expect(p, TK_SEMICOLON);
        }

        zan_ast_node_t *n = zan_ast_new(p->arena, AST_CONSTRUCTOR_DECL, loc);
        n->method_decl.name = name;
        n->method_decl.params = params;
        n->method_decl.body = body;
        n->method_decl.modifiers = mods;
        n->method_decl.return_type = NULL;
        if (base_args.count > 0) {
            zan_ast_ensure_method_ext(n, p->arena)->base_args = base_args;
        }
        n->method_decl.has_base_init = has_base_init;
        n->method_decl.has_this_init = has_this_init;
        zan_ast_list_init(&n->method_decl.type_params);
        return n;
    }

    if (parser_check(p, TK_OPERATOR)) {
        parser_advance(p);
        char op_name[32];
        switch (p->current.kind) {
        case TK_PLUS:    snprintf(op_name, sizeof(op_name), "op_add"); break;
        case TK_MINUS:   snprintf(op_name, sizeof(op_name), "op_sub"); break;
        case TK_STAR:    snprintf(op_name, sizeof(op_name), "op_mul"); break;
        case TK_SLASH:   snprintf(op_name, sizeof(op_name), "op_div"); break;
        case TK_PERCENT: snprintf(op_name, sizeof(op_name), "op_mod"); break;
        case TK_EQ_EQ:   snprintf(op_name, sizeof(op_name), "op_eq"); break;
        case TK_BANG_EQ: snprintf(op_name, sizeof(op_name), "op_neq"); break;
        case TK_LESS:    snprintf(op_name, sizeof(op_name), "op_lt"); break;
        case TK_GREATER: snprintf(op_name, sizeof(op_name), "op_gt"); break;
        case TK_LESS_EQ: snprintf(op_name, sizeof(op_name), "op_le"); break;
        case TK_GREATER_EQ: snprintf(op_name, sizeof(op_name), "op_ge"); break;
        case TK_PLUS_PLUS:  snprintf(op_name, sizeof(op_name), "op_inc"); break;
        case TK_MINUS_MINUS: snprintf(op_name, sizeof(op_name), "op_dec"); break;
        default:         snprintf(op_name, sizeof(op_name), "op_unknown"); break;
        }
        parser_advance(p);
        zan_ast_list_t params = parse_param_list(p);
        zan_ast_node_t *body = NULL;
        if (parser_check(p, TK_LBRACE)) {
            body = parse_block(p);
        } else {
            parser_expect(p, TK_SEMICOLON);
        }
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_METHOD_DECL, loc);
        size_t op_len = strlen(op_name);
        char *op_str = zan_arena_strdup(p->arena, op_name, op_len);
        zan_istr_t iname = { op_str, (int)op_len };
        n->method_decl.name = iname;
        n->method_decl.return_type = type;
        n->method_decl.params = params;
        zan_ast_list_init(&n->method_decl.type_params);
        n->method_decl.body = body;
        n->method_decl.modifiers = mods | MOD_STATIC;
        return n;
    }

    if (parser_check(p, TK_THIS)) {
        parser_advance(p);
        parser_expect(p, TK_LBRACKET);
        zan_ast_list_t idx_params;
        zan_ast_list_init(&idx_params);
        while (!parser_check(p, TK_RBRACKET) && !parser_check(p, TK_EOF)) {
            zan_ast_node_t *param = parse_parameter(p);
            zan_ast_list_push(&idx_params, param, p->arena);
            if (!parser_match(p, TK_COMMA)) break;
        }
        parser_expect(p, TK_RBRACKET);

        zan_ast_node_t *getter_body = NULL;
        zan_ast_node_t *setter_body = NULL;
        bool has_getter = false;
        bool has_setter = false;
        bool has_init = false;

        if (parser_check(p, TK_ARROW)) {
            parser_advance(p);
            zan_ast_node_t *expr = parse_expression(p);
            parser_expect(p, TK_SEMICOLON);
            getter_body = zan_ast_new(p->arena, AST_BLOCK, expr->loc);
            zan_ast_list_init(&getter_body->block.stmts);
            zan_ast_node_t *ret = zan_ast_new(p->arena, AST_RETURN_STMT, expr->loc);
            ret->ret.value = expr;
            zan_ast_list_push(&getter_body->block.stmts, ret, p->arena);
            has_getter = true;
        } else if (parser_check(p, TK_LBRACE)) {
            parser_advance(p);
            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                if (parser_match(p, TK_GET)) {
                    has_getter = true;
                    if (parser_match(p, TK_SEMICOLON)) {
                        getter_body = NULL; /* automatic */
                    } else if (parser_check(p, TK_LBRACE)) {
                        getter_body = parse_block(p);
                    } else {
                        parser_expect(p, TK_SEMICOLON);
                    }
                } else if (parser_match(p, TK_SET)) {
                    has_setter = true;
                    if (parser_match(p, TK_SEMICOLON)) {
                        setter_body = NULL; /* automatic */
                    } else if (parser_check(p, TK_LBRACE)) {
                        setter_body = parse_block(p);
                    } else {
                        parser_expect(p, TK_SEMICOLON);
                    }
                } else if (is_init_accessor_kw(p)) {
                    parser_advance(p); /* init */
                    has_init = true;
                    if (parser_match(p, TK_SEMICOLON)) {
                        setter_body = NULL; /* automatic */
                    } else if (parser_check(p, TK_LBRACE)) {
                        setter_body = parse_block(p);
                    } else {
                        parser_expect(p, TK_SEMICOLON);
                    }
                } else {
                    parser_advance(p);
                }
            }
            parser_expect(p, TK_RBRACE);
        } else {
            parser_expect(p, TK_LBRACE);
        }

        zan_ast_node_t *n = zan_ast_new(p->arena, AST_PROPERTY_DECL, loc);
        zan_istr_t iname = {(char *)"Item", 4};
        n->field_decl.name = iname;
        n->field_decl.type = type;
        n->field_decl.initializer = NULL;
        n->field_decl.modifiers = mods;
        n->field_decl.getter_body = getter_body;
        n->field_decl.setter_body = setter_body;
        n->field_decl.has_getter = has_getter;
        n->field_decl.has_setter = has_setter;
        n->field_decl.has_init = has_init;
        zan_ast_list_t *iparams =
            (zan_ast_list_t *)zan_arena_alloc(p->arena, sizeof(zan_ast_list_t));
        *iparams = idx_params;
        n->field_decl.indexer_params = iparams;

        if (getter_body) {
            zan_ast_node_t *g = zan_ast_new(p->arena, AST_METHOD_DECL, loc);
            zan_istr_t gistr = {(char *)"op_index", 8};
            g->method_decl.name = gistr;
            g->method_decl.return_type = type;
            g->method_decl.params = idx_params;
            zan_ast_list_init(&g->method_decl.type_params);
            g->method_decl.body = getter_body;
            g->method_decl.modifiers = mods; /* instance, receiver is `this` */
            zan_ast_list_push(&p->pending_members, g, p->arena);
        }
        if (setter_body) {
            zan_ast_node_t *s = zan_ast_new(p->arena, AST_METHOD_DECL, loc);
            zan_istr_t sistr = {(char *)"op_index_set", 12};
            s->method_decl.name = sistr;
            s->method_decl.return_type = NULL; /* void */
            s->method_decl.params = idx_params;
            zan_ast_list_init(&s->method_decl.type_params);
            zan_ast_node_t *vp = zan_ast_new(p->arena, AST_PARAM, loc);
            zan_istr_t vn = {(char *)"value", 5};
            vp->param.name = vn;
            vp->param.type = type;
            vp->param.is_this = false;
            zan_ast_list_push(&s->method_decl.params, vp, p->arena);
            s->method_decl.body = setter_body;
            s->method_decl.modifiers = mods;
            zan_ast_list_push(&p->pending_members, s, p->arena);
        }
        return n;
    }

    zan_istr_t name = {0};
    if (parser_check(p, TK_IDENT)) {
        parser_advance(p);
        name = p->previous.str_val;
    } else if (!parser_take_reserved_word(p, &name)) {
        parser_diag_expected_name(p, p->current.loc, "expected member name");
        return parser_error_node(p);
    }

    if (parser_check(p, TK_ARROW)) {
        parser_advance(p);
        zan_ast_node_t *expr = parse_expression(p);
        parser_expect(p, TK_SEMICOLON);

        zan_ast_node_t *body = zan_ast_new(p->arena, AST_BLOCK, expr->loc);
        zan_ast_list_init(&body->block.stmts);
        zan_ast_node_t *ret = zan_ast_new(p->arena, AST_RETURN_STMT, expr->loc);
        ret->ret.value = expr;
        zan_ast_list_push(&body->block.stmts, ret, p->arena);

        size_t gn = 4;
        char *gname = (char *)zan_arena_alloc(p->arena, gn + (size_t)name.len + 1);
        memcpy(gname, "get_", gn);
        memcpy(gname + gn, name.str, name.len);
        gname[gn + name.len] = '\0';
        zan_istr_t gistr = { gname, (uint32_t)(gn + name.len) };

        zan_ast_node_t *n = zan_ast_new(p->arena, AST_PROPERTY_DECL, loc);
        n->field_decl.name = name;
        n->field_decl.type = type;
        n->field_decl.initializer = NULL;
        n->field_decl.modifiers = mods;
        n->field_decl.getter_body = body;
        n->field_decl.setter_body = NULL;
        zan_ast_list_push(&p->pending_members,
            synth_property_accessor(p, gistr, type, NULL, body, mods, loc),
            p->arena);
        return n;
    }

    if (parser_check(p, TK_LPAREN) || parser_check(p, TK_LESS)) {
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

        zan_ast_list_t params = parse_param_list(p);

        zan_ast_list_t wheres;
        zan_ast_list_init(&wheres);
        parse_where_clauses(p, &wheres);

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
            parser_expect(p, TK_SEMICOLON); /* abstract / extern */
        }

        zan_ast_node_t *n = zan_ast_new(p->arena, AST_METHOD_DECL, loc);
        n->method_decl.name = name;
        n->method_decl.return_type = type;
        n->method_decl.params = params;
        n->method_decl.type_params = type_params;
        n->method_decl.body = body;
        n->method_decl.modifiers = mods;
        n->method_decl.is_variadic = dll_variadic;
        if (dll_import_lib.str || dll_entry_point.str || wheres.count > 0) {
            zan_method_ext_t *ext = zan_ast_ensure_method_ext(n, p->arena);
            ext->extern_lib = dll_import_lib;
            if (dll_entry_point.str) {
                ext->entry_point = (zan_istr_t *)zan_arena_alloc(
                    p->arena, sizeof(zan_istr_t));
                *ext->entry_point = dll_entry_point;
            }
            ext->where_clauses = wheres;
        }
        desugar_yield_method(p, n);
        desugar_async_task_method(p, n);
        return n;
    }

    if (parser_check(p, TK_LBRACE)) {
        zan_token_t peek = zan_lexer_peek(p->lex);
        if (peek.kind == TK_GET || peek.kind == TK_SET) {
            parser_advance(p);
            zan_ast_node_t *getter_body = NULL;
            zan_ast_node_t *setter_body = NULL;
            bool has_getter = false;
            bool has_setter = false;
            bool has_init = false;
            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                if (parser_match(p, TK_GET)) {
                    has_getter = true;
                    if (parser_match(p, TK_SEMICOLON)) {
                        getter_body = NULL; /* automatic */
                    } else if (parser_check(p, TK_LBRACE)) {
                        getter_body = parse_block(p);
                    } else {
                        parser_expect(p, TK_SEMICOLON);
                    }
                } else if (parser_match(p, TK_SET)) {
                    has_setter = true;
                    if (parser_match(p, TK_SEMICOLON)) {
                        setter_body = NULL; /* automatic */
                    } else if (parser_check(p, TK_LBRACE)) {
                        setter_body = parse_block(p);
                    } else {
                        parser_expect(p, TK_SEMICOLON);
                    }
                } else if (is_init_accessor_kw(p)) {
                    parser_advance(p); /* init */
                    has_init = true;
                    if (parser_match(p, TK_SEMICOLON)) {
                        setter_body = NULL; /* automatic */
                    } else if (parser_check(p, TK_LBRACE)) {
                        setter_body = parse_block(p);
                    } else {
                        parser_expect(p, TK_SEMICOLON);
                    }
                } else {
                    parser_advance(p);
                }
            }
            parser_expect(p, TK_RBRACE);

            zan_ast_node_t *init = NULL;
            if (parser_match(p, TK_EQ)) {
                init = parse_expression(p);
                parser_expect(p, TK_SEMICOLON);
            }

            zan_ast_node_t *n = zan_ast_new(p->arena, AST_PROPERTY_DECL, loc);
            n->field_decl.name = name;
            n->field_decl.type = type;
            n->field_decl.initializer = init;
            n->field_decl.modifiers = mods;
            n->field_decl.getter_body = getter_body;
            n->field_decl.setter_body = setter_body;
            n->field_decl.has_getter = has_getter;
            n->field_decl.has_setter = has_setter;
            n->field_decl.has_init = has_init;

            if (getter_body) {
                size_t gn = 4;
                char *gname = (char *)zan_arena_alloc(p->arena,
                    gn + (size_t)name.len + 1);
                memcpy(gname, "get_", gn);
                memcpy(gname + gn, name.str, name.len);
                gname[gn + name.len] = '\0';
                zan_istr_t gistr = { gname, (uint32_t)(gn + name.len) };
                zan_ast_list_push(&p->pending_members,
                    synth_property_accessor(p, gistr, type, NULL, getter_body,
                                            mods, loc),
                    p->arena);
            }
            if (setter_body) {
                size_t sn = 4;
                char *sname = (char *)zan_arena_alloc(p->arena,
                    sn + (size_t)name.len + 1);
                memcpy(sname, "set_", sn);
                memcpy(sname + sn, name.str, name.len);
                sname[sn + name.len] = '\0';
                zan_istr_t sistr = { sname, (uint32_t)(sn + name.len) };
                zan_ast_list_push(&p->pending_members,
                    synth_property_accessor(p, sistr, NULL, type, setter_body,
                                            mods, loc),
                    p->arena);
            }
            return n;
        }
    }

    zan_ast_node_t *init = NULL;
    if (parser_match(p, TK_EQ)) {
        init = parse_expression(p);
    }

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_FIELD_DECL, loc);
    n->field_decl.name = name;
    n->field_decl.type = type;
    n->field_decl.initializer = init;
    n->field_decl.modifiers = mods;
    while (parser_match(p, TK_COMMA)) {
        zan_istr_t extra = {0};
        if (parser_check(p, TK_IDENT)) {
            parser_advance(p);
            extra = p->previous.str_val;
        } else if (!parser_take_reserved_word(p, &extra)) {
            parser_diag_expected_name(p, p->current.loc, "expected member name");
            return parser_error_node(p);
        }
        zan_ast_node_t *extra_init = NULL;
        if (parser_match(p, TK_EQ)) {
            extra_init = parse_expression(p);
        }
        zan_ast_node_t *e = zan_ast_new(p->arena, AST_FIELD_DECL, loc);
        e->field_decl.name = extra;
        e->field_decl.type = type;
        e->field_decl.initializer = extra_init;
        e->field_decl.modifiers = mods;
        zan_ast_list_push(&p->pending_members, e, p->arena);
    }
    parser_expect(p, TK_SEMICOLON);
    return n;
}

