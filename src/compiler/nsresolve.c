/* nsresolve */

#include "nsresolve.h"
#include "arena.h"
#include "diag.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "../common/host_oom.h"

static zan_istr_t mk_istr(zan_arena_t *a, const char *s, size_t n) {
    zan_istr_t r;
    r.str = zan_arena_strdup(a, s, n);
    r.len = (uint32_t)n;
    return r;
}

static bool ns_istr_eq(zan_istr_t a, zan_istr_t b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.str, b.str, a.len) == 0);
}

static bool ns_has_dot(zan_istr_t s) {
    for (uint32_t i = 0; i < s.len; i++)
        if (s.str[i] == '.') return true;
    return false;
}

/* 构建"<a> */
static zan_istr_t join_ns(zan_arena_t *ar, zan_istr_t a, zan_istr_t b) {
    if (a.len == 0) return b;
    size_t n = (size_t)a.len + 1 + (size_t)b.len;
    char *buf = (char *)zan_arena_alloc(ar, n);
    if (!buf) { zan_istr_t r = {0}; return r; }
    memcpy(buf, a.str, a.len);
    buf[a.len] = '.';
    if (b.len) memcpy(buf + a.len + 1, b.str, b.len);
    zan_istr_t r;
    r.str = buf;
    r.len = (uint32_t)n;
    return r;
}

static zan_istr_t flatten_qname(zan_ast_node_t *q, zan_arena_t *ar) {
    zan_istr_t empty = {0};
    if (!q) return empty;
    if (q->kind == AST_IDENTIFIER) return q->ident.name;
    if (q->kind == AST_QUALIFIED_NAME) {
        size_t total = 0;
        int parts = 0;
        for (int i = 0; i < q->qualified_name.parts.count; i++) {
            zan_ast_node_t *p = q->qualified_name.parts.items[i];
            if (!p || p->kind != AST_IDENTIFIER) continue;
            if (parts > 0) total++;
            total += p->ident.name.len;
            parts++;
        }
        char *buf = (char *)zan_arena_alloc(ar, total ? total : 1);
        if (!buf) return empty;
        size_t n = 0;
        parts = 0;
        for (int i = 0; i < q->qualified_name.parts.count; i++) {
            zan_ast_node_t *p = q->qualified_name.parts.items[i];
            if (!p || p->kind != AST_IDENTIFIER) continue;
            if (parts > 0) buf[n++] = '.';
            memcpy(buf + n, p->ident.name.str, p->ident.name.len);
            n += p->ident.name.len;
            parts++;
        }
        return mk_istr(ar, buf, n);
    }
    return empty;
}

/* Nested declarations are hoisted only after the input units have been merged */
static void stamp_decl_context(zan_ast_node_t *d, zan_istr_t ns,
                               zan_ast_list_t *usings, zan_arena_t *arena) {
    if (!d) return;
    if (ns.len > 0 || (usings && usings->count > 0)) {
        zan_decl_meta_t *m = zan_ast_ensure_decl_meta(d, arena);
        m->ns_name = ns;
        m->ns_usings = usings;
    }
    if (d->kind == AST_CLASS_DECL || d->kind == AST_STRUCT_DECL ||
        d->kind == AST_INTERFACE_DECL || d->kind == AST_ENUM_DECL) {
        for (int i = 0; i < d->type_decl.members.count; i++)
            stamp_decl_context(d->type_decl.members.items[i], ns, usings, arena);
    }
}

void zan_nsresolve_stamp(zan_ast_node_t *unit, zan_arena_t *arena) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    zan_istr_t ns = {0};
    if (unit->comp_unit.ns && unit->comp_unit.ns->kind == AST_NAMESPACE_DECL)
        ns = flatten_qname(unit->comp_unit.ns->namespace_decl.name, arena);
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        stamp_decl_context(d, ns, &unit->comp_unit.usings, arena);
    }
}

typedef struct {
    zan_ast_node_t *decl;
    zan_istr_t simple;   /* declared simple name */
    zan_istr_t ns;       /* namespace ("" if global) */
    zan_istr_t full;     /* ns.simple, or simple if global */
    zan_istr_t final;    /* rewritten name (== simple unless mangled) */
    bool conflicting;
} nr_type_t;

