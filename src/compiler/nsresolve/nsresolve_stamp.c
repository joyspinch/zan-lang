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

/* 本文件 using 列表的快照。
 *
 * 驱动层（main.c）会把所有编译输入——用户文件 + 各包源码——的 using 声明
 * 合并进同一个 compilation unit 的 usings 列表。decl 里存的是"列表指针"，
 * 所以第一个文件的声明会看到全工程的 using：某个包文件里的 `using
 * System.Data;` 会让用户的裸名 `Window` 绑到 System.Data.Window，而不是
 * 自己 import 的那个（A438）。快照把每个文件的可见导入钉死在自己身上。 */
static zan_ast_list_t *snapshot_usings(zan_ast_node_t *unit, zan_arena_t *arena) {
    zan_ast_list_t *src = &unit->comp_unit.usings;
    if (src->count == 0) return src;
    zan_ast_list_t *snap =
        (zan_ast_list_t *)zan_arena_alloc(arena, sizeof(zan_ast_list_t));
    if (!snap) return src;
    zan_ast_list_init(snap);
    for (int i = 0; i < src->count; i++)
        zan_ast_list_push(snap, src->items[i], arena);
    return snap;
}

void zan_nsresolve_stamp(zan_ast_node_t *unit, zan_arena_t *arena) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    zan_istr_t ns = {0};
    if (unit->comp_unit.ns && unit->comp_unit.ns->kind == AST_NAMESPACE_DECL)
        ns = flatten_qname(unit->comp_unit.ns->namespace_decl.name, arena);
    zan_ast_list_t *usings = snapshot_usings(unit, arena);
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        stamp_decl_context(d, ns, usings, arena);
    }
}

typedef struct {
    zan_ast_node_t *decl;
    zan_istr_t simple;   /* source name before renaming */
    zan_istr_t full;     /* namespace, enclosing types, and source name */
    zan_istr_t alias;    /* legacy namespace + short name for nested types */
    zan_istr_t final;    /* flat name used by the binder */
    bool conflicting;
} nr_type_t;

typedef enum { NR_SIMPLE, NR_FULL, NR_ALIAS, NR_FINAL } nr_key_t;

static zan_istr_t nr_key(const nr_type_t *t, nr_key_t key) {
    switch (key) {
    case NR_FULL: return t->full;
    case NR_ALIAS: return t->alias;
    case NR_FINAL: return t->final;
    default: return t->simple;
    }
}

typedef struct {
    zan_istr_t *items;
    int count;
    int cap;
} nr_shadow_t;

typedef struct nr_chain {
    int idx;
    int next;
} nr_chain_t;

typedef struct {
    int *buckets;
    uint32_t mask;
    nr_chain_t *chains;
    int count;
} nr_index_t;

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
    nr_shadow_t fields;
    nr_index_t by_full;
    nr_index_t by_simple;
    nr_index_t by_alias;
    nr_index_t by_final;
    const nr_type_t *owner;
    zp_refs_t *refs;
    /* 数组的元素类型节点与外层的包装节点**同名**：包装节点已经报过
     * "ambiguous type"，元素节点再报一遍就是同一条诊断出现两次。解析元素
     * 节点时把这个标志置上，抑制重复的那一条。 */
    bool quiet_ambiguity;
    bool qualified_keys;
    const zan_ast_list_t *type_params;
    const zan_ast_list_t *method_type_params;
} nr_ctx_t;

static void nr_index_build(nr_index_t *ix, nr_type_t *items, int count,
                           nr_key_t key, zan_arena_t *arena) {
    int nb = 16;
    while (nb < count * 2) nb <<= 1;
    ix->mask = (uint32_t)(nb - 1);
    ix->buckets = (int *)zan_arena_alloc(arena, sizeof(int) * (size_t)nb);
    ix->chains = (nr_chain_t *)zan_arena_alloc(arena,
                    sizeof(nr_chain_t) * (size_t)(count > 0 ? count : 1));
    ix->count = 0;
    for (int i = 0; i < nb; i++) ix->buckets[i] = -1;
    for (int i = 0; i < count; i++) {
        zan_istr_t k = nr_key(&items[i], key);
        if (!k.len) continue;
        uint32_t b = nr_hash(k) & ix->mask;
        ix->chains[ix->count].idx = i;
        ix->chains[ix->count].next = ix->buckets[b];
        ix->buckets[b] = ix->count++;
    }
}

