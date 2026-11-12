/* `Task.<method>(...)` 的静态调用识别：与 irgen 的 is_call_to 同口径
 * （按接收者标识符的字面名判定，不依赖类型解析结果）。 */
static bool checker_is_task_call(zan_ast_node_t *expr, const char *method) {
    if (!expr || expr->kind != AST_CALL) return false;
    zan_ast_node_t *callee = expr->call.callee;
    if (!callee || callee->kind != AST_MEMBER_ACCESS) return false;
    if (!callee->member.object || callee->member.object->kind != AST_IDENTIFIER)
        return false;
    zan_istr_t obj_name = callee->member.object->ident.name;
    zan_istr_t method_name = callee->member.name;
    return obj_name.len == 4 && memcmp(obj_name.str, "Task", 4) == 0 &&
           method_name.len == (int)strlen(method) &&
           memcmp(method_name.str, method, (size_t)method_name.len) == 0;
}


/* Task.Run / Task.Spawn 的结果类型按实参形态区分，与 irgen 的帧形态一一对应：
 *
 *   实参 lambda / 委托   → action 帧：无结果，由完成路径自行释放 → Task（无类型实参）
 *   实参调用且结果非 void → keep_result 帧：有结果，由用户侧释放   → Task<T>
 *   实参调用且结果 void   → 脱钩帧：无结果                          → Task
 *   Task.Spawn(...)       → 脱钩帧：无结果                          → Task
 *
 * 这条区分是「Task.Run(lambda) 帧被释放两次」的**类型侧收口**：lambda 形态的
 * 句柄没有结果，把它当 Task<T> 用去读 .Result/.Wait()，会让同一帧被释放两次
 * （完成路径 reap 一次 + 用户侧 reap 一次），运行期 double free 直接 abort。
 * 收口后这种写法在编译期就被拒绝，而"跑一个 Action 再等它"仍然合法
 * （Task t = Task.Run(action); t.Wait(); —— 无结果任务，Wait 不 reap）。
 */
static zan_symbol_t *checker_find_field(zan_symbol_t *type_sym, zan_istr_t name);
static zan_type_t *checker_find_local(zan_checker_t *c, zan_istr_t name);
static bool access_member_allowed(zan_checker_t *c, zan_symbol_t *m);
static void report_inaccessible(zan_checker_t *c, zan_symbol_t *m, zan_loc_t loc);

static zan_type_t *task_run_result_type(zan_checker_t *c, zan_ast_node_t *expr,
                                        zan_type_t **arg_types) {
    if (!expr || expr->call.args.count != 1 || !arg_types) return NULL;
    zan_ast_node_t *arg = expr->call.args.items[0];
    if (arg && arg->kind == AST_NAMED_ARG) arg = arg->named_arg.expr;
    if (!arg) return NULL;
    /* 无结果任务句柄 */
    zan_type_t *base = zan_binder_make_task_type(c->binder, NULL);
    if (arg->kind == AST_LAMBDA) return base;
    zan_symbol_t *owner = NULL;
    zan_istr_t name = {0};
    int receiver = 0;
    if (arg->kind == AST_IDENTIFIER && !checker_find_local(c, arg->ident.name) &&
        !checker_find_field(c->current_type_sym, arg->ident.name)) {
        owner = c->current_type_sym;
        name = arg->ident.name;
    } else if (arg->kind == AST_MEMBER_ACCESS) {
        zan_ast_node_t *obj = arg->member.object;
        zan_type_t *ot = zan_checker_check_expr(c, obj);
        owner = ot ? ot->sym : NULL;
        name = arg->member.name;
        receiver = 2;
        if (obj->kind == AST_IDENTIFIER && !checker_find_local(c, obj->ident.name) &&
            !checker_find_field(c->current_type_sym, obj->ident.name)) {
            zan_symbol_t *ts = zan_binder_lookup(c->binder, obj->ident.name);
            if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT)) receiver = 1;
        }
    }
    bool has_this = c->current_method_decl &&
        !(c->current_method_decl->method_decl.modifiers & MOD_STATIC);
    int action_error = 0;
    zan_symbol_t *method = zan_binder_task_action_method(c->binder, owner, name,
        receiver, has_this, &action_error);
    if (method) {
        if (!access_member_allowed(c, method)) report_inaccessible(c, method, arg->loc);
        if (action_error)
            zan_diag_emit_code(c->diag, DIAG_ERROR, arg->loc, 3052,
                action_error == 2 ? "Task method group requires a matching receiver (instance methods need this)" :
                action_error == 3 ? "ambiguous Task Action method group" :
                "Task.Run delegate must be a parameterless synchronous nongeneric void method");
        return base;
    }
    if (checker_is_task_call(expr, "Spawn")) return base;
    zan_type_t *at = arg_types[0];
    if (!at) return NULL; /* 实参类型未知：维持旧的宽松行为 */
    if (at->kind == TYPE_DELEGATE) return base;
    if (at == c->binder->type_error) return NULL; /* 类型不明：维持旧的宽松行为 */
    if (at->kind == TYPE_VOID) return base;
    zan_type_t *elem = (at->kind == TYPE_TASK)
                       ? (at->type_arg_count == 1 ? at->type_args[0] : NULL)
                       : at;
    if (!elem) return base;
    return zan_binder_make_task_type(c->binder, elem);
}