/* 内部辅助逻辑 */
typedef struct {
    zan_istr_t *items;
    int count;
    int cap;
} nr_shadow_t;

/* 内部辅助逻辑 */
typedef struct nr_chain {
    int idx;            /* index into ctx->items */
    int next;           /* next node in the bucket chain, -1 ends */
} nr_chain_t;

typedef struct {
    int *buckets;       /* head chain node per bucket, -1 empty */
    uint32_t mask;
    nr_chain_t *chains;
    int count;
} nr_index_t;

/* Ref-recording set used by the reachability prune (nr_ctx_t */
typedef struct {
    zan_istr_t *items;
    int count;
    int cap;
} zp_refs_t;

static uint32_t nr_hash(zan_istr_t s) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < s.len; i++) {
        h ^= (unsigned char)s.str[i];
        h *= 16777619u;
    }
    return h;
}

typedef struct {
    nr_type_t *items;
    int count;
    zan_arena_t *arena;
    zan_diag_t *diag;
    nr_shadow_t shadow;
    /* 内部辅助逻辑 */
    nr_shadow_t fields;
    nr_index_t by_full;
    nr_index_t by_simple;
    /* 内部辅助逻辑 */
    zp_refs_t *refs;
} nr_ctx_t;

/* key_is_full: nonzero -> index by t->full, zero -> index by t->simple */
static void nr_index_build(nr_index_t *ix, nr_type_t *items, int count,
                           int key_is_full, zan_arena_t *arena) {
    int nb = 16;
    while (nb < count * 2) nb <<= 1;
    ix->mask = (uint32_t)(nb - 1);
    ix->buckets = (int *)zan_arena_alloc(arena, sizeof(int) * (size_t)nb);
    ix->chains = (nr_chain_t *)zan_arena_alloc(arena,
                    sizeof(nr_chain_t) * (size_t)(count > 0 ? count : 1));
    ix->count = 0;
    for (int i = 0; i < nb; i++) ix->buckets[i] = -1;
    for (int i = 0; i < count; i++) {
        zan_istr_t k = key_is_full ? items[i].full : items[i].simple;
        uint32_t b = nr_hash(k) & ix->mask;
        ix->chains[ix->count].idx = i;
        ix->chains[ix->count].next = ix->buckets[b];
        ix->buckets[b] = ix->count++;
    }
}

/* Earliest-declared item (lowest index) whose key equals `key`, or -1 */
static int nr_index_find(nr_index_t *ix, nr_type_t *items, int key_is_full,
                         zan_istr_t key) {
    int best = -1;
    for (int node = ix->buckets[nr_hash(key) & ix->mask]; node >= 0;
         node = ix->chains[node].next) {
        zan_istr_t k = key_is_full ? items[ix->chains[node].idx].full
                                   : items[ix->chains[node].idx].simple;
        if (ns_istr_eq(k, key)) {
            int idx = ix->chains[node].idx;
            if (best < 0 || idx < best) best = idx;
        }
    }
    return best;
}

static void shadow_add(nr_shadow_t *s, zan_istr_t name) {
    if (name.len == 0) return;
    for (int i = 0; i < s->count; i++)
        if (ns_istr_eq(s->items[i], name)) return;
    if (s->count == s->cap) {
        int cap = s->cap ? s->cap * 2 : 16;
        zan_istr_t *items = (zan_istr_t *)realloc(s->items,
                                sizeof(zan_istr_t) * (size_t)cap);
        if (!items) return;
        s->items = items;
        s->cap = cap;
    }
    s->items[s->count++] = name;
}

static bool shadow_has(nr_shadow_t *s, zan_istr_t name) {
    for (int i = 0; i < s->count; i++)
        if (ns_istr_eq(s->items[i], name)) return true;
    return false;
}

