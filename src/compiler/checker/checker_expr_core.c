struct checker_byref_parameter {
  struct checker_local *slot;
  zan_ast_node_t *param;
  int lambda_depth;
  bool reported;
  struct checker_byref_parameter *next;
};

static void checker_bind_byref_parameter(zan_checker_t *c,
                                         zan_ast_node_t *param) {
  if (!param->param.by_ref)
    return;
  struct checker_local *slot = checker_find_local_slot(c, param->param.name);
  if (!slot)
    return;
  struct checker_byref_parameter *binding =
      zan_arena_alloc(c->arena, sizeof(*binding));
  binding->slot = slot;
  binding->param = param;
  binding->lambda_depth = c->lambda_depth;
  binding->reported = false;
  binding->next = c->byref_parameters;
  c->byref_parameters = binding;
}

static void checker_reject_byref_capture(zan_checker_t *c, zan_istr_t name) {
  if (!c->byref_parameters || !c->lambda_depth)
    return;
  struct checker_local *slot = checker_find_local_slot(c, name);
  for (struct checker_byref_parameter *p = c->byref_parameters; p;
       p = p->next) {
    /* Compare bindings, not names: lambda parameters and locals may shadow. */
    if (p->slot != slot || p->lambda_depth >= c->lambda_depth)
      continue;
    if (!p->reported) {
      zan_diag_emit_code(c->diag, DIAG_ERROR, p->param->loc, 3043,
                         "cannot capture a ref or out parameter in a lambda");
      p->reported = true;
    }
    return;
  }
}

