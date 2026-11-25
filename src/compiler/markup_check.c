/* markup_check.c: S20 窗体标记的编译期检查（见 markup_check.h）。 */
#include "markup_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "suggest.h"

typedef struct {
    zan_diag_t *diag;
    zan_binder_t *b;
    int explicit_count;
    /* 当前窗体 */
    zan_ast_node_t *decl;
    zan_symbol_t *cls;
    const char *cls_name;
    bool check_binds;
    /* 当前标记 */
    char *path;
    char *src;
    size_t len;
    const char *shown;  /* 消息里的文件名（basename） */
    int file_id;        /* 首次报错时才登记进诊断文件表，-1 未登记 */
    int errors;
} mk_t;

static int mk_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static bool mk_ieq(const char *a, size_t al, const char *b) {
    size_t bl = strlen(b);
    if (al != bl) return false;
    for (size_t i = 0; i < al; i++)
        if (mk_lower((unsigned char)a[i]) != mk_lower((unsigned char)b[i])) return false;
    return true;
}

static bool mk_ident_char(int c, bool first) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           (!first && c >= '0' && c <= '9');
}

static const char *mk_basename(const char *p) {
    const char *s = p;
    for (const char *q = p; *q; q++)
        if (*q == '/' || *q == '\\') s = q + 1;
    return s;
}

static char *mk_read(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > (64L << 20)) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (buf && fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); buf = NULL; }
    fclose(f);
    if (!buf) return NULL;
    buf[sz] = 0;
    *len = (size_t)sz;
    return buf;
}

/* ---------- 符号 ---------- */

static zan_symbol_t *mk_find_sym_in(zan_symbol_t **syms, int n, zan_ast_node_t *decl, int depth) {
    if (depth > 16) return NULL;
    for (int i = 0; i < n; i++) {
        zan_symbol_t *s = syms[i];
        if (!s) continue;
        if (s->decl == decl && (s->kind == SYM_CLASS || s->kind == SYM_STRUCT)) return s;
        if (s->kind == SYM_NAMESPACE || s->kind == SYM_CLASS) {
            zan_symbol_t *r = mk_find_sym_in(s->members, s->member_count, decl, depth + 1);
            if (r) return r;
        }
    }
    return NULL;
}

static zan_symbol_t *mk_class_sym(zan_binder_t *b, zan_ast_node_t *decl) {
    zan_symbol_t *s = zan_binder_lookup(b, decl->type_decl.name);
    if (s && s->decl == decl) return s;
    zan_scope_t *root = b->current_scope;
    while (root && root->parent) root = root->parent;
    return root ? mk_find_sym_in(root->symbols, root->sym_count, decl, 0) : NULL;
}

static zan_symbol_t *mk_base(zan_symbol_t *s) {
    return (s && s->type && s->type->base_type) ? s->type->base_type->sym : NULL;
}

static bool mk_name_is(zan_istr_t n, const char *want) {
    size_t wl = strlen(want);
    if (n.len < wl || memcmp(n.str + n.len - wl, want, wl) != 0) return false;
    return n.len == wl || n.str[n.len - wl - 1] == '.';
}

/* 派生自 Gui.Form / Gui.ChildWindow（不含自身） */
static bool mk_is_window(zan_symbol_t *cls) {
    int guard = 0;
    for (zan_symbol_t *s = mk_base(cls); s && guard < 64; s = mk_base(s), guard++)
        if (mk_name_is(s->name, "Form") || mk_name_is(s->name, "ChildWindow")) return true;
    return false;
}

static bool mk_kind_ok(zan_symbol_t *m, bool method) {
    return method ? m->kind == SYM_METHOD : (m->kind == SYM_FIELD || m->kind == SYM_PROPERTY);
}

static zan_symbol_t *mk_member(zan_symbol_t *cls, const char *n, size_t len, bool method) {
    int guard = 0;
    for (zan_symbol_t *s = cls; s && guard < 64; s = mk_base(s), guard++)
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            if (m && mk_kind_ok(m, method) && m->name.len == len &&
                memcmp(m->name.str, n, len) == 0)
                return m;
        }
    return NULL;
}

