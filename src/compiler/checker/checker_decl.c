
static bool expr_is_null_literal(zan_ast_node_t *e) {
    if (!e) return false;
    if (e->kind == AST_NULL_LITERAL) return true;
    if (e->kind == AST_CONDITIONAL)
        return expr_is_null_literal(e->conditional.then_expr) ||
               expr_is_null_literal(e->conditional.else_expr);
    return false;
}

static bool stmt_returns_null(zan_ast_node_t *n, int depth) {
    if (!n || depth > CHECKER_DERIVES_MAX_DEPTH) return false;
    switch (n->kind) {
    case AST_RETURN_STMT:
        return expr_is_null_literal(n->ret.value);
    case AST_BLOCK:
        for (int i = 0; i < n->block.stmts.count; i++)
            if (stmt_returns_null(n->block.stmts.items[i], depth + 1)) return true;
        return false;
    case AST_IF_STMT:
        return stmt_returns_null(n->if_stmt.then_body, depth + 1) ||
               stmt_returns_null(n->if_stmt.else_body, depth + 1);
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        return stmt_returns_null(n->while_stmt.body, depth + 1);
    case AST_FOR_STMT:
        return stmt_returns_null(n->for_stmt.body, depth + 1);
    case AST_FOREACH_STMT:
        return stmt_returns_null(n->foreach_stmt.body, depth + 1);
    case AST_LOCK_STMT:
        return stmt_returns_null(n->lock_stmt.body, depth + 1);
    case AST_TRY_STMT: {
        if (stmt_returns_null(n->try_stmt.try_body, depth + 1)) return true;
        for (int i = 0; i < n->try_stmt.catches.count; i++) {
            zan_ast_node_t *cc = n->try_stmt.catches.items[i];
            if (cc && stmt_returns_null(cc->catch_clause.body, depth + 1)) return true;
        }
        return stmt_returns_null(n->try_stmt.finally_body, depth + 1);
    }
    case AST_SWITCH_STMT:
        for (int i = 0; i < n->switch_stmt.cases.count; i++) {
            zan_ast_node_t *cs = n->switch_stmt.cases.items[i];
            if (cs && stmt_returns_null(cs->switch_case.body, depth + 1)) return true;
        }
        return false;
    default:
        return false;
    }
}

static bool method_can_return_null(zan_symbol_t *m) {
    if (!m || m->kind != SYM_METHOD || !m->decl) return false;
    if (m->decl->kind != AST_METHOD_DECL) return false;
    if (!m->decl->method_decl.return_type) return false;
    return stmt_returns_null(m->decl->method_decl.body, 0);
}

static bool loc_at_or_after(zan_loc_t a, zan_loc_t b) {
    if (a.file_id != b.file_id)
        return a.line > b.line || (a.line == b.line && a.col >= b.col);
    return a.offset >= b.offset;
}

static bool node_loc_prunable(zan_ast_kind_t k) {
    switch (k) {
    case AST_BLOCK: case AST_EXPR_STMT: case AST_RETURN_STMT:
    case AST_THROW_STMT: case AST_VAR_DECL: case AST_IF_STMT:
    case AST_WHILE_STMT: case AST_DO_WHILE_STMT: case AST_FOR_STMT:
    case AST_FOREACH_STMT: case AST_LOCK_STMT:
    case AST_TRY_STMT: case AST_SWITCH_STMT:
        return true;
    default:
        return false;
    }
}

static bool node_guards_null(zan_ast_node_t *n, zan_istr_t name, int depth,
                             zan_loc_t use);

static bool list_guards_null(zan_ast_list_t *l, zan_istr_t name, int depth,
                             zan_loc_t use) {
    for (int i = 0; i < l->count; i++)
        if (node_guards_null(l->items[i], name, depth + 1, use)) return true;
    return false;
}

static bool is_named_ident(zan_ast_node_t *n, zan_istr_t name) {
    return n && n->kind == AST_IDENTIFIER &&
           n->ident.name.len == name.len &&
           memcmp(n->ident.name.str, name.str, (size_t)name.len) == 0;
}

