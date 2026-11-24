/* ---------- 编译器合成辅助（控制目录与 GUI 窗口入口） ----------
 *
 * 1. __ZanControlCatalog（docs/GUI_CONTROL_CATALOG.md）：
 *    取代手写的 XxxBootstrap.Install()，收集编译单元里继承自 Gui.Control
 *    的公开可构造控件与 [ControlKind] 声明，合成 Create(kind)/Kinds()。
 *
 * 2. __ZanDocMain：
 *    zan.proj type=gui 且用户未写 Main、没有 __DesignMain 时，合成
 *    new <入口类名>().Show() 回退入口。
 *
 * 注意：MVC 约定路由动作派发器已迁至 packages/Zan.Mvc 生成器，本文件不再合成
 * __ZanActionDispatch。 */

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

static zan_istr_t zc_decl_orig_name(const zan_ast_node_t *d) {
    zan_istr_t orig = zan_ast_orig_name(d);
    return orig.len > 0 ? orig : d->type_decl.name;
}

static zan_ast_node_t *zd_find_class(zan_ast_node_t *unit, const char *name) {
    int len = (int)strlen(name);
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (!d || d->kind != AST_CLASS_DECL || !d->type_decl.name.str) continue;
        zan_istr_t nm = zc_decl_orig_name(d);
        if ((int)nm.len == len && memcmp(nm.str, name, (size_t)len) == 0)
            return d;
    }
    return NULL;
}

static zan_ast_node_t *zd_find_class_ns(zan_ast_node_t *unit, const char *ns_name,
                                        const char *name) {
    int nlen = (int)strlen(ns_name);
    int len = (int)strlen(name);
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (!d || d->kind != AST_CLASS_DECL || !d->type_decl.name.str) continue;
        zan_istr_t ns = zan_ast_ns_name(d);
        if ((int)ns.len != nlen || memcmp(ns.str, ns_name, (size_t)nlen) != 0)
            continue;
        zan_istr_t nm = zc_decl_orig_name(d);
        if ((int)nm.len == len && memcmp(nm.str, name, (size_t)len) == 0)
            return d;
    }
    return NULL;
}

/* ---------- 控件目录的编译期合成（docs/GUI_CONTROL_CATALOG.md） ---------- */

typedef struct { char name[128]; } zc_kind_t;

static bool zc_kind_seen(zc_kind_t *ks, int n, const char *name) {
    for (int i = 0; i < n; i++) if (strcmp(ks[i].name, name) == 0) return true;
    return false;
}

/* 继承链上是否有命名空间 Gui 下的 Control。 */
static bool zc_is_control(zan_ast_node_t *unit, zan_ast_node_t *decl, int depth) {
    if (!decl || depth > 32) return false;
    for (int i = 0; i < decl->type_decl.bases.count; i++) {
        zan_ast_node_t *b = decl->type_decl.bases.items[i];
        if (!b || b->kind != AST_TYPE_REF || !b->type_ref.name.str) continue;
        const char *bs = b->type_ref.name.str;
        int len = (int)b->type_ref.name.len;
        for (int k = len - 1; k >= 0; k--) {
            if (bs[k] == '.') { bs += k + 1; len -= k + 1; break; }
        }
        char base[128];
        if (len <= 0 || len >= (int)sizeof(base)) continue;
        memcpy(base, bs, (size_t)len);
        base[len] = 0;
        zan_ast_node_t *bd = strcmp(base, "Control") == 0
            ? zd_find_class_ns(unit, "Gui", "Control")
            : zd_find_class(unit, base);
        if (!bd) continue;
        if (strcmp(base, "Control") == 0) return true;
        if (zc_is_control(unit, bd, depth + 1)) return true;
    }
    return false;
}

/* 有可访问的无参构造器（或未声明任何构造器）。 */
static bool zc_default_constructible(zan_ast_node_t *decl) {
    bool any = false;
    for (int j = 0; j < decl->type_decl.members.count; j++) {
        zan_ast_node_t *m = decl->type_decl.members.items[j];
        if (!m || m->kind != AST_CONSTRUCTOR_DECL) continue;
        if (m->method_decl.modifiers & MOD_STATIC) continue;
        any = true;
        if (m->method_decl.modifiers & (MOD_PRIVATE | MOD_PROTECTED | MOD_INTERNAL)) continue;
        if (m->method_decl.params.count == 0) return true;
    }
    return !any;
}

/* [ControlKind] 的一个具名实参（Name/Factory/Of）；首个位置实参等同 Name。 */
static bool zc_attr_arg(zan_ast_node_t *a, const char *key, char *out, size_t cap) {
    out[0] = 0;
    for (int j = 0; j < a->attribute.args.count; j++) {
        zan_ast_node_t *arg = a->attribute.args.items[j];
        if (!arg) continue;
        zan_ast_node_t *val = NULL;
        if (arg->kind == AST_ASSIGNMENT && arg->binary.left &&
            arg->binary.left->kind == AST_IDENTIFIER) {
            zan_istr_t k = arg->binary.left->ident.name;
            if ((int)k.len == (int)strlen(key) && memcmp(k.str, key, k.len) == 0)
                val = arg->binary.right;
        } else if (j == 0 && strcmp(key, "Name") == 0) {
            val = arg;
        }
        if (val && val->kind == AST_STRING_LITERAL && val->str_val.str) {
            snprintf(out, cap, "%s", val->str_val.str);
            return true;
        }
    }
    return false;
}

