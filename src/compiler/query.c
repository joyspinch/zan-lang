/* query.c: S2 zanc --query <符号>（docs/ZAN_STANDARD.md §1）
 *
 * 类型检查之后，对匹配的方法/构造器回答 AI 最常问、最难从源码一眼看出的问题，
 * 每个匹配一行 JSON（JSON Lines，末行 summary，与 --diag=json 同一约定）：
 *   allocates     方法体里直接的分配：new 对象/数组/集合初始化、lambda 闭包记录
 *   captures      方法体里每个 lambda 用到的 this 与外层局部量/形参
 *   throws        direct：方法体里没被本方法 catch 住的 `throw new X`；escapes：可能
 *                 逃出本方法的异常类型（经已解析调用传递，扣除沿途 try 的 catch——
 *                 catch 基类型也算、裸 catch 与 catch (Exception) 全收、catch 体里
 *                 rethrow 的不算），每种给一条真能逃出来的示例调用链
 *   capabilities  可达调用里用到的能力（§2.2 S7 口径）：Net、File、Process、
 *                 Registry、Clipboard、Native（标准库之外的 extern/DllImport），
 *                 每项给一条证据调用链
 *   calls         直接调用的方法；callers：全程序里解析到它的调用点
 * 调用图来自 checker 写在调用节点上的 call.resolved_sym；每个调用点与 throw 点记下
 * 所处的 catch 上下文（只有 try 体受保护；lambda 体不受外层 try 保护）。`new T(...)` 连到 T 的
 * 同元数构造器。虚调用按静态目标连边（不展开覆盖），委托调用不连边（目标在运行期
 * 才定），lambda 里的调用算宿主方法的。各列表最多 64 项，callers 截断时
 * callers_truncated 为 true。
 *
 * 名字：`Type.Method`、`Type.Type` / `Type.ctor`（构造器）；单名先当类型名列出它的
 * 全部方法，没有这个类型再找任意类型里的同名方法。 */

#include "query.h"
#include "symbols.h"
#include "binder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define Q_MAX_LIST 64

/* 调用点 / throw 点所处的 catch 上下文：一条 catch 子句 + 外层上下文（链表，
 * 下标进 q_prog_t.filters；-1 = 不在任何 try 体内） */
typedef struct {
    int             parent;
    zan_ast_node_t *catch_type;   /* NULL = 裸 catch */
    bool            all;          /* 裸 catch 或 catch (Exception) */
} q_filter_t;

typedef struct {
    int             callee;
    int             filt;
    bool            deferred;     /* 在 lambda 体里：委托被调用时才执行 */
} q_site_t;

typedef struct {
    zan_ast_node_t *node;         /* AST_THROW_STMT */
    int             filt;
    bool            deferred;
} q_throw_t;

typedef struct {
    zan_ast_node_t *decl;   /* AST_METHOD_DECL / AST_CONSTRUCTOR_DECL */
    zan_ast_node_t *owner;  /* 类型声明 */
    int            *callees;
    int             callee_count, callee_cap;
    q_site_t       *sites;
    int             site_count, site_cap;
    q_throw_t      *throws;
    int             throw_count, throw_cap;
} q_method_t;

typedef struct {
    zan_binder_t   *binder;
    q_method_t     *m;
    int             count, cap;
    zan_ast_node_t **keys;  /* decl -> 下标（开放寻址） */
    int            *vals;
    size_t          mask;
    FILE           *out;
    const zan_diag_t *diag;
    q_filter_t     *filters;
    int             filter_count, filter_cap;
} q_prog_t;

/* ---------- JSON ---------- */

static void q_json_str(FILE *f, const char *s, size_t n) {
    fputc('"', f);
    for (size_t i = 0; i < n && s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
        else if (c == '\n') fputs("\\n", f);
        else if (c == '\t') fputs("\\t", f);
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

static void q_json_cstr(FILE *f, const char *s) { q_json_str(f, s, strlen(s)); }

/* ---------- 程序索引 ---------- */

static size_t q_hash(const void *p) {
    uintptr_t x = (uintptr_t)p >> 3;
    x ^= x >> 17;
    x *= (uintptr_t)0x9E3779B97F4A7C15ull;
    return (size_t)x;
}

static int q_find(const q_prog_t *P, const zan_ast_node_t *decl) {
    if (!P->keys || !decl) return -1;
    for (size_t i = q_hash(decl) & P->mask; P->keys[i]; i = (i + 1) & P->mask)
        if (P->keys[i] == decl) return P->vals[i];
    return -1;
}

static void q_add_method(q_prog_t *P, zan_ast_node_t *decl, zan_ast_node_t *owner) {
    if (P->count == P->cap) {
        int ncap = P->cap ? P->cap * 2 : 1024;
        q_method_t *nm = (q_method_t *)realloc(P->m, (size_t)ncap * sizeof(*nm));
        if (!nm) return;
        P->m = nm;
        P->cap = ncap;
    }
    memset(&P->m[P->count], 0, sizeof(P->m[0]));
    P->m[P->count].decl = decl;
    P->m[P->count].owner = owner;
    P->count++;
}

static void q_collect_types(q_prog_t *P, zan_ast_list_t *decls) {
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *d = decls->items[i];
        if (!d) continue;
        if (d->kind == AST_NAMESPACE_DECL) { q_collect_types(P, &d->namespace_decl.members); continue; }
        if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL &&
            d->kind != AST_INTERFACE_DECL) continue;
        for (int j = 0; j < d->type_decl.members.count; j++) {
            zan_ast_node_t *m = d->type_decl.members.items[j];
            if (m && (m->kind == AST_METHOD_DECL || m->kind == AST_CONSTRUCTOR_DECL))
                q_add_method(P, m, d);
        }
    }
}