static bool node_guards_null(zan_ast_node_t *n, zan_istr_t name, int depth,
                             zan_loc_t use) {
    if (!n || depth > CHECKER_DERIVES_MAX_DEPTH) return false;
    if (node_loc_prunable(n->kind) && loc_at_or_after(n->loc, use)) {
        return false;
    }
    switch (n->kind) {
    case AST_BINARY:
        if ((is_named_ident(n->binary.left, name) &&
             (n->binary.right && n->binary.right->kind == AST_NULL_LITERAL)) ||
            (is_named_ident(n->binary.right, name) &&
             (n->binary.left && n->binary.left->kind == AST_NULL_LITERAL)) ||
            (n->binary.op == TK_QUESTION_QUESTION &&
             is_named_ident(n->binary.left, name)))
            return true;
        return node_guards_null(n->binary.left, name, depth + 1, use) ||
               node_guards_null(n->binary.right, name, depth + 1, use);
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        if (is_named_ident(n->type_test.expr, name)) return true;
        return node_guards_null(n->type_test.expr, name, depth + 1, use);
    case AST_MEMBER_ACCESS:
        if (n->member.null_cond && is_named_ident(n->member.object, name))
            return true;
        return node_guards_null(n->member.object, name, depth + 1, use);
    case AST_NEW_EXPR:
    case AST_STRING_LITERAL:
        return true;
    case AST_POSTFIX_UNARY:
        if (n->unary.op == TK_BANG && is_named_ident(n->unary.operand, name))
            return true;
        return node_guards_null(n->unary.operand, name, depth + 1, use);
    case AST_UNARY:
        return node_guards_null(n->unary.operand, name, depth + 1, use);
    case AST_CAST_EXPR:
        return node_guards_null(n->cast.expr, name, depth + 1, use);
    case AST_CALL:
        return node_guards_null(n->call.callee, name, depth + 1, use) ||
               list_guards_null(&n->call.args, name, depth + 1, use);
    case AST_INDEX:
        return node_guards_null(n->index.object, name, depth + 1, use) ||
               node_guards_null(n->index.index, name, depth + 1, use);
    case AST_CONDITIONAL:
        return node_guards_null(n->conditional.cond, name, depth + 1, use) ||
               node_guards_null(n->conditional.then_expr, name, depth + 1, use) ||
               node_guards_null(n->conditional.else_expr, name, depth + 1, use);
    case AST_ASSIGNMENT:
        if (is_named_ident(n->binary.left, name)) {
            if (n->binary.right &&
                (n->binary.right->kind == AST_NEW_EXPR ||
                 n->binary.right->kind == AST_STRING_LITERAL ||
                 (n->binary.right->kind == AST_POSTFIX_UNARY &&
                  n->binary.right->unary.op == TK_BANG)))
                return true;
        }
        return node_guards_null(n->binary.left, name, depth + 1, use) ||
               node_guards_null(n->binary.right, name, depth + 1, use);
    case AST_BLOCK:
        return list_guards_null(&n->block.stmts, name, depth + 1, use);
    case AST_EXPR_STMT:
        return node_guards_null(n->expr_stmt.expr, name, depth + 1, use);
    case AST_RETURN_STMT:
        return node_guards_null(n->ret.value, name, depth + 1, use);
    case AST_THROW_STMT:
        return node_guards_null(n->throw_stmt.value, name, depth + 1, use);
    case AST_VAR_DECL:
        if (n->var_decl.name.len == name.len &&
            memcmp(n->var_decl.name.str, name.str, (size_t)name.len) == 0) {
            if (n->var_decl.initializer &&
                (n->var_decl.initializer->kind == AST_NEW_EXPR ||
                 n->var_decl.initializer->kind == AST_STRING_LITERAL ||
                 (n->var_decl.initializer->kind == AST_POSTFIX_UNARY &&
                  n->var_decl.initializer->unary.op == TK_BANG)))
                return true;
        }
        return node_guards_null(n->var_decl.initializer, name, depth + 1, use);
    case AST_IF_STMT:
        return node_guards_null(n->if_stmt.cond, name, depth + 1, use) ||
               node_guards_null(n->if_stmt.then_body, name, depth + 1, use) ||
               node_guards_null(n->if_stmt.else_body, name, depth + 1, use);
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        return node_guards_null(n->while_stmt.cond, name, depth + 1, use) ||
               node_guards_null(n->while_stmt.body, name, depth + 1, use);
    case AST_FOR_STMT:
        return node_guards_null(n->for_stmt.init, name, depth + 1, use) ||
               node_guards_null(n->for_stmt.cond, name, depth + 1, use) ||
               node_guards_null(n->for_stmt.step, name, depth + 1, use) ||
               node_guards_null(n->for_stmt.body, name, depth + 1, use);
    case AST_FOREACH_STMT:
        return node_guards_null(n->foreach_stmt.collection, name, depth + 1, use) ||
               node_guards_null(n->foreach_stmt.body, name, depth + 1, use);
    case AST_LOCK_STMT:
        return node_guards_null(n->lock_stmt.expr, name, depth + 1, use) ||
               node_guards_null(n->lock_stmt.body, name, depth + 1, use);
    case AST_TRY_STMT: {
        if (node_guards_null(n->try_stmt.try_body, name, depth + 1, use)) return true;
        for (int i = 0; i < n->try_stmt.catches.count; i++) {
            zan_ast_node_t *cc = n->try_stmt.catches.items[i];
            if (cc && node_guards_null(cc->catch_clause.body, name, depth + 1, use))
                return true;
        }
        return node_guards_null(n->try_stmt.finally_body, name, depth + 1, use);
    }
    case AST_SWITCH_STMT: {
        if (node_guards_null(n->switch_stmt.expr, name, depth + 1, use)) return true;
        for (int i = 0; i < n->switch_stmt.cases.count; i++) {
            zan_ast_node_t *cs = n->switch_stmt.cases.items[i];
            if (cs && node_guards_null(cs->switch_case.body, name, depth + 1, use))
                return true;
        }
        return false;
    }
    default:
        return false;
    }
}

static void checker_reject_null_receiver(zan_checker_t *c, zan_ast_node_t *expr) {
    if (!expr || expr->kind != AST_MEMBER_ACCESS) return;
    if (expr->member.null_cond) return;
    zan_ast_node_t *obj = expr->member.object;
    if (!obj) return;

    /* 1 */
    zan_ast_node_t *core_obj = obj;
    while (core_obj && core_obj->kind == AST_CAST_EXPR) core_obj = core_obj->cast.expr;
    if (core_obj && core_obj->kind == AST_NULL_LITERAL) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2082,
                      "cannot access member '%.*s' on 'null'; null reference dereference faults at runtime",
                      (int)expr->member.name.len, expr->member.name.str);
        return;
    }

    zan_symbol_t *m = NULL;
    if (obj->kind == AST_CALL) {
        if (obj != c->last_call_node) return;
        m = c->last_call_method;
    } else if (obj->kind == AST_IDENTIFIER) {
        struct checker_local *l = checker_find_local_slot(c, obj->ident.name);
        if (l && l->type && l->type->is_nullable) {
            if (c->current_body && !node_guards_null(c->current_body, obj->ident.name, 0, expr->loc)) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2109,
                              "possible null reference dereference: '%.*s' is a nullable type '%s'; "
                              "access via '?.' or assert via '!' or check for null first",
                              (int)obj->ident.name.len, obj->ident.name.str,
                              type_name(l->type));
                return;
            }
        }
        if (!l || !l->null_src) return;
        if (!c->current_body) return;
        if (node_guards_null(c->current_body, obj->ident.name, 0, expr->loc))
            return;
        m = l->null_src;
    } else {
        return;
    }
    if (!method_can_return_null(m)) return;
    zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2083,
                  "'%.*s' can return null; accessing '%.*s' on its result "
                  "faults at runtime -- use '?.', or store it and check for "
                  "null first",
                  (int)m->name.len, m->name.str,
                  (int)expr->member.name.len, expr->member.name.str);
}

