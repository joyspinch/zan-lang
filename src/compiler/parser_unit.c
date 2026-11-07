zan_ast_node_t *zan_parser_parse(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    zan_ast_node_t *unit = zan_ast_new(p->arena, AST_COMPILATION_UNIT, loc);
    zan_ast_list_init(&unit->comp_unit.usings);
    zan_ast_list_init(&unit->comp_unit.decls);
    unit->comp_unit.ns = NULL;

    while (parser_check(p, TK_USING)) {
        zan_ast_node_t *u = parse_using_decl(p);
        zan_ast_list_push(&unit->comp_unit.usings, u, p->arena);
    }

    if (parser_check(p, TK_NAMESPACE)) {
        zan_loc_t ns_loc = p->current.loc;
        parser_advance(p);
        zan_ast_node_t *ns_name = parse_qualified_name(p);

        zan_ast_node_t *ns = zan_ast_new(p->arena, AST_NAMESPACE_DECL, ns_loc);
        ns->namespace_decl.name = ns_name;
        zan_ast_list_init(&ns->namespace_decl.members);

        if (parser_match(p, TK_SEMICOLON)) {
            ns->namespace_decl.is_file_scoped = true;
        } else {
            parser_expect(p, TK_LBRACE);
            ns->namespace_decl.is_file_scoped = false;
        }

        unit->comp_unit.ns = ns;

        /* Block-scoped `namespace X { */
        if (!ns->namespace_decl.is_file_scoped) {
            for (;;) {
                if (parser_match(p, TK_RBRACE)) break;
                if (parser_check(p, TK_EOF)) {
                    zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 54,
                                  "unexpected end of file inside namespace block; missing '}'");
                    break;
                }
                if (!parse_top_level_decl(p, unit))
                    parser_advance(p); /* skip to recover */
            }
        }
    }

    while (!parser_check(p, TK_EOF) && !parser_check(p, TK_RBRACE)) {
        if (!parse_top_level_decl(p, unit))
            parser_advance(p); /* skip to recover */
        if (parser_check(p, TK_RBRACE)) {
            /* 顶层游离右大括号容错：报告语法错误并跳过以继续解析后续声明 */
            zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 55,
                          "unexpected '}' at top level");
            parser_advance(p);
        }
    }

    /* A381 lazy iterators：IEnumerable<T>+yield 的方法合成迭代器类 */
    iter_desugar_unit(p, unit);

    return unit;
}


static zan_ast_node_t *find_delegate_decl(zan_ast_node_t *unit, zan_istr_t name) {
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (d->kind == AST_DELEGATE_DECL &&
            d->method_decl.name.len == name.len &&
            memcmp(d->method_decl.name.str, name.str, (size_t)name.len) == 0)
            return d;
    }
    return NULL;
}

#define ZAN_GEN_SRC_CAP (256 * 1024)

static void zsrc_append(char *buf, int cap, int *off, const char *fmt, ...) {
    if (*off >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + *off, (size_t)(cap - *off), fmt, ap);
    va_end(ap);
    if (w < 0) return;
    *off += w;
    if (*off > cap) *off = cap;
}

static int tref_write(char *buf, int cap, zan_ast_node_t *t) {
    int n = 0;
    if (!t || t->kind != AST_TYPE_REF || cap <= 0) return 0;
    zsrc_append(buf, cap, &n, "%.*s",
                     (int)t->type_ref.name.len, t->type_ref.name.str);
    if (t->type_ref.type_args.count > 0) {
        zsrc_append(buf, cap, &n, "<");
        for (int i = 0; i < t->type_ref.type_args.count; i++) {
            if (i > 0) zsrc_append(buf, cap, &n, ", ");
            n += tref_write(buf + n, cap - n, t->type_ref.type_args.items[i]);
        }
        zsrc_append(buf, cap, &n, ">");
    }
    if (t->type_ref.is_array) zsrc_append(buf, cap, &n, "[]");
    if (t->type_ref.is_nullable) zsrc_append(buf, cap, &n, "?");
    return n;
}

