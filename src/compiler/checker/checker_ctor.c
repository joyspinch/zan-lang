static void check_builtin_generic_call_args(zan_checker_t *c,
                                            zan_type_t *recv,
                                            zan_istr_t name,
                                            zan_ast_node_t *call,
                                            zan_type_t **arg_types) {
    if (!recv || !call || !call->call.callee) return;
    if (recv->type_arg_count == 0) return;   /* 非泛型容器不管 */
    /* 只有 List / Dict / Dictionary 这类内建泛型。 */
    const char *bt = NULL;
    if (recv->kind == TYPE_CLASS && recv->name.len == 4 &&
        memcmp(recv->name.str, "List", 4) == 0) bt = "List";
    else if (recv->kind == TYPE_CLASS && recv->name.len == 4 &&
             memcmp(recv->name.str, "Dict", 4) == 0) bt = "Dict";
    if (!bt) {
        /* 用户泛型类\uff08Box<int>）\uff1a成员有 sym，但当调用时会因为
         * “调用了一个同名方法”�%80�过不会报错——核心是调用时会备代替代的实参类型代入。
         * 最好在这里也检查了\uff08解释型参数为\u2014\u2014\u4f20\u7ed9 checker_arg_type_mismatch\uff09\u3002 */
        checker_user_generic_call_args(c, recv, name, call, arg_types);
        return;
    }

    const char *params[8];
    int np = zan_builtin_member_params(bt, name.str, (int)name.len, params, 8);
    if (np <= 0) return;
    int argc = call->call.args.count;
    for (int i = 0; i < argc && i < np; i++) {
        zan_ast_node_t *arg = call->call.args.items[i];
        if (arg && arg->kind == AST_NAMED_ARG) arg = arg->named_arg.expr;
        if (!arg || arg->kind == AST_REF_ARG) continue;
        zan_type_t *at = arg_types[i];
        if (!at) continue;
        zan_type_t *pt = checker_builtin_param_type(c, recv, params[i]);
        if (!pt) continue;
        /* null 字面量按 irgen 的同一条规则处理。 */
        if (arg->kind == AST_NULL_LITERAL) {
            if (zan_null_arg_score(pt) < 0) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, arg->loc, 2032,
                              "cannot convert 'null' to '%s' in argument %d of "
                              "'%s.%.*s': no implicit conversion",
                              type_name(pt), i + 1,
                              type_name(recv), (int)name.len, name.str);
            }
            continue;
        }
        if (!checker_arg_type_mismatch(c, pt, at)) {
            /* 值目是值类型、实参是引用类型＊
             * checker_arg_type_mismatch 对"值目是值类型、实参是引用类型"这个结合放过＊
             * 这里自己检查＊
             * `List<int>.Add("字符串")` 本来会报错，过直接存过取出来是垃转指针。
             * 支持的是“核心原则：“值目型和实参型变换的任何结合都这里跳过。 */
            bool pt_ref = checker_type_is_ref(pt) || pt->kind == TYPE_STRUCT;
            bool at_ref = checker_type_is_ref(at) || at->kind == TYPE_STRUCT;
            if (pt_ref == at_ref) continue;
            if (checker_type_assignable(pt, at)) continue;
            if (checker_has_user_conversion(c, pt, at)) continue;
        }
        zan_diag_emit_code(c->diag, DIAG_ERROR, arg->loc, 2033,
                      "cannot convert '%s' to '%s' in argument %d of "
                      "'%s.%.*s': no implicit conversion",
                      type_name(at), type_name(pt), i + 1,
                      type_name(recv), (int)name.len, name.str);
    }
}

