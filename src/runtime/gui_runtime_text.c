/* gui_runtime_text */

/* Text Rendering — Platform-specific (Win32: GDI, Linux: Xft/fallback) */

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
        /* glyph-atlas internals (gui_runtime_glyph */
        case 76: return (i64)g_atlas_bytes;
        case 77: return (i64)g_atlas_stores;
        case 78: return (i64)g_atlas_swept;
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
static int g_font_bold[16];   /* 1 = FW_BOLD slot; the table is keyed by (size, weight) */
/* 内部辅助逻辑 */
static wchar_t g_font_family[64] = L"Segoe UI";
static int g_font_family_env_done = 0;
static int g_font_family_explicit = 0;
/* 内部辅助逻辑 */
static int g_os2_asc = 0;    /* usWinAscent, font units */
static int g_os2_desc = 0;   /* usWinDescent, font units */
static int g_os2_upem = 0;   /* unitsPerEm ('head'), 0 = fall back to GDI */
static int g_os2_tried = 0;

static HFONT get_or_create_font(int size, int bold) {
    /* Cache by exact (size, weight) match */
    for (int i = 0; i < 16; i++) {
        if (g_font_sizes[i] == size && g_font_bold[i] == bold && g_fonts[i]) {
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

    /* 内部辅助实现 */
    if (!g_font_family_explicit && !g_font_family_env_done) {
        g_font_family_env_done = 1;
        wchar_t env[64];
        DWORD n = GetEnvironmentVariableW(L"ZAN_GUI_FONT_FACE", env, 64);
        if (n > 0 && n < 64) {
            memcpy(g_font_family, env, (size_t)n * sizeof(wchar_t));
            g_font_family[n] = 0;
        }
    }

    if (g_fonts[slot]) DeleteObject(g_fonts[slot]);
    g_fonts[slot] = CreateFontW(
        -size, 0, 0, 0,
        bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        g_font_family
    );
    if (g_text_stats_enabled) { g_text_font_creates++; }
    g_font_sizes[slot] = size;
    g_font_bold[slot] = bold;
    return g_fonts[slot];
}

/* Programmatic face selection (games: a rounder face matching the art) */
EXPORT i32 zan_gui_set_text_family(const char *utf8_family) {
    if (!utf8_family || !*utf8_family) return 0;
    wchar_t want[64];
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8_family, -1, want, 64);
    if (n <= 0 || n > 64) return 0;
    memcpy(g_font_family, want, (size_t)n * sizeof(wchar_t));
    g_font_family_explicit = 1;
    /* Drop cached HFONTs so the next draw rebuilds with the new face */
    for (int i = 0; i < 16; i++) {
        if (g_fonts[i]) { DeleteObject(g_fonts[i]); g_fonts[i] = NULL; }
        g_font_sizes[i] = 0;
        g_font_bold[i] = 0;
    }
    /* OS/2 vertical metrics belong to the face: force a reload. */
    g_os2_tried = 0;
    g_os2_upem = 0;
    return 1;
}

/* 内部辅助逻辑 */
EXPORT i32 zan_gui_font_face_matches(const char *utf8_family) {
    if (!utf8_family || !*utf8_family) return 0;
    wchar_t want[64];
    if (!MultiByteToWideChar(CP_UTF8, 0, utf8_family, -1, want, 64)) return 0;
    ensure_text_dc();
    HFONT probe = CreateFontW(
        16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, want);
    if (!probe) return 0;
    HFONT old = (HFONT)SelectObject(g_text_dc, probe);
    wchar_t got[LF_FACESIZE];
    int ok = GetTextFaceW(g_text_dc, LF_FACESIZE, got);
    SelectObject(g_text_dc, old);
    DeleteObject(probe);
    if (!ok) return 0;
    int i = 0, j = 0;
    while (got[i] && want[j]) {
        wchar_t a = got[i], b = want[j];
        if (a >= L'A' && a <= L'Z') { a = a - L'A' + L'a'; }
        if (b >= L'A' && b <= L'Z') { b = b - L'A' + L'a'; }
        if (a != b) return 0;
        i++; j++;
    }
    return (got[i] == 0 && want[j] == 0) ? 1 : 0;
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

/* Runs are cached per (text, size, weight) */
#define ZAN_RUN_BOLD_FLAG 0x10000
static int run_key_size(int size, int bold) {
    return bold ? (size | ZAN_RUN_BOLD_FLAG) : size;
}

/* 内部辅助实现 */
extern int zan_dw_init(void);
extern int zan_dw_text_width(const wchar_t *w, int len,
    const wchar_t *family, float size, int bold);
extern int zan_dw_render(const wchar_t *w, int len,
    const wchar_t *family, float size, int bold,
    unsigned char **out_px, int *out_w, int *out_h, int *out_advance);

/* 内部辅助实现 */
static int win_cp_is_emoji(unsigned int cp) {
    if (cp >= 0x1F000 && cp <= 0x1FFFF) return 1;   /* SMP emoji + tags */
    if (cp >= 0xD83C && cp <= 0xD83E) return 1;     /* SMP high surrogates */
    if (cp >= 0x2600 && cp <= 0x27BF) return 1;     /* misc symbols+dingbats */
    if (cp >= 0x2B00 && cp <= 0x2BFF) return 1;     /* stars/arrows */
    if (cp >= 0xFE00 && cp <= 0xFE0F) return 1;     /* variation selectors */
    if (cp == 0x200D) return 1;                     /* ZWJ sequences */
    if (cp == 0x20E3) return 1;                     /* keycap combine */
    if (cp >= 0x2190 && cp <= 0x21AA) return 1;     /* arrows/hands */
    if (cp >= 0x231A && cp <= 0x231B) return 1;
    if (cp >= 0x23E9 && cp <= 0x23FA) return 1;
    if (cp == 0x2139 || cp == 0x24C2) return 1;
    if (cp >= 0x25AA && cp <= 0x25FE) return 1;
    if (cp == 0x2934 || cp == 0x2935) return 1;
    if (cp == 0x3030 || cp == 0x303D) return 1;
    if (cp == 0x3297 || cp == 0x3299) return 1;
    return 0;
}

/* 内部辅助逻辑 */
static int win_run_has_emoji(const wchar_t *w, int len) {
    for (int i = 0; i < len; i++) {
        if (win_cp_is_emoji((unsigned int)w[i])) return 1;
    }
    return 0;
}

/* 内部辅助逻辑 */
static int win_run_has_missing(int size, int bold,
                               const wchar_t *w, int len) {
    HFONT font = get_or_create_font(size, bold);
    HFONT old = (HFONT)SelectObject(g_text_dc, font);
    WORD *gi = (WORD *)malloc((size_t)len * sizeof(WORD));
    int missing = 0;
    if (gi) {
        if (GetGlyphIndicesW(g_text_dc, w, len, gi,
                             GGI_MARK_NONEXISTING_GLYPHS)) {
            for (int i = 0; i < len; i++) {
                if (gi[i] == 0xFFFF) { missing = 1; break; }
            }
        }
        free(gi);
    }
    SelectObject(g_text_dc, old);
    return missing;
}

/* 内部辅助逻辑 */
static const zan_glyph_tile *win_dwrite_tile(const char *text, int size,
                                             int bold) {
    int key_len = (int)strlen(text);
    int ksize = run_key_size(size, bold);
    const zan_glyph_tile *cached =
        zan_atlas_find(ZAN_TILE_RUN, ksize, text, key_len);
    if (cached) return cached;

    ensure_text_dc();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *wtext = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wtext) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, wlen);
    HFONT font = get_or_create_font(size, bold);
    HFONT old_font = (HFONT)SelectObject(g_text_dc, font);
    wchar_t family[LF_FACESIZE];
    GetTextFaceW(g_text_dc, LF_FACESIZE, family);
    SelectObject(g_text_dc, old_font);

    unsigned char *px = NULL;
    int tw = 0, th = 0, adv = 0;
    int ok = zan_dw_render(wtext, wlen - 1, family, (float)size,
                           bold ? 1 : 0, &px, &tw, &th, &adv);
    free(wtext);
    if (!ok || !px) return NULL;

    const zan_glyph_tile *tile = zan_atlas_store(ZAN_TILE_RUN, ksize, text,
        key_len, tw, th, 0, 0, adv, px, 4);
    if (tile) {
        ((zan_glyph_tile *)tile)->flags = ZAN_TILE_RGBA;
    }
    free(px);
    return tile;
}

