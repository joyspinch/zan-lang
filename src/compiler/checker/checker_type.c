
static void ctor_arity(zan_ast_node_t *decl, int *min, int *max) {
    int total = decl->method_decl.params.count;
    int required = 0;
    bool variadic = false;
    for (int i = 0; i < total; i++) {
        zan_ast_node_t *p = decl->method_decl.params.items[i];
        if (!p || p->kind != AST_PARAM) { required++; continue; }
        if (p->param.is_params) { variadic = true; continue; }
        if (!p->param.default_val) required++;
    }
    *min = required;
    *max = variadic ? -1 : total;
}

static int ctor_arg_count(zan_checker_t *c, zan_ast_node_t *expr,
                          zan_symbol_t *type_sym) {
    int n = expr->new_expr.args.count;
    while (n > 0) {
        zan_ast_node_t *a = expr->new_expr.args.items[n - 1];
        if (a && a->kind == AST_COLL_INIT) {
            if (!checker_find_field(type_sym, a->coll_init.name)) break;
            n--;
            continue;
        }
        if (!a || a->kind != AST_ASSIGNMENT || !a->binary.left ||
            a->binary.left->kind != AST_IDENTIFIER ||
            !checker_find_field(type_sym, a->binary.left->ident.name))
            break;
        n--;
    }
    (void)c;
    return n;
}

static bool arg_is_initializer_entry(zan_ast_node_t *a) {
    if (!a) return false;
    if (a->kind == AST_COLL_INIT) return true;
    return a->kind == AST_ASSIGNMENT && a->binary.left &&
           a->binary.left->kind == AST_IDENTIFIER;
}

static bool initializer_covers_all_members(zan_ast_node_t *expr,
                                           zan_symbol_t *type_sym,
                                           int init_start) {
    int covered = 0;
    int members = 0;
    for (zan_symbol_t *t = type_sym; t; ) {
        for (int i = 0; i < t->member_count; i++) {
            zan_symbol_t *m = t->members[i];
            if (!m || (m->kind != SYM_FIELD && m->kind != SYM_PROPERTY))
                continue;
            if (m->modifiers & MOD_STATIC) continue;
            members++;
            for (int a = init_start; a < expr->new_expr.args.count; a++) {
                zan_ast_node_t *as = expr->new_expr.args.items[a];
                if (!arg_is_initializer_entry(as)) continue;
                zan_istr_t n = as->kind == AST_COLL_INIT
                    ? as->coll_init.name
                    : as->binary.left->ident.name;
                if (n.len == m->name.len &&
                    memcmp(n.str, m->name.str, (size_t)n.len) == 0) {
                    covered++;
                    break;
                }
            }
        }
        t = (t->type && t->type->base_type) ? t->type->base_type->sym : NULL;
    }
    return members > 0 && covered == members;
}

static bool type_is_scalar_primitive(zan_type_t *t);

static void check_ctor_available(zan_checker_t *c, zan_type_t *type,
                                 zan_ast_node_t *expr) {
    if (!type || expr->new_expr.is_array) return;
    if (type_is_scalar_primitive(type)) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2016,
            "the builtin type '%s' has no constructor -- use literals, casts or the conversion functions",
            type_name(type));
        return;
    }
    if (type->kind != TYPE_CLASS || !type->sym) return;
    zan_symbol_t *sym = type->sym;
    int declared = 0;
    int argc = ctor_arg_count(c, expr, sym);
    if (argc == 0 && argc < expr->new_expr.args.count &&
        initializer_covers_all_members(expr, sym, argc)) {
        return;
    }
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (!m || m->kind != SYM_CONSTRUCTOR || !m->decl) continue;
        if (m->decl->kind != AST_CONSTRUCTOR_DECL) continue;
        declared++;
        int lo = 0;
        int hi = 0;
        ctor_arity(m->decl, &lo, &hi);
        if (argc >= lo && (hi < 0 || argc <= hi)) {
            if (!access_member_allowed(c, m))
                report_inaccessible(c, m, expr->loc);
            return;
        }
    }
    if (declared == 0) {
        if (argc > 0) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2017,
                "no constructor of '%s' accepts %d arguments",
                type_name(type), argc);
        }
        return;
    }
    zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2018,
        "no constructor of '%s' takes %d argument(s); a class that declares "
        "constructors has no implicit parameterless one",
        type_name(type), argc);
}

static bool type_is_numeric(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BYTE: case TYPE_SHORT: case TYPE_INT: case TYPE_LONG:
    case TYPE_SBYTE: case TYPE_USHORT: case TYPE_UINT: case TYPE_ULONG:
    case TYPE_FLOAT: case TYPE_DOUBLE: case TYPE_DECIMAL: case TYPE_NINT:
        return true;
    default:
        return false;
    }
}

static bool type_is_scalar_primitive(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BOOL: case TYPE_BYTE: case TYPE_SHORT: case TYPE_INT:
    case TYPE_LONG: case TYPE_SBYTE: case TYPE_USHORT: case TYPE_UINT:
    case TYPE_ULONG: case TYPE_FLOAT: case TYPE_DOUBLE: case TYPE_DECIMAL: case TYPE_CHAR:
    case TYPE_STRING: case TYPE_NINT:
        return true;
    default:
        return false;
    }
}

static bool type_is_integral(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BYTE: case TYPE_SHORT: case TYPE_INT: case TYPE_LONG:
    case TYPE_SBYTE: case TYPE_USHORT: case TYPE_UINT: case TYPE_ULONG:
    case TYPE_NINT:
        return true;
    default:
        return false;
    }
}

/* 一元运算符的操作数必须能降级成算术：数值（或类/结构体上的重载路径）之外的
   一切都要报人话诊断。
   原来的写法只 `return c->binder->type_error` —— 而这个哨兵**本身不发诊断**，
   错误就一路漏进 irgen：`-"x"` 造出对字符串指针的整数减法（LLVM 校验失败：
   "Integer arithmetic operators only work with integral types"），`~"x"` 更是
   在发射非法 IR 的过程中把编译器自己打崩（0xC0000005）。
   二元路径早就有 "cannot apply operator to 'X' and 'Y'"（同一条口径：数值与
   char 之外一律拒），一元这边补齐。 */