static zan_type_t *builtin_call_result_type(zan_checker_t *c,
                                            zan_type_t *recv,
                                            zan_istr_t name) {
    const char *bt = NULL;
    if (!recv) return NULL;
    if (recv->kind == TYPE_STRING) bt = "string";
    else if (recv->kind == TYPE_CLASS && recv->name.len == 4 &&
             memcmp(recv->name.str, "List", 4) == 0) bt = "List";
    else if (recv->kind == TYPE_CLASS && recv->name.len == 4 &&
             memcmp(recv->name.str, "Dict", 4) == 0) bt = "Dict";
    else if (recv->kind == TYPE_CLASS && recv->name.len == 13 &&
             memcmp(recv->name.str, "StringBuilder", 13) == 0)
        bt = "StringBuilder";
    if (recv->kind == TYPE_STRING &&
        ((name.len == 6 && memcmp(name.str, "AsSpan", 6) == 0) ||
         (name.len == 14 && memcmp(name.str, "AsReadOnlySpan", 14) == 0))) {
        return zan_binder_make_readonly_span_type(c->binder, c->binder->type_char);
    }
    if (recv->kind == TYPE_ARRAY && recv->element_type) {
        if (name.len == 6 && memcmp(name.str, "AsSpan", 6) == 0)
            return zan_binder_make_span_type(c->binder, recv->element_type);
        if (name.len == 14 && memcmp(name.str, "AsReadOnlySpan", 14) == 0)
            return zan_binder_make_readonly_span_type(c->binder, recv->element_type);
    }

    if (recv->kind == TYPE_STRUCT &&
        ((recv->name.len == 4 && memcmp(recv->name.str, "Span", 4) == 0) ||
         (recv->name.len == 12 && memcmp(recv->name.str, "ReadOnlySpan", 12) == 0)) &&
        name.len == 5 && memcmp(name.str, "Slice", 5) == 0) {
        return recv;
    }
    if (recv->kind == TYPE_NULLABLE && recv->element_type &&
        name.len == 17 && memcmp(name.str, "GetValueOrDefault", 17) == 0) {
        return recv->element_type;
    }
    if (recv->kind == TYPE_OBJECT || recv->kind == TYPE_CLASS ||
        recv->kind == TYPE_INTERFACE) {
        if (name.len == 8 && memcmp(name.str, "ToString", 8) == 0)
            return c->binder->type_string;
        if (name.len == 6 && memcmp(name.str, "Equals", 6) == 0)
            return c->binder->type_bool;
        if (name.len == 11 && memcmp(name.str, "GetHashCode", 11) == 0)
            return c->binder->type_int;
    }
    {
        zan_type_t *rt = zan_refl_member_type(c->binder, recv, name);
        if (rt) return rt;
    }
    if (!bt) return NULL;
    if (zan_builtin_member_kind(bt, name.str, (int)name.len) != 'M') return NULL;
    const char *res = zan_builtin_member_result(bt, name.str, (int)name.len);
    if (!res) return NULL;
    if (strcmp(res, "void") == 0) return c->binder->type_void;
    if (strcmp(res, "string") == 0) return c->binder->type_string;
    if (strcmp(res, "int") == 0) return c->binder->type_int;
    if (strcmp(res, "long") == 0) return c->binder->type_long;
    if (strcmp(res, "double") == 0) return c->binder->type_double;
    if (strcmp(res, "bool") == 0) return c->binder->type_bool;
    if (strcmp(res, "char") == 0) return c->binder->type_char;
    if (strcmp(res, "nint") == 0) return c->binder->type_nint;
    if (strcmp(res, "List<string>") == 0)
        return zan_binder_make_list_type(c->binder, c->binder->type_string);
    if (strcmp(res, "T[]") == 0) {
        zan_type_t *elem = recv->type_arg_count > 0 ? recv->type_args[0] : NULL;
        if (!elem) return NULL;
        return zan_binder_make_array_type(c->binder, elem);
    }
    return NULL;
}

#define CHECKER_DERIVES_MAX_DEPTH 8192

/* SyncStructureAll(Async) 编译期实体发现（定义见文件尾 SyncStructureAll 节） */
static bool checker_recv_is_conn(zan_checker_t *c, zan_type_t *recv);
static zan_ast_node_t *checker_synthesize_table_array(zan_checker_t *c,
                                                      zan_loc_t loc);
static zan_ast_node_t *checker_synthesize_controllers_array(zan_checker_t *c,
                                                            zan_loc_t loc);
static zan_ast_node_t *checker_synthesize_dispatcher_ref(zan_checker_t *c,
                                                         zan_loc_t loc,
                                                         bool framework);
static bool checker_is_builtin_class_name(zan_istr_t n);
static bool checker_is_target_builtin_class(zan_ast_node_t *obj) {
    if (!obj) return false;
    if (obj->kind == AST_IDENTIFIER) return checker_is_builtin_class_name(obj->ident.name);
    if (obj->kind == AST_MEMBER_ACCESS) return checker_is_builtin_class_name(obj->member.name);
    return false;
}
static void checker_bind_call_intrinsic(zan_checker_t *c, zan_ast_node_t *expr,
                                       zan_symbol_t *called_sym, zan_type_t *recv) {
    if (!expr || expr->kind != AST_CALL) return;
    expr->call.resolved_sym = called_sym;
    if (called_sym) {
        if ((called_sym->modifiers & MOD_INTRINSIC) != 0 ||
            (called_sym->parent && (called_sym->parent->modifiers & MOD_INTRINSIC) != 0)) {
            expr->call.is_intrinsic = 1;
        } else if (called_sym->parent && called_sym->parent->name.str && called_sym->name.str &&
                   zan_builtin_has_native_member(called_sym->parent->name.str,
                                                 called_sym->parent->name.len,
                                                 called_sym->name.str,
                                                 called_sym->name.len)) {
            expr->call.is_intrinsic = 1;
        } else {
            expr->call.is_intrinsic = 0;
        }
        return;
    }
    /* called_sym 为 NULL 时：若调用目标为原生内建类（Console/Math/File/BitOperations 等）
     * 或接收者为标量/原始容器/TypeInfo，标记为内建 Intrinsic；否则为普通未解析调用 */
    if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
        if (checker_is_target_builtin_class(expr->call.callee->member.object)) {
            expr->call.is_intrinsic = 1;
            return;
        }
    }
    if (recv) {
        if (recv->kind == TYPE_STRING || recv->kind == TYPE_BOOL ||
            recv->kind == TYPE_BYTE || recv->kind == TYPE_SBYTE ||
            recv->kind == TYPE_SHORT || recv->kind == TYPE_USHORT ||
            recv->kind == TYPE_INT || recv->kind == TYPE_UINT ||
            recv->kind == TYPE_LONG || recv->kind == TYPE_ULONG ||
            recv->kind == TYPE_FLOAT || recv->kind == TYPE_DOUBLE ||
            recv->kind == TYPE_CHAR || recv->kind == TYPE_NINT ||
            recv->kind == TYPE_ARRAY || recv->kind == TYPE_NULLABLE ||
            zan_refl_is_typeinfo(recv) ||
            checker_is_builtin_class_name(recv->name)) {
            expr->call.is_intrinsic = 1;
            return;
        }
        if (recv->name.str &&
            ((recv->name.len == 4 && memcmp(recv->name.str, "Span", 4) == 0) ||
             (recv->name.len == 12 && memcmp(recv->name.str, "ReadOnlySpan", 12) == 0))) {
            expr->call.is_intrinsic = 1;
            return;
        }
    }
    expr->call.is_intrinsic = 0;
}
static zan_istr_t checker_istr_lit(zan_arena_t *arena, const char *s);
void zan_checker_init(zan_checker_t *c, zan_binder_t *binder,
                      zan_arena_t *arena, zan_diag_t *diag) {
    memset(c, 0, sizeof(*c));
    c->binder = binder;
    c->arena = arena;
    c->diag = diag;
    c->current_return_type = NULL;
}

