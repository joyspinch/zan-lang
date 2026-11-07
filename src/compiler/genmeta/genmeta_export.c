    if (!n) return;
    switch (n->kind) {
    case AST_CALL:
        gm_walk_expr(n->call.callee, c);
        for (int i = 0; i < n->call.args.count; i++)
            gm_walk_expr(n->call.args.items[i], c);
        for (int i = 0; i < n->call.type_args.count; i++)
            gm_walk_expr(n->call.type_args.items[i], c);
        if (n->call.callee &&
            (n->call.callee->kind == AST_MEMBER_ACCESS ||
             n->call.callee->kind == AST_IDENTIFIER))
            gm_record_call(n, c);
        break;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        gm_walk_expr(n->binary.left, c);
        gm_walk_expr(n->binary.right, c);
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        gm_walk_expr(n->unary.operand, c);
        break;
    case AST_MEMBER_ACCESS:
        gm_walk_expr(n->member.object, c);
        break;
    case AST_INDEX:
        gm_walk_expr(n->index.object, c);
        gm_walk_expr(n->index.index, c);
        break;
    case AST_CONDITIONAL:
        gm_walk_expr(n->conditional.cond, c);
        gm_walk_expr(n->conditional.then_expr, c);
        gm_walk_expr(n->conditional.else_expr, c);
        break;
    case AST_NEW_EXPR:
        for (int i = 0; i < n->new_expr.args.count; i++)
            gm_walk_expr(n->new_expr.args.items[i], c);
        for (int i = 0; i < n->new_expr.arg_inits.count; i++)
            gm_walk_expr(n->new_expr.arg_inits.items[i], c);
        break;
    case AST_COLL_INIT:
        for (int i = 0; i < n->coll_init.items.count; i++)
            gm_walk_expr(n->coll_init.items.items[i], c);
        break;
    case AST_CAST_EXPR:
        gm_walk_expr(n->cast.expr, c);
        break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        gm_walk_expr(n->type_test.expr, c);
        break;
    case AST_LAMBDA:
        gm_walk_expr(n->lambda.body, c);
        break;
    case AST_AWAIT_EXPR:
        gm_walk_expr(n->await_expr.expr, c);
        break;
    case AST_REF_ARG:
        gm_walk_expr(n->ref_arg.expr, c);
        break;
    case AST_STRING_INTERP:
        for (int i = 0; i < n->string_interp.parts.count; i++)
            gm_walk_expr(n->string_interp.parts.items[i], c);
        break;
    default:
        break;
    }
}

static void gm_walk_stmt(zan_ast_node_t *n, gm_ctx_t *c) {
    if (!n) return;
    switch (n->kind) {
    case AST_BLOCK:
        for (int i = 0; i < n->block.stmts.count; i++)
            gm_walk_stmt(n->block.stmts.items[i], c);
        break;
    case AST_VAR_DECL:
        gm_walk_expr(n->var_decl.initializer, c);
        break;
    case AST_EXPR_STMT:
        gm_walk_expr(n->expr_stmt.expr, c);
        break;
    case AST_RETURN_STMT:
        gm_walk_expr(n->ret.value, c);
        break;
    case AST_IF_STMT:
        gm_walk_expr(n->if_stmt.cond, c);
        gm_walk_stmt(n->if_stmt.then_body, c);
        gm_walk_stmt(n->if_stmt.else_body, c);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        gm_walk_expr(n->while_stmt.cond, c);
        gm_walk_stmt(n->while_stmt.body, c);
        break;
    case AST_FOR_STMT:
        gm_walk_stmt(n->for_stmt.init, c);
        gm_walk_expr(n->for_stmt.cond, c);
        gm_walk_expr(n->for_stmt.step, c);
        gm_walk_stmt(n->for_stmt.body, c);
        break;
    case AST_FOREACH_STMT:
        gm_walk_expr(n->foreach_stmt.collection, c);
        gm_walk_stmt(n->foreach_stmt.body, c);
        break;
    case AST_THROW_STMT:
        gm_walk_expr(n->throw_stmt.value, c);
        break;
    case AST_TRY_STMT:
        gm_walk_stmt(n->try_stmt.try_body, c);
        for (int i = 0; i < n->try_stmt.catches.count; i++)
            gm_walk_stmt(n->try_stmt.catches.items[i], c);
        gm_walk_stmt(n->try_stmt.finally_body, c);
        break;
    case AST_CATCH_CLAUSE:
        gm_walk_stmt(n->catch_clause.body, c);
        break;
    case AST_SWITCH_STMT:
        gm_walk_expr(n->switch_stmt.expr, c);
        for (int i = 0; i < n->switch_stmt.cases.count; i++)
            gm_walk_stmt(n->switch_stmt.cases.items[i], c);
        break;
    case AST_SWITCH_CASE:
        gm_walk_expr(n->switch_case.pattern, c);
        gm_walk_stmt(n->switch_case.body, c);
        break;
    case AST_LOCK_STMT:
        gm_walk_expr(n->lock_stmt.expr, c);
        gm_walk_stmt(n->lock_stmt.body, c);
        break;
    case AST_CHECKED_STMT:
        gm_walk_stmt(n->checked_stmt.body, c);
        break;
    case AST_YIELD_STMT:
        gm_walk_expr(n->yield_stmt.value, c);
        break;
    case AST_LABEL_STMT:
    case AST_GOTO_STMT:
        break;
    case AST_QUERY_EXPR:
        gm_walk_expr(n->query.source, c);
        for (int i = 0; i < n->query.clauses.count; i++) {
            zan_ast_node_t *cl = n->query.clauses.items[i];
            gm_walk_expr(cl->query_clause.expr, c);
            if (cl->kind == AST_QUERY_JOIN) {
                gm_walk_expr(cl->query_clause.source, c);
                gm_walk_expr(cl->query_clause.left_key, c);
                gm_walk_expr(cl->query_clause.right_key, c);
            }
        }
        gm_walk_expr(n->query.group_expr, c);
        gm_walk_expr(n->query.group_key, c);
        gm_walk_expr(n->query.select, c);
        break;
    default:
        break;
    }
}