static void mk_offer_all(zan_suggest_t *sg, zan_symbol_t *cls, bool method) {
    int guard = 0;
    for (zan_symbol_t *s = cls; s && guard < 64; s = mk_base(s), guard++)
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            if (m && mk_kind_ok(m, method)) zan_suggest_offer(sg, m->name.str, m->name.len);
        }
}

/* 用户在类自己的源码里以字符串字面量 "name" 登记过（binder.Handle("name", ...)）——
 * 运行期 AutoWire 先认已登记的处理器，这类名字不算缺失。 */
static bool mk_named_in_source(mk_t *M, const char *n, size_t len) {
    zan_diag_t *d = M->diag;
    for (int f = 0; f < M->explicit_count && f < d->file_count; f++) {
        bool mine = M->decl->loc.file_id == (uint32_t)f;
        for (int j = 0; !mine && j < M->decl->type_decl.members.count; j++) {
            zan_ast_node_t *m = M->decl->type_decl.members.items[j];
            if (m && m->loc.file_id == (uint32_t)f) mine = true;
        }
        const char *s = mine && d->file_sources ? d->file_sources[f] : NULL;
        if (!s) continue;
        for (const char *p = strchr(s, '"'); p; p = strchr(p + 1, '"'))
            if (strncmp(p + 1, n, len) == 0 && p[1 + len] == '"') return true;
    }
    return false;
}

/* ---------- 诊断 ---------- */

static zan_loc_t mk_loc(mk_t *M, size_t off) {
    if (M->file_id < 0) {
        zan_diag_add_file(M->diag, M->path, M->src);
        M->file_id = M->diag->file_count - 1;
        M->path = NULL;  /* 归诊断文件表持有（进程期） */
        M->src = NULL;
    }
    const char *src = M->diag->file_sources[M->file_id];
    zan_loc_t loc = {(uint32_t)M->file_id, 1, 1, (uint32_t)off};
    for (size_t i = 0; i < off; i++) {
        if (src[i] == '\n') { loc.line++; loc.col = 1; }
        else loc.col++;
    }
    return loc;
}

/* 名字 [n, n+len) 落在标记里 off 处：报 code，带唯一最近候选的补丁 */
static void mk_report(mk_t *M, int code, size_t off, const char *n, size_t len,
                      zan_symbol_t *scope, bool method, const char *fmt_head) {
    zan_suggest_t sg;
    zan_suggest_init(&sg, n, len);
    mk_offer_all(&sg, scope, method);
    const char *best;
    size_t best_len;
    char repl[128], hint[160] = "";
    zan_diag_fixit_t fix = {0}, *fixp = NULL;
    zan_loc_t at = mk_loc(M, off);
    if (zan_suggest_result(&sg, &best, &best_len) && best_len < sizeof(repl)) {
        memcpy(repl, best, best_len);
        repl[best_len] = 0;
        snprintf(hint, sizeof(hint), " -- did you mean '%s'?", repl);
        fix.loc = at;
        fix.len = (uint32_t)len;
        fix.text = repl;
        fixp = &fix;
    }
    const char *owner = scope && scope->name.str ? scope->name.str : M->cls_name;
    int owner_len = scope ? (int)scope->name.len : (int)strlen(M->cls_name);
    zan_diag_emit_fix(M->diag, DIAG_ERROR, at, code, fixp, "%s '%.*s' in %s is not %s of '%.*s'%s",
                      fmt_head, (int)len, n, M->shown,
                      method ? "a method" : "a field or property", owner_len, owner, hint);
    M->errors++;
}

/* ---------- 属性检查 ---------- */

static bool mk_is_event_attr(const char *s, size_t n) {
    if (n > 1 && s[0] == '@') return true;
    if (n > 5 && (memcmp(s, "v-on:", 5) == 0 || memcmp(s, "z-on:", 5) == 0)) return true;
    if (n > 8 && memcmp(s, "data-on-", 8) == 0) return true;
    if (n > 2 && s[0] == 'o' && s[1] == 'n') {
        for (size_t i = 2; i < n; i++)
            if (s[i] < 'a' || s[i] > 'z') return false;
        return true;
    }
    return false;
}

