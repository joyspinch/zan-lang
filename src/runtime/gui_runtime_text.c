/* gui_runtime_text.c -- text rendering (Win32 GDI / Linux Xft + fallback) and the Win32
 * window shell with its custom title-bar window controls.
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
        if (g_font_sizes[i] == size && g_fonts[i]) return g_fonts[i];
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

EXPORT void zan_gui_draw_text(i64 surface_id, i64 x, i64 y,
                              const char *text, i64 color, i64 font_size) {
    if (surface_id < 0 || surface_id >= g_surface_count) return;
    zan_surface_t *s = g_surfaces[surface_id];
    if (!s || !text || !*text) return;

    int size = (int)font_size;
    if (size < 8) size = 8;

    ensure_text_dc();

    zan_text_cache_t *e = zan_text_cache_get(text, size);
    if (!e || !e->bits) return;
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

EXPORT i64 zan_gui_measure_text(const char *text, i64 font_size) {
    if (!text || !*text) return 0;
    int size = (int)font_size;
    if (size < 8) size = 8;

    ensure_text_dc();

    uint64_t h = zan_text_hash(text, size);
    int base = (int)(h % ZAN_MEAS_CACHE_CAP);
    zan_measure_cache_t *victim = NULL;
    uint64_t best = ~0ULL;
    for (int i = 0; i < ZAN_MEAS_CACHE_PROBE; i++) {
        zan_measure_cache_t *e = &g_mcache[(base + i) % ZAN_MEAS_CACHE_CAP];
        if (e->text && e->size == size && strcmp(e->text, text) == 0) {
            e->used = ++g_mcache_clock;
            return (i64)e->width;
        }
        if (!e->text) { if (!victim || best != 0) { victim = e; best = 0; } }
        else if (e->used < best) { best = e->used; victim = e; }
    }

    int w = measure_text_gdi(text, size);

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
    return (i64)w;
}

/* Font height depends only on size; only a handful of sizes are ever used. */
static int g_fh_size[16];
static int g_fh_val[16];
static int g_fh_count = 0;

EXPORT i64 zan_gui_font_height(i64 font_size) {
    int size = (int)font_size;
    if (size < 8) size = 8;

    for (int i = 0; i < g_fh_count; i++) {
        if (g_fh_size[i] == size) return (i64)g_fh_val[i];
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
    return (i64)val;
}


/* ========================================================================
 * Win32 Window Shell
 * ======================================================================== */
/* Compiled out when the unified SDL backend owns windowing (see ZAN_GUI_SDL
 * shell below); the Win32 GDI *text* rendering above still builds. */
#if !defined(ZAN_GUI_SDL)

static HWND g_main_hwnd = NULL;
/* Source window of the most recently reported event, so an app that owns more
 * than one top-level window (e.g. the IDE with a floating dialog) can route the
 * event to the right window. Set at the top of the window procedure. */
static HWND g_event_hwnd = NULL;
static int g_pending_event[8]; /* kind, x, y, button, keycode, modifiers, 0, 0 */
static int g_has_event = 0;
static int g_window_width = 0, g_window_height = 0;

/* --- Synthetic (driver/automation) event injection ------------------------
 * A UI-automation driver (see Gui.UiDriver) pushes synthetic input events so
 * the IDE can be driven programmatically, without moving the OS mouse or
 * synthesizing OS keystrokes. Enqueued events are drained at the head of
 * poll/wait_event exactly like real window messages, so the entire App +
 * widget dispatch path (hit-testing, focus, click targets) sees them
 * unchanged. This is what makes the IDE "AI-operable": target a widget by its
 * registered hit-region and inject the click, rather than screenshot+SetCursorPos. */
#define ZAN_INJECT_CAP 256
static int  g_inject_q[ZAN_INJECT_CAP][6]; /* kind,x,y,button,keycode,mods */
static HWND g_inject_hwnd[ZAN_INJECT_CAP];
static int  g_inject_head = 0, g_inject_tail = 0;
static CRITICAL_SECTION g_inject_cs;
static int  g_inject_cs_ready = 0;

static void zan_inject_lock(void) {
    if (!g_inject_cs_ready) {
        InitializeCriticalSection(&g_inject_cs);
        g_inject_cs_ready = 1;
    }
    EnterCriticalSection(&g_inject_cs);
}
static void zan_inject_unlock(void) { LeaveCriticalSection(&g_inject_cs); }

/* Pop one synthetic event into g_pending_event if any are queued. Returns 1
 * when an event was delivered (poll/wait then reports it as a normal event). */
static int zan_drain_injected(void) {
    int got = 0;
    zan_inject_lock();
    if (g_inject_head != g_inject_tail) {
        int *e = g_inject_q[g_inject_head];
        g_pending_event[0] = e[0]; g_pending_event[1] = e[1];
        g_pending_event[2] = e[2]; g_pending_event[3] = e[3];
        g_pending_event[4] = e[4]; g_pending_event[5] = e[5];
        HWND h = g_inject_hwnd[g_inject_head];
        g_event_hwnd = h ? h : g_main_hwnd;
        g_inject_head = (g_inject_head + 1) % ZAN_INJECT_CAP;
        g_has_event = 1;
        got = 1;
    }
    zan_inject_unlock();
    return got;
}
/* g_last_surface: most recently presented surface, re-blitted synchronously on
 * WM_SIZE/WM_PAINT so a maximize/resize fills the new client area immediately
 * instead of flashing black. g_bg_color: last frame's clear color, used to fill
 * the newly-exposed margin during a live resize. Both declared near the top. */
/* --- Windows 10/11 acrylic backdrop --------------------------------------
 * When enabled, the window becomes a layered window presented with per-pixel
 * alpha (UpdateLayeredWindow) and the DWM composites a real blurred+tinted
 * acrylic of whatever is behind it wherever the surface alpha is < 255. The
 * app clears the surface transparent under a glass theme, so the desktop shows
 * through the gaps while opaque widgets/text stay crisp.
 *
 * Robustness: the present hands UpdateLayeredWindow a NULL destination point
 * so it only updates the *content*, never the window position — the OS keeps
 * ownership of where the window is. That is what makes a title-bar drag stay
 * in sync (an earlier version repositioned the window on every present from a
 * stale GetWindowRect, which fought the modal move loop and left the content
 * torn/offset after a drag). */
enum {
    ZAN_ACCENT_DISABLED = 0,
    ZAN_ACCENT_ENABLE_BLURBEHIND = 3,
    ZAN_ACCENT_ENABLE_ACRYLICBLURBEHIND = 4
};
typedef struct {
    int AccentState;
    int AccentFlags;
    unsigned int GradientColor; /* AABBGGRR */
    int AnimationId;
} ZAN_ACCENT_POLICY;
typedef struct {
    int Attrib;      /* WCA_ACCENT_POLICY == 19 */
    void *pvData;
    size_t cbData;
} ZAN_WINCOMPATTRDATA;
typedef BOOL (WINAPI *ZAN_pSetWCA)(HWND, ZAN_WINCOMPATTRDATA *);

static int g_glass_on = 0;
static u32 g_glass_tint = 0; /* ARGB from Zan (0 -> a light default) */

static void acrylic_apply(HWND hwnd, int state, u32 tint_argb) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    ZAN_pSetWCA setWCA = (ZAN_pSetWCA)(void *)
        GetProcAddress(user32, "SetWindowCompositionAttribute");
    if (!setWCA) return;
    u32 a = (tint_argb >> 24) & 0xFF, r = (tint_argb >> 16) & 0xFF;
    u32 g = (tint_argb >> 8) & 0xFF, b = tint_argb & 0xFF;
    ZAN_ACCENT_POLICY accent;
    accent.AccentState = state;
    accent.AccentFlags = 0;
    accent.GradientColor = (a << 24) | (b << 16) | (g << 8) | r; /* -> ABGR */
    accent.AnimationId = 0;
    ZAN_WINCOMPATTRDATA data;
    data.Attrib = 19; /* WCA_ACCENT_POLICY */
    data.pvData = &accent;
    data.cbData = sizeof(accent);
    setWCA(hwnd, &data);
}