static int nr_index_find(nr_index_t *ix, nr_type_t *items, nr_key_t kind,
                         zan_istr_t key) {
    int best = -1;
    for (int node = ix->buckets[nr_hash(key) & ix->mask]; node >= 0;
         node = ix->chains[node].next) {
        if (ns_istr_eq(nr_key(&items[ix->chains[node].idx], kind), key)) {
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

static zan_istr_t decl_simple_name(const zan_ast_node_t *d) {
    if (d->kind == AST_DELEGATE_DECL) return d->method_decl.name;
    return d->type_decl.name;
}

static void decl_set_name(zan_ast_node_t *d, zan_istr_t name) {
    if (d->kind == AST_DELEGATE_DECL) d->method_decl.name = name;
    else d->type_decl.name = name;
}

static zan_ast_node_t *decl_owner(const zan_ast_node_t *decl) {
    if (decl->kind == AST_DELEGATE_DECL)
        return decl->meta ? decl->meta->delegate_host : NULL;
    return decl->type_decl.nested_host;
}

static zan_istr_t decl_source_name(zan_ast_node_t *decl) {
    zan_istr_t name = zan_ast_orig_name(decl);
    return name.len ? name : decl_simple_name(decl);
}

static zan_istr_t decl_full_name(zan_arena_t *arena, zan_ast_node_t *decl) {
    zan_ast_node_t *owner = decl_owner(decl);
    zan_istr_t prefix = owner ? decl_full_name(arena, owner) : zan_ast_ns_name(decl);
    return join_ns(arena, prefix, decl_source_name(decl));
}

static bool same_decl_identity(const nr_type_t *a, const nr_type_t *b) {
    /* A textual path can cross the namespace boundary at different points. */
    return ns_istr_eq(a->full, b->full) &&
           ns_istr_eq(zan_ast_ns_name(a->decl), zan_ast_ns_name(b->decl));
}

static nr_type_t *find_full_exact(nr_ctx_t *c, zan_istr_t full) {
    int i = nr_index_find(&c->by_full, c->items, NR_FULL, full);
    return i >= 0 ? &c->items[i] : NULL;
}

static nr_type_t *find_full(nr_ctx_t *c, zan_istr_t full) {
    nr_type_t *exact = find_full_exact(c, full);
    if (exact) return exact;

    /* A nested type was historically visible through its namespace alone.
     * Keep that spelling when it identifies only one declaration identity. */
    nr_type_t *found = NULL;
    nr_index_t *ix = &c->by_alias;
    for (int node = ix->buckets[nr_hash(full) & ix->mask]; node >= 0;
         node = ix->chains[node].next) {
        nr_type_t *t = &c->items[ix->chains[node].idx];
        if (!ns_istr_eq(t->alias, full)) continue;
        if (found && !same_decl_identity(found, t)) return NULL;
        found = t;
    }
    return found;
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

static nr_type_t *find_lexical_type(nr_ctx_t *c, zan_istr_t name) {
    if (!c->owner) return NULL;
    zan_istr_t prefix = c->owner->full;
    for (zan_ast_node_t *owner = c->owner->decl; owner; owner = decl_owner(owner)) {
        nr_type_t *t = find_full_joined(c, prefix, name);
        if (t) return t;
        while (prefix.len > 0 && prefix.str[prefix.len - 1] != '.') prefix.len--;
        if (prefix.len > 0) prefix.len--;
    }
    return NULL;
}

static nr_type_t *find_final(nr_ctx_t *c, zan_istr_t name) {
    int i = nr_index_find(&c->by_final, c->items, NR_FINAL, name);
    return i >= 0 ? &c->items[i] : NULL;
}

static nr_type_t *find_namespace_type(nr_ctx_t *c, zan_istr_t ns, zan_istr_t name) {
    for (;;) {
        nr_type_t *t = find_full_joined(c, ns, name);
        if (t || ns.len == 0) return t;
        while (ns.len > 0 && ns.str[ns.len - 1] != '.') ns.len--;
        if (ns.len == 0) return NULL;
        ns.len--;
    }
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

static int decl_type_param_count(const zan_ast_node_t *d) {
    if (!d) return 0;
    if (d->kind == AST_DELEGATE_DECL) return d->method_decl.type_params.count;
    return d->type_decl.type_params.count;
}

static nr_type_t *find_imported_type(nr_ctx_t *c, const zan_ast_list_t *usings,
                                     zan_istr_t name, int arity) {
    if (!usings) return NULL;
    char buf[256];
    for (int i = 0; i < usings->count; i++) {
        zan_ast_node_t *u = usings->items[i];
        if (!u || u->kind != AST_USING_DECL || u->using_decl.is_static) continue;
        char *heap = NULL;
        zan_istr_t ns = flatten_qname_buf(u->using_decl.name, buf, sizeof(buf), &heap);
        nr_type_t *t = find_full_joined(c, ns, name);
        free(heap);
        if (t && (arity < 0 || decl_type_param_count(t->decl) == arity)) return t;
    }
    return NULL;
}

static bool ambiguous_simple_type(nr_ctx_t *c, zan_istr_t simple, int arity) {
    const nr_type_t *first = NULL;
    for (int node = c->by_simple.buckets[nr_hash(simple) & c->by_simple.mask];
         node >= 0; node = c->by_simple.chains[node].next) {
        const nr_type_t *t = &c->items[c->by_simple.chains[node].idx];
        if (!ns_istr_eq(t->simple, simple) || decl_type_param_count(t->decl) != arity) continue;
        if (first && !same_decl_identity(first, t)) return true;
        first = t;
    }
    return false;
}

static bool matches_type_param(const zan_ast_list_t *params, zan_istr_t name) {
    if (!params) return false;
    for (int i = 0; i < params->count; i++) {
        const zan_ast_node_t *param = params->items[i];
        if (param && param->kind == AST_IDENTIFIER && ns_istr_eq(param->ident.name, name))
            return true;
    }
    return false;
}

static zan_istr_t qualified_builtin_name(zan_istr_t name) {
    static const char *const names[] = {
        "System.Collections.Generic.List",
        "System.Collections.Generic.Dictionary",
        "System.Text.StringBuilder",
        "System.Span",
        "System.ReadOnlySpan",
        "System.Reflection.TypeInfo",
        "System.Threading.Tasks.Task",
        "System.Func",
        "System.Action",
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const char *full = names[i];
        size_t len = strlen(full);
        if (name.len == len && memcmp(name.str, full, len) == 0) {
            const char *simple = strrchr(full, '.') + 1;
            return (zan_istr_t){(char *)simple, (uint32_t)(full + len - simple)};
        }
    }
    return name;
}

static void resolve_ref(nr_ctx_t *c, zan_ast_node_t *tr,
                        zan_istr_t ctx_ns, zan_ast_list_t *usings) {
    if (tr->type_ref.ns_decl) {
        tr->type_ref.name = decl_simple_name(tr->type_ref.ns_decl);
        if (c->refs) zan_refs_add(c->refs, tr->type_ref.name, c->arena);
        return;
    }
    zan_istr_t R = tr->type_ref.name;
    if (R.len == 0) return;
    int arity = tr->type_ref.type_args.count;
    if (arity == 0 && (matches_type_param(c->type_params, R) ||
                      matches_type_param(c->method_type_params, R))) return;

    bool qualified = ns_has_dot(R);
    nr_type_t *t = qualified ? find_full_exact(c, R) : NULL;
    if (!t) t = find_lexical_type(c, R);
    if (!t) t = find_namespace_type(c, ctx_ns, R);
    if (!t) t = find_imported_type(c, usings, R, arity);
    if (!t) t = find_full(c, R);
    if (!t && !qualified) t = find_final(c, R);
    if (t) {
        if (decl_type_param_count(t->decl) == arity) {
            tr->type_ref.ns_decl = t->decl;
            tr->type_ref.name = t->final;
        }
        if (c->refs) zan_refs_add(c->refs, tr->type_ref.name, c->arena);
        return;
    }
    if (qualified) {
        R = qualified_builtin_name(R);
        tr->type_ref.name = R;
    }
    if (c->refs) zan_refs_add(c->refs, R, c->arena);
    if (qualified) return; /* Preserve unresolved qualifiers for the binder diagnostic. */

    if (c->diag && !c->quiet_ambiguity && ambiguous_simple_type(c, R, arity) &&
        !find_full(c, R)) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, tr->loc, 1019,
                      "ambiguous type '%.*s'; qualify it with its namespace",
                      (int)R.len, R.str);
    }
}

/* 类型声明里有没有同名成员（静态成员与实例成员都算——只用于给裸名消歧，
 * 不参与可见性/重载判定）。 */
static bool decl_has_member(const zan_ast_node_t *d, zan_istr_t name) {
    if (!d || name.len == 0) return false;
    if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL &&
        d->kind != AST_INTERFACE_DECL && d->kind != AST_ENUM_DECL)
        return false;
    zan_ast_list_t *ms = &d->type_decl.members;
    for (int i = 0; i < ms->count; i++) {
        zan_ast_node_t *m = ms->items[i];
        if (!m) continue;
        zan_istr_t mn = {0};
        switch (m->kind) {
        case AST_METHOD_DECL: mn = m->method_decl.name; break;
        case AST_FIELD_DECL:
        case AST_PROPERTY_DECL: mn = m->field_decl.name; break;
        case AST_ENUM_MEMBER: mn = m->enum_member.name; break;
        default: break;
        }
        if (mn.len == name.len && mn.str &&
            memcmp(mn.str, name.str, (size_t)name.len) == 0)
            return true;
    }
    return false;
}

/* 裸名消歧：同名类型不止一个时，用被访问的成员挑唯一命中那个
 * （"Color Color" 的思路）。命中唯一 → 用它；命中零个或多个 → 不猜，
 * 留给后面的诊断说话。 */
static nr_type_t *pick_by_member(nr_ctx_t *c, zan_istr_t simple,
                                  zan_istr_t member, bool *ambiguous) {
    nr_type_t *hit = NULL;
    for (int node = c->by_simple.buckets[nr_hash(simple) & c->by_simple.mask];
         node >= 0; node = c->by_simple.chains[node].next) {
        nr_type_t *t = &c->items[c->by_simple.chains[node].idx];
        if (!ns_istr_eq(t->simple, simple) || !decl_has_member(t->decl, member)) continue;
        if (hit && !same_decl_identity(hit, t)) {
            *ambiguous = true;
            return NULL;
        }
        hit = t;
    }
    return hit;
}

static bool receiver_shadowed(nr_ctx_t *c, zan_istr_t name) {
    return shadow_has(&c->shadow, name) || shadow_has(&c->fields, name);
}

static void resolve_static_receiver(nr_ctx_t *c, zan_ast_node_t *id,
                                    zan_istr_t ctx_ns, zan_ast_list_t *usings,
                                    zan_istr_t member) {
    if (id->ident.ns_decl) {
        zan_istr_t name = decl_simple_name(id->ident.ns_decl);
        if (!ns_istr_eq(id->ident.name, name)) id->ident.is_type_receiver = true;
        id->ident.name = name;
        if (c->refs) zan_refs_add(c->refs, name, c->arena);
        return;
    }
    zan_istr_t R = id->ident.name;
    if (R.len == 0 || receiver_shadowed(c, R)) return;

    nr_type_t *t = find_lexical_type(c, R);
    if (!t) t = find_namespace_type(c, ctx_ns, R);
    if (!t) t = find_imported_type(c, usings, R, -1);
    if (!t) t = find_full(c, R);
    /* 名字被多个命名空间占用（System.Data.Window / System.Automation.Window /
     * Gui.Backend.Window）时，裸名 + 成员访问按"谁声明了这个成员"消歧：
     * 命中唯一就用它；命中多个 → 报歧义（和类型位置的诊断同一口径）；
     * 命中零个 → 不猜，交给后面的诊断。 */
    if (!t && member.len && !c->qualified_keys) {
        bool ambiguous = false;
        t = pick_by_member(c, R, member, &ambiguous);
        if (ambiguous && c->diag)
            zan_diag_emit_code(c->diag, DIAG_ERROR, id->loc, 1019,
                          "ambiguous type '%.*s'; qualify it with its namespace",
                          (int)R.len, R.str);
    }
    if (!t) t = find_final(c, R);
    if (t) {
        if (!ns_istr_eq(R, t->final)) id->ident.is_type_receiver = true;
        id->ident.ns_decl = t->decl;
        id->ident.name = t->final;
    }
    if (c->refs) zan_refs_add(c->refs, id->ident.name, c->arena);
}

static zan_istr_t flatten_receiver(nr_ctx_t *c, zan_ast_node_t *n) {
    zan_istr_t empty = {0};
    if (!n) return empty;
    if (n->kind == AST_IDENTIFIER) {
        if (n->ident.inst_type_ref || n->ident.ns_decl ||
            receiver_shadowed(c, n->ident.name)) return empty;
        return n->ident.name;
    }
    if (n->kind == AST_MEMBER_ACCESS) {
        zan_istr_t head = flatten_receiver(c, n->member.object);
        if (head.len == 0) return empty;
        return join_ns(c->arena, head, n->member.name);
    }
    return empty;
}

static void resolve_qualified_receiver(nr_ctx_t *c, zan_ast_node_t *recv,
                                       zan_istr_t ctx_ns, zan_ast_list_t *usings) {
    zan_istr_t full = flatten_receiver(c, recv);
    if (full.len == 0) return;
    nr_type_t *t = find_full_exact(c, full);
    if (!t) t = find_lexical_type(c, full);
    if (!t) t = find_namespace_type(c, ctx_ns, full);
    if (!t) t = find_imported_type(c, usings, full, -1);
    if (!t) t = find_full(c, full);
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
    /* The original member token is the terminal type name in the receiver.
     * Its common name span survives the in-place change of the payload. */
    recv->kind = AST_IDENTIFIER;
    recv->ident.name = t->final;
    recv->ident.inst_type_ref = NULL;
    recv->ident.method_group_target = NULL;
    recv->ident.source_ident = false;
    recv->ident.is_type_receiver = true;
    recv->ident.variance = 0;
    recv->ident.ns_decl = t->decl;
    if (c->refs) zan_refs_add(c->refs, t->final, c->arena);
}