static void mk_handler(mk_t *M, const char *v, size_t vl, size_t voff) {
    size_t i = 0;
    while (i < vl && (v[i] == ' ' || v[i] == '\t')) i++;
    size_t s = i;
    while (i < vl && mk_ident_char((unsigned char)v[i], i == s)) i++;
    if (i == s) return;
    size_t e = i;
    while (i < vl && (v[i] == ' ' || v[i] == '\t')) i++;
    if (i < vl && v[i] != '(') return;  /* 不是处理器名（路由动作、表达式） */
    if (mk_member(M->cls, v + s, e - s, true)) return;
    if (mk_named_in_source(M, v + s, e - s)) return;
    mk_report(M, 2112, voff + s, v + s, e - s, M->cls, true, "handler");
}

/* 纯模型路径 ident(.ident)* 逐段对照声明类型；下钻只进用户代码里声明的类
 * （集合、字典、JsonValue、包里的类型按运行期协议取值，编译期不猜）。 */
static void mk_path(mk_t *M, const char *v, size_t vl, size_t voff, int code, const char *head) {
    size_t a = 0, b = vl;
    while (a < b && (v[a] == ' ' || v[a] == '\t')) a++;
    while (b > a && (v[b - 1] == ' ' || v[b - 1] == '\t')) b--;
    if (a == b) return;
    for (size_t i = a; i < b; i++) {
        bool seg_start = i == a || v[i - 1] == '.';
        if (v[i] == '.') {
            if (seg_start || i + 1 == b) return;
            continue;
        }
        if (!mk_ident_char((unsigned char)v[i], seg_start)) return;
    }
    size_t first = a;
    while (first < b && v[first] != '.') first++;
    if ((first - a == 4 && (memcmp(v + a, "true", 4) == 0 || memcmp(v + a, "null", 4) == 0)) ||
        (first - a == 5 && memcmp(v + a, "false", 5) == 0))
        return;
    zan_symbol_t *cur = M->cls;
    size_t i = a;
    while (cur && i < b) {
        size_t e = i;
        while (e < b && v[e] != '.') e++;
        zan_symbol_t *m = mk_member(cur, v + i, e - i, false);
        if (!m) {
            mk_report(M, code, voff + i, v + i, e - i, cur, false, head);
            return;
        }
        zan_type_t *t = m->type;
        zan_symbol_t *next = (t && t->sym && t->type_arg_count == 0) ? t->sym : NULL;
        if (next && (!next->decl || next->decl->kind != AST_CLASS_DECL ||
                     (int)next->decl->loc.file_id >= M->explicit_count))
            next = NULL;
        cur = next;
        i = e + 1;
    }
}

static void mk_attr(mk_t *M, const char *k, size_t kl, const char *v, size_t vl,
                    size_t voff, bool in_template) {
    if (mk_is_event_attr(k, kl)) { mk_handler(M, v, vl, voff); return; }
    if (in_template) return;  /* 模板行内绑定以行项为作用域 */
    bool bind = mk_ieq(k, kl, "bind") || (kl > 5 && mk_ieq(k, 5, "bind:")) ||
                (kl > 1 && k[0] == ':');
    if (bind && M->check_binds) { mk_path(M, v, vl, voff, 2113, "bind path segment"); return; }
    if (mk_ieq(k, kl, "z-mount") || mk_ieq(k, kl, "data-mount")) {
        size_t a = 0, b = vl;
        while (a < b && v[a] == ' ') a++;
        while (b > a && v[b - 1] == ' ') b--;
        for (size_t i = a; i < b; i++)
            if (!mk_ident_char((unsigned char)v[i], i == a)) return;
        if (a < b && !mk_member(M->cls, v + a, b - a, false))
            mk_report(M, 2114, voff + a, v + a, b - a, M->cls, false, "z-mount field");
    }
}

/* ---------- 标记扫描 ---------- */

static size_t mk_skip_past(const char *s, size_t len, size_t i, const char *end_ci) {
    size_t el = strlen(end_ci);
    for (; i + el <= len; i++)
        if (mk_ieq(s + i, el, end_ci)) return i + el;
    return len;
}

