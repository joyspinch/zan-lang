/* checker_cycles.c: S11 类型图强环检查（docs/ZAN_STANDARD.md §3，W2111）
 *
 * ARC 回收不了互相强引用的对象。声明期就能看出的形状——A 的字段强持 B、
 * B 的字段（直接或经集合/数组）又强持 A——在这里零标注静态找出来，告警并
 * 给出把哪一侧改成 `weak` 的 fix-it（插入 "weak "），写法不用改。
 *
 * 图：节点是类声明；每个非静态、非 weak 的字段或自动属性贡献一条边：
 *   - 直接引用 `B` / `B?` → 单引用边（能加 weak 的候选）；
 *   - `B[]`、`List<B>`、`Dictionary<K, B>` 等泛型实参里的类 → 集合边。
 * 不进图：接口与类型参数（编译期不知道实际指向谁）、委托（捕获内容不可见，
 * 交给 S12 与运行期 S13）、自环（`Node.Next`、`Node.Children`——链表与树是
 * 正常结构，只有实际连成环才是泄漏，那由 S13 在退出时报）。
 * 字段的声明类型按精确类型连边，不展开子类——展开会把 GUI 控件树之类的
 * 宽基类图整个连成一团，告警失去意义。
 *
 * 只报用户代码里的环（标准库与 packages 的环由库作者负责）；设环境变量
 * ZAN_CYCLE_AUDIT=1 时连库代码一起报，用于审计。每个强连通分量报一条：
 * 它里面最短的环；改掉一处再编译会报下一处。
 *
 * UNITY-ORDER: before checker/checker_unit.c */

#include <stdlib.h>
#include <string.h>

#define CYC_MAX_EDGES_PER_FIELD 8

typedef struct {
    int             to;          /* 目标节点 */
    int             coll;        /* 1 = 经集合/数组 */
    zan_ast_node_t *field;       /* 贡献这条边的字段声明 */
} cyc_edge_t;

typedef struct {
    zan_ast_node_t *decl;
    zan_symbol_t   *sym;
    cyc_edge_t     *edges;
    int             edge_count, edge_cap;
    /* Tarjan */
    int             index, low, on_stack, comp;
} cyc_node_t;

typedef struct {
    zan_checker_t *c;
    cyc_node_t    *nodes;
    int            count, cap;
    int           *stack;
    int            sp;
    zan_ast_node_t **map_keys;   /* decl -> 节点下标（开放寻址） */
    int           *map_vals;
    size_t         map_mask;
    int            next_index;
    int            comp_count;
    bool           audit;
} cyc_graph_t;

static size_t cyc_hash(const void *p) {
    uintptr_t x = (uintptr_t)p >> 3;
    x ^= x >> 17;
    x *= (uintptr_t)0x9E3779B97F4A7C15ull;
    return (size_t)x;
}

static int cyc_find(cyc_graph_t *g, zan_ast_node_t *decl) {
    if (!g->map_keys) return -1;
    for (size_t i = cyc_hash(decl) & g->map_mask; g->map_keys[i]; i = (i + 1) & g->map_mask)
        if (g->map_keys[i] == decl) return g->map_vals[i];
    return -1;
}

static bool cyc_map_build(cyc_graph_t *g) {
    size_t cap = 64;
    while (cap < (size_t)g->count * 2) cap *= 2;
    g->map_keys = (zan_ast_node_t **)calloc(cap, sizeof(*g->map_keys));
    g->map_vals = (int *)calloc(cap, sizeof(int));
    if (!g->map_keys || !g->map_vals) return false;
    g->map_mask = cap - 1;
    for (int v = 0; v < g->count; v++) {
        size_t i = cyc_hash(g->nodes[v].decl) & g->map_mask;
        while (g->map_keys[i]) i = (i + 1) & g->map_mask;
        g->map_keys[i] = g->nodes[v].decl;
        g->map_vals[i] = v;
    }
    return true;
}

static bool cyc_decl_is_library(zan_ast_node_t *decl) {
    return decl && (decl->from_stdlib || decl->from_package);
}