static void gm_export_type(zan_ast_node_t *decl, json_value *classes) {
    json_value *o = json_new_obj();
    switch (decl->kind) {
    case AST_CLASS_DECL:
    case AST_STRUCT_DECL:
    case AST_INTERFACE_DECL: {
        zan_istr_t decl_ns = zan_ast_ns_name(decl);
        zan_istr_t decl_orig = zan_ast_orig_name(decl);
        json_obj_set(o, "name", json_new_str(
            decl->type_decl.name.str ? (const char *)decl->type_decl.name.str
                                     : ""));
        json_obj_set(o, "ns", json_new_str(
            decl_ns.str ? (const char *)decl_ns.str : ""));
        json_obj_set(o, "kind", json_new_str(
            decl->kind == AST_CLASS_DECL ? "class" :
            decl->kind == AST_STRUCT_DECL ? "struct" : "interface"));
        json_obj_set(o, "abstract", json_new_bool(
            (decl->type_decl.modifiers & MOD_ABSTRACT) != 0));
        json_obj_set(o, "generic", json_new_bool(
            decl->type_decl.type_params.count > 0));
        if (decl->type_decl.type_params.count > 0) {
            json_value *tparams = json_new_arr();
            for (int i = 0; i < decl->type_decl.type_params.count; i++) {
                zan_ast_node_t *tp = decl->type_decl.type_params.items[i];
                if (tp && tp->ident.name.str) {
                    json_arr_add(tparams, json_new_str((const char *)tp->ident.name.str));
                }
            }
            json_obj_set(o, "type_params", tparams);
        }
        if (decl_orig.len)
            json_obj_set(o, "orig", json_new_str(
                decl_orig.str ? (const char *)decl_orig.str : ""));
        json_obj_set(o, "file", json_new_num((double)decl->loc.file_id));
        json_obj_set(o, "line", json_new_num((double)decl->loc.line));
        json_value *bases = json_new_arr();
        for (int i = 0; i < decl->type_decl.bases.count; i++) {
            char b[256];
            gm_type_str(decl->type_decl.bases.items[i], b, sizeof(b));
            json_arr_add(bases, json_new_str(b));
        }
        json_obj_set(o, "bases", bases);
        json_value *attrs = json_new_arr();
        gm_attrs_json(zan_ast_attributes(decl), attrs);
        json_obj_set(o, "attrs", attrs);
        json_value *fields = json_new_arr();
        json_value *methods = json_new_arr();
        json_value *props = json_new_arr();
        json_value *ctors = json_new_arr();
        for (int i = 0; i < decl->type_decl.members.count; i++) {
            zan_ast_node_t *m = decl->type_decl.members.items[i];
            if (m->kind == AST_FIELD_DECL) {
                json_value *f = json_new_obj();
                json_obj_set(f, "name", json_new_str(
                    m->field_decl.name.str ? (const char *)m->field_decl.name.str
                                           : ""));
                json_obj_set(f, "static", json_new_bool(
                    (m->field_decl.modifiers & MOD_STATIC) != 0));
                /* 字段可见性：默认是 public（Zan 里不写修饰符即可外部访问），
                 * 但显式 private/protected 的字段外部碰不到——代码生成器
                 * （System.Mapping.GenMapping 按字段名配对并写赋值）需要据此
                 * 跳过不可写的字段，否则生成的代码编译不过。 */
                json_obj_set(f, "private", json_new_bool(
                    (m->field_decl.modifiers & MOD_PRIVATE) != 0));
                json_obj_set(f, "protected", json_new_bool(
                    (m->field_decl.modifiers & MOD_PROTECTED) != 0));
                json_obj_set(f, "file", json_new_num((double)m->loc.file_id));
                json_obj_set(f, "line", json_new_num((double)m->loc.line));
                json_obj_set(f, "col", json_new_num((double)m->loc.col));
                char t[256];
                gm_type_str(m->field_decl.type, t, sizeof(t));
                json_obj_set(f, "type", json_new_str(t));
                json_value *fa = json_new_arr();
                gm_attrs_json(zan_ast_attributes(m), fa);
                json_obj_set(f, "attrs", fa);
                json_arr_add(fields, f);
            } else if (m->kind == AST_METHOD_DECL ||
                       m->kind == AST_CONSTRUCTOR_DECL) {
                json_value *mm = json_new_obj();
                json_obj_set(mm, "name", json_new_str(
                    m->method_decl.name.str ? (const char *)m->method_decl.name.str
                                            : ""));
                json_obj_set(mm, "ctor", json_new_bool(
                    m->kind == AST_CONSTRUCTOR_DECL));
                json_obj_set(mm, "static", json_new_bool(
                    (m->method_decl.modifiers & MOD_STATIC) != 0));
                json_obj_set(mm, "async", json_new_bool(
                    (m->method_decl.modifiers & MOD_ASYNC) != 0));
                json_obj_set(mm, "private", json_new_bool(
                    (m->method_decl.modifiers & MOD_PRIVATE) != 0));
                json_obj_set(mm, "protected", json_new_bool(
                    (m->method_decl.modifiers & MOD_PROTECTED) != 0));
                json_obj_set(mm, "virtual", json_new_bool(
                    (m->method_decl.modifiers & MOD_VIRTUAL) != 0));
                json_obj_set(mm, "override", json_new_bool(
                    (m->method_decl.modifiers & MOD_OVERRIDE) != 0));
                json_obj_set(mm, "abstract", json_new_bool(
                    (m->method_decl.modifiers & MOD_ABSTRACT) != 0));
                if (m->kind == AST_METHOD_DECL && m->method_decl.return_type) {
                    char rt[256];
                    gm_type_str(m->method_decl.return_type, rt, sizeof(rt));
                    json_obj_set(mm, "ret", json_new_str(rt));
                }
                if (m->method_decl.type_params.count > 0) {
                    json_value *mtparams = json_new_arr();
                    for (int tp_i = 0; tp_i < m->method_decl.type_params.count; tp_i++) {
                        zan_ast_node_t *tp = m->method_decl.type_params.items[tp_i];
                        if (tp && tp->ident.name.str) {
                            json_arr_add(mtparams, json_new_str((const char *)tp->ident.name.str));
                        }
                    }
                    json_obj_set(mm, "type_params", mtparams);
                }
                json_obj_set(mm, "file", json_new_num((double)m->loc.file_id));
                json_obj_set(mm, "line", json_new_num((double)m->loc.line));
                json_value *ps = json_new_arr();
                for (int j = 0; j < m->method_decl.params.count; j++) {
                    zan_ast_node_t *p = m->method_decl.params.items[j];
                    if (p->kind != AST_PARAM) continue;
                    json_value *pv = json_new_obj();
                    json_obj_set(pv, "name", json_new_str(
                        p->param.name.str ? (const char *)p->param.name.str
                                          : ""));
                    char pt[256];
                    gm_type_str(p->param.type, pt, sizeof(pt));
                    json_obj_set(pv, "type", json_new_str(pt));
                    if (p->param.by_ref) {
                        json_obj_set(pv, "by_ref", json_new_num((double)p->param.by_ref));
                    }
                    if (p->param.default_val)
                        json_obj_set(pv, "def",
                                     gm_expr_tree(p->param.default_val));
                    json_arr_add(ps, pv);
                }
                json_obj_set(mm, "params", ps);
                json_value *ma = json_new_arr();
                gm_attrs_json(zan_ast_attributes(m), ma);
                json_obj_set(mm, "attrs", ma);
                json_arr_add(methods, mm);
                if (m->kind == AST_CONSTRUCTOR_DECL) {
                    json_value *co = json_new_obj();
                    json_value *cps = json_new_arr();
                    for (int j = 0; j < m->method_decl.params.count; j++) {
                        zan_ast_node_t *p = m->method_decl.params.items[j];
                        if (p->kind != AST_PARAM) continue;
                        json_value *pv = json_new_obj();
                        json_obj_set(pv, "name", json_new_str(
                            p->param.name.str ? (const char *)p->param.name.str
                                              : ""));
                        char pt[256];
                        gm_type_str(p->param.type, pt, sizeof(pt));
                        json_obj_set(pv, "type", json_new_str(pt));
                        if (p->param.default_val)
                            json_obj_set(pv, "def",
                                         gm_expr_tree(p->param.default_val));
                        json_arr_add(cps, pv);
                    }
                    json_obj_set(co, "params", cps);
                    json_value *as = json_new_arr();
                    zan_ast_node_t *body = m->method_decl.body;
                    if (body && body->kind == AST_BLOCK) {
                        for (int k = 0; k < body->block.stmts.count; k++) {
                            zan_ast_node_t *st = body->block.stmts.items[k];
                            if (!st || st->kind != AST_EXPR_STMT) continue;
                            zan_ast_node_t *ex = st->expr_stmt.expr;
                            if (!ex || ex->kind != AST_ASSIGNMENT) continue;
                            zan_ast_node_t *l = ex->binary.left;
                            zan_ast_node_t *rt = ex->binary.right;
                            const char *propn = NULL, *paramn = NULL;
                            char lb[64], rb[64];
                            if (l && l->kind == AST_IDENTIFIER) {
                                gm_istr_to(lb, sizeof(lb), l->ident.name);
                                propn = lb;
                            } else if (l && l->kind == AST_MEMBER_ACCESS &&
                                       l->member.object &&
                                       (l->member.object->kind == AST_THIS_EXPR ||
                                        (l->member.object->kind == AST_IDENTIFIER &&
                                         gm_istr_is(l->member.object->ident.name,
                                                 "this")))) {
                                gm_istr_to(lb, sizeof(lb), l->member.name);
                                propn = lb;
                            }
                            if (rt && rt->kind == AST_IDENTIFIER) {
                                gm_istr_to(rb, sizeof(rb), rt->ident.name);
                                paramn = rb;
                            }
                            if (propn && paramn) {
                                json_value *a = json_new_obj();
                                json_obj_set(a, "prop", json_new_str(propn));
                                json_obj_set(a, "param", json_new_str(paramn));
                                json_arr_add(as, a);
                            }
                        }
                    }
                    json_obj_set(co, "assigns", as);
                    json_arr_add(ctors, co);
                }
            } else if (m->kind == AST_PROPERTY_DECL) {
                json_value *p = json_new_obj();
                json_obj_set(p, "name", json_new_str(
                    m->field_decl.name.str ? (const char *)m->field_decl.name.str
                                           : ""));
                char pt[256];
                gm_type_str(m->field_decl.type, pt, sizeof(pt));
                json_obj_set(p, "type", json_new_str(pt));
                json_obj_set(p, "static", json_new_bool(
                    (m->field_decl.modifiers & MOD_STATIC) != 0));
                json_obj_set(p, "get", json_new_bool(m->field_decl.has_getter));
                json_obj_set(p, "set", json_new_bool(m->field_decl.has_setter));
                if (m->field_decl.initializer) {
                    json_obj_set(p, "init",
                                 gm_expr_tree(m->field_decl.initializer));
                }
                json_value *pa = json_new_arr();
                gm_attrs_json(zan_ast_attributes(m), pa);
                json_obj_set(p, "attrs", pa);
                json_arr_add(props, p);
            }
        }
        json_obj_set(o, "fields", fields);
        json_obj_set(o, "methods", methods);
        json_obj_set(o, "props", props);
        json_obj_set(o, "ctors", ctors);
        break;
    }
    case AST_ENUM_DECL: {
        json_obj_set(o, "name", json_new_str(
            decl->type_decl.name.str ? (const char *)decl->type_decl.name.str
                                     : ""));
        zan_istr_t decl_ns = zan_ast_ns_name(decl);
        json_obj_set(o, "ns", json_new_str(
            decl_ns.str ? (const char *)decl_ns.str : ""));
        json_obj_set(o, "kind", json_new_str("enum"));
        json_value *members = json_new_arr();
        for (int i = 0; i < decl->type_decl.members.count; i++) {
            zan_ast_node_t *m = decl->type_decl.members.items[i];
            if (m->kind != AST_ENUM_MEMBER) continue;
            json_value *em = json_new_obj();
            json_obj_set(em, "name", json_new_str(
                m->enum_member.name.str ? (const char *)m->enum_member.name.str
                                        : ""));
            if (m->enum_member.value && m->enum_member.value->kind == AST_INT_LITERAL)
                json_obj_set(em, "value", json_new_num(
                    (double)m->enum_member.value->int_val));
            json_arr_add(members, em);
        }
        json_obj_set(o, "members", members);
        break;
    }
    default:
        json_free(o);
        return;
    }
    json_arr_add(classes, o);
}