static bool checker_class_is_abstract(zan_symbol_t *sym);

static void no_runtime_reject(zan_checker_t *c, zan_loc_t loc, const char *what) {
    if (!c->in_no_runtime) return;
    zan_diag_emit_code(c->diag, DIAG_ERROR, loc, 2000,
                  "%s is not allowed in a [NoRuntime] method", what);
}

static bool no_runtime_arc_return_type(zan_type_t *type) {
    return type && (type->kind == TYPE_STRING ||
                    type->kind == TYPE_INTERFACE ||
                    type->kind == TYPE_CLASS);
}

static void no_runtime_warn_arc_return(zan_checker_t *c,
                                       zan_ast_node_t *call,
                                       zan_type_t *type) {
    if (!c->in_no_runtime || !no_runtime_arc_return_type(type)) return;
    zan_diag_emit_code(c->diag, DIAG_WARNING, call->loc, 2001,
                  "call in a [NoRuntime] method returns an ARC-managed value "
                  "with an owned +1 reference, but [NoRuntime] emits no ARC; "
                  "the +1 will not be released and will leak. Move the call "
                  "out of the [NoRuntime] method or use a non-owning return");
}

static zan_type_t *checker_nullable_base(zan_type_t *t) {
    return (t && t->kind == TYPE_NULLABLE && t->element_type)
        ? t->element_type : t;
}

static zan_type_t *checker_clone_type(zan_checker_t *c, zan_type_t *src) {
    if (!src) return NULL;
    zan_type_t *t = (zan_type_t *)zan_arena_alloc(c->arena, sizeof(zan_type_t));
    *t = *src;
    return t;
}

/* 查找a named method (e */
static zan_symbol_t *checker_find_method(zan_symbol_t *type_sym, zan_istr_t name) {
    while (type_sym) {
        for (int i = 0; i < type_sym->member_count; i++) {
            zan_symbol_t *m = type_sym->members[i];
            if (m && m->kind == SYM_METHOD &&
                m->name.len == name.len &&
                memcmp(m->name.str, name.str, (size_t)name.len) == 0)
                return m;
        }
        if (type_sym->kind == SYM_INTERFACE && type_sym->type && type_sym->type->interfaces) {
            for (int i = 0; i < type_sym->type->interface_count; i++) {
                zan_type_t *it = type_sym->type->interfaces[i];
                if (it && it->sym) {
                    zan_symbol_t *im = checker_find_method(it->sym, name);
                    if (im) return im;
                }
            }
        }
        type_sym = (type_sym->type && type_sym->type->base_type)
            ? type_sym->type->base_type->sym : NULL;
    }
    return NULL;
}


static zan_symbol_t *checker_find_field(zan_symbol_t *type_sym, zan_istr_t name) {
    while (type_sym) {
        /* 从后往前查找，确保派生类隐藏字段（排在基类复制字段之后）优先命中并提前退出 */
        for (int i = type_sym->member_count - 1; i >= 0; i--) {
            zan_symbol_t *m = type_sym->members[i];
            if (m && (m->kind == SYM_FIELD || m->kind == SYM_PROPERTY) &&
                m->name.len == name.len &&
                memcmp(m->name.str, name.str, (size_t)name.len) == 0)
                return m;
        }
        type_sym = (type_sym->type && type_sym->type->base_type)
            ? type_sym->type->base_type->sym : NULL;
    }
    return NULL;
}


static bool checker_type_derives_from(zan_type_t *sub, zan_type_t *sup);
static bool checker_type_is_ref(zan_type_t *t);

static uint32_t access_module_len(zan_istr_t ns) {
    int dots = 0;
    for (uint32_t i = 0; i < ns.len; i++) {
        if (ns.str[i] == '.') {
            dots++;
            if (dots == 2) return i;
        }
    }
    return ns.len;
}

static bool access_same_module(zan_istr_t a, zan_istr_t b) {
    uint32_t la = access_module_len(a), lb = access_module_len(b);
    return la == lb && memcmp(a.str, b.str, (size_t)la) == 0;
}

static zan_istr_t access_symbol_ns(zan_symbol_t *s) {
    while (s) {
        if (s->decl && zan_ast_ns_name(s->decl).len)
            return zan_ast_ns_name(s->decl);
        s = s->parent;
    }
    return (zan_istr_t){NULL, 0};
}

static zan_ast_node_t *access_current_decl(zan_checker_t *c) {
    return c->current_type_sym ? c->current_type_sym->decl : NULL;
}

static void report_inaccessible(zan_checker_t *c, zan_symbol_t *m,
                                zan_loc_t loc) {
    zan_istr_t owner_name = m->parent ? m->parent->name : m->name;
    if (!c->current_type_sym) return;
    if (m->modifiers & MOD_PRIVATE)
        zan_diag_emit_code(c->diag, DIAG_ERROR, loc, 2002,
                      "'%.*s.%.*s' is private and cannot be accessed "
                      "from '%.*s'",
                      (int)owner_name.len, owner_name.str,
                      (int)m->name.len, m->name.str,
                      (int)c->current_type_sym->name.len,
                      c->current_type_sym->name.str);
    else if (m->modifiers & MOD_PROTECTED)
        zan_diag_emit_code(c->diag, DIAG_ERROR, loc, 2003,
                      "'%.*s.%.*s' is protected and cannot be accessed "
                      "from '%.*s'",
                      (int)owner_name.len, owner_name.str,
                      (int)m->name.len, m->name.str,
                      (int)c->current_type_sym->name.len,
                      c->current_type_sym->name.str);
    else if (m->modifiers & MOD_INTERNAL) {
        zan_istr_t mod_ns = access_symbol_ns(m);
        zan_ast_node_t *cur = access_current_decl(c);
        zan_istr_t cur_ns = cur ? zan_ast_ns_name(cur) : (zan_istr_t){0};
        zan_istr_t cur_name = cur_ns.len ? cur_ns : c->current_type_sym->name;
        zan_diag_emit_code(c->diag, DIAG_ERROR, loc, 2004,
                      "'%.*s.%.*s' is internal to '%.*s' and cannot be "
                      "accessed from '%.*s'",
                      (int)owner_name.len, owner_name.str,
                      (int)m->name.len, m->name.str,
                      (int)mod_ns.len, mod_ns.str,
                      (int)cur_name.len, cur_name.str);
    }
}