static void check_unary_operand_type(zan_checker_t *c, zan_ast_node_t *expr,
                                     zan_type_t *operand, char sign) {
    if (!operand) return;
    /* type_error 已经有别的诊断在报；类型参数会在实例化时被具体类型替换。 */
    if (operand->kind == TYPE_ERROR || operand->kind == TYPE_TYPE_PARAM) return;
    zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2019,
                  "cannot apply unary '%c' to '%s'", sign, type_name(operand));
}

static zan_type_t *promote_numeric(zan_binder_t *b, zan_type_t *a, zan_type_t *b_type) {
    if (!a || !b_type) return b->type_error;
    bool a_dec = (a->kind == TYPE_DECIMAL);
    bool b_dec = (b_type->kind == TYPE_DECIMAL);
    if (a_dec && b_dec) return b->type_decimal;
    if (a_dec) {
        if (b_type->kind == TYPE_FLOAT || b_type->kind == TYPE_DOUBLE) return b->type_error;
        return b->type_decimal;
    }
    if (b_dec) {
        if (a->kind == TYPE_FLOAT || a->kind == TYPE_DOUBLE) return b->type_error;
        return b->type_decimal;
    }
    if (a->kind == TYPE_DOUBLE || b_type->kind == TYPE_DOUBLE) return b->type_double;
    if (a->kind == TYPE_FLOAT  || b_type->kind == TYPE_FLOAT)  return b->type_float;
    if (a->kind == TYPE_ULONG  || b_type->kind == TYPE_ULONG)  return b->type_ulong;
    if (a->kind == TYPE_LONG   || b_type->kind == TYPE_LONG)   return b->type_long;
    if (a->kind == TYPE_NINT   || b_type->kind == TYPE_NINT)   return b->type_nint;
    return b->type_int;
}


static bool checker_type_equal(zan_type_t *a, zan_type_t *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_NULLABLE) {
        if (a->kind == TYPE_ARRAY && a->array_rank != b->array_rank)
            return false;
        return checker_type_equal(a->element_type, b->element_type);
    }
    if (a->sym && b->sym && a->sym == b->sym) {
        /* 同一符号，实参匹配即可 */
    } else if (a->name.len != b->name.len ||
        (a->name.len && memcmp(a->name.str, b->name.str, (size_t)a->name.len) != 0)) {
        return false;
    }
    if (a->type_arg_count != b->type_arg_count) return false;
    for (int i = 0; i < a->type_arg_count; i++)
        if (!checker_type_equal(a->type_args[i], b->type_args[i])) return false;
    return true;
}

/* 检查是否one declared base edge proves a derivation */
static bool iface_edge_derives_params_ast(zan_ast_node_t *edge_ref,
                                          zan_ast_list_t *tps,
                                          zan_type_t **args,
                                          zan_type_t *sup) {
    if (!edge_ref || edge_ref->kind != AST_TYPE_REF || !sup || !sup->sym)
        return false;
    zan_istr_t en = edge_ref->type_ref.name;
    if (en.len != sup->sym->name.len ||
        memcmp(en.str, sup->sym->name.str, (size_t)en.len) != 0)
        return false;
    zan_ast_list_t *eargs = &edge_ref->type_ref.type_args;
    int want = sup->type_arg_count;
    if (eargs->count != want) return false;
    for (int j = 0; j < want; j++) {
        zan_ast_node_t *ea = eargs->items[j];
        if (!ea) return false;
        if (ea->kind == AST_TYPE_REF && ea->type_ref.type_args.count > 0)
            return false;
        zan_istr_t ename;
        if (ea->kind == AST_IDENTIFIER) ename = ea->ident.name;
        else if (ea->kind == AST_TYPE_REF) ename = ea->type_ref.name;
        else return false;
        int k = 0;
        for (; k < tps->count; k++) {
            zan_istr_t tn = tps->items[k]->ident.name;
            if (tn.len == ename.len &&
                memcmp(tn.str, ename.str, (size_t)tn.len) == 0)
                break;
        }
        if (k < tps->count) {
            if (args == NULL || args[k] == NULL ||
                !checker_type_equal(args[k], sup->type_args[j]))
                return false;
        } else {
            zan_type_t *sa = sup->type_args[j];
            if (!sa || ename.len != sa->name.len ||
                memcmp(ename.str, sa->name.str, (size_t)sa->name.len) != 0)
                return false;
        }
    }
    return true;
}

static bool checker_type_derives_from_depth(zan_type_t *sub, zan_type_t *sup,
                                            int depth) {
    if (!sub || !sup) return false;
    if (depth > CHECKER_DERIVES_MAX_DEPTH) return false;
    if (sub == sup) return true;
    if (sub->sym && sup->sym && sub->sym == sup->sym) {
        if (sup->type_arg_count == 0 && sup->sym->decl &&
            (sup->sym->decl->kind == AST_CLASS_DECL || sup->sym->decl->kind == AST_STRUCT_DECL) &&
            sup->sym->decl->type_decl.type_params.count > 0)
            return true;
        if (sub->sym->decl && sub->sym->decl->kind == AST_INTERFACE_DECL &&
            sub->type_arg_count == sup->type_arg_count && sub->type_arg_count > 0) {
            bool all_match = true;
            for (int i = 0; i < sub->type_arg_count; i++) {
                int var = zan_type_param_variance(sub->sym, i);
                zan_type_t *a = sub->type_args[i];
                zan_type_t *b = sup->type_args[i];
                if (!a || !b) { all_match = false; break; }
                if (var == 1) {
                    if (checker_type_equal(a, b)) continue;
                    if (checker_type_is_ref(a) && checker_type_is_ref(b) &&
                        checker_type_derives_from_depth(a, b, depth + 1))
                        continue;
                    all_match = false; break;
                } else if (var == -1) {
                    if (checker_type_equal(a, b)) continue;
                    if (checker_type_is_ref(a) && checker_type_is_ref(b) &&
                        checker_type_derives_from_depth(b, a, depth + 1))
                        continue;
                    all_match = false; break;
                } else {
                    if (!checker_type_equal(a, b)) { all_match = false; break; }
                }
            }
            if (all_match) return true;
        }
        return checker_type_equal(sub, sup);
    }
    if (sub->kind == sup->kind && checker_type_equal(sub, sup)) return true;
    for (int i = 0; i < sub->interface_count; i++)
        if (checker_type_derives_from_depth(sub->interfaces[i], sup, depth + 1))
            return true;
    zan_type_t *base = sub->base_type;
    if (!base && sub->sym && sub->sym->type && sub->sym->type != sub)
        base = sub->sym->type->base_type;
    if (base && base != sub)
        return checker_type_derives_from_depth(base, sup, depth + 1);
    if (sub->type_arg_count > 0 && sub->sym && sub->sym->type &&
        sub->sym->type != sub && sub->sym->decl &&
        (sub->sym->decl->kind == AST_CLASS_DECL ||
         sub->sym->decl->kind == AST_STRUCT_DECL ||
         sub->sym->decl->kind == AST_INTERFACE_DECL)) {
        zan_ast_list_t *tps = &sub->sym->decl->type_decl.type_params;
        zan_ast_list_t *bases = &sub->sym->decl->type_decl.bases;

        for (int i = 0; i < bases->count; i++)
            if (iface_edge_derives_params_ast(bases->items[i], tps,
                                              sub->type_args, sup))
                return true;
    }
    return false;
}