static bool is_type_decl_kind(zan_ast_kind_t k) {
    return k == AST_CLASS_DECL || k == AST_STRUCT_DECL ||
           k == AST_INTERFACE_DECL || k == AST_ENUM_DECL ||
           k == AST_DELEGATE_DECL;
}

static zan_istr_t decl_simple_name(zan_ast_node_t *d) {
    if (d->kind == AST_DELEGATE_DECL) return d->method_decl.name;
    return d->type_decl.name;
}

static void decl_set_name(zan_ast_node_t *d, zan_istr_t name) {
    if (d->kind == AST_DELEGATE_DECL) d->method_decl.name = name;
    else d->type_decl.name = name;
}

static nr_type_t *find_full(nr_ctx_t *c, zan_istr_t full) {
    int i = nr_index_find(&c->by_full, c->items, 1, full);
    return i >= 0 ? &c->items[i] : NULL;
}

static nr_type_t *find_full_joined(nr_ctx_t *c, zan_istr_t a, zan_istr_t b) {
    if (a.len == 0) return find_full(c, b);
    if (b.len == 0) return find_full(c, a);
    char buf[512];
    size_t total = (size_t)a.len + 1 + (size_t)b.len;
    char *p = buf;
    if (total > sizeof(buf)) {
        p = (char *)malloc(total);
        if (!p) return NULL;
    }
    memcpy(p, a.str, a.len);
    p[a.len] = '.';
    memcpy(p + a.len + 1, b.str, b.len);
    zan_istr_t key;
    key.str = p;
    key.len = (uint32_t)total;
    nr_type_t *res = find_full(c, key);
    if (p != buf) free(p);
    return res;
}

static zan_istr_t flatten_qname_buf(zan_ast_node_t *q, char *buf, size_t buf_cap, char **out_heap) {
    zan_istr_t empty = {0};
    *out_heap = NULL;
    if (!q) return empty;
    if (q->kind == AST_IDENTIFIER) return q->ident.name;
    if (q->kind == AST_QUALIFIED_NAME) {
        size_t total = 0;
        int parts = 0;
        for (int i = 0; i < q->qualified_name.parts.count; i++) {
            zan_ast_node_t *p = q->qualified_name.parts.items[i];
            if (!p || p->kind != AST_IDENTIFIER) continue;
            if (parts > 0) total++;
            total += p->ident.name.len;
            parts++;
        }
        char *p = buf;
        if (total > buf_cap) {
            p = (char *)malloc(total ? total : 1);
            *out_heap = p;
            if (!p) return empty;
        }
        size_t n = 0;
        parts = 0;
        for (int i = 0; i < q->qualified_name.parts.count; i++) {
            zan_ast_node_t *part = q->qualified_name.parts.items[i];
            if (!part || part->kind != AST_IDENTIFIER) continue;
            if (parts > 0) p[n++] = '.';
            memcpy(p + n, part->ident.name.str, part->ident.name.len);
            n += part->ident.name.len;
            parts++;
        }
        zan_istr_t r;
        r.str = p;
        r.len = (uint32_t)n;
        return r;
    }
    return empty;
}

static int count_simple(nr_ctx_t *c, zan_istr_t simple) {
    /* same-simple-name groups live in one bucket chain; count them there */
    int n = 0;
    for (int node = c->by_simple.buckets[nr_hash(simple) & c->by_simple.mask];
         node >= 0; node = c->by_simple.chains[node].next) {
        if (ns_istr_eq(c->items[c->by_simple.chains[node].idx].simple, simple))
            n++;
    }
    return n;
}

static bool name_taken(nr_ctx_t *c, const char *s, size_t n) {
    for (int i = 0; i < c->count; i++) {
        zan_istr_t f = c->items[i].final;
        if (f.len == (uint32_t)n && memcmp(f.str, s, n) == 0) return true;
    }
    return false;
}

/* mangle "A */
static zan_istr_t mangle(nr_ctx_t *c, zan_istr_t full) {
    char buf[600];
    size_t n = 0;
    for (uint32_t i = 0; i < full.len && n < sizeof buf - 12; i++)
        buf[n++] = (full.str[i] == '.') ? '_' : full.str[i];
    buf[n] = 0;
    size_t base = n;
    int suffix = 2;
    while (name_taken(c, buf, n)) {
        int w = snprintf(buf + base, sizeof buf - base, "_%d", suffix++);
        n = base + (w > 0 ? (size_t)w : 0);
    }
    return mk_istr(c->arena, buf, n);
}