typedef struct {
    int want;
    int seen;
    zan_ast_node_t *found;
    zan_ast_node_t **nodes;
    int cap;
} gm_find_ctx_t;

static void gm_find_expr(zan_ast_node_t *n, gm_find_ctx_t *f);
static void gm_find_stmt(zan_ast_node_t *n, gm_find_ctx_t *f);

static void gm_find_expr(zan_ast_node_t *n, gm_find_ctx_t *f) {
    if (!n || f->found) return;
    switch (n->kind) {
    case AST_CALL:
        gm_find_expr(n->call.callee, f);
        for (int i = 0; i < n->call.args.count; i++)
            gm_find_expr(n->call.args.items[i], f);
        for (int i = 0; i < n->call.type_args.count; i++)
            gm_find_expr(n->call.type_args.items[i], f);
        if (n->call.callee &&
            (n->call.callee->kind == AST_MEMBER_ACCESS ||
             n->call.callee->kind == AST_IDENTIFIER)) {
            f->seen++;
            if (f->nodes && f->seen <= f->cap) f->nodes[f->seen - 1] = n;
            if (f->seen == f->want) { f->found = n; return; }
        }
        break;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        gm_find_expr(n->binary.left, f);
        gm_find_expr(n->binary.right, f);
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        gm_find_expr(n->unary.operand, f);
        break;
    case AST_MEMBER_ACCESS:
        gm_find_expr(n->member.object, f);
        break;
    case AST_INDEX:
        gm_find_expr(n->index.object, f);
        gm_find_expr(n->index.index, f);
        break;
    case AST_CONDITIONAL:
        gm_find_expr(n->conditional.cond, f);
        gm_find_expr(n->conditional.then_expr, f);
        gm_find_expr(n->conditional.else_expr, f);
        break;
    case AST_NEW_EXPR:
        for (int i = 0; i < n->new_expr.args.count; i++)
            gm_find_expr(n->new_expr.args.items[i], f);
        for (int i = 0; i < n->new_expr.arg_inits.count; i++)
            gm_find_expr(n->new_expr.arg_inits.items[i], f);
        break;
    case AST_COLL_INIT:
        for (int i = 0; i < n->coll_init.items.count; i++)
            gm_find_expr(n->coll_init.items.items[i], f);
        break;
    case AST_CAST_EXPR:
        gm_find_expr(n->cast.expr, f);
        break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        gm_find_expr(n->type_test.expr, f);
        break;
    case AST_LAMBDA:
        gm_find_expr(n->lambda.body, f);
        break;
    case AST_AWAIT_EXPR:
        gm_find_expr(n->await_expr.expr, f);
        break;
    case AST_REF_ARG:
        gm_find_expr(n->ref_arg.expr, f);
        break;
    case AST_STRING_INTERP:
        for (int i = 0; i < n->string_interp.parts.count; i++)
            gm_find_expr(n->string_interp.parts.items[i], f);
        break;
    default:
        break;
    }
}