static bool checker_type_derives_from(zan_type_t *sub, zan_type_t *sup) {
    return checker_type_derives_from_depth(sub, sup, 0);
}

static bool checker_type_is_ref(zan_type_t *t) {
    if (!t) return false;
    return t->kind == TYPE_OBJECT || t->kind == TYPE_INTERFACE ||
           t->kind == TYPE_STRING || t->kind == TYPE_CLASS ||
           t->kind == TYPE_ARRAY || t->kind == TYPE_DELEGATE ||
           t->kind == TYPE_TASK;
}

static zan_type_t *merge_conditional_types(zan_checker_t *c,
                                           zan_type_t *a, zan_type_t *b);
static bool expr_is_null_literal(zan_ast_node_t *e);
static zan_type_t *merge_conditional_types(zan_checker_t *c,
                                           zan_type_t *a, zan_type_t *b) {
    if (!a) return b;
    if (!b) return a;
    if (a == c->binder->type_error) return b;
    if (b == c->binder->type_error) return a;
    if (a == b) return a;
    if (a->kind == TYPE_TYPE_PARAM || b->kind == TYPE_TYPE_PARAM) return a;

    if (a->kind == b->kind && checker_type_equal(a, b)) return a;

    if ((type_is_numeric(a) || a->kind == TYPE_CHAR) &&
        (type_is_numeric(b) || b->kind == TYPE_CHAR))
        return promote_numeric(c->binder, a, b);

    if (checker_type_is_ref(a) && checker_type_is_ref(b)) {
        if (checker_type_derives_from(b, a)) return a;  /* b is-a a */
        if (checker_type_derives_from(a, b)) return b;  /* a is-a b */
        for (zan_type_t *t = a->base_type; t && t != a; t = t->base_type) {
            if (checker_type_derives_from(b, t)) return t;
            if (!t->base_type) break;
        }
        return NULL;
    }

    if (a->kind == TYPE_ENUM || b->kind == TYPE_ENUM) return a;

    return NULL;
}

static bool same_generic_container(zan_type_t *a, zan_type_t *b) {
    if (!a || !b || a->kind != b->kind) return false;
    if (a->type_arg_count != b->type_arg_count || a->type_arg_count == 0)
        return false;
    if (a->sym && b->sym) return a->sym == b->sym;
    return a->name.len == b->name.len &&
           memcmp(a->name.str, b->name.str, (size_t)a->name.len) == 0;
}

/* 同名泛型接口的实参兼容：按形参方差位逐位判定（B-ID63，与 derives 的
 * 接口方差分支同口径）。协变位要求 from 实参 is-a target 实参（且都是
 * 引用形态），逆变位反向。只在接口显式声明了 out/in 的形参上收紧；
 * 无任何方差声明（内建接口与未标记的用户接口）保持旧口径放行——
 * 全面收紧未标记接口是独立语义变更，不随本修。 */
static bool checker_variant_args_ok(zan_type_t *target, zan_type_t *value) {
    if (!target->sym || !target->sym->decl) return true;
    bool any_variance = false;
    for (int i = 0; i < target->type_arg_count; i++)
        if (zan_type_param_variance(target->sym, i) != 0) { any_variance = true; break; }
    if (!any_variance) return true;
    for (int i = 0; i < target->type_arg_count; i++) {
        int var = zan_type_param_variance(target->sym, i);
        zan_type_t *targ = target->type_args[i];
        zan_type_t *varg = value->type_arg_count > i ? value->type_args[i] : NULL;
        if (!targ || !varg) return false;
        if (checker_type_equal(targ, varg)) continue;
        if (var == 1) {
            if (checker_type_is_ref(targ) && checker_type_is_ref(varg) &&
                checker_type_derives_from(varg, targ))
                continue;
            return false;
        } else if (var == -1) {
            if (checker_type_is_ref(targ) && checker_type_is_ref(varg) &&
                checker_type_derives_from(targ, varg))
                continue;
            return false;
        }
        return false;
    }
    return true;
}

static bool checker_is_byte_buffer(zan_type_t *t) {
    if (!t || t->kind != TYPE_ARRAY || !t->element_type) return false;
    return t->element_type->kind == TYPE_BYTE ||
           t->element_type->kind == TYPE_SBYTE ||
           t->element_type->kind == TYPE_CHAR;
}


/* Binding<T> 允许接收 T 值（降级成绑定对象）；按名字识别，
 * 与 checker_type_assignable 里的放行规则保持同一口径。 */
static bool checker_type_is_binding(zan_type_t *t) {
    return t && t->kind == TYPE_CLASS && t->name.len == 7 &&
           memcmp(t->name.str, "Binding", 7) == 0 && t->type_arg_count == 1;
}

static bool checker_struct_boxable(zan_type_t *t, int depth);