static bool access_member_allowed(zan_checker_t *c, zan_symbol_t *m) {
    if (!(m->modifiers & (MOD_PRIVATE | MOD_PROTECTED | MOD_INTERNAL)))
        return true;
    if (!c->current_type_sym || !m->parent ||
        !m->parent->type || !c->current_type_sym->type)
        return false;
    if (c->current_type_sym == m->parent) return true;
    if ((m->modifiers & MOD_PROTECTED) &&
        checker_type_derives_from(c->current_type_sym->type,
                                  m->parent->type))
        return true;
    if ((m->modifiers & MOD_INTERNAL) &&
        access_same_module(access_symbol_ns(c->current_type_sym),
                           access_symbol_ns(m)))
        return true;
    return false;
}

static zan_symbol_t *checker_field_visible(zan_checker_t *c,
                                           zan_symbol_t *ts, zan_loc_t loc,
                                           zan_istr_t name) {
    zan_symbol_t *f = checker_find_field(ts, name);
    if (f && !access_member_allowed(c, f)) {
        report_inaccessible(c, f, loc);
        return NULL;
    }
    return f;
}

struct checker_local {
    zan_istr_t name;
    zan_type_t *type;
    zan_symbol_t *null_src;
    struct checker_local *next;
};

static struct checker_local *checker_find_local_slot(zan_checker_t *c,
                                                     zan_istr_t name) {
    for (struct checker_local *l = c->locals; l; l = l->next) {
        if (l->name.len == name.len &&
            memcmp(l->name.str, name.str, (size_t)name.len) == 0)
            return l;
    }
    return NULL;
}

static zan_type_t *checker_find_local(zan_checker_t *c, zan_istr_t name) {
    for (struct checker_local *l = c->locals; l; l = l->next) {
        if (l->name.len == name.len &&
            memcmp(l->name.str, name.str, (size_t)name.len) == 0)
            return l->type;
    }
    return NULL;
}

static void checker_add_local(zan_checker_t *c, zan_istr_t name,
                              zan_type_t *type) {
    if (!type || type == c->binder->type_error) return;
    struct checker_local *l = (struct checker_local *)
        zan_arena_alloc(c->arena, sizeof(struct checker_local));
    l->name = name;
    l->type = type;
    l->null_src = NULL;
    l->next = c->locals;
    c->locals = l;
}

/* 把名字放进作用域，**不**因为类型未知而丢弃：类型未知时记成 error 类型，
 * 让引用它的检查安静降级（而不是落到"同名类字段"上去）。 */
static void checker_add_local_permissive(zan_checker_t *c, zan_istr_t name,
                                         zan_type_t *type) {
    struct checker_local *l = (struct checker_local *)
        zan_arena_alloc(c->arena, sizeof(struct checker_local));
    l->name = name;
    l->type = (type && type != c->binder->type_error) ? type
                                                       : c->binder->type_error;
    l->null_src = NULL;
    l->next = c->locals;
    c->locals = l;
}

static void checker_mark_local_null_src(zan_checker_t *c, zan_ast_node_t *decl) {
    zan_ast_node_t *init = decl->var_decl.initializer;
    if (!init || init->kind != AST_CALL || init != c->last_call_node) return;
    if (!c->last_call_method) return;
    struct checker_local *l = checker_find_local_slot(c, decl->var_decl.name);
    if (l) l->null_src = c->last_call_method;
}

static bool field_is_readonly(zan_symbol_t *field) {
    return field && field->decl && field->decl->kind == AST_FIELD_DECL &&
           (field->decl->field_decl.modifiers & MOD_READONLY) != 0;
}

static bool property_is_readonly(zan_symbol_t *prop) {
    if (!prop || prop->kind != SYM_PROPERTY || !prop->decl) return false;
    return !prop->decl->field_decl.has_setter &&
           !prop->decl->field_decl.has_init;
}

static bool property_is_init_only(zan_symbol_t *prop) {
    if (!prop || prop->kind != SYM_PROPERTY || !prop->decl) return false;
    return prop->decl->field_decl.has_init &&
           !prop->decl->field_decl.has_setter;
}

static zan_symbol_t *assign_target_field(zan_checker_t *c, zan_ast_node_t *lhs,
                                         zan_symbol_t **owner) {
    if (!lhs) return NULL;
    if (lhs->kind == AST_IDENTIFIER) {
        if (!c->current_type_sym) return NULL;
        if (owner) *owner = c->current_type_sym;
        return checker_field_visible(c, c->current_type_sym, lhs->loc,
                                     lhs->ident.name);
    }
    if (lhs->kind == AST_MEMBER_ACCESS) {
        zan_symbol_t *ts = NULL;
        if (lhs->member.object && lhs->member.object->kind == AST_THIS_EXPR) {
            ts = c->current_type_sym;
        } else {
            zan_type_t *ot = zan_checker_check_expr(c, lhs->member.object);
            ts = ot ? ot->sym : NULL;
        }
        if (!ts) return NULL;
        if (owner) *owner = ts;
        return checker_field_visible(c, ts, lhs->loc, lhs->member.name);
    }
    return NULL;
}