static void zan_refs_add(zp_refs_t *r, zan_istr_t name, zan_arena_t *arena) {
    if (!r || !name.len) return;
    for (int i = 0; i < r->count; i++)
        if (ns_istr_eq(r->items[i], name)) return;
    if (r->count == r->cap) {
        int ncap = r->cap ? r->cap * 2 : 64;
        zan_istr_t *grown = (zan_istr_t *)zan_arena_alloc(
            arena, (size_t)ncap * sizeof(zan_istr_t));
        if (!grown) return;
        for (int i = 0; i < r->count; i++) grown[i] = r->items[i];
        r->items = grown;
        r->cap = ncap;
    }
    r->items[r->count++] = name;
}

/* 内部辅助逻辑 */
static int decl_type_param_count(const zan_ast_node_t *d) {
    if (!d) return 0;
    if (d->kind == AST_DELEGATE_DECL) return d->method_decl.type_params.count;
    return d->type_decl.type_params.count;
}

/* 内部辅助逻辑 */
static bool arity_matches(const zan_ast_node_t *tr, const nr_type_t *t) {
    if (tr->type_ref.type_args.count == 0) return true;
    return decl_type_param_count(t->decl) == tr->type_ref.type_args.count;
}

/* 内部辅助逻辑 */
static int count_simple_matching(nr_ctx_t *c, zan_istr_t simple,
                                 const zan_ast_node_t *tr) {
    int want = tr->type_ref.type_args.count;
    if (want == 0) return count_simple(c, simple);
    int n = 0;
    for (int node = c->by_simple.buckets[nr_hash(simple) & c->by_simple.mask];
         node >= 0; node = c->by_simple.chains[node].next) {
        int idx = c->by_simple.chains[node].idx;
        if (ns_istr_eq(c->items[idx].simple, simple) &&
            decl_type_param_count(c->items[idx].decl) == want)
            n++;
    }
    return n;
}

static void resolve_ref(nr_ctx_t *c, zan_ast_node_t *tr,
                        zan_istr_t ctx_ns, zan_ast_list_t *usings) {
    zan_istr_t R = tr->type_ref.name;
    if (R.len == 0) return;
    nr_type_t *t = NULL;

    if (ns_has_dot(R)) {
        /* explicit qualified reference: try full, then using-prefixed */
        t = find_full(c, R);
        if (!t && usings) {
            char ubuf[256];
            for (int i = 0; i < usings->count && !t; i++) {
                zan_ast_node_t *u = usings->items[i];
                if (!u || u->kind != AST_USING_DECL || u->using_decl.is_static) continue;
                char *uheap = NULL;
                zan_istr_t up = flatten_qname_buf(u->using_decl.name, ubuf, sizeof(ubuf), &uheap);
                t = find_full_joined(c, up, R);
                if (uheap) free(uheap);
            }
        }
        if (t && !arity_matches(tr, t)) {
            /* 内部辅助逻辑 */
            t = NULL;
        }
        if (t) {
            tr->type_ref.name = t->final;
            if (c->refs) zan_refs_add(c->refs, t->final, c->arena);
            return;
        }
        /* qualified reference to a non-declared (e */
        uint32_t last = 0;
        for (uint32_t i = 0; i < R.len; i++)
            if (R.str[i] == '.') last = i + 1;
        tr->type_ref.name = mk_istr(c->arena, R.str + last, R.len - last);
        if (c->refs)
            zan_refs_add(c->refs, tr->type_ref.name, c->arena);
        return;
    }

    /* 内部辅助逻辑 */
    t = find_full_joined(c, ctx_ns, R);
    if (!t && ctx_ns.len > 0) {
        char abuf[256];
        if (ctx_ns.len < sizeof(abuf)) {
            memcpy(abuf, ctx_ns.str, ctx_ns.len);
            abuf[ctx_ns.len] = '\0';
            for (char *dot = strrchr(abuf, '.'); dot; dot = strrchr(abuf, '.')) {
                *dot = '\0';
                zan_istr_t anc = mk_istr(c->arena, abuf, (uint32_t)(dot - abuf));
                t = find_full_joined(c, anc, R);
                if (t) break;
            }
        }
    }
    if (t && !arity_matches(tr, t)) {
        /* 内部辅助逻辑 */
        if (c->refs) zan_refs_add(c->refs, R, c->arena);
        return;
    }
    if (!t && usings) {
        char ubuf[256];
        for (int i = 0; i < usings->count && !t; i++) {
            zan_ast_node_t *u = usings->items[i];
            if (!u || u->kind != AST_USING_DECL || u->using_decl.is_static) continue;
            char *uheap = NULL;
            zan_istr_t up = flatten_qname_buf(u->using_decl.name, ubuf, sizeof(ubuf), &uheap);
            t = find_full_joined(c, up, R);
            if (uheap) free(uheap);
            if (t && !arity_matches(tr, t)) t = NULL;
        }
    }
    if (t) {
        tr->type_ref.name = t->final;
        if (c->refs) zan_refs_add(c->refs, t->final, c->arena);
        return;
    }
    /* 内部辅助逻辑 */
    if (c->refs) zan_refs_add(c->refs, R, c->arena);

    /* 内部辅助逻辑 */
    if (count_simple_matching(c, R, tr) >= 2 && !find_full(c, R)) {
        zan_diag_emit(c->diag, DIAG_ERROR, tr->loc,
                      "ambiguous type '%.*s'; qualify it with its namespace",
                      (int)R.len, R.str);
    }
}

