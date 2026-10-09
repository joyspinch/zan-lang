/* gui_runtime_glyph */

/* 核心系统底层抽象与内存语义契约 */
#define ZAN_TILE_RUN   0
#define ZAN_TILE_GLYPH 1

typedef struct {
    char    *key;      /* 核心系统底层抽象与内存语义契约 */
    int      key_len;
    int      kind;
    int      size;
    uint64_t used;     /* 核心系统底层抽象与内存语义契约 */
    uint64_t stamped;  /* 底层系统交互与数据协议契约 */
    size_t   bytes;    /* 核心系统底层抽象与内存语义契约 */
    zan_glyph_tile tile;
} zan_atlas_slot;

/* 内部辅助逻辑 */
#define ZAN_ATLAS_CAP   4096
#define ZAN_ATLAS_PROBE 8
/* 底层系统交互与数据协议契约 */
#if defined(__ANDROID__)
#define ZAN_ATLAS_BYTES (4u * 1024u * 1024u)
#else
#define ZAN_ATLAS_BYTES (32u * 1024u * 1024u)
#endif

static zan_atlas_slot g_atlas[ZAN_ATLAS_CAP];
static uint64_t g_atlas_clock = 0;
static uint64_t g_atlas_stores = 0;
static size_t g_atlas_bytes = 0;
static uint32_t g_atlas_next_id = 1;
static uint64_t g_atlas_hits = 0;
static uint64_t g_atlas_misses = 0;
static uint64_t g_atlas_swept = 0;

/* 核心系统底层抽象与内存语义契约 */
#define ZAN_ATLAS_POOL_CAP 256
#if defined(__ANDROID__)
#define ZAN_ATLAS_POOL_BYTES (1u * 1024u * 1024u)
#else
#define ZAN_ATLAS_POOL_BYTES (4u * 1024u * 1024u)
#endif
static struct { void *p; size_t bytes; } g_cov_pool[ZAN_ATLAS_POOL_CAP];
static int g_cov_pool_n = 0;
static size_t g_cov_pool_bytes = 0;

static size_t zan_cov_round(size_t bytes) {
    if (bytes <= 4096) { return (bytes + 1023) & ~(size_t)1023; }
    return (bytes + 8191) & ~(size_t)8191;
}

static void *zan_cov_pool_take(size_t bytes) {
    for (int i = 0; i < g_cov_pool_n; i++) {
        if (g_cov_pool[i].bytes == bytes) {
            void *p = g_cov_pool[i].p;
            g_cov_pool[i] = g_cov_pool[g_cov_pool_n - 1];
            g_cov_pool_n--;
            g_cov_pool_bytes -= bytes;
            return p;
        }
    }
    return malloc(bytes);
}

static void zan_cov_pool_put(void *p, size_t bytes) {
    if (!p) return;
    if (g_cov_pool_n >= ZAN_ATLAS_POOL_CAP
        || g_cov_pool_bytes + bytes > ZAN_ATLAS_POOL_BYTES) {
        free(p);
        return;
    }
    g_cov_pool[g_cov_pool_n].p = p;
    g_cov_pool[g_cov_pool_n].bytes = bytes;
    g_cov_pool_n++;
    g_cov_pool_bytes += bytes;
}

static uint64_t zan_atlas_hash(int kind, int size,
                               const char *key, int key_len) {
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < key_len; i++) {
        h ^= (uint64_t)(unsigned char)key[i];
        h *= 1099511628211ULL;
    }
    h ^= (uint64_t)(unsigned)size;
    h *= 1099511628211ULL;
    h ^= (uint64_t)(unsigned)kind;
    h *= 1099511628211ULL;
    return h;
}

static void zan_atlas_release(zan_atlas_slot *slot) {
    if (!slot->key) return;
    free(slot->key);
    zan_cov_pool_put((void *)slot->tile.cov, zan_cov_round(slot->bytes));
    g_atlas_bytes -= slot->bytes;
    memset(slot, 0, sizeof(*slot));
}

static void zan_atlas_trim(void) {
    while (g_atlas_bytes > ZAN_ATLAS_BYTES) {
        zan_atlas_slot *oldest = NULL;
        for (int i = 0; i < ZAN_ATLAS_CAP; i++) {
            if (!g_atlas[i].key) continue;
            if (!oldest || g_atlas[i].used < oldest->used) oldest = &g_atlas[i];
        }
        if (!oldest) return;
        zan_atlas_release(oldest);
    }
}

/* 内部辅助逻辑 */
#define ZAN_ATLAS_COLD_STORES 64
/* 底层系统交互与数据协议契约 */
#define ZAN_ATLAS_SWEEP 64

