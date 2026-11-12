static bool method_accepts_argc(zan_symbol_t *m, int argc) {
    int lo, hi;
    if (method_arity(m, &lo, &hi)) return argc >= lo && argc <= hi;
    if (!m->decl || m->decl->kind != AST_METHOD_DECL) return false;
    zan_ast_list_t *ps = &m->decl->method_decl.params;
    if (m->decl->method_decl.is_variadic) return argc >= ps->count;
    if (ps->count == 0) return false;
    zan_ast_node_t *last = ps->items[ps->count - 1];
    if (!last || last->kind != AST_PARAM || !last->param.is_params) return false;
    return argc >= ps->count - 1;
}

static bool method_is_params_tail(zan_symbol_t *m) {
    if (!m->decl || m->decl->kind != AST_METHOD_DECL) return false;
    zan_ast_list_t *ps = &m->decl->method_decl.params;
    if (ps->count == 0) return false;
    zan_ast_node_t *last = ps->items[ps->count - 1];
    return last && last->kind == AST_PARAM && last->param.is_params;
}

static zan_symbol_t *checker_find_method_call(zan_symbol_t *type_sym,
                                               zan_istr_t name, int argc,
                                               int type_arg_count) {
    /* Inapplicable same-name members must not hide extension methods. */
    for (zan_symbol_t *s = type_sym; s;
         s = (s->type && s->type->base_type) ? s->type->base_type->sym : NULL) {
        zan_symbol_t *variadic = NULL;
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            if (!m || m->kind != SYM_METHOD || m->name.len != name.len ||
                memcmp(m->name.str, name.str, (size_t)name.len) != 0)
                continue;

            if (!method_accepts_argc(m, argc)) continue;
            int mtps = (m->decl && m->decl->kind == AST_METHOD_DECL)
                ? m->decl->method_decl.type_params.count : 0;
            if (type_arg_count > 0 && mtps != type_arg_count) continue;
            if (method_is_params_tail(m)) {
                if (!variadic) variadic = m;
                continue;
            }
            return m;
        }
        if (variadic) return variadic;
    }
    return NULL;
}

/* 同一个实参上的"null 绑不上值类型形参"只报一次：唯一方法路径
 * （check_call_arg_type）与重载路径（checker_find_method_typed）会撞上
 * 同一处，报两遍只是噪音。 */
static bool checker_report_null_arg_mismatch(zan_checker_t *c,
                                             zan_ast_node_t *arg,
                                             zan_type_t *pt, int index,
                                             zan_istr_t sig_name) {
    if (!arg || !pt) return false;
    zan_loc_t loc = arg->loc;
    if (c->has_null_arg_reported &&
        c->null_arg_reported.file_id == loc.file_id &&
        c->null_arg_reported.line == loc.line &&
        c->null_arg_reported.col == loc.col &&
        c->null_arg_reported.offset == loc.offset)
        return false;
    c->has_null_arg_reported = true;
    c->null_arg_reported = loc;
    zan_diag_emit_code(c->diag, DIAG_ERROR, loc, 2025,
                  "cannot convert 'null' to '%s' in argument %d of '%.*s': "
                  "null cannot be assigned to a value type",
                  type_name(pt), index + 1, (int)sig_name.len, sig_name.str);
    return true;
}

/* 方法类型参数的结构化合一（A518）：dp 形参类型里的 TYPE_TYPE_PARAM 按
 * 名字绑到实参类型；容器/数组/可空递归进元素。推断不出留 NULL——
 * 约束检查对未定绑定放行（与 skill 一.7 的"判不出不误杀"同口径）。 */
static void checker_unify_method_tp(zan_type_t *dp, zan_type_t *at,
                                    zan_ast_list_t *tps, zan_type_t **bind,
                                    int ntp) {
    if (!dp || !at) return;
    if (dp->kind == TYPE_TYPE_PARAM) {
        for (int i = 0; i < tps->count && i < ntp; i++) {
            zan_ast_node_t *tp = tps->items[i];
            if (tp->ident.name.len == dp->name.len &&
                memcmp(tp->ident.name.str, dp->name.str,
                       (size_t)dp->name.len) == 0) {
                if (!bind[i] && at->kind != TYPE_TYPE_PARAM &&
                    at->kind != TYPE_ERROR)
                    bind[i] = at;
                return;
            }
        }
        return;
    }
    if (dp->type_arg_count > 0 && dp->type_arg_count == at->type_arg_count) {
        for (int i = 0; i < dp->type_arg_count; i++)
            checker_unify_method_tp(dp->type_args[i], at->type_args[i],
                                    tps, bind, ntp);
    } else if ((dp->kind == TYPE_ARRAY || dp->kind == TYPE_NULLABLE) &&
               dp->element_type && at->element_type) {
        checker_unify_method_tp(dp->element_type, at->element_type,
                                tps, bind, ntp);
    }
}