static bool q_build_map(q_prog_t *P) {
    size_t cap = 64;
    while (cap < (size_t)P->count * 2) cap *= 2;
    P->keys = (zan_ast_node_t **)calloc(cap, sizeof(*P->keys));
    P->vals = (int *)calloc(cap, sizeof(int));
    if (!P->keys || !P->vals) return false;
    P->mask = cap - 1;
    for (int v = 0; v < P->count; v++) {
        size_t i = q_hash(P->m[v].decl) & P->mask;
        while (P->keys[i]) i = (i + 1) & P->mask;
        P->keys[i] = P->m[v].decl;
        P->vals[i] = v;
    }
    return true;
}

static void q_add_edge(q_method_t *m, int to) {
    for (int i = 0; i < m->callee_count; i++) if (m->callees[i] == to) return;
    if (m->callee_count == m->callee_cap) {
        int ncap = m->callee_cap ? m->callee_cap * 2 : 8;
        int *nc = (int *)realloc(m->callees, (size_t)ncap * sizeof(int));
        if (!nc) return;
        m->callees = nc;
        m->callee_cap = ncap;
    }
    m->callees[m->callee_count++] = to;
}

static zan_ast_node_t *q_type_decl_of_ref(q_prog_t *P, zan_ast_node_t *tref) {
    if (!tref || tref->kind != AST_TYPE_REF) return NULL;
    zan_symbol_t *s = zan_binder_lookup(P->binder, tref->type_ref.name);
    if (!s || !s->decl) return NULL;
    zan_ast_kind_t k = s->decl->kind;
    return (k == AST_CLASS_DECL || k == AST_STRUCT_DECL) ? s->decl : NULL;
}

/* 调用点 → 被调方法下标；new T(...) → T 的同元数构造器（可能多个，回调逐个给） */
typedef void (*q_site_fn)(q_prog_t *P, int callee, zan_ast_node_t *site, void *ud);

static void q_visit_calls(q_prog_t *P, zan_ast_node_t *n, q_site_fn fn, void *ud) {
    if (!n) return;
    if (n->kind == AST_CALL && n->call.resolved_sym && n->call.resolved_sym->decl) {
        int to = q_find(P, n->call.resolved_sym->decl);
        if (to >= 0) fn(P, to, n, ud);
    } else if (n->kind == AST_NEW_EXPR && !n->new_expr.is_array) {
        zan_ast_node_t *td = q_type_decl_of_ref(P, n->new_expr.type);
        if (td) {
            for (int j = 0; j < td->type_decl.members.count; j++) {
                zan_ast_node_t *c = td->type_decl.members.items[j];
                if (c && c->kind == AST_CONSTRUCTOR_DECL &&
                    c->method_decl.params.count == n->new_expr.args.count) {
                    int to = q_find(P, c);
                    if (to >= 0) fn(P, to, n, ud);
                }
            }
        }
    }
    int cc = zan_ast_child_count(n);
    for (int i = 0; i < cc; i++) q_visit_calls(P, zan_ast_child_at(n, i), fn, ud);
}

/* ---------- catch 上下文 ---------- */

static int q_filter_push(q_prog_t *P, int parent, zan_ast_node_t *catch_type) {
    if (P->filter_count == P->filter_cap) {
        int ncap = P->filter_cap ? P->filter_cap * 2 : 256;
        q_filter_t *nf = (q_filter_t *)realloc(P->filters, (size_t)ncap * sizeof(*nf));
        if (!nf) return parent;
        P->filters = nf;
        P->filter_cap = ncap;
    }
    q_filter_t *f = &P->filters[P->filter_count];
    f->parent = parent;
    f->catch_type = catch_type;
    f->all = !catch_type ||
             (catch_type->kind == AST_TYPE_REF && catch_type->type_ref.name.len == 9 &&
              memcmp(catch_type->type_ref.name.str, "Exception", 9) == 0);
    return P->filter_count++;
}

/* catch 体里 `throw;` 或把捕获变量原样抛出：异常照样逃出，这条 catch 不算收住 */
static bool q_catch_rethrows(zan_ast_node_t *n, zan_istr_t var) {
    if (!n || n->kind == AST_LAMBDA) return false;
    if (n->kind == AST_THROW_STMT) {
        zan_ast_node_t *v = n->throw_stmt.value;
        if (!v) return true;
        if (v->kind == AST_IDENTIFIER && var.len && v->ident.name.len == var.len &&
            memcmp(v->ident.name.str, var.str, var.len) == 0)
            return true;
    }
    int cc = zan_ast_child_count(n);
    for (int i = 0; i < cc; i++)
        if (q_catch_rethrows(zan_ast_child_at(n, i), var)) return true;
    return false;
}

static void q_add_site(q_method_t *m, int callee, int filt, bool deferred) {
    if (m->site_count == m->site_cap) {
        int ncap = m->site_cap ? m->site_cap * 2 : 8;
        q_site_t *ns = (q_site_t *)realloc(m->sites, (size_t)ncap * sizeof(*ns));
        if (!ns) return;
        m->sites = ns;
        m->site_cap = ncap;
    }
    m->sites[m->site_count].callee = callee;
    m->sites[m->site_count].filt = filt;
    m->sites[m->site_count].deferred = deferred;
    m->site_count++;
    q_add_edge(m, callee);
}

static void q_add_throw(q_method_t *m, zan_ast_node_t *node, int filt, bool deferred) {
    if (m->throw_count == m->throw_cap) {
        int ncap = m->throw_cap ? m->throw_cap * 2 : 4;
        q_throw_t *nt = (q_throw_t *)realloc(m->throws, (size_t)ncap * sizeof(*nt));
        if (!nt) return;
        m->throws = nt;
        m->throw_cap = ncap;
    }
    m->throws[m->throw_count].node = node;
    m->throws[m->throw_count].filt = filt;
    m->throws[m->throw_count].deferred = deferred;
    m->throw_count++;
}