static void gm_find_stmt(zan_ast_node_t *n, gm_find_ctx_t *f) {
    if (!n || f->found) return;
    switch (n->kind) {
    case AST_BLOCK:
        for (int i = 0; i < n->block.stmts.count; i++)
            gm_find_stmt(n->block.stmts.items[i], f);
        break;
    case AST_VAR_DECL:
        gm_find_expr(n->var_decl.initializer, f);
        break;
    case AST_EXPR_STMT:
        gm_find_expr(n->expr_stmt.expr, f);
        break;
    case AST_RETURN_STMT:
        gm_find_expr(n->ret.value, f);
        break;
    case AST_IF_STMT:
        gm_find_expr(n->if_stmt.cond, f);
        gm_find_stmt(n->if_stmt.then_body, f);
        gm_find_stmt(n->if_stmt.else_body, f);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        gm_find_expr(n->while_stmt.cond, f);
        gm_find_stmt(n->while_stmt.body, f);
        break;
    case AST_FOR_STMT:
        gm_find_stmt(n->for_stmt.init, f);
        gm_find_expr(n->for_stmt.cond, f);
        gm_find_expr(n->for_stmt.step, f);
        gm_find_stmt(n->for_stmt.body, f);
        break;
    case AST_FOREACH_STMT:
        gm_find_expr(n->foreach_stmt.collection, f);
        gm_find_stmt(n->foreach_stmt.body, f);
        break;
    case AST_THROW_STMT:
        gm_find_expr(n->throw_stmt.value, f);
        break;
    case AST_TRY_STMT:
        gm_find_stmt(n->try_stmt.try_body, f);
        for (int i = 0; i < n->try_stmt.catches.count; i++)
            gm_find_stmt(n->try_stmt.catches.items[i], f);
        gm_find_stmt(n->try_stmt.finally_body, f);
        break;
    case AST_CATCH_CLAUSE:
        gm_find_stmt(n->catch_clause.body, f);
        break;
    case AST_SWITCH_STMT:
        gm_find_expr(n->switch_stmt.expr, f);
        for (int i = 0; i < n->switch_stmt.cases.count; i++)
            gm_find_stmt(n->switch_stmt.cases.items[i], f);
        break;
    case AST_SWITCH_CASE:
        gm_find_expr(n->switch_case.pattern, f);
        gm_find_stmt(n->switch_case.body, f);
        break;
    case AST_LOCK_STMT:
        gm_find_expr(n->lock_stmt.expr, f);
        gm_find_stmt(n->lock_stmt.body, f);
        break;
    case AST_CHECKED_STMT:
        gm_find_stmt(n->checked_stmt.body, f);
        break;
    case AST_YIELD_STMT:
        gm_find_expr(n->yield_stmt.value, f);
        break;
    case AST_LABEL_STMT:
    case AST_GOTO_STMT:
        break;
    case AST_QUERY_EXPR:
        gm_find_expr(n->query.source, f);
        for (int i = 0; i < n->query.clauses.count; i++) {
            zan_ast_node_t *cl = n->query.clauses.items[i];
            gm_find_expr(cl->query_clause.expr, f);
            if (cl->kind == AST_QUERY_JOIN) {
                gm_find_expr(cl->query_clause.source, f);
                gm_find_expr(cl->query_clause.left_key, f);
                gm_find_expr(cl->query_clause.right_key, f);
            }
        }
        gm_find_expr(n->query.group_expr, f);
        gm_find_expr(n->query.group_key, f);
        gm_find_expr(n->query.select, f);
        break;
    default:
        break;
    }
}