/* Present a layered window: build a premultiplied top-down BGRA DIB from the
 * straight-alpha surface and hand it to the DWM via UpdateLayeredWindow. The
 * memory DC + DIB section are cached across frames (rebuilt only when the
 * surface size changes) so a live drag/animation doesn't churn GDI objects. */
static HDC g_lw_mem = NULL;
static HBITMAP g_lw_dib = NULL;
static HGDIOBJ g_lw_old = NULL;
static u32 *g_lw_bits = NULL;
static int g_lw_w = 0, g_lw_h = 0;

static void present_layered(HWND hwnd, zan_surface_t *s) {
    if (g_lw_mem == NULL || g_lw_w != s->width || g_lw_h != s->height) {
        if (g_lw_mem) {
            SelectObject(g_lw_mem, g_lw_old);
            DeleteObject(g_lw_dib);
            DeleteDC(g_lw_mem);
        }
        HDC screen = GetDC(NULL);
        g_lw_mem = CreateCompatibleDC(screen);
        ReleaseDC(NULL, screen);
        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = s->width;
        bmi.bmiHeader.biHeight = -(int)s->height; /* top-down */
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void *bits = NULL;
        g_lw_dib = CreateDIBSection(g_lw_mem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (!g_lw_dib) { DeleteDC(g_lw_mem); g_lw_mem = NULL; return; }
        g_lw_old = SelectObject(g_lw_mem, g_lw_dib);
        g_lw_bits = (u32 *)bits;
        g_lw_w = s->width;
        g_lw_h = s->height;
    }
    u32 *d = g_lw_bits;
    int n = s->width * s->height;
    for (int i = 0; i < n; i++) {
        u32 p = s->pixels[i];
        u32 a = (p >> 24) & 0xFF;
        u32 r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
        /* premultiply for AC_SRC_ALPHA */
        r = r * a / 255; g = g * a / 255; b = b * a / 255;
        d[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    HDC screen = GetDC(NULL);
    SIZE sz = { s->width, s->height };
    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION bf; bf.BlendOp = AC_SRC_OVER; bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255; bf.AlphaFormat = AC_SRC_ALPHA;
    /* pptDst = NULL: keep the OS-managed window position (never reposition on a
     * content update), so a drag never tears/offsets the window. */
    UpdateLayeredWindow(hwnd, screen, NULL, &sz, g_lw_mem, &ptSrc, 0, &bf, ULW_ALPHA);
    ReleaseDC(NULL, screen);
}

/* Enable native glass on a window. tint_argb: ARGB tint (alpha ~0x40-0xA0
 * reads as frost strength). Idempotent; safe to call every theme change. */
EXPORT i64 zan_gui_enable_glass(i64 hwnd_val, i64 tint_argb) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    if (!hwnd) return 1;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (!(ex & WS_EX_LAYERED)) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    }
    g_glass_tint = (u32)tint_argb;
    acrylic_apply(hwnd, ZAN_ACCENT_ENABLE_ACRYLICBLURBEHIND, g_glass_tint);
    g_glass_on = 1;
    /* Present the current frame immediately so the layered window has valid
     * content the moment the style flips (otherwise it can flash empty). */
    i64 owng = (i64)(intptr_t)GetPropW(hwnd, L"ZanSurfId") - 1;
    if (owng >= 0 && owng < g_surface_count && g_surfaces[owng])
        present_layered(hwnd, g_surfaces[owng]);
    return 0;
}

/* Disable native glass and drop the layered style, back to opaque GDI present. */
EXPORT i64 zan_gui_disable_glass(i64 hwnd_val) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    if (!hwnd) return 1;
    acrylic_apply(hwnd, ZAN_ACCENT_DISABLED, 0);
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
    g_glass_on = 0;
    return 0;
}