/* 方法体扫描：调用点与 throw 点连同所处的 catch 上下文记下来。只有 try 体受
 * 本 try 的 catch 保护；catch 体与 finally 在外层上下文里；lambda 体是委托被调用
 * 时才执行的代码（deferred）：不算本方法会抛的异常，但算本方法代码用到的能力。 */
static void q_scan(q_prog_t *P, int mi, zan_ast_node_t *n, int filt, bool deferred) {
    if (!n) return;
    switch (n->kind) {
    case AST_TRY_STMT: {
        int f = filt;
        for (int i = 0; i < n->try_stmt.catches.count; i++) {
            zan_ast_node_t *c = n->try_stmt.catches.items[i];
            if (!c || q_catch_rethrows(c->catch_clause.body, c->catch_clause.var_name)) continue;
            f = q_filter_push(P, f, c->catch_clause.type);
        }
        q_scan(P, mi, n->try_stmt.try_body, f, deferred);
        for (int i = 0; i < n->try_stmt.catches.count; i++) {
            zan_ast_node_t *c = n->try_stmt.catches.items[i];
            if (c) q_scan(P, mi, c->catch_clause.body, filt, deferred);
        }
        q_scan(P, mi, n->try_stmt.finally_body, filt, deferred);
        return;
    }
    case AST_LAMBDA:
        q_scan(P, mi, n->lambda.body, -1, true);
        return;
    case AST_THROW_STMT:
        q_add_throw(&P->m[mi], n, filt, deferred);
        break;
    case AST_CALL:
        if (n->call.resolved_sym && n->call.resolved_sym->decl) {
            int to = q_find(P, n->call.resolved_sym->decl);
            if (to >= 0) q_add_site(&P->m[mi], to, filt, deferred);
        }
        break;
    case AST_NEW_EXPR:
        if (!n->new_expr.is_array) {
            zan_ast_node_t *td = q_type_decl_of_ref(P, n->new_expr.type);
            for (int j = 0; td && j < td->type_decl.members.count; j++) {
                zan_ast_node_t *c = td->type_decl.members.items[j];
                if (c && c->kind == AST_CONSTRUCTOR_DECL &&
                    c->method_decl.params.count == n->new_expr.args.count) {
                    int to = q_find(P, c);
                    if (to >= 0) q_add_site(&P->m[mi], to, filt, deferred);
                }
            }
        }
        break;
    default:
        break;
    }
    int cc = zan_ast_child_count(n);
    for (int i = 0; i < cc; i++) q_scan(P, mi, zan_ast_child_at(n, i), filt, deferred);
}

/* 异常类型名 tname 是否是（或派生自）catch 类型 */
static bool q_is_subtype(const q_prog_t *P, const char *tname, zan_istr_t cname) {
    size_t tl = strlen(tname);
    if (tl == cname.len && memcmp(tname, cname.str, tl) == 0) return true;
    zan_istr_t key = { (char *)tname, (uint32_t)tl };
    zan_symbol_t *s = zan_binder_lookup(P->binder, key);
    for (int guard = 0; s && s->type && guard < 32; guard++) {
        zan_type_t *b = s->type->base_type;
        if (!b || !b->sym) break;
        s = b->sym;
        if (s->name.len == cname.len && memcmp(s->name.str, cname.str, cname.len) == 0)
            return true;
    }
    return false;
}

static bool q_caught(const q_prog_t *P, const char *tname, int filt) {
    for (int f = filt; f >= 0; f = P->filters[f].parent) {
        const q_filter_t *F = &P->filters[f];
        if (F->all) return true;
        if (F->catch_type && F->catch_type->kind == AST_TYPE_REF &&
            q_is_subtype(P, tname, F->catch_type->type_ref.name))
            return true;
    }
    return false;
}

static bool q_filter_all(const q_prog_t *P, int filt) {
    for (int f = filt; f >= 0; f = P->filters[f].parent)
        if (P->filters[f].all) return true;
    return false;
}

/* ---------- 标签与能力 ---------- */

static void q_label(const q_prog_t *P, int i, char *buf, size_t cap) {
    char sig[1024];
    zan_symbols_method_sig(P->m[i].decl, sig, (int)sizeof(sig));
    const char *name = sig;
    zan_istr_t mn = P->m[i].decl->method_decl.name;
    /* sig 形如 "ret Name(...)"：从名字处截取，前面挂类型名 */
    for (const char *p = sig; *p; p++) {
        if (mn.len && strncmp(p, mn.str, mn.len) == 0 && p[mn.len] == '(' &&
            (p == sig || p[-1] == ' ')) { name = p; break; }
    }
    snprintf(buf, cap, "%.*s.%s", (int)P->m[i].owner->type_decl.name.len,
             P->m[i].owner->type_decl.name.str, name);
}

static bool q_owner_is(const zan_ast_node_t *owner, const char *name) {
    size_t n = strlen(name);
    return owner->type_decl.name.len == n && memcmp(owner->type_decl.name.str, name, n) == 0;
}

static const char *const Q_CAPS[] = { "Net", "File", "Process", "Registry", "Clipboard", "Native" };
#define Q_CAP_COUNT 6