static void check_readonly_assignment(zan_checker_t *c, zan_ast_node_t *expr) {
    if (expr && expr->binary.left && expr->binary.left->kind == AST_INDEX) {
        zan_type_t *obj = zan_checker_check_expr(c, expr->binary.left->index.object);
        if (obj && obj->kind == TYPE_STRUCT && obj->name.len == 12 &&
            memcmp(obj->name.str, "ReadOnlySpan", 12) == 0) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2005,
                          "cannot assign to indexing expression of ReadOnlySpan");
            return;
        }
    }
    if (expr && expr->binary.left && expr->binary.left->kind == AST_IDENTIFIER &&
        checker_find_local(c, expr->binary.left->ident.name))
        return;
    zan_symbol_t *owner = NULL;
    zan_symbol_t *field = assign_target_field(c, expr->binary.left, &owner);
    if (property_is_readonly(field)) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2006,
                      "property '%.*s' has no setter and cannot be assigned",
                      (int)field->name.len, field->name.str);
        return;
    }
    if (property_is_init_only(field)) {
        if (c->in_ctor && field->parent == c->current_type_sym) return;
        zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2007,
                      "property '%.*s' has an init accessor and can only be "
                      "assigned in an object initializer or a constructor of "
                      "'%.*s'",
                      (int)field->name.len, field->name.str,
                      (int)(field->parent ? field->parent->name.len : 0),
                      field->parent ? field->parent->name.str : "");
        return;
    }
    if (!field_is_readonly(field)) return;
    /* 派生类构造函数不可初始化基类声明的 readonly 只读字段 */
    if (c->in_ctor && field->parent == c->current_type_sym) return;
    zan_symbol_t *decl_owner = field->parent ? field->parent : owner;
    zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2008,
                  "cannot assign to readonly field '%.*s' outside a "
                  "constructor of '%.*s'",
                  (int)field->name.len, field->name.str,
                  (int)(decl_owner ? decl_owner->name.len : 0),
                  decl_owner ? decl_owner->name.str : "");
}

static void check_readonly_incdec(zan_checker_t *c, zan_ast_node_t *expr) {
    if (!expr || !expr->unary.operand) return;
    zan_ast_node_t *operand = expr->unary.operand;
    zan_symbol_t *field = NULL;
    if (operand->kind == AST_IDENTIFIER) {
        if (checker_find_local(c, operand->ident.name)) return;
        if (!c->current_type_sym) return;
        field = checker_field_visible(c, c->current_type_sym, expr->loc,
                                      operand->ident.name);
    } else if (operand->kind == AST_MEMBER_ACCESS) {
        zan_symbol_t *ts = NULL;
        if (operand->member.object &&
            operand->member.object->kind == AST_THIS_EXPR) {
            ts = c->current_type_sym;
        } else {
            zan_type_t *ot = zan_checker_check_expr(c, operand->member.object);
            ts = ot ? ot->sym : NULL;
        }
        if (!ts) return;
        field = checker_field_visible(c, ts, expr->loc, operand->member.name);
    }
    if (!property_is_readonly(field)) return;
    zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2006,
                  "property '%.*s' has no setter and cannot be assigned",
                  (int)field->name.len, field->name.str);
}

static bool checker_type_assignable(zan_type_t *target, zan_type_t *value);

static zan_type_t *checker_assignment_target_type(zan_checker_t *c,
                                                   zan_ast_node_t *lhs,
                                                   zan_type_t *resolved_lhs) {
    if (!lhs) return NULL;
    if (lhs->kind == AST_IDENTIFIER) {
        zan_type_t *local = checker_find_local(c, lhs->ident.name);
        if (local) return local;
        zan_symbol_t *field = assign_target_field(c, lhs, NULL);
        return field ? field->type : NULL;
    }
    if (lhs->kind == AST_MEMBER_ACCESS) {
        zan_type_t *obj = zan_checker_check_expr(c, lhs->member.object);
        zan_symbol_t *sym = obj && obj->sym
            ? checker_field_visible(c, obj->sym, lhs->loc, lhs->member.name)
            : NULL;
        /* Member access already specializes the field type through the
         * receiver (including delegate argument/return types). Returning the
         * declaration type here discards that substitution before assignment
         * compatibility is checked. Keep field lookup/shadowing unchanged. */
        return sym ? resolved_lhs : NULL;
    }
    if (lhs->kind == AST_INDEX) {
        zan_type_t *obj = zan_checker_check_expr(c, lhs->index.object);
        zan_checker_check_expr(c, lhs->index.index);
        if (obj && obj->kind == TYPE_ARRAY) return obj->element_type;
        if (obj && obj->kind == TYPE_STRING) return c->binder->type_char;
        if (obj && obj->kind == TYPE_CLASS && obj->type_arg_count == 1 &&
            obj->name.len == 4 && memcmp(obj->name.str, "List", 4) == 0)
            return obj->type_args[0];
        if (obj && obj->kind == TYPE_STRUCT && obj->type_arg_count == 1 &&
            obj->name.len == 4 && memcmp(obj->name.str, "Span", 4) == 0)
            return obj->type_args[0];
    }
    return NULL;
}

static zan_type_t *checker_index_set_target(zan_checker_t *c,
                                            zan_ast_node_t *lhs,
                                            zan_type_t *right) {
    zan_type_t *obj = zan_checker_check_expr(c, lhs->index.object);
    zan_type_t *idx = zan_checker_check_expr(c, lhs->index.index);
    if (!obj || !obj->sym ||
        (obj->kind != TYPE_CLASS && obj->kind != TYPE_STRUCT))
        return NULL;
    zan_istr_t op_name = {(char *)"op_index_set", 12};
    for (int i = 0; i < obj->sym->member_count; i++) {
        zan_symbol_t *m = obj->sym->members[i];
        if (!m) continue;
        if (m->kind != SYM_METHOD || m->name.len != op_name.len ||
            memcmp(m->name.str, op_name.str, op_name.len) != 0) continue;
        if (!m->decl || m->decl->kind != AST_METHOD_DECL)
            continue;
        zan_ast_list_t *ps = &m->decl->method_decl.params;
        int n = ps->count;
        if (n < 2) continue;
        zan_ast_node_t *pidx = ps->items[n - 2];
        zan_ast_node_t *pval = ps->items[n - 1];
        if (n >= 3) {
            zan_ast_node_t *pself = ps->items[0];
            if (pself && pself->kind == AST_PARAM && pself->param.type) {
                zan_type_t *ptself =
                    zan_binder_resolve_type(c->binder, pself->param.type);
                if (ptself && ptself->kind != TYPE_TYPE_PARAM &&
                    !checker_type_assignable(ptself, obj))
                    continue;
            }
        }
        if (pidx->kind != AST_PARAM || pval->kind != AST_PARAM) continue;
        zan_type_t *pt1 = zan_binder_resolve_type(c->binder, pidx->param.type);
        zan_type_t *pt2 = zan_binder_resolve_type(c->binder, pval->param.type);
        if (obj->type_args && obj->type_arg_count > 0 && obj->sym && obj->sym->decl &&
            (obj->sym->decl->kind == AST_CLASS_DECL || obj->sym->decl->kind == AST_STRUCT_DECL)) {
            zan_ast_list_t *tps = &obj->sym->decl->type_decl.type_params;
            if (tps->count == obj->type_arg_count) {
                pt1 = zan_binder_subst_named(c->binder, pt1, tps, obj->type_args);
                pt2 = zan_binder_subst_named(c->binder, pt2, tps, obj->type_args);
            }
        }
        if (idx && !checker_type_assignable(pt1, idx)) continue;
        if (right && !checker_type_assignable(pt2, right)) continue;
        return pt2;
    }
    return NULL;
}