static void gen_event_holder(zan_ast_node_t *unit, zan_ast_node_t *ddecl,
                             const char *dname, const char *hname,
                             zan_arena_t *arena, zan_diag_t *diag) {
    char *src = (char *)malloc(ZAN_GEN_SRC_CAP);
    if (!src) return;
    const int cap = ZAN_GEN_SRC_CAP;
    int n = 0;
    zsrc_append(src, cap, &n,
        "class %s {\n"
        "    List<%s> hs;\n"
        "    static %s op_add(%s self, %s h) {\n"
        "        %s r = self;\n"
        "        if (r == null) { r = new %s(); r.hs = new List<%s>(); }\n"
        "        r.hs.Add(h);\n"
        "        return r;\n"
        "    }\n"
        "    static %s op_sub(%s self, %s h) {\n"
        "        if (self == null) { return self; }\n"
        "        int i = self.hs.Count - 1;\n"
        "        while (i >= 0) {\n"
        "            if (self.hs[i] == h) { self.hs.RemoveAt(i); return self; }\n"
        "            i = i - 1;\n"
        "        }\n"
        "        return self;\n"
        "    }\n"
        "    int Count() { return hs.Count; }\n"
        "    void Invoke(",
        hname, dname, hname, hname, dname, hname, hname, dname,
        hname, hname, dname);
    int invoke_params_at = n;
    for (int i = 0; i < ddecl->method_decl.params.count; i++) {
        zan_ast_node_t *pp = ddecl->method_decl.params.items[i];
        if (i > 0) zsrc_append(src, cap, &n, ", ");
        n += tref_write(src + n, cap - n, pp->param.type);
        zsrc_append(src, cap, &n, " a%d", i);
    }
    int invoke_params_end = n;
    zsrc_append(src, cap, &n,
        ") {\n"
        "        int i = 0;\n"
        "        while (i < hs.Count) {\n"
        "            %s d = hs[i];\n"
        "            d(", dname);
    for (int i = 0; i < ddecl->method_decl.params.count; i++) {
        zsrc_append(src, cap, &n, "%sa%d",
                         i > 0 ? ", " : "", i);
    }
    zsrc_append(src, cap, &n,
        ");\n"
        "            i = i + 1;\n"
        "        }\n"
        "    }\n");

    int params_len = invoke_params_end - invoke_params_at;
    char *params = (char *)malloc((size_t)params_len + 1);
    if (!params) { free(src); return; }
    memcpy(params, src + invoke_params_at, (size_t)params_len);
    params[params_len] = '\0';
    zsrc_append(src, cap, &n,
        "    static void op_call(%s self%s%s) {\n"
        "        if (self == null) { return; }\n"
        "        self.Invoke(",
        hname, ddecl->method_decl.params.count ? ", " : "", params);
    for (int i = 0; i < ddecl->method_decl.params.count; i++)
        zsrc_append(src, cap, &n, "%sa%d", i > 0 ? ", " : "", i);
    zsrc_append(src, cap, &n,
        ");\n"
        "    }\n"
        "}\n");
    free(params);

    if (n >= cap) {
        zan_loc_t loc = {0};
        zan_diag_emit_code(diag, DIAG_ERROR, loc, 56,
                      "declaration too large to lower: generated source "
                      "exceeds %d bytes", cap);
        free(src);
        return;
    }
    char *gsrc = zan_arena_strdup(arena, src, (size_t)n);
    free(src);
    zan_lexer_t lex;
    zan_lexer_init(&lex, gsrc, (size_t)n, 0, arena, diag);
    zan_parser_t gp;
    zan_parser_init(&gp, &lex, arena, diag);
    zan_ast_node_t *gu = zan_parser_parse(&gp);
    for (int i = 0; i < gu->comp_unit.decls.count; i++)
        zan_ast_list_push(&unit->comp_unit.decls, gu->comp_unit.decls.items[i],
                          arena);
}

static void gen_record_class(zan_ast_node_t *unit, zan_istr_t rname,
                             zan_ast_list_t *params, zan_arena_t *arena,
                             zan_diag_t *diag) {
    char *src = (char *)malloc(ZAN_GEN_SRC_CAP);
    if (!src) return;
    const int cap = ZAN_GEN_SRC_CAP;
    char nm[256];
    int n = 0;
    snprintf(nm, sizeof(nm), "%.*s", (int)rname.len, rname.str);
    zsrc_append(src, cap, &n, "class %s {\n", nm);
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *pp = params->items[i];
        zsrc_append(src, cap, &n, "    public ");
        n += tref_write(src + n, cap - n, pp->param.type);
        zsrc_append(src, cap, &n, " %.*s;\n",
                         (int)pp->param.name.len, pp->param.name.str);
    }
    zsrc_append(src, cap, &n, "    public %s(", nm);
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *pp = params->items[i];
        if (i > 0) zsrc_append(src, cap, &n, ", ");
        n += tref_write(src + n, cap - n, pp->param.type);
        zsrc_append(src, cap, &n, " %.*s",
                         (int)pp->param.name.len, pp->param.name.str);
    }
    zsrc_append(src, cap, &n, ") {\n");
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *pp = params->items[i];
        zsrc_append(src, cap, &n,
                         "        this.%.*s = %.*s;\n",
                         (int)pp->param.name.len, pp->param.name.str,
                         (int)pp->param.name.len, pp->param.name.str);
    }
    zsrc_append(src, cap, &n, "    }\n");
    zsrc_append(src, cap, &n, "    public %s __CloneWith(", nm);
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *pp = params->items[i];
        if (i > 0) zsrc_append(src, cap, &n, ", ");
        n += tref_write(src + n, cap - n, pp->param.type);
        zsrc_append(src, cap, &n, " %.*s",
                         (int)pp->param.name.len, pp->param.name.str);
    }
    zsrc_append(src, cap, &n, ") {\n        return new %s(", nm);
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *pp = params->items[i];
        if (i > 0) zsrc_append(src, cap, &n, ", ");
        zsrc_append(src, cap, &n, "%.*s",
                         (int)pp->param.name.len, pp->param.name.str);
    }
    zsrc_append(src, cap, &n, ");\n    }\n");
    zsrc_append(src, cap, &n,
        "    static bool op_eq(%s l, %s r) {\n"
        "        if (l == null) { return r == null; }\n"
        "        if (r == null) { return false; }\n"
        "        return true", nm, nm);
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *pp = params->items[i];
        zsrc_append(src, cap, &n,
                         " && l.%.*s == r.%.*s",
                         (int)pp->param.name.len, pp->param.name.str,
                         (int)pp->param.name.len, pp->param.name.str);
    }
    zsrc_append(src, cap, &n,
        ";\n"
        "    }\n"
        "    static bool op_neq(%s l, %s r) { return !(l == r); }\n"
        "    public string ToString() {\n"
        "        return \"%s {\"", nm, nm, nm);
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *pp = params->items[i];
        zan_ast_node_t *pt = pp->param.type;
        int is_str = pt && pt->kind == AST_TYPE_REF && !pt->type_ref.is_array &&
                     pt->type_ref.name.len == 6 &&
                     memcmp(pt->type_ref.name.str, "string", 6) == 0;
        zsrc_append(src, cap, &n,
                         " + \"%s %.*s = \" + %s%.*s%s",
                         i > 0 ? "," : "",
                         (int)pp->param.name.len, pp->param.name.str,
                         is_str ? "" : "Convert.ToString(",
                         (int)pp->param.name.len, pp->param.name.str,
                         is_str ? "" : ")");
    }
    zsrc_append(src, cap, &n,
        " + \" }\";\n"
        "    }\n"
        "}\n");

    if (n >= cap) {
        zan_loc_t loc = {0};
        zan_diag_emit_code(diag, DIAG_ERROR, loc, 56,
                      "declaration too large to lower: generated source "
                      "exceeds %d bytes", cap);
        free(src);
        return;
    }
    char *gsrc = zan_arena_strdup(arena, src, (size_t)n);
    free(src);
    zan_lexer_t lex;
    zan_lexer_init(&lex, gsrc, (size_t)n, 0, arena, diag);
    zan_parser_t gp;
    zan_parser_init(&gp, &lex, arena, diag);
    zan_ast_node_t *gu = zan_parser_parse(&gp);
    for (int i = 0; i < gu->comp_unit.decls.count; i++)
        zan_ast_list_push(&unit->comp_unit.decls, gu->comp_unit.decls.items[i],
                          arena);
}