/* 内部辅助逻辑 */
static void resolve_static_receiver(nr_ctx_t *c, zan_ast_node_t *id,
                                    zan_istr_t ctx_ns, zan_ast_list_t *usings) {
    zan_istr_t R = id->ident.name;
    if (R.len == 0 || shadow_has(&c->shadow, R)) return;

    /* The prune needs every receiver name as an edge: `DataTable */
    if (c->refs) zan_refs_add(c->refs, R, c->arena);

    nr_type_t *t = NULL;
    /* 内部辅助逻辑 */
    t = find_full_joined(c, ctx_ns, R);
    if (!t && ctx_ns.len > 0) {
        char abuf[256];
        if (ctx_ns.len < sizeof(abuf)) {
            memcpy(abuf, ctx_ns.str, ctx_ns.len);
            abuf[ctx_ns.len] = '\0';
            for (char *dot = strrchr(abuf, '.'); dot; dot = strrchr(abuf, '.')) {
                *dot = '\0';
                zan_istr_t anc = mk_istr(c->arena, abuf, (uint32_t)(dot - abuf));
                t = find_full_joined(c, anc, R);
                if (t) break;
            }
        }
    }
    if (!t && usings) {
        char ubuf[256];
        for (int i = 0; i < usings->count && !t; i++) {
            zan_ast_node_t *u = usings->items[i];
            if (!u || u->kind != AST_USING_DECL || u->using_decl.is_static) continue;
            char *uheap = NULL;
            zan_istr_t up = flatten_qname_buf(u->using_decl.name, ubuf, sizeof(ubuf), &uheap);
            t = find_full_joined(c, up, R);
            if (uheap) free(uheap);
        }
    }
    if (!t) t = find_full(c, R);
    if (t) {
        id->ident.name = t->final;
        if (c->refs) zan_refs_add(c->refs, t->final, c->arena);
    }
}

/* Flattens a receiver made of identifiers and member accesses (`Gui */
static zan_istr_t flatten_receiver(nr_ctx_t *c, zan_ast_node_t *n) {
    zan_istr_t empty = {0};
    if (!n) return empty;
    if (n->kind == AST_IDENTIFIER) return n->ident.name;
    if (n->kind == AST_MEMBER_ACCESS) {
        zan_istr_t head = flatten_receiver(c, n->member.object);
        if (head.len == 0) return empty;
        return join_ns(c->arena, head, n->member.name);
    }
    return empty;
}