/* dp 的类型形状里是否出现 tps 中的任一方法类型参数（按名字） */
static bool type_mentions_tp_checker(zan_type_t *t, zan_ast_list_t *tps) {
    if (!t) return false;
    if (t->kind == TYPE_TYPE_PARAM) {
        for (int i = 0; i < tps->count; i++) {
            zan_ast_node_t *tp = tps->items[i];
            if (tp->ident.name.len == t->name.len &&
                memcmp(tp->ident.name.str, t->name.str,
                       (size_t)t->name.len) == 0)
                return true;
        }
        return false;
    }
    if (t->type_arg_count > 0) {
        for (int i = 0; i < t->type_arg_count; i++)
            if (type_mentions_tp_checker(t->type_args[i], tps)) return true;
    }
    if (t->element_type && type_mentions_tp_checker(t->element_type, tps))
        return true;
    return false;
}

/* 泛型方法调用点的 where 约束检查（TASKS.md A518）：
 * `static T FirstItem<T>(List<T> xs) where T : Entity` 传 List<int> 曾静默
 * 通过——check_generic_constraints 只挂在 new/声明/返回类型三处，调用解析
 * 路径缺失。这里按显式类型实参 + 形参形状合一推出绑定，再逐条 where 检查；
 * 绑不出的参数放行。 */
static void checker_check_method_call_constraints(zan_checker_t *c,
                                                  zan_symbol_t *msym,
                                                  zan_ast_node_t *call,
                                                  zan_type_t **args) {
    if (!msym || msym->kind != SYM_METHOD || !msym->decl ||
        msym->decl->kind != AST_METHOD_DECL)
        return;
    zan_ast_node_t *m = msym->decl;
    zan_ast_list_t *tps = &m->method_decl.type_params;
    if (tps->count <= 0 || tps->count > 8) return;
    zan_ast_list_t *wcs = zan_ast_method_where_clauses(m);
    if (!wcs || wcs->count == 0) return;

    zan_type_t *bind[8] = { 0 };
    for (int i = 0; i < tps->count && i < call->call.type_args.count; i++)
        bind[i] = zan_binder_resolve_type(c->binder, call->call.type_args.items[i]);
    if (call->call.type_args.count == 0) {
        zan_ast_list_t *ps = &m->method_decl.params;
        for (int j = 0; j < ps->count && j < call->call.args.count; j++) {
            zan_ast_node_t *a = call->call.args.items[j];
            if (!a || a->kind == AST_LAMBDA || a->kind == AST_NAMED_ARG) continue;
            zan_type_t *dp = zan_binder_resolve_type(c->binder,
                ps->items[j]->param.type);
            if (!dp || !type_mentions_tp_checker(dp, tps)) continue;
            checker_unify_method_tp(dp, args ? args[j] : NULL, tps, bind,
                                    tps->count);
        }
    }
    for (int w = 0; w < wcs->count; w++) {
        zan_ast_node_t *wc = wcs->items[w];
        int idx = -1;
        for (int i = 0; i < tps->count; i++) {
            zan_ast_node_t *tp = tps->items[i];
            if (tp->ident.name.len == wc->where_clause.param_name.len &&
                memcmp(tp->ident.name.str, wc->where_clause.param_name.str,
                       (size_t)wc->where_clause.param_name.len) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0 || idx >= tps->count) continue;
        zan_type_t *arg = bind[idx];
        if (wc->where_clause.has_new && arg && arg->kind != TYPE_TYPE_PARAM && arg->kind != TYPE_ERROR) {
            bool ok = false;
            if (arg->kind == TYPE_STRUCT) {
                ok = true;
            } else if (arg->kind == TYPE_CLASS) {
                if (arg->sym && !checker_class_is_abstract(arg->sym)) {
                    if (checker_has_default_ctor(c, arg->sym)) ok = true;
                }
            }
            if (!ok) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, call->loc, 2026,
                    "type '%s' must have a public parameterless constructor to satisfy 'new()' constraint for type parameter '%.*s' of method '%.*s'",
                    type_name(arg),
                    (int)wc->where_clause.param_name.len,
                    wc->where_clause.param_name.str,
                    (int)msym->name.len, msym->name.str);
            }
        }
        if (!arg || arg->kind == TYPE_TYPE_PARAM || arg->kind == TYPE_ERROR)
            continue; /* 未定绑定放行 */
        for (int k = 0; k < wc->where_clause.constraints.count; k++) {
            zan_ast_node_t *cons_ref = wc->where_clause.constraints.items[k];
            zan_type_t *cons = zan_binder_resolve_type(c->binder, cons_ref);
            if (!cons || cons->kind == TYPE_ERROR) continue;
            if (!type_satisfies_constraint(arg, cons)) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, call->loc, 2027,
                    "type '%s' does not satisfy constraint '%s' for type "
                    "parameter '%.*s' of method '%.*s'",
                    type_name(arg), type_name(cons),
                    (int)wc->where_clause.param_name.len,
                    wc->where_clause.param_name.str,
                    (int)msym->name.len, msym->name.str);
            }
        }
    }
}

