static bool gm_name_in(const char *name, const char *const *names, int count) {
    if (!name || !names) return false;
    for (int i = 0; i < count; i++)
        if (strcmp(name, names[i]) == 0) return true;
    return false;
}

static json_value *gm_arg_json(zan_ast_node_t *n);

static void gm_istr_to(char *dst, size_t cap, zan_istr_t s) {
    size_t n = (s.str && s.len < cap - 1) ? s.len : (cap ? cap - 1 : 0);
    if (s.str && n) memcpy(dst, s.str, n);
    dst[n] = 0;
}

static bool gm_istr_is(zan_istr_t s, const char *lit) {
    size_t n = strlen(lit);
    return s.str && s.len == (uint32_t)n && memcmp(s.str, lit, n) == 0;
}

static size_t gm_snpcat(char *out, size_t cap, size_t pos,
                        const char *fmt, ...) {
    if (cap == 0) return 0;
    if (pos >= cap) pos = cap - 1;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(out + pos, cap - pos, fmt, ap);
    va_end(ap);
    if (w < 0 || (size_t)w >= cap - pos) return cap - 1;
    return pos + (size_t)w;
}

static void gm_type_str(zan_ast_node_t *t, char *out, size_t outsz) {
    if (!t) { out[0] = '\0'; return; }
    switch (t->kind) {
    case AST_TYPE_REF: {
        size_t n = gm_snpcat(out, outsz, 0, "%.*s",
                             (int)t->type_ref.name.len,
                             t->type_ref.name.str ? t->type_ref.name.str : "");
        if (t->type_ref.type_args.count > 0) {
            n = gm_snpcat(out, outsz, n, "<");
            for (int i = 0; i < t->type_ref.type_args.count; i++) {
                if (i) n = gm_snpcat(out, outsz, n, ",");
                char tmp[256];
                gm_type_str(t->type_ref.type_args.items[i], tmp, sizeof(tmp));
                n = gm_snpcat(out, outsz, n, "%s", tmp);
            }
            n = gm_snpcat(out, outsz, n, ">");
        }
        if (t->type_ref.is_array)
            n = gm_snpcat(out, outsz, n, "[]");
        if (t->type_ref.is_nullable)
            gm_snpcat(out, outsz, n, "?");
        break;
    }
    case AST_ARRAY_TYPE:
    case AST_NULLABLE_TYPE:
        out[0] = '\0';
        break;
    case AST_QUALIFIED_NAME: {
        size_t n = 0;
        for (int i = 0; i < t->qualified_name.parts.count; i++) {
            zan_ast_node_t *p = t->qualified_name.parts.items[i];
            if (!p || p->kind != AST_IDENTIFIER) continue;
            if (n) n = gm_snpcat(out, outsz, n, ".");
            n = gm_snpcat(out, outsz, n, "%.*s", (int)p->ident.name.len,
                          p->ident.name.str ? p->ident.name.str : "");
        }
        break;
    }
    default:
        out[0] = '\0';
        break;
    }
}

static void gm_attr_json(zan_ast_node_t *attr, json_value *obj) {
    char nm[256];
    nm[0] = '\0';
    if (attr->kind == AST_ATTRIBUTE) {
        if (attr->attribute.name->kind == AST_IDENTIFIER)
            snprintf(nm, sizeof(nm), "%.*s",
                     (int)attr->attribute.name->ident.name.len,
                     attr->attribute.name->ident.name.str);
        else
            gm_type_str(attr->attribute.name, nm, sizeof(nm));
        json_obj_set(obj, "name", json_new_str(nm));
        json_value *args = json_new_arr();
        for (int i = 0; i < attr->attribute.args.count; i++) {
            json_value *a = gm_arg_json(attr->attribute.args.items[i]);
            if (!a) a = json_new_str("?");
            json_arr_add(args, a);
        }
        json_obj_set(obj, "args", args);
    } else {
        json_obj_set(obj, "name", json_new_str("?"));
        json_obj_set(obj, "args", json_new_arr());
    }
}

