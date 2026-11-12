static void refl_rg_params_add_paged(refl_param_list_t *ps, zan_ast_node_t *call) {
    static const struct { const char *name, *type, *desc; } listed[] = {
        { "page",  "int",    "页码，从 1 开始" },
        { "limit", "int",    "每页条数" },
        { "order", "string", "排序字段" },
        { "dir",   "string", "排序方向：desc 或 asc" },
        { "kw",    "string", "搜索关键词" },
    };
    char def_order[64] = "", def_limit[64] = "";
    if (call->call.args.count >= 1)
        refl_rg_literal(call->call.args.items[0], def_order, sizeof(def_order));
    if (call->call.args.count >= 2)
        refl_rg_literal(call->call.args.items[1], def_limit, sizeof(def_limit));

    for (int i = 0; i < (int)(sizeof(listed) / sizeof(listed[0])); i++) {
        if (ps->count >= (int)(sizeof(ps->items) / sizeof(ps->items[0]))) return;
        bool seen = false;
        for (int j = 0; j < ps->count; j++) {
            if (strcmp(ps->items[j].name, listed[i].name) == 0) {
                seen = true;
                break;
            }
        }
        if (seen) continue;
        refl_param_entry_t *p = &ps->items[ps->count++];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", listed[i].name);
        snprintf(p->type, sizeof(p->type), "%s", listed[i].type);
        p->required = false;
        snprintf(p->where, sizeof(p->where), "query");
        snprintf(p->desc, sizeof(p->desc), "%s", listed[i].desc);
        if (strcmp(listed[i].name, "page") == 0)
            snprintf(p->def, sizeof(p->def), "1");
        else if (strcmp(listed[i].name, "limit") == 0)
            snprintf(p->def, sizeof(p->def), "%s", def_limit);
        else if (strcmp(listed[i].name, "order") == 0)
            snprintf(p->def, sizeof(p->def), "%s", def_order);
        else if (strcmp(listed[i].name, "dir") == 0)
            snprintf(p->def, sizeof(p->def), "desc");
    }
}

static void refl_rg_scan(zan_ast_node_t *n, refl_param_list_t *ps);

static void refl_rg_scan_list(zan_ast_list_t *l, refl_param_list_t *ps) {
    if (!l) return;
    for (int i = 0; i < l->count; i++) refl_rg_scan(l->items[i], ps);
}

