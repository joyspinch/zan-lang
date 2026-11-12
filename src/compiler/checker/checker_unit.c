static void check_method_body(zan_checker_t *c, zan_ast_node_t *method) {
    if (!method->method_decl.body) return;

    bool saved_ctor = c->in_ctor;
    c->in_ctor = method->kind == AST_CONSTRUCTOR_DECL;
    if (c->in_ctor && !(method->method_decl.modifiers & MOD_STATIC) &&
        !method->method_decl.has_this_init && !method->method_decl.has_base_init) {
        zan_type_t *bt = c->current_type_sym && c->current_type_sym->type
            ? c->current_type_sym->type->base_type : NULL;
        if (bt && bt->sym && bt->kind == TYPE_CLASS) {
            if (!checker_has_default_ctor(c, bt->sym)) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, method->loc, 3088,
                    "'%.*s' does not contain a constructor that takes 0 arguments",
                    (int)bt->sym->name.len, bt->sym->name.str);
            }
        }
    }
    zan_scope_t *saved_scope = zan_binder_push_type_params(
        c->binder, &method->method_decl.type_params);
    zan_ast_node_t *saved_method = c->current_method_decl;
    c->current_method_decl = method;
    bool saved_nrt = c->in_no_runtime;
    c->in_no_runtime = zan_ast_has_attr(method, "NoRuntime");
    zan_type_t *saved = c->current_return_type;
    c->current_return_type = zan_binder_resolve_type(c->binder,
                                                      method->method_decl.return_type);
    check_generic_constraints(c, c->current_return_type, method->loc);
    struct checker_local *saved_locals = c->locals;
    c->locals = NULL;
    for (int j = 0; j < method->method_decl.params.count; j++) {
        zan_ast_node_t *p = method->method_decl.params.items[j];
        if (p && p->kind == AST_PARAM) {
            zan_type_t *pt = zan_binder_resolve_type(c->binder, p->param.type);
            checker_add_local(c, p->param.name, pt);
        }
    }
    zan_ast_node_t *saved_body = c->current_body;
    c->current_body = method->method_decl.body;
    zan_checker_check_stmt(c, method->method_decl.body);
    zan_definite_check(c->diag, method);
    c->current_body = saved_body;
    c->locals = saved_locals;
    c->current_return_type = saved;
    c->in_no_runtime = saved_nrt;
    c->current_method_decl = saved_method;
    c->binder->current_scope = saved_scope;
    c->in_ctor = saved_ctor;
}

typedef struct {
    zan_ast_node_t **slots;
    int cap;                     /* power of two, 0 = unset */
    int count;
} zan_struct_visited_t;

static size_t struct_visit_hash(zan_ast_node_t *n) {
    uintptr_t h = (uintptr_t)n;
    h ^= h >> 33;
    h *= (uintptr_t)0xff51afd7ed558ccdULL;
    h ^= h >> 29;
    return (size_t)h;
}

static bool struct_visit_seen(const zan_struct_visited_t *v,
                              zan_ast_node_t *n) {
    if (!v->cap) return false;
    size_t mask = (size_t)v->cap - 1;
    for (size_t i = struct_visit_hash(n) & mask; v->slots[i];
         i = (i + 1) & mask) {
        if (v->slots[i] == n) return true;
    }
    return false;
}

static void struct_visit_add(zan_checker_t *c, zan_struct_visited_t *v,
                             zan_ast_node_t *n) {
    if (v->count * 2 + 1 >= v->cap) {
        int ncap = v->cap ? v->cap * 2 : 64;
        zan_ast_node_t **nslots = (zan_ast_node_t **)zan_arena_alloc(
            c->arena, sizeof(zan_ast_node_t *) * (size_t)ncap);
        memset(nslots, 0, sizeof(zan_ast_node_t *) * (size_t)ncap);
        int ocap = v->cap;
        zan_ast_node_t **oslots = v->slots;
        v->slots = nslots;
        v->cap = ncap;
        v->count = 0;
        for (int i = 0; i < ocap; i++) {
            if (!oslots[i]) continue;
            size_t mask = (size_t)ncap - 1;
            size_t j = struct_visit_hash(oslots[i]) & mask;
            while (nslots[j]) j = (j + 1) & mask;
            nslots[j] = oslots[i];
            v->count++;
        }
    }
    size_t mask = (size_t)v->cap - 1;
    size_t i = struct_visit_hash(n) & mask;
    while (v->slots[i]) {
        if (v->slots[i] == n) return;
        i = (i + 1) & mask;
    }
    v->slots[i] = n;
    v->count++;
}