static void gm_attrs_json(zan_ast_list_t *attrs, json_value *arr) {
    for (int i = 0; i < attrs->count; i++) {
        json_value *a = json_new_obj();
        gm_attr_json(attrs->items[i], a);
        json_arr_add(arr, a);
    }
}

static json_value *gm_expr_tree(zan_ast_node_t *n) {
    json_value *o = json_new_obj();
    if (!n) { json_obj_set(o, "k", json_new_str("null")); return o; }
    switch (n->kind) {
    case AST_INT_LITERAL:
        json_obj_set(o, "k", json_new_str("lit"));
        json_obj_set(o, "t", json_new_str("int"));
        json_obj_set(o, "v", json_new_num((double)n->int_val));
        break;
    case AST_FLOAT_LITERAL:
        json_obj_set(o, "k", json_new_str("lit"));
        json_obj_set(o, "t",
            json_new_str(n->lit_suffix ? "float" : "double"));
        json_obj_set(o, "v", json_new_num(n->float_val));
        break;
    case AST_DECIMAL_LITERAL:
        json_obj_set(o, "k", json_new_str("lit"));
        json_obj_set(o, "t", json_new_str("decimal"));
        json_obj_set(o, "v", json_new_str(n->str_val.str ? n->str_val.str : "0"));
        break;
    case AST_BOOL_LITERAL:
        json_obj_set(o, "k", json_new_str("lit"));
        json_obj_set(o, "t", json_new_str("bool"));
        json_obj_set(o, "v", json_new_bool(n->bool_val));
        break;
    case AST_CHAR_LITERAL:
        json_obj_set(o, "k", json_new_str("lit"));
        json_obj_set(o, "t", json_new_str("char"));
        json_obj_set(o, "v", json_new_num((double)n->int_val));
        break;
    case AST_STRING_LITERAL:
        json_obj_set(o, "k", json_new_str("lit"));
        json_obj_set(o, "t", json_new_str("str"));
        json_obj_set(o, "v", json_new_str(
            n->str_val.str ? (const char *)n->str_val.str : ""));
        break;
    case AST_NULL_LITERAL:
        json_obj_set(o, "k", json_new_str("null"));
        break;
    case AST_IDENTIFIER:
        json_obj_set(o, "k", json_new_str("id"));
        json_obj_set(o, "n", json_new_str(
            n->ident.name.str ? (const char *)n->ident.name.str : ""));
        break;
    case AST_THIS_EXPR:
        json_obj_set(o, "k", json_new_str("this"));
        break;
    case AST_BASE_EXPR:
        json_obj_set(o, "k", json_new_str("base"));
        break;
    case AST_BINARY:
        json_obj_set(o, "k", json_new_str("bin"));
        json_obj_set(o, "op", json_new_str(zan_token_kind_name(n->binary.op)));
        json_obj_set(o, "l", gm_expr_tree(n->binary.left));
        json_obj_set(o, "r", gm_expr_tree(n->binary.right));
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        json_obj_set(o, "k", json_new_str("uni"));
        json_obj_set(o, "op", json_new_str(zan_token_kind_name(n->unary.op)));
        json_obj_set(o, "o", gm_expr_tree(n->unary.operand));
        break;
    case AST_MEMBER_ACCESS: {
        json_obj_set(o, "k", json_new_str("mem"));
        json_obj_set(o, "o", gm_expr_tree(n->member.object));
        json_obj_set(o, "n", json_new_str(
            n->member.name.str ? (const char *)n->member.name.str : ""));
        break;
    }
    case AST_CALL: {
        json_obj_set(o, "k", json_new_str("call"));
        json_obj_set(o, "f", gm_expr_tree(n->call.callee));
        json_value *args = json_new_arr();
        for (int i = 0; i < n->call.args.count; i++)
            json_arr_add(args, gm_expr_tree(n->call.args.items[i]));
        json_obj_set(o, "a", args);
        break;
    }
    case AST_CONDITIONAL:
        json_obj_set(o, "k", json_new_str("cond"));
        json_obj_set(o, "c", gm_expr_tree(n->conditional.cond));
        json_obj_set(o, "t", gm_expr_tree(n->conditional.then_expr));
        json_obj_set(o, "e", gm_expr_tree(n->conditional.else_expr));
        break;
    case AST_CAST_EXPR:
        json_obj_set(o, "k", json_new_str("cast"));
        { char t[256];
          gm_type_str(n->cast.type, t, sizeof(t));
          json_obj_set(o, "t", json_new_str(t)); }
        json_obj_set(o, "e", gm_expr_tree(n->cast.expr));
        break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        json_obj_set(o, "k", json_new_str("is"));
        json_obj_set(o, "e", gm_expr_tree(n->type_test.expr));
        { char t[256];
          gm_type_str(n->type_test.type, t, sizeof(t));
          json_obj_set(o, "t", json_new_str(t)); }
        break;
    case AST_INDEX:
        json_obj_set(o, "k", json_new_str("idx"));
        json_obj_set(o, "o", gm_expr_tree(n->index.object));
        json_obj_set(o, "i", gm_expr_tree(n->index.index));
        break;
    case AST_ASSIGNMENT:
        json_obj_set(o, "k", json_new_str("assign"));
        json_obj_set(o, "op", json_new_str(zan_token_kind_name(n->binary.op)));
        json_obj_set(o, "l", gm_expr_tree(n->binary.left));
        json_obj_set(o, "r", gm_expr_tree(n->binary.right));
        break;
    case AST_NEW_EXPR:
        json_obj_set(o, "k", json_new_str("new"));
        { char t[256];
          gm_type_str(n->new_expr.type, t, sizeof(t));
          json_obj_set(o, "t", json_new_str(t));
          json_value *args = json_new_arr();
          for (int i = 0; i < n->new_expr.args.count; i++)
              json_arr_add(args, gm_expr_tree(n->new_expr.args.items[i]));
          json_obj_set(o, "a", args); }
        break;
    case AST_BLOCK:
        json_obj_set(o, "k", json_new_str("block"));
        break;
    case AST_LAMBDA: {
        json_obj_set(o, "k", json_new_str("lam"));
        json_value *ps = json_new_arr();
        for (int i = 0; i < n->lambda.params.count; i++) {
            zan_ast_node_t *p = n->lambda.params.items[i];
            if (p->kind == AST_PARAM)
                json_arr_add(ps, json_new_str(
                    p->param.name.str ? (const char *)p->param.name.str : ""));
        }
        json_obj_set(o, "p", ps);
        json_obj_set(o, "b", gm_expr_tree(n->lambda.body));
        break;
    }
    case AST_AWAIT_EXPR:
        json_obj_set(o, "k", json_new_str("await"));
        json_obj_set(o, "o", gm_expr_tree(n->await_expr.expr));
        break;
    case AST_REF_ARG:
        json_obj_set(o, "k", json_new_str("ref"));
        json_obj_set(o, "o", gm_arg_json(n->ref_arg.expr));
        break;
    default:
        json_obj_set(o, "k", json_new_str("other"));
        break;
    }
    return o;
}