static bool checker_type_assignable(zan_type_t *target, zan_type_t *value) {
    if (!target || !value) return true;
    if (target == value) return true;
    if (target->kind == TYPE_ERROR || value->kind == TYPE_ERROR) return true;
    if (target->kind == TYPE_TYPE_PARAM || value->kind == TYPE_TYPE_PARAM)
        return true;

    /* 可空引用类型安全契约：可空引用 (T?) 不能隐式赋给非空引用目标 (T) */
    if (value->is_nullable && !target->is_nullable) {
        return false;
    }

    if (target->kind == value->kind && checker_type_equal(target, value))
        return true;

    if (target->kind == TYPE_CLASS && target->name.len == 7 &&
        memcmp(target->name.str, "Binding", 7) == 0 &&
        target->type_arg_count == 1 && target->type_args[0]) {
        if (value->kind == TYPE_OBJECT) return true;
        return checker_type_assignable(target->type_args[0], value);
    }

    if (target->kind == TYPE_NULLABLE && value->kind == TYPE_OBJECT)
        return true; /* null -> T? */
    if (target->kind == TYPE_NULLABLE && value->kind != TYPE_NULLABLE &&
        value->kind != TYPE_OBJECT && target->element_type)
        return checker_type_assignable(target->element_type, value); /* T -> T? */

    if (target->kind == TYPE_DELEGATE) {
        if (value->kind == TYPE_OBJECT) return true;
        if (value->kind == TYPE_DELEGATE) {
            if (checker_type_equal(target, value)) return true;
            bool same_delegate = false;
            if (target->sym && value->sym && target->sym == value->sym) {
                same_delegate = true;
            } else if (target->name.len == value->name.len &&
                       target->name.len > 0 &&
                       memcmp(target->name.str, value->name.str, target->name.len) == 0) {
                same_delegate = true;
            }
            if (same_delegate && target->type_arg_count == value->type_arg_count && target->type_arg_count > 0) {
                bool all_match = true;
                for (int i = 0; i < target->type_arg_count; i++) {
                    int var = 0;
                    if (target->sym) {
                        var = zan_type_param_variance(target->sym, i);
                    }
                    if (var == 0 && target->name.len == 4 && memcmp(target->name.str, "Func", 4) == 0) {
                        var = (i == target->type_arg_count - 1) ? 1 : -1;
                    } else if (var == 0 && target->name.len == 6 && memcmp(target->name.str, "Action", 6) == 0) {
                        var = -1;
                    }
                    zan_type_t *targ = target->type_args[i];
                    zan_type_t *varg = value->type_args[i];
                    if (!targ || !varg) { all_match = false; break; }
                    if (var == 1) {
                        if (checker_type_equal(targ, varg)) continue;
                        if (checker_type_is_ref(targ) && checker_type_is_ref(varg) &&
                            checker_type_derives_from(varg, targ))
                            continue;
                        all_match = false; break;
                    } else if (var == -1) {
                        if (checker_type_equal(targ, varg)) continue;
                        if (checker_type_is_ref(targ) && checker_type_is_ref(varg) &&
                            checker_type_derives_from(targ, varg))
                            continue;
                        all_match = false; break;
                    } else {
                        if (!checker_type_equal(targ, varg)) { all_match = false; break; }
                    }
                }
                if (all_match) return true;
            }
        }
        return false;
    }

    if (target->kind == TYPE_TASK && value->kind == TYPE_TASK)
        return value->type_arg_count >= target->type_arg_count;
    /* Task 句柄本质是一个 long 帧指针：允许把它取回裸句柄
     * （Task.IsDone(h) / TaskJoin.WhenAll(handles) 这类 API 收的就是 long）。 */
    if (target->kind == TYPE_LONG && value->kind == TYPE_TASK) return true;
    bool tnum = type_is_numeric(target), vnum = type_is_numeric(value);
    if (target->kind == TYPE_DECIMAL && (value->kind == TYPE_FLOAT || value->kind == TYPE_DOUBLE))
        return false;
    if ((target->kind == TYPE_FLOAT || target->kind == TYPE_DOUBLE) && value->kind == TYPE_DECIMAL)
        return false;
    if ((type_is_integral(target) || target->kind == TYPE_CHAR) && value->kind == TYPE_DECIMAL)
        return false;
    if (target->kind == TYPE_ENUM)
        return value->kind == TYPE_ENUM || vnum || value->kind == TYPE_CHAR;
    if (value->kind == TYPE_ENUM)
        return tnum || target->kind == TYPE_CHAR;
    if (tnum && (vnum || value->kind == TYPE_CHAR)) return true;
    if (target->kind == TYPE_CHAR && (vnum || value->kind == TYPE_CHAR))
        return true;

    if (value->kind == TYPE_OBJECT) {
        return checker_type_is_ref(target);
    }
    switch (target->kind) {
    case TYPE_STRING:
        return value->kind == TYPE_STRING || checker_is_byte_buffer(value);
    case TYPE_CLASS:
        if (value->kind == TYPE_CLASS) {
            if (target->sym && value->sym && target->sym == value->sym &&
                (target->type_arg_count == 0 || value->type_arg_count == 0))
                return true;
            /* Generic classes are invariant. Sharing the declaration does not
             * make different closed instantiations assignment compatible. */
            if (same_generic_container(target, value))
                return checker_type_equal(target, value);
            return checker_type_derives_from(value, target);
        }
        return false;
    case TYPE_INTERFACE:
        if (target->name.str) {
            /* 内建集合接口赋值规则（IEnumerable / IReadOnlyCollection / IReadOnlyList / IList / ICollection） */
            bool is_ro_coll = (target->name.len == 19 && memcmp(target->name.str, "IReadOnlyCollection", 19) == 0);
            bool is_ro_list = (target->name.len == 13 && memcmp(target->name.str, "IReadOnlyList", 13) == 0);
            bool is_enumerable = (target->name.len == 11 && memcmp(target->name.str, "IEnumerable", 11) == 0);
            bool is_list_iface = (target->name.len == 5 && memcmp(target->name.str, "IList", 5) == 0);
            bool is_coll_iface = (target->name.len == 11 && memcmp(target->name.str, "ICollection", 11) == 0);
            if (is_ro_coll || is_ro_list || is_enumerable || is_list_iface || is_coll_iface) {
                zan_type_t *te = (target->type_args && target->type_arg_count > 0) ? target->type_args[0] : NULL;
                /* List<T> -> 集合接口 */
                if (value->kind == TYPE_CLASS && value->name.len == 4 && memcmp(value->name.str, "List", 4) == 0) {
                    zan_type_t *ve = (value->type_args && value->type_arg_count > 0) ? value->type_args[0] : NULL;
                    if (!te || !ve || checker_type_assignable(te, ve)) return true;
                }
                /* T[] -> 集合接口 */
                if (value->kind == TYPE_ARRAY && value->element_type) {
                    if (!te || checker_type_assignable(te, value->element_type)) return true;
                }
                /* 接口间只读派生关系：IReadOnlyList<T> -> IReadOnlyCollection<T> -> IEnumerable<T> */
                if (value->kind == TYPE_INTERFACE && value->name.str) {
                    zan_type_t *ve = (value->type_args && value->type_arg_count > 0) ? value->type_args[0] : NULL;
                    bool v_is_ro_list = (value->name.len == 13 && memcmp(value->name.str, "IReadOnlyList", 13) == 0);
                    bool v_is_ro_coll = (value->name.len == 19 && memcmp(value->name.str, "IReadOnlyCollection", 19) == 0);
                    if (is_enumerable && (v_is_ro_list || v_is_ro_coll)) {
                        if (!te || !ve || checker_type_assignable(te, ve)) return true;
                    }
                    if (is_ro_coll && v_is_ro_list) {
                        if (!te || !ve || checker_type_assignable(te, ve)) return true;
                    }
                }
            }
            /* 字典接口规则（IReadOnlyDictionary / IDictionary） */
            bool is_ro_dict = (target->name.len == 19 && memcmp(target->name.str, "IReadOnlyDictionary", 19) == 0);
            bool is_dict_iface = (target->name.len == 11 && memcmp(target->name.str, "IDictionary", 11) == 0);
            if (is_ro_dict || is_dict_iface) {
                zan_type_t *tk = (target->type_args && target->type_arg_count > 0) ? target->type_args[0] : NULL;
                zan_type_t *tv = (target->type_args && target->type_arg_count > 1) ? target->type_args[1] : NULL;
                if (value->kind == TYPE_CLASS && ((value->name.len == 10 && memcmp(value->name.str, "Dictionary", 10) == 0) ||
                                                  (value->name.len == 4 && memcmp(value->name.str, "Dict", 4) == 0))) {
                    zan_type_t *vk = (value->type_args && value->type_arg_count > 0) ? value->type_args[0] : NULL;
                    zan_type_t *vv = (value->type_args && value->type_arg_count > 1) ? value->type_args[1] : NULL;
                    if ((!tk || !vk || checker_type_assignable(tk, vk)) &&
                        (!tv || !vv || checker_type_assignable(tv, vv)))
                        return true;
                }
                if (value->kind == TYPE_INTERFACE && is_ro_dict &&
                    value->name.len == 11 && memcmp(value->name.str, "IDictionary", 11) == 0) {
                    zan_type_t *vk = (value->type_args && value->type_arg_count > 0) ? value->type_args[0] : NULL;
                    zan_type_t *vv = (value->type_args && value->type_arg_count > 1) ? value->type_args[1] : NULL;
                    if ((!tk || !vk || checker_type_assignable(tk, vk)) &&
                        (!tv || !vv || checker_type_assignable(tv, vv)))
                        return true;
                }
            }
        }
        if (value->kind == TYPE_INTERFACE || value->kind == TYPE_CLASS ||
            value->kind == TYPE_STRUCT) {
            if (same_generic_container(target, value) &&
                !checker_type_equal(target, value)) {
                /* 同名泛型接口（value 为接口时才可能命中：same_generic_
                 * container 先比 kind）实参兼容走方差方向判定，不再无条件
                 * 放行——否则 IBox<Cat> 能塞进 IBox<Animal> 位（B-ID63）。
                 * 判不过时不再落 derives：同符号在 derives 里必同判，
                 * 这里直接给出确定结论。 */
                return checker_variant_args_ok(target, value);
            }
            return checker_type_derives_from(value, target);
        }
        return false;
    case TYPE_STRUCT:
        if (target->name.len == 12 && memcmp(target->name.str, "ReadOnlySpan", 12) == 0) {
            zan_type_t *te = (target->type_args && target->type_arg_count > 0) ? target->type_args[0] : NULL;
            if (value->kind == TYPE_STRUCT &&
                ((value->name.len == 4 && memcmp(value->name.str, "Span", 4) == 0) ||
                 (value->name.len == 12 && memcmp(value->name.str, "ReadOnlySpan", 12) == 0))) {
                zan_type_t *ve = (value->type_args && value->type_arg_count > 0) ? value->type_args[0] : NULL;
                if (te && ve && checker_type_equal(te, ve)) return true;
            }
            if (value->kind == TYPE_ARRAY && value->element_type && te &&
                checker_type_equal(te, value->element_type)) return true;
            if (value->kind == TYPE_STRING && te &&
                (te->kind == TYPE_CHAR || te->kind == TYPE_BYTE)) return true;
        }
        if (target->name.len == 4 && memcmp(target->name.str, "Span", 4) == 0) {
            zan_type_t *te = (target->type_args && target->type_arg_count > 0) ? target->type_args[0] : NULL;
            if (value->kind == TYPE_STRUCT && value->name.len == 4 &&
                memcmp(value->name.str, "Span", 4) == 0) {
                zan_type_t *ve = (value->type_args && value->type_arg_count > 0) ? value->type_args[0] : NULL;
                if (te && ve && checker_type_equal(te, ve)) return true;
            }
            if (value->kind == TYPE_ARRAY && value->element_type && te &&
                checker_type_equal(te, value->element_type)) return true;
        }
        if (value->kind == TYPE_STRUCT) {
            if (target->sym && value->sym && target->sym == value->sym) return true;
            if (same_generic_container(target, value) && checker_type_equal(target, value)) return true;
        }
        return false;
    case TYPE_ARRAY:
        return value->kind == TYPE_ARRAY &&
               checker_type_equal(target->element_type, value->element_type);
    case TYPE_OBJECT:
        if (checker_type_is_ref(value)) return true;
        /* A556-1 隐式装箱：boxable 结构体流向 object 自动装箱
         * （irgen 在赋值/初始化/传参/返回/集合槽等流动点经
         * emit_implicit_box 落地） */
        return value->kind == TYPE_STRUCT && checker_struct_boxable(value, 0);
    default:
        return false;
    }

    return false;
}