static bool receiver_is_value_expr(zan_checker_t *c, zan_ast_node_t *obj) {
    switch (obj->kind) {
    case AST_IDENTIFIER: {
        if (checker_find_local_slot(c, obj->ident.name)) return true;
        zan_symbol_t *s = zan_binder_lookup(c->binder, obj->ident.name);
        if (!s) return false;
        return s->kind == SYM_FIELD || s->kind == SYM_PROPERTY ||
               s->kind == SYM_PARAM || s->kind == SYM_LOCAL;
    }
    case AST_CALL:
    case AST_INDEX:
        return true;
    default:
        return false;
    }
}

/* ---------- 面向用户的类型名（A476） ---------- */

/* 类型的原始简单名。
 *
 * nsresolve 在同名冲突时会把声明名改写成 mangle 后的内部名
 * （"Gui.Widget.QrEncoder" → "Gui_Widget_QrEncoder"），原始简单名留在
 * decl 的 meta 里。诊断要给用户看原始名——内部符号名对用户没有意义，
 * 还会让人以为自己的类被改名了。 */
static zan_istr_t checker_type_simple_name(zan_type_t *t) {
    zan_istr_t empty = {NULL, 0};
    if (!t) return empty;
    if (t->sym) {
        if (t->sym->decl) {
            zan_istr_t on = zan_ast_orig_name(t->sym->decl);
            if (on.len) return on;
        }
        if (t->sym->name.len) return t->sym->name;
    }
    return t->name;
}

/* 报"这个类型上没有这个成员/方法"。
 *
 * 措辞分两档：取值位置说"没有这个成员"，调用位置说"没有这个方法"——
 * 混成一句话，"方法名写错了"和"字段名写错了"就分不开了。 */
static void checker_report_no_member(zan_checker_t *c, zan_ast_node_t *expr,
                                     zan_type_t *recv, zan_istr_t name,
                                     bool is_method) {
    const char *what = is_method ? "has no method" : "has no member";
    zan_istr_t ns = {NULL, 0};
    zan_istr_t simple = checker_type_simple_name(recv);
    if (recv && recv->sym) ns = access_symbol_ns(recv->sym);
    if (ns.len) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2084,
                      "type '%.*s.%.*s' %s '%.*s'",
                      (int)ns.len, ns.str,
                      (int)simple.len, simple.str,
                      what,
                      (int)name.len, name.str);
    } else {
        zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2085,
                      "'%.*s' %s '%.*s'",
                      (int)simple.len, simple.str,
                      what,
                      (int)name.len, name.str);
    }
}

/* 符号链（类 + 基类）里有没有这个名字的成员 */
static bool checker_sym_chain_has_member(zan_symbol_t *sym, zan_istr_t name) {
    for (zan_symbol_t *s = sym; s;
         s = (s->type && s->type->base_type) ? s->type->base_type->sym : NULL) {
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            if (m && m->name.len == name.len &&
                memcmp(m->name.str, name.str, (size_t)name.len) == 0)
                return true;
        }
    }
    return false;
}

/* 类型上有没有这个名字的成员：类链 + 实现的接口（接口继承链也算）。
 *
 * 接口必须一起看：抽象接口方法没有 body，不会被拷进实现类的成员表，
 * 只看类链会把"实现了接口方法"的调用判成"没有这个方法"。
 * 泛型实例类型不拷贝 interfaces 字段，所以接口从符号的原型类型上取。 */
static bool checker_type_has_member(zan_type_t *t, zan_istr_t name, int depth) {
    if (!t || !t->sym || depth > 16) return false;
    if (checker_sym_chain_has_member(t->sym, name)) return true;
    zan_type_t *proto = t->sym->type ? t->sym->type : t;
    for (int i = 0; i < proto->interface_count; i++) {
        zan_type_t *it = proto->interfaces[i];
        if (!it) continue;
        if (it->sym && checker_sym_chain_has_member(it->sym, name)) return true;
        if (checker_type_has_member(it, name, depth + 1)) return true;
    }
    return false;
}

/* 编译期内建类（Convert/Math/Console/File/…）的成员一半在 stdlib 里声明、
 * 一半由编译器原生实现（Convert.ToString、File.ReadAllText 这类）。按名字
 * 枚举成员必然漏掉原生那半边，所以这类接收者一律判不了，不报"没有这个方法"。
 * 名单直接取 builtin_api 的内建类型表，不另维护一份。 */
static bool checker_is_builtin_class_name(zan_istr_t n) {
    int count = 0;
    const zan_builtin_type_t *tbl = zan_builtin_types(&count);
    for (int i = 0; i < count; i++) {
        size_t l = strlen(tbl[i].type);
        if ((size_t)n.len == l && memcmp(n.str, tbl[i].type, l) == 0)
            return true;
    }
    return false;
}

/* 内建接收者的成员集。返回 1 = 有，0 = 没有，-1 = 判不了（不报）。 */
static int checker_builtin_member_state(zan_type_t *t, zan_istr_t name) {
    if (!t || !name.str || name.len == 0) return -1;
    const char *bt = NULL;
    switch (t->kind) {
    case TYPE_STRING: bt = "string"; break;
    case TYPE_BOOL: bt = "bool"; break;
    case TYPE_BYTE: bt = "byte"; break;
    case TYPE_SBYTE: bt = "sbyte"; break;
    case TYPE_SHORT: bt = "short"; break;
    case TYPE_USHORT: bt = "ushort"; break;
    case TYPE_INT: bt = "int"; break;
    case TYPE_UINT: bt = "uint"; break;
    case TYPE_LONG: bt = "long"; break;
    case TYPE_ULONG: bt = "ulong"; break;
    case TYPE_FLOAT: bt = "float"; break;
    case TYPE_DOUBLE: bt = "double"; break;
    case TYPE_DECIMAL: bt = "decimal"; break;
    case TYPE_CHAR: bt = "char"; break;
    case TYPE_NINT: bt = "nint"; break;
    /* object 的成员集没定死（Equals/GetHashCode 之类），判不了就不报。 */
    default: return -1;
    }
    if (!zan_builtin_find(bt)) return -1;   /* decimal 没进表：判不了 */
    /* 编译器原生成员（int.Parse / string.AsSpan 这类）不在成员表里，
     * 但确实存在——成员表查不到 ≠ 方法不存在。 */
    if (zan_builtin_has_native_member(bt, (int)strlen(bt), name.str, name.len))
        return 1;
    return zan_builtin_has_member(bt, name.str, (int)name.len) ? 1 : 0;
}

