static int type_family(zan_type_t *t) {
    if (!t) return FAM_UNKNOWN;
    switch (t->kind) {
    case TYPE_BOOL: return FAM_BOOL;
    case TYPE_BYTE: case TYPE_SHORT: case TYPE_INT: case TYPE_LONG:
    case TYPE_SBYTE: case TYPE_USHORT: case TYPE_UINT: case TYPE_ULONG:
    case TYPE_CHAR: case TYPE_ENUM:
        return FAM_INT;
    case TYPE_FLOAT: case TYPE_DOUBLE: return FAM_FLOAT;
    case TYPE_DECIMAL: return FAM_DECIMAL;
    case TYPE_STRING: return FAM_STRING;
    case TYPE_CLASS: case TYPE_INTERFACE: case TYPE_ARRAY: case TYPE_OBJECT: return FAM_REF;
    default: return FAM_UNKNOWN;
    }
}

static bool str_and_byte_buffer(zan_type_t *s, zan_type_t *b) {
    if (!s || !b || s->kind != TYPE_STRING || b->kind != TYPE_ARRAY) return false;
    zan_type_t *e = b->element_type;
    return e && (e->kind == TYPE_BYTE || e->kind == TYPE_SBYTE ||
                 e->kind == TYPE_CHAR);
}

static void stmt_collect_return_types(zan_irgen_t *g, zan_ast_node_t *stmt,
                                      local_scope_t *locals,
                                      zan_type_t **found, int *mixed) {
    if (!stmt || *mixed) return;
    switch (stmt->kind) {
    case AST_RETURN_STMT:
        if (!stmt->ret.value) { *mixed = 1; return; } /* `return;` is void */
        {
            zan_type_t *t = infer_expr_type(g, stmt->ret.value, locals);
            if (!t) { *mixed = 1; return; }
            if (!*found) { *found = t; return; }
            if (!types_concrete_equal(*found, t)) *mixed = 1;
        }
        return;
    case AST_BLOCK:
        for (int i = 0; i < stmt->block.stmts.count; i++)
            stmt_collect_return_types(g, stmt->block.stmts.items[i],
                                      locals, found, mixed);
        return;
    case AST_IF_STMT:
        stmt_collect_return_types(g, stmt->if_stmt.then_body, locals, found, mixed);
        if (stmt->if_stmt.else_body)
            stmt_collect_return_types(g, stmt->if_stmt.else_body, locals,
                                      found, mixed);
        return;
    case AST_VAR_DECL:
        if (stmt->var_decl.initializer) {
            zan_type_t *t = infer_expr_type(g, stmt->var_decl.initializer, locals);
            if (t) local_add(locals, stmt->var_decl.name, NULL, t);
        }
        return;
    case AST_LAMBDA:
        return;
    case AST_WHILE_STMT: case AST_DO_WHILE_STMT: case AST_FOR_STMT:
    case AST_FOREACH_STMT: case AST_SWITCH_STMT: case AST_TRY_STMT:
        *mixed = 1;
        return;
    default:
        return;
    }
}

static zan_type_t *stmt_lambda_return_type(zan_irgen_t *g, zan_ast_node_t *body,
                                           local_scope_t *locals) {
    if (!body || body->kind != AST_BLOCK) return NULL;
    zan_type_t *found = NULL;
    int mixed = 0;
    stmt_collect_return_types(g, body, locals, &found, &mixed);
    return mixed ? NULL : found;
}

static zan_type_t *lambda_body_type(zan_irgen_t *g, zan_ast_node_t *lam,
                                    zan_type_t *dt, local_scope_t *locals) {
    if (!lam || !dt || !locals) return NULL;
    zan_ast_node_t *body = lam->lambda.body;
    if (!body) return NULL;
    int mark = locals->count;
    for (int k = 0; k < lam->lambda.params.count; k++) {
        zan_ast_node_t *p = lam->lambda.params.items[k];
        zan_type_t *pt = k < dt->delegate_param_count
            ? dt->delegate_param_types[k] : NULL;
        local_add(locals, p->param.name, NULL, pt);
    }
    zan_type_t *bt = (body->kind == AST_BLOCK)
        ? stmt_lambda_return_type(g, body, locals)
        : infer_expr_type(g, body, locals);
    locals->count = mark;
    return bt;
}

static zan_symbol_t *arg_method_group(zan_irgen_t *g, zan_ast_node_t *a,
                                      local_scope_t *locals, int arity) {
    if (!a) return NULL;
    if (a->kind == AST_IDENTIFIER) {
        /* 裸方法名（`form.On("x", OnFocus)`）：与发码侧标识符兜底
         * （irgen_expr_ops 的 get_method_sym）同一口径——局部先遮蔽、
         * 类型名优先（R2.Table 的 R2 是类不是方法组）、字段再遮蔽，
         * 最后在当前类型链上找方法。不识别这一形态时打分循环拿它
         * 当普通表达式，全部候选 0 分，重载退化成"第一个同名"
         * （B-ID48：On 四重载错绑 Action，运行期形参腐坏）。 */
        if (locals && local_find(locals, a->ident.name)) return NULL;
        zan_symbol_t *ts = zan_binder_lookup(g->binder, a->ident.name);
        if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT))
            return NULL;
        if (!g->current_type_sym) return NULL;
        if (get_field_sym(g->current_type_sym, a->ident.name)) return NULL;
        zan_symbol_t *bm = get_method_sym(g->current_type_sym, a->ident.name);
        if (!bm || !bm->decl || bm->decl->kind != AST_METHOD_DECL) return NULL;
        return bm;
    }
    if (a->kind != AST_MEMBER_ACCESS) return NULL;
    zan_ast_node_t *obj = a->member.object;
    if (!obj || obj->kind != AST_IDENTIFIER) return NULL;
    if (locals && local_find(locals, obj->ident.name)) return NULL;
    zan_symbol_t *cs = zan_binder_lookup(g->binder, obj->ident.name);
    if (!cs || (cs->kind != SYM_CLASS && cs->kind != SYM_STRUCT)) return NULL;
    if (get_field_sym(cs, a->member.name)) return NULL;
    zan_symbol_t *m = arity >= 0 ? resolve_overload(cs, a->member.name, arity, 0)
                                 : NULL;
    if (!m) m = get_method_sym(cs, a->member.name);
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) return NULL;
    return m;
}