zan_type_t *zan_checker_check_expr(zan_checker_t *c, zan_ast_node_t *expr) {
  if (!expr)
    return c->binder->type_error;

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
      if ((expr->lit_radix == 2 || expr->lit_radix == 8 ||
           expr->lit_radix == 16) &&
          expr->int_val >= 0 && expr->int_val <= 4294967295LL) {
        return c->binder->type_int;
      }
      if (expr->int_val < -2147483648LL || expr->int_val > 2147483647LL)
        return c->binder->type_long;
      return c->binder->type_int;
    }
  case AST_FLOAT_LITERAL:
    /* f/F 后缀字面量按 float 语义定型(C# 同规):无后缀仍为 double */
    return expr->lit_suffix ? c->binder->type_float : c->binder->type_double;
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

  case AST_REF_ARG: {
    /* A ref/out use inside a lambda still captures its storage. An inline
     * out declaration instead introduces a new, potentially shadowing local. */
    zan_ast_node_t *place = expr->ref_arg.expr;
    if (expr->ref_arg.decl_type && place && place->kind == AST_IDENTIFIER) {
      zan_type_t *dt =
          zan_binder_resolve_type(c->binder, expr->ref_arg.decl_type);
      /* 缓存引用体类型：ref/out 实参的精确类型校验在
       * check_call_arg_type 里做（check_call_arg_type 拿不到
       * place 的推导结果——REF_ARG 整体定型为 error）。 */
      expr->ref_arg.place_type = dt;
      checker_add_local_permissive(c, place->ident.name, dt, expr);
    } else {
      expr->ref_arg.place_type = zan_checker_check_expr(c, place);
    }
    return c->binder->type_error;
  }

  case AST_IDENTIFIER: {
    if (expr->ident.inst_type_ref)
      return zan_binder_resolve_type(c->binder, expr->ident.inst_type_ref);
    if (expr->ident.is_type_receiver) {
      zan_symbol_t *ts = zan_binder_type_receiver(c->binder, expr);
      zan_binder_record_reference(c->binder, ts, expr);
      return ts && ts->type ? ts->type : c->binder->type_error;
    }
    if (c->lexical_ref)
      checker_record_lexical_ref(c, expr);
    checker_reject_byref_capture(c, expr->ident.name);
    zan_type_t *lt = checker_find_local(c, expr->ident.name);
    if (lt && c->lambda_depth > 0 && !c->in_noescape_lambda) {
      struct checker_local *ne_loc = checker_find_local_slot(c, expr->ident.name);
      if (ne_loc && ne_loc->decl && ne_loc->decl->kind == AST_PARAM &&
          ne_loc->decl->param.is_noescape) {
        zan_diag_emit_code(
            c->diag, DIAG_ERROR, expr->loc, 2142,
            "non-escaping parameter '%.*s' cannot be captured by an escaping lambda",
            (int)expr->ident.name.len, expr->ident.name.str);
      } else if (ne_loc && ne_loc->lambda_depth < c->lambda_depth) {
        if (checker_type_is_noncopyable(lt)) {
          zan_diag_emit_code(
              c->diag, DIAG_ERROR, expr->loc, 2153,
              "non-copyable (~Copyable) value '%.*s' cannot be captured by an "
              "escaping lambda",
              (int)expr->ident.name.len, expr->ident.name.str);
        } else if (checker_type_is_ref_struct(lt)) {
          zan_diag_emit_code(
              c->diag, DIAG_ERROR, expr->loc, 2157,
              "stack-only ref struct '%.*s' cannot be captured by an escaping "
              "lambda",
              (int)expr->ident.name.len, expr->ident.name.str);
        }
      }
    }
    if (c->binder->symbol_reference) {
      struct checker_local *local =
          checker_lookup_local(c, expr->ident.name, true);
      if (local && !local->type)
        c->last_unproven_reference_expr = expr;
    }
    if (lt)
      return lt;
    if (c->current_type_sym) {
      zan_symbol_t *fsym =
          checker_find_field(c->current_type_sym, expr->ident.name);
      if (fsym) {
        checker_record_bare_field(c, fsym, expr);
        return fsym->type ? fsym->type : c->binder->type_error;
      }
    }
    /* Const generic parameter in expression position (e.g. `for (int i = 0; i < N; i++)`) */
    if (c->current_method_decl &&
        c->current_method_decl->kind == AST_METHOD_DECL) {
      zan_ast_list_t *mtps = &c->current_method_decl->method_decl.type_params;
      for (int ti = 0; ti < mtps->count; ti++) {
        zan_ast_node_t *tp = mtps->items[ti];
        if (tp && tp->kind == AST_IDENTIFIER && tp->ident.is_const_param &&
            tp->ident.name.len == expr->ident.name.len &&
            memcmp(tp->ident.name.str, expr->ident.name.str,
                   (size_t)tp->ident.name.len) == 0)
          return c->binder->type_int;
      }
    }
    if (c->current_type_sym && c->current_type_sym->decl &&
        (c->current_type_sym->decl->kind == AST_CLASS_DECL ||
         c->current_type_sym->decl->kind == AST_STRUCT_DECL ||
         c->current_type_sym->decl->kind == AST_INTERFACE_DECL)) {
      zan_ast_list_t *ttps = &c->current_type_sym->decl->type_decl.type_params;
      for (int ti = 0; ti < ttps->count; ti++) {
        zan_ast_node_t *tp = ttps->items[ti];
        if (tp && tp->kind == AST_IDENTIFIER && tp->ident.is_const_param &&
            tp->ident.name.len == expr->ident.name.len &&
            memcmp(tp->ident.name.str, expr->ident.name.str,
                   (size_t)tp->ident.name.len) == 0)
          return c->binder->type_int;
      }
    }
    zan_symbol_t *sym = zan_binder_lookup(c->binder, expr->ident.name);
    if (!sym) {
      return c->binder->type_error;
    }
    if (c->binder->symbol_reference &&
        expr != c->last_unproven_reference_expr) {
      if (checker_is_type_symbol(sym))
        zan_binder_record_reference(c->binder, sym, expr);
      else if (sym->kind == SYM_FIELD || sym->kind == SYM_PROPERTY)
        checker_record_bare_field(c, sym, expr);
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
        zan_ast_node_t *other_node =
            left->kind == TYPE_STRING ? expr->binary.right : expr->binary.left;
        zan_ast_node_t *raw_other = other_node;
        while (raw_other && raw_other->kind == AST_CAST_EXPR)
          raw_other = raw_other->cast.expr;
        if (raw_other && raw_other->kind == AST_NULL_LITERAL) {
          no_runtime_reject(c, expr->loc, "string concatenation");
          no_alloc_reject(c, expr->loc, "string concatenation");
          return c->binder->type_string;
        }
        zan_type_t *other = left->kind == TYPE_STRING ? right : left;
        if (!type_is_concatable(other)) {
          zan_diag_emit_code(
              c->diag, DIAG_ERROR, expr->loc, 2040,
              "cannot concatenate 'string' and '%s' -- convert the operand "
              "explicitly (e.g. call .ToStr()/read the field) instead of "
              "concatenating the reference",
              type_name(other));
          return c->binder->type_error;
        }
        no_runtime_reject(c, expr->loc, "string concatenation");
        no_alloc_reject(c, expr->loc, "string concatenation");
        return c->binder->type_string;
      }
    case TK_MINUS:
    case TK_STAR:
    case TK_SLASH:
    case TK_PERCENT:
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
          zan_diag_emit_code(
              c->diag, DIAG_ERROR, expr->loc, 2042,
              "delegates are single-cast: '+='/'-=' combination is not "
              "supported -- assign directly (`d = h`) or subscribe on a "
              "multicast event type (`UiEvent`, `e += h`)");
        else
          zan_diag_emit_code(
              c->diag, DIAG_ERROR, expr->loc, 2043,
              "cannot apply operator to '%s' and '%s'",
              left->kind == TYPE_ERROR ? "lambda" : type_name(left),
              right->kind == TYPE_ERROR ? "lambda" : type_name(right));
        return c->binder->type_error;
      }
      if (type_is_numeric(left) && type_is_numeric(right)) {
        return promote_numeric(c->binder, left, right);
      }
      if (left->kind == TYPE_STRUCT && left->name.len == 3 &&
          memcmp(left->name.str, "Vec", 3) == 0 &&
          checker_type_equal(left, right)) {
        return left;
      }
      if (left->kind == TYPE_NULLABLE || right->kind == TYPE_NULLABLE) {
        zan_type_t *lb = checker_nullable_base(left);
        zan_type_t *rb = checker_nullable_base(right);
        if ((lb->kind == TYPE_DECIMAL || rb->kind == TYPE_DECIMAL) &&
            type_is_numeric(lb) && type_is_numeric(rb)) {
          if (lb->kind == TYPE_FLOAT || lb->kind == TYPE_DOUBLE ||
              rb->kind == TYPE_FLOAT || rb->kind == TYPE_DOUBLE) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2041,
                               "cannot apply operator to nullable decimal and "
                               "floating-point operands -- convert explicitly");
            return c->binder->type_error;
          }
          return zan_binder_make_nullable_type(c->binder,
                                               c->binder->type_decimal);
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
      if (left->kind == TYPE_TYPE_PARAM && right->kind == TYPE_TYPE_PARAM &&
          checker_type_equal(left, right)) {
        const char *op_name = NULL;
        switch (expr->binary.op) {
        case TK_PLUS:    op_name = "op_add"; break;
        case TK_MINUS:   op_name = "op_sub"; break;
        case TK_STAR:    op_name = "op_mul"; break;
        case TK_SLASH:   op_name = "op_div"; break;
        case TK_PERCENT: op_name = "op_mod"; break;
        default: break;
        }
        zan_type_t *op_ret = NULL;
        if (op_name && checker_tp_has_operator(c, left, op_name, 2, &op_ret))
          return op_ret ? op_ret : left;
      }
      if (left->kind != TYPE_ERROR && right->kind != TYPE_ERROR) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2043,
                           "cannot apply operator to '%s' and '%s'",
                           type_name(left), type_name(right));
      }
      return c->binder->type_error;

    case TK_AMP:
    case TK_PIPE:
    case TK_CARET:
    case TK_LESS_LESS:
    case TK_GREATER_GREATER:
    case TK_GREATER_GREATER_GREATER:
      if ((type_is_integral(left) || left->kind == TYPE_CHAR ||
           left->kind == TYPE_ENUM) &&
          (type_is_integral(right) || right->kind == TYPE_CHAR ||
           right->kind == TYPE_ENUM)) {
        return promote_numeric(c->binder, left, right);
      }
      if (left->kind == TYPE_STRUCT && left->name.len == 3 &&
          memcmp(left->name.str, "Vec", 3) == 0 &&
          (checker_type_equal(left, right) ||
           ((expr->binary.op == TK_LESS_LESS ||
             expr->binary.op == TK_GREATER_GREATER ||
             expr->binary.op == TK_GREATER_GREATER_GREATER) &&
            type_is_integral(right)))) {
        return left;
      }
      return c->binder->type_error;

    case TK_EQ_EQ:
    case TK_BANG_EQ: {
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
        if (l2 && r2)
          return c->binder->type_bool;
        if (lb->kind == TYPE_BOOL && rb->kind == TYPE_BOOL)
          return c->binder->type_bool;
        if (checker_type_assignable(lb, rb) || checker_type_assignable(rb, lb))
          return c->binder->type_bool;
      }
      if (left->kind == TYPE_STRUCT && right->kind == TYPE_STRUCT &&
          left->sym && left->sym == right->sym)
        return c->binder->type_bool;
      if (left->kind == TYPE_TYPE_PARAM && right->kind == TYPE_TYPE_PARAM &&
          checker_type_equal(left, right))
        return c->binder->type_bool;
      if ((left->kind == TYPE_TYPE_PARAM &&
           (right->kind == TYPE_OBJECT ||
            expr_is_null_literal(expr->binary.right))) ||
          (right->kind == TYPE_TYPE_PARAM &&
           (left->kind == TYPE_OBJECT ||
            expr_is_null_literal(expr->binary.left))))
        return c->binder->type_bool;
      /* 单侧 Binding<T> 的比较会落到 object==object 的引用比较，恒为
       * false（live 绑定每次取值都是新对象语义）——当场拒绝并指引
       * .Get()。Binding==null（判绑定对象本身）与 Binding==Binding
       * 照常放行。 */
      if (left->kind != TYPE_ERROR && right->kind != TYPE_ERROR &&
          !expr_is_null_literal(expr->binary.left) &&
          !expr_is_null_literal(expr->binary.right)) {
        zan_type_t *bnd = checker_type_is_binding(left)    ? left
                          : checker_type_is_binding(right) ? right
                                                           : NULL;
        zan_type_t *oth = bnd == left ? right : left;
        if (bnd && oth->kind != TYPE_OBJECT && oth->kind != TYPE_TYPE_PARAM &&
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
                           "cannot compare '%s' and '%s'", type_name(left),
                           type_name(right));
      return c->binder->type_error;
    }
    case TK_LESS:
    case TK_GREATER:
    case TK_LESS_EQ:
    case TK_GREATER_EQ: {
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
        if (l2 && r2)
          return c->binder->type_bool;
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
        case TK_LESS:
          rel_name = "op_lt";
          break;
        case TK_GREATER:
          rel_name = "op_gt";
          break;
        case TK_LESS_EQ:
          rel_name = "op_le";
          break;
        case TK_GREATER_EQ:
          rel_name = "op_ge";
          break;
        default:
          break;
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

    case TK_AMP_AMP:
    case TK_PIPE_PIPE:
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
        zan_type_t *merged =
            merge_conditional_types(c, left->element_type, right);
        if (merged)
          return merged;
      } else if (left->is_nullable) {
        zan_type_t *non_null = checker_clone_type(c, left);
        non_null->is_nullable = 0;
        zan_type_t *merged = merge_conditional_types(c, non_null, right);
        if (merged)
          return merged;
      } else if (checker_type_is_ref(left) || checker_type_is_ref(right) ||
                 right->kind == TYPE_OBJECT || right->kind == TYPE_NULLABLE) {
        zan_type_t *merged = merge_conditional_types(c, left, right);
        if (merged)
          return merged;
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
      if (operand->kind == TYPE_TYPE_PARAM) {
        const char *uop = expr->unary.op == TK_MINUS ? "op_sub" : "op_add";
        zan_type_t *uret = NULL;
        if (checker_tp_has_operator(c, operand, uop, 1, &uret))
          return uret ? uret : operand;
      }
      check_unary_operand_type(c, expr, operand, '+');
      return c->binder->type_error;
    case TK_BANG:
      /* `!` 的操作数必须是 bool——与二元 `&&` / `||` 的口径一致
         （"cannot apply logical operator to 'X' and 'Y': both operands
         must be bool"）。原来这里无条件返回 bool，`!someString` 就
         静默变成"拿指针和 0 比"的恒假值。 */
      if (operand->kind == TYPE_BOOL)
        return c->binder->type_bool;
      if (operand->kind == TYPE_ERROR || operand->kind == TYPE_TYPE_PARAM)
        return c->binder->type_bool;
      zan_diag_emit_code(
          c->diag, DIAG_ERROR, expr->loc, 2049,
          "cannot apply unary '!' to '%s': the operand must be a bool",
          type_name(operand));
      return c->binder->type_error;
    case TK_TILDE:
      if (type_is_integral(operand) ||
          (operand->kind == TYPE_STRUCT && operand->name.len == 3 &&
           memcmp(operand->name.str, "Vec", 3) == 0))
        return operand;
      check_unary_operand_type(c, expr, operand, '~');
      return c->binder->type_error;
    case TK_PLUS_PLUS:
    case TK_MINUS_MINUS:
      check_readonly_incdec(c, expr);
      if (type_is_numeric(operand) ||
          (operand->kind == TYPE_NULLABLE && operand->element_type &&
           operand->element_type->kind == TYPE_DECIMAL))
        return operand;
      return c->binder->type_error;
    default:
      return c->binder->type_error;
    }
  }

  case AST_POSTFIX_UNARY: {
    zan_type_t *operand = zan_checker_check_expr(c, expr->unary.operand);
    if (expr->unary.op == TK_BANG) {
      if (!operand)
        return c->binder->type_error;
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
    if (expr->unary.op == TK_QUESTION) {
      if (!operand || operand == c->binder->type_error)
        return c->binder->type_error;
      bool is_result = operand->kind == TYPE_STRUCT &&
                       operand->type_arg_count == 2 &&
                       operand->name.len == 6 &&
                       memcmp(operand->name.str, "Result", 6) == 0;
      if (!is_result) {
        zan_diag_emit_code(
            c->diag, DIAG_ERROR, expr->loc, 2166,
            "operator '?' requires a 'Result<T, E>' operand, got '%s'",
            type_name(operand));
        return c->binder->type_error;
      }
      zan_type_t *ret_t = c->current_return_type;
      if (ret_t && ret_t->kind == TYPE_TASK && ret_t->type_arg_count == 1)
        ret_t = ret_t->type_args[0];
      bool ret_is_result = ret_t && ret_t->kind == TYPE_STRUCT &&
                           ret_t->type_arg_count == 2 &&
                           ret_t->name.len == 6 &&
                           memcmp(ret_t->name.str, "Result", 6) == 0;
      if (!ret_is_result ||
          !checker_type_equal(ret_t->type_args[1], operand->type_args[1])) {
        zan_diag_emit_code(
            c->diag, DIAG_ERROR, expr->loc, 2167,
            "operator '?' can only be used in a method returning "
            "'Result<_, %s>' (current return type is '%s')",
            type_name(operand->type_args[1]),
            ret_t ? type_name(ret_t) : "void");
      }
      return operand->type_args[0];
    }
    check_readonly_incdec(c, expr);
    if (type_is_numeric(operand) ||
        (operand->kind == TYPE_NULLABLE && operand->element_type &&
         operand->element_type->kind == TYPE_DECIMAL))
      return operand;
    return c->binder->type_error;
  }

  case AST_CALL: {
    if (c->in_noalloc && expr->call.callee &&
        expr->call.callee->kind == AST_MEMBER_ACCESS &&
        expr->call.callee->member.object &&
        expr->call.callee->member.object->kind == AST_IDENTIFIER) {
      zan_istr_t on = expr->call.callee->member.object->ident.name;
      if (on.len == 7 && memcmp(on.str, "Console", 7) == 0 &&
          !checker_find_local(c, on)) {
        zan_diag_emit_code(
            c->diag, DIAG_ERROR, expr->loc, 3100,
            "[NoAlloc] method cannot call Console.%.*s",
            (int)expr->call.callee->member.name.len,
            expr->call.callee->member.name.str);
      }
    }
    /* nameof(expr) 编译期常量折叠：取标识符末段拼写为 string */
    if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER &&
        expr->call.callee->ident.name.len == 6 &&
        memcmp(expr->call.callee->ident.name.str, "nameof", 6) == 0 &&
        expr->call.args.count == 1 &&
        !zan_binder_lookup(c->binder, expr->call.callee->ident.name)) {
      expr->call.is_intrinsic = 1;
      zan_ast_node_t *na = expr->call.args.items[0];
      if (na && (na->kind == AST_IDENTIFIER || na->kind == AST_MEMBER_ACCESS)) {
        zan_checker_check_expr(c, na);
        return c->binder->type_string;
      }
      zan_diag_emit_code(
          c->diag, DIAG_ERROR, expr->loc, 2050,
          "nameof requires a single identifier or member access");
      return c->binder->type_error;
    }
    /* comptime(expr): compile-time evaluation folded to constant / .rodata */
    if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER &&
        expr->call.callee->ident.name.len == 8 &&
        memcmp(expr->call.callee->ident.name.str, "comptime", 8) == 0 &&
        expr->call.args.count == 1 &&
        !zan_binder_lookup(c->binder, expr->call.callee->ident.name) &&
        !checker_find_local(c, expr->call.callee->ident.name)) {
      expr->call.is_intrinsic = 1;
      return zan_checker_check_expr(c, expr->call.args.items[0]);
    }
    /* static_assert(cond [, "message"]) compile-time assertion */
    if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER &&
        expr->call.callee->ident.name.len == 13 &&
        memcmp(expr->call.callee->ident.name.str, "static_assert", 13) == 0 &&
        !zan_binder_lookup(c->binder, expr->call.callee->ident.name) &&
        !checker_find_local(c, expr->call.callee->ident.name)) {
      if (expr->call.args.count < 1 || expr->call.args.count > 2) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                           "static_assert expects 1 or 2 arguments, got %d",
                           expr->call.args.count);
        return c->binder->type_void;
      }
      zan_type_t *ct = zan_checker_check_expr(c, expr->call.args.items[0]);
      if (ct && ct->kind != TYPE_BOOL && ct->kind != TYPE_ERROR) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, expr->call.args.items[0]->loc,
                           2040, "static_assert condition must be 'bool', got '%s'",
                           type_name(ct));
      }
      if (expr->call.args.count == 2) {
        zan_type_t *mt = zan_checker_check_expr(c, expr->call.args.items[1]);
        if (mt && mt->kind != TYPE_STRING && mt->kind != TYPE_ERROR) {
          zan_diag_emit_code(c->diag, DIAG_ERROR, expr->call.args.items[1]->loc,
                             2040, "static_assert message must be 'string', got '%s'",
                             type_name(mt));
        }
      }
      expr->call.is_intrinsic = 1;
      return c->binder->type_void;
    }
    /* likely(cond) / unlikely(cond) / Hint.Likely(cond) / Hint.Unlikely(cond) */
    if (expr->call.args.count == 1 && expr->call.callee) {
      bool is_branch_hint = false;
      if (expr->call.callee->kind == AST_IDENTIFIER) {
        zan_istr_t bn = expr->call.callee->ident.name;
        if (((bn.len == 6 && memcmp(bn.str, "likely", 6) == 0) ||
             (bn.len == 8 && memcmp(bn.str, "unlikely", 8) == 0)) &&
            !zan_binder_lookup(c->binder, bn) && !checker_find_local(c, bn))
          is_branch_hint = true;
      } else if (expr->call.callee->kind == AST_MEMBER_ACCESS &&
                 expr->call.callee->member.object &&
                 expr->call.callee->member.object->kind == AST_IDENTIFIER &&
                 expr->call.callee->member.object->ident.name.len == 4 &&
                 memcmp(expr->call.callee->member.object->ident.name.str,
                        "Hint", 4) == 0 &&
                 !zan_binder_lookup(
                     c->binder,
                     expr->call.callee->member.object->ident.name) &&
                 !checker_find_local(
                     c, expr->call.callee->member.object->ident.name)) {
        zan_istr_t mn = expr->call.callee->member.name;
        if ((mn.len == 6 && memcmp(mn.str, "Likely", 6) == 0) ||
            (mn.len == 8 && memcmp(mn.str, "Unlikely", 8) == 0))
          is_branch_hint = true;
      }
      if (is_branch_hint) {
        zan_type_t *at = zan_checker_check_expr(c, expr->call.args.items[0]);
        if (at && at->kind != TYPE_BOOL && at->kind != TYPE_ERROR) {
          zan_diag_emit_code(c->diag, DIAG_ERROR, expr->call.args.items[0]->loc,
                             2040, "branch hint expects 'bool', got '%s'",
                             type_name(at));
        }
        expr->call.is_intrinsic = 1;
        return c->binder->type_bool;
      }
    }
    /* NativeMemory aligned/typed/span intrinsics */
    if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
        expr->call.callee->member.object &&
        expr->call.callee->member.object->kind == AST_IDENTIFIER &&
        !checker_find_local(c, expr->call.callee->member.object->ident.name)) {
      zan_istr_t on = expr->call.callee->member.object->ident.name;
      zan_istr_t mn = expr->call.callee->member.name;
      if (on.len == 4 && memcmp(on.str, "Hint", 4) == 0 &&
          !zan_binder_lookup(c->binder, on)) {
        int ac = expr->call.args.count;
        if (mn.len == 6 && memcmp(mn.str, "Assume", 6) == 0) {
          if (ac != 1) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                               "Hint.Assume expects 1 bool argument");
          } else {
            zan_type_t *at = zan_checker_check_expr(c, expr->call.args.items[0]);
            if (at && at->kind != TYPE_BOOL && at->kind != TYPE_ERROR)
              zan_diag_emit_code(c->diag, DIAG_ERROR, expr->call.args.items[0]->loc,
                                 2040, "Hint.Assume expects 'bool', got '%s'",
                                 type_name(at));
          }
          expr->call.is_intrinsic = 1;
          return c->binder->type_void;
        }
        if ((mn.len == 11 && memcmp(mn.str, "Unreachable", 11) == 0) ||
            (mn.len == 4 && memcmp(mn.str, "Trap", 4) == 0)) {
          if (ac != 0)
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                               "Hint.%.*s takes no arguments", (int)mn.len, mn.str);
          expr->call.is_intrinsic = 1;
          return c->binder->type_void;
        }
        if (mn.len == 8 && memcmp(mn.str, "BlackBox", 8) == 0) {
          if (ac != 1) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                               "Hint.BlackBox expects 1 argument");
            expr->call.is_intrinsic = 1;
            return c->binder->type_error;
          }
          zan_type_t *at = zan_checker_check_expr(c, expr->call.args.items[0]);
          zan_type_t *rt = at ? at : c->binder->type_error;
          if (expr->call.type_args.count == 1) {
            zan_type_t *ta = zan_binder_resolve_type(c->binder, expr->call.type_args.items[0]);
            if (ta && ta->kind != TYPE_ERROR) {
              checker_check_assignable(c, ta, at, expr->call.args.items[0],
                                       expr->call.args.items[0]->loc, "Hint.BlackBox");
              rt = ta;
            }
          }
          expr->call.is_intrinsic = 1;
          return rt;
        }
      }
      if (on.len == 9 && memcmp(on.str, "InlineAsm", 9) == 0 &&
          ((mn.len == 4 && memcmp(mn.str, "Emit", 4) == 0) ||
           (mn.len == 9 && memcmp(mn.str, "EmitIntel", 9) == 0))) {
        int ac = expr->call.args.count;
        int tc = expr->call.type_args.count;
        if (ac < 2) {
          zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                             "InlineAsm.%.*s requires at least 2 arguments (template, constraints)",
                             (int)mn.len, mn.str);
        } else {
          if (!expr->call.args.items[0] ||
              expr->call.args.items[0]->kind != AST_STRING_LITERAL)
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->call.args.items[0]->loc, 2040,
                               "InlineAsm.%.*s assembly template must be a string literal",
                               (int)mn.len, mn.str);
          if (!expr->call.args.items[1] ||
              expr->call.args.items[1]->kind != AST_STRING_LITERAL)
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->call.args.items[1]->loc, 2040,
                               "InlineAsm.%.*s constraints must be a string literal",
                               (int)mn.len, mn.str);
        }
        zan_type_t *rt = c->binder->type_void;
        if (tc == 1) {
          rt = zan_binder_resolve_type(c->binder, expr->call.type_args.items[0]);
          if (rt && rt->kind != TYPE_ERROR && rt->kind != TYPE_VOID &&
              (rt->kind == TYPE_STRING || rt->kind == TYPE_CLASS ||
               rt->kind == TYPE_INTERFACE || rt->kind == TYPE_DELEGATE ||
               rt->kind == TYPE_ARRAY || rt->kind == TYPE_OBJECT ||
               rt->kind == TYPE_TASK)) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2040,
                               "InlineAsm.%.*s<T> return type must be an unmanaged scalar/value type, not '%s'",
                               (int)mn.len, mn.str, type_name(rt));
          }
        } else if (tc > 1) {
          zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                             "InlineAsm.%.*s accepts at most 1 return type argument",
                             (int)mn.len, mn.str);
          rt = c->binder->type_error;
        }
        for (int i = 2; i < ac; i++) {
          zan_ast_node_t *ai = expr->call.args.items[i];
          zan_type_t *at = (ai && ai->kind == AST_REF_ARG && ai->ref_arg.expr)
                               ? zan_checker_check_expr(c, ai->ref_arg.expr)
                               : zan_checker_check_expr(c, ai);
          if (at && at->kind != TYPE_ERROR &&
              (at->kind == TYPE_STRING || at->kind == TYPE_CLASS ||
               at->kind == TYPE_INTERFACE || at->kind == TYPE_DELEGATE ||
               at->kind == TYPE_ARRAY || at->kind == TYPE_OBJECT ||
               at->kind == TYPE_TASK)) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, ai->loc, 2040,
                               "InlineAsm.%.*s operand %d must be an unmanaged type, not '%s'",
                               (int)mn.len, mn.str, i - 1, type_name(at));
          }
        }
        expr->call.is_intrinsic = 1;
        return rt ? rt : c->binder->type_void;
      }
      if (on.len == 12 && memcmp(on.str, "NativeMemory", 12) == 0) {
        int ac = expr->call.args.count;
        int tc = expr->call.type_args.count;
        bool is_realloc = (mn.len == 7 && memcmp(mn.str, "Realloc", 7) == 0);
        bool is_aalloc = (mn.len == 12 && memcmp(mn.str, "AlignedAlloc", 12) == 0);
        bool is_afree = (mn.len == 11 && memcmp(mn.str, "AlignedFree", 11) == 0);
        bool is_addrof = (mn.len == 9 && memcmp(mn.str, "AddressOf", 9) == 0);
        bool is_fnptr = (mn.len == 15 && memcmp(mn.str, "FunctionPointer", 15) == 0);
        bool is_invoke = (mn.len == 6 && memcmp(mn.str, "Invoke", 6) == 0);
        bool is_inv_void = (mn.len == 10 && memcmp(mn.str, "InvokeVoid", 10) == 0);
        bool is_inv_cc = (mn.len == 12 && memcmp(mn.str, "InvokeWithCC", 12) == 0);
        bool is_inv_vcc = (mn.len == 16 && memcmp(mn.str, "InvokeVoidWithCC", 16) == 0);
        bool is_inv_var = (mn.len == 14 && memcmp(mn.str, "InvokeVariadic", 14) == 0);
        bool is_inv_vvar = (mn.len == 18 && memcmp(mn.str, "InvokeVariadicVoid", 18) == 0);
        bool is_bitcast = (mn.len == 7 && memcmp(mn.str, "BitCast", 7) == 0);
        bool is_zeroinit = (mn.len == 8 && memcmp(mn.str, "ZeroInit", 8) == 0);
        bool is_read = (mn.len == 4 && memcmp(mn.str, "Read", 4) == 0);
        bool is_write = (mn.len == 5 && memcmp(mn.str, "Write", 5) == 0);
        bool is_ru = (mn.len == 13 && memcmp(mn.str, "ReadUnaligned", 13) == 0);
        bool is_wu = (mn.len == 14 && memcmp(mn.str, "WriteUnaligned", 14) == 0);
        bool is_sload = (mn.len == 10 && memcmp(mn.str, "StreamLoad", 10) == 0);
        bool is_sstore = (mn.len == 11 && memcmp(mn.str, "StreamStore", 11) == 0);
        bool is_asspan = (mn.len == 6 && memcmp(mn.str, "AsSpan", 6) == 0);
        bool is_asref = (mn.len == 5 && memcmp(mn.str, "AsRef", 5) == 0);
        bool is_at = (mn.len == 2 && memcmp(mn.str, "At", 2) == 0);
        bool is_add = (mn.len == 3 && memcmp(mn.str, "Add", 3) == 0);
        bool is_byteoff = (mn.len == 10 && memcmp(mn.str, "ByteOffset", 10) == 0);
        bool is_incbytes = (mn.len == 12 && memcmp(mn.str, "IncludeBytes", 12) == 0);
        bool is_retaddr = (mn.len == 13 && memcmp(mn.str, "ReturnAddress", 13) == 0);
        bool is_frmaddr = (mn.len == 12 && memcmp(mn.str, "FrameAddress", 12) == 0);
        bool is_pfr = (mn.len == 12 && memcmp(mn.str, "PrefetchRead", 12) == 0);
        bool is_pfw = (mn.len == 13 && memcmp(mn.str, "PrefetchWrite", 13) == 0);
        if (is_incbytes) {
          if (ac != 1 || !expr->call.args.items[0] ||
              expr->call.args.items[0]->kind != AST_STRING_LITERAL) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 3108,
                               "NativeMemory.IncludeBytes requires a single string literal file path");
          } else {
            zan_istr_t ps = expr->call.args.items[0]->str_val;
            char full[1024];
            bool is_abs = (ps.len > 0 && (ps.str[0] == '/' || ps.str[0] == '\\')) ||
                          (ps.len > 1 && ((ps.str[0] >= 'A' && ps.str[0] <= 'Z') ||
                                          (ps.str[0] >= 'a' && ps.str[0] <= 'z')) &&
                           ps.str[1] == ':');
            if (is_abs || !c->diag || !c->diag->file_names ||
                expr->loc.file_id >= (uint32_t)c->diag->file_count ||
                !c->diag->file_names[expr->loc.file_id]) {
              snprintf(full, sizeof(full), "%.*s", (int)ps.len, ps.str ? ps.str : "");
            } else {
              const char *cf = c->diag->file_names[expr->loc.file_id];
              const char *slash = strrchr(cf, '/');
              const char *bslash = strrchr(cf, '\\');
              const char *sep = (!slash || (bslash && bslash > slash)) ? bslash : slash;
              if (sep) {
                int dlen = (int)(sep - cf);
                snprintf(full, sizeof(full), "%.*s/%.*s", dlen, cf, (int)ps.len, ps.str ? ps.str : "");
              } else {
                snprintf(full, sizeof(full), "%.*s", (int)ps.len, ps.str ? ps.str : "");
              }
            }
            FILE *fp = fopen(full, "rb");
            if (!fp) {
              zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 3108,
                                 "NativeMemory.IncludeBytes could not open file '%s'", full);
            } else {
              fclose(fp);
            }
          }
          expr->call.is_intrinsic = 1;
          return zan_binder_make_span_type(c->binder, c->binder->type_byte);
        }
        if (is_retaddr || is_frmaddr) {
          if (ac != 1 || !expr->call.args.items[0] ||
              expr->call.args.items[0]->kind != AST_INT_LITERAL ||
              expr->call.args.items[0]->int_val < 0 ||
              expr->call.args.items[0]->int_val > 64) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 3108,
                               "NativeMemory.%.*s requires a compile-time integer level in 0..64",
                               (int)mn.len, mn.str);
          }
          expr->call.is_intrinsic = 1;
          return c->binder->type_nint;
        }
        if (is_pfr || is_pfw) {
          if (ac < 1 || ac > 2)
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                               "NativeMemory.%.*s expects 1 or 2 arguments, got %d",
                               (int)mn.len, mn.str, ac);
          for (int i = 0; i < ac; i++)
            zan_checker_check_expr(c, expr->call.args.items[i]);
          expr->call.is_intrinsic = 1;
          return c->binder->type_void;
        }
        if (is_realloc || is_aalloc || is_afree || is_addrof || is_fnptr ||
            is_invoke || is_inv_void || is_inv_cc || is_inv_vcc ||
            is_inv_var || is_inv_vvar ||
            is_bitcast || is_zeroinit ||
            is_read || is_write || is_ru || is_wu || is_sload || is_sstore ||
            is_asspan || is_asref || is_at || is_add || is_byteoff) {
          if (is_fnptr) {
            if (ac != 1) {
              zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                                 "NativeMemory.FunctionPointer expects 1 argument, got %d", ac);
            } else {
              zan_ast_node_t *a0 = expr->call.args.items[0];
              zan_symbol_t *msym = NULL;
              if (a0 && a0->kind == AST_MEMBER_ACCESS && a0->member.object &&
                  a0->member.object->kind == AST_IDENTIFIER) {
                zan_symbol_t *ts = zan_binder_lookup(c->binder, a0->member.object->ident.name);
                if (ts) msym = checker_find_method(ts, a0->member.name);
              } else if (a0 && a0->kind == AST_IDENTIFIER && c->current_type_sym) {
                msym = checker_find_method(c->current_type_sym, a0->ident.name);
              }
              if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2040,
                                   "NativeMemory.FunctionPointer argument must reference a static [Export], [UnmanagedCallersOnly], or [DllImport] method");
              } else if (!zan_ast_has_attr(msym->decl, "Export") &&
                         !zan_ast_has_attr(msym->decl, "UnmanagedCallersOnly") &&
                         !(msym->decl->method_decl.ext &&
                           msym->decl->method_decl.ext->extern_lib.str)) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2040,
                                   "NativeMemory.FunctionPointer target '%.*s' must be marked [Export], [UnmanagedCallersOnly], or [DllImport]",
                                   (int)msym->name.len, msym->name.str);
              }
            }
            expr->call.is_intrinsic = 1;
            return c->binder->type_nint;
          }
          if (is_invoke || is_inv_void || is_inv_cc || is_inv_vcc ||
              is_inv_var || is_inv_vvar) {
            bool with_cc = (is_inv_cc || is_inv_vcc);
            bool is_variadic = (is_inv_var || is_inv_vvar);
            bool returns_val = (is_invoke || is_inv_cc || is_inv_var);
            int min_ac = (with_cc || is_variadic) ? 2 : 1;
            if (ac < min_ac) {
              zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc,
                                 is_variadic ? 3109 : 2033,
                                 "NativeMemory.%.*s requires at least %d argument(s)",
                                 (int)mn.len, mn.str, min_ac);
            } else if (is_variadic) {
              zan_ast_node_t *fc_arg = expr->call.args.items[1];
              int n_call_args = ac - 2;
              if (!fc_arg || fc_arg->kind != AST_INT_LITERAL ||
                  fc_arg->int_val < 0 || fc_arg->int_val > n_call_args) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, fc_arg ? fc_arg->loc : expr->loc, 3109,
                                   "NativeMemory.%.*s fixedArgCount must be a compile-time integer literal in 0..%d",
                                   (int)mn.len, mn.str, n_call_args);
              }
            } else if (with_cc) {
              zan_ast_node_t *cc_arg = expr->call.args.items[0];
              if (!cc_arg || cc_arg->kind != AST_STRING_LITERAL || cc_arg->str_val.len <= 0) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 3106,
                                   "NativeMemory.%.*s first argument must be a calling convention string literal",
                                   (int)mn.len, mn.str);
              } else {
                char ccb[64];
                int cl = cc_arg->str_val.len < 63 ? cc_arg->str_val.len : 63;
                for (int ci = 0; ci < cl; ci++) {
                  char ch = cc_arg->str_val.str[ci];
                  ccb[ci] = (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
                }
                ccb[cl] = '\0';
                bool ok_cc = (strcmp(ccb, "c") == 0 || strcmp(ccb, "cdecl") == 0 ||
                              strcmp(ccb, "stdcall") == 0 || strcmp(ccb, "fastcall") == 0 ||
                              strcmp(ccb, "win64") == 0 || strcmp(ccb, "sysv64") == 0 ||
                              strcmp(ccb, "aapcs") == 0 || strcmp(ccb, "vectorcall") == 0 ||
                              strcmp(ccb, "preserve_most") == 0 || strcmp(ccb, "preserve_all") == 0);
                if (!ok_cc) {
                  zan_diag_emit_code(c->diag, DIAG_ERROR, cc_arg->loc, 3106,
                                     "unsupported calling convention '%s' in NativeMemory.%.*s",
                                     ccb, (int)mn.len, mn.str);
                }
              }
            }
            zan_type_t *rt = c->binder->type_void;
            if (returns_val) {
              if (tc != 1) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                                   "NativeMemory.%.*s<TRet> requires 1 return type argument",
                                   (int)mn.len, mn.str);
                rt = c->binder->type_error;
              } else {
                rt = zan_binder_resolve_type(c->binder, expr->call.type_args.items[0]);
                if (rt && rt->kind != TYPE_ERROR && rt->kind != TYPE_VOID &&
                    (rt->kind == TYPE_STRING || rt->kind == TYPE_CLASS ||
                     rt->kind == TYPE_INTERFACE || rt->kind == TYPE_DELEGATE ||
                     rt->kind == TYPE_ARRAY || rt->kind == TYPE_OBJECT ||
                     rt->kind == TYPE_TASK)) {
                  zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2040,
                                     "NativeMemory.%.*s<TRet> return type must be unmanaged, not '%s'",
                                     (int)mn.len, mn.str, type_name(rt));
                }
              }
            }
            for (int i = 0; i < ac; i++) {
              zan_ast_node_t *ai = expr->call.args.items[i];
              zan_type_t *at = (ai && ai->kind == AST_REF_ARG && ai->ref_arg.expr)
                                   ? zan_checker_check_expr(c, ai->ref_arg.expr)
                                   : zan_checker_check_expr(c, ai);
              if (i >= min_ac && at && at->kind != TYPE_ERROR &&
                  (at->kind == TYPE_STRING || at->kind == TYPE_CLASS ||
                   at->kind == TYPE_INTERFACE || at->kind == TYPE_DELEGATE ||
                   at->kind == TYPE_ARRAY || at->kind == TYPE_OBJECT ||
                   at->kind == TYPE_TASK)) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, ai->loc, 2040,
                                   "NativeMemory.%.*s argument %d must be an unmanaged type, not '%s'",
                                   (int)mn.len, mn.str, i + 1, type_name(at));
              }
            }
            expr->call.is_intrinsic = 1;
            return rt;
          }
          if (is_bitcast) {
            zan_type_t *ft = NULL, *tt = NULL;
            if (tc != 2) {
              zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                                 "NativeMemory.BitCast<TFrom, TTo> requires 2 type arguments");
            } else {
              ft = zan_binder_resolve_type(c->binder, expr->call.type_args.items[0]);
              tt = zan_binder_resolve_type(c->binder, expr->call.type_args.items[1]);
              if (ft && ft->kind != TYPE_ERROR &&
                  (ft->kind == TYPE_STRING || ft->kind == TYPE_CLASS ||
                   ft->kind == TYPE_INTERFACE || ft->kind == TYPE_DELEGATE ||
                   ft->kind == TYPE_ARRAY || ft->kind == TYPE_OBJECT ||
                   ft->kind == TYPE_TASK || ft->kind == TYPE_VOID)) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2040,
                                   "NativeMemory.BitCast source type must be unmanaged, not '%s'",
                                   type_name(ft));
              }
              if (tt && tt->kind != TYPE_ERROR &&
                  (tt->kind == TYPE_STRING || tt->kind == TYPE_CLASS ||
                   tt->kind == TYPE_INTERFACE || tt->kind == TYPE_DELEGATE ||
                   tt->kind == TYPE_ARRAY || tt->kind == TYPE_OBJECT ||
                   tt->kind == TYPE_TASK || tt->kind == TYPE_VOID)) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2040,
                                   "NativeMemory.BitCast target type must be unmanaged, not '%s'",
                                   type_name(tt));
              }
            }
            if (ac != 1) {
              zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                                 "NativeMemory.BitCast expects 1 argument, got %d", ac);
            } else {
              zan_type_t *at = zan_checker_check_expr(c, expr->call.args.items[0]);
              if (ft && ft->kind != TYPE_ERROR)
                checker_check_assignable(c, ft, at, expr->call.args.items[0],
                                         expr->call.args.items[0]->loc,
                                         "NativeMemory.BitCast argument");
            }
            expr->call.is_intrinsic = 1;
            return tt ? tt : c->binder->type_error;
          }
          if (is_zeroinit) {
            if (ac != 1 || !expr->call.args.items[0] ||
                expr->call.args.items[0]->kind != AST_REF_ARG) {
              zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2040,
                                 "NativeMemory.ZeroInit requires a single 'ref' argument");
            } else {
              zan_type_t *zt = zan_checker_check_expr(c, expr->call.args.items[0]->ref_arg.expr);
              if (zt && zt->kind != TYPE_ERROR &&
                  (zt->kind == TYPE_STRING || zt->kind == TYPE_CLASS ||
                   zt->kind == TYPE_INTERFACE || zt->kind == TYPE_DELEGATE ||
                   zt->kind == TYPE_ARRAY || zt->kind == TYPE_OBJECT ||
                   zt->kind == TYPE_TASK)) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2040,
                                   "NativeMemory.ZeroInit target must be an unmanaged value type, not '%s'",
                                   type_name(zt));
              }
            }
            expr->call.is_intrinsic = 1;
            return c->binder->type_void;
          }
          if (is_realloc || is_aalloc) {
            if (ac != 2)
              zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                                 "NativeMemory.%.*s expects 2 arguments, got %d",
                                 (int)mn.len, mn.str, ac);
            for (int i = 0; i < ac; i++)
              zan_checker_check_expr(c, expr->call.args.items[i]);
            expr->call.is_intrinsic = 1;
            return c->binder->type_nint;
          }
          if (is_afree) {
            if (ac != 1)
              zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                                 "NativeMemory.AlignedFree expects 1 argument, got %d", ac);
            for (int i = 0; i < ac; i++)
              zan_checker_check_expr(c, expr->call.args.items[i]);
            expr->call.is_intrinsic = 1;
            return c->binder->type_void;
          }
          if (is_addrof) {
            if (ac != 1 || !expr->call.args.items[0] ||
                expr->call.args.items[0]->kind != AST_REF_ARG) {
              zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2040,
                                 "NativeMemory.AddressOf requires a single 'ref' argument");
            } else {
              zan_checker_check_expr(c, expr->call.args.items[0]->ref_arg.expr);
            }
            expr->call.is_intrinsic = 1;
            return c->binder->type_nint;
          }
          if (is_byteoff) {
            if (ac != 2 || !expr->call.args.items[0] || !expr->call.args.items[1] ||
                expr->call.args.items[0]->kind != AST_REF_ARG ||
                expr->call.args.items[1]->kind != AST_REF_ARG) {
              zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2040,
                                 "NativeMemory.ByteOffset requires two 'ref' arguments");
            } else {
              zan_checker_check_expr(c, expr->call.args.items[0]->ref_arg.expr);
              zan_checker_check_expr(c, expr->call.args.items[1]->ref_arg.expr);
            }
            expr->call.is_intrinsic = 1;
            return c->binder->type_long;
          }
          zan_type_t *et = NULL;
          if (tc != 1) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                               "NativeMemory.%.*s requires 1 type argument <T>",
                               (int)mn.len, mn.str);
          } else {
            et = zan_binder_resolve_type(c->binder, expr->call.type_args.items[0]);
            if (et && et->kind != TYPE_ERROR &&
                (et->kind == TYPE_STRING || et->kind == TYPE_CLASS ||
                 et->kind == TYPE_INTERFACE || et->kind == TYPE_DELEGATE ||
                 et->kind == TYPE_ARRAY || et->kind == TYPE_OBJECT ||
                 et->kind == TYPE_TASK || et->kind == TYPE_VOID)) {
              zan_diag_emit_code(
                  c->diag, DIAG_ERROR, expr->loc, 2040,
                  "NativeMemory.%.*s<T> requires an unmanaged value type, not '%s'",
                  (int)mn.len, mn.str, type_name(et));
            }
          }
          int need = (is_read || is_ru || is_sload || is_asref) ? 1 : 2;
          if (ac != need) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                               "NativeMemory.%.*s expects %d argument(s), got %d",
                               (int)mn.len, mn.str, need, ac);
          }
          for (int i = 0; i < ac; i++) {
            zan_type_t *at = zan_checker_check_expr(c, expr->call.args.items[i]);
            if ((is_write || is_wu || is_sstore) && i == 1 && et && et->kind != TYPE_ERROR) {
              checker_check_assignable(c, et, at, expr->call.args.items[1],
                                       expr->call.args.items[1]->loc,
                                       "NativeMemory.Write value");
            }
          }
          expr->call.is_intrinsic = 1;
          if (is_write || is_wu || is_sstore)
            return c->binder->type_void;
          if (is_add)
            return c->binder->type_nint;
          if (is_asspan)
            return et ? zan_binder_make_span_type(c->binder, et) : c->binder->type_error;
          return et ? et : c->binder->type_error;
        }
      }
      bool is_volatile = (on.len == 8 && memcmp(on.str, "Volatile", 8) == 0);
      bool is_atomic = (on.len == 6 && memcmp(on.str, "Atomic", 6) == 0);
      if (is_volatile || is_atomic) {
        int ac = expr->call.args.count;
        if (is_volatile &&
            ((mn.len == 9 && memcmp(mn.str, "CopyBlock", 9) == 0) ||
             (mn.len == 9 && memcmp(mn.str, "InitBlock", 9) == 0))) {
          if (ac != 3)
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                               "Volatile.%.*s expects 3 arguments, got %d",
                               (int)mn.len, mn.str, ac);
          for (int i = 0; i < ac; i++)
            zan_checker_check_expr(c, expr->call.args.items[i]);
          expr->call.is_intrinsic = 1;
          return c->binder->type_void;
        }
        if (is_atomic && mn.len == 5 && memcmp(mn.str, "Pause", 5) == 0) {
          if (ac != 0)
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                               "Atomic.Pause expects 0 arguments, got %d", ac);
          expr->call.is_intrinsic = 1;
          return c->binder->type_void;
        }
        if (is_atomic && mn.len == 13 && memcmp(mn.str, "CompilerFence", 13) == 0) {
          if (ac != 0)
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                               "Atomic.CompilerFence expects 0 arguments, got %d", ac);
          expr->call.is_intrinsic = 1;
          return c->binder->type_void;
        }
        if (is_atomic && mn.len == 5 && memcmp(mn.str, "Fence", 5) == 0) {
          if (ac > 1)
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                               "Atomic.Fence expects 0 or 1 arguments, got %d", ac);
          if (ac == 1) {
            zan_type_t *ot = zan_checker_check_expr(c, expr->call.args.items[0]);
            if (ot && ot->kind != TYPE_INT && ot->kind != TYPE_ERROR)
              zan_diag_emit_code(c->diag, DIAG_ERROR, expr->call.args.items[0]->loc,
                                 2040, "MemoryOrder argument must be 'int', got '%s'",
                                 type_name(ot));
            zan_ast_node_t *oa = expr->call.args.items[0];
            if ((oa->kind == AST_INT_LITERAL && oa->int_val == 0) ||
                (oa->kind == AST_MEMBER_ACCESS && oa->member.name.len == 7 &&
                 memcmp(oa->member.name.str, "Relaxed", 7) == 0)) {
              zan_diag_emit_code(c->diag, DIAG_ERROR, oa->loc, 2040,
                                 "Atomic.Fence ordering cannot be MemoryOrder.Relaxed");
            }
          }
          expr->call.is_intrinsic = 1;
          return c->binder->type_void;
        }
        if (is_atomic && mn.len == 20 &&
            memcmp(mn.str, "CompareExchange128At", 20) == 0) {
          if (ac < 5 || ac > 7) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                               "Atomic.CompareExchange128At requires 5..7 arguments");
            return c->binder->type_error;
          }
          zan_type_t *addr_t = zan_checker_check_expr(c, expr->call.args.items[0]);
          if (addr_t && addr_t->kind != TYPE_ERROR &&
              addr_t->kind != TYPE_NINT && addr_t->kind != TYPE_LONG &&
              addr_t->kind != TYPE_ULONG) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->call.args.items[0]->loc, 2040,
                               "Atomic.CompareExchange128At address must be 'nint'/'long', got '%s'",
                               type_name(addr_t));
          }
          for (int k = 1; k <= 2; k++) {
            zan_ast_node_t *rk = expr->call.args.items[k];
            if (!rk || rk->kind != AST_REF_ARG || rk->ref_arg.is_out) {
              zan_diag_emit_code(c->diag, DIAG_ERROR, rk ? rk->loc : expr->loc, 2040,
                                 "Atomic.CompareExchange128At argument %d must be passed with 'ref'",
                                 k + 1);
            } else {
              zan_type_t *et = zan_checker_check_expr(c, rk->ref_arg.expr);
              if (et && et->kind != TYPE_ERROR &&
                  et->kind != TYPE_LONG && et->kind != TYPE_ULONG) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, rk->loc, 2040,
                                   "Atomic.CompareExchange128At expected half must be 'long' or 'ulong', got '%s'",
                                   type_name(et));
              }
            }
          }
          for (int k = 3; k < ac; k++)
            zan_checker_check_expr(c, expr->call.args.items[k]);
          expr->call.is_intrinsic = 1;
          return c->binder->type_bool;
        }
        bool is_at = (mn.len > 2 && memcmp(mn.str + mn.len - 2, "At", 2) == 0);
        if (ac < 1) {
          zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                             "%.*s.%.*s requires a target argument",
                             (int)on.len, on.str, (int)mn.len, mn.str);
          return c->binder->type_error;
        }
        zan_ast_node_t *a0 = expr->call.args.items[0];
        zan_type_t *lt = NULL;
        if (is_at) {
          if (expr->call.type_args.count != 1) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                               "%.*s.%.*s requires 1 type argument <T>",
                               (int)on.len, on.str, (int)mn.len, mn.str);
            return c->binder->type_error;
          }
          lt = zan_binder_resolve_type(c->binder, expr->call.type_args.items[0]);
          zan_type_t *addr_t = zan_checker_check_expr(c, a0);
          if (addr_t && addr_t->kind != TYPE_ERROR &&
              addr_t->kind != TYPE_NINT && addr_t->kind != TYPE_LONG &&
              addr_t->kind != TYPE_ULONG) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, a0->loc, 2040,
                               "%.*s.%.*s address must be 'nint'/'long', got '%s'",
                               (int)on.len, on.str, (int)mn.len, mn.str,
                               type_name(addr_t));
          }
          mn.len -= 2;
        } else {
          if (!a0 || a0->kind != AST_REF_ARG || a0->ref_arg.is_out) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, a0 ? a0->loc : expr->loc, 2040,
                               "%.*s.%.*s first argument must be passed with 'ref'",
                               (int)on.len, on.str, (int)mn.len, mn.str);
            return c->binder->type_error;
          }
          lt = zan_checker_check_expr(c, a0->ref_arg.expr);
        }
        bool ok_type = false;
        if (lt && !lt->is_nullable) {
          switch (lt->kind) {
          case TYPE_BYTE: case TYPE_SBYTE:
          case TYPE_SHORT: case TYPE_USHORT:
          case TYPE_INT: case TYPE_UINT:
          case TYPE_LONG: case TYPE_ULONG:
          case TYPE_NINT:
            ok_type = true;
            break;
          case TYPE_BOOL: case TYPE_CHAR:
          case TYPE_FLOAT: case TYPE_DOUBLE:
            ok_type = is_volatile;
            break;
          default:
            break;
          }
        }
        if (lt && lt->kind != TYPE_ERROR && !ok_type) {
          zan_diag_emit_code(c->diag, DIAG_ERROR, a0->loc, 2040,
                             "%.*s.%.*s does not support target type '%s'",
                             (int)on.len, on.str, (int)mn.len, mn.str,
                             type_name(lt));
          return c->binder->type_error;
        }
        for (int ai = 1; ai < ac; ai++)
          zan_checker_check_expr(c, expr->call.args.items[ai]);
        if (is_volatile) {
          if (mn.len == 4 && memcmp(mn.str, "Read", 4) == 0 && ac == 1) {
            expr->call.is_intrinsic = 1;
            return lt ? lt : c->binder->type_error;
          }
          if (mn.len == 5 && memcmp(mn.str, "Write", 5) == 0 && ac == 2) {
            expr->call.is_intrinsic = 1;
            return c->binder->type_void;
          }
        } else {
          if (mn.len == 4 && memcmp(mn.str, "Load", 4) == 0 && (ac == 1 || ac == 2)) {
            if (ac == 2) {
              zan_ast_node_t *oa = expr->call.args.items[1];
              if ((oa->kind == AST_INT_LITERAL && (oa->int_val == 2 || oa->int_val == 3)) ||
                  (oa->kind == AST_MEMBER_ACCESS &&
                   ((oa->member.name.len == 7 && memcmp(oa->member.name.str, "Release", 7) == 0) ||
                    (oa->member.name.len == 6 && memcmp(oa->member.name.str, "AcqRel", 6) == 0))))
                zan_diag_emit_code(c->diag, DIAG_ERROR, oa->loc, 2040,
                                   "Atomic.Load ordering cannot be Release or AcqRel");
            }
            expr->call.is_intrinsic = 1;
            return lt ? lt : c->binder->type_error;
          }
          if (mn.len == 5 && memcmp(mn.str, "Store", 5) == 0 && (ac == 2 || ac == 3)) {
            if (ac == 3) {
              zan_ast_node_t *oa = expr->call.args.items[2];
              if ((oa->kind == AST_INT_LITERAL && (oa->int_val == 1 || oa->int_val == 3)) ||
                  (oa->kind == AST_MEMBER_ACCESS &&
                   ((oa->member.name.len == 7 && memcmp(oa->member.name.str, "Acquire", 7) == 0) ||
                    (oa->member.name.len == 6 && memcmp(oa->member.name.str, "AcqRel", 6) == 0))))
                zan_diag_emit_code(c->diag, DIAG_ERROR, oa->loc, 2040,
                                   "Atomic.Store ordering cannot be Acquire or AcqRel");
            }
            expr->call.is_intrinsic = 1;
            return c->binder->type_void;
          }
          if (mn.len == 19 && memcmp(mn.str, "CompareExchangeBool", 19) == 0 &&
              (ac >= 3 && ac <= 5)) {
            expr->call.is_intrinsic = 1;
            return c->binder->type_bool;
          }
          if (mn.len == 15 && memcmp(mn.str, "CompareExchange", 15) == 0 &&
              (ac >= 3 && ac <= 5)) {
            expr->call.is_intrinsic = 1;
            return lt ? lt : c->binder->type_error;
          }
          if (((mn.len == 8 && (memcmp(mn.str, "Exchange", 8) == 0 ||
                                memcmp(mn.str, "FetchAdd", 8) == 0 ||
                                memcmp(mn.str, "FetchSub", 8) == 0 ||
                                memcmp(mn.str, "FetchAnd", 8) == 0 ||
                                memcmp(mn.str, "FetchXor", 8) == 0 ||
                                memcmp(mn.str, "FetchMin", 8) == 0 ||
                                memcmp(mn.str, "FetchMax", 8) == 0)) ||
               (mn.len == 7 && memcmp(mn.str, "FetchOr", 7) == 0)) &&
              (ac == 2 || ac == 3)) {
            expr->call.is_intrinsic = 1;
            return lt ? lt : c->binder->type_error;
          }
        }
        zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2033,
                           "invalid call to %.*s.%.*s with %d argument(s)",
                           (int)on.len, on.str, (int)mn.len, mn.str, ac);
        return c->binder->type_error;
      }
    }
    zan_type_t *callee_type;
    zan_type_t *recv = NULL;
    int callee_is_method = 0;
    bool typed_overload_changed = false;
    bool exact_method = false;
    bool null_receiver = false;
    bool observe = c->binder->symbol_reference != NULL;
    int errors_before = c->diag->error_count;
    zan_ast_node_t *callee = expr->call.callee;
    bool receiver_known = false;
    bool type_receiver = false;
    bool unproven_operand = false;
    zan_symbol_t *called_sym = NULL;
    int argc = expr->call.args.count;
    zan_type_t **arg_types =
        argc > 0 ? (zan_type_t **)zan_arena_alloc(
                       c->arena, (size_t)argc * sizeof(zan_type_t *))
                 : NULL;
    if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
        expr->call.callee->member.object->kind == AST_IDENTIFIER &&
        expr->call.args.count == 2 && expr->call.callee->member.name.len == 8 &&
        memcmp(expr->call.callee->member.name.str, "TryParse", 8) == 0) {
      zan_symbol_t *es = zan_binder_lookup(
          c->binder, expr->call.callee->member.object->ident.name);
      if (es && es->kind == SYM_ENUM) {
        for (int ai = 0; ai < 2; ai++)
          zan_checker_check_expr(c, expr->call.args.items[ai]);
        expr->call.is_intrinsic = 1;
        return c->binder->type_bool;
      }
    }
    if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
      recv = zan_checker_check_expr(c, expr->call.callee->member.object);
      if (observe) {
        receiver_known = checker_reference_receiver(c, callee->member.object,
                                                    recv, &type_receiver);
        unproven_operand =
            callee->member.object == c->last_unproven_reference_expr;
      }
      if (recv && recv->sym) {
        zan_symbol_t *m = checker_find_method_call(
            recv->sym, expr->call.callee->member.name, expr->call.args.count,
            expr->call.type_args.count);
        if (m && m->kind == SYM_METHOD) {
          if (!access_member_allowed(c, m)) {
            report_inaccessible(c, m, expr->call.callee->loc);
            return c->binder->type_error;
          }
          null_receiver = checker_reject_null_receiver(c, callee, true);
          called_sym = m;
          callee_type = m->type ? m->type : c->binder->type_error;
          callee_is_method = 1;
        }
      }
      if (!callee_is_method) {
        /* 标量原语（string/int/…）也要走成员检查：调用一个不存在
         * 的方法应该在类型检查阶段就报出来，而不是拖到发码阶段
         * 变成 "code generation failed"（A476）。 */
        callee_type =
            check_member_access(c, expr->call.callee, recv, true, expr);
      }
    } else {
      callee_type = zan_checker_check_expr(c, expr->call.callee);
    }
    zan_symbol_t *arg_sig = call_arg_signature(c, expr, recv);
    bool is_parallel_for_call = false;
    if (!callee_is_method && !called_sym && expr->call.callee &&
        expr->call.callee->kind == AST_MEMBER_ACCESS &&
        expr->call.callee->member.object->kind == AST_IDENTIFIER &&
        expr->call.callee->member.object->ident.name.len == 8 &&
        memcmp(expr->call.callee->member.object->ident.name.str, "Parallel", 8) == 0) {
      zan_istr_t pmn = expr->call.callee->member.name;
      if ((pmn.len == 3 && memcmp(pmn.str, "For", 3) == 0) ||
          (pmn.len == 8 && memcmp(pmn.str, "ForRange", 8) == 0))
        is_parallel_for_call = true;
    }
    int prev_in_call_args = c->in_call_args;
    c->in_call_args = 1;
    for (int i = 0; i < expr->call.args.count; i++) {
      zan_ast_node_t *arg = expr->call.args.items[i];
      if (arg && arg->kind == AST_NAMED_ARG)
        arg = arg->named_arg.expr;
      bool target_param_noescape = false;
      if (is_parallel_for_call && i == 2) {
        target_param_noescape = true;
      } else {
        zan_symbol_t *psym = arg_sig ? arg_sig : called_sym;
        if (psym && psym->decl && psym->decl->kind == AST_METHOD_DECL &&
            i < psym->decl->method_decl.params.count) {
          zan_ast_node_t *pp = psym->decl->method_decl.params.items[i];
          if (pp && pp->kind == AST_PARAM && pp->param.is_noescape)
            target_param_noescape = true;
        }
      }
      if (target_param_noescape && arg && arg->kind == AST_LAMBDA)
        arg->lambda.is_noescape = 1;
      zan_ast_node_t *prev_ok = c->async_call_ok;
      if (arg_is_spawn_callee(expr))
        c->async_call_ok = arg;
      zan_type_t *arg_type = zan_checker_check_expr(c, arg);
      c->async_call_ok = prev_ok;
      if (!target_param_noescape) {
        zan_istr_t ne_name = {NULL, 0};
        if (checker_expr_is_noescape_param(c, arg, &ne_name)) {
          zan_diag_emit_code(
              c->diag, DIAG_ERROR, arg->loc, 2142,
              "non-escaping parameter '%.*s' cannot be passed to escaping parameter",
              (int)ne_name.len, ne_name.str);
        }
      }
      if (observe && arg && arg == c->last_unproven_reference_expr)
        unproven_operand = true;
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
      check_builtin_generic_call_args(c, recv, expr->call.callee->member.name,
                                      expr, arg_types);
      /* 方法组实参裁决：内建成员没有签名路径，单独兜
       * （B-ID151——check_member_access 实参位置静默后的闸）。 */
      checker_builtin_mg_args(c, recv, expr->call.callee->member.name, expr);
    }
    if (callee_is_method && recv && recv->sym) {
      zan_symbol_t *typed = checker_find_method_typed(
          c, recv, recv->sym, expr->call.callee->member.name, expr, arg_types,
          &exact_method);
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
      zan_symbol_t *resolved = checker_find_method_typed(
          c, c->current_type_sym ? c->current_type_sym->type : NULL,
          c->current_type_sym, expr->call.callee->ident.name, expr, arg_types,
          &exact_method);
      if (resolved) {
        called_sym = resolved;
      } else {
        called_sym = NULL;
        callee_type = c->binder->type_error;
      }
    }
    /* 泛型方法调用点的 where 约束（A518）：符号定了就查，别等运行期 */
    if (called_sym && called_sym->kind == SYM_METHOD)
      checker_check_method_call_constraints(c, called_sym, expr, arg_types);
    /* Transitive [NoAlloc] call-graph enforcement (D @nogc semantics) */
    if (c->in_noalloc && called_sym && called_sym->kind == SYM_METHOD &&
        called_sym->decl && called_sym->decl->kind == AST_METHOD_DECL &&
        !expr->call.is_intrinsic &&
        !(called_sym->decl->method_decl.modifiers & MOD_EXTERN) &&
        !zan_ast_has_attr(called_sym->decl, "NoAlloc") &&
        !zan_ast_has_attr(called_sym->decl, "NoRuntime")) {
      bool callee_is_lib =
          called_sym->decl->from_stdlib || called_sym->decl->from_package ||
          (called_sym->parent && called_sym->parent->decl &&
           (called_sym->parent->decl->from_stdlib ||
            called_sym->parent->decl->from_package));
      if (!callee_is_lib && !c->freestanding) {
        zan_diag_emit_code(
            c->diag, DIAG_ERROR, expr->loc, 3100,
            "[NoAlloc] method cannot call non-[NoAlloc] method '%.*s'",
            (int)called_sym->name.len, called_sym->name.str);
      } else {
        zan_type_t *crt = called_sym->type;
        bool alloc_ret =
            crt && (crt->kind == TYPE_STRING || crt->kind == TYPE_CLASS ||
                    crt->kind == TYPE_ARRAY || crt->kind == TYPE_OBJECT ||
                    crt->kind == TYPE_TASK);
        bool alloc_owner = false;
        if (called_sym->parent && called_sym->parent->name.str) {
          zan_istr_t pn = called_sym->parent->name;
          if ((pn.len == 4 && memcmp(pn.str, "List", 4) == 0) ||
              (pn.len == 10 && memcmp(pn.str, "Dictionary", 10) == 0) ||
              (pn.len == 7 && memcmp(pn.str, "HashSet", 7) == 0) ||
              (pn.len == 13 && memcmp(pn.str, "StringBuilder", 13) == 0) ||
              (pn.len == 7 && memcmp(pn.str, "Console", 7) == 0))
            alloc_owner = true;
        }
        if (alloc_ret || alloc_owner) {
          zan_diag_emit_code(
              c->diag, DIAG_ERROR, expr->loc, 3100,
              "[NoAlloc] method cannot call allocating stdlib method '%.*s'",
              (int)called_sym->name.len, called_sym->name.str);
        }
      }
    }
    /* E2116：async 调用的静态类型是结果类型，但未 await 时 irgen 产出的
     * 是协程帧——当值用（赋值、运算、实参、return）读到的是截断的帧
     * 指针，静默错值；与 + 等运算组合时还会让 LLVM 校验失败。 */
    if (called_sym && called_sym->kind == SYM_METHOD && called_sym->decl &&
        called_sym->decl->kind == AST_METHOD_DECL &&
        (called_sym->decl->method_decl.modifiers & MOD_ASYNC) &&
        c->async_call_ok != expr && called_sym->type &&
        called_sym->type->kind != TYPE_VOID &&
        called_sym->type->kind != TYPE_ERROR) {
      zan_diag_emit_code(
          c->diag, DIAG_ERROR, expr->loc, 2116,
          "async call '%.*s' used as a value without 'await': write "
          "'await %.*s(...)' (allowed in synchronous methods too), or "
          "call it as a statement / pass it to Task.Spawn or Task.Run "
          "to run it detached",
          (int)called_sym->name.len, called_sym->name.str,
          (int)called_sym->name.len, called_sym->name.str);
    }
    if (!called_sym && expr->call.callee &&
        expr->call.callee->kind == AST_IDENTIFIER) {
      called_sym = zan_binder_lookup(c->binder, expr->call.callee->ident.name);
    }
    if (!called_sym && arg_sig) {
      called_sym = arg_sig;
    }
    checker_bind_call_intrinsic(c, expr, called_sym, recv);
    check_builtin_string_join_args(c, expr, arg_types);
    check_call_arity(c, expr, recv);
    for (int i = 0; exact_method && i < expr->call.type_args.count; i++) {
      zan_type_t *type_arg =
          zan_binder_resolve_type(c->binder, expr->call.type_args.items[i]);
      exact_method = checker_call_type_known(type_arg, 0);
    }
    if (exact_method) {
      bool is_static = (called_sym->modifiers & MOD_STATIC) != 0;
      if (callee->kind == AST_MEMBER_ACCESS)
        exact_method = receiver_known && type_receiver == is_static &&
                       !checker_find_field(recv->sym, callee->member.name);
      else
        exact_method =
            !callee->ident.is_type_receiver && !callee->ident.inst_type_ref &&
            !checker_lookup_local(c, callee->ident.name, true) &&
            !checker_find_field(c->current_type_sym, callee->ident.name) &&
            (is_static || !checker_in_static_method(c));
      exact_method = exact_method && !null_receiver && !unproven_operand &&
                     c->diag->error_count == errors_before &&
                     access_member_allowed(c, called_sym);
      /* Extension lookup here certifies only the name and receiver.
       * The checker has no typed result for that competing call path. */
      if (exact_method && recv && !type_receiver &&
          zan_binder_has_extension_method(c->binder, recv, callee->member.name))
        exact_method = false;
    }
    if (exact_method && !expr->call.is_intrinsic) {
      zan_binder_record_reference(c->binder, called_sym, callee);
      /* The winning outer signature supplies the target. Use the
       * existing delegate resolver for the actual method-group symbol. */
      for (int i = 0; i < argc; i++) {
        zan_ast_node_t *arg = expr->call.args.items[i];
        zan_type_t *target = zan_binder_method_group_target(arg);
        if (target)
          checker_method_group_candidate(c, arg, target, true);
      }
    }
    c->last_call_node = expr;
    c->last_call_method = called_sym;
    if (observe &&
        (unproven_operand || c->diag->error_count != errors_before ||
         (called_sym && called_sym->kind == SYM_METHOD && !exact_method)))
      c->last_unproven_reference_expr = expr;
    if (!callee_is_method && (checker_is_task_call(expr, "Run") ||
                              checker_is_task_call(expr, "Spawn"))) {
      zan_type_t *tr = task_run_result_type(c, expr, arg_types);
      if (tr)
        return tr;
    }
    if (!callee_is_method && !called_sym && argc == 2 && expr->call.callee &&
        expr->call.callee->kind == AST_MEMBER_ACCESS) {
      zan_ast_node_t *callee = expr->call.callee;
      zan_ast_node_t *obj = callee->member.object;
      zan_istr_t owner = {NULL, 0};
      if (obj->kind == AST_IDENTIFIER &&
          !checker_find_local(c, obj->ident.name))
        owner = obj->ident.name;
      else if (obj->kind == AST_MEMBER_ACCESS)
        owner = obj->member.name;
      if (owner.len == 4 && memcmp(owner.str, "Math", 4) == 0) {
        const char *res = zan_builtin_minmax_result(
            "Math", callee->member.name.str, (int)callee->member.name.len,
            arg_types[0], arg_types[1]);
        if (res) {
          if (strcmp(res, "int") == 0)
            return c->binder->type_int;
          if (strcmp(res, "long") == 0)
            return c->binder->type_long;
          if (strcmp(res, "uint") == 0)
            return c->binder->type_uint;
          if (strcmp(res, "ulong") == 0)
            return c->binder->type_ulong;
          if (strcmp(res, "nint") == 0)
            return c->binder->type_nint;
          if (strcmp(res, "float") == 0)
            return c->binder->type_float;
          return c->binder->type_double;
        }
        if (callee->member.name.len == 3 &&
            (memcmp(callee->member.name.str, "Min", 3) == 0 ||
             memcmp(callee->member.name.str, "Max", 3) == 0) &&
            arg_types[0] && arg_types[1] && arg_types[0]->kind != TYPE_ERROR &&
            arg_types[1]->kind != TYPE_ERROR &&
            arg_types[0]->kind != TYPE_TYPE_PARAM &&
            arg_types[1]->kind != TYPE_TYPE_PARAM) {
          int numeric_pair =
              zan_builtin_minmax_result("Math", "Min", 3, arg_types[0],
                                        arg_types[0]) &&
              zan_builtin_minmax_result("Math", "Min", 3, arg_types[1],
                                        arg_types[1]);
          zan_diag_emit_code(
              c->diag, DIAG_ERROR, expr->loc, 3055,
              numeric_pair ? "Math.%.*s has no matching numeric overload; "
                             "convert arguments explicitly"
                           : "Math.%.*s requires numeric arguments",
              (int)callee->member.name.len, callee->member.name.str);
          return c->binder->type_error;
        }
      }
    }
    if (!callee_is_method && !called_sym && expr->call.callee &&
        expr->call.callee->kind == AST_MEMBER_ACCESS) {
      zan_ast_node_t *callee = expr->call.callee;
      zan_ast_node_t *obj = callee->member.object;
      zan_istr_t owner = {NULL, 0};
      if (obj->kind == AST_IDENTIFIER &&
          !checker_find_local(c, obj->ident.name))
        owner = obj->ident.name;
      else if (obj->kind == AST_MEMBER_ACCESS)
        owner = obj->member.name;
      if (owner.len == 8 && memcmp(owner.str, "Parallel", 8) == 0) {
        return c->binder->type_void;
      }
      if (owner.len == 5 && memcmp(owner.str, "MathF", 5) == 0) {
        return c->binder->type_float;
      }
      if (owner.len == 4 && memcmp(owner.str, "Math", 4) == 0) {
        zan_istr_t mn = callee->member.name;
        if (mn.len == 3 && memcmp(mn.str, "Abs", 3) == 0 && argc == 1 &&
            arg_types[0] && type_is_numeric(arg_types[0])) {
          if (arg_types[0]->kind == TYPE_FLOAT)
            return c->binder->type_float;
          if (arg_types[0]->kind == TYPE_DOUBLE)
            return c->binder->type_double;
          if (arg_types[0]->kind == TYPE_LONG)
            return c->binder->type_long;
          return c->binder->type_int;
        }
        if (mn.len == 5 && memcmp(mn.str, "Clamp", 5) == 0 && argc == 3 &&
            arg_types[0] && arg_types[1] && arg_types[2] &&
            type_is_numeric(arg_types[0]) && type_is_numeric(arg_types[1]) &&
            type_is_numeric(arg_types[2])) {
          return promote_numeric(
              c->binder, promote_numeric(c->binder, arg_types[0], arg_types[1]),
              arg_types[2]);
        }
        if (mn.len == 16 && memcmp(mn.str, "FusedMultiplyAdd", 16) == 0 &&
            argc == 3 && arg_types[0] && arg_types[1] && arg_types[2]) {
          if (arg_types[0]->kind == TYPE_FLOAT &&
              arg_types[1]->kind == TYPE_FLOAT &&
              arg_types[2]->kind == TYPE_FLOAT)
            return c->binder->type_float;
          return c->binder->type_double;
        }
      }
    }
    if (!callee_is_method && recv && recv->kind == TYPE_STRUCT &&
        recv->name.len == 3 && memcmp(recv->name.str, "Vec", 3) == 0 &&
        recv->type_args && recv->type_arg_count >= 2 && recv->type_args[1]) {
      zan_istr_t mn = expr->call.callee->member.name;
      if ((mn.len == 3 && memcmp(mn.str, "Sum", 3) == 0) ||
          (mn.len == 3 && memcmp(mn.str, "Dot", 3) == 0) ||
          (mn.len == 4 && memcmp(mn.str, "HMin", 4) == 0) ||
          (mn.len == 4 && memcmp(mn.str, "HMax", 4) == 0) ||
          (argc == 0 &&
           ((mn.len == 3 && memcmp(mn.str, "Min", 3) == 0) ||
            (mn.len == 3 && memcmp(mn.str, "Max", 3) == 0))))
        return recv->type_args[1];
      if (mn.len == 5 && memcmp(mn.str, "Store", 5) == 0)
        return c->binder->type_void;
      return recv;
    }
    if (!callee_is_method && recv) {
      zan_type_t *br =
          builtin_call_result_type(c, recv, expr->call.callee->member.name);
      if (br)
        return br;
    }
    if (!callee_is_method && callee_type &&
        callee_type->kind == TYPE_DELEGATE) {
      zan_type_t *ret = callee_type->delegate_ret_type
                            ? callee_type->delegate_ret_type
                            : c->binder->type_void;
      no_runtime_warn_arc_return(c, expr, ret);
      return ret;
    }
    if (callee_type &&
        (callee_type->kind == TYPE_CLASS || callee_type->kind == TYPE_STRUCT) &&
        callee_type->sym) {
      zan_istr_t op_name = {(char *)"op_call", 7};
      zan_symbol_t *op = checker_find_method(callee_type->sym, op_name);
      if (op && op->decl && op->decl->kind == AST_METHOD_DECL &&
          op->decl->method_decl.return_type) {
        zan_type_t *ret = zan_binder_resolve_type(
            c->binder, op->decl->method_decl.return_type);
        no_runtime_warn_arc_return(c, expr, ret);
        return ret;
      }
      if (op && op->type) {
        no_runtime_warn_arc_return(c, expr, op->type);
        return op->type;
      }
    }
    if (called_sym && called_sym->decl &&
        called_sym->decl->kind == AST_METHOD_DECL &&
        expr->call.type_args.count > 0) {
      zan_ast_node_t *m = called_sym->decl;
      zan_ast_list_t *tps = &m->method_decl.type_params;
      if (tps->count == expr->call.type_args.count &&
          m->method_decl.return_type) {
        zan_type_t *ret =
            zan_binder_resolve_type(c->binder, m->method_decl.return_type);
        zan_type_t **targs =
            zan_arena_alloc(c->arena, sizeof(*targs) * (size_t)tps->count);
        for (int i = 0; i < tps->count; i++) {
          targs[i] =
              zan_binder_resolve_type(c->binder, expr->call.type_args.items[i]);
        }
        /* Close receiver parameters except names shadowed by the
         * method's own parameters, then substitute method arguments. */
        if (recv && recv->type_args && recv->type_arg_count > 0 && recv->sym &&
            recv->sym->decl &&
            (recv->sym->decl->kind == AST_CLASS_DECL ||
             recv->sym->decl->kind == AST_STRUCT_DECL)) {
          zan_ast_list_t *class_tps = &recv->sym->decl->type_decl.type_params;
          if (class_tps->count == recv->type_arg_count) {
            zan_type_t **class_args = zan_arena_alloc(
                c->arena, sizeof(*class_args) * (size_t)class_tps->count);
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
            ret = zan_binder_subst_named(c->binder, ret, class_tps, class_args);
          }
        }
        ret = zan_binder_subst_named(c->binder, ret, tps, targs);
        no_runtime_warn_arc_return(c, expr, ret);
        return ret;
      }
    }

    if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER) {
      zan_symbol_t *fsym =
          called_sym
              ? called_sym
              : zan_binder_lookup(c->binder, expr->call.callee->ident.name);
      if (fsym && fsym->kind == SYM_METHOD && fsym->decl) {
        zan_ast_node_t *m = fsym->decl;
        if (m->method_decl.return_type) {
          zan_type_t *ret =
              zan_binder_resolve_type(c->binder, m->method_decl.return_type);
          /* 裸名调用泛型方法带显式类型实参（First<int>(xs)）：返回类型
           * 要按实参代换，否则 `a + First<int>(xs)` 报 'int' 与 'T'
           * 不可运算（E2043）。与下方成员调用分支同一口径。 */
          zan_ast_list_t *tps = &m->method_decl.type_params;
          int n = expr->call.type_args.count;
          if (n > 0 && n <= 8 && tps->count == n) {
            zan_type_t *targs[8] = {NULL};
            for (int i = 0; i < n; i++)
              targs[i] = zan_binder_resolve_type(c->binder,
                                                 expr->call.type_args.items[i]);
            ret = zan_binder_subst_named(c->binder, ret, tps, targs);
          }
          no_runtime_warn_arc_return(c, expr, ret);
          return ret;
        }
        return c->binder->type_void;
      }
    }

    /* Generic receivers have no call_arg_signature. Type their selected
     * method even when it is the initial overload, and close its return
     * type using the type arguments of its declaring class or struct. */
    zan_ast_list_t *recv_tps = NULL;
    bool uninstantiated_generic_type_recv =
        callee_is_method && recv && recv->sym && recv->sym->decl &&
        (recv->sym->decl->kind == AST_CLASS_DECL ||
         recv->sym->decl->kind == AST_STRUCT_DECL) &&
        expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
        expr->call.callee->member.object &&
        expr->call.callee->member.object->kind == AST_IDENTIFIER &&
        !expr->call.callee->member.object->ident.inst_type_ref &&
        checker_method_group_type_receiver(
            c, expr->call.callee->member.object) == recv->sym &&
        recv->type_arg_count == 0 &&
        recv->sym->decl->type_decl.type_params.count > 0;
    if (callee_is_method && recv && called_sym &&
        called_sym->parent == recv->sym && recv->sym->decl &&
        (recv->sym->decl->kind == AST_CLASS_DECL ||
         recv->sym->decl->kind == AST_STRUCT_DECL)) {
      zan_ast_list_t *tps = &recv->sym->decl->type_decl.type_params;
      if ((recv->type_arg_count > 0 && tps->count == recv->type_arg_count) ||
          uninstantiated_generic_type_recv)
        recv_tps = tps;
    }
    if (called_sym && called_sym->decl &&
        called_sym->decl->kind == AST_METHOD_DECL &&
        (typed_overload_changed ||
         (recv_tps && called_sym->decl->method_decl.type_params.count == 0))) {
      zan_ast_node_t *m = called_sym->decl;
      zan_type_t *ret =
          m->method_decl.return_type
              ? zan_binder_resolve_type(c->binder, m->method_decl.return_type)
              : c->binder->type_void;
      if (recv_tps) {
        zan_type_t **subst_args = recv->type_args;
        /* Static calls on an uninstantiated generic type receiver (e.g.
         * Box.Create(8)) inherit open class type parameters from the receiver
         * symbol. Infer concrete bindings from call arguments when possible
         * while leaving unbound parameters intact inside generic bodies. */
        if ((called_sym->modifiers & MOD_STATIC) &&
            uninstantiated_generic_type_recv && recv_tps->count > 0 &&
            recv_tps->count <= 8) {
          zan_type_t *inferred[8] = {0};
          zan_ast_list_t *mps = &m->method_decl.params;
          for (int i = 0; i < expr->call.args.count && i < mps->count; i++) {
            zan_ast_node_t *param = mps->items[i];
            if (!param || param->kind != AST_PARAM || !param->param.type)
              continue;
            zan_type_t *dp = zan_binder_resolve_type(c->binder, param->param.type);
            if (param->param.is_params && dp && dp->kind == TYPE_ARRAY)
              dp = dp->element_type;
            checker_unify_method_tp(dp, arg_types[i], recv_tps, inferred,
                                    recv_tps->count);
          }
          bool any_inferred = false;
          for (int i = 0; i < recv_tps->count; i++) {
            if (inferred[i])
              any_inferred = true;
            else if (i < recv->type_arg_count)
              inferred[i] = recv->type_args[i];
          }
          if (any_inferred) {
            zan_type_t **copied = zan_arena_alloc(
                c->arena, sizeof(*copied) * (size_t)recv_tps->count);
            memcpy(copied, inferred, sizeof(*copied) * (size_t)recv_tps->count);
            subst_args = copied;
          }
        }
        if (subst_args)
          ret = zan_binder_subst_named(c->binder, ret, recv_tps, subst_args);
      }
      no_runtime_warn_arc_return(c, expr, ret);
      return ret;
    }
    if (arg_sig && arg_sig->decl && arg_sig->decl->kind == AST_METHOD_DECL) {
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
    no_alloc_reject(c, expr->loc, "string interpolation");
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
                           "cannot interpolate '%s' -- convert the operand "
                           "explicitly instead of formatting the reference",
                           type_name(pt));
      }
    }
    return c->binder->type_string;
  }

  case AST_MEMBER_ACCESS: {
    int errors_before = c->diag->error_count;
    zan_type_t *obj_type = zan_checker_check_expr(c, expr->member.object);
    if (c->binder->symbol_reference && c->diag->error_count != errors_before)
      c->last_unproven_reference_expr = expr->member.object;
    return check_member_access(c, expr, obj_type, false, NULL);
  }

  case AST_INDEX: {
    zan_type_t *obj = zan_checker_check_expr(c, expr->index.object);
    zan_type_t *idx = zan_checker_check_expr(c, expr->index.index);
    if ((obj->kind == TYPE_CLASS || obj->kind == TYPE_STRUCT) && obj->sym) {
      zan_istr_t op_name = {(char *)"op_index", 8};
      zan_symbol_t *op = NULL;
      int op_count = 0;
      for (zan_symbol_t *ts = obj->sym; ts;
           ts = (ts->type && ts->type->base_type) ? ts->type->base_type->sym
                                                  : NULL) {
        for (int mi = 0; mi < ts->member_count; mi++) {
          zan_symbol_t *mm = ts->members[mi];
          if (mm && mm->kind == SYM_METHOD && mm->name.len == op_name.len &&
              memcmp(mm->name.str, op_name.str, (size_t)op_name.len) == 0) {
            if (!op)
              op = mm;
            op_count++;
          }
        }
      }
      if (op_count == 1 && op && op->decl &&
          op->decl->kind == AST_METHOD_DECL &&
          op->decl->method_decl.return_type) {
        /* 索引器形参与实参类型匹配校验 */
        zan_ast_list_t *ps = &op->decl->method_decl.params;
        bool is_static = (op->decl->method_decl.modifiers & MOD_STATIC) != 0;
        int self_off = is_static ? 1 : 0;
        int given = 1 + expr->index.extra.count;
        int want = ps->count - self_off;
        bool any_generic = false;
        for (int pi = self_off; pi < ps->count; pi++) {
          zan_ast_node_t *pp = ps->items[pi];
          if (pp && pp->kind == AST_PARAM && pp->param.type) {
            zan_type_t *pt = zan_binder_resolve_type(c->binder, pp->param.type);
            if (pt && type_refs_type_param(pt))
              any_generic = true;
          }
        }
        if (!any_generic && ps->count >= self_off && want != given) {
          zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2053,
                             "'%.*s' indexer takes %d index%s, but %d given",
                             (int)obj->sym->name.len, obj->sym->name.str, want,
                             want == 1 ? "" : "es", given);
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
        zan_type_t *ret_ty = zan_binder_resolve_type(
            c->binder, op->decl->method_decl.return_type);
        if (obj->type_args && obj->type_arg_count > 0 && obj->sym &&
            obj->sym->decl &&
            (obj->sym->decl->kind == AST_CLASS_DECL ||
             obj->sym->decl->kind == AST_STRUCT_DECL)) {
          zan_ast_list_t *tps = &obj->sym->decl->type_decl.type_params;
          if (tps->count == obj->type_arg_count) {
            ret_ty =
                zan_binder_subst_named(c->binder, ret_ty, tps, obj->type_args);
          }
        }
        return ret_ty;
      }
      if (op && op_count > 0 && op->decl && op->decl->kind == AST_METHOD_DECL &&
          op->decl->method_decl.return_type) {
        zan_type_t *ret_ty = zan_binder_resolve_type(
            c->binder, op->decl->method_decl.return_type);
        if (obj->type_args && obj->type_arg_count > 0 && obj->sym &&
            obj->sym->decl &&
            (obj->sym->decl->kind == AST_CLASS_DECL ||
             obj->sym->decl->kind == AST_STRUCT_DECL)) {
          zan_ast_list_t *tps = &obj->sym->decl->type_decl.type_params;
          if (tps->count == obj->type_arg_count) {
            ret_ty =
                zan_binder_subst_named(c->binder, ret_ty, tps, obj->type_args);
          }
        }
        return ret_ty;
      }
      if (op && op->type)
        return op->type;
    }
    if (obj->kind == TYPE_ARRAY && obj->element_type) {
      int rank = obj->array_rank > 1 ? obj->array_rank : 1;
      int given = 1 + expr->index.extra.count;
      if (given != rank) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2055,
                           "rank-%d array indexed with %d indices", rank,
                           given);
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
    if (obj->kind == TYPE_STRUCT &&
        ((obj->name.len == 4 && memcmp(obj->name.str, "Span", 4) == 0) ||
         (obj->name.len == 12 &&
          memcmp(obj->name.str, "ReadOnlySpan", 12) == 0))) {
      for (int i = 0; i < expr->index.extra.count; i++)
        zan_checker_check_expr(c, expr->index.extra.items[i]);
      return (obj->type_args && obj->type_arg_count > 0) ? obj->type_args[0]
                                                         : c->binder->type_int;
    }
    if (obj->kind == TYPE_STRUCT &&
        ((obj->name.len == 11 &&
          memcmp(obj->name.str, "InlineArray", 11) == 0) ||
         (obj->name.len == 3 && memcmp(obj->name.str, "Vec", 3) == 0))) {
      for (int i = 0; i < expr->index.extra.count; i++)
        zan_checker_check_expr(c, expr->index.extra.items[i]);
      return (obj->type_args && obj->type_arg_count >= 2) ? obj->type_args[1]
                                                          : c->binder->type_int;
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
    zan_type_t *target =
        checker_assignment_target_type(c, expr->binary.left, left);
    if (!target && expr->binary.left->kind == AST_INDEX) {
      target = checker_index_set_target(c, expr->binary.left, right);
      if (!target) {
        zan_type_t *iobj =
            zan_checker_check_expr(c, expr->binary.left->index.object);
        if (iobj && iobj->sym &&
            (iobj->kind == TYPE_CLASS || iobj->kind == TYPE_STRUCT)) {
          zan_istr_t set_name = {(char *)"op_index_set", 12};
          bool has_set = false;
          for (int mi = 0; mi < iobj->sym->member_count; mi++) {
            zan_symbol_t *mm = iobj->sym->members[mi];
            if (mm && mm->kind == SYM_METHOD && mm->name.len == set_name.len &&
                memcmp(mm->name.str, set_name.str, (size_t)set_name.len) == 0) {
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
    if (!target)
      target = left;
    if (target)
      checker_check_assignable(c, target, right, expr->binary.right, expr->loc,
                               "assignment");
    check_closure_cycle_warning(c, expr->binary.left, expr->binary.right,
                                expr->loc);
    zan_istr_t ne_name = {NULL, 0};
    if (checker_expr_is_noescape_param(c, expr->binary.right, &ne_name)) {
      zan_diag_emit_code(
          c->diag, DIAG_ERROR, expr->loc, 2142,
          "non-escaping parameter '%.*s' cannot escape via assignment",
          (int)ne_name.len, ne_name.str);
    }
    return right;
  }