/* 类型里提到了类型形参（泛型模板里的 T）。 */
static bool checker_type_mentions_type_param(zan_type_t *t, int depth) {
    if (!t || depth > 8) return false;
    if (t->kind == TYPE_TYPE_PARAM || t->kind == TYPE_ERROR) return true;
    if (t->element_type &&
        checker_type_mentions_type_param(t->element_type, depth + 1))
        return true;
    for (int i = 0; i < t->type_arg_count; i++)
        if (checker_type_mentions_type_param(t->type_args[i], depth + 1))
            return true;
    return false;
}

/* 这个调用是不是要到运行期才落到真正的成员上。
 *
 * 判据是**两条同时成立**：
 *   ① 所在类/结构体是开放泛型模板（带类型形参）；
 *   ② 这个调用的类型实参里带类型形参。
 *
 * 为什么是"擦除模板"：开放泛型模板（类/结构体带类型形参）的方法体是
 * 模板，真身由泛型基类特化（zan_parser_specialize_generic_bases）在派生
 * 类里补出，模板体本身只留一个运行期转发桩。模板体里"真解析不到"的调用
 * （典型：T 接收者的调用，成员集要等 T 落成具体类型才存在），irgen 不报
 * 3068，而是发运行期转发桩 + A494 软诊断（执行到才报，返回默认值继续跑；
 * 见 irgen_call.c 的 call_receiver_is_open_generic，那边口径更宽：模板体
 * 内、或接收者/实参带类型形参都算）。checker 这边必须把该放行的放行，
 * 否则合法的泛型基类模板直接编不过；conformance 的
 * generic_template_forward_stub 钉这个口径。
 *
 * 注意 ORM 已经不是这里的例子：Insert<T>/Select<T>/Update<T>/Delete<T>
 * 现在是 System.Data.Orm（DbExtensions.zan）的真扩展方法，接收者是已知
 * 类型时静态绑定就解析——模板体里的 db.Select<T>() 也一样，不经过本函数
 * （历史上这四个方法由编译期生成器 GenDb 改写调用点而来，生成器已随 ORM
 * 重构移除）。反过来，接收者类型已知且成员谁都不声明时，irgen 的成员
 * 访问健壮性路径（irgen_expr_access.c）当场报 E3009——本函数的放行只是
 * 不抢在 irgen 的诊断前面按自己的口径误报。
 *
 * 为什么 ② 不可省：**只按 ① 放行，等于把整个泛型类的成员存在性检查
 * 关掉**——模板里调一个谁都没有的方法（repo.NoSuchMethod()，不带类型
 * 实参）成员集是定死的，该报就报（checker_report_no_member）；带类型
 * 实参的调用才是"真身要等类型定了才落得到成员上"的那一类。
 *
 * 为什么 ① 不可省：模板体之外（普通类里的泛型方法、非泛型类），
 * 成员集判不出来只是"这个成员压根不存在"——那是真错。缺了 ①，
 * `class P { void Use<T>(Helper h) { h.Missing<T>(); } }` 会被放过，
 * 错误一路滑到发码阶段只剩一句 "code generation failed"，比 checker
 * 直接点名差得多。
 *
 * 接收者是类型形参本身（T.Foo()）或没有符号的泛型容器（List<T>）时，
 * 成员集同样判不出来，但那是 checker_call_target_member_state 里
 * 返回 -1 的既有路径，不归这里管。 */
static bool checker_call_is_runtime_dispatched(zan_checker_t *c,
                                               zan_ast_node_t *call) {
    /* ① 所在类型必须是开放泛型模板 */
    if (!c->current_type_sym || !c->current_type_sym->decl) return false;
    if (c->current_type_sym->decl->kind != AST_CLASS_DECL &&
        c->current_type_sym->decl->kind != AST_STRUCT_DECL)
        return false;
    if (c->current_type_sym->decl->type_decl.type_params.count == 0)
        return false;
    /* ② 这个调用还带着类型形参 */
    if (!call || call->kind != AST_CALL) return false;
    if (call->call.type_args.count == 0) return false;
    for (int i = 0; i < call->call.type_args.count; i++) {
        zan_type_t *ta = zan_binder_resolve_type(
            c->binder, call->call.type_args.items[i]);
        /* 解析不出来（type_error）也算"带类型形参"：宁可放行，不可误报。 */
        if (checker_type_mentions_type_param(ta, 0)) return true;
    }
    return false;
}

/* 调用目标上有没有这个成员。
 *
 * 1 = 有；0 = 没有（可以报"没有这个方法"）；-1 = 判不了（保持原样，不报）。
 *
 * 口径必须和 irgen 一致：irgen 能发出去的调用，checker 不能判成不存在。
 * 所以扩展方法（接收者类型上查不到成员，但 recv.Foo() 合法）必须算进去；
 * 运行期才落地的调用（开放泛型模板）同样判不了成员集，也得放行。 */
