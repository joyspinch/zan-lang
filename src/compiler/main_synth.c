/* ---------- __AttrRoutes 动作派发的编译期合成 ----------
 *
 * 约定路由（packages/Zan.Mvc/.../AttrRoutes.zan）把每条路由的 handler 统一
 * 挂到 __AttrRoutes.Dispatch，由它按 ctx.routeAction 派发到真正的控制器
 * 方法。派发器无法用反射实现：反射 thunk 只回传方法返回值，异步方法的
 * 协程句柄被丢弃，`await` 不到（而控制器动作绝大多数 `async`，且异常要
 * 沿 await 链回到 WebApp 的错误处理）。
 *
 * 所以派发在编译期展开成强类型调用：本函数扫描编译单元里的用户控制器
 * （跳过 stdlib / packages 来源，口径同 checker_collect_controllers），
 * 合成一个 __ZanActionDispatch 类——每个动作一条 `if (actionKey == "...")`
 * 分支，分支内 new 控制器 → Bind(ctx) → 参数绑定 → 前置钩子 → await 动作
 * → 后置钩子。checker 在 __AttrRoutes.Register 调用点把它作为第三实参传给
 * 框架（Register(app, controllers, dispatcher)）。
 *
 * 覆盖口径：实例/静态动作、同步/异步动作、int/long/double/bool/string
 * 参数（必填走 Need*，[Opt("name=default")] 可选走 In*），[Tx]/[ReadOnly]。
 * 表单模型参数与泛型动作暂不合成（对应路由仍注册，派发回落到 404）。
 */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} zd_buf_t;

static void zd_putf(zd_buf_t *b, const char *fmt, ...) {
    if (!b || !fmt) return;
    if (!b->buf) {
        b->cap = 16384;
        b->buf = (char *)malloc(b->cap);
        if (!b->buf) { b->cap = 0; return; }
        b->buf[0] = 0;
    }
    for (;;) {
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap);
        va_end(ap);
        if (n < 0) return;
        if ((size_t)n < b->cap - b->len) { b->len += (size_t)n; return; }
        size_t ncap = b->cap * 2;
        while (ncap < b->len + (size_t)n + 1) ncap *= 2;
        char *grown = (char *)realloc(b->buf, ncap);
        if (!grown) return;
        b->buf = grown;
        b->cap = ncap;
    }
}

/* 属性名归一：剥命名空间前缀与尾部 "Attribute"（与反射侧打包口径一致）。 */
static const char *zd_attr_name(zan_ast_node_t *a, int *out_len) {
    if (!a || a->kind != AST_ATTRIBUTE || !a->attribute.name) return NULL;
    if (a->attribute.name->kind != AST_IDENTIFIER) return NULL;
    zan_istr_t n = a->attribute.name->ident.name;
    if (!n.str || n.len == 0) return NULL;
    const char *s = n.str;
    int len = (int)n.len;
    for (int k = len - 1; k >= 0; k--) {
        if (s[k] == '.') { s += k + 1; len -= k + 1; break; }
    }
    if (len > 9 && memcmp(s + len - 9, "Attribute", 9) == 0) len -= 9;
    *out_len = len;
    return s;
}

static bool zd_name_eq(const char *s, int len, const char *lit) {
    size_t n = strlen(lit);
    return (size_t)len == n && memcmp(s, lit, n) == 0;
}

static bool zd_name_has(const char *s, int len, const char *lit) {
    size_t n = strlen(lit);
    if (n == 0 || (size_t)len < n) return false;
    for (int i = 0; i + (int)n <= len; i++)
        if (memcmp(s + i, lit, n) == 0) return true;
    return false;
}

static bool zd_has_attr(zan_ast_node_t *n, const char *name) {
    const zan_ast_list_t *attrs = zan_ast_attributes_const(n);
    for (int i = 0; i < attrs->count; i++) {
        int len = 0;
        const char *s = zd_attr_name(attrs->items[i], &len);
        if (s && zd_name_eq(s, len, name)) return true;
    }
    return false;
}