static zan_symbol_t *checker_find_method_typed(zan_checker_t *c,
                                                zan_type_t *recv_type,
                                                zan_symbol_t *type_sym,
                                                zan_istr_t name,
                                                zan_ast_node_t *call,
                                                zan_type_t **args) {
    zan_symbol_t *best = NULL;
    int best_score = -1;
    /* null 实参把候选判死的现场：留一份用于报错（见函数末尾）。 */
    zan_type_t *null_bad_type = NULL;
    int null_bad_index = -1;
    zan_type_t *mg_delegate_candidates[16] = {0};
    int argc = call->call.args.count;
    int type_arg_count = call->call.type_args.count;
    for (zan_symbol_t *s = type_sym; s;
         s = (s->type && s->type->base_type) ? s->type->base_type->sym : NULL) {
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            if (!m || m->kind != SYM_METHOD || m->name.len != name.len ||
                memcmp(m->name.str, name.str, (size_t)name.len) != 0 ||
                !method_accepts_argc(m, argc)) continue;
            int mtps = (m->decl && m->decl->kind == AST_METHOD_DECL)
                ? m->decl->method_decl.type_params.count : 0;
            if (type_arg_count > 0 && mtps != type_arg_count) continue;
            zan_ast_list_t *ps = &m->decl->method_decl.params;
            int score = 0;
            bool compatible = true;
            for (int j = 0; j < argc && j < ps->count; j++) {
                zan_ast_node_t *arg = call->call.args.items[j];
                if (!arg || arg->kind == AST_NAMED_ARG ||
                    arg->kind == AST_REF_ARG) continue;
                zan_ast_node_t *param = ps->items[j];
                if (!param || param->kind != AST_PARAM) continue;
                zan_type_t *pt = zan_binder_resolve_type(c->binder, param->param.type);
                if (param->param.is_params && pt && pt->kind == TYPE_ARRAY)
                    pt = pt->element_type;
                if (pt && recv_type && recv_type->type_arg_count > 0 && s && s->decl &&
                    (s->decl->kind == AST_CLASS_DECL || s->decl->kind == AST_STRUCT_DECL)) {
                    zan_ast_list_t *tps = &s->decl->type_decl.type_params;
                    if (tps->count == recv_type->type_arg_count) {
                        pt = zan_binder_subst_named(c->binder, pt, tps, recv_type->type_args);
                    }
                }
                zan_type_t *at = args[j];
                /* lambda 实参：lambda 自己没有类型（类型由目标委托形参决定），
                 * 所以这里不能拿 at 比——at 是 error，比不出结果，只能按
                 * 「形参是不是委托 + 委托的形参个数对不对得上」判。
                 *
                 * 踩过的坑：这里早退（at 是 error）时，所有候选同分，
                 * 重载决议就退化成「取成员表里第一个同名方法」。IDE 里
                 * `StartAsync(app != null ? (Action a) => ... : null)`
                 * 因此绑到了 App 那个重载上，无限自我递归。 */
                zan_ast_node_t *lam = zan_ast_lambda_source(arg);
                if (lam) {
                    if (!pt || pt->kind == TYPE_ERROR) continue;
                    if (pt->kind == TYPE_TYPE_PARAM) continue; /* T 可能是委托 */
                    if (checker_type_is_expr_tree_target(pt)) {
                        /* B-ID65：ExprNode / Expr<T> 形参收树 lambda。只有
                         * 显式标了树类型形参（`(ExprNode x) => …`）的表达
                         * 式体单参 lambda 才给 +8 压过 Func 候选（+4）——
                         * 无类型 lambda 只给 +2，存量 `Where(x => …)` 继续
                         * 走委托（内存过滤），行为零变化。块体不可树译。 */
                        if (lam->lambda.params.count != 1 || !lam->lambda.body ||
                            lam->lambda.body->kind == AST_BLOCK) {
                            compatible = false;
                            break;
                        }
                        zan_ast_node_t *lp0 = lam->lambda.params.items[0];
                        bool want_tree = lp0 && lp0->kind == AST_PARAM &&
                            lp0->param.type &&
                            checker_type_is_expr_tree_target(
                                zan_binder_resolve_type(c->binder,
                                                        lp0->param.type));
                        score += want_tree ? 8 : 2;
                        continue;
                    }
                    if (pt->kind != TYPE_DELEGATE) { compatible = false; break; }
                    if (lam->lambda.params.count != pt->delegate_param_count) {
                        compatible = false;
                        break;
                    }
                    score += 4;
                    /* lambda 显式声明的形参类型与委托形参类型一致 → 更贴 */
                    for (int k = 0; k < lam->lambda.params.count; k++) {
                        zan_ast_node_t *lp = lam->lambda.params.items[k];
                        if (!lp || lp->kind != AST_PARAM || !lp->param.type) continue;
                        zan_type_t *lpt = zan_binder_resolve_type(c->binder, lp->param.type);
                        zan_type_t *dpt = pt->delegate_param_types[k];
                        if (lpt && dpt && checker_type_equal(lpt, dpt)) score += 1;
                    }
                    if (pt->delegate_ret_type && lam->lambda.body &&
                        lam->lambda.body->kind != AST_BLOCK &&
                        lam->lambda.params.count == 1 && pt->delegate_param_count == 1) {
                        zan_ast_node_t *lp = lam->lambda.params.items[0];
                        zan_ast_node_t *body = lam->lambda.body;
                        if (lp && lp->kind == AST_PARAM && body->kind == AST_MEMBER_ACCESS &&
                            body->member.object && body->member.object->kind == AST_IDENTIFIER &&
                            body->member.object->ident.name.len == lp->param.name.len &&
                            memcmp(body->member.object->ident.name.str, lp->param.name.str, lp->param.name.len) == 0) {
                            zan_type_t *param_t = pt->delegate_param_types[0];
                            if (param_t && param_t->kind == TYPE_TYPE_PARAM && recv_type && recv_type->type_arg_count > 0) {
                                if (type_sym && type_sym->decl && (type_sym->decl->kind == AST_CLASS_DECL || type_sym->decl->kind == AST_STRUCT_DECL)) {
                                    zan_ast_list_t *tps = &type_sym->decl->type_decl.type_params;
                                    for (int tp_i = 0; tp_i < tps->count && tp_i < recv_type->type_arg_count; tp_i++) {
                                        zan_ast_node_t *tpn = tps->items[tp_i];
                                        if (tpn && tpn->param.name.len == param_t->name.len &&
                                            memcmp(tpn->param.name.str, param_t->name.str, param_t->name.len) == 0) {
                                            param_t = recv_type->type_args[tp_i];
                                            break;
                                        }
                                    }
                                } else if (recv_type->type_arg_count == 1) {
                                    param_t = recv_type->type_args[0];
                                }
                            }
                            zan_symbol_t *tsym = param_t ? (param_t->sym ? param_t->sym : (param_t->name.len ? zan_binder_lookup(c->binder, param_t->name) : NULL)) : NULL;
                            if (tsym) {
                                zan_symbol_t *f = checker_find_field(tsym, body->member.name);
                                if (f && f->type) {
                                    if (checker_type_equal(f->type, pt->delegate_ret_type) ||
                                        checker_type_assignable(pt->delegate_ret_type, f->type)) {
                                        score += 8;
                                    } else {
                                        compatible = false;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    continue;
                }
                zan_ast_node_t *mg_probe = arg;
                while (mg_probe && mg_probe->kind == AST_CAST_EXPR)
                    mg_probe = mg_probe->cast.expr;
                /* 最外层是 cast 的实参不在此裁决（豁免目标有意转换，
                 * 非豁免已由 AST_CAST_EXPR 报过，同 check_call_arg_type）。 */
                zan_symbol_t *mgs = (arg && arg->kind == AST_CAST_EXPR)
                    ? NULL : expr_method_group(c, mg_probe);
                if (mgs) {
                    /* 方法组实参：与 lambda 同规——按目标委托形参的签名
                     * 兼容性打分，不兼容的候选直接出局。裸方法名在
                     * checking 阶段定型为 void（方法符号的 ->type 是返回
                     * 类型），不比签名会全候选同分，退化成"第一个同名
                     * 重载"（B-ID48：On 四重载错绑 Action，运行期腐坏
                     * 形参）。 */
                    if (!pt || pt->kind == TYPE_ERROR) continue;
                    if (pt->kind == TYPE_TYPE_PARAM) continue; /* T 可能是委托 */
                    if (pt->kind == TYPE_DELEGATE) {
                        if (j < 16 && !mg_delegate_candidates[j])
                            mg_delegate_candidates[j] = pt;
                        if (!method_group_matches_delegate(c, mgs, pt)) {
                            compatible = false;
                            break;
                        }
                        score += 4;
                        continue;
                    }
                    compatible = false;
                    break;
                }
                if (!pt || !at || pt->kind == TYPE_ERROR ||
                    at->kind == TYPE_ERROR || pt->kind == TYPE_TYPE_PARAM ||
                    at->kind == TYPE_TYPE_PARAM) continue;
                /* null 字面量：只能绑到接得住 null 的形参（与 irgen 同一条
                 * 规则，见 zan_null_arg_score）。 */
                if (arg->kind == AST_NULL_LITERAL) {
                    int ns = zan_null_arg_score(pt);
                    if (ns < 0) {
                        if (!null_bad_type) {
                            null_bad_type = pt;
                            null_bad_index = j;
                        }
                        compatible = false;
                        break;
                    }
                    score += ns;
                    continue;
                }
                if (checker_type_equal(pt, at)) { score += 4; continue; }
                if (checker_type_assignable(pt, at)) { score += 1; continue; }
                if (checker_has_user_conversion(c, pt, at)) continue;
                compatible = false;
                break;
            }
            if (!compatible) continue;
            if (score > best_score ||
                (score == best_score && best &&
                 method_is_params_tail(best) && !method_is_params_tail(m))) {
                best = m;
                best_score = score;
            }
        }
    }
    if (!best) {
        if (null_bad_type) {
            /* 没有任何重载接得住这个 null：报错，不要退回"第一个同名方法"，
             * 否则 null 又会被悄悄当成值类型实参传下去。 */
            zan_ast_node_t *bad_arg =
                (null_bad_index >= 0 && null_bad_index < argc)
                    ? call->call.args.items[null_bad_index] : NULL;
            checker_report_null_arg_mismatch(c, bad_arg, null_bad_type,
                                             null_bad_index, name);
            return NULL;
        }
        for (int j = 0; j < argc; j++) {
            zan_ast_node_t *arg = call->call.args.items[j];
            if (!arg) continue;
            if (arg->kind == AST_NAMED_ARG) arg = arg->named_arg.expr;
            zan_ast_node_t *vg = arg;
            while (vg && vg->kind == AST_CAST_EXPR) vg = vg->cast.expr;
            /* 最外层是 cast 的实参不在此裁决（豁免目标有意转换，
             * 非豁免已由 AST_CAST_EXPR 报过，同 check_call_arg_type）。 */
            zan_symbol_t *mgs = (arg && arg->kind == AST_CAST_EXPR)
                ? NULL : expr_method_group(c, vg);
            if (mgs) {
                /* 没有任何重载接受此方法组：若有候选形参为委托但签名不匹配，
                 * 报 E2098；若候选形参均非委托（如 Convert.ToString(sw.ElapsedMilliseconds)），
                 * 报 E2039。绝不能退回"第一个同名方法"把裸函数指针当值传下去（B-ID151）。 */
                zan_type_t *dlg = (j < 16) ? mg_delegate_candidates[j] : NULL;
                if (dlg) {
                    zan_diag_emit_code(c->diag, DIAG_ERROR, vg->loc, 2098,
                        "method '%.*s' cannot be converted to delegate type "
                        "'%s': signature is not compatible",
                        (int)mgs->name.len, mgs->name.str, type_name(dlg));
                } else {
                    reject_method_group(c, vg);
                }
                return NULL;
            }
        }
    }
    return best ? best : checker_find_method_call(type_sym, name, argc, type_arg_count);
}

static void check_call_arity(zan_checker_t *c, zan_ast_node_t *call,
                             zan_type_t *recv) {
    zan_ast_node_t *callee = call->call.callee;
    if (!callee || callee->kind != AST_MEMBER_ACCESS) return;
    if (!recv || !recv->sym) return;
    zan_istr_t name = callee->member.name;
    int argc = call->call.args.count;
    zan_compile_trace("arity? %.*s.%.*s argc=%d members=%d",
                    (int)recv->sym->name.len, recv->sym->name.str,
                    (int)name.len, name.str, argc, recv->sym->member_count);
    int candidates = 0, want_min = 0, want_max = 0;
    for (zan_symbol_t *s = recv->sym; s;
         s = (s->type && s->type->base_type) ? s->type->base_type->sym : NULL) {
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            int lo, hi;
            if (!m || m->kind != SYM_METHOD || m->name.len != name.len ||
                memcmp(m->name.str, name.str, (size_t)name.len) != 0)
                continue;
            if (!method_arity(m, &lo, &hi)) {
                if (candidates == 0 && m->decl->method_decl.is_variadic &&
                    argc < m->decl->method_decl.params.count) {
                    int floor = m->decl->method_decl.params.count;
                    zan_diag_emit_code(c->diag, DIAG_ERROR, call->loc, 2028,
                                  "'%.*s.%.*s' takes at least %d argument%s, "
                                  "but %d given",
                                  (int)recv->sym->name.len, recv->sym->name.str,
                                  (int)name.len, name.str, floor,
                                  floor == 1 ? "" : "s", argc);
                }
                return;
            }
            if (argc >= lo && argc <= hi) return;
            if (!candidates) { want_min = lo; want_max = hi; }
            candidates++;
        }
    }
    if (!candidates) return;
    /* 成员组无适用候选时扩展方法仍是候选（C# 同款）：名字匹配、接收
     * 者可赋给扩展的 this 形参、实参数（含 this）落在其元数区间内——
     * 命中则交给主解析，不在此抢报（ICodeFirst.SyncStructure(Meta)
     * 遮蔽 SyncStructure<T>() 扩展的 E2030 由此而来）。 */
    for (int ei = 0; ei < c->binder->ext_method_count; ei++) {
        zan_symbol_t *em = c->binder->ext_methods[ei].method;
        if (!em || em->kind != SYM_METHOD || em->name.len != name.len ||
            memcmp(em->name.str, name.str, (size_t)name.len) != 0) continue;
        zan_type_t *ert = c->binder->ext_methods[ei].recv_type;
        int asn = (ert && ert->sym) ? checker_type_assignable(ert, recv) : -1;
        int elo = -1, ehi = -1;
        int ar = method_arity(em, &elo, &ehi);
        if (!ar && em->decl && em->decl->kind == AST_METHOD_DECL &&
            em->decl->method_decl.params.count > 0) {
            /* 泛型扩展（SyncStructure<T>(this IDbExecutor)）的 method_arity
             * 失算：扩展没有变参，参数个数即元数。 */
            elo = ehi = em->decl->method_decl.params.count;
            ar = 1;
        }
        if (ert && ert->sym && recv->sym != ert->sym && asn == 0) continue;
        if (!ar) continue;
        if (argc + 1 >= elo && argc + 1 <= ehi) return;
    }
    if (candidates > 1) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, call->loc, 2029,
                      "no overload of '%.*s.%.*s' takes %d argument%s",
                      (int)recv->sym->name.len, recv->sym->name.str,
                      (int)name.len, name.str, argc, argc == 1 ? "" : "s");
        return;
    }
    if (want_min == want_max) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, call->loc, 2030,
                      "'%.*s.%.*s' takes %d argument%s, but %d given",
                      (int)recv->sym->name.len, recv->sym->name.str,
                      (int)name.len, name.str, want_min,
                      want_min == 1 ? "" : "s", argc);
        return;
    }
    zan_diag_emit_code(c->diag, DIAG_ERROR, call->loc, 2031,
                  "'%.*s.%.*s' takes %d to %d arguments, but %d given",
                  (int)recv->sym->name.len, recv->sym->name.str,
                  (int)name.len, name.str, want_min, want_max, argc);
}