static bool expr_captures_this(zan_checker_t *c, zan_ast_node_t *node) {
    if (!node) return false;
    switch (node->kind) {
    case AST_THIS_EXPR:
        return true;
    case AST_IDENTIFIER: {
        zan_istr_t name = node->ident.name;
        /* If resolved to a local or parameter, it is not 'this' */
        if (checker_find_local(c, name)) return false;
        if (c->current_type_sym) {
            zan_symbol_t *f = checker_find_field(c->current_type_sym, name);
            if (f && !(f->modifiers & MOD_STATIC)) return true;
            zan_symbol_t *m = checker_find_method(c->current_type_sym, name);
            if (m && !(m->modifiers & MOD_STATIC)) return true;
        }
        return false;
    }
    case AST_MEMBER_ACCESS:
        if (node->member.object && node->member.object->kind == AST_THIS_EXPR)
            return true;
        return expr_captures_this(c, node->member.object);
    case AST_CALL:
        if (expr_captures_this(c, node->call.callee)) return true;
        for (int i = 0; i < node->call.args.count; i++) {
            if (expr_captures_this(c, node->call.args.items[i])) return true;
        }
        return false;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        return expr_captures_this(c, node->binary.left) ||
               expr_captures_this(c, node->binary.right);
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        return expr_captures_this(c, node->unary.operand);
    case AST_INDEX:
        if (expr_captures_this(c, node->index.object)) return true;
        if (expr_captures_this(c, node->index.index)) return true;
        for (int i = 0; i < node->index.extra.count; i++) {
            if (expr_captures_this(c, node->index.extra.items[i])) return true;
        }
        return false;
    case AST_CONDITIONAL:
        return expr_captures_this(c, node->conditional.cond) ||
               expr_captures_this(c, node->conditional.then_expr) ||
               expr_captures_this(c, node->conditional.else_expr);
    case AST_NEW_EXPR:
        if (node->new_expr.call_init && expr_captures_this(c, node->new_expr.call_init))
            return true;
        for (int i = 0; i < node->new_expr.args.count; i++) {
            if (expr_captures_this(c, node->new_expr.args.items[i])) return true;
        }
        for (int i = 0; i < node->new_expr.arg_inits.count; i++) {
            if (expr_captures_this(c, node->new_expr.arg_inits.items[i])) return true;
        }
        return false;
    case AST_LAMBDA:
        return expr_captures_this(c, node->lambda.body);
    case AST_BLOCK:
        for (int i = 0; i < node->block.stmts.count; i++) {
            if (expr_captures_this(c, node->block.stmts.items[i])) return true;
        }
        return false;
    case AST_RETURN_STMT:
        return expr_captures_this(c, node->ret.value);
    case AST_IF_STMT:
        return expr_captures_this(c, node->if_stmt.cond) ||
               expr_captures_this(c, node->if_stmt.then_body) ||
               expr_captures_this(c, node->if_stmt.else_body);
    case AST_WHILE_STMT:
        return expr_captures_this(c, node->while_stmt.cond) ||
               expr_captures_this(c, node->while_stmt.body);
    case AST_FOR_STMT:
        return expr_captures_this(c, node->for_stmt.init) ||
               expr_captures_this(c, node->for_stmt.cond) ||
               expr_captures_this(c, node->for_stmt.step) ||
               expr_captures_this(c, node->for_stmt.body);
    case AST_DO_WHILE_STMT:
        return expr_captures_this(c, node->while_stmt.cond) ||
               expr_captures_this(c, node->while_stmt.body);
    case AST_FOREACH_STMT:
        return expr_captures_this(c, node->foreach_stmt.collection) ||
               expr_captures_this(c, node->foreach_stmt.body);
    case AST_TRY_STMT:
        if (expr_captures_this(c, node->try_stmt.try_body)) return true;
        for (int i = 0; i < node->try_stmt.catches.count; i++) {
            zan_ast_node_t *cc = node->try_stmt.catches.items[i];
            if (cc && expr_captures_this(c, cc->catch_clause.body)) return true;
        }
        if (node->try_stmt.finally_body && expr_captures_this(c, node->try_stmt.finally_body)) return true;
        return false;
    case AST_SWITCH_STMT:
        if (expr_captures_this(c, node->switch_stmt.expr)) return true;
        for (int i = 0; i < node->switch_stmt.cases.count; i++) {
            zan_ast_node_t *sc = node->switch_stmt.cases.items[i];
            if (sc) {
                if (expr_captures_this(c, sc->switch_case.pattern)) return true;
                if (expr_captures_this(c, sc->switch_case.when_cond)) return true;
                if (expr_captures_this(c, sc->switch_case.body)) return true;
            }
        }
        return false;
    case AST_THROW_STMT:
        return expr_captures_this(c, node->throw_stmt.value);
    case AST_EXPR_STMT:
        return expr_captures_this(c, node->expr_stmt.expr);
    case AST_CAST_EXPR:
        return expr_captures_this(c, node->cast.expr);
    case AST_AWAIT_EXPR:
        return expr_captures_this(c, node->await_expr.expr);
    case AST_VAR_DECL:
        return expr_captures_this(c, node->var_decl.initializer);
    case AST_TUPLE_EXPR:
        for (int i = 0; i < node->tuple_expr.items.count; i++) {
            if (expr_captures_this(c, node->tuple_expr.items.items[i])) return true;
        }
        return false;
    default:
        return false;
    }
}