/* 方法是否会成为路由动作（口径照抄 __AttrRoutes.RegisterController：
   Route= / Http* / Description= / Custom. / Action，或约定 Index）。 */
static bool zd_is_action_method(zan_ast_node_t *m, bool *is_index) {
    zan_istr_t nm = m->method_decl.name;
    bool idx = nm.len == 5 && nm.str && memcmp(nm.str, "Index", 5) == 0;
    if (is_index) *is_index = idx;
    const zan_ast_list_t *attrs = zan_ast_attributes_const(m);
    for (int i = 0; i < attrs->count; i++) {
        int len = 0;
        const char *s = zd_attr_name(attrs->items[i], &len);
        if (!s) continue;
        if (zd_name_eq(s, len, "Route") ||
            (len >= 4 && memcmp(s, "Http", 4) == 0) ||
            zd_name_eq(s, len, "Description") ||
            zd_name_eq(s, len, "Custom") ||
            zd_name_has(s, len, "Action"))
            return true;
    }
    return idx;
}

static void zd_esc(char *out, size_t cap, const char *s) {
    size_t n = 0;
    if (!cap) return;
    for (; s && *s && n + 3 < cap; s++) {
        if (*s == '"' || *s == '\\') out[n++] = '\\';
        out[n++] = *s;
    }
    out[n] = 0;
}

/* [Opt("page=1","kw=")] → 可选参数名/默认值表。 */
static int zd_opt_args(zan_ast_node_t *m, char (*names)[64],
                       char (*defs)[64], int cap) {
    const zan_ast_list_t *attrs = zan_ast_attributes_const(m);
    int n = 0;
    for (int i = 0; i < attrs->count; i++) {
        zan_ast_node_t *a = attrs->items[i];
        int len = 0;
        const char *s = zd_attr_name(a, &len);
        if (!s || !zd_name_eq(s, len, "Opt")) continue;
        for (int k = 0; k < a->attribute.args.count && n < cap; k++) {
            zan_ast_node_t *arg = a->attribute.args.items[k];
            char v[192];
            if (!arg || arg->kind != AST_STRING_LITERAL || !arg->str_val.str)
                continue;
            snprintf(v, sizeof(v), "%.*s", (int)arg->str_val.len,
                     arg->str_val.str);
            char *eq = strchr(v, '=');
            if (eq) { *eq = 0; snprintf(defs[n], 64, "%s", eq + 1); }
            else defs[n][0] = 0;
            snprintf(names[n], 64, "%s", v);
            n++;
        }
    }
    return n;
}

/* 参数或字段类型是否可直接从请求绑定；返回规范类型名或 NULL。 */
static const char *zd_param_type(zan_ast_node_t *p) {
    if (!p) return NULL;
    zan_ast_node_t *t = NULL;
    if (p->kind == AST_PARAM) t = p->param.type;
    else if (p->kind == AST_FIELD_DECL) t = p->field_decl.type;
    if (!t || t->kind != AST_TYPE_REF) return NULL;
    zan_istr_t n = t->type_ref.name;
    if (!n.str) return NULL;
    const char *s = n.str;
    int len = (int)n.len;
    for (int k = len - 1; k >= 0; k--) {
        if (s[k] == '.') { s += k + 1; len -= k + 1; break; }
    }
    if (zd_name_eq(s, len, "int")) return "int";
    if (zd_name_eq(s, len, "long")) return "long";
    if (zd_name_eq(s, len, "double")) return "double";
    if (zd_name_eq(s, len, "bool")) return "bool";
    if (zd_name_eq(s, len, "string")) return "string";
    return NULL;
}