static bool check_struct_cycle_dfs(zan_checker_t *c, zan_ast_node_t *struct_decl,
                                   zan_ast_node_t **stack, int depth,
                                   zan_struct_visited_t *visited) {
    if (!struct_decl || struct_decl->kind != AST_STRUCT_DECL) return false;
    if (depth >= 512) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, struct_decl->loc, 2091,
                      "struct '%.*s' exceeds maximum nesting depth",
                      (int)struct_decl->type_decl.name.len,
                      struct_decl->type_decl.name.str);
        return false;
    }
    if (struct_visit_seen(visited, struct_decl)) return false;
    stack[depth] = struct_decl;

    for (int j = 0; j < struct_decl->type_decl.members.count; j++) {
        zan_ast_node_t *member = struct_decl->type_decl.members.items[j];
        if (!member) continue;
        if (member->kind != AST_FIELD_DECL && member->kind != AST_PROPERTY_DECL) continue;
        if (member->field_decl.modifiers & MOD_STATIC) continue;
        if (member->kind == AST_PROPERTY_DECL &&
            (member->field_decl.getter_body || member->field_decl.setter_body)) continue;

        zan_type_t *ftype = zan_binder_resolve_type(c->binder, member->field_decl.type);
        if (!ftype || ftype->kind != TYPE_STRUCT) continue;
        zan_symbol_t *fsym = ftype->sym;
        if (!fsym && c->binder) fsym = zan_binder_lookup(c->binder, ftype->name);
        if (fsym && fsym->decl && fsym->decl->kind == AST_STRUCT_DECL) {
            for (int k = 0; k <= depth; k++) {
                if (stack[k] == fsym->decl) {
                    zan_diag_emit_code(c->diag, DIAG_ERROR, member->loc, 2092,
                                  "struct member '%.*s' of type '%.*s' causes a cycle in the struct layout (recursive struct has infinite size)",
                                  (int)member->field_decl.name.len,
                                  member->field_decl.name.str,
                                  (int)fsym->decl->type_decl.name.len,
                                  fsym->decl->type_decl.name.str);
                    return true;
                }
            }
            if (check_struct_cycle_dfs(c, fsym->decl, stack, depth + 1,
                                       visited)) {
                return true;
            }
        }
    }
    struct_visit_add(c, visited, struct_decl);
    return false;
}

static void check_all_struct_cycles(zan_checker_t *c, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    zan_struct_visited_t visited = { NULL, 0, 0 };
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl && decl->kind == AST_STRUCT_DECL) {
            zan_ast_node_t *stack[512];
            check_struct_cycle_dfs(c, decl, stack, 0, &visited);
        }
    }
}


static int checker_method_param_count(zan_symbol_t *m) {
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) return -1;
    return m->decl->method_decl.params.count;
}

static bool checker_base_has_virtual_slot(zan_symbol_t *base_sym,
                                          zan_istr_t name, int arity,
                                          int depth) {
    if (!base_sym || depth > CHECKER_DERIVES_MAX_DEPTH) return false;
    for (int i = 0; i < base_sym->member_count; i++) {
        zan_symbol_t *m = base_sym->members[i];
        if (m && m->kind == SYM_METHOD &&
            (m->modifiers & (MOD_VIRTUAL | MOD_OVERRIDE | MOD_ABSTRACT)) &&
            m->name.len == name.len &&
            memcmp(m->name.str, name.str, (size_t)name.len) == 0 &&
            checker_method_param_count(m) == arity)
            return true;
    }
    return checker_base_has_virtual_slot(
        (base_sym->type && base_sym->type->base_type)
            ? base_sym->type->base_type->sym : NULL,
        name, arity, depth + 1);
}