static int method_group_score(zan_irgen_t *g, zan_symbol_t *mg, zan_type_t *pt) {
    if (!pt || pt->kind != TYPE_DELEGATE) return -1;
    if (mg->decl->method_decl.params.count != pt->delegate_param_count) return -1;
    /* 委托形参族必须与方法形参族一致：不查这一层，`OnFocus(Control)`
     * 会在 Action<string> 与 ControlEvent 之间打出平分，第一个声明
     * 的错重载照样赢（B-ID48）。 */
    for (int i = 0; i < pt->delegate_param_count; i++) {
        zan_type_t *dpt = pt->delegate_param_types[i];
        zan_ast_node_t *pn = mg->decl->method_decl.params.items[i];
        zan_type_t *mpt = (pn && pn->kind == AST_PARAM && pn->param.type)
            ? zan_binder_resolve_type(g->binder, pn->param.type) : NULL;
        if (!dpt || !mpt) return -1;
        if (!type_mentions_tp(dpt) && !type_mentions_tp(mpt) &&
            type_family(dpt) != type_family(mpt))
            return -1;
    }
    int score = 2;
    zan_type_t *rt = mg->decl->method_decl.return_type
        ? zan_binder_resolve_type(g->binder, mg->decl->method_decl.return_type)
        : NULL;
    zan_type_t *dr = pt->delegate_ret_type;
    if (rt && dr && !type_mentions_tp(rt) && !type_mentions_tp(dr)) {
        if (types_concrete_equal(rt, dr)) score += 2;
        else if (type_family(rt) != type_family(dr) &&
                 type_family(rt) != FAM_UNKNOWN &&
                 type_family(dr) != FAM_UNKNOWN)
            return -1;
    } else if (rt && !dr) {
        /* 委托无返回类型槽而方法是 void：视作 void==void 贴合（Action
         * 系委托 dr 为空时的等价判定） */
        if (rt->kind == TYPE_VOID) score += 2;
    }
    return score;
}

struct zan_ctor_entry *find_ctor(zan_irgen_t *g, zan_symbol_t *type_sym,
                                        zan_ast_list_t *args, local_scope_t *locals,
                                        zan_ast_node_t *exclude);
bool implicit_ctor_for_arg(zan_irgen_t *g, zan_type_t *target,
                                  zan_type_t *source, zan_ast_node_t *arg,
                                  local_scope_t *locals);
bool class_implements_iface(zan_symbol_t *cls, zan_symbol_t *iface);
bool class_derives_from(zan_symbol_t *derived, zan_symbol_t *base) {
    if (!derived || !base) return false;
    for (zan_symbol_t *cur = derived; cur && cur->type && cur->type->base_type;
         cur = cur->type->base_type->sym) {
        if (cur->type->base_type->sym == base) return true;
    }
    return false;
}
bool has_declared_ctor(zan_irgen_t *g, zan_symbol_t *sym) {
    if (!sym) return false;
    for (int i = 0; i < g->ctor_count; i++) {
        if (g->ctors[i].type_sym == sym) return true;
    }
    return false;
}