/* 用户类声明按短名查找（控制器基类链回溯用）。 */
static zan_ast_node_t *zd_find_class(zan_ast_node_t *unit, const char *name) {
    int len = (int)strlen(name);
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (!d || d->kind != AST_CLASS_DECL || !d->type_decl.name.str) continue;
        if ((int)d->type_decl.name.len == len &&
            memcmp(d->type_decl.name.str, name, (size_t)len) == 0)
            return d;
    }
    return NULL;
}

/* 控制器判定：基类链上任一段以 Controller 结尾。非控制器（如 Program
   里的 Index 方法）也进路由表，但没有 Bind 等钩子，不能合成强类型调用。 */
static bool zd_class_is_controller(zan_ast_node_t *unit, zan_ast_node_t *decl,
                                   int depth) {
    if (!decl || depth > 16) return false;
    for (int i = 0; i < decl->type_decl.bases.count; i++) {
        zan_ast_node_t *b = decl->type_decl.bases.items[i];
        if (!b || b->kind != AST_TYPE_REF || !b->type_ref.name.str) continue;
        zan_istr_t n = b->type_ref.name;
        const char *s = n.str;
        int len = (int)n.len;
        for (int k = len - 1; k >= 0; k--) {
            if (s[k] == '.') { s += k + 1; len -= k + 1; break; }
        }
        if (len >= 10 && memcmp(s + len - 10, "Controller", 10) == 0) return true;
        char base[128];
        if (len <= 0 || len >= (int)sizeof(base)) continue;
        memcpy(base, s, (size_t)len);
        base[len] = 0;
        zan_ast_node_t *bd = zd_find_class(unit, base);
        if (bd && zd_class_is_controller(unit, bd, depth + 1)) return true;
    }
    return false;
}

