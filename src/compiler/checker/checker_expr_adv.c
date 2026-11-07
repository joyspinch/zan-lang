    case AST_NEW_EXPR: {
        no_runtime_reject(c, expr->loc, "allocation (`new`)");
        zan_type_t *type;
        if (expr->new_expr.call_init) {
            /* `FactoryCall( */
            type = zan_checker_check_expr(c, expr->new_expr.call_init);
        } else if (!expr->new_expr.type) {
            for (int i = 0; i < expr->new_expr.args.count; i++) {
                zan_checker_check_expr(c, expr->new_expr.args.items[i]);
            }
            return c->binder->type_error;
        }
        bool factory_init = expr->new_expr.call_init != NULL;
        if (!factory_init) {
            type = zan_binder_resolve_type(c->binder, expr->new_expr.type);
            if (expr->new_expr.is_array && type && type->kind != TYPE_ERROR &&
                type->kind != TYPE_ARRAY)
                type = zan_binder_make_array_type(c->binder, type);
            /* 类型参数不能实例化（**标量**形态）：类型参数没有构造器，
               irgen 侧只会造出一个 null 引用——运行期第一次成员访问就崩
               进程（0xC0000005）。数组形态 `new T[n]` 不在此列：元素类型
               在实例化时被替换，是合法的（实测 `T[] buf = new T[3]` 正常）。
               真要"由类型参数造对象"，只能把工厂交给调用方。 */
            if (type && type->kind == TYPE_TYPE_PARAM) {
                if (!type_param_has_new_constraint(c, type)) {
                    zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2057,
                                  "cannot instantiate a type parameter '%s'",
                                  type_name(type));
                }
            }
            check_generic_constraints(c, type, expr->loc);
            if (type && type->kind == TYPE_CLASS && type->sym &&
                checker_class_is_abstract(type->sym)) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2058,
                    "cannot instantiate abstract class '%s'",
                    type_name(type));
            }
            check_ctor_available(c, type, expr);
        }
        /* new List<T>(src) 单参数拷贝构造函数类型推导 */
        bool list_copy_candidate = !expr->new_expr.is_array && type &&
            type->kind == TYPE_CLASS && type->type_arg_count == 1 &&
            type->name.len == 4 && memcmp(type->name.str, "List", 4) == 0 &&
            expr->new_expr.args.count == 1 &&
            expr->new_expr.args.items[0] &&
            expr->new_expr.args.items[0]->kind != AST_ASSIGNMENT;
        expr->new_expr.list_copy = false;
        if (expr->new_expr.is_array && !expr->new_expr.array_init &&
            expr->new_expr.args.count > 0) {
            int rank = expr->new_expr.array_rank > 0
                ? expr->new_expr.array_rank : 1;
            int nd = rank > 16 ? 16 : rank;
            int ninit = expr->new_expr.args.count - nd;
            if (ninit > 0) {
                int64_t prod = 1;
                bool const_dims = true;
                for (int d = 0; d < nd && const_dims; d++) {
                    int64_t dv = 0;
                    if (!const_integral_value(expr->new_expr.args.items[d], &dv))
                        const_dims = false;
                    else
                        prod *= dv;
                }
                if (!const_dims)
                    zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2059,
                                  "an array creation with an initializer "
                                  "requires constant dimensions");
                else if (prod != ninit)
                    zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2060,
                                  "array initializer has %d element%s but the "
                                  "dimensions describe %lld",
                                  ninit, ninit == 1 ? "" : "s",
                                  (long long)prod);
            }
        }
        int ctor_argc = (!factory_init && !expr->new_expr.is_array && type && type->sym)
            ? ctor_arg_count(c, expr, type->sym) : 0;
        zan_type_t *ctor_arg_types[256];
        if (ctor_argc > 256) ctor_argc = 256;
        for (int k = 0; k < ctor_argc; k++) ctor_arg_types[k] = NULL;

        zan_ast_list_t *init_lists[2] = { &expr->new_expr.args,
                                          &expr->new_expr.arg_inits };
        for (int li = 0; li < 2; li++) {
        zan_ast_list_t *init_args = init_lists[li];
        for (int i = 0; i < init_args->count; i++) {
            zan_ast_node_t *arg = init_args->items[i];
            if (arg && arg_is_initializer_entry(arg) && type && type->sym) {
                zan_istr_t mname = arg->kind == AST_COLL_INIT
                    ? arg->coll_init.name
                    : arg->binary.left->ident.name;
                zan_symbol_t *field = checker_find_field(type->sym, mname);
                if (!field && factory_init) {
                    zan_diag_emit_code(c->diag, DIAG_ERROR, arg->loc, 2061,
                                  "'%s' has no member '%.*s'",
                                  type_name(type), (int)mname.len, mname.str);
                    continue;
                }
                if (field) {
                    if (property_is_readonly(field) &&
                        arg->kind != AST_COLL_INIT) {
                        zan_diag_emit_code(c->diag, DIAG_ERROR, arg->loc, 2006,
                                      "property '%.*s' has no setter and "
                                      "cannot be assigned",
                                      (int)field->name.len, field->name.str);
                    }
                }
                if (arg->kind == AST_COLL_INIT) {
                    if (!field || !field->type) {
                        zan_diag_emit_code(c->diag, DIAG_ERROR, arg->loc, 2061,
                                      "'%s' has no member '%.*s'",
                                      type_name(type),
                                      (int)mname.len, mname.str);
                    } else {
                        zan_istr_t add_istr = {(char *)"Add", 3};
                        bool builtin_list = false;
                        zan_type_t *coll = field->type;
                        if (coll && ((coll->name.len == 4 &&
                                      memcmp(coll->name.str, "List", 4) == 0) ||
                                     (coll->name.len == 4 &&
                                      memcmp(coll->name.str, "Dict", 4) == 0)))
                            builtin_list = true;
                        zan_symbol_t *coll_sym =
                            coll && (coll->kind == TYPE_CLASS ||
                                     coll->kind == TYPE_STRUCT)
                                ? coll->sym : NULL;
                        zan_symbol_t *add = builtin_list
                            ? (zan_symbol_t *)1
                            : (coll_sym ? checker_find_method(coll_sym, add_istr)
                                        : NULL);
                        bool dict_kv = coll && coll->name.len == 4 &&
                                       memcmp(coll->name.str, "Dict", 4) == 0;
                        zan_type_t *elem =
                            coll && coll->type_arg_count >= 1
                                ? coll->type_args[0]
                                : (coll && coll->kind == TYPE_ARRAY
                                   ? coll->element_type : NULL);
                        zan_type_t *elem2 = coll && coll->type_arg_count >= 2
                            ? coll->type_args[1] : NULL;
                        if (!add) {
                            zan_diag_emit_code(c->diag, DIAG_ERROR, arg->loc, 2062,
                                          "type '%s' has no Add method for a "
                                          "collection initializer",
                                          type_name(coll));
                        }
                        for (int k = 0; k < arg->coll_init.items.count; k++) {
                            zan_ast_node_t *item =
                                arg->coll_init.items.items[k];
                            zan_type_t *it = zan_checker_check_expr(c, item);
                            if (elem && it && add && !dict_kv)
                                checker_check_assignable(c, elem, it, item,
                                                         item->loc,
                                                         "collection initializer");
                            if (dict_kv && add) {
                                zan_ast_node_t *vnode =
                                    k + 1 < arg->coll_init.items.count
                                        ? arg->coll_init.items.items[k + 1]
                                        : NULL;
                                if (vnode) {
                                    zan_type_t *vt =
                                        zan_checker_check_expr(c, vnode);
                                    if (elem && it)
                                        checker_check_assignable(c, elem, it,
                                                                 item, item->loc,
                                                                 "collection initializer");
                                    if (elem2 && vt)
                                        checker_check_assignable(c, elem2, vt,
                                                                 vnode, vnode->loc,
                                                                 "collection initializer");
                                    k++;
                                } else if (elem && it) {
                                    checker_check_assignable(c, elem, it, item,
                                                             item->loc,
                                                             "collection initializer");
                                }
                            }
                        }
                    }
                    continue;
                }
                if (field && field->type) {
                    /* 对象初始化器右值有字段类型做委托裁决（B-ID151） */
                    int prev_assign = c->in_assignable_ctx;
                    c->in_assignable_ctx = 1;
                    zan_type_t *right = zan_checker_check_expr(
                        c, arg->binary.right);
                    c->in_assignable_ctx = prev_assign;
                    checker_check_assignable(c, field->type, right,
                                             arg->binary.right, arg->loc,
                                             "object initializer");
                    continue;
                }
            }
            /* 构造器实参：与方法调用实参同规（可能有委托形参，
             * 如 new Thread(Run)——方法组在此静默，交构造器候选
             * 评分按形参裁决）。 */
            int prev_in_call_args = c->in_call_args;
            c->in_call_args = 1;
            zan_type_t *arg_type = zan_checker_check_expr(c, arg);
            c->in_call_args = prev_in_call_args;
            if (li == 0 && i < ctor_argc) {
                ctor_arg_types[i] = arg_type;
            }
            if (list_copy_candidate &&
                arg_type && arg_type->kind == TYPE_CLASS &&
                arg_type->type_arg_count == 1 && arg_type->name.len == 4 &&
                memcmp(arg_type->name.str, "List", 4) == 0) {
                expr->new_expr.list_copy = true;
            }
        }
        }
        if (ctor_argc > 0) {
            check_ctor_call_arguments(c, type, expr, ctor_argc, ctor_arg_types);
        }
        return type;
    }

    case AST_CONDITIONAL: {
        zan_checker_check_expr(c, expr->conditional.cond);
        zan_type_t *then_type = zan_checker_check_expr(c, expr->conditional.then_expr);
        zan_type_t *else_type = zan_checker_check_expr(c, expr->conditional.else_expr);
        zan_type_t *merged = merge_conditional_types(c, then_type, else_type);
        if (!merged) {
            /* 三元条件表达式中引用类型与 null 字面量类型推导 */
            if (expr_is_null_literal(expr->conditional.else_expr) &&
                checker_type_is_ref(then_type))
                merged = then_type;
            else if (expr_is_null_literal(expr->conditional.then_expr) &&
                     checker_type_is_ref(else_type))
                merged = else_type;
        }
        if (!merged) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2063,
                "cannot apply '?:' operator to operands of type '%s' and '%s': "
                "neither converts to the other and they share no common type",
                type_name(then_type), type_name(else_type));
            return c->binder->type_error;
        }
        return merged;
    }

    case AST_LAMBDA:
        no_runtime_reject(c, expr->loc, "a lambda");
        /* B-ID65：调用实参位置的显式树类型 lambda（`(ExprNode x) => …` /
         * `(Expr<T> x) => …`）不查体——树 lambda 在 irgen 发射口降级为
         * ExprNode 构造树；此刻查体会把 x.F 误报成 ExprNode 上无成员
         * （E2084 假阳性，探针 p1 实证）。返回形参的树类型让绑定侧正常
         * 比对；非调用位置（局部赋值等）不在此列，照旧查体按旧语义报
         * 错（局部赋值位置 v1 不支持，文档见 irgen_expr_lambda.c）。 */
        if (c->in_call_args && expr->lambda.params.count == 1 &&
            expr->lambda.body && expr->lambda.body->kind != AST_BLOCK) {
            zan_ast_node_t *p0 = expr->lambda.params.items[0];
            if (p0 && p0->kind == AST_PARAM && p0->param.type) {
                zan_type_t *p0t =
                    zan_binder_resolve_type(c->binder, p0->param.type);
                if (checker_type_is_expr_tree_target(p0t)) return p0t;
            }
        }
        /* lambda 体要过类型检查：它有自己的作用域（形参 + 局部变量），并且
         * **不受外层方法的返回类型约束**——lambda 的返回类型在检查器这里还
         * 不知道（委托类型是从使用现场推出来的），拿外层方法的返回类型去判
         * lambda 里的 `return` 会误报。自举侧同口径（checker.zan 的
         * AK.Lambda 分支把 curRet 置空）。 */
        {
            /* 作用域是**嵌套**的：lambda 体看得见外层的局部与形参（Zan 的
             * 闭包会捕获它们），lambda 自己的形参与局部加在前面遮住同名；
             * 出 lambda 时把链头还原，外层的作用域原样留着。 */
            struct checker_local *saved_locals = c->locals;
            zan_type_t *saved_ret = c->current_return_type;
            c->current_return_type = NULL;
            zan_ast_node_t *saved_body = c->current_body;
            c->current_body = expr->lambda.body;
            for (int i = 0; i < expr->lambda.params.count; i++) {
                zan_ast_node_t *p = expr->lambda.params.items[i];
                if (p && p->kind == AST_PARAM) {
                    zan_type_t *pt = p->param.type
                        ? zan_binder_resolve_type(c->binder, p->param.type)
                        : NULL;
                    /* 无类型的 lambda 形参（`x => ...`，类型由使用现场的委托
                     * 定型）**也要进作用域**：不进的话，体里的 `x` 会落到
                     * `checker_find_field`——解析成外层类的同名字段，类型对不上
                     * 就是一条假阳性（例如类里有 `string count` 字段、lambda
                     * 形参也叫 `count`）。类型先记成 error：委托的形参类型是
                     * 在使用现场推出来的，检查器这里不知道，error 类型让
                     * 后续检查安静降级（`checker_check_assignable` 见到
                     * error 就直接返回）。 */
                    checker_add_local_permissive(c, p->param.name, pt);
                }
            }
            if (expr->lambda.body) {
                if (expr->lambda.body->kind == AST_BLOCK)
                    zan_checker_check_stmt(c, expr->lambda.body);
                else
                    zan_checker_check_expr(c, expr->lambda.body);
            }
            c->current_body = saved_body;
            c->current_return_type = saved_ret;
            c->locals = saved_locals;
        }
        return c->binder->type_error;

    case AST_THIS_EXPR:
        if (c->current_type_sym && c->current_type_sym->type) {
            return c->current_type_sym->type;
        }
        return c->binder->type_error;

    case AST_QUERY_EXPR: {
        zan_type_t *src_ty = zan_checker_check_expr(c, expr->query.source);
        zan_type_t *elem = (src_ty && src_ty->element_type)
            ? src_ty->element_type : NULL;
        if (!elem && src_ty && src_ty->type_args && src_ty->type_arg_count > 0)
            elem = src_ty->type_args[0];
        if (!elem) elem = c->binder->type_int;
        checker_add_local(c, expr->query.var, elem);
        for (int ci = 0; ci < expr->query.clauses.count; ci++) {
            zan_ast_node_t *cl = expr->query.clauses.items[ci];
            if (cl->kind == AST_QUERY_JOIN) {
                zan_type_t *jty = zan_checker_check_expr(
                    c, cl->query_clause.source);
                zan_type_t *je = (jty && jty->element_type)
                    ? jty->element_type : NULL;
                if (!je && jty && jty->type_args && jty->type_arg_count > 0)
                    je = jty->type_args[0];
                if (!je) je = c->binder->type_int;
                zan_checker_check_expr(c, cl->query_clause.left_key);
                checker_add_local(c, cl->query_clause.name, je);
                zan_checker_check_expr(c, cl->query_clause.right_key);
                if (cl->query_clause.into.len > 0)
                    checker_add_local(c, cl->query_clause.into,
                        zan_binder_make_list_type(c->binder, je));
            } else {
                zan_type_t *ct = zan_checker_check_expr(c,
                    cl->query_clause.expr);
                if (cl->kind == AST_QUERY_LET)
                    checker_add_local(c, cl->query_clause.name, ct);
            }
        }
        if (expr->query.group_expr) {
            zan_type_t *ge = zan_checker_check_expr(c, expr->query.group_expr);
            if (!ge) ge = elem;
            zan_checker_check_expr(c, expr->query.group_key);
            zan_type_t *grp = zan_binder_make_grouping_type(c->binder, ge);
            if (expr->query.group_into.len > 0) {
                checker_add_local(c, expr->query.group_into, grp);
                zan_type_t *sel =
                    zan_checker_check_expr(c, expr->query.select);
                if (!sel) sel = ge;
                return zan_binder_make_list_type(c->binder, sel);
            }
            return zan_binder_make_list_type(c->binder, grp);
        }
        zan_type_t *sel = zan_checker_check_expr(c, expr->query.select);
        if (!sel) sel = elem;
        return zan_binder_make_list_type(c->binder, sel);
    }

    case AST_TUPLE_EXPR: {
        int n = expr->tuple_expr.items.count;
        zan_type_t **elems = (zan_type_t **)zan_arena_alloc(
            c->binder->arena, sizeof(zan_type_t *) * (size_t)(n > 0 ? n : 1));
        for (int i = 0; i < n; i++) {
            elems[i] = zan_checker_check_expr(c, expr->tuple_expr.items.items[i]);
        }
        return zan_binder_make_tuple_type(c->binder, elems, n);
    }

    case AST_SWITCH_EXPR: {
        zan_type_t *disc_type = zan_checker_check_expr(c, expr->switch_expr.expr);
        zan_type_t *merged = NULL;
        bool has_result = false;
        bool seen_wildcard = false;

        for (int i = 0; i < expr->switch_expr.arms.count; i++) {
            zan_ast_node_t *arm = expr->switch_expr.arms.items[i];
            struct checker_local *saved_locals = c->locals;

            if (seen_wildcard) {
                zan_diag_emit_code(c->diag, DIAG_WARNING, arm->loc, 2064,
                              "unreachable switch arm: previous discard arm '_' matches all remaining values");
            }

            if (arm->switch_arm.pattern) {
                zan_checker_check_expr(c, arm->switch_arm.pattern);
                if (arm->switch_arm.when_cond == NULL) {
                    for (int prev = 0; prev < i; prev++) {
                        zan_ast_node_t *prev_arm = expr->switch_expr.arms.items[prev];
                        if (prev_arm->switch_arm.when_cond == NULL &&
                            prev_arm->switch_arm.pattern != NULL &&
                            patterns_match_same(arm->switch_arm.pattern, prev_arm->switch_arm.pattern)) {
                            zan_diag_emit_code(c->diag, DIAG_ERROR, arm->loc, 2065,
                                          "duplicate pattern in switch expression");
                            break;
                        }
                    }
                }
            } else if (arm->switch_arm.is_default) {
                for (int prev = 0; prev < i; prev++) {
                    zan_ast_node_t *prev_arm = expr->switch_expr.arms.items[prev];
                    if (prev_arm->switch_arm.is_default) {
                        zan_diag_emit_code(c->diag, DIAG_ERROR, arm->loc, 2066,
                                      "duplicate discard arm '_' in switch expression");
                        break;
                    }
                }
                seen_wildcard = true;
            }

            if (arm->switch_arm.type_pattern && arm->switch_arm.var_name.len > 0) {
                zan_type_t *pt = zan_binder_resolve_type(
                    c->binder, arm->switch_arm.type_pattern);
                checker_add_local(c, arm->switch_arm.var_name, pt);
                if (arm->switch_arm.when_cond == NULL &&
                    (pt && (pt->kind == TYPE_OBJECT || pt == disc_type))) {
                    seen_wildcard = true;
                }
            }
            if (arm->switch_arm.when_cond) {
                zan_checker_check_expr(c, arm->switch_arm.when_cond);
            }
            zan_type_t *rt = zan_checker_check_expr(c, arm->switch_arm.result);
            c->locals = saved_locals;
            if (rt && rt->kind != TYPE_ERROR) {
                if (!has_result) {
                    merged = rt;
                    has_result = true;
                } else {
                    zan_type_t *m2 = merge_conditional_types(c, merged, rt);
                    if (m2) merged = m2;
                }
            }
        }

        if (disc_type && disc_type->kind == TYPE_ENUM && disc_type->sym && !seen_wildcard) {
            zan_symbol_t *esym = disc_type->sym;
            for (int mi = 0; mi < esym->member_count; mi++) {
                zan_symbol_t *em = esym->members[mi];
                if (em->kind != SYM_ENUM_MEMBER) continue;
                bool handled = false;
                for (int ai = 0; ai < expr->switch_expr.arms.count; ai++) {
                    zan_ast_node_t *arm = expr->switch_expr.arms.items[ai];
                    if (arm->switch_arm.pattern) {
                        zan_ast_node_t *pat = arm->switch_arm.pattern;
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
                    zan_diag_emit_code(c->diag, DIAG_WARNING, expr->loc, 2067,
                                  "switch expression does not handle enum member '%.*s' of '%.*s'; add missing case or '_' discard arm",
                                  (int)em->name.len, em->name.str,
                                  (int)esym->name.len, esym->name.str);
                }
            }
        }
        return has_result ? merged : c->binder->type_error;
    }

    case AST_SIZEOF_EXPR: {
        /* sizeof(T)：值类型才有固定非托管尺寸；string、类、委托、数组是
         * 引用形态，编译期拒绝（对齐 C# 对引用类型抛 ArgumentException）。
         * 类型形参放行——泛型方法体内的 sizeof(T) 在实例化点定型。 */
        zan_type_t *t = expr->cast.type
            ? zan_binder_resolve_type(c->binder, expr->cast.type) : NULL;
        if (!t || t->kind == TYPE_ERROR) return c->binder->type_int;
        if (t->kind != TYPE_TYPE_PARAM &&
            (t->kind == TYPE_VOID || t->kind == TYPE_STRING ||
             t->kind == TYPE_CLASS || t->kind == TYPE_INTERFACE ||
             t->kind == TYPE_DELEGATE || t->kind == TYPE_ARRAY ||
             t->kind == TYPE_OBJECT || t->kind == TYPE_TASK)) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 4002,
                "sizeof: type '%s' has no fixed unmanaged size -- only value types (structs, primitives, enums) can be marshaled",
                type_name(t));
        }
        return c->binder->type_int;
    }

    case AST_WITH_EXPR: {
        zan_type_t *rt = zan_checker_check_expr(c, expr->with_expr.expr);
        if (!rt || (rt->kind != TYPE_CLASS && rt->kind != TYPE_STRUCT)) {
            if (rt && rt->kind != TYPE_ERROR)
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2068,
                    "'with' requires a record (class/struct) receiver, "
                    "not '%s'", type_name(rt));
            return c->binder->type_error;
        }
        for (int i = 0; i < expr->with_expr.assigns.count; i++) {
            zan_ast_node_t *asg = expr->with_expr.assigns.items[i];
            if (!asg || asg->kind != AST_ASSIGNMENT) continue;
            zan_istr_t fname = asg->binary.left->ident.name;
            zan_symbol_t *field = rt->sym
                ? checker_find_field(rt->sym, fname) : NULL;
            if (!field || !field->type) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, asg->loc, 2069,
                    "'%s' has no field '%.*s' for 'with'",
                    type_name(rt),
                    (int)fname.len, fname.str);
                zan_checker_check_expr(c, asg->binary.right);
                continue;
            }
            /* with 右值有字段类型做委托裁决（B-ID151） */
            int prev_assign = c->in_assignable_ctx;
            c->in_assignable_ctx = 1;
            zan_type_t *val = zan_checker_check_expr(c, asg->binary.right);
            c->in_assignable_ctx = prev_assign;
            checker_check_assignable(c, field->type, val,
                                     asg->binary.right, asg->loc, "with");
        }
        return rt;
    }

    case AST_CAST_EXPR: {
        zan_type_t *dst = zan_binder_resolve_type(c->binder, expr->cast.type);
        if (dst && dst->kind != TYPE_DELEGATE && dst->kind != TYPE_TYPE_PARAM &&
            dst->kind != TYPE_NINT && dst->kind != TYPE_ULONG) {
            /* 方法组 cast 到非委托类型永远是错：裸方法名在 checking
             * 阶段定型为 type_error 静默放行，漏到发码侧变裸函数
             * 指针，运行期按腐坏对象解引用段错误（B-ID151）。例外：
             * cast 到委托类型的显式转换、泛型 T（可能是委托）、
             * nint/ulong——(nint)App.GuardBody 是方法指针进原生互操
             * 的惯用法（Window.GuardCall((nint)App.GuardBody, 0)）。 */
            reject_method_group(c, expr->cast.expr);
        }
        int prev_assign = c->in_assignable_ctx;
        if (dst && (dst->kind == TYPE_DELEGATE || dst->kind == TYPE_TYPE_PARAM ||
                    dst->kind == TYPE_NINT || dst->kind == TYPE_ULONG)) {
            /* 豁免目标：内层操作数可能是合法方法组（委托转换/泛型 T/
             * 函数指针互操）——内层 check 静默，别让 saw_method 抢报。 */
            c->in_assignable_ctx = 1;
        }
        zan_type_t *src = zan_checker_check_expr(c, expr->cast.expr);
        c->in_assignable_ctx = prev_assign;
        if (!dst) return c->binder->type_error;
        if (!checker_cast_is_valid(c, dst, src)) {
            if (dst->kind == TYPE_OBJECT && !checker_type_is_ref(src))
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2070,
                              "cannot convert '%s' to 'object': value types "
                              "cannot be boxed (Zan has no boxing facility)",
                              type_name(src));
            else
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2071,
                              "cannot convert '%s' to '%s': no such conversion",
                              type_name(src), type_name(dst));
        }
        return dst;
    }

    case AST_AWAIT_EXPR:
        {
            /* 非 async 方法里的 await **是合法的**，判据是"**这个 await 目标
             * 需不需要真挂起**"，不是"外层方法是不是 async"。
             *
             * 能同步降级的几类在非 async 方法里语义都正确：
             *   - await 一个普通值     → 原样求值（不是协程帧）
             *   - await 一个 async 调用 → 根驱动：分配子帧，让当前线程驱动到完成
             *   - await Task.Delay      → 同步休眠
             *   - await Task.JoinWait   → 事件汇合
             *   - await Task.Yield      → 同步上下文里没有协程可让出，退化成空操作
             * 自举侧也是这么判的（src/selfhost/checker.zan："an 'await' in a
             * *synchronous* method is fine ... Only the *operand* has to be a
             * coroutine frame"）。
             *
             * **必须真挂起**的目标（Gate.Park、Socket.ReadReady / WriteReady /
             * RecvOv / RecvToOv / AcceptOv / ResolveAsync / ResolveSockAddr）要
             * 把恢复函数挂到协程帧的状态槽上，没有帧就无路可走——这类由 irgen
             * 在降级处逐个报更具体的诊断（"await Gate.Park is only supported
             * inside an async method"），判据在那边，名单也只有一份。
             */
            zan_type_t *inner = zan_checker_check_expr(c, expr->await_expr.expr);
            if (inner && inner->kind == TYPE_TASK) {
                return inner->type_arg_count == 1 ? inner->type_args[0]
                                                  : c->binder->type_void;
            }
            return inner ? inner : c->binder->type_error;
        }

    case AST_BASE_EXPR:
        return c->binder->type_error; /* resolved in */
    default: {
        /* B-ID102 覆盖审计：有非 NULL 子节点的表达式 kind 落到 default 即被静默
         * type_error，疑似漏改（ZANC_TRACE 门控）。 */
        int nc = zan_ast_child_count(expr), live = 0;
        for (int i = 0; i < nc; i++) if (zan_ast_child_at(expr, i)) live++;
        if (live > 0)
            zan_compile_trace("checker.check_expr: kind %d has %d live children but no case",
                              (int)expr->kind, live);
        return c->binder->type_error;
    }
    }
}