struct zan_ctor_entry *find_ctor(zan_irgen_t *g, zan_symbol_t *type_sym,
                                        zan_ast_list_t *args, local_scope_t *locals,
                                        zan_ast_node_t *exclude) {
    struct zan_ctor_entry *best = NULL;
    int best_score = -1;
    int argc = args ? args->count : 0;
    for (int i = 0; i < g->ctor_count; i++) {
        struct zan_ctor_entry *entry = &g->ctors[i];
        if (entry->type_sym != type_sym || entry->param_count != argc ||
            entry->decl == exclude)
            continue;
        int score = 0;
        bool compatible = true;
        if (entry->decl && locals) {
            for (int j = 0; j < argc; j++) {
                zan_ast_node_t *arg = args->items[j];
                if (arg && arg->kind == AST_NAMED_ARG) {
                    int target = -1;
                    for (int q = 0; q < entry->decl->method_decl.params.count; q++) {
                        zan_ast_node_t *pp = entry->decl->method_decl.params.items[q];
                        if (pp && pp->kind == AST_PARAM &&
                            pp->param.name.len == arg->named_arg.name.len &&
                            memcmp(pp->param.name.str, arg->named_arg.name.str,
                                   (size_t)arg->named_arg.name.len) == 0) {
                            target = q;
                            break;
                        }
                    }
                    if (target < 0) {
                        compatible = false;
                        break;
                    }
                    if (target < argc && args->items[target] &&
                        args->items[target]->kind != AST_NAMED_ARG) {
                        compatible = false;
                        break;
                    }
                    arg = arg->named_arg.expr;
                    zan_type_t *nt = zan_binder_resolve_type(g->binder,
                        entry->decl->method_decl.params.items[target]->param.type);
                    zan_type_t *nat = infer_expr_type(g, arg, locals);
                    if (nt && nat && types_concrete_equal(nt, nat)) score += 4;
                    continue;
                }
                zan_ast_node_t *param = entry->decl->method_decl.params.items[j];
                zan_type_t *pt = zan_binder_resolve_type(g->binder, param->param.type);
                if (pt && args->items[j] && args->items[j]->kind == AST_LAMBDA) {
                    zan_ast_node_t *lam = args->items[j];
                    if (pt->kind != TYPE_DELEGATE ||
                        pt->delegate_param_count != lam->lambda.params.count) {
                        compatible = false;
                        break;
                    }
                    score += 2;
                    zan_type_t *bt = lambda_body_type(g, lam, pt, locals);
                    if (bt && pt->delegate_ret_type &&
                        types_concrete_equal(bt, pt->delegate_ret_type))
                        score += 2;
                    continue;
                }
                zan_symbol_t *mg = arg_method_group(
                    g, args->items[j], locals,
                    (pt && pt->kind == TYPE_DELEGATE)
                        ? pt->delegate_param_count : -1);
                if (mg) {
                    int ms = method_group_score(g, mg, pt);
                    if (ms < 0) { compatible = false; break; }
                    score += ms;
                    continue;
                }
                zan_type_t *at = infer_expr_type(g, args->items[j], locals);
                if (pt && at && type_mentions_tp(pt) && !type_mentions_tp(at)) {
                    if (types_match_modulo_tp(pt, at)) score += 3;
                    continue;
                }
                if (!pt || !at || type_mentions_tp(pt) || type_mentions_tp(at))
                    continue;
                if (types_concrete_equal(pt, at)) {
                    score += 4;
                    continue;
                }
                int pf = type_family(pt), af = type_family(at);
                if (pt->kind == TYPE_OBJECT && (af == FAM_REF || af == FAM_STRING)) {
                    score += 1;
                    continue;
                }
                if (pf == af && pf != FAM_UNKNOWN) {
                    if (pf == FAM_REF) {
                        if (pt->kind == TYPE_OBJECT) {
                            score += 1;
                            continue;
                        }
                        if (pt->kind == TYPE_INTERFACE) {
                            if (at->kind == TYPE_CLASS && at->sym && pt->sym) {
                                if (!class_implements_iface(at->sym, pt->sym)) {
                                    compatible = false;
                                    break;
                                }
                            }
                        } else if (pt->kind == TYPE_CLASS && at->kind == TYPE_CLASS) {
                            if (pt->sym && at->sym && pt->sym != at->sym &&
                                !class_derives_from(at->sym, pt->sym)) {
                                compatible = false;
                                break;
                            }
                        }
                    }
                    score += 2;
                    continue;
                }
                if (implicit_ctor_for_arg(g, pt, at, args->items[j], locals)) {
                    score += 1;
                    continue;
                }
                if (pf == FAM_FLOAT && af == FAM_INT)
                    continue;
                if (str_and_byte_buffer(pt, at) || str_and_byte_buffer(at, pt))
                    continue;
                compatible = false;
                break;
            }
        }
        if (compatible && score > best_score) {
            best = entry;
            best_score = score;
        }
    }
    return best;
}
bool is_implicit_ctor_source(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BOOL:
    case TYPE_SBYTE: case TYPE_BYTE: case TYPE_SHORT: case TYPE_USHORT:
    case TYPE_INT: case TYPE_UINT: case TYPE_LONG: case TYPE_ULONG:
    case TYPE_NINT: case TYPE_CHAR: case TYPE_ENUM:
        return true;
    default:
        return false;
    }
}
bool implicit_ctor_for_arg(zan_irgen_t *g, zan_type_t *target,
                                  zan_type_t *source, zan_ast_node_t *arg,
                                  local_scope_t *locals) {
    if (!target || target->kind != TYPE_CLASS || !target->sym ||
        !is_implicit_ctor_source(source) || !arg || !locals)
        return false;
    zan_ast_node_t *items[1] = { arg };
    zan_ast_list_t one;
    one.items = items;
    one.count = 1;
    one.capacity = 1;
    return find_ctor(g, target->sym, &one, locals, NULL) != NULL;
}
bool fill_ctor_default_args(zan_irgen_t *g, zan_symbol_t *type_sym,
                                   const zan_ast_list_t *args,
                                   zan_ast_list_t *out) {
    int argc = args ? args->count : 0;
    for (int i = 0; i < g->ctor_count; i++) {
        struct zan_ctor_entry *entry = &g->ctors[i];
        if (entry->type_sym != type_sym || entry->param_count <= argc ||
            !entry->decl) continue;
        zan_ast_list_t *ps = &entry->decl->method_decl.params;
        bool defaulted = true;
        for (int k = argc; k < ps->count; k++) {
            zan_ast_node_t *p = ps->items[k];
            if (!p || p->kind != AST_PARAM || !p->param.default_val) {
                defaulted = false;
                break;
            }
        }
        if (!defaulted) continue;
        zan_ast_list_init(out);
        for (int k = 0; k < argc; k++)
            zan_ast_list_push(out, args->items[k], g->arena);
        for (int k = argc; k < ps->count; k++)
            zan_ast_list_push(out, ps->items[k]->param.default_val, g->arena);
        return true;
    }
    return false;
}
int expr_family(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    if (!e) return FAM_UNKNOWN;
    switch (e->kind) {
    case AST_INT_LITERAL: case AST_CHAR_LITERAL: return FAM_INT;
    case AST_FLOAT_LITERAL: return FAM_FLOAT;
    case AST_DECIMAL_LITERAL: return FAM_DECIMAL;
    case AST_STRING_LITERAL: case AST_STRING_INTERP: return FAM_STRING;
    case AST_BOOL_LITERAL: return FAM_BOOL;
    case AST_UNARY:
        if (e->unary.op == TK_BANG) return FAM_BOOL;
        return expr_family(g, e->unary.operand, locals);
    case AST_POSTFIX_UNARY:
        return expr_family(g, e->unary.operand, locals);
    case AST_CONDITIONAL: {
        /* 两支家族合并：一边未知取另一边；数值家族按提升阶梯统一
         * （float/double、decimal 赢整数）——与 checker 的
         * merge_conditional_types 同口径。曾按 then 臂定型：
         * (t?1:2.0)/2 因此做整数除法得 0，C# 是 0.5（TASKS.md A514）。 */
        int tf = expr_family(g, e->conditional.then_expr, locals);
        int ef = expr_family(g, e->conditional.else_expr, locals);
        if (tf == FAM_UNKNOWN) return ef;
        if (ef == FAM_UNKNOWN) return tf;
        if (tf == ef) return tf;
        if ((tf == FAM_INT && ef == FAM_FLOAT) ||
            (tf == FAM_FLOAT && ef == FAM_INT))
            return FAM_FLOAT;
        if ((tf == FAM_INT && ef == FAM_DECIMAL) ||
            (tf == FAM_DECIMAL && ef == FAM_INT) ||
            (tf == FAM_FLOAT && ef == FAM_DECIMAL) ||
            (tf == FAM_DECIMAL && ef == FAM_FLOAT))
            return FAM_DECIMAL;
        /* 其余混合（STRING/REF/BOOL 之间）维持 then 臂——与旧行为一致 */
        return tf;
    }
    case AST_BINARY:
        switch (e->binary.op) {
        case TK_EQ_EQ: case TK_BANG_EQ: case TK_LESS: case TK_GREATER:
        case TK_LESS_EQ: case TK_GREATER_EQ: case TK_AMP_AMP: case TK_PIPE_PIPE:
            return FAM_BOOL;
        case TK_PLUS: case TK_MINUS: case TK_STAR: case TK_SLASH:
        case TK_PERCENT: {
            int lf = expr_family(g, e->binary.left, locals);
            int rf = expr_family(g, e->binary.right, locals);
            if (lf == FAM_STRING || rf == FAM_STRING) return FAM_STRING;
            if (lf == FAM_DECIMAL || rf == FAM_DECIMAL) return FAM_DECIMAL;
            if (lf == FAM_FLOAT || rf == FAM_FLOAT) return FAM_FLOAT;
            if (lf != FAM_UNKNOWN) return lf;
            return rf;
        }
        default:
            return FAM_UNKNOWN;
        }
    default:
        return type_family(infer_expr_type(g, e, locals));
    }
}