static int checker_call_target_member_state(zan_checker_t *c, zan_type_t *t,
                                            zan_istr_t name,
                                            zan_ast_node_t *call) {
    if (!t || t == c->binder->type_error) return -1;
    if (!name.str || name.len == 0) return -1;
    if (zan_binder_has_extension_method(c->binder, t, name)) return 1;
    /* 运行期才落地的调用：成员集编译期根本判不出来 —— 判不了就不报。 */
    if (checker_call_is_runtime_dispatched(c, call)) return -1;
    switch (t->kind) {
    case TYPE_CLASS:
    case TYPE_STRUCT:
    case TYPE_INTERFACE:
        /* 内建泛型容器（List/Dict/StringBuilder/Span）没有 sym，
         * 成员集没定死 —— 判不了就不报，交给 irgen。 */
        if (!t->sym) return -1;
        {
            zan_istr_t simple = checker_type_simple_name(t);
            /* 内建类（Convert/Math/Console/…）成员一半是编译器原生实现的，
             * 枚举不全 —— 同样判不了。 */
            if (checker_is_builtin_class_name(simple)) return -1;
            /* 编译器原生成员（Gate.Park / Socket.RecvOv 这类）不在类的
             * 成员表里，但确实存在。 */
            if (simple.str && simple.len &&
                zan_builtin_has_native_member(simple.str, simple.len,
                                              name.str, name.len))
                return 1;
        }
        if (checker_type_has_member(t, name, 0)) return 1;
        if ((t->kind == TYPE_CLASS || t->kind == TYPE_INTERFACE) && call && call->kind == AST_CALL) {
            int argc = call->call.args.count;
            if (name.len == 8 && memcmp(name.str, "ToString", 8) == 0 && argc == 0) return 1;
            if (name.len == 6 && memcmp(name.str, "Equals", 6) == 0 && argc == 1) return 1;
            if (name.len == 11 && memcmp(name.str, "GetHashCode", 11) == 0 && argc == 0) return 1;
        }
        return 0;
    default:
        break;
    }
    if (type_is_scalar_primitive(t)) return checker_builtin_member_state(t, name);
    return -1;
}

/* ---------- 类型化数据访问口：recv.<Entity>（"表即属性"） ----------
 *
 * [Table] 实体把"取一个查询面"变成普通成员访问：
 *   await this.User.Where(a => a.published == 1).Take(20).ToListAsync()
 *   await db.User.Read(id)
 * 生成器时代（ee0cfd87b dbgen.c，后迁 ZanGen.zan）由外部生成器改写链
 * 条；生成器废除后（de208fe98）收编为编译器原生合成：checker 把成员
 * 节点原地降级为普通 new 表达式，irgen 与后续各道零感知。
 *
 * 命中条件全部满足才接管（宁可漏配、不可错配——普通成员永远优先，
 * 本函数只在常规成员查找落空后到达）：
 *   ① 成员名按类型解析得到带 [Table] 的类；
 *   ② 接收者能给出连接：本身是（或实现）IDbConnection，或其类自身或
 *      基类链上声明了无参 __Conn()（控制器基类声明一次，全部派生类
 *      的表访问器共用）；
 *   ③ <Entity>Dao 已声明且有"单个连接可赋值参数"的构造器 —— 用它
 *      （合成调用属编译器可信代码，与反射取值口同等待遇，不做访问
 *      检查）；否则回退 BaseRepository<Entity>，经
 *      ZanSql.FromConnection(conn) 构造——这是编译器与 Zan.Data 的
 *      唯一构造接线点，库侧重构该口时同步改这里。 */
static zan_istr_t checker_istr_lit(zan_arena_t *arena, const char *s) {
    zan_istr_t out;
    out.str = zan_arena_strdup(arena, s, strlen(s));
    out.len = (uint32_t)strlen(s);
    return out;
}

static zan_istr_t checker_istr_join(zan_arena_t *arena, zan_istr_t a,
                                    const char *b) {
    size_t blen = strlen(b);
    char buf[256];
    uint32_t n = 0;
    for (uint32_t i = 0; i < a.len && n < sizeof buf; i++) buf[n++] = a.str[i];
    for (size_t i = 0; i < blen && n < sizeof buf - 1; i++) buf[n++] = b[i];
    zan_istr_t out;
    out.str = zan_arena_strdup(arena, buf, n);
    out.len = n;
    return out;
}

/* 造一个 AST_TYPE_REF；gen 参数非空时造单参泛型（BaseRepository<E>）。 */
static zan_ast_node_t *checker_type_ref(zan_checker_t *c, zan_istr_t name,
                                        zan_ast_node_t *gen, zan_loc_t loc) {
    zan_ast_node_t *t = zan_ast_new(c->arena, AST_TYPE_REF, loc);
    t->type_ref.name = name;
    t->type_ref.is_nullable = false;
    t->type_ref.is_array = false;
    zan_ast_list_init(&t->type_ref.type_args);
    if (gen) zan_ast_list_push(&t->type_ref.type_args, gen, c->arena);
    return t;
}

static bool checker_is_table_entity(zan_checker_t *c, zan_istr_t name,
                                    zan_type_t **out) {
    if (name.len == 0 || name.len > 100) return false;
    if (name.str[0] < 'A' || name.str[0] > 'Z') return false;
    /* 探针必须无诊断：binder_lookup 查无此名静默返回 NULL，不能用
     * resolve_type——未定义名会当场打 E1005，把"不是访问器"的正常
     * 路径污染成编译错误。 */
    zan_symbol_t *sym = zan_binder_lookup(c->binder, name);
    if (!sym || sym->kind != SYM_CLASS || !sym->decl) return false;
    if (!zan_ast_has_attr(sym->decl, "Table")) return false;
    *out = sym->type;
    return true;
}

/* 连接表达式：连接接收者返回对象节点本身，类接收者返回 __Conn() 调用
 * 节点；同时经 *conn_out 带回连接表达式的静态类型。 */