static json_value *gm_arg_json(zan_ast_node_t *n) {
    if (!n) { json_value *o = json_new_obj();
              json_obj_set(o, "k", json_new_str("null")); return o; }
    return gm_expr_tree(n);
}

static void gm_recv_str(zan_ast_node_t *obj, char *out, size_t outsz) {
    if (!obj) { snprintf(out, outsz, "other"); return; }
    switch (obj->kind) {
    case AST_THIS_EXPR: snprintf(out, outsz, "this"); break;
    case AST_IDENTIFIER:
        snprintf(out, outsz, "id:%.*s", (int)obj->ident.name.len,
                 obj->ident.name.str);
        break;
    case AST_MEMBER_ACCESS: {
        char pre[128];
        gm_recv_str(obj->member.object, pre, sizeof(pre));
        snprintf(out, outsz, "mem:%s.%.*s", pre, (int)obj->member.name.len,
                 obj->member.name.str);
        break;
    }
    case AST_CALL: snprintf(out, outsz, "call"); break;
    case AST_BASE_EXPR: snprintf(out, outsz, "base"); break;
    default: snprintf(out, outsz, "other"); break;
    }
}

static int gm_recv_reserve(zan_ast_node_t *recv, gm_ctx_t *c) {
    for (int i = 0; i < c->pend_count; i++)
        if (c->pend_nodes[i] == recv) return c->pend_ids[i];
    if (c->pend_count >= c->pend_cap) {
        int ncap = c->pend_cap ? c->pend_cap * 2 : 64;
        zan_ast_node_t **nn = (zan_ast_node_t **)realloc(
            c->pend_nodes, (size_t)ncap * sizeof(*nn));
        if (nn) c->pend_nodes = nn;
        int *ni = nn ? (int *)realloc(c->pend_ids, (size_t)ncap * sizeof(*ni))
                     : NULL;
        if (!nn || !ni) return 0;
        c->pend_ids = ni;
        c->pend_cap = ncap;
    }
    c->pend_nodes[c->pend_count] = recv;
    c->pend_ids[c->pend_count] = ++c->call_id;
    return c->pend_ids[c->pend_count++];
}