void zan_parser_merge_partials(zan_ast_node_t *unit, zan_arena_t *arena,
                               zan_diag_t *diag) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    zan_ast_list_t *decls = &unit->comp_unit.decls;
    int out = 0;
    for (int di = 0; di < decls->count; di++) {
        zan_ast_node_t *d = decls->items[di];
        zan_ast_node_t *first = NULL;
        if ((d->kind == AST_CLASS_DECL || d->kind == AST_STRUCT_DECL ||
             d->kind == AST_INTERFACE_DECL) &&
            (d->type_decl.modifiers & MOD_PARTIAL)) {
            for (int fi = 0; fi < out; fi++) {
                zan_ast_node_t *e = decls->items[fi];
                if (e->kind == d->kind &&
                    (e->type_decl.modifiers & MOD_PARTIAL) &&
                    e->type_decl.name.len == d->type_decl.name.len &&
                    memcmp(e->type_decl.name.str, d->type_decl.name.str,
                           (size_t)d->type_decl.name.len) == 0) {
                    first = e;
                    break;
                }
            }
        }
        if (first) {
            for (int mi = 0; mi < d->type_decl.members.count; mi++)
                zan_ast_list_push(&first->type_decl.members,
                                  d->type_decl.members.items[mi], arena);
            for (int bi = 0; bi < d->type_decl.bases.count; bi++)
                zan_ast_list_push(&first->type_decl.bases,
                                  d->type_decl.bases.items[bi], arena);
            if (d->type_decl.where_clauses && d->type_decl.where_clauses->count > 0) {
                if (!first->type_decl.where_clauses) {
                    first->type_decl.where_clauses = (zan_ast_list_t *)zan_arena_alloc(
                        arena, sizeof(zan_ast_list_t));
                    zan_ast_list_init(first->type_decl.where_clauses);
                }
                for (int wi = 0; wi < d->type_decl.where_clauses->count; wi++)
                    zan_ast_list_push(first->type_decl.where_clauses,
                                      d->type_decl.where_clauses->items[wi], arena);
            }
        } else {
            decls->items[out++] = d;
        }
    }
    decls->count = out;
    (void)diag;
}

static int hoist_nested_types(zan_ast_node_t *unit, zan_ast_node_t *type_node,
                              zan_ast_list_t *decls, zan_arena_t *arena) {
    int hoisted = 0;
    zan_ast_list_t *members = &type_node->type_decl.members;
    int m = members->count;
    int mi = 0;
    while (mi < m) {
        zan_ast_node_t *mem = members->items[mi];
        if (mem->kind == AST_DELEGATE_DECL) {
            zan_ast_list_push(decls, mem, arena);
            hoisted++;
            for (int k = mi; k < m - 1; k++) members->items[k] = members->items[k + 1];
            members->count--;
            m--;
        } else if (mem->kind == AST_CLASS_DECL || mem->kind == AST_STRUCT_DECL ||
                   mem->kind == AST_INTERFACE_DECL || mem->kind == AST_ENUM_DECL) {
            hoisted += hoist_nested_types(unit, mem, decls, arena);
            mem->type_decl.nested_host = type_node;
            zan_ast_list_push(decls, mem, arena);
            hoisted++;
            for (int k = mi; k < m - 1; k++) members->items[k] = members->items[k + 1];
            members->count--;
            m--;
        } else {
            mi++;
        }
    }
    return hoisted;
}

void zan_parser_flatten_nested_types(zan_ast_node_t *unit, zan_arena_t *arena,
                                     zan_diag_t *diag) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    zan_ast_list_t *decls = &unit->comp_unit.decls;
    int n = decls->count;
    for (int di = 0; di < n; di++) {
        zan_ast_node_t *d = decls->items[di];
        if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL &&
            d->kind != AST_INTERFACE_DECL && d->kind != AST_ENUM_DECL) continue;
        hoist_nested_types(unit, d, decls, arena);
    }
    (void)diag;
}