static int q_capability(const q_prog_t *P, int i) {
    const zan_ast_node_t *m = P->m[i].decl, *o = P->m[i].owner;
    if (m->kind == AST_METHOD_DECL && !m->method_decl.body && !o->from_stdlib &&
        ((m->method_decl.modifiers & MOD_EXTERN) ||
         zan_ast_method_extern_lib(m).str != NULL))
        return 5;
    zan_istr_t ns = zan_ast_ns_name(o);
    if (ns.len >= 10 && memcmp(ns.str, "System.Net", 10) == 0) return 0;
    if (q_owner_is(o, "File") || q_owner_is(o, "Directory") || q_owner_is(o, "FileStream") ||
        q_owner_is(o, "FileInfo") || q_owner_is(o, "DirectoryInfo") ||
        q_owner_is(o, "FileSystemWatcher"))
        return 1;
    if (q_owner_is(o, "Process")) return 2;
    if (q_owner_is(o, "Registry") || q_owner_is(o, "RegistryKey")) return 3;
    if (q_owner_is(o, "Clipboard")) return 4;
    return -1;
}

static void q_path(const q_prog_t *P, const int *parent, int node, char *buf, size_t cap) {
    int chain[32], n = 0;
    for (int v = node; v >= 0 && n < 32; v = parent[v]) chain[n++] = v;
    size_t o = 0;
    buf[0] = '\0';
    for (int k = n - 1; k >= 0 && o < cap - 8; k--) {
        char lab[512];
        q_label(P, chain[k], lab, sizeof(lab));
        o += (size_t)snprintf(buf + o, cap - o, "%s%s", k == n - 1 ? "" : " -> ", lab);
    }
}

/* ---------- 方法体内的直接事实 ---------- */

static void q_throw_name(zan_ast_node_t *t, char *buf, size_t cap) {
    zan_ast_node_t *v = t->throw_stmt.value;
    if (v && v->kind == AST_NEW_EXPR && v->new_expr.type)
        zan_symbols_type_name(v->new_expr.type, buf, (int)cap);
    else
        snprintf(buf, cap, "%s", v ? "(rethrown value)" : "(rethrow)");
}

typedef struct {
    FILE *f;
    int   n;
} q_list_t;

static void q_item_sep(q_list_t *l) { if (l->n++) fputc(',', l->f); }

static void q_emit_allocs(FILE *f, zan_ast_node_t *n, q_list_t *l) {
    if (!n || l->n >= Q_MAX_LIST) return;
    if (n->kind == AST_NEW_EXPR) {
        char t[256] = "?";
        if (n->new_expr.type) zan_symbols_type_name(n->new_expr.type, t, (int)sizeof(t));
        q_item_sep(l);
        fprintf(f, "{\"what\":");
        char what[300];
        snprintf(what, sizeof(what), "%s%s", t, n->new_expr.is_array ? "[]" : "");
        q_json_cstr(f, what);
        fprintf(f, ",\"line\":%u}", n->loc.line);
    } else if (n->kind == AST_LAMBDA) {
        q_item_sep(l);
        fprintf(f, "{\"what\":\"closure\",\"line\":%u}", n->loc.line);
    } else if (n->kind == AST_COLL_INIT) {
        q_item_sep(l);
        fprintf(f, "{\"what\":\"collection initializer\",\"line\":%u}", n->loc.line);
    }
    int cc = zan_ast_child_count(n);
    for (int i = 0; i < cc; i++) q_emit_allocs(f, zan_ast_child_at(n, i), l);
}

/* 外层名字：形参 + 方法体里声明的局部量（不分位置，近似） */
typedef struct {
    zan_istr_t names[256];
    int        n;
} q_names_t;

static void q_names_add(q_names_t *s, zan_istr_t nm) {
    if (!nm.len || s->n >= 256) return;
    for (int i = 0; i < s->n; i++)
        if (s->names[i].len == nm.len && memcmp(s->names[i].str, nm.str, nm.len) == 0) return;
    s->names[s->n++] = nm;
}

static bool q_names_has(const q_names_t *s, zan_istr_t nm) {
    for (int i = 0; i < s->n; i++)
        if (s->names[i].len == nm.len && memcmp(s->names[i].str, nm.str, nm.len) == 0) return true;
    return false;
}

static void q_collect_locals(zan_ast_node_t *n, q_names_t *s) {
    if (!n) return;
    if (n->kind == AST_VAR_DECL) q_names_add(s, n->var_decl.name);
    if (n->kind == AST_LAMBDA) return;   /* lambda 自己的局部量不算外层 */
    int cc = zan_ast_child_count(n);
    for (int i = 0; i < cc; i++) q_collect_locals(zan_ast_child_at(n, i), s);
}

static bool q_owner_has_instance_member(const zan_ast_node_t *owner, zan_istr_t nm) {
    for (int i = 0; i < owner->type_decl.members.count; i++) {
        const zan_ast_node_t *m = owner->type_decl.members.items[i];
        if (!m) continue;
        zan_istr_t mn = {0};
        uint32_t mods = 0;
        if (m->kind == AST_FIELD_DECL || m->kind == AST_PROPERTY_DECL) {
            mn = m->field_decl.name; mods = m->field_decl.modifiers;
        } else if (m->kind == AST_METHOD_DECL) {
            mn = m->method_decl.name; mods = m->method_decl.modifiers;
        } else continue;
        if (!(mods & MOD_STATIC) && mn.len == nm.len && memcmp(mn.str, nm.str, nm.len) == 0)
            return true;
    }
    return false;
}

typedef struct {
    const q_names_t *outer;
    q_names_t        params;      /* lambda 自己的形参 */
    q_names_t        used;
    bool             uses_this;
    const zan_ast_node_t *owner;
} q_cap_scan_t;