static void blit_surface_to_hwnd(HWND hwnd, zan_surface_t *s) {
    if (!s) return;
    if (g_glass_on) { present_layered(hwnd, s); return; }
    HDC hdc = GetDC(hwnd);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = s->width;
    bmi.bmiHeader.biHeight = -(int)s->height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    /* Blit the last frame crisply at 1:1 (never stretched — stretching looks
     * rubbery/blurry during a drag-resize). Any client area beyond the frame is
     * filled with the canvas background so a grow-resize shows solid bg in the
     * new region until the app thread repaints it crisply. */
    SetDIBitsToDevice(hdc, 0, 0, (DWORD)s->width, (DWORD)s->height,
        0, 0, 0, (UINT)s->height, s->pixels, &bmi, DIB_RGB_COLORS);
    if (g_window_width > (int)s->width || g_window_height > (int)s->height) {
        HBRUSH br = CreateSolidBrush(RGB((g_bg_color >> 16) & 0xFF,
                                         (g_bg_color >> 8) & 0xFF,
                                          g_bg_color & 0xFF));
        if (g_window_width > (int)s->width) {
            RECT r = { (int)s->width, 0, g_window_width, g_window_height };
            FillRect(hdc, &r, br);
        }
        if (g_window_height > (int)s->height) {
            RECT r = { 0, (int)s->height, (int)s->width, g_window_height };
            FillRect(hdc, &r, br);
        }
        DeleteObject(br);
    }
    ReleaseDC(hwnd, hdc);
}
/* Borderless (client-covers-everything) window chrome metrics, in device px. */
static int g_dpi = 96;
static int g_titlebar_h = 32;
static int g_btn_w = 46;
/* Number of caption buttons drawn on the right of the title bar (theme, pin,
 * minimize, maximize, close). The rightmost N*btn_w px are treated as client
 * area (clickable) rather than draggable caption. */
static int g_caption_btn_count = 5;

/* Caret position (client-area px) reported by the focused text widget so the
 * IME composition + candidate windows follow the cursor instead of pinning to
 * the window origin. Updated from Zan via zan_gui_set_ime_pos each frame. */
static int g_ime_x = 0;
static int g_ime_y = 0;

/* Move the active input context's composition + candidate windows to the caret.
 * Safe to call whenever; no-op if the window has no IMM context. */
static void zan_ime_reposition(HWND hwnd) {
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) { return; }
    COMPOSITIONFORM cf;
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = g_ime_x;
    cf.ptCurrentPos.y = g_ime_y;
    ImmSetCompositionWindow(himc, &cf);
    CANDIDATEFORM cand;
    cand.dwIndex = 0;
    cand.dwStyle = CFS_CANDIDATEPOS;
    cand.ptCurrentPos.x = g_ime_x;
    cand.ptCurrentPos.y = g_ime_y;
    cand.rcArea.left = 0;
    cand.rcArea.top = 0;
    cand.rcArea.right = 0;
    cand.rcArea.bottom = 0;
    ImmSetCandidateWindow(himc, &cand);
    ImmReleaseContext(hwnd, himc);
}