/* 抽象类判定：声明带 abstract，或含抽象方法。
 * 抽象类不能被实例化——它的抽象方法没有实现，new 出来的对象调下去
 * 只会撞上编译器发的「未被覆盖」致命桩。 */
static bool checker_class_is_abstract(zan_symbol_t *sym) {
    if (!sym) return false;
    if (sym->decl && sym->decl->kind == AST_CLASS_DECL &&
        (sym->decl->type_decl.modifiers & MOD_ABSTRACT))
        return true;
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (m && m->kind == SYM_METHOD && (m->modifiers & MOD_ABSTRACT))
            return true;
    }
    return false;
}

/* 本类（含继承来的）是否覆盖了某个抽象方法（按名字 + 参数个数匹配）。 */
static bool checker_class_overrides(zan_symbol_t *sym, zan_istr_t name,
                                    int arity, int depth) {
    if (!sym || depth > 16) return false;
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (!m || m->kind != SYM_METHOD) continue;
        if (!(m->modifiers & MOD_OVERRIDE)) continue;
        if (m->name.len == name.len &&
            memcmp(m->name.str, name.str, (size_t)name.len) == 0 &&
            checker_method_param_count(m) == arity)
            return true;
    }
    if (sym->type && sym->type->base_type && sym->type->base_type->sym)
        return checker_class_overrides(sym->type->base_type->sym, name, arity,
                                       depth + 1);
    return false;
}

/* 收集继承链上声明的全部抽象方法（连同声明它们的基类）。
 * 一次性收集完再判定：逐层递归判定会把「中间那层抽象类」误当成
 * 被检查对象——中间层本来就可以不实现。 */
typedef struct {
    zan_symbol_t *owner;   /* 声明该抽象方法的基类 */
    zan_symbol_t *method;
} checker_abstract_item_t;

static void collect_base_abstract_methods(zan_symbol_t *sym,
                                          checker_abstract_item_t *out,
                                          int *n, int cap, int depth) {
    if (!sym || depth > 16) return;
    zan_symbol_t *base = (sym->type && sym->type->base_type)
        ? sym->type->base_type->sym : NULL;
    if (!base) return;
    for (int i = 0; i < base->member_count; i++) {
        zan_symbol_t *m = base->members[i];
        if (!m || m->kind != SYM_METHOD) continue;
        if (!(m->modifiers & MOD_ABSTRACT)) continue;
        if (*n < cap) { out[*n].owner = base; out[*n].method = m; (*n)++; }
    }
    collect_base_abstract_methods(base, out, n, cap, depth + 1);
}

/* 收集继承链上尚未被本类覆盖的抽象方法，逐条报错。
 * 只看「继承来的」抽象方法：本类自己声明的抽象方法由「本类必须也是
 * abstract」那条规则管。 */
static void check_abstract_implementations(zan_checker_t *c,
                                           zan_symbol_t *sym) {
    if (!sym) return;
    checker_abstract_item_t items[128];
    int n = 0;
    collect_base_abstract_methods(sym, items, &n, 128, 0);
    for (int i = 0; i < n; i++) {
        zan_symbol_t *m = items[i].method;
        zan_symbol_t *owner = items[i].owner;
        if (checker_class_overrides(sym, m->name, checker_method_param_count(m), 0))
            continue;
        zan_diag_emit_code(c->diag, DIAG_ERROR,
            m->decl ? m->decl->loc : (zan_loc_t){0, 0, 0}, 2093,
            "non-abstract class '%.*s' does not implement abstract method "
            "'%.*s' inherited from '%.*s'",
            (int)sym->name.len, sym->name.str,
            (int)m->name.len, m->name.str,
            (int)owner->name.len, owner->name.str);
    }
}