static void q_scan_lambda_body(zan_ast_node_t *n, q_cap_scan_t *cs) {
    if (!n) return;
    if (n->kind == AST_THIS_EXPR || n->kind == AST_BASE_EXPR) cs->uses_this = true;
    if (n->kind == AST_IDENTIFIER) {
        zan_istr_t nm = n->ident.name;
        if (!q_names_has(&cs->params, nm)) {
            if (q_names_has(cs->outer, nm)) q_names_add(&cs->used, nm);
            else if (cs->owner && q_owner_has_instance_member(cs->owner, nm)) cs->uses_this = true;
        }
    }
    int cc = zan_ast_child_count(n);
    for (int i = 0; i < cc; i++) q_scan_lambda_body(zan_ast_child_at(n, i), cs);
}

static void q_emit_captures(FILE *f, zan_ast_node_t *n, const q_names_t *outer,
                            const zan_ast_node_t *owner, q_list_t *l) {
    if (!n || l->n >= Q_MAX_LIST) return;
    if (n->kind == AST_LAMBDA) {
        q_cap_scan_t cs;
        memset(&cs, 0, sizeof(cs));
        cs.outer = outer;
        cs.owner = owner;
        for (int i = 0; i < n->lambda.params.count; i++)
            if (n->lambda.params.items[i]) q_names_add(&cs.params, n->lambda.params.items[i]->param.name);
        q_scan_lambda_body(n->lambda.body, &cs);
        q_item_sep(l);
        fprintf(f, "{\"line\":%u,\"this\":%s,\"locals\":[", n->loc.line,
                cs.uses_this ? "true" : "false");
        for (int i = 0; i < cs.used.n; i++) {
            if (i) fputc(',', f);
            q_json_str(f, cs.used.names[i].str, cs.used.names[i].len);
        }
        fputs("]}", f);
    }
    int cc = zan_ast_child_count(n);
    for (int i = 0; i < cc; i++) q_emit_captures(f, zan_ast_child_at(n, i), outer, owner, l);
}

/* ===================================================================
 * 效果表（--summary 的效果列与 --query 的 escapes 共用）
 *
 * 逐方法 BFS 对几千个公开成员太贵；改为全程序一遍：Tarjan 求调用图强连通
 * 分量定出"被调先完成"的顺序，集合按方法记（同一分量里的方法各有各的 catch，
 * 不能合并）。方法的异常集 = 自身没被本方法 catch 住的 throw ∪ 各调用点上被调
 * 方法的异常集减去该调用点所处 catch 上下文收住的类型；分量完成时在分量内迭代
 * 到不动点。lambda 体里的调用与 throw（deferred）不算本方法会抛的，但算能力。
 * 每方法最多记 FX_MAX 个类型，超出记 more（只有裸 catch / catch (Exception)
 * 拦得住）。rethrow 不计类型；虚调用按静态目标、委托不连边——所以仍是上界。
 * =================================================================== */

#define FX_MAX 32
#define FX_SHOW 6

static bool q_prog_build(q_prog_t *P, zan_ast_node_t *unit, zan_binder_t *binder);
static void q_prog_free(q_prog_t *P);

struct zan_effects {
    q_prog_t   P;
    char     **names;      /* 异常类型名驻留表 */
    int        name_count, name_cap;
    int       *comp;       /* 方法 -> 分量（只用于定顺序） */
    int        comp_count;
    int       *throws;     /* 方法数 * FX_MAX */
    uint8_t   *nthrows;    /* 以下均按方法下标 */
    uint8_t   *more;
    uint32_t  *caps;
    /* Tarjan */
    int       *index, *low, *stack;
    uint8_t   *on_stack;
    int        sp, next;
    int       *direct_first, *direct_ids; /* 方法 -> 未被本方法收住的 throw 类型 id */
};

static int fx_intern(zan_effects_t *E, const char *name) {
    for (int i = 0; i < E->name_count; i++) if (strcmp(E->names[i], name) == 0) return i;
    if (E->name_count == E->name_cap) {
        int ncap = E->name_cap ? E->name_cap * 2 : 64;
        char **nn = (char **)realloc(E->names, (size_t)ncap * sizeof(char *));
        if (!nn) return -1;
        E->names = nn;
        E->name_cap = ncap;
    }
    size_t n = strlen(name);
    char *copy = (char *)malloc(n + 1);
    if (!copy) return -1;
    memcpy(copy, name, n + 1);
    E->names[E->name_count] = copy;
    return E->name_count++;
}

static bool fx_has(const zan_effects_t *E, int c, int id) {
    const int *set = &E->throws[(size_t)c * FX_MAX];
    for (int i = 0; i < E->nthrows[c]; i++) if (set[i] == id) return true;
    return false;
}

/* 返回 true = 集合变了 */
static bool fx_add(zan_effects_t *E, int c, int id) {
    if (id < 0 || fx_has(E, c, id)) return false;
    if (E->nthrows[c] < FX_MAX) {
        E->throws[(size_t)c * FX_MAX + E->nthrows[c]++] = id;
        return true;
    }
    if (!E->more[c]) { E->more[c] = 1; return true; }
    return false;
}

/* 被调方法 wc 的异常集经调用点（catch 上下文 filt）流进方法 c */
static bool fx_flow(zan_effects_t *E, int c, int wc, int filt) {
    bool changed = false;
    int n = E->nthrows[wc];
    for (int i = 0; i < n; i++) {
        int id = E->throws[(size_t)wc * FX_MAX + i];
        if (!q_caught(&E->P, E->names[id], filt)) changed |= fx_add(E, c, id);
    }
    if (E->more[wc] && !E->more[c] && !q_filter_all(&E->P, filt)) {
        E->more[c] = 1;
        changed = true;
    }
    return changed;
}

