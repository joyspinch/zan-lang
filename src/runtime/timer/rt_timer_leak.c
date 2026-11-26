/* rt_timer_leak.c: --check-leaks 退出报告的环路诊断（S13，docs/ZAN_STANDARD.md §3）
 *
 * 随 rt_timer.c 在 rt_timer_gc.c 之后 #include，复用其活对象注册表、对象图
 * 走边与指针表（均为文件内 static）。wasm/bare 不编入，IR 侧弱实现返回 0。
 *
 * 退出时对注册表里的残留对象做试探删除（trial deletion，同 zan_gc_detect_cycles）：
 * rc 大于"来自其他残留对象的强引用数"者是根（栈帧、静态字段、未登记对象持有），
 * 从根出发可达者都不算孤儿；剩下的孤儿只被孤儿引用——ARC 永远回收不了的环。
 * print 非 0 时在孤儿子图里找环，逐条打印具体路径：
 *     cycle: Node@a.zan:5:35 .Next -> List<Node>@a.zan:3:57 [0] -> Node@a.zan:5:35
 * 字段名来自编译器按站点生成的字段名表（与字段走图表 refs[] 同序）。 */

#define ZAN_LEAK_MAX_CYCLES 8
#define ZAN_LEAK_MAX_PATH   12

typedef struct {
    int      to;
    int      kind;  /* 0 字段，1 List/数组元素，2 Dict 键，3 Dict 值 */
    uint32_t index;
} zan_leak_edge_t;

typedef void (*zan_leak_edge_cb_t)(const zan_live_entry_t *from, void *to,
                                   int kind, uint32_t index, void *ctx);

/* zan_walk_object_edges 的带标签版本：额外给出边来自哪个字段/元素下标 */
static void zan_leak_walk_edges(const zan_live_entry_t *entry,
                                zan_leak_edge_cb_t cb, void *ctx) {
    const char *obj = (const char *)entry->obj;
    if (entry->kind != ZAN_LIVE_OBJECT) {
        if (entry->kind == ZAN_LIVE_REF_ARRAY) {
            int64_t count = *(const int64_t *)(obj + ZAN_ARR_COUNT_OFF);
            size_t bytes = entry->size - ZAN_ARR_HDR_SIZE - entry->data_offset;
            if (count > 0 && (uint64_t)count <= bytes / sizeof(void *)) {
                void *const *elems = (void *const *)(obj + entry->data_offset);
                for (int64_t i = 0; elems && i < count; i++) {
                    if (elems[i]) cb(entry, zan_edge_untag(elems[i]), 1, (uint32_t)i, ctx);
                }
            }
        }
        return;
    }

    uint64_t word = entry->site & ~ZAN_OBJ_STACK_FLAG;
    if (zan_word_is_string(word)) return;
    const zan_field_graph_t *fg = NULL;
    const zan_arc_desc_t *desc = zan_word_desc(word);
    if (desc) {
        fg = desc->field_graph;
    } else {
        uint64_t site = zan_word_site(word);
        if (g_site_field_graphs_reg && site > 0 && site < (uint64_t)g_site_reg_count)
            fg = (const zan_field_graph_t *)g_site_field_graphs_reg[site];
    }
    if (!fg) return;

    size_t hdr_bytes = zan_obj_hdr_bytes(word);
    if (entry->size < hdr_bytes) return;
    size_t bytes = entry->size - hdr_bytes;
    if (fg->kind == 0) {
        for (uint32_t k = 0; k < fg->ref_count; k++) {
            uint32_t off = fg->refs[k].offset;
            if (!fg->refs[k].is_rc || off > bytes || bytes - off < sizeof(void *))
                continue;
            void *child;
            memcpy(&child, obj + off, sizeof(child));
            if (child) cb(entry, zan_edge_untag(child), 0, k, ctx);
        }
    } else if (fg->kind == 1 || fg->kind == 2) {
        size_t required = 16 + sizeof(void *) * fg->kind;
        if (bytes < required) return;
        int64_t count = *(const int64_t *)obj;
        int64_t capacity = *(const int64_t *)(obj + 8);
        if (count <= 0 || count > capacity) return;
        if (fg->kind == 1) {
            void *const *data = *(void *const *const *)(obj + 16);
            for (int64_t j = 0; data && j < count; j++)
                if (data[j]) cb(entry, zan_edge_untag(data[j]), 1, (uint32_t)j, ctx);
        } else {
            void *const *ks = *(void *const *const *)(obj + 16);
            void *const *vs = *(void *const *const *)(obj + 24);
            int key_is_rc = fg->ref_count & 1;
            int val_is_rc = (fg->ref_count >> 1) & 1;
            for (int64_t j = 0; j < count; j++) {
                if (key_is_rc && ks && ks[j]) cb(entry, zan_edge_untag(ks[j]), 2, (uint32_t)j, ctx);
                if (val_is_rc && vs && vs[j]) cb(entry, zan_edge_untag(vs[j]), 3, (uint32_t)j, ctx);
            }
        }
    }
}