static void check_call_arg_type(zan_checker_t *c, zan_symbol_t *sig, int index,
                                zan_ast_node_t *arg, zan_type_t *arg_type) {
    zan_ast_list_t *ps = &sig->decl->method_decl.params;
    if (index >= ps->count) return;
    zan_type_t *pt = zan_binder_resolve_type(c->binder,
                                             ps->items[index]->param.type);
    if (arg_type == c->binder->type_void) {
        if (!pt || pt->kind != TYPE_DELEGATE) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, arg->loc, 2034,
                          "cannot use a 'void' value in argument %d of "
                          "'%.*s': a void call has no result",
                          index + 1, (int)sig->name.len, sig->name.str);
            return;
        }
        /* void 实参里藏着方法组：形参是委托时必须验签名兼容——
         * 重载路径按兼容性选优，这里兜底唯一方法/无类型兜底的场景
         * （B-ID48：裸方法名 + 委托形参 = 无条件放行）。 */
        zan_ast_node_t *vg = arg;
        while (vg && vg->kind == AST_CAST_EXPR) vg = vg->cast.expr;
        zan_symbol_t *vmg = expr_method_group(c, vg);
        if (vmg && !method_group_matches_delegate(c, vmg, pt)) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, arg->loc, 2098,
                "method '%.*s' cannot be converted to delegate type "
                "'%s': signature is not compatible",
                (int)vmg->name.len, vmg->name.str, type_name(pt));
        }
        return;
    }
    /* null 字面量绑不上值类型形参（唯一方法时没有重载可选，必须在这里
     * 报错：以前它会一路走到 irgen，被当成 0 传下去）。 */
    if (arg && arg->kind == AST_NULL_LITERAL && zan_null_arg_score(pt) < 0) {
        checker_report_null_arg_mismatch(c, arg, pt, index, sig->name);
        return;
    }
    /* 方法组当值实参（B-ID151）：裸方法名/成员方法名在 checking 阶段
     * 定型为 type_error，mismatch 检查对 error 静默——漏到发码侧变
     * 裸函数指针。形参非委托时报 E2039；形参是委托时验签名兼容（与
     * 上面 void 实参分支同规，B-ID48 兜底）；泛型形参 T 可能是委托，
     * 交重载/实例化路径裁决。成员访问形态已在 check_member_access
     * 报过（E2039 同消息同位置，diag 去重不会双报）。 */
    {
        zan_ast_node_t *vg = arg;
        while (vg && vg->kind == AST_CAST_EXPR) vg = vg->cast.expr;
        /* 最外层是 cast 的实参不在此裁决：豁免目标（委托/泛型 T/
         * nint/ulong）是有意转换（(nint)App.GuardBody 函数指针互操），
         * 非豁免目标已由 AST_CAST_EXPR 的方法组检查报过（B-ID151）。 */
        zan_symbol_t *vmg = (arg && arg->kind == AST_CAST_EXPR)
            ? NULL : expr_method_group(c, vg);
        if (vmg && pt && pt->kind != TYPE_TYPE_PARAM) {
            if (pt->kind != TYPE_DELEGATE) {
                reject_method_group(c, vg);
                return;
            }
            if (!method_group_matches_delegate(c, vmg, pt)) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, arg->loc, 2098,
                    "method '%.*s' cannot be converted to delegate type "
                    "'%s': signature is not compatible",
                    (int)vmg->name.len, vmg->name.str, type_name(pt));
                return;
            }
        }
    }
    if (!checker_arg_type_mismatch(c, pt, arg_type)) return;
    zan_diag_emit_code(c->diag, DIAG_ERROR, arg->loc, 2035,
                  "cannot convert '%s' to '%s' in argument %d of '%.*s': "
                  "no implicit conversion",
                  type_name(arg_type), type_name(pt), index + 1,
                  (int)sig->name.len, sig->name.str);
}

