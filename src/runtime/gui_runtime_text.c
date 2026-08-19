/* gui_runtime_text.c -- text rendering (Win32 GDI / Linux Xft + fallback).
 *
 * The Win32 window shell (window class, WndProc, event queue, presentation,
 * clipboard, IME, glass) lives in Zan: stdlib/Gui/Win32Shell.zan.
 *
 * Part of the gui_runtime translation unit: #include'd by gui_runtime.c in
 * a fixed order; not compiled standalone (preprocessor state and static
 * linkage are shared across the parts).
 */

/* ========================================================================
 * Text Rendering — Platform-specific (Win32: GDI, Linux: Xft/fallback)
 * ======================================================================== */

#ifdef _WIN32

static HDC g_text_dc = NULL;
static HFONT g_fonts[16]; /* cached fonts by size index */
static int g_font_count = 0;
static int g_text_stats_enabled = 0;
static uint64_t g_text_draw_calls = 0;
static uint64_t g_text_measure_calls = 0;
static uint64_t g_text_height_calls = 0;
static uint64_t g_text_font_creates = 0;
static uint64_t g_text_font_hits = 0;
static uint64_t g_text_measure_cache_hits = 0;
static uint64_t g_text_measure_cache_misses = 0;
static uint64_t g_text_draw_us = 0;
static uint64_t g_text_measure_us = 0;
static uint64_t g_text_height_us = 0;
static uint64_t g_text_size_calls[64];
static LARGE_INTEGER g_text_qpc0;
static LARGE_INTEGER g_text_qpc_freq;

static uint64_t text_qpc_us(void) {
    LARGE_INTEGER now;
    if (!g_text_qpc_freq.QuadPart) {
        QueryPerformanceFrequency(&g_text_qpc_freq);
    }
    QueryPerformanceCounter(&now);
    if (!g_text_qpc0.QuadPart) { g_text_qpc0 = now; }
    return (uint64_t)((now.QuadPart - g_text_qpc0.QuadPart) * 1000000
                      / g_text_qpc_freq.QuadPart);
}

EXPORT void zan_gui_text_stat_enable(i32 enabled) {
    if (enabled && !g_text_stats_enabled) {
        g_text_draw_calls = 0;
        g_text_measure_calls = 0;
        g_text_height_calls = 0;
        g_text_font_creates = 0;
        g_text_font_hits = 0;
        g_atlas_hits = 0;
        g_atlas_misses = 0;
        g_text_measure_cache_hits = 0;
        g_text_measure_cache_misses = 0;
        g_text_draw_us = 0;
        g_text_measure_us = 0;
        g_text_height_us = 0;
        memset(g_text_size_calls, 0, sizeof(g_text_size_calls));
    }
    g_text_stats_enabled = enabled ? 1 : 0;
}

EXPORT i64 zan_gui_text_stat_read(i32 idx) {
    switch (idx) {
        case 0: return (i64)g_text_draw_calls;
        case 1: return (i64)g_text_measure_calls;
        case 2: return (i64)g_text_height_calls;
        case 3: return (i64)g_text_font_creates;
        case 4: return (i64)g_text_font_hits;
        case 5: return (i64)g_atlas_hits;
        case 6: return (i64)g_atlas_misses;
        case 7: return (i64)g_text_measure_cache_hits;
        case 8: return (i64)g_text_measure_cache_misses;
        case 9: return (i64)g_text_draw_us;
        case 10: return (i64)g_text_measure_us;
        case 11: return (i64)g_text_height_us;
        default:
            if (idx >= 12 && idx < 76) {
                return (i64)g_text_size_calls[idx - 12];
            }
            return 0;
    }
}

static void ensure_text_dc(void) {
    if (!g_text_dc) {
        g_text_dc = CreateCompatibleDC(NULL);
        SetBkMode(g_text_dc, TRANSPARENT);
    }
}

static int g_font_sizes[16];

static HFONT get_or_create_font(int size) {
    /* Cache by exact size match */
    for (int i = 0; i < 16; i++) {
        if (g_font_sizes[i] == size && g_fonts[i]) {
            if (g_text_stats_enabled) { g_text_font_hits++; }
            return g_fonts[i];
        }
    }
    /* Find empty slot */
    int slot = -1;
    for (int i = 0; i < 16; i++) {
        if (!g_fonts[i]) { slot = i; break; }
    }
    if (slot < 0) slot = 15; /* reuse last */

    if (g_fonts[slot]) DeleteObject(g_fonts[slot]);
    g_fonts[slot] = CreateFontW(
        -size, 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"Segoe UI"
    );
    if (g_text_stats_enabled) { g_text_font_creates++; }
    g_font_sizes[slot] = size;
    return g_fonts[slot];
}