static void cyc_add_edge(cyc_node_t *n, int to, int coll, zan_ast_node_t *field) {
    for (int i = 0; i < n->edge_count; i++)
        if (n->edges[i].to == to && n->edges[i].field == field) return;
    if (n->edge_count == n->edge_cap) {
        int ncap = n->edge_cap ? n->edge_cap * 2 : 8;
        cyc_edge_t *ne = (cyc_edge_t *)realloc(n->edges, (size_t)ncap * sizeof(*ne));
        if (!ne) return;
        n->edges = ne;
        n->edge_cap = ncap;
    }
    n->edges[n->edge_count].to = to;
    n->edges[n->edge_count].coll = coll;
    n->edges[n->edge_count].field = field;
    n->edge_count++;
}

/* 字段类型里能强持的类声明：直接的（coll=0）与经集合/数组/泛型实参的（coll=1） */
static void cyc_type_targets(cyc_graph_t *g, int from, zan_type_t *t, int coll,
                             zan_ast_node_t *field, int depth) {
    if (!t || depth > 4) return;
    switch (t->kind) {
    case TYPE_NULLABLE:
        cyc_type_targets(g, from, t->element_type, coll, field, depth + 1);
        return;
    case TYPE_ARRAY:
        cyc_type_targets(g, from, t->element_type, 1, field, depth + 1);
        return;
    case TYPE_CLASS:
        if (t->type_arg_count > 0) {
            for (int i = 0; i < t->type_arg_count; i++)
                cyc_type_targets(g, from, t->type_args[i], 1, field, depth + 1);
            return;
        }
        if (t->sym && t->sym->decl && t->sym->decl->kind == AST_CLASS_DECL) {
            int to = cyc_find(g, t->sym->decl);
            if (to >= 0 && to != from) cyc_add_edge(&g->nodes[from], to, coll, field);
        }
        return;
    default:
        return;
    }
}

static void cyc_strongconnect(cyc_graph_t *g, int v) {
    /* 递归深度受类型数限制；类型图深度远小于栈上限 */
    cyc_node_t *n = &g->nodes[v];
    n->index = n->low = g->next_index++;
    g->stack[g->sp++] = v;
    n->on_stack = 1;
    for (int i = 0; i < g->nodes[v].edge_count; i++) {
        int w = g->nodes[v].edges[i].to;
        if (g->nodes[w].index < 0) {
            cyc_strongconnect(g, w);
            if (g->nodes[w].low < g->nodes[v].low) g->nodes[v].low = g->nodes[w].low;
        } else if (g->nodes[w].on_stack && g->nodes[w].index < g->nodes[v].low) {
            g->nodes[v].low = g->nodes[w].index;
        }
    }
    if (g->nodes[v].low == g->nodes[v].index) {
        int comp = g->comp_count++;
        int w;
        do {
            w = g->stack[--g->sp];
            g->nodes[w].on_stack = 0;
            g->nodes[w].comp = comp;
        } while (w != v);
    }
}

static bool cyc_name_is_owner_like(zan_ast_node_t *field) {
    static const char *const owners[] = {
        "parent", "owner", "host", "container", "root", "form", "window",
        "manager", "back", "prev", "previous", "outer", "context", "site", NULL };
    zan_istr_t nm = field->field_decl.name;
    char buf[64];
    size_t n = nm.len < sizeof(buf) - 1 ? nm.len : sizeof(buf) - 1;
    for (size_t i = 0; i < n; i++) {
        char ch = nm.str[i];
        buf[i] = (char)((ch >= 'A' && ch <= 'Z') ? ch + 32 : ch);
    }
    buf[n] = '\0';
    for (int i = 0; owners[i]; i++)
        if (strstr(buf, owners[i])) return true;
    return false;
}

/* 在分量 comp 内从 start 出发 BFS 找回到 start 的最短环；返回环长（边数），
 * path_edges[k] 是第 k 条边（节点下标 + 边下标编码在 path_from/path_edge）。 */