static LRESULT CALLBACK ZanGuiWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    g_event_hwnd = hwnd;
    switch (msg) {
    case WM_IME_STARTCOMPOSITION:
        /* Position the composition window at the caret before the IME draws it,
         * then let DefWindowProc run the normal composition handling. */
        zan_ime_reposition(hwnd);
        break;
    case WM_IME_COMPOSITION:
        zan_ime_reposition(hwnd);
        break;
    case WM_IME_SETCONTEXT:
        zan_ime_reposition(hwnd);
        break;
    case WM_DESTROY:
        g_pending_event[0] = 8;
        g_has_event = 1;
        /* Only tear the whole app down when the primary window closes; a
         * secondary window (dialog) just reports kind 8 for its own hwnd. */
        if (hwnd == g_main_hwnd) { PostQuitMessage(0); }
        return 0;
    case WM_ENTERSIZEMOVE:
        /* Full acrylic re-blurs the desktop on every move, so dragging can
         * stutter. Downgrade to plain blur-behind for the modal move/size loop:
         * it is far cheaper (smooth drag) yet still frosts the backdrop so the
         * window never looks see-through, then restore full acrylic on release. */
        if (g_glass_on) {
            acrylic_apply(hwnd, ZAN_ACCENT_ENABLE_BLURBEHIND, g_glass_tint);
        }
        break;
    case WM_EXITSIZEMOVE:
        if (g_glass_on) {
            acrylic_apply(hwnd, ZAN_ACCENT_ENABLE_ACRYLICBLURBEHIND, g_glass_tint);
            /* Re-present the current frame at the settled position so the
             * layered content is fresh and correctly aligned after the drag. */
            if (g_last_surface >= 0 && g_last_surface < g_surface_count)
                present_layered(hwnd, g_surfaces[g_last_surface]);
        }
        break;
    case WM_SIZE:
        g_window_width = LOWORD(lp);
        g_window_height = HIWORD(lp);
        /* Fill the resized client area now (last frame at 1:1 + bg-colored
         * margin) so a maximize/drag doesn't flash black before the app thread
         * repaints crisply. Skipped under native glass: a layered present here
         * would fight the resize with the old surface size. */
        if (!g_glass_on && g_last_surface >= 0 && g_last_surface < g_surface_count)
            blit_surface_to_hwnd(hwnd, g_surfaces[g_last_surface]);
        g_pending_event[0] = 7;
        g_pending_event[1] = g_window_width;
        g_pending_event[2] = g_window_height;
        g_has_event = 1;
        return 0;
    case WM_MOUSEMOVE:
        g_pending_event[0] = 1;
        g_pending_event[1] = (short)LOWORD(lp);
        g_pending_event[2] = (short)HIWORD(lp);
        g_has_event = 1;
        return 0;
    case WM_LBUTTONDOWN:
        g_pending_event[0] = 2; g_pending_event[3] = 0;
        g_pending_event[1] = (short)LOWORD(lp);
        g_pending_event[2] = (short)HIWORD(lp);
        g_pending_event[5] = (GetKeyState(VK_CONTROL) & 0x8000 ? 1 : 0) |
                             (GetKeyState(VK_SHIFT) & 0x8000 ? 2 : 0) |
                             (GetKeyState(VK_MENU) & 0x8000 ? 4 : 0);
        g_has_event = 1;
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        g_pending_event[0] = 3; g_pending_event[3] = 0;
        g_pending_event[1] = (short)LOWORD(lp);
        g_pending_event[2] = (short)HIWORD(lp);
        g_pending_event[5] = (GetKeyState(VK_CONTROL) & 0x8000 ? 1 : 0) |
                             (GetKeyState(VK_SHIFT) & 0x8000 ? 2 : 0) |
                             (GetKeyState(VK_MENU) & 0x8000 ? 4 : 0);
        g_has_event = 1;
        ReleaseCapture();
        return 0;
    case WM_RBUTTONDOWN:
        g_pending_event[0] = 2; g_pending_event[3] = 1;
        g_pending_event[1] = (short)LOWORD(lp);
        g_pending_event[2] = (short)HIWORD(lp);
        g_pending_event[5] = (GetKeyState(VK_CONTROL) & 0x8000 ? 1 : 0) |
                             (GetKeyState(VK_SHIFT) & 0x8000 ? 2 : 0) |
                             (GetKeyState(VK_MENU) & 0x8000 ? 4 : 0);
        g_has_event = 1;
        return 0;
    case WM_RBUTTONUP:
        g_pending_event[0] = 3; g_pending_event[3] = 1;
        g_pending_event[1] = (short)LOWORD(lp);
        g_pending_event[2] = (short)HIWORD(lp);
        g_has_event = 1;
        return 0;
    case WM_KEYDOWN:
        g_pending_event[0] = 4;
        g_pending_event[4] = (int)wp;
        g_pending_event[5] = (GetKeyState(VK_CONTROL) & 0x8000 ? 1 : 0) |
                             (GetKeyState(VK_SHIFT) & 0x8000 ? 2 : 0) |
                             (GetKeyState(VK_MENU) & 0x8000 ? 4 : 0);
        g_has_event = 1;
        return 0;
    case WM_KEYUP:
        g_pending_event[0] = 5;
        g_pending_event[4] = (int)wp;
        g_has_event = 1;
        return 0;
    case WM_CHAR:
        g_pending_event[0] = 6;
        g_pending_event[4] = (int)wp;
        g_has_event = 1;
        return 0;
    case WM_MOUSEWHEEL: {
        POINT wheel_pt = {
            (int)(short)LOWORD(lp), (int)(short)HIWORD(lp)
        };
        ScreenToClient(hwnd, &wheel_pt);
        g_pending_event[1] = wheel_pt.x;
        g_pending_event[2] = wheel_pt.y;
        g_pending_event[0] = 13;
        g_pending_event[4] = GET_WHEEL_DELTA_WPARAM(wp);
        g_has_event = 1;
        return 0;
    }
    case WM_NCCALCSIZE:
        /* Remove the standard window frame so the client area covers the whole
         * window (custom, themeable title bar). Keep the frame inset when
         * maximized so content is not clipped off-screen. */
        if (wp) {
            WINDOWPLACEMENT wpl; wpl.length = sizeof(wpl);
            if (GetWindowPlacement(hwnd, &wpl) && wpl.showCmd == SW_SHOWMAXIMIZED) {
                NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)lp;
                int fx = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                int fy = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                p->rgrc[0].left += fx;
                p->rgrc[0].right -= fx;
                p->rgrc[0].top += fy;
                p->rgrc[0].bottom -= fy;
            }
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        POINT pt; pt.x = (short)LOWORD(lp); pt.y = (short)HIWORD(lp);
        RECT rc; GetWindowRect(hwnd, &rc);
        int x = (int)pt.x - rc.left;
        int y = (int)pt.y - rc.top;
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        int b = 8 * g_dpi / 96;
        WINDOWPLACEMENT wpl; wpl.length = sizeof(wpl);
        int maxed = (GetWindowPlacement(hwnd, &wpl) && wpl.showCmd == SW_SHOWMAXIMIZED);
        int inButtons = (y < g_titlebar_h && x >= w - g_caption_btn_count * g_btn_w);
        if (!maxed && !inButtons) {
            int left = x < b, right = x >= w - b, top = y < b, bottom = y >= h - b;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }
        if (inButtons) return HTCLIENT;
        if (y < g_titlebar_h) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        /* Repaint from THIS window's own last frame so OS-driven invalidations
         * (move, uncover, DWM) never leave the client black between app
         * repaints -- and never paint another window's frame here. A layered
         * (glass) window keeps its own composited bitmap, so WM_PAINT need not
         * re-present it. */
        i64 own = (i64)(intptr_t)GetPropW(hwnd, L"ZanSurfId") - 1;
        if (!g_glass_on && own >= 0 && own < g_surface_count && g_surfaces[own])
            blit_surface_to_hwnd(hwnd, g_surfaces[own]);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            return TRUE; /* prevent default cursor reset */
        }
        break;
    case WM_ERASEBKGND:
        return 1; /* prevent flicker */
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* Enable DPI awareness without a static Shcore.dll dependency so the binary
 * still loads on Windows 7. Prefer per-monitor awareness (SetProcessDpiAwareness,
 * Shcore.dll, Windows 8.1+); fall back to system-DPI awareness
 * (SetProcessDPIAware, user32.dll, Vista+) when Shcore is unavailable. */