static int concrete_arg_score(zan_irgen_t *g, zan_type_t *pt,
                              zan_ast_node_t *a, local_scope_t *locals) {
    /* null 字面量实参：推断不出静态类型，只能按"接不接得住 null"判——
     * 与 checker 共用同一条规则（zan_null_arg_score）。
     *
     * 踩过的坑：`Arg.Text(null)` 以前走到 `Text(int)`，null 被当成 0 传
     * 下去，调用点毫无异常，断言却永远对不上——因为这里对 null 一律给
     * 0 分，等于所有重载同分，第一个（值类型）就赢了。 */
    if (a && a->kind == AST_NULL_LITERAL) {
        int ns = zan_null_arg_score(pt);
        return ns < 0 ? -1 : ns;
    }
    zan_type_t *at = infer_expr_type(g, a, locals);
    if (at && !type_mentions_tp(at)) {
        if (types_concrete_equal(pt, at)) return 4;
        int pf = type_family(pt), af = type_family(at);
        if (pt->kind == TYPE_OBJECT && (af == FAM_REF || af == FAM_STRING)) return 1;
        if (pf == FAM_UNKNOWN || af == FAM_UNKNOWN) return 0;
        if (pf == af) return 2;
        if (implicit_ctor_for_arg(g, pt, at, a, locals)) return 1;
        if (pf == FAM_FLOAT && af == FAM_INT) return 0;
        if (pf == FAM_DECIMAL && af == FAM_INT) return 1;
        if (str_and_byte_buffer(pt, at) || str_and_byte_buffer(at, pt)) return 0;
        return -1;
    }
    int af = expr_family(g, a, locals);
    int pf = type_family(pt);
    if (af == FAM_UNKNOWN || pf == FAM_UNKNOWN) return 0;
    if (af == pf) return 2;
    if (pf == FAM_FLOAT && af == FAM_INT) return 0;
    if (pf == FAM_DECIMAL && af == FAM_INT) return 1;
    return -1;
}

/* B-ID65 只读表达式树：树目标类型判定。实现在 irgen_expr_lambda.c
 * （同一编译单元，irgen.c 先包含本文件后包含它，这里前置声明）。 */
bool irgen_type_is_tree_target(zan_type_t *t);