static uint64_t zan_text_hash(const char *s, int size) {
    uint64_t h = 1469598103934665603ULL;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        h ^= (uint64_t)(*p);
        h *= 1099511628211ULL;
    }
    h ^= (uint64_t)(unsigned)size;
    h *= 1099511628211ULL;
    return h;
}

/* The coverage tile of a whole text run, rasterised by GDI on the first use of
 * that (text, size) and cached in the glyph atlas afterwards.
 *
 * The run -- not the glyph -- is the unit here: ClearType coverage is
 * per-channel and produced for the string GDI laid out itself, so caching
 * glyphs separately and placing them at integer advances would change how
 * every label in the app looks.
 *
 * White on black is what makes the result a mask: the draw colour is applied
 * when the run is composited, so it is not part of the key and one tile serves
 * every colour the same string is drawn in. */
static const zan_glyph_tile *win_run_tile(const char *text, int size) {
    int key_len = (int)strlen(text);
    const zan_glyph_tile *cached =
        zan_atlas_find(ZAN_TILE_RUN, size, text, key_len);
    if (cached) return cached;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *wtext = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wtext) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, wlen);
    int text_len = wlen - 1;

    HFONT font = get_or_create_font(size);
    HFONT old_font = (HFONT)SelectObject(g_text_dc, font);
    SIZE ts;
    GetTextExtentPoint32W(g_text_dc, wtext, text_len, &ts);
    int tw = ts.cx + 2;
    int th = ts.cy + 2;
    if (tw <= 0 || th <= 0) {
        free(wtext); SelectObject(g_text_dc, old_font); return NULL;
    }

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = tw;
    bmi.bmiHeader.biHeight = -th;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void *dbits = NULL;
    HBITMAP hbmp = CreateDIBSection(g_text_dc, &bmi, DIB_RGB_COLORS, &dbits, NULL, 0);
    if (!hbmp) { free(wtext); SelectObject(g_text_dc, old_font); return NULL; }
    HBITMAP old_bmp = (HBITMAP)SelectObject(g_text_dc, hbmp);
    memset(dbits, 0, (size_t)(tw * th * 4));
    SetTextColor(g_text_dc, RGB(255, 255, 255));
    TextOutW(g_text_dc, 0, 0, wtext, text_len);
    GdiFlush();

    const zan_glyph_tile *tile =
        zan_atlas_store(ZAN_TILE_RUN, size, text, key_len,
                        tw, th, 0, 0, (int)ts.cx, dbits, 4);

    SelectObject(g_text_dc, old_bmp);
    SelectObject(g_text_dc, old_font);
    DeleteObject(hbmp);
    free(wtext);
    return tile;
}

EXPORT void zan_gui_draw_text(
    i32 surface_id, i32 x, i32 y, const char *text, i32 color, i32 font_size) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !text || !*text) return;
    ZAN_BE(s, draw_text, s, (int)x, (int)y, text, (u32)color, (int)font_size);

    int size = (int)font_size;
    if (size < 8) size = 8;

    ensure_text_dc();
    uint64_t t0 = 0;
    if (g_text_stats_enabled) {
        g_text_draw_calls++;
        if (size >= 0 && size < 64) { g_text_size_calls[size]++; }
        t0 = text_qpc_us();
    }

    const zan_glyph_tile *tile = win_run_tile(text, size);
    if (tile) {
        zan_glyph_item item;
        item.tile = tile;
        item.x = (int)x + tile->left;
        item.y = (int)y + tile->top;
        zan_glyph_run run;
        run.color = (u32)color;
        run.count = 1;
        run.items = &item;
        ZAN_IMPL(s, glyph_run)->glyph_run(s, &run);
    }
    if (g_text_stats_enabled) {
        g_text_draw_us += text_qpc_us() - t0;
    }
}

/* --- Measured-width cache -------------------------------------------------
 * zan_gui_measure_text runs a GDI GetTextExtentPoint32W round-trip plus two
 * MultiByteToWideChar and a malloc/free on every call, and layout / caret /
 * selection / syntax-highlight code calls it hundreds of times per frame with
 * the same strings (one per visible token, every frame while scrolling). Cache
 * width by (text, size) so a steady frame measures nothing and allocates
 * nothing -- the dominant per-frame CPU + allocation churn otherwise. */
typedef struct {
    char    *text;   /* UTF-8 key; NULL marks an empty slot */
    int      size;
    int      width;
    uint64_t used;   /* LRU tick */
} zan_measure_cache_t;

