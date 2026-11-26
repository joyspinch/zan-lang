/* rt_timer_gc.c: 活对象注册表、孤儿环探测/回收器与堆快照（B-ID89）
 *
 * 随 rt_timer.o 进入所有 hosted 链接路径（rt_timer.c 末尾 #include）。曾在
 * rt_mem.c 里——那个对象只在 --fast-alloc 且非 Apple 的链接里存在，macOS
 * 上 GC.DetectCycles/WriteHeapSnapshot 静默恒为 0。wasm/bare 不编入（无线程
 * 锁原语），IR 侧的弱空实现兜底。
 *
 * 分配走普通 malloc/free（可通过 ZAN_GC_* 宏在独立测试中覆写）：--fast-alloc
 * 的 --wrap 会把它们导向小对象分配器，分配器本身不回调注册表，不会重入。 */

#ifndef ZAN_GC_MALLOC
#define ZAN_GC_MALLOC malloc
#endif
#ifndef ZAN_GC_CALLOC
#define ZAN_GC_CALLOC calloc
#endif
#ifndef ZAN_GC_REALLOC
#define ZAN_GC_REALLOC realloc
#endif
#ifndef ZAN_GC_FREE
#define ZAN_GC_FREE free
#endif

/* =========================================================================
 * B-ID89: 运行时活对象注册表、孤儿环探测器与堆快照导出
 * ========================================================================= */

/* A host may enable cycle scans after establishing control of all Zan heap
 * access. Every thread must enter before it can change the heap or its
 * reference counts and leave after cleanup, including any foreign callback or
 * destructor. Nested entries on a thread share one active slot. A host may also
 * scan after all mutators have left. Call at a boundary between heap
 * operations.
 *
 * The scan keeps the gate held to block new mutators. If another is active,
 * the scan returns -1 immediately rather than waiting for that thread, which
 * might itself be waiting for the caller. No scan runs without host opt-in. */
#if defined(_WIN32)
static SRWLOCK g_heap_gate = SRWLOCK_INIT;
#define ZAN_HEAP_GATE_LOCK() AcquireSRWLockExclusive(&g_heap_gate)
#define ZAN_HEAP_GATE_TRY_LOCK() TryAcquireSRWLockExclusive(&g_heap_gate)
#define ZAN_HEAP_GATE_UNLOCK() ReleaseSRWLockExclusive(&g_heap_gate)
#else
static pthread_mutex_t g_heap_gate = PTHREAD_MUTEX_INITIALIZER;
#define ZAN_HEAP_GATE_LOCK() pthread_mutex_lock(&g_heap_gate)
#define ZAN_HEAP_GATE_TRY_LOCK() (pthread_mutex_trylock(&g_heap_gate) == 0)
#define ZAN_HEAP_GATE_UNLOCK() pthread_mutex_unlock(&g_heap_gate)
#endif

static int g_heap_diagnostics_enabled;
static size_t g_heap_active_mutators;
static __thread size_t t_heap_mutator_depth;

void zan_rt_live_enable(void);
int64_t zan_gc_detect_cycles(void);

void zan_rt_heap_diagnostics_enable(void) {
    ZAN_HEAP_GATE_LOCK();
    g_heap_diagnostics_enabled = 1;
    ZAN_HEAP_GATE_UNLOCK();
    zan_rt_live_enable();
}

void zan_rt_heap_mutator_enter(void) {
    if (t_heap_mutator_depth++ != 0) return;
    ZAN_HEAP_GATE_LOCK();
    g_heap_active_mutators++;
    ZAN_HEAP_GATE_UNLOCK();
}

void zan_rt_heap_mutator_leave(void) {
    if (!t_heap_mutator_depth) {
        zan_rt_fatal("mem", "heap mutator leave without enter");
        return;
    }
    if (--t_heap_mutator_depth != 0) return;
    ZAN_HEAP_GATE_LOCK();
    g_heap_active_mutators--;
    ZAN_HEAP_GATE_UNLOCK();
}

static int zan_heap_scan_try_begin(void) {
    if (!ZAN_HEAP_GATE_TRY_LOCK()) return 0;
    size_t own_slot = t_heap_mutator_depth != 0;
    if (g_heap_diagnostics_enabled && g_heap_active_mutators == own_slot)
        return 1;
    ZAN_HEAP_GATE_UNLOCK();
    return 0;
}

#define ZAN_LIVE_STRIPES 32

typedef enum {
    ZAN_LIVE_OBJECT,
    ZAN_LIVE_ARRAY,
    ZAN_LIVE_REF_ARRAY,
} zan_live_kind_t;