void zan_parser_desugar_events(zan_ast_node_t *unit, zan_arena_t *arena,
                               zan_diag_t *diag) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    const char **generated = NULL;
    int generated_count = 0;
    int generated_cap = 0;
    int decl_count = unit->comp_unit.decls.count;
    for (int di = 0; di < decl_count; di++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[di];
        if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL) continue;
        for (int mi = 0; mi < d->type_decl.members.count; mi++) {
            zan_ast_node_t *m = d->type_decl.members.items[mi];
            if (m->kind != AST_FIELD_DECL || !(m->field_decl.modifiers & MOD_EVENT))
                continue;
            zan_ast_node_t *tr = m->field_decl.type;
            if (!tr || tr->kind != AST_TYPE_REF || tr->type_ref.type_args.count) {
                zan_diag_emit_code(diag, DIAG_ERROR, m->loc, 57,
                              "event field requires a non-generic delegate type");
                continue;
            }
            zan_ast_node_t *ddecl = find_delegate_decl(unit, tr->type_ref.name);
            if (!ddecl) {
                zan_diag_emit_code(diag, DIAG_ERROR, m->loc, 58,
                              "event type '%.*s' is not a declared delegate",
                              (int)tr->type_ref.name.len, tr->type_ref.name.str);
                continue;
            }
            char dname[256], hname[280];
            snprintf(dname, sizeof(dname), "%.*s",
                     (int)tr->type_ref.name.len, tr->type_ref.name.str);
            snprintf(hname, sizeof(hname), "__Event_%s", dname);
            int already = 0;
            for (int gi = 0; gi < generated_count; gi++)
                if (strcmp(generated[gi], hname) == 0) { already = 1; break; }
            if (!already) {
                if (generated_count == generated_cap) {
                    int ncap = generated_cap ? generated_cap * 2 : 16;
                    const char **ng = (const char **)realloc(
                        (void *)generated, (size_t)ncap * sizeof(*generated));
                    if (!ng) {
                        free((void *)generated);
                        return;
                    }
                    generated = ng;
                    generated_cap = ncap;
                }
                gen_event_holder(unit, ddecl, dname, hname, arena, diag);
                generated[generated_count++] =
                    zan_arena_strdup(arena, hname, strlen(hname));
            }
            char *hn = zan_arena_strdup(arena, hname, strlen(hname));
            tr->type_ref.name = (zan_istr_t){hn, (uint32_t)strlen(hname)};
        }
    }
    free((void *)generated);
}

typedef struct {
    zan_ast_list_t *tparams;
    zan_ast_list_t *targs;
    zan_arena_t *arena;
    int depth;
    zan_diag_t *diag;
} type_subst_ctx_t;

static zan_ast_node_t *clone_ast_subst_impl(zan_ast_node_t *node, type_subst_ctx_t *ctx);

static zan_ast_node_t *clone_ast_subst(zan_ast_node_t *node, type_subst_ctx_t *ctx) {
    if (!node) return NULL;
    if (ctx->depth > 64) return node;
    ctx->depth++;
    zan_ast_node_t *res = clone_ast_subst_impl(node, ctx);
    ctx->depth--;
    return res;
}