/* 查找the id a call node was recorded with (children-first walk) */
static int gm_rec_lookup(zan_ast_node_t *call, gm_ctx_t *c) {
    for (int i = 0; i < c->rec_count; i++)
        if (c->rec_nodes[i] == call) return c->rec_ids[i];
    return 0;
}

static bool gm_is_candidate_ast_call(zan_ast_node_t *call, gm_ctx_t *c) {
    zan_ast_node_t *callee = call->call.callee;
    if (!callee) return false;
    const char *name = NULL;
    if (callee->kind == AST_MEMBER_ACCESS) {
        if (callee->member.name.str) name = callee->member.name.str;
    } else if (callee->kind == AST_IDENTIFIER) {
        if (callee->ident.name.str) name = callee->ident.name.str;
    }

    for (int i = 0; i < c->pend_count; i++) {
        if (c->pend_nodes[i] == call) return true;
    }

    /* 2 */
    if (call->call.type_args.count > 0) return true;

    zan_ast_node_t *robj = (callee->kind == AST_MEMBER_ACCESS)
                               ? callee->member.object
                               : NULL;
    /* 3 */
    if (robj && robj->kind == AST_CALL) {
        if (gm_rec_lookup(robj, c) != 0) return true;
    }

    /* 4 */
    if (name && gm_name_in(name, c->expr_names, c->expr_count))
        return true;

    if (robj && robj->kind == AST_MEMBER_ACCESS && robj->member.name.str &&
        gm_name_in(robj->member.name.str, c->table_names, c->table_count))
        return true;

    return false;
}