static int win_dw_text_width(const wchar_t *w, int len, int size, int bold) {
    ensure_text_dc();
    HFONT font = get_or_create_font(size, bold);
    HFONT old_font = (HFONT)SelectObject(g_text_dc, font);
    wchar_t family[LF_FACESIZE];
    GetTextFaceW(g_text_dc, LF_FACESIZE, family);
    SelectObject(g_text_dc, old_font);
    return zan_dw_text_width(w, len, family, (float)size, bold ? 1 : 0);
}

/* 内部辅助实现 */
static const zan_glyph_tile *win_run_tile(const char *text, int size, int bold) {
    int key_len = (int)strlen(text);
    int ksize = run_key_size(size, bold);
    const zan_glyph_tile *cached =
        zan_atlas_find(ZAN_TILE_RUN, ksize, text, key_len);
    if (cached) return cached;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *wtext = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wtext) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, wlen);
    int text_len = wlen - 1;

    HFONT font = get_or_create_font(size, bold);
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
        zan_atlas_store(ZAN_TILE_RUN, ksize, text, key_len,
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

    const zan_glyph_tile *tile = win_run_tile(text, size, 0);
    if (!tile) {
        /* 内部辅助逻辑 */
        tile = win_dwrite_tile(text, size, 0);
    }
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

/* Bold variant of zan_gui_draw_text */
EXPORT void zan_gui_draw_text_bold(
    i32 surface_id, i32 x, i32 y, const char *text, i32 color, i32 font_size) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !text || !*text) return;

    int size = (int)font_size;
    if (size < 8) size = 8;

    ensure_text_dc();
    const zan_glyph_tile *tile = win_run_tile(text, size, 1);
    if (!tile) tile = win_dwrite_tile(text, size, 1);
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
}