static zan_ast_node_t *checker_accessor_conn_expr(zan_checker_t *c,
                                                  zan_ast_node_t *expr,
                                                  zan_type_t *obj_type,
                                                  zan_type_t **conn_out) {
    zan_type_t *idbc = NULL;
    zan_symbol_t *isym = zan_binder_lookup(
        c->binder, checker_istr_lit(c->arena, "IDbConnection"));
    if (isym && isym->kind == SYM_INTERFACE && isym->type) idbc = isym->type;
    if (idbc && obj_type->sym &&
        (obj_type->sym == idbc->sym ||
         ((obj_type->kind == TYPE_CLASS || obj_type->kind == TYPE_STRUCT) &&
          zan_binder_class_implements_iface(obj_type->sym, idbc->sym)))) {
        *conn_out = obj_type;
        return expr->member.object;
    }
    if (idbc && obj_type->kind == TYPE_CLASS && obj_type->sym) {
        /* 沿基类链逐层找：__Conn() 的常见落点是控制器基类（AppController），
         * 派生控制器（Dashboard : AdminController : AppController）上的
         * this.<Entity> 同样要落到该连接上——只扫自身成员会让每个子类都被
         * 迫重复声明一份访问器。基类链按派生优先走，override 形态先命中。 */
        for (zan_symbol_t *cls = obj_type->sym; cls;
             cls = (cls->type && cls->type->base_type)
                       ? cls->type->base_type->sym
                       : NULL) {
            for (int i = 0; i < cls->member_count; i++) {
                zan_symbol_t *m = cls->members[i];
                if (m->kind != SYM_METHOD || m->name.len != 6 ||
                    memcmp(m->name.str, "__Conn", 6) != 0) continue;
                if (!m->decl || m->decl->kind != AST_METHOD_DECL ||
                    m->decl->method_decl.params.count != 0) continue;
                zan_type_t *rt = zan_binder_resolve_type(
                    c->binder, m->decl->method_decl.return_type);
                if (!rt || !(rt->sym == idbc->sym ||
                             ((rt->kind == TYPE_CLASS ||
                               rt->kind == TYPE_STRUCT) &&
                              zan_binder_class_implements_iface(rt->sym,
                                                                idbc->sym))))
                    continue;
                zan_ast_node_t *callee = zan_ast_new(c->arena,
                                                     AST_MEMBER_ACCESS, expr->loc);
                callee->member.object = expr->member.object;
                callee->member.name = m->name;
                callee->member.null_cond = 0;
                zan_ast_node_t *call = zan_ast_new(c->arena, AST_CALL, expr->loc);
                call->call.callee = callee;
                zan_ast_list_init(&call->call.args);
                zan_ast_list_init(&call->call.type_args);
                *conn_out = rt;
                return call;
            }
        }
    }
    return NULL;
}

/* 连接实参是否能赋给构造器形参（形参为接口时按实现判定）。 */
static bool checker_conn_fits_param(zan_checker_t *c, zan_type_t *conn,
                                    zan_type_t *param) {
    if (!conn || !param || !conn->sym || !param->sym) return false;
    if (conn->sym == param->sym) return true;
    if (param->kind == TYPE_INTERFACE &&
        (conn->kind == TYPE_CLASS || conn->kind == TYPE_STRUCT))
        return zan_binder_class_implements_iface(conn->sym, param->sym);
    return false;
}

/* 原地降级：成员节点改写为 new <Dao 或 BaseRepository<E>>(...)。成员与
 * new_expr 是同一 union 的两个臂，必须先取尽 member 侧数据再落笔。 */
/* 树类型：System.Linq.ExprNode（ORM 门面既有重载 Where(ExprNode)）或
 * System.Linq.Expr<T>（用户 API 的装箱形态）。B-ID65 的树降级在 irgen
 * 发射口做（irgen_expr_lambda.c emit_lambda_typed），checker 侧只负责
 * 预检跳过与打分放行。 */
static bool checker_type_is_expr_tree_target(zan_type_t *t) {
    if (!t || t->kind != TYPE_CLASS || !t->sym) return false;
    if (t->sym->name.len == 8 &&
        memcmp(t->sym->name.str, "ExprNode", 8) == 0)
        return true;
    return t->sym->name.len == 4 &&
           memcmp(t->sym->name.str, "Expr", 4) == 0 &&
           t->type_arg_count == 1 && t->type_args && t->type_args[0];
}

static zan_type_t *checker_emit_accessor_new(zan_checker_t *c,
                                             zan_ast_node_t *expr,
                                             zan_ast_node_t *type_ref,
                                             zan_ast_node_t *arg) {
    expr->kind = AST_NEW_EXPR;
    expr->new_expr.type = type_ref;
    expr->new_expr.call_init = NULL;
    zan_ast_list_init(&expr->new_expr.args);
    if (arg) zan_ast_list_push(&expr->new_expr.args, arg, c->arena);
    zan_ast_list_init(&expr->new_expr.arg_inits);
    expr->new_expr.is_array = false;
    expr->new_expr.array_init = false;
    expr->new_expr.array_rank = 0;
    expr->new_expr.list_copy = false;
    return zan_checker_check_expr(c, expr);
}