static void refl_rg_scan(zan_ast_node_t *n, refl_param_list_t *ps) {
    if (!n) return;
    switch (n->kind) {
    case AST_CALL: {
        zan_ast_node_t *callee = n->call.callee;
        if (callee && callee->kind == AST_MEMBER_ACCESS &&
            refl_is_input_receiver(callee->member.object)) {
            for (int i = 0; s_refl_readers[i].fn; i++) {
                if (refl_istr_is(callee->member.name, s_refl_readers[i].fn)) {
                    refl_rg_params_add(ps, &s_refl_readers[i], n);
                    break;
                }
            }
            if (refl_istr_is(callee->member.name, "Paged")) {
                refl_rg_params_add_paged(ps, n);
            }
        }
        refl_rg_scan(callee, ps);
        refl_rg_scan_list(&n->call.args, ps);
        break;
    }
    case AST_BLOCK: refl_rg_scan_list(&n->block.stmts, ps); break;
    case AST_VAR_DECL: refl_rg_scan(n->var_decl.initializer, ps); break;
    case AST_EXPR_STMT: refl_rg_scan(n->expr_stmt.expr, ps); break;
    case AST_RETURN_STMT: refl_rg_scan(n->ret.value, ps); break;
    case AST_IF_STMT:
        refl_rg_scan(n->if_stmt.cond, ps);
        refl_rg_scan(n->if_stmt.then_body, ps);
        refl_rg_scan(n->if_stmt.else_body, ps);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        refl_rg_scan(n->while_stmt.cond, ps);
        refl_rg_scan(n->while_stmt.body, ps);
        break;
    case AST_FOR_STMT:
        refl_rg_scan(n->for_stmt.init, ps);
        refl_rg_scan(n->for_stmt.cond, ps);
        refl_rg_scan(n->for_stmt.step, ps);
        refl_rg_scan(n->for_stmt.body, ps);
        break;
    case AST_FOREACH_STMT:
        refl_rg_scan(n->foreach_stmt.collection, ps);
        refl_rg_scan(n->foreach_stmt.body, ps);
        break;
    case AST_THROW_STMT: refl_rg_scan(n->throw_stmt.value, ps); break;
    case AST_TRY_STMT:
        refl_rg_scan(n->try_stmt.try_body, ps);
        for (int i = 0; i < n->try_stmt.catches.count; i++)
            refl_rg_scan(n->try_stmt.catches.items[i]->catch_clause.body, ps);
        refl_rg_scan(n->try_stmt.finally_body, ps);
        break;
    case AST_SWITCH_STMT:
        refl_rg_scan(n->switch_stmt.expr, ps);
        for (int i = 0; i < n->switch_stmt.cases.count; i++)
            refl_rg_scan(n->switch_stmt.cases.items[i]->switch_case.body, ps);
        break;
    case AST_LOCK_STMT:
        refl_rg_scan(n->lock_stmt.expr, ps);
        refl_rg_scan(n->lock_stmt.body, ps);
        break;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        refl_rg_scan(n->binary.left, ps);
        refl_rg_scan(n->binary.right, ps);
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY: refl_rg_scan(n->unary.operand, ps); break;
    case AST_MEMBER_ACCESS: refl_rg_scan(n->member.object, ps); break;
    case AST_INDEX:
        refl_rg_scan(n->index.object, ps);
        refl_rg_scan(n->index.index, ps);
        break;
    case AST_NEW_EXPR: refl_rg_scan_list(&n->new_expr.args, ps); break;
    case AST_CAST_EXPR: refl_rg_scan(n->cast.expr, ps); break;
    case AST_CONDITIONAL:
        refl_rg_scan(n->conditional.cond, ps);
        refl_rg_scan(n->conditional.then_expr, ps);
        refl_rg_scan(n->conditional.else_expr, ps);
        break;
    case AST_LAMBDA: refl_rg_scan(n->lambda.body, ps); break;
    case AST_AWAIT_EXPR: refl_rg_scan(n->await_expr.expr, ps); break;
    case AST_STRING_INTERP: refl_rg_scan_list(&n->string_interp.parts, ps); break;
    case AST_REF_ARG: refl_rg_scan(n->ref_arg.expr, ps); break;
    default: break;
    }
}