/* 内部辅助实现 */
EXPORT void zan_gui_draw_text_rot(
    i32 surface_id, i32 x, i32 y, const char *text, i32 color, i32 font_size,
    i32 angle_deg) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !text || !*text) return;
    int angle = (int)angle_deg;
    if (angle < -90) angle = -90;
    if (angle > 90) angle = 90;
    if (angle == 0) {
        zan_gui_draw_text(surface_id, x, y, text, color, font_size);
        return;
    }

    int size = (int)font_size;
    if (size < 8) size = 8;

    int klen = (int)strlen(text) + 3;
    char *key = (char *)malloc((size_t)klen);
    if (!key) return;
    key[0] = (char)0xFF;
    key[1] = (char)(angle & 0xFF);
    key[2] = (char)((angle >> 8) & 0xFF);
    memcpy(key + 3, text, (size_t)klen - 3);

    ensure_text_dc();
    uint64_t t0 = 0;
    if (g_text_stats_enabled) {
        g_text_draw_calls++;
        if (size >= 0 && size < 64) { g_text_size_calls[size]++; }
        t0 = text_qpc_us();
    }

    const zan_glyph_tile *tile = zan_atlas_find(ZAN_TILE_RUN, size, key, klen);
    if (!tile) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
        wchar_t *wtext = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
        if (!wtext) { free(key); return; }
        MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, wlen);
        int text_len = wlen - 1;

        HFONT font = get_or_create_font(size, 0);
        HFONT old_font = (HFONT)SelectObject(g_text_dc, font);
        SIZE ts;
        GetTextExtentPoint32W(g_text_dc, wtext, text_len, &ts);
        int w = ts.cx + 2;
        int h = ts.cy + 2;

        double rad = (double)angle * 3.14159265358979323846 / 180.0;
        double cs = cos(rad), sn = sin(rad);
        /* Rotated bbox of the padded box [0 */
        double cx0 = 0.0, cx1 = cs * w, cx2 = -sn * h, cx3 = cs * w - sn * h;
        double cy0 = 0.0, cy1 = sn * w, cy2 = cs * h, cy3 = sn * w + cs * h;
        double mnx = cx0;
        if (cx1 < mnx) mnx = cx1;
        if (cx2 < mnx) mnx = cx2;
        if (cx3 < mnx) mnx = cx3;
        double mxx = cx0;
        if (cx1 > mxx) mxx = cx1;
        if (cx2 > mxx) mxx = cx2;
        if (cx3 > mxx) mxx = cx3;
        double mny = cy0;
        if (cy1 < mny) mny = cy1;
        if (cy2 < mny) mny = cy2;
        if (cy3 < mny) mny = cy3;
        double mxy = cy0;
        if (cy1 > mxy) mxy = cy1;
        if (cy2 > mxy) mxy = cy2;
        if (cy3 > mxy) mxy = cy3;
        int left = (int)floor(mnx) - 1;
        int top = (int)floor(mny) - 1;
        int bw = (int)ceil(mxx) - (int)floor(mnx) + 2;
        int bh = (int)ceil(mxy) - (int)floor(mny) + 2;

        if (bw <= 0 || bh <= 0 || bw > 16384 || bh > 16384) {
            free(wtext);
            SelectObject(g_text_dc, old_font);
            free(key);
            return;
        }

        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = bw;
        bmi.bmiHeader.biHeight = -bh;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void *dbits = NULL;
        HBITMAP hbmp = CreateDIBSection(g_text_dc, &bmi, DIB_RGB_COLORS,
                                        &dbits, NULL, 0);
        if (!hbmp) {
            free(wtext);
            SelectObject(g_text_dc, old_font);
            free(key);
            return;
        }
        HBITMAP old_bmp = (HBITMAP)SelectObject(g_text_dc, hbmp);
        memset(dbits, 0, (size_t)bw * (size_t)bh * 4);

        /* 内部辅助逻辑 */
        XFORM xf;
        xf.eM11 = (FLOAT)cs;   xf.eM12 = (FLOAT)sn;
        xf.eM21 = (FLOAT)-sn;  xf.eM22 = (FLOAT)cs;
        xf.eDx = (FLOAT)(-mnx + 1.0);
        xf.eDy = (FLOAT)(-mny + 1.0);
        SetGraphicsMode(g_text_dc, GM_ADVANCED);
        SetWorldTransform(g_text_dc, &xf);
        SetTextColor(g_text_dc, RGB(255, 255, 255));
        SetBkMode(g_text_dc, TRANSPARENT);
        TextOutW(g_text_dc, 0, 0, wtext, text_len);
        GdiFlush();
        ModifyWorldTransform(g_text_dc, NULL, MWT_IDENTITY);
        SetGraphicsMode(g_text_dc, GM_COMPATIBLE);

        tile = zan_atlas_store(ZAN_TILE_RUN, size, key, klen,
                               bw, bh, left, top, (int)ts.cx, dbits, 4);
        SelectObject(g_text_dc, old_bmp);
        DeleteObject(hbmp);
        SelectObject(g_text_dc, old_font);
        free(wtext);
    }
    free(key);
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