typedef struct {
    zan_obj_map_t   *map;
    zan_leak_edge_t *edges;
    size_t           count;
    size_t           cap;
    zan_cycle_node_t *nodes;
} zan_leak_collect_t;

static void zan_leak_internal_cb(const zan_live_entry_t *from, void *to,
                                 int kind, uint32_t index, void *ctx) {
    (void)from; (void)kind; (void)index;
    zan_leak_collect_t *c = (zan_leak_collect_t *)ctx;
    int j = zan_obj_map_get(c->map, to);
    if (j >= 0) c->nodes[j].internal_rc++;
}

static void zan_leak_mark_cb(const zan_live_entry_t *from, void *to,
                             int kind, uint32_t index, void *ctx) {
    (void)from; (void)kind; (void)index;
    zan_bfs_ctx_t *b = (zan_bfs_ctx_t *)ctx;
    int v = zan_obj_map_get(b->map, to);
    if (v >= 0 && !b->nodes[v].marked) {
        b->nodes[v].marked = 1;
        b->queue[(*b->qtail)++] = v;
    }
}

static void zan_leak_edge_cb(const zan_live_entry_t *from, void *to,
                             int kind, uint32_t index, void *ctx) {
    (void)from;
    zan_leak_collect_t *c = (zan_leak_collect_t *)ctx;
    int j = zan_obj_map_get(c->map, to);
    if (j < 0 || c->nodes[j].marked) return;
    if (c->count == c->cap) {
        if (c->cap > SIZE_MAX / (2 * sizeof(zan_leak_edge_t))) return;
        size_t ncap = c->cap ? c->cap * 2 : 64;
        zan_leak_edge_t *ne = (zan_leak_edge_t *)ZAN_GC_REALLOC(c->edges, ncap * sizeof(zan_leak_edge_t));
        if (!ne) return;
        c->edges = ne;
        c->cap = ncap;
    }
    c->edges[c->count].to = j;
    c->edges[c->count].kind = kind;
    c->edges[c->count].index = index;
    c->count++;
}

typedef struct {
    void      **site_names;
    void      **field_names;
    int64_t     site_count;
    FILE       *log;
} zan_leak_out_t;

static void zan_leak_emit(zan_leak_out_t *o, const char *fmt, const char *a, const char *b) {
    printf(fmt, a, b);
    if (o->log) fprintf(o->log, fmt, a, b);
}

static void zan_leak_emit_node(zan_leak_out_t *o, const zan_live_entry_t *entry) {
    uint64_t site = zan_word_site(entry->site & ~ZAN_OBJ_STACK_FLAG);
    const char *ty = zan_get_object_tyname(entry);
    const char *where = NULL;
    if (o->site_names && site > 0 && site < (uint64_t)o->site_count)
        where = (const char *)o->site_names[site];
    else if (entry->source)
        where = entry->source;
    /* 闭包记录与捕获盒没有类型名，按站点名的方括号标注显示 */
    if (where && strcmp(ty, "object") == 0) {
        if (strstr(where, "[closure]")) ty = "closure";
        else if (strstr(where, "[captured local]")) ty = "box";
    }
    char buf[512];
    if (where) {
        /* 站点名形如 "file:line:col [List<T>]"：类型已由 ty 给出，去掉方括号尾巴 */
        size_t n = strcspn(where, " ");
        if (n >= sizeof(buf)) n = sizeof(buf) - 1;
        memcpy(buf, where, n);
        buf[n] = '\0';
        zan_leak_emit(o, "%s@%s", ty, buf);
    } else {
        zan_leak_emit(o, "%s%s", ty, "");
    }
}