typedef struct zan_live_entry {
    void                  *obj;
    size_t                 size;
    uint64_t               site;
    const char            *source;
    size_t                 data_offset;
    zan_live_kind_t        kind;
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

static void zan_live_lock_all(void) {
    for (int i = 0; i < ZAN_LIVE_STRIPES; i++) zan_live_lock(&g_live_stripes[i]);
}

static void zan_live_unlock_all(void) {
    for (int i = ZAN_LIVE_STRIPES - 1; i >= 0; i--) zan_live_unlock(&g_live_stripes[i]);
}

static inline unsigned zan_live_hash(uintptr_t ptr) {
    uintptr_t x = ptr >> 4;
    x ^= (x >> 16);
    x ^= (x >> 8);
    return (unsigned)x;
}

static void zan_live_atexit_detector(void) {
    zan_rt_heap_diagnostics_enable();
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
            g_live_stripes[i].buckets = (zan_live_entry_t **)ZAN_GC_CALLOC(64, sizeof(zan_live_entry_t *));
            if (g_live_stripes[i].buckets)
                g_live_stripes[i].bucket_count = 64;
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

/* 活对象注册表只服务 GC.DetectCycles / GC.CollectCycles / GC.WriteHeapSnapshot
 * 与 ZAN_DETECT_LEAKS 退出检测。它曾对每次分配无条件加条带锁登记——分配密集代码里
 * 占 20%~30% 的 CPU（binary-trees / 字符串拼接实测）。现在按需开启：编译器在程序引用
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

static void zan_live_register(void *obj, size_t size, zan_live_kind_t kind,
                              size_t data_offset, const char *source) {
    if (!obj || !zan_live_wanted() || size < ZAN_OBJ_HDR_SIZE) return;
    /* The compiler initializes the header before publishing the allocation.
     * Store its header word so snapshots need not read live object data. */
    uint64_t site = *(const uint64_t *)((const char *)obj + ZAN_HDR_WORD_OFF);
    /* Older callers use the object hook for rank-one primitive arrays. The
     * reserved site value identifies that layout without probing before -8. */
    if (kind == ZAN_LIVE_OBJECT && site == ZAN_ARRAY_MAGIC)
        kind = ZAN_LIVE_ARRAY;
    size_t header_size = kind == ZAN_LIVE_OBJECT ? ZAN_OBJ_HDR_SIZE : ZAN_ARR_HDR_SIZE;
    if (size < header_size || data_offset > size - header_size) return;

    zan_live_ensure_init();
    unsigned h = zan_live_hash((uintptr_t)obj);
    unsigned stripe_idx = h & (ZAN_LIVE_STRIPES - 1);
    zan_live_stripe_t *s = &g_live_stripes[stripe_idx];

    zan_live_lock(s);
    if (!s->buckets) {
        s->buckets = (zan_live_entry_t **)ZAN_GC_CALLOC(64, sizeof(zan_live_entry_t *));
        if (!s->buckets) {
            zan_live_unlock(s);
            return;
        }
        s->bucket_count = 64;
    }

    /* Grow buckets if load factor > 2 */
    if (s->count > s->bucket_count * 2 && s->bucket_count < (1u << 24)) {
        size_t new_cap = s->bucket_count * 2;
        zan_live_entry_t **new_buckets = (zan_live_entry_t **)ZAN_GC_CALLOC(new_cap, sizeof(zan_live_entry_t *));
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
            ZAN_GC_FREE(s->buckets);
            s->buckets = new_buckets;
            s->bucket_count = new_cap;
        }
    }

    unsigned b_idx = (h / ZAN_LIVE_STRIPES) & (s->bucket_count - 1);
    zan_live_entry_t *entry = s->free_list;
    if (entry) {
        s->free_list = entry->next;
    } else {
        entry = (zan_live_entry_t *)ZAN_GC_MALLOC(sizeof(zan_live_entry_t));
        if (!entry) {
            zan_live_unlock(s);
            return;
        }
    }
    entry->obj = obj;
    entry->size = size;
    entry->site = site;
    entry->source = source;
    entry->data_offset = data_offset;
    entry->kind = kind;
    entry->next = s->buckets[b_idx];
    s->buckets[b_idx] = entry;
    s->count++;
    zan_live_unlock(s);
}

void zan_rt_live_register(void *obj, size_t size) {
    zan_live_register(obj, size, ZAN_LIVE_OBJECT, 0, NULL);
}