static void fx_strongconnect(zan_effects_t *E, int v) {
    q_prog_t *P = &E->P;
    E->index[v] = E->low[v] = E->next++;
    E->stack[E->sp++] = v;
    E->on_stack[v] = 1;
    for (int e = 0; e < P->m[v].callee_count; e++) {
        int w = P->m[v].callees[e];
        if (E->index[w] < 0) {
            fx_strongconnect(E, w);
            if (E->low[w] < E->low[v]) E->low[v] = E->low[w];
        } else if (E->on_stack[w] && E->index[w] < E->low[v]) {
            E->low[v] = E->index[w];
        }
    }
    if (E->low[v] != E->index[v]) return;
    int c = E->comp_count++;
    int start = E->sp;
    do { start--; } while (E->stack[start] != v);
    for (int k = start; k < E->sp; k++) E->comp[E->stack[k]] = c;
    /* 被调分量都已完成：先放各成员自身的直接效果，再沿调用点在分量内迭代到不动点 */
    for (int k = start; k < E->sp; k++) {
        int u = E->stack[k];
        for (int j = E->direct_first[u]; j < E->direct_first[u + 1]; j++)
            fx_add(E, u, E->direct_ids[j]);
        int cap = q_capability(P, u);
        if (cap >= 0) E->caps[u] |= 1u << cap;
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (int k = start; k < E->sp; k++) {
            int u = E->stack[k];
            for (int e = 0; e < P->m[u].site_count; e++) {
                const q_site_t *st = &P->m[u].sites[e];
                if (!st->deferred) changed |= fx_flow(E, u, st->callee, st->filt);
                uint32_t nc = E->caps[u] | E->caps[st->callee];
                if (nc != E->caps[u]) { E->caps[u] = nc; changed = true; }
            }
        }
    }
    for (int k = start; k < E->sp; k++) E->on_stack[E->stack[k]] = 0;
    E->sp = start;
}

zan_effects_t *zan_effects_build(zan_ast_node_t *unit, zan_binder_t *binder) {
    zan_effects_t *E = (zan_effects_t *)calloc(1, sizeof(*E));
    if (!E) return NULL;
    if (!q_prog_build(&E->P, unit, binder)) { zan_effects_free(E); return NULL; }
    int n = E->P.count;
    size_t sz = (size_t)(n ? n : 1);
    E->comp = (int *)malloc(sz * sizeof(int));
    E->index = (int *)malloc(sz * sizeof(int));
    E->low = (int *)malloc(sz * sizeof(int));
    E->stack = (int *)malloc(sz * sizeof(int));
    E->on_stack = (uint8_t *)calloc(sz, 1);
    E->throws = (int *)calloc(sz * FX_MAX, sizeof(int));
    E->nthrows = (uint8_t *)calloc(sz, 1);
    E->more = (uint8_t *)calloc(sz, 1);
    E->caps = (uint32_t *)calloc(sz, sizeof(uint32_t));
    E->direct_first = (int *)calloc(sz + 1, sizeof(int));
    if (!E->comp || !E->index || !E->low || !E->stack || !E->on_stack || !E->throws ||
        !E->nthrows || !E->more || !E->caps || !E->direct_first) {
        zan_effects_free(E);
        return NULL;
    }
    int cap = 0, count = 0;
    for (int i = 0; i < n; i++) {
        E->direct_first[i] = count;
        q_method_t *m = &E->P.m[i];
        for (int t = 0; t < m->throw_count; t++) {
            if (m->throws[t].deferred) continue;
            zan_ast_node_t *v = m->throws[t].node->throw_stmt.value;
            if (!v || v->kind != AST_NEW_EXPR) continue;   /* rethrow 不计类型 */
            char tn[128];
            q_throw_name(m->throws[t].node, tn, sizeof(tn));
            if (q_caught(&E->P, tn, m->throws[t].filt)) continue;
            int id = fx_intern(E, tn);
            if (id < 0) continue;
            if (count == cap) {
                int ncap = cap ? cap * 2 : 256;
                int *ni = (int *)realloc(E->direct_ids, (size_t)ncap * sizeof(int));
                if (!ni) break;
                E->direct_ids = ni;
                cap = ncap;
            }
            E->direct_ids[count++] = id;
        }
        E->comp[i] = -1;
        E->index[i] = -1;
    }
    E->direct_first[n] = count;
    for (int v = 0; v < n; v++)
        if (E->index[v] < 0) fx_strongconnect(E, v);
    return E;
}

int zan_effects_describe(const zan_effects_t *E, const zan_ast_node_t *method_decl,
                         char *buf, size_t cap) {
    buf[0] = '\0';
    if (!E || cap < 16) return 0;
    int c = q_find(&E->P, method_decl);
    if (c < 0) return 0;
    size_t o = 0;
    int n = E->nthrows[c];
    if (n) {
        o += (size_t)snprintf(buf + o, cap - o, "throws ");
        for (int k = 0; k < n && k < FX_SHOW && o < cap - 32; k++)
            o += (size_t)snprintf(buf + o, cap - o, "%s%s", k ? ", " : "",
                                  E->names[E->throws[(size_t)c * FX_MAX + k]]);
        if ((E->more[c] || n > FX_SHOW) && o < cap - 8)
            o += (size_t)snprintf(buf + o, cap - o, ", ...");
    }
    if (E->caps[c]) {
        o += (size_t)snprintf(buf + o, cap - o, "%suses ", o ? "; " : "");
        int first = 1;
        for (int k = 0; k < Q_CAP_COUNT && o < cap - 16; k++) {
            if (!(E->caps[c] & (1u << k))) continue;
            o += (size_t)snprintf(buf + o, cap - o, "%s%s", first ? "" : ", ", Q_CAPS[k]);
            first = 0;
        }
    }
    return (int)o;
}