/* 核心系统底层抽象与内存语义契约 */
static void zan_atlas_sweep(void) {
    for (int i = 0; i < ZAN_ATLAS_CAP; i++) {
        zan_atlas_slot *e = &g_atlas[i];
        if (!e->key) continue;
        if (g_atlas_stores - e->stamped >= ZAN_ATLAS_COLD_STORES) {
            zan_atlas_release(e);
            g_atlas_swept++;
        }
    }
}

/* 底层系统交互与数据协议契约 */
static const zan_glyph_tile *zan_atlas_find(int kind, int size,
                                            const char *key, int key_len) {
    uint64_t h = zan_atlas_hash(kind, size, key, key_len);
    int base = (int)(h % ZAN_ATLAS_CAP);
    for (int i = 0; i < ZAN_ATLAS_PROBE; i++) {
        zan_atlas_slot *e = &g_atlas[(base + i) % ZAN_ATLAS_CAP];
        if (!e->key) continue;
        if (e->kind == kind && e->size == size && e->key_len == key_len
            && memcmp(e->key, key, (size_t)key_len) == 0) {
            e->used = ++g_atlas_clock;
            e->stamped = g_atlas_stores;
            g_atlas_hits++;
            return &e->tile;
        }
    }
    g_atlas_misses++;
    return NULL;
}

/* 内部辅助逻辑 */
static const zan_glyph_tile *zan_atlas_store(
    int kind, int size, const char *key, int key_len,
    int w, int h, int left, int top, int advance,
    const void *cov, int bpp) {
    if (w < 0 || h < 0) return NULL;
    if (w == 0 || h == 0) { w = 0; h = 0; cov = NULL; }
    uint64_t hv = zan_atlas_hash(kind, size, key, key_len);
    int base = (int)(hv % ZAN_ATLAS_CAP);
    zan_atlas_slot *victim = NULL;
    for (int i = 0; i < ZAN_ATLAS_PROBE; i++) {
        zan_atlas_slot *e = &g_atlas[(base + i) % ZAN_ATLAS_CAP];
        if (!e->key) { victim = e; break; }
        if (!victim || e->used < victim->used) victim = e;
    }

    size_t bytes = (size_t)w * (size_t)h * (size_t)bpp;
    char *key_copy = (char *)malloc((size_t)key_len ? (size_t)key_len : 1);
    void *cov_copy = bytes ? zan_cov_pool_take(zan_cov_round(bytes)) : NULL;
    if (!key_copy || (bytes && !cov_copy)) {
        free(key_copy);
        free(cov_copy);
        return NULL;
    }
    memcpy(key_copy, key, (size_t)key_len);
    if (bytes) memcpy(cov_copy, cov, bytes);

    zan_atlas_release(victim);
    victim->key = key_copy;
    victim->key_len = key_len;
    victim->kind = kind;
    victim->size = size;
    victim->used = ++g_atlas_clock;
    victim->stamped = g_atlas_stores;
    victim->bytes = bytes;
    victim->tile.id = g_atlas_next_id++;
    victim->tile.rev = 1;
    victim->tile.w = w;
    victim->tile.h = h;
    victim->tile.left = left;
    victim->tile.top = top;
    victim->tile.advance = advance;
    victim->tile.bpp = bpp;
    victim->tile.flags = 0;
    victim->tile.cov = cov_copy;
    g_atlas_bytes += bytes;
    g_atlas_stores++;
    if ((g_atlas_stores & (ZAN_ATLAS_SWEEP - 1)) == 0) { zan_atlas_sweep(); }
    zan_atlas_trim();
    return &victim->tile;
}

/* 内部辅助逻辑 */
#define ZAN_GLYPH_BATCH 128

typedef struct {
    zan_surface_t *s;
    u32 color;
    zan_glyph_item items[ZAN_GLYPH_BATCH];
    int count;
} zan_glyph_batch;

static void zan_glyph_batch_flush(zan_glyph_batch *b) {
    if (!b->count) return;
    zan_glyph_run run;
    run.color = b->color;
    run.count = b->count;
    run.items = b->items;
    ZAN_IMPL(b->s, glyph_run)->glyph_run(b->s, &run);
    b->count = 0;
}

static void zan_glyph_batch_add(zan_glyph_batch *b, const zan_glyph_tile *tile,
                                int x, int y) {
    if (!tile || tile->w <= 0 || tile->h <= 0) return;
    if (b->count == ZAN_GLYPH_BATCH) zan_glyph_batch_flush(b);
    b->items[b->count].tile = tile;
    b->items[b->count].x = x;
    b->items[b->count].y = y;
    b->count++;
}