static bool type_is_scalar_register(zan_type_t *t) {
    return t && (type_is_numeric(t) || t->kind == TYPE_BOOL ||
                 t->kind == TYPE_CHAR || t->kind == TYPE_ENUM ||
                 t->kind == TYPE_NINT);
}

/* 检查是否an argument of type `value` cannot reach a parameter of type `target` */
static bool type_refs_type_param_d(zan_type_t *t, int depth) {
    if (!t || depth > 4) return false;
    if (t->kind == TYPE_TYPE_PARAM) return true;
    if (t->element_type && type_refs_type_param_d(t->element_type, depth + 1))
        return true;
    if (t->delegate_ret_type && type_refs_type_param_d(t->delegate_ret_type, depth + 1))
        return true;
    for (int i = 0; i < t->type_arg_count && t->type_args; i++)
        if (type_refs_type_param_d(t->type_args[i], depth + 1)) return true;
    return false;
}
static bool type_refs_type_param(zan_type_t *t) { return type_refs_type_param_d(t, 0); }

static bool checker_arg_type_mismatch(zan_checker_t *c, zan_type_t *target,
                                      zan_type_t *value) {
    if (!target || !value) return false;
    if (target->kind == TYPE_ERROR || value->kind == TYPE_ERROR) return false;
    if (type_refs_type_param(target) || type_refs_type_param(value)) return false;
    if (type_is_scalar_register(value)) {
        bool ref_target = target->kind == TYPE_OBJECT ||
                          target->kind == TYPE_INTERFACE ||
                          target->kind == TYPE_DELEGATE ||
                          target->kind == TYPE_ARRAY;
        if (!ref_target) return false;
        if (checker_type_assignable(target, value)) return false;
        return !checker_has_user_conversion(c, target, value);
    }
    bool ref_target = checker_type_is_ref(target) || target->kind == TYPE_STRUCT;
    bool ref_value = checker_type_is_ref(value) || value->kind == TYPE_STRUCT;
    if (ref_target && ref_value) {
        if (checker_type_assignable(target, value)) return false;
        return !checker_has_user_conversion(c, target, value);
    }
    return false;
}

