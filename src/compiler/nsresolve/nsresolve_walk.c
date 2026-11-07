static void collect_shadows(nr_ctx_t *c, zan_ast_node_t *n);

static void collect_shadows_list(nr_ctx_t *c, zan_ast_list_t *l) {
    if (!l) return;
    for (int i = 0; i < l->count; i++) collect_shadows(c, l->items[i]);
}

static void collect_shadows(nr_ctx_t *c, zan_ast_node_t *n) {
    if (!n) return;
    switch (n->kind) {
    case AST_PARAM:
        shadow_add(&c->shadow, n->param.name);
        collect_shadows(c, n->param.default_val);
        break;
    case AST_VAR_DECL:
        shadow_add(&c->shadow, n->var_decl.name);
        collect_shadows(c, n->var_decl.initializer);
        break;
    case AST_FOREACH_STMT:
        shadow_add(&c->shadow, n->foreach_stmt.var_name);
        collect_shadows(c, n->foreach_stmt.collection);
        collect_shadows(c, n->foreach_stmt.body);
        break;
    case AST_CATCH_CLAUSE:
        shadow_add(&c->shadow, n->catch_clause.var_name);
        collect_shadows(c, n->catch_clause.body);
        break;
    case AST_LAMBDA:
        collect_shadows_list(c, &n->lambda.params);
        collect_shadows(c, n->lambda.body);
        break;
    case AST_BLOCK:
        collect_shadows_list(c, &n->block.stmts);
        break;
    case AST_EXPR_STMT:  collect_shadows(c, n->expr_stmt.expr); break;
    case AST_RETURN_STMT: collect_shadows(c, n->ret.value); break;
    case AST_IF_STMT:
        collect_shadows(c, n->if_stmt.cond);
        collect_shadows(c, n->if_stmt.then_body);
        collect_shadows(c, n->if_stmt.else_body);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        collect_shadows(c, n->while_stmt.cond);
        collect_shadows(c, n->while_stmt.body);
        break;
    case AST_FOR_STMT:
        collect_shadows(c, n->for_stmt.init);
        collect_shadows(c, n->for_stmt.cond);
        collect_shadows(c, n->for_stmt.step);
        collect_shadows(c, n->for_stmt.body);
        break;
    case AST_TRY_STMT:
        collect_shadows(c, n->try_stmt.try_body);
        collect_shadows_list(c, &n->try_stmt.catches);
        collect_shadows(c, n->try_stmt.finally_body);
        break;
    case AST_SWITCH_STMT:
        collect_shadows(c, n->switch_stmt.expr);
        collect_shadows_list(c, &n->switch_stmt.cases);
        break;
    case AST_SWITCH_CASE:
        collect_shadows(c, n->switch_case.body);
        break;
    case AST_LOCK_STMT:
        collect_shadows(c, n->lock_stmt.expr);
        collect_shadows(c, n->lock_stmt.body);
        break;
    case AST_CHECKED_STMT:
        collect_shadows(c, n->checked_stmt.body);
        break;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        collect_shadows(c, n->binary.left);
        collect_shadows(c, n->binary.right);
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        collect_shadows(c, n->unary.operand);
        break;
    case AST_CALL:
        collect_shadows(c, n->call.callee);
        collect_shadows_list(c, &n->call.args);
        break;
    case AST_MEMBER_ACCESS:
        collect_shadows(c, n->member.object);
        break;
    case AST_INDEX:
        collect_shadows(c, n->index.object);
        collect_shadows(c, n->index.index);
        break;
    case AST_NEW_EXPR:
        collect_shadows_list(c, &n->new_expr.args);
        collect_shadows_list(c, &n->new_expr.arg_inits);
        break;
    case AST_CONDITIONAL:
        collect_shadows(c, n->conditional.cond);
        collect_shadows(c, n->conditional.then_expr);
        collect_shadows(c, n->conditional.else_expr);
        break;
    case AST_CAST_EXPR:
    case AST_TYPEOF_EXPR:
    case AST_SIZEOF_EXPR:
        collect_shadows(c, n->cast.expr);
        break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        collect_shadows(c, n->type_test.expr);
        break;
    case AST_AWAIT_EXPR:
        collect_shadows(c, n->await_expr.expr);
        break;
    case AST_STRING_INTERP:
        collect_shadows_list(c, &n->string_interp.parts);
        break;
    case AST_REF_ARG:
        collect_shadows(c, n->ref_arg.expr);
        break;
    case AST_NAMED_ARG:
        collect_shadows(c, n->named_arg.expr);
        break;
    case AST_COLL_INIT:
        collect_shadows_list(c, &n->coll_init.items);
        break;
    case AST_THROW_STMT:
        collect_shadows(c, n->throw_stmt.value);
        break;
    case AST_YIELD_STMT:
        collect_shadows(c, n->yield_stmt.value);
        break;
    case AST_TUPLE_EXPR:
        collect_shadows_list(c, &n->tuple_expr.items);
        break;
    case AST_TUPLE_DECON:
        /* 解构声明本身就是局部影子名（names 为 IDENTIFIER 节点） */
        for (int i = 0; i < n->tuple_decon.names.count; i++)
            shadow_add(&c->shadow, n->tuple_decon.names.items[i]->ident.name);
        collect_shadows(c, n->tuple_decon.initializer);
        break;
    case AST_WITH_EXPR:
        collect_shadows(c, n->with_expr.expr);
        collect_shadows_list(c, &n->with_expr.assigns);
        break;
    case AST_SWITCH_EXPR:
        collect_shadows(c, n->switch_expr.expr);
        collect_shadows_list(c, &n->switch_expr.arms);
        break;
    case AST_SWITCH_ARM:
        collect_shadows(c, n->switch_arm.when_cond);
        collect_shadows(c, n->switch_arm.result);
        break;
    case AST_IDENTIFIER:
        /* inst_type_ref 是类型引用，不含局部影子名——显式 no-op 而非落 default */
        break;
    default: {
        /* B-ID102 覆盖审计：有非 NULL 子节点的 kind 落到 default 即疑似漏改
         * （ZANC_TRACE 门控）。 */
        int nc = zan_ast_child_count(n), live = 0;
        for (int i = 0; i < nc; i++) if (zan_ast_child_at(n, i)) live++;
        if (live > 0)
            zan_compile_trace("nsresolve.collect_shadows: kind %d has %d live children but no case",
                              (int)n->kind, live);
        break;
    }
    }
}