static bool integral_range(zan_type_t *t, int64_t *min_value,
                           uint64_t *max_value, bool *is_unsigned) {
    if (!t) return false;
    *is_unsigned = false;
    switch (t->kind) {
    case TYPE_SBYTE:  *min_value = -128; *max_value = 127; return true;
    case TYPE_BYTE:   *min_value = 0; *max_value = 255; *is_unsigned = true; return true;
    case TYPE_SHORT:  *min_value = -32768; *max_value = 32767; return true;
    case TYPE_USHORT: *min_value = 0; *max_value = 65535; *is_unsigned = true; return true;
    case TYPE_CHAR:   *min_value = 0; *max_value = 65535; *is_unsigned = true; return true;
    case TYPE_INT:    *min_value = INT32_MIN; *max_value = INT32_MAX; return true;
    case TYPE_UINT:   *min_value = 0; *max_value = UINT32_MAX; *is_unsigned = true; return true;
    case TYPE_LONG:
    case TYPE_NINT:   *min_value = INT64_MIN; *max_value = INT64_MAX; return true;
    case TYPE_ULONG:  *min_value = 0; *max_value = UINT64_MAX; *is_unsigned = true; return true;
    case TYPE_ENUM:   *min_value = INT32_MIN; *max_value = INT32_MAX; return true;
    default: return false;
    }
}