/* 内部辅助实现 */
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
    HFONT font = get_or_create_font(size, 0);
    HFONT old_font = (HFONT)SelectObject(g_text_dc, font);
    SIZE text_size;
    GetTextExtentPoint32W(g_text_dc, wtext, wlen - 1, &text_size);
    int missing = win_run_has_emoji(wtext, wlen - 1);
    SelectObject(g_text_dc, old_font);
    /* Emoji runs: GDI measures the */
    int w = 0;
    if (missing) {
        w = win_dw_text_width(wtext, wlen - 1, size, 0);
    }
    free(wtext);
    if (w > 0) return w;
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

/* Font height depends only on size; only a handful of sizes are ever used */
static int g_fh_size[16];
static int g_fh_val[16];
static int g_fh_count = 0;

static int g_fa_size[16];
static int g_fa_val[16];
static int g_fa_count = 0;

static void load_os2_vertical_metrics(void) {
    if (g_os2_tried) { return; }
    g_os2_tried = 1;
    ensure_text_dc();
    HFONT font = get_or_create_font(16, 0);
    HFONT old_font = (HFONT)SelectObject(g_text_dc, font);
    uint8_t buf[2];
    /* 内部辅助实现 */
    if (GetFontData(g_text_dc, 0x64616568, 18, buf, 2) == 2) {
        g_os2_upem = (int)((buf[0] << 8) | buf[1]);
    }
    if (GetFontData(g_text_dc, 0x322F534F, 74, buf, 2) == 2) {
        g_os2_asc = (int)((buf[0] << 8) | buf[1]);
    }
    if (GetFontData(g_text_dc, 0x322F534F, 76, buf, 2) == 2) {
        g_os2_desc = (int)((buf[0] << 8) | buf[1]);
    }
    SelectObject(g_text_dc, old_font);
    if (g_os2_upem <= 0 || g_os2_asc <= 0 || g_os2_desc <= 0) {
        g_os2_upem = 0;
    }
}