static void nr_walk(nr_ctx_t *c, zan_ast_node_t *n,
                    zan_istr_t ns, zan_ast_list_t *usings);

static void nr_walk_list(nr_ctx_t *c, zan_ast_list_t *l,
                         zan_istr_t ns, zan_ast_list_t *usings) {
    if (!l) return;
    for (int i = 0; i < l->count; i++)
        nr_walk(c, l->items[i], ns, usings);
}

static void nr_walk(nr_ctx_t *c, zan_ast_node_t *n,
                    zan_istr_t ns, zan_ast_list_t *usings) {
    if (!n) return;
    if (zan_ast_ns_usings(n)) {
        ns = zan_ast_ns_name(n);
        usings = zan_ast_ns_usings(n);
    }
    switch (n->kind) {
    case AST_TYPE_REF:
        resolve_ref(c, n, ns, usings);
        nr_walk_list(c, &n->type_ref.type_args, ns, usings);
        /* 数组的元素类型是**另一个**类型节点（解析器把元素包进一个合成的
         * AST_TYPE_REF，元素本体挂在 array_element 上）。不下钻的话，元素
         * 节点的名字从没被改写过——限定名（`Outer.Inner`）就保持原样，绑定期
         * 的 scope_find 查不到 → "undefined type 'Outer.Inner'"。
         * 元素节点与包装节点同名，"ambiguous type" 只由包装节点报一次。 */
        if (n->type_ref.array_element) {
            bool saved = c->quiet_ambiguity;
            c->quiet_ambiguity = true;
            nr_walk(c, n->type_ref.array_element, ns, usings);
            c->quiet_ambiguity = saved;
        }
        break;

    case AST_CLASS_DECL:
    case AST_STRUCT_DECL:
    case AST_INTERFACE_DECL:
    case AST_ENUM_DECL: {
        int saved_fields = c->fields.count;
        for (int i = 0; i < n->type_decl.members.count; i++) {
            zan_ast_node_t *m = n->type_decl.members.items[i];
            if (m && (m->kind == AST_FIELD_DECL || m->kind == AST_PROPERTY_DECL))
                shadow_add(&c->fields, m->field_decl.name);
        }
        nr_walk_list(c, &n->type_decl.bases, ns, usings);
        if (n->type_decl.where_clauses)
            nr_walk_list(c, n->type_decl.where_clauses, ns, usings);
        nr_walk_list(c, &n->type_decl.members, ns, usings);
        c->fields.count = saved_fields;
        break;
    }

    case AST_DELEGATE_DECL:
    case AST_METHOD_DECL:
    case AST_CONSTRUCTOR_DECL:
    case AST_DESTRUCTOR_DECL:
        c->shadow.count = 0;
        for (int i = 0; i < c->fields.count; i++)
            shadow_add(&c->shadow, c->fields.items[i]);
        collect_shadows_list(c, &n->method_decl.params);
        collect_shadows(c, n->method_decl.body);
        nr_walk(c, n->method_decl.return_type, ns, usings);
        nr_walk_list(c, &n->method_decl.params, ns, usings);
        if (n->method_decl.ext) {
            nr_walk_list(c, &n->method_decl.ext->where_clauses, ns, usings);
            nr_walk_list(c, &n->method_decl.ext->base_args, ns, usings);
        }
        nr_walk(c, n->method_decl.body, ns, usings);
        break;

    case AST_FIELD_DECL:
    case AST_PROPERTY_DECL:
        nr_walk(c, n->field_decl.type, ns, usings);
        nr_walk(c, n->field_decl.initializer, ns, usings);
        break;

    case AST_PARAM:
        nr_walk(c, n->param.type, ns, usings);
        nr_walk(c, n->param.default_val, ns, usings);
        break;

    case AST_WHERE_CLAUSE:
        nr_walk_list(c, &n->where_clause.constraints, ns, usings);
        break;

    case AST_BLOCK:
        nr_walk_list(c, &n->block.stmts, ns, usings);
        break;

    case AST_VAR_DECL:
        nr_walk(c, n->var_decl.type, ns, usings);
        nr_walk(c, n->var_decl.initializer, ns, usings);
        break;

    case AST_EXPR_STMT:
        nr_walk(c, n->expr_stmt.expr, ns, usings);
        break;

    case AST_RETURN_STMT:
        nr_walk(c, n->ret.value, ns, usings);
        break;

    case AST_IF_STMT:
        nr_walk(c, n->if_stmt.cond, ns, usings);
        nr_walk(c, n->if_stmt.then_body, ns, usings);
        nr_walk(c, n->if_stmt.else_body, ns, usings);
        break;

    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        nr_walk(c, n->while_stmt.cond, ns, usings);
        nr_walk(c, n->while_stmt.body, ns, usings);
        break;

    case AST_FOR_STMT:
        nr_walk(c, n->for_stmt.init, ns, usings);
        nr_walk(c, n->for_stmt.cond, ns, usings);
        nr_walk(c, n->for_stmt.step, ns, usings);
        nr_walk(c, n->for_stmt.body, ns, usings);
        break;

    case AST_FOREACH_STMT:
        nr_walk(c, n->foreach_stmt.var_type, ns, usings);
        nr_walk(c, n->foreach_stmt.collection, ns, usings);
        nr_walk(c, n->foreach_stmt.body, ns, usings);
        break;

    case AST_THROW_STMT:
        nr_walk(c, n->throw_stmt.value, ns, usings);
        break;

    case AST_TRY_STMT:
        nr_walk(c, n->try_stmt.try_body, ns, usings);
        nr_walk_list(c, &n->try_stmt.catches, ns, usings);
        nr_walk(c, n->try_stmt.finally_body, ns, usings);
        break;

    case AST_CATCH_CLAUSE:
        nr_walk(c, n->catch_clause.type, ns, usings);
        nr_walk(c, n->catch_clause.body, ns, usings);
        break;

    case AST_SWITCH_STMT:
        nr_walk(c, n->switch_stmt.expr, ns, usings);
        nr_walk_list(c, &n->switch_stmt.cases, ns, usings);
        break;

    case AST_SWITCH_CASE:
        nr_walk(c, n->switch_case.pattern, ns, usings);
        nr_walk(c, n->switch_case.body, ns, usings);
        break;

    case AST_LOCK_STMT:
        nr_walk(c, n->lock_stmt.expr, ns, usings);
        nr_walk(c, n->lock_stmt.body, ns, usings);
        break;

    case AST_CHECKED_STMT:
        nr_walk(c, n->checked_stmt.body, ns, usings);
        break;

    case AST_YIELD_STMT:
        nr_walk(c, n->yield_stmt.value, ns, usings);
        break;

    case AST_BINARY:
    case AST_ASSIGNMENT:
        nr_walk(c, n->binary.left, ns, usings);
        nr_walk(c, n->binary.right, ns, usings);
        break;

    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        nr_walk(c, n->unary.operand, ns, usings);
        break;

    case AST_CALL:
        nr_walk(c, n->call.callee, ns, usings);
        nr_walk_list(c, &n->call.type_args, ns, usings);
        nr_walk_list(c, &n->call.args, ns, usings);
        break;

    case AST_MEMBER_ACCESS:
        if (n->member.object && n->member.object->kind == AST_IDENTIFIER)
            resolve_static_receiver(c, n->member.object, ns, usings,
                                    n->member.name);
        else if (n->member.object && n->member.object->kind == AST_MEMBER_ACCESS)
            resolve_qualified_receiver(c, n->member.object);
        nr_walk(c, n->member.object, ns, usings);
        break;

    case AST_INDEX:
        nr_walk(c, n->index.object, ns, usings);
        nr_walk(c, n->index.index, ns, usings);
        break;

    case AST_NEW_EXPR:
        nr_walk(c, n->new_expr.type, ns, usings);
        nr_walk_list(c, &n->new_expr.args, ns, usings);
        nr_walk_list(c, &n->new_expr.arg_inits, ns, usings);
        break;

    case AST_CAST_EXPR:
    case AST_TYPEOF_EXPR:
    case AST_SIZEOF_EXPR:
        nr_walk(c, n->cast.type, ns, usings);
        nr_walk(c, n->cast.expr, ns, usings);
        break;

    case AST_IS_EXPR:
    case AST_AS_EXPR:
        nr_walk(c, n->type_test.expr, ns, usings);
        nr_walk(c, n->type_test.type, ns, usings);
        break;

    case AST_CONDITIONAL:
        nr_walk(c, n->conditional.cond, ns, usings);
        nr_walk(c, n->conditional.then_expr, ns, usings);
        nr_walk(c, n->conditional.else_expr, ns, usings);
        break;

    case AST_LAMBDA:
        nr_walk_list(c, &n->lambda.params, ns, usings);
        nr_walk(c, n->lambda.body, ns, usings);
        break;

    case AST_AWAIT_EXPR:
        nr_walk(c, n->await_expr.expr, ns, usings);
        break;

    case AST_STRING_INTERP:
        nr_walk_list(c, &n->string_interp.parts, ns, usings);
        break;

    case AST_REF_ARG:
        nr_walk(c, n->ref_arg.expr, ns, usings);
        nr_walk(c, n->ref_arg.decl_type, ns, usings);
        break;

    case AST_QUERY_EXPR:
        nr_walk(c, n->query.source, ns, usings);
        for (int ci = 0; ci < n->query.clauses.count; ci++) {
            zan_ast_node_t *cl = n->query.clauses.items[ci];
            nr_walk(c, cl->query_clause.expr, ns, usings);
            if (cl->kind == AST_QUERY_JOIN) {
                nr_walk(c, cl->query_clause.source, ns, usings);
                nr_walk(c, cl->query_clause.left_key, ns, usings);
                nr_walk(c, cl->query_clause.right_key, ns, usings);
            }
        }
        nr_walk(c, n->query.group_expr, ns, usings);
        nr_walk(c, n->query.group_key, ns, usings);
        nr_walk(c, n->query.select, ns, usings);
        break;

    case AST_ENUM_MEMBER:
        nr_walk(c, n->enum_member.value, ns, usings);
        break;

    case AST_NAMED_ARG:
        nr_walk(c, n->named_arg.expr, ns, usings);
        break;

    case AST_COLL_INIT:
        nr_walk_list(c, &n->coll_init.items, ns, usings);
        break;

    case AST_TUPLE_EXPR:
        nr_walk_list(c, &n->tuple_expr.items, ns, usings);
        break;

    case AST_TUPLE_TYPE:
        nr_walk_list(c, &n->tuple_type.elems, ns, usings);
        break;

    case AST_TUPLE_DECON:
        nr_walk_list(c, &n->tuple_decon.types, ns, usings);
        nr_walk(c, n->tuple_decon.initializer, ns, usings);
        break;

    case AST_WITH_EXPR:
        nr_walk(c, n->with_expr.expr, ns, usings);
        nr_walk_list(c, &n->with_expr.assigns, ns, usings);
        break;

    case AST_SWITCH_EXPR:
        nr_walk(c, n->switch_expr.expr, ns, usings);
        nr_walk_list(c, &n->switch_expr.arms, ns, usings);
        break;

    case AST_SWITCH_ARM:
        nr_walk(c, n->switch_arm.pattern, ns, usings);
        nr_walk(c, n->switch_arm.type_pattern, ns, usings);
        nr_walk(c, n->switch_arm.when_cond, ns, usings);
        nr_walk(c, n->switch_arm.result, ns, usings);
        break;

    case AST_IDENTIFIER:
        /* 表达式位泛型实例化（`Gen<T>.Static()` / `Gen<T>{..}`）把类型实参挂在
         * inst_type_ref 上——不下钻则其中的命名空间类型从未改写，绑定期
         * "undefined type"（B-ID102 拓扑审计首发）。 */
        nr_walk(c, n->ident.inst_type_ref, ns, usings);
        break;

    default: {
        /* B-ID102 覆盖审计：有非 NULL 子节点的 kind 落到 default 即疑似漏改
         * （ZANC_TRACE 门控）。 */
        int nc = zan_ast_child_count(n), live = 0;
        for (int i = 0; i < nc; i++) if (zan_ast_child_at(n, i)) live++;
        if (live > 0)
            zan_compile_trace("nsresolve.nr_walk: kind %d has %d live children but no case",
                              (int)n->kind, live);
        break;
    }
    }
}