static void zan_enable_dpi_awareness(void) {
    HMODULE shcore = LoadLibraryW(L"Shcore.dll");
    if (shcore) {
        typedef HRESULT (WINAPI *SetProcessDpiAwarenessFn)(PROCESS_DPI_AWARENESS);
        SetProcessDpiAwarenessFn set_awareness =
            (SetProcessDpiAwarenessFn)GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (set_awareness) {
            set_awareness(PROCESS_PER_MONITOR_DPI_AWARE);
            FreeLibrary(shcore);
            return;
        }
        FreeLibrary(shcore);
    }
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef BOOL (WINAPI *SetProcessDPIAwareFn)(void);
        SetProcessDPIAwareFn set_aware =
            (SetProcessDPIAwareFn)GetProcAddress(user32, "SetProcessDPIAware");
        if (set_aware) set_aware();
    }
}

EXPORT i64 zan_gui_create_window(const char *title, i64 width, i64 height) {
    static int registered = 0;
    static int dpi_set = 0;
    if (!dpi_set) {
        zan_enable_dpi_awareness();
        dpi_set = 1;
    }
    if (!registered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ZanGuiWndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorA(NULL, MAKEINTRESOURCEA(32512));
        wc.hbrBackground = NULL; /* no background brush - prevents flicker */
        /* App icon (resource id 1, embedded via zan.rc) for taskbar/alt-tab. */
        wc.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
        wc.hIconSm = wc.hIcon;
        wc.lpszClassName = L"ZanGuiApp";
        wc.style = CS_OWNDC | CS_DBLCLKS;
        RegisterClassExW(&wc);
        registered = 1;
    }

    int wtitle_len = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
    wchar_t *wtitle = (wchar_t *)malloc((size_t)wtitle_len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, wtitle_len);

    /* Scale requested size by system DPI */
    HDC screenDC = GetDC(NULL);
    int dpi = GetDeviceCaps(screenDC, LOGPIXELSX);
    ReleaseDC(NULL, screenDC);
    int scaledW = (int)width * dpi / 96;
    int scaledH = (int)height * dpi / 96;

    g_dpi = dpi;
    g_titlebar_h = 32 * dpi / 96;
    g_btn_w = 46 * dpi / 96;

    RECT r = {0, 0, (LONG)scaledW, (LONG)scaledH};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);

    g_window_width = scaledW;
    g_window_height = scaledH;

    HWND hwnd = CreateWindowExW(
        0, L"ZanGuiApp", wtitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top,
        NULL, NULL, GetModuleHandleW(NULL), NULL
    );
    free(wtitle);
    if (!g_main_hwnd) {
        g_main_hwnd = hwnd;
    } else {
        RECT parent_rect;
        RECT child_rect;
        if (GetWindowRect(g_main_hwnd, &parent_rect) &&
            GetWindowRect(hwnd, &child_rect)) {
            int child_w = child_rect.right - child_rect.left;
            int child_h = child_rect.bottom - child_rect.top;
            int x = parent_rect.left +
                ((parent_rect.right - parent_rect.left) - child_w) / 2;
            int y = parent_rect.top +
                ((parent_rect.bottom - parent_rect.top) - child_h) / 2;
            MONITORINFO monitor = { sizeof(MONITORINFO) };
            HMONITOR hm = MonitorFromWindow(
                g_main_hwnd, MONITOR_DEFAULTTONEAREST);
            if (GetMonitorInfoW(hm, &monitor)) {
                if (x < monitor.rcWork.left) x = monitor.rcWork.left;
                if (y < monitor.rcWork.top) y = monitor.rcWork.top;
                if (x + child_w > monitor.rcWork.right)
                    x = monitor.rcWork.right - child_w;
                if (y + child_h > monitor.rcWork.bottom)
                    y = monitor.rcWork.bottom - child_h;
            }
            SetWindowPos(hwnd, NULL, x, y, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    /* Restore the system drop shadow on the borderless window. Extending the
     * DWM frame by a 1px sheet re-enables the shadow + rounded corners while
     * WM_NCCALCSIZE still hides the standard frame. */
    MARGINS shadow = {0, 0, 0, 1};
    DwmExtendFrameIntoClientArea(hwnd, &shadow);
    /* Apply the frame removal (WM_NCCALCSIZE) right away. */
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return (i64)(intptr_t)hwnd;
}

EXPORT i64 zan_gui_show_window(i64 hwnd_val) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    /* Ensure the window is focused so wheel/keyboard events route to it. */
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    return 0;
}

/* ---- Custom title bar window controls ---- */
EXPORT i64 zan_gui_minimize(i64 hwnd_val) {
    ShowWindow((HWND)(intptr_t)hwnd_val, SW_MINIMIZE);
    return 0;
}

EXPORT i64 zan_gui_toggle_maximize(i64 hwnd_val) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    WINDOWPLACEMENT wpl; wpl.length = sizeof(wpl);
    if (GetWindowPlacement(hwnd, &wpl) && wpl.showCmd == SW_SHOWMAXIMIZED) {
        ShowWindow(hwnd, SW_RESTORE);
    } else {
        ShowWindow(hwnd, SW_MAXIMIZE);
    }
    return 0;
}