static void zan_leak_emit_edge(zan_leak_out_t *o, const zan_live_entry_t *entry,
                               const zan_leak_edge_t *e) {
    char buf[32];
    if (e->kind == 0) {
        uint64_t site = zan_word_site(entry->site & ~ZAN_OBJ_STACK_FLAG);
        const char *name = NULL;
        if (o->field_names && site > 0 && site < (uint64_t)o->site_count) {
            const char **names = (const char **)o->field_names[site];
            if (names) name = names[e->index];
        }
        if (name) {
            zan_leak_emit(o, " .%s%s -> ", name, "");
            return;
        }
        snprintf(buf, sizeof(buf), "#%u", e->index);
        zan_leak_emit(o, " .%s%s -> ", buf, "");
        return;
    }
    snprintf(buf, sizeof(buf), "%u", e->index);
    zan_leak_emit(o, " [%s]%s -> ", buf,
        e->kind == 2 ? " (key)" : (e->kind == 3 ? " (value)" : ""));
}

/* 在孤儿子图（CSR：first[i]..first[i+1]）上做迭代 DFS，遇到指回栈内节点的
 * 边即得一条环，打印后把环上节点记为已报告，避免同一个环反复出现。 */
static int zan_leak_print_cycles(zan_leak_out_t *o, zan_cycle_node_t *nodes, size_t n,
                                 const size_t *first, const zan_leak_edge_t *edges) {
    int *state = (int *)ZAN_GC_CALLOC(n, sizeof(int));        /* 0 未访 1 在栈 2 完成 */
    int *stack = (int *)ZAN_GC_MALLOC(n * sizeof(int));
    size_t *cursor = (size_t *)ZAN_GC_MALLOC(n * sizeof(size_t));
    int *via = (int *)ZAN_GC_MALLOC(n * sizeof(int));          /* 进入该节点所走的边下标 */
    char *reported = (char *)ZAN_GC_CALLOC(n, 1);
    int printed = 0;
    if (!state || !stack || !cursor || !via || !reported) goto done;
    for (size_t root = 0; root < n && printed < ZAN_LEAK_MAX_CYCLES; root++) {
        if (nodes[root].marked || state[root]) continue;
        size_t sp = 0;
        stack[sp++] = (int)root;
        state[root] = 1;
        cursor[root] = first[root];
        via[root] = -1;
        while (sp > 0 && printed < ZAN_LEAK_MAX_CYCLES) {
            int u = stack[sp - 1];
            if (cursor[u] >= first[u + 1]) {
                state[u] = 2;
                sp--;
                continue;
            }
            size_t ei = cursor[u]++;
            int v = edges[ei].to;
            if (state[v] == 0) {
                state[v] = 1;
                cursor[v] = first[v];
                via[v] = (int)ei;
                stack[sp++] = v;
            } else if (state[v] == 1 && !reported[v]) {
                size_t start = sp;
                while (start > 0 && stack[start - 1] != v) start--;
                start--;
                size_t len = sp - start;
                zan_leak_emit(o, "  cycle: %s%s", "", "");
                for (size_t k = start; k < sp; k++) {
                    int node = stack[k];
                    if (k - start == ZAN_LEAK_MAX_PATH) {
                        zan_leak_emit(o, "... %s%s-> ", "", "");
                        break;
                    }
                    zan_leak_emit_node(o, nodes[node].entry);
                    const zan_leak_edge_t *e = (k + 1 < sp) ? &edges[via[stack[k + 1]]] : &edges[ei];
                    zan_leak_emit_edge(o, nodes[node].entry, e);
                    reported[node] = 1;
                }
                zan_leak_emit_node(o, nodes[v].entry);
                zan_leak_emit(o, "%s%s\n", "", len == 1 ? " (self)" : "");
                printed++;
            }
        }
    }
done:
    ZAN_GC_FREE(state); ZAN_GC_FREE(stack); ZAN_GC_FREE(cursor);
    ZAN_GC_FREE(via); ZAN_GC_FREE(reported);
    return printed;
}