static void check_ctor_call_arguments(zan_checker_t *c, zan_type_t *type,
                                      zan_ast_node_t *expr, int argc,
                                      zan_type_t **arg_types) {
    if (!type || type->kind != TYPE_CLASS || !type->sym || argc <= 0) return;
    zan_symbol_t *sym = type->sym;
    int declared = 0;
    int arity_matches = 0;
    int mismatch_first_arg = -1;
    zan_type_t *mismatch_target = NULL;
    zan_type_t *mismatch_source = NULL;
    zan_ast_node_t *mismatch_node = NULL;

    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (!m || m->kind != SYM_CONSTRUCTOR || !m->decl) continue;
        if (m->decl->kind != AST_CONSTRUCTOR_DECL) continue;
        declared++;
        int lo = 0, hi = 0;
        ctor_arity(m->decl, &lo, &hi);
        if (argc < lo || (hi >= 0 && argc > hi)) continue;
        arity_matches++;

        zan_ast_list_t *ps = &m->decl->method_decl.params;
        bool match = true;
        int cand_bad_arg = -1;
        zan_type_t *cand_bad_tgt = NULL;
        zan_type_t *cand_bad_src = NULL;
        zan_ast_node_t *cand_bad_node = NULL;

        for (int j = 0; j < argc; j++) {
            zan_ast_node_t *arg_node = (j < expr->new_expr.args.count)
                ? expr->new_expr.args.items[j] : NULL;
            zan_type_t *at = arg_types[j];
            zan_type_t *pt = NULL;

            if (arg_node && arg_node->kind == AST_NAMED_ARG) {
                for (int q = 0; q < ps->count; q++) {
                    zan_ast_node_t *pp = ps->items[q];
                    if (pp && pp->kind == AST_PARAM &&
                        pp->param.name.len == arg_node->named_arg.name.len &&
                        memcmp(pp->param.name.str, arg_node->named_arg.name.str,
                               (size_t)arg_node->named_arg.name.len) == 0) {
                        pt = zan_binder_resolve_type(c->binder, pp->param.type);
                        break;
                    }
                }
            } else if (j < ps->count) {
                zan_ast_node_t *pp = ps->items[j];
                if (pp && pp->kind == AST_PARAM) {
                    pt = zan_binder_resolve_type(c->binder, pp->param.type);
                    if (pp->param.is_params && pt && pt->kind == TYPE_ARRAY)
                        pt = pt->element_type;
                }
            } else if (ps->count > 0) {
                zan_ast_node_t *pp = ps->items[ps->count - 1];
                if (pp && pp->kind == AST_PARAM && pp->param.is_params) {
                    pt = zan_binder_resolve_type(c->binder, pp->param.type);
                    if (pt && pt->kind == TYPE_ARRAY)
                        pt = pt->element_type;
                }
            }

            if (!pt || !at) continue;
            if (at == c->binder->type_void) {
                /* 方法组实参：委托形参还须签名兼容（B-ID48 同源） */
                bool mg_ok = true;
                zan_ast_node_t *vg = arg_node;
                while (vg && vg->kind == AST_CAST_EXPR) vg = vg->cast.expr;
                zan_symbol_t *vmg = expr_method_group(c, vg);
                if (pt && pt->kind == TYPE_DELEGATE && vmg &&
                    !method_group_matches_delegate(c, vmg, pt))
                    mg_ok = false;
                if (!pt || pt->kind != TYPE_DELEGATE || !mg_ok) {
                    match = false;
                    if (cand_bad_arg < 0) {
                        cand_bad_arg = j;
                        cand_bad_tgt = pt;
                        cand_bad_src = at;
                        cand_bad_node = arg_node;
                    }
                }
                continue;
            }
            if (checker_arg_type_mismatch(c, pt, at)) {
                match = false;
                if (cand_bad_arg < 0) {
                    cand_bad_arg = j;
                    cand_bad_tgt = pt;
                    cand_bad_src = at;
                    cand_bad_node = arg_node;
                }
                break;
            }
        }

        if (match) {
            return;
        }

        if (arity_matches == 1) {
            mismatch_first_arg = cand_bad_arg;
            mismatch_target = cand_bad_tgt;
            mismatch_source = cand_bad_src;
            mismatch_node = cand_bad_node;
        }
    }

    if (declared == 0 || arity_matches == 0) {
        return;
    }

    if (arity_matches == 1 && mismatch_first_arg >= 0 && mismatch_target && mismatch_source) {
        zan_loc_t eloc = mismatch_node ? mismatch_node->loc : expr->loc;
        if (mismatch_source == c->binder->type_void) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, eloc, 2036,
                          "cannot use a 'void' value in argument %d of '%s' "
                          "constructor: a void call has no result",
                          mismatch_first_arg + 1, type_name(type));
        } else {
            zan_diag_emit_code(c->diag, DIAG_ERROR, eloc, 2037,
                          "cannot convert '%s' to '%s' in argument %d of '%s' "
                          "constructor: no implicit conversion",
                          type_name(mismatch_source), type_name(mismatch_target),
                          mismatch_first_arg + 1, type_name(type));
        }
        return;
    }

    zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2038,
                  "no overload of constructor '%s' matches the given argument types",
                  type_name(type));
}

static bool type_is_concatable(zan_type_t *t) {
    if (t->kind == TYPE_NULLABLE)
        return t->element_type && type_is_concatable(t->element_type);
    return t->kind == TYPE_STRING || t->kind == TYPE_CHAR ||
           t->kind == TYPE_ENUM ||
           t->kind == TYPE_BOOL || t->kind == TYPE_ERROR ||
           type_is_numeric(t);
}