static int cyc_shortest_cycle(cyc_graph_t *g, int start, int comp,
                              int *path_from, int *path_edge, int max_len) {
    int n = g->count;
    int *prev_node = (int *)malloc((size_t)n * sizeof(int));
    int *prev_edge = (int *)malloc((size_t)n * sizeof(int));
    int *queue = (int *)malloc((size_t)n * sizeof(int));
    int len = 0;
    if (!prev_node || !prev_edge || !queue) goto done;
    for (int i = 0; i < n; i++) prev_node[i] = -2;
    int qh = 0, qt = 0;
    queue[qt++] = start;
    prev_node[start] = -1;
    int close_from = -1, close_edge = -1;
    while (qh < qt && close_from < 0) {
        int u = queue[qh++];
        for (int e = 0; e < g->nodes[u].edge_count; e++) {
            int w = g->nodes[u].edges[e].to;
            if (g->nodes[w].comp != comp) continue;
            if (w == start) { close_from = u; close_edge = e; break; }
            if (prev_node[w] != -2) continue;
            prev_node[w] = u;
            prev_edge[w] = e;
            queue[qt++] = w;
        }
    }
    if (close_from < 0) goto done;
    /* 回溯：start -> ... -> close_from -> start */
    int rev_from[256], rev_edge[256], rn = 0;
    rev_from[rn] = close_from; rev_edge[rn] = close_edge; rn++;
    for (int v = close_from; v != start && rn < 256; v = prev_node[v]) {
        rev_from[rn] = prev_node[v];
        rev_edge[rn] = prev_edge[v];
        rn++;
    }
    if (rn > max_len) rn = max_len;
    for (int k = 0; k < rn; k++) {
        path_from[k] = rev_from[rn - 1 - k];
        path_edge[k] = rev_edge[rn - 1 - k];
    }
    len = rn;
done:
    free(prev_node); free(prev_edge); free(queue);
    return len;
}

static void cyc_report_component(cyc_graph_t *g, int comp) {
    /* 选报告起点：分量里第一个用户类（审计模式下任意类） */
    int best_len = 0;
    int path_from[64], path_edge[64];
    for (int v = 0; v < g->count; v++) {
        if (g->nodes[v].comp != comp) continue;
        if (!g->audit && cyc_decl_is_library(g->nodes[v].decl)) continue;
        int pf[64], pe[64];
        int len = cyc_shortest_cycle(g, v, comp, pf, pe, 64);
        if (len > 0 && (best_len == 0 || len < best_len)) {
            best_len = len;
            memcpy(path_from, pf, sizeof(int) * (size_t)len);
            memcpy(path_edge, pe, sizeof(int) * (size_t)len);
        }
    }
    if (best_len == 0) return;

    /* 选要改 weak 的边：单引用边里，优先名字像"所有者"的，其次目标经集合
     * 持有来源的回指针，最后取环上最后一条单引用边。只在用户代码里改。 */
    int pick = -1, pick_score = -1;
    for (int k = 0; k < best_len; k++) {
        cyc_edge_t *e = &g->nodes[path_from[k]].edges[path_edge[k]];
        if (e->coll) continue;
        if (!g->audit && cyc_decl_is_library(g->nodes[path_from[k]].decl)) continue;
        int score = 1;
        if (cyc_name_is_owner_like(e->field)) score += 4;
        int prevk = (k + best_len - 1) % best_len;
        cyc_edge_t *pe = &g->nodes[path_from[prevk]].edges[path_edge[prevk]];
        if (pe->coll) score += 2;
        if (score >= pick_score) { pick = k; pick_score = score; }
    }

    char path[1024];
    size_t pn = 0;
    path[0] = '\0';
    for (int k = 0; k < best_len && pn < sizeof(path) - 200; k++) {
        cyc_node_t *from = &g->nodes[path_from[k]];
        cyc_edge_t *e = &from->edges[path_edge[k]];
        pn += (size_t)snprintf(path + pn, sizeof(path) - pn, "%.*s.%.*s%s -> ",
            (int)from->decl->type_decl.name.len, from->decl->type_decl.name.str,
            (int)e->field->field_decl.name.len, e->field->field_decl.name.str,
            e->coll ? "[]" : "");
    }
    cyc_node_t *first = &g->nodes[path_from[0]];
    snprintf(path + pn, sizeof(path) - pn, "%.*s",
             (int)first->decl->type_decl.name.len, first->decl->type_decl.name.str);

    if (pick >= 0) {
        cyc_node_t *owner = &g->nodes[path_from[pick]];
        zan_ast_node_t *field = owner->edges[path_edge[pick]].field;
        zan_diag_fixit_t fix = { field->field_decl.type ? field->field_decl.type->loc
                                                        : field->loc, 0, "weak " };
        zan_diag_emit_fix(g->c->diag, DIAG_WARNING, field->loc, 2111, &fix,
            "strong reference cycle between classes: %s; ARC cannot free "
            "objects linked this way -- mark '%.*s.%.*s' 'weak' (the back "
            "reference) so the cycle does not keep both alive",
            path,
            (int)owner->decl->type_decl.name.len, owner->decl->type_decl.name.str,
            (int)field->field_decl.name.len, field->field_decl.name.str);
        return;
    }
    cyc_node_t *owner = &g->nodes[path_from[0]];
    zan_ast_node_t *field = owner->edges[path_edge[0]].field;
    zan_diag_emit_code(g->c->diag, DIAG_WARNING, field->loc, 2111,
        "strong reference cycle between classes: %s; every link is a "
        "collection, so no single field can be made 'weak' -- clear the "
        "collections (Dispose) before dropping the objects, or hold the "
        "back references through a weak field",
        path);
}

