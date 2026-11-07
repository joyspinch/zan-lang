/* gui_runtime_glyph */

#define ZAN_TILE_RUN   0
#define ZAN_TILE_GLYPH 1

#define ZAN_ATLAS_INLINE_KEY 32

typedef struct {
    char     inline_key[ZAN_ATLAS_INLINE_KEY];
    char    *heap_key;
    int      key_len;
    int      kind;
    int      size;
    uint64_t used;
    uint64_t stamped;
    size_t   bytes;
    zan_glyph_tile tile;
} zan_atlas_slot;

#define ZAN_ATLAS_CAP   4096
#define ZAN_ATLAS_PROBE 8
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

static int g_atlas_last_kind = -1;
static int g_atlas_last_size = -1;
static int g_atlas_last_klen = -1;
static char g_atlas_last_key[ZAN_ATLAS_INLINE_KEY];
static const zan_glyph_tile *g_atlas_last_tile = NULL;

static void zan_atlas_release(zan_atlas_slot *slot) {
    if (slot->key_len <= 0) return;
    if (g_atlas_last_tile == &slot->tile) {
        g_atlas_last_tile = NULL;
        g_atlas_last_klen = -1;
    }
    if (slot->heap_key) {
        free(slot->heap_key);
        slot->heap_key = NULL;
    }
    slot->inline_key[0] = '\0';
    slot->key_len = 0;
    zan_cov_pool_put((void *)slot->tile.cov, zan_cov_round(slot->bytes));
    g_atlas_bytes -= slot->bytes;
    memset(slot, 0, sizeof(*slot));
}

static void zan_atlas_trim(void) {
    while (g_atlas_bytes > ZAN_ATLAS_BYTES) {
        zan_atlas_slot *oldest = NULL;
        for (int i = 0; i < ZAN_ATLAS_CAP; i++) {
            if (g_atlas[i].key_len <= 0) continue;
            if (!oldest || g_atlas[i].used < oldest->used) oldest = &g_atlas[i];
        }
        if (!oldest) return;
        zan_atlas_release(oldest);
    }
}

#define ZAN_ATLAS_COLD_STORES 64
#define ZAN_ATLAS_SWEEP 64

static void zan_atlas_sweep(void) {
    for (int i = 0; i < ZAN_ATLAS_CAP; i++) {
        zan_atlas_slot *e = &g_atlas[i];
        if (e->key_len <= 0) continue;
        if (g_atlas_stores - e->stamped >= ZAN_ATLAS_COLD_STORES) {
            zan_atlas_release(e);
            g_atlas_swept++;
        }
    }
}

static const zan_glyph_tile *zan_atlas_find(int kind, int size,
                                            const char *key, int key_len) {
    if (kind == g_atlas_last_kind && size == g_atlas_last_size &&
        key_len == g_atlas_last_klen && key_len <= ZAN_ATLAS_INLINE_KEY &&
        memcmp(key, g_atlas_last_key, (size_t)key_len) == 0 && g_atlas_last_tile != NULL) {
        g_atlas_hits++;
        return g_atlas_last_tile;
    }

    uint64_t h = zan_atlas_hash(kind, size, key, key_len);
    int base = (int)(h % ZAN_ATLAS_CAP);
    for (int i = 0; i < ZAN_ATLAS_PROBE; i++) {
        zan_atlas_slot *e = &g_atlas[(base + i) % ZAN_ATLAS_CAP];
        if (e->key_len <= 0) continue;
        const char *ekey = e->heap_key ? e->heap_key : e->inline_key;
        if (e->kind == kind && e->size == size && e->key_len == key_len
            && memcmp(ekey, key, (size_t)key_len) == 0) {
            e->used = ++g_atlas_clock;
            e->stamped = g_atlas_stores;
            g_atlas_hits++;
            if (key_len <= ZAN_ATLAS_INLINE_KEY) {
                g_atlas_last_kind = kind;
                g_atlas_last_size = size;
                g_atlas_last_klen = key_len;
                memcpy(g_atlas_last_key, key, (size_t)key_len);
                g_atlas_last_tile = &e->tile;
            }
            return &e->tile;
        }
    }
    g_atlas_misses++;
    return NULL;
}

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
        if (e->key_len <= 0) { victim = e; break; }
        if (!victim || e->used < victim->used) victim = e;
    }

    size_t bytes = (size_t)w * (size_t)h * (size_t)bpp;
    void *cov_copy = bytes ? zan_cov_pool_take(zan_cov_round(bytes)) : NULL;
    char *heap_key_copy = NULL;
    if (key_len > ZAN_ATLAS_INLINE_KEY) {
        heap_key_copy = (char *)malloc((size_t)key_len);
        if (!heap_key_copy || (bytes && !cov_copy)) {
            free(heap_key_copy);
            free(cov_copy);
            return NULL;
        }
        memcpy(heap_key_copy, key, (size_t)key_len);
    } else {
        if (bytes && !cov_copy) {
            return NULL;
        }
    }
    if (bytes) memcpy(cov_copy, cov, bytes);

    if (g_atlas_last_tile == &victim->tile) {
        g_atlas_last_tile = NULL;
        g_atlas_last_klen = -1;
    }
    zan_atlas_release(victim);
    if (heap_key_copy) {
        victim->heap_key = heap_key_copy;
    } else {
        memcpy(victim->inline_key, key, (size_t)key_len);
    }
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

    if (key_len <= ZAN_ATLAS_INLINE_KEY) {
        g_atlas_last_kind = kind;
        g_atlas_last_size = size;
        g_atlas_last_klen = key_len;
        memcpy(g_atlas_last_key, key, (size_t)key_len);
        g_atlas_last_tile = &victim->tile;
    }
    return &victim->tile;
}

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