static zan_symbol_t *unique_named_method(zan_symbol_t *type_sym,
                                         zan_istr_t name) {
    zan_symbol_t *only = NULL;
    for (zan_symbol_t *s = type_sym; s;
         s = (s->type && s->type->base_type) ? s->type->base_type->sym : NULL) {
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            if (!m || m->kind != SYM_METHOD || m->name.len != name.len ||
                memcmp(m->name.str, name.str, (size_t)name.len) != 0)
                continue;
            if (only && only != m) return NULL;
            only = m;
        }
    }
    return only;
}

static zan_symbol_t *call_arg_signature(zan_checker_t *c, zan_ast_node_t *call,
                                        zan_type_t *recv) {
    zan_ast_node_t *callee = call->call.callee;
    if (!callee) return NULL;
    zan_symbol_t *only = NULL;
    if (callee->kind == AST_MEMBER_ACCESS) {
        if (!recv || !recv->sym || recv->type_arg_count) return NULL;
        only = unique_named_method(recv->sym, callee->member.name);
    } else if (callee->kind == AST_IDENTIFIER) {
        if (!c->current_type_sym) return NULL;
        if (c->current_type_sym->type &&
            c->current_type_sym->type->type_arg_count) return NULL;
        zan_symbol_t *shadow = zan_binder_lookup(c->binder, callee->ident.name);
        if (shadow && shadow->kind != SYM_METHOD) return NULL;
        only = unique_named_method(c->current_type_sym, callee->ident.name);
    } else {
        return NULL;
    }
    if (!only || !only->decl || only->decl->kind != AST_METHOD_DECL) return NULL;
    if (only->decl->method_decl.type_params.count ||
        call->call.type_args.count ||
        !method_accepts_argc(only, call->call.args.count)) return NULL;
    zan_ast_list_t *ps = &only->decl->method_decl.params;
    if (call->call.args.count > ps->count) return NULL;
    for (int i = 0; i < ps->count; i++) {
        zan_ast_node_t *p = ps->items[i];
        if (!p || p->kind != AST_PARAM || p->param.is_params || p->param.is_this)
            return NULL;
    }
    for (int i = 0; i < call->call.args.count; i++) {
        zan_ast_node_t *a = call->call.args.items[i];
        if (!a || a->kind == AST_NAMED_ARG || a->kind == AST_REF_ARG) return NULL;
    }
    return only;
}