static void refl_append_method_params(zan_irgen_t *g, zan_ast_node_t *decl, char *buf, size_t cap) {
    if (!decl || decl->kind != AST_METHOD_DECL || cap <= 1) return;
    refl_param_list_t ps;
    memset(&ps, 0, sizeof(ps));

    /* 0. 扫描 [Opt("k=v", ...)] 特性 */
    char opt_names[32][64];
    char opt_defs[32][64];
    int opt_count = 0;
    zan_ast_list_t *attrs = zan_ast_attributes(decl);
    if (attrs) {
        for (int i = 0; i < attrs->count; i++) {
            zan_ast_node_t *a = attrs->items[i];
            if (!a || a->kind != AST_ATTRIBUTE || !a->attribute.name) continue;
            if (a->attribute.name->kind != AST_IDENTIFIER) continue;
            zan_istr_t an = a->attribute.name->ident.name;
            if (!an.str || an.len == 0) continue;
            if ((an.len == 3 && memcmp(an.str, "Opt", 3) == 0) ||
                (an.len == 12 && memcmp(an.str, "OptAttribute", 12) == 0)) {
                for (int k = 0; k < a->attribute.args.count && opt_count < 32; k++) {
                    zan_ast_node_t *arg = a->attribute.args.items[k];
                    if (!arg || arg->kind != AST_STRING_LITERAL || !arg->str_val.str) continue;
                    char v[192];
                    snprintf(v, sizeof(v), "%.*s", (int)arg->str_val.len, arg->str_val.str);
                    char *eq = strchr(v, '=');
                    if (eq) {
                        *eq = 0;
                        snprintf(opt_defs[opt_count], 64, "%s", eq + 1);
                    } else {
                        opt_defs[opt_count][0] = 0;
                    }
                    snprintf(opt_names[opt_count], 64, "%s", v);
                    opt_count++;
                }
            }
        }
    }

    /* 1. 签名形参直录 */
    for (int i = 0; i < decl->method_decl.params.count; i++) {
        if (ps.count >= (int)(sizeof(ps.items) / sizeof(ps.items[0]))) break;
        zan_ast_node_t *param = decl->method_decl.params.items[i];
        if (!param || param->kind != AST_PARAM) continue;
        char pname[64]; refl_istr_to(pname, sizeof(pname), param->param.name);
        char ptype[64] = "string";
        if (param->param.type && param->param.type->kind == AST_TYPE_REF) {
            refl_istr_to(ptype, sizeof(ptype), param->param.type->type_ref.name);
        }
        if (strcmp(ptype, "HttpContext") == 0 || strcmp(ptype, "System.Web.HttpContext") == 0) {
            continue;
        }

        /* 表单模型类展开：如果参数类型是类且有实例字段，展开为 form 来源字段 */
        zan_type_t *pt = NULL;
        if (g && g->binder && param->param.type) {
            pt = zan_binder_resolve_type(g->binder, param->param.type);
        }
        if (pt && pt->kind == TYPE_CLASS && pt->sym && pt->sym->decl) {
            zan_ast_node_t *fcls = pt->sym->decl;
            bool is_form = false;
            if (fcls->kind == AST_CLASS_DECL && fcls->type_decl.members.count > 0) {
                for (int fi = 0; fi < fcls->type_decl.members.count; fi++) {
                    zan_ast_node_t *mem = fcls->type_decl.members.items[fi];
                    if (!mem || mem->kind != AST_FIELD_DECL) continue;
                    if (mem->field_decl.modifiers & MOD_STATIC) continue;
                    if (ps.count >= (int)(sizeof(ps.items) / sizeof(ps.items[0]))) break;
                    char fname[64]; refl_istr_to(fname, sizeof(fname), mem->field_decl.name);
                    char ftype[64] = "string";
                    if (mem->field_decl.type && mem->field_decl.type->kind == AST_TYPE_REF) {
                        refl_istr_to(ftype, sizeof(ftype), mem->field_decl.type->type_ref.name);
                    }
                    refl_param_entry_t *p = &ps.items[ps.count++];
                    memset(p, 0, sizeof(*p));
                    snprintf(p->name, sizeof(p->name), "%s", fname);
                    snprintf(p->type, sizeof(p->type), "%s", ftype[0] ? ftype : "string");
                    p->required = false;
                    snprintf(p->where, sizeof(p->where), "form");
                    is_form = true;
                }
            }
            if (is_form) continue;
        }

        refl_param_entry_t *p = &ps.items[ps.count++];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%s", pname);
        snprintf(p->type, sizeof(p->type), "%s", ptype[0] ? ptype : "string");
        p->required = true;
        snprintf(p->where, sizeof(p->where), "query");
        for (int k = 0; k < opt_count; k++) {
            if (strcmp(opt_names[k], pname) == 0) {
                p->required = false;
                snprintf(p->def, sizeof(p->def), "%s", opt_defs[k]);
                break;
            }
        }
    }

    /* 2. 体内 In/Need/Paged 扫描 */
    if (decl->method_decl.body) {
        refl_rg_scan(decl->method_decl.body, &ps);
    }

    /* 3. 序列化为 Param=<name>\t<type>\t<where>\t<required>\t<def>\t<desc>; 追加到 buf */
    size_t cur = strlen(buf);
    for (int i = 0; i < ps.count; i++) {
        refl_param_entry_t *p = &ps.items[i];
        int w = snprintf(buf + cur, cap - cur,
                         "Param=%s\t%s\t%s\t%s\t%s\t%s;",
                         p->name, p->type[0] ? p->type : "string",
                         p->where[0] ? p->where : "query",
                         p->required ? "true" : "false",
                         p->def, p->desc);
        if (w > 0 && cur + (size_t)w < cap) {
            cur += (size_t)w;
        }
    }
}