/* 内部辅助逻辑 */
EXPORT i32 zan_gui_font_ascent(i32 font_size) {
    int size = (int)font_size;
    if (size < 8) size = 8;
    load_os2_vertical_metrics();
    if (g_os2_upem > 0) {
        int v = (size * g_os2_asc) / g_os2_upem;
        return (i64)(v > 0 ? v : 1);
    }
    for (int i = 0; i < g_fa_count; i++) {
        if (g_fa_size[i] == size) { return (i64)g_fa_val[i]; }
    }
    ensure_text_dc();
    HFONT font = get_or_create_font(size, 0);
    HFONT old_font = (HFONT)SelectObject(g_text_dc, font);
    TEXTMETRICW tm;
    GetTextMetricsW(g_text_dc, &tm);
    SelectObject(g_text_dc, old_font);
    int val = (int)tm.tmAscent;
    if (g_fa_count < 16) {
        g_fa_size[g_fa_count] = size;
        g_fa_val[g_fa_count] = val;
        g_fa_count = g_fa_count + 1;
    }
    return (i64)val;
}

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

    load_os2_vertical_metrics();
    int val;
    if (g_os2_upem > 0) {
        /* Chrome content box: floor(size*usWinAscent/upem) + floor(size*usWinDescent/upem) */
        val = (size * g_os2_asc) / g_os2_upem
            + (size * g_os2_desc) / g_os2_upem;
    } else {
        ensure_text_dc();
        HFONT font = get_or_create_font(size, 0);
        HFONT old_font = (HFONT)SelectObject(g_text_dc, font);
        TEXTMETRICW tm;
        GetTextMetricsW(g_text_dc, &tm);
        SelectObject(g_text_dc, old_font);
        val = (int)tm.tmHeight;
    }
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

/* 内部辅助逻辑 */
EXPORT void zan_gui_text_stat_enable(i32 enabled) { (void)enabled; }

EXPORT i64 zan_gui_text_stat_read(i32 idx) { (void)idx; return 0; }

/* 内部辅助逻辑 */
EXPORT i32 zan_gui_set_text_family(const char *utf8_family) {
    (void)utf8_family;
    return 0;
}

EXPORT i32 zan_gui_font_face_matches(const char *utf8_family) {
    (void)utf8_family;
    return 0;
}

#endif /* _WIN32 */