static bool arg_is_spawn_callee(zan_ast_node_t *call) {
    if (!call || call->call.callee == NULL ||
        call->call.callee->kind != AST_MEMBER_ACCESS ||
        call->call.args.count != 1) return false;
    zan_istr_t n = call->call.callee->member.name;
    if (n.len == 5 && memcmp(n.str, "Spawn", 5) == 0) return true;
    if (n.len == 3 && memcmp(n.str, "Run", 3) == 0) return true;
    return false;
}

/* 内建泛型容器的**形参类型** → 具体类型。
 *
 * 内建泛型（`List<T>` / `Dict<K,V>`）的成员签名写在 `builtin_api.c` 的
 * 表里，形参类型是**类型参数名**（"T" / "K" / "V"）。要检查实参类型，
 * 得先把 "T" 换成调用点的**类型实参**（`List<int>` 的 int）。
 *
 * 映射规则：`List<T>` 的 T → type_args[0]；`Dict<K,V>` 的 K → type_args[0]、
 * V → type_args[1]。其余名字（"int" / "string" / …）按内建基本类型解析。
 *
 * 返回 NULL 表示"这个形参类型我管不了"（未知名字、非泛型容器）——调用
 * 方放行，不报错。宁可漏检也不误报。
 */
static zan_type_t *checker_builtin_param_type(zan_checker_t *c,
                                               zan_type_t *recv,
                                               const char *name) {
    if (!recv || !name || !name[0]) return NULL;
    /* 泛型容器：先按类型实参替换 T/K/V。 */
    if (recv->type_arg_count > 0) {
        /* List<T> / Span<T> / Task<T>：T 是第 0 个实参。
         * Dict<K,V>：K 是第 0 个，V 是第 1 个。 */
        int slot = -1;
        if (strcmp(name, "T") == 0) slot = 0;
        else if (strcmp(name, "K") == 0) slot = 0;
        else if (strcmp(name, "V") == 0) slot = 1;
        if (slot >= 0) {
            if (slot >= recv->type_arg_count) return NULL;
            return recv->type_args[slot];
        }
    }
    /* 非泛型的基本类型。 */
    zan_binder_t *b = c->binder;
    if (strcmp(name, "int") == 0) return b->type_int;
    if (strcmp(name, "long") == 0) return b->type_long;
    if (strcmp(name, "short") == 0) return b->type_int;
    if (strcmp(name, "byte") == 0) return b->type_byte;
    if (strcmp(name, "sbyte") == 0) return b->type_sbyte;
    if (strcmp(name, "bool") == 0) return b->type_bool;
    if (strcmp(name, "float") == 0) return b->type_float;
    if (strcmp(name, "double") == 0) return b->type_double;
    if (strcmp(name, "decimal") == 0) return b->type_decimal;
    if (strcmp(name, "char") == 0) return b->type_char;
    if (strcmp(name, "string") == 0) return b->type_string;
    if (strcmp(name, "object") == 0) return b->type_object;
    if (strcmp(name, "nint") == 0) return b->type_nint;
    zan_istr_t in = {(char *)name, (int)strlen(name)};
    zan_symbol_t *sym = zan_binder_lookup(b, in);
    if (sym && (sym->kind == SYM_CLASS || sym->kind == SYM_STRUCT ||
                sym->kind == SYM_ENUM || sym->kind == SYM_INTERFACE))
        return sym->type;
    return NULL;
}