void zan_nsresolve_run(zan_ast_node_t *unit, zan_arena_t *arena, zan_diag_t *diag) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    zan_ast_list_t *decls = &unit->comp_unit.decls;
    if (decls->count == 0) return;
    /* B-ID102 拓扑表自检（ZANC_TRACE 门控）：经 child_count/child_at 递归数全树，
     * 与 zan_ast_node_count() 对照——拓扑表登错字段即崩或计数离谱。 */
    if (getenv("ZANC_TRACE"))
        zan_compile_trace("nsresolve: unit subtree nodes=%zu (allocated=%zu)",
                          zan_ast_subtree_count(unit), zan_ast_node_count());

    nr_ctx_t c;
    c.arena = arena;
    c.diag = diag;
    c.count = 0;
    c.refs = NULL;
    c.quiet_ambiguity = false;
    c.shadow.items = NULL;
    c.shadow.count = 0;
    c.shadow.cap = 0;
    c.fields.items = NULL;
    c.fields.count = 0;
    c.fields.cap = 0;
    c.items = (nr_type_t *)zan_arena_alloc(arena,
                    sizeof(nr_type_t) * (size_t)(decls->count + 1));

    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *d = decls->items[i];
        if (!d || !is_type_decl_kind(d->kind)) continue;
        nr_type_t *t = &c.items[c.count++];
        t->decl = d;
        t->simple = decl_simple_name(d);
        t->ns = zan_ast_ns_name(d);
        t->full = join_ns(arena, t->ns, t->simple);
        t->final = t->simple;
        t->conflicting = false;
    }
    nr_index_build(&c.by_full, c.items, c.count, 1, arena);
    nr_index_build(&c.by_simple, c.items, c.count, 0, arena);

    /* 2 */
    for (int node = 0; node < c.count; node++) {
        zan_istr_t simple = c.items[node].simple;
        for (int other = c.by_simple.buckets[nr_hash(simple) & c.by_simple.mask];
             other >= 0; other = c.by_simple.chains[other].next) {
            int j = c.by_simple.chains[other].idx;
            if (j <= node) continue;
            if (ns_istr_eq(c.items[j].simple, simple) &&
                !ns_istr_eq(c.items[node].full, c.items[j].full)) {
                c.items[node].conflicting = true;
                c.items[j].conflicting = true;
            }
        }
    }

    for (int i = 0; i < c.count; i++) {
        if (!c.items[i].conflicting) continue;
        zan_istr_t m = mangle(&c, c.items[i].full);
        c.items[i].final = m;
        if (zan_ast_orig_name(c.items[i].decl).len == 0)
            zan_ast_ensure_decl_meta(c.items[i].decl, arena)->orig_name = c.items[i].simple;
        decl_set_name(c.items[i].decl, m);
    }

    /* 4 */
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *d = decls->items[i];
        if (!d) continue;
        c.shadow.count = 0;
        nr_walk(&c, d, zan_ast_ns_name(d), zan_ast_ns_usings(d));
    }
    free(c.shadow.items);
}