/* Rewrites a namespace-qualified static receiver (`Beta */
static void resolve_qualified_receiver(nr_ctx_t *c, zan_ast_node_t *recv) {
    zan_istr_t full = flatten_receiver(c, recv);
    if (full.len == 0 || !ns_has_dot(full)) return;
    nr_type_t *t = find_full(c, full);
    if (!t) {
        /* `A */
        if (c->refs) {
            zan_ast_node_t *seg = recv;
            while (seg && seg->kind == AST_MEMBER_ACCESS) {
                zan_refs_add(c->refs, seg->member.name, c->arena);
                seg = seg->member.object;
            }
            if (seg && seg->kind == AST_IDENTIFIER)
                zan_refs_add(c->refs, seg->ident.name, c->arena);
        }
        return;
    }
    recv->kind = AST_IDENTIFIER;
    recv->ident.name = t->final;
    recv->ident.inst_type_ref = NULL;
    if (c->refs) zan_refs_add(c->refs, t->final, c->arena);
}

static void collect_shadows(nr_ctx_t *c, zan_ast_node_t *n);

static void collect_shadows_list(nr_ctx_t *c, zan_ast_list_t *l) {
    if (!l) return;
    for (int i = 0; i < l->count; i++) collect_shadows(c, l->items[i]);
}

/* 内部辅助逻辑 */
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
    default:
        break;
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
    /* 内部辅助逻辑 */
    if (zan_ast_ns_usings(n)) {
        ns = zan_ast_ns_name(n);
        usings = zan_ast_ns_usings(n);
    }
    switch (n->kind) {
    case AST_TYPE_REF:
        resolve_ref(c, n, ns, usings);
        nr_walk_list(c, &n->type_ref.type_args, ns, usings);
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
            resolve_static_receiver(c, n->member.object, ns, usings);
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

    default:
        /* literals, identifiers, this/base, labels, goto: no nested types */
        break;
    }
}

void zan_nsresolve_run(zan_ast_node_t *unit, zan_arena_t *arena, zan_diag_t *diag) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    zan_ast_list_t *decls = &unit->comp_unit.decls;
    if (decls->count == 0) return;

    nr_ctx_t c;
    c.arena = arena;
    c.diag = diag;
    c.count = 0;
    /* 内部辅助逻辑 */
    c.refs = NULL;
    c.shadow.items = NULL;
    c.shadow.count = 0;
    c.shadow.cap = 0;
    c.fields.items = NULL;
    c.fields.count = 0;
    c.fields.cap = 0;
    c.items = (nr_type_t *)zan_arena_alloc(arena,
                    sizeof(nr_type_t) * (size_t)(decls->count + 1));

    /* 1. collect declared types */
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

    /* 3. give each conflicting declaration a unique mangled name */
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

    /* 内部辅助逻辑 */
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

    /* Roots: the program itself */
    /* 内部辅助逻辑 */
    int user_roots = 0;
    for (int i = 0; i < n; i++)
        if (!items[i].decl->from_stdlib) user_roots++;
    if (user_roots == 0) return;
    /* Escape hatch for A/B timing and for bisecting a bad prune: */
    if (getenv("ZAN_NO_PRUNE")) return;
    unsigned char *kept = (unsigned char *)calloc((size_t)n, 1);
    if (!kept) return;
    for (int i = 0; i < n; i++) {
        zan_ast_node_t *d = items[i].decl;
        if (!d->from_stdlib) { kept[i] = 1; continue; }
        /* 内部辅助逻辑 */
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
        }
    }

    /* 内部辅助逻辑 */
    unsigned char *walked = (unsigned char *)calloc((size_t)n, 1);
    if (!walked) { free(kept); return; }
    nr_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.arena = arena;
    c.diag = NULL; /* Prune is purely for reachability: suppress duplicate diagnostics */
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
            /* 内部辅助逻辑 */
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

    /* Rewrite the merged decl list, dropping unreachable type declarations */
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
