/* rt_timer_gc.c: 活对象注册表、孤儿环探测/回收器与堆快照（B-ID89）
 *
 * 随 rt_timer.o 进入所有 hosted 链接路径（rt_timer.c 末尾 #include）。曾在
 * rt_mem.c 里——那个对象只在 --fast-alloc 且非 Apple 的链接里存在，macOS
 * 上 GC.DetectCycles/WriteHeapSnapshot 静默恒为 0。wasm/bare 不编入（无线程
 * 锁原语），IR 侧的弱空实现兜底。
 *
 * 分配走普通 malloc/free：--fast-alloc 的 --wrap 会把它们导向小对象分配器，
 * 分配器本身不回调注册表，不会重入。 */

/* =========================================================================
 * B-ID89: 运行时活对象注册表、孤儿环探测器与堆快照导出
 * ========================================================================= */

#define ZAN_LIVE_STRIPES 32

typedef struct zan_live_entry {
    void                  *obj;
    size_t                 size;
    struct zan_live_entry *next;
} zan_live_entry_t;

typedef struct {
#if defined(_WIN32)
    SRWLOCK            lock;
#else
    pthread_mutex_t    lock;
#endif
    zan_live_entry_t **buckets;
    size_t             bucket_count;
    size_t             count;
    zan_live_entry_t  *free_list;
} zan_live_stripe_t;

static zan_live_stripe_t g_live_stripes[ZAN_LIVE_STRIPES];
static int g_live_stripes_inited = 0;

static inline void zan_live_lock_init(zan_live_stripe_t *s) {
#if defined(_WIN32)
    (void)s;
#else
    pthread_mutex_init(&s->lock, NULL);
#endif
}

static inline void zan_live_lock(zan_live_stripe_t *s) {
#if defined(_WIN32)
    AcquireSRWLockExclusive(&s->lock);
#else
    pthread_mutex_lock(&s->lock);
#endif
}

static inline void zan_live_unlock(zan_live_stripe_t *s) {
#if defined(_WIN32)
    ReleaseSRWLockExclusive(&s->lock);
#else
    pthread_mutex_unlock(&s->lock);
#endif
}

static inline unsigned zan_live_hash(uintptr_t ptr) {
    uintptr_t x = ptr >> 4;
    x ^= (x >> 16);
    x ^= (x >> 8);
    return (unsigned)x;
}

static void zan_live_atexit_detector(void) {
    zan_gc_detect_cycles();
}

static void zan_live_ensure_init(void) {
    if (__atomic_load_n(&g_live_stripes_inited, __ATOMIC_ACQUIRE)) return;
    static volatile long init_lock = 0;
    while (__sync_lock_test_and_set(&init_lock, 1)) {
        /* spin */
    }
    if (!g_live_stripes_inited) {
        for (int i = 0; i < ZAN_LIVE_STRIPES; i++) {
            zan_live_lock_init(&g_live_stripes[i]);
            g_live_stripes[i].bucket_count = 64;
            g_live_stripes[i].buckets = (zan_live_entry_t **)calloc(64, sizeof(zan_live_entry_t *));
            g_live_stripes[i].count = 0;
            g_live_stripes[i].free_list = NULL;
        }
        if (getenv("ZAN_DETECT_LEAKS")) {
            atexit(zan_live_atexit_detector);
        }
        __atomic_store_n(&g_live_stripes_inited, 1, __ATOMIC_RELEASE);
    }
    __sync_lock_release(&init_lock);
}

/* 活对象注册表只服务 GC.DetectCycles / GC.WriteHeapSnapshot 与 ZAN_DETECT_LEAKS
 * 退出检测。它曾对每次分配无条件加条带锁登记——分配密集代码里占 20%~30%
 * 的 CPU（binary-trees / 字符串拼接实测）。现在按需开启：编译器在程序引用
 * 上述 GC API 时生成优先级 101 的构造器调用 zan_rt_live_enable()（早于静态
 * 初始化），环境变量 ZAN_DETECT_LEAKS 在首次分配时开启；其余程序直接跳过。 */