/* 生成一条动作分支；返回 false 表示签名不可绑定（跳过该动作）。 */
static bool zd_emit_action(zd_buf_t *b, zan_ast_node_t *unit, zan_ast_node_t *m,
                           const char *full, const char *short_name,
                           bool class_tx, bool class_ro) {
    if (!b->buf || !m || !full || !short_name) return false;
    if (m->kind != AST_METHOD_DECL) return false;
    if (!m->method_decl.name.str || m->method_decl.name.len == 0) return false;
    if (m->method_decl.type_params.count > 0) return false;   /* 泛型动作 */
    if (!zd_is_action_method(m, NULL)) return false;

    int np = m->method_decl.params.count;
    char ptype[32][128];
    char pname[32][128];
    char pdef[32][64];
    char pdesc[32][64];   /* 中文名字段，直接用参数名 */
    bool pform[32];
    bool popt[32];
    memset(pdef, 0, sizeof(pdef));
    memset(pform, 0, sizeof(pform));
    memset(popt, 0, sizeof(popt));
    if (np > 32) return false;
    char optn[32][64];
    char optd[32][64];
    int nopt = zd_opt_args(m, optn, optd, 32);
    for (int i = 0; i < np; i++) {
        zan_ast_node_t *p = m->method_decl.params.items[i];
        const char *pt = zd_param_type(p);
        zan_ast_node_t *fcls = NULL;
        if (!pt && p && p->kind == AST_PARAM && p->param.type &&
            p->param.type->kind == AST_TYPE_REF && p->param.type->type_ref.name.str) {
            char tname[128];
            snprintf(tname, sizeof(tname), "%.*s", (int)p->param.type->type_ref.name.len,
                     p->param.type->type_ref.name.str);
            fcls = zd_find_class(unit, tname);
            if (fcls) {
                snprintf(ptype[i], sizeof(ptype[i]), "%s", tname);
                pt = ptype[i];
                pform[i] = true;
            }
        }
        if (!pt) return false;         /* 未知类型 / 泛型参数：暂不合成 */
        if (!pform[i]) snprintf(ptype[i], sizeof(ptype[i]), "%s", pt);
        snprintf(pname[i], sizeof(pname[i]), "%.*s", (int)p->param.name.len,
                 p->param.name.str ? p->param.name.str : "");
        pdesc[i][0] = 0;
        for (int k = 0; k < nopt; k++) {
            if (strcmp(optn[k], pname[i]) == 0) {
                popt[i] = true;
                snprintf(pdesc[i], 64, "%s", optd[k]);
                break;
            }
        }
    }

    bool is_static = (m->method_decl.modifiers & MOD_STATIC) != 0;
    bool is_async = (m->method_decl.modifiers & MOD_ASYNC) != 0;
    const char *aw = is_async ? "await " : "";
    bool tx = class_tx || zd_has_attr(m, "Tx");
    bool ro = class_ro || zd_has_attr(m, "ReadOnly");

    char mname[192];
    snprintf(mname, sizeof(mname), "%.*s", (int)m->method_decl.name.len,
             m->method_decl.name.str);
    zd_putf(b, "        if (actionKey == \"%s.%s\") {\n", short_name, mname);

    /* 参数绑定：实例动作借控制器上的 Need* / In*（带 ApiError 中止语义），
       静态动作没有实例，直接读 ctx 并在缺失时抛同样的 ApiError。
       表单模型类实例化后按字段 HasIn / In* 绑定。 */
    char binds[6144];
    binds[0] = 0;
    size_t blen = 0;
    for (int i = 0; i < np; i++) {
        const char *rt = ptype[i];
        const char *dn = pdesc[i];
        const char *recv = is_static ? "ctx" : "__c";
        if (pform[i]) {
            zan_ast_node_t *fcls = zd_find_class(unit, rt);
            char fline[2048];
            snprintf(fline, sizeof(fline), "            %s %s = new %s();\n", rt, pname[i], rt);
            for (int fi = 0; fcls && fi < fcls->type_decl.members.count; fi++) {
                zan_ast_node_t *mem = fcls->type_decl.members.items[fi];
                if (!mem || mem->kind != AST_FIELD_DECL) continue;
                if (mem->field_decl.modifiers & MOD_STATIC) continue;
                const char *ft = zd_param_type(mem);
                if (!ft) continue;
                char fn[64];
                snprintf(fn, sizeof(fn), "%.*s", (int)mem->field_decl.name.len,
                         mem->field_decl.name.str ? mem->field_decl.name.str : "");
                char fsub[512];
                if (strcmp(ft, "int") == 0)
                    snprintf(fsub, sizeof(fsub), "            if (ctx.HasIn(\"%s\")) { %s.%s = ctx.InInt(\"%s\", %s.%s); }\n", fn, pname[i], fn, fn, pname[i], fn);
                else if (strcmp(ft, "long") == 0)
                    snprintf(fsub, sizeof(fsub), "            if (ctx.HasIn(\"%s\")) { %s.%s = ctx.InLong(\"%s\", %s.%s); }\n", fn, pname[i], fn, fn, pname[i], fn);
                else if (strcmp(ft, "double") == 0)
                    snprintf(fsub, sizeof(fsub), "            if (ctx.HasIn(\"%s\")) { %s.%s = ctx.InDouble(\"%s\", %s.%s); }\n", fn, pname[i], fn, fn, pname[i], fn);
                else if (strcmp(ft, "bool") == 0)
                    snprintf(fsub, sizeof(fsub), "            if (ctx.HasIn(\"%s\")) { %s.%s = ctx.InBool(\"%s\", %s.%s); }\n", fn, pname[i], fn, fn, pname[i], fn);
                else
                    snprintf(fsub, sizeof(fsub), "            if (ctx.HasIn(\"%s\")) { %s.%s = ctx.In(\"%s\"); }\n", fn, pname[i], fn, fn);
                strncat(fline, fsub, sizeof(fline) - strlen(fline) - 1);
            }
            size_t flen = strlen(fline);
            if (blen + flen + 1 < sizeof(binds)) {
                memcpy(binds + blen, fline, flen + 1);
                blen += flen;
            }
            continue;
        }
        char line[512];
        if (popt[i]) {
            /* [Opt]：缺省读，缺失取默认值 */
            if (strcmp(rt, "int") == 0)
                snprintf(line, sizeof(line), "            int %s = %s.InInt(\"%s\", %s);\n", pname[i], recv, pname[i], dn[0] ? dn : "0");
            else if (strcmp(rt, "long") == 0)
                snprintf(line, sizeof(line), "            long %s = %s.InLong(\"%s\", %s);\n", pname[i], recv, pname[i], dn[0] ? dn : "0");
            else if (strcmp(rt, "double") == 0)
                snprintf(line, sizeof(line), "            double %s = %s.InDouble(\"%s\", %s);\n", pname[i], recv, pname[i], dn[0] ? dn : "0");
            else if (strcmp(rt, "bool") == 0)
                snprintf(line, sizeof(line), "            bool %s = %s.InBool(\"%s\", %s);\n", pname[i], recv, pname[i], strcmp(dn, "true") == 0 ? "true" : "false");
            else {
                char ed[128];
                zd_esc(ed, sizeof(ed), dn);
                snprintf(line, sizeof(line),
                         "            string %s = %s.In(\"%s\");\n            if (%s.Length == 0) { %s = \"%s\"; }\n",
                         pname[i], recv, pname[i], pname[i], pname[i], ed);
            }
        } else if (is_static) {
            if (strcmp(rt, "int") == 0)
                snprintf(line, sizeof(line), "            if (!ctx.HasIn(\"%s\")) { throw new System.Web.ApiError(400, \"0003\", \"%s不能为空\"); }\n            int %s = ctx.InInt(\"%s\", 0);\n", pname[i], pname[i], pname[i], pname[i]);
            else if (strcmp(rt, "long") == 0)
                snprintf(line, sizeof(line), "            if (!ctx.HasIn(\"%s\")) { throw new System.Web.ApiError(400, \"0003\", \"%s不能为空\"); }\n            long %s = ctx.InLong(\"%s\", 0);\n", pname[i], pname[i], pname[i], pname[i]);
            else if (strcmp(rt, "double") == 0)
                snprintf(line, sizeof(line), "            if (!ctx.HasIn(\"%s\")) { throw new System.Web.ApiError(400, \"0003\", \"%s不能为空\"); }\n            double %s = ctx.InDouble(\"%s\", 0);\n", pname[i], pname[i], pname[i], pname[i]);
            else if (strcmp(rt, "bool") == 0)
                snprintf(line, sizeof(line), "            if (!ctx.HasIn(\"%s\")) { throw new System.Web.ApiError(400, \"0003\", \"%s不能为空\"); }\n            bool %s = ctx.InBool(\"%s\", false);\n", pname[i], pname[i], pname[i], pname[i]);
            else
                snprintf(line, sizeof(line), "            string %s = ctx.In(\"%s\");\n            if (%s.Length == 0) { throw new System.Web.ApiError(400, \"0003\", \"%s不能为空\"); }\n", pname[i], pname[i], pname[i], pname[i]);
        } else {
            if (strcmp(rt, "int") == 0)
                snprintf(line, sizeof(line), "            int %s = __c.NeedInt(\"%s\", \"%s\");\n", pname[i], pname[i], pname[i]);
            else if (strcmp(rt, "long") == 0)
                snprintf(line, sizeof(line), "            long %s = __c.NeedLong(\"%s\", \"%s\");\n", pname[i], pname[i], pname[i]);
            else if (strcmp(rt, "double") == 0)
                snprintf(line, sizeof(line), "            double %s = __c.NeedDouble(\"%s\", \"%s\");\n", pname[i], pname[i], pname[i]);
            else if (strcmp(rt, "bool") == 0)
                snprintf(line, sizeof(line), "            bool %s = __c.NeedBool(\"%s\", \"%s\");\n", pname[i], pname[i], pname[i]);
            else
                snprintf(line, sizeof(line), "            string %s = __c.Need(\"%s\", \"%s\");\n", pname[i], pname[i], pname[i]);
        }
        size_t ll = strlen(line);
        if (blen + ll + 1 < sizeof(binds)) {
            memcpy(binds + blen, line, ll + 1);
            blen += ll;
        }
    }

    char args[1024];
    args[0] = 0;
    for (int i = 0; i < np; i++) {
        if (i) strncat(args, ", ", sizeof(args) - strlen(args) - 1);
        strncat(args, pname[i], sizeof(args) - strlen(args) - 1);
    }

    if (is_static) {
        zd_putf(b, "%s", binds);
        zd_putf(b, "            %s%s.%s(%s);\n", aw, full, mname, args);
    } else {
        zd_putf(b, "            %s __c = new %s();\n", full, full);
        zd_putf(b, "            __c.Bind(ctx);\n");
        if (ro) zd_putf(b, "            __c.UseReadOnly();\n");
        if (tx) zd_putf(b, "            bool __tx = false;\n");
        zd_putf(b, "            try {\n");
        zd_putf(b, "                if (await __c.BeforeAsync()) {\n");
        if (tx) {
            zd_putf(b, "                    bool __begun = await __c.TxBegin();\n");
            zd_putf(b, "                    if (!__begun) { throw new System.Web.ApiError(503, \"0503\", \"服务不可用\"); }\n");
            zd_putf(b, "                    __tx = true;\n");
        }
        zd_putf(b, "%s", binds);
        zd_putf(b, "                    %s__c.%s(%s);\n", aw, mname, args);
        if (tx) {
            zd_putf(b, "                    await __c.TxCommit();\n");
            zd_putf(b, "                    __tx = false;\n");
        }
        zd_putf(b, "                }\n");
        if (tx) {
            zd_putf(b, "            } catch (System.Exception __x) {\n");
            zd_putf(b, "                if (__tx) { try { await __c.TxRollback(); } catch (System.Exception __r) { } }\n");
            zd_putf(b, "                throw;\n");
        }
        zd_putf(b, "            } finally {\n");
        zd_putf(b, "                await __c.AfterAsync();\n");
        zd_putf(b, "            }\n");
    }
    zd_putf(b, "            return true;\n        }\n");
    return true;
}