zan_ast_node_t *zan_genmeta_find_call(zan_ast_node_t *unit, int id) {
    gm_find_ctx_t f;
    f.want = id;
    f.seen = 0;
    f.found = NULL;
    f.nodes = NULL;
    f.cap = 0;
    for (int i = 0; i < unit->comp_unit.decls.count && !f.found; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL)
            continue;
        for (int j = 0; j < decl->type_decl.members.count && !f.found; j++) {
            zan_ast_node_t *m = decl->type_decl.members.items[j];
            if (m->kind == AST_METHOD_DECL || m->kind == AST_CONSTRUCTOR_DECL)
                gm_find_stmt(m->method_decl.body, &f);
            else if (m->kind == AST_FIELD_DECL || m->kind == AST_PROPERTY_DECL)
                gm_find_expr(m->field_decl.initializer, &f);
        }
    }
    return f.found;
}

int zan_genmeta_index_calls(zan_ast_node_t *unit, zan_ast_node_t **nodes,
                            int cap) {
    gm_find_ctx_t f;
    f.want = 0;
    f.seen = 0;
    f.found = NULL;
    f.nodes = nodes;
    f.cap = cap;
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL)
            continue;
        for (int j = 0; j < decl->type_decl.members.count; j++) {
            zan_ast_node_t *m = decl->type_decl.members.items[j];
            if (m->kind == AST_METHOD_DECL || m->kind == AST_CONSTRUCTOR_DECL)
                gm_find_stmt(m->method_decl.body, &f);
            else if (m->kind == AST_FIELD_DECL || m->kind == AST_PROPERTY_DECL)
                gm_find_expr(m->field_decl.initializer, &f);
        }
    }
    return f.seen;
}

