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
static uint64_t g_text_draw_cache_hits = 0;
static uint64_t g_text_draw_cache_misses = 0;
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
        g_text_draw_cache_hits = 0;
        g_text_draw_cache_misses = 0;
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
        case 5: return (i64)g_text_draw_cache_hits;
        case 6: return (i64)g_text_draw_cache_misses;
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

/* --- Rasterised text-run cache -------------------------------------------
 * zan_gui_draw_text is called many times per frame (one call per token/word)
 * and, while scrolling, with the *same* strings every frame -- only the Y
 * position moves. GDI-rendering each call (DIB alloc + TextOutW + GdiFlush)
 * then dominates CPU. Cache the white-on-black coverage bitmap keyed by
 * (text, size); the draw colour is applied at composite time, so it is not
 * part of the key and the same glyph run is reused for any colour. Entries
 * persist across frames, so scrolling only re-blits cached masks. */
typedef struct {
    char *text;   /* UTF-8 key; NULL marks an empty slot */
    int   size;
    int   tw, th;
    u32  *bits;   /* tw*th ARGB coverage straight from GDI */
    uint64_t used;   /* LRU tick */
} zan_text_cache_t;

#define ZAN_TEXT_CACHE_CAP 1024
#define ZAN_TEXT_CACHE_PROBE 8
static zan_text_cache_t g_tcache[ZAN_TEXT_CACHE_CAP];
static uint64_t g_tcache_clock = 0;

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

/* Returns a cache entry whose bits hold the coverage mask, or NULL on error. */
static zan_text_cache_t *zan_text_cache_get(const char *text, int size) {
    uint64_t h = zan_text_hash(text, size);
    int base = (int)(h % ZAN_TEXT_CACHE_CAP);
    zan_text_cache_t *victim = NULL;
    for (int i = 0; i < ZAN_TEXT_CACHE_PROBE; i++) {
        zan_text_cache_t *e = &g_tcache[(base + i) % ZAN_TEXT_CACHE_CAP];
        if (e->text && e->size == size && strcmp(e->text, text) == 0) {
            e->used = ++g_tcache_clock;
            if (g_text_stats_enabled) { g_text_draw_cache_hits++; }
            return e;
        }
        if (!e->text && !victim) victim = e;
    }
    if (!victim) {
        uint64_t best = ~0ULL;
        for (int i = 0; i < ZAN_TEXT_CACHE_PROBE; i++) {
            zan_text_cache_t *e = &g_tcache[(base + i) % ZAN_TEXT_CACHE_CAP];
            if (e->used <= best) { best = e->used; victim = e; }
        }
    }

    /* Miss: rasterise white text on black to capture the coverage mask. */
    if (g_text_stats_enabled) { g_text_draw_cache_misses++; }
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

    u32 *copy = (u32 *)malloc((size_t)(tw * th) * sizeof(u32));
    if (copy) memcpy(copy, dbits, (size_t)(tw * th) * sizeof(u32));

    SelectObject(g_text_dc, old_bmp);
    SelectObject(g_text_dc, old_font);
    DeleteObject(hbmp);
    free(wtext);
    if (!copy) return NULL;

    if (victim->text) { free(victim->text); victim->text = NULL; }
    if (victim->bits) { free(victim->bits); victim->bits = NULL; }
    size_t klen = strlen(text) + 1;
    victim->text = (char *)malloc(klen);
    if (!victim->text) { free(copy); return NULL; }
    memcpy(victim->text, text, klen);
    victim->size = size;
    victim->tw = tw;
    victim->th = th;
    victim->bits = copy;
    victim->used = ++g_tcache_clock;
    return victim;
}

EXPORT void zan_gui_draw_text(
    i32 surface_id, i32 x, i32 y, const char *text, i32 color, i32 font_size) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !text || !*text) return;

    int size = (int)font_size;
    if (size < 8) size = 8;

    ensure_text_dc();
    uint64_t t0 = 0;
    if (g_text_stats_enabled) {
        g_text_draw_calls++;
        if (size >= 0 && size < 64) { g_text_size_calls[size]++; }
        t0 = text_qpc_us();
    }

    zan_text_cache_t *e = zan_text_cache_get(text, size);
    if (!e || !e->bits) {
        if (g_text_stats_enabled) {
            g_text_draw_us += text_qpc_us() - t0;
        }
        return;
    }
    int tw = e->tw;
    int th = e->th;
    u32 *src = e->bits;

    /* Extract color components */
    u32 cr = ((u32)color >> 16) & 0xFF;
    u32 cg = ((u32)color >> 8) & 0xFF;
    u32 cb = (u32)color & 0xFF;
    u32 ca = ((u32)color >> 24) & 0xFF;
    if (ca == 0) ca = 255; /* treat 0 alpha as opaque for text */

    /* Composite cached coverage using per-channel ClearType AA. Clip is
     * resolved to pixel ranges once here instead of a per-pixel branch, since
     * this loop runs over every glyph pixel on every repaint. */
    int ox = (int)x, oy = (int)y;
    int py0 = s->clip_y0 - oy; if (py0 < 0) py0 = 0;
    int py1 = s->clip_y1 - oy; if (py1 > th) py1 = th;
    int px0 = s->clip_x0 - ox; if (px0 < 0) px0 = 0;
    int px1 = s->clip_x1 - ox; if (px1 > tw) px1 = tw;
    for (int py = py0; py < py1; py++) {
        const u32 *srow = src + (size_t)py * tw;
        int dst_row = (oy + py) * s->stride + ox;
        for (int px = px0; px < px1; px++) {
            u32 sp = srow[px];
            if ((sp & 0x00FFFFFFu) == 0) continue;
            u32 sr = (sp >> 16) & 0xFF;
            u32 sg = (sp >> 8) & 0xFF;
            u32 sb = sp & 0xFF;
            /* Per-channel blend for subpixel AA */
            int idx = dst_row + px;
            u32 dp = s->pixels[idx];
            u32 dr = (dp >> 16) & 0xFF;
            u32 dg = (dp >> 8) & 0xFF;
            u32 db = dp & 0xFF;
            u32 ar = sr * ca / 255;
            u32 ag = sg * ca / 255;
            u32 ab = sb * ca / 255;
            u32 or_ = (cr * ar + dr * (255 - ar)) / 255;
            u32 og = (cg * ag + dg * (255 - ag)) / 255;
            u32 ob = (cb * ab + db * (255 - ab)) / 255;
            s->pixels[idx] = (255u << 24) | (or_ << 16) | (og << 8) | ob;
        }
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


#endif /* _WIN32 */