static zan_ast_node_t *clone_ast_subst_impl(zan_ast_node_t *node, type_subst_ctx_t *ctx) {
    if (!node) return NULL;
    switch (node->kind) {
    case AST_TYPE_REF: {
        for (int i = 0; i < ctx->tparams->count && i < ctx->targs->count; i++) {
            zan_ast_node_t *tp = ctx->tparams->items[i];
            if (tp && tp->kind == AST_IDENTIFIER &&
                tp->ident.name.len == node->type_ref.name.len &&
                memcmp(tp->ident.name.str, node->type_ref.name.str, (size_t)node->type_ref.name.len) == 0) {
                zan_ast_node_t *ta = ctx->targs->items[i];
                zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_TYPE_REF, node->loc);
                *res = *ta;
                res->type_ref.is_array = node->type_ref.is_array || ta->type_ref.is_array;
                res->type_ref.is_nullable = node->type_ref.is_nullable || ta->type_ref.is_nullable;
                return res;
            }
        }
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_TYPE_REF, node->loc);
        *res = *node;
        zan_ast_list_init(&res->type_ref.type_args);
        for (int i = 0; i < node->type_ref.type_args.count; i++) {
            zan_ast_list_push(&res->type_ref.type_args,
                              clone_ast_subst(node->type_ref.type_args.items[i], ctx),
                              ctx->arena);
        }
        return res;
    }
    case AST_IDENTIFIER: {
        for (int i = 0; i < ctx->tparams->count && i < ctx->targs->count; i++) {
            zan_ast_node_t *tp = ctx->tparams->items[i];
            if (tp && tp->kind == AST_IDENTIFIER &&
                tp->ident.name.len == node->ident.name.len &&
                memcmp(tp->ident.name.str, node->ident.name.str, (size_t)node->ident.name.len) == 0) {
                zan_ast_node_t *ta = ctx->targs->items[i];
                zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_IDENTIFIER, node->loc);
                *res = *node;
                res->ident.name = ta->type_ref.name;
                return res;
            }
        }
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_IDENTIFIER, node->loc);
        *res = *node;
        return res;
    }
    case AST_CALL: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_CALL, node->loc);
        *res = *node;
        res->call.callee = clone_ast_subst(node->call.callee, ctx);
        zan_ast_list_init(&res->call.args);
        for (int i = 0; i < node->call.args.count; i++)
            zan_ast_list_push(&res->call.args, clone_ast_subst(node->call.args.items[i], ctx), ctx->arena);
        zan_ast_list_init(&res->call.type_args);
        if (node->call.type_args.count > 0) {
            zan_ast_node_t *ta0 = node->call.type_args.items[0];
            zan_compile_trace("clone_ast_subst CALL type_args count=%d kind=%d name='%.*s'",
                node->call.type_args.count, ta0->kind,
                ta0->kind == AST_TYPE_REF ? (int)ta0->type_ref.name.len : (ta0->kind == AST_IDENTIFIER ? (int)ta0->ident.name.len : 0),
                ta0->kind == AST_TYPE_REF ? ta0->type_ref.name.str : (ta0->kind == AST_IDENTIFIER ? ta0->ident.name.str : ""));
        }
        for (int i = 0; i < node->call.type_args.count; i++)
            zan_ast_list_push(&res->call.type_args, clone_ast_subst(node->call.type_args.items[i], ctx), ctx->arena);
        if (res->call.type_args.count > 0) {
            zan_ast_node_t *rta0 = res->call.type_args.items[0];
            zan_compile_trace("clone_ast_subst CALL type_args after: kind=%d name='%.*s'",
                rta0->kind,
                rta0->kind == AST_TYPE_REF ? (int)rta0->type_ref.name.len : (rta0->kind == AST_IDENTIFIER ? (int)rta0->ident.name.len : 0),
                rta0->kind == AST_TYPE_REF ? rta0->type_ref.name.str : (rta0->kind == AST_IDENTIFIER ? rta0->ident.name.str : ""));
        }
        return res;
    }
    case AST_BLOCK: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_BLOCK, node->loc);
        *res = *node;
        zan_ast_list_init(&res->block.stmts);
        for (int i = 0; i < node->block.stmts.count; i++)
            zan_ast_list_push(&res->block.stmts, clone_ast_subst(node->block.stmts.items[i], ctx), ctx->arena);
        return res;
    }
    case AST_RETURN_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_RETURN_STMT, node->loc);
        *res = *node;
        res->ret.value = clone_ast_subst(node->ret.value, ctx);
        return res;
    }
    case AST_EXPR_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_EXPR_STMT, node->loc);
        *res = *node;
        res->expr_stmt.expr = clone_ast_subst(node->expr_stmt.expr, ctx);
        return res;
    }
    case AST_MEMBER_ACCESS: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_MEMBER_ACCESS, node->loc);
        *res = *node;
        res->member.object = clone_ast_subst(node->member.object, ctx);
        return res;
    }
    case AST_BINARY: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_BINARY, node->loc);
        *res = *node;
        res->binary.left = clone_ast_subst(node->binary.left, ctx);
        res->binary.right = clone_ast_subst(node->binary.right, ctx);
        return res;
    }
    case AST_UNARY:
    case AST_POSTFIX_UNARY: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, node->kind, node->loc);
        *res = *node;
        res->unary.operand = clone_ast_subst(node->unary.operand, ctx);
        return res;
    }
    case AST_ASSIGNMENT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_ASSIGNMENT, node->loc);
        *res = *node;
        res->binary.left = clone_ast_subst(node->binary.left, ctx);
        res->binary.right = clone_ast_subst(node->binary.right, ctx);
        return res;
    }
    case AST_IF_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_IF_STMT, node->loc);
        *res = *node;
        res->if_stmt.cond = clone_ast_subst(node->if_stmt.cond, ctx);
        res->if_stmt.then_body = clone_ast_subst(node->if_stmt.then_body, ctx);
        res->if_stmt.else_body = clone_ast_subst(node->if_stmt.else_body, ctx);
        return res;
    }
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, node->kind, node->loc);
        *res = *node;
        res->while_stmt.cond = clone_ast_subst(node->while_stmt.cond, ctx);
        res->while_stmt.body = clone_ast_subst(node->while_stmt.body, ctx);
        return res;
    }
    case AST_FOR_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_FOR_STMT, node->loc);
        *res = *node;
        res->for_stmt.init = clone_ast_subst(node->for_stmt.init, ctx);
        res->for_stmt.cond = clone_ast_subst(node->for_stmt.cond, ctx);
        res->for_stmt.step = clone_ast_subst(node->for_stmt.step, ctx);
        res->for_stmt.body = clone_ast_subst(node->for_stmt.body, ctx);
        return res;
    }
    case AST_FOREACH_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_FOREACH_STMT, node->loc);
        *res = *node;
        res->foreach_stmt.var_type = clone_ast_subst(node->foreach_stmt.var_type, ctx);
        res->foreach_stmt.collection = clone_ast_subst(node->foreach_stmt.collection, ctx);
        res->foreach_stmt.body = clone_ast_subst(node->foreach_stmt.body, ctx);
        return res;
    }
    case AST_VAR_DECL: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_VAR_DECL, node->loc);
        *res = *node;
        res->var_decl.type = clone_ast_subst(node->var_decl.type, ctx);
        res->var_decl.initializer = clone_ast_subst(node->var_decl.initializer, ctx);
        return res;
    }
    case AST_NEW_EXPR: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_NEW_EXPR, node->loc);
        *res = *node;
        res->new_expr.type = clone_ast_subst(node->new_expr.type, ctx);
        zan_ast_list_init(&res->new_expr.args);
        for (int i = 0; i < node->new_expr.args.count; i++)
            zan_ast_list_push(&res->new_expr.args, clone_ast_subst(node->new_expr.args.items[i], ctx), ctx->arena);
        zan_ast_list_init(&res->new_expr.arg_inits);
        for (int i = 0; i < node->new_expr.arg_inits.count; i++)
            zan_ast_list_push(&res->new_expr.arg_inits, clone_ast_subst(node->new_expr.arg_inits.items[i], ctx), ctx->arena);
        return res;
    }
    case AST_CAST_EXPR:
    case AST_TYPEOF_EXPR:
    case AST_SIZEOF_EXPR: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, node->kind, node->loc);
        *res = *node;
        res->cast.type = clone_ast_subst(node->cast.type, ctx);
        res->cast.expr = clone_ast_subst(node->cast.expr, ctx);
        return res;
    }
    case AST_IS_EXPR:
    case AST_AS_EXPR: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, node->kind, node->loc);
        *res = *node;
        res->type_test.expr = clone_ast_subst(node->type_test.expr, ctx);
        res->type_test.type = clone_ast_subst(node->type_test.type, ctx);
        return res;
    }
    case AST_AWAIT_EXPR: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_AWAIT_EXPR, node->loc);
        *res = *node;
        res->await_expr.expr = clone_ast_subst(node->await_expr.expr, ctx);
        return res;
    }
    case AST_PARAM: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_PARAM, node->loc);
        *res = *node;
        res->param.type = clone_ast_subst(node->param.type, ctx);
        res->param.default_val = clone_ast_subst(node->param.default_val, ctx);
        return res;
    }
    case AST_INDEX: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_INDEX, node->loc);
        *res = *node;
        res->index.object = clone_ast_subst(node->index.object, ctx);
        res->index.index = clone_ast_subst(node->index.index, ctx);
        zan_ast_list_init(&res->index.extra);
        for (int i = 0; i < node->index.extra.count; i++)
            zan_ast_list_push(&res->index.extra, clone_ast_subst(node->index.extra.items[i], ctx), ctx->arena);
        return res;
    }
    case AST_CONDITIONAL: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_CONDITIONAL, node->loc);
        *res = *node;
        res->if_stmt.cond = clone_ast_subst(node->if_stmt.cond, ctx);
        res->if_stmt.then_body = clone_ast_subst(node->if_stmt.then_body, ctx);
        res->if_stmt.else_body = clone_ast_subst(node->if_stmt.else_body, ctx);
        return res;
    }
    case AST_THROW_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_THROW_STMT, node->loc);
        *res = *node;
        res->throw_stmt.value = clone_ast_subst(node->throw_stmt.value, ctx);
        return res;
    }
    case AST_TRY_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_TRY_STMT, node->loc);
        *res = *node;
        res->try_stmt.try_body = clone_ast_subst(node->try_stmt.try_body, ctx);
        res->try_stmt.finally_body = clone_ast_subst(node->try_stmt.finally_body, ctx);
        zan_ast_list_init(&res->try_stmt.catches);
        for (int i = 0; i < node->try_stmt.catches.count; i++)
            zan_ast_list_push(&res->try_stmt.catches, clone_ast_subst(node->try_stmt.catches.items[i], ctx), ctx->arena);
        return res;
    }
    case AST_CATCH_CLAUSE: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_CATCH_CLAUSE, node->loc);
        *res = *node;
        res->catch_clause.type = clone_ast_subst(node->catch_clause.type, ctx);
        res->catch_clause.body = clone_ast_subst(node->catch_clause.body, ctx);
        return res;
    }
    case AST_SWITCH_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_SWITCH_STMT, node->loc);
        *res = *node;
        res->switch_stmt.expr = clone_ast_subst(node->switch_stmt.expr, ctx);
        zan_ast_list_init(&res->switch_stmt.cases);
        for (int i = 0; i < node->switch_stmt.cases.count; i++)
            zan_ast_list_push(&res->switch_stmt.cases, clone_ast_subst(node->switch_stmt.cases.items[i], ctx), ctx->arena);
        return res;
    }
    case AST_SWITCH_CASE: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_SWITCH_CASE, node->loc);
        *res = *node;
        res->switch_case.pattern = clone_ast_subst(node->switch_case.pattern, ctx);
        res->switch_case.type_pattern = clone_ast_subst(node->switch_case.type_pattern, ctx);
        res->switch_case.when_cond = clone_ast_subst(node->switch_case.when_cond, ctx);
        res->switch_case.body = clone_ast_subst(node->switch_case.body, ctx);
        return res;
    }
    case AST_SWITCH_EXPR: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_SWITCH_EXPR, node->loc);
        *res = *node;
        res->switch_expr.expr = clone_ast_subst(node->switch_expr.expr, ctx);
        zan_ast_list_init(&res->switch_expr.arms);
        for (int i = 0; i < node->switch_expr.arms.count; i++)
            zan_ast_list_push(&res->switch_expr.arms, clone_ast_subst(node->switch_expr.arms.items[i], ctx), ctx->arena);
        return res;
    }
    case AST_SWITCH_ARM: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_SWITCH_ARM, node->loc);
        *res = *node;
        res->switch_arm.pattern = clone_ast_subst(node->switch_arm.pattern, ctx);
        res->switch_arm.type_pattern = clone_ast_subst(node->switch_arm.type_pattern, ctx);
        res->switch_arm.when_cond = clone_ast_subst(node->switch_arm.when_cond, ctx);
        res->switch_arm.result = clone_ast_subst(node->switch_arm.result, ctx);
        return res;
    }
    case AST_LAMBDA: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_LAMBDA, node->loc);
        *res = *node;
        zan_ast_list_init(&res->lambda.params);
        for (int i = 0; i < node->lambda.params.count; i++)
            zan_ast_list_push(&res->lambda.params, clone_ast_subst(node->lambda.params.items[i], ctx), ctx->arena);
        res->lambda.body = clone_ast_subst(node->lambda.body, ctx);
        return res;
    }
    case AST_STRING_INTERP: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_STRING_INTERP, node->loc);
        *res = *node;
        zan_ast_list_init(&res->string_interp.parts);
        for (int i = 0; i < node->string_interp.parts.count; i++)
            zan_ast_list_push(&res->string_interp.parts, clone_ast_subst(node->string_interp.parts.items[i], ctx), ctx->arena);
        zan_ast_list_init(&res->string_interp.formats);
        for (int i = 0; i < node->string_interp.formats.count; i++)
            zan_ast_list_push(&res->string_interp.formats, clone_ast_subst(node->string_interp.formats.items[i], ctx), ctx->arena);
        return res;
    }
    case AST_TUPLE_EXPR: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_TUPLE_EXPR, node->loc);
        *res = *node;
        zan_ast_list_init(&res->tuple_expr.items);
        for (int i = 0; i < node->tuple_expr.items.count; i++)
            zan_ast_list_push(&res->tuple_expr.items, clone_ast_subst(node->tuple_expr.items.items[i], ctx), ctx->arena);
        return res;
    }
    case AST_TUPLE_TYPE: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_TUPLE_TYPE, node->loc);
        *res = *node;
        zan_ast_list_init(&res->tuple_type.elems);
        for (int i = 0; i < node->tuple_type.elems.count; i++)
            zan_ast_list_push(&res->tuple_type.elems, clone_ast_subst(node->tuple_type.elems.items[i], ctx), ctx->arena);
        return res;
    }
    case AST_TUPLE_DECON: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_TUPLE_DECON, node->loc);
        *res = *node;
        zan_ast_list_init(&res->tuple_decon.names);
        for (int i = 0; i < node->tuple_decon.names.count; i++)
            zan_ast_list_push(&res->tuple_decon.names, clone_ast_subst(node->tuple_decon.names.items[i], ctx), ctx->arena);
        zan_ast_list_init(&res->tuple_decon.types);
        for (int i = 0; i < node->tuple_decon.types.count; i++)
            zan_ast_list_push(&res->tuple_decon.types, clone_ast_subst(node->tuple_decon.types.items[i], ctx), ctx->arena);
        res->tuple_decon.initializer = clone_ast_subst(node->tuple_decon.initializer, ctx);
        return res;
    }
    case AST_COLL_INIT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_COLL_INIT, node->loc);
        *res = *node;
        zan_ast_list_init(&res->coll_init.items);
        for (int i = 0; i < node->coll_init.items.count; i++)
            zan_ast_list_push(&res->coll_init.items, clone_ast_subst(node->coll_init.items.items[i], ctx), ctx->arena);
        return res;
    }
    case AST_NAMED_ARG: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_NAMED_ARG, node->loc);
        *res = *node;
        res->named_arg.expr = clone_ast_subst(node->named_arg.expr, ctx);
        return res;
    }
    case AST_REF_ARG: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_REF_ARG, node->loc);
        *res = *node;
        res->ref_arg.decl_type = clone_ast_subst(node->ref_arg.decl_type, ctx);
        res->ref_arg.expr = clone_ast_subst(node->ref_arg.expr, ctx);
        return res;
    }
    case AST_QUERY_EXPR: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_QUERY_EXPR, node->loc);
        *res = *node;
        res->query.source = clone_ast_subst(node->query.source, ctx);
        zan_ast_list_init(&res->query.clauses);
        for (int i = 0; i < node->query.clauses.count; i++)
            zan_ast_list_push(&res->query.clauses, clone_ast_subst(node->query.clauses.items[i], ctx), ctx->arena);
        res->query.group_expr = clone_ast_subst(node->query.group_expr, ctx);
        res->query.group_key = clone_ast_subst(node->query.group_key, ctx);
        res->query.select = clone_ast_subst(node->query.select, ctx);
        return res;
    }
    case AST_QUERY_WHERE:
    case AST_QUERY_LET:
    case AST_QUERY_ORDERBY:
    case AST_QUERY_JOIN: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, node->kind, node->loc);
        *res = *node;
        res->query_clause.expr = clone_ast_subst(node->query_clause.expr, ctx);
        res->query_clause.source = clone_ast_subst(node->query_clause.source, ctx);
        res->query_clause.left_key = clone_ast_subst(node->query_clause.left_key, ctx);
        res->query_clause.right_key = clone_ast_subst(node->query_clause.right_key, ctx);
        return res;
    }
    case AST_WITH_EXPR: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_WITH_EXPR, node->loc);
        *res = *node;
        res->with_expr.expr = clone_ast_subst(node->with_expr.expr, ctx);
        zan_ast_list_init(&res->with_expr.assigns);
        for (int i = 0; i < node->with_expr.assigns.count; i++)
            zan_ast_list_push(&res->with_expr.assigns, clone_ast_subst(node->with_expr.assigns.items[i], ctx), ctx->arena);
        return res;
    }
    case AST_YIELD_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_YIELD_STMT, node->loc);
        *res = *node;
        res->yield_stmt.value = clone_ast_subst(node->yield_stmt.value, ctx);
        return res;
    }
    case AST_LOCK_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_LOCK_STMT, node->loc);
        *res = *node;
        res->lock_stmt.expr = clone_ast_subst(node->lock_stmt.expr, ctx);
        res->lock_stmt.body = clone_ast_subst(node->lock_stmt.body, ctx);
        return res;
    }
    case AST_CHECKED_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_CHECKED_STMT, node->loc);
        *res = *node;
        res->checked_stmt.body = clone_ast_subst(node->checked_stmt.body, ctx);
        return res;
    }
    case AST_QUALIFIED_NAME: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_QUALIFIED_NAME, node->loc);
        *res = *node;
        zan_ast_list_init(&res->qualified_name.parts);
        for (int i = 0; i < node->qualified_name.parts.count; i++)
            zan_ast_list_push(&res->qualified_name.parts, clone_ast_subst(node->qualified_name.parts.items[i], ctx), ctx->arena);
        return res;
    }
    default: {
        /* B-ID99：default 只允许纯标量叶子浅拷——带子节点/列表的复合 kind
         * 必须走上面显式 case。新增 AST kind 落到这里即 E0061 拒绝：
         * 浅拷会共享 zan_ast_list_t.items，而 push 未扩容时原地写
         * items[count++]，两个特化克隆会互写穿对方的列表。 */
        switch (node->kind) {
        case AST_INT_LITERAL:
        case AST_FLOAT_LITERAL:
        case AST_DECIMAL_LITERAL:
        case AST_STRING_LITERAL:
        case AST_CHAR_LITERAL:
        case AST_BOOL_LITERAL:
        case AST_NULL_LITERAL:
        case AST_THIS_EXPR:
        case AST_BASE_EXPR:
        case AST_BREAK_STMT:
        case AST_CONTINUE_STMT:
        case AST_GOTO_STMT:
        case AST_LABEL_STMT:
        case AST_ERROR_EXPR:
            break;
        default:
            if (ctx->diag)
                zan_diag_emit_code(ctx->diag, DIAG_ERROR, node->loc, 61,
                                   "internal: generic base specialization hit unhandled AST kind %d",
                                   (int)node->kind);
            return node;
        }
        zan_ast_node_t *res = zan_ast_new(ctx->arena, node->kind, node->loc);
        *res = *node;
        return res;
    }
    }
}