static void mk_scan(mk_t *M) {
    const char *s = M->src;
    size_t len = M->len, i = 0;
    int tdepth = 0;
    while (i < len) {
        if (s[i] != '<') { i++; continue; }
        if (i + 4 <= len && memcmp(s + i, "<!--", 4) == 0) { i = mk_skip_past(s, len, i + 4, "-->"); continue; }
        if (i + 1 < len && (s[i + 1] == '!' || s[i + 1] == '?')) { i = mk_skip_past(s, len, i, ">"); continue; }
        bool closing = i + 1 < len && s[i + 1] == '/';
        size_t t = i + (closing ? 2 : 1), te = t;
        while (te < len && (mk_ident_char((unsigned char)s[te], false) || s[te] == '-' || s[te] == ':')) te++;
        if (te == t) { i++; continue; }
        bool is_template = mk_ieq(s + t, te - t, "template");
        if (closing) {
            if (is_template && tdepth > 0) tdepth--;
            i = mk_skip_past(s, len, te, ">");
            continue;
        }
        size_t p = te;
        bool self_close = false;
        while (p < len && s[p] != '>') {
            if (s[p] == '/' && p + 1 < len && s[p + 1] == '>') { self_close = true; p++; break; }
            if (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n') { p++; continue; }
            size_t ks = p;
            while (p < len && s[p] != '=' && s[p] != '>' && s[p] != ' ' && s[p] != '\t' &&
                   s[p] != '\r' && s[p] != '\n' && !(s[p] == '/' && p + 1 < len && s[p + 1] == '>'))
                p++;
            size_t ke = p;
            if (ke == ks) { p++; continue; }
            size_t q = p;
            while (q < len && (s[q] == ' ' || s[q] == '\t' || s[q] == '\r' || s[q] == '\n')) q++;
            if (q >= len || s[q] != '=') continue;  /* 无值属性 */
            q++;
            while (q < len && (s[q] == ' ' || s[q] == '\t' || s[q] == '\r' || s[q] == '\n')) q++;
            size_t vs, ve;
            if (q < len && (s[q] == '"' || s[q] == '\'')) {
                char quote = s[q];
                vs = q + 1;
                ve = vs;
                while (ve < len && s[ve] != quote) ve++;
                p = ve < len ? ve + 1 : len;
            } else {
                vs = q;
                ve = vs;
                while (ve < len && s[ve] != ' ' && s[ve] != '\t' && s[ve] != '\r' &&
                       s[ve] != '\n' && s[ve] != '>')
                    ve++;
                p = ve;
            }
            mk_attr(M, s + ks, ke - ks, s + vs, ve - vs, vs, tdepth > 0 || is_template);
        }
        i = p < len ? p + 1 : len;
        if (mk_ieq(s + t, te - t, "script")) { i = mk_skip_past(s, len, i, "</script"); continue; }
        if (mk_ieq(s + t, te - t, "style")) { i = mk_skip_past(s, len, i, "</style"); continue; }
        if (is_template && !self_close) tdepth++;
    }
}

/* ---------- 窗体与标记配对 ---------- */

static bool mk_has_html_tail(const char *p, size_t n) {
    return (n > 5 && mk_ieq(p + n - 5, 5, ".html")) || (n > 4 && mk_ieq(p + n - 4, 4, ".htm"));
}

/* 同目录 <类名>.html（再试全小写）；或 --embed 的同名 .html 文件 */
static char *mk_find_markup(mk_t *M, const char *const *specs, int nspecs) {
    zan_ast_node_t *d = M->decl;
    zan_istr_t nm = d->type_decl.name;
    for (int j = -1; j < d->type_decl.members.count; j++) {
        zan_ast_node_t *at = j < 0 ? d : d->type_decl.members.items[j];
        if (!at || (int)at->loc.file_id >= M->explicit_count ||
            (int)at->loc.file_id >= M->diag->file_count)
            continue;
        const char *fn = M->diag->file_names[at->loc.file_id];
        if (!fn || fn[0] == '<') continue;
        size_t dir = (size_t)(mk_basename(fn) - fn);
        for (int lower = 0; lower < 2; lower++) {
            size_t cap = dir + nm.len + 8;
            char *p = (char *)malloc(cap);
            if (!p) return NULL;
            memcpy(p, fn, dir);
            for (uint32_t c = 0; c < nm.len; c++)
                p[dir + c] = lower ? (char)mk_lower((unsigned char)nm.str[c]) : nm.str[c];
            memcpy(p + dir + nm.len, ".html", 6);
            FILE *f = fopen(p, "rb");
            if (f) { fclose(f); return p; }
            free(p);
        }
    }
    for (int k = 0; k < nspecs; k++) {
        const char *sp = specs[k];
        if (!sp) continue;
        const char *eq = strrchr(sp, '=');
        size_t n = eq ? (size_t)(eq - sp) : strlen(sp);
        if (!mk_has_html_tail(sp, n)) continue;
        const char *bn = sp;
        for (size_t c = 0; c < n; c++)
            if (sp[c] == '/' || sp[c] == '\\') bn = sp + c + 1;
        size_t stem = (size_t)(sp + n - bn) - (mk_ieq(sp + n - 4, 4, ".htm") ? 4 : 5);
        if (stem == nm.len && stem > 0) {
            bool same = true;
            for (size_t c = 0; c < stem; c++)
                if (mk_lower((unsigned char)bn[c]) != mk_lower((unsigned char)nm.str[c])) same = false;
            if (!same) continue;
            char *p = (char *)malloc(n + 1);
            if (!p) return NULL;
            memcpy(p, sp, n);
            p[n] = 0;
            return p;
        }
    }
    return NULL;
}

/* 用户代码里（类自己或用户基类）声明了 ViewModel()：bind 的模型不是窗体本身 */
static bool mk_has_custom_model(mk_t *M) {
    int guard = 0;
    for (zan_symbol_t *s = M->cls; s && guard < 64; s = mk_base(s), guard++) {
        if (!s->decl || (int)s->decl->loc.file_id >= M->explicit_count) break;
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            if (m && m->kind == SYM_METHOD && m->name.len == 9 &&
                memcmp(m->name.str, "ViewModel", 9) == 0)
                return true;
        }
    }
    return false;
}