typedef struct {
    int found;
} zd_probe_t;

static zan_walk_action_t zd_probe_register(zan_ast_node_t *n, void *ud) {
    zd_probe_t *p = (zd_probe_t *)ud;
    if (n && n->kind == AST_CALL && n->call.callee &&
        n->call.callee->kind == AST_MEMBER_ACCESS &&
        n->call.callee->member.name.len == 8 && n->call.callee->member.name.str &&
        memcmp(n->call.callee->member.name.str, "Register", 8) == 0) {
        zan_ast_node_t *obj = n->call.callee->member.object;
        if (obj && obj->kind == AST_IDENTIFIER && obj->ident.name.len == 12 &&
            obj->ident.name.str &&
            memcmp(obj->ident.name.str, "__AttrRoutes", 12) == 0) {
            p->found = 1;
            return ZAN_WALK_ABORT;
        }
    }
    return ZAN_WALK_CONTINUE;
}

static void zan_synth_action_dispatch(zan_ast_node_t *unit, zan_arena_t *arena,
                                      zan_diag_t *diag) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    zd_probe_t probe = {0};
    for (int i = 0; i < unit->comp_unit.decls.count && !probe.found; i++)
        zan_ast_walk(unit->comp_unit.decls.items[i], zd_probe_register, NULL,
                     &probe);
    if (!probe.found) return;

    zd_buf_t b;
    memset(&b, 0, sizeof(b));
    zd_putf(&b, "/* 由编译器合成的动作派发器（勿手改）：约定路由的 handler 统一是\n");
    zd_putf(&b, "   __AttrRoutes.Dispatch，实际控制器方法调用在这里按 actionKey 展开。 */\n");
    zd_putf(&b, "class __ZanActionDispatch {\n");
    zd_putf(&b, "    public static async bool Dispatch(System.Web.HttpContext ctx, string actionKey) {\n");

    int emitted = 0;
    int skipped = 0;
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (!decl || decl->kind != AST_CLASS_DECL || !decl->type_decl.name.str)
            continue;
        if (decl->from_stdlib || decl->from_package) continue;
        if (decl->type_decl.modifiers & MOD_ABSTRACT) continue;
        if (decl->type_decl.type_params.count > 0) continue;
        if (!zd_class_is_controller(unit, decl, 0)) continue;

        char full[512];
        zan_istr_t ns = zan_ast_ns_name(decl);
        if (ns.len > 0 && ns.str)
            snprintf(full, sizeof(full), "%.*s.%.*s", (int)ns.len, ns.str,
                     (int)decl->type_decl.name.len, decl->type_decl.name.str);
        else
            snprintf(full, sizeof(full), "%.*s", (int)decl->type_decl.name.len,
                     decl->type_decl.name.str);

        /* 动作键短名口径同框架：剥最后一段命名空间，'_' 视作嵌套分隔符 */
        char short_name[256];
        snprintf(short_name, sizeof(short_name), "%.*s",
                 (int)decl->type_decl.name.len, decl->type_decl.name.str);
        {
            char *u = strrchr(short_name, '_');
            if (u && u[1]) memmove(short_name, u + 1, strlen(u + 1) + 1);
        }

        bool class_tx = zd_has_attr(decl, "Tx");
        bool class_ro = zd_has_attr(decl, "ReadOnly");
        for (int j = 0; j < decl->type_decl.members.count; j++) {
            zan_ast_node_t *m = decl->type_decl.members.items[j];
            if (!m || m->kind != AST_METHOD_DECL) continue;
            if (m->method_decl.modifiers & MOD_PRIVATE) continue;
            if (m->method_decl.modifiers & MOD_ABSTRACT) continue;
            if (zd_emit_action(&b, unit, m, full, short_name, class_tx, class_ro))
                emitted++;
            else {
                bool idx = false;
                if (zd_is_action_method(m, &idx)) skipped++;
            }
        }
    }

    if (emitted == 0) zd_putf(&b, "        await Task.Delay(0);\n");
    zd_putf(&b, "        return false;\n");
    zd_putf(&b, "    }\n}\n");

    if (!b.buf) return;
    if (skipped > 0)
        zan_compile_trace("action dispatch: %d emitted, %d skipped "
                          "(unsupported signature)", emitted, skipped);

    size_t slen = b.len;
    char *src = zan_arena_strdup(arena, b.buf, slen);
    free(b.buf);
    if (!src) return;
    int file_id = diag->file_count;
    zan_diag_add_file(diag, "<action-dispatch>", src);
    zan_lexer_t lex;
    zan_lexer_init(&lex, src, slen, file_id, arena, diag);
    zan_parser_t parser;
    zan_parser_init(&parser, &lex, arena, diag);
    zan_ast_node_t *synth = zan_parser_parse(&parser);
    if (!synth || synth->kind != AST_COMPILATION_UNIT) return;
    for (int i = 0; i < synth->comp_unit.decls.count; i++)
        zan_ast_list_push(&unit->comp_unit.decls,
                          synth->comp_unit.decls.items[i], arena);
}