static void refl_pack_attrs(zan_ast_list_t *attrs, char *buf, size_t buf_size) {
    if (!attrs || attrs->count == 0 || buf_size <= 1) {
        if (buf_size > 0) buf[0] = 0;
        return;
    }
    size_t pos = 0;
    buf[0] = 0;
    for (int i = 0; i < attrs->count; i++) {
        zan_ast_node_t *a = attrs->items[i];
        if (!a || a->kind != AST_ATTRIBUTE || !a->attribute.name) continue;
        if (a->attribute.name->kind != AST_IDENTIFIER) continue;
        zan_istr_t n = a->attribute.name->ident.name;
        if (!n.str || n.len == 0) continue;

        char attr_name[64];
        int nlen = (int)n.len;
        if (nlen > 60) nlen = 60;
        memcpy(attr_name, n.str, (size_t)nlen);
        attr_name[nlen] = 0;
        if (nlen > 9 && memcmp(attr_name + nlen - 9, "Attribute", 9) == 0) {
            attr_name[nlen - 9] = 0;
        }

        if (a->attribute.args.count == 0) {
            int written = snprintf(buf + pos, buf_size - pos, "%s=true;", attr_name);
            if (written > 0 && pos + (size_t)written < buf_size) pos += (size_t)written;
            continue;
        }

        for (int j = 0; j < a->attribute.args.count; j++) {
            zan_ast_node_t *arg = a->attribute.args.items[j];
            if (!arg) continue;
            if (arg->kind == AST_ASSIGNMENT && arg->binary.left &&
                arg->binary.left->kind == AST_IDENTIFIER && arg->binary.right) {
                zan_istr_t kname = arg->binary.left->ident.name;
                char kstr[64];
                int klen = (int)kname.len;
                if (klen > 60) klen = 60;
                memcpy(kstr, kname.str, (size_t)klen);
                kstr[klen] = 0;

                char val_str[256];
                refl_pack_val_str(arg->binary.right, val_str, sizeof(val_str));
                int written = snprintf(buf + pos, buf_size - pos, "%s.%s=%s;", attr_name, kstr, val_str);
                if (written > 0 && pos + (size_t)written < buf_size) pos += (size_t)written;
            } else {
                char val_str[256];
                refl_pack_val_str(arg, val_str, sizeof(val_str));
                int written = snprintf(buf + pos, buf_size - pos, "%s=%s;", attr_name, val_str);
                if (written > 0 && pos + (size_t)written < buf_size) pos += (size_t)written;
            }
        }
    }
}

static void refl_append_inherited_attrs(char *dest, size_t dest_cap, const char *src) {
    if (!src || !*src || dest_cap <= 1) return;
    const char *p = src;
    while (*p) {
        const char *semi = strchr(p, ';');
        if (!semi) semi = p + strlen(p);
        const char *eq = memchr(p, '=', (size_t)(semi - p));
        if (eq) {
            size_t klen = (size_t)(eq - p + 1);
            char key[128];
            if (klen < sizeof(key)) {
                memcpy(key, p, klen);
                key[klen] = 0;
                if (!strstr(dest, key)) {
                    size_t cur_len = strlen(dest);
                    size_t token_len = (size_t)(semi - p);
                    if (cur_len + token_len + 2 < dest_cap) {
                        memcpy(dest + cur_len, p, token_len);
                        cur_len += token_len;
                        dest[cur_len++] = ';';
                        dest[cur_len] = 0;
                    }
                }
            }
        }
        p = (*semi == ';') ? semi + 1 : semi;
    }
}