static zan_type_t *checker_try_entity_accessor(zan_checker_t *c,
                                               zan_ast_node_t *expr,
                                               zan_type_t *obj_type) {
    zan_type_t *ent = NULL;
    if (!checker_is_table_entity(c, expr->member.name, &ent)) return NULL;
    zan_type_t *conn_ty = NULL;
    zan_ast_node_t *conn = checker_accessor_conn_expr(c, expr, obj_type,
                                                      &conn_ty);
    if (!conn) return NULL;

    /* ③ <Entity>Dao 已声明：单连接参数构造器直连。 */
    zan_istr_t dao_name = checker_istr_join(c->arena, expr->member.name,
                                            "Dao");
    zan_symbol_t *dsym = zan_binder_lookup(c->binder, dao_name);
    zan_type_t *dao = (dsym && dsym->kind == SYM_CLASS) ? dsym->type : NULL;
    if (dao && dao->kind == TYPE_CLASS && dao->sym) {
        for (int i = 0; i < dao->sym->member_count; i++) {
            zan_symbol_t *m = dao->sym->members[i];
            if (m->kind != SYM_CONSTRUCTOR || !m->decl ||
                m->decl->kind != AST_CONSTRUCTOR_DECL ||
                m->decl->method_decl.params.count != 1) continue;
            zan_type_t *pt = zan_binder_resolve_type(
                c->binder,
                m->decl->method_decl.params.items[0]->param.type);
            if (!checker_conn_fits_param(c, conn_ty, pt)) continue;
            return checker_emit_accessor_new(c, expr,
                checker_type_ref(c, dao_name, NULL, expr->loc), conn);
        }
        zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2095,
            "'%.*s' accessor needs a single-parameter constructor on "
            "'%.*s' taking the connection",
            (int)expr->member.name.len, expr->member.name.str,
            (int)dao_name.len, dao_name.str);
        return c->binder->type_error;
    }

    /* 回退：BaseRepository<Entity>(ZanSql.FromConnection(conn))。 */
    zan_symbol_t *bsym = zan_binder_lookup(
        c->binder, checker_istr_lit(c->arena, "BaseRepository"));
    zan_type_t *baset = (bsym && bsym->kind == SYM_CLASS) ? bsym->type : NULL;
    /* 只探存在性：开放泛型的 type_arg_count 形态不定，绑定交给降级
     * 后节点自身的 resolve_type。 */
    if (!baset || baset->kind != TYPE_CLASS || !baset->sym)
        return NULL;
    zan_ast_node_t *ent_ref = checker_type_ref(c, expr->member.name, NULL,
                                               expr->loc);
    zan_ast_node_t *base_ref = checker_type_ref(
        c, checker_istr_lit(c->arena, "BaseRepository"), ent_ref, expr->loc);
    zan_ast_node_t *zid = zan_ast_new(c->arena, AST_IDENTIFIER, expr->loc);
    zid->ident.name = checker_istr_lit(c->arena, "ZanSql");
    zid->ident.inst_type_ref = NULL;
    zan_ast_node_t *zcallee = zan_ast_new(c->arena, AST_MEMBER_ACCESS,
                                          expr->loc);
    zcallee->member.object = zid;
    zcallee->member.name = checker_istr_lit(c->arena, "FromConnection");
    zcallee->member.null_cond = 0;
    zan_ast_node_t *zcall = zan_ast_new(c->arena, AST_CALL, expr->loc);
    zcall->call.callee = zcallee;
    zan_ast_list_init(&zcall->call.args);
    zan_ast_list_push(&zcall->call.args, conn, c->arena);
    zan_ast_list_init(&zcall->call.type_args);
    return checker_emit_accessor_new(c, expr, base_ref, zcall);
}

