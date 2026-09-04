/* gui_runtime_glyph.c -- the glyph atlas: coverage tiles a platform font
 * engine produced, cached across frames and keyed by what was rasterized.
 *
 * Text is redrawn from scratch every frame (a scrolling editor repaints the
 * same tokens with only their Y moved), while rasterizing a glyph is by far the
 * most expensive thing in that frame: a GDI DIB round-trip or a FreeType
 * FT_LOAD_RENDER per character. So the font engine is asked once per distinct
 * (kind, size, key) and the coverage is kept; drawing text then only composites
 * cached tiles, which is also exactly what a GPU backend needs -- upload the
 * tile once, draw the run in one call (see zan_glyph_run in gui_backend.h).
 *
 * Colour is deliberately not part of the key: coverage is colourless, the draw
 * colour is applied when the run is composited, so the same tile serves every
 * colour the same string is ever drawn in.
 *
 * Part of the gui_runtime translation unit: #include'd by gui_runtime.c in
 * a fixed order; not compiled standalone (preprocessor state and static
 * linkage are shared across the parts).
 */

/* What the key means. A single glyph is the cacheable unit wherever the font
 * engine gives per-glyph coverage and metrics (FreeType); GDI's ClearType
 * coverage is per-channel and produced for a whole laid-out run, so there the
 * run itself is the tile. */
#define ZAN_TILE_RUN   0
#define ZAN_TILE_GLYPH 1

typedef struct {
    char    *key;      /* NULL marks a free slot */
    int      key_len;
    int      kind;
    int      size;
    uint64_t used;     /* LRU tick */
    uint64_t stamped;  /* store epoch of the last find/store (see sweep) */
    size_t   bytes;    /* payload accounted against the budget */
    zan_glyph_tile tile;
} zan_atlas_slot;

/* Open addressing with a short probe window, like the caches this replaces: a
 * miss costs the font engine anyway, so a slot collision evicting the coldest
 * of eight is cheaper than any chaining. The byte budget is what actually
 * bounds memory -- a run tile is as wide as a whole line of text, so a count
 * alone would not. */
#define ZAN_ATLAS_CAP   4096
#define ZAN_ATLAS_PROBE 8
/* Desktop workstations never notice the atlas; phones do. A CJK page holds
 * hundreds of whole-line run tiles and the desktop budget lands straight on
 * the device's PSS bill (gui_gallery sat at ~32MB atlas on Android). Sweep
 * evicts cold tiles either way -- a miss costs the font engine pass that
 * already had to happen -- so the budget trades resident memory for an
 * occasional re-raster, which is the right trade on a phone. */
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

/* Coverage-buffer pool. A churn UI -- counters, clocks, log lines -- cycles
 * tile allocations through store/sweep forever. Routing those buffers
 * through malloc/free made the CRT heap climb to a fragmentation high-water,
 * which read as "still leaking" under fast clicking even though every tile
 * was recycled. The pool keeps released coverage buffers and hands them back,
 * so a steady state performs zero heap traffic.
 *
 * Exact-size matching would almost never hit: on proportional fonts every
 * new string value rasterizes a few pixels wider or narrower than the last,
 * so allocations are rounded up to bucket steps first -- 1KB for the small
 * glyph tiles, 8KB for whole-line run tiles -- collapsing the recurring
 * sizes into a handful of buckets that recycle perfectly. The waste is
 * bounded by one step per tile. The pool is capped by count and total bytes;
 * overflow falls back to free() as before. */
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

/* A tile stops earning its bytes once no frame has found it for this many
 * subsequent stores. Every string a live frame still draws is re-found (and
 * re-stamped) every frame, while dynamic values -- counters, clocks, log and
 * chat lines -- are superseded and never drawn again. Stores only happen when
 * some text on screen CHANGES, so a threshold of a few dozen covers every
 * realistic lifetime of a displayed value; text that has been superseded this
 * many stores ago cannot come back (worst case: one re-rasterisation when a
 * long-idle screen is revisited). */
#define ZAN_ATLAS_COLD_STORES 64
/* Sweep cadence: amortises the full-slot scan over this many stores. */
#define ZAN_ATLAS_SWEEP 64

/* Drop every cold tile. Without this, a UI that renders ever-changing text
 * (the gallery's `count: N` labels, per-click) mints one run tile per
 * distinct value and holds all of them until the 32 MB hard cap, so RSS
 * climbs linearly with use. Sweeping recycles exactly the superseded values
 * and never touches text the current frames still draw. */
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

/* The cached tile for this key, or NULL when the caller has to rasterize. */
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

/* Take a copy of freshly rasterized coverage and return the cached tile, or
 * NULL if it could not be cached -- callers must handle NULL rather than draw
 * uncached, so a failed allocation degrades to "this text is not drawn" instead
 * of duplicating the composite path. `cov` holds w*h samples of `bpp` bytes;
 * w/h of 0 caches a glyph that has no coverage but still moves the pen (a
 * space), which is worth a slot precisely because it is so common. */
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
    victim->tile.cov = cov_copy;
    g_atlas_bytes += bytes;
    g_atlas_stores++;
    if ((g_atlas_stores & (ZAN_ATLAS_SWEEP - 1)) == 0) { zan_atlas_sweep(); }
    zan_atlas_trim();
    return &victim->tile;
}

/* A run is emitted in batches so a long line of text costs one backend call
 * per batch instead of one per glyph, without an unbounded buffer. */
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