static const char *refl_find_table_attr(zan_symbol_t *sym) {
    if (!sym || !sym->decl || !sym->decl->meta) return NULL;
    zan_ast_list_t *attrs = &sym->decl->meta->attributes;
    for (int i = 0; i < attrs->count; i++) {
        zan_ast_node_t *a = attrs->items[i];
        if (!a || a->kind != AST_ATTRIBUTE || !a->attribute.name) continue;
        if (a->attribute.name->kind != AST_IDENTIFIER) continue;
        zan_istr_t n = a->attribute.name->ident.name;
        if ((n.len == 5 && memcmp(n.str, "Table", 5) == 0) ||
            (n.len == 14 && memcmp(n.str, "TableAttribute", 14) == 0)) {
            if (a->attribute.args.count > 0) {
                for (int j = 0; j < a->attribute.args.count; j++) {
                    zan_ast_node_t *arg = a->attribute.args.items[j];
                    if (!arg) continue;
                    if (arg->kind == AST_STRING_LITERAL) {
                        return arg->str_val.str;
                    } else if (arg->kind == AST_ASSIGNMENT && arg->binary.left &&
                               arg->binary.left->kind == AST_IDENTIFIER && arg->binary.right &&
                               arg->binary.right->kind == AST_STRING_LITERAL) {
                        zan_istr_t kname = arg->binary.left->ident.name;
                        if ((kname.len == 4 && memcmp(kname.str, "Name", 4) == 0) ||
                            (kname.len == 9 && memcmp(kname.str, "TableName", 9) == 0)) {
                            return arg->binary.right->str_val.str;
                        }
                    }
                }
            }
            return sym->name.str;
        }
    }
    return NULL;
}

/* B-ID107：短名渲染——与 render_type_full 同构但不注入命名空间前缀，
 * 顶层与泛型实参一律剥 ns（List<Ns.Foo> -> List<Foo>），对齐 C# Name 契约 */