static bool binary_op_compares(zan_token_kind_t op) {
    switch (op) {
    case TK_EQ_EQ: case TK_BANG_EQ:
    case TK_LESS: case TK_GREATER: case TK_LESS_EQ: case TK_GREATER_EQ:
        return true;
    default:
        return false;
    }
}

static zan_symbol_t *expr_method_group(zan_checker_t *c, zan_ast_node_t *e) {
    if (!e) return NULL;
    if (e->kind == AST_IDENTIFIER) {
        /* 类型名（如类名访问静态成员 Deflate.LengthBase）优先是类型，绝非方法组 */
        zan_symbol_t *ts = zan_binder_lookup(c->binder, e->ident.name);
        if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT ||
                   ts->kind == SYM_ENUM || ts->kind == SYM_INTERFACE))
            return NULL;
        if (c->current_type_sym &&
            c->current_type_sym->name.len == e->ident.name.len &&
            memcmp(c->current_type_sym->name.str, e->ident.name.str,
                   (size_t)e->ident.name.len) == 0)
            return NULL;
        /* 裸方法名（`form.On("x", OnFocus)`）：局部/形参/字段先遮蔽，
         * 再在当前类型链上找同名方法——与 irgen 的 get_method_sym
         * 同一口径，否则 checker 看不见的方法组会在发码侧变成裸
         * 函数指针，按错误重载的签名调用（B-ID48：腐坏形参）。 */
        if (checker_find_local(c, e->ident.name)) return NULL;
        if (!c->current_type_sym) return NULL;
        zan_symbol_t *f = checker_find_field(c->current_type_sym,
                                             e->ident.name);
        if (f) return NULL;
        return checker_find_method(c->current_type_sym, e->ident.name);
    }
    if (e->kind != AST_MEMBER_ACCESS) return NULL;
    zan_ast_node_t *obj = e->member.object;
    if (!obj || (obj->kind != AST_IDENTIFIER && obj->kind != AST_THIS_EXPR))
        return NULL;
    zan_type_t *ot = zan_checker_check_expr(c, obj);
    if (!ot || !ot->sym) return NULL;
    for (zan_symbol_t *s = ot->sym; s;
         s = (s->type && s->type->base_type) ? s->type->base_type->sym : NULL) {
        for (int i = s->member_count - 1; i >= 0; i--) {
            zan_symbol_t *m = s->members[i];
            if (!m) continue;
            if (m->name.len == e->member.name.len &&
                memcmp(m->name.str, e->member.name.str, (size_t)m->name.len) == 0)
                return m->kind == SYM_METHOD ? m : NULL;
        }
    }
    return NULL;
}

static bool body_returns_this(zan_ast_node_t *n, int depth, int *count) {
    if (!n || depth > CHECKER_DERIVES_MAX_DEPTH) return true;
    switch (n->kind) {
    case AST_RETURN_STMT:
        if (n->ret.value && n->ret.value->kind == AST_THIS_EXPR) {
            (*count)++;
            return true;
        }
        if (n->ret.value && n->ret.value->kind == AST_NULL_LITERAL)
            return true;
        if (!n->ret.value) return true; /* bare `return;` (void) */
        return false;
    case AST_BLOCK:
        for (int i = 0; i < n->block.stmts.count; i++)
            if (!body_returns_this(n->block.stmts.items[i], depth + 1, count))
                return false;
        return true;
    case AST_IF_STMT:
        return body_returns_this(n->if_stmt.then_body, depth + 1, count) &&
               body_returns_this(n->if_stmt.else_body, depth + 1, count);
    case AST_WHILE_STMT: case AST_DO_WHILE_STMT:
        return body_returns_this(n->while_stmt.body, depth + 1, count);
    case AST_FOR_STMT:
        return body_returns_this(n->for_stmt.body, depth + 1, count);
    case AST_FOREACH_STMT:
        return body_returns_this(n->foreach_stmt.body, depth + 1, count);
    case AST_SWITCH_STMT: {
        for (int i = 0; i < n->switch_stmt.cases.count; i++) {
            zan_ast_node_t *sec = n->switch_stmt.cases.items[i];
            if (sec && !body_returns_this(sec->switch_case.body, depth + 1, count))
                return false;
        }
        return true;
    }
    case AST_TRY_STMT: {
        if (!body_returns_this(n->try_stmt.try_body, depth + 1, count))
            return false;
        for (int i = 0; i < n->try_stmt.catches.count; i++) {
            zan_ast_node_t *cat = n->try_stmt.catches.items[i];
            if (cat && !body_returns_this(cat->catch_clause.body, depth + 1, count))
                return false;
        }
        return body_returns_this(n->try_stmt.finally_body, depth + 1, count);
    }
    default:
        return true;
    }
}

