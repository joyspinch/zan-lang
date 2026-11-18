#include "mvc_compiler.h"
#include "binder.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    char name[64];
    char type[32];
    bool required;
    char def[64];
    char desc[128];
    char where[32];
} refl_param_entry_t;

typedef struct {
    refl_param_entry_t items[32];
    int count;
} refl_param_list_t;

typedef struct {
    const char *fn;
    const char *type;
    bool required;
    bool has_default;
    bool has_label;
    const char *where;
} refl_rg_reader_t;

static const refl_rg_reader_t s_refl_readers[] = {
    { "In",        "string", false, false, false, "query" },
    { "InText",    "string", false, false, false, "query" },
    { "InRaw",     "string", false, false, false, "query" },
    { "InInt",     "int",    false, true,  false, "query" },
    { "InLong",    "long",   false, true,  false, "query" },
    { "InDouble",  "number", false, true,  false, "query" },
    { "InBool",    "bool",   false, true,  false, "query" },
    { "HasIn",     "bool",   false, false, false, "query" },
    { "Need",      "string", true,  false, true,  "query" },
    { "NeedText",  "string", true,  false, true,  "query" },
    { "NeedInt",   "int",    true,  false, true,  "query" },
    { "NeedLong",  "long",   true,  false, true,  "query" },
    { "Param",     "string", true,  false, false, "path"  },
    { NULL, NULL, false, false, false, NULL },
};

static inline bool refl_istr_is(zan_istr_t s, const char *lit) {
    size_t len = strlen(lit);
    return s.len == len && memcmp(s.str, lit, len) == 0;
}

static inline void refl_istr_to(char *out, size_t cap, zan_istr_t s) {
    if (!cap) return;
    size_t n = s.len < cap - 1 ? s.len : cap - 1;
    if (s.str && n) memcpy(out, s.str, n);
    out[n] = 0;
}

static void refl_rg_literal(zan_ast_node_t *e, char *out, size_t cap) {
    out[0] = 0;
    if (!e || cap <= 1) return;
    bool neg = false;
    if (e->kind == AST_UNARY && e->unary.operand) {
        neg = true;
        e = e->unary.operand;
    }
    if (e->kind == AST_INT_LITERAL) {
        snprintf(out, cap, "%s%lld", neg ? "-" : "", (long long)e->int_val);
    } else if (e->kind == AST_FLOAT_LITERAL) {
        snprintf(out, cap, "%s%g", neg ? "-" : "", e->float_val);
    } else if (e->kind == AST_BOOL_LITERAL) {
        snprintf(out, cap, "%s", e->bool_val ? "true" : "false");
    } else if (e->kind == AST_STRING_LITERAL) {
        refl_istr_to(out, cap, e->str_val);
    }
}

static bool refl_is_input_receiver(zan_ast_node_t *obj) {
    if (!obj) return false;
    if (obj->kind == AST_THIS_EXPR) return true;
    if (obj->kind == AST_IDENTIFIER)
        return refl_istr_is(obj->ident.name, "ctx") || refl_istr_is(obj->ident.name, "c");
    if (obj->kind == AST_CALL && obj->call.callee &&
        obj->call.callee->kind == AST_MEMBER_ACCESS &&
        refl_istr_is(obj->call.callee->member.name, "Ctx"))
        return true;
    return false;
}

static void refl_rg_params_add(refl_param_list_t *ps, const refl_rg_reader_t *rd, zan_ast_node_t *call) {
    if (ps->count >= (int)(sizeof(ps->items) / sizeof(ps->items[0]))) return;
    if (call->call.args.count < 1) return;
    zan_ast_node_t *a0 = call->call.args.items[0];
    if (a0->kind != AST_STRING_LITERAL) return;

    char nm[64]; refl_istr_to(nm, sizeof(nm), a0->str_val);
    if (!nm[0]) return;

    for (int i = 0; i < ps->count; i++) {
        if (strcmp(ps->items[i].name, nm) != 0) continue;
        refl_param_entry_t *p = &ps->items[i];
        if (rd->required) p->required = true;
        if (rd->has_label && call->call.args.count >= 2 && !p->desc[0])
            refl_rg_literal(call->call.args.items[1], p->desc, sizeof(p->desc));
        if (rd->has_default && call->call.args.count >= 2 && !p->def[0])
            refl_rg_literal(call->call.args.items[1], p->def, sizeof(p->def));
        if (strcmp(p->type, "string") == 0) snprintf(p->type, sizeof(p->type), "%s", rd->type);
        return;
    }

    refl_param_entry_t *p = &ps->items[ps->count++];
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "%s", nm);
    snprintf(p->type, sizeof(p->type), "%s", rd->type);
    p->required = rd->required;
    snprintf(p->where, sizeof(p->where), "%s", rd->where);
    if (call->call.args.count >= 2) {
        if (rd->has_default) refl_rg_literal(call->call.args.items[1], p->def, sizeof(p->def));
        if (rd->has_label) refl_rg_literal(call->call.args.items[1], p->desc, sizeof(p->desc));
    }
}

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

void zan_mvc_append_method_params(zan_binder_t *binder, zan_ast_node_t *decl,
                                  char *buf, size_t cap) {
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
        if (binder && param->param.type) {
            pt = zan_binder_resolve_type(binder, param->param.type);
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