static void check_class_ref_cycles(zan_checker_t *c, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    cyc_graph_t g;
    memset(&g, 0, sizeof(g));
    g.c = c;
    const char *audit = getenv("ZAN_CYCLE_AUDIT");
    g.audit = audit && audit[0] && audit[0] != '0';

    bool any_user = false;
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (!d || d->kind != AST_CLASS_DECL) continue;
        if (d->type_decl.type_params.count > 0) continue; /* 泛型定义按实参连边 */
        if (!cyc_decl_is_library(d)) any_user = true;
        if (g.count == g.cap) {
            int ncap = g.cap ? g.cap * 2 : 256;
            cyc_node_t *nn = (cyc_node_t *)realloc(g.nodes, (size_t)ncap * sizeof(*nn));
            if (!nn) goto out;
            g.nodes = nn;
            g.cap = ncap;
        }
        memset(&g.nodes[g.count], 0, sizeof(g.nodes[0]));
        g.nodes[g.count].decl = d;
        g.nodes[g.count].sym = zan_binder_lookup(c->binder, d->type_decl.name);
        g.nodes[g.count].index = -1;
        g.count++;
    }
    if (!any_user && !g.audit) goto out;
    if (!cyc_map_build(&g)) goto out;

    for (int v = 0; v < g.count; v++) {
        zan_ast_node_t *d = g.nodes[v].decl;
        for (int j = 0; j < d->type_decl.members.count; j++) {
            zan_ast_node_t *m = d->type_decl.members.items[j];
            if (!m || (m->kind != AST_FIELD_DECL && m->kind != AST_PROPERTY_DECL)) continue;
            if (m->field_decl.modifiers & (MOD_STATIC | MOD_WEAK)) continue;
            if (m->kind == AST_PROPERTY_DECL &&
                (m->field_decl.getter_body || m->field_decl.setter_body)) continue;
            if (!m->field_decl.type) continue;
            zan_type_t *ft = zan_binder_resolve_type(c->binder, m->field_decl.type);
            cyc_type_targets(&g, v, ft, 0, m, 0);
        }
    }

    g.stack = (int *)malloc((size_t)(g.count ? g.count : 1) * sizeof(int));
    if (!g.stack) goto out;
    for (int v = 0; v < g.count; v++)
        if (g.nodes[v].index < 0) cyc_strongconnect(&g, v);

    int *comp_size = (int *)calloc((size_t)(g.comp_count ? g.comp_count : 1), sizeof(int));
    if (comp_size) {
        for (int v = 0; v < g.count; v++) comp_size[g.nodes[v].comp]++;
        for (int comp = 0; comp < g.comp_count; comp++)
            if (comp_size[comp] >= 2) cyc_report_component(&g, comp);
        free(comp_size);
    }

out:
    for (int v = 0; v < g.count; v++) free(g.nodes[v].edges);
    free(g.nodes);
    free(g.stack);
    free(g.map_keys);
    free(g.map_vals);
}