static bool expr_is_this_call(zan_checker_t *c, zan_symbol_t *method,
                              zan_type_t *recv) {
    if (!method || !method->decl) return false;
    zan_ast_node_t *m = method->decl;
    if (m->kind != AST_METHOD_DECL) return false;
    zan_ast_node_t *rt = m->method_decl.return_type;
    if (!rt) return false;
    zan_type_t *ret = zan_binder_resolve_type(c->binder, rt);
    if (!ret || !checker_type_is_ref(ret)) return false;
    if (!recv || recv == ret || !checker_type_derives_from(recv, ret))
        return false;
    int count = 0;
    if (!body_returns_this(m->method_decl.body, 0, &count)) return false;
    return count > 0;
}

static void reject_method_group(zan_checker_t *c, zan_ast_node_t *e) {
    zan_symbol_t *m = expr_method_group(c, e);
    if (!m) return;
    zan_diag_emit_code(c->diag, DIAG_ERROR, e->loc, 2039,
                  "'%.*s' is a method, not a value; call it as '%.*s()'",
                  (int)m->name.len, m->name.str,
                  (int)m->name.len, m->name.str);
}

/* 运算符重载委托形参判定（B-ID151）。a += b 在 parser 侧被脱糖为
 * a = a + b；当 a 为事件类（如 UiEvent）时，其重载 static op_add(UiEvent self, Action handler)
 * 的实参 b 是合法方法组。若左操作数类型定义了形参为委托且与方法组
 * 签名兼容的 op_add/op_sub，放行方法组操作数。 */
static bool class_has_op_delegate(zan_checker_t *c, zan_type_t *left,
                                  zan_token_kind_t op, zan_ast_node_t *right_node) {
    if (!left || (left->kind != TYPE_CLASS && left->kind != TYPE_STRUCT) || !left->sym)
        return false;
    zan_symbol_t *mg = expr_method_group(c, right_node);
    if (!mg) return false;
    const char *op_name = NULL;
    if (op == TK_PLUS) op_name = "op_add";
    else if (op == TK_MINUS) op_name = "op_sub";
    if (!op_name) return false;
    zan_istr_t name = { (char *)op_name, (int)strlen(op_name) };
    for (zan_symbol_t *s = left->sym; s;
         s = (s->type && s->type->base_type) ? s->type->base_type->sym : NULL) {
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            if (!m || m->kind != SYM_METHOD || m->name.len != name.len ||
                memcmp(m->name.str, name.str, (size_t)name.len) != 0)
                continue;
            if (!m->decl || m->decl->kind != AST_METHOD_DECL) continue;
            zan_ast_list_t *ps = &m->decl->method_decl.params;
            bool is_static = (m->decl->method_decl.modifiers & MOD_STATIC) != 0;
            int param_idx = is_static ? 1 : 0;
            if (ps->count <= param_idx) continue;
            zan_ast_node_t *pnode = ps->items[param_idx];
            if (!pnode || pnode->kind != AST_PARAM || !pnode->param.type) continue;
            zan_type_t *pt = zan_binder_resolve_type(c->binder, pnode->param.type);
            if (pt && pt->kind == TYPE_DELEGATE) {
                if (method_group_matches_delegate(c, mg, pt)) return true;
            }
        }
    }
    return false;
}

/* 内建类成员调用的方法组实参裁决（B-ID151）。内建类（Console/Math/
 * List/…）没有 sym，call_arg_signature 拿不到签名，check_call_arg_type
 * 从不运行——check_member_access 在实参位置静默后，方法组实参
 * （WriteLine(fi.Length)）原本唯一的一道闸就没了。按 builtin_api 的
 * 签名串解析目标形参：能解析出来时按形参裁决（非委托 E2039，委托验
 * 签名兼容 E2098）；解析不出放行——宁可漏检也不误报。
 * 表名来源：实例接收者用 recv->name（裸名，type_name 会带 "<T>" 后缀
 * 对不上表）；静态内建类（Console/Convert/…）的接收者定型为 error，
 * 用 AST 上的类名标识符。 */