void zan_effects_free(zan_effects_t *E) {
    if (!E) return;
    q_prog_free(&E->P);
    for (int i = 0; i < E->name_count; i++) free(E->names[i]);
    free(E->names);
    free(E->comp); free(E->index); free(E->low); free(E->stack); free(E->on_stack);
    free(E->throws); free(E->nthrows); free(E->more); free(E->caps);
    free(E->direct_first); free(E->direct_ids);
    free(E);
}

/* 方法 v 自身未收住的 throw 里有类型 id 吗 */
static bool fx_direct_has(const zan_effects_t *E, int v, int id) {
    for (int j = E->direct_first[v]; j < E->direct_first[v + 1]; j++)
        if (E->direct_ids[j] == id) return true;
    return false;
}

/* 类型 id 从 t 逃出来的一条示例链：只走不收住该类型的调用点，停在自己 throw 它的方法 */
static int fx_example(const zan_effects_t *E, int t, int id, int *parent, int *queue) {
    const q_prog_t *P = &E->P;
    for (int i = 0; i < P->count; i++) parent[i] = -2;
    int qh = 0, qt = 0;
    parent[t] = -1;
    queue[qt++] = t;
    while (qh < qt) {
        int u = queue[qh++];
        if (fx_direct_has(E, u, id)) return u;
        for (int e = 0; e < P->m[u].site_count; e++) {
            int v = P->m[u].sites[e].callee;
            if (parent[v] != -2 || P->m[u].sites[e].deferred) continue;
            if (q_caught(P, E->names[id], P->m[u].sites[e].filt)) continue;
            parent[v] = u;
            queue[qt++] = v;
        }
    }
    return -1;
}

/* ---------- 单个匹配的报告 ---------- */

typedef struct {
    int target;
    q_list_t *l;
    FILE *f;
    int caller;
} q_callers_t;

static void q_caller_cb(q_prog_t *P, int callee, zan_ast_node_t *site, void *ud) {
    q_callers_t *c = (q_callers_t *)ud;
    if (callee != c->target || c->l->n >= Q_MAX_LIST) return;
    char lab[512];
    q_label(P, c->caller, lab, sizeof(lab));
    const char *file = (P->diag && P->diag->file_names &&
                        (int)site->loc.file_id < P->diag->file_count)
                       ? P->diag->file_names[site->loc.file_id] : "";
    q_item_sep(c->l);
    fprintf(c->f, "{\"caller\":");
    q_json_cstr(c->f, lab);
    fprintf(c->f, ",\"file\":");
    q_json_cstr(c->f, file ? file : "");
    fprintf(c->f, ",\"line\":%u}", site->loc.line);
}

static void q_report(zan_effects_t *E, int t) {
    q_prog_t *P = &E->P;
    FILE *f = P->out;
    q_method_t *M = &P->m[t];
    char lab[512];
    q_label(P, t, lab, sizeof(lab));
    const char *file = (P->diag && P->diag->file_names &&
                        (int)M->decl->loc.file_id < P->diag->file_count)
                       ? P->diag->file_names[M->decl->loc.file_id] : "";
    fprintf(f, "{\"kind\":\"query\",\"symbol\":");
    q_json_cstr(f, lab);
    fprintf(f, ",\"file\":");
    q_json_cstr(f, file ? file : "");
    fprintf(f, ",\"line\":%u", M->decl->loc.line);
    zan_ast_node_t *body = M->decl->method_decl.body;

    q_list_t l = { f, 0 };
    fprintf(f, ",\"allocates\":[");
    q_emit_allocs(f, body, &l);
    fputc(']', f);

    q_names_t outer;
    memset(&outer, 0, sizeof(outer));
    for (int i = 0; i < M->decl->method_decl.params.count; i++)
        if (M->decl->method_decl.params.items[i])
            q_names_add(&outer, M->decl->method_decl.params.items[i]->param.name);
    q_collect_locals(body, &outer);
    l.n = 0;
    fprintf(f, ",\"captures\":[");
    q_emit_captures(f, body, &outer, M->owner, &l);
    fputc(']', f);

    /* 可达集：BFS，parent 记示例链 */
    int *parent = (int *)malloc((size_t)P->count * sizeof(int));
    int *queue = (int *)malloc((size_t)P->count * sizeof(int));
    if (!parent || !queue) { free(parent); free(queue); fputs("}\n", f); return; }
    for (int i = 0; i < P->count; i++) parent[i] = -2;
    int qh = 0, qt = 0;
    parent[t] = -1;
    queue[qt++] = t;
    while (qh < qt) {
        int u = queue[qh++];
        for (int e = 0; e < P->m[u].callee_count; e++) {
            int v = P->m[u].callees[e];
            if (parent[v] != -2) continue;
            parent[v] = u;
            queue[qt++] = v;
        }
    }

    /* throws：direct = 本方法没 catch 住的；escapes = 可能逃出本方法的类型（效果表），
     * 每种一条只走"不收住它"的调用点的示例链 */
    l.n = 0;
    fprintf(f, ",\"throws\":{\"direct\":[");
    for (int k = 0; k < M->throw_count && l.n < Q_MAX_LIST; k++) {
        zan_ast_node_t *tn = M->throws[k].node;
        char name[128];
        q_throw_name(tn, name, sizeof(name));
        bool typed = tn->throw_stmt.value && tn->throw_stmt.value->kind == AST_NEW_EXPR;
        if (M->throws[k].deferred) continue;
        if (typed && q_caught(P, name, M->throws[k].filt)) continue;
        q_item_sep(&l);
        fprintf(f, "{\"type\":");
        q_json_cstr(f, name);
        fprintf(f, ",\"line\":%u}", tn->loc.line);
    }
    fprintf(f, "],\"escapes\":[");
    int c = t;   /* 效果表按方法下标 */
    int *par2 = (int *)malloc((size_t)P->count * sizeof(int));
    int *q2 = (int *)malloc((size_t)P->count * sizeof(int));
    for (int k = 0; c >= 0 && par2 && q2 && k < E->nthrows[c]; k++) {
        int id = E->throws[(size_t)c * FX_MAX + k];
        int end = fx_example(E, t, id, par2, q2);
        char path[2048] = "";
        if (end >= 0) q_path(P, par2, end, path, sizeof(path));
        if (k) fputc(',', f);
        fprintf(f, "{\"type\":");
        q_json_cstr(f, E->names[id]);
        fprintf(f, ",\"via\":");
        q_json_cstr(f, path);
        fputc('}', f);
    }
    free(par2);
    free(q2);
    fprintf(f, "],\"escapes_truncated\":%s,\"note\":\"upper bound: virtual overrides "
               "and delegate targets are not followed\"}",
            (c >= 0 && E->more[c]) ? "true" : "false");

    fprintf(f, ",\"capabilities\":[");
    int capn = 0;
    for (int c = 0; c < Q_CAP_COUNT; c++) {
        for (int k = 0; k < qt; k++) {
            int v = queue[k];
            if (q_capability(P, v) != c) continue;
            char path[2048];
            q_path(P, parent, v, path, sizeof(path));
            if (capn++) fputc(',', f);
            fprintf(f, "{\"capability\":\"%s\",\"via\":", Q_CAPS[c]);
            q_json_cstr(f, path);
            fputc('}', f);
            break;
        }
    }
    fputc(']', f);

    fprintf(f, ",\"calls\":[");
    for (int e = 0; e < M->callee_count && e < Q_MAX_LIST; e++) {
        char cl[512];
        q_label(P, M->callees[e], cl, sizeof(cl));
        if (e) fputc(',', f);
        q_json_cstr(f, cl);
    }
    fputc(']', f);

    l.n = 0;
    fprintf(f, ",\"callers\":[");
    for (int i = 0; i < P->count && l.n < Q_MAX_LIST; i++) {
        q_callers_t cb = { t, &l, f, i };
        q_visit_calls(P, P->m[i].decl->method_decl.body, q_caller_cb, &cb);
    }
    fprintf(f, "],\"callers_truncated\":%s,\"reachable_methods\":%d}\n",
            l.n >= Q_MAX_LIST ? "true" : "false", qt - 1);
    free(parent);
    free(queue);
}