/* 检查**内建泛型**容器方法调用的实参类型。
 *
 * 缺口（修之前）：内建泛型（`List<T>` / `Dict<K,V>`）的成员**完全没有
 * 类型检查**——`List<int>.Add("字符串")` 编译通过，运行时把字符串指针
 * 存进 int 列表，读回来是垃圾指针（不是崩，是"看起来能用"的静默损坏）。
 *
 * 根因：`List` 没有 `sym`（成员来自 `builtin_api.c` 的表，不是源码
 * 声明），所以 `call_arg_signature` 拿不到签名，`check_call_arg_type`
 * 从没被调用过——内建泛型的方法调用**根本没进实参类型检查这条路**。
 *
 * 修法：把内建成员的形参类型从签名串里取出来，按接收者的**类型实参**
 * 替换 T/K/V，然后走同一套 `checker_arg_type_mismatch`。
 */
/* 检查**用户泛型类**方法调用的实参类型。
 *
 * 法型参数是**本身**的型参\uff08Box<T> → Box<int> → T = int\uff09，
 * 很多人会写成 `box.value = "x"`\uff08\u5b57\u6bb5代��\uff09——
 * 但代入检查过不化结果\u3002
 *
 * 为何么会报错\uff1a\u2014\u2014当调用时 `call_arg_signature` 回 NULL\uff08像码按不到\u2014\u2014
 * 有 type_arg_count 的收员\u55�直接返回\uff09\uff0c实参类型仍来没被检查\u3002
 * 所以这这条路又故自己\u3002
 */