static char *gm_export(zan_ast_node_t *unit, zan_diag_t *diag) {
    if (!unit) return NULL;
    json_value *root = json_new_obj();
    json_obj_set(root, "version", json_new_num(1));

    if (diag) {
        json_value *files = json_new_arr();
        for (int i = 0; i < diag->file_count; i++) {
            const char *name = diag->file_names ? diag->file_names[i] : NULL;
            json_arr_add(files, json_new_str(name ? name : ""));
        }
        json_obj_set(root, "files", files);
    }

    json_value *classes = json_new_arr();
    for (int i = 0; i < unit->comp_unit.decls.count; i++)
        gm_export_type(unit->comp_unit.decls.items[i], classes);
    json_obj_set(root, "classes", classes);

    int expr_count = 0;
    const char **expr_names = gm_expr_method_names(classes, &expr_count);
    int table_count = 0;
    const char **table_names = gm_table_entity_names(classes, &table_count);

    gm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.calls = json_new_arr();
    ctx.call_id = 0;
    ctx.pend_nodes = NULL;
    ctx.pend_ids = NULL;
    ctx.pend_count = 0;
    ctx.pend_cap = 0;
    ctx.expr_names = expr_names;
    ctx.expr_count = expr_count;
    ctx.table_names = table_names;
    ctx.table_count = table_count;
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL)
            continue;
        gm_istr_to(ctx.cur_cls, sizeof(ctx.cur_cls), decl->type_decl.name);
        for (int j = 0; j < decl->type_decl.members.count; j++) {
            zan_ast_node_t *m = decl->type_decl.members.items[j];
            if (m->kind == AST_METHOD_DECL || m->kind == AST_CONSTRUCTOR_DECL) {
                gm_istr_to(ctx.cur_fn, sizeof(ctx.cur_fn), m->method_decl.name);
                gm_walk_stmt(m->method_decl.body, &ctx);
            } else if (m->kind == AST_FIELD_DECL ||
                       m->kind == AST_PROPERTY_DECL) {
                ctx.cur_fn[0] = 0;
                gm_walk_expr(m->field_decl.initializer, &ctx);
            }
        }
    }
    gm_prune_calls(ctx.calls, expr_names, expr_count, table_names, table_count);
    json_obj_set(root, "calls", ctx.calls);

    free(expr_names);
    free(table_names);

    free(ctx.pend_nodes);
    free(ctx.pend_ids);
    free(ctx.rec_nodes);
    free(ctx.rec_ids);

    char *out = json_serialize(root);
    {
        const char *gm_dump = getenv("ZAN_GENMETA_DUMP");
        if (gm_dump && gm_dump[0]) {
            FILE *df = fopen(gm_dump, "wb");
            if (df) {
                fwrite(out, 1, strlen(out), df);
                fclose(df);
            }
        }
    }
    fflush(stderr);
    json_free(root);
    return out;
}

char *zan_genmeta_export(zan_ast_node_t *unit) {
    return gm_export(unit, NULL);
}

char *zan_genmeta_export_files(zan_ast_node_t *unit, zan_diag_t *diag) {
    return gm_export(unit, diag);
}


static zan_token_kind_t gm_token_kind(const char *name) {
    if (!name) return TK_INVALID;
    for (int k = 0; k < TK__COUNT; k++)
        if (strcmp(zan_token_kind_name((zan_token_kind_t)k), name) == 0)
            return (zan_token_kind_t)k;
    return TK_INVALID;
}