static int method_args_score(zan_irgen_t *g, zan_symbol_t *m,
                             zan_ast_node_t *call, zan_ast_node_t *recv_expr,
                             local_scope_t *locals, int p0) {
    zan_ast_list_t *ps = &m->decl->method_decl.params;
    int score = 0;
    if (!call || call->kind != AST_CALL || !locals) return score;
    for (int j = p0; j < ps->count; j++) {
        int ai = j - p0;
        if (ai >= call->call.args.count) break;
        if (ps->items[j]->kind == AST_PARAM && ps->items[j]->param.is_params) {
            zan_type_t *bundle = method_param_type(g, m, j);
            if (!bundle || bundle->kind != TYPE_ARRAY ||
                !bundle->element_type || type_mentions_tp(bundle))
                break;
            for (; ai < call->call.args.count; ai++) {
                zan_ast_node_t *ta = call->call.args.items[ai];
                if (!ta || ta->kind == AST_NAMED_ARG || ta->kind == AST_LAMBDA)
                    continue;
                zan_type_t *ta_ty = infer_expr_type(g, ta, locals);
                if (ta_ty && types_concrete_equal(bundle, ta_ty)) {
                    score += 4;
                    continue;
                }
                int es = concrete_arg_score(g, bundle->element_type, ta, locals);
                if (es < 0) return -1;
                score += es;
            }
            break;
        }
        zan_ast_node_t *a = call->call.args.items[ai];
        if (!a) continue;
        if (a->kind == AST_NAMED_ARG) {
            continue;
        }
        /* lambda 实参（也包括三元表达式里的一支是 lambda 的形状）：lambda
         * 没有自己的类型——类型由目标委托形参决定。所以打分只能按形参的
         * 委托类型来，不能按实参自身的类型（推不出来）。
         *
         * 形参不是委托 → 判死（-1）。不判死的话这个候选与正确候选同分，
         * 重载决议退化成「取成员表里第一个同名方法」：IDE 里
         * `LspSession.StartAsync(app != null ? (Action a) => ... : null)`
         * 就绑到了 App 那个重载上，无限自我递归，一开工程就 exit(1)。 */
        zan_ast_node_t *lam = zan_ast_lambda_source(a);
        if (lam) {
            zan_type_t *dp = method_param_type_at(g, m, j, call, recv_expr, locals);
            if (!dp) continue;                        /* 判不出来：宽容放过 */
            if (dp->kind == TYPE_TYPE_PARAM || type_mentions_tp(dp)) continue;
            /* B-ID65：ExprNode / Expr<T> 形参收树 lambda。显式树形参 +
             * 表达式体单参 lambda → +8 压过 Func 候选；其余 lambda 对
             * 树形参一律判死——无类型 lambda 继续走 Func（内存过滤，
             * 存量行为零变化），块体不可树译 fail-closed。门面链的过载
             * 决议在 checker 不可达（接收者类型降级），这里的打分是
             * 此类调用唯一的决议点；checker 侧同名分支
             * （checker_find_method_typed）只服务接收者类型完好的调用。 */
            if (irgen_type_is_tree_target(dp)) {
                zan_ast_node_t *lp0 = (lam->lambda.params.count == 1)
                    ? lam->lambda.params.items[0] : NULL;
                if (lam->lambda.body && lam->lambda.body->kind != AST_BLOCK &&
                    lp0 && lp0->kind == AST_PARAM && lp0->param.type &&
                    irgen_type_is_tree_target(
                        resolve_type_ctx(g, lp0->param.type))) {
                    score += 8;
                    continue;
                }
                return -1;
            }
            if (dp->kind != TYPE_DELEGATE) return -1; /* lambda 只能转委托 */
            if (lam->lambda.params.count != dp->delegate_param_count) return -1;
            score += 2;
            /* lambda 显式声明的形参类型与委托形参类型一致 → 更贴 */
            for (int k = 0; k < lam->lambda.params.count; k++) {
                zan_ast_node_t *lp = lam->lambda.params.items[k];
                if (!lp || lp->kind != AST_PARAM || !lp->param.type) continue;
                zan_type_t *lpt = zan_binder_resolve_type(g->binder, lp->param.type);
                zan_type_t *dpt = dp->delegate_param_types[k];
                if (lpt && dpt && types_concrete_equal(lpt, dpt)) score += 2;
            }
            zan_ast_node_t *body = lam->lambda.body;
            if (!body) continue;
            int mark = locals->count;
            unsigned mark_gen = g_local_gen;
            for (int k = 0; k < lam->lambda.params.count; k++) {
                zan_ast_node_t *lp = lam->lambda.params.items[k];
                zan_type_t *lpt = lp->param.type
                    ? zan_binder_resolve_type(g->binder, lp->param.type)
                    : dp->delegate_param_types[k];
                local_add(locals, lp->param.name, NULL, lpt);
            }
            int bf = FAM_UNKNOWN;
            if (body->kind == AST_BLOCK) {
                zan_type_t *bt = stmt_lambda_return_type(g, body, locals);
                if (bt) bf = type_family(bt);
            } else {
                bf = expr_family(g, body, locals);
            }
            locals->count = mark;
            g_local_gen = mark_gen;
            int df = type_family(dp->delegate_ret_type);
            if (bf != FAM_UNKNOWN && df != FAM_UNKNOWN) {
                if (bf == df) score += 2;
                else return -1;
            }
            continue;
        }
        zan_type_t *dp0 = method_param_type_at(g, m, j, call, recv_expr, locals);
        zan_symbol_t *mg = arg_method_group(
            g, a, locals,
            (dp0 && dp0->kind == TYPE_DELEGATE) ? dp0->delegate_param_count : -1);
        if (mg) {
            int ms = method_group_score(g, mg, dp0);
            if (ms < 0) return -1;
            score += ms;
            continue;
        }
        zan_type_t *pt = method_param_type(g, m, j);
        if (!pt) continue;
        if (type_mentions_tp(pt)) {
            if (pt->kind != TYPE_TYPE_PARAM) {
                zan_type_t *gat = infer_expr_type(g, a, locals);
                if (gat && types_match_modulo_tp(pt, gat)) score += 3;
            }
            continue;
        }
        if (pt->kind == TYPE_INTERFACE) {
            /* 接口形参的匹配判定：
             *   - null 字面量：按"接不接得住 null"判（与 checker、concrete_arg_score
             *     同一条规则 zan_null_arg_score）。
             *   - 实参静态类型**也是接口** → 精确比对：同一个接口 = 精确匹配（+4）；
             *     实参接口派生自形参接口 = 可赋值（+1）；毫无关系 = 不加分。
             *   - 实参是**类** → 实现了该接口 = 可赋值（+1）；没实现就不加分，
             *     但不判死（实现关系可能经由泛型实例化等路径看不出来，误杀代价大）。
             *
             * 踩过的坑：这里原来一律 0 分（continue），理由是"打分看不出实现
             * 列表"。但"只靠接口类型区分"的一组重载于是**全部同分**，第一个永远
             * 赢：`TransactionScope.Create(fsql)`（fsql 是 IZanSql）被发到
             * `Create(IUnitOfWork)` 上——字段里装的是错类型，后面调用保存点方法
             * 时才炸，错误消息完全指不到病根。 */
            if (a && a->kind == AST_NULL_LITERAL) {
                int ns = zan_null_arg_score(pt);
                if (ns < 0) return -1;
                score += ns;
                continue;
            }
            zan_type_t *at = infer_expr_type(g, a, locals);
            if (at && at->sym && pt->sym && pt->type_arg_count == 0 &&
                at->type_arg_count == 0) {
                if (at->kind == TYPE_INTERFACE) {
                    if (at->sym == pt->sym) { score += 4; continue; }
                    if (zan_binder_class_implements_iface(at->sym, pt->sym))
                        score += 1;
                    continue;
                }
                if (at->kind == TYPE_CLASS &&
                    zan_binder_class_implements_iface(at->sym, pt->sym))
                    score += 1;
            }
            continue;
        }
        int s = concrete_arg_score(g, pt, a, locals);
        if (s < 0) return -1;
        score += s;
    }
    return score;
}