static void checker_user_generic_call_args(zan_checker_t *c,
                                            zan_type_t *recv,
                                            zan_istr_t name,
                                            zan_ast_node_t *call,
                                            zan_type_t **arg_types) {
    if (!recv || !recv->sym || !recv->sym->decl) return;
    zan_ast_node_t *decl = recv->sym->decl;
    if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL)
        return;
    zan_ast_list_t *tps = &decl->type_decl.type_params;
    if (tps->count == 0 || tps->count != recv->type_arg_count) return;
    /* 找到唯一个名字在当前类上的方法\uff08有参数\u2014\u2014\u2014\u2014\u2014\u2014\u2014\u2014\u2014\u2014\uff09\u3002 */
    zan_symbol_t *sig = unique_named_method(recv->sym, name);
    if (!sig || !sig->decl || sig->decl->kind != AST_METHOD_DECL) return;
    if (sig->decl->method_decl.type_params.count) return;
    zan_ast_list_t *ps = &sig->decl->method_decl.params;
    int argc = call->call.args.count;
    for (int i = 0; i < argc && i < ps->count; i++) {
        zan_ast_node_t *arg = call->call.args.items[i];
        if (arg && arg->kind == AST_NAMED_ARG) arg = arg->named_arg.expr;
        if (!arg || arg->kind == AST_REF_ARG) continue;
        zan_ast_node_t *p = ps->items[i];
        if (!p || p->kind != AST_PARAM) continue;
        zan_type_t *pt = zan_binder_resolve_type(c->binder, p->param.type);
        if (!pt) continue;
        /* 把当前类的类型实参替\u2014\u2014T → int\u3002 */
        pt = zan_binder_subst_named(c->binder, pt, tps, recv->type_args);
        zan_type_t *at = arg_types[i];
        if (!at) continue;
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

