zan_type_t *zan_checker_check_expr(zan_checker_t *c, zan_ast_node_t *expr) {
    if (!expr) return c->binder->type_error;

    switch (expr->kind) {
    case AST_ERROR_EXPR:
        /* parser 已报过语法错误；哨兵类型静默吞掉下游一切诊断（B-ID91） */
        return c->binder->type_error;
    case AST_INT_LITERAL:
        switch (expr->lit_suffix) {
        case 1:
            return c->binder->type_long;
        case 2:
            if (expr->int_val < 0 || expr->int_val > 4294967295LL)
                return c->binder->type_ulong;
            return c->binder->type_uint;
        case 3:
            return c->binder->type_ulong;
        default:
            if ((expr->lit_radix == 2 || expr->lit_radix == 8
                 || expr->lit_radix == 16)
                && expr->int_val >= 0 && expr->int_val <= 4294967295LL) {
                return c->binder->type_int;
            }
            if (expr->int_val < -2147483648LL || expr->int_val > 2147483647LL)
                return c->binder->type_long;
            return c->binder->type_int;
        }
    case AST_FLOAT_LITERAL:
        /* f/F 后缀字面量按 float 语义定型(C# 同规):无后缀仍为 double */
        return expr->lit_suffix ? c->binder->type_float
                                : c->binder->type_double;
    case AST_DECIMAL_LITERAL:
        return c->binder->type_decimal;
    case AST_STRING_LITERAL:
        return c->binder->type_string;
    case AST_CHAR_LITERAL:
        return c->binder->type_char;
    case AST_BOOL_LITERAL:
        return c->binder->type_bool;
    case AST_NULL_LITERAL:
        return c->binder->type_object;

    case AST_IDENTIFIER: {
        zan_type_t *lt = checker_find_local(c, expr->ident.name);
        if (lt) return lt;
        if (c->current_type_sym) {
            zan_symbol_t *fsym = checker_find_field(c->current_type_sym,
                                                    expr->ident.name);
            if (fsym) {
                return fsym->type ? fsym->type : c->binder->type_error;
            }
        }
        zan_symbol_t *sym = zan_binder_lookup(c->binder, expr->ident.name);
        if (!sym) {
            return c->binder->type_error;
        }
        return sym->type;
    }

    case AST_BINARY: {
        zan_type_t *left = zan_checker_check_expr(c, expr->binary.left);
        zan_type_t *right = zan_checker_check_expr(c, expr->binary.right);
        if (binary_op_compares(expr->binary.op)) {
            if (right->kind != TYPE_DELEGATE)
                reject_method_group(c, expr->binary.left);
            if (left->kind != TYPE_DELEGATE)
                reject_method_group(c, expr->binary.right);
        } else {
            /* 算术/逻辑/位运算：方法组当操作数通常是错（没有委托目标
             * 可救）。裸方法名/成员方法名在 checking 阶段定型为
             * type_error 被静默放行，漏到发码侧变裸函数指针，按
             * E3090（i64+ptr 混算）炸 LLVM（B-ID151）。
             * 特例：类/结构体重载了委托形参的 op_add/op_sub（如事件
             * 类型的 `event += OnClick` 脱糖为 `event = event + OnClick`），
             * 放行该右操作数。 */
            reject_method_group(c, expr->binary.left);
            if (!class_has_op_delegate(c, left, expr->binary.op, expr->binary.right))
                reject_method_group(c, expr->binary.right);
        }

        switch (expr->binary.op) {
        case TK_PLUS:
            if (left->kind == TYPE_STRING || right->kind == TYPE_STRING) {
                zan_ast_node_t *other_node = left->kind == TYPE_STRING ? expr->binary.right : expr->binary.left;
                zan_ast_node_t *raw_other = other_node;
                while (raw_other && raw_other->kind == AST_CAST_EXPR)
                    raw_other = raw_other->cast.expr;
                if (raw_other && raw_other->kind == AST_NULL_LITERAL) {
                    no_runtime_reject(c, expr->loc, "string concatenation");
                    return c->binder->type_string;
                }
                zan_type_t *other = left->kind == TYPE_STRING ? right : left;
                if (!type_is_concatable(other)) {
                    zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2040,
                                  "cannot concatenate 'string' and '%s' -- convert the operand explicitly (e.g. call .ToStr()/read the field) instead of concatenating the reference",
                                  type_name(other));
                    return c->binder->type_error;
                }
                no_runtime_reject(c, expr->loc, "string concatenation");
                return c->binder->type_string;
            }
        case TK_MINUS: case TK_STAR: case TK_SLASH: case TK_PERCENT:
            /* decimal 与 float/double 的混合算术是 C# CS0019 编译错——
             * 不能静默放行到发码侧（double 位模式被按 decimal 结构重解读，
             * 产出 30 位垃圾数；TASKS.md A511②）。 */
            if ((left->kind == TYPE_DECIMAL &&
                 (right->kind == TYPE_FLOAT || right->kind == TYPE_DOUBLE)) ||
                (right->kind == TYPE_DECIMAL &&
                 (left->kind == TYPE_FLOAT || left->kind == TYPE_DOUBLE))) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2041,
                              "cannot apply operator to 'decimal' and '%s' -- "
                              "convert explicitly (e.g. Decimal.FromDouble)",
                              right->kind == TYPE_DECIMAL ? type_name(left)
                                                          : type_name(right));
                return c->binder->type_error;
            }
            if (left->kind == TYPE_DELEGATE || right->kind == TYPE_DELEGATE) {
                if (expr->binary.op == TK_PLUS || expr->binary.op == TK_MINUS)
                    zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2042,
                                  "delegates are single-cast: '+='/'-=' combination is not supported -- assign directly (`d = h`) or subscribe on a multicast event type (`UiEvent`, `e += h`)");
                else
                    zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2043,
                                  "cannot apply operator to '%s' and '%s'",
                                  left->kind == TYPE_ERROR ? "lambda"
                                                           : type_name(left),
                                  right->kind == TYPE_ERROR ? "lambda"
                                                            : type_name(right));
                return c->binder->type_error;
            }
            if (type_is_numeric(left) && type_is_numeric(right)) {
                return promote_numeric(c->binder, left, right);
            }
            if (left->kind == TYPE_NULLABLE || right->kind == TYPE_NULLABLE) {
                zan_type_t *lb = checker_nullable_base(left);
                zan_type_t *rb = checker_nullable_base(right);
                if ((lb->kind == TYPE_DECIMAL || rb->kind == TYPE_DECIMAL) &&
                    type_is_numeric(lb) && type_is_numeric(rb)) {
                    if (lb->kind == TYPE_FLOAT || lb->kind == TYPE_DOUBLE ||
                        rb->kind == TYPE_FLOAT || rb->kind == TYPE_DOUBLE) {
                        zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2041,
                            "cannot apply operator to nullable decimal and floating-point operands -- convert explicitly");
                        return c->binder->type_error;
                    }
                    return zan_binder_make_nullable_type(c->binder, c->binder->type_decimal);
                }
                if (type_is_numeric(lb) && type_is_numeric(rb))
                    return left->kind == TYPE_NULLABLE ? left : right;
            }
            if ((type_is_numeric(left) || left->kind == TYPE_ENUM ||
                 left->kind == TYPE_CHAR) &&
                (type_is_numeric(right) || right->kind == TYPE_ENUM ||
                 right->kind == TYPE_CHAR)) {
                return promote_numeric(c->binder, left, right);
            }
            if (left->kind == TYPE_CLASS || left->kind == TYPE_STRUCT) {
                return left;
            }
            if (left->kind != TYPE_ERROR && right->kind != TYPE_ERROR) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2043,
                              "cannot apply operator to '%s' and '%s'",
                              type_name(left), type_name(right));
            }
            return c->binder->type_error;

        case TK_AMP: case TK_PIPE: case TK_CARET:
        case TK_LESS_LESS: case TK_GREATER_GREATER:
        case TK_GREATER_GREATER_GREATER:
            if ((type_is_integral(left) || left->kind == TYPE_CHAR ||
                 left->kind == TYPE_ENUM) &&
                (type_is_integral(right) || right->kind == TYPE_CHAR ||
                 right->kind == TYPE_ENUM)) {
                return promote_numeric(c->binder, left, right);
            }
            return c->binder->type_error;

        case TK_EQ_EQ: case TK_BANG_EQ: {
            bool ln = type_is_numeric(left) || left->kind == TYPE_CHAR ||
                      left->kind == TYPE_ENUM;
            bool rn = type_is_numeric(right) || right->kind == TYPE_CHAR ||
                      right->kind == TYPE_ENUM;
            if (ln && rn) {
                if (mixed_sign_compare_error(c->binder, c->diag, expr, left, right))
                    return c->binder->type_error;
                return c->binder->type_bool;
            }
            if (left->kind == TYPE_NULLABLE || right->kind == TYPE_NULLABLE) {
                zan_type_t *lb = checker_nullable_base(left);
                zan_type_t *rb = checker_nullable_base(right);
                if (left->kind == TYPE_OBJECT || right->kind == TYPE_OBJECT)
                    return c->binder->type_bool;
                bool l2 = type_is_numeric(lb) || lb->kind == TYPE_CHAR ||
                          lb->kind == TYPE_ENUM;
                bool r2 = type_is_numeric(rb) || rb->kind == TYPE_CHAR ||
                          rb->kind == TYPE_ENUM;
                if (l2 && r2) return c->binder->type_bool;
                if (lb->kind == TYPE_BOOL && rb->kind == TYPE_BOOL)
                    return c->binder->type_bool;
                if (checker_type_assignable(lb, rb) ||
                    checker_type_assignable(rb, lb))
                    return c->binder->type_bool;
            }
            if (left->kind == TYPE_STRUCT && right->kind == TYPE_STRUCT &&
                left->sym && left->sym == right->sym)
                return c->binder->type_bool;
            if (left->kind == TYPE_TYPE_PARAM && right->kind == TYPE_TYPE_PARAM &&
                checker_type_equal(left, right))
                return c->binder->type_bool;
            if ((left->kind == TYPE_TYPE_PARAM && (right->kind == TYPE_OBJECT || expr_is_null_literal(expr->binary.right))) ||
                (right->kind == TYPE_TYPE_PARAM && (left->kind == TYPE_OBJECT || expr_is_null_literal(expr->binary.left))))
                return c->binder->type_bool;
            /* 单侧 Binding<T> 的比较会落到 object==object 的引用比较，恒为
             * false（live 绑定每次取值都是新对象语义）——当场拒绝并指引
             * .Get()。Binding==null（判绑定对象本身）与 Binding==Binding
             * 照常放行。 */
            if (left->kind != TYPE_ERROR && right->kind != TYPE_ERROR &&
                !expr_is_null_literal(expr->binary.left) &&
                !expr_is_null_literal(expr->binary.right)) {
                zan_type_t *bnd = checker_type_is_binding(left) ? left
                                : checker_type_is_binding(right) ? right : NULL;
                zan_type_t *oth = bnd == left ? right : left;
                if (bnd && oth->kind != TYPE_OBJECT &&
                    oth->kind != TYPE_TYPE_PARAM &&
                    !checker_type_is_binding(oth)) {
                    zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2044,
                                  "cannot compare '%s' and '%s' -- call .Get() "
                                  "on the binding to compare the bound value",
                                  type_name(left), type_name(right));
                    return c->binder->type_error;
                }
            }
            if (checker_type_is_ref(left) && checker_type_is_ref(right) &&
                (left->kind == TYPE_OBJECT || right->kind == TYPE_OBJECT ||
                 checker_type_assignable(left, right) ||
                 checker_type_assignable(right, left)))
                return c->binder->type_bool;
            if (left->kind == TYPE_BOOL && right->kind == TYPE_BOOL)
                return c->binder->type_bool;
            if (left->kind != TYPE_ERROR && right->kind != TYPE_ERROR)
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2045,
                              "cannot compare '%s' and '%s'",
                              type_name(left), type_name(right));
            return c->binder->type_error;
        }
        case TK_LESS: case TK_GREATER:
        case TK_LESS_EQ: case TK_GREATER_EQ: {
            bool ln = type_is_numeric(left) || left->kind == TYPE_CHAR ||
                      left->kind == TYPE_ENUM;
            bool rn = type_is_numeric(right) || right->kind == TYPE_CHAR ||
                      right->kind == TYPE_ENUM;
            if (ln && rn) {
                if (mixed_sign_compare_error(c->binder, c->diag, expr, left, right))
                    return c->binder->type_error;
                return c->binder->type_bool;
            }
            if (left->kind == TYPE_NULLABLE || right->kind == TYPE_NULLABLE) {
                zan_type_t *lb = checker_nullable_base(left);
                zan_type_t *rb = checker_nullable_base(right);
                bool l2 = type_is_numeric(lb) || lb->kind == TYPE_CHAR ||
                          lb->kind == TYPE_ENUM;
                bool r2 = type_is_numeric(rb) || rb->kind == TYPE_CHAR ||
                          rb->kind == TYPE_ENUM;
                if (l2 && r2) return c->binder->type_bool;
            }
            if (left->kind == TYPE_TYPE_PARAM && right->kind == TYPE_TYPE_PARAM &&
                checker_type_equal(left, right))
                return c->binder->type_bool;
            if (left->kind == TYPE_STRING && right->kind == TYPE_STRING)
                return c->binder->type_bool;
            if ((left->kind == TYPE_CLASS || left->kind == TYPE_STRUCT) &&
                left->sym) {
                const char *rel_name = NULL;
                switch (expr->binary.op) {
                case TK_LESS:       rel_name = "op_lt"; break;
                case TK_GREATER:    rel_name = "op_gt"; break;
                case TK_LESS_EQ:    rel_name = "op_le"; break;
                case TK_GREATER_EQ: rel_name = "op_ge"; break;
                default: break;
                }
                if (rel_name) {
                    zan_istr_t rn = {(char *)rel_name, (int)strlen(rel_name)};
                    if (checker_find_method(left->sym, rn))
                        return c->binder->type_bool;
                }
            }
            if (left->kind != TYPE_ERROR && right->kind != TYPE_ERROR)
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2046,
                              "cannot compare '%s' and '%s' with a relational "
                              "operator: no implicit numeric conversion",
                              type_name(left), type_name(right));
            return c->binder->type_error;
        }

        case TK_AMP_AMP: case TK_PIPE_PIPE:
            if ((left->kind == TYPE_BOOL && right->kind == TYPE_BOOL) ||
                left->kind == TYPE_ERROR || right->kind == TYPE_ERROR)
                return c->binder->type_bool;
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2047,
                          "cannot apply logical operator to '%s' and '%s': "
                          "both operands must be bool",
                          type_name(left), type_name(right));
            return c->binder->type_error;

        case TK_QUESTION_QUESTION: {
            if (left->kind == TYPE_NULLABLE && left->element_type) {
                zan_type_t *merged = merge_conditional_types(c,
                                                              left->element_type,
                                                              right);
                if (merged) return merged;
            } else if (left->is_nullable) {
                zan_type_t *non_null = checker_clone_type(c, left);
                non_null->is_nullable = 0;
                zan_type_t *merged = merge_conditional_types(c, non_null, right);
                if (merged) return merged;
            } else if (checker_type_is_ref(left) || checker_type_is_ref(right) ||
                       right->kind == TYPE_OBJECT ||
                       right->kind == TYPE_NULLABLE) {
                zan_type_t *merged = merge_conditional_types(c, left, right);
                if (merged) return merged;
            }
            if (left->kind != TYPE_ERROR && right->kind != TYPE_ERROR)
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2048,
                              "cannot apply '?' '?' to '%s' and '%s'",
                              type_name(left), type_name(right));
            return c->binder->type_error;
        }

        default:
            return c->binder->type_error;
        }
    }

    case AST_UNARY: {
        zan_type_t *operand = zan_checker_check_expr(c, expr->unary.operand);
        switch (expr->unary.op) {
        case TK_MINUS:
            if (type_is_numeric(operand) || operand->kind == TYPE_CHAR ||
                (operand->kind == TYPE_NULLABLE && operand->element_type &&
                 operand->element_type->kind == TYPE_DECIMAL))
                return operand;
            /* 一元运算符重载：类/结构体上的 static op_sub（1 个形参）。
               重载存不存在由 irgen 判定，这里按二元路径的同一口径把类型
               交给操作数类型（没有重载时 irgen 会报人话诊断）。 */
            if (operand->kind == TYPE_CLASS || operand->kind == TYPE_STRUCT)
                return operand;
            check_unary_operand_type(c, expr, operand, '-');
            return c->binder->type_error;
        case TK_PLUS:
            if (type_is_numeric(operand) || operand->kind == TYPE_CHAR ||
                (operand->kind == TYPE_NULLABLE && operand->element_type &&
                 operand->element_type->kind == TYPE_DECIMAL))
                return operand;
            if (operand->kind == TYPE_CLASS || operand->kind == TYPE_STRUCT)
                return operand;
            check_unary_operand_type(c, expr, operand, '+');
            return c->binder->type_error;
        case TK_BANG:
            /* `!` 的操作数必须是 bool——与二元 `&&` / `||` 的口径一致
               （"cannot apply logical operator to 'X' and 'Y': both operands
               must be bool"）。原来这里无条件返回 bool，`!someString` 就
               静默变成"拿指针和 0 比"的恒假值。 */
            if (operand->kind == TYPE_BOOL) return c->binder->type_bool;
            if (operand->kind == TYPE_ERROR || operand->kind == TYPE_TYPE_PARAM)
                return c->binder->type_bool;
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2049,
                          "cannot apply unary '!' to '%s': the operand must be a bool",
                          type_name(operand));
            return c->binder->type_error;
        case TK_TILDE:
            if (type_is_integral(operand)) return operand;
            check_unary_operand_type(c, expr, operand, '~');
            return c->binder->type_error;
        case TK_PLUS_PLUS: case TK_MINUS_MINUS:
            check_readonly_incdec(c, expr);
            if (type_is_numeric(operand) ||
                (operand->kind == TYPE_NULLABLE && operand->element_type &&
                 operand->element_type->kind == TYPE_DECIMAL)) return operand;
            return c->binder->type_error;
        default:
            return c->binder->type_error;
        }
    }

    case AST_POSTFIX_UNARY: {
        zan_type_t *operand = zan_checker_check_expr(c, expr->unary.operand);
        if (expr->unary.op == TK_BANG) {
            if (!operand) return c->binder->type_error;
            if (operand->kind == TYPE_NULLABLE && operand->element_type) {
                return operand->element_type;
            }
            if (operand->is_nullable) {
                zan_type_t *non_null = checker_clone_type(c, operand);
                non_null->is_nullable = 0;
                return non_null;
            }
            return operand;
        }
        check_readonly_incdec(c, expr);
        if (type_is_numeric(operand) ||
            (operand->kind == TYPE_NULLABLE && operand->element_type &&
             operand->element_type->kind == TYPE_DECIMAL)) return operand;
        return c->binder->type_error;
    }

    case AST_CALL: {
        /* nameof(expr) 编译期常量折叠：取标识符末段拼写为 string */
        if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER &&
            expr->call.callee->ident.name.len == 6 &&
            memcmp(expr->call.callee->ident.name.str, "nameof", 6) == 0 &&
            expr->call.args.count == 1 &&
            !zan_binder_lookup(c->binder, expr->call.callee->ident.name)) {
            expr->call.is_intrinsic = 1;
            zan_ast_node_t *na = expr->call.args.items[0];
            if (na && (na->kind == AST_IDENTIFIER ||
                       na->kind == AST_MEMBER_ACCESS)) {
                zan_checker_check_expr(c, na);
                return c->binder->type_string;
            }
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2050,
                "nameof requires a single identifier or member access");
            return c->binder->type_error;
        }
        /* 零参 SyncStructureAll(Async)：合成 Meta[] 实参（见文件尾
         * checker_synthesize_table_array），之后按普通扩展调用走。 */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 0 &&
            ((expr->call.callee->member.name.len == 16 &&
              memcmp(expr->call.callee->member.name.str, "SyncStructureAll",
                     16) == 0) ||
             (expr->call.callee->member.name.len == 21 &&
              memcmp(expr->call.callee->member.name.str,
                     "SyncStructureAllAsync", 21) == 0))) {
            zan_type_t *srecv = zan_checker_check_expr(
                c, expr->call.callee->member.object);
            if (checker_recv_is_conn(c, srecv)) {
                /* 改写为静态直调 DbExtensions.SyncStructureAll*(db, metas)：
                 * 扩展分发 + await 会踩泛型环境失真（体不执行、结果槽 0，
                 * B-ID63 家族；metrics 直调 SyncAsync 的同一绕开术）——
                 * 扩展就是 this 首参的静态，直调语义等价。 */
                zan_ast_node_t *recvnode = expr->call.callee->member.object;
                zan_istr_t mname = expr->call.callee->member.name;
                zan_ast_node_t *zid = zan_ast_new(c->arena, AST_IDENTIFIER,
                                                  expr->loc);
                zid->ident.name = checker_istr_lit(c->arena, "DbExtensions");
                zid->ident.inst_type_ref = NULL;
                zan_ast_node_t *ncal = zan_ast_new(c->arena, AST_MEMBER_ACCESS,
                                                   expr->loc);
                ncal->member.object = zid;
                ncal->member.name = mname;
                ncal->member.null_cond = 0;
                expr->call.callee = ncal;
                zan_ast_list_init(&expr->call.args);
                zan_ast_list_push(&expr->call.args, recvnode, c->arena);
                zan_ast_list_push(&expr->call.args,
                                  checker_synthesize_table_array(c, expr->loc),
                                  c->arena);
            }
        }
        /* __AttrRoutes.Register(app) 编译期控制器发现：合成 TypeInfo[] 实参
         * 注入为第 2 实参，交给 Zan.Mvc 框架动态约定路由引擎遍历计算；
         * 第 3 实参是 main.c 合成的动作派发器（约定路由的 handler 统一是
         * __AttrRoutes.Dispatch，真正的方法调用按 actionKey 在这里展开成
         * 强类型调用——反射 Invoke 拿不到异步协程句柄，await 不了）。 */
        int route_registration = zan_routes_registration(expr);
        if (route_registration == 1 &&
            (expr->call.args.count == 1 || expr->call.args.count == 2)) {
            if (expr->call.args.count == 1) {
                zan_ast_list_push(&expr->call.args,
                                  checker_synthesize_controllers_array(c, expr->loc),
                                  c->arena);
            }
            zan_ast_list_push(&expr->call.args,
                              checker_synthesize_dispatcher_ref(c, expr->loc, false),
                              c->arena);
        } else if (route_registration == 2 && expr->call.args.count == 1) {
            /* Framework opt-in keeps its own dispatcher; Register(app) must
             * neither discover these controllers nor overwrite this delegate. */
            zan_ast_list_push(&expr->call.args,
                              checker_synthesize_dispatcher_ref(c, expr->loc, true),
                              c->arena);
        }
        zan_type_t *callee_type;
        zan_type_t *recv = NULL;
        int callee_is_method = 0;
        bool typed_overload_changed = false;
        zan_symbol_t *called_sym = NULL;
        int argc = expr->call.args.count;
        zan_type_t **arg_types = argc > 0 ? (zan_type_t **)zan_arena_alloc(
            c->arena, (size_t)argc * sizeof(zan_type_t *)) : NULL;
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_IDENTIFIER &&
            expr->call.args.count == 2 &&
            expr->call.callee->member.name.len == 8 &&
            memcmp(expr->call.callee->member.name.str, "TryParse", 8) == 0) {
            zan_symbol_t *es = zan_binder_lookup(c->binder,
                expr->call.callee->member.object->ident.name);
            if (es && es->kind == SYM_ENUM) {
                for (int ai = 0; ai < 2; ai++)
                    zan_checker_check_expr(c, expr->call.args.items[ai]);
                expr->call.is_intrinsic = 1;
                return c->binder->type_bool;
            }
        }
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            recv = zan_checker_check_expr(c, expr->call.callee->member.object);
            if (recv && recv->sym) {
                zan_symbol_t *m = checker_find_method_call(
                    recv->sym, expr->call.callee->member.name,
                    expr->call.args.count, expr->call.type_args.count);
                if (m && m->kind == SYM_METHOD) {
                    if (!access_member_allowed(c, m)) {
                        report_inaccessible(c, m, expr->call.callee->loc);
                        return c->binder->type_error;
                    }
                    checker_reject_null_receiver(c, expr->call.callee);
                    called_sym = m;
                    callee_type = m->type ? m->type : c->binder->type_error;
                    callee_is_method = 1;
                }
            }
            if (!callee_is_method) {
                /* 标量原语（string/int/…）也要走成员检查：调用一个不存在
                 * 的方法应该在类型检查阶段就报出来，而不是拖到发码阶段
                 * 变成 "code generation failed"（A476）。 */
                callee_type = check_member_access(c, expr->call.callee, recv, true,
                                                 expr);
            }
        } else {
            callee_type = zan_checker_check_expr(c, expr->call.callee);
        }
        zan_symbol_t *arg_sig = call_arg_signature(c, expr, recv);
        int prev_in_call_args = c->in_call_args;
        c->in_call_args = 1;
        for (int i = 0; i < expr->call.args.count; i++) {
            zan_ast_node_t *arg = expr->call.args.items[i];
            if (arg && arg->kind == AST_NAMED_ARG) arg = arg->named_arg.expr;
            zan_type_t *arg_type = zan_checker_check_expr(c, arg);
            arg_types[i] = arg_type;
            if (!arg_sig && arg && arg_type == c->binder->type_void &&
                arg->kind != AST_REF_ARG && !arg_is_spawn_callee(expr)) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, arg->loc, 2051,
                              "cannot use a 'void' value in argument %d: a "
                              "void call has no result",
                              i + 1);
            }
            if (arg_sig && arg)
                check_call_arg_type(c, arg_sig, i, arg, arg_type);
        }
        c->in_call_args = prev_in_call_args;
        /* 内建泛型容器（List<T> / Dict<K,V>）的成员没有 sym，签名拼不到
         * → 实参类型从来没被检查过。单独走一条路（见
         * check_builtin_generic_call_args 的注释）。 */
        if (recv && !arg_sig && expr->call.callee &&
            expr->call.callee->kind == AST_MEMBER_ACCESS) {
            check_builtin_generic_call_args(c, recv,
                expr->call.callee->member.name, expr, arg_types);
            /* 方法组实参裁决：内建成员没有签名路径，单独兜
             * （B-ID151——check_member_access 实参位置静默后的闸）。 */
            checker_builtin_mg_args(c, recv,
                expr->call.callee->member.name, expr);
        }
        if (callee_is_method && recv && recv->sym) {
            zan_symbol_t *typed = checker_find_method_typed(c, recv, recv->sym,
                expr->call.callee->member.name, expr, arg_types);
            if (!typed) {
                called_sym = NULL;
                callee_type = c->binder->type_error;
            } else if (typed != called_sym) {
                if (!access_member_allowed(c, typed)) {
                    report_inaccessible(c, typed, expr->call.callee->loc);
                    return c->binder->type_error;
                }
                called_sym = typed;
                typed_overload_changed = true;
                callee_type = typed->type ? typed->type : c->binder->type_error;
            }
        }
        if (!called_sym && expr->call.callee &&
            expr->call.callee->kind == AST_IDENTIFIER)
            called_sym = zan_binder_lookup(c->binder, expr->call.callee->ident.name);
        if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER &&
            c->current_type_sym &&
            (!called_sym || called_sym->kind == SYM_METHOD)) {
            zan_symbol_t *resolved = checker_find_method_typed(c,
                c->current_type_sym ? c->current_type_sym->type : NULL,
                c->current_type_sym, expr->call.callee->ident.name,
                expr, arg_types);
            if (resolved) {
                called_sym = resolved;
            } else {
                called_sym = NULL;
                callee_type = c->binder->type_error;
            }
        }
        /* 泛型方法调用点的 where 约束（A518）：符号定了就查，别等运行期 */
        if (called_sym && called_sym->kind == SYM_METHOD)
            checker_check_method_call_constraints(c, called_sym, expr,
                                                  arg_types);
        if (!called_sym && expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER) {
            called_sym = zan_binder_lookup(c->binder, expr->call.callee->ident.name);
        }
        if (!called_sym && arg_sig) {
            called_sym = arg_sig;
        }
        c->last_call_node = expr;
        c->last_call_method = called_sym;
        checker_bind_call_intrinsic(c, expr, called_sym, recv);
        check_call_arity(c, expr, recv);
        if (!callee_is_method &&
            (checker_is_task_call(expr, "Run") || checker_is_task_call(expr, "Spawn"))) {
            zan_type_t *tr = task_run_result_type(c, expr, arg_types);
            if (tr) return tr;
        }
        if (!callee_is_method && recv) {
            zan_type_t *br = builtin_call_result_type(c, recv,
                                                      expr->call.callee->member.name);
            if (br) return br;
        }
        if (!callee_is_method && callee_type &&
            callee_type->kind == TYPE_DELEGATE) {
            zan_type_t *ret = callee_type->delegate_ret_type
                ? callee_type->delegate_ret_type
                : c->binder->type_void;
            no_runtime_warn_arc_return(c, expr, ret);
            return ret;
        }
        if (callee_type && (callee_type->kind == TYPE_CLASS ||
                            callee_type->kind == TYPE_STRUCT) && callee_type->sym) {
            zan_istr_t op_name = {(char *)"op_call", 7};
            zan_symbol_t *op = checker_find_method(callee_type->sym, op_name);
            if (op && op->decl && op->decl->kind == AST_METHOD_DECL &&
                op->decl->method_decl.return_type) {
                zan_type_t *ret = zan_binder_resolve_type(c->binder,
                    op->decl->method_decl.return_type);
                no_runtime_warn_arc_return(c, expr, ret);
                return ret;
            }
            if (op && op->type) {
                no_runtime_warn_arc_return(c, expr, op->type);
                return op->type;
            }
        }
        /* 解析作用域内被调函数/方法的返回类型 */
        if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER) {
            zan_symbol_t *fsym = called_sym
                ? called_sym
                : zan_binder_lookup(c->binder, expr->call.callee->ident.name);
            if (fsym && (fsym->kind == SYM_METHOD) && fsym->decl) {
                zan_ast_node_t *m = fsym->decl;
                if (m->method_decl.return_type) {
                    zan_type_t *ret = zan_binder_resolve_type(
                        c->binder, m->method_decl.return_type);
                    no_runtime_warn_arc_return(c, expr, ret);
                    return ret;
                }
                return c->binder->type_void;
            }
        }
        if (called_sym && called_sym->decl &&
            called_sym->decl->kind == AST_METHOD_DECL &&
            expr->call.type_args.count > 0) {
            zan_ast_node_t *m = called_sym->decl;
            zan_ast_list_t *tps = &m->method_decl.type_params;
            if (tps->count == expr->call.type_args.count && m->method_decl.return_type) {
                zan_type_t *ret = zan_binder_resolve_type(c->binder, m->method_decl.return_type);
                zan_type_t *targs[8] = { NULL };
                int n = expr->call.type_args.count < 8 ? expr->call.type_args.count : 8;
                for (int i = 0; i < n; i++) {
                    targs[i] = zan_binder_resolve_type(c->binder, expr->call.type_args.items[i]);
                }
                /* Close receiver parameters except names shadowed by the
                 * method's own parameters, then substitute method arguments. */
                if (recv && recv->type_args && recv->type_arg_count > 0 &&
                    recv->sym && recv->sym->decl &&
                    (recv->sym->decl->kind == AST_CLASS_DECL ||
                     recv->sym->decl->kind == AST_STRUCT_DECL)) {
                    zan_ast_list_t *class_tps =
                        &recv->sym->decl->type_decl.type_params;
                    if (class_tps->count == recv->type_arg_count) {
                        zan_type_t **class_args = zan_arena_alloc(c->arena,
                            sizeof(*class_args) * (size_t)class_tps->count);
                        for (int i = 0; i < class_tps->count; i++) {
                            class_args[i] = recv->type_args[i];
                            zan_istr_t name = class_tps->items[i]->ident.name;
                            for (int j = 0; j < tps->count; j++) {
                                zan_istr_t method_name = tps->items[j]->ident.name;
                                if (name.len == method_name.len &&
                                    memcmp(name.str, method_name.str, name.len) == 0) {
                                    class_args[i] = NULL;
                                    break;
                                }
                            }
                        }
                        ret = zan_binder_subst_named(c->binder, ret, class_tps,
                                                     class_args);
                    }
                }
                ret = zan_binder_subst_named(c->binder, ret, tps, targs);
                no_runtime_warn_arc_return(c, expr, ret);
                return ret;
            }
        }

        /* Generic receivers have no call_arg_signature. Type their selected
         * method even when it is the initial overload, and close its return
         * type using the type arguments of its declaring class or struct. */
        zan_ast_list_t *recv_tps = NULL;
        if (callee_is_method && recv && recv->type_arg_count > 0 &&
            called_sym && called_sym->parent == recv->sym && recv->sym->decl &&
            (recv->sym->decl->kind == AST_CLASS_DECL ||
             recv->sym->decl->kind == AST_STRUCT_DECL)) {
            zan_ast_list_t *tps = &recv->sym->decl->type_decl.type_params;
            if (tps->count == recv->type_arg_count)
                recv_tps = tps;
        }
        if (called_sym && called_sym->decl && called_sym->decl->kind == AST_METHOD_DECL &&
            (typed_overload_changed ||
             (recv_tps && called_sym->decl->method_decl.type_params.count == 0))) {
            zan_ast_node_t *m = called_sym->decl;
            zan_type_t *ret = m->method_decl.return_type
                ? zan_binder_resolve_type(c->binder, m->method_decl.return_type)
                : c->binder->type_void;
            if (recv_tps)
                ret = zan_binder_subst_named(c->binder, ret, recv_tps,
                                             recv->type_args);
            no_runtime_warn_arc_return(c, expr, ret);
            return ret;
        }
        if (arg_sig && arg_sig->decl &&
            arg_sig->decl->kind == AST_METHOD_DECL) {
            if (arg_sig->decl->method_decl.return_type) {
                if (expr_is_this_call(c, arg_sig, recv)) {
                    no_runtime_warn_arc_return(c, expr, recv);
                    return recv;
                }
                zan_type_t *ret = zan_binder_resolve_type(
                    c->binder, arg_sig->decl->method_decl.return_type);
                no_runtime_warn_arc_return(c, expr, ret);
                return ret;
            }
            no_runtime_warn_arc_return(c, expr, c->binder->type_void);
            return c->binder->type_void;
        }
        no_runtime_warn_arc_return(c, expr, callee_is_method ? callee_type : NULL);
        return c->binder->type_error;
    }

    case AST_STRING_INTERP: {
        no_runtime_reject(c, expr->loc, "string interpolation");
        for (int i = 0; i < expr->string_interp.parts.count; i++) {
            zan_ast_node_t *part = expr->string_interp.parts.items[i];
            zan_ast_node_t *raw_part = part;
            while (raw_part && raw_part->kind == AST_CAST_EXPR)
                raw_part = raw_part->cast.expr;
            zan_type_t *pt = zan_checker_check_expr(c, part);
            if (raw_part && raw_part->kind == AST_NULL_LITERAL)
                continue;
            if (!type_is_concatable(pt)) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2052,
                              "cannot interpolate '%s' -- convert the operand explicitly instead of formatting the reference",
                              type_name(pt));
            }
        }
        return c->binder->type_string;
    }

    case AST_MEMBER_ACCESS:
        return check_member_access(c, expr,
                                   zan_checker_check_expr(c, expr->member.object),
                                   false, NULL);

    case AST_INDEX: {
        zan_type_t *obj = zan_checker_check_expr(c, expr->index.object);
        zan_type_t *idx = zan_checker_check_expr(c, expr->index.index);
        if ((obj->kind == TYPE_CLASS || obj->kind == TYPE_STRUCT) && obj->sym) {
            zan_istr_t op_name = {(char *)"op_index", 8};
            zan_symbol_t *op = NULL;
            int op_count = 0;
            for (zan_symbol_t *ts = obj->sym; ts;
                 ts = (ts->type && ts->type->base_type)
                     ? ts->type->base_type->sym : NULL) {
                for (int mi = 0; mi < ts->member_count; mi++) {
                    zan_symbol_t *mm = ts->members[mi];
                    if (mm && mm->kind == SYM_METHOD &&
                        mm->name.len == op_name.len &&
                        memcmp(mm->name.str, op_name.str,
                               (size_t)op_name.len) == 0) {
                        if (!op) op = mm;
                        op_count++;
                    }
                }
            }
            if (op_count == 1 && op && op->decl &&
                op->decl->kind == AST_METHOD_DECL &&
                op->decl->method_decl.return_type) {
                /* 索引器形参与实参类型匹配校验 */
                zan_ast_list_t *ps = &op->decl->method_decl.params;
                bool is_static = (op->decl->method_decl.modifiers &
                                  MOD_STATIC) != 0;
                int self_off = is_static ? 1 : 0;
                int given = 1 + expr->index.extra.count;
                int want = ps->count - self_off;
                bool any_generic = false;
                for (int pi = self_off; pi < ps->count; pi++) {
                    zan_ast_node_t *pp = ps->items[pi];
                    if (pp && pp->kind == AST_PARAM && pp->param.type) {
                        zan_type_t *pt =
                            zan_binder_resolve_type(c->binder, pp->param.type);
                        if (pt && type_refs_type_param(pt)) any_generic = true;
                    }
                }
                if (!any_generic && ps->count >= self_off && want != given) {
                    zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2053,
                        "'%.*s' indexer takes %d index%s, but %d given",
                        (int)obj->sym->name.len, obj->sym->name.str,
                        want, want == 1 ? "" : "es", given);
                    return c->binder->type_error;
                }
                if (!any_generic && idx && idx->kind != TYPE_ERROR &&
                    ps->count > self_off) {
                    zan_ast_node_t *p0 = ps->items[self_off];
                    if (p0 && p0->kind == AST_PARAM && p0->param.type) {
                        zan_type_t *pt0 =
                            zan_binder_resolve_type(c->binder, p0->param.type);
                        if (pt0 && !type_refs_type_param(pt0) &&
                            !checker_type_assignable(pt0, idx)) {
                            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2054,
                                "no '%.*s' indexer takes an index of type "
                                "'%.*s'",
                                (int)obj->sym->name.len, obj->sym->name.str,
                                (int)idx->name.len, idx->name.str);
                            return c->binder->type_error;
                        }
                    }
                }
                zan_type_t *ret_ty = zan_binder_resolve_type(c->binder,
                    op->decl->method_decl.return_type);
                if (obj->type_args && obj->type_arg_count > 0 && obj->sym && obj->sym->decl &&
                    (obj->sym->decl->kind == AST_CLASS_DECL || obj->sym->decl->kind == AST_STRUCT_DECL)) {
                    zan_ast_list_t *tps = &obj->sym->decl->type_decl.type_params;
                    if (tps->count == obj->type_arg_count) {
                        ret_ty = zan_binder_subst_named(c->binder, ret_ty, tps, obj->type_args);
                    }
                }
                return ret_ty;
            }
            if (op && op_count > 0 && op->decl &&
                op->decl->kind == AST_METHOD_DECL &&
                op->decl->method_decl.return_type) {
                zan_type_t *ret_ty = zan_binder_resolve_type(c->binder,
                    op->decl->method_decl.return_type);
                if (obj->type_args && obj->type_arg_count > 0 && obj->sym && obj->sym->decl &&
                    (obj->sym->decl->kind == AST_CLASS_DECL || obj->sym->decl->kind == AST_STRUCT_DECL)) {
                    zan_ast_list_t *tps = &obj->sym->decl->type_decl.type_params;
                    if (tps->count == obj->type_arg_count) {
                        ret_ty = zan_binder_subst_named(c->binder, ret_ty, tps, obj->type_args);
                    }
                }
                return ret_ty;
            }
            if (op && op->type) return op->type;
        }
        if (obj->kind == TYPE_ARRAY && obj->element_type) {
            int rank = obj->array_rank > 1 ? obj->array_rank : 1;
            int given = 1 + expr->index.extra.count;
            if (given != rank) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2055,
                    "rank-%d array indexed with %d indices", rank, given);
            }
            for (int i = 0; i < expr->index.extra.count; i++)
                zan_checker_check_expr(c, expr->index.extra.items[i]);
            return obj->element_type;
        }
        if (obj->kind == TYPE_STRING) {
            for (int i = 0; i < expr->index.extra.count; i++)
                zan_checker_check_expr(c, expr->index.extra.items[i]);
            return c->binder->type_char;
        }
        if (obj->kind == TYPE_STRUCT && obj->name.len == 4 &&
            memcmp(obj->name.str, "Span", 4) == 0) {
            for (int i = 0; i < expr->index.extra.count; i++)
                zan_checker_check_expr(c, expr->index.extra.items[i]);
            return (obj->type_args && obj->type_arg_count > 0)
                ? obj->type_args[0] : c->binder->type_int;
        }
        return c->binder->type_error;
    }

    case AST_ASSIGNMENT: {
        zan_type_t *left = zan_checker_check_expr(c, expr->binary.left);
        /* 右值有赋值目标做委托裁决（事件 += 处理器等）：方法组在此
         * 静默，合法性由下面 check_assignable 判（B-ID151）。 */
        int prev_assign = c->in_assignable_ctx;
        c->in_assignable_ctx = 1;
        zan_type_t *right = zan_checker_check_expr(c, expr->binary.right);
        c->in_assignable_ctx = prev_assign;
        check_readonly_assignment(c, expr);
        zan_type_t *target = checker_assignment_target_type(c, expr->binary.left, left);
        if (!target && expr->binary.left->kind == AST_INDEX) {
            target = checker_index_set_target(c, expr->binary.left, right);
            if (!target) {
                zan_type_t *iobj = zan_checker_check_expr(
                    c, expr->binary.left->index.object);
                if (iobj && iobj->sym &&
                    (iobj->kind == TYPE_CLASS || iobj->kind == TYPE_STRUCT)) {
                    zan_istr_t set_name = {(char *)"op_index_set", 12};
                    bool has_set = false;
                    for (int mi = 0; mi < iobj->sym->member_count; mi++) {
                        zan_symbol_t *mm = iobj->sym->members[mi];
                        if (mm && mm->kind == SYM_METHOD &&
                            mm->name.len == set_name.len &&
                            memcmp(mm->name.str, set_name.str,
                                   (size_t)set_name.len) == 0) {
                            has_set = true;
                            break;
                        }
                    }
                    if (has_set) {
                        zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2056,
                            "no '%.*s' indexer accepts this assignment",
                            (int)iobj->sym->name.len, iobj->sym->name.str);
                    }
                }
            }
        }
        if (!target) target = left;
        if (target)
            checker_check_assignable(c, target, right, expr->binary.right,
                                     expr->loc, "assignment");
        check_closure_cycle_warning(c, expr->binary.left, expr->binary.right, expr->loc);
        return right;
    }