static void gm_record_call(zan_ast_node_t *call, gm_ctx_t *c) {
    zan_ast_node_t *callee = call->call.callee;
    int id = 0;
    for (int i = 0; i < c->pend_count; i++) {
        if (c->pend_nodes[i] == call) {
            id = c->pend_ids[i];
            c->pend_nodes[i] = c->pend_nodes[c->pend_count - 1];
            c->pend_ids[i] = c->pend_ids[c->pend_count - 1];
            c->pend_count--;
            break;
        }
    }
    if (!id) id = ++c->call_id;

    if (c->rec_count >= c->rec_cap) {
        int ncap = c->rec_cap ? c->rec_cap * 2 : 256;
        zan_ast_node_t **nn =
            (zan_ast_node_t **)realloc(c->rec_nodes, (size_t)ncap * sizeof(*nn));
        int *ni = (int *)realloc(c->rec_ids, (size_t)ncap * sizeof(*ni));
        if (nn && ni) {
            c->rec_nodes = nn;
            c->rec_ids = ni;
            c->rec_cap = ncap;
        }
    }
    if (c->rec_count < c->rec_cap) {
        c->rec_nodes[c->rec_count] = call;
        c->rec_ids[c->rec_count] = id;
        c->rec_count++;
    }

    if (!gm_is_candidate_ast_call(call, c)) {
        return;
    }

    json_value *o = json_new_obj();
    json_obj_set(o, "id", json_new_num((double)id));
    json_obj_set(o, "file", json_new_num((double)call->loc.file_id));
    json_obj_set(o, "line", json_new_num((double)call->loc.line));
    json_obj_set(o, "col", json_new_num((double)call->loc.col));
    json_obj_set(o, "name", json_new_str(
        callee->kind == AST_MEMBER_ACCESS
            ? (callee->member.name.str
                   ? (const char *)callee->member.name.str : "")
            : (callee->ident.name.str ? (const char *)callee->ident.name.str
                                      : "")));
    json_obj_set(o, "cls", json_new_str(c->cur_cls));
    json_obj_set(o, "fn", json_new_str(c->cur_fn));
    zan_ast_node_t *robj = (callee->kind == AST_MEMBER_ACCESS)
                               ? callee->member.object
                               : NULL;
    int recv_id = 0;
    char recv[160];
    if (robj && robj->kind == AST_CALL) {
        recv_id = gm_rec_lookup(robj, c);
        if (!recv_id) recv_id = gm_recv_reserve(robj, c);
        snprintf(recv, sizeof(recv), "call#%d", recv_id);
    } else if (robj) {
        gm_recv_str(robj, recv, sizeof(recv));
    } else {
        snprintf(recv, sizeof(recv), "other");
    }
    json_obj_set(o, "recv", json_new_str(recv));
    json_obj_set(o, "recv_id", json_new_num((double)recv_id));
    json_obj_set(o, "recvx", gm_expr_tree(robj));
    json_value *targs = json_new_arr();
    for (int i = 0; i < call->call.type_args.count; i++) {
        char t[256];
        gm_type_str(call->call.type_args.items[i], t, sizeof(t));
        json_arr_add(targs, json_new_str(t));
    }
    json_obj_set(o, "targs", targs);
    json_value *args = json_new_arr();
    for (int i = 0; i < call->call.args.count; i++)
        json_arr_add(args, gm_arg_json(call->call.args.items[i]));
    json_obj_set(o, "args", args);
    json_arr_add(c->calls, o);
}

static const char **gm_table_entity_names(json_value *classes, int *out_count) {
    const char **names = NULL;
    int count = 0, cap = 0;
    *out_count = 0;
    if (!classes || classes->type != JSON_ARR) return NULL;
    for (int ci = 0; ci < classes->as.arr.count; ci++) {
        json_value *cls = classes->as.arr.items[ci];
        json_value *attrs = json_obj_get(cls, "attrs");
        if (!attrs || attrs->type != JSON_ARR) continue;
        bool table = false;
        for (int ai = 0; ai < attrs->as.arr.count; ai++) {
            const char *an =
                json_get_str(json_obj_get(attrs->as.arr.items[ai], "name"));
            if (an && strcmp(an, "Table") == 0) {
                table = true;
                break;
            }
        }
        if (!table) continue;
        const char *name = json_get_str(json_obj_get(cls, "name"));
        if (!name || !name[0] || gm_name_in(name, names, count)) continue;
        if (count >= cap) {
            int next = cap ? cap * 2 : 8;
            const char **grown =
                (const char **)realloc(names,
                                       sizeof(const char *) * (size_t)next);
            if (!grown) zan_host_oom();
            names = grown;
            cap = next;
        }
        names[count++] = name;
    }
    *out_count = count;
    return names;
}

static bool gm_is_codegen_seed(json_value *call, const char *const *expr_names,
                               int expr_count, const char *const *table_names,
                               int table_count) {
    const char *name = json_get_str(json_obj_get(call, "name"));
    if (name && gm_name_in(name, expr_names, expr_count)) return true;
    {
        json_value *recvx = json_obj_get(call, "recvx");
        if (recvx && recvx->type == JSON_OBJ) {
            const char *k = json_get_str(json_obj_get(recvx, "k"));
            const char *n = json_get_str(json_obj_get(recvx, "n"));
            if (k && strcmp(k, "mem") == 0 &&
                gm_name_in(n, table_names, table_count))
                return true;
        }
    }
    return false;
}