static zan_ast_node_t *gm_type_ref_from(const char *name, zan_arena_t *arena,
                                        zan_loc_t loc) {
    zan_ast_node_t *t = zan_ast_new(arena, AST_TYPE_REF, loc);
    t->type_ref.name.str = name ? zan_arena_strdup(arena, name, strlen(name))
                                : NULL;
    t->type_ref.name.len = name ? (uint32_t)strlen(name) : 0;
    zan_ast_list_init(&t->type_ref.type_args);
    t->type_ref.is_array = false;
    t->type_ref.is_nullable = false;
    return t;
}

zan_ast_node_t *zan_genmeta_expr_from_json(json_value *j, zan_arena_t *arena) {
    if (!j || j->type != JSON_OBJ) return NULL;
    const char *k = json_get_str(json_obj_get(j, "k"));
    if (!k) return NULL;
    zan_loc_t loc = zan_loc(0, 0, 0, 0);
    json_value *f = NULL;
    zan_ast_node_t *n = NULL;

    if (strcmp(k, "null") == 0)
        return zan_ast_new(arena, AST_NULL_LITERAL, loc);

    if (strcmp(k, "lit") == 0) {
        const char *t = json_get_str(json_obj_get(j, "t"));
        json_value *v = json_obj_get(j, "v");
        if (t && strcmp(t, "int") == 0) {
            n = zan_ast_new(arena, AST_INT_LITERAL, loc);
            n->int_val = (int64_t)json_get_num(v, 0);
        } else if (t && strcmp(t, "double") == 0) {
            n = zan_ast_new(arena, AST_FLOAT_LITERAL, loc);
            n->float_val = json_get_num(v, 0);
        } else if (t && strcmp(t, "float") == 0) {
            n = zan_ast_new(arena, AST_FLOAT_LITERAL, loc);
            n->float_val = json_get_num(v, 0);
            n->lit_suffix = 1;
        } else if (t && strcmp(t, "bool") == 0) {
            n = zan_ast_new(arena, AST_BOOL_LITERAL, loc);
            n->bool_val = json_get_bool(v, false);
        } else if (t && strcmp(t, "char") == 0) {
            n = zan_ast_new(arena, AST_CHAR_LITERAL, loc);
            n->int_val = (int64_t)json_get_num(v, 0);
        } else if (t && strcmp(t, "str") == 0) {
            const char *s = json_get_str(v);
            n = zan_ast_new(arena, AST_STRING_LITERAL, loc);
            n->str_val.str = s ? zan_arena_strdup(arena, s, strlen(s)) : NULL;
            n->str_val.len = s ? (uint32_t)strlen(s) : 0;
        }
        return n;
    }

    if (strcmp(k, "id") == 0) {
        const char *nm = json_get_str(json_obj_get(j, "n"));
        n = zan_ast_new(arena, AST_IDENTIFIER, loc);
        n->ident.name.str = nm ? zan_arena_strdup(arena, nm, strlen(nm)) : NULL;
        n->ident.name.len = nm ? (uint32_t)strlen(nm) : 0;
        json_value *ta = json_obj_get(j, "targs");
        if (ta && ta->type == JSON_ARR && ta->as.arr.count > 0) {
            zan_ast_node_t *tr = gm_type_ref_from(nm, arena, loc);
            for (int i = 0; i < ta->as.arr.count; i++) {
                const char *targ = json_get_str(ta->as.arr.items[i]);
                if (!targ) continue;
                zan_ast_list_push(&tr->type_ref.type_args,
                                  gm_type_ref_from(targ, arena, loc), arena);
            }
            n->ident.inst_type_ref = tr;
        }
        return n;
    }

    if (strcmp(k, "this") == 0) return zan_ast_new(arena, AST_THIS_EXPR, loc);
    if (strcmp(k, "base") == 0) return zan_ast_new(arena, AST_BASE_EXPR, loc);

    if (strcmp(k, "bin") == 0) {
        n = zan_ast_new(arena, AST_BINARY, loc);
        n->binary.op = gm_token_kind(json_get_str(json_obj_get(j, "op")));
        n->binary.left =
            zan_genmeta_expr_from_json(json_obj_get(j, "l"), arena);
        n->binary.right =
            zan_genmeta_expr_from_json(json_obj_get(j, "r"), arena);
        return n;
    }
    if (strcmp(k, "uni") == 0) {
        n = zan_ast_new(arena, AST_UNARY, loc);
        n->unary.op = gm_token_kind(json_get_str(json_obj_get(j, "op")));
        n->unary.operand =
            zan_genmeta_expr_from_json(json_obj_get(j, "o"), arena);
        return n;
    }
    if (strcmp(k, "mem") == 0) {
        const char *nm = json_get_str(json_obj_get(j, "n"));
        n = zan_ast_new(arena, AST_MEMBER_ACCESS, loc);
        n->member.object =
            zan_genmeta_expr_from_json(json_obj_get(j, "o"), arena);
        n->member.name.str = nm ? zan_arena_strdup(arena, nm, strlen(nm)) : NULL;
        n->member.name.len = nm ? (uint32_t)strlen(nm) : 0;
        n->member.null_cond = 0;
        return n;
    }
    if (strcmp(k, "call") == 0) {
        n = zan_ast_new(arena, AST_CALL, loc);
        n->call.callee = zan_genmeta_expr_from_json(json_obj_get(j, "f"), arena);
        zan_ast_list_init(&n->call.args);
        zan_ast_list_init(&n->call.type_args);
        f = json_obj_get(j, "a");
        if (f && f->type == JSON_ARR)
            for (int i = 0; i < f->as.arr.count; i++)
                zan_ast_list_push(&n->call.args,
                                  zan_genmeta_expr_from_json(f->as.arr.items[i],
                                                             arena),
                                  arena);
        return n;
    }
    if (strcmp(k, "cond") == 0) {
        n = zan_ast_new(arena, AST_CONDITIONAL, loc);
        n->conditional.cond =
            zan_genmeta_expr_from_json(json_obj_get(j, "c"), arena);
        n->conditional.then_expr =
            zan_genmeta_expr_from_json(json_obj_get(j, "t"), arena);
        n->conditional.else_expr =
            zan_genmeta_expr_from_json(json_obj_get(j, "e"), arena);
        return n;
    }
    if (strcmp(k, "cast") == 0) {
        n = zan_ast_new(arena, AST_CAST_EXPR, loc);
        n->cast.type =
            gm_type_ref_from(json_get_str(json_obj_get(j, "t")), arena, loc);
        n->cast.expr =
            zan_genmeta_expr_from_json(json_obj_get(j, "e"), arena);
        return n;
    }
    if (strcmp(k, "is") == 0) {
        n = zan_ast_new(arena, AST_IS_EXPR, loc);
        n->type_test.expr =
            zan_genmeta_expr_from_json(json_obj_get(j, "e"), arena);
        n->type_test.type =
            gm_type_ref_from(json_get_str(json_obj_get(j, "t")), arena, loc);
        return n;
    }
    if (strcmp(k, "idx") == 0) {
        n = zan_ast_new(arena, AST_INDEX, loc);
        n->index.object =
            zan_genmeta_expr_from_json(json_obj_get(j, "o"), arena);
        n->index.index =
            zan_genmeta_expr_from_json(json_obj_get(j, "i"), arena);
        return n;
    }
    if (strcmp(k, "assign") == 0) {
        n = zan_ast_new(arena, AST_ASSIGNMENT, loc);
        n->binary.op = gm_token_kind(json_get_str(json_obj_get(j, "op")));
        n->binary.left =
            zan_genmeta_expr_from_json(json_obj_get(j, "l"), arena);
        n->binary.right =
            zan_genmeta_expr_from_json(json_obj_get(j, "r"), arena);
        return n;
    }
    if (strcmp(k, "new") == 0) {
        n = zan_ast_new(arena, AST_NEW_EXPR, loc);
        n->new_expr.type =
            gm_type_ref_from(json_get_str(json_obj_get(j, "t")), arena, loc);
        zan_ast_list_init(&n->new_expr.args);
        f = json_obj_get(j, "a");
        if (f && f->type == JSON_ARR)
            for (int i = 0; i < f->as.arr.count; i++)
                zan_ast_list_push(&n->new_expr.args,
                                  zan_genmeta_expr_from_json(f->as.arr.items[i],
                                                             arena),
                                  arena);
        n->new_expr.is_array = false;
        n->new_expr.array_init = false;
        return n;
    }
    if (strcmp(k, "block") == 0) {
        n = zan_ast_new(arena, AST_BLOCK, loc);
        zan_ast_list_init(&n->block.stmts);
        return n;
    }
    if (strcmp(k, "lam") == 0) {
        n = zan_ast_new(arena, AST_LAMBDA, loc);
        zan_ast_list_init(&n->lambda.params);
        f = json_obj_get(j, "p");
        if (f && f->type == JSON_ARR) {
            for (int i = 0; i < f->as.arr.count; i++) {
                const char *pn = json_get_str(f->as.arr.items[i]);
                if (!pn) continue;
                zan_ast_node_t *p = zan_ast_new(arena, AST_PARAM, loc);
                p->param.name.str = zan_arena_strdup(arena, pn, strlen(pn));
                p->param.name.len = (uint32_t)strlen(pn);
                p->param.type = NULL;
                p->param.default_val = NULL;
                p->param.is_params = 0;
                p->param.by_ref = 0;
                p->param.is_this = 0;
                zan_ast_list_push(&n->lambda.params, p, arena);
            }
        }
        n->lambda.body =
            zan_genmeta_expr_from_json(json_obj_get(j, "b"), arena);
        return n;
    }
    if (strcmp(k, "await") == 0) {
        n = zan_ast_new(arena, AST_AWAIT_EXPR, loc);
        n->await_expr.expr =
            zan_genmeta_expr_from_json(json_obj_get(j, "o"), arena);
        return n;
    }
    if (strcmp(k, "ref") == 0) {
        n = zan_ast_new(arena, AST_REF_ARG, loc);
        n->ref_arg.expr =
            zan_genmeta_expr_from_json(json_obj_get(j, "o"), arena);
        n->ref_arg.decl_type = NULL;
        n->ref_arg.is_out = 0;
        return n;
    }
    return NULL;
}