void zan_rt_live_register_named(void *obj, uint64_t size, const char *source) {
    if (size > SIZE_MAX) return;
    zan_live_register(obj, (size_t)size, ZAN_LIVE_OBJECT, 0, source);
}

void zan_rt_live_register_array(void *obj, uint64_t size, uint64_t data_offset,
                                int32_t elements_are_refs) {
    if (size > SIZE_MAX || data_offset > SIZE_MAX) return;
    zan_live_register(obj, (size_t)size,
                      elements_are_refs ? ZAN_LIVE_REF_ARRAY : ZAN_LIVE_ARRAY,
                      (size_t)data_offset, NULL);
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

/* 默认（描述符）模式：站点号 → 描述符表（编译器在 main 入口登记）。
 * 对象头字 = (tw<<32)|rc，tw = 栈标记 | 对象标记 | 站点号（docs/OBJECT_LAYOUT.md）。 */
static void **g_desc_tbl_reg = NULL;
static int64_t g_desc_tbl_count = 0;

void zan_rt_register_desc_table(void **tbl, int64_t count) {
    g_desc_tbl_reg = tbl;
    g_desc_tbl_count = count;
}

static inline int zan_word_is_string(uint64_t word) { return (word >> 32) == ZAN_STRING_TAG; }

/* 对象头字的站点号；不是对象头（串、数组、外来指针）给 0（哨兵站点） */
static inline uint64_t zan_word_site(uint64_t word) {
    if (zan_word_is_string(word) || !ZAN_HDR_IS_OBJ(word)) return 0;
    return ZAN_HDR_SITE(word);
}

/* 对象头实际占用的前缀字节数：普通 8 字节，PAD16 类 16 字节 */
static inline size_t zan_obj_hdr_bytes(uint64_t word) {
    return (ZAN_HDR_IS_OBJ(word) && (((word >> 32) & ZAN_OBJ_TW_PAD16) != 0))
               ? (ZAN_OBJ_HDR_SIZE + 8)
               : ZAN_OBJ_HDR_SIZE;
}

/* 头字 → 描述符：对象按站点号查登记的描述符表；数组头字里的大值是数组
 * 描述符指针（没有对象标记）；其余（串、魔数、秩、越界站点）为 NULL */
static const zan_arc_desc_t *zan_word_desc(uint64_t word) {
    if (zan_word_is_string(word)) return NULL;
    if (ZAN_HDR_IS_OBJ(word)) {
        uint64_t site = ZAN_HDR_SITE(word);
        if (g_desc_tbl_reg && site > 0 && (int64_t)site < g_desc_tbl_count)
            return (const zan_arc_desc_t *)g_desc_tbl_reg[site];
        return NULL;
    }
    if (word >= 4096 && word != ZAN_ARRAY_MAGIC) return (const zan_arc_desc_t *)(uintptr_t)word;
    return NULL;
}

/* 委托值是带 ZAN_CLOSURE_TAG 低位标记的闭包记录指针（对象至少 8 字节对齐，
 * 低位恒 0）：走边时去掉标记才能在注册表里对上闭包记录本身。 */
static inline void *zan_edge_untag(void *p) {
    return (void *)((uintptr_t)p & ~(uintptr_t)ZAN_CLOSURE_TAG);
}

/* 数组的引用计数在 -32（i64；-16 是长度）；其余对象与字符串在 -8（u32） */
static inline int64_t zan_live_read_rc(const zan_live_entry_t *e) {
    if (e->kind == ZAN_LIVE_OBJECT)
        return (int64_t)(int32_t)__atomic_load_n(ZAN_RC_PTR(e->obj), __ATOMIC_RELAXED);
    return __atomic_load_n((const int64_t *)((const char *)e->obj + ZAN_ARR_RC_OFF), __ATOMIC_RELAXED);
}

static const char *zan_get_object_tyname(const zan_live_entry_t *entry) {
    uint64_t word = entry->site & ~ZAN_OBJ_STACK_FLAG;
    const char *const *names = NULL;
    if (entry->kind != ZAN_LIVE_OBJECT) {
        /* The rectangular layout uses its site word for the rank. */
        if (entry->kind == ZAN_LIVE_REF_ARRAY && entry->data_offset == 0 &&
            word != ZAN_ARRAY_MAGIC && word >= 4096 && !ZAN_HDR_IS_OBJ(word)) {
            const zan_arc_desc_t *desc = (const zan_arc_desc_t *)(uintptr_t)word;
            names = desc->tynames_arr;
        }
        return names && names[0] ? names[0] : "Array";
    }
    if (zan_word_is_string(word)) return "string";
    const zan_arc_desc_t *desc = zan_word_desc(word);
    if (desc && desc->tynames_arr) {
        names = desc->tynames_arr;
    } else {
        uint64_t site = zan_word_site(word);
        if (g_site_tynames_reg && site > 0 && site < (uint64_t)g_site_reg_count)
            names = g_site_tynames_reg[site];
    }
    return names && names[0] ? names[0] : "object";
}

typedef void (*zan_edge_cb_t)(void *to, void *ctx);

static void zan_walk_pointer_edges(void *const *data, size_t count,
                                   zan_edge_cb_t cb, void *ctx) {
    if (!data) return;
    for (size_t i = 0; i < count; i++) {
        if (data[i]) cb(zan_edge_untag(data[i]), ctx);
    }
}

static void zan_walk_object_edges(const zan_live_entry_t *entry,
                                  zan_edge_cb_t cb, void *ctx) {
    const char *obj = entry->obj;
    if (entry->kind != ZAN_LIVE_OBJECT) {
        if (entry->kind == ZAN_LIVE_REF_ARRAY) {
            int64_t count = *(const int64_t *)(obj + ZAN_ARR_COUNT_OFF);
            size_t bytes = entry->size - ZAN_ARR_HDR_SIZE - entry->data_offset;
            if (count > 0 && (uint64_t)count <= bytes / sizeof(void *)) {
                zan_walk_pointer_edges((void *const *)(obj + entry->data_offset),
                                       (size_t)count, cb, ctx);
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
            if (child) cb(zan_edge_untag(child), ctx);
        }
    } else if (fg->kind == 1 || fg->kind == 2) { /* List<T> or Dict<K,V> */
        size_t required = 16 + sizeof(void *) * fg->kind;
        if (bytes < required) return;
        int64_t count = *(const int64_t *)obj;
        int64_t capacity = *(const int64_t *)(obj + 8);
        if (count <= 0 || count > capacity) return;
        if (fg->kind == 1 || (fg->ref_count & 1)) {
            void *const *data = *(void *const *const *)(obj + 16);
            zan_walk_pointer_edges(data, (size_t)count, cb, ctx);
        }
        if (fg->kind == 2 && (fg->ref_count & 2)) {
            void *const *values = *(void *const *const *)(obj + 24);
            zan_walk_pointer_edges(values, (size_t)count, cb, ctx);
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
    if (min_cap > SIZE_MAX / (2 * sizeof(zan_map_entry_t))) {
        map->capacity = 0;
        map->mask = 0;
        map->entries = NULL;
        return;
    }
    size_t target = min_cap * 2;
    size_t cap = 64;
    while (cap < target) {
        if (cap > SIZE_MAX / (2 * sizeof(zan_map_entry_t))) {
            map->capacity = 0;
            map->mask = 0;
            map->entries = NULL;
            return;
        }
        cap *= 2;
    }
    map->capacity = cap;
    map->mask = cap - 1;
    map->entries = (zan_map_entry_t *)ZAN_GC_CALLOC(cap, sizeof(zan_map_entry_t));
}

static inline void zan_obj_map_free(zan_obj_map_t *map) {
    if (map->entries) ZAN_GC_FREE(map->entries);
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
    const zan_live_entry_t *entry;
    int64_t                 rc;
    int64_t                 internal_rc;
    int                     marked;
} zan_cycle_node_t;

typedef struct {
    zan_cycle_node_t *nodes;
    zan_obj_map_t    *map;
} zan_cycle_ctx_t;

static void zan_cycle_edge_count_cb(void *to, void *ctx) {
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

static void zan_cycle_bfs_cb(void *to, void *ctx) {
    zan_bfs_ctx_t *b = (zan_bfs_ctx_t *)ctx;
    int v = zan_obj_map_get(b->map, to);
    if (v >= 0 && !b->nodes[v].marked) {
        b->nodes[v].marked = 1;
        b->queue[(*b->qtail)++] = v;
    }
}

int64_t zan_gc_detect_cycles(void) {
    if (!zan_heap_scan_try_begin()) return -1;
    if (!__atomic_load_n(&g_live_stripes_inited, __ATOMIC_ACQUIRE)) {
        ZAN_HEAP_GATE_UNLOCK();
        return 0;
    }

    zan_live_lock_all();
    zan_cycle_node_t *nodes = NULL;
    int *queue = NULL;
    zan_obj_map_t map = {0};
    int64_t orphan_count = -1;

    size_t total_count = 0;
    for (int s = 0; s < ZAN_LIVE_STRIPES; s++) {
        total_count += g_live_stripes[s].count;
    }
    if (total_count == 0) {
        orphan_count = 0;
        goto done;
    }
    if (total_count > INT32_MAX || total_count > SIZE_MAX / sizeof(*nodes))
        goto done;

    nodes = (zan_cycle_node_t *)ZAN_GC_MALLOC(total_count * sizeof(*nodes));
    queue = (int *)ZAN_GC_MALLOC(total_count * sizeof(*queue));
    zan_obj_map_init(&map, total_count);
    if (!nodes || !queue || !map.entries)
        goto done;

    size_t idx = 0;
    for (int s = 0; s < ZAN_LIVE_STRIPES; s++) {
        for (size_t b = 0; b < g_live_stripes[s].bucket_count; b++) {
            zan_live_entry_t *entry = g_live_stripes[s].buckets[b];
            while (entry && idx < total_count) {
                int64_t rc = zan_live_read_rc(entry);
                /* A destructor can call the diagnostic while its own object
                 * is still registered. The outgoing edges are no longer live. */
                if (rc != 0) {
                    nodes[idx] = (zan_cycle_node_t){.entry = entry, .rc = rc};
                    zan_obj_map_put(&map, entry->obj, (int)idx++);
                }
                entry = entry->next;
            }
        }
    }
    size_t actual_count = idx;

    zan_cycle_ctx_t ctx = { nodes, &map };
    for (size_t i = 0; i < actual_count; i++) {
        zan_walk_object_edges(nodes[i].entry, zan_cycle_edge_count_cb, &ctx);
    }

    size_t qhead = 0, qtail = 0;
    for (size_t i = 0; i < actual_count; i++) {
        if (nodes[i].rc < 0 || nodes[i].rc > nodes[i].internal_rc) {
            nodes[i].marked = 1;
            queue[qtail++] = (int)i;
        }
    }
    zan_bfs_ctx_t bfs_ctx = { nodes, &map, queue, &qtail };
    while (qhead < qtail) {
        int u = queue[qhead++];
        zan_walk_object_edges(nodes[u].entry, zan_cycle_bfs_cb, &bfs_ctx);
    }

    orphan_count = 0;
    size_t orphan_bytes = 0;
    for (size_t i = 0; i < actual_count; i++) {
        if (!nodes[i].marked) {
            orphan_count++;
            orphan_bytes += nodes[i].entry->size;
        }
    }

    if (orphan_count > 0) {
        fprintf(stderr,
            "[zan-gc] cycle detection: %lld unreachable object(s) in orphan cycles (%zu bytes):\n",
            (long long)orphan_count, orphan_bytes);
        int printed = 0;
        for (size_t i = 0; i < actual_count && printed < 20; i++) {
            if (!nodes[i].marked) {
                const zan_live_entry_t *entry = nodes[i].entry;
                uint64_t site = zan_word_site(entry->site & ~ZAN_OBJ_STACK_FLAG);
                fprintf(stderr,
                    "  - [%s] at %p (rc=%lld, internal_rc=%lld, size=%zu, site=%llu)\n",
                    zan_get_object_tyname(entry), entry->obj, (long long)nodes[i].rc,
                    (long long)nodes[i].internal_rc, entry->size,
                    (unsigned long long)site);
                printed++;
            }
        }
        if (orphan_count > 20) {
            fprintf(stderr, "  ... and %lld more orphan object(s)\n",
                    (long long)(orphan_count - 20));
        }
    }

done:
    ZAN_GC_FREE(queue);
    zan_obj_map_free(&map);
    ZAN_GC_FREE(nodes);
    zan_live_unlock_all();
    ZAN_HEAP_GATE_UNLOCK();
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
 * 前提：已启用堆诊断并通过安全闸门，调用期间没有其他活跃 mutator。
 * 环内对象的析构看到的是已被清空的环内字段。 */

typedef void (*zan_slot_cb_t)(void *from, void **slot, void *ctx);

/* 可回收对象返回其字段图并经 *dtor 给出类型化释放函数；否则 NULL（视为根） */
static const zan_field_graph_t *zan_gc_collectable_graph(const zan_live_entry_t *entry,
                                                         void **dtor) {
    if (entry->kind != ZAN_LIVE_OBJECT) return NULL;
    uint64_t raw = entry->site;
    if (raw & ZAN_OBJ_STACK_FLAG) return NULL;
    if (zan_word_is_string(raw) || !ZAN_HDR_IS_OBJ(raw)) return NULL; /* 串、数组 */
    uint64_t site = ZAN_HDR_SITE(raw);
    if (!g_desc_tbl_reg) {
        /* 站点表模式；0 号是未知站点。闭包记录等非类站点在析构表里为空，自然排除 */
        if (site == 0 || !g_site_dtors_reg || !g_site_field_graphs_reg ||
            (int64_t)site >= g_site_dtors_count || (int64_t)site >= g_site_reg_count)
            return NULL;
        *dtor = g_site_dtors_reg[site];
        const zan_field_graph_t *fg = (const zan_field_graph_t *)g_site_field_graphs_reg[site];
        return (*dtor && fg) ? fg : NULL;
    }
    const zan_arc_desc_t *desc = zan_word_desc(raw);
    if (!desc || !desc->dtor_fn || !desc->field_graph) return NULL;
    *dtor = desc->dtor_fn;
    return desc->field_graph;
}

static void zan_gc_walk_slots(void *obj, size_t body_bytes, const zan_field_graph_t *fg,
                              zan_slot_cb_t cb, void *ctx) {
    if (fg->kind == 0) {
        for (uint32_t k = 0; k < fg->ref_count; k++) {
            uint32_t off = fg->refs[k].offset;
            if (!fg->refs[k].is_rc || off > body_bytes || body_bytes - off < sizeof(void *))
                continue;
            void **slot = (void **)((char *)obj + off);
            if (*slot) cb(obj, slot, ctx);
        }
    } else if (fg->kind == 1) {
        if (body_bytes < 16 + sizeof(void *)) return;
        int64_t count = *(const int64_t *)obj;
        int64_t capacity = *(const int64_t *)((const char *)obj + 8);
        void **data = *(void ***)((char *)obj + 16);
        if (data && count > 0 && count <= capacity) {
            for (int64_t j = 0; j < count; j++)
                if (data[j]) cb(obj, &data[j], ctx);
        }
    }
}

typedef struct {
    void                    *obj;
    size_t                   body_bytes;
    const zan_field_graph_t *fg;
    void                    *dtor;
    int64_t                  rc;
    int64_t                  internal;
    int                      marked;
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
    if (!zan_heap_scan_try_begin()) return -1;
    if (!__atomic_load_n(&g_live_stripes_inited, __ATOMIC_ACQUIRE)) {
        ZAN_HEAP_GATE_UNLOCK();
        return 0;
    }

    zan_live_lock_all();
    size_t total = 0;
    for (int s = 0; s < ZAN_LIVE_STRIPES; s++) total += g_live_stripes[s].count;
    if (total == 0) {
        zan_live_unlock_all();
        ZAN_HEAP_GATE_UNLOCK();
        return 0;
    }
    if (total > INT32_MAX || total > SIZE_MAX / sizeof(zan_gc_node_t)) {
        zan_live_unlock_all();
        ZAN_HEAP_GATE_UNLOCK();
        return -1;
    }

    zan_gc_node_t *nodes = (zan_gc_node_t *)ZAN_GC_MALLOC(total * sizeof(zan_gc_node_t));
    int *queue = (int *)ZAN_GC_MALLOC(total * sizeof(int));
    zan_obj_map_t map = { 0 };
    size_t n = 0;
    if (nodes && queue) {
        zan_obj_map_init(&map, total);
        for (int s = 0; s < ZAN_LIVE_STRIPES; s++) {
            for (size_t b = 0; b < g_live_stripes[s].bucket_count; b++) {
                for (zan_live_entry_t *e = g_live_stripes[s].buckets[b]; e && n < total; e = e->next) {
                    zan_gc_node_t *nd = &nodes[n];
                    size_t hdr_bytes = zan_obj_hdr_bytes(e->site & ~ZAN_OBJ_STACK_FLAG);
                    nd->obj = e->obj;
                    nd->body_bytes = e->size > hdr_bytes ? e->size - hdr_bytes : 0;
                    nd->dtor = NULL;
                    nd->fg = zan_gc_collectable_graph(e, &nd->dtor);
                    nd->rc = nd->fg ? zan_live_read_rc(e) : 0;
                    nd->internal = 0;
                    nd->marked = 0;
                    zan_obj_map_put(&map, e->obj, (int)n);
                    n++;
                }
            }
        }
    }
    /* 注册表锁只护住快照；释放阶段会经 free 路径反注册，必须先放锁 */
    zan_live_unlock_all();
    if (!nodes || !queue || !map.entries || n == 0) {
        zan_obj_map_free(&map);
        ZAN_GC_FREE(nodes);
        ZAN_GC_FREE(queue);
        ZAN_HEAP_GATE_UNLOCK();
        return (nodes && queue && map.entries) ? 0 : -1;
    }

    zan_gc_ctx_t ctx = { nodes, &map, queue, 0 };
    for (size_t i = 0; i < n; i++)
        if (nodes[i].fg) zan_gc_walk_slots(nodes[i].obj, nodes[i].body_bytes, nodes[i].fg, zan_gc_count_cb, &ctx);
    for (size_t i = 0; i < n; i++) {
        /* 不可回收的对象（字符串/数组/闭包/栈对象）与有外部持有者的对象都是根；
         * rc <= 0 或 immortal 的异常计数也按根处理，不碰 */
        if (!nodes[i].fg || nodes[i].rc > nodes[i].internal || nodes[i].rc <= 0) {
            nodes[i].marked = 1;
            queue[ctx.qtail++] = (int)i;
        }
    }
    for (size_t qh = 0; qh < ctx.qtail; qh++) {
        zan_gc_node_t *u = &nodes[queue[qh]];
        if (u->fg) zan_gc_walk_slots(u->obj, u->body_bytes, u->fg, zan_gc_mark_cb, &ctx);
    }

    for (size_t i = 0; i < n; i++) {
        if (!nodes[i].marked) {
            zan_gc_walk_slots(nodes[i].obj, nodes[i].body_bytes, nodes[i].fg, zan_gc_clear_cb, &ctx);
            __atomic_store_n(ZAN_RC_PTR(nodes[i].obj), 1u, __ATOMIC_RELEASE);
        }
    }
    /* 孤儿环之间已断链且 rc 归 1；析构体可能回调诊断/mutator，锁外执行 */
    ZAN_HEAP_GATE_UNLOCK();

    int64_t freed = 0;
    for (size_t i = 0; i < n; i++) {
        if (nodes[i].marked) continue;
        ((void (*)(void *))nodes[i].dtor)(nodes[i].obj);
        freed++;
    }
    zan_obj_map_free(&map);
    ZAN_GC_FREE(nodes);
    ZAN_GC_FREE(queue);
    return freed;
}

typedef struct {
    const char *tyname;
    size_t      count;
    size_t      bytes;
} zan_heap_type_stat_t;

typedef struct {
    zan_heap_type_stat_t type;
    const char          *source;
} zan_heap_source_stat_t;

typedef struct {
    zan_heap_source_stat_t *rows;
    size_t                  capacity;
    size_t                  count;
} zan_heap_sources_t;

static zan_heap_source_stat_t *zan_heap_source_slot(zan_heap_sources_t *sources,
                                                    const char *type,
                                                    const char *source) {
    size_t hash = 5381;
    for (const char *p = type; *p; p++)
        hash = hash * 33 + (unsigned char)*p;
    hash *= 33;
    for (const char *p = source; *p; p++)
        hash = hash * 33 + (unsigned char)*p;
    size_t mask = sources->capacity - 1;
    size_t i = hash & mask;
    while (sources->rows[i].source &&
           (strcmp(sources->rows[i].type.tyname, type) ||
            strcmp(sources->rows[i].source, source))) {
        i = (i + 1) & mask;
    }
    return &sources->rows[i];
}

static int zan_heap_source_add(zan_heap_sources_t *sources, const char *type,
                               const char *source, size_t bytes) {
    if (!source || !source[0]) source = "<unknown>";
    zan_heap_source_stat_t *row =
        sources->capacity ? zan_heap_source_slot(sources, type, source) : NULL;
    if ((!row || !row->source) && sources->count >= sources->capacity / 2) {
        if (sources->capacity > SIZE_MAX / sizeof(*sources->rows) / 2)
            return 0;
        size_t capacity = sources->capacity ? sources->capacity * 2 : 256;
        zan_heap_source_stat_t *rows =
            (zan_heap_source_stat_t *)ZAN_GC_CALLOC(capacity, sizeof(*rows));
        if (!rows) return 0;
        zan_heap_sources_t grown = {rows, capacity, sources->count};
        for (size_t i = 0; i < sources->capacity; i++) {
            zan_heap_source_stat_t *old_row = &sources->rows[i];
            if (old_row->source)
                *zan_heap_source_slot(&grown, old_row->type.tyname, old_row->source) = *old_row;
        }
        ZAN_GC_FREE(sources->rows);
        *sources = grown;
        row = zan_heap_source_slot(sources, type, source);
    }
    if (!row->source) {
        row->type.tyname = type;
        row->source = source;
        sources->count++;
    }
    row->type.count++;
    row->type.bytes += bytes;
    return 1;
}

static int zan_heap_source_compare(const void *lhs, const void *rhs) {
    const zan_heap_source_stat_t *a = lhs;
    const zan_heap_source_stat_t *b = rhs;
    if (a->type.bytes != b->type.bytes)
        return a->type.bytes > b->type.bytes ? -1 : 1;
    int types = strcmp(a->type.tyname, b->type.tyname);
    return types ? types : strcmp(a->source, b->source);
}

int64_t zan_gc_write_heap_snapshot(const char *path) {
    if (!path || !path[0]) return -1;
    if (!__atomic_load_n(&g_live_stripes_inited, __ATOMIC_ACQUIRE)) return 0;

    zan_live_lock_all();

    size_t total_count = 0;
    size_t total_bytes = 0;
    for (int s = 0; s < ZAN_LIVE_STRIPES; s++) {
        total_count += g_live_stripes[s].count;
    }

    size_t stat_cap = 0;
    size_t stat_count = 0;
    zan_heap_type_stat_t *stats = NULL;
    zan_heap_sources_t sources = {0};

    for (int s = 0; s < ZAN_LIVE_STRIPES; s++) {
        for (size_t b = 0; b < g_live_stripes[s].bucket_count; b++) {
            zan_live_entry_t *entry = g_live_stripes[s].buckets[b];
            while (entry) {
                total_bytes += entry->size;
                const char *tn = zan_get_object_tyname(entry);
                size_t k = 0;
                while (k < stat_count && strcmp(stats[k].tyname, tn) != 0) k++;
                if (k == stat_count) {
                    if (stat_count == stat_cap) {
                        if (stat_cap > SIZE_MAX / sizeof(*stats) / 2)
                            goto stats_error;
                        size_t new_cap = stat_cap ? stat_cap * 2 : 256;
                        zan_heap_type_stat_t *new_stats = (zan_heap_type_stat_t *)ZAN_GC_REALLOC(
                            stats, new_cap * sizeof(*stats));
                        if (!new_stats) goto stats_error;
                        stats = new_stats;
                        stat_cap = new_cap;
                    }
                    stats[stat_count++] = (zan_heap_type_stat_t){.tyname = tn};
                }
                stats[k].count++;
                stats[k].bytes += entry->size;
                if (!zan_heap_source_add(&sources, tn, entry->source, entry->size))
                    goto stats_error;
                entry = entry->next;
            }
        }
    }

    zan_live_unlock_all();

    for (size_t i = 0; i < stat_count; i++) {
        for (size_t j = i + 1; j < stat_count; j++) {
            if (stats[j].bytes > stats[i].bytes) {
                zan_heap_type_stat_t tmp = stats[i];
                stats[i] = stats[j];
                stats[j] = tmp;
            }
        }
    }

    size_t occupied = 0;
    for (size_t i = 0; i < sources.capacity; i++) {
        if (sources.rows[i].source)
            sources.rows[occupied++] = sources.rows[i];
    }
    if (sources.count > 1) {
        qsort(sources.rows, sources.count, sizeof(*sources.rows),
              zan_heap_source_compare);
    }

    FILE *f = fopen(path, "w");
    if (!f) goto write_error;

    fprintf(f, "# Zan Heap Snapshot\n");
    fprintf(f, "# Total Live Objects: %zu\n", total_count);
    fprintf(f, "# Total Allocated Bytes: %zu\n\n", total_bytes);
    fprintf(f, "%-40s %15s %15s\n", "Type", "Count", "Bytes");
    fprintf(f, "--------------------------------------------------------------------------------\n");
    for (size_t k = 0; k < stat_count; k++) {
        fprintf(f, "%-40s %15zu %15zu\n", stats[k].tyname, stats[k].count, stats[k].bytes);
    }
    fprintf(f, "--------------------------------------------------------------------------------\n");
    fprintf(f, "\n# Live Objects by Type and Source\nType\tSource\tCount\tBytes\n");
    for (size_t k = 0; k < sources.count; k++) {
        const zan_heap_source_stat_t *row = &sources.rows[k];
        fprintf(f, "%s\t%s\t%zu\t%zu\n",
                row->type.tyname, row->source, row->type.count, row->type.bytes);
    }
    int write_failed = ferror(f);
    if (fclose(f) != 0) write_failed = 1;

    ZAN_GC_FREE(sources.rows);
    ZAN_GC_FREE(stats);
    return write_failed ? -1 : (int64_t)total_count;

stats_error:
    zan_live_unlock_all();
write_error:
    ZAN_GC_FREE(sources.rows);
    ZAN_GC_FREE(stats);
    return -1;
}
