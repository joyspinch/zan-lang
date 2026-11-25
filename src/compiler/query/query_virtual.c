/* query_virtual.c: S2 调用图展开虚调用与方法组（docs/ZAN_STANDARD.md §1）
 *
 * 由 query.c #include（共享 q_prog_t 等文件内类型）。
 *
 * 1. 覆盖索引：每个方法记下"可能代替它运行的方法"——派生类里 override 它的、
 *    实现它的接口方法的（按名字 + 形参个数匹配）。调用解析到虚/抽象/接口方法时，
 *    调用点同时连到这些覆盖者，--query 的 escapes/能力与 --summary 的效果列不再
 *    漏掉"真正运行的那个实现"。祖先沿声明的基类型与接口逐层找（含接口继承）。
 * 2. 方法组：方法名当值用（`b.Click += OnClick`、`Action a = Save;`、
 *    `this.OnClick`）时，按当前类型及其基类里的同名方法连 deferred 边——和 lambda
 *    一样，委托被调用时才执行：算能力，不算本方法会抛的异常。 */

#define QV_MAX_ANCESTORS 64

static zan_ast_node_t *qv_decl_of_ref(q_prog_t *P, zan_ast_node_t *tref) {
    if (!tref || tref->kind != AST_TYPE_REF) return NULL;
    zan_symbol_t *s = zan_binder_lookup(P->binder, tref->type_ref.name);
    if (!s || !s->decl) return NULL;
    zan_ast_kind_t k = s->decl->kind;
    return (k == AST_CLASS_DECL || k == AST_STRUCT_DECL || k == AST_INTERFACE_DECL)
           ? s->decl : NULL;
}

/* owner 的全部祖先类型（基类链 + 实现/继承的接口），广度优先、去重 */
static int qv_ancestors(q_prog_t *P, zan_ast_node_t *owner, zan_ast_node_t **out, int max) {
    int n = 0, head = 0;
    zan_ast_node_t *queue[QV_MAX_ANCESTORS];
    int qt = 0;
    queue[qt++] = owner;
    while (head < qt && n < max) {
        zan_ast_node_t *t = queue[head++];
        for (int i = 0; i < t->type_decl.bases.count && n < max; i++) {
            zan_ast_node_t *b = qv_decl_of_ref(P, t->type_decl.bases.items[i]);
            if (!b || b == owner) continue;
            bool seen = false;
            for (int k = 0; k < n; k++) if (out[k] == b) { seen = true; break; }
            if (seen) continue;
            out[n++] = b;
            if (qt < QV_MAX_ANCESTORS) queue[qt++] = b;
        }
    }
    return n;
}

static bool qv_same_shape(const zan_ast_node_t *a, const zan_ast_node_t *b) {
    return a->method_decl.name.len == b->method_decl.name.len &&
           memcmp(a->method_decl.name.str, b->method_decl.name.str, a->method_decl.name.len) == 0 &&
           a->method_decl.params.count == b->method_decl.params.count;
}

static void qv_add_overrider(q_method_t *m, int who) {
    for (int i = 0; i < m->ov_count; i++) if (m->overriders[i] == who) return;
    if (m->ov_count == m->ov_cap) {
        int ncap = m->ov_cap ? m->ov_cap * 2 : 4;
        int *no = (int *)realloc(m->overriders, (size_t)ncap * sizeof(int));
        if (!no) return;
        m->overriders = no;
        m->ov_cap = ncap;
    }
    m->overriders[m->ov_count++] = who;
}

static void qv_build_overrides(q_prog_t *P) {
    zan_ast_node_t *anc[QV_MAX_ANCESTORS];
    for (int i = 0; i < P->count; i++) {
        zan_ast_node_t *d = P->m[i].decl, *owner = P->m[i].owner;
        if (d->kind != AST_METHOD_DECL || owner->kind == AST_INTERFACE_DECL) continue;
        if (d->method_decl.modifiers & MOD_STATIC) continue;
        bool is_override = (d->method_decl.modifiers & MOD_OVERRIDE) != 0;
        int na = qv_ancestors(P, owner, anc, QV_MAX_ANCESTORS);
        for (int a = 0; a < na; a++) {
            bool iface = anc[a]->kind == AST_INTERFACE_DECL;
            if (!iface && !is_override) continue;
            for (int j = 0; j < anc[a]->type_decl.members.count; j++) {
                zan_ast_node_t *bm = anc[a]->type_decl.members.items[j];
                if (!bm || bm->kind != AST_METHOD_DECL || !qv_same_shape(bm, d)) continue;
                if (!iface && !(bm->method_decl.modifiers &
                                (MOD_VIRTUAL | MOD_ABSTRACT | MOD_OVERRIDE)))
                    continue;
                int bi = q_find(P, bm);
                if (bi >= 0) qv_add_overrider(&P->m[bi], i);
            }
        }
    }
}

/* 当前类型（含基类）里叫 name 的方法连 deferred 边（方法组当值用） */
static void qv_method_group(q_prog_t *P, int mi, zan_istr_t name, int filt) {
    zan_ast_node_t *owner = P->m[mi].owner;
    zan_ast_node_t *chain[QV_MAX_ANCESTORS + 1];
    chain[0] = owner;
    int n = 1 + qv_ancestors(P, owner, chain + 1, QV_MAX_ANCESTORS);
    for (int t = 0; t < n; t++) {
        for (int j = 0; j < chain[t]->type_decl.members.count; j++) {
            zan_ast_node_t *m = chain[t]->type_decl.members.items[j];
            if (!m || m->kind != AST_METHOD_DECL || m->method_decl.name.len != name.len ||
                memcmp(m->method_decl.name.str, name.str, name.len) != 0)
                continue;
            int to = q_find(P, m);
            if (to >= 0 && to != mi) q_add_site(&P->m[mi], to, filt, true);
        }
    }
}