EXPORT i64 zan_gui_close_window(i64 hwnd_val) {
    PostMessageW((HWND)(intptr_t)hwnd_val, WM_CLOSE, 0, 0);
    return 0;
}

EXPORT i64 zan_gui_destroy_window(i64 hwnd_val) {
    DestroyWindow((HWND)(intptr_t)hwnd_val);
    return 0;
}

EXPORT i64 zan_gui_is_maximized(i64 hwnd_val) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    WINDOWPLACEMENT wpl; wpl.length = sizeof(wpl);
    if (GetWindowPlacement(hwnd, &wpl) && wpl.showCmd == SW_SHOWMAXIMIZED) { return 1; }
    return 0;
}

/* 1 while the window can be seen (not minimized/hidden); ambient animations
 * pause while this reports 0. */
EXPORT i64 zan_gui_window_visible(i64 hwnd_val) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    if (IsIconic(hwnd) || !IsWindowVisible(hwnd)) return 0;
    return 1;
}

EXPORT i64 zan_gui_titlebar_height(void) { return g_titlebar_h; }
EXPORT i64 zan_gui_caption_button_width(void) { return g_btn_w; }

/* Set the number of caption buttons so the draggable caption region excludes
 * exactly the button cluster on the right. */
EXPORT i64 zan_gui_set_caption_buttons(i64 hwnd_val, i64 count) {
    (void)hwnd_val;
    if (count >= 0 && count <= 8) { g_caption_btn_count = (int)count; }
    return 0;
}

/* Toggle always-on-top (topmost) state for the window. */
EXPORT i64 zan_gui_set_topmost(i64 hwnd_val, i64 on) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    HWND after = on ? HWND_TOPMOST : HWND_NOTOPMOST;
    SetWindowPos(hwnd, after, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return 0;
}

EXPORT i64 zan_gui_poll_event(void) {
    g_has_event = 0;
    memset(g_pending_event, 0, sizeof(g_pending_event));
    if (zan_drain_injected()) return 0;
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return -1;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (g_has_event) return 0;
    }
    return 1;
}

EXPORT i64 zan_gui_wait_event(void) {
    g_has_event = 0;
    memset(g_pending_event, 0, sizeof(g_pending_event));
    if (zan_drain_injected()) return 0;
    MSG msg;
    if (GetMessageW(&msg, NULL, 0, 0) <= 0) return -1;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
    return 0;
}

/* Like wait_event but gives up after `ms` milliseconds. Returns 0 when an
 * event was delivered, 1 on timeout, -1 on quit. Lets an animation loop idle
 * in the kernel until either input arrives or its next frame deadline. */