static bool ext_receiver_interface_match(zan_type_t *pt, zan_type_t *recv_ty) {
    return pt && recv_ty && pt->kind == TYPE_INTERFACE &&
           pt->type_arg_count == 0 &&
           (recv_ty->kind == TYPE_CLASS || recv_ty->kind == TYPE_INTERFACE) &&
           class_implements_iface(recv_ty->sym, pt->sym);
}

static int ext_method_score(zan_irgen_t *g, zan_symbol_t *m,
                            zan_type_t *recv_ty, zan_ast_node_t *call,
                            zan_ast_node_t *recv_expr, local_scope_t *locals) {
    zan_ast_list_t *ps = &m->decl->method_decl.params;
    zan_type_t *pt = zan_binder_resolve_type(g->binder, ps->items[0]->param.type);
    int score = 0;
    if (pt && !type_mentions_tp(pt)) {
        if (types_concrete_equal(pt, recv_ty)) score += 4;
        else if (ext_receiver_interface_match(pt, recv_ty)) score += 2;
        else if (!type_mentions_tp(recv_ty)) return -1;
    }
    int as = method_args_score(g, m, call, recv_expr, locals, 1);
    if (as < 0) return -1;
    return score + as;
}
zan_symbol_t *find_extension_method(zan_irgen_t *g, zan_type_t *recv_ty,
                                           zan_istr_t name, int argc,
                                           zan_ast_node_t *call,
                                           zan_ast_node_t *recv_expr,
                                           local_scope_t *locals) {
    if (!recv_ty) return NULL;
    zan_symbol_t *best = NULL;
    int best_score = -1;

    /* 优先从 binder 的全量扩展方法登记表中查找：包含泛型方法模板（其在发射阶段
     * 因带有类型参数未特化而被 method_is_tp_template 略过，未进 g->functions）。 */
    int ext_count = (g->binder && g->binder->ext_methods) ? g->binder->ext_method_count : 0;
    for (int i = 0; i < ext_count; i++) {
        zan_symbol_t *m = g->binder->ext_methods[i].method;
        if (!m || m->kind != SYM_METHOD || !m->decl ||
            m->decl->kind != AST_METHOD_DECL)
            continue;
        if (m->name.len != name.len ||
            memcmp(m->name.str, name.str, (size_t)name.len) != 0)
            continue;
        zan_ast_list_t *ps = &m->decl->method_decl.params;
        if (ps->count != argc + 1) continue;
        zan_ast_node_t *p0 = ps->items[0];
        if (!p0 || p0->kind != AST_PARAM || !p0->param.is_this) continue;
        zan_type_t *pt = g->binder->ext_methods[i].recv_type;
        if (!pt) pt = zan_binder_resolve_type(g->binder, p0->param.type);
        if (!pt) continue;
        if (pt->kind != TYPE_TYPE_PARAM &&
            !ext_receiver_interface_match(pt, recv_ty)) {
            if (pt->kind != recv_ty->kind) continue;
            if ((pt->kind == TYPE_CLASS || pt->kind == TYPE_STRUCT ||
                 pt->kind == TYPE_INTERFACE || pt->kind == TYPE_ENUM) &&
                pt->sym != recv_ty->sym)
                continue;
        }
        int score = ext_method_score(g, m, recv_ty, call, recv_expr, locals);
        if (score > best_score) {
            best_score = score;
            best = m;
        }
    }
    if (best && best_score >= 0) return best;

    for (int fi = 0; fi < g->function_count; fi++) {
        zan_symbol_t *m = g->functions[fi].sym;
        if (!m || m->kind != SYM_METHOD || !m->decl ||
            m->decl->kind != AST_METHOD_DECL)
            continue;
        if (m->name.len != name.len ||
            memcmp(m->name.str, name.str, (size_t)name.len) != 0)
            continue;
        zan_ast_list_t *ps = &m->decl->method_decl.params;
        if (ps->count != argc + 1) continue;
        zan_ast_node_t *p0 = ps->count > 0 ? ps->items[0] : NULL;
        if (!p0 || p0->kind != AST_PARAM || !p0->param.is_this) continue;
        zan_type_t *pt = zan_binder_resolve_type(g->binder, p0->param.type);
        if (!pt) continue;
        if (pt->kind != TYPE_TYPE_PARAM &&
            !ext_receiver_interface_match(pt, recv_ty)) {
            if (pt->kind != recv_ty->kind) continue;
            if ((pt->kind == TYPE_CLASS || pt->kind == TYPE_STRUCT ||
                 pt->kind == TYPE_INTERFACE || pt->kind == TYPE_ENUM) &&
                pt->sym != recv_ty->sym)
                continue;
        }
        int score = ext_method_score(g, m, recv_ty, call, recv_expr, locals);
        if (score > best_score) {
            best_score = score;
            best = m;
        }
    }
    return (best && best_score >= 0) ? best : NULL;
}

#define IRGEN_SHARED_MAX_STRING 1048576
#define IRGEN_SHARED_MAX_KEY 1024

static void check_shared_table_width(zan_irgen_t *g, zan_symbol_t *type_sym,
                                     zan_istr_t name, zan_ast_node_t *call) {
    if (!type_sym || !call || call->kind != AST_CALL) return;
    if (type_sym->name.len != 11 ||
        memcmp(type_sym->name.str, "SharedTable", 11) != 0) return;

    long long limit;
    int idx;
    const char *what;
    if (name.len == 12 && memcmp(name.str, "ColumnString", 12) == 0) {
        limit = IRGEN_SHARED_MAX_STRING; idx = 1; what = "string column";
    } else if (name.len == 7 && memcmp(name.str, "KeySize", 7) == 0) {
        limit = IRGEN_SHARED_MAX_KEY; idx = 0; what = "key";
    } else {
        return;
    }
    if (call->call.args.count <= idx) return;
    zan_ast_node_t *arg = call->call.args.items[idx];
    if (!arg || arg->kind != AST_INT_LITERAL) return;
    if (arg->int_val > 0 && arg->int_val <= limit) return;

    zan_diag_emit_code(g->diag, DIAG_ERROR, arg->loc, 4001,
                  "shared table %s width %lld is out of range: a width must be "
                  "between 1 and %lld bytes",
                  what, (long long)arg->int_val, limit);
}