/* ---------- gui 工程的入口 shim 合成（B-ID147） ----------
 *
 * zan.proj type=gui 且用户没写 Main、没有 __DesignMain 时，合成一个
 * `new <入口文件基名>().show()` 调用——窗口即对象（类继承框架的
 * Window 基类，show 装载同目录同名 .html 并绑定自身）。编译器不认识
 * 任何框架类型：`show` 只是类型检查自然会过的方法调用；用户写了
 * Main 即整体优先（__DesignMain 回退语义）。 */
typedef struct {
    int main;        /* 用户声明的 static Main */
    int design_main; /* 已有 __DesignMain */
} zdm_probe_t;

static zan_walk_action_t zdm_probe_walk(zan_ast_node_t *n, void *ud) {
    zdm_probe_t *p = (zdm_probe_t *)ud;
    if (!n) return ZAN_WALK_CONTINUE;
    if (n->kind == AST_METHOD_DECL && n->method_decl.name.str) {
        zan_istr_t nm = n->method_decl.name;
        if (nm.len == 4 && memcmp(nm.str, "Main", 4) == 0 &&
            (n->method_decl.modifiers & MOD_STATIC))
            p->main = 1;
        if (nm.len == 12 && memcmp(nm.str, "__DesignMain", 12) == 0)
            p->design_main = 1;
    }
    return ZAN_WALK_CONTINUE;
}