static zan_ast_node_t *find_type_decl_by_name(zan_ast_node_t *unit, zan_istr_t name) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return NULL;
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL) continue;
        if (d->type_decl.name.len == name.len &&
            memcmp(d->type_decl.name.str, name.str, (size_t)name.len) == 0)
            return d;
        zan_istr_t orig = zan_ast_orig_name(d);
        if (orig.len == name.len &&
            memcmp(orig.str, name.str, (size_t)name.len) == 0)
            return d;
        if (d->type_decl.name.len > name.len + 1 &&
            d->type_decl.name.str[d->type_decl.name.len - name.len - 1] == '_' &&
            memcmp(d->type_decl.name.str + (d->type_decl.name.len - name.len),
                   name.str, (size_t)name.len) == 0)
            return d;
    }
    return NULL;
}

static bool class_has_method_name_and_param_count(zan_ast_node_t *cls, zan_istr_t name, int argc) {
    for (int i = 0; i < cls->type_decl.members.count; i++) {
        zan_ast_node_t *m = cls->type_decl.members.items[i];
        if (m->kind == AST_METHOD_DECL &&
            m->method_decl.name.len == name.len &&
            memcmp(m->method_decl.name.str, name.str, (size_t)name.len) == 0 &&
            m->method_decl.params.count == argc)
            return true;
    }
    return false;
}

