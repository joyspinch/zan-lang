
void zan_checker_check_stmt(zan_checker_t *c, zan_ast_node_t *stmt) {
    if (!stmt) return;

    switch (stmt->kind) {
    case AST_BLOCK: {
        struct checker_local *saved_locals = c->locals;
        for (int i = 0; i < stmt->block.stmts.count; i++) {
            zan_checker_check_stmt(c, stmt->block.stmts.items[i]);
        }
        c->locals = saved_locals;
        break;
    }

    case AST_VAR_DECL: {
        zan_type_t *dt = stmt->var_decl.type
            ? zan_binder_resolve_type(c->binder, stmt->var_decl.type)
            : NULL;
        zan_type_t *iv = NULL;
        if (stmt->var_decl.initializer) {
            /* 初始化器有声明类型做委托目标裁决：方法组在此静默，
             * 合法性由下面 check_assignable 判（B-ID151）。 */
            int prev_assign = c->in_assignable_ctx;
            c->in_assignable_ctx = 1;
            iv = zan_checker_check_expr(c, stmt->var_decl.initializer);
            c->in_assignable_ctx = prev_assign;
        }
        if (dt) {
            check_generic_constraints(c, dt, stmt->loc);
            if (iv) {
                checker_check_assignable(c, dt, iv, stmt->var_decl.initializer,
                                         stmt->loc, "initializer");
            }
            checker_add_local(c, stmt->var_decl.name, dt);
            checker_mark_local_null_src(c, stmt);
        } else {
            if (stmt->var_decl.initializer) {
                zan_symbol_t *mg = expr_method_group(c, stmt->var_decl.initializer);
                if (mg) {
                    reject_method_group(c, stmt->var_decl.initializer);
                }
            }
            checker_add_local(c, stmt->var_decl.name, iv);
            checker_mark_local_null_src(c, stmt);
        }
        break;
    }

    case AST_EXPR_STMT: {
        zan_symbol_t *mg = expr_method_group(c, stmt->expr_stmt.expr);
        if (mg) {
            reject_method_group(c, stmt->expr_stmt.expr);
        } else {
            zan_checker_check_expr(c, stmt->expr_stmt.expr);
        }
        break;
    }

    case AST_RETURN_STMT:
        if (stmt->ret.value) {
            /* 返回值有 current_return_type 做委托目标裁决：方法组在此
             * 静默，合法性由 check_assignable 判（B-ID151）。 */
            int prev_assign = c->in_assignable_ctx;
            c->in_assignable_ctx = 1;
            zan_type_t *rv = zan_checker_check_expr(c, stmt->ret.value);
            c->in_assignable_ctx = prev_assign;
            if (c->current_return_type &&
                c->current_return_type->kind != TYPE_VOID)
                checker_check_assignable(c, c->current_return_type, rv,
                                         stmt->ret.value, stmt->loc,
                                         "return statement");
            if (c->current_type_sym && c->current_type_sym->kind == SYM_CLASS) {
                if (has_closure_capturing_this(c, stmt->ret.value)) {
                    zan_diag_emit_code(c->diag, DIAG_WARNING, stmt->loc, 2072,
                        "returned closure captures 'this' and escapes method scope; "
                        "ensure the caller does not retain it indefinitely to prevent ARC memory leaks");
                }
            }
        }
        break;

    case AST_IF_STMT: {
        /* A522-③：`if (x = 1)` 曾仅警告、按 C 风格真假运行（C# CS0029 硬错）。
         * 仓库 .zan 全量 4283 处裸标识符条件全是合法 bool——赋值入条件没有
         * 任何合法用例，升级为编译错误。 */
        if (stmt->if_stmt.cond && stmt->if_stmt.cond->kind == AST_ASSIGNMENT)
            zan_diag_emit_code(c->diag, DIAG_ERROR, stmt->if_stmt.cond->loc, 2073,
                          "assignment used as a condition (did you mean '=='? "
                          "assignment (=) has no boolean value");
        zan_type_t *cond_type = zan_checker_check_expr(c, stmt->if_stmt.cond);
        if (cond_type->kind != TYPE_BOOL && cond_type->kind != TYPE_ERROR) {
            zan_diag_emit_code(c->diag, DIAG_WARNING, stmt->if_stmt.cond->loc, 2074,
                          "condition should be bool, got '%s'", type_name(cond_type));
        }
        zan_checker_check_stmt(c, stmt->if_stmt.then_body);
        if (stmt->if_stmt.else_body) {
            zan_checker_check_stmt(c, stmt->if_stmt.else_body);
        }
        break;
    }

    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        zan_checker_check_expr(c, stmt->while_stmt.cond);
        zan_checker_check_stmt(c, stmt->while_stmt.body);
        break;

    case AST_FOR_STMT:
        if (stmt->for_stmt.init) zan_checker_check_stmt(c, stmt->for_stmt.init);
        if (stmt->for_stmt.cond) zan_checker_check_expr(c, stmt->for_stmt.cond);
        if (stmt->for_stmt.step) zan_checker_check_expr(c, stmt->for_stmt.step);
        zan_checker_check_stmt(c, stmt->for_stmt.body);
        break;

    case AST_FOREACH_STMT:
        no_runtime_reject(c, stmt->loc, "`foreach`");
        {
            zan_type_t *col = zan_checker_check_expr(c, stmt->foreach_stmt.collection);
            zan_type_t *elem = NULL;
            if (col) {
                if (col->kind == TYPE_ARRAY) {
                    elem = col->element_type;
                } else if (col->kind == TYPE_CLASS || col->kind == TYPE_STRUCT || col->kind == TYPE_INTERFACE) {
                    zan_symbol_t *csym = col->sym;
                    if (csym) {
                        zan_istr_t gi = { (char *)"GetEnumerator", 13 };
                        zan_symbol_t *gm = checker_find_method(csym, gi);
                        if (gm && gm->decl && gm->decl->kind == AST_METHOD_DECL &&
                            gm->decl->method_decl.return_type) {
                            zan_type_t *rt = zan_binder_resolve_type(c->binder,
                                gm->decl->method_decl.return_type);
                            if (rt && rt->sym) {
                                zan_istr_t ci = { (char *)"Current", 7 };
                                zan_symbol_t *cm = checker_find_method(rt->sym, ci);
                                if (cm && cm->decl && cm->decl->kind == AST_METHOD_DECL &&
                                    cm->decl->method_decl.return_type) {
                                    elem = zan_binder_resolve_type(c->binder,
                                        cm->decl->method_decl.return_type);
                                } else {
                                    zan_symbol_t *cf = checker_find_field(rt->sym, ci);
                                    if (cf && cf->decl &&
                                        (cf->decl->kind == AST_FIELD_DECL ||
                                         cf->decl->kind == AST_PROPERTY_DECL) &&
                                        cf->decl->field_decl.type) {
                                        elem = zan_binder_resolve_type(c->binder,
                                            cf->decl->field_decl.type);
                                    }
                                }
                            }
                        }
                    }
                    if (elem && elem->kind == TYPE_TYPE_PARAM) {
                        if (col->type_args && col->type_arg_count > 0 && csym && csym->decl &&
                            (csym->decl->kind == AST_CLASS_DECL || csym->decl->kind == AST_STRUCT_DECL || csym->decl->kind == AST_INTERFACE_DECL)) {
                            zan_ast_list_t *tps = &csym->decl->type_decl.type_params;
                            if (tps->count == col->type_arg_count) {
                                elem = zan_binder_subst_named(c->binder, elem, tps, col->type_args);
                            }
                        }
                        if (elem && elem->kind == TYPE_TYPE_PARAM && col->type_arg_count == 1) {
                            elem = col->type_args[0];
                        }
                    }
                    if (!elem && (col->kind == TYPE_CLASS || col->kind == TYPE_INTERFACE) && col->type_arg_count == 1) {
                        elem = col->type_args[0];
                    }
                }
            }
            if (stmt->foreach_stmt.var_type) {
                elem = zan_binder_resolve_type(c->binder,
                                               stmt->foreach_stmt.var_type);
            }
            checker_add_local(c, stmt->foreach_stmt.var_name, elem);
        }
        zan_checker_check_stmt(c, stmt->foreach_stmt.body);
        break;

    case AST_THROW_STMT:
        no_runtime_reject(c, stmt->loc, "`throw`");
        zan_checker_check_expr(c, stmt->throw_stmt.value);
        break;

    case AST_BREAK_STMT:
    case AST_CONTINUE_STMT:
    case AST_GOTO_STMT:
    case AST_LABEL_STMT:
        break;

    case AST_CHECKED_STMT:
        zan_checker_check_stmt(c, stmt->checked_stmt.body);
        break;

    case AST_LOCK_STMT:
        no_runtime_reject(c, stmt->loc, "`lock`");
        zan_checker_check_expr(c, stmt->lock_stmt.expr);
        zan_checker_check_stmt(c, stmt->lock_stmt.body);
        break;

    case AST_SWITCH_STMT: {
        zan_type_t *sw_type = zan_checker_check_expr(c, stmt->switch_stmt.expr);
        bool has_patterns = false;
        for (int i = 0; i < stmt->switch_stmt.cases.count && !has_patterns; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            if (sc->switch_case.type_pattern ||
                (sc->switch_case.pattern &&
                 sc->switch_case.pattern->kind == AST_NULL_LITERAL))
                has_patterns = true;
        }
        if (!has_patterns && !type_is_numeric(sw_type) &&
            sw_type->kind != TYPE_STRING && sw_type->kind != TYPE_CHAR &&
            sw_type->kind != TYPE_ENUM && sw_type->kind != TYPE_BOOL &&
            sw_type->kind != TYPE_ERROR) {
            zan_diag_emit_code(c->diag, DIAG_WARNING, stmt->loc, 2075,
                          "switch expression has non-switchable type '%s'", type_name(sw_type));
        }

        bool seen_wildcard = false;
        for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            bool is_default = (sc->switch_case.pattern == NULL &&
                               sc->switch_case.type_pattern == NULL &&
                               sc->switch_case.var_name.len == 0);

            if (seen_wildcard) {
                zan_diag_emit_code(c->diag, DIAG_WARNING, sc->loc, 2076,
                              "unreachable switch case: previous default/catch-all case matches all remaining values");
            }

            if (sc->switch_case.pattern) {
                zan_checker_check_expr(c, sc->switch_case.pattern);
                if (sc->switch_case.when_cond == NULL) {
                    for (int prev = 0; prev < i; prev++) {
                        zan_ast_node_t *prev_sc = stmt->switch_stmt.cases.items[prev];
                        if (prev_sc->switch_case.when_cond == NULL &&
                            prev_sc->switch_case.pattern != NULL &&
                            patterns_match_same(sc->switch_case.pattern, prev_sc->switch_case.pattern)) {
                            zan_diag_emit_code(c->diag, DIAG_ERROR, sc->loc, 2077,
                                          "duplicate case label in switch statement");
                            break;
                        }
                    }
                }
            } else if (is_default) {
                for (int prev = 0; prev < i; prev++) {
                    zan_ast_node_t *prev_sc = stmt->switch_stmt.cases.items[prev];
                    bool prev_is_default = (prev_sc->switch_case.pattern == NULL &&
                                            prev_sc->switch_case.type_pattern == NULL &&
                                            prev_sc->switch_case.var_name.len == 0);
                    if (prev_is_default) {
                        zan_diag_emit_code(c->diag, DIAG_ERROR, sc->loc, 2078,
                                      "duplicate default label in switch statement");
                        break;
                    }
                }
                seen_wildcard = true;
            }

            /* 模式匹配变量引入：case T x 将 x 绑定入守卫与代码块作用域 */
            if (sc->switch_case.type_pattern && sc->switch_case.var_name.len > 0) {
                zan_type_t *pt = zan_binder_resolve_type(
                    c->binder, sc->switch_case.type_pattern);
                checker_add_local(c, sc->switch_case.var_name, pt);
                if (sc->switch_case.when_cond == NULL &&
                    (pt->kind == TYPE_OBJECT || pt == sw_type)) {
                    seen_wildcard = true;
                }
            }
            if (sc->switch_case.when_cond) {
                zan_checker_check_expr(c, sc->switch_case.when_cond);
            }
            zan_checker_check_stmt(c, sc->switch_case.body);
        }

        if (sw_type && sw_type->kind == TYPE_ENUM && sw_type->sym && !seen_wildcard) {
            zan_symbol_t *esym = sw_type->sym;
            for (int mi = 0; mi < esym->member_count; mi++) {
                zan_symbol_t *em = esym->members[mi];
                if (em->kind != SYM_ENUM_MEMBER) continue;
                bool handled = false;
                for (int ci = 0; ci < stmt->switch_stmt.cases.count; ci++) {
                    zan_ast_node_t *sc = stmt->switch_stmt.cases.items[ci];
                    if (sc->switch_case.pattern) {
                        zan_ast_node_t *pat = sc->switch_case.pattern;
                        while (pat && pat->kind == AST_CAST_EXPR) pat = pat->cast.expr;
                        if (pat && pat->kind == AST_MEMBER_ACCESS &&
                            pat->member.name.len == em->name.len &&
                            memcmp(pat->member.name.str, em->name.str, em->name.len) == 0) {
                            handled = true;
                            break;
                        }
                        if (pat && pat->kind == AST_IDENTIFIER &&
                            pat->ident.name.len == em->name.len &&
                            memcmp(pat->ident.name.str, em->name.str, em->name.len) == 0) {
                            handled = true;
                            break;
                        }
                    }
                }
                if (!handled) {
                    zan_diag_emit_code(c->diag, DIAG_WARNING, stmt->loc, 2079,
                                  "switch statement does not handle enum member '%.*s' of '%.*s'; add missing case or default label",
                                  (int)em->name.len, em->name.str,
                                  (int)esym->name.len, esym->name.str);
                }
            }
        }

        if (sw_type && sw_type->kind == TYPE_BOOL && !seen_wildcard) {
            bool has_true = false;
            bool has_false = false;
            for (int ci = 0; ci < stmt->switch_stmt.cases.count; ci++) {
                zan_ast_node_t *sc = stmt->switch_stmt.cases.items[ci];
                if (sc->switch_case.pattern && sc->switch_case.when_cond == NULL) {
                    zan_ast_node_t *pat = sc->switch_case.pattern;
                    while (pat && pat->kind == AST_CAST_EXPR) pat = pat->cast.expr;
                    if (pat && pat->kind == AST_BOOL_LITERAL) {
                        if (pat->bool_val) has_true = true;
                        else has_false = true;
                    }
                }
            }
            if (!has_true) {
                zan_diag_emit_code(c->diag, DIAG_WARNING, stmt->loc, 2080,
                              "switch statement on bool does not handle 'true' case; add missing case or default label");
            }
            if (!has_false) {
                zan_diag_emit_code(c->diag, DIAG_WARNING, stmt->loc, 2081,
                              "switch statement on bool does not handle 'false' case; add missing case or default label");
            }
        }
        break;
    }

    case AST_TRY_STMT:
        no_runtime_reject(c, stmt->loc, "`try`");
        zan_checker_check_stmt(c, stmt->try_stmt.try_body);
        for (int i = 0; i < stmt->try_stmt.catches.count; i++) {
            zan_ast_node_t *cc = stmt->try_stmt.catches.items[i];
            zan_checker_check_stmt(c, cc->catch_clause.body);
        }
        if (stmt->try_stmt.finally_body) {
            zan_checker_check_stmt(c, stmt->try_stmt.finally_body);
        }
        break;

    default: {
        /* B-ID102 覆盖审计：有非 NULL 子节点的语句 kind 落到 default 即疑似漏改
         * （ZANC_TRACE 门控）。 */
        int nc = zan_ast_child_count(stmt), live = 0;
        for (int i = 0; i < nc; i++) if (zan_ast_child_at(stmt, i)) live++;
        if (live > 0)
            zan_compile_trace("checker.check_stmt: kind %d has %d live children but no case",
                              (int)stmt->kind, live);
        break;
    }
    }
}