/* 抽象成员的规则：抽象方法不能是 static / async；含抽象方法的类必须
 * 声明为 abstract；具体类必须实现继承来的全部抽象方法。 */
static void check_abstract_members(zan_checker_t *c, zan_ast_node_t *decl) {
    zan_symbol_t *ts = c->current_type_sym;
    if (!ts) return;
    /* 类是否 abstract 以声明上的修饰符为准（符号表里的那份不总是同一个）。 */
    bool cls_abstract = (decl->type_decl.modifiers & MOD_ABSTRACT) != 0;
    bool has_abstract_method = false;
    for (int j = 0; j < decl->type_decl.members.count; j++) {
        zan_ast_node_t *member = decl->type_decl.members.items[j];
        if (member->kind != AST_METHOD_DECL) continue;
        if (!(member->method_decl.modifiers & MOD_ABSTRACT)) continue;
        has_abstract_method = true;
        if (member->method_decl.modifiers & MOD_STATIC) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, member->loc, 2094,
                "an abstract method cannot be static: '%.*s'",
                (int)member->method_decl.name.len, member->method_decl.name.str);
        }
        if (member->method_decl.modifiers & MOD_ASYNC) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, member->loc, 2095,
                "an abstract method cannot be async: '%.*s'",
                (int)member->method_decl.name.len, member->method_decl.name.str);
        }
    }
    if (has_abstract_method && !cls_abstract) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, decl->loc, 2096,
            "class '%.*s' declares an abstract method and must be declared "
            "'abstract'", (int)ts->name.len, ts->name.str);
    }
    if (!cls_abstract)
        check_abstract_implementations(c, ts);
}

static void check_virtual_shadows_base(zan_checker_t *c, zan_ast_node_t *decl) {
    zan_symbol_t *ts = c->current_type_sym;
    if (!ts || !ts->type || !ts->type->base_type || !ts->type->base_type->sym)
        return;
    for (int j = 0; j < decl->type_decl.members.count; j++) {
        zan_ast_node_t *member = decl->type_decl.members.items[j];
        if (member->kind != AST_METHOD_DECL) continue;
        int want = member->method_decl.params.count;
        zan_symbol_t *sym = NULL;
        for (int i = 0; i < ts->member_count; i++) {
            zan_symbol_t *m = ts->members[i];
            if (m && m->kind == SYM_METHOD &&
                m->name.len == member->method_decl.name.len &&
                memcmp(m->name.str, member->method_decl.name.str,
                       (size_t)member->method_decl.name.len) == 0 &&
                checker_method_param_count(m) == want)
                sym = m;
        }
        if (!sym || !(sym->modifiers & MOD_VIRTUAL) ||
            (sym->modifiers & MOD_OVERRIDE))
            continue;
        if (checker_base_has_virtual_slot(ts->type->base_type->sym,
                                          sym->name, want, 0)) {
            zan_diag_emit_code(c->diag, DIAG_WARNING, member->loc, 2097,
                "'%.*s' is declared 'virtual' but hides an inherited virtual "
                "with the same name and arity; calls through base-class "
                "references will run the base implementation -- "
                "use 'override' to replace it",
                (int)member->method_decl.name.len,
                member->method_decl.name.str);
        }
    }
}

/* ---------- SyncStructureAll(Async) 的编译期实体发现 ----------
 *
 * "自动发现自动建表"：`db.SyncStructureAllAsync()` 零参调用不查运行时
 * 注册表——checker 在编译单元里预扫描 [Table] 类，在调用点原地合成
 * `new Meta[] { Meta.For<E1>(), ... }` 实参，交给库侧逐实体 Sync。
 * 无运行时 ABI、无生成器，实体清单与编译单元永远一致。 */