static bool has_closure_capturing_this(zan_checker_t *c, zan_ast_node_t *node) {
    if (!node) return false;
    while (node && node->kind == AST_CAST_EXPR) node = node->cast.expr;
    if (!node) return false;
    if (node->kind == AST_LAMBDA) {
        return expr_captures_this(c, node->lambda.body);
    }
    if (node->kind == AST_ASSIGNMENT) {
        return has_closure_capturing_this(c, node->binary.right);
    }
    if (node->kind == AST_CONDITIONAL) {
        return has_closure_capturing_this(c, node->conditional.then_expr) ||
               has_closure_capturing_this(c, node->conditional.else_expr);
    }
    if (node->kind == AST_CALL) {
        for (int i = 0; i < node->call.args.count; i++) {
            if (has_closure_capturing_this(c, node->call.args.items[i])) return true;
        }
    }
    if (node->kind == AST_NEW_EXPR) {
        for (int i = 0; i < node->new_expr.args.count; i++) {
            if (has_closure_capturing_this(c, node->new_expr.args.items[i])) return true;
        }
        for (int i = 0; i < node->new_expr.arg_inits.count; i++) {
            if (has_closure_capturing_this(c, node->new_expr.arg_inits.items[i])) return true;
        }
    }
    return false;
}

static void check_closure_cycle_warning(zan_checker_t *c, zan_ast_node_t *lhs,
                                       zan_ast_node_t *rhs, zan_loc_t loc) {
    if (!c->current_type_sym || !lhs || !rhs) return;
    if (c->current_type_sym->kind != SYM_CLASS) return;

    /* 检查if lhs is a member of 'this', e */
    bool targets_this = false;
    if (lhs->kind == AST_MEMBER_ACCESS) {
        zan_ast_node_t *obj = lhs->member.object;
        while (obj && obj->kind == AST_MEMBER_ACCESS) {
            obj = obj->member.object;
        }
        if (obj && obj->kind == AST_THIS_EXPR) {
            targets_this = true;
        } else if (obj && obj->kind == AST_IDENTIFIER) {
            zan_istr_t name = obj->ident.name;
            if (!checker_find_local(c, name)) {
                zan_symbol_t *f = checker_find_field(c->current_type_sym, name);
                if (f && !(f->modifiers & MOD_STATIC)) {
                    targets_this = true;
                }
            }
        }
    } else if (lhs->kind == AST_IDENTIFIER) {
        zan_istr_t name = lhs->ident.name;
        if (!checker_find_local(c, name)) {
            zan_symbol_t *f = checker_find_field(c->current_type_sym, name);
            if (f && !(f->modifiers & MOD_STATIC)) {
                targets_this = true;
            }
        }
    }

    if (targets_this && has_closure_capturing_this(c, rhs)) {
        zan_diag_emit_code(c->diag, DIAG_WARNING, loc, 2009,
            "closure captures 'this' while assigned to a member of 'this'; "
            "this creates a potential strong reference cycle under ARC");
    }
}


static bool type_satisfies_constraint_depth(zan_type_t *arg, zan_type_t *cons,
                                            int depth) {
    if (!arg || !cons) return true;
    if (depth > CHECKER_DERIVES_MAX_DEPTH) return false;
    if (arg == cons) return true;
    if (arg->sym && cons->sym && arg->sym == cons->sym) return true;
    for (int i = 0; i < arg->interface_count; i++) {
        if (type_satisfies_constraint_depth(arg->interfaces[i], cons, depth + 1))
            return true;
    }
    if (arg->base_type && arg->base_type != arg)
        return type_satisfies_constraint_depth(arg->base_type, cons, depth + 1);
    return false;
}

static bool type_satisfies_constraint(zan_type_t *arg, zan_type_t *cons) {
    return type_satisfies_constraint_depth(arg, cons, 0);
}

static const char *type_name(zan_type_t *t);
static void ctor_arity(zan_ast_node_t *decl, int *min, int *max);

static bool type_param_has_new_constraint(zan_checker_t *c, zan_type_t *tp) {
    if (!tp || tp->kind != TYPE_TYPE_PARAM) return false;
    zan_istr_t name = tp->name;
    if (c->current_method_decl) {
        zan_ast_list_t *mwcs = zan_ast_method_where_clauses(c->current_method_decl);
        if (mwcs) {
            for (int w = 0; w < mwcs->count; w++) {
                zan_ast_node_t *wc = mwcs->items[w];
                if (wc && wc->kind == AST_WHERE_CLAUSE &&
                    wc->where_clause.param_name.len == name.len &&
                    memcmp(wc->where_clause.param_name.str, name.str, (size_t)name.len) == 0) {
                    if (wc->where_clause.has_new) return true;
                }
            }
        }
    }
    if (c->current_type_sym && c->current_type_sym->decl) {
        zan_ast_list_t *twcs = zan_ast_type_where_clauses(c->current_type_sym->decl);
        if (twcs) {
            for (int w = 0; w < twcs->count; w++) {
                zan_ast_node_t *wc = twcs->items[w];
                if (wc && wc->kind == AST_WHERE_CLAUSE &&
                    wc->where_clause.param_name.len == name.len &&
                    memcmp(wc->where_clause.param_name.str, name.str, (size_t)name.len) == 0) {
                    if (wc->where_clause.has_new) return true;
                }
            }
        }
    }
    return false;
}

static bool checker_has_default_ctor(zan_checker_t *c, zan_symbol_t *sym) {
    if (!sym) return false;
    int declared = 0;
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (!m || m->kind != SYM_CONSTRUCTOR || !m->decl) continue;
        if (m->decl->kind != AST_CONSTRUCTOR_DECL) continue;
        declared++;
        int lo = 0, hi = 0;
        ctor_arity(m->decl, &lo, &hi);
        if (0 >= lo && (hi < 0 || 0 <= hi)) {
            if (access_member_allowed(c, m)) return true;
        }
    }
    return declared == 0;
}