static int g_live_enabled = -1; /* -1 未判定，0 关，1 开 */

void zan_rt_live_enable(void) {
    __atomic_store_n(&g_live_enabled, 1, __ATOMIC_RELEASE);
}

static inline int zan_live_wanted(void) {
    int v = __atomic_load_n(&g_live_enabled, __ATOMIC_ACQUIRE);
    if (__builtin_expect(v >= 0, 1)) return v;
    int on = getenv("ZAN_DETECT_LEAKS") != NULL;
    int expected = -1;
    __atomic_compare_exchange_n(&g_live_enabled, &expected, on, 0,
                                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    return __atomic_load_n(&g_live_enabled, __ATOMIC_ACQUIRE);
}

void zan_rt_live_register(void *obj, size_t size) {
    if (!obj || !zan_live_wanted()) return;
    zan_live_ensure_init();
    unsigned h = zan_live_hash((uintptr_t)obj);
    unsigned stripe_idx = h & (ZAN_LIVE_STRIPES - 1);
    zan_live_stripe_t *s = &g_live_stripes[stripe_idx];

    zan_live_lock(s);
    if (!s->buckets) {
        s->bucket_count = 64;
        s->buckets = (zan_live_entry_t **)calloc(64, sizeof(zan_live_entry_t *));
    }

    /* Grow buckets if load factor > 2 */
    if (s->count > s->bucket_count * 2 && s->bucket_count < (1u << 24)) {
        size_t new_cap = s->bucket_count * 2;
        zan_live_entry_t **new_buckets = (zan_live_entry_t **)calloc(new_cap, sizeof(zan_live_entry_t *));
        if (new_buckets) {
            for (size_t b = 0; b < s->bucket_count; b++) {
                zan_live_entry_t *curr = s->buckets[b];
                while (curr) {
                    zan_live_entry_t *nxt = curr->next;
                    unsigned b_idx = (zan_live_hash((uintptr_t)curr->obj) / ZAN_LIVE_STRIPES) & (new_cap - 1);
                    curr->next = new_buckets[b_idx];
                    new_buckets[b_idx] = curr;
                    curr = nxt;
                }
            }
            free(s->buckets);
            s->buckets = new_buckets;
            s->bucket_count = new_cap;
        }
    }

    unsigned b_idx = (h / ZAN_LIVE_STRIPES) & (s->bucket_count - 1);
    zan_live_entry_t *entry = s->free_list;
    if (entry) {
        s->free_list = entry->next;
    } else {
        entry = (zan_live_entry_t *)malloc(sizeof(zan_live_entry_t));
        if (!entry) {
            zan_live_unlock(s);
            return;
        }
    }
    entry->obj = obj;
    entry->size = size;
    entry->next = s->buckets[b_idx];
    s->buckets[b_idx] = entry;
    s->count++;
    zan_live_unlock(s);
}

void zan_rt_live_unregister(void *obj) {
    if (!obj) return;
    if (!__atomic_load_n(&g_live_stripes_inited, __ATOMIC_ACQUIRE)) return;
    unsigned h = zan_live_hash((uintptr_t)obj);
    unsigned stripe_idx = h & (ZAN_LIVE_STRIPES - 1);
    zan_live_stripe_t *s = &g_live_stripes[stripe_idx];

    zan_live_lock(s);
    if (!s->buckets || s->count == 0) {
        zan_live_unlock(s);
        return;
    }

    unsigned b_idx = (h / ZAN_LIVE_STRIPES) & (s->bucket_count - 1);
    zan_live_entry_t **prev = &s->buckets[b_idx];
    zan_live_entry_t *curr = *prev;
    while (curr) {
        if (curr->obj == obj) {
            *prev = curr->next;
            curr->next = s->free_list;
            s->free_list = curr;
            s->count--;
            break;
        }
        prev = &curr->next;
        curr = curr->next;
    }
    zan_live_unlock(s);
}

static void **g_site_field_graphs_reg = NULL;
static void **g_site_tynames_reg = NULL;
static int64_t g_site_reg_count = 0;

void zan_rt_register_site_field_graphs(void **graphs, void **tynames, int64_t count) {
    g_site_field_graphs_reg = graphs;
    g_site_tynames_reg = tynames;
    g_site_reg_count = count;
}

/* --check-leaks 站点表模式：site 是小整数下标，释放函数在站点析构表里 */
static void **g_site_dtors_reg = NULL;
static int64_t g_site_dtors_count = 0;

void zan_rt_register_site_dtors(void **dtors, int64_t count) {
    g_site_dtors_reg = dtors;
    g_site_dtors_count = count;
}

static const char *zan_get_object_tyname(void *obj) {
    if (!obj) return "null";
    uint64_t site = *(uint64_t *)((char *)obj + ZAN_OBJ_SITE_OFF) & ~ZAN_OBJ_STACK_FLAG;
    if ((site >> 32) == ZAN_STRING_TAG) {
        return "string";
    }
    uint64_t arr_rc_magic = *(uint64_t *)((char *)obj + ZAN_ARR_RC_MAGIC_OFF);
    if (arr_rc_magic == ZAN_ARRAY_RC_MAGIC) {
        if (site == ZAN_ARRAY_MAGIC) return "Array";
        if (site >= 4096) {
            zan_arc_desc_t *desc = (zan_arc_desc_t *)(uintptr_t)site;
            if (desc && desc->tynames_arr) {
                const char **tns = (const char **)desc->tynames_arr;
                if (tns[0]) return tns[0];
            }
        }
        return "Array";
    }
    if (site >= 4096) {
        zan_arc_desc_t *desc = (zan_arc_desc_t *)(uintptr_t)site;
        if (desc && desc->tynames_arr) {
            const char **tns = (const char **)desc->tynames_arr;
            if (tns[0]) return tns[0];
        }
    } else if (g_site_tynames_reg && site > 0 && site < (uint64_t)g_site_reg_count) {
        const char **tns = (const char **)g_site_tynames_reg[site];
        if (tns && tns[0]) return tns[0];
    }
    return "object";
}

typedef void (*zan_edge_cb_t)(void *from, void *to, void *ctx);

static void zan_walk_object_edges(void *obj, zan_edge_cb_t cb, void *ctx) {
    if (!obj) return;
    uint64_t site = *(uint64_t *)((char *)obj + ZAN_OBJ_SITE_OFF) & ~ZAN_OBJ_STACK_FLAG;
    if ((site >> 32) == ZAN_STRING_TAG) {
        return;
    }
    uint64_t arr_rc_magic = *(uint64_t *)((char *)obj + ZAN_ARR_RC_MAGIC_OFF);
    if (arr_rc_magic == ZAN_ARRAY_RC_MAGIC) {
        if (site == ZAN_ARRAY_MAGIC) return;
        int64_t count = *(int64_t *)((char *)obj + ZAN_OBJ_RC_OFF);
        if (count <= 0) return;
        void **elems = (void **)obj;
        for (int64_t i = 0; i < count; i++) {
            void *child = elems[i];
            if (child) cb(obj, child, ctx);
        }
        return;
    }
    zan_field_graph_t *fg = NULL;
    if (site >= 4096) {
        zan_arc_desc_t *desc = (zan_arc_desc_t *)(uintptr_t)site;
        if (desc) fg = desc->field_graph;
    } else if (g_site_field_graphs_reg && site > 0 && site < (uint64_t)g_site_reg_count) {
        fg = (zan_field_graph_t *)g_site_field_graphs_reg[site];
    }
    if (!fg) return;
    if (fg->kind == 0) {
        for (uint32_t k = 0; k < fg->ref_count; k++) {
            uint32_t off = fg->refs[k].offset;
            void *child = *(void **)((char *)obj + off);
            if (child) cb(obj, child, ctx);
        }
    } else if (fg->kind == 1) { /* List<T> */
            int64_t count = *(int64_t *)obj;
            void **data = *(void ***)((char *)obj + 16);
            if (data && count > 0) {
                for (int64_t j = 0; j < count; j++) {
                    void *child = data[j];
                    if (child) cb(obj, child, ctx);
                }
            }
        } else if (fg->kind == 2) { /* Dict<K,V> */
            int64_t count = *(int64_t *)obj;
            void **ks = *(void ***)((char *)obj + 16);
            void **vs = *(void ***)((char *)obj + 24);
            int key_is_rc = fg->ref_count & 1;
            int val_is_rc = (fg->ref_count >> 1) & 1;
            for (int64_t j = 0; j < count; j++) {
                if (key_is_rc && ks && ks[j]) cb(obj, ks[j], ctx);
                if (val_is_rc && vs && vs[j]) cb(obj, vs[j], ctx);
            }
        }
    }

typedef struct {
    void *key;
    int   val;
} zan_map_entry_t;

typedef struct {
    zan_map_entry_t *entries;
    size_t           capacity;
    size_t           mask;
} zan_obj_map_t;

static inline void zan_obj_map_init(zan_obj_map_t *map, size_t min_cap) {
    size_t cap = 64;
    while (cap < min_cap * 2) cap *= 2;
    map->capacity = cap;
    map->mask = cap - 1;
    map->entries = (zan_map_entry_t *)calloc(cap, sizeof(zan_map_entry_t));
}

static inline void zan_obj_map_free(zan_obj_map_t *map) {
    if (map->entries) free(map->entries);
    map->entries = NULL;
}

static inline void zan_obj_map_put(zan_obj_map_t *map, void *key, int val) {
    if (!key || !map->entries) return;
    size_t idx = zan_live_hash((uintptr_t)key) & map->mask;
    while (map->entries[idx].key && map->entries[idx].key != key) {
        idx = (idx + 1) & map->mask;
    }
    map->entries[idx].key = key;
    map->entries[idx].val = val;
}

static inline int zan_obj_map_get(const zan_obj_map_t *map, void *key) {
    if (!key || !map->entries) return -1;
    size_t idx = zan_live_hash((uintptr_t)key) & map->mask;
    while (map->entries[idx].key) {
        if (map->entries[idx].key == key) return map->entries[idx].val;
        idx = (idx + 1) & map->mask;
    }
    return -1;
}

typedef struct {
    void       *obj;
    int64_t     rc;
    int64_t     internal_rc;
    int         marked;
    size_t      size;
} zan_cycle_node_t;

typedef struct {
    zan_cycle_node_t *nodes;
    zan_obj_map_t    *map;
} zan_cycle_ctx_t;

static void zan_cycle_edge_count_cb(void *from, void *to, void *ctx) {
    (void)from;
    zan_cycle_ctx_t *c = (zan_cycle_ctx_t *)ctx;
    int j = zan_obj_map_get(c->map, to);
    if (j >= 0) {
        c->nodes[j].internal_rc++;
    }
}

typedef struct {
    zan_cycle_node_t *nodes;
    zan_obj_map_t    *map;
    int              *queue;
    size_t           *qtail;
} zan_bfs_ctx_t;

static void zan_cycle_bfs_cb(void *from, void *to, void *ctx) {
    (void)from;
    zan_bfs_ctx_t *b = (zan_bfs_ctx_t *)ctx;
    int v = zan_obj_map_get(b->map, to);
    if (v >= 0 && !b->nodes[v].marked) {
        b->nodes[v].marked = 1;
        b->queue[(*b->qtail)++] = v;
    }
}

/* 数组的引用计数在 -32（-16 是长度）；其余对象在 -16 */
static int64_t zan_gc_read_rc(void *obj) {
    if (*(uint64_t *)((char *)obj + ZAN_ARR_RC_MAGIC_OFF) == ZAN_ARRAY_RC_MAGIC)
        return *(int64_t *)((char *)obj + ZAN_ARR_RC_OFF);
    return *(int64_t *)((char *)obj + ZAN_OBJ_RC_OFF);
}

int64_t zan_gc_detect_cycles(void) {
    if (!__atomic_load_n(&g_live_stripes_inited, __ATOMIC_ACQUIRE)) return 0;

    for (int s = 0; s < ZAN_LIVE_STRIPES; s++) {
        zan_live_lock(&g_live_stripes[s]);
    }

    size_t total_count = 0;
    for (int s = 0; s < ZAN_LIVE_STRIPES; s++) {
        total_count += g_live_stripes[s].count;
    }
    if (total_count == 0) {
        for (int s = ZAN_LIVE_STRIPES - 1; s >= 0; s--) {
            zan_live_unlock(&g_live_stripes[s]);
        }
        return 0;
    }

    zan_cycle_node_t *nodes = (zan_cycle_node_t *)malloc(total_count * sizeof(zan_cycle_node_t));
    if (!nodes) {
        for (int s = ZAN_LIVE_STRIPES - 1; s >= 0; s--) {
            zan_live_unlock(&g_live_stripes[s]);
        }
        return 0;
    }

    zan_obj_map_t map;
    zan_obj_map_init(&map, total_count);

    size_t idx = 0;
    for (int s = 0; s < ZAN_LIVE_STRIPES; s++) {
        for (size_t b = 0; b < g_live_stripes[s].bucket_count; b++) {
            zan_live_entry_t *entry = g_live_stripes[s].buckets[b];
            while (entry && idx < total_count) {
                nodes[idx].obj = entry->obj;
                nodes[idx].rc = zan_gc_read_rc(entry->obj);
                nodes[idx].internal_rc = 0;
                nodes[idx].marked = 0;
                nodes[idx].size = entry->size;
                zan_obj_map_put(&map, entry->obj, (int)idx);
                idx++;
                entry = entry->next;
            }
        }
    }
    size_t actual_count = idx;

    zan_cycle_ctx_t ctx = { nodes, &map };
    for (size_t i = 0; i < actual_count; i++) {
        zan_walk_object_edges(nodes[i].obj, zan_cycle_edge_count_cb, &ctx);
    }

    int *queue = (int *)malloc(actual_count * sizeof(int));
    size_t qhead = 0, qtail = 0;
    if (queue) {
        for (size_t i = 0; i < actual_count; i++) {
            if (nodes[i].rc > nodes[i].internal_rc) {
                nodes[i].marked = 1;
                queue[qtail++] = (int)i;
            }
        }

        zan_bfs_ctx_t bfs_ctx = { nodes, &map, queue, &qtail };
        while (qhead < qtail) {
            int u = queue[qhead++];
            zan_walk_object_edges(nodes[u].obj, zan_cycle_bfs_cb, &bfs_ctx);
        }
    }

    int64_t orphan_count = 0;
    size_t orphan_bytes = 0;
    for (size_t i = 0; i < actual_count; i++) {
        if (!nodes[i].marked) {
            orphan_count++;
            orphan_bytes += nodes[i].size;
        }
    }

    if (orphan_count > 0) {
        fprintf(stderr,
            "[zan-gc] cycle detection: %lld unreachable object(s) in orphan cycles (%zu bytes):\n",
            (long long)orphan_count, orphan_bytes);
        int printed = 0;
        for (size_t i = 0; i < actual_count && printed < 20; i++) {
            if (!nodes[i].marked) {
                const char *tyname = zan_get_object_tyname(nodes[i].obj);
                uint64_t site = *(uint64_t *)((char *)nodes[i].obj + ZAN_OBJ_SITE_OFF);
                fprintf(stderr, "  - [%s] at %p (rc=%lld, internal_rc=%lld, size=%zu, site=%llu)\n",
                        tyname, nodes[i].obj, (long long)nodes[i].rc, (long long)nodes[i].internal_rc,
                        nodes[i].size, (unsigned long long)site);
                printed++;
            }
        }
        if (orphan_count > 20) {
            fprintf(stderr, "  ... and %lld more orphan object(s)\n", (long long)(orphan_count - 20));
        }
    }

    if (queue) free(queue);
    zan_obj_map_free(&map);
    free(nodes);

    for (int s = ZAN_LIVE_STRIPES - 1; s >= 0; s--) {
        zan_live_unlock(&g_live_stripes[s]);
    }

    return orphan_count;
}

void zan_rt_dump_leaks(void) {
    zan_gc_detect_cycles();
}

/* =========================================================================
 * 孤儿环回收（GC.CollectCycles）
 *
 * ARC 回收不了互相强引用的对象环。这里在探测器的基础上真正释放它们，
 * 思路同 CPython 的环回收（trial deletion）：
 *   1. 注册表里每个对象记 rc 与"来自其他已登记对象的强引用数" internal；
 *      rc > internal 说明有栈/全局/未登记对象持有它——是根。
 *   2. 从根出发沿强引用标记可达；未标记者即孤儿：只被孤儿引用。
 *   3. 把孤儿之间的强引用槽清空（不调释放——孤儿集合是闭的），此时每个
 *      孤儿的引用全部来自刚清空的槽，rc 置 1 后调它自己的类型化释放函数：
 *      计数归零、析构剩余字段（字符串、仍存活对象……）、弱引用置空、free。
 *
 * 保守原则——宁可漏收，绝不误收：
 *   - 只回收能找到类型化释放函数的类对象/集合（描述符 dtor_fn，或
 *     --check-leaks 站点表模式下的站点析构表）；字符串、数组、闭包记录、
 *     栈上提升对象一律视为根。
 *   - 只统计类字段（field_graph kind 0）与 List 元素（kind 1）两类强引用槽；
 *     Dict/数组/结构体内嵌字段里的引用不计入 internal，被它们引用的对象因
 *     rc > internal 自然成为根。
 * 前提：调用期间没有其他线程在改对象图（与 .NET 的 GC.Collect 语义不同，
 * 这是显式、同步、由调用方选择安全点的回收）。环内对象的析构看到的是已被
 * 清空的环内字段。 */

typedef void (*zan_slot_cb_t)(void *from, void **slot, void *ctx);

/* 可回收对象返回其字段图并经 *dtor 给出类型化释放函数；否则 NULL（视为根） */
static zan_field_graph_t *zan_gc_collectable_graph(void *obj, void **dtor) {
    uint64_t raw = *(uint64_t *)((char *)obj + ZAN_OBJ_SITE_OFF);
    if (raw & ZAN_OBJ_STACK_FLAG) return NULL;
    if ((raw >> 32) == ZAN_STRING_TAG) return NULL;
    if (*(uint64_t *)((char *)obj + ZAN_ARR_RC_MAGIC_OFF) == ZAN_ARRAY_RC_MAGIC) return NULL;
    if (raw < 4096) {
        /* 站点表模式；0 号是未知站点。闭包记录等非类站点在析构表里为空，自然排除 */
        if (raw == 0 || !g_site_dtors_reg || !g_site_field_graphs_reg ||
            (int64_t)raw >= g_site_dtors_count || (int64_t)raw >= g_site_reg_count)
            return NULL;
        *dtor = g_site_dtors_reg[raw];
        zan_field_graph_t *fg = (zan_field_graph_t *)g_site_field_graphs_reg[raw];
        return (*dtor && fg) ? fg : NULL;
    }
    zan_arc_desc_t *desc = (zan_arc_desc_t *)(uintptr_t)raw;
    if (!desc->dtor_fn || !desc->field_graph) return NULL;
    *dtor = desc->dtor_fn;
    return desc->field_graph;
}

static void zan_gc_walk_slots(void *obj, zan_field_graph_t *fg, zan_slot_cb_t cb, void *ctx) {
    if (fg->kind == 0) {
        for (uint32_t k = 0; k < fg->ref_count; k++) {
            void **slot = (void **)((char *)obj + fg->refs[k].offset);
            if (*slot) cb(obj, slot, ctx);
        }
    } else if (fg->kind == 1) {
        int64_t count = *(int64_t *)obj;
        void **data = *(void ***)((char *)obj + 16);
        for (int64_t j = 0; data && j < count; j++)
            if (data[j]) cb(obj, &data[j], ctx);
    }
}

typedef struct {
    void             *obj;
    zan_field_graph_t *fg;
    void             *dtor;
    int64_t           rc;
    int64_t           internal;
    int               marked;
} zan_gc_node_t;

typedef struct {
    zan_gc_node_t *nodes;
    zan_obj_map_t *map;
    int           *queue;
    size_t         qtail;
} zan_gc_ctx_t;

static void zan_gc_count_cb(void *from, void **slot, void *ctx) {
    (void)from;
    zan_gc_ctx_t *c = (zan_gc_ctx_t *)ctx;
    int j = zan_obj_map_get(c->map, *slot);
    if (j >= 0) c->nodes[j].internal++;
}

static void zan_gc_mark_cb(void *from, void **slot, void *ctx) {
    (void)from;
    zan_gc_ctx_t *c = (zan_gc_ctx_t *)ctx;
    int j = zan_obj_map_get(c->map, *slot);
    if (j >= 0 && !c->nodes[j].marked) {
        c->nodes[j].marked = 1;
        c->queue[c->qtail++] = j;
    }
}

static void zan_gc_clear_cb(void *from, void **slot, void *ctx) {
    (void)from;
    zan_gc_ctx_t *c = (zan_gc_ctx_t *)ctx;
    int j = zan_obj_map_get(c->map, *slot);
    if (j >= 0 && !c->nodes[j].marked) *slot = NULL;
}

int64_t zan_gc_collect_cycles(void) {
    if (!__atomic_load_n(&g_live_stripes_inited, __ATOMIC_ACQUIRE)) return 0;
    for (int s = 0; s < ZAN_LIVE_STRIPES; s++) zan_live_lock(&g_live_stripes[s]);
    size_t total = 0;
    for (int s = 0; s < ZAN_LIVE_STRIPES; s++) total += g_live_stripes[s].count;
    zan_gc_node_t *nodes = total ? (zan_gc_node_t *)malloc(total * sizeof(zan_gc_node_t)) : NULL;
    int *queue = total ? (int *)malloc(total * sizeof(int)) : NULL;
    zan_obj_map_t map = { 0 };
    size_t n = 0;
    if (nodes && queue) {
        zan_obj_map_init(&map, total);
        for (int s = 0; s < ZAN_LIVE_STRIPES; s++) {
            for (size_t b = 0; b < g_live_stripes[s].bucket_count; b++) {
                for (zan_live_entry_t *e = g_live_stripes[s].buckets[b]; e && n < total; e = e->next) {
                    zan_gc_node_t *nd = &nodes[n];
                    nd->obj = e->obj;
                    nd->dtor = NULL;
                    nd->fg = zan_gc_collectable_graph(e->obj, &nd->dtor);
                    nd->rc = nd->fg ? *(int64_t *)((char *)e->obj + ZAN_OBJ_RC_OFF) : 0;
                    nd->internal = 0;
                    nd->marked = 0;
                    zan_obj_map_put(&map, e->obj, (int)n);
                    n++;
                }
            }
        }
    }
    /* 注册表锁只护住快照；释放阶段会经 free 路径反注册，必须先放锁 */
    for (int s = ZAN_LIVE_STRIPES - 1; s >= 0; s--) zan_live_unlock(&g_live_stripes[s]);
    if (!map.entries || n == 0) {
        zan_obj_map_free(&map);
        free(nodes);
        free(queue);
        return 0;
    }

    zan_gc_ctx_t ctx = { nodes, &map, queue, 0 };
    for (size_t i = 0; i < n; i++)
        if (nodes[i].fg) zan_gc_walk_slots(nodes[i].obj, nodes[i].fg, zan_gc_count_cb, &ctx);
    for (size_t i = 0; i < n; i++) {
        /* 不可回收的对象（字符串/数组/闭包/栈对象）与有外部持有者的对象都是根；
         * rc ≤ 0 或 immortal 的异常计数也按根处理，不碰 */
        if (!nodes[i].fg || nodes[i].rc > nodes[i].internal || nodes[i].rc <= 0) {
            nodes[i].marked = 1;
            queue[ctx.qtail++] = (int)i;
        }
    }
    for (size_t qh = 0; qh < ctx.qtail; qh++) {
        zan_gc_node_t *u = &nodes[queue[qh]];
        if (u->fg) zan_gc_walk_slots(u->obj, u->fg, zan_gc_mark_cb, &ctx);
    }

    int64_t freed = 0;
    for (size_t i = 0; i < n; i++)
        if (!nodes[i].marked) zan_gc_walk_slots(nodes[i].obj, nodes[i].fg, zan_gc_clear_cb, &ctx);
    for (size_t i = 0; i < n; i++) {
        if (nodes[i].marked) continue;
        void *obj = nodes[i].obj;
        __atomic_store_n((int64_t *)((char *)obj + ZAN_OBJ_RC_OFF), 1, __ATOMIC_RELEASE);
        ((void (*)(void *))nodes[i].dtor)(obj);
        freed++;
    }
    zan_obj_map_free(&map);
    free(nodes);
    free(queue);
    return freed;
}

typedef struct {
    const char *tyname;
    size_t      count;
    size_t      bytes;
} zan_heap_type_stat_t;

int64_t zan_gc_write_heap_snapshot(const char *path) {
    if (!path || !path[0]) return -1;
    if (!__atomic_load_n(&g_live_stripes_inited, __ATOMIC_ACQUIRE)) return 0;

    for (int s = 0; s < ZAN_LIVE_STRIPES; s++) {
        zan_live_lock(&g_live_stripes[s]);
    }

    size_t total_count = 0;
    size_t total_bytes = 0;
    for (int s = 0; s < ZAN_LIVE_STRIPES; s++) {
        total_count += g_live_stripes[s].count;
    }

    size_t stat_cap = 256;
    size_t stat_count = 0;
    zan_heap_type_stat_t *stats = (zan_heap_type_stat_t *)malloc(stat_cap * sizeof(zan_heap_type_stat_t));

    for (int s = 0; s < ZAN_LIVE_STRIPES; s++) {
        for (size_t b = 0; b < g_live_stripes[s].bucket_count; b++) {
            zan_live_entry_t *entry = g_live_stripes[s].buckets[b];
            while (entry) {
                total_bytes += entry->size;
                const char *tn = zan_get_object_tyname(entry->obj);
                int found = 0;
                for (size_t k = 0; k < stat_count; k++) {
                    if (strcmp(stats[k].tyname, tn) == 0) {
                        stats[k].count++;
                        stats[k].bytes += entry->size;
                        found = 1;
                        break;
                    }
                }
                if (!found && stats) {
                    if (stat_count >= stat_cap) {
                        stat_cap *= 2;
                        zan_heap_type_stat_t *ns = (zan_heap_type_stat_t *)realloc(stats, stat_cap * sizeof(zan_heap_type_stat_t));
                        if (ns) stats = ns;
                    }
                    if (stat_count < stat_cap) {
                        stats[stat_count].tyname = tn;
                        stats[stat_count].count = 1;
                        stats[stat_count].bytes = entry->size;
                        stat_count++;
                    }
                }
                entry = entry->next;
            }
        }
    }

    for (int s = ZAN_LIVE_STRIPES - 1; s >= 0; s--) {
        zan_live_unlock(&g_live_stripes[s]);
    }

    if (stats) {
        for (size_t i = 0; i < stat_count; i++) {
            for (size_t j = i + 1; j < stat_count; j++) {
                if (stats[j].bytes > stats[i].bytes) {
                    zan_heap_type_stat_t tmp = stats[i];
                    stats[i] = stats[j];
                    stats[j] = tmp;
                }
            }
        }
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        if (stats) free(stats);
        return -1;
    }

    fprintf(f, "# Zan Heap Snapshot\n");
    fprintf(f, "# Total Live Objects: %zu\n", total_count);
    fprintf(f, "# Total Allocated Bytes: %zu\n\n", total_bytes);
    fprintf(f, "%-40s %15s %15s\n", "Type", "Count", "Bytes");
    fprintf(f, "--------------------------------------------------------------------------------\n");
    if (stats) {
        for (size_t k = 0; k < stat_count; k++) {
            fprintf(f, "%-40s %15zu %15zu\n", stats[k].tyname, stats[k].count, stats[k].bytes);
        }
    }
    fprintf(f, "--------------------------------------------------------------------------------\n");
    fclose(f);

    if (stats) free(stats);
    return (int64_t)total_count;
}