void zan_nsresolve_prune(zan_ast_node_t *unit, zan_arena_t *arena,
                         zan_diag_t *diag) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    zan_ast_list_t *decls = &unit->comp_unit.decls;
    if (decls->count == 0) return;

    int count = 0;
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *d = decls->items[i];
        if (d && is_type_decl_kind(d->kind)) count++;
    }
    if (count == 0) return;
    nr_type_t *items = (nr_type_t *)zan_arena_alloc(
        arena, sizeof(nr_type_t) * (size_t)count);
    if (!items) return;
    int n = 0;
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *d = decls->items[i];
        if (!d || !is_type_decl_kind(d->kind)) continue;
        items[n].decl = d;
        items[n].simple = decl_simple_name(d);
        items[n].ns = zan_ast_ns_name(d);
        items[n].full = join_ns(arena, items[n].ns, items[n].simple);
        items[n].final = items[n].simple;
        items[n].conflicting = false;
        n++;
    }
    nr_index_t by_full;
    nr_index_build(&by_full, items, n, 1, arena);
    nr_index_t by_simple;
    nr_index_build(&by_simple, items, n, 0, arena);

    int user_roots = 0;
    for (int i = 0; i < n; i++)
        if (!items[i].decl->from_stdlib) user_roots++;
    if (user_roots == 0) return;
    if (getenv("ZAN_NO_PRUNE")) return;
    unsigned char *kept = (unsigned char *)calloc((size_t)n, 1);
    if (!kept) return;
    for (int i = 0; i < n; i++) {
        zan_ast_node_t *d = items[i].decl;
        if (!d->from_stdlib) { kept[i] = 1; continue; }
        if (d->kind == AST_CLASS_DECL || d->kind == AST_STRUCT_DECL) {
            bool is_ext_host = false;
            for (int m = 0; m < d->type_decl.members.count && !is_ext_host; m++) {
                zan_ast_node_t *mem = d->type_decl.members.items[m];
                if (!mem || mem->kind != AST_METHOD_DECL) continue;
                zan_ast_list_t *ps = &mem->method_decl.params;
                if (ps->count > 0 && ps->items[0] &&
                    ps->items[0]->kind == AST_PARAM &&
                    ps->items[0]->param.is_this) is_ext_host = true;
            }
            if (is_ext_host) kept[i] = 1;

            /* 控制器（自动路由发现）：不修剪包与标准库控制器 */
            if (d->kind == AST_CLASS_DECL && d->type_decl.name.str) {
                uint32_t nl = d->type_decl.name.len;
                const char *ns = d->type_decl.name.str;
                if (nl >= 10 && memcmp(ns + nl - 10, "Controller", 10) == 0) {
                    if (nl != 10 && (nl != 13 || memcmp(ns, "AppController", 13) != 0) &&
                        (nl != 15 || memcmp(ns, "AdminController", 15) != 0) &&
                        (nl != 13 || memcmp(ns, "ApiController", 13) != 0)) {
                        kept[i] = 1;
                    }
                } else if (zan_ast_has_attr(d, "Route")) {
                    kept[i] = 1;
                }
            }
        }
    }

    unsigned char *walked = (unsigned char *)calloc((size_t)n, 1);
    if (!walked) { free(kept); return; }
    nr_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.arena = arena;
    c.diag = NULL;
    c.items = items;
    c.count = n;
    nr_index_build(&c.by_full, items, n, 1, arena);
    nr_index_build(&c.by_simple, items, n, 0, arena);
    zp_refs_t refs;
    memset(&refs, 0, sizeof(refs));
    c.refs = &refs;
    for (;;) {
        int before = 0;
        for (int i = 0; i < n; i++) before += kept[i];
        for (int i = 0; i < n; i++) {
            if (!kept[i] || walked[i]) continue;
            walked[i] = 1;
            zan_ast_node_t *d = items[i].decl;
            refs.count = 0;
            nr_walk(&c, d, zan_ast_ns_name(d), zan_ast_ns_usings(d));
            for (int k = 0; k < refs.count; k++) {
                int j = nr_index_find(&by_simple, items, 0, refs.items[k]);
                if (j >= 0) kept[j] = 1;
            }
        }
        int after = 0;
        for (int i = 0; i < n; i++) after += kept[i];
        if (after == before) break;
    }
    free(walked);

    int w = 0;
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *d = decls->items[i];
        if (d && is_type_decl_kind(d->kind)) {
            int j = nr_index_find(&by_full, items, 1,
                                  join_ns(arena, zan_ast_ns_name(d), decl_simple_name(d)));
            if (j >= 0 && !kept[j]) continue;
        }
        decls->items[w++] = d;
    }
    decls->count = w;
    free(kept);
}