static bool const_integral_value(zan_ast_node_t *expr, int64_t *value) {
    if (!expr) return false;
    if (expr->kind == AST_INT_LITERAL) {
        *value = expr->int_val;
        return true;
    }
    if (expr->kind == AST_CHAR_LITERAL) {
        *value = expr->int_val;
        return true;
    }
    if (expr->kind == AST_UNARY && expr->unary.op == TK_MINUS &&
        expr->unary.operand && expr->unary.operand->kind == AST_INT_LITERAL) {
        *value = (int64_t)(0ULL - (uint64_t)expr->unary.operand->int_val);
        return true;
    }
    return false;
}

static bool mixed_sign_compare_error(zan_binder_t *b, zan_diag_t *diag,
                                     zan_ast_node_t *expr,
                                     zan_type_t *left, zan_type_t *right) {
    bool lu = left->kind == TYPE_ULONG, ru = right->kind == TYPE_ULONG;
    if (!lu && !ru) return false;
    zan_type_t *s = lu ? right : left;
    if (s->kind != TYPE_LONG && s->kind != TYPE_INT &&
        s->kind != TYPE_SHORT && s->kind != TYPE_SBYTE &&
        s->kind != TYPE_NINT)
        return false;
    zan_ast_node_t *se = lu ? expr->binary.right : expr->binary.left;
    int64_t constant = 0;
    if (const_integral_value(se, &constant) && constant >= 0) return false;
    zan_diag_emit_code(diag, DIAG_ERROR, expr->loc, 2020,
                  "operator cannot be applied to operands of type '%s' and "
                  "'%s': mixed signed/unsigned comparison needs an explicit "
                  "cast (C# rules)",
                  type_name(left), type_name(right));
    (void)b;
    return true;
}

static bool integral_conversion_is_safe(zan_type_t *target, zan_type_t *value,
                                        zan_ast_node_t *expr) {
    int64_t tmin, vmin, constant;
    uint64_t tmax, vmax;
    bool tu, vu;
    if (!integral_range(target, &tmin, &tmax, &tu) ||
        !integral_range(value, &vmin, &vmax, &vu))
        return true;
    if ((!vu || tu) && vmin >= tmin && vmax <= tmax) return true;
    if (!const_integral_value(expr, &constant)) return false;
    if (vu && !tu) {
        if (expr && expr->kind == AST_UNARY && expr->unary.op == TK_MINUS &&
            expr->unary.operand && expr->unary.operand->kind == AST_INT_LITERAL &&
            (uint64_t)expr->unary.operand->int_val == 0x8000000000000000ULL)
            return constant >= tmin;
        if (constant < 0) return false;
        if (expr && expr->kind == AST_INT_LITERAL)
            return (uint64_t)expr->int_val <= (uint64_t)tmax;
        return (uint64_t)constant <= (uint64_t)tmax;
    }
    if (constant < tmin) return false;
    if (tu) {
        if (constant < 0) return false;
        return (uint64_t)constant <= tmax;
    }
    return constant <= (int64_t)tmax;
}

/* 发射the diagnostic when `value` is not assignable to `target` */
static bool checker_conversion_method(zan_checker_t *c, zan_symbol_t *sym,
                                      zan_type_t *from, zan_type_t *to,
                                      const char *op_name) {
    if (!sym) return false;
    zan_istr_t op = { (char *)op_name, (int)strlen(op_name) };
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (!m || m->kind != SYM_METHOD || !(m->modifiers & MOD_STATIC)) continue;
        if (!m->decl || m->decl->kind != AST_METHOD_DECL) continue;
        if (m->name.len != op.len ||
            memcmp(m->name.str, op.str, (size_t)op.len) != 0) continue;
        zan_type_t *rt = zan_binder_resolve_type(c->binder,
            m->decl->method_decl.return_type);
        if (!rt || !checker_type_equal(rt, to)) continue;
        zan_ast_list_t *ps = &m->decl->method_decl.params;
        if (ps->count != 1 || !ps->items[0] || ps->items[0]->kind != AST_PARAM)
            continue;
        zan_type_t *pt = zan_binder_resolve_type(c->binder,
            ps->items[0]->param.type);
        if (pt && checker_type_equal(pt, from)) return true;
    }
    return false;
}

static bool checker_user_conversion(zan_checker_t *c, zan_type_t *target,
                                    zan_type_t *value, const char *op_name) {
    if (!value || !target) return false;
    if (value->kind == TYPE_CLASS || value->kind == TYPE_STRUCT) {
        if (checker_conversion_method(c, value->sym, value, target, op_name))
            return true;
    }
    if (target->kind == TYPE_CLASS || target->kind == TYPE_STRUCT) {
        if (checker_conversion_method(c, target->sym, value, target, op_name))
            return true;
    }
    return false;
}