static zan_symbol_t *resolve_overload_typed_inner(zan_irgen_t *g,
                                                  zan_symbol_t *type_sym,
                                                  zan_istr_t name,
                                                  zan_ast_node_t *call,
                                                  local_scope_t *locals) {
    int argc = (call && call->kind == AST_CALL) ? call->call.args.count : 0;
    int type_arg_count = (call && call->kind == AST_CALL)
        ? call->call.type_args.count : 0;
    check_shared_table_width(g, type_sym, name, call);
    zan_symbol_t *best = NULL;
    int best_score = -1;
    int arity_matches = 0;
    zan_symbol_t *cls = type_sym;
    zan_ast_node_t *recv_obj = (call && call->kind == AST_CALL &&
                                call->call.callee &&
                                call->call.callee->kind == AST_MEMBER_ACCESS)
                               ? call->call.callee->member.object
                               : NULL;
    while (cls) {
        for (int i = 0; i < cls->member_count; i++) {
            zan_symbol_t *m = cls->members[i];
            if (m->kind != SYM_METHOD || !m->decl ||
                m->decl->kind != AST_METHOD_DECL) continue;
            if (m->name.len != name.len ||
                memcmp(m->name.str, name.str, name.len) != 0) continue;
            if (type_arg_count > 0 &&
                m->decl->method_decl.type_params.count != type_arg_count)
                continue;
            if (!method_accepts_arity(m, argc)) continue;
            arity_matches++;
            int score = method_args_score(g, m, call, recv_obj, locals, 0);
            if (score > best_score) {
                best_score = score;
                best = m;
            }
        }
        zan_symbol_t *base = (cls->type && cls->type->base_type)
            ? cls->type->base_type->sym : NULL;
        cls = (base && base != cls) ? base : NULL;
    }
    if (best && best_score >= 0) {
        /* A443：Convert.ToString 的真重载进了 stdlib，但内建特判覆盖的形状
         * 面更宽。宽容分曾把不该进的实参送进真方法：可空→标量 0 分
         * （type_family(可空)=UNKNOWN 走"宽容放过"），box 指针当 i64 传，
         * LLVM verify 当场炸；枚举/数值协变 2 分虽然能传，但内建对它们的
         * 渲染才是被 reject/golden 锁住的既有行为。收敛口径：只有精确
         * 类型匹配（分=4）才解析到真方法，其余形状静默交还调用方——
         * 与「候选一个都没有」的旧路径同一落点（object 指针拷贝、可空
         * emit_to_cstr_u、class/struct/array/delegate 的 A463 拒绝措辞
         * 全部原样保留）。 */
        if (best_score < 4 && name.len == 8 &&
            memcmp(name.str, "ToString", 8) == 0 &&
            type_sym->name.len == 7 &&
            memcmp(type_sym->name.str, "Convert", 7) == 0) {
            return NULL;
        }
        return best;
    }
    if (arity_matches > 0) {
        /* 无类型匹配的形状（int[]/object/class/struct/delegate）同理回落
         * 内建——曾在这里报死 "no overload matches"，顶掉 A463 措辞，
         * 四个 convert_to_string_* reject 用例齐红。 */
        if (name.len == 8 && memcmp(name.str, "ToString", 8) == 0 &&
            type_sym->name.len == 7 &&
            memcmp(type_sym->name.str, "Convert", 7) == 0) {
            return NULL;
        }
        zan_diag_emit_code(g->diag, DIAG_ERROR,
                      call ? call->loc : (zan_loc_t){0}, 2103,
                      "no overload of '%.*s.%.*s' matches argument type(s)",
                      (int)type_sym->name.len, type_sym->name.str,
                      (int)name.len, name.str);
        return NULL;
    }
    return resolve_overload(type_sym, name, argc, type_arg_count);
}
zan_symbol_t *resolve_overload_typed(zan_irgen_t *g,
                                            zan_symbol_t *type_sym,
                                            zan_istr_t name,
                                            zan_ast_node_t *call,
                                            local_scope_t *locals) {
    if (call && (call->in_infer & 2)) return NULL;
    if (call) call->in_infer |= 2;
    zan_ast_node_t *recv_obj = (call && call->kind == AST_CALL &&
                                call->call.callee &&
                                call->call.callee->kind == AST_MEMBER_ACCESS)
                               ? call->call.callee->member.object
                               : NULL;
    zan_type_t *saved_recv = g->cur_recv_type;
    if (recv_obj) g->cur_recv_type = infer_expr_type(g, recv_obj, locals);
    else if (type_sym && type_sym->type) g->cur_recv_type = type_sym->type;
    zan_symbol_t *res = resolve_overload_typed_inner(g, type_sym, name, call, locals);
    g->cur_recv_type = saved_recv;
    if (call) call->in_infer &= ~2;
    return res;
}
zan_symbol_t *resolve_op_overload(zan_irgen_t *g,
                                        zan_symbol_t *type_sym,
                                        zan_istr_t name,
                                        zan_ast_node_t *call,
                                        local_scope_t *locals) {
    int argc = (call && call->kind == AST_CALL) ? call->call.args.count : 0;
    zan_symbol_t *best = NULL;
    zan_symbol_t *first_variadic = NULL;
    int best_score = -1;
    for (int i = 0; i < type_sym->member_count; i++) {
        zan_symbol_t *m = type_sym->members[i];
        if (m->kind != SYM_METHOD || !m->decl ||
            m->decl->kind != AST_METHOD_DECL) continue;
        if (m->name.len != name.len ||
            memcmp(m->name.str, name.str, (size_t)name.len) != 0) continue;
        zan_ast_list_t *ps = &m->decl->method_decl.params;
        if (ps->count < 1) continue;
        int is_static = (m->modifiers & MOD_STATIC) != 0;
        int p0 = is_static ? 1 : 0;
        int variadic = method_is_params_variadic(m);
        int score = 0;
        if (variadic) {
            int fixed = ps->count - 1 - p0;
            if (argc < fixed) continue;
            if (!first_variadic) first_variadic = m;
            score = method_args_score(g, m, call, NULL, locals, p0);
            if (score < 0 && fixed > 0) continue;
            score = 0;
            zan_type_t *pt = zan_binder_resolve_type(g->binder,
                ps->items[ps->count - 1]->param.type);
            zan_type_t *et = pt ? pt->element_type : NULL;
            for (int ai = fixed; ai < argc; ai++) {
                zan_type_t *at = infer_expr_type(g, call->call.args.items[ai], locals);
                if (!et || !at) continue;
                if (types_concrete_equal(et, at)) {
                    score += 4;
                    continue;
                }
                int ef = type_family(et), af = type_family(at);
                if (ef == FAM_UNKNOWN || af == FAM_UNKNOWN) continue;
                if (ef == FAM_FLOAT && af == FAM_INT) continue;
                score = -1;
                break;
            }
        } else {
            if (ps->count != argc + p0) continue;
            score = method_args_score(g, m, call, NULL, locals, p0);
        }
        if (score > best_score) {
            best_score = score;
            best = m;
        }
    }
    if (best && best_score >= 0) return best;
    return first_variadic;
}
int op_index_param_offset(zan_symbol_t *m) {
    return (m->modifiers & MOD_STATIC) ? 1 : 0;
}