static zan_type_t *check_member_access(zan_checker_t *c, zan_ast_node_t *expr,
                                       zan_type_t *obj_type, bool in_call_callee,
                                       zan_ast_node_t *call) {
    checker_reject_null_receiver(c, expr);
    if (expr->member.object) {
        zan_symbol_t *mg = expr_method_group(c, expr->member.object);
        if (mg) {
            reject_method_group(c, expr->member.object);
            return c->binder->type_error;
        }
    }
    if (obj_type && obj_type->kind == TYPE_ENUM && obj_type->sym &&
        expr->member.object->kind == AST_IDENTIFIER) {
        zan_symbol_t *os = zan_binder_lookup(c->binder,
                                             expr->member.object->ident.name);
        if (os && os->kind == SYM_ENUM) {
            int ei;
            for (ei = 0; ei < obj_type->sym->member_count; ei++) {
                zan_symbol_t *m = obj_type->sym->members[ei];
                if (m->kind == SYM_ENUM_MEMBER &&
                    m->name.len == expr->member.name.len &&
                    memcmp(m->name.str, expr->member.name.str,
                           (size_t)expr->member.name.len) == 0)
                    break;
            }
            if (ei == obj_type->sym->member_count) {
                zan_istr_t mn = expr->member.name;
                bool pseudo = mn.len == 8 &&
                    memcmp(mn.str, "TryParse", 8) == 0;
                if (!pseudo) {
                    zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2086,
                                  "enum type '%.*s' has no member '%.*s'",
                                  (int)obj_type->sym->name.len,
                                  obj_type->sym->name.str,
                                  (int)expr->member.name.len,
                                  expr->member.name.str);
                    return c->binder->type_error;
                }
            }
        }
    }
    if (obj_type && obj_type->kind == TYPE_NULLABLE) {
        zan_istr_t mn = expr->member.name;
        if (mn.len == 8 && memcmp(mn.str, "HasValue", 8) == 0) {
            return c->binder->type_bool;
        }
        if (mn.len == 5 && memcmp(mn.str, "Value", 5) == 0) {
            return obj_type->element_type ? obj_type->element_type : c->binder->type_error;
        }
        if (mn.len == 17 && memcmp(mn.str, "GetValueOrDefault", 17) == 0) {
            return obj_type->element_type ? obj_type->element_type : c->binder->type_error;
        }
        /* 可空类型整体渲染（自带 '?'），不再手写 '?' 拼在元素名后面——
         * 元素名本身也可能是带形状的（`bool?` 的元素是 `bool`，
         * 但嵌套形态要靠渲染器统一给）。 */
        zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2087,
                      "cannot access member '%.*s' directly on nullable type '%s'; access via '.Value' or check '.HasValue' first",
                      (int)mn.len, mn.str, type_name(obj_type));
        return c->binder->type_error;
    }
    if (obj_type && obj_type->sym) {
        /* 同名冲突裁决：构造器名 == 类型名，它会和同名字段一起命中——
         * SYM_CONSTRUCTOR 不是 "." 取值成员，必须跳过让字段赢（C# 同规：
         * 只有当所有同名成员都是方法时才按方法组处理）。构造器符号的
         * ->type 是类自身的类型，不跳过就会把 `c.Col` 定型成类 Col。 */
        zan_symbol_t *hit = NULL;
        zan_ast_node_t *hit_decl = NULL;
        bool saw_method = false;
        for (zan_symbol_t *s = obj_type->sym; s;
             s = (s->type && s->type->base_type) ? s->type->base_type->sym : NULL) {
            for (int i = s->member_count - 1; i >= 0; i--) {
                zan_symbol_t *m = s->members[i];
                if (!m || m->name.len != expr->member.name.len ||
                    memcmp(m->name.str, expr->member.name.str, m->name.len) != 0)
                    continue;
                if (m->kind == SYM_CONSTRUCTOR) continue;
                if (m->kind == SYM_METHOD) { saw_method = true; continue; }
                if (!hit) { hit = m; hit_decl = s->decl; }
            }
            if (hit) break;
        }
        if (hit) {
            if (!access_member_allowed(c, hit)) {
                report_inaccessible(c, hit, expr->loc);
                return c->binder->type_error;
            }
            if (hit->kind == SYM_FIELD && (hit->modifiers & MOD_STATIC) &&
                expr->member.object &&
                receiver_is_value_expr(c, expr->member.object)) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2088,
                              "'%.*s' is a static field of '%.*s'; "
                              "qualify it with the type name "
                              "('%.*s.%.*s'), not an instance",
                              (int)expr->member.name.len,
                              expr->member.name.str,
                              (int)obj_type->sym->name.len,
                              obj_type->sym->name.str,
                              (int)obj_type->sym->name.len,
                              obj_type->sym->name.str,
                              (int)expr->member.name.len,
                              expr->member.name.str);
                return c->binder->type_error;
            }
            zan_type_t *mt = hit->type;
            if (mt && obj_type->type_args && obj_type->type_arg_count > 0 &&
                hit_decl &&
                (hit_decl->kind == AST_CLASS_DECL ||
                 hit_decl->kind == AST_STRUCT_DECL)) {
                zan_ast_list_t *tps = &hit_decl->type_decl.type_params;
                if (tps->count == obj_type->type_arg_count) {
                    mt = zan_binder_subst_named(c->binder, mt, tps, obj_type->type_args);
                }
            }
            return mt ? mt : c->binder->type_error;
        }
        if (saw_method) {
            /* 只命中方法/构造器：方法组不能当值用。取值位置必须报
             * （E2039）——静默 type_error 曾被下游（if 条件、二元
             * 操作数等）当「已报过错误」跳过，裸方法名漏到发码侧按
             * E3090 炸 LLVM 或当值传参运行期腐坏（B-ID151）。调用
             * 位置与实参位置保持静默：前者合法方法调用走 check_call
             * 的重载路径；后者实参的合法性（委托形参的签名兼容、
             * 非委托形参的 E2039/E2098）由 check_call_arg_type 与
             * 重载评分按目标形参裁决——这里抢报会把「方法组 → 委托
             * 实参」（sf.Scan("", s.RecoverRecord)）误杀。diag 按
             * file/line/col/消息去重（B-ID106），先报的场合不双报。 */
            if (!in_call_callee && !c->in_call_args &&
                !c->in_assignable_ctx) {
                reject_method_group(c, expr);
            }
            return c->binder->type_error;
        }
    }
    {
        zan_type_t *rt = zan_refl_member_type(c->binder, obj_type,
                                              expr->member.name);
        if (rt) return rt;
    }
    if (obj_type && obj_type->kind == TYPE_ARRAY) {
        bool is_len = expr->member.name.len == 6 &&
            memcmp(expr->member.name.str, "Length", 6) == 0;
        bool is_cnt = expr->member.name.len == 5 &&
            memcmp(expr->member.name.str, "Count", 5) == 0;
        if (is_len || is_cnt) return c->binder->type_int;
    }
    if (obj_type && obj_type->kind == TYPE_STRUCT &&
        ((obj_type->name.len == 4 && memcmp(obj_type->name.str, "Span", 4) == 0) ||
         (obj_type->name.len == 12 && memcmp(obj_type->name.str, "ReadOnlySpan", 12) == 0))) {
        bool is_len = expr->member.name.len == 6 &&
            memcmp(expr->member.name.str, "Length", 6) == 0;
        bool is_cnt = expr->member.name.len == 5 &&
            memcmp(expr->member.name.str, "Count", 5) == 0;
        if (is_len || is_cnt) return c->binder->type_int;
    }
    if (obj_type && type_is_scalar_primitive(obj_type)) {
        if (obj_type->kind == TYPE_STRING && expr->member.name.len == 6 &&
            memcmp(expr->member.name.str, "Length", 6) == 0) {
            return c->binder->type_int;
        }
        if (!in_call_callee) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2089,
                          "type '%s' has no member '%.*s'",
                          type_name(obj_type),
                          (int)expr->member.name.len, expr->member.name.str);
            return c->binder->type_error;
        }
        /* 调用位置：内建标量的成员集是定死的（builtin_api 表），
         * 表里没有这个名字 = 这个方法不存在，当场报。扩展方法
         * （`this string` 接收者）不在内建表里，由统一判定一并算进去。 */
        if (checker_call_target_member_state(c, obj_type, expr->member.name,
                                             call) == 0) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2090,
                          "type '%s' has no method '%.*s'",
                          type_name(obj_type),
                          (int)expr->member.name.len, expr->member.name.str);
        }
        return c->binder->type_error;
    }
    if (obj_type && obj_type != c->binder->type_error && obj_type->sym &&
        (obj_type->kind == TYPE_CLASS || obj_type->kind == TYPE_STRUCT ||
         obj_type->kind == TYPE_INTERFACE)) {
        if (!in_call_callee) {
            zan_type_t *dao = checker_try_entity_accessor(c, expr, obj_type);
            if (dao) return dao;
            checker_report_no_member(c, expr, obj_type, expr->member.name, false);
            return c->binder->type_error;
        }
        /* 调用位置：这个类型上根本没有这个名字的方法 —— 当场报，
         * 不用拖到发码阶段变成 "code generation failed"（A476）。 */
        if (checker_call_target_member_state(c, obj_type, expr->member.name,
                                             call) == 0)
            checker_report_no_member(c, expr, obj_type, expr->member.name, true);
        return c->binder->type_error;
    }
    return c->binder->type_error;
}