static void zc_full_name(zan_ast_node_t *decl, char *out, size_t cap) {
    zan_istr_t ns = zan_ast_ns_name(decl);
    zan_istr_t nm = zc_decl_orig_name(decl);
    if (ns.len > 0 && ns.str)
        snprintf(out, cap, "%.*s.%.*s", (int)ns.len, ns.str,
                 (int)nm.len, nm.str);
    else
        snprintf(out, cap, "%.*s", (int)nm.len, nm.str);
}

static void zan_synth_control_catalog(zan_ast_node_t *unit, zan_arena_t *arena,
                                      zan_diag_t *diag) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    zan_ast_node_t *factory = zd_find_class_ns(unit, "Gui", "ControlFactory");
    if (!factory) return;
    int cap = 1024, count = 0;
    zc_kind_t *kinds = (zc_kind_t *)calloc((size_t)cap, sizeof(zc_kind_t));
    if (!kinds) return;
    zd_buf_t create, list;
    memset(&create, 0, sizeof(create));
    memset(&list, 0, sizeof(list));
    zd_putf(&create, "/* 由编译器合成的控件目录（勿手改），见 docs/GUI_CONTROL_CATALOG.md。 */\n");
    zd_putf(&create, "class __ZanControlCatalog {\n");
    zd_putf(&create, "    public static Gui.Control Create(string kind) {\n");
    zd_putf(&list, "    public static List<string> Kinds() {\n");
    zd_putf(&list, "        List<string> k = new List<string>();\n");
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (!decl || decl->kind != AST_CLASS_DECL || !decl->type_decl.name.str) continue;
        if (decl->type_decl.modifiers & (MOD_ABSTRACT | MOD_STATIC | MOD_INTERNAL | MOD_PRIVATE))
            continue;
        if (!zc_is_control(unit, decl, 0)) continue;
        char full[512];
        zc_full_name(decl, full, sizeof(full));
        bool generic = decl->type_decl.type_params.count > 0;
        bool constructible = zc_default_constructible(decl);
        zan_istr_t orig = zc_decl_orig_name(decl);
        char simple[128];
        snprintf(simple, sizeof(simple), "%.*s", (int)orig.len, orig.str);
        /* [ControlKind] 声明：别名 / 工厂 / 泛型闭合。 */
        bool declared_default = false;
        const zan_ast_list_t *attrs = zan_ast_attributes_const(decl);
        for (int a = 0; a < attrs->count; a++) {
            int alen = 0;
            const char *an = zd_attr_name(attrs->items[a], &alen);
            if (!an || !zd_name_eq(an, alen, "ControlKind")) continue;
            char name[128], fac[128], of[128];
            if (!zc_attr_arg(attrs->items[a], "Name", name, sizeof(name))) snprintf(name, sizeof(name), "%s", simple);
            zc_attr_arg(attrs->items[a], "Factory", fac, sizeof(fac));
            zc_attr_arg(attrs->items[a], "Of", of, sizeof(of));
            if (zc_kind_seen(kinds, count, name) || count >= cap) continue;
            if (fac[0]) {
                zd_putf(&create, "        if (kind == \"%s\") { return %s.%s(); }\n", name, full, fac);
            } else if (generic && of[0]) {
                zd_putf(&create, "        if (kind == \"%s\") { return new %s<%s>(); }\n", name, full, of);
            } else if (!generic && constructible) {
                zd_putf(&create, "        if (kind == \"%s\") { return new %s(); }\n", name, full);
            } else {
                continue;
            }
            snprintf(kinds[count++].name, sizeof(kinds[0].name), "%s", name);
            zd_putf(&list, "        k.Add(\"%s\");\n", name);
            if (strcmp(name, simple) == 0) declared_default = true;
        }
        if (generic || !constructible || declared_default) continue;
        if (zc_kind_seen(kinds, count, simple) || count >= cap) continue;
        snprintf(kinds[count++].name, sizeof(kinds[0].name), "%s", simple);
        zd_putf(&create, "        if (kind == \"%s\") { return new %s(); }\n", simple, full);
        zd_putf(&list, "        k.Add(\"%s\");\n", simple);
    }
    free(kinds);
    zd_putf(&create, "        return null;\n    }\n");
    zd_putf(&list, "        return k;\n    }\n}\n");
    zd_putf(&create, "%s", list.buf ? list.buf : "");
    free(list.buf);
    if (!create.buf) return;
    size_t slen = create.len;
    char *src = zan_arena_strdup(arena, create.buf, slen);
    free(create.buf);
    if (!src) return;
    int file_id = diag->file_count;
    zan_diag_add_file(diag, "<control-catalog>", src);
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

/* ---------- gui 工程的入口 shim 合成 ---------- */

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