#define ZAN_MEAS_CACHE_CAP 2048
#define ZAN_MEAS_CACHE_PROBE 8
static zan_measure_cache_t g_mcache[ZAN_MEAS_CACHE_CAP];
static uint64_t g_mcache_clock = 0;

static int measure_text_gdi(const char *text, int size) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *wtext = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wtext) return 0;
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, wlen);
    HFONT font = get_or_create_font(size);
    HFONT old_font = (HFONT)SelectObject(g_text_dc, font);
    SIZE text_size;
    GetTextExtentPoint32W(g_text_dc, wtext, wlen - 1, &text_size);
    SelectObject(g_text_dc, old_font);
    free(wtext);
    return (int)text_size.cx;
}

EXPORT i32 zan_gui_measure_text(const char *text, i32 font_size) {
    if (!text || !*text) return 0;
    int size = (int)font_size;
    if (size < 8) size = 8;

    ensure_text_dc();
    uint64_t t0 = 0;
    if (g_text_stats_enabled) {
        g_text_measure_calls++;
        if (size >= 0 && size < 64) { g_text_size_calls[size]++; }
        t0 = text_qpc_us();
    }

    uint64_t h = zan_text_hash(text, size);
    int base = (int)(h % ZAN_MEAS_CACHE_CAP);
    zan_measure_cache_t *victim = NULL;
    uint64_t best = ~0ULL;
    for (int i = 0; i < ZAN_MEAS_CACHE_PROBE; i++) {
        zan_measure_cache_t *e = &g_mcache[(base + i) % ZAN_MEAS_CACHE_CAP];
        if (e->text && e->size == size && strcmp(e->text, text) == 0) {
            e->used = ++g_mcache_clock;
            if (g_text_stats_enabled) {
                g_text_measure_cache_hits++;
                g_text_measure_us += text_qpc_us() - t0;
            }
            return (i64)e->width;
        }
        if (!e->text) { if (!victim || best != 0) { victim = e; best = 0; } }
        else if (e->used < best) { best = e->used; victim = e; }
    }

    int w = measure_text_gdi(text, size);
    if (g_text_stats_enabled) {
        g_text_measure_cache_misses++;
    }

    if (victim) {
        if (victim->text) { free(victim->text); victim->text = NULL; }
        size_t klen = strlen(text) + 1;
        victim->text = (char *)malloc(klen);
        if (victim->text) {
            memcpy(victim->text, text, klen);
            victim->size = size;
            victim->width = w;
            victim->used = ++g_mcache_clock;
        }
    }
    if (g_text_stats_enabled) {
        g_text_measure_us += text_qpc_us() - t0;
    }
    return (i64)w;
}

/* Font height depends only on size; only a handful of sizes are ever used. */
static int g_fh_size[16];
static int g_fh_val[16];
static int g_fh_count = 0;

EXPORT i32 zan_gui_font_height(i32 font_size) {
    int size = (int)font_size;
    if (size < 8) size = 8;
    uint64_t t0 = 0;
    if (g_text_stats_enabled) {
        g_text_height_calls++;
        if (size >= 0 && size < 64) { g_text_size_calls[size]++; }
        t0 = text_qpc_us();
    }

    for (int i = 0; i < g_fh_count; i++) {
        if (g_fh_size[i] == size) {
            if (g_text_stats_enabled) {
                g_text_height_us += text_qpc_us() - t0;
            }
            return (i64)g_fh_val[i];
        }
    }

    ensure_text_dc();
    HFONT font = get_or_create_font(size);
    HFONT old_font = (HFONT)SelectObject(g_text_dc, font);
    TEXTMETRICW tm;
    GetTextMetricsW(g_text_dc, &tm);
    SelectObject(g_text_dc, old_font);

    int val = (int)tm.tmHeight;
    if (g_fh_count < 16) {
        g_fh_size[g_fh_count] = size;
        g_fh_val[g_fh_count] = val;
        g_fh_count = g_fh_count + 1;
    }
    if (g_text_stats_enabled) {
        g_text_height_us += text_qpc_us() - t0;
    }
    return (i64)val;
}


#else /* !_WIN32 */

/* The text profiler counts GDI calls and its caches, so only the Win32 text
 * path has anything to report. Canvas.TextStat* is declared unconditionally
 * in Zan (stdlib/Gui/Render.zan), so every program touching it -- including
 * the GUI conformance cases -- needs the symbols to exist elsewhere too;
 * here they report "profiler off / nothing measured". */
EXPORT void zan_gui_text_stat_enable(i32 enabled) { (void)enabled; }

EXPORT i64 zan_gui_text_stat_read(i32 idx) { (void)idx; return 0; }

#endif /* _WIN32 */