static void zan_synth_docapp_main(zan_ast_node_t *unit, zan_arena_t *arena,
                                  zan_diag_t *diag, const char *entry_file) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    zdm_probe_t probe = {0};
    for (int i = 0; i < unit->comp_unit.decls.count; i++)
        zan_ast_walk(unit->comp_unit.decls.items[i], zdm_probe_walk, NULL,
                     &probe);
    if (probe.main || probe.design_main) return;
    if (!load_proj_android_keys_done) {
        load_proj_android_keys();
        load_proj_android_keys_done = true;
    }
    if (strcmp(proj_app_type, "gui") != 0) return;

    /* 入口类名 = 入口文件基名帕斯卡化（App.zan ↔ class App 约定）。
       zan.proj 声明了 entry 键时优先（多文件工程的入口不在排序首位，
       如 src/Models/ 辅助类排在窗口类之前）。 */
    const char *entry = proj_entry_file[0] ? proj_entry_file : entry_file;
    const char *slash = strrchr(entry, '/');
    const char *bslash = strrchr(entry, '\\');
    if (bslash > slash) slash = bslash;
    const char *base = slash ? slash + 1 : entry;
    char cls[256];
    size_t blen = strlen(base);
    if (blen >= sizeof(cls)) blen = sizeof(cls) - 1;
    memcpy(cls, base, blen);
    cls[blen] = 0;
    char *dot = strrchr(cls, '.');
    if (dot) *dot = 0;
    if (cls[0] >= 'a' && cls[0] <= 'z') cls[0] = (char)(cls[0] - 32);
    if (cls[0] == 0) return;

    zd_buf_t b;
    memset(&b, 0, sizeof(b));
    zd_putf(&b, "/* 由编译器合成的窗口入口（勿手改）：type=gui 工程无 Main\n");
    zd_putf(&b, "   时的回退入口；用户写了 Main 即整体优先，本类不参与。 */\n");
    zd_putf(&b, "static class __ZanDocMain {\n");
    zd_putf(&b, "    static void __DesignMain() {\n");
    zd_putf(&b, "        new %s().Show();\n", cls);
    zd_putf(&b, "    }\n}\n");
    if (!b.buf) return;

    size_t slen = b.len;
    char *src = zan_arena_strdup(arena, b.buf, slen);
    free(b.buf);
    if (!src) return;
    int file_id = diag->file_count;
    zan_diag_add_file(diag, "<docapp-main>", src);
    zan_lexer_t lex;
    zan_lexer_init(&lex, src, slen, file_id, arena, diag);
    zan_parser_t parser;
    zan_parser_init(&parser, &lex, arena, diag);
    zan_ast_node_t *synth = zan_parser_parse(&parser);
    if (!synth || synth->kind != AST_COMPILATION_UNIT) return;
    for (int i = 0; i < synth->comp_unit.usings.count; i++)
        zan_ast_list_push(&unit->comp_unit.usings,
                          synth->comp_unit.usings.items[i], arena);
    for (int i = 0; i < synth->comp_unit.decls.count; i++)
        zan_ast_list_push(&unit->comp_unit.decls,
                          synth->comp_unit.decls.items[i], arena);
}