EXPORT i64 zan_gui_wait_event_timeout(i64 ms) {
    g_has_event = 0;
    memset(g_pending_event, 0, sizeof(g_pending_event));
    if (zan_drain_injected()) return 0;
    if (ms < 0) ms = 0;
    DWORD start = GetTickCount();
    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return -1;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (g_has_event) return 0;
        }
        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= (DWORD)ms) return 1;
        DWORD wr = MsgWaitForMultipleObjects(0, NULL, FALSE,
                                             (DWORD)ms - elapsed, QS_ALLINPUT);
        if (wr == WAIT_TIMEOUT) return 1;
    }
}

/* Wake a UI thread blocked in wait_event so it can drain the dispatch queue.
 * PostMessageW is documented thread-safe, so this is callable from any
 * thread. The posted WM_NULL carries no event; wait_event returns kind 0. */
EXPORT i64 zan_gui_wake(void) {
    if (g_main_hwnd) { PostMessageW(g_main_hwnd, WM_NULL, 0, 0); }
    return 0;
}

/* Queue a synthetic input event for the automation driver. hwnd_val=0 targets
 * the main window. kind/x/y/button/keycode/mods use the same encoding as real
 * events (see zan_gui_event_kind). Thread-safe. */
EXPORT i64 zan_gui_inject_event(i64 hwnd_val, i64 kind, i64 x, i64 y,
                                i64 button, i64 keycode, i64 mods) {
    zan_inject_lock();
    int nt = (g_inject_tail + 1) % ZAN_INJECT_CAP;
    if (nt != g_inject_head) { /* drop silently when the queue is full */
        int *e = g_inject_q[g_inject_tail];
        e[0] = (int)kind;    e[1] = (int)x;       e[2] = (int)y;
        e[3] = (int)button;  e[4] = (int)keycode; e[5] = (int)mods;
        g_inject_hwnd[g_inject_tail] =
            hwnd_val ? (HWND)(intptr_t)hwnd_val : g_main_hwnd;
        g_inject_tail = nt;
    }
    zan_inject_unlock();
    /* Wake a UI thread blocked in wait_event so the event is served promptly. */
    if (g_main_hwnd) { PostMessageW(g_main_hwnd, WM_NULL, 0, 0); }
    return 0;
}

/* Number of synthetic events still queued (0 = the driver's last batch has
 * been fully consumed by the event loop). */
EXPORT i64 zan_gui_inject_pending(void) {
    int n;
    zan_inject_lock();
    n = (g_inject_tail - g_inject_head + ZAN_INJECT_CAP) % ZAN_INJECT_CAP;
    zan_inject_unlock();
    return n;
}

EXPORT i64 zan_gui_event_kind(void)    { return g_pending_event[0]; }
EXPORT i64 zan_gui_event_x(void)       { return g_pending_event[1]; }
EXPORT i64 zan_gui_event_y(void)       { return g_pending_event[2]; }
EXPORT i64 zan_gui_event_button(void)  { return g_pending_event[3]; }
EXPORT i64 zan_gui_event_keycode(void) { return g_pending_event[4]; }
EXPORT i64 zan_gui_event_mods(void)    { return g_pending_event[5]; }

EXPORT i64 zan_gui_window_width(void)  { return g_window_width; }
EXPORT i64 zan_gui_window_height(void) { return g_window_height; }

EXPORT i64 zan_gui_event_hwnd(void)    { return (i64)(intptr_t)g_event_hwnd; }

EXPORT i64 zan_gui_client_width(i64 hwnd_val) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    RECT rc;
    if (!hwnd || !GetClientRect(hwnd, &rc)) { return 0; }
    return rc.right - rc.left;
}

EXPORT i64 zan_gui_client_height(i64 hwnd_val) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    RECT rc;
    if (!hwnd || !GetClientRect(hwnd, &rc)) { return 0; }
    return rc.bottom - rc.top;
}

EXPORT i64 zan_gui_present(i64 hwnd_val, i64 surface_id) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    if (surface_id < 0 || surface_id >= g_surface_count || !g_surfaces[surface_id]) return 1;
    g_last_surface = surface_id;
    /* Remember which surface belongs to THIS window so WM_PAINT can repaint
     * it from its own frame. A single global last-surface repainted whichever
     * window asked, so with two windows open an OS invalidation of the main
     * window blitted the dialog's frame into it (ghost dialogs). */
    SetPropW(hwnd, L"ZanSurfId", (HANDLE)(intptr_t)(surface_id + 1));
    blit_surface_to_hwnd(hwnd, g_surfaces[surface_id]);
    return 0;
}

/* Whole-window opacity, 10..100 percent. Uses a constant-alpha layered window
 * (independent from the per-pixel-alpha glass present). 100 removes the
 * layered style so the normal opaque present path is restored. */
EXPORT i64 zan_gui_set_opacity(i64 hwnd_val, i64 percent) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    if (!hwnd) return 1;
    if (percent < 10) percent = 10;
    if (percent > 100) percent = 100;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (percent >= 100) {
        if (!g_glass_on && (ex & WS_EX_LAYERED)) {
            SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
        }
        return 0;
    }
    if (!(ex & WS_EX_LAYERED)) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    }
    SetLayeredWindowAttributes(hwnd, 0, (BYTE)(percent * 255 / 100), LWA_ALPHA);
    return 0;
}