static const char **gm_expr_method_names(json_value *classes, int *out_count) {
    const char **names = NULL;
    int count = 0, cap = 0;
    if (!classes || classes->type != JSON_ARR) {
        *out_count = 0;
        return NULL;
    }
    for (int ci = 0; ci < classes->as.arr.count; ci++) {
        json_value *methods =
            json_obj_get(classes->as.arr.items[ci], "methods");
        if (!methods || methods->type != JSON_ARR) continue;
        for (int mi = 0; mi < methods->as.arr.count; mi++) {
            json_value *method = methods->as.arr.items[mi];
            json_value *params = json_obj_get(method, "params");
            if (!params || params->type != JSON_ARR) continue;
            bool has_expr = false;
            for (int pi = 0; pi < params->as.arr.count; pi++) {
                const char *type =
                    json_get_str(json_obj_get(params->as.arr.items[pi],
                                              "type"));
                size_t n = type ? strlen(type) : 0;
                if (n > 6 && strncmp(type, "Expr<", 5) == 0 &&
                    type[n - 1] == '>') {
                    has_expr = true;
                    break;
                }
            }
            if (!has_expr) continue;
            const char *name = json_get_str(json_obj_get(method, "name"));
            if (!name || gm_name_in(name, names, count)) continue;
            if (count >= cap) {
                int next = cap ? cap * 2 : 8;
                const char **grown =
                    (const char **)realloc(names,
                                          sizeof(const char *) * (size_t)next);
                if (!grown) zan_host_oom();
                names = grown;
                cap = next;
            }
            names[count++] = name;
        }
    }
    *out_count = count;
    return names;
}

static void gm_prune_calls(json_value *calls, const char *const *expr_names,
                           int expr_count, const char *const *table_names,
                           int table_count) {
    if (!calls || calls->type != JSON_ARR || calls->as.arr.count == 0) return;
    int max_id = 0;
    for (int i = 0; i < calls->as.arr.count; i++) {
        int id = (int)json_get_num(json_obj_get(calls->as.arr.items[i], "id"),
                                   0);
        if (id > max_id) max_id = id;
    }
    bool *keep = (bool *)calloc((size_t)max_id + 1, sizeof(bool));
    if (!keep) zan_host_oom();
    for (int i = 0; i < calls->as.arr.count; i++) {
        json_value *call = calls->as.arr.items[i];
        int id = (int)json_get_num(json_obj_get(call, "id"), 0);
        if (id > 0 && gm_is_codegen_seed(call, expr_names, expr_count,
                                         table_names, table_count))
            keep[id] = true;
    }

    bool changed;
    do {
        changed = false;
        for (int i = 0; i < calls->as.arr.count; i++) {
            json_value *call = calls->as.arr.items[i];
            int id =
                (int)json_get_num(json_obj_get(call, "id"), 0);
            int recv_id =
                (int)json_get_num(json_obj_get(call, "recv_id"), 0);
            if (id <= 0 || id > max_id || recv_id <= 0 ||
                recv_id > max_id)
                continue;
            if (keep[id] && !keep[recv_id]) {
                keep[recv_id] = true;
                changed = true;
            }
            if (keep[recv_id] && !keep[id]) {
                keep[id] = true;
                changed = true;
            }
        }
    } while (changed);

    int out = 0;
    for (int i = 0; i < calls->as.arr.count; i++) {
        json_value *call = calls->as.arr.items[i];
        int id = (int)json_get_num(json_obj_get(call, "id"), 0);
        if (id > 0 && id <= max_id && keep[id])
            calls->as.arr.items[out++] = call;
        else
            json_free(call);
    }
    calls->as.arr.count = out;
    free(keep);
}

static void gm_walk_expr(zan_ast_node_t *n, gm_ctx_t *c) {