static int render_type_short(zan_type_t *t, char *buf, int cap) {
    if (!t || cap <= 1) return 0;
    int n = 0;
    if (t->kind == TYPE_ARRAY && t->element_type) {
        n = render_type_short(t->element_type, buf, cap);
        if (n < cap - 1) {
            buf[n++] = '[';
            for (int r = 1; r < (t->array_rank > 1 ? t->array_rank : 1); r++)
                if (n < cap - 1) buf[n++] = ',';
            if (n < cap - 1) buf[n++] = ']';
        }
        if (n < cap) buf[n] = '\0';
        return n;
    }
    if (t->kind == TYPE_NULLABLE && t->element_type) {
        n = render_type_short(t->element_type, buf, cap);
        if (n < cap - 1) buf[n++] = '?';
        if (n < cap) buf[n] = '\0';
        return n;
    }
    if (t->name.len) {
        int w = (int)t->name.len;
        if (w > cap - n - 1) w = cap - n - 1;
        memcpy(buf + n, t->name.str, (size_t)w);
        n += w;
    } else if (n < cap - 1) {
        buf[n++] = '?';
    }
    if (t->type_arg_count > 0 && n < cap - 2) {
        buf[n++] = '<';
        for (int i = 0; i < t->type_arg_count && n < cap - 2; i++) {
            if (i) buf[n++] = ',';
            n += render_type_short(t->type_args[i], buf + n, cap - n);
        }
        if (n < cap - 1) buf[n++] = '>';
    }
    if (n < cap) buf[n] = '\0';
    return n;
}
LLVMValueRef refl_meta_for(zan_irgen_t *g, zan_type_t *t,
                                  const char *disp, int disp_len) {
    char namebuf[256];
    if (!disp || disp_len <= 0) {
        int n = t ? render_type_full(t, namebuf, sizeof(namebuf)) : 0;
        if (n <= 0) {
            namebuf[0] = '?';
            namebuf[1] = 0;
            n = 1;
        }
        disp = namebuf;
        disp_len = n;
    }
    /* B-ID107：blob 名槽存短名（Name/TableName 兜底），FullName 另存 -112 槽，
     * 去重键维持全名（metas[].name 仍存 disp） */
    char shortbuf[256];
    int sn = render_type_short(t, shortbuf, sizeof(shortbuf));
    if (sn <= 0) {
        if (disp_len > (int)sizeof(shortbuf) - 1) disp_len = (int)sizeof(shortbuf) - 1;
        memcpy(shortbuf, disp, (size_t)disp_len);
        shortbuf[disp_len] = 0;
        sn = disp_len;
    }
    zan_symbol_t *sym = t ? t->sym : NULL;
    for (int i = 0; i < g->refl_meta_count; i++) {
        if (g->refl_metas[i].sym == sym &&
            strncmp(g->refl_metas[i].name, disp, (size_t)disp_len) == 0 &&
            g->refl_metas[i].name[disp_len] == 0)
            return g->refl_metas[i].rec;
    }

    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);

    char tattrbuf[2048];
    tattrbuf[0] = 0;
    if (sym && sym->decl) refl_pack_attrs(zan_ast_attributes(sym->decl), tattrbuf, sizeof(tattrbuf));
    for (zan_symbol_t *cur = (sym && sym->type && sym->type->base_type) ? sym->type->base_type->sym : NULL;
         cur;
         cur = (cur->type && cur->type->base_type) ? cur->type->base_type->sym : NULL) {
        if (cur->decl) {
            char base_buf[1024];
            base_buf[0] = 0;
            refl_pack_attrs(zan_ast_attributes(cur->decl), base_buf, sizeof(base_buf));
            refl_append_inherited_attrs(tattrbuf, sizeof(tattrbuf), base_buf);
        }
    }
    LLVMValueRef tattr_val = refl_const_string(g, tattrbuf, (int)strlen(tattrbuf));

    const char *tbl_name = refl_find_table_attr(sym);
    LLVMValueRef tbl_val = tbl_name
        ? refl_const_string(g, tbl_name, (int)strlen(tbl_name))
        : refl_const_string(g, shortbuf, sn);

    LLVMTypeRef rec_ty = LLVMStructTypeInContext(g->ctx, (LLVMTypeRef[]){
        i8ptr, /* full_name (-112) B-ID107 */
        i8ptr, /* type_attrs (-104) */
        i8ptr, /* table_name (-96) */
        i8ptr, i64, i8ptr, i64, i8ptr, i8ptr, i8ptr, i64, i64, i64, i64,
        LLVMArrayType(i8, (unsigned)(sn + 1)) }, 15, 0);
    char nm[128];
    snprintf(nm, sizeof(nm), "__zan.refl.type.%d", g->refl_meta_count);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, rec_ty, nm);
    LLVMSetInitializer(gv, LLVMConstNull(rec_ty));
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv, 1);
    LLVMSetAlignment(gv, 8);
    LLVMValueRef idx[3] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 14, 0),
                            LLVMConstInt(i64, 0, 0) };
    LLVMValueRef rec = LLVMConstGEP2(rec_ty, gv, idx, 3);

    if (g->refl_meta_count == g->refl_meta_cap) {
        int ncap = g->refl_meta_cap ? g->refl_meta_cap * 2 : 32;
        g->refl_metas = (typeof(g->refl_metas))realloc(g->refl_metas,
            (size_t)ncap * sizeof(*g->refl_metas));
        g->refl_meta_cap = ncap;
    }
    g->refl_metas[g->refl_meta_count].sym = sym;
    g->refl_metas[g->refl_meta_count].name =
        zan_arena_strdup(g->arena, disp, (size_t)disp_len);
    g->refl_metas[g->refl_meta_count].rec = rec;
    g->refl_meta_count++;
    g->refl_used = true;

    int field_count = 0;
    LLVMValueRef fields = NULL;
    if (t && sym) {
        if (t->kind == TYPE_ENUM)
            fields = refl_emit_fields_enum(g, sym, &field_count);
        else if (t->kind == TYPE_CLASS || t->kind == TYPE_STRUCT)
            fields = refl_emit_fields_class(g, sym, &field_count);
    }
    if (!fields && refl_is_builtin_list(t)) {
        fields = refl_emit_fields_builtin_list(g, t, &field_count);
    }
    if (!fields) fields = LLVMConstNull(i8ptr);

    int method_count = 0, ctor_count = 0;
    LLVMValueRef methods = LLVMConstNull(i8ptr), ctors = LLVMConstNull(i8ptr);
    if (t && sym && (t->kind == TYPE_CLASS || t->kind == TYPE_STRUCT ||
                     t->kind == TYPE_INTERFACE)) {
        methods = refl_shape_mtab(g, sym, false, &method_count);
        ctors = refl_shape_mtab(g, sym, true, &ctor_count);
    } else if (refl_is_builtin_list(t)) {
        methods = refl_emit_methods_builtin_list(g, t, &method_count);
        ctors = refl_emit_ctor_builtin_list(g, t);
        ctor_count = 1;
    }

    LLVMValueRef elem = LLVMConstNull(i8ptr);
    if (t && (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE) &&
        t->element_type)
        elem = refl_meta_for(g, t->element_type, NULL, 0);
    else if (refl_is_builtin_list(t) && t->type_arg_count > 0 && t->type_args)
        elem = refl_meta_for(g, t->type_args[0], NULL, 0);

    LLVMValueRef targs = LLVMConstNull(i8ptr);
    if (t && t->type_arg_count > 0 && t->type_args) {
        int n = t->type_arg_count;
        LLVMValueRef *recs = (LLVMValueRef *)calloc((size_t)n + 1,
                                                    sizeof(LLVMValueRef));
        if (recs) {
            for (int i = 0; i < n; i++)
                recs[i] = t->type_args[i]
                    ? refl_meta_for(g, t->type_args[i], NULL, 0)
                    : LLVMConstNull(i8ptr);
            recs[n] = LLVMConstNull(i8ptr);
            LLVMTypeRef arr_ty = LLVMArrayType(i8ptr, (unsigned)n + 1);
            char tn[64];
            snprintf(tn, sizeof(tn), "__zan.refl.targs.%d", g->refl_str_count++);
            LLVMValueRef tgv = LLVMAddGlobal(g->mod, arr_ty, tn);
            LLVMSetInitializer(tgv, LLVMConstArray(i8ptr, recs,
                                                   (unsigned)n + 1));
            LLVMSetLinkage(tgv, LLVMPrivateLinkage);
            LLVMSetGlobalConstant(tgv, 1);
            free(recs);
            targs = LLVMConstBitCast(tgv, i8ptr);
        }
    }

    LLVMValueRef *chars = (LLVMValueRef *)calloc((size_t)sn + 1,
                                                sizeof(LLVMValueRef));
    if (!chars) return rec;
    for (int i = 0; i < sn; i++)
        chars[i] = LLVMConstInt(i8, (unsigned char)shortbuf[i], 0);
    chars[sn] = LLVMConstInt(i8, 0, 0);
    LLVMSetInitializer(gv, LLVMConstNamedStruct(rec_ty, (LLVMValueRef[]){
        refl_const_string(g, disp, disp_len), /* FullName（B-ID107） */
        tattr_val,
        tbl_val,
        methods,
        LLVMConstInt(i64, (unsigned long long)method_count, 0),
        ctors,
        LLVMConstInt(i64, (unsigned long long)ctor_count, 0),
        elem,
        targs,
        fields,
        LLVMConstInt(i64, (unsigned long long)field_count, 0),
        LLVMConstInt(i64, (unsigned long long)refl_type_kind(t), 0),
        LLVMConstInt(i64, ZAN_STRING_SENTINEL_RC, 0),
        LLVMConstInt(i64, ZAN_STR_HDR_WORD(sn), 0),
        LLVMConstArray(i8, chars, (unsigned)(sn + 1)) }, 15));
    free(chars);
    return rec;
}