EXPORT i64 zan_gui_set_title(i64 hwnd_val, const char *title) {
    HWND hwnd = (HWND)(intptr_t)hwnd_val;
    int len = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
    wchar_t *wt = (wchar_t *)malloc((size_t)len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wt, len);
    SetWindowTextW(hwnd, wt);
    free(wt);
    return 0;
}

EXPORT i64 zan_gui_set_cursor(i64 cursor_type) {
    HCURSOR c;
    switch ((int)cursor_type) {
        case 0: c = LoadCursorA(NULL, MAKEINTRESOURCEA(32512)); break;
        case 1: c = LoadCursorA(NULL, MAKEINTRESOURCEA(32649)); break;
        case 2: c = LoadCursorA(NULL, MAKEINTRESOURCEA(32513)); break;
        case 3: c = LoadCursorA(NULL, MAKEINTRESOURCEA(32644)); break; /* SIZEWE */
        case 4: c = LoadCursorA(NULL, MAKEINTRESOURCEA(32645)); break; /* SIZENS */
        case 5: c = LoadCursorA(NULL, MAKEINTRESOURCEA(32515)); break; /* CROSS */
        default: c = LoadCursorA(NULL, MAKEINTRESOURCEA(32512)); break;
    }
    SetCursor(c);
    return 0;
}

EXPORT i64 zan_gui_get_dpi_scale(void) {
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    return (i64)(dpi * 100 / 96);
}

EXPORT i64 zan_gui_get_tick_ms(void) {
    /* GetTickCount64 only updates every ~10-16ms, so consecutive reads a frame
     * apart often return the same value -> per-frame dt jitters between 0 and
     * ~16ms and animations step unevenly (visible stutter). Use the
     * high-resolution performance counter for smooth, monotonic ms. */
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) { QueryPerformanceFrequency(&freq); }
    QueryPerformanceCounter(&now);
    return (i64)(now.QuadPart * 1000 / freq.QuadPart);
}

EXPORT void zan_gui_sleep_ms(i64 ms) {
    if (ms > 0) Sleep((DWORD)ms);
}

/* Copy a UTF-8 string to the Windows clipboard as CF_UNICODETEXT.
 * Returns 0 on success, -1 on failure. */
EXPORT i64 zan_gui_set_clipboard(const char *utf8) {
    if (!utf8) return -1;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wlen <= 0) return -1;
    HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, (size_t)wlen * sizeof(WCHAR));
    if (!hmem) return -1;
    WCHAR *dst = (WCHAR *)GlobalLock(hmem);
    if (!dst) { GlobalFree(hmem); return -1; }
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, dst, wlen);
    GlobalUnlock(hmem);
    if (!OpenClipboard(g_main_hwnd)) { GlobalFree(hmem); return -1; }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, hmem)) {
        CloseClipboard();
        GlobalFree(hmem);
        return -1;
    }
    CloseClipboard();
    /* Ownership of hmem transferred to the clipboard; do not free. */
    return 0;
}

/* Report the caret position (client-area px) so the IME composition/candidate
 * windows follow the cursor. Called each frame by the focused text widget. */
EXPORT void zan_gui_set_ime_pos(i64 x, i64 y) {
    g_ime_x = (int)x;
    g_ime_y = (int)y;
    if (g_event_hwnd) { zan_ime_reposition(g_event_hwnd); }
}

/* Read CF_UNICODETEXT from the Windows clipboard as a UTF-8 string. Returns a
 * pointer valid until the next call (the previous buffer is freed each time),
 * or "" when the clipboard holds no text. The Zan `string` ABI is a plain
 * NUL-terminated i8*, so a heap char* can be handed back directly. */
EXPORT const char *zan_gui_get_clipboard(void) {
    static char *g_clip_buf = NULL;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return "";
    if (!OpenClipboard(g_main_hwnd)) return "";
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) { CloseClipboard(); return ""; }
    WCHAR *w = (WCHAR *)GlobalLock(h);
    if (!w) { CloseClipboard(); return ""; }
    int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *nb = (char *)malloc((size_t)(need > 0 ? need : 1));
    if (nb) {
        if (need > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, nb, need, NULL, NULL);
        else nb[0] = '\0';
    }
    GlobalUnlock(h);
    CloseClipboard();
    if (!nb) return "";
    free(g_clip_buf);
    g_clip_buf = nb;
    return g_clip_buf;
}

/* Write a UTF-8 string to a file (overwrite), prefixed with a UTF-8 BOM so
 * spreadsheet apps detect the encoding (important for CJK CSV export).
 * Returns 0 on success, -1 on failure. */
EXPORT i64 zan_gui_write_file(const char *path, const char *utf8) {
    if (!path || !utf8) return -1;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wlen <= 0) return -1;
    WCHAR *wpath = (WCHAR *)malloc((size_t)wlen * sizeof(WCHAR));
    if (!wpath) return -1;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);
    HANDLE hf = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    free(wpath);
    if (hf == INVALID_HANDLE_VALUE) return -1;
    DWORD wr = 0;
    const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    WriteFile(hf, bom, 3, &wr, NULL);
    size_t len = strlen(utf8);
    BOOL ok = WriteFile(hf, utf8, (DWORD)len, &wr, NULL);
    CloseHandle(hf);
    if (!ok) return -1;
    return 0;
}

#endif /* !ZAN_GUI_SDL (Win32 window shell) */

#endif /* _WIN32 */