static void specialize_type_from_base(zan_ast_node_t *derived, zan_ast_node_t *base_ref,
                                      zan_ast_node_t *unit, zan_arena_t *arena, int depth,
                                      zan_diag_t *diag) {
    if (!derived || !base_ref || !unit || depth > 8) return;
    if (base_ref->kind != AST_TYPE_REF || base_ref->type_ref.type_args.count == 0) return;

    zan_compile_trace("specialize_type_from_base: derived='%.*s' base='%.*s' args=%d arg0='%.*s'",
                      (int)derived->type_decl.name.len, derived->type_decl.name.str,
                      (int)base_ref->type_ref.name.len, base_ref->type_ref.name.str,
                      base_ref->type_ref.type_args.count,
                      base_ref->type_ref.type_args.count > 0 ? (int)base_ref->type_ref.type_args.items[0]->type_ref.name.len : 0,
                      base_ref->type_ref.type_args.count > 0 ? base_ref->type_ref.type_args.items[0]->type_ref.name.str : "");

    zan_ast_node_t *base_decl = find_type_decl_by_name(unit, base_ref->type_ref.name);
    if (!base_decl || base_decl == derived || (base_decl->kind != AST_CLASS_DECL && base_decl->kind != AST_STRUCT_DECL))
        return;
    if (base_decl->type_decl.type_params.count != base_ref->type_ref.type_args.count)
        return;

    type_subst_ctx_t ctx;
    ctx.tparams = &base_decl->type_decl.type_params;
    ctx.targs = &base_ref->type_ref.type_args;
    ctx.arena = arena;
    ctx.depth = 0;
    ctx.diag = diag;

    for (int i = 0; i < base_decl->type_decl.members.count; i++) {
        zan_ast_node_t *m = base_decl->type_decl.members.items[i];
        if (m->kind != AST_METHOD_DECL || !m->method_decl.body) continue;
        if (m->method_decl.modifiers & MOD_STATIC) continue;
        if (m->method_decl.name.len == base_decl->type_decl.name.len &&
            memcmp(m->method_decl.name.str, base_decl->type_decl.name.str, (size_t)m->method_decl.name.len) == 0)
            continue;
        if (class_has_method_name_and_param_count(derived, m->method_decl.name, m->method_decl.params.count))
            continue;

        zan_ast_node_t *spec = zan_ast_new(arena, AST_METHOD_DECL, m->loc);
        *spec = *m;
        spec->method_decl.return_type = clone_ast_subst(m->method_decl.return_type, &ctx);
        zan_ast_list_init(&spec->method_decl.params);
        for (int p = 0; p < m->method_decl.params.count; p++) {
            zan_ast_list_push(&spec->method_decl.params,
                              clone_ast_subst(m->method_decl.params.items[p], &ctx),
                              arena);
        }
        spec->method_decl.body = clone_ast_subst(m->method_decl.body, &ctx);
        zan_ast_list_push(&derived->type_decl.members, spec, arena);
    }

    for (int b = 0; b < base_decl->type_decl.bases.count; b++) {
        specialize_type_from_base(derived, base_decl->type_decl.bases.items[b], unit, arena, depth + 1, diag);
    }
}

void zan_parser_specialize_generic_bases(zan_ast_node_t *unit, zan_arena_t *arena,
                                         zan_diag_t *diag) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    int decl_count = unit->comp_unit.decls.count;
    for (int i = 0; i < decl_count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL) continue;
        for (int b = 0; b < d->type_decl.bases.count; b++) {
            specialize_type_from_base(d, d->type_decl.bases.items[b], unit, arena, 0, diag);
        }
    }
}