static void checker_collect_table_entities(zan_checker_t *c,
                                           zan_ast_node_t *unit) {
    if (c->table_ents_done || !unit || unit->kind != AST_COMPILATION_UNIT) {
        return;
    }
    c->table_ents_done = true;
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL || !decl->type_decl.name.str) continue;
        if (!zan_ast_has_attr(decl, "Table")) continue;
        int dup = 0;
        for (int k = 0; k < c->table_ent_count; k++) {
            if (c->table_ents[k].len == decl->type_decl.name.len &&
                memcmp(c->table_ents[k].str, decl->type_decl.name.str,
                       (size_t)decl->type_decl.name.len) == 0) { dup = 1; break; }
        }
        if (dup) continue;
        if (c->table_ent_count >= c->table_ent_cap) {
            int ncap = c->table_ent_cap ? c->table_ent_cap * 2 : 16;
            zan_istr_t *nm = (zan_istr_t *)zan_arena_alloc(
                c->arena, (size_t)ncap * sizeof(zan_istr_t));
            for (int k = 0; k < c->table_ent_count; k++) nm[k] = c->table_ents[k];
            c->table_ents = nm;
            c->table_ent_cap = ncap;
        }
        c->table_ents[c->table_ent_count++] = decl->type_decl.name;
    }
}

/* 接收者能否充当同步连接：本身是（或实现）IDbConnection/IDbExecutor。 */
static bool checker_recv_is_conn(zan_checker_t *c, zan_type_t *recv) {
    if (!recv || !recv->sym) return false;
    if (recv->kind != TYPE_CLASS && recv->kind != TYPE_STRUCT &&
        recv->kind != TYPE_INTERFACE) return false;
    zan_symbol_t *isym = zan_binder_lookup(
        c->binder, checker_istr_lit(c->arena, "IDbConnection"));
    zan_symbol_t *xsym = zan_binder_lookup(
        c->binder, checker_istr_lit(c->arena, "IDbExecutor"));
    if (isym && recv->sym == isym) return true;
    if (xsym && recv->sym == xsym) return true;
    if (recv->kind != TYPE_INTERFACE &&
        (recv->kind == TYPE_CLASS || recv->kind == TYPE_STRUCT)) {
        if (isym && zan_binder_class_implements_iface(recv->sym, isym)) return true;
        if (xsym && zan_binder_class_implements_iface(recv->sym, xsym)) return true;
    }
    return false;
}

/* new Meta[] { Meta.For<E1>(), ... }：数组初始化式，元素为泛型静态
 * 调用（与 ZanSql.FromConnection 同款合成形状，多 type_args）。 */
static zan_ast_node_t *checker_synthesize_table_array(zan_checker_t *c,
                                                      zan_loc_t loc) {
    zan_ast_node_t *tref = zan_ast_new(c->arena, AST_TYPE_REF, loc);
    tref->type_ref.name = checker_istr_lit(c->arena, "Meta");
    tref->type_ref.is_nullable = false;
    tref->type_ref.is_array = true;
    zan_ast_list_init(&tref->type_ref.type_args);
    zan_ast_node_t *n = zan_ast_new(c->arena, AST_NEW_EXPR, loc);
    n->new_expr.type = tref;
    n->new_expr.call_init = NULL;
    zan_ast_list_init(&n->new_expr.args);
    zan_ast_list_init(&n->new_expr.arg_inits);
    n->new_expr.is_array = true;
    n->new_expr.array_init = true;
    n->new_expr.array_rank = 0;
    n->new_expr.list_copy = false;
    for (int i = 0; i < c->table_ent_count; i++) {
        zan_ast_node_t *zid = zan_ast_new(c->arena, AST_IDENTIFIER, loc);
        zid->ident.name = checker_istr_lit(c->arena, "Meta");
        zid->ident.inst_type_ref = NULL;
        zan_ast_node_t *cal = zan_ast_new(c->arena, AST_MEMBER_ACCESS, loc);
        cal->member.object = zid;
        cal->member.name = checker_istr_lit(c->arena, "For");
        cal->member.null_cond = 0;
        zan_ast_node_t *call = zan_ast_new(c->arena, AST_CALL, loc);
        call->call.callee = cal;
        zan_ast_list_init(&call->call.args);
        zan_ast_list_init(&call->call.type_args);
        zan_ast_list_push(&call->call.type_args,
                          checker_type_ref(c, c->table_ents[i], NULL, loc),
                          c->arena);
        zan_ast_list_push(&n->new_expr.args, call, c->arena);
    }
    return n;
}