static bool checker_has_user_conversion(zan_checker_t *c, zan_type_t *target,
                                        zan_type_t *value) {
    return checker_user_conversion(c, target, value, "op_implicit");
}

static bool cast_is_scalar(zan_type_t *t) {
    return t && (type_is_numeric(t) || t->kind == TYPE_CHAR ||
                 t->kind == TYPE_ENUM || t->kind == TYPE_NINT);
}

/* 检查是否`(dst)src` is a conversion the language has */
static bool checker_struct_boxable(zan_type_t *t, int depth);

/* A454 装箱资格：用户声明的 struct 即可装箱（A556 第 3 件放开托管
 * 字段）：载荷原样写入堆对象，装箱时按捕获协议对托管字段 retain、
 * 箱的释放函数按字段 walk 释放（build_class_release_body 对
 * SYM_STRUCT 的偏移从 user_ptr+0 起，与载荷布局一致）。
 * Span/Decimal/Nullable 这类内建值形状同样满足（视图/标量载荷）。
 * 唯独 TYPE_TYPE_PARAM 字段仍拒：开泛形参载荷尺寸/释放按实例走，
 * 资格判定在开型上做不可靠。 */
static bool checker_struct_boxable(zan_type_t *t, int depth) {
    if (!t || t->kind != TYPE_STRUCT || !t->sym || depth > 32) return false;
    zan_symbol_t *sym = t->sym;
    if (!sym->decl || sym->decl->kind != AST_STRUCT_DECL) return false;
    if (sym->decl->type_decl.is_explicit_layout) return false;
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) continue;
        if (m->modifiers & (MOD_STATIC | MOD_WEAK)) continue;
        zan_type_t *ft = m->type;
        if (!ft) return false;
        if (ft->kind == TYPE_STRUCT) {
            if (!checker_struct_boxable(ft, depth + 1)) return false;
            continue;
        }
        if (ft->kind == TYPE_TYPE_PARAM) return false;
    }
    return true;
}

static bool checker_cast_is_valid(zan_checker_t *c, zan_type_t *dst,
                                  zan_type_t *src) {
    if (!dst || !src) return true;
    if (dst == c->binder->type_error || src == c->binder->type_error ||
        src == c->binder->type_void)
        return true;
    if (dst->kind == TYPE_TYPE_PARAM || src->kind == TYPE_TYPE_PARAM) return true;
    if (dst->kind == TYPE_OBJECT) {
        /* 值类型没有装箱设施时，`(object)42` 只会把整数值当成对象指针
         * （irgen 走 IntToPtr），调用方拿到野指针，进程退出时 ARC 去回收
         * 这个假对象再崩一次（0xC0000005）。编译期就拦下来。
         * 引用类型之间的转换照旧放行；无托管字段的 struct 走真装箱
         * （A454），其余值形状仍拦。 */
        zan_type_t *sbase = src->kind == TYPE_NULLABLE
                                ? checker_nullable_base(src) : src;
        if (sbase && sbase->kind == TYPE_OBJECT) return true;
        if (sbase && sbase->kind == TYPE_STRUCT &&
            checker_struct_boxable(sbase, 0))
            return true;
        return checker_type_is_ref(sbase);
    }
    if (src->kind == TYPE_OBJECT) return true;
    if (dst->kind == TYPE_NINT || src->kind == TYPE_NINT) return true;
    if (dst->kind == TYPE_DELEGATE || src->kind == TYPE_DELEGATE) return true;
    if (checker_type_equal(dst, src)) return true;
    if (dst->kind == TYPE_NULLABLE || src->kind == TYPE_NULLABLE)
        return checker_cast_is_valid(c, checker_nullable_base(dst),
                                     checker_nullable_base(src));
    if (cast_is_scalar(dst) && cast_is_scalar(src)) return true;
    if (checker_type_is_ref(dst) && checker_type_is_ref(src)) return true;
    if (checker_user_conversion(c, dst, src, "op_explicit")) return true;
    if (checker_user_conversion(c, dst, src, "op_implicit")) return true;
    return checker_type_assignable(dst, src);
}

static zan_symbol_t *expr_method_group(zan_checker_t *c, zan_ast_node_t *e);
static void reject_method_group(zan_checker_t *c, zan_ast_node_t *e);

/* B-ID65 只读表达式树：树类型识别；lambda 的树降级在 irgen 发射口
 * （irgen_expr_lambda.c emit_lambda_typed），实现体在文件尾。 */
static bool checker_type_is_expr_tree_target(zan_type_t *t);

/* 方法组 → 委托的签名兼容性（C# 方法组转换规则的收窄版）：
 * 参数个数一致、每个委托形参可赋给方法形参（逆变方向）、
 * 返回值 void 对 void，否则方法返回类型可赋给委托返回类型（协变）。
 * 不兼容绝不能静默放行——发码侧裸函数指针按错误签名调用时，
 * 形参读到的就是寄存器残渣（B-ID48 的运行期腐坏）。 */
static bool method_matches_delegate_sig(zan_checker_t *c, zan_symbol_t *m,
                                        zan_type_t *dlg) {
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL ||
        !dlg || dlg->kind != TYPE_DELEGATE)
        return false;
    if (m->decl->method_decl.type_params.count > 0)
        return false; /* 泛型方法组：发码侧无实例化路径，直接判不兼容 */
    zan_ast_list_t *ps = &m->decl->method_decl.params;
    int dp = dlg->delegate_param_count;
    if (!ps || ps->count != dp) return false;
    if (dp > 0 && !dlg->delegate_param_types) return false;
    for (int i = 0; i < dp; i++) {
        zan_type_t *dpt = dlg->delegate_param_types[i];
        zan_ast_node_t *pn = ps->items[i];
        zan_type_t *mpt = (pn && pn->kind == AST_PARAM && pn->param.type)
            ? zan_binder_resolve_type(c->binder, pn->param.type) : NULL;
        if (!dpt || !mpt) return false;
        if (dpt->kind == TYPE_TYPE_PARAM || mpt->kind == TYPE_TYPE_PARAM)
            return false;
        if (!checker_type_equal(dpt, mpt) &&
            !checker_type_assignable(mpt, dpt))
            return false;
    }
    zan_type_t *dret = dlg->delegate_ret_type;
    zan_ast_node_t *rt = m->decl->method_decl.return_type;
    zan_type_t *mret = rt ? zan_binder_resolve_type(c->binder, rt)
                          : c->binder->type_void;
    if (!dret || dret->kind == TYPE_VOID || dret->kind == TYPE_ERROR)
        return !mret || mret->kind == TYPE_VOID;
    if (!mret || mret->kind == TYPE_VOID) return false;
    return checker_type_equal(dret, mret) ||
           checker_type_assignable(dret, mret);
}