static void checker_builtin_mg_args(zan_checker_t *c, zan_type_t *recv,
                                    zan_istr_t name, zan_ast_node_t *call) {
    if (!call || !call->call.callee ||
        call->call.callee->kind != AST_MEMBER_ACCESS) return;
    char buf[64];
    const char *tn = NULL;
    if (recv && recv->kind != TYPE_ERROR && recv->name.len > 0 &&
        recv->name.len < (int)sizeof(buf)) {
        memcpy(buf, recv->name.str, (size_t)recv->name.len);
        buf[recv->name.len] = 0;
        tn = buf;
    }
    zan_ast_node_t *robj = call->call.callee->member.object;
    if ((!tn || (recv && recv->kind == TYPE_ERROR)) && robj &&
        robj->kind == AST_IDENTIFIER &&
        robj->ident.name.len > 0 &&
        robj->ident.name.len < (int)sizeof(buf)) {
        memcpy(buf, robj->ident.name.str, (size_t)robj->ident.name.len);
        buf[robj->ident.name.len] = 0;
        tn = buf;
    }
    if (!tn) return;
    const char *params[8];
    int np = zan_builtin_member_params(tn, name.str, (int)name.len, params, 8);
    if (np <= 0) return;
    int argc = call->call.args.count;
    for (int i = 0; i < argc && i < np; i++) {
        zan_ast_node_t *arg = call->call.args.items[i];
        if (arg && arg->kind == AST_NAMED_ARG) arg = arg->named_arg.expr;
        if (!arg || arg->kind == AST_REF_ARG) continue;
        zan_ast_node_t *vg = arg;
        while (vg && vg->kind == AST_CAST_EXPR) vg = vg->cast.expr;
        /* 最外层是 cast 的实参不在此裁决——豁免目标是有意转换，
         * 非豁免已由 AST_CAST_EXPR 报过（B-ID151，同 check_call_arg_type）。 */
        zan_symbol_t *vmg = (arg && arg->kind == AST_CAST_EXPR)
            ? NULL : expr_method_group(c, vg);
        if (!vmg) continue;
        zan_type_t *pt = checker_builtin_param_type(c, recv, params[i]);
        if (!pt || pt->kind == TYPE_TYPE_PARAM) continue;
        if (pt->kind != TYPE_DELEGATE) {
            reject_method_group(c, vg);
            continue;
        }
        if (!method_group_matches_delegate(c, vmg, pt)) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, arg->loc, 2098,
                "method '%.*s' cannot be converted to delegate type "
                "'%s': signature is not compatible",
                (int)vmg->name.len, vmg->name.str, type_name(pt));
        }
    }
}

static bool patterns_match_same(zan_ast_node_t *a, zan_ast_node_t *b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    while (a && a->kind == AST_CAST_EXPR) a = a->cast.expr;
    while (b && b->kind == AST_CAST_EXPR) b = b->cast.expr;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case AST_INT_LITERAL:
    case AST_CHAR_LITERAL:
        return a->int_val == b->int_val;
    case AST_STRING_LITERAL:
        return a->str_val.len == b->str_val.len &&
               memcmp(a->str_val.str, b->str_val.str, (size_t)a->str_val.len) == 0;
    case AST_BOOL_LITERAL:
        return a->bool_val == b->bool_val;
    case AST_NULL_LITERAL:
        return true;
    case AST_MEMBER_ACCESS:
        if (a->member.name.len != b->member.name.len ||
            memcmp(a->member.name.str, b->member.name.str, (size_t)a->member.name.len) != 0)
            return false;
        if (a->member.object && b->member.object &&
            a->member.object->kind == AST_IDENTIFIER &&
            b->member.object->kind == AST_IDENTIFIER) {
            return a->member.object->ident.name.len == b->member.object->ident.name.len &&
                   memcmp(a->member.object->ident.name.str,
                          b->member.object->ident.name.str,
                          (size_t)a->member.object->ident.name.len) == 0;
        }
        return false;
    case AST_IDENTIFIER:
        return a->ident.name.len == b->ident.name.len &&
               memcmp(a->ident.name.str, b->ident.name.str, (size_t)a->ident.name.len) == 0;
    default:
        return false;
    }
}