/* ---------- __AttrRoutes.Register 的编译期控制器发现 ----------
 *
 * 预扫描 compilation unit 里的 Controller 类，在 __AttrRoutes.Register(app)
 * 调用点原地合成 `new TypeInfo[] { typeof(C1), typeof(C2), ... }` 实参，
 * 由框架动态约定路由引擎遍历计算并载入共享内存表。 */

static void checker_collect_controllers(zan_checker_t *c,
                                        zan_ast_node_t *unit) {
    if (c->ctrl_ents_done || !unit || unit->kind != AST_COMPILATION_UNIT) {
        return;
    }
    c->ctrl_ents_done = true;
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL || !decl->type_decl.name.str) continue;
        if (decl->type_decl.modifiers & MOD_ABSTRACT) continue;
        if (decl->type_decl.type_params.count > 0) continue;

        /* B-ID110：库侧（stdlib / packages）自带控制器不参与隐式发现——
         * 否则 Zan.Mvc 拉入编译集的 200+ 框架控制器被用户项目的
         * __AttrRoutes.Register 全量吞掉；框架页归 Boot/显式注册管。
         *
         * 判定按**来源**而不是命名空间：模板/示例项目（用户自己的代码）
         * 也统一使用 Zan.Web.* 命名空间，按命名空间跳过会把项目自己的
         * 控制器一并吞掉（server-legend 全体 API 404 就是这么来的）。 */
        if (decl->from_stdlib || decl->from_package ||
            zan_routes_framework_controller(unit, decl)) continue;

        /* 排除框架基类本身 */
        uint32_t nl = decl->type_decl.name.len;
        const char *ns = decl->type_decl.name.str;
        if (nl == 10 && memcmp(ns, "Controller", 10) == 0) continue;
        if (nl == 13 && memcmp(ns, "AppController", 13) == 0) continue;
        if (nl == 15 && memcmp(ns, "AdminController", 15) == 0) continue;
        if (nl == 13 && memcmp(ns, "ApiController", 13) == 0) continue;
        if (nl == 20 && memcmp(ns, "CrudScreenController", 20) == 0) continue;

        /* 判定是否为控制器：以 Controller 结尾，或带有 [Route]/[ApiController]，或继承自 Controller 家族 */
        bool is_ctrl = false;
        if (nl >= 10 && memcmp(ns + nl - 10, "Controller", 10) == 0) {
            is_ctrl = true;
        } else if (zan_ast_has_attr(decl, "Route") || zan_ast_has_attr(decl, "ApiController")) {
            is_ctrl = true;
        } else {
            for (int b = 0; b < decl->type_decl.bases.count; b++) {
                zan_ast_node_t *base = decl->type_decl.bases.items[b];
                if (base && base->kind == AST_TYPE_REF && base->type_ref.name.str) {
                    uint32_t bl = base->type_ref.name.len;
                    const char *bs = base->type_ref.name.str;
                    if (bl >= 10 && memcmp(bs + bl - 10, "Controller", 10) == 0) {
                        is_ctrl = true;
                        break;
                    }
                }
            }
        }
        if (!is_ctrl) continue;

        int dup = 0;
        for (int k = 0; k < c->ctrl_ent_count; k++) {
            if (c->ctrl_ents[k].len == decl->type_decl.name.len &&
                memcmp(c->ctrl_ents[k].str, decl->type_decl.name.str,
                       (size_t)decl->type_decl.name.len) == 0) { dup = 1; break; }
        }
        if (dup) continue;
        if (c->ctrl_ent_count >= c->ctrl_ent_cap) {
            int ncap = c->ctrl_ent_cap ? c->ctrl_ent_cap * 2 : 32;
            zan_istr_t *nm = (zan_istr_t *)zan_arena_alloc(
                c->arena, (size_t)ncap * sizeof(zan_istr_t));
            for (int k = 0; k < c->ctrl_ent_count; k++) nm[k] = c->ctrl_ents[k];
            c->ctrl_ents = nm;
            c->ctrl_ent_cap = ncap;
        }
        c->ctrl_ents[c->ctrl_ent_count++] = decl->type_decl.name;
    }
}