static void mk_check_class(mk_t *M, zan_ast_node_t *d, const char *const *specs, int nspecs) {
    if ((int)d->loc.file_id >= M->explicit_count) return;
    zan_symbol_t *cls = mk_class_sym(M->b, d);
    if (!cls || !mk_is_window(cls)) return;
    M->decl = d;
    M->cls = cls;
    char name[256];
    snprintf(name, sizeof(name), "%.*s", (int)d->type_decl.name.len, d->type_decl.name.str);
    M->cls_name = name;
    char *path = mk_find_markup(M, specs, nspecs);
    if (!path) return;
    size_t len = 0;
    char *src = mk_read(path, &len);
    if (!src) { free(path); return; }
    M->path = path;
    M->src = src;
    M->len = len;
    M->shown = mk_basename(path);
    M->file_id = -1;
    M->check_binds = !mk_has_custom_model(M);
    mk_scan(M);
    free(M->path);  /* 已登记进诊断文件表的置 NULL，这里不释放 */
    free(M->src);
    M->path = M->src = NULL;
}

static void mk_walk(mk_t *M, zan_ast_list_t *decls, const char *const *specs, int nspecs) {
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *d = decls->items[i];
        if (!d) continue;
        if (d->kind == AST_NAMESPACE_DECL) { mk_walk(M, &d->namespace_decl.members, specs, nspecs); continue; }
        if (d->kind == AST_CLASS_DECL) mk_check_class(M, d, specs, nspecs);
    }
}

int zan_markup_check(zan_ast_node_t *unit, zan_binder_t *binder, zan_diag_t *diag,
                     int explicit_input_count, const char *const *embed_specs,
                     int embed_spec_count) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT || explicit_input_count <= 0) return 0;
    mk_t M;
    memset(&M, 0, sizeof(M));
    M.diag = diag;
    M.b = binder;
    M.explicit_count = explicit_input_count;
    if (unit->comp_unit.ns) mk_walk(&M, &unit->comp_unit.ns->namespace_decl.members, embed_specs, embed_spec_count);
    mk_walk(&M, &unit->comp_unit.decls, embed_specs, embed_spec_count);
    return M.errors;
}