static void check_generic_constraints(zan_checker_t *c, zan_type_t *t,
                                      zan_loc_t loc) {
    if (!t || !t->sym || !t->sym->decl) return;
    zan_ast_node_t *d = t->sym->decl;
    if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL &&
        d->kind != AST_INTERFACE_DECL)
        return;
    if (!t->type_args || t->type_arg_count == 0) return;
    if (!d->type_decl.where_clauses) return;
    for (int w = 0; w < d->type_decl.where_clauses->count; w++) {
        zan_ast_node_t *wc = d->type_decl.where_clauses->items[w];
        int idx = -1;
        for (int i = 0; i < d->type_decl.type_params.count; i++) {
            zan_ast_node_t *tp = d->type_decl.type_params.items[i];
            if (tp->ident.name.len == wc->where_clause.param_name.len &&
                memcmp(tp->ident.name.str, wc->where_clause.param_name.str,
                       (size_t)tp->ident.name.len) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0 || idx >= t->type_arg_count) continue;
        zan_type_t *arg = t->type_args[idx];
        if (wc->where_clause.has_new) {
            bool ok = false;
            if (arg) {
                if (arg->kind == TYPE_STRUCT) {
                    ok = true;
                } else if (arg->kind == TYPE_CLASS) {
                    if (arg->sym && !checker_class_is_abstract(arg->sym)) {
                        if (checker_has_default_ctor(c, arg->sym)) ok = true;
                    }
                } else if (arg->kind == TYPE_TYPE_PARAM) {
                    if (type_param_has_new_constraint(c, arg)) ok = true;
                }
            }
            if (!ok) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, loc, 2010,
                    "'%s' must have a public parameterless constructor to satisfy 'new()' constraint for '%.*s' of '%s'",
                    type_name(arg),
                    (int)wc->where_clause.param_name.len,
                    wc->where_clause.param_name.str,
                    type_name(t));
            }
        }
        if (arg && arg->kind == TYPE_TYPE_PARAM) continue;
        for (int k = 0; k < wc->where_clause.constraints.count; k++) {
            zan_ast_node_t *cons_ref = wc->where_clause.constraints.items[k];
            zan_type_t *cons = zan_binder_resolve_type(c->binder, cons_ref);
            if (!cons || cons->kind == TYPE_ERROR) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, loc, 2011,
                    "unknown constraint '%.*s' in 'where %.*s : ...' for '%s'",
                    cons_ref->type_ref.name.len, cons_ref->type_ref.name.str,
                    wc->where_clause.param_name.len,
                    wc->where_clause.param_name.str,
                    type_name(t));
                continue;
            }
            if (!type_satisfies_constraint(arg, cons)) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, loc, 2012,
                    "type '%s' does not satisfy constraint '%s' for type parameter '%.*s' of '%s'",
                    type_name(arg), type_name(cons),
                    wc->where_clause.param_name.len,
                    wc->where_clause.param_name.str,
                    type_name(t));
            }
        }
    }
}


/* 类型名：按形状渲染（`T?` / `T[]` / `List<T>`）。
 *
 * 不能直接返回 t->name.str：可空类型与数组类型的 name 就是元素类型的
 * 名字，`bool?` 的 name 是 "bool"——诊断里会把 `bool?` 说成 `bool`
 * （A463）。渲染交给 binder 的 zan_type_display，全仓只有这一份真相源。
 * 名字里的 mangle（nsresolve 同名冲突改写过的内部名）在这一层拿不到，
 * 那是 A476 的另一条轴（checker_type_simple_name 走 decl 的原始名）。 */
static const char *type_name(zan_type_t *t) {
    if (!t) return "<unknown>";
    int slot = g_type_name_slot;
    g_type_name_slot = (g_type_name_slot + 1) % TYPE_NAME_SLOTS;
    char *buf = g_type_name_pool[slot];
    zan_type_display(t, buf, TYPE_NAME_SLOT_SIZE);
    return buf;
}

static bool checker_weak_target_is_arc_ref(zan_type_t *t) {
    if (!t) return false;
    if (t->kind == TYPE_TYPE_PARAM) return true;
    if (t->kind == TYPE_INTERFACE) return true;
    return t->kind == TYPE_CLASS && t->sym != NULL;
}

static void check_extern_static(zan_checker_t *c, zan_ast_node_t *member) {
    if (!c || !member || member->kind != AST_METHOD_DECL) return;
    bool is_extern = zan_ast_method_extern_lib(member).str != NULL ||
                     (member->method_decl.modifiers & MOD_EXTERN) != 0;
    if (!is_extern) return;
    if (member->method_decl.modifiers & MOD_STATIC) return;
    zan_diag_emit_code(c->diag, DIAG_ERROR, member->loc, 2013,
                  "extern method '%.*s' must be static; a native function has "
                  "no receiver, so drop 'extern' or mark it 'static'",
                  (int)member->method_decl.name.len,
                  member->method_decl.name.str);
}
static void check_weak_member(zan_checker_t *c, zan_ast_node_t *owner,
                              zan_ast_node_t *member) {
    if (!c || !owner || !member ||
        (member->kind != AST_FIELD_DECL && member->kind != AST_PROPERTY_DECL) ||
        !(member->field_decl.modifiers & MOD_WEAK))
        return;

    if (owner->kind == AST_STRUCT_DECL) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, member->loc, 2014,
                      "weak field '%.*s' inside value-type struct '%.*s' "
                      "cannot be tracked safely; move it to a class or remove "
                      "'weak'",
                      (int)member->field_decl.name.len,
                      member->field_decl.name.str,
                      (int)owner->type_decl.name.len,
                      owner->type_decl.name.str);
        return;
    }

    zan_type_t *type = zan_binder_resolve_type(c->binder,
                                                member->field_decl.type);
    if (!type || type == c->binder->type_error) return;
    if (checker_weak_target_is_arc_ref(type)) return;

    zan_diag_emit_code(c->diag, DIAG_ERROR, member->loc, 2015,
                  "weak field '%.*s' has type '%s'; weak requires an "
                  "ARC-managed class or interface reference, so use a "
                  "class/interface type or remove 'weak'",
                  (int)member->field_decl.name.len,
                  member->field_decl.name.str,
                  type_name(type));
}