/* ctor thunk 的分配点必须在站点→类型名表冻结之前登记（irgen_emit.c
 * 的顺序：refl_reserve_thunk_sites → arc desc/tyname 表 → 
 * refl_finalize_mtabs）。登记时 sym 已知，名表把实体类名的继承链
 * 挂到该分配点上，反射 CreateInstance 造出的对象才能通过 is/下转。 */
void refl_reserve_thunk_sites(zan_irgen_t *g) {
    /* emit_site_meta_table snapshots leak_site_count before lazily asking for
     * metadata. A closed List discovered there needs its own ctor site, as do
     * nested generic arguments. Warm the entire reachable site set first.
     * Metadata records enter the cache before visiting children, so recursive
     * class graphs terminate; each synthetic List site is reserved only once. */
    if (g->refl_used) {
        int sites, metas, mtabs;
        int ctor_cursor = 0;
        do {
            sites = g->leak_site_count;
            metas = g->refl_meta_count;
            mtabs = g->refl_mtab_count;
            for (int i = 1; i < g->leak_site_count; i++) {
                if (g->site_coll && g->site_coll[i]) {
                    if (g->site_coll[i] == 1) {
                        zan_type_t *elem = g->site_coll_elem[i];
                        refl_meta_for(g, zan_binder_make_list_type(g->binder, elem), NULL, 0);
                    }
                } else {
                    zan_symbol_t *sym = g->site_syms ? g->site_syms[i] : NULL;
                    if (sym && sym->type) {
                        zan_type_t *t = g->site_inst && g->site_inst[i]
                            ? g->site_inst[i] : sym->type;
                        refl_meta_for(g, t, NULL, 0);
                    }
                }
            }
            for (int i = 0; i < g->refl_mtab_count; i++) {
                if (!g->refl_mtabs[i].ctors || g->refl_mtabs[i].site >= 0) continue;
                zan_symbol_t *sym = g->refl_mtabs[i].sym;
                if (!sym || !sym->type || sym->type->kind != TYPE_CLASS) continue;
                g->refl_mtabs[i].site = reserve_arc_site(g, sym, sym->type, 0, NULL);
            }
            /* Implicit class field initializers can allocate further classes,
             * Lists or closures. Emit ctor bodies here, while site tables can
             * still grow, rather than during the post-freeze method fixup. */
            int ctor_end = g->refl_mtab_count;
            while (ctor_cursor < ctor_end) {
                int i = ctor_cursor++;
                if (!g->refl_mtabs[i].ctors) continue;
                LLVMValueRef init = refl_mtab_init(g, g->refl_mtabs[i].sym,
                    true, g->refl_mtabs[i].n, true);
                if (init) LLVMSetInitializer(g->refl_mtabs[i].gv, init);
            }
        } while (sites != g->leak_site_count || metas != g->refl_meta_count ||
                 mtabs != g->refl_mtab_count);
    }
}
void refl_finalize_mtabs(zan_irgen_t *g) {
    for (int i = 0; i < g->refl_mtab_count; i++) {
        if (g->refl_mtabs[i].ctors) continue;
        LLVMValueRef init = refl_mtab_init(g, g->refl_mtabs[i].sym,
                                           g->refl_mtabs[i].ctors,
                                           g->refl_mtabs[i].n, true);
        if (init) LLVMSetInitializer(g->refl_mtabs[i].gv, init);
    }
}

static LLVMValueRef refl_hdr_i64(zan_irgen_t *g, LLVMValueRef ti, int off,
                                 const char *name) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef d = LLVMConstInt(i64, (unsigned long long)(long long)off, 1);
    LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, ti, &d, 1, "refl.hp");
    p = LLVMBuildBitCast(g->builder, p, LLVMPointerType(i64, 0), "refl.hpc");
    return LLVMBuildLoad2(g->builder, i64, p, name);
}