/* __ZanActionDispatch.Dispatch：main.c 为含 __AttrRoutes.Register 的编译单元
 * 合成动作派发类；这里把它的方法组作为 Register 的第三实参传进去。 */
static zan_ast_node_t *checker_synthesize_dispatcher_ref(zan_checker_t *c,
                                                         zan_loc_t loc,
                                                         bool framework) {
    zan_ast_node_t *id = zan_ast_new(c->arena, AST_IDENTIFIER, loc);
    id->ident.name = checker_istr_lit(c->arena, "__ZanActionDispatch");
    id->ident.inst_type_ref = NULL;
    zan_ast_node_t *m = zan_ast_new(c->arena, AST_MEMBER_ACCESS, loc);
    m->member.object = id;
    m->member.name = checker_istr_lit(c->arena,
                                    framework ? "DispatchFramework" : "Dispatch");
    m->member.null_cond = 0;
    return m;
}

/* new TypeInfo[] { typeof(C1), typeof(C2), ... } */
static zan_ast_node_t *checker_synthesize_controllers_array(zan_checker_t *c,
                                                            zan_loc_t loc) {
    zan_ast_node_t *tref = zan_ast_new(c->arena, AST_TYPE_REF, loc);
    tref->type_ref.name = checker_istr_lit(c->arena, "TypeInfo");
    tref->type_ref.is_nullable = false;
    tref->type_ref.is_array = true;
    zan_ast_list_init(&tref->type_ref.type_args);
    zan_ast_node_t *n = zan_ast_new(c->arena, AST_NEW_EXPR, loc);
    n->new_expr.type = tref;
    n->new_expr.call_init = NULL;
    zan_ast_list_init(&n->new_expr.args);
    zan_ast_list_init(&n->new_expr.arg_inits);
    n->new_expr.is_array = true;
    n->new_expr.array_init = true;
    n->new_expr.array_rank = 0;
    n->new_expr.list_copy = false;
    for (int i = 0; i < c->ctrl_ent_count; i++) {
        zan_ast_node_t *tof = zan_ast_new(c->arena, AST_TYPEOF_EXPR, loc);
        tof->cast.type = checker_type_ref(c, c->ctrl_ents[i], NULL, loc);
        zan_ast_list_push(&n->new_expr.args, tof, c->arena);
    }
    return n;
}

void zan_checker_check(zan_checker_t *c, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;

    checker_collect_table_entities(c, unit);
    checker_collect_controllers(c, unit);
    check_all_struct_cycles(c, unit);

    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL &&
            decl->kind != AST_INTERFACE_DECL && decl->kind != AST_ENUM_DECL) {
            continue;
        }

        c->current_type_sym = zan_binder_lookup(c->binder, decl->type_decl.name);
        zan_scope_t *saved_scope = zan_binder_push_type_params(
            c->binder, &decl->type_decl.type_params);
        if (decl->kind == AST_CLASS_DECL) {
            check_virtual_shadows_base(c, decl);
            check_abstract_members(c, decl);
        }
        for (int j = 0; j < decl->type_decl.members.count; j++) {
            zan_ast_node_t *member = decl->type_decl.members.items[j];
            check_weak_member(c, decl, member);
            check_extern_static(c, member);
            if (member->kind == AST_METHOD_DECL ||
                member->kind == AST_CONSTRUCTOR_DECL ||
                member->kind == AST_DESTRUCTOR_DECL) {
                zan_compile_trace("check %.*s.%.*s",
                                (int)decl->type_decl.name.len,
                                decl->type_decl.name.str,
                                (int)member->method_decl.name.len,
                                member->method_decl.name.str);
                check_method_body(c, member);
            }
        }
        c->binder->current_scope = saved_scope;
    }
}