/* The result type of a member of a built-in type (List<T>, string,
 * Dictionary, StringBuilder).
 *
 * A built-in type has no binder symbol, so the symbol table cannot answer
 * "what does this member return" -- the built-in table can.  Without it the
 * type of `list.Contains(x)` was unknown, and the renderer that picks the
 * text for a bool fell back to the integer path: the same program printed
 * "true" for `Console.WriteLine(true)` but "1" for
 * `Console.WriteLine(list.Contains(x))`. */
static zan_type_t *builtin_member_result_type(zan_irgen_t *g, zan_type_t *rt,
                                               const char *name, int name_len) {
    if (!g || !g->binder || !rt || rt->sym || !name || name_len <= 0) return NULL;
    char tname[64];
    int n = rt->name.len;
    if (n <= 0 || n >= (int)sizeof(tname)) return NULL;
    memcpy(tname, rt->name.str, (size_t)n);
    tname[n] = 0;
    const char *res = zan_builtin_member_result(tname, name, name_len);
    if (!res) return NULL;
    if (strcmp(res, "string") == 0) return g->binder->type_string;
    if (strcmp(res, "int") == 0) return g->binder->type_int;
    if (strcmp(res, "long") == 0) return g->binder->type_long;
    if (strcmp(res, "double") == 0) return g->binder->type_double;
    if (strcmp(res, "bool") == 0) return g->binder->type_bool;
    /* the element type comes from the receiver: List<T>.ToArray -> T[],
     * Dictionary<K,V>.Keys -> List<K> */
    if (strcmp(res, "T[]") == 0 && rt->type_arg_count == 1)
        return zan_binder_make_array_type(g->binder, rt->type_args[0]);
    if (strcmp(res, "List<K>") == 0 && rt->type_arg_count == 2)
        return zan_binder_make_list_type(g->binder, rt->type_args[0]);
    if (strcmp(res, "List<V>") == 0 && rt->type_arg_count == 2)
        return zan_binder_make_list_type(g->binder, rt->type_args[1]);
    if (strcmp(res, "List<string>") == 0)
        return zan_binder_make_list_type(g->binder, g->binder->type_string);
    return NULL;
}

/* The static built-in class behind a primitive type name: `string.Format` is
 * a member of the static String class, not of the instance string type. */
static const char *builtin_static_class_for(const char *name) {
    if (strcmp(name, "string") == 0) return "String";
    if (strcmp(name, "char") == 0) return "Char";
    return NULL;
}

static zan_type_t *infer_expr_type_raw(zan_irgen_t *g, zan_ast_node_t *e,
                                       local_scope_t *locals);

#define INFER_CACHE_SLOTS 65536   /* power of two */

typedef struct {
    zan_ast_node_t *node;
    zan_type_t *type;
} infer_cache_slot_t;

static infer_cache_slot_t g_infer_cache[INFER_CACHE_SLOTS];
static struct {
    void *locals;
    int lcount;
    unsigned gen;
    void *inst;
    void *mtps;
    void *mbind;
    void *tsym;
    bool live;
} g_infer_ctx;

static infer_cache_slot_t *infer_cache_slot(zan_irgen_t *g, zan_ast_node_t *e,
                                            local_scope_t *locals) {
    if (!e || !locals) return NULL;
    if (!g_infer_ctx.live || g_infer_ctx.locals != (void *)locals ||
        g_infer_ctx.lcount != locals->count || g_infer_ctx.gen != g_local_gen ||
        g_infer_ctx.inst != (void *)g->cur_inst ||
        g_infer_ctx.mtps != (void *)g->cur_mtps ||
        g_infer_ctx.mbind != (void *)g->cur_mbind ||
        g_infer_ctx.tsym != (void *)g->current_type_sym) {
        memset(g_infer_cache, 0, sizeof(g_infer_cache));
        g_infer_ctx.locals = (void *)locals;
        g_infer_ctx.lcount = locals->count;
        g_infer_ctx.gen = g_local_gen;
        g_infer_ctx.inst = (void *)g->cur_inst;
        g_infer_ctx.mtps = (void *)g->cur_mtps;
        g_infer_ctx.mbind = (void *)g->cur_mbind;
        g_infer_ctx.tsym = (void *)g->current_type_sym;
        g_infer_ctx.live = true;
    }
    size_t h = ((size_t)(uintptr_t)e >> 4) * 2654435761u;
    return &g_infer_cache[h & (INFER_CACHE_SLOTS - 1)];
}