/* ---------- 名字匹配 ---------- */

static bool q_istr_eq(zan_istr_t a, const char *s, size_t n) {
    return a.len == n && memcmp(a.str, s, n) == 0;
}

/* 全程序方法索引 + 调用图（--query 与 --summary 的效果列共用） */
static bool q_prog_build(q_prog_t *P, zan_ast_node_t *unit, zan_binder_t *binder) {
    P->binder = binder;
    if (unit && unit->kind == AST_COMPILATION_UNIT) {
        if (unit->comp_unit.ns) q_collect_types(P, &unit->comp_unit.ns->namespace_decl.members);
        q_collect_types(P, &unit->comp_unit.decls);
    }
    if (!q_build_map(P)) return false;
    for (int i = 0; i < P->count; i++)
        q_scan(P, i, P->m[i].decl->method_decl.body, -1, false);
    return true;
}

static void q_prog_free(q_prog_t *P) {
    for (int i = 0; i < P->count; i++) {
        free(P->m[i].callees);
        free(P->m[i].sites);
        free(P->m[i].throws);
    }
    free(P->filters);
    free(P->m);
    free(P->keys);
    free(P->vals);
}

int zan_query_emit(zan_ast_node_t *unit, zan_binder_t *binder, const zan_diag_t *diag,
                   const char *query, FILE *out) {
    int matches = 0;
    zan_effects_t *E = zan_effects_build(unit, binder);
    if (!E) {
        fprintf(out, "{\"kind\":\"summary\",\"query\":");
        q_json_cstr(out, query);
        fprintf(out, ",\"matches\":0}\n");
        return 1;
    }
    E->P.out = out;
    E->P.diag = diag;
    q_prog_t P = E->P;   /* 名字匹配用的只读视图（与 E->P 共享数组） */

    const char *dot = strrchr(query, '.');
    const char *mname = dot ? dot + 1 : query;
    size_t mlen = strlen(mname);
    const char *tname = NULL;
    size_t tlen = 0;
    if (dot) {
        const char *tstart = dot;
        while (tstart > query && tstart[-1] != '.') tstart--;
        tname = tstart;
        tlen = (size_t)(dot - tstart);
    }
    /* 单名先当类型名（列出它的全部方法），没有这个类型再按方法名找 */
    if (!dot) {
        for (int i = 0; i < P.count; i++) {
            if (!q_istr_eq(P.m[i].owner->type_decl.name, query, strlen(query))) continue;
            q_report(E, i);
            matches++;
        }
        if (matches > 0) goto done;
    }
    for (int i = 0; i < P.count; i++) {
        q_method_t *m = &P.m[i];
        if (tname && !q_istr_eq(m->owner->type_decl.name, tname, tlen)) continue;
        bool hit = q_istr_eq(m->decl->method_decl.name, mname, mlen);
        if (!hit && m->decl->kind == AST_CONSTRUCTOR_DECL && tname &&
            (q_istr_eq(m->owner->type_decl.name, mname, mlen) ||
             (mlen == 4 && memcmp(mname, "ctor", 4) == 0)))
            hit = true;
        if (!hit) continue;
        q_report(E, i);
        matches++;
    }
done:
    fprintf(out, "{\"kind\":\"summary\",\"query\":");
    q_json_cstr(out, query);
    fprintf(out, ",\"matches\":%d}\n", matches);
    zan_effects_free(E);
    return matches > 0 ? 0 : 1;
}