int64_t zan_rt_leak_cycles(void **site_names, void **field_names, int64_t site_count,
                           void *log_fh, int32_t print) {
    if (!__atomic_load_n(&g_live_stripes_inited, __ATOMIC_ACQUIRE)) return 0;
    zan_live_lock_all();

    size_t total = 0;
    for (int s = 0; s < ZAN_LIVE_STRIPES; s++) total += g_live_stripes[s].count;
    zan_cycle_node_t *nodes = NULL;
    int *queue = NULL;
    int64_t orphans = 0;
    zan_obj_map_t map = {0};
    if (total == 0 || total > INT32_MAX || total > SIZE_MAX / sizeof(*nodes)) goto unlock;

    nodes = (zan_cycle_node_t *)ZAN_GC_MALLOC(total * sizeof(*nodes));
    queue = (int *)ZAN_GC_MALLOC(total * sizeof(*queue));
    zan_obj_map_init(&map, total);
    if (!nodes || !queue || !map.entries) goto unlock;

    size_t n = 0;
    for (int s = 0; s < ZAN_LIVE_STRIPES; s++) {
        for (size_t b = 0; b < g_live_stripes[s].bucket_count; b++) {
            for (zan_live_entry_t *e = g_live_stripes[s].buckets[b]; e && n < total; e = e->next) {
                int64_t rc = zan_live_read_rc(e);
                if (rc != 0) {
                    nodes[n] = (zan_cycle_node_t){.entry = e, .rc = rc};
                    zan_obj_map_put(&map, e->obj, (int)n);
                    n++;
                }
            }
        }
    }

    zan_leak_collect_t col = { &map, NULL, 0, 0, nodes };
    for (size_t i = 0; i < n; i++) zan_leak_walk_edges(nodes[i].entry, zan_leak_internal_cb, &col);

    size_t qhead = 0, qtail = 0;
    for (size_t i = 0; i < n; i++) {
        if (nodes[i].rc < 0 || nodes[i].rc > nodes[i].internal_rc) {
            nodes[i].marked = 1;
            queue[qtail++] = (int)i;
        }
    }
    zan_bfs_ctx_t bfs = { nodes, &map, queue, &qtail };
    while (qhead < qtail) {
        int u = queue[qhead++];
        zan_leak_walk_edges(nodes[u].entry, zan_leak_mark_cb, &bfs);
    }
    for (size_t i = 0; i < n; i++) if (!nodes[i].marked) orphans++;

    if (print && orphans > 0) {
        size_t *first = (size_t *)ZAN_GC_CALLOC(n + 1, sizeof(size_t));
        if (first) {
            for (size_t i = 0; i < n; i++) {
                first[i] = col.count;
                if (!nodes[i].marked) zan_leak_walk_edges(nodes[i].entry, zan_leak_edge_cb, &col);
            }
            first[n] = col.count;
            zan_leak_out_t out = { site_names, field_names, site_count, (FILE *)log_fh };
            char cnt[32];
            snprintf(cnt, sizeof(cnt), "%lld", (long long)orphans);
            zan_leak_emit(&out, "  %s object(s) form unreachable reference cycles%s:\n", cnt, "");
            int cycles = zan_leak_print_cycles(&out, nodes, n, first, col.edges);
            if (cycles > 0)
                zan_leak_emit(&out, "  break each cycle: mark one field on it 'weak', unsubscribe the handler, or Dispose%s%s\n", "", "");
            ZAN_GC_FREE(first);
        }
    }
    ZAN_GC_FREE(col.edges);

unlock:
    zan_obj_map_free(&map);
    ZAN_GC_FREE(queue);
    ZAN_GC_FREE(nodes);
    zan_live_unlock_all();
    if (log_fh) fflush((FILE *)log_fh);
    fflush(stdout);
    return orphans;
}