static bool method_group_matches_delegate(zan_checker_t *c, zan_symbol_t *m,
                                         zan_type_t *dlg) {
    if (!m || !dlg) return false;
    if (method_matches_delegate_sig(c, m, dlg)) return true;
    for (zan_symbol_t *ts = m->parent; ts;
         ts = (ts->type && ts->type->base_type) ? ts->type->base_type->sym : NULL) {
        for (int i = 0; i < ts->member_count; i++) {
            zan_symbol_t *cand = ts->members[i];
            if (!cand || cand == m || cand->kind != SYM_METHOD) continue;
            if (cand->name.len == m->name.len &&
                memcmp(cand->name.str, m->name.str, (size_t)m->name.len) == 0) {
                if (method_matches_delegate_sig(c, cand, dlg))
                    return true;
            }
        }
    }
    return false;
}

static void checker_check_assignable(zan_checker_t *c, zan_type_t *target,
                                     zan_type_t *value, zan_ast_node_t *expr,
                                     zan_loc_t loc, const char *what) {
    if (!target || !value) return;
    if (expr) {
        zan_symbol_t *mg = expr_method_group(c, expr);
        if (mg) {
            if (target->kind != TYPE_DELEGATE) {
                reject_method_group(c, expr);
                return;
            }
            /* 方法组 → 委托：签名兼容才放行，不兼容当场报——
             * 静默放行会走到发码侧按错误签名调用（B-ID48）。 */
            if (!method_group_matches_delegate(c, mg, target)) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, loc, 2098,
                    "method '%.*s' cannot be converted to delegate type "
                    "'%s': signature is not compatible",
                    (int)mg->name.len, mg->name.str, type_name(target));
                return;
            }
            return; /* 兼容：方法组转换成立，跳过 void/常规可赋值检查 */
        }
    }
    if (target == c->binder->type_error || value == c->binder->type_error)
        return;
    if (value == c->binder->type_void && target != c->binder->type_void) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, loc, 2021,
                      "cannot convert 'void' to '%s' in %s: no implicit "
                      "conversion",
                      type_name(target), what);
        return;
    }
    bool target_integral = type_is_integral(target) || target->kind == TYPE_CHAR ||
                           target->kind == TYPE_ENUM;
    bool value_integral = type_is_integral(value) || value->kind == TYPE_CHAR ||
                          value->kind == TYPE_ENUM;
    if (target_integral &&
        (value->kind == TYPE_FLOAT || value->kind == TYPE_DOUBLE)) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, loc, 2022,
                      "narrowing conversion from '%s' to '%s' in %s needs an "
                      "explicit cast (C# rules)",
                      type_name(value), type_name(target), what);
        return;
    }
    if (target_integral && value_integral) {
        int64_t constant = 0;
        bool has_constant = const_integral_value(expr, &constant);
        bool native_handle_narrowing = value->kind == TYPE_NINT &&
            target->kind != TYPE_LONG && target->kind != TYPE_NINT;
        bool decimal_radix = expr && expr->kind == AST_INT_LITERAL &&
            expr->lit_radix == 10;
        bool uint32_bit_pattern = value->kind == TYPE_LONG &&
            target->kind == TYPE_INT && has_constant && constant >= 0 &&
            (uint64_t)constant <= UINT32_MAX && !decimal_radix;
        bool ulong_literal = target->kind == TYPE_ULONG && expr &&
            expr->kind == AST_INT_LITERAL;
        if (native_handle_narrowing ||
            (!uint32_bit_pattern && !ulong_literal && has_constant &&
             !integral_conversion_is_safe(target, value, expr))) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, loc, 2022,
                          "narrowing conversion from '%s' to '%s' in %s needs an "
                          "explicit cast (C# rules)",
                          type_name(value), type_name(target), what);
            return;
        }
    }
    if (checker_type_assignable(target, value)) return;
    if (checker_has_user_conversion(c, target, value)) return;
    if (target->kind == TYPE_TASK && value->kind == TYPE_TASK &&
        target->type_arg_count == 1 && value->type_arg_count == 0) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, loc, 2023,
                      "cannot convert '%s' to '%s' in %s: a task started from a "
                      "lambda/Action carries no result; declare 'Task' (not "
                      "'Task<T>') and await it or join with Task.WhenAll/JoinWait",
                      type_name(value), type_name(target), what);
        return;
    }
    zan_diag_emit_code(c->diag, DIAG_ERROR, loc, 2024,
                  "cannot convert '%s' to '%s' in %s: no implicit conversion",
                  type_name(value), type_name(target), what);
}

static zan_type_t *check_member_access(zan_checker_t *c, zan_ast_node_t *expr,
                                       zan_type_t *obj_type, bool in_call_callee,
                                       zan_ast_node_t *call);
static void checker_reject_null_receiver(zan_checker_t *c, zan_ast_node_t *expr);

static bool method_arity(zan_symbol_t *m, int *min, int *max) {
    if (!m->decl || m->decl->kind != AST_METHOD_DECL) return false;
    /* a [DllImport( */
    if (m->decl->method_decl.is_variadic) return false;
    zan_ast_list_t *ps = &m->decl->method_decl.params;
    int lo = 0;
    for (int i = 0; i < ps->count; i++) {
        zan_ast_node_t *p = ps->items[i];
        if (!p || p->kind != AST_PARAM) return false;
        if (p->param.is_params || p->param.is_this) return false;
        if (!p->param.default_val) lo++;
    }
    *min = lo;
    *max = ps->count;
    return true;
}

